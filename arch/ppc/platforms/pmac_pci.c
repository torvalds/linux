/*
 * Support for PCI bridges found on Power Macintoshes.
 * At present the "bandit" and "chaos" bridges are supported.
 * Fortunately you access configuration space in the same
 * way with either bridge.
 *
 * Copyright (C) 1997 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/sections.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>

#undef DEBUG

#ifdef DEBUG
#ifdef CONFIG_XMON
extern void xmon_printf(const char *fmt, ...);
#define DBG(x...) xmon_printf(x)
#else
#define DBG(x...) printk(x)
#endif
#else
#define DBG(x...)
#endif

static int add_bridge(struct device_node *dev);
extern void pmac_check_ht_link(void);

/* XXX Could be per-controller, but I don't think we risk anything by
 * assuming we won't have both UniNorth and Bandit */
static int has_uninorth;
#ifdef CONFIG_POWER4
static struct pci_controller *u3_agp;
#endif /* CONFIG_POWER4 */

extern u8 pci_cache_line_size;
extern int pcibios_assign_bus_offset;

struct device_node *k2_skiplist[2];

/*
 * Magic constants for enabling cache coherency in the bandit/PSX bridge.
 */
#define BANDIT_DEVID_2	8
#define BANDIT_REVID	3

#define BANDIT_DEVNUM	11
#define BANDIT_MAGIC	0x50
#define BANDIT_COHERENT	0x40

static int __init
fixup_one_level_bus_range(struct device_node *node, int higher)
{
	for (; node != 0;node = node->sibling) {
		int * bus_range;
		unsigned int *class_code;
		int len;

		/* For PCI<->PCI bridges or CardBus bridges, we go down */
		class_code = (unsigned int *) get_property(node, "class-code", NULL);
		if (!class_code || ((*class_code >> 8) != PCI_CLASS_BRIDGE_PCI &&
			(*class_code >> 8) != PCI_CLASS_BRIDGE_CARDBUS))
			continue;
		bus_range = (int *) get_property(node, "bus-range", &len);
		if (bus_range != NULL && len > 2 * sizeof(int)) {
			if (bus_range[1] > higher)
				higher = bus_range[1];
		}
		higher = fixup_one_level_bus_range(node->child, higher);
	}
	return higher;
}

/* This routine fixes the "bus-range" property of all bridges in the
 * system since they tend to have their "last" member wrong on macs
 *
 * Note that the bus numbers manipulated here are OF bus numbers, they
 * are not Linux bus numbers.
 */
static void __init
fixup_bus_range(struct device_node *bridge)
{
	int * bus_range;
	int len;

	/* Lookup the "bus-range" property for the hose */
	bus_range = (int *) get_property(bridge, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		printk(KERN_WARNING "Can't get bus-range for %s\n",
			       bridge->full_name);
		return;
	}
	bus_range[1] = fixup_one_level_bus_range(bridge->child, bus_range[1]);
}

/*
 * Apple MacRISC (U3, UniNorth, Bandit, Chaos) PCI controllers.
 *
 * The "Bandit" version is present in all early PCI PowerMacs,
 * and up to the first ones using Grackle. Some machines may
 * have 2 bandit controllers (2 PCI busses).
 *
 * "Chaos" is used in some "Bandit"-type machines as a bridge
 * for the separate display bus. It is accessed the same
 * way as bandit, but cannot be probed for devices. It therefore
 * has its own config access functions.
 *
 * The "UniNorth" version is present in all Core99 machines
 * (iBook, G4, new IMacs, and all the recent Apple machines).
 * It contains 3 controllers in one ASIC.
 *
 * The U3 is the bridge used on G5 machines. It contains an
 * AGP bus which is dealt with the old UniNorth access routines
 * and a HyperTransport bus which uses its own set of access
 * functions.
 */

#define MACRISC_CFA0(devfn, off)	\
	((1 << (unsigned long)PCI_SLOT(dev_fn)) \
	| (((unsigned long)PCI_FUNC(dev_fn)) << 8) \
	| (((unsigned long)(off)) & 0xFCUL))

#define MACRISC_CFA1(bus, devfn, off)	\
	((((unsigned long)(bus)) << 16) \
	|(((unsigned long)(devfn)) << 8) \
	|(((unsigned long)(off)) & 0xFCUL) \
	|1UL)

static void volatile __iomem *
macrisc_cfg_access(struct pci_controller* hose, u8 bus, u8 dev_fn, u8 offset)
{
	unsigned int caddr;

	if (bus == hose->first_busno) {
		if (dev_fn < (11 << 3))
			return NULL;
		caddr = MACRISC_CFA0(dev_fn, offset);
	} else
		caddr = MACRISC_CFA1(bus, dev_fn, offset);

	/* Uninorth will return garbage if we don't read back the value ! */
	do {
		out_le32(hose->cfg_addr, caddr);
	} while (in_le32(hose->cfg_addr) != caddr);

	offset &= has_uninorth ? 0x07 : 0x03;
	return hose->cfg_data + offset;
}

static int
macrisc_read_config(struct pci_bus *bus, unsigned int devfn, int offset,
		    int len, u32 *val)
{
	struct pci_controller *hose = bus->sysdata;
	void volatile __iomem *addr;

	addr = macrisc_cfg_access(hose, bus->number, devfn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		*val = in_8(addr);
		break;
	case 2:
		*val = in_le16(addr);
		break;
	default:
		*val = in_le32(addr);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int
macrisc_write_config(struct pci_bus *bus, unsigned int devfn, int offset,
		     int len, u32 val)
{
	struct pci_controller *hose = bus->sysdata;
	void volatile __iomem *addr;

	addr = macrisc_cfg_access(hose, bus->number, devfn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		out_8(addr, val);
		(void) in_8(addr);
		break;
	case 2:
		out_le16(addr, val);
		(void) in_le16(addr);
		break;
	default:
		out_le32(addr, val);
		(void) in_le32(addr);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops macrisc_pci_ops =
{
	macrisc_read_config,
	macrisc_write_config
};

/*
 * Verifiy that a specific (bus, dev_fn) exists on chaos
 */
static int
chaos_validate_dev(struct pci_bus *bus, int devfn, int offset)
{
	struct device_node *np;
	u32 *vendor, *device;

	np = pci_busdev_to_OF_node(bus, devfn);
	if (np == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	vendor = (u32 *)get_property(np, "vendor-id", NULL);
	device = (u32 *)get_property(np, "device-id", NULL);
	if (vendor == NULL || device == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if ((*vendor == 0x106b) && (*device == 3) && (offset >= 0x10)
	    && (offset != 0x14) && (offset != 0x18) && (offset <= 0x24))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return PCIBIOS_SUCCESSFUL;
}

static int
chaos_read_config(struct pci_bus *bus, unsigned int devfn, int offset,
		  int len, u32 *val)
{
	int result = chaos_validate_dev(bus, devfn, offset);
	if (result == PCIBIOS_BAD_REGISTER_NUMBER)
		*val = ~0U;
	if (result != PCIBIOS_SUCCESSFUL)
		return result;
	return macrisc_read_config(bus, devfn, offset, len, val);
}

static int
chaos_write_config(struct pci_bus *bus, unsigned int devfn, int offset,
		   int len, u32 val)
{
	int result = chaos_validate_dev(bus, devfn, offset);
	if (result != PCIBIOS_SUCCESSFUL)
		return result;
	return macrisc_write_config(bus, devfn, offset, len, val);
}

static struct pci_ops chaos_pci_ops =
{
	chaos_read_config,
	chaos_write_config
};

#ifdef CONFIG_POWER4

/*
 * These versions of U3 HyperTransport config space access ops do not
 * implement self-view of the HT host yet
 */

#define U3_HT_CFA0(devfn, off)		\
		((((unsigned long)devfn) << 8) | offset)
#define U3_HT_CFA1(bus, devfn, off)	\
		(U3_HT_CFA0(devfn, off) \
		+ (((unsigned long)bus) << 16) \
		+ 0x01000000UL)

static void volatile __iomem *
u3_ht_cfg_access(struct pci_controller* hose, u8 bus, u8 devfn, u8 offset)
{
	if (bus == hose->first_busno) {
		/* For now, we don't self probe U3 HT bridge */
		if (PCI_FUNC(devfn) != 0 || PCI_SLOT(devfn) > 7 ||
		    PCI_SLOT(devfn) < 1)
			return 0;
		return hose->cfg_data + U3_HT_CFA0(devfn, offset);
	} else
		return hose->cfg_data + U3_HT_CFA1(bus, devfn, offset);
}

static int
u3_ht_read_config(struct pci_bus *bus, unsigned int devfn, int offset,
		    int len, u32 *val)
{
	struct pci_controller *hose = bus->sysdata;
	void volatile __iomem *addr;
	int i;

	struct device_node *np = pci_busdev_to_OF_node(bus, devfn);
	if (np == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * When a device in K2 is powered down, we die on config
	 * cycle accesses. Fix that here.
	 */
	for (i=0; i<2; i++)
		if (k2_skiplist[i] == np) {
			switch (len) {
			case 1:
				*val = 0xff; break;
			case 2:
				*val = 0xffff; break;
			default:
				*val = 0xfffffffful; break;
			}
			return PCIBIOS_SUCCESSFUL;
		}
	    
	addr = u3_ht_cfg_access(hose, bus->number, devfn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		*val = in_8(addr);
		break;
	case 2:
		*val = in_le16(addr);
		break;
	default:
		*val = in_le32(addr);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int
u3_ht_write_config(struct pci_bus *bus, unsigned int devfn, int offset,
		     int len, u32 val)
{
	struct pci_controller *hose = bus->sysdata;
	void volatile __iomem *addr;
	int i;

	struct device_node *np = pci_busdev_to_OF_node(bus, devfn);
	if (np == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;
	/*
	 * When a device in K2 is powered down, we die on config
	 * cycle accesses. Fix that here.
	 */
	for (i=0; i<2; i++)
		if (k2_skiplist[i] == np)
			return PCIBIOS_SUCCESSFUL;

	addr = u3_ht_cfg_access(hose, bus->number, devfn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		out_8(addr, val);
		(void) in_8(addr);
		break;
	case 2:
		out_le16(addr, val);
		(void) in_le16(addr);
		break;
	default:
		out_le32(addr, val);
		(void) in_le32(addr);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops u3_ht_pci_ops =
{
	u3_ht_read_config,
	u3_ht_write_config
};

#endif /* CONFIG_POWER4 */

/*
 * For a bandit bridge, turn on cache coherency if necessary.
 * N.B. we could clean this up using the hose ops directly.
 */
static void __init
init_bandit(struct pci_controller *bp)
{
	unsigned int vendev, magic;
	int rev;

	/* read the word at offset 0 in config space for device 11 */
	out_le32(bp->cfg_addr, (1UL << BANDIT_DEVNUM) + PCI_VENDOR_ID);
	udelay(2);
	vendev = in_le32(bp->cfg_data);
	if (vendev == (PCI_DEVICE_ID_APPLE_BANDIT << 16) +
			PCI_VENDOR_ID_APPLE) {
		/* read the revision id */
		out_le32(bp->cfg_addr,
			 (1UL << BANDIT_DEVNUM) + PCI_REVISION_ID);
		udelay(2);
		rev = in_8(bp->cfg_data);
		if (rev != BANDIT_REVID)
			printk(KERN_WARNING
			       "Unknown revision %d for bandit\n", rev);
	} else if (vendev != (BANDIT_DEVID_2 << 16) + PCI_VENDOR_ID_APPLE) {
		printk(KERN_WARNING "bandit isn't? (%x)\n", vendev);
		return;
	}

	/* read the word at offset 0x50 */
	out_le32(bp->cfg_addr, (1UL << BANDIT_DEVNUM) + BANDIT_MAGIC);
	udelay(2);
	magic = in_le32(bp->cfg_data);
	if ((magic & BANDIT_COHERENT) != 0)
		return;
	magic |= BANDIT_COHERENT;
	udelay(2);
	out_le32(bp->cfg_data, magic);
	printk(KERN_INFO "Cache coherency enabled for bandit/PSX\n");
}


/*
 * Tweak the PCI-PCI bridge chip on the blue & white G3s.
 */
static void __init
init_p2pbridge(void)
{
	struct device_node *p2pbridge;
	struct pci_controller* hose;
	u8 bus, devfn;
	u16 val;

	/* XXX it would be better here to identify the specific
	   PCI-PCI bridge chip we have. */
	if ((p2pbridge = find_devices("pci-bridge")) == 0
	    || p2pbridge->parent == NULL
	    || strcmp(p2pbridge->parent->name, "pci") != 0)
		return;
	if (pci_device_from_OF_node(p2pbridge, &bus, &devfn) < 0) {
		DBG("Can't find PCI infos for PCI<->PCI bridge\n");
		return;
	}
	/* Warning: At this point, we have not yet renumbered all busses.
	 * So we must use OF walking to find out hose
	 */
	hose = pci_find_hose_for_OF_device(p2pbridge);
	if (!hose) {
		DBG("Can't find hose for PCI<->PCI bridge\n");
		return;
	}
	if (early_read_config_word(hose, bus, devfn,
				   PCI_BRIDGE_CONTROL, &val) < 0) {
		printk(KERN_ERR "init_p2pbridge: couldn't read bridge control\n");
		return;
	}
	val &= ~PCI_BRIDGE_CTL_MASTER_ABORT;
	early_write_config_word(hose, bus, devfn, PCI_BRIDGE_CONTROL, val);
}

/*
 * Some Apple desktop machines have a NEC PD720100A USB2 controller
 * on the motherboard. Open Firmware, on these, will disable the
 * EHCI part of it so it behaves like a pair of OHCI's. This fixup
 * code re-enables it ;)
 */
static void __init
fixup_nec_usb2(void)
{
	struct device_node *nec;

	for (nec = NULL; (nec = of_find_node_by_name(nec, "usb")) != NULL;) {
		struct pci_controller *hose;
		u32 data, *prop;
		u8 bus, devfn;
		
		prop = (u32 *)get_property(nec, "vendor-id", NULL);
		if (prop == NULL)
			continue;
		if (0x1033 != *prop)
			continue;
		prop = (u32 *)get_property(nec, "device-id", NULL);
		if (prop == NULL)
			continue;
		if (0x0035 != *prop)
			continue;
		prop = (u32 *)get_property(nec, "reg", NULL);
		if (prop == NULL)
			continue;
		devfn = (prop[0] >> 8) & 0xff;
		bus = (prop[0] >> 16) & 0xff;
		if (PCI_FUNC(devfn) != 0)
			continue;
		hose = pci_find_hose_for_OF_device(nec);
		if (!hose)
			continue;
		early_read_config_dword(hose, bus, devfn, 0xe4, &data);
		if (data & 1UL) {
			printk("Found NEC PD720100A USB2 chip with disabled EHCI, fixing up...\n");
			data &= ~1UL;
			early_write_config_dword(hose, bus, devfn, 0xe4, data);
			early_write_config_byte(hose, bus, devfn | 2, PCI_INTERRUPT_LINE,
				nec->intrs[0].line);
		}
	}
}

void __init
pmac_find_bridges(void)
{
	struct device_node *np, *root;
	struct device_node *ht = NULL;

	root = of_find_node_by_path("/");
	if (root == NULL) {
		printk(KERN_CRIT "pmac_find_bridges: can't find root of device tree\n");
		return;
	}
	for (np = NULL; (np = of_get_next_child(root, np)) != NULL;) {
		if (np->name == NULL)
			continue;
		if (strcmp(np->name, "bandit") == 0
		    || strcmp(np->name, "chaos") == 0
		    || strcmp(np->name, "pci") == 0) {
			if (add_bridge(np) == 0)
				of_node_get(np);
		}
		if (strcmp(np->name, "ht") == 0) {
			of_node_get(np);
			ht = np;
		}
	}
	of_node_put(root);

	/* Probe HT last as it relies on the agp resources to be already
	 * setup
	 */
	if (ht && add_bridge(ht) != 0)
		of_node_put(ht);

	init_p2pbridge();
	fixup_nec_usb2();
	
	/* We are still having some issues with the Xserve G4, enabling
	 * some offset between bus number and domains for now when we
	 * assign all busses should help for now
	 */
	if (pci_assign_all_buses)
		pcibios_assign_bus_offset = 0x10;

#ifdef CONFIG_POWER4 
	/* There is something wrong with DMA on U3/HT. I haven't figured out
	 * the details yet, but if I set the cache line size to 128 bytes like
	 * it should, I'm getting memory corruption caused by devices like
	 * sungem (even without the MWI bit set, but maybe sungem doesn't
	 * care). Right now, it appears that setting up a 64 bytes line size
	 * works properly, 64 bytes beeing the max transfer size of HT, I
	 * suppose this is related the way HT/PCI are hooked together. I still
	 * need to dive into more specs though to be really sure of what's
	 * going on. --BenH.
	 *
	 * Ok, apparently, it's just that HT can't do more than 64 bytes
	 * transactions. MWI seem to be meaningless there as well, it may
	 * be worth nop'ing out pci_set_mwi too though I haven't done that
	 * yet.
	 *
	 * Note that it's a bit different for whatever is in the AGP slot.
	 * For now, I don't care, but this can become a real issue, we
	 * should probably hook pci_set_mwi anyway to make sure it sets
	 * the real cache line size in there.
	 */
	if (machine_is_compatible("MacRISC4"))
		pci_cache_line_size = 16; /* 64 bytes */

	pmac_check_ht_link();
#endif /* CONFIG_POWER4 */
}

#define GRACKLE_CFA(b, d, o)	(0x80 | ((b) << 8) | ((d) << 16) \
				 | (((o) & ~3) << 24))

#define GRACKLE_PICR1_STG		0x00000040
#define GRACKLE_PICR1_LOOPSNOOP		0x00000010

/* N.B. this is called before bridges is initialized, so we can't
   use grackle_pcibios_{read,write}_config_dword. */
static inline void grackle_set_stg(struct pci_controller* bp, int enable)
{
	unsigned int val;

	out_be32(bp->cfg_addr, GRACKLE_CFA(0, 0, 0xa8));
	val = in_le32(bp->cfg_data);
	val = enable? (val | GRACKLE_PICR1_STG) :
		(val & ~GRACKLE_PICR1_STG);
	out_be32(bp->cfg_addr, GRACKLE_CFA(0, 0, 0xa8));
	out_le32(bp->cfg_data, val);
	(void)in_le32(bp->cfg_data);
}

static inline void grackle_set_loop_snoop(struct pci_controller *bp, int enable)
{
	unsigned int val;

	out_be32(bp->cfg_addr, GRACKLE_CFA(0, 0, 0xa8));
	val = in_le32(bp->cfg_data);
	val = enable? (val | GRACKLE_PICR1_LOOPSNOOP) :
		(val & ~GRACKLE_PICR1_LOOPSNOOP);
	out_be32(bp->cfg_addr, GRACKLE_CFA(0, 0, 0xa8));
	out_le32(bp->cfg_data, val);
	(void)in_le32(bp->cfg_data);
}

static int __init
setup_uninorth(struct pci_controller* hose, struct reg_property* addr)
{
	pci_assign_all_buses = 1;
	has_uninorth = 1;
	hose->ops = &macrisc_pci_ops;
	hose->cfg_addr = ioremap(addr->address + 0x800000, 0x1000);
	hose->cfg_data = ioremap(addr->address + 0xc00000, 0x1000);
	/* We "know" that the bridge at f2000000 has the PCI slots. */
	return addr->address == 0xf2000000;
}

static void __init
setup_bandit(struct pci_controller* hose, struct reg_property* addr)
{
	hose->ops = &macrisc_pci_ops;
	hose->cfg_addr = ioremap(addr->address + 0x800000, 0x1000);
	hose->cfg_data = ioremap(addr->address + 0xc00000, 0x1000);
	init_bandit(hose);
}

static void __init
setup_chaos(struct pci_controller* hose, struct reg_property* addr)
{
	/* assume a `chaos' bridge */
	hose->ops = &chaos_pci_ops;
	hose->cfg_addr = ioremap(addr->address + 0x800000, 0x1000);
	hose->cfg_data = ioremap(addr->address + 0xc00000, 0x1000);
}

#ifdef CONFIG_POWER4

static void __init
setup_u3_agp(struct pci_controller* hose, struct reg_property* addr)
{
	/* On G5, we move AGP up to high bus number so we don't need
	 * to reassign bus numbers for HT. If we ever have P2P bridges
	 * on AGP, we'll have to move pci_assign_all_buses to the
	 * pci_controller structure so we enable it for AGP and not for
	 * HT childs.
	 * We hard code the address because of the different size of
	 * the reg address cell, we shall fix that by killing struct
	 * reg_property and using some accessor functions instead
	 */
       	hose->first_busno = 0xf0;
	hose->last_busno = 0xff;
	has_uninorth = 1;
	hose->ops = &macrisc_pci_ops;
	hose->cfg_addr = ioremap(0xf0000000 + 0x800000, 0x1000);
	hose->cfg_data = ioremap(0xf0000000 + 0xc00000, 0x1000);

	u3_agp = hose;
}

static void __init
setup_u3_ht(struct pci_controller* hose, struct reg_property *addr)
{
	struct device_node *np = (struct device_node *)hose->arch_data;
	int i, cur;

	hose->ops = &u3_ht_pci_ops;

	/* We hard code the address because of the different size of
	 * the reg address cell, we shall fix that by killing struct
	 * reg_property and using some accessor functions instead
	 */
	hose->cfg_data = ioremap(0xf2000000, 0x02000000);

	/*
	 * /ht node doesn't expose a "ranges" property, so we "remove" regions that
	 * have been allocated to AGP. So far, this version of the code doesn't assign
	 * any of the 0xfxxxxxxx "fine" memory regions to /ht.
	 * We need to fix that sooner or later by either parsing all child "ranges"
	 * properties or figuring out the U3 address space decoding logic and
	 * then read its configuration register (if any).
	 */
	hose->io_base_phys = 0xf4000000;
	hose->io_base_virt = ioremap(hose->io_base_phys, 0x00400000);
	isa_io_base = (unsigned long) hose->io_base_virt;
	hose->io_resource.name = np->full_name;
	hose->io_resource.start = 0;
	hose->io_resource.end = 0x003fffff;
	hose->io_resource.flags = IORESOURCE_IO;
	hose->pci_mem_offset = 0;
	hose->first_busno = 0;
	hose->last_busno = 0xef;
	hose->mem_resources[0].name = np->full_name;
	hose->mem_resources[0].start = 0x80000000;
	hose->mem_resources[0].end = 0xefffffff;
	hose->mem_resources[0].flags = IORESOURCE_MEM;

	if (u3_agp == NULL) {
		DBG("U3 has no AGP, using full resource range\n");
		return;
	}

	/* We "remove" the AGP resources from the resources allocated to HT, that
	 * is we create "holes". However, that code does assumptions that so far
	 * happen to be true (cross fingers...), typically that resources in the
	 * AGP node are properly ordered
	 */
	cur = 0;
	for (i=0; i<3; i++) {
		struct resource *res = &u3_agp->mem_resources[i];
		if (res->flags != IORESOURCE_MEM)
			continue;
		/* We don't care about "fine" resources */
		if (res->start >= 0xf0000000)
			continue;
		/* Check if it's just a matter of "shrinking" us in one direction */
		if (hose->mem_resources[cur].start == res->start) {
			DBG("U3/HT: shrink start of %d, %08lx -> %08lx\n",
			    cur, hose->mem_resources[cur].start, res->end + 1);
			hose->mem_resources[cur].start = res->end + 1;
			continue;
		}
		if (hose->mem_resources[cur].end == res->end) {
			DBG("U3/HT: shrink end of %d, %08lx -> %08lx\n",
			    cur, hose->mem_resources[cur].end, res->start - 1);
			hose->mem_resources[cur].end = res->start - 1;
			continue;
		}
		/* No, it's not the case, we need a hole */
		if (cur == 2) {
			/* not enough resources to make a hole, we drop part of the range */
			printk(KERN_WARNING "Running out of resources for /ht host !\n");
			hose->mem_resources[cur].end = res->start - 1;
			continue;
		}		
		cur++;
       		DBG("U3/HT: hole, %d end at %08lx, %d start at %08lx\n",
		    cur-1, res->start - 1, cur, res->end + 1);
		hose->mem_resources[cur].name = np->full_name;
		hose->mem_resources[cur].flags = IORESOURCE_MEM;
		hose->mem_resources[cur].start = res->end + 1;
		hose->mem_resources[cur].end = hose->mem_resources[cur-1].end;
		hose->mem_resources[cur-1].end = res->start - 1;
	}
}

#endif /* CONFIG_POWER4 */

void __init
setup_grackle(struct pci_controller *hose)
{
	setup_indirect_pci(hose, 0xfec00000, 0xfee00000);
	if (machine_is_compatible("AAPL,PowerBook1998"))
		grackle_set_loop_snoop(hose, 1);
#if 0	/* Disabled for now, HW problems ??? */
	grackle_set_stg(hose, 1);
#endif
}

/*
 * We assume that if we have a G3 powermac, we have one bridge called
 * "pci" (a MPC106) and no bandit or chaos bridges, and contrariwise,
 * if we have one or more bandit or chaos bridges, we don't have a MPC106.
 */
static int __init
add_bridge(struct device_node *dev)
{
	int len;
	struct pci_controller *hose;
	struct reg_property *addr;
	char* disp_name;
	int *bus_range;
	int primary = 1;

	DBG("Adding PCI host bridge %s\n", dev->full_name);

       	addr = (struct reg_property *) get_property(dev, "reg", &len);
       	if (addr == NULL || len < sizeof(*addr)) {
       		printk(KERN_WARNING "Can't use %s: no address\n",
       		       dev->full_name);
       		return -ENODEV;
       	}
       	bus_range = (int *) get_property(dev, "bus-range", &len);
       	if (bus_range == NULL || len < 2 * sizeof(int)) {
       		printk(KERN_WARNING "Can't get bus-range for %s, assume bus 0\n",
       			       dev->full_name);
       	}

       	hose = pcibios_alloc_controller();
       	if (!hose)
       		return -ENOMEM;
       	hose->arch_data = dev;
       	hose->first_busno = bus_range ? bus_range[0] : 0;
       	hose->last_busno = bus_range ? bus_range[1] : 0xff;

	disp_name = NULL;
#ifdef CONFIG_POWER4
       	if (device_is_compatible(dev, "u3-agp")) {
       		setup_u3_agp(hose, addr);
       		disp_name = "U3-AGP";
       		primary = 0;
       	} else if (device_is_compatible(dev, "u3-ht")) {
       		setup_u3_ht(hose, addr);
       		disp_name = "U3-HT";
       		primary = 1;
       	} else
#endif /* CONFIG_POWER4 */
	if (device_is_compatible(dev, "uni-north")) {
       		primary = setup_uninorth(hose, addr);
       		disp_name = "UniNorth";
       	} else if (strcmp(dev->name, "pci") == 0) {
       		/* XXX assume this is a mpc106 (grackle) */
       		setup_grackle(hose);
       		disp_name = "Grackle (MPC106)";
       	} else if (strcmp(dev->name, "bandit") == 0) {
       		setup_bandit(hose, addr);
       		disp_name = "Bandit";
       	} else if (strcmp(dev->name, "chaos") == 0) {
       		setup_chaos(hose, addr);
       		disp_name = "Chaos";
       		primary = 0;
       	}
       	printk(KERN_INFO "Found %s PCI host bridge at 0x%08x. Firmware bus number: %d->%d\n",
       		disp_name, addr->address, hose->first_busno, hose->last_busno);
       	DBG(" ->Hose at 0x%p, cfg_addr=0x%p,cfg_data=0x%p\n",
       		hose, hose->cfg_addr, hose->cfg_data);

       	/* Interpret the "ranges" property */
       	/* This also maps the I/O region and sets isa_io/mem_base */
       	pci_process_bridge_OF_ranges(hose, dev, primary);

       	/* Fixup "bus-range" OF property */
       	fixup_bus_range(dev);

	return 0;
}

static void __init
pcibios_fixup_OF_interrupts(void)
{
	struct pci_dev* dev = NULL;

	/*
	 * Open Firmware often doesn't initialize the
	 * PCI_INTERRUPT_LINE config register properly, so we
	 * should find the device node and apply the interrupt
	 * obtained from the OF device-tree
	 */
	for_each_pci_dev(dev) {
		struct device_node *node;
		node = pci_device_to_OF_node(dev);
		/* this is the node, see if it has interrupts */
		if (node && node->n_intrs > 0)
			dev->irq = node->intrs[0].line;
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
	}
}

void __init
pmac_pcibios_fixup(void)
{
	/* Fixup interrupts according to OF tree */
	pcibios_fixup_OF_interrupts();
}

int
pmac_pci_enable_device_hook(struct pci_dev *dev, int initial)
{
	struct device_node* node;
	int updatecfg = 0;
	int uninorth_child;

	node = pci_device_to_OF_node(dev);

	/* We don't want to enable USB controllers absent from the OF tree
	 * (iBook second controller)
	 */
	if (dev->vendor == PCI_VENDOR_ID_APPLE
	    && (dev->class == ((PCI_CLASS_SERIAL_USB << 8) | 0x10))
	    && !node) {
		printk(KERN_INFO "Apple USB OHCI %s disabled by firmware\n",
		       pci_name(dev));
		return -EINVAL;
	}

	if (!node)
		return 0;

	uninorth_child = node->parent &&
		device_is_compatible(node->parent, "uni-north");
	
	/* Firewire & GMAC were disabled after PCI probe, the driver is
	 * claiming them, we must re-enable them now.
	 */
	if (uninorth_child && !strcmp(node->name, "firewire") &&
	    (device_is_compatible(node, "pci106b,18") ||
	     device_is_compatible(node, "pci106b,30") ||
	     device_is_compatible(node, "pci11c1,5811"))) {
		pmac_call_feature(PMAC_FTR_1394_CABLE_POWER, node, 0, 1);
		pmac_call_feature(PMAC_FTR_1394_ENABLE, node, 0, 1);
		updatecfg = 1;
	}
	if (uninorth_child && !strcmp(node->name, "ethernet") &&
	    device_is_compatible(node, "gmac")) {
		pmac_call_feature(PMAC_FTR_GMAC_ENABLE, node, 0, 1);
		updatecfg = 1;
	}

	if (updatecfg) {
		u16 cmd;
	
		/*
		 * Make sure PCI is correctly configured
		 *
		 * We use old pci_bios versions of the function since, by
		 * default, gmac is not powered up, and so will be absent
		 * from the kernel initial PCI lookup.
		 *
		 * Should be replaced by 2.4 new PCI mechanisms and really
		 * register the device.
		 */
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_INVALIDATE;
    		pci_write_config_word(dev, PCI_COMMAND, cmd);
    		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 16);
    		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, pci_cache_line_size);
	}

	return 0;
}

/* We power down some devices after they have been probed. They'll
 * be powered back on later on
 */
void __init
pmac_pcibios_after_init(void)
{
	struct device_node* nd;

#ifdef CONFIG_BLK_DEV_IDE
	struct pci_dev *dev = NULL;

	/* OF fails to initialize IDE controllers on macs
	 * (and maybe other machines)
	 *
	 * Ideally, this should be moved to the IDE layer, but we need
	 * to check specifically with Andre Hedrick how to do it cleanly
	 * since the common IDE code seem to care about the fact that the
	 * BIOS may have disabled a controller.
	 *
	 * -- BenH
	 */
	for_each_pci_dev(dev) {
		if ((dev->class >> 16) == PCI_BASE_CLASS_STORAGE)
			pci_enable_device(dev);
	}
#endif /* CONFIG_BLK_DEV_IDE */

	nd = find_devices("firewire");
	while (nd) {
		if (nd->parent && (device_is_compatible(nd, "pci106b,18") ||
				   device_is_compatible(nd, "pci106b,30") ||
				   device_is_compatible(nd, "pci11c1,5811"))
		    && device_is_compatible(nd->parent, "uni-north")) {
			pmac_call_feature(PMAC_FTR_1394_ENABLE, nd, 0, 0);
			pmac_call_feature(PMAC_FTR_1394_CABLE_POWER, nd, 0, 0);
		}
		nd = nd->next;
	}
	nd = find_devices("ethernet");
	while (nd) {
		if (nd->parent && device_is_compatible(nd, "gmac")
		    && device_is_compatible(nd->parent, "uni-north"))
			pmac_call_feature(PMAC_FTR_GMAC_ENABLE, nd, 0, 0);
		nd = nd->next;
	}
}

void pmac_pci_fixup_cardbus(struct pci_dev* dev)
{
	if (_machine != _MACH_Pmac)
		return;
	/*
	 * Fix the interrupt routing on the various cardbus bridges
	 * used on powerbooks
	 */
	if (dev->vendor != PCI_VENDOR_ID_TI)
		return;
	if (dev->device == PCI_DEVICE_ID_TI_1130 ||
	    dev->device == PCI_DEVICE_ID_TI_1131) {
		u8 val;
	    	/* Enable PCI interrupt */
		if (pci_read_config_byte(dev, 0x91, &val) == 0)
			pci_write_config_byte(dev, 0x91, val | 0x30);
		/* Disable ISA interrupt mode */
		if (pci_read_config_byte(dev, 0x92, &val) == 0)
			pci_write_config_byte(dev, 0x92, val & ~0x06);
	}
	if (dev->device == PCI_DEVICE_ID_TI_1210 ||
	    dev->device == PCI_DEVICE_ID_TI_1211 ||
	    dev->device == PCI_DEVICE_ID_TI_1410 ||
	    dev->device == PCI_DEVICE_ID_TI_1510) {
		u8 val;
		/* 0x8c == TI122X_IRQMUX, 2 says to route the INTA
		   signal out the MFUNC0 pin */
		if (pci_read_config_byte(dev, 0x8c, &val) == 0)
			pci_write_config_byte(dev, 0x8c, (val & ~0x0f) | 2);
		/* Disable ISA interrupt mode */
		if (pci_read_config_byte(dev, 0x92, &val) == 0)
			pci_write_config_byte(dev, 0x92, val & ~0x06);
	}
}

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_TI, PCI_ANY_ID, pmac_pci_fixup_cardbus);

void pmac_pci_fixup_pciata(struct pci_dev* dev)
{
       u8 progif = 0;

       /*
        * On PowerMacs, we try to switch any PCI ATA controller to
	* fully native mode
        */
	if (_machine != _MACH_Pmac)
		return;
	/* Some controllers don't have the class IDE */
	if (dev->vendor == PCI_VENDOR_ID_PROMISE)
		switch(dev->device) {
		case PCI_DEVICE_ID_PROMISE_20246:
		case PCI_DEVICE_ID_PROMISE_20262:
		case PCI_DEVICE_ID_PROMISE_20263:
		case PCI_DEVICE_ID_PROMISE_20265:
		case PCI_DEVICE_ID_PROMISE_20267:
		case PCI_DEVICE_ID_PROMISE_20268:
		case PCI_DEVICE_ID_PROMISE_20269:
		case PCI_DEVICE_ID_PROMISE_20270:
		case PCI_DEVICE_ID_PROMISE_20271:
		case PCI_DEVICE_ID_PROMISE_20275:
		case PCI_DEVICE_ID_PROMISE_20276:
		case PCI_DEVICE_ID_PROMISE_20277:
			goto good;
		}
	/* Others, check PCI class */
	if ((dev->class >> 8) != PCI_CLASS_STORAGE_IDE)
		return;
 good:
	pci_read_config_byte(dev, PCI_CLASS_PROG, &progif);
	if ((progif & 5) != 5) {
		printk(KERN_INFO "Forcing PCI IDE into native mode: %s\n", pci_name(dev));
		(void) pci_write_config_byte(dev, PCI_CLASS_PROG, progif|5);
		if (pci_read_config_byte(dev, PCI_CLASS_PROG, &progif) ||
		    (progif & 5) != 5)
			printk(KERN_ERR "Rewrite of PROGIF failed !\n");
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_ANY_ID, PCI_ANY_ID, pmac_pci_fixup_pciata);


/*
 * Disable second function on K2-SATA, it's broken
 * and disable IO BARs on first one
 */
void pmac_pci_fixup_k2_sata(struct pci_dev* dev)
{
	int i;
	u16 cmd;

	if (PCI_FUNC(dev->devfn) > 0) {
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		cmd &= ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
		for (i = 0; i < 6; i++) {
			dev->resource[i].start = dev->resource[i].end = 0;
			dev->resource[i].flags = 0;
			pci_write_config_dword(dev, PCI_BASE_ADDRESS_0 + 4 * i, 0);
		}
	} else {
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		cmd &= ~PCI_COMMAND_IO;
		pci_write_config_word(dev, PCI_COMMAND, cmd);
		for (i = 0; i < 5; i++) {
			dev->resource[i].start = dev->resource[i].end = 0;
			dev->resource[i].flags = 0;
			pci_write_config_dword(dev, PCI_BASE_ADDRESS_0 + 4 * i, 0);
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SERVERWORKS, 0x0240, pmac_pci_fixup_k2_sata);
