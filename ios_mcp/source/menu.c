/*
 *   Copyright (C) 2022 GaryOderNichts
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "menu.h"

#include "imports.h"
#include "gfx.h"
#include "utils.h"
#include "fsa.h"
#include "socket.h"
#include "netconf.h"
#include "mcp_install.h"
#include "sci.h"
#include "mcp_misc.h"
#include "mdinfo.h"
#include "dumper.h"

#include <string.h>
#include <unistd.h>
#include <stdarg.h>

#include "StartWupserver.h"
#include "PairDRC.h"
#include "DebugSystemRegion.h"
#include "SystemInformation.h"
#include "SubmitSystemData.h"
#include "file_check.h"
#include "Installer.h"


static void option_dumpNand(void);
static void option_SetColdbootTitle(void);
static void option_DumpSyslogs(void);
static void option_DumpOtpAndSeeprom(void);
static void option_LoadNetConf(void);
static void option_EditParental(void);
static void option_SetInitialLaunch(void);
static void option_Shutdown(void);
static void option_checkMLC(void);
static void option_checkSLC(void);
static void option_formatMlc(void);
static void option_cloneMlc(void);
static void option_dumpSlcCloneMlc(void);
static void option_flashBoot1(void);

extern int ppcHeartBeatThreadId;
extern uint64_t currentColdbootOS;
extern uint64_t currentColdbootTitle;

int fsaHandle = -1;

static const Menu mainMenuOptions[] = {
    {"Dump SLC + MLC",              {.callback = option_dumpNand}},
    {"Dump SLC + Clone MLC",        {.callback = option_dumpSlcCloneMlc}},
    {"Clone MLC",                   {.callback = option_cloneMlc}},
    {"Check MLC",                   {.callback = option_checkMLC}},
    {"Check SLC",                   {.callback = option_checkSLC}},
    {"Set Initinal Launch",         {.callback = option_SetInitialLaunch}},
    {"Flash boot1.img to slcmpt",   {.callback = option_flashBoot1}},
    //{"Format MLC (Brick Mii)",      {.callback = option_formatMlc}},
    {"Set Coldboot Title",          {.callback = option_SetColdbootTitle}},
    {"Dump Syslogs",                {.callback = option_DumpSyslogs}},
    {"Dump OTP + SEEPROM",          {.callback = option_DumpOtpAndSeeprom}},
    {"Start wupserver",             {.callback = option_StartWupserver}},
    {"Load Network Configuration",  {.callback = option_LoadNetConf}},
    {"Pair Gamepad",                {.callback = option_PairDRC}},
    {"Install WUP",                 {.callback = option_InstallWUP}},
    {"Edit Parental Controls",      {.callback = option_EditParental}},
    {"Debug System Region",         {.callback = option_DebugSystemRegion}},
    {"System Information",          {.callback = option_SystemInformation}},
    {"Submit System Data",          {.callback = option_SubmitSystemData}},
    {"Shutdown",                    {.callback = option_Shutdown}},
};

/**
 * Draw the top bar.
 * @param title Title
 */
void drawTopBar(const char* title)
{
    // draw top bar
    gfx_set_font_color(COLOR_PRIMARY);
    gfx_print((SCREEN_WIDTH / 2) + (gfx_get_text_width(title) / 2), 8, 1, title);
    gfx_draw_rect_filled(8, 16 + 8, SCREEN_WIDTH - 8 * 2, 2, COLOR_SECONDARY);
}

static void drawBars(const char* title)
{
    drawTopBar(title);

    // draw bottom bar
    gfx_draw_rect_filled(8, SCREEN_HEIGHT - (16 + 8 + 2), SCREEN_WIDTH - 8 * 2, 2, COLOR_SECONDARY);
    gfx_print(16, SCREEN_HEIGHT - CHAR_SIZE_DRC_Y - 4, 0, "EJECT: Navigate");
    gfx_print(SCREEN_WIDTH - 16, SCREEN_HEIGHT - CHAR_SIZE_DRC_Y - 4, GfxPrintFlag_AlignRight, "POWER: Choose");
}

/**
 * Draw a single menu item. Called by drawMenu().
 * @param menuItem Menu item
 * @param selected If non-zero, item is selected
 * @param flags
 * @param x
 * @param y
 */
static void drawMenuItem(const Menu* menuItem, int selected, uint32_t flags, uint32_t x, uint32_t y)
{
    const char *text;
    char buf[64];
    if (!(flags & MenuFlag_ShowTID)) {
        text = menuItem->name;
    } else {
        if (menuItem->tid != 0) {
            snprintf(buf, sizeof(buf), "%s - %08lx-%08lx",
                menuItem->name, (uint32_t)(menuItem->tid >> 32), (uint32_t)(menuItem->tid & 0xFFFFFFFF));
            text = buf;
        } else {
            text = menuItem->name;
        }
    }

    gfx_draw_rect_filled(x - 1, y - 1,
        gfx_get_text_width(text) + 2, CHAR_SIZE_DRC_Y + 2,
        selected ? COLOR_PRIMARY : COLOR_BACKGROUND);

    gfx_set_font_color(selected ? COLOR_BACKGROUND : COLOR_PRIMARY);
    gfx_print(x, y, 0, text);
}

/**
 * Draw a menu and wait for the user to select an option.
 * @param title Menu title
 * @param menu Array of menu entries
 * @param count Number of menu entries
 * @param selected Initial selected item index
 * @param flags
 * @param x
 * @param y
 * @return Selected menu entry index
 */
int drawMenu(const char* title, const Menu* menu, size_t count,
        int selected, uint32_t flags, uint32_t x, uint32_t y)
{
    int redraw = 1;
    int prev_selected = -1;
    if (selected < 0 || selected >= count)
        selected = 0;

    // draw the full menu
    if (!(flags & MenuFlag_NoClearScreen)) {
        gfx_clear(COLOR_BACKGROUND);
    }
    int index = y;
    for (int i = 0; i < count; i++) {
        drawMenuItem(&menu[i], selected == i, flags, x, index);
        index += CHAR_SIZE_DRC_Y + 4;
    }

    if (flags & MenuFlag_ShowGitHubLink) {
        static const int ypos = SCREEN_HEIGHT - (CHAR_SIZE_DRC_Y * 3);
        gfx_set_font_color(COLOR_PRIMARY);
        static const char link_prefix[] = "Check out the GitHub repository at:";
        gfx_print(16, ypos, 0, link_prefix);
        static const int xpos = 16 + CHAR_SIZE_DRC_X * sizeof(link_prefix);
        gfx_set_font_color(COLOR_LINK);
        gfx_print(xpos, ypos, GfxPrintFlag_Underline, "https://github.com/GaryOderNichts/recovery_menu");
    }

    uint8_t cur_flag = 0;
    uint8_t flag = 0;
    while (1) {
        SMC_ReadSystemEventFlag(&flag);
        if (cur_flag != flag) {
            if (flag & SYSTEM_EVENT_FLAG_EJECT_BUTTON) {
                prev_selected = selected;
                selected++;
                if (selected == count)
                    selected = 0;
                redraw = 1;
            } else if (flag & SYSTEM_EVENT_FLAG_POWER_BUTTON) {
                return selected;
            }
            cur_flag = flag;
        }

        if (redraw) {
            if (prev_selected != selected) {
                // Redraw the previously selected menu item.
                if (prev_selected >= 0) {
                    index = y + ((CHAR_SIZE_DRC_Y + 4) * prev_selected);
                    drawMenuItem(&menu[prev_selected], 0, flags, x, index);
                }

                // Redraw the selected item.
                index = y + ((CHAR_SIZE_DRC_Y + 4) * selected);
                drawMenuItem(&menu[selected], 1, flags, x, index);
            }

            gfx_set_font_color(COLOR_PRIMARY);
            drawBars(title);
            redraw = 0;
        }
    }
}

/**
 * Wait for the user to press a button.
 */
void waitButtonInput(void)
{
    gfx_set_font_color(COLOR_PRIMARY);
    gfx_draw_rect_filled(8, SCREEN_HEIGHT - (16 + 8 + 2), SCREEN_WIDTH - 8 * 2, 2, COLOR_SECONDARY);

    gfx_draw_rect_filled(16 - 1, SCREEN_HEIGHT - CHAR_SIZE_DRC_Y - 4,
        SCREEN_WIDTH - 16, CHAR_SIZE_DRC_Y + 2,
        COLOR_BACKGROUND);
    gfx_print(16, SCREEN_HEIGHT - CHAR_SIZE_DRC_Y - 4, 0, "Press EJECT or POWER to proceed...");

    uint8_t cur_flag = 0;
    uint8_t flag = 0;

    while (1) {
        SMC_ReadSystemEventFlag(&flag);
        if (cur_flag != flag) {
            if ((flag & SYSTEM_EVENT_FLAG_EJECT_BUTTON) || (flag & SYSTEM_EVENT_FLAG_POWER_BUTTON)) {
                return;
            }

            cur_flag = flag;
        }
    }
}

static void option_dumpNand(void){
    gfx_clear(COLOR_BACKGROUND);
    drawTopBar("Dumping NAND...");
    dump_nand_complete(fsaHandle);
    FSA_FlushVolume(fsaHandle, "/vol/storage_recovsd");
    waitButtonInput();
}

void print_error(int index, const char *msg)
{
    gfx_set_font_color(COLOR_ERROR);
    gfx_print(16, index, GfxPrintFlag_ClearBG, msg);
    SMC_SetNotificationLED(NOTIF_LED_RED);
    waitButtonInput();
    SMC_SetNotificationLED(NOTIF_LED_PURPLE);
}

void printf_error(int index, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    char buffer[0x100];

    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    print_error(index, buffer);
}

static void option_SetColdbootTitle(void)
{
    static const Menu coldbootTitleOptions[] = {
        {"Back", {0} },
        {"Wii U Menu (JPN)", {.tid = 0x0005001010040000}},
        {"Wii U Menu (USA)", {.tid = 0x0005001010040100}},
        {"Wii U Menu (EUR)", {.tid = 0x0005001010040200}},

        // non-retail systems only
        {"System Config Tool", {.tid = 0x000500101F700500}},
        {"DEVMENU (pre-2.09)", {.tid = 0x000500101F7001FF}},
        {"Kiosk Menu        ", {.tid = 0x000500101FA81000}},
    };

    // Only print the non-retail options if the system is in debug mode.
    const int option_count = ((IOS_CheckDebugMode() == 0) ? 7 : 4);

    int rval;
    uint64_t newtid = 0;
    int selected = 0;

    gfx_clear(COLOR_BACKGROUND);
    while (1) {
        uint32_t index = 16 + 8 + 2 + 8;
        gfx_set_font_color(COLOR_PRIMARY);

        // draw current titles
        gfx_printf(16, index, GfxPrintFlag_ClearBG, "Current coldboot title:    %08lx-%08lx",
            (uint32_t)(currentColdbootTitle >> 32), (uint32_t)(currentColdbootTitle & 0xFFFFFFFFU));
        index += CHAR_SIZE_DRC_Y + 4;

        gfx_printf(16, index, GfxPrintFlag_ClearBG, "Current coldboot os:       %08lx-%08lx",
            (uint32_t)(currentColdbootOS >> 32), (uint32_t)(currentColdbootOS & 0xFFFFFFFFU));
        index += (CHAR_SIZE_DRC_Y + 4) * 2;

        selected = drawMenu("Set Coldboot Title",
            coldbootTitleOptions, option_count, selected,
            MenuFlag_ShowTID | MenuFlag_NoClearScreen, 16, index);
        index += (CHAR_SIZE_DRC_Y + 4) * option_count;

        newtid = coldbootTitleOptions[selected].tid;
        if (newtid == 0)
            return;

        // set the new default title ID
        rval = setDefaultTitleId(newtid);

        if (newtid) {
            index += (CHAR_SIZE_DRC_Y + 4) * 2;

            gfx_set_font_color(COLOR_PRIMARY);
            gfx_printf(16, index, GfxPrintFlag_ClearBG,
                "Setting coldboot title id to %08lx-%08lx, rval %d  ",
                (uint32_t)(newtid >> 32), (uint32_t)(newtid & 0xFFFFFFFFU), rval);
            index += CHAR_SIZE_DRC_Y + 4;

            if (rval < 0) {
                gfx_set_font_color(COLOR_ERROR);
                gfx_print(16, index, GfxPrintFlag_ClearBG, "Error! Make sure title is installed correctly.");
            } else {
                gfx_set_font_color(COLOR_SUCCESS);
                gfx_print(16, index, GfxPrintFlag_ClearBG, "Success!                                      ");
            }
        }
    }
}

static void option_DumpSyslogs(void)
{
    gfx_clear(COLOR_BACKGROUND);

    drawTopBar("Dumping Syslogs...");

    uint32_t index = 16 + 8 + 2 + 8;
    gfx_print(16, index, 0, "Creating 'logs' directory...");
    index += CHAR_SIZE_DRC_Y + 4;

    int res = FSA_MakeDir(fsaHandle, "/vol/storage_recovsd/logs", 0x600);
    if ((res < 0) && !(res == -0x30016)) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, index, 0, "Failed to create directory: %x", res);
        waitButtonInput();
        return;
    }

    gfx_print(16, index, 0, "Opening system 'logs' directory...");
    index += CHAR_SIZE_DRC_Y + 4;

    int dir_handle;
    res = FSA_OpenDir(fsaHandle, "/vol/system/logs", &dir_handle);
    if (res < 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, index, 0, "Failed to open system logs: %x", res);
        waitButtonInput();
        return;
    }

    char src_path[500];
    char dst_path[500];
    FSDirectoryEntry dir_entry;
    while (FSA_ReadDir(fsaHandle, dir_handle, &dir_entry) >= 0) {
        if (dir_entry.stat.flags & DIR_ENTRY_IS_DIRECTORY) {
            continue;
        }

        gfx_printf(16, index, GfxPrintFlag_ClearBG, "Copying %s...", dir_entry.name);

        snprintf(src_path, sizeof(src_path), "/vol/system/logs/" "%s", dir_entry.name);
        snprintf(dst_path, sizeof(dst_path), "/vol/storage_recovsd/logs/" "%s", dir_entry.name);

        res = copy_file(fsaHandle, src_path, dst_path);
        if (res < 0) {
            index += CHAR_SIZE_DRC_Y + 4;
            gfx_set_font_color(COLOR_ERROR);
            gfx_printf(16, index, GfxPrintFlag_ClearBG, "Failed to copy %s: %x", dir_entry.name, res);
            waitButtonInput();

            FSA_CloseDir(fsaHandle, dir_handle);
            return;
        }
    }

    index += CHAR_SIZE_DRC_Y + 4;
    gfx_set_font_color(COLOR_SUCCESS);
    gfx_print(16, index, 0, "Done!");
    waitButtonInput();

    FSA_CloseDir(fsaHandle, dir_handle);
}

static void option_DumpOtpAndSeeprom(void)
{
    gfx_clear(COLOR_BACKGROUND);
    drawTopBar("Dumping OTP + SEEPROM...");

    uint32_t index = 16 + 8 + 2 + 8;
    gfx_print(16, index, 0, "Creating otp.bin...");
    index += CHAR_SIZE_DRC_Y + 4;

    void* dataBuffer = IOS_HeapAllocAligned(CROSS_PROCESS_HEAP_ID, 0x400, 0x40);
    if (!dataBuffer) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_print(16, index, 0, "Out of memory!");
        waitButtonInput();
        return;
    }

    int otpHandle;
    int res = FSA_OpenFile(fsaHandle, "/vol/storage_recovsd/otp.bin", "w", &otpHandle);
    if (res < 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, index, 0, "Failed to create otp.bin: %x", res);
        waitButtonInput();

        IOS_HeapFree(CROSS_PROCESS_HEAP_ID, dataBuffer);
        return;
    }

    gfx_print(16, index, 0, "Reading OTP...");
    index += CHAR_SIZE_DRC_Y + 4;

    res = IOS_ReadOTP(0, dataBuffer, 0x400);
    if (res < 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, index, 0, "Failed to read OTP: %x", res);
        waitButtonInput();

        FSA_CloseFile(fsaHandle, otpHandle);
        IOS_HeapFree(CROSS_PROCESS_HEAP_ID, dataBuffer);
        return;
    }

    gfx_print(16, index, 0, "Writing otp.bin...");
    index += CHAR_SIZE_DRC_Y + 4;

    res = FSA_WriteFile(fsaHandle, dataBuffer, 1, 0x400, otpHandle, 0);
    if (res != 0x400) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, index, 0, "Failed to write otp.bin: %x", res);
        waitButtonInput();

        FSA_CloseFile(fsaHandle, otpHandle);
        IOS_HeapFree(CROSS_PROCESS_HEAP_ID, dataBuffer);
        return;
    }

    FSA_CloseFile(fsaHandle, otpHandle);

    gfx_print(16, index, 0, "Creating seeprom.bin...");
    index += CHAR_SIZE_DRC_Y + 4;

    int seepromHandle;
    res = FSA_OpenFile(fsaHandle, "/vol/storage_recovsd/seeprom.bin", "w", &seepromHandle);
    if (res < 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, index, 0, "Failed to create seeprom.bin: %x", res);
        waitButtonInput();

        IOS_HeapFree(CROSS_PROCESS_HEAP_ID, dataBuffer);
        return;
    }

    gfx_print(16, index, 0, "Reading SEEPROM...");
    index += CHAR_SIZE_DRC_Y + 4;

    res = EEPROM_Read(0, 0x100, (uint16_t*) dataBuffer);
    if (res < 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, index, 0, "Failed to read EEPROM: %x", res);
        waitButtonInput();

        FSA_CloseFile(fsaHandle, seepromHandle);
        IOS_HeapFree(CROSS_PROCESS_HEAP_ID, dataBuffer);
        return;
    }

    gfx_print(16, index, 0, "Writing seeprom.bin...");
    index += CHAR_SIZE_DRC_Y + 4;

    res = FSA_WriteFile(fsaHandle, dataBuffer, 1, 0x200, seepromHandle, 0);
    if (res != 0x200) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, index, 0, "Failed to write seeprom.bin: %x", res);
        waitButtonInput();

        FSA_CloseFile(fsaHandle, seepromHandle);
        IOS_HeapFree(CROSS_PROCESS_HEAP_ID, dataBuffer);
        return;
    }

    gfx_set_font_color(COLOR_SUCCESS);
    gfx_print(16, index, 0, "Done!");
    waitButtonInput();

    FSA_CloseFile(fsaHandle, seepromHandle);
    IOS_HeapFree(CROSS_PROCESS_HEAP_ID, dataBuffer);
}

/**
 * Initialize the network configuration.
 * @param index [in/out] Starting (and ending) Y position.
 * @return 0 on success; non-zero on error.
 */
int initNetconf(uint32_t* index)
{
    gfx_print(16, *index, 0, "Initializing netconf...");
    *index += CHAR_SIZE_DRC_Y;

    int res = netconf_init();
    if (res <= 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, *index, 0, "Failed to initialize netconf: %x", res);
        waitButtonInput();
        return res;
    }

    gfx_printf(16, *index, 0, "Waiting for network connection... %ds", 5);

    NetConfInterfaceTypeEnum interface = 0xff;
    for (int i = 0; i < 5; i++) {
        if (netconf_get_if_linkstate(NET_CFG_INTERFACE_TYPE_WIFI) == NET_CFG_LINK_STATE_UP) {
            interface = NET_CFG_INTERFACE_TYPE_WIFI;
            break;
        }

        if (netconf_get_if_linkstate(NET_CFG_INTERFACE_TYPE_ETHERNET) == NET_CFG_LINK_STATE_UP) {
            interface = NET_CFG_INTERFACE_TYPE_ETHERNET;
            break;
        }

        usleep(1000 * 1000);
        gfx_printf(16, *index, GfxPrintFlag_ClearBG, "Waiting for network connection... %ds", 4 - i);
    }

    *index += CHAR_SIZE_DRC_Y;

    if (interface == 0xff) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_print(16, *index, 0, "No network connection!");
        waitButtonInput();
        return 1;
    }

    gfx_printf(16, *index, 0, "Connected using %s", (interface == NET_CFG_INTERFACE_TYPE_WIFI) ? "WIFI" : "ETHERNET");
    *index += CHAR_SIZE_DRC_Y;

    uint8_t ip_address[4];
    res = netconf_get_assigned_address(interface, (uint32_t*) ip_address);
    if (res < 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, *index, 0, "Failed to get IP address: %x", res);
        waitButtonInput();
        return res;
    }

    gfx_printf(16, *index, 0, "IP address: %u.%u.%u.%u",
        ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
    *index += CHAR_SIZE_DRC_Y;

    res = socketInit();
    if (res <= 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, *index, 0, "Failed to initialize socketlib: %x", res);
        waitButtonInput();
        return res;
    }

    return 0;
}

static void network_parse_config_value(uint32_t* console_idx, NetConfCfg* cfg, const char* key, const char* value, uint32_t value_len)
{
    if (strncmp(key, "type", sizeof("type")) == 0) {
        gfx_printf(16, *console_idx, 0, "Type: %s", value);
        (*console_idx) += CHAR_SIZE_DRC_Y + 4;

        if (value) {
            if (strncmp(value, "wifi", sizeof("wifi")) == 0) {
                cfg->wl0.if_index = NET_CFG_INTERFACE_TYPE_WIFI;
                cfg->wl0.if_sate = 1;
                cfg->wl0.ipv4Info.mode = NET_CONFIG_IPV4_MODE_AUTO_OBTAIN_IP;
                cfg->wifi.config_method = 0;
            } else if (strncmp(value, "eth", sizeof("eth")) == 0) {
                cfg->eth0.if_index = NET_CFG_INTERFACE_TYPE_ETHERNET;
                cfg->eth0.if_sate = 1;
                cfg->eth0.ipv4Info.mode = NET_CONFIG_IPV4_MODE_AUTO_OBTAIN_IP;
                cfg->ethCfg.negotiation = NET_CFG_ETH_CFG_NEGOTIATION_AUTO;
            }
        }
    } else if (strncmp(key, "ssid", sizeof("ssid")) == 0) {
        gfx_printf(16, *console_idx, 0, "SSID: %s (%lu)", value, value_len);
        (*console_idx) += CHAR_SIZE_DRC_Y + 4;

        if (value) {
            memcpy(cfg->wifi.config.ssid, value, value_len);
            cfg->wifi.config.ssidlength = value_len;
        }
    } else if (strncmp(key, "key", sizeof("key")) == 0) {
        gfx_printf(16, *console_idx, 0, "Key: ******* (%lu)", value_len);
        (*console_idx) += CHAR_SIZE_DRC_Y + 4;

        if (value) {
            memcpy(cfg->wifi.config.privacy.aes_key, value, value_len);
            cfg->wifi.config.privacy.aes_key_len = value_len;
        }
    } else if (strncmp(key, "key_type", sizeof("key_type")) == 0) {
        gfx_printf(16, *console_idx, 0, "Key type: %s", value);
        (*console_idx) += CHAR_SIZE_DRC_Y + 4;

        if (value) {
            if (strncmp(value, "NONE", sizeof("NONE")) == 0) {
                cfg->wifi.config.privacy.mode = NET_CFG_WIFI_PRIVACY_MODE_NONE;
            } else if (strncmp(value, "WEP", sizeof("WEP")) == 0) {
                cfg->wifi.config.privacy.mode = NET_CFG_WIFI_PRIVACY_MODE_WEP;
            } else if (strncmp(value, "WPA2_PSK_TKIP", sizeof("WPA2_PSK_TKIP")) == 0) {
                cfg->wifi.config.privacy.mode = NET_CFG_WIFI_PRIVACY_MODE_WPA2_PSK_TKIP;
            } else if (strncmp(value, "WPA_PSK_TKIP", sizeof("WPA_PSK_TKIP")) == 0) {
                cfg->wifi.config.privacy.mode = NET_CFG_WIFI_PRIVACY_MODE_WPA_PSK_TKIP;
            } else if (strncmp(value, "WPA2_PSK_AES", sizeof("WPA2_PSK_AES")) == 0) {
                cfg->wifi.config.privacy.mode = NET_CFG_WIFI_PRIVACY_MODE_WPA2_PSK_AES;
            } else if (strncmp(value, "WPA_PSK_AES", sizeof("WPA_PSK_AES")) == 0) {
                cfg->wifi.config.privacy.mode = NET_CFG_WIFI_PRIVACY_MODE_WPA_PSK_AES;
            } else {
                gfx_print(16, *console_idx, 0, "Unknown key type!");
                (*console_idx) += CHAR_SIZE_DRC_Y + 4;
            }
        }
    }
}

static void option_LoadNetConf(void)
{
    gfx_clear(COLOR_BACKGROUND);
    drawTopBar("Loading network configuration...");

    uint32_t index = 16 + 8 + 2 + 8;
    gfx_print(16, index, 0, "Initializing netconf...");
    index += CHAR_SIZE_DRC_Y + 4;

    int res = netconf_init();
    if (res <= 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, index, 0, "Failed to initialize netconf: %x", res);
        waitButtonInput();
        return;
    }

    gfx_print(16, index, 0, "Reading network.cfg...");
    index += CHAR_SIZE_DRC_Y + 4;

    int cfgHandle;
    res = FSA_OpenFile(fsaHandle, "/vol/storage_recovsd/network.cfg", "r", &cfgHandle);
    if (res < 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, index, 0, "Failed to open network.cfg: %x", res);
        waitButtonInput();
        return;
    }

    FSStat stat;
    res = FSA_StatFile(fsaHandle, cfgHandle, &stat);
    if (res < 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, index, 0, "Failed to stat file: %x", res);
        waitButtonInput();

        FSA_CloseFile(fsaHandle, cfgHandle);
        return;
    }

    char* cfgBuffer = (char*) IOS_HeapAllocAligned(CROSS_PROCESS_HEAP_ID, stat.size + 1, 0x40);
    if (!cfgBuffer) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_print(16, index, 0, "Out of memory!");
        waitButtonInput();

        FSA_CloseFile(fsaHandle, cfgHandle);
        return;
    }

    cfgBuffer[stat.size] = '\0';

    res = FSA_ReadFile(fsaHandle, cfgBuffer, 1, stat.size, cfgHandle, 0);
    if (res != stat.size) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, index, 0, "Failed to read file: %x", res);
        waitButtonInput();

        IOS_HeapFree(CROSS_PROCESS_HEAP_ID, cfgBuffer);
        FSA_CloseFile(fsaHandle, cfgHandle);
        return;
    }

    NetConfCfg cfg;
    memset(&cfg, 0, sizeof(cfg));

    // parse network cfg file
    const char* keyPtr = cfgBuffer;
    const char* valuePtr = NULL;
    for (size_t i = 0; i < stat.size; i++) {
        if (cfgBuffer[i] == '=') {
            cfgBuffer[i] = '\0';

            valuePtr = cfgBuffer + i + 1;
        } else if (cfgBuffer[i] == '\n') {
            size_t end = i;
            cfgBuffer[end] = '\0';
            if (cfgBuffer[end - 1] == '\r') {
                end = i - 1;
                cfgBuffer[end] = '\0';
            }

            network_parse_config_value(&index, &cfg, keyPtr, valuePtr, (cfgBuffer + end) - valuePtr);

            keyPtr = cfgBuffer + i + 1;
            valuePtr = NULL;
        }
    }

    // if valuePtr isn't NULL there is another option without a newline at the end
    if (valuePtr) {
        network_parse_config_value(&index, &cfg, keyPtr, valuePtr, (cfgBuffer + stat.size) - valuePtr);
    }

    gfx_print(16, index, 0, "Applying configuration...");
    index += CHAR_SIZE_DRC_Y + 4;

    res = netconf_set_running(&cfg);
    if (res < 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, index, 0, "Failed to apply configuration: %x", res);
        waitButtonInput();

        IOS_HeapFree(CROSS_PROCESS_HEAP_ID, cfgBuffer);
        FSA_CloseFile(fsaHandle, cfgHandle);
        return;
    }

    gfx_set_font_color(COLOR_SUCCESS);
    gfx_print(16, index, 0, "Done!");
    index += CHAR_SIZE_DRC_Y + 4;

    waitButtonInput();

    IOS_HeapFree(CROSS_PROCESS_HEAP_ID, cfgBuffer);
    FSA_CloseFile(fsaHandle, cfgHandle);
}

static void option_EditParental(void)
{
    static const Menu parentalControlOptions[] = {
        {"Back", {0} },
        {"Disable", {0} },
    };

    int rval;
    int selected = 0;

    gfx_clear(COLOR_BACKGROUND);
    while (1) {
        uint32_t index = 16 + 8 + 2 + 8;
        gfx_set_font_color(COLOR_PRIMARY);

        // draw current parental control info
        uint8_t enabled = 0;
        int res = SCIGetParentalEnable(&enabled);
        if (res == 1) {
            gfx_set_font_color(COLOR_PRIMARY);
            gfx_printf(16, index, GfxPrintFlag_ClearBG, "Parental Controls: %s",
                enabled ? "Enabled" : "Disabled");
        } else {
            gfx_set_font_color(COLOR_ERROR);
            gfx_printf(16, index, GfxPrintFlag_ClearBG, "SCIGetParentalEnable failed: %d", res);
        }
        index += CHAR_SIZE_DRC_Y + 4;

        char pin[5] = "";
        res = SCIGetParentalPinCode(pin, sizeof(pin));
        if (res == 1) {
            gfx_set_font_color(COLOR_PRIMARY);
            gfx_printf(16, index, GfxPrintFlag_ClearBG, "Parental Pin Code: %s", pin);
        } else {
            gfx_set_font_color(COLOR_ERROR);
            gfx_printf(16, index, GfxPrintFlag_ClearBG, "SCIGetParentalPinCode failed: %d", res);
        }
        index += (CHAR_SIZE_DRC_Y + 4) * 2;

        gfx_set_font_color(COLOR_PRIMARY);

        selected = drawMenu("Edit Parental Controls",
            parentalControlOptions, ARRAY_SIZE(parentalControlOptions), selected,
            MenuFlag_NoClearScreen, 16, index);
        index += (CHAR_SIZE_DRC_Y + 4) * (ARRAY_SIZE(parentalControlOptions) + 1);

        if (selected == 0)
            return;

        // Option 1: Disable the parental controls.
        rval = SCISetParentalEnable(0);

        gfx_printf(16, index, GfxPrintFlag_ClearBG, "SCISetParentalEnable(false): %d  ", rval);
        index += CHAR_SIZE_DRC_Y + 4;

        if (rval != 1) {
            gfx_set_font_color(COLOR_ERROR);
            gfx_print(16, index, GfxPrintFlag_ClearBG, "Error!  ");
        } else {
            gfx_set_font_color(COLOR_SUCCESS);
            gfx_print(16, index, GfxPrintFlag_ClearBG, "Success!");
        }
        index += CHAR_SIZE_DRC_Y + 4;
    }
}

/**
 * Get region code information.
 * @param productArea_id Product area ID: 0-6
 * @param gameRegion Bitfield of game regions
 * @return 0 on success; negative on error.
 */
int getRegionInfo(int* productArea_id, int* gameRegion)
{
    MCPSysProdSettings sysProdSettings;

    int mcpHandle = IOS_Open("/dev/mcp", 0);
    if (mcpHandle < 0)
        return mcpHandle;

    int res = MCP_GetSysProdSettings(mcpHandle, &sysProdSettings);
    IOS_Close(mcpHandle);
    if (res < 0)
        return res;

    // productArea is a single region.
    if (sysProdSettings.product_area == 0)
        return -1;

    if (productArea_id) {
        *productArea_id = __builtin_ctz(sysProdSettings.product_area);
    }
    if (gameRegion) {
        *gameRegion = sysProdSettings.game_region;
    }
    return 0;
}

/**
 * Read OTP and SEEPROM.
 *
 * If an error occurs, a message will be displayed and the
 * user will be prompted to press a button.
 *
 * @param buf Buffer (must be 0x600 bytes)
 * @param index Row index for error messages
 * @return 0 on success; non-zero on error.
 */
int read_otp_seeprom(void *buf, int index)
{
    uint8_t* const otp = (uint8_t*)buf;
    uint16_t* const seeprom = (uint16_t*)buf + 0x200;

    int res = IOS_ReadOTP(0, otp, 0x400);
    if (res < 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, index, 0, "Failed to read OTP: %x", res);
        waitButtonInput();
        return res;
    }

    res = EEPROM_Read(0, 0x100, seeprom);
    if (res < 0) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(16, index, 0, "Failed to read EEPROM: %x", res);
        waitButtonInput();
        return res;
    }

    return 0;
}

static void option_SetInitialLaunch(void)
{
    static const Menu initialLaunchOptions[] = {
        {"Back", { .tid = 256} },
        {"2   - Default",       {.tid = 2}},
        {"0   - Initial Setup", {.tid = 0}},
        {"255 - Factory Reset", {.tid = 255}},
    };
    
    const int option_count = sizeof(initialLaunchOptions) / sizeof(Menu);

    int selected = 0;

    gfx_clear(COLOR_BACKGROUND);
    while (1) {
        uint32_t index = 16 + 8 + 2 + 8;
        gfx_set_font_color(COLOR_PRIMARY);

        uint8_t currentInitialLaunch;
        int rval = SCIGetInitialLaunch(&currentInitialLaunch);

        if(rval<0)
            gfx_printf(16, index, GfxPrintFlag_ClearBG, "Error getting current Initial Launch Value: %X" , rval);
        else
            gfx_printf(16, index, GfxPrintFlag_ClearBG, "Current initial Launch Value: %u", currentInitialLaunch);
        index += CHAR_SIZE_DRC_Y + 4;

        selected = drawMenu("Set Initial Launch Value",
            initialLaunchOptions, option_count, selected, MenuFlag_NoClearScreen, 16, index);
        index += (CHAR_SIZE_DRC_Y + 4) * option_count;

        uint64_t newil = initialLaunchOptions[selected].tid;
        if (newil == 256)
            return;


        index += (CHAR_SIZE_DRC_Y + 4) * 2;

        gfx_set_font_color(COLOR_PRIMARY);
        gfx_printf(16, index, GfxPrintFlag_ClearBG,
            "Setting Initial Launch to %u", (uint8_t)newil);
        index += CHAR_SIZE_DRC_Y + 4;

        rval = SCISetInitialLaunch((uint8_t)newil);

        if (rval < 0) {
            gfx_set_font_color(COLOR_ERROR);
            gfx_printf(16, index, GfxPrintFlag_ClearBG, "Error! %X", rval);
        } else {
            gfx_set_font_color(COLOR_SUCCESS);
            gfx_print(16, index, GfxPrintFlag_ClearBG, "Success!                                      ");
        }

    }
}




static void option_formatMlc(void){
    gfx_clear(COLOR_BACKGROUND);
    drawTopBar("Formatting MLC...");
    int res = -1;
    for(uint8_t i=0; res && (i< 10); i++){
        gfx_print(20, 30, GfxPrintFlag_ClearBG, "Waiting for System to settle...");
        usleep(5 * 1000 * 1000);
        gfx_printf(20, 30, GfxPrintFlag_ClearBG, "Unmounting MLC...");
        res = FSA_Unmount(fsaHandle, "/vol/storage_mlc01", 2);
    }
    if (res) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_printf(160, 45, 0, "Error %x", res);
        waitButtonInput();
        gfx_set_font_color(COLOR_PRIMARY);
    }
    gfx_print(20, 60, GfxPrintFlag_ClearBG, "Formatting MLC...");
    res = FSA_Format(fsaHandle, "/dev/mlc01", "wfs", 0, NULL, 0);
    gfx_printf(40, 85, 0, "Result -%08X", -res);
    gfx_print(20, 200, GfxPrintFlag_ClearBG, "Mounting MLC...");
    res = FSA_Mount(fsaHandle, "/dev/mlc01", "/vol/storage_mlc01", 2, NULL, 0);
    gfx_printf(40, 225, 0, "Result -%08X", -res);
    waitButtonInput();
    dump_nand_complete(fsaHandle);
    waitButtonInput();
}


static void cloneMlcCheckResult(void){
    gfx_clear(COLOR_BACKGROUND);
    drawTopBar("Cloning MLC...");
    int y_offset = 30;
    int res = mlc_clone(fsaHandle, y_offset+= 40);
    if(!res){
        gfx_print(20, y_offset += 20, GfxPrintFlag_ClearBG, "finished!");
        gfx_print(20, y_offset += 20, GfxPrintFlag_ClearBG, "Now remove power from the console, only turn it on again after the replacement is complete!");
        gfx_print(20, y_offset += 20, GfxPrintFlag_ClearBG, "If you turn on the console in between, you have to redo the clone again or the SLC cache will missmatch!!!");
    }
    waitButtonInput();
}

static void option_cloneMlc(void){
    gfx_clear(COLOR_BACKGROUND);
    drawTopBar("Clone MLC");
    gfx_print(20, 30, GfxPrintFlag_ClearBG, "Unmounting SDCard...");
    FSA_FlushVolume(fsaHandle, "/vol/storage_recovsd");
    FSA_Unmount(fsaHandle, "/vol/storage_recovsd", 2);
    gfx_print(20, 50, GfxPrintFlag_ClearBG, "Now Insert target SD Card. ALL DATA ON THE SD WILL BE LOST!!!");
    waitButtonInput();
    unmount_mlc(fsaHandle, 70);
    cloneMlcCheckResult();
}

static void option_dumpSlcCloneMlc(void){
    gfx_clear(COLOR_BACKGROUND);
    drawTopBar("Dumping SLC...");
    unmount_mlc(fsaHandle, 30);
    gfx_printf(20, 50, GfxPrintFlag_ClearBG, "Unmounting SLC...");
    unmount_slc(fsaHandle, 70);
    slc_dump(fsaHandle, 90, "/vol/storage_recovsd/slc.bin");
    FSA_FlushVolume(fsaHandle, "/vol/storage_recovsd");
    FSA_Unmount(fsaHandle, "/vol/storage_recovsd", 2);
    drawTopBar("Clone MLC");
    gfx_print(20, 130, GfxPrintFlag_ClearBG, "Now remove the SD Card and copy the slc.bin to the PC");
    gfx_print(20, 150, GfxPrintFlag_ClearBG, "Then insert the target SD Card for the MLC Clone");
    gfx_print(20, 170, GfxPrintFlag_ClearBG, "ALL DATA ON THE SDCARD WILL BE LOST!!!");
    waitButtonInput();
    cloneMlcCheckResult();
}

static void option_Shutdown(void)
{
    if (fsaHandle > 0) {
        // flush mlc and slc before forcing shutdown
        FSA_FlushVolume(fsaHandle, "/vol/storage_mlc01");
        FSA_FlushVolume(fsaHandle, "/vol/system");

        // unmount sd
        FSA_Unmount(fsaHandle, "/vol/storage_recovsd", 2);

        IOS_Close(fsaHandle);
    }

    IOS_Shutdown(0);   
}

static void option_checkMLC(void)
{
    gfx_clear(COLOR_BACKGROUND);
    drawTopBar("Checking MLC...");

    int fileHandle;
    int res = FSA_OpenFile(fsaHandle, "/vol/storage_recovsd/mlc_checker.txt", "w", &fileHandle);
    if (res < 0) {
        printf_error(16 + 8 + 2 + 8, "Failed to create mlc_checker.txt: %x", res);
        return;
    }

    SMC_SetNotificationLED(NOTIF_LED_ORANGE | NOTIF_LED_ORANGE_BLINKING);
    int result = checkDirRecursive(fsaHandle, "/vol/storage_mlc01", fileHandle);
    if (result<0) {
        print_error(16 + 8 + 2 + 15, "ERROR!");
        goto close;
    }

    gfx_draw_rect_filled(0, 16 + 8 + 2 + 15, 1280, 16 + 8 + 2 + 8, COLOR_BACKGROUND);
    gfx_set_font_color(COLOR_SUCCESS);
    gfx_printf(16, 16 + 8 + 2 + 15, 0, "Done! %u errors", result);

close:
    FSA_CloseFile(fsaHandle, fileHandle);
    SMC_SetNotificationLED(NOTIF_LED_PURPLE);

    waitButtonInput();
}

static void option_checkSLC(void)
{
    gfx_clear(COLOR_BACKGROUND);
    drawTopBar("Checking SLC...");

    int fileHandle;
    int res = FSA_OpenFile(fsaHandle, "/vol/storage_recovsd/slc_checker.txt", "w", &fileHandle);
    if (res < 0) {
        printf_error(16 + 8 + 2 + 8, "Failed to create slc_checker.txt: %x", res);
        return;
    }

    SMC_SetNotificationLED(NOTIF_LED_ORANGE | NOTIF_LED_ORANGE_BLINKING);
    int result = checkDirRecursive(fsaHandle, "/vol/system", fileHandle);
    if (result<0) {
        print_error(16 + 8 + 2 + 15, "ERROR!");
        goto close;
    }

    gfx_draw_rect_filled(0, 16 + 8 + 2 + 15, 1280, 16 + 8 + 2 + 8, COLOR_BACKGROUND);
    gfx_set_font_color(COLOR_SUCCESS);
    gfx_printf(16, 16 + 8 + 2 + 15, 0, "Done! %u errors", result);

close:
    FSA_CloseFile(fsaHandle, fileHandle);
    SMC_SetNotificationLED(NOTIF_LED_PURPLE);

    waitButtonInput();
}

static void option_flashBoot1(void){
    gfx_clear(COLOR_BACKGROUND);
    drawTopBar("Checking MLC...");
    size_t buff_size = 64 * 1024;

    uint32_t index = 30;
    char* buffer = (char*) IOS_HeapAllocAligned(CROSS_PROCESS_HEAP_ID, buff_size, 0x40);
    if (!buffer) {
        gfx_set_font_color(COLOR_ERROR);
        gfx_print(16, index, 0, "Out of memory!");
        waitButtonInput();
        return;
    }
    gfx_print(16, index+= CHAR_SIZE_DRC_Y + 4, 0, "Reading boot1.img...");
    int fileHandle = 0;
    int res = FSA_OpenFile(fsaHandle, "/vol/storage_recovsd/boot1.img", "rb", &fileHandle);
    if(res<0){
        gfx_printf(16, index+= CHAR_SIZE_DRC_Y + 4, 0, "Error opening boot1.img -%X", -res);
        goto end;
    }

    int read_bytes = FSA_ReadFile(fsaHandle, buffer, 1, buff_size, fileHandle, 0);
    FSA_CloseFile(fsaHandle, fileHandle);
    if(read_bytes<0){
        gfx_printf(16, index+= CHAR_SIZE_DRC_Y + 4, 0, "Error reading boot1.img -%X", -read_bytes);
        goto end;
    }
    gfx_printf(16, index+= CHAR_SIZE_DRC_Y + 4, 0, "Writing %d bytes to slccmpt! CONFIRM?", read_bytes);
    waitButtonInput();
    gfx_print(16, index+= CHAR_SIZE_DRC_Y + 4, 0, "Writing...");
    res = FSA_RawOpen(fsaHandle, "/dev/slccmpt01", &fileHandle);
    if(res<0){
        gfx_printf(16, index+= CHAR_SIZE_DRC_Y + 4, 0, "Error opening slccmpt -%X", -res);
        goto end;
    }
    res = FSA_RawWrite(fsaHandle, buffer, 2048, (read_bytes + 2047)/2048, 0, fileHandle);
    if(res<0){
        gfx_printf(16, index+= CHAR_SIZE_DRC_Y + 4, 0, "Error writing slcmpt -%X", -res);
    }
    res = FSA_RawClose(fsaHandle, fileHandle);
    if(res<0){
        gfx_printf(16, index+= CHAR_SIZE_DRC_Y + 4, 0, "Error closing slcmpt -%X", -res);
    }

end:
    IOS_HeapFree(CROSS_PROCESS_HEAP_ID, buffer);
    waitButtonInput();
}

int menuThread(void* arg)
{
    printf("menuThread running\n");

    // set LED to purple-orange blinking
    SMC_SetNotificationLED(NOTIF_LED_RED | NOTIF_LED_RED_BLINKING | NOTIF_LED_BLUE | NOTIF_LED_BLUE_BLINKING | NOTIF_LED_ORANGE);

    // stop ppcHeartbeatThread and reset PPC
    IOS_CancelThread(ppcHeartBeatThreadId, 0);
    resetPPC();

    // cut power to the disc drive to not eject a disc every eject press
    SMC_SetODDPower(0);

#ifdef DC_INIT
    // (re-)init the graphics subsystem
    GFX_SubsystemInit(0);

    /* Note: CONFIGURATION_0 is 720p instead of 480p,
       but doesn't shut down the GPU properly? The
       GamePad just stays connected after calling iOS_Shutdown.
       To be safe, let's use CONFIGURATION_1 for now */
    DISPLAY_DCInit(DC_CONFIGURATION_1);

    /* Note about the display configuration struct:
       The returned framebuffer address seems to be AV out only?
       Writing to the hardcoded addresses in gfx.c works for HDMI though */
    DC_Config dc_config;
    DISPLAY_ReadDCConfig(&dc_config);
#endif

    // initialize the font
    if (gfx_init_font() != 0) {
        // failed to initialize font
        // can't do anything without a font, so sleep for 5 secs and shut down
        usleep(1000 * 1000 * 5);
        IOS_Shutdown(0);
    }

    // open fsa and mount sdcard
    fsaHandle = IOS_Open("/dev/fsa", 0);
    if (fsaHandle > 0) {
        int res = FSA_Mount(fsaHandle, "/dev/sdcard01", "/vol/storage_recovsd", 2, NULL, 0);
        if (res < 0) {
            printf("Failed to mount SD: %x\n", res);
        }
    } else {
        printf("Failed to open FSA: %x\n", fsaHandle);
    }

    // set LED to purple
    SMC_SetNotificationLED(NOTIF_LED_RED | NOTIF_LED_BLUE);

    int selected = 0;
    while (1) {
        selected = drawMenu("Wii U Recovery Menu v" VERSION_STRING " by GaryOderNichts",
            mainMenuOptions, ARRAY_SIZE(mainMenuOptions), selected,
            MenuFlag_ShowGitHubLink, 16, 16+8+2+8);
        if (selected >= 0 && selected < ARRAY_SIZE(mainMenuOptions)) {
            mainMenuOptions[selected].callback();
        }
    }

    return 0;
}
