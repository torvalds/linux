#include <linux/pci.h>
#include <linux/kernel.h>

static int __init
early_read_config_word(struct pci_channel *hose,
		       int top_bus, int bus, int devfn, int offset, u16 *value)
{
	struct pci_dev fake_dev;
	struct pci_bus fake_bus;

	fake_dev.bus = &fake_bus;
	fake_dev.sysdata = hose;
	fake_dev.devfn = devfn;
	fake_bus.number = bus;
	fake_bus.sysdata = hose;
	fake_bus.ops = hose->pci_ops;

	if (bus != top_bus)
		/* Fake a parent bus structure. */
		fake_bus.parent = &fake_bus;
	else
		fake_bus.parent = NULL;

	return pci_read_config_word(&fake_dev, offset, value);
}

int __init pci_is_66mhz_capable(struct pci_channel *hose,
				int top_bus, int current_bus)
{
	u32 pci_devfn;
	unsigned short vid;
	int cap66 = -1;
	u16 stat;

	printk(KERN_INFO "PCI: Checking 66MHz capabilities...\n");

	for (pci_devfn = 0; pci_devfn < 0xff; pci_devfn++) {
		if (PCI_FUNC(pci_devfn))
			continue;
		if (early_read_config_word(hose, top_bus, current_bus,
					   pci_devfn, PCI_VENDOR_ID, &vid) !=
		    PCIBIOS_SUCCESSFUL)
			continue;
		if (vid == 0xffff)
			continue;

		/* check 66MHz capability */
		if (cap66 < 0)
			cap66 = 1;
		if (cap66) {
			early_read_config_word(hose, top_bus, current_bus,
					       pci_devfn, PCI_STATUS, &stat);
			if (!(stat & PCI_STATUS_66MHZ)) {
				printk(KERN_DEBUG
				       "PCI: %02x:%02x not 66MHz capable.\n",
				       current_bus, pci_devfn);
				cap66 = 0;
				break;
			}
		}
	}

	return cap66 > 0;
}
