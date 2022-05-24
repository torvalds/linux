/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	Low-Level PCI Access for i386 machines.
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/spinlock.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(fmt, ...) printk(fmt, ##__VA_ARGS__)
#else
#define DBG(fmt, ...)				\
do {						\
	if (0)					\
		printk(fmt, ##__VA_ARGS__);	\
} while (0)
#endif

#define PCI_PROBE_BIOS		0x0001
#define PCI_PROBE_CONF1		0x0002
#define PCI_PROBE_CONF2		0x0004
#define PCI_PROBE_MMCONF	0x0008
#define PCI_PROBE_MASK		0x000f
#define PCI_PROBE_NOEARLY	0x0010

#define PCI_NO_CHECKS		0x0400
#define PCI_USE_PIRQ_MASK	0x0800
#define PCI_ASSIGN_ROMS		0x1000
#define PCI_BIOS_IRQ_SCAN	0x2000
#define PCI_ASSIGN_ALL_BUSSES	0x4000
#define PCI_CAN_SKIP_ISA_ALIGN	0x8000
#define PCI_USE__CRS		0x10000
#define PCI_CHECK_ENABLE_AMD_MMCONF	0x20000
#define PCI_HAS_IO_ECS		0x40000
#define PCI_NOASSIGN_ROMS	0x80000
#define PCI_ROOT_NO_CRS		0x100000
#define PCI_NOASSIGN_BARS	0x200000
#define PCI_BIG_ROOT_WINDOW	0x400000

extern unsigned int pci_probe;
extern unsigned long pirq_table_addr;

enum pci_bf_sort_state {
	pci_bf_sort_default,
	pci_force_nobf,
	pci_force_bf,
	pci_dmi_bf,
};

/* pci-i386.c */

void pcibios_resource_survey(void);
void pcibios_set_cache_line_size(void);

/* pci-pc.c */

extern int pcibios_last_bus;
extern struct pci_ops pci_root_ops;

void pcibios_scan_specific_bus(int busn);

/* pci-irq.c */

struct irq_info {
	u8 bus, devfn;			/* Bus, device and function */
	struct {
		u8 link;		/* IRQ line ID, chipset dependent,
					   0 = not routed */
		u16 bitmap;		/* Available IRQs */
	} __attribute__((packed)) irq[4];
	u8 slot;			/* Slot number, 0=onboard */
	u8 rfu;
} __attribute__((packed));

struct irq_routing_table {
	u32 signature;			/* PIRQ_SIGNATURE should be here */
	u16 version;			/* PIRQ_VERSION */
	u16 size;			/* Table size in bytes */
	u8 rtr_bus, rtr_devfn;		/* Where the interrupt router lies */
	u16 exclusive_irqs;		/* IRQs devoted exclusively to
					   PCI usage */
	u16 rtr_vendor, rtr_device;	/* Vendor and device ID of
					   interrupt router */
	u32 miniport_data;		/* Crap */
	u8 rfu[11];
	u8 checksum;			/* Modulo 256 checksum must give 0 */
	struct irq_info slots[];
} __attribute__((packed));

extern unsigned int pcibios_irq_mask;

extern raw_spinlock_t pci_config_lock;

extern int (*pcibios_enable_irq)(struct pci_dev *dev);
extern void (*pcibios_disable_irq)(struct pci_dev *dev);

extern bool mp_should_keep_irq(struct device *dev);

struct pci_raw_ops {
	int (*read)(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 *val);
	int (*write)(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 val);
};

extern const struct pci_raw_ops *raw_pci_ops;
extern const struct pci_raw_ops *raw_pci_ext_ops;

extern const struct pci_raw_ops pci_mmcfg;
extern const struct pci_raw_ops pci_direct_conf1;
extern bool port_cf9_safe;

/* arch_initcall level */
#ifdef CONFIG_PCI_DIRECT
extern int pci_direct_probe(void);
extern void pci_direct_init(int type);
#else
static inline int pci_direct_probe(void) { return -1; }
static inline  void pci_direct_init(int type) { }
#endif

#ifdef CONFIG_PCI_BIOS
extern void pci_pcbios_init(void);
#else
static inline void pci_pcbios_init(void) { }
#endif

extern void __init dmi_check_pciprobe(void);
extern void __init dmi_check_skip_isa_align(void);

/* some common used subsys_initcalls */
#ifdef CONFIG_PCI
extern int __init pci_acpi_init(void);
#else
static inline int  __init pci_acpi_init(void)
{
	return -EINVAL;
}
#endif
extern void __init pcibios_irq_init(void);
extern int __init pcibios_init(void);
extern int pci_legacy_init(void);
extern void pcibios_fixup_irqs(void);

/* pci-mmconfig.c */

/* "PCI MMCONFIG %04x [bus %02x-%02x]" */
#define PCI_MMCFG_RESOURCE_NAME_LEN (22 + 4 + 2 + 2)

struct pci_mmcfg_region {
	struct list_head list;
	struct resource res;
	u64 address;
	char __iomem *virt;
	u16 segment;
	u8 start_bus;
	u8 end_bus;
	char name[PCI_MMCFG_RESOURCE_NAME_LEN];
};

extern int __init pci_mmcfg_arch_init(void);
extern void __init pci_mmcfg_arch_free(void);
extern int pci_mmcfg_arch_map(struct pci_mmcfg_region *cfg);
extern void pci_mmcfg_arch_unmap(struct pci_mmcfg_region *cfg);
extern int pci_mmconfig_insert(struct device *dev, u16 seg, u8 start, u8 end,
			       phys_addr_t addr);
extern int pci_mmconfig_delete(u16 seg, u8 start, u8 end);
extern struct pci_mmcfg_region *pci_mmconfig_lookup(int segment, int bus);
extern struct pci_mmcfg_region *__init pci_mmconfig_add(int segment, int start,
							int end, u64 addr);

extern struct list_head pci_mmcfg_list;

#define PCI_MMCFG_BUS_OFFSET(bus)      ((bus) << 20)

/*
 * On AMD Fam10h CPUs, all PCI MMIO configuration space accesses must use
 * %eax.  No other source or target registers may be used.  The following
 * mmio_config_* accessors enforce this.  See "BIOS and Kernel Developer's
 * Guide (BKDG) For AMD Family 10h Processors", rev. 3.48, sec 2.11.1,
 * "MMIO Configuration Coding Requirements".
 */
static inline unsigned char mmio_config_readb(void __iomem *pos)
{
	u8 val;
	asm volatile("movb (%1),%%al" : "=a" (val) : "r" (pos));
	return val;
}

static inline unsigned short mmio_config_readw(void __iomem *pos)
{
	u16 val;
	asm volatile("movw (%1),%%ax" : "=a" (val) : "r" (pos));
	return val;
}

static inline unsigned int mmio_config_readl(void __iomem *pos)
{
	u32 val;
	asm volatile("movl (%1),%%eax" : "=a" (val) : "r" (pos));
	return val;
}

static inline void mmio_config_writeb(void __iomem *pos, u8 val)
{
	asm volatile("movb %%al,(%1)" : : "a" (val), "r" (pos) : "memory");
}

static inline void mmio_config_writew(void __iomem *pos, u16 val)
{
	asm volatile("movw %%ax,(%1)" : : "a" (val), "r" (pos) : "memory");
}

static inline void mmio_config_writel(void __iomem *pos, u32 val)
{
	asm volatile("movl %%eax,(%1)" : : "a" (val), "r" (pos) : "memory");
}

#ifdef CONFIG_PCI
# ifdef CONFIG_ACPI
#  define x86_default_pci_init		pci_acpi_init
# else
#  define x86_default_pci_init		pci_legacy_init
# endif
# define x86_default_pci_init_irq	pcibios_irq_init
# define x86_default_pci_fixup_irqs	pcibios_fixup_irqs
#else
# define x86_default_pci_init		NULL
# define x86_default_pci_init_irq	NULL
# define x86_default_pci_fixup_irqs	NULL
#endif
