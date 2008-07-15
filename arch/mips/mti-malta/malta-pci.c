/*
 * Copyright (C) 1999, 2000, 2004, 2005  MIPS Technologies, Inc.
 *	All rights reserved.
 *	Authors: Carsten Langgaard <carstenl@mips.com>
 *		 Maciej W. Rozycki <macro@mips.com>
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
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/gt64120.h>

#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/bonito64.h>
#include <asm/mips-boards/msc01_pci.h>

static struct resource bonito64_mem_resource = {
	.name	= "Bonito PCI MEM",
	.flags	= IORESOURCE_MEM,
};

static struct resource bonito64_io_resource = {
	.name	= "Bonito PCI I/O",
	.start	= 0x00000000UL,
	.end	= 0x000fffffUL,
	.flags	= IORESOURCE_IO,
};

static struct resource gt64120_mem_resource = {
	.name	= "GT-64120 PCI MEM",
	.flags	= IORESOURCE_MEM,
};

static struct resource gt64120_io_resource = {
	.name	= "GT-64120 PCI I/O",
	.flags	= IORESOURCE_IO,
};

static struct resource msc_mem_resource = {
	.name	= "MSC PCI MEM",
	.flags	= IORESOURCE_MEM,
};

static struct resource msc_io_resource = {
	.name	= "MSC PCI I/O",
	.flags	= IORESOURCE_IO,
};

extern struct pci_ops bonito64_pci_ops;
extern struct pci_ops gt64xxx_pci0_ops;
extern struct pci_ops msc_pci_ops;

static struct pci_controller bonito64_controller = {
	.pci_ops	= &bonito64_pci_ops,
	.io_resource	= &bonito64_io_resource,
	.mem_resource	= &bonito64_mem_resource,
	.io_offset	= 0x00000000UL,
};

static struct pci_controller gt64120_controller = {
	.pci_ops	= &gt64xxx_pci0_ops,
	.io_resource	= &gt64120_io_resource,
	.mem_resource	= &gt64120_mem_resource,
};

static struct pci_controller msc_controller = {
	.pci_ops	= &msc_pci_ops,
	.io_resource	= &msc_io_resource,
	.mem_resource	= &msc_mem_resource,
};

void __init mips_pcibios_init(void)
{
	struct pci_controller *controller;
	resource_size_t start, end, map, start1, end1, map1, map2, map3, mask;

	switch (mips_revision_sconid) {
	case MIPS_REVISION_SCON_GT64120:
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
			 GT_PCI0_CFGADDR_CONFIGEN_BIT);

		/* Perform the write */
		GT_WRITE(GT_PCI0_CFGDATA_OFS, CPHYSADDR(MIPS_GT_BASE));

		/* Set up resource ranges from the controller's registers.  */
		start = GT_READ(GT_PCI0M0LD_OFS);
		end = GT_READ(GT_PCI0M0HD_OFS);
		map = GT_READ(GT_PCI0M0REMAP_OFS);
		end = (end & GT_PCI_HD_MSK) | (start & ~GT_PCI_HD_MSK);
		start1 = GT_READ(GT_PCI0M1LD_OFS);
		end1 = GT_READ(GT_PCI0M1HD_OFS);
		map1 = GT_READ(GT_PCI0M1REMAP_OFS);
		end1 = (end1 & GT_PCI_HD_MSK) | (start1 & ~GT_PCI_HD_MSK);
		/* Cannot support multiple windows, use the wider.  */
		if (end1 - start1 > end - start) {
			start = start1;
			end = end1;
			map = map1;
		}
		mask = ~(start ^ end);
                /* We don't support remapping with a discontiguous mask.  */
		BUG_ON((start & GT_PCI_HD_MSK) != (map & GT_PCI_HD_MSK) &&
		       mask != ~((mask & -mask) - 1));
		gt64120_mem_resource.start = start;
		gt64120_mem_resource.end = end;
		gt64120_controller.mem_offset = (start & mask) - (map & mask);
		/* Addresses are 36-bit, so do shifts in the destinations.  */
		gt64120_mem_resource.start <<= GT_PCI_DCRM_SHF;
		gt64120_mem_resource.end <<= GT_PCI_DCRM_SHF;
		gt64120_mem_resource.end |= (1 << GT_PCI_DCRM_SHF) - 1;
		gt64120_controller.mem_offset <<= GT_PCI_DCRM_SHF;

		start = GT_READ(GT_PCI0IOLD_OFS);
		end = GT_READ(GT_PCI0IOHD_OFS);
		map = GT_READ(GT_PCI0IOREMAP_OFS);
		end = (end & GT_PCI_HD_MSK) | (start & ~GT_PCI_HD_MSK);
		mask = ~(start ^ end);
                /* We don't support remapping with a discontiguous mask.  */
		BUG_ON((start & GT_PCI_HD_MSK) != (map & GT_PCI_HD_MSK) &&
		       mask != ~((mask & -mask) - 1));
		gt64120_io_resource.start = map & mask;
		gt64120_io_resource.end = (map & mask) | ~mask;
		gt64120_controller.io_offset = 0;
		/* Addresses are 36-bit, so do shifts in the destinations.  */
		gt64120_io_resource.start <<= GT_PCI_DCRM_SHF;
		gt64120_io_resource.end <<= GT_PCI_DCRM_SHF;
		gt64120_io_resource.end |= (1 << GT_PCI_DCRM_SHF) - 1;

		controller = &gt64120_controller;
		break;

	case MIPS_REVISION_SCON_BONITO:
		/* Set up resource ranges from the controller's registers.  */
		map = BONITO_PCIMAP;
		map1 = (BONITO_PCIMAP & BONITO_PCIMAP_PCIMAP_LO0) >>
		       BONITO_PCIMAP_PCIMAP_LO0_SHIFT;
		map2 = (BONITO_PCIMAP & BONITO_PCIMAP_PCIMAP_LO1) >>
		       BONITO_PCIMAP_PCIMAP_LO1_SHIFT;
		map3 = (BONITO_PCIMAP & BONITO_PCIMAP_PCIMAP_LO2) >>
		       BONITO_PCIMAP_PCIMAP_LO2_SHIFT;
		/* Combine as many adjacent windows as possible.  */
		map = map1;
		start = BONITO_PCILO0_BASE;
		end = 1;
		if (map3 == map2 + 1) {
			map = map2;
			start = BONITO_PCILO1_BASE;
			end++;
		}
		if (map2 == map1 + 1) {
			map = map1;
			start = BONITO_PCILO0_BASE;
			end++;
		}
		bonito64_mem_resource.start = start;
		bonito64_mem_resource.end = start +
					    BONITO_PCIMAP_WINBASE(end) - 1;
		bonito64_controller.mem_offset = start -
						 BONITO_PCIMAP_WINBASE(map);

		controller = &bonito64_controller;
		break;

	case MIPS_REVISION_SCON_SOCIT:
	case MIPS_REVISION_SCON_ROCIT:
	case MIPS_REVISION_SCON_SOCITSC:
	case MIPS_REVISION_SCON_SOCITSCP:
		/* Set up resource ranges from the controller's registers.  */
		MSC_READ(MSC01_PCI_SC2PMBASL, start);
		MSC_READ(MSC01_PCI_SC2PMMSKL, mask);
		MSC_READ(MSC01_PCI_SC2PMMAPL, map);
		msc_mem_resource.start = start & mask;
		msc_mem_resource.end = (start & mask) | ~mask;
		msc_controller.mem_offset = (start & mask) - (map & mask);

		MSC_READ(MSC01_PCI_SC2PIOBASL, start);
		MSC_READ(MSC01_PCI_SC2PIOMSKL, mask);
		MSC_READ(MSC01_PCI_SC2PIOMAPL, map);
		msc_io_resource.start = map & mask;
		msc_io_resource.end = (map & mask) | ~mask;
		msc_controller.io_offset = 0;
		ioport_resource.end = ~mask;

		/* If ranges overlap I/O takes precedence.  */
		start = start & mask;
		end = start | ~mask;
		if ((start >= msc_mem_resource.start &&
		     start <= msc_mem_resource.end) ||
		    (end >= msc_mem_resource.start &&
		     end <= msc_mem_resource.end)) {
			/* Use the larger space.  */
			start = max(start, msc_mem_resource.start);
			end = min(end, msc_mem_resource.end);
			if (start - msc_mem_resource.start >=
			    msc_mem_resource.end - end)
				msc_mem_resource.end = start - 1;
			else
				msc_mem_resource.start = end + 1;
		}

		controller = &msc_controller;
		break;
	default:
		return;
	}

	if (controller->io_resource->start < 0x00001000UL)	/* FIXME */
		controller->io_resource->start = 0x00001000UL;

	iomem_resource.end &= 0xfffffffffULL;			/* 64 GB */
	ioport_resource.end = controller->io_resource->end;

	register_pci_controller(controller);
}
