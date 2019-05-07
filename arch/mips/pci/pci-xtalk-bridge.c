// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2003 Christoph Hellwig (hch@lst.de)
 * Copyright (C) 1999, 2000, 04 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/smp.h>
#include <linux/dma-direct.h>
#include <linux/platform_device.h>
#include <linux/platform_data/xtalk-bridge.h>

#include <asm/pci/bridge.h>
#include <asm/paccess.h>
#include <asm/sn/intr.h>

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

static void bridge_disable_swapping(struct pci_dev *dev)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(dev->bus);
	int slot = PCI_SLOT(dev->devfn);

	/* Turn off byte swapping */
	bridge_clr(bc, b_device[slot].reg, BRIDGE_DEV_SWAP_DIR);
	bridge_read(bc, b_widget.w_tflush);	/* Flush */
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SGI, PCI_DEVICE_ID_SGI_IOC3,
	bridge_disable_swapping);


/*
 * The Bridge ASIC supports both type 0 and type 1 access.  Type 1 is
 * not really documented, so right now I can't write code which uses it.
 * Therefore we use type 0 accesses for now even though they won't work
 * correctly for PCI-to-PCI bridges.
 *
 * The function is complicated by the ultimate brokenness of the IOC3 chip
 * which is used in SGI systems.  The IOC3 can only handle 32-bit PCI
 * accesses and does only decode parts of it's address space.
 */
static int pci_conf0_read_config(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *value)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);
	struct bridge_regs *bridge = bc->base;
	int slot = PCI_SLOT(devfn);
	int fn = PCI_FUNC(devfn);
	void *addr;
	u32 cf, shift, mask;
	int res;

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].c[PCI_VENDOR_ID];
	if (get_dbe(cf, (u32 *)addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * IOC3 is broken beyond belief ...  Don't even give the
	 * generic PCI code a chance to look at it for real ...
	 */
	if (cf == (PCI_VENDOR_ID_SGI | (PCI_DEVICE_ID_SGI_IOC3 << 16)))
		goto is_ioc3;

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].c[where ^ (4 - size)];

	if (size == 1)
		res = get_dbe(*value, (u8 *)addr);
	else if (size == 2)
		res = get_dbe(*value, (u16 *)addr);
	else
		res = get_dbe(*value, (u32 *)addr);

	return res ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;

is_ioc3:

	/*
	 * IOC3 special handling
	 */
	if ((where >= 0x14 && where < 0x40) || (where >= 0x48)) {
		*value = emulate_ioc3_cfg(where, size);
		return PCIBIOS_SUCCESSFUL;
	}

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].l[where >> 2];
	if (get_dbe(cf, (u32 *)addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	shift = ((where & 3) << 3);
	mask = (0xffffffffU >> ((4 - size) << 3));
	*value = (cf >> shift) & mask;

	return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_read_config(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *value)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);
	struct bridge_regs *bridge = bc->base;
	int busno = bus->number;
	int slot = PCI_SLOT(devfn);
	int fn = PCI_FUNC(devfn);
	void *addr;
	u32 cf, shift, mask;
	int res;

	bridge_write(bc, b_pci_cfg, (busno << 16) | (slot << 11));
	addr = &bridge->b_type1_cfg.c[(fn << 8) | PCI_VENDOR_ID];
	if (get_dbe(cf, (u32 *)addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * IOC3 is broken beyond belief ...  Don't even give the
	 * generic PCI code a chance to look at it for real ...
	 */
	if (cf == (PCI_VENDOR_ID_SGI | (PCI_DEVICE_ID_SGI_IOC3 << 16)))
		goto is_ioc3;

	addr = &bridge->b_type1_cfg.c[(fn << 8) | (where ^ (4 - size))];

	if (size == 1)
		res = get_dbe(*value, (u8 *)addr);
	else if (size == 2)
		res = get_dbe(*value, (u16 *)addr);
	else
		res = get_dbe(*value, (u32 *)addr);

	return res ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;

is_ioc3:

	/*
	 * IOC3 special handling
	 */
	if ((where >= 0x14 && where < 0x40) || (where >= 0x48)) {
		*value = emulate_ioc3_cfg(where, size);
		return PCIBIOS_SUCCESSFUL;
	}

	addr = &bridge->b_type1_cfg.c[(fn << 8) | where];
	if (get_dbe(cf, (u32 *)addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	shift = ((where & 3) << 3);
	mask = (0xffffffffU >> ((4 - size) << 3));
	*value = (cf >> shift) & mask;

	return PCIBIOS_SUCCESSFUL;
}

static int pci_read_config(struct pci_bus *bus, unsigned int devfn,
			   int where, int size, u32 *value)
{
	if (!pci_is_root_bus(bus))
		return pci_conf1_read_config(bus, devfn, where, size, value);

	return pci_conf0_read_config(bus, devfn, where, size, value);
}

static int pci_conf0_write_config(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 value)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);
	struct bridge_regs *bridge = bc->base;
	int slot = PCI_SLOT(devfn);
	int fn = PCI_FUNC(devfn);
	void *addr;
	u32 cf, shift, mask, smask;
	int res;

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].c[PCI_VENDOR_ID];
	if (get_dbe(cf, (u32 *)addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * IOC3 is broken beyond belief ...  Don't even give the
	 * generic PCI code a chance to look at it for real ...
	 */
	if (cf == (PCI_VENDOR_ID_SGI | (PCI_DEVICE_ID_SGI_IOC3 << 16)))
		goto is_ioc3;

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].c[where ^ (4 - size)];

	if (size == 1)
		res = put_dbe(value, (u8 *)addr);
	else if (size == 2)
		res = put_dbe(value, (u16 *)addr);
	else
		res = put_dbe(value, (u32 *)addr);

	if (res)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;

is_ioc3:

	/*
	 * IOC3 special handling
	 */
	if ((where >= 0x14 && where < 0x40) || (where >= 0x48))
		return PCIBIOS_SUCCESSFUL;

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].l[where >> 2];

	if (get_dbe(cf, (u32 *)addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	shift = ((where & 3) << 3);
	mask = (0xffffffffU >> ((4 - size) << 3));
	smask = mask << shift;

	cf = (cf & ~smask) | ((value & mask) << shift);
	if (put_dbe(cf, (u32 *)addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_write_config(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 value)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);
	struct bridge_regs *bridge = bc->base;
	int slot = PCI_SLOT(devfn);
	int fn = PCI_FUNC(devfn);
	int busno = bus->number;
	void *addr;
	u32 cf, shift, mask, smask;
	int res;

	bridge_write(bc, b_pci_cfg, (busno << 16) | (slot << 11));
	addr = &bridge->b_type1_cfg.c[(fn << 8) | PCI_VENDOR_ID];
	if (get_dbe(cf, (u32 *)addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * IOC3 is broken beyond belief ...  Don't even give the
	 * generic PCI code a chance to look at it for real ...
	 */
	if (cf == (PCI_VENDOR_ID_SGI | (PCI_DEVICE_ID_SGI_IOC3 << 16)))
		goto is_ioc3;

	addr = &bridge->b_type1_cfg.c[(fn << 8) | (where ^ (4 - size))];

	if (size == 1)
		res = put_dbe(value, (u8 *)addr);
	else if (size == 2)
		res = put_dbe(value, (u16 *)addr);
	else
		res = put_dbe(value, (u32 *)addr);

	if (res)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;

is_ioc3:

	/*
	 * IOC3 special handling
	 */
	if ((where >= 0x14 && where < 0x40) || (where >= 0x48))
		return PCIBIOS_SUCCESSFUL;

	addr = &bridge->b_type0_cfg_dev[slot].f[fn].l[where >> 2];
	if (get_dbe(cf, (u32 *)addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	shift = ((where & 3) << 3);
	mask = (0xffffffffU >> ((4 - size) << 3));
	smask = mask << shift;

	cf = (cf & ~smask) | ((value & mask) << shift);
	if (put_dbe(cf, (u32 *)addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static int pci_write_config(struct pci_bus *bus, unsigned int devfn,
	int where, int size, u32 value)
{
	if (!pci_is_root_bus(bus))
		return pci_conf1_write_config(bus, devfn, where, size, value);

	return pci_conf0_write_config(bus, devfn, where, size, value);
}

static struct pci_ops bridge_pci_ops = {
	.read	 = pci_read_config,
	.write	 = pci_write_config,
};

/*
 * All observed requests have pin == 1. We could have a global here, that
 * gets incremented and returned every time - unfortunately, pci_map_irq
 * may be called on the same device over and over, and need to return the
 * same value. On O2000, pin can be 0 or 1, and PCI slots can be [0..7].
 *
 * A given PCI device, in general, should be able to intr any of the cpus
 * on any one of the hubs connected to its xbow.
 */
static int bridge_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(dev->bus);
	int irq;

	irq = bc->pci_int[slot];
	if (irq == -1) {
		irq = request_bridge_irq(bc, slot);
		if (irq < 0)
			return irq;

		bc->pci_int[slot] = irq;
	}
	return irq;
}

static int bridge_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bridge_controller *bc;
	struct pci_host_bridge *host;
	int slot;
	int err;
	struct xtalk_bridge_platform_data *bd = dev_get_platdata(&pdev->dev);

	pci_set_flags(PCI_PROBE_ONLY);

	host = devm_pci_alloc_host_bridge(dev, sizeof(*bc));
	if (!host)
		return -ENOMEM;

	bc = pci_host_bridge_priv(host);

	bc->busn.name		= "Bridge PCI busn";
	bc->busn.start		= 0;
	bc->busn.end		= 0xff;
	bc->busn.flags		= IORESOURCE_BUS;

	pci_add_resource_offset(&host->windows, &bd->mem, bd->mem_offset);
	pci_add_resource_offset(&host->windows, &bd->io, bd->io_offset);
	pci_add_resource(&host->windows, &bc->busn);

	err = devm_request_pci_bus_resources(dev, &host->windows);
	if (err < 0) {
		pci_free_resource_list(&host->windows);
		return err;
	}

	bc->nasid = bd->nasid;

	bc->baddr = (u64)bd->masterwid << 60 | PCI64_ATTR_BAR;
	bc->base = (struct bridge_regs *)bd->bridge_addr;
	bc->intr_addr = bd->intr_addr;

	/*
	 * Clear all pending interrupts.
	 */
	bridge_write(bc, b_int_rst_stat, BRIDGE_IRR_ALL_CLR);

	/*
	 * Until otherwise set up, assume all interrupts are from slot 0
	 */
	bridge_write(bc, b_int_device, 0x0);

	/*
	 * disable swapping for big windows
	 */
	bridge_clr(bc, b_wid_control,
		   BRIDGE_CTRL_IO_SWAP | BRIDGE_CTRL_MEM_SWAP);
#ifdef CONFIG_PAGE_SIZE_4KB
	bridge_clr(bc, b_wid_control, BRIDGE_CTRL_PAGE_SIZE);
#else /* 16kB or larger */
	bridge_set(bc, b_wid_control, BRIDGE_CTRL_PAGE_SIZE);
#endif

	/*
	 * Hmm...  IRIX sets additional bits in the address which
	 * are documented as reserved in the bridge docs.
	 */
	bridge_write(bc, b_wid_int_upper,
		     ((bc->intr_addr >> 32) & 0xffff) | (bd->masterwid << 16));
	bridge_write(bc, b_wid_int_lower, bc->intr_addr & 0xffffffff);
	bridge_write(bc, b_dir_map, (bd->masterwid << 20));	/* DMA */
	bridge_write(bc, b_int_enable, 0);

	for (slot = 0; slot < 8; slot++) {
		bridge_set(bc, b_device[slot].reg, BRIDGE_DEV_SWAP_DIR);
		bc->pci_int[slot] = -1;
	}
	bridge_read(bc, b_wid_tflush);	  /* wait until Bridge PIO complete */

	host->dev.parent = dev;
	host->sysdata = bc;
	host->busnr = 0;
	host->ops = &bridge_pci_ops;
	host->map_irq = bridge_map_irq;
	host->swizzle_irq = pci_common_swizzle;

	err = pci_scan_root_bus_bridge(host);
	if (err < 0)
		return err;

	pci_bus_claim_resources(host->bus);
	pci_bus_add_devices(host->bus);

	platform_set_drvdata(pdev, host->bus);

	return 0;
}

static int bridge_remove(struct platform_device *pdev)
{
	struct pci_bus *bus = platform_get_drvdata(pdev);

	pci_lock_rescan_remove();
	pci_stop_root_bus(bus);
	pci_remove_root_bus(bus);
	pci_unlock_rescan_remove();

	return 0;
}

static struct platform_driver bridge_driver = {
	.probe  = bridge_probe,
	.remove = bridge_remove,
	.driver = {
		.name = "xtalk-bridge",
	}
};

builtin_platform_driver(bridge_driver);
