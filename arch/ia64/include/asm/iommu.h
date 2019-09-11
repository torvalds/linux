/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_IOMMU_H
#define _ASM_IA64_IOMMU_H 1

/* 10 seconds */
#define DMAR_OPERATION_TIMEOUT (((cycles_t) local_cpu_data->itc_freq)*10)

extern void no_iommu_init(void);
#ifdef	CONFIG_INTEL_IOMMU
extern int force_iommu, no_iommu;
extern int iommu_detected;
#else
#define no_iommu		(1)
#define iommu_detected		(0)
#endif
extern void machvec_init(const char *name);

#endif
