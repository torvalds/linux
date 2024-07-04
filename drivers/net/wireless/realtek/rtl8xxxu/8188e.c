// SPDX-License-Identifier: GPL-2.0-only
/*
 * RTL8XXXU mac80211 USB driver - 8188e specific subdriver
 *
 * Copyright (c) 2014 - 2016 Jes Sorensen <Jes.Sorensen@gmail.com>
 *
 * Portions, notably calibration code:
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This driver was written as a replacement for the vendor provided
 * rtl8723au driver. As the Realtek 8xxx chips are very similar in
 * their programming interface, I have started adding support for
 * additional 8xxx chips like the 8192cu, 8188cus, etc.
 */

#include "regs.h"
#include "rtl8xxxu.h"

static const struct rtl8xxxu_reg8val rtl8188e_mac_init_table[] = {
	{0x026, 0x41}, {0x027, 0x35}, {0x040, 0x00}, {0x421, 0x0f},
	{0x428, 0x0a}, {0x429, 0x10}, {0x430, 0x00}, {0x431, 0x01},
	{0x432, 0x02}, {0x433, 0x04}, {0x434, 0x05}, {0x435, 0x06},
	{0x436, 0x07}, {0x437, 0x08}, {0x438, 0x00}, {0x439, 0x00},
	{0x43a, 0x01}, {0x43b, 0x02}, {0x43c, 0x04}, {0x43d, 0x05},
	{0x43e, 0x06}, {0x43f, 0x07}, {0x440, 0x5d}, {0x441, 0x01},
	{0x442, 0x00}, {0x444, 0x15}, {0x445, 0xf0}, {0x446, 0x0f},
	{0x447, 0x00}, {0x458, 0x41}, {0x459, 0xa8}, {0x45a, 0x72},
	{0x45b, 0xb9}, {0x460, 0x66}, {0x461, 0x66}, {0x480, 0x08},
	{0x4c8, 0xff}, {0x4c9, 0x08}, {0x4cc, 0xff}, {0x4cd, 0xff},
	{0x4ce, 0x01}, {0x4d3, 0x01}, {0x500, 0x26}, {0x501, 0xa2},
	{0x502, 0x2f}, {0x503, 0x00}, {0x504, 0x28}, {0x505, 0xa3},
	{0x506, 0x5e}, {0x507, 0x00}, {0x508, 0x2b}, {0x509, 0xa4},
	{0x50a, 0x5e}, {0x50b, 0x00}, {0x50c, 0x4f}, {0x50d, 0xa4},
	{0x50e, 0x00}, {0x50f, 0x00}, {0x512, 0x1c}, {0x514, 0x0a},
	{0x516, 0x0a}, {0x525, 0x4f}, {0x550, 0x10}, {0x551, 0x10},
	{0x559, 0x02}, {0x55d, 0xff}, {0x605, 0x30}, {0x608, 0x0e},
	{0x609, 0x2a}, {0x620, 0xff}, {0x621, 0xff}, {0x622, 0xff},
	{0x623, 0xff}, {0x624, 0xff}, {0x625, 0xff}, {0x626, 0xff},
	{0x627, 0xff}, {0x63c, 0x08}, {0x63d, 0x08}, {0x63e, 0x0c},
	{0x63f, 0x0c}, {0x640, 0x40}, {0x652, 0x20}, {0x66e, 0x05},
	{0x700, 0x21}, {0x701, 0x43}, {0x702, 0x65}, {0x703, 0x87},
	{0x708, 0x21}, {0x709, 0x43}, {0x70a, 0x65}, {0x70b, 0x87},
	{0xffff, 0xff},
};

static const struct rtl8xxxu_reg32val rtl8188eu_phy_init_table[] = {
	{0x800, 0x80040000}, {0x804, 0x00000003},
	{0x808, 0x0000fc00}, {0x80c, 0x0000000a},
	{0x810, 0x10001331}, {0x814, 0x020c3d10},
	{0x818, 0x02200385}, {0x81c, 0x00000000},
	{0x820, 0x01000100}, {0x824, 0x00390204},
	{0x828, 0x00000000}, {0x82c, 0x00000000},
	{0x830, 0x00000000}, {0x834, 0x00000000},
	{0x838, 0x00000000}, {0x83c, 0x00000000},
	{0x840, 0x00010000}, {0x844, 0x00000000},
	{0x848, 0x00000000}, {0x84c, 0x00000000},
	{0x850, 0x00000000}, {0x854, 0x00000000},
	{0x858, 0x569a11a9}, {0x85c, 0x01000014},
	{0x860, 0x66f60110}, {0x864, 0x061f0649},
	{0x868, 0x00000000}, {0x86c, 0x27272700},
	{0x870, 0x07000760}, {0x874, 0x25004000},
	{0x878, 0x00000808}, {0x87c, 0x00000000},
	{0x880, 0xb0000c1c}, {0x884, 0x00000001},
	{0x888, 0x00000000}, {0x88c, 0xccc000c0},
	{0x890, 0x00000800}, {0x894, 0xfffffffe},
	{0x898, 0x40302010}, {0x89c, 0x00706050},
	{0x900, 0x00000000}, {0x904, 0x00000023},
	{0x908, 0x00000000}, {0x90c, 0x81121111},
	{0x910, 0x00000002}, {0x914, 0x00000201},
	{0xa00, 0x00d047c8}, {0xa04, 0x80ff800c},
	{0xa08, 0x8c838300}, {0xa0c, 0x2e7f120f},
	{0xa10, 0x9500bb7e}, {0xa14, 0x1114d028},
	{0xa18, 0x00881117}, {0xa1c, 0x89140f00},
	{0xa20, 0x1a1b0000}, {0xa24, 0x090e1317},
	{0xa28, 0x00000204}, {0xa2c, 0x00d30000},
	{0xa70, 0x101fbf00}, {0xa74, 0x00000007},
	{0xa78, 0x00000900}, {0xa7c, 0x225b0606},
	{0xa80, 0x218075b1}, {0xb2c, 0x80000000},
	{0xc00, 0x48071d40}, {0xc04, 0x03a05611},
	{0xc08, 0x000000e4}, {0xc0c, 0x6c6c6c6c},
	{0xc10, 0x08800000}, {0xc14, 0x40000100},
	{0xc18, 0x08800000}, {0xc1c, 0x40000100},
	{0xc20, 0x00000000}, {0xc24, 0x00000000},
	{0xc28, 0x00000000}, {0xc2c, 0x00000000},
	{0xc30, 0x69e9ac47}, {0xc34, 0x469652af},
	{0xc38, 0x49795994}, {0xc3c, 0x0a97971c},
	{0xc40, 0x1f7c403f}, {0xc44, 0x000100b7},
	{0xc48, 0xec020107}, {0xc4c, 0x007f037f},
	{0xc50, 0x69553420}, {0xc54, 0x43bc0094},
	{0xc58, 0x00013169}, {0xc5c, 0x00250492},
	{0xc60, 0x00000000}, {0xc64, 0x7112848b},
	{0xc68, 0x47c00bff}, {0xc6c, 0x00000036},
	{0xc70, 0x2c7f000d}, {0xc74, 0x020610db},
	{0xc78, 0x0000001f}, {0xc7c, 0x00b91612},
	{0xc80, 0x390000e4}, {0xc84, 0x21f60000},
	{0xc88, 0x40000100}, {0xc8c, 0x20200000},
	{0xc90, 0x00091521}, {0xc94, 0x00000000},
	{0xc98, 0x00121820}, {0xc9c, 0x00007f7f},
	{0xca0, 0x00000000}, {0xca4, 0x000300a0},
	{0xca8, 0x00000000}, {0xcac, 0x00000000},
	{0xcb0, 0x00000000}, {0xcb4, 0x00000000},
	{0xcb8, 0x00000000}, {0xcbc, 0x28000000},
	{0xcc0, 0x00000000}, {0xcc4, 0x00000000},
	{0xcc8, 0x00000000}, {0xccc, 0x00000000},
	{0xcd0, 0x00000000}, {0xcd4, 0x00000000},
	{0xcd8, 0x64b22427}, {0xcdc, 0x00766932},
	{0xce0, 0x00222222}, {0xce4, 0x00000000},
	{0xce8, 0x37644302}, {0xcec, 0x2f97d40c},
	{0xd00, 0x00000740}, {0xd04, 0x00020401},
	{0xd08, 0x0000907f}, {0xd0c, 0x20010201},
	{0xd10, 0xa0633333}, {0xd14, 0x3333bc43},
	{0xd18, 0x7a8f5b6f}, {0xd2c, 0xcc979975},
	{0xd30, 0x00000000}, {0xd34, 0x80608000},
	{0xd38, 0x00000000}, {0xd3c, 0x00127353},
	{0xd40, 0x00000000}, {0xd44, 0x00000000},
	{0xd48, 0x00000000}, {0xd4c, 0x00000000},
	{0xd50, 0x6437140a}, {0xd54, 0x00000000},
	{0xd58, 0x00000282}, {0xd5c, 0x30032064},
	{0xd60, 0x4653de68}, {0xd64, 0x04518a3c},
	{0xd68, 0x00002101}, {0xd6c, 0x2a201c16},
	{0xd70, 0x1812362e}, {0xd74, 0x322c2220},
	{0xd78, 0x000e3c24}, {0xe00, 0x2d2d2d2d},
	{0xe04, 0x2d2d2d2d}, {0xe08, 0x0390272d},
	{0xe10, 0x2d2d2d2d}, {0xe14, 0x2d2d2d2d},
	{0xe18, 0x2d2d2d2d}, {0xe1c, 0x2d2d2d2d},
	{0xe28, 0x00000000}, {0xe30, 0x1000dc1f},
	{0xe34, 0x10008c1f}, {0xe38, 0x02140102},
	{0xe3c, 0x681604c2}, {0xe40, 0x01007c00},
	{0xe44, 0x01004800}, {0xe48, 0xfb000000},
	{0xe4c, 0x000028d1}, {0xe50, 0x1000dc1f},
	{0xe54, 0x10008c1f}, {0xe58, 0x02140102},
	{0xe5c, 0x28160d05}, {0xe60, 0x00000048},
	{0xe68, 0x001b25a4}, {0xe6c, 0x00c00014},
	{0xe70, 0x00c00014}, {0xe74, 0x01000014},
	{0xe78, 0x01000014}, {0xe7c, 0x01000014},
	{0xe80, 0x01000014}, {0xe84, 0x00c00014},
	{0xe88, 0x01000014}, {0xe8c, 0x00c00014},
	{0xed0, 0x00c00014}, {0xed4, 0x00c00014},
	{0xed8, 0x00c00014}, {0xedc, 0x00000014},
	{0xee0, 0x00000014}, {0xee8, 0x21555448},
	{0xeec, 0x01c00014}, {0xf14, 0x00000003},
	{0xf4c, 0x00000000}, {0xf00, 0x00000300},
	{0xffff, 0xffffffff},
};

static const struct rtl8xxxu_reg32val rtl8188e_agc_table[] = {
	{0xc78, 0xfb000001}, {0xc78, 0xfb010001},
	{0xc78, 0xfb020001}, {0xc78, 0xfb030001},
	{0xc78, 0xfb040001}, {0xc78, 0xfb050001},
	{0xc78, 0xfa060001}, {0xc78, 0xf9070001},
	{0xc78, 0xf8080001}, {0xc78, 0xf7090001},
	{0xc78, 0xf60a0001}, {0xc78, 0xf50b0001},
	{0xc78, 0xf40c0001}, {0xc78, 0xf30d0001},
	{0xc78, 0xf20e0001}, {0xc78, 0xf10f0001},
	{0xc78, 0xf0100001}, {0xc78, 0xef110001},
	{0xc78, 0xee120001}, {0xc78, 0xed130001},
	{0xc78, 0xec140001}, {0xc78, 0xeb150001},
	{0xc78, 0xea160001}, {0xc78, 0xe9170001},
	{0xc78, 0xe8180001}, {0xc78, 0xe7190001},
	{0xc78, 0xe61a0001}, {0xc78, 0xe51b0001},
	{0xc78, 0xe41c0001}, {0xc78, 0xe31d0001},
	{0xc78, 0xe21e0001}, {0xc78, 0xe11f0001},
	{0xc78, 0x8a200001}, {0xc78, 0x89210001},
	{0xc78, 0x88220001}, {0xc78, 0x87230001},
	{0xc78, 0x86240001}, {0xc78, 0x85250001},
	{0xc78, 0x84260001}, {0xc78, 0x83270001},
	{0xc78, 0x82280001}, {0xc78, 0x6b290001},
	{0xc78, 0x6a2a0001}, {0xc78, 0x692b0001},
	{0xc78, 0x682c0001}, {0xc78, 0x672d0001},
	{0xc78, 0x662e0001}, {0xc78, 0x652f0001},
	{0xc78, 0x64300001}, {0xc78, 0x63310001},
	{0xc78, 0x62320001}, {0xc78, 0x61330001},
	{0xc78, 0x46340001}, {0xc78, 0x45350001},
	{0xc78, 0x44360001}, {0xc78, 0x43370001},
	{0xc78, 0x42380001}, {0xc78, 0x41390001},
	{0xc78, 0x403a0001}, {0xc78, 0x403b0001},
	{0xc78, 0x403c0001}, {0xc78, 0x403d0001},
	{0xc78, 0x403e0001}, {0xc78, 0x403f0001},
	{0xc78, 0xfb400001}, {0xc78, 0xfb410001},
	{0xc78, 0xfb420001}, {0xc78, 0xfb430001},
	{0xc78, 0xfb440001}, {0xc78, 0xfb450001},
	{0xc78, 0xfb460001}, {0xc78, 0xfb470001},
	{0xc78, 0xfb480001}, {0xc78, 0xfa490001},
	{0xc78, 0xf94a0001}, {0xc78, 0xf84b0001},
	{0xc78, 0xf74c0001}, {0xc78, 0xf64d0001},
	{0xc78, 0xf54e0001}, {0xc78, 0xf44f0001},
	{0xc78, 0xf3500001}, {0xc78, 0xf2510001},
	{0xc78, 0xf1520001}, {0xc78, 0xf0530001},
	{0xc78, 0xef540001}, {0xc78, 0xee550001},
	{0xc78, 0xed560001}, {0xc78, 0xec570001},
	{0xc78, 0xeb580001}, {0xc78, 0xea590001},
	{0xc78, 0xe95a0001}, {0xc78, 0xe85b0001},
	{0xc78, 0xe75c0001}, {0xc78, 0xe65d0001},
	{0xc78, 0xe55e0001}, {0xc78, 0xe45f0001},
	{0xc78, 0xe3600001}, {0xc78, 0xe2610001},
	{0xc78, 0xc3620001}, {0xc78, 0xc2630001},
	{0xc78, 0xc1640001}, {0xc78, 0x8b650001},
	{0xc78, 0x8a660001}, {0xc78, 0x89670001},
	{0xc78, 0x88680001}, {0xc78, 0x87690001},
	{0xc78, 0x866a0001}, {0xc78, 0x856b0001},
	{0xc78, 0x846c0001}, {0xc78, 0x676d0001},
	{0xc78, 0x666e0001}, {0xc78, 0x656f0001},
	{0xc78, 0x64700001}, {0xc78, 0x63710001},
	{0xc78, 0x62720001}, {0xc78, 0x61730001},
	{0xc78, 0x60740001}, {0xc78, 0x46750001},
	{0xc78, 0x45760001}, {0xc78, 0x44770001},
	{0xc78, 0x43780001}, {0xc78, 0x42790001},
	{0xc78, 0x417a0001}, {0xc78, 0x407b0001},
	{0xc78, 0x407c0001}, {0xc78, 0x407d0001},
	{0xc78, 0x407e0001}, {0xc78, 0x407f0001},
	{0xc50, 0x69553422}, {0xc50, 0x69553420},
	{0xffff, 0xffffffff}
};

static const struct rtl8xxxu_rfregval rtl8188eu_radioa_init_table[] = {
	{0x00, 0x00030000}, {0x08, 0x00084000},
	{0x18, 0x00000407}, {0x19, 0x00000012},
	{0x1e, 0x00080009}, {0x1f, 0x00000880},
	{0x2f, 0x0001a060}, {0x3f, 0x00000000},
	{0x42, 0x000060c0}, {0x57, 0x000d0000},
	{0x58, 0x000be180}, {0x67, 0x00001552},
	{0x83, 0x00000000}, {0xb0, 0x000ff8fc},
	{0xb1, 0x00054400}, {0xb2, 0x000ccc19},
	{0xb4, 0x00043003}, {0xb6, 0x0004953e},
	{0xb7, 0x0001c718}, {0xb8, 0x000060ff},
	{0xb9, 0x00080001}, {0xba, 0x00040000},
	{0xbb, 0x00000400}, {0xbf, 0x000c0000},
	{0xc2, 0x00002400}, {0xc3, 0x00000009},
	{0xc4, 0x00040c91}, {0xc5, 0x00099999},
	{0xc6, 0x000000a3}, {0xc7, 0x00088820},
	{0xc8, 0x00076c06}, {0xc9, 0x00000000},
	{0xca, 0x00080000}, {0xdf, 0x00000180},
	{0xef, 0x000001a0}, {0x51, 0x0006b27d},
	{0x52, 0x0007e49d},	/* Set to 0x0007e4dd for SDIO */
	{0x53, 0x00000073}, {0x56, 0x00051ff3},
	{0x35, 0x00000086}, {0x35, 0x00000186},
	{0x35, 0x00000286}, {0x36, 0x00001c25},
	{0x36, 0x00009c25}, {0x36, 0x00011c25},
	{0x36, 0x00019c25}, {0xb6, 0x00048538},
	{0x18, 0x00000c07}, {0x5a, 0x0004bd00},
	{0x19, 0x000739d0}, {0x34, 0x0000adf3},
	{0x34, 0x00009df0}, {0x34, 0x00008ded},
	{0x34, 0x00007dea}, {0x34, 0x00006de7},
	{0x34, 0x000054ee}, {0x34, 0x000044eb},
	{0x34, 0x000034e8}, {0x34, 0x0000246b},
	{0x34, 0x00001468}, {0x34, 0x0000006d},
	{0x00, 0x00030159}, {0x84, 0x00068200},
	{0x86, 0x000000ce}, {0x87, 0x00048a00},
	{0x8e, 0x00065540}, {0x8f, 0x00088000},
	{0xef, 0x000020a0}, {0x3b, 0x000f02b0},
	{0x3b, 0x000ef7b0}, {0x3b, 0x000d4fb0},
	{0x3b, 0x000cf060}, {0x3b, 0x000b0090},
	{0x3b, 0x000a0080}, {0x3b, 0x00090080},
	{0x3b, 0x0008f780}, {0x3b, 0x000722b0},
	{0x3b, 0x0006f7b0}, {0x3b, 0x00054fb0},
	{0x3b, 0x0004f060}, {0x3b, 0x00030090},
	{0x3b, 0x00020080}, {0x3b, 0x00010080},
	{0x3b, 0x0000f780}, {0xef, 0x000000a0},
	{0x00, 0x00010159}, {0x18, 0x0000f407},
	{0xFE, 0x00000000}, {0xFE, 0x00000000},
	{0x1F, 0x00080003}, {0xFE, 0x00000000},
	{0xFE, 0x00000000}, {0x1E, 0x00000001},
	{0x1F, 0x00080000}, {0x00, 0x00033e60},
	{0xff, 0xffffffff}
};

#define PERENTRY		23
#define RETRYSIZE		5
#define RATESIZE		28
#define TX_RPT2_ITEM_SIZE	8

static const u8 retry_penalty[PERENTRY][RETRYSIZE + 1] = {
	{5, 4, 3, 2, 0, 3}, /* 92 , idx=0 */
	{6, 5, 4, 3, 0, 4}, /* 86 , idx=1 */
	{6, 5, 4, 2, 0, 4}, /* 81 , idx=2 */
	{8, 7, 6, 4, 0, 6}, /* 75 , idx=3 */
	{10, 9, 8, 6, 0, 8}, /* 71 , idx=4 */
	{10, 9, 8, 4, 0, 8}, /* 66 , idx=5 */
	{10, 9, 8, 2, 0, 8}, /* 62 , idx=6 */
	{10, 9, 8, 0, 0, 8}, /* 59 , idx=7 */
	{18, 17, 16, 8, 0, 16}, /* 53 , idx=8 */
	{26, 25, 24, 16, 0, 24}, /* 50 , idx=9 */
	{34, 33, 32, 24, 0, 32}, /* 47 , idx=0x0a */
	{34, 31, 28, 20, 0, 32}, /* 43 , idx=0x0b */
	{34, 31, 27, 18, 0, 32}, /* 40 , idx=0x0c */
	{34, 31, 26, 16, 0, 32}, /* 37 , idx=0x0d */
	{34, 30, 22, 16, 0, 32}, /* 32 , idx=0x0e */
	{34, 30, 24, 16, 0, 32}, /* 26 , idx=0x0f */
	{49, 46, 40, 16, 0, 48}, /* 20 , idx=0x10 */
	{49, 45, 32, 0, 0, 48}, /* 17 , idx=0x11 */
	{49, 45, 22, 18, 0, 48}, /* 15 , idx=0x12 */
	{49, 40, 24, 16, 0, 48}, /* 12 , idx=0x13 */
	{49, 32, 18, 12, 0, 48}, /* 9 , idx=0x14 */
	{49, 22, 18, 14, 0, 48}, /* 6 , idx=0x15 */
	{49, 16, 16, 0, 0, 48} /* 3, idx=0x16 */
};

static const u8 pt_penalty[RETRYSIZE + 1] = {34, 31, 30, 24, 0, 32};

static const u8 retry_penalty_idx_normal[2][RATESIZE] = {
	{ /* RSSI>TH */
		4, 4, 4, 5,
		4, 4, 5, 7, 7, 7, 8, 0x0a,
		4, 4, 4, 4, 6, 0x0a, 0x0b, 0x0d,
		5, 5, 7, 7, 8, 0x0b, 0x0d, 0x0f
	},
	{ /* RSSI<TH */
		0x0a, 0x0a, 0x0b, 0x0c,
		0x0a, 0x0a, 0x0b, 0x0c, 0x0d, 0x10, 0x13, 0x13,
		0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x11, 0x13, 0x13,
		9, 9, 9, 9, 0x0c, 0x0e, 0x11, 0x13
	}
};

static const u8 retry_penalty_idx_cut_i[2][RATESIZE] = {
	{ /* RSSI>TH */
		4, 4, 4, 5,
		4, 4, 5, 7, 7, 7, 8, 0x0a,
		4, 4, 4, 4, 6, 0x0a, 0x0b, 0x0d,
		5, 5, 7, 7, 8, 0x0b, 0x0d, 0x0f
	},
	{ /* RSSI<TH */
		0x0a, 0x0a, 0x0b, 0x0c,
		0x0a, 0x0a, 0x0b, 0x0c, 0x0d, 0x10, 0x13, 0x13,
		0x06, 0x07, 0x08, 0x0d, 0x0e, 0x11, 0x11, 0x11,
		9, 9, 9, 9, 0x0c, 0x0e, 0x11, 0x13
	}
};

static const u8 retry_penalty_up_idx_normal[RATESIZE] = {
	0x0c, 0x0d, 0x0d, 0x0f,
	0x0d, 0x0e, 0x0f, 0x0f, 0x10, 0x12, 0x13, 0x14,
	0x0f, 0x10, 0x10, 0x12, 0x12, 0x13, 0x14, 0x15,
	0x11, 0x11, 0x12, 0x13, 0x13, 0x13, 0x14, 0x15
};

static const u8 retry_penalty_up_idx_cut_i[RATESIZE] = {
	0x0c, 0x0d, 0x0d, 0x0f,
	0x0d, 0x0e, 0x0f, 0x0f, 0x10, 0x12, 0x13, 0x14,
	0x0b, 0x0b, 0x11, 0x11, 0x12, 0x12, 0x12, 0x12,
	0x11, 0x11, 0x12, 0x13, 0x13, 0x13, 0x14, 0x15
};

static const u8 rssi_threshold[RATESIZE] = {
	0, 0, 0, 0,
	0, 0, 0, 0, 0, 0x24, 0x26, 0x2a,
	0x18, 0x1a, 0x1d, 0x1f, 0x21, 0x27, 0x29, 0x2a,
	0, 0, 0, 0x1f, 0x23, 0x28, 0x2a, 0x2c
};

static const u16 n_threshold_high[RATESIZE] = {
	4, 4, 8, 16,
	24, 36, 48, 72, 96, 144, 192, 216,
	60, 80, 100, 160, 240, 400, 600, 800,
	300, 320, 480, 720, 1000, 1200, 1600, 2000
};

static const u16 n_threshold_low[RATESIZE] = {
	2, 2, 4, 8,
	12, 18, 24, 36, 48, 72, 96, 108,
	30, 40, 50, 80, 120, 200, 300, 400,
	150, 160, 240, 360, 500, 600, 800, 1000
};

static const u8 dropping_necessary[RATESIZE] = {
	1, 1, 1, 1,
	1, 2, 3, 4, 5, 6, 7, 8,
	1, 2, 3, 4, 5, 6, 7, 8,
	5, 6, 7, 8, 9, 10, 11, 12
};

static const u8 pending_for_rate_up_fail[5] = {2, 10, 24, 40, 60};

static const u16 dynamic_tx_rpt_timing[6] = {
	0x186a, 0x30d4, 0x493e, 0x61a8, 0x7a12, 0x927c /* 200ms-1200ms */
};

enum rtl8188e_tx_rpt_timing {
	DEFAULT_TIMING = 0,
	INCREASE_TIMING,
	DECREASE_TIMING
};

static int rtl8188eu_identify_chip(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	u32 sys_cfg, vendor;
	int ret = 0;

	strscpy(priv->chip_name, "8188EU", sizeof(priv->chip_name));
	priv->rtl_chip = RTL8188E;
	priv->rf_paths = 1;
	priv->rx_paths = 1;
	priv->tx_paths = 1;
	priv->has_wifi = 1;

	sys_cfg = rtl8xxxu_read32(priv, REG_SYS_CFG);
	priv->chip_cut = u32_get_bits(sys_cfg, SYS_CFG_CHIP_VERSION_MASK);
	if (sys_cfg & SYS_CFG_TRP_VAUX_EN) {
		dev_info(dev, "Unsupported test chip\n");
		return -EOPNOTSUPP;
	}

	/*
	 * TODO: At a glance, I cut requires a different firmware,
	 * different initialisation tables, and no software rate
	 * control. The vendor driver is not configured to handle
	 * I cut chips by default. Are there any in the wild?
	 */
	if (priv->chip_cut == 8) {
		dev_info(dev, "RTL8188EU cut I is not supported. Please complain about it at linux-wireless@vger.kernel.org.\n");
		return -EOPNOTSUPP;
	}

	vendor = sys_cfg & SYS_CFG_VENDOR_ID;
	rtl8xxxu_identify_vendor_1bit(priv, vendor);

	ret = rtl8xxxu_config_endpoints_no_sie(priv);

	return ret;
}

static void rtl8188eu_config_channel(struct ieee80211_hw *hw)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	u32 val32, rsr;
	u8 opmode;
	int sec_ch_above, channel;
	int i;

	opmode = rtl8xxxu_read8(priv, REG_BW_OPMODE);
	rsr = rtl8xxxu_read32(priv, REG_RESPONSE_RATE_SET);
	channel = hw->conf.chandef.chan->hw_value;

	switch (hw->conf.chandef.width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		opmode |= BW_OPMODE_20MHZ;
		rtl8xxxu_write8(priv, REG_BW_OPMODE, opmode);

		val32 = rtl8xxxu_read32(priv, REG_FPGA0_RF_MODE);
		val32 &= ~FPGA_RF_MODE;
		rtl8xxxu_write32(priv, REG_FPGA0_RF_MODE, val32);

		val32 = rtl8xxxu_read32(priv, REG_FPGA1_RF_MODE);
		val32 &= ~FPGA_RF_MODE;
		rtl8xxxu_write32(priv, REG_FPGA1_RF_MODE, val32);
		break;
	case NL80211_CHAN_WIDTH_40:
		if (hw->conf.chandef.center_freq1 >
		    hw->conf.chandef.chan->center_freq) {
			sec_ch_above = 1;
			channel += 2;
		} else {
			sec_ch_above = 0;
			channel -= 2;
		}

		opmode &= ~BW_OPMODE_20MHZ;
		rtl8xxxu_write8(priv, REG_BW_OPMODE, opmode);
		rsr &= ~RSR_RSC_BANDWIDTH_40M;
		if (sec_ch_above)
			rsr |= RSR_RSC_LOWER_SUB_CHANNEL;
		else
			rsr |= RSR_RSC_UPPER_SUB_CHANNEL;
		rtl8xxxu_write32(priv, REG_RESPONSE_RATE_SET, rsr);

		val32 = rtl8xxxu_read32(priv, REG_FPGA0_RF_MODE);
		val32 |= FPGA_RF_MODE;
		rtl8xxxu_write32(priv, REG_FPGA0_RF_MODE, val32);

		val32 = rtl8xxxu_read32(priv, REG_FPGA1_RF_MODE);
		val32 |= FPGA_RF_MODE;
		rtl8xxxu_write32(priv, REG_FPGA1_RF_MODE, val32);

		/*
		 * Set Control channel to upper or lower. These settings
		 * are required only for 40MHz
		 */
		val32 = rtl8xxxu_read32(priv, REG_CCK0_SYSTEM);
		val32 &= ~CCK0_SIDEBAND;
		if (!sec_ch_above)
			val32 |= CCK0_SIDEBAND;
		rtl8xxxu_write32(priv, REG_CCK0_SYSTEM, val32);

		val32 = rtl8xxxu_read32(priv, REG_OFDM1_LSTF);
		val32 &= ~OFDM_LSTF_PRIME_CH_MASK; /* 0xc00 */
		if (sec_ch_above)
			val32 |= OFDM_LSTF_PRIME_CH_LOW;
		else
			val32 |= OFDM_LSTF_PRIME_CH_HIGH;
		rtl8xxxu_write32(priv, REG_OFDM1_LSTF, val32);

		val32 = rtl8xxxu_read32(priv, REG_FPGA0_POWER_SAVE);
		val32 &= ~(FPGA0_PS_LOWER_CHANNEL | FPGA0_PS_UPPER_CHANNEL);
		if (sec_ch_above)
			val32 |= FPGA0_PS_UPPER_CHANNEL;
		else
			val32 |= FPGA0_PS_LOWER_CHANNEL;
		rtl8xxxu_write32(priv, REG_FPGA0_POWER_SAVE, val32);
		break;

	default:
		break;
	}

	for (i = RF_A; i < priv->rf_paths; i++) {
		val32 = rtl8xxxu_read_rfreg(priv, i, RF6052_REG_MODE_AG);
		u32p_replace_bits(&val32, channel, MODE_AG_CHANNEL_MASK);
		rtl8xxxu_write_rfreg(priv, i, RF6052_REG_MODE_AG, val32);
	}

	for (i = RF_A; i < priv->rf_paths; i++) {
		val32 = rtl8xxxu_read_rfreg(priv, i, RF6052_REG_MODE_AG);
		val32 &= ~MODE_AG_BW_MASK;
		if (hw->conf.chandef.width == NL80211_CHAN_WIDTH_40)
			val32 |= MODE_AG_BW_40MHZ_8723B;
		else
			val32 |= MODE_AG_BW_20MHZ_8723B;
		rtl8xxxu_write_rfreg(priv, i, RF6052_REG_MODE_AG, val32);
	}
}

static void rtl8188eu_init_aggregation(struct rtl8xxxu_priv *priv)
{
	u8 agg_ctrl, usb_spec;

	usb_spec = rtl8xxxu_read8(priv, REG_USB_SPECIAL_OPTION);
	usb_spec &= ~USB_SPEC_USB_AGG_ENABLE;
	rtl8xxxu_write8(priv, REG_USB_SPECIAL_OPTION, usb_spec);

	agg_ctrl = rtl8xxxu_read8(priv, REG_TRXDMA_CTRL);
	agg_ctrl &= ~TRXDMA_CTRL_RXDMA_AGG_EN;
	rtl8xxxu_write8(priv, REG_TRXDMA_CTRL, agg_ctrl);
}

static int rtl8188eu_parse_efuse(struct rtl8xxxu_priv *priv)
{
	struct rtl8188eu_efuse *efuse = &priv->efuse_wifi.efuse8188eu;

	if (efuse->rtl_id != cpu_to_le16(0x8129))
		return -EINVAL;

	ether_addr_copy(priv->mac_addr, efuse->mac_addr);

	memcpy(priv->cck_tx_power_index_A, efuse->tx_power_index_A.cck_base,
	       sizeof(efuse->tx_power_index_A.cck_base));

	memcpy(priv->ht40_1s_tx_power_index_A,
	       efuse->tx_power_index_A.ht40_base,
	       sizeof(efuse->tx_power_index_A.ht40_base));

	priv->default_crystal_cap = efuse->xtal_k & 0x3f;

	return 0;
}

static void rtl8188eu_reset_8051(struct rtl8xxxu_priv *priv)
{
	u16 sys_func;

	sys_func = rtl8xxxu_read16(priv, REG_SYS_FUNC);
	sys_func &= ~SYS_FUNC_CPU_ENABLE;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, sys_func);

	sys_func |= SYS_FUNC_CPU_ENABLE;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, sys_func);
}

static int rtl8188eu_load_firmware(struct rtl8xxxu_priv *priv)
{
	const char *fw_name;
	int ret;

	fw_name = "rtlwifi/rtl8188eufw.bin";

	ret = rtl8xxxu_load_firmware(priv, fw_name);

	return ret;
}

static void rtl8188eu_init_phy_bb(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u16 val16;

	val16 = rtl8xxxu_read16(priv, REG_SYS_FUNC);
	val16 |= SYS_FUNC_BB_GLB_RSTN | SYS_FUNC_BBRSTB | SYS_FUNC_DIO_RF;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, val16);

	/*
	 * Per vendor driver, run power sequence before init of RF
	 */
	val8 = RF_ENABLE | RF_RSTB | RF_SDMRSTB;
	rtl8xxxu_write8(priv, REG_RF_CTRL, val8);

	val8 = SYS_FUNC_USBA | SYS_FUNC_USBD |
	       SYS_FUNC_BB_GLB_RSTN | SYS_FUNC_BBRSTB;
	rtl8xxxu_write8(priv, REG_SYS_FUNC, val8);

	rtl8xxxu_init_phy_regs(priv, rtl8188eu_phy_init_table);
	rtl8xxxu_init_phy_regs(priv, rtl8188e_agc_table);
}

static int rtl8188eu_init_phy_rf(struct rtl8xxxu_priv *priv)
{
	return rtl8xxxu_init_phy_rf(priv, rtl8188eu_radioa_init_table, RF_A);
}

static int rtl8188eu_iqk_path_a(struct rtl8xxxu_priv *priv)
{
	u32 reg_eac, reg_e94, reg_e9c;
	int result = 0;

	/* Path A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x10008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x30008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_A, 0x8214032a);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, 0x28160000);

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x00462911);

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
	    ((reg_e9c & 0x03ff0000) != 0x00420000))
		result |= 0x01;

	return result;
}

static int rtl8188eu_rx_iqk_path_a(struct rtl8xxxu_priv *priv)
{
	u32 reg_ea4, reg_eac, reg_e94, reg_e9c, val32;
	int result = 0;

	/* Leave IQK mode */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	u32p_replace_bits(&val32, 0, 0xffffff00);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/* Enable path A PA in TX IQK mode */
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_WE_LUT, 0x800a0);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_RCK_OS, 0x30000);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G1, 0x0000f);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G2, 0xf117b);

	/* Enter IQK mode */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	u32p_replace_bits(&val32, 0x808000, 0xffffff00);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/* TX IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK, 0x01007c00);
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x81004800);

	/* path-A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x10008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x30008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_A, 0x82160804);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, 0x28160000);

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
	    ((reg_e9c & 0x03ff0000) != 0x00420000))
		result |= 0x01;
	else
		goto out;

	val32 = 0x80007c00 |
		(reg_e94 & 0x03ff0000) | ((reg_e9c >> 16) & 0x03ff);
	rtl8xxxu_write32(priv, REG_TX_IQK, val32);

	/* Modify RX IQK mode table */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	u32p_replace_bits(&val32, 0, 0xffffff00);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_WE_LUT, 0x800a0);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_RCK_OS, 0x30000);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G1, 0x0000f);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G2, 0xf7ffa);

	/* Enter IQK mode */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	u32p_replace_bits(&val32, 0x808000, 0xffffff00);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/* IQK setting */
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

	/* Path A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x30008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x10008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_A, 0x82160c05);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, 0x28160c05);

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x0046a911);

	/* One shot, path A LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf9000000);
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf8000000);

	mdelay(10);

	reg_eac = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_A_2);
	reg_ea4 = rtl8xxxu_read32(priv, REG_RX_POWER_BEFORE_IQK_A_2);

	if (!(reg_eac & BIT(27)) &&
	    ((reg_ea4 & 0x03ff0000) != 0x01320000) &&
	    ((reg_eac & 0x03ff0000) != 0x00360000))
		result |= 0x02;
	else
		dev_warn(&priv->udev->dev, "%s: Path A RX IQK failed!\n",
			 __func__);

out:
	return result;
}

static void rtl8188eu_phy_iqcalibrate(struct rtl8xxxu_priv *priv,
				      int result[][8], int t)
{
	struct device *dev = &priv->udev->dev;
	u32 i, val32;
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
		/* Switch BB to PI mode to do IQ Calibration. */
		rtl8xxxu_write32(priv, REG_FPGA0_XA_HSSI_PARM1, 0x01000100);
		rtl8xxxu_write32(priv, REG_FPGA0_XB_HSSI_PARM1, 0x01000100);
	}

	/* MAC settings */
	rtl8xxxu_mac_calibration(priv, iqk_mac_regs, priv->mac_backup);

	val32 = rtl8xxxu_read32(priv, REG_CCK0_AFE_SETTING);
	u32p_replace_bits(&val32, 0xf, 0x0f000000);
	rtl8xxxu_write32(priv, REG_CCK0_AFE_SETTING, val32);

	rtl8xxxu_write32(priv, REG_OFDM0_TRX_PATH_ENABLE, 0x03a05600);
	rtl8xxxu_write32(priv, REG_OFDM0_TR_MUX_PAR, 0x000800e4);
	rtl8xxxu_write32(priv, REG_FPGA0_XCD_RF_SW_CTRL, 0x22204000);

	if (!priv->no_pape) {
		val32 = rtl8xxxu_read32(priv, REG_FPGA0_XAB_RF_SW_CTRL);
		val32 |= (FPGA0_RF_PAPE |
			  (FPGA0_RF_PAPE << FPGA0_RF_BD_CTRL_SHIFT));
		rtl8xxxu_write32(priv, REG_FPGA0_XAB_RF_SW_CTRL, val32);
	}

	val32 = rtl8xxxu_read32(priv, REG_FPGA0_XA_RF_INT_OE);
	val32 &= ~BIT(10);
	rtl8xxxu_write32(priv, REG_FPGA0_XA_RF_INT_OE, val32);
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_XB_RF_INT_OE);
	val32 &= ~BIT(10);
	rtl8xxxu_write32(priv, REG_FPGA0_XB_RF_INT_OE, val32);

	/* Page B init */
	rtl8xxxu_write32(priv, REG_CONFIG_ANT_A, 0x0f600000);

	/* IQ calibration setting */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	u32p_replace_bits(&val32, 0x808000, 0xffffff00);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);
	rtl8xxxu_write32(priv, REG_TX_IQK, 0x01007c00);
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x81004800);

	for (i = 0; i < retry; i++) {
		path_a_ok = rtl8188eu_iqk_path_a(priv);
		if (path_a_ok == 0x01) {
			val32 = rtl8xxxu_read32(priv,
						REG_TX_POWER_BEFORE_IQK_A);
			result[t][0] = (val32 >> 16) & 0x3ff;
			val32 = rtl8xxxu_read32(priv,
						REG_TX_POWER_AFTER_IQK_A);
			result[t][1] = (val32 >> 16) & 0x3ff;
			break;
		}
	}

	if (!path_a_ok)
		dev_dbg(dev, "%s: Path A TX IQK failed!\n", __func__);

	for (i = 0; i < retry; i++) {
		path_a_ok = rtl8188eu_rx_iqk_path_a(priv);
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
		dev_dbg(dev, "%s: Path A RX IQK failed!\n", __func__);

	/* Back to BB mode, load original value */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	u32p_replace_bits(&val32, 0, 0xffffff00);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	if (t == 0)
		return;

	if (!priv->pi_enabled) {
		/* Switch back BB to SI mode after finishing IQ Calibration */
		rtl8xxxu_write32(priv, REG_FPGA0_XA_HSSI_PARM1, 0x01000000);
		rtl8xxxu_write32(priv, REG_FPGA0_XB_HSSI_PARM1, 0x01000000);
	}

	/* Reload ADDA power saving parameters */
	rtl8xxxu_restore_regs(priv, adda_regs, priv->adda_backup,
			      RTL8XXXU_ADDA_REGS);

	/* Reload MAC parameters */
	rtl8xxxu_restore_mac_regs(priv, iqk_mac_regs, priv->mac_backup);

	/* Reload BB parameters */
	rtl8xxxu_restore_regs(priv, iqk_bb_regs,
			      priv->bb_backup, RTL8XXXU_BB_REGS);

	/* Restore RX initial gain */
	rtl8xxxu_write32(priv, REG_FPGA0_XA_LSSI_PARM, 0x00032ed3);

	/* Load 0xe30 IQC default value */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x01008c00);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x01008c00);
}

static void rtl8188eu_phy_iq_calibrate(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	int result[4][8];	/* last is final result */
	int i, candidate;
	bool path_a_ok;
	u32 reg_e94, reg_e9c, reg_ea4, reg_eac;
	u32 reg_eb4, reg_ebc, reg_ec4, reg_ecc;
	bool simu;

	memset(result, 0, sizeof(result));
	result[3][0] = 0x100;
	result[3][2] = 0x100;
	result[3][4] = 0x100;
	result[3][6] = 0x100;

	candidate = -1;

	path_a_ok = false;

	for (i = 0; i < 3; i++) {
		rtl8188eu_phy_iqcalibrate(priv, result, i);

		if (i == 1) {
			simu = rtl8xxxu_simularity_compare(priv,
							   result, 0, 1);
			if (simu) {
				candidate = 0;
				break;
			}
		}

		if (i == 2) {
			simu = rtl8xxxu_simularity_compare(priv,
							   result, 0, 2);
			if (simu) {
				candidate = 0;
				break;
			}

			simu = rtl8xxxu_simularity_compare(priv,
							   result, 1, 2);
			if (simu)
				candidate = 1;
			else
				candidate = 3;
		}
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
			"%s: e94=%x e9c=%x ea4=%x eac=%x eb4=%x ebc=%x ec4=%x ecc=%x\n",
			__func__, reg_e94, reg_e9c, reg_ea4, reg_eac,
			reg_eb4, reg_ebc, reg_ec4, reg_ecc);
		path_a_ok = true;
	} else {
		reg_e94 = 0x100;
		reg_eb4 = 0x100;
		priv->rege94 = 0x100;
		priv->regeb4 = 0x100;
		reg_e9c = 0x0;
		reg_ebc = 0x0;
		priv->rege9c = 0x0;
		priv->regebc = 0x0;
	}

	if (reg_e94 && candidate >= 0)
		rtl8xxxu_fill_iqk_matrix_a(priv, path_a_ok, result,
					   candidate, (reg_ea4 == 0));

	rtl8xxxu_save_regs(priv, rtl8xxxu_iqk_phy_iq_bb_reg,
			   priv->bb_recovery_backup, RTL8XXXU_BB_REGS);
}

static void rtl8188e_disabled_to_emu(struct rtl8xxxu_priv *priv)
{
	u16 val16;

	val16 = rtl8xxxu_read16(priv, REG_APS_FSMCO);
	val16 &= ~(APS_FSMCO_HW_SUSPEND | APS_FSMCO_PCIE);
	rtl8xxxu_write16(priv, REG_APS_FSMCO, val16);
}

static int rtl8188e_emu_to_active(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u32 val32;
	u16 val16;
	int count, ret = 0;

	/* wait till 0x04[17] = 1 power ready*/
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

	/* reset baseband */
	val8 = rtl8xxxu_read8(priv, REG_SYS_FUNC);
	val8 &= ~(SYS_FUNC_BBRSTB | SYS_FUNC_BB_GLB_RSTN);
	rtl8xxxu_write8(priv, REG_SYS_FUNC, val8);

	/*0x24[23] = 2b'01 schmit trigger */
	val32 = rtl8xxxu_read32(priv, REG_AFE_XTAL_CTRL);
	val32 |= BIT(23);
	rtl8xxxu_write32(priv, REG_AFE_XTAL_CTRL, val32);

	/* 0x04[15] = 0 disable HWPDN (control by DRV)*/
	val16 = rtl8xxxu_read16(priv, REG_APS_FSMCO);
	val16 &= ~APS_FSMCO_HW_POWERDOWN;
	rtl8xxxu_write16(priv, REG_APS_FSMCO, val16);

	/*0x04[12:11] = 2b'00 disable WL suspend*/
	val16 = rtl8xxxu_read16(priv, REG_APS_FSMCO);
	val16 &= ~(APS_FSMCO_HW_SUSPEND | APS_FSMCO_PCIE);
	rtl8xxxu_write16(priv, REG_APS_FSMCO, val16);

	/* set, then poll until 0 */
	val32 = rtl8xxxu_read32(priv, REG_APS_FSMCO);
	val32 |= APS_FSMCO_MAC_ENABLE;
	rtl8xxxu_write32(priv, REG_APS_FSMCO, val32);

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

	/* LDO normal mode*/
	val8 = rtl8xxxu_read8(priv, REG_LPLDO_CTRL);
	val8 &= ~BIT(4);
	rtl8xxxu_write8(priv, REG_LPLDO_CTRL, val8);

exit:
	return ret;
}

static int rtl8188eu_active_to_emu(struct rtl8xxxu_priv *priv)
{
	u8 val8;

	/* Turn off RF */
	val8 = rtl8xxxu_read8(priv, REG_RF_CTRL);
	val8 &= ~RF_ENABLE;
	rtl8xxxu_write8(priv, REG_RF_CTRL, val8);

	/* LDO Sleep mode */
	val8 = rtl8xxxu_read8(priv, REG_LPLDO_CTRL);
	val8 |= BIT(4);
	rtl8xxxu_write8(priv, REG_LPLDO_CTRL, val8);

	return 0;
}

static int rtl8188eu_emu_to_disabled(struct rtl8xxxu_priv *priv)
{
	u32 val32;
	u16 val16;
	u8 val8;

	val32 = rtl8xxxu_read32(priv, REG_AFE_XTAL_CTRL);
	val32 |= BIT(23);
	rtl8xxxu_write32(priv, REG_AFE_XTAL_CTRL, val32);

	val16 = rtl8xxxu_read16(priv, REG_APS_FSMCO);
	val16 &= ~APS_FSMCO_PCIE;
	val16 |= APS_FSMCO_HW_SUSPEND;
	rtl8xxxu_write16(priv, REG_APS_FSMCO, val16);

	rtl8xxxu_write8(priv, REG_APS_FSMCO + 3, 0x00);

	val8 = rtl8xxxu_read8(priv, REG_GPIO_MUXCFG + 1);
	val8 &= ~BIT(4);
	rtl8xxxu_write8(priv, REG_GPIO_MUXCFG + 1, val8);

	/* Set USB suspend enable local register 0xfe10[4]=1 */
	val8 = rtl8xxxu_read8(priv, 0xfe10);
	val8 |= BIT(4);
	rtl8xxxu_write8(priv, 0xfe10, val8);

	return 0;
}

static int rtl8188eu_active_to_lps(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	u8 val8;
	u16 val16;
	u32 val32;
	int retry, retval;

	rtl8xxxu_write8(priv, REG_TXPAUSE, 0x7f);

	retry = 100;
	retval = -EBUSY;
	/* Poll 32 bit wide REG_SCH_TX_CMD for 0 to ensure no TX is pending. */
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

	/* Reset MAC TRX */
	val16 = rtl8xxxu_read16(priv, REG_CR);
	val16 |= 0xff;
	val16 &= ~(CR_MAC_TX_ENABLE | CR_MAC_RX_ENABLE | CR_SECURITY_ENABLE);
	rtl8xxxu_write16(priv, REG_CR, val16);

	val8 = rtl8xxxu_read8(priv, REG_DUAL_TSF_RST);
	val8 |= DUAL_TSF_TX_OK;
	rtl8xxxu_write8(priv, REG_DUAL_TSF_RST, val8);

out:
	return retval;
}

static int rtl8188eu_power_on(struct rtl8xxxu_priv *priv)
{
	u16 val16;
	int ret;

	rtl8188e_disabled_to_emu(priv);

	ret = rtl8188e_emu_to_active(priv);
	if (ret)
		goto exit;

	/*
	 * Enable MAC DMA/WMAC/SCHEDULE/SEC block
	 * Set CR bit10 to enable 32k calibration.
	 * We do not set CR_MAC_TX_ENABLE | CR_MAC_RX_ENABLE here
	 * due to a hardware bug in the 88E, requiring those to be
	 * set after REG_TRXFF_BNDY is set. If not the RXFF bundary
	 * will get set to a larger buffer size than the real buffer
	 * size.
	 */
	val16 = (CR_HCI_TXDMA_ENABLE | CR_HCI_RXDMA_ENABLE |
		 CR_TXDMA_ENABLE | CR_RXDMA_ENABLE |
		 CR_PROTOCOL_ENABLE | CR_SCHEDULE_ENABLE |
		 CR_SECURITY_ENABLE | CR_CALTIMER_ENABLE);
	rtl8xxxu_write16(priv, REG_CR, val16);

exit:
	return ret;
}

static void rtl8188eu_power_off(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u16 val16;

	rtl8xxxu_flush_fifo(priv);

	val8 = rtl8xxxu_read8(priv, REG_TX_REPORT_CTRL);
	val8 &= ~TX_REPORT_CTRL_TIMER_ENABLE;
	rtl8xxxu_write8(priv, REG_TX_REPORT_CTRL, val8);

	/* Turn off RF */
	rtl8xxxu_write8(priv, REG_RF_CTRL, 0x00);

	rtl8188eu_active_to_lps(priv);

	/* Reset Firmware if running in RAM */
	if (rtl8xxxu_read8(priv, REG_MCU_FW_DL) & MCU_FW_RAM_SEL)
		rtl8xxxu_firmware_self_reset(priv);

	/* Reset MCU */
	val16 = rtl8xxxu_read16(priv, REG_SYS_FUNC);
	val16 &= ~SYS_FUNC_CPU_ENABLE;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, val16);

	/* Reset MCU ready status */
	rtl8xxxu_write8(priv, REG_MCU_FW_DL, 0x00);

	/* 32K_CTRL looks to be very 8188e specific */
	val8 = rtl8xxxu_read8(priv, REG_32K_CTRL);
	val8 &= ~BIT(0);
	rtl8xxxu_write8(priv, REG_32K_CTRL, val8);

	rtl8188eu_active_to_emu(priv);
	rtl8188eu_emu_to_disabled(priv);

	/* Reset MCU IO Wrapper */
	val8 = rtl8xxxu_read8(priv, REG_RSV_CTRL + 1);
	val8 &= ~BIT(3);
	rtl8xxxu_write8(priv, REG_RSV_CTRL + 1, val8);

	val8 = rtl8xxxu_read8(priv, REG_RSV_CTRL + 1);
	val8 |= BIT(3);
	rtl8xxxu_write8(priv, REG_RSV_CTRL + 1, val8);

	/* Vendor driver refers to GPIO_IN */
	val8 = rtl8xxxu_read8(priv, REG_GPIO_PIN_CTRL);
	/* Vendor driver refers to GPIO_OUT */
	rtl8xxxu_write8(priv, REG_GPIO_PIN_CTRL + 1, val8);
	rtl8xxxu_write8(priv, REG_GPIO_PIN_CTRL + 2, 0xff);

	val8 = rtl8xxxu_read8(priv, REG_GPIO_IO_SEL);
	rtl8xxxu_write8(priv, REG_GPIO_IO_SEL, val8 << 4);
	val8 = rtl8xxxu_read8(priv, REG_GPIO_IO_SEL + 1);
	rtl8xxxu_write8(priv, REG_GPIO_IO_SEL + 1, val8 | 0x0f);

	/*
	 * Set LNA, TRSW, EX_PA Pin to output mode
	 * Referred to as REG_BB_PAD_CTRL in 8188eu vendor driver
	 */
	rtl8xxxu_write32(priv, REG_PAD_CTRL1, 0x00080808);

	rtl8xxxu_write8(priv, REG_RSV_CTRL, 0x00);

	rtl8xxxu_write32(priv, REG_GPIO_MUXCFG, 0x00000000);
}

static void rtl8188e_enable_rf(struct rtl8xxxu_priv *priv)
{
	u32 val32;

	rtl8xxxu_write8(priv, REG_RF_CTRL, RF_ENABLE | RF_RSTB | RF_SDMRSTB);

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_TRX_PATH_ENABLE);
	val32 &= ~(OFDM_RF_PATH_RX_MASK | OFDM_RF_PATH_TX_MASK);
	val32 |= OFDM_RF_PATH_RX_A | OFDM_RF_PATH_TX_A;
	rtl8xxxu_write32(priv, REG_OFDM0_TRX_PATH_ENABLE, val32);

	rtl8xxxu_write8(priv, REG_TXPAUSE, 0x00);
}

static void rtl8188e_disable_rf(struct rtl8xxxu_priv *priv)
{
	u32 val32;

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_TRX_PATH_ENABLE);
	val32 &= ~OFDM_RF_PATH_TX_MASK;
	rtl8xxxu_write32(priv, REG_OFDM0_TRX_PATH_ENABLE, val32);

	/* Power down RF module */
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_AC, 0);

	rtl8188eu_active_to_emu(priv);
}

static void rtl8188e_usb_quirks(struct rtl8xxxu_priv *priv)
{
	u16 val16;

	/*
	 * Technically this is not a USB quirk, but a chip quirk.
	 * This has to be done after REG_TRXFF_BNDY is set, see
	 * rtl8188eu_power_on() for details.
	 */
	val16 = rtl8xxxu_read16(priv, REG_CR);
	val16 |= (CR_MAC_TX_ENABLE | CR_MAC_RX_ENABLE);
	rtl8xxxu_write16(priv, REG_CR, val16);

	rtl8xxxu_gen2_usb_quirks(priv);

	/* Pre-TX enable WEP/TKIP security */
	rtl8xxxu_write8(priv, REG_EARLY_MODE_CONTROL_8188E + 3, 0x01);
}

static s8 rtl8188e_cck_rssi(struct rtl8xxxu_priv *priv, struct rtl8723au_phy_stats *phy_stats)
{
	/* only use lna 0/1/2/3/7 */
	static const s8 lna_gain_table_0[8] = {17, -1, -13, -29, -32, -35, -38, -41};
	/* only use lna 3/7 */
	static const s8 lna_gain_table_1[8] = {29, 20, 12, 3, -6, -15, -24, -33};

	u8 cck_agc_rpt = phy_stats->cck_agc_rpt_ofdm_cfosho_a;
	s8 rx_pwr_all = 0x00;
	u8 vga_idx, lna_idx;
	s8 lna_gain = 0;

	lna_idx = u8_get_bits(cck_agc_rpt, CCK_AGC_RPT_LNA_IDX_MASK);
	vga_idx = u8_get_bits(cck_agc_rpt, CCK_AGC_RPT_VGA_IDX_MASK);

	if (priv->chip_cut >= 8) /* cut I */ /* SMIC */
		lna_gain = lna_gain_table_0[lna_idx];
	else /* TSMC */
		lna_gain = lna_gain_table_1[lna_idx];

	rx_pwr_all = lna_gain - (2 * vga_idx);

	return rx_pwr_all;
}

static int rtl8188eu_led_brightness_set(struct led_classdev *led_cdev,
					enum led_brightness brightness)
{
	struct rtl8xxxu_priv *priv = container_of(led_cdev,
						  struct rtl8xxxu_priv,
						  led_cdev);
	u8 ledcfg = rtl8xxxu_read8(priv, REG_LEDCFG2);

	if (brightness == LED_OFF) {
		ledcfg &= ~LEDCFG2_HW_LED_CONTROL;
		ledcfg |= LEDCFG2_SW_LED_CONTROL | LEDCFG2_SW_LED_DISABLE;
	} else if (brightness == LED_ON) {
		ledcfg &= ~(LEDCFG2_HW_LED_CONTROL | LEDCFG2_SW_LED_DISABLE);
		ledcfg |= LEDCFG2_SW_LED_CONTROL;
	} else if (brightness == RTL8XXXU_HW_LED_CONTROL) {
		ledcfg &= ~LEDCFG2_SW_LED_DISABLE;
		ledcfg |= LEDCFG2_HW_LED_CONTROL | LEDCFG2_HW_LED_ENABLE;
	}

	rtl8xxxu_write8(priv, REG_LEDCFG2, ledcfg);

	return 0;
}

static void rtl8188e_set_tx_rpt_timing(struct rtl8xxxu_ra_info *ra, u8 timing)
{
	u8 idx;

	for (idx = 0; idx < 5; idx++)
		if (dynamic_tx_rpt_timing[idx] == ra->rpt_time)
			break;

	if (timing == DEFAULT_TIMING) {
		idx = 0; /* 200ms */
	} else if (timing == INCREASE_TIMING) {
		if (idx < 5)
			idx++;
	} else if (timing == DECREASE_TIMING) {
		if (idx > 0)
			idx--;
	}

	ra->rpt_time = dynamic_tx_rpt_timing[idx];
}

static void rtl8188e_rate_down(struct rtl8xxxu_ra_info *ra)
{
	u8 rate_id = ra->pre_rate;
	u8 lowest_rate = ra->lowest_rate;
	u8 highest_rate = ra->highest_rate;
	s8 i;

	if (rate_id > highest_rate) {
		rate_id = highest_rate;
	} else if (ra->rate_sgi) {
		ra->rate_sgi = 0;
	} else if (rate_id > lowest_rate) {
		if (rate_id > 0) {
			for (i = rate_id - 1; i >= lowest_rate; i--) {
				if (ra->ra_use_rate & BIT(i)) {
					rate_id = i;
					goto rate_down_finish;
				}
			}
		}
	} else if (rate_id <= lowest_rate) {
		rate_id = lowest_rate;
	}

rate_down_finish:
	if (ra->ra_waiting_counter == 1) {
		ra->ra_waiting_counter++;
		ra->ra_pending_counter++;
	} else if (ra->ra_waiting_counter > 1) {
		ra->ra_waiting_counter = 0;
		ra->ra_pending_counter = 0;
	}

	if (ra->ra_pending_counter >= 4)
		ra->ra_pending_counter = 4;

	ra->ra_drop_after_down = 1;

	ra->decision_rate = rate_id;

	rtl8188e_set_tx_rpt_timing(ra, DECREASE_TIMING);
}

static void rtl8188e_rate_up(struct rtl8xxxu_ra_info *ra)
{
	u8 rate_id = ra->pre_rate;
	u8 highest_rate = ra->highest_rate;
	u8 i;

	if (ra->ra_waiting_counter == 1) {
		ra->ra_waiting_counter = 0;
		ra->ra_pending_counter = 0;
	} else if (ra->ra_waiting_counter > 1) {
		ra->pre_rssi_sta_ra = ra->rssi_sta_ra;
		goto rate_up_finish;
	}

	rtl8188e_set_tx_rpt_timing(ra, DEFAULT_TIMING);

	if (rate_id < highest_rate) {
		for (i = rate_id + 1; i <= highest_rate; i++) {
			if (ra->ra_use_rate & BIT(i)) {
				rate_id = i;
				goto rate_up_finish;
			}
		}
	} else if (rate_id == highest_rate) {
		if (ra->sgi_enable && !ra->rate_sgi)
			ra->rate_sgi = 1;
		else if (!ra->sgi_enable)
			ra->rate_sgi = 0;
	} else { /* rate_id > ra->highest_rate */
		rate_id = highest_rate;
	}

rate_up_finish:
	if (ra->ra_waiting_counter == (4 + pending_for_rate_up_fail[ra->ra_pending_counter]))
		ra->ra_waiting_counter = 0;
	else
		ra->ra_waiting_counter++;

	ra->decision_rate = rate_id;
}

static void rtl8188e_reset_ra_counter(struct rtl8xxxu_ra_info *ra)
{
	u8 rate_id = ra->decision_rate;

	ra->nsc_up = (n_threshold_high[rate_id] + n_threshold_low[rate_id]) >> 1;
	ra->nsc_down = (n_threshold_high[rate_id] + n_threshold_low[rate_id]) >> 1;
}

static void rtl8188e_rate_decision(struct rtl8xxxu_ra_info *ra)
{
	struct rtl8xxxu_priv *priv = container_of(ra, struct rtl8xxxu_priv, ra_info);
	const u8 *retry_penalty_idx_0;
	const u8 *retry_penalty_idx_1;
	const u8 *retry_penalty_up_idx;
	u8 rate_id, penalty_id1, penalty_id2;
	int i;

	if (ra->total == 0)
		return;

	if (ra->ra_drop_after_down) {
		ra->ra_drop_after_down--;

		rtl8188e_reset_ra_counter(ra);

		return;
	}

	if (priv->chip_cut == 8) { /* cut I */
		retry_penalty_idx_0 = retry_penalty_idx_cut_i[0];
		retry_penalty_idx_1 = retry_penalty_idx_cut_i[1];
		retry_penalty_up_idx = retry_penalty_up_idx_cut_i;
	} else {
		retry_penalty_idx_0 = retry_penalty_idx_normal[0];
		retry_penalty_idx_1 = retry_penalty_idx_normal[1];
		retry_penalty_up_idx = retry_penalty_up_idx_normal;
	}

	if (ra->rssi_sta_ra < (ra->pre_rssi_sta_ra - 3) ||
	    ra->rssi_sta_ra > (ra->pre_rssi_sta_ra + 3)) {
		ra->pre_rssi_sta_ra = ra->rssi_sta_ra;
		ra->ra_waiting_counter = 0;
		ra->ra_pending_counter = 0;
	}

	/* Start RA decision */
	if (ra->pre_rate > ra->highest_rate)
		rate_id = ra->highest_rate;
	else
		rate_id = ra->pre_rate;

	/* rate down */
	if (ra->rssi_sta_ra > rssi_threshold[rate_id])
		penalty_id1 = retry_penalty_idx_0[rate_id];
	else
		penalty_id1 = retry_penalty_idx_1[rate_id];

	for (i = 0; i < 5; i++)
		ra->nsc_down += ra->retry[i] * retry_penalty[penalty_id1][i];

	if (ra->nsc_down > (ra->total * retry_penalty[penalty_id1][5]))
		ra->nsc_down -= ra->total * retry_penalty[penalty_id1][5];
	else
		ra->nsc_down = 0;

	/* rate up */
	penalty_id2 = retry_penalty_up_idx[rate_id];

	for (i = 0; i < 5; i++)
		ra->nsc_up += ra->retry[i] * retry_penalty[penalty_id2][i];

	if (ra->nsc_up > (ra->total * retry_penalty[penalty_id2][5]))
		ra->nsc_up -= ra->total * retry_penalty[penalty_id2][5];
	else
		ra->nsc_up = 0;

	if (ra->nsc_down < n_threshold_low[rate_id] ||
	    ra->drop > dropping_necessary[rate_id]) {
		rtl8188e_rate_down(ra);

		rtl8xxxu_update_ra_report(&priv->ra_report, ra->decision_rate,
					  ra->rate_sgi, priv->ra_report.txrate.bw);
	} else if (ra->nsc_up > n_threshold_high[rate_id]) {
		rtl8188e_rate_up(ra);

		rtl8xxxu_update_ra_report(&priv->ra_report, ra->decision_rate,
					  ra->rate_sgi, priv->ra_report.txrate.bw);
	}

	if (ra->decision_rate == ra->pre_rate)
		ra->dynamic_tx_rpt_timing_counter++;
	else
		ra->dynamic_tx_rpt_timing_counter = 0;

	if (ra->dynamic_tx_rpt_timing_counter >= 4) {
		/* Rate didn't change 4 times, extend RPT timing */
		rtl8188e_set_tx_rpt_timing(ra, INCREASE_TIMING);
		ra->dynamic_tx_rpt_timing_counter = 0;
	}

	ra->pre_rate = ra->decision_rate;

	rtl8188e_reset_ra_counter(ra);
}

static void rtl8188e_power_training_try_state(struct rtl8xxxu_ra_info *ra)
{
	ra->pt_try_state = 0;
	switch (ra->pt_mode_ss) {
	case 3:
		if (ra->decision_rate >= DESC_RATE_MCS13)
			ra->pt_try_state = 1;
		break;
	case 2:
		if (ra->decision_rate >= DESC_RATE_MCS5)
			ra->pt_try_state = 1;
		break;
	case 1:
		if (ra->decision_rate >= DESC_RATE_48M)
			ra->pt_try_state = 1;
		break;
	case 0:
		if (ra->decision_rate >= DESC_RATE_11M)
			ra->pt_try_state = 1;
		break;
	default:
		break;
	}

	if (ra->rssi_sta_ra < 48) {
		ra->pt_stage = 0;
	} else if (ra->pt_try_state == 1) {
		if ((ra->pt_stop_count >= 10) ||
		    (ra->pt_pre_rssi > ra->rssi_sta_ra + 5) ||
		    (ra->pt_pre_rssi < ra->rssi_sta_ra - 5) ||
		    (ra->decision_rate != ra->pt_pre_rate)) {
			if (ra->pt_stage == 0)
				ra->pt_stage = 1;
			else if (ra->pt_stage == 1)
				ra->pt_stage = 3;
			else
				ra->pt_stage = 5;

			ra->pt_pre_rssi = ra->rssi_sta_ra;
			ra->pt_stop_count = 0;
		} else {
			ra->ra_stage = 0;
			ra->pt_stop_count++;
		}
	} else {
		ra->pt_stage = 0;
		ra->ra_stage = 0;
	}

	ra->pt_pre_rate = ra->decision_rate;

	/* TODO: implement the "false alarm" statistics for this */
	/* Disable power training when noisy environment */
	/* if (p_dm_odm->is_disable_power_training) { */
	if (1) {
		ra->pt_stage = 0;
		ra->ra_stage = 0;
		ra->pt_stop_count = 0;
	}
}

static void rtl8188e_power_training_decision(struct rtl8xxxu_ra_info *ra)
{
	u8 temp_stage;
	u32 numsc;
	u32 num_total;
	u8 stage_id;
	u8 j;

	numsc = 0;
	num_total = ra->total * pt_penalty[5];
	for (j = 0; j <= 4; j++) {
		numsc += ra->retry[j] * pt_penalty[j];

		if (numsc > num_total)
			break;
	}

	j >>= 1;
	temp_stage = (ra->pt_stage + 1) >> 1;
	if (temp_stage > j)
		stage_id = temp_stage - j;
	else
		stage_id = 0;

	ra->pt_smooth_factor = (ra->pt_smooth_factor >> 1) +
			       (ra->pt_smooth_factor >> 2) +
			       stage_id * 16 + 2;
	if (ra->pt_smooth_factor > 192)
		ra->pt_smooth_factor = 192;
	stage_id = ra->pt_smooth_factor >> 6;
	temp_stage = stage_id * 2;
	if (temp_stage != 0)
		temp_stage--;
	if (ra->drop > 3)
		temp_stage = 0;
	ra->pt_stage = temp_stage;
}

void rtl8188e_handle_ra_tx_report2(struct rtl8xxxu_priv *priv, struct sk_buff *skb)
{
	u32 *_rx_desc = (u32 *)(skb->data - sizeof(struct rtl8xxxu_rxdesc16));
	struct rtl8xxxu_rxdesc16 *rx_desc = (struct rtl8xxxu_rxdesc16 *)_rx_desc;
	struct device *dev = &priv->udev->dev;
	struct rtl8xxxu_ra_info *ra = &priv->ra_info;
	u32 tx_rpt_len = rx_desc->pktlen & 0x3ff;
	u32 items = tx_rpt_len / TX_RPT2_ITEM_SIZE;
	u64 macid_valid = ((u64)_rx_desc[5] << 32) | _rx_desc[4];
	u32 macid;
	u8 *rpt = skb->data;
	bool valid;
	u16 min_rpt_time = 0x927c;

	dev_dbg(dev, "%s: len: %d items: %d\n", __func__, tx_rpt_len, items);

	/* We only use macid 0, so only the first item is relevant.
	 * AP mode will use more of them if it's ever implemented.
	 */
	if (!priv->vifs[0] || priv->vifs[0]->type == NL80211_IFTYPE_STATION)
		items = 1;

	for (macid = 0; macid < items; macid++) {
		valid = false;

		if (macid < 64)
			valid = macid_valid & BIT(macid);

		if (valid) {
			ra->retry[0] = le16_to_cpu(*(__le16 *)rpt);
			ra->retry[1] = rpt[2];
			ra->retry[2] = rpt[3];
			ra->retry[3] = rpt[4];
			ra->retry[4] = rpt[5];
			ra->drop = rpt[6];
			ra->total = ra->retry[0] + ra->retry[1] + ra->retry[2] +
				    ra->retry[3] + ra->retry[4] + ra->drop;

			if (ra->total > 0) {
				if (ra->ra_stage < 5)
					rtl8188e_rate_decision(ra);
				else if (ra->ra_stage == 5)
					rtl8188e_power_training_try_state(ra);
				else /* ra->ra_stage == 6 */
					rtl8188e_power_training_decision(ra);

				if (ra->ra_stage <= 5)
					ra->ra_stage++;
				else
					ra->ra_stage = 0;
			}
		} else if (macid == 0) {
			dev_warn(dev, "%s: TX report item 0 not valid\n", __func__);
		}

		dev_dbg(dev, "%s:  valid: %d retry: %d %d %d %d %d drop: %d\n",
			__func__, valid,
			ra->retry[0], ra->retry[1], ra->retry[2],
			ra->retry[3], ra->retry[4], ra->drop);

		if (min_rpt_time > ra->rpt_time)
			min_rpt_time = ra->rpt_time;

		rpt += TX_RPT2_ITEM_SIZE;
	}

	if (min_rpt_time != ra->pre_min_rpt_time) {
		rtl8xxxu_write16(priv, REG_TX_REPORT_TIME, min_rpt_time);
		ra->pre_min_rpt_time = min_rpt_time;
	}
}

static void rtl8188e_arfb_refresh(struct rtl8xxxu_ra_info *ra)
{
	s8 i;

	ra->ra_use_rate = ra->rate_mask;

	/* Highest rate */
	if (ra->ra_use_rate) {
		for (i = RATESIZE; i >= 0; i--) {
			if (ra->ra_use_rate & BIT(i)) {
				ra->highest_rate = i;
				break;
			}
		}
	} else {
		ra->highest_rate = 0;
	}

	/* Lowest rate */
	if (ra->ra_use_rate) {
		for (i = 0; i < RATESIZE; i++) {
			if (ra->ra_use_rate & BIT(i)) {
				ra->lowest_rate = i;
				break;
			}
		}
	} else {
		ra->lowest_rate = 0;
	}

	if (ra->highest_rate > DESC_RATE_MCS7)
		ra->pt_mode_ss = 3;
	else if (ra->highest_rate > DESC_RATE_54M)
		ra->pt_mode_ss = 2;
	else if (ra->highest_rate > DESC_RATE_11M)
		ra->pt_mode_ss = 1;
	else
		ra->pt_mode_ss = 0;
}

static void
rtl8188e_update_rate_mask(struct rtl8xxxu_priv *priv,
			  u32 ramask, u8 rateid, int sgi, int txbw_40mhz,
			  u8 macid)
{
	struct rtl8xxxu_ra_info *ra = &priv->ra_info;

	ra->rate_id = rateid;
	ra->rate_mask = ramask;
	ra->sgi_enable = sgi;

	rtl8188e_arfb_refresh(ra);
}

static void rtl8188e_ra_set_rssi(struct rtl8xxxu_priv *priv, u8 macid, u8 rssi)
{
	priv->ra_info.rssi_sta_ra = rssi;
}

void rtl8188e_ra_info_init_all(struct rtl8xxxu_ra_info *ra)
{
	ra->decision_rate = DESC_RATE_MCS7;
	ra->pre_rate = DESC_RATE_MCS7;
	ra->highest_rate = DESC_RATE_MCS7;
	ra->lowest_rate = 0;
	ra->rate_id = 0;
	ra->rate_mask = 0xfffff;
	ra->rssi_sta_ra = 0;
	ra->pre_rssi_sta_ra = 0;
	ra->sgi_enable = 0;
	ra->ra_use_rate = 0xfffff;
	ra->nsc_down = (n_threshold_high[DESC_RATE_MCS7] + n_threshold_low[DESC_RATE_MCS7]) / 2;
	ra->nsc_up = (n_threshold_high[DESC_RATE_MCS7] + n_threshold_low[DESC_RATE_MCS7]) / 2;
	ra->rate_sgi = 0;
	ra->rpt_time = 0x927c;
	ra->drop = 0;
	ra->retry[0] = 0;
	ra->retry[1] = 0;
	ra->retry[2] = 0;
	ra->retry[3] = 0;
	ra->retry[4] = 0;
	ra->total = 0;
	ra->ra_waiting_counter = 0;
	ra->ra_pending_counter = 0;
	ra->ra_drop_after_down = 0;

	ra->pt_try_state = 0;
	ra->pt_stage = 5;
	ra->pt_smooth_factor = 192;
	ra->pt_stop_count = 0;
	ra->pt_pre_rate = 0;
	ra->pt_pre_rssi = 0;
	ra->pt_mode_ss = 0;
	ra->ra_stage = 0;
}

struct rtl8xxxu_fileops rtl8188eu_fops = {
	.identify_chip = rtl8188eu_identify_chip,
	.parse_efuse = rtl8188eu_parse_efuse,
	.load_firmware = rtl8188eu_load_firmware,
	.power_on = rtl8188eu_power_on,
	.power_off = rtl8188eu_power_off,
	.read_efuse = rtl8xxxu_read_efuse,
	.reset_8051 = rtl8188eu_reset_8051,
	.llt_init = rtl8xxxu_init_llt_table,
	.init_phy_bb = rtl8188eu_init_phy_bb,
	.init_phy_rf = rtl8188eu_init_phy_rf,
	.phy_lc_calibrate = rtl8723a_phy_lc_calibrate,
	.phy_iq_calibrate = rtl8188eu_phy_iq_calibrate,
	.config_channel = rtl8188eu_config_channel,
	.parse_rx_desc = rtl8xxxu_parse_rxdesc16,
	.parse_phystats = rtl8723au_rx_parse_phystats,
	.init_aggregation = rtl8188eu_init_aggregation,
	.enable_rf = rtl8188e_enable_rf,
	.disable_rf = rtl8188e_disable_rf,
	.usb_quirks = rtl8188e_usb_quirks,
	.set_tx_power = rtl8188f_set_tx_power,
	.update_rate_mask = rtl8188e_update_rate_mask,
	.report_connect = rtl8xxxu_gen2_report_connect,
	.report_rssi = rtl8188e_ra_set_rssi,
	.fill_txdesc = rtl8xxxu_fill_txdesc_v3,
	.set_crystal_cap = rtl8188f_set_crystal_cap,
	.cck_rssi = rtl8188e_cck_rssi,
	.led_classdev_brightness_set = rtl8188eu_led_brightness_set,
	.writeN_block_size = 128,
	.rx_desc_size = sizeof(struct rtl8xxxu_rxdesc16),
	.tx_desc_size = sizeof(struct rtl8xxxu_txdesc32),
	.has_tx_report = 1,
	.init_reg_pkt_life_time = 1,
	.gen2_thermal_meter = 1,
	.max_sec_cam_num = 32,
	.adda_1t_init = 0x0b1b25a0,
	.adda_1t_path_on = 0x0bdb25a0,
	/*
	 * Use 9K for 8188e normal chip
	 * Max RX buffer = 10K - max(TxReportSize(64*8), WOLPattern(16*24))
	 */
	.trxff_boundary = 0x25ff,
	.pbp_rx = PBP_PAGE_SIZE_128,
	.pbp_tx = PBP_PAGE_SIZE_128,
	.mactable = rtl8188e_mac_init_table,
	.total_page_num = TX_TOTAL_PAGE_NUM_8188E,
	.page_num_hi = TX_PAGE_NUM_HI_PQ_8188E,
	.page_num_lo = TX_PAGE_NUM_LO_PQ_8188E,
	.page_num_norm = TX_PAGE_NUM_NORM_PQ_8188E,
	.last_llt_entry = 175,
};
