/*
 *  Copyright (C) NEC Electronics Corporation 2004-2006
 *
 *  This file is based on the arch/mips/ddb5xxx/ddb5477/pci.c
 *
 *	Copyright 2001 MontaVista Software Inc.
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>

#include <asm/bootinfo.h>

#include <asm/emma/emma2rh.h>

static struct resource pci_io_resource = {
	.name = "pci IO space",
	.start = EMMA2RH_PCI_IO_BASE,
	.end = EMMA2RH_PCI_IO_BASE + EMMA2RH_PCI_IO_SIZE - 1,
	.flags = IORESOURCE_IO,
};

static struct resource pci_mem_resource = {
	.name = "pci memory space",
	.start = EMMA2RH_PCI_MEM_BASE,
	.end = EMMA2RH_PCI_MEM_BASE + EMMA2RH_PCI_MEM_SIZE - 1,
	.flags = IORESOURCE_MEM,
};

extern struct pci_ops emma2rh_pci_ops;

static struct pci_controller emma2rh_pci_controller = {
	.pci_ops = &emma2rh_pci_ops,
	.mem_resource = &pci_mem_resource,
	.io_resource = &pci_io_resource,
	.mem_offset = -0x04000000,
	.io_offset = 0,
};

static void __init emma2rh_pci_init(void)
{
	/* setup PCI interface */
	emma2rh_out32(EMMA2RH_PCI_ARBIT_CTR, 0x70f);

	emma2rh_out32(EMMA2RH_PCI_IWIN0_CTR, 0x80000a18);
	emma2rh_out32(EMMA2RH_PCI_CONFIG_BASE + PCI_COMMAND,
		      PCI_STATUS_DEVSEL_MEDIUM | PCI_STATUS_CAP_LIST |
		      PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY);
	emma2rh_out32(EMMA2RH_PCI_CONFIG_BASE + PCI_BASE_ADDRESS_0, 0x10000000);
	emma2rh_out32(EMMA2RH_PCI_CONFIG_BASE + PCI_BASE_ADDRESS_1, 0x00000000);

	emma2rh_out32(EMMA2RH_PCI_IWIN0_CTR, 0x12000000 | 0x218);
	emma2rh_out32(EMMA2RH_PCI_IWIN1_CTR, 0x18000000 | 0x600);
	emma2rh_out32(EMMA2RH_PCI_INIT_ESWP, 0x00000200);

	emma2rh_out32(EMMA2RH_PCI_TWIN_CTR, 0x00009200);
	emma2rh_out32(EMMA2RH_PCI_TWIN_BADR, 0x00000000);
	emma2rh_out32(EMMA2RH_PCI_TWIN0_DADR, 0x00000000);
	emma2rh_out32(EMMA2RH_PCI_TWIN1_DADR, 0x00000000);
}

static int __init emma2rh_pci_setup(void)
{
	emma2rh_pci_init();
	register_pci_controller(&emma2rh_pci_controller);
	return 0;
}

arch_initcall(emma2rh_pci_setup);
