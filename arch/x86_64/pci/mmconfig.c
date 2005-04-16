/*
 * mmconfig.c - Low-level direct PCI config space access via MMCONFIG
 * 
 * This is an 64bit optimized version that always keeps the full mmconfig
 * space mapped. This allows lockless config space operation.
 */

#include <linux/pci.h>
#include <linux/init.h>
#include "pci.h"

#define MMCONFIG_APER_SIZE (256*1024*1024)

/* The physical address of the MMCONFIG aperture.  Set from ACPI tables. */
u32 pci_mmcfg_base_addr;

/* Static virtual mapping of the MMCONFIG aperture */
char *pci_mmcfg_virt;

static inline char *pci_dev_base(unsigned int bus, unsigned int devfn)
{
	return pci_mmcfg_virt + ((bus << 20) | (devfn << 12));
}

static int pci_mmcfg_read(unsigned int seg, unsigned int bus,
			  unsigned int devfn, int reg, int len, u32 *value)
{
	char *addr = pci_dev_base(bus, devfn); 

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
	char *addr = pci_dev_base(bus,devfn);

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
	if ((pci_probe & PCI_PROBE_MMCONF) == 0)
		return 0;
	if (!pci_mmcfg_base_addr)
		return 0;

	/* Kludge for now. Don't use mmconfig on AMD systems because
	   those have some busses where mmconfig doesn't work,
	   and we don't parse ACPI MCFG well enough to handle that. 
	   Remove when proper handling is added. */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD)
		return 0; 

	/* RED-PEN i386 doesn't do _nocache right now */
	pci_mmcfg_virt = ioremap_nocache(pci_mmcfg_base_addr, MMCONFIG_APER_SIZE);
	if (!pci_mmcfg_virt) { 
		printk("PCI: Cannot map mmconfig aperture\n");
		return 0;
	}	

	printk(KERN_INFO "PCI: Using MMCONFIG at %x\n", pci_mmcfg_base_addr);
	raw_pci_ops = &pci_mmcfg;
	pci_probe = (pci_probe & ~PCI_PROBE_MASK) | PCI_PROBE_MMCONF;

	return 0;
}

arch_initcall(pci_mmcfg_init);
