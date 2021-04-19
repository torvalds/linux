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

#ifdef CONFIG_X86_INTEL_MID

extern void intel_scu_devices_create(void);
extern void intel_scu_devices_destroy(void);

#else /* !CONFIG_X86_INTEL_MID */

static inline void intel_scu_devices_create(void) { }
static inline void intel_scu_devices_destroy(void) { }

#endif /* !CONFIG_X86_INTEL_MID */

/* Bus Select SoC Fuse value */
#define BSEL_SOC_FUSE_MASK		0x7
/* FSB 133MHz */
#define BSEL_SOC_FUSE_001		0x1
/* FSB 100MHz */
#define BSEL_SOC_FUSE_101		0x5
/* FSB 83MHz */
#define BSEL_SOC_FUSE_111		0x7

#endif /* _ASM_X86_INTEL_MID_H */
