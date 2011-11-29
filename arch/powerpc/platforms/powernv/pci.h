#ifndef __POWERNV_PCI_H
#define __POWERNV_PCI_H

struct pci_dn;

enum pnv_phb_type {
	PNV_PHB_P5IOC2,
	PNV_PHB_IODA1,
	PNV_PHB_IODA2,
};

/* Precise PHB model for error management */
enum pnv_phb_model {
	PNV_PHB_MODEL_UNKNOWN,
	PNV_PHB_MODEL_P5IOC2,
	PNV_PHB_MODEL_P7IOC,
};

#define PNV_PCI_DIAG_BUF_SIZE	4096

/* Data associated with a PE, including IOMMU tracking etc.. */
struct pnv_ioda_pe {
	/* A PE can be associated with a single device or an
	 * entire bus (& children). In the former case, pdev
	 * is populated, in the later case, pbus is.
	 */
	struct pci_dev		*pdev;
	struct pci_bus		*pbus;

	/* Effective RID (device RID for a device PE and base bus
	 * RID with devfn 0 for a bus PE)
	 */
	unsigned int		rid;

	/* PE number */
	unsigned int		pe_number;

	/* "Weight" assigned to the PE for the sake of DMA resource
	 * allocations
	 */
	unsigned int		dma_weight;

	/* This is a PCI-E -> PCI-X bridge, this points to the
	 * corresponding bus PE
	 */
	struct pnv_ioda_pe	*bus_pe;

	/* "Base" iommu table, ie, 4K TCEs, 32-bit DMA */
	int			tce32_seg;
	int			tce32_segcount;
	struct iommu_table	tce32_table;

	/* XXX TODO: Add support for additional 64-bit iommus */

	/* MSIs. MVE index is identical for for 32 and 64 bit MSI
	 * and -1 if not supported. (It's actually identical to the
	 * PE number)
	 */
	int			mve_number;

	/* Link in list of PE#s */
	struct list_head	link;
};

struct pnv_phb {
	struct pci_controller	*hose;
	enum pnv_phb_type	type;
	enum pnv_phb_model	model;
	u64			opal_id;
	void __iomem		*regs;
	spinlock_t		lock;

#ifdef CONFIG_PCI_MSI
	unsigned long		*msi_map;
	unsigned int		msi_base;
	unsigned int		msi_count;
	unsigned int		msi_next;
	unsigned int		msi32_support;
#endif
	int (*msi_setup)(struct pnv_phb *phb, struct pci_dev *dev,
			 unsigned int hwirq, unsigned int is_64,
			 struct msi_msg *msg);
	void (*dma_dev_setup)(struct pnv_phb *phb, struct pci_dev *pdev);
	void (*fixup_phb)(struct pci_controller *hose);
	u32 (*bdfn_to_pe)(struct pnv_phb *phb, struct pci_bus *bus, u32 devfn);

	union {
		struct {
			struct iommu_table iommu_table;
		} p5ioc2;

		struct {
			/* Global bridge info */
			unsigned int		total_pe;
			unsigned int		m32_size;
			unsigned int		m32_segsize;
			unsigned int		m32_pci_base;
			unsigned int		io_size;
			unsigned int		io_segsize;
			unsigned int		io_pci_base;

			/* PE allocation bitmap */
			unsigned long		*pe_alloc;

			/* M32 & IO segment maps */
			unsigned int		*m32_segmap;
			unsigned int		*io_segmap;
			struct pnv_ioda_pe	*pe_array;

			/* Reverse map of PEs, will have to extend if
			 * we are to support more than 256 PEs, indexed
			 * bus { bus, devfn }
			 */
			unsigned char		pe_rmap[0x10000];

			/* 32-bit TCE tables allocation */
			unsigned long		tce32_count;

			/* Total "weight" for the sake of DMA resources
			 * allocation
			 */
			unsigned int		dma_weight;
			unsigned int		dma_pe_count;

			/* Sorted list of used PE's, sorted at
			 * boot for resource allocation purposes
			 */
			struct list_head	pe_list;
		} ioda;
	};

	/* PHB status structure */
	union {
		unsigned char			blob[PNV_PCI_DIAG_BUF_SIZE];
		struct OpalIoP7IOCPhbErrorData	p7ioc;
	} diag;
};

extern struct pci_ops pnv_pci_ops;

extern void pnv_pci_setup_iommu_table(struct iommu_table *tbl,
				      void *tce_mem, u64 tce_size,
				      u64 dma_offset);
extern void pnv_pci_init_p5ioc2_hub(struct device_node *np);
extern void pnv_pci_init_ioda_hub(struct device_node *np);


#endif /* __POWERNV_PCI_H */
