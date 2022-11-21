// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2020-2021  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/pci.h>

#include "pci.h"
#include "rtw8852a.h"

static const struct rtw89_pci_info rtw8852a_pci_info = {
	.dma_addr_set		= &rtw89_pci_ch_dma_addr_set,
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
