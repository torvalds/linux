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
#include <asm/e820.h>
#include "pci.h"

/* aperture is up to 256MB but BIOS may reserve less */
#define MMCONFIG_APER_MIN	(2 * 1024*1024)
#define MMCONFIG_APER_MAX	(256 * 1024*1024)

/* Assume systems with more busses have correct MCFG */
#define MAX_CHECK_BUS 16

#define mmcfg_virt_addr ((void __iomem *) fix_to_virt(FIX_PCIE_MCFG))

/* The base address of the last MMCONFIG device accessed */
static u32 mmcfg_last_accessed_device;

static DECLARE_BITMAP(fallback_slots, MAX_CHECK_BUS*32);

/*
 * Functions for accessing PCI configuration space with MMCONFIG accesses
 */
static u32 get_base_addr(unsigned int seg, int bus, unsigned devfn)
{
	int cfg_num = -1;
	struct acpi_table_mcfg_config *cfg;

	if (seg == 0 && bus < MAX_CHECK_BUS &&
	    test_bit(PCI_SLOT(devfn) + 32*bus, fallback_slots))
		return 0;

	while (1) {
		++cfg_num;
		if (cfg_num >= pci_mmcfg_config_num) {
			break;
		}
		cfg = &pci_mmcfg_config[cfg_num];
		if (cfg->pci_segment_group_number != seg)
			continue;
		if ((cfg->start_bus_number <= bus) &&
		    (cfg->end_bus_number >= bus))
			return cfg->base_address;
	}

	/* Handle more broken MCFG tables on Asus etc.
	   They only contain a single entry for bus 0-0. Assume
 	   this applies to all busses. */
	cfg = &pci_mmcfg_config[0];
	if (pci_mmcfg_config_num == 1 &&
		cfg->pci_segment_group_number == 0 &&
		(cfg->start_bus_number | cfg->end_bus_number) == 0)
		return cfg->base_address;

	/* Fall back to type 0 */
	return 0;
}

static inline void pci_exp_set_dev_base(unsigned int base, int bus, int devfn)
{
	u32 dev_base = base | (bus << 20) | (devfn << 12);
	if (dev_base != mmcfg_last_accessed_device) {
		mmcfg_last_accessed_device = dev_base;
		set_fixmap_nocache(FIX_PCIE_MCFG, dev_base);
	}
}

static int pci_mmcfg_read(unsigned int seg, unsigned int bus,
			  unsigned int devfn, int reg, int len, u32 *value)
{
	unsigned long flags;
	u32 base;

	if ((bus > 255) || (devfn > 255) || (reg > 4095)) {
		*value = -1;
		return -EINVAL;
	}

	base = get_base_addr(seg, bus, devfn);
	if (!base)
		return pci_conf1_read(seg,bus,devfn,reg,len,value);

	spin_lock_irqsave(&pci_config_lock, flags);

	pci_exp_set_dev_base(base, bus, devfn);

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
	u32 base;

	if ((bus > 255) || (devfn > 255) || (reg > 4095)) 
		return -EINVAL;

	base = get_base_addr(seg, bus, devfn);
	if (!base)
		return pci_conf1_write(seg,bus,devfn,reg,len,value);

	spin_lock_irqsave(&pci_config_lock, flags);

	pci_exp_set_dev_base(base, bus, devfn);

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

/* K8 systems have some devices (typically in the builtin northbridge)
   that are only accessible using type1
   Normally this can be expressed in the MCFG by not listing them
   and assigning suitable _SEGs, but this isn't implemented in some BIOS.
   Instead try to discover all devices on bus 0 that are unreachable using MM
   and fallback for them. */
static __init void unreachable_devices(void)
{
	int i, k;
	unsigned long flags;

	for (k = 0; k < MAX_CHECK_BUS; k++) {
		for (i = 0; i < 32; i++) {
			u32 val1;
			u32 addr;

			pci_conf1_read(0, k, PCI_DEVFN(i, 0), 0, 4, &val1);
			if (val1 == 0xffffffff)
				continue;

			/* Locking probably not needed, but safer */
			spin_lock_irqsave(&pci_config_lock, flags);
			addr = get_base_addr(0, k, PCI_DEVFN(i, 0));
			if (addr != 0)
				pci_exp_set_dev_base(addr, k, PCI_DEVFN(i, 0));
			if (addr == 0 ||
			    readl((u32 __iomem *)mmcfg_virt_addr) != val1) {
				set_bit(i, fallback_slots);
				printk(KERN_NOTICE
			"PCI: No mmconfig possible on %x:%x\n", k, i);
			}
			spin_unlock_irqrestore(&pci_config_lock, flags);
		}
	}
}

void __init pci_mmcfg_init(void)
{
	if ((pci_probe & PCI_PROBE_MMCONF) == 0)
		return;

	acpi_table_parse(ACPI_MCFG, acpi_parse_mcfg);
	if ((pci_mmcfg_config_num == 0) ||
	    (pci_mmcfg_config == NULL) ||
	    (pci_mmcfg_config[0].base_address == 0))
		return;

	if (!e820_all_mapped(pci_mmcfg_config[0].base_address,
			pci_mmcfg_config[0].base_address + MMCONFIG_APER_MIN,
			E820_RESERVED)) {
		printk(KERN_ERR "PCI: BIOS Bug: MCFG area at %x is not E820-reserved\n",
				pci_mmcfg_config[0].base_address);
		printk(KERN_ERR "PCI: Not using MMCONFIG.\n");
		return;
	}

	printk(KERN_INFO "PCI: Using MMCONFIG\n");
	raw_pci_ops = &pci_mmcfg;
	pci_probe = (pci_probe & ~PCI_PROBE_MASK) | PCI_PROBE_MMCONF;

	unreachable_devices();
}
