// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2023  Realtek Corporation
 */

#include <linux/pci.h>

#include "mac.h"
#include "pci.h"
#include "reg.h"

enum pcie_rxbd_mode {
	PCIE_RXBD_NORM = 0,
	PCIE_RXBD_SEP,
	PCIE_RXBD_EXT,
};

#define PL0_TMR_SCALE_ASIC 1
#define PL0_TMR_ANA_172US 0x800
#define PL0_TMR_MAC_1MS 0x27100
#define PL0_TMR_AUX_1MS 0x1E848

static void rtw89_pci_aspm_set_be(struct rtw89_dev *rtwdev, bool enable)
{
	struct rtw89_pci *rtwpci = (struct rtw89_pci *)rtwdev->priv;
	struct pci_dev *pdev = rtwpci->pdev;
	u8 value = 0;
	int ret;

	ret = pci_read_config_byte(pdev, RTW89_PCIE_ASPM_CTRL, &value);
	if (ret)
		rtw89_warn(rtwdev, "failed to read ASPM Delay\n");

	u8p_replace_bits(&value, PCIE_L1DLY_16US, RTW89_L1DLY_MASK);

	ret = pci_write_config_byte(pdev, RTW89_PCIE_ASPM_CTRL, value);
	if (ret)
		rtw89_warn(rtwdev, "failed to write ASPM Delay\n");

	if (enable)
		rtw89_write32_set(rtwdev, R_AX_PCIE_MIX_CFG_V1,
				  B_BE_ASPM_CTRL_L1);
	else
		rtw89_write32_clr(rtwdev, R_AX_PCIE_MIX_CFG_V1,
				  B_BE_ASPM_CTRL_L1);
}

static void rtw89_pci_l1ss_set_be(struct rtw89_dev *rtwdev, bool enable)
{
	if (enable)
		rtw89_write32_set(rtwdev, R_BE_PCIE_MIX_CFG,
				  B_BE_L1SUB_ENABLE);
	else
		rtw89_write32_clr(rtwdev, R_BE_PCIE_MIX_CFG,
				  B_BE_L1SUB_ENABLE);
}

static void rtw89_pci_clkreq_set_be(struct rtw89_dev *rtwdev, bool enable)
{
	rtw89_write32_mask(rtwdev, R_BE_PCIE_LAT_CTRL, B_BE_CLK_REQ_LAT_MASK,
			   PCIE_CLKDLY_HW_V1_0);

	if (enable)
		rtw89_write32_set(rtwdev, R_BE_L1_CLK_CTRL,
				  B_BE_CLK_PM_EN);
	else
		rtw89_write32_clr(rtwdev, R_AX_L1_CLK_CTRL,
				  B_BE_CLK_PM_EN);
}

static void _patch_pcie_power_wake_be(struct rtw89_dev *rtwdev, bool power_up)
{
	if (power_up)
		rtw89_write32_set(rtwdev, R_BE_HCI_OPT_CTRL, BIT_WAKE_CTRL_V1);
	else
		rtw89_write32_clr(rtwdev, R_BE_HCI_OPT_CTRL, BIT_WAKE_CTRL_V1);
}

static void rtw89_pci_set_io_rcy_be(struct rtw89_dev *rtwdev)
{
	const struct rtw89_pci_info *info = rtwdev->pci_info;
	u32 scale = PL0_TMR_SCALE_ASIC;
	u32 val32;

	if (info->io_rcy_en == MAC_AX_PCIE_ENABLE) {
		val32 = info->io_rcy_tmr == MAC_AX_IO_RCY_ANA_TMR_DEF ?
			PL0_TMR_ANA_172US : info->io_rcy_tmr;
		val32 /= scale;

		rtw89_write32(rtwdev, R_BE_AON_WDT_TMR, val32);
		rtw89_write32(rtwdev, R_BE_MDIO_WDT_TMR, val32);
		rtw89_write32(rtwdev, R_BE_LA_MODE_WDT_TMR, val32);
		rtw89_write32(rtwdev, R_BE_WDT_AR_TMR, val32);
		rtw89_write32(rtwdev, R_BE_WDT_AW_TMR, val32);
		rtw89_write32(rtwdev, R_BE_WDT_W_TMR, val32);
		rtw89_write32(rtwdev, R_BE_WDT_B_TMR, val32);
		rtw89_write32(rtwdev, R_BE_WDT_R_TMR, val32);

		val32 = info->io_rcy_tmr == MAC_AX_IO_RCY_ANA_TMR_DEF ?
			PL0_TMR_MAC_1MS : info->io_rcy_tmr;
		val32 /= scale;
		rtw89_write32(rtwdev, R_BE_WLAN_WDT_TMR, val32);
		rtw89_write32(rtwdev, R_BE_AXIDMA_WDT_TMR, val32);

		val32 = info->io_rcy_tmr == MAC_AX_IO_RCY_ANA_TMR_DEF ?
			PL0_TMR_AUX_1MS : info->io_rcy_tmr;
		val32 /= scale;
		rtw89_write32(rtwdev, R_BE_LOCAL_WDT_TMR, val32);
	} else {
		rtw89_write32_clr(rtwdev, R_BE_WLAN_WDT, B_BE_WLAN_WDT_ENABLE);
		rtw89_write32_clr(rtwdev, R_BE_AXIDMA_WDT, B_BE_AXIDMA_WDT_ENABLE);
		rtw89_write32_clr(rtwdev, R_BE_AON_WDT, B_BE_AON_WDT_ENABLE);
		rtw89_write32_clr(rtwdev, R_BE_LOCAL_WDT, B_BE_LOCAL_WDT_ENABLE);
		rtw89_write32_clr(rtwdev, R_BE_MDIO_WDT, B_BE_MDIO_WDT_ENABLE);
		rtw89_write32_clr(rtwdev, R_BE_LA_MODE_WDT, B_BE_LA_MODE_WDT_ENABLE);
		rtw89_write32_clr(rtwdev, R_BE_WDT_AR, B_BE_WDT_AR_ENABLE);
		rtw89_write32_clr(rtwdev, R_BE_WDT_AW, B_BE_WDT_AW_ENABLE);
		rtw89_write32_clr(rtwdev, R_BE_WDT_W, B_BE_WDT_W_ENABLE);
		rtw89_write32_clr(rtwdev, R_BE_WDT_B, B_BE_WDT_B_ENABLE);
		rtw89_write32_clr(rtwdev, R_BE_WDT_R, B_BE_WDT_R_ENABLE);
	}
}

static void rtw89_pci_ctrl_wpdma_pcie_be(struct rtw89_dev *rtwdev, bool en)
{
	if (en)
		rtw89_write32_clr(rtwdev, R_BE_HAXI_DMA_STOP1, B_BE_STOP_WPDMA);
	else
		rtw89_write32_set(rtwdev, R_BE_HAXI_DMA_STOP1, B_BE_STOP_WPDMA);
}

static void rtw89_pci_ctrl_trxdma_pcie_be(struct rtw89_dev *rtwdev,
					  enum mac_ax_pcie_func_ctrl tx_en,
					  enum mac_ax_pcie_func_ctrl rx_en,
					  enum mac_ax_pcie_func_ctrl io_en)
{
	u32 val;

	val = rtw89_read32(rtwdev, R_BE_HAXI_INIT_CFG1);

	if (tx_en == MAC_AX_PCIE_ENABLE)
		val |= B_BE_TXDMA_EN;
	else if (tx_en == MAC_AX_PCIE_DISABLE)
		val &= ~B_BE_TXDMA_EN;

	if (rx_en == MAC_AX_PCIE_ENABLE)
		val |= B_BE_RXDMA_EN;
	else if (rx_en == MAC_AX_PCIE_DISABLE)
		val &= ~B_BE_RXDMA_EN;

	if (io_en == MAC_AX_PCIE_ENABLE)
		val &= ~B_BE_STOP_AXI_MST;
	else if (io_en == MAC_AX_PCIE_DISABLE)
		val |= B_BE_STOP_AXI_MST;

	rtw89_write32(rtwdev, R_BE_HAXI_INIT_CFG1, val);

	if (io_en == MAC_AX_PCIE_ENABLE)
		rtw89_write32_mask(rtwdev, R_BE_HAXI_MST_WDT_TIMEOUT_SEL_V1,
				   B_BE_HAXI_MST_WDT_TIMEOUT_SEL_MASK, 4);
}

static void rtw89_pci_clr_idx_all_be(struct rtw89_dev *rtwdev)
{
	struct rtw89_pci *rtwpci = (struct rtw89_pci *)rtwdev->priv;
	struct rtw89_pci_rx_ring *rx_ring;
	u32 val;

	val = B_BE_CLR_CH0_IDX | B_BE_CLR_CH1_IDX | B_BE_CLR_CH2_IDX |
	      B_BE_CLR_CH3_IDX | B_BE_CLR_CH4_IDX | B_BE_CLR_CH5_IDX |
	      B_BE_CLR_CH6_IDX | B_BE_CLR_CH7_IDX | B_BE_CLR_CH8_IDX |
	      B_BE_CLR_CH9_IDX | B_BE_CLR_CH10_IDX | B_BE_CLR_CH11_IDX |
	      B_BE_CLR_CH12_IDX | B_BE_CLR_CH13_IDX | B_BE_CLR_CH14_IDX;
	rtw89_write32(rtwdev, R_BE_TXBD_RWPTR_CLR1, val);

	rtw89_write32(rtwdev, R_BE_RXBD_RWPTR_CLR1_V1,
		      B_BE_CLR_RXQ0_IDX | B_BE_CLR_RPQ0_IDX);

	rx_ring = &rtwpci->rx_rings[RTW89_RXCH_RXQ];
	rtw89_write16(rtwdev, R_BE_RXQ0_RXBD_IDX_V1, rx_ring->bd_ring.len - 1);

	rx_ring = &rtwpci->rx_rings[RTW89_RXCH_RPQ];
	rtw89_write16(rtwdev, R_BE_RPQ0_RXBD_IDX_V1, rx_ring->bd_ring.len - 1);
}

static int rtw89_pci_poll_txdma_ch_idle_be(struct rtw89_dev *rtwdev)
{
	u32 val;

	return read_poll_timeout(rtw89_read32, val, (val & DMA_BUSY1_CHECK_BE) == 0,
				 10, 1000, false, rtwdev, R_BE_HAXI_DMA_BUSY1);
}

static int rtw89_pci_poll_rxdma_ch_idle_be(struct rtw89_dev *rtwdev)
{
	u32 check;
	u32 val;

	check = B_BE_RXQ0_BUSY_V1 | B_BE_RPQ0_BUSY_V1;

	return read_poll_timeout(rtw89_read32, val, (val & check) == 0,
				 10, 1000, false, rtwdev, R_BE_HAXI_DMA_BUSY1);
}

static int rtw89_pci_poll_dma_all_idle_be(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = rtw89_pci_poll_txdma_ch_idle_be(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "txdma ch busy\n");
		return ret;
	}

	ret = rtw89_pci_poll_rxdma_ch_idle_be(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "rxdma ch busy\n");
		return ret;
	}

	return 0;
}

static void rtw89_pci_mode_op_be(struct rtw89_dev *rtwdev)
{
	const struct rtw89_pci_info *info = rtwdev->pci_info;
	u32 val32_init1, val32_rxapp, val32_exp;

	val32_init1 = rtw89_read32(rtwdev, R_BE_HAXI_INIT_CFG1);
	val32_rxapp = rtw89_read32(rtwdev, R_BE_RX_APPEND_MODE);
	val32_exp = rtw89_read32(rtwdev, R_BE_HAXI_EXP_CTRL_V1);

	if (info->rxbd_mode == MAC_AX_RXBD_PKT) {
		val32_init1 = u32_replace_bits(val32_init1, PCIE_RXBD_NORM,
					       B_BE_RXQ_RXBD_MODE_MASK);
	} else if (info->rxbd_mode == MAC_AX_RXBD_SEP) {
		val32_init1 = u32_replace_bits(val32_init1, PCIE_RXBD_SEP,
					       B_BE_RXQ_RXBD_MODE_MASK);
		val32_rxapp = u32_replace_bits(val32_rxapp, 0,
					       B_BE_APPEND_LEN_MASK);
	}

	val32_init1 = u32_replace_bits(val32_init1, info->tx_burst,
				       B_BE_MAX_TXDMA_MASK);
	val32_init1 = u32_replace_bits(val32_init1, info->rx_burst,
				       B_BE_MAX_RXDMA_MASK);
	val32_exp = u32_replace_bits(val32_exp, info->multi_tag_num,
				     B_BE_MAX_TAG_NUM_MASK);
	val32_init1 = u32_replace_bits(val32_init1, info->wd_dma_idle_intvl,
				       B_BE_CFG_WD_PERIOD_IDLE_MASK);
	val32_init1 = u32_replace_bits(val32_init1, info->wd_dma_act_intvl,
				       B_BE_CFG_WD_PERIOD_ACTIVE_MASK);

	rtw89_write32(rtwdev, R_BE_HAXI_INIT_CFG1, val32_init1);
	rtw89_write32(rtwdev, R_BE_RX_APPEND_MODE, val32_rxapp);
	rtw89_write32(rtwdev, R_BE_HAXI_EXP_CTRL_V1, val32_exp);
}

static int rtw89_pci_rst_bdram_be(struct rtw89_dev *rtwdev)
{
	u32 val;

	rtw89_write32_set(rtwdev, R_BE_HAXI_INIT_CFG1, B_BE_SET_BDRAM_BOUND);

	return read_poll_timeout(rtw89_read32, val, !(val & B_BE_SET_BDRAM_BOUND),
				 50, 500000, false, rtwdev, R_BE_HAXI_INIT_CFG1);
}

static void rtw89_pci_debounce_be(struct rtw89_dev *rtwdev)
{
	u32 val32;

	val32 = rtw89_read32(rtwdev, R_BE_SYS_PAGE_CLK_GATED);
	val32 = u32_replace_bits(val32, 0, B_BE_PCIE_PRST_DEBUNC_PERIOD_MASK);
	val32 |= B_BE_SYM_PRST_DEBUNC_SEL;
	rtw89_write32(rtwdev, R_BE_SYS_PAGE_CLK_GATED, val32);
}

static void rtw89_pci_ldo_low_pwr_be(struct rtw89_dev *rtwdev)
{
	rtw89_write32_set(rtwdev, R_BE_SYS_PW_CTRL, B_BE_PSUS_OFF_CAPC_EN);
	rtw89_write32_set(rtwdev, R_BE_SYS_PAGE_CLK_GATED,
			  B_BE_SOP_OFFPOOBS_PC | B_BE_CPHY_AUXCLK_OP |
			  B_BE_CPHY_POWER_READY_CHK);
	rtw89_write32_clr(rtwdev, R_BE_SYS_SDIO_CTRL, B_BE_PCIE_FORCE_IBX_EN |
						      B_BE_PCIE_DIS_L2_RTK_PERST |
						      B_BE_PCIE_DIS_L2__CTRL_LDO_HCI);
	rtw89_write32_clr(rtwdev, R_BE_L1_2_CTRL_HCILDO, B_BE_PCIE_DIS_L1_2_CTRL_HCILDO);
}

static void rtw89_pci_pcie_setting_be(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_hal *hal = &rtwdev->hal;

	rtw89_write32_set(rtwdev, R_BE_PCIE_FRZ_CLK, B_BE_PCIE_EN_AUX_CLK);
	rtw89_write32_clr(rtwdev, R_BE_PCIE_PS_CTRL, B_BE_CMAC_EXIT_L1_EN);

	if (chip->chip_id == RTL8922A && hal->cv == CHIP_CAV)
		return;

	rtw89_write32_set(rtwdev, R_BE_EFUSE_CTRL_2_V1, B_BE_R_SYM_AUTOLOAD_WITH_PMC_SEL);
	rtw89_write32_set(rtwdev, R_BE_PCIE_LAT_CTRL, B_BE_SYM_AUX_CLK_SEL);
}

static void rtw89_pci_ser_setting_be(struct rtw89_dev *rtwdev)
{
	u32 val32;

	rtw89_write32(rtwdev, R_BE_PL1_DBG_INFO, 0x0);
	rtw89_write32_set(rtwdev, R_BE_FWS1IMR, B_BE_PCIE_SER_TIMEOUT_INDIC_EN);
	rtw89_write32_set(rtwdev, R_BE_SER_PL1_CTRL, B_BE_PL1_SER_PL1_EN);
	rtw89_write32_mask(rtwdev, R_BE_SER_PL1_CTRL, B_BE_PL1_TIMER_UNIT_MASK, 1);

	val32 = rtw89_read32(rtwdev, R_BE_REG_PL1_MASK);
	val32 |= B_BE_SER_PMU_IMR | B_BE_SER_L1SUB_IMR | B_BE_SER_PM_MASTER_IMR |
		 B_BE_SER_LTSSM_IMR | B_BE_SER_PM_CLK_MASK | B_BE_SER_PCLKREQ_ACK_MASK;
	rtw89_write32(rtwdev, R_BE_REG_PL1_MASK, val32);
}

static void rtw89_pci_ctrl_txdma_ch_be(struct rtw89_dev *rtwdev, bool enable)
{
	u32 mask_all;
	u32 val;

	mask_all = B_BE_STOP_CH0 | B_BE_STOP_CH1 | B_BE_STOP_CH2 |
		   B_BE_STOP_CH3 | B_BE_STOP_CH4 | B_BE_STOP_CH5 |
		   B_BE_STOP_CH6 | B_BE_STOP_CH7 | B_BE_STOP_CH8 |
		   B_BE_STOP_CH9 | B_BE_STOP_CH10 | B_BE_STOP_CH11;

	val = rtw89_read32(rtwdev, R_BE_HAXI_DMA_STOP1);
	val |= B_BE_STOP_CH13 | B_BE_STOP_CH14;

	if (enable)
		val &= ~mask_all;
	else
		val |= mask_all;

	rtw89_write32(rtwdev, R_BE_HAXI_DMA_STOP1, val);
}

static void rtw89_pci_ctrl_txdma_fw_ch_be(struct rtw89_dev *rtwdev, bool enable)
{
	u32 val = rtw89_read32(rtwdev, R_BE_HAXI_DMA_STOP1);

	if (enable)
		val &= ~B_BE_STOP_CH12;
	else
		val |= B_BE_STOP_CH12;

	rtw89_write32(rtwdev, R_BE_HAXI_DMA_STOP1, val);
}

static int rtw89_pci_ops_mac_pre_init_be(struct rtw89_dev *rtwdev)
{
	int ret;

	rtw89_pci_set_io_rcy_be(rtwdev);
	_patch_pcie_power_wake_be(rtwdev, true);
	rtw89_pci_ctrl_wpdma_pcie_be(rtwdev, false);
	rtw89_pci_ctrl_trxdma_pcie_be(rtwdev, MAC_AX_PCIE_DISABLE,
				      MAC_AX_PCIE_DISABLE, MAC_AX_PCIE_DISABLE);
	rtw89_pci_clr_idx_all_be(rtwdev);

	ret = rtw89_pci_poll_dma_all_idle_be(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "[ERR] poll pcie dma all idle\n");
		return ret;
	}

	rtw89_pci_mode_op_be(rtwdev);
	rtw89_pci_ops_reset(rtwdev);

	ret = rtw89_pci_rst_bdram_be(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "[ERR]pcie rst bdram\n");
		return ret;
	}

	rtw89_pci_debounce_be(rtwdev);
	rtw89_pci_ldo_low_pwr_be(rtwdev);
	rtw89_pci_pcie_setting_be(rtwdev);
	rtw89_pci_ser_setting_be(rtwdev);

	rtw89_pci_ctrl_txdma_ch_be(rtwdev, false);
	rtw89_pci_ctrl_txdma_fw_ch_be(rtwdev, true);
	rtw89_pci_ctrl_trxdma_pcie_be(rtwdev, MAC_AX_PCIE_ENABLE,
				      MAC_AX_PCIE_ENABLE, MAC_AX_PCIE_ENABLE);

	return 0;
}

static int rtw89_pci_ops_mac_pre_deinit_be(struct rtw89_dev *rtwdev)
{
	u32 val;

	_patch_pcie_power_wake_be(rtwdev, false);

	val = rtw89_read32_mask(rtwdev, R_BE_IC_PWR_STATE, B_BE_WLMAC_PWR_STE_MASK);
	if (val == 0)
		return 0;

	rtw89_pci_ctrl_trxdma_pcie_be(rtwdev, MAC_AX_PCIE_DISABLE,
				      MAC_AX_PCIE_DISABLE, MAC_AX_PCIE_DISABLE);
	rtw89_pci_clr_idx_all_be(rtwdev);

	return 0;
}

int rtw89_pci_ltr_set_v2(struct rtw89_dev *rtwdev, bool en)
{
	u32 ctrl0, cfg0, cfg1, dec_ctrl, idle_ltcy, act_ltcy, dis_ltcy;

	ctrl0 = rtw89_read32(rtwdev, R_BE_LTR_CTRL_0);
	if (rtw89_pci_ltr_is_err_reg_val(ctrl0))
		return -EINVAL;
	cfg0 = rtw89_read32(rtwdev, R_BE_LTR_CFG_0);
	if (rtw89_pci_ltr_is_err_reg_val(cfg0))
		return -EINVAL;
	cfg1 = rtw89_read32(rtwdev, R_BE_LTR_CFG_1);
	if (rtw89_pci_ltr_is_err_reg_val(cfg1))
		return -EINVAL;
	dec_ctrl = rtw89_read32(rtwdev, R_BE_LTR_DECISION_CTRL_V1);
	if (rtw89_pci_ltr_is_err_reg_val(dec_ctrl))
		return -EINVAL;
	idle_ltcy = rtw89_read32(rtwdev, R_BE_LTR_LATENCY_IDX3_V1);
	if (rtw89_pci_ltr_is_err_reg_val(idle_ltcy))
		return -EINVAL;
	act_ltcy = rtw89_read32(rtwdev, R_BE_LTR_LATENCY_IDX1_V1);
	if (rtw89_pci_ltr_is_err_reg_val(act_ltcy))
		return -EINVAL;
	dis_ltcy = rtw89_read32(rtwdev, R_BE_LTR_LATENCY_IDX0_V1);
	if (rtw89_pci_ltr_is_err_reg_val(dis_ltcy))
		return -EINVAL;

	if (en) {
		dec_ctrl |= B_BE_ENABLE_LTR_CTL_DECISION | B_BE_LTR_HW_DEC_EN_V1;
		ctrl0 |= B_BE_LTR_HW_EN;
	} else {
		dec_ctrl &= ~(B_BE_ENABLE_LTR_CTL_DECISION | B_BE_LTR_HW_DEC_EN_V1 |
			      B_BE_LTR_EN_PORT_V1_MASK);
		ctrl0 &= ~B_BE_LTR_HW_EN;
	}

	dec_ctrl = u32_replace_bits(dec_ctrl, PCI_LTR_SPC_500US,
				    B_BE_LTR_SPACE_IDX_MASK);
	cfg0 = u32_replace_bits(cfg0, PCI_LTR_IDLE_TIMER_3_2MS,
				B_BE_LTR_IDLE_TIMER_IDX_MASK);
	cfg1 = u32_replace_bits(cfg1, 0xC0, B_BE_LTR_CMAC0_RX_USE_PG_TH_MASK);
	cfg1 = u32_replace_bits(cfg1, 0xC0, B_BE_LTR_CMAC1_RX_USE_PG_TH_MASK);
	cfg0 = u32_replace_bits(cfg0, 1, B_BE_LTR_IDX_ACTIVE_MASK);
	cfg0 = u32_replace_bits(cfg0, 3, B_BE_LTR_IDX_IDLE_MASK);
	dec_ctrl = u32_replace_bits(dec_ctrl, 0, B_BE_LTR_IDX_DISABLE_V1_MASK);

	rtw89_write32(rtwdev, R_BE_LTR_LATENCY_IDX3_V1, 0x90039003);
	rtw89_write32(rtwdev, R_BE_LTR_LATENCY_IDX1_V1, 0x880b880b);
	rtw89_write32(rtwdev, R_BE_LTR_LATENCY_IDX0_V1, 0);
	rtw89_write32(rtwdev, R_BE_LTR_DECISION_CTRL_V1, dec_ctrl);
	rtw89_write32(rtwdev, R_BE_LTR_CFG_0, cfg0);
	rtw89_write32(rtwdev, R_BE_LTR_CFG_1, cfg1);
	rtw89_write32(rtwdev, R_BE_LTR_CTRL_0, ctrl0);

	return 0;
}
EXPORT_SYMBOL(rtw89_pci_ltr_set_v2);

static void rtw89_pci_configure_mit_be(struct rtw89_dev *rtwdev)
{
	u32 cnt;
	u32 val;

	rtw89_write32_mask(rtwdev, R_BE_PCIE_MIT0_TMR,
			   B_BE_PCIE_MIT0_RX_TMR_MASK, BE_MIT0_TMR_UNIT_1MS);

	val = rtw89_read32(rtwdev, R_BE_PCIE_MIT0_CNT);
	cnt = min_t(u32, U8_MAX, RTW89_PCI_RXBD_NUM_MAX / 2);
	val = u32_replace_bits(val, cnt, B_BE_PCIE_RX_MIT0_CNT_MASK);
	val = u32_replace_bits(val, 2, B_BE_PCIE_RX_MIT0_TMR_CNT_MASK);
	rtw89_write32(rtwdev, R_BE_PCIE_MIT0_CNT, val);
}

static int rtw89_pci_ops_mac_post_init_be(struct rtw89_dev *rtwdev)
{
	const struct rtw89_pci_info *info = rtwdev->pci_info;
	int ret;

	ret = info->ltr_set(rtwdev, true);
	if (ret) {
		rtw89_err(rtwdev, "pci ltr set fail\n");
		return ret;
	}

	rtw89_pci_ctrl_trxdma_pcie_be(rtwdev, MAC_AX_PCIE_IGNORE,
				      MAC_AX_PCIE_IGNORE, MAC_AX_PCIE_ENABLE);
	rtw89_pci_ctrl_wpdma_pcie_be(rtwdev, true);
	rtw89_pci_ctrl_txdma_ch_be(rtwdev, true);
	rtw89_pci_ctrl_txdma_fw_ch_be(rtwdev, true);
	rtw89_pci_configure_mit_be(rtwdev);

	return 0;
}

static int rtw89_pci_poll_io_idle_be(struct rtw89_dev *rtwdev)
{
	u32 sts;
	int ret;

	ret = read_poll_timeout_atomic(rtw89_read32, sts,
				       !(sts & B_BE_HAXI_MST_BUSY),
				       10, 1000, false, rtwdev,
				       R_BE_HAXI_DMA_BUSY1);
	if (ret) {
		rtw89_err(rtwdev, "pci dmach busy1 0x%X\n", sts);
		return ret;
	}

	return 0;
}

static int rtw89_pci_lv1rst_stop_dma_be(struct rtw89_dev *rtwdev)
{
	int ret;

	rtw89_pci_ctrl_dma_all(rtwdev, false);
	ret = rtw89_pci_poll_io_idle_be(rtwdev);
	if (!ret)
		return 0;

	rtw89_debug(rtwdev, RTW89_DBG_HCI,
		    "[PCIe] poll_io_idle fail; reset hci dma trx\n");

	rtw89_mac_ctrl_hci_dma_trx(rtwdev, false);
	rtw89_mac_ctrl_hci_dma_trx(rtwdev, true);

	return rtw89_pci_poll_io_idle_be(rtwdev);
}

static int rtw89_pci_lv1rst_start_dma_be(struct rtw89_dev *rtwdev)
{
	int ret;

	rtw89_mac_ctrl_hci_dma_trx(rtwdev, false);
	rtw89_mac_ctrl_hci_dma_trx(rtwdev, true);
	rtw89_pci_clr_idx_all(rtwdev);

	ret = rtw89_pci_rst_bdram_be(rtwdev);
	if (ret)
		return ret;

	rtw89_pci_ctrl_dma_all(rtwdev, true);
	return 0;
}

static void rtw89_pci_disable_eq_be(struct rtw89_dev *rtwdev)
{
	u32 backup_aspm, phy_offset;
	u16 oobs_val, offset_cal;
	u16 g1_oobs, g2_oobs;
	u8 gen;

	if (rtwdev->chip->chip_id != RTL8922A)
		return;

	g1_oobs = rtw89_read16_mask(rtwdev, R_RAC_DIRECT_OFFSET_BE_LANE0_G1 +
					    RAC_ANA09 * RAC_MULT, BAC_OOBS_SEL);
	g2_oobs = rtw89_read16_mask(rtwdev, R_RAC_DIRECT_OFFSET_BE_LANE0_G2 +
					    RAC_ANA09 * RAC_MULT, BAC_OOBS_SEL);
	if (g1_oobs && g2_oobs)
		return;

	backup_aspm = rtw89_read32(rtwdev, R_BE_PCIE_MIX_CFG);
	rtw89_write32_clr(rtwdev, R_BE_PCIE_MIX_CFG, B_BE_RTK_ASPM_CTRL_MASK);

	/* offset K */
	for (gen = 1; gen <= 2; gen++) {
		phy_offset = gen == 1 ? R_RAC_DIRECT_OFFSET_BE_LANE0_G1 :
					R_RAC_DIRECT_OFFSET_BE_LANE0_G2;

		rtw89_write16_clr(rtwdev, phy_offset + RAC_ANA19 * RAC_MULT,
				  B_PCIE_BIT_RD_SEL);
	}

	offset_cal = rtw89_read16_mask(rtwdev, R_RAC_DIRECT_OFFSET_BE_LANE0_G1 +
					       RAC_ANA1F * RAC_MULT, OFFSET_CAL_MASK);

	for (gen = 1; gen <= 2; gen++) {
		phy_offset = gen == 1 ? R_RAC_DIRECT_OFFSET_BE_LANE0_G1 :
					R_RAC_DIRECT_OFFSET_BE_LANE0_G2;

		rtw89_write16_mask(rtwdev, phy_offset + RAC_ANA0B * RAC_MULT,
				   MANUAL_LVL_MASK, offset_cal);
		rtw89_write16_clr(rtwdev, phy_offset + RAC_ANA0D * RAC_MULT,
				  OFFSET_CAL_MODE);
	}

	/* OOBS */
	for (gen = 1; gen <= 2; gen++) {
		phy_offset = gen == 1 ? R_RAC_DIRECT_OFFSET_BE_LANE0_G1 :
					R_RAC_DIRECT_OFFSET_BE_LANE0_G2;

		rtw89_write16_set(rtwdev, phy_offset + RAC_ANA0D * RAC_MULT,
				  BAC_RX_TEST_EN);
		rtw89_write16_mask(rtwdev, phy_offset + RAC_ANA10 * RAC_MULT,
				   ADDR_SEL_MASK, ADDR_SEL_VAL);
		rtw89_write16_clr(rtwdev, phy_offset + RAC_ANA10 * RAC_MULT,
				  B_PCIE_BIT_PINOUT_DIS);
		rtw89_write16_set(rtwdev, phy_offset + RAC_ANA19 * RAC_MULT,
				  B_PCIE_BIT_RD_SEL);
	}

	oobs_val = rtw89_read16_mask(rtwdev, R_RAC_DIRECT_OFFSET_BE_LANE0_G1 +
					     RAC_ANA1F * RAC_MULT, OOBS_LEVEL_MASK);

	for (gen = 1; gen <= 2; gen++) {
		phy_offset = gen == 1 ? R_RAC_DIRECT_OFFSET_BE_LANE0_G1 :
					R_RAC_DIRECT_OFFSET_BE_LANE0_G2;

		rtw89_write16_mask(rtwdev, phy_offset + RAC_ANA03 * RAC_MULT,
				   OOBS_SEN_MASK, oobs_val);
		rtw89_write16_set(rtwdev, phy_offset + RAC_ANA09 * RAC_MULT,
				  BAC_OOBS_SEL);
	}

	rtw89_write32(rtwdev, R_BE_PCIE_MIX_CFG, backup_aspm);
}

static int __maybe_unused rtw89_pci_suspend_be(struct device *dev)
{
	struct ieee80211_hw *hw = dev_get_drvdata(dev);
	struct rtw89_dev *rtwdev = hw->priv;

	rtw89_write32_set(rtwdev, R_BE_RSV_CTRL, B_BE_WLOCK_1C_BIT6);
	rtw89_write32_set(rtwdev, R_BE_RSV_CTRL, B_BE_R_DIS_PRST);
	rtw89_write32_clr(rtwdev, R_BE_RSV_CTRL, B_BE_WLOCK_1C_BIT6);
	rtw89_write32_set(rtwdev, R_BE_PCIE_FRZ_CLK, B_BE_PCIE_FRZ_REG_RST);
	rtw89_write32_clr(rtwdev, R_BE_REG_PL1_MASK, B_BE_SER_PM_MASTER_IMR);
	return 0;
}

static int __maybe_unused rtw89_pci_resume_be(struct device *dev)
{
	struct ieee80211_hw *hw = dev_get_drvdata(dev);
	struct rtw89_dev *rtwdev = hw->priv;
	u32 polling;
	int ret;

	rtw89_write32_set(rtwdev, R_BE_RSV_CTRL, B_BE_WLOCK_1C_BIT6);
	rtw89_write32_clr(rtwdev, R_BE_RSV_CTRL, B_BE_R_DIS_PRST);
	rtw89_write32_clr(rtwdev, R_BE_RSV_CTRL, B_BE_WLOCK_1C_BIT6);
	rtw89_write32_clr(rtwdev, R_BE_PCIE_FRZ_CLK, B_BE_PCIE_FRZ_REG_RST);
	rtw89_write32_clr(rtwdev, R_BE_SER_PL1_CTRL, B_BE_PL1_SER_PL1_EN);

	ret = read_poll_timeout_atomic(rtw89_read32, polling, !polling, 1, 1000,
				       false, rtwdev, R_BE_REG_PL1_ISR);
	if (ret)
		rtw89_warn(rtwdev, "[ERR] PCIE SER clear polling fail\n");

	rtw89_write32_set(rtwdev, R_BE_SER_PL1_CTRL, B_BE_PL1_SER_PL1_EN);
	rtw89_write32_set(rtwdev, R_BE_REG_PL1_MASK, B_BE_SER_PM_MASTER_IMR);

	rtw89_pci_basic_cfg(rtwdev, true);

	return 0;
}

SIMPLE_DEV_PM_OPS(rtw89_pm_ops_be, rtw89_pci_suspend_be, rtw89_pci_resume_be);
EXPORT_SYMBOL(rtw89_pm_ops_be);

const struct rtw89_pci_gen_def rtw89_pci_gen_be = {
	.isr_rdu = B_BE_RDU_CH1_INT | B_BE_RDU_CH0_INT,
	.isr_halt_c2h = B_BE_HALT_C2H_INT,
	.isr_wdt_timeout = B_BE_WDT_TIMEOUT_INT,
	.isr_clear_rpq = {R_BE_PCIE_DMA_ISR, B_BE_PCIE_RX_RPQ0_ISR_V1},
	.isr_clear_rxq = {R_BE_PCIE_DMA_ISR, B_BE_PCIE_RX_RX0P2_ISR_V1},

	.mac_pre_init = rtw89_pci_ops_mac_pre_init_be,
	.mac_pre_deinit = rtw89_pci_ops_mac_pre_deinit_be,
	.mac_post_init = rtw89_pci_ops_mac_post_init_be,

	.clr_idx_all = rtw89_pci_clr_idx_all_be,
	.rst_bdram = rtw89_pci_rst_bdram_be,

	.lv1rst_stop_dma = rtw89_pci_lv1rst_stop_dma_be,
	.lv1rst_start_dma = rtw89_pci_lv1rst_start_dma_be,

	.ctrl_txdma_ch = rtw89_pci_ctrl_txdma_ch_be,
	.ctrl_txdma_fw_ch = rtw89_pci_ctrl_txdma_fw_ch_be,
	.poll_txdma_ch_idle = rtw89_pci_poll_txdma_ch_idle_be,

	.aspm_set = rtw89_pci_aspm_set_be,
	.clkreq_set = rtw89_pci_clkreq_set_be,
	.l1ss_set = rtw89_pci_l1ss_set_be,

	.disable_eq = rtw89_pci_disable_eq_be,
	.power_wake = _patch_pcie_power_wake_be,
};
EXPORT_SYMBOL(rtw89_pci_gen_be);
