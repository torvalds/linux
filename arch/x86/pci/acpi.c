#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/dmi.h>
#include <asm/numa.h>
#include "pci.h"

static int __devinit can_skip_ioresource_align(struct dmi_system_id *d)
{
	pci_probe |= PCI_CAN_SKIP_ISA_ALIGN;
	printk(KERN_INFO "PCI: %s detected, can skip ISA alignment\n", d->ident);
	return 0;
}

static struct dmi_system_id acpi_pciprobe_dmi_table[] = {
/*
 * Systems where PCI IO resource ISA alignment can be skipped
 * when the ISA enable bit in the bridge control is not set
 */
	{
		.callback = can_skip_ioresource_align,
		.ident = "IBM System x3800",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IBM"),
			DMI_MATCH(DMI_PRODUCT_NAME, "x3800"),
		},
	},
	{
		.callback = can_skip_ioresource_align,
		.ident = "IBM System x3850",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IBM"),
			DMI_MATCH(DMI_PRODUCT_NAME, "x3850"),
		},
	},
	{
		.callback = can_skip_ioresource_align,
		.ident = "IBM System x3950",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IBM"),
			DMI_MATCH(DMI_PRODUCT_NAME, "x3950"),
		},
	},
	{}
};

struct pci_bus * __devinit pci_acpi_scan_root(struct acpi_device *device, int domain, int busnum)
{
	struct pci_bus *bus;
	struct pci_sysdata *sd;
	int pxm;

	dmi_check_system(acpi_pciprobe_dmi_table);

	/* Allocate per-root-bus (not per bus) arch-specific data.
	 * TODO: leak; this memory is never freed.
	 * It's arguable whether it's worth the trouble to care.
	 */
	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd) {
		printk(KERN_ERR "PCI: OOM, not probing PCI bus %02x\n", busnum);
		return NULL;
	}

	if (domain != 0) {
		printk(KERN_WARNING "PCI: Multiple domains not supported\n");
		kfree(sd);
		return NULL;
	}

	sd->node = -1;

	pxm = acpi_get_pxm(device->handle);
#ifdef CONFIG_ACPI_NUMA
	if (pxm >= 0)
		sd->node = pxm_to_node(pxm);
#endif

	bus = pci_scan_bus_parented(NULL, busnum, &pci_root_ops, sd);
	if (!bus)
		kfree(sd);

#ifdef CONFIG_ACPI_NUMA
	if (bus != NULL) {
		if (pxm >= 0) {
			printk("bus %d -> pxm %d -> node %d\n",
				busnum, pxm, sd->node);
		}
	}
#endif
	
	return bus;
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
	pcibios_disable_irq = acpi_pci_irq_disable;

	if (pci_routeirq) {
		/*
		 * PCI IRQ routing is set up by pci_enable_device(), but we
		 * also do it here in case there are still broken drivers that
		 * don't use pci_enable_device().
		 */
		printk(KERN_INFO "PCI: Routing PCI interrupts for all devices because \"pci=routeirq\" specified\n");
		for_each_pci_dev(dev)
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
