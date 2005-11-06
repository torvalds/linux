/*
 * Copyright (C) 2004 Matthew Wilcox <matthew@wil.cx>
 * Copyright (C) 2004 Intel Corp.
 *
 * This code is released under the GNU General Public License version 2.
 */

/*
 * mmconfig.c - Low-level direct PCI config space access via MMCONFIG
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include "pci.h"

#define mmcfg_virt_addr ((void __iomem *) fix_to_virt(FIX_PCIE_MCFG))

/* The base address of the last MMCONFIG device accessed */
static u32 mmcfg_last_accessed_device;

/*
 * Functions for accessing PCI configuration space with MMCONFIG accesses
 */
static u32 get_base_addr(unsigned int seg, int bus)
{
	int cfg_num = -1;
	struct acpi_table_mcfg_config *cfg;

	while (1) {
		++cfg_num;
		if (cfg_num >= pci_mmcfg_config_num) {
			/* something bad is going on, no cfg table is found. */
			/* so we fall back to the old way we used to do this */
			/* and just rely on the first entry to be correct. */
			return pci_mmcfg_config[0].base_address;
		}
		cfg = &pci_mmcfg_config[cfg_num];
		if (cfg->pci_segment_group_number != seg)
			continue;
		if ((cfg->start_bus_number <= bus) &&
		    (cfg->end_bus_number >= bus))
			return cfg->base_address;
	}
}

static inline void pci_exp_set_dev_base(unsigned int seg, int bus, int devfn)
{
	u32 dev_base = get_base_addr(seg, bus) | (bus << 20) | (devfn << 12);
	if (dev_base != mmcfg_last_accessed_device) {
		mmcfg_last_accessed_device = dev_base;
		set_fixmap_nocache(FIX_PCIE_MCFG, dev_base);
	}
}

static int pci_mmcfg_read(unsigned int seg, unsigned int bus,
			  unsigned int devfn, int reg, int len, u32 *value)
{
	unsigned long flags;

	if (!value || (bus > 255) || (devfn > 255) || (reg > 4095))
		return -EINVAL;

	spin_lock_irqsave(&pci_config_lock, flags);

	pci_exp_set_dev_base(seg, bus, devfn);

	switch (len) {
	case 1:
		*value = readb(mmcfg_virt_addr + reg);
		break;
	case 2:
		*value = readw(mmcfg_virt_addr + reg);
		break;
	case 4:
		*value = readl(mmcfg_virt_addr + reg);
		break;
	}

	spin_unlock_irqrestore(&pci_config_lock, flags);

	return 0;
}

static int pci_mmcfg_write(unsigned int seg, unsigned int bus,
			   unsigned int devfn, int reg, int len, u32 value)
{
	unsigned long flags;

	if ((bus > 255) || (devfn > 255) || (reg > 4095)) 
		return -EINVAL;

	spin_lock_irqsave(&pci_config_lock, flags);

	pci_exp_set_dev_base(seg, bus, devfn);

	switch (len) {
	case 1:
		writeb(value, mmcfg_virt_addr + reg);
		break;
	case 2:
		writew(value, mmcfg_virt_addr + reg);
		break;
	case 4:
		writel(value, mmcfg_virt_addr + reg);
		break;
	}

	spin_unlock_irqrestore(&pci_config_lock, flags);

	return 0;
}

static struct pci_raw_ops pci_mmcfg = {
	.read =		pci_mmcfg_read,
	.write =	pci_mmcfg_write,
};

static int __init pci_mmcfg_init(void)
{
	if ((pci_probe & PCI_PROBE_MMCONF) == 0)
		goto out;

	acpi_table_parse(ACPI_MCFG, acpi_parse_mcfg);
	if ((pci_mmcfg_config_num == 0) ||
	    (pci_mmcfg_config == NULL) ||
	    (pci_mmcfg_config[0].base_address == 0))
		goto out;

	printk(KERN_INFO "PCI: Using MMCONFIG\n");
	raw_pci_ops = &pci_mmcfg;
	pci_probe = (pci_probe & ~PCI_PROBE_MASK) | PCI_PROBE_MMCONF;

 out:
	return 0;
}

arch_initcall(pci_mmcfg_init);
