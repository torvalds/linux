#include <linux/init.h>
#include <linux/pci.h>
#include <asm/mach-powertv/interrupts.h>
#include "powertv-pci.h"

int __init pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	return asic_pcie_map_irq(dev, slot, pin);
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

/*
 * asic_pcie_map_irq
 *
 * Parameters:
 * *dev - pointer to a pci_dev structure  (not used)
 * slot - slot number  (not used)
 * pin - pin number  (not used)
 *
 * Return Value:
 * Returns: IRQ number (always the PCI Express IRQ number)
 *
 * Description:
 * asic_pcie_map_irq will return the IRQ number of the PCI Express interrupt.
 *
 */
int asic_pcie_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	return irq_pciexp;
}
EXPORT_SYMBOL(asic_pcie_map_irq);
