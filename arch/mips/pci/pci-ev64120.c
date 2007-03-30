#include <linux/pci.h>
#include <asm/irq.h>

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;

	if (!pin)
		return 0;

	irq = allocate_irqno();
	if (irq < 0)
		return 0;

	return irq;
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
