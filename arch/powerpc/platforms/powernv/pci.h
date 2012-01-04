#ifndef __POWERNV_PCI_H
#define __POWERNV_PCI_H

struct pci_dn;

enum pnv_phb_type {
	PNV_PHB_P5IOC2,
	PNV_PHB_IODA1,
	PNV_PHB_IODA2,
};

struct pnv_phb {
	struct pci_controller	*hose;
	enum pnv_phb_type	type;
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
	};
};

extern struct pci_ops pnv_pci_ops;

extern void pnv_pci_setup_iommu_table(struct iommu_table *tbl,
				      void *tce_mem, u64 tce_size,
				      u64 dma_offset);
extern void pnv_pci_init_p5ioc2_hub(struct device_node *np);


#endif /* __POWERNV_PCI_H */
