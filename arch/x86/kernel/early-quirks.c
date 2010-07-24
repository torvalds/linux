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
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/hpet.h>

static void __init fix_hypertransport_config(int num, int slot, int func)
{
	u32 htcfg;
	/*
	 * we found a hypertransport bus
	 * make sure that we are broadcasting
	 * interrupts to all cpus on the ht bus
	 * if we're using extended apic ids
	 */
	htcfg = read_pci_config(num, slot, func, 0x68);
	if (htcfg & (1 << 18)) {
		printk(KERN_INFO "Detected use of extended apic ids "
				 "on hypertransport bus\n");
		if ((htcfg & (1 << 17)) == 0) {
			printk(KERN_INFO "Enabling hypertransport extended "
					 "apic interrupt broadcast\n");
			printk(KERN_INFO "Note this is a bios bug, "
					 "please contact your hw vendor\n");
			htcfg |= (1 << 17);
			write_pci_config(num, slot, func, 0x68, htcfg);
		}
	}


}

static void __init via_bugs(int  num, int slot, int func)
{
#ifdef CONFIG_GART_IOMMU
	if ((max_pfn > MAX_DMA32_PFN ||  force_iommu) &&
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

static void __init nvidia_bugs(int num, int slot, int func)
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

#if defined(CONFIG_ACPI) && defined(CONFIG_X86_IO_APIC)
#if defined(CONFIG_ACPI) && defined(CONFIG_X86_IO_APIC)
static u32 __init ati_ixp4x0_rev(int num, int slot, int func)
{
	u32 d;
	u8  b;

	b = read_pci_config_byte(num, slot, func, 0xac);
	b &= ~(1<<5);
	write_pci_config_byte(num, slot, func, 0xac, b);

	d = read_pci_config(num, slot, func, 0x70);
	d |= 1<<8;
	write_pci_config(num, slot, func, 0x70, d);

	d = read_pci_config(num, slot, func, 0x8);
	d &= 0xff;
	return d;
}
#endif

static void __init ati_bugs(int num, int slot, int func)
{
	u32 d;
	u8  b;

	if (acpi_use_timer_override)
		return;

	d = ati_ixp4x0_rev(num, slot, func);
	if (d  < 0x82)
		acpi_skip_timer_override = 1;
	else {
		/* check for IRQ0 interrupt swap */
		outb(0x72, 0xcd6); b = inb(0xcd7);
		if (!(b & 0x2))
			acpi_skip_timer_override = 1;
	}

	if (acpi_skip_timer_override) {
		printk(KERN_INFO "SB4X0 revision 0x%x\n", d);
		printk(KERN_INFO "Ignoring ACPI timer override.\n");
		printk(KERN_INFO "If you got timer trouble "
		       "try acpi_use_timer_override\n");
	}
}

static u32 __init ati_sbx00_rev(int num, int slot, int func)
{
	u32 old, d;

	d = read_pci_config(num, slot, func, 0x70);
	old = d;
	d &= ~(1<<8);
	write_pci_config(num, slot, func, 0x70, d);
	d = read_pci_config(num, slot, func, 0x8);
	d &= 0xff;
	write_pci_config(num, slot, func, 0x70, old);

	return d;
}

static void __init ati_bugs_contd(int num, int slot, int func)
{
	u32 d, rev;

	if (acpi_use_timer_override)
		return;

	rev = ati_sbx00_rev(num, slot, func);
	if (rev > 0x13)
		return;

	/* check for IRQ0 interrupt swap */
	d = read_pci_config(num, slot, func, 0x64);
	if (!(d & (1<<14)))
		acpi_skip_timer_override = 1;

	if (acpi_skip_timer_override) {
		printk(KERN_INFO "SB600 revision 0x%x\n", rev);
		printk(KERN_INFO "Ignoring ACPI timer override.\n");
		printk(KERN_INFO "If you got timer trouble "
		       "try acpi_use_timer_override\n");
	}
}
#else
static void __init ati_bugs(int num, int slot, int func)
{
}

static void __init ati_bugs_contd(int num, int slot, int func)
{
}
#endif

/*
 * Force the read back of the CMP register in hpet_next_event()
 * to work around the problem that the CMP register write seems to be
 * delayed. See hpet_next_event() for details.
 *
 * We do this on all SMBUS incarnations for now until we have more
 * information about the affected chipsets.
 */
static void __init ati_hpet_bugs(int num, int slot, int func)
{
#ifdef CONFIG_HPET_TIMER
	hpet_readback_cmp = 1;
#endif
}

#define QFLAG_APPLY_ONCE 	0x1
#define QFLAG_APPLIED		0x2
#define QFLAG_DONE		(QFLAG_APPLY_ONCE|QFLAG_APPLIED)
struct chipset {
	u32 vendor;
	u32 device;
	u32 class;
	u32 class_mask;
	u32 flags;
	void (*f)(int num, int slot, int func);
};

/*
 * Only works for devices on the root bus. If you add any devices
 * not on bus 0 readd another loop level in early_quirks(). But
 * be careful because at least the Nvidia quirk here relies on
 * only matching on bus 0.
 */
static struct chipset early_qrk[] __initdata = {
	{ PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID,
	  PCI_CLASS_BRIDGE_PCI, PCI_ANY_ID, QFLAG_APPLY_ONCE, nvidia_bugs },
	{ PCI_VENDOR_ID_VIA, PCI_ANY_ID,
	  PCI_CLASS_BRIDGE_PCI, PCI_ANY_ID, QFLAG_APPLY_ONCE, via_bugs },
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB,
	  PCI_CLASS_BRIDGE_HOST, PCI_ANY_ID, 0, fix_hypertransport_config },
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_IXP400_SMBUS,
	  PCI_CLASS_SERIAL_SMBUS, PCI_ANY_ID, 0, ati_bugs },
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_SBX00_SMBUS,
	  PCI_CLASS_SERIAL_SMBUS, PCI_ANY_ID, 0, ati_bugs_contd },
	{ PCI_VENDOR_ID_ATI, PCI_ANY_ID,
	  PCI_CLASS_SERIAL_SMBUS, PCI_ANY_ID, 0, ati_hpet_bugs },
	{}
};

/**
 * check_dev_quirk - apply early quirks to a given PCI device
 * @num: bus number
 * @slot: slot number
 * @func: PCI function
 *
 * Check the vendor & device ID against the early quirks table.
 *
 * If the device is single function, let early_quirks() know so we don't
 * poke at this device again.
 */
static int __init check_dev_quirk(int num, int slot, int func)
{
	u16 class;
	u16 vendor;
	u16 device;
	u8 type;
	int i;

	class = read_pci_config_16(num, slot, func, PCI_CLASS_DEVICE);

	if (class == 0xffff)
		return -1; /* no class, treat as single function */

	vendor = read_pci_config_16(num, slot, func, PCI_VENDOR_ID);

	device = read_pci_config_16(num, slot, func, PCI_DEVICE_ID);

	for (i = 0; early_qrk[i].f != NULL; i++) {
		if (((early_qrk[i].vendor == PCI_ANY_ID) ||
			(early_qrk[i].vendor == vendor)) &&
			((early_qrk[i].device == PCI_ANY_ID) ||
			(early_qrk[i].device == device)) &&
			(!((early_qrk[i].class ^ class) &
			    early_qrk[i].class_mask))) {
				if ((early_qrk[i].flags &
				     QFLAG_DONE) != QFLAG_DONE)
					early_qrk[i].f(num, slot, func);
				early_qrk[i].flags |= QFLAG_APPLIED;
			}
	}

	type = read_pci_config_byte(num, slot, func,
				    PCI_HEADER_TYPE);
	if (!(type & 0x80))
		return -1;

	return 0;
}

void __init early_quirks(void)
{
	int slot, func;

	if (!early_pci_allowed())
		return;

	/* Poor man's PCI discovery */
	/* Only scan the root bus */
	for (slot = 0; slot < 32; slot++)
		for (func = 0; func < 8; func++) {
			/* Only probe function 0 on single fn devices */
			if (check_dev_quirk(0, slot, func))
				break;
		}
}
