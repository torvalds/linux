#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/hw_irq.h>
#include "pci.h"

struct pci_bus * __devinit pci_acpi_scan_root(struct acpi_device *device, int domain, int busnum)
{
	if (domain != 0) {
		printk(KERN_WARNING "PCI: Multiple domains not supported\n");
		return NULL;
	}

	return pcibios_scan_root(busnum);
}

extern int pci_routeirq;
static int __init pci_acpi_init(void)
{
	struct pci_dev *dev = NULL;

	if (pcibios_scanned)
		return 0;

	if (acpi_noirq)
		return 0;

	printk(KERN_INFO "PCI: Using ACPI for IRQ routing\n");
	acpi_irq_penalty_init();
	pcibios_scanned++;
	pcibios_enable_irq = acpi_pci_irq_enable;

	if (pci_routeirq) {
		/*
		 * PCI IRQ routing is set up by pci_enable_device(), but we
		 * also do it here in case there are still broken drivers that
		 * don't use pci_enable_device().
		 */
		printk(KERN_INFO "PCI: Routing PCI interrupts for all devices because \"pci=routeirq\" specified\n");
		while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL)
			acpi_pci_irq_enable(dev);
	} else
		printk(KERN_INFO "PCI: If a device doesn't work, try \"pci=routeirq\".  If it helps, post a report\n");

#ifdef CONFIG_X86_IO_APIC
	if (acpi_ioapic)
		print_IO_APIC();
#endif

	return 0;
}
subsys_initcall(pci_acpi_init);
