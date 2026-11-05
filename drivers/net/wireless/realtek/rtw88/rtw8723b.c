// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2026  Realtek Corporation
 * Copyright(c) Michael Straube <straube.linux@gmail.com>
 * Copyright(c) 2024-2026 Luka Gejak <luka.gejak@linux.dev>
 */

#include "main.h"
#include "coex.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "rtw8723b.h"
#include "rtw8723b_table.h"
#include "sdio.h"
#include "tx.h"

#define TRANS_SEQ_END			\
	0xFFFF,				\
	RTW_PWR_CUT_ALL_MSK,		\
	RTW_PWR_INTF_ALL_MSK,		\
	0,				\
	RTW_PWR_CMD_END, 0, 0

#define TBTT_PROHIBIT_SETUP_TIME		0x04
#define TBTT_PROHIBIT_HOLD_TIME_STOP_BCN	0x64
#define WLAN_BCN_DMA_TIME			0x02
#define WLAN_BAR_VAL				0x0201ffff
#define WLAN_SLOT_TIME				0x09
#define WLAN_RF_CTRL_ENABLE			(BIT_RF_EN | BIT_RF_RSTB | \
						 BIT_RF_SDM_RSTB)

#define ADDA_ON_VAL_8723B			0x01c00014

/*
 * BIT(7) lets the 8051 control antenna selection, BIT(1) is LED2_CM.
 */
#define WLAN_ANT_SEL			(BIT(7) | BIT(1))

#define IQK_DELAY_TIME_8723B		20

/* rssi in percent (dbm = % - 100); the values are the vendor driver's. */
static const u8 wl_rssi_step_8723b[] = {60, 50, 44, 30};
static const u8 bt_rssi_step_8723b[] = {30, 30, 30, 30};
static const struct coex_5g_afh_map afh_5g_8723b[] = { {0, 0, 0} };

static const struct coex_rf_para rf_para_tx_8723b[] = {
	{0, 0, false, 7},  /* for normal */
	{0, 10, false, 7}, /* for WL-CPT */
	{1, 0, true, 4},
	{1, 2, true, 4},
	{1, 10, true, 4},
	{1, 15, true, 4}
};

static const struct coex_rf_para rf_para_rx_8723b[] = {
	{0, 0, false, 7},  /* for normal */
	{0, 10, false, 7}, /* for WL-CPT */
	{1, 0, true, 5},
	{1, 2, true, 5},
	{1, 10, true, 5},
	{1, 15, true, 5}
};

static_assert(ARRAY_SIZE(rf_para_tx_8723b) == ARRAY_SIZE(rf_para_rx_8723b));

static const u32 rtw8723b_ofdm_swing_table[] = {
	0x0b40002d, /* 0, -15.0dB */
	0x0c000030, /* 1, -14.5dB */
	0x0cc00033, /* 2, -14.0dB */
	0x0d800036, /* 3, -13.5dB */
	0x0e400039, /* 4, -13.0dB */
	0x0f00003c, /* 5, -12.5dB */
	0x10000040, /* 6, -12.0dB */
	0x11000044, /* 7, -11.5dB */
	0x12000048, /* 8, -11.0dB */
	0x1300004c, /* 9, -10.5dB */
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
	0x7f8001fe, /* 42, +6.0dB */
};

/* The eight per-rate CCK transmit power registers are consecutive. */
#define RTW_CCK_SWING_REG_NUM	8

static const u8 rtw8723b_cck_swing_table_ch1_ch13[][8] = {
	{0x09, 0x08, 0x07, 0x06, 0x04, 0x03, 0x01, 0x01},	/* 0, -16.0dB */
	{0x09, 0x09, 0x08, 0x06, 0x05, 0x03, 0x01, 0x01},	/* 1, -15.5dB */
	{0x0a, 0x09, 0x08, 0x07, 0x05, 0x03, 0x02, 0x01},	/* 2, -15.0dB */
	{0x0a, 0x0a, 0x09, 0x07, 0x05, 0x03, 0x02, 0x01},	/* 3, -14.5dB */
	{0x0b, 0x0a, 0x09, 0x08, 0x06, 0x04, 0x02, 0x01},	/* 4, -14.0dB */
	{0x0b, 0x0b, 0x0a, 0x08, 0x06, 0x04, 0x02, 0x01},	/* 5, -13.5dB */
	{0x0c, 0x0c, 0x0a, 0x09, 0x06, 0x04, 0x02, 0x01},	/* 6, -13.0dB */
	{0x0d, 0x0c, 0x0b, 0x09, 0x07, 0x04, 0x02, 0x01},	/* 7, -12.5dB */
	{0x0d, 0x0d, 0x0c, 0x0a, 0x07, 0x05, 0x02, 0x01},	/* 8, -12.0dB */
	{0x0e, 0x0e, 0x0c, 0x0a, 0x08, 0x05, 0x02, 0x01},	/* 9, -11.5dB */
	{0x0f, 0x0f, 0x0d, 0x0b, 0x08, 0x05, 0x03, 0x01},	/* 10, -11.0dB */
	{0x10, 0x10, 0x0e, 0x0b, 0x08, 0x05, 0x03, 0x01},	/* 11, -10.5dB */
	{0x11, 0x11, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01},	/* 12, -10.0dB */
	{0x12, 0x12, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01},	/* 13, -9.5dB */
	{0x13, 0x13, 0x10, 0x0d, 0x0a, 0x06, 0x03, 0x01},	/* 14, -9.0dB */
	{0x14, 0x14, 0x11, 0x0e, 0x0b, 0x07, 0x03, 0x02},	/* 15, -8.5dB */
	{0x16, 0x15, 0x12, 0x0f, 0x0b, 0x07, 0x04, 0x01},	/* 16, -8.0dB */
	{0x17, 0x16, 0x13, 0x10, 0x0c, 0x08, 0x04, 0x02},	/* 17, -7.5dB */
	{0x18, 0x17, 0x15, 0x11, 0x0c, 0x08, 0x04, 0x02},	/* 18, -7.0dB */
	{0x1a, 0x19, 0x16, 0x12, 0x0d, 0x09, 0x04, 0x02},	/* 19, -6.5dB */
	{0x1b, 0x1a, 0x17, 0x13, 0x0e, 0x09, 0x04, 0x02},	/* 20, -6.0dB */
	{0x1d, 0x1c, 0x18, 0x14, 0x0f, 0x0a, 0x05, 0x02},	/* 21, -5.5dB */
	{0x1f, 0x1e, 0x1a, 0x15, 0x10, 0x0a, 0x05, 0x02},	/* 22, -5.0dB */
	{0x20, 0x20, 0x1b, 0x16, 0x11, 0x08, 0x05, 0x02},	/* 23, -4.5dB */
	{0x22, 0x21, 0x1d, 0x18, 0x11, 0x0b, 0x06, 0x02},	/* 24, -4.0dB */
	{0x24, 0x23, 0x1f, 0x19, 0x13, 0x0c, 0x06, 0x03},	/* 25, -3.5dB */
	{0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03},	/* 26, -3.0dB */
	{0x28, 0x28, 0x22, 0x1c, 0x15, 0x0d, 0x07, 0x03},	/* 27, -2.5dB */
	{0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03},	/* 28, -2.0dB */
	{0x2d, 0x2d, 0x27, 0x1f, 0x18, 0x0f, 0x08, 0x03},	/* 29, -1.5dB */
	{0x30, 0x2f, 0x29, 0x21, 0x19, 0x10, 0x08, 0x03},	/* 30, -1.0dB */
	{0x33, 0x32, 0x2b, 0x23, 0x1a, 0x11, 0x08, 0x04},	/* 31, -0.5dB */
	{0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04},	/* 32, +0dB */
};

static const u8 rtw8723b_cck_swing_table_ch14[][8] = {
	{0x09, 0x08, 0x07, 0x04, 0x00, 0x00, 0x00, 0x00},	/* 0, -16.0dB */
	{0x09, 0x09, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00},	/* 1, -15.5dB */
	{0x0a, 0x09, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00},	/* 2, -15.0dB */
	{0x0a, 0x0a, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00},	/* 3, -14.5dB */
	{0x0b, 0x0a, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00},	/* 4, -14.0dB */
	{0x0b, 0x0b, 0x0a, 0x06, 0x00, 0x00, 0x00, 0x00},	/* 5, -13.5dB */
	{0x0c, 0x0c, 0x0a, 0x06, 0x00, 0x00, 0x00, 0x00},	/* 6, -13.0dB */
	{0x0d, 0x0c, 0x0b, 0x06, 0x00, 0x00, 0x00, 0x00},	/* 7, -12.5dB */
	{0x0d, 0x0d, 0x0c, 0x07, 0x00, 0x00, 0x00, 0x00},	/* 8, -12.0dB */
	{0x0e, 0x0e, 0x0c, 0x07, 0x00, 0x00, 0x00, 0x00},	/* 9, -11.5dB */
	{0x0f, 0x0f, 0x0d, 0x08, 0x00, 0x00, 0x00, 0x00},	/* 10, -11.0dB */
	{0x10, 0x10, 0x0e, 0x08, 0x00, 0x00, 0x00, 0x00},	/* 11, -10.5dB */
	{0x11, 0x11, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00},	/* 12, -10.0dB */
	{0x12, 0x12, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00},	/* 13, -9.5dB */
	{0x13, 0x13, 0x10, 0x0a, 0x00, 0x00, 0x00, 0x00},	/* 14, -9.0dB */
	{0x14, 0x14, 0x11, 0x0a, 0x00, 0x00, 0x00, 0x00},	/* 15, -8.5dB */
	{0x16, 0x15, 0x12, 0x0b, 0x00, 0x00, 0x00, 0x00},	/* 16, -8.0dB */
	{0x17, 0x16, 0x13, 0x0b, 0x00, 0x00, 0x00, 0x00},	/* 17, -7.5dB */
	{0x18, 0x17, 0x15, 0x0c, 0x00, 0x00, 0x00, 0x00},	/* 18, -7.0dB */
	{0x1a, 0x19, 0x16, 0x0d, 0x00, 0x00, 0x00, 0x00},	/* 19, -6.5dB */
	{0x1b, 0x1a, 0x17, 0x0e, 0x00, 0x00, 0x00, 0x00},	/* 20, -6.0dB */
	{0x1d, 0x1c, 0x18, 0x0e, 0x00, 0x00, 0x00, 0x00},	/* 21, -5.5dB */
	{0x1f, 0x1e, 0x1a, 0x0f, 0x00, 0x00, 0x00, 0x00},	/* 22, -5.0dB */
	{0x20, 0x20, 0x1b, 0x10, 0x00, 0x00, 0x00, 0x00},	/* 23, -4.5dB */
	{0x22, 0x21, 0x1d, 0x11, 0x00, 0x00, 0x00, 0x00},	/* 24, -4.0dB */
	{0x24, 0x23, 0x1f, 0x12, 0x00, 0x00, 0x00, 0x00},	/* 25, -3.5dB */
	{0x26, 0x25, 0x21, 0x13, 0x00, 0x00, 0x00, 0x00},	/* 26, -3.0dB */
	{0x28, 0x28, 0x24, 0x14, 0x00, 0x00, 0x00, 0x00},	/* 27, -2.5dB */
	{0x2b, 0x2a, 0x25, 0x15, 0x00, 0x00, 0x00, 0x00},	/* 28, -2.0dB */
	{0x2d, 0x2d, 0x17, 0x17, 0x00, 0x00, 0x00, 0x00},	/* 29, -1.5dB */
	{0x30, 0x2f, 0x29, 0x18, 0x00, 0x00, 0x00, 0x00},	/* 30, -1.0dB */
	{0x33, 0x32, 0x2b, 0x19, 0x00, 0x00, 0x00, 0x00},	/* 31, -0.5dB */
	{0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00},	/* 32, +0dB */
};

static_assert(ARRAY_SIZE(rtw8723b_cck_swing_table_ch1_ch13) ==
	      ARRAY_SIZE(rtw8723b_cck_swing_table_ch14));

#define RTW_OFDM_SWING_TABLE_SIZE	ARRAY_SIZE(rtw8723b_ofdm_swing_table)
#define RTW_CCK_SWING_TABLE_SIZE	ARRAY_SIZE(rtw8723b_cck_swing_table_ch14)

/*
 * Swing index field of REG_OFDM_0_XA_TX_IQ_IMBALANCE; the table entries
 * below hold the full register value, so read them back with
 * u32_get_bits().
 */
#define OFDM_SWING_MASK			GENMASK(31, 22)

static const struct rtw_pwr_seq_cmd trans_pre_enable_8723b[] = {
	/* unlock ISO/CLK/power control register */
	{REG_RSV_CTRL,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xff, 0},
	{TRANS_SEQ_END},
};

static const struct rtw_pwr_seq_cmd trans_carddis_to_cardemu_8723b[] = {
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(7), 0},
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_POLLING, BIT(1), BIT(1)},
	{0x004A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4), 0},
	{0x0023,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), 0},
	{0x0301,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{TRANS_SEQ_END},
};

static const struct rtw_pwr_seq_cmd trans_cardemu_to_act_8723b[] = {
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
	 RTW_PWR_CMD_WRITE, BIT(4) | BIT(3), 0},
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

static const struct rtw_pwr_seq_cmd trans_act_to_lps_8723b[] = {
	{0x0301,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0xFF},
	{0x0522,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0xFF},
	{0x05F8,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xFF, 0},
	{0x05F9,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xFF, 0},
	{0x05FA,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xFF, 0},
	{0x05FB,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xFF, 0},
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
	 RTW_PWR_CMD_WRITE, 0xFF, 0x03},
	{0x0101,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0093,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x00},
	{0x0553,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{TRANS_SEQ_END},
};

static const struct rtw_pwr_seq_cmd trans_act_to_reset_mcu_8723b[] = {
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

static const struct rtw_pwr_seq_cmd trans_act_to_cardemu_8723b[] = {
	{0x001F,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
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

static const struct rtw_pwr_seq_cmd trans_cardemu_to_carddis_8723b[] = {
	{0x0007,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x20},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), BIT(2)},
	{0x004A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 1},
	{0x0023,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), BIT(4)},
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_POLLING, BIT(1), 0},
	{TRANS_SEQ_END},
};

static const struct rtw_pwr_seq_cmd * const card_enable_flow_8723b[] = {
	trans_pre_enable_8723b,
	trans_carddis_to_cardemu_8723b,
	trans_cardemu_to_act_8723b,
	NULL
};

static const struct rtw_pwr_seq_cmd * const card_disable_flow_8723b[] = {
	trans_act_to_lps_8723b,
	trans_act_to_reset_mcu_8723b,
	trans_act_to_cardemu_8723b,
	trans_cardemu_to_carddis_8723b,
	NULL
};

static const struct rtw_page_table page_table_8723b[] = {
	{12, 2, 2, 0, 1}, /* SDIO */
	{12, 2, 2, 0, 1},
	{12, 2, 2, 0, 1},
	{12, 2, 2, 0, 1},
	{12, 2, 2, 0, 1},
};

static const struct rtw_rqpn rqpn_table_8723b[] = {
	/* SDIO maps VO, MGMT and HI to the high queue. */
	{RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
	/* PCIE */
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
	/* USB bulkout 2 */
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_HIGH,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
	/* USB bulkout 3 */
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
	/* USB bulkout 4 */
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
};

static const u8 rtw8723b_pwrtrk_2gb_n[] = {
	0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 6, 6,
	7, 7, 7, 8, 8, 9, 9, 10, 10, 11, 12, 13, 14, 15
};

static const u8 rtw8723b_pwrtrk_2gb_p[] = {
	0, 0, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8,
	9, 9, 10, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15, 15
};

static const u8 rtw8723b_pwrtrk_2ga_n[] = {
	0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 6, 6,
	7, 7, 7, 8, 8, 9, 9, 10, 10, 11, 12, 13, 14, 15
};

static const u8 rtw8723b_pwrtrk_2ga_p[] = {
	0, 0, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8,
	9, 9, 10, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15, 15
};

static const u8 rtw8723b_pwrtrk_2g_cck_b_n[] = {
	0, 0, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 7, 8,
	8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15
};

static const u8 rtw8723b_pwrtrk_2g_cck_b_p[] = {
	0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7,
	8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15
};

static const u8 rtw8723b_pwrtrk_2g_cck_a_n[] = {
	0, 0, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 7, 8,
	8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15
};

static const u8 rtw8723b_pwrtrk_2g_cck_a_p[] = {
	0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7,
	8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15
};

static const struct rtw_pwr_track_tbl rtw8723b_rtw_pwr_track_tbl = {
	.pwrtrk_2gb_n = rtw8723b_pwrtrk_2gb_n,
	.pwrtrk_2gb_p = rtw8723b_pwrtrk_2gb_p,
	.pwrtrk_2ga_n = rtw8723b_pwrtrk_2ga_n,
	.pwrtrk_2ga_p = rtw8723b_pwrtrk_2ga_p,
	.pwrtrk_2g_cckb_n = rtw8723b_pwrtrk_2g_cck_b_n,
	.pwrtrk_2g_cckb_p = rtw8723b_pwrtrk_2g_cck_b_p,
	.pwrtrk_2g_ccka_n = rtw8723b_pwrtrk_2g_cck_a_n,
	.pwrtrk_2g_ccka_p = rtw8723b_pwrtrk_2g_cck_a_p,
	/* rtw8723x_pwrtrack_set_xtal() is not used on this chip. */
	.pwrtrk_xtal_n = NULL,
	.pwrtrk_xtal_p = NULL,
};

static const struct rtw_rfe_def rtw8723b_rfe_defs[] = {
	[0] = { .phy_pg_tbl	= &rtw8723b_bb_pg_tbl,
		.txpwr_lmt_tbl	= &rtw8723b_txpwr_lmt_tbl,
		.pwr_track_tbl	= &rtw8723b_rtw_pwr_track_tbl, },
};

/* Shared-Antenna Coex Table */
static const struct coex_table_para table_sant_8723b[] = {
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

/* Non-Shared-Antenna Coex Table */
static const struct coex_table_para table_nsant_8723b[] = {
	{0xffffffff, 0xffffffff}, /* case-100 */
	{0x55555555, 0x55555555},
	{0x66555555, 0x66555555},
	{0xaaaaaaaa, 0xaaaaaaaa},
	{0x5a5a5a5a, 0x5a5a5a5a},
	{0xfafafafa, 0xfafafafa}, /* case-105 */
	{0x5afa5afa, 0x5afa5afa},
	{0x55555555, 0xfafafafa},
	{0x66555555, 0xfafafafa},
	{0x66555555, 0x5a5a5a5a},
	{0x66555555, 0x6a5a5a5a}, /* case-110 */
	{0x66555555, 0xaaaaaaaa},
	{0xffff55ff, 0xfafafafa},
	{0xffff55ff, 0x5afa5afa},
	{0xffff55ff, 0xaaaaaaaa},
	{0xffff55ff, 0xffff55ff}, /* case-115 */
	{0xaaffffaa, 0x5afa5afa},
	{0xaaffffaa, 0xaaaaaaaa},
	{0xffffffff, 0xfafafafa},
	{0xffffffff, 0x5afa5afa},
	{0xffffffff, 0xaaaaaaaa}, /* case-120 */
	{0x55ff55ff, 0x5afa5afa},
	{0x55ff55ff, 0xaaaaaaaa},
	{0x55ff55ff, 0x55ff55ff}
};

/* Shared-Antenna TDMA */
static const struct coex_tdma_para tdma_sant_8723b[] = {
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
	{ {0x61, 0x08, 0x03, 0x11, 0x11} }
};

/* Non-Shared-Antenna TDMA */
static const struct coex_tdma_para tdma_nsant_8723b[] = {
	{ {0x00, 0x00, 0x00, 0x00, 0x01} }, /* case-100 */
	{ {0x61, 0x45, 0x03, 0x11, 0x11} }, /* case-101 */
	{ {0x61, 0x3a, 0x03, 0x11, 0x11} },
	{ {0x61, 0x30, 0x03, 0x11, 0x11} },
	{ {0x61, 0x20, 0x03, 0x11, 0x11} },
	{ {0x61, 0x10, 0x03, 0x11, 0x11} }, /* case-105 */
	{ {0x61, 0x45, 0x03, 0x11, 0x10} },
	{ {0x61, 0x3a, 0x03, 0x11, 0x10} },
	{ {0x61, 0x30, 0x03, 0x11, 0x10} },
	{ {0x61, 0x20, 0x03, 0x11, 0x10} },
	{ {0x61, 0x10, 0x03, 0x11, 0x10} }, /* case-110 */
	{ {0x61, 0x08, 0x03, 0x11, 0x14} },
	{ {0x61, 0x08, 0x03, 0x10, 0x14} },
	{ {0x51, 0x08, 0x03, 0x10, 0x54} },
	{ {0x51, 0x08, 0x03, 0x10, 0x55} },
	{ {0x51, 0x08, 0x07, 0x10, 0x54} }, /* case-115 */
	{ {0x51, 0x45, 0x03, 0x10, 0x50} },
	{ {0x51, 0x3a, 0x03, 0x10, 0x50} },
	{ {0x51, 0x30, 0x03, 0x10, 0x50} },
	{ {0x51, 0x20, 0x03, 0x10, 0x50} },
	{ {0x51, 0x10, 0x03, 0x10, 0x50} }, /* case-120 */
	{ {0x51, 0x08, 0x03, 0x10, 0x50} }
};

static void rtw8723b_efuse_grant(struct rtw_dev *rtwdev, bool on)
{
	rtw_write8_set(rtwdev, REG_BT_EFUSE_CTRL,
		       on ? BIT_BT_PWR_CUT : BIT_BT_OUT_ISO);

	rtw8723x_efuse_grant(rtwdev, on);
}

static u8 rtw8723b_default_ofdm_index(struct rtw_dev *rtwdev)
{
	u32 swing;
	u8 i;

	swing = rtw_read32_mask(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE,
				OFDM_SWING_MASK);

	for (i = 0; i < RTW_OFDM_SWING_TABLE_SIZE; i++) {
		if (u32_get_bits(rtw8723b_ofdm_swing_table[i],
				 OFDM_SWING_MASK) == swing)
			break;
	}

	if (i >= RTW_OFDM_SWING_TABLE_SIZE)
		i = 30; /* 30, +0dB */

	return i;
}

static u8 rtw8723b_default_cck_index(struct rtw_dev *rtwdev)
{
	const u8 (*table)[RTW_CCK_SWING_REG_NUM];
	u8 swing;
	u8 i;

	table = rtw8723b_cck_swing_table_ch1_ch13;
	if (rtwdev->hal.current_channel == 14)
		table = rtw8723b_cck_swing_table_ch14;

	swing = rtw_read8(rtwdev, REG_CCK_SWING);

	for (i = 0; i < RTW_CCK_SWING_TABLE_SIZE; i++) {
		if (table[i][0] == swing)
			break;
	}

	if (i >= RTW_CCK_SWING_TABLE_SIZE)
		i = 20; /* 20, -6.0dB */

	return i;
}

static void rtw8723b_pwrtrack_init(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 path;

	dm_info->default_ofdm_index = rtw8723b_default_ofdm_index(rtwdev);
	dm_info->default_cck_index = rtw8723b_default_cck_index(rtwdev);

	/* Clear the OFDM remnant per path: tracking keeps one per path. */
	for (path = RF_PATH_A; path < rtwdev->hal.rf_path_num; path++) {
		ewma_thermal_init(&dm_info->avg_thermal[path]);
		dm_info->delta_power_index[path] = 0;
		dm_info->txagc_remnant_ofdm[path] = 0;
	}
	dm_info->pwr_trk_triggered = false;
	dm_info->pwr_trk_init_trigger = true;
	dm_info->thermal_meter_k = rtwdev->efuse.thermal_meter_k;
	dm_info->txagc_remnant_cck = 0;
}

static void rtw8723b_post_enable_flow(struct rtw_dev *rtwdev)
{
	/*
	 * Enable falling edge triggered interrupts and GPIO9 interrupt mode.
	 * The power-on sequence sets both, but it runs before the firmware is
	 * downloaded, so reassert them here as the vendor driver does.
	 */
	rtw_write32_set(rtwdev, REG_GPIO_INTM, BIT(9));
	rtw_write32_set(rtwdev, REG_GPIO_PIN_CTRL_2, BIT(25));

	rtw_write16_set(rtwdev, REG_APS_FSMCO, BIT_EN_PDN);

	/*
	 * Only OR in the missing enables: writing CR to zero would reset the
	 * hardware free-page counters and leave TX DMA no pages to allocate.
	 */
	rtw_write16_set(rtwdev, REG_CR, MAC_TRX_ENABLE | BIT_MAC_SEC_EN |
			BIT_32K_CAL_TMR_EN);

	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_SDIO)
		rtw_write16_set(rtwdev, REG_PWR_DATA,
				BIT_EEPRPAD_RFE_CTRL_EN);

	rtw_write8(rtwdev, REG_EARLY_MODE_CONTROL, 0);

	/* Keep every MACID eligible for firmware-scheduled TX at power-on. */
	rtw_write32(rtwdev, REG_MACID_PKT_DROP0, 0);
	rtw_write32(rtwdev, REG_MACID_PKT_SLEEP, 0);
}

static void rtw8723b_phy_bb_config(struct rtw_dev *rtwdev)
{
	u8 xtal_cap;

	/* Enable BB and RF */
	rtw_write16_set(rtwdev, REG_SYS_FUNC_EN,
			BIT_FEN_EN_25_1 | BIT_FEN_BB_GLB_RST | BIT_FEN_BB_RSTB);

	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_USB)
		rtw_write32(rtwdev, REG_BB_SEL_BTG, 0x0);
	else
		rtw_write32(rtwdev, REG_BB_SEL_BTG, 0x280);

	/* Full write: preserving spuriously set bits can wedge the RF bus. */
	rtw_write8(rtwdev, REG_RF_CTRL, WLAN_RF_CTRL_ENABLE);
	fsleep(1000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_WLINT, RFREG_MASK, 0x0780);
	rtw_write8(rtwdev, REG_SYS_FUNC_EN,
		   BIT_FEN_PPLL | BIT_FEN_PCIEA | BIT_FEN_DIO_PCIE |
		   BIT_FEN_BB_GLB_RST | BIT_FEN_BB_RSTB);
	rtw_write8(rtwdev, REG_AFE_CTRL1 + 1, 0x80);

	xtal_cap = rtwdev->efuse.crystal_cap & 0x3f;
	rtw_write32_mask(rtwdev,  REG_AFE_CTRL3, BIT_MASK_XTAL,
			 xtal_cap | (xtal_cap << 6));
}

static void rtw8723b_phy_load_bb_tables(struct rtw_dev *rtwdev)
{
	const struct rtw_rfe_def *rfe_def = rtw_get_rfe_def(rtwdev);
	const struct rtw_chip_info *chip = rtwdev->chip;

	rtw_load_table(rtwdev, chip->bb_tbl);
	rtw_load_table(rtwdev, chip->agc_tbl);
	if (rfe_def && rfe_def->agc_btg_tbl)
		rtw_load_table(rtwdev, rfe_def->agc_btg_tbl);
}

static void rtw8723b_phy_rf6052_config(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u32 intf_s, intf_oe, hssi_2;
	u32 val32, mask;
	u8 path;

	for (path = RF_PATH_A; path < hal->rf_path_num; path++) {
		switch (path) {
		case RF_PATH_A:
			intf_s = REG_FPGA0_XA_RF_SW_CTRL;
			intf_oe = REG_FPGA0_XA_RF_INT_OE;
			hssi_2 = REG_FPGA0_XA_HSSI_PARM2;
			mask = RFSI_RFENV;
			break;
		case RF_PATH_B:
			/*
			 * The path B switch control is the upper half of the
			 * word at REG_FPGA0_XA_RF_SW_CTRL, which the shifted
			 * mask already selects. Addressing 0x0872 directly
			 * would be a 32-bit access on a 2-byte boundary and
			 * would reach into the register that follows.
			 */
			intf_s = REG_FPGA0_XA_RF_SW_CTRL;
			intf_oe = REG_FPGA0_XB_RF_INT_OE;
			hssi_2 = REG_FPGA0_XB_HSSI_PARM2;
			mask = RFSI_RFENV << 16;
			break;
		default:
			rtw_err(rtwdev, "invalid rf path %c\n", path + 'A');
			return;
		}

		val32 = rtw_read32_mask(rtwdev, intf_s, mask);

		rtw_write32_mask(rtwdev, intf_oe, RFSI_RFENV << 16, 0x1);
		udelay(1);

		rtw_write32_mask(rtwdev, intf_oe, RFSI_RFENV, 0x1);
		udelay(1);

		rtw_write32_mask(rtwdev, hssi_2, HSSI_3WIRE_ADDR_LEN, 0x0);
		udelay(1);

		rtw_write32_mask(rtwdev, hssi_2, HSSI_3WIRE_DATA_LEN, 0x0);
		udelay(1);

		/* Path B has no table of its own; radio_a serves both paths. */
		rtw_load_table(rtwdev, rtwdev->chip->rf_tbl[RF_PATH_A]);

		rtw_write32_mask(rtwdev, intf_s, mask, val32);
	}
}

static void rtw8723b_phy_lck(struct rtw_dev *rtwdev)
{
	rtw_write_rf(rtwdev, RF_PATH_A, RF_SYN_PFD, RFREG_MASK, 0xdfbe0);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK, 0x8c01);
	fsleep(200 * 1000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_SYN_PFD, RFREG_MASK, 0xdffe0);
}

static void rtw8723b_lck(struct rtw_dev *rtwdev)
{
	/*
	 * The vendor's LC calibration turns the PFD LDO on before the LCK
	 * trigger and off after it, every time it runs, not only during the
	 * initial RF configuration.
	 */
	rtw_write_rf(rtwdev, RF_PATH_A, RF_SYN_PFD, RFREG_MASK, 0xdfbe0);

	rtw8723x_lck(rtwdev);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_SYN_PFD, RFREG_MASK, 0xdffe0);
}

static void rtw8723b_phy_rf_config(struct rtw_dev *rtwdev)
{
	rtw8723b_phy_rf6052_config(rtwdev);

	/* LCK must run as part of the RF configuration. */
	rtw8723b_phy_lck(rtwdev);
}

static void rtw8723b_init_transfer_page_size(struct rtw_dev *rtwdev)
{
	rtw_write8(rtwdev, REG_PBP,
		   u8_encode_bits(PBP_128, PBP_RX_MASK) |
		   u8_encode_bits(PBP_128, PBP_TX_MASK));
}

static void rtw8723b_init_adaptive_ctrl(struct rtw_dev *rtwdev)
{
	/*
	 * Firmware validates RRSR at init: narrowing it to the mandatory
	 * rates makes it drop all management TX, so keep the full set.
	 */
	rtw_write32_mask(rtwdev, REG_RRSR, 0xfffff, 0xffff1);
	rtwdev->dm_info.rrsr_val_init = 0xffff1;
	rtw_write16(rtwdev, REG_RETRY_LIMIT, 0x3030);
}

static void rtw8723b_init_edca(struct rtw_dev *rtwdev)
{
	rtw_write16(rtwdev, REG_SPEC_SIFS, 0x100a);
	rtw_write16(rtwdev, REG_MAC_SPEC_SIFS, 0x100a);
	rtw_write16(rtwdev, REG_SIFS, 0x100a);
	rtw_write16(rtwdev, REG_SIFS + 2, 0x100a);

	/*
	 * RESP_SIFS must leave the AP enough slack, or it times out before the
	 * ACK reaches the air and the unicast handshake never completes.
	 */
	rtw_write16(rtwdev, REG_RESP_SIFS_CCK, 0x0808);
	rtw_write16(rtwdev, REG_RESP_SIFS_OFDM, 0x0a0a);

	/* TXOP */
	rtw_write32(rtwdev, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtw_write32(rtwdev, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtw_write32(rtwdev, REG_EDCA_VI_PARAM, 0x005EA324);
	rtw_write32(rtwdev, REG_EDCA_VO_PARAM, 0x002FA226);
}

static void rtw8723b_init_beacon_parameters(struct rtw_dev *rtwdev)
{
	/*
	 * Both ports get DIS_TSF_UDT and EN_BCN_FUNCTION only; DIS_BCNQ_SUB
	 * belongs to AP/IBSS and must stay clear for station mode.
	 */
	rtw_write16(rtwdev, REG_BCN_CTRL,
		    (BIT_DIS_TSF_UDT | BIT_EN_BCN_FUNCTION) |
		    ((BIT_DIS_TSF_UDT | BIT_EN_BCN_FUNCTION) << 8));
	rtw_write8(rtwdev, REG_TBTT_PROHIBIT, TBTT_PROHIBIT_SETUP_TIME);
	rtw_write8(rtwdev, REG_TBTT_PROHIBIT + 1,
		   TBTT_PROHIBIT_HOLD_TIME_STOP_BCN & 0xff);
	rtw_write8(rtwdev, REG_TBTT_PROHIBIT + 2,
		   (rtw_read8(rtwdev, REG_TBTT_PROHIBIT + 2) & 0xf0) |
		   (TBTT_PROHIBIT_HOLD_TIME_STOP_BCN >> 8));

	rtw_write8(rtwdev, REG_BCNDMATIM, WLAN_BCN_DMA_TIME);
	/* Largest beacon AIFS: the chip does not contend before beaconing. */
	rtw_write16(rtwdev, REG_BCNTCFG, 0x660F);
}

static void rtw8723b_init_antenna_selection(struct rtw_dev *rtwdev)
{
	rtw_write8(rtwdev, REG_LEDCFG2, WLAN_ANT_SEL);
}

static void rtw8723b_inform_rfk_status(struct rtw_dev *rtwdev, bool start)
{
	u8 val8;
	int ret;

	rtw_fw_inform_rfk_status(rtwdev, start);

	if (!start)
		return;

	ret = read_poll_timeout(rtw_read8, val8,
				val8 & BIT_RFK_FW_ACK,
				50000, 400000, false,
				rtwdev, REG_RFK_FW_ACK);
	if (ret)
		rtw_warn(rtwdev, "failed to poll firmware RFK start ack\n");
}

static void rtw8723b_phy_set_param(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	u32 val32;

	rtw8723b_post_enable_flow(rtwdev);

	rtw_load_table(rtwdev, chip->mac_tbl);
	rtw8723b_phy_bb_config(rtwdev);
	rtw8723b_phy_load_bb_tables(rtwdev);
	rtw8723b_phy_rf_config(rtwdev);

	/* enable CCK and OFDM block */
	rtw_write32_set(rtwdev, REG_FPGA0_RFMOD, BIT_CCKEN | BIT_OFDMEN);

	rtw8723b_init_transfer_page_size(rtwdev);

	/*
	 * The MAC table overwrites REG_MAR with 0xff, which would restrict
	 * multicast reception; accept every multicast address instead.
	 */
	rtw_write32(rtwdev, REG_MAR, 0xffffffff);
	rtw_write32(rtwdev, REG_MAR + 4, 0xffffffff);

	rtw8723b_init_adaptive_ctrl(rtwdev);
	rtw8723b_init_edca(rtwdev);

	rtw_write8_set(rtwdev, REG_FWHW_TXQ_CTRL, BIT(7));
	rtw_write8(rtwdev, REG_ACKTO, 0x40);

	/* sdio.c sets the DMA mode, but not the burst parameters. */
	rtw_write8(rtwdev, REG_RXDMA_MODE,
		   BIT_DMA_MODE |
		   FIELD_PREP_CONST(BIT_MASK_AGG_BURST_NUM, AGG_BURST_NUM) |
		   FIELD_PREP_CONST(BIT_MASK_AGG_BURST_SIZE, AGG_BURST_SIZE));

	rtw8723b_init_beacon_parameters(rtwdev);

	/* configure packet burst */
	rtw_write8_set(rtwdev, REG_SINGLE_AMPDU_CTRL, BIT_EN_SINGLE_APMDU);
	rtw_write8(rtwdev, REG_RX_PKT_LIMIT, 0x18);
	rtw_write8(rtwdev, REG_MAX_AGGR_NUM, 0x1F);
	rtw_write8(rtwdev, REG_PIFS, 0x00);
	rtw_write8_clr(rtwdev, REG_FWHW_TXQ_CTRL, BIT(7));
	rtw_write8(rtwdev, REG_AMPDU_MAX_TIME, 0x70);

	/*
	 * 256 ms per-AC lifetime: the ROM default of ~1 s lets the chip retry
	 * data frames past the EAPOL retry window and reorders the connect.
	 */
	rtw_write16(rtwdev, REG_PKT_VO_VI_LIFE_TIME, 0x0400);
	rtw_write16(rtwdev, REG_PKT_BE_BK_LIFE_TIME, 0x0400);

	rtw_write8(rtwdev, REG_SLOT, WLAN_SLOT_TIME);

	/* disable BAR */
	rtw_write32(rtwdev, REG_BAR_MODE_CTRL, WLAN_BAR_VAL);

	/* Enable hardware sequence numbering for all queues. */
	rtw_write8(rtwdev, REG_HWSEQ_CTRL, 0xff);

	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_SDIO) {
		/*
		 * Enable Rx DMA timer masking: clear 0x0[2:0] and
		 * 0x2[15:0] only.
		 */
		val32 = rtw_read32(rtwdev, REG_SDIO_TX_CTRL);
		val32 &= 0x0000fff8;
		rtw_write32(rtwdev, REG_SDIO_TX_CTRL, val32);
	}

	rtw_write16(rtwdev, REG_ATIMWND, 0x2);

	rtw8723b_init_antenna_selection(rtwdev);

	rtw_write8_set(rtwdev, REG_CR, BIT_MACTXEN | BIT_MACRXEN);

	rtw_write8(rtwdev, REG_NAV_UPPER, 0xeb); /* ((30000 + 128 - 1) / 128) */

	/* ack for xmit mgmt frames */
	rtw_write32_set(rtwdev, REG_FWHW_TXQ_CTRL, BIT(12));

	rtw_phy_init(rtwdev);

	rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, 0x50);
	rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, 0x20);

	rtw8723b_pwrtrack_init(rtwdev);
}

static u32 rtw8723b_iqk_ant_switch_path(struct rtw_dev *rtwdev)
{
	bool shared_ant = rtwdev->efuse.bt_setting & BIT(0);
	bool ant_path_b;

	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_USB)
		ant_path_b = shared_ant;
	else
		ant_path_b = rtwdev->efuse.bt_setting & BIT(6);

	if (!shared_ant || !ant_path_b)
		return 0x0;

	return 0x280;
}

static void rtw8723b_set_channel_rf(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
	u32 rf_cfgch;

	rf_cfgch = rtw_read_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK);

	u32p_replace_bits(&rf_cfgch, channel, RFCFGCH_CHANNEL_MASK);

	rf_cfgch &= ~RFCFGCH_BW_MASK;
	rf_cfgch |= bw == RTW_CHANNEL_WIDTH_40 ? RFCFGCH_BW_40M :
						 RFCFGCH_BW_20M;

	/* The vendor driver programs both paths from the path A value. */
	rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK, rf_cfgch);
	if (rtwdev->hal.rf_path_num > 1)
		rtw_write_rf(rtwdev, RF_PATH_B, RF_CFGCH, RFREG_MASK, rf_cfgch);
}

static void rtw8723b_set_channel_bb(struct rtw_dev *rtwdev, u8 bw,
				    u8 primary_ch_idx)
{
	switch (bw) {
	case RTW_CHANNEL_WIDTH_20:
		rtw_write32_mask(rtwdev, REG_FPGA0_RFMOD, BIT_MASK_RFMOD, 0x0);
		rtw_write32_mask(rtwdev, REG_FPGA1_RFMOD, BIT_MASK_RFMOD, 0x0);
		rtw_write32_mask(rtwdev, REG_OFDM0_TX_PSD_NOISE,
				 GENMASK(31, 30), 0x0);
		break;
	case RTW_CHANNEL_WIDTH_40:
		rtw_write32_mask(rtwdev, REG_FPGA0_RFMOD, BIT_MASK_RFMOD, 0x1);
		rtw_write32_mask(rtwdev, REG_FPGA1_RFMOD, BIT_MASK_RFMOD, 0x1);
		rtw_write32_mask(rtwdev, REG_CCK0_SYS, BIT_CCK_SIDE_BAND,
				 primary_ch_idx == RTW_SC_20_UPPER ? 1 : 0);
		rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTD_11N, 0xc00,
				 primary_ch_idx == RTW_SC_20_UPPER ? 2 : 1);
		rtw_write32_mask(rtwdev, REG_BB_PWR_SAV5_11N, GENMASK(27, 26),
				 primary_ch_idx == RTW_SC_20_UPPER ? 1 : 2);
		break;
	default:
		break;
	}
}

static void rtw8723b_set_channel(struct rtw_dev *rtwdev, u8 channel,
				 u8 bw, u8 primary_chan_idx)
{
	rtw8723b_set_channel_rf(rtwdev, channel, bw);
	rtw_set_channel_mac(rtwdev, channel, bw, primary_chan_idx);
	rtw8723b_set_channel_bb(rtwdev, bw, primary_chan_idx);
}

static s8 rtw8723b_cck_rx_power(u8 lna_idx, u8 vga_idx)
{
	s8 rx_power = 0;

	switch (lna_idx) {
	case 6:
		rx_power = -40 - (2 * vga_idx);
		break;
	case 4:
		rx_power = -20 - (2 * vga_idx);
		break;
	case 1:
		rx_power = 0 - (2 * vga_idx);
		break;
	case 0:
		rx_power = 10 - (2 * vga_idx);
		break;
	default:
		break;
	}

	return rx_power;
}

static void rtw8723b_query_phy_status_cck(struct rtw_dev *rtwdev, void *phy_raw,
					  struct rtw_rx_pkt_stat *pkt_stat)
{
	struct phy_status_8723x *phy_status = phy_raw;
	u8 lna_idx = u8_get_bits(phy_status->cck_agc_rpt_ofdm_cfosho_a, 0xE0);
	u8 vga_idx = u8_get_bits(phy_status->cck_agc_rpt_ofdm_cfosho_a, 0x1F);
	s8 rx_power = rtw8723b_cck_rx_power(lna_idx, vga_idx);
	s8 min_rx_power = -120;

	pkt_stat->bw = RTW_CHANNEL_WIDTH_20;

	pkt_stat->rx_power[RF_PATH_A] = rx_power;
	pkt_stat->rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power, 1);
	pkt_stat->signal_power = max(pkt_stat->rx_power[RF_PATH_A],
				     min_rx_power);
	rtwdev->dm_info.rssi[RF_PATH_A] = pkt_stat->rssi;
}

static void rtw8723b_query_phy_status_ofdm(struct rtw_dev *rtwdev, void *phy_raw,
					   struct rtw_rx_pkt_stat *pkt_stat)
{
	struct phy_status_8723x *phy_status = phy_raw;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s8 val_s8;

	/* pkt_stat->bw comes from the RX descriptor, not the PHY status. */

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
	dm_info->cfo_tail[RF_PATH_A] = (pkt_stat->cfo_tail[RF_PATH_A] * 5) >> 1;

	val_s8 = (s8)pkt_stat->rx_evm[RF_PATH_A];
	val_s8 = clamp_t(s8, -val_s8 >> 1, 0, 64);
	val_s8 &= 0x3F; /* 64->0: second path of 1SS rate is 64 */
	dm_info->rx_evm_dbm[RF_PATH_A] = val_s8;
}

static void rtw8723b_query_phy_status(struct rtw_dev *rtwdev, u8 *phy_status,
				      struct rtw_rx_pkt_stat *pkt_stat)
{
	/*
	 * The 8723B PHY status does not report the channel, so we must
	 * mark it invalid to allow mac80211/rtw88 to parse it from the IE
	 * during scanning.
	 */
	pkt_stat->channel_invalid = true;

	if (pkt_stat->rate <= DESC_RATE11M)
		rtw8723b_query_phy_status_cck(rtwdev, phy_status, pkt_stat);
	else
		rtw8723b_query_phy_status_ofdm(rtwdev, phy_status, pkt_stat);
}

static void rtw8723b_set_iqk_matrix_by_result(struct rtw_dev *rtwdev,
					      u32 ofdm_swing, u8 path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s32 ele_a, ele_d, ele_c, ele_a_ext;
	s32 iqk_result_x;
	s32 iqk_result_y;
	s32 value32;

	switch (path) {
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
	ele_d = OFDM_SWING_D(ofdm_swing);

	/* new element A */
	iqk_result_x = iqkxy_to_s32(iqk_result_x);
	ele_a = iqk_mult(iqk_result_x, ele_d, &ele_a_ext);

	/* new element C */
	iqk_result_y = iqkxy_to_s32(iqk_result_y);
	ele_c = iqk_mult(iqk_result_y, ele_d, NULL);

	switch (path) {
	case RF_PATH_A:
	default:
		/* write new elements A, C, D, element B is always 0 */
		value32 = BIT_SET_TXIQ_ELM_ACD(ele_a, ele_c, ele_d);
		rtw_write32(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE, value32);
		value32 = BIT_SET_TXIQ_ELM_C1(ele_c);
		rtw_write32_mask(rtwdev, REG_TXIQK_MATRIXA_LSB2_11N, MASKH4BITS,
				 value32);
		rtw_write32_mask(rtwdev, REG_OFDM_0_ECCA_THRESHOLD, BIT(24),
				 ele_a_ext);
		break;

	case RF_PATH_B:
		/* write new elements A, C, D, element B is always 0 */
		value32 = BIT_SET_TXIQ_ELM_ACD(ele_a, ele_c, ele_d);
		rtw_write32(rtwdev, REG_OFDM_0_XB_TX_IQ_IMBALANCE, value32);
		value32 = BIT_SET_TXIQ_ELM_C1(ele_c);
		rtw_write32_mask(rtwdev, REG_TXIQK_MATRIXB_LSB2_11N, MASKH4BITS,
				 value32);
		rtw_write32_mask(rtwdev, REG_OFDM_0_ECCA_THRESHOLD, BIT(28),
				 ele_a_ext);
		break;
	}
}

static void rtw8723b_set_iqk_matrix(struct rtw_dev *rtwdev, s8 ofdm_index,
				    u8 path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u32 ofdm_swing;

	ofdm_index = clamp_t(s8, ofdm_index, 0, RTW_OFDM_SWING_TABLE_SIZE - 1);

	ofdm_swing = rtw8723b_ofdm_swing_table[ofdm_index];

	if (dm_info->iqk.done) {
		rtw8723b_set_iqk_matrix_by_result(rtwdev, ofdm_swing, path);
		return;
	}

	switch (path) {
	case RF_PATH_A:
	default:
		rtw_write32(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE, ofdm_swing);
		rtw_write32_mask(rtwdev, REG_TXIQK_MATRIXA_LSB2_11N, MASKH4BITS,
				 0x00);
		rtw_write32_mask(rtwdev, REG_OFDM_0_ECCA_THRESHOLD, BIT(24),
				 0x00);
		break;

	case RF_PATH_B:
		rtw_write32(rtwdev, REG_OFDM_0_XB_TX_IQ_IMBALANCE, ofdm_swing);
		rtw_write32_mask(rtwdev, REG_TXIQK_MATRIXB_LSB2_11N, MASKH4BITS,
				 0x00);
		rtw_write32_mask(rtwdev, REG_OFDM_0_ECCA_THRESHOLD, BIT(28),
				 0x00);
		break;
	}
}

static u8 rtw8723b_iqk_check_tx_failed(struct rtw_dev *rtwdev)
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

static u8 rtw8723b_iqk_check_rx_failed(struct rtw_dev *rtwdev)
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

static u8 rtw8723b_iqk_tx_path_a(struct rtw_dev *rtwdev)
{
	u32 path_sel;
	u8 status;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path A TX IQK!\n");

	/* Save RF path */
	path_sel = rtw_read32(rtwdev, REG_BB_SEL_BTG);

	/* leave IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, MASKH3BYTES, 0x000000);

	/* enable path A PA in TX IQK mode */
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, 0x80000, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_ADDR, RFREG_MASK,
		     0x20000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA0, RFREG_MASK, 0x0003f);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA1, RFREG_MASK, 0xc7f87);

	/* Tx IQK setting */
	rtw_write32(rtwdev, REG_TXIQK_11N, 0x01007c00);
	rtw_write32(rtwdev, REG_RXIQK_11N, 0x01004800);

	/* path-A IQK setting */
	rtw_write32(rtwdev, REG_TXIQK_TONE_A_11N, 0x18008c1c);
	rtw_write32(rtwdev, REG_RXIQK_TONE_A_11N, 0x38008c1c);
	rtw_write32(rtwdev, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_RX_IQK_TONE_B, 0x38008c1c);

	rtw_write32(rtwdev, REG_TXIQK_PI_A_11N, 0x821403ea);
	rtw_write32(rtwdev, REG_RXIQK_PI_A_11N, 0x28110000);
	rtw_write32(rtwdev, REG_TXIQK_PI_B, 0x82110000);
	rtw_write32(rtwdev, REG_RXIQK_PI_B, 0x28110000);

	/* LO calibration setting */
	rtw_write32(rtwdev, REG_IQK_AGC_RSP_11N, 0x00462911);

	/* enter IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, MASKH3BYTES, 0x808000);

	/* ant switch */
	rtw_write32(rtwdev, REG_BB_SEL_BTG,
		    rtw8723b_iqk_ant_switch_path(rtwdev));

	/* GNT_BT = 0 */
	rtw_write32(rtwdev, REG_BT_CONTROL, 0x00000800);

	/* One shot, path A LOK & IQK */
	rtw_write32(rtwdev, REG_IQK_AGC_PTS_11N, 0xf9000000);
	rtw_write32(rtwdev, REG_IQK_AGC_PTS_11N, 0xf8000000);

	msleep(IQK_DELAY_TIME_8723B);

	/* restore ant path */
	rtw_write32(rtwdev, REG_BB_SEL_BTG, path_sel);

	/* GNT_BT = 1 */
	rtw_write32(rtwdev, REG_BT_CONTROL, 0x00001800);

	/* leave IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, MASKH3BYTES, 0x000000);

	status = rtw8723b_iqk_check_tx_failed(rtwdev);

	return status;
}

static u8 rtw8723b_iqk_rx_path_a(struct rtw_dev *rtwdev)
{
	u32 reg_e94, reg_e9c, val32, path_sel;
	u8 status;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path A RX IQK step1!\n");

	/* Save RF path */
	path_sel = rtw_read32(rtwdev, REG_BB_SEL_BTG);

	/* leave IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, MASKH3BYTES, 0x000000);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, 0x80000, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_ADDR, RFREG_MASK,
		     0x30000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA0, RFREG_MASK, 0x0001f);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA1, RFREG_MASK, 0xf7fb7);

	/* IQK setting */
	rtw_write32(rtwdev, REG_TXIQK_11N, 0x01007c00);
	rtw_write32(rtwdev, REG_RXIQK_11N, 0x01004800);

	/* path-A IQK setting */
	rtw_write32(rtwdev, REG_TXIQK_TONE_A_11N, 0x18008c1c);
	rtw_write32(rtwdev, REG_RXIQK_TONE_A_11N, 0x38008c1c);
	rtw_write32(rtwdev, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_RX_IQK_TONE_B, 0x38008c1c);

	rtw_write32(rtwdev, REG_TXIQK_PI_A_11N, 0x82160ff0);
	rtw_write32(rtwdev, REG_RXIQK_PI_A_11N, 0x28110000);
	rtw_write32(rtwdev, REG_TXIQK_PI_B, 0x82110000);
	rtw_write32(rtwdev, REG_RXIQK_PI_B, 0x28110000);

	/* LO calibration setting */
	rtw_write32(rtwdev, REG_IQK_AGC_RSP_11N, 0x0046a911);

	/* enter IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, MASKH3BYTES, 0x808000);

	/* ant switch */
	rtw_write32(rtwdev, REG_BB_SEL_BTG,
		    rtw8723b_iqk_ant_switch_path(rtwdev));

	/* GNT_BT = 0 (disable BT) */
	rtw_write32(rtwdev, REG_BT_CONTROL, 0x00000800);

	/* One shot, path A LOK & IQK */
	rtw_write32(rtwdev, REG_IQK_AGC_PTS_11N, 0xf9000000);
	rtw_write32(rtwdev, REG_IQK_AGC_PTS_11N, 0xf8000000);

	msleep(IQK_DELAY_TIME_8723B);

	/* restore ant path */
	rtw_write32(rtwdev, REG_BB_SEL_BTG, path_sel);

	/* GNT_BT = 1 */
	rtw_write32(rtwdev, REG_BT_CONTROL, 0x00001800);

	/* leave IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, MASKH3BYTES, 0x000000);

	status = rtw8723b_iqk_check_tx_failed(rtwdev);

	/* if Tx not OK, ignore Rx */
	if (!status)
		return status;

	reg_e94 = rtw_read32(rtwdev, REG_IQK_RES_TX);
	reg_e9c = rtw_read32(rtwdev, REG_IQK_RES_TY);
	val32 = 0x80007c00 | (reg_e94 & 0x3ff0000) |
		((reg_e9c & 0x3ff0000) >> 16);
	rtw_write32(rtwdev, REG_TXIQK_11N, val32);

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path A RX IQK step2!\n");

	/* modify RX IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, MASKH3BYTES, 0x000000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, 0x80000, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_ADDR, RFREG_MASK,
		     0x30000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA0, RFREG_MASK, 0x0001f);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA1, RFREG_MASK, 0xf7d77);

	/* PA, PAD setting */
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTDBG, RFREG_MASK, 0xf80);
	rtw_write_rf(rtwdev, RF_PATH_A, 0x55, RFREG_MASK, 0x4021f);

	/* IQK setting */
	rtw_write32(rtwdev, REG_RXIQK_11N, 0x01004800);

	/* path-A IQK setting */
	rtw_write32(rtwdev, REG_TXIQK_TONE_A_11N, 0x38008c1c);
	rtw_write32(rtwdev, REG_RXIQK_TONE_A_11N, 0x18008c1c);
	rtw_write32(rtwdev, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_RX_IQK_TONE_B, 0x38008c1c);

	rtw_write32(rtwdev, REG_TXIQK_PI_A_11N, 0x82110000);
	rtw_write32(rtwdev, REG_RXIQK_PI_A_11N, 0x2816001f);
	rtw_write32(rtwdev, REG_TXIQK_PI_B, 0x82110000);
	rtw_write32(rtwdev, REG_RXIQK_PI_B, 0x28110000);

	/* LO calibration setting */
	rtw_write32(rtwdev, REG_IQK_AGC_RSP_11N, 0x0046a8d1);

	/* enter IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, MASKH3BYTES, 0x808000);

	/* ant switch */
	rtw_write32(rtwdev, REG_BB_SEL_BTG,
		    rtw8723b_iqk_ant_switch_path(rtwdev));

	/* GNT_BT = 0 */
	rtw_write32(rtwdev, REG_BT_CONTROL, 0x00000800);

	/* One shot, path A LOK & IQK */
	rtw_write32(rtwdev, REG_IQK_AGC_PTS_11N, 0xf9000000);
	rtw_write32(rtwdev, REG_IQK_AGC_PTS_11N, 0xf8000000);

	msleep(IQK_DELAY_TIME_8723B);

	/* restore ant path */
	rtw_write32(rtwdev, REG_BB_SEL_BTG, path_sel);

	/* GNT_BT = 1 */
	rtw_write32(rtwdev, REG_BT_CONTROL, 0x00001800);

	/* leave IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, MASKH3BYTES, 0x000000);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTDBG, RFREG_MASK, 0x780);

	status |= rtw8723b_iqk_check_rx_failed(rtwdev);

	return status;
}

static void
rtw8723b_iqk_fill_a_matrix(struct rtw_dev *rtwdev, const s32 result[])
{
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

	rtw_write32_mask(rtwdev, REG_A_RXIQI, BIT_MASK_RXIQ_S1_X,
			 result[IQK_S1_RX_X]);
	rtw_write32_mask(rtwdev, REG_A_RXIQI, BIT_MASK_RXIQ_S1_Y1,
			 BIT_SET_RXIQ_S1_Y1(result[IQK_S1_RX_Y]));
	rtw_write32_mask(rtwdev, REG_RXIQK_MATRIX_LSB_11N, BIT_MASK_RXIQ_S1_Y2,
			 BIT_SET_RXIQ_S1_Y2(result[IQK_S1_RX_Y]));
}

static void
rtw8723b_iqk_fill_b_matrix(struct rtw_dev *rtwdev, const s32 result[])
{
	s32 tx0_a, tx0_a_ext;
	s32 tx0_c, tx0_c_ext;
	s32 oldval_0;
	s32 x, y;

	if (result[IQK_S0_TX_X] == 0)
		return;

	oldval_0 = rtw_read32_mask(rtwdev, REG_OFDM_0_XB_TX_IQ_IMBALANCE,
				   BIT_MASK_TXIQ_ELM_D);

	x = iqkxy_to_s32(result[IQK_S0_TX_X]);
	tx0_a = iqk_mult(x, oldval_0, &tx0_a_ext);

	rtw_write32_mask(rtwdev, REG_OFDM_0_XB_TX_IQ_IMBALANCE,
			 BIT_MASK_TXIQ_ELM_A, tx0_a);
	rtw_write32_mask(rtwdev, REG_OFDM_0_ECCA_THRESHOLD, BIT(27),
			 tx0_a_ext);

	y = iqkxy_to_s32(result[IQK_S0_TX_Y]);
	tx0_c = iqk_mult(y, oldval_0, &tx0_c_ext);

	rtw_write32_mask(rtwdev, REG_TXIQK_MATRIXB_LSB2_11N, MASKH4BITS,
			 BIT_SET_TXIQ_ELM_C1(tx0_c));
	rtw_write32_mask(rtwdev, REG_OFDM_0_XB_TX_IQ_IMBALANCE,
			 BIT_MASK_TXIQ_ELM_C, BIT_SET_TXIQ_ELM_C2(tx0_c));
	rtw_write32_mask(rtwdev, REG_OFDM_0_ECCA_THRESHOLD, BIT(25),
			 tx0_c_ext);

	if (result[IQK_S0_RX_X] == 0)
		return;

	rtw_write32_mask(rtwdev, REG_B_RXIQI, BIT_MASK_RXIQ_X_S0,
			 result[IQK_S0_RX_X]);
	rtw_write32_mask(rtwdev, REG_B_RXIQI, BIT_MASK_RXIQ_S1_Y1,
			 BIT_SET_RXIQ_S1_Y1(result[IQK_S0_RX_Y]));
}

static void
rtw8723b_iqk_config_mac(struct rtw_dev *rtwdev,
			const struct rtw8723x_iqk_backup_regs *backup)
{
	int i;

	rtw_write8(rtwdev, rtw8723x_common.iqk_mac8_regs[0], 0x3f);

	for (i = 1; i < RTW8723X_IQK_MAC8_REG_NUM; i++)
		rtw_write8(rtwdev, rtw8723x_common.iqk_mac8_regs[i],
			   backup->mac8[i] & (~BIT(3)));

	/* This MAC backup register needs a byte-wide write. */
	rtw_write8(rtwdev, rtw8723x_common.iqk_mac32_regs[0],
		   backup->mac32[0] & (~BIT(5)));
}

static void
rtw8723b_iqk_one_round(struct rtw_dev *rtwdev, s32 result[][IQK_NR], u8 t,
		       const struct rtw8723x_iqk_backup_regs *backup)
{
	u8 a_ok;
	u32 i;

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] IQ Calibration for 1T1R_S0/S1 for %d times\n", t);

	rtw8723x_iqk_path_adda_on(rtwdev, ADDA_ON_VAL_8723B);
	rtw8723b_iqk_config_mac(rtwdev, backup);

	rtw_write32_mask(rtwdev, REG_CCK_ANT_SEL_11N, 0x0f000000, 0xf);
	rtw_write32(rtwdev, REG_BB_RX_PATH_11N, 0x03a05600);
	rtw_write32(rtwdev, REG_TRMUX_11N, 0x000800e4);
	rtw_write32(rtwdev, REG_BB_PWR_SAV1_11N, 0x22204000);

	/*
	 * RX IQ calibration setting for 8723B D cut large current issue
	 * when leaving IPS
	 */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, MASKH3BYTES, 0x000000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, 0x80000, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_ADDR, RFREG_MASK, 0x30000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA0, RFREG_MASK, 0x0001f);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA1, RFREG_MASK, 0xf7fb7);
	rtw_write_rf(rtwdev, RF_PATH_A, 0xed, 0x20, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, 0x43, RFREG_MASK, 0x60fbd);

	for (i = 0; i < PATH_IQK_RETRY; i++) {
		a_ok = rtw8723b_iqk_tx_path_a(rtwdev);
		if (a_ok != IQK_TX_OK) {
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"[IQK] path A TX IQK fail!\n");
			result[t][IQK_S1_TX_X] = 0x100;
			result[t][IQK_S1_TX_Y] = 0x0;
			continue;
		}

		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path A TX IQK success!\n");

		rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N,
				 MASKH3BYTES, 0x000000);

		result[t][IQK_S1_TX_X] =
			rtw_read32_mask(rtwdev, REG_IQK_RES_TX,
					BIT_MASK_RES_TX);
		result[t][IQK_S1_TX_Y] =
			rtw_read32_mask(rtwdev, REG_IQK_RES_TY,
					BIT_MASK_RES_TY);
		break;
	}

	for (i = 0; i < PATH_IQK_RETRY; i++) {
		a_ok = rtw8723b_iqk_rx_path_a(rtwdev);
		if (a_ok != (IQK_TX_OK | IQK_RX_OK)) {
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"[IQK] path A RX IQK fail!\n");
			result[t][IQK_S1_RX_X] = 0x100;
			result[t][IQK_S1_RX_Y] = 0x0;
			continue;
		}

		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path A RX IQK success!\n");
		result[t][IQK_S1_RX_X] =
			rtw_read32_mask(rtwdev, REG_IQK_RES_RX,
					BIT_MASK_RES_RX);
		result[t][IQK_S1_RX_Y] =
			rtw_read32_mask(rtwdev, REG_IQK_RES_RY,
					BIT_MASK_RES_RY);
		break;
	}

	if (a_ok == 0x0)
		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path A IQK fail!\n");

	/*
	 * The vendor driver calibrates path B only when the board is not
	 * 1-antenna (its is2T), which this driver does not support.
	 */

	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, MASKH3BYTES, 0x000000);
}

static void rtw8723b_phy_calibration(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw8723x_iqk_backup_regs backup;
	s32 result[IQK_ROUND_SIZE][IQK_NR];
	u32 bt_control_before;
	u8 final_candidate;
	bool good;
	u8 i, j;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] Start!\n");
	memset(result, 0, sizeof(result));
	final_candidate = IQK_ROUND_INVALID;

	rtw8723b_lck(rtwdev);
	rtw8723b_inform_rfk_status(rtwdev, true);

	/* The LTE path GNT backup that 8723d does is not needed on SDIO. */
	rtw8723x_iqk_backup_path_ctrl(rtwdev, &backup);
	rtw8723x_iqk_backup_regs(rtwdev, &backup);

	/* save default GNT_BT */
	bt_control_before = rtw_read32(rtwdev, REG_BT_CONTROL);

	for (i = IQK_ROUND_0; i <= IQK_ROUND_2; i++) {
		rtw8723b_iqk_one_round(rtwdev, result, i, &backup);

		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[IQK] back to BB mode, load original value!\n");

		if (i > IQK_ROUND_0) {
			rtw8723x_iqk_restore_regs(rtwdev, &backup);

			/* Restore RX initial gain */
			rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, 0x50);
			rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, backup.igia);

			/* load 0xe30 IQC default value */
			rtw_write32(rtwdev, REG_TXIQK_TONE_A_11N, 0x01008c00);
			rtw_write32(rtwdev, REG_RXIQK_TONE_A_11N, 0x01008c00);
		}

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
			rtw_warn(rtwdev, "IQK failed\n");
			goto out;
		}
	}

iqk_done:
	if (result[final_candidate][IQK_S1_TX_X])
		rtw8723b_iqk_fill_a_matrix(rtwdev, result[final_candidate]);
	if (result[final_candidate][IQK_S0_TX_X])
		rtw8723b_iqk_fill_b_matrix(rtwdev, result[final_candidate]);

	dm_info->iqk.result.s1_x = result[final_candidate][IQK_S1_TX_X];
	dm_info->iqk.result.s1_y = result[final_candidate][IQK_S1_TX_Y];
	dm_info->iqk.result.s0_x = result[final_candidate][IQK_S0_TX_X];
	dm_info->iqk.result.s0_y = result[final_candidate][IQK_S0_TX_Y];
	dm_info->iqk.done = true;

out:
	/* restore RF path */
	rtw_write32(rtwdev, REG_BB_SEL_BTG, backup.bb_sel_btg);

	/* restore GNT_BT */
	rtw_write32(rtwdev, REG_BT_CONTROL, bt_control_before);

	/* Restore RX mode table parameter */
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, 0x80000, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_ADDR, RFREG_MASK, 0x18000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA0, RFREG_MASK, 0x0001f);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA1, RFREG_MASK, 0xe6177);
	rtw_write_rf(rtwdev, RF_PATH_A, 0xed, 0x20, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, 0x43, RFREG_MASK, 0x300bd);

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] final_candidate is %x\n",
		final_candidate);

	for (i = IQK_ROUND_0; i < IQK_ROUND_SIZE; i++) {
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[IQK] Result %u s1: rege94=%x rege9c=%x regea4=%x regeac=%x\n",
			i, result[i][0], result[i][1], result[i][2],
			result[i][3]);
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[IQK] Result %u s0: rege94=%x rege9c=%x regea4=%x regeac=%x%s\n",
			i, result[i][4], result[i][5], result[i][6],
			result[i][7],
			final_candidate == i ? " (final candidate)" : "");
	}

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK]0xc80 = 0x%x 0xc94 = 0x%x 0xc14 = 0x%x 0xca0 = 0x%x\n",
		rtw_read32(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE),
		rtw_read32(rtwdev, REG_TXIQK_MATRIXA_LSB2_11N),
		rtw_read32(rtwdev, REG_A_RXIQI),
		rtw_read32(rtwdev, REG_RXIQK_MATRIX_LSB_11N));
	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK]0xcd0 = 0x%x 0xcd4 = 0x%x 0xcd8 = 0x%x\n",
		rtw_read32(rtwdev, REG_TXIQ_AB_S0),
		rtw_read32(rtwdev, REG_TXIQ_CD_S0),
		rtw_read32(rtwdev, REG_RXIQ_AB_S0));

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] finished\n");

	rtw8723b_inform_rfk_status(rtwdev, false);
}

static void rtw8723b_pwrtrack_set_ofdm_pwr(struct rtw_dev *rtwdev, u8 path,
					   s8 swing_idx, s8 txagc_idx)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	dm_info->txagc_remnant_ofdm[path] = txagc_idx;

	rtw8723b_set_iqk_matrix(rtwdev, swing_idx, path);
}

static void rtw8723b_pwrtrack_set_cck_pwr(struct rtw_dev *rtwdev, s8 swing_idx,
					  s8 txagc_idx)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	const u8 *table;
	int i;

	dm_info->txagc_remnant_cck = txagc_idx;

	swing_idx = clamp_t(s8, swing_idx, 0, RTW_CCK_SWING_TABLE_SIZE - 1);

	BUILD_BUG_ON(ARRAY_SIZE(rtw8723b_cck_swing_table_ch1_ch13[0]) !=
		     RTW_CCK_SWING_REG_NUM);

	table = rtwdev->hal.current_channel == 14 ?
		rtw8723b_cck_swing_table_ch14[swing_idx] :
		rtw8723b_cck_swing_table_ch1_ch13[swing_idx];

	for (i = 0; i < RTW_CCK_SWING_REG_NUM; i++)
		rtw_write8(rtwdev, REG_CCK_SWING + i, table[i]);
}

static void rtw8723b_pwrtrack_set(struct rtw_dev *rtwdev, u8 path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_hal *hal = &rtwdev->hal;
	s8 final_ofdm_swing_index;
	s8 final_cck_swing_index;
	/*
	 * 8703b and 8723d seem to use RTW_CCK_SWING_TABLE_SIZE, which is
	 * -2dB on this chip.
	 */
	u8 limit_cck = 28;
	u8 limit_ofdm;

	limit_ofdm = rtw8723x_pwrtrack_get_limit_ofdm(rtwdev);

	final_ofdm_swing_index = dm_info->default_ofdm_index +
				 dm_info->delta_power_index[path];
	final_cck_swing_index = dm_info->default_cck_index +
				dm_info->delta_power_index[path];

	if (final_ofdm_swing_index > limit_ofdm)
		rtw8723b_pwrtrack_set_ofdm_pwr(rtwdev, path, limit_ofdm,
					       final_ofdm_swing_index - limit_ofdm);
	else if (final_ofdm_swing_index < 0)
		rtw8723b_pwrtrack_set_ofdm_pwr(rtwdev, path, 0,
					       final_ofdm_swing_index);
	else
		rtw8723b_pwrtrack_set_ofdm_pwr(rtwdev, path, final_ofdm_swing_index, 0);

	if (final_cck_swing_index > limit_cck)
		rtw8723b_pwrtrack_set_cck_pwr(rtwdev, limit_cck,
					      final_cck_swing_index - limit_cck);
	else if (final_cck_swing_index < 0)
		rtw8723b_pwrtrack_set_cck_pwr(rtwdev, 0,
					      final_cck_swing_index);
	else
		rtw8723b_pwrtrack_set_cck_pwr(rtwdev, final_cck_swing_index, 0);

	rtw_phy_set_tx_power_level(rtwdev, hal->current_channel);
}

static void rtw8723b_phy_pwrtrack(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_swing_table swing_table;
	u8 thermal_value, delta, path;
	bool do_lck = false;

	rtw_phy_config_swing_table(rtwdev, &swing_table);

	if (rtwdev->efuse.thermal_meter[0] == 0xff)
		return;

	thermal_value = rtw_read_rf(rtwdev, RF_PATH_A, RF_T_METER, 0xfc00);

	/* Average the thermal meter readings. */
	rtw_phy_pwrtrack_avg(rtwdev, thermal_value, RF_PATH_A);

	/*
	 * The vendor's power tracking redoes the LC calibration when the
	 * thermal value drifts, but it skips the IQK for this chip, so the
	 * threshold that rtw_phy_pwrtrack_need_iqk() tracks gates only the
	 * LC calibration here.
	 */
	do_lck = rtw_phy_pwrtrack_need_iqk(rtwdev);

	if (do_lck)
		rtw8723b_lck(rtwdev);

	if (dm_info->pwr_trk_init_trigger)
		dm_info->pwr_trk_init_trigger = false;
	else if (!rtw_phy_pwrtrack_thermal_changed(rtwdev, thermal_value,
						   RF_PATH_A))
		return;

	delta = rtw_phy_pwrtrack_get_delta(rtwdev, RF_PATH_A);

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		s8 delta_cur, delta_last;

		delta_last = dm_info->delta_power_index[path];
		delta_cur = rtw_phy_pwrtrack_get_pwridx(rtwdev, &swing_table,
							path, RF_PATH_A, delta);
		if (delta_last == delta_cur)
			continue;

		dm_info->delta_power_index[path] = delta_cur;
		rtw8723b_pwrtrack_set(rtwdev, path);
	}
}

static void rtw8723b_pwr_track(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_efuse *efuse = &rtwdev->efuse;

	if (efuse->power_track_type != 0)
		return;

	if (!dm_info->pwr_trk_triggered) {
		rtw_write_rf(rtwdev, RF_PATH_A, RF_T_METER,
			     GENMASK(17, 16), 0x03);
		dm_info->pwr_trk_triggered = true;
		return;
	}

	rtw8723b_phy_pwrtrack(rtwdev);
	dm_info->pwr_trk_triggered = false;
}

/*
 * The vendor sequence for this chip differs from rtw8723x_coex_cfg_init():
 * REG_BT_TDMA_TIME is written whole, PTA pins and REG_QUEUE_CTRL untouched.
 */
static void rtw8723b_coex_cfg_init(struct rtw_dev *rtwdev)
{
	/* enable TBTT interrupt */
	rtw_write8_set(rtwdev, REG_BCN_CTRL, BIT_EN_BCN_FUNCTION);

	/* BT report packet sample rate, 0x790[5:0] = 0x5 */
	rtw_write8(rtwdev, REG_BT_TDMA_TIME, 0x5);

	/* enable BT counter statistics */
	rtw_write8(rtwdev, REG_BT_STAT_CTRL, 0x1);

	/* enable PTA (3-wire function from BT side) */
	rtw_write8_mask(rtwdev, REG_GPIO_MUXCFG, BIT_BT_PTA_EN, 0x1);
}

static void rtw8723b_coex_set_gnt_fix(struct rtw_dev *rtwdev)
{
	/* intentionally empty: rtw8723d's coex_set_gnt_fix is empty too */
}

static void rtw8723b_coex_set_gnt_debug(struct rtw_dev *rtwdev)
{
	/* GNT_BT debug routing is not implemented; not needed for operation. */
}

static bool rtw8723b_coex_ant_is_aux(struct rtw_dev *rtwdev)
{
	return !!(rtwdev->efuse.bt_setting & BIT(6));
}

static void rtw8723b_coex_set_ant_ctrl_by_wifi(struct rtw_dev *rtwdev)
{
	/* 0x4c[23] = 1, 0x4c[24] = 0: antenna control by 0x64. */
	rtw_write32_clr(rtwdev, REG_LED_CFG, BIT(24));
	rtw_write32_set(rtwdev, REG_LED_CFG, BIT(23));
}

static void rtw8723b_coex_set_ant_ctrl_by_bt(struct rtw_dev *rtwdev)
{
	/* 0x4c[24:23] = 0: antenna control by BT_RFE_CTRL. */
	rtw_write32_clr(rtwdev, REG_LED_CFG, BIT(23) | BIT(24));
}

static void rtw8723b_coex_cfg_ant_buffer(struct rtw_dev *rtwdev)
{
	rtw_write8_set(rtwdev, REG_PWR_DATA + 1,
		       BIT_EEPRPAD_RFE_CTRL_EN >> 8);
	rtw_write8(rtwdev, REG_RFE_CTRL_E, 0xff);
	rtw_write8_mask(rtwdev, REG_RFE_CTRL_ANT_SW, BIT_RFE_CTRL_ANT_SW_SEL, 0x3);
	rtw_write8(rtwdev, REG_RFE_CTRL_ANTA_SRC, 0x77);
}

static u32 rtw8723b_coex_ant_path_value(struct rtw_dev *rtwdev, u8 pos_type)
{
	bool aux = rtw8723b_coex_ant_is_aux(rtwdev);

	switch (pos_type) {
	case COEX_SWITCH_TO_BT:
		return aux ? 0x0 : 0x280;
	case COEX_SWITCH_TO_WLG:
	case COEX_SWITCH_TO_WLA:
		return aux ? 0x280 : 0x0;
	case COEX_SWITCH_TO_WLG_BT:
	case COEX_SWITCH_TO_NOCARE:
	default:
		return aux ? 0x80 : 0x200;
	}
}

static void rtw8723b_coex_cfg_ant_switch(struct rtw_dev *rtwdev,
					 u8 ctrl_type, u8 pos_type)
{
	u32 ant_path;

	if (rtw_hci_type(rtwdev) != RTW_HCI_TYPE_SDIO)
		return;

	if (ctrl_type == COEX_SWITCH_CTRL_BY_BT) {
		rtw_write8(rtwdev, REG_GNT_BT, 0x18);
		rtw_write8(rtwdev, REG_BT_COEX_ENH_INTR_CTRL, 0x4);
		rtw_write8_clr(rtwdev, REG_BT_ANT_SEL,
			       BIT_BT_SEL_BY_WIFI);
		rtw8723b_coex_set_ant_ctrl_by_bt(rtwdev);
		ant_path = rtw8723b_coex_ant_path_value(rtwdev,
							COEX_SWITCH_TO_BT);
		rtw_write32(rtwdev, REG_BB_SEL_BTG, ant_path);

		rtw_dbg(rtwdev, RTW_DBG_COEX,
			"[BTCoex], 8723bs ant switch by BT BB_SEL_BTG=0x%08x 0x4c=0x%08x 0x67=0x%02x 0x765=0x%02x 0x76e=0x%02x\n",
			rtw_read32(rtwdev, REG_BB_SEL_BTG),
			rtw_read32(rtwdev, REG_LED_CFG),
			rtw_read8(rtwdev, REG_BT_ANT_SEL),
			rtw_read8(rtwdev, REG_GNT_BT),
			rtw_read8(rtwdev, REG_BT_COEX_ENH_INTR_CTRL));
		return;
	}

	rtw8723b_coex_set_ant_ctrl_by_wifi(rtwdev);
	rtw_write8(rtwdev, REG_BT_ANT_SEL, 0x20);

	if (ctrl_type == COEX_SWITCH_CTRL_BY_BBSW &&
	    pos_type == COEX_SWITCH_TO_BT) {
		rtw_write8(rtwdev, REG_GNT_BT, 0x18);
		rtw_write8(rtwdev, REG_BT_COEX_ENH_INTR_CTRL, 0x4);
	} else {
		if (rtw_read8(rtwdev, REG_GNT_BT) != 0)
			rtw_write8(rtwdev, REG_GNT_BT, 0x0);

		if (rtw_read8(rtwdev, REG_BT_COEX_ENH_INTR_CTRL) != 0xc)
			rtw_write8(rtwdev, REG_BT_COEX_ENH_INTR_CTRL, 0xc);
	}

	if (ctrl_type == COEX_SWITCH_CTRL_BY_BBSW)
		ant_path = rtw8723b_coex_ant_path_value(rtwdev, pos_type);
	else
		ant_path = rtw8723b_coex_ant_path_value(rtwdev,
							COEX_SWITCH_TO_NOCARE);

	rtw_write32(rtwdev, REG_BB_SEL_BTG, ant_path);

	rtw_dbg(rtwdev, RTW_DBG_COEX,
		"[BTCoex], 8723bs ant switch ctrl=%u pos=%u BB_SEL_BTG=0x%08x 0x4c=0x%08x 0x67=0x%02x 0x765=0x%02x 0x76e=0x%02x\n",
		ctrl_type, pos_type, rtw_read32(rtwdev, REG_BB_SEL_BTG),
		rtw_read32(rtwdev, REG_LED_CFG),
		rtw_read8(rtwdev, REG_BT_ANT_SEL),
		rtw_read8(rtwdev, REG_GNT_BT),
		rtw_read8(rtwdev, REG_BT_COEX_ENH_INTR_CTRL));
}

static void rtw8723b_coex_set_rfe_type(struct rtw_dev *rtwdev)
{
	enum rtw_hci_type hci_type = rtw_hci_type(rtwdev);
	struct rtw_coex_rfe *coex_rfe = &rtwdev->coex.rfe;
	bool aux = rtw8723b_coex_ant_is_aux(rtwdev);
	u32 reg;

	coex_rfe->rfe_module_type = rtwdev->efuse.rfe_option;
	coex_rfe->ant_switch_polarity = aux ? 1 : 0;
	coex_rfe->ant_switch_exist = hci_type == RTW_HCI_TYPE_SDIO;
	coex_rfe->ant_switch_with_bt = false;
	coex_rfe->ant_switch_diversity = false;
	coex_rfe->wlg_at_btg = true;

	rtw_write8(rtwdev, REG_BT_ANT_SEL, 0x20);

	/* set GRAN_BT = 1 */
	rtw_write8(rtwdev, REG_GNT_BT, 0x18);

	/* set WLAN_ACT = 0 */
	rtw_write8(rtwdev, REG_BT_COEX_ENH_INTR_CTRL, 0x4);

	switch (hci_type) {
	case RTW_HCI_TYPE_USB:
		rtw_write32(rtwdev, REG_BB_SEL_BTG, 0x0);
		rtw_write8(rtwdev, 0xfe08, 0x1);
		break;
	case RTW_HCI_TYPE_PCIE:
		reg = 0x384;
		/* 0xc3[6]: 0 = S1 (main, path A), 1 = S0 (aux, path B). */
		if (aux) {
			rtw_write32(rtwdev, REG_BB_SEL_BTG, 0x0);
			rtw_write8(rtwdev, reg, 0x1);
		} else {
			rtw_write32(rtwdev, REG_BB_SEL_BTG, 0x280);
			rtw_write8(rtwdev, reg, 0x0);
		}
		break;
	case RTW_HCI_TYPE_SDIO:
		/*
		 * Internal switch: WiFi drives S0/S1, firmware gets the
		 * inverse hint.
		 */
		rtw_write_rf(rtwdev, RF_PATH_A, RF_WLINT, RFREG_MASK, 0x0780);

		if (aux) {
			rtw_write32(rtwdev, REG_BB_SEL_BTG, 0x0);
			rtw_write8(rtwdev, REG_SDIO_H2C, 0x1);
		} else {
			rtw_write32(rtwdev, REG_BB_SEL_BTG, 0x280);
			rtw_write8(rtwdev, REG_SDIO_H2C, 0x0);
		}

		rtw8723b_coex_set_ant_ctrl_by_wifi(rtwdev);
		rtw_write8_mask(rtwdev, REG_PAD_CTRL1,
				BIT_SW_DPDT_SEL_DATA, 0x0);
		rtw8723b_coex_cfg_ant_buffer(rtwdev);

		/* COEX_ANT_SEL_RSV is sent later from rtw_power_on(). */
		break;
	default:
		break;
	}
}

static void rtw8723b_coex_set_wl_tx_power(struct rtw_dev *rtwdev, u8 wl_pwr)
{
	/* Deferred: 8723d adjusts WL Tx power (0xb2/0x90) with BT on. */
}

static void rtw8723b_coex_set_wl_rx_gain(struct rtw_dev *rtwdev, bool low_gain)
{
	/* Deferred: 8723d lowers the WL Rx AGC while BT is active. */
}

static void rtw8723b_cfg_ldo25(struct rtw_dev *rtwdev, bool enable)
{
	/* Nothing to do: rtw88 only reads the efuse, it never writes it. */
}

static void rtw8723b_fill_txdesc_checksum(struct rtw_dev *rtwdev,
					  struct rtw_tx_pkt_info *pkt_info,
					  u8 *txdesc)
{
	/* Unlike the shared 8723x helper, the checksum is not inverted here. */
	fill_txdesc_checksum_common(txdesc, 16);
}

static int rtw8723b_read_efuse(struct rtw_dev *rtwdev, u8 *log_map)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	int ret;

	ret = rtw8723x_read_efuse(rtwdev, log_map);
	if (ret)
		return ret;

	/*
	 * Report a power track type this driver does not implement once,
	 * here, rather than on every two second pwr_track() tick.
	 */
	if (efuse->power_track_type != 0)
		rtw_warn(rtwdev, "unsupported power track type\n");

	/*
	 * No firmware hardware feature report exists, so fill the hardware
	 * capability here; a zero stream count kills the HT MCS set.
	 */
	efuse->hw_cap.nss = max_t(u8, rtwdev->hal.rf_path_num, 1);
	efuse->hw_cap.ant_num = efuse->hw_cap.nss;
	efuse->hw_cap.bw = BIT(RTW_CHANNEL_WIDTH_20) |
			   BIT(RTW_CHANNEL_WIDTH_40);

	return 0;
}

static const struct rtw_chip_ops rtw8723b_ops = {
	.power_on		= rtw_power_on,
	.power_off		= rtw_power_off,

	.mac_init		= rtw8723x_mac_init,
	.mac_postinit		= rtw8723x_mac_postinit,

	.dump_fw_crash		= NULL,
	/*
	 * 8723d sets REG_HCI_OPT_CTRL BIT_USB_SUS_DIS in its shutdown
	 * function; that is USB-only.
	 */
	.shutdown		= NULL,
	.read_efuse		= rtw8723b_read_efuse,
	.phy_set_param		= rtw8723b_phy_set_param,

	.set_channel		= rtw8723b_set_channel,

	.query_phy_status	= rtw8723b_query_phy_status,
	.read_rf		= rtw_phy_read_rf_sipi,
	.write_rf		= rtw_phy_write_rf_reg_sipi,
	.set_tx_power_index	= rtw8723x_set_tx_power_index,
	.rsvd_page_dump		= NULL,
	.set_antenna		= NULL,
	.cfg_ldo25		= rtw8723b_cfg_ldo25,
	.efuse_grant		= rtw8723b_efuse_grant,
	.set_ampdu_factor	= NULL,
	.false_alarm_statistics	= rtw8723x_false_alarm_statistics,
	.phy_calibration	= rtw8723b_phy_calibration,
	.dpk_track		= NULL,
	.cck_pd_set		= rtw_phy_cck_pd_set,
	.pwr_track		= rtw8723b_pwr_track,
	.config_bfee		= NULL,
	.set_gid_table		= NULL,
	.cfg_csi_rate		= NULL,
	.adaptivity_init	= NULL,
	.adaptivity		= NULL,
	.cfo_init		= NULL,
	.cfo_track		= NULL,
	.config_tx_path		= NULL,
	.config_txrx_mode	= NULL,
	.led_set		= NULL,
	.fill_txdesc_checksum	= rtw8723b_fill_txdesc_checksum,

	.coex_set_init		= rtw8723b_coex_cfg_init,
	.coex_set_ant_switch	= rtw8723b_coex_cfg_ant_switch,
	.coex_set_gnt_fix	= rtw8723b_coex_set_gnt_fix,
	.coex_set_gnt_debug	= rtw8723b_coex_set_gnt_debug,
	.coex_set_rfe_type	= rtw8723b_coex_set_rfe_type,
	.coex_set_wl_tx_power	= rtw8723b_coex_set_wl_tx_power,
	.coex_set_wl_rx_gain	= rtw8723b_coex_set_wl_rx_gain,
};

const struct rtw_chip_info rtw8723b_hw_spec = {
	.ops = &rtw8723b_ops,
	.id = RTW_CHIP_TYPE_8723B,
	.fw_name = "rtw88/rtw8723b_fw.bin",
	.wlan_cpu = RTW_WCPU_8051,
	.tx_pkt_desc_sz = 40,
	.tx_buf_desc_sz = 16,
	.rx_pkt_desc_sz = 24,
	.rx_buf_desc_sz = 8,
	.phy_efuse_size = 512,
	.log_efuse_size = 512,
	.ptct_efuse_size = 15,
	.txff_size = 32768,
	.rxff_size = 16384,
	.rsvd_drv_pg_num = 8,
	.txgi_factor = 1,
	.is_pwr_by_rate_dec = true,
	.max_power_index = 0x3f,
	.csi_buf_pg_num = 0,
	.band = RTW_BAND_2G,
	.page_size = TX_PAGE_SIZE,
	.dig_min = 0x20,
	.usb_tx_agg_desc_num = 6,
	/*
	 * The firmware reports id 0xfd instead of C2H_HW_FEATURE_REPORT, so
	 * the hardware feature report is not supported on this chip.
	 */
	.hw_feature_report = false,
	.c2h_ra_report_size = 4,
	.old_datarate_fb_limit = true,
	.path_div_supported = false,
	.ht_supported = true,
	.vht_supported = false,
	.lps_deep_mode_supported = 0,
	.sys_func_en = 0xfd,
	.pwr_on_seq = card_enable_flow_8723b,
	.pwr_off_seq = card_disable_flow_8723b,
	.page_table = page_table_8723b,
	.rqpn_table = rqpn_table_8723b,
	/* same shared table as the sibling rtw8703b and rtw8723d */
	.prioq_addrs = &rtw8723x_common.prioq_addrs,
	/* used only in pci.c, not needed for SDIO devices */
	.intf_table = NULL,
	.dig = rtw8723x_common.dig,
	/* The vendor driver never writes the CCK IGI on this chip. */
	.dig_cck = NULL,
	.rf_sipi_addr = {0x840, 0x844},
	.rf_sipi_read_addr = rtw8723x_common.rf_sipi_addr,
	.fix_rf_phy_num = 2,
	/* This chip has no LTE coex registers. */
	.ltecoex_addr = NULL,
	.mac_tbl = &rtw8723b_mac_tbl,
	.agc_tbl = &rtw8723b_agc_tbl,
	.bb_tbl = &rtw8723b_bb_tbl,
	.rf_tbl = {&rtw8723b_rf_a_tbl},
	.rfe_defs = rtw8723b_rfe_defs,
	.rfe_defs_size = ARRAY_SIZE(rtw8723b_rfe_defs),
	.iqk_threshold = 8,
	.rx_ldpc = false,
	.tx_stbc = false,
	.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16,
	.max_scan_ie_len = IEEE80211_MAX_DATA_LEN,
	.coex_para_ver = 20180201,	/* glcoex_ver_date_8723b_1ant */
	.bt_desired_ver = 0x6d,
	.scbd_support = false,
	.new_scbd10_def = true,
	.ble_hid_profile_support = false,
	.wl_mimo_ps_support = false,
	.pstdma_type = COEX_PSTDMA_FORCE_LPSOFF,
	.bt_rssi_type = COEX_BTRSSI_RATIO,
	.ant_isolation = 15,
	.rssi_tolerance = 2,
	.wl_rssi_step = wl_rssi_step_8723b,
	.bt_rssi_step = bt_rssi_step_8723b,
	.table_sant_num = ARRAY_SIZE(table_sant_8723b),
	.table_sant = table_sant_8723b,
	.table_nsant_num = ARRAY_SIZE(table_nsant_8723b),
	.table_nsant = table_nsant_8723b,
	.tdma_sant_num = ARRAY_SIZE(tdma_sant_8723b),
	.tdma_sant = tdma_sant_8723b,
	.tdma_nsant_num = ARRAY_SIZE(tdma_nsant_8723b),
	.tdma_nsant = tdma_nsant_8723b,
	.wl_rf_para_num = ARRAY_SIZE(rf_para_tx_8723b),
	.wl_rf_para_tx = rf_para_tx_8723b,
	.wl_rf_para_rx = rf_para_rx_8723b,
	.bt_afh_span_bw20 = 0x20,
	.bt_afh_span_bw40 = 0x30,
	.afh_5g_num =  ARRAY_SIZE(afh_5g_8723b),
	.afh_5g = afh_5g_8723b,
	/* BTG_SEL is driven by the cardemu_to_act power sequence instead. */
	.btg_reg = NULL,
	.coex_info_hw_regs_num = 0,
	.coex_info_hw_regs = NULL,
};
EXPORT_SYMBOL(rtw8723b_hw_spec);

MODULE_FIRMWARE("rtw88/rtw8723b_fw.bin");

MODULE_AUTHOR("Luka Gejak <luka.gejak@linux.dev>");
MODULE_AUTHOR("Michael Straube <straube.linux@gmail.com>");
MODULE_DESCRIPTION("Realtek 802.11n wireless 8723b driver");
MODULE_LICENSE("Dual BSD/GPL");
