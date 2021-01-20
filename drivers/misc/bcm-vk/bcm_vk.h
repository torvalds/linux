/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2018-2020 Broadcom.
 */

#ifndef BCM_VK_H
#define BCM_VK_H

#include <linux/pci.h>

#define DRV_MODULE_NAME		"bcm-vk"

/* VK device supports a maximum of 3 bars */
#define MAX_BAR	3

enum pci_barno {
	BAR_0 = 0,
	BAR_1,
	BAR_2
};

#define BCM_VK_NUM_TTY 2

struct bcm_vk {
	struct pci_dev *pdev;
	void __iomem *bar[MAX_BAR];
};

#endif
