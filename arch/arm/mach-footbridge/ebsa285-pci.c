/*
 * linux/arch/arm/mach-footbridge/ebsa285-pci.c
 *
 * PCI bios-type initialisation for PCI machines
 *
 * Bits taken from various places.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/irq.h>
#include <asm/mach/pci.h>
#include <asm/mach-types.h>

static int irqmap_ebsa285[] __initdata = { IRQ_IN3, IRQ_IN1, IRQ_IN0, IRQ_PCI };

static int __init ebsa285_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	if (dev->vendor == PCI_VENDOR_ID_CONTAQ &&
	    dev->device == PCI_DEVICE_ID_CONTAQ_82C693)
		switch (PCI_FUNC(dev->devfn)) {
		case 1:	return 14;
		case 2:	return 15;
		case 3:	return 12;
		}

	return irqmap_ebsa285[(slot + pin) & 3];
}

static struct hw_pci ebsa285_pci __initdata = {
	.map_irq		= ebsa285_map_irq,
	.nr_controllers		= 1,
	.setup			= dc21285_setup,
	.scan			= dc21285_scan_bus,
	.preinit		= dc21285_preinit,
	.postinit		= dc21285_postinit,
};

static int __init ebsa285_init_pci(void)
{
	if (machine_is_ebsa285())
		pci_common_init(&ebsa285_pci);
	return 0;
}

subsys_initcall(ebsa285_init_pci);
