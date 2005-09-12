/*
 * mmconfig.c - Low-level direct PCI config space access via MMCONFIG
 * 
 * This is an 64bit optimized version that always keeps the full mmconfig
 * space mapped. This allows lockless config space operation.
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include "pci.h"

#define MMCONFIG_APER_SIZE (256*1024*1024)

/* Static virtual mapping of the MMCONFIG aperture */
struct mmcfg_virt {
	struct acpi_table_mcfg_config *cfg;
	char *virt;
};
static struct mmcfg_virt *pci_mmcfg_virt;

static char *get_virt(unsigned int seg, int bus)
{
	int cfg_num = -1;
	struct acpi_table_mcfg_config *cfg;

	while (1) {
		++cfg_num;
		if (cfg_num >= pci_mmcfg_config_num) {
			/* something bad is going on, no cfg table is found. */
			/* so we fall back to the old way we used to do this */
			/* and just rely on the first entry to be correct. */
			return pci_mmcfg_virt[0].virt;
		}
		cfg = pci_mmcfg_virt[cfg_num].cfg;
		if (cfg->pci_segment_group_number != seg)
			continue;
		if ((cfg->start_bus_number <= bus) &&
		    (cfg->end_bus_number >= bus))
			return pci_mmcfg_virt[cfg_num].virt;
	}
}

static inline char *pci_dev_base(unsigned int seg, unsigned int bus, unsigned int devfn)
{

	return get_virt(seg, bus) + ((bus << 20) | (devfn << 12));
}

static int pci_mmcfg_read(unsigned int seg, unsigned int bus,
			  unsigned int devfn, int reg, int len, u32 *value)
{
	char *addr = pci_dev_base(seg, bus, devfn);

	if (unlikely(!value || (bus > 255) || (devfn > 255) || (reg > 4095)))
		return -EINVAL;

	switch (len) {
	case 1:
		*value = readb(addr + reg);
		break;
	case 2:
		*value = readw(addr + reg);
		break;
	case 4:
		*value = readl(addr + reg);
		break;
	}

	return 0;
}

static int pci_mmcfg_write(unsigned int seg, unsigned int bus,
			   unsigned int devfn, int reg, int len, u32 value)
{
	char *addr = pci_dev_base(seg, bus, devfn);

	if (unlikely((bus > 255) || (devfn > 255) || (reg > 4095)))
		return -EINVAL;

	switch (len) {
	case 1:
		writeb(value, addr + reg);
		break;
	case 2:
		writew(value, addr + reg);
		break;
	case 4:
		writel(value, addr + reg);
		break;
	}

	return 0;
}

static struct pci_raw_ops pci_mmcfg = {
	.read =		pci_mmcfg_read,
	.write =	pci_mmcfg_write,
};

static int __init pci_mmcfg_init(void)
{
	int i;

	if ((pci_probe & PCI_PROBE_MMCONF) == 0)
		return 0;

	acpi_table_parse(ACPI_MCFG, acpi_parse_mcfg);
	if ((pci_mmcfg_config_num == 0) ||
	    (pci_mmcfg_config == NULL) ||
	    (pci_mmcfg_config[0].base_address == 0))
		return 0;

	/* RED-PEN i386 doesn't do _nocache right now */
	pci_mmcfg_virt = kmalloc(sizeof(*pci_mmcfg_virt) * pci_mmcfg_config_num, GFP_KERNEL);
	if (pci_mmcfg_virt == NULL) {
		printk("PCI: Can not allocate memory for mmconfig structures\n");
		return 0;
	}
	for (i = 0; i < pci_mmcfg_config_num; ++i) {
		pci_mmcfg_virt[i].cfg = &pci_mmcfg_config[i];
		pci_mmcfg_virt[i].virt = ioremap_nocache(pci_mmcfg_config[i].base_address, MMCONFIG_APER_SIZE);
		if (!pci_mmcfg_virt[i].virt) {
			printk("PCI: Cannot map mmconfig aperture for segment %d\n",
			       pci_mmcfg_config[i].pci_segment_group_number);
			return 0;
		}
		printk(KERN_INFO "PCI: Using MMCONFIG at %x\n", pci_mmcfg_config[i].base_address);
	}

	raw_pci_ops = &pci_mmcfg;
	pci_probe = (pci_probe & ~PCI_PROBE_MASK) | PCI_PROBE_MMCONF;

	return 0;
}

arch_initcall(pci_mmcfg_init);
