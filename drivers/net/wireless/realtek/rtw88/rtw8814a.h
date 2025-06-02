/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2025  Realtek Corporation
 */

#ifndef __RTW8814A_H__
#define __RTW8814A_H__

struct rtw8814au_efuse {
	u8 vid[2];			/* 0xd0 */
	u8 pid[2];			/* 0xd2 */
	u8 res[4];			/* 0xd4 */
	u8 mac_addr[ETH_ALEN];		/* 0xd8 */
} __packed;

struct rtw8814ae_efuse {
	u8 mac_addr[ETH_ALEN];		/* 0xd0 */
	u8 vid[2];			/* 0xd6 */
	u8 did[2];			/* 0xd8 */
	u8 svid[2];			/* 0xda */
	u8 smid[2];			/* 0xdc */
} __packed;

struct rtw8814a_efuse {
	__le16 rtl_id;
	u8 res0[0x0c];
	u8 usb_mode;			/* 0x0e */
	u8 res1;

	/* power index for four RF paths */
	struct rtw_txpwr_idx txpwr_idx_table[4];

	u8 channel_plan;		/* 0xb8 */
	u8 xtal_k;			/* 0xb9 */
	u8 thermal_meter;		/* 0xba */
	u8 iqk_lck;			/* 0xbb */
	u8 pa_type;			/* 0xbc */
	u8 lna_type_2g[2];		/* 0xbd */
	u8 lna_type_5g[2];		/* 0xbf */
	u8 rf_board_option;		/* 0xc1 */
	u8 res2;
	u8 rf_bt_setting;		/* 0xc3 */
	u8 eeprom_version;		/* 0xc4 */
	u8 eeprom_customer_id;		/* 0xc5 */
	u8 tx_bb_swing_setting_2g;	/* 0xc6 */
	u8 tx_bb_swing_setting_5g;	/* 0xc7 */
	u8 res3;
	u8 trx_antenna_option;		/* 0xc9 */
	u8 rfe_option;			/* 0xca */
	u8 country_code[2];		/* 0xcb */
	u8 res4[3];
	union {
		struct rtw8814au_efuse u;
		struct rtw8814ae_efuse e;
	};
	u8 res5[0x122];			/* 0xde */
} __packed;

static_assert(sizeof(struct rtw8814a_efuse) == 512);

extern const struct rtw_chip_info rtw8814a_hw_spec;

#endif
