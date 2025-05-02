// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright Fiona Klute <fiona.klute@gmx.de> */

#include <linux/of_net.h>
#include "main.h"
#include "coex.h"
#include "debug.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rx.h"
#include "rtw8703b.h"
#include "rtw8703b_tables.h"
#include "rtw8723x.h"

#define BIT_MASK_TXQ_INIT (BIT(7))
#define WLAN_RL_VAL 0x3030
/* disable BAR */
#define WLAN_BAR_VAL 0x0201ffff
#define WLAN_PIFS_VAL 0
#define WLAN_RX_PKT_LIMIT 0x18
#define WLAN_SLOT_TIME 0x09
#define WLAN_SPEC_SIFS 0x100a
#define WLAN_MAX_AGG_NR 0x1f
#define WLAN_AMPDU_MAX_TIME 0x70

/* unit is 32us */
#define TBTT_PROHIBIT_SETUP_TIME 0x04
#define TBTT_PROHIBIT_HOLD_TIME 0x80
#define TBTT_PROHIBIT_HOLD_TIME_STOP_BCN 0x64

#define TRANS_SEQ_END			\
	0xFFFF,				\
	RTW_PWR_CUT_ALL_MSK,		\
	RTW_PWR_INTF_ALL_MSK,		\
	0,				\
	RTW_PWR_CMD_END, 0, 0

/* rssi in percentage % (dbm = % - 100) */
/* These are used to select simple signal quality levels, might need
 * tweaking. Same for rf_para tables below.
 */
static const u8 wl_rssi_step_8703b[] = {60, 50, 44, 30};
static const u8 bt_rssi_step_8703b[] = {30, 30, 30, 30};
static const struct coex_5g_afh_map afh_5g_8703b[] = { {0, 0, 0} };

/* Actually decreasing wifi TX power/RX gain isn't implemented in
 * rtw8703b, but hopefully adjusting the BT side helps.
 */
static const struct coex_rf_para rf_para_tx_8703b[] = {
	{0, 0, false, 7},  /* for normal */
	{0, 10, false, 7}, /* for WL-CPT */
	{1, 0, true, 4},
	{1, 2, true, 4},
	{1, 10, true, 4},
	{1, 15, true, 4}
};

static const struct coex_rf_para rf_para_rx_8703b[] = {
	{0, 0, false, 7},  /* for normal */
	{0, 10, false, 7}, /* for WL-CPT */
	{1, 0, true, 5},
	{1, 2, true, 5},
	{1, 10, true, 5},
	{1, 15, true, 5}
};

static const u32 rtw8703b_ofdm_swing_table[] = {
	0x0b40002d, /* 0,  -15.0dB */
	0x0c000030, /* 1,  -14.5dB */
	0x0cc00033, /* 2,  -14.0dB */
	0x0d800036, /* 3,  -13.5dB */
	0x0e400039, /* 4,  -13.0dB */
	0x0f00003c, /* 5,  -12.5dB */
	0x10000040, /* 6,  -12.0dB */
	0x11000044, /* 7,  -11.5dB */
	0x12000048, /* 8,  -11.0dB */
	0x1300004c, /* 9,  -10.5dB */
	0x14400051, /* 10, -10.0dB */
	0x15800056, /* 11, -9.5dB */
	0x16c0005b, /* 12, -9.0dB */
	0x18000060, /* 13, -8.5dB */
	0x19800066, /* 14, -8.0dB */
	0x1b00006c, /* 15, -7.5dB */
	0x1c800072, /* 16, -7.0dB */
	0x1e400079, /* 17, -6.5dB */
	0x20000080, /* 18, -6.0dB */
	0x22000088, /* 19, -5.5dB */
	0x24000090, /* 20, -5.0dB */
	0x26000098, /* 21, -4.5dB */
	0x288000a2, /* 22, -4.0dB */
	0x2ac000ab, /* 23, -3.5dB */
	0x2d4000b5, /* 24, -3.0dB */
	0x300000c0, /* 25, -2.5dB */
	0x32c000cb, /* 26, -2.0dB */
	0x35c000d7, /* 27, -1.5dB */
	0x390000e4, /* 28, -1.0dB */
	0x3c8000f2, /* 29, -0.5dB */
	0x40000100, /* 30, +0dB */
	0x43c0010f, /* 31, +0.5dB */
	0x47c0011f, /* 32, +1.0dB */
	0x4c000130, /* 33, +1.5dB */
	0x50800142, /* 34, +2.0dB */
	0x55400155, /* 35, +2.5dB */
	0x5a400169, /* 36, +3.0dB */
	0x5fc0017f, /* 37, +3.5dB */
	0x65400195, /* 38, +4.0dB */
	0x6b8001ae, /* 39, +4.5dB */
	0x71c001c7, /* 40, +5.0dB */
	0x788001e2, /* 41, +5.5dB */
	0x7f8001fe /* 42, +6.0dB */
};

static const u32 rtw8703b_cck_pwr_regs[] = {
	0x0a22, 0x0a23, 0x0a24, 0x0a25, 0x0a26, 0x0a27, 0x0a28, 0x0a29,
	0x0a9a, 0x0a9b, 0x0a9c, 0x0a9d, 0x0aa0, 0x0aa1, 0x0aa2, 0x0aa3,
};

static const u8 rtw8703b_cck_swing_table[][16] = {
	{0x44, 0x42, 0x3C, 0x33, 0x28, 0x1C, 0x13, 0x0B, 0x05, 0x02,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-16dB*/
	{0x48, 0x46, 0x3F, 0x36, 0x2A, 0x1E, 0x14, 0x0B, 0x05, 0x02,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-15.5dB*/
	{0x4D, 0x4A, 0x43, 0x39, 0x2C, 0x20, 0x15, 0x0C, 0x06, 0x02,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-15dB*/
	{0x51, 0x4F, 0x47, 0x3C, 0x2F, 0x22, 0x16, 0x0D, 0x06, 0x02,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-14.5dB*/
	{0x56, 0x53, 0x4B, 0x40, 0x32, 0x24, 0x17, 0x0E, 0x06, 0x02,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-14dB*/
	{0x5B, 0x58, 0x50, 0x43, 0x35, 0x26, 0x19, 0x0E, 0x07, 0x02,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-13.5dB*/
	{0x60, 0x5D, 0x54, 0x47, 0x38, 0x28, 0x1A, 0x0F, 0x07, 0x02,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-13dB*/
	{0x66, 0x63, 0x59, 0x4C, 0x3B, 0x2B, 0x1C, 0x10, 0x08, 0x02,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-12.5dB*/
	{0x6C, 0x69, 0x5F, 0x50, 0x3F, 0x2D, 0x1E, 0x11, 0x08, 0x03,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-12dB*/
	{0x73, 0x6F, 0x64, 0x55, 0x42, 0x30, 0x1F, 0x12, 0x08, 0x03,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-11.5dB*/
	{0x79, 0x76, 0x6A, 0x5A, 0x46, 0x33, 0x21, 0x13, 0x09, 0x03,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-11dB*/
	{0x81, 0x7C, 0x71, 0x5F, 0x4A, 0x36, 0x23, 0x14, 0x0A, 0x03,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-10.5dB*/
	{0x88, 0x84, 0x77, 0x65, 0x4F, 0x39, 0x25, 0x15, 0x0A, 0x03,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-10dB*/
	{0x90, 0x8C, 0x7E, 0x6B, 0x54, 0x3C, 0x27, 0x17, 0x0B, 0x03,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-9.5dB*/
	{0x99, 0x94, 0x86, 0x71, 0x58, 0x40, 0x2A, 0x18, 0x0B, 0x04,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-9dB*/
	{0xA2, 0x9D, 0x8E, 0x78, 0x5E, 0x43, 0x2C, 0x19, 0x0C, 0x04,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-8.5dB*/
	{0xAC, 0xA6, 0x96, 0x7F, 0x63, 0x47, 0x2F, 0x1B, 0x0D, 0x04,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-8dB*/
	{0xB6, 0xB0, 0x9F, 0x87, 0x69, 0x4C, 0x32, 0x1D, 0x0D, 0x04,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-7.5dB*/
	{0xC1, 0xBA, 0xA8, 0x8F, 0x6F, 0x50, 0x35, 0x1E, 0x0E, 0x04,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-7dB*/
	{0xCC, 0xC5, 0xB2, 0x97, 0x76, 0x55, 0x38, 0x20, 0x0F, 0x05,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*-6.5dB*/
	{0xD8, 0xD1, 0xBD, 0xA0, 0x7D, 0x5A, 0x3B, 0x22, 0x10, 0x05,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} /*-6dB*/
};

#define RTW_OFDM_SWING_TABLE_SIZE	ARRAY_SIZE(rtw8703b_ofdm_swing_table)
#define RTW_CCK_SWING_TABLE_SIZE	ARRAY_SIZE(rtw8703b_cck_swing_table)

static const struct rtw_pwr_seq_cmd trans_pre_enable_8703b[] = {
	/* set up external crystal (XTAL) */
	{REG_PAD_CTRL1 + 2,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7), BIT(7)},
	/* set CLK_REQ to high active */
	{0x0069,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	/* unlock ISO/CLK/power control register */
	{REG_RSV_CTRL,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xff, 0},
	{TRANS_SEQ_END},
};

static const struct rtw_pwr_seq_cmd trans_carddis_to_cardemu_8703b[] = {
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7), 0},
	{TRANS_SEQ_END},
};

static const struct rtw_pwr_seq_cmd trans_cardemu_to_carddis_8703b[] = {
	{0x0023,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), BIT(4)},
	{0x0007,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK | RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x20},
	{0x0006,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7), BIT(7)},
	{TRANS_SEQ_END},
};

static const struct rtw_pwr_seq_cmd trans_cardemu_to_act_8703b[] = {
	{0x0020,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), 0},
	{0x0001,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_DELAY, 1, RTW_PWR_DELAY_MS},
	{0x0000,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(4) | BIT(3) | BIT(2)), 0},
	{0x0075,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0004,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), BIT(3)},
	{0x0004,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), 0},
	/* wait for power ready */
	{0x0006,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(1), BIT(1)},
	{0x0075,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0006,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(4) | BIT(3)), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(0), 0},
	{0x0010,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(6), BIT(6)},
	{0x0049,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0063,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0062,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0058,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x005A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0068,
	 RTW_PWR_CUT_TEST_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), BIT(3)},
	{0x0069,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(6), BIT(6)},
	{TRANS_SEQ_END},
};

static const struct rtw_pwr_seq_cmd trans_act_to_cardemu_8703b[] = {
	{0x001f,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xff, 0},
	{0x0049,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0006,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(1), 0},
	{0x0010,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(6), 0},
	{0x0000,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0x0020,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{TRANS_SEQ_END},
};

static const struct rtw_pwr_seq_cmd trans_act_to_reset_mcu_8703b[] = {
	{REG_SYS_FUNC_EN + 1,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT_FEN_CPUEN, 0},
	/* reset MCU ready */
	{REG_MCUFW_CTRL,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xff, 0},
	/* reset MCU IO wrapper */
	{REG_RSV_CTRL + 1,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{REG_RSV_CTRL + 1,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 1},
	{TRANS_SEQ_END},
};

static const struct rtw_pwr_seq_cmd trans_act_to_lps_8703b[] = {
	{0x0301,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xff, 0xff},
	{0x0522,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xff, 0xff},
	{0x05f8,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xff, 0},
	{0x05f9,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xff, 0},
	{0x05fa,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xff, 0},
	{0x05fb,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xff, 0},
	{0x0002,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0002,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_DELAY, 0, RTW_PWR_DELAY_US},
	{0x0002,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0100,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xff, 0x03},
	{0x0101,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0093,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xff, 0},
	{0x0553,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{TRANS_SEQ_END},
};

static const struct rtw_pwr_seq_cmd * const card_enable_flow_8703b[] = {
	trans_pre_enable_8703b,
	trans_carddis_to_cardemu_8703b,
	trans_cardemu_to_act_8703b,
	NULL
};

static const struct rtw_pwr_seq_cmd * const card_disable_flow_8703b[] = {
	trans_act_to_lps_8703b,
	trans_act_to_reset_mcu_8703b,
	trans_act_to_cardemu_8703b,
	trans_cardemu_to_carddis_8703b,
	NULL
};

static const struct rtw_page_table page_table_8703b[] = {
	{12, 2, 2, 0, 1},
	{12, 2, 2, 0, 1},
	{12, 2, 2, 0, 1},
	{12, 2, 2, 0, 1},
	{12, 2, 2, 0, 1},
};

static const struct rtw_rqpn rqpn_table_8703b[] = {
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH},
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH},
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_HIGH,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH},
};

static void try_mac_from_devicetree(struct rtw_dev *rtwdev)
{
	struct device_node *node = rtwdev->dev->of_node;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	int ret;

	if (node) {
		ret = of_get_mac_address(node, efuse->addr);
		if (ret == 0) {
			rtw_dbg(rtwdev, RTW_DBG_EFUSE,
				"got wifi mac address from DT: %pM\n",
				efuse->addr);
		}
	}
}

static int rtw8703b_read_efuse(struct rtw_dev *rtwdev, u8 *log_map)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	int ret;

	ret = rtw8723x_read_efuse(rtwdev, log_map);
	if (ret != 0)
		return ret;

	if (!is_valid_ether_addr(efuse->addr))
		try_mac_from_devicetree(rtwdev);

	return 0;
}

static void rtw8703b_pwrtrack_init(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 path;

	/* TODO: The vendor driver selects these using tables in
	 * halrf_powertracking_ce.c, functions are called
	 * get_swing_index and get_cck_swing_index. There the current
	 * fixed values are only the defaults in case no match is
	 * found.
	 */
	dm_info->default_ofdm_index = 30;
	dm_info->default_cck_index = 20;

	for (path = RF_PATH_A; path < rtwdev->hal.rf_path_num; path++) {
		ewma_thermal_init(&dm_info->avg_thermal[path]);
		dm_info->delta_power_index[path] = 0;
	}
	dm_info->pwr_trk_triggered = false;
	dm_info->pwr_trk_init_trigger = true;
	dm_info->thermal_meter_k = rtwdev->efuse.thermal_meter_k;
	dm_info->txagc_remnant_cck = 0;
	dm_info->txagc_remnant_ofdm[RF_PATH_A] = 0;
}

static void rtw8703b_phy_set_param(struct rtw_dev *rtwdev)
{
	u8 xtal_cap = rtwdev->efuse.crystal_cap & 0x3F;

	/* power on BB/RF domain */
	rtw_write16_set(rtwdev, REG_SYS_FUNC_EN,
			BIT_FEN_EN_25_1 | BIT_FEN_BB_GLB_RST | BIT_FEN_BB_RSTB);
	rtw_write8_set(rtwdev, REG_RF_CTRL,
		       BIT_RF_EN | BIT_RF_RSTB | BIT_RF_SDM_RSTB);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_WLINT, RFREG_MASK, 0x0780);
	rtw_write8(rtwdev, REG_AFE_CTRL1 + 1, 0x80);

	rtw_phy_load_tables(rtwdev);

	rtw_write32_clr(rtwdev, REG_RCR, BIT_RCR_ADF);
	/* 0xff is from vendor driver, rtw8723d uses
	 * BIT_HIQ_NO_LMT_EN_ROOT.  Comment in vendor driver: "Packet
	 * in Hi Queue Tx immediately". I wonder if setting all bits
	 * is really necessary.
	 */
	rtw_write8_set(rtwdev, REG_HIQ_NO_LMT_EN, 0xff);
	rtw_write16_set(rtwdev, REG_AFE_CTRL_4, BIT_CK320M_AFE_EN | BIT_EN_SYN);

	rtw_write32_mask(rtwdev, REG_AFE_CTRL3, BIT_MASK_XTAL,
			 xtal_cap | (xtal_cap << 6));
	rtw_write32_set(rtwdev, REG_FPGA0_RFMOD, BIT_CCKEN | BIT_OFDMEN);

	/* Init EDCA */
	rtw_write16(rtwdev, REG_SPEC_SIFS, WLAN_SPEC_SIFS);
	rtw_write16(rtwdev, REG_MAC_SPEC_SIFS, WLAN_SPEC_SIFS);
	rtw_write16(rtwdev, REG_SIFS, WLAN_SPEC_SIFS); /* CCK */
	rtw_write16(rtwdev, REG_SIFS + 2, WLAN_SPEC_SIFS); /* OFDM */
	/* TXOP */
	rtw_write32(rtwdev, REG_EDCA_VO_PARAM, 0x002FA226);
	rtw_write32(rtwdev, REG_EDCA_VI_PARAM, 0x005EA324);
	rtw_write32(rtwdev, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtw_write32(rtwdev, REG_EDCA_BK_PARAM, 0x0000A44F);

	/* Init retry */
	rtw_write8(rtwdev, REG_ACKTO, 0x40);

	/* Set up RX aggregation. sdio.c also sets DMA mode, but not
	 * the burst parameters.
	 */
	rtw_write8(rtwdev, REG_RXDMA_MODE,
		   BIT_DMA_MODE |
		   FIELD_PREP_CONST(BIT_MASK_AGG_BURST_NUM, AGG_BURST_NUM) |
		   FIELD_PREP_CONST(BIT_MASK_AGG_BURST_SIZE, AGG_BURST_SIZE));

	/* Init beacon parameters */
	rtw_write8(rtwdev, REG_BCN_CTRL,
		   BIT_DIS_TSF_UDT | BIT_EN_BCN_FUNCTION | BIT_EN_TXBCN_RPT);
	rtw_write8(rtwdev, REG_TBTT_PROHIBIT, TBTT_PROHIBIT_SETUP_TIME);
	rtw_write8(rtwdev, REG_TBTT_PROHIBIT + 1,
		   TBTT_PROHIBIT_HOLD_TIME_STOP_BCN & 0xFF);
	rtw_write8(rtwdev, REG_TBTT_PROHIBIT + 2,
		   (rtw_read8(rtwdev, REG_TBTT_PROHIBIT + 2) & 0xF0)
		   | (TBTT_PROHIBIT_HOLD_TIME_STOP_BCN >> 8));

	/* configure packet burst */
	rtw_write8_set(rtwdev, REG_SINGLE_AMPDU_CTRL, BIT_EN_SINGLE_APMDU);
	rtw_write8(rtwdev, REG_RX_PKT_LIMIT, WLAN_RX_PKT_LIMIT);
	rtw_write8(rtwdev, REG_MAX_AGGR_NUM, WLAN_MAX_AGG_NR);
	rtw_write8(rtwdev, REG_PIFS, WLAN_PIFS_VAL);
	rtw_write8_clr(rtwdev, REG_FWHW_TXQ_CTRL, BIT_MASK_TXQ_INIT);
	rtw_write8(rtwdev, REG_AMPDU_MAX_TIME, WLAN_AMPDU_MAX_TIME);

	rtw_write8(rtwdev, REG_SLOT, WLAN_SLOT_TIME);
	rtw_write16(rtwdev, REG_RETRY_LIMIT, WLAN_RL_VAL);
	rtw_write32(rtwdev, REG_BAR_MODE_CTRL, WLAN_BAR_VAL);
	rtw_write16(rtwdev, REG_ATIMWND, 0x2);

	rtw_phy_init(rtwdev);

	if (rtw_read32_mask(rtwdev, REG_BB_AMP, BIT_MASK_RX_LNA) != 0) {
		rtwdev->dm_info.rx_cck_agc_report_type = 1;
	} else {
		rtwdev->dm_info.rx_cck_agc_report_type = 0;
		rtw_warn(rtwdev, "unexpected cck agc report type");
	}

	rtw8723x_lck(rtwdev);

	rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, 0x50);
	rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, 0x20);

	rtw8703b_pwrtrack_init(rtwdev);
}

static bool rtw8703b_check_spur_ov_thres(struct rtw_dev *rtwdev,
					 u32 freq, u32 thres)
{
	bool ret = false;

	rtw_write32(rtwdev, REG_ANALOG_P4, DIS_3WIRE);
	rtw_write32(rtwdev, REG_PSDFN, freq);
	rtw_write32(rtwdev, REG_PSDFN, START_PSD | freq);

	msleep(30);
	if (rtw_read32(rtwdev, REG_PSDRPT) >= thres)
		ret = true;

	rtw_write32(rtwdev, REG_PSDFN, freq);
	rtw_write32(rtwdev, REG_ANALOG_P4, EN_3WIRE);

	return ret;
}

static void rtw8703b_cfg_notch(struct rtw_dev *rtwdev, u8 channel, bool notch)
{
	if (!notch) {
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_MASK_RXDSP, 0x1f);
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x0);
		rtw_write32(rtwdev, REG_OFDM1_CSI1, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI2, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI3, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI4, 0x00000000);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x0);
		return;
	}

	switch (channel) {
	case 5:
		fallthrough;
	case 13:
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_MASK_RXDSP, 0xb);
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x1);
		rtw_write32(rtwdev, REG_OFDM1_CSI1, 0x06000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI2, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI3, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI4, 0x00000000);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x1);
		break;
	case 6:
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_MASK_RXDSP, 0x4);
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x1);
		rtw_write32(rtwdev, REG_OFDM1_CSI1, 0x00000600);
		rtw_write32(rtwdev, REG_OFDM1_CSI2, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI3, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI4, 0x00000000);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x1);
		break;
	case 7:
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_MASK_RXDSP, 0x3);
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x1);
		rtw_write32(rtwdev, REG_OFDM1_CSI1, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI2, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI3, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI4, 0x06000000);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x1);
		break;
	case 8:
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_MASK_RXDSP, 0xa);
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x1);
		rtw_write32(rtwdev, REG_OFDM1_CSI1, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI2, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI3, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI4, 0x00000380);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x1);
		break;
	case 14:
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_MASK_RXDSP, 0x5);
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x1);
		rtw_write32(rtwdev, REG_OFDM1_CSI1, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI2, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI3, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI4, 0x00180000);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x1);
		break;
	default:
		rtw_warn(rtwdev,
			 "Bug: Notch filter enable called for channel %u!",
			 channel);
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x0);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x0);
		break;
	}
}

static void rtw8703b_spur_cal(struct rtw_dev *rtwdev, u8 channel)
{
	bool notch;
	u32 freq;

	if (channel == 5) {
		freq = FREQ_CH5;
	} else if (channel == 6) {
		freq = FREQ_CH6;
	} else if (channel == 7) {
		freq = FREQ_CH7;
	} else if (channel == 8) {
		freq = FREQ_CH8;
	} else if (channel == 13) {
		freq = FREQ_CH13;
	} else if (channel == 14) {
		freq = FREQ_CH14;
	} else {
		rtw8703b_cfg_notch(rtwdev, channel, false);
		return;
	}

	notch = rtw8703b_check_spur_ov_thres(rtwdev, freq, SPUR_THRES);
	rtw8703b_cfg_notch(rtwdev, channel, notch);
}

static void rtw8703b_set_channel_rf(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
	u32 rf_cfgch_a;
	u32 rf_cfgch_b;
	/* default value for 20M */
	u32 rf_rck = 0x00000C08;

	rf_cfgch_a = rtw_read_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK);
	rf_cfgch_b = rtw_read_rf(rtwdev, RF_PATH_B, RF_CFGCH, RFREG_MASK);

	rf_cfgch_a &= ~RFCFGCH_CHANNEL_MASK;
	rf_cfgch_b &= ~RFCFGCH_CHANNEL_MASK;
	rf_cfgch_a |= (channel & RFCFGCH_CHANNEL_MASK);
	rf_cfgch_b |= (channel & RFCFGCH_CHANNEL_MASK);

	rf_cfgch_a &= ~RFCFGCH_BW_MASK;
	switch (bw) {
	case RTW_CHANNEL_WIDTH_20:
		rf_cfgch_a |= RFCFGCH_BW_20M;
		break;
	case RTW_CHANNEL_WIDTH_40:
		rf_cfgch_a |= RFCFGCH_BW_40M;
		rf_rck = 0x00000C4C;
		break;
	default:
		break;
	}

	rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK, rf_cfgch_a);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_CFGCH, RFREG_MASK, rf_cfgch_b);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_RCK1, RFREG_MASK, rf_rck);
	rtw8703b_spur_cal(rtwdev, channel);
}

#define CCK_DFIR_NR_8703B 2
static const struct rtw_backup_info cck_dfir_cfg[][CCK_DFIR_NR_8703B] = {
	[0] = {
		{ .len = 4, .reg = REG_CCK_TXSF2, .val = 0x5A7DA0BD },
		{ .len = 4, .reg = REG_CCK_DBG, .val = 0x0000223B },
	},
	[1] = {
		{ .len = 4, .reg = REG_CCK_TXSF2, .val = 0x00000000 },
		{ .len = 4, .reg = REG_CCK_DBG, .val = 0x00000000 },
	},
};

static void rtw8703b_set_channel_bb(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				    u8 primary_ch_idx)
{
	const struct rtw_backup_info *cck_dfir;
	int i;

	cck_dfir = channel <= 13 ? cck_dfir_cfg[0] : cck_dfir_cfg[1];

	for (i = 0; i < CCK_DFIR_NR_8703B; i++, cck_dfir++)
		rtw_write32(rtwdev, cck_dfir->reg, cck_dfir->val);

	switch (bw) {
	case RTW_CHANNEL_WIDTH_20:
		rtw_write32_mask(rtwdev, REG_FPGA0_RFMOD, BIT_MASK_RFMOD, 0x0);
		rtw_write32_mask(rtwdev, REG_FPGA1_RFMOD, BIT_MASK_RFMOD, 0x0);
		rtw_write32_mask(rtwdev, REG_OFDM0_TX_PSD_NOISE,
				 GENMASK(31, 30), 0x0);
		rtw_write32(rtwdev, REG_BBRX_DFIR, 0x4A880000);
		rtw_write32(rtwdev, REG_OFDM0_A_TX_AFE, 0x19F60000);
		break;
	case RTW_CHANNEL_WIDTH_40:
		rtw_write32_mask(rtwdev, REG_FPGA0_RFMOD, BIT_MASK_RFMOD, 0x1);
		rtw_write32_mask(rtwdev, REG_FPGA1_RFMOD, BIT_MASK_RFMOD, 0x1);
		rtw_write32(rtwdev, REG_BBRX_DFIR, 0x40100000);
		rtw_write32(rtwdev, REG_OFDM0_A_TX_AFE, 0x51F60000);
		rtw_write32_mask(rtwdev, REG_CCK0_SYS, BIT_CCK_SIDE_BAND,
				 primary_ch_idx == RTW_SC_20_UPPER ? 1 : 0);
		rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTD_11N, 0xC00,
				 primary_ch_idx == RTW_SC_20_UPPER ? 2 : 1);

		rtw_write32_mask(rtwdev, REG_BB_PWR_SAV5_11N, GENMASK(27, 26),
				 primary_ch_idx == RTW_SC_20_UPPER ? 1 : 2);
		break;
	default:
		break;
	}
}

static void rtw8703b_set_channel(struct rtw_dev *rtwdev, u8 channel,
				 u8 bw, u8 primary_chan_idx)
{
	rtw8703b_set_channel_rf(rtwdev, channel, bw);
	rtw_set_channel_mac(rtwdev, channel, bw, primary_chan_idx);
	rtw8703b_set_channel_bb(rtwdev, channel, bw, primary_chan_idx);
}

/* Not all indices are valid, based on available data. None of the
 * known valid values are positive, so use 0x7f as "invalid".
 */
#define LNA_IDX_INVALID 0x7f
static const s8 lna_gain_table[16] = {
	-2, LNA_IDX_INVALID, LNA_IDX_INVALID, LNA_IDX_INVALID,
	-6, LNA_IDX_INVALID, LNA_IDX_INVALID, -19,
	-32, LNA_IDX_INVALID, -36, -42,
	LNA_IDX_INVALID, LNA_IDX_INVALID, LNA_IDX_INVALID, -48,
};

static s8 get_cck_rx_pwr(struct rtw_dev *rtwdev, u8 lna_idx, u8 vga_idx)
{
	s8 lna_gain = 0;

	if (lna_idx < ARRAY_SIZE(lna_gain_table))
		lna_gain = lna_gain_table[lna_idx];

	if (lna_gain >= 0) {
		rtw_warn(rtwdev, "incorrect lna index (%d)\n", lna_idx);
		return -120;
	}

	return lna_gain - 2 * vga_idx;
}

static void query_phy_status_cck(struct rtw_dev *rtwdev, u8 *phy_raw,
				 struct rtw_rx_pkt_stat *pkt_stat)
{
	struct phy_status_8703b *phy_status = (struct phy_status_8703b *)phy_raw;
	u8 vga_idx = phy_status->cck_agc_rpt_ofdm_cfosho_a & VGA_BITS;
	u8 lna_idx = phy_status->cck_agc_rpt_ofdm_cfosho_a & LNA_L_BITS;
	s8 rx_power;

	if (rtwdev->dm_info.rx_cck_agc_report_type == 1)
		lna_idx = FIELD_PREP(BIT_LNA_H_MASK,
				     phy_status->cck_rpt_b_ofdm_cfosho_b & LNA_H_BIT)
			| FIELD_PREP(BIT_LNA_L_MASK, lna_idx);
	else
		lna_idx = FIELD_PREP(BIT_LNA_L_MASK, lna_idx);
	rx_power = get_cck_rx_pwr(rtwdev, lna_idx, vga_idx);

	pkt_stat->rx_power[RF_PATH_A] = rx_power;
	pkt_stat->rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power, 1);
	rtwdev->dm_info.rssi[RF_PATH_A] = pkt_stat->rssi;
	pkt_stat->signal_power = rx_power;
}

static void query_phy_status_ofdm(struct rtw_dev *rtwdev, u8 *phy_raw,
				  struct rtw_rx_pkt_stat *pkt_stat)
{
	struct phy_status_8703b *phy_status = (struct phy_status_8703b *)phy_raw;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s8 val_s8;

	val_s8 = phy_status->path_agc[RF_PATH_A].gain & 0x3F;
	pkt_stat->rx_power[RF_PATH_A] = (val_s8 * 2) - 110;
	pkt_stat->rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power, 1);
	pkt_stat->rx_snr[RF_PATH_A] = (s8)(phy_status->path_rxsnr[RF_PATH_A] / 2);

	/* signal power reported by HW */
	val_s8 = phy_status->cck_sig_qual_ofdm_pwdb_all >> 1;
	pkt_stat->signal_power = (val_s8 & 0x7f) - 110;

	pkt_stat->rx_evm[RF_PATH_A] = phy_status->stream_rxevm[RF_PATH_A];
	pkt_stat->cfo_tail[RF_PATH_A] = phy_status->path_cfotail[RF_PATH_A];

	dm_info->curr_rx_rate = pkt_stat->rate;
	dm_info->rssi[RF_PATH_A] = pkt_stat->rssi;
	dm_info->rx_snr[RF_PATH_A] = pkt_stat->rx_snr[RF_PATH_A] >> 1;
	/* convert to KHz (used only for debugfs) */
	dm_info->cfo_tail[RF_PATH_A] = (pkt_stat->cfo_tail[RF_PATH_A] * 5) >> 1;

	/* (EVM value as s8 / 2) is dbm, should usually be in -33 to 0
	 * range. rx_evm_dbm needs the absolute (positive) value.
	 */
	val_s8 = (s8)pkt_stat->rx_evm[RF_PATH_A];
	val_s8 = clamp_t(s8, -val_s8 >> 1, 0, 64);
	val_s8 &= 0x3F; /* 64->0: second path of 1SS rate is 64 */
	dm_info->rx_evm_dbm[RF_PATH_A] = val_s8;
}

static void query_phy_status(struct rtw_dev *rtwdev, u8 *phy_status,
			     struct rtw_rx_pkt_stat *pkt_stat)
{
	if (pkt_stat->rate <= DESC_RATE11M)
		query_phy_status_cck(rtwdev, phy_status, pkt_stat);
	else
		query_phy_status_ofdm(rtwdev, phy_status, pkt_stat);
}

#define ADDA_ON_VAL_8703B 0x03c00014

static
void rtw8703b_iqk_config_mac(struct rtw_dev *rtwdev,
			     const struct rtw8723x_iqk_backup_regs *backup)
{
	rtw_write8(rtwdev, rtw8723x_common.iqk_mac8_regs[0], 0x3F);
	for (int i = 1; i < RTW8723X_IQK_MAC8_REG_NUM; i++)
		rtw_write8(rtwdev, rtw8723x_common.iqk_mac8_regs[i],
			   backup->mac8[i] & (~BIT(3)));
}

#define IQK_LTE_WRITE_VAL_8703B 0x00007700
#define IQK_DELAY_TIME_8703B 4

static void rtw8703b_iqk_one_shot(struct rtw_dev *rtwdev, bool tx)
{
	u32 regval;
	ktime_t t;
	s64 dur;
	int ret;

	/* enter IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, EN_IQK);
	rtw8723x_iqk_config_lte_path_gnt(rtwdev, IQK_LTE_WRITE_VAL_8703B);

	/* One shot, LOK & IQK */
	rtw_write32(rtwdev, REG_IQK_AGC_PTS_11N, 0xf9000000);
	rtw_write32(rtwdev, REG_IQK_AGC_PTS_11N, 0xf8000000);

	t = ktime_get();
	msleep(IQK_DELAY_TIME_8703B);
	ret = read_poll_timeout(rtw_read32, regval, regval != 0, 1000,
				100000, false, rtwdev,
				REG_IQK_RDY);
	dur = ktime_us_delta(ktime_get(), t);

	if (ret)
		rtw_warn(rtwdev, "[IQK] %s timed out after %lldus!\n",
			 tx ? "TX" : "RX", dur);
	else
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[IQK] %s done after %lldus\n",
			tx ? "TX" : "RX", dur);
}

static void rtw8703b_iqk_txrx_path_post(struct rtw_dev *rtwdev,
					const struct rtw8723x_iqk_backup_regs *backup)
{
	rtw8723x_iqk_restore_lte_path_gnt(rtwdev, backup);
	rtw_write32(rtwdev, REG_BB_SEL_BTG, backup->bb_sel_btg);

	/* leave IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, RST_IQK);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTDBG, 0x800, 0x0);
}

static u8 rtw8703b_iqk_check_tx_failed(struct rtw_dev *rtwdev)
{
	s32 tx_x, tx_y;
	u32 tx_fail;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0xeac = 0x%x\n",
		rtw_read32(rtwdev, REG_IQK_RES_RY));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0xe94 = 0x%x, 0xe9c = 0x%x\n",
		rtw_read32(rtwdev, REG_IQK_RES_TX),
		rtw_read32(rtwdev, REG_IQK_RES_TY));
	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] 0xe90(before IQK) = 0x%x, 0xe98(after IQK) = 0x%x\n",
		rtw_read32(rtwdev, REG_IQK_RDY),
		rtw_read32(rtwdev, 0xe98));

	tx_fail = rtw_read32_mask(rtwdev, REG_IQK_RES_RY, BIT_IQK_TX_FAIL);
	tx_x = rtw_read32_mask(rtwdev, REG_IQK_RES_TX, BIT_MASK_RES_TX);
	tx_y = rtw_read32_mask(rtwdev, REG_IQK_RES_TY, BIT_MASK_RES_TY);

	if (!tx_fail && tx_x != IQK_TX_X_ERR && tx_y != IQK_TX_Y_ERR)
		return IQK_TX_OK;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] A TX IQK failed\n");

	return 0;
}

static u8 rtw8703b_iqk_check_rx_failed(struct rtw_dev *rtwdev)
{
	s32 rx_x, rx_y;
	u32 rx_fail;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0xea4 = 0x%x, 0xeac = 0x%x\n",
		rtw_read32(rtwdev, REG_IQK_RES_RX),
		rtw_read32(rtwdev, REG_IQK_RES_RY));

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] 0xea0(before IQK) = 0x%x, 0xea8(after IQK) = 0x%x\n",
		rtw_read32(rtwdev, 0xea0),
		rtw_read32(rtwdev, 0xea8));

	rx_fail = rtw_read32_mask(rtwdev, REG_IQK_RES_RY, BIT_IQK_RX_FAIL);
	rx_x = rtw_read32_mask(rtwdev, REG_IQK_RES_RX, BIT_MASK_RES_RX);
	rx_y = rtw_read32_mask(rtwdev, REG_IQK_RES_RY, BIT_MASK_RES_RY);
	rx_y = abs(iqkxy_to_s32(rx_y));

	if (!rx_fail && rx_x != IQK_RX_X_ERR && rx_y != IQK_RX_Y_ERR &&
	    rx_x < IQK_RX_X_UPPER && rx_x > IQK_RX_X_LOWER &&
	    rx_y < IQK_RX_Y_LMT)
		return IQK_RX_OK;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] A RX IQK failed\n");

	return 0;
}

static u8 rtw8703b_iqk_tx_path(struct rtw_dev *rtwdev,
			       const struct rtw8723x_iqk_backup_regs *backup)
{
	u8 status;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path A TX IQK!\n");

	/* IQK setting */
	rtw_write32(rtwdev, REG_TXIQK_11N, 0x01007c00);
	rtw_write32(rtwdev, REG_RXIQK_11N, 0x01004800);
	rtw_write32(rtwdev, REG_TXIQK_TONE_A_11N, 0x18008c1c);
	rtw_write32(rtwdev, REG_RXIQK_TONE_A_11N, 0x38008c1c);
	rtw_write32(rtwdev, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_RX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_TXIQK_PI_A_11N, 0x8214030f);
	rtw_write32(rtwdev, REG_RXIQK_PI_A_11N, 0x28110000);
	rtw_write32(rtwdev, REG_TXIQK_PI_B, 0x82110000);
	rtw_write32(rtwdev, REG_RXIQK_PI_B, 0x28110000);

	/* LO calibration setting */
	rtw_write32(rtwdev, REG_IQK_AGC_RSP_11N, 0x00462911);

	/* leave IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, 0xffffff00, 0x000000);

	/* PA, PAD setting */
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTDBG, 0x800, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, 0x55, 0x7f, 0x7);
	rtw_write_rf(rtwdev, RF_PATH_A, 0x7f, RFREG_MASK, 0xd400);

	rtw8703b_iqk_one_shot(rtwdev, true);
	status = rtw8703b_iqk_check_tx_failed(rtwdev);

	rtw8703b_iqk_txrx_path_post(rtwdev, backup);

	return status;
}

static u8 rtw8703b_iqk_rx_path(struct rtw_dev *rtwdev,
			       const struct rtw8723x_iqk_backup_regs *backup)
{
	u8 status;
	u32 tx_x, tx_y;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path A RX IQK step 1!\n");
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0x67 @A RX IQK1 = 0x%x\n",
		rtw_read32_mask(rtwdev, REG_PAD_CTRL1, MASKBYTE3));
	rtw_write32(rtwdev, REG_BB_SEL_BTG, 0x99000000);

	/* disable IQC mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, RST_IQK);

	/* IQK setting */
	rtw_write32(rtwdev, REG_TXIQK_11N, 0x01007c00);
	rtw_write32(rtwdev, REG_RXIQK_11N, 0x01004800);

	/* path IQK setting */
	rtw_write32(rtwdev, REG_TXIQK_TONE_A_11N, 0x18008c1c);
	rtw_write32(rtwdev, REG_RXIQK_TONE_A_11N, 0x38008c1c);
	rtw_write32(rtwdev, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_RX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_TXIQK_PI_A_11N, 0x8214030f);
	rtw_write32(rtwdev, REG_RXIQK_PI_A_11N, 0x28110000);
	rtw_write32(rtwdev, REG_TXIQK_PI_B, 0x82110000);
	rtw_write32(rtwdev, REG_RXIQK_PI_B, 0x28110000);

	/* LOK setting */
	rtw_write32(rtwdev, REG_IQK_AGC_RSP_11N, 0x0046a911);

	/* RX IQK mode */
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, 0x80000, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, 0x30, RFREG_MASK, 0x30000);
	rtw_write_rf(rtwdev, RF_PATH_A, 0x31, RFREG_MASK, 0x00007);
	rtw_write_rf(rtwdev, RF_PATH_A, 0x32, RFREG_MASK, 0x57db7);

	rtw8703b_iqk_one_shot(rtwdev, true);
	/* leave IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, 0xffffff00, 0x000000);
	status = rtw8703b_iqk_check_tx_failed(rtwdev);

	if (!status)
		goto restore;

	/* second round */
	tx_x = rtw_read32_mask(rtwdev, REG_IQK_RES_TX, BIT_MASK_RES_TX);
	tx_y = rtw_read32_mask(rtwdev, REG_IQK_RES_TY, BIT_MASK_RES_TY);

	rtw_write32(rtwdev, REG_TXIQK_11N, BIT_SET_TXIQK_11N(tx_x, tx_y));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0xe40 = 0x%x u4tmp = 0x%x\n",
		rtw_read32(rtwdev, REG_TXIQK_11N),
		BIT_SET_TXIQK_11N(tx_x, tx_y));

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path A RX IQK step 2!\n");
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0x67 @A RX IQK 2 = 0x%x\n",
		rtw_read32_mask(rtwdev, REG_PAD_CTRL1, MASKBYTE3));

	/* IQK setting */
	rtw_write32(rtwdev, REG_RXIQK_11N, 0x01004800);
	rtw_write32(rtwdev, REG_TXIQK_TONE_A_11N, 0x38008c1c);
	rtw_write32(rtwdev, REG_RXIQK_TONE_A_11N, 0x18008c1c);
	rtw_write32(rtwdev, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_RX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_TXIQK_PI_A_11N, 0x82110000);
	rtw_write32(rtwdev, REG_RXIQK_PI_A_11N, 0x28160c1f);
	rtw_write32(rtwdev, REG_TXIQK_PI_B, 0x82110000);
	rtw_write32(rtwdev, REG_RXIQK_PI_B, 0x28110000);

	/* LO calibration setting */
	rtw_write32(rtwdev, REG_IQK_AGC_RSP_11N, 0x0046a8d1);

	/* leave IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, 0xffffff00, 0x000000);
	/* modify RX IQK mode table */
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, 0x80000, 0x1);
	/* RF_RCK_OS, RF_TXPA_G1, RF_TXPA_G2 */
	rtw_write_rf(rtwdev, RF_PATH_A, 0x30, RFREG_MASK, 0x30000);
	rtw_write_rf(rtwdev, RF_PATH_A, 0x31, RFREG_MASK, 0x00007);
	rtw_write_rf(rtwdev, RF_PATH_A, 0x32, RFREG_MASK, 0xf7d77);

	/* PA, PAD setting */
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTDBG, 0x800, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, 0x55, 0x7f, 0x5);

	rtw8703b_iqk_one_shot(rtwdev, false);
	status |= rtw8703b_iqk_check_rx_failed(rtwdev);

restore:
	rtw8703b_iqk_txrx_path_post(rtwdev, backup);

	return status;
}

static
void rtw8703b_iqk_one_round(struct rtw_dev *rtwdev, s32 result[][IQK_NR], u8 t,
			    const struct rtw8723x_iqk_backup_regs *backup)
{
	u32 i;
	u8 a_ok;

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] IQ Calibration for 1T1R_S0/S1 for %d times\n", t);

	rtw8723x_iqk_path_adda_on(rtwdev, ADDA_ON_VAL_8703B);
	rtw8703b_iqk_config_mac(rtwdev, backup);
	rtw_write32_mask(rtwdev, REG_CCK_ANT_SEL_11N, 0x0f000000, 0xf);
	rtw_write32(rtwdev, REG_BB_RX_PATH_11N, 0x03a05600);
	rtw_write32(rtwdev, REG_TRMUX_11N, 0x000800e4);
	rtw_write32(rtwdev, REG_BB_PWR_SAV1_11N, 0x25204000);

	for (i = 0; i < PATH_IQK_RETRY; i++) {
		a_ok = rtw8703b_iqk_tx_path(rtwdev, backup);
		if (a_ok == IQK_TX_OK) {
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"[IQK] path A TX IQK success!\n");
			result[t][IQK_S1_TX_X] =
				rtw_read32_mask(rtwdev, REG_IQK_RES_TX,
						BIT_MASK_RES_TX);
			result[t][IQK_S1_TX_Y] =
				rtw_read32_mask(rtwdev, REG_IQK_RES_TY,
						BIT_MASK_RES_TY);
			break;
		}

		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path A TX IQK fail!\n");
		result[t][IQK_S1_TX_X] = 0x100;
		result[t][IQK_S1_TX_Y] = 0x0;
	}

	for (i = 0; i < PATH_IQK_RETRY; i++) {
		a_ok = rtw8703b_iqk_rx_path(rtwdev, backup);
		if (a_ok == (IQK_TX_OK | IQK_RX_OK)) {
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"[IQK] path A RX IQK success!\n");
			result[t][IQK_S1_RX_X] =
				rtw_read32_mask(rtwdev, REG_IQK_RES_RX,
						BIT_MASK_RES_RX);
			result[t][IQK_S1_RX_Y] =
				rtw_read32_mask(rtwdev, REG_IQK_RES_RY,
						BIT_MASK_RES_RY);
			break;
		}

		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path A RX IQK fail!\n");
		result[t][IQK_S1_RX_X] = 0x100;
		result[t][IQK_S1_RX_Y] = 0x0;
	}

	if (a_ok == 0x0)
		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path A IQK fail!\n");

	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, RST_IQK);
	mdelay(1);
}

static
void rtw8703b_iqk_fill_a_matrix(struct rtw_dev *rtwdev, const s32 result[])
{
	u32 tmp_rx_iqi = 0x40000100 & GENMASK(31, 16);
	s32 tx1_a, tx1_a_ext;
	s32 tx1_c, tx1_c_ext;
	s32 oldval_1;
	s32 x, y;

	if (result[IQK_S1_TX_X] == 0)
		return;

	oldval_1 = rtw_read32_mask(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE,
				   BIT_MASK_TXIQ_ELM_D);

	x = iqkxy_to_s32(result[IQK_S1_TX_X]);
	tx1_a = iqk_mult(x, oldval_1, &tx1_a_ext);
	rtw_write32_mask(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE,
			 BIT_MASK_TXIQ_ELM_A, tx1_a);
	rtw_write32_mask(rtwdev, REG_OFDM_0_ECCA_THRESHOLD,
			 BIT_MASK_OFDM0_EXT_A, tx1_a_ext);

	y = iqkxy_to_s32(result[IQK_S1_TX_Y]);
	tx1_c = iqk_mult(y, oldval_1, &tx1_c_ext);
	rtw_write32_mask(rtwdev, REG_TXIQK_MATRIXA_LSB2_11N, MASKH4BITS,
			 BIT_SET_TXIQ_ELM_C1(tx1_c));
	rtw_write32_mask(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE,
			 BIT_MASK_TXIQ_ELM_C, BIT_SET_TXIQ_ELM_C2(tx1_c));
	rtw_write32_mask(rtwdev, REG_OFDM_0_ECCA_THRESHOLD,
			 BIT_MASK_OFDM0_EXT_C, tx1_c_ext);

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] X = 0x%x, TX1_A = 0x%x, oldval_1 0x%x\n",
		x, tx1_a, oldval_1);
	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] Y = 0x%x, TX1_C = 0x%x\n", y, tx1_c);

	if (result[IQK_S1_RX_X] == 0)
		return;

	tmp_rx_iqi |= FIELD_PREP(BIT_MASK_RXIQ_S1_X, result[IQK_S1_RX_X]);
	tmp_rx_iqi |= FIELD_PREP(BIT_MASK_RXIQ_S1_Y1, result[IQK_S1_RX_Y]);
	rtw_write32(rtwdev, REG_A_RXIQI, tmp_rx_iqi);
	rtw_write32_mask(rtwdev, REG_RXIQK_MATRIX_LSB_11N, BIT_MASK_RXIQ_S1_Y2,
			 BIT_SET_RXIQ_S1_Y2(result[IQK_S1_RX_Y]));
}

static void rtw8703b_phy_calibration(struct rtw_dev *rtwdev)
{
	/* For some reason path A is called S1 and B S0 in shared
	 * rtw88 calibration data.
	 */
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw8723x_iqk_backup_regs backup;
	u8 final_candidate = IQK_ROUND_INVALID;
	s32 result[IQK_ROUND_SIZE][IQK_NR];
	bool good;
	u8 i, j;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] Start!\n");

	memset(result, 0, sizeof(result));

	rtw8723x_iqk_backup_path_ctrl(rtwdev, &backup);
	rtw8723x_iqk_backup_lte_path_gnt(rtwdev, &backup);
	rtw8723x_iqk_backup_regs(rtwdev, &backup);

	for (i = IQK_ROUND_0; i <= IQK_ROUND_2; i++) {
		rtw8723x_iqk_config_path_ctrl(rtwdev);
		rtw8723x_iqk_config_lte_path_gnt(rtwdev, IQK_LTE_WRITE_VAL_8703B);

		rtw8703b_iqk_one_round(rtwdev, result, i, &backup);

		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[IQK] back to BB mode, load original values!\n");
		if (i > IQK_ROUND_0)
			rtw8723x_iqk_restore_regs(rtwdev, &backup);
		rtw8723x_iqk_restore_lte_path_gnt(rtwdev, &backup);
		rtw8723x_iqk_restore_path_ctrl(rtwdev, &backup);

		for (j = IQK_ROUND_0; j < i; j++) {
			good = rtw8723x_iqk_similarity_cmp(rtwdev, result, j, i);

			if (good) {
				final_candidate = j;
				rtw_dbg(rtwdev, RTW_DBG_RFK,
					"[IQK] cmp %d:%d final_candidate is %x\n",
					j, i, final_candidate);
				goto iqk_done;
			}
		}
	}

	if (final_candidate == IQK_ROUND_INVALID) {
		s32 reg_tmp = 0;

		for (i = 0; i < IQK_NR; i++)
			reg_tmp += result[IQK_ROUND_HYBRID][i];

		if (reg_tmp != 0) {
			final_candidate = IQK_ROUND_HYBRID;
		} else {
			WARN(1, "IQK failed\n");
			goto out;
		}
	}

iqk_done:
	/* only path A is calibrated in rtl8703b */
	rtw8703b_iqk_fill_a_matrix(rtwdev, result[final_candidate]);

	dm_info->iqk.result.s1_x = result[final_candidate][IQK_S1_TX_X];
	dm_info->iqk.result.s1_y = result[final_candidate][IQK_S1_TX_Y];
	dm_info->iqk.result.s0_x = result[final_candidate][IQK_S0_TX_X];
	dm_info->iqk.result.s0_y = result[final_candidate][IQK_S0_TX_Y];
	dm_info->iqk.done = true;

out:
	rtw_write32(rtwdev, REG_BB_SEL_BTG, backup.bb_sel_btg);

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] final_candidate is %x\n",
		final_candidate);

	for (i = IQK_ROUND_0; i < IQK_ROUND_SIZE; i++)
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[IQK] Result %u: rege94_s1=%x rege9c_s1=%x regea4_s1=%x regeac_s1=%x rege94_s0=%x rege9c_s0=%x regea4_s0=%x regeac_s0=%x %s\n",
			i,
			result[i][0], result[i][1], result[i][2], result[i][3],
			result[i][4], result[i][5], result[i][6], result[i][7],
			final_candidate == i ? "(final candidate)" : "");

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] 0xc80 = 0x%x 0xc94 = 0x%x 0xc14 = 0x%x 0xca0 = 0x%x\n",
		rtw_read32(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE),
		rtw_read32(rtwdev, REG_TXIQK_MATRIXA_LSB2_11N),
		rtw_read32(rtwdev, REG_A_RXIQI),
		rtw_read32(rtwdev, REG_RXIQK_MATRIX_LSB_11N));
	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] 0xcd0 = 0x%x 0xcd4 = 0x%x 0xcd8 = 0x%x\n",
		rtw_read32(rtwdev, REG_TXIQ_AB_S0),
		rtw_read32(rtwdev, REG_TXIQ_CD_S0),
		rtw_read32(rtwdev, REG_RXIQ_AB_S0));

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] Finished.\n");
}

static void rtw8703b_set_iqk_matrix_by_result(struct rtw_dev *rtwdev,
					      u32 ofdm_swing, u8 rf_path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s32 ele_A, ele_D, ele_C;
	s32 ele_A_ext, ele_C_ext, ele_D_ext;
	s32 iqk_result_x;
	s32 iqk_result_y;
	s32 value32;

	switch (rf_path) {
	default:
	case RF_PATH_A:
		iqk_result_x = dm_info->iqk.result.s1_x;
		iqk_result_y = dm_info->iqk.result.s1_y;
		break;
	case RF_PATH_B:
		iqk_result_x = dm_info->iqk.result.s0_x;
		iqk_result_y = dm_info->iqk.result.s0_y;
		break;
	}

	/* new element D */
	ele_D = OFDM_SWING_D(ofdm_swing);
	iqk_mult(iqk_result_x, ele_D, &ele_D_ext);
	/* new element A */
	iqk_result_x = iqkxy_to_s32(iqk_result_x);
	ele_A = iqk_mult(iqk_result_x, ele_D, &ele_A_ext);
	/* new element C */
	iqk_result_y = iqkxy_to_s32(iqk_result_y);
	ele_C = iqk_mult(iqk_result_y, ele_D, &ele_C_ext);

	switch (rf_path) {
	case RF_PATH_A:
	default:
		/* write new elements A, C, D, and element B is always 0 */
		value32 = BIT_SET_TXIQ_ELM_ACD(ele_A, ele_C, ele_D);
		rtw_write32(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE, value32);
		value32 = BIT_SET_TXIQ_ELM_C1(ele_C);
		rtw_write32_mask(rtwdev, REG_TXIQK_MATRIXA_LSB2_11N, MASKH4BITS,
				 value32);
		value32 = rtw_read32(rtwdev, REG_OFDM_0_ECCA_THRESHOLD);
		value32 &= ~BIT_MASK_OFDM0_EXTS;
		value32 |= BIT_SET_OFDM0_EXTS(ele_A_ext, ele_C_ext, ele_D_ext);
		rtw_write32(rtwdev, REG_OFDM_0_ECCA_THRESHOLD, value32);
		break;

	case RF_PATH_B:
		/* write new elements A, C, D, and element B is always 0 */
		value32 = BIT_SET_TXIQ_ELM_ACD(ele_A, ele_C, ele_D);
		rtw_write32(rtwdev, REG_OFDM_0_XB_TX_IQ_IMBALANCE, value32);
		value32 = BIT_SET_TXIQ_ELM_C1(ele_C);
		rtw_write32_mask(rtwdev, REG_TXIQK_MATRIXB_LSB2_11N, MASKH4BITS,
				 value32);
		value32 = rtw_read32(rtwdev, REG_OFDM_0_ECCA_THRESHOLD);
		value32 &= ~BIT_MASK_OFDM0_EXTS_B;
		value32 |= BIT_SET_OFDM0_EXTS_B(ele_A_ext, ele_C_ext, ele_D_ext);
		rtw_write32(rtwdev, REG_OFDM_0_ECCA_THRESHOLD, value32);
		break;
	}
}

static void rtw8703b_set_iqk_matrix(struct rtw_dev *rtwdev, s8 ofdm_index,
				    u8 rf_path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s32 value32;
	u32 ofdm_swing;

	ofdm_index = clamp_t(s8, ofdm_index, 0, RTW_OFDM_SWING_TABLE_SIZE - 1);

	ofdm_swing = rtw8703b_ofdm_swing_table[ofdm_index];

	if (dm_info->iqk.done) {
		rtw8703b_set_iqk_matrix_by_result(rtwdev, ofdm_swing, rf_path);
		return;
	}

	switch (rf_path) {
	case RF_PATH_A:
	default:
		rtw_write32(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE, ofdm_swing);
		rtw_write32_mask(rtwdev, REG_TXIQK_MATRIXA_LSB2_11N, MASKH4BITS,
				 0x00);

		value32 = rtw_read32(rtwdev, REG_OFDM_0_ECCA_THRESHOLD);
		value32 &= ~BIT_MASK_OFDM0_EXTS;
		rtw_write32(rtwdev, REG_OFDM_0_ECCA_THRESHOLD, value32);
		break;

	case RF_PATH_B:
		rtw_write32(rtwdev, REG_OFDM_0_XB_TX_IQ_IMBALANCE, ofdm_swing);
		rtw_write32_mask(rtwdev, REG_TXIQK_MATRIXB_LSB2_11N, MASKH4BITS,
				 0x00);

		value32 = rtw_read32(rtwdev, REG_OFDM_0_ECCA_THRESHOLD);
		value32 &= ~BIT_MASK_OFDM0_EXTS_B;
		rtw_write32(rtwdev, REG_OFDM_0_ECCA_THRESHOLD, value32);
		break;
	}
}

static void rtw8703b_pwrtrack_set_ofdm_pwr(struct rtw_dev *rtwdev, s8 swing_idx,
					   s8 txagc_idx)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	dm_info->txagc_remnant_ofdm[RF_PATH_A] = txagc_idx;

	/* Only path A is calibrated for rtl8703b */
	rtw8703b_set_iqk_matrix(rtwdev, swing_idx, RF_PATH_A);
}

static void rtw8703b_pwrtrack_set_cck_pwr(struct rtw_dev *rtwdev, s8 swing_idx,
					  s8 txagc_idx)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	dm_info->txagc_remnant_cck = txagc_idx;

	swing_idx = clamp_t(s8, swing_idx, 0, RTW_CCK_SWING_TABLE_SIZE - 1);

	BUILD_BUG_ON(ARRAY_SIZE(rtw8703b_cck_pwr_regs)
		     != ARRAY_SIZE(rtw8703b_cck_swing_table[0]));

	for (int i = 0; i < ARRAY_SIZE(rtw8703b_cck_pwr_regs); i++)
		rtw_write8(rtwdev, rtw8703b_cck_pwr_regs[i],
			   rtw8703b_cck_swing_table[swing_idx][i]);
}

static void rtw8703b_pwrtrack_set(struct rtw_dev *rtwdev, u8 path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_hal *hal = &rtwdev->hal;
	u8 limit_ofdm;
	u8 limit_cck = 21;
	s8 final_ofdm_swing_index;
	s8 final_cck_swing_index;

	limit_ofdm = rtw8723x_pwrtrack_get_limit_ofdm(rtwdev);

	final_ofdm_swing_index = dm_info->default_ofdm_index +
				 dm_info->delta_power_index[path];
	final_cck_swing_index = dm_info->default_cck_index +
				dm_info->delta_power_index[path];

	if (final_ofdm_swing_index > limit_ofdm)
		rtw8703b_pwrtrack_set_ofdm_pwr(rtwdev, limit_ofdm,
					       final_ofdm_swing_index - limit_ofdm);
	else if (final_ofdm_swing_index < 0)
		rtw8703b_pwrtrack_set_ofdm_pwr(rtwdev, 0,
					       final_ofdm_swing_index);
	else
		rtw8703b_pwrtrack_set_ofdm_pwr(rtwdev, final_ofdm_swing_index, 0);

	if (final_cck_swing_index > limit_cck)
		rtw8703b_pwrtrack_set_cck_pwr(rtwdev, limit_cck,
					      final_cck_swing_index - limit_cck);
	else if (final_cck_swing_index < 0)
		rtw8703b_pwrtrack_set_cck_pwr(rtwdev, 0,
					      final_cck_swing_index);
	else
		rtw8703b_pwrtrack_set_cck_pwr(rtwdev, final_cck_swing_index, 0);

	rtw_phy_set_tx_power_level(rtwdev, hal->current_channel);
}

static void rtw8703b_phy_pwrtrack(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_swing_table swing_table;
	u8 thermal_value, delta, path;
	bool do_iqk = false;

	rtw_phy_config_swing_table(rtwdev, &swing_table);

	if (rtwdev->efuse.thermal_meter[0] == 0xff)
		return;

	thermal_value = rtw_read_rf(rtwdev, RF_PATH_A, RF_T_METER, 0xfc00);

	rtw_phy_pwrtrack_avg(rtwdev, thermal_value, RF_PATH_A);

	do_iqk = rtw_phy_pwrtrack_need_iqk(rtwdev);

	if (do_iqk)
		rtw8723x_lck(rtwdev);

	if (dm_info->pwr_trk_init_trigger)
		dm_info->pwr_trk_init_trigger = false;
	else if (!rtw_phy_pwrtrack_thermal_changed(rtwdev, thermal_value,
						   RF_PATH_A))
		goto iqk;

	delta = rtw_phy_pwrtrack_get_delta(rtwdev, RF_PATH_A);

	delta = min_t(u8, delta, RTW_PWR_TRK_TBL_SZ - 1);

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		s8 delta_cur, delta_last;

		delta_last = dm_info->delta_power_index[path];
		delta_cur = rtw_phy_pwrtrack_get_pwridx(rtwdev, &swing_table,
							path, RF_PATH_A, delta);
		if (delta_last == delta_cur)
			continue;

		dm_info->delta_power_index[path] = delta_cur;
		rtw8703b_pwrtrack_set(rtwdev, path);
	}

	rtw8723x_pwrtrack_set_xtal(rtwdev, RF_PATH_A, delta);

iqk:
	if (do_iqk)
		rtw8703b_phy_calibration(rtwdev);
}

static void rtw8703b_pwr_track(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	if (efuse->power_track_type != 0) {
		rtw_warn(rtwdev, "unsupported power track type");
		return;
	}

	if (!dm_info->pwr_trk_triggered) {
		rtw_write_rf(rtwdev, RF_PATH_A, RF_T_METER,
			     GENMASK(17, 16), 0x03);
		dm_info->pwr_trk_triggered = true;
		return;
	}

	rtw8703b_phy_pwrtrack(rtwdev);
	dm_info->pwr_trk_triggered = false;
}

static void rtw8703b_coex_set_gnt_fix(struct rtw_dev *rtwdev)
{
}

static void rtw8703b_coex_set_gnt_debug(struct rtw_dev *rtwdev)
{
}

static void rtw8703b_coex_set_rfe_type(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_rfe *coex_rfe = &coex->rfe;

	coex_rfe->rfe_module_type = rtwdev->efuse.rfe_option;
	coex_rfe->ant_switch_polarity = 0;
	coex_rfe->ant_switch_exist = false;
	coex_rfe->ant_switch_with_bt = false;
	coex_rfe->ant_switch_diversity = false;
	coex_rfe->wlg_at_btg = true;

	/* disable LTE coex on wifi side */
	rtw_coex_write_indirect_reg(rtwdev, LTE_COEX_CTRL, BIT_LTE_COEX_EN, 0x0);
	rtw_coex_write_indirect_reg(rtwdev, LTE_WL_TRX_CTRL, MASKLWORD, 0xffff);
	rtw_coex_write_indirect_reg(rtwdev, LTE_BT_TRX_CTRL, MASKLWORD, 0xffff);
}

static void rtw8703b_coex_set_wl_tx_power(struct rtw_dev *rtwdev, u8 wl_pwr)
{
}

static void rtw8703b_coex_set_wl_rx_gain(struct rtw_dev *rtwdev, bool low_gain)
{
}

static const u8 rtw8703b_pwrtrk_2gb_n[] = {
	0, 0, 1, 2, 2, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6,
	7, 7, 7, 7, 8, 8, 9, 9, 10, 10, 10, 11, 11, 11, 11
};

static const u8 rtw8703b_pwrtrk_2gb_p[] = {
	0, 1, 2, 3, 3, 4, 4, 4, 4, 5, 5, 6, 7, 7, 7,
	8, 8, 9, 9, 10, 10, 11, 11, 12, 13, 13, 14, 14, 15, 15
};

static const u8 rtw8703b_pwrtrk_2ga_n[] = {
	0, 0, 1, 2, 2, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6,
	7, 7, 7, 7, 8, 8, 9, 9, 10, 10, 10, 11, 11, 11, 11
};

static const u8 rtw8703b_pwrtrk_2ga_p[] = {
	0, 1, 2, 3, 3, 4, 4, 4, 4, 5, 5, 6, 7, 7, 7,
	8, 8, 9, 9, 10, 10, 11, 11, 12, 13, 13, 14, 14, 15, 15
};

static const u8 rtw8703b_pwrtrk_2g_cck_b_n[] = {
	0, 0, 1, 2, 2, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6,
	7, 7, 7, 7, 8, 8, 9, 9, 10, 10, 10, 11, 11, 11, 11
};

static const u8 rtw8703b_pwrtrk_2g_cck_b_p[] = {
	0, 0, 1, 1, 2, 3, 3, 3, 4, 4, 4, 5, 6, 6, 6,
	7, 7, 8, 8, 8, 9, 10, 10, 10, 11, 11, 12, 12, 13, 13
};

static const u8 rtw8703b_pwrtrk_2g_cck_a_n[] = {
	0, 0, 1, 2, 2, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6,
	7, 7, 7, 7, 8, 8, 9, 9, 10, 10, 10, 11, 11, 11, 11
};

static const u8 rtw8703b_pwrtrk_2g_cck_a_p[] = {
	0, 0, 1, 1, 2, 3, 3, 3, 4, 4, 4, 5, 6, 6, 6,
	7, 7, 8, 8, 8, 9, 10, 10, 10, 11, 11, 12, 12, 13, 13
};

static const s8 rtw8703b_pwrtrk_xtal_n[] = {
	0, 0, 0, -1, -1, -1, -1, -2, -2, -2, -3, -3, -3, -3, -3,
	-4, -2, -2, -1, -1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1
};

static const s8 rtw8703b_pwrtrk_xtal_p[] = {
	0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 1, 0, -1, -1, -1,
	-2, -3, -7, -9, -10, -11, -14, -16, -18, -20, -22, -24, -26, -28, -30
};

static const struct rtw_pwr_track_tbl rtw8703b_rtw_pwr_track_tbl = {
	.pwrtrk_2gb_n = rtw8703b_pwrtrk_2gb_n,
	.pwrtrk_2gb_p = rtw8703b_pwrtrk_2gb_p,
	.pwrtrk_2ga_n = rtw8703b_pwrtrk_2ga_n,
	.pwrtrk_2ga_p = rtw8703b_pwrtrk_2ga_p,
	.pwrtrk_2g_cckb_n = rtw8703b_pwrtrk_2g_cck_b_n,
	.pwrtrk_2g_cckb_p = rtw8703b_pwrtrk_2g_cck_b_p,
	.pwrtrk_2g_ccka_n = rtw8703b_pwrtrk_2g_cck_a_n,
	.pwrtrk_2g_ccka_p = rtw8703b_pwrtrk_2g_cck_a_p,
	.pwrtrk_xtal_n = rtw8703b_pwrtrk_xtal_n,
	.pwrtrk_xtal_p = rtw8703b_pwrtrk_xtal_p,
};

static const struct rtw_rfe_def rtw8703b_rfe_defs[] = {
	[0] = { .phy_pg_tbl	= &rtw8703b_bb_pg_tbl,
		.txpwr_lmt_tbl	= &rtw8703b_txpwr_lmt_tbl,
		.pwr_track_tbl	= &rtw8703b_rtw_pwr_track_tbl, },
};

/* Shared-Antenna Coex Table */
static const struct coex_table_para table_sant_8703b[] = {
	{0xffffffff, 0xffffffff}, /* case-0 */
	{0x55555555, 0x55555555},
	{0x66555555, 0x66555555},
	{0xaaaaaaaa, 0xaaaaaaaa},
	{0x5a5a5a5a, 0x5a5a5a5a},
	{0xfafafafa, 0xfafafafa}, /* case-5 */
	{0x6a5a5555, 0xaaaaaaaa},
	{0x6a5a56aa, 0x6a5a56aa},
	{0x6a5a5a5a, 0x6a5a5a5a},
	{0x66555555, 0x5a5a5a5a},
	{0x66555555, 0x6a5a5a5a}, /* case-10 */
	{0x66555555, 0x6a5a5aaa},
	{0x66555555, 0x5a5a5aaa},
	{0x66555555, 0x6aaa5aaa},
	{0x66555555, 0xaaaa5aaa},
	{0x66555555, 0xaaaaaaaa}, /* case-15 */
	{0xffff55ff, 0xfafafafa},
	{0xffff55ff, 0x6afa5afa},
	{0xaaffffaa, 0xfafafafa},
	{0xaa5555aa, 0x5a5a5a5a},
	{0xaa5555aa, 0x6a5a5a5a}, /* case-20 */
	{0xaa5555aa, 0xaaaaaaaa},
	{0xffffffff, 0x5a5a5a5a},
	{0xffffffff, 0x5a5a5a5a},
	{0xffffffff, 0x55555555},
	{0xffffffff, 0x5a5a5aaa}, /* case-25 */
	{0x55555555, 0x5a5a5a5a},
	{0x55555555, 0xaaaaaaaa},
	{0x55555555, 0x6a5a6a5a},
	{0x66556655, 0x66556655},
	{0x66556aaa, 0x6a5a6aaa}, /* case-30 */
	{0xffffffff, 0x5aaa5aaa},
	{0x56555555, 0x5a5a5aaa},
};

/* Shared-Antenna TDMA */
static const struct coex_tdma_para tdma_sant_8703b[] = {
	{ {0x00, 0x00, 0x00, 0x00, 0x00} }, /* case-0 */
	{ {0x61, 0x45, 0x03, 0x11, 0x11} }, /* case-1 */
	{ {0x61, 0x3a, 0x03, 0x11, 0x11} },
	{ {0x61, 0x30, 0x03, 0x11, 0x11} },
	{ {0x61, 0x20, 0x03, 0x11, 0x11} },
	{ {0x61, 0x10, 0x03, 0x11, 0x11} }, /* case-5 */
	{ {0x61, 0x45, 0x03, 0x11, 0x10} },
	{ {0x61, 0x3a, 0x03, 0x11, 0x10} },
	{ {0x61, 0x30, 0x03, 0x11, 0x10} },
	{ {0x61, 0x20, 0x03, 0x11, 0x10} },
	{ {0x61, 0x10, 0x03, 0x11, 0x10} }, /* case-10 */
	{ {0x61, 0x08, 0x03, 0x11, 0x14} },
	{ {0x61, 0x08, 0x03, 0x10, 0x14} },
	{ {0x51, 0x08, 0x03, 0x10, 0x54} },
	{ {0x51, 0x08, 0x03, 0x10, 0x55} },
	{ {0x51, 0x08, 0x07, 0x10, 0x54} }, /* case-15 */
	{ {0x51, 0x45, 0x03, 0x10, 0x50} },
	{ {0x51, 0x3a, 0x03, 0x10, 0x50} },
	{ {0x51, 0x30, 0x03, 0x10, 0x50} },
	{ {0x51, 0x20, 0x03, 0x10, 0x50} },
	{ {0x51, 0x10, 0x03, 0x10, 0x50} }, /* case-20 */
	{ {0x51, 0x4a, 0x03, 0x10, 0x50} },
	{ {0x51, 0x0c, 0x03, 0x10, 0x54} },
	{ {0x55, 0x08, 0x03, 0x10, 0x54} },
	{ {0x65, 0x10, 0x03, 0x11, 0x10} },
	{ {0x51, 0x10, 0x03, 0x10, 0x51} }, /* case-25 */
	{ {0x51, 0x08, 0x03, 0x10, 0x50} },
	{ {0x61, 0x08, 0x03, 0x11, 0x11} },
};

static const struct rtw_chip_ops rtw8703b_ops = {
	.power_on		= rtw_power_on,
	.power_off		= rtw_power_off,
	.mac_init		= rtw8723x_mac_init,
	.dump_fw_crash		= NULL,
	.shutdown		= NULL,
	.read_efuse		= rtw8703b_read_efuse,
	.phy_set_param		= rtw8703b_phy_set_param,
	.set_channel		= rtw8703b_set_channel,
	.query_phy_status	= query_phy_status,
	.read_rf		= rtw_phy_read_rf_sipi,
	.write_rf		= rtw_phy_write_rf_reg_sipi,
	.set_tx_power_index	= rtw8723x_set_tx_power_index,
	.set_antenna		= NULL,
	.cfg_ldo25		= rtw8723x_cfg_ldo25,
	.efuse_grant		= rtw8723x_efuse_grant,
	.set_ampdu_factor	= NULL,
	.false_alarm_statistics	= rtw8723x_false_alarm_statistics,
	.phy_calibration	= rtw8703b_phy_calibration,
	.dpk_track		= NULL,
	/* 8723d uses REG_CSRATIO to set dm_info.cck_pd_default, which
	 * is used in its cck_pd_set function. According to comments
	 * in the vendor driver code it doesn't exist in this chip
	 * generation, only 0xa0a ("ODM_CCK_PD_THRESH", which is only
	 * *written* to).
	 */
	.cck_pd_set		= NULL,
	.pwr_track		= rtw8703b_pwr_track,
	.config_bfee		= NULL,
	.set_gid_table		= NULL,
	.cfg_csi_rate		= NULL,
	.adaptivity_init	= NULL,
	.adaptivity		= NULL,
	.cfo_init		= NULL,
	.cfo_track		= NULL,
	.config_tx_path		= NULL,
	.config_txrx_mode	= NULL,
	.fill_txdesc_checksum	= rtw8723x_fill_txdesc_checksum,

	/* for coex */
	.coex_set_init		= rtw8723x_coex_cfg_init,
	.coex_set_ant_switch	= NULL,
	.coex_set_gnt_fix	= rtw8703b_coex_set_gnt_fix,
	.coex_set_gnt_debug	= rtw8703b_coex_set_gnt_debug,
	.coex_set_rfe_type	= rtw8703b_coex_set_rfe_type,
	.coex_set_wl_tx_power	= rtw8703b_coex_set_wl_tx_power,
	.coex_set_wl_rx_gain	= rtw8703b_coex_set_wl_rx_gain,
};

const struct rtw_chip_info rtw8703b_hw_spec = {
	.ops = &rtw8703b_ops,
	.id = RTW_CHIP_TYPE_8703B,

	.fw_name = "rtw88/rtw8703b_fw.bin",
	.wlan_cpu = RTW_WCPU_11N,
	.tx_pkt_desc_sz = 40,
	.tx_buf_desc_sz = 16,
	.rx_pkt_desc_sz = 24,
	.rx_buf_desc_sz = 8,
	.phy_efuse_size = 256,
	.log_efuse_size = 512,
	.ptct_efuse_size = 15,
	.txff_size = 32768,
	.rxff_size = 16384,
	.rsvd_drv_pg_num = 8,
	.band = RTW_BAND_2G,
	.page_size = TX_PAGE_SIZE,
	.csi_buf_pg_num = 0,
	.dig_min = 0x20,
	.txgi_factor = 1,
	.is_pwr_by_rate_dec = true,
	.rx_ldpc = false,
	.tx_stbc = false,
	.max_power_index = 0x3f,
	.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16,
	.usb_tx_agg_desc_num = 1, /* Not sure if this chip has USB interface */
	.hw_feature_report = true,
	.c2h_ra_report_size = 7,
	.old_datarate_fb_limit = true,

	.path_div_supported = false,
	.ht_supported = true,
	.vht_supported = false,
	.lps_deep_mode_supported = 0,

	.sys_func_en = 0xFD,
	.pwr_on_seq = card_enable_flow_8703b,
	.pwr_off_seq = card_disable_flow_8703b,
	.rqpn_table = rqpn_table_8703b,
	.prioq_addrs = &rtw8723x_common.prioq_addrs,
	.page_table = page_table_8703b,
	/* used only in pci.c, not needed for SDIO devices */
	.intf_table = NULL,

	.dig = rtw8723x_common.dig,
	.dig_cck = rtw8723x_common.dig_cck,

	.rf_sipi_addr = {0x840, 0x844},
	.rf_sipi_read_addr = rtw8723x_common.rf_sipi_addr,
	.fix_rf_phy_num = 2,
	.ltecoex_addr = &rtw8723x_common.ltecoex_addr,

	.mac_tbl = &rtw8703b_mac_tbl,
	.agc_tbl = &rtw8703b_agc_tbl,
	.bb_tbl = &rtw8703b_bb_tbl,
	.rf_tbl = {&rtw8703b_rf_a_tbl},

	.rfe_defs = rtw8703b_rfe_defs,
	.rfe_defs_size = ARRAY_SIZE(rtw8703b_rfe_defs),

	.iqk_threshold = 8,

	/* WOWLAN firmware exists, but not implemented yet */
	.wow_fw_name = "rtw88/rtw8703b_wow_fw.bin",
	.wowlan_stub = NULL,
	.max_scan_ie_len = IEEE80211_MAX_DATA_LEN,

	/* Vendor driver has a time-based format, converted from
	 * 20180330
	 */
	.coex_para_ver = 0x0133ed6a,
	.bt_desired_ver = 0x1c,
	.scbd_support = true,
	.new_scbd10_def = true,
	.ble_hid_profile_support = false,
	.wl_mimo_ps_support = false,
	.pstdma_type = COEX_PSTDMA_FORCE_LPSOFF,
	.bt_rssi_type = COEX_BTRSSI_RATIO,
	.ant_isolation = 15,
	.rssi_tolerance = 2,
	.bt_rssi_step = bt_rssi_step_8703b,
	.wl_rssi_step = wl_rssi_step_8703b,
	/* sant -> shared antenna, nsant -> non-shared antenna
	 * Not sure if 8703b versions with non-shard antenna even exist.
	 */
	.table_sant_num = ARRAY_SIZE(table_sant_8703b),
	.table_sant = table_sant_8703b,
	.table_nsant_num = 0,
	.table_nsant = NULL,
	.tdma_sant_num = ARRAY_SIZE(tdma_sant_8703b),
	.tdma_sant = tdma_sant_8703b,
	.tdma_nsant_num = 0,
	.tdma_nsant = NULL,
	.wl_rf_para_num = ARRAY_SIZE(rf_para_tx_8703b),
	.wl_rf_para_tx = rf_para_tx_8703b,
	.wl_rf_para_rx = rf_para_rx_8703b,
	.bt_afh_span_bw20 = 0x20,
	.bt_afh_span_bw40 = 0x30,
	.afh_5g_num = ARRAY_SIZE(afh_5g_8703b),
	.afh_5g = afh_5g_8703b,
	/* REG_BTG_SEL doesn't seem to have a counterpart in the
	 * vendor driver. Mathematically it's REG_PAD_CTRL1 + 3.
	 *
	 * It is used in the cardemu_to_act power sequence by though
	 * (by address, 0x0067), comment: "0x67[0] = 0 to disable
	 * BT_GPS_SEL pins" That seems to fit.
	 */
	.btg_reg = NULL,
	/* These registers are used to read (and print) from if
	 * CONFIG_RTW88_DEBUGFS is enabled.
	 */
	.coex_info_hw_regs_num = 0,
	.coex_info_hw_regs = NULL,
};
EXPORT_SYMBOL(rtw8703b_hw_spec);

MODULE_FIRMWARE("rtw88/rtw8703b_fw.bin");
MODULE_FIRMWARE("rtw88/rtw8703b_wow_fw.bin");

MODULE_AUTHOR("Fiona Klute <fiona.klute@gmx.de>");
MODULE_DESCRIPTION("Realtek 802.11n wireless 8703b driver");
MODULE_LICENSE("Dual BSD/GPL");
