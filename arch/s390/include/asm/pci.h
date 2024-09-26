/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_S390_PCI_H
#define __ASM_S390_PCI_H

#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/iommu.h>
#include <linux/pci_hotplug.h>
#include <asm/pci_clp.h>
#include <asm/pci_debug.h>
#include <asm/pci_insn.h>
#include <asm/sclp.h>

#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		0x10000000

#define pcibios_assign_all_busses()	(0)

void __iomem *pci_iomap(struct pci_dev *, int, unsigned long);
void pci_iounmap(struct pci_dev *, void __iomem *);
int pci_domain_nr(struct pci_bus *);
int pci_proc_domain(struct pci_bus *);

#define ZPCI_BUS_NR			0	/* default bus number */

#define ZPCI_NR_DMA_SPACES		1
#define ZPCI_NR_DEVICES			CONFIG_PCI_NR_FUNCTIONS
#define ZPCI_DOMAIN_BITMAP_SIZE		(1 << 16)

#ifdef PCI
#if (ZPCI_NR_DEVICES > ZPCI_DOMAIN_BITMAP_SIZE)
# error ZPCI_NR_DEVICES can not be bigger than ZPCI_DOMAIN_BITMAP_SIZE
#endif
#endif /* PCI */

/* PCI Function Controls */
#define ZPCI_FC_FN_ENABLED		0x80
#define ZPCI_FC_ERROR			0x40
#define ZPCI_FC_BLOCKED			0x20
#define ZPCI_FC_DMA_ENABLED		0x10

#define ZPCI_FMB_DMA_COUNTER_VALID	(1 << 23)

struct zpci_fmb_fmt0 {
	u64 dma_rbytes;
	u64 dma_wbytes;
};

struct zpci_fmb_fmt1 {
	u64 rx_bytes;
	u64 rx_packets;
	u64 tx_bytes;
	u64 tx_packets;
};

struct zpci_fmb_fmt2 {
	u64 consumed_work_units;
	u64 max_work_units;
};

struct zpci_fmb_fmt3 {
	u64 tx_bytes;
};

struct zpci_fmb {
	u32 format	: 8;
	u32 fmt_ind	: 24;
	u32 samples;
	u64 last_update;
	/* common counters */
	u64 ld_ops;
	u64 st_ops;
	u64 stb_ops;
	u64 rpcit_ops;
	/* format specific counters */
	union {
		struct zpci_fmb_fmt0 fmt0;
		struct zpci_fmb_fmt1 fmt1;
		struct zpci_fmb_fmt2 fmt2;
		struct zpci_fmb_fmt3 fmt3;
	};
} __packed __aligned(128);

enum zpci_state {
	ZPCI_FN_STATE_STANDBY = 0,
	ZPCI_FN_STATE_CONFIGURED = 1,
	ZPCI_FN_STATE_RESERVED = 2,
};

struct zpci_bar_struct {
	struct resource *res;		/* bus resource */
	void __iomem	*mio_wb;
	void __iomem	*mio_wt;
	u32		val;		/* bar start & 3 flag bits */
	u16		map_idx;	/* index into bar mapping array */
	u8		size;		/* order 2 exponent */
};

struct kvm_zdev;

#define ZPCI_FUNCTIONS_PER_BUS 256
struct zpci_bus {
	struct kref		kref;
	struct pci_bus		*bus;
	struct zpci_dev		*function[ZPCI_FUNCTIONS_PER_BUS];
	struct list_head	resources;
	struct list_head	bus_next;
	struct resource		bus_resource;
	int			topo;		/* TID if topo_is_tid, PCHID otherwise */
	int			domain_nr;
	u8			multifunction	: 1;
	u8			topo_is_tid	: 1;
	enum pci_bus_speed	max_bus_speed;
};

/* Private data per function */
struct zpci_dev {
	struct zpci_bus *zbus;
	struct list_head entry;		/* list of all zpci_devices, needed for hotplug, etc. */
	struct list_head iommu_list;
	struct kref kref;
	struct rcu_head rcu;
	struct hotplug_slot hotplug_slot;

	struct mutex state_lock;	/* protect state changes */
	enum zpci_state state;
	u32		fid;		/* function ID, used by sclp */
	u32		fh;		/* function handle, used by insn's */
	u32		gisa;		/* GISA designation for passthrough */
	u16		vfn;		/* virtual function number */
	u16		pchid;		/* physical channel ID */
	u16		maxstbl;	/* Maximum store block size */
	u16		rid;		/* RID as supplied by firmware */
	u16		tid;		/* Topology for which RID is valid */
	u8		pfgid;		/* function group ID */
	u8		pft;		/* pci function type */
	u8		port;
	u8		dtsm;		/* Supported DT mask */
	u8		rid_available	: 1;
	u8		has_hp_slot	: 1;
	u8		has_resources	: 1;
	u8		is_physfn	: 1;
	u8		util_str_avail	: 1;
	u8		irqs_registered	: 1;
	u8		tid_avail	: 1;
	u8		reserved	: 1;
	unsigned int	devfn;		/* DEVFN part of the RID*/

	u8 pfip[CLP_PFIP_NR_SEGMENTS];	/* pci function internal path */
	u32 uid;			/* user defined id */
	u8 util_str[CLP_UTIL_STR_LEN];	/* utility string */

	/* IRQ stuff */
	u64		msi_addr;	/* MSI address */
	unsigned int	max_msi;	/* maximum number of MSI's */
	unsigned int	msi_first_bit;
	unsigned int	msi_nr_irqs;
	struct airq_iv *aibv;		/* adapter interrupt bit vector */
	unsigned long	aisb;		/* number of the summary bit */

	/* DMA stuff */
	unsigned long	*dma_table;
	int		tlb_refresh;

	struct iommu_device iommu_dev;  /* IOMMU core handle */

	char res_name[16];
	bool mio_capable;
	struct zpci_bar_struct bars[PCI_STD_NUM_BARS];

	u64		start_dma;	/* Start of available DMA addresses */
	u64		end_dma;	/* End of available DMA addresses */
	u64		dma_mask;	/* DMA address space mask */

	/* Function measurement block */
	struct mutex fmb_lock;
	struct zpci_fmb *fmb;
	u16		fmb_update;	/* update interval */
	u16		fmb_length;

	u8		version;
	enum pci_bus_speed max_bus_speed;

	struct dentry	*debugfs_dev;

	/* IOMMU and passthrough */
	struct iommu_domain *s390_domain; /* attached IOMMU domain */
	struct kvm_zdev *kzdev;
	struct mutex kzdev_lock;
	spinlock_t dom_lock;		/* protect s390_domain change */
};

static inline bool zdev_enabled(struct zpci_dev *zdev)
{
	return (zdev->fh & (1UL << 31)) ? true : false;
}

extern const struct attribute_group zpci_attr_group;
extern const struct attribute_group pfip_attr_group;
extern const struct attribute_group zpci_ident_attr_group;

#define ARCH_PCI_DEV_GROUPS &zpci_attr_group,		 \
			    &pfip_attr_group,		 \
			    &zpci_ident_attr_group,

extern unsigned int s390_pci_force_floating __initdata;
extern unsigned int s390_pci_no_rid;

extern union zpci_sic_iib *zpci_aipb;
extern struct airq_iv *zpci_aif_sbv;

/* -----------------------------------------------------------------------------
  Prototypes
----------------------------------------------------------------------------- */
/* Base stuff */
struct zpci_dev *zpci_create_device(u32 fid, u32 fh, enum zpci_state state);
int zpci_add_device(struct zpci_dev *zdev);
int zpci_enable_device(struct zpci_dev *);
int zpci_disable_device(struct zpci_dev *);
int zpci_scan_configured_device(struct zpci_dev *zdev, u32 fh);
int zpci_deconfigure_device(struct zpci_dev *zdev);
void zpci_device_reserved(struct zpci_dev *zdev);
bool zpci_is_device_configured(struct zpci_dev *zdev);
int zpci_scan_devices(void);

int zpci_hot_reset_device(struct zpci_dev *zdev);
int zpci_register_ioat(struct zpci_dev *, u8, u64, u64, u64, u8 *);
int zpci_unregister_ioat(struct zpci_dev *, u8);
void zpci_remove_reserved_devices(void);
void zpci_update_fh(struct zpci_dev *zdev, u32 fh);

/* CLP */
int clp_setup_writeback_mio(void);
int clp_scan_pci_devices(struct list_head *scan_list);
int clp_query_pci_fn(struct zpci_dev *zdev);
int clp_enable_fh(struct zpci_dev *zdev, u32 *fh, u8 nr_dma_as);
int clp_disable_fh(struct zpci_dev *zdev, u32 *fh);
int clp_get_state(u32 fid, enum zpci_state *state);
int clp_refresh_fh(u32 fid, u32 *fh);

/* UID */
void update_uid_checking(bool new);

/* IOMMU Interface */
int zpci_init_iommu(struct zpci_dev *zdev);
void zpci_destroy_iommu(struct zpci_dev *zdev);

#ifdef CONFIG_PCI
static inline bool zpci_use_mio(struct zpci_dev *zdev)
{
	return static_branch_likely(&have_mio) && zdev->mio_capable;
}

/* Error handling and recovery */
void zpci_event_error(void *);
void zpci_event_availability(void *);
bool zpci_is_enabled(void);
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
static inline struct zpci_dev *to_zpci(struct pci_dev *pdev)
{
	struct zpci_bus *zbus = pdev->sysdata;

	return zbus->function[pdev->devfn];
}

static inline struct zpci_dev *to_zpci_dev(struct device *dev)
{
	return to_zpci(to_pci_dev(dev));
}

struct zpci_dev *get_zdev_by_fid(u32);

/* DMA */
int zpci_dma_init(void);
void zpci_dma_exit(void);
int zpci_dma_init_device(struct zpci_dev *zdev);
int zpci_dma_exit_device(struct zpci_dev *zdev);

/* IRQ */
int __init zpci_irq_init(void);
void __init zpci_irq_exit(void);

/* FMB */
int zpci_fmb_enable_device(struct zpci_dev *);
int zpci_fmb_disable_device(struct zpci_dev *);

/* Debug */
int zpci_debug_init(void);
void zpci_debug_exit(void);
void zpci_debug_init_device(struct zpci_dev *, const char *);
void zpci_debug_exit_device(struct zpci_dev *);

/* Error handling */
int zpci_report_error(struct pci_dev *, struct zpci_report_error_header *);
int zpci_clear_error_state(struct zpci_dev *zdev);
int zpci_reset_load_store_blocked(struct zpci_dev *zdev);

#ifdef CONFIG_NUMA

/* Returns the node based on PCI bus */
static inline int __pcibus_to_node(const struct pci_bus *bus)
{
	return NUMA_NO_NODE;
}

static inline const struct cpumask *
cpumask_of_pcibus(const struct pci_bus *bus)
{
	return cpu_online_mask;
}

#endif /* CONFIG_NUMA */

#endif
