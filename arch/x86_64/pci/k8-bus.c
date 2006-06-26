#include <linux/init.h>
#include <linux/pci.h>
#include <asm/mpspec.h>
#include <linux/cpumask.h>
#include <asm/k8.h>

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

/**
 * fill_mp_bus_to_cpumask()
 * fills the mp_bus_to_cpumask array based according to the LDT Bus Number
 * Registers found in the K8 northbridge
 */
__init static int
fill_mp_bus_to_cpumask(void)
{
	int i, j, k;
	u32 ldtbus, nid;
	static int lbnr[3] = {
		LDT_BUS_NUMBER_REGISTER_0,
		LDT_BUS_NUMBER_REGISTER_1,
		LDT_BUS_NUMBER_REGISTER_2
	};

	cache_k8_northbridges();
	for (k = 0; k < num_k8_northbridges; k++) {
		struct pci_dev *nb_dev = k8_northbridges[k];
		pci_read_config_dword(nb_dev, NODE_ID_REGISTER, &nid);

		for (i = 0; i < NR_LDT_BUS_NUMBER_REGISTERS; i++) {
			pci_read_config_dword(nb_dev, lbnr[i], &ldtbus);
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
					struct pci_bus *bus;
					long node = NODE_ID(nid);
					/* Algorithm a bit dumb, but
 					   it shouldn't matter here */
					bus = pci_find_bus(0, j);
					if (!bus)
						continue;
					if (!node_online(node))
						node = 0;
					bus->sysdata = (void *)node;
				}		
			}
		}
	}

	return 0;
}

fs_initcall(fill_mp_bus_to_cpumask);
