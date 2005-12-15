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
#include "pci.h"

#define MMCONFIG_APER_SIZE (256*1024*1024)

static DECLARE_BITMAP(fallback_slots, 32);

/* Static virtual mapping of the MMCONFIG aperture */
struct mmcfg_virt {
	struct acpi_table_mcfg_config *cfg;
	char __iomem *virt;
};
static struct mmcfg_virt *pci_mmcfg_virt;

static char __iomem *get_virt(unsigned int seg, unsigned bus)
{
	int cfg_num = -1;
	struct acpi_table_mcfg_config *cfg;

	while (1) {
		++cfg_num;
		if (cfg_num >= pci_mmcfg_config_num) {
			/* Not found - fall back to type 1. This happens
			   e.g. on the internal devices of a K8 northbridge. */
			return NULL;
		}
		cfg = pci_mmcfg_virt[cfg_num].cfg;
		if (cfg->pci_segment_group_number != seg)
			continue;
		if ((cfg->start_bus_number <= bus) &&
		    (cfg->end_bus_number >= bus))
			return pci_mmcfg_virt[cfg_num].virt;
	}
}

static char __iomem *pci_dev_base(unsigned int seg, unsigned int bus, unsigned int devfn)
{
	char __iomem *addr;
	if (seg == 0 && bus == 0 && test_bit(PCI_SLOT(devfn), &fallback_slots))
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
	if (unlikely(!value || (bus > 255) || (devfn > 255) || (reg > 4095)))
		return -EINVAL;

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

/* K8 systems have some devices (typically in the builtin northbridge)
   that are only accessible using type1
   Normally this can be expressed in the MCFG by not listing them
   and assigning suitable _SEGs, but this isn't implemented in some BIOS.
   Instead try to discover all devices on bus 0 that are unreachable using MM
   and fallback for them.
   We only do this for bus 0/seg 0 */
static __init void unreachable_devices(void)
{
	int i;
	for (i = 0; i < 32; i++) {
		u32 val1;
		char __iomem *addr;

		pci_conf1_read(0, 0, PCI_DEVFN(i,0), 0, 4, &val1);
		if (val1 == 0xffffffff)
			continue;
		addr = pci_dev_base(0, 0, PCI_DEVFN(i, 0));
		if (addr == NULL|| readl(addr) != val1) {
			set_bit(i, &fallback_slots);
		}
	}
}

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

	unreachable_devices();

	raw_pci_ops = &pci_mmcfg;
	pci_probe = (pci_probe & ~PCI_PROBE_MASK) | PCI_PROBE_MMCONF;

	return 0;
}

arch_initcall(pci_mmcfg_init);
