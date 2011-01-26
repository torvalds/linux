/*
 *	Low-Level PCI Support for PC
 *
 *	(c) 1999--2000 Martin Mares <mj@ucw.cz>
 */

#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <linux/slab.h>

#include <asm/acpi.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/pci_x86.h>

unsigned int pci_probe = PCI_PROBE_BIOS | PCI_PROBE_CONF1 | PCI_PROBE_CONF2 |
				PCI_PROBE_MMCONF;

unsigned int pci_early_dump_regs;
static int pci_bf_sort;
static int smbios_type_b1_flag;
int pci_routeirq;
int noioapicquirk;
#ifdef CONFIG_X86_REROUTE_FOR_BROKEN_BOOT_IRQS
int noioapicreroute = 0;
#else
int noioapicreroute = 1;
#endif
int pcibios_last_bus = -1;
unsigned long pirq_table_addr;
struct pci_bus *pci_root_bus;
struct pci_raw_ops *raw_pci_ops;
struct pci_raw_ops *raw_pci_ext_ops;

int raw_pci_read(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 *val)
{
	if (domain == 0 && reg < 256 && raw_pci_ops)
		return raw_pci_ops->read(domain, bus, devfn, reg, len, val);
	if (raw_pci_ext_ops)
		return raw_pci_ext_ops->read(domain, bus, devfn, reg, len, val);
	return -EINVAL;
}

int raw_pci_write(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 val)
{
	if (domain == 0 && reg < 256 && raw_pci_ops)
		return raw_pci_ops->write(domain, bus, devfn, reg, len, val);
	if (raw_pci_ext_ops)
		return raw_pci_ext_ops->write(domain, bus, devfn, reg, len, val);
	return -EINVAL;
}

static int pci_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *value)
{
	return raw_pci_read(pci_domain_nr(bus), bus->number,
				 devfn, where, size, value);
}

static int pci_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 value)
{
	return raw_pci_write(pci_domain_nr(bus), bus->number,
				  devfn, where, size, value);
}

struct pci_ops pci_root_ops = {
	.read = pci_read,
	.write = pci_write,
};

/*
 * This interrupt-safe spinlock protects all accesses to PCI
 * configuration space.
 */
DEFINE_RAW_SPINLOCK(pci_config_lock);

static int __devinit can_skip_ioresource_align(const struct dmi_system_id *d)
{
	pci_probe |= PCI_CAN_SKIP_ISA_ALIGN;
	printk(KERN_INFO "PCI: %s detected, can skip ISA alignment\n", d->ident);
	return 0;
}

static const struct dmi_system_id can_skip_pciprobe_dmi_table[] __devinitconst = {
/*
 * Systems where PCI IO resource ISA alignment can be skipped
 * when the ISA enable bit in the bridge control is not set
 */
	{
		.callback = can_skip_ioresource_align,
		.ident = "IBM System x3800",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IBM"),
			DMI_MATCH(DMI_PRODUCT_NAME, "x3800"),
		},
	},
	{
		.callback = can_skip_ioresource_align,
		.ident = "IBM System x3850",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IBM"),
			DMI_MATCH(DMI_PRODUCT_NAME, "x3850"),
		},
	},
	{
		.callback = can_skip_ioresource_align,
		.ident = "IBM System x3950",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IBM"),
			DMI_MATCH(DMI_PRODUCT_NAME, "x3950"),
		},
	},
	{}
};

void __init dmi_check_skip_isa_align(void)
{
	dmi_check_system(can_skip_pciprobe_dmi_table);
}

static void __devinit pcibios_fixup_device_resources(struct pci_dev *dev)
{
	struct resource *rom_r = &dev->resource[PCI_ROM_RESOURCE];
	struct resource *bar_r;
	int bar;

	if (pci_probe & PCI_NOASSIGN_BARS) {
		/*
		* If the BIOS did not assign the BAR, zero out the
		* resource so the kernel doesn't attmept to assign
		* it later on in pci_assign_unassigned_resources
		*/
		for (bar = 0; bar <= PCI_STD_RESOURCE_END; bar++) {
			bar_r = &dev->resource[bar];
			if (bar_r->start == 0 && bar_r->end != 0) {
				bar_r->flags = 0;
				bar_r->end = 0;
			}
		}
	}

	if (pci_probe & PCI_NOASSIGN_ROMS) {
		if (rom_r->parent)
			return;
		if (rom_r->start) {
			/* we deal with BIOS assigned ROM later */
			return;
		}
		rom_r->start = rom_r->end = rom_r->flags = 0;
	}
}

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */

void __devinit pcibios_fixup_bus(struct pci_bus *b)
{
	struct pci_dev *dev;

	/* root bus? */
	if (!b->parent)
		x86_pci_root_bus_res_quirks(b);
	pci_read_bridge_bases(b);
	list_for_each_entry(dev, &b->devices, bus_list)
		pcibios_fixup_device_resources(dev);
}

/*
 * Only use DMI information to set this if nothing was passed
 * on the kernel command line (which was parsed earlier).
 */

static int __devinit set_bf_sort(const struct dmi_system_id *d)
{
	if (pci_bf_sort == pci_bf_sort_default) {
		pci_bf_sort = pci_dmi_bf;
		printk(KERN_INFO "PCI: %s detected, enabling pci=bfsort.\n", d->ident);
	}
	return 0;
}

static void __devinit read_dmi_type_b1(const struct dmi_header *dm,
				       void *private_data)
{
	u8 *d = (u8 *)dm + 4;

	if (dm->type != 0xB1)
		return;
	switch (((*(u32 *)d) >> 9) & 0x03) {
	case 0x00:
		printk(KERN_INFO "dmi type 0xB1 record - unknown flag\n");
		break;
	case 0x01: /* set pci=bfsort */
		smbios_type_b1_flag = 1;
		break;
	case 0x02: /* do not set pci=bfsort */
		smbios_type_b1_flag = 2;
		break;
	default:
		break;
	}
}

static int __devinit find_sort_method(const struct dmi_system_id *d)
{
	dmi_walk(read_dmi_type_b1, NULL);

	if (smbios_type_b1_flag == 1) {
		set_bf_sort(d);
		return 0;
	}
	return -1;
}

/*
 * Enable renumbering of PCI bus# ranges to reach all PCI busses (Cardbus)
 */
#ifdef __i386__
static int __devinit assign_all_busses(const struct dmi_system_id *d)
{
	pci_probe |= PCI_ASSIGN_ALL_BUSSES;
	printk(KERN_INFO "%s detected: enabling PCI bus# renumbering"
			" (pci=assign-busses)\n", d->ident);
	return 0;
}
#endif

static const struct dmi_system_id __devinitconst pciprobe_dmi_table[] = {
#ifdef __i386__
/*
 * Laptops which need pci=assign-busses to see Cardbus cards
 */
	{
		.callback = assign_all_busses,
		.ident = "Samsung X20 Laptop",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Samsung Electronics"),
			DMI_MATCH(DMI_PRODUCT_NAME, "SX20S"),
		},
	},
#endif		/* __i386__ */
	{
		.callback = find_sort_method,
		.ident = "Dell System",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "Dell PowerEdge 1950",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 1950"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "Dell PowerEdge 1955",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 1955"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "Dell PowerEdge 2900",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 2900"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "Dell PowerEdge 2950",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 2950"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "Dell PowerEdge R900",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge R900"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL20p G3",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL20p G3"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL20p G4",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL20p G4"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL30p G1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL30p G1"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL25p G1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL25p G1"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL35p G1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL35p G1"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL45p G1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL45p G1"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL45p G2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL45p G2"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL460c G1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL460c G1"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL465c G1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL465c G1"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL480c G1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL480c G1"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant BL685c G1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant BL685c G1"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant DL360",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant DL360"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant DL380",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant DL380"),
		},
	},
#ifdef __i386__
	{
		.callback = assign_all_busses,
		.ident = "Compaq EVO N800c",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Compaq"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EVO N800c"),
		},
	},
#endif
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant DL385 G2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant DL385 G2"),
		},
	},
	{
		.callback = set_bf_sort,
		.ident = "HP ProLiant DL585 G2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ProLiant DL585 G2"),
		},
	},
	{}
};

void __init dmi_check_pciprobe(void)
{
	dmi_check_system(pciprobe_dmi_table);
}

struct pci_bus * __devinit pcibios_scan_root(int busnum)
{
	struct pci_bus *bus = NULL;
	struct pci_sysdata *sd;

	while ((bus = pci_find_next_bus(bus)) != NULL) {
		if (bus->number == busnum) {
			/* Already scanned */
			return bus;
		}
	}

	/* Allocate per-root-bus (not per bus) arch-specific data.
	 * TODO: leak; this memory is never freed.
	 * It's arguable whether it's worth the trouble to care.
	 */
	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd) {
		printk(KERN_ERR "PCI: OOM, not probing PCI bus %02x\n", busnum);
		return NULL;
	}

	sd->node = get_mp_bus_to_node(busnum);

	printk(KERN_DEBUG "PCI: Probing PCI hardware (bus %02x)\n", busnum);
	bus = pci_scan_bus_parented(NULL, busnum, &pci_root_ops, sd);
	if (!bus)
		kfree(sd);

	return bus;
}
void __init pcibios_set_cache_line_size(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	/*
	 * Set PCI cacheline size to that of the CPU if the CPU has reported it.
	 * (For older CPUs that don't support cpuid, we se it to 32 bytes
	 * It's also good for 386/486s (which actually have 16)
	 * as quite a few PCI devices do not support smaller values.
	 */
	if (c->x86_clflush_size > 0) {
		pci_dfl_cache_line_size = c->x86_clflush_size >> 2;
		printk(KERN_DEBUG "PCI: pci_cache_line_size set to %d bytes\n",
			pci_dfl_cache_line_size << 2);
	} else {
 		pci_dfl_cache_line_size = 32 >> 2;
		printk(KERN_DEBUG "PCI: Unknown cacheline size. Setting to 32 bytes\n");
	}
}

int __init pcibios_init(void)
{
	if (!raw_pci_ops) {
		printk(KERN_WARNING "PCI: System does not support PCI\n");
		return 0;
	}

	pcibios_set_cache_line_size();
	pcibios_resource_survey();

	if (pci_bf_sort >= pci_force_bf)
		pci_sort_breadthfirst();
	return 0;
}

char * __devinit  pcibios_setup(char *str)
{
	if (!strcmp(str, "off")) {
		pci_probe = 0;
		return NULL;
	} else if (!strcmp(str, "bfsort")) {
		pci_bf_sort = pci_force_bf;
		return NULL;
	} else if (!strcmp(str, "nobfsort")) {
		pci_bf_sort = pci_force_nobf;
		return NULL;
	}
#ifdef CONFIG_PCI_BIOS
	else if (!strcmp(str, "bios")) {
		pci_probe = PCI_PROBE_BIOS;
		return NULL;
	} else if (!strcmp(str, "nobios")) {
		pci_probe &= ~PCI_PROBE_BIOS;
		return NULL;
	} else if (!strcmp(str, "biosirq")) {
		pci_probe |= PCI_BIOS_IRQ_SCAN;
		return NULL;
	} else if (!strncmp(str, "pirqaddr=", 9)) {
		pirq_table_addr = simple_strtoul(str+9, NULL, 0);
		return NULL;
	}
#endif
#ifdef CONFIG_PCI_DIRECT
	else if (!strcmp(str, "conf1")) {
		pci_probe = PCI_PROBE_CONF1 | PCI_NO_CHECKS;
		return NULL;
	}
	else if (!strcmp(str, "conf2")) {
		pci_probe = PCI_PROBE_CONF2 | PCI_NO_CHECKS;
		return NULL;
	}
#endif
#ifdef CONFIG_PCI_MMCONFIG
	else if (!strcmp(str, "nommconf")) {
		pci_probe &= ~PCI_PROBE_MMCONF;
		return NULL;
	}
	else if (!strcmp(str, "check_enable_amd_mmconf")) {
		pci_probe |= PCI_CHECK_ENABLE_AMD_MMCONF;
		return NULL;
	}
#endif
	else if (!strcmp(str, "noacpi")) {
		acpi_noirq_set();
		return NULL;
	}
	else if (!strcmp(str, "noearly")) {
		pci_probe |= PCI_PROBE_NOEARLY;
		return NULL;
	}
#ifndef CONFIG_X86_VISWS
	else if (!strcmp(str, "usepirqmask")) {
		pci_probe |= PCI_USE_PIRQ_MASK;
		return NULL;
	} else if (!strncmp(str, "irqmask=", 8)) {
		pcibios_irq_mask = simple_strtol(str+8, NULL, 0);
		return NULL;
	} else if (!strncmp(str, "lastbus=", 8)) {
		pcibios_last_bus = simple_strtol(str+8, NULL, 0);
		return NULL;
	}
#endif
	else if (!strcmp(str, "rom")) {
		pci_probe |= PCI_ASSIGN_ROMS;
		return NULL;
	} else if (!strcmp(str, "norom")) {
		pci_probe |= PCI_NOASSIGN_ROMS;
		return NULL;
	} else if (!strcmp(str, "nobar")) {
		pci_probe |= PCI_NOASSIGN_BARS;
		return NULL;
	} else if (!strcmp(str, "assign-busses")) {
		pci_probe |= PCI_ASSIGN_ALL_BUSSES;
		return NULL;
	} else if (!strcmp(str, "use_crs")) {
		pci_probe |= PCI_USE__CRS;
		return NULL;
	} else if (!strcmp(str, "nocrs")) {
		pci_probe |= PCI_ROOT_NO_CRS;
		return NULL;
	} else if (!strcmp(str, "earlydump")) {
		pci_early_dump_regs = 1;
		return NULL;
	} else if (!strcmp(str, "routeirq")) {
		pci_routeirq = 1;
		return NULL;
	} else if (!strcmp(str, "skip_isa_align")) {
		pci_probe |= PCI_CAN_SKIP_ISA_ALIGN;
		return NULL;
	} else if (!strcmp(str, "noioapicquirk")) {
		noioapicquirk = 1;
		return NULL;
	} else if (!strcmp(str, "ioapicreroute")) {
		if (noioapicreroute != -1)
			noioapicreroute = 0;
		return NULL;
	} else if (!strcmp(str, "noioapicreroute")) {
		if (noioapicreroute != -1)
			noioapicreroute = 1;
		return NULL;
	}
	return str;
}

unsigned int pcibios_assign_all_busses(void)
{
	return (pci_probe & PCI_ASSIGN_ALL_BUSSES) ? 1 : 0;
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	int err;

	if ((err = pci_enable_resources(dev, mask)) < 0)
		return err;

	if (!pci_dev_msi_enabled(dev))
		return pcibios_enable_irq(dev);
	return 0;
}

void pcibios_disable_device (struct pci_dev *dev)
{
	if (!pci_dev_msi_enabled(dev) && pcibios_disable_irq)
		pcibios_disable_irq(dev);
}

int pci_ext_cfg_avail(struct pci_dev *dev)
{
	if (raw_pci_ext_ops)
		return 1;
	else
		return 0;
}

struct pci_bus * __devinit pci_scan_bus_on_node(int busno, struct pci_ops *ops, int node)
{
	struct pci_bus *bus = NULL;
	struct pci_sysdata *sd;

	/*
	 * Allocate per-root-bus (not per bus) arch-specific data.
	 * TODO: leak; this memory is never freed.
	 * It's arguable whether it's worth the trouble to care.
	 */
	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd) {
		printk(KERN_ERR "PCI: OOM, skipping PCI bus %02x\n", busno);
		return NULL;
	}
	sd->node = node;
	bus = pci_scan_bus(busno, ops, sd);
	if (!bus)
		kfree(sd);

	return bus;
}

struct pci_bus * __devinit pci_scan_bus_with_sysdata(int busno)
{
	return pci_scan_bus_on_node(busno, &pci_root_ops, -1);
}

/*
 * NUMA info for PCI busses
 *
 * Early arch code is responsible for filling in reasonable values here.
 * A node id of "-1" means "use current node".  In other words, if a bus
 * has a -1 node id, it's not tightly coupled to any particular chunk
 * of memory (as is the case on some Nehalem systems).
 */
#ifdef CONFIG_NUMA

#define BUS_NR 256

#ifdef CONFIG_X86_64

static int mp_bus_to_node[BUS_NR] = {
	[0 ... BUS_NR - 1] = -1
};

void set_mp_bus_to_node(int busnum, int node)
{
	if (busnum >= 0 &&  busnum < BUS_NR)
		mp_bus_to_node[busnum] = node;
}

int get_mp_bus_to_node(int busnum)
{
	int node = -1;

	if (busnum < 0 || busnum > (BUS_NR - 1))
		return node;

	node = mp_bus_to_node[busnum];

	/*
	 * let numa_node_id to decide it later in dma_alloc_pages
	 * if there is no ram on that node
	 */
	if (node != -1 && !node_online(node))
		node = -1;

	return node;
}

#else /* CONFIG_X86_32 */

static int mp_bus_to_node[BUS_NR] = {
	[0 ... BUS_NR - 1] = -1
};

void set_mp_bus_to_node(int busnum, int node)
{
	if (busnum >= 0 &&  busnum < BUS_NR)
	mp_bus_to_node[busnum] = (unsigned char) node;
}

int get_mp_bus_to_node(int busnum)
{
	int node;

	if (busnum < 0 || busnum > (BUS_NR - 1))
		return 0;
	node = mp_bus_to_node[busnum];
	return node;
}

#endif /* CONFIG_X86_32 */

#endif /* CONFIG_NUMA */
