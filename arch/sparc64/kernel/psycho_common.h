#ifndef _PSYCHO_COMMON_H
#define _PSYCHO_COMMON_H

extern int psycho_iommu_init(struct pci_pbm_info *pbm, int tsbsize,
			     u32 dvma_offset, u32 dma_mask,
			     unsigned long write_complete_offset);

#endif /* _PSYCHO_COMMON_H */
