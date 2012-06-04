/*
 * linux/arch/arm/mach-footbridge/cats-pci.c
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

/* cats host-specific stuff */
static int irqmap_cats[] __initdata = { IRQ_PCI, IRQ_IN0, IRQ_IN1, IRQ_IN3 };

static u8 cats_no_swizzle(struct pci_dev *dev, u8 *pin)
{
	return 0;
}

static int __init cats_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	if (dev->irq >= 255)
		return -1;	/* not a valid interrupt. */

	if (dev->irq >= 128)
		return dev->irq & 0x1f;

	if (dev->irq >= 1 && dev->irq <= 4)
		return irqmap_cats[dev->irq - 1];

	if (dev->irq != 0)
		printk("PCI: device %02x:%02x has unknown irq line %x\n",
		       dev->bus->number, dev->devfn, dev->irq);

	return -1;
}

/*
 * why not the standard PCI swizzle?  does this prevent 4-port tulip
 * cards being used (ie, pci-pci bridge based cards)?
 */
static struct hw_pci cats_pci __initdata = {
	.swizzle		= cats_no_swizzle,
	.map_irq		= cats_map_irq,
	.nr_controllers		= 1,
	.ops			= &dc21285_ops,
	.setup			= dc21285_setup,
	.preinit		= dc21285_preinit,
	.postinit		= dc21285_postinit,
};

static int __init cats_pci_init(void)
{
	if (machine_is_cats())
		pci_common_init(&cats_pci);
	return 0;
}

subsys_initcall(cats_pci_init);
