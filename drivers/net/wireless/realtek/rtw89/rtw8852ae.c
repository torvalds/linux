// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2020-2021  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/pci.h>

#include "pci.h"
#include "rtw8852a.h"

static const struct rtw89_pci_info rtw8852a_pci_info = {
	.init_cfg_reg		= R_AX_PCIE_INIT_CFG1,
	.txhci_en_bit		= B_AX_TXHCI_EN,
	.rxhci_en_bit		= B_AX_RXHCI_EN,
	.rxbd_mode_bit		= B_AX_RXBD_MODE,
	.exp_ctrl_reg		= R_AX_PCIE_EXP_CTRL,
	.max_tag_num_mask	= B_AX_MAX_TAG_NUM,
	.rxbd_rwptr_clr_reg	= R_AX_RXBD_RWPTR_CLR,
	.txbd_rwptr_clr2_reg	= R_AX_TXBD_RWPTR_CLR2,
	.dma_stop1_reg		= R_AX_PCIE_DMA_STOP1,
	.dma_stop2_reg		= R_AX_PCIE_DMA_STOP2,

	.dma_addr_set		= &rtw89_pci_ch_dma_addr_set,

	.fill_txaddr_info	= rtw89_pci_fill_txaddr_info,
};

static const struct rtw89_driver_info rtw89_8852ae_info = {
	.chip = &rtw8852a_chip_info,
	.bus = {
		.pci = &rtw8852a_pci_info,
	},
};

static const struct pci_device_id rtw89_8852ae_id_table[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8852),
		.driver_data = (kernel_ulong_t)&rtw89_8852ae_info,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0xa85a),
		.driver_data = (kernel_ulong_t)&rtw89_8852ae_info,
	},
	{},
};
MODULE_DEVICE_TABLE(pci, rtw89_8852ae_id_table);

static struct pci_driver rtw89_8852ae_driver = {
	.name		= "rtw89_8852ae",
	.id_table	= rtw89_8852ae_id_table,
	.probe		= rtw89_pci_probe,
	.remove		= rtw89_pci_remove,
	.driver.pm	= &rtw89_pm_ops,
};
module_pci_driver(rtw89_8852ae_driver);

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8852AE driver");
MODULE_LICENSE("Dual BSD/GPL");
