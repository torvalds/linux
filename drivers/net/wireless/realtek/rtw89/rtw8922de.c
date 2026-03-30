// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2026  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/pci.h>

#include "pci.h"
#include "reg.h"
#include "rtw8922d.h"

static const struct rtw89_pci_info rtw8922d_pci_info = {
	.gen_def		= &rtw89_pci_gen_be,
	.isr_def		= &rtw89_pci_isr_be_v1,
	.txbd_trunc_mode	= MAC_AX_BD_TRUNC,
	.rxbd_trunc_mode	= MAC_AX_BD_TRUNC,
	.rxbd_mode		= MAC_AX_RXBD_PKT,
	.tag_mode		= MAC_AX_TAG_MULTI,
	.tx_burst		= MAC_AX_TX_BURST_V1_256B,
	.rx_burst		= MAC_AX_RX_BURST_V1_128B,
	.wd_dma_idle_intvl	= MAC_AX_WD_DMA_INTVL_256NS,
	.wd_dma_act_intvl	= MAC_AX_WD_DMA_INTVL_256NS,
	.multi_tag_num		= MAC_AX_TAG_NUM_8,
	.lbc_en			= MAC_AX_PCIE_ENABLE,
	.lbc_tmr		= MAC_AX_LBC_TMR_2MS,
	.autok_en		= MAC_AX_PCIE_DISABLE,
	.io_rcy_en		= MAC_AX_PCIE_ENABLE,
	.io_rcy_tmr		= MAC_AX_IO_RCY_ANA_TMR_DEF,
	.rx_ring_eq_is_full	= true,
	.check_rx_tag		= true,
	.no_rxbd_fs		= true,
	.group_bd_addr		= true,
	.rpp_fmt_size		= sizeof(struct rtw89_pci_rpp_fmt_v1),

	.init_cfg_reg		= R_BE_HAXI_INIT_CFG1,
	.txhci_en_bit		= B_BE_TXDMA_EN,
	.rxhci_en_bit		= B_BE_RXDMA_EN,
	.rxbd_mode_bit		= B_BE_RXQ_RXBD_MODE_MASK,
	.exp_ctrl_reg		= R_BE_HAXI_EXP_CTRL_V1,
	.max_tag_num_mask	= B_BE_MAX_TAG_NUM_MASK,
	.rxbd_rwptr_clr_reg	= R_BE_RXBD_RWPTR_CLR1_V1,
	.txbd_rwptr_clr2_reg	= R_BE_TXBD_RWPTR_CLR1,
	.dma_io_stop		= {R_BE_HAXI_INIT_CFG1, B_BE_STOP_AXI_MST},
	.dma_stop1		= {R_BE_HAXI_DMA_STOP1, B_BE_TX_STOP1_MASK_V1},
	.dma_stop2		= {0},
	.dma_busy1		= {R_BE_HAXI_DMA_BUSY1, DMA_BUSY1_CHECK_BE_V1},
	.dma_busy2_reg		= 0,
	.dma_busy3_reg		= R_BE_HAXI_DMA_BUSY1,

	.rpwm_addr		= R_BE_PCIE_HRPWM,
	.cpwm_addr		= R_BE_PCIE_CRPWM,
	.mit_addr		= R_BE_PCIE_MIT_CH_EN,
	.wp_sel_addr		= R_BE_WP_ADDR_H_SEL0_3_V1,
	.tx_dma_ch_mask		= BIT(RTW89_TXCH_ACH1) | BIT(RTW89_TXCH_ACH3) |
				  BIT(RTW89_TXCH_ACH5) | BIT(RTW89_TXCH_ACH7) |
				  BIT(RTW89_TXCH_CH9) | BIT(RTW89_TXCH_CH11),
	.bd_idx_addr_low_power	= NULL,
	.dma_addr_set		= &rtw89_pci_ch_dma_addr_set_be_v1,
	.bd_ram_table		= NULL,

	.ltr_set		= rtw89_pci_ltr_set_v2,
	.fill_txaddr_info	= rtw89_pci_fill_txaddr_info_v1,
	.parse_rpp		= rtw89_pci_parse_rpp_v1,
	.config_intr_mask	= rtw89_pci_config_intr_mask_v3,
	.enable_intr		= rtw89_pci_enable_intr_v3,
	.disable_intr		= rtw89_pci_disable_intr_v3,
	.recognize_intrs	= rtw89_pci_recognize_intrs_v3,

	.ssid_quirks		= NULL,
};

static const struct rtw89_driver_info rtw89_8922de_vs_info = {
	.chip = &rtw8922d_chip_info,
	.variant = &rtw8922de_vs_variant,
	.quirks = NULL,
	.bus = {
		.pci = &rtw8922d_pci_info,
	},
};

static const struct rtw89_driver_info rtw89_8922de_info = {
	.chip = &rtw8922d_chip_info,
	.variant = NULL,
	.quirks = NULL,
	.bus = {
		.pci = &rtw8922d_pci_info,
	},
};

static const struct pci_device_id rtw89_8922de_id_table[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x892D),
		.driver_data = (kernel_ulong_t)&rtw89_8922de_vs_info,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x882D),
		.driver_data = (kernel_ulong_t)&rtw89_8922de_vs_info,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x895D),
		.driver_data = (kernel_ulong_t)&rtw89_8922de_info,
	},
	{},
};
MODULE_DEVICE_TABLE(pci, rtw89_8922de_id_table);

static struct pci_driver rtw89_8922de_driver = {
	.name		= "rtw89_8922de",
	.id_table	= rtw89_8922de_id_table,
	.probe		= rtw89_pci_probe,
	.remove		= rtw89_pci_remove,
	.driver.pm	= &rtw89_pm_ops_be,
	.err_handler    = &rtw89_pci_err_handler,
};
module_pci_driver(rtw89_8922de_driver);

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11be wireless 8922DE/8922DE-VS driver");
MODULE_LICENSE("Dual BSD/GPL");
