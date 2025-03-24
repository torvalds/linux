/*
 * Broadcom specific AMBA
 * PCI Core in hostmode
 *
 * Copyright 2005 - 2011, Broadcom Corporation
 * Copyright 2006, 2007, Michael Buesch <m@bues.ch>
 * Copyright 2011, 2012, Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/bcma/bcma.h>
#include <asm/paccess.h>

/* Probe a 32bit value on the bus and catch bus exceptions.
 * Returns nonzero on a bus exception.
 * This is MIPS specific */
#define mips_busprobe32(val, addr)	get_dbe((val), ((u32 *)(addr)))

/* Assume one-hot slot wiring */
#define BCMA_PCI_SLOT_MAX	16
#define	PCI_CONFIG_SPACE_SIZE	256

bool bcma_core_pci_is_in_hostmode(struct bcma_drv_pci *pc)
{
	struct bcma_bus *bus = pc->core->bus;
	u16 chipid_top;
	u32 tmp;

	chipid_top = (bus->chipinfo.id & 0xFF00);
	if (chipid_top != 0x4700 &&
	    chipid_top != 0x5300)
		return false;

	bcma_core_enable(pc->core, 0);

	return !mips_busprobe32(tmp, pc->core->io_addr);
}

static u32 bcma_pcie_read_config(struct bcma_drv_pci *pc, u32 address)
{
	pcicore_write32(pc, BCMA_CORE_PCI_CONFIG_ADDR, address);
	pcicore_read32(pc, BCMA_CORE_PCI_CONFIG_ADDR);
	return pcicore_read32(pc, BCMA_CORE_PCI_CONFIG_DATA);
}

static void bcma_pcie_write_config(struct bcma_drv_pci *pc, u32 address,
				   u32 data)
{
	pcicore_write32(pc, BCMA_CORE_PCI_CONFIG_ADDR, address);
	pcicore_read32(pc, BCMA_CORE_PCI_CONFIG_ADDR);
	pcicore_write32(pc, BCMA_CORE_PCI_CONFIG_DATA, data);
}

static u32 bcma_get_cfgspace_addr(struct bcma_drv_pci *pc, unsigned int dev,
			     unsigned int func, unsigned int off)
{
	u32 addr = 0;

	/* Issue config commands only when the data link is up (at least
	 * one external pcie device is present).
	 */
	if (dev >= 2 || !(bcma_pcie_read(pc, BCMA_CORE_PCI_DLLP_LSREG)
			  & BCMA_CORE_PCI_DLLP_LSREG_LINKUP))
		goto out;

	/* Type 0 transaction */
	/* Slide the PCI window to the appropriate slot */
	pcicore_write32(pc, BCMA_CORE_PCI_SBTOPCI1, BCMA_CORE_PCI_SBTOPCI_CFG0);
	/* Calculate the address */
	addr = pc->host_controller->host_cfg_addr;
	addr |= (dev << BCMA_CORE_PCI_CFG_SLOT_SHIFT);
	addr |= (func << BCMA_CORE_PCI_CFG_FUN_SHIFT);
	addr |= (off & ~3);

out:
	return addr;
}

static int bcma_extpci_read_config(struct bcma_drv_pci *pc, unsigned int dev,
				  unsigned int func, unsigned int off,
				  void *buf, int len)
{
	int err = -EINVAL;
	u32 addr, val;
	void __iomem *mmio = 0;

	WARN_ON(!pc->hostmode);
	if (unlikely(len != 1 && len != 2 && len != 4))
		goto out;
	if (dev == 0) {
		/* we support only two functions on device 0 */
		if (func > 1)
			goto out;

		/* accesses to config registers with offsets >= 256
		 * requires indirect access.
		 */
		if (off >= PCI_CONFIG_SPACE_SIZE) {
			addr = (func << 12);
			addr |= (off & 0x0FFC);
			val = bcma_pcie_read_config(pc, addr);
		} else {
			addr = BCMA_CORE_PCI_PCICFG0;
			addr |= (func << 8);
			addr |= (off & 0xFC);
			val = pcicore_read32(pc, addr);
		}
	} else {
		addr = bcma_get_cfgspace_addr(pc, dev, func, off);
		if (unlikely(!addr))
			goto out;
		err = -ENOMEM;
		mmio = ioremap(addr, sizeof(val));
		if (!mmio)
			goto out;

		if (mips_busprobe32(val, mmio)) {
			val = 0xFFFFFFFF;
			goto unmap;
		}
	}
	val >>= (8 * (off & 3));

	switch (len) {
	case 1:
		*((u8 *)buf) = (u8)val;
		break;
	case 2:
		*((u16 *)buf) = (u16)val;
		break;
	case 4:
		*((u32 *)buf) = (u32)val;
		break;
	}
	err = 0;
unmap:
	if (mmio)
		iounmap(mmio);
out:
	return err;
}

static int bcma_extpci_write_config(struct bcma_drv_pci *pc, unsigned int dev,
				   unsigned int func, unsigned int off,
				   const void *buf, int len)
{
	int err = -EINVAL;
	u32 addr, val;
	void __iomem *mmio = 0;
	u16 chipid = pc->core->bus->chipinfo.id;

	WARN_ON(!pc->hostmode);
	if (unlikely(len != 1 && len != 2 && len != 4))
		goto out;
	if (dev == 0) {
		/* we support only two functions on device 0 */
		if (func > 1)
			goto out;

		/* accesses to config registers with offsets >= 256
		 * requires indirect access.
		 */
		if (off >= PCI_CONFIG_SPACE_SIZE) {
			addr = (func << 12);
			addr |= (off & 0x0FFC);
			val = bcma_pcie_read_config(pc, addr);
		} else {
			addr = BCMA_CORE_PCI_PCICFG0;
			addr |= (func << 8);
			addr |= (off & 0xFC);
			val = pcicore_read32(pc, addr);
		}
	} else {
		addr = bcma_get_cfgspace_addr(pc, dev, func, off);
		if (unlikely(!addr))
			goto out;
		err = -ENOMEM;
		mmio = ioremap(addr, sizeof(val));
		if (!mmio)
			goto out;

		if (mips_busprobe32(val, mmio)) {
			val = 0xFFFFFFFF;
			goto unmap;
		}
	}

	switch (len) {
	case 1:
		val &= ~(0xFF << (8 * (off & 3)));
		val |= *((const u8 *)buf) << (8 * (off & 3));
		break;
	case 2:
		val &= ~(0xFFFF << (8 * (off & 3)));
		val |= *((const u16 *)buf) << (8 * (off & 3));
		break;
	case 4:
		val = *((const u32 *)buf);
		break;
	}
	if (dev == 0) {
		/* accesses to config registers with offsets >= 256
		 * requires indirect access.
		 */
		if (off >= PCI_CONFIG_SPACE_SIZE)
			bcma_pcie_write_config(pc, addr, val);
		else
			pcicore_write32(pc, addr, val);
	} else {
		writel(val, mmio);

		if (chipid == BCMA_CHIP_ID_BCM4716 ||
		    chipid == BCMA_CHIP_ID_BCM4748)
			readl(mmio);
	}

	err = 0;
unmap:
	if (mmio)
		iounmap(mmio);
out:
	return err;
}

static int bcma_core_pci_hostmode_read_config(struct pci_bus *bus,
					      unsigned int devfn,
					      int reg, int size, u32 *val)
{
	unsigned long flags;
	int err;
	struct bcma_drv_pci *pc;
	struct bcma_drv_pci_host *pc_host;

	pc_host = container_of(bus->ops, struct bcma_drv_pci_host, pci_ops);
	pc = pc_host->pdev;

	spin_lock_irqsave(&pc_host->cfgspace_lock, flags);
	err = bcma_extpci_read_config(pc, PCI_SLOT(devfn),
				     PCI_FUNC(devfn), reg, val, size);
	spin_unlock_irqrestore(&pc_host->cfgspace_lock, flags);

	return err ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;
}

static int bcma_core_pci_hostmode_write_config(struct pci_bus *bus,
					       unsigned int devfn,
					       int reg, int size, u32 val)
{
	unsigned long flags;
	int err;
	struct bcma_drv_pci *pc;
	struct bcma_drv_pci_host *pc_host;

	pc_host = container_of(bus->ops, struct bcma_drv_pci_host, pci_ops);
	pc = pc_host->pdev;

	spin_lock_irqsave(&pc_host->cfgspace_lock, flags);
	err = bcma_extpci_write_config(pc, PCI_SLOT(devfn),
				      PCI_FUNC(devfn), reg, &val, size);
	spin_unlock_irqrestore(&pc_host->cfgspace_lock, flags);

	return err ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;
}

/* return cap_offset if requested capability exists in the PCI config space */
static u8 bcma_find_pci_capability(struct bcma_drv_pci *pc, unsigned int dev,
				   unsigned int func, u8 req_cap_id,
				   unsigned char *buf, u32 *buflen)
{
	u8 cap_id;
	u8 cap_ptr = 0;
	u32 bufsize;
	u8 byte_val;

	/* check for Header type 0 */
	bcma_extpci_read_config(pc, dev, func, PCI_HEADER_TYPE, &byte_val,
				sizeof(u8));
	if ((byte_val & PCI_HEADER_TYPE_MASK) != PCI_HEADER_TYPE_NORMAL)
		return cap_ptr;

	/* check if the capability pointer field exists */
	bcma_extpci_read_config(pc, dev, func, PCI_STATUS, &byte_val,
				sizeof(u8));
	if (!(byte_val & PCI_STATUS_CAP_LIST))
		return cap_ptr;

	/* check if the capability pointer is 0x00 */
	bcma_extpci_read_config(pc, dev, func, PCI_CAPABILITY_LIST, &cap_ptr,
				sizeof(u8));
	if (cap_ptr == 0x00)
		return cap_ptr;

	/* loop through the capability list and see if the requested capability
	 * exists */
	bcma_extpci_read_config(pc, dev, func, cap_ptr, &cap_id, sizeof(u8));
	while (cap_id != req_cap_id) {
		bcma_extpci_read_config(pc, dev, func, cap_ptr + 1, &cap_ptr,
					sizeof(u8));
		if (cap_ptr == 0x00)
			return cap_ptr;
		bcma_extpci_read_config(pc, dev, func, cap_ptr, &cap_id,
					sizeof(u8));
	}

	/* found the caller requested capability */
	if ((buf != NULL) && (buflen != NULL)) {
		u8 cap_data;

		bufsize = *buflen;
		if (!bufsize)
			return cap_ptr;

		*buflen = 0;

		/* copy the capability data excluding cap ID and next ptr */
		cap_data = cap_ptr + 2;
		if ((bufsize + cap_data)  > PCI_CONFIG_SPACE_SIZE)
			bufsize = PCI_CONFIG_SPACE_SIZE - cap_data;
		*buflen = bufsize;
		while (bufsize--) {
			bcma_extpci_read_config(pc, dev, func, cap_data, buf,
						sizeof(u8));
			cap_data++;
			buf++;
		}
	}

	return cap_ptr;
}

/* If the root port is capable of returning Config Request
 * Retry Status (RRS) Completion Status to software then
 * enable the feature.
 */
static void bcma_core_pci_enable_crs(struct bcma_drv_pci *pc)
{
	struct bcma_bus *bus = pc->core->bus;
	u8 cap_ptr, root_ctrl, root_cap, dev;
	u16 val16;
	int i;

	cap_ptr = bcma_find_pci_capability(pc, 0, 0, PCI_CAP_ID_EXP, NULL,
					   NULL);
	root_cap = cap_ptr + PCI_EXP_RTCAP;
	bcma_extpci_read_config(pc, 0, 0, root_cap, &val16, sizeof(u16));
	if (val16 & BCMA_CORE_PCI_RC_RRS_VISIBILITY) {
		/* Enable Configuration RRS Software Visibility */
		root_ctrl = cap_ptr + PCI_EXP_RTCTL;
		val16 = PCI_EXP_RTCTL_RRS_SVE;
		bcma_extpci_read_config(pc, 0, 0, root_ctrl, &val16,
					sizeof(u16));

		/* Initiate a configuration request to read the vendor id
		 * field of the device function's config space header after
		 * 100 ms wait time from the end of Reset. If the device is
		 * not done with its internal initialization, it must at
		 * least return a completion TLP, with a completion status
		 * of "Configuration Request Retry Status (RRS)". The root
		 * complex must complete the request to the host by returning
		 * a read-data value of 0001h for the Vendor ID field and
		 * all 1s for any additional bytes included in the request.
		 * Poll using the config reads for max wait time of 1 sec or
		 * until we receive the successful completion status. Repeat
		 * the procedure for all the devices.
		 */
		for (dev = 1; dev < BCMA_PCI_SLOT_MAX; dev++) {
			for (i = 0; i < 100000; i++) {
				bcma_extpci_read_config(pc, dev, 0,
							PCI_VENDOR_ID, &val16,
							sizeof(val16));
				if (val16 != 0x1)
					break;
				udelay(10);
			}
			if (val16 == 0x1)
				bcma_err(bus, "PCI: Broken device in slot %d\n",
					 dev);
		}
	}
}

void bcma_core_pci_hostmode_init(struct bcma_drv_pci *pc)
{
	struct bcma_bus *bus = pc->core->bus;
	struct bcma_drv_pci_host *pc_host;
	u32 tmp;
	u32 pci_membase_1G;
	unsigned long io_map_base;

	bcma_info(bus, "PCIEcore in host mode found\n");

	if (bus->sprom.boardflags_lo & BCMA_CORE_PCI_BFL_NOPCI) {
		bcma_info(bus, "This PCIE core is disabled and not working\n");
		return;
	}

	pc_host = kzalloc(sizeof(*pc_host), GFP_KERNEL);
	if (!pc_host)  {
		bcma_err(bus, "can not allocate memory");
		return;
	}

	spin_lock_init(&pc_host->cfgspace_lock);

	pc->host_controller = pc_host;
	pc_host->pci_controller.io_resource = &pc_host->io_resource;
	pc_host->pci_controller.mem_resource = &pc_host->mem_resource;
	pc_host->pci_controller.pci_ops = &pc_host->pci_ops;
	pc_host->pdev = pc;

	pci_membase_1G = BCMA_SOC_PCI_DMA;
	pc_host->host_cfg_addr = BCMA_SOC_PCI_CFG;

	pc_host->pci_ops.read = bcma_core_pci_hostmode_read_config;
	pc_host->pci_ops.write = bcma_core_pci_hostmode_write_config;

	pc_host->mem_resource.name = "BCMA PCIcore external memory";
	pc_host->mem_resource.start = BCMA_SOC_PCI_DMA;
	pc_host->mem_resource.end = BCMA_SOC_PCI_DMA + BCMA_SOC_PCI_DMA_SZ - 1;
	pc_host->mem_resource.flags = IORESOURCE_MEM | IORESOURCE_PCI_FIXED;

	pc_host->io_resource.name = "BCMA PCIcore external I/O";
	pc_host->io_resource.start = 0x100;
	pc_host->io_resource.end = 0x7FF;
	pc_host->io_resource.flags = IORESOURCE_IO | IORESOURCE_PCI_FIXED;

	/* Reset RC */
	usleep_range(3000, 5000);
	pcicore_write32(pc, BCMA_CORE_PCI_CTL, BCMA_CORE_PCI_CTL_RST_OE);
	msleep(50);
	pcicore_write32(pc, BCMA_CORE_PCI_CTL, BCMA_CORE_PCI_CTL_RST |
			BCMA_CORE_PCI_CTL_RST_OE);

	/* 64 MB I/O access window. On 4716, use
	 * sbtopcie0 to access the device registers. We
	 * can't use address match 2 (1 GB window) region
	 * as mips can't generate 64-bit address on the
	 * backplane.
	 */
	if (bus->chipinfo.id == BCMA_CHIP_ID_BCM4716 ||
	    bus->chipinfo.id == BCMA_CHIP_ID_BCM4748) {
		pc_host->mem_resource.start = BCMA_SOC_PCI_MEM;
		pc_host->mem_resource.end = BCMA_SOC_PCI_MEM +
					    BCMA_SOC_PCI_MEM_SZ - 1;
		pcicore_write32(pc, BCMA_CORE_PCI_SBTOPCI0,
				BCMA_CORE_PCI_SBTOPCI_MEM | BCMA_SOC_PCI_MEM);
	} else if (bus->chipinfo.id == BCMA_CHIP_ID_BCM4706) {
		tmp = BCMA_CORE_PCI_SBTOPCI_MEM;
		tmp |= BCMA_CORE_PCI_SBTOPCI_PREF;
		tmp |= BCMA_CORE_PCI_SBTOPCI_BURST;
		if (pc->core->core_unit == 0) {
			pc_host->mem_resource.start = BCMA_SOC_PCI_MEM;
			pc_host->mem_resource.end = BCMA_SOC_PCI_MEM +
						    BCMA_SOC_PCI_MEM_SZ - 1;
			pc_host->io_resource.start = 0x100;
			pc_host->io_resource.end = 0x47F;
			pci_membase_1G = BCMA_SOC_PCIE_DMA_H32;
			pcicore_write32(pc, BCMA_CORE_PCI_SBTOPCI0,
					tmp | BCMA_SOC_PCI_MEM);
		} else if (pc->core->core_unit == 1) {
			pc_host->mem_resource.start = BCMA_SOC_PCI1_MEM;
			pc_host->mem_resource.end = BCMA_SOC_PCI1_MEM +
						    BCMA_SOC_PCI_MEM_SZ - 1;
			pc_host->io_resource.start = 0x480;
			pc_host->io_resource.end = 0x7FF;
			pci_membase_1G = BCMA_SOC_PCIE1_DMA_H32;
			pc_host->host_cfg_addr = BCMA_SOC_PCI1_CFG;
			pcicore_write32(pc, BCMA_CORE_PCI_SBTOPCI0,
					tmp | BCMA_SOC_PCI1_MEM);
		}
	} else
		pcicore_write32(pc, BCMA_CORE_PCI_SBTOPCI0,
				BCMA_CORE_PCI_SBTOPCI_IO);

	/* 64 MB configuration access window */
	pcicore_write32(pc, BCMA_CORE_PCI_SBTOPCI1, BCMA_CORE_PCI_SBTOPCI_CFG0);

	/* 1 GB memory access window */
	pcicore_write32(pc, BCMA_CORE_PCI_SBTOPCI2,
			BCMA_CORE_PCI_SBTOPCI_MEM | pci_membase_1G);


	/* As per PCI Express Base Spec 1.1 we need to wait for
	 * at least 100 ms from the end of a reset (cold/warm/hot)
	 * before issuing configuration requests to PCI Express
	 * devices.
	 */
	msleep(100);

	bcma_core_pci_enable_crs(pc);

	if (bus->chipinfo.id == BCMA_CHIP_ID_BCM4706 ||
	    bus->chipinfo.id == BCMA_CHIP_ID_BCM4716) {
		u16 val16;
		bcma_extpci_read_config(pc, 0, 0, BCMA_CORE_PCI_CFG_DEVCTRL,
					&val16, sizeof(val16));
		val16 |= (2 << 5);	/* Max payload size of 512 */
		val16 |= (2 << 12);	/* MRRS 512 */
		bcma_extpci_write_config(pc, 0, 0, BCMA_CORE_PCI_CFG_DEVCTRL,
					 &val16, sizeof(val16));
	}

	/* Enable PCI bridge BAR0 memory & master access */
	tmp = PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY;
	bcma_extpci_write_config(pc, 0, 0, PCI_COMMAND, &tmp, sizeof(tmp));

	/* Enable PCI interrupts */
	pcicore_write32(pc, BCMA_CORE_PCI_IMASK, BCMA_CORE_PCI_IMASK_INTA);

	/* Ok, ready to run, register it to the system.
	 * The following needs change, if we want to port hostmode
	 * to non-MIPS platform. */
	io_map_base = (unsigned long)ioremap(pc_host->mem_resource.start,
						     resource_size(&pc_host->mem_resource));
	pc_host->pci_controller.io_map_base = io_map_base;
	set_io_port_base(pc_host->pci_controller.io_map_base);
	/* Give some time to the PCI controller to configure itself with the new
	 * values. Not waiting at this point causes crashes of the machine. */
	usleep_range(10000, 15000);
	register_pci_controller(&pc_host->pci_controller);
	return;
}

/* Early PCI fixup for a device on the PCI-core bridge. */
static void bcma_core_pci_fixup_pcibridge(struct pci_dev *dev)
{
	if (dev->bus->ops->read != bcma_core_pci_hostmode_read_config) {
		/* This is not a device on the PCI-core bridge. */
		return;
	}
	if (PCI_SLOT(dev->devfn) != 0)
		return;

	pr_info("PCI: Fixing up bridge %s\n", pci_name(dev));

	/* Enable PCI bridge bus mastering and memory space */
	pci_set_master(dev);
	if (pcibios_enable_device(dev, ~0) < 0) {
		pr_err("PCI: BCMA bridge enable failed\n");
		return;
	}

	/* Enable PCI bridge BAR1 prefetch and burst */
	pci_write_config_dword(dev, BCMA_PCI_BAR1_CONTROL, 3);
}
DECLARE_PCI_FIXUP_EARLY(PCI_ANY_ID, PCI_ANY_ID, bcma_core_pci_fixup_pcibridge);

/* Early PCI fixup for all PCI-cores to set the correct memory address. */
static void bcma_core_pci_fixup_addresses(struct pci_dev *dev)
{
	struct resource *res;
	int pos, err;

	if (dev->bus->ops->read != bcma_core_pci_hostmode_read_config) {
		/* This is not a device on the PCI-core bridge. */
		return;
	}
	if (PCI_SLOT(dev->devfn) == 0)
		return;

	pr_info("PCI: Fixing up addresses %s\n", pci_name(dev));

	for (pos = 0; pos < 6; pos++) {
		res = &dev->resource[pos];
		if (res->flags & (IORESOURCE_IO | IORESOURCE_MEM)) {
			err = pci_assign_resource(dev, pos);
			if (err)
				pr_err("PCI: Problem fixing up the addresses on %s\n",
				       pci_name(dev));
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, bcma_core_pci_fixup_addresses);

/* This function is called when doing a pci_enable_device().
 * We must first check if the device is a device on the PCI-core bridge. */
int bcma_core_pci_plat_dev_init(struct pci_dev *dev)
{
	struct bcma_drv_pci_host *pc_host;
	int readrq;

	if (dev->bus->ops->read != bcma_core_pci_hostmode_read_config) {
		/* This is not a device on the PCI-core bridge. */
		return -ENODEV;
	}
	pc_host = container_of(dev->bus->ops, struct bcma_drv_pci_host,
			       pci_ops);

	pr_info("PCI: Fixing up device %s\n", pci_name(dev));

	/* Fix up interrupt lines */
	dev->irq = bcma_core_irq(pc_host->pdev->core, 0);
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);

	readrq = pcie_get_readrq(dev);
	if (readrq > 128) {
		pr_info("change PCIe max read request size from %i to 128\n", readrq);
		pcie_set_readrq(dev, 128);
	}
	return 0;
}
EXPORT_SYMBOL(bcma_core_pci_plat_dev_init);

/* PCI device IRQ mapping. */
int bcma_core_pci_pcibios_map_irq(const struct pci_dev *dev)
{
	struct bcma_drv_pci_host *pc_host;

	if (dev->bus->ops->read != bcma_core_pci_hostmode_read_config) {
		/* This is not a device on the PCI-core bridge. */
		return -ENODEV;
	}

	pc_host = container_of(dev->bus->ops, struct bcma_drv_pci_host,
			       pci_ops);
	return bcma_core_irq(pc_host->pdev->core, 0);
}
EXPORT_SYMBOL(bcma_core_pci_pcibios_map_irq);
