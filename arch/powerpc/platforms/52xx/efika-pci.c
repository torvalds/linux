
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/sections.h>
#include <asm/pci-bridge.h>
#include <asm/rtas.h>

#include "efika.h"

#ifdef CONFIG_PCI
/*
 * Access functions for PCI config space using RTAS calls.
 */
static int rtas_read_config(struct pci_bus *bus, unsigned int devfn, int offset,
			    int len, u32 * val)
{
	struct pci_controller *hose = bus->sysdata;
	unsigned long addr = (offset & 0xff) | ((devfn & 0xff) << 8)
	    | (((bus->number - hose->first_busno) & 0xff) << 16)
	    | (hose->index << 24);
	int ret = -1;
	int rval;

	rval = rtas_call(rtas_token("read-pci-config"), 2, 2, &ret, addr, len);
	*val = ret;
	return rval ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;
}

static int rtas_write_config(struct pci_bus *bus, unsigned int devfn,
			     int offset, int len, u32 val)
{
	struct pci_controller *hose = bus->sysdata;
	unsigned long addr = (offset & 0xff) | ((devfn & 0xff) << 8)
	    | (((bus->number - hose->first_busno) & 0xff) << 16)
	    | (hose->index << 24);
	int rval;

	rval = rtas_call(rtas_token("write-pci-config"), 3, 1, NULL,
			 addr, len, val);
	return rval ? PCIBIOS_DEVICE_NOT_FOUND : PCIBIOS_SUCCESSFUL;
}

static struct pci_ops rtas_pci_ops = {
	rtas_read_config,
	rtas_write_config
};

void __init efika_pcisetup(void)
{
	const int *bus_range;
	int len;
	struct pci_controller *hose;
	struct device_node *root;
	struct device_node *pcictrl;

	root = of_find_node_by_path("/");
	if (root == NULL) {
		printk(KERN_WARNING EFIKA_PLATFORM_NAME
		       ": Unable to find the root node\n");
		return;
	}

	for (pcictrl = NULL;;) {
		pcictrl = of_get_next_child(root, pcictrl);
		if ((pcictrl == NULL) || (strcmp(pcictrl->name, "pci") == 0))
			break;
	}

	of_node_put(root);

	if (pcictrl == NULL) {
		printk(KERN_WARNING EFIKA_PLATFORM_NAME
		       ": Unable to find the PCI bridge node\n");
		return;
	}

	bus_range = get_property(pcictrl, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		printk(KERN_WARNING EFIKA_PLATFORM_NAME
		       ": Can't get bus-range for %s\n", pcictrl->full_name);
		return;
	}

	if (bus_range[1] == bus_range[0])
		printk(KERN_INFO EFIKA_PLATFORM_NAME ": PCI bus %d",
		       bus_range[0]);
	else
		printk(KERN_INFO EFIKA_PLATFORM_NAME ": PCI buses %d..%d",
		       bus_range[0], bus_range[1]);
	printk(" controlled by %s\n", pcictrl->full_name);
	printk("\n");

	hose = pcibios_alloc_controller();
	if (!hose) {
		printk(KERN_WARNING EFIKA_PLATFORM_NAME
		       ": Can't allocate PCI controller structure for %s\n",
		       pcictrl->full_name);
		return;
	}

	hose->arch_data = of_node_get(pcictrl);
	hose->first_busno = bus_range[0];
	hose->last_busno = bus_range[1];
	hose->ops = &rtas_pci_ops;

	pci_process_bridge_OF_ranges(hose, pcictrl, 0);
}

#else
void __init efika_pcisetup(void)
{}
#endif
