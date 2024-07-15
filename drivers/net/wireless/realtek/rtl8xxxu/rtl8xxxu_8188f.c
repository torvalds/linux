// SPDX-License-Identifier: GPL-2.0-only
/*
 * RTL8XXXU mac80211 USB driver - 8188f specific subdriver
 *
 * Copyright (c) 2022 Bitterblue Smith <rtl8821cerfe2@gmail.com>
 *
 * Portions copied from existing rtl8xxxu code:
 * Copyright (c) 2014 - 2017 Jes Sorensen <Jes.Sorensen@gmail.com>
 *
 * Portions, notably calibration code:
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/usb.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/wireless.h>
#include <linux/firmware.h>
#include <linux/moduleparam.h>
#include <net/mac80211.h>
#include "rtl8xxxu.h"
#include "rtl8xxxu_regs.h"

static const struct rtl8xxxu_reg8val rtl8188f_mac_init_table[] = {
	{0x024, 0xDF}, {0x025, 0x07}, {0x02B, 0x1C}, {0x283, 0x20},
	{0x421, 0x0F}, {0x428, 0x0A}, {0x429, 0x10}, {0x430, 0x00},
	{0x431, 0x00}, {0x432, 0x00}, {0x433, 0x01}, {0x434, 0x04},
	{0x435, 0x05}, {0x436, 0x07}, {0x437, 0x08}, {0x43C, 0x04},
	{0x43D, 0x05}, {0x43E, 0x07}, {0x43F, 0x08}, {0x440, 0x5D},
	{0x441, 0x01}, {0x442, 0x00}, {0x444, 0x10}, {0x445, 0x00},
	{0x446, 0x00}, {0x447, 0x00}, {0x448, 0x00}, {0x449, 0xF0},
	{0x44A, 0x0F}, {0x44B, 0x3E}, {0x44C, 0x10}, {0x44D, 0x00},
	{0x44E, 0x00}, {0x44F, 0x00}, {0x450, 0x00}, {0x451, 0xF0},
	{0x452, 0x0F}, {0x453, 0x00}, {0x456, 0x5E}, {0x460, 0x44},
	{0x461, 0x44}, {0x4BC, 0xC0}, {0x4C8, 0xFF}, {0x4C9, 0x08},
	{0x4CC, 0xFF}, {0x4CD, 0xFF}, {0x4CE, 0x01}, {0x500, 0x26},
	{0x501, 0xA2}, {0x502, 0x2F}, {0x503, 0x00}, {0x504, 0x28},
	{0x505, 0xA3}, {0x506, 0x5E}, {0x507, 0x00}, {0x508, 0x2B},
	{0x509, 0xA4}, {0x50A, 0x5E}, {0x50B, 0x00}, {0x50C, 0x4F},
	{0x50D, 0xA4}, {0x50E, 0x00}, {0x50F, 0x00}, {0x512, 0x1C},
	{0x514, 0x0A}, {0x516, 0x0A}, {0x525, 0x4F}, {0x550, 0x10},
	{0x551, 0x10}, {0x559, 0x02}, {0x55C, 0x28}, {0x55D, 0xFF},
	{0x605, 0x30}, {0x608, 0x0E}, {0x609, 0x2A}, {0x620, 0xFF},
	{0x621, 0xFF}, {0x622, 0xFF}, {0x623, 0xFF}, {0x624, 0xFF},
	{0x625, 0xFF}, {0x626, 0xFF}, {0x627, 0xFF}, {0x638, 0x28},
	{0x63C, 0x0A}, {0x63D, 0x0A}, {0x63E, 0x0E}, {0x63F, 0x0E},
	{0x640, 0x40}, {0x642, 0x40}, {0x643, 0x00}, {0x652, 0xC8},
	{0x66E, 0x05}, {0x700, 0x21}, {0x701, 0x43}, {0x702, 0x65},
	{0x703, 0x87}, {0x708, 0x21}, {0x709, 0x43}, {0x70A, 0x65},
	{0x70B, 0x87},
	{0xffff, 0xff},
};

static const struct rtl8xxxu_reg32val rtl8188fu_phy_init_table[] = {
	{0x800, 0x80045700}, {0x804, 0x00000001},
	{0x808, 0x0000FC00}, {0x80C, 0x0000000A},
	{0x810, 0x10001331}, {0x814, 0x020C3D10},
	{0x818, 0x00200385}, {0x81C, 0x00000000},
	{0x820, 0x01000100}, {0x824, 0x00390204},
	{0x828, 0x00000000}, {0x82C, 0x00000000},
	{0x830, 0x00000000}, {0x834, 0x00000000},
	{0x838, 0x00000000}, {0x83C, 0x00000000},
	{0x840, 0x00010000}, {0x844, 0x00000000},
	{0x848, 0x00000000}, {0x84C, 0x00000000},
	{0x850, 0x00030000}, {0x854, 0x00000000},
	{0x858, 0x569A569A}, {0x85C, 0x569A569A},
	{0x860, 0x00000130}, {0x864, 0x00000000},
	{0x868, 0x00000000}, {0x86C, 0x27272700},
	{0x870, 0x00000000}, {0x874, 0x25004000},
	{0x878, 0x00000808}, {0x87C, 0x004F0201},
	{0x880, 0xB0000B1E}, {0x884, 0x00000007},
	{0x888, 0x00000000}, {0x88C, 0xCCC000C0},
	{0x890, 0x00000800}, {0x894, 0xFFFFFFFE},
	{0x898, 0x40302010}, {0x89C, 0x00706050},
	{0x900, 0x00000000}, {0x904, 0x00000023},
	{0x908, 0x00000000}, {0x90C, 0x81121111},
	{0x910, 0x00000002}, {0x914, 0x00000201},
	{0x948, 0x99000000}, {0x94C, 0x00000010},
	{0x950, 0x20003000}, {0x954, 0x4A880000},
	{0x958, 0x4BC5D87A}, {0x95C, 0x04EB9B79},
	{0x96C, 0x00000003}, {0xA00, 0x00D047C8},
	{0xA04, 0x80FF800C}, {0xA08, 0x8C898300},
	{0xA0C, 0x2E7F120F}, {0xA10, 0x9500BB78},
	{0xA14, 0x1114D028}, {0xA18, 0x00881117},
	{0xA1C, 0x89140F00}, {0xA20, 0xD1D80000},
	{0xA24, 0x5A7DA0BD}, {0xA28, 0x0000223B},
	{0xA2C, 0x00D30000}, {0xA70, 0x101FBF00},
	{0xA74, 0x00000007}, {0xA78, 0x00000900},
	{0xA7C, 0x225B0606}, {0xA80, 0x218075B1},
	{0xA84, 0x00120000}, {0xA88, 0x040C0000},
	{0xA8C, 0x12345678}, {0xA90, 0xABCDEF00},
	{0xA94, 0x001B1B89}, {0xA98, 0x05100000},
	{0xA9C, 0x3F000000}, {0xAA0, 0x00000000},
	{0xB2C, 0x00000000}, {0xC00, 0x48071D40},
	{0xC04, 0x03A05611}, {0xC08, 0x000000E4},
	{0xC0C, 0x6C6C6C6C}, {0xC10, 0x18800000},
	{0xC14, 0x40000100}, {0xC18, 0x08800000},
	{0xC1C, 0x40000100}, {0xC20, 0x00000000},
	{0xC24, 0x00000000}, {0xC28, 0x00000000},
	{0xC2C, 0x00000000}, {0xC30, 0x69E9CC4A},
	{0xC34, 0x31000040}, {0xC38, 0x21688080},
	{0xC3C, 0x00001714}, {0xC40, 0x1F78403F},
	{0xC44, 0x00010036}, {0xC48, 0xEC020107},
	{0xC4C, 0x007F037F}, {0xC50, 0x69553420},
	{0xC54, 0x43BC0094}, {0xC58, 0x00013169},
	{0xC5C, 0x00250492}, {0xC60, 0x00000000},
	{0xC64, 0x7112848B}, {0xC68, 0x47C07BFF},
	{0xC6C, 0x00000036}, {0xC70, 0x2C7F000D},
	{0xC74, 0x020600DB}, {0xC78, 0x0000001F},
	{0xC7C, 0x00B91612}, {0xC80, 0x390000E4},
	{0xC84, 0x11F60000},
	{0xC88, 0x40000100}, {0xC8C, 0x20200000},
	{0xC90, 0x00091521}, {0xC94, 0x00000000},
	{0xC98, 0x00121820}, {0xC9C, 0x00007F7F},
	{0xCA0, 0x00000000}, {0xCA4, 0x000300A0},
	{0xCA8, 0x00000000}, {0xCAC, 0x00000000},
	{0xCB0, 0x00000000}, {0xCB4, 0x00000000},
	{0xCB8, 0x00000000}, {0xCBC, 0x28000000},
	{0xCC0, 0x00000000}, {0xCC4, 0x00000000},
	{0xCC8, 0x00000000}, {0xCCC, 0x00000000},
	{0xCD0, 0x00000000}, {0xCD4, 0x00000000},
	{0xCD8, 0x64B22427}, {0xCDC, 0x00766932},
	{0xCE0, 0x00222222}, {0xCE4, 0x10000000},
	{0xCE8, 0x37644302}, {0xCEC, 0x2F97D40C},
	{0xD00, 0x04030740}, {0xD04, 0x40020401},
	{0xD08, 0x0000907F}, {0xD0C, 0x20010201},
	{0xD10, 0xA0633333}, {0xD14, 0x3333BC53},
	{0xD18, 0x7A8F5B6F}, {0xD2C, 0xCB979975},
	{0xD30, 0x00000000}, {0xD34, 0x80608000},
	{0xD38, 0x98000000}, {0xD3C, 0x40127353},
	{0xD40, 0x00000000}, {0xD44, 0x00000000},
	{0xD48, 0x00000000}, {0xD4C, 0x00000000},
	{0xD50, 0x6437140A}, {0xD54, 0x00000000},
	{0xD58, 0x00000282}, {0xD5C, 0x30032064},
	{0xD60, 0x4653DE68}, {0xD64, 0x04518A3C},
	{0xD68, 0x00002101}, {0xD6C, 0x2A201C16},
	{0xD70, 0x1812362E}, {0xD74, 0x322C2220},
	{0xD78, 0x000E3C24}, {0xE00, 0x2D2D2D2D},
	{0xE04, 0x2D2D2D2D}, {0xE08, 0x0390272D},
	{0xE10, 0x2D2D2D2D}, {0xE14, 0x2D2D2D2D},
	{0xE18, 0x2D2D2D2D}, {0xE1C, 0x2D2D2D2D},
	{0xE28, 0x00000000}, {0xE30, 0x1000DC1F},
	{0xE34, 0x10008C1F}, {0xE38, 0x02140102},
	{0xE3C, 0x681604C2}, {0xE40, 0x01007C00},
	{0xE44, 0x01004800}, {0xE48, 0xFB000000},
	{0xE4C, 0x000028D1}, {0xE50, 0x1000DC1F},
	{0xE54, 0x10008C1F}, {0xE58, 0x02140102},
	{0xE5C, 0x28160D05}, {0xE60, 0x00000008},
	{0xE60, 0x021400A0}, {0xE64, 0x281600A0},
	{0xE6C, 0x01C00010}, {0xE70, 0x01C00010},
	{0xE74, 0x02000010}, {0xE78, 0x02000010},
	{0xE7C, 0x02000010}, {0xE80, 0x02000010},
	{0xE84, 0x01C00010}, {0xE88, 0x02000010},
	{0xE8C, 0x01C00010}, {0xED0, 0x01C00010},
	{0xED4, 0x01C00010}, {0xED8, 0x01C00010},
	{0xEDC, 0x00000010}, {0xEE0, 0x00000010},
	{0xEEC, 0x03C00010}, {0xF14, 0x00000003},
	{0xF4C, 0x00000000}, {0xF00, 0x00000300},
	{0xffff, 0xffffffff},
};

static const struct rtl8xxxu_reg32val rtl8188f_agc_table[] = {
	{0xC78, 0xFC000001}, {0xC78, 0xFB010001},
	{0xC78, 0xFA020001}, {0xC78, 0xF9030001},
	{0xC78, 0xF8040001}, {0xC78, 0xF7050001},
	{0xC78, 0xF6060001}, {0xC78, 0xF5070001},
	{0xC78, 0xF4080001}, {0xC78, 0xF3090001},
	{0xC78, 0xF20A0001}, {0xC78, 0xF10B0001},
	{0xC78, 0xF00C0001}, {0xC78, 0xEF0D0001},
	{0xC78, 0xEE0E0001}, {0xC78, 0xED0F0001},
	{0xC78, 0xEC100001}, {0xC78, 0xEB110001},
	{0xC78, 0xEA120001}, {0xC78, 0xE9130001},
	{0xC78, 0xE8140001}, {0xC78, 0xE7150001},
	{0xC78, 0xE6160001}, {0xC78, 0xE5170001},
	{0xC78, 0xE4180001}, {0xC78, 0xE3190001},
	{0xC78, 0xE21A0001}, {0xC78, 0xE11B0001},
	{0xC78, 0xE01C0001}, {0xC78, 0xC21D0001},
	{0xC78, 0xC11E0001}, {0xC78, 0xC01F0001},
	{0xC78, 0xA5200001}, {0xC78, 0xA4210001},
	{0xC78, 0xA3220001}, {0xC78, 0xA2230001},
	{0xC78, 0xA1240001}, {0xC78, 0xA0250001},
	{0xC78, 0x65260001}, {0xC78, 0x64270001},
	{0xC78, 0x63280001}, {0xC78, 0x62290001},
	{0xC78, 0x612A0001}, {0xC78, 0x442B0001},
	{0xC78, 0x432C0001}, {0xC78, 0x422D0001},
	{0xC78, 0x412E0001}, {0xC78, 0x402F0001},
	{0xC78, 0x21300001}, {0xC78, 0x20310001},
	{0xC78, 0x05320001}, {0xC78, 0x04330001},
	{0xC78, 0x03340001}, {0xC78, 0x02350001},
	{0xC78, 0x01360001}, {0xC78, 0x00370001},
	{0xC78, 0x00380001}, {0xC78, 0x00390001},
	{0xC78, 0x003A0001}, {0xC78, 0x003B0001},
	{0xC78, 0x003C0001}, {0xC78, 0x003D0001},
	{0xC78, 0x003E0001}, {0xC78, 0x003F0001},
	{0xC50, 0x69553422}, {0xC50, 0x69553420},
	{0xffff, 0xffffffff}
};

static const struct rtl8xxxu_rfregval rtl8188fu_radioa_init_table[] = {
	{0x00, 0x00030000}, {0x08, 0x00008400},
	{0x18, 0x00000407}, {0x19, 0x00000012},
	{0x1B, 0x00001C6C},
	{0x1E, 0x00080009}, {0x1F, 0x00000880},
	{0x2F, 0x0001A060}, {0x3F, 0x00028000},
	{0x42, 0x000060C0}, {0x57, 0x000D0000},
	{0x58, 0x000C0160}, {0x67, 0x00001552},
	{0x83, 0x00000000}, {0xB0, 0x000FF9F0},
	{0xB1, 0x00022218}, {0xB2, 0x00034C00},
	{0xB4, 0x0004484B}, {0xB5, 0x0000112A},
	{0xB6, 0x0000053E}, {0xB7, 0x00010408},
	{0xB8, 0x00010200}, {0xB9, 0x00080001},
	{0xBA, 0x00040001}, {0xBB, 0x00000400},
	{0xBF, 0x000C0000}, {0xC2, 0x00002400},
	{0xC3, 0x00000009}, {0xC4, 0x00040C91},
	{0xC5, 0x00099999}, {0xC6, 0x000000A3},
	{0xC7, 0x0008F820}, {0xC8, 0x00076C06},
	{0xC9, 0x00000000}, {0xCA, 0x00080000},
	{0xDF, 0x00000180}, {0xEF, 0x000001A0},
	{0x51, 0x000E8333}, {0x52, 0x000FAC2C},
	{0x53, 0x00000103}, {0x56, 0x000517F0},
	{0x35, 0x00000099}, {0x35, 0x00000199},
	{0x35, 0x00000299}, {0x36, 0x00000064},
	{0x36, 0x00008064}, {0x36, 0x00010064},
	{0x36, 0x00018064}, {0x18, 0x00000C07},
	{0x5A, 0x00048000}, {0x19, 0x000739D0},
	{0x34, 0x0000ADD6}, {0x34, 0x00009DD3},
	{0x34, 0x00008CF4}, {0x34, 0x00007CF1},
	{0x34, 0x00006CEE}, {0x34, 0x00005CEB},
	{0x34, 0x00004CCE}, {0x34, 0x00003CCB},
	{0x34, 0x00002CC8}, {0x34, 0x00001C4B},
	{0x34, 0x00000C48},
	{0x00, 0x00030159}, {0x84, 0x00048000},
	{0x86, 0x0000002A}, {0x87, 0x00000025},
	{0x8E, 0x00065540}, {0x8F, 0x00088000},
	{0xEF, 0x000020A0}, {0x3B, 0x000F0F00},
	{0x3B, 0x000E0B00}, {0x3B, 0x000D0900},
	{0x3B, 0x000C0700}, {0x3B, 0x000B0600},
	{0x3B, 0x000A0400}, {0x3B, 0x00090200},
	{0x3B, 0x00080000}, {0x3B, 0x0007BF00},
	{0x3B, 0x00060B00}, {0x3B, 0x0005C900},
	{0x3B, 0x00040700}, {0x3B, 0x00030600},
	{0x3B, 0x0002D500}, {0x3B, 0x00010200},
	{0x3B, 0x0000E000}, {0xEF, 0x000000A0},
	{0xEF, 0x00000010}, {0x3B, 0x0000C0A8},
	{0x3B, 0x00010400}, {0xEF, 0x00000000},
	{0xEF, 0x00080000}, {0x30, 0x00010000},
	{0x31, 0x0000000F}, {0x32, 0x00007EFE},
	{0xEF, 0x00000000}, {0x00, 0x00010159},
	{0x18, 0x0000FC07}, {0xFE, 0x00000000},
	{0xFE, 0x00000000}, {0x1F, 0x00080003},
	{0xFE, 0x00000000}, {0xFE, 0x00000000},
	{0x1E, 0x00000001}, {0x1F, 0x00080000},
	{0x00, 0x00033D95},
	{0xff, 0xffffffff}
};

static const struct rtl8xxxu_rfregval rtl8188fu_cut_b_radioa_init_table[] = {
	{0x00, 0x00030000}, {0x08, 0x00008400},
	{0x18, 0x00000407}, {0x19, 0x00000012},
	{0x1B, 0x00001C6C},
	{0x1E, 0x00080009}, {0x1F, 0x00000880},
	{0x2F, 0x0001A060}, {0x3F, 0x00028000},
	{0x42, 0x000060C0}, {0x57, 0x000D0000},
	{0x58, 0x000C0160}, {0x67, 0x00001552},
	{0x83, 0x00000000}, {0xB0, 0x000FF9F0},
	{0xB1, 0x00022218}, {0xB2, 0x00034C00},
	{0xB4, 0x0004484B}, {0xB5, 0x0000112A},
	{0xB6, 0x0000053E}, {0xB7, 0x00010408},
	{0xB8, 0x00010200}, {0xB9, 0x00080001},
	{0xBA, 0x00040001}, {0xBB, 0x00000400},
	{0xBF, 0x000C0000}, {0xC2, 0x00002400},
	{0xC3, 0x00000009}, {0xC4, 0x00040C91},
	{0xC5, 0x00099999}, {0xC6, 0x000000A3},
	{0xC7, 0x0008F820}, {0xC8, 0x00076C06},
	{0xC9, 0x00000000}, {0xCA, 0x00080000},
	{0xDF, 0x00000180}, {0xEF, 0x000001A0},
	{0x51, 0x000E8231}, {0x52, 0x000FAC2C},
	{0x53, 0x00000141}, {0x56, 0x000517F0},
	{0x35, 0x00000090}, {0x35, 0x00000190},
	{0x35, 0x00000290}, {0x36, 0x00001064},
	{0x36, 0x00009064}, {0x36, 0x00011064},
	{0x36, 0x00019064}, {0x18, 0x00000C07},
	{0x5A, 0x00048000}, {0x19, 0x000739D0},
	{0x34, 0x0000ADD2}, {0x34, 0x00009DD0},
	{0x34, 0x00008CF3}, {0x34, 0x00007CF0},
	{0x34, 0x00006CED}, {0x34, 0x00005CD2},
	{0x34, 0x00004CCF}, {0x34, 0x00003CCC},
	{0x34, 0x00002CC9}, {0x34, 0x00001C4C},
	{0x34, 0x00000C49},
	{0x00, 0x00030159}, {0x84, 0x00048000},
	{0x86, 0x0000002A}, {0x87, 0x00000025},
	{0x8E, 0x00065540}, {0x8F, 0x00088000},
	{0xEF, 0x000020A0}, {0x3B, 0x000F0F00},
	{0x3B, 0x000E0B00}, {0x3B, 0x000D0900},
	{0x3B, 0x000C0700}, {0x3B, 0x000B0600},
	{0x3B, 0x000A0400}, {0x3B, 0x00090200},
	{0x3B, 0x00080000}, {0x3B, 0x0007BF00},
	{0x3B, 0x00060B00}, {0x3B, 0x0005C900},
	{0x3B, 0x00040700}, {0x3B, 0x00030600},
	{0x3B, 0x0002D500}, {0x3B, 0x00010200},
	{0x3B, 0x0000E000}, {0xEF, 0x000000A0},
	{0xEF, 0x00000010}, {0x3B, 0x0000C0A8},
	{0x3B, 0x00010400}, {0xEF, 0x00000000},
	{0xEF, 0x00080000}, {0x30, 0x00010000},
	{0x31, 0x0000000F}, {0x32, 0x00007EFE},
	{0xEF, 0x00000000}, {0x00, 0x00010159},
	{0x18, 0x0000FC07}, {0xFE, 0x00000000},
	{0xFE, 0x00000000}, {0x1F, 0x00080003},
	{0xFE, 0x00000000}, {0xFE, 0x00000000},
	{0x1E, 0x00000001}, {0x1F, 0x00080000},
	{0x00, 0x00033D95},
	{0xff, 0xffffffff}
};

static int rtl8188fu_identify_chip(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	u32 sys_cfg, vendor;
	int ret = 0;

	strscpy(priv->chip_name, "8188FU", sizeof(priv->chip_name));
	priv->rtl_chip = RTL8188F;
	priv->rf_paths = 1;
	priv->rx_paths = 1;
	priv->tx_paths = 1;
	priv->has_wifi = 1;

	sys_cfg = rtl8xxxu_read32(priv, REG_SYS_CFG);
	priv->chip_cut = u32_get_bits(sys_cfg, SYS_CFG_CHIP_VERSION_MASK);
	if (sys_cfg & SYS_CFG_TRP_VAUX_EN) {
		dev_info(dev, "Unsupported test chip\n");
		ret = -ENOTSUPP;
		goto out;
	}

	vendor = sys_cfg & SYS_CFG_VENDOR_EXT_MASK;
	rtl8xxxu_identify_vendor_2bits(priv, vendor);

	ret = rtl8xxxu_config_endpoints_no_sie(priv);

out:
	return ret;
}

void rtl8188f_channel_to_group(int channel, int *group, int *cck_group)
{
	if (channel < 3)
		*group = 0;
	else if (channel < 6)
		*group = 1;
	else if (channel < 9)
		*group = 2;
	else if (channel < 12)
		*group = 3;
	else
		*group = 4;

	if (channel == 14)
		*cck_group = 5;
	else
		*cck_group = *group;
}

void
rtl8188f_set_tx_power(struct rtl8xxxu_priv *priv, int channel, bool ht40)
{
	u32 val32, ofdm, mcs;
	u8 cck, ofdmbase, mcsbase;
	int group, cck_group;

	rtl8188f_channel_to_group(channel, &group, &cck_group);

	cck = priv->cck_tx_power_index_A[cck_group];

	val32 = rtl8xxxu_read32(priv, REG_TX_AGC_A_CCK1_MCS32);
	val32 &= 0xffff00ff;
	val32 |= (cck << 8);
	rtl8xxxu_write32(priv, REG_TX_AGC_A_CCK1_MCS32, val32);

	val32 = rtl8xxxu_read32(priv, REG_TX_AGC_B_CCK11_A_CCK2_11);
	val32 &= 0xff;
	val32 |= ((cck << 8) | (cck << 16) | (cck << 24));
	rtl8xxxu_write32(priv, REG_TX_AGC_B_CCK11_A_CCK2_11, val32);

	ofdmbase = priv->ht40_1s_tx_power_index_A[group];
	ofdmbase += priv->ofdm_tx_power_diff[0].a;
	ofdm = ofdmbase | ofdmbase << 8 | ofdmbase << 16 | ofdmbase << 24;

	rtl8xxxu_write32(priv, REG_TX_AGC_A_RATE18_06, ofdm);
	rtl8xxxu_write32(priv, REG_TX_AGC_A_RATE54_24, ofdm);

	mcsbase = priv->ht40_1s_tx_power_index_A[group];
	if (ht40)
		/* This diff is always 0 - not used in 8188FU. */
		mcsbase += priv->ht40_tx_power_diff[0].a;
	else
		mcsbase += priv->ht20_tx_power_diff[0].a;
	mcs = mcsbase | mcsbase << 8 | mcsbase << 16 | mcsbase << 24;

	rtl8xxxu_write32(priv, REG_TX_AGC_A_MCS03_MCS00, mcs);
	rtl8xxxu_write32(priv, REG_TX_AGC_A_MCS07_MCS04, mcs);
	rtl8xxxu_write32(priv, REG_TX_AGC_A_MCS11_MCS08, mcs);
	rtl8xxxu_write32(priv, REG_TX_AGC_A_MCS15_MCS12, mcs);
}

/* A workaround to eliminate the 2400MHz, 2440MHz, 2480MHz spur of 8188F. */
static void rtl8188f_spur_calibration(struct rtl8xxxu_priv *priv, u8 channel)
{
	static const u32 frequencies[14 + 1] = {
		[5] = 0xFCCD,
		[6] = 0xFC4D,
		[7] = 0xFFCD,
		[8] = 0xFF4D,
		[11] = 0xFDCD,
		[13] = 0xFCCD,
		[14] = 0xFF9A
	};

	static const u32 reg_d40[14 + 1] = {
		[5] = 0x06000000,
		[6] = 0x00000600,
		[13] = 0x06000000
	};

	static const u32 reg_d44[14 + 1] = {
		[11] = 0x04000000
	};

	static const u32 reg_d4c[14 + 1] = {
		[7] = 0x06000000,
		[8] = 0x00000380,
		[14] = 0x00180000
	};

	const u8 threshold = 0x16;
	bool do_notch, hw_ctrl, sw_ctrl, hw_ctrl_s1 = 0, sw_ctrl_s1 = 0;
	u32 val32, initial_gain, reg948;

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_RX_D_SYNC_PATH);
	val32 |= GENMASK(28, 24);
	rtl8xxxu_write32(priv, REG_OFDM0_RX_D_SYNC_PATH, val32);

	/* enable notch filter */
	val32 = rtl8xxxu_read32(priv, REG_OFDM0_RX_D_SYNC_PATH);
	val32 |= BIT(9);
	rtl8xxxu_write32(priv, REG_OFDM0_RX_D_SYNC_PATH, val32);

	if (channel <= 14 && frequencies[channel] > 0) {
		reg948 = rtl8xxxu_read32(priv, REG_S0S1_PATH_SWITCH);
		hw_ctrl = reg948 & BIT(6);
		sw_ctrl = !hw_ctrl;

		if (hw_ctrl) {
			val32 = rtl8xxxu_read32(priv, REG_FPGA0_XB_RF_INT_OE);
			val32 &= GENMASK(5, 3);
			hw_ctrl_s1 = val32 == BIT(3);
		} else if (sw_ctrl) {
			sw_ctrl_s1 = !(reg948 & BIT(9));
		}

		if (hw_ctrl_s1 || sw_ctrl_s1) {
			initial_gain = rtl8xxxu_read32(priv, REG_OFDM0_XA_AGC_CORE1);

			/* Disable CCK block */
			val32 = rtl8xxxu_read32(priv, REG_FPGA0_RF_MODE);
			val32 &= ~FPGA_RF_MODE_CCK;
			rtl8xxxu_write32(priv, REG_FPGA0_RF_MODE, val32);

			val32 = initial_gain & ~OFDM0_X_AGC_CORE1_IGI_MASK;
			val32 |= 0x30;
			rtl8xxxu_write32(priv, REG_OFDM0_XA_AGC_CORE1, val32);

			/* disable 3-wire */
			rtl8xxxu_write32(priv, REG_FPGA0_ANALOG4, 0xccf000c0);

			/* Setup PSD */
			rtl8xxxu_write32(priv, REG_FPGA0_PSD_FUNC, frequencies[channel]);

			/* Start PSD */
			rtl8xxxu_write32(priv, REG_FPGA0_PSD_FUNC, 0x400000 | frequencies[channel]);

			msleep(30);

			do_notch = rtl8xxxu_read32(priv, REG_FPGA0_PSD_REPORT) >= threshold;

			/* turn off PSD */
			rtl8xxxu_write32(priv, REG_FPGA0_PSD_FUNC, frequencies[channel]);

			/* enable 3-wire */
			rtl8xxxu_write32(priv, REG_FPGA0_ANALOG4, 0xccc000c0);

			/* Enable CCK block */
			val32 = rtl8xxxu_read32(priv, REG_FPGA0_RF_MODE);
			val32 |= FPGA_RF_MODE_CCK;
			rtl8xxxu_write32(priv, REG_FPGA0_RF_MODE, val32);

			rtl8xxxu_write32(priv, REG_OFDM0_XA_AGC_CORE1, initial_gain);

			if (do_notch) {
				rtl8xxxu_write32(priv, REG_OFDM1_CSI_FIX_MASK1, reg_d40[channel]);
				rtl8xxxu_write32(priv, REG_OFDM1_CSI_FIX_MASK2, reg_d44[channel]);
				rtl8xxxu_write32(priv, 0xd48, 0x0);
				rtl8xxxu_write32(priv, 0xd4c, reg_d4c[channel]);

				/* enable CSI mask */
				val32 = rtl8xxxu_read32(priv, REG_OFDM1_CFO_TRACKING);
				val32 |= BIT(28);
				rtl8xxxu_write32(priv, REG_OFDM1_CFO_TRACKING, val32);

				return;
			}
		}
	}

	/* disable CSI mask function */
	val32 = rtl8xxxu_read32(priv, REG_OFDM1_CFO_TRACKING);
	val32 &= ~BIT(28);
	rtl8xxxu_write32(priv, REG_OFDM1_CFO_TRACKING, val32);
}

static void rtl8188fu_config_channel(struct ieee80211_hw *hw)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	u32 val32;
	u8 channel, subchannel;
	bool sec_ch_above;

	channel = (u8)hw->conf.chandef.chan->hw_value;

	/* Set channel */
	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_MODE_AG);
	val32 &= ~MODE_AG_CHANNEL_MASK;
	val32 |= channel;
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_MODE_AG, val32);

	/* Spur calibration */
	rtl8188f_spur_calibration(priv, channel);

	/* Set bandwidth mode */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_RF_MODE);
	val32 &= ~FPGA_RF_MODE;
	val32 |= hw->conf.chandef.width == NL80211_CHAN_WIDTH_40;
	rtl8xxxu_write32(priv, REG_FPGA0_RF_MODE, val32);

	val32 = rtl8xxxu_read32(priv, REG_FPGA1_RF_MODE);
	val32 &= ~FPGA_RF_MODE;
	val32 |= hw->conf.chandef.width == NL80211_CHAN_WIDTH_40;
	rtl8xxxu_write32(priv, REG_FPGA1_RF_MODE, val32);

	/* RXADC CLK */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_RF_MODE);
	val32 |= GENMASK(10, 8);
	rtl8xxxu_write32(priv, REG_FPGA0_RF_MODE, val32);

	/* TXDAC CLK */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_RF_MODE);
	val32 |= BIT(14) | BIT(12);
	val32 &= ~BIT(13);
	rtl8xxxu_write32(priv, REG_FPGA0_RF_MODE, val32);

	/* small BW */
	val32 = rtl8xxxu_read32(priv, REG_OFDM0_TX_PSDO_NOISE_WEIGHT);
	val32 &= ~GENMASK(31, 30);
	rtl8xxxu_write32(priv, REG_OFDM0_TX_PSDO_NOISE_WEIGHT, val32);

	/* adc buffer clk */
	val32 = rtl8xxxu_read32(priv, REG_OFDM0_TX_PSDO_NOISE_WEIGHT);
	val32 &= ~BIT(29);
	val32 |= BIT(28);
	rtl8xxxu_write32(priv, REG_OFDM0_TX_PSDO_NOISE_WEIGHT, val32);

	/* adc buffer clk */
	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XA_RX_AFE);
	val32 &= ~BIT(29);
	val32 |= BIT(28);
	rtl8xxxu_write32(priv, REG_OFDM0_XA_RX_AFE, val32);

	val32 = rtl8xxxu_read32(priv, REG_OFDM_RX_DFIR);
	val32 &= ~BIT(19);
	rtl8xxxu_write32(priv, REG_OFDM_RX_DFIR, val32);

	val32 = rtl8xxxu_read32(priv, REG_OFDM_RX_DFIR);
	val32 &= ~GENMASK(23, 20);
	val32 |= BIT(21);
	if (hw->conf.chandef.width == NL80211_CHAN_WIDTH_20 ||
	    hw->conf.chandef.width == NL80211_CHAN_WIDTH_20_NOHT)
		val32 |= BIT(20);
	else if (hw->conf.chandef.width == NL80211_CHAN_WIDTH_40)
		val32 |= BIT(22);
	rtl8xxxu_write32(priv, REG_OFDM_RX_DFIR, val32);

	if (hw->conf.chandef.width == NL80211_CHAN_WIDTH_40) {
		if (hw->conf.chandef.center_freq1 >
		    hw->conf.chandef.chan->center_freq) {
			sec_ch_above = 1;
			channel += 2;
		} else {
			sec_ch_above = 0;
			channel -= 2;
		}

		/* Set Control channel to upper or lower. */
		val32 = rtl8xxxu_read32(priv, REG_CCK0_SYSTEM);
		val32 &= ~CCK0_SIDEBAND;
		if (!sec_ch_above)
			val32 |= CCK0_SIDEBAND;
		rtl8xxxu_write32(priv, REG_CCK0_SYSTEM, val32);

		val32 = rtl8xxxu_read32(priv, REG_DATA_SUBCHANNEL);
		val32 &= ~GENMASK(3, 0);
		if (sec_ch_above)
			subchannel = 2;
		else
			subchannel = 1;
		val32 |= subchannel;
		rtl8xxxu_write32(priv, REG_DATA_SUBCHANNEL, val32);

		val32 = rtl8xxxu_read32(priv, REG_RESPONSE_RATE_SET);
		val32 &= ~RSR_RSC_BANDWIDTH_40M;
		rtl8xxxu_write32(priv, REG_RESPONSE_RATE_SET, val32);
	}

	/* RF TRX_BW */
	val32 = channel;
	if (hw->conf.chandef.width == NL80211_CHAN_WIDTH_20 ||
	    hw->conf.chandef.width == NL80211_CHAN_WIDTH_20_NOHT)
		val32 |= MODE_AG_BW_20MHZ_8723B;
	else if (hw->conf.chandef.width == NL80211_CHAN_WIDTH_40)
		val32 |= MODE_AG_BW_40MHZ_8723B;
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_MODE_AG, val32);

	/* FILTER BW&RC Corner (ACPR) */
	if (hw->conf.chandef.width == NL80211_CHAN_WIDTH_20 ||
	    hw->conf.chandef.width == NL80211_CHAN_WIDTH_20_NOHT)
		val32 = 0x00065;
	else if (hw->conf.chandef.width == NL80211_CHAN_WIDTH_40)
		val32 = 0x00025;
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_RXG_MIX_SWBW, val32);

	if (hw->conf.chandef.width == NL80211_CHAN_WIDTH_20 ||
	    hw->conf.chandef.width == NL80211_CHAN_WIDTH_20_NOHT)
		val32 = 0x0;
	else if (hw->conf.chandef.width == NL80211_CHAN_WIDTH_40)
		val32 = 0x01000;
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_RX_BB2, val32);

	/* RC Corner */
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA, 0x00140);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_RX_G2, 0x01c6c);
}

static void rtl8188fu_init_aggregation(struct rtl8xxxu_priv *priv)
{
	u8 agg_ctrl, rxdma_mode, usb_tx_agg_desc_num = 6;
	u32 agg_rx, val32;

	/* TX aggregation */
	val32 = rtl8xxxu_read32(priv, REG_DWBCN0_CTRL_8188F);
	val32 &= ~(0xf << 4);
	val32 |= usb_tx_agg_desc_num << 4;
	rtl8xxxu_write32(priv, REG_DWBCN0_CTRL_8188F, val32);
	rtl8xxxu_write8(priv, REG_DWBCN1_CTRL_8723B, usb_tx_agg_desc_num << 1);

	/* RX aggregation */
	agg_ctrl = rtl8xxxu_read8(priv, REG_TRXDMA_CTRL);
	agg_ctrl &= ~TRXDMA_CTRL_RXDMA_AGG_EN;

	agg_rx = rtl8xxxu_read32(priv, REG_RXDMA_AGG_PG_TH);
	agg_rx &= ~RXDMA_USB_AGG_ENABLE;
	agg_rx &= ~0xFF0F; /* reset agg size and timeout */

	rxdma_mode = rtl8xxxu_read8(priv, REG_RXDMA_PRO_8723B);
	rxdma_mode &= ~BIT(1);

	rtl8xxxu_write8(priv, REG_TRXDMA_CTRL, agg_ctrl);
	rtl8xxxu_write32(priv, REG_RXDMA_AGG_PG_TH, agg_rx);
	rtl8xxxu_write8(priv, REG_RXDMA_PRO_8723B, rxdma_mode);
}

static void rtl8188fu_init_statistics(struct rtl8xxxu_priv *priv)
{
	u32 val32;

	/* Time duration for NHM unit: 4us, 0xc350=200ms */
	rtl8xxxu_write16(priv, REG_NHM_TIMER_8723B + 2, 0xc350);
	rtl8xxxu_write16(priv, REG_NHM_TH9_TH10_8723B + 2, 0xffff);
	rtl8xxxu_write32(priv, REG_NHM_TH3_TO_TH0_8723B, 0xffffff50);
	rtl8xxxu_write32(priv, REG_NHM_TH7_TO_TH4_8723B, 0xffffffff);

	/* TH8 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 |= 0xff;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/* Enable CCK */
	val32 = rtl8xxxu_read32(priv, REG_NHM_TH9_TH10_8723B);
	val32 &= ~(BIT(8) | BIT(9) | BIT(10));
	val32 |= BIT(8);
	rtl8xxxu_write32(priv, REG_NHM_TH9_TH10_8723B, val32);

	/* Max power amongst all RX antennas */
	val32 = rtl8xxxu_read32(priv, REG_OFDM0_FA_RSTC);
	val32 |= BIT(7);
	rtl8xxxu_write32(priv, REG_OFDM0_FA_RSTC, val32);
}

static int rtl8188fu_parse_efuse(struct rtl8xxxu_priv *priv)
{
	struct rtl8188fu_efuse *efuse = &priv->efuse_wifi.efuse8188fu;

	if (efuse->rtl_id != cpu_to_le16(0x8129))
		return -EINVAL;

	ether_addr_copy(priv->mac_addr, efuse->mac_addr);

	memcpy(priv->cck_tx_power_index_A, efuse->tx_power_index_A.cck_base,
	       sizeof(efuse->tx_power_index_A.cck_base));

	memcpy(priv->ht40_1s_tx_power_index_A,
	       efuse->tx_power_index_A.ht40_base,
	       sizeof(efuse->tx_power_index_A.ht40_base));

	priv->ofdm_tx_power_diff[0].a = efuse->tx_power_index_A.ht20_ofdm_1s_diff.a;
	priv->ht20_tx_power_diff[0].a = efuse->tx_power_index_A.ht20_ofdm_1s_diff.b;

	priv->default_crystal_cap = efuse->xtal_k & 0x3f;

	return 0;
}

static int rtl8188fu_load_firmware(struct rtl8xxxu_priv *priv)
{
	const char *fw_name;
	int ret;

	fw_name = "rtlwifi/rtl8188fufw.bin";

	ret = rtl8xxxu_load_firmware(priv, fw_name);

	return ret;
}

static void rtl8188fu_init_phy_bb(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u16 val16;

	/* Enable BB and RF */
	val16 = rtl8xxxu_read16(priv, REG_SYS_FUNC);
	val16 |= SYS_FUNC_BB_GLB_RSTN | SYS_FUNC_BBRSTB | SYS_FUNC_DIO_RF;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, val16);

	/*
	 * Per vendor driver, run power sequence before init of RF
	 */
	val8 = RF_ENABLE | RF_RSTB | RF_SDMRSTB;
	rtl8xxxu_write8(priv, REG_RF_CTRL, val8);

	usleep_range(10, 20);

	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_IQADJ_G1, 0x780);

	val8 = SYS_FUNC_BB_GLB_RSTN | SYS_FUNC_BBRSTB | SYS_FUNC_USBA | SYS_FUNC_USBD;
	rtl8xxxu_write8(priv, REG_SYS_FUNC, val8);

	rtl8xxxu_init_phy_regs(priv, rtl8188fu_phy_init_table);
	rtl8xxxu_init_phy_regs(priv, rtl8188f_agc_table);
}

static int rtl8188fu_init_phy_rf(struct rtl8xxxu_priv *priv)
{
	int ret;

	if (priv->chip_cut == 1)
		ret = rtl8xxxu_init_phy_rf(priv, rtl8188fu_cut_b_radioa_init_table, RF_A);
	else
		ret = rtl8xxxu_init_phy_rf(priv, rtl8188fu_radioa_init_table, RF_A);

	return ret;
}

void rtl8188f_phy_lc_calibrate(struct rtl8xxxu_priv *priv)
{
	u32 val32;
	u32 rf_amode, lstf;
	int i;

	/* Check continuous TX and Packet TX */
	lstf = rtl8xxxu_read32(priv, REG_OFDM1_LSTF);

	if (lstf & OFDM_LSTF_MASK) {
		/* Disable all continuous TX */
		val32 = lstf & ~OFDM_LSTF_MASK;
		rtl8xxxu_write32(priv, REG_OFDM1_LSTF, val32);
	} else {
		/* Deal with Packet TX case */
		/* block all queues */
		rtl8xxxu_write8(priv, REG_TXPAUSE, 0xff);
	}

	/* Read original RF mode Path A */
	rf_amode = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_MODE_AG);

	/* Start LC calibration */
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_MODE_AG, rf_amode | 0x08000);

	for (i = 0; i < 100; i++) {
		if ((rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_MODE_AG) & 0x08000) == 0)
			break;
		msleep(10);
	}

	if (i == 100)
		dev_warn(&priv->udev->dev, "LC calibration timed out.\n");

	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_MODE_AG, rf_amode);

	/* Restore original parameters */
	if (lstf & OFDM_LSTF_MASK)
		rtl8xxxu_write32(priv, REG_OFDM1_LSTF, lstf);
	else /*  Deal with Packet TX case */
		rtl8xxxu_write8(priv, REG_TXPAUSE, 0x00);
}

static int rtl8188fu_iqk_path_a(struct rtl8xxxu_priv *priv, u32 *lok_result)
{
	u32 reg_eac, reg_e94, reg_e9c, val32;
	int result = 0;

	/*
	 * Leave IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/*
	 * Enable path A PA in TX IQK mode
	 */
	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_WE_LUT);
	val32 |= 0x80000;
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_WE_LUT, val32);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_RCK_OS, 0x20000);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G1, 0x0000f);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G2, 0x07ff7);

	/* PA,PAD gain adjust */
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA, 0x980);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_PAD_TXG, 0x5102a);

	/* enter IQK mode */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	val32 |= 0x80800000;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/* path-A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x18008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x38008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_A, 0x821403ff);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, 0x28160000);

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x00462911);

	/* One shot, path A LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf9000000);
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf8000000);

	mdelay(25);

	/*
	 * Leave IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA, 0x180);

	/* save LOK result */
	*lok_result = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_TXM_IDAC);

	/* Check failed */
	reg_eac = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_A_2);
	reg_e94 = rtl8xxxu_read32(priv, REG_TX_POWER_BEFORE_IQK_A);
	reg_e9c = rtl8xxxu_read32(priv, REG_TX_POWER_AFTER_IQK_A);

	if (!(reg_eac & BIT(28)) &&
	    ((reg_e94 & 0x03ff0000) != 0x01420000) &&
	    ((reg_e9c & 0x03ff0000) != 0x00420000))
		result |= 0x01;

	return result;
}

static int rtl8188fu_rx_iqk_path_a(struct rtl8xxxu_priv *priv, u32 lok_result)
{
	u32 reg_ea4, reg_eac, reg_e94, reg_e9c, val32;
	int result = 0;

	/*
	 * Leave IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/*
	 * Enable path A PA in TX IQK mode
	 */
	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_WE_LUT);
	val32 |= 0x80000;
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_WE_LUT, val32);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_RCK_OS, 0x30000);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G1, 0x0000f);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G2, 0xf1173);

	/* PA,PAD gain adjust */
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA, 0x980);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_PAD_TXG, 0x5102a);

	/*
	 * Enter IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	val32 |= 0x80800000;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/*
	 * Tx IQK setting
	 */
	rtl8xxxu_write32(priv, REG_TX_IQK, 0x01007c00);
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

	/* path-A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x10008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x30008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_A, 0x82160fff);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, 0x28160000);

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x00462911);

	/* One shot, path A LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf9000000);
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf8000000);

	mdelay(25);

	/*
	 * Leave IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA, 0x180);

	/* Check failed */
	reg_eac = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_A_2);
	reg_e94 = rtl8xxxu_read32(priv, REG_TX_POWER_BEFORE_IQK_A);
	reg_e9c = rtl8xxxu_read32(priv, REG_TX_POWER_AFTER_IQK_A);

	if (!(reg_eac & BIT(28)) &&
	    ((reg_e94 & 0x03ff0000) != 0x01420000) &&
	    ((reg_e9c & 0x03ff0000) != 0x00420000))
		result |= 0x01;
	else /* If TX not OK, ignore RX */
		goto out;

	val32 = 0x80007c00 | (reg_e94 & 0x3ff0000) |
		((reg_e9c & 0x3ff0000) >> 16);
	rtl8xxxu_write32(priv, REG_TX_IQK, val32);

	/*
	 * Modify RX IQK mode table
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_WE_LUT);
	val32 |= 0x80000;
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_WE_LUT, val32);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_RCK_OS, 0x30000);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G1, 0x0000f);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G2, 0xf7ff2);

	/*
	 * PA, PAD setting
	 */
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA, 0x980);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_PAD_TXG, 0x51000);

	/*
	 * Enter IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	val32 |= 0x80800000;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/*
	 * RX IQK setting
	 */
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

	/* path-A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x30008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x10008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_A, 0x82160000);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, 0x281613ff);

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x0046a911);

	/* One shot, path A LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf9000000);
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf8000000);

	mdelay(25);

	/*
	 * Leave IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA, 0x180);

	/* reload LOK value */
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXM_IDAC, lok_result);

	/* Check failed */
	reg_eac = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_A_2);
	reg_ea4 = rtl8xxxu_read32(priv, REG_RX_POWER_BEFORE_IQK_A_2);

	if (!(reg_eac & BIT(27)) &&
	    ((reg_ea4 & 0x03ff0000) != 0x01320000) &&
	    ((reg_eac & 0x03ff0000) != 0x00360000))
		result |= 0x02;

out:
	return result;
}

static void rtl8188fu_phy_iqcalibrate(struct rtl8xxxu_priv *priv,
				      int result[][8], int t)
{
	struct device *dev = &priv->udev->dev;
	u32 i, val32, rx_initial_gain, lok_result;
	u32 path_sel_bb, path_sel_rf;
	int path_a_ok;
	int retry = 2;
	static const u32 adda_regs[RTL8XXXU_ADDA_REGS] = {
		REG_FPGA0_XCD_SWITCH_CTRL, REG_BLUETOOTH,
		REG_RX_WAIT_CCA, REG_TX_CCK_RFON,
		REG_TX_CCK_BBON, REG_TX_OFDM_RFON,
		REG_TX_OFDM_BBON, REG_TX_TO_RX,
		REG_TX_TO_TX, REG_RX_CCK,
		REG_RX_OFDM, REG_RX_WAIT_RIFS,
		REG_RX_TO_RX, REG_STANDBY,
		REG_SLEEP, REG_PMPD_ANAEN
	};
	static const u32 iqk_mac_regs[RTL8XXXU_MAC_REGS] = {
		REG_TXPAUSE, REG_BEACON_CTRL,
		REG_BEACON_CTRL_1, REG_GPIO_MUXCFG
	};
	static const u32 iqk_bb_regs[RTL8XXXU_BB_REGS] = {
		REG_OFDM0_TRX_PATH_ENABLE, REG_OFDM0_TR_MUX_PAR,
		REG_FPGA0_XCD_RF_SW_CTRL, REG_CONFIG_ANT_A, REG_CONFIG_ANT_B,
		REG_FPGA0_XAB_RF_SW_CTRL, REG_FPGA0_XA_RF_INT_OE,
		REG_FPGA0_XB_RF_INT_OE, REG_FPGA0_RF_MODE
	};

	/*
	 * Note: IQ calibration must be performed after loading
	 *       PHY_REG.txt , and radio_a, radio_b.txt
	 */

	rx_initial_gain = rtl8xxxu_read32(priv, REG_OFDM0_XA_AGC_CORE1);

	if (t == 0) {
		/* Save ADDA parameters, turn Path A ADDA on */
		rtl8xxxu_save_regs(priv, adda_regs, priv->adda_backup,
				   RTL8XXXU_ADDA_REGS);
		rtl8xxxu_save_mac_regs(priv, iqk_mac_regs, priv->mac_backup);
		rtl8xxxu_save_regs(priv, iqk_bb_regs,
				   priv->bb_backup, RTL8XXXU_BB_REGS);
	}

	rtl8xxxu_path_adda_on(priv, adda_regs, true);

	if (t == 0) {
		val32 = rtl8xxxu_read32(priv, REG_FPGA0_XA_HSSI_PARM1);
		priv->pi_enabled = u32_get_bits(val32, FPGA0_HSSI_PARM1_PI);
	}

	/* save RF path */
	path_sel_bb = rtl8xxxu_read32(priv, REG_S0S1_PATH_SWITCH);
	path_sel_rf = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_S0S1);

	/* BB setting */
	rtl8xxxu_write32(priv, REG_OFDM0_TRX_PATH_ENABLE, 0x03a05600);
	rtl8xxxu_write32(priv, REG_OFDM0_TR_MUX_PAR, 0x000800e4);
	rtl8xxxu_write32(priv, REG_FPGA0_XCD_RF_SW_CTRL, 0x25204000);

	/* MAC settings */
	val32 = rtl8xxxu_read32(priv, REG_TX_PTCL_CTRL);
	val32 |= 0x00ff0000;
	rtl8xxxu_write32(priv, REG_TX_PTCL_CTRL, val32);

	/* IQ calibration setting */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0xff;
	val32 |= 0x80800000;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);
	rtl8xxxu_write32(priv, REG_TX_IQK, 0x01007c00);
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

	for (i = 0; i < retry; i++) {
		path_a_ok = rtl8188fu_iqk_path_a(priv, &lok_result);
		if (path_a_ok == 0x01) {
			val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
			val32 &= 0xff;
			rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

			val32 = rtl8xxxu_read32(priv,
						REG_TX_POWER_BEFORE_IQK_A);
			result[t][0] = (val32 >> 16) & 0x3ff;

			val32 = rtl8xxxu_read32(priv,
						REG_TX_POWER_AFTER_IQK_A);
			result[t][1] = (val32 >> 16) & 0x3ff;
			break;
		}
	}

	for (i = 0; i < retry; i++) {
		path_a_ok = rtl8188fu_rx_iqk_path_a(priv, lok_result);
		if (path_a_ok == 0x03) {
			val32 = rtl8xxxu_read32(priv,
						REG_RX_POWER_BEFORE_IQK_A_2);
			result[t][2] = (val32 >> 16) & 0x3ff;

			val32 = rtl8xxxu_read32(priv,
						REG_RX_POWER_AFTER_IQK_A_2);
			result[t][3] = (val32 >> 16) & 0x3ff;
			break;
		}
	}

	if (!path_a_ok)
		dev_dbg(dev, "%s: Path A IQK failed!\n", __func__);

	/* Back to BB mode, load original value */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0xff;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	if (t == 0)
		return;

	if (!priv->pi_enabled) {
		/*
		 * Switch back BB to SI mode after finishing
		 * IQ Calibration
		 */
		val32 = 0x01000000;
		rtl8xxxu_write32(priv, REG_FPGA0_XA_HSSI_PARM1, val32);
		rtl8xxxu_write32(priv, REG_FPGA0_XB_HSSI_PARM1, val32);
	}

	/* Reload ADDA power saving parameters */
	rtl8xxxu_restore_regs(priv, adda_regs, priv->adda_backup,
			      RTL8XXXU_ADDA_REGS);

	/* Reload MAC parameters */
	rtl8xxxu_restore_mac_regs(priv, iqk_mac_regs, priv->mac_backup);

	/* Reload BB parameters */
	rtl8xxxu_restore_regs(priv, iqk_bb_regs,
			      priv->bb_backup, RTL8XXXU_BB_REGS);

	/* Reload RF path */
	rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, path_sel_bb);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_S0S1, path_sel_rf);

	/* Restore RX initial gain */
	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XA_AGC_CORE1);
	val32 &= 0xffffff00;
	val32 |= 0x50;
	rtl8xxxu_write32(priv, REG_OFDM0_XA_AGC_CORE1, val32);
	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XA_AGC_CORE1);
	val32 &= 0xffffff00;
	val32 |= rx_initial_gain & 0xff;
	rtl8xxxu_write32(priv, REG_OFDM0_XA_AGC_CORE1, val32);

	/* Load 0xe30 IQC default value */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x01008c00);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x01008c00);
}

static void rtl8188fu_phy_iq_calibrate(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	int result[4][8]; /* last is final result */
	int i, candidate;
	bool path_a_ok;
	u32 reg_e94, reg_e9c, reg_ea4, reg_eac;
	u32 reg_eb4, reg_ebc, reg_ec4, reg_ecc;
	s32 reg_tmp = 0;
	bool simu;
	u32 path_sel_bb, path_sel_rf;

	/* Save RF path */
	path_sel_bb = rtl8xxxu_read32(priv, REG_S0S1_PATH_SWITCH);
	path_sel_rf = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_S0S1);

	memset(result, 0, sizeof(result));
	candidate = -1;

	path_a_ok = false;

	for (i = 0; i < 3; i++) {
		rtl8188fu_phy_iqcalibrate(priv, result, i);

		if (i == 1) {
			simu = rtl8xxxu_gen2_simularity_compare(priv, result, 0, 1);
			if (simu) {
				candidate = 0;
				break;
			}
		}

		if (i == 2) {
			simu = rtl8xxxu_gen2_simularity_compare(priv, result, 0, 2);
			if (simu) {
				candidate = 0;
				break;
			}

			simu = rtl8xxxu_gen2_simularity_compare(priv, result, 1, 2);
			if (simu) {
				candidate = 1;
			} else {
				for (i = 0; i < 8; i++)
					reg_tmp += result[3][i];

				if (reg_tmp)
					candidate = 3;
				else
					candidate = -1;
			}
		}
	}

	for (i = 0; i < 4; i++) {
		reg_e94 = result[i][0];
		reg_e9c = result[i][1];
		reg_ea4 = result[i][2];
		reg_eac = result[i][3];
		reg_eb4 = result[i][4];
		reg_ebc = result[i][5];
		reg_ec4 = result[i][6];
		reg_ecc = result[i][7];
	}

	if (candidate >= 0) {
		reg_e94 = result[candidate][0];
		priv->rege94 =  reg_e94;
		reg_e9c = result[candidate][1];
		priv->rege9c = reg_e9c;
		reg_ea4 = result[candidate][2];
		reg_eac = result[candidate][3];
		reg_eb4 = result[candidate][4];
		priv->regeb4 = reg_eb4;
		reg_ebc = result[candidate][5];
		priv->regebc = reg_ebc;
		reg_ec4 = result[candidate][6];
		reg_ecc = result[candidate][7];
		dev_dbg(dev, "%s: candidate is %x\n", __func__, candidate);
		dev_dbg(dev,
			"%s: e94 =%x e9c=%x ea4=%x eac=%x eb4=%x ebc=%x ec4=%x ecc=%x\n",
			__func__, reg_e94, reg_e9c,
			reg_ea4, reg_eac, reg_eb4, reg_ebc, reg_ec4, reg_ecc);
		path_a_ok = true;
	} else {
		reg_e94 = reg_eb4 = priv->rege94 = priv->regeb4 = 0x100;
		reg_e9c = reg_ebc = priv->rege9c = priv->regebc = 0x0;
	}

	if (reg_e94 && candidate >= 0)
		rtl8xxxu_fill_iqk_matrix_a(priv, path_a_ok, result,
					   candidate, (reg_ea4 == 0));

	rtl8xxxu_save_regs(priv, rtl8xxxu_iqk_phy_iq_bb_reg,
			   priv->bb_recovery_backup, RTL8XXXU_BB_REGS);

	rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, path_sel_bb);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_S0S1, path_sel_rf);
}

static void rtl8188f_disabled_to_emu(struct rtl8xxxu_priv *priv)
{
	u16 val8;

	/* 0x04[12:11] = 2b'01enable WL suspend */
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 &= ~((APS_FSMCO_PCIE | APS_FSMCO_HW_SUSPEND) >> 8);
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	/* 0xC4[4] <= 1, turn off USB APHY LDO under suspend mode */
	val8 = rtl8xxxu_read8(priv, 0xc4);
	val8 &= ~BIT(4);
	rtl8xxxu_write8(priv, 0xc4, val8);
}

static int rtl8188f_emu_to_active(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u32 val32;
	int count, ret = 0;

	/* Disable SW LPS */
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 &= ~(APS_FSMCO_SW_LPS >> 8);
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	/* wait till 0x04[17] = 1 power ready */
	for (count = RTL8XXXU_MAX_REG_POLL; count; count--) {
		val32 = rtl8xxxu_read32(priv, REG_APS_FSMCO);
		if (val32 & BIT(17))
			break;

		udelay(10);
	}

	if (!count) {
		ret = -EBUSY;
		goto exit;
	}

	/* Disable HWPDN */
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 &= ~(APS_FSMCO_HW_POWERDOWN >> 8);
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	/* Disable WL suspend */
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 &= ~(APS_FSMCO_HW_SUSPEND >> 8);
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	/* set, then poll until 0 */
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 |= APS_FSMCO_MAC_ENABLE >> 8;
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	for (count = RTL8XXXU_MAX_REG_POLL; count; count--) {
		val32 = rtl8xxxu_read32(priv, REG_APS_FSMCO);
		if ((val32 & APS_FSMCO_MAC_ENABLE) == 0) {
			ret = 0;
			break;
		}
		udelay(10);
	}

	if (!count) {
		ret = -EBUSY;
		goto exit;
	}

	/* 0x27<=35 to reduce RF noise */
	val8 = rtl8xxxu_write8(priv, 0x27, 0x35);
exit:
	return ret;
}

static int rtl8188fu_active_to_emu(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u32 val32;
	int count, ret = 0;

	/* Turn off RF */
	rtl8xxxu_write8(priv, REG_RF_CTRL, 0);

	/* 0x4C[23] = 0x4E[7] = 0, switch DPDT_SEL_P output from register 0x65[2] */
	val8 = rtl8xxxu_read8(priv, 0x4e);
	val8 &= ~BIT(7);
	rtl8xxxu_write8(priv, 0x4e, val8);

	/* 0x27 <= 34, xtal_qsel = 0 to xtal bring up */
	rtl8xxxu_write8(priv, 0x27, 0x34);

	/* 0x04[9] = 1 turn off MAC by HW state machine */
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 |= APS_FSMCO_MAC_OFF >> 8;
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	for (count = RTL8XXXU_MAX_REG_POLL; count; count--) {
		val32 = rtl8xxxu_read32(priv, REG_APS_FSMCO);
		if ((val32 & APS_FSMCO_MAC_OFF) == 0) {
			ret = 0;
			break;
		}
		udelay(10);
	}

	if (!count) {
		ret = -EBUSY;
		goto exit;
	}

exit:
	return ret;
}

static int rtl8188fu_emu_to_disabled(struct rtl8xxxu_priv *priv)
{
	u8 val8;

	/* 0x04[12:11] = 2b'01 enable WL suspend */
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 &= ~((APS_FSMCO_PCIE | APS_FSMCO_HW_SUSPEND) >> 8);
	val8 |= APS_FSMCO_HW_SUSPEND >> 8;
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	/* 0xC4[4] <= 1, turn off USB APHY LDO under suspend mode */
	val8 = rtl8xxxu_read8(priv, 0xc4);
	val8 |= BIT(4);
	rtl8xxxu_write8(priv, 0xc4, val8);

	return 0;
}

static int rtl8188fu_active_to_lps(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	u8 val8;
	u16 val16;
	u32 val32;
	int retry, retval;

	/* set RPWM IMR */
	val8 = rtl8xxxu_read8(priv, REG_FTIMR + 1);
	val8 |= IMR0_CPWM >> 8;
	rtl8xxxu_write8(priv, REG_FTIMR + 1, val8);

	/* Tx Pause */
	rtl8xxxu_write8(priv, REG_TXPAUSE, 0xff);

	retry = 100;
	retval = -EBUSY;

	/*
	 * Poll 32 bit wide REG_SCH_TX_CMD for 0x00000000 to ensure no TX is pending.
	 */
	do {
		val32 = rtl8xxxu_read32(priv, REG_SCH_TX_CMD);
		if (!val32) {
			retval = 0;
			break;
		}
	} while (retry--);

	if (!retry) {
		dev_warn(dev, "Failed to flush TX queue\n");
		retval = -EBUSY;
		goto out;
	}

	/* Disable CCK and OFDM, clock gated */
	val8 = rtl8xxxu_read8(priv, REG_SYS_FUNC);
	val8 &= ~SYS_FUNC_BBRSTB;
	rtl8xxxu_write8(priv, REG_SYS_FUNC, val8);

	udelay(2);

	/* Whole BB is reset */
	val8 = rtl8xxxu_read8(priv, REG_SYS_FUNC);
	val8 &= ~SYS_FUNC_BB_GLB_RSTN;
	rtl8xxxu_write8(priv, REG_SYS_FUNC, val8);

	/* Reset MAC TRX */
	val16 = rtl8xxxu_read16(priv, REG_CR);
	val16 |= 0x3f;
	val16 &= ~(CR_MAC_TX_ENABLE | CR_MAC_RX_ENABLE | CR_SECURITY_ENABLE);
	rtl8xxxu_write16(priv, REG_CR, val16);

	/* Respond TxOK to scheduler */
	val8 = rtl8xxxu_read8(priv, REG_DUAL_TSF_RST);
	val8 |= DUAL_TSF_TX_OK;
	rtl8xxxu_write8(priv, REG_DUAL_TSF_RST, val8);

out:
	return retval;
}

static int rtl8188fu_power_on(struct rtl8xxxu_priv *priv)
{
	u16 val16;
	int ret;

	rtl8188f_disabled_to_emu(priv);

	ret = rtl8188f_emu_to_active(priv);
	if (ret)
		goto exit;

	rtl8xxxu_write8(priv, REG_CR, 0);

	val16 = rtl8xxxu_read16(priv, REG_CR);

	val16 |= (CR_HCI_TXDMA_ENABLE | CR_HCI_RXDMA_ENABLE |
		 CR_TXDMA_ENABLE | CR_RXDMA_ENABLE |
		 CR_PROTOCOL_ENABLE | CR_SCHEDULE_ENABLE |
		 CR_SECURITY_ENABLE | CR_CALTIMER_ENABLE);
	rtl8xxxu_write16(priv, REG_CR, val16);

exit:
	return ret;
}

static void rtl8188fu_power_off(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u16 val16;

	rtl8xxxu_flush_fifo(priv);

	val16 = rtl8xxxu_read16(priv, REG_GPIO_MUXCFG);
	val16 &= ~BIT(12);
	rtl8xxxu_write16(priv, REG_GPIO_MUXCFG, val16);

	rtl8xxxu_write32(priv, REG_HISR0, 0xFFFFFFFF);
	rtl8xxxu_write32(priv, REG_HISR1, 0xFFFFFFFF);

	/* Stop Tx Report Timer. 0x4EC[Bit1]=b'0 */
	val8 = rtl8xxxu_read8(priv, REG_TX_REPORT_CTRL);
	val8 &= ~TX_REPORT_CTRL_TIMER_ENABLE;
	rtl8xxxu_write8(priv, REG_TX_REPORT_CTRL, val8);

	/* Turn off RF */
	rtl8xxxu_write8(priv, REG_RF_CTRL, 0x00);

	/* Reset Firmware if running in RAM */
	if (rtl8xxxu_read8(priv, REG_MCU_FW_DL) & MCU_FW_RAM_SEL)
		rtl8xxxu_firmware_self_reset(priv);

	rtl8188fu_active_to_lps(priv);

	/* Reset MCU */
	val16 = rtl8xxxu_read16(priv, REG_SYS_FUNC);
	val16 &= ~SYS_FUNC_CPU_ENABLE;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, val16);

	/* Reset MCU ready status */
	rtl8xxxu_write8(priv, REG_MCU_FW_DL, 0x00);

	rtl8188fu_active_to_emu(priv);
	rtl8188fu_emu_to_disabled(priv);
}

#define PPG_BB_GAIN_2G_TXA_OFFSET_8188F 0xee
#define PPG_BB_GAIN_2G_TX_OFFSET_MASK 0x0f

static void rtl8188f_enable_rf(struct rtl8xxxu_priv *priv)
{
	u32 val32;
	u8 pg_pwrtrim = 0xff, val8;
	s8 bb_gain;

	/* Somehow this is not found in the efuse we read earlier. */
	rtl8xxxu_read_efuse8(priv, PPG_BB_GAIN_2G_TXA_OFFSET_8188F, &pg_pwrtrim);

	if (pg_pwrtrim != 0xff) {
		bb_gain = pg_pwrtrim & PPG_BB_GAIN_2G_TX_OFFSET_MASK;

		if (bb_gain == PPG_BB_GAIN_2G_TX_OFFSET_MASK)
			bb_gain = 0;
		else if (bb_gain & 1)
			bb_gain = bb_gain >> 1;
		else
			bb_gain = -(bb_gain >> 1);

		val8 = abs(bb_gain);
		if (bb_gain > 0)
			val8 |= BIT(5);

		val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_55);
		val32 &= ~0xfc000;
		val32 |= val8 << 14;
		rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_55, val32);
	}

	rtl8xxxu_write8(priv, REG_RF_CTRL, RF_ENABLE | RF_RSTB | RF_SDMRSTB);

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_TRX_PATH_ENABLE);
	val32 &= ~(OFDM_RF_PATH_RX_MASK | OFDM_RF_PATH_TX_MASK);
	val32 |= OFDM_RF_PATH_RX_A | OFDM_RF_PATH_TX_A;
	rtl8xxxu_write32(priv, REG_OFDM0_TRX_PATH_ENABLE, val32);

	rtl8xxxu_write8(priv, REG_TXPAUSE, 0x00);
}

static void rtl8188f_disable_rf(struct rtl8xxxu_priv *priv)
{
	u32 val32;

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_TRX_PATH_ENABLE);
	val32 &= ~OFDM_RF_PATH_TX_MASK;
	rtl8xxxu_write32(priv, REG_OFDM0_TRX_PATH_ENABLE, val32);

	/* Power down RF module */
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_AC, 0);
}

static void rtl8188f_usb_quirks(struct rtl8xxxu_priv *priv)
{
	u16 val16;
	u32 val32;

	val16 = rtl8xxxu_read16(priv, REG_CR);
	val16 |= (CR_MAC_TX_ENABLE | CR_MAC_RX_ENABLE);
	rtl8xxxu_write16(priv, REG_CR, val16);

	val32 = rtl8xxxu_read32(priv, REG_TXDMA_OFFSET_CHK);
	val32 |= TXDMA_OFFSET_DROP_DATA_EN;
	rtl8xxxu_write32(priv, REG_TXDMA_OFFSET_CHK, val32);
}

#define XTAL1	GENMASK(22, 17)
#define XTAL0	GENMASK(16, 11)

void rtl8188f_set_crystal_cap(struct rtl8xxxu_priv *priv, u8 crystal_cap)
{
	struct rtl8xxxu_cfo_tracking *cfo = &priv->cfo_tracking;
	u32 val32;

	if (crystal_cap == cfo->crystal_cap)
		return;

	val32 = rtl8xxxu_read32(priv, REG_AFE_XTAL_CTRL);

	dev_dbg(&priv->udev->dev,
	        "%s: Adjusting crystal cap from 0x%x (actually 0x%lx 0x%lx) to 0x%x\n",
	        __func__,
	        cfo->crystal_cap,
	        FIELD_GET(XTAL1, val32),
	        FIELD_GET(XTAL0, val32),
	        crystal_cap);

	val32 &= ~(XTAL1 | XTAL0);
	val32 |= FIELD_PREP(XTAL1, crystal_cap) |
		 FIELD_PREP(XTAL0, crystal_cap);
	rtl8xxxu_write32(priv, REG_AFE_XTAL_CTRL, val32);

	cfo->crystal_cap = crystal_cap;
}

static s8 rtl8188f_cck_rssi(struct rtl8xxxu_priv *priv, struct rtl8723au_phy_stats *phy_stats)
{
	u8 cck_agc_rpt = phy_stats->cck_agc_rpt_ofdm_cfosho_a;
	s8 rx_pwr_all = 0x00;
	u8 vga_idx, lna_idx;

	lna_idx = u8_get_bits(cck_agc_rpt, CCK_AGC_RPT_LNA_IDX_MASK);
	vga_idx = u8_get_bits(cck_agc_rpt, CCK_AGC_RPT_VGA_IDX_MASK);

	switch (lna_idx) {
	case 7:
		if (vga_idx <= 27)
			rx_pwr_all = -100 + 2 * (27 - vga_idx);
		else
			rx_pwr_all = -100;
		break;
	case 5:
		rx_pwr_all = -74 + 2 * (21 - vga_idx);
		break;
	case 3:
		rx_pwr_all = -60 + 2 * (20 - vga_idx);
		break;
	case 1:
		rx_pwr_all = -44 + 2 * (19 - vga_idx);
		break;
	default:
		break;
	}

	return rx_pwr_all;
}

struct rtl8xxxu_fileops rtl8188fu_fops = {
	.identify_chip = rtl8188fu_identify_chip,
	.parse_efuse = rtl8188fu_parse_efuse,
	.load_firmware = rtl8188fu_load_firmware,
	.power_on = rtl8188fu_power_on,
	.power_off = rtl8188fu_power_off,
	.read_efuse = rtl8xxxu_read_efuse,
	.reset_8051 = rtl8xxxu_reset_8051,
	.llt_init = rtl8xxxu_auto_llt_table,
	.init_phy_bb = rtl8188fu_init_phy_bb,
	.init_phy_rf = rtl8188fu_init_phy_rf,
	.phy_init_antenna_selection = rtl8723bu_phy_init_antenna_selection,
	.phy_lc_calibrate = rtl8188f_phy_lc_calibrate,
	.phy_iq_calibrate = rtl8188fu_phy_iq_calibrate,
	.config_channel = rtl8188fu_config_channel,
	.parse_rx_desc = rtl8xxxu_parse_rxdesc24,
	.parse_phystats = rtl8723au_rx_parse_phystats,
	.init_aggregation = rtl8188fu_init_aggregation,
	.init_statistics = rtl8188fu_init_statistics,
	.init_burst = rtl8xxxu_init_burst,
	.enable_rf = rtl8188f_enable_rf,
	.disable_rf = rtl8188f_disable_rf,
	.usb_quirks = rtl8188f_usb_quirks,
	.set_tx_power = rtl8188f_set_tx_power,
	.update_rate_mask = rtl8xxxu_gen2_update_rate_mask,
	.report_connect = rtl8xxxu_gen2_report_connect,
	.report_rssi = rtl8xxxu_gen2_report_rssi,
	.fill_txdesc = rtl8xxxu_fill_txdesc_v2,
	.set_crystal_cap = rtl8188f_set_crystal_cap,
	.cck_rssi = rtl8188f_cck_rssi,
	.writeN_block_size = 128,
	.rx_desc_size = sizeof(struct rtl8xxxu_rxdesc24),
	.tx_desc_size = sizeof(struct rtl8xxxu_txdesc40),
	.has_s0s1 = 1,
	.has_tx_report = 1,
	.gen2_thermal_meter = 1,
	.needs_full_init = 1,
	.init_reg_rxfltmap = 1,
	.init_reg_pkt_life_time = 1,
	.init_reg_hmtfr = 1,
	.ampdu_max_time = 0x70,
	.ustime_tsf_edca = 0x28,
	.max_aggr_num = 0x0c14,
	.supports_ap = 1,
	.max_macid_num = 16,
	.max_sec_cam_num = 16,
	.supports_concurrent = 1,
	.adda_1t_init = 0x03c00014,
	.adda_1t_path_on = 0x03c00014,
	.trxff_boundary = 0x3f7f,
	.pbp_rx = PBP_PAGE_SIZE_256,
	.pbp_tx = PBP_PAGE_SIZE_256,
	.mactable = rtl8188f_mac_init_table,
	.total_page_num = TX_TOTAL_PAGE_NUM_8188F,
	.page_num_hi = TX_PAGE_NUM_HI_PQ_8188F,
	.page_num_lo = TX_PAGE_NUM_LO_PQ_8188F,
	.page_num_norm = TX_PAGE_NUM_NORM_PQ_8188F,
};
