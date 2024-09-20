// SPDX-License-Identifier: GPL-2.0-only
/*
 * RTL8XXXU mac80211 USB driver - 8710bu aka 8188gu specific subdriver
 *
 * Copyright (c) 2023 Bitterblue Smith <rtl8821cerfe2@gmail.com>
 *
 * Portions copied from existing rtl8xxxu code:
 * Copyright (c) 2014 - 2017 Jes Sorensen <Jes.Sorensen@gmail.com>
 *
 * Portions, notably calibration code:
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 */

#include "regs.h"
#include "rtl8xxxu.h"

static const struct rtl8xxxu_reg8val rtl8710b_mac_init_table[] = {
	{0x421, 0x0F}, {0x428, 0x0A}, {0x429, 0x10}, {0x430, 0x00},
	{0x431, 0x00}, {0x432, 0x00}, {0x433, 0x01}, {0x434, 0x04},
	{0x435, 0x05}, {0x436, 0x07}, {0x437, 0x08}, {0x43C, 0x04},
	{0x43D, 0x05}, {0x43E, 0x07}, {0x43F, 0x08}, {0x440, 0x5D},
	{0x441, 0x01}, {0x442, 0x00}, {0x444, 0x10}, {0x445, 0x00},
	{0x446, 0x00}, {0x447, 0x00}, {0x448, 0x00}, {0x449, 0xF0},
	{0x44A, 0x0F}, {0x44B, 0x3E}, {0x44C, 0x10}, {0x44D, 0x00},
	{0x44E, 0x00}, {0x44F, 0x00}, {0x450, 0x00}, {0x451, 0xF0},
	{0x452, 0x0F}, {0x453, 0x00}, {0x456, 0x5E}, {0x460, 0x66},
	{0x461, 0x66}, {0x4C8, 0xFF}, {0x4C9, 0x08}, {0x4CC, 0xFF},
	{0x4CD, 0xFF}, {0x4CE, 0x01}, {0x500, 0x26}, {0x501, 0xA2},
	{0x502, 0x2F}, {0x503, 0x00}, {0x504, 0x28}, {0x505, 0xA3},
	{0x506, 0x5E}, {0x507, 0x00}, {0x508, 0x2B}, {0x509, 0xA4},
	{0x50A, 0x5E}, {0x50B, 0x00}, {0x50C, 0x4F}, {0x50D, 0xA4},
	{0x50E, 0x00}, {0x50F, 0x00}, {0x512, 0x1C}, {0x514, 0x0A},
	{0x516, 0x0A}, {0x525, 0x4F}, {0x550, 0x10}, {0x551, 0x10},
	{0x559, 0x02}, {0x55C, 0x28}, {0x55D, 0xFF}, {0x605, 0x30},
	{0x608, 0x0E}, {0x609, 0x2A}, {0x620, 0xFF}, {0x621, 0xFF},
	{0x622, 0xFF}, {0x623, 0xFF}, {0x624, 0xFF}, {0x625, 0xFF},
	{0x626, 0xFF}, {0x627, 0xFF}, {0x638, 0x28}, {0x63C, 0x0A},
	{0x63D, 0x0A}, {0x63E, 0x0C}, {0x63F, 0x0C}, {0x640, 0x40},
	{0x642, 0x40}, {0x643, 0x00}, {0x652, 0xC8}, {0x66A, 0xB0},
	{0x66E, 0x05}, {0x700, 0x21}, {0x701, 0x43}, {0x702, 0x65},
	{0x703, 0x87}, {0x708, 0x21}, {0x709, 0x43}, {0x70A, 0x65},
	{0x70B, 0x87},
	{0xffff, 0xff},
};

/* If updating the phy init tables, also update rtl8710b_revise_cck_tx_psf(). */
static const struct rtl8xxxu_reg32val rtl8710bu_qfn48m_u_phy_init_table[] = {
	{0x800, 0x80045700}, {0x804, 0x00000001},
	{0x808, 0x00FC8000}, {0x80C, 0x0000000A},
	{0x810, 0x10001331}, {0x814, 0x020C3D10},
	{0x818, 0x00200385}, {0x81C, 0x00000000},
	{0x820, 0x01000100}, {0x824, 0x00390204},
	{0x828, 0x00000000}, {0x82C, 0x00000000},
	{0x830, 0x00000000}, {0x834, 0x00000000},
	{0x838, 0x00000000}, {0x83C, 0x00000000},
	{0x840, 0x00010000}, {0x844, 0x00000000},
	{0x848, 0x00000000}, {0x84C, 0x00000000},
	{0x850, 0x00030000}, {0x854, 0x00000000},
	{0x858, 0x7E1A569A}, {0x85C, 0x569A569A},
	{0x860, 0x00000130}, {0x864, 0x20000000},
	{0x868, 0x00000000}, {0x86C, 0x27272700},
	{0x870, 0x00050000}, {0x874, 0x25005000},
	{0x878, 0x00000808}, {0x87C, 0x004F0201},
	{0x880, 0xB0000B1E}, {0x884, 0x00000007},
	{0x888, 0x00000000}, {0x88C, 0xCCC400C0},
	{0x890, 0x00000800}, {0x894, 0xFFFFFFFE},
	{0x898, 0x40302010}, {0x89C, 0x00706050},
	{0x900, 0x00000000}, {0x904, 0x00000023},
	{0x908, 0x00000000}, {0x90C, 0x81121111},
	{0x910, 0x00000402}, {0x914, 0x00000201},
	{0x920, 0x18C6318C}, {0x924, 0x0000018C},
	{0x948, 0x99000000}, {0x94C, 0x00000010},
	{0x950, 0x00003000}, {0x954, 0x5A880000},
	{0x958, 0x4BC6D87A}, {0x95C, 0x04EB9B79},
	{0x96C, 0x00000003}, {0x970, 0x00000000},
	{0x974, 0x00000000}, {0x978, 0x00000000},
	{0x97C, 0x13000000}, {0x980, 0x00000000},
	{0xA00, 0x00D046C8}, {0xA04, 0x80FF800C},
	{0xA08, 0x84838300}, {0xA0C, 0x2E20100F},
	{0xA10, 0x9500BB78}, {0xA14, 0x1114D028},
	{0xA18, 0x00881117}, {0xA1C, 0x89140F00},
	{0xA20, 0xE82C0001}, {0xA24, 0x64B80C1C},
	{0xA28, 0x00008810}, {0xA2C, 0x00D30000},
	{0xA70, 0x101FBF00}, {0xA74, 0x00000007},
	{0xA78, 0x00000900}, {0xA7C, 0x225B0606},
	{0xA80, 0x218075B1}, {0xA84, 0x00200000},
	{0xA88, 0x040C0000}, {0xA8C, 0x12345678},
	{0xA90, 0xABCDEF00}, {0xA94, 0x001B1B89},
	{0xA98, 0x00000000}, {0xA9C, 0x80020000},
	{0xAA0, 0x00000000}, {0xAA4, 0x0000000C},
	{0xAA8, 0xCA110058}, {0xAAC, 0x01235667},
	{0xAB0, 0x00000000}, {0xAB4, 0x20201402},
	{0xB2C, 0x00000000}, {0xC00, 0x48071D40},
	{0xC04, 0x03A05611}, {0xC08, 0x000000E4},
	{0xC0C, 0x6C6C6C6C}, {0xC10, 0x18800000},
	{0xC14, 0x40000100}, {0xC18, 0x08800000},
	{0xC1C, 0x40000100}, {0xC20, 0x00000000},
	{0xC24, 0x00000000}, {0xC28, 0x00000000},
	{0xC2C, 0x00000000}, {0xC30, 0x69E9AC4A},
	{0xC34, 0x31000040}, {0xC38, 0x21688080},
	{0xC3C, 0x0000170C}, {0xC40, 0x1F78403F},
	{0xC44, 0x00010036}, {0xC48, 0xEC020107},
	{0xC4C, 0x007F037F}, {0xC50, 0x69553420},
	{0xC54, 0x43BC0094}, {0xC58, 0x00013169},
	{0xC5C, 0x00250492}, {0xC60, 0x00280A00},
	{0xC64, 0x7112848B}, {0xC68, 0x47C074FF},
	{0xC6C, 0x00000036}, {0xC70, 0x2C7F000D},
	{0xC74, 0x020600DB}, {0xC78, 0x0000001F},
	{0xC7C, 0x00B91612}, {0xC80, 0x390000E4},
	{0xC84, 0x11F60000}, {0xC88, 0x1051B75F},
	{0xC8C, 0x20200109}, {0xC90, 0x00091521},
	{0xC94, 0x00000000}, {0xC98, 0x00121820},
	{0xC9C, 0x00007F7F}, {0xCA0, 0x00011000},
	{0xCA4, 0x800000A0}, {0xCA8, 0x84E6C606},
	{0xCAC, 0x00000060}, {0xCB0, 0x00000000},
	{0xCB4, 0x00000000}, {0xCB8, 0x00000000},
	{0xCBC, 0x28000000}, {0xCC0, 0x1051B75F},
	{0xCC4, 0x00000109}, {0xCC8, 0x000442D6},
	{0xCCC, 0x00000000}, {0xCD0, 0x000001C8},
	{0xCD4, 0x001C8000}, {0xCD8, 0x00000100},
	{0xCDC, 0x40100000}, {0xCE0, 0x00222220},
	{0xCE4, 0x10000000}, {0xCE8, 0x37644302},
	{0xCEC, 0x2F97D40C}, {0xD00, 0x04030740},
	{0xD04, 0x40020401}, {0xD08, 0x0000907F},
	{0xD0C, 0x20010201}, {0xD10, 0xA0633333},
	{0xD14, 0x3333BC53}, {0xD18, 0x7A8F5B6F},
	{0xD2C, 0xCB979975}, {0xD30, 0x00000000},
	{0xD34, 0x40608000}, {0xD38, 0x88000000},
	{0xD3C, 0xC0127353}, {0xD40, 0x00000000},
	{0xD44, 0x00000000}, {0xD48, 0x00000000},
	{0xD4C, 0x00000000}, {0xD50, 0x00006528},
	{0xD54, 0x00000000}, {0xD58, 0x00000282},
	{0xD5C, 0x30032064}, {0xD60, 0x4653DE68},
	{0xD64, 0x04518A3C}, {0xD68, 0x00002101},
	{0xE00, 0x2D2D2D2D}, {0xE04, 0x2D2D2D2D},
	{0xE08, 0x0390272D}, {0xE10, 0x2D2D2D2D},
	{0xE14, 0x2D2D2D2D}, {0xE18, 0x2D2D2D2D},
	{0xE1C, 0x2D2D2D2D}, {0xE28, 0x00000000},
	{0xE30, 0x1000DC1F}, {0xE34, 0x10008C1F},
	{0xE38, 0x02140102}, {0xE3C, 0x681604C2},
	{0xE40, 0x01007C00}, {0xE44, 0x01004800},
	{0xE48, 0xFB000000}, {0xE4C, 0x000028D1},
	{0xE50, 0x1000DC1F}, {0xE54, 0x10008C1F},
	{0xE58, 0x02140102}, {0xE5C, 0x28160D05},
	{0xE60, 0x0000C008}, {0xE68, 0x001B25A4},
	{0xE64, 0x281600A0}, {0xE6C, 0x01C00010},
	{0xE70, 0x01C00010}, {0xE74, 0x02000010},
	{0xE78, 0x02000010}, {0xE7C, 0x02000010},
	{0xE80, 0x02000010}, {0xE84, 0x01C00010},
	{0xE88, 0x02000010}, {0xE8C, 0x01C00010},
	{0xED0, 0x01C00010}, {0xED4, 0x01C00010},
	{0xED8, 0x01C00010}, {0xEDC, 0x00000010},
	{0xEE0, 0x00000010}, {0xEEC, 0x03C00010},
	{0xF14, 0x00000003}, {0xF00, 0x00100300},
	{0xF08, 0x0000800B}, {0xF0C, 0x0000F007},
	{0xF10, 0x0000A487}, {0xF1C, 0x80000064},
	{0xF38, 0x00030155}, {0xF3C, 0x0000003A},
	{0xF4C, 0x13000000}, {0xF50, 0x00000000},
	{0xF18, 0x00000000},
	{0xffff, 0xffffffff},
};

/* If updating the phy init tables, also update rtl8710b_revise_cck_tx_psf(). */
static const struct rtl8xxxu_reg32val rtl8710bu_qfn48m_s_phy_init_table[] = {
	{0x800, 0x80045700}, {0x804, 0x00000001},
	{0x808, 0x00FC8000}, {0x80C, 0x0000000A},
	{0x810, 0x10001331}, {0x814, 0x020C3D10},
	{0x818, 0x00200385}, {0x81C, 0x00000000},
	{0x820, 0x01000100}, {0x824, 0x00390204},
	{0x828, 0x00000000}, {0x82C, 0x00000000},
	{0x830, 0x00000000}, {0x834, 0x00000000},
	{0x838, 0x00000000}, {0x83C, 0x00000000},
	{0x840, 0x00010000}, {0x844, 0x00000000},
	{0x848, 0x00000000}, {0x84C, 0x00000000},
	{0x850, 0x00030000}, {0x854, 0x00000000},
	{0x858, 0x7E1A569A}, {0x85C, 0x569A569A},
	{0x860, 0x00000130}, {0x864, 0x20000000},
	{0x868, 0x00000000}, {0x86C, 0x27272700},
	{0x870, 0x00050000}, {0x874, 0x25005000},
	{0x878, 0x00000808}, {0x87C, 0x004F0201},
	{0x880, 0xB0000B1E}, {0x884, 0x00000007},
	{0x888, 0x00000000}, {0x88C, 0xCCC400C0},
	{0x890, 0x00000800}, {0x894, 0xFFFFFFFE},
	{0x898, 0x40302010}, {0x89C, 0x00706050},
	{0x900, 0x00000000}, {0x904, 0x00000023},
	{0x908, 0x00000000}, {0x90C, 0x81121111},
	{0x910, 0x00000402}, {0x914, 0x00000201},
	{0x920, 0x18C6318C}, {0x924, 0x0000018C},
	{0x948, 0x99000000}, {0x94C, 0x00000010},
	{0x950, 0x00003000}, {0x954, 0x5A880000},
	{0x958, 0x4BC6D87A}, {0x95C, 0x04EB9B79},
	{0x96C, 0x00000003}, {0x970, 0x00000000},
	{0x974, 0x00000000}, {0x978, 0x00000000},
	{0x97C, 0x13000000}, {0x980, 0x00000000},
	{0xA00, 0x00D046C8}, {0xA04, 0x80FF800C},
	{0xA08, 0x84838300}, {0xA0C, 0x2A20100F},
	{0xA10, 0x9500BB78}, {0xA14, 0x1114D028},
	{0xA18, 0x00881117}, {0xA1C, 0x89140F00},
	{0xA20, 0xE82C0001}, {0xA24, 0x64B80C1C},
	{0xA28, 0x00008810}, {0xA2C, 0x00D30000},
	{0xA70, 0x101FBF00}, {0xA74, 0x00000007},
	{0xA78, 0x00000900}, {0xA7C, 0x225B0606},
	{0xA80, 0x218075B1}, {0xA84, 0x00200000},
	{0xA88, 0x040C0000}, {0xA8C, 0x12345678},
	{0xA90, 0xABCDEF00}, {0xA94, 0x001B1B89},
	{0xA98, 0x00000000}, {0xA9C, 0x80020000},
	{0xAA0, 0x00000000}, {0xAA4, 0x0000000C},
	{0xAA8, 0xCA110058}, {0xAAC, 0x01235667},
	{0xAB0, 0x00000000}, {0xAB4, 0x20201402},
	{0xB2C, 0x00000000}, {0xC00, 0x48071D40},
	{0xC04, 0x03A05611}, {0xC08, 0x000000E4},
	{0xC0C, 0x6C6C6C6C}, {0xC10, 0x18800000},
	{0xC14, 0x40000100}, {0xC18, 0x08800000},
	{0xC1C, 0x40000100}, {0xC20, 0x00000000},
	{0xC24, 0x00000000}, {0xC28, 0x00000000},
	{0xC2C, 0x00000000}, {0xC30, 0x69E9AC4A},
	{0xC34, 0x31000040}, {0xC38, 0x21688080},
	{0xC3C, 0x0000170C}, {0xC40, 0x1F78403F},
	{0xC44, 0x00010036}, {0xC48, 0xEC020107},
	{0xC4C, 0x007F037F}, {0xC50, 0x69553420},
	{0xC54, 0x43BC0094}, {0xC58, 0x00013169},
	{0xC5C, 0x00250492}, {0xC60, 0x00280A00},
	{0xC64, 0x7112848B}, {0xC68, 0x47C074FF},
	{0xC6C, 0x00000036}, {0xC70, 0x2C7F000D},
	{0xC74, 0x020600DB}, {0xC78, 0x0000001F},
	{0xC7C, 0x00B91612}, {0xC80, 0x390000E4},
	{0xC84, 0x11F60000}, {0xC88, 0x1051B75F},
	{0xC8C, 0x20200109}, {0xC90, 0x00091521},
	{0xC94, 0x00000000}, {0xC98, 0x00121820},
	{0xC9C, 0x00007F7F}, {0xCA0, 0x00011000},
	{0xCA4, 0x800000A0}, {0xCA8, 0x84E6C606},
	{0xCAC, 0x00000060}, {0xCB0, 0x00000000},
	{0xCB4, 0x00000000}, {0xCB8, 0x00000000},
	{0xCBC, 0x28000000}, {0xCC0, 0x1051B75F},
	{0xCC4, 0x00000109}, {0xCC8, 0x000442D6},
	{0xCCC, 0x00000000}, {0xCD0, 0x000001C8},
	{0xCD4, 0x001C8000}, {0xCD8, 0x00000100},
	{0xCDC, 0x40100000}, {0xCE0, 0x00222220},
	{0xCE4, 0x10000000}, {0xCE8, 0x37644302},
	{0xCEC, 0x2F97D40C}, {0xD00, 0x04030740},
	{0xD04, 0x40020401}, {0xD08, 0x0000907F},
	{0xD0C, 0x20010201}, {0xD10, 0xA0633333},
	{0xD14, 0x3333BC53}, {0xD18, 0x7A8F5B6F},
	{0xD2C, 0xCB979975}, {0xD30, 0x00000000},
	{0xD34, 0x40608000}, {0xD38, 0x88000000},
	{0xD3C, 0xC0127353}, {0xD40, 0x00000000},
	{0xD44, 0x00000000}, {0xD48, 0x00000000},
	{0xD4C, 0x00000000}, {0xD50, 0x00006528},
	{0xD54, 0x00000000}, {0xD58, 0x00000282},
	{0xD5C, 0x30032064}, {0xD60, 0x4653DE68},
	{0xD64, 0x04518A3C}, {0xD68, 0x00002101},
	{0xE00, 0x2D2D2D2D}, {0xE04, 0x2D2D2D2D},
	{0xE08, 0x0390272D}, {0xE10, 0x2D2D2D2D},
	{0xE14, 0x2D2D2D2D}, {0xE18, 0x2D2D2D2D},
	{0xE1C, 0x2D2D2D2D}, {0xE28, 0x00000000},
	{0xE30, 0x1000DC1F}, {0xE34, 0x10008C1F},
	{0xE38, 0x02140102}, {0xE3C, 0x681604C2},
	{0xE40, 0x01007C00}, {0xE44, 0x01004800},
	{0xE48, 0xFB000000}, {0xE4C, 0x000028D1},
	{0xE50, 0x1000DC1F}, {0xE54, 0x10008C1F},
	{0xE58, 0x02140102}, {0xE5C, 0x28160D05},
	{0xE60, 0x0000C008}, {0xE68, 0x001B25A4},
	{0xE64, 0x281600A0}, {0xE6C, 0x01C00010},
	{0xE70, 0x01C00010}, {0xE74, 0x02000010},
	{0xE78, 0x02000010}, {0xE7C, 0x02000010},
	{0xE80, 0x02000010}, {0xE84, 0x01C00010},
	{0xE88, 0x02000010}, {0xE8C, 0x01C00010},
	{0xED0, 0x01C00010}, {0xED4, 0x01C00010},
	{0xED8, 0x01C00010}, {0xEDC, 0x00000010},
	{0xEE0, 0x00000010}, {0xEEC, 0x03C00010},
	{0xF14, 0x00000003}, {0xF00, 0x00100300},
	{0xF08, 0x0000800B}, {0xF0C, 0x0000F007},
	{0xF10, 0x0000A487}, {0xF1C, 0x80000064},
	{0xF38, 0x00030155}, {0xF3C, 0x0000003A},
	{0xF4C, 0x13000000}, {0xF50, 0x00000000},
	{0xF18, 0x00000000},
	{0xffff, 0xffffffff},
};

static const struct rtl8xxxu_reg32val rtl8710b_agc_table[] = {
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
	{0xC78, 0xE01C0001}, {0xC78, 0xC31D0001},
	{0xC78, 0xC21E0001}, {0xC78, 0xC11F0001},
	{0xC78, 0xC0200001}, {0xC78, 0xA3210001},
	{0xC78, 0xA2220001}, {0xC78, 0xA1230001},
	{0xC78, 0xA0240001}, {0xC78, 0x86250001},
	{0xC78, 0x85260001}, {0xC78, 0x84270001},
	{0xC78, 0x83280001}, {0xC78, 0x82290001},
	{0xC78, 0x812A0001}, {0xC78, 0x802B0001},
	{0xC78, 0x632C0001}, {0xC78, 0x622D0001},
	{0xC78, 0x612E0001}, {0xC78, 0x602F0001},
	{0xC78, 0x42300001}, {0xC78, 0x41310001},
	{0xC78, 0x40320001}, {0xC78, 0x23330001},
	{0xC78, 0x22340001}, {0xC78, 0x21350001},
	{0xC78, 0x20360001}, {0xC78, 0x02370001},
	{0xC78, 0x01380001}, {0xC78, 0x00390001},
	{0xC78, 0x003A0001}, {0xC78, 0x003B0001},
	{0xC78, 0x003C0001}, {0xC78, 0x003D0001},
	{0xC78, 0x003E0001}, {0xC78, 0x003F0001},
	{0xC78, 0xF7400001}, {0xC78, 0xF7410001},
	{0xC78, 0xF7420001}, {0xC78, 0xF7430001},
	{0xC78, 0xF7440001}, {0xC78, 0xF7450001},
	{0xC78, 0xF7460001}, {0xC78, 0xF7470001},
	{0xC78, 0xF7480001}, {0xC78, 0xF6490001},
	{0xC78, 0xF34A0001}, {0xC78, 0xF24B0001},
	{0xC78, 0xF14C0001}, {0xC78, 0xF04D0001},
	{0xC78, 0xD14E0001}, {0xC78, 0xD04F0001},
	{0xC78, 0xB5500001}, {0xC78, 0xB4510001},
	{0xC78, 0xB3520001}, {0xC78, 0xB2530001},
	{0xC78, 0xB1540001}, {0xC78, 0xB0550001},
	{0xC78, 0xAF560001}, {0xC78, 0xAE570001},
	{0xC78, 0xAD580001}, {0xC78, 0xAC590001},
	{0xC78, 0xAB5A0001}, {0xC78, 0xAA5B0001},
	{0xC78, 0xA95C0001}, {0xC78, 0xA85D0001},
	{0xC78, 0xA75E0001}, {0xC78, 0xA65F0001},
	{0xC78, 0xA5600001}, {0xC78, 0xA4610001},
	{0xC78, 0xA3620001}, {0xC78, 0xA2630001},
	{0xC78, 0xA1640001}, {0xC78, 0xA0650001},
	{0xC78, 0x87660001}, {0xC78, 0x86670001},
	{0xC78, 0x85680001}, {0xC78, 0x84690001},
	{0xC78, 0x836A0001}, {0xC78, 0x826B0001},
	{0xC78, 0x816C0001}, {0xC78, 0x806D0001},
	{0xC78, 0x636E0001}, {0xC78, 0x626F0001},
	{0xC78, 0x61700001}, {0xC78, 0x60710001},
	{0xC78, 0x42720001}, {0xC78, 0x41730001},
	{0xC78, 0x40740001}, {0xC78, 0x23750001},
	{0xC78, 0x22760001}, {0xC78, 0x21770001},
	{0xC78, 0x20780001}, {0xC78, 0x03790001},
	{0xC78, 0x027A0001}, {0xC78, 0x017B0001},
	{0xC78, 0x007C0001}, {0xC78, 0x007D0001},
	{0xC78, 0x007E0001}, {0xC78, 0x007F0001},
	{0xC50, 0x69553422}, {0xC50, 0x69553420},
	{0xffff, 0xffffffff}
};

static const struct rtl8xxxu_rfregval rtl8710bu_qfn48m_u_radioa_init_table[] = {
	{0x00, 0x00030000}, {0x08, 0x00008400},
	{0x17, 0x00000000}, {0x18, 0x00000C01},
	{0x19, 0x000739D2}, {0x1C, 0x00000C4C},
	{0x1B, 0x00000C6C}, {0x1E, 0x00080009},
	{0x1F, 0x00000880}, {0x2F, 0x0001A060},
	{0x3F, 0x00015000}, {0x42, 0x000060C0},
	{0x57, 0x000D0000}, {0x58, 0x000C0160},
	{0x67, 0x00001552}, {0x83, 0x00000000},
	{0xB0, 0x000FF9F0}, {0xB1, 0x00010018},
	{0xB2, 0x00054C00}, {0xB4, 0x0004486B},
	{0xB5, 0x0000112A}, {0xB6, 0x0000053E},
	{0xB7, 0x00014408}, {0xB8, 0x00010200},
	{0xB9, 0x00080801}, {0xBA, 0x00040001},
	{0xBB, 0x00000400}, {0xBF, 0x000C0000},
	{0xC2, 0x00002400}, {0xC3, 0x00000009},
	{0xC4, 0x00040C91}, {0xC5, 0x00099999},
	{0xC6, 0x000000A3}, {0xC7, 0x00088820},
	{0xC8, 0x00076C06}, {0xC9, 0x00000000},
	{0xCA, 0x00080000}, {0xDF, 0x00000180},
	{0xEF, 0x000001A8}, {0x3D, 0x00000003},
	{0x3D, 0x00080003}, {0x51, 0x000F1E69},
	{0x52, 0x000FBF6C}, {0x53, 0x0000032F},
	{0x54, 0x00055007}, {0x56, 0x000517F0},
	{0x35, 0x000000F4}, {0x35, 0x00000179},
	{0x35, 0x000002F4}, {0x36, 0x00000BF8},
	{0x36, 0x00008BF8}, {0x36, 0x00010BF8},
	{0x36, 0x00018BF8}, {0x18, 0x00000C01},
	{0x5A, 0x00048000}, {0x5A, 0x00048000},
	{0x34, 0x0000ADF5}, {0x34, 0x00009DF2},
	{0x34, 0x00008DEF}, {0x34, 0x00007DEC},
	{0x34, 0x00006DE9}, {0x34, 0x00005CEC},
	{0x34, 0x00004CE9}, {0x34, 0x00003C6C},
	{0x34, 0x00002C69}, {0x34, 0x0000106E},
	{0x34, 0x0000006B}, {0x84, 0x00048000},
	{0x87, 0x00000065}, {0x8E, 0x00065540},
	{0xDF, 0x00000110}, {0x86, 0x0000002A},
	{0x8F, 0x00088000}, {0x81, 0x0003FD80},
	{0xEF, 0x00082000}, {0x3B, 0x000F0F00},
	{0x3B, 0x000E0E00}, {0x3B, 0x000DFE00},
	{0x3B, 0x000C0D00}, {0x3B, 0x000B0C00},
	{0x3B, 0x000A0500}, {0x3B, 0x00090400},
	{0x3B, 0x00080000}, {0x3B, 0x00070F00},
	{0x3B, 0x00060E00}, {0x3B, 0x00050A00},
	{0x3B, 0x00040D00}, {0x3B, 0x00030C00},
	{0x3B, 0x00020500}, {0x3B, 0x00010400},
	{0x3B, 0x00000000}, {0xEF, 0x00080000},
	{0xEF, 0x00088000}, {0x3B, 0x00000170},
	{0x3B, 0x000C0030}, {0xEF, 0x00080000},
	{0xEF, 0x00080000}, {0x30, 0x00010000},
	{0x31, 0x0000000F}, {0x32, 0x00047EFE},
	{0xEF, 0x00000000}, {0x00, 0x00010159},
	{0x18, 0x0000FC01}, {0xFE, 0x00000000},
	{0x00, 0x00033D95},
	{0xff, 0xffffffff}
};

static const struct rtl8xxxu_rfregval rtl8710bu_qfn48m_s_radioa_init_table[] = {
	{0x00, 0x00030000}, {0x08, 0x00008400},
	{0x17, 0x00000000}, {0x18, 0x00000C01},
	{0x19, 0x000739D2}, {0x1C, 0x00000C4C},
	{0x1B, 0x00000C6C}, {0x1E, 0x00080009},
	{0x1F, 0x00000880}, {0x2F, 0x0001A060},
	{0x3F, 0x00015000}, {0x42, 0x000060C0},
	{0x57, 0x000D0000}, {0x58, 0x000C0160},
	{0x67, 0x00001552}, {0x83, 0x00000000},
	{0xB0, 0x000FF9F0}, {0xB1, 0x00010018},
	{0xB2, 0x00054C00}, {0xB4, 0x0004486B},
	{0xB5, 0x0000112A}, {0xB6, 0x0000053E},
	{0xB7, 0x00014408}, {0xB8, 0x00010200},
	{0xB9, 0x00080801}, {0xBA, 0x00040001},
	{0xBB, 0x00000400}, {0xBF, 0x000C0000},
	{0xC2, 0x00002400}, {0xC3, 0x00000009},
	{0xC4, 0x00040C91}, {0xC5, 0x00099999},
	{0xC6, 0x000000A3}, {0xC7, 0x00088820},
	{0xC8, 0x00076C06}, {0xC9, 0x00000000},
	{0xCA, 0x00080000}, {0xDF, 0x00000180},
	{0xEF, 0x000001A8}, {0x3D, 0x00000003},
	{0x3D, 0x00080003}, {0x51, 0x000F1E69},
	{0x52, 0x000FBF6C}, {0x53, 0x0000032F},
	{0x54, 0x00055007}, {0x56, 0x000517F0},
	{0x35, 0x000000F4}, {0x35, 0x00000179},
	{0x35, 0x000002F4}, {0x36, 0x00000BF8},
	{0x36, 0x00008BF8}, {0x36, 0x00010BF8},
	{0x36, 0x00018BF8}, {0x18, 0x00000C01},
	{0x5A, 0x00048000}, {0x5A, 0x00048000},
	{0x34, 0x0000ADF5}, {0x34, 0x00009DF2},
	{0x34, 0x00008DEF}, {0x34, 0x00007DEC},
	{0x34, 0x00006DE9}, {0x34, 0x00005CEC},
	{0x34, 0x00004CE9}, {0x34, 0x00003C6C},
	{0x34, 0x00002C69}, {0x34, 0x0000106E},
	{0x34, 0x0000006B}, {0x84, 0x00048000},
	{0x87, 0x00000065}, {0x8E, 0x00065540},
	{0xDF, 0x00000110}, {0x86, 0x0000002A},
	{0x8F, 0x00088000}, {0x81, 0x0003FD80},
	{0xEF, 0x00082000}, {0x3B, 0x000F0F00},
	{0x3B, 0x000E0E00}, {0x3B, 0x000DFE00},
	{0x3B, 0x000C0D00}, {0x3B, 0x000B0C00},
	{0x3B, 0x000A0500}, {0x3B, 0x00090400},
	{0x3B, 0x00080000}, {0x3B, 0x00070F00},
	{0x3B, 0x00060E00}, {0x3B, 0x00050A00},
	{0x3B, 0x00040D00}, {0x3B, 0x00030C00},
	{0x3B, 0x00020500}, {0x3B, 0x00010400},
	{0x3B, 0x00000000}, {0xEF, 0x00080000},
	{0xEF, 0x00088000}, {0x3B, 0x000000B0},
	{0x3B, 0x000C0030}, {0xEF, 0x00080000},
	{0xEF, 0x00080000}, {0x30, 0x00010000},
	{0x31, 0x0000000F}, {0x32, 0x00047EFE},
	{0xEF, 0x00000000}, {0x00, 0x00010159},
	{0x18, 0x0000FC01}, {0xFE, 0x00000000},
	{0x00, 0x00033D95},
	{0xff, 0xffffffff}
};

static u32 rtl8710b_indirect_read32(struct rtl8xxxu_priv *priv, u32 addr)
{
	struct device *dev = &priv->udev->dev;
	u32 val32, value = 0xffffffff;
	u8 polling_count = 0xff;

	if (!IS_ALIGNED(addr, 4)) {
		dev_warn(dev, "%s: Aborting because 0x%x is not a multiple of 4.\n",
			 __func__, addr);
		return value;
	}

	mutex_lock(&priv->syson_indirect_access_mutex);

	rtl8xxxu_write32(priv, REG_USB_HOST_INDIRECT_ADDR_8710B, addr);
	rtl8xxxu_write32(priv, REG_EFUSE_INDIRECT_CTRL_8710B, NORMAL_REG_READ_OFFSET);

	do
		val32 = rtl8xxxu_read32(priv, REG_EFUSE_INDIRECT_CTRL_8710B);
	while ((val32 & BIT(31)) && (--polling_count > 0));

	if (polling_count == 0)
		dev_warn(dev, "%s: Failed to read from 0x%x, 0x806c = 0x%x\n",
			 __func__, addr, val32);
	else
		value = rtl8xxxu_read32(priv, REG_USB_HOST_INDIRECT_DATA_8710B);

	mutex_unlock(&priv->syson_indirect_access_mutex);

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_REG_READ)
		dev_info(dev, "%s(%04x) = 0x%08x\n", __func__, addr, value);

	return value;
}

static void rtl8710b_indirect_write32(struct rtl8xxxu_priv *priv, u32 addr, u32 val)
{
	struct device *dev = &priv->udev->dev;
	u8 polling_count = 0xff;
	u32 val32;

	if (!IS_ALIGNED(addr, 4)) {
		dev_warn(dev, "%s: Aborting because 0x%x is not a multiple of 4.\n",
			 __func__, addr);
		return;
	}

	mutex_lock(&priv->syson_indirect_access_mutex);

	rtl8xxxu_write32(priv, REG_USB_HOST_INDIRECT_ADDR_8710B, addr);
	rtl8xxxu_write32(priv, REG_USB_HOST_INDIRECT_DATA_8710B, val);
	rtl8xxxu_write32(priv, REG_EFUSE_INDIRECT_CTRL_8710B, NORMAL_REG_WRITE_OFFSET);

	do
		val32 = rtl8xxxu_read32(priv, REG_EFUSE_INDIRECT_CTRL_8710B);
	while ((val32 & BIT(31)) && (--polling_count > 0));

	if (polling_count == 0)
		dev_warn(dev, "%s: Failed to write 0x%x to 0x%x, 0x806c = 0x%x\n",
			 __func__, val, addr, val32);

	mutex_unlock(&priv->syson_indirect_access_mutex);

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_REG_WRITE)
		dev_info(dev, "%s(%04x) = 0x%08x\n", __func__, addr, val);
}

static u32 rtl8710b_read_syson_reg(struct rtl8xxxu_priv *priv, u32 addr)
{
	return rtl8710b_indirect_read32(priv, addr | SYSON_REG_BASE_ADDR_8710B);
}

static void rtl8710b_write_syson_reg(struct rtl8xxxu_priv *priv, u32 addr, u32 val)
{
	rtl8710b_indirect_write32(priv, addr | SYSON_REG_BASE_ADDR_8710B, val);
}

static int rtl8710b_read_efuse8(struct rtl8xxxu_priv *priv, u16 offset, u8 *data)
{
	u32 val32;
	int i;

	/* Write Address */
	rtl8xxxu_write32(priv, REG_USB_HOST_INDIRECT_ADDR_8710B, offset);

	rtl8xxxu_write32(priv, REG_EFUSE_INDIRECT_CTRL_8710B, EFUSE_READ_OFFSET);

	/* Poll for data read */
	val32 = rtl8xxxu_read32(priv, REG_EFUSE_INDIRECT_CTRL_8710B);
	for (i = 0; i < RTL8XXXU_MAX_REG_POLL; i++) {
		val32 = rtl8xxxu_read32(priv, REG_EFUSE_INDIRECT_CTRL_8710B);
		if (!(val32 & BIT(31)))
			break;
	}

	if (i == RTL8XXXU_MAX_REG_POLL)
		return -EIO;

	val32 = rtl8xxxu_read32(priv, REG_USB_HOST_INDIRECT_DATA_8710B);

	*data = val32 & 0xff;
	return 0;
}

#define EEPROM_PACKAGE_TYPE_8710B	0xF8
#define PACKAGE_QFN48M_U		0xee
#define PACKAGE_QFN48M_S		0xfe

static int rtl8710bu_identify_chip(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	u32 cfg0, cfg2, vendor;
	u8 package_type = 0x7; /* a nonsense value */

	sprintf(priv->chip_name, "8710BU");
	priv->rtl_chip = RTL8710B;
	priv->rf_paths = 1;
	priv->rx_paths = 1;
	priv->tx_paths = 1;
	priv->has_wifi = 1;

	cfg0 = rtl8710b_read_syson_reg(priv, REG_SYS_SYSTEM_CFG0_8710B);
	priv->chip_cut = cfg0 & 0xf;

	if (cfg0 & BIT(16)) {
		dev_info(dev, "%s: Unsupported test chip\n", __func__);
		return -EOPNOTSUPP;
	}

	vendor = u32_get_bits(cfg0, 0xc0);

	/* SMIC and TSMC are swapped compared to rtl8xxxu_identify_vendor_2bits */
	switch (vendor) {
	case 0:
		sprintf(priv->chip_vendor, "SMIC");
		priv->vendor_smic = 1;
		break;
	case 1:
		sprintf(priv->chip_vendor, "TSMC");
		break;
	case 2:
		sprintf(priv->chip_vendor, "UMC");
		priv->vendor_umc = 1;
		break;
	default:
		sprintf(priv->chip_vendor, "unknown");
		break;
	}

	rtl8710b_read_efuse8(priv, EEPROM_PACKAGE_TYPE_8710B, &package_type);

	if (package_type == 0xff) {
		dev_warn(dev, "Package type is undefined. Assuming it based on the vendor.\n");

		if (priv->vendor_umc) {
			package_type = PACKAGE_QFN48M_U;
		} else if (priv->vendor_smic) {
			package_type = PACKAGE_QFN48M_S;
		} else {
			dev_warn(dev, "The vendor is neither UMC nor SMIC. Assuming the package type is QFN48M_U.\n");

			/*
			 * In this case the vendor driver doesn't set
			 * the package type to anything, which is the
			 * same as setting it to PACKAGE_DEFAULT (0).
			 */
			package_type = PACKAGE_QFN48M_U;
		}
	} else if (package_type != PACKAGE_QFN48M_S &&
		   package_type != PACKAGE_QFN48M_U) {
		dev_warn(dev, "Failed to read the package type. Assuming it's the default QFN48M_U.\n");

		/*
		 * In this case the vendor driver actually sets it to
		 * PACKAGE_DEFAULT, but that selects the same values
		 * from the init tables as PACKAGE_QFN48M_U.
		 */
		package_type = PACKAGE_QFN48M_U;
	}

	priv->package_type = package_type;

	dev_dbg(dev, "Package type: 0x%x\n", package_type);

	cfg2 = rtl8710b_read_syson_reg(priv, REG_SYS_SYSTEM_CFG2_8710B);
	priv->rom_rev = cfg2 & 0xf;

	return rtl8xxxu_config_endpoints_no_sie(priv);
}

static void rtl8710b_revise_cck_tx_psf(struct rtl8xxxu_priv *priv, u8 channel)
{
	if (channel == 13) {
		/* Normal values */
		rtl8xxxu_write32(priv, REG_CCK0_TX_FILTER2, 0x64B80C1C);
		rtl8xxxu_write32(priv, REG_CCK0_DEBUG_PORT, 0x00008810);
		rtl8xxxu_write32(priv, REG_CCK0_TX_FILTER3, 0x01235667);
		/* Special value for channel 13 */
		rtl8xxxu_write32(priv, REG_CCK0_TX_FILTER1, 0xd1d80001);
	} else if (channel == 14) {
		/* Special values for channel 14 */
		rtl8xxxu_write32(priv, REG_CCK0_TX_FILTER2, 0x0000B81C);
		rtl8xxxu_write32(priv, REG_CCK0_DEBUG_PORT, 0x00000000);
		rtl8xxxu_write32(priv, REG_CCK0_TX_FILTER3, 0x00003667);
		/* Normal value */
		rtl8xxxu_write32(priv, REG_CCK0_TX_FILTER1, 0xE82C0001);
	} else {
		/* Restore normal values from the phy init table */
		rtl8xxxu_write32(priv, REG_CCK0_TX_FILTER2, 0x64B80C1C);
		rtl8xxxu_write32(priv, REG_CCK0_DEBUG_PORT, 0x00008810);
		rtl8xxxu_write32(priv, REG_CCK0_TX_FILTER3, 0x01235667);
		rtl8xxxu_write32(priv, REG_CCK0_TX_FILTER1, 0xE82C0001);
	}
}

static void rtl8710bu_config_channel(struct ieee80211_hw *hw)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	bool ht40 = conf_is_ht40(&hw->conf);
	u8 channel, subchannel = 0;
	bool sec_ch_above = 0;
	u32 val32;
	u16 val16;

	channel = (u8)hw->conf.chandef.chan->hw_value;

	if (conf_is_ht40_plus(&hw->conf)) {
		sec_ch_above = 1;
		channel += 2;
		subchannel = 2;
	} else if (conf_is_ht40_minus(&hw->conf)) {
		sec_ch_above = 0;
		channel -= 2;
		subchannel = 1;
	}

	/* Set channel */
	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_MODE_AG);
	u32p_replace_bits(&val32, channel, MODE_AG_CHANNEL_MASK);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_MODE_AG, val32);

	rtl8710b_revise_cck_tx_psf(priv, channel);

	/* Set bandwidth mode */
	val16 = rtl8xxxu_read16(priv, REG_WMAC_TRXPTCL_CTL);
	val16 &= ~WMAC_TRXPTCL_CTL_BW_MASK;
	if (ht40)
		val16 |= WMAC_TRXPTCL_CTL_BW_40;
	rtl8xxxu_write16(priv, REG_WMAC_TRXPTCL_CTL, val16);

	rtl8xxxu_write8(priv, REG_DATA_SUBCHANNEL, subchannel);

	val32 = rtl8xxxu_read32(priv, REG_FPGA0_RF_MODE);
	u32p_replace_bits(&val32, ht40, FPGA_RF_MODE);
	rtl8xxxu_write32(priv, REG_FPGA0_RF_MODE, val32);

	val32 = rtl8xxxu_read32(priv, REG_FPGA1_RF_MODE);
	u32p_replace_bits(&val32, ht40, FPGA_RF_MODE);
	rtl8xxxu_write32(priv, REG_FPGA1_RF_MODE, val32);

	if (ht40) {
		/* Set Control channel to upper or lower. */
		val32 = rtl8xxxu_read32(priv, REG_CCK0_SYSTEM);
		u32p_replace_bits(&val32, !sec_ch_above, CCK0_SIDEBAND);
		rtl8xxxu_write32(priv, REG_CCK0_SYSTEM, val32);
	}

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

	val32 = rtl8xxxu_read32(priv, REG_FPGA0_XB_RF_INT_OE);
	val32 &= ~BIT(30);
	val32 |= BIT(29);
	rtl8xxxu_write32(priv, REG_FPGA0_XB_RF_INT_OE, val32);

	if (ht40) {
		val32 = rtl8xxxu_read32(priv, REG_OFDM_RX_DFIR);
		val32 &= ~BIT(19);
		rtl8xxxu_write32(priv, REG_OFDM_RX_DFIR, val32);

		val32 = rtl8xxxu_read32(priv, REG_OFDM_RX_DFIR);
		val32 &= ~GENMASK(23, 20);
		rtl8xxxu_write32(priv, REG_OFDM_RX_DFIR, val32);

		val32 = rtl8xxxu_read32(priv, REG_OFDM_RX_DFIR);
		val32 &= ~GENMASK(27, 24);
		rtl8xxxu_write32(priv, REG_OFDM_RX_DFIR, val32);

		/* RF TRX_BW */
		val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_MODE_AG);
		val32 &= ~MODE_AG_BW_MASK;
		val32 |= MODE_AG_BW_40MHZ_8723B;
		rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_MODE_AG, val32);
	} else {
		val32 = rtl8xxxu_read32(priv, REG_OFDM_RX_DFIR);
		val32 |= BIT(19);
		rtl8xxxu_write32(priv, REG_OFDM_RX_DFIR, val32);

		val32 = rtl8xxxu_read32(priv, REG_OFDM_RX_DFIR);
		val32 &= ~GENMASK(23, 20);
		val32 |= BIT(23);
		rtl8xxxu_write32(priv, REG_OFDM_RX_DFIR, val32);

		val32 = rtl8xxxu_read32(priv, REG_OFDM_RX_DFIR);
		val32 &= ~GENMASK(27, 24);
		val32 |= BIT(27) | BIT(25);
		rtl8xxxu_write32(priv, REG_OFDM_RX_DFIR, val32);

		/* RF TRX_BW */
		val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_MODE_AG);
		val32 &= ~MODE_AG_BW_MASK;
		val32 |= MODE_AG_BW_20MHZ_8723B;
		rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_MODE_AG, val32);
	}
}

static void rtl8710bu_init_aggregation(struct rtl8xxxu_priv *priv)
{
	u32 agg_rx;
	u8 agg_ctrl;

	/* RX aggregation */
	agg_ctrl = rtl8xxxu_read8(priv, REG_TRXDMA_CTRL);
	agg_ctrl &= ~TRXDMA_CTRL_RXDMA_AGG_EN;

	agg_rx = rtl8xxxu_read32(priv, REG_RXDMA_AGG_PG_TH);
	agg_rx &= ~RXDMA_USB_AGG_ENABLE;
	agg_rx &= ~0xFF0F; /* reset agg size and timeout */

	rtl8xxxu_write8(priv, REG_TRXDMA_CTRL, agg_ctrl);
	rtl8xxxu_write32(priv, REG_RXDMA_AGG_PG_TH, agg_rx);
}

static void rtl8710bu_init_statistics(struct rtl8xxxu_priv *priv)
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

static int rtl8710b_read_efuse(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	u8 val8, word_mask, header, extheader;
	u16 efuse_addr, offset;
	int i, ret = 0;
	u32 val32;

	val32 = rtl8710b_read_syson_reg(priv, REG_SYS_EEPROM_CTRL0_8710B);
	priv->boot_eeprom = u32_get_bits(val32, EEPROM_BOOT);
	priv->has_eeprom = u32_get_bits(val32, EEPROM_ENABLE);

	/* Default value is 0xff */
	memset(priv->efuse_wifi.raw, 0xff, EFUSE_MAP_LEN);

	efuse_addr = 0;
	while (efuse_addr < EFUSE_REAL_CONTENT_LEN_8723A) {
		u16 map_addr;

		ret = rtl8710b_read_efuse8(priv, efuse_addr++, &header);
		if (ret || header == 0xff)
			goto exit;

		if ((header & 0x1f) == 0x0f) {	/* extended header */
			offset = (header & 0xe0) >> 5;

			ret = rtl8710b_read_efuse8(priv, efuse_addr++, &extheader);
			if (ret)
				goto exit;

			/* All words disabled */
			if ((extheader & 0x0f) == 0x0f)
				continue;

			offset |= ((extheader & 0xf0) >> 1);
			word_mask = extheader & 0x0f;
		} else {
			offset = (header >> 4) & 0x0f;
			word_mask = header & 0x0f;
		}

		/* Get word enable value from PG header */

		/* We have 8 bits to indicate validity */
		map_addr = offset * 8;
		for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
			/* Check word enable condition in the section */
			if (word_mask & BIT(i)) {
				map_addr += 2;
				continue;
			}

			ret = rtl8710b_read_efuse8(priv, efuse_addr++, &val8);
			if (ret)
				goto exit;
			if (map_addr >= EFUSE_MAP_LEN - 1) {
				dev_warn(dev, "%s: Illegal map_addr (%04x), efuse corrupt!\n",
					 __func__, map_addr);
				ret = -EINVAL;
				goto exit;
			}
			priv->efuse_wifi.raw[map_addr++] = val8;

			ret = rtl8710b_read_efuse8(priv, efuse_addr++, &val8);
			if (ret)
				goto exit;
			priv->efuse_wifi.raw[map_addr++] = val8;
		}
	}

exit:

	return ret;
}

static int rtl8710bu_parse_efuse(struct rtl8xxxu_priv *priv)
{
	struct rtl8710bu_efuse *efuse = &priv->efuse_wifi.efuse8710bu;

	if (efuse->rtl_id != cpu_to_le16(0x8195))
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

static int rtl8710bu_load_firmware(struct rtl8xxxu_priv *priv)
{
	if (priv->vendor_smic) {
		return rtl8xxxu_load_firmware(priv, "rtlwifi/rtl8710bufw_SMIC.bin");
	} else if (priv->vendor_umc) {
		return rtl8xxxu_load_firmware(priv, "rtlwifi/rtl8710bufw_UMC.bin");
	} else {
		dev_err(&priv->udev->dev, "We have no suitable firmware for this chip.\n");
		return -1;
	}
}

static void rtl8710bu_init_phy_bb(struct rtl8xxxu_priv *priv)
{
	const struct rtl8xxxu_reg32val *phy_init_table;
	u32 val32;

	/* Enable BB and RF */
	val32 = rtl8xxxu_read32(priv, REG_SYS_FUNC_8710B);
	val32 |= GENMASK(17, 16) | GENMASK(26, 24);
	rtl8xxxu_write32(priv, REG_SYS_FUNC_8710B, val32);

	if (priv->package_type == PACKAGE_QFN48M_U)
		phy_init_table = rtl8710bu_qfn48m_u_phy_init_table;
	else
		phy_init_table = rtl8710bu_qfn48m_s_phy_init_table;

	rtl8xxxu_init_phy_regs(priv, phy_init_table);

	rtl8xxxu_init_phy_regs(priv, rtl8710b_agc_table);
}

static int rtl8710bu_init_phy_rf(struct rtl8xxxu_priv *priv)
{
	const struct rtl8xxxu_rfregval *radioa_init_table;

	if (priv->package_type == PACKAGE_QFN48M_U)
		radioa_init_table = rtl8710bu_qfn48m_u_radioa_init_table;
	else
		radioa_init_table = rtl8710bu_qfn48m_s_radioa_init_table;

	return rtl8xxxu_init_phy_rf(priv, radioa_init_table, RF_A);
}

static int rtl8710bu_iqk_path_a(struct rtl8xxxu_priv *priv, u32 *lok_result)
{
	u32 reg_eac, reg_e94, reg_e9c, val32, path_sel_bb;
	int result = 0;

	path_sel_bb = rtl8xxxu_read32(priv, REG_S0S1_PATH_SWITCH);

	rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, 0x99000000);

	/*
	 * Leave IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	u32p_replace_bits(&val32, 0, 0xffffff00);
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
	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA);
	val32 |= BIT(11);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA, val32);
	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_PAD_TXG);
	u32p_replace_bits(&val32, 0x1ed, 0x00fff);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_PAD_TXG, val32);

	/* enter IQK mode */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	u32p_replace_bits(&val32, 0x808000, 0xffffff00);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/* path-A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x18008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x38008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_A, 0x821403ff);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, 0x28160c06);

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x02002911);

	/* One shot, path A LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xfa000000);
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf8000000);

	mdelay(10);

	rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, path_sel_bb);

	/*
	 * Leave IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	u32p_replace_bits(&val32, 0, 0xffffff00);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA);
	val32 &= ~BIT(11);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA, val32);

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

static int rtl8710bu_rx_iqk_path_a(struct rtl8xxxu_priv *priv, u32 lok_result)
{
	u32 reg_ea4, reg_eac, reg_e94, reg_e9c, val32, path_sel_bb, tmp;
	int result = 0;

	path_sel_bb = rtl8xxxu_read32(priv, REG_S0S1_PATH_SWITCH);

	rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, 0x99000000);

	/*
	 * Leave IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	u32p_replace_bits(&val32, 0, 0xffffff00);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/* modify RXIQK mode table */
	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_WE_LUT);
	val32 |= 0x80000;
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_WE_LUT, val32);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_RCK_OS, 0x30000);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G1, 0x0000f);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G2, 0xf1173);

	/* PA,PAD gain adjust */
	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA);
	val32 |= BIT(11);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA, val32);
	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_PAD_TXG);
	u32p_replace_bits(&val32, 0xf, 0x003e0);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_PAD_TXG, val32);

	/*
	 * Enter IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	u32p_replace_bits(&val32, 0x808000, 0xffffff00);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/* path-A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x18008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x38008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_A, 0x8216129f);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, 0x28160c00);

	/*
	 * Tx IQK setting
	 */
	rtl8xxxu_write32(priv, REG_TX_IQK, 0x01007c00);
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x0046a911);

	/* One shot, path A LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf9000000);
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf8000000);

	mdelay(10);

	/* Check failed */
	reg_eac = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_A_2);
	reg_e94 = rtl8xxxu_read32(priv, REG_TX_POWER_BEFORE_IQK_A);
	reg_e9c = rtl8xxxu_read32(priv, REG_TX_POWER_AFTER_IQK_A);

	if (!(reg_eac & BIT(28)) &&
	    ((reg_e94 & 0x03ff0000) != 0x01420000) &&
	    ((reg_e9c & 0x03ff0000) != 0x00420000)) {
		result |= 0x01;
	} else { /* If TX not OK, ignore RX */

		/* reload RF path */
		rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, path_sel_bb);

		/*
		 * Leave IQK mode
		 */
		val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
		u32p_replace_bits(&val32, 0, 0xffffff00);
		rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

		val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA);
		val32 &= ~BIT(11);
		rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA, val32);

		return result;
	}

	val32 = 0x80007c00 | (reg_e94 & 0x3ff0000) | ((reg_e9c & 0x3ff0000) >> 16);
	rtl8xxxu_write32(priv, REG_TX_IQK, val32);

	/*
	 * Modify RX IQK mode table
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	u32p_replace_bits(&val32, 0, 0xffffff00);
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
	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA);
	val32 |= BIT(11);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA, val32);
	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_PAD_TXG);
	u32p_replace_bits(&val32, 0x2a, 0x00fff);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_PAD_TXG, val32);

	/*
	 * Enter IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	u32p_replace_bits(&val32, 0x808000, 0xffffff00);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/*
	 * RX IQK setting
	 */
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

	/* path-A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x18008c1c);

	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, 0x2816169f);

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x0046a911);

	/* One shot, path A LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf9000000);
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf8000000);

	mdelay(10);

	/* reload RF path */
	rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, path_sel_bb);

	/*
	 * Leave IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	u32p_replace_bits(&val32, 0, 0xffffff00);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA);
	val32 &= ~BIT(11);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_GAIN_CCA, val32);

	/* reload LOK value */
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXM_IDAC, lok_result);

	/* Check failed */
	reg_eac = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_A_2);
	reg_ea4 = rtl8xxxu_read32(priv, REG_RX_POWER_BEFORE_IQK_A_2);

	tmp = (reg_eac & 0x03ff0000) >> 16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	if (!(reg_eac & BIT(27)) &&
	    ((reg_ea4 & 0x03ff0000) != 0x01320000) &&
	    ((reg_eac & 0x03ff0000) != 0x00360000) &&
	    (((reg_ea4 & 0x03ff0000) >> 16) < 0x11a) &&
	    (((reg_ea4 & 0x03ff0000) >> 16) > 0xe6) &&
	    (tmp < 0x1a))
		result |= 0x02;

	return result;
}

static void rtl8710bu_phy_iqcalibrate(struct rtl8xxxu_priv *priv,
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
		REG_FPGA0_XB_RF_INT_OE, REG_CCK0_AFE_SETTING
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

	if (!priv->pi_enabled) {
		/* Switch BB to PI mode to do IQ Calibration */
		rtl8xxxu_write32(priv, REG_FPGA0_XA_HSSI_PARM1, 0x01000100);
		rtl8xxxu_write32(priv, REG_FPGA0_XB_HSSI_PARM1, 0x01000100);
	}

	/* MAC settings */
	val32 = rtl8xxxu_read32(priv, REG_TX_PTCL_CTRL);
	val32 |= 0x00ff0000;
	rtl8xxxu_write32(priv, REG_TX_PTCL_CTRL, val32);

	/* save RF path */
	path_sel_bb = rtl8xxxu_read32(priv, REG_S0S1_PATH_SWITCH);
	path_sel_rf = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_S0S1);

	/* BB setting */
	val32 = rtl8xxxu_read32(priv, REG_CCK0_AFE_SETTING);
	val32 |= 0x0f000000;
	rtl8xxxu_write32(priv, REG_CCK0_AFE_SETTING, val32);
	rtl8xxxu_write32(priv, REG_RX_WAIT_CCA, 0x03c00010);
	rtl8xxxu_write32(priv, REG_OFDM0_TRX_PATH_ENABLE, 0x03a05601);
	rtl8xxxu_write32(priv, REG_OFDM0_TR_MUX_PAR, 0x000800e4);
	rtl8xxxu_write32(priv, REG_FPGA0_XCD_RF_SW_CTRL, 0x25204000);

	/* IQ calibration setting */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	u32p_replace_bits(&val32, 0x808000, 0xffffff00);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);
	rtl8xxxu_write32(priv, REG_TX_IQK, 0x01007c00);
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

	for (i = 0; i < retry; i++) {
		path_a_ok = rtl8710bu_iqk_path_a(priv, &lok_result);

		if (path_a_ok == 0x01) {
			val32 = rtl8xxxu_read32(priv, REG_TX_POWER_BEFORE_IQK_A);
			result[t][0] = (val32 >> 16) & 0x3ff;

			val32 = rtl8xxxu_read32(priv, REG_TX_POWER_AFTER_IQK_A);
			result[t][1] = (val32 >> 16) & 0x3ff;
			break;
		} else {
			result[t][0] = 0x100;
			result[t][1] = 0x0;
		}
	}

	for (i = 0; i < retry; i++) {
		path_a_ok = rtl8710bu_rx_iqk_path_a(priv, lok_result);

		if (path_a_ok == 0x03) {
			val32 = rtl8xxxu_read32(priv, REG_RX_POWER_BEFORE_IQK_A_2);
			result[t][2] = (val32 >> 16) & 0x3ff;

			val32 = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_A_2);
			result[t][3] = (val32 >> 16) & 0x3ff;
			break;
		} else {
			result[t][2] = 0x100;
			result[t][3] = 0x0;
		}
	}

	if (!path_a_ok)
		dev_warn(dev, "%s: Path A IQK failed!\n", __func__);

	/* Back to BB mode, load original value */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	u32p_replace_bits(&val32, 0, 0xffffff00);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	if (t == 0)
		return;

	/* Reload ADDA power saving parameters */
	rtl8xxxu_restore_regs(priv, adda_regs, priv->adda_backup, RTL8XXXU_ADDA_REGS);

	/* Reload MAC parameters */
	rtl8xxxu_restore_mac_regs(priv, iqk_mac_regs, priv->mac_backup);

	/* Reload BB parameters */
	rtl8xxxu_restore_regs(priv, iqk_bb_regs, priv->bb_backup, RTL8XXXU_BB_REGS);

	/* Reload RF path */
	rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, path_sel_bb);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_S0S1, path_sel_rf);

	/* Restore RX initial gain */
	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XA_AGC_CORE1);
	u32p_replace_bits(&val32, 0x50, 0x000000ff);
	rtl8xxxu_write32(priv, REG_OFDM0_XA_AGC_CORE1, val32);
	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XA_AGC_CORE1);
	u32p_replace_bits(&val32, rx_initial_gain & 0xff, 0x000000ff);
	rtl8xxxu_write32(priv, REG_OFDM0_XA_AGC_CORE1, val32);

	/* Load 0xe30 IQC default value */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x01008c00);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x01008c00);
}

static void rtl8710bu_phy_iq_calibrate(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	int result[4][8]; /* last is final result */
	int i, candidate;
	bool path_a_ok;
	s32 reg_e94, reg_e9c, reg_ea4, reg_eac;
	s32 reg_tmp = 0;
	bool simu;
	u32 path_sel_bb;

	/* Save RF path */
	path_sel_bb = rtl8xxxu_read32(priv, REG_S0S1_PATH_SWITCH);

	memset(result, 0, sizeof(result));
	candidate = -1;

	path_a_ok = false;

	for (i = 0; i < 3; i++) {
		rtl8710bu_phy_iqcalibrate(priv, result, i);

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

	if (candidate >= 0) {
		reg_e94 = result[candidate][0];
		reg_e9c = result[candidate][1];
		reg_ea4 = result[candidate][2];
		reg_eac = result[candidate][3];

		dev_dbg(dev, "%s: candidate is %x\n", __func__, candidate);
		dev_dbg(dev, "%s: e94=%x e9c=%x ea4=%x eac=%x\n",
			__func__, reg_e94, reg_e9c, reg_ea4, reg_eac);

		path_a_ok = true;

		if (reg_e94)
			rtl8xxxu_fill_iqk_matrix_a(priv, path_a_ok, result,
						   candidate, (reg_ea4 == 0));
	}

	rtl8xxxu_save_regs(priv, rtl8xxxu_iqk_phy_iq_bb_reg,
			   priv->bb_recovery_backup, RTL8XXXU_BB_REGS);

	rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, path_sel_bb);
}

static int rtl8710b_emu_to_active(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	int count, ret = 0;

	/* AFE power mode selection: 1: LDO mode, 0: Power-cut mode */
	val8 = rtl8xxxu_read8(priv, 0x5d);
	val8 &= ~BIT(0);
	rtl8xxxu_write8(priv, 0x5d, val8);

	val8 = rtl8xxxu_read8(priv, REG_SYS_FUNC_8710B);
	val8 |= BIT(0);
	rtl8xxxu_write8(priv, REG_SYS_FUNC_8710B, val8);

	rtl8xxxu_write8(priv, 0x56, 0x0e);

	val8 = rtl8xxxu_read8(priv, 0x20);
	val8 |= BIT(0);
	rtl8xxxu_write8(priv, 0x20, val8);

	for (count = RTL8XXXU_MAX_REG_POLL; count; count--) {
		val8 = rtl8xxxu_read8(priv, 0x20);
		if (!(val8 & BIT(0)))
			break;

		udelay(10);
	}

	if (!count)
		ret = -EBUSY;

	return ret;
}

static int rtl8710bu_active_to_emu(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u32 val32;
	int count, ret = 0;

	/* Turn off RF */
	val32 = rtl8xxxu_read32(priv, REG_SYS_FUNC_8710B);
	val32 &= ~GENMASK(26, 24);
	rtl8xxxu_write32(priv, REG_SYS_FUNC_8710B, val32);

	/* BB reset */
	val32 = rtl8xxxu_read32(priv, REG_SYS_FUNC_8710B);
	val32 &= ~GENMASK(17, 16);
	rtl8xxxu_write32(priv, REG_SYS_FUNC_8710B, val32);

	/* Turn off MAC by HW state machine */
	val8 = rtl8xxxu_read8(priv, 0x20);
	val8 |= BIT(1);
	rtl8xxxu_write8(priv, 0x20, val8);

	for (count = RTL8XXXU_MAX_REG_POLL; count; count--) {
		val8 = rtl8xxxu_read8(priv, 0x20);
		if ((val8 & BIT(1)) == 0) {
			ret = 0;
			break;
		}
		udelay(10);
	}

	if (!count)
		ret = -EBUSY;

	return ret;
}

static int rtl8710bu_active_to_lps(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	u8 val8;
	u16 val16;
	u32 val32;
	int retry, retval;

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
		udelay(10);
	} while (retry--);

	if (!retry) {
		dev_warn(dev, "Failed to flush TX queue\n");
		retval = -EBUSY;
		return retval;
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
	val16 &= 0xff00;
	val16 |= CR_HCI_RXDMA_ENABLE | CR_HCI_TXDMA_ENABLE;
	val16 &= ~CR_SECURITY_ENABLE;
	rtl8xxxu_write16(priv, REG_CR, val16);

	/* Respond TxOK to scheduler */
	val8 = rtl8xxxu_read8(priv, REG_DUAL_TSF_RST);
	val8 |= DUAL_TSF_TX_OK;
	rtl8xxxu_write8(priv, REG_DUAL_TSF_RST, val8);

	return retval;
}

static int rtl8710bu_power_on(struct rtl8xxxu_priv *priv)
{
	u32 val32;
	u16 val16;
	u8 val8;
	int ret;

	rtl8xxxu_write8(priv, REG_USB_ACCESS_TIMEOUT, 0x80);

	val8 = rtl8xxxu_read8(priv, REG_SYS_ISO_CTRL);
	val8 &= ~BIT(5);
	rtl8xxxu_write8(priv, REG_SYS_ISO_CTRL, val8);

	val8 = rtl8xxxu_read8(priv, REG_SYS_FUNC_8710B);
	val8 |= BIT(0);
	rtl8xxxu_write8(priv, REG_SYS_FUNC_8710B, val8);

	val8 = rtl8xxxu_read8(priv, 0x20);
	val8 |= BIT(0);
	rtl8xxxu_write8(priv, 0x20, val8);

	rtl8xxxu_write8(priv, REG_AFE_CTRL_8710B, 0);

	val8 = rtl8xxxu_read8(priv, REG_WL_STATUS_8710B);
	val8 |= BIT(1);
	rtl8xxxu_write8(priv, REG_WL_STATUS_8710B, val8);

	ret = rtl8710b_emu_to_active(priv);
	if (ret)
		return ret;

	rtl8xxxu_write16(priv, REG_CR, 0);

	val16 = rtl8xxxu_read16(priv, REG_CR);

	val16 |= CR_HCI_TXDMA_ENABLE | CR_HCI_RXDMA_ENABLE |
		 CR_TXDMA_ENABLE | CR_RXDMA_ENABLE |
		 CR_PROTOCOL_ENABLE | CR_SCHEDULE_ENABLE |
		 CR_SECURITY_ENABLE | CR_CALTIMER_ENABLE;
	rtl8xxxu_write16(priv, REG_CR, val16);

	/* Enable hardware sequence number. */
	val8 = rtl8xxxu_read8(priv, REG_HWSEQ_CTRL);
	val8 |= 0x7f;
	rtl8xxxu_write8(priv, REG_HWSEQ_CTRL, val8);

	udelay(2);

	/*
	 * Technically the rest was in the rtl8710bu_hal_init function,
	 * not the power_on function, but it's fine because we only
	 * call power_on from init_device.
	 */

	val8 = rtl8xxxu_read8(priv, 0xfef9);
	val8 &= ~BIT(0);
	rtl8xxxu_write8(priv, 0xfef9, val8);

	/* Clear the 0x40000138[5] to prevent CM4 Suspend */
	val32 = rtl8710b_read_syson_reg(priv, 0x138);
	val32 &= ~BIT(5);
	rtl8710b_write_syson_reg(priv, 0x138, val32);

	return ret;
}

static void rtl8710bu_power_off(struct rtl8xxxu_priv *priv)
{
	u32 val32;
	u8 val8;

	rtl8xxxu_flush_fifo(priv);

	rtl8xxxu_write32(priv, REG_HISR0_8710B, 0xffffffff);
	rtl8xxxu_write32(priv, REG_HIMR0_8710B, 0x0);

	/* Set the 0x40000138[5] to allow CM4 Suspend */
	val32 = rtl8710b_read_syson_reg(priv, 0x138);
	val32 |= BIT(5);
	rtl8710b_write_syson_reg(priv, 0x138, val32);

	/* Stop rx */
	rtl8xxxu_write8(priv, REG_CR, 0x00);

	rtl8710bu_active_to_lps(priv);

	/* Reset MCU ? */
	val8 = rtl8xxxu_read8(priv, REG_8051FW_CTRL_V1_8710B + 3);
	val8 &= ~BIT(0);
	rtl8xxxu_write8(priv, REG_8051FW_CTRL_V1_8710B + 3, val8);

	/* Reset MCU ready status */
	rtl8xxxu_write8(priv, REG_8051FW_CTRL_V1_8710B, 0x00);

	rtl8710bu_active_to_emu(priv);
}

static void rtl8710b_reset_8051(struct rtl8xxxu_priv *priv)
{
	u8 val8;

	val8 = rtl8xxxu_read8(priv, REG_8051FW_CTRL_V1_8710B + 3);
	val8 &= ~BIT(0);
	rtl8xxxu_write8(priv, REG_8051FW_CTRL_V1_8710B + 3, val8);

	udelay(50);

	val8 = rtl8xxxu_read8(priv, REG_8051FW_CTRL_V1_8710B + 3);
	val8 |= BIT(0);
	rtl8xxxu_write8(priv, REG_8051FW_CTRL_V1_8710B + 3, val8);
}

static void rtl8710b_enable_rf(struct rtl8xxxu_priv *priv)
{
	u32 val32;

	rtl8xxxu_write8(priv, REG_RF_CTRL, RF_ENABLE | RF_RSTB | RF_SDMRSTB);

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_TRX_PATH_ENABLE);
	val32 &= ~(OFDM_RF_PATH_RX_MASK | OFDM_RF_PATH_TX_MASK);
	val32 |= OFDM_RF_PATH_RX_A | OFDM_RF_PATH_TX_A;
	rtl8xxxu_write32(priv, REG_OFDM0_TRX_PATH_ENABLE, val32);

	rtl8xxxu_write8(priv, REG_TXPAUSE, 0x00);
}

static void rtl8710b_disable_rf(struct rtl8xxxu_priv *priv)
{
	u32 val32;

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_TRX_PATH_ENABLE);
	val32 &= ~OFDM_RF_PATH_TX_MASK;
	rtl8xxxu_write32(priv, REG_OFDM0_TRX_PATH_ENABLE, val32);

	/* Power down RF module */
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_AC, 0);
}

static void rtl8710b_usb_quirks(struct rtl8xxxu_priv *priv)
{
	u16 val16;

	rtl8xxxu_gen2_usb_quirks(priv);

	val16 = rtl8xxxu_read16(priv, REG_CR);
	val16 |= (CR_MAC_TX_ENABLE | CR_MAC_RX_ENABLE);
	rtl8xxxu_write16(priv, REG_CR, val16);
}

#define XTAL1	GENMASK(29, 24)
#define XTAL0	GENMASK(23, 18)

static void rtl8710b_set_crystal_cap(struct rtl8xxxu_priv *priv, u8 crystal_cap)
{
	struct rtl8xxxu_cfo_tracking *cfo = &priv->cfo_tracking;
	u32 val32;

	if (crystal_cap == cfo->crystal_cap)
		return;

	val32 = rtl8710b_read_syson_reg(priv, REG_SYS_XTAL_CTRL0_8710B);

	dev_dbg(&priv->udev->dev,
		"%s: Adjusting crystal cap from 0x%x (actually 0x%x 0x%x) to 0x%x\n",
		__func__,
		cfo->crystal_cap,
		u32_get_bits(val32, XTAL1),
		u32_get_bits(val32, XTAL0),
		crystal_cap);

	u32p_replace_bits(&val32, crystal_cap, XTAL1);
	u32p_replace_bits(&val32, crystal_cap, XTAL0);
	rtl8710b_write_syson_reg(priv, REG_SYS_XTAL_CTRL0_8710B, val32);

	cfo->crystal_cap = crystal_cap;
}

static s8 rtl8710b_cck_rssi(struct rtl8xxxu_priv *priv, struct rtl8723au_phy_stats *phy_stats)
{
	struct jaguar2_phy_stats_type0 *phy_stats0 = (struct jaguar2_phy_stats_type0 *)phy_stats;
	u8 lna_idx = (phy_stats0->lna_h << 3) | phy_stats0->lna_l;
	u8 vga_idx = phy_stats0->vga;
	s8 rx_pwr_all = 0x00;

	switch (lna_idx) {
	case 7:
		rx_pwr_all = -52 - (2 * vga_idx);
		break;
	case 6:
		rx_pwr_all = -42 - (2 * vga_idx);
		break;
	case 5:
		rx_pwr_all = -36 - (2 * vga_idx);
		break;
	case 3:
		rx_pwr_all = -12 - (2 * vga_idx);
		break;
	case 2:
		rx_pwr_all = 0 - (2 * vga_idx);
		break;
	default:
		rx_pwr_all = 0;
		break;
	}

	return rx_pwr_all;
}

struct rtl8xxxu_fileops rtl8710bu_fops = {
	.identify_chip = rtl8710bu_identify_chip,
	.parse_efuse = rtl8710bu_parse_efuse,
	.load_firmware = rtl8710bu_load_firmware,
	.power_on = rtl8710bu_power_on,
	.power_off = rtl8710bu_power_off,
	.read_efuse = rtl8710b_read_efuse,
	.reset_8051 = rtl8710b_reset_8051,
	.llt_init = rtl8xxxu_auto_llt_table,
	.init_phy_bb = rtl8710bu_init_phy_bb,
	.init_phy_rf = rtl8710bu_init_phy_rf,
	.phy_lc_calibrate = rtl8188f_phy_lc_calibrate,
	.phy_iq_calibrate = rtl8710bu_phy_iq_calibrate,
	.config_channel = rtl8710bu_config_channel,
	.parse_rx_desc = rtl8xxxu_parse_rxdesc24,
	.parse_phystats = jaguar2_rx_parse_phystats,
	.init_aggregation = rtl8710bu_init_aggregation,
	.init_statistics = rtl8710bu_init_statistics,
	.init_burst = rtl8xxxu_init_burst,
	.enable_rf = rtl8710b_enable_rf,
	.disable_rf = rtl8710b_disable_rf,
	.usb_quirks = rtl8710b_usb_quirks,
	.set_tx_power = rtl8188f_set_tx_power,
	.update_rate_mask = rtl8xxxu_gen2_update_rate_mask,
	.report_connect = rtl8xxxu_gen2_report_connect,
	.report_rssi = rtl8xxxu_gen2_report_rssi,
	.fill_txdesc = rtl8xxxu_fill_txdesc_v2,
	.set_crystal_cap = rtl8710b_set_crystal_cap,
	.cck_rssi = rtl8710b_cck_rssi,
	.writeN_block_size = 4,
	.rx_desc_size = sizeof(struct rtl8xxxu_rxdesc24),
	.tx_desc_size = sizeof(struct rtl8xxxu_txdesc40),
	.has_tx_report = 1,
	.gen2_thermal_meter = 1,
	.needs_full_init = 1,
	.init_reg_rxfltmap = 1,
	.init_reg_pkt_life_time = 1,
	.init_reg_hmtfr = 1,
	.ampdu_max_time = 0x5e,
	/*
	 * The RTL8710BU vendor driver uses 0x50 here and it works fine,
	 * but in rtl8xxxu 0x50 causes slow upload and random packet loss. Why?
	 */
	.ustime_tsf_edca = 0x28,
	.max_aggr_num = 0x0c14,
	.supports_ap = 1,
	.max_macid_num = 16,
	.max_sec_cam_num = 32,
	.adda_1t_init = 0x03c00016,
	.adda_1t_path_on = 0x03c00016,
	.trxff_boundary = 0x3f7f,
	.pbp_rx = PBP_PAGE_SIZE_256,
	.pbp_tx = PBP_PAGE_SIZE_256,
	.mactable = rtl8710b_mac_init_table,
	.total_page_num = TX_TOTAL_PAGE_NUM_8723B,
	.page_num_hi = TX_PAGE_NUM_HI_PQ_8723B,
	.page_num_lo = TX_PAGE_NUM_LO_PQ_8723B,
	.page_num_norm = TX_PAGE_NUM_NORM_PQ_8723B,
};
