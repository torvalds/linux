/*
 *  Copyright (C) 2004 by Basler Vision Technologies AG
 *  Author: Thomas Koeller <thomas.koeller@baslerweb.com>
 *  Based on the PMC-Sierra Yosemite board support by Ralf Baechle.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/bitops.h>
#include <asm/rm9k-ocd.h>
#include <excite.h>


extern struct pci_ops titan_pci_ops;


static struct resource
	mem_resource = 	{
		.name	= "PCI memory",
		.start	= EXCITE_PHYS_PCI_MEM,
		.end	= EXCITE_PHYS_PCI_MEM + EXCITE_SIZE_PCI_MEM - 1,
		.flags	= IORESOURCE_MEM
	},
	io_resource = {
		.name	= "PCI I/O",
		.start	= EXCITE_PHYS_PCI_IO,
		.end	= EXCITE_PHYS_PCI_IO + EXCITE_SIZE_PCI_IO - 1,
		.flags	= IORESOURCE_IO
	};


static struct pci_controller bx_controller = {
	.pci_ops	= &titan_pci_ops,
	.mem_resource	= &mem_resource,
	.mem_offset	= 0x00000000UL,
	.io_resource	= &io_resource,
	.io_offset	= 0x00000000UL
};


static char
	iopage_failed[] __initdata   = "Cannot allocate PCI I/O page",
	modebits_no_pci[] __initdata = "PCI is not configured in mode bits";

#define RM9000x2_OCD_HTSC	0x0604
#define RM9000x2_OCD_HTBHL	0x060c
#define RM9000x2_OCD_PCIHRST	0x078c

#define RM9K_OCD_MODEBIT1	0x00d4 /* (MODEBIT1) Mode Bit 1 */
#define RM9K_OCD_CPHDCR		0x00f4 /* CPU-PCI/HT Data Control. */

#define PCISC_FB2B 		0x00000200
#define PCISC_MWICG		0x00000010
#define PCISC_EMC		0x00000004
#define PCISC_ERMA		0x00000002



static int __init basler_excite_pci_setup(void)
{
	const unsigned int fullbars = memsize / (256 << 20);
	unsigned int i;

	/* Check modebits to see if PCI is really enabled. */
	if (!((ocd_readl(RM9K_OCD_MODEBIT1) >> (47-32)) & 0x1))
		panic(modebits_no_pci);

	if (NULL == request_mem_region(EXCITE_PHYS_PCI_IO, EXCITE_SIZE_PCI_IO,
				       "Memory-mapped PCI I/O page"))
		panic(iopage_failed);

	/* Enable PCI 0 as master for config cycles */
	ocd_writel(PCISC_EMC | PCISC_ERMA, RM9000x2_OCD_HTSC);


	/* Set up latency timer */
	ocd_writel(0x8008, RM9000x2_OCD_HTBHL);

	/*  Setup host IO and Memory space */
	ocd_writel((EXCITE_PHYS_PCI_IO >> 4) | 1, LKB7);
	ocd_writel(((EXCITE_SIZE_PCI_IO >> 4) & 0x7fffff00) - 0x100, LKM7);
	ocd_writel((EXCITE_PHYS_PCI_MEM >> 4) | 1, LKB8);
	ocd_writel(((EXCITE_SIZE_PCI_MEM >> 4) & 0x7fffff00) - 0x100, LKM8);

	/* Set up PCI BARs to map all installed memory */
	for (i = 0; i < 6; i++) {
		const unsigned int bar = 0x610 + i * 4;

	     	if (i < fullbars) {
			ocd_writel(0x10000000 * i, bar);
			ocd_writel(0x01000000 * i, bar + 0x140);
			ocd_writel(0x0ffff029, bar + 0x100);
			continue;
		}

	     	if (i == fullbars) {
			int o;
			u32 mask;

			const unsigned long rem = memsize - i * 0x10000000;
			if (!rem) {
				ocd_writel(0x00000000, bar + 0x100);
				continue;
			}

			o = ffs(rem) - 1;
			if (rem & ~(0x1 << o))
				o++;
			mask = ((0x1 << o) & 0x0ffff000) - 0x1000;
			ocd_writel(0x10000000 * i, bar);
			ocd_writel(0x01000000 * i, bar + 0x140);
			ocd_writel(0x00000029 | mask, bar + 0x100);
			continue;
		}

		ocd_writel(0x00000000, bar + 0x100);
	}

	/* Finally, enable the PCI interrupt */
#if USB_IRQ > 7
	set_c0_intcontrol(1 << USB_IRQ);
#else
	set_c0_status(1 << (USB_IRQ + 8));
#endif

	ioport_resource.start = EXCITE_PHYS_PCI_IO;
	ioport_resource.end = EXCITE_PHYS_PCI_IO + EXCITE_SIZE_PCI_IO - 1;
	set_io_port_base((unsigned long) ioremap_nocache(EXCITE_PHYS_PCI_IO, EXCITE_SIZE_PCI_IO));
	register_pci_controller(&bx_controller);
	return 0;
}


arch_initcall(basler_excite_pci_setup);
