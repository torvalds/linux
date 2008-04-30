/*
 *	Low-Level PCI Access for i386 machines.
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 */

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
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

extern unsigned int pci_probe;
extern unsigned long pirq_table_addr;

enum pci_bf_sort_state {
	pci_bf_sort_default,
	pci_force_nobf,
	pci_force_bf,
	pci_dmi_bf,
};

/* pci-i386.c */

extern unsigned int pcibios_max_latency;

void pcibios_resource_survey(void);

/* pci-pc.c */

extern int pcibios_last_bus;
extern struct pci_bus *pci_root_bus;
extern struct pci_ops pci_root_ops;

/* pci-irq.c */

struct irq_info {
	u8 bus, devfn;			/* Bus, device and function */
	struct {
		u8 link;		/* IRQ line ID, chipset dependent, 0=not routed */
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
	u16 exclusive_irqs;		/* IRQs devoted exclusively to PCI usage */
	u16 rtr_vendor, rtr_device;	/* Vendor and device ID of interrupt router */
	u32 miniport_data;		/* Crap */
	u8 rfu[11];
	u8 checksum;			/* Modulo 256 checksum must give zero */
	struct irq_info slots[0];
} __attribute__((packed));

extern unsigned int pcibios_irq_mask;

extern int pcibios_scanned;
extern spinlock_t pci_config_lock;

extern int (*pcibios_enable_irq)(struct pci_dev *dev);
extern void (*pcibios_disable_irq)(struct pci_dev *dev);

struct pci_raw_ops {
	int (*read)(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 *val);
	int (*write)(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 val);
};

extern struct pci_raw_ops *raw_pci_ops;
extern struct pci_raw_ops *raw_pci_ext_ops;

extern struct pci_raw_ops pci_direct_conf1;

extern int pci_direct_probe(void);
extern void pci_direct_init(int type);
extern void pci_pcbios_init(void);
extern void pci_mmcfg_init(int type);

/* pci-mmconfig.c */

extern int __init pci_mmcfg_arch_init(void);

/*
 * AMD Fam10h CPUs are buggy, and cannot access MMIO config space
 * on their northbrige except through the * %eax register. As such, you MUST
 * NOT use normal IOMEM accesses, you need to only use the magic mmio-config
 * accessor functions.
 * In fact just use pci_config_*, nothing else please.
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
	asm volatile("movb %%al,(%1)" :: "a" (val), "r" (pos) : "memory");
}

static inline void mmio_config_writew(void __iomem *pos, u16 val)
{
	asm volatile("movw %%ax,(%1)" :: "a" (val), "r" (pos) : "memory");
}

static inline void mmio_config_writel(void __iomem *pos, u32 val)
{
	asm volatile("movl %%eax,(%1)" :: "a" (val), "r" (pos) : "memory");
}
