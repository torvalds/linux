#ifndef __ASM_S390_PCI_H
#define __ASM_S390_PCI_H

/* must be set before including asm-generic/pci.h */
#define PCI_DMA_BUS_IS_PHYS (0)
/* must be set before including pci_clp.h */
#define PCI_BAR_COUNT	6

#include <asm-generic/pci.h>
#include <asm-generic/pci-dma-compat.h>
#include <asm/pci_clp.h>
#include <asm/pci_debug.h>

#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		0x10000000

#define pcibios_assign_all_busses()	(0)

void __iomem *pci_iomap(struct pci_dev *, int, unsigned long);
void pci_iounmap(struct pci_dev *, void __iomem *);
int pci_domain_nr(struct pci_bus *);
int pci_proc_domain(struct pci_bus *);

/* MSI arch hooks */
#define arch_setup_msi_irqs	arch_setup_msi_irqs
#define arch_teardown_msi_irqs	arch_teardown_msi_irqs

#define ZPCI_BUS_NR			0	/* default bus number */
#define ZPCI_DEVFN			0	/* default device number */

/* PCI Function Controls */
#define ZPCI_FC_FN_ENABLED		0x80
#define ZPCI_FC_ERROR			0x40
#define ZPCI_FC_BLOCKED			0x20
#define ZPCI_FC_DMA_ENABLED		0x10

struct zpci_fmb {
	u32 format	:  8;
	u32 dma_valid	:  1;
	u32		: 23;
	u32 samples;
	u64 last_update;
	/* hardware counters */
	u64 ld_ops;
	u64 st_ops;
	u64 stb_ops;
	u64 rpcit_ops;
	u64 dma_rbytes;
	u64 dma_wbytes;
	/* software counters */
	atomic64_t allocated_pages;
	atomic64_t mapped_pages;
	atomic64_t unmapped_pages;
} __packed __aligned(16);

#define ZPCI_MSI_VEC_BITS	11
#define ZPCI_MSI_VEC_MAX	(1 << ZPCI_MSI_VEC_BITS)
#define ZPCI_MSI_VEC_MASK	(ZPCI_MSI_VEC_MAX - 1)

enum zpci_state {
	ZPCI_FN_STATE_RESERVED,
	ZPCI_FN_STATE_STANDBY,
	ZPCI_FN_STATE_CONFIGURED,
	ZPCI_FN_STATE_ONLINE,
	NR_ZPCI_FN_STATES,
};

struct zpci_bar_struct {
	u32		val;		/* bar start & 3 flag bits */
	u8		size;		/* order 2 exponent */
	u16		map_idx;	/* index into bar mapping array */
};

/* Private data per function */
struct zpci_dev {
	struct pci_dev	*pdev;
	struct pci_bus	*bus;
	struct list_head entry;		/* list of all zpci_devices, needed for hotplug, etc. */

	enum zpci_state state;
	u32		fid;		/* function ID, used by sclp */
	u32		fh;		/* function handle, used by insn's */
	u16		pchid;		/* physical channel ID */
	u8		pfgid;		/* function group ID */
	u16		domain;

	/* IRQ stuff */
	u64		msi_addr;	/* MSI address */
	struct airq_iv *aibv;		/* adapter interrupt bit vector */
	unsigned int	aisb;		/* number of the summary bit */

	/* DMA stuff */
	unsigned long	*dma_table;
	spinlock_t	dma_table_lock;
	int		tlb_refresh;

	spinlock_t	iommu_bitmap_lock;
	unsigned long	*iommu_bitmap;
	unsigned long	iommu_size;
	unsigned long	iommu_pages;
	unsigned int	next_bit;

	struct zpci_bar_struct bars[PCI_BAR_COUNT];

	u64		start_dma;	/* Start of available DMA addresses */
	u64		end_dma;	/* End of available DMA addresses */
	u64		dma_mask;	/* DMA address space mask */

	/* Function measurement block */
	struct zpci_fmb *fmb;
	u16		fmb_update;	/* update interval */

	enum pci_bus_speed max_bus_speed;

	struct dentry	*debugfs_dev;
	struct dentry	*debugfs_perf;
};

static inline bool zdev_enabled(struct zpci_dev *zdev)
{
	return (zdev->fh & (1UL << 31)) ? true : false;
}

/* -----------------------------------------------------------------------------
  Prototypes
----------------------------------------------------------------------------- */
/* Base stuff */
struct zpci_dev *zpci_alloc_device(void);
int zpci_create_device(struct zpci_dev *);
int zpci_enable_device(struct zpci_dev *);
int zpci_disable_device(struct zpci_dev *);
void zpci_stop_device(struct zpci_dev *);
void zpci_free_device(struct zpci_dev *);
int zpci_register_ioat(struct zpci_dev *, u8, u64, u64, u64);
int zpci_unregister_ioat(struct zpci_dev *, u8);

/* CLP */
int clp_scan_pci_devices(void);
int clp_rescan_pci_devices(void);
int clp_add_pci_device(u32, u32, int);
int clp_enable_fh(struct zpci_dev *, u8);
int clp_disable_fh(struct zpci_dev *);

#ifdef CONFIG_PCI
/* Error handling and recovery */
void zpci_event_error(void *);
void zpci_event_availability(void *);
#else /* CONFIG_PCI */
static inline void zpci_event_error(void *e) {}
static inline void zpci_event_availability(void *e) {}
#endif /* CONFIG_PCI */

#ifdef CONFIG_HOTPLUG_PCI_S390
int zpci_init_slot(struct zpci_dev *);
void zpci_exit_slot(struct zpci_dev *);
#else /* CONFIG_HOTPLUG_PCI_S390 */
static inline int zpci_init_slot(struct zpci_dev *zdev)
{
	return 0;
}
static inline void zpci_exit_slot(struct zpci_dev *zdev) {}
#endif /* CONFIG_HOTPLUG_PCI_S390 */

/* Helpers */
struct zpci_dev *get_zdev(struct pci_dev *);
struct zpci_dev *get_zdev_by_fid(u32);

/* sysfs */
int zpci_sysfs_add_device(struct device *);
void zpci_sysfs_remove_device(struct device *);

/* DMA */
int zpci_dma_init(void);
void zpci_dma_exit(void);

/* FMB */
int zpci_fmb_enable_device(struct zpci_dev *);
int zpci_fmb_disable_device(struct zpci_dev *);

/* Debug */
int zpci_debug_init(void);
void zpci_debug_exit(void);
void zpci_debug_init_device(struct zpci_dev *);
void zpci_debug_exit_device(struct zpci_dev *);
void zpci_debug_info(struct zpci_dev *, struct seq_file *);

#endif
