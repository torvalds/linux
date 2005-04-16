/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999, 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * MIPS boards specific PCI support.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/mips-boards/generic.h>
#include <asm/gt64120.h>
#include <asm/mips-boards/bonito64.h>
#include <asm/mips-boards/msc01_pci.h>
#ifdef CONFIG_MIPS_MALTA
#include <asm/mips-boards/malta.h>
#endif

static struct resource bonito64_mem_resource = {
	.name	= "Bonito PCI MEM",
	.start	= 0x10000000UL,
	.end	= 0x1bffffffUL,
	.flags	= IORESOURCE_MEM,
};

static struct resource bonito64_io_resource = {
	.name	= "Bonito IO MEM",
	.start	= 0x00002000UL,	/* avoid conflicts with YAMON allocated I/O addresses */
	.end	= 0x000fffffUL,
	.flags	= IORESOURCE_IO,
};

static struct resource gt64120_mem_resource = {
	.name	= "GT64120 PCI MEM",
	.start	= 0x10000000UL,
	.end	= 0x1bdfffffUL,
	.flags	= IORESOURCE_MEM,
};

static struct resource gt64120_io_resource = {
	.name	= "GT64120 IO MEM",
#ifdef CONFIG_MIPS_ATLAS
	.start	= 0x18000000UL,
	.end	= 0x181fffffUL,
#endif
#ifdef CONFIG_MIPS_MALTA
	.start	= 0x00002000UL,
	.end	= 0x001fffffUL,
#endif
	.flags	= IORESOURCE_IO,
};

static struct resource msc_mem_resource = {
	.name	= "MSC PCI MEM",
	.start	= 0x10000000UL,
	.end	= 0x1fffffffUL,
	.flags	= IORESOURCE_MEM,
};

static struct resource msc_io_resource = {
	.name	= "MSC IO MEM",
	.start	= 0x00002000UL,
	.end	= 0x007fffffUL,
	.flags	= IORESOURCE_IO,
};

extern struct pci_ops bonito64_pci_ops;
extern struct pci_ops gt64120_pci_ops;
extern struct pci_ops msc_pci_ops;

static struct pci_controller bonito64_controller = {
	.pci_ops	= &bonito64_pci_ops,
	.io_resource	= &bonito64_io_resource,
	.mem_resource	= &bonito64_mem_resource,
	.mem_offset	= 0x10000000UL,
	.io_offset	= 0x00000000UL,
};

static struct pci_controller gt64120_controller = {
	.pci_ops	= &gt64120_pci_ops,
	.io_resource	= &gt64120_io_resource,
	.mem_resource	= &gt64120_mem_resource,
	.mem_offset	= 0x00000000UL,
	.io_offset	= 0x00000000UL,
};

static struct pci_controller  msc_controller = {
	.pci_ops	= &msc_pci_ops,
	.io_resource	= &msc_io_resource,
	.mem_resource	= &msc_mem_resource,
	.mem_offset	= 0x10000000UL,
	.io_offset	= 0x00000000UL,
};

static int __init pcibios_init(void)
{
	struct pci_controller *controller;

	switch (mips_revision_corid) {
	case MIPS_REVISION_CORID_QED_RM5261:
	case MIPS_REVISION_CORID_CORE_LV:
	case MIPS_REVISION_CORID_CORE_FPGA:
	case MIPS_REVISION_CORID_CORE_FPGAR2:
		/*
		 * Due to a bug in the Galileo system controller, we need
		 * to setup the PCI BAR for the Galileo internal registers.
		 * This should be done in the bios/bootprom and will be
		 * fixed in a later revision of YAMON (the MIPS boards
		 * boot prom).
		 */
		GT_WRITE(GT_PCI0_CFGADDR_OFS,
			 (0 << GT_PCI0_CFGADDR_BUSNUM_SHF) | /* Local bus */
			 (0 << GT_PCI0_CFGADDR_DEVNUM_SHF) | /* GT64120 dev */
			 (0 << GT_PCI0_CFGADDR_FUNCTNUM_SHF) | /* Function 0*/
			 ((0x20/4) << GT_PCI0_CFGADDR_REGNUM_SHF) | /* BAR 4*/
			 GT_PCI0_CFGADDR_CONFIGEN_BIT );

		/* Perform the write */
		GT_WRITE(GT_PCI0_CFGDATA_OFS, CPHYSADDR(MIPS_GT_BASE));

		controller = &gt64120_controller;
		break;

	case MIPS_REVISION_CORID_BONITO64:
	case MIPS_REVISION_CORID_CORE_20K:
	case MIPS_REVISION_CORID_CORE_EMUL_BON:
		controller = &bonito64_controller;
		break;

	case MIPS_REVISION_CORID_CORE_MSC:
	case MIPS_REVISION_CORID_CORE_FPGA2:
	case MIPS_REVISION_CORID_CORE_EMUL_MSC:
		controller = &msc_controller;
		break;
	default:
		return 1;
	}

	ioport_resource.end = controller->io_resource->end;

	register_pci_controller (controller);

	return 0;
}

early_initcall(pcibios_init);
