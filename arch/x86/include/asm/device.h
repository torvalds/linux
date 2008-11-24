#ifndef _ASM_X86_DEVICE_H
#define _ASM_X86_DEVICE_H

struct dev_archdata {
#ifdef CONFIG_ACPI
	void	*acpi_handle;
#endif
#ifdef CONFIG_X86_64
struct dma_mapping_ops *dma_ops;
#endif
#ifdef CONFIG_DMAR
	void *iommu; /* hook for IOMMU specific extension */
#endif
};

#endif /* _ASM_X86_DEVICE_H */
