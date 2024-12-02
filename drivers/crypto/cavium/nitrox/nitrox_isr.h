/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NITROX_ISR_H
#define __NITROX_ISR_H

#include "nitrox_dev.h"

int nitrox_register_interrupts(struct nitrox_device *ndev);
void nitrox_unregister_interrupts(struct nitrox_device *ndev);
int nitrox_sriov_register_interupts(struct nitrox_device *ndev);
void nitrox_sriov_unregister_interrupts(struct nitrox_device *ndev);

#ifdef CONFIG_PCI_IOV
int nitrox_sriov_configure(struct pci_dev *pdev, int num_vfs);
#else
static inline int nitrox_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	return 0;
}
#endif

#endif /* __NITROX_ISR_H */
