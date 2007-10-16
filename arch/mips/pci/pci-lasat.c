/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000, 2001, 04 Keith M Wesolowski
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <asm/bootinfo.h>
#include <asm/lasat/lasatint.h>

extern struct pci_ops nile4_pci_ops;
extern struct pci_ops gt64xxx_pci0_ops;
static struct resource lasat_pci_mem_resource = {
	.name	= "LASAT PCI MEM",
	.start	= 0x18000000,
	.end	= 0x19ffffff,
	.flags	= IORESOURCE_MEM,
};

static struct resource lasat_pci_io_resource = {
	.name	= "LASAT PCI IO",
	.start	= 0x1a000000,
	.end	= 0x1bffffff,
	.flags	= IORESOURCE_IO,
};

static struct pci_controller lasat_pci_controller = {
	.mem_resource	= &lasat_pci_mem_resource,
	.io_resource	= &lasat_pci_io_resource,
};

static int __init lasat_pci_setup(void)
{
	printk(KERN_DEBUG "PCI: starting\n");

	switch (mips_machtype) {
	case MACH_LASAT_100:
		lasat_pci_controller.pci_ops = &gt64xxx_pci0_ops;
		break;
	case MACH_LASAT_200:
		lasat_pci_controller.pci_ops = &nile4_pci_ops;
		break;
	default:
		panic("pcibios_init: mips_machtype incorrect");
	}

	register_pci_controller(&lasat_pci_controller);

	return 0;
}

arch_initcall(lasat_pci_setup);

#define LASATINT_ETH1   (LASATINT_BASE + 0)
#define LASATINT_ETH0   (LASATINT_BASE + 1)
#define LASATINT_HDC    (LASATINT_BASE + 2)
#define LASATINT_COMP   (LASATINT_BASE + 3)
#define LASATINT_HDLC   (LASATINT_BASE + 4)
#define LASATINT_PCIA   (LASATINT_BASE + 5)
#define LASATINT_PCIB   (LASATINT_BASE + 6)
#define LASATINT_PCIC   (LASATINT_BASE + 7)
#define LASATINT_PCID   (LASATINT_BASE + 8)

int __init pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	switch (slot) {
	case 1:
	case 2:
	case 3:
		return LASATINT_PCIA + (((slot-1) + (pin-1)) % 4);
	case 4:
		return LASATINT_ETH1;   /* Ethernet 1 (LAN 2) */
	case 5:
		return LASATINT_ETH0;   /* Ethernet 0 (LAN 1) */
	case 6:
		return LASATINT_HDC;    /* IDE controller */
	default:
		return 0xff;            /* Illegal */
	}

	return -1;
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
