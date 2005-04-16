#include <linux/pci.h>
#include <linux/init.h>

int pci_range_ck(unsigned char bus, unsigned char dev)
{
	if (((bus == 0) || (bus == 1)) && (dev >= 6) && (dev <= 8))
		return 0;

	return -1;
}

/*
 * After detecting all agents over the PCI , this function is called
 * in order to give an interrupt number for each PCI device starting
 * from IRQ 20. It does also enables master for each device.
 */
void __devinit pcibios_fixup_bus(struct pci_bus *bus)
{
	unsigned int irq = 20;
	struct pci_bus *current_bus = bus;
	struct pci_dev *dev;
	struct list_head *devices_link;

	list_for_each(devices_link, &(current_bus->devices)) {
		dev = pci_dev_b(devices_link);
		if (dev != NULL) {
			dev->irq = irq++;

			/* Assign an interrupt number for the device */
			pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
			pcibios_set_master(dev);
		}
	}
}
