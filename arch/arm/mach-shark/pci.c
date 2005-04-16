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

#include <asm/irq.h>
#include <asm/mach/pci.h>
#include <asm/mach-types.h>

static int __init shark_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	if (dev->bus->number == 0)
		if (dev->devfn == 0) return 255;
		else return 11;
	else return 255;
}

extern void __init via82c505_preinit(void);

static struct hw_pci shark_pci __initdata = {
	.setup		= via82c505_setup,
	.swizzle     	= pci_std_swizzle,
	.map_irq	= shark_map_irq,
	.nr_controllers = 1,
	.scan		= via82c505_scan_bus,
	.preinit	= via82c505_preinit,
};

static int __init shark_pci_init(void)
{
	if (machine_is_shark())
		pci_common_init(&shark_pci);
	return 0;
}

subsys_initcall(shark_pci_init);
