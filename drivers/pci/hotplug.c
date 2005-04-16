#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/module.h>
#include "pci.h"

int pci_hotplug (struct device *dev, char **envp, int num_envp,
		 char *buffer, int buffer_size)
{
	struct pci_dev *pdev;
	char *scratch;
	int i = 0;
	int length = 0;

	if (!dev)
		return -ENODEV;

	pdev = to_pci_dev(dev);
	if (!pdev)
		return -ENODEV;

	scratch = buffer;

	/* stuff we want to pass to /sbin/hotplug */
	envp[i++] = scratch;
	length += scnprintf (scratch, buffer_size - length, "PCI_CLASS=%04X",
			    pdev->class);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;
	++length;
	scratch += length;

	envp[i++] = scratch;
	length += scnprintf (scratch, buffer_size - length, "PCI_ID=%04X:%04X",
			    pdev->vendor, pdev->device);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;
	++length;
	scratch += length;

	envp[i++] = scratch;
	length += scnprintf (scratch, buffer_size - length,
			    "PCI_SUBSYS_ID=%04X:%04X", pdev->subsystem_vendor,
			    pdev->subsystem_device);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;
	++length;
	scratch += length;

	envp[i++] = scratch;
	length += scnprintf (scratch, buffer_size - length, "PCI_SLOT_NAME=%s",
			    pci_name(pdev));
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;

	envp[i] = NULL;

	return 0;
}

static int pci_visit_bus (struct pci_visit * fn, struct pci_bus_wrapped *wrapped_bus, struct pci_dev_wrapped *wrapped_parent)
{
	struct list_head *ln;
	struct pci_dev *dev;
	struct pci_dev_wrapped wrapped_dev;
	int result = 0;

	pr_debug("PCI: Scanning bus %04x:%02x\n", pci_domain_nr(wrapped_bus->bus),
		wrapped_bus->bus->number);

	if (fn->pre_visit_pci_bus) {
		result = fn->pre_visit_pci_bus(wrapped_bus, wrapped_parent);
		if (result)
			return result;
	}

	ln = wrapped_bus->bus->devices.next; 
	while (ln != &wrapped_bus->bus->devices) {
		dev = pci_dev_b(ln);
		ln = ln->next;

		memset(&wrapped_dev, 0, sizeof(struct pci_dev_wrapped));
		wrapped_dev.dev = dev;

		result = pci_visit_dev(fn, &wrapped_dev, wrapped_bus);
		if (result)
			return result;
	}

	if (fn->post_visit_pci_bus)
		result = fn->post_visit_pci_bus(wrapped_bus, wrapped_parent);

	return result;
}

static int pci_visit_bridge (struct pci_visit * fn,
			     struct pci_dev_wrapped *wrapped_dev,
			     struct pci_bus_wrapped *wrapped_parent)
{
	struct pci_bus *bus;
	struct pci_bus_wrapped wrapped_bus;
	int result = 0;

	pr_debug("PCI: Scanning bridge %s\n", pci_name(wrapped_dev->dev));

	if (fn->visit_pci_dev) {
		result = fn->visit_pci_dev(wrapped_dev, wrapped_parent);
		if (result)
			return result;
	}

	bus = wrapped_dev->dev->subordinate;
	if (bus) {
		memset(&wrapped_bus, 0, sizeof(struct pci_bus_wrapped));
		wrapped_bus.bus = bus;

		result = pci_visit_bus(fn, &wrapped_bus, wrapped_dev);
	}
	return result;
}

/**
 * pci_visit_dev - scans the pci buses.
 * Every bus and every function is presented to a custom
 * function that can act upon it.
 */
int pci_visit_dev(struct pci_visit *fn, struct pci_dev_wrapped *wrapped_dev,
		  struct pci_bus_wrapped *wrapped_parent)
{
	struct pci_dev* dev = wrapped_dev ? wrapped_dev->dev : NULL;
	int result = 0;

	if (!dev)
		return 0;

	if (fn->pre_visit_pci_dev) {
		result = fn->pre_visit_pci_dev(wrapped_dev, wrapped_parent);
		if (result)
			return result;
	}

	switch (dev->class >> 8) {
		case PCI_CLASS_BRIDGE_PCI:
			result = pci_visit_bridge(fn, wrapped_dev,
						  wrapped_parent);
			if (result)
				return result;
			break;
		default:
			pr_debug("PCI: Scanning device %s\n", pci_name(dev));
			if (fn->visit_pci_dev) {
				result = fn->visit_pci_dev (wrapped_dev,
							    wrapped_parent);
				if (result)
					return result;
			}
	}

	if (fn->post_visit_pci_dev)
		result = fn->post_visit_pci_dev(wrapped_dev, wrapped_parent);

	return result;
}
EXPORT_SYMBOL(pci_visit_dev);
