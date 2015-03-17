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
#include <drm/i915_drm.h>
#include <asm/pci-direct.h>
#include <asm/dma.h>
#include <asm/io_apic.h>
#include <asm/apic.h>
#include <asm/hpet.h>
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/irq_remapping.h>

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
	u32 d;

	d = read_pci_config(num, slot, func, 0x8);
	d &= 0xff;

	return d;
}

static void __init ati_bugs_contd(int num, int slot, int func)
{
	u32 d, rev;

	rev = ati_sbx00_rev(num, slot, func);
	if (rev >= 0x40)
		acpi_fix_pin2_polarity = 1;

	/*
	 * SB600: revisions 0x11, 0x12, 0x13, 0x14, ...
	 * SB700: revisions 0x39, 0x3a, ...
	 * SB800: revisions 0x40, 0x41, ...
	 */
	if (rev >= 0x39)
		return;

	if (acpi_use_timer_override)
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

static void __init intel_remapping_check(int num, int slot, int func)
{
	u8 revision;
	u16 device;

	device = read_pci_config_16(num, slot, func, PCI_DEVICE_ID);
	revision = read_pci_config_byte(num, slot, func, PCI_REVISION_ID);

	/*
	 * Revision <= 13 of all triggering devices id in this quirk
	 * have a problem draining interrupts when irq remapping is
	 * enabled, and should be flagged as broken. Additionally
	 * revision 0x22 of device id 0x3405 has this problem.
	 */
	if (revision <= 0x13)
		set_irq_remapping_broken();
	else if (device == 0x3405 && revision == 0x22)
		set_irq_remapping_broken();
}

/*
 * Systems with Intel graphics controllers set aside memory exclusively
 * for gfx driver use.  This memory is not marked in the E820 as reserved
 * or as RAM, and so is subject to overlap from E820 manipulation later
 * in the boot process.  On some systems, MMIO space is allocated on top,
 * despite the efforts of the "RAM buffer" approach, which simply rounds
 * memory boundaries up to 64M to try to catch space that may decode
 * as RAM and so is not suitable for MMIO.
 *
 * And yes, so far on current devices the base addr is always under 4G.
 */
static u32 __init intel_stolen_base(int num, int slot, int func, size_t stolen_size)
{
	u32 base;

	/*
	 * For the PCI IDs in this quirk, the stolen base is always
	 * in 0x5c, aka the BDSM register (yes that's really what
	 * it's called).
	 */
	base = read_pci_config(num, slot, func, 0x5c);
	base &= ~((1<<20) - 1);

	return base;
}

#define KB(x)	((x) * 1024UL)
#define MB(x)	(KB (KB (x)))
#define GB(x)	(MB (KB (x)))

static size_t __init i830_tseg_size(void)
{
	u8 tmp = read_pci_config_byte(0, 0, 0, I830_ESMRAMC);

	if (!(tmp & TSEG_ENABLE))
		return 0;

	if (tmp & I830_TSEG_SIZE_1M)
		return MB(1);
	else
		return KB(512);
}

static size_t __init i845_tseg_size(void)
{
	u8 tmp = read_pci_config_byte(0, 0, 0, I845_ESMRAMC);

	if (!(tmp & TSEG_ENABLE))
		return 0;

	switch (tmp & I845_TSEG_SIZE_MASK) {
	case I845_TSEG_SIZE_512K:
		return KB(512);
	case I845_TSEG_SIZE_1M:
		return MB(1);
	default:
		WARN_ON(1);
		return 0;
	}
}

static size_t __init i85x_tseg_size(void)
{
	u8 tmp = read_pci_config_byte(0, 0, 0, I85X_ESMRAMC);

	if (!(tmp & TSEG_ENABLE))
		return 0;

	return MB(1);
}

static size_t __init i830_mem_size(void)
{
	return read_pci_config_byte(0, 0, 0, I830_DRB3) * MB(32);
}

static size_t __init i85x_mem_size(void)
{
	return read_pci_config_byte(0, 0, 1, I85X_DRB3) * MB(32);
}

/*
 * On 830/845/85x the stolen memory base isn't available in any
 * register. We need to calculate it as TOM-TSEG_SIZE-stolen_size.
 */
static u32 __init i830_stolen_base(int num, int slot, int func, size_t stolen_size)
{
	return i830_mem_size() - i830_tseg_size() - stolen_size;
}

static u32 __init i845_stolen_base(int num, int slot, int func, size_t stolen_size)
{
	return i830_mem_size() - i845_tseg_size() - stolen_size;
}

static u32 __init i85x_stolen_base(int num, int slot, int func, size_t stolen_size)
{
	return i85x_mem_size() - i85x_tseg_size() - stolen_size;
}

static u32 __init i865_stolen_base(int num, int slot, int func, size_t stolen_size)
{
	/*
	 * FIXME is the graphics stolen memory region
	 * always at TOUD? Ie. is it always the last
	 * one to be allocated by the BIOS?
	 */
	return read_pci_config_16(0, 0, 0, I865_TOUD) << 16;
}

static size_t __init i830_stolen_size(int num, int slot, int func)
{
	size_t stolen_size;
	u16 gmch_ctrl;

	gmch_ctrl = read_pci_config_16(0, 0, 0, I830_GMCH_CTRL);

	switch (gmch_ctrl & I830_GMCH_GMS_MASK) {
	case I830_GMCH_GMS_STOLEN_512:
		stolen_size = KB(512);
		break;
	case I830_GMCH_GMS_STOLEN_1024:
		stolen_size = MB(1);
		break;
	case I830_GMCH_GMS_STOLEN_8192:
		stolen_size = MB(8);
		break;
	case I830_GMCH_GMS_LOCAL:
		/* local memory isn't part of the normal address space */
		stolen_size = 0;
		break;
	default:
		return 0;
	}

	return stolen_size;
}

static size_t __init gen3_stolen_size(int num, int slot, int func)
{
	size_t stolen_size;
	u16 gmch_ctrl;

	gmch_ctrl = read_pci_config_16(0, 0, 0, I830_GMCH_CTRL);

	switch (gmch_ctrl & I855_GMCH_GMS_MASK) {
	case I855_GMCH_GMS_STOLEN_1M:
		stolen_size = MB(1);
		break;
	case I855_GMCH_GMS_STOLEN_4M:
		stolen_size = MB(4);
		break;
	case I855_GMCH_GMS_STOLEN_8M:
		stolen_size = MB(8);
		break;
	case I855_GMCH_GMS_STOLEN_16M:
		stolen_size = MB(16);
		break;
	case I855_GMCH_GMS_STOLEN_32M:
		stolen_size = MB(32);
		break;
	case I915_GMCH_GMS_STOLEN_48M:
		stolen_size = MB(48);
		break;
	case I915_GMCH_GMS_STOLEN_64M:
		stolen_size = MB(64);
		break;
	case G33_GMCH_GMS_STOLEN_128M:
		stolen_size = MB(128);
		break;
	case G33_GMCH_GMS_STOLEN_256M:
		stolen_size = MB(256);
		break;
	case INTEL_GMCH_GMS_STOLEN_96M:
		stolen_size = MB(96);
		break;
	case INTEL_GMCH_GMS_STOLEN_160M:
		stolen_size = MB(160);
		break;
	case INTEL_GMCH_GMS_STOLEN_224M:
		stolen_size = MB(224);
		break;
	case INTEL_GMCH_GMS_STOLEN_352M:
		stolen_size = MB(352);
		break;
	default:
		stolen_size = 0;
		break;
	}

	return stolen_size;
}

static size_t __init gen6_stolen_size(int num, int slot, int func)
{
	u16 gmch_ctrl;

	gmch_ctrl = read_pci_config_16(num, slot, func, SNB_GMCH_CTRL);
	gmch_ctrl >>= SNB_GMCH_GMS_SHIFT;
	gmch_ctrl &= SNB_GMCH_GMS_MASK;

	return gmch_ctrl << 25; /* 32 MB units */
}

static size_t __init gen8_stolen_size(int num, int slot, int func)
{
	u16 gmch_ctrl;

	gmch_ctrl = read_pci_config_16(num, slot, func, SNB_GMCH_CTRL);
	gmch_ctrl >>= BDW_GMCH_GMS_SHIFT;
	gmch_ctrl &= BDW_GMCH_GMS_MASK;
	return gmch_ctrl << 25; /* 32 MB units */
}

static size_t __init chv_stolen_size(int num, int slot, int func)
{
	u16 gmch_ctrl;

	gmch_ctrl = read_pci_config_16(num, slot, func, SNB_GMCH_CTRL);
	gmch_ctrl >>= SNB_GMCH_GMS_SHIFT;
	gmch_ctrl &= SNB_GMCH_GMS_MASK;

	/*
	 * 0x0  to 0x10: 32MB increments starting at 0MB
	 * 0x11 to 0x16: 4MB increments starting at 8MB
	 * 0x17 to 0x1d: 4MB increments start at 36MB
	 */
	if (gmch_ctrl < 0x11)
		return gmch_ctrl << 25;
	else if (gmch_ctrl < 0x17)
		return (gmch_ctrl - 0x11 + 2) << 22;
	else
		return (gmch_ctrl - 0x17 + 9) << 22;
}

struct intel_stolen_funcs {
	size_t (*size)(int num, int slot, int func);
	u32 (*base)(int num, int slot, int func, size_t size);
};

static size_t __init gen9_stolen_size(int num, int slot, int func)
{
	u16 gmch_ctrl;

	gmch_ctrl = read_pci_config_16(num, slot, func, SNB_GMCH_CTRL);
	gmch_ctrl >>= BDW_GMCH_GMS_SHIFT;
	gmch_ctrl &= BDW_GMCH_GMS_MASK;

	if (gmch_ctrl < 0xf0)
		return gmch_ctrl << 25; /* 32 MB units */
	else
		/* 4MB increments starting at 0xf0 for 4MB */
		return (gmch_ctrl - 0xf0 + 1) << 22;
}

typedef size_t (*stolen_size_fn)(int num, int slot, int func);

static const struct intel_stolen_funcs i830_stolen_funcs __initconst = {
	.base = i830_stolen_base,
	.size = i830_stolen_size,
};

static const struct intel_stolen_funcs i845_stolen_funcs __initconst = {
	.base = i845_stolen_base,
	.size = i830_stolen_size,
};

static const struct intel_stolen_funcs i85x_stolen_funcs __initconst = {
	.base = i85x_stolen_base,
	.size = gen3_stolen_size,
};

static const struct intel_stolen_funcs i865_stolen_funcs __initconst = {
	.base = i865_stolen_base,
	.size = gen3_stolen_size,
};

static const struct intel_stolen_funcs gen3_stolen_funcs __initconst = {
	.base = intel_stolen_base,
	.size = gen3_stolen_size,
};

static const struct intel_stolen_funcs gen6_stolen_funcs __initconst = {
	.base = intel_stolen_base,
	.size = gen6_stolen_size,
};

static const struct intel_stolen_funcs gen8_stolen_funcs __initconst = {
	.base = intel_stolen_base,
	.size = gen8_stolen_size,
};

static const struct intel_stolen_funcs gen9_stolen_funcs __initconst = {
	.base = intel_stolen_base,
	.size = gen9_stolen_size,
};

static const struct intel_stolen_funcs chv_stolen_funcs __initconst = {
	.base = intel_stolen_base,
	.size = chv_stolen_size,
};

static const struct pci_device_id intel_stolen_ids[] __initconst = {
	INTEL_I830_IDS(&i830_stolen_funcs),
	INTEL_I845G_IDS(&i845_stolen_funcs),
	INTEL_I85X_IDS(&i85x_stolen_funcs),
	INTEL_I865G_IDS(&i865_stolen_funcs),
	INTEL_I915G_IDS(&gen3_stolen_funcs),
	INTEL_I915GM_IDS(&gen3_stolen_funcs),
	INTEL_I945G_IDS(&gen3_stolen_funcs),
	INTEL_I945GM_IDS(&gen3_stolen_funcs),
	INTEL_VLV_M_IDS(&gen6_stolen_funcs),
	INTEL_VLV_D_IDS(&gen6_stolen_funcs),
	INTEL_PINEVIEW_IDS(&gen3_stolen_funcs),
	INTEL_I965G_IDS(&gen3_stolen_funcs),
	INTEL_G33_IDS(&gen3_stolen_funcs),
	INTEL_I965GM_IDS(&gen3_stolen_funcs),
	INTEL_GM45_IDS(&gen3_stolen_funcs),
	INTEL_G45_IDS(&gen3_stolen_funcs),
	INTEL_IRONLAKE_D_IDS(&gen3_stolen_funcs),
	INTEL_IRONLAKE_M_IDS(&gen3_stolen_funcs),
	INTEL_SNB_D_IDS(&gen6_stolen_funcs),
	INTEL_SNB_M_IDS(&gen6_stolen_funcs),
	INTEL_IVB_M_IDS(&gen6_stolen_funcs),
	INTEL_IVB_D_IDS(&gen6_stolen_funcs),
	INTEL_HSW_D_IDS(&gen6_stolen_funcs),
	INTEL_HSW_M_IDS(&gen6_stolen_funcs),
	INTEL_BDW_M_IDS(&gen8_stolen_funcs),
	INTEL_BDW_D_IDS(&gen8_stolen_funcs),
	INTEL_CHV_IDS(&chv_stolen_funcs),
	INTEL_SKL_IDS(&gen9_stolen_funcs),
	INTEL_BXT_IDS(&gen9_stolen_funcs),
};

static void __init intel_graphics_stolen(int num, int slot, int func)
{
	size_t size;
	int i;
	u32 start;
	u16 device, subvendor, subdevice;

	device = read_pci_config_16(num, slot, func, PCI_DEVICE_ID);
	subvendor = read_pci_config_16(num, slot, func,
				       PCI_SUBSYSTEM_VENDOR_ID);
	subdevice = read_pci_config_16(num, slot, func, PCI_SUBSYSTEM_ID);

	for (i = 0; i < ARRAY_SIZE(intel_stolen_ids); i++) {
		if (intel_stolen_ids[i].device == device) {
			const struct intel_stolen_funcs *stolen_funcs =
				(const struct intel_stolen_funcs *)intel_stolen_ids[i].driver_data;
			size = stolen_funcs->size(num, slot, func);
			start = stolen_funcs->base(num, slot, func, size);
			if (size && start) {
				printk(KERN_INFO "Reserving Intel graphics stolen memory at 0x%x-0x%x\n",
				       start, start + (u32)size - 1);
				/* Mark this space as reserved */
				e820_add_region(start, size, E820_RESERVED);
				sanitize_e820_map(e820.map,
						  ARRAY_SIZE(e820.map),
						  &e820.nr_map);
			}
			return;
		}
	}
}

static void __init force_disable_hpet(int num, int slot, int func)
{
#ifdef CONFIG_HPET_TIMER
	boot_hpet_disable = 1;
	pr_info("x86/hpet: Will disable the HPET for this platform because it's not reliable\n");
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
	{ PCI_VENDOR_ID_INTEL, 0x3403, PCI_CLASS_BRIDGE_HOST,
	  PCI_BASE_CLASS_BRIDGE, 0, intel_remapping_check },
	{ PCI_VENDOR_ID_INTEL, 0x3405, PCI_CLASS_BRIDGE_HOST,
	  PCI_BASE_CLASS_BRIDGE, 0, intel_remapping_check },
	{ PCI_VENDOR_ID_INTEL, 0x3406, PCI_CLASS_BRIDGE_HOST,
	  PCI_BASE_CLASS_BRIDGE, 0, intel_remapping_check },
	{ PCI_VENDOR_ID_INTEL, PCI_ANY_ID, PCI_CLASS_DISPLAY_VGA, PCI_ANY_ID,
	  QFLAG_APPLY_ONCE, intel_graphics_stolen },
	/*
	 * HPET on current version of Baytrail platform has accuracy
	 * problems, disable it for now:
	 */
	{ PCI_VENDOR_ID_INTEL, 0x0f00,
		PCI_CLASS_BRIDGE_HOST, PCI_ANY_ID, 0, force_disable_hpet},
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
