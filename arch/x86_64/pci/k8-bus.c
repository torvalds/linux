#include <linux/init.h>
#include <linux/pci.h>
#include <asm/mpspec.h>
#include <linux/cpumask.h>

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

/**
 * fill_mp_bus_to_cpumask()
 * fills the mp_bus_to_cpumask array based according to the LDT Bus Number
 * Registers found in the K8 northbridge
 */
__init static int
fill_mp_bus_to_cpumask(void)
{
	struct pci_dev *nb_dev = NULL;
	int i, j, printed;
	u32 ldtbus, nid;
	static int lbnr[3] = {
		LDT_BUS_NUMBER_REGISTER_0,
		LDT_BUS_NUMBER_REGISTER_1,
		LDT_BUS_NUMBER_REGISTER_2
	};

	while ((nb_dev = pci_get_device(PCI_VENDOR_ID_AMD,
			PCI_DEVICE_ID_K8HTCONFIG, nb_dev))) {
		pci_read_config_dword(nb_dev, NODE_ID_REGISTER, &nid);

		for (i = 0; i < NR_LDT_BUS_NUMBER_REGISTERS; i++) {
			pci_read_config_dword(nb_dev, lbnr[i], &ldtbus);
			/*
			 * if there are no busses hanging off of the current
			 * ldt link then both the secondary and subordinate
			 * bus number fields are set to 0.
			 */
			if (!(SECONDARY_LDT_BUS_NUMBER(ldtbus) == 0
				&& SUBORDINATE_LDT_BUS_NUMBER(ldtbus) == 0)) {
				for (j = SECONDARY_LDT_BUS_NUMBER(ldtbus);
				     j <= SUBORDINATE_LDT_BUS_NUMBER(ldtbus);
				     j++)
					pci_bus_to_cpumask[j] =
						node_to_cpumask(NODE_ID(nid));
			}
		}
	}

	/* quick sanity check */
	printed = 0;
	for (i = 0; i < 256; i++) {
		if (cpus_empty(pci_bus_to_cpumask[i])) {
			pci_bus_to_cpumask[i] = CPU_MASK_ALL;
			if (printed)
				continue;
			printk(KERN_ERR
			       "k8-bus.c: some busses have empty cpu mask\n");
			printed = 1;
		}
	}

	return 0;
}

fs_initcall(fill_mp_bus_to_cpumask);
