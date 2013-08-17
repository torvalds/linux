#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>

int __init pcibios_map_platform_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;

	if (dev->bus->number == 0) {
		switch (slot) {
		case 4: return 5;	/* eth0       */
		case 8: return 5;	/* eth1       */
		case 6: return 2;	/* PCI bridge */
		default:
			printk(KERN_ERR "PCI: Bad IRQ mapping request "
					"for slot %d\n", slot);
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
