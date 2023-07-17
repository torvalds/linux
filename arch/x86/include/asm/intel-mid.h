/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel MID specific setup code
 *
 * (C) Copyright 2009, 2021 Intel Corporation
 */
#ifndef _ASM_X86_INTEL_MID_H
#define _ASM_X86_INTEL_MID_H

#include <linux/pci.h>

extern int intel_mid_pci_init(void);
extern int intel_mid_pci_set_power_state(struct pci_dev *pdev, pci_power_t state);
extern pci_power_t intel_mid_pci_get_power_state(struct pci_dev *pdev);

extern void intel_mid_pwr_power_off(void);

#define INTEL_MID_PWR_LSS_OFFSET	4
#define INTEL_MID_PWR_LSS_TYPE		(1 << 7)

extern int intel_mid_pwr_get_lss_id(struct pci_dev *pdev);

#endif /* _ASM_X86_INTEL_MID_H */
