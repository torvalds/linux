/*
 * mmconfig-shared.c - Low-level direct PCI config space access via
 *                     MMCONFIG - common code between i386 and x86-64.
 *
 * This code does:
 * - known chipset handling
 * - ACPI decoding and validation
 *
 * Per-architecture code takes care of the mappings and accesses
 * themselves.
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/bitmap.h>
#include <asm/e820.h>

#include "pci.h"

/* aperture is up to 256MB but BIOS may reserve less */
#define MMCONFIG_APER_MIN	(2 * 1024*1024)
#define MMCONFIG_APER_MAX	(256 * 1024*1024)

/* Verify the first 16 busses. We assume that systems with more busses
   get MCFG right. */
#define PCI_MMCFG_MAX_CHECK_BUS 16

DECLARE_BITMAP(pci_mmcfg_fallback_slots, 32*PCI_MMCFG_MAX_CHECK_BUS);

/* K8 systems have some devices (typically in the builtin northbridge)
   that are only accessible using type1
   Normally this can be expressed in the MCFG by not listing them
   and assigning suitable _SEGs, but this isn't implemented in some BIOS.
   Instead try to discover all devices on bus 0 that are unreachable using MM
   and fallback for them. */
static __init void unreachable_devices(void)
{
	int i, k;
	/* Use the max bus number from ACPI here? */
	for (k = 0; k < PCI_MMCFG_MAX_CHECK_BUS; k++) {
		for (i = 0; i < 32; i++) {
			u32 val1, val2;

			pci_conf1_read(0, k, PCI_DEVFN(i,0), 0, 4, &val1);
			if (val1 == 0xffffffff)
				continue;

			raw_pci_ops->read(0, k, PCI_DEVFN(i, 0), 0, 4, &val2);
			if (val1 != val2) {
				set_bit(i + 32*k, pci_mmcfg_fallback_slots);
				printk(KERN_NOTICE "PCI: No mmconfig possible"
				       " on device %02x:%02x\n", k, i);
			}
		}
	}
}

static __init const char *pci_mmcfg_e7520(void)
{
	u32 win;
	pci_conf1_read(0, 0, PCI_DEVFN(0,0), 0xce, 2, &win);

	pci_mmcfg_config_num = 1;
	pci_mmcfg_config = kzalloc(sizeof(pci_mmcfg_config[0]), GFP_KERNEL);
	if (!pci_mmcfg_config)
		return NULL;
	pci_mmcfg_config[0].address = (win & 0xf000) << 16;
	pci_mmcfg_config[0].pci_segment = 0;
	pci_mmcfg_config[0].start_bus_number = 0;
	pci_mmcfg_config[0].end_bus_number = 255;

	return "Intel Corporation E7520 Memory Controller Hub";
}

static __init const char *pci_mmcfg_intel_945(void)
{
	u32 pciexbar, mask = 0, len = 0;

	pci_mmcfg_config_num = 1;

	pci_conf1_read(0, 0, PCI_DEVFN(0,0), 0x48, 4, &pciexbar);

	/* Enable bit */
	if (!(pciexbar & 1))
		pci_mmcfg_config_num = 0;

	/* Size bits */
	switch ((pciexbar >> 1) & 3) {
	case 0:
		mask = 0xf0000000U;
		len  = 0x10000000U;
		break;
	case 1:
		mask = 0xf8000000U;
		len  = 0x08000000U;
		break;
	case 2:
		mask = 0xfc000000U;
		len  = 0x04000000U;
		break;
	default:
		pci_mmcfg_config_num = 0;
	}

	/* Errata #2, things break when not aligned on a 256Mb boundary */
	/* Can only happen in 64M/128M mode */

	if ((pciexbar & mask) & 0x0fffffffU)
		pci_mmcfg_config_num = 0;

	if (pci_mmcfg_config_num) {
		pci_mmcfg_config = kzalloc(sizeof(pci_mmcfg_config[0]), GFP_KERNEL);
		if (!pci_mmcfg_config)
			return NULL;
		pci_mmcfg_config[0].address = pciexbar & mask;
		pci_mmcfg_config[0].pci_segment = 0;
		pci_mmcfg_config[0].start_bus_number = 0;
		pci_mmcfg_config[0].end_bus_number = (len >> 20) - 1;
	}

	return "Intel Corporation 945G/GZ/P/PL Express Memory Controller Hub";
}

struct pci_mmcfg_hostbridge_probe {
	u32 vendor;
	u32 device;
	const char *(*probe)(void);
};

static __initdata struct pci_mmcfg_hostbridge_probe pci_mmcfg_probes[] = {
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_E7520_MCH, pci_mmcfg_e7520 },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82945G_HB, pci_mmcfg_intel_945 },
};

static int __init pci_mmcfg_check_hostbridge(void)
{
	u32 l;
	u16 vendor, device;
	int i;
	const char *name;

	pci_conf1_read(0, 0, PCI_DEVFN(0,0), 0, 4, &l);
	vendor = l & 0xffff;
	device = (l >> 16) & 0xffff;

	pci_mmcfg_config_num = 0;
	pci_mmcfg_config = NULL;
	name = NULL;

	for (i = 0; !name && i < ARRAY_SIZE(pci_mmcfg_probes); i++)
		if ((pci_mmcfg_probes[i].vendor == PCI_ANY_ID ||
		     pci_mmcfg_probes[i].vendor == vendor) &&
		    (pci_mmcfg_probes[i].device == PCI_ANY_ID ||
		     pci_mmcfg_probes[i].device == device))
			name = pci_mmcfg_probes[i].probe();

	if (name) {
		if (pci_mmcfg_config_num)
			printk(KERN_INFO "PCI: Found %s with MMCONFIG support.\n", name);
		else
			printk(KERN_INFO "PCI: Found %s without MMCONFIG support.\n",
			       name);
	}

	return name != NULL;
}

void __init pci_mmcfg_init(int type)
{
	int known_bridge = 0;

	if ((pci_probe & PCI_PROBE_MMCONF) == 0)
		return;

	if (type == 1 && pci_mmcfg_check_hostbridge())
		known_bridge = 1;

	if (!known_bridge)
		acpi_table_parse(ACPI_SIG_MCFG, acpi_parse_mcfg);

	if ((pci_mmcfg_config_num == 0) ||
	    (pci_mmcfg_config == NULL) ||
	    (pci_mmcfg_config[0].address == 0))
		return;

	/* Only do this check when type 1 works. If it doesn't work
           assume we run on a Mac and always use MCFG */
	if (type == 1 && !known_bridge &&
	    !e820_all_mapped(pci_mmcfg_config[0].address,
			     pci_mmcfg_config[0].address + MMCONFIG_APER_MIN,
			     E820_RESERVED)) {
		printk(KERN_ERR "PCI: BIOS Bug: MCFG area at %Lx is not E820-reserved\n",
				pci_mmcfg_config[0].address);
		printk(KERN_ERR "PCI: Not using MMCONFIG.\n");
		return;
	}

	if (pci_mmcfg_arch_init()) {
		if (type == 1)
			unreachable_devices();
		pci_probe = (pci_probe & ~PCI_PROBE_MASK) | PCI_PROBE_MMCONF;
	}
}
