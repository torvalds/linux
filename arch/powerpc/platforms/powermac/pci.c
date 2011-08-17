/*
 * Support for PCI bridges found on Power Macintoshes.
 *
 * Copyright (C) 2003-2005 Benjamin Herrenschmuidt (benh@kernel.crashing.org)
 * Copyright (C) 1997 Paul Mackerras (paulus@samba.org)
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
#include <linux/bootmem.h>
#include <linux/irq.h>
#include <linux/of_pci.h>

#include <asm/sections.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/grackle.h>
#include <asm/ppc-pci.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

/* XXX Could be per-controller, but I don't think we risk anything by
 * assuming we won't have both UniNorth and Bandit */
static int has_uninorth;
#ifdef CONFIG_PPC64
static struct pci_controller *u3_agp;
#else
static int has_second_ohare;
#endif /* CONFIG_PPC64 */

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

static int __init fixup_one_level_bus_range(struct device_node *node, int higher)
{
	for (; node != 0;node = node->sibling) {
		const int * bus_range;
		const unsigned int *class_code;
		int len;

		/* For PCI<->PCI bridges or CardBus bridges, we go down */
		class_code = of_get_property(node, "class-code", NULL);
		if (!class_code || ((*class_code >> 8) != PCI_CLASS_BRIDGE_PCI &&
			(*class_code >> 8) != PCI_CLASS_BRIDGE_CARDBUS))
			continue;
		bus_range = of_get_property(node, "bus-range", &len);
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
static void __init fixup_bus_range(struct device_node *bridge)
{
	int *bus_range, len;
	struct property *prop;

	/* Lookup the "bus-range" property for the hose */
	prop = of_find_property(bridge, "bus-range", &len);
	if (prop == NULL || prop->length < 2 * sizeof(int))
		return;

	bus_range = prop->value;
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
	((1 << (unsigned int)PCI_SLOT(dev_fn)) \
	| (((unsigned int)PCI_FUNC(dev_fn)) << 8) \
	| (((unsigned int)(off)) & 0xFCUL))

#define MACRISC_CFA1(bus, devfn, off)	\
	((((unsigned int)(bus)) << 16) \
	|(((unsigned int)(devfn)) << 8) \
	|(((unsigned int)(off)) & 0xFCUL) \
	|1UL)

static volatile void __iomem *macrisc_cfg_access(struct pci_controller* hose,
					       u8 bus, u8 dev_fn, u8 offset)
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

static int macrisc_read_config(struct pci_bus *bus, unsigned int devfn,
				      int offset, int len, u32 *val)
{
	struct pci_controller *hose;
	volatile void __iomem *addr;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (offset >= 0x100)
		return  PCIBIOS_BAD_REGISTER_NUMBER;
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

static int macrisc_write_config(struct pci_bus *bus, unsigned int devfn,
				       int offset, int len, u32 val)
{
	struct pci_controller *hose;
	volatile void __iomem *addr;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (offset >= 0x100)
		return  PCIBIOS_BAD_REGISTER_NUMBER;
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
		break;
	case 2:
		out_le16(addr, val);
		break;
	default:
		out_le32(addr, val);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops macrisc_pci_ops =
{
	.read = macrisc_read_config,
	.write = macrisc_write_config,
};

#ifdef CONFIG_PPC32
/*
 * Verify that a specific (bus, dev_fn) exists on chaos
 */
static int chaos_validate_dev(struct pci_bus *bus, int devfn, int offset)
{
	struct device_node *np;
	const u32 *vendor, *device;

	if (offset >= 0x100)
		return  PCIBIOS_BAD_REGISTER_NUMBER;
	np = of_pci_find_child_device(bus->dev.of_node, devfn);
	if (np == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	vendor = of_get_property(np, "vendor-id", NULL);
	device = of_get_property(np, "device-id", NULL);
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
	.read = chaos_read_config,
	.write = chaos_write_config,
};

static void __init setup_chaos(struct pci_controller *hose,
			       struct resource *addr)
{
	/* assume a `chaos' bridge */
	hose->ops = &chaos_pci_ops;
	hose->cfg_addr = ioremap(addr->start + 0x800000, 0x1000);
	hose->cfg_data = ioremap(addr->start + 0xc00000, 0x1000);
}
#endif /* CONFIG_PPC32 */

#ifdef CONFIG_PPC64
/*
 * These versions of U3 HyperTransport config space access ops do not
 * implement self-view of the HT host yet
 */

/*
 * This function deals with some "special cases" devices.
 *
 *  0 -> No special case
 *  1 -> Skip the device but act as if the access was successful
 *       (return 0xff's on reads, eventually, cache config space
 *       accesses in a later version)
 * -1 -> Hide the device (unsuccessful access)
 */
static int u3_ht_skip_device(struct pci_controller *hose,
			     struct pci_bus *bus, unsigned int devfn)
{
	struct device_node *busdn, *dn;
	int i;

	/* We only allow config cycles to devices that are in OF device-tree
	 * as we are apparently having some weird things going on with some
	 * revs of K2 on recent G5s, except for the host bridge itself, which
	 * is missing from the tree but we know we can probe.
	 */
	if (bus->self)
		busdn = pci_device_to_OF_node(bus->self);
	else if (devfn == 0)
		return 0;
	else
		busdn = hose->dn;
	for (dn = busdn->child; dn; dn = dn->sibling)
		if (PCI_DN(dn) && PCI_DN(dn)->devfn == devfn)
			break;
	if (dn == NULL)
		return -1;

	/*
	 * When a device in K2 is powered down, we die on config
	 * cycle accesses. Fix that here.
	 */
	for (i=0; i<2; i++)
		if (k2_skiplist[i] == dn)
			return 1;

	return 0;
}

#define U3_HT_CFA0(devfn, off)		\
		((((unsigned int)devfn) << 8) | offset)
#define U3_HT_CFA1(bus, devfn, off)	\
		(U3_HT_CFA0(devfn, off) \
		+ (((unsigned int)bus) << 16) \
		+ 0x01000000UL)

static void __iomem *u3_ht_cfg_access(struct pci_controller *hose, u8 bus,
				      u8 devfn, u8 offset, int *swap)
{
	*swap = 1;
	if (bus == hose->first_busno) {
		if (devfn != 0)
			return hose->cfg_data + U3_HT_CFA0(devfn, offset);
		*swap = 0;
		return ((void __iomem *)hose->cfg_addr) + (offset << 2);
	} else
		return hose->cfg_data + U3_HT_CFA1(bus, devfn, offset);
}

static int u3_ht_read_config(struct pci_bus *bus, unsigned int devfn,
				    int offset, int len, u32 *val)
{
	struct pci_controller *hose;
	void __iomem *addr;
	int swap;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (offset >= 0x100)
		return  PCIBIOS_BAD_REGISTER_NUMBER;
	addr = u3_ht_cfg_access(hose, bus->number, devfn, offset, &swap);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (u3_ht_skip_device(hose, bus, devfn)) {
	case 0:
		break;
	case 1:
		switch (len) {
		case 1:
			*val = 0xff; break;
		case 2:
			*val = 0xffff; break;
		default:
			*val = 0xfffffffful; break;
		}
		return PCIBIOS_SUCCESSFUL;
	default:
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		*val = in_8(addr);
		break;
	case 2:
		*val = swap ? in_le16(addr) : in_be16(addr);
		break;
	default:
		*val = swap ? in_le32(addr) : in_be32(addr);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int u3_ht_write_config(struct pci_bus *bus, unsigned int devfn,
				     int offset, int len, u32 val)
{
	struct pci_controller *hose;
	void __iomem *addr;
	int swap;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (offset >= 0x100)
		return  PCIBIOS_BAD_REGISTER_NUMBER;
	addr = u3_ht_cfg_access(hose, bus->number, devfn, offset, &swap);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (u3_ht_skip_device(hose, bus, devfn)) {
	case 0:
		break;
	case 1:
		return PCIBIOS_SUCCESSFUL;
	default:
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		out_8(addr, val);
		break;
	case 2:
		swap ? out_le16(addr, val) : out_be16(addr, val);
		break;
	default:
		swap ? out_le32(addr, val) : out_be32(addr, val);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops u3_ht_pci_ops =
{
	.read = u3_ht_read_config,
	.write = u3_ht_write_config,
};

#define U4_PCIE_CFA0(devfn, off)	\
	((1 << ((unsigned int)PCI_SLOT(dev_fn)))	\
	 | (((unsigned int)PCI_FUNC(dev_fn)) << 8)	\
	 | ((((unsigned int)(off)) >> 8) << 28) \
	 | (((unsigned int)(off)) & 0xfcU))

#define U4_PCIE_CFA1(bus, devfn, off)	\
	((((unsigned int)(bus)) << 16) \
	 |(((unsigned int)(devfn)) << 8)	\
	 | ((((unsigned int)(off)) >> 8) << 28) \
	 |(((unsigned int)(off)) & 0xfcU)	\
	 |1UL)

static volatile void __iomem *u4_pcie_cfg_access(struct pci_controller* hose,
					u8 bus, u8 dev_fn, int offset)
{
	unsigned int caddr;

	if (bus == hose->first_busno) {
		caddr = U4_PCIE_CFA0(dev_fn, offset);
	} else
		caddr = U4_PCIE_CFA1(bus, dev_fn, offset);

	/* Uninorth will return garbage if we don't read back the value ! */
	do {
		out_le32(hose->cfg_addr, caddr);
	} while (in_le32(hose->cfg_addr) != caddr);

	offset &= 0x03;
	return hose->cfg_data + offset;
}

static int u4_pcie_read_config(struct pci_bus *bus, unsigned int devfn,
			       int offset, int len, u32 *val)
{
	struct pci_controller *hose;
	volatile void __iomem *addr;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (offset >= 0x1000)
		return  PCIBIOS_BAD_REGISTER_NUMBER;
	addr = u4_pcie_cfg_access(hose, bus->number, devfn, offset);
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

static int u4_pcie_write_config(struct pci_bus *bus, unsigned int devfn,
				int offset, int len, u32 val)
{
	struct pci_controller *hose;
	volatile void __iomem *addr;

	hose = pci_bus_to_host(bus);
	if (hose == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (offset >= 0x1000)
		return  PCIBIOS_BAD_REGISTER_NUMBER;
	addr = u4_pcie_cfg_access(hose, bus->number, devfn, offset);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	switch (len) {
	case 1:
		out_8(addr, val);
		break;
	case 2:
		out_le16(addr, val);
		break;
	default:
		out_le32(addr, val);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops u4_pcie_pci_ops =
{
	.read = u4_pcie_read_config,
	.write = u4_pcie_write_config,
};

#endif /* CONFIG_PPC64 */

#ifdef CONFIG_PPC32
/*
 * For a bandit bridge, turn on cache coherency if necessary.
 * N.B. we could clean this up using the hose ops directly.
 */
static void __init init_bandit(struct pci_controller *bp)
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
static void __init init_p2pbridge(void)
{
	struct device_node *p2pbridge;
	struct pci_controller* hose;
	u8 bus, devfn;
	u16 val;

	/* XXX it would be better here to identify the specific
	   PCI-PCI bridge chip we have. */
	p2pbridge = of_find_node_by_name(NULL, "pci-bridge");
	if (p2pbridge == NULL
	    || p2pbridge->parent == NULL
	    || strcmp(p2pbridge->parent->name, "pci") != 0)
		goto done;
	if (pci_device_from_OF_node(p2pbridge, &bus, &devfn) < 0) {
		DBG("Can't find PCI infos for PCI<->PCI bridge\n");
		goto done;
	}
	/* Warning: At this point, we have not yet renumbered all busses.
	 * So we must use OF walking to find out hose
	 */
	hose = pci_find_hose_for_OF_device(p2pbridge);
	if (!hose) {
		DBG("Can't find hose for PCI<->PCI bridge\n");
		goto done;
	}
	if (early_read_config_word(hose, bus, devfn,
				   PCI_BRIDGE_CONTROL, &val) < 0) {
		printk(KERN_ERR "init_p2pbridge: couldn't read bridge"
		       " control\n");
		goto done;
	}
	val &= ~PCI_BRIDGE_CTL_MASTER_ABORT;
	early_write_config_word(hose, bus, devfn, PCI_BRIDGE_CONTROL, val);
done:
	of_node_put(p2pbridge);
}

static void __init init_second_ohare(void)
{
	struct device_node *np = of_find_node_by_name(NULL, "pci106b,7");
	unsigned char bus, devfn;
	unsigned short cmd;

	if (np == NULL)
		return;

	/* This must run before we initialize the PICs since the second
	 * ohare hosts a PIC that will be accessed there.
	 */
	if (pci_device_from_OF_node(np, &bus, &devfn) == 0) {
		struct pci_controller* hose =
			pci_find_hose_for_OF_device(np);
		if (!hose) {
			printk(KERN_ERR "Can't find PCI hose for OHare2 !\n");
			of_node_put(np);
			return;
		}
		early_read_config_word(hose, bus, devfn, PCI_COMMAND, &cmd);
		cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
		cmd &= ~PCI_COMMAND_IO;
		early_write_config_word(hose, bus, devfn, PCI_COMMAND, cmd);
	}
	has_second_ohare = 1;
	of_node_put(np);
}

/*
 * Some Apple desktop machines have a NEC PD720100A USB2 controller
 * on the motherboard. Open Firmware, on these, will disable the
 * EHCI part of it so it behaves like a pair of OHCI's. This fixup
 * code re-enables it ;)
 */
static void __init fixup_nec_usb2(void)
{
	struct device_node *nec;

	for (nec = NULL; (nec = of_find_node_by_name(nec, "usb")) != NULL;) {
		struct pci_controller *hose;
		u32 data;
		const u32 *prop;
		u8 bus, devfn;

		prop = of_get_property(nec, "vendor-id", NULL);
		if (prop == NULL)
			continue;
		if (0x1033 != *prop)
			continue;
		prop = of_get_property(nec, "device-id", NULL);
		if (prop == NULL)
			continue;
		if (0x0035 != *prop)
			continue;
		prop = of_get_property(nec, "reg", NULL);
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
			printk("Found NEC PD720100A USB2 chip with disabled"
			       " EHCI, fixing up...\n");
			data &= ~1UL;
			early_write_config_dword(hose, bus, devfn, 0xe4, data);
		}
	}
}

static void __init setup_bandit(struct pci_controller *hose,
				struct resource *addr)
{
	hose->ops = &macrisc_pci_ops;
	hose->cfg_addr = ioremap(addr->start + 0x800000, 0x1000);
	hose->cfg_data = ioremap(addr->start + 0xc00000, 0x1000);
	init_bandit(hose);
}

static int __init setup_uninorth(struct pci_controller *hose,
				 struct resource *addr)
{
	pci_add_flags(PCI_REASSIGN_ALL_BUS);
	has_uninorth = 1;
	hose->ops = &macrisc_pci_ops;
	hose->cfg_addr = ioremap(addr->start + 0x800000, 0x1000);
	hose->cfg_data = ioremap(addr->start + 0xc00000, 0x1000);
	/* We "know" that the bridge at f2000000 has the PCI slots. */
	return addr->start == 0xf2000000;
}
#endif /* CONFIG_PPC32 */

#ifdef CONFIG_PPC64
static void __init setup_u3_agp(struct pci_controller* hose)
{
	/* On G5, we move AGP up to high bus number so we don't need
	 * to reassign bus numbers for HT. If we ever have P2P bridges
	 * on AGP, we'll have to move pci_assign_all_busses to the
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

static void __init setup_u4_pcie(struct pci_controller* hose)
{
	/* We currently only implement the "non-atomic" config space, to
	 * be optimised later.
	 */
	hose->ops = &u4_pcie_pci_ops;
	hose->cfg_addr = ioremap(0xf0000000 + 0x800000, 0x1000);
	hose->cfg_data = ioremap(0xf0000000 + 0xc00000, 0x1000);

	/* The bus contains a bridge from root -> device, we need to
	 * make it visible on bus 0 so that we pick the right type
	 * of config cycles. If we didn't, we would have to force all
	 * config cycles to be type 1. So we override the "bus-range"
	 * property here
	 */
	hose->first_busno = 0x00;
	hose->last_busno = 0xff;
}

static void __init parse_region_decode(struct pci_controller *hose,
				       u32 decode)
{
	unsigned long base, end, next = -1;
	int i, cur = -1;

	/* Iterate through all bits. We ignore the last bit as this region is
	 * reserved for the ROM among other niceties
	 */
	for (i = 0; i < 31; i++) {
		if ((decode & (0x80000000 >> i)) == 0)
			continue;
		if (i < 16) {
			base = 0xf0000000 | (((u32)i) << 24);
			end = base + 0x00ffffff;
		} else {
			base = ((u32)i-16) << 28;
			end = base + 0x0fffffff;
		}
		if (base != next) {
			if (++cur >= 3) {
				printk(KERN_WARNING "PCI: Too many ranges !\n");
				break;
			}
			hose->mem_resources[cur].flags = IORESOURCE_MEM;
			hose->mem_resources[cur].name = hose->dn->full_name;
			hose->mem_resources[cur].start = base;
			hose->mem_resources[cur].end = end;
			DBG("  %d: 0x%08lx-0x%08lx\n", cur, base, end);
		} else {
			DBG("   :           -0x%08lx\n", end);
			hose->mem_resources[cur].end = end;
		}
		next = end + 1;
	}
}

static void __init setup_u3_ht(struct pci_controller* hose)
{
	struct device_node *np = hose->dn;
	struct resource cfg_res, self_res;
	u32 decode;

	hose->ops = &u3_ht_pci_ops;

	/* Get base addresses from OF tree
	 */
	if (of_address_to_resource(np, 0, &cfg_res) ||
	    of_address_to_resource(np, 1, &self_res)) {
		printk(KERN_ERR "PCI: Failed to get U3/U4 HT resources !\n");
		return;
	}

	/* Map external cfg space access into cfg_data and self registers
	 * into cfg_addr
	 */
	hose->cfg_data = ioremap(cfg_res.start, 0x02000000);
	hose->cfg_addr = ioremap(self_res.start, resource_size(&self_res));

	/*
	 * /ht node doesn't expose a "ranges" property, we read the register
	 * that controls the decoding logic and use that for memory regions.
	 * The IO region is hard coded since it is fixed in HW as well.
	 */
	hose->io_base_phys = 0xf4000000;
	hose->pci_io_size = 0x00400000;
	hose->io_resource.name = np->full_name;
	hose->io_resource.start = 0;
	hose->io_resource.end = 0x003fffff;
	hose->io_resource.flags = IORESOURCE_IO;
	hose->pci_mem_offset = 0;
	hose->first_busno = 0;
	hose->last_busno = 0xef;

	/* Note: fix offset when cfg_addr becomes a void * */
	decode = in_be32(hose->cfg_addr + 0x80);

	DBG("PCI: Apple HT bridge decode register: 0x%08x\n", decode);

	/* NOTE: The decode register setup is a bit weird... region
	 * 0xf8000000 for example is marked as enabled in there while it's
	 & actually the memory controller registers.
	 * That means that we are incorrectly attributing it to HT.
	 *
	 * In a similar vein, region 0xf4000000 is actually the HT IO space but
	 * also marked as enabled in here and 0xf9000000 is used by some other
	 * internal bits of the northbridge.
	 *
	 * Unfortunately, we can't just mask out those bit as we would end
	 * up with more regions than we can cope (linux can only cope with
	 * 3 memory regions for a PHB at this stage).
	 *
	 * So for now, we just do a little hack. We happen to -know- that
	 * Apple firmware doesn't assign things below 0xfa000000 for that
	 * bridge anyway so we mask out all bits we don't want.
	 */
	decode &= 0x003fffff;

	/* Now parse the resulting bits and build resources */
	parse_region_decode(hose, decode);
}
#endif /* CONFIG_PPC64 */

/*
 * We assume that if we have a G3 powermac, we have one bridge called
 * "pci" (a MPC106) and no bandit or chaos bridges, and contrariwise,
 * if we have one or more bandit or chaos bridges, we don't have a MPC106.
 */
static int __init pmac_add_bridge(struct device_node *dev)
{
	int len;
	struct pci_controller *hose;
	struct resource rsrc;
	char *disp_name;
	const int *bus_range;
	int primary = 1, has_address = 0;

	DBG("Adding PCI host bridge %s\n", dev->full_name);

	/* Fetch host bridge registers address */
	has_address = (of_address_to_resource(dev, 0, &rsrc) == 0);

	/* Get bus range if any */
	bus_range = of_get_property(dev, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		printk(KERN_WARNING "Can't get bus-range for %s, assume"
		       " bus 0\n", dev->full_name);
	}

	hose = pcibios_alloc_controller(dev);
	if (!hose)
		return -ENOMEM;
	hose->first_busno = bus_range ? bus_range[0] : 0;
	hose->last_busno = bus_range ? bus_range[1] : 0xff;

	disp_name = NULL;

	/* 64 bits only bridges */
#ifdef CONFIG_PPC64
	if (of_device_is_compatible(dev, "u3-agp")) {
		setup_u3_agp(hose);
		disp_name = "U3-AGP";
		primary = 0;
	} else if (of_device_is_compatible(dev, "u3-ht")) {
		setup_u3_ht(hose);
		disp_name = "U3-HT";
		primary = 1;
	} else if (of_device_is_compatible(dev, "u4-pcie")) {
		setup_u4_pcie(hose);
		disp_name = "U4-PCIE";
		primary = 0;
	}
	printk(KERN_INFO "Found %s PCI host bridge.  Firmware bus number:"
	       " %d->%d\n", disp_name, hose->first_busno, hose->last_busno);
#endif /* CONFIG_PPC64 */

	/* 32 bits only bridges */
#ifdef CONFIG_PPC32
	if (of_device_is_compatible(dev, "uni-north")) {
		primary = setup_uninorth(hose, &rsrc);
		disp_name = "UniNorth";
	} else if (strcmp(dev->name, "pci") == 0) {
		/* XXX assume this is a mpc106 (grackle) */
		setup_grackle(hose);
		disp_name = "Grackle (MPC106)";
	} else if (strcmp(dev->name, "bandit") == 0) {
		setup_bandit(hose, &rsrc);
		disp_name = "Bandit";
	} else if (strcmp(dev->name, "chaos") == 0) {
		setup_chaos(hose, &rsrc);
		disp_name = "Chaos";
		primary = 0;
	}
	printk(KERN_INFO "Found %s PCI host bridge at 0x%016llx. "
	       "Firmware bus number: %d->%d\n",
		disp_name, (unsigned long long)rsrc.start, hose->first_busno,
		hose->last_busno);
#endif /* CONFIG_PPC32 */

	DBG(" ->Hose at 0x%p, cfg_addr=0x%p,cfg_data=0x%p\n",
		hose, hose->cfg_addr, hose->cfg_data);

	/* Interpret the "ranges" property */
	/* This also maps the I/O region and sets isa_io/mem_base */
	pci_process_bridge_OF_ranges(hose, dev, primary);

	/* Fixup "bus-range" OF property */
	fixup_bus_range(dev);

	return 0;
}

void __devinit pmac_pci_irq_fixup(struct pci_dev *dev)
{
#ifdef CONFIG_PPC32
	/* Fixup interrupt for the modem/ethernet combo controller.
	 * on machines with a second ohare chip.
	 * The number in the device tree (27) is bogus (correct for
	 * the ethernet-only board but not the combo ethernet/modem
	 * board). The real interrupt is 28 on the second controller
	 * -> 28+32 = 60.
	 */
	if (has_second_ohare &&
	    dev->vendor == PCI_VENDOR_ID_DEC &&
	    dev->device == PCI_DEVICE_ID_DEC_TULIP_PLUS) {
		dev->irq = irq_create_mapping(NULL, 60);
		irq_set_irq_type(dev->irq, IRQ_TYPE_LEVEL_LOW);
	}
#endif /* CONFIG_PPC32 */
}

void __init pmac_pci_init(void)
{
	struct device_node *np, *root;
	struct device_node *ht = NULL;

	pci_set_flags(PCI_CAN_SKIP_ISA_ALIGN);

	root = of_find_node_by_path("/");
	if (root == NULL) {
		printk(KERN_CRIT "pmac_pci_init: can't find root "
		       "of device tree\n");
		return;
	}
	for (np = NULL; (np = of_get_next_child(root, np)) != NULL;) {
		if (np->name == NULL)
			continue;
		if (strcmp(np->name, "bandit") == 0
		    || strcmp(np->name, "chaos") == 0
		    || strcmp(np->name, "pci") == 0) {
			if (pmac_add_bridge(np) == 0)
				of_node_get(np);
		}
		if (strcmp(np->name, "ht") == 0) {
			of_node_get(np);
			ht = np;
		}
	}
	of_node_put(root);

#ifdef CONFIG_PPC64
	/* Probe HT last as it relies on the agp resources to be already
	 * setup
	 */
	if (ht && pmac_add_bridge(ht) != 0)
		of_node_put(ht);

	/* Setup the linkage between OF nodes and PHBs */
	pci_devs_phb_init();

	/* Fixup the PCI<->OF mapping for U3 AGP due to bus renumbering. We
	 * assume there is no P2P bridge on the AGP bus, which should be a
	 * safe assumptions for now. We should do something better in the
	 * future though
	 */
	if (u3_agp) {
		struct device_node *np = u3_agp->dn;
		PCI_DN(np)->busno = 0xf0;
		for (np = np->child; np; np = np->sibling)
			PCI_DN(np)->busno = 0xf0;
	}
	/* pmac_check_ht_link(); */

	/* We can allocate missing resources if any */
	pci_probe_only = 0;

#else /* CONFIG_PPC64 */
	init_p2pbridge();
	init_second_ohare();
	fixup_nec_usb2();

	/* We are still having some issues with the Xserve G4, enabling
	 * some offset between bus number and domains for now when we
	 * assign all busses should help for now
	 */
	if (pci_has_flag(PCI_REASSIGN_ALL_BUS))
		pcibios_assign_bus_offset = 0x10;
#endif
}

#ifdef CONFIG_PPC32
int pmac_pci_enable_device_hook(struct pci_dev *dev)
{
	struct device_node* node;
	int updatecfg = 0;
	int uninorth_child;

	node = pci_device_to_OF_node(dev);

	/* We don't want to enable USB controllers absent from the OF tree
	 * (iBook second controller)
	 */
	if (dev->vendor == PCI_VENDOR_ID_APPLE
	    && dev->class == PCI_CLASS_SERIAL_USB_OHCI
	    && !node) {
		printk(KERN_INFO "Apple USB OHCI %s disabled by firmware\n",
		       pci_name(dev));
		return -EINVAL;
	}

	if (!node)
		return 0;

	uninorth_child = node->parent &&
		of_device_is_compatible(node->parent, "uni-north");

	/* Firewire & GMAC were disabled after PCI probe, the driver is
	 * claiming them, we must re-enable them now.
	 */
	if (uninorth_child && !strcmp(node->name, "firewire") &&
	    (of_device_is_compatible(node, "pci106b,18") ||
	     of_device_is_compatible(node, "pci106b,30") ||
	     of_device_is_compatible(node, "pci11c1,5811"))) {
		pmac_call_feature(PMAC_FTR_1394_CABLE_POWER, node, 0, 1);
		pmac_call_feature(PMAC_FTR_1394_ENABLE, node, 0, 1);
		updatecfg = 1;
	}
	if (uninorth_child && !strcmp(node->name, "ethernet") &&
	    of_device_is_compatible(node, "gmac")) {
		pmac_call_feature(PMAC_FTR_GMAC_ENABLE, node, 0, 1);
		updatecfg = 1;
	}

	/*
	 * Fixup various header fields on 32 bits. We don't do that on
	 * 64 bits as some of these have strange values behind the HT
	 * bridge and we must not, for example, enable MWI or set the
	 * cache line size on them.
	 */
	if (updatecfg) {
		u16 cmd;

		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER
			| PCI_COMMAND_INVALIDATE;
		pci_write_config_word(dev, PCI_COMMAND, cmd);
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 16);

		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE,
				      L1_CACHE_BYTES >> 2);
	}

	return 0;
}

void __devinit pmac_pci_fixup_ohci(struct pci_dev *dev)
{
	struct device_node *node = pci_device_to_OF_node(dev);

	/* We don't want to assign resources to USB controllers
	 * absent from the OF tree (iBook second controller)
	 */
	if (dev->class == PCI_CLASS_SERIAL_USB_OHCI && !node)
		dev->resource[0].flags = 0;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_APPLE, PCI_ANY_ID, pmac_pci_fixup_ohci);

/* We power down some devices after they have been probed. They'll
 * be powered back on later on
 */
void __init pmac_pcibios_after_init(void)
{
	struct device_node* nd;

	for_each_node_by_name(nd, "firewire") {
		if (nd->parent && (of_device_is_compatible(nd, "pci106b,18") ||
				   of_device_is_compatible(nd, "pci106b,30") ||
				   of_device_is_compatible(nd, "pci11c1,5811"))
		    && of_device_is_compatible(nd->parent, "uni-north")) {
			pmac_call_feature(PMAC_FTR_1394_ENABLE, nd, 0, 0);
			pmac_call_feature(PMAC_FTR_1394_CABLE_POWER, nd, 0, 0);
		}
	}
	for_each_node_by_name(nd, "ethernet") {
		if (nd->parent && of_device_is_compatible(nd, "gmac")
		    && of_device_is_compatible(nd->parent, "uni-north"))
			pmac_call_feature(PMAC_FTR_GMAC_ENABLE, nd, 0, 0);
	}
}

void pmac_pci_fixup_cardbus(struct pci_dev* dev)
{
	if (!machine_is(powermac))
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
	if (!machine_is(powermac))
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
		printk(KERN_INFO "PCI: %s Forcing PCI IDE into native mode\n",
		       pci_name(dev));
		(void) pci_write_config_byte(dev, PCI_CLASS_PROG, progif|5);
		if (pci_read_config_byte(dev, PCI_CLASS_PROG, &progif) ||
		    (progif & 5) != 5)
			printk(KERN_ERR "Rewrite of PROGIF failed !\n");
		else {
			/* Clear IO BARs, they will be reassigned */
			pci_write_config_dword(dev, PCI_BASE_ADDRESS_0, 0);
			pci_write_config_dword(dev, PCI_BASE_ADDRESS_1, 0);
			pci_write_config_dword(dev, PCI_BASE_ADDRESS_2, 0);
			pci_write_config_dword(dev, PCI_BASE_ADDRESS_3, 0);
		}
	}
}
DECLARE_PCI_FIXUP_EARLY(PCI_ANY_ID, PCI_ANY_ID, pmac_pci_fixup_pciata);
#endif /* CONFIG_PPC32 */

/*
 * Disable second function on K2-SATA, it's broken
 * and disable IO BARs on first one
 */
static void fixup_k2_sata(struct pci_dev* dev)
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
			pci_write_config_dword(dev, PCI_BASE_ADDRESS_0 + 4 * i,
					       0);
		}
	} else {
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		cmd &= ~PCI_COMMAND_IO;
		pci_write_config_word(dev, PCI_COMMAND, cmd);
		for (i = 0; i < 5; i++) {
			dev->resource[i].start = dev->resource[i].end = 0;
			dev->resource[i].flags = 0;
			pci_write_config_dword(dev, PCI_BASE_ADDRESS_0 + 4 * i,
					       0);
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SERVERWORKS, 0x0240, fixup_k2_sata);

/*
 * On U4 (aka CPC945) the PCIe root complex "P2P" bridge resource ranges aren't
 * configured by the firmware. The bridge itself seems to ignore them but it
 * causes problems with Linux which then re-assigns devices below the bridge,
 * thus changing addresses of those devices from what was in the device-tree,
 * which sucks when those are video cards using offb
 *
 * We could just mark it transparent but I prefer fixing up the resources to
 * properly show what's going on here, as I have some doubts about having them
 * badly configured potentially being an issue for DMA.
 *
 * We leave PIO alone, it seems to be fine
 *
 * Oh and there's another funny bug. The OF properties advertize the region
 * 0xf1000000..0xf1ffffff as being forwarded as memory space. But that's
 * actually not true, this region is the memory mapped config space. So we
 * also need to filter it out or we'll map things in the wrong place.
 */
static void fixup_u4_pcie(struct pci_dev* dev)
{
	struct pci_controller *host = pci_bus_to_host(dev->bus);
	struct resource *region = NULL;
	u32 reg;
	int i;

	/* Only do that on PowerMac */
	if (!machine_is(powermac))
		return;

	/* Find the largest MMIO region */
	for (i = 0; i < 3; i++) {
		struct resource *r = &host->mem_resources[i];
		if (!(r->flags & IORESOURCE_MEM))
			continue;
		/* Skip the 0xf0xxxxxx..f2xxxxxx regions, we know they
		 * are reserved by HW for other things
		 */
		if (r->start >= 0xf0000000 && r->start < 0xf3000000)
			continue;
		if (!region || resource_size(r) > resource_size(region))
			region = r;
	}
	/* Nothing found, bail */
	if (region == 0)
		return;

	/* Print things out */
	printk(KERN_INFO "PCI: Fixup U4 PCIe bridge range: %pR\n", region);

	/* Fixup bridge config space. We know it's a Mac, resource aren't
	 * offset so let's just blast them as-is. We also know that they
	 * fit in 32 bits
	 */
	reg = ((region->start >> 16) & 0xfff0) | (region->end & 0xfff00000);
	pci_write_config_dword(dev, PCI_MEMORY_BASE, reg);
	pci_write_config_dword(dev, PCI_PREF_BASE_UPPER32, 0);
	pci_write_config_dword(dev, PCI_PREF_LIMIT_UPPER32, 0);
	pci_write_config_dword(dev, PCI_PREF_MEMORY_BASE, 0);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_U4_PCIE, fixup_u4_pcie);
