/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Christoph Hellwig (hch@lst.de)
 * Copyright (C) 1999, 2000, 04 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <asm/sn/arch.h>
#include <asm/pci/bridge.h>
#include <asm/paccess.h>
#include <asm/sn/intr.h>
#include <asm/sn/sn0/hub.h>

extern unsigned int allocate_irqno(void);

/*
 * Max #PCI busses we can handle; ie, max #PCI bridges.
 */
#define MAX_PCI_BUSSES		40

/*
 * Max #PCI devices (like scsi controllers) we handle on a bus.
 */
#define MAX_DEVICES_PER_PCIBUS	8

/*
 * XXX: No kmalloc available when we do our crosstalk scan,
 * 	we should try to move it later in the boot process.
 */
static struct bridge_controller bridges[MAX_PCI_BUSSES];

/*
 * Translate from irq to software PCI bus number and PCI slot.
 */
struct bridge_controller *irq_to_bridge[MAX_PCI_BUSSES * MAX_DEVICES_PER_PCIBUS];
int irq_to_slot[MAX_PCI_BUSSES * MAX_DEVICES_PER_PCIBUS];

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
	 * IOC3 is fucked fucked beyond believe ...  Don't even give the
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
	 * IOC3 is fucked fucked beyond believe ...  Don't even give the
	 * generic PCI code a chance to look at the wrong register.
	 */
	if ((where >= 0x14 && where < 0x40) || (where >= 0x48)) {
		*value = 0;
		return PCIBIOS_SUCCESSFUL;
	}

	/*
	 * IOC3 is fucked fucked beyond believe ...  Don't try to access
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
	 * IOC3 is fucked fucked beyond believe ...  Don't even give the
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
	 * IOC3 is fucked fucked beyond believe ...  Don't even give the
	 * generic PCI code a chance to look at the wrong register.
	 */
	if ((where >= 0x14 && where < 0x40) || (where >= 0x48)) {
		*value = 0;
		return PCIBIOS_SUCCESSFUL;
	}

	/*
	 * IOC3 is fucked fucked beyond believe ...  Don't try to access
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
	 * IOC3 is fucked fucked beyond believe ...  Don't even give the
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
	 * IOC3 is fucked fucked beyond believe ...  Don't even give the
	 * generic PCI code a chance to touch the wrong register.
	 */
	if ((where >= 0x14 && where < 0x40) || (where >= 0x48))
		return PCIBIOS_SUCCESSFUL;

	/*
	 * IOC3 is fucked fucked beyond believe ...  Don't try to access
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
	 * IOC3 is fucked fucked beyond believe ...  Don't even give the
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
	 * IOC3 is fucked fucked beyond believe ...  Don't even give the
	 * generic PCI code a chance to touch the wrong register.
	 */
	if ((where >= 0x14 && where < 0x40) || (where >= 0x48))
		return PCIBIOS_SUCCESSFUL;

	/*
	 * IOC3 is fucked fucked beyond believe ...  Don't try to access
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

static struct pci_ops bridge_pci_ops = {
	.read = pci_read_config,
	.write = pci_write_config,
};

int __init bridge_probe(nasid_t nasid, int widget_id, int masterwid)
{
	unsigned long offset = NODE_OFFSET(nasid);
	struct bridge_controller *bc;
	static int num_bridges = 0;
	bridge_t *bridge;
	int slot;

	printk("a bridge\n");

	/* XXX: kludge alert.. */
	if (!num_bridges)
		ioport_resource.end = ~0UL;

	bc = &bridges[num_bridges];

	bc->pc.pci_ops		= &bridge_pci_ops;
	bc->pc.mem_resource	= &bc->mem;
	bc->pc.io_resource	= &bc->io;

	bc->pc.index		= num_bridges;

	bc->mem.name		= "Bridge PCI MEM";
	bc->pc.mem_offset	= offset;
	bc->mem.start		= 0;
	bc->mem.end		= ~0UL;
	bc->mem.flags		= IORESOURCE_MEM;

	bc->io.name		= "Bridge IO MEM";
	bc->pc.io_offset	= offset;
	bc->io.start		= 0UL;
	bc->io.end		= ~0UL;
	bc->io.flags		= IORESOURCE_IO;

	bc->irq_cpu = smp_processor_id();
	bc->widget_id = widget_id;
	bc->nasid = nasid;

	bc->baddr = (u64)masterwid << 60;
	bc->baddr |= (1UL << 56);	/* Barrier set */

	/*
	 * point to this bridge
	 */
	bridge = (bridge_t *) RAW_NODE_SWIN_BASE(nasid, widget_id);

	/*
	 * Clear all pending interrupts.
	 */
	bridge->b_int_rst_stat = BRIDGE_IRR_ALL_CLR;

	/*
	 * Until otherwise set up, assume all interrupts are from slot 0
	 */
	bridge->b_int_device = 0x0;

	/*
	 * swap pio's to pci mem and io space (big windows)
	 */
	bridge->b_wid_control |= BRIDGE_CTRL_IO_SWAP |
	                         BRIDGE_CTRL_MEM_SWAP;

	/*
	 * Hmm...  IRIX sets additional bits in the address which
	 * are documented as reserved in the bridge docs.
	 */
	bridge->b_wid_int_upper = 0x8000 | (masterwid << 16);
	bridge->b_wid_int_lower = 0x01800090;	/* PI_INT_PEND_MOD off*/
	bridge->b_dir_map = (masterwid << 20);	/* DMA */
	bridge->b_int_enable = 0;

	for (slot = 0; slot < 8; slot ++) {
		bridge->b_device[slot].reg |= BRIDGE_DEV_SWAP_DIR;
		bc->pci_int[slot] = -1;
	}
	bridge->b_wid_tflush;     /* wait until Bridge PIO complete */

	bc->base = bridge;

	register_pci_controller(&bc->pc);

	num_bridges++;

	return 0;
}

/*
 * All observed requests have pin == 1. We could have a global here, that
 * gets incremented and returned every time - unfortunately, pci_map_irq
 * may be called on the same device over and over, and need to return the
 * same value. On O2000, pin can be 0 or 1, and PCI slots can be [0..7].
 *
 * A given PCI device, in general, should be able to intr any of the cpus
 * on any one of the hubs connected to its xbow.
 */
int __devinit pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(dev->bus);
	int irq = bc->pci_int[slot];

	if (irq == -1) {
		irq = bc->pci_int[slot] = request_bridge_irq(bc);
		if (irq < 0)
			panic("Can't allocate interrupt for PCI device %s\n",
			      pci_name(dev));
	}

	irq_to_bridge[irq] = bc;
	irq_to_slot[irq] = slot;

	return irq;
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

/*
 * Device might live on a subordinate PCI bus.  XXX Walk up the chain of buses
 * to find the slot number in sense of the bridge device register.
 * XXX This also means multiple devices might rely on conflicting bridge
 * settings.
 */

static inline void pci_disable_swapping(struct pci_dev *dev)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(dev->bus);
	bridge_t *bridge = bc->base;
	int slot = PCI_SLOT(dev->devfn);

	/* Turn off byte swapping */
	bridge->b_device[slot].reg &= ~BRIDGE_DEV_SWAP_DIR;
	bridge->b_widget.w_tflush;	/* Flush */
}

static inline void pci_enable_swapping(struct pci_dev *dev)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(dev->bus);
	bridge_t *bridge = bc->base;
	int slot = PCI_SLOT(dev->devfn);

	/* Turn on byte swapping */
	bridge->b_device[slot].reg |= BRIDGE_DEV_SWAP_DIR;
	bridge->b_widget.w_tflush;	/* Flush */
}

static void __init pci_fixup_ioc3(struct pci_dev *d)
{
	pci_disable_swapping(d);
}

int pcibus_to_node(struct pci_bus *bus)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);

	return bc->nasid;
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SGI, PCI_DEVICE_ID_SGI_IOC3,
	pci_fixup_ioc3);
