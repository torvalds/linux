// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2025  Realtek Corporation
 */

#include <linux/usb.h>
#include "main.h"
#include "coex.h"
#include "tx.h"
#include "phy.h"
#include "rtw8814a.h"
#include "rtw8814a_table.h"
#include "rtw88xxa.h"
#include "reg.h"
#include "debug.h"
#include "efuse.h"
#include "regd.h"
#include "usb.h"

static void rtw8814a_efuse_grant(struct rtw_dev *rtwdev, bool on)
{
	if (on) {
		rtw_write8(rtwdev, REG_EFUSE_ACCESS, EFUSE_ACCESS_ON);

		rtw_write16_set(rtwdev, REG_SYS_FUNC_EN, BIT_FEN_ELDR);
		rtw_write16_set(rtwdev, REG_SYS_CLKR,
				BIT_LOADER_CLK_EN | BIT_ANA8M);
	} else {
		rtw_write8(rtwdev, REG_EFUSE_ACCESS, EFUSE_ACCESS_OFF);
	}
}

static void rtw8814a_read_rfe_type(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;

	if (!(efuse->rfe_option & BIT(7)))
		return;

	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_PCIE)
		efuse->rfe_option = 0;
	else if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_USB)
		efuse->rfe_option = 1;
}

static void rtw8814a_read_amplifier_type(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;

	switch (efuse->rfe_option) {
	case 1:
		/* Internal 2G */
		efuse->pa_type_2g = 0;
		efuse->lna_type_2g = 0;
		/* External 5G */
		efuse->pa_type_5g = BIT(0);
		efuse->lna_type_5g = BIT(3);
		break;
	case 2 ... 5:
		/* External everything */
		efuse->pa_type_2g = BIT(4);
		efuse->lna_type_2g = BIT(3);
		efuse->pa_type_5g = BIT(0);
		efuse->lna_type_5g = BIT(3);
		break;
	case 6:
		efuse->lna_type_5g = BIT(3);
		break;
	default:
		break;
	}
}

static void rtw8814a_read_rf_type(struct rtw_dev *rtwdev,
				  struct rtw8814a_efuse *map)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);
	struct rtw_hal *hal = &rtwdev->hal;

	switch (map->trx_antenna_option) {
	case 0xff: /* 4T4R */
	case 0xee: /* 3T3R */
		if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_USB &&
		    rtwusb->udev->speed != USB_SPEED_SUPER)
			hal->rf_type = RF_2T2R;
		else
			hal->rf_type = RF_3T3R;

		break;
	case 0x66: /* 2T2R */
	case 0x6f: /* 2T4R */
	default:
		hal->rf_type = RF_2T2R;
		break;
	}

	hal->rf_path_num = 4;
	hal->rf_phy_num = 4;

	if (hal->rf_type == RF_3T3R) {
		hal->antenna_rx = BB_PATH_ABC;
		hal->antenna_tx = BB_PATH_ABC;
	} else {
		hal->antenna_rx = BB_PATH_AB;
		hal->antenna_tx = BB_PATH_AB;
	}
}

static void rtw8814a_init_hwcap(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_hal *hal = &rtwdev->hal;

	efuse->hw_cap.bw = BIT(RTW_CHANNEL_WIDTH_20) |
			   BIT(RTW_CHANNEL_WIDTH_40) |
			   BIT(RTW_CHANNEL_WIDTH_80);
	efuse->hw_cap.ptcl = EFUSE_HW_CAP_PTCL_VHT;

	if (hal->rf_type == RF_3T3R)
		efuse->hw_cap.nss = 3;
	else
		efuse->hw_cap.nss = 2;

	rtw_dbg(rtwdev, RTW_DBG_EFUSE,
		"hw cap: hci=0x%02x, bw=0x%02x, ptcl=0x%02x, ant_num=%d, nss=%d\n",
		efuse->hw_cap.hci, efuse->hw_cap.bw, efuse->hw_cap.ptcl,
		efuse->hw_cap.ant_num, efuse->hw_cap.nss);
}

static int rtw8814a_read_efuse(struct rtw_dev *rtwdev, u8 *log_map)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw8814a_efuse *map;
	int i;

	if (rtw_dbg_is_enabled(rtwdev, RTW_DBG_EFUSE))
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 1,
			       log_map, rtwdev->chip->log_efuse_size, true);

	map = (struct rtw8814a_efuse *)log_map;

	efuse->usb_mode_switch = u8_get_bits(map->usb_mode, BIT(4));
	efuse->rfe_option = map->rfe_option;
	efuse->rf_board_option = map->rf_board_option;
	efuse->crystal_cap = map->xtal_k;
	efuse->channel_plan = map->channel_plan;
	efuse->country_code[0] = map->country_code[0];
	efuse->country_code[1] = map->country_code[1];
	efuse->bt_setting = map->rf_bt_setting;
	efuse->regd = map->rf_board_option & 0x7;
	efuse->thermal_meter[RF_PATH_A] = map->thermal_meter;
	efuse->thermal_meter_k = map->thermal_meter;
	efuse->tx_bb_swing_setting_2g = map->tx_bb_swing_setting_2g;
	efuse->tx_bb_swing_setting_5g = map->tx_bb_swing_setting_5g;

	rtw8814a_read_rfe_type(rtwdev);
	rtw8814a_read_amplifier_type(rtwdev);

	/* Override rtw_chip_parameter_setup() */
	rtw8814a_read_rf_type(rtwdev, map);

	rtw8814a_init_hwcap(rtwdev);

	for (i = 0; i < 4; i++)
		efuse->txpwr_idx_table[i] = map->txpwr_idx_table[i];

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_USB:
		ether_addr_copy(efuse->addr, map->u.mac_addr);
		break;
	case RTW_HCI_TYPE_PCIE:
		ether_addr_copy(efuse->addr, map->e.mac_addr);
		break;
	case RTW_HCI_TYPE_SDIO:
	default:
		/* unsupported now */
		return -EOPNOTSUPP;
	}

	return 0;
}

static void rtw8814a_init_rfe_reg(struct rtw_dev *rtwdev)
{
	u8 rfe_option = rtwdev->efuse.rfe_option;

	if (rfe_option == 2 || rfe_option == 1) {
		rtw_write32_mask(rtwdev, 0x1994, 0xf, 0xf);
		rtw_write8_set(rtwdev, REG_GPIO_MUXCFG + 2, 0xf0);
	} else if (rfe_option == 0) {
		rtw_write32_mask(rtwdev, 0x1994, 0xf, 0xf);
		rtw_write8_set(rtwdev, REG_GPIO_MUXCFG + 2, 0xc0);
	}
}

#define RTW_TXSCALE_SIZE 37
static const u32 rtw8814a_txscale_tbl[RTW_TXSCALE_SIZE] = {
	0x081, 0x088, 0x090, 0x099, 0x0a2, 0x0ac, 0x0b6, 0x0c0, 0x0cc, 0x0d8,
	0x0e5, 0x0f2, 0x101, 0x110, 0x120, 0x131, 0x143, 0x156, 0x16a, 0x180,
	0x197, 0x1af, 0x1c8, 0x1e3, 0x200, 0x21e, 0x23e, 0x261, 0x285, 0x2ab,
	0x2d3, 0x2fe, 0x32b, 0x35c, 0x38e, 0x3c4, 0x3fe
};

static u32 rtw8814a_get_bb_swing(struct rtw_dev *rtwdev, u8 band, u8 rf_path)
{
	static const u32 swing2setting[4] = {0x200, 0x16a, 0x101, 0x0b6};
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u8 tx_bb_swing;

	if (band == RTW_BAND_2G)
		tx_bb_swing = efuse->tx_bb_swing_setting_2g;
	else
		tx_bb_swing = efuse->tx_bb_swing_setting_5g;

	tx_bb_swing >>= 2 * rf_path;
	tx_bb_swing &= 0x3;

	return swing2setting[tx_bb_swing];
}

static u8 rtw8814a_get_swing_index(struct rtw_dev *rtwdev)
{
	u32 swing, table_value;
	u8 i;

	swing = rtw8814a_get_bb_swing(rtwdev, rtwdev->hal.current_band_type,
				      RF_PATH_A);

	for (i = 0; i < ARRAY_SIZE(rtw8814a_txscale_tbl); i++) {
		table_value = rtw8814a_txscale_tbl[i];
		if (swing == table_value)
			return i;
	}

	return 24;
}

static void rtw8814a_pwrtrack_init(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 path;

	dm_info->default_ofdm_index = rtw8814a_get_swing_index(rtwdev);

	for (path = RF_PATH_A; path < rtwdev->hal.rf_path_num; path++) {
		ewma_thermal_init(&dm_info->avg_thermal[path]);
		dm_info->delta_power_index[path] = 0;
		dm_info->delta_power_index_last[path] = 0;
	}
	dm_info->pwr_trk_triggered = false;
	dm_info->pwr_trk_init_trigger = true;
	dm_info->thermal_meter_k = rtwdev->efuse.thermal_meter_k;
}

static void rtw8814a_config_trx_path(struct rtw_dev *rtwdev)
{
	/* RX CCK disable 2R CCA */
	rtw_write32_clr(rtwdev, REG_CCK0_FAREPORT,
			BIT_CCK0_2RX | BIT_CCK0_MRC);
	/* pathB tx on, path A/C/D tx off */
	rtw_write32_mask(rtwdev, REG_CCK_RX, 0xf0000000, 0x4);
	/* pathB rx */
	rtw_write32_mask(rtwdev, REG_CCK_RX, 0x0f000000, 0x5);
}

static void rtw8814a_config_cck_rx_antenna_init(struct rtw_dev *rtwdev)
{
	/* CCK 2R CCA parameters */

	/* Disable Ant diversity */
	rtw_write32_mask(rtwdev, REG_RXSB, BIT_RXSB_ANA_DIV, 0x0);
	/* Concurrent CCA at LSB & USB */
	rtw_write32_mask(rtwdev, REG_CCA, BIT_CCA_CO, 0);
	/* RX path diversity enable */
	rtw_write32_mask(rtwdev, REG_ANTSEL, BIT_ANT_BYCO, 0);
	/* r_en_mrc_antsel */
	rtw_write32_mask(rtwdev, REG_PRECTRL, BIT_DIS_CO_PATHSEL, 0);
	/* MBC weighting */
	rtw_write32_mask(rtwdev, REG_CCA_MF, BIT_MBC_WIN, 1);
	/* 2R CCA only */
	rtw_write32_mask(rtwdev, REG_CCKTX, BIT_CMB_CCA_2R, 1);
}

static void rtw8814a_phy_set_param(struct rtw_dev *rtwdev)
{
	u32 crystal_cap, val32;
	u8 val8, rf_path;

	/* power on BB/RF domain */
	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_USB)
		rtw_write8_set(rtwdev, REG_SYS_FUNC_EN, BIT_FEN_USBA);
	else if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_PCIE)
		rtw_write8_set(rtwdev, REG_SYS_FUNC_EN, BIT_FEN_PCIEA);

	rtw_write8_set(rtwdev, REG_SYS_CFG3_8814A + 2,
		       BIT_FEN_BB_GLB_RST | BIT_FEN_BB_RSTB);

	/* Power on RF paths A..D */
	val8 = BIT_RF_EN | BIT_RF_RSTB | BIT_RF_SDM_RSTB;
	rtw_write8(rtwdev, REG_RF_CTRL, val8);
	rtw_write8(rtwdev, REG_RF_CTRL1, val8);
	rtw_write8(rtwdev, REG_RF_CTRL2, val8);
	rtw_write8(rtwdev, REG_RF_CTRL3, val8);

	rtw_load_table(rtwdev, rtwdev->chip->bb_tbl);
	rtw_load_table(rtwdev, rtwdev->chip->agc_tbl);

	crystal_cap = rtwdev->efuse.crystal_cap & 0x3F;
	crystal_cap |= crystal_cap << 6;
	rtw_write32_mask(rtwdev, REG_AFE_CTRL3, 0x07ff8000, crystal_cap);

	rtw8814a_config_trx_path(rtwdev);

	for (rf_path = 0; rf_path < rtwdev->hal.rf_path_num; rf_path++)
		rtw_load_table(rtwdev, rtwdev->chip->rf_tbl[rf_path]);

	val32 = rtw_read_rf(rtwdev, RF_PATH_A, RF_RCK1_V1, RFREG_MASK);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_RCK1_V1, RFREG_MASK, val32);
	rtw_write_rf(rtwdev, RF_PATH_C, RF_RCK1_V1, RFREG_MASK, val32);
	rtw_write_rf(rtwdev, RF_PATH_D, RF_RCK1_V1, RFREG_MASK, val32);

	rtw_write32_set(rtwdev, REG_RXPSEL, BIT_RX_PSEL_RST);

	rtw_write8(rtwdev, REG_HWSEQ_CTRL, 0xFF);

	rtw_write32(rtwdev, REG_BAR_MODE_CTRL, 0x0201ffff);

	rtw_write8(rtwdev, REG_MISC_CTRL, BIT_DIS_SECOND_CCA);

	rtw_write8(rtwdev, REG_NAV_CTRL + 2, 0);

	rtw_write8_clr(rtwdev, REG_GPIO_MUXCFG, BIT(5));

	rtw8814a_config_cck_rx_antenna_init(rtwdev);

	rtw_phy_init(rtwdev);
	rtw8814a_pwrtrack_init(rtwdev);

	rtw8814a_init_rfe_reg(rtwdev);

	rtw_write8_clr(rtwdev, REG_QUEUE_CTRL, BIT(3));

	rtw_write8(rtwdev, REG_NAV_CTRL + 2, 235);

	/* enable Tx report. */
	rtw_write8(rtwdev,  REG_FWHW_TXQ_CTRL + 1, 0x1F);

	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_USB) {
		/* Reset USB mode switch setting */
		rtw_write8(rtwdev, REG_SYS_SDIO_CTRL, 0x0);
		rtw_write8(rtwdev, REG_ACLK_MON, 0x0);
	}
}

static void rtw8814ae_enable_rf_1_2v(struct rtw_dev *rtwdev)
{
	/* This is for fullsize card, because GPIO7 there is floating.
	 * We should pull GPIO7 high to enable RF 1.2V Switch Power Supply
	 */

	/* 1. set 0x40[1:0] to 0, BIT_GPIOSEL=0, select pin as GPIO */
	rtw_write8_clr(rtwdev, REG_GPIO_MUXCFG, BIT(1) | BIT(0));

	/* 2. set 0x44[31] to 0
	 * mode=0: data port;
	 * mode=1 and BIT_GPIO_IO_SEL=0: interrupt mode;
	 */
	rtw_write8_clr(rtwdev, REG_GPIO_PIN_CTRL + 3, BIT(7));

	/* 3. data mode
	 * 3.1 set 0x44[23] to 1
	 * sel=0: input;
	 * sel=1: output;
	 */
	rtw_write8_set(rtwdev, REG_GPIO_PIN_CTRL + 2, BIT(7));

	/* 3.2 set 0x44[15] to 1
	 * output high value;
	 */
	rtw_write8_set(rtwdev, REG_GPIO_PIN_CTRL + 1, BIT(7));
}

static int rtw8814a_mac_init(struct rtw_dev *rtwdev)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);

	rtw_write16(rtwdev, REG_CR,
		    MAC_TRX_ENABLE | BIT_MAC_SEC_EN | BIT_32K_CAL_TMR_EN);

	rtw_load_table(rtwdev, rtwdev->chip->mac_tbl);

	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_USB)
		rtw_write8(rtwdev, REG_AUTO_LLT_V1 + 3,
			   rtwdev->chip->usb_tx_agg_desc_num << 1);

	rtw_write32(rtwdev, REG_HIMR0, 0);
	rtw_write32(rtwdev, REG_HIMR1, 0);

	rtw_write32_mask(rtwdev, REG_RRSR, 0xfffff, 0xfffff);

	rtw_write16(rtwdev, REG_RETRY_LIMIT, 0x3030);

	rtw_write16(rtwdev, REG_RXFLTMAP0, 0xffff);
	rtw_write16(rtwdev, REG_RXFLTMAP1, 0x0400);
	rtw_write16(rtwdev, REG_RXFLTMAP2, 0xffff);

	rtw_write8(rtwdev, REG_MAX_AGGR_NUM, 0x36);
	rtw_write8(rtwdev, REG_MAX_AGGR_NUM + 1, 0x36);

	/* Set Spec SIFS (used in NAV) */
	rtw_write16(rtwdev, REG_SPEC_SIFS, 0x100a);
	rtw_write16(rtwdev, REG_MAC_SPEC_SIFS, 0x100a);

	/* Set SIFS for CCK */
	rtw_write16(rtwdev, REG_SIFS, 0x100a);

	/* Set SIFS for OFDM */
	rtw_write16(rtwdev, REG_SIFS + 2, 0x100a);

	/* TXOP */
	rtw_write32(rtwdev, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtw_write32(rtwdev, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtw_write32(rtwdev, REG_EDCA_VI_PARAM, 0x005EA324);
	rtw_write32(rtwdev, REG_EDCA_VO_PARAM, 0x002FA226);

	rtw_write8_set(rtwdev, REG_FWHW_TXQ_CTRL, BIT(7));

	rtw_write8(rtwdev, REG_ACKTO, 0x80);

	rtw_write16(rtwdev, REG_BCN_CTRL,
		    BIT_DIS_TSF_UDT | (BIT_DIS_TSF_UDT << 8));
	rtw_write32_mask(rtwdev, REG_TBTT_PROHIBIT, 0xfffff, WLAN_TBTT_TIME);
	rtw_write8(rtwdev, REG_DRVERLYINT, 0x05);
	rtw_write8(rtwdev, REG_BCNDMATIM, WLAN_BCN_DMA_TIME);
	rtw_write16(rtwdev, REG_BCNTCFG, 0x4413);
	rtw_write8(rtwdev, REG_BCN_MAX_ERR, 0xFF);

	rtw_write32(rtwdev, REG_FAST_EDCA_VOVI_SETTING, 0x08070807);
	rtw_write32(rtwdev, REG_FAST_EDCA_BEBK_SETTING, 0x08070807);

	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_USB &&
	    rtwusb->udev->speed == USB_SPEED_SUPER) {
		/* Disable U1/U2 Mode to avoid 2.5G spur in USB3.0. */
		rtw_write8_clr(rtwdev, REG_USB_MOD, BIT(4) | BIT(3));
		/* To avoid usb 3.0 H2C fail. */
		rtw_write16(rtwdev, 0xf002, 0);

		rtw_write8_clr(rtwdev, REG_SW_AMPDU_BURST_MODE_CTRL,
			       BIT_PRE_TX_CMD);
	} else if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_PCIE) {
		rtw8814ae_enable_rf_1_2v(rtwdev);

		/* Force the antenna b to wifi. */
		rtw_write8_set(rtwdev, REG_PAD_CTRL1, BIT(2));
		rtw_write8_set(rtwdev, REG_PAD_CTRL1 + 1, BIT(0));
		rtw_write8_set(rtwdev, REG_LED_CFG + 3,
			       (BIT(27) | BIT_DPDT_WL_SEL) >> 24);
	}

	return 0;
}

static void rtw8814a_set_rfe_reg_24g(struct rtw_dev *rtwdev)
{
	switch (rtwdev->efuse.rfe_option) {
	case 2:
		rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x72707270);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x72707270);
		rtw_write32(rtwdev, REG_RFE_PINMUX_C, 0x72707270);
		rtw_write32(rtwdev, REG_RFE_PINMUX_D, 0x77707770);

		rtw_write32_mask(rtwdev, REG_RFE_INVSEL_D,
				 BIT_RFE_SELSW0_D, 0x72);

		break;
	case 1:
		rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x77777777);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x77777777);
		rtw_write32(rtwdev, REG_RFE_PINMUX_C, 0x77777777);
		rtw_write32(rtwdev, REG_RFE_PINMUX_D, 0x77777777);

		rtw_write32_mask(rtwdev, REG_RFE_INVSEL_D,
				 BIT_RFE_SELSW0_D, 0x77);

		break;
	case 0:
	default:
		rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x77777777);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x77777777);
		rtw_write32(rtwdev, REG_RFE_PINMUX_C, 0x77777777);
		/* Is it not necessary to set REG_RFE_PINMUX_D ? */

		rtw_write32_mask(rtwdev, REG_RFE_INVSEL_D,
				 BIT_RFE_SELSW0_D, 0x77);

		break;
	}
}

static void rtw8814a_set_rfe_reg_5g(struct rtw_dev *rtwdev)
{
	switch (rtwdev->efuse.rfe_option) {
	case 2:
		rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x37173717);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x37173717);
		rtw_write32(rtwdev, REG_RFE_PINMUX_C, 0x37173717);
		rtw_write32(rtwdev, REG_RFE_PINMUX_D, 0x77177717);

		rtw_write32_mask(rtwdev, REG_RFE_INVSEL_D,
				 BIT_RFE_SELSW0_D, 0x37);

		break;
	case 1:
		rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x33173317);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x33173317);
		rtw_write32(rtwdev, REG_RFE_PINMUX_C, 0x33173317);
		rtw_write32(rtwdev, REG_RFE_PINMUX_D, 0x77177717);

		rtw_write32_mask(rtwdev, REG_RFE_INVSEL_D,
				 BIT_RFE_SELSW0_D, 0x33);

		break;
	case 0:
	default:
		rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x54775477);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x54775477);
		rtw_write32(rtwdev, REG_RFE_PINMUX_C, 0x54775477);
		rtw_write32(rtwdev, REG_RFE_PINMUX_D, 0x54775477);

		rtw_write32_mask(rtwdev, REG_RFE_INVSEL_D,
				 BIT_RFE_SELSW0_D, 0x54);

		break;
	}
}

static void rtw8814a_set_channel_bb_swing(struct rtw_dev *rtwdev, u8 band)
{
	rtw_write32_mask(rtwdev, REG_TXSCALE_A, BB_SWING_MASK,
			 rtw8814a_get_bb_swing(rtwdev, band, RF_PATH_A));
	rtw_write32_mask(rtwdev, REG_TXSCALE_B, BB_SWING_MASK,
			 rtw8814a_get_bb_swing(rtwdev, band, RF_PATH_B));
	rtw_write32_mask(rtwdev, REG_TXSCALE_C, BB_SWING_MASK,
			 rtw8814a_get_bb_swing(rtwdev, band, RF_PATH_C));
	rtw_write32_mask(rtwdev, REG_TXSCALE_D, BB_SWING_MASK,
			 rtw8814a_get_bb_swing(rtwdev, band, RF_PATH_D));
	rtw8814a_pwrtrack_init(rtwdev);
}

static void rtw8814a_set_bw_reg_adc(struct rtw_dev *rtwdev, u8 bw)
{
	u32 adc = 0;

	if (bw == RTW_CHANNEL_WIDTH_20)
		adc = 0;
	else if (bw == RTW_CHANNEL_WIDTH_40)
		adc = 1;
	else if (bw == RTW_CHANNEL_WIDTH_80)
		adc = 2;

	rtw_write32_mask(rtwdev, REG_ADCCLK, BIT(1) | BIT(0), adc);
}

static void rtw8814a_set_bw_reg_agc(struct rtw_dev *rtwdev, u8 new_band, u8 bw)
{
	u32 agc = 7;

	if (bw == RTW_CHANNEL_WIDTH_20) {
		agc = 6;
	} else if (bw == RTW_CHANNEL_WIDTH_40) {
		if (new_band == RTW_BAND_5G)
			agc = 8;
		else
			agc = 7;
	} else if (bw == RTW_CHANNEL_WIDTH_80) {
		agc = 3;
	}

	rtw_write32_mask(rtwdev, REG_CCASEL, 0xf000, agc);
}

static void rtw8814a_switch_band(struct rtw_dev *rtwdev, u8 new_band, u8 bw)
{
	/* Clear 0x1000[16], When this bit is set to 0, CCK and OFDM
	 * are disabled, and clock are gated. Otherwise, CCK and OFDM
	 * are enabled.
	 */
	rtw_write8_clr(rtwdev, REG_SYS_CFG3_8814A + 2, BIT_FEN_BB_RSTB);

	if (new_band == RTW_BAND_2G) {
		rtw_write32_mask(rtwdev, REG_AGC_TABLE, 0x1f, 0);

		rtw8814a_set_rfe_reg_24g(rtwdev);

		rtw_write32_mask(rtwdev, REG_TXPSEL, 0xf0, 0x2);
		rtw_write32_mask(rtwdev, REG_CCK_RX, 0x0f000000, 0x5);

		rtw_write32_mask(rtwdev, REG_RXPSEL, BIT_RX_PSEL_RST, 0x3);

		rtw_write8(rtwdev, REG_CCK_CHECK, 0);

		rtw_write32_mask(rtwdev, 0xa80, BIT(18), 0);
	} else {
		rtw_write8(rtwdev, REG_CCK_CHECK, BIT_CHECK_CCK_EN);

		/* Enable CCK Tx function, even when CCK is off */
		rtw_write32_mask(rtwdev, 0xa80, BIT(18), 1);

		rtw8814a_set_rfe_reg_5g(rtwdev);

		rtw_write32_mask(rtwdev, REG_TXPSEL, 0xf0, 0x0);
		rtw_write32_mask(rtwdev, REG_CCK_RX, 0x0f000000, 0xf);

		rtw_write32_mask(rtwdev, REG_RXPSEL, BIT_RX_PSEL_RST, 0x2);
	}

	rtw8814a_set_channel_bb_swing(rtwdev, new_band);

	rtw8814a_set_bw_reg_adc(rtwdev, bw);
	rtw8814a_set_bw_reg_agc(rtwdev, new_band, bw);

	rtw_write8_set(rtwdev, REG_SYS_CFG3_8814A + 2, BIT_FEN_BB_RSTB);
}

static void rtw8814a_switch_channel(struct rtw_dev *rtwdev, u8 channel)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u32 fc_area, rf_mod_ag, cfgch;
	u8 path;

	switch (channel) {
	case 36 ... 48:
		fc_area = 0x494;
		break;
	case 50 ... 64:
		fc_area = 0x453;
		break;
	case 100 ... 116:
		fc_area = 0x452;
		break;
	default:
		if (channel >= 118)
			fc_area = 0x412;
		else
			fc_area = 0x96a;
		break;
	}

	rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, fc_area);

	for (path = 0; path < hal->rf_path_num; path++) {
		switch (channel) {
		case 36 ... 64:
			rf_mod_ag = 0x101;
			break;
		case 100 ... 140:
			rf_mod_ag = 0x301;
			break;
		default:
			if (channel > 140)
				rf_mod_ag = 0x501;
			else
				rf_mod_ag = 0x000;
			break;
		}

		cfgch = (rf_mod_ag << 8) | channel;

		rtw_write_rf(rtwdev, path, RF_CFGCH,
			     RF18_RFSI_MASK | RF18_BAND_MASK | RF18_CHANNEL_MASK, cfgch);
	}

	switch (channel) {
	case 36 ... 64:
		rtw_write32_mask(rtwdev, REG_AGC_TABLE, 0x1f, 1);
		break;
	case 100 ... 144:
		rtw_write32_mask(rtwdev, REG_AGC_TABLE, 0x1f, 2);
		break;
	default:
		if (channel >= 149)
			rtw_write32_mask(rtwdev, REG_AGC_TABLE, 0x1f, 3);

		break;
	}
}

static void rtw8814a_24g_cck_tx_dfir(struct rtw_dev *rtwdev, u8 channel)
{
	if (channel >= 1 && channel <= 11) {
		rtw_write32(rtwdev, REG_CCK0_TX_FILTER1, 0x1a1b0030);
		rtw_write32(rtwdev, REG_CCK0_TX_FILTER2, 0x090e1317);
		rtw_write32(rtwdev, REG_CCK0_DEBUG_PORT, 0x00000204);
	} else if (channel >= 12 && channel <= 13) {
		rtw_write32(rtwdev, REG_CCK0_TX_FILTER1, 0x1a1b0030);
		rtw_write32(rtwdev, REG_CCK0_TX_FILTER2, 0x090e1217);
		rtw_write32(rtwdev, REG_CCK0_DEBUG_PORT, 0x00000305);
	} else if (channel == 14) {
		rtw_write32(rtwdev, REG_CCK0_TX_FILTER1, 0x1a1b0030);
		rtw_write32(rtwdev, REG_CCK0_TX_FILTER2, 0x00000E17);
		rtw_write32(rtwdev, REG_CCK0_DEBUG_PORT, 0x00000000);
	}
}

static void rtw8814a_set_bw_reg_mac(struct rtw_dev *rtwdev, u8 bw)
{
	u16 val16 = rtw_read16(rtwdev, REG_WMAC_TRXPTCL_CTL);

	val16 &= ~BIT_RFMOD;
	if (bw == RTW_CHANNEL_WIDTH_80)
		val16 |= BIT_RFMOD_80M;
	else if (bw == RTW_CHANNEL_WIDTH_40)
		val16 |= BIT_RFMOD_40M;

	rtw_write16(rtwdev, REG_WMAC_TRXPTCL_CTL, val16);
}

static void rtw8814a_set_bw_rf(struct rtw_dev *rtwdev, u8 bw)
{
	u8 path;

	for (path = RF_PATH_A; path < rtwdev->hal.rf_path_num; path++) {
		switch (bw) {
		case RTW_CHANNEL_WIDTH_5:
		case RTW_CHANNEL_WIDTH_10:
		case RTW_CHANNEL_WIDTH_20:
		default:
			rtw_write_rf(rtwdev, path, RF_CFGCH, RF18_BW_MASK, 3);
			break;
		case RTW_CHANNEL_WIDTH_40:
			rtw_write_rf(rtwdev, path, RF_CFGCH, RF18_BW_MASK, 1);
			break;
		case RTW_CHANNEL_WIDTH_80:
			rtw_write_rf(rtwdev, path, RF_CFGCH, RF18_BW_MASK, 0);
			break;
		}
	}
}

static void rtw8814a_adc_clk(struct rtw_dev *rtwdev)
{
	static const u32 rxiqc_reg[2][4] = {
		{ REG_RX_IQC_AB_A, REG_RX_IQC_AB_B,
		  REG_RX_IQC_AB_C, REG_RX_IQC_AB_D },
		{ REG_RX_IQC_CD_A, REG_RX_IQC_CD_B,
		  REG_RX_IQC_CD_C, REG_RX_IQC_CD_D }
	};
	u32 bb_reg_8fc, bb_reg_808, rxiqc[4];
	u32 i = 0, mac_active = 1;
	u8 mac_reg_522;

	if (rtwdev->hal.cut_version != RTW_CHIP_VER_CUT_A)
		return;

	/* 1 Step1. MAC TX pause */
	mac_reg_522 = rtw_read8(rtwdev, REG_TXPAUSE);
	bb_reg_8fc = rtw_read32(rtwdev, REG_DBGSEL);
	bb_reg_808 = rtw_read32(rtwdev, REG_RXPSEL);
	rtw_write8(rtwdev, REG_TXPAUSE, 0x3f);

	/* 1 Step 2. Backup rxiqc & rxiqc = 0 */
	for (i = 0; i < 4; i++) {
		rxiqc[i] = rtw_read32(rtwdev, rxiqc_reg[0][i]);
		rtw_write32(rtwdev, rxiqc_reg[0][i], 0x0);
		rtw_write32(rtwdev, rxiqc_reg[1][i], 0x0);
	}
	rtw_write32_mask(rtwdev, REG_PRECTRL, BIT_IQ_WGT, 0x3);
	i = 0;

	/* 1 Step 3. Monitor MAC IDLE */
	rtw_write32(rtwdev, REG_DBGSEL, 0x0);
	while (mac_active) {
		mac_active = rtw_read32(rtwdev, REG_DBGRPT) & 0x803e0008;
		i++;
		if (i > 1000)
			break;
	}

	/* 1 Step 4. ADC clk flow */
	rtw_write8(rtwdev, REG_RXPSEL, 0x11);
	rtw_write32_mask(rtwdev, REG_DAC_RSTB, BIT(13), 0x1);
	rtw_write8_mask(rtwdev, REG_GNT_BT, BIT(2) | BIT(1), 0x3);
	rtw_write32_mask(rtwdev, REG_CCK_RPT_FORMAT, BIT(2), 0x1);

	/* 0xc1c/0xe1c/0x181c/0x1a1c[4] must=1 to ensure table can be
	 * written when bbrstb=0
	 * 0xc60/0xe60/0x1860/0x1a60[15] always = 1 after this line
	 * 0xc60/0xe60/0x1860/0x1a60[14] always = 0 bcz its error in A-cut
	 */

	/* power_off/clk_off @ anapar_state=idle mode */
	rtw_write32(rtwdev, REG_AFE_PWR1_A, 0x15800002);
	rtw_write32(rtwdev, REG_AFE_PWR1_A, 0x01808003);
	rtw_write32(rtwdev, REG_AFE_PWR1_B, 0x15800002);
	rtw_write32(rtwdev, REG_AFE_PWR1_B, 0x01808003);
	rtw_write32(rtwdev, REG_AFE_PWR1_C, 0x15800002);
	rtw_write32(rtwdev, REG_AFE_PWR1_C, 0x01808003);
	rtw_write32(rtwdev, REG_AFE_PWR1_D, 0x15800002);
	rtw_write32(rtwdev, REG_AFE_PWR1_D, 0x01808003);

	rtw_write8_mask(rtwdev, REG_GNT_BT, BIT(2), 0x0);
	rtw_write32_mask(rtwdev, REG_CCK_RPT_FORMAT, BIT(2), 0x0);
	/* [19] = 1 to turn off ADC */
	rtw_write32(rtwdev, REG_CK_MONHA, 0x0D080058);
	rtw_write32(rtwdev, REG_CK_MONHB, 0x0D080058);
	rtw_write32(rtwdev, REG_CK_MONHC, 0x0D080058);
	rtw_write32(rtwdev, REG_CK_MONHD, 0x0D080058);

	/* power_on/clk_off */
	/* [19] = 0 to turn on ADC */
	rtw_write32(rtwdev, REG_CK_MONHA, 0x0D000058);
	rtw_write32(rtwdev, REG_CK_MONHB, 0x0D000058);
	rtw_write32(rtwdev, REG_CK_MONHC, 0x0D000058);
	rtw_write32(rtwdev, REG_CK_MONHD, 0x0D000058);

	/* power_on/clk_on @ anapar_state=BT mode */
	rtw_write32(rtwdev, REG_AFE_PWR1_A, 0x05808032);
	rtw_write32(rtwdev, REG_AFE_PWR1_B, 0x05808032);
	rtw_write32(rtwdev, REG_AFE_PWR1_C, 0x05808032);
	rtw_write32(rtwdev, REG_AFE_PWR1_D, 0x05808032);
	rtw_write8_mask(rtwdev, REG_GNT_BT, BIT(2), 0x1);
	rtw_write32_mask(rtwdev, REG_CCK_RPT_FORMAT, BIT(2), 0x1);

	/* recover original setting @ anapar_state=BT mode */
	rtw_write32(rtwdev, REG_AFE_PWR1_A, 0x05808032);
	rtw_write32(rtwdev, REG_AFE_PWR1_B, 0x05808032);
	rtw_write32(rtwdev, REG_AFE_PWR1_C, 0x05808032);
	rtw_write32(rtwdev, REG_AFE_PWR1_D, 0x05808032);

	rtw_write32(rtwdev, REG_AFE_PWR1_A, 0x05800002);
	rtw_write32(rtwdev, REG_AFE_PWR1_A, 0x07808003);
	rtw_write32(rtwdev, REG_AFE_PWR1_B, 0x05800002);
	rtw_write32(rtwdev, REG_AFE_PWR1_B, 0x07808003);
	rtw_write32(rtwdev, REG_AFE_PWR1_C, 0x05800002);
	rtw_write32(rtwdev, REG_AFE_PWR1_C, 0x07808003);
	rtw_write32(rtwdev, REG_AFE_PWR1_D, 0x05800002);
	rtw_write32(rtwdev, REG_AFE_PWR1_D, 0x07808003);

	rtw_write8_mask(rtwdev, REG_GNT_BT, BIT(2) | BIT(1), 0x0);
	rtw_write32_mask(rtwdev, REG_CCK_RPT_FORMAT, BIT(2), 0x0);
	rtw_write32_mask(rtwdev, REG_DAC_RSTB, BIT(13), 0x0);

	/* 1 Step 5. Recover MAC TX & IQC */
	rtw_write8(rtwdev, REG_TXPAUSE, mac_reg_522);
	rtw_write32(rtwdev, REG_DBGSEL, bb_reg_8fc);
	rtw_write32(rtwdev, REG_RXPSEL, bb_reg_808);
	for (i = 0; i < 4; i++) {
		rtw_write32(rtwdev, rxiqc_reg[0][i], rxiqc[i]);
		rtw_write32(rtwdev, rxiqc_reg[1][i], 0x01000000);
	}
	rtw_write32_mask(rtwdev, REG_PRECTRL, BIT_IQ_WGT, 0x0);
}

static void rtw8814a_spur_calibration_ch140(struct rtw_dev *rtwdev, u8 channel)
{
	struct rtw_hal *hal = &rtwdev->hal;

	/* Add for 8814AE module ch140 MP Rx */
	if (channel == 140) {
		if (hal->ch_param[0] == 0)
			hal->ch_param[0] = rtw_read32(rtwdev, REG_CCASEL);
		if (hal->ch_param[1] == 0)
			hal->ch_param[1] = rtw_read32(rtwdev, REG_PDMFTH);

		rtw_write32(rtwdev, REG_CCASEL, 0x75438170);
		rtw_write32(rtwdev, REG_PDMFTH, 0x79a18a0a);
	} else {
		if (rtw_read32(rtwdev, REG_CCASEL) == 0x75438170 &&
		    hal->ch_param[0] != 0)
			rtw_write32(rtwdev, REG_CCASEL, hal->ch_param[0]);

		if (rtw_read32(rtwdev, REG_PDMFTH) == 0x79a18a0a &&
		    hal->ch_param[1] != 0)
			rtw_write32(rtwdev, REG_PDMFTH, hal->ch_param[1]);

		hal->ch_param[0] = rtw_read32(rtwdev, REG_CCASEL);
		hal->ch_param[1] = rtw_read32(rtwdev, REG_PDMFTH);
	}
}

static void rtw8814a_set_nbi_reg(struct rtw_dev *rtwdev, u32 tone_idx)
{
	/* tone_idx X 10 */
	static const u32 nbi_128[] = {
		25, 55, 85, 115, 135,
		155, 185, 205, 225, 245,
		265, 285, 305, 335, 355,
		375, 395, 415, 435, 455,
		485, 505, 525, 555, 585, 615, 635
	};
	u32 reg_idx = 0;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(nbi_128); i++) {
		if (tone_idx < nbi_128[i]) {
			reg_idx = i + 1;
			break;
		}
	}

	rtw_write32_mask(rtwdev, REG_NBI_SETTING, 0xfc000, reg_idx);
}

static void rtw8814a_nbi_setting(struct rtw_dev *rtwdev, u32 ch, u32 f_intf)
{
	u32 fc, int_distance, tone_idx;

	fc = 2412 + (ch - 1) * 5;
	int_distance = abs_diff(fc, f_intf);

	/* 10 * (int_distance / 0.3125) */
	tone_idx = int_distance << 5;

	rtw8814a_set_nbi_reg(rtwdev, tone_idx);

	rtw_write32_mask(rtwdev, REG_NBI_SETTING, BIT_NBI_ENABLE, 1);
}

static void rtw8814a_spur_nbi_setting(struct rtw_dev *rtwdev)
{
	u8 primary_channel = rtwdev->hal.primary_channel;
	u8 rfe_type = rtwdev->efuse.rfe_option;

	if (rfe_type != 0 && rfe_type != 1 && rfe_type != 6 && rfe_type != 7)
		return;

	if (primary_channel == 14)
		rtw8814a_nbi_setting(rtwdev, primary_channel, 2480);
	else if (primary_channel >= 4 && primary_channel <= 8)
		rtw8814a_nbi_setting(rtwdev, primary_channel, 2440);
	else
		rtw_write32_mask(rtwdev, REG_NBI_SETTING, BIT_NBI_ENABLE, 0);
}

/* A workaround to eliminate the 5280 MHz & 5600 MHz & 5760 MHz spur of 8814A */
static void rtw8814a_spur_calibration(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
	u8 rfe_type = rtwdev->efuse.rfe_option;
	bool reset_nbi_csi = true;

	if (rfe_type == 0) {
		switch (bw) {
		case RTW_CHANNEL_WIDTH_40:
			if (channel == 54 || channel == 118) {
				rtw_write32_mask(rtwdev, REG_NBI_SETTING,
						 0x000fe000, 0x3e >> 1);
				rtw_write32_mask(rtwdev, REG_CSI_MASK_SETTING1,
						 BIT(0), 1);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK0, 0);
				rtw_write32_mask(rtwdev, REG_CSI_FIX_MASK1,
						 BIT(0), 1);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK6, 0);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK7, 0);

				reset_nbi_csi = false;
			} else if (channel == 151) {
				rtw_write32_mask(rtwdev, REG_NBI_SETTING,
						 0x000fe000, 0x1e >> 1);
				rtw_write32_mask(rtwdev, REG_CSI_MASK_SETTING1,
						 BIT(0), 1);
				rtw_write32_mask(rtwdev, REG_CSI_FIX_MASK0,
						 BIT(16), 1);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK1, 0);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK6, 0);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK7, 0);

				reset_nbi_csi = false;
			}
			break;
		case RTW_CHANNEL_WIDTH_80:
			if (channel == 58 || channel == 122) {
				rtw_write32_mask(rtwdev, REG_NBI_SETTING,
						 0x000fe000, 0x3a >> 1);
				rtw_write32_mask(rtwdev, REG_CSI_MASK_SETTING1,
						 BIT(0), 1);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK0, 0);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK1, 0);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK6, 0);
				rtw_write32_mask(rtwdev, REG_CSI_FIX_MASK7,
						 BIT(0), 1);

				reset_nbi_csi = false;
			} else if (channel == 155) {
				rtw_write32_mask(rtwdev, REG_NBI_SETTING,
						 0x000fe000, 0x5a >> 1);
				rtw_write32_mask(rtwdev, REG_CSI_MASK_SETTING1,
						 BIT(0), 1);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK0, 0);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK1, 0);
				rtw_write32_mask(rtwdev, REG_CSI_FIX_MASK6,
						 BIT(16), 1);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK7, 0);

				reset_nbi_csi = false;
			}
			break;
		case RTW_CHANNEL_WIDTH_20:
			if (channel == 153) {
				rtw_write32_mask(rtwdev, REG_NBI_SETTING,
						 0x000fe000, 0x1e >> 1);
				rtw_write32_mask(rtwdev, REG_CSI_MASK_SETTING1,
						 BIT(0), 1);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK0, 0);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK1, 0);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK6, 0);
				rtw_write32_mask(rtwdev, REG_CSI_FIX_MASK7,
						 BIT(16), 1);

				reset_nbi_csi = false;
			}

			rtw8814a_spur_calibration_ch140(rtwdev, channel);
			break;
		default:
			break;
		}
	} else if (rfe_type == 1 || rfe_type == 2) {
		switch (bw) {
		case RTW_CHANNEL_WIDTH_20:
			if (channel == 153) {
				rtw_write32_mask(rtwdev, REG_NBI_SETTING,
						 0x000fe000, 0x1E >> 1);
				rtw_write32_mask(rtwdev, REG_CSI_MASK_SETTING1,
						 BIT(0), 1);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK0, 0);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK1, 0);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK6, 0);
				rtw_write32_mask(rtwdev, REG_CSI_FIX_MASK7,
						 BIT(16), 1);

				reset_nbi_csi = false;
			}
			break;
		case RTW_CHANNEL_WIDTH_40:
			if (channel == 151) {
				rtw_write32_mask(rtwdev, REG_NBI_SETTING,
						 0x000fe000, 0x1e >> 1);
				rtw_write32_mask(rtwdev, REG_CSI_MASK_SETTING1,
						 BIT(0), 1);
				rtw_write32_mask(rtwdev, REG_CSI_FIX_MASK0,
						 BIT(16), 1);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK1, 0);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK6, 0);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK7, 0);

				reset_nbi_csi = false;
			}
			break;
		case RTW_CHANNEL_WIDTH_80:
			if (channel == 155) {
				rtw_write32_mask(rtwdev, REG_NBI_SETTING,
						 0x000fe000, 0x5a >> 1);
				rtw_write32_mask(rtwdev, REG_CSI_MASK_SETTING1,
						 BIT(0), 1);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK0, 0);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK1, 0);
				rtw_write32_mask(rtwdev, REG_CSI_FIX_MASK6,
						 BIT(16), 1);
				rtw_write32(rtwdev, REG_CSI_FIX_MASK7, 0);

				reset_nbi_csi = false;
			}
			break;
		default:
			break;
		}
	}

	if (reset_nbi_csi) {
		rtw_write32_mask(rtwdev, REG_NBI_SETTING,
				 0x000fe000, 0xfc >> 1);
		rtw_write32_mask(rtwdev, REG_CSI_MASK_SETTING1, BIT(0), 0);
		rtw_write32(rtwdev, REG_CSI_FIX_MASK0, 0);
		rtw_write32(rtwdev, REG_CSI_FIX_MASK1, 0);
		rtw_write32(rtwdev, REG_CSI_FIX_MASK6, 0);
		rtw_write32(rtwdev, REG_CSI_FIX_MASK7, 0);
	}

	rtw8814a_spur_nbi_setting(rtwdev);
}

static void rtw8814a_set_bw_mode(struct rtw_dev *rtwdev, u8 new_band,
				 u8 channel, u8 bw, u8 primary_chan_idx)
{
	u8 txsc40 = 0, txsc20, txsc;

	rtw8814a_set_bw_reg_mac(rtwdev, bw);

	txsc20 = primary_chan_idx;
	if (bw == RTW_CHANNEL_WIDTH_80) {
		if (txsc20 == RTW_SC_20_UPPER || txsc20 == RTW_SC_20_UPMOST)
			txsc40 = RTW_SC_40_UPPER;
		else
			txsc40 = RTW_SC_40_LOWER;
	}

	txsc = BIT_TXSC_20M(txsc20) | BIT_TXSC_40M(txsc40);
	rtw_write8(rtwdev, REG_DATA_SC, txsc);

	rtw8814a_set_bw_reg_adc(rtwdev, bw);
	rtw8814a_set_bw_reg_agc(rtwdev, new_band, bw);

	if (bw == RTW_CHANNEL_WIDTH_80) {
		rtw_write32_mask(rtwdev, REG_ADCCLK, 0x3c, txsc);
	} else if (bw == RTW_CHANNEL_WIDTH_40) {
		rtw_write32_mask(rtwdev, REG_ADCCLK, 0x3c, txsc);

		if (txsc == RTW_SC_20_UPPER)
			rtw_write32_set(rtwdev, REG_RXSB, BIT(4));
		else
			rtw_write32_clr(rtwdev, REG_RXSB, BIT(4));
	}

	rtw8814a_set_bw_rf(rtwdev, bw);

	rtw8814a_adc_clk(rtwdev);

	rtw8814a_spur_calibration(rtwdev, channel, bw);
}

static void rtw8814a_set_channel(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				 u8 primary_chan_idx)
{
	u8 old_band, new_band;

	if (rtw_read8(rtwdev, REG_CCK_CHECK) & BIT_CHECK_CCK_EN)
		old_band = RTW_BAND_5G;
	else
		old_band = RTW_BAND_2G;

	if (channel > 14)
		new_band = RTW_BAND_5G;
	else
		new_band = RTW_BAND_2G;

	if (new_band != old_band)
		rtw8814a_switch_band(rtwdev, new_band, bw);

	rtw8814a_switch_channel(rtwdev, channel);

	rtw8814a_24g_cck_tx_dfir(rtwdev, channel);

	rtw8814a_set_bw_mode(rtwdev, new_band, channel, bw, primary_chan_idx);
}

static s8 rtw8814a_cck_rx_pwr(u8 lna_idx, u8 vga_idx)
{
	s8 rx_pwr_all = 0;

	switch (lna_idx) {
	case 7:
		rx_pwr_all = -38 - 2 * vga_idx;
		break;
	case 5:
		rx_pwr_all = -28 - 2 * vga_idx;
		break;
	case 3:
		rx_pwr_all = -8 - 2 * vga_idx;
		break;
	case 2:
		rx_pwr_all = -1 - 2 * vga_idx;
		break;
	default:
		break;
	}

	return rx_pwr_all;
}

static void rtw8814a_query_phy_status(struct rtw_dev *rtwdev, u8 *phy_status,
				      struct rtw_rx_pkt_stat *pkt_stat)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_jaguar_phy_status_rpt *rpt;
	u8 gain[RTW_RF_PATH_MAX], rssi, i;
	s8 rx_pwr_db, middle1, middle2;
	s8 snr[RTW_RF_PATH_MAX];
	s8 evm[RTW_RF_PATH_MAX];
	u8 rfmode, subchannel;
	u8 lna, vga;
	s8 cfo[2];

	rpt = (struct rtw_jaguar_phy_status_rpt *)phy_status;

	pkt_stat->bw = RTW_CHANNEL_WIDTH_20;

	if (pkt_stat->rate <= DESC_RATE11M) {
		lna = le32_get_bits(rpt->w1, RTW_JGRPHY_W1_AGC_RPT_LNA_IDX);
		vga = le32_get_bits(rpt->w1, RTW_JGRPHY_W1_AGC_RPT_VGA_IDX);

		rx_pwr_db = rtw8814a_cck_rx_pwr(lna, vga);

		pkt_stat->rx_power[RF_PATH_A] = rx_pwr_db;
		pkt_stat->rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power, 1);
		dm_info->rssi[RF_PATH_A] = pkt_stat->rssi;
		pkt_stat->signal_power = rx_pwr_db;
	} else { /* OFDM rate */
		gain[RF_PATH_A] = le32_get_bits(rpt->w0, RTW_JGRPHY_W0_GAIN_A);
		gain[RF_PATH_B] = le32_get_bits(rpt->w0, RTW_JGRPHY_W0_GAIN_B);
		gain[RF_PATH_C] = le32_get_bits(rpt->w5, RTW_JGRPHY_W5_GAIN_C);
		gain[RF_PATH_D] = le32_get_bits(rpt->w6, RTW_JGRPHY_W6_GAIN_D);

		snr[RF_PATH_A] = le32_get_bits(rpt->w3, RTW_JGRPHY_W3_RXSNR_A);
		snr[RF_PATH_B] = le32_get_bits(rpt->w4, RTW_JGRPHY_W4_RXSNR_B);
		snr[RF_PATH_C] = le32_get_bits(rpt->w5, RTW_JGRPHY_W5_RXSNR_C);
		snr[RF_PATH_D] = le32_get_bits(rpt->w5, RTW_JGRPHY_W5_RXSNR_D);

		evm[RF_PATH_A] = le32_get_bits(rpt->w3, RTW_JGRPHY_W3_RXEVM_1);
		evm[RF_PATH_B] = le32_get_bits(rpt->w3, RTW_JGRPHY_W3_RXEVM_2);
		evm[RF_PATH_C] = le32_get_bits(rpt->w4, RTW_JGRPHY_W4_RXEVM_3);
		evm[RF_PATH_D] = le32_get_bits(rpt->w5, RTW_JGRPHY_W5_RXEVM_4);

		if (pkt_stat->rate <= DESC_RATE54M)
			evm[RF_PATH_A] = le32_get_bits(rpt->w6,
						       RTW_JGRPHY_W6_SIGEVM);

		for (i = RF_PATH_A; i < RTW_RF_PATH_MAX; i++) {
			pkt_stat->rx_power[i] = gain[i] - 110;

			rssi = rtw_phy_rf_power_2_rssi(&pkt_stat->rx_power[i], 1);
			dm_info->rssi[i] = rssi;

			pkt_stat->rx_snr[i] = snr[i];
			dm_info->rx_snr[i] = snr[i] >> 1;

			pkt_stat->rx_evm[i] = evm[i];
			evm[i] = max_t(s8, -127, evm[i]);
			dm_info->rx_evm_dbm[i] = abs(evm[i]) >> 1;
		}

		rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power,
					       RTW_RF_PATH_MAX);
		pkt_stat->rssi = rssi;

		/* When power saving is enabled the hardware sometimes
		 * reports unbelievably high gain for paths A and C
		 * (e.g. one frame 64 68 68 72, the next frame 106 66 88 72,
		 * the next 66 66 68 72), so use the second lowest gain
		 * instead of the highest.
		 */
		middle1 = max(min(gain[RF_PATH_A], gain[RF_PATH_B]),
			      min(gain[RF_PATH_C], gain[RF_PATH_D]));
		middle2 = min(max(gain[RF_PATH_A], gain[RF_PATH_B]),
			      max(gain[RF_PATH_C], gain[RF_PATH_D]));
		rx_pwr_db = min(middle1, middle2);
		rx_pwr_db -= 110;
		pkt_stat->signal_power = rx_pwr_db;

		rfmode = le32_get_bits(rpt->w0, RTW_JGRPHY_W0_R_RFMOD);
		subchannel = le32_get_bits(rpt->w0, RTW_JGRPHY_W0_SUB_CHNL);

		if (rfmode == 1 && subchannel == 0) {
			pkt_stat->bw = RTW_CHANNEL_WIDTH_40;
		} else if (rfmode == 2) {
			if (subchannel == 0)
				pkt_stat->bw = RTW_CHANNEL_WIDTH_80;
			else if (subchannel == 9 || subchannel == 10)
				pkt_stat->bw = RTW_CHANNEL_WIDTH_40;
		}

		cfo[RF_PATH_A] = le32_get_bits(rpt->w2, RTW_JGRPHY_W2_CFO_TAIL_A);
		cfo[RF_PATH_B] = le32_get_bits(rpt->w2, RTW_JGRPHY_W2_CFO_TAIL_B);

		for (i = RF_PATH_A; i < 2; i++) {
			pkt_stat->cfo_tail[i] = cfo[i];
			dm_info->cfo_tail[i] = (cfo[i] * 5) >> 1;
		}
	}
}

static void
rtw8814a_set_tx_power_index_by_rate(struct rtw_dev *rtwdev, u8 path, u8 rs)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u32 txagc_table_wd;
	u8 rate, pwr_index;
	int j;

	for (j = 0; j < rtw_rate_size[rs]; j++) {
		rate = rtw_rate_section[rs][j];

		pwr_index = hal->tx_pwr_tbl[path][rate] + 2;
		if (pwr_index > rtwdev->chip->max_power_index)
			pwr_index = rtwdev->chip->max_power_index;

		txagc_table_wd = 0x00801000;
		txagc_table_wd |= (pwr_index << 24) | (path << 8) | rate;

		rtw_write32(rtwdev, REG_AGC_TBL, txagc_table_wd);

		/* first time to turn on the txagc table
		 * second to write the addr0
		 */
		if (rate == DESC_RATE1M)
			rtw_write32(rtwdev, REG_AGC_TBL, txagc_table_wd);
	}
}

static void rtw8814a_set_tx_power_index(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	int path;

	for (path = 0; path < hal->rf_path_num; path++) {
		if (hal->current_band_type == RTW_BAND_2G)
			rtw8814a_set_tx_power_index_by_rate(rtwdev, path,
							    RTW_RATE_SECTION_CCK);

		rtw8814a_set_tx_power_index_by_rate(rtwdev, path,
						    RTW_RATE_SECTION_OFDM);

		if (test_bit(RTW_FLAG_SCANNING, rtwdev->flags))
			continue;

		rtw8814a_set_tx_power_index_by_rate(rtwdev, path,
						    RTW_RATE_SECTION_HT_1S);
		rtw8814a_set_tx_power_index_by_rate(rtwdev, path,
						    RTW_RATE_SECTION_VHT_1S);

		rtw8814a_set_tx_power_index_by_rate(rtwdev, path,
						    RTW_RATE_SECTION_HT_2S);
		rtw8814a_set_tx_power_index_by_rate(rtwdev, path,
						    RTW_RATE_SECTION_VHT_2S);

		rtw8814a_set_tx_power_index_by_rate(rtwdev, path,
						    RTW_RATE_SECTION_HT_3S);
		rtw8814a_set_tx_power_index_by_rate(rtwdev, path,
						    RTW_RATE_SECTION_VHT_3S);
	}
}

static void rtw8814a_cfg_ldo25(struct rtw_dev *rtwdev, bool enable)
{
}

/* Without this RTL8814A sends too many frames and (some?) 11n AP
 * can't handle it, resulting in low TX speed. Other chips seem fine.
 */
static void rtw8814a_set_ampdu_factor(struct rtw_dev *rtwdev, u8 factor)
{
	factor = min_t(u8, factor, IEEE80211_VHT_MAX_AMPDU_256K);

	rtw_write32(rtwdev, REG_AMPDU_MAX_LENGTH, (8192 << factor) - 1);
}

static void rtw8814a_false_alarm_statistics(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u32 cck_fa_cnt, ofdm_fa_cnt;
	u32 crc32_cnt, cca32_cnt;
	u32 cck_enable;

	cck_enable = rtw_read32(rtwdev, REG_RXPSEL) & BIT(28);
	cck_fa_cnt = rtw_read16(rtwdev, REG_FA_CCK);
	ofdm_fa_cnt = rtw_read16(rtwdev, REG_FA_OFDM);

	dm_info->cck_fa_cnt = cck_fa_cnt;
	dm_info->ofdm_fa_cnt = ofdm_fa_cnt;
	dm_info->total_fa_cnt = ofdm_fa_cnt;
	if (cck_enable)
		dm_info->total_fa_cnt += cck_fa_cnt;

	crc32_cnt = rtw_read32(rtwdev, REG_CRC_CCK);
	dm_info->cck_ok_cnt = u32_get_bits(crc32_cnt, MASKLWORD);
	dm_info->cck_err_cnt = u32_get_bits(crc32_cnt, MASKHWORD);

	crc32_cnt = rtw_read32(rtwdev, REG_CRC_OFDM);
	dm_info->ofdm_ok_cnt = u32_get_bits(crc32_cnt, MASKLWORD);
	dm_info->ofdm_err_cnt = u32_get_bits(crc32_cnt, MASKHWORD);

	crc32_cnt = rtw_read32(rtwdev, REG_CRC_HT);
	dm_info->ht_ok_cnt = u32_get_bits(crc32_cnt, MASKLWORD);
	dm_info->ht_err_cnt = u32_get_bits(crc32_cnt, MASKHWORD);

	crc32_cnt = rtw_read32(rtwdev, REG_CRC_VHT);
	dm_info->vht_ok_cnt = u32_get_bits(crc32_cnt, MASKLWORD);
	dm_info->vht_err_cnt = u32_get_bits(crc32_cnt, MASKHWORD);

	cca32_cnt = rtw_read32(rtwdev, REG_CCA_OFDM);
	dm_info->ofdm_cca_cnt = u32_get_bits(cca32_cnt, MASKHWORD);
	dm_info->total_cca_cnt = dm_info->ofdm_cca_cnt;
	if (cck_enable) {
		cca32_cnt = rtw_read32(rtwdev, REG_CCA_CCK);
		dm_info->cck_cca_cnt = u32_get_bits(cca32_cnt, MASKLWORD);
		dm_info->total_cca_cnt += dm_info->cck_cca_cnt;
	}

	rtw_write32_set(rtwdev, REG_FAS, BIT(17));
	rtw_write32_clr(rtwdev, REG_FAS, BIT(17));
	rtw_write32_clr(rtwdev, REG_CCK0_FAREPORT, BIT(15));
	rtw_write32_set(rtwdev, REG_CCK0_FAREPORT, BIT(15));
	rtw_write32_set(rtwdev, REG_CNTRST, BIT(0));
	rtw_write32_clr(rtwdev, REG_CNTRST, BIT(0));
}

#define MAC_REG_NUM_8814 2
#define BB_REG_NUM_8814 14
#define RF_REG_NUM_8814 1

static void rtw8814a_iqk_backup_mac_bb(struct rtw_dev *rtwdev,
				       u32 *mac_backup, u32 *bb_backup,
				       const u32 *mac_regs,
				       const u32 *bb_regs)
{
	u32 i;

	/* save MACBB default value */
	for (i = 0; i < MAC_REG_NUM_8814; i++)
		mac_backup[i] = rtw_read32(rtwdev, mac_regs[i]);

	for (i = 0; i < BB_REG_NUM_8814; i++)
		bb_backup[i] = rtw_read32(rtwdev, bb_regs[i]);
}

static void rtw8814a_iqk_backup_rf(struct rtw_dev *rtwdev,
				   u32 rf_backup[][4], const u32 *rf_regs)
{
	u32 i;

	/* Save RF Parameters */
	for (i = 0; i < RF_REG_NUM_8814; i++) {
		rf_backup[i][RF_PATH_A] = rtw_read_rf(rtwdev, RF_PATH_A,
						      rf_regs[i], RFREG_MASK);
		rf_backup[i][RF_PATH_B] = rtw_read_rf(rtwdev, RF_PATH_B,
						      rf_regs[i], RFREG_MASK);
		rf_backup[i][RF_PATH_C] = rtw_read_rf(rtwdev, RF_PATH_C,
						      rf_regs[i], RFREG_MASK);
		rf_backup[i][RF_PATH_D] = rtw_read_rf(rtwdev, RF_PATH_D,
						      rf_regs[i], RFREG_MASK);
	}
}

static void rtw8814a_iqk_afe_setting(struct rtw_dev *rtwdev, bool do_iqk)
{
	if (do_iqk) {
		/* IQK AFE setting RX_WAIT_CCA mode */
		rtw_write32(rtwdev, REG_AFE_PWR1_A, 0x0e808003);
		rtw_write32(rtwdev, REG_AFE_PWR1_B, 0x0e808003);
		rtw_write32(rtwdev, REG_AFE_PWR1_C, 0x0e808003);
		rtw_write32(rtwdev, REG_AFE_PWR1_D, 0x0e808003);
	} else {
		rtw_write32(rtwdev, REG_AFE_PWR1_A, 0x07808003);
		rtw_write32(rtwdev, REG_AFE_PWR1_B, 0x07808003);
		rtw_write32(rtwdev, REG_AFE_PWR1_C, 0x07808003);
		rtw_write32(rtwdev, REG_AFE_PWR1_D, 0x07808003);
	}

	rtw_write32_mask(rtwdev, REG_DAC_RSTB, BIT(13), 0x1);

	rtw_write8_set(rtwdev, REG_GNT_BT, BIT(2) | BIT(1));
	rtw_write8_clr(rtwdev, REG_GNT_BT, BIT(2) | BIT(1));

	rtw_write32_set(rtwdev, REG_CCK_RPT_FORMAT, BIT(2));
	rtw_write32_clr(rtwdev, REG_CCK_RPT_FORMAT, BIT(2));
}

static void rtw8814a_iqk_restore_mac_bb(struct rtw_dev *rtwdev,
					u32 *mac_backup, u32 *bb_backup,
					const u32 *mac_regs,
					const u32 *bb_regs)
{
	u32 i;

	/* Reload MacBB Parameters */
	for (i = 0; i < MAC_REG_NUM_8814; i++)
		rtw_write32(rtwdev, mac_regs[i], mac_backup[i]);

	for (i = 0; i < BB_REG_NUM_8814; i++)
		rtw_write32(rtwdev, bb_regs[i], bb_backup[i]);
}

static void rtw8814a_iqk_restore_rf(struct rtw_dev *rtwdev,
				    const u32 rf_backup[][4],
				    const u32 *rf_regs)
{
	u32 i;

	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, RFREG_MASK, 0x0);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_LUTWE, RFREG_MASK, 0x0);
	rtw_write_rf(rtwdev, RF_PATH_C, RF_LUTWE, RFREG_MASK, 0x0);
	rtw_write_rf(rtwdev, RF_PATH_D, RF_LUTWE, RFREG_MASK, 0x0);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_RXBB2, RFREG_MASK, 0x88001);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_RXBB2, RFREG_MASK, 0x88001);
	rtw_write_rf(rtwdev, RF_PATH_C, RF_RXBB2, RFREG_MASK, 0x88001);
	rtw_write_rf(rtwdev, RF_PATH_D, RF_RXBB2, RFREG_MASK, 0x88001);

	for (i = 0; i < RF_REG_NUM_8814; i++) {
		rtw_write_rf(rtwdev, RF_PATH_A, rf_regs[i],
			     RFREG_MASK, rf_backup[i][RF_PATH_A]);
		rtw_write_rf(rtwdev, RF_PATH_B, rf_regs[i],
			     RFREG_MASK, rf_backup[i][RF_PATH_B]);
		rtw_write_rf(rtwdev, RF_PATH_C, rf_regs[i],
			     RFREG_MASK, rf_backup[i][RF_PATH_C]);
		rtw_write_rf(rtwdev, RF_PATH_D, rf_regs[i],
			     RFREG_MASK, rf_backup[i][RF_PATH_D]);
	}
}

static void rtw8814a_iqk_reset_nctl(struct rtw_dev *rtwdev)
{
	rtw_write32(rtwdev, 0x1b00, 0xf8000000);
	rtw_write32(rtwdev, 0x1b80, 0x00000006);

	rtw_write32(rtwdev, 0x1b00, 0xf8000000);
	rtw_write32(rtwdev, 0x1b80, 0x00000002);
}

static void rtw8814a_iqk_configure_mac(struct rtw_dev *rtwdev)
{
	rtw_write8(rtwdev, REG_TXPAUSE, 0x3f);
	rtw_write32_clr(rtwdev, REG_BCN_CTRL,
			(BIT_EN_BCN_FUNCTION << 8) | BIT_EN_BCN_FUNCTION);

	/* RX ante off */
	rtw_write8(rtwdev, REG_RXPSEL, 0x00);
	/* CCA off */
	rtw_write32_mask(rtwdev, REG_CCA2ND, 0xf, 0xe);
	/* CCK RX path off */
	rtw_write32_set(rtwdev, REG_PRECTRL, BIT_IQ_WGT);
	rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x77777777);
	rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x77777777);
	rtw_write32(rtwdev, REG_RFE_PINMUX_C, 0x77777777);
	rtw_write32(rtwdev, REG_RFE_PINMUX_D, 0x77777777);
	rtw_write32_mask(rtwdev, REG_RFE_INVSEL_D, BIT_RFE_SELSW0_D, 0x77);
	rtw_write32_mask(rtwdev, REG_PSD, BIT_PSD_INI, 0x0);

	rtw_write32_mask(rtwdev, REG_RFE_INV0, 0xf, 0x0);
}

static void rtw8814a_lok_one_shot(struct rtw_dev *rtwdev, u8 path)
{
	u32 lok_temp1, lok_temp2;
	bool lok_ready;
	u8 ii;

	/* ADC Clock source */
	rtw_write32_mask(rtwdev, REG_FAS, BIT(21) | BIT(20), path);
	/* LOK: CMD ID = 0
	 * {0xf8000011, 0xf8000021, 0xf8000041, 0xf8000081}
	 */
	rtw_write32(rtwdev, 0x1b00, 0xf8000001 | (BIT(path) << 4));

	usleep_range(1000, 1100);

	if (read_poll_timeout(!rtw_read32_mask, lok_ready, lok_ready,
			      1000, 10000, false,
			      rtwdev, 0x1b00, BIT(0))) {
		rtw_dbg(rtwdev, RTW_DBG_RFK, "==>S%d LOK timed out\n", path);

		rtw8814a_iqk_reset_nctl(rtwdev);

		rtw_write_rf(rtwdev, path, RF_DTXLOK, RFREG_MASK, 0x08400);

		return;
	}

	rtw_write32(rtwdev, 0x1b00, 0xf8000000 | (path << 1));
	rtw_write32(rtwdev, 0x1bd4, 0x003f0001);

	lok_temp2 = rtw_read32_mask(rtwdev, 0x1bfc, 0x003e0000);
	lok_temp2 = (lok_temp2 + 0x10) & 0x1f;

	lok_temp1 = rtw_read32_mask(rtwdev, 0x1bfc, 0x0000003e);
	lok_temp1 = (lok_temp1 + 0x10) & 0x1f;

	for (ii = 1; ii < 5; ii++) {
		lok_temp1 += (lok_temp1 & BIT(4 - ii)) << (ii * 2);
		lok_temp2 += (lok_temp2 & BIT(4 - ii)) << (ii * 2);
	}

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"path %d lok_temp1 = %#x, lok_temp2 = %#x\n",
		path, lok_temp1 >> 4, lok_temp2 >> 4);

	rtw_write_rf(rtwdev, path, RF_DTXLOK, 0x07c00, lok_temp1 >> 4);
	rtw_write_rf(rtwdev, path, RF_DTXLOK, 0xf8000, lok_temp2 >> 4);
}

static void rtw8814a_iqk_tx_one_shot(struct rtw_dev *rtwdev, u8 path,
				     u32 *tx_matrix, bool *tx_ok)
{
	u8 bw = rtwdev->hal.current_band_width;
	u8 cal_retry;
	u32 iqk_cmd;

	for (cal_retry = 0; cal_retry < 4; cal_retry++) {
		rtw_write32_mask(rtwdev, REG_FAS, BIT(21) | BIT(20), path);

		iqk_cmd = 0xf8000001 | ((bw + 3) << 8) | (BIT(path) << 4);

		rtw_dbg(rtwdev, RTW_DBG_RFK, "TXK_Trigger = %#x\n", iqk_cmd);

		rtw_write32(rtwdev, 0x1b00, iqk_cmd);

		usleep_range(10000, 11000);

		if (read_poll_timeout(!rtw_read32_mask, *tx_ok, *tx_ok,
				      1000, 20000, false,
				      rtwdev, 0x1b00, BIT(0))) {
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"tx iqk S%d timed out\n", path);

			rtw8814a_iqk_reset_nctl(rtwdev);
		} else {
			*tx_ok = !rtw_read32_mask(rtwdev, 0x1b08, BIT(26));

			if (*tx_ok)
				break;
		}
	}

	rtw_dbg(rtwdev, RTW_DBG_RFK, "S%d tx ==> 0x1b00 = 0x%x\n",
		path, rtw_read32(rtwdev, 0x1b00));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "S%d tx ==> 0x1b08 = 0x%x\n",
		path, rtw_read32(rtwdev, 0x1b08));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "S%d tx ==> cal_retry = %x\n",
		path, cal_retry);

	rtw_write32(rtwdev, 0x1b00, 0xf8000000 | (path << 1));

	if (*tx_ok) {
		*tx_matrix = rtw_read32(rtwdev, 0x1b38);

		rtw_dbg(rtwdev, RTW_DBG_RFK, "S%d_IQC = 0x%x\n",
			path, *tx_matrix);
	}
}

static void rtw8814a_iqk_rx_one_shot(struct rtw_dev *rtwdev, u8 path,
				     u32 *tx_matrix, bool *tx_ok)
{
	static const u16 iqk_apply[RTW_RF_PATH_MAX] = {
		REG_TXAGCIDX, REG_TX_AGC_B, REG_TX_AGC_C, REG_TX_AGC_D
	};
	u8 band = rtwdev->hal.current_band_type;
	u8 bw = rtwdev->hal.current_band_width;
	u32 rx_matrix;
	u8 cal_retry;
	u32 iqk_cmd;
	bool rx_ok;

	for (cal_retry = 0; cal_retry < 4; cal_retry++) {
		rtw_write32_mask(rtwdev, REG_FAS, BIT(21) | BIT(20), path);

		if (band == RTW_BAND_2G) {
			rtw_write_rf(rtwdev, path, RF_LUTDBG, BIT(11), 0x1);
			rtw_write_rf(rtwdev, path, RF_GAINTX, 0xfffff, 0x51ce1);

			switch (path) {
			case 0:
			case 1:
				rtw_write32(rtwdev, REG_RFE_PINMUX_B,
					    0x54775477);
				break;
			case 2:
				rtw_write32(rtwdev, REG_RFE_PINMUX_C,
					    0x54775477);
				break;
			case 3:
				rtw_write32(rtwdev, REG_RFE_INVSEL_D, 0x75400000);
				rtw_write32(rtwdev, REG_RFE_PINMUX_D,
					    0x77777777);
				break;
			}
		}

		iqk_cmd = 0xf8000001 | ((9 - bw) << 8) | (BIT(path) << 4);

		rtw_dbg(rtwdev, RTW_DBG_RFK, "RXK_Trigger = 0x%x\n", iqk_cmd);

		rtw_write32(rtwdev, 0x1b00, iqk_cmd);

		usleep_range(10000, 11000);

		if (read_poll_timeout(!rtw_read32_mask, rx_ok, rx_ok,
				      1000, 20000, false,
				      rtwdev, 0x1b00, BIT(0))) {
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"rx iqk S%d timed out\n", path);

			rtw8814a_iqk_reset_nctl(rtwdev);
		} else {
			rx_ok = !rtw_read32_mask(rtwdev, 0x1b08, BIT(26));

			if (rx_ok)
				break;
		}
	}

	rtw_dbg(rtwdev, RTW_DBG_RFK, "S%d rx ==> 0x1b00 = 0x%x\n",
		path, rtw_read32(rtwdev, 0x1b00));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "S%d rx ==> 0x1b08 = 0x%x\n",
		path, rtw_read32(rtwdev, 0x1b08));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "S%d rx ==> cal_retry = %x\n",
		path, cal_retry);

	rtw_write32(rtwdev, 0x1b00, 0xf8000000 | (path << 1));

	if (rx_ok) {
		rtw_write32(rtwdev, 0x1b3c, 0x20000000);
		rx_matrix = rtw_read32(rtwdev, 0x1b3c);

		rtw_dbg(rtwdev, RTW_DBG_RFK, "S%d_IQC = 0x%x\n",
			path, rx_matrix);
	}

	if (*tx_ok)
		rtw_write32(rtwdev, 0x1b38, *tx_matrix);
	else
		rtw_write32_mask(rtwdev, iqk_apply[path], BIT(0), 0x0);

	if (!rx_ok)
		rtw_write32_mask(rtwdev, iqk_apply[path],
				 BIT(11) | BIT(10), 0x0);

	if (band == RTW_BAND_2G)
		rtw_write_rf(rtwdev, path, RF_LUTDBG, BIT(11), 0x0);
}

static void rtw8814a_iqk(struct rtw_dev *rtwdev)
{
	u8 band = rtwdev->hal.current_band_type;
	u8 bw = rtwdev->hal.current_band_width;
	u32 tx_matrix[RTW_RF_PATH_MAX];
	bool tx_ok[RTW_RF_PATH_MAX];
	u8 path;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "IQK band = %d GHz bw = %d MHz\n",
		band == RTW_BAND_2G ? 2 : 5, (1 << (bw + 1)) * 10);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_TXMOD, BIT(19), 0x1);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_TXMOD, BIT(19), 0x1);
	rtw_write_rf(rtwdev, RF_PATH_C, RF_TXMOD, BIT(19), 0x1);
	rtw_write_rf(rtwdev, RF_PATH_D, RF_TXMOD, BIT(19), 0x1);

	rtw_write32_mask(rtwdev, REG_TXAGCIDX,
			 (BIT(11) | BIT(10) | BIT(0)), 0x401);
	rtw_write32_mask(rtwdev, REG_TX_AGC_B,
			 (BIT(11) | BIT(10) | BIT(0)), 0x401);
	rtw_write32_mask(rtwdev, REG_TX_AGC_C,
			 (BIT(11) | BIT(10) | BIT(0)), 0x401);
	rtw_write32_mask(rtwdev, REG_TX_AGC_D,
			 (BIT(11) | BIT(10) | BIT(0)), 0x401);

	if (band == RTW_BAND_5G)
		rtw_write32(rtwdev, 0x1b00, 0xf8000ff1);
	else
		rtw_write32(rtwdev, 0x1b00, 0xf8000ef1);

	usleep_range(1000, 1100);

	rtw_write32(rtwdev, 0x810, 0x20101063);
	rtw_write32(rtwdev, REG_DAC_RSTB, 0x0B00C000);

	for (path = RF_PATH_A; path < RTW_RF_PATH_MAX; path++)
		rtw8814a_lok_one_shot(rtwdev, path);

	for (path = RF_PATH_A; path < RTW_RF_PATH_MAX; path++)
		rtw8814a_iqk_tx_one_shot(rtwdev, path,
					 &tx_matrix[path], &tx_ok[path]);

	for (path = RF_PATH_A; path < RTW_RF_PATH_MAX; path++)
		rtw8814a_iqk_rx_one_shot(rtwdev, path,
					 &tx_matrix[path], &tx_ok[path]);
}

static void rtw8814a_do_iqk(struct rtw_dev *rtwdev)
{
	static const u32 backup_mac_reg[MAC_REG_NUM_8814] = {0x520, 0x550};
	static const u32 backup_bb_reg[BB_REG_NUM_8814] = {
		0xa14, 0x808, 0x838, 0x90c, 0x810, 0xcb0, 0xeb0,
		0x18b4, 0x1ab4, 0x1abc, 0x9a4, 0x764, 0xcbc, 0x910
	};
	static const u32 backup_rf_reg[RF_REG_NUM_8814] = {0x0};
	u32 rf_backup[RF_REG_NUM_8814][RTW_RF_PATH_MAX];
	u32 mac_backup[MAC_REG_NUM_8814];
	u32 bb_backup[BB_REG_NUM_8814];

	rtw8814a_iqk_backup_mac_bb(rtwdev, mac_backup, bb_backup,
				   backup_mac_reg, backup_bb_reg);
	rtw8814a_iqk_afe_setting(rtwdev, true);
	rtw8814a_iqk_backup_rf(rtwdev, rf_backup, backup_rf_reg);
	rtw8814a_iqk_configure_mac(rtwdev);
	rtw8814a_iqk(rtwdev);
	rtw8814a_iqk_reset_nctl(rtwdev); /* for 3-wire to BB use */
	rtw8814a_iqk_afe_setting(rtwdev, false);
	rtw8814a_iqk_restore_mac_bb(rtwdev, mac_backup, bb_backup,
				    backup_mac_reg, backup_bb_reg);
	rtw8814a_iqk_restore_rf(rtwdev, rf_backup, backup_rf_reg);
}

static void rtw8814a_phy_calibration(struct rtw_dev *rtwdev)
{
	rtw8814a_do_iqk(rtwdev);
}

static void rtw8814a_coex_cfg_init(struct rtw_dev *rtwdev)
{
}

static void rtw8814a_coex_cfg_ant_switch(struct rtw_dev *rtwdev, u8 ctrl_type,
					 u8 pos_type)
{
	/* Override rtw_coex_coex_ctrl_owner(). RF path C does not
	 * function when BIT_LTE_MUX_CTRL_PATH is set.
	 */
	rtw_write8_clr(rtwdev, REG_SYS_SDIO_CTRL + 3,
		       BIT_LTE_MUX_CTRL_PATH >> 24);
}

static void rtw8814a_coex_cfg_gnt_fix(struct rtw_dev *rtwdev)
{
}

static void rtw8814a_coex_cfg_gnt_debug(struct rtw_dev *rtwdev)
{
}

static void rtw8814a_coex_cfg_rfe_type(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_rfe *coex_rfe = &coex->rfe;

	/* Only needed to make rtw8814a_coex_cfg_ant_switch() run. */
	coex_rfe->ant_switch_exist = true;
}

static void rtw8814a_coex_cfg_wl_tx_power(struct rtw_dev *rtwdev, u8 wl_pwr)
{
}

static void rtw8814a_coex_cfg_wl_rx_gain(struct rtw_dev *rtwdev, bool low_gain)
{
}

static void rtw8814a_txagc_swing_offset(struct rtw_dev *rtwdev, u8 path,
					u8 tx_pwr_idx_offset,
					s8 *txagc_idx, u8 *swing_idx)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 swing_upper_bound = dm_info->default_ofdm_index + 10;
	s8 delta_pwr_idx = dm_info->delta_power_index[path];
	u8 swing_index = dm_info->default_ofdm_index;
	u8 max_tx_pwr_idx_offset = 0xf;
	u8 swing_lower_bound = 0;
	s8 agc_index = 0;

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

static void rtw8814a_pwrtrack_set_pwr(struct rtw_dev *rtwdev, u8 path,
				      u8 pwr_idx_offset)
{
	static const u32 txagc_reg[RTW_RF_PATH_MAX] = {
		REG_TX_AGC_A, REG_TX_AGC_B, REG_TX_AGC_C, REG_TX_AGC_D
	};
	static const u32 txscale_reg[RTW_RF_PATH_MAX] = {
		REG_TXSCALE_A, REG_TXSCALE_B, REG_TXSCALE_C, REG_TXSCALE_D
	};
	s8 txagc_idx;
	u8 swing_idx;

	rtw8814a_txagc_swing_offset(rtwdev, path, pwr_idx_offset,
				    &txagc_idx, &swing_idx);
	rtw_write32_mask(rtwdev, txagc_reg[path], GENMASK(29, 25),
			 txagc_idx);
	rtw_write32_mask(rtwdev, txscale_reg[path], BB_SWING_MASK,
			 rtw8814a_txscale_tbl[swing_idx]);
}

static void rtw8814a_pwrtrack_set(struct rtw_dev *rtwdev, u8 path)
{
	u8 max_pwr_idx = rtwdev->chip->max_power_index;
	u8 band_width = rtwdev->hal.current_band_width;
	u8 channel = rtwdev->hal.current_channel;
	u8 tx_rate = rtwdev->dm_info.tx_rate;
	u8 regd = rtw_regd_get(rtwdev);
	u8 pwr_idx_offset, tx_pwr_idx;

	tx_pwr_idx = rtw_phy_get_tx_power_index(rtwdev, path, tx_rate,
						band_width, channel, regd);

	tx_pwr_idx = min_t(u8, tx_pwr_idx, max_pwr_idx);

	pwr_idx_offset = max_pwr_idx - tx_pwr_idx;

	rtw8814a_pwrtrack_set_pwr(rtwdev, path, pwr_idx_offset);
}

static void rtw8814a_phy_pwrtrack_path(struct rtw_dev *rtwdev,
				       struct rtw_swing_table *swing_table,
				       u8 path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 power_idx_cur, power_idx_last;
	u8 delta;

	/* 8814A only has one thermal meter at PATH A */
	delta = rtw_phy_pwrtrack_get_delta(rtwdev, RF_PATH_A);

	power_idx_last = dm_info->delta_power_index[path];
	power_idx_cur = rtw_phy_pwrtrack_get_pwridx(rtwdev, swing_table,
						    path, RF_PATH_A, delta);

	/* if delta of power indexes are the same, just skip */
	if (power_idx_cur == power_idx_last)
		return;

	dm_info->delta_power_index[path] = power_idx_cur;
	rtw8814a_pwrtrack_set(rtwdev, path);
}

static void rtw8814a_phy_pwrtrack(struct rtw_dev *rtwdev)
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

	for (path = RF_PATH_A; path < rtwdev->hal.rf_path_num; path++)
		rtw8814a_phy_pwrtrack_path(rtwdev, &swing_table, path);

iqk:
	if (rtw_phy_pwrtrack_need_iqk(rtwdev))
		rtw8814a_do_iqk(rtwdev);
}

static void rtw8814a_pwr_track(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	if (!dm_info->pwr_trk_triggered) {
		rtw_write_rf(rtwdev, RF_PATH_A, RF_T_METER,
			     GENMASK(17, 16), 0x03);
		dm_info->pwr_trk_triggered = true;
		return;
	}

	rtw8814a_phy_pwrtrack(rtwdev);
	dm_info->pwr_trk_triggered = false;
}

static void rtw8814a_phy_cck_pd_set(struct rtw_dev *rtwdev, u8 new_lvl)
{
	static const u8 pd[CCK_PD_LV_MAX] = {0x40, 0x83, 0xcd, 0xdd, 0xed};
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	/* Override rtw_phy_cck_pd_lv_link(). It implements something
	 * like type 2/3/4. We need type 1 here.
	 */
	if (rtw_is_assoc(rtwdev)) {
		if (dm_info->min_rssi > 60) {
			new_lvl = CCK_PD_LV3;
		} else if (dm_info->min_rssi > 35) {
			new_lvl = CCK_PD_LV2;
		} else if (dm_info->min_rssi > 20) {
			if (dm_info->cck_fa_avg > 500)
				new_lvl = CCK_PD_LV2;
			else if (dm_info->cck_fa_avg < 250)
				new_lvl = CCK_PD_LV1;
			else
				return;
		} else {
			new_lvl = CCK_PD_LV1;
		}
	}

	rtw_dbg(rtwdev, RTW_DBG_PHY, "lv: (%d) -> (%d)\n",
		dm_info->cck_pd_lv[RTW_CHANNEL_WIDTH_20][RF_PATH_A], new_lvl);

	if (dm_info->cck_pd_lv[RTW_CHANNEL_WIDTH_20][RF_PATH_A] == new_lvl)
		return;

	dm_info->cck_fa_avg = CCK_FA_AVG_RESET;
	dm_info->cck_pd_lv[RTW_CHANNEL_WIDTH_20][RF_PATH_A] = new_lvl;

	rtw_write8(rtwdev, REG_CCK_PD_TH, pd[new_lvl]);
}

static void rtw8814a_led_set(struct led_classdev *led,
			     enum led_brightness brightness)
{
	struct rtw_dev *rtwdev = container_of(led, struct rtw_dev, led_cdev);
	u32 led_gpio_cfg;

	led_gpio_cfg = rtw_read32(rtwdev, REG_GPIO_PIN_CTRL_2);
	led_gpio_cfg |= BIT(16) | BIT(17) | BIT(21) | BIT(22);

	if (brightness == LED_OFF) {
		led_gpio_cfg |= BIT(8) | BIT(9) | BIT(13) | BIT(14);
	} else {
		led_gpio_cfg &= ~(BIT(8) | BIT(9) | BIT(13) | BIT(14));
		led_gpio_cfg &= ~(BIT(0) | BIT(1) | BIT(5) | BIT(6));
	}

	rtw_write32(rtwdev, REG_GPIO_PIN_CTRL_2, led_gpio_cfg);
}

static void rtw8814a_fill_txdesc_checksum(struct rtw_dev *rtwdev,
					  struct rtw_tx_pkt_info *pkt_info,
					  u8 *txdesc)
{
	size_t words = 32 / 2; /* calculate the first 32 bytes (16 words) */

	fill_txdesc_checksum_common(txdesc, words);
}

static const struct rtw_chip_ops rtw8814a_ops = {
	.power_on		= rtw_power_on,
	.power_off		= rtw_power_off,
	.phy_set_param		= rtw8814a_phy_set_param,
	.read_efuse		= rtw8814a_read_efuse,
	.query_phy_status	= rtw8814a_query_phy_status,
	.set_channel		= rtw8814a_set_channel,
	.mac_init		= rtw8814a_mac_init,
	.read_rf		= rtw_phy_read_rf,
	.write_rf		= rtw_phy_write_rf_reg_sipi,
	.set_tx_power_index	= rtw8814a_set_tx_power_index,
	.set_antenna		= NULL,
	.cfg_ldo25		= rtw8814a_cfg_ldo25,
	.efuse_grant		= rtw8814a_efuse_grant,
	.set_ampdu_factor	= rtw8814a_set_ampdu_factor,
	.false_alarm_statistics	= rtw8814a_false_alarm_statistics,
	.phy_calibration	= rtw8814a_phy_calibration,
	.cck_pd_set		= rtw8814a_phy_cck_pd_set,
	.pwr_track		= rtw8814a_pwr_track,
	.config_bfee		= NULL,
	.set_gid_table		= NULL,
	.cfg_csi_rate		= NULL,
	.led_set		= rtw8814a_led_set,
	.fill_txdesc_checksum	= rtw8814a_fill_txdesc_checksum,

	.coex_set_init		= rtw8814a_coex_cfg_init,
	.coex_set_ant_switch	= rtw8814a_coex_cfg_ant_switch,
	.coex_set_gnt_fix	= rtw8814a_coex_cfg_gnt_fix,
	.coex_set_gnt_debug	= rtw8814a_coex_cfg_gnt_debug,
	.coex_set_rfe_type	= rtw8814a_coex_cfg_rfe_type,
	.coex_set_wl_tx_power	= rtw8814a_coex_cfg_wl_tx_power,
	.coex_set_wl_rx_gain	= rtw8814a_coex_cfg_wl_rx_gain,
};

static const struct rtw_rqpn rqpn_table_8814a[] = {
	/* SDIO */
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL, /* vo vi */
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,	 /* be bk */
	 RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH},	 /* mg hi */
	/* PCIE */
	{RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
	/* USB, 2 bulk out */
	{RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH,
	 RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
	/* USB, 3 bulk out */
	{RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
	/* USB, 4 bulk out */
	{RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
};

static const struct rtw_prioq_addrs prioq_addrs_8814a = {
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

static const struct rtw_page_table page_table_8814a[] = {
	/* SDIO */
	{0, 0, 0, 0, 0},	/* hq nq lq exq gapq */
	/* PCIE */
	{32, 32, 32, 32, 0},
	/* USB, 2 bulk out */
	{32, 32, 32, 32, 0},
	/* USB, 3 bulk out */
	{32, 32, 32, 32, 0},
	/* USB, 4 bulk out */
	{32, 32, 32, 32, 0},
};

static const struct rtw_intf_phy_para_table phy_para_table_8814a = {};

static const struct rtw_hw_reg rtw8814a_dig[] = {
	[0] = { .addr = 0xc50, .mask = 0x7f },
	[1] = { .addr = 0xe50, .mask = 0x7f },
	[2] = { .addr = 0x1850, .mask = 0x7f },
	[3] = { .addr = 0x1a50, .mask = 0x7f },
};

static const struct rtw_rfe_def rtw8814a_rfe_defs[] = {
	[0] = { .phy_pg_tbl	= &rtw8814a_bb_pg_type0_tbl,
		.txpwr_lmt_tbl	= &rtw8814a_txpwr_lmt_type0_tbl,
		.pwr_track_tbl	= &rtw8814a_rtw_pwrtrk_type0_tbl },
	[1] = { .phy_pg_tbl	= &rtw8814a_bb_pg_tbl,
		.txpwr_lmt_tbl	= &rtw8814a_txpwr_lmt_type1_tbl,
		.pwr_track_tbl	= &rtw8814a_rtw_pwrtrk_tbl },
};

/* rssi in percentage % (dbm = % - 100) */
static const u8 wl_rssi_step_8814a[] = {60, 50, 44, 30};
static const u8 bt_rssi_step_8814a[] = {30, 30, 30, 30};

/* wl_tx_dec_power, bt_tx_dec_power, wl_rx_gain, bt_rx_lna_constrain */
static const struct coex_rf_para rf_para_tx_8814a[] = {
	{0, 0, false, 7},  /* for normal */
	{0, 16, false, 7}, /* for WL-CPT */
	{4, 0, true, 1},
	{3, 6, true, 1},
	{2, 9, true, 1},
	{1, 13, true, 1}
};

static const struct coex_rf_para rf_para_rx_8814a[] = {
	{0, 0, false, 7},  /* for normal */
	{0, 16, false, 7}, /* for WL-CPT */
	{4, 0, true, 1},
	{3, 6, true, 1},
	{2, 9, true, 1},
	{1, 13, true, 1}
};

static_assert(ARRAY_SIZE(rf_para_tx_8814a) == ARRAY_SIZE(rf_para_rx_8814a));

const struct rtw_chip_info rtw8814a_hw_spec = {
	.ops = &rtw8814a_ops,
	.id = RTW_CHIP_TYPE_8814A,
	.fw_name = "rtw88/rtw8814a_fw.bin",
	.wlan_cpu = RTW_WCPU_3081,
	.tx_pkt_desc_sz = 40,
	.tx_buf_desc_sz = 16,
	.rx_pkt_desc_sz = 24,
	.rx_buf_desc_sz = 8,
	.phy_efuse_size = 1024,
	.log_efuse_size = 512,
	.ptct_efuse_size = 0,
	.txff_size = (2048 - 10) * TX_PAGE_SIZE,
	.rxff_size = 23552,
	.rsvd_drv_pg_num = 8,
	.band = RTW_BAND_2G | RTW_BAND_5G,
	.page_size = TX_PAGE_SIZE,
	.csi_buf_pg_num = 0,
	.dig_min = 0x1c,
	.txgi_factor = 1,
	.is_pwr_by_rate_dec = true,
	.rx_ldpc = true,
	.max_power_index = 0x3f,
	.ampdu_density = IEEE80211_HT_MPDU_DENSITY_2,
	.amsdu_in_ampdu = false, /* RX speed is better without AMSDU */
	.usb_tx_agg_desc_num = 3,
	.hw_feature_report = false,
	.c2h_ra_report_size = 6,
	.old_datarate_fb_limit = false,
	.ht_supported = true,
	.vht_supported = true,
	.lps_deep_mode_supported = BIT(LPS_DEEP_MODE_LCLK),
	.sys_func_en = 0xDC,
	.pwr_on_seq = card_enable_flow_8814a,
	.pwr_off_seq = card_disable_flow_8814a,
	.rqpn_table = rqpn_table_8814a,
	.prioq_addrs = &prioq_addrs_8814a,
	.page_table = page_table_8814a,
	.intf_table = &phy_para_table_8814a,
	.dig = rtw8814a_dig,
	.dig_cck = NULL,
	.rf_base_addr = {0x2800, 0x2c00, 0x3800, 0x3c00},
	.rf_sipi_addr = {0xc90, 0xe90, 0x1890, 0x1a90},
	.ltecoex_addr = NULL,
	.mac_tbl = &rtw8814a_mac_tbl,
	.agc_tbl = &rtw8814a_agc_tbl,
	.bb_tbl = &rtw8814a_bb_tbl,
	.rf_tbl = {&rtw8814a_rf_a_tbl, &rtw8814a_rf_b_tbl,
		   &rtw8814a_rf_c_tbl, &rtw8814a_rf_d_tbl},
	.rfe_defs = rtw8814a_rfe_defs,
	.rfe_defs_size = ARRAY_SIZE(rtw8814a_rfe_defs),
	.iqk_threshold = 8,
	.max_scan_ie_len = IEEE80211_MAX_DATA_LEN,

	.coex_para_ver = 0,
	.bt_desired_ver = 0,
	.scbd_support = false,
	.new_scbd10_def = false,
	.ble_hid_profile_support = false,
	.wl_mimo_ps_support = false,
	.pstdma_type = COEX_PSTDMA_FORCE_LPSOFF,
	.bt_rssi_type = COEX_BTRSSI_RATIO,
	.ant_isolation = 15,
	.rssi_tolerance = 2,
	.wl_rssi_step = wl_rssi_step_8814a,
	.bt_rssi_step = bt_rssi_step_8814a,
	.table_sant_num = 0,
	.table_sant = NULL,
	.table_nsant_num = 0,
	.table_nsant = NULL,
	.tdma_sant_num = 0,
	.tdma_sant = NULL,
	.tdma_nsant_num = 0,
	.tdma_nsant = NULL,
	.wl_rf_para_num = ARRAY_SIZE(rf_para_tx_8814a),
	.wl_rf_para_tx = rf_para_tx_8814a,
	.wl_rf_para_rx = rf_para_rx_8814a,
	.bt_afh_span_bw20 = 0x24,
	.bt_afh_span_bw40 = 0x36,
	.afh_5g_num = 0,
	.afh_5g = NULL,
	.coex_info_hw_regs_num = 0,
	.coex_info_hw_regs = NULL,
};
EXPORT_SYMBOL(rtw8814a_hw_spec);

MODULE_FIRMWARE("rtw88/rtw8814a_fw.bin");

MODULE_AUTHOR("Bitterblue Smith <rtl8821cerfe2@gmail.com>");
MODULE_DESCRIPTION("Realtek 802.11ac wireless 8814a driver");
MODULE_LICENSE("Dual BSD/GPL");
