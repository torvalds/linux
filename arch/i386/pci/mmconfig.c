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
#include "pci.h"

/* The physical address of the MMCONFIG aperture.  Set from ACPI tables. */
u32 pci_mmcfg_base_addr;

#define mmcfg_virt_addr ((void __iomem *) fix_to_virt(FIX_PCIE_MCFG))

/* The base address of the last MMCONFIG device accessed */
static u32 mmcfg_last_accessed_device;

/*
 * Functions for accessing PCI configuration space with MMCONFIG accesses
 */

static inline void pci_exp_set_dev_base(int bus, int devfn)
{
	u32 dev_base = pci_mmcfg_base_addr | (bus << 20) | (devfn << 12);
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

	pci_exp_set_dev_base(bus, devfn);

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

	pci_exp_set_dev_base(bus, devfn);

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
	if (!pci_mmcfg_base_addr)
		goto out;

	/* Kludge for now. Don't use mmconfig on AMD systems because
	   those have some busses where mmconfig doesn't work,
	   and we don't parse ACPI MCFG well enough to handle that. 
	   Remove when proper handling is added. */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD)
		goto out; 

	printk(KERN_INFO "PCI: Using MMCONFIG\n");
	raw_pci_ops = &pci_mmcfg;
	pci_probe = (pci_probe & ~PCI_PROBE_MASK) | PCI_PROBE_MMCONF;

 out:
	return 0;
}

arch_initcall(pci_mmcfg_init);
