/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Arch specific extensions to struct device
 */
#ifndef _ASM_IA64_DEVICE_H
#define _ASM_IA64_DEVICE_H

struct dev_archdata {
#ifdef CONFIG_INTEL_IOMMU
	void *iommu; /* hook for IOMMU specific extension */
#endif
};

struct pdev_archdata {
};

#endif /* _ASM_IA64_DEVICE_H */
