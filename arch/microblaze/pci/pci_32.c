/*
 * Common pmac/prep/chrp pci routines. -- Cort
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/bootmem.h>
#include <linux/irq.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/slab.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/sections.h>
#include <asm/pci-bridge.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>

#undef DEBUG

unsigned long isa_io_base;
unsigned long pci_dram_offset;
int pcibios_assign_bus_offset = 1;

static u8 *pci_to_OF_bus_map;

/* By default, we don't re-assign bus numbers. We do this only on
 * some pmacs
 */
static int pci_assign_all_buses;

static int pci_bus_count;

/*
 * Functions below are used on OpenFirmware machines.
 */
static void
make_one_node_map(struct device_node *node, u8 pci_bus)
{
	const int *bus_range;
	int len;

	if (pci_bus >= pci_bus_count)
		return;
	bus_range = of_get_property(node, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		printk(KERN_WARNING "Can't get bus-range for %s, "
		       "assuming it starts at 0\n", node->full_name);
		pci_to_OF_bus_map[pci_bus] = 0;
	} else
		pci_to_OF_bus_map[pci_bus] = bus_range[0];

	for_each_child_of_node(node, node) {
		struct pci_dev *dev;
		const unsigned int *class_code, *reg;

		class_code = of_get_property(node, "class-code", NULL);
		if (!class_code ||
			((*class_code >> 8) != PCI_CLASS_BRIDGE_PCI &&
			(*class_code >> 8) != PCI_CLASS_BRIDGE_CARDBUS))
			continue;
		reg = of_get_property(node, "reg", NULL);
		if (!reg)
			continue;
		dev = pci_get_bus_and_slot(pci_bus, ((reg[0] >> 8) & 0xff));
		if (!dev || !dev->subordinate) {
			pci_dev_put(dev);
			continue;
		}
		make_one_node_map(node, dev->subordinate->number);
		pci_dev_put(dev);
	}
}

void
pcibios_make_OF_bus_map(void)
{
	int i;
	struct pci_controller *hose, *tmp;
	struct property *map_prop;
	struct device_node *dn;

	pci_to_OF_bus_map = kmalloc(pci_bus_count, GFP_KERNEL);
	if (!pci_to_OF_bus_map) {
		printk(KERN_ERR "Can't allocate OF bus map !\n");
		return;
	}

	/* We fill the bus map with invalid values, that helps
	 * debugging.
	 */
	for (i = 0; i < pci_bus_count; i++)
		pci_to_OF_bus_map[i] = 0xff;

	/* For each hose, we begin searching bridges */
	list_for_each_entry_safe(hose, tmp, &hose_list, list_node) {
		struct device_node *node = hose->dn;

		if (!node)
			continue;
		make_one_node_map(node, hose->first_busno);
	}
	dn = of_find_node_by_path("/");
	map_prop = of_find_property(dn, "pci-OF-bus-map", NULL);
	if (map_prop) {
		BUG_ON(pci_bus_count > map_prop->length);
		memcpy(map_prop->value, pci_to_OF_bus_map, pci_bus_count);
	}
	of_node_put(dn);
#ifdef DEBUG
	printk(KERN_INFO "PCI->OF bus map:\n");
	for (i = 0; i < pci_bus_count; i++) {
		if (pci_to_OF_bus_map[i] == 0xff)
			continue;
		printk(KERN_INFO "%d -> %d\n", i, pci_to_OF_bus_map[i]);
	}
#endif
}

typedef int (*pci_OF_scan_iterator)(struct device_node *node, void *data);

static struct device_node *scan_OF_pci_childs(struct device_node *parent,
					pci_OF_scan_iterator filter, void *data)
{
	struct device_node *node;
	struct device_node *sub_node;

	for_each_child_of_node(parent, node) {
		const unsigned int *class_code;

		if (filter(node, data)) {
			of_node_put(node);
			return node;
		}

		/* For PCI<->PCI bridges or CardBus bridges, we go down
		 * Note: some OFs create a parent node "multifunc-device" as
		 * a fake root for all functions of a multi-function device,
		 * we go down them as well.
		 */
		class_code = of_get_property(node, "class-code", NULL);
		if ((!class_code ||
			((*class_code >> 8) != PCI_CLASS_BRIDGE_PCI &&
			(*class_code >> 8) != PCI_CLASS_BRIDGE_CARDBUS)) &&
			strcmp(node->name, "multifunc-device"))
			continue;
		sub_node = scan_OF_pci_childs(node, filter, data);
		if (sub_node) {
			of_node_put(node);
			return sub_node;
		}
	}
	return NULL;
}

static struct device_node *scan_OF_for_pci_dev(struct device_node *parent,
					       unsigned int devfn)
{
	struct device_node *np, *cnp;
	const u32 *reg;
	unsigned int psize;

	for_each_child_of_node(parent, np) {
		reg = of_get_property(np, "reg", &psize);
		if (reg && psize >= 4 && ((reg[0] >> 8) & 0xff) == devfn)
			return np;

		/* Note: some OFs create a parent node "multifunc-device" as
		 * a fake root for all functions of a multi-function device,
		 * we go down them as well. */
		if (!strcmp(np->name, "multifunc-device")) {
			cnp = scan_OF_for_pci_dev(np, devfn);
			if (cnp)
				return cnp;
		}
	}
	return NULL;
}


static struct device_node *scan_OF_for_pci_bus(struct pci_bus *bus)
{
	struct device_node *parent, *np;

	/* Are we a root bus ? */
	if (bus->self == NULL || bus->parent == NULL) {
		struct pci_controller *hose = pci_bus_to_host(bus);
		if (hose == NULL)
			return NULL;
		return of_node_get(hose->dn);
	}

	/* not a root bus, we need to get our parent */
	parent = scan_OF_for_pci_bus(bus->parent);
	if (parent == NULL)
		return NULL;

	/* now iterate for children for a match */
	np = scan_OF_for_pci_dev(parent, bus->self->devfn);
	of_node_put(parent);

	return np;
}

/*
 * Scans the OF tree for a device node matching a PCI device
 */
struct device_node *
pci_busdev_to_OF_node(struct pci_bus *bus, int devfn)
{
	struct device_node *parent, *np;

	pr_debug("pci_busdev_to_OF_node(%d,0x%x)\n", bus->number, devfn);
	parent = scan_OF_for_pci_bus(bus);
	if (parent == NULL)
		return NULL;
	pr_debug(" parent is %s\n", parent ? parent->full_name : "<NULL>");
	np = scan_OF_for_pci_dev(parent, devfn);
	of_node_put(parent);
	pr_debug(" result is %s\n", np ? np->full_name : "<NULL>");

	/* XXX most callers don't release the returned node
	 * mostly because ppc64 doesn't increase the refcount,
	 * we need to fix that.
	 */
	return np;
}
EXPORT_SYMBOL(pci_busdev_to_OF_node);

struct device_node*
pci_device_to_OF_node(struct pci_dev *dev)
{
	return pci_busdev_to_OF_node(dev->bus, dev->devfn);
}
EXPORT_SYMBOL(pci_device_to_OF_node);

static int
find_OF_pci_device_filter(struct device_node *node, void *data)
{
	return ((void *)node == data);
}

/*
 * Returns the PCI device matching a given OF node
 */
int
pci_device_from_OF_node(struct device_node *node, u8 *bus, u8 *devfn)
{
	const unsigned int *reg;
	struct pci_controller *hose;
	struct pci_dev *dev = NULL;

	/* Make sure it's really a PCI device */
	hose = pci_find_hose_for_OF_device(node);
	if (!hose || !hose->dn)
		return -ENODEV;
	if (!scan_OF_pci_childs(hose->dn,
			find_OF_pci_device_filter, (void *)node))
		return -ENODEV;
	reg = of_get_property(node, "reg", NULL);
	if (!reg)
		return -ENODEV;
	*bus = (reg[0] >> 16) & 0xff;
	*devfn = ((reg[0] >> 8) & 0xff);

	/* Ok, here we need some tweak. If we have already renumbered
	 * all busses, we can't rely on the OF bus number any more.
	 * the pci_to_OF_bus_map is not enough as several PCI busses
	 * may match the same OF bus number.
	 */
	if (!pci_to_OF_bus_map)
		return 0;

	for_each_pci_dev(dev)
		if (pci_to_OF_bus_map[dev->bus->number] == *bus &&
				dev->devfn == *devfn) {
			*bus = dev->bus->number;
			pci_dev_put(dev);
			return 0;
		}

	return -ENODEV;
}
EXPORT_SYMBOL(pci_device_from_OF_node);

/* We create the "pci-OF-bus-map" property now so it appears in the
 * /proc device tree
 */
void __init
pci_create_OF_bus_map(void)
{
	struct property *of_prop;
	struct device_node *dn;

	of_prop = (struct property *) alloc_bootmem(sizeof(struct property) + \
									 256);
	if (!of_prop)
		return;
	dn = of_find_node_by_path("/");
	if (dn) {
		memset(of_prop, -1, sizeof(struct property) + 256);
		of_prop->name = "pci-OF-bus-map";
		of_prop->length = 256;
		of_prop->value = &of_prop[1];
		prom_add_property(dn, of_prop);
		of_node_put(dn);
	}
}

static void __devinit pcibios_scan_phb(struct pci_controller *hose)
{
	struct pci_bus *bus;
	struct device_node *node = hose->dn;
	unsigned long io_offset;
	struct resource *res = &hose->io_resource;

	pr_debug("PCI: Scanning PHB %s\n",
		 node ? node->full_name : "<NO NAME>");

	/* Create an empty bus for the toplevel */
	bus = pci_create_bus(hose->parent, hose->first_busno, hose->ops, hose);
	if (bus == NULL) {
		printk(KERN_ERR "Failed to create bus for PCI domain %04x\n",
		       hose->global_number);
		return;
	}
	bus->secondary = hose->first_busno;
	hose->bus = bus;

	/* Fixup IO space offset */
	io_offset = (unsigned long)hose->io_base_virt - isa_io_base;
	res->start = (res->start + io_offset) & 0xffffffffu;
	res->end = (res->end + io_offset) & 0xffffffffu;

	/* Wire up PHB bus resources */
	pcibios_setup_phb_resources(hose);

	/* Scan children */
	hose->last_busno = bus->subordinate = pci_scan_child_bus(bus);
}

static int __init pcibios_init(void)
{
	struct pci_controller *hose, *tmp;
	int next_busno = 0;

	printk(KERN_INFO "PCI: Probing PCI hardware\n");

	if (pci_flags & PCI_REASSIGN_ALL_BUS) {
		printk(KERN_INFO "setting pci_asign_all_busses\n");
		pci_assign_all_buses = 1;
	}

	/* Scan all of the recorded PCI controllers.  */
	list_for_each_entry_safe(hose, tmp, &hose_list, list_node) {
		if (pci_assign_all_buses)
			hose->first_busno = next_busno;
		hose->last_busno = 0xff;
		pcibios_scan_phb(hose);
		printk(KERN_INFO "calling pci_bus_add_devices()\n");
		pci_bus_add_devices(hose->bus);
		if (pci_assign_all_buses || next_busno <= hose->last_busno)
			next_busno = hose->last_busno + \
					pcibios_assign_bus_offset;
	}
	pci_bus_count = next_busno;

	/* OpenFirmware based machines need a map of OF bus
	 * numbers vs. kernel bus numbers since we may have to
	 * remap them.
	 */
	if (pci_assign_all_buses)
		pcibios_make_OF_bus_map();

	/* Call common code to handle resource allocation */
	pcibios_resource_survey();

	return 0;
}

subsys_initcall(pcibios_init);

static struct pci_controller*
pci_bus_to_hose(int bus)
{
	struct pci_controller *hose, *tmp;

	list_for_each_entry_safe(hose, tmp, &hose_list, list_node)
		if (bus >= hose->first_busno && bus <= hose->last_busno)
			return hose;
	return NULL;
}

/* Provide information on locations of various I/O regions in physical
 * memory.  Do this on a per-card basis so that we choose the right
 * root bridge.
 * Note that the returned IO or memory base is a physical address
 */

long sys_pciconfig_iobase(long which, unsigned long bus, unsigned long devfn)
{
	struct pci_controller *hose;
	long result = -EOPNOTSUPP;

	hose = pci_bus_to_hose(bus);
	if (!hose)
		return -ENODEV;

	switch (which) {
	case IOBASE_BRIDGE_NUMBER:
		return (long)hose->first_busno;
	case IOBASE_MEMORY:
		return (long)hose->pci_mem_offset;
	case IOBASE_IO:
		return (long)hose->io_base_phys;
	case IOBASE_ISA_IO:
		return (long)isa_io_base;
	case IOBASE_ISA_MEM:
		return (long)isa_mem_base;
	}

	return result;
}
