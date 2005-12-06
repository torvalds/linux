/*
 * CHRP pci routines.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/hydra.h>
#include <asm/prom.h>
#include <asm/gg2.h>
#include <asm/machdep.h>
#include <asm/sections.h>
#include <asm/pci-bridge.h>
#include <asm/open_pic.h>

/* LongTrail */
void __iomem *gg2_pci_config_base;

/*
 * The VLSI Golden Gate II has only 512K of PCI configuration space, so we
 * limit the bus number to 3 bits
 */

int gg2_read_config(struct pci_bus *bus, unsigned int devfn, int off,
			   int len, u32 *val)
{
	volatile void __iomem *cfg_data;
	struct pci_controller *hose = bus->sysdata;

	if (bus->number > 7)
		return PCIBIOS_DEVICE_NOT_FOUND;
	/*
	 * Note: the caller has already checked that off is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	cfg_data = hose->cfg_data + ((bus->number<<16) | (devfn<<8) | off);
	switch (len) {
	case 1:
		*val =  in_8(cfg_data);
		break;
	case 2:
		*val = in_le16(cfg_data);
		break;
	default:
		*val = in_le32(cfg_data);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

int gg2_write_config(struct pci_bus *bus, unsigned int devfn, int off,
			    int len, u32 val)
{
	volatile void __iomem *cfg_data;
	struct pci_controller *hose = bus->sysdata;

	if (bus->number > 7)
		return PCIBIOS_DEVICE_NOT_FOUND;
	/*
	 * Note: the caller has already checked that off is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	cfg_data = hose->cfg_data + ((bus->number<<16) | (devfn<<8) | off);
	switch (len) {
	case 1:
		out_8(cfg_data, val);
		break;
	case 2:
		out_le16(cfg_data, val);
		break;
	default:
		out_le32(cfg_data, val);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops gg2_pci_ops =
{
	gg2_read_config,
	gg2_write_config
};

/*
 * Access functions for PCI config space using RTAS calls.
 */
int
rtas_read_config(struct pci_bus *bus, unsigned int devfn, int offset,
		 int len, u32 *val)
{
	struct pci_controller *hose = bus->sysdata;
	unsigned long addr = (offset & 0xff) | ((devfn & 0xff) << 8)
		| (((bus->number - hose->first_busno) & 0xff) << 16)
		| (hose->index << 24);
        unsigned long ret = ~0UL;
	int rval;

	rval = call_rtas("read-pci-config", 2, 2, &ret, addr, len);
	*val = ret;
	return rval? PCIBIOS_DEVICE_NOT_FOUND: PCIBIOS_SUCCESSFUL;
}

int
rtas_write_config(struct pci_bus *bus, unsigned int devfn, int offset,
		  int len, u32 val)
{
	struct pci_controller *hose = bus->sysdata;
	unsigned long addr = (offset & 0xff) | ((devfn & 0xff) << 8)
		| (((bus->number - hose->first_busno) & 0xff) << 16)
		| (hose->index << 24);
	int rval;

	rval = call_rtas("write-pci-config", 3, 1, NULL, addr, len, val);
	return rval? PCIBIOS_DEVICE_NOT_FOUND: PCIBIOS_SUCCESSFUL;
}

static struct pci_ops rtas_pci_ops =
{
	rtas_read_config,
	rtas_write_config
};

volatile struct Hydra __iomem *Hydra = NULL;

int __init
hydra_init(void)
{
	struct device_node *np;

	np = find_devices("mac-io");
	if (np == NULL || np->n_addrs == 0)
		return 0;
	Hydra = ioremap(np->addrs[0].address, np->addrs[0].size);
	printk("Hydra Mac I/O at %x\n", np->addrs[0].address);
	printk("Hydra Feature_Control was %x",
	       in_le32(&Hydra->Feature_Control));
	out_le32(&Hydra->Feature_Control, (HYDRA_FC_SCC_CELL_EN |
					   HYDRA_FC_SCSI_CELL_EN |
					   HYDRA_FC_SCCA_ENABLE |
					   HYDRA_FC_SCCB_ENABLE |
					   HYDRA_FC_ARB_BYPASS |
					   HYDRA_FC_MPIC_ENABLE |
					   HYDRA_FC_SLOW_SCC_PCLK |
					   HYDRA_FC_MPIC_IS_MASTER));
	printk(", now %x\n", in_le32(&Hydra->Feature_Control));
	return 1;
}

void __init
chrp_pcibios_fixup(void)
{
	struct pci_dev *dev = NULL;
	struct device_node *np;

	/* PCI interrupts are controlled by the OpenPIC */
	for_each_pci_dev(dev) {
		np = pci_device_to_OF_node(dev);
		if ((np != 0) && (np->n_intrs > 0) && (np->intrs[0].line != 0))
			dev->irq = np->intrs[0].line;
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
	}
}

#define PRG_CL_RESET_VALID 0x00010000

static void __init
setup_python(struct pci_controller *hose, struct device_node *dev)
{
	u32 __iomem *reg;
	u32 val;
	unsigned long addr = dev->addrs[0].address;

	setup_indirect_pci(hose, addr + 0xf8000, addr + 0xf8010);

	/* Clear the magic go-slow bit */
	reg = ioremap(dev->addrs[0].address + 0xf6000, 0x40);
	val = in_be32(&reg[12]);
	if (val & PRG_CL_RESET_VALID) {
		out_be32(&reg[12], val & ~PRG_CL_RESET_VALID);
		in_be32(&reg[12]);
	}
	iounmap(reg);
}

/* Marvell Discovery II based Pegasos 2 */
static void __init setup_peg2(struct pci_controller *hose, struct device_node *dev)
{
	struct device_node *root = find_path_device("/");
	struct device_node *rtas;

	rtas = of_find_node_by_name (root, "rtas");
	if (rtas) {
		hose->ops = &rtas_pci_ops;
	} else {
		printk ("RTAS supporting Pegasos OF not found, please upgrade"
			" your firmware\n");
	}
	pci_assign_all_buses = 1;
}

void __init
chrp_find_bridges(void)
{
	struct device_node *dev;
	int *bus_range;
	int len, index = -1;
	struct pci_controller *hose;
	unsigned int *dma;
	char *model, *machine;
	int is_longtrail = 0, is_mot = 0, is_pegasos = 0;
	struct device_node *root = find_path_device("/");

	/*
	 * The PCI host bridge nodes on some machines don't have
	 * properties to adequately identify them, so we have to
	 * look at what sort of machine this is as well.
	 */
	machine = get_property(root, "model", NULL);
	if (machine != NULL) {
		is_longtrail = strncmp(machine, "IBM,LongTrail", 13) == 0;
		is_mot = strncmp(machine, "MOT", 3) == 0;
		if (strncmp(machine, "Pegasos2", 8) == 0)
			is_pegasos = 2;
		else if (strncmp(machine, "Pegasos", 7) == 0)
			is_pegasos = 1;
	}
	for (dev = root->child; dev != NULL; dev = dev->sibling) {
		if (dev->type == NULL || strcmp(dev->type, "pci") != 0)
			continue;
		++index;
		/* The GG2 bridge on the LongTrail doesn't have an address */
		if (dev->n_addrs < 1 && !is_longtrail) {
			printk(KERN_WARNING "Can't use %s: no address\n",
			       dev->full_name);
			continue;
		}
		bus_range = (int *) get_property(dev, "bus-range", &len);
		if (bus_range == NULL || len < 2 * sizeof(int)) {
			printk(KERN_WARNING "Can't get bus-range for %s\n",
				dev->full_name);
			continue;
		}
		if (bus_range[1] == bus_range[0])
			printk(KERN_INFO "PCI bus %d", bus_range[0]);
		else
			printk(KERN_INFO "PCI buses %d..%d",
			       bus_range[0], bus_range[1]);
		printk(" controlled by %s", dev->type);
		if (dev->n_addrs > 0)
			printk(" at %x", dev->addrs[0].address);
		printk("\n");

		hose = pcibios_alloc_controller();
		if (!hose) {
			printk("Can't allocate PCI controller structure for %s\n",
				dev->full_name);
			continue;
		}
		hose->arch_data = dev;
		hose->first_busno = bus_range[0];
		hose->last_busno = bus_range[1];

		model = get_property(dev, "model", NULL);
		if (model == NULL)
			model = "<none>";
		if (device_is_compatible(dev, "IBM,python")) {
			setup_python(hose, dev);
		} else if (is_mot
			   || strncmp(model, "Motorola, Grackle", 17) == 0) {
			setup_grackle(hose);
		} else if (is_longtrail) {
			void __iomem *p = ioremap(GG2_PCI_CONFIG_BASE, 0x80000);
			hose->ops = &gg2_pci_ops;
			hose->cfg_data = p;
			gg2_pci_config_base = p;
		} else if (is_pegasos == 1) {
			setup_indirect_pci(hose, 0xfec00cf8, 0xfee00cfc);
		} else if (is_pegasos == 2) {
			setup_peg2(hose, dev);
		} else {
			printk("No methods for %s (model %s), using RTAS\n",
			       dev->full_name, model);
			hose->ops = &rtas_pci_ops;
		}

		pci_process_bridge_OF_ranges(hose, dev, index == 0);

		/* check the first bridge for a property that we can
		   use to set pci_dram_offset */
		dma = (unsigned int *)
			get_property(dev, "ibm,dma-ranges", &len);
		if (index == 0 && dma != NULL && len >= 6 * sizeof(*dma)) {
			pci_dram_offset = dma[2] - dma[3];
			printk("pci_dram_offset = %lx\n", pci_dram_offset);
		}
	}

	/* Do not fixup interrupts from OF tree on pegasos */
	if (is_pegasos == 0)
		ppc_md.pcibios_fixup = chrp_pcibios_fixup;
}
