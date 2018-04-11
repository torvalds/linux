/*
 * pci.c -- PCI bus support for ColdFire processors
 *
 * (C) Copyright 2012, Greg Ungerer <gerg@uclinux.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/m54xxpci.h>

/*
 * Memory and IO mappings. We use a 1:1 mapping for local host memory to
 * PCI bus memory (no reason not to really). IO space is mapped in its own
 * separate address region. The device configuration space is mapped over
 * the IO map space when we enable it in the PCICAR register.
 */
static struct pci_bus *rootbus;
static unsigned long iospace;

/*
 * We need to be carefull probing on bus 0 (directly connected to host
 * bridge). We should only access the well defined possible devices in
 * use, ignore aliases and the like.
 */
static unsigned char mcf_host_slot2sid[32] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 1, 2, 0, 3, 4, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
};

static unsigned char mcf_host_irq[] = {
	0, 69, 69, 71, 71,
};

/*
 * Configuration space access functions. Configuration space access is
 * through the IO mapping window, enabling it via the PCICAR register.
 */
static unsigned long mcf_mk_pcicar(int bus, unsigned int devfn, int where)
{
	return (bus << PCICAR_BUSN) | (devfn << PCICAR_DEVFNN) | (where & 0xfc);
}

static int mcf_pci_readconfig(struct pci_bus *bus, unsigned int devfn,
	int where, int size, u32 *value)
{
	unsigned long addr;

	*value = 0xffffffff;

	if (bus->number == 0) {
		if (mcf_host_slot2sid[PCI_SLOT(devfn)] == 0)
			return PCIBIOS_SUCCESSFUL;
	}

	addr = mcf_mk_pcicar(bus->number, devfn, where);
	__raw_writel(PCICAR_E | addr, PCICAR);
	__raw_readl(PCICAR);
	addr = iospace + (where & 0x3);

	switch (size) {
	case 1:
		*value = __raw_readb(addr);
		break;
	case 2:
		*value = le16_to_cpu(__raw_readw(addr));
		break;
	default:
		*value = le32_to_cpu(__raw_readl(addr));
		break;
	}

	__raw_writel(0, PCICAR);
	__raw_readl(PCICAR);
	return PCIBIOS_SUCCESSFUL;
}

static int mcf_pci_writeconfig(struct pci_bus *bus, unsigned int devfn,
	int where, int size, u32 value)
{
	unsigned long addr;

	if (bus->number == 0) {
		if (mcf_host_slot2sid[PCI_SLOT(devfn)] == 0)
			return PCIBIOS_SUCCESSFUL;
	}

	addr = mcf_mk_pcicar(bus->number, devfn, where);
	__raw_writel(PCICAR_E | addr, PCICAR);
	__raw_readl(PCICAR);
	addr = iospace + (where & 0x3);

	switch (size) {
	case 1:
		 __raw_writeb(value, addr);
		break;
	case 2:
		__raw_writew(cpu_to_le16(value), addr);
		break;
	default:
		__raw_writel(cpu_to_le32(value), addr);
		break;
	}

	__raw_writel(0, PCICAR);
	__raw_readl(PCICAR);
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops mcf_pci_ops = {
	.read	= mcf_pci_readconfig,
	.write	= mcf_pci_writeconfig,
};

/*
 * Initialize the PCI bus registers, and scan the bus.
 */
static struct resource mcf_pci_mem = {
	.name	= "PCI Memory space",
	.start	= PCI_MEM_PA,
	.end	= PCI_MEM_PA + PCI_MEM_SIZE - 1,
	.flags	= IORESOURCE_MEM,
};

static struct resource mcf_pci_io = {
	.name	= "PCI IO space",
	.start	= 0x400,
	.end	= 0x10000 - 1,
	.flags	= IORESOURCE_IO,
};

static struct resource busn_resource = {
	.name	= "PCI busn",
	.start	= 0,
	.end	= 255,
	.flags	= IORESOURCE_BUS,
};

/*
 * Interrupt mapping and setting.
 */
static int mcf_pci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int sid;

	sid = mcf_host_slot2sid[slot];
	if (sid)
		return mcf_host_irq[sid];
	return 0;
}

static int __init mcf_pci_init(void)
{
	struct pci_host_bridge *bridge;
	int ret;

	bridge = pci_alloc_host_bridge(0);
	if (!bridge)
		return -ENOMEM;

	pr_info("ColdFire: PCI bus initialization...\n");

	/* Reset the external PCI bus */
	__raw_writel(PCIGSCR_RESET, PCIGSCR);
	__raw_writel(0, PCITCR);

	request_resource(&iomem_resource, &mcf_pci_mem);
	request_resource(&iomem_resource, &mcf_pci_io);

	/* Configure PCI arbiter */
	__raw_writel(PACR_INTMPRI | PACR_INTMINTE | PACR_EXTMPRI(0x1f) |
		PACR_EXTMINTE(0x1f), PACR);

	/* Set required multi-function pins for PCI bus use */
	__raw_writew(0x3ff, MCFGPIO_PAR_PCIBG);
	__raw_writew(0x3ff, MCFGPIO_PAR_PCIBR);

	/* Set up config space for local host bus controller */
	__raw_writel(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
		PCI_COMMAND_INVALIDATE, PCISCR);
	__raw_writel(PCICR1_LT(32) | PCICR1_CL(8), PCICR1);
	__raw_writel(0, PCICR2);

	/*
	 * Set up the initiator windows for memory and IO mapping.
	 * These give the CPU bus access onto the PCI bus. One for each of
	 * PCI memory and IO address spaces.
	 */
	__raw_writel(WXBTAR(PCI_MEM_PA, PCI_MEM_BA, PCI_MEM_SIZE),
		PCIIW0BTAR);
	__raw_writel(WXBTAR(PCI_IO_PA, PCI_IO_BA, PCI_IO_SIZE),
		PCIIW1BTAR);
	__raw_writel(PCIIWCR_W0_MEM /*| PCIIWCR_W0_MRDL*/ | PCIIWCR_W0_E |
		PCIIWCR_W1_IO | PCIIWCR_W1_E, PCIIWCR);

	/*
	 * Set up the target windows for access from the PCI bus back to the
	 * CPU bus. All we need is access to system RAM (for mastering).
	 */
	__raw_writel(CONFIG_RAMBASE, PCIBAR1);
	__raw_writel(CONFIG_RAMBASE | PCITBATR1_E, PCITBATR1);

	/* Keep a virtual mapping to IO/config space active */
	iospace = (unsigned long) ioremap(PCI_IO_PA, PCI_IO_SIZE);
	if (iospace == 0)
		return -ENODEV;
	pr_info("Coldfire: PCI IO/config window mapped to 0x%x\n",
		(u32) iospace);

	/* Turn of PCI reset, and wait for devices to settle */
	__raw_writel(0, PCIGSCR);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(200));


	pci_add_resource(&bridge->windows, &ioport_resource);
	pci_add_resource(&bridge->windows, &iomem_resource);
	pci_add_resource(&bridge->windows, &busn_resource);
	bridge->dev.parent = NULL;
	bridge->sysdata = NULL;
	bridge->busnr = 0;
	bridge->ops = &mcf_pci_ops;
	bridge->swizzle_irq = pci_common_swizzle;
	bridge->map_irq = mcf_pci_map_irq;

	ret = pci_scan_root_bus_bridge(bridge);
	if (ret) {
		pci_free_host_bridge(bridge);
		return ret;
	}

	rootbus = bridge->bus;

	rootbus->resource[0] = &mcf_pci_io;
	rootbus->resource[1] = &mcf_pci_mem;

	pci_bus_size_bridges(rootbus);
	pci_bus_assign_resources(rootbus);
	pci_bus_add_devices(rootbus);
	return 0;
}

subsys_initcall(mcf_pci_init);
