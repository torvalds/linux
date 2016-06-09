/*
 * RTL8XXXU mac80211 USB driver - 8192e specific subdriver
 *
 * Copyright (c) 2014 - 2016 Jes Sorensen <Jes.Sorensen@redhat.com>
 *
 * Portions, notably calibration code:
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This driver was written as a replacement for the vendor provided
 * rtl8723au driver. As the Realtek 8xxx chips are very similar in
 * their programming interface, I have started adding support for
 * additional 8xxx chips like the 8192cu, 8188cus, etc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
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

static struct rtl8xxxu_reg8val rtl8192e_mac_init_table[] = {
	{0x011, 0xeb}, {0x012, 0x07}, {0x014, 0x75}, {0x303, 0xa7},
	{0x428, 0x0a}, {0x429, 0x10}, {0x430, 0x00}, {0x431, 0x00},
	{0x432, 0x00}, {0x433, 0x01}, {0x434, 0x04}, {0x435, 0x05},
	{0x436, 0x07}, {0x437, 0x08}, {0x43c, 0x04}, {0x43d, 0x05},
	{0x43e, 0x07}, {0x43f, 0x08}, {0x440, 0x5d}, {0x441, 0x01},
	{0x442, 0x00}, {0x444, 0x10}, {0x445, 0x00}, {0x446, 0x00},
	{0x447, 0x00}, {0x448, 0x00}, {0x449, 0xf0}, {0x44a, 0x0f},
	{0x44b, 0x3e}, {0x44c, 0x10}, {0x44d, 0x00}, {0x44e, 0x00},
	{0x44f, 0x00}, {0x450, 0x00}, {0x451, 0xf0}, {0x452, 0x0f},
	{0x453, 0x00}, {0x456, 0x5e}, {0x460, 0x66}, {0x461, 0x66},
	{0x4c8, 0xff}, {0x4c9, 0x08}, {0x4cc, 0xff}, {0x4cd, 0xff},
	{0x4ce, 0x01}, {0x500, 0x26}, {0x501, 0xa2}, {0x502, 0x2f},
	{0x503, 0x00}, {0x504, 0x28}, {0x505, 0xa3}, {0x506, 0x5e},
	{0x507, 0x00}, {0x508, 0x2b}, {0x509, 0xa4}, {0x50a, 0x5e},
	{0x50b, 0x00}, {0x50c, 0x4f}, {0x50d, 0xa4}, {0x50e, 0x00},
	{0x50f, 0x00}, {0x512, 0x1c}, {0x514, 0x0a}, {0x516, 0x0a},
	{0x525, 0x4f}, {0x540, 0x12}, {0x541, 0x64}, {0x550, 0x10},
	{0x551, 0x10}, {0x559, 0x02}, {0x55c, 0x50}, {0x55d, 0xff},
	{0x605, 0x30}, {0x608, 0x0e}, {0x609, 0x2a}, {0x620, 0xff},
	{0x621, 0xff}, {0x622, 0xff}, {0x623, 0xff}, {0x624, 0xff},
	{0x625, 0xff}, {0x626, 0xff}, {0x627, 0xff}, {0x638, 0x50},
	{0x63c, 0x0a}, {0x63d, 0x0a}, {0x63e, 0x0e}, {0x63f, 0x0e},
	{0x640, 0x40}, {0x642, 0x40}, {0x643, 0x00}, {0x652, 0xc8},
	{0x66e, 0x05}, {0x700, 0x21}, {0x701, 0x43}, {0x702, 0x65},
	{0x703, 0x87}, {0x708, 0x21}, {0x709, 0x43}, {0x70a, 0x65},
	{0x70b, 0x87},
	{0xffff, 0xff},
};

static struct rtl8xxxu_reg32val rtl8192eu_phy_init_table[] = {
	{0x800, 0x80040000}, {0x804, 0x00000003},
	{0x808, 0x0000fc00}, {0x80c, 0x0000000a},
	{0x810, 0x10001331}, {0x814, 0x020c3d10},
	{0x818, 0x02220385}, {0x81c, 0x00000000},
	{0x820, 0x01000100}, {0x824, 0x00390204},
	{0x828, 0x01000100}, {0x82c, 0x00390204},
	{0x830, 0x32323232}, {0x834, 0x30303030},
	{0x838, 0x30303030}, {0x83c, 0x30303030},
	{0x840, 0x00010000}, {0x844, 0x00010000},
	{0x848, 0x28282828}, {0x84c, 0x28282828},
	{0x850, 0x00000000}, {0x854, 0x00000000},
	{0x858, 0x009a009a}, {0x85c, 0x01000014},
	{0x860, 0x66f60000}, {0x864, 0x061f0000},
	{0x868, 0x30303030}, {0x86c, 0x30303030},
	{0x870, 0x00000000}, {0x874, 0x55004200},
	{0x878, 0x08080808}, {0x87c, 0x00000000},
	{0x880, 0xb0000c1c}, {0x884, 0x00000001},
	{0x888, 0x00000000}, {0x88c, 0xcc0000c0},
	{0x890, 0x00000800}, {0x894, 0xfffffffe},
	{0x898, 0x40302010}, {0x900, 0x00000000},
	{0x904, 0x00000023}, {0x908, 0x00000000},
	{0x90c, 0x81121313}, {0x910, 0x806c0001},
	{0x914, 0x00000001}, {0x918, 0x00000000},
	{0x91c, 0x00010000}, {0x924, 0x00000001},
	{0x928, 0x00000000}, {0x92c, 0x00000000},
	{0x930, 0x00000000}, {0x934, 0x00000000},
	{0x938, 0x00000000}, {0x93c, 0x00000000},
	{0x940, 0x00000000}, {0x944, 0x00000000},
	{0x94c, 0x00000008}, {0xa00, 0x00d0c7c8},
	{0xa04, 0x81ff000c}, {0xa08, 0x8c838300},
	{0xa0c, 0x2e68120f}, {0xa10, 0x95009b78},
	{0xa14, 0x1114d028}, {0xa18, 0x00881117},
	{0xa1c, 0x89140f00}, {0xa20, 0x1a1b0000},
	{0xa24, 0x090e1317}, {0xa28, 0x00000204},
	{0xa2c, 0x00d30000}, {0xa70, 0x101fff00},
	{0xa74, 0x00000007}, {0xa78, 0x00000900},
	{0xa7c, 0x225b0606}, {0xa80, 0x218075b1},
	{0xb38, 0x00000000}, {0xc00, 0x48071d40},
	{0xc04, 0x03a05633}, {0xc08, 0x000000e4},
	{0xc0c, 0x6c6c6c6c}, {0xc10, 0x08800000},
	{0xc14, 0x40000100}, {0xc18, 0x08800000},
	{0xc1c, 0x40000100}, {0xc20, 0x00000000},
	{0xc24, 0x00000000}, {0xc28, 0x00000000},
	{0xc2c, 0x00000000}, {0xc30, 0x69e9ac47},
	{0xc34, 0x469652af}, {0xc38, 0x49795994},
	{0xc3c, 0x0a97971c}, {0xc40, 0x1f7c403f},
	{0xc44, 0x000100b7}, {0xc48, 0xec020107},
	{0xc4c, 0x007f037f},
#ifdef EXT_PA_8192EU
	/* External PA or external LNA */
	{0xc50, 0x00340220},
#else
	{0xc50, 0x00340020},
#endif
	{0xc54, 0x0080801f},
#ifdef EXT_PA_8192EU
	/* External PA or external LNA */
	{0xc58, 0x00000220},
#else
	{0xc58, 0x00000020},
#endif
	{0xc5c, 0x00248492}, {0xc60, 0x00000000},
	{0xc64, 0x7112848b}, {0xc68, 0x47c00bff},
	{0xc6c, 0x00000036}, {0xc70, 0x00000600},
	{0xc74, 0x02013169}, {0xc78, 0x0000001f},
	{0xc7c, 0x00b91612},
#ifdef EXT_PA_8192EU
	/* External PA or external LNA */
	{0xc80, 0x2d4000b5},
#else
	{0xc80, 0x40000100},
#endif
	{0xc84, 0x21f60000},
#ifdef EXT_PA_8192EU
	/* External PA or external LNA */
	{0xc88, 0x2d4000b5},
#else
	{0xc88, 0x40000100},
#endif
	{0xc8c, 0xa0e40000}, {0xc90, 0x00121820},
	{0xc94, 0x00000000}, {0xc98, 0x00121820},
	{0xc9c, 0x00007f7f}, {0xca0, 0x00000000},
	{0xca4, 0x000300a0}, {0xca8, 0x00000000},
	{0xcac, 0x00000000}, {0xcb0, 0x00000000},
	{0xcb4, 0x00000000}, {0xcb8, 0x00000000},
	{0xcbc, 0x28000000}, {0xcc0, 0x00000000},
	{0xcc4, 0x00000000}, {0xcc8, 0x00000000},
	{0xccc, 0x00000000}, {0xcd0, 0x00000000},
	{0xcd4, 0x00000000}, {0xcd8, 0x64b22427},
	{0xcdc, 0x00766932}, {0xce0, 0x00222222},
	{0xce4, 0x00040000}, {0xce8, 0x77644302},
	{0xcec, 0x2f97d40c}, {0xd00, 0x00080740},
	{0xd04, 0x00020403}, {0xd08, 0x0000907f},
	{0xd0c, 0x20010201}, {0xd10, 0xa0633333},
	{0xd14, 0x3333bc43}, {0xd18, 0x7a8f5b6b},
	{0xd1c, 0x0000007f}, {0xd2c, 0xcc979975},
	{0xd30, 0x00000000}, {0xd34, 0x80608000},
	{0xd38, 0x00000000}, {0xd3c, 0x00127353},
	{0xd40, 0x00000000}, {0xd44, 0x00000000},
	{0xd48, 0x00000000}, {0xd4c, 0x00000000},
	{0xd50, 0x6437140a}, {0xd54, 0x00000000},
	{0xd58, 0x00000282}, {0xd5c, 0x30032064},
	{0xd60, 0x4653de68}, {0xd64, 0x04518a3c},
	{0xd68, 0x00002101}, {0xd6c, 0x2a201c16},
	{0xd70, 0x1812362e}, {0xd74, 0x322c2220},
	{0xd78, 0x000e3c24}, {0xd80, 0x01081008},
	{0xd84, 0x00000800}, {0xd88, 0xf0b50000},
	{0xe00, 0x30303030}, {0xe04, 0x30303030},
	{0xe08, 0x03903030}, {0xe10, 0x30303030},
	{0xe14, 0x30303030}, {0xe18, 0x30303030},
	{0xe1c, 0x30303030}, {0xe28, 0x00000000},
	{0xe30, 0x1000dc1f}, {0xe34, 0x10008c1f},
	{0xe38, 0x02140102}, {0xe3c, 0x681604c2},
	{0xe40, 0x01007c00}, {0xe44, 0x01004800},
	{0xe48, 0xfb000000}, {0xe4c, 0x000028d1},
	{0xe50, 0x1000dc1f}, {0xe54, 0x10008c1f},
	{0xe58, 0x02140102}, {0xe5c, 0x28160d05},
	{0xe60, 0x00000008}, {0xe68, 0x0fc05656},
	{0xe6c, 0x03c09696}, {0xe70, 0x03c09696},
	{0xe74, 0x0c005656}, {0xe78, 0x0c005656},
	{0xe7c, 0x0c005656}, {0xe80, 0x0c005656},
	{0xe84, 0x03c09696}, {0xe88, 0x0c005656},
	{0xe8c, 0x03c09696}, {0xed0, 0x03c09696},
	{0xed4, 0x03c09696}, {0xed8, 0x03c09696},
	{0xedc, 0x0000d6d6}, {0xee0, 0x0000d6d6},
	{0xeec, 0x0fc01616}, {0xee4, 0xb0000c1c},
	{0xee8, 0x00000001}, {0xf14, 0x00000003},
	{0xf4c, 0x00000000}, {0xf00, 0x00000300},
	{0xffff, 0xffffffff},
};

static struct rtl8xxxu_reg32val rtl8xxx_agc_8192eu_std_table[] = {
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
	{0xc78, 0xc81a0001}, {0xc78, 0xc71b0001},
	{0xc78, 0xc61c0001}, {0xc78, 0x071d0001},
	{0xc78, 0x061e0001}, {0xc78, 0x051f0001},
	{0xc78, 0x04200001}, {0xc78, 0x03210001},
	{0xc78, 0xaa220001}, {0xc78, 0xa9230001},
	{0xc78, 0xa8240001}, {0xc78, 0xa7250001},
	{0xc78, 0xa6260001}, {0xc78, 0x85270001},
	{0xc78, 0x84280001}, {0xc78, 0x83290001},
	{0xc78, 0x252a0001}, {0xc78, 0x242b0001},
	{0xc78, 0x232c0001}, {0xc78, 0x222d0001},
	{0xc78, 0x672e0001}, {0xc78, 0x662f0001},
	{0xc78, 0x65300001}, {0xc78, 0x64310001},
	{0xc78, 0x63320001}, {0xc78, 0x62330001},
	{0xc78, 0x61340001}, {0xc78, 0x45350001},
	{0xc78, 0x44360001}, {0xc78, 0x43370001},
	{0xc78, 0x42380001}, {0xc78, 0x41390001},
	{0xc78, 0x403a0001}, {0xc78, 0x403b0001},
	{0xc78, 0x403c0001}, {0xc78, 0x403d0001},
	{0xc78, 0x403e0001}, {0xc78, 0x403f0001},
	{0xc78, 0xfb400001}, {0xc78, 0xfb410001},
	{0xc78, 0xfb420001}, {0xc78, 0xfb430001},
	{0xc78, 0xfb440001}, {0xc78, 0xfb450001},
	{0xc78, 0xfa460001}, {0xc78, 0xf9470001},
	{0xc78, 0xf8480001}, {0xc78, 0xf7490001},
	{0xc78, 0xf64a0001}, {0xc78, 0xf54b0001},
	{0xc78, 0xf44c0001}, {0xc78, 0xf34d0001},
	{0xc78, 0xf24e0001}, {0xc78, 0xf14f0001},
	{0xc78, 0xf0500001}, {0xc78, 0xef510001},
	{0xc78, 0xee520001}, {0xc78, 0xed530001},
	{0xc78, 0xec540001}, {0xc78, 0xeb550001},
	{0xc78, 0xea560001}, {0xc78, 0xe9570001},
	{0xc78, 0xe8580001}, {0xc78, 0xe7590001},
	{0xc78, 0xe65a0001}, {0xc78, 0xe55b0001},
	{0xc78, 0xe45c0001}, {0xc78, 0xe35d0001},
	{0xc78, 0xe25e0001}, {0xc78, 0xe15f0001},
	{0xc78, 0x8a600001}, {0xc78, 0x89610001},
	{0xc78, 0x88620001}, {0xc78, 0x87630001},
	{0xc78, 0x86640001}, {0xc78, 0x85650001},
	{0xc78, 0x84660001}, {0xc78, 0x83670001},
	{0xc78, 0x82680001}, {0xc78, 0x6b690001},
	{0xc78, 0x6a6a0001}, {0xc78, 0x696b0001},
	{0xc78, 0x686c0001}, {0xc78, 0x676d0001},
	{0xc78, 0x666e0001}, {0xc78, 0x656f0001},
	{0xc78, 0x64700001}, {0xc78, 0x63710001},
	{0xc78, 0x62720001}, {0xc78, 0x61730001},
	{0xc78, 0x49740001}, {0xc78, 0x48750001},
	{0xc78, 0x47760001}, {0xc78, 0x46770001},
	{0xc78, 0x45780001}, {0xc78, 0x44790001},
	{0xc78, 0x437a0001}, {0xc78, 0x427b0001},
	{0xc78, 0x417c0001}, {0xc78, 0x407d0001},
	{0xc78, 0x407e0001}, {0xc78, 0x407f0001},
	{0xc50, 0x00040022}, {0xc50, 0x00040020},
	{0xffff, 0xffffffff}
};

static struct rtl8xxxu_reg32val rtl8xxx_agc_8192eu_highpa_table[] = {
	{0xc78, 0xfa000001}, {0xc78, 0xf9010001},
	{0xc78, 0xf8020001}, {0xc78, 0xf7030001},
	{0xc78, 0xf6040001}, {0xc78, 0xf5050001},
	{0xc78, 0xf4060001}, {0xc78, 0xf3070001},
	{0xc78, 0xf2080001}, {0xc78, 0xf1090001},
	{0xc78, 0xf00a0001}, {0xc78, 0xef0b0001},
	{0xc78, 0xee0c0001}, {0xc78, 0xed0d0001},
	{0xc78, 0xec0e0001}, {0xc78, 0xeb0f0001},
	{0xc78, 0xea100001}, {0xc78, 0xe9110001},
	{0xc78, 0xe8120001}, {0xc78, 0xe7130001},
	{0xc78, 0xe6140001}, {0xc78, 0xe5150001},
	{0xc78, 0xe4160001}, {0xc78, 0xe3170001},
	{0xc78, 0xe2180001}, {0xc78, 0xe1190001},
	{0xc78, 0x8a1a0001}, {0xc78, 0x891b0001},
	{0xc78, 0x881c0001}, {0xc78, 0x871d0001},
	{0xc78, 0x861e0001}, {0xc78, 0x851f0001},
	{0xc78, 0x84200001}, {0xc78, 0x83210001},
	{0xc78, 0x82220001}, {0xc78, 0x6a230001},
	{0xc78, 0x69240001}, {0xc78, 0x68250001},
	{0xc78, 0x67260001}, {0xc78, 0x66270001},
	{0xc78, 0x65280001}, {0xc78, 0x64290001},
	{0xc78, 0x632a0001}, {0xc78, 0x622b0001},
	{0xc78, 0x612c0001}, {0xc78, 0x602d0001},
	{0xc78, 0x472e0001}, {0xc78, 0x462f0001},
	{0xc78, 0x45300001}, {0xc78, 0x44310001},
	{0xc78, 0x43320001}, {0xc78, 0x42330001},
	{0xc78, 0x41340001}, {0xc78, 0x40350001},
	{0xc78, 0x40360001}, {0xc78, 0x40370001},
	{0xc78, 0x40380001}, {0xc78, 0x40390001},
	{0xc78, 0x403a0001}, {0xc78, 0x403b0001},
	{0xc78, 0x403c0001}, {0xc78, 0x403d0001},
	{0xc78, 0x403e0001}, {0xc78, 0x403f0001},
	{0xc78, 0xfa400001}, {0xc78, 0xf9410001},
	{0xc78, 0xf8420001}, {0xc78, 0xf7430001},
	{0xc78, 0xf6440001}, {0xc78, 0xf5450001},
	{0xc78, 0xf4460001}, {0xc78, 0xf3470001},
	{0xc78, 0xf2480001}, {0xc78, 0xf1490001},
	{0xc78, 0xf04a0001}, {0xc78, 0xef4b0001},
	{0xc78, 0xee4c0001}, {0xc78, 0xed4d0001},
	{0xc78, 0xec4e0001}, {0xc78, 0xeb4f0001},
	{0xc78, 0xea500001}, {0xc78, 0xe9510001},
	{0xc78, 0xe8520001}, {0xc78, 0xe7530001},
	{0xc78, 0xe6540001}, {0xc78, 0xe5550001},
	{0xc78, 0xe4560001}, {0xc78, 0xe3570001},
	{0xc78, 0xe2580001}, {0xc78, 0xe1590001},
	{0xc78, 0x8a5a0001}, {0xc78, 0x895b0001},
	{0xc78, 0x885c0001}, {0xc78, 0x875d0001},
	{0xc78, 0x865e0001}, {0xc78, 0x855f0001},
	{0xc78, 0x84600001}, {0xc78, 0x83610001},
	{0xc78, 0x82620001}, {0xc78, 0x6a630001},
	{0xc78, 0x69640001}, {0xc78, 0x68650001},
	{0xc78, 0x67660001}, {0xc78, 0x66670001},
	{0xc78, 0x65680001}, {0xc78, 0x64690001},
	{0xc78, 0x636a0001}, {0xc78, 0x626b0001},
	{0xc78, 0x616c0001}, {0xc78, 0x606d0001},
	{0xc78, 0x476e0001}, {0xc78, 0x466f0001},
	{0xc78, 0x45700001}, {0xc78, 0x44710001},
	{0xc78, 0x43720001}, {0xc78, 0x42730001},
	{0xc78, 0x41740001}, {0xc78, 0x40750001},
	{0xc78, 0x40760001}, {0xc78, 0x40770001},
	{0xc78, 0x40780001}, {0xc78, 0x40790001},
	{0xc78, 0x407a0001}, {0xc78, 0x407b0001},
	{0xc78, 0x407c0001}, {0xc78, 0x407d0001},
	{0xc78, 0x407e0001}, {0xc78, 0x407f0001},
	{0xc50, 0x00040222}, {0xc50, 0x00040220},
	{0xffff, 0xffffffff}
};

static struct rtl8xxxu_rfregval rtl8192eu_radioa_init_table[] = {
	{0x7f, 0x00000082}, {0x81, 0x0003fc00},
	{0x00, 0x00030000}, {0x08, 0x00008400},
	{0x18, 0x00000407}, {0x19, 0x00000012},
	{0x1b, 0x00000064}, {0x1e, 0x00080009},
	{0x1f, 0x00000880}, {0x2f, 0x0001a060},
	{0x3f, 0x00000000}, {0x42, 0x000060c0},
	{0x57, 0x000d0000}, {0x58, 0x000be180},
	{0x67, 0x00001552}, {0x83, 0x00000000},
	{0xb0, 0x000ff9f1}, {0xb1, 0x00055418},
	{0xb2, 0x0008cc00}, {0xb4, 0x00043083},
	{0xb5, 0x00008166}, {0xb6, 0x0000803e},
	{0xb7, 0x0001c69f}, {0xb8, 0x0000407f},
	{0xb9, 0x00080001}, {0xba, 0x00040001},
	{0xbb, 0x00000400}, {0xbf, 0x000c0000},
	{0xc2, 0x00002400}, {0xc3, 0x00000009},
	{0xc4, 0x00040c91}, {0xc5, 0x00099999},
	{0xc6, 0x000000a3}, {0xc7, 0x00088820},
	{0xc8, 0x00076c06}, {0xc9, 0x00000000},
	{0xca, 0x00080000}, {0xdf, 0x00000180},
	{0xef, 0x000001a0}, {0x51, 0x00069545},
	{0x52, 0x0007e45e}, {0x53, 0x00000071},
	{0x56, 0x00051ff3}, {0x35, 0x000000a8},
	{0x35, 0x000001e2}, {0x35, 0x000002a8},
	{0x36, 0x00001c24}, {0x36, 0x00009c24},
	{0x36, 0x00011c24}, {0x36, 0x00019c24},
	{0x18, 0x00000c07}, {0x5a, 0x00048000},
	{0x19, 0x000739d0},
#ifdef EXT_PA_8192EU
	/* External PA or external LNA */
	{0x34, 0x0000a093}, {0x34, 0x0000908f},
	{0x34, 0x0000808c}, {0x34, 0x0000704d},
	{0x34, 0x0000604a}, {0x34, 0x00005047},
	{0x34, 0x0000400a}, {0x34, 0x00003007},
	{0x34, 0x00002004}, {0x34, 0x00001001},
	{0x34, 0x00000000},
#else
	/* Regular */
	{0x34, 0x0000add7}, {0x34, 0x00009dd4},
	{0x34, 0x00008dd1}, {0x34, 0x00007dce},
	{0x34, 0x00006dcb}, {0x34, 0x00005dc8},
	{0x34, 0x00004dc5}, {0x34, 0x000034cc},
	{0x34, 0x0000244f}, {0x34, 0x0000144c},
	{0x34, 0x00000014},
#endif
	{0x00, 0x00030159},
	{0x84, 0x00068180},
	{0x86, 0x0000014e},
	{0x87, 0x00048e00},
	{0x8e, 0x00065540},
	{0x8f, 0x00088000},
	{0xef, 0x000020a0},
#ifdef EXT_PA_8192EU
	/* External PA or external LNA */
	{0x3b, 0x000f07b0},
#else
	{0x3b, 0x000f02b0},
#endif
	{0x3b, 0x000ef7b0}, {0x3b, 0x000d4fb0},
	{0x3b, 0x000cf060}, {0x3b, 0x000b0090},
	{0x3b, 0x000a0080}, {0x3b, 0x00090080},
	{0x3b, 0x0008f780},
#ifdef EXT_PA_8192EU
	/* External PA or external LNA */
	{0x3b, 0x000787b0},
#else
	{0x3b, 0x00078730},
#endif
	{0x3b, 0x00060fb0}, {0x3b, 0x0005ffa0},
	{0x3b, 0x00040620}, {0x3b, 0x00037090},
	{0x3b, 0x00020080}, {0x3b, 0x0001f060},
	{0x3b, 0x0000ffb0}, {0xef, 0x000000a0},
	{0xfe, 0x00000000}, {0x18, 0x0000fc07},
	{0xfe, 0x00000000}, {0xfe, 0x00000000},
	{0xfe, 0x00000000}, {0xfe, 0x00000000},
	{0x1e, 0x00000001}, {0x1f, 0x00080000},
	{0x00, 0x00033e70},
	{0xff, 0xffffffff}
};

static struct rtl8xxxu_rfregval rtl8192eu_radiob_init_table[] = {
	{0x7f, 0x00000082}, {0x81, 0x0003fc00},
	{0x00, 0x00030000}, {0x08, 0x00008400},
	{0x18, 0x00000407}, {0x19, 0x00000012},
	{0x1b, 0x00000064}, {0x1e, 0x00080009},
	{0x1f, 0x00000880}, {0x2f, 0x0001a060},
	{0x3f, 0x00000000}, {0x42, 0x000060c0},
	{0x57, 0x000d0000}, {0x58, 0x000be180},
	{0x67, 0x00001552}, {0x7f, 0x00000082},
	{0x81, 0x0003f000}, {0x83, 0x00000000},
	{0xdf, 0x00000180}, {0xef, 0x000001a0},
	{0x51, 0x00069545}, {0x52, 0x0007e42e},
	{0x53, 0x00000071}, {0x56, 0x00051ff3},
	{0x35, 0x000000a8}, {0x35, 0x000001e0},
	{0x35, 0x000002a8}, {0x36, 0x00001ca8},
	{0x36, 0x00009c24}, {0x36, 0x00011c24},
	{0x36, 0x00019c24}, {0x18, 0x00000c07},
	{0x5a, 0x00048000}, {0x19, 0x000739d0},
#ifdef EXT_PA_8192EU
	/* External PA or external LNA */
	{0x34, 0x0000a093}, {0x34, 0x0000908f},
	{0x34, 0x0000808c}, {0x34, 0x0000704d},
	{0x34, 0x0000604a}, {0x34, 0x00005047},
	{0x34, 0x0000400a}, {0x34, 0x00003007},
	{0x34, 0x00002004}, {0x34, 0x00001001},
	{0x34, 0x00000000},
#else
	{0x34, 0x0000add7}, {0x34, 0x00009dd4},
	{0x34, 0x00008dd1}, {0x34, 0x00007dce},
	{0x34, 0x00006dcb}, {0x34, 0x00005dc8},
	{0x34, 0x00004dc5}, {0x34, 0x000034cc},
	{0x34, 0x0000244f}, {0x34, 0x0000144c},
	{0x34, 0x00000014},
#endif
	{0x00, 0x00030159}, {0x84, 0x00068180},
	{0x86, 0x000000ce}, {0x87, 0x00048a00},
	{0x8e, 0x00065540}, {0x8f, 0x00088000},
	{0xef, 0x000020a0},
#ifdef EXT_PA_8192EU
	/* External PA or external LNA */
	{0x3b, 0x000f07b0},
#else
	{0x3b, 0x000f02b0},
#endif

	{0x3b, 0x000ef7b0}, {0x3b, 0x000d4fb0},
	{0x3b, 0x000cf060}, {0x3b, 0x000b0090},
	{0x3b, 0x000a0080}, {0x3b, 0x00090080},
	{0x3b, 0x0008f780},
#ifdef EXT_PA_8192EU
	/* External PA or external LNA */
	{0x3b, 0x000787b0},
#else
	{0x3b, 0x00078730},
#endif
	{0x3b, 0x00060fb0}, {0x3b, 0x0005ffa0},
	{0x3b, 0x00040620}, {0x3b, 0x00037090},
	{0x3b, 0x00020080}, {0x3b, 0x0001f060},
	{0x3b, 0x0000ffb0}, {0xef, 0x000000a0},
	{0x00, 0x00010159}, {0xfe, 0x00000000},
	{0xfe, 0x00000000}, {0xfe, 0x00000000},
	{0xfe, 0x00000000}, {0x1e, 0x00000001},
	{0x1f, 0x00080000}, {0x00, 0x00033e70},
	{0xff, 0xffffffff}
};

static void
rtl8192e_set_tx_power(struct rtl8xxxu_priv *priv, int channel, bool ht40)
{
	u32 val32, ofdm, mcs;
	u8 cck, ofdmbase, mcsbase;
	int group, tx_idx;

	tx_idx = 0;
	group = rtl8xxxu_gen2_channel_to_group(channel);

	cck = priv->cck_tx_power_index_A[group];

	val32 = rtl8xxxu_read32(priv, REG_TX_AGC_A_CCK1_MCS32);
	val32 &= 0xffff00ff;
	val32 |= (cck << 8);
	rtl8xxxu_write32(priv, REG_TX_AGC_A_CCK1_MCS32, val32);

	val32 = rtl8xxxu_read32(priv, REG_TX_AGC_B_CCK11_A_CCK2_11);
	val32 &= 0xff;
	val32 |= ((cck << 8) | (cck << 16) | (cck << 24));
	rtl8xxxu_write32(priv, REG_TX_AGC_B_CCK11_A_CCK2_11, val32);

	ofdmbase = priv->ht40_1s_tx_power_index_A[group];
	ofdmbase += priv->ofdm_tx_power_diff[tx_idx].a;
	ofdm = ofdmbase | ofdmbase << 8 | ofdmbase << 16 | ofdmbase << 24;

	rtl8xxxu_write32(priv, REG_TX_AGC_A_RATE18_06, ofdm);
	rtl8xxxu_write32(priv, REG_TX_AGC_A_RATE54_24, ofdm);

	mcsbase = priv->ht40_1s_tx_power_index_A[group];
	if (ht40)
		mcsbase += priv->ht40_tx_power_diff[tx_idx++].a;
	else
		mcsbase += priv->ht20_tx_power_diff[tx_idx++].a;
	mcs = mcsbase | mcsbase << 8 | mcsbase << 16 | mcsbase << 24;

	rtl8xxxu_write32(priv, REG_TX_AGC_A_MCS03_MCS00, mcs);
	rtl8xxxu_write32(priv, REG_TX_AGC_A_MCS07_MCS04, mcs);
	rtl8xxxu_write32(priv, REG_TX_AGC_A_MCS11_MCS08, mcs);
	rtl8xxxu_write32(priv, REG_TX_AGC_A_MCS15_MCS12, mcs);

	if (priv->tx_paths > 1) {
		cck = priv->cck_tx_power_index_B[group];

		val32 = rtl8xxxu_read32(priv, REG_TX_AGC_B_CCK1_55_MCS32);
		val32 &= 0xff;
		val32 |= ((cck << 8) | (cck << 16) | (cck << 24));
		rtl8xxxu_write32(priv, REG_TX_AGC_B_CCK1_55_MCS32, val32);

		val32 = rtl8xxxu_read32(priv, REG_TX_AGC_B_CCK11_A_CCK2_11);
		val32 &= 0xffffff00;
		val32 |= cck;
		rtl8xxxu_write32(priv, REG_TX_AGC_B_CCK11_A_CCK2_11, val32);

		ofdmbase = priv->ht40_1s_tx_power_index_B[group];
		ofdmbase += priv->ofdm_tx_power_diff[tx_idx].b;
		ofdm = ofdmbase | ofdmbase << 8 |
			ofdmbase << 16 | ofdmbase << 24;

		rtl8xxxu_write32(priv, REG_TX_AGC_B_RATE18_06, ofdm);
		rtl8xxxu_write32(priv, REG_TX_AGC_B_RATE54_24, ofdm);

		mcsbase = priv->ht40_1s_tx_power_index_B[group];
		if (ht40)
			mcsbase += priv->ht40_tx_power_diff[tx_idx++].b;
		else
			mcsbase += priv->ht20_tx_power_diff[tx_idx++].b;
		mcs = mcsbase | mcsbase << 8 | mcsbase << 16 | mcsbase << 24;

		rtl8xxxu_write32(priv, REG_TX_AGC_B_MCS03_MCS00, mcs);
		rtl8xxxu_write32(priv, REG_TX_AGC_B_MCS07_MCS04, mcs);
		rtl8xxxu_write32(priv, REG_TX_AGC_B_MCS11_MCS08, mcs);
		rtl8xxxu_write32(priv, REG_TX_AGC_B_MCS15_MCS12, mcs);
	}
}

static int rtl8192eu_parse_efuse(struct rtl8xxxu_priv *priv)
{
	struct rtl8192eu_efuse *efuse = &priv->efuse_wifi.efuse8192eu;
	int i;

	if (efuse->rtl_id != cpu_to_le16(0x8129))
		return -EINVAL;

	ether_addr_copy(priv->mac_addr, efuse->mac_addr);

	memcpy(priv->cck_tx_power_index_A, efuse->tx_power_index_A.cck_base,
	       sizeof(efuse->tx_power_index_A.cck_base));
	memcpy(priv->cck_tx_power_index_B, efuse->tx_power_index_B.cck_base,
	       sizeof(efuse->tx_power_index_B.cck_base));

	memcpy(priv->ht40_1s_tx_power_index_A,
	       efuse->tx_power_index_A.ht40_base,
	       sizeof(efuse->tx_power_index_A.ht40_base));
	memcpy(priv->ht40_1s_tx_power_index_B,
	       efuse->tx_power_index_B.ht40_base,
	       sizeof(efuse->tx_power_index_B.ht40_base));

	priv->ht20_tx_power_diff[0].a =
		efuse->tx_power_index_A.ht20_ofdm_1s_diff.b;
	priv->ht20_tx_power_diff[0].b =
		efuse->tx_power_index_B.ht20_ofdm_1s_diff.b;

	priv->ht40_tx_power_diff[0].a = 0;
	priv->ht40_tx_power_diff[0].b = 0;

	for (i = 1; i < RTL8723B_TX_COUNT; i++) {
		priv->ofdm_tx_power_diff[i].a =
			efuse->tx_power_index_A.pwr_diff[i - 1].ofdm;
		priv->ofdm_tx_power_diff[i].b =
			efuse->tx_power_index_B.pwr_diff[i - 1].ofdm;

		priv->ht20_tx_power_diff[i].a =
			efuse->tx_power_index_A.pwr_diff[i - 1].ht20;
		priv->ht20_tx_power_diff[i].b =
			efuse->tx_power_index_B.pwr_diff[i - 1].ht20;

		priv->ht40_tx_power_diff[i].a =
			efuse->tx_power_index_A.pwr_diff[i - 1].ht40;
		priv->ht40_tx_power_diff[i].b =
			efuse->tx_power_index_B.pwr_diff[i - 1].ht40;
	}

	priv->has_xtalk = 1;
	priv->xtalk = priv->efuse_wifi.efuse8192eu.xtal_k & 0x3f;

	dev_info(&priv->udev->dev, "Vendor: %.7s\n", efuse->vendor_name);
	dev_info(&priv->udev->dev, "Product: %.11s\n", efuse->device_name);
	dev_info(&priv->udev->dev, "Serial: %.11s\n", efuse->serial);

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_EFUSE) {
		unsigned char *raw = priv->efuse_wifi.raw;

		dev_info(&priv->udev->dev,
			 "%s: dumping efuse (0x%02zx bytes):\n",
			 __func__, sizeof(struct rtl8192eu_efuse));
		for (i = 0; i < sizeof(struct rtl8192eu_efuse); i += 8) {
			dev_info(&priv->udev->dev, "%02x: "
				 "%02x %02x %02x %02x %02x %02x %02x %02x\n", i,
				 raw[i], raw[i + 1], raw[i + 2],
				 raw[i + 3], raw[i + 4], raw[i + 5],
				 raw[i + 6], raw[i + 7]);
		}
	}
	return 0;
}

static int rtl8192eu_load_firmware(struct rtl8xxxu_priv *priv)
{
	char *fw_name;
	int ret;

	fw_name = "rtlwifi/rtl8192eu_nic.bin";

	ret = rtl8xxxu_load_firmware(priv, fw_name);

	return ret;
}

static void rtl8192eu_init_phy_bb(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u16 val16;

	val16 = rtl8xxxu_read16(priv, REG_SYS_FUNC);
	val16 |= SYS_FUNC_BB_GLB_RSTN | SYS_FUNC_BBRSTB | SYS_FUNC_DIO_RF;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, val16);

	/* 6. 0x1f[7:0] = 0x07 */
	val8 = RF_ENABLE | RF_RSTB | RF_SDMRSTB;
	rtl8xxxu_write8(priv, REG_RF_CTRL, val8);

	val16 = rtl8xxxu_read16(priv, REG_SYS_FUNC);
	val16 |= (SYS_FUNC_USBA | SYS_FUNC_USBD | SYS_FUNC_DIO_RF |
		  SYS_FUNC_BB_GLB_RSTN | SYS_FUNC_BBRSTB);
	rtl8xxxu_write16(priv, REG_SYS_FUNC, val16);
	val8 = RF_ENABLE | RF_RSTB | RF_SDMRSTB;
	rtl8xxxu_write8(priv, REG_RF_CTRL, val8);
	rtl8xxxu_init_phy_regs(priv, rtl8192eu_phy_init_table);

	if (priv->hi_pa)
		rtl8xxxu_init_phy_regs(priv, rtl8xxx_agc_8192eu_highpa_table);
	else
		rtl8xxxu_init_phy_regs(priv, rtl8xxx_agc_8192eu_std_table);
}

static int rtl8192eu_init_phy_rf(struct rtl8xxxu_priv *priv)
{
	int ret;

	ret = rtl8xxxu_init_phy_rf(priv, rtl8192eu_radioa_init_table, RF_A);
	if (ret)
		goto exit;

	ret = rtl8xxxu_init_phy_rf(priv, rtl8192eu_radiob_init_table, RF_B);

exit:
	return ret;
}

static int rtl8192eu_iqk_path_a(struct rtl8xxxu_priv *priv)
{
	u32 reg_eac, reg_e94, reg_e9c;
	int result = 0;

	/*
	 * TX IQK
	 * PA/PAD controlled by 0x0
	 */
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x00000000);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_DF, 0x00180);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x80800000);

	/* Path A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x18008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_B, 0x38008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_A, 0x82140303);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, 0x68160000);

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

static int rtl8192eu_rx_iqk_path_a(struct rtl8xxxu_priv *priv)
{
	u32 reg_ea4, reg_eac, reg_e94, reg_e9c, val32;
	int result = 0;

	/* Leave IQK mode */
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x00);

	/* Enable path A PA in TX IQK mode */
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_WE_LUT, 0x800a0);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_RCK_OS, 0x30000);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G1, 0x0000f);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G2, 0xf117b);

	/* PA/PAD control by 0x56, and set = 0x0 */
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_DF, 0x00980);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_56, 0x51000);

	/* Enter IQK mode */
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x80800000);

	/* TX IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK, 0x01007c00);
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

	/* path-A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x18008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_B, 0x38008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_A, 0x82160c1f);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, 0x68160c1f);

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x0046a911);

	/* One shot, path A LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xfa000000);
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
	} else {
		/* PA/PAD controlled by 0x0 */
		rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x00000000);
		rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_DF, 0x180);
		goto out;
	}

	val32 = 0x80007c00 |
		(reg_e94 & 0x03ff0000) | ((reg_e9c >> 16) & 0x03ff);
	rtl8xxxu_write32(priv, REG_TX_IQK, val32);

	/* Modify RX IQK mode table */
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x00000000);

	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_WE_LUT, 0x800a0);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_RCK_OS, 0x30000);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G1, 0x0000f);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G2, 0xf7ffa);

	/* PA/PAD control by 0x56, and set = 0x0 */
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_DF, 0x00980);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_56, 0x51000);

	/* Enter IQK mode */
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x80800000);

	/* IQK setting */
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

	/* Path A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x18008c1c);
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_B, 0x38008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_A, 0x82160c1f);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, 0x28160c1f);

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x0046a891);

	/* One shot, path A LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xfa000000);
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf8000000);

	mdelay(10);

	reg_eac = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_A_2);
	reg_ea4 = rtl8xxxu_read32(priv, REG_RX_POWER_BEFORE_IQK_A_2);

	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x00000000);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_DF, 0x180);

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

static int rtl8192eu_iqk_path_b(struct rtl8xxxu_priv *priv)
{
	u32 reg_eac, reg_eb4, reg_ebc;
	int result = 0;

	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x00000000);
	rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_UNKNOWN_DF, 0x00180);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x80800000);

	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x00000000);
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x80800000);

	/* Path B IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_B, 0x18008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_B, 0x38008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_B, 0x821403e2);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_B, 0x68160000);

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x00492911);

	/* One shot, path A LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xfa000000);
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf8000000);

	mdelay(1);

	/* Check failed */
	reg_eac = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_A_2);
	reg_eb4 = rtl8xxxu_read32(priv, REG_TX_POWER_BEFORE_IQK_B);
	reg_ebc = rtl8xxxu_read32(priv, REG_TX_POWER_AFTER_IQK_B);

	if (!(reg_eac & BIT(31)) &&
	    ((reg_eb4 & 0x03ff0000) != 0x01420000) &&
	    ((reg_ebc & 0x03ff0000) != 0x00420000))
		result |= 0x01;
	else
		dev_warn(&priv->udev->dev, "%s: Path B IQK failed!\n",
			 __func__);

	return result;
}

static int rtl8192eu_rx_iqk_path_b(struct rtl8xxxu_priv *priv)
{
	u32 reg_eac, reg_eb4, reg_ebc, reg_ec4, reg_ecc, val32;
	int result = 0;

	/* Leave IQK mode */
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x00000000);

	/* Enable path A PA in TX IQK mode */
	rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_WE_LUT, 0x800a0);
	rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_RCK_OS, 0x30000);
	rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_TXPA_G1, 0x0000f);
	rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_TXPA_G2, 0xf117b);

	/* PA/PAD control by 0x56, and set = 0x0 */
	rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_UNKNOWN_DF, 0x00980);
	rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_UNKNOWN_56, 0x51000);

	/* Enter IQK mode */
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x80800000);

	/* TX IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK, 0x01007c00);
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

	/* path-A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_B, 0x18008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_B, 0x38008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_B, 0x82160c1f);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_B, 0x68160c1f);

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x0046a911);

	/* One shot, path A LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xfa000000);
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf8000000);

	mdelay(10);

	/* Check failed */
	reg_eac = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_A_2);
	reg_eb4 = rtl8xxxu_read32(priv, REG_TX_POWER_BEFORE_IQK_B);
	reg_ebc = rtl8xxxu_read32(priv, REG_TX_POWER_AFTER_IQK_B);

	if (!(reg_eac & BIT(31)) &&
	    ((reg_eb4 & 0x03ff0000) != 0x01420000) &&
	    ((reg_ebc & 0x03ff0000) != 0x00420000)) {
		result |= 0x01;
	} else {
		/*
		 * PA/PAD controlled by 0x0
		 * Vendor driver restores RF_A here which I believe is a bug
		 */
		rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x00000000);
		rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_UNKNOWN_DF, 0x180);
		goto out;
	}

	val32 = 0x80007c00 |
		(reg_eb4 & 0x03ff0000) | ((reg_ebc >> 16) & 0x03ff);
	rtl8xxxu_write32(priv, REG_TX_IQK, val32);

	/* Modify RX IQK mode table */
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x00000000);

	rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_WE_LUT, 0x800a0);
	rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_RCK_OS, 0x30000);
	rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_TXPA_G1, 0x0000f);
	rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_TXPA_G2, 0xf7ffa);

	/* PA/PAD control by 0x56, and set = 0x0 */
	rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_UNKNOWN_DF, 0x00980);
	rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_UNKNOWN_56, 0x51000);

	/* Enter IQK mode */
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x80800000);

	/* IQK setting */
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

	/* Path A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_B, 0x18008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_A, 0x82160c1f);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, 0x28160c1f);

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x0046a891);

	/* One shot, path A LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xfa000000);
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf8000000);

	mdelay(10);

	reg_eac = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_A_2);
	reg_ec4 = rtl8xxxu_read32(priv, REG_RX_POWER_BEFORE_IQK_B_2);
	reg_ecc = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_B_2);

	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x00000000);
	rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_UNKNOWN_DF, 0x180);

	if (!(reg_eac & BIT(30)) &&
	    ((reg_ec4 & 0x03ff0000) != 0x01320000) &&
	    ((reg_ecc & 0x03ff0000) != 0x00360000))
		result |= 0x02;
	else
		dev_warn(&priv->udev->dev, "%s: Path B RX IQK failed!\n",
			 __func__);

out:
	return result;
}

static void rtl8192eu_phy_iqcalibrate(struct rtl8xxxu_priv *priv,
				      int result[][8], int t)
{
	struct device *dev = &priv->udev->dev;
	u32 i, val32;
	int path_a_ok, path_b_ok;
	int retry = 2;
	const u32 adda_regs[RTL8XXXU_ADDA_REGS] = {
		REG_FPGA0_XCD_SWITCH_CTRL, REG_BLUETOOTH,
		REG_RX_WAIT_CCA, REG_TX_CCK_RFON,
		REG_TX_CCK_BBON, REG_TX_OFDM_RFON,
		REG_TX_OFDM_BBON, REG_TX_TO_RX,
		REG_TX_TO_TX, REG_RX_CCK,
		REG_RX_OFDM, REG_RX_WAIT_RIFS,
		REG_RX_TO_RX, REG_STANDBY,
		REG_SLEEP, REG_PMPD_ANAEN
	};
	const u32 iqk_mac_regs[RTL8XXXU_MAC_REGS] = {
		REG_TXPAUSE, REG_BEACON_CTRL,
		REG_BEACON_CTRL_1, REG_GPIO_MUXCFG
	};
	const u32 iqk_bb_regs[RTL8XXXU_BB_REGS] = {
		REG_OFDM0_TRX_PATH_ENABLE, REG_OFDM0_TR_MUX_PAR,
		REG_FPGA0_XCD_RF_SW_CTRL, REG_CONFIG_ANT_A, REG_CONFIG_ANT_B,
		REG_FPGA0_XAB_RF_SW_CTRL, REG_FPGA0_XA_RF_INT_OE,
		REG_FPGA0_XB_RF_INT_OE, REG_CCK0_AFE_SETTING
	};
	u8 xa_agc = rtl8xxxu_read32(priv, REG_OFDM0_XA_AGC_CORE1) & 0xff;
	u8 xb_agc = rtl8xxxu_read32(priv, REG_OFDM0_XB_AGC_CORE1) & 0xff;

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

	/* MAC settings */
	rtl8xxxu_mac_calibration(priv, iqk_mac_regs, priv->mac_backup);

	val32 = rtl8xxxu_read32(priv, REG_CCK0_AFE_SETTING);
	val32 |= 0x0f000000;
	rtl8xxxu_write32(priv, REG_CCK0_AFE_SETTING, val32);

	rtl8xxxu_write32(priv, REG_OFDM0_TRX_PATH_ENABLE, 0x03a05600);
	rtl8xxxu_write32(priv, REG_OFDM0_TR_MUX_PAR, 0x000800e4);
	rtl8xxxu_write32(priv, REG_FPGA0_XCD_RF_SW_CTRL, 0x22208200);

	val32 = rtl8xxxu_read32(priv, REG_FPGA0_XAB_RF_SW_CTRL);
	val32 |= (FPGA0_RF_PAPE | (FPGA0_RF_PAPE << FPGA0_RF_BD_CTRL_SHIFT));
	rtl8xxxu_write32(priv, REG_FPGA0_XAB_RF_SW_CTRL, val32);

	val32 = rtl8xxxu_read32(priv, REG_FPGA0_XA_RF_INT_OE);
	val32 |= BIT(10);
	rtl8xxxu_write32(priv, REG_FPGA0_XA_RF_INT_OE, val32);
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_XB_RF_INT_OE);
	val32 |= BIT(10);
	rtl8xxxu_write32(priv, REG_FPGA0_XB_RF_INT_OE, val32);

	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x80800000);
	rtl8xxxu_write32(priv, REG_TX_IQK, 0x01007c00);
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

	for (i = 0; i < retry; i++) {
		path_a_ok = rtl8192eu_iqk_path_a(priv);
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
		path_a_ok = rtl8192eu_rx_iqk_path_a(priv);
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

	if (priv->rf_paths > 1) {
		/* Path A into standby */
		rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x00000000);
		rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_AC, 0x10000);
		rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x80800000);

		/* Turn Path B ADDA on */
		rtl8xxxu_path_adda_on(priv, adda_regs, false);

		rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x80800000);
		rtl8xxxu_write32(priv, REG_TX_IQK, 0x01007c00);
		rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

		for (i = 0; i < retry; i++) {
			path_b_ok = rtl8192eu_iqk_path_b(priv);
			if (path_b_ok == 0x01) {
				val32 = rtl8xxxu_read32(priv, REG_TX_POWER_BEFORE_IQK_B);
				result[t][4] = (val32 >> 16) & 0x3ff;
				val32 = rtl8xxxu_read32(priv, REG_TX_POWER_AFTER_IQK_B);
				result[t][5] = (val32 >> 16) & 0x3ff;
				break;
			}
		}

		if (!path_b_ok)
			dev_dbg(dev, "%s: Path B IQK failed!\n", __func__);

		for (i = 0; i < retry; i++) {
			path_b_ok = rtl8192eu_rx_iqk_path_b(priv);
			if (path_b_ok == 0x03) {
				val32 = rtl8xxxu_read32(priv,
							REG_RX_POWER_BEFORE_IQK_B_2);
				result[t][6] = (val32 >> 16) & 0x3ff;
				val32 = rtl8xxxu_read32(priv,
							REG_RX_POWER_AFTER_IQK_B_2);
				result[t][7] = (val32 >> 16) & 0x3ff;
				break;
			}
		}

		if (!path_b_ok)
			dev_dbg(dev, "%s: Path B RX IQK failed!\n", __func__);
	}

	/* Back to BB mode, load original value */
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x00000000);

	if (t) {
		/* Reload ADDA power saving parameters */
		rtl8xxxu_restore_regs(priv, adda_regs, priv->adda_backup,
				      RTL8XXXU_ADDA_REGS);

		/* Reload MAC parameters */
		rtl8xxxu_restore_mac_regs(priv, iqk_mac_regs, priv->mac_backup);

		/* Reload BB parameters */
		rtl8xxxu_restore_regs(priv, iqk_bb_regs,
				      priv->bb_backup, RTL8XXXU_BB_REGS);

		/* Restore RX initial gain */
		val32 = rtl8xxxu_read32(priv, REG_OFDM0_XA_AGC_CORE1);
		val32 &= 0xffffff00;
		rtl8xxxu_write32(priv, REG_OFDM0_XA_AGC_CORE1, val32 | 0x50);
		rtl8xxxu_write32(priv, REG_OFDM0_XA_AGC_CORE1, val32 | xa_agc);

		if (priv->rf_paths > 1) {
			val32 = rtl8xxxu_read32(priv, REG_OFDM0_XB_AGC_CORE1);
			val32 &= 0xffffff00;
			rtl8xxxu_write32(priv, REG_OFDM0_XB_AGC_CORE1,
					 val32 | 0x50);
			rtl8xxxu_write32(priv, REG_OFDM0_XB_AGC_CORE1,
					 val32 | xb_agc);
		}

		/* Load 0xe30 IQC default value */
		rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x01008c00);
		rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x01008c00);
	}
}

static void rtl8192eu_phy_iq_calibrate(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	int result[4][8];	/* last is final result */
	int i, candidate;
	bool path_a_ok, path_b_ok;
	u32 reg_e94, reg_e9c, reg_ea4, reg_eac;
	u32 reg_eb4, reg_ebc, reg_ec4, reg_ecc;
	bool simu;

	memset(result, 0, sizeof(result));
	candidate = -1;

	path_a_ok = false;
	path_b_ok = false;

	for (i = 0; i < 3; i++) {
		rtl8192eu_phy_iqcalibrate(priv, result, i);

		if (i == 1) {
			simu = rtl8xxxu_gen2_simularity_compare(priv,
								result, 0, 1);
			if (simu) {
				candidate = 0;
				break;
			}
		}

		if (i == 2) {
			simu = rtl8xxxu_gen2_simularity_compare(priv,
								result, 0, 2);
			if (simu) {
				candidate = 0;
				break;
			}

			simu = rtl8xxxu_gen2_simularity_compare(priv,
								result, 1, 2);
			if (simu)
				candidate = 1;
			else
				candidate = 3;
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
			"%s: e94 =%x e9c=%x ea4=%x eac=%x eb4=%x ebc=%x ec4=%x "
			"ecc=%x\n ", __func__, reg_e94, reg_e9c,
			reg_ea4, reg_eac, reg_eb4, reg_ebc, reg_ec4, reg_ecc);
		path_a_ok = true;
		path_b_ok = true;
	} else {
		reg_e94 = reg_eb4 = priv->rege94 = priv->regeb4 = 0x100;
		reg_e9c = reg_ebc = priv->rege9c = priv->regebc = 0x0;
	}

	if (reg_e94 && candidate >= 0)
		rtl8xxxu_fill_iqk_matrix_a(priv, path_a_ok, result,
					   candidate, (reg_ea4 == 0));

	if (priv->rf_paths > 1)
		rtl8xxxu_fill_iqk_matrix_b(priv, path_b_ok, result,
					   candidate, (reg_ec4 == 0));

	rtl8xxxu_save_regs(priv, rtl8xxxu_iqk_phy_iq_bb_reg,
			   priv->bb_recovery_backup, RTL8XXXU_BB_REGS);
}

/*
 * This is needed for 8723bu as well, presumable
 */
static void rtl8192e_crystal_afe_adjust(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u32 val32;

	/*
	 * 40Mhz crystal source, MAC 0x28[2]=0
	 */
	val8 = rtl8xxxu_read8(priv, REG_AFE_PLL_CTRL);
	val8 &= 0xfb;
	rtl8xxxu_write8(priv, REG_AFE_PLL_CTRL, val8);

	val32 = rtl8xxxu_read32(priv, REG_AFE_CTRL4);
	val32 &= 0xfffffc7f;
	rtl8xxxu_write32(priv, REG_AFE_CTRL4, val32);

	/*
	 * 92e AFE parameter
	 * AFE PLL KVCO selection, MAC 0x28[6]=1
	 */
	val8 = rtl8xxxu_read8(priv, REG_AFE_PLL_CTRL);
	val8 &= 0xbf;
	rtl8xxxu_write8(priv, REG_AFE_PLL_CTRL, val8);

	/*
	 * AFE PLL KVCO selection, MAC 0x78[21]=0
	 */
	val32 = rtl8xxxu_read32(priv, REG_AFE_CTRL4);
	val32 &= 0xffdfffff;
	rtl8xxxu_write32(priv, REG_AFE_CTRL4, val32);
}

static void rtl8192e_disabled_to_emu(struct rtl8xxxu_priv *priv)
{
	u8 val8;

	/* Clear suspend enable and power down enable*/
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 &= ~(BIT(3) | BIT(4));
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);
}

static int rtl8192e_emu_to_active(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u32 val32;
	int count, ret = 0;

	/* disable HWPDN 0x04[15]=0*/
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 &= ~BIT(7);
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	/* disable SW LPS 0x04[10]= 0 */
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 &= ~BIT(2);
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	/* disable WL suspend*/
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 &= ~(BIT(3) | BIT(4));
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

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

	/* We should be able to optimize the following three entries into one */

	/* release WLON reset 0x04[16]= 1*/
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 2);
	val8 |= BIT(0);
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 2, val8);

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

exit:
	return ret;
}

static int rtl8192eu_power_on(struct rtl8xxxu_priv *priv)
{
	u16 val16;
	u32 val32;
	int ret;

	ret = 0;

	val32 = rtl8xxxu_read32(priv, REG_SYS_CFG);
	if (val32 & SYS_CFG_SPS_LDO_SEL) {
		rtl8xxxu_write8(priv, REG_LDO_SW_CTRL, 0xc3);
	} else {
		/*
		 * Raise 1.2V voltage
		 */
		val32 = rtl8xxxu_read32(priv, REG_8192E_LDOV12_CTRL);
		val32 &= 0xff0fffff;
		val32 |= 0x00500000;
		rtl8xxxu_write32(priv, REG_8192E_LDOV12_CTRL, val32);
		rtl8xxxu_write8(priv, REG_LDO_SW_CTRL, 0x83);
	}

	/*
	 * Adjust AFE before enabling PLL
	 */
	rtl8192e_crystal_afe_adjust(priv);
	rtl8192e_disabled_to_emu(priv);

	ret = rtl8192e_emu_to_active(priv);
	if (ret)
		goto exit;

	rtl8xxxu_write16(priv, REG_CR, 0x0000);

	/*
	 * Enable MAC DMA/WMAC/SCHEDULE/SEC block
	 * Set CR bit10 to enable 32k calibration.
	 */
	val16 = rtl8xxxu_read16(priv, REG_CR);
	val16 |= (CR_HCI_TXDMA_ENABLE | CR_HCI_RXDMA_ENABLE |
		  CR_TXDMA_ENABLE | CR_RXDMA_ENABLE |
		  CR_PROTOCOL_ENABLE | CR_SCHEDULE_ENABLE |
		  CR_MAC_TX_ENABLE | CR_MAC_RX_ENABLE |
		  CR_SECURITY_ENABLE | CR_CALTIMER_ENABLE);
	rtl8xxxu_write16(priv, REG_CR, val16);

exit:
	return ret;
}

static void rtl8192e_enable_rf(struct rtl8xxxu_priv *priv)
{
	u32 val32;
	u8 val8;

	val8 = rtl8xxxu_read8(priv, REG_GPIO_MUXCFG);
	val8 |= BIT(5);
	rtl8xxxu_write8(priv, REG_GPIO_MUXCFG, val8);

	/*
	 * WLAN action by PTA
	 */
	rtl8xxxu_write8(priv, REG_WLAN_ACT_CONTROL_8723B, 0x04);

	val32 = rtl8xxxu_read32(priv, REG_PWR_DATA);
	val32 |= PWR_DATA_EEPRPAD_RFE_CTRL_EN;
	rtl8xxxu_write32(priv, REG_PWR_DATA, val32);

	val32 = rtl8xxxu_read32(priv, REG_RFE_BUFFER);
	val32 |= (BIT(0) | BIT(1));
	rtl8xxxu_write32(priv, REG_RFE_BUFFER, val32);

	rtl8xxxu_write8(priv, REG_RFE_CTRL_ANTA_SRC, 0x77);

	val32 = rtl8xxxu_read32(priv, REG_LEDCFG0);
	val32 &= ~BIT(24);
	val32 |= BIT(23);
	rtl8xxxu_write32(priv, REG_LEDCFG0, val32);

	/*
	 * Fix external switch Main->S1, Aux->S0
	 */
	val8 = rtl8xxxu_read8(priv, REG_PAD_CTRL1);
	val8 &= ~BIT(0);
	rtl8xxxu_write8(priv, REG_PAD_CTRL1, val8);
}

struct rtl8xxxu_fileops rtl8192eu_fops = {
	.parse_efuse = rtl8192eu_parse_efuse,
	.load_firmware = rtl8192eu_load_firmware,
	.power_on = rtl8192eu_power_on,
	.power_off = rtl8xxxu_power_off,
	.reset_8051 = rtl8xxxu_reset_8051,
	.llt_init = rtl8xxxu_auto_llt_table,
	.init_phy_bb = rtl8192eu_init_phy_bb,
	.init_phy_rf = rtl8192eu_init_phy_rf,
	.phy_iq_calibrate = rtl8192eu_phy_iq_calibrate,
	.config_channel = rtl8xxxu_gen2_config_channel,
	.parse_rx_desc = rtl8xxxu_parse_rxdesc24,
	.enable_rf = rtl8192e_enable_rf,
	.disable_rf = rtl8xxxu_gen2_disable_rf,
	.usb_quirks = rtl8xxxu_gen2_usb_quirks,
	.set_tx_power = rtl8192e_set_tx_power,
	.update_rate_mask = rtl8xxxu_gen2_update_rate_mask,
	.report_connect = rtl8xxxu_gen2_report_connect,
	.writeN_block_size = 128,
	.tx_desc_size = sizeof(struct rtl8xxxu_txdesc40),
	.rx_desc_size = sizeof(struct rtl8xxxu_rxdesc24),
	.has_s0s1 = 0,
	.adda_1t_init = 0x0fc01616,
	.adda_1t_path_on = 0x0fc01616,
	.adda_2t_path_on_a = 0x0fc01616,
	.adda_2t_path_on_b = 0x0fc01616,
	.trxff_boundary = 0x3cff,
	.mactable = rtl8192e_mac_init_table,
	.total_page_num = TX_TOTAL_PAGE_NUM_8192E,
	.page_num_hi = TX_PAGE_NUM_HI_PQ_8192E,
	.page_num_lo = TX_PAGE_NUM_LO_PQ_8192E,
	.page_num_norm = TX_PAGE_NUM_NORM_PQ_8192E,
};
