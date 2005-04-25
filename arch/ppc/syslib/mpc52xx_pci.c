/*
 * arch/ppc/syslib/mpc52xx_pci.c
 *
 * PCI code for the Freescale MPC52xx embedded CPU.
 *
 *
 * Maintainer : Sylvain Munaut <tnt@246tNt.com>
 *
 * Copyright (C) 2004 Sylvain Munaut <tnt@246tNt.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/config.h>

#include <asm/pci.h>

#include <asm/mpc52xx.h>
#include "mpc52xx_pci.h"

#include <asm/delay.h>


static int
mpc52xx_pci_read_config(struct pci_bus *bus, unsigned int devfn,
				int offset, int len, u32 *val)
{
	struct pci_controller *hose = bus->sysdata;
	u32 value;

	if (ppc_md.pci_exclude_device)
		if (ppc_md.pci_exclude_device(bus->number, devfn))
			return PCIBIOS_DEVICE_NOT_FOUND;

	out_be32(hose->cfg_addr,
		(1 << 31) |
		((bus->number - hose->bus_offset) << 16) |
		(devfn << 8) |
		(offset & 0xfc));

	value = in_le32(hose->cfg_data);

	if (len != 4) {
		value >>= ((offset & 0x3) << 3);
		value &= 0xffffffff >> (32 - (len << 3));
	}

	*val = value;

	out_be32(hose->cfg_addr, 0);

	return PCIBIOS_SUCCESSFUL;
}

static int
mpc52xx_pci_write_config(struct pci_bus *bus, unsigned int devfn,
				int offset, int len, u32 val)
{
	struct pci_controller *hose = bus->sysdata;
	u32 value, mask;

	if (ppc_md.pci_exclude_device)
		if (ppc_md.pci_exclude_device(bus->number, devfn))
			return PCIBIOS_DEVICE_NOT_FOUND;

	out_be32(hose->cfg_addr,
		(1 << 31) |
		((bus->number - hose->bus_offset) << 16) |
		(devfn << 8) |
		(offset & 0xfc));

	if (len != 4) {
		value = in_le32(hose->cfg_data);

		offset = (offset & 0x3) << 3;
		mask = (0xffffffff >> (32 - (len << 3)));
		mask <<= offset;

		value &= ~mask;
		val = value | ((val << offset) & mask);
	}

	out_le32(hose->cfg_data, val);

	out_be32(hose->cfg_addr, 0);

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops mpc52xx_pci_ops = {
	.read  = mpc52xx_pci_read_config,
	.write = mpc52xx_pci_write_config
};


static void __init
mpc52xx_pci_setup(struct mpc52xx_pci __iomem *pci_regs)
{

	/* Setup control regs */
		/* Nothing to do afaik */

	/* Setup windows */
	out_be32(&pci_regs->iw0btar, MPC52xx_PCI_IWBTAR_TRANSLATION(
		MPC52xx_PCI_MEM_START + MPC52xx_PCI_MEM_OFFSET,
		MPC52xx_PCI_MEM_START,
		MPC52xx_PCI_MEM_SIZE ));

	out_be32(&pci_regs->iw1btar, MPC52xx_PCI_IWBTAR_TRANSLATION(
		MPC52xx_PCI_MMIO_START + MPC52xx_PCI_MEM_OFFSET,
		MPC52xx_PCI_MMIO_START,
		MPC52xx_PCI_MMIO_SIZE ));

	out_be32(&pci_regs->iw2btar, MPC52xx_PCI_IWBTAR_TRANSLATION(
		MPC52xx_PCI_IO_BASE,
		MPC52xx_PCI_IO_START,
		MPC52xx_PCI_IO_SIZE ));

	out_be32(&pci_regs->iwcr, MPC52xx_PCI_IWCR_PACK(
		( MPC52xx_PCI_IWCR_ENABLE |		/* iw0btar */
		  MPC52xx_PCI_IWCR_READ_MULTI |
		  MPC52xx_PCI_IWCR_MEM ),
		( MPC52xx_PCI_IWCR_ENABLE |		/* iw1btar */
		  MPC52xx_PCI_IWCR_READ |
		  MPC52xx_PCI_IWCR_MEM ),
		( MPC52xx_PCI_IWCR_ENABLE |		/* iw2btar */
		  MPC52xx_PCI_IWCR_IO )
	));


	out_be32(&pci_regs->tbatr0,
		MPC52xx_PCI_TBATR_ENABLE | MPC52xx_PCI_TARGET_IO );
	out_be32(&pci_regs->tbatr1,
		MPC52xx_PCI_TBATR_ENABLE | MPC52xx_PCI_TARGET_MEM );

	out_be32(&pci_regs->tcr, MPC52xx_PCI_TCR_LD);

	/* Reset the exteral bus ( internal PCI controller is NOT resetted ) */
	/* Not necessary and can be a bad thing if for example the bootloader
	   is displaying a splash screen or ... Just left here for
	   documentation purpose if anyone need it */
#if 0
	u32 tmp;
	tmp = in_be32(&pci_regs->gscr);
	out_be32(&pci_regs->gscr, tmp | MPC52xx_PCI_GSCR_PR);
	udelay(50);
	out_be32(&pci_regs->gscr, tmp);
#endif
}

static void __init
mpc52xx_pci_fixup_resources(struct pci_dev *dev)
{
	int i;

	/* We don't rely on boot loader for PCI and resets all
	   devices */
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		struct resource *res = &dev->resource[i];
		if (res->end > res->start) {	/* Only valid resources */
			res->end -= res->start;
			res->start = 0;
			res->flags |= IORESOURCE_UNSET;
		}
	}

	/* The PCI Host bridge of MPC52xx has a prefetch memory resource
	   fixed to 1Gb. Doesn't fit in the resource system so we remove it */
	if ( (dev->vendor == PCI_VENDOR_ID_MOTOROLA) &&
	     (dev->device == PCI_DEVICE_ID_MOTOROLA_MPC5200) ) {
		struct resource *res = &dev->resource[1];
		res->start = res->end = res->flags = 0;
	}
}

void __init
mpc52xx_find_bridges(void)
{
	struct mpc52xx_pci __iomem *pci_regs;
	struct pci_controller *hose;

	pci_assign_all_busses = 1;

	pci_regs = ioremap(MPC52xx_PA(MPC52xx_PCI_OFFSET), MPC52xx_PCI_SIZE);
	if (!pci_regs)
		return;

	hose = pcibios_alloc_controller();
	if (!hose) {
		iounmap(pci_regs);
		return;
	}

	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pcibios_fixup_resources = mpc52xx_pci_fixup_resources;

	hose->first_busno = 0;
	hose->last_busno = 0xff;
	hose->bus_offset = 0;
	hose->ops = &mpc52xx_pci_ops;

	mpc52xx_pci_setup(pci_regs);

	hose->pci_mem_offset = MPC52xx_PCI_MEM_OFFSET;

	hose->io_base_virt = ioremap(MPC52xx_PCI_IO_BASE, MPC52xx_PCI_IO_SIZE);
	isa_io_base = (unsigned long) hose->io_base_virt;

	hose->cfg_addr = &pci_regs->car;
	hose->cfg_data = hose->io_base_virt;

	/* Setup resources */
	pci_init_resource(&hose->mem_resources[0],
			MPC52xx_PCI_MEM_START,
			MPC52xx_PCI_MEM_STOP,
			IORESOURCE_MEM|IORESOURCE_PREFETCH,
			"PCI prefetchable memory");

	pci_init_resource(&hose->mem_resources[1],
			MPC52xx_PCI_MMIO_START,
			MPC52xx_PCI_MMIO_STOP,
			IORESOURCE_MEM,
			"PCI memory");

	pci_init_resource(&hose->io_resource,
			MPC52xx_PCI_IO_START,
			MPC52xx_PCI_IO_STOP,
			IORESOURCE_IO,
			"PCI I/O");

}
