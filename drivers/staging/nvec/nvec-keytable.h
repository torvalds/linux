/*
 * drivers/input/keyboard/tegra-nvec.c
 *
 * Keyboard class input driver for keyboards connected to an NvEc compliant
 * embedded controller
 *
 * Copyright (c) 2009, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

static unsigned short code_tab_102us[] = {
	KEY_GRAVE,	// 0x00
	KEY_ESC,
	KEY_1,
	KEY_2,
	KEY_3,
	KEY_4,
	KEY_5,
	KEY_6,
	KEY_7,
	KEY_8,
	KEY_9,
	KEY_0,
	KEY_MINUS,
	KEY_EQUAL,
	KEY_BACKSPACE,
	KEY_TAB,
	KEY_Q,		// 0x10
	KEY_W,
	KEY_E,
	KEY_R,
	KEY_T,
	KEY_Y,
	KEY_U,
	KEY_I,
	KEY_O,
	KEY_P,
	KEY_LEFTBRACE,
	KEY_RIGHTBRACE,
	KEY_ENTER,
	KEY_LEFTCTRL,
	KEY_A,
	KEY_S,
	KEY_D,		// 0x20
	KEY_F,
	KEY_G,
	KEY_H,
	KEY_J,
	KEY_K,
	KEY_L,
	KEY_SEMICOLON,
	KEY_APOSTROPHE,
	KEY_GRAVE,
	KEY_LEFTSHIFT,
	KEY_BACKSLASH,
	KEY_Z,
	KEY_X,
	KEY_C,
	KEY_V,
	KEY_B,		// 0x30
	KEY_N,
	KEY_M,
	KEY_COMMA,
	KEY_DOT,
	KEY_SLASH,
	KEY_RIGHTSHIFT,
	KEY_KPASTERISK,
	KEY_LEFTALT,
	KEY_SPACE,
	KEY_CAPSLOCK,
	KEY_F1,
	KEY_F2,
	KEY_F3,
	KEY_F4,
	KEY_F5,
	KEY_F6,		// 0x40
	KEY_F7,
	KEY_F8,
	KEY_F9,
	KEY_F10,
	KEY_FN,
	0,		//VK_SCROLL
	KEY_KP7,
	KEY_KP8,
	KEY_KP9,
	KEY_KPMINUS,
	KEY_KP4,
	KEY_KP5,
	KEY_KP6,
	KEY_KPPLUS,
	KEY_KP1,
	KEY_KP2,	// 0x50
	KEY_KP3,
	KEY_KP0,
	KEY_KPDOT,
	KEY_MENU,		//VK_SNAPSHOT
	KEY_POWER,
	KEY_102ND,		//VK_OEM_102   henry+ 0x2B (43) BACKSLASH have been used,change to use 0X56 (86)
	KEY_F11,		//VK_F11
	KEY_F12,		//VK_F12
	0, 
	0, 
	0, 
	0, 
	0, 
	0, 
	0, 
	0, // 60 
	0,
	0,
	KEY_SEARCH, // add search key map 
	0,		
	0,
	0,
	0,	
	0,		
	0, 
	0, 
	0, 
	0, 
	0, 
	0, 
	0, 
	0, // 70 
	0,
	0,
	KEY_KP5,  //73 for JP keyboard '\' key, report 0x4c
	0,		
	0,
	0,
	0,	
	0,		
	0, 
	0, 
    0, 
	0, 
	KEY_KP9, //7d  for JP keyboard '|' key, report 0x49
};

static unsigned short extcode_tab_us102[] = {
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,		// 0xE0 0x10
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,		//VK_MEDIA_NEXT_TRACK,
	0,
	0,
	0,		//VK_RETURN,
	KEY_RIGHTCTRL,		//VK_RCONTROL,
	0,
	0,
	KEY_MUTE,	// 0xE0 0x20
	0,		//VK_LAUNCH_APP1
	0,		//VK_MEDIA_PLAY_PAUSE
	0,
	0,		//VK_MEDIA_STOP
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	KEY_VOLUMEUP,	// 0xE0 0x30
	0,
	0,		//VK_BROWSER_HOME
	0,
	0,
	KEY_KPSLASH,	//VK_DIVIDE
	0,
	KEY_SYSRQ,		//VK_SNAPSHOT
	KEY_RIGHTALT,		//VK_RMENU
	0,		//VK_OEM_NV_BACKLIGHT_UP
	0,		//VK_OEM_NV_BACKLIGHT_DN
	0,		//VK_OEM_NV_BACKLIGHT_AUTOTOGGLE
	0,		//VK_OEM_NV_POWER_INFO
	0,		//VK_OEM_NV_WIFI_TOGGLE
	0,		//VK_OEM_NV_DISPLAY_SELECT
	0,		//VK_OEM_NV_AIRPLANE_TOGGLE
	0,		//0xE0 0x40
	KEY_LEFT,		//VK_OEM_NV_RESERVED    henry+ for JP keyboard
	0,		//VK_OEM_NV_RESERVED
	0,		//VK_OEM_NV_RESERVED
	0,		//VK_OEM_NV_RESERVED
	0,		//VK_OEM_NV_RESERVED
	KEY_CANCEL,
	KEY_HOME,
	KEY_UP,
	KEY_PAGEUP,		//VK_PRIOR
	0,
	KEY_LEFT,
	0,
	KEY_RIGHT,
	0,
	KEY_END,
	KEY_DOWN,	// 0xE0 0x50
	KEY_PAGEDOWN,		//VK_NEXT
	KEY_INSERT,
	KEY_DELETE,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	KEY_LEFTMETA,	//VK_LWIN
	0,		//VK_RWIN
	KEY_ESC,	//VK_APPS
	KEY_KPMINUS, //for power button workaround
	0, 
	0,
	0,
	0,
	0,
	0,
	0,		//VK_BROWSER_SEARCH
	0,		//VK_BROWSER_FAVORITES
	0,		//VK_BROWSER_REFRESH
	0,		//VK_BROWSER_STOP
	0,		//VK_BROWSER_FORWARD
	0,		//VK_BROWSER_BACK
	0,		//VK_LAUNCH_APP2
	0,		//VK_LAUNCH_MAIL
	0,		//VK_LAUNCH_MEDIA_SELECT
};

static unsigned short* code_tabs[] = {code_tab_102us, extcode_tab_us102 };
