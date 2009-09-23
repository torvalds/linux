/*
 * Copyright (C) 2007 Lemote, Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/pci.h>

#include <pci.h>
#include <loongson.h>

static struct resource loongson_pci_mem_resource = {
	.name   = "pci memory space",
	.start  = LOONGSON_PCI_MEM_START,
	.end    = LOONGSON_PCI_MEM_END,
	.flags  = IORESOURCE_MEM,
};

static struct resource loongson_pci_io_resource = {
	.name   = "pci io space",
	.start  = LOONGSON_PCI_IO_START,
	.end    = IO_SPACE_LIMIT,
	.flags  = IORESOURCE_IO,
};

static struct pci_controller  loongson_pci_controller = {
	.pci_ops        = &bonito64_pci_ops,
	.io_resource    = &loongson_pci_io_resource,
	.mem_resource   = &loongson_pci_mem_resource,
	.mem_offset     = 0x00000000UL,
	.io_offset      = 0x00000000UL,
};

static void __init setup_pcimap(void)
{
	/*
	 * local to PCI mapping for CPU accessing PCI space
	 * CPU address space [256M,448M] is window for accessing pci space
	 * we set pcimap_lo[0,1,2] to map it to pci space[0M,64M], [320M,448M]
	 *
	 * pcimap: PCI_MAP2  PCI_Mem_Lo2 PCI_Mem_Lo1 PCI_Mem_Lo0
	 * 	     [<2G]   [384M,448M] [320M,384M] [0M,64M]
	 */
	BONITO_PCIMAP = BONITO_PCIMAP_PCIMAP_2 |
		BONITO_PCIMAP_WIN(2, BONITO_PCILO2_BASE) |
		BONITO_PCIMAP_WIN(1, BONITO_PCILO1_BASE) |
		BONITO_PCIMAP_WIN(0, 0);

	/*
	 * PCI-DMA to local mapping: [2G,2G+256M] -> [0M,256M]
	 */
	BONITO_PCIBASE0 = 0x80000000ul;   /* base: 2G -> mmap: 0M */
	/* size: 256M, burst transmission, pre-fetch enable, 64bit */
	LOONGSON_PCI_HIT0_SEL_L = 0xc000000cul;
	LOONGSON_PCI_HIT0_SEL_H = 0xfffffffful;
	LOONGSON_PCI_HIT1_SEL_L = 0x00000006ul; /* set this BAR as invalid */
	LOONGSON_PCI_HIT1_SEL_H = 0x00000000ul;
	LOONGSON_PCI_HIT2_SEL_L = 0x00000006ul; /* set this BAR as invalid */
	LOONGSON_PCI_HIT2_SEL_H = 0x00000000ul;

	/* avoid deadlock of PCI reading/writing lock operation */
	LOONGSON_PCI_ISR4C = 0xd2000001ul;

	/* can not change gnt to break pci transfer when device's gnt not
	deassert for some broken device */
	LOONGSON_PXARB_CFG = 0x00fe0105ul;
}

static int __init pcibios_init(void)
{
	setup_pcimap();

	loongson_pci_controller.io_map_base = mips_io_port_base;

	register_pci_controller(&loongson_pci_controller);

	return 0;
}

arch_initcall(pcibios_init);
