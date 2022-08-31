// SPDX-License-Identifier: GPL-2.0-only
/*
 * RTL8XXXU mac80211 USB driver
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

#define DRIVER_NAME "rtl8xxxu"

int rtl8xxxu_debug = RTL8XXXU_DEBUG_EFUSE;
static bool rtl8xxxu_ht40_2g;
static bool rtl8xxxu_dma_aggregation;
static int rtl8xxxu_dma_agg_timeout = -1;
static int rtl8xxxu_dma_agg_pages = -1;

MODULE_AUTHOR("Jes Sorensen <Jes.Sorensen@gmail.com>");
MODULE_DESCRIPTION("RTL8XXXu USB mac80211 Wireless LAN Driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("rtlwifi/rtl8723aufw_A.bin");
MODULE_FIRMWARE("rtlwifi/rtl8723aufw_B.bin");
MODULE_FIRMWARE("rtlwifi/rtl8723aufw_B_NoBT.bin");
MODULE_FIRMWARE("rtlwifi/rtl8192cufw_A.bin");
MODULE_FIRMWARE("rtlwifi/rtl8192cufw_B.bin");
MODULE_FIRMWARE("rtlwifi/rtl8192cufw_TMSC.bin");
MODULE_FIRMWARE("rtlwifi/rtl8192eu_nic.bin");
MODULE_FIRMWARE("rtlwifi/rtl8723bu_nic.bin");
MODULE_FIRMWARE("rtlwifi/rtl8723bu_bt.bin");

module_param_named(debug, rtl8xxxu_debug, int, 0600);
MODULE_PARM_DESC(debug, "Set debug mask");
module_param_named(ht40_2g, rtl8xxxu_ht40_2g, bool, 0600);
MODULE_PARM_DESC(ht40_2g, "Enable HT40 support on the 2.4GHz band");
module_param_named(dma_aggregation, rtl8xxxu_dma_aggregation, bool, 0600);
MODULE_PARM_DESC(dma_aggregation, "Enable DMA packet aggregation");
module_param_named(dma_agg_timeout, rtl8xxxu_dma_agg_timeout, int, 0600);
MODULE_PARM_DESC(dma_agg_timeout, "Set DMA aggregation timeout (range 1-127)");
module_param_named(dma_agg_pages, rtl8xxxu_dma_agg_pages, int, 0600);
MODULE_PARM_DESC(dma_agg_pages, "Set DMA aggregation pages (range 1-127, 0 to disable)");

#define USB_VENDOR_ID_REALTEK		0x0bda
#define RTL8XXXU_RX_URBS		32
#define RTL8XXXU_RX_URB_PENDING_WATER	8
#define RTL8XXXU_TX_URBS		64
#define RTL8XXXU_TX_URB_LOW_WATER	25
#define RTL8XXXU_TX_URB_HIGH_WATER	32

static int rtl8xxxu_submit_rx_urb(struct rtl8xxxu_priv *priv,
				  struct rtl8xxxu_rx_urb *rx_urb);

static struct ieee80211_rate rtl8xxxu_rates[] = {
	{ .bitrate = 10, .hw_value = DESC_RATE_1M, .flags = 0 },
	{ .bitrate = 20, .hw_value = DESC_RATE_2M, .flags = 0 },
	{ .bitrate = 55, .hw_value = DESC_RATE_5_5M, .flags = 0 },
	{ .bitrate = 110, .hw_value = DESC_RATE_11M, .flags = 0 },
	{ .bitrate = 60, .hw_value = DESC_RATE_6M, .flags = 0 },
	{ .bitrate = 90, .hw_value = DESC_RATE_9M, .flags = 0 },
	{ .bitrate = 120, .hw_value = DESC_RATE_12M, .flags = 0 },
	{ .bitrate = 180, .hw_value = DESC_RATE_18M, .flags = 0 },
	{ .bitrate = 240, .hw_value = DESC_RATE_24M, .flags = 0 },
	{ .bitrate = 360, .hw_value = DESC_RATE_36M, .flags = 0 },
	{ .bitrate = 480, .hw_value = DESC_RATE_48M, .flags = 0 },
	{ .bitrate = 540, .hw_value = DESC_RATE_54M, .flags = 0 },
};

static struct ieee80211_channel rtl8xxxu_channels_2g[] = {
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2412,
	  .hw_value = 1, .max_power = 30 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2417,
	  .hw_value = 2, .max_power = 30 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2422,
	  .hw_value = 3, .max_power = 30 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2427,
	  .hw_value = 4, .max_power = 30 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2432,
	  .hw_value = 5, .max_power = 30 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2437,
	  .hw_value = 6, .max_power = 30 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2442,
	  .hw_value = 7, .max_power = 30 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2447,
	  .hw_value = 8, .max_power = 30 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2452,
	  .hw_value = 9, .max_power = 30 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2457,
	  .hw_value = 10, .max_power = 30 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2462,
	  .hw_value = 11, .max_power = 30 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2467,
	  .hw_value = 12, .max_power = 30 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2472,
	  .hw_value = 13, .max_power = 30 },
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2484,
	  .hw_value = 14, .max_power = 30 }
};

static struct ieee80211_supported_band rtl8xxxu_supported_band = {
	.channels = rtl8xxxu_channels_2g,
	.n_channels = ARRAY_SIZE(rtl8xxxu_channels_2g),
	.bitrates = rtl8xxxu_rates,
	.n_bitrates = ARRAY_SIZE(rtl8xxxu_rates),
};

struct rtl8xxxu_reg8val rtl8xxxu_gen1_mac_init_table[] = {
	{0x420, 0x80}, {0x423, 0x00}, {0x430, 0x00}, {0x431, 0x00},
	{0x432, 0x00}, {0x433, 0x01}, {0x434, 0x04}, {0x435, 0x05},
	{0x436, 0x06}, {0x437, 0x07}, {0x438, 0x00}, {0x439, 0x00},
	{0x43a, 0x00}, {0x43b, 0x01}, {0x43c, 0x04}, {0x43d, 0x05},
	{0x43e, 0x06}, {0x43f, 0x07}, {0x440, 0x5d}, {0x441, 0x01},
	{0x442, 0x00}, {0x444, 0x15}, {0x445, 0xf0}, {0x446, 0x0f},
	{0x447, 0x00}, {0x458, 0x41}, {0x459, 0xa8}, {0x45a, 0x72},
	{0x45b, 0xb9}, {0x460, 0x66}, {0x461, 0x66}, {0x462, 0x08},
	{0x463, 0x03}, {0x4c8, 0xff}, {0x4c9, 0x08}, {0x4cc, 0xff},
	{0x4cd, 0xff}, {0x4ce, 0x01}, {0x500, 0x26}, {0x501, 0xa2},
	{0x502, 0x2f}, {0x503, 0x00}, {0x504, 0x28}, {0x505, 0xa3},
	{0x506, 0x5e}, {0x507, 0x00}, {0x508, 0x2b}, {0x509, 0xa4},
	{0x50a, 0x5e}, {0x50b, 0x00}, {0x50c, 0x4f}, {0x50d, 0xa4},
	{0x50e, 0x00}, {0x50f, 0x00}, {0x512, 0x1c}, {0x514, 0x0a},
	{0x515, 0x10}, {0x516, 0x0a}, {0x517, 0x10}, {0x51a, 0x16},
	{0x524, 0x0f}, {0x525, 0x4f}, {0x546, 0x40}, {0x547, 0x00},
	{0x550, 0x10}, {0x551, 0x10}, {0x559, 0x02}, {0x55a, 0x02},
	{0x55d, 0xff}, {0x605, 0x30}, {0x608, 0x0e}, {0x609, 0x2a},
	{0x652, 0x20}, {0x63c, 0x0a}, {0x63d, 0x0a}, {0x63e, 0x0e},
	{0x63f, 0x0e}, {0x66e, 0x05}, {0x700, 0x21}, {0x701, 0x43},
	{0x702, 0x65}, {0x703, 0x87}, {0x708, 0x21}, {0x709, 0x43},
	{0x70a, 0x65}, {0x70b, 0x87}, {0xffff, 0xff},
};

static struct rtl8xxxu_reg32val rtl8723a_phy_1t_init_table[] = {
	{0x800, 0x80040000}, {0x804, 0x00000003},
	{0x808, 0x0000fc00}, {0x80c, 0x0000000a},
	{0x810, 0x10001331}, {0x814, 0x020c3d10},
	{0x818, 0x02200385}, {0x81c, 0x00000000},
	{0x820, 0x01000100}, {0x824, 0x00390004},
	{0x828, 0x00000000}, {0x82c, 0x00000000},
	{0x830, 0x00000000}, {0x834, 0x00000000},
	{0x838, 0x00000000}, {0x83c, 0x00000000},
	{0x840, 0x00010000}, {0x844, 0x00000000},
	{0x848, 0x00000000}, {0x84c, 0x00000000},
	{0x850, 0x00000000}, {0x854, 0x00000000},
	{0x858, 0x569a569a}, {0x85c, 0x001b25a4},
	{0x860, 0x66f60110}, {0x864, 0x061f0130},
	{0x868, 0x00000000}, {0x86c, 0x32323200},
	{0x870, 0x07000760}, {0x874, 0x22004000},
	{0x878, 0x00000808}, {0x87c, 0x00000000},
	{0x880, 0xc0083070}, {0x884, 0x000004d5},
	{0x888, 0x00000000}, {0x88c, 0xccc000c0},
	{0x890, 0x00000800}, {0x894, 0xfffffffe},
	{0x898, 0x40302010}, {0x89c, 0x00706050},
	{0x900, 0x00000000}, {0x904, 0x00000023},
	{0x908, 0x00000000}, {0x90c, 0x81121111},
	{0xa00, 0x00d047c8}, {0xa04, 0x80ff000c},
	{0xa08, 0x8c838300}, {0xa0c, 0x2e68120f},
	{0xa10, 0x9500bb78}, {0xa14, 0x11144028},
	{0xa18, 0x00881117}, {0xa1c, 0x89140f00},
	{0xa20, 0x1a1b0000}, {0xa24, 0x090e1317},
	{0xa28, 0x00000204}, {0xa2c, 0x00d30000},
	{0xa70, 0x101fbf00}, {0xa74, 0x00000007},
	{0xa78, 0x00000900},
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
	{0xc50, 0x69543420}, {0xc54, 0x43bc0094},
	{0xc58, 0x69543420}, {0xc5c, 0x433c0094},
	{0xc60, 0x00000000}, {0xc64, 0x7112848b},
	{0xc68, 0x47c00bff}, {0xc6c, 0x00000036},
	{0xc70, 0x2c7f000d}, {0xc74, 0x018610db},
	{0xc78, 0x0000001f}, {0xc7c, 0x00b91612},
	{0xc80, 0x40000100}, {0xc84, 0x20f60000},
	{0xc88, 0x40000100}, {0xc8c, 0x20200000},
	{0xc90, 0x00121820}, {0xc94, 0x00000000},
	{0xc98, 0x00121820}, {0xc9c, 0x00007f7f},
	{0xca0, 0x00000000}, {0xca4, 0x00000080},
	{0xca8, 0x00000000}, {0xcac, 0x00000000},
	{0xcb0, 0x00000000}, {0xcb4, 0x00000000},
	{0xcb8, 0x00000000}, {0xcbc, 0x28000000},
	{0xcc0, 0x00000000}, {0xcc4, 0x00000000},
	{0xcc8, 0x00000000}, {0xccc, 0x00000000},
	{0xcd0, 0x00000000}, {0xcd4, 0x00000000},
	{0xcd8, 0x64b22427}, {0xcdc, 0x00766932},
	{0xce0, 0x00222222}, {0xce4, 0x00000000},
	{0xce8, 0x37644302}, {0xcec, 0x2f97d40c},
	{0xd00, 0x00080740}, {0xd04, 0x00020401},
	{0xd08, 0x0000907f}, {0xd0c, 0x20010201},
	{0xd10, 0xa0633333}, {0xd14, 0x3333bc43},
	{0xd18, 0x7a8f5b6b}, {0xd2c, 0xcc979975},
	{0xd30, 0x00000000}, {0xd34, 0x80608000},
	{0xd38, 0x00000000}, {0xd3c, 0x00027293},
	{0xd40, 0x00000000}, {0xd44, 0x00000000},
	{0xd48, 0x00000000}, {0xd4c, 0x00000000},
	{0xd50, 0x6437140a}, {0xd54, 0x00000000},
	{0xd58, 0x00000000}, {0xd5c, 0x30032064},
	{0xd60, 0x4653de68}, {0xd64, 0x04518a3c},
	{0xd68, 0x00002101}, {0xd6c, 0x2a201c16},
	{0xd70, 0x1812362e}, {0xd74, 0x322c2220},
	{0xd78, 0x000e3c24}, {0xe00, 0x2a2a2a2a},
	{0xe04, 0x2a2a2a2a}, {0xe08, 0x03902a2a},
	{0xe10, 0x2a2a2a2a}, {0xe14, 0x2a2a2a2a},
	{0xe18, 0x2a2a2a2a}, {0xe1c, 0x2a2a2a2a},
	{0xe28, 0x00000000}, {0xe30, 0x1000dc1f},
	{0xe34, 0x10008c1f}, {0xe38, 0x02140102},
	{0xe3c, 0x681604c2}, {0xe40, 0x01007c00},
	{0xe44, 0x01004800}, {0xe48, 0xfb000000},
	{0xe4c, 0x000028d1}, {0xe50, 0x1000dc1f},
	{0xe54, 0x10008c1f}, {0xe58, 0x02140102},
	{0xe5c, 0x28160d05}, {0xe60, 0x00000008},
	{0xe68, 0x001b25a4}, {0xe6c, 0x631b25a0},
	{0xe70, 0x631b25a0}, {0xe74, 0x081b25a0},
	{0xe78, 0x081b25a0}, {0xe7c, 0x081b25a0},
	{0xe80, 0x081b25a0}, {0xe84, 0x631b25a0},
	{0xe88, 0x081b25a0}, {0xe8c, 0x631b25a0},
	{0xed0, 0x631b25a0}, {0xed4, 0x631b25a0},
	{0xed8, 0x631b25a0}, {0xedc, 0x001b25a0},
	{0xee0, 0x001b25a0}, {0xeec, 0x6b1b25a0},
	{0xf14, 0x00000003}, {0xf4c, 0x00000000},
	{0xf00, 0x00000300},
	{0xffff, 0xffffffff},
};

static struct rtl8xxxu_reg32val rtl8192cu_phy_2t_init_table[] = {
	{0x024, 0x0011800f}, {0x028, 0x00ffdb83},
	{0x800, 0x80040002}, {0x804, 0x00000003},
	{0x808, 0x0000fc00}, {0x80c, 0x0000000a},
	{0x810, 0x10000330}, {0x814, 0x020c3d10},
	{0x818, 0x02200385}, {0x81c, 0x00000000},
	{0x820, 0x01000100}, {0x824, 0x00390004},
	{0x828, 0x01000100}, {0x82c, 0x00390004},
	{0x830, 0x27272727}, {0x834, 0x27272727},
	{0x838, 0x27272727}, {0x83c, 0x27272727},
	{0x840, 0x00010000}, {0x844, 0x00010000},
	{0x848, 0x27272727}, {0x84c, 0x27272727},
	{0x850, 0x00000000}, {0x854, 0x00000000},
	{0x858, 0x569a569a}, {0x85c, 0x0c1b25a4},
	{0x860, 0x66e60230}, {0x864, 0x061f0130},
	{0x868, 0x27272727}, {0x86c, 0x2b2b2b27},
	{0x870, 0x07000700}, {0x874, 0x22184000},
	{0x878, 0x08080808}, {0x87c, 0x00000000},
	{0x880, 0xc0083070}, {0x884, 0x000004d5},
	{0x888, 0x00000000}, {0x88c, 0xcc0000c0},
	{0x890, 0x00000800}, {0x894, 0xfffffffe},
	{0x898, 0x40302010}, {0x89c, 0x00706050},
	{0x900, 0x00000000}, {0x904, 0x00000023},
	{0x908, 0x00000000}, {0x90c, 0x81121313},
	{0xa00, 0x00d047c8}, {0xa04, 0x80ff000c},
	{0xa08, 0x8c838300}, {0xa0c, 0x2e68120f},
	{0xa10, 0x9500bb78}, {0xa14, 0x11144028},
	{0xa18, 0x00881117}, {0xa1c, 0x89140f00},
	{0xa20, 0x1a1b0000}, {0xa24, 0x090e1317},
	{0xa28, 0x00000204}, {0xa2c, 0x00d30000},
	{0xa70, 0x101fbf00}, {0xa74, 0x00000007},
	{0xc00, 0x48071d40}, {0xc04, 0x03a05633},
	{0xc08, 0x000000e4}, {0xc0c, 0x6c6c6c6c},
	{0xc10, 0x08800000}, {0xc14, 0x40000100},
	{0xc18, 0x08800000}, {0xc1c, 0x40000100},
	{0xc20, 0x00000000}, {0xc24, 0x00000000},
	{0xc28, 0x00000000}, {0xc2c, 0x00000000},
	{0xc30, 0x69e9ac44}, {0xc34, 0x469652cf},
	{0xc38, 0x49795994}, {0xc3c, 0x0a97971c},
	{0xc40, 0x1f7c403f}, {0xc44, 0x000100b7},
	{0xc48, 0xec020107}, {0xc4c, 0x007f037f},
	{0xc50, 0x69543420}, {0xc54, 0x43bc0094},
	{0xc58, 0x69543420}, {0xc5c, 0x433c0094},
	{0xc60, 0x00000000}, {0xc64, 0x5116848b},
	{0xc68, 0x47c00bff}, {0xc6c, 0x00000036},
	{0xc70, 0x2c7f000d}, {0xc74, 0x2186115b},
	{0xc78, 0x0000001f}, {0xc7c, 0x00b99612},
	{0xc80, 0x40000100}, {0xc84, 0x20f60000},
	{0xc88, 0x40000100}, {0xc8c, 0xa0e40000},
	{0xc90, 0x00121820}, {0xc94, 0x00000000},
	{0xc98, 0x00121820}, {0xc9c, 0x00007f7f},
	{0xca0, 0x00000000}, {0xca4, 0x00000080},
	{0xca8, 0x00000000}, {0xcac, 0x00000000},
	{0xcb0, 0x00000000}, {0xcb4, 0x00000000},
	{0xcb8, 0x00000000}, {0xcbc, 0x28000000},
	{0xcc0, 0x00000000}, {0xcc4, 0x00000000},
	{0xcc8, 0x00000000}, {0xccc, 0x00000000},
	{0xcd0, 0x00000000}, {0xcd4, 0x00000000},
	{0xcd8, 0x64b22427}, {0xcdc, 0x00766932},
	{0xce0, 0x00222222}, {0xce4, 0x00000000},
	{0xce8, 0x37644302}, {0xcec, 0x2f97d40c},
	{0xd00, 0x00080740}, {0xd04, 0x00020403},
	{0xd08, 0x0000907f}, {0xd0c, 0x20010201},
	{0xd10, 0xa0633333}, {0xd14, 0x3333bc43},
	{0xd18, 0x7a8f5b6b}, {0xd2c, 0xcc979975},
	{0xd30, 0x00000000}, {0xd34, 0x80608000},
	{0xd38, 0x00000000}, {0xd3c, 0x00027293},
	{0xd40, 0x00000000}, {0xd44, 0x00000000},
	{0xd48, 0x00000000}, {0xd4c, 0x00000000},
	{0xd50, 0x6437140a}, {0xd54, 0x00000000},
	{0xd58, 0x00000000}, {0xd5c, 0x30032064},
	{0xd60, 0x4653de68}, {0xd64, 0x04518a3c},
	{0xd68, 0x00002101}, {0xd6c, 0x2a201c16},
	{0xd70, 0x1812362e}, {0xd74, 0x322c2220},
	{0xd78, 0x000e3c24}, {0xe00, 0x2a2a2a2a},
	{0xe04, 0x2a2a2a2a}, {0xe08, 0x03902a2a},
	{0xe10, 0x2a2a2a2a}, {0xe14, 0x2a2a2a2a},
	{0xe18, 0x2a2a2a2a}, {0xe1c, 0x2a2a2a2a},
	{0xe28, 0x00000000}, {0xe30, 0x1000dc1f},
	{0xe34, 0x10008c1f}, {0xe38, 0x02140102},
	{0xe3c, 0x681604c2}, {0xe40, 0x01007c00},
	{0xe44, 0x01004800}, {0xe48, 0xfb000000},
	{0xe4c, 0x000028d1}, {0xe50, 0x1000dc1f},
	{0xe54, 0x10008c1f}, {0xe58, 0x02140102},
	{0xe5c, 0x28160d05}, {0xe60, 0x00000010},
	{0xe68, 0x001b25a4}, {0xe6c, 0x63db25a4},
	{0xe70, 0x63db25a4}, {0xe74, 0x0c1b25a4},
	{0xe78, 0x0c1b25a4}, {0xe7c, 0x0c1b25a4},
	{0xe80, 0x0c1b25a4}, {0xe84, 0x63db25a4},
	{0xe88, 0x0c1b25a4}, {0xe8c, 0x63db25a4},
	{0xed0, 0x63db25a4}, {0xed4, 0x63db25a4},
	{0xed8, 0x63db25a4}, {0xedc, 0x001b25a4},
	{0xee0, 0x001b25a4}, {0xeec, 0x6fdb25a4},
	{0xf14, 0x00000003}, {0xf4c, 0x00000000},
	{0xf00, 0x00000300},
	{0xffff, 0xffffffff},
};

static struct rtl8xxxu_reg32val rtl8188ru_phy_1t_highpa_table[] = {
	{0x024, 0x0011800f}, {0x028, 0x00ffdb83},
	{0x040, 0x000c0004}, {0x800, 0x80040000},
	{0x804, 0x00000001}, {0x808, 0x0000fc00},
	{0x80c, 0x0000000a}, {0x810, 0x10005388},
	{0x814, 0x020c3d10}, {0x818, 0x02200385},
	{0x81c, 0x00000000}, {0x820, 0x01000100},
	{0x824, 0x00390204}, {0x828, 0x00000000},
	{0x82c, 0x00000000}, {0x830, 0x00000000},
	{0x834, 0x00000000}, {0x838, 0x00000000},
	{0x83c, 0x00000000}, {0x840, 0x00010000},
	{0x844, 0x00000000}, {0x848, 0x00000000},
	{0x84c, 0x00000000}, {0x850, 0x00000000},
	{0x854, 0x00000000}, {0x858, 0x569a569a},
	{0x85c, 0x001b25a4}, {0x860, 0x66e60230},
	{0x864, 0x061f0130}, {0x868, 0x00000000},
	{0x86c, 0x20202000}, {0x870, 0x03000300},
	{0x874, 0x22004000}, {0x878, 0x00000808},
	{0x87c, 0x00ffc3f1}, {0x880, 0xc0083070},
	{0x884, 0x000004d5}, {0x888, 0x00000000},
	{0x88c, 0xccc000c0}, {0x890, 0x00000800},
	{0x894, 0xfffffffe}, {0x898, 0x40302010},
	{0x89c, 0x00706050}, {0x900, 0x00000000},
	{0x904, 0x00000023}, {0x908, 0x00000000},
	{0x90c, 0x81121111}, {0xa00, 0x00d047c8},
	{0xa04, 0x80ff000c}, {0xa08, 0x8c838300},
	{0xa0c, 0x2e68120f}, {0xa10, 0x9500bb78},
	{0xa14, 0x11144028}, {0xa18, 0x00881117},
	{0xa1c, 0x89140f00}, {0xa20, 0x15160000},
	{0xa24, 0x070b0f12}, {0xa28, 0x00000104},
	{0xa2c, 0x00d30000}, {0xa70, 0x101fbf00},
	{0xa74, 0x00000007}, {0xc00, 0x48071d40},
	{0xc04, 0x03a05611}, {0xc08, 0x000000e4},
	{0xc0c, 0x6c6c6c6c}, {0xc10, 0x08800000},
	{0xc14, 0x40000100}, {0xc18, 0x08800000},
	{0xc1c, 0x40000100}, {0xc20, 0x00000000},
	{0xc24, 0x00000000}, {0xc28, 0x00000000},
	{0xc2c, 0x00000000}, {0xc30, 0x69e9ac44},
	{0xc34, 0x469652cf}, {0xc38, 0x49795994},
	{0xc3c, 0x0a97971c}, {0xc40, 0x1f7c403f},
	{0xc44, 0x000100b7}, {0xc48, 0xec020107},
	{0xc4c, 0x007f037f}, {0xc50, 0x6954342e},
	{0xc54, 0x43bc0094}, {0xc58, 0x6954342f},
	{0xc5c, 0x433c0094}, {0xc60, 0x00000000},
	{0xc64, 0x5116848b}, {0xc68, 0x47c00bff},
	{0xc6c, 0x00000036}, {0xc70, 0x2c46000d},
	{0xc74, 0x018610db}, {0xc78, 0x0000001f},
	{0xc7c, 0x00b91612}, {0xc80, 0x24000090},
	{0xc84, 0x20f60000}, {0xc88, 0x24000090},
	{0xc8c, 0x20200000}, {0xc90, 0x00121820},
	{0xc94, 0x00000000}, {0xc98, 0x00121820},
	{0xc9c, 0x00007f7f}, {0xca0, 0x00000000},
	{0xca4, 0x00000080}, {0xca8, 0x00000000},
	{0xcac, 0x00000000}, {0xcb0, 0x00000000},
	{0xcb4, 0x00000000}, {0xcb8, 0x00000000},
	{0xcbc, 0x28000000}, {0xcc0, 0x00000000},
	{0xcc4, 0x00000000}, {0xcc8, 0x00000000},
	{0xccc, 0x00000000}, {0xcd0, 0x00000000},
	{0xcd4, 0x00000000}, {0xcd8, 0x64b22427},
	{0xcdc, 0x00766932}, {0xce0, 0x00222222},
	{0xce4, 0x00000000}, {0xce8, 0x37644302},
	{0xcec, 0x2f97d40c}, {0xd00, 0x00080740},
	{0xd04, 0x00020401}, {0xd08, 0x0000907f},
	{0xd0c, 0x20010201}, {0xd10, 0xa0633333},
	{0xd14, 0x3333bc43}, {0xd18, 0x7a8f5b6b},
	{0xd2c, 0xcc979975}, {0xd30, 0x00000000},
	{0xd34, 0x80608000}, {0xd38, 0x00000000},
	{0xd3c, 0x00027293}, {0xd40, 0x00000000},
	{0xd44, 0x00000000}, {0xd48, 0x00000000},
	{0xd4c, 0x00000000}, {0xd50, 0x6437140a},
	{0xd54, 0x00000000}, {0xd58, 0x00000000},
	{0xd5c, 0x30032064}, {0xd60, 0x4653de68},
	{0xd64, 0x04518a3c}, {0xd68, 0x00002101},
	{0xd6c, 0x2a201c16}, {0xd70, 0x1812362e},
	{0xd74, 0x322c2220}, {0xd78, 0x000e3c24},
	{0xe00, 0x24242424}, {0xe04, 0x24242424},
	{0xe08, 0x03902024}, {0xe10, 0x24242424},
	{0xe14, 0x24242424}, {0xe18, 0x24242424},
	{0xe1c, 0x24242424}, {0xe28, 0x00000000},
	{0xe30, 0x1000dc1f}, {0xe34, 0x10008c1f},
	{0xe38, 0x02140102}, {0xe3c, 0x681604c2},
	{0xe40, 0x01007c00}, {0xe44, 0x01004800},
	{0xe48, 0xfb000000}, {0xe4c, 0x000028d1},
	{0xe50, 0x1000dc1f}, {0xe54, 0x10008c1f},
	{0xe58, 0x02140102}, {0xe5c, 0x28160d05},
	{0xe60, 0x00000008}, {0xe68, 0x001b25a4},
	{0xe6c, 0x631b25a0}, {0xe70, 0x631b25a0},
	{0xe74, 0x081b25a0}, {0xe78, 0x081b25a0},
	{0xe7c, 0x081b25a0}, {0xe80, 0x081b25a0},
	{0xe84, 0x631b25a0}, {0xe88, 0x081b25a0},
	{0xe8c, 0x631b25a0}, {0xed0, 0x631b25a0},
	{0xed4, 0x631b25a0}, {0xed8, 0x631b25a0},
	{0xedc, 0x001b25a0}, {0xee0, 0x001b25a0},
	{0xeec, 0x6b1b25a0}, {0xee8, 0x31555448},
	{0xf14, 0x00000003}, {0xf4c, 0x00000000},
	{0xf00, 0x00000300},
	{0xffff, 0xffffffff},
};

static struct rtl8xxxu_reg32val rtl8xxx_agc_standard_table[] = {
	{0xc78, 0x7b000001}, {0xc78, 0x7b010001},
	{0xc78, 0x7b020001}, {0xc78, 0x7b030001},
	{0xc78, 0x7b040001}, {0xc78, 0x7b050001},
	{0xc78, 0x7a060001}, {0xc78, 0x79070001},
	{0xc78, 0x78080001}, {0xc78, 0x77090001},
	{0xc78, 0x760a0001}, {0xc78, 0x750b0001},
	{0xc78, 0x740c0001}, {0xc78, 0x730d0001},
	{0xc78, 0x720e0001}, {0xc78, 0x710f0001},
	{0xc78, 0x70100001}, {0xc78, 0x6f110001},
	{0xc78, 0x6e120001}, {0xc78, 0x6d130001},
	{0xc78, 0x6c140001}, {0xc78, 0x6b150001},
	{0xc78, 0x6a160001}, {0xc78, 0x69170001},
	{0xc78, 0x68180001}, {0xc78, 0x67190001},
	{0xc78, 0x661a0001}, {0xc78, 0x651b0001},
	{0xc78, 0x641c0001}, {0xc78, 0x631d0001},
	{0xc78, 0x621e0001}, {0xc78, 0x611f0001},
	{0xc78, 0x60200001}, {0xc78, 0x49210001},
	{0xc78, 0x48220001}, {0xc78, 0x47230001},
	{0xc78, 0x46240001}, {0xc78, 0x45250001},
	{0xc78, 0x44260001}, {0xc78, 0x43270001},
	{0xc78, 0x42280001}, {0xc78, 0x41290001},
	{0xc78, 0x402a0001}, {0xc78, 0x262b0001},
	{0xc78, 0x252c0001}, {0xc78, 0x242d0001},
	{0xc78, 0x232e0001}, {0xc78, 0x222f0001},
	{0xc78, 0x21300001}, {0xc78, 0x20310001},
	{0xc78, 0x06320001}, {0xc78, 0x05330001},
	{0xc78, 0x04340001}, {0xc78, 0x03350001},
	{0xc78, 0x02360001}, {0xc78, 0x01370001},
	{0xc78, 0x00380001}, {0xc78, 0x00390001},
	{0xc78, 0x003a0001}, {0xc78, 0x003b0001},
	{0xc78, 0x003c0001}, {0xc78, 0x003d0001},
	{0xc78, 0x003e0001}, {0xc78, 0x003f0001},
	{0xc78, 0x7b400001}, {0xc78, 0x7b410001},
	{0xc78, 0x7b420001}, {0xc78, 0x7b430001},
	{0xc78, 0x7b440001}, {0xc78, 0x7b450001},
	{0xc78, 0x7a460001}, {0xc78, 0x79470001},
	{0xc78, 0x78480001}, {0xc78, 0x77490001},
	{0xc78, 0x764a0001}, {0xc78, 0x754b0001},
	{0xc78, 0x744c0001}, {0xc78, 0x734d0001},
	{0xc78, 0x724e0001}, {0xc78, 0x714f0001},
	{0xc78, 0x70500001}, {0xc78, 0x6f510001},
	{0xc78, 0x6e520001}, {0xc78, 0x6d530001},
	{0xc78, 0x6c540001}, {0xc78, 0x6b550001},
	{0xc78, 0x6a560001}, {0xc78, 0x69570001},
	{0xc78, 0x68580001}, {0xc78, 0x67590001},
	{0xc78, 0x665a0001}, {0xc78, 0x655b0001},
	{0xc78, 0x645c0001}, {0xc78, 0x635d0001},
	{0xc78, 0x625e0001}, {0xc78, 0x615f0001},
	{0xc78, 0x60600001}, {0xc78, 0x49610001},
	{0xc78, 0x48620001}, {0xc78, 0x47630001},
	{0xc78, 0x46640001}, {0xc78, 0x45650001},
	{0xc78, 0x44660001}, {0xc78, 0x43670001},
	{0xc78, 0x42680001}, {0xc78, 0x41690001},
	{0xc78, 0x406a0001}, {0xc78, 0x266b0001},
	{0xc78, 0x256c0001}, {0xc78, 0x246d0001},
	{0xc78, 0x236e0001}, {0xc78, 0x226f0001},
	{0xc78, 0x21700001}, {0xc78, 0x20710001},
	{0xc78, 0x06720001}, {0xc78, 0x05730001},
	{0xc78, 0x04740001}, {0xc78, 0x03750001},
	{0xc78, 0x02760001}, {0xc78, 0x01770001},
	{0xc78, 0x00780001}, {0xc78, 0x00790001},
	{0xc78, 0x007a0001}, {0xc78, 0x007b0001},
	{0xc78, 0x007c0001}, {0xc78, 0x007d0001},
	{0xc78, 0x007e0001}, {0xc78, 0x007f0001},
	{0xc78, 0x3800001e}, {0xc78, 0x3801001e},
	{0xc78, 0x3802001e}, {0xc78, 0x3803001e},
	{0xc78, 0x3804001e}, {0xc78, 0x3805001e},
	{0xc78, 0x3806001e}, {0xc78, 0x3807001e},
	{0xc78, 0x3808001e}, {0xc78, 0x3c09001e},
	{0xc78, 0x3e0a001e}, {0xc78, 0x400b001e},
	{0xc78, 0x440c001e}, {0xc78, 0x480d001e},
	{0xc78, 0x4c0e001e}, {0xc78, 0x500f001e},
	{0xc78, 0x5210001e}, {0xc78, 0x5611001e},
	{0xc78, 0x5a12001e}, {0xc78, 0x5e13001e},
	{0xc78, 0x6014001e}, {0xc78, 0x6015001e},
	{0xc78, 0x6016001e}, {0xc78, 0x6217001e},
	{0xc78, 0x6218001e}, {0xc78, 0x6219001e},
	{0xc78, 0x621a001e}, {0xc78, 0x621b001e},
	{0xc78, 0x621c001e}, {0xc78, 0x621d001e},
	{0xc78, 0x621e001e}, {0xc78, 0x621f001e},
	{0xffff, 0xffffffff}
};

static struct rtl8xxxu_reg32val rtl8xxx_agc_highpa_table[] = {
	{0xc78, 0x7b000001}, {0xc78, 0x7b010001},
	{0xc78, 0x7b020001}, {0xc78, 0x7b030001},
	{0xc78, 0x7b040001}, {0xc78, 0x7b050001},
	{0xc78, 0x7b060001}, {0xc78, 0x7b070001},
	{0xc78, 0x7b080001}, {0xc78, 0x7a090001},
	{0xc78, 0x790a0001}, {0xc78, 0x780b0001},
	{0xc78, 0x770c0001}, {0xc78, 0x760d0001},
	{0xc78, 0x750e0001}, {0xc78, 0x740f0001},
	{0xc78, 0x73100001}, {0xc78, 0x72110001},
	{0xc78, 0x71120001}, {0xc78, 0x70130001},
	{0xc78, 0x6f140001}, {0xc78, 0x6e150001},
	{0xc78, 0x6d160001}, {0xc78, 0x6c170001},
	{0xc78, 0x6b180001}, {0xc78, 0x6a190001},
	{0xc78, 0x691a0001}, {0xc78, 0x681b0001},
	{0xc78, 0x671c0001}, {0xc78, 0x661d0001},
	{0xc78, 0x651e0001}, {0xc78, 0x641f0001},
	{0xc78, 0x63200001}, {0xc78, 0x62210001},
	{0xc78, 0x61220001}, {0xc78, 0x60230001},
	{0xc78, 0x46240001}, {0xc78, 0x45250001},
	{0xc78, 0x44260001}, {0xc78, 0x43270001},
	{0xc78, 0x42280001}, {0xc78, 0x41290001},
	{0xc78, 0x402a0001}, {0xc78, 0x262b0001},
	{0xc78, 0x252c0001}, {0xc78, 0x242d0001},
	{0xc78, 0x232e0001}, {0xc78, 0x222f0001},
	{0xc78, 0x21300001}, {0xc78, 0x20310001},
	{0xc78, 0x06320001}, {0xc78, 0x05330001},
	{0xc78, 0x04340001}, {0xc78, 0x03350001},
	{0xc78, 0x02360001}, {0xc78, 0x01370001},
	{0xc78, 0x00380001}, {0xc78, 0x00390001},
	{0xc78, 0x003a0001}, {0xc78, 0x003b0001},
	{0xc78, 0x003c0001}, {0xc78, 0x003d0001},
	{0xc78, 0x003e0001}, {0xc78, 0x003f0001},
	{0xc78, 0x7b400001}, {0xc78, 0x7b410001},
	{0xc78, 0x7b420001}, {0xc78, 0x7b430001},
	{0xc78, 0x7b440001}, {0xc78, 0x7b450001},
	{0xc78, 0x7b460001}, {0xc78, 0x7b470001},
	{0xc78, 0x7b480001}, {0xc78, 0x7a490001},
	{0xc78, 0x794a0001}, {0xc78, 0x784b0001},
	{0xc78, 0x774c0001}, {0xc78, 0x764d0001},
	{0xc78, 0x754e0001}, {0xc78, 0x744f0001},
	{0xc78, 0x73500001}, {0xc78, 0x72510001},
	{0xc78, 0x71520001}, {0xc78, 0x70530001},
	{0xc78, 0x6f540001}, {0xc78, 0x6e550001},
	{0xc78, 0x6d560001}, {0xc78, 0x6c570001},
	{0xc78, 0x6b580001}, {0xc78, 0x6a590001},
	{0xc78, 0x695a0001}, {0xc78, 0x685b0001},
	{0xc78, 0x675c0001}, {0xc78, 0x665d0001},
	{0xc78, 0x655e0001}, {0xc78, 0x645f0001},
	{0xc78, 0x63600001}, {0xc78, 0x62610001},
	{0xc78, 0x61620001}, {0xc78, 0x60630001},
	{0xc78, 0x46640001}, {0xc78, 0x45650001},
	{0xc78, 0x44660001}, {0xc78, 0x43670001},
	{0xc78, 0x42680001}, {0xc78, 0x41690001},
	{0xc78, 0x406a0001}, {0xc78, 0x266b0001},
	{0xc78, 0x256c0001}, {0xc78, 0x246d0001},
	{0xc78, 0x236e0001}, {0xc78, 0x226f0001},
	{0xc78, 0x21700001}, {0xc78, 0x20710001},
	{0xc78, 0x06720001}, {0xc78, 0x05730001},
	{0xc78, 0x04740001}, {0xc78, 0x03750001},
	{0xc78, 0x02760001}, {0xc78, 0x01770001},
	{0xc78, 0x00780001}, {0xc78, 0x00790001},
	{0xc78, 0x007a0001}, {0xc78, 0x007b0001},
	{0xc78, 0x007c0001}, {0xc78, 0x007d0001},
	{0xc78, 0x007e0001}, {0xc78, 0x007f0001},
	{0xc78, 0x3800001e}, {0xc78, 0x3801001e},
	{0xc78, 0x3802001e}, {0xc78, 0x3803001e},
	{0xc78, 0x3804001e}, {0xc78, 0x3805001e},
	{0xc78, 0x3806001e}, {0xc78, 0x3807001e},
	{0xc78, 0x3808001e}, {0xc78, 0x3c09001e},
	{0xc78, 0x3e0a001e}, {0xc78, 0x400b001e},
	{0xc78, 0x440c001e}, {0xc78, 0x480d001e},
	{0xc78, 0x4c0e001e}, {0xc78, 0x500f001e},
	{0xc78, 0x5210001e}, {0xc78, 0x5611001e},
	{0xc78, 0x5a12001e}, {0xc78, 0x5e13001e},
	{0xc78, 0x6014001e}, {0xc78, 0x6015001e},
	{0xc78, 0x6016001e}, {0xc78, 0x6217001e},
	{0xc78, 0x6218001e}, {0xc78, 0x6219001e},
	{0xc78, 0x621a001e}, {0xc78, 0x621b001e},
	{0xc78, 0x621c001e}, {0xc78, 0x621d001e},
	{0xc78, 0x621e001e}, {0xc78, 0x621f001e},
	{0xffff, 0xffffffff}
};

static struct rtl8xxxu_rfregs rtl8xxxu_rfregs[] = {
	{	/* RF_A */
		.hssiparm1 = REG_FPGA0_XA_HSSI_PARM1,
		.hssiparm2 = REG_FPGA0_XA_HSSI_PARM2,
		.lssiparm = REG_FPGA0_XA_LSSI_PARM,
		.hspiread = REG_HSPI_XA_READBACK,
		.lssiread = REG_FPGA0_XA_LSSI_READBACK,
		.rf_sw_ctrl = REG_FPGA0_XA_RF_SW_CTRL,
	},
	{	/* RF_B */
		.hssiparm1 = REG_FPGA0_XB_HSSI_PARM1,
		.hssiparm2 = REG_FPGA0_XB_HSSI_PARM2,
		.lssiparm = REG_FPGA0_XB_LSSI_PARM,
		.hspiread = REG_HSPI_XB_READBACK,
		.lssiread = REG_FPGA0_XB_LSSI_READBACK,
		.rf_sw_ctrl = REG_FPGA0_XB_RF_SW_CTRL,
	},
};

const u32 rtl8xxxu_iqk_phy_iq_bb_reg[RTL8XXXU_BB_REGS] = {
	REG_OFDM0_XA_RX_IQ_IMBALANCE,
	REG_OFDM0_XB_RX_IQ_IMBALANCE,
	REG_OFDM0_ENERGY_CCA_THRES,
	REG_OFDM0_AGCR_SSI_TABLE,
	REG_OFDM0_XA_TX_IQ_IMBALANCE,
	REG_OFDM0_XB_TX_IQ_IMBALANCE,
	REG_OFDM0_XC_TX_AFE,
	REG_OFDM0_XD_TX_AFE,
	REG_OFDM0_RX_IQ_EXT_ANTA
};

u8 rtl8xxxu_read8(struct rtl8xxxu_priv *priv, u16 addr)
{
	struct usb_device *udev = priv->udev;
	int len;
	u8 data;

	mutex_lock(&priv->usb_buf_mutex);
	len = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			      REALTEK_USB_CMD_REQ, REALTEK_USB_READ,
			      addr, 0, &priv->usb_buf.val8, sizeof(u8),
			      RTW_USB_CONTROL_MSG_TIMEOUT);
	data = priv->usb_buf.val8;
	mutex_unlock(&priv->usb_buf_mutex);

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_REG_READ)
		dev_info(&udev->dev, "%s(%04x)   = 0x%02x, len %i\n",
			 __func__, addr, data, len);
	return data;
}

u16 rtl8xxxu_read16(struct rtl8xxxu_priv *priv, u16 addr)
{
	struct usb_device *udev = priv->udev;
	int len;
	u16 data;

	mutex_lock(&priv->usb_buf_mutex);
	len = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			      REALTEK_USB_CMD_REQ, REALTEK_USB_READ,
			      addr, 0, &priv->usb_buf.val16, sizeof(u16),
			      RTW_USB_CONTROL_MSG_TIMEOUT);
	data = le16_to_cpu(priv->usb_buf.val16);
	mutex_unlock(&priv->usb_buf_mutex);

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_REG_READ)
		dev_info(&udev->dev, "%s(%04x)  = 0x%04x, len %i\n",
			 __func__, addr, data, len);
	return data;
}

u32 rtl8xxxu_read32(struct rtl8xxxu_priv *priv, u16 addr)
{
	struct usb_device *udev = priv->udev;
	int len;
	u32 data;

	mutex_lock(&priv->usb_buf_mutex);
	len = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			      REALTEK_USB_CMD_REQ, REALTEK_USB_READ,
			      addr, 0, &priv->usb_buf.val32, sizeof(u32),
			      RTW_USB_CONTROL_MSG_TIMEOUT);
	data = le32_to_cpu(priv->usb_buf.val32);
	mutex_unlock(&priv->usb_buf_mutex);

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_REG_READ)
		dev_info(&udev->dev, "%s(%04x)  = 0x%08x, len %i\n",
			 __func__, addr, data, len);
	return data;
}

int rtl8xxxu_write8(struct rtl8xxxu_priv *priv, u16 addr, u8 val)
{
	struct usb_device *udev = priv->udev;
	int ret;

	mutex_lock(&priv->usb_buf_mutex);
	priv->usb_buf.val8 = val;
	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			      REALTEK_USB_CMD_REQ, REALTEK_USB_WRITE,
			      addr, 0, &priv->usb_buf.val8, sizeof(u8),
			      RTW_USB_CONTROL_MSG_TIMEOUT);

	mutex_unlock(&priv->usb_buf_mutex);

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_REG_WRITE)
		dev_info(&udev->dev, "%s(%04x) = 0x%02x\n",
			 __func__, addr, val);
	return ret;
}

int rtl8xxxu_write16(struct rtl8xxxu_priv *priv, u16 addr, u16 val)
{
	struct usb_device *udev = priv->udev;
	int ret;

	mutex_lock(&priv->usb_buf_mutex);
	priv->usb_buf.val16 = cpu_to_le16(val);
	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			      REALTEK_USB_CMD_REQ, REALTEK_USB_WRITE,
			      addr, 0, &priv->usb_buf.val16, sizeof(u16),
			      RTW_USB_CONTROL_MSG_TIMEOUT);
	mutex_unlock(&priv->usb_buf_mutex);

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_REG_WRITE)
		dev_info(&udev->dev, "%s(%04x) = 0x%04x\n",
			 __func__, addr, val);
	return ret;
}

int rtl8xxxu_write32(struct rtl8xxxu_priv *priv, u16 addr, u32 val)
{
	struct usb_device *udev = priv->udev;
	int ret;

	mutex_lock(&priv->usb_buf_mutex);
	priv->usb_buf.val32 = cpu_to_le32(val);
	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			      REALTEK_USB_CMD_REQ, REALTEK_USB_WRITE,
			      addr, 0, &priv->usb_buf.val32, sizeof(u32),
			      RTW_USB_CONTROL_MSG_TIMEOUT);
	mutex_unlock(&priv->usb_buf_mutex);

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_REG_WRITE)
		dev_info(&udev->dev, "%s(%04x) = 0x%08x\n",
			 __func__, addr, val);
	return ret;
}

static int
rtl8xxxu_writeN(struct rtl8xxxu_priv *priv, u16 addr, u8 *buf, u16 len)
{
	struct usb_device *udev = priv->udev;
	int blocksize = priv->fops->writeN_block_size;
	int ret, i, count, remainder;

	count = len / blocksize;
	remainder = len % blocksize;

	for (i = 0; i < count; i++) {
		ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				      REALTEK_USB_CMD_REQ, REALTEK_USB_WRITE,
				      addr, 0, buf, blocksize,
				      RTW_USB_CONTROL_MSG_TIMEOUT);
		if (ret != blocksize)
			goto write_error;

		addr += blocksize;
		buf += blocksize;
	}

	if (remainder) {
		ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				      REALTEK_USB_CMD_REQ, REALTEK_USB_WRITE,
				      addr, 0, buf, remainder,
				      RTW_USB_CONTROL_MSG_TIMEOUT);
		if (ret != remainder)
			goto write_error;
	}

	return len;

write_error:
	dev_info(&udev->dev,
		 "%s: Failed to write block at addr: %04x size: %04x\n",
		 __func__, addr, blocksize);
	return -EAGAIN;
}

u32 rtl8xxxu_read_rfreg(struct rtl8xxxu_priv *priv,
			enum rtl8xxxu_rfpath path, u8 reg)
{
	u32 hssia, val32, retval;

	hssia = rtl8xxxu_read32(priv, REG_FPGA0_XA_HSSI_PARM2);
	if (path != RF_A)
		val32 = rtl8xxxu_read32(priv, rtl8xxxu_rfregs[path].hssiparm2);
	else
		val32 = hssia;

	val32 &= ~FPGA0_HSSI_PARM2_ADDR_MASK;
	val32 |= (reg << FPGA0_HSSI_PARM2_ADDR_SHIFT);
	val32 |= FPGA0_HSSI_PARM2_EDGE_READ;
	hssia &= ~FPGA0_HSSI_PARM2_EDGE_READ;
	rtl8xxxu_write32(priv, REG_FPGA0_XA_HSSI_PARM2, hssia);

	udelay(10);

	rtl8xxxu_write32(priv, rtl8xxxu_rfregs[path].hssiparm2, val32);
	udelay(100);

	hssia |= FPGA0_HSSI_PARM2_EDGE_READ;
	rtl8xxxu_write32(priv, REG_FPGA0_XA_HSSI_PARM2, hssia);
	udelay(10);

	val32 = rtl8xxxu_read32(priv, rtl8xxxu_rfregs[path].hssiparm1);
	if (val32 & FPGA0_HSSI_PARM1_PI)
		retval = rtl8xxxu_read32(priv, rtl8xxxu_rfregs[path].hspiread);
	else
		retval = rtl8xxxu_read32(priv, rtl8xxxu_rfregs[path].lssiread);

	retval &= 0xfffff;

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_RFREG_READ)
		dev_info(&priv->udev->dev, "%s(%02x) = 0x%06x\n",
			 __func__, reg, retval);
	return retval;
}

/*
 * The RTL8723BU driver indicates that registers 0xb2 and 0xb6 can
 * have write issues in high temperature conditions. We may have to
 * retry writing them.
 */
int rtl8xxxu_write_rfreg(struct rtl8xxxu_priv *priv,
			 enum rtl8xxxu_rfpath path, u8 reg, u32 data)
{
	int ret, retval;
	u32 dataaddr, val32;

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_RFREG_WRITE)
		dev_info(&priv->udev->dev, "%s(%02x) = 0x%06x\n",
			 __func__, reg, data);

	data &= FPGA0_LSSI_PARM_DATA_MASK;
	dataaddr = (reg << FPGA0_LSSI_PARM_ADDR_SHIFT) | data;

	if (priv->rtl_chip == RTL8192E) {
		val32 = rtl8xxxu_read32(priv, REG_FPGA0_POWER_SAVE);
		val32 &= ~0x20000;
		rtl8xxxu_write32(priv, REG_FPGA0_POWER_SAVE, val32);
	}

	/* Use XB for path B */
	ret = rtl8xxxu_write32(priv, rtl8xxxu_rfregs[path].lssiparm, dataaddr);
	if (ret != sizeof(dataaddr))
		retval = -EIO;
	else
		retval = 0;

	udelay(1);

	if (priv->rtl_chip == RTL8192E) {
		val32 = rtl8xxxu_read32(priv, REG_FPGA0_POWER_SAVE);
		val32 |= 0x20000;
		rtl8xxxu_write32(priv, REG_FPGA0_POWER_SAVE, val32);
	}

	return retval;
}

static int
rtl8xxxu_gen1_h2c_cmd(struct rtl8xxxu_priv *priv, struct h2c_cmd *h2c, int len)
{
	struct device *dev = &priv->udev->dev;
	int mbox_nr, retry, retval = 0;
	int mbox_reg, mbox_ext_reg;
	u8 val8;

	mutex_lock(&priv->h2c_mutex);

	mbox_nr = priv->next_mbox;
	mbox_reg = REG_HMBOX_0 + (mbox_nr * 4);
	mbox_ext_reg = REG_HMBOX_EXT_0 + (mbox_nr * 2);

	/*
	 * MBOX ready?
	 */
	retry = 100;
	do {
		val8 = rtl8xxxu_read8(priv, REG_HMTFR);
		if (!(val8 & BIT(mbox_nr)))
			break;
	} while (retry--);

	if (!retry) {
		dev_info(dev, "%s: Mailbox busy\n", __func__);
		retval = -EBUSY;
		goto error;
	}

	/*
	 * Need to swap as it's being swapped again by rtl8xxxu_write16/32()
	 */
	if (len > sizeof(u32)) {
		rtl8xxxu_write16(priv, mbox_ext_reg, le16_to_cpu(h2c->raw.ext));
		if (rtl8xxxu_debug & RTL8XXXU_DEBUG_H2C)
			dev_info(dev, "H2C_EXT %04x\n",
				 le16_to_cpu(h2c->raw.ext));
	}
	rtl8xxxu_write32(priv, mbox_reg, le32_to_cpu(h2c->raw.data));
	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_H2C)
		dev_info(dev, "H2C %08x\n", le32_to_cpu(h2c->raw.data));

	priv->next_mbox = (mbox_nr + 1) % H2C_MAX_MBOX;

error:
	mutex_unlock(&priv->h2c_mutex);
	return retval;
}

int
rtl8xxxu_gen2_h2c_cmd(struct rtl8xxxu_priv *priv, struct h2c_cmd *h2c, int len)
{
	struct device *dev = &priv->udev->dev;
	int mbox_nr, retry, retval = 0;
	int mbox_reg, mbox_ext_reg;
	u8 val8;

	mutex_lock(&priv->h2c_mutex);

	mbox_nr = priv->next_mbox;
	mbox_reg = REG_HMBOX_0 + (mbox_nr * 4);
	mbox_ext_reg = REG_HMBOX_EXT0_8723B + (mbox_nr * 4);

	/*
	 * MBOX ready?
	 */
	retry = 100;
	do {
		val8 = rtl8xxxu_read8(priv, REG_HMTFR);
		if (!(val8 & BIT(mbox_nr)))
			break;
	} while (retry--);

	if (!retry) {
		dev_info(dev, "%s: Mailbox busy\n", __func__);
		retval = -EBUSY;
		goto error;
	}

	/*
	 * Need to swap as it's being swapped again by rtl8xxxu_write16/32()
	 */
	if (len > sizeof(u32)) {
		rtl8xxxu_write32(priv, mbox_ext_reg,
				 le32_to_cpu(h2c->raw_wide.ext));
		if (rtl8xxxu_debug & RTL8XXXU_DEBUG_H2C)
			dev_info(dev, "H2C_EXT %08x\n",
				 le32_to_cpu(h2c->raw_wide.ext));
	}
	rtl8xxxu_write32(priv, mbox_reg, le32_to_cpu(h2c->raw.data));
	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_H2C)
		dev_info(dev, "H2C %08x\n", le32_to_cpu(h2c->raw.data));

	priv->next_mbox = (mbox_nr + 1) % H2C_MAX_MBOX;

error:
	mutex_unlock(&priv->h2c_mutex);
	return retval;
}

void rtl8xxxu_gen1_enable_rf(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u32 val32;

	val8 = rtl8xxxu_read8(priv, REG_SPS0_CTRL);
	val8 |= BIT(0) | BIT(3);
	rtl8xxxu_write8(priv, REG_SPS0_CTRL, val8);

	val32 = rtl8xxxu_read32(priv, REG_FPGA0_XAB_RF_PARM);
	val32 &= ~(BIT(4) | BIT(5));
	val32 |= BIT(3);
	if (priv->rf_paths == 2) {
		val32 &= ~(BIT(20) | BIT(21));
		val32 |= BIT(19);
	}
	rtl8xxxu_write32(priv, REG_FPGA0_XAB_RF_PARM, val32);

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_TRX_PATH_ENABLE);
	val32 &= ~OFDM_RF_PATH_TX_MASK;
	if (priv->tx_paths == 2)
		val32 |= OFDM_RF_PATH_TX_A | OFDM_RF_PATH_TX_B;
	else if (priv->rtl_chip == RTL8192C || priv->rtl_chip == RTL8191C)
		val32 |= OFDM_RF_PATH_TX_B;
	else
		val32 |= OFDM_RF_PATH_TX_A;
	rtl8xxxu_write32(priv, REG_OFDM0_TRX_PATH_ENABLE, val32);

	val32 = rtl8xxxu_read32(priv, REG_FPGA0_RF_MODE);
	val32 &= ~FPGA_RF_MODE_JAPAN;
	rtl8xxxu_write32(priv, REG_FPGA0_RF_MODE, val32);

	if (priv->rf_paths == 2)
		rtl8xxxu_write32(priv, REG_RX_WAIT_CCA, 0x63db25a0);
	else
		rtl8xxxu_write32(priv, REG_RX_WAIT_CCA, 0x631b25a0);

	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_AC, 0x32d95);
	if (priv->rf_paths == 2)
		rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_AC, 0x32d95);

	rtl8xxxu_write8(priv, REG_TXPAUSE, 0x00);
}

void rtl8xxxu_gen1_disable_rf(struct rtl8xxxu_priv *priv)
{
	u8 sps0;
	u32 val32;

	sps0 = rtl8xxxu_read8(priv, REG_SPS0_CTRL);

	/* RF RX code for preamble power saving */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_XAB_RF_PARM);
	val32 &= ~(BIT(3) | BIT(4) | BIT(5));
	if (priv->rf_paths == 2)
		val32 &= ~(BIT(19) | BIT(20) | BIT(21));
	rtl8xxxu_write32(priv, REG_FPGA0_XAB_RF_PARM, val32);

	/* Disable TX for four paths */
	val32 = rtl8xxxu_read32(priv, REG_OFDM0_TRX_PATH_ENABLE);
	val32 &= ~OFDM_RF_PATH_TX_MASK;
	rtl8xxxu_write32(priv, REG_OFDM0_TRX_PATH_ENABLE, val32);

	/* Enable power saving */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_RF_MODE);
	val32 |= FPGA_RF_MODE_JAPAN;
	rtl8xxxu_write32(priv, REG_FPGA0_RF_MODE, val32);

	/* AFE control register to power down bits [30:22] */
	if (priv->rf_paths == 2)
		rtl8xxxu_write32(priv, REG_RX_WAIT_CCA, 0x00db25a0);
	else
		rtl8xxxu_write32(priv, REG_RX_WAIT_CCA, 0x001b25a0);

	/* Power down RF module */
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_AC, 0);
	if (priv->rf_paths == 2)
		rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_AC, 0);

	sps0 &= ~(BIT(0) | BIT(3));
	rtl8xxxu_write8(priv, REG_SPS0_CTRL, sps0);
}

static void rtl8xxxu_stop_tx_beacon(struct rtl8xxxu_priv *priv)
{
	u8 val8;

	val8 = rtl8xxxu_read8(priv, REG_FWHW_TXQ_CTRL + 2);
	val8 &= ~BIT(6);
	rtl8xxxu_write8(priv, REG_FWHW_TXQ_CTRL + 2, val8);

	rtl8xxxu_write8(priv, REG_TBTT_PROHIBIT + 1, 0x64);
	val8 = rtl8xxxu_read8(priv, REG_TBTT_PROHIBIT + 2);
	val8 &= ~BIT(0);
	rtl8xxxu_write8(priv, REG_TBTT_PROHIBIT + 2, val8);
}


/*
 * The rtl8723a has 3 channel groups for it's efuse settings. It only
 * supports the 2.4GHz band, so channels 1 - 14:
 *  group 0: channels 1 - 3
 *  group 1: channels 4 - 9
 *  group 2: channels 10 - 14
 *
 * Note: We index from 0 in the code
 */
static int rtl8xxxu_gen1_channel_to_group(int channel)
{
	int group;

	if (channel < 4)
		group = 0;
	else if (channel < 10)
		group = 1;
	else
		group = 2;

	return group;
}

/*
 * Valid for rtl8723bu and rtl8192eu
 */
int rtl8xxxu_gen2_channel_to_group(int channel)
{
	int group;

	if (channel < 3)
		group = 0;
	else if (channel < 6)
		group = 1;
	else if (channel < 9)
		group = 2;
	else if (channel < 12)
		group = 3;
	else
		group = 4;

	return group;
}

void rtl8xxxu_gen1_config_channel(struct ieee80211_hw *hw)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	u32 val32, rsr;
	u8 val8, opmode;
	bool ht = true;
	int sec_ch_above, channel;
	int i;

	opmode = rtl8xxxu_read8(priv, REG_BW_OPMODE);
	rsr = rtl8xxxu_read32(priv, REG_RESPONSE_RATE_SET);
	channel = hw->conf.chandef.chan->hw_value;

	switch (hw->conf.chandef.width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
		ht = false;
		fallthrough;
	case NL80211_CHAN_WIDTH_20:
		opmode |= BW_OPMODE_20MHZ;
		rtl8xxxu_write8(priv, REG_BW_OPMODE, opmode);

		val32 = rtl8xxxu_read32(priv, REG_FPGA0_RF_MODE);
		val32 &= ~FPGA_RF_MODE;
		rtl8xxxu_write32(priv, REG_FPGA0_RF_MODE, val32);

		val32 = rtl8xxxu_read32(priv, REG_FPGA1_RF_MODE);
		val32 &= ~FPGA_RF_MODE;
		rtl8xxxu_write32(priv, REG_FPGA1_RF_MODE, val32);

		val32 = rtl8xxxu_read32(priv, REG_FPGA0_ANALOG2);
		val32 |= FPGA0_ANALOG2_20MHZ;
		rtl8xxxu_write32(priv, REG_FPGA0_ANALOG2, val32);
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
			rsr |= RSR_RSC_UPPER_SUB_CHANNEL;
		else
			rsr |= RSR_RSC_LOWER_SUB_CHANNEL;
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

		val32 = rtl8xxxu_read32(priv, REG_FPGA0_ANALOG2);
		val32 &= ~FPGA0_ANALOG2_20MHZ;
		rtl8xxxu_write32(priv, REG_FPGA0_ANALOG2, val32);

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
		val32 &= ~MODE_AG_CHANNEL_MASK;
		val32 |= channel;
		rtl8xxxu_write_rfreg(priv, i, RF6052_REG_MODE_AG, val32);
	}

	if (ht)
		val8 = 0x0e;
	else
		val8 = 0x0a;

	rtl8xxxu_write8(priv, REG_SIFS_CCK + 1, val8);
	rtl8xxxu_write8(priv, REG_SIFS_OFDM + 1, val8);

	rtl8xxxu_write16(priv, REG_R2T_SIFS, 0x0808);
	rtl8xxxu_write16(priv, REG_T2T_SIFS, 0x0a0a);

	for (i = RF_A; i < priv->rf_paths; i++) {
		val32 = rtl8xxxu_read_rfreg(priv, i, RF6052_REG_MODE_AG);
		if (hw->conf.chandef.width == NL80211_CHAN_WIDTH_40)
			val32 &= ~MODE_AG_CHANNEL_20MHZ;
		else
			val32 |= MODE_AG_CHANNEL_20MHZ;
		rtl8xxxu_write_rfreg(priv, i, RF6052_REG_MODE_AG, val32);
	}
}

void rtl8xxxu_gen2_config_channel(struct ieee80211_hw *hw)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	u32 val32;
	u8 val8, subchannel;
	u16 rf_mode_bw;
	bool ht = true;
	int sec_ch_above, channel;
	int i;

	rf_mode_bw = rtl8xxxu_read16(priv, REG_WMAC_TRXPTCL_CTL);
	rf_mode_bw &= ~WMAC_TRXPTCL_CTL_BW_MASK;
	channel = hw->conf.chandef.chan->hw_value;

/* Hack */
	subchannel = 0;

	switch (hw->conf.chandef.width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
		ht = false;
		fallthrough;
	case NL80211_CHAN_WIDTH_20:
		rf_mode_bw |= WMAC_TRXPTCL_CTL_BW_20;
		subchannel = 0;

		val32 = rtl8xxxu_read32(priv, REG_FPGA0_RF_MODE);
		val32 &= ~FPGA_RF_MODE;
		rtl8xxxu_write32(priv, REG_FPGA0_RF_MODE, val32);

		val32 = rtl8xxxu_read32(priv, REG_FPGA1_RF_MODE);
		val32 &= ~FPGA_RF_MODE;
		rtl8xxxu_write32(priv, REG_FPGA1_RF_MODE, val32);

		val32 = rtl8xxxu_read32(priv, REG_OFDM0_TX_PSDO_NOISE_WEIGHT);
		val32 &= ~(BIT(30) | BIT(31));
		rtl8xxxu_write32(priv, REG_OFDM0_TX_PSDO_NOISE_WEIGHT, val32);

		break;
	case NL80211_CHAN_WIDTH_40:
		rf_mode_bw |= WMAC_TRXPTCL_CTL_BW_40;

		if (hw->conf.chandef.center_freq1 >
		    hw->conf.chandef.chan->center_freq) {
			sec_ch_above = 1;
			channel += 2;
		} else {
			sec_ch_above = 0;
			channel -= 2;
		}

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
	case NL80211_CHAN_WIDTH_80:
		rf_mode_bw |= WMAC_TRXPTCL_CTL_BW_80;
		break;
	default:
		break;
	}

	for (i = RF_A; i < priv->rf_paths; i++) {
		val32 = rtl8xxxu_read_rfreg(priv, i, RF6052_REG_MODE_AG);
		val32 &= ~MODE_AG_CHANNEL_MASK;
		val32 |= channel;
		rtl8xxxu_write_rfreg(priv, i, RF6052_REG_MODE_AG, val32);
	}

	rtl8xxxu_write16(priv, REG_WMAC_TRXPTCL_CTL, rf_mode_bw);
	rtl8xxxu_write8(priv, REG_DATA_SUBCHANNEL, subchannel);

	if (ht)
		val8 = 0x0e;
	else
		val8 = 0x0a;

	rtl8xxxu_write8(priv, REG_SIFS_CCK + 1, val8);
	rtl8xxxu_write8(priv, REG_SIFS_OFDM + 1, val8);

	rtl8xxxu_write16(priv, REG_R2T_SIFS, 0x0808);
	rtl8xxxu_write16(priv, REG_T2T_SIFS, 0x0a0a);

	for (i = RF_A; i < priv->rf_paths; i++) {
		val32 = rtl8xxxu_read_rfreg(priv, i, RF6052_REG_MODE_AG);
		val32 &= ~MODE_AG_BW_MASK;
		switch(hw->conf.chandef.width) {
		case NL80211_CHAN_WIDTH_80:
			val32 |= MODE_AG_BW_80MHZ_8723B;
			break;
		case NL80211_CHAN_WIDTH_40:
			val32 |= MODE_AG_BW_40MHZ_8723B;
			break;
		default:
			val32 |= MODE_AG_BW_20MHZ_8723B;
			break;
		}
		rtl8xxxu_write_rfreg(priv, i, RF6052_REG_MODE_AG, val32);
	}
}

void
rtl8xxxu_gen1_set_tx_power(struct rtl8xxxu_priv *priv, int channel, bool ht40)
{
	struct rtl8xxxu_power_base *power_base = priv->power_base;
	u8 cck[RTL8723A_MAX_RF_PATHS], ofdm[RTL8723A_MAX_RF_PATHS];
	u8 ofdmbase[RTL8723A_MAX_RF_PATHS], mcsbase[RTL8723A_MAX_RF_PATHS];
	u32 val32, ofdm_a, ofdm_b, mcs_a, mcs_b;
	u8 val8;
	int group, i;

	group = rtl8xxxu_gen1_channel_to_group(channel);

	cck[0] = priv->cck_tx_power_index_A[group] - 1;
	cck[1] = priv->cck_tx_power_index_B[group] - 1;

	if (priv->hi_pa) {
		if (cck[0] > 0x20)
			cck[0] = 0x20;
		if (cck[1] > 0x20)
			cck[1] = 0x20;
	}

	ofdm[0] = priv->ht40_1s_tx_power_index_A[group];
	ofdm[1] = priv->ht40_1s_tx_power_index_B[group];
	if (ofdm[0])
		ofdm[0] -= 1;
	if (ofdm[1])
		ofdm[1] -= 1;

	ofdmbase[0] = ofdm[0] +	priv->ofdm_tx_power_index_diff[group].a;
	ofdmbase[1] = ofdm[1] +	priv->ofdm_tx_power_index_diff[group].b;

	mcsbase[0] = ofdm[0];
	mcsbase[1] = ofdm[1];
	if (!ht40) {
		mcsbase[0] += priv->ht20_tx_power_index_diff[group].a;
		mcsbase[1] += priv->ht20_tx_power_index_diff[group].b;
	}

	if (priv->tx_paths > 1) {
		if (ofdm[0] > priv->ht40_2s_tx_power_index_diff[group].a)
			ofdm[0] -=  priv->ht40_2s_tx_power_index_diff[group].a;
		if (ofdm[1] > priv->ht40_2s_tx_power_index_diff[group].b)
			ofdm[1] -=  priv->ht40_2s_tx_power_index_diff[group].b;
	}

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_CHANNEL)
		dev_info(&priv->udev->dev,
			 "%s: Setting TX power CCK A: %02x, "
			 "CCK B: %02x, OFDM A: %02x, OFDM B: %02x\n",
			 __func__, cck[0], cck[1], ofdm[0], ofdm[1]);

	for (i = 0; i < RTL8723A_MAX_RF_PATHS; i++) {
		if (cck[i] > RF6052_MAX_TX_PWR)
			cck[i] = RF6052_MAX_TX_PWR;
		if (ofdm[i] > RF6052_MAX_TX_PWR)
			ofdm[i] = RF6052_MAX_TX_PWR;
	}

	val32 = rtl8xxxu_read32(priv, REG_TX_AGC_A_CCK1_MCS32);
	val32 &= 0xffff00ff;
	val32 |= (cck[0] << 8);
	rtl8xxxu_write32(priv, REG_TX_AGC_A_CCK1_MCS32, val32);

	val32 = rtl8xxxu_read32(priv, REG_TX_AGC_B_CCK11_A_CCK2_11);
	val32 &= 0xff;
	val32 |= ((cck[0] << 8) | (cck[0] << 16) | (cck[0] << 24));
	rtl8xxxu_write32(priv, REG_TX_AGC_B_CCK11_A_CCK2_11, val32);

	val32 = rtl8xxxu_read32(priv, REG_TX_AGC_B_CCK11_A_CCK2_11);
	val32 &= 0xffffff00;
	val32 |= cck[1];
	rtl8xxxu_write32(priv, REG_TX_AGC_B_CCK11_A_CCK2_11, val32);

	val32 = rtl8xxxu_read32(priv, REG_TX_AGC_B_CCK1_55_MCS32);
	val32 &= 0xff;
	val32 |= ((cck[1] << 8) | (cck[1] << 16) | (cck[1] << 24));
	rtl8xxxu_write32(priv, REG_TX_AGC_B_CCK1_55_MCS32, val32);

	ofdm_a = ofdmbase[0] | ofdmbase[0] << 8 |
		ofdmbase[0] << 16 | ofdmbase[0] << 24;
	ofdm_b = ofdmbase[1] | ofdmbase[1] << 8 |
		ofdmbase[1] << 16 | ofdmbase[1] << 24;

	rtl8xxxu_write32(priv, REG_TX_AGC_A_RATE18_06,
			 ofdm_a + power_base->reg_0e00);
	rtl8xxxu_write32(priv, REG_TX_AGC_B_RATE18_06,
			 ofdm_b + power_base->reg_0830);

	rtl8xxxu_write32(priv, REG_TX_AGC_A_RATE54_24,
			 ofdm_a + power_base->reg_0e04);
	rtl8xxxu_write32(priv, REG_TX_AGC_B_RATE54_24,
			 ofdm_b + power_base->reg_0834);

	mcs_a = mcsbase[0] | mcsbase[0] << 8 |
		mcsbase[0] << 16 | mcsbase[0] << 24;
	mcs_b = mcsbase[1] | mcsbase[1] << 8 |
		mcsbase[1] << 16 | mcsbase[1] << 24;

	rtl8xxxu_write32(priv, REG_TX_AGC_A_MCS03_MCS00,
			 mcs_a + power_base->reg_0e10);
	rtl8xxxu_write32(priv, REG_TX_AGC_B_MCS03_MCS00,
			 mcs_b + power_base->reg_083c);

	rtl8xxxu_write32(priv, REG_TX_AGC_A_MCS07_MCS04,
			 mcs_a + power_base->reg_0e14);
	rtl8xxxu_write32(priv, REG_TX_AGC_B_MCS07_MCS04,
			 mcs_b + power_base->reg_0848);

	rtl8xxxu_write32(priv, REG_TX_AGC_A_MCS11_MCS08,
			 mcs_a + power_base->reg_0e18);
	rtl8xxxu_write32(priv, REG_TX_AGC_B_MCS11_MCS08,
			 mcs_b + power_base->reg_084c);

	rtl8xxxu_write32(priv, REG_TX_AGC_A_MCS15_MCS12,
			 mcs_a + power_base->reg_0e1c);
	for (i = 0; i < 3; i++) {
		if (i != 2)
			val8 = (mcsbase[0] > 8) ? (mcsbase[0] - 8) : 0;
		else
			val8 = (mcsbase[0] > 6) ? (mcsbase[0] - 6) : 0;
		rtl8xxxu_write8(priv, REG_OFDM0_XC_TX_IQ_IMBALANCE + i, val8);
	}
	rtl8xxxu_write32(priv, REG_TX_AGC_B_MCS15_MCS12,
			 mcs_b + power_base->reg_0868);
	for (i = 0; i < 3; i++) {
		if (i != 2)
			val8 = (mcsbase[1] > 8) ? (mcsbase[1] - 8) : 0;
		else
			val8 = (mcsbase[1] > 6) ? (mcsbase[1] - 6) : 0;
		rtl8xxxu_write8(priv, REG_OFDM0_XD_TX_IQ_IMBALANCE + i, val8);
	}
}

static void rtl8xxxu_set_linktype(struct rtl8xxxu_priv *priv,
				  enum nl80211_iftype linktype)
{
	u8 val8;

	val8 = rtl8xxxu_read8(priv, REG_MSR);
	val8 &= ~MSR_LINKTYPE_MASK;

	switch (linktype) {
	case NL80211_IFTYPE_UNSPECIFIED:
		val8 |= MSR_LINKTYPE_NONE;
		break;
	case NL80211_IFTYPE_ADHOC:
		val8 |= MSR_LINKTYPE_ADHOC;
		break;
	case NL80211_IFTYPE_STATION:
		val8 |= MSR_LINKTYPE_STATION;
		break;
	case NL80211_IFTYPE_AP:
		val8 |= MSR_LINKTYPE_AP;
		break;
	default:
		goto out;
	}

	rtl8xxxu_write8(priv, REG_MSR, val8);
out:
	return;
}

static void
rtl8xxxu_set_retry(struct rtl8xxxu_priv *priv, u16 short_retry, u16 long_retry)
{
	u16 val16;

	val16 = ((short_retry << RETRY_LIMIT_SHORT_SHIFT) &
		 RETRY_LIMIT_SHORT_MASK) |
		((long_retry << RETRY_LIMIT_LONG_SHIFT) &
		 RETRY_LIMIT_LONG_MASK);

	rtl8xxxu_write16(priv, REG_RETRY_LIMIT, val16);
}

static void
rtl8xxxu_set_spec_sifs(struct rtl8xxxu_priv *priv, u16 cck, u16 ofdm)
{
	u16 val16;

	val16 = ((cck << SPEC_SIFS_CCK_SHIFT) & SPEC_SIFS_CCK_MASK) |
		((ofdm << SPEC_SIFS_OFDM_SHIFT) & SPEC_SIFS_OFDM_MASK);

	rtl8xxxu_write16(priv, REG_SPEC_SIFS, val16);
}

static void rtl8xxxu_print_chipinfo(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	char *cut;

	switch (priv->chip_cut) {
	case 0:
		cut = "A";
		break;
	case 1:
		cut = "B";
		break;
	case 2:
		cut = "C";
		break;
	case 3:
		cut = "D";
		break;
	case 4:
		cut = "E";
		break;
	default:
		cut = "unknown";
	}

	dev_info(dev,
		 "RTL%s rev %s (%s) %iT%iR, TX queues %i, WiFi=%i, BT=%i, GPS=%i, HI PA=%i\n",
		 priv->chip_name, cut, priv->chip_vendor, priv->tx_paths,
		 priv->rx_paths, priv->ep_tx_count, priv->has_wifi,
		 priv->has_bluetooth, priv->has_gps, priv->hi_pa);

	dev_info(dev, "RTL%s MAC: %pM\n", priv->chip_name, priv->mac_addr);
}

static int rtl8xxxu_identify_chip(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	u32 val32, bonding;
	u16 val16;

	val32 = rtl8xxxu_read32(priv, REG_SYS_CFG);
	priv->chip_cut = (val32 & SYS_CFG_CHIP_VERSION_MASK) >>
		SYS_CFG_CHIP_VERSION_SHIFT;
	if (val32 & SYS_CFG_TRP_VAUX_EN) {
		dev_info(dev, "Unsupported test chip\n");
		return -ENOTSUPP;
	}

	if (val32 & SYS_CFG_BT_FUNC) {
		if (priv->chip_cut >= 3) {
			sprintf(priv->chip_name, "8723BU");
			priv->rtl_chip = RTL8723B;
		} else {
			sprintf(priv->chip_name, "8723AU");
			priv->usb_interrupts = 1;
			priv->rtl_chip = RTL8723A;
		}

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
	} else if (val32 & SYS_CFG_TYPE_ID) {
		bonding = rtl8xxxu_read32(priv, REG_HPON_FSM);
		bonding &= HPON_FSM_BONDING_MASK;
		if (priv->fops->tx_desc_size ==
		    sizeof(struct rtl8xxxu_txdesc40)) {
			if (bonding == HPON_FSM_BONDING_1T2R) {
				sprintf(priv->chip_name, "8191EU");
				priv->rf_paths = 2;
				priv->rx_paths = 2;
				priv->tx_paths = 1;
				priv->rtl_chip = RTL8191E;
			} else {
				sprintf(priv->chip_name, "8192EU");
				priv->rf_paths = 2;
				priv->rx_paths = 2;
				priv->tx_paths = 2;
				priv->rtl_chip = RTL8192E;
			}
		} else if (bonding == HPON_FSM_BONDING_1T2R) {
			sprintf(priv->chip_name, "8191CU");
			priv->rf_paths = 2;
			priv->rx_paths = 2;
			priv->tx_paths = 1;
			priv->usb_interrupts = 1;
			priv->rtl_chip = RTL8191C;
		} else {
			sprintf(priv->chip_name, "8192CU");
			priv->rf_paths = 2;
			priv->rx_paths = 2;
			priv->tx_paths = 2;
			priv->usb_interrupts = 0;
			priv->rtl_chip = RTL8192C;
		}
		priv->has_wifi = 1;
	} else {
		sprintf(priv->chip_name, "8188CU");
		priv->rf_paths = 1;
		priv->rx_paths = 1;
		priv->tx_paths = 1;
		priv->rtl_chip = RTL8188C;
		priv->usb_interrupts = 0;
		priv->has_wifi = 1;
	}

	switch (priv->rtl_chip) {
	case RTL8188E:
	case RTL8192E:
	case RTL8723B:
		switch (val32 & SYS_CFG_VENDOR_EXT_MASK) {
		case SYS_CFG_VENDOR_ID_TSMC:
			sprintf(priv->chip_vendor, "TSMC");
			break;
		case SYS_CFG_VENDOR_ID_SMIC:
			sprintf(priv->chip_vendor, "SMIC");
			priv->vendor_smic = 1;
			break;
		case SYS_CFG_VENDOR_ID_UMC:
			sprintf(priv->chip_vendor, "UMC");
			priv->vendor_umc = 1;
			break;
		default:
			sprintf(priv->chip_vendor, "unknown");
		}
		break;
	default:
		if (val32 & SYS_CFG_VENDOR_ID) {
			sprintf(priv->chip_vendor, "UMC");
			priv->vendor_umc = 1;
		} else {
			sprintf(priv->chip_vendor, "TSMC");
		}
	}

	val32 = rtl8xxxu_read32(priv, REG_GPIO_OUTSTS);
	priv->rom_rev = (val32 & GPIO_RF_RL_ID) >> 28;

	val16 = rtl8xxxu_read16(priv, REG_NORMAL_SIE_EP_TX);
	if (val16 & NORMAL_SIE_EP_TX_HIGH_MASK) {
		priv->ep_tx_high_queue = 1;
		priv->ep_tx_count++;
	}

	if (val16 & NORMAL_SIE_EP_TX_NORMAL_MASK) {
		priv->ep_tx_normal_queue = 1;
		priv->ep_tx_count++;
	}

	if (val16 & NORMAL_SIE_EP_TX_LOW_MASK) {
		priv->ep_tx_low_queue = 1;
		priv->ep_tx_count++;
	}

	/*
	 * Fallback for devices that do not provide REG_NORMAL_SIE_EP_TX
	 */
	if (!priv->ep_tx_count) {
		switch (priv->nr_out_eps) {
		case 4:
		case 3:
			priv->ep_tx_low_queue = 1;
			priv->ep_tx_count++;
			fallthrough;
		case 2:
			priv->ep_tx_normal_queue = 1;
			priv->ep_tx_count++;
			fallthrough;
		case 1:
			priv->ep_tx_high_queue = 1;
			priv->ep_tx_count++;
			break;
		default:
			dev_info(dev, "Unsupported USB TX end-points\n");
			return -ENOTSUPP;
		}
	}

	return 0;
}

static int
rtl8xxxu_read_efuse8(struct rtl8xxxu_priv *priv, u16 offset, u8 *data)
{
	int i;
	u8 val8;
	u32 val32;

	/* Write Address */
	rtl8xxxu_write8(priv, REG_EFUSE_CTRL + 1, offset & 0xff);
	val8 = rtl8xxxu_read8(priv, REG_EFUSE_CTRL + 2);
	val8 &= 0xfc;
	val8 |= (offset >> 8) & 0x03;
	rtl8xxxu_write8(priv, REG_EFUSE_CTRL + 2, val8);

	val8 = rtl8xxxu_read8(priv, REG_EFUSE_CTRL + 3);
	rtl8xxxu_write8(priv, REG_EFUSE_CTRL + 3, val8 & 0x7f);

	/* Poll for data read */
	val32 = rtl8xxxu_read32(priv, REG_EFUSE_CTRL);
	for (i = 0; i < RTL8XXXU_MAX_REG_POLL; i++) {
		val32 = rtl8xxxu_read32(priv, REG_EFUSE_CTRL);
		if (val32 & BIT(31))
			break;
	}

	if (i == RTL8XXXU_MAX_REG_POLL)
		return -EIO;

	udelay(50);
	val32 = rtl8xxxu_read32(priv, REG_EFUSE_CTRL);

	*data = val32 & 0xff;
	return 0;
}

static int rtl8xxxu_read_efuse(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	int i, ret = 0;
	u8 val8, word_mask, header, extheader;
	u16 val16, efuse_addr, offset;
	u32 val32;

	val16 = rtl8xxxu_read16(priv, REG_9346CR);
	if (val16 & EEPROM_ENABLE)
		priv->has_eeprom = 1;
	if (val16 & EEPROM_BOOT)
		priv->boot_eeprom = 1;

	if (priv->is_multi_func) {
		val32 = rtl8xxxu_read32(priv, REG_EFUSE_TEST);
		val32 = (val32 & ~EFUSE_SELECT_MASK) | EFUSE_WIFI_SELECT;
		rtl8xxxu_write32(priv, REG_EFUSE_TEST, val32);
	}

	dev_dbg(dev, "Booting from %s\n",
		priv->boot_eeprom ? "EEPROM" : "EFUSE");

	rtl8xxxu_write8(priv, REG_EFUSE_ACCESS, EFUSE_ACCESS_ENABLE);

	/*  1.2V Power: From VDDON with Power Cut(0x0000[15]), default valid */
	val16 = rtl8xxxu_read16(priv, REG_SYS_ISO_CTRL);
	if (!(val16 & SYS_ISO_PWC_EV12V)) {
		val16 |= SYS_ISO_PWC_EV12V;
		rtl8xxxu_write16(priv, REG_SYS_ISO_CTRL, val16);
	}
	/*  Reset: 0x0000[28], default valid */
	val16 = rtl8xxxu_read16(priv, REG_SYS_FUNC);
	if (!(val16 & SYS_FUNC_ELDR)) {
		val16 |= SYS_FUNC_ELDR;
		rtl8xxxu_write16(priv, REG_SYS_FUNC, val16);
	}

	/*
	 * Clock: Gated(0x0008[5]) 8M(0x0008[1]) clock from ANA, default valid
	 */
	val16 = rtl8xxxu_read16(priv, REG_SYS_CLKR);
	if (!(val16 & SYS_CLK_LOADER_ENABLE) || !(val16 & SYS_CLK_ANA8M)) {
		val16 |= (SYS_CLK_LOADER_ENABLE | SYS_CLK_ANA8M);
		rtl8xxxu_write16(priv, REG_SYS_CLKR, val16);
	}

	/* Default value is 0xff */
	memset(priv->efuse_wifi.raw, 0xff, EFUSE_MAP_LEN);

	efuse_addr = 0;
	while (efuse_addr < EFUSE_REAL_CONTENT_LEN_8723A) {
		u16 map_addr;

		ret = rtl8xxxu_read_efuse8(priv, efuse_addr++, &header);
		if (ret || header == 0xff)
			goto exit;

		if ((header & 0x1f) == 0x0f) {	/* extended header */
			offset = (header & 0xe0) >> 5;

			ret = rtl8xxxu_read_efuse8(priv, efuse_addr++,
						   &extheader);
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

			ret = rtl8xxxu_read_efuse8(priv, efuse_addr++, &val8);
			if (ret)
				goto exit;
			if (map_addr >= EFUSE_MAP_LEN - 1) {
				dev_warn(dev, "%s: Illegal map_addr (%04x), "
					 "efuse corrupt!\n",
					 __func__, map_addr);
				ret = -EINVAL;
				goto exit;
			}
			priv->efuse_wifi.raw[map_addr++] = val8;

			ret = rtl8xxxu_read_efuse8(priv, efuse_addr++, &val8);
			if (ret)
				goto exit;
			priv->efuse_wifi.raw[map_addr++] = val8;
		}
	}

exit:
	rtl8xxxu_write8(priv, REG_EFUSE_ACCESS, EFUSE_ACCESS_DISABLE);

	return ret;
}

void rtl8xxxu_reset_8051(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u16 sys_func;

	val8 = rtl8xxxu_read8(priv, REG_RSV_CTRL + 1);
	val8 &= ~BIT(0);
	rtl8xxxu_write8(priv, REG_RSV_CTRL + 1, val8);

	sys_func = rtl8xxxu_read16(priv, REG_SYS_FUNC);
	sys_func &= ~SYS_FUNC_CPU_ENABLE;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, sys_func);

	val8 = rtl8xxxu_read8(priv, REG_RSV_CTRL + 1);
	val8 |= BIT(0);
	rtl8xxxu_write8(priv, REG_RSV_CTRL + 1, val8);

	sys_func |= SYS_FUNC_CPU_ENABLE;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, sys_func);
}

static int rtl8xxxu_start_firmware(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	int ret = 0, i;
	u32 val32;

	/* Poll checksum report */
	for (i = 0; i < RTL8XXXU_FIRMWARE_POLL_MAX; i++) {
		val32 = rtl8xxxu_read32(priv, REG_MCU_FW_DL);
		if (val32 & MCU_FW_DL_CSUM_REPORT)
			break;
	}

	if (i == RTL8XXXU_FIRMWARE_POLL_MAX) {
		dev_warn(dev, "Firmware checksum poll timed out\n");
		ret = -EAGAIN;
		goto exit;
	}

	val32 = rtl8xxxu_read32(priv, REG_MCU_FW_DL);
	val32 |= MCU_FW_DL_READY;
	val32 &= ~MCU_WINT_INIT_READY;
	rtl8xxxu_write32(priv, REG_MCU_FW_DL, val32);

	/*
	 * Reset the 8051 in order for the firmware to start running,
	 * otherwise it won't come up on the 8192eu
	 */
	priv->fops->reset_8051(priv);

	/* Wait for firmware to become ready */
	for (i = 0; i < RTL8XXXU_FIRMWARE_POLL_MAX; i++) {
		val32 = rtl8xxxu_read32(priv, REG_MCU_FW_DL);
		if (val32 & MCU_WINT_INIT_READY)
			break;

		udelay(100);
	}

	if (i == RTL8XXXU_FIRMWARE_POLL_MAX) {
		dev_warn(dev, "Firmware failed to start\n");
		ret = -EAGAIN;
		goto exit;
	}

	/*
	 * Init H2C command
	 */
	if (priv->rtl_chip == RTL8723B)
		rtl8xxxu_write8(priv, REG_HMTFR, 0x0f);
exit:
	return ret;
}

static int rtl8xxxu_download_firmware(struct rtl8xxxu_priv *priv)
{
	int pages, remainder, i, ret;
	u8 val8;
	u16 val16;
	u32 val32;
	u8 *fwptr;

	val8 = rtl8xxxu_read8(priv, REG_SYS_FUNC + 1);
	val8 |= 4;
	rtl8xxxu_write8(priv, REG_SYS_FUNC + 1, val8);

	/* 8051 enable */
	val16 = rtl8xxxu_read16(priv, REG_SYS_FUNC);
	val16 |= SYS_FUNC_CPU_ENABLE;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, val16);

	val8 = rtl8xxxu_read8(priv, REG_MCU_FW_DL);
	if (val8 & MCU_FW_RAM_SEL) {
		pr_info("do the RAM reset\n");
		rtl8xxxu_write8(priv, REG_MCU_FW_DL, 0x00);
		priv->fops->reset_8051(priv);
	}

	/* MCU firmware download enable */
	val8 = rtl8xxxu_read8(priv, REG_MCU_FW_DL);
	val8 |= MCU_FW_DL_ENABLE;
	rtl8xxxu_write8(priv, REG_MCU_FW_DL, val8);

	/* 8051 reset */
	val32 = rtl8xxxu_read32(priv, REG_MCU_FW_DL);
	val32 &= ~BIT(19);
	rtl8xxxu_write32(priv, REG_MCU_FW_DL, val32);

	/* Reset firmware download checksum */
	val8 = rtl8xxxu_read8(priv, REG_MCU_FW_DL);
	val8 |= MCU_FW_DL_CSUM_REPORT;
	rtl8xxxu_write8(priv, REG_MCU_FW_DL, val8);

	pages = priv->fw_size / RTL_FW_PAGE_SIZE;
	remainder = priv->fw_size % RTL_FW_PAGE_SIZE;

	fwptr = priv->fw_data->data;

	for (i = 0; i < pages; i++) {
		val8 = rtl8xxxu_read8(priv, REG_MCU_FW_DL + 2) & 0xF8;
		val8 |= i;
		rtl8xxxu_write8(priv, REG_MCU_FW_DL + 2, val8);

		ret = rtl8xxxu_writeN(priv, REG_FW_START_ADDRESS,
				      fwptr, RTL_FW_PAGE_SIZE);
		if (ret != RTL_FW_PAGE_SIZE) {
			ret = -EAGAIN;
			goto fw_abort;
		}

		fwptr += RTL_FW_PAGE_SIZE;
	}

	if (remainder) {
		val8 = rtl8xxxu_read8(priv, REG_MCU_FW_DL + 2) & 0xF8;
		val8 |= i;
		rtl8xxxu_write8(priv, REG_MCU_FW_DL + 2, val8);
		ret = rtl8xxxu_writeN(priv, REG_FW_START_ADDRESS,
				      fwptr, remainder);
		if (ret != remainder) {
			ret = -EAGAIN;
			goto fw_abort;
		}
	}

	ret = 0;
fw_abort:
	/* MCU firmware download disable */
	val16 = rtl8xxxu_read16(priv, REG_MCU_FW_DL);
	val16 &= ~MCU_FW_DL_ENABLE;
	rtl8xxxu_write16(priv, REG_MCU_FW_DL, val16);

	return ret;
}

int rtl8xxxu_load_firmware(struct rtl8xxxu_priv *priv, char *fw_name)
{
	struct device *dev = &priv->udev->dev;
	const struct firmware *fw;
	int ret = 0;
	u16 signature;

	dev_info(dev, "%s: Loading firmware %s\n", DRIVER_NAME, fw_name);
	if (request_firmware(&fw, fw_name, &priv->udev->dev)) {
		dev_warn(dev, "request_firmware(%s) failed\n", fw_name);
		ret = -EAGAIN;
		goto exit;
	}
	if (!fw) {
		dev_warn(dev, "Firmware data not available\n");
		ret = -EINVAL;
		goto exit;
	}

	priv->fw_data = kmemdup(fw->data, fw->size, GFP_KERNEL);
	if (!priv->fw_data) {
		ret = -ENOMEM;
		goto exit;
	}
	priv->fw_size = fw->size - sizeof(struct rtl8xxxu_firmware_header);

	signature = le16_to_cpu(priv->fw_data->signature);
	switch (signature & 0xfff0) {
	case 0x92e0:
	case 0x92c0:
	case 0x88c0:
	case 0x5300:
	case 0x2300:
		break;
	default:
		ret = -EINVAL;
		dev_warn(dev, "%s: Invalid firmware signature: 0x%04x\n",
			 __func__, signature);
	}

	dev_info(dev, "Firmware revision %i.%i (signature 0x%04x)\n",
		 le16_to_cpu(priv->fw_data->major_version),
		 priv->fw_data->minor_version, signature);

exit:
	release_firmware(fw);
	return ret;
}

void rtl8xxxu_firmware_self_reset(struct rtl8xxxu_priv *priv)
{
	u16 val16;
	int i = 100;

	/* Inform 8051 to perform reset */
	rtl8xxxu_write8(priv, REG_HMTFR + 3, 0x20);

	for (i = 100; i > 0; i--) {
		val16 = rtl8xxxu_read16(priv, REG_SYS_FUNC);

		if (!(val16 & SYS_FUNC_CPU_ENABLE)) {
			dev_dbg(&priv->udev->dev,
				"%s: Firmware self reset success!\n", __func__);
			break;
		}
		udelay(50);
	}

	if (!i) {
		/* Force firmware reset */
		val16 = rtl8xxxu_read16(priv, REG_SYS_FUNC);
		val16 &= ~SYS_FUNC_CPU_ENABLE;
		rtl8xxxu_write16(priv, REG_SYS_FUNC, val16);
	}
}

static int
rtl8xxxu_init_mac(struct rtl8xxxu_priv *priv)
{
	struct rtl8xxxu_reg8val *array = priv->fops->mactable;
	int i, ret;
	u16 reg;
	u8 val;

	for (i = 0; ; i++) {
		reg = array[i].reg;
		val = array[i].val;

		if (reg == 0xffff && val == 0xff)
			break;

		ret = rtl8xxxu_write8(priv, reg, val);
		if (ret != 1) {
			dev_warn(&priv->udev->dev,
				 "Failed to initialize MAC "
				 "(reg: %04x, val %02x)\n", reg, val);
			return -EAGAIN;
		}
	}

	if (priv->rtl_chip != RTL8723B && priv->rtl_chip != RTL8192E)
		rtl8xxxu_write8(priv, REG_MAX_AGGR_NUM, 0x0a);

	return 0;
}

int rtl8xxxu_init_phy_regs(struct rtl8xxxu_priv *priv,
			   struct rtl8xxxu_reg32val *array)
{
	int i, ret;
	u16 reg;
	u32 val;

	for (i = 0; ; i++) {
		reg = array[i].reg;
		val = array[i].val;

		if (reg == 0xffff && val == 0xffffffff)
			break;

		ret = rtl8xxxu_write32(priv, reg, val);
		if (ret != sizeof(val)) {
			dev_warn(&priv->udev->dev,
				 "Failed to initialize PHY\n");
			return -EAGAIN;
		}
		udelay(1);
	}

	return 0;
}

void rtl8xxxu_gen1_init_phy_bb(struct rtl8xxxu_priv *priv)
{
	u8 val8, ldoa15, ldov12d, lpldo, ldohci12;
	u16 val16;
	u32 val32;

	val8 = rtl8xxxu_read8(priv, REG_AFE_PLL_CTRL);
	udelay(2);
	val8 |= AFE_PLL_320_ENABLE;
	rtl8xxxu_write8(priv, REG_AFE_PLL_CTRL, val8);
	udelay(2);

	rtl8xxxu_write8(priv, REG_AFE_PLL_CTRL + 1, 0xff);
	udelay(2);

	val16 = rtl8xxxu_read16(priv, REG_SYS_FUNC);
	val16 |= SYS_FUNC_BB_GLB_RSTN | SYS_FUNC_BBRSTB;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, val16);

	val32 = rtl8xxxu_read32(priv, REG_AFE_XTAL_CTRL);
	val32 &= ~AFE_XTAL_RF_GATE;
	if (priv->has_bluetooth)
		val32 &= ~AFE_XTAL_BT_GATE;
	rtl8xxxu_write32(priv, REG_AFE_XTAL_CTRL, val32);

	/* 6. 0x1f[7:0] = 0x07 */
	val8 = RF_ENABLE | RF_RSTB | RF_SDMRSTB;
	rtl8xxxu_write8(priv, REG_RF_CTRL, val8);

	if (priv->hi_pa)
		rtl8xxxu_init_phy_regs(priv, rtl8188ru_phy_1t_highpa_table);
	else if (priv->tx_paths == 2)
		rtl8xxxu_init_phy_regs(priv, rtl8192cu_phy_2t_init_table);
	else
		rtl8xxxu_init_phy_regs(priv, rtl8723a_phy_1t_init_table);

	if (priv->rtl_chip == RTL8188R && priv->hi_pa &&
	    priv->vendor_umc && priv->chip_cut == 1)
		rtl8xxxu_write8(priv, REG_OFDM0_AGC_PARM1 + 2, 0x50);

	if (priv->hi_pa)
		rtl8xxxu_init_phy_regs(priv, rtl8xxx_agc_highpa_table);
	else
		rtl8xxxu_init_phy_regs(priv, rtl8xxx_agc_standard_table);

	ldoa15 = LDOA15_ENABLE | LDOA15_OBUF;
	ldov12d = LDOV12D_ENABLE | BIT(2) | (2 << LDOV12D_VADJ_SHIFT);
	ldohci12 = 0x57;
	lpldo = 1;
	val32 = (lpldo << 24) | (ldohci12 << 16) | (ldov12d << 8) | ldoa15;
	rtl8xxxu_write32(priv, REG_LDOA15_CTRL, val32);
}

/*
 * Most of this is black magic retrieved from the old rtl8723au driver
 */
static int rtl8xxxu_init_phy_bb(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u32 val32;

	priv->fops->init_phy_bb(priv);

	if (priv->tx_paths == 1 && priv->rx_paths == 2) {
		/*
		 * For 1T2R boards, patch the registers.
		 *
		 * It looks like 8191/2 1T2R boards use path B for TX
		 */
		val32 = rtl8xxxu_read32(priv, REG_FPGA0_TX_INFO);
		val32 &= ~(BIT(0) | BIT(1));
		val32 |= BIT(1);
		rtl8xxxu_write32(priv, REG_FPGA0_TX_INFO, val32);

		val32 = rtl8xxxu_read32(priv, REG_FPGA1_TX_INFO);
		val32 &= ~0x300033;
		val32 |= 0x200022;
		rtl8xxxu_write32(priv, REG_FPGA1_TX_INFO, val32);

		val32 = rtl8xxxu_read32(priv, REG_CCK0_AFE_SETTING);
		val32 &= ~CCK0_AFE_RX_MASK;
		val32 &= 0x00ffffff;
		val32 |= 0x40000000;
		val32 |= CCK0_AFE_RX_ANT_B;
		rtl8xxxu_write32(priv, REG_CCK0_AFE_SETTING, val32);

		val32 = rtl8xxxu_read32(priv, REG_OFDM0_TRX_PATH_ENABLE);
		val32 &= ~(OFDM_RF_PATH_RX_MASK | OFDM_RF_PATH_TX_MASK);
		val32 |= (OFDM_RF_PATH_RX_A | OFDM_RF_PATH_RX_B |
			  OFDM_RF_PATH_TX_B);
		rtl8xxxu_write32(priv, REG_OFDM0_TRX_PATH_ENABLE, val32);

		val32 = rtl8xxxu_read32(priv, REG_OFDM0_AGC_PARM1);
		val32 &= ~(BIT(4) | BIT(5));
		val32 |= BIT(4);
		rtl8xxxu_write32(priv, REG_OFDM0_AGC_PARM1, val32);

		val32 = rtl8xxxu_read32(priv, REG_TX_CCK_RFON);
		val32 &= ~(BIT(27) | BIT(26));
		val32 |= BIT(27);
		rtl8xxxu_write32(priv, REG_TX_CCK_RFON, val32);

		val32 = rtl8xxxu_read32(priv, REG_TX_CCK_BBON);
		val32 &= ~(BIT(27) | BIT(26));
		val32 |= BIT(27);
		rtl8xxxu_write32(priv, REG_TX_CCK_BBON, val32);

		val32 = rtl8xxxu_read32(priv, REG_TX_OFDM_RFON);
		val32 &= ~(BIT(27) | BIT(26));
		val32 |= BIT(27);
		rtl8xxxu_write32(priv, REG_TX_OFDM_RFON, val32);

		val32 = rtl8xxxu_read32(priv, REG_TX_OFDM_BBON);
		val32 &= ~(BIT(27) | BIT(26));
		val32 |= BIT(27);
		rtl8xxxu_write32(priv, REG_TX_OFDM_BBON, val32);

		val32 = rtl8xxxu_read32(priv, REG_TX_TO_TX);
		val32 &= ~(BIT(27) | BIT(26));
		val32 |= BIT(27);
		rtl8xxxu_write32(priv, REG_TX_TO_TX, val32);
	}

	if (priv->has_xtalk) {
		val32 = rtl8xxxu_read32(priv, REG_MAC_PHY_CTRL);

		val8 = priv->xtalk;
		val32 &= 0xff000fff;
		val32 |= ((val8 | (val8 << 6)) << 12);

		rtl8xxxu_write32(priv, REG_MAC_PHY_CTRL, val32);
	}

	if (priv->rtl_chip == RTL8192E)
		rtl8xxxu_write32(priv, REG_AFE_XTAL_CTRL, 0x000f81fb);

	return 0;
}

static int rtl8xxxu_init_rf_regs(struct rtl8xxxu_priv *priv,
				 struct rtl8xxxu_rfregval *array,
				 enum rtl8xxxu_rfpath path)
{
	int i, ret;
	u8 reg;
	u32 val;

	for (i = 0; ; i++) {
		reg = array[i].reg;
		val = array[i].val;

		if (reg == 0xff && val == 0xffffffff)
			break;

		switch (reg) {
		case 0xfe:
			msleep(50);
			continue;
		case 0xfd:
			mdelay(5);
			continue;
		case 0xfc:
			mdelay(1);
			continue;
		case 0xfb:
			udelay(50);
			continue;
		case 0xfa:
			udelay(5);
			continue;
		case 0xf9:
			udelay(1);
			continue;
		}

		ret = rtl8xxxu_write_rfreg(priv, path, reg, val);
		if (ret) {
			dev_warn(&priv->udev->dev,
				 "Failed to initialize RF\n");
			return -EAGAIN;
		}
		udelay(1);
	}

	return 0;
}

int rtl8xxxu_init_phy_rf(struct rtl8xxxu_priv *priv,
			 struct rtl8xxxu_rfregval *table,
			 enum rtl8xxxu_rfpath path)
{
	u32 val32;
	u16 val16, rfsi_rfenv;
	u16 reg_sw_ctrl, reg_int_oe, reg_hssi_parm2;

	switch (path) {
	case RF_A:
		reg_sw_ctrl = REG_FPGA0_XA_RF_SW_CTRL;
		reg_int_oe = REG_FPGA0_XA_RF_INT_OE;
		reg_hssi_parm2 = REG_FPGA0_XA_HSSI_PARM2;
		break;
	case RF_B:
		reg_sw_ctrl = REG_FPGA0_XB_RF_SW_CTRL;
		reg_int_oe = REG_FPGA0_XB_RF_INT_OE;
		reg_hssi_parm2 = REG_FPGA0_XB_HSSI_PARM2;
		break;
	default:
		dev_err(&priv->udev->dev, "%s:Unsupported RF path %c\n",
			__func__, path + 'A');
		return -EINVAL;
	}
	/* For path B, use XB */
	rfsi_rfenv = rtl8xxxu_read16(priv, reg_sw_ctrl);
	rfsi_rfenv &= FPGA0_RF_RFENV;

	/*
	 * These two we might be able to optimize into one
	 */
	val32 = rtl8xxxu_read32(priv, reg_int_oe);
	val32 |= BIT(20);	/* 0x10 << 16 */
	rtl8xxxu_write32(priv, reg_int_oe, val32);
	udelay(1);

	val32 = rtl8xxxu_read32(priv, reg_int_oe);
	val32 |= BIT(4);
	rtl8xxxu_write32(priv, reg_int_oe, val32);
	udelay(1);

	/*
	 * These two we might be able to optimize into one
	 */
	val32 = rtl8xxxu_read32(priv, reg_hssi_parm2);
	val32 &= ~FPGA0_HSSI_3WIRE_ADDR_LEN;
	rtl8xxxu_write32(priv, reg_hssi_parm2, val32);
	udelay(1);

	val32 = rtl8xxxu_read32(priv, reg_hssi_parm2);
	val32 &= ~FPGA0_HSSI_3WIRE_DATA_LEN;
	rtl8xxxu_write32(priv, reg_hssi_parm2, val32);
	udelay(1);

	rtl8xxxu_init_rf_regs(priv, table, path);

	/* For path B, use XB */
	val16 = rtl8xxxu_read16(priv, reg_sw_ctrl);
	val16 &= ~FPGA0_RF_RFENV;
	val16 |= rfsi_rfenv;
	rtl8xxxu_write16(priv, reg_sw_ctrl, val16);

	return 0;
}

static int rtl8xxxu_llt_write(struct rtl8xxxu_priv *priv, u8 address, u8 data)
{
	int ret = -EBUSY;
	int count = 0;
	u32 value;

	value = LLT_OP_WRITE | address << 8 | data;

	rtl8xxxu_write32(priv, REG_LLT_INIT, value);

	do {
		value = rtl8xxxu_read32(priv, REG_LLT_INIT);
		if ((value & LLT_OP_MASK) == LLT_OP_INACTIVE) {
			ret = 0;
			break;
		}
	} while (count++ < 20);

	return ret;
}

int rtl8xxxu_init_llt_table(struct rtl8xxxu_priv *priv)
{
	int ret;
	int i;
	u8 last_tx_page;

	last_tx_page = priv->fops->total_page_num;

	for (i = 0; i < last_tx_page; i++) {
		ret = rtl8xxxu_llt_write(priv, i, i + 1);
		if (ret)
			goto exit;
	}

	ret = rtl8xxxu_llt_write(priv, last_tx_page, 0xff);
	if (ret)
		goto exit;

	/* Mark remaining pages as a ring buffer */
	for (i = last_tx_page + 1; i < 0xff; i++) {
		ret = rtl8xxxu_llt_write(priv, i, (i + 1));
		if (ret)
			goto exit;
	}

	/*  Let last entry point to the start entry of ring buffer */
	ret = rtl8xxxu_llt_write(priv, 0xff, last_tx_page + 1);
	if (ret)
		goto exit;

exit:
	return ret;
}

int rtl8xxxu_auto_llt_table(struct rtl8xxxu_priv *priv)
{
	u32 val32;
	int ret = 0;
	int i;

	val32 = rtl8xxxu_read32(priv, REG_AUTO_LLT);
	val32 |= AUTO_LLT_INIT_LLT;
	rtl8xxxu_write32(priv, REG_AUTO_LLT, val32);

	for (i = 500; i; i--) {
		val32 = rtl8xxxu_read32(priv, REG_AUTO_LLT);
		if (!(val32 & AUTO_LLT_INIT_LLT))
			break;
		usleep_range(2, 4);
	}

	if (!i) {
		ret = -EBUSY;
		dev_warn(&priv->udev->dev, "LLT table init failed\n");
	}

	return ret;
}

static int rtl8xxxu_init_queue_priority(struct rtl8xxxu_priv *priv)
{
	u16 val16, hi, lo;
	u16 hiq, mgq, bkq, beq, viq, voq;
	int hip, mgp, bkp, bep, vip, vop;
	int ret = 0;

	switch (priv->ep_tx_count) {
	case 1:
		if (priv->ep_tx_high_queue) {
			hi = TRXDMA_QUEUE_HIGH;
		} else if (priv->ep_tx_low_queue) {
			hi = TRXDMA_QUEUE_LOW;
		} else if (priv->ep_tx_normal_queue) {
			hi = TRXDMA_QUEUE_NORMAL;
		} else {
			hi = 0;
			ret = -EINVAL;
		}

		hiq = hi;
		mgq = hi;
		bkq = hi;
		beq = hi;
		viq = hi;
		voq = hi;

		hip = 0;
		mgp = 0;
		bkp = 0;
		bep = 0;
		vip = 0;
		vop = 0;
		break;
	case 2:
		if (priv->ep_tx_high_queue && priv->ep_tx_low_queue) {
			hi = TRXDMA_QUEUE_HIGH;
			lo = TRXDMA_QUEUE_LOW;
		} else if (priv->ep_tx_normal_queue && priv->ep_tx_low_queue) {
			hi = TRXDMA_QUEUE_NORMAL;
			lo = TRXDMA_QUEUE_LOW;
		} else if (priv->ep_tx_high_queue && priv->ep_tx_normal_queue) {
			hi = TRXDMA_QUEUE_HIGH;
			lo = TRXDMA_QUEUE_NORMAL;
		} else {
			ret = -EINVAL;
			hi = 0;
			lo = 0;
		}

		hiq = hi;
		mgq = hi;
		bkq = lo;
		beq = lo;
		viq = hi;
		voq = hi;

		hip = 0;
		mgp = 0;
		bkp = 1;
		bep = 1;
		vip = 0;
		vop = 0;
		break;
	case 3:
		beq = TRXDMA_QUEUE_LOW;
		bkq = TRXDMA_QUEUE_LOW;
		viq = TRXDMA_QUEUE_NORMAL;
		voq = TRXDMA_QUEUE_HIGH;
		mgq = TRXDMA_QUEUE_HIGH;
		hiq = TRXDMA_QUEUE_HIGH;

		hip = hiq ^ 3;
		mgp = mgq ^ 3;
		bkp = bkq ^ 3;
		bep = beq ^ 3;
		vip = viq ^ 3;
		vop = viq ^ 3;
		break;
	default:
		ret = -EINVAL;
	}

	/*
	 * None of the vendor drivers are configuring the beacon
	 * queue here .... why?
	 */
	if (!ret) {
		val16 = rtl8xxxu_read16(priv, REG_TRXDMA_CTRL);
		val16 &= 0x7;
		val16 |= (voq << TRXDMA_CTRL_VOQ_SHIFT) |
			(viq << TRXDMA_CTRL_VIQ_SHIFT) |
			(beq << TRXDMA_CTRL_BEQ_SHIFT) |
			(bkq << TRXDMA_CTRL_BKQ_SHIFT) |
			(mgq << TRXDMA_CTRL_MGQ_SHIFT) |
			(hiq << TRXDMA_CTRL_HIQ_SHIFT);
		rtl8xxxu_write16(priv, REG_TRXDMA_CTRL, val16);

		priv->pipe_out[TXDESC_QUEUE_VO] =
			usb_sndbulkpipe(priv->udev, priv->out_ep[vop]);
		priv->pipe_out[TXDESC_QUEUE_VI] =
			usb_sndbulkpipe(priv->udev, priv->out_ep[vip]);
		priv->pipe_out[TXDESC_QUEUE_BE] =
			usb_sndbulkpipe(priv->udev, priv->out_ep[bep]);
		priv->pipe_out[TXDESC_QUEUE_BK] =
			usb_sndbulkpipe(priv->udev, priv->out_ep[bkp]);
		priv->pipe_out[TXDESC_QUEUE_BEACON] =
			usb_sndbulkpipe(priv->udev, priv->out_ep[0]);
		priv->pipe_out[TXDESC_QUEUE_MGNT] =
			usb_sndbulkpipe(priv->udev, priv->out_ep[mgp]);
		priv->pipe_out[TXDESC_QUEUE_HIGH] =
			usb_sndbulkpipe(priv->udev, priv->out_ep[hip]);
		priv->pipe_out[TXDESC_QUEUE_CMD] =
			usb_sndbulkpipe(priv->udev, priv->out_ep[0]);
	}

	return ret;
}

void rtl8xxxu_fill_iqk_matrix_a(struct rtl8xxxu_priv *priv, bool iqk_ok,
				int result[][8], int candidate, bool tx_only)
{
	u32 oldval, x, tx0_a, reg;
	int y, tx0_c;
	u32 val32;

	if (!iqk_ok)
		return;

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XA_TX_IQ_IMBALANCE);
	oldval = val32 >> 22;

	x = result[candidate][0];
	if ((x & 0x00000200) != 0)
		x = x | 0xfffffc00;
	tx0_a = (x * oldval) >> 8;

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XA_TX_IQ_IMBALANCE);
	val32 &= ~0x3ff;
	val32 |= tx0_a;
	rtl8xxxu_write32(priv, REG_OFDM0_XA_TX_IQ_IMBALANCE, val32);

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_ENERGY_CCA_THRES);
	val32 &= ~BIT(31);
	if ((x * oldval >> 7) & 0x1)
		val32 |= BIT(31);
	rtl8xxxu_write32(priv, REG_OFDM0_ENERGY_CCA_THRES, val32);

	y = result[candidate][1];
	if ((y & 0x00000200) != 0)
		y = y | 0xfffffc00;
	tx0_c = (y * oldval) >> 8;

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XC_TX_AFE);
	val32 &= ~0xf0000000;
	val32 |= (((tx0_c & 0x3c0) >> 6) << 28);
	rtl8xxxu_write32(priv, REG_OFDM0_XC_TX_AFE, val32);

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XA_TX_IQ_IMBALANCE);
	val32 &= ~0x003f0000;
	val32 |= ((tx0_c & 0x3f) << 16);
	rtl8xxxu_write32(priv, REG_OFDM0_XA_TX_IQ_IMBALANCE, val32);

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_ENERGY_CCA_THRES);
	val32 &= ~BIT(29);
	if ((y * oldval >> 7) & 0x1)
		val32 |= BIT(29);
	rtl8xxxu_write32(priv, REG_OFDM0_ENERGY_CCA_THRES, val32);

	if (tx_only) {
		dev_dbg(&priv->udev->dev, "%s: only TX\n", __func__);
		return;
	}

	reg = result[candidate][2];

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XA_RX_IQ_IMBALANCE);
	val32 &= ~0x3ff;
	val32 |= (reg & 0x3ff);
	rtl8xxxu_write32(priv, REG_OFDM0_XA_RX_IQ_IMBALANCE, val32);

	reg = result[candidate][3] & 0x3F;

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XA_RX_IQ_IMBALANCE);
	val32 &= ~0xfc00;
	val32 |= ((reg << 10) & 0xfc00);
	rtl8xxxu_write32(priv, REG_OFDM0_XA_RX_IQ_IMBALANCE, val32);

	reg = (result[candidate][3] >> 6) & 0xF;

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_RX_IQ_EXT_ANTA);
	val32 &= ~0xf0000000;
	val32 |= (reg << 28);
	rtl8xxxu_write32(priv, REG_OFDM0_RX_IQ_EXT_ANTA, val32);
}

void rtl8xxxu_fill_iqk_matrix_b(struct rtl8xxxu_priv *priv, bool iqk_ok,
				int result[][8], int candidate, bool tx_only)
{
	u32 oldval, x, tx1_a, reg;
	int y, tx1_c;
	u32 val32;

	if (!iqk_ok)
		return;

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XB_TX_IQ_IMBALANCE);
	oldval = val32 >> 22;

	x = result[candidate][4];
	if ((x & 0x00000200) != 0)
		x = x | 0xfffffc00;
	tx1_a = (x * oldval) >> 8;

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XB_TX_IQ_IMBALANCE);
	val32 &= ~0x3ff;
	val32 |= tx1_a;
	rtl8xxxu_write32(priv, REG_OFDM0_XB_TX_IQ_IMBALANCE, val32);

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_ENERGY_CCA_THRES);
	val32 &= ~BIT(27);
	if ((x * oldval >> 7) & 0x1)
		val32 |= BIT(27);
	rtl8xxxu_write32(priv, REG_OFDM0_ENERGY_CCA_THRES, val32);

	y = result[candidate][5];
	if ((y & 0x00000200) != 0)
		y = y | 0xfffffc00;
	tx1_c = (y * oldval) >> 8;

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XD_TX_AFE);
	val32 &= ~0xf0000000;
	val32 |= (((tx1_c & 0x3c0) >> 6) << 28);
	rtl8xxxu_write32(priv, REG_OFDM0_XD_TX_AFE, val32);

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XB_TX_IQ_IMBALANCE);
	val32 &= ~0x003f0000;
	val32 |= ((tx1_c & 0x3f) << 16);
	rtl8xxxu_write32(priv, REG_OFDM0_XB_TX_IQ_IMBALANCE, val32);

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_ENERGY_CCA_THRES);
	val32 &= ~BIT(25);
	if ((y * oldval >> 7) & 0x1)
		val32 |= BIT(25);
	rtl8xxxu_write32(priv, REG_OFDM0_ENERGY_CCA_THRES, val32);

	if (tx_only) {
		dev_dbg(&priv->udev->dev, "%s: only TX\n", __func__);
		return;
	}

	reg = result[candidate][6];

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XB_RX_IQ_IMBALANCE);
	val32 &= ~0x3ff;
	val32 |= (reg & 0x3ff);
	rtl8xxxu_write32(priv, REG_OFDM0_XB_RX_IQ_IMBALANCE, val32);

	reg = result[candidate][7] & 0x3f;

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_XB_RX_IQ_IMBALANCE);
	val32 &= ~0xfc00;
	val32 |= ((reg << 10) & 0xfc00);
	rtl8xxxu_write32(priv, REG_OFDM0_XB_RX_IQ_IMBALANCE, val32);

	reg = (result[candidate][7] >> 6) & 0xf;

	val32 = rtl8xxxu_read32(priv, REG_OFDM0_AGCR_SSI_TABLE);
	val32 &= ~0x0000f000;
	val32 |= (reg << 12);
	rtl8xxxu_write32(priv, REG_OFDM0_AGCR_SSI_TABLE, val32);
}

#define MAX_TOLERANCE		5

static bool rtl8xxxu_simularity_compare(struct rtl8xxxu_priv *priv,
					int result[][8], int c1, int c2)
{
	u32 i, j, diff, simubitmap, bound = 0;
	int candidate[2] = {-1, -1};	/* for path A and path B */
	bool retval = true;

	if (priv->tx_paths > 1)
		bound = 8;
	else
		bound = 4;

	simubitmap = 0;

	for (i = 0; i < bound; i++) {
		diff = (result[c1][i] > result[c2][i]) ?
			(result[c1][i] - result[c2][i]) :
			(result[c2][i] - result[c1][i]);
		if (diff > MAX_TOLERANCE) {
			if ((i == 2 || i == 6) && !simubitmap) {
				if (result[c1][i] + result[c1][i + 1] == 0)
					candidate[(i / 4)] = c2;
				else if (result[c2][i] + result[c2][i + 1] == 0)
					candidate[(i / 4)] = c1;
				else
					simubitmap = simubitmap | (1 << i);
			} else {
				simubitmap = simubitmap | (1 << i);
			}
		}
	}

	if (simubitmap == 0) {
		for (i = 0; i < (bound / 4); i++) {
			if (candidate[i] >= 0) {
				for (j = i * 4; j < (i + 1) * 4 - 2; j++)
					result[3][j] = result[candidate[i]][j];
				retval = false;
			}
		}
		return retval;
	} else if (!(simubitmap & 0x0f)) {
		/* path A OK */
		for (i = 0; i < 4; i++)
			result[3][i] = result[c1][i];
	} else if (!(simubitmap & 0xf0) && priv->tx_paths > 1) {
		/* path B OK */
		for (i = 4; i < 8; i++)
			result[3][i] = result[c1][i];
	}

	return false;
}

bool rtl8xxxu_gen2_simularity_compare(struct rtl8xxxu_priv *priv,
				      int result[][8], int c1, int c2)
{
	u32 i, j, diff, simubitmap, bound = 0;
	int candidate[2] = {-1, -1};	/* for path A and path B */
	int tmp1, tmp2;
	bool retval = true;

	if (priv->tx_paths > 1)
		bound = 8;
	else
		bound = 4;

	simubitmap = 0;

	for (i = 0; i < bound; i++) {
		if (i & 1) {
			if ((result[c1][i] & 0x00000200))
				tmp1 = result[c1][i] | 0xfffffc00;
			else
				tmp1 = result[c1][i];

			if ((result[c2][i]& 0x00000200))
				tmp2 = result[c2][i] | 0xfffffc00;
			else
				tmp2 = result[c2][i];
		} else {
			tmp1 = result[c1][i];
			tmp2 = result[c2][i];
		}

		diff = (tmp1 > tmp2) ? (tmp1 - tmp2) : (tmp2 - tmp1);

		if (diff > MAX_TOLERANCE) {
			if ((i == 2 || i == 6) && !simubitmap) {
				if (result[c1][i] + result[c1][i + 1] == 0)
					candidate[(i / 4)] = c2;
				else if (result[c2][i] + result[c2][i + 1] == 0)
					candidate[(i / 4)] = c1;
				else
					simubitmap = simubitmap | (1 << i);
			} else {
				simubitmap = simubitmap | (1 << i);
			}
		}
	}

	if (simubitmap == 0) {
		for (i = 0; i < (bound / 4); i++) {
			if (candidate[i] >= 0) {
				for (j = i * 4; j < (i + 1) * 4 - 2; j++)
					result[3][j] = result[candidate[i]][j];
				retval = false;
			}
		}
		return retval;
	} else {
		if (!(simubitmap & 0x03)) {
			/* path A TX OK */
			for (i = 0; i < 2; i++)
				result[3][i] = result[c1][i];
		}

		if (!(simubitmap & 0x0c)) {
			/* path A RX OK */
			for (i = 2; i < 4; i++)
				result[3][i] = result[c1][i];
		}

		if (!(simubitmap & 0x30) && priv->tx_paths > 1) {
			/* path B RX OK */
			for (i = 4; i < 6; i++)
				result[3][i] = result[c1][i];
		}

		if (!(simubitmap & 0x30) && priv->tx_paths > 1) {
			/* path B RX OK */
			for (i = 6; i < 8; i++)
				result[3][i] = result[c1][i];
		}
	}

	return false;
}

void
rtl8xxxu_save_mac_regs(struct rtl8xxxu_priv *priv, const u32 *reg, u32 *backup)
{
	int i;

	for (i = 0; i < (RTL8XXXU_MAC_REGS - 1); i++)
		backup[i] = rtl8xxxu_read8(priv, reg[i]);

	backup[i] = rtl8xxxu_read32(priv, reg[i]);
}

void rtl8xxxu_restore_mac_regs(struct rtl8xxxu_priv *priv,
			       const u32 *reg, u32 *backup)
{
	int i;

	for (i = 0; i < (RTL8XXXU_MAC_REGS - 1); i++)
		rtl8xxxu_write8(priv, reg[i], backup[i]);

	rtl8xxxu_write32(priv, reg[i], backup[i]);
}

void rtl8xxxu_save_regs(struct rtl8xxxu_priv *priv, const u32 *regs,
			u32 *backup, int count)
{
	int i;

	for (i = 0; i < count; i++)
		backup[i] = rtl8xxxu_read32(priv, regs[i]);
}

void rtl8xxxu_restore_regs(struct rtl8xxxu_priv *priv, const u32 *regs,
			   u32 *backup, int count)
{
	int i;

	for (i = 0; i < count; i++)
		rtl8xxxu_write32(priv, regs[i], backup[i]);
}


void rtl8xxxu_path_adda_on(struct rtl8xxxu_priv *priv, const u32 *regs,
			   bool path_a_on)
{
	u32 path_on;
	int i;

	if (priv->tx_paths == 1) {
		path_on = priv->fops->adda_1t_path_on;
		rtl8xxxu_write32(priv, regs[0], priv->fops->adda_1t_init);
	} else {
		path_on = path_a_on ? priv->fops->adda_2t_path_on_a :
			priv->fops->adda_2t_path_on_b;

		rtl8xxxu_write32(priv, regs[0], path_on);
	}

	for (i = 1 ; i < RTL8XXXU_ADDA_REGS ; i++)
		rtl8xxxu_write32(priv, regs[i], path_on);
}

void rtl8xxxu_mac_calibration(struct rtl8xxxu_priv *priv,
			      const u32 *regs, u32 *backup)
{
	int i = 0;

	rtl8xxxu_write8(priv, regs[i], 0x3f);

	for (i = 1 ; i < (RTL8XXXU_MAC_REGS - 1); i++)
		rtl8xxxu_write8(priv, regs[i], (u8)(backup[i] & ~BIT(3)));

	rtl8xxxu_write8(priv, regs[i], (u8)(backup[i] & ~BIT(5)));
}

static int rtl8xxxu_iqk_path_a(struct rtl8xxxu_priv *priv)
{
	u32 reg_eac, reg_e94, reg_e9c, reg_ea4, val32;
	int result = 0;

	/* path-A IQK setting */
	rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x10008c1f);
	rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x10008c1f);
	rtl8xxxu_write32(priv, REG_TX_IQK_PI_A, 0x82140102);

	val32 = (priv->rf_paths > 1) ? 0x28160202 :
		/*IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID)?0x28160202: */
		0x28160502;
	rtl8xxxu_write32(priv, REG_RX_IQK_PI_A, val32);

	/* path-B IQK setting */
	if (priv->rf_paths > 1) {
		rtl8xxxu_write32(priv, REG_TX_IQK_TONE_B, 0x10008c22);
		rtl8xxxu_write32(priv, REG_RX_IQK_TONE_B, 0x10008c22);
		rtl8xxxu_write32(priv, REG_TX_IQK_PI_B, 0x82140102);
		rtl8xxxu_write32(priv, REG_RX_IQK_PI_B, 0x28160202);
	}

	/* LO calibration setting */
	rtl8xxxu_write32(priv, REG_IQK_AGC_RSP, 0x001028d1);

	/* One shot, path A LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf9000000);
	rtl8xxxu_write32(priv, REG_IQK_AGC_PTS, 0xf8000000);

	mdelay(1);

	/* Check failed */
	reg_eac = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_A_2);
	reg_e94 = rtl8xxxu_read32(priv, REG_TX_POWER_BEFORE_IQK_A);
	reg_e9c = rtl8xxxu_read32(priv, REG_TX_POWER_AFTER_IQK_A);
	reg_ea4 = rtl8xxxu_read32(priv, REG_RX_POWER_BEFORE_IQK_A_2);

	if (!(reg_eac & BIT(28)) &&
	    ((reg_e94 & 0x03ff0000) != 0x01420000) &&
	    ((reg_e9c & 0x03ff0000) != 0x00420000))
		result |= 0x01;
	else	/* If TX not OK, ignore RX */
		goto out;

	/* If TX is OK, check whether RX is OK */
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

static int rtl8xxxu_iqk_path_b(struct rtl8xxxu_priv *priv)
{
	u32 reg_eac, reg_eb4, reg_ebc, reg_ec4, reg_ecc;
	int result = 0;

	/* One shot, path B LOK & IQK */
	rtl8xxxu_write32(priv, REG_IQK_AGC_CONT, 0x00000002);
	rtl8xxxu_write32(priv, REG_IQK_AGC_CONT, 0x00000000);

	mdelay(1);

	/* Check failed */
	reg_eac = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_A_2);
	reg_eb4 = rtl8xxxu_read32(priv, REG_TX_POWER_BEFORE_IQK_B);
	reg_ebc = rtl8xxxu_read32(priv, REG_TX_POWER_AFTER_IQK_B);
	reg_ec4 = rtl8xxxu_read32(priv, REG_RX_POWER_BEFORE_IQK_B_2);
	reg_ecc = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_B_2);

	if (!(reg_eac & BIT(31)) &&
	    ((reg_eb4 & 0x03ff0000) != 0x01420000) &&
	    ((reg_ebc & 0x03ff0000) != 0x00420000))
		result |= 0x01;
	else
		goto out;

	if (!(reg_eac & BIT(30)) &&
	    (((reg_ec4 & 0x03ff0000) >> 16) != 0x132) &&
	    (((reg_ecc & 0x03ff0000) >> 16) != 0x36))
		result |= 0x02;
	else
		dev_warn(&priv->udev->dev, "%s: Path B RX IQK failed!\n",
			 __func__);
out:
	return result;
}

static void rtl8xxxu_phy_iqcalibrate(struct rtl8xxxu_priv *priv,
				     int result[][8], int t)
{
	struct device *dev = &priv->udev->dev;
	u32 i, val32;
	int path_a_ok, path_b_ok;
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
		if (val32 & FPGA0_HSSI_PARM1_PI)
			priv->pi_enabled = 1;
	}

	if (!priv->pi_enabled) {
		/* Switch BB to PI mode to do IQ Calibration. */
		rtl8xxxu_write32(priv, REG_FPGA0_XA_HSSI_PARM1, 0x01000100);
		rtl8xxxu_write32(priv, REG_FPGA0_XB_HSSI_PARM1, 0x01000100);
	}

	val32 = rtl8xxxu_read32(priv, REG_FPGA0_RF_MODE);
	val32 &= ~FPGA_RF_MODE_CCK;
	rtl8xxxu_write32(priv, REG_FPGA0_RF_MODE, val32);

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

	if (priv->tx_paths > 1) {
		rtl8xxxu_write32(priv, REG_FPGA0_XA_LSSI_PARM, 0x00010000);
		rtl8xxxu_write32(priv, REG_FPGA0_XB_LSSI_PARM, 0x00010000);
	}

	/* MAC settings */
	rtl8xxxu_mac_calibration(priv, iqk_mac_regs, priv->mac_backup);

	/* Page B init */
	rtl8xxxu_write32(priv, REG_CONFIG_ANT_A, 0x00080000);

	if (priv->tx_paths > 1)
		rtl8xxxu_write32(priv, REG_CONFIG_ANT_B, 0x00080000);

	/* IQ calibration setting */
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x80800000);
	rtl8xxxu_write32(priv, REG_TX_IQK, 0x01007c00);
	rtl8xxxu_write32(priv, REG_RX_IQK, 0x01004800);

	for (i = 0; i < retry; i++) {
		path_a_ok = rtl8xxxu_iqk_path_a(priv);
		if (path_a_ok == 0x03) {
			val32 = rtl8xxxu_read32(priv,
						REG_TX_POWER_BEFORE_IQK_A);
			result[t][0] = (val32 >> 16) & 0x3ff;
			val32 = rtl8xxxu_read32(priv,
						REG_TX_POWER_AFTER_IQK_A);
			result[t][1] = (val32 >> 16) & 0x3ff;
			val32 = rtl8xxxu_read32(priv,
						REG_RX_POWER_BEFORE_IQK_A_2);
			result[t][2] = (val32 >> 16) & 0x3ff;
			val32 = rtl8xxxu_read32(priv,
						REG_RX_POWER_AFTER_IQK_A_2);
			result[t][3] = (val32 >> 16) & 0x3ff;
			break;
		} else if (i == (retry - 1) && path_a_ok == 0x01) {
			/* TX IQK OK */
			dev_dbg(dev, "%s: Path A IQK Only Tx Success!!\n",
				__func__);

			val32 = rtl8xxxu_read32(priv,
						REG_TX_POWER_BEFORE_IQK_A);
			result[t][0] = (val32 >> 16) & 0x3ff;
			val32 = rtl8xxxu_read32(priv,
						REG_TX_POWER_AFTER_IQK_A);
			result[t][1] = (val32 >> 16) & 0x3ff;
		}
	}

	if (!path_a_ok)
		dev_dbg(dev, "%s: Path A IQK failed!\n", __func__);

	if (priv->tx_paths > 1) {
		/*
		 * Path A into standby
		 */
		rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x0);
		rtl8xxxu_write32(priv, REG_FPGA0_XA_LSSI_PARM, 0x00010000);
		rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0x80800000);

		/* Turn Path B ADDA on */
		rtl8xxxu_path_adda_on(priv, adda_regs, false);

		for (i = 0; i < retry; i++) {
			path_b_ok = rtl8xxxu_iqk_path_b(priv);
			if (path_b_ok == 0x03) {
				val32 = rtl8xxxu_read32(priv, REG_TX_POWER_BEFORE_IQK_B);
				result[t][4] = (val32 >> 16) & 0x3ff;
				val32 = rtl8xxxu_read32(priv, REG_TX_POWER_AFTER_IQK_B);
				result[t][5] = (val32 >> 16) & 0x3ff;
				val32 = rtl8xxxu_read32(priv, REG_RX_POWER_BEFORE_IQK_B_2);
				result[t][6] = (val32 >> 16) & 0x3ff;
				val32 = rtl8xxxu_read32(priv, REG_RX_POWER_AFTER_IQK_B_2);
				result[t][7] = (val32 >> 16) & 0x3ff;
				break;
			} else if (i == (retry - 1) && path_b_ok == 0x01) {
				/* TX IQK OK */
				val32 = rtl8xxxu_read32(priv, REG_TX_POWER_BEFORE_IQK_B);
				result[t][4] = (val32 >> 16) & 0x3ff;
				val32 = rtl8xxxu_read32(priv, REG_TX_POWER_AFTER_IQK_B);
				result[t][5] = (val32 >> 16) & 0x3ff;
			}
		}

		if (!path_b_ok)
			dev_dbg(dev, "%s: Path B IQK failed!\n", __func__);
	}

	/* Back to BB mode, load original value */
	rtl8xxxu_write32(priv, REG_FPGA0_IQK, 0);

	if (t) {
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

		/* Restore RX initial gain */
		rtl8xxxu_write32(priv, REG_FPGA0_XA_LSSI_PARM, 0x00032ed3);

		if (priv->tx_paths > 1) {
			rtl8xxxu_write32(priv, REG_FPGA0_XB_LSSI_PARM,
					 0x00032ed3);
		}

		/* Load 0xe30 IQC default value */
		rtl8xxxu_write32(priv, REG_TX_IQK_TONE_A, 0x01008c00);
		rtl8xxxu_write32(priv, REG_RX_IQK_TONE_A, 0x01008c00);
	}
}

void rtl8xxxu_gen2_prepare_calibrate(struct rtl8xxxu_priv *priv, u8 start)
{
	struct h2c_cmd h2c;

	memset(&h2c, 0, sizeof(struct h2c_cmd));
	h2c.bt_wlan_calibration.cmd = H2C_8723B_BT_WLAN_CALIBRATION;
	h2c.bt_wlan_calibration.data = start;

	rtl8xxxu_gen2_h2c_cmd(priv, &h2c, sizeof(h2c.bt_wlan_calibration));
}

void rtl8xxxu_gen1_phy_iq_calibrate(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	int result[4][8];	/* last is final result */
	int i, candidate;
	bool path_a_ok, path_b_ok;
	u32 reg_e94, reg_e9c, reg_ea4, reg_eac;
	u32 reg_eb4, reg_ebc, reg_ec4, reg_ecc;
	s32 reg_tmp = 0;
	bool simu;

	memset(result, 0, sizeof(result));
	candidate = -1;

	path_a_ok = false;
	path_b_ok = false;

	rtl8xxxu_read32(priv, REG_FPGA0_RF_MODE);

	for (i = 0; i < 3; i++) {
		rtl8xxxu_phy_iqcalibrate(priv, result, i);

		if (i == 1) {
			simu = rtl8xxxu_simularity_compare(priv, result, 0, 1);
			if (simu) {
				candidate = 0;
				break;
			}
		}

		if (i == 2) {
			simu = rtl8xxxu_simularity_compare(priv, result, 0, 2);
			if (simu) {
				candidate = 0;
				break;
			}

			simu = rtl8xxxu_simularity_compare(priv, result, 1, 2);
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
}

static void rtl8723a_phy_lc_calibrate(struct rtl8xxxu_priv *priv)
{
	u32 val32;
	u32 rf_amode, rf_bmode = 0, lstf;

	/* Check continuous TX and Packet TX */
	lstf = rtl8xxxu_read32(priv, REG_OFDM1_LSTF);

	if (lstf & OFDM_LSTF_MASK) {
		/* Disable all continuous TX */
		val32 = lstf & ~OFDM_LSTF_MASK;
		rtl8xxxu_write32(priv, REG_OFDM1_LSTF, val32);

		/* Read original RF mode Path A */
		rf_amode = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_AC);

		/* Set RF mode to standby Path A */
		rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_AC,
				     (rf_amode & 0x8ffff) | 0x10000);

		/* Path-B */
		if (priv->tx_paths > 1) {
			rf_bmode = rtl8xxxu_read_rfreg(priv, RF_B,
						       RF6052_REG_AC);

			rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_AC,
					     (rf_bmode & 0x8ffff) | 0x10000);
		}
	} else {
		/*  Deal with Packet TX case */
		/*  block all queues */
		rtl8xxxu_write8(priv, REG_TXPAUSE, 0xff);
	}

	/* Start LC calibration */
	if (priv->fops->has_s0s1)
		rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_S0S1, 0xdfbe0);
	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_MODE_AG);
	val32 |= 0x08000;
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_MODE_AG, val32);

	msleep(100);

	if (priv->fops->has_s0s1)
		rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_S0S1, 0xdffe0);

	/* Restore original parameters */
	if (lstf & OFDM_LSTF_MASK) {
		/* Path-A */
		rtl8xxxu_write32(priv, REG_OFDM1_LSTF, lstf);
		rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_AC, rf_amode);

		/* Path-B */
		if (priv->tx_paths > 1)
			rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_AC,
					     rf_bmode);
	} else /*  Deal with Packet TX case */
		rtl8xxxu_write8(priv, REG_TXPAUSE, 0x00);
}

static int rtl8xxxu_set_mac(struct rtl8xxxu_priv *priv)
{
	int i;
	u16 reg;

	reg = REG_MACID;

	for (i = 0; i < ETH_ALEN; i++)
		rtl8xxxu_write8(priv, reg + i, priv->mac_addr[i]);

	return 0;
}

static int rtl8xxxu_set_bssid(struct rtl8xxxu_priv *priv, const u8 *bssid)
{
	int i;
	u16 reg;

	dev_dbg(&priv->udev->dev, "%s: (%pM)\n", __func__, bssid);

	reg = REG_BSSID;

	for (i = 0; i < ETH_ALEN; i++)
		rtl8xxxu_write8(priv, reg + i, bssid[i]);

	return 0;
}

static void
rtl8xxxu_set_ampdu_factor(struct rtl8xxxu_priv *priv, u8 ampdu_factor)
{
	u8 vals[4] = { 0x41, 0xa8, 0x72, 0xb9 };
	u8 max_agg = 0xf;
	int i;

	ampdu_factor = 1 << (ampdu_factor + 2);
	if (ampdu_factor > max_agg)
		ampdu_factor = max_agg;

	for (i = 0; i < 4; i++) {
		if ((vals[i] & 0xf0) > (ampdu_factor << 4))
			vals[i] = (vals[i] & 0x0f) | (ampdu_factor << 4);

		if ((vals[i] & 0x0f) > ampdu_factor)
			vals[i] = (vals[i] & 0xf0) | ampdu_factor;

		rtl8xxxu_write8(priv, REG_AGGLEN_LMT + i, vals[i]);
	}
}

static void rtl8xxxu_set_ampdu_min_space(struct rtl8xxxu_priv *priv, u8 density)
{
	u8 val8;

	val8 = rtl8xxxu_read8(priv, REG_AMPDU_MIN_SPACE);
	val8 &= 0xf8;
	val8 |= density;
	rtl8xxxu_write8(priv, REG_AMPDU_MIN_SPACE, val8);
}

static int rtl8xxxu_active_to_emu(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	int count, ret = 0;

	/* Start of rtl8723AU_card_enable_flow */
	/* Act to Cardemu sequence*/
	/* Turn off RF */
	rtl8xxxu_write8(priv, REG_RF_CTRL, 0);

	/* 0x004E[7] = 0, switch DPDT_SEL_P output from register 0x0065[2] */
	val8 = rtl8xxxu_read8(priv, REG_LEDCFG2);
	val8 &= ~LEDCFG2_DPDT_SELECT;
	rtl8xxxu_write8(priv, REG_LEDCFG2, val8);

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

int rtl8xxxu_active_to_lps(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u8 val32;
	int count, ret = 0;

	rtl8xxxu_write8(priv, REG_TXPAUSE, 0xff);

	/*
	 * Poll - wait for RX packet to complete
	 */
	for (count = RTL8XXXU_MAX_REG_POLL; count; count--) {
		val32 = rtl8xxxu_read32(priv, 0x5f8);
		if (!val32)
			break;
		udelay(10);
	}

	if (!count) {
		dev_warn(&priv->udev->dev,
			 "%s: RX poll timed out (0x05f8)\n", __func__);
		ret = -EBUSY;
		goto exit;
	}

	/* Disable CCK and OFDM, clock gated */
	val8 = rtl8xxxu_read8(priv, REG_SYS_FUNC);
	val8 &= ~SYS_FUNC_BBRSTB;
	rtl8xxxu_write8(priv, REG_SYS_FUNC, val8);

	udelay(2);

	/* Reset baseband */
	val8 = rtl8xxxu_read8(priv, REG_SYS_FUNC);
	val8 &= ~SYS_FUNC_BB_GLB_RSTN;
	rtl8xxxu_write8(priv, REG_SYS_FUNC, val8);

	/* Reset MAC TRX */
	val8 = rtl8xxxu_read8(priv, REG_CR);
	val8 = CR_HCI_TXDMA_ENABLE | CR_HCI_RXDMA_ENABLE;
	rtl8xxxu_write8(priv, REG_CR, val8);

	/* Reset MAC TRX */
	val8 = rtl8xxxu_read8(priv, REG_CR + 1);
	val8 &= ~BIT(1); /* CR_SECURITY_ENABLE */
	rtl8xxxu_write8(priv, REG_CR + 1, val8);

	/* Respond TX OK to scheduler */
	val8 = rtl8xxxu_read8(priv, REG_DUAL_TSF_RST);
	val8 |= DUAL_TSF_TX_OK;
	rtl8xxxu_write8(priv, REG_DUAL_TSF_RST, val8);

exit:
	return ret;
}

void rtl8xxxu_disabled_to_emu(struct rtl8xxxu_priv *priv)
{
	u8 val8;

	/* Clear suspend enable and power down enable*/
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 &= ~(BIT(3) | BIT(7));
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	/* 0x48[16] = 0 to disable GPIO9 as EXT WAKEUP*/
	val8 = rtl8xxxu_read8(priv, REG_GPIO_INTM + 2);
	val8 &= ~BIT(0);
	rtl8xxxu_write8(priv, REG_GPIO_INTM + 2, val8);

	/* 0x04[12:11] = 11 enable WL suspend*/
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 &= ~(BIT(3) | BIT(4));
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);
}

static int rtl8xxxu_emu_to_disabled(struct rtl8xxxu_priv *priv)
{
	u8 val8;

	/* 0x0007[7:0] = 0x20 SOP option to disable BG/MB */
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 3, 0x20);

	/* 0x04[12:11] = 01 enable WL suspend */
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 &= ~BIT(4);
	val8 |= BIT(3);
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 |= BIT(7);
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	/* 0x48[16] = 1 to enable GPIO9 as EXT wakeup */
	val8 = rtl8xxxu_read8(priv, REG_GPIO_INTM + 2);
	val8 |= BIT(0);
	rtl8xxxu_write8(priv, REG_GPIO_INTM + 2, val8);

	return 0;
}

int rtl8xxxu_flush_fifo(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	u32 val32;
	int retry, retval;

	rtl8xxxu_write8(priv, REG_TXPAUSE, 0xff);

	val32 = rtl8xxxu_read32(priv, REG_RXPKT_NUM);
	val32 |= RXPKT_NUM_RW_RELEASE_EN;
	rtl8xxxu_write32(priv, REG_RXPKT_NUM, val32);

	retry = 100;
	retval = -EBUSY;

	do {
		val32 = rtl8xxxu_read32(priv, REG_RXPKT_NUM);
		if (val32 & RXPKT_NUM_RXDMA_IDLE) {
			retval = 0;
			break;
		}
	} while (retry--);

	rtl8xxxu_write16(priv, REG_RQPN_NPQ, 0);
	rtl8xxxu_write32(priv, REG_RQPN, 0x80000000);
	mdelay(2);

	if (!retry)
		dev_warn(dev, "Failed to flush FIFO\n");

	return retval;
}

void rtl8xxxu_gen1_usb_quirks(struct rtl8xxxu_priv *priv)
{
	/* Fix USB interface interference issue */
	rtl8xxxu_write8(priv, 0xfe40, 0xe0);
	rtl8xxxu_write8(priv, 0xfe41, 0x8d);
	rtl8xxxu_write8(priv, 0xfe42, 0x80);
	/*
	 * This sets TXDMA_OFFSET_DROP_DATA_EN (bit 9) as well as bits
	 * 8 and 5, for which I have found no documentation.
	 */
	rtl8xxxu_write32(priv, REG_TXDMA_OFFSET_CHK, 0xfd0320);

	/*
	 * Solve too many protocol error on USB bus.
	 * Can't do this for 8188/8192 UMC A cut parts
	 */
	if (!(!priv->chip_cut && priv->vendor_umc)) {
		rtl8xxxu_write8(priv, 0xfe40, 0xe6);
		rtl8xxxu_write8(priv, 0xfe41, 0x94);
		rtl8xxxu_write8(priv, 0xfe42, 0x80);

		rtl8xxxu_write8(priv, 0xfe40, 0xe0);
		rtl8xxxu_write8(priv, 0xfe41, 0x19);
		rtl8xxxu_write8(priv, 0xfe42, 0x80);

		rtl8xxxu_write8(priv, 0xfe40, 0xe5);
		rtl8xxxu_write8(priv, 0xfe41, 0x91);
		rtl8xxxu_write8(priv, 0xfe42, 0x80);

		rtl8xxxu_write8(priv, 0xfe40, 0xe2);
		rtl8xxxu_write8(priv, 0xfe41, 0x81);
		rtl8xxxu_write8(priv, 0xfe42, 0x80);
	}
}

void rtl8xxxu_gen2_usb_quirks(struct rtl8xxxu_priv *priv)
{
	u32 val32;

	val32 = rtl8xxxu_read32(priv, REG_TXDMA_OFFSET_CHK);
	val32 |= TXDMA_OFFSET_DROP_DATA_EN;
	rtl8xxxu_write32(priv, REG_TXDMA_OFFSET_CHK, val32);
}

void rtl8xxxu_power_off(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u16 val16;
	u32 val32;

	/*
	 * Workaround for 8188RU LNA power leakage problem.
	 */
	if (priv->rtl_chip == RTL8188R) {
		val32 = rtl8xxxu_read32(priv, REG_FPGA0_XCD_RF_PARM);
		val32 |= BIT(1);
		rtl8xxxu_write32(priv, REG_FPGA0_XCD_RF_PARM, val32);
	}

	rtl8xxxu_flush_fifo(priv);

	rtl8xxxu_active_to_lps(priv);

	/* Turn off RF */
	rtl8xxxu_write8(priv, REG_RF_CTRL, 0x00);

	/* Reset Firmware if running in RAM */
	if (rtl8xxxu_read8(priv, REG_MCU_FW_DL) & MCU_FW_RAM_SEL)
		rtl8xxxu_firmware_self_reset(priv);

	/* Reset MCU */
	val16 = rtl8xxxu_read16(priv, REG_SYS_FUNC);
	val16 &= ~SYS_FUNC_CPU_ENABLE;
	rtl8xxxu_write16(priv, REG_SYS_FUNC, val16);

	/* Reset MCU ready status */
	rtl8xxxu_write8(priv, REG_MCU_FW_DL, 0x00);

	rtl8xxxu_active_to_emu(priv);
	rtl8xxxu_emu_to_disabled(priv);

	/* Reset MCU IO Wrapper */
	val8 = rtl8xxxu_read8(priv, REG_RSV_CTRL + 1);
	val8 &= ~BIT(0);
	rtl8xxxu_write8(priv, REG_RSV_CTRL + 1, val8);

	val8 = rtl8xxxu_read8(priv, REG_RSV_CTRL + 1);
	val8 |= BIT(0);
	rtl8xxxu_write8(priv, REG_RSV_CTRL + 1, val8);

	/* RSV_CTRL 0x1C[7:0] = 0x0e  lock ISO/CLK/Power control register */
	rtl8xxxu_write8(priv, REG_RSV_CTRL, 0x0e);
}

void rtl8723bu_set_ps_tdma(struct rtl8xxxu_priv *priv,
			   u8 arg1, u8 arg2, u8 arg3, u8 arg4, u8 arg5)
{
	struct h2c_cmd h2c;

	memset(&h2c, 0, sizeof(struct h2c_cmd));
	h2c.b_type_dma.cmd = H2C_8723B_B_TYPE_TDMA;
	h2c.b_type_dma.data1 = arg1;
	h2c.b_type_dma.data2 = arg2;
	h2c.b_type_dma.data3 = arg3;
	h2c.b_type_dma.data4 = arg4;
	h2c.b_type_dma.data5 = arg5;
	rtl8xxxu_gen2_h2c_cmd(priv, &h2c, sizeof(h2c.b_type_dma));
}

void rtl8xxxu_gen2_disable_rf(struct rtl8xxxu_priv *priv)
{
	u32 val32;

	val32 = rtl8xxxu_read32(priv, REG_RX_WAIT_CCA);
	val32 &= ~(BIT(22) | BIT(23));
	rtl8xxxu_write32(priv, REG_RX_WAIT_CCA, val32);
}

static void rtl8xxxu_init_queue_reserved_page(struct rtl8xxxu_priv *priv)
{
	struct rtl8xxxu_fileops *fops = priv->fops;
	u32 hq, lq, nq, eq, pubq;
	u32 val32;

	hq = 0;
	lq = 0;
	nq = 0;
	eq = 0;
	pubq = 0;

	if (priv->ep_tx_high_queue)
		hq = fops->page_num_hi;
	if (priv->ep_tx_low_queue)
		lq = fops->page_num_lo;
	if (priv->ep_tx_normal_queue)
		nq = fops->page_num_norm;

	val32 = (nq << RQPN_NPQ_SHIFT) | (eq << RQPN_EPQ_SHIFT);
	rtl8xxxu_write32(priv, REG_RQPN_NPQ, val32);

	pubq = fops->total_page_num - hq - lq - nq - 1;

	val32 = RQPN_LOAD;
	val32 |= (hq << RQPN_HI_PQ_SHIFT);
	val32 |= (lq << RQPN_LO_PQ_SHIFT);
	val32 |= (pubq << RQPN_PUB_PQ_SHIFT);

	rtl8xxxu_write32(priv, REG_RQPN, val32);
}

static int rtl8xxxu_init_device(struct ieee80211_hw *hw)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	struct device *dev = &priv->udev->dev;
	struct rtl8xxxu_fileops *fops = priv->fops;
	bool macpower;
	int ret;
	u8 val8;
	u16 val16;
	u32 val32;

	/* Check if MAC is already powered on */
	val8 = rtl8xxxu_read8(priv, REG_CR);
	val16 = rtl8xxxu_read16(priv, REG_SYS_CLKR);

	/*
	 * Fix 92DU-VC S3 hang with the reason is that secondary mac is not
	 * initialized. First MAC returns 0xea, second MAC returns 0x00
	 */
	if (val8 == 0xea || !(val16 & SYS_CLK_MAC_CLK_ENABLE))
		macpower = false;
	else
		macpower = true;

	if (fops->needs_full_init)
		macpower = false;

	ret = fops->power_on(priv);
	if (ret < 0) {
		dev_warn(dev, "%s: Failed power on\n", __func__);
		goto exit;
	}

	if (!macpower)
		rtl8xxxu_init_queue_reserved_page(priv);

	ret = rtl8xxxu_init_queue_priority(priv);
	dev_dbg(dev, "%s: init_queue_priority %i\n", __func__, ret);
	if (ret)
		goto exit;

	/*
	 * Set RX page boundary
	 */
	rtl8xxxu_write16(priv, REG_TRXFF_BNDY + 2, fops->trxff_boundary);

	ret = rtl8xxxu_download_firmware(priv);
	dev_dbg(dev, "%s: download_firmware %i\n", __func__, ret);
	if (ret)
		goto exit;
	ret = rtl8xxxu_start_firmware(priv);
	dev_dbg(dev, "%s: start_firmware %i\n", __func__, ret);
	if (ret)
		goto exit;

	if (fops->phy_init_antenna_selection)
		fops->phy_init_antenna_selection(priv);

	ret = rtl8xxxu_init_mac(priv);

	dev_dbg(dev, "%s: init_mac %i\n", __func__, ret);
	if (ret)
		goto exit;

	ret = rtl8xxxu_init_phy_bb(priv);
	dev_dbg(dev, "%s: init_phy_bb %i\n", __func__, ret);
	if (ret)
		goto exit;

	ret = fops->init_phy_rf(priv);
	if (ret)
		goto exit;

	/* RFSW Control - clear bit 14 ?? */
	if (priv->rtl_chip != RTL8723B && priv->rtl_chip != RTL8192E)
		rtl8xxxu_write32(priv, REG_FPGA0_TX_INFO, 0x00000003);

	val32 = FPGA0_RF_TRSW | FPGA0_RF_TRSWB | FPGA0_RF_ANTSW |
		FPGA0_RF_ANTSWB |
		((FPGA0_RF_ANTSW | FPGA0_RF_ANTSWB) << FPGA0_RF_BD_CTRL_SHIFT);
	if (!priv->no_pape) {
		val32 |= (FPGA0_RF_PAPE |
			  (FPGA0_RF_PAPE << FPGA0_RF_BD_CTRL_SHIFT));
	}
	rtl8xxxu_write32(priv, REG_FPGA0_XAB_RF_SW_CTRL, val32);

	/* 0x860[6:5]= 00 - why? - this sets antenna B */
	if (priv->rtl_chip != RTL8192E)
		rtl8xxxu_write32(priv, REG_FPGA0_XA_RF_INT_OE, 0x66f60210);

	if (!macpower) {
		/*
		 * Set TX buffer boundary
		 */
		val8 = fops->total_page_num + 1;

		rtl8xxxu_write8(priv, REG_TXPKTBUF_BCNQ_BDNY, val8);
		rtl8xxxu_write8(priv, REG_TXPKTBUF_MGQ_BDNY, val8);
		rtl8xxxu_write8(priv, REG_TXPKTBUF_WMAC_LBK_BF_HD, val8);
		rtl8xxxu_write8(priv, REG_TRXFF_BNDY, val8);
		rtl8xxxu_write8(priv, REG_TDECTRL + 1, val8);
	}

	/*
	 * The vendor drivers set PBP for all devices, except 8192e.
	 * There is no explanation for this in any of the sources.
	 */
	val8 = (fops->pbp_rx << PBP_PAGE_SIZE_RX_SHIFT) |
		(fops->pbp_tx << PBP_PAGE_SIZE_TX_SHIFT);
	if (priv->rtl_chip != RTL8192E)
		rtl8xxxu_write8(priv, REG_PBP, val8);

	dev_dbg(dev, "%s: macpower %i\n", __func__, macpower);
	if (!macpower) {
		ret = fops->llt_init(priv);
		if (ret) {
			dev_warn(dev, "%s: LLT table init failed\n", __func__);
			goto exit;
		}

		/*
		 * Chip specific quirks
		 */
		fops->usb_quirks(priv);

		/*
		 * Enable TX report and TX report timer for 8723bu/8188eu/...
		 */
		if (fops->has_tx_report) {
			val8 = rtl8xxxu_read8(priv, REG_TX_REPORT_CTRL);
			val8 |= TX_REPORT_CTRL_TIMER_ENABLE;
			rtl8xxxu_write8(priv, REG_TX_REPORT_CTRL, val8);
			/* Set MAX RPT MACID */
			rtl8xxxu_write8(priv, REG_TX_REPORT_CTRL + 1, 0x02);
			/* TX report Timer. Unit: 32us */
			rtl8xxxu_write16(priv, REG_TX_REPORT_TIME, 0xcdf0);

			/* tmp ps ? */
			val8 = rtl8xxxu_read8(priv, 0xa3);
			val8 &= 0xf8;
			rtl8xxxu_write8(priv, 0xa3, val8);
		}
	}

	/*
	 * Unit in 8 bytes, not obvious what it is used for
	 */
	rtl8xxxu_write8(priv, REG_RX_DRVINFO_SZ, 4);

	if (priv->rtl_chip == RTL8192E) {
		rtl8xxxu_write32(priv, REG_HIMR0, 0x00);
		rtl8xxxu_write32(priv, REG_HIMR1, 0x00);
	} else {
		/*
		 * Enable all interrupts - not obvious USB needs to do this
		 */
		rtl8xxxu_write32(priv, REG_HISR, 0xffffffff);
		rtl8xxxu_write32(priv, REG_HIMR, 0xffffffff);
	}

	rtl8xxxu_set_mac(priv);
	rtl8xxxu_set_linktype(priv, NL80211_IFTYPE_STATION);

	/*
	 * Configure initial WMAC settings
	 */
	val32 = RCR_ACCEPT_PHYS_MATCH | RCR_ACCEPT_MCAST | RCR_ACCEPT_BCAST |
		RCR_ACCEPT_MGMT_FRAME | RCR_HTC_LOC_CTRL |
		RCR_APPEND_PHYSTAT | RCR_APPEND_ICV | RCR_APPEND_MIC;
	rtl8xxxu_write32(priv, REG_RCR, val32);

	/*
	 * Accept all multicast
	 */
	rtl8xxxu_write32(priv, REG_MAR, 0xffffffff);
	rtl8xxxu_write32(priv, REG_MAR + 4, 0xffffffff);

	/*
	 * Init adaptive controls
	 */
	val32 = rtl8xxxu_read32(priv, REG_RESPONSE_RATE_SET);
	val32 &= ~RESPONSE_RATE_BITMAP_ALL;
	val32 |= RESPONSE_RATE_RRSR_CCK_ONLY_1M;
	rtl8xxxu_write32(priv, REG_RESPONSE_RATE_SET, val32);

	/* CCK = 0x0a, OFDM = 0x10 */
	rtl8xxxu_set_spec_sifs(priv, 0x10, 0x10);
	rtl8xxxu_set_retry(priv, 0x30, 0x30);
	rtl8xxxu_set_spec_sifs(priv, 0x0a, 0x10);

	/*
	 * Init EDCA
	 */
	rtl8xxxu_write16(priv, REG_MAC_SPEC_SIFS, 0x100a);

	/* Set CCK SIFS */
	rtl8xxxu_write16(priv, REG_SIFS_CCK, 0x100a);

	/* Set OFDM SIFS */
	rtl8xxxu_write16(priv, REG_SIFS_OFDM, 0x100a);

	/* TXOP */
	rtl8xxxu_write32(priv, REG_EDCA_BE_PARAM, 0x005ea42b);
	rtl8xxxu_write32(priv, REG_EDCA_BK_PARAM, 0x0000a44f);
	rtl8xxxu_write32(priv, REG_EDCA_VI_PARAM, 0x005ea324);
	rtl8xxxu_write32(priv, REG_EDCA_VO_PARAM, 0x002fa226);

	/* Set data auto rate fallback retry count */
	rtl8xxxu_write32(priv, REG_DARFRC, 0x00000000);
	rtl8xxxu_write32(priv, REG_DARFRC + 4, 0x10080404);
	rtl8xxxu_write32(priv, REG_RARFRC, 0x04030201);
	rtl8xxxu_write32(priv, REG_RARFRC + 4, 0x08070605);

	val8 = rtl8xxxu_read8(priv, REG_FWHW_TXQ_CTRL);
	val8 |= FWHW_TXQ_CTRL_AMPDU_RETRY;
	rtl8xxxu_write8(priv, REG_FWHW_TXQ_CTRL, val8);

	/*  Set ACK timeout */
	rtl8xxxu_write8(priv, REG_ACKTO, 0x40);

	/*
	 * Initialize beacon parameters
	 */
	val16 = BEACON_DISABLE_TSF_UPDATE | (BEACON_DISABLE_TSF_UPDATE << 8);
	rtl8xxxu_write16(priv, REG_BEACON_CTRL, val16);
	rtl8xxxu_write16(priv, REG_TBTT_PROHIBIT, 0x6404);
	rtl8xxxu_write8(priv, REG_DRIVER_EARLY_INT, DRIVER_EARLY_INT_TIME);
	rtl8xxxu_write8(priv, REG_BEACON_DMA_TIME, BEACON_DMA_ATIME_INT_TIME);
	rtl8xxxu_write16(priv, REG_BEACON_TCFG, 0x660F);

	/*
	 * Initialize burst parameters
	 */
	if (priv->rtl_chip == RTL8723B) {
		/*
		 * For USB high speed set 512B packets
		 */
		val8 = rtl8xxxu_read8(priv, REG_RXDMA_PRO_8723B);
		val8 &= ~(BIT(4) | BIT(5));
		val8 |= BIT(4);
		val8 |= BIT(1) | BIT(2) | BIT(3);
		rtl8xxxu_write8(priv, REG_RXDMA_PRO_8723B, val8);

		/*
		 * For USB high speed set 512B packets
		 */
		val8 = rtl8xxxu_read8(priv, REG_HT_SINGLE_AMPDU_8723B);
		val8 |= BIT(7);
		rtl8xxxu_write8(priv, REG_HT_SINGLE_AMPDU_8723B, val8);

		rtl8xxxu_write16(priv, REG_MAX_AGGR_NUM, 0x0c14);
		rtl8xxxu_write8(priv, REG_AMPDU_MAX_TIME_8723B, 0x5e);
		rtl8xxxu_write32(priv, REG_AGGLEN_LMT, 0xffffffff);
		rtl8xxxu_write8(priv, REG_RX_PKT_LIMIT, 0x18);
		rtl8xxxu_write8(priv, REG_PIFS, 0x00);
		rtl8xxxu_write8(priv, REG_USTIME_TSF_8723B, 0x50);
		rtl8xxxu_write8(priv, REG_USTIME_EDCA, 0x50);

		val8 = rtl8xxxu_read8(priv, REG_RSV_CTRL);
		val8 |= BIT(5) | BIT(6);
		rtl8xxxu_write8(priv, REG_RSV_CTRL, val8);
	}

	if (fops->init_aggregation)
		fops->init_aggregation(priv);

	/*
	 * Enable CCK and OFDM block
	 */
	val32 = rtl8xxxu_read32(priv, REG_FPGA0_RF_MODE);
	val32 |= (FPGA_RF_MODE_CCK | FPGA_RF_MODE_OFDM);
	rtl8xxxu_write32(priv, REG_FPGA0_RF_MODE, val32);

	/*
	 * Invalidate all CAM entries - bit 30 is undocumented
	 */
	rtl8xxxu_write32(priv, REG_CAM_CMD, CAM_CMD_POLLING | BIT(30));

	/*
	 * Start out with default power levels for channel 6, 20MHz
	 */
	fops->set_tx_power(priv, 1, false);

	/* Let the 8051 take control of antenna setting */
	if (priv->rtl_chip != RTL8192E) {
		val8 = rtl8xxxu_read8(priv, REG_LEDCFG2);
		val8 |= LEDCFG2_DPDT_SELECT;
		rtl8xxxu_write8(priv, REG_LEDCFG2, val8);
	}

	rtl8xxxu_write8(priv, REG_HWSEQ_CTRL, 0xff);

	/* Disable BAR - not sure if this has any effect on USB */
	rtl8xxxu_write32(priv, REG_BAR_MODE_CTRL, 0x0201ffff);

	rtl8xxxu_write16(priv, REG_FAST_EDCA_CTRL, 0);

	if (fops->init_statistics)
		fops->init_statistics(priv);

	if (priv->rtl_chip == RTL8192E) {
		/*
		 * 0x4c6[3] 1: RTS BW = Data BW
		 * 0: RTS BW depends on CCA / secondary CCA result.
		 */
		val8 = rtl8xxxu_read8(priv, REG_QUEUE_CTRL);
		val8 &= ~BIT(3);
		rtl8xxxu_write8(priv, REG_QUEUE_CTRL, val8);
		/*
		 * Reset USB mode switch setting
		 */
		rtl8xxxu_write8(priv, REG_ACLK_MON, 0x00);
	}

	rtl8723a_phy_lc_calibrate(priv);

	fops->phy_iq_calibrate(priv);

	/*
	 * This should enable thermal meter
	 */
	if (fops->gen2_thermal_meter)
		rtl8xxxu_write_rfreg(priv,
				     RF_A, RF6052_REG_T_METER_8723B, 0x37cf8);
	else
		rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_T_METER, 0x60);

	/* Set NAV_UPPER to 30000us */
	val8 = ((30000 + NAV_UPPER_UNIT - 1) / NAV_UPPER_UNIT);
	rtl8xxxu_write8(priv, REG_NAV_UPPER, val8);

	if (priv->rtl_chip == RTL8723A) {
		/*
		 * 2011/03/09 MH debug only, UMC-B cut pass 2500 S5 test,
		 * but we need to find root cause.
		 * This is 8723au only.
		 */
		val32 = rtl8xxxu_read32(priv, REG_FPGA0_RF_MODE);
		if ((val32 & 0xff000000) != 0x83000000) {
			val32 |= FPGA_RF_MODE_CCK;
			rtl8xxxu_write32(priv, REG_FPGA0_RF_MODE, val32);
		}
	} else if (priv->rtl_chip == RTL8192E) {
		rtl8xxxu_write8(priv, REG_USB_HRPWM, 0x00);
	}

	val32 = rtl8xxxu_read32(priv, REG_FWHW_TXQ_CTRL);
	val32 |= FWHW_TXQ_CTRL_XMIT_MGMT_ACK;
	/* ack for xmit mgmt frames. */
	rtl8xxxu_write32(priv, REG_FWHW_TXQ_CTRL, val32);

	if (priv->rtl_chip == RTL8192E) {
		/*
		 * Fix LDPC rx hang issue.
		 */
		val32 = rtl8xxxu_read32(priv, REG_AFE_MISC);
		rtl8xxxu_write8(priv, REG_8192E_LDOV12_CTRL, 0x75);
		val32 &= 0xfff00fff;
		val32 |= 0x0007e000;
		rtl8xxxu_write32(priv, REG_AFE_MISC, val32);
	}
exit:
	return ret;
}

static void rtl8xxxu_cam_write(struct rtl8xxxu_priv *priv,
			       struct ieee80211_key_conf *key, const u8 *mac)
{
	u32 cmd, val32, addr, ctrl;
	int j, i, tmp_debug;

	tmp_debug = rtl8xxxu_debug;
	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_KEY)
		rtl8xxxu_debug |= RTL8XXXU_DEBUG_REG_WRITE;

	/*
	 * This is a bit of a hack - the lower bits of the cipher
	 * suite selector happens to match the cipher index in the CAM
	 */
	addr = key->keyidx << CAM_CMD_KEY_SHIFT;
	ctrl = (key->cipher & 0x0f) << 2 | key->keyidx | CAM_WRITE_VALID;

	for (j = 5; j >= 0; j--) {
		switch (j) {
		case 0:
			val32 = ctrl | (mac[0] << 16) | (mac[1] << 24);
			break;
		case 1:
			val32 = mac[2] | (mac[3] << 8) |
				(mac[4] << 16) | (mac[5] << 24);
			break;
		default:
			i = (j - 2) << 2;
			val32 = key->key[i] | (key->key[i + 1] << 8) |
				key->key[i + 2] << 16 | key->key[i + 3] << 24;
			break;
		}

		rtl8xxxu_write32(priv, REG_CAM_WRITE, val32);
		cmd = CAM_CMD_POLLING | CAM_CMD_WRITE | (addr + j);
		rtl8xxxu_write32(priv, REG_CAM_CMD, cmd);
		udelay(100);
	}

	rtl8xxxu_debug = tmp_debug;
}

static void rtl8xxxu_sw_scan_start(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif, const u8 *mac)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	u8 val8;

	val8 = rtl8xxxu_read8(priv, REG_BEACON_CTRL);
	val8 |= BEACON_DISABLE_TSF_UPDATE;
	rtl8xxxu_write8(priv, REG_BEACON_CTRL, val8);
}

static void rtl8xxxu_sw_scan_complete(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	u8 val8;

	val8 = rtl8xxxu_read8(priv, REG_BEACON_CTRL);
	val8 &= ~BEACON_DISABLE_TSF_UPDATE;
	rtl8xxxu_write8(priv, REG_BEACON_CTRL, val8);
}

void rtl8xxxu_update_rate_mask(struct rtl8xxxu_priv *priv,
			       u32 ramask, u8 rateid, int sgi)
{
	struct h2c_cmd h2c;

	memset(&h2c, 0, sizeof(struct h2c_cmd));

	h2c.ramask.cmd = H2C_SET_RATE_MASK;
	h2c.ramask.mask_lo = cpu_to_le16(ramask & 0xffff);
	h2c.ramask.mask_hi = cpu_to_le16(ramask >> 16);

	h2c.ramask.arg = 0x80;
	if (sgi)
		h2c.ramask.arg |= 0x20;

	dev_dbg(&priv->udev->dev, "%s: rate mask %08x, arg %02x, size %zi\n",
		__func__, ramask, h2c.ramask.arg, sizeof(h2c.ramask));
	rtl8xxxu_gen1_h2c_cmd(priv, &h2c, sizeof(h2c.ramask));
}

void rtl8xxxu_gen2_update_rate_mask(struct rtl8xxxu_priv *priv,
				    u32 ramask, u8 rateid, int sgi)
{
	struct h2c_cmd h2c;
	u8 bw = RTL8XXXU_CHANNEL_WIDTH_20;

	memset(&h2c, 0, sizeof(struct h2c_cmd));

	h2c.b_macid_cfg.cmd = H2C_8723B_MACID_CFG_RAID;
	h2c.b_macid_cfg.ramask0 = ramask & 0xff;
	h2c.b_macid_cfg.ramask1 = (ramask >> 8) & 0xff;
	h2c.b_macid_cfg.ramask2 = (ramask >> 16) & 0xff;
	h2c.b_macid_cfg.ramask3 = (ramask >> 24) & 0xff;

	h2c.ramask.arg = 0x80;
	h2c.b_macid_cfg.data1 = rateid;
	if (sgi)
		h2c.b_macid_cfg.data1 |= BIT(7);

	h2c.b_macid_cfg.data2 = bw;

	dev_dbg(&priv->udev->dev, "%s: rate mask %08x, arg %02x, size %zi\n",
		__func__, ramask, h2c.ramask.arg, sizeof(h2c.b_macid_cfg));
	rtl8xxxu_gen2_h2c_cmd(priv, &h2c, sizeof(h2c.b_macid_cfg));
}

void rtl8xxxu_gen1_report_connect(struct rtl8xxxu_priv *priv,
				  u8 macid, bool connect)
{
	struct h2c_cmd h2c;

	memset(&h2c, 0, sizeof(struct h2c_cmd));

	h2c.joinbss.cmd = H2C_JOIN_BSS_REPORT;

	if (connect)
		h2c.joinbss.data = H2C_JOIN_BSS_CONNECT;
	else
		h2c.joinbss.data = H2C_JOIN_BSS_DISCONNECT;

	rtl8xxxu_gen1_h2c_cmd(priv, &h2c, sizeof(h2c.joinbss));
}

void rtl8xxxu_gen2_report_connect(struct rtl8xxxu_priv *priv,
				  u8 macid, bool connect)
{
#ifdef RTL8XXXU_GEN2_REPORT_CONNECT
	/*
	 * Barry Day reports this causes issues with 8192eu and 8723bu
	 * devices reconnecting. The reason for this is unclear, but
	 * until it is better understood, leave the code in place but
	 * disabled, so it is not lost.
	 */
	struct h2c_cmd h2c;

	memset(&h2c, 0, sizeof(struct h2c_cmd));

	h2c.media_status_rpt.cmd = H2C_8723B_MEDIA_STATUS_RPT;
	if (connect)
		h2c.media_status_rpt.parm |= BIT(0);
	else
		h2c.media_status_rpt.parm &= ~BIT(0);

	rtl8xxxu_gen2_h2c_cmd(priv, &h2c, sizeof(h2c.media_status_rpt));
#endif
}

void rtl8xxxu_gen1_init_aggregation(struct rtl8xxxu_priv *priv)
{
	u8 agg_ctrl, usb_spec, page_thresh, timeout;

	usb_spec = rtl8xxxu_read8(priv, REG_USB_SPECIAL_OPTION);
	usb_spec &= ~USB_SPEC_USB_AGG_ENABLE;
	rtl8xxxu_write8(priv, REG_USB_SPECIAL_OPTION, usb_spec);

	agg_ctrl = rtl8xxxu_read8(priv, REG_TRXDMA_CTRL);
	agg_ctrl &= ~TRXDMA_CTRL_RXDMA_AGG_EN;

	if (!rtl8xxxu_dma_aggregation) {
		rtl8xxxu_write8(priv, REG_TRXDMA_CTRL, agg_ctrl);
		return;
	}

	agg_ctrl |= TRXDMA_CTRL_RXDMA_AGG_EN;
	rtl8xxxu_write8(priv, REG_TRXDMA_CTRL, agg_ctrl);

	/*
	 * The number of packets we can take looks to be buffer size / 512
	 * which matches the 512 byte rounding we have to do when de-muxing
	 * the packets.
	 *
	 * Sample numbers from the vendor driver:
	 * USB High-Speed mode values:
	 *   RxAggBlockCount = 8 : 512 byte unit
	 *   RxAggBlockTimeout = 6
	 *   RxAggPageCount = 48 : 128 byte unit
	 *   RxAggPageTimeout = 4 or 6 (absolute time 34ms/(2^6))
	 */

	page_thresh = (priv->fops->rx_agg_buf_size / 512);
	if (rtl8xxxu_dma_agg_pages >= 0) {
		if (rtl8xxxu_dma_agg_pages <= page_thresh)
			timeout = page_thresh;
		else if (rtl8xxxu_dma_agg_pages <= 6)
			dev_err(&priv->udev->dev,
				"%s: dma_agg_pages=%i too small, minimum is 6\n",
				__func__, rtl8xxxu_dma_agg_pages);
		else
			dev_err(&priv->udev->dev,
				"%s: dma_agg_pages=%i larger than limit %i\n",
				__func__, rtl8xxxu_dma_agg_pages, page_thresh);
	}
	rtl8xxxu_write8(priv, REG_RXDMA_AGG_PG_TH, page_thresh);
	/*
	 * REG_RXDMA_AGG_PG_TH + 1 seems to be the timeout register on
	 * gen2 chips and rtl8188eu. The rtl8723au seems unhappy if we
	 * don't set it, so better set both.
	 */
	timeout = 4;

	if (rtl8xxxu_dma_agg_timeout >= 0) {
		if (rtl8xxxu_dma_agg_timeout <= 127)
			timeout = rtl8xxxu_dma_agg_timeout;
		else
			dev_err(&priv->udev->dev,
				"%s: Invalid dma_agg_timeout: %i\n",
				__func__, rtl8xxxu_dma_agg_timeout);
	}

	rtl8xxxu_write8(priv, REG_RXDMA_AGG_PG_TH + 1, timeout);
	rtl8xxxu_write8(priv, REG_USB_DMA_AGG_TO, timeout);
	priv->rx_buf_aggregation = 1;
}

static void rtl8xxxu_set_basic_rates(struct rtl8xxxu_priv *priv, u32 rate_cfg)
{
	u32 val32;
	u8 rate_idx = 0;

	rate_cfg &= RESPONSE_RATE_BITMAP_ALL;

	val32 = rtl8xxxu_read32(priv, REG_RESPONSE_RATE_SET);
	val32 &= ~RESPONSE_RATE_BITMAP_ALL;
	val32 |= rate_cfg;
	rtl8xxxu_write32(priv, REG_RESPONSE_RATE_SET, val32);

	dev_dbg(&priv->udev->dev, "%s: rates %08x\n", __func__,	rate_cfg);

	while (rate_cfg) {
		rate_cfg = (rate_cfg >> 1);
		rate_idx++;
	}
	rtl8xxxu_write8(priv, REG_INIRTS_RATE_SEL, rate_idx);
}

static u16
rtl8xxxu_wireless_mode(struct ieee80211_hw *hw, struct ieee80211_sta *sta)
{
	u16 network_type = WIRELESS_MODE_UNKNOWN;

	if (hw->conf.chandef.chan->band == NL80211_BAND_5GHZ) {
		if (sta->vht_cap.vht_supported)
			network_type = WIRELESS_MODE_AC;
		else if (sta->ht_cap.ht_supported)
			network_type = WIRELESS_MODE_N_5G;

		network_type |= WIRELESS_MODE_A;
	} else {
		if (sta->vht_cap.vht_supported)
			network_type = WIRELESS_MODE_AC;
		else if (sta->ht_cap.ht_supported)
			network_type = WIRELESS_MODE_N_24G;

		if (sta->supp_rates[0] <= 0xf)
			network_type |= WIRELESS_MODE_B;
		else if (sta->supp_rates[0] & 0xf)
			network_type |= (WIRELESS_MODE_B | WIRELESS_MODE_G);
		else
			network_type |= WIRELESS_MODE_G;
	}

	return network_type;
}

static void
rtl8xxxu_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *bss_conf, u32 changed)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	struct device *dev = &priv->udev->dev;
	struct ieee80211_sta *sta;
	u32 val32;
	u8 val8;

	if (changed & BSS_CHANGED_ASSOC) {
		dev_dbg(dev, "Changed ASSOC: %i!\n", bss_conf->assoc);

		rtl8xxxu_set_linktype(priv, vif->type);

		if (bss_conf->assoc) {
			u32 ramask;
			int sgi = 0;

			rcu_read_lock();
			sta = ieee80211_find_sta(vif, bss_conf->bssid);
			if (!sta) {
				dev_info(dev, "%s: ASSOC no sta found\n",
					 __func__);
				rcu_read_unlock();
				goto error;
			}

			if (sta->ht_cap.ht_supported)
				dev_info(dev, "%s: HT supported\n", __func__);
			if (sta->vht_cap.vht_supported)
				dev_info(dev, "%s: VHT supported\n", __func__);

			/* TODO: Set bits 28-31 for rate adaptive id */
			ramask = (sta->supp_rates[0] & 0xfff) |
				sta->ht_cap.mcs.rx_mask[0] << 12 |
				sta->ht_cap.mcs.rx_mask[1] << 20;
			if (sta->ht_cap.cap &
			    (IEEE80211_HT_CAP_SGI_40 | IEEE80211_HT_CAP_SGI_20))
				sgi = 1;
			rcu_read_unlock();

			priv->vif = vif;
			priv->rssi_level = RTL8XXXU_RATR_STA_INIT;

			priv->fops->update_rate_mask(priv, ramask, 0, sgi);

			rtl8xxxu_write8(priv, REG_BCN_MAX_ERR, 0xff);

			rtl8xxxu_stop_tx_beacon(priv);

			/* joinbss sequence */
			rtl8xxxu_write16(priv, REG_BCN_PSR_RPT,
					 0xc000 | bss_conf->aid);

			priv->fops->report_connect(priv, 0, true);
		} else {
			val8 = rtl8xxxu_read8(priv, REG_BEACON_CTRL);
			val8 |= BEACON_DISABLE_TSF_UPDATE;
			rtl8xxxu_write8(priv, REG_BEACON_CTRL, val8);

			priv->fops->report_connect(priv, 0, false);
		}
	}

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		dev_dbg(dev, "Changed ERP_PREAMBLE: Use short preamble %i\n",
			bss_conf->use_short_preamble);
		val32 = rtl8xxxu_read32(priv, REG_RESPONSE_RATE_SET);
		if (bss_conf->use_short_preamble)
			val32 |= RSR_ACK_SHORT_PREAMBLE;
		else
			val32 &= ~RSR_ACK_SHORT_PREAMBLE;
		rtl8xxxu_write32(priv, REG_RESPONSE_RATE_SET, val32);
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		dev_dbg(dev, "Changed ERP_SLOT: short_slot_time %i\n",
			bss_conf->use_short_slot);

		if (bss_conf->use_short_slot)
			val8 = 9;
		else
			val8 = 20;
		rtl8xxxu_write8(priv, REG_SLOT, val8);
	}

	if (changed & BSS_CHANGED_BSSID) {
		dev_dbg(dev, "Changed BSSID!\n");
		rtl8xxxu_set_bssid(priv, bss_conf->bssid);
	}

	if (changed & BSS_CHANGED_BASIC_RATES) {
		dev_dbg(dev, "Changed BASIC_RATES!\n");
		rtl8xxxu_set_basic_rates(priv, bss_conf->basic_rates);
	}
error:
	return;
}

static u32 rtl8xxxu_80211_to_rtl_queue(u32 queue)
{
	u32 rtlqueue;

	switch (queue) {
	case IEEE80211_AC_VO:
		rtlqueue = TXDESC_QUEUE_VO;
		break;
	case IEEE80211_AC_VI:
		rtlqueue = TXDESC_QUEUE_VI;
		break;
	case IEEE80211_AC_BE:
		rtlqueue = TXDESC_QUEUE_BE;
		break;
	case IEEE80211_AC_BK:
		rtlqueue = TXDESC_QUEUE_BK;
		break;
	default:
		rtlqueue = TXDESC_QUEUE_BE;
	}

	return rtlqueue;
}

static u32 rtl8xxxu_queue_select(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	u32 queue;

	if (ieee80211_is_mgmt(hdr->frame_control))
		queue = TXDESC_QUEUE_MGNT;
	else
		queue = rtl8xxxu_80211_to_rtl_queue(skb_get_queue_mapping(skb));

	return queue;
}

/*
 * Despite newer chips 8723b/8812/8821 having a larger TX descriptor
 * format. The descriptor checksum is still only calculated over the
 * initial 32 bytes of the descriptor!
 */
static void rtl8xxxu_calc_tx_desc_csum(struct rtl8xxxu_txdesc32 *tx_desc)
{
	__le16 *ptr = (__le16 *)tx_desc;
	u16 csum = 0;
	int i;

	/*
	 * Clear csum field before calculation, as the csum field is
	 * in the middle of the struct.
	 */
	tx_desc->csum = cpu_to_le16(0);

	for (i = 0; i < (sizeof(struct rtl8xxxu_txdesc32) / sizeof(u16)); i++)
		csum = csum ^ le16_to_cpu(ptr[i]);

	tx_desc->csum |= cpu_to_le16(csum);
}

static void rtl8xxxu_free_tx_resources(struct rtl8xxxu_priv *priv)
{
	struct rtl8xxxu_tx_urb *tx_urb, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&priv->tx_urb_lock, flags);
	list_for_each_entry_safe(tx_urb, tmp, &priv->tx_urb_free_list, list) {
		list_del(&tx_urb->list);
		priv->tx_urb_free_count--;
		usb_free_urb(&tx_urb->urb);
	}
	spin_unlock_irqrestore(&priv->tx_urb_lock, flags);
}

static struct rtl8xxxu_tx_urb *
rtl8xxxu_alloc_tx_urb(struct rtl8xxxu_priv *priv)
{
	struct rtl8xxxu_tx_urb *tx_urb;
	unsigned long flags;

	spin_lock_irqsave(&priv->tx_urb_lock, flags);
	tx_urb = list_first_entry_or_null(&priv->tx_urb_free_list,
					  struct rtl8xxxu_tx_urb, list);
	if (tx_urb) {
		list_del(&tx_urb->list);
		priv->tx_urb_free_count--;
		if (priv->tx_urb_free_count < RTL8XXXU_TX_URB_LOW_WATER &&
		    !priv->tx_stopped) {
			priv->tx_stopped = true;
			ieee80211_stop_queues(priv->hw);
		}
	}

	spin_unlock_irqrestore(&priv->tx_urb_lock, flags);

	return tx_urb;
}

static void rtl8xxxu_free_tx_urb(struct rtl8xxxu_priv *priv,
				 struct rtl8xxxu_tx_urb *tx_urb)
{
	unsigned long flags;

	INIT_LIST_HEAD(&tx_urb->list);

	spin_lock_irqsave(&priv->tx_urb_lock, flags);

	list_add(&tx_urb->list, &priv->tx_urb_free_list);
	priv->tx_urb_free_count++;
	if (priv->tx_urb_free_count > RTL8XXXU_TX_URB_HIGH_WATER &&
	    priv->tx_stopped) {
		priv->tx_stopped = false;
		ieee80211_wake_queues(priv->hw);
	}

	spin_unlock_irqrestore(&priv->tx_urb_lock, flags);
}

static void rtl8xxxu_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = (struct sk_buff *)urb->context;
	struct ieee80211_tx_info *tx_info;
	struct ieee80211_hw *hw;
	struct rtl8xxxu_priv *priv;
	struct rtl8xxxu_tx_urb *tx_urb =
		container_of(urb, struct rtl8xxxu_tx_urb, urb);

	tx_info = IEEE80211_SKB_CB(skb);
	hw = tx_info->rate_driver_data[0];
	priv = hw->priv;

	skb_pull(skb, priv->fops->tx_desc_size);

	ieee80211_tx_info_clear_status(tx_info);
	tx_info->status.rates[0].idx = -1;
	tx_info->status.rates[0].count = 0;

	if (!urb->status)
		tx_info->flags |= IEEE80211_TX_STAT_ACK;

	ieee80211_tx_status_irqsafe(hw, skb);

	rtl8xxxu_free_tx_urb(priv, tx_urb);
}

static void rtl8xxxu_dump_action(struct device *dev,
				 struct ieee80211_hdr *hdr)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)hdr;
	u16 cap, timeout;

	if (!(rtl8xxxu_debug & RTL8XXXU_DEBUG_ACTION))
		return;

	switch (mgmt->u.action.u.addba_resp.action_code) {
	case WLAN_ACTION_ADDBA_RESP:
		cap = le16_to_cpu(mgmt->u.action.u.addba_resp.capab);
		timeout = le16_to_cpu(mgmt->u.action.u.addba_resp.timeout);
		dev_info(dev, "WLAN_ACTION_ADDBA_RESP: "
			 "timeout %i, tid %02x, buf_size %02x, policy %02x, "
			 "status %02x\n",
			 timeout,
			 (cap & IEEE80211_ADDBA_PARAM_TID_MASK) >> 2,
			 (cap & IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK) >> 6,
			 (cap >> 1) & 0x1,
			 le16_to_cpu(mgmt->u.action.u.addba_resp.status));
		break;
	case WLAN_ACTION_ADDBA_REQ:
		cap = le16_to_cpu(mgmt->u.action.u.addba_req.capab);
		timeout = le16_to_cpu(mgmt->u.action.u.addba_req.timeout);
		dev_info(dev, "WLAN_ACTION_ADDBA_REQ: "
			 "timeout %i, tid %02x, buf_size %02x, policy %02x\n",
			 timeout,
			 (cap & IEEE80211_ADDBA_PARAM_TID_MASK) >> 2,
			 (cap & IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK) >> 6,
			 (cap >> 1) & 0x1);
		break;
	default:
		dev_info(dev, "action frame %02x\n",
			 mgmt->u.action.u.addba_resp.action_code);
		break;
	}
}

/*
 * Fill in v1 (gen1) specific TX descriptor bits.
 * This format is used on 8188cu/8192cu/8723au
 */
void
rtl8xxxu_fill_txdesc_v1(struct ieee80211_hw *hw, struct ieee80211_hdr *hdr,
			struct ieee80211_tx_info *tx_info,
			struct rtl8xxxu_txdesc32 *tx_desc, bool sgi,
			bool short_preamble, bool ampdu_enable, u32 rts_rate)
{
	struct ieee80211_rate *tx_rate = ieee80211_get_tx_rate(hw, tx_info);
	struct rtl8xxxu_priv *priv = hw->priv;
	struct device *dev = &priv->udev->dev;
	u8 *qc = ieee80211_get_qos_ctl(hdr);
	u8 tid = qc[0] & IEEE80211_QOS_CTL_TID_MASK;
	u32 rate;
	u16 rate_flags = tx_info->control.rates[0].flags;
	u16 seq_number;

	if (rate_flags & IEEE80211_TX_RC_MCS &&
	    !ieee80211_is_mgmt(hdr->frame_control))
		rate = tx_info->control.rates[0].idx + DESC_RATE_MCS0;
	else
		rate = tx_rate->hw_value;

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_TX)
		dev_info(dev, "%s: TX rate: %d, pkt size %u\n",
			 __func__, rate, le16_to_cpu(tx_desc->pkt_size));

	seq_number = IEEE80211_SEQ_TO_SN(le16_to_cpu(hdr->seq_ctrl));

	tx_desc->txdw5 = cpu_to_le32(rate);

	if (ieee80211_is_data(hdr->frame_control))
		tx_desc->txdw5 |= cpu_to_le32(0x0001ff00);

	tx_desc->txdw3 = cpu_to_le32((u32)seq_number << TXDESC32_SEQ_SHIFT);

	if (ampdu_enable && test_bit(tid, priv->tid_tx_operational))
		tx_desc->txdw1 |= cpu_to_le32(TXDESC32_AGG_ENABLE);
	else
		tx_desc->txdw1 |= cpu_to_le32(TXDESC32_AGG_BREAK);

	if (ieee80211_is_mgmt(hdr->frame_control)) {
		tx_desc->txdw5 = cpu_to_le32(rate);
		tx_desc->txdw4 |= cpu_to_le32(TXDESC32_USE_DRIVER_RATE);
		tx_desc->txdw5 |= cpu_to_le32(6 << TXDESC32_RETRY_LIMIT_SHIFT);
		tx_desc->txdw5 |= cpu_to_le32(TXDESC32_RETRY_LIMIT_ENABLE);
	}

	if (ieee80211_is_data_qos(hdr->frame_control))
		tx_desc->txdw4 |= cpu_to_le32(TXDESC32_QOS);

	if (short_preamble)
		tx_desc->txdw4 |= cpu_to_le32(TXDESC32_SHORT_PREAMBLE);

	if (sgi)
		tx_desc->txdw5 |= cpu_to_le32(TXDESC32_SHORT_GI);

	/*
	 * rts_rate is zero if RTS/CTS or CTS to SELF are not enabled
	 */
	tx_desc->txdw4 |= cpu_to_le32(rts_rate << TXDESC32_RTS_RATE_SHIFT);
	if (rate_flags & IEEE80211_TX_RC_USE_RTS_CTS) {
		tx_desc->txdw4 |= cpu_to_le32(TXDESC32_RTS_CTS_ENABLE);
		tx_desc->txdw4 |= cpu_to_le32(TXDESC32_HW_RTS_ENABLE);
	} else if (rate_flags & IEEE80211_TX_RC_USE_CTS_PROTECT) {
		tx_desc->txdw4 |= cpu_to_le32(TXDESC32_CTS_SELF_ENABLE);
		tx_desc->txdw4 |= cpu_to_le32(TXDESC32_HW_RTS_ENABLE);
	}
}

/*
 * Fill in v2 (gen2) specific TX descriptor bits.
 * This format is used on 8192eu/8723bu
 */
void
rtl8xxxu_fill_txdesc_v2(struct ieee80211_hw *hw, struct ieee80211_hdr *hdr,
			struct ieee80211_tx_info *tx_info,
			struct rtl8xxxu_txdesc32 *tx_desc32, bool sgi,
			bool short_preamble, bool ampdu_enable, u32 rts_rate)
{
	struct ieee80211_rate *tx_rate = ieee80211_get_tx_rate(hw, tx_info);
	struct rtl8xxxu_priv *priv = hw->priv;
	struct device *dev = &priv->udev->dev;
	struct rtl8xxxu_txdesc40 *tx_desc40;
	u8 *qc = ieee80211_get_qos_ctl(hdr);
	u8 tid = qc[0] & IEEE80211_QOS_CTL_TID_MASK;
	u32 rate;
	u16 rate_flags = tx_info->control.rates[0].flags;
	u16 seq_number;

	tx_desc40 = (struct rtl8xxxu_txdesc40 *)tx_desc32;

	if (rate_flags & IEEE80211_TX_RC_MCS &&
	    !ieee80211_is_mgmt(hdr->frame_control))
		rate = tx_info->control.rates[0].idx + DESC_RATE_MCS0;
	else
		rate = tx_rate->hw_value;

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_TX)
		dev_info(dev, "%s: TX rate: %d, pkt size %u\n",
			 __func__, rate, le16_to_cpu(tx_desc40->pkt_size));

	seq_number = IEEE80211_SEQ_TO_SN(le16_to_cpu(hdr->seq_ctrl));

	tx_desc40->txdw4 = cpu_to_le32(rate);
	if (ieee80211_is_data(hdr->frame_control)) {
		tx_desc40->txdw4 |= cpu_to_le32(0x1f <<
						TXDESC40_DATA_RATE_FB_SHIFT);
	}

	tx_desc40->txdw9 = cpu_to_le32((u32)seq_number << TXDESC40_SEQ_SHIFT);

	if (ampdu_enable && test_bit(tid, priv->tid_tx_operational))
		tx_desc40->txdw2 |= cpu_to_le32(TXDESC40_AGG_ENABLE);
	else
		tx_desc40->txdw2 |= cpu_to_le32(TXDESC40_AGG_BREAK);

	if (ieee80211_is_mgmt(hdr->frame_control)) {
		tx_desc40->txdw4 = cpu_to_le32(rate);
		tx_desc40->txdw3 |= cpu_to_le32(TXDESC40_USE_DRIVER_RATE);
		tx_desc40->txdw4 |=
			cpu_to_le32(6 << TXDESC40_RETRY_LIMIT_SHIFT);
		tx_desc40->txdw4 |= cpu_to_le32(TXDESC40_RETRY_LIMIT_ENABLE);
	}

	if (short_preamble)
		tx_desc40->txdw5 |= cpu_to_le32(TXDESC40_SHORT_PREAMBLE);

	tx_desc40->txdw4 |= cpu_to_le32(rts_rate << TXDESC40_RTS_RATE_SHIFT);
	/*
	 * rts_rate is zero if RTS/CTS or CTS to SELF are not enabled
	 */
	if (rate_flags & IEEE80211_TX_RC_USE_RTS_CTS) {
		tx_desc40->txdw3 |= cpu_to_le32(TXDESC40_RTS_CTS_ENABLE);
		tx_desc40->txdw3 |= cpu_to_le32(TXDESC40_HW_RTS_ENABLE);
	} else if (rate_flags & IEEE80211_TX_RC_USE_CTS_PROTECT) {
		/*
		 * For some reason the vendor driver doesn't set
		 * TXDESC40_HW_RTS_ENABLE for CTS to SELF
		 */
		tx_desc40->txdw3 |= cpu_to_le32(TXDESC40_CTS_SELF_ENABLE);
	}
}

static void rtl8xxxu_tx(struct ieee80211_hw *hw,
			struct ieee80211_tx_control *control,
			struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct rtl8xxxu_priv *priv = hw->priv;
	struct rtl8xxxu_txdesc32 *tx_desc;
	struct rtl8xxxu_tx_urb *tx_urb;
	struct ieee80211_sta *sta = NULL;
	struct ieee80211_vif *vif = tx_info->control.vif;
	struct device *dev = &priv->udev->dev;
	u32 queue, rts_rate;
	u16 pktlen = skb->len;
	u16 rate_flag = tx_info->control.rates[0].flags;
	int tx_desc_size = priv->fops->tx_desc_size;
	int ret;
	bool ampdu_enable, sgi = false, short_preamble = false;

	if (skb_headroom(skb) < tx_desc_size) {
		dev_warn(dev,
			 "%s: Not enough headroom (%i) for tx descriptor\n",
			 __func__, skb_headroom(skb));
		goto error;
	}

	if (unlikely(skb->len > (65535 - tx_desc_size))) {
		dev_warn(dev, "%s: Trying to send over-sized skb (%i)\n",
			 __func__, skb->len);
		goto error;
	}

	tx_urb = rtl8xxxu_alloc_tx_urb(priv);
	if (!tx_urb) {
		dev_warn(dev, "%s: Unable to allocate tx urb\n", __func__);
		goto error;
	}

	if (ieee80211_is_action(hdr->frame_control))
		rtl8xxxu_dump_action(dev, hdr);

	tx_info->rate_driver_data[0] = hw;

	if (control && control->sta)
		sta = control->sta;

	queue = rtl8xxxu_queue_select(hw, skb);

	tx_desc = skb_push(skb, tx_desc_size);

	memset(tx_desc, 0, tx_desc_size);
	tx_desc->pkt_size = cpu_to_le16(pktlen);
	tx_desc->pkt_offset = tx_desc_size;

	tx_desc->txdw0 =
		TXDESC_OWN | TXDESC_FIRST_SEGMENT | TXDESC_LAST_SEGMENT;
	if (is_multicast_ether_addr(ieee80211_get_DA(hdr)) ||
	    is_broadcast_ether_addr(ieee80211_get_DA(hdr)))
		tx_desc->txdw0 |= TXDESC_BROADMULTICAST;

	tx_desc->txdw1 = cpu_to_le32(queue << TXDESC_QUEUE_SHIFT);

	if (tx_info->control.hw_key) {
		switch (tx_info->control.hw_key->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
		case WLAN_CIPHER_SUITE_TKIP:
			tx_desc->txdw1 |= cpu_to_le32(TXDESC_SEC_RC4);
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			tx_desc->txdw1 |= cpu_to_le32(TXDESC_SEC_AES);
			break;
		default:
			break;
		}
	}

	/* (tx_info->flags & IEEE80211_TX_CTL_AMPDU) && */
	ampdu_enable = false;
	if (ieee80211_is_data_qos(hdr->frame_control) && sta) {
		if (sta->ht_cap.ht_supported) {
			u32 ampdu, val32;
			u8 *qc = ieee80211_get_qos_ctl(hdr);
			u8 tid = qc[0] & IEEE80211_QOS_CTL_TID_MASK;

			ampdu = (u32)sta->ht_cap.ampdu_density;
			val32 = ampdu << TXDESC_AMPDU_DENSITY_SHIFT;
			tx_desc->txdw2 |= cpu_to_le32(val32);

			ampdu_enable = true;

			if (!test_bit(tid, priv->tx_aggr_started) &&
			    !(skb->protocol == cpu_to_be16(ETH_P_PAE)))
				if (!ieee80211_start_tx_ba_session(sta, tid, 0))
					set_bit(tid, priv->tx_aggr_started);
		}
	}

	if (rate_flag & IEEE80211_TX_RC_SHORT_GI ||
	    (ieee80211_is_data_qos(hdr->frame_control) &&
	     sta && sta->ht_cap.cap &
	     (IEEE80211_HT_CAP_SGI_40 | IEEE80211_HT_CAP_SGI_20)))
		sgi = true;

	if (rate_flag & IEEE80211_TX_RC_USE_SHORT_PREAMBLE ||
	    (sta && vif && vif->bss_conf.use_short_preamble))
		short_preamble = true;

	if (rate_flag & IEEE80211_TX_RC_USE_RTS_CTS)
		rts_rate = ieee80211_get_rts_cts_rate(hw, tx_info)->hw_value;
	else if (rate_flag & IEEE80211_TX_RC_USE_CTS_PROTECT)
		rts_rate = ieee80211_get_rts_cts_rate(hw, tx_info)->hw_value;
	else
		rts_rate = 0;


	priv->fops->fill_txdesc(hw, hdr, tx_info, tx_desc, sgi, short_preamble,
				ampdu_enable, rts_rate);

	rtl8xxxu_calc_tx_desc_csum(tx_desc);

	usb_fill_bulk_urb(&tx_urb->urb, priv->udev, priv->pipe_out[queue],
			  skb->data, skb->len, rtl8xxxu_tx_complete, skb);

	usb_anchor_urb(&tx_urb->urb, &priv->tx_anchor);
	ret = usb_submit_urb(&tx_urb->urb, GFP_ATOMIC);
	if (ret) {
		usb_unanchor_urb(&tx_urb->urb);
		rtl8xxxu_free_tx_urb(priv, tx_urb);
		goto error;
	}
	return;
error:
	dev_kfree_skb(skb);
}

static void rtl8xxxu_rx_parse_phystats(struct rtl8xxxu_priv *priv,
				       struct ieee80211_rx_status *rx_status,
				       struct rtl8723au_phy_stats *phy_stats,
				       u32 rxmcs)
{
	if (phy_stats->sgi_en)
		rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;

	if (rxmcs < DESC_RATE_6M) {
		/*
		 * Handle PHY stats for CCK rates
		 */
		u8 cck_agc_rpt = phy_stats->cck_agc_rpt_ofdm_cfosho_a;

		switch (cck_agc_rpt & 0xc0) {
		case 0xc0:
			rx_status->signal = -46 - (cck_agc_rpt & 0x3e);
			break;
		case 0x80:
			rx_status->signal = -26 - (cck_agc_rpt & 0x3e);
			break;
		case 0x40:
			rx_status->signal = -12 - (cck_agc_rpt & 0x3e);
			break;
		case 0x00:
			rx_status->signal = 16 - (cck_agc_rpt & 0x3e);
			break;
		}
	} else {
		rx_status->signal =
			(phy_stats->cck_sig_qual_ofdm_pwdb_all >> 1) - 110;
	}
}

static void rtl8xxxu_free_rx_resources(struct rtl8xxxu_priv *priv)
{
	struct rtl8xxxu_rx_urb *rx_urb, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&priv->rx_urb_lock, flags);

	list_for_each_entry_safe(rx_urb, tmp,
				 &priv->rx_urb_pending_list, list) {
		list_del(&rx_urb->list);
		priv->rx_urb_pending_count--;
		usb_free_urb(&rx_urb->urb);
	}

	spin_unlock_irqrestore(&priv->rx_urb_lock, flags);
}

static void rtl8xxxu_queue_rx_urb(struct rtl8xxxu_priv *priv,
				  struct rtl8xxxu_rx_urb *rx_urb)
{
	struct sk_buff *skb;
	unsigned long flags;
	int pending = 0;

	spin_lock_irqsave(&priv->rx_urb_lock, flags);

	if (!priv->shutdown) {
		list_add_tail(&rx_urb->list, &priv->rx_urb_pending_list);
		priv->rx_urb_pending_count++;
		pending = priv->rx_urb_pending_count;
	} else {
		skb = (struct sk_buff *)rx_urb->urb.context;
		dev_kfree_skb(skb);
		usb_free_urb(&rx_urb->urb);
	}

	spin_unlock_irqrestore(&priv->rx_urb_lock, flags);

	if (pending > RTL8XXXU_RX_URB_PENDING_WATER)
		schedule_work(&priv->rx_urb_wq);
}

static void rtl8xxxu_rx_urb_work(struct work_struct *work)
{
	struct rtl8xxxu_priv *priv;
	struct rtl8xxxu_rx_urb *rx_urb, *tmp;
	struct list_head local;
	struct sk_buff *skb;
	unsigned long flags;
	int ret;

	priv = container_of(work, struct rtl8xxxu_priv, rx_urb_wq);
	INIT_LIST_HEAD(&local);

	spin_lock_irqsave(&priv->rx_urb_lock, flags);

	list_splice_init(&priv->rx_urb_pending_list, &local);
	priv->rx_urb_pending_count = 0;

	spin_unlock_irqrestore(&priv->rx_urb_lock, flags);

	list_for_each_entry_safe(rx_urb, tmp, &local, list) {
		list_del_init(&rx_urb->list);
		ret = rtl8xxxu_submit_rx_urb(priv, rx_urb);
		/*
		 * If out of memory or temporary error, put it back on the
		 * queue and try again. Otherwise the device is dead/gone
		 * and we should drop it.
		 */
		switch (ret) {
		case 0:
			break;
		case -ENOMEM:
		case -EAGAIN:
			rtl8xxxu_queue_rx_urb(priv, rx_urb);
			break;
		default:
			pr_info("failed to requeue urb %i\n", ret);
			skb = (struct sk_buff *)rx_urb->urb.context;
			dev_kfree_skb(skb);
			usb_free_urb(&rx_urb->urb);
		}
	}
}

/*
 * The RTL8723BU/RTL8192EU vendor driver use coexistence table type
 * 0-7 to represent writing different combinations of register values
 * to REG_BT_COEX_TABLEs. It's for different kinds of coexistence use
 * cases which Realtek doesn't provide detail for these settings. Keep
 * this aligned with vendor driver for easier maintenance.
 */
static
void rtl8723bu_set_coex_with_type(struct rtl8xxxu_priv *priv, u8 type)
{
	switch (type) {
	case 0:
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE1, 0x55555555);
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE2, 0x55555555);
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE3, 0x00ffffff);
		rtl8xxxu_write8(priv, REG_BT_COEX_TABLE4, 0x03);
		break;
	case 1:
	case 3:
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE1, 0x55555555);
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE2, 0x5a5a5a5a);
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE3, 0x00ffffff);
		rtl8xxxu_write8(priv, REG_BT_COEX_TABLE4, 0x03);
		break;
	case 2:
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE1, 0x5a5a5a5a);
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE2, 0x5a5a5a5a);
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE3, 0x00ffffff);
		rtl8xxxu_write8(priv, REG_BT_COEX_TABLE4, 0x03);
		break;
	case 4:
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE1, 0x5a5a5a5a);
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE2, 0xaaaa5a5a);
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE3, 0x00ffffff);
		rtl8xxxu_write8(priv, REG_BT_COEX_TABLE4, 0x03);
		break;
	case 5:
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE1, 0x5a5a5a5a);
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE2, 0xaa5a5a5a);
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE3, 0x00ffffff);
		rtl8xxxu_write8(priv, REG_BT_COEX_TABLE4, 0x03);
		break;
	case 6:
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE1, 0x55555555);
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE2, 0xaaaaaaaa);
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE3, 0x00ffffff);
		rtl8xxxu_write8(priv, REG_BT_COEX_TABLE4, 0x03);
		break;
	case 7:
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE1, 0xaaaaaaaa);
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE2, 0xaaaaaaaa);
		rtl8xxxu_write32(priv, REG_BT_COEX_TABLE3, 0x00ffffff);
		rtl8xxxu_write8(priv, REG_BT_COEX_TABLE4, 0x03);
		break;
	default:
		break;
	}
}

static
void rtl8723bu_update_bt_link_info(struct rtl8xxxu_priv *priv, u8 bt_info)
{
	struct rtl8xxxu_btcoex *btcoex = &priv->bt_coex;

	if (bt_info & BT_INFO_8723B_1ANT_B_INQ_PAGE)
		btcoex->c2h_bt_inquiry = true;
	else
		btcoex->c2h_bt_inquiry = false;

	if (!(bt_info & BT_INFO_8723B_1ANT_B_CONNECTION)) {
		btcoex->bt_status = BT_8723B_1ANT_STATUS_NON_CONNECTED_IDLE;
		btcoex->has_sco = false;
		btcoex->has_hid = false;
		btcoex->has_pan = false;
		btcoex->has_a2dp = false;
	} else {
		if ((bt_info & 0x1f) == BT_INFO_8723B_1ANT_B_CONNECTION)
			btcoex->bt_status = BT_8723B_1ANT_STATUS_CONNECTED_IDLE;
		else if ((bt_info & BT_INFO_8723B_1ANT_B_SCO_ESCO) ||
			 (bt_info & BT_INFO_8723B_1ANT_B_SCO_BUSY))
			btcoex->bt_status = BT_8723B_1ANT_STATUS_SCO_BUSY;
		else if (bt_info & BT_INFO_8723B_1ANT_B_ACL_BUSY)
			btcoex->bt_status = BT_8723B_1ANT_STATUS_ACL_BUSY;
		else
			btcoex->bt_status = BT_8723B_1ANT_STATUS_MAX;

		if (bt_info & BT_INFO_8723B_1ANT_B_FTP)
			btcoex->has_pan = true;
		else
			btcoex->has_pan = false;

		if (bt_info & BT_INFO_8723B_1ANT_B_A2DP)
			btcoex->has_a2dp = true;
		else
			btcoex->has_a2dp = false;

		if (bt_info & BT_INFO_8723B_1ANT_B_HID)
			btcoex->has_hid = true;
		else
			btcoex->has_hid = false;

		if (bt_info & BT_INFO_8723B_1ANT_B_SCO_ESCO)
			btcoex->has_sco = true;
		else
			btcoex->has_sco = false;
	}

	if (!btcoex->has_a2dp && !btcoex->has_sco &&
	    !btcoex->has_pan && btcoex->has_hid)
		btcoex->hid_only = true;
	else
		btcoex->hid_only = false;

	if (!btcoex->has_sco && !btcoex->has_pan &&
	    !btcoex->has_hid && btcoex->has_a2dp)
		btcoex->has_a2dp = true;
	else
		btcoex->has_a2dp = false;

	if (btcoex->bt_status == BT_8723B_1ANT_STATUS_SCO_BUSY ||
	    btcoex->bt_status == BT_8723B_1ANT_STATUS_ACL_BUSY)
		btcoex->bt_busy = true;
	else
		btcoex->bt_busy = false;
}

static
void rtl8723bu_handle_bt_inquiry(struct rtl8xxxu_priv *priv)
{
	struct ieee80211_vif *vif;
	struct rtl8xxxu_btcoex *btcoex;
	bool wifi_connected;

	vif = priv->vif;
	btcoex = &priv->bt_coex;
	wifi_connected = (vif && vif->bss_conf.assoc);

	if (!wifi_connected) {
		rtl8723bu_set_ps_tdma(priv, 0x8, 0x0, 0x0, 0x0, 0x0);
		rtl8723bu_set_coex_with_type(priv, 0);
	} else if (btcoex->has_sco || btcoex->has_hid || btcoex->has_a2dp) {
		rtl8723bu_set_ps_tdma(priv, 0x61, 0x35, 0x3, 0x11, 0x11);
		rtl8723bu_set_coex_with_type(priv, 4);
	} else if (btcoex->has_pan) {
		rtl8723bu_set_ps_tdma(priv, 0x61, 0x3f, 0x3, 0x11, 0x11);
		rtl8723bu_set_coex_with_type(priv, 4);
	} else {
		rtl8723bu_set_ps_tdma(priv, 0x8, 0x0, 0x0, 0x0, 0x0);
		rtl8723bu_set_coex_with_type(priv, 7);
	}
}

static
void rtl8723bu_handle_bt_info(struct rtl8xxxu_priv *priv)
{
	struct ieee80211_vif *vif;
	struct rtl8xxxu_btcoex *btcoex;
	bool wifi_connected;

	vif = priv->vif;
	btcoex = &priv->bt_coex;
	wifi_connected = (vif && vif->bss_conf.assoc);

	if (wifi_connected) {
		u32 val32 = 0;
		u32 high_prio_tx = 0, high_prio_rx = 0;

		val32 = rtl8xxxu_read32(priv, 0x770);
		high_prio_tx = val32 & 0x0000ffff;
		high_prio_rx = (val32  & 0xffff0000) >> 16;

		if (btcoex->bt_busy) {
			if (btcoex->hid_only) {
				rtl8723bu_set_ps_tdma(priv, 0x61, 0x20,
						      0x3, 0x11, 0x11);
				rtl8723bu_set_coex_with_type(priv, 5);
			} else if (btcoex->a2dp_only) {
				rtl8723bu_set_ps_tdma(priv, 0x61, 0x35,
						      0x3, 0x11, 0x11);
				rtl8723bu_set_coex_with_type(priv, 4);
			} else if ((btcoex->has_a2dp && btcoex->has_pan) ||
				   (btcoex->has_hid && btcoex->has_a2dp &&
				    btcoex->has_pan)) {
				rtl8723bu_set_ps_tdma(priv, 0x51, 0x21,
						      0x3, 0x10, 0x10);
				rtl8723bu_set_coex_with_type(priv, 4);
			} else if (btcoex->has_hid && btcoex->has_a2dp) {
				rtl8723bu_set_ps_tdma(priv, 0x51, 0x21,
						      0x3, 0x10, 0x10);
				rtl8723bu_set_coex_with_type(priv, 3);
			} else {
				rtl8723bu_set_ps_tdma(priv, 0x61, 0x35,
						      0x3, 0x11, 0x11);
				rtl8723bu_set_coex_with_type(priv, 4);
			}
		} else {
			rtl8723bu_set_ps_tdma(priv, 0x8, 0x0, 0x0, 0x0, 0x0);
			if (high_prio_tx + high_prio_rx <= 60)
				rtl8723bu_set_coex_with_type(priv, 2);
			else
				rtl8723bu_set_coex_with_type(priv, 7);
		}
	} else {
		rtl8723bu_set_ps_tdma(priv, 0x8, 0x0, 0x0, 0x0, 0x0);
		rtl8723bu_set_coex_with_type(priv, 0);
	}
}

static struct ieee80211_rate rtl8xxxu_legacy_ratetable[] = {
	{.bitrate = 10, .hw_value = 0x00,},
	{.bitrate = 20, .hw_value = 0x01,},
	{.bitrate = 55, .hw_value = 0x02,},
	{.bitrate = 110, .hw_value = 0x03,},
	{.bitrate = 60, .hw_value = 0x04,},
	{.bitrate = 90, .hw_value = 0x05,},
	{.bitrate = 120, .hw_value = 0x06,},
	{.bitrate = 180, .hw_value = 0x07,},
	{.bitrate = 240, .hw_value = 0x08,},
	{.bitrate = 360, .hw_value = 0x09,},
	{.bitrate = 480, .hw_value = 0x0a,},
	{.bitrate = 540, .hw_value = 0x0b,},
};

static void rtl8xxxu_desc_to_mcsrate(u16 rate, u8 *mcs, u8 *nss)
{
	if (rate <= DESC_RATE_54M)
		return;

	if (rate >= DESC_RATE_MCS0 && rate <= DESC_RATE_MCS15) {
		if (rate < DESC_RATE_MCS8)
			*nss = 1;
		else
			*nss = 2;
		*mcs = rate - DESC_RATE_MCS0;
	}
}

static void rtl8xxxu_c2hcmd_callback(struct work_struct *work)
{
	struct rtl8xxxu_priv *priv;
	struct rtl8723bu_c2h *c2h;
	struct sk_buff *skb = NULL;
	u8 bt_info = 0;
	struct rtl8xxxu_btcoex *btcoex;
	struct rtl8xxxu_ra_report *rarpt;
	u8 rate, sgi, bw;
	u32 bit_rate;
	u8 mcs = 0, nss = 0;

	priv = container_of(work, struct rtl8xxxu_priv, c2hcmd_work);
	btcoex = &priv->bt_coex;
	rarpt = &priv->ra_report;

	if (priv->rf_paths > 1)
		goto out;

	while (!skb_queue_empty(&priv->c2hcmd_queue)) {
		skb = skb_dequeue(&priv->c2hcmd_queue);

		c2h = (struct rtl8723bu_c2h *)skb->data;

		switch (c2h->id) {
		case C2H_8723B_BT_INFO:
			bt_info = c2h->bt_info.bt_info;

			rtl8723bu_update_bt_link_info(priv, bt_info);
			if (btcoex->c2h_bt_inquiry) {
				rtl8723bu_handle_bt_inquiry(priv);
				break;
			}
			rtl8723bu_handle_bt_info(priv);
			break;
		case C2H_8723B_RA_REPORT:
			rarpt->txrate.flags = 0;
			rate = c2h->ra_report.rate;
			sgi = c2h->ra_report.sgi;
			bw = c2h->ra_report.bw;

			if (rate < DESC_RATE_MCS0) {
				rarpt->txrate.legacy =
					rtl8xxxu_legacy_ratetable[rate].bitrate;
			} else {
				rtl8xxxu_desc_to_mcsrate(rate, &mcs, &nss);
				rarpt->txrate.flags |= RATE_INFO_FLAGS_MCS;

				rarpt->txrate.mcs = mcs;
				rarpt->txrate.nss = nss;

				if (sgi) {
					rarpt->txrate.flags |=
						RATE_INFO_FLAGS_SHORT_GI;
				}

				if (bw == RATE_INFO_BW_20)
					rarpt->txrate.bw |= RATE_INFO_BW_20;
			}
			bit_rate = cfg80211_calculate_bitrate(&rarpt->txrate);
			rarpt->bit_rate = bit_rate;
			rarpt->desc_rate = rate;
			break;
		default:
			break;
		}
	}

out:
	dev_kfree_skb(skb);
}

static void rtl8723bu_handle_c2h(struct rtl8xxxu_priv *priv,
				 struct sk_buff *skb)
{
	struct rtl8723bu_c2h *c2h = (struct rtl8723bu_c2h *)skb->data;
	struct device *dev = &priv->udev->dev;
	int len;

	len = skb->len - 2;

	dev_dbg(dev, "C2H ID %02x seq %02x, len %02x source %02x\n",
		c2h->id, c2h->seq, len, c2h->bt_info.response_source);

	switch(c2h->id) {
	case C2H_8723B_BT_INFO:
		if (c2h->bt_info.response_source >
		    BT_INFO_SRC_8723B_BT_ACTIVE_SEND)
			dev_dbg(dev, "C2H_BT_INFO WiFi only firmware\n");
		else
			dev_dbg(dev, "C2H_BT_INFO BT/WiFi coexist firmware\n");

		if (c2h->bt_info.bt_has_reset)
			dev_dbg(dev, "BT has been reset\n");
		if (c2h->bt_info.tx_rx_mask)
			dev_dbg(dev, "BT TRx mask\n");

		break;
	case C2H_8723B_BT_MP_INFO:
		dev_dbg(dev, "C2H_MP_INFO ext ID %02x, status %02x\n",
			c2h->bt_mp_info.ext_id, c2h->bt_mp_info.status);
		break;
	case C2H_8723B_RA_REPORT:
		dev_dbg(dev,
			"C2H RA RPT: rate %02x, unk %i, macid %02x, noise %i\n",
			c2h->ra_report.rate, c2h->ra_report.sgi,
			c2h->ra_report.macid, c2h->ra_report.noisy_state);
		break;
	default:
		dev_info(dev, "Unhandled C2H event %02x seq %02x\n",
			 c2h->id, c2h->seq);
		print_hex_dump(KERN_INFO, "C2H content: ", DUMP_PREFIX_NONE,
			       16, 1, c2h->raw.payload, len, false);
		break;
	}

	skb_queue_tail(&priv->c2hcmd_queue, skb);

	schedule_work(&priv->c2hcmd_work);
}

int rtl8xxxu_parse_rxdesc16(struct rtl8xxxu_priv *priv, struct sk_buff *skb)
{
	struct ieee80211_hw *hw = priv->hw;
	struct ieee80211_rx_status *rx_status;
	struct rtl8xxxu_rxdesc16 *rx_desc;
	struct rtl8723au_phy_stats *phy_stats;
	struct sk_buff *next_skb = NULL;
	__le32 *_rx_desc_le;
	u32 *_rx_desc;
	int drvinfo_sz, desc_shift;
	int i, pkt_cnt, pkt_len, urb_len, pkt_offset;

	urb_len = skb->len;
	pkt_cnt = 0;

	if (urb_len < sizeof(struct rtl8xxxu_rxdesc16)) {
		kfree_skb(skb);
		return RX_TYPE_ERROR;
	}

	do {
		rx_desc = (struct rtl8xxxu_rxdesc16 *)skb->data;
		_rx_desc_le = (__le32 *)skb->data;
		_rx_desc = (u32 *)skb->data;

		for (i = 0;
		     i < (sizeof(struct rtl8xxxu_rxdesc16) / sizeof(u32)); i++)
			_rx_desc[i] = le32_to_cpu(_rx_desc_le[i]);

		/*
		 * Only read pkt_cnt from the header if we're parsing the
		 * first packet
		 */
		if (!pkt_cnt)
			pkt_cnt = rx_desc->pkt_cnt;
		pkt_len = rx_desc->pktlen;

		drvinfo_sz = rx_desc->drvinfo_sz * 8;
		desc_shift = rx_desc->shift;
		pkt_offset = roundup(pkt_len + drvinfo_sz + desc_shift +
				     sizeof(struct rtl8xxxu_rxdesc16), 128);

		/*
		 * Only clone the skb if there's enough data at the end to
		 * at least cover the rx descriptor
		 */
		if (pkt_cnt > 1 &&
		    urb_len >= (pkt_offset + sizeof(struct rtl8xxxu_rxdesc16)))
			next_skb = skb_clone(skb, GFP_ATOMIC);

		rx_status = IEEE80211_SKB_RXCB(skb);
		memset(rx_status, 0, sizeof(struct ieee80211_rx_status));

		skb_pull(skb, sizeof(struct rtl8xxxu_rxdesc16));

		phy_stats = (struct rtl8723au_phy_stats *)skb->data;

		skb_pull(skb, drvinfo_sz + desc_shift);

		skb_trim(skb, pkt_len);

		if (rx_desc->phy_stats)
			rtl8xxxu_rx_parse_phystats(priv, rx_status, phy_stats,
						   rx_desc->rxmcs);

		rx_status->mactime = rx_desc->tsfl;
		rx_status->flag |= RX_FLAG_MACTIME_START;

		if (!rx_desc->swdec)
			rx_status->flag |= RX_FLAG_DECRYPTED;
		if (rx_desc->crc32)
			rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
		if (rx_desc->bw)
			rx_status->bw = RATE_INFO_BW_40;

		if (rx_desc->rxht) {
			rx_status->encoding = RX_ENC_HT;
			rx_status->rate_idx = rx_desc->rxmcs - DESC_RATE_MCS0;
		} else {
			rx_status->rate_idx = rx_desc->rxmcs;
		}

		rx_status->freq = hw->conf.chandef.chan->center_freq;
		rx_status->band = hw->conf.chandef.chan->band;

		ieee80211_rx_irqsafe(hw, skb);

		skb = next_skb;
		if (skb)
			skb_pull(next_skb, pkt_offset);

		pkt_cnt--;
		urb_len -= pkt_offset;
		next_skb = NULL;
	} while (skb && pkt_cnt > 0 &&
		 urb_len >= sizeof(struct rtl8xxxu_rxdesc16));

	return RX_TYPE_DATA_PKT;
}

int rtl8xxxu_parse_rxdesc24(struct rtl8xxxu_priv *priv, struct sk_buff *skb)
{
	struct ieee80211_hw *hw = priv->hw;
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct rtl8xxxu_rxdesc24 *rx_desc =
		(struct rtl8xxxu_rxdesc24 *)skb->data;
	struct rtl8723au_phy_stats *phy_stats;
	__le32 *_rx_desc_le = (__le32 *)skb->data;
	u32 *_rx_desc = (u32 *)skb->data;
	int drvinfo_sz, desc_shift;
	int i;

	for (i = 0; i < (sizeof(struct rtl8xxxu_rxdesc24) / sizeof(u32)); i++)
		_rx_desc[i] = le32_to_cpu(_rx_desc_le[i]);

	memset(rx_status, 0, sizeof(struct ieee80211_rx_status));

	skb_pull(skb, sizeof(struct rtl8xxxu_rxdesc24));

	phy_stats = (struct rtl8723au_phy_stats *)skb->data;

	drvinfo_sz = rx_desc->drvinfo_sz * 8;
	desc_shift = rx_desc->shift;
	skb_pull(skb, drvinfo_sz + desc_shift);

	if (rx_desc->rpt_sel) {
		struct device *dev = &priv->udev->dev;
		dev_dbg(dev, "%s: C2H packet\n", __func__);
		rtl8723bu_handle_c2h(priv, skb);
		return RX_TYPE_C2H;
	}

	if (rx_desc->phy_stats)
		rtl8xxxu_rx_parse_phystats(priv, rx_status, phy_stats,
					   rx_desc->rxmcs);

	rx_status->mactime = rx_desc->tsfl;
	rx_status->flag |= RX_FLAG_MACTIME_START;

	if (!rx_desc->swdec)
		rx_status->flag |= RX_FLAG_DECRYPTED;
	if (rx_desc->crc32)
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
	if (rx_desc->bw)
		rx_status->bw = RATE_INFO_BW_40;

	if (rx_desc->rxmcs >= DESC_RATE_MCS0) {
		rx_status->encoding = RX_ENC_HT;
		rx_status->rate_idx = rx_desc->rxmcs - DESC_RATE_MCS0;
	} else {
		rx_status->rate_idx = rx_desc->rxmcs;
	}

	rx_status->freq = hw->conf.chandef.chan->center_freq;
	rx_status->band = hw->conf.chandef.chan->band;

	ieee80211_rx_irqsafe(hw, skb);
	return RX_TYPE_DATA_PKT;
}

static void rtl8xxxu_rx_complete(struct urb *urb)
{
	struct rtl8xxxu_rx_urb *rx_urb =
		container_of(urb, struct rtl8xxxu_rx_urb, urb);
	struct ieee80211_hw *hw = rx_urb->hw;
	struct rtl8xxxu_priv *priv = hw->priv;
	struct sk_buff *skb = (struct sk_buff *)urb->context;
	struct device *dev = &priv->udev->dev;

	skb_put(skb, urb->actual_length);

	if (urb->status == 0) {
		priv->fops->parse_rx_desc(priv, skb);

		skb = NULL;
		rx_urb->urb.context = NULL;
		rtl8xxxu_queue_rx_urb(priv, rx_urb);
	} else {
		dev_dbg(dev, "%s: status %i\n",	__func__, urb->status);
		goto cleanup;
	}
	return;

cleanup:
	usb_free_urb(urb);
	dev_kfree_skb(skb);
	return;
}

static int rtl8xxxu_submit_rx_urb(struct rtl8xxxu_priv *priv,
				  struct rtl8xxxu_rx_urb *rx_urb)
{
	struct rtl8xxxu_fileops *fops = priv->fops;
	struct sk_buff *skb;
	int skb_size;
	int ret, rx_desc_sz;

	rx_desc_sz = fops->rx_desc_size;

	if (priv->rx_buf_aggregation && fops->rx_agg_buf_size) {
		skb_size = fops->rx_agg_buf_size;
		skb_size += (rx_desc_sz + sizeof(struct rtl8723au_phy_stats));
	} else {
		skb_size = IEEE80211_MAX_FRAME_LEN;
	}

	skb = __netdev_alloc_skb(NULL, skb_size, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	memset(skb->data, 0, rx_desc_sz);
	usb_fill_bulk_urb(&rx_urb->urb, priv->udev, priv->pipe_in, skb->data,
			  skb_size, rtl8xxxu_rx_complete, skb);
	usb_anchor_urb(&rx_urb->urb, &priv->rx_anchor);
	ret = usb_submit_urb(&rx_urb->urb, GFP_ATOMIC);
	if (ret)
		usb_unanchor_urb(&rx_urb->urb);
	return ret;
}

static void rtl8xxxu_int_complete(struct urb *urb)
{
	struct rtl8xxxu_priv *priv = (struct rtl8xxxu_priv *)urb->context;
	struct device *dev = &priv->udev->dev;
	int ret;

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_INTERRUPT)
		dev_dbg(dev, "%s: status %i\n", __func__, urb->status);
	if (urb->status == 0) {
		usb_anchor_urb(urb, &priv->int_anchor);
		ret = usb_submit_urb(urb, GFP_ATOMIC);
		if (ret)
			usb_unanchor_urb(urb);
	} else {
		dev_dbg(dev, "%s: Error %i\n", __func__, urb->status);
	}
}


static int rtl8xxxu_submit_int_urb(struct ieee80211_hw *hw)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	struct urb *urb;
	u32 val32;
	int ret;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;

	usb_fill_int_urb(urb, priv->udev, priv->pipe_interrupt,
			 priv->int_buf, USB_INTR_CONTENT_LENGTH,
			 rtl8xxxu_int_complete, priv, 1);
	usb_anchor_urb(urb, &priv->int_anchor);
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret) {
		usb_unanchor_urb(urb);
		goto error;
	}

	val32 = rtl8xxxu_read32(priv, REG_USB_HIMR);
	val32 |= USB_HIMR_CPWM;
	rtl8xxxu_write32(priv, REG_USB_HIMR, val32);

error:
	usb_free_urb(urb);
	return ret;
}

static int rtl8xxxu_add_interface(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	int ret;
	u8 val8;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		if (!priv->vif)
			priv->vif = vif;
		else
			return -EOPNOTSUPP;
		rtl8xxxu_stop_tx_beacon(priv);

		val8 = rtl8xxxu_read8(priv, REG_BEACON_CTRL);
		val8 |= BEACON_ATIM | BEACON_FUNCTION_ENABLE |
			BEACON_DISABLE_TSF_UPDATE;
		rtl8xxxu_write8(priv, REG_BEACON_CTRL, val8);
		ret = 0;
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	rtl8xxxu_set_linktype(priv, vif->type);

	return ret;
}

static void rtl8xxxu_remove_interface(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif)
{
	struct rtl8xxxu_priv *priv = hw->priv;

	dev_dbg(&priv->udev->dev, "%s\n", __func__);

	if (priv->vif)
		priv->vif = NULL;
}

static int rtl8xxxu_config(struct ieee80211_hw *hw, u32 changed)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	struct device *dev = &priv->udev->dev;
	u16 val16;
	int ret = 0, channel;
	bool ht40;

	if (rtl8xxxu_debug & RTL8XXXU_DEBUG_CHANNEL)
		dev_info(dev,
			 "%s: channel: %i (changed %08x chandef.width %02x)\n",
			 __func__, hw->conf.chandef.chan->hw_value,
			 changed, hw->conf.chandef.width);

	if (changed & IEEE80211_CONF_CHANGE_RETRY_LIMITS) {
		val16 = ((hw->conf.long_frame_max_tx_count <<
			  RETRY_LIMIT_LONG_SHIFT) & RETRY_LIMIT_LONG_MASK) |
			((hw->conf.short_frame_max_tx_count <<
			  RETRY_LIMIT_SHORT_SHIFT) & RETRY_LIMIT_SHORT_MASK);
		rtl8xxxu_write16(priv, REG_RETRY_LIMIT, val16);
	}

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		switch (hw->conf.chandef.width) {
		case NL80211_CHAN_WIDTH_20_NOHT:
		case NL80211_CHAN_WIDTH_20:
			ht40 = false;
			break;
		case NL80211_CHAN_WIDTH_40:
			ht40 = true;
			break;
		default:
			ret = -ENOTSUPP;
			goto exit;
		}

		channel = hw->conf.chandef.chan->hw_value;

		priv->fops->set_tx_power(priv, channel, ht40);

		priv->fops->config_channel(hw);
	}

exit:
	return ret;
}

static int rtl8xxxu_conf_tx(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif, u16 queue,
			    const struct ieee80211_tx_queue_params *param)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	struct device *dev = &priv->udev->dev;
	u32 val32;
	u8 aifs, acm_ctrl, acm_bit;

	aifs = param->aifs;

	val32 = aifs |
		fls(param->cw_min) << EDCA_PARAM_ECW_MIN_SHIFT |
		fls(param->cw_max) << EDCA_PARAM_ECW_MAX_SHIFT |
		(u32)param->txop << EDCA_PARAM_TXOP_SHIFT;

	acm_ctrl = rtl8xxxu_read8(priv, REG_ACM_HW_CTRL);
	dev_dbg(dev,
		"%s: IEEE80211 queue %02x val %08x, acm %i, acm_ctrl %02x\n",
		__func__, queue, val32, param->acm, acm_ctrl);

	switch (queue) {
	case IEEE80211_AC_VO:
		acm_bit = ACM_HW_CTRL_VO;
		rtl8xxxu_write32(priv, REG_EDCA_VO_PARAM, val32);
		break;
	case IEEE80211_AC_VI:
		acm_bit = ACM_HW_CTRL_VI;
		rtl8xxxu_write32(priv, REG_EDCA_VI_PARAM, val32);
		break;
	case IEEE80211_AC_BE:
		acm_bit = ACM_HW_CTRL_BE;
		rtl8xxxu_write32(priv, REG_EDCA_BE_PARAM, val32);
		break;
	case IEEE80211_AC_BK:
		acm_bit = ACM_HW_CTRL_BK;
		rtl8xxxu_write32(priv, REG_EDCA_BK_PARAM, val32);
		break;
	default:
		acm_bit = 0;
		break;
	}

	if (param->acm)
		acm_ctrl |= acm_bit;
	else
		acm_ctrl &= ~acm_bit;
	rtl8xxxu_write8(priv, REG_ACM_HW_CTRL, acm_ctrl);

	return 0;
}

static void rtl8xxxu_configure_filter(struct ieee80211_hw *hw,
				      unsigned int changed_flags,
				      unsigned int *total_flags, u64 multicast)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	u32 rcr = rtl8xxxu_read32(priv, REG_RCR);

	dev_dbg(&priv->udev->dev, "%s: changed_flags %08x, total_flags %08x\n",
		__func__, changed_flags, *total_flags);

	/*
	 * FIF_ALLMULTI ignored as all multicast frames are accepted (REG_MAR)
	 */

	if (*total_flags & FIF_FCSFAIL)
		rcr |= RCR_ACCEPT_CRC32;
	else
		rcr &= ~RCR_ACCEPT_CRC32;

	/*
	 * FIF_PLCPFAIL not supported?
	 */

	if (*total_flags & FIF_BCN_PRBRESP_PROMISC)
		rcr &= ~RCR_CHECK_BSSID_BEACON;
	else
		rcr |= RCR_CHECK_BSSID_BEACON;

	if (*total_flags & FIF_CONTROL)
		rcr |= RCR_ACCEPT_CTRL_FRAME;
	else
		rcr &= ~RCR_ACCEPT_CTRL_FRAME;

	if (*total_flags & FIF_OTHER_BSS) {
		rcr |= RCR_ACCEPT_AP;
		rcr &= ~RCR_CHECK_BSSID_MATCH;
	} else {
		rcr &= ~RCR_ACCEPT_AP;
		rcr |= RCR_CHECK_BSSID_MATCH;
	}

	if (*total_flags & FIF_PSPOLL)
		rcr |= RCR_ACCEPT_PM;
	else
		rcr &= ~RCR_ACCEPT_PM;

	/*
	 * FIF_PROBE_REQ ignored as probe requests always seem to be accepted
	 */

	rtl8xxxu_write32(priv, REG_RCR, rcr);

	*total_flags &= (FIF_ALLMULTI | FIF_FCSFAIL | FIF_BCN_PRBRESP_PROMISC |
			 FIF_CONTROL | FIF_OTHER_BSS | FIF_PSPOLL |
			 FIF_PROBE_REQ);
}

static int rtl8xxxu_set_rts_threshold(struct ieee80211_hw *hw, u32 rts)
{
	if (rts > 2347)
		return -EINVAL;

	return 0;
}

static int rtl8xxxu_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			    struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta,
			    struct ieee80211_key_conf *key)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	struct device *dev = &priv->udev->dev;
	u8 mac_addr[ETH_ALEN];
	u8 val8;
	u16 val16;
	u32 val32;
	int retval = -EOPNOTSUPP;

	dev_dbg(dev, "%s: cmd %02x, cipher %08x, index %i\n",
		__func__, cmd, key->cipher, key->keyidx);

	if (vif->type != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;

	if (key->keyidx > 3)
		return -EOPNOTSUPP;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:

		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key->flags |= IEEE80211_KEY_FLAG_SW_MGMT_TX;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) {
		dev_dbg(dev, "%s: pairwise key\n", __func__);
		ether_addr_copy(mac_addr, sta->addr);
	} else {
		dev_dbg(dev, "%s: group key\n", __func__);
		eth_broadcast_addr(mac_addr);
	}

	val16 = rtl8xxxu_read16(priv, REG_CR);
	val16 |= CR_SECURITY_ENABLE;
	rtl8xxxu_write16(priv, REG_CR, val16);

	val8 = SEC_CFG_TX_SEC_ENABLE | SEC_CFG_TXBC_USE_DEFKEY |
		SEC_CFG_RX_SEC_ENABLE | SEC_CFG_RXBC_USE_DEFKEY;
	val8 |= SEC_CFG_TX_USE_DEFKEY | SEC_CFG_RX_USE_DEFKEY;
	rtl8xxxu_write8(priv, REG_SECURITY_CFG, val8);

	switch (cmd) {
	case SET_KEY:
		key->hw_key_idx = key->keyidx;
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
		rtl8xxxu_cam_write(priv, key, mac_addr);
		retval = 0;
		break;
	case DISABLE_KEY:
		rtl8xxxu_write32(priv, REG_CAM_WRITE, 0x00000000);
		val32 = CAM_CMD_POLLING | CAM_CMD_WRITE |
			key->keyidx << CAM_CMD_KEY_SHIFT;
		rtl8xxxu_write32(priv, REG_CAM_CMD, val32);
		retval = 0;
		break;
	default:
		dev_warn(dev, "%s: Unsupported command %02x\n", __func__, cmd);
	}

	return retval;
}

static int
rtl8xxxu_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct ieee80211_ampdu_params *params)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	struct device *dev = &priv->udev->dev;
	u8 ampdu_factor, ampdu_density;
	struct ieee80211_sta *sta = params->sta;
	u16 tid = params->tid;
	enum ieee80211_ampdu_mlme_action action = params->action;

	switch (action) {
	case IEEE80211_AMPDU_TX_START:
		dev_dbg(dev, "%s: IEEE80211_AMPDU_TX_START\n", __func__);
		ampdu_factor = sta->ht_cap.ampdu_factor;
		ampdu_density = sta->ht_cap.ampdu_density;
		rtl8xxxu_set_ampdu_factor(priv, ampdu_factor);
		rtl8xxxu_set_ampdu_min_space(priv, ampdu_density);
		dev_dbg(dev,
			"Changed HT: ampdu_factor %02x, ampdu_density %02x\n",
			ampdu_factor, ampdu_density);
		return IEEE80211_AMPDU_TX_START_IMMEDIATE;
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		dev_dbg(dev, "%s: IEEE80211_AMPDU_TX_STOP\n", __func__);
		rtl8xxxu_set_ampdu_factor(priv, 0);
		rtl8xxxu_set_ampdu_min_space(priv, 0);
		clear_bit(tid, priv->tx_aggr_started);
		clear_bit(tid, priv->tid_tx_operational);
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		dev_dbg(dev, "%s: IEEE80211_AMPDU_TX_OPERATIONAL\n", __func__);
		set_bit(tid, priv->tid_tx_operational);
		break;
	case IEEE80211_AMPDU_RX_START:
		dev_dbg(dev, "%s: IEEE80211_AMPDU_RX_START\n", __func__);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		dev_dbg(dev, "%s: IEEE80211_AMPDU_RX_STOP\n", __func__);
		break;
	default:
		break;
	}
	return 0;
}

static void
rtl8xxxu_sta_statistics(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, struct station_info *sinfo)
{
	struct rtl8xxxu_priv *priv = hw->priv;

	sinfo->txrate = priv->ra_report.txrate;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
}

static u8 rtl8xxxu_signal_to_snr(int signal)
{
	if (signal < RTL8XXXU_NOISE_FLOOR_MIN)
		signal = RTL8XXXU_NOISE_FLOOR_MIN;
	else if (signal > 0)
		signal = 0;
	return (u8)(signal - RTL8XXXU_NOISE_FLOOR_MIN);
}

static void rtl8xxxu_refresh_rate_mask(struct rtl8xxxu_priv *priv,
				       int signal, struct ieee80211_sta *sta)
{
	struct ieee80211_hw *hw = priv->hw;
	u16 wireless_mode;
	u8 rssi_level, ratr_idx;
	u8 txbw_40mhz;
	u8 snr, snr_thresh_high, snr_thresh_low;
	u8 go_up_gap = 5;

	rssi_level = priv->rssi_level;
	snr = rtl8xxxu_signal_to_snr(signal);
	snr_thresh_high = RTL8XXXU_SNR_THRESH_HIGH;
	snr_thresh_low = RTL8XXXU_SNR_THRESH_LOW;
	txbw_40mhz = (hw->conf.chandef.width == NL80211_CHAN_WIDTH_40) ? 1 : 0;

	switch (rssi_level) {
	case RTL8XXXU_RATR_STA_MID:
		snr_thresh_high += go_up_gap;
		break;
	case RTL8XXXU_RATR_STA_LOW:
		snr_thresh_high += go_up_gap;
		snr_thresh_low += go_up_gap;
		break;
	default:
		break;
	}

	if (snr > snr_thresh_high)
		rssi_level = RTL8XXXU_RATR_STA_HIGH;
	else if (snr > snr_thresh_low)
		rssi_level = RTL8XXXU_RATR_STA_MID;
	else
		rssi_level = RTL8XXXU_RATR_STA_LOW;

	if (rssi_level != priv->rssi_level) {
		int sgi = 0;
		u32 rate_bitmap = 0;

		rcu_read_lock();
		rate_bitmap = (sta->supp_rates[0] & 0xfff) |
				(sta->ht_cap.mcs.rx_mask[0] << 12) |
				(sta->ht_cap.mcs.rx_mask[1] << 20);
		if (sta->ht_cap.cap &
		    (IEEE80211_HT_CAP_SGI_40 | IEEE80211_HT_CAP_SGI_20))
			sgi = 1;
		rcu_read_unlock();

		wireless_mode = rtl8xxxu_wireless_mode(hw, sta);
		switch (wireless_mode) {
		case WIRELESS_MODE_B:
			ratr_idx = RATEID_IDX_B;
			if (rate_bitmap & 0x0000000c)
				rate_bitmap &= 0x0000000d;
			else
				rate_bitmap &= 0x0000000f;
			break;
		case WIRELESS_MODE_A:
		case WIRELESS_MODE_G:
			ratr_idx = RATEID_IDX_G;
			if (rssi_level == RTL8XXXU_RATR_STA_HIGH)
				rate_bitmap &= 0x00000f00;
			else
				rate_bitmap &= 0x00000ff0;
			break;
		case (WIRELESS_MODE_B | WIRELESS_MODE_G):
			ratr_idx = RATEID_IDX_BG;
			if (rssi_level == RTL8XXXU_RATR_STA_HIGH)
				rate_bitmap &= 0x00000f00;
			else if (rssi_level == RTL8XXXU_RATR_STA_MID)
				rate_bitmap &= 0x00000ff0;
			else
				rate_bitmap &= 0x00000ff5;
			break;
		case WIRELESS_MODE_N_24G:
		case WIRELESS_MODE_N_5G:
		case (WIRELESS_MODE_G | WIRELESS_MODE_N_24G):
		case (WIRELESS_MODE_A | WIRELESS_MODE_N_5G):
			if (priv->tx_paths == 2 && priv->rx_paths == 2)
				ratr_idx = RATEID_IDX_GN_N2SS;
			else
				ratr_idx = RATEID_IDX_GN_N1SS;
			break;
		case (WIRELESS_MODE_B | WIRELESS_MODE_G | WIRELESS_MODE_N_24G):
		case (WIRELESS_MODE_B | WIRELESS_MODE_N_24G):
			if (txbw_40mhz) {
				if (priv->tx_paths == 2 && priv->rx_paths == 2)
					ratr_idx = RATEID_IDX_BGN_40M_2SS;
				else
					ratr_idx = RATEID_IDX_BGN_40M_1SS;
			} else {
				if (priv->tx_paths == 2 && priv->rx_paths == 2)
					ratr_idx = RATEID_IDX_BGN_20M_2SS_BN;
				else
					ratr_idx = RATEID_IDX_BGN_20M_1SS_BN;
			}

			if (priv->tx_paths == 2 && priv->rx_paths == 2) {
				if (rssi_level == RTL8XXXU_RATR_STA_HIGH) {
					rate_bitmap &= 0x0f8f0000;
				} else if (rssi_level == RTL8XXXU_RATR_STA_MID) {
					rate_bitmap &= 0x0f8ff000;
				} else {
					if (txbw_40mhz)
						rate_bitmap &= 0x0f8ff015;
					else
						rate_bitmap &= 0x0f8ff005;
				}
			} else {
				if (rssi_level == RTL8XXXU_RATR_STA_HIGH) {
					rate_bitmap &= 0x000f0000;
				} else if (rssi_level == RTL8XXXU_RATR_STA_MID) {
					rate_bitmap &= 0x000ff000;
				} else {
					if (txbw_40mhz)
						rate_bitmap &= 0x000ff015;
					else
						rate_bitmap &= 0x000ff005;
				}
			}
			break;
		default:
			ratr_idx = RATEID_IDX_BGN_40M_2SS;
			rate_bitmap &= 0x0fffffff;
			break;
		}

		priv->rssi_level = rssi_level;
		priv->fops->update_rate_mask(priv, rate_bitmap, ratr_idx, sgi);
	}
}

static void rtl8xxxu_watchdog_callback(struct work_struct *work)
{
	struct ieee80211_vif *vif;
	struct rtl8xxxu_priv *priv;

	priv = container_of(work, struct rtl8xxxu_priv, ra_watchdog.work);
	vif = priv->vif;

	if (vif && vif->type == NL80211_IFTYPE_STATION) {
		int signal;
		struct ieee80211_sta *sta;

		rcu_read_lock();
		sta = ieee80211_find_sta(vif, vif->bss_conf.bssid);
		if (!sta) {
			struct device *dev = &priv->udev->dev;

			dev_dbg(dev, "%s: no sta found\n", __func__);
			rcu_read_unlock();
			goto out;
		}
		rcu_read_unlock();

		signal = ieee80211_ave_rssi(vif);
		rtl8xxxu_refresh_rate_mask(priv, signal, sta);
	}

out:
	schedule_delayed_work(&priv->ra_watchdog, 2 * HZ);
}

static int rtl8xxxu_start(struct ieee80211_hw *hw)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	struct rtl8xxxu_rx_urb *rx_urb;
	struct rtl8xxxu_tx_urb *tx_urb;
	struct sk_buff *skb;
	unsigned long flags;
	int ret, i;

	ret = 0;

	init_usb_anchor(&priv->rx_anchor);
	init_usb_anchor(&priv->tx_anchor);
	init_usb_anchor(&priv->int_anchor);

	priv->fops->enable_rf(priv);
	if (priv->usb_interrupts) {
		ret = rtl8xxxu_submit_int_urb(hw);
		if (ret)
			goto exit;
	}

	for (i = 0; i < RTL8XXXU_TX_URBS; i++) {
		tx_urb = kmalloc(sizeof(struct rtl8xxxu_tx_urb), GFP_KERNEL);
		if (!tx_urb) {
			if (!i)
				ret = -ENOMEM;

			goto error_out;
		}
		usb_init_urb(&tx_urb->urb);
		INIT_LIST_HEAD(&tx_urb->list);
		tx_urb->hw = hw;
		list_add(&tx_urb->list, &priv->tx_urb_free_list);
		priv->tx_urb_free_count++;
	}

	priv->tx_stopped = false;

	spin_lock_irqsave(&priv->rx_urb_lock, flags);
	priv->shutdown = false;
	spin_unlock_irqrestore(&priv->rx_urb_lock, flags);

	for (i = 0; i < RTL8XXXU_RX_URBS; i++) {
		rx_urb = kmalloc(sizeof(struct rtl8xxxu_rx_urb), GFP_KERNEL);
		if (!rx_urb) {
			if (!i)
				ret = -ENOMEM;

			goto error_out;
		}
		usb_init_urb(&rx_urb->urb);
		INIT_LIST_HEAD(&rx_urb->list);
		rx_urb->hw = hw;

		ret = rtl8xxxu_submit_rx_urb(priv, rx_urb);
		if (ret) {
			if (ret != -ENOMEM) {
				skb = (struct sk_buff *)rx_urb->urb.context;
				dev_kfree_skb(skb);
			}
			rtl8xxxu_queue_rx_urb(priv, rx_urb);
		}
	}

	schedule_delayed_work(&priv->ra_watchdog, 2 * HZ);
exit:
	/*
	 * Accept all data and mgmt frames
	 */
	rtl8xxxu_write16(priv, REG_RXFLTMAP2, 0xffff);
	rtl8xxxu_write16(priv, REG_RXFLTMAP0, 0xffff);

	rtl8xxxu_write32(priv, REG_OFDM0_XA_AGC_CORE1, 0x6954341e);

	return ret;

error_out:
	rtl8xxxu_free_tx_resources(priv);
	/*
	 * Disable all data and mgmt frames
	 */
	rtl8xxxu_write16(priv, REG_RXFLTMAP2, 0x0000);
	rtl8xxxu_write16(priv, REG_RXFLTMAP0, 0x0000);

	return ret;
}

static void rtl8xxxu_stop(struct ieee80211_hw *hw)
{
	struct rtl8xxxu_priv *priv = hw->priv;
	unsigned long flags;

	rtl8xxxu_write8(priv, REG_TXPAUSE, 0xff);

	rtl8xxxu_write16(priv, REG_RXFLTMAP0, 0x0000);
	rtl8xxxu_write16(priv, REG_RXFLTMAP2, 0x0000);

	spin_lock_irqsave(&priv->rx_urb_lock, flags);
	priv->shutdown = true;
	spin_unlock_irqrestore(&priv->rx_urb_lock, flags);

	usb_kill_anchored_urbs(&priv->rx_anchor);
	usb_kill_anchored_urbs(&priv->tx_anchor);
	if (priv->usb_interrupts)
		usb_kill_anchored_urbs(&priv->int_anchor);

	rtl8xxxu_write8(priv, REG_TXPAUSE, 0xff);

	priv->fops->disable_rf(priv);

	/*
	 * Disable interrupts
	 */
	if (priv->usb_interrupts)
		rtl8xxxu_write32(priv, REG_USB_HIMR, 0);

	cancel_delayed_work_sync(&priv->ra_watchdog);

	rtl8xxxu_free_rx_resources(priv);
	rtl8xxxu_free_tx_resources(priv);
}

static const struct ieee80211_ops rtl8xxxu_ops = {
	.tx = rtl8xxxu_tx,
	.add_interface = rtl8xxxu_add_interface,
	.remove_interface = rtl8xxxu_remove_interface,
	.config = rtl8xxxu_config,
	.conf_tx = rtl8xxxu_conf_tx,
	.bss_info_changed = rtl8xxxu_bss_info_changed,
	.configure_filter = rtl8xxxu_configure_filter,
	.set_rts_threshold = rtl8xxxu_set_rts_threshold,
	.start = rtl8xxxu_start,
	.stop = rtl8xxxu_stop,
	.sw_scan_start = rtl8xxxu_sw_scan_start,
	.sw_scan_complete = rtl8xxxu_sw_scan_complete,
	.set_key = rtl8xxxu_set_key,
	.ampdu_action = rtl8xxxu_ampdu_action,
	.sta_statistics = rtl8xxxu_sta_statistics,
};

static int rtl8xxxu_parse_usb(struct rtl8xxxu_priv *priv,
			      struct usb_interface *interface)
{
	struct usb_interface_descriptor *interface_desc;
	struct usb_host_interface *host_interface;
	struct usb_endpoint_descriptor *endpoint;
	struct device *dev = &priv->udev->dev;
	int i, j = 0, endpoints;
	u8 dir, xtype, num;
	int ret = 0;

	host_interface = interface->cur_altsetting;
	interface_desc = &host_interface->desc;
	endpoints = interface_desc->bNumEndpoints;

	for (i = 0; i < endpoints; i++) {
		endpoint = &host_interface->endpoint[i].desc;

		dir = endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK;
		num = usb_endpoint_num(endpoint);
		xtype = usb_endpoint_type(endpoint);
		if (rtl8xxxu_debug & RTL8XXXU_DEBUG_USB)
			dev_dbg(dev,
				"%s: endpoint: dir %02x, # %02x, type %02x\n",
				__func__, dir, num, xtype);
		if (usb_endpoint_dir_in(endpoint) &&
		    usb_endpoint_xfer_bulk(endpoint)) {
			if (rtl8xxxu_debug & RTL8XXXU_DEBUG_USB)
				dev_dbg(dev, "%s: in endpoint num %i\n",
					__func__, num);

			if (priv->pipe_in) {
				dev_warn(dev,
					 "%s: Too many IN pipes\n", __func__);
				ret = -EINVAL;
				goto exit;
			}

			priv->pipe_in =	usb_rcvbulkpipe(priv->udev, num);
		}

		if (usb_endpoint_dir_in(endpoint) &&
		    usb_endpoint_xfer_int(endpoint)) {
			if (rtl8xxxu_debug & RTL8XXXU_DEBUG_USB)
				dev_dbg(dev, "%s: interrupt endpoint num %i\n",
					__func__, num);

			if (priv->pipe_interrupt) {
				dev_warn(dev, "%s: Too many INTERRUPT pipes\n",
					 __func__);
				ret = -EINVAL;
				goto exit;
			}

			priv->pipe_interrupt = usb_rcvintpipe(priv->udev, num);
		}

		if (usb_endpoint_dir_out(endpoint) &&
		    usb_endpoint_xfer_bulk(endpoint)) {
			if (rtl8xxxu_debug & RTL8XXXU_DEBUG_USB)
				dev_dbg(dev, "%s: out endpoint num %i\n",
					__func__, num);
			if (j >= RTL8XXXU_OUT_ENDPOINTS) {
				dev_warn(dev,
					 "%s: Too many OUT pipes\n", __func__);
				ret = -EINVAL;
				goto exit;
			}
			priv->out_ep[j++] = num;
		}
	}
exit:
	priv->nr_out_eps = j;
	return ret;
}

static int rtl8xxxu_probe(struct usb_interface *interface,
			  const struct usb_device_id *id)
{
	struct rtl8xxxu_priv *priv;
	struct ieee80211_hw *hw;
	struct usb_device *udev;
	struct ieee80211_supported_band *sband;
	int ret;
	int untested = 1;

	udev = usb_get_dev(interface_to_usbdev(interface));

	switch (id->idVendor) {
	case USB_VENDOR_ID_REALTEK:
		switch(id->idProduct) {
		case 0x1724:
		case 0x8176:
		case 0x8178:
		case 0x817f:
		case 0x818b:
			untested = 0;
			break;
		}
		break;
	case 0x7392:
		if (id->idProduct == 0x7811 || id->idProduct == 0xa611)
			untested = 0;
		break;
	case 0x050d:
		if (id->idProduct == 0x1004)
			untested = 0;
		break;
	case 0x20f4:
		if (id->idProduct == 0x648b)
			untested = 0;
		break;
	case 0x2001:
		if (id->idProduct == 0x3308)
			untested = 0;
		break;
	case 0x2357:
		if (id->idProduct == 0x0109)
			untested = 0;
		break;
	default:
		break;
	}

	if (untested) {
		rtl8xxxu_debug |= RTL8XXXU_DEBUG_EFUSE;
		dev_info(&udev->dev,
			 "This Realtek USB WiFi dongle (0x%04x:0x%04x) is untested!\n",
			 id->idVendor, id->idProduct);
		dev_info(&udev->dev,
			 "Please report results to Jes.Sorensen@gmail.com\n");
	}

	hw = ieee80211_alloc_hw(sizeof(struct rtl8xxxu_priv), &rtl8xxxu_ops);
	if (!hw) {
		ret = -ENOMEM;
		priv = NULL;
		goto exit;
	}

	priv = hw->priv;
	priv->hw = hw;
	priv->udev = udev;
	priv->fops = (struct rtl8xxxu_fileops *)id->driver_info;
	mutex_init(&priv->usb_buf_mutex);
	mutex_init(&priv->h2c_mutex);
	INIT_LIST_HEAD(&priv->tx_urb_free_list);
	spin_lock_init(&priv->tx_urb_lock);
	INIT_LIST_HEAD(&priv->rx_urb_pending_list);
	spin_lock_init(&priv->rx_urb_lock);
	INIT_WORK(&priv->rx_urb_wq, rtl8xxxu_rx_urb_work);
	INIT_DELAYED_WORK(&priv->ra_watchdog, rtl8xxxu_watchdog_callback);
	INIT_WORK(&priv->c2hcmd_work, rtl8xxxu_c2hcmd_callback);
	skb_queue_head_init(&priv->c2hcmd_queue);

	usb_set_intfdata(interface, hw);

	ret = rtl8xxxu_parse_usb(priv, interface);
	if (ret)
		goto exit;

	ret = rtl8xxxu_identify_chip(priv);
	if (ret) {
		dev_err(&udev->dev, "Fatal - failed to identify chip\n");
		goto exit;
	}

	ret = rtl8xxxu_read_efuse(priv);
	if (ret) {
		dev_err(&udev->dev, "Fatal - failed to read EFuse\n");
		goto exit;
	}

	ret = priv->fops->parse_efuse(priv);
	if (ret) {
		dev_err(&udev->dev, "Fatal - failed to parse EFuse\n");
		goto exit;
	}

	rtl8xxxu_print_chipinfo(priv);

	ret = priv->fops->load_firmware(priv);
	if (ret) {
		dev_err(&udev->dev, "Fatal - failed to load firmware\n");
		goto exit;
	}

	ret = rtl8xxxu_init_device(hw);
	if (ret)
		goto exit;

	hw->wiphy->max_scan_ssids = 1;
	hw->wiphy->max_scan_ie_len = IEEE80211_MAX_DATA_LEN;
	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	hw->queues = 4;

	sband = &rtl8xxxu_supported_band;
	sband->ht_cap.ht_supported = true;
	sband->ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	sband->ht_cap.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16;
	sband->ht_cap.cap = IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40;
	memset(&sband->ht_cap.mcs, 0, sizeof(sband->ht_cap.mcs));
	sband->ht_cap.mcs.rx_mask[0] = 0xff;
	sband->ht_cap.mcs.rx_mask[4] = 0x01;
	if (priv->rf_paths > 1) {
		sband->ht_cap.mcs.rx_mask[1] = 0xff;
		sband->ht_cap.cap |= IEEE80211_HT_CAP_SGI_40;
	}
	sband->ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
	/*
	 * Some APs will negotiate HT20_40 in a noisy environment leading
	 * to miserable performance. Rather than defaulting to this, only
	 * enable it if explicitly requested at module load time.
	 */
	if (rtl8xxxu_ht40_2g) {
		dev_info(&udev->dev, "Enabling HT_20_40 on the 2.4GHz band\n");
		sband->ht_cap.cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
	}
	hw->wiphy->bands[NL80211_BAND_2GHZ] = sband;

	hw->wiphy->rts_threshold = 2347;

	SET_IEEE80211_DEV(priv->hw, &interface->dev);
	SET_IEEE80211_PERM_ADDR(hw, priv->mac_addr);

	hw->extra_tx_headroom = priv->fops->tx_desc_size;
	ieee80211_hw_set(hw, SIGNAL_DBM);
	/*
	 * The firmware handles rate control
	 */
	ieee80211_hw_set(hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);

	wiphy_ext_feature_set(hw->wiphy, NL80211_EXT_FEATURE_CQM_RSSI_LIST);

	ret = ieee80211_register_hw(priv->hw);
	if (ret) {
		dev_err(&udev->dev, "%s: Failed to register: %i\n",
			__func__, ret);
		goto exit;
	}

	return 0;

exit:
	usb_set_intfdata(interface, NULL);

	if (priv) {
		kfree(priv->fw_data);
		mutex_destroy(&priv->usb_buf_mutex);
		mutex_destroy(&priv->h2c_mutex);
	}
	usb_put_dev(udev);

	ieee80211_free_hw(hw);

	return ret;
}

static void rtl8xxxu_disconnect(struct usb_interface *interface)
{
	struct rtl8xxxu_priv *priv;
	struct ieee80211_hw *hw;

	hw = usb_get_intfdata(interface);
	priv = hw->priv;

	ieee80211_unregister_hw(hw);

	priv->fops->power_off(priv);

	usb_set_intfdata(interface, NULL);

	dev_info(&priv->udev->dev, "disconnecting\n");

	kfree(priv->fw_data);
	mutex_destroy(&priv->usb_buf_mutex);
	mutex_destroy(&priv->h2c_mutex);

	if (priv->udev->state != USB_STATE_NOTATTACHED) {
		dev_info(&priv->udev->dev,
			 "Device still attached, trying to reset\n");
		usb_reset_device(priv->udev);
	}
	usb_put_dev(priv->udev);
	ieee80211_free_hw(hw);
}

static const struct usb_device_id dev_table[] = {
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x8724, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8723au_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x1724, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8723au_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x0724, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8723au_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x818b, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192eu_fops},
/* TP-Link TL-WN822N v4 */
{USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x0108, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192eu_fops},
/* D-Link DWA-131 rev E1, tested by David Patiño */
{USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x3319, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192eu_fops},
/* Tested by Myckel Habets */
{USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x0109, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192eu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0xb720, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8723bu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0xa611, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8723bu_fops},
#ifdef CONFIG_RTL8XXXU_UNTESTED
/* Still supported by rtlwifi */
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x8176, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x8178, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x817f, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
/* Tested by Larry Finger */
{USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0x7811, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
/* Tested by Andrea Merello */
{USB_DEVICE_AND_INTERFACE_INFO(0x050d, 0x1004, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
/* Tested by Jocelyn Mayer */
{USB_DEVICE_AND_INTERFACE_INFO(0x20f4, 0x648b, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
/* Tested by Stefano Bravi */
{USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x3308, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
/* Currently untested 8188 series devices */
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x018a, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x8191, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x8170, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x8177, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x817a, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x817b, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x817d, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x817e, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x818a, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x317f, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x1058, 0x0631, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x04bb, 0x094c, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x050d, 0x1102, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x06f8, 0xe033, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x07b8, 0x8189, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x0846, 0x9041, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x0b05, 0x17ba, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x1e1e, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x5088, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x0df6, 0x0052, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x0df6, 0x005c, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x0eb0, 0x9071, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x103c, 0x1629, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x13d3, 0x3357, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x330b, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x2019, 0x4902, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x2019, 0xab2a, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x2019, 0xab2e, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x2019, 0xed17, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x4855, 0x0090, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x4856, 0x0091, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0xcdab, 0x8010, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x04f2, 0xaff7, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x04f2, 0xaff9, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x04f2, 0xaffa, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x04f2, 0xaff8, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x04f2, 0xaffb, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x04f2, 0xaffc, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x2019, 0x1201, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
/* Currently untested 8192 series devices */
{USB_DEVICE_AND_INTERFACE_INFO(0x04bb, 0x0950, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x050d, 0x2102, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x050d, 0x2103, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x0586, 0x341f, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x06f8, 0xe035, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x0b05, 0x17ab, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x0df6, 0x0061, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x0df6, 0x0070, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x0789, 0x016d, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x07aa, 0x0056, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x07b8, 0x8178, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x0846, 0x9021, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x0846, 0xf001, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x2e2e, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x0e66, 0x0019, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x0e66, 0x0020, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x3307, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x3309, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x330a, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x2019, 0xab2b, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x20f4, 0x624d, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x0100, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x4855, 0x0091, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0x7822, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192cu_fops},
/* found in rtl8192eu vendor driver */
{USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x0107, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192eu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(0x2019, 0xab33, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192eu_fops},
{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_REALTEK, 0x818c, 0xff, 0xff, 0xff),
	.driver_info = (unsigned long)&rtl8192eu_fops},
#endif
{ }
};

static struct usb_driver rtl8xxxu_driver = {
	.name = DRIVER_NAME,
	.probe = rtl8xxxu_probe,
	.disconnect = rtl8xxxu_disconnect,
	.id_table = dev_table,
	.no_dynamic_id = 1,
	.disable_hub_initiated_lpm = 1,
};

static int __init rtl8xxxu_module_init(void)
{
	int res;

	res = usb_register(&rtl8xxxu_driver);
	if (res < 0)
		pr_err(DRIVER_NAME ": usb_register() failed (%i)\n", res);

	return res;
}

static void __exit rtl8xxxu_module_exit(void)
{
	usb_deregister(&rtl8xxxu_driver);
}


MODULE_DEVICE_TABLE(usb, dev_table);

module_init(rtl8xxxu_module_init);
module_exit(rtl8xxxu_module_exit);
