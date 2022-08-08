/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2021 NXP Semiconductors */

#include <linux/pci.h>
#include <linux/platform_device.h>

#if IS_ENABLED(CONFIG_FSL_ENETC_IERB)

int enetc_ierb_register_pf(struct platform_device *pdev,
			   struct pci_dev *pf_pdev);

#else

static inline int enetc_ierb_register_pf(struct platform_device *pdev,
					 struct pci_dev *pf_pdev)
{
	return -EOPNOTSUPP;
}

#endif
