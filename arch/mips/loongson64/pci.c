// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2007 Lemote, Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 */
#include <linux/pci.h>

#include <pci.h>
#include <loongson.h>
#include <boot_param.h>

static struct resource loongson_pci_mem_resource = {
	.name	= "pci memory space",
	.start	= LOONGSON_PCI_MEM_START,
	.end	= LOONGSON_PCI_MEM_END,
	.flags	= IORESOURCE_MEM,
};

static struct resource loongson_pci_io_resource = {
	.name	= "pci io space",
	.start	= LOONGSON_PCI_IO_START,
	.end	= IO_SPACE_LIMIT,
	.flags	= IORESOURCE_IO,
};

static struct pci_controller  loongson_pci_controller = {
	.pci_ops	= &loongson_pci_ops,
	.io_resource	= &loongson_pci_io_resource,
	.mem_resource	= &loongson_pci_mem_resource,
	.mem_offset	= 0x00000000UL,
	.io_offset	= 0x00000000UL,
};


extern int sbx00_acpi_init(void);

static int __init pcibios_init(void)
{

	loongson_pci_controller.io_map_base = mips_io_port_base;
	loongson_pci_mem_resource.start = loongson_sysconf.pci_mem_start_addr;
	loongson_pci_mem_resource.end = loongson_sysconf.pci_mem_end_addr;

	register_pci_controller(&loongson_pci_controller);

	sbx00_acpi_init();

	return 0;
}

arch_initcall(pcibios_init);
