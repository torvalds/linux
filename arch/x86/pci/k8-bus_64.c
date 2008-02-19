#include <linux/init.h>
#include <linux/pci.h>
#include <asm/pci-direct.h>
#include <asm/mpspec.h>
#include <linux/cpumask.h>
#include <linux/topology.h>

/*
 * This discovers the pcibus <-> node mapping on AMD K8.
 *
 * RED-PEN need to call this again on PCI hotplug
 * RED-PEN empty cpus get reported wrong
 */

#define NODE_ID(dword) ((dword>>4) & 0x07)
#define LDT_BUS_NUMBER_REGISTER_0 0xE0
#define LDT_BUS_NUMBER_REGISTER_1 0xE4
#define LDT_BUS_NUMBER_REGISTER_2 0xE8
#define LDT_BUS_NUMBER_REGISTER_3 0xEC
#define NR_LDT_BUS_NUMBER_REGISTERS 4
#define SECONDARY_LDT_BUS_NUMBER(dword) ((dword >> 16) & 0xFF)
#define SUBORDINATE_LDT_BUS_NUMBER(dword) ((dword >> 24) & 0xFF)

#define PCI_DEVICE_ID_K8HTCONFIG 0x1100
#define PCI_DEVICE_ID_K8_10H_HTCONFIG 0x1200
#define PCI_DEVICE_ID_K8_11H_HTCONFIG 0x1300

#ifdef CONFIG_NUMA

#define BUS_NR 256

static int mp_bus_to_node[BUS_NR];

void set_mp_bus_to_node(int busnum, int node)
{
	if (busnum >= 0 &&  busnum < BUS_NR)
		mp_bus_to_node[busnum] = node;
}

int get_mp_bus_to_node(int busnum)
{
	int node = -1;

	if (busnum < 0 || busnum > (BUS_NR - 1))
		return node;

	node = mp_bus_to_node[busnum];

	/*
	 * let numa_node_id to decide it later in dma_alloc_pages
	 * if there is no ram on that node
	 */
	if (node != -1 && !node_online(node))
		node = -1;

	return node;
}

#endif

/**
 * early_fill_mp_bus_to_node()
 * called before pcibios_scan_root and pci_scan_bus
 * fills the mp_bus_to_cpumask array based according to the LDT Bus Number
 * Registers found in the K8 northbridge
 */
__init static int
early_fill_mp_bus_to_node(void)
{
#ifdef CONFIG_NUMA
	int i, j;
	unsigned slot;
	u32 ldtbus;
	u32 id;
	int node;
	u16 deviceid;
	u16 vendorid;
	int min_bus;
	int max_bus;

	static int lbnr[NR_LDT_BUS_NUMBER_REGISTERS] = {
		LDT_BUS_NUMBER_REGISTER_0,
		LDT_BUS_NUMBER_REGISTER_1,
		LDT_BUS_NUMBER_REGISTER_2,
		LDT_BUS_NUMBER_REGISTER_3
	};

	for (i = 0; i < BUS_NR; i++)
		mp_bus_to_node[i] = -1;

	if (!early_pci_allowed())
		return -1;

	slot = 0x18;
	id = read_pci_config(0, slot, 0, PCI_VENDOR_ID);

	vendorid = id & 0xffff;
	if (vendorid != PCI_VENDOR_ID_AMD)
		goto out;

	deviceid = (id>>16) & 0xffff;
	if ((deviceid != PCI_DEVICE_ID_K8HTCONFIG) &&
	    (deviceid != PCI_DEVICE_ID_K8_10H_HTCONFIG) &&
	    (deviceid != PCI_DEVICE_ID_K8_11H_HTCONFIG))
		goto out;

	for (i = 0; i < NR_LDT_BUS_NUMBER_REGISTERS; i++) {
		ldtbus = read_pci_config(0, slot, 1, lbnr[i]);

		/* Check if that register is enabled for bus range */
		if ((ldtbus & 7) != 3)
			continue;

		min_bus = SECONDARY_LDT_BUS_NUMBER(ldtbus);
		max_bus = SUBORDINATE_LDT_BUS_NUMBER(ldtbus);
		node = NODE_ID(ldtbus);
		for (j = min_bus; j <= max_bus; j++)
			mp_bus_to_node[j] = (unsigned char) node;
	}

out:
	for (i = 0; i < BUS_NR; i++) {
		node = mp_bus_to_node[i];
		if (node >= 0)
			printk(KERN_DEBUG "bus: %02x to node: %02x\n", i, node);
	}
#endif
	return 0;
}

postcore_initcall(early_fill_mp_bus_to_node);
