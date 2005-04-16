#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>

/*
 * 	IRQ functions
 */

int __init pcibios_map_platform_irq(u8 slot, u8 pin, struct pci_dev *dev)
{
	int irq;

	if (dev->bus->number == 0) {
		switch (slot) {
		case 4: return 5;	/* eth0       */
		case 8: return 5;	/* eth1       */
		case 6: return 2;	/* PCI bridge */
		default:
                	printk("PCI: Bad IRQ mapping request for slot %d\n", slot);
                	return 2;
		}
	} else {
		switch (pin) {
		case 0:   irq =  2; break;
		case 1:   irq =  2; break;
		case 2:   irq =  2; break;
		case 3:   irq =  2; break;
		case 4:   irq =  2; break;
		default:  irq = -1; break;
		}
	}
	return irq;
}

static u8 __init sh03_no_swizzle(struct pci_dev *dev, u8 *pin)
{
	/* no swizzling */
	return PCI_SLOT(dev->devfn);
}

static int sh03_pci_lookup_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq = -1;

	/* now lookup the actual IRQ on a platform specific basis (pci-'platform'.c) */
	irq = pcibios_map_platform_irq(slot, pin, dev);
	if( irq < 0 ) {
		pr_debug("PCI: Error mapping IRQ on device %s\n", pci_name(dev));
		return irq;
	}

	pr_debug("Setting IRQ for slot %s to %d\n", pci_name(dev), irq);

	return irq;
}

void __init pcibios_fixup_irqs(void)
{
	pci_fixup_irqs(sh03_no_swizzle, sh03_pci_lookup_irq);
}
