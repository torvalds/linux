/*
 * mmconfig-shared.c - Low-level direct PCI config space access via
 *                     MMCONFIG - common code between i386 and x86-64.
 *
 * This code does:
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

void __init pci_mmcfg_init(int type)
{
	if ((pci_probe & PCI_PROBE_MMCONF) == 0)
		return;

	acpi_table_parse(ACPI_SIG_MCFG, acpi_parse_mcfg);

	if ((pci_mmcfg_config_num == 0) ||
	    (pci_mmcfg_config == NULL) ||
	    (pci_mmcfg_config[0].address == 0))
		return;

	/* Only do this check when type 1 works. If it doesn't work
           assume we run on a Mac and always use MCFG */
	if (type == 1 &&
	    !e820_all_mapped(pci_mmcfg_config[0].address,
			     pci_mmcfg_config[0].address + MMCONFIG_APER_MIN,
			     E820_RESERVED)) {
		printk(KERN_ERR "PCI: BIOS Bug: MCFG area at %Lx is not E820-reserved\n",
				pci_mmcfg_config[0].address);
		printk(KERN_ERR "PCI: Not using MMCONFIG.\n");
		return;
	}

	if (pci_mmcfg_arch_init()) {
		unreachable_devices();
		pci_probe = (pci_probe & ~PCI_PROBE_MASK) | PCI_PROBE_MMCONF;
	}
}
