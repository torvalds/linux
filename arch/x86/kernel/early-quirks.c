/* Various workarounds for chipset bugs.
   This code runs very early and can't use the regular PCI subsystem
   The entries are keyed to PCI bridges which usually identify chipsets
   uniquely.
   This is only for whole classes of chipsets with specific problems which
   need early invasive action (e.g. before the timers are initialized).
   Most PCI device specific workarounds can be done later and should be
   in standard PCI quirks
   Mainboard specific bugs should be handled by DMI entries.
   CPU specific bugs in setup.c */

#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/pci_ids.h>
#include <asm/pci-direct.h>
#include <asm/dma.h>
#include <asm/io_apic.h>
#include <asm/apic.h>

#ifdef CONFIG_GART_IOMMU
#include <asm/gart.h>
#endif

static void __init via_bugs(void)
{
#ifdef CONFIG_GART_IOMMU
	if ((end_pfn > MAX_DMA32_PFN ||  force_iommu) &&
	    !gart_iommu_aperture_allowed) {
		printk(KERN_INFO
		       "Looks like a VIA chipset. Disabling IOMMU."
		       " Override with iommu=allowed\n");
		gart_iommu_aperture_disabled = 1;
	}
#endif
}

#ifdef CONFIG_ACPI
#ifdef CONFIG_X86_IO_APIC

static int __init nvidia_hpet_check(struct acpi_table_header *header)
{
	return 0;
}
#endif /* CONFIG_X86_IO_APIC */
#endif /* CONFIG_ACPI */

static void __init nvidia_bugs(void)
{
#ifdef CONFIG_ACPI
#ifdef CONFIG_X86_IO_APIC
	/*
	 * All timer overrides on Nvidia are
	 * wrong unless HPET is enabled.
	 * Unfortunately that's not true on many Asus boards.
	 * We don't know yet how to detect this automatically, but
	 * at least allow a command line override.
	 */
	if (acpi_use_timer_override)
		return;

	if (acpi_table_parse(ACPI_SIG_HPET, nvidia_hpet_check)) {
		acpi_skip_timer_override = 1;
		printk(KERN_INFO "Nvidia board "
		       "detected. Ignoring ACPI "
		       "timer override.\n");
		printk(KERN_INFO "If you got timer trouble "
			"try acpi_use_timer_override\n");
	}
#endif
#endif
	/* RED-PEN skip them on mptables too? */

}

static void __init ati_bugs(void)
{
#ifdef CONFIG_X86_IO_APIC
	if (timer_over_8254 == 1) {
		timer_over_8254 = 0;
		printk(KERN_INFO
		"ATI board detected. Disabling timer routing over 8254.\n");
	}
#endif
}

struct chipset {
	u16 vendor;
	void (*f)(void);
};

static struct chipset early_qrk[] __initdata = {
	{ PCI_VENDOR_ID_NVIDIA, nvidia_bugs },
	{ PCI_VENDOR_ID_VIA, via_bugs },
	{ PCI_VENDOR_ID_ATI, ati_bugs },
	{}
};

void __init early_quirks(void)
{
	int num, slot, func;

	if (!early_pci_allowed())
		return;

	/* Poor man's PCI discovery */
	for (num = 0; num < 32; num++) {
		for (slot = 0; slot < 32; slot++) {
			for (func = 0; func < 8; func++) {
				u32 class;
				u32 vendor;
				u8 type;
				int i;
				class = read_pci_config(num,slot,func,
							PCI_CLASS_REVISION);
				if (class == 0xffffffff)
					break;

				if ((class >> 16) != PCI_CLASS_BRIDGE_PCI)
					continue;

				vendor = read_pci_config(num, slot, func,
							 PCI_VENDOR_ID);
				vendor &= 0xffff;

				for (i = 0; early_qrk[i].f; i++)
					if (early_qrk[i].vendor == vendor) {
						early_qrk[i].f();
						return;
					}

				type = read_pci_config_byte(num, slot, func,
							    PCI_HEADER_TYPE);
				if (!(type & 0x80))
					break;
			}
		}
	}
}
