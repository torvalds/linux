/* 
 * Do early PCI probing for bug detection when the main PCI subsystem is 
 * not up yet.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/acpi.h>

#include <asm/pci-direct.h>
#include <asm/acpi.h>
#include <asm/apic.h>

#ifdef CONFIG_ACPI

static int nvidia_hpet_detected __initdata;

static int __init nvidia_hpet_check(unsigned long phys, unsigned long size)
{
	nvidia_hpet_detected = 1;
	return 0;
}
#endif

static int __init check_bridge(int vendor, int device)
{
#ifdef CONFIG_ACPI
	/* According to Nvidia all timer overrides are bogus unless HPET
	   is enabled. */
	if (vendor == PCI_VENDOR_ID_NVIDIA) {
		nvidia_hpet_detected = 0;
		acpi_table_parse(ACPI_HPET, nvidia_hpet_check);
		if (nvidia_hpet_detected == 0) {
			acpi_skip_timer_override = 1;
		}
	}
#endif
	if (vendor == PCI_VENDOR_ID_ATI && timer_over_8254 == 1) {
		timer_over_8254 = 0;
		printk(KERN_INFO "ATI board detected. Disabling timer routing "
				"over 8254.\n");
	}
	return 0;
}

void __init check_acpi_pci(void)
{
	int num, slot, func;

	/* Assume the machine supports type 1. If not it will 
	   always read ffffffff and should not have any side effect. */

	/* Poor man's PCI discovery */
	for (num = 0; num < 32; num++) {
		for (slot = 0; slot < 32; slot++) {
			for (func = 0; func < 8; func++) {
				u32 class;
				u32 vendor;
				class = read_pci_config(num, slot, func,
							PCI_CLASS_REVISION);
				if (class == 0xffffffff)
					break;

				if ((class >> 16) != PCI_CLASS_BRIDGE_PCI)
					continue;

				vendor = read_pci_config(num, slot, func,
							 PCI_VENDOR_ID);

				if (check_bridge(vendor & 0xffff, vendor >> 16))
					return;
			}

		}
	}
}
