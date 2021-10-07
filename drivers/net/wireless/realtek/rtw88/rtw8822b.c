// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include <linux/module.h>
#include "main.h"
#include "coex.h"
#include "fw.h"
#include "tx.h"
#include "rx.h"
#include "phy.h"
#include "rtw8822b.h"
#include "rtw8822b_table.h"
#include "mac.h"
#include "reg.h"
#include "debug.h"
#include "bf.h"
#include "regd.h"

static void rtw8822b_config_trx_mode(struct rtw_dev *rtwdev, u8 tx_path,
				     u8 rx_path, bool is_tx2_path);

static void rtw8822be_efuse_parsing(struct rtw_efuse *efuse,
				    struct rtw8822b_efuse *map)
{
	ether_addr_copy(efuse->addr, map->e.mac_addr);
}

static int rtw8822b_read_efuse(struct rtw_dev *rtwdev, u8 *log_map)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw8822b_efuse *map;
	int i;

	map = (struct rtw8822b_efuse *)log_map;

	efuse->rfe_option = map->rfe_option;
	efuse->rf_board_option = map->rf_board_option;
	efuse->crystal_cap = map->xtal_k;
	efuse->pa_type_2g = map->pa_type;
	efuse->pa_type_5g = map->pa_type;
	efuse->lna_type_2g = map->lna_type_2g[0];
	efuse->lna_type_5g = map->lna_type_5g[0];
	efuse->channel_plan = map->channel_plan;
	efuse->country_code[0] = map->country_code[0];
	efuse->country_code[1] = map->country_code[1];
	efuse->bt_setting = map->rf_bt_setting;
	efuse->regd = map->rf_board_option & 0x7;
	efuse->thermal_meter[RF_PATH_A] = map->thermal_meter;
	efuse->thermal_meter_k = map->thermal_meter;

	for (i = 0; i < 4; i++)
		efuse->txpwr_idx_table[i] = map->txpwr_idx_table[i];

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
		rtw8822be_efuse_parsing(efuse, map);
		break;
	default:
		/* unsupported now */
		return -ENOTSUPP;
	}

	return 0;
}

static void rtw8822b_phy_rfe_init(struct rtw_dev *rtwdev)
{
	/* chip top mux */
	rtw_write32_mask(rtwdev, 0x64, BIT(29) | BIT(28), 0x3);
	rtw_write32_mask(rtwdev, 0x4c, BIT(26) | BIT(25), 0x0);
	rtw_write32_mask(rtwdev, 0x40, BIT(2), 0x1);

	/* from s0 or s1 */
	rtw_write32_mask(rtwdev, 0x1990, 0x3f, 0x30);
	rtw_write32_mask(rtwdev, 0x1990, (BIT(11) | BIT(10)), 0x3);

	/* input or output */
	rtw_write32_mask(rtwdev, 0x974, 0x3f, 0x3f);
	rtw_write32_mask(rtwdev, 0x974, (BIT(11) | BIT(10)), 0x3);
}

#define RTW_TXSCALE_SIZE 37
static const u32 rtw8822b_txscale_tbl[RTW_TXSCALE_SIZE] = {
	0x081, 0x088, 0x090, 0x099, 0x0a2, 0x0ac, 0x0b6, 0x0c0, 0x0cc, 0x0d8,
	0x0e5, 0x0f2, 0x101, 0x110, 0x120, 0x131, 0x143, 0x156, 0x16a, 0x180,
	0x197, 0x1af, 0x1c8, 0x1e3, 0x200, 0x21e, 0x23e, 0x261, 0x285, 0x2ab,
	0x2d3, 0x2fe, 0x32b, 0x35c, 0x38e, 0x3c4, 0x3fe
};

static u8 rtw8822b_get_swing_index(struct rtw_dev *rtwdev)
{
	u8 i = 0;
	u32 swing, table_value;

	swing = rtw_read32_mask(rtwdev, 0xc1c, 0xffe00000);
	for (i = 0; i < RTW_TXSCALE_SIZE; i++) {
		table_value = rtw8822b_txscale_tbl[i];
		if (swing == table_value)
			break;
	}

	return i;
}

static void rtw8822b_pwrtrack_init(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 swing_idx = rtw8822b_get_swing_index(rtwdev);
	u8 path;

	if (swing_idx >= RTW_TXSCALE_SIZE)
		dm_info->default_ofdm_index = 24;
	else
		dm_info->default_ofdm_index = swing_idx;

	for (path = RF_PATH_A; path < rtwdev->hal.rf_path_num; path++) {
		ewma_thermal_init(&dm_info->avg_thermal[path]);
		dm_info->delta_power_index[path] = 0;
	}
	dm_info->pwr_trk_triggered = false;
	dm_info->pwr_trk_init_trigger = true;
	dm_info->thermal_meter_k = rtwdev->efuse.thermal_meter_k;
}

static void rtw8822b_phy_bf_init(struct rtw_dev *rtwdev)
{
	rtw_bf_phy_init(rtwdev);
	/* Grouping bitmap parameters */
	rtw_write32(rtwdev, 0x1C94, 0xAFFFAFFF);
}

static void rtw8822b_phy_set_param(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u8 crystal_cap;
	bool is_tx2_path;

	/* power on BB/RF domain */
	rtw_write8_set(rtwdev, REG_SYS_FUNC_EN,
		       BIT_FEN_BB_RSTB | BIT_FEN_BB_GLB_RST);
	rtw_write8_set(rtwdev, REG_RF_CTRL,
		       BIT_RF_EN | BIT_RF_RSTB | BIT_RF_SDM_RSTB);
	rtw_write32_set(rtwdev, REG_WLRF1, BIT_WLRF1_BBRF_EN);

	/* pre init before header files config */
	rtw_write32_clr(rtwdev, REG_RXPSEL, BIT_RX_PSEL_RST);

	rtw_phy_load_tables(rtwdev);

	crystal_cap = rtwdev->efuse.crystal_cap & 0x3F;
	rtw_write32_mask(rtwdev, 0x24, 0x7e000000, crystal_cap);
	rtw_write32_mask(rtwdev, 0x28, 0x7e, crystal_cap);

	/* post init after header files config */
	rtw_write32_set(rtwdev, REG_RXPSEL, BIT_RX_PSEL_RST);

	is_tx2_path = false;
	rtw8822b_config_trx_mode(rtwdev, hal->antenna_tx, hal->antenna_rx,
				 is_tx2_path);
	rtw_phy_init(rtwdev);

	rtw8822b_phy_rfe_init(rtwdev);
	rtw8822b_pwrtrack_init(rtwdev);

	rtw8822b_phy_bf_init(rtwdev);
}

#define WLAN_SLOT_TIME		0x09
#define WLAN_PIFS_TIME		0x19
#define WLAN_SIFS_CCK_CONT_TX	0xA
#define WLAN_SIFS_OFDM_CONT_TX	0xE
#define WLAN_SIFS_CCK_TRX	0x10
#define WLAN_SIFS_OFDM_TRX	0x10
#define WLAN_VO_TXOP_LIMIT	0x186 /* unit : 32us */
#define WLAN_VI_TXOP_LIMIT	0x3BC /* unit : 32us */
#define WLAN_RDG_NAV		0x05
#define WLAN_TXOP_NAV		0x1B
#define WLAN_CCK_RX_TSF		0x30
#define WLAN_OFDM_RX_TSF	0x30
#define WLAN_TBTT_PROHIBIT	0x04 /* unit : 32us */
#define WLAN_TBTT_HOLD_TIME	0x064 /* unit : 32us */
#define WLAN_DRV_EARLY_INT	0x04
#define WLAN_BCN_DMA_TIME	0x02

#define WLAN_RX_FILTER0		0x0FFFFFFF
#define WLAN_RX_FILTER2		0xFFFF
#define WLAN_RCR_CFG		0xE400220E
#define WLAN_RXPKT_MAX_SZ	12288
#define WLAN_RXPKT_MAX_SZ_512	(WLAN_RXPKT_MAX_SZ >> 9)

#define WLAN_AMPDU_MAX_TIME		0x70
#define WLAN_RTS_LEN_TH			0xFF
#define WLAN_RTS_TX_TIME_TH		0x08
#define WLAN_MAX_AGG_PKT_LIMIT		0x20
#define WLAN_RTS_MAX_AGG_PKT_LIMIT	0x20
#define FAST_EDCA_VO_TH		0x06
#define FAST_EDCA_VI_TH		0x06
#define FAST_EDCA_BE_TH		0x06
#define FAST_EDCA_BK_TH		0x06
#define WLAN_BAR_RETRY_LIMIT		0x01
#define WLAN_RA_TRY_RATE_AGG_LIMIT	0x08

#define WLAN_TX_FUNC_CFG1		0x30
#define WLAN_TX_FUNC_CFG2		0x30
#define WLAN_MAC_OPT_NORM_FUNC1		0x98
#define WLAN_MAC_OPT_LB_FUNC1		0x80
#define WLAN_MAC_OPT_FUNC2		0x30810041

#define WLAN_SIFS_CFG	(WLAN_SIFS_CCK_CONT_TX | \
			(WLAN_SIFS_OFDM_CONT_TX << BIT_SHIFT_SIFS_OFDM_CTX) | \
			(WLAN_SIFS_CCK_TRX << BIT_SHIFT_SIFS_CCK_TRX) | \
			(WLAN_SIFS_OFDM_TRX << BIT_SHIFT_SIFS_OFDM_TRX))

#define WLAN_TBTT_TIME	(WLAN_TBTT_PROHIBIT |\
			(WLAN_TBTT_HOLD_TIME << BIT_SHIFT_TBTT_HOLD_TIME_AP))

#define WLAN_NAV_CFG		(WLAN_RDG_NAV | (WLAN_TXOP_NAV << 16))
#define WLAN_RX_TSF_CFG		(WLAN_CCK_RX_TSF | (WLAN_OFDM_RX_TSF) << 8)

static int rtw8822b_mac_init(struct rtw_dev *rtwdev)
{
	u32 value32;

	/* protocol configuration */
	rtw_write8_clr(rtwdev, REG_SW_AMPDU_BURST_MODE_CTRL, BIT_PRE_TX_CMD);
	rtw_write8(rtwdev, REG_AMPDU_MAX_TIME_V1, WLAN_AMPDU_MAX_TIME);
	rtw_write8_set(rtwdev, REG_TX_HANG_CTRL, BIT_EN_EOF_V1);
	value32 = WLAN_RTS_LEN_TH | (WLAN_RTS_TX_TIME_TH << 8) |
		  (WLAN_MAX_AGG_PKT_LIMIT << 16) |
		  (WLAN_RTS_MAX_AGG_PKT_LIMIT << 24);
	rtw_write32(rtwdev, REG_PROT_MODE_CTRL, value32);
	rtw_write16(rtwdev, REG_BAR_MODE_CTRL + 2,
		    WLAN_BAR_RETRY_LIMIT | WLAN_RA_TRY_RATE_AGG_LIMIT << 8);
	rtw_write8(rtwdev, REG_FAST_EDCA_VOVI_SETTING, FAST_EDCA_VO_TH);
	rtw_write8(rtwdev, REG_FAST_EDCA_VOVI_SETTING + 2, FAST_EDCA_VI_TH);
	rtw_write8(rtwdev, REG_FAST_EDCA_BEBK_SETTING, FAST_EDCA_BE_TH);
	rtw_write8(rtwdev, REG_FAST_EDCA_BEBK_SETTING + 2, FAST_EDCA_BK_TH);
	/* EDCA configuration */
	rtw_write8_clr(rtwdev, REG_TIMER0_SRC_SEL, BIT_TSFT_SEL_TIMER0);
	rtw_write16(rtwdev, REG_TXPAUSE, 0x0000);
	rtw_write8(rtwdev, REG_SLOT, WLAN_SLOT_TIME);
	rtw_write8(rtwdev, REG_PIFS, WLAN_PIFS_TIME);
	rtw_write32(rtwdev, REG_SIFS, WLAN_SIFS_CFG);
	rtw_write16(rtwdev, REG_EDCA_VO_PARAM + 2, WLAN_VO_TXOP_LIMIT);
	rtw_write16(rtwdev, REG_EDCA_VI_PARAM + 2, WLAN_VI_TXOP_LIMIT);
	rtw_write32(rtwdev, REG_RD_NAV_NXT, WLAN_NAV_CFG);
	rtw_write16(rtwdev, REG_RXTSF_OFFSET_CCK, WLAN_RX_TSF_CFG);
	/* Set beacon cotnrol - enable TSF and other related functions */
	rtw_write8_set(rtwdev, REG_BCN_CTRL, BIT_EN_BCN_FUNCTION);
	/* Set send beacon related registers */
	rtw_write32(rtwdev, REG_TBTT_PROHIBIT, WLAN_TBTT_TIME);
	rtw_write8(rtwdev, REG_DRVERLYINT, WLAN_DRV_EARLY_INT);
	rtw_write8(rtwdev, REG_BCNDMATIM, WLAN_BCN_DMA_TIME);
	rtw_write8_clr(rtwdev, REG_TX_PTCL_CTRL + 1, BIT_SIFS_BK_EN >> 8);
	/* WMAC configuration */
	rtw_write32(rtwdev, REG_RXFLTMAP0, WLAN_RX_FILTER0);
	rtw_write16(rtwdev, REG_RXFLTMAP2, WLAN_RX_FILTER2);
	rtw_write32(rtwdev, REG_RCR, WLAN_RCR_CFG);
	rtw_write8(rtwdev, REG_RX_PKT_LIMIT, WLAN_RXPKT_MAX_SZ_512);
	rtw_write8(rtwdev, REG_TCR + 2, WLAN_TX_FUNC_CFG2);
	rtw_write8(rtwdev, REG_TCR + 1, WLAN_TX_FUNC_CFG1);
	rtw_write32(rtwdev, REG_WMAC_OPTION_FUNCTION + 8, WLAN_MAC_OPT_FUNC2);
	rtw_write8(rtwdev, REG_WMAC_OPTION_FUNCTION + 4, WLAN_MAC_OPT_NORM_FUNC1);

	return 0;
}

static void rtw8822b_set_channel_rfe_efem(struct rtw_dev *rtwdev, u8 channel)
{
	struct rtw_hal *hal = &rtwdev->hal;

	if (IS_CH_2G_BAND(channel)) {
		rtw_write32s_mask(rtwdev, REG_RFESEL0, 0xffffff, 0x705770);
		rtw_write32s_mask(rtwdev, REG_RFESEL8, MASKBYTE1, 0x57);
		rtw_write32s_mask(rtwdev, REG_RFECTL, BIT(4), 0);
	} else {
		rtw_write32s_mask(rtwdev, REG_RFESEL0, 0xffffff, 0x177517);
		rtw_write32s_mask(rtwdev, REG_RFESEL8, MASKBYTE1, 0x75);
		rtw_write32s_mask(rtwdev, REG_RFECTL, BIT(5), 0);
	}

	rtw_write32s_mask(rtwdev, REG_RFEINV, BIT(11) | BIT(10) | 0x3f, 0x0);

	if (hal->antenna_rx == BB_PATH_AB ||
	    hal->antenna_tx == BB_PATH_AB) {
		/* 2TX or 2RX */
		rtw_write32s_mask(rtwdev, REG_TRSW, MASKLWORD, 0xa501);
	} else if (hal->antenna_rx == hal->antenna_tx) {
		/* TXA+RXA or TXB+RXB */
		rtw_write32s_mask(rtwdev, REG_TRSW, MASKLWORD, 0xa500);
	} else {
		/* TXB+RXA or TXA+RXB */
		rtw_write32s_mask(rtwdev, REG_TRSW, MASKLWORD, 0xa005);
	}
}

static void rtw8822b_set_channel_rfe_ifem(struct rtw_dev *rtwdev, u8 channel)
{
	struct rtw_hal *hal = &rtwdev->hal;

	if (IS_CH_2G_BAND(channel)) {
		/* signal source */
		rtw_write32s_mask(rtwdev, REG_RFESEL0, 0xffffff, 0x745774);
		rtw_write32s_mask(rtwdev, REG_RFESEL8, MASKBYTE1, 0x57);
	} else {
		/* signal source */
		rtw_write32s_mask(rtwdev, REG_RFESEL0, 0xffffff, 0x477547);
		rtw_write32s_mask(rtwdev, REG_RFESEL8, MASKBYTE1, 0x75);
	}

	rtw_write32s_mask(rtwdev, REG_RFEINV, BIT(11) | BIT(10) | 0x3f, 0x0);

	if (IS_CH_2G_BAND(channel)) {
		if (hal->antenna_rx == BB_PATH_AB ||
		    hal->antenna_tx == BB_PATH_AB) {
			/* 2TX or 2RX */
			rtw_write32s_mask(rtwdev, REG_TRSW, MASKLWORD, 0xa501);
		} else if (hal->antenna_rx == hal->antenna_tx) {
			/* TXA+RXA or TXB+RXB */
			rtw_write32s_mask(rtwdev, REG_TRSW, MASKLWORD, 0xa500);
		} else {
			/* TXB+RXA or TXA+RXB */
			rtw_write32s_mask(rtwdev, REG_TRSW, MASKLWORD, 0xa005);
		}
	} else {
		rtw_write32s_mask(rtwdev, REG_TRSW, MASKLWORD, 0xa5a5);
	}
}

enum {
	CCUT_IDX_1R_2G,
	CCUT_IDX_2R_2G,
	CCUT_IDX_1R_5G,
	CCUT_IDX_2R_5G,
	CCUT_IDX_NR,
};

struct cca_ccut {
	u32 reg82c[CCUT_IDX_NR];
	u32 reg830[CCUT_IDX_NR];
	u32 reg838[CCUT_IDX_NR];
};

static const struct cca_ccut cca_ifem_ccut = {
	{0x75C97010, 0x75C97010, 0x75C97010, 0x75C97010}, /*Reg82C*/
	{0x79a0eaaa, 0x79A0EAAC, 0x79a0eaaa, 0x79a0eaaa}, /*Reg830*/
	{0x87765541, 0x87746341, 0x87765541, 0x87746341}, /*Reg838*/
};

static const struct cca_ccut cca_efem_ccut = {
	{0x75B86010, 0x75B76010, 0x75B86010, 0x75B76010}, /*Reg82C*/
	{0x79A0EAA8, 0x79A0EAAC, 0x79A0EAA8, 0x79a0eaaa}, /*Reg830*/
	{0x87766451, 0x87766431, 0x87766451, 0x87766431}, /*Reg838*/
};

static const struct cca_ccut cca_ifem_ccut_ext = {
	{0x75da8010, 0x75da8010, 0x75da8010, 0x75da8010}, /*Reg82C*/
	{0x79a0eaaa, 0x97A0EAAC, 0x79a0eaaa, 0x79a0eaaa}, /*Reg830*/
	{0x87765541, 0x86666341, 0x87765561, 0x86666361}, /*Reg838*/
};

static void rtw8822b_get_cca_val(const struct cca_ccut *cca_ccut, u8 col,
				 u32 *reg82c, u32 *reg830, u32 *reg838)
{
	*reg82c = cca_ccut->reg82c[col];
	*reg830 = cca_ccut->reg830[col];
	*reg838 = cca_ccut->reg838[col];
}

struct rtw8822b_rfe_info {
	const struct cca_ccut *cca_ccut_2g;
	const struct cca_ccut *cca_ccut_5g;
	enum rtw_rfe_fem fem;
	bool ifem_ext;
	void (*rtw_set_channel_rfe)(struct rtw_dev *rtwdev, u8 channel);
};

#define I2GE5G_CCUT(set_ch) {						\
	.cca_ccut_2g = &cca_ifem_ccut,					\
	.cca_ccut_5g = &cca_efem_ccut,					\
	.fem = RTW_RFE_IFEM2G_EFEM5G,					\
	.ifem_ext = false,						\
	.rtw_set_channel_rfe = &rtw8822b_set_channel_rfe_ ## set_ch,	\
	}
#define IFEM_EXT_CCUT(set_ch) {						\
	.cca_ccut_2g = &cca_ifem_ccut_ext,				\
	.cca_ccut_5g = &cca_ifem_ccut_ext,				\
	.fem = RTW_RFE_IFEM,						\
	.ifem_ext = true,						\
	.rtw_set_channel_rfe = &rtw8822b_set_channel_rfe_ ## set_ch,	\
	}

static const struct rtw8822b_rfe_info rtw8822b_rfe_info[] = {
	[2] = I2GE5G_CCUT(efem),
	[3] = IFEM_EXT_CCUT(ifem),
	[5] = IFEM_EXT_CCUT(ifem),
};

static void rtw8822b_set_channel_cca(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				     const struct rtw8822b_rfe_info *rfe_info)
{
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	const struct cca_ccut *cca_ccut;
	u8 col;
	u32 reg82c, reg830, reg838;
	bool is_efem_cca = false, is_ifem_cca = false, is_rfe_type = false;

	if (IS_CH_2G_BAND(channel)) {
		cca_ccut = rfe_info->cca_ccut_2g;

		if (hal->antenna_rx == BB_PATH_A ||
		    hal->antenna_rx == BB_PATH_B)
			col = CCUT_IDX_1R_2G;
		else
			col = CCUT_IDX_2R_2G;
	} else {
		cca_ccut = rfe_info->cca_ccut_5g;

		if (hal->antenna_rx == BB_PATH_A ||
		    hal->antenna_rx == BB_PATH_B)
			col = CCUT_IDX_1R_5G;
		else
			col = CCUT_IDX_2R_5G;
	}

	rtw8822b_get_cca_val(cca_ccut, col, &reg82c, &reg830, &reg838);

	switch (rfe_info->fem) {
	case RTW_RFE_IFEM:
	default:
		is_ifem_cca = true;
		if (rfe_info->ifem_ext)
			is_rfe_type = true;
		break;
	case RTW_RFE_EFEM:
		is_efem_cca = true;
		break;
	case RTW_RFE_IFEM2G_EFEM5G:
		if (IS_CH_2G_BAND(channel))
			is_ifem_cca = true;
		else
			is_efem_cca = true;
		break;
	}

	if (is_ifem_cca) {
		if ((hal->cut_version == RTW_CHIP_VER_CUT_B &&
		     (col == CCUT_IDX_2R_2G || col == CCUT_IDX_2R_5G) &&
		     bw == RTW_CHANNEL_WIDTH_40) ||
		    (!is_rfe_type && col == CCUT_IDX_2R_5G &&
		     bw == RTW_CHANNEL_WIDTH_40) ||
		    (efuse->rfe_option == 5 && col == CCUT_IDX_2R_5G))
			reg830 = 0x79a0ea28;
	}

	rtw_write32_mask(rtwdev, REG_CCASEL, MASKDWORD, reg82c);
	rtw_write32_mask(rtwdev, REG_PDMFTH, MASKDWORD, reg830);
	rtw_write32_mask(rtwdev, REG_CCA2ND, MASKDWORD, reg838);

	if (is_efem_cca && !(hal->cut_version == RTW_CHIP_VER_CUT_B))
		rtw_write32_mask(rtwdev, REG_L1WT, MASKDWORD, 0x9194b2b9);

	if (bw == RTW_CHANNEL_WIDTH_20 && IS_CH_5G_BAND_MID(channel))
		rtw_write32_mask(rtwdev, REG_CCA2ND, 0xf0, 0x4);
}

static const u8 low_band[15] = {0x7, 0x6, 0x6, 0x5, 0x0, 0x0, 0x7, 0xff, 0x6,
				0x5, 0x0, 0x0, 0x7, 0x6, 0x6};
static const u8 middle_band[23] = {0x6, 0x5, 0x0, 0x0, 0x7, 0x6, 0x6, 0xff, 0x0,
				   0x0, 0x7, 0x6, 0x6, 0x5, 0x0, 0xff, 0x7, 0x6,
				   0x6, 0x5, 0x0, 0x0, 0x7};
static const u8 high_band[15] = {0x5, 0x5, 0x0, 0x7, 0x7, 0x6, 0x5, 0xff, 0x0,
				 0x7, 0x7, 0x6, 0x5, 0x5, 0x0};

static void rtw8822b_set_channel_rf(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
#define RF18_BAND_MASK		(BIT(16) | BIT(9) | BIT(8))
#define RF18_BAND_2G		(0)
#define RF18_BAND_5G		(BIT(16) | BIT(8))
#define RF18_CHANNEL_MASK	(MASKBYTE0)
#define RF18_RFSI_MASK		(BIT(18) | BIT(17))
#define RF18_RFSI_GE_CH80	(BIT(17))
#define RF18_RFSI_GT_CH144	(BIT(18))
#define RF18_BW_MASK		(BIT(11) | BIT(10))
#define RF18_BW_20M		(BIT(11) | BIT(10))
#define RF18_BW_40M		(BIT(11))
#define RF18_BW_80M		(BIT(10))
#define RFBE_MASK		(BIT(17) | BIT(16) | BIT(15))

	struct rtw_hal *hal = &rtwdev->hal;
	u32 rf_reg18, rf_reg_be;

	rf_reg18 = rtw_read_rf(rtwdev, RF_PATH_A, 0x18, RFREG_MASK);

	rf_reg18 &= ~(RF18_BAND_MASK | RF18_CHANNEL_MASK | RF18_RFSI_MASK |
		      RF18_BW_MASK);

	rf_reg18 |= (IS_CH_2G_BAND(channel) ? RF18_BAND_2G : RF18_BAND_5G);
	rf_reg18 |= (channel & RF18_CHANNEL_MASK);
	if (channel > 144)
		rf_reg18 |= RF18_RFSI_GT_CH144;
	else if (channel >= 80)
		rf_reg18 |= RF18_RFSI_GE_CH80;

	switch (bw) {
	case RTW_CHANNEL_WIDTH_5:
	case RTW_CHANNEL_WIDTH_10:
	case RTW_CHANNEL_WIDTH_20:
	default:
		rf_reg18 |= RF18_BW_20M;
		break;
	case RTW_CHANNEL_WIDTH_40:
		rf_reg18 |= RF18_BW_40M;
		break;
	case RTW_CHANNEL_WIDTH_80:
		rf_reg18 |= RF18_BW_80M;
		break;
	}

	if (IS_CH_2G_BAND(channel))
		rf_reg_be = 0x0;
	else if (IS_CH_5G_BAND_1(channel) || IS_CH_5G_BAND_2(channel))
		rf_reg_be = low_band[(channel - 36) >> 1];
	else if (IS_CH_5G_BAND_3(channel))
		rf_reg_be = middle_band[(channel - 100) >> 1];
	else if (IS_CH_5G_BAND_4(channel))
		rf_reg_be = high_band[(channel - 149) >> 1];
	else
		goto err;

	rtw_write_rf(rtwdev, RF_PATH_A, RF_MALSEL, RFBE_MASK, rf_reg_be);

	/* need to set 0xdf[18]=1 before writing RF18 when channel 144 */
	if (channel == 144)
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTDBG, BIT(18), 0x1);
	else
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTDBG, BIT(18), 0x0);

	rtw_write_rf(rtwdev, RF_PATH_A, 0x18, RFREG_MASK, rf_reg18);
	if (hal->rf_type > RF_1T1R)
		rtw_write_rf(rtwdev, RF_PATH_B, 0x18, RFREG_MASK, rf_reg18);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_XTALX2, BIT(19), 0);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_XTALX2, BIT(19), 1);

	return;

err:
	WARN_ON(1);
}

static void rtw8822b_toggle_igi(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u32 igi;

	igi = rtw_read32_mask(rtwdev, REG_RXIGI_A, 0x7f);
	rtw_write32_mask(rtwdev, REG_RXIGI_A, 0x7f, igi - 2);
	rtw_write32_mask(rtwdev, REG_RXIGI_A, 0x7f, igi);
	rtw_write32_mask(rtwdev, REG_RXIGI_B, 0x7f, igi - 2);
	rtw_write32_mask(rtwdev, REG_RXIGI_B, 0x7f, igi);

	rtw_write32_mask(rtwdev, REG_RXPSEL, MASKBYTE0, 0x0);
	rtw_write32_mask(rtwdev, REG_RXPSEL, MASKBYTE0,
			 hal->antenna_rx | (hal->antenna_rx << 4));
}

static void rtw8822b_set_channel_rxdfir(struct rtw_dev *rtwdev, u8 bw)
{
	if (bw == RTW_CHANNEL_WIDTH_40) {
		/* RX DFIR for BW40 */
		rtw_write32_mask(rtwdev, REG_ACBB0, BIT(29) | BIT(28), 0x1);
		rtw_write32_mask(rtwdev, REG_ACBBRXFIR, BIT(29) | BIT(28), 0x0);
		rtw_write32s_mask(rtwdev, REG_TXDFIR, BIT(31), 0x0);
	} else if (bw == RTW_CHANNEL_WIDTH_80) {
		/* RX DFIR for BW80 */
		rtw_write32_mask(rtwdev, REG_ACBB0, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_ACBBRXFIR, BIT(29) | BIT(28), 0x1);
		rtw_write32s_mask(rtwdev, REG_TXDFIR, BIT(31), 0x0);
	} else {
		/* RX DFIR for BW20, BW10 and BW5*/
		rtw_write32_mask(rtwdev, REG_ACBB0, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_ACBBRXFIR, BIT(29) | BIT(28), 0x2);
		rtw_write32s_mask(rtwdev, REG_TXDFIR, BIT(31), 0x1);
	}
}

static void rtw8822b_set_channel_bb(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				    u8 primary_ch_idx)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u8 rfe_option = efuse->rfe_option;
	u32 val32;

	if (IS_CH_2G_BAND(channel)) {
		rtw_write32_mask(rtwdev, REG_RXPSEL, BIT(28), 0x1);
		rtw_write32_mask(rtwdev, REG_CCK_CHECK, BIT(7), 0x0);
		rtw_write32_mask(rtwdev, REG_ENTXCCK, BIT(18), 0x0);
		rtw_write32_mask(rtwdev, REG_RXCCAMSK, 0x0000FC00, 15);

		rtw_write32_mask(rtwdev, REG_ACGG2TBL, 0x1f, 0x0);
		rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x96a);
		if (channel == 14) {
			rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD, 0x00006577);
			rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD, 0x0000);
		} else {
			rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD, 0x384f6577);
			rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD, 0x1525);
		}

		rtw_write32_mask(rtwdev, REG_RFEINV, 0x300, 0x2);
	} else if (IS_CH_5G_BAND(channel)) {
		rtw_write32_mask(rtwdev, REG_ENTXCCK, BIT(18), 0x1);
		rtw_write32_mask(rtwdev, REG_CCK_CHECK, BIT(7), 0x1);
		rtw_write32_mask(rtwdev, REG_RXPSEL, BIT(28), 0x0);
		rtw_write32_mask(rtwdev, REG_RXCCAMSK, 0x0000FC00, 34);

		if (IS_CH_5G_BAND_1(channel) || IS_CH_5G_BAND_2(channel))
			rtw_write32_mask(rtwdev, REG_ACGG2TBL, 0x1f, 0x1);
		else if (IS_CH_5G_BAND_3(channel))
			rtw_write32_mask(rtwdev, REG_ACGG2TBL, 0x1f, 0x2);
		else if (IS_CH_5G_BAND_4(channel))
			rtw_write32_mask(rtwdev, REG_ACGG2TBL, 0x1f, 0x3);

		if (IS_CH_5G_BAND_1(channel))
			rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x494);
		else if (IS_CH_5G_BAND_2(channel))
			rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x453);
		else if (channel >= 100 && channel <= 116)
			rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x452);
		else if (channel >= 118 && channel <= 177)
			rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x412);

		rtw_write32_mask(rtwdev, 0xcbc, 0x300, 0x1);
	}

	switch (bw) {
	case RTW_CHANNEL_WIDTH_20:
	default:
		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xFFCFFC00;
		val32 |= (RTW_CHANNEL_WIDTH_20);
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x1);
		break;
	case RTW_CHANNEL_WIDTH_40:
		if (primary_ch_idx == RTW_SC_20_UPPER)
			rtw_write32_set(rtwdev, REG_RXSB, BIT(4));
		else
			rtw_write32_clr(rtwdev, REG_RXSB, BIT(4));

		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xFF3FF300;
		val32 |= (((primary_ch_idx & 0xf) << 2) | RTW_CHANNEL_WIDTH_40);
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x1);
		break;
	case RTW_CHANNEL_WIDTH_80:
		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xFCEFCF00;
		val32 |= (((primary_ch_idx & 0xf) << 2) | RTW_CHANNEL_WIDTH_80);
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x1);

		if (rfe_option == 2 || rfe_option == 3) {
			rtw_write32_mask(rtwdev, REG_L1PKWT, 0x0000f000, 0x6);
			rtw_write32_mask(rtwdev, REG_ADC40, BIT(10), 0x1);
		}
		break;
	case RTW_CHANNEL_WIDTH_5:
		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xEFEEFE00;
		val32 |= ((BIT(6) | RTW_CHANNEL_WIDTH_20));
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x0);
		rtw_write32_mask(rtwdev, REG_ADC40, BIT(31), 0x1);
		break;
	case RTW_CHANNEL_WIDTH_10:
		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xEFFEFF00;
		val32 |= ((BIT(7) | RTW_CHANNEL_WIDTH_20));
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x0);
		rtw_write32_mask(rtwdev, REG_ADC40, BIT(31), 0x1);
		break;
	}
}

static void rtw8822b_set_channel(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				 u8 primary_chan_idx)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	const struct rtw8822b_rfe_info *rfe_info;

	if (WARN(efuse->rfe_option >= ARRAY_SIZE(rtw8822b_rfe_info),
		 "rfe_option %d is out of boundary\n", efuse->rfe_option))
		return;

	rfe_info = &rtw8822b_rfe_info[efuse->rfe_option];

	rtw8822b_set_channel_bb(rtwdev, channel, bw, primary_chan_idx);
	rtw_set_channel_mac(rtwdev, channel, bw, primary_chan_idx);
	rtw8822b_set_channel_rf(rtwdev, channel, bw);
	rtw8822b_set_channel_rxdfir(rtwdev, bw);
	rtw8822b_toggle_igi(rtwdev);
	rtw8822b_set_channel_cca(rtwdev, channel, bw, rfe_info);
	(*rfe_info->rtw_set_channel_rfe)(rtwdev, channel);
}

static void rtw8822b_config_trx_mode(struct rtw_dev *rtwdev, u8 tx_path,
				     u8 rx_path, bool is_tx2_path)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	const struct rtw8822b_rfe_info *rfe_info;
	u8 ch = rtwdev->hal.current_channel;
	u8 tx_path_sel, rx_path_sel;
	int counter;

	if (WARN(efuse->rfe_option >= ARRAY_SIZE(rtw8822b_rfe_info),
		 "rfe_option %d is out of boundary\n", efuse->rfe_option))
		return;

	rfe_info = &rtw8822b_rfe_info[efuse->rfe_option];

	if ((tx_path | rx_path) & BB_PATH_A)
		rtw_write32_mask(rtwdev, REG_AGCTR_A, MASKLWORD, 0x3231);
	else
		rtw_write32_mask(rtwdev, REG_AGCTR_A, MASKLWORD, 0x1111);

	if ((tx_path | rx_path) & BB_PATH_B)
		rtw_write32_mask(rtwdev, REG_AGCTR_B, MASKLWORD, 0x3231);
	else
		rtw_write32_mask(rtwdev, REG_AGCTR_B, MASKLWORD, 0x1111);

	rtw_write32_mask(rtwdev, REG_CDDTXP, (BIT(19) | BIT(18)), 0x3);
	rtw_write32_mask(rtwdev, REG_TXPSEL, (BIT(29) | BIT(28)), 0x1);
	rtw_write32_mask(rtwdev, REG_TXPSEL, BIT(30), 0x1);

	if (tx_path & BB_PATH_A) {
		rtw_write32_mask(rtwdev, REG_CDDTXP, 0xfff00000, 0x001);
		rtw_write32_mask(rtwdev, REG_ADCINI, 0xf0000000, 0x8);
	} else if (tx_path & BB_PATH_B) {
		rtw_write32_mask(rtwdev, REG_CDDTXP, 0xfff00000, 0x002);
		rtw_write32_mask(rtwdev, REG_ADCINI, 0xf0000000, 0x4);
	}

	if (tx_path == BB_PATH_A || tx_path == BB_PATH_B)
		rtw_write32_mask(rtwdev, REG_TXPSEL1, 0xfff0, 0x01);
	else
		rtw_write32_mask(rtwdev, REG_TXPSEL1, 0xfff0, 0x43);

	tx_path_sel = (tx_path << 4) | tx_path;
	rtw_write32_mask(rtwdev, REG_TXPSEL, MASKBYTE0, tx_path_sel);

	if (tx_path != BB_PATH_A && tx_path != BB_PATH_B) {
		if (is_tx2_path || rtwdev->mp_mode) {
			rtw_write32_mask(rtwdev, REG_CDDTXP, 0xfff00000, 0x043);
			rtw_write32_mask(rtwdev, REG_ADCINI, 0xf0000000, 0xc);
		}
	}

	rtw_write32_mask(rtwdev, REG_RXDESC, BIT(22), 0x0);
	rtw_write32_mask(rtwdev, REG_RXDESC, BIT(18), 0x0);

	if (rx_path & BB_PATH_A)
		rtw_write32_mask(rtwdev, REG_ADCINI, 0x0f000000, 0x0);
	else if (rx_path & BB_PATH_B)
		rtw_write32_mask(rtwdev, REG_ADCINI, 0x0f000000, 0x5);

	rx_path_sel = (rx_path << 4) | rx_path;
	rtw_write32_mask(rtwdev, REG_RXPSEL, MASKBYTE0, rx_path_sel);

	if (rx_path == BB_PATH_A || rx_path == BB_PATH_B) {
		rtw_write32_mask(rtwdev, REG_ANTWT, BIT(16), 0x0);
		rtw_write32_mask(rtwdev, REG_HTSTFWT, BIT(28), 0x0);
		rtw_write32_mask(rtwdev, REG_MRC, BIT(23), 0x0);
	} else {
		rtw_write32_mask(rtwdev, REG_ANTWT, BIT(16), 0x1);
		rtw_write32_mask(rtwdev, REG_HTSTFWT, BIT(28), 0x1);
		rtw_write32_mask(rtwdev, REG_MRC, BIT(23), 0x1);
	}

	for (counter = 100; counter > 0; counter--) {
		u32 rf_reg33;

		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, RFREG_MASK, 0x80000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x00001);

		udelay(2);
		rf_reg33 = rtw_read_rf(rtwdev, RF_PATH_A, 0x33, RFREG_MASK);

		if (rf_reg33 == 0x00001)
			break;
	}

	if (WARN(counter <= 0, "write RF mode table fail\n"))
		return;

	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, RFREG_MASK, 0x80000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x00001);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD1, RFREG_MASK, 0x00034);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x4080c);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, RFREG_MASK, 0x00000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, RFREG_MASK, 0x00000);

	rtw8822b_toggle_igi(rtwdev);
	rtw8822b_set_channel_cca(rtwdev, 1, RTW_CHANNEL_WIDTH_20, rfe_info);
	(*rfe_info->rtw_set_channel_rfe)(rtwdev, ch);
}

static void query_phy_status_page0(struct rtw_dev *rtwdev, u8 *phy_status,
				   struct rtw_rx_pkt_stat *pkt_stat)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s8 min_rx_power = -120;
	u8 pwdb = GET_PHY_STAT_P0_PWDB(phy_status);

	/* 8822B uses only 1 antenna to RX CCK rates */
	pkt_stat->rx_power[RF_PATH_A] = pwdb - 110;
	pkt_stat->rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power, 1);
	pkt_stat->bw = RTW_CHANNEL_WIDTH_20;
	pkt_stat->signal_power = max(pkt_stat->rx_power[RF_PATH_A],
				     min_rx_power);
	dm_info->rssi[RF_PATH_A] = pkt_stat->rssi;
}

static void query_phy_status_page1(struct rtw_dev *rtwdev, u8 *phy_status,
				   struct rtw_rx_pkt_stat *pkt_stat)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 rxsc, bw;
	s8 min_rx_power = -120;
	s8 rx_evm;
	u8 evm_dbm = 0;
	u8 rssi;
	int path;

	if (pkt_stat->rate > DESC_RATE11M && pkt_stat->rate < DESC_RATEMCS0)
		rxsc = GET_PHY_STAT_P1_L_RXSC(phy_status);
	else
		rxsc = GET_PHY_STAT_P1_HT_RXSC(phy_status);

	if (rxsc >= 1 && rxsc <= 8)
		bw = RTW_CHANNEL_WIDTH_20;
	else if (rxsc >= 9 && rxsc <= 12)
		bw = RTW_CHANNEL_WIDTH_40;
	else if (rxsc >= 13)
		bw = RTW_CHANNEL_WIDTH_80;
	else
		bw = GET_PHY_STAT_P1_RF_MODE(phy_status);

	pkt_stat->rx_power[RF_PATH_A] = GET_PHY_STAT_P1_PWDB_A(phy_status) - 110;
	pkt_stat->rx_power[RF_PATH_B] = GET_PHY_STAT_P1_PWDB_B(phy_status) - 110;
	pkt_stat->rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power, 2);
	pkt_stat->bw = bw;
	pkt_stat->signal_power = max3(pkt_stat->rx_power[RF_PATH_A],
				      pkt_stat->rx_power[RF_PATH_B],
				      min_rx_power);

	dm_info->curr_rx_rate = pkt_stat->rate;

	pkt_stat->rx_evm[RF_PATH_A] = GET_PHY_STAT_P1_RXEVM_A(phy_status);
	pkt_stat->rx_evm[RF_PATH_B] = GET_PHY_STAT_P1_RXEVM_B(phy_status);

	pkt_stat->rx_snr[RF_PATH_A] = GET_PHY_STAT_P1_RXSNR_A(phy_status);
	pkt_stat->rx_snr[RF_PATH_B] = GET_PHY_STAT_P1_RXSNR_B(phy_status);

	pkt_stat->cfo_tail[RF_PATH_A] = GET_PHY_STAT_P1_CFO_TAIL_A(phy_status);
	pkt_stat->cfo_tail[RF_PATH_B] = GET_PHY_STAT_P1_CFO_TAIL_B(phy_status);

	for (path = 0; path <= rtwdev->hal.rf_path_num; path++) {
		rssi = rtw_phy_rf_power_2_rssi(&pkt_stat->rx_power[path], 1);
		dm_info->rssi[path] = rssi;
		dm_info->rx_snr[path] = pkt_stat->rx_snr[path] >> 1;
		dm_info->cfo_tail[path] = (pkt_stat->cfo_tail[path] * 5) >> 1;

		rx_evm = pkt_stat->rx_evm[path];

		if (rx_evm < 0) {
			if (rx_evm == S8_MIN)
				evm_dbm = 0;
			else
				evm_dbm = ((u8)-rx_evm >> 1);
		}
		dm_info->rx_evm_dbm[path] = evm_dbm;
	}
}

static void query_phy_status(struct rtw_dev *rtwdev, u8 *phy_status,
			     struct rtw_rx_pkt_stat *pkt_stat)
{
	u8 page;

	page = *phy_status & 0xf;

	switch (page) {
	case 0:
		query_phy_status_page0(rtwdev, phy_status, pkt_stat);
		break;
	case 1:
		query_phy_status_page1(rtwdev, phy_status, pkt_stat);
		break;
	default:
		rtw_warn(rtwdev, "unused phy status page (%d)\n", page);
		return;
	}
}

static void rtw8822b_query_rx_desc(struct rtw_dev *rtwdev, u8 *rx_desc,
				   struct rtw_rx_pkt_stat *pkt_stat,
				   struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_hdr *hdr;
	u32 desc_sz = rtwdev->chip->rx_pkt_desc_sz;
	u8 *phy_status = NULL;

	memset(pkt_stat, 0, sizeof(*pkt_stat));

	pkt_stat->phy_status = GET_RX_DESC_PHYST(rx_desc);
	pkt_stat->icv_err = GET_RX_DESC_ICV_ERR(rx_desc);
	pkt_stat->crc_err = GET_RX_DESC_CRC32(rx_desc);
	pkt_stat->decrypted = !GET_RX_DESC_SWDEC(rx_desc) &&
			      GET_RX_DESC_ENC_TYPE(rx_desc) != RX_DESC_ENC_NONE;
	pkt_stat->is_c2h = GET_RX_DESC_C2H(rx_desc);
	pkt_stat->pkt_len = GET_RX_DESC_PKT_LEN(rx_desc);
	pkt_stat->drv_info_sz = GET_RX_DESC_DRV_INFO_SIZE(rx_desc);
	pkt_stat->shift = GET_RX_DESC_SHIFT(rx_desc);
	pkt_stat->rate = GET_RX_DESC_RX_RATE(rx_desc);
	pkt_stat->cam_id = GET_RX_DESC_MACID(rx_desc);
	pkt_stat->ppdu_cnt = GET_RX_DESC_PPDU_CNT(rx_desc);
	pkt_stat->tsf_low = GET_RX_DESC_TSFL(rx_desc);

	/* drv_info_sz is in unit of 8-bytes */
	pkt_stat->drv_info_sz *= 8;

	/* c2h cmd pkt's rx/phy status is not interested */
	if (pkt_stat->is_c2h)
		return;

	hdr = (struct ieee80211_hdr *)(rx_desc + desc_sz + pkt_stat->shift +
				       pkt_stat->drv_info_sz);
	if (pkt_stat->phy_status) {
		phy_status = rx_desc + desc_sz + pkt_stat->shift;
		query_phy_status(rtwdev, phy_status, pkt_stat);
	}

	rtw_rx_fill_rx_status(rtwdev, pkt_stat, hdr, rx_status, phy_status);
}

static void
rtw8822b_set_tx_power_index_by_rate(struct rtw_dev *rtwdev, u8 path, u8 rs)
{
	struct rtw_hal *hal = &rtwdev->hal;
	static const u32 offset_txagc[2] = {0x1d00, 0x1d80};
	static u32 phy_pwr_idx;
	u8 rate, rate_idx, pwr_index, shift;
	int j;

	for (j = 0; j < rtw_rate_size[rs]; j++) {
		rate = rtw_rate_section[rs][j];
		pwr_index = hal->tx_pwr_tbl[path][rate];
		shift = rate & 0x3;
		phy_pwr_idx |= ((u32)pwr_index << (shift * 8));
		if (shift == 0x3) {
			rate_idx = rate & 0xfc;
			rtw_write32(rtwdev, offset_txagc[path] + rate_idx,
				    phy_pwr_idx);
			phy_pwr_idx = 0;
		}
	}
}

static void rtw8822b_set_tx_power_index(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	int rs, path;

	for (path = 0; path < hal->rf_path_num; path++) {
		for (rs = 0; rs < RTW_RATE_SECTION_MAX; rs++)
			rtw8822b_set_tx_power_index_by_rate(rtwdev, path, rs);
	}
}

static bool rtw8822b_check_rf_path(u8 antenna)
{
	switch (antenna) {
	case BB_PATH_A:
	case BB_PATH_B:
	case BB_PATH_AB:
		return true;
	default:
		return false;
	}
}

static int rtw8822b_set_antenna(struct rtw_dev *rtwdev,
				u32 antenna_tx,
				u32 antenna_rx)
{
	struct rtw_hal *hal = &rtwdev->hal;

	rtw_dbg(rtwdev, RTW_DBG_PHY, "config RF path, tx=0x%x rx=0x%x\n",
		antenna_tx, antenna_rx);

	if (!rtw8822b_check_rf_path(antenna_tx)) {
		rtw_info(rtwdev, "unsupported tx path 0x%x\n", antenna_tx);
		return -EINVAL;
	}

	if (!rtw8822b_check_rf_path(antenna_rx)) {
		rtw_info(rtwdev, "unsupported rx path 0x%x\n", antenna_rx);
		return -EINVAL;
	}

	hal->antenna_tx = antenna_tx;
	hal->antenna_rx = antenna_rx;

	rtw8822b_config_trx_mode(rtwdev, antenna_tx, antenna_rx, false);

	return 0;
}

static void rtw8822b_cfg_ldo25(struct rtw_dev *rtwdev, bool enable)
{
	u8 ldo_pwr;

	ldo_pwr = rtw_read8(rtwdev, REG_LDO_EFUSE_CTRL + 3);
	ldo_pwr = enable ? ldo_pwr | BIT_LDO25_EN : ldo_pwr & ~BIT_LDO25_EN;
	rtw_write8(rtwdev, REG_LDO_EFUSE_CTRL + 3, ldo_pwr);
}

static void rtw8822b_false_alarm_statistics(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u32 cck_enable;
	u32 cck_fa_cnt;
	u32 ofdm_fa_cnt;
	u32 crc32_cnt;
	u32 cca32_cnt;

	cck_enable = rtw_read32(rtwdev, 0x808) & BIT(28);
	cck_fa_cnt = rtw_read16(rtwdev, 0xa5c);
	ofdm_fa_cnt = rtw_read16(rtwdev, 0xf48);

	dm_info->cck_fa_cnt = cck_fa_cnt;
	dm_info->ofdm_fa_cnt = ofdm_fa_cnt;
	dm_info->total_fa_cnt = ofdm_fa_cnt;
	dm_info->total_fa_cnt += cck_enable ? cck_fa_cnt : 0;

	crc32_cnt = rtw_read32(rtwdev, 0xf04);
	dm_info->cck_ok_cnt = crc32_cnt & 0xffff;
	dm_info->cck_err_cnt = (crc32_cnt & 0xffff0000) >> 16;
	crc32_cnt = rtw_read32(rtwdev, 0xf14);
	dm_info->ofdm_ok_cnt = crc32_cnt & 0xffff;
	dm_info->ofdm_err_cnt = (crc32_cnt & 0xffff0000) >> 16;
	crc32_cnt = rtw_read32(rtwdev, 0xf10);
	dm_info->ht_ok_cnt = crc32_cnt & 0xffff;
	dm_info->ht_err_cnt = (crc32_cnt & 0xffff0000) >> 16;
	crc32_cnt = rtw_read32(rtwdev, 0xf0c);
	dm_info->vht_ok_cnt = crc32_cnt & 0xffff;
	dm_info->vht_err_cnt = (crc32_cnt & 0xffff0000) >> 16;

	cca32_cnt = rtw_read32(rtwdev, 0xf08);
	dm_info->ofdm_cca_cnt = ((cca32_cnt & 0xffff0000) >> 16);
	dm_info->total_cca_cnt = dm_info->ofdm_cca_cnt;
	if (cck_enable) {
		cca32_cnt = rtw_read32(rtwdev, 0xfcc);
		dm_info->cck_cca_cnt = cca32_cnt & 0xffff;
		dm_info->total_cca_cnt += dm_info->cck_cca_cnt;
	}

	rtw_write32_set(rtwdev, 0x9a4, BIT(17));
	rtw_write32_clr(rtwdev, 0x9a4, BIT(17));
	rtw_write32_clr(rtwdev, 0xa2c, BIT(15));
	rtw_write32_set(rtwdev, 0xa2c, BIT(15));
	rtw_write32_set(rtwdev, 0xb58, BIT(0));
	rtw_write32_clr(rtwdev, 0xb58, BIT(0));
}

static void rtw8822b_do_iqk(struct rtw_dev *rtwdev)
{
	static int do_iqk_cnt;
	struct rtw_iqk_para para = {.clear = 0, .segment_iqk = 0};
	u32 rf_reg, iqk_fail_mask;
	int counter;
	bool reload;

	rtw_fw_do_iqk(rtwdev, &para);

	for (counter = 0; counter < 300; counter++) {
		rf_reg = rtw_read_rf(rtwdev, RF_PATH_A, RF_DTXLOK, RFREG_MASK);
		if (rf_reg == 0xabcde)
			break;
		msleep(20);
	}
	rtw_write_rf(rtwdev, RF_PATH_A, RF_DTXLOK, RFREG_MASK, 0x0);

	reload = !!rtw_read32_mask(rtwdev, REG_IQKFAILMSK, BIT(16));
	iqk_fail_mask = rtw_read32_mask(rtwdev, REG_IQKFAILMSK, GENMASK(7, 0));
	rtw_dbg(rtwdev, RTW_DBG_PHY,
		"iqk counter=%d reload=%d do_iqk_cnt=%d n_iqk_fail(mask)=0x%02x\n",
		counter, reload, ++do_iqk_cnt, iqk_fail_mask);
}

static void rtw8822b_phy_calibration(struct rtw_dev *rtwdev)
{
	rtw8822b_do_iqk(rtwdev);
}

static void rtw8822b_coex_cfg_init(struct rtw_dev *rtwdev)
{
	/* enable TBTT nterrupt */
	rtw_write8_set(rtwdev, REG_BCN_CTRL, BIT_EN_BCN_FUNCTION);

	/* BT report packet sample rate */
	/* 0x790[5:0]=0x5 */
	rtw_write8_mask(rtwdev, REG_BT_TDMA_TIME, BIT_MASK_SAMPLE_RATE, 0x5);

	/* enable BT counter statistics */
	rtw_write8(rtwdev, REG_BT_STAT_CTRL, 0x1);

	/* enable PTA (3-wire function form BT side) */
	rtw_write32_set(rtwdev, REG_GPIO_MUXCFG, BIT_BT_PTA_EN);
	rtw_write32_set(rtwdev, REG_GPIO_MUXCFG, BIT_PO_BT_PTA_PINS);

	/* enable PTA (tx/rx signal form WiFi side) */
	rtw_write8_set(rtwdev, REG_QUEUE_CTRL, BIT_PTA_WL_TX_EN);
	/* wl tx signal to PTA not case EDCCA */
	rtw_write8_clr(rtwdev, REG_QUEUE_CTRL, BIT_PTA_EDCCA_EN);
	/* GNT_BT=1 while select both */
	rtw_write16_set(rtwdev, REG_BT_COEX_V2, BIT_GNT_BT_POLARITY);
}

static void rtw8822b_coex_cfg_ant_switch(struct rtw_dev *rtwdev,
					 u8 ctrl_type, u8 pos_type)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	struct rtw_coex_rfe *coex_rfe = &coex->rfe;
	bool polarity_inverse;
	u8 regval = 0;

	if (((ctrl_type << 8) + pos_type) == coex_dm->cur_switch_status)
		return;

	coex_dm->cur_switch_status = (ctrl_type << 8) + pos_type;

	if (coex_rfe->ant_switch_diversity &&
	    ctrl_type == COEX_SWITCH_CTRL_BY_BBSW)
		ctrl_type = COEX_SWITCH_CTRL_BY_ANTDIV;

	polarity_inverse = (coex_rfe->ant_switch_polarity == 1);

	switch (ctrl_type) {
	default:
	case COEX_SWITCH_CTRL_BY_BBSW:
		/* 0x4c[23] = 0 */
		rtw_write8_mask(rtwdev, REG_LED_CFG + 2, BIT_DPDT_SEL_EN >> 16, 0x0);
		/* 0x4c[24] = 1 */
		rtw_write8_mask(rtwdev, REG_LED_CFG + 3, BIT_DPDT_WL_SEL >> 24, 0x1);
		/* BB SW, DPDT use RFE_ctrl8 and RFE_ctrl9 as ctrl pin */
		rtw_write8_mask(rtwdev, REG_RFE_CTRL8, BIT_MASK_RFE_SEL89, 0x77);

		if (pos_type == COEX_SWITCH_TO_WLG_BT) {
			if (coex_rfe->rfe_module_type != 0x4 &&
			    coex_rfe->rfe_module_type != 0x2)
				regval = 0x3;
			else
				regval = (!polarity_inverse ? 0x2 : 0x1);
		} else if (pos_type == COEX_SWITCH_TO_WLG) {
			regval = (!polarity_inverse ? 0x2 : 0x1);
		} else {
			regval = (!polarity_inverse ? 0x1 : 0x2);
		}

		rtw_write8_mask(rtwdev, REG_RFE_INV8, BIT_MASK_RFE_INV89, regval);
		break;
	case COEX_SWITCH_CTRL_BY_PTA:
		/* 0x4c[23] = 0 */
		rtw_write8_mask(rtwdev, REG_LED_CFG + 2, BIT_DPDT_SEL_EN >> 16, 0x0);
		/* 0x4c[24] = 1 */
		rtw_write8_mask(rtwdev, REG_LED_CFG + 3, BIT_DPDT_WL_SEL >> 24, 0x1);
		/* PTA,  DPDT use RFE_ctrl8 and RFE_ctrl9 as ctrl pin */
		rtw_write8_mask(rtwdev, REG_RFE_CTRL8, BIT_MASK_RFE_SEL89, 0x66);

		regval = (!polarity_inverse ? 0x2 : 0x1);
		rtw_write8_mask(rtwdev, REG_RFE_INV8, BIT_MASK_RFE_INV89, regval);
		break;
	case COEX_SWITCH_CTRL_BY_ANTDIV:
		/* 0x4c[23] = 0 */
		rtw_write8_mask(rtwdev, REG_LED_CFG + 2, BIT_DPDT_SEL_EN >> 16, 0x0);
		/* 0x4c[24] = 1 */
		rtw_write8_mask(rtwdev, REG_LED_CFG + 3, BIT_DPDT_WL_SEL >> 24, 0x1);
		rtw_write8_mask(rtwdev, REG_RFE_CTRL8, BIT_MASK_RFE_SEL89, 0x88);
		break;
	case COEX_SWITCH_CTRL_BY_MAC:
		/* 0x4c[23] = 1 */
		rtw_write8_mask(rtwdev, REG_LED_CFG + 2, BIT_DPDT_SEL_EN >> 16, 0x1);

		regval = (!polarity_inverse ? 0x0 : 0x1);
		rtw_write8_mask(rtwdev, REG_PAD_CTRL1, BIT_SW_DPDT_SEL_DATA, regval);
		break;
	case COEX_SWITCH_CTRL_BY_FW:
		/* 0x4c[23] = 0 */
		rtw_write8_mask(rtwdev, REG_LED_CFG + 2, BIT_DPDT_SEL_EN >> 16, 0x0);
		/* 0x4c[24] = 1 */
		rtw_write8_mask(rtwdev, REG_LED_CFG + 3, BIT_DPDT_WL_SEL >> 24, 0x1);
		break;
	case COEX_SWITCH_CTRL_BY_BT:
		/* 0x4c[23] = 0 */
		rtw_write8_mask(rtwdev, REG_LED_CFG + 2, BIT_DPDT_SEL_EN >> 16, 0x0);
		/* 0x4c[24] = 0 */
		rtw_write8_mask(rtwdev, REG_LED_CFG + 3, BIT_DPDT_WL_SEL >> 24, 0x0);
		break;
	}
}

static void rtw8822b_coex_cfg_gnt_fix(struct rtw_dev *rtwdev)
{
}

static void rtw8822b_coex_cfg_gnt_debug(struct rtw_dev *rtwdev)
{
	rtw_write8_mask(rtwdev, REG_PAD_CTRL1 + 2, BIT_BTGP_SPI_EN >> 16, 0);
	rtw_write8_mask(rtwdev, REG_PAD_CTRL1 + 3, BIT_BTGP_JTAG_EN >> 24, 0);
	rtw_write8_mask(rtwdev, REG_GPIO_MUXCFG + 2, BIT_FSPI_EN >> 16, 0);
	rtw_write8_mask(rtwdev, REG_PAD_CTRL1 + 1, BIT_LED1DIS >> 8, 0);
	rtw_write8_mask(rtwdev, REG_SYS_SDIO_CTRL + 3, BIT_DBG_GNT_WL_BT >> 24, 0);
}

static void rtw8822b_coex_cfg_rfe_type(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_rfe *coex_rfe = &coex->rfe;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	bool is_ext_fem = false;

	coex_rfe->rfe_module_type = rtwdev->efuse.rfe_option;
	coex_rfe->ant_switch_polarity = 0;
	coex_rfe->ant_switch_diversity = false;
	if (coex_rfe->rfe_module_type == 0x12 ||
	    coex_rfe->rfe_module_type == 0x15 ||
	    coex_rfe->rfe_module_type == 0x16)
		coex_rfe->ant_switch_exist = false;
	else
		coex_rfe->ant_switch_exist = true;

	if (coex_rfe->rfe_module_type == 2 ||
	    coex_rfe->rfe_module_type == 4) {
		rtw_coex_write_scbd(rtwdev, COEX_SCBD_EXTFEM, true);
		is_ext_fem = true;
	} else {
		rtw_coex_write_scbd(rtwdev, COEX_SCBD_EXTFEM, false);
	}

	coex_rfe->wlg_at_btg = false;

	if (efuse->share_ant &&
	    coex_rfe->ant_switch_exist && !is_ext_fem)
		coex_rfe->ant_switch_with_bt = true;
	else
		coex_rfe->ant_switch_with_bt = false;

	/* Ext switch buffer mux */
	rtw_write8(rtwdev, REG_RFE_CTRL_E, 0xff);
	rtw_write8_mask(rtwdev, REG_RFESEL_CTRL + 1, 0x3, 0x0);
	rtw_write8_mask(rtwdev, REG_RFE_INV16, BIT_RFE_BUF_EN, 0x0);

	/* Disable LTE Coex Function in WiFi side */
	rtw_coex_write_indirect_reg(rtwdev, LTE_COEX_CTRL, BIT_LTE_COEX_EN, 0);

	/* BTC_CTT_WL_VS_LTE */
	rtw_coex_write_indirect_reg(rtwdev, LTE_WL_TRX_CTRL, MASKLWORD, 0xffff);

	/* BTC_CTT_BT_VS_LTE */
	rtw_coex_write_indirect_reg(rtwdev, LTE_BT_TRX_CTRL, MASKLWORD, 0xffff);
}

static void rtw8822b_coex_cfg_wl_tx_power(struct rtw_dev *rtwdev, u8 wl_pwr)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	static const u16 reg_addr[] = {0xc58, 0xe58};
	static const u8	wl_tx_power[] = {0xd8, 0xd4, 0xd0, 0xcc, 0xc8};
	u8 i, pwr;

	if (wl_pwr == coex_dm->cur_wl_pwr_lvl)
		return;

	coex_dm->cur_wl_pwr_lvl = wl_pwr;

	if (coex_dm->cur_wl_pwr_lvl >= ARRAY_SIZE(wl_tx_power))
		coex_dm->cur_wl_pwr_lvl = ARRAY_SIZE(wl_tx_power) - 1;

	pwr = wl_tx_power[coex_dm->cur_wl_pwr_lvl];

	for (i = 0; i < ARRAY_SIZE(reg_addr); i++)
		rtw_write8_mask(rtwdev, reg_addr[i], 0xff, pwr);
}

static void rtw8822b_coex_cfg_wl_rx_gain(struct rtw_dev *rtwdev, bool low_gain)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	/* WL Rx Low gain on */
	static const u32 wl_rx_low_gain_on[] = {
		0xff000003, 0xbd120003, 0xbe100003, 0xbf080003, 0xbf060003,
		0xbf050003, 0xbc140003, 0xbb160003, 0xba180003, 0xb91a0003,
		0xb81c0003, 0xb71e0003, 0xb4200003, 0xb5220003, 0xb4240003,
		0xb3260003, 0xb2280003, 0xb12a0003, 0xb02c0003, 0xaf2e0003,
		0xae300003, 0xad320003, 0xac340003, 0xab360003, 0x8d380003,
		0x8c3a0003, 0x8b3c0003, 0x8a3e0003, 0x6e400003, 0x6d420003,
		0x6c440003, 0x6b460003, 0x6a480003, 0x694a0003, 0x684c0003,
		0x674e0003, 0x66500003, 0x65520003, 0x64540003, 0x64560003,
		0x007e0403
	};

	/* WL Rx Low gain off */
	static const u32 wl_rx_low_gain_off[] = {
		0xff000003, 0xf4120003, 0xf5100003, 0xf60e0003, 0xf70c0003,
		0xf80a0003, 0xf3140003, 0xf2160003, 0xf1180003, 0xf01a0003,
		0xef1c0003, 0xee1e0003, 0xed200003, 0xec220003, 0xeb240003,
		0xea260003, 0xe9280003, 0xe82a0003, 0xe72c0003, 0xe62e0003,
		0xe5300003, 0xc8320003, 0xc7340003, 0xc6360003, 0xc5380003,
		0xc43a0003, 0xc33c0003, 0xc23e0003, 0xc1400003, 0xc0420003,
		0xa5440003, 0xa4460003, 0xa3480003, 0xa24a0003, 0xa14c0003,
		0x834e0003, 0x82500003, 0x81520003, 0x80540003, 0x65560003,
		0x007e0403
	};
	u8 i;

	if (low_gain == coex_dm->cur_wl_rx_low_gain_en)
		return;

	coex_dm->cur_wl_rx_low_gain_en = low_gain;

	if (coex_dm->cur_wl_rx_low_gain_en) {
		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], Hi-Li Table On!\n");
		for (i = 0; i < ARRAY_SIZE(wl_rx_low_gain_on); i++)
			rtw_write32(rtwdev, REG_RX_GAIN_EN, wl_rx_low_gain_on[i]);

		/* set Rx filter corner RCK offset */
		rtw_write_rf(rtwdev, RF_PATH_A, RF_RCKD, 0x2, 0x1);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_RCK, 0x3f, 0x3f);
		rtw_write_rf(rtwdev, RF_PATH_B, RF_RCKD, 0x2, 0x1);
		rtw_write_rf(rtwdev, RF_PATH_B, RF_RCK, 0x3f, 0x3f);
	} else {
		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], Hi-Li Table Off!\n");
		for (i = 0; i < ARRAY_SIZE(wl_rx_low_gain_off); i++)
			rtw_write32(rtwdev, 0x81c, wl_rx_low_gain_off[i]);

		/* set Rx filter corner RCK offset */
		rtw_write_rf(rtwdev, RF_PATH_A, RF_RCK, 0x3f, 0x4);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_RCKD, 0x2, 0x0);
		rtw_write_rf(rtwdev, RF_PATH_B, RF_RCK, 0x3f, 0x4);
		rtw_write_rf(rtwdev, RF_PATH_B, RF_RCKD, 0x2, 0x0);
	}
}

static void rtw8822b_txagc_swing_offset(struct rtw_dev *rtwdev, u8 path,
					u8 tx_pwr_idx_offset,
					s8 *txagc_idx, u8 *swing_idx)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s8 delta_pwr_idx = dm_info->delta_power_index[path];
	u8 swing_upper_bound = dm_info->default_ofdm_index + 10;
	u8 swing_lower_bound = 0;
	u8 max_tx_pwr_idx_offset = 0xf;
	s8 agc_index = 0;
	u8 swing_index = dm_info->default_ofdm_index;

	tx_pwr_idx_offset = min_t(u8, tx_pwr_idx_offset, max_tx_pwr_idx_offset);

	if (delta_pwr_idx >= 0) {
		if (delta_pwr_idx <= tx_pwr_idx_offset) {
			agc_index = delta_pwr_idx;
			swing_index = dm_info->default_ofdm_index;
		} else if (delta_pwr_idx > tx_pwr_idx_offset) {
			agc_index = tx_pwr_idx_offset;
			swing_index = dm_info->default_ofdm_index +
					delta_pwr_idx - tx_pwr_idx_offset;
			swing_index = min_t(u8, swing_index, swing_upper_bound);
		}
	} else {
		if (dm_info->default_ofdm_index > abs(delta_pwr_idx))
			swing_index =
				dm_info->default_ofdm_index + delta_pwr_idx;
		else
			swing_index = swing_lower_bound;
		swing_index = max_t(u8, swing_index, swing_lower_bound);

		agc_index = 0;
	}

	if (swing_index >= RTW_TXSCALE_SIZE) {
		rtw_warn(rtwdev, "swing index overflow\n");
		swing_index = RTW_TXSCALE_SIZE - 1;
	}
	*txagc_idx = agc_index;
	*swing_idx = swing_index;
}

static void rtw8822b_pwrtrack_set_pwr(struct rtw_dev *rtwdev, u8 path,
				      u8 pwr_idx_offset)
{
	s8 txagc_idx;
	u8 swing_idx;
	u32 reg1, reg2;

	if (path == RF_PATH_A) {
		reg1 = 0xc94;
		reg2 = 0xc1c;
	} else if (path == RF_PATH_B) {
		reg1 = 0xe94;
		reg2 = 0xe1c;
	} else {
		return;
	}

	rtw8822b_txagc_swing_offset(rtwdev, path, pwr_idx_offset,
				    &txagc_idx, &swing_idx);
	rtw_write32_mask(rtwdev, reg1, GENMASK(29, 25), txagc_idx);
	rtw_write32_mask(rtwdev, reg2, GENMASK(31, 21),
			 rtw8822b_txscale_tbl[swing_idx]);
}

static void rtw8822b_pwrtrack_set(struct rtw_dev *rtwdev, u8 path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 pwr_idx_offset, tx_pwr_idx;
	u8 channel = rtwdev->hal.current_channel;
	u8 band_width = rtwdev->hal.current_band_width;
	u8 regd = rtw_regd_get(rtwdev);
	u8 tx_rate = dm_info->tx_rate;
	u8 max_pwr_idx = rtwdev->chip->max_power_index;

	tx_pwr_idx = rtw_phy_get_tx_power_index(rtwdev, path, tx_rate,
						band_width, channel, regd);

	tx_pwr_idx = min_t(u8, tx_pwr_idx, max_pwr_idx);

	pwr_idx_offset = max_pwr_idx - tx_pwr_idx;

	rtw8822b_pwrtrack_set_pwr(rtwdev, path, pwr_idx_offset);
}

static void rtw8822b_phy_pwrtrack_path(struct rtw_dev *rtwdev,
				       struct rtw_swing_table *swing_table,
				       u8 path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 power_idx_cur, power_idx_last;
	u8 delta;

	/* 8822B only has one thermal meter at PATH A */
	delta = rtw_phy_pwrtrack_get_delta(rtwdev, RF_PATH_A);

	power_idx_last = dm_info->delta_power_index[path];
	power_idx_cur = rtw_phy_pwrtrack_get_pwridx(rtwdev, swing_table,
						    path, RF_PATH_A, delta);

	/* if delta of power indexes are the same, just skip */
	if (power_idx_cur == power_idx_last)
		return;

	dm_info->delta_power_index[path] = power_idx_cur;
	rtw8822b_pwrtrack_set(rtwdev, path);
}

static void rtw8822b_phy_pwrtrack(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_swing_table swing_table;
	u8 thermal_value, path;

	rtw_phy_config_swing_table(rtwdev, &swing_table);

	if (rtwdev->efuse.thermal_meter[RF_PATH_A] == 0xff)
		return;

	thermal_value = rtw_read_rf(rtwdev, RF_PATH_A, RF_T_METER, 0xfc00);

	rtw_phy_pwrtrack_avg(rtwdev, thermal_value, RF_PATH_A);

	if (dm_info->pwr_trk_init_trigger)
		dm_info->pwr_trk_init_trigger = false;
	else if (!rtw_phy_pwrtrack_thermal_changed(rtwdev, thermal_value,
						   RF_PATH_A))
		goto iqk;

	for (path = 0; path < rtwdev->hal.rf_path_num; path++)
		rtw8822b_phy_pwrtrack_path(rtwdev, &swing_table, path);

iqk:
	if (rtw_phy_pwrtrack_need_iqk(rtwdev))
		rtw8822b_do_iqk(rtwdev);
}

static void rtw8822b_pwr_track(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	if (efuse->power_track_type != 0)
		return;

	if (!dm_info->pwr_trk_triggered) {
		rtw_write_rf(rtwdev, RF_PATH_A, RF_T_METER,
			     GENMASK(17, 16), 0x03);
		dm_info->pwr_trk_triggered = true;
		return;
	}

	rtw8822b_phy_pwrtrack(rtwdev);
	dm_info->pwr_trk_triggered = false;
}

static void rtw8822b_bf_config_bfee_su(struct rtw_dev *rtwdev,
				       struct rtw_vif *vif,
				       struct rtw_bfee *bfee, bool enable)
{
	if (enable)
		rtw_bf_enable_bfee_su(rtwdev, vif, bfee);
	else
		rtw_bf_remove_bfee_su(rtwdev, bfee);
}

static void rtw8822b_bf_config_bfee_mu(struct rtw_dev *rtwdev,
				       struct rtw_vif *vif,
				       struct rtw_bfee *bfee, bool enable)
{
	if (enable)
		rtw_bf_enable_bfee_mu(rtwdev, vif, bfee);
	else
		rtw_bf_remove_bfee_mu(rtwdev, bfee);
}

static void rtw8822b_bf_config_bfee(struct rtw_dev *rtwdev, struct rtw_vif *vif,
				    struct rtw_bfee *bfee, bool enable)
{
	if (bfee->role == RTW_BFEE_SU)
		rtw8822b_bf_config_bfee_su(rtwdev, vif, bfee, enable);
	else if (bfee->role == RTW_BFEE_MU)
		rtw8822b_bf_config_bfee_mu(rtwdev, vif, bfee, enable);
	else
		rtw_warn(rtwdev, "wrong bfee role\n");
}

static void rtw8822b_adaptivity_init(struct rtw_dev *rtwdev)
{
	rtw_phy_set_edcca_th(rtwdev, RTW8822B_EDCCA_MAX, RTW8822B_EDCCA_MAX);

	/* mac edcca state setting */
	rtw_write32_clr(rtwdev, REG_TX_PTCL_CTRL, BIT_DIS_EDCCA);
	rtw_write32_set(rtwdev, REG_RD_CTRL, BIT_EDCCA_MSK_CNTDOWN_EN);
	rtw_write32_mask(rtwdev, REG_EDCCA_SOURCE, BIT_SOURCE_OPTION,
			 RTW8822B_EDCCA_SRC_DEF);
	rtw_write32_mask(rtwdev, REG_EDCCA_POW_MA, BIT_MA_LEVEL, 0);

	/* edcca decision opt */
	rtw_write32_set(rtwdev, REG_EDCCA_DECISION, BIT_EDCCA_OPTION);
}

static void rtw8822b_adaptivity(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s8 l2h, h2l;
	u8 igi;

	igi = dm_info->igi_history[0];
	if (dm_info->edcca_mode == RTW_EDCCA_NORMAL) {
		l2h = max_t(s8, igi + EDCCA_IGI_L2H_DIFF, EDCCA_TH_L2H_LB);
		h2l = l2h - EDCCA_L2H_H2L_DIFF_NORMAL;
	} else {
		l2h = min_t(s8, igi, dm_info->l2h_th_ini);
		h2l = l2h - EDCCA_L2H_H2L_DIFF;
	}

	rtw_phy_set_edcca_th(rtwdev, l2h, h2l);
}

static const struct rtw_pwr_seq_cmd trans_carddis_to_cardemu_8822b[] = {
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
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4) | BIT(7), 0},
	{0x0300,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0x0301,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd trans_cardemu_to_act_8822b[] = {
	{0x0012,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0012,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0020,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
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
	{0xFF1A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
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
	{0x10C3,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
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
	{0x0020,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), BIT(3)},
	{0x10A8,
	 RTW_PWR_CUT_C_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0x10A9,
	 RTW_PWR_CUT_C_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0xef},
	{0x10AA,
	 RTW_PWR_CUT_C_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x0c},
	{0x0068,
	 RTW_PWR_CUT_C_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), BIT(4)},
	{0x0029,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0xF9},
	{0x0024,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), 0},
	{0x0074,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0x00AF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd trans_act_to_cardemu_8822b[] = {
	{0x0003,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), 0},
	{0x0093,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), 0},
	{0x001F,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0x00EF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0xFF1A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x30},
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
	{0x0002,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x10C3,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
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
	{0x0020,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), 0},
	{0x0000,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd trans_cardemu_to_carddis_8822b[] = {
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7), BIT(7)},
	{0x0007,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x20},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), BIT(2)},
	{0x004A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), 0},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), 0},
	{0x004F,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0046,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(6), BIT(6)},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), 0},
	{0x0046,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7), BIT(7)},
	{0x0062,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), BIT(4)},
	{0x0081,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7) | BIT(6), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3)},
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
	{0x0090,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0044,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0x0040,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x90},
	{0x0041,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x00},
	{0x0042,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x04},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd *card_enable_flow_8822b[] = {
	trans_carddis_to_cardemu_8822b,
	trans_cardemu_to_act_8822b,
	NULL
};

static const struct rtw_pwr_seq_cmd *card_disable_flow_8822b[] = {
	trans_act_to_cardemu_8822b,
	trans_cardemu_to_carddis_8822b,
	NULL
};

static const struct rtw_intf_phy_para usb2_param_8822b[] = {
	{0xFFFF, 0x00,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
};

static const struct rtw_intf_phy_para usb3_param_8822b[] = {
	{0x0001, 0xA841,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_D,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0xFFFF, 0x0000,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
};

static const struct rtw_intf_phy_para pcie_gen1_param_8822b[] = {
	{0x0001, 0xA841,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x0002, 0x60C6,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x0008, 0x3596,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x0009, 0x321C,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x000A, 0x9623,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x0020, 0x94FF,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x0021, 0xFFCF,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x0026, 0xC006,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x0029, 0xFF0E,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x002A, 0x1840,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0xFFFF, 0x0000,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
};

static const struct rtw_intf_phy_para pcie_gen2_param_8822b[] = {
	{0x0001, 0xA841,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x0002, 0x60C6,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x0008, 0x3597,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x0009, 0x321C,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x000A, 0x9623,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x0020, 0x94FF,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x0021, 0xFFCF,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x0026, 0xC006,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x0029, 0xFF0E,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x002A, 0x3040,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_C,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0xFFFF, 0x0000,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
};

static const struct rtw_intf_phy_para_table phy_para_table_8822b = {
	.usb2_para	= usb2_param_8822b,
	.usb3_para	= usb3_param_8822b,
	.gen1_para	= pcie_gen1_param_8822b,
	.gen2_para	= pcie_gen2_param_8822b,
	.n_usb2_para	= ARRAY_SIZE(usb2_param_8822b),
	.n_usb3_para	= ARRAY_SIZE(usb2_param_8822b),
	.n_gen1_para	= ARRAY_SIZE(pcie_gen1_param_8822b),
	.n_gen2_para	= ARRAY_SIZE(pcie_gen2_param_8822b),
};

static const struct rtw_rfe_def rtw8822b_rfe_defs[] = {
	[2] = RTW_DEF_RFE(8822b, 2, 2),
	[3] = RTW_DEF_RFE(8822b, 3, 0),
	[5] = RTW_DEF_RFE(8822b, 5, 5),
};

static const struct rtw_hw_reg rtw8822b_dig[] = {
	[0] = { .addr = 0xc50, .mask = 0x7f },
	[1] = { .addr = 0xe50, .mask = 0x7f },
};

static const struct rtw_ltecoex_addr rtw8822b_ltecoex_addr = {
	.ctrl = LTECOEX_ACCESS_CTRL,
	.wdata = LTECOEX_WRITE_DATA,
	.rdata = LTECOEX_READ_DATA,
};

static const struct rtw_page_table page_table_8822b[] = {
	{64, 64, 64, 64, 1},
	{64, 64, 64, 64, 1},
	{64, 64, 0, 0, 1},
	{64, 64, 64, 0, 1},
	{64, 64, 64, 64, 1},
};

static const struct rtw_rqpn rqpn_table_8822b[] = {
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

static struct rtw_prioq_addrs prioq_addrs_8822b = {
	.prio[RTW_DMA_MAPPING_EXTRA] = {
		.rsvd = REG_FIFOPAGE_INFO_4, .avail = REG_FIFOPAGE_INFO_4 + 2,
	},
	.prio[RTW_DMA_MAPPING_LOW] = {
		.rsvd = REG_FIFOPAGE_INFO_2, .avail = REG_FIFOPAGE_INFO_2 + 2,
	},
	.prio[RTW_DMA_MAPPING_NORMAL] = {
		.rsvd = REG_FIFOPAGE_INFO_3, .avail = REG_FIFOPAGE_INFO_3 + 2,
	},
	.prio[RTW_DMA_MAPPING_HIGH] = {
		.rsvd = REG_FIFOPAGE_INFO_1, .avail = REG_FIFOPAGE_INFO_1 + 2,
	},
	.wsize = true,
};

static struct rtw_chip_ops rtw8822b_ops = {
	.phy_set_param		= rtw8822b_phy_set_param,
	.read_efuse		= rtw8822b_read_efuse,
	.query_rx_desc		= rtw8822b_query_rx_desc,
	.set_channel		= rtw8822b_set_channel,
	.mac_init		= rtw8822b_mac_init,
	.read_rf		= rtw_phy_read_rf,
	.write_rf		= rtw_phy_write_rf_reg_sipi,
	.set_tx_power_index	= rtw8822b_set_tx_power_index,
	.set_antenna		= rtw8822b_set_antenna,
	.cfg_ldo25		= rtw8822b_cfg_ldo25,
	.false_alarm_statistics	= rtw8822b_false_alarm_statistics,
	.phy_calibration	= rtw8822b_phy_calibration,
	.pwr_track		= rtw8822b_pwr_track,
	.config_bfee		= rtw8822b_bf_config_bfee,
	.set_gid_table		= rtw_bf_set_gid_table,
	.cfg_csi_rate		= rtw_bf_cfg_csi_rate,
	.adaptivity_init	= rtw8822b_adaptivity_init,
	.adaptivity		= rtw8822b_adaptivity,

	.coex_set_init		= rtw8822b_coex_cfg_init,
	.coex_set_ant_switch	= rtw8822b_coex_cfg_ant_switch,
	.coex_set_gnt_fix	= rtw8822b_coex_cfg_gnt_fix,
	.coex_set_gnt_debug	= rtw8822b_coex_cfg_gnt_debug,
	.coex_set_rfe_type	= rtw8822b_coex_cfg_rfe_type,
	.coex_set_wl_tx_power	= rtw8822b_coex_cfg_wl_tx_power,
	.coex_set_wl_rx_gain	= rtw8822b_coex_cfg_wl_rx_gain,
};

/* Shared-Antenna Coex Table */
static const struct coex_table_para table_sant_8822b[] = {
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
	{0x66555555, 0xfafafafa},
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
	{0xffffffff, 0x6a5a5aaa}, /* case-25 */
	{0x55555555, 0x5a5a5a5a},
	{0x55555555, 0xaaaaaaaa},
	{0x55555555, 0x6a5a6a5a},
	{0x66556655, 0x66556655},
	{0x66556aaa, 0x6a5a6aaa}, /* case-30 */
	{0xffffffff, 0x5aaa5aaa},
	{0x56555555, 0x5a5a5aaa},
};

/* Non-Shared-Antenna Coex Table */
static const struct coex_table_para table_nsant_8822b[] = {
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
static const struct coex_tdma_para tdma_sant_8822b[] = {
	{ {0x00, 0x00, 0x00, 0x00, 0x00} }, /* case-0 */
	{ {0x61, 0x45, 0x03, 0x11, 0x11} },
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
static const struct coex_tdma_para tdma_nsant_8822b[] = {
	{ {0x00, 0x00, 0x00, 0x00, 0x00} }, /* case-100 */
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

/* rssi in percentage % (dbm = % - 100) */
static const u8 wl_rssi_step_8822b[] = {60, 50, 44, 30};
static const u8 bt_rssi_step_8822b[] = {30, 30, 30, 30};

/* wl_tx_dec_power, bt_tx_dec_power, wl_rx_gain, bt_rx_lna_constrain */
static const struct coex_rf_para rf_para_tx_8822b[] = {
	{0, 0, false, 7},  /* for normal */
	{0, 16, false, 7}, /* for WL-CPT */
	{4, 0, true, 1},
	{3, 6, true, 1},
	{2, 9, true, 1},
	{1, 13, true, 1}
};

static const struct coex_rf_para rf_para_rx_8822b[] = {
	{0, 0, false, 7},  /* for normal */
	{0, 16, false, 7}, /* for WL-CPT */
	{4, 0, true, 1},
	{3, 6, true, 1},
	{2, 9, true, 1},
	{1, 13, true, 1}
};

static const struct coex_5g_afh_map afh_5g_8822b[] = {
	{120, 2, 4},
	{124, 8, 8},
	{128, 17, 8},
	{132, 26, 10},
	{136, 34, 8},
	{140, 42, 10},
	{144, 51, 8},
	{149, 62, 8},
	{153, 71, 10},
	{157, 77, 4},
	{118, 2, 4},
	{126, 12, 16},
	{134, 29, 16},
	{142, 46, 16},
	{151, 66, 16},
	{159, 76, 4},
	{122, 10, 20},
	{138, 37, 34},
	{155, 68, 20}
};
static_assert(ARRAY_SIZE(rf_para_tx_8822b) == ARRAY_SIZE(rf_para_rx_8822b));

static const u8
rtw8822b_pwrtrk_5gb_n[RTW_PWR_TRK_5G_NUM][RTW_PWR_TRK_TBL_SZ] = {
	{ 0,  1,  2,  2,  3,  4,  5,  5,  6,  7,
	  8,  8,  9, 10, 11, 11, 12, 13, 14, 14,
	 15, 16, 17, 17, 18, 19, 20, 20, 21, 22 },
	{ 0,  1,  2,  2,  3,  4,  5,  5,  6,  7,
	  8,  8,  9, 10, 11, 11, 12, 13, 14, 14,
	 15, 16, 17, 17, 18, 19, 20, 20, 21, 22 },
	{ 0,  1,  2,  2,  3,  4,  5,  5,  6,  7,
	  8,  8,  9, 10, 11, 11, 12, 13, 14, 14,
	 15, 16, 17, 17, 18, 19, 20, 20, 21, 22 },
};

static const u8
rtw8822b_pwrtrk_5gb_p[RTW_PWR_TRK_5G_NUM][RTW_PWR_TRK_TBL_SZ] = {
	{ 0,  1,  2,  2,  3,  4,  5,  5,  6,  7,
	  8,  9,  9, 10, 11, 12, 13, 14, 14, 15,
	 16, 17, 18, 19, 19, 20, 21, 22, 22, 23 },
	{ 0,  1,  2,  2,  3,  4,  5,  5,  6,  7,
	  8,  9,  9, 10, 11, 12, 13, 14, 14, 15,
	 16, 17, 18, 19, 19, 20, 21, 22, 22, 23 },
	{ 0,  1,  2,  2,  3,  4,  5,  5,  6,  7,
	  8,  9,  9, 10, 11, 12, 13, 14, 14, 15,
	 16, 17, 18, 19, 19, 20, 21, 22, 22, 23 },
};

static const u8
rtw8822b_pwrtrk_5ga_n[RTW_PWR_TRK_5G_NUM][RTW_PWR_TRK_TBL_SZ] = {
	{ 0,  1,  2,  2,  3,  4,  5,  5,  6,  7,
	  8,  8,  9, 10, 11, 11, 12, 13, 14, 14,
	 15, 16, 17, 17, 18, 19, 20, 20, 21, 22 },
	{ 0,  1,  2,  2,  3,  4,  5,  5,  6,  7,
	  8,  8,  9, 10, 11, 11, 12, 13, 14, 14,
	 15, 16, 17, 17, 18, 19, 20, 20, 21, 22 },
	{ 0,  1,  2,  2,  3,  4,  5,  5,  6,  7,
	  8,  8,  9, 10, 11, 11, 12, 13, 14, 14,
	 15, 16, 17, 17, 18, 19, 20, 20, 21, 22 },
};

static const u8
rtw8822b_pwrtrk_5ga_p[RTW_PWR_TRK_5G_NUM][RTW_PWR_TRK_TBL_SZ] = {
	{ 0,  1,  2,  2,  3,  4,  5,  5,  6,  7,
	  8,  9,  9, 10, 11, 12, 13, 14, 14, 15,
	 16, 17, 18, 19, 19, 20, 21, 22, 22, 23},
	{ 0,  1,  2,  2,  3,  4,  5,  5,  6,  7,
	  8,  9,  9, 10, 11, 12, 13, 14, 14, 15,
	 16, 17, 18, 19, 19, 20, 21, 22, 22, 23},
	{ 0,  1,  2,  2,  3,  4,  5,  5,  6,  7,
	  8,  9,  9, 10, 11, 12, 13, 14, 14, 15,
	 16, 17, 18, 19, 19, 20, 21, 22, 22, 23},
};

static const u8 rtw8822b_pwrtrk_2gb_n[RTW_PWR_TRK_TBL_SZ] = {
	0,  1,  1,  1,  2,  2,  3,  3,  3,  4,
	4,  5,  5,  5,  6,  6,  7,  7,  7,  8,
	8,  9,  9,  9, 10, 10, 11, 11, 11, 12
};

static const u8 rtw8822b_pwrtrk_2gb_p[RTW_PWR_TRK_TBL_SZ] = {
	0,  0,  1,  1,  2,  2,  3,  3,  4,  4,
	5,  5,  6,  6,  6,  7,  7,  8,  8,  9,
	9, 10, 10, 11, 11, 12, 12, 12, 13, 13
};

static const u8 rtw8822b_pwrtrk_2ga_n[RTW_PWR_TRK_TBL_SZ] = {
	0,  1,  1,  1,  2,  2,  3,  3,  3,  4,
	4,  5,  5,  5,  6,  6,  7,  7,  7,  8,
	8,  9,  9,  9, 10, 10, 11, 11, 11, 12
};

static const u8 rtw8822b_pwrtrk_2ga_p[RTW_PWR_TRK_TBL_SZ] = {
	0,  1,  1,  2,  2,  3,  3,  4,  4,  5,
	5,  6,  6,  7,  7,  8,  8,  9,  9, 10,
	10, 11, 11, 12, 12, 13, 13, 14, 14, 15
};

static const u8 rtw8822b_pwrtrk_2g_cck_b_n[RTW_PWR_TRK_TBL_SZ] = {
	0,  1,  1,  1,  2,  2,  3,  3,  3,  4,
	4,  5,  5,  5,  6,  6,  7,  7,  7,  8,
	8,  9,  9,  9, 10, 10, 11, 11, 11, 12
};

static const u8 rtw8822b_pwrtrk_2g_cck_b_p[RTW_PWR_TRK_TBL_SZ] = {
	0,  0,  1,  1,  2,  2,  3,  3,  4,  4,
	5,  5,  6,  6,  6,  7,  7,  8,  8,  9,
	9, 10, 10, 11, 11, 12, 12, 12, 13, 13
};

static const u8 rtw8822b_pwrtrk_2g_cck_a_n[RTW_PWR_TRK_TBL_SZ] = {
	0,  1,  1,  1,  2,  2,  3,  3,  3,  4,
	4,  5,  5,  5,  6,  6,  7,  7,  7,  8,
	8,  9,  9,  9, 10, 10, 11, 11, 11, 12
};

static const u8 rtw8822b_pwrtrk_2g_cck_a_p[RTW_PWR_TRK_TBL_SZ] = {
	 0,  1,  1,  2,  2,  3,  3,  4,  4,  5,
	 5,  6,  6,  7,  7,  8,  8,  9,  9, 10,
	10, 11, 11, 12, 12, 13, 13, 14, 14, 15
};

static const struct rtw_pwr_track_tbl rtw8822b_rtw_pwr_track_tbl = {
	.pwrtrk_5gb_n[RTW_PWR_TRK_5G_1] = rtw8822b_pwrtrk_5gb_n[RTW_PWR_TRK_5G_1],
	.pwrtrk_5gb_n[RTW_PWR_TRK_5G_2] = rtw8822b_pwrtrk_5gb_n[RTW_PWR_TRK_5G_2],
	.pwrtrk_5gb_n[RTW_PWR_TRK_5G_3] = rtw8822b_pwrtrk_5gb_n[RTW_PWR_TRK_5G_3],
	.pwrtrk_5gb_p[RTW_PWR_TRK_5G_1] = rtw8822b_pwrtrk_5gb_p[RTW_PWR_TRK_5G_1],
	.pwrtrk_5gb_p[RTW_PWR_TRK_5G_2] = rtw8822b_pwrtrk_5gb_p[RTW_PWR_TRK_5G_2],
	.pwrtrk_5gb_p[RTW_PWR_TRK_5G_3] = rtw8822b_pwrtrk_5gb_p[RTW_PWR_TRK_5G_3],
	.pwrtrk_5ga_n[RTW_PWR_TRK_5G_1] = rtw8822b_pwrtrk_5ga_n[RTW_PWR_TRK_5G_1],
	.pwrtrk_5ga_n[RTW_PWR_TRK_5G_2] = rtw8822b_pwrtrk_5ga_n[RTW_PWR_TRK_5G_2],
	.pwrtrk_5ga_n[RTW_PWR_TRK_5G_3] = rtw8822b_pwrtrk_5ga_n[RTW_PWR_TRK_5G_3],
	.pwrtrk_5ga_p[RTW_PWR_TRK_5G_1] = rtw8822b_pwrtrk_5ga_p[RTW_PWR_TRK_5G_1],
	.pwrtrk_5ga_p[RTW_PWR_TRK_5G_2] = rtw8822b_pwrtrk_5ga_p[RTW_PWR_TRK_5G_2],
	.pwrtrk_5ga_p[RTW_PWR_TRK_5G_3] = rtw8822b_pwrtrk_5ga_p[RTW_PWR_TRK_5G_3],
	.pwrtrk_2gb_n = rtw8822b_pwrtrk_2gb_n,
	.pwrtrk_2gb_p = rtw8822b_pwrtrk_2gb_p,
	.pwrtrk_2ga_n = rtw8822b_pwrtrk_2ga_n,
	.pwrtrk_2ga_p = rtw8822b_pwrtrk_2ga_p,
	.pwrtrk_2g_cckb_n = rtw8822b_pwrtrk_2g_cck_b_n,
	.pwrtrk_2g_cckb_p = rtw8822b_pwrtrk_2g_cck_b_p,
	.pwrtrk_2g_ccka_n = rtw8822b_pwrtrk_2g_cck_a_n,
	.pwrtrk_2g_ccka_p = rtw8822b_pwrtrk_2g_cck_a_p,
};

static const struct rtw_reg_domain coex_info_hw_regs_8822b[] = {
	{0xcb0, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0xcb4, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0xcba, MASKBYTE0, RTW_REG_DOMAIN_MAC8},
	{0xcbd, MASKBYTE0, RTW_REG_DOMAIN_MAC8},
	{0xc58, MASKBYTE0, RTW_REG_DOMAIN_MAC8},
	{0xcbd, BIT(0), RTW_REG_DOMAIN_MAC8},
	{0, 0, RTW_REG_DOMAIN_NL},
	{0x430, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0x434, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0x42a, MASKLWORD, RTW_REG_DOMAIN_MAC16},
	{0x426, MASKBYTE0, RTW_REG_DOMAIN_MAC8},
	{0x45e, BIT(3), RTW_REG_DOMAIN_MAC8},
	{0x454, MASKLWORD, RTW_REG_DOMAIN_MAC16},
	{0, 0, RTW_REG_DOMAIN_NL},
	{0x4c, BIT(24) | BIT(23), RTW_REG_DOMAIN_MAC32},
	{0x64, BIT(0), RTW_REG_DOMAIN_MAC8},
	{0x4c6, BIT(4), RTW_REG_DOMAIN_MAC8},
	{0x40, BIT(5), RTW_REG_DOMAIN_MAC8},
	{0x1, RFREG_MASK, RTW_REG_DOMAIN_RF_B},
	{0, 0, RTW_REG_DOMAIN_NL},
	{0x550, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0x522, MASKBYTE0, RTW_REG_DOMAIN_MAC8},
	{0x953, BIT(1), RTW_REG_DOMAIN_MAC8},
	{0xc50,  MASKBYTE0, RTW_REG_DOMAIN_MAC8},
};

static struct rtw_hw_reg_offset rtw8822b_edcca_th[] = {
	[EDCCA_TH_L2H_IDX] = {{.addr = 0x8a4, .mask = MASKBYTE0}, .offset = 0},
	[EDCCA_TH_H2L_IDX] = {{.addr = 0x8a4, .mask = MASKBYTE1}, .offset = 0},
};

struct rtw_chip_info rtw8822b_hw_spec = {
	.ops = &rtw8822b_ops,
	.id = RTW_CHIP_TYPE_8822B,
	.fw_name = "rtw88/rtw8822b_fw.bin",
	.wlan_cpu = RTW_WCPU_11AC,
	.tx_pkt_desc_sz = 48,
	.tx_buf_desc_sz = 16,
	.rx_pkt_desc_sz = 24,
	.rx_buf_desc_sz = 8,
	.phy_efuse_size = 1024,
	.log_efuse_size = 768,
	.ptct_efuse_size = 96,
	.txff_size = 262144,
	.rxff_size = 24576,
	.fw_rxff_size = 12288,
	.txgi_factor = 1,
	.is_pwr_by_rate_dec = true,
	.max_power_index = 0x3f,
	.csi_buf_pg_num = 0,
	.band = RTW_BAND_2G | RTW_BAND_5G,
	.page_size = 128,
	.dig_min = 0x1c,
	.ht_supported = true,
	.vht_supported = true,
	.lps_deep_mode_supported = BIT(LPS_DEEP_MODE_LCLK),
	.sys_func_en = 0xDC,
	.pwr_on_seq = card_enable_flow_8822b,
	.pwr_off_seq = card_disable_flow_8822b,
	.page_table = page_table_8822b,
	.rqpn_table = rqpn_table_8822b,
	.prioq_addrs = &prioq_addrs_8822b,
	.intf_table = &phy_para_table_8822b,
	.dig = rtw8822b_dig,
	.dig_cck = NULL,
	.rf_base_addr = {0x2800, 0x2c00},
	.rf_sipi_addr = {0xc90, 0xe90},
	.ltecoex_addr = &rtw8822b_ltecoex_addr,
	.mac_tbl = &rtw8822b_mac_tbl,
	.agc_tbl = &rtw8822b_agc_tbl,
	.bb_tbl = &rtw8822b_bb_tbl,
	.rf_tbl = {&rtw8822b_rf_a_tbl, &rtw8822b_rf_b_tbl},
	.rfe_defs = rtw8822b_rfe_defs,
	.rfe_defs_size = ARRAY_SIZE(rtw8822b_rfe_defs),
	.pwr_track_tbl = &rtw8822b_rtw_pwr_track_tbl,
	.iqk_threshold = 8,
	.bfer_su_max_num = 2,
	.bfer_mu_max_num = 1,
	.rx_ldpc = true,
	.edcca_th = rtw8822b_edcca_th,
	.l2h_th_ini_cs = 10 + EDCCA_IGI_BASE,
	.l2h_th_ini_ad = -14 + EDCCA_IGI_BASE,

	.coex_para_ver = 0x20070206,
	.bt_desired_ver = 0x6,
	.scbd_support = true,
	.new_scbd10_def = false,
	.ble_hid_profile_support = false,
	.pstdma_type = COEX_PSTDMA_FORCE_LPSOFF,
	.bt_rssi_type = COEX_BTRSSI_RATIO,
	.ant_isolation = 15,
	.rssi_tolerance = 2,
	.wl_rssi_step = wl_rssi_step_8822b,
	.bt_rssi_step = bt_rssi_step_8822b,
	.table_sant_num = ARRAY_SIZE(table_sant_8822b),
	.table_sant = table_sant_8822b,
	.table_nsant_num = ARRAY_SIZE(table_nsant_8822b),
	.table_nsant = table_nsant_8822b,
	.tdma_sant_num = ARRAY_SIZE(tdma_sant_8822b),
	.tdma_sant = tdma_sant_8822b,
	.tdma_nsant_num = ARRAY_SIZE(tdma_nsant_8822b),
	.tdma_nsant = tdma_nsant_8822b,
	.wl_rf_para_num = ARRAY_SIZE(rf_para_tx_8822b),
	.wl_rf_para_tx = rf_para_tx_8822b,
	.wl_rf_para_rx = rf_para_rx_8822b,
	.bt_afh_span_bw20 = 0x24,
	.bt_afh_span_bw40 = 0x36,
	.afh_5g_num = ARRAY_SIZE(afh_5g_8822b),
	.afh_5g = afh_5g_8822b,

	.coex_info_hw_regs_num = ARRAY_SIZE(coex_info_hw_regs_8822b),
	.coex_info_hw_regs = coex_info_hw_regs_8822b,

	.fw_fifo_addr = {0x780, 0x700, 0x780, 0x660, 0x650, 0x680},
};
EXPORT_SYMBOL(rtw8822b_hw_spec);

MODULE_FIRMWARE("rtw88/rtw8822b_fw.bin");

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ac wireless 8822b driver");
MODULE_LICENSE("Dual BSD/GPL");
