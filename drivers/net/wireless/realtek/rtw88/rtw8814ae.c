// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2025  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/pci.h>
#include "pci.h"
#include "rtw8814a.h"

static const struct pci_device_id rtw_8814ae_id_table[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8813),
		.driver_data = (kernel_ulong_t)&rtw8814a_hw_spec
	},
	{}
};
MODULE_DEVICE_TABLE(pci, rtw_8814ae_id_table);

static struct pci_driver rtw_8814ae_driver = {
	.name = KBUILD_MODNAME,
	.id_table = rtw_8814ae_id_table,
	.probe = rtw_pci_probe,
	.remove = rtw_pci_remove,
	.driver.pm = &rtw_pm_ops,
	.shutdown = rtw_pci_shutdown,
};
module_pci_driver(rtw_8814ae_driver);

MODULE_AUTHOR("Bitterblue Smith <rtl8821cerfe2@gmail.com>");
MODULE_DESCRIPTION("Realtek 802.11ac wireless 8814ae driver");
MODULE_LICENSE("Dual BSD/GPL");
