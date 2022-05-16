/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW_8723DE_H_
#define __RTW_8723DE_H_

extern const struct dev_pm_ops rtw_pm_ops;
extern struct rtw_chip_info rtw8723d_hw_spec;
int rtw_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id);
void rtw_pci_remove(struct pci_dev *pdev);
void rtw_pci_shutdown(struct pci_dev *pdev);

#endif
