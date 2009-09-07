#ifndef _ASM_IA64_IOMMU_H
#define _ASM_IA64_IOMMU_H 1

#define cpu_has_x2apic 0
/* 10 seconds */
#define DMAR_OPERATION_TIMEOUT (((cycles_t) local_cpu_data->itc_freq)*10)

extern void pci_iommu_shutdown(void);
extern void no_iommu_init(void);
extern int force_iommu, no_iommu;
extern int iommu_detected;
#ifdef	CONFIG_DMAR
extern int iommu_pass_through;
#else
#define iommu_pass_through	(0)
#endif
extern void iommu_dma_init(void);
extern void machvec_init(const char *name);

#endif
