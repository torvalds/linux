// SPDX-License-Identifier: GPL-2.0-only
/*
 * RTL8XXXU mac80211 USB driver - 8723b specific subdriver
 *
 * Copyright (c) 2014 - 2017 Jes Sorensen <Jes.Sorensen@gmail.com>
 *
 * Portions, notably calibration code:
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This driver was written as a replacement for the vendor provided
 * rtl8723au driver. As the Realtek 8xxx chips are very similar in
 * their programming interface, I have started adding support for
 * additional 8xxx chips like the 8192cu, 8188cus, etc.
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

static const struct rtl8xxxu_reg8val rtl8723b_mac_init_table[] = {
	{0x02f, 0x30}, {0x035, 0x00}, {0x039, 0x08}, {0x04e, 0xe0},
	{0x064, 0x00}, {0x067, 0x20}, {0x428, 0x0a}, {0x429, 0x10},
	{0x430, 0x00}, {0x431, 0x00},
	{0x432, 0x00}, {0x433, 0x01}, {0x434, 0x04}, {0x435, 0x05},
	{0x436, 0x07}, {0x437, 0x08}, {0x43c, 0x04}, {0x43d, 0x05},
	{0x43e, 0x07}, {0x43f, 0x08}, {0x440, 0x5d}, {0x441, 0x01},
	{0x442, 0x00}, {0x444, 0x10}, {0x445, 0x00}, {0x446, 0x00},
	{0x447, 0x00}, {0x448, 0x00}, {0x449, 0xf0}, {0x44a, 0x0f},
	{0x44b, 0x3e}, {0x44c, 0x10}, {0x44d, 0x00}, {0x44e, 0x00},
	{0x44f, 0x00}, {0x450, 0x00}, {0x451, 0xf0}, {0x452, 0x0f},
	{0x453, 0x00}, {0x456, 0x5e}, {0x460, 0x66}, {0x461, 0x66},
	{0x4c8, 0xff}, {0x4c9, 0x08}, {0x4cc, 0xff},
	{0x4cd, 0xff}, {0x4ce, 0x01}, {0x500, 0x26}, {0x501, 0xa2},
	{0x502, 0x2f}, {0x503, 0x00}, {0x504, 0x28}, {0x505, 0xa3},
	{0x506, 0x5e}, {0x507, 0x00}, {0x508, 0x2b}, {0x509, 0xa4},
	{0x50a, 0x5e}, {0x50b, 0x00}, {0x50c, 0x4f}, {0x50d, 0xa4},
	{0x50e, 0x00}, {0x50f, 0x00}, {0x512, 0x1c}, {0x514, 0x0a},
	{0x516, 0x0a}, {0x525, 0x4f},
	{0x550, 0x10}, {0x551, 0x10}, {0x559, 0x02}, {0x55c, 0x50},
	{0x55d, 0xff}, {0x605, 0x30}, {0x608, 0x0e}, {0x609, 0x2a},
	{0x620, 0xff}, {0x621, 0xff}, {0x622, 0xff}, {0x623, 0xff},
	{0x624, 0xff}, {0x625, 0xff}, {0x626, 0xff}, {0x627, 0xff},
	{0x638, 0x50}, {0x63c, 0x0a}, {0x63d, 0x0a}, {0x63e, 0x0e},
	{0x63f, 0x0e}, {0x640, 0x40}, {0x642, 0x40}, {0x643, 0x00},
	{0x652, 0xc8}, {0x66e, 0x05}, {0x700, 0x21}, {0x701, 0x43},
	{0x702, 0x65}, {0x703, 0x87}, {0x708, 0x21}, {0x709, 0x43},
	{0x70a, 0x65}, {0x70b, 0x87}, {0x765, 0x18}, {0x76e, 0x04},
	{0xffff, 0xff},
};

static const struct rtl8xxxu_reg32val rtl8723b_phy_1t_init_table[] = {
	{0x800, 0x80040000}, {0x804, 0x00000003},
	{0x808, 0x0000fc00}, {0x80c, 0x0000000a},
	{0x810, 0x10001331}, {0x814, 0x020c3d10},
	{0x818, 0x02200385}, {0x81c, 0x00000000},
	{0x820, 0x01000100}, {0x824, 0x00190204},
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
	{0xa10, 0x9500bb78}, {0xa14, 0x1114d028},
	{0xa18, 0x00881117}, {0xa1c, 0x89140f00},
	{0xa20, 0x1a1b0000}, {0xa24, 0x090e1317},
	{0xa28, 0x00000204}, {0xa2c, 0x00d30000},
	{0xa70, 0x101fbf00}, {0xa74, 0x00000007},
	{0xa78, 0x00000900}, {0xa7c, 0x225b0606},
	{0xa80, 0x21806490}, {0xb2c, 0x00000000},
	{0xc00, 0x48071d40}, {0xc04, 0x03a05611},
	{0xc08, 0x000000e4}, {0xc0c, 0x6c6c6c6c},
	{0xc10, 0x08800000}, {0xc14, 0x40000100},
	{0xc18, 0x08800000}, {0xc1c, 0x40000100},
	{0xc20, 0x00000000}, {0xc24, 0x00000000},
	{0xc28, 0x00000000}, {0xc2c, 0x00000000},
	{0xc30, 0x69e9ac44}, {0xc34, 0x469652af},
	{0xc38, 0x49795994}, {0xc3c, 0x0a97971c},
	{0xc40, 0x1f7c403f}, {0xc44, 0x000100b7},
	{0xc48, 0xec020107}, {0xc4c, 0x007f037f},
	{0xc50, 0x69553420}, {0xc54, 0x43bc0094},
	{0xc58, 0x00013149}, {0xc5c, 0x00250492},
	{0xc60, 0x00000000}, {0xc64, 0x7112848b},
	{0xc68, 0x47c00bff}, {0xc6c, 0x00000036},
	{0xc70, 0x2c7f000d}, {0xc74, 0x020610db},
	{0xc78, 0x0000001f}, {0xc7c, 0x00b91612},
	{0xc80, 0x390000e4}, {0xc84, 0x20f60000},
	{0xc88, 0x40000100}, {0xc8c, 0x20200000},
	{0xc90, 0x00020e1a}, {0xc94, 0x00000000},
	{0xc98, 0x00020e1a}, {0xc9c, 0x00007f7f},
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
	{0xd00, 0x00000740}, {0xd04, 0x40020401},
	{0xd08, 0x0000907f}, {0xd0c, 0x20010201},
	{0xd10, 0xa0633333}, {0xd14, 0x3333bc53},
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
	{0xe5c, 0x28160d05}, {0xe60, 0x00000008},
	{0xe68, 0x001b2556}, {0xe6c, 0x00c00096},
	{0xe70, 0x00c00096}, {0xe74, 0x01000056},
	{0xe78, 0x01000014}, {0xe7c, 0x01000056},
	{0xe80, 0x01000014}, {0xe84, 0x00c00096},
	{0xe88, 0x01000056}, {0xe8c, 0x00c00096},
	{0xed0, 0x00c00096}, {0xed4, 0x00c00096},
	{0xed8, 0x00c00096}, {0xedc, 0x000000d6},
	{0xee0, 0x000000d6}, {0xeec, 0x01c00016},
	{0xf14, 0x00000003}, {0xf4c, 0x00000000},
	{0xf00, 0x00000300},
	{0x820, 0x01000100}, {0x800, 0x83040000},
	{0xffff, 0xffffffff},
};

static const struct rtl8xxxu_reg32val rtl8xxx_agc_8723bu_table[] = {
	{0xc78, 0xfd000001}, {0xc78, 0xfc010001},
	{0xc78, 0xfb020001}, {0xc78, 0xfa030001},
	{0xc78, 0xf9040001}, {0xc78, 0xf8050001},
	{0xc78, 0xf7060001}, {0xc78, 0xf6070001},
	{0xc78, 0xf5080001}, {0xc78, 0xf4090001},
	{0xc78, 0xf30a0001}, {0xc78, 0xf20b0001},
	{0xc78, 0xf10c0001}, {0xc78, 0xf00d0001},
	{0xc78, 0xef0e0001}, {0xc78, 0xee0f0001},
	{0xc78, 0xed100001}, {0xc78, 0xec110001},
	{0xc78, 0xeb120001}, {0xc78, 0xea130001},
	{0xc78, 0xe9140001}, {0xc78, 0xe8150001},
	{0xc78, 0xe7160001}, {0xc78, 0xe6170001},
	{0xc78, 0xe5180001}, {0xc78, 0xe4190001},
	{0xc78, 0xe31a0001}, {0xc78, 0xa51b0001},
	{0xc78, 0xa41c0001}, {0xc78, 0xa31d0001},
	{0xc78, 0x671e0001}, {0xc78, 0x661f0001},
	{0xc78, 0x65200001}, {0xc78, 0x64210001},
	{0xc78, 0x63220001}, {0xc78, 0x4a230001},
	{0xc78, 0x49240001}, {0xc78, 0x48250001},
	{0xc78, 0x47260001}, {0xc78, 0x46270001},
	{0xc78, 0x45280001}, {0xc78, 0x44290001},
	{0xc78, 0x432a0001}, {0xc78, 0x422b0001},
	{0xc78, 0x292c0001}, {0xc78, 0x282d0001},
	{0xc78, 0x272e0001}, {0xc78, 0x262f0001},
	{0xc78, 0x0a300001}, {0xc78, 0x09310001},
	{0xc78, 0x08320001}, {0xc78, 0x07330001},
	{0xc78, 0x06340001}, {0xc78, 0x05350001},
	{0xc78, 0x04360001}, {0xc78, 0x03370001},
	{0xc78, 0x02380001}, {0xc78, 0x01390001},
	{0xc78, 0x013a0001}, {0xc78, 0x013b0001},
	{0xc78, 0x013c0001}, {0xc78, 0x013d0001},
	{0xc78, 0x013e0001}, {0xc78, 0x013f0001},
	{0xc78, 0xfc400001}, {0xc78, 0xfb410001},
	{0xc78, 0xfa420001}, {0xc78, 0xf9430001},
	{0xc78, 0xf8440001}, {0xc78, 0xf7450001},
	{0xc78, 0xf6460001}, {0xc78, 0xf5470001},
	{0xc78, 0xf4480001}, {0xc78, 0xf3490001},
	{0xc78, 0xf24a0001}, {0xc78, 0xf14b0001},
	{0xc78, 0xf04c0001}, {0xc78, 0xef4d0001},
	{0xc78, 0xee4e0001}, {0xc78, 0xed4f0001},
	{0xc78, 0xec500001}, {0xc78, 0xeb510001},
	{0xc78, 0xea520001}, {0xc78, 0xe9530001},
	{0xc78, 0xe8540001}, {0xc78, 0xe7550001},
	{0xc78, 0xe6560001}, {0xc78, 0xe5570001},
	{0xc78, 0xe4580001}, {0xc78, 0xe3590001},
	{0xc78, 0xa65a0001}, {0xc78, 0xa55b0001},
	{0xc78, 0xa45c0001}, {0xc78, 0xa35d0001},
	{0xc78, 0x675e0001}, {0xc78, 0x665f0001},
	{0xc78, 0x65600001}, {0xc78, 0x64610001},
	{0xc78, 0x63620001}, {0xc78, 0x62630001},
	{0xc78, 0x61640001}, {0xc78, 0x48650001},
	{0xc78, 0x47660001}, {0xc78, 0x46670001},
	{0xc78, 0x45680001}, {0xc78, 0x44690001},
	{0xc78, 0x436a0001}, {0xc78, 0x426b0001},
	{0xc78, 0x286c0001}, {0xc78, 0x276d0001},
	{0xc78, 0x266e0001}, {0xc78, 0x256f0001},
	{0xc78, 0x24700001}, {0xc78, 0x09710001},
	{0xc78, 0x08720001}, {0xc78, 0x07730001},
	{0xc78, 0x06740001}, {0xc78, 0x05750001},
	{0xc78, 0x04760001}, {0xc78, 0x03770001},
	{0xc78, 0x02780001}, {0xc78, 0x01790001},
	{0xc78, 0x017a0001}, {0xc78, 0x017b0001},
	{0xc78, 0x017c0001}, {0xc78, 0x017d0001},
	{0xc78, 0x017e0001}, {0xc78, 0x017f0001},
	{0xc50, 0x69553422},
	{0xc50, 0x69553420},
	{0x824, 0x00390204},
	{0xffff, 0xffffffff}
};

static const struct rtl8xxxu_rfregval rtl8723bu_radioa_1t_init_table[] = {
	{0x00, 0x00010000}, {0xb0, 0x000dffe0},
	{0xfe, 0x00000000}, {0xfe, 0x00000000},
	{0xfe, 0x00000000}, {0xb1, 0x00000018},
	{0xfe, 0x00000000}, {0xfe, 0x00000000},
	{0xfe, 0x00000000}, {0xb2, 0x00084c00},
	{0xb5, 0x0000d2cc}, {0xb6, 0x000925aa},
	{0xb7, 0x00000010}, {0xb8, 0x0000907f},
	{0x5c, 0x00000002}, {0x7c, 0x00000002},
	{0x7e, 0x00000005}, {0x8b, 0x0006fc00},
	{0xb0, 0x000ff9f0}, {0x1c, 0x000739d2},
	{0x1e, 0x00000000}, {0xdf, 0x00000780},
	{0x50, 0x00067435},
	/*
	 * The 8723bu vendor driver indicates that bit 8 should be set in
	 * 0x51 for package types TFBGA90, TFBGA80, and TFBGA79. However
	 * they never actually check the package type - and just default
	 * to not setting it.
	 */
	{0x51, 0x0006b04e},
	{0x52, 0x000007d2}, {0x53, 0x00000000},
	{0x54, 0x00050400}, {0x55, 0x0004026e},
	{0xdd, 0x0000004c}, {0x70, 0x00067435},
	/*
	 * 0x71 has same package type condition as for register 0x51
	 */
	{0x71, 0x0006b04e},
	{0x72, 0x000007d2}, {0x73, 0x00000000},
	{0x74, 0x00050400}, {0x75, 0x0004026e},
	{0xef, 0x00000100}, {0x34, 0x0000add7},
	{0x35, 0x00005c00}, {0x34, 0x00009dd4},
	{0x35, 0x00005000}, {0x34, 0x00008dd1},
	{0x35, 0x00004400}, {0x34, 0x00007dce},
	{0x35, 0x00003800}, {0x34, 0x00006cd1},
	{0x35, 0x00004400}, {0x34, 0x00005cce},
	{0x35, 0x00003800}, {0x34, 0x000048ce},
	{0x35, 0x00004400}, {0x34, 0x000034ce},
	{0x35, 0x00003800}, {0x34, 0x00002451},
	{0x35, 0x00004400}, {0x34, 0x0000144e},
	{0x35, 0x00003800}, {0x34, 0x00000051},
	{0x35, 0x00004400}, {0xef, 0x00000000},
	{0xef, 0x00000100}, {0xed, 0x00000010},
	{0x44, 0x0000add7}, {0x44, 0x00009dd4},
	{0x44, 0x00008dd1}, {0x44, 0x00007dce},
	{0x44, 0x00006cc1}, {0x44, 0x00005cce},
	{0x44, 0x000044d1}, {0x44, 0x000034ce},
	{0x44, 0x00002451}, {0x44, 0x0000144e},
	{0x44, 0x00000051}, {0xef, 0x00000000},
	{0xed, 0x00000000}, {0x7f, 0x00020080},
	{0xef, 0x00002000}, {0x3b, 0x000380ef},
	{0x3b, 0x000302fe}, {0x3b, 0x00028ce6},
	{0x3b, 0x000200bc}, {0x3b, 0x000188a5},
	{0x3b, 0x00010fbc}, {0x3b, 0x00008f71},
	{0x3b, 0x00000900}, {0xef, 0x00000000},
	{0xed, 0x00000001}, {0x40, 0x000380ef},
	{0x40, 0x000302fe}, {0x40, 0x00028ce6},
	{0x40, 0x000200bc}, {0x40, 0x000188a5},
	{0x40, 0x00010fbc}, {0x40, 0x00008f71},
	{0x40, 0x00000900}, {0xed, 0x00000000},
	{0x82, 0x00080000}, {0x83, 0x00008000},
	{0x84, 0x00048d80}, {0x85, 0x00068000},
	{0xa2, 0x00080000}, {0xa3, 0x00008000},
	{0xa4, 0x00048d80}, {0xa5, 0x00068000},
	{0xed, 0x00000002}, {0xef, 0x00000002},
	{0x56, 0x00000032}, {0x76, 0x00000032},
	{0x01, 0x00000780},
	{0xff, 0xffffffff}
};

static int rtl8723bu_identify_chip(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	u32 val32, sys_cfg, vendor;
	int ret = 0;

	sys_cfg = rtl8xxxu_read32(priv, REG_SYS_CFG);
	priv->chip_cut = u32_get_bits(sys_cfg, SYS_CFG_CHIP_VERSION_MASK);
	if (sys_cfg & SYS_CFG_TRP_VAUX_EN) {
		dev_info(dev, "Unsupported test chip\n");
		ret = -ENOTSUPP;
		goto out;
	}

	strscpy(priv->chip_name, "8723BU", sizeof(priv->chip_name));
	priv->rtl_chip = RTL8723B;
	priv->rf_paths = 1;
	priv->rx_paths = 1;
	priv->tx_paths = 1;

	val32 = rtl8xxxu_read32(priv, REG_MULTI_FUNC_CTRL);
	if (val32 & MULTI_WIFI_FUNC_EN)
		priv->has_wifi = 1;
	if (val32 & MULTI_BT_FUNC_EN)
		priv->has_bluetooth = 1;
	if (val32 & MULTI_GPS_FUNC_EN)
		priv->has_gps = 1;
	priv->is_multi_func = 1;

	vendor = sys_cfg & SYS_CFG_VENDOR_EXT_MASK;
	rtl8xxxu_identify_vendor_2bits(priv, vendor);

	val32 = rtl8xxxu_read32(priv, REG_GPIO_OUTSTS);
	priv->rom_rev = u32_get_bits(val32, GPIO_RF_RL_ID);

	rtl8xxxu_config_endpoints_sie(priv);

	/*
	 * Fallback for devices that do not provide REG_NORMAL_SIE_EP_TX
	 */
	if (!priv->ep_tx_count)
		ret = rtl8xxxu_config_endpoints_no_sie(priv);

out:
	return ret;
}

static void rtl8723bu_write_btreg(struct rtl8xxxu_priv *priv, u8 reg, u8 data)
{
	struct h2c_cmd h2c;
	int reqnum = 0;

	memset(&h2c, 0, sizeof(struct h2c_cmd));
	h2c.bt_mp_oper.cmd = H2C_8723B_BT_MP_OPER;
	h2c.bt_mp_oper.operreq = 0 | (reqnum << 4);
	h2c.bt_mp_oper.opcode = BT_MP_OP_WRITE_REG_VALUE;
	h2c.bt_mp_oper.data = data;
	rtl8xxxu_gen2_h2c_cmd(priv, &h2c, sizeof(h2c.bt_mp_oper));

	reqnum++;
	memset(&h2c, 0, sizeof(struct h2c_cmd));
	h2c.bt_mp_oper.cmd = H2C_8723B_BT_MP_OPER;
	h2c.bt_mp_oper.operreq = 0 | (reqnum << 4);
	h2c.bt_mp_oper.opcode = BT_MP_OP_WRITE_REG_VALUE;
	h2c.bt_mp_oper.addr = reg;
	rtl8xxxu_gen2_h2c_cmd(priv, &h2c, sizeof(h2c.bt_mp_oper));
}

static void rtl8723bu_reset_8051(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u16 sys_func;

	val8 = rtl8xxxu_read8(priv, REG_RSV_CTRL);
	val8 &= ~BIT(1);
	rtl8xxxu_write8(priv, REG_RSV_CTRL, val8);

	val8 = rtl8xxxu_read8(priv, REG_RSV_CTRL + 1);
	val8 &= ~BIT(0);
	rtl8xxxu_write8(priv, REG_RSV_CTRL + 1, val8);

	sys_func = rtl8xxxu_read16(priv, REG_SYS_FUNC);
	sys_func &= ~SYS_FUNC_CPU_ENABLE;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, sys_func);

	val8 = rtl8xxxu_read8(priv, REG_RSV_CTRL);
	val8 &= ~BIT(1);
	rtl8xxxu_write8(priv, REG_RSV_CTRL, val8);

	val8 = rtl8xxxu_read8(priv, REG_RSV_CTRL + 1);
	val8 |= BIT(0);
	rtl8xxxu_write8(priv, REG_RSV_CTRL + 1, val8);

	sys_func |= SYS_FUNC_CPU_ENABLE;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, sys_func);
}

static void
rtl8723b_set_tx_power(struct rtl8xxxu_priv *priv, int channel, bool ht40)
{
	u32 val32, ofdm, mcs;
	u8 cck, ofdmbase, mcsbase;
	int group, tx_idx;

	tx_idx = 0;
	group = rtl8xxxu_gen2_channel_to_group(channel);

	cck = priv->cck_tx_power_index_B[group];
	val32 = rtl8xxxu_read32(priv, REG_TX_AGC_A_CCK1_MCS32);
	val32 &= 0xffff00ff;
	val32 |= (cck << 8);
	rtl8xxxu_write32(priv, REG_TX_AGC_A_CCK1_MCS32, val32);

	val32 = rtl8xxxu_read32(priv, REG_TX_AGC_B_CCK11_A_CCK2_11);
	val32 &= 0xff;
	val32 |= ((cck << 8) | (cck << 16) | (cck << 24));
	rtl8xxxu_write32(priv, REG_TX_AGC_B_CCK11_A_CCK2_11, val32);

	ofdmbase = priv->ht40_1s_tx_power_index_B[group];
	ofdmbase += priv->ofdm_tx_power_diff[tx_idx].b;
	ofdm = ofdmbase | ofdmbase << 8 | ofdmbase << 16 | ofdmbase << 24;

	rtl8xxxu_write32(priv, REG_TX_AGC_A_RATE18_06, ofdm);
	rtl8xxxu_write32(priv, REG_TX_AGC_A_RATE54_24, ofdm);

	mcsbase = priv->ht40_1s_tx_power_index_B[group];
	if (ht40)
		mcsbase += priv->ht40_tx_power_diff[tx_idx++].b;
	else
		mcsbase += priv->ht20_tx_power_diff[tx_idx++].b;
	mcs = mcsbase | mcsbase << 8 | mcsbase << 16 | mcsbase << 24;

	rtl8xxxu_write32(priv, REG_TX_AGC_A_MCS03_MCS00, mcs);
	rtl8xxxu_write32(priv, REG_TX_AGC_A_MCS07_MCS04, mcs);
}

static int rtl8723bu_parse_efuse(struct rtl8xxxu_priv *priv)
{
	struct rtl8723bu_efuse *efuse = &priv->efuse_wifi.efuse8723bu;
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

	priv->ofdm_tx_power_diff[0].a =
		efuse->tx_power_index_A.ht20_ofdm_1s_diff.a;
	priv->ofdm_tx_power_diff[0].b =
		efuse->tx_power_index_B.ht20_ofdm_1s_diff.a;

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

	priv->default_crystal_cap = priv->efuse_wifi.efuse8723bu.xtal_k & 0x3f;

	dev_info(&priv->udev->dev, "Vendor: %.7s\n", efuse->vendor_name);
	dev_info(&priv->udev->dev, "Product: %.41s\n", efuse->device_name);

	return 0;
}

static int rtl8723bu_load_firmware(struct rtl8xxxu_priv *priv)
{
	const char *fw_name;
	int ret;

	if (priv->enable_bluetooth)
		fw_name = "rtlwifi/rtl8723bu_bt.bin";
	else
		fw_name = "rtlwifi/rtl8723bu_nic.bin";

	ret = rtl8xxxu_load_firmware(priv, fw_name);
	return ret;
}

static void rtl8723bu_init_phy_bb(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u16 val16;

	val16 = rtl8xxxu_read16(priv, REG_SYS_FUNC);
	val16 |= SYS_FUNC_BB_GLB_RSTN | SYS_FUNC_BBRSTB | SYS_FUNC_DIO_RF;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, val16);

	rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, 0x00);

	/* 6. 0x1f[7:0] = 0x07 */
	val8 = RF_ENABLE | RF_RSTB | RF_SDMRSTB;
	rtl8xxxu_write8(priv, REG_RF_CTRL, val8);

	/* Why? */
	rtl8xxxu_write8(priv, REG_SYS_FUNC, 0xe3);
	rtl8xxxu_write8(priv, REG_AFE_XTAL_CTRL + 1, 0x80);
	rtl8xxxu_init_phy_regs(priv, rtl8723b_phy_1t_init_table);

	rtl8xxxu_init_phy_regs(priv, rtl8xxx_agc_8723bu_table);
}

static int rtl8723bu_init_phy_rf(struct rtl8xxxu_priv *priv)
{
	int ret;

	ret = rtl8xxxu_init_phy_rf(priv, rtl8723bu_radioa_1t_init_table, RF_A);
	/*
	 * PHY LCK
	 */
	rtl8xxxu_write_rfreg(priv, RF_A, 0xb0, 0xdfbe0);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_MODE_AG, 0x8c01);
	msleep(200);
	rtl8xxxu_write_rfreg(priv, RF_A, 0xb0, 0xdffe0);

	return ret;
}

void rtl8723bu_phy_init_antenna_selection(struct rtl8xxxu_priv *priv)
{
	u32 val32;

	val32 = rtl8xxxu_read32(priv, REG_PAD_CTRL1);
	val32 &= ~(BIT(20) | BIT(24));
	rtl8xxxu_write32(priv, REG_PAD_CTRL1, val32);

	val32 = rtl8xxxu_read32(priv, REG_GPIO_MUXCFG);
	val32 &= ~BIT(4);
	rtl8xxxu_write32(priv, REG_GPIO_MUXCFG, val32);

	val32 = rtl8xxxu_read32(priv, REG_GPIO_MUXCFG);
	val32 |= BIT(3);
	rtl8xxxu_write32(priv, REG_GPIO_MUXCFG, val32);

	val32 = rtl8xxxu_read32(priv, REG_LEDCFG0);
	val32 |= BIT(24);
	rtl8xxxu_write32(priv, REG_LEDCFG0, val32);

	val32 = rtl8xxxu_read32(priv, REG_LEDCFG0);
	val32 &= ~BIT(23);
	rtl8xxxu_write32(priv, REG_LEDCFG0, val32);

	val32 = rtl8xxxu_read32(priv, REG_RFE_BUFFER);
	val32 |= (BIT(0) | BIT(1));
	rtl8xxxu_write32(priv, REG_RFE_BUFFER, val32);

	val32 = rtl8xxxu_read32(priv, REG_RFE_CTRL_ANTA_SRC);
	val32 &= 0xffffff00;
	val32 |= 0x77;
	rtl8xxxu_write32(priv, REG_RFE_CTRL_ANTA_SRC, val32);

	val32 = rtl8xxxu_read32(priv, REG_PWR_DATA);
	val32 |= PWR_DATA_EEPRPAD_RFE_CTRL_EN;
	rtl8xxxu_write32(priv, REG_PWR_DATA, val32);
}

static int rtl8723bu_iqk_path_a(struct rtl8xxxu_priv *priv)
{
	u32 reg_eac, reg_e94, reg_e9c, path_sel, val32;
	int result = 0;

	path_sel = rtl8xxxu_read32(priv, REG_S0S1_PATH_SWITCH);

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
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G1, 0x0003f);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G2, 0xc7f87);

	/*
	 * Tx IQK setting
	 */
	rtl8xxxu_write32(priv, REG_TX_IQK, 0x01007c00);
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

	/* path-A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x18008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_B, 0x38008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_A, 0x821403ea);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, 0x28110000);
	rtl8xxxu_write32(priv, REG_TX_IQK_PI_B, 0x82110000);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_B, 0x28110000);

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x00462911);

	/*
	 * Enter IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	val32 |= 0x80800000;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/*
	 * The vendor driver indicates the USB module is always using
	 * S0S1 path 1 for the 8723bu. This may be different for 8192eu
	 */
	if (priv->rf_paths > 1)
		rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, 0x00000000);
	else
		rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, 0x00000280);

	/*
	 * Bit 12 seems to be BT_GRANT, and is only found in the 8723bu.
	 * No trace of this in the 8192eu or 8188eu vendor drivers.
	 */
	rtl8xxxu_write32(priv, REG_BT_CONTROL_8723BU, 0x00000800);

	/* One shot, path A LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf9000000);
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf8000000);

	mdelay(1);

	/* Restore Ant Path */
	rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, path_sel);
#ifdef RTL8723BU_BT
	/* GNT_BT = 1 */
	rtl8xxxu_write32(priv, REG_BT_CONTROL_8723BU, 0x00001800);
#endif

	/*
	 * Leave IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/* Check failed */
	reg_eac = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_A_2);
	reg_e94 = rtl8xxxu_read32(priv, REG_TX_POWER_BEFORE_IQK_A);
	reg_e9c = rtl8xxxu_read32(priv, REG_TX_POWER_AFTER_IQK_A);

	val32 = (reg_e9c >> 16) & 0x3ff;
	if (val32 & 0x200)
		val32 = 0x400 - val32;

	if (!(reg_eac & BIT(28)) &&
	    ((reg_e94 & 0x03ff0000) != 0x01420000) &&
	    ((reg_e9c & 0x03ff0000) != 0x00420000) &&
	    ((reg_e94 & 0x03ff0000)  < 0x01100000) &&
	    ((reg_e94 & 0x03ff0000)  > 0x00f00000) &&
	    val32 < 0xf)
		result |= 0x01;
	else	/* If TX not OK, ignore RX */
		goto out;

out:
	return result;
}

static int rtl8723bu_rx_iqk_path_a(struct rtl8xxxu_priv *priv)
{
	u32 reg_ea4, reg_eac, reg_e94, reg_e9c, path_sel, val32;
	int result = 0;

	path_sel = rtl8xxxu_read32(priv, REG_S0S1_PATH_SWITCH);

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
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G1, 0x0001f);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G2, 0xf7fb7);

	/*
	 * Tx IQK setting
	 */
	rtl8xxxu_write32(priv, REG_TX_IQK, 0x01007c00);
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

	/* path-A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x18008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_B, 0x38008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_A, 0x82160ff0);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, 0x28110000);
	rtl8xxxu_write32(priv, REG_TX_IQK_PI_B, 0x82110000);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_B, 0x28110000);

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x0046a911);

	/*
	 * Enter IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	val32 |= 0x80800000;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/*
	 * The vendor driver indicates the USB module is always using
	 * S0S1 path 1 for the 8723bu. This may be different for 8192eu
	 */
	if (priv->rf_paths > 1)
		rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, 0x00000000);
	else
		rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, 0x00000280);

	/*
	 * Bit 12 seems to be BT_GRANT, and is only found in the 8723bu.
	 * No trace of this in the 8192eu or 8188eu vendor drivers.
	 */
	rtl8xxxu_write32(priv, REG_BT_CONTROL_8723BU, 0x00000800);

	/* One shot, path A LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf9000000);
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf8000000);

	mdelay(1);

	/* Restore Ant Path */
	rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, path_sel);
#ifdef RTL8723BU_BT
	/* GNT_BT = 1 */
	rtl8xxxu_write32(priv, REG_BT_CONTROL_8723BU, 0x00001800);
#endif

	/*
	 * Leave IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/* Check failed */
	reg_eac = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_A_2);
	reg_e94 = rtl8xxxu_read32(priv, REG_TX_POWER_BEFORE_IQK_A);
	reg_e9c = rtl8xxxu_read32(priv, REG_TX_POWER_AFTER_IQK_A);

	val32 = (reg_e9c >> 16) & 0x3ff;
	if (val32 & 0x200)
		val32 = 0x400 - val32;

	if (!(reg_eac & BIT(28)) &&
	    ((reg_e94 & 0x03ff0000) != 0x01420000) &&
	    ((reg_e9c & 0x03ff0000) != 0x00420000) &&
	    ((reg_e94 & 0x03ff0000)  < 0x01100000) &&
	    ((reg_e94 & 0x03ff0000)  > 0x00f00000) &&
	    val32 < 0xf)
		result |= 0x01;
	else	/* If TX not OK, ignore RX */
		goto out;

	val32 = 0x80007c00 | (reg_e94 &0x3ff0000) |
		((reg_e9c & 0x3ff0000) >> 16);
	rtl8xxxu_write32(priv, REG_TX_IQK, val32);

	/*
	 * Modify RX IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);
	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_WE_LUT);
	val32 |= 0x80000;
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_WE_LUT, val32);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_RCK_OS, 0x30000);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G1, 0x0001f);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G2, 0xf7d77);

	/*
	 * PA, PAD setting
	 */
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_DF, 0xf80);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_55, 0x4021f);

	/*
	 * RX IQK setting
	 */
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

	/* path-A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x18008c1c);
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_B, 0x38008c1c);

	rtl8xxxu_write32(priv, REG_TX_IQK_PI_A, 0x82110000);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, 0x2816001f);
	rtl8xxxu_write32(priv, REG_TX_IQK_PI_B, 0x82110000);
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_B, 0x28110000);

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x0046a8d1);

	/*
	 * Enter IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	val32 |= 0x80800000;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	if (priv->rf_paths > 1)
		rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, 0x00000000);
	else
		rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, 0x00000280);

	/*
	 * Disable BT
	 */
	rtl8xxxu_write32(priv, REG_BT_CONTROL_8723BU, 0x00000800);

	/* One shot, path A LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf9000000);
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf8000000);

	mdelay(1);

	/* Restore Ant Path */
	rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, path_sel);
#ifdef RTL8723BU_BT
	/* GNT_BT = 1 */
	rtl8xxxu_write32(priv, REG_BT_CONTROL_8723BU, 0x00001800);
#endif

	/*
	 * Leave IQK mode
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	/* Check failed */
	reg_eac = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_A_2);
	reg_ea4 = rtl8xxxu_read32(priv, REG_RX_POWER_BEFORE_IQK_A_2);

	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_DF, 0x780);

	val32 = (reg_eac >> 16) & 0x3ff;
	if (val32 & 0x200)
		val32 = 0x400 - val32;

	if (!(reg_eac & BIT(27)) &&
	    ((reg_ea4 & 0x03ff0000) != 0x01320000) &&
	    ((reg_eac & 0x03ff0000) != 0x00360000) &&
	    ((reg_ea4 & 0x03ff0000)  < 0x01100000) &&
	    ((reg_ea4 & 0x03ff0000)  > 0x00f00000) &&
	    val32 < 0xf)
		result |= 0x02;
	else	/* If TX not OK, ignore RX */
		goto out;
out:
	return result;
}

static void rtl8723bu_phy_iqcalibrate(struct rtl8xxxu_priv *priv,
				      int result[][8], int t)
{
	struct device *dev = &priv->udev->dev;
	u32 i, val32;
	int path_a_ok /*, path_b_ok */;
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
	rtl8xxxu_write32(priv, REG_FPGA0_XCD_RF_SW_CTRL, 0x22204000);

	/*
	 * RX IQ calibration setting for 8723B D cut large current issue
	 * when leaving IPS
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_WE_LUT);
	val32 |= 0x80000;
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_WE_LUT, val32);

	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_RCK_OS, 0x30000);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G1, 0x0001f);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G2, 0xf7fb7);

	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_ED);
	val32 |= 0x20;
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_ED, val32);

	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_43, 0x60fbd);

	for (i = 0; i < retry; i++) {
		path_a_ok = rtl8723bu_iqk_path_a(priv);
		if (path_a_ok == 0x01) {
			val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
			val32 &= 0x000000ff;
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

	if (!path_a_ok)
		dev_dbg(dev, "%s: Path A TX IQK failed!\n", __func__);

	for (i = 0; i < retry; i++) {
		path_a_ok = rtl8723bu_rx_iqk_path_a(priv);
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

	if (priv->tx_paths > 1) {
#if 1
		dev_warn(dev, "%s: Path B not supported\n", __func__);
#else

		/*
		 * Path A into standby
		 */
		val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
		val32 &= 0x000000ff;
		rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);
		rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_AC, 0x10000);

		val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
		val32 &= 0x000000ff;
		val32 |= 0x80800000;
		rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

		/* Turn Path B ADDA on */
		rtl8xxxu_path_adda_on(priv, adda_regs, false);

		for (i = 0; i < retry; i++) {
			path_b_ok = rtl8xxxu_iqk_path_b(priv);
			if (path_b_ok == 0x03) {
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
			path_b_ok = rtl8723bu_rx_iqk_path_b(priv);
			if (path_a_ok == 0x03) {
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
#endif
	}

	/* Back to BB mode, load original value */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 &= 0x000000ff;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);

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

		if (priv->tx_paths > 1) {
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

static void rtl8723bu_phy_iq_calibrate(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	int result[4][8];	/* last is final result */
	int i, candidate;
	bool path_a_ok, path_b_ok;
	u32 reg_e94, reg_e9c, reg_ea4, reg_eac;
	u32 reg_eb4, reg_ebc, reg_ec4, reg_ecc;
	u32 val32, bt_control;
	s32 reg_tmp = 0;
	bool simu;

	rtl8xxxu_gen2_prepare_calibrate(priv, 1);

	memset(result, 0, sizeof(result));
	candidate = -1;

	path_a_ok = false;
	path_b_ok = false;

	bt_control = rtl8xxxu_read32(priv, REG_BT_CONTROL_8723BU);

	for (i = 0; i < 3; i++) {
		rtl8723bu_phy_iqcalibrate(priv, result, i);

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
		path_b_ok = true;
	} else {
		reg_e94 = reg_eb4 = priv->rege94 = priv->regeb4 = 0x100;
		reg_e9c = reg_ebc = priv->rege9c = priv->regebc = 0x0;
	}

	if (reg_e94 && candidate >= 0)
		rtl8xxxu_fill_iqk_matrix_a(priv, path_a_ok, result,
					   candidate, (reg_ea4 == 0));

	if (priv->tx_paths > 1 && reg_eb4)
		rtl8xxxu_fill_iqk_matrix_b(priv, path_b_ok, result,
					   candidate, (reg_ec4 == 0));

	rtl8xxxu_save_regs(priv, rtl8xxxu_iqk_phy_iq_bb_reg,
			   priv->bb_recovery_backup, RTL8XXXU_BB_REGS);

	rtl8xxxu_write32(priv, REG_BT_CONTROL_8723BU, bt_control);

	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_WE_LUT);
	val32 |= 0x80000;
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_WE_LUT, val32);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_RCK_OS, 0x18000);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G1, 0x0001f);
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_TXPA_G2, 0xe6177);
	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_ED);
	val32 |= 0x20;
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_UNKNOWN_ED, val32);
	rtl8xxxu_write_rfreg(priv, RF_A, 0x43, 0x300bd);

	if (priv->rf_paths > 1)
		dev_dbg(dev, "%s: 8723BU 2T not supported\n", __func__);

	rtl8xxxu_gen2_prepare_calibrate(priv, 0);
}

static int rtl8723bu_active_to_emu(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u16 val16;
	u32 val32;
	int count, ret = 0;

	/* Turn off RF */
	rtl8xxxu_write8(priv, REG_RF_CTRL, 0);

	/* Enable rising edge triggering interrupt */
	val16 = rtl8xxxu_read16(priv, REG_GPIO_INTM);
	val16 &= ~GPIO_INTM_EDGE_TRIG_IRQ;
	rtl8xxxu_write16(priv, REG_GPIO_INTM, val16);

	/* Release WLON reset 0x04[16]= 1*/
	val32 = rtl8xxxu_read32(priv, REG_APS_FSMCO);
	val32 |= APS_FSMCO_WLON_RESET;
	rtl8xxxu_write32(priv, REG_APS_FSMCO, val32);

	/* 0x0005[1] = 1 turn off MAC by HW state machine*/
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 |= BIT(1);
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	for (count = RTL8XXXU_MAX_REG_POLL; count; count--) {
		val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
		if ((val8 & BIT(1)) == 0)
			break;
		udelay(10);
	}

	if (!count) {
		dev_warn(&priv->udev->dev, "%s: Disabling MAC timed out\n",
			 __func__);
		ret = -EBUSY;
		goto exit;
	}

	/* Enable BT control XTAL setting */
	val8 = rtl8xxxu_read8(priv, REG_AFE_MISC);
	val8 &= ~AFE_MISC_WL_XTAL_CTRL;
	rtl8xxxu_write8(priv, REG_AFE_MISC, val8);

	/* 0x0000[5] = 1 analog Ips to digital, 1:isolation */
	val8 = rtl8xxxu_read8(priv, REG_SYS_ISO_CTRL);
	val8 |= SYS_ISO_ANALOG_IPS;
	rtl8xxxu_write8(priv, REG_SYS_ISO_CTRL, val8);

	/* 0x0020[0] = 0 disable LDOA12 MACRO block*/
	val8 = rtl8xxxu_read8(priv, REG_LDOA15_CTRL);
	val8 &= ~LDOA15_ENABLE;
	rtl8xxxu_write8(priv, REG_LDOA15_CTRL, val8);

exit:
	return ret;
}

static int rtl8723b_emu_to_active(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u32 val32;
	int count, ret = 0;

	/* 0x20[0] = 1 enable LDOA12 MACRO block for all interface */
	val8 = rtl8xxxu_read8(priv, REG_LDOA15_CTRL);
	val8 |= LDOA15_ENABLE;
	rtl8xxxu_write8(priv, REG_LDOA15_CTRL, val8);

	/* 0x67[0] = 0 to disable BT_GPS_SEL pins*/
	val8 = rtl8xxxu_read8(priv, 0x0067);
	val8 &= ~BIT(4);
	rtl8xxxu_write8(priv, 0x0067, val8);

	mdelay(1);

	/* 0x00[5] = 0 release analog Ips to digital, 1:isolation */
	val8 = rtl8xxxu_read8(priv, REG_SYS_ISO_CTRL);
	val8 &= ~SYS_ISO_ANALOG_IPS;
	rtl8xxxu_write8(priv, REG_SYS_ISO_CTRL, val8);

	/* Disable SW LPS 0x04[10]= 0 */
	val32 = rtl8xxxu_read8(priv, REG_APS_FSMCO);
	val32 &= ~APS_FSMCO_SW_LPS;
	rtl8xxxu_write32(priv, REG_APS_FSMCO, val32);

	/* Wait until 0x04[17] = 1 power ready */
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

	/* Release WLON reset 0x04[16]= 1*/
	val32 = rtl8xxxu_read32(priv, REG_APS_FSMCO);
	val32 |= APS_FSMCO_WLON_RESET;
	rtl8xxxu_write32(priv, REG_APS_FSMCO, val32);

	/* Disable HWPDN 0x04[15]= 0*/
	val32 = rtl8xxxu_read32(priv, REG_APS_FSMCO);
	val32 &= ~APS_FSMCO_HW_POWERDOWN;
	rtl8xxxu_write32(priv, REG_APS_FSMCO, val32);

	/* Disable WL suspend*/
	val32 = rtl8xxxu_read32(priv, REG_APS_FSMCO);
	val32 &= ~(APS_FSMCO_HW_SUSPEND | APS_FSMCO_PCIE);
	rtl8xxxu_write32(priv, REG_APS_FSMCO, val32);

	/* Set, then poll until 0 */
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

	/* Enable WL control XTAL setting */
	val8 = rtl8xxxu_read8(priv, REG_AFE_MISC);
	val8 |= AFE_MISC_WL_XTAL_CTRL;
	rtl8xxxu_write8(priv, REG_AFE_MISC, val8);

	/* Enable falling edge triggering interrupt */
	val8 = rtl8xxxu_read8(priv, REG_GPIO_INTM + 1);
	val8 |= BIT(1);
	rtl8xxxu_write8(priv, REG_GPIO_INTM + 1, val8);

	/* Enable GPIO9 interrupt mode */
	val8 = rtl8xxxu_read8(priv, REG_GPIO_IO_SEL_2 + 1);
	val8 |= BIT(1);
	rtl8xxxu_write8(priv, REG_GPIO_IO_SEL_2 + 1, val8);

	/* Enable GPIO9 input mode */
	val8 = rtl8xxxu_read8(priv, REG_GPIO_IO_SEL_2);
	val8 &= ~BIT(1);
	rtl8xxxu_write8(priv, REG_GPIO_IO_SEL_2, val8);

	/* Enable HSISR GPIO[C:0] interrupt */
	val8 = rtl8xxxu_read8(priv, REG_HSIMR);
	val8 |= BIT(0);
	rtl8xxxu_write8(priv, REG_HSIMR, val8);

	/* Enable HSISR GPIO9 interrupt */
	val8 = rtl8xxxu_read8(priv, REG_HSIMR + 2);
	val8 |= BIT(1);
	rtl8xxxu_write8(priv, REG_HSIMR + 2, val8);

	val8 = rtl8xxxu_read8(priv, REG_MULTI_FUNC_CTRL);
	val8 |= MULTI_WIFI_HW_ROF_EN;
	rtl8xxxu_write8(priv, REG_MULTI_FUNC_CTRL, val8);

	/* For GPIO9 internal pull high setting BIT(14) */
	val8 = rtl8xxxu_read8(priv, REG_MULTI_FUNC_CTRL + 1);
	val8 |= BIT(6);
	rtl8xxxu_write8(priv, REG_MULTI_FUNC_CTRL + 1, val8);

exit:
	return ret;
}

static int rtl8723bu_power_on(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u16 val16;
	u32 val32;
	int ret;

	rtl8xxxu_disabled_to_emu(priv);

	ret = rtl8723b_emu_to_active(priv);
	if (ret)
		goto exit;

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

	/*
	 * BT coexist power on settings. This is identical for 1 and 2
	 * antenna parts.
	 */
	rtl8xxxu_write8(priv, REG_PAD_CTRL1 + 3, 0x20);

	val16 = rtl8xxxu_read16(priv, REG_SYS_FUNC);
	val16 |= SYS_FUNC_BBRSTB | SYS_FUNC_BB_GLB_RSTN;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, val16);

	rtl8xxxu_write8(priv, REG_BT_CONTROL_8723BU + 1, 0x18);
	rtl8xxxu_write8(priv, REG_WLAN_ACT_CONTROL_8723B, 0x04);
	rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, 0x00);
	/* Antenna inverse */
	rtl8xxxu_write8(priv, 0xfe08, 0x01);

	val16 = rtl8xxxu_read16(priv, REG_PWR_DATA);
	val16 |= PWR_DATA_EEPRPAD_RFE_CTRL_EN;
	rtl8xxxu_write16(priv, REG_PWR_DATA, val16);

	val32 = rtl8xxxu_read32(priv, REG_LEDCFG0);
	val32 |= LEDCFG0_DPDT_SELECT;
	rtl8xxxu_write32(priv, REG_LEDCFG0, val32);

	val8 = rtl8xxxu_read8(priv, REG_PAD_CTRL1);
	val8 &= ~PAD_CTRL1_SW_DPDT_SEL_DATA;
	rtl8xxxu_write8(priv, REG_PAD_CTRL1, val8);
exit:
	return ret;
}

static void rtl8723bu_power_off(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u16 val16;

	rtl8xxxu_flush_fifo(priv);

	/*
	 * Disable TX report timer
	 */
	val8 = rtl8xxxu_read8(priv, REG_TX_REPORT_CTRL);
	val8 &= ~TX_REPORT_CTRL_TIMER_ENABLE;
	rtl8xxxu_write8(priv, REG_TX_REPORT_CTRL, val8);

	rtl8xxxu_write8(priv, REG_CR, 0x0000);

	rtl8xxxu_active_to_lps(priv);

	/* Reset Firmware if running in RAM */
	if (rtl8xxxu_read8(priv, REG_MCU_FW_DL) & MCU_FW_RAM_SEL)
		rtl8xxxu_firmware_self_reset(priv);

	/* Reset MCU */
	val16 = rtl8xxxu_read16(priv, REG_SYS_FUNC);
	val16 &= ~SYS_FUNC_CPU_ENABLE;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, val16);

	/* Reset MCU ready status */
	rtl8xxxu_write8(priv, REG_MCU_FW_DL, 0x00);

	rtl8723bu_active_to_emu(priv);

	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 |= BIT(3); /* APS_FSMCO_HW_SUSPEND */
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	/* 0x48[16] = 1 to enable GPIO9 as EXT wakeup */
	val8 = rtl8xxxu_read8(priv, REG_GPIO_INTM + 2);
	val8 |= BIT(0);
	rtl8xxxu_write8(priv, REG_GPIO_INTM + 2, val8);
}

static void rtl8723b_enable_rf(struct rtl8xxxu_priv *priv)
{
	struct h2c_cmd h2c;
	u32 val32;
	u8 val8;

	val32 = rtl8xxxu_read32(priv, REG_RX_WAIT_CCA);
	val32 |= (BIT(22) | BIT(23));
	rtl8xxxu_write32(priv, REG_RX_WAIT_CCA, val32);

	/*
	 * No indication anywhere as to what 0x0790 does. The 2 antenna
	 * vendor code preserves bits 6-7 here.
	 */
	rtl8xxxu_write8(priv, 0x0790, 0x05);
	/*
	 * 0x0778 seems to be related to enabling the number of antennas
	 * In the vendor driver halbtc8723b2ant_InitHwConfig() sets it
	 * to 0x03, while halbtc8723b1ant_InitHwConfig() sets it to 0x01
	 */
	rtl8xxxu_write8(priv, 0x0778, 0x01);

	val8 = rtl8xxxu_read8(priv, REG_GPIO_MUXCFG);
	val8 |= BIT(5);
	rtl8xxxu_write8(priv, REG_GPIO_MUXCFG, val8);

	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_IQADJ_G1, 0x780);

	rtl8723bu_write_btreg(priv, 0x3c, 0x15); /* BT TRx Mask on */

	/*
	 * Set BT grant to low
	 */
	memset(&h2c, 0, sizeof(struct h2c_cmd));
	h2c.bt_grant.cmd = H2C_8723B_BT_GRANT;
	h2c.bt_grant.data = 0;
	rtl8xxxu_gen2_h2c_cmd(priv, &h2c, sizeof(h2c.bt_grant));

	/*
	 * WLAN action by PTA
	 */
	rtl8xxxu_write8(priv, REG_WLAN_ACT_CONTROL_8723B, 0x0c);

	/*
	 * BT select S0/S1 controlled by WiFi
	 */
	val8 = rtl8xxxu_read8(priv, 0x0067);
	val8 |= BIT(5);
	rtl8xxxu_write8(priv, 0x0067, val8);

	val32 = rtl8xxxu_read32(priv, REG_PWR_DATA);
	val32 |= PWR_DATA_EEPRPAD_RFE_CTRL_EN;
	rtl8xxxu_write32(priv, REG_PWR_DATA, val32);

	/*
	 * Bits 6/7 are marked in/out ... but for what?
	 */
	rtl8xxxu_write8(priv, 0x0974, 0xff);

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

	memset(&h2c, 0, sizeof(struct h2c_cmd));
	h2c.ant_sel_rsv.cmd = H2C_8723B_ANT_SEL_RSV;
	h2c.ant_sel_rsv.ant_inverse = 1;
	h2c.ant_sel_rsv.int_switch_type = 0;
	rtl8xxxu_gen2_h2c_cmd(priv, &h2c, sizeof(h2c.ant_sel_rsv));

	/*
	 * Different settings per different antenna position.
	 *      Antenna Position:   | Normal   Inverse
	 * --------------------------------------------------
	 * Antenna switch to BT:    |  0x280,   0x00
	 * Antenna switch to WiFi:  |  0x0,     0x280
	 * Antenna switch to PTA:   |  0x200,   0x80
	 */
	rtl8xxxu_write32(priv, REG_S0S1_PATH_SWITCH, 0x80);

	/*
	 * Software control, antenna at WiFi side
	 */
	rtl8723bu_set_ps_tdma(priv, 0x08, 0x00, 0x00, 0x00, 0x00);

	rtl8xxxu_write32(priv, REG_BT_COEX_TABLE1, 0x55555555);
	rtl8xxxu_write32(priv, REG_BT_COEX_TABLE2, 0x55555555);
	rtl8xxxu_write32(priv, REG_BT_COEX_TABLE3, 0x00ffffff);
	rtl8xxxu_write8(priv, REG_BT_COEX_TABLE4, 0x03);

	memset(&h2c, 0, sizeof(struct h2c_cmd));
	h2c.bt_info.cmd = H2C_8723B_BT_INFO;
	h2c.bt_info.data = BIT(0);
	rtl8xxxu_gen2_h2c_cmd(priv, &h2c, sizeof(h2c.bt_info));

	memset(&h2c, 0, sizeof(struct h2c_cmd));
	h2c.ignore_wlan.cmd = H2C_8723B_BT_IGNORE_WLANACT;
	h2c.ignore_wlan.data = 0;
	rtl8xxxu_gen2_h2c_cmd(priv, &h2c, sizeof(h2c.ignore_wlan));
}

static void rtl8723bu_init_aggregation(struct rtl8xxxu_priv *priv)
{
	u32 agg_rx;
	u8 agg_ctrl;

	/*
	 * For now simply disable RX aggregation
	 */
	agg_ctrl = rtl8xxxu_read8(priv, REG_TRXDMA_CTRL);
	agg_ctrl &= ~TRXDMA_CTRL_RXDMA_AGG_EN;

	agg_rx = rtl8xxxu_read32(priv, REG_RXDMA_AGG_PG_TH);
	agg_rx &= ~RXDMA_USB_AGG_ENABLE;
	agg_rx &= ~0xff0f;

	rtl8xxxu_write8(priv, REG_TRXDMA_CTRL, agg_ctrl);
	rtl8xxxu_write32(priv, REG_RXDMA_AGG_PG_TH, agg_rx);
}

static void rtl8723bu_init_statistics(struct rtl8xxxu_priv *priv)
{
	u32 val32;

	/* Time duration for NHM unit: 4us, 0x2710=40ms */
	rtl8xxxu_write16(priv, REG_NHM_TIMER_8723B + 2, 0x2710);
	rtl8xxxu_write16(priv, REG_NHM_TH9_TH10_8723B + 2, 0xffff);
	rtl8xxxu_write32(priv, REG_NHM_TH3_TO_TH0_8723B, 0xffffff52);
	rtl8xxxu_write32(priv, REG_NHM_TH7_TO_TH4_8723B, 0xffffffff);
	/* TH8 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_IQK);
	val32 |= 0xff;
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, val32);
	/* Enable CCK */
	val32 = rtl8xxxu_read32(priv, REG_NHM_TH9_TH10_8723B);
	val32 |= BIT(8) | BIT(9) | BIT(10);
	rtl8xxxu_write32(priv, REG_NHM_TH9_TH10_8723B, val32);
	/* Max power amongst all RX antennas */
	val32 = rtl8xxxu_read32(priv, REG_OFDM0_FA_RSTC);
	val32 |= BIT(7);
	rtl8xxxu_write32(priv, REG_OFDM0_FA_RSTC, val32);
}

static s8 rtl8723b_cck_rssi(struct rtl8xxxu_priv *priv, u8 cck_agc_rpt)
{
	s8 rx_pwr_all = 0x00;
	u8 vga_idx, lna_idx;

	lna_idx = u8_get_bits(cck_agc_rpt, CCK_AGC_RPT_LNA_IDX_MASK);
	vga_idx = u8_get_bits(cck_agc_rpt, CCK_AGC_RPT_VGA_IDX_MASK);

	switch (lna_idx) {
	case 6:
		rx_pwr_all = -34 - (2 * vga_idx);
		break;
	case 4:
		rx_pwr_all = -14 - (2 * vga_idx);
		break;
	case 1:
		rx_pwr_all = 6 - (2 * vga_idx);
		break;
	case 0:
		rx_pwr_all = 16 - (2 * vga_idx);
		break;
	default:
		break;
	}

	return rx_pwr_all;
}

struct rtl8xxxu_fileops rtl8723bu_fops = {
	.identify_chip = rtl8723bu_identify_chip,
	.parse_efuse = rtl8723bu_parse_efuse,
	.load_firmware = rtl8723bu_load_firmware,
	.power_on = rtl8723bu_power_on,
	.power_off = rtl8723bu_power_off,
	.reset_8051 = rtl8723bu_reset_8051,
	.llt_init = rtl8xxxu_auto_llt_table,
	.init_phy_bb = rtl8723bu_init_phy_bb,
	.init_phy_rf = rtl8723bu_init_phy_rf,
	.phy_init_antenna_selection = rtl8723bu_phy_init_antenna_selection,
	.phy_lc_calibrate = rtl8723a_phy_lc_calibrate,
	.phy_iq_calibrate = rtl8723bu_phy_iq_calibrate,
	.config_channel = rtl8xxxu_gen2_config_channel,
	.parse_rx_desc = rtl8xxxu_parse_rxdesc24,
	.init_aggregation = rtl8723bu_init_aggregation,
	.init_statistics = rtl8723bu_init_statistics,
	.init_burst = rtl8xxxu_init_burst,
	.enable_rf = rtl8723b_enable_rf,
	.disable_rf = rtl8xxxu_gen2_disable_rf,
	.usb_quirks = rtl8xxxu_gen2_usb_quirks,
	.set_tx_power = rtl8723b_set_tx_power,
	.update_rate_mask = rtl8xxxu_gen2_update_rate_mask,
	.report_connect = rtl8xxxu_gen2_report_connect,
	.report_rssi = rtl8xxxu_gen2_report_rssi,
	.fill_txdesc = rtl8xxxu_fill_txdesc_v2,
	.set_crystal_cap = rtl8723a_set_crystal_cap,
	.cck_rssi = rtl8723b_cck_rssi,
	.writeN_block_size = 1024,
	.tx_desc_size = sizeof(struct rtl8xxxu_txdesc40),
	.rx_desc_size = sizeof(struct rtl8xxxu_rxdesc24),
	.has_s0s1 = 1,
	.has_tx_report = 1,
	.gen2_thermal_meter = 1,
	.needs_full_init = 1,
	.adda_1t_init = 0x01c00014,
	.adda_1t_path_on = 0x01c00014,
	.adda_2t_path_on_a = 0x01c00014,
	.adda_2t_path_on_b = 0x01c00014,
	.trxff_boundary = 0x3f7f,
	.pbp_rx = PBP_PAGE_SIZE_256,
	.pbp_tx = PBP_PAGE_SIZE_256,
	.mactable = rtl8723b_mac_init_table,
	.total_page_num = TX_TOTAL_PAGE_NUM_8723B,
	.page_num_hi = TX_PAGE_NUM_HI_PQ_8723B,
	.page_num_lo = TX_PAGE_NUM_LO_PQ_8723B,
	.page_num_norm = TX_PAGE_NUM_NORM_PQ_8723B,
};
