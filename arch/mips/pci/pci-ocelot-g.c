/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
 *
 * This doesn't really fly - but I don't have a GT64240 system for testing.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <asm/gt64240.h>

/*
 * We assume these address ranges have been programmed into the GT-64240 by
 * the firmware.  PMON in case of the Ocelot G does that.  Note the size of
 * the I/O range is completly stupid; I/O mappings are limited to at most
 * 256 bytes by the PCI spec and deprecated; and just to make things worse
 * apparently many devices don't decode more than 64k of I/O space.
 */

#define gt_io_size	0x20000000UL
#define gt_io_base	0xe0000000UL

static struct resource gt_pci_mem0_resource = {
	.name	= "MV64240 PCI0 MEM",
	.start	= 0xc0000000UL,
	.end	= 0xcfffffffUL,
	.flags	= IORESOURCE_MEM
};

static struct resource gt_pci_io_mem0_resource = {
	.name	= "MV64240 PCI0 IO MEM",
	.start	= 0xe0000000UL,
	.end	= 0xefffffffUL,
	.flags	= IORESOURCE_IO
};

static struct mv_pci_controller gt_bus0_controller = {
	.pcic = {
		.pci_ops	= &mv_pci_ops,
		.mem_resource	= &gt_pci_mem0_resource,
		.mem_offset	= 0xc0000000UL,
		.io_resource	= &gt_pci_io_mem0_resource,
		.io_offset	= 0x00000000UL
	},
	.config_addr	= PCI_0CONFIGURATION_ADDRESS,
	.config_vreg	= PCI_0CONFIGURATION_DATA_VIRTUAL_REGISTER,
};

static struct resource gt_pci_mem1_resource = {
	.name	= "MV64240 PCI1 MEM",
	.start	= 0xd0000000UL,
	.end	= 0xdfffffffUL,
	.flags	= IORESOURCE_MEM
};

static struct resource gt_pci_io_mem1_resource = {
	.name	= "MV64240 PCI1 IO MEM",
	.start	= 0xf0000000UL,
	.end	= 0xffffffffUL,
	.flags	= IORESOURCE_IO
};

static struct mv_pci_controller gt_bus1_controller = {
	.pcic = {
		.pci_ops	= &mv_pci_ops,
		.mem_resource	= &gt_pci_mem1_resource,
		.mem_offset	= 0xd0000000UL,
		.io_resource	= &gt_pci_io_mem1_resource,
		.io_offset	= 0x10000000UL
	},
	.config_addr	= PCI_1CONFIGURATION_ADDRESS,
	.config_vreg	= PCI_1CONFIGURATION_DATA_VIRTUAL_REGISTER,
};

static __init int __init ocelot_g_pci_init(void)
{
	unsigned long io_v_base;

	if (gt_io_size) {
		io_v_base = (unsigned long) ioremap(gt_io_base, gt_io_size);
		if (!io_v_base)
			panic("Could not ioremap I/O port range");

		set_io_port_base(io_v_base);
	}

	register_pci_controller(&gt_bus0_controller.pcic);
	register_pci_controller(&gt_bus1_controller.pcic);

	return 0;
}

arch_initcall(ocelot_g_pci_init);
