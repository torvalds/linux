#ifndef _PSYCHO_COMMON_H
#define _PSYCHO_COMMON_H

extern int psycho_iommu_init(struct pci_pbm_info *pbm, int tsbsize,
			     u32 dvma_offset, u32 dma_mask,
			     unsigned long write_complete_offset);

extern void psycho_pbm_init_common(struct pci_pbm_info *pbm,
				   struct of_device *op,
				   const char *chip_name, int chip_type);

#endif /* _PSYCHO_COMMON_H */
