/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Arch specific extensions to struct device
 */
#ifndef _ASM_POWERPC_DEVICE_H
#define _ASM_POWERPC_DEVICE_H

struct device_node;
#ifdef CONFIG_PPC64
struct pci_dn;
struct iommu_table;
#endif

/*
 * Arch extensions to struct device.
 *
 * When adding fields, consider macio_add_one_device in
 * drivers/macintosh/macio_asic.c
 */
struct dev_archdata {
	/*
	 * These two used to be a union. However, with the hybrid ops we need
	 * both so here we store both a DMA offset for direct mappings and
	 * an iommu_table for remapped DMA.
	 */
	dma_addr_t		dma_offset;

#ifdef CONFIG_PPC64
	struct iommu_table	*iommu_table_base;
#endif

#ifdef CONFIG_PPC64
	struct pci_dn		*pci_data;
#endif
#ifdef CONFIG_EEH
	struct eeh_dev		*edev;
#endif
#ifdef CONFIG_FAIL_IOMMU
	int fail_iommu;
#endif
#ifdef CONFIG_CXL_BASE
	struct cxl_context	*cxl_ctx;
#endif
#ifdef CONFIG_PCI_IOV
	void *iov_data;
#endif
};

struct pdev_archdata {
	u64 dma_mask;
	/*
	 * Pointer to nvdimm_pmu structure, to handle the unregistering
	 * of pmu device
	 */
	void *priv;
};

#endif /* _ASM_POWERPC_DEVICE_H */
