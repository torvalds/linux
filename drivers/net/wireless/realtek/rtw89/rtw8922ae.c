// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2023  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/pci.h>

#include "pci.h"
#include "reg.h"
#include "rtw8922a.h"

static const struct rtw89_pci_info rtw8922a_pci_info = {
	.gen_def		= &rtw89_pci_gen_be,
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

	.init_cfg_reg		= R_BE_HAXI_INIT_CFG1,
	.txhci_en_bit		= B_BE_TXDMA_EN,
	.rxhci_en_bit		= B_BE_RXDMA_EN,
	.rxbd_mode_bit		= B_BE_RXQ_RXBD_MODE_MASK,
	.exp_ctrl_reg		= R_BE_HAXI_EXP_CTRL_V1,
	.max_tag_num_mask	= B_BE_MAX_TAG_NUM_MASK,
	.rxbd_rwptr_clr_reg	= R_BE_RXBD_RWPTR_CLR1_V1,
	.txbd_rwptr_clr2_reg	= R_BE_TXBD_RWPTR_CLR1,
	.dma_io_stop		= {R_BE_HAXI_INIT_CFG1, B_BE_STOP_AXI_MST},
	.dma_stop1		= {R_BE_HAXI_DMA_STOP1, B_BE_TX_STOP1_MASK},
	.dma_stop2		= {0},
	.dma_busy1		= {R_BE_HAXI_DMA_BUSY1, DMA_BUSY1_CHECK_BE},
	.dma_busy2_reg		= 0,
	.dma_busy3_reg		= R_BE_HAXI_DMA_BUSY1,

	.rpwm_addr		= R_BE_PCIE_HRPWM,
	.cpwm_addr		= R_BE_PCIE_CRPWM,
	.mit_addr		= R_BE_PCIE_MIT_CH_EN,
	.tx_dma_ch_mask		= 0,
	.bd_idx_addr_low_power	= NULL,
	.dma_addr_set		= &rtw89_pci_ch_dma_addr_set_be,
	.bd_ram_table		= NULL,

	.ltr_set		= rtw89_pci_ltr_set_v2,
	.fill_txaddr_info	= rtw89_pci_fill_txaddr_info_v1,
	.config_intr_mask	= rtw89_pci_config_intr_mask_v2,
	.enable_intr		= rtw89_pci_enable_intr_v2,
	.disable_intr		= rtw89_pci_disable_intr_v2,
	.recognize_intrs	= rtw89_pci_recognize_intrs_v2,
};

static const struct rtw89_driver_info rtw89_8922ae_info = {
	.chip = &rtw8922a_chip_info,
	.quirks = NULL,
	.bus = {
		.pci = &rtw8922a_pci_info,
	},
};

static const struct pci_device_id rtw89_8922ae_id_table[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8922),
		.driver_data = (kernel_ulong_t)&rtw89_8922ae_info,
	},
	{},
};
MODULE_DEVICE_TABLE(pci, rtw89_8922ae_id_table);

static struct pci_driver rtw89_8922ae_driver = {
	.name		= "rtw89_8922ae",
	.id_table	= rtw89_8922ae_id_table,
	.probe		= rtw89_pci_probe,
	.remove		= rtw89_pci_remove,
	.driver.pm	= &rtw89_pm_ops_be,
};
module_pci_driver(rtw89_8922ae_driver);

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11be wireless 8922AE driver");
MODULE_LICENSE("Dual BSD/GPL");
