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
#include <asm/irq.h>

#ifdef CONFIG_ACPI

static int __init nvidia_hpet_check(struct acpi_table_header *header)
{
	return 0;
}
#endif

static int __init check_bridge(int vendor, int device)
{
#ifdef CONFIG_ACPI
	/* According to Nvidia all timer overrides are bogus unless HPET
	   is enabled. */
	if (!acpi_use_timer_override && vendor == PCI_VENDOR_ID_NVIDIA) {
		if (acpi_table_parse(ACPI_SIG_HPET, nvidia_hpet_check)) {
			acpi_skip_timer_override = 1;
			  printk(KERN_INFO "Nvidia board "
                       "detected. Ignoring ACPI "
                       "timer override.\n");
                printk(KERN_INFO "If you got timer trouble "
			 	 "try acpi_use_timer_override\n");

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

static void check_intel(void)
{
	u16 vendor, device;

	vendor = read_pci_config_16(0, 0, 0, PCI_VENDOR_ID);

	if (vendor != PCI_VENDOR_ID_INTEL)
		return;

	device = read_pci_config_16(0, 0, 0, PCI_DEVICE_ID);
#ifdef CONFIG_SMP
	if (device == PCI_DEVICE_ID_INTEL_E7320_MCH ||
	    device == PCI_DEVICE_ID_INTEL_E7520_MCH ||
	    device == PCI_DEVICE_ID_INTEL_E7525_MCH)
		quirk_intel_irqbalance();
#endif
}

void __init check_acpi_pci(void)
{
	int num, slot, func;

	/* Assume the machine supports type 1. If not it will 
	   always read ffffffff and should not have any side effect.
	   Actually a few buggy systems can machine check. Allow the user
	   to disable it by command line option at least -AK */
	if (!early_pci_allowed())
		return;

	check_intel();

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
