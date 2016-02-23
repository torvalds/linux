/*
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the Broadcom
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/mm.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/console.h>

#include <asm/io.h>

#include <asm/netlogic/interrupt.h>
#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/common.h>
#include <asm/netlogic/mips-extns.h>

#include <asm/netlogic/xlp-hal/iomap.h>
#include <asm/netlogic/xlp-hal/xlp.h>
#include <asm/netlogic/xlp-hal/pic.h>
#include <asm/netlogic/xlp-hal/pcibus.h>
#include <asm/netlogic/xlp-hal/bridge.h>

static void *pci_config_base;

#define pci_cfg_addr(bus, devfn, off) (((bus) << 20) | ((devfn) << 12) | (off))

/* PCI ops */
static inline u32 pci_cfg_read_32bit(struct pci_bus *bus, unsigned int devfn,
	int where)
{
	u32 data;
	u32 *cfgaddr;

	where &= ~3;
	if (cpu_is_xlp9xx()) {
		/* be very careful on SoC buses */
		if (bus->number == 0) {
			/* Scan only existing nodes - uboot bug? */
			if (PCI_SLOT(devfn) != 0 ||
					   !nlm_node_present(PCI_FUNC(devfn)))
				return 0xffffffff;
		} else if (bus->parent->number == 0) {	/* SoC bus */
			if (PCI_SLOT(devfn) == 0)	/* b.0.0 hangs */
				return 0xffffffff;
			if (devfn == 44)		/* b.5.4 hangs */
				return 0xffffffff;
		}
	} else if (bus->number == 0 && PCI_SLOT(devfn) == 1 && where == 0x954) {
		return 0xffffffff;
	}
	cfgaddr = (u32 *)(pci_config_base +
			pci_cfg_addr(bus->number, devfn, where));
	data = *cfgaddr;
	return data;
}

static inline void pci_cfg_write_32bit(struct pci_bus *bus, unsigned int devfn,
	int where, u32 data)
{
	u32 *cfgaddr;

	cfgaddr = (u32 *)(pci_config_base +
			pci_cfg_addr(bus->number, devfn, where & ~3));
	*cfgaddr = data;
}

static int nlm_pcibios_read(struct pci_bus *bus, unsigned int devfn,
	int where, int size, u32 *val)
{
	u32 data;

	if ((size == 2) && (where & 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (where & 3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	data = pci_cfg_read_32bit(bus, devfn, where);

	if (size == 1)
		*val = (data >> ((where & 3) << 3)) & 0xff;
	else if (size == 2)
		*val = (data >> ((where & 3) << 3)) & 0xffff;
	else
		*val = data;

	return PCIBIOS_SUCCESSFUL;
}


static int nlm_pcibios_write(struct pci_bus *bus, unsigned int devfn,
		int where, int size, u32 val)
{
	u32 data;

	if ((size == 2) && (where & 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (where & 3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	data = pci_cfg_read_32bit(bus, devfn, where);

	if (size == 1)
		data = (data & ~(0xff << ((where & 3) << 3))) |
			(val << ((where & 3) << 3));
	else if (size == 2)
		data = (data & ~(0xffff << ((where & 3) << 3))) |
			(val << ((where & 3) << 3));
	else
		data = val;

	pci_cfg_write_32bit(bus, devfn, where, data);

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops nlm_pci_ops = {
	.read  = nlm_pcibios_read,
	.write = nlm_pcibios_write
};

static struct resource nlm_pci_mem_resource = {
	.name		= "XLP PCI MEM",
	.start		= 0xd0000000UL, /* 256MB PCI mem @ 0xd000_0000 */
	.end		= 0xdfffffffUL,
	.flags		= IORESOURCE_MEM,
};

static struct resource nlm_pci_io_resource = {
	.name		= "XLP IO MEM",
	.start		= 0x14000000UL, /* 64MB PCI IO @ 0x1000_0000 */
	.end		= 0x17ffffffUL,
	.flags		= IORESOURCE_IO,
};

struct pci_controller nlm_pci_controller = {
	.index		= 0,
	.pci_ops	= &nlm_pci_ops,
	.mem_resource	= &nlm_pci_mem_resource,
	.mem_offset	= 0x00000000UL,
	.io_resource	= &nlm_pci_io_resource,
	.io_offset	= 0x00000000UL,
};

struct pci_dev *xlp_get_pcie_link(const struct pci_dev *dev)
{
	struct pci_bus *bus, *p;

	bus = dev->bus;

	if (cpu_is_xlp9xx()) {
		/* find bus with grand parent number == 0 */
		for (p = bus->parent; p && p->parent && p->parent->number != 0;
				p = p->parent)
			bus = p;
		return (p && p->parent) ? bus->self : NULL;
	} else {
		/* Find the bridge on bus 0 */
		for (p = bus->parent; p && p->number != 0; p = p->parent)
			bus = p;

		return p ? bus->self : NULL;
	}
}

int xlp_socdev_to_node(const struct pci_dev *lnkdev)
{
	if (cpu_is_xlp9xx())
		return PCI_FUNC(lnkdev->bus->self->devfn);
	else
		return PCI_SLOT(lnkdev->devfn) / 8;
}

int __init pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct pci_dev *lnkdev;
	int lnkfunc, node;

	/*
	 * For XLP PCIe, there is an IRQ per Link, find out which
	 * link the device is on to assign interrupts
	*/
	lnkdev = xlp_get_pcie_link(dev);
	if (lnkdev == NULL)
		return 0;

	lnkfunc = PCI_FUNC(lnkdev->devfn);
	node = xlp_socdev_to_node(lnkdev);

	return nlm_irq_to_xirq(node, PIC_PCIE_LINK_LEGACY_IRQ(lnkfunc));
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

/*
 * If big-endian, enable hardware byteswap on the PCIe bridges.
 * This will make both the SoC and PCIe devices behave consistently with
 * readl/writel.
 */
#ifdef __BIG_ENDIAN
static void xlp_config_pci_bswap(int node, int link)
{
	uint64_t nbubase, lnkbase;
	u32 reg;

	nbubase = nlm_get_bridge_regbase(node);
	lnkbase = nlm_get_pcie_base(node, link);

	/*
	 *  Enable byte swap in hardware. Program each link's PCIe SWAP regions
	 * from the link's address ranges.
	 */
	if (cpu_is_xlp9xx()) {
		reg = nlm_read_bridge_reg(nbubase,
				BRIDGE_9XX_PCIEMEM_BASE0 + link);
		nlm_write_pci_reg(lnkbase, PCIE_9XX_BYTE_SWAP_MEM_BASE, reg);

		reg = nlm_read_bridge_reg(nbubase,
				BRIDGE_9XX_PCIEMEM_LIMIT0 + link);
		nlm_write_pci_reg(lnkbase,
				PCIE_9XX_BYTE_SWAP_MEM_LIM, reg | 0xfff);

		reg = nlm_read_bridge_reg(nbubase,
				BRIDGE_9XX_PCIEIO_BASE0 + link);
		nlm_write_pci_reg(lnkbase, PCIE_9XX_BYTE_SWAP_IO_BASE, reg);

		reg = nlm_read_bridge_reg(nbubase,
				BRIDGE_9XX_PCIEIO_LIMIT0 + link);
		nlm_write_pci_reg(lnkbase,
				PCIE_9XX_BYTE_SWAP_IO_LIM, reg | 0xfff);
	} else {
		reg = nlm_read_bridge_reg(nbubase, BRIDGE_PCIEMEM_BASE0 + link);
		nlm_write_pci_reg(lnkbase, PCIE_BYTE_SWAP_MEM_BASE, reg);

		reg = nlm_read_bridge_reg(nbubase,
					BRIDGE_PCIEMEM_LIMIT0 + link);
		nlm_write_pci_reg(lnkbase, PCIE_BYTE_SWAP_MEM_LIM, reg | 0xfff);

		reg = nlm_read_bridge_reg(nbubase, BRIDGE_PCIEIO_BASE0 + link);
		nlm_write_pci_reg(lnkbase, PCIE_BYTE_SWAP_IO_BASE, reg);

		reg = nlm_read_bridge_reg(nbubase, BRIDGE_PCIEIO_LIMIT0 + link);
		nlm_write_pci_reg(lnkbase, PCIE_BYTE_SWAP_IO_LIM, reg | 0xfff);
	}
}
#else
/* Swap configuration not needed in little-endian mode */
static inline void xlp_config_pci_bswap(int node, int link) {}
#endif /* __BIG_ENDIAN */

static int __init pcibios_init(void)
{
	uint64_t pciebase;
	int link, n;
	u32 reg;

	/* Firmware assigns PCI resources */
	pci_set_flags(PCI_PROBE_ONLY);
	pci_config_base = ioremap(XLP_DEFAULT_PCI_ECFG_BASE, 64 << 20);

	/* Extend IO port for memory mapped io */
	ioport_resource.start =	 0;
	ioport_resource.end   = ~0;

	for (n = 0; n < NLM_NR_NODES; n++) {
		if (!nlm_node_present(n))
			continue;

		for (link = 0; link < PCIE_NLINKS; link++) {
			pciebase = nlm_get_pcie_base(n, link);
			if (nlm_read_pci_reg(pciebase, 0) == 0xffffffff)
				continue;
			xlp_config_pci_bswap(n, link);
			xlp_init_node_msi_irqs(n, link);

			/* put in intpin and irq - u-boot does not */
			reg = nlm_read_pci_reg(pciebase, 0xf);
			reg &= ~0x1ffu;
			reg |= (1 << 8) | PIC_PCIE_LINK_LEGACY_IRQ(link);
			nlm_write_pci_reg(pciebase, 0xf, reg);
			pr_info("XLP PCIe: Link %d-%d initialized.\n", n, link);
		}
	}

	set_io_port_base(CKSEG1);
	nlm_pci_controller.io_map_base = CKSEG1;

	register_pci_controller(&nlm_pci_controller);
	pr_info("XLP PCIe Controller %pR%pR.\n", &nlm_pci_io_resource,
		&nlm_pci_mem_resource);

	return 0;
}
arch_initcall(pcibios_init);
