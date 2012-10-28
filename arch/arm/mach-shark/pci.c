/*
 *  linux/arch/arm/mach-shark/pci.c
 *
 *  PCI bios-type initialisation for PCI machines
 *
 *  Bits taken from various places.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/io.h>
#include <video/vga.h>

#include <asm/irq.h>
#include <asm/mach/pci.h>
#include <asm/mach-types.h>

#define IO_START	0x40000000

static int __init shark_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	if (dev->bus->number == 0)
		if (dev->devfn == 0)
			return 255;
		else
			return 11;
	else
		return 255;
}

extern void __init via82c505_preinit(void);

static struct hw_pci shark_pci __initdata = {
	.setup		= via82c505_setup,
	.map_irq	= shark_map_irq,
	.nr_controllers = 1,
	.ops		= &via82c505_ops,
	.preinit	= via82c505_preinit,
};

static int __init shark_pci_init(void)
{
	if (!machine_is_shark())
		return -ENODEV;

	pcibios_min_io = 0x6000;
	pcibios_min_mem = 0x50000000;
	vga_base = 0xe8000000;

	pci_ioremap_io(0, IO_START);

	pci_common_init(&shark_pci);

	return 0;
}

subsys_initcall(shark_pci_init);
