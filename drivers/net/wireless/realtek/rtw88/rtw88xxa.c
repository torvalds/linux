// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2024  Realtek Corporation
 */

#include <linux/usb.h>
#include "main.h"
#include "coex.h"
#include "phy.h"
#include "rtw88xxa.h"
#include "mac.h"
#include "reg.h"
#include "sec.h"
#include "debug.h"
#include "bf.h"
#include "efuse.h"
#include "usb.h"

void rtw88xxa_efuse_grant(struct rtw_dev *rtwdev, bool on)
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
EXPORT_SYMBOL(rtw88xxa_efuse_grant);

static void rtw8812a_read_amplifier_type(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;

	efuse->ext_pa_2g = (efuse->pa_type_2g & BIT(5)) &&
			   (efuse->pa_type_2g & BIT(4));
	efuse->ext_lna_2g = (efuse->lna_type_2g & BIT(7)) &&
			    (efuse->lna_type_2g & BIT(3));

	efuse->ext_pa_5g = (efuse->pa_type_5g & BIT(1)) &&
			   (efuse->pa_type_5g & BIT(0));
	efuse->ext_lna_5g = (efuse->lna_type_5g & BIT(7)) &&
			    (efuse->lna_type_5g & BIT(3));

	/* For rtw_phy_cond2: */
	if (efuse->ext_pa_2g) {
		u8 ext_type_pa_2g_a = u8_get_bits(efuse->lna_type_2g, BIT(2));
		u8 ext_type_pa_2g_b = u8_get_bits(efuse->lna_type_2g, BIT(6));

		efuse->gpa_type = (ext_type_pa_2g_b << 2) | ext_type_pa_2g_a;
	}

	if (efuse->ext_pa_5g) {
		u8 ext_type_pa_5g_a = u8_get_bits(efuse->lna_type_5g, BIT(2));
		u8 ext_type_pa_5g_b = u8_get_bits(efuse->lna_type_5g, BIT(6));

		efuse->apa_type = (ext_type_pa_5g_b << 2) | ext_type_pa_5g_a;
	}

	if (efuse->ext_lna_2g) {
		u8 ext_type_lna_2g_a = u8_get_bits(efuse->lna_type_2g,
						   BIT(1) | BIT(0));
		u8 ext_type_lna_2g_b = u8_get_bits(efuse->lna_type_2g,
						   BIT(5) | BIT(4));

		efuse->glna_type = (ext_type_lna_2g_b << 2) | ext_type_lna_2g_a;
	}

	if (efuse->ext_lna_5g) {
		u8 ext_type_lna_5g_a = u8_get_bits(efuse->lna_type_5g,
						   BIT(1) | BIT(0));
		u8 ext_type_lna_5g_b = u8_get_bits(efuse->lna_type_5g,
						   BIT(5) | BIT(4));

		efuse->alna_type = (ext_type_lna_5g_b << 2) | ext_type_lna_5g_a;
	}
}

static void rtw8812a_read_rfe_type(struct rtw_dev *rtwdev,
				   struct rtw88xxa_efuse *map)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;

	if (map->rfe_option == 0xff) {
		if (rtwdev->hci.type == RTW_HCI_TYPE_USB)
			efuse->rfe_option = 0;
		else if (rtwdev->hci.type == RTW_HCI_TYPE_PCIE)
			efuse->rfe_option = 2;
		else
			efuse->rfe_option = 4;
	} else if (map->rfe_option & BIT(7)) {
		if (efuse->ext_lna_5g) {
			if (efuse->ext_pa_5g) {
				if (efuse->ext_lna_2g && efuse->ext_pa_2g)
					efuse->rfe_option = 3;
				else
					efuse->rfe_option = 0;
			} else {
				efuse->rfe_option = 2;
			}
		} else {
			efuse->rfe_option = 4;
		}
	} else {
		efuse->rfe_option = map->rfe_option & 0x3f;

		/* Due to other customer already use incorrect EFUSE map for
		 * their product. We need to add workaround to prevent to
		 * modify spec and notify all customer to revise the IC 0xca
		 * content.
		 */
		if (efuse->rfe_option == 4 &&
		    (efuse->ext_pa_5g || efuse->ext_pa_2g ||
		     efuse->ext_lna_5g || efuse->ext_lna_2g)) {
			if (rtwdev->hci.type == RTW_HCI_TYPE_USB)
				efuse->rfe_option = 0;
			else if (rtwdev->hci.type == RTW_HCI_TYPE_PCIE)
				efuse->rfe_option = 2;
		}
	}
}

static void rtw88xxa_read_usb_type(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_hal *hal = &rtwdev->hal;
	u8 antenna = 0;
	u8 wmode = 0;
	u8 val8, i;

	efuse->hw_cap.bw = BIT(RTW_CHANNEL_WIDTH_20) |
			   BIT(RTW_CHANNEL_WIDTH_40) |
			   BIT(RTW_CHANNEL_WIDTH_80);
	efuse->hw_cap.ptcl = EFUSE_HW_CAP_PTCL_VHT;

	if (rtwdev->chip->id == RTW_CHIP_TYPE_8821A)
		efuse->hw_cap.nss = 1;
	else
		efuse->hw_cap.nss = 2;

	if (rtwdev->chip->id == RTW_CHIP_TYPE_8821A)
		goto print_hw_cap;

	for (i = 0; i < 2; i++) {
		rtw_read8_physical_efuse(rtwdev, 1019 - i, &val8);

		antenna = u8_get_bits(val8, GENMASK(7, 5));
		if (antenna)
			break;
		antenna = u8_get_bits(val8, GENMASK(3, 1));
		if (antenna)
			break;
	}

	for (i = 0; i < 2; i++) {
		rtw_read8_physical_efuse(rtwdev, 1021 - i, &val8);

		wmode = u8_get_bits(val8, GENMASK(3, 2));
		if (wmode)
			break;
	}

	if (antenna == 1) {
		rtw_info(rtwdev, "This RTL8812AU says it is 1T1R.\n");

		efuse->hw_cap.nss = 1;
		hal->rf_type = RF_1T1R;
		hal->rf_path_num = 1;
		hal->rf_phy_num = 1;
		hal->antenna_tx = BB_PATH_A;
		hal->antenna_rx = BB_PATH_A;
	} else {
		/* Override rtw_chip_parameter_setup(). It detects 8812au as 1T1R. */
		efuse->hw_cap.nss = 2;
		hal->rf_type = RF_2T2R;
		hal->rf_path_num = 2;
		hal->rf_phy_num = 2;
		hal->antenna_tx = BB_PATH_AB;
		hal->antenna_rx = BB_PATH_AB;

		if (antenna == 2 && wmode == 2) {
			rtw_info(rtwdev, "This RTL8812AU says it can't do VHT.\n");

			/* Can't be EFUSE_HW_CAP_IGNORE and can't be
			 * EFUSE_HW_CAP_PTCL_VHT, so make it 1.
			 */
			efuse->hw_cap.ptcl = 1;
			efuse->hw_cap.bw &= ~BIT(RTW_CHANNEL_WIDTH_80);
		}
	}

print_hw_cap:
	rtw_dbg(rtwdev, RTW_DBG_EFUSE,
		"hw cap: hci=0x%02x, bw=0x%02x, ptcl=0x%02x, ant_num=%d, nss=%d\n",
		efuse->hw_cap.hci, efuse->hw_cap.bw, efuse->hw_cap.ptcl,
		efuse->hw_cap.ant_num, efuse->hw_cap.nss);
}

int rtw88xxa_read_efuse(struct rtw_dev *rtwdev, u8 *log_map)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw88xxa_efuse *map;
	int i;

	if (chip->id == RTW_CHIP_TYPE_8812A)
		rtwdev->hal.cut_version += 1;

	if (rtw_dbg_is_enabled(rtwdev, RTW_DBG_EFUSE))
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 1,
			       log_map, chip->log_efuse_size, true);

	map = (struct rtw88xxa_efuse *)log_map;

	efuse->rf_board_option = map->rf_board_option;
	efuse->crystal_cap = map->xtal_k;
	if (efuse->crystal_cap == 0xff)
		efuse->crystal_cap = 0x20;
	efuse->pa_type_2g = map->pa_type;
	efuse->pa_type_5g = map->pa_type;
	efuse->lna_type_2g = map->lna_type_2g;
	efuse->lna_type_5g = map->lna_type_5g;
	if (chip->id == RTW_CHIP_TYPE_8812A) {
		rtw8812a_read_amplifier_type(rtwdev);
		rtw8812a_read_rfe_type(rtwdev, map);

		efuse->usb_mode_switch = u8_get_bits(map->usb_mode, BIT(1));
	}
	efuse->channel_plan = map->channel_plan;
	efuse->country_code[0] = map->country_code[0];
	efuse->country_code[1] = map->country_code[1];
	efuse->bt_setting = map->rf_bt_setting;
	efuse->regd = map->rf_board_option & 0x7;
	efuse->thermal_meter[0] = map->thermal_meter;
	efuse->thermal_meter[1] = map->thermal_meter;
	efuse->thermal_meter_k = map->thermal_meter;
	efuse->tx_bb_swing_setting_2g = map->tx_bb_swing_setting_2g;
	efuse->tx_bb_swing_setting_5g = map->tx_bb_swing_setting_5g;

	rtw88xxa_read_usb_type(rtwdev);

	if (chip->id == RTW_CHIP_TYPE_8821A)
		efuse->btcoex = rtw_read32_mask(rtwdev, REG_WL_BT_PWR_CTRL,
						BIT_BT_FUNC_EN);
	else
		efuse->btcoex = (map->rf_board_option & 0xe0) == 0x20;
	efuse->share_ant = !!(efuse->bt_setting & BIT(0));

	/* No antenna diversity because it's disabled in the vendor driver */
	efuse->ant_div_cfg = 0;

	efuse->ant_div_type = map->rf_antenna_option;
	if (efuse->ant_div_type == 0xff)
		efuse->ant_div_type = 0x3;

	for (i = 0; i < 4; i++)
		efuse->txpwr_idx_table[i] = map->txpwr_idx_table[i];

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_USB:
		if (chip->id == RTW_CHIP_TYPE_8821A)
			ether_addr_copy(efuse->addr, map->rtw8821au.mac_addr);
		else
			ether_addr_copy(efuse->addr, map->rtw8812au.mac_addr);
		break;
	case RTW_HCI_TYPE_PCIE:
	case RTW_HCI_TYPE_SDIO:
	default:
		/* unsupported now */
		return -EOPNOTSUPP;
	}

	return 0;
}
EXPORT_SYMBOL(rtw88xxa_read_efuse);

static void rtw88xxa_reset_8051(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	u8 val8;

	/* Reset MCU IO Wrapper */
	rtw_write8_clr(rtwdev, REG_RSV_CTRL, BIT(1));
	if (chip->id == RTW_CHIP_TYPE_8812A)
		rtw_write8_clr(rtwdev, REG_RSV_CTRL + 1, BIT(3));
	else
		rtw_write8_clr(rtwdev, REG_RSV_CTRL + 1, BIT(0));

	val8 = rtw_read8(rtwdev, REG_SYS_FUNC_EN + 1);
	rtw_write8(rtwdev, REG_SYS_FUNC_EN + 1, val8 & ~BIT(2));

	/* Enable MCU IO Wrapper */
	rtw_write8_clr(rtwdev, REG_RSV_CTRL, BIT(1));
	if (chip->id == RTW_CHIP_TYPE_8812A)
		rtw_write8_set(rtwdev, REG_RSV_CTRL + 1, BIT(3));
	else
		rtw_write8_set(rtwdev, REG_RSV_CTRL + 1, BIT(0));

	rtw_write8(rtwdev, REG_SYS_FUNC_EN + 1, val8 | BIT(2));
}

/* A lightweight deinit function */
static void rtw88xxau_hw_reset(struct rtw_dev *rtwdev)
{
	u8 val8;

	if (!(rtw_read8(rtwdev, REG_MCUFW_CTRL) & BIT_RAM_DL_SEL))
		return;

	rtw88xxa_reset_8051(rtwdev);
	rtw_write8(rtwdev, REG_MCUFW_CTRL, 0x00);

	/* before BB reset should do clock gated */
	rtw_write32_set(rtwdev, REG_FPGA0_XCD_RF_PARA, BIT(6));

	/* reset BB */
	rtw_write8_clr(rtwdev, REG_SYS_FUNC_EN, BIT(0) | BIT(1));

	/* reset RF */
	rtw_write8(rtwdev, REG_RF_CTRL, 0);

	/* reset TRX path */
	rtw_write16(rtwdev, REG_CR, 0);

	/* reset MAC, reg0x5[1], auto FSM off */
	rtw_write8_set(rtwdev, REG_APS_FSMCO + 1, APS_FSMCO_MAC_OFF >> 8);

	/* check if reg0x5[1] auto cleared */
	if (read_poll_timeout_atomic(rtw_read8, val8,
				     !(val8 & (APS_FSMCO_MAC_OFF >> 8)),
				     1, 5000, false,
				     rtwdev, REG_APS_FSMCO + 1))
		rtw_err(rtwdev, "%s: timed out waiting for 0x5[1]\n", __func__);

	/* reg0x5[0], auto FSM on */
	val8 |= APS_FSMCO_MAC_ENABLE >> 8;
	rtw_write8(rtwdev, REG_APS_FSMCO + 1, val8);

	rtw_write8_clr(rtwdev, REG_SYS_FUNC_EN + 1, BIT(4) | BIT(7));
	rtw_write8_set(rtwdev, REG_SYS_FUNC_EN + 1, BIT(4) | BIT(7));
}

static int rtw88xxau_init_power_on(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	u16 val16;
	int ret;

	ret = rtw_pwr_seq_parser(rtwdev, chip->pwr_on_seq);
	if (ret) {
		rtw_err(rtwdev, "power on flow failed\n");
		return ret;
	}

	rtw_write16(rtwdev, REG_CR, 0);
	val16 = BIT_HCI_TXDMA_EN | BIT_HCI_RXDMA_EN | BIT_TXDMA_EN |
		BIT_RXDMA_EN | BIT_PROTOCOL_EN | BIT_SCHEDULE_EN |
		BIT_MAC_SEC_EN | BIT_32K_CAL_TMR_EN;
	rtw_write16_set(rtwdev, REG_CR, val16);

	if (chip->id == RTW_CHIP_TYPE_8821A) {
		if (rtw_read8(rtwdev, REG_SYS_CFG1 + 3) & BIT(0))
			rtw_write8_set(rtwdev, REG_LDO_SWR_CTRL, BIT(6));
	}

	return ret;
}

static int rtw88xxa_llt_write(struct rtw_dev *rtwdev, u32 address, u32 data)
{
	u32 value = BIT_LLT_WRITE_ACCESS | (address << 8) | data;
	int count = 0;

	rtw_write32(rtwdev, REG_LLT_INIT, value);

	do {
		if (!rtw_read32_mask(rtwdev, REG_LLT_INIT, BIT(31) | BIT(30)))
			break;

		if (count > 20) {
			rtw_err(rtwdev, "Failed to poll write LLT done at %d!\n",
				address);
			return -EBUSY;
		}
	} while (++count);

	return 0;
}

static int rtw88xxa_llt_init(struct rtw_dev *rtwdev, u32 boundary)
{
	u32 last_entry = 255;
	int status = 0;
	u32 i;

	for (i = 0; i < boundary - 1; i++) {
		status = rtw88xxa_llt_write(rtwdev, i, i + 1);
		if (status)
			return status;
	}

	status = rtw88xxa_llt_write(rtwdev, boundary - 1, 0xFF);
	if (status)
		return status;

	for (i = boundary; i < last_entry; i++) {
		status = rtw88xxa_llt_write(rtwdev, i, i + 1);
		if (status)
			return status;
	}

	status = rtw88xxa_llt_write(rtwdev, last_entry, boundary);

	return status;
}

static void rtw88xxau_init_queue_reserved_page(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_fifo_conf *fifo = &rtwdev->fifo;
	const struct rtw_page_table *pg_tbl = NULL;
	u16 pubq_num;
	u32 val32;

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
		pg_tbl = &chip->page_table[1];
		break;
	case RTW_HCI_TYPE_USB:
		if (rtwdev->hci.bulkout_num == 2)
			pg_tbl = &chip->page_table[2];
		else if (rtwdev->hci.bulkout_num == 3)
			pg_tbl = &chip->page_table[3];
		else if (rtwdev->hci.bulkout_num == 4)
			pg_tbl = &chip->page_table[4];
		break;
	case RTW_HCI_TYPE_SDIO:
		pg_tbl = &chip->page_table[0];
		break;
	default:
		break;
	}

	pubq_num = fifo->acq_pg_num - pg_tbl->hq_num - pg_tbl->lq_num -
		   pg_tbl->nq_num - pg_tbl->exq_num - pg_tbl->gapq_num;

	val32 = BIT_RQPN_NE(pg_tbl->nq_num, pg_tbl->exq_num);
	rtw_write32(rtwdev, REG_RQPN_NPQ, val32);

	val32 = BIT_RQPN_HLP(pg_tbl->hq_num, pg_tbl->lq_num, pubq_num);
	rtw_write32(rtwdev, REG_RQPN, val32);
}

static void rtw88xxau_init_tx_buffer_boundary(struct rtw_dev *rtwdev)
{
	struct rtw_fifo_conf *fifo = &rtwdev->fifo;

	rtw_write8(rtwdev, REG_BCNQ_BDNY, fifo->rsvd_boundary);
	rtw_write8(rtwdev, REG_MGQ_BDNY, fifo->rsvd_boundary);
	rtw_write8(rtwdev, REG_WMAC_LBK_BF_HD, fifo->rsvd_boundary);
	rtw_write8(rtwdev, REG_TRXFF_BNDY, fifo->rsvd_boundary);
	rtw_write8(rtwdev, REG_DWBCN0_CTRL + 1, fifo->rsvd_boundary);
}

static int rtw88xxau_init_queue_priority(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	u8 bulkout_num = rtwdev->hci.bulkout_num;
	const struct rtw_rqpn *rqpn = NULL;
	u16 txdma_pq_map;

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
		rqpn = &chip->rqpn_table[1];
		break;
	case RTW_HCI_TYPE_USB:
		if (bulkout_num == 2)
			rqpn = &chip->rqpn_table[2];
		else if (bulkout_num == 3)
			rqpn = &chip->rqpn_table[3];
		else if (bulkout_num == 4)
			rqpn = &chip->rqpn_table[4];
		else
			return -EINVAL;
		break;
	case RTW_HCI_TYPE_SDIO:
		rqpn = &chip->rqpn_table[0];
		break;
	default:
		return -EINVAL;
	}

	rtwdev->fifo.rqpn = rqpn;

	txdma_pq_map = rtw_read16(rtwdev, REG_TXDMA_PQ_MAP) & 0x7;
	txdma_pq_map |= BIT_TXDMA_HIQ_MAP(rqpn->dma_map_hi);
	txdma_pq_map |= BIT_TXDMA_MGQ_MAP(rqpn->dma_map_mg);
	txdma_pq_map |= BIT_TXDMA_BKQ_MAP(rqpn->dma_map_bk);
	txdma_pq_map |= BIT_TXDMA_BEQ_MAP(rqpn->dma_map_be);
	txdma_pq_map |= BIT_TXDMA_VIQ_MAP(rqpn->dma_map_vi);
	txdma_pq_map |= BIT_TXDMA_VOQ_MAP(rqpn->dma_map_vo);
	rtw_write16(rtwdev, REG_TXDMA_PQ_MAP, txdma_pq_map);

	/* Packet in Hi Queue Tx immediately (No constraint for ATIM Period). */
	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_USB && bulkout_num == 4)
		rtw_write8(rtwdev, REG_HIQ_NO_LMT_EN, 0xff);

	return 0;
}

static void rtw88xxa_init_wmac_setting(struct rtw_dev *rtwdev)
{
	rtw_write16(rtwdev, REG_RXFLTMAP0, 0xffff);
	rtw_write16(rtwdev, REG_RXFLTMAP1, 0x0400);
	rtw_write16(rtwdev, REG_RXFLTMAP2, 0xffff);

	rtw_write32(rtwdev, REG_MAR, 0xffffffff);
	rtw_write32(rtwdev, REG_MAR + 4, 0xffffffff);
}

static void rtw88xxa_init_adaptive_ctrl(struct rtw_dev *rtwdev)
{
	rtw_write32_mask(rtwdev, REG_RRSR, 0xfffff, 0xffff1);
	rtw_write16(rtwdev, REG_RETRY_LIMIT, 0x3030);
}

static void rtw88xxa_init_edca(struct rtw_dev *rtwdev)
{
	rtw_write16(rtwdev, REG_SPEC_SIFS, 0x100a);
	rtw_write16(rtwdev, REG_MAC_SPEC_SIFS, 0x100a);

	rtw_write16(rtwdev, REG_SIFS, 0x100a);
	rtw_write16(rtwdev, REG_SIFS + 2, 0x100a);

	rtw_write32(rtwdev, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtw_write32(rtwdev, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtw_write32(rtwdev, REG_EDCA_VI_PARAM, 0x005EA324);
	rtw_write32(rtwdev, REG_EDCA_VO_PARAM, 0x002FA226);

	rtw_write8(rtwdev, REG_USTIME_TSF, 0x50);
	rtw_write8(rtwdev, REG_USTIME_EDCA, 0x50);
}

static void rtw88xxau_tx_aggregation(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;

	rtw_write32_mask(rtwdev, REG_DWBCN0_CTRL, 0xf0,
			 chip->usb_tx_agg_desc_num);

	if (chip->id == RTW_CHIP_TYPE_8821A)
		rtw_write8(rtwdev, REG_DWBCN1_CTRL,
			   chip->usb_tx_agg_desc_num << 1);
}

static void rtw88xxa_init_beacon_parameters(struct rtw_dev *rtwdev)
{
	u16 val16;

	val16 = (BIT_DIS_TSF_UDT << 8) | BIT_DIS_TSF_UDT;
	if (rtwdev->efuse.btcoex)
		val16 |= BIT_EN_BCN_FUNCTION;
	rtw_write16(rtwdev, REG_BCN_CTRL, val16);

	rtw_write32_mask(rtwdev, REG_TBTT_PROHIBIT, 0xfffff, WLAN_TBTT_TIME);
	rtw_write8(rtwdev, REG_DRVERLYINT, 0x05);
	rtw_write8(rtwdev, REG_BCNDMATIM, WLAN_BCN_DMA_TIME);
	rtw_write16(rtwdev, REG_BCNTCFG, 0x4413);
}

static void rtw88xxa_phy_bb_config(struct rtw_dev *rtwdev)
{
	u8 val8, crystal_cap;

	/* power on BB/RF domain */
	val8 = rtw_read8(rtwdev, REG_SYS_FUNC_EN);
	val8 |= BIT_FEN_USBA;
	rtw_write8(rtwdev, REG_SYS_FUNC_EN, val8);

	/* toggle BB reset */
	val8 |= BIT_FEN_BB_RSTB | BIT_FEN_BB_GLB_RST;
	rtw_write8(rtwdev, REG_SYS_FUNC_EN, val8);

	rtw_write8(rtwdev, REG_RF_CTRL,
		   BIT_RF_EN | BIT_RF_RSTB | BIT_RF_SDM_RSTB);
	rtw_write8(rtwdev, REG_RF_B_CTRL,
		   BIT_RF_EN | BIT_RF_RSTB | BIT_RF_SDM_RSTB);

	rtw_load_table(rtwdev, rtwdev->chip->bb_tbl);
	rtw_load_table(rtwdev, rtwdev->chip->agc_tbl);

	crystal_cap = rtwdev->efuse.crystal_cap & 0x3F;
	if (rtwdev->chip->id == RTW_CHIP_TYPE_8812A)
		rtw_write32_mask(rtwdev, REG_AFE_CTRL3, 0x7FF80000,
				 crystal_cap | (crystal_cap << 6));
	else
		rtw_write32_mask(rtwdev, REG_AFE_CTRL3, 0x00FFF000,
				 crystal_cap | (crystal_cap << 6));
}

static void rtw88xxa_phy_rf_config(struct rtw_dev *rtwdev)
{
	u8 rf_path;

	for (rf_path = 0; rf_path < rtwdev->hal.rf_path_num; rf_path++)
		rtw_load_table(rtwdev, rtwdev->chip->rf_tbl[rf_path]);
}

static void rtw8812a_config_1t(struct rtw_dev *rtwdev)
{
	/* BB OFDM RX Path_A */
	rtw_write32_mask(rtwdev, REG_RXPSEL, 0xff, 0x11);

	/* BB OFDM TX Path_A */
	rtw_write32_mask(rtwdev, REG_TXPSEL, MASKLWORD, 0x1111);

	/* BB CCK R/Rx Path_A */
	rtw_write32_mask(rtwdev, REG_CCK_RX, 0x0c000000, 0x0);

	/* MCS support */
	rtw_write32_mask(rtwdev, REG_RX_MCS_LIMIT, 0xc0000060, 0x4);

	/* RF Path_B HSSI OFF */
	rtw_write32_mask(rtwdev, REG_3WIRE_SWB, 0xf, 0x4);

	/* RF Path_B Power Down */
	rtw_write32_mask(rtwdev, REG_LSSI_WRITE_B, MASKDWORD, 0);

	/* ADDA Path_B OFF */
	rtw_write32_mask(rtwdev, REG_AFE_PWR1_B, MASKDWORD, 0);
	rtw_write32_mask(rtwdev, REG_AFE_PWR2_B, MASKDWORD, 0);
}

static const u32 rtw88xxa_txscale_tbl[] = {
	0x081, 0x088, 0x090, 0x099, 0x0a2, 0x0ac, 0x0b6, 0x0c0, 0x0cc, 0x0d8,
	0x0e5, 0x0f2, 0x101, 0x110, 0x120, 0x131, 0x143, 0x156, 0x16a, 0x180,
	0x197, 0x1af, 0x1c8, 0x1e3, 0x200, 0x21e, 0x23e, 0x261, 0x285, 0x2ab,
	0x2d3, 0x2fe, 0x32b, 0x35c, 0x38e, 0x3c4, 0x3fe
};

static u32 rtw88xxa_get_bb_swing(struct rtw_dev *rtwdev, u8 band, u8 path)
{
	static const u32 swing2setting[4] = {0x200, 0x16a, 0x101, 0x0b6};
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u8 tx_bb_swing;

	if (band == RTW_BAND_2G)
		tx_bb_swing = efuse->tx_bb_swing_setting_2g;
	else
		tx_bb_swing = efuse->tx_bb_swing_setting_5g;

	if (path == RF_PATH_B)
		tx_bb_swing >>= 2;
	tx_bb_swing &= 0x3;

	return swing2setting[tx_bb_swing];
}

static u8 rtw88xxa_get_swing_index(struct rtw_dev *rtwdev)
{
	u32 swing, table_value;
	u8 i;

	swing = rtw88xxa_get_bb_swing(rtwdev, rtwdev->hal.current_band_type,
				      RF_PATH_A);

	for (i = 0; i < ARRAY_SIZE(rtw88xxa_txscale_tbl); i++) {
		table_value = rtw88xxa_txscale_tbl[i];
		if (swing == table_value)
			return i;
	}

	return 24;
}

static void rtw88xxa_pwrtrack_init(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 path;

	dm_info->default_ofdm_index = rtw88xxa_get_swing_index(rtwdev);

	if (rtwdev->chip->id == RTW_CHIP_TYPE_8821A)
		dm_info->default_cck_index = 0;
	else
		dm_info->default_cck_index = 24;

	for (path = RF_PATH_A; path < rtwdev->hal.rf_path_num; path++) {
		ewma_thermal_init(&dm_info->avg_thermal[path]);
		dm_info->delta_power_index[path] = 0;
		dm_info->delta_power_index_last[path] = 0;
	}

	dm_info->pwr_trk_triggered = false;
	dm_info->pwr_trk_init_trigger = true;
	dm_info->thermal_meter_k = rtwdev->efuse.thermal_meter_k;
}

void rtw88xxa_power_off(struct rtw_dev *rtwdev,
			const struct rtw_pwr_seq_cmd *const *enter_lps_flow)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);
	enum usb_device_speed speed = rtwusb->udev->speed;
	u16 ori_fsmc0;
	u8 reg_cr;

	reg_cr = rtw_read8(rtwdev, REG_CR);

	/* Already powered off */
	if (reg_cr == 0 || reg_cr == 0xEA)
		return;

	rtw_hci_stop(rtwdev);

	if (!rtwdev->efuse.btcoex)
		rtw_write16_clr(rtwdev, REG_GPIO_MUXCFG, BIT_EN_SIC);

	/* set Reg 0xf008[3:4] to 2'11 to enable U1/U2 Mode in USB3.0. */
	if (speed == USB_SPEED_SUPER)
		rtw_write8_set(rtwdev, REG_USB_MOD, 0x18);

	rtw_write32(rtwdev, REG_HISR0, 0xffffffff);
	rtw_write32(rtwdev, REG_HISR1, 0xffffffff);
	rtw_write32(rtwdev, REG_HIMR0, 0);
	rtw_write32(rtwdev, REG_HIMR1, 0);

	if (rtwdev->efuse.btcoex)
		rtw_coex_power_off_setting(rtwdev);

	ori_fsmc0 = rtw_read16(rtwdev, REG_APS_FSMCO);
	rtw_write16(rtwdev, REG_APS_FSMCO, ori_fsmc0 & ~APS_FSMCO_HW_POWERDOWN);

	/* Stop Tx Report Timer. */
	rtw_write8_clr(rtwdev, REG_TX_RPT_CTRL, BIT(1));

	/* Stop Rx */
	rtw_write8(rtwdev, REG_CR, 0);

	rtw_pwr_seq_parser(rtwdev, enter_lps_flow);

	if (rtw_read8(rtwdev, REG_MCUFW_CTRL) & BIT_RAM_DL_SEL)
		rtw88xxa_reset_8051(rtwdev);

	rtw_write8_clr(rtwdev, REG_SYS_FUNC_EN + 1, BIT(2));
	rtw_write8(rtwdev, REG_MCUFW_CTRL, 0);

	rtw_pwr_seq_parser(rtwdev, rtwdev->chip->pwr_off_seq);

	if (ori_fsmc0 & APS_FSMCO_HW_POWERDOWN)
		rtw_write16_set(rtwdev, REG_APS_FSMCO, APS_FSMCO_HW_POWERDOWN);

	clear_bit(RTW_FLAG_POWERON, rtwdev->flags);
}
EXPORT_SYMBOL(rtw88xxa_power_off);

static void rtw88xxa_set_channel_bb_swing(struct rtw_dev *rtwdev, u8 band)
{
	rtw_write32_mask(rtwdev, REG_TXSCALE_A, BB_SWING_MASK,
			 rtw88xxa_get_bb_swing(rtwdev, band, RF_PATH_A));
	rtw_write32_mask(rtwdev, REG_TXSCALE_B, BB_SWING_MASK,
			 rtw88xxa_get_bb_swing(rtwdev, band, RF_PATH_B));
	rtw88xxa_pwrtrack_init(rtwdev);
}

static void rtw8821a_set_ext_band_switch(struct rtw_dev *rtwdev, u8 band)
{
	rtw_write32_mask(rtwdev, REG_LED_CFG, BIT_DPDT_SEL_EN, 0);
	rtw_write32_mask(rtwdev, REG_LED_CFG, BIT_DPDT_WL_SEL, 1);
	rtw_write32_mask(rtwdev, REG_RFE_INV_A, 0xf, 7);
	rtw_write32_mask(rtwdev, REG_RFE_INV_A, 0xf0, 7);

	if (band == RTW_BAND_2G)
		rtw_write32_mask(rtwdev, REG_RFE_INV_A, BIT(29) | BIT(28), 1);
	else
		rtw_write32_mask(rtwdev, REG_RFE_INV_A, BIT(29) | BIT(28), 2);
}

static void rtw8821a_phy_set_rfe_reg_24g(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;

	/* Turn off RF PA and LNA */

	/* 0xCB0[15:12] = 0x7 (LNA_On)*/
	rtw_write32_mask(rtwdev, REG_RFE_PINMUX_A, 0xF000, 0x7);
	/* 0xCB0[7:4] = 0x7 (PAPE_A)*/
	rtw_write32_mask(rtwdev, REG_RFE_PINMUX_A, 0xF0, 0x7);

	if (efuse->ext_lna_2g) {
		/* Turn on 2.4G External LNA */
		rtw_write32_mask(rtwdev, REG_RFE_INV_A, BIT(20), 1);
		rtw_write32_mask(rtwdev, REG_RFE_INV_A, BIT(22), 0);
		rtw_write32_mask(rtwdev, REG_RFE_PINMUX_A, GENMASK(2, 0), 0x2);
		rtw_write32_mask(rtwdev, REG_RFE_PINMUX_A, GENMASK(10, 8), 0x2);
	} else {
		/* Bypass 2.4G External LNA */
		rtw_write32_mask(rtwdev, REG_RFE_INV_A, BIT(20), 0);
		rtw_write32_mask(rtwdev, REG_RFE_INV_A, BIT(22), 0);
		rtw_write32_mask(rtwdev, REG_RFE_PINMUX_A, GENMASK(2, 0), 0x7);
		rtw_write32_mask(rtwdev, REG_RFE_PINMUX_A, GENMASK(10, 8), 0x7);
	}
}

static void rtw8821a_phy_set_rfe_reg_5g(struct rtw_dev *rtwdev)
{
	/* Turn ON RF PA and LNA */

	/* 0xCB0[15:12] = 0x7 (LNA_On)*/
	rtw_write32_mask(rtwdev, REG_RFE_PINMUX_A, 0xF000, 0x5);
	/* 0xCB0[7:4] = 0x7 (PAPE_A)*/
	rtw_write32_mask(rtwdev, REG_RFE_PINMUX_A, 0xF0, 0x4);

	/* Bypass 2.4G External LNA */
	rtw_write32_mask(rtwdev, REG_RFE_INV_A, BIT(20), 0);
	rtw_write32_mask(rtwdev, REG_RFE_INV_A, BIT(22), 0);
	rtw_write32_mask(rtwdev, REG_RFE_PINMUX_A, GENMASK(2, 0), 0x7);
	rtw_write32_mask(rtwdev, REG_RFE_PINMUX_A, GENMASK(10, 8), 0x7);
}

static void rtw8812a_phy_set_rfe_reg_24g(struct rtw_dev *rtwdev)
{
	switch (rtwdev->efuse.rfe_option) {
	case 0:
	case 2:
		rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x77777777);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x77777777);
		rtw_write32_mask(rtwdev, REG_RFE_INV_A, RFE_INV_MASK, 0x000);
		rtw_write32_mask(rtwdev, REG_RFE_INV_B, RFE_INV_MASK, 0x000);
		break;
	case 1:
		if (rtwdev->efuse.btcoex) {
			rtw_write32_mask(rtwdev, REG_RFE_PINMUX_A, 0xffffff, 0x777777);
			rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x77777777);
			rtw_write32_mask(rtwdev, REG_RFE_INV_A, 0x33f00000, 0x000);
			rtw_write32_mask(rtwdev, REG_RFE_INV_B, RFE_INV_MASK, 0x000);
		} else {
			rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x77777777);
			rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x77777777);
			rtw_write32_mask(rtwdev, REG_RFE_INV_A, RFE_INV_MASK, 0x000);
			rtw_write32_mask(rtwdev, REG_RFE_INV_B, RFE_INV_MASK, 0x000);
		}
		break;
	case 3:
		rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x54337770);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x54337770);
		rtw_write32_mask(rtwdev, REG_RFE_INV_A, RFE_INV_MASK, 0x010);
		rtw_write32_mask(rtwdev, REG_RFE_INV_B, RFE_INV_MASK, 0x010);
		rtw_write32_mask(rtwdev, REG_ANTSEL_SW, 0x00000303, 0x1);
		break;
	case 4:
		rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x77777777);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x77777777);
		rtw_write32_mask(rtwdev, REG_RFE_INV_A, RFE_INV_MASK, 0x001);
		rtw_write32_mask(rtwdev, REG_RFE_INV_B, RFE_INV_MASK, 0x001);
		break;
	case 5:
		rtw_write8(rtwdev, REG_RFE_PINMUX_A + 2, 0x77);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x77777777);
		rtw_write8_clr(rtwdev, REG_RFE_INV_A + 3, BIT(0));
		rtw_write32_mask(rtwdev, REG_RFE_INV_B, RFE_INV_MASK, 0x000);
		break;
	case 6:
		rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x07772770);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x07772770);
		rtw_write32(rtwdev, REG_RFE_INV_A, 0x00000077);
		rtw_write32(rtwdev, REG_RFE_INV_B, 0x00000077);
		break;
	default:
		break;
	}
}

static void rtw8812a_phy_set_rfe_reg_5g(struct rtw_dev *rtwdev)
{
	switch (rtwdev->efuse.rfe_option) {
	case 0:
		rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x77337717);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x77337717);
		rtw_write32_mask(rtwdev, REG_RFE_INV_A, RFE_INV_MASK, 0x010);
		rtw_write32_mask(rtwdev, REG_RFE_INV_B, RFE_INV_MASK, 0x010);
		break;
	case 1:
		if (rtwdev->efuse.btcoex) {
			rtw_write32_mask(rtwdev, REG_RFE_PINMUX_A, 0xffffff, 0x337717);
			rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x77337717);
			rtw_write32_mask(rtwdev, REG_RFE_INV_A, 0x33f00000, 0x000);
			rtw_write32_mask(rtwdev, REG_RFE_INV_B, RFE_INV_MASK, 0x000);
		} else {
			rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x77337717);
			rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x77337717);
			rtw_write32_mask(rtwdev, REG_RFE_INV_A, RFE_INV_MASK, 0x000);
			rtw_write32_mask(rtwdev, REG_RFE_INV_B, RFE_INV_MASK, 0x000);
		}
		break;
	case 2:
	case 4:
		rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x77337777);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x77337777);
		rtw_write32_mask(rtwdev, REG_RFE_INV_A, RFE_INV_MASK, 0x010);
		rtw_write32_mask(rtwdev, REG_RFE_INV_B, RFE_INV_MASK, 0x010);
		break;
	case 3:
		rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x54337717);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x54337717);
		rtw_write32_mask(rtwdev, REG_RFE_INV_A, RFE_INV_MASK, 0x010);
		rtw_write32_mask(rtwdev, REG_RFE_INV_B, RFE_INV_MASK, 0x010);
		rtw_write32_mask(rtwdev, REG_ANTSEL_SW, 0x00000303, 0x1);
		break;
	case 5:
		rtw_write8(rtwdev, REG_RFE_PINMUX_A + 2, 0x33);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x77337777);
		rtw_write8_set(rtwdev, REG_RFE_INV_A + 3, BIT(0));
		rtw_write32_mask(rtwdev, REG_RFE_INV_B, RFE_INV_MASK, 0x010);
		break;
	case 6:
		rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x07737717);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x07737717);
		rtw_write32(rtwdev, REG_RFE_INV_A, 0x00000077);
		rtw_write32(rtwdev, REG_RFE_INV_B, 0x00000077);
		break;
	default:
		break;
	}
}

static void rtw88xxa_switch_band(struct rtw_dev *rtwdev, u8 new_band, u8 bw)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	u16 basic_rates, reg_41a;

	/* 8811au one antenna module doesn't support antenna div, so driver must
	 * control antenna band, otherwise one of the band will have issue
	 */
	if (chip->id == RTW_CHIP_TYPE_8821A && !rtwdev->efuse.btcoex &&
	    rtwdev->efuse.ant_div_cfg == 0)
		rtw8821a_set_ext_band_switch(rtwdev, new_band);

	if (new_band == RTW_BAND_2G) {
		rtw_write32_set(rtwdev, REG_RXPSEL, BIT_RX_PSEL_RST);

		if (chip->id == RTW_CHIP_TYPE_8821A) {
			rtw8821a_phy_set_rfe_reg_24g(rtwdev);

			rtw_write32_mask(rtwdev, REG_TXSCALE_A, 0xf00, 0);
		} else {
			rtw_write32_mask(rtwdev, REG_BWINDICATION, 0x3, 0x1);
			rtw_write32_mask(rtwdev, REG_PDMFTH, GENMASK(17, 13), 0x17);

			if (bw == RTW_CHANNEL_WIDTH_20 &&
			    rtwdev->hal.rf_type == RF_1T1R &&
			    !rtwdev->efuse.ext_lna_2g)
				rtw_write32_mask(rtwdev, REG_PDMFTH, GENMASK(3, 1), 0x02);
			else
				rtw_write32_mask(rtwdev, REG_PDMFTH, GENMASK(3, 1), 0x04);

			rtw_write32_mask(rtwdev, REG_CCASEL, 0x3, 0);

			rtw8812a_phy_set_rfe_reg_24g(rtwdev);
		}

		rtw_write32_mask(rtwdev, REG_TXPSEL, 0xf0, 0x1);
		rtw_write32_mask(rtwdev, REG_CCK_RX, 0x0f000000, 0x1);

		basic_rates = BIT(DESC_RATE1M) | BIT(DESC_RATE2M) |
			      BIT(DESC_RATE5_5M) | BIT(DESC_RATE11M) |
			      BIT(DESC_RATE6M) | BIT(DESC_RATE12M) |
			      BIT(DESC_RATE24M);
		rtw_write32_mask(rtwdev, REG_RRSR, 0xfffff, basic_rates);

		rtw_write8_clr(rtwdev, REG_CCK_CHECK, BIT_CHECK_CCK_EN);
	} else { /* RTW_BAND_5G */
		if (chip->id == RTW_CHIP_TYPE_8821A)
			rtw8821a_phy_set_rfe_reg_5g(rtwdev);

		rtw_write8_set(rtwdev, REG_CCK_CHECK, BIT_CHECK_CCK_EN);

		read_poll_timeout_atomic(rtw_read16, reg_41a, (reg_41a & 0x30) == 0x30,
					 50, 2500, false, rtwdev, REG_TXPKT_EMPTY);

		rtw_write32_set(rtwdev, REG_RXPSEL, BIT_RX_PSEL_RST);

		if (chip->id == RTW_CHIP_TYPE_8821A) {
			rtw_write32_mask(rtwdev, REG_TXSCALE_A, 0xf00, 1);
		} else {
			rtw_write32_mask(rtwdev, REG_BWINDICATION, 0x3, 0x2);
			rtw_write32_mask(rtwdev, REG_PDMFTH, GENMASK(17, 13), 0x15);
			rtw_write32_mask(rtwdev, REG_PDMFTH, GENMASK(3, 1), 0x04);

			rtw_write32_mask(rtwdev, REG_CCASEL, 0x3, 1);

			rtw8812a_phy_set_rfe_reg_5g(rtwdev);
		}

		rtw_write32_mask(rtwdev, REG_TXPSEL, 0xf0, 0);
		rtw_write32_mask(rtwdev, REG_CCK_RX, 0x0f000000, 0xf);

		basic_rates = BIT(DESC_RATE6M) | BIT(DESC_RATE12M) |
			      BIT(DESC_RATE24M);
		rtw_write32_mask(rtwdev, REG_RRSR, 0xfffff, basic_rates);
	}

	rtw88xxa_set_channel_bb_swing(rtwdev, new_band);
}

int rtw88xxa_power_on(struct rtw_dev *rtwdev)
{
	struct rtw_usb *rtwusb = rtw_get_usb_priv(rtwdev);
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_hal *hal = &rtwdev->hal;
	int ret;

	if (test_bit(RTW_FLAG_POWERON, rtwdev->flags))
		return 0;

	/* Override rtw_chip_efuse_info_setup() */
	if (chip->id == RTW_CHIP_TYPE_8821A)
		efuse->btcoex = rtw_read32_mask(rtwdev, REG_WL_BT_PWR_CTRL,
						BIT_BT_FUNC_EN);

	/* Override rtw_chip_efuse_info_setup() */
	if (chip->id == RTW_CHIP_TYPE_8812A)
		rtw8812a_read_amplifier_type(rtwdev);

	ret = rtw_hci_setup(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to setup hci\n");
		goto err;
	}

	/* Revise for U2/U3 switch we can not update RF-A/B reset.
	 * Reset after MAC power on to prevent RF R/W error.
	 * Is it a right method?
	 */
	if (chip->id == RTW_CHIP_TYPE_8812A) {
		rtw_write8(rtwdev, REG_RF_CTRL, 5);
		rtw_write8(rtwdev, REG_RF_CTRL, 7);
		rtw_write8(rtwdev, REG_RF_B_CTRL, 5);
		rtw_write8(rtwdev, REG_RF_B_CTRL, 7);
	}

	/* If HW didn't go through a complete de-initial procedure,
	 * it probably occurs some problem for double initial
	 * procedure.
	 */
	rtw88xxau_hw_reset(rtwdev);

	ret = rtw88xxau_init_power_on(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to power on\n");
		goto err;
	}

	ret = rtw_set_trx_fifo_info(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to set trx fifo info\n");
		goto err;
	}

	ret = rtw88xxa_llt_init(rtwdev, rtwdev->fifo.rsvd_boundary);
	if (ret) {
		rtw_err(rtwdev, "failed to init llt\n");
		goto err;
	}

	rtw_write32_set(rtwdev, REG_TXDMA_OFFSET_CHK, BIT_DROP_DATA_EN);

	ret = rtw_wait_firmware_completion(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to wait firmware completion\n");
		goto err_off;
	}

	ret = rtw_download_firmware(rtwdev, &rtwdev->fw);
	if (ret) {
		rtw_err(rtwdev, "failed to download firmware\n");
		goto err_off;
	}

	rtw_write8(rtwdev, REG_HMETFR, 0xf);

	rtw_load_table(rtwdev, chip->mac_tbl);

	rtw88xxau_init_queue_reserved_page(rtwdev);
	rtw88xxau_init_tx_buffer_boundary(rtwdev);
	rtw88xxau_init_queue_priority(rtwdev);

	rtw_write16(rtwdev, REG_TRXFF_BNDY + 2,
		    chip->rxff_size - REPORT_BUF - 1);

	if (chip->id == RTW_CHIP_TYPE_8812A)
		rtw_write8(rtwdev, REG_PBP,
			   u8_encode_bits(PBP_512, PBP_TX_MASK) |
			   u8_encode_bits(PBP_64, PBP_RX_MASK));

	rtw_write8(rtwdev, REG_RX_DRVINFO_SZ, PHY_STATUS_SIZE);

	rtw_write32(rtwdev, REG_HIMR0, 0);
	rtw_write32(rtwdev, REG_HIMR1, 0);

	rtw_write32_mask(rtwdev, REG_CR, 0x30000, 0x2);

	rtw88xxa_init_wmac_setting(rtwdev);
	rtw88xxa_init_adaptive_ctrl(rtwdev);
	rtw88xxa_init_edca(rtwdev);

	rtw_write8_set(rtwdev, REG_FWHW_TXQ_CTRL, BIT(7));
	rtw_write8(rtwdev, REG_ACKTO, 0x80);

	rtw88xxau_tx_aggregation(rtwdev);

	rtw88xxa_init_beacon_parameters(rtwdev);
	rtw_write8(rtwdev, REG_BCN_MAX_ERR, 0xff);

	rtw_hci_interface_cfg(rtwdev);

	/* usb3 rx interval */
	rtw_write8(rtwdev, REG_USB3_RXITV, 0x01);

	/* burst length=4, set 0x3400 for burst length=2 */
	rtw_write16(rtwdev, REG_RXDMA_STATUS, 0x7400);
	rtw_write8(rtwdev, REG_RXDMA_STATUS + 1, 0xf5);

	/* 0x456 = 0x70, sugguested by Zhilin */
	if (chip->id == RTW_CHIP_TYPE_8821A)
		rtw_write8(rtwdev, REG_AMPDU_MAX_TIME, 0x5e);
	else
		rtw_write8(rtwdev, REG_AMPDU_MAX_TIME, 0x70);

	rtw_write32(rtwdev, REG_AMPDU_MAX_LENGTH, 0xffffffff);
	rtw_write8(rtwdev, REG_USTIME_TSF, 0x50);
	rtw_write8(rtwdev, REG_USTIME_EDCA, 0x50);

	if (rtwusb->udev->speed == USB_SPEED_SUPER)
		/* Disable U1/U2 Mode to avoid 2.5G spur in USB3.0. */
		rtw_write8_clr(rtwdev, REG_USB_MOD, BIT(4) | BIT(3));

	rtw_write8_set(rtwdev, REG_SINGLE_AMPDU_CTRL, BIT_EN_SINGLE_APMDU);

	/* for VHT packet length 11K */
	rtw_write8(rtwdev, REG_RX_PKT_LIMIT, 0x18);

	rtw_write8(rtwdev, REG_PIFS, 0x00);

	if (chip->id == RTW_CHIP_TYPE_8821A) {
		/* 0x0a0a too small, it can't pass AC logo. change to 0x1f1f */
		rtw_write16(rtwdev, REG_MAX_AGGR_NUM, 0x1f1f);
		rtw_write8(rtwdev, REG_FWHW_TXQ_CTRL, 0x80);
		rtw_write32(rtwdev, REG_FAST_EDCA_CTRL, 0x03087777);
	} else {
		rtw_write16(rtwdev, REG_MAX_AGGR_NUM, 0x1f1f);
		rtw_write8_clr(rtwdev, REG_FWHW_TXQ_CTRL, BIT(7));
	}

	 /* to prevent mac is reseted by bus. */
	rtw_write8_set(rtwdev, REG_RSV_CTRL, BIT(5) | BIT(6));

	/* ARFB table 9 for 11ac 5G 2SS */
	rtw_write32(rtwdev, REG_ARFR0, 0x00000010);
	rtw_write32(rtwdev, REG_ARFRH0, 0xfffff000);

	/* ARFB table 10 for 11ac 5G 1SS */
	rtw_write32(rtwdev, REG_ARFR1_V1, 0x00000010);
	rtw_write32(rtwdev, REG_ARFRH1_V1, 0x003ff000);

	/* ARFB table 11 for 11ac 24G 1SS */
	rtw_write32(rtwdev, REG_ARFR2_V1, 0x00000015);
	rtw_write32(rtwdev, REG_ARFRH2_V1, 0x003ff000);

	/* ARFB table 12 for 11ac 24G 2SS */
	rtw_write32(rtwdev, REG_ARFR3_V1, 0x00000015);
	rtw_write32(rtwdev, REG_ARFRH3_V1, 0xffcff000);

	rtw_write8_set(rtwdev, REG_CR, BIT_MACTXEN | BIT_MACRXEN);

	rtw88xxa_phy_bb_config(rtwdev);
	rtw88xxa_phy_rf_config(rtwdev);

	if (chip->id == RTW_CHIP_TYPE_8812A && hal->rf_path_num == 1)
		rtw8812a_config_1t(rtwdev);

	rtw88xxa_switch_band(rtwdev, RTW_BAND_2G, RTW_CHANNEL_WIDTH_20);

	rtw_write32(rtwdev, RTW_SEC_CMD_REG, BIT(31) | BIT(30));

	rtw_write8(rtwdev, REG_HWSEQ_CTRL, 0xff);
	rtw_write32(rtwdev, REG_BAR_MODE_CTRL, 0x0201ffff);
	rtw_write8(rtwdev, REG_NAV_CTRL + 2, 0);

	rtw_write8_clr(rtwdev, REG_GPIO_MUXCFG, BIT(5));

	rtw_phy_init(rtwdev);

	rtw88xxa_pwrtrack_init(rtwdev);

	/* 0x4c6[3] 1: RTS BW = Data BW
	 * 0: RTS BW depends on CCA / secondary CCA result.
	 */
	rtw_write8_clr(rtwdev, REG_QUEUE_CTRL, BIT(3));

	/* enable Tx report. */
	rtw_write8(rtwdev, REG_FWHW_TXQ_CTRL + 1, 0x0f);

	/* Pretx_en, for WEP/TKIP SEC */
	rtw_write8(rtwdev, REG_EARLY_MODE_CONTROL + 3, 0x01);

	rtw_write16(rtwdev, REG_TX_RPT_TIME, 0x3df0);

	/* Reset USB mode switch setting */
	rtw_write8(rtwdev, REG_SYS_SDIO_CTRL, 0x0);
	rtw_write8(rtwdev, REG_ACLK_MON, 0x0);

	rtw_write8(rtwdev, REG_USB_HRPWM, 0);

	/* ack for xmit mgmt frames. */
	rtw_write32_set(rtwdev, REG_FWHW_TXQ_CTRL, BIT(12));

	hal->cck_high_power = rtw_read32_mask(rtwdev, REG_CCK_RPT_FORMAT,
					      BIT_CCK_RPT_FORMAT);

	ret = rtw_hci_start(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to start hci\n");
		goto err_off;
	}

	if (efuse->btcoex) {
		rtw_coex_power_on_setting(rtwdev);
		rtw_coex_init_hw_config(rtwdev, false);
	}

	set_bit(RTW_FLAG_POWERON, rtwdev->flags);

	return 0;

err_off:
	chip->ops->power_off(rtwdev);

err:
	return ret;
}
EXPORT_SYMBOL(rtw88xxa_power_on);

u32 rtw88xxa_phy_read_rf(struct rtw_dev *rtwdev,
			 enum rtw_rf_path rf_path, u32 addr, u32 mask)
{
	static const u32 pi_addr[2] = { REG_3WIRE_SWA, REG_3WIRE_SWB };
	static const u32 read_addr[2][2] = {
		{ REG_SI_READ_A, REG_SI_READ_B },
		{ REG_PI_READ_A, REG_PI_READ_B }
	};
	const struct rtw_chip_info *chip = rtwdev->chip;
	const struct rtw_hal *hal = &rtwdev->hal;
	bool set_cca, pi_mode;
	u32 val;

	if (rf_path >= hal->rf_phy_num) {
		rtw_err(rtwdev, "unsupported rf path (%d)\n", rf_path);
		return INV_RF_DATA;
	}

	/* CCA off to avoid reading the wrong value.
	 * Toggling CCA would affect RF 0x0, skip it.
	 */
	set_cca = addr != 0x0 && chip->id == RTW_CHIP_TYPE_8812A &&
		  hal->cut_version != RTW_CHIP_VER_CUT_C;

	if (set_cca)
		rtw_write32_set(rtwdev, REG_CCA2ND, BIT(3));

	addr &= 0xff;

	pi_mode = rtw_read32_mask(rtwdev, pi_addr[rf_path], 0x4);

	rtw_write32_mask(rtwdev, REG_HSSI_READ, MASKBYTE0, addr);

	if (chip->id == RTW_CHIP_TYPE_8821A ||
	    hal->cut_version == RTW_CHIP_VER_CUT_C)
		udelay(20);

	val = rtw_read32_mask(rtwdev, read_addr[pi_mode][rf_path], mask);

	/* CCA on */
	if (set_cca)
		rtw_write32_clr(rtwdev, REG_CCA2ND, BIT(3));

	return val;
}
EXPORT_SYMBOL(rtw88xxa_phy_read_rf);

static void rtw8812a_phy_fix_spur(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
	/* C cut Item12 ADC FIFO CLOCK */
	if (rtwdev->hal.cut_version == RTW_CHIP_VER_CUT_C) {
		if (bw == RTW_CHANNEL_WIDTH_40 && channel == 11)
			rtw_write32_mask(rtwdev, REG_ADCCLK, 0xC00, 0x3);
		else
			rtw_write32_mask(rtwdev, REG_ADCCLK, 0xC00, 0x2);

		/* A workaround to resolve 2480Mhz spur by setting ADC clock
		 * as 160M.
		 */
		if (bw == RTW_CHANNEL_WIDTH_20 && (channel == 13 || channel == 14)) {
			rtw_write32_mask(rtwdev, REG_ADCCLK, 0x300, 0x3);
			rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 1);
		} else if (bw == RTW_CHANNEL_WIDTH_40 && channel == 11) {
			rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 1);
		} else if (bw != RTW_CHANNEL_WIDTH_80) {
			rtw_write32_mask(rtwdev, REG_ADCCLK, 0x300, 0x2);
			rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0);
		}
	} else {
		/* A workaround to resolve 2480Mhz spur by setting ADC clock
		 * as 160M.
		 */
		if (bw == RTW_CHANNEL_WIDTH_20 && (channel == 13 || channel == 14))
			rtw_write32_mask(rtwdev, REG_ADCCLK, 0x300, 0x3);
		else if (channel <= 14) /* 2.4G only */
			rtw_write32_mask(rtwdev, REG_ADCCLK, 0x300, 0x2);
	}
}

static void rtw88xxa_switch_channel(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u32 fc_area, rf_mod_ag;
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

		rtw_write_rf(rtwdev, path, RF_CFGCH,
			     RF18_RFSI_MASK | RF18_BAND_MASK, rf_mod_ag);

		if (rtwdev->chip->id == RTW_CHIP_TYPE_8812A)
			rtw8812a_phy_fix_spur(rtwdev, channel, bw);

		rtw_write_rf(rtwdev, path, RF_CFGCH, RF18_CHANNEL_MASK, channel);
	}
}

static void rtw88xxa_set_reg_bw(struct rtw_dev *rtwdev, u8 bw)
{
	u16 val16 = rtw_read16(rtwdev, REG_WMAC_TRXPTCL_CTL);

	val16 &= ~BIT_RFMOD;
	if (bw == RTW_CHANNEL_WIDTH_80)
		val16 |= BIT_RFMOD_80M;
	else if (bw == RTW_CHANNEL_WIDTH_40)
		val16 |= BIT_RFMOD_40M;

	rtw_write16(rtwdev, REG_WMAC_TRXPTCL_CTL, val16);
}

static void rtw88xxa_post_set_bw_mode(struct rtw_dev *rtwdev, u8 channel,
				      u8 bw, u8 primary_chan_idx)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u8 txsc40 = 0, txsc20, txsc;
	u8 reg_837, l1pkval;

	rtw88xxa_set_reg_bw(rtwdev, bw);

	txsc20 = primary_chan_idx;
	if (bw == RTW_CHANNEL_WIDTH_80) {
		if (txsc20 == RTW_SC_20_UPPER || txsc20 == RTW_SC_20_UPMOST)
			txsc40 = RTW_SC_40_UPPER;
		else
			txsc40 = RTW_SC_40_LOWER;
	}

	txsc = BIT_TXSC_20M(txsc20) | BIT_TXSC_40M(txsc40);
	rtw_write8(rtwdev, REG_DATA_SC, txsc);

	reg_837 = rtw_read8(rtwdev, REG_BWINDICATION + 3);

	switch (bw) {
	default:
	case RTW_CHANNEL_WIDTH_20:
		rtw_write32_mask(rtwdev, REG_ADCCLK, 0x003003C3, 0x00300200);
		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0);

		if (hal->rf_type == RF_2T2R)
			rtw_write32_mask(rtwdev, REG_L1PKTH, 0x03C00000, 7);
		else
			rtw_write32_mask(rtwdev, REG_L1PKTH, 0x03C00000, 8);

		break;
	case RTW_CHANNEL_WIDTH_40:
		rtw_write32_mask(rtwdev, REG_ADCCLK, 0x003003C3, 0x00300201);
		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0);
		rtw_write32_mask(rtwdev, REG_ADCCLK, 0x3C, txsc);
		rtw_write32_mask(rtwdev, REG_CCA2ND, 0xf0000000, txsc);

		if (reg_837 & BIT(2)) {
			l1pkval = 6;
		} else {
			if (hal->rf_type == RF_2T2R)
				l1pkval = 7;
			else
				l1pkval = 8;
		}

		rtw_write32_mask(rtwdev, REG_L1PKTH, 0x03C00000, l1pkval);

		if (txsc == RTW_SC_20_UPPER)
			rtw_write32_set(rtwdev, REG_RXSB, BIT(4));
		else
			rtw_write32_clr(rtwdev, REG_RXSB, BIT(4));

		break;
	case RTW_CHANNEL_WIDTH_80:
		rtw_write32_mask(rtwdev, REG_ADCCLK, 0x003003C3, 0x00300202);
		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 1);
		rtw_write32_mask(rtwdev, REG_ADCCLK, 0x3C, txsc);
		rtw_write32_mask(rtwdev, REG_CCA2ND, 0xf0000000, txsc);

		if (reg_837 & BIT(2)) {
			l1pkval = 5;
		} else {
			if (hal->rf_type == RF_2T2R)
				l1pkval = 6;
			else
				l1pkval = 7;
		}

		rtw_write32_mask(rtwdev, REG_L1PKTH, 0x03C00000, l1pkval);

		break;
	}
}

static void rtw88xxa_set_channel_rf(struct rtw_dev *rtwdev, u8 channel, u8 bw)
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

void rtw88xxa_set_channel(struct rtw_dev *rtwdev, u8 channel, u8 bw,
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
		rtw88xxa_switch_band(rtwdev, new_band, bw);

	rtw88xxa_switch_channel(rtwdev, channel, bw);

	rtw88xxa_post_set_bw_mode(rtwdev, channel, bw, primary_chan_idx);

	if (rtwdev->chip->id == RTW_CHIP_TYPE_8812A)
		rtw8812a_phy_fix_spur(rtwdev, channel, bw);

	rtw88xxa_set_channel_rf(rtwdev, channel, bw);
}
EXPORT_SYMBOL(rtw88xxa_set_channel);

void rtw88xxa_query_phy_status(struct rtw_dev *rtwdev, u8 *phy_status,
			       struct rtw_rx_pkt_stat *pkt_stat,
			       s8 (*cck_rx_pwr)(u8 lna_idx, u8 vga_idx))
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_jaguar_phy_status_rpt *rpt;
	u8 gain[RTW_RF_PATH_MAX], rssi, i;
	s8 rx_pwr_db, power_a, power_b;
	const s8 min_rx_power = -120;
	u8 lna_idx, vga_idx;

	rpt = (struct rtw_jaguar_phy_status_rpt *)phy_status;

	if (pkt_stat->rate <= DESC_RATE11M) {
		lna_idx = le32_get_bits(rpt->w1, RTW_JGRPHY_W1_AGC_RPT_LNA_IDX);
		vga_idx = le32_get_bits(rpt->w1, RTW_JGRPHY_W1_AGC_RPT_VGA_IDX);

		rx_pwr_db = cck_rx_pwr(lna_idx, vga_idx);

		pkt_stat->rx_power[RF_PATH_A] = rx_pwr_db;
		pkt_stat->rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power, 1);
		dm_info->rssi[RF_PATH_A] = pkt_stat->rssi;
		pkt_stat->bw = RTW_CHANNEL_WIDTH_20;
		pkt_stat->signal_power = rx_pwr_db;
	} else { /* OFDM rate */
		gain[RF_PATH_A] = le32_get_bits(rpt->w0, RTW_JGRPHY_W0_GAIN_A);
		gain[RF_PATH_B] = le32_get_bits(rpt->w0, RTW_JGRPHY_W0_GAIN_B);

		for (i = RF_PATH_A; i < rtwdev->hal.rf_path_num; i++) {
			pkt_stat->rx_power[i] = gain[i] - 110;
			rssi = rtw_phy_rf_power_2_rssi(&pkt_stat->rx_power[i], 1);
			dm_info->rssi[i] = rssi;
		}

		pkt_stat->rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power,
							 rtwdev->hal.rf_path_num);

		power_a = pkt_stat->rx_power[RF_PATH_A];
		power_b = pkt_stat->rx_power[RF_PATH_B];
		if (rtwdev->hal.rf_path_num == 1)
			power_b = power_a;

		pkt_stat->signal_power = max3(power_a, power_b, min_rx_power);
	}
}
EXPORT_SYMBOL(rtw88xxa_query_phy_status);

static void
rtw88xxa_set_tx_power_index_by_rate(struct rtw_dev *rtwdev, u8 path,
				    u8 rs, u32 *phy_pwr_idx)
{
	static const u32 offset_txagc[2] = {
		REG_TX_AGC_A_CCK_11_CCK_1, REG_TX_AGC_B_CCK_11_CCK_1
	};
	u8 rate, rate_idx, pwr_index, shift;
	struct rtw_hal *hal = &rtwdev->hal;
	bool write_1ss_mcs9;
	u32 mask;
	int j;

	for (j = 0; j < rtw_rate_size[rs]; j++) {
		rate = rtw_rate_section[rs][j];

		pwr_index = hal->tx_pwr_tbl[path][rate];

		shift = rate & 0x3;
		*phy_pwr_idx |= ((u32)pwr_index << (shift * 8));

		write_1ss_mcs9 = rate == DESC_RATEVHT1SS_MCS9 &&
				 hal->rf_path_num == 1;

		if (write_1ss_mcs9)
			mask = MASKLWORD;
		else
			mask = MASKDWORD;

		if (shift == 0x3 || write_1ss_mcs9) {
			rate_idx = rate & 0xfc;
			if (rate >= DESC_RATEVHT1SS_MCS0)
				rate_idx -= 0x10;

			rtw_write32_mask(rtwdev, offset_txagc[path] + rate_idx,
					 mask, *phy_pwr_idx);

			*phy_pwr_idx = 0;
		}
	}
}

static void rtw88xxa_tx_power_training(struct rtw_dev *rtwdev, u8 bw,
				       u8 channel, u8 path)
{
	static const u32 write_offset[] = {
		REG_TX_PWR_TRAINING_A, REG_TX_PWR_TRAINING_B,
	};
	u32 power_level, write_data;
	u8 i;

	power_level = rtwdev->hal.tx_pwr_tbl[path][DESC_RATEMCS7];
	write_data = 0;

	for (i = 0; i < 3; i++) {
		if (i == 0)
			power_level -= 10;
		else if (i == 1)
			power_level -= 8;
		else
			power_level -= 6;

		write_data |= max_t(u32, power_level, 2) << (i * 8);
	}

	rtw_write32_mask(rtwdev, write_offset[path], 0xffffff, write_data);
}

void rtw88xxa_set_tx_power_index(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u32 phy_pwr_idx = 0;
	int rs, path;

	for (path = 0; path < hal->rf_path_num; path++) {
		for (rs = 0; rs < RTW_RATE_SECTION_MAX; rs++) {
			if (hal->rf_path_num == 1 &&
			    (rs == RTW_RATE_SECTION_HT_2S ||
			     rs == RTW_RATE_SECTION_VHT_2S))
				continue;

			if (test_bit(RTW_FLAG_SCANNING, rtwdev->flags) &&
			    rs > RTW_RATE_SECTION_OFDM)
				continue;

			if (hal->current_band_type == RTW_BAND_5G &&
			    rs == RTW_RATE_SECTION_CCK)
				continue;

			rtw88xxa_set_tx_power_index_by_rate(rtwdev, path, rs,
							    &phy_pwr_idx);
		}

		rtw88xxa_tx_power_training(rtwdev, hal->current_band_width,
					   hal->current_channel, path);
	}
}
EXPORT_SYMBOL(rtw88xxa_set_tx_power_index);

void rtw88xxa_false_alarm_statistics(struct rtw_dev *rtwdev)
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
EXPORT_SYMBOL(rtw88xxa_false_alarm_statistics);

void rtw88xxa_iqk_backup_mac_bb(struct rtw_dev *rtwdev,
				u32 *macbb_backup,
				const u32 *backup_macbb_reg,
				u32 macbb_num)
{
	u32 i;

	/* [31] = 0 --> Page C */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);

	/* save MACBB default value */
	for (i = 0; i < macbb_num; i++)
		macbb_backup[i] = rtw_read32(rtwdev, backup_macbb_reg[i]);
}
EXPORT_SYMBOL(rtw88xxa_iqk_backup_mac_bb);

void rtw88xxa_iqk_backup_afe(struct rtw_dev *rtwdev, u32 *afe_backup,
			     const u32 *backup_afe_reg, u32 afe_num)
{
	u32 i;

	/* [31] = 0 --> Page C */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);

	/* Save AFE Parameters */
	for (i = 0; i < afe_num; i++)
		afe_backup[i] = rtw_read32(rtwdev, backup_afe_reg[i]);
}
EXPORT_SYMBOL(rtw88xxa_iqk_backup_afe);

void rtw88xxa_iqk_restore_mac_bb(struct rtw_dev *rtwdev,
				 u32 *macbb_backup,
				 const u32 *backup_macbb_reg,
				 u32 macbb_num)
{
	u32 i;

	/* [31] = 0 --> Page C */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);

	/* Reload MacBB Parameters */
	for (i = 0; i < macbb_num; i++)
		rtw_write32(rtwdev, backup_macbb_reg[i], macbb_backup[i]);
}
EXPORT_SYMBOL(rtw88xxa_iqk_restore_mac_bb);

void rtw88xxa_iqk_configure_mac(struct rtw_dev *rtwdev)
{
	/* [31] = 0 --> Page C */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);

	rtw_write8(rtwdev, REG_TXPAUSE, 0x3f);
	rtw_write32_mask(rtwdev, REG_BCN_CTRL,
			 (BIT_EN_BCN_FUNCTION << 8) | BIT_EN_BCN_FUNCTION, 0x0);

	/* RX ante off */
	rtw_write8(rtwdev, REG_RXPSEL, 0x00);

	/* CCA off */
	rtw_write32_mask(rtwdev, REG_CCA2ND, 0xf, 0xc);

	/* CCK RX path off */
	rtw_write8(rtwdev, REG_CCK_RX + 3, 0xf);
}
EXPORT_SYMBOL(rtw88xxa_iqk_configure_mac);

bool rtw88xxa_iqk_finish(int average, int threshold,
			 int *x_temp, int *y_temp, int *x, int *y,
			 bool break_inner, bool break_outer)
{
	bool finish = false;
	int i, ii, dx, dy;

	for (i = 0; i < average; i++) {
		for (ii = i + 1; ii < average; ii++) {
			dx = abs_diff(x_temp[i] >> 21, x_temp[ii] >> 21);
			dy = abs_diff(y_temp[i] >> 21, y_temp[ii] >> 21);

			if (dx < threshold && dy < threshold) {
				*x = ((x_temp[i] >> 21) + (x_temp[ii] >> 21));
				*y = ((y_temp[i] >> 21) + (y_temp[ii] >> 21));

				*x /= 2;
				*y /= 2;

				finish = true;

				if (break_inner)
					break;
			}
		}

		if (finish && break_outer)
			break;
	}

	return finish;
}
EXPORT_SYMBOL(rtw88xxa_iqk_finish);

static void rtw88xxa_pwrtrack_set(struct rtw_dev *rtwdev, u8 tx_rate, u8 path)
{
	static const u32 reg_txscale[2] = { REG_TXSCALE_A, REG_TXSCALE_B };
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 cck_swing_idx, ofdm_swing_idx;
	u8 pwr_tracking_limit;

	switch (tx_rate) {
	case DESC_RATE1M ... DESC_RATE11M:
		pwr_tracking_limit = 32;
		break;
	case DESC_RATE6M ... DESC_RATE48M:
	case DESC_RATEMCS3 ... DESC_RATEMCS4:
	case DESC_RATEMCS11 ... DESC_RATEMCS12:
	case DESC_RATEVHT1SS_MCS3 ... DESC_RATEVHT1SS_MCS4:
	case DESC_RATEVHT2SS_MCS3 ... DESC_RATEVHT2SS_MCS4:
		pwr_tracking_limit = 30;
		break;
	case DESC_RATE54M:
	case DESC_RATEMCS5 ... DESC_RATEMCS7:
	case DESC_RATEMCS13 ... DESC_RATEMCS15:
	case DESC_RATEVHT1SS_MCS5 ... DESC_RATEVHT1SS_MCS6:
	case DESC_RATEVHT2SS_MCS5 ... DESC_RATEVHT2SS_MCS6:
		pwr_tracking_limit = 28;
		break;
	case DESC_RATEMCS0 ... DESC_RATEMCS2:
	case DESC_RATEMCS8 ... DESC_RATEMCS10:
	case DESC_RATEVHT1SS_MCS0 ... DESC_RATEVHT1SS_MCS2:
	case DESC_RATEVHT2SS_MCS0 ... DESC_RATEVHT2SS_MCS2:
		pwr_tracking_limit = 34;
		break;
	case DESC_RATEVHT1SS_MCS7:
	case DESC_RATEVHT2SS_MCS7:
		pwr_tracking_limit = 26;
		break;
	default:
	case DESC_RATEVHT1SS_MCS8:
	case DESC_RATEVHT2SS_MCS8:
		pwr_tracking_limit = 24;
		break;
	case DESC_RATEVHT1SS_MCS9:
	case DESC_RATEVHT2SS_MCS9:
		pwr_tracking_limit = 22;
		break;
	}

	cck_swing_idx = dm_info->delta_power_index[path] + dm_info->default_cck_index;
	ofdm_swing_idx = dm_info->delta_power_index[path] + dm_info->default_ofdm_index;

	if (ofdm_swing_idx > pwr_tracking_limit) {
		if (path == RF_PATH_A)
			dm_info->txagc_remnant_cck = cck_swing_idx - pwr_tracking_limit;
		dm_info->txagc_remnant_ofdm[path] = ofdm_swing_idx - pwr_tracking_limit;

		ofdm_swing_idx = pwr_tracking_limit;
	} else if (ofdm_swing_idx == 0) {
		if (path == RF_PATH_A)
			dm_info->txagc_remnant_cck = cck_swing_idx;
		dm_info->txagc_remnant_ofdm[path] = ofdm_swing_idx;
	} else {
		if (path == RF_PATH_A)
			dm_info->txagc_remnant_cck = 0;
		dm_info->txagc_remnant_ofdm[path] = 0;
	}

	rtw_write32_mask(rtwdev, reg_txscale[path], GENMASK(31, 21),
			 rtw88xxa_txscale_tbl[ofdm_swing_idx]);
}

void rtw88xxa_phy_pwrtrack(struct rtw_dev *rtwdev,
			   void (*do_lck)(struct rtw_dev *rtwdev),
			   void (*do_iqk)(struct rtw_dev *rtwdev))
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_swing_table swing_table;
	s8 remnant_pre[RTW_RF_PATH_MAX];
	u8 thermal_value, delta, path;
	bool need_iqk;

	rtw_phy_config_swing_table(rtwdev, &swing_table);

	if (rtwdev->efuse.thermal_meter[0] == 0xff) {
		pr_err_once("efuse thermal meter is 0xff\n");
		return;
	}

	thermal_value = rtw_read_rf(rtwdev, RF_PATH_A, RF_T_METER, 0xfc00);

	rtw_phy_pwrtrack_avg(rtwdev, thermal_value, RF_PATH_A);

	need_iqk = rtw_phy_pwrtrack_need_iqk(rtwdev);

	if (need_iqk && do_lck)
		do_lck(rtwdev);

	if (dm_info->pwr_trk_init_trigger)
		dm_info->pwr_trk_init_trigger = false;
	else if (!rtw_phy_pwrtrack_thermal_changed(rtwdev, thermal_value,
						   RF_PATH_A))
		goto iqk;

	delta = rtw_phy_pwrtrack_get_delta(rtwdev, RF_PATH_A);

	for (path = RF_PATH_A; path < hal->rf_path_num; path++) {
		remnant_pre[path] = dm_info->txagc_remnant_ofdm[path];

		dm_info->delta_power_index[path] =
			rtw_phy_pwrtrack_get_pwridx(rtwdev, &swing_table, path,
						    RF_PATH_A, delta);

		if (dm_info->delta_power_index[path] !=
		    dm_info->delta_power_index_last[path]) {
			dm_info->delta_power_index_last[path] =
				dm_info->delta_power_index[path];

			rtw88xxa_pwrtrack_set(rtwdev, dm_info->tx_rate, path);
		}
	}

	for (path = RF_PATH_A; path < hal->rf_path_num; path++) {
		if (remnant_pre[path] != dm_info->txagc_remnant_ofdm[path]) {
			rtw_phy_set_tx_power_level(rtwdev,
						   hal->current_channel);
			break;
		}
	}

iqk:
	if (need_iqk)
		do_iqk(rtwdev);
}
EXPORT_SYMBOL(rtw88xxa_phy_pwrtrack);

void rtw88xxa_phy_cck_pd_set(struct rtw_dev *rtwdev, u8 new_lvl)
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
EXPORT_SYMBOL(rtw88xxa_phy_cck_pd_set);

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ac wireless 8821a/8811a/8812a common code");
MODULE_LICENSE("Dual BSD/GPL");
