#ifndef _ASM_X86_IOMMU_H
#define _ASM_X86_IOMMU_H

extern void pci_iommu_shutdown(void);
extern void no_iommu_init(void);
extern struct dma_map_ops nommu_dma_ops;
extern int force_iommu, no_iommu;
extern int iommu_detected;

/* 10 seconds */
#define DMAR_OPERATION_TIMEOUT ((cycles_t) tsc_khz*10*1000)

#endif /* _ASM_X86_IOMMU_H */
