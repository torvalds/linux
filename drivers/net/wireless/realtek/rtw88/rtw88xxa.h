/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2024  Realtek Corporation
 */

#ifndef __RTW88XXA_H__
#define __RTW88XXA_H__

#include <asm/byteorder.h>
#include "reg.h"

struct rtw8821au_efuse {
	u8 res4[48];			/* 0xd0 */
	u8 vid[2];			/* 0x100 */
	u8 pid[2];
	u8 res8[3];
	u8 mac_addr[ETH_ALEN];		/* 0x107 */
	u8 res9[243];
} __packed;

struct rtw8812au_efuse {
	u8 vid[2];			/* 0xd0 */
	u8 pid[2];			/* 0xd2 */
	u8 res0[3];
	u8 mac_addr[ETH_ALEN];		/* 0xd7 */
	u8 res1[291];
} __packed;

struct rtw88xxa_efuse {
	__le16 rtl_id;
	u8 res0[6];			/* 0x02 */
	u8 usb_mode;			/* 0x08 */
	u8 res1[7];			/* 0x09 */

	/* power index for four RF paths */
	struct rtw_txpwr_idx txpwr_idx_table[4];

	u8 channel_plan;		/* 0xb8 */
	u8 xtal_k;
	u8 thermal_meter;
	u8 iqk_lck;
	u8 pa_type;			/* 0xbc */
	u8 lna_type_2g;			/* 0xbd */
	u8 res2;
	u8 lna_type_5g;			/* 0xbf */
	u8 res3;
	u8 rf_board_option;		/* 0xc1 */
	u8 rf_feature_option;
	u8 rf_bt_setting;
	u8 eeprom_version;
	u8 eeprom_customer_id;		/* 0xc5 */
	u8 tx_bb_swing_setting_2g;
	u8 tx_bb_swing_setting_5g;
	u8 tx_pwr_calibrate_rate;
	u8 rf_antenna_option;		/* 0xc9 */
	u8 rfe_option;
	u8 country_code[2];
	u8 res4[3];
	union {
		struct rtw8821au_efuse rtw8821au;
		struct rtw8812au_efuse rtw8812au;
	};
} __packed;

static_assert(sizeof(struct rtw88xxa_efuse) == 512);

#define WLAN_BCN_DMA_TIME			0x02
#define WLAN_TBTT_PROHIBIT			0x04
#define WLAN_TBTT_HOLD_TIME			0x064
#define WLAN_TBTT_TIME	(WLAN_TBTT_PROHIBIT |\
			(WLAN_TBTT_HOLD_TIME << BIT_SHIFT_TBTT_HOLD_TIME_AP))

struct rtw_jaguar_phy_status_rpt {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
	__le32 w4;
	__le32 w5;
	__le32 w6;
} __packed;

#define RTW_JGRPHY_W0_GAIN_A		GENMASK(6, 0)
#define RTW_JGRPHY_W0_TRSW_A		BIT(7)
#define RTW_JGRPHY_W0_GAIN_B		GENMASK(14, 8)
#define RTW_JGRPHY_W0_TRSW_B		BIT(15)
#define RTW_JGRPHY_W0_CHL_NUM		GENMASK(25, 16)
#define RTW_JGRPHY_W0_SUB_CHNL		GENMASK(29, 26)
#define RTW_JGRPHY_W0_R_RFMOD		GENMASK(31, 30)

/* CCK: */
#define RTW_JGRPHY_W1_SIG_QUAL		GENMASK(7, 0)
#define RTW_JGRPHY_W1_AGC_RPT_VGA_IDX	GENMASK(12, 8)
#define RTW_JGRPHY_W1_AGC_RPT_LNA_IDX	GENMASK(15, 13)
#define RTW_JGRPHY_W1_BB_POWER		GENMASK(23, 16)
/* OFDM: */
#define RTW_JGRPHY_W1_PWDB_ALL		GENMASK(7, 0)
#define RTW_JGRPHY_W1_CFO_SHORT_A	GENMASK(15, 8)	/* s8 */
#define RTW_JGRPHY_W1_CFO_SHORT_B	GENMASK(23, 16)	/* s8 */
#define RTW_JGRPHY_W1_BT_RF_CH_MSB	GENMASK(31, 30)

#define RTW_JGRPHY_W2_ANT_DIV_SW_A	BIT(0)
#define RTW_JGRPHY_W2_ANT_DIV_SW_B	BIT(1)
#define RTW_JGRPHY_W2_BT_RF_CH_LSB	GENMASK(7, 2)
#define RTW_JGRPHY_W2_CFO_TAIL_A	GENMASK(15, 8)	/* s8 */
#define RTW_JGRPHY_W2_CFO_TAIL_B	GENMASK(23, 16)	/* s8 */
#define RTW_JGRPHY_W2_PCTS_MSK_RPT_0	GENMASK(31, 24)

#define RTW_JGRPHY_W3_PCTS_MSK_RPT_1	GENMASK(7, 0)
/* Stream 1 and 2 RX EVM: */
#define RTW_JGRPHY_W3_RXEVM_1		GENMASK(15, 8)	/* s8 */
#define RTW_JGRPHY_W3_RXEVM_2		GENMASK(23, 16)	/* s8 */
#define RTW_JGRPHY_W3_RXSNR_A		GENMASK(31, 24)	/* s8 */

#define RTW_JGRPHY_W4_RXSNR_B		GENMASK(7, 0)	/* s8 */
#define RTW_JGRPHY_W4_PCTS_MSK_RPT_2	GENMASK(21, 8)
#define RTW_JGRPHY_W4_PCTS_RPT_VALID	BIT(22)
#define RTW_JGRPHY_W4_RXEVM_3		GENMASK(31, 24)	/* s8 */

#define RTW_JGRPHY_W5_RXEVM_4		GENMASK(7, 0)	/* s8 */
/* 8812a, stream 1 and 2 CSI: */
#define RTW_JGRPHY_W5_CSI_CURRENT_1	GENMASK(15, 8)
#define RTW_JGRPHY_W5_CSI_CURRENT_2	GENMASK(23, 16)
/* 8814a: */
#define RTW_JGRPHY_W5_RXSNR_C		GENMASK(15, 8)	/* s8 */
#define RTW_JGRPHY_W5_RXSNR_D		GENMASK(23, 16)	/* s8 */
#define RTW_JGRPHY_W5_GAIN_C		GENMASK(30, 24)
#define RTW_JGRPHY_W5_TRSW_C		BIT(31)

#define RTW_JGRPHY_W6_GAIN_D		GENMASK(6, 0)
#define RTW_JGRPHY_W6_TRSW_D		BIT(7)
#define RTW_JGRPHY_W6_SIGEVM		GENMASK(15, 8)	/* s8 */
#define RTW_JGRPHY_W6_ANTIDX_ANTC	GENMASK(18, 16)
#define RTW_JGRPHY_W6_ANTIDX_ANTD	GENMASK(21, 19)
#define RTW_JGRPHY_W6_DPDT_CTRL_KEEP	BIT(22)
#define RTW_JGRPHY_W6_GNT_BT_KEEP	BIT(23)
#define RTW_JGRPHY_W6_ANTIDX_ANTA	GENMASK(26, 24)
#define RTW_JGRPHY_W6_ANTIDX_ANTB	GENMASK(29, 27)
#define RTW_JGRPHY_W6_HW_ANTSW_OCCUR	GENMASK(31, 30)

#define RF18_BW_MASK		(BIT(11) | BIT(10))

void rtw88xxa_efuse_grant(struct rtw_dev *rtwdev, bool on);
int rtw88xxa_read_efuse(struct rtw_dev *rtwdev, u8 *log_map);
void rtw88xxa_power_off(struct rtw_dev *rtwdev,
			const struct rtw_pwr_seq_cmd *const *enter_lps_flow);
int rtw88xxa_power_on(struct rtw_dev *rtwdev);
u32 rtw88xxa_phy_read_rf(struct rtw_dev *rtwdev,
			 enum rtw_rf_path rf_path, u32 addr, u32 mask);
void rtw88xxa_set_channel(struct rtw_dev *rtwdev, u8 channel, u8 bw,
			  u8 primary_chan_idx);
void rtw88xxa_query_phy_status(struct rtw_dev *rtwdev, u8 *phy_status,
			       struct rtw_rx_pkt_stat *pkt_stat,
			       s8 (*cck_rx_pwr)(u8 lna_idx, u8 vga_idx));
void rtw88xxa_set_tx_power_index(struct rtw_dev *rtwdev);
void rtw88xxa_false_alarm_statistics(struct rtw_dev *rtwdev);
void rtw88xxa_iqk_backup_mac_bb(struct rtw_dev *rtwdev,
				u32 *macbb_backup,
				const u32 *backup_macbb_reg,
				u32 macbb_num);
void rtw88xxa_iqk_backup_afe(struct rtw_dev *rtwdev, u32 *afe_backup,
			     const u32 *backup_afe_reg, u32 afe_num);
void rtw88xxa_iqk_restore_mac_bb(struct rtw_dev *rtwdev,
				 u32 *macbb_backup,
				 const u32 *backup_macbb_reg,
				 u32 macbb_num);
void rtw88xxa_iqk_configure_mac(struct rtw_dev *rtwdev);
bool rtw88xxa_iqk_finish(int average, int threshold,
			 int *x_temp, int *y_temp, int *x, int *y,
			 bool break_inner, bool break_outer);
void rtw88xxa_phy_pwrtrack(struct rtw_dev *rtwdev,
			   void (*do_lck)(struct rtw_dev *rtwdev),
			   void (*do_iqk)(struct rtw_dev *rtwdev));
void rtw88xxa_phy_cck_pd_set(struct rtw_dev *rtwdev, u8 new_lvl);

#endif
