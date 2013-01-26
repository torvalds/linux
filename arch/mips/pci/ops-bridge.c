/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999, 2000, 04, 06 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/pci.h>
#include <asm/paccess.h>
#include <asm/pci/bridge.h>
#include <asm/sn/arch.h>
#include <asm/sn/intr.h>
#include <asm/sn/sn0/hub.h>

/*
 * Most of the IOC3 PCI config register aren't present
 * we emulate what is needed for a normal PCI enumeration
 */
static u32 emulate_ioc3_cfg(int where, int size)
{
	if (size == 1 && where == 0x3d)
		return 0x01;
	else if (size == 2 && where == 0x3c)
		return 0x0100;
	else if (size == 4 && where == 0x3c)
		return 0x00000100;

	return 0;
}

/*
 * The Bridge ASIC supports both type 0 and type 1 access.  Type 1 is
 * not really documented, so right now I can't write code which uses it.
 * Therefore we use type 0 accesses for now even though they won't work
 * correcly for PCI-to-PCI bridges.
 *
 * The function is complicated by the ultimate brokeness of the IOC3 chip
 * which is used in SGI systems.  The IOC3 can only handle 32-bit PCI
 * accesses and does only decode parts of it's address space.
 */

static int pci_conf0_read_config(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 * value)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);
	bridge_t *bridge = bc->base;
	int slot = PCI_SLOT(devfn);
	int fn = PCI_FUNC(devfn);
	volatile void *addr;
	u32 cf, shift, mask;
	int res;

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].c[PCI_VENDOR_ID];
	if (get_dbe(cf, (u32 *) addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * IOC3 is fucking fucked beyond belief ...  Don't even give the
	 * generic PCI code a chance to look at it for real ...
	 */
	if (cf == (PCI_VENDOR_ID_SGI | (PCI_DEVICE_ID_SGI_IOC3 << 16)))
		goto oh_my_gawd;

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].c[where ^ (4 - size)];

	if (size == 1)
		res = get_dbe(*value, (u8 *) addr);
	else if (size == 2)
		res = get_dbe(*value, (u16 *) addr);
	else
		res = get_dbe(*value, (u32 *) addr);

	return res ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;

oh_my_gawd:

	/*
	 * IOC3 is fucking fucked beyond belief ...  Don't even give the
	 * generic PCI code a chance to look at the wrong register.
	 */
	if ((where >= 0x14 && where < 0x40) || (where >= 0x48)) {
		*value = emulate_ioc3_cfg(where, size);
		return PCIBIOS_SUCCESSFUL;
	}

	/*
	 * IOC3 is fucking fucked beyond belief ...  Don't try to access
	 * anything but 32-bit words ...
	 */
	addr = &bridge->b_type0_cfg_dev[slot].f[fn].l[where >> 2];

	if (get_dbe(cf, (u32 *) addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	shift = ((where & 3) << 3);
	mask = (0xffffffffU >> ((4 - size) << 3));
	*value = (cf >> shift) & mask;

	return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_read_config(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 * value)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);
	bridge_t *bridge = bc->base;
	int busno = bus->number;
	int slot = PCI_SLOT(devfn);
	int fn = PCI_FUNC(devfn);
	volatile void *addr;
	u32 cf, shift, mask;
	int res;

	bridge->b_pci_cfg = (busno << 16) | (slot << 11);
	addr = &bridge->b_type1_cfg.c[(fn << 8) | PCI_VENDOR_ID];
	if (get_dbe(cf, (u32 *) addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * IOC3 is fucking fucked beyond belief ...  Don't even give the
	 * generic PCI code a chance to look at it for real ...
	 */
	if (cf == (PCI_VENDOR_ID_SGI | (PCI_DEVICE_ID_SGI_IOC3 << 16)))
		goto oh_my_gawd;

	bridge->b_pci_cfg = (busno << 16) | (slot << 11);
	addr = &bridge->b_type1_cfg.c[(fn << 8) | (where ^ (4 - size))];

	if (size == 1)
		res = get_dbe(*value, (u8 *) addr);
	else if (size == 2)
		res = get_dbe(*value, (u16 *) addr);
	else
		res = get_dbe(*value, (u32 *) addr);

	return res ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;

oh_my_gawd:

	/*
	 * IOC3 is fucking fucked beyond belief ...  Don't even give the
	 * generic PCI code a chance to look at the wrong register.
	 */
	if ((where >= 0x14 && where < 0x40) || (where >= 0x48)) {
		*value = emulate_ioc3_cfg(where, size);
		return PCIBIOS_SUCCESSFUL;
	}

	/*
	 * IOC3 is fucking fucked beyond belief ...  Don't try to access
	 * anything but 32-bit words ...
	 */
	bridge->b_pci_cfg = (busno << 16) | (slot << 11);
	addr = &bridge->b_type1_cfg.c[(fn << 8) | where];

	if (get_dbe(cf, (u32 *) addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	shift = ((where & 3) << 3);
	mask = (0xffffffffU >> ((4 - size) << 3));
	*value = (cf >> shift) & mask;

	return PCIBIOS_SUCCESSFUL;
}

static int pci_read_config(struct pci_bus *bus, unsigned int devfn,
			   int where, int size, u32 * value)
{
	if (bus->number > 0)
		return pci_conf1_read_config(bus, devfn, where, size, value);

	return pci_conf0_read_config(bus, devfn, where, size, value);
}

static int pci_conf0_write_config(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 value)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);
	bridge_t *bridge = bc->base;
	int slot = PCI_SLOT(devfn);
	int fn = PCI_FUNC(devfn);
	volatile void *addr;
	u32 cf, shift, mask, smask;
	int res;

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].c[PCI_VENDOR_ID];
	if (get_dbe(cf, (u32 *) addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * IOC3 is fucking fucked beyond belief ...  Don't even give the
	 * generic PCI code a chance to look at it for real ...
	 */
	if (cf == (PCI_VENDOR_ID_SGI | (PCI_DEVICE_ID_SGI_IOC3 << 16)))
		goto oh_my_gawd;

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].c[where ^ (4 - size)];

	if (size == 1) {
		res = put_dbe(value, (u8 *) addr);
	} else if (size == 2) {
		res = put_dbe(value, (u16 *) addr);
	} else {
		res = put_dbe(value, (u32 *) addr);
	}

	if (res)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;

oh_my_gawd:

	/*
	 * IOC3 is fucking fucked beyond belief ...  Don't even give the
	 * generic PCI code a chance to touch the wrong register.
	 */
	if ((where >= 0x14 && where < 0x40) || (where >= 0x48))
		return PCIBIOS_SUCCESSFUL;

	/*
	 * IOC3 is fucking fucked beyond belief ...  Don't try to access
	 * anything but 32-bit words ...
	 */
	addr = &bridge->b_type0_cfg_dev[slot].f[fn].l[where >> 2];

	if (get_dbe(cf, (u32 *) addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	shift = ((where & 3) << 3);
	mask = (0xffffffffU >> ((4 - size) << 3));
	smask = mask << shift;

	cf = (cf & ~smask) | ((value & mask) << shift);
	if (put_dbe(cf, (u32 *) addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_write_config(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 value)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);
	bridge_t *bridge = bc->base;
	int slot = PCI_SLOT(devfn);
	int fn = PCI_FUNC(devfn);
	int busno = bus->number;
	volatile void *addr;
	u32 cf, shift, mask, smask;
	int res;

	bridge->b_pci_cfg = (busno << 16) | (slot << 11);
	addr = &bridge->b_type1_cfg.c[(fn << 8) | PCI_VENDOR_ID];
	if (get_dbe(cf, (u32 *) addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * IOC3 is fucking fucked beyond belief ...  Don't even give the
	 * generic PCI code a chance to look at it for real ...
	 */
	if (cf == (PCI_VENDOR_ID_SGI | (PCI_DEVICE_ID_SGI_IOC3 << 16)))
		goto oh_my_gawd;

	addr = &bridge->b_type1_cfg.c[(fn << 8) | (where ^ (4 - size))];

	if (size == 1) {
		res = put_dbe(value, (u8 *) addr);
	} else if (size == 2) {
		res = put_dbe(value, (u16 *) addr);
	} else {
		res = put_dbe(value, (u32 *) addr);
	}

	if (res)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;

oh_my_gawd:

	/*
	 * IOC3 is fucking fucked beyond belief ...  Don't even give the
	 * generic PCI code a chance to touch the wrong register.
	 */
	if ((where >= 0x14 && where < 0x40) || (where >= 0x48))
		return PCIBIOS_SUCCESSFUL;

	/*
	 * IOC3 is fucking fucked beyond belief ...  Don't try to access
	 * anything but 32-bit words ...
	 */
	addr = &bridge->b_type0_cfg_dev[slot].f[fn].l[where >> 2];

	if (get_dbe(cf, (u32 *) addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	shift = ((where & 3) << 3);
	mask = (0xffffffffU >> ((4 - size) << 3));
	smask = mask << shift;

	cf = (cf & ~smask) | ((value & mask) << shift);
	if (put_dbe(cf, (u32 *) addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static int pci_write_config(struct pci_bus *bus, unsigned int devfn,
	int where, int size, u32 value)
{
	if (bus->number > 0)
		return pci_conf1_write_config(bus, devfn, where, size, value);

	return pci_conf0_write_config(bus, devfn, where, size, value);
}

struct pci_ops bridge_pci_ops = {
	.read	= pci_read_config,
	.write	= pci_write_config,
};
