/*
 * mmconfig.c - Low-level direct PCI config space access via MMCONFIG
 *
 * This is an 64bit optimized version that always keeps the full mmconfig
 * space mapped. This allows lockless config space operation.
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/bitmap.h>
#include <asm/e820.h>

#include "pci.h"

/* Static virtual mapping of the MMCONFIG aperture */
struct mmcfg_virt {
	struct acpi_mcfg_allocation *cfg;
	char __iomem *virt;
};
static struct mmcfg_virt *pci_mmcfg_virt;

static char __iomem *get_virt(unsigned int seg, unsigned bus)
{
	struct acpi_mcfg_allocation *cfg;
	int cfg_num;

	for (cfg_num = 0; cfg_num < pci_mmcfg_config_num; cfg_num++) {
		cfg = pci_mmcfg_virt[cfg_num].cfg;
		if (cfg->pci_segment == seg &&
		    (cfg->start_bus_number <= bus) &&
		    (cfg->end_bus_number >= bus))
			return pci_mmcfg_virt[cfg_num].virt;
	}

	/* Fall back to type 0 */
	return NULL;
}

static char __iomem *pci_dev_base(unsigned int seg, unsigned int bus, unsigned int devfn)
{
	char __iomem *addr;
	if (seg == 0 && bus < PCI_MMCFG_MAX_CHECK_BUS &&
		test_bit(32*bus + PCI_SLOT(devfn), pci_mmcfg_fallback_slots))
		return NULL;
	addr = get_virt(seg, bus);
	if (!addr)
		return NULL;
 	return addr + ((bus << 20) | (devfn << 12));
}

static int pci_mmcfg_read(unsigned int seg, unsigned int bus,
			  unsigned int devfn, int reg, int len, u32 *value)
{
	char __iomem *addr;

	/* Why do we have this when nobody checks it. How about a BUG()!? -AK */
	if (unlikely((bus > 255) || (devfn > 255) || (reg > 4095))) {
		*value = -1;
		return -EINVAL;
	}

	addr = pci_dev_base(seg, bus, devfn);
	if (!addr)
		return pci_conf1_read(seg,bus,devfn,reg,len,value);

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
	char __iomem *addr;

	/* Why do we have this when nobody checks it. How about a BUG()!? -AK */
	if (unlikely((bus > 255) || (devfn > 255) || (reg > 4095)))
		return -EINVAL;

	addr = pci_dev_base(seg, bus, devfn);
	if (!addr)
		return pci_conf1_write(seg,bus,devfn,reg,len,value);

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

static void __iomem * __init mcfg_ioremap(struct acpi_mcfg_allocation *cfg)
{
	void __iomem *addr;
	u32 size;

	size = (cfg->end_bus_number + 1) << 20;
	addr = ioremap_nocache(cfg->address, size);
	if (addr) {
		printk(KERN_INFO "PCI: Using MMCONFIG at %Lx - %Lx\n",
		       cfg->address, cfg->address + size - 1);
	}
	return addr;
}

int __init pci_mmcfg_arch_reachable(unsigned int seg, unsigned int bus,
				    unsigned int devfn)
{
	return pci_dev_base(seg, bus, devfn) != NULL;
}

int __init pci_mmcfg_arch_init(void)
{
	int i;
	pci_mmcfg_virt = kmalloc(sizeof(*pci_mmcfg_virt) *
				 pci_mmcfg_config_num, GFP_KERNEL);
	if (pci_mmcfg_virt == NULL) {
		printk(KERN_ERR "PCI: Can not allocate memory for mmconfig structures\n");
		return 0;
	}

	for (i = 0; i < pci_mmcfg_config_num; ++i) {
		pci_mmcfg_virt[i].cfg = &pci_mmcfg_config[i];
		pci_mmcfg_virt[i].virt = mcfg_ioremap(&pci_mmcfg_config[i]);
		if (!pci_mmcfg_virt[i].virt) {
			printk(KERN_ERR "PCI: Cannot map mmconfig aperture for "
					"segment %d\n",
				pci_mmcfg_config[i].pci_segment);
			return 0;
		}
	}
	raw_pci_ops = &pci_mmcfg;
	return 1;
}
