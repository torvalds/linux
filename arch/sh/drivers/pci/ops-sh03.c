/*
 * linux/arch/sh/drivers/pci/ops-sh03.c
 *
 * PCI initialization for the Interface CTP/PCI-SH03 board
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <asm/io.h>
#include "pci-sh7751.h"

/*
 * Description:  This function sets up and initializes the pcic, sets
 * up the BARS, maps the DRAM into the address space etc, etc.
 */
int __init pcibios_init_platform(void)
{
   return 1;
}

static struct resource sh7751_io_resource = {
	.name   = "SH03 IO",
	.start  = SH7751_PCI_IO_BASE,
	.end    = SH7751_PCI_IO_BASE + SH7751_PCI_IO_SIZE - 1,
	.flags  = IORESOURCE_IO
};

static struct resource sh7751_mem_resource = {
	.name   = "SH03 mem",
	.start  = SH7751_PCI_MEMORY_BASE,
	.end    = SH7751_PCI_MEMORY_BASE + SH7751_PCI_MEM_SIZE - 1,
	.flags  = IORESOURCE_MEM
};

extern struct pci_ops sh7751_pci_ops;

struct pci_channel board_pci_channels[] = {
	{ &sh7751_pci_ops, &sh7751_io_resource, &sh7751_mem_resource, 0, 0xff },
	{ NULL, NULL, NULL, 0, 0 },
};

