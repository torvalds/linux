// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2023  Realtek Corporation
 */

#include <linux/pci.h>

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

	val32 = rtw89_read32(rtwdev, R_BE_REG_PL1_MASK);
	val32 |= B_BE_SER_PMU_IMR | B_BE_SER_L1SUB_IMR | B_BE_SER_PM_MASTER_IMR |
		 B_BE_SER_LTSSM_IMR | B_BE_SER_PM_CLK_MASK | B_BE_SER_PCLKREQ_ACK_MASK;
	rtw89_write32(rtwdev, R_BE_REG_PL1_MASK, val32);
}

static void rtw89_pci_ctrl_txdma_ch_be(struct rtw89_dev *rtwdev, bool all_en,
				       bool h2c_en)
{
	u32 mask_all;
	u32 val;

	mask_all = B_BE_STOP_CH0 | B_BE_STOP_CH1 | B_BE_STOP_CH2 |
		   B_BE_STOP_CH3 | B_BE_STOP_CH4 | B_BE_STOP_CH5 |
		   B_BE_STOP_CH6 | B_BE_STOP_CH7 | B_BE_STOP_CH8 |
		   B_BE_STOP_CH9 | B_BE_STOP_CH10 | B_BE_STOP_CH11;

	val = rtw89_read32(rtwdev, R_BE_HAXI_DMA_STOP1);
	val |= B_BE_STOP_CH13 | B_BE_STOP_CH14;

	if (all_en)
		val &= ~mask_all;
	else
		val |= mask_all;

	if (h2c_en)
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

	rtw89_pci_ctrl_txdma_ch_be(rtwdev, false, true);
	rtw89_pci_ctrl_trxdma_pcie_be(rtwdev, MAC_AX_PCIE_ENABLE,
				      MAC_AX_PCIE_ENABLE, MAC_AX_PCIE_ENABLE);

	return 0;
}

const struct rtw89_pci_gen_def rtw89_pci_gen_be = {
	.mac_pre_init = rtw89_pci_ops_mac_pre_init_be,

	.clr_idx_all = rtw89_pci_clr_idx_all_be,
};
EXPORT_SYMBOL(rtw89_pci_gen_be);
