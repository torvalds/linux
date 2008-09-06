#ifndef _DMA_REMAPPING_H
#define _DMA_REMAPPING_H

/*
 * We need a fixed PAGE_SIZE of 4K irrespective of
 * arch PAGE_SIZE for IOMMU page tables.
 */
#define PAGE_SHIFT_4K		(12)
#define PAGE_SIZE_4K		(1UL << PAGE_SHIFT_4K)
#define PAGE_MASK_4K		(((u64)-1) << PAGE_SHIFT_4K)
#define PAGE_ALIGN_4K(addr)	(((addr) + PAGE_SIZE_4K - 1) & PAGE_MASK_4K)

#define IOVA_PFN(addr)		((addr) >> PAGE_SHIFT_4K)
#define DMA_32BIT_PFN		IOVA_PFN(DMA_32BIT_MASK)
#define DMA_64BIT_PFN		IOVA_PFN(DMA_64BIT_MASK)


/*
 * 0: Present
 * 1-11: Reserved
 * 12-63: Context Ptr (12 - (haw-1))
 * 64-127: Reserved
 */
struct root_entry {
	u64	val;
	u64	rsvd1;
};
#define ROOT_ENTRY_NR (PAGE_SIZE_4K/sizeof(struct root_entry))
static inline bool root_present(struct root_entry *root)
{
	return (root->val & 1);
}
static inline void set_root_present(struct root_entry *root)
{
	root->val |= 1;
}
static inline void set_root_value(struct root_entry *root, unsigned long value)
{
	root->val |= value & PAGE_MASK_4K;
}

struct context_entry;
static inline struct context_entry *
get_context_addr_from_root(struct root_entry *root)
{
	return (struct context_entry *)
		(root_present(root)?phys_to_virt(
		root->val & PAGE_MASK_4K):
		NULL);
}

/*
 * low 64 bits:
 * 0: present
 * 1: fault processing disable
 * 2-3: translation type
 * 12-63: address space root
 * high 64 bits:
 * 0-2: address width
 * 3-6: aval
 * 8-23: domain id
 */
struct context_entry {
	u64 lo;
	u64 hi;
};
#define context_present(c) ((c).lo & 1)
#define context_fault_disable(c) (((c).lo >> 1) & 1)
#define context_translation_type(c) (((c).lo >> 2) & 3)
#define context_address_root(c) ((c).lo & PAGE_MASK_4K)
#define context_address_width(c) ((c).hi &  7)
#define context_domain_id(c) (((c).hi >> 8) & ((1 << 16) - 1))

#define context_set_present(c) do {(c).lo |= 1;} while (0)
#define context_set_fault_enable(c) \
	do {(c).lo &= (((u64)-1) << 2) | 1;} while (0)
#define context_set_translation_type(c, val) \
	do { \
		(c).lo &= (((u64)-1) << 4) | 3; \
		(c).lo |= ((val) & 3) << 2; \
	} while (0)
#define CONTEXT_TT_MULTI_LEVEL 0
#define context_set_address_root(c, val) \
	do {(c).lo |= (val) & PAGE_MASK_4K;} while (0)
#define context_set_address_width(c, val) do {(c).hi |= (val) & 7;} while (0)
#define context_set_domain_id(c, val) \
	do {(c).hi |= ((val) & ((1 << 16) - 1)) << 8;} while (0)
#define context_clear_entry(c) do {(c).lo = 0; (c).hi = 0;} while (0)

/*
 * 0: readable
 * 1: writable
 * 2-6: reserved
 * 7: super page
 * 8-11: available
 * 12-63: Host physcial address
 */
struct dma_pte {
	u64 val;
};
#define dma_clear_pte(p)	do {(p).val = 0;} while (0)

#define DMA_PTE_READ (1)
#define DMA_PTE_WRITE (2)

#define dma_set_pte_readable(p) do {(p).val |= DMA_PTE_READ;} while (0)
#define dma_set_pte_writable(p) do {(p).val |= DMA_PTE_WRITE;} while (0)
#define dma_set_pte_prot(p, prot) \
		do {(p).val = ((p).val & ~3) | ((prot) & 3); } while (0)
#define dma_pte_addr(p) ((p).val & PAGE_MASK_4K)
#define dma_set_pte_addr(p, addr) do {\
		(p).val |= ((addr) & PAGE_MASK_4K); } while (0)
#define dma_pte_present(p) (((p).val & 3) != 0)

struct intel_iommu;

struct dmar_domain {
	int	id;			/* domain id */
	struct intel_iommu *iommu;	/* back pointer to owning iommu */

	struct list_head devices; 	/* all devices' list */
	struct iova_domain iovad;	/* iova's that belong to this domain */

	struct dma_pte	*pgd;		/* virtual address */
	spinlock_t	mapping_lock;	/* page table lock */
	int		gaw;		/* max guest address width */

	/* adjusted guest address width, 0 is level 2 30-bit */
	int		agaw;

#define DOMAIN_FLAG_MULTIPLE_DEVICES 1
	int		flags;
};

/* PCI domain-device relationship */
struct device_domain_info {
	struct list_head link;	/* link to domain siblings */
	struct list_head global; /* link to global list */
	u8 bus;			/* PCI bus numer */
	u8 devfn;		/* PCI devfn number */
	struct pci_dev *dev; /* it's NULL for PCIE-to-PCI bridge */
	struct dmar_domain *domain; /* pointer to domain */
};

extern int init_dmars(void);
extern void free_dmar_iommu(struct intel_iommu *iommu);

extern int dmar_disabled;

#ifndef CONFIG_DMAR_GFX_WA
static inline void iommu_prepare_gfx_mapping(void)
{
	return;
}
#endif /* !CONFIG_DMAR_GFX_WA */

#endif
