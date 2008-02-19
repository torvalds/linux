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

#define NODE_ID_REGISTER 0x60
#define NODE_ID(dword) (dword & 0x07)
#define LDT_BUS_NUMBER_REGISTER_0 0x94
#define LDT_BUS_NUMBER_REGISTER_1 0xB4
#define LDT_BUS_NUMBER_REGISTER_2 0xD4
#define NR_LDT_BUS_NUMBER_REGISTERS 3
#define SECONDARY_LDT_BUS_NUMBER(dword) ((dword >> 8) & 0xFF)
#define SUBORDINATE_LDT_BUS_NUMBER(dword) ((dword >> 16) & 0xFF)
#define PCI_DEVICE_ID_K8HTCONFIG 0x1100

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
	u32 ldtbus, nid;
	u32 id;
	static int lbnr[3] = {
		LDT_BUS_NUMBER_REGISTER_0,
		LDT_BUS_NUMBER_REGISTER_1,
		LDT_BUS_NUMBER_REGISTER_2
	};

	for (i = 0; i < BUS_NR; i++)
		mp_bus_to_node[i] = -1;

	if (!early_pci_allowed())
		return -1;

	for (slot = 0x18; slot < 0x20; slot++) {
		id = read_pci_config(0, slot, 0, PCI_VENDOR_ID);
		if (id != (PCI_VENDOR_ID_AMD | (PCI_DEVICE_ID_K8HTCONFIG<<16)))
			break;
		nid = read_pci_config(0, slot, 0, NODE_ID_REGISTER);

		for (i = 0; i < NR_LDT_BUS_NUMBER_REGISTERS; i++) {
			ldtbus = read_pci_config(0, slot, 0, lbnr[i]);
			/*
			 * if there are no busses hanging off of the current
			 * ldt link then both the secondary and subordinate
			 * bus number fields are set to 0.
			 *
			 * RED-PEN
			 * This is slightly broken because it assumes
			 * HT node IDs == Linux node ids, which is not always
			 * true. However it is probably mostly true.
			 */
			if (!(SECONDARY_LDT_BUS_NUMBER(ldtbus) == 0
				&& SUBORDINATE_LDT_BUS_NUMBER(ldtbus) == 0)) {
				for (j = SECONDARY_LDT_BUS_NUMBER(ldtbus);
				     j <= SUBORDINATE_LDT_BUS_NUMBER(ldtbus);
				     j++) {
					int node = NODE_ID(nid);
					mp_bus_to_node[j] = (unsigned char)node;
				}
			}
		}
	}

	for (i = 0; i < BUS_NR; i++) {
		int node = mp_bus_to_node[i];
		if (node >= 0)
			printk(KERN_DEBUG "bus: %02x to node: %02x\n", i, node);
	}
#endif
	return 0;
}

postcore_initcall(early_fill_mp_bus_to_node);
