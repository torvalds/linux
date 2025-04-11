// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains work-arounds for many known PCI hardware bugs.
 * Devices present only on certain architectures (host bridges et cetera)
 * should be handled in arch-specific code.
 *
 * Note: any quirks for hotpluggable devices must _NOT_ be declared __init.
 *
 * Copyright (c) 1999 Martin Mares <mj@ucw.cz>
 *
 * Init/reset quirks for USB host controllers should be in the USB quirks
 * file, where their drivers can use them.
 */

#include <linux/aer.h>
#include <linux/align.h>
#include <linux/bitfield.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/isa-dma.h> /* isa_dma_bridge_buggy */
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/nvme.h>
#include <linux/platform_data/x86/apple.h>
#include <linux/pm_runtime.h>
#include <linux/sizes.h>
#include <linux/suspend.h>
#include <linux/switchtec.h>
#include "pci.h"

static bool pcie_lbms_seen(struct pci_dev *dev, u16 lnksta)
{
	unsigned long count;
	int ret;

	ret = pcie_lbms_count(dev, &count);
	if (ret < 0)
		return lnksta & PCI_EXP_LNKSTA_LBMS;

	return count > 0;
}

/*
 * Retrain the link of a downstream PCIe port by hand if necessary.
 *
 * This is needed at least where a downstream port of the ASMedia ASM2824
 * Gen 3 switch is wired to the upstream port of the Pericom PI7C9X2G304
 * Gen 2 switch, and observed with the Delock Riser Card PCI Express x1 >
 * 2 x PCIe x1 device, P/N 41433, plugged into the SiFive HiFive Unmatched
 * board.
 *
 * In such a configuration the switches are supposed to negotiate the link
 * speed of preferably 5.0GT/s, falling back to 2.5GT/s.  However the link
 * continues switching between the two speeds indefinitely and the data
 * link layer never reaches the active state, with link training reported
 * repeatedly active ~84% of the time.  Forcing the target link speed to
 * 2.5GT/s with the upstream ASM2824 device makes the two switches talk to
 * each other correctly however.  And more interestingly retraining with a
 * higher target link speed afterwards lets the two successfully negotiate
 * 5.0GT/s.
 *
 * With the ASM2824 we can rely on the otherwise optional Data Link Layer
 * Link Active status bit and in the failed link training scenario it will
 * be off along with the Link Bandwidth Management Status indicating that
 * hardware has changed the link speed or width in an attempt to correct
 * unreliable link operation.  For a port that has been left unconnected
 * both bits will be clear.  So use this information to detect the problem
 * rather than polling the Link Training bit and watching out for flips or
 * at least the active status.
 *
 * Since the exact nature of the problem isn't known and in principle this
 * could trigger where an ASM2824 device is downstream rather upstream,
 * apply this erratum workaround to any downstream ports as long as they
 * support Link Active reporting and have the Link Control 2 register.
 * Restrict the speed to 2.5GT/s then with the Target Link Speed field,
 * request a retrain and check the result.
 *
 * If this turns out successful and we know by the Vendor:Device ID it is
 * safe to do so, then lift the restriction, letting the devices negotiate
 * a higher speed.  Also check for a similar 2.5GT/s speed restriction the
 * firmware may have already arranged and lift it with ports that already
 * report their data link being up.
 *
 * Otherwise revert the speed to the original setting and request a retrain
 * again to remove any residual state, ignoring the result as it's supposed
 * to fail anyway.
 *
 * Return 0 if the link has been successfully retrained.  Return an error
 * if retraining was not needed or we attempted a retrain and it failed.
 */
int pcie_failed_link_retrain(struct pci_dev *dev)
{
	static const struct pci_device_id ids[] = {
		{ PCI_VDEVICE(ASMEDIA, 0x2824) }, /* ASMedia ASM2824 */
		{}
	};
	u16 lnksta, lnkctl2;
	int ret = -ENOTTY;

	if (!pci_is_pcie(dev) || !pcie_downstream_port(dev) ||
	    !pcie_cap_has_lnkctl2(dev) || !dev->link_active_reporting)
		return ret;

	pcie_capability_read_word(dev, PCI_EXP_LNKCTL2, &lnkctl2);
	pcie_capability_read_word(dev, PCI_EXP_LNKSTA, &lnksta);
	if (!(lnksta & PCI_EXP_LNKSTA_DLLLA) && pcie_lbms_seen(dev, lnksta)) {
		u16 oldlnkctl2 = lnkctl2;

		pci_info(dev, "broken device, retraining non-functional downstream link at 2.5GT/s\n");

		ret = pcie_set_target_speed(dev, PCIE_SPEED_2_5GT, false);
		if (ret) {
			pci_info(dev, "retraining failed\n");
			pcie_set_target_speed(dev, PCIE_LNKCTL2_TLS2SPEED(oldlnkctl2),
					      true);
			return ret;
		}

		pcie_capability_read_word(dev, PCI_EXP_LNKSTA, &lnksta);
	}

	if ((lnksta & PCI_EXP_LNKSTA_DLLLA) &&
	    (lnkctl2 & PCI_EXP_LNKCTL2_TLS) == PCI_EXP_LNKCTL2_TLS_2_5GT &&
	    pci_match_id(ids, dev)) {
		u32 lnkcap;

		pci_info(dev, "removing 2.5GT/s downstream link speed restriction\n");
		pcie_capability_read_dword(dev, PCI_EXP_LNKCAP, &lnkcap);
		ret = pcie_set_target_speed(dev, PCIE_LNKCAP_SLS2SPEED(lnkcap), false);
		if (ret) {
			pci_info(dev, "retraining failed\n");
			return ret;
		}
	}

	return ret;
}

static ktime_t fixup_debug_start(struct pci_dev *dev,
				 void (*fn)(struct pci_dev *dev))
{
	if (initcall_debug)
		pci_info(dev, "calling  %pS @ %i\n", fn, task_pid_nr(current));

	return ktime_get();
}

static void fixup_debug_report(struct pci_dev *dev, ktime_t calltime,
			       void (*fn)(struct pci_dev *dev))
{
	ktime_t delta, rettime;
	unsigned long long duration;

	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long) ktime_to_ns(delta) >> 10;
	if (initcall_debug || duration > 10000)
		pci_info(dev, "%pS took %lld usecs\n", fn, duration);
}

static void pci_do_fixups(struct pci_dev *dev, struct pci_fixup *f,
			  struct pci_fixup *end)
{
	ktime_t calltime;

	for (; f < end; f++)
		if ((f->class == (u32) (dev->class >> f->class_shift) ||
		     f->class == (u32) PCI_ANY_ID) &&
		    (f->vendor == dev->vendor ||
		     f->vendor == (u16) PCI_ANY_ID) &&
		    (f->device == dev->device ||
		     f->device == (u16) PCI_ANY_ID)) {
			void (*hook)(struct pci_dev *dev);
#ifdef CONFIG_HAVE_ARCH_PREL32_RELOCATIONS
			hook = offset_to_ptr(&f->hook_offset);
#else
			hook = f->hook;
#endif
			calltime = fixup_debug_start(dev, hook);
			hook(dev);
			fixup_debug_report(dev, calltime, hook);
		}
}

extern struct pci_fixup __start_pci_fixups_early[];
extern struct pci_fixup __end_pci_fixups_early[];
extern struct pci_fixup __start_pci_fixups_header[];
extern struct pci_fixup __end_pci_fixups_header[];
extern struct pci_fixup __start_pci_fixups_final[];
extern struct pci_fixup __end_pci_fixups_final[];
extern struct pci_fixup __start_pci_fixups_enable[];
extern struct pci_fixup __end_pci_fixups_enable[];
extern struct pci_fixup __start_pci_fixups_resume[];
extern struct pci_fixup __end_pci_fixups_resume[];
extern struct pci_fixup __start_pci_fixups_resume_early[];
extern struct pci_fixup __end_pci_fixups_resume_early[];
extern struct pci_fixup __start_pci_fixups_suspend[];
extern struct pci_fixup __end_pci_fixups_suspend[];
extern struct pci_fixup __start_pci_fixups_suspend_late[];
extern struct pci_fixup __end_pci_fixups_suspend_late[];

static bool pci_apply_fixup_final_quirks;

void pci_fixup_device(enum pci_fixup_pass pass, struct pci_dev *dev)
{
	struct pci_fixup *start, *end;

	switch (pass) {
	case pci_fixup_early:
		start = __start_pci_fixups_early;
		end = __end_pci_fixups_early;
		break;

	case pci_fixup_header:
		start = __start_pci_fixups_header;
		end = __end_pci_fixups_header;
		break;

	case pci_fixup_final:
		if (!pci_apply_fixup_final_quirks)
			return;
		start = __start_pci_fixups_final;
		end = __end_pci_fixups_final;
		break;

	case pci_fixup_enable:
		start = __start_pci_fixups_enable;
		end = __end_pci_fixups_enable;
		break;

	case pci_fixup_resume:
		start = __start_pci_fixups_resume;
		end = __end_pci_fixups_resume;
		break;

	case pci_fixup_resume_early:
		start = __start_pci_fixups_resume_early;
		end = __end_pci_fixups_resume_early;
		break;

	case pci_fixup_suspend:
		start = __start_pci_fixups_suspend;
		end = __end_pci_fixups_suspend;
		break;

	case pci_fixup_suspend_late:
		start = __start_pci_fixups_suspend_late;
		end = __end_pci_fixups_suspend_late;
		break;

	default:
		/* stupid compiler warning, you would think with an enum... */
		return;
	}
	pci_do_fixups(dev, start, end);
}
EXPORT_SYMBOL(pci_fixup_device);

static int __init pci_apply_final_quirks(void)
{
	struct pci_dev *dev = NULL;
	u8 cls = 0;
	u8 tmp;

	if (pci_cache_line_size)
		pr_info("PCI: CLS %u bytes\n", pci_cache_line_size << 2);

	pci_apply_fixup_final_quirks = true;
	for_each_pci_dev(dev) {
		pci_fixup_device(pci_fixup_final, dev);
		/*
		 * If arch hasn't set it explicitly yet, use the CLS
		 * value shared by all PCI devices.  If there's a
		 * mismatch, fall back to the default value.
		 */
		if (!pci_cache_line_size) {
			pci_read_config_byte(dev, PCI_CACHE_LINE_SIZE, &tmp);
			if (!cls)
				cls = tmp;
			if (!tmp || cls == tmp)
				continue;

			pci_info(dev, "CLS mismatch (%u != %u), using %u bytes\n",
			         cls << 2, tmp << 2,
				 pci_dfl_cache_line_size << 2);
			pci_cache_line_size = pci_dfl_cache_line_size;
		}
	}

	if (!pci_cache_line_size) {
		pr_info("PCI: CLS %u bytes, default %u\n", cls << 2,
			pci_dfl_cache_line_size << 2);
		pci_cache_line_size = cls ? cls : pci_dfl_cache_line_size;
	}

	return 0;
}
fs_initcall_sync(pci_apply_final_quirks);

/*
 * Decoding should be disabled for a PCI device during BAR sizing to avoid
 * conflict. But doing so may cause problems on host bridge and perhaps other
 * key system devices. For devices that need to have mmio decoding always-on,
 * we need to set the dev->mmio_always_on bit.
 */
static void quirk_mmio_always_on(struct pci_dev *dev)
{
	dev->mmio_always_on = 1;
}
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_ANY_ID, PCI_ANY_ID,
				PCI_CLASS_BRIDGE_HOST, 8, quirk_mmio_always_on);

/*
 * The Mellanox Tavor device gives false positive parity errors.  Disable
 * parity error reporting.
 */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_MELLANOX, PCI_DEVICE_ID_MELLANOX_TAVOR, pci_disable_parity);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_MELLANOX, PCI_DEVICE_ID_MELLANOX_TAVOR_BRIDGE, pci_disable_parity);

/*
 * Deal with broken BIOSes that neglect to enable passive release,
 * which can cause problems in combination with the 82441FX/PPro MTRRs
 */
static void quirk_passive_release(struct pci_dev *dev)
{
	struct pci_dev *d = NULL;
	unsigned char dlc;

	/*
	 * We have to make sure a particular bit is set in the PIIX3
	 * ISA bridge, so we have to go out and find it.
	 */
	while ((d = pci_get_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371SB_0, d))) {
		pci_read_config_byte(d, 0x82, &dlc);
		if (!(dlc & 1<<1)) {
			pci_info(d, "PIIX3: Enabling Passive Release\n");
			dlc |= 1<<1;
			pci_write_config_byte(d, 0x82, dlc);
		}
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82441,	quirk_passive_release);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82441,	quirk_passive_release);

#ifdef CONFIG_X86_32
/*
 * The VIA VP2/VP3/MVP3 seem to have some 'features'. There may be a
 * workaround but VIA don't answer queries. If you happen to have good
 * contacts at VIA ask them for me please -- Alan
 *
 * This appears to be BIOS not version dependent. So presumably there is a
 * chipset level fix.
 */
static void quirk_isa_dma_hangs(struct pci_dev *dev)
{
	if (!isa_dma_bridge_buggy) {
		isa_dma_bridge_buggy = 1;
		pci_info(dev, "Activating ISA DMA hang workarounds\n");
	}
}
/*
 * It's not totally clear which chipsets are the problematic ones.  We know
 * 82C586 and 82C596 variants are affected.
 */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C586_0,	quirk_isa_dma_hangs);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C596,	quirk_isa_dma_hangs);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82371SB_0,  quirk_isa_dma_hangs);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AL,	PCI_DEVICE_ID_AL_M1533,		quirk_isa_dma_hangs);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NEC,	PCI_DEVICE_ID_NEC_CBUS_1,	quirk_isa_dma_hangs);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NEC,	PCI_DEVICE_ID_NEC_CBUS_2,	quirk_isa_dma_hangs);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NEC,	PCI_DEVICE_ID_NEC_CBUS_3,	quirk_isa_dma_hangs);
#endif

#ifdef CONFIG_HAS_IOPORT
/*
 * Intel NM10 "Tiger Point" LPC PM1a_STS.BM_STS must be clear
 * for some HT machines to use C4 w/o hanging.
 */
static void quirk_tigerpoint_bm_sts(struct pci_dev *dev)
{
	u32 pmbase;
	u16 pm1a;

	pci_read_config_dword(dev, 0x40, &pmbase);
	pmbase = pmbase & 0xff80;
	pm1a = inw(pmbase);

	if (pm1a & 0x10) {
		pci_info(dev, FW_BUG "Tiger Point LPC.BM_STS cleared\n");
		outw(0x10, pmbase);
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_TGP_LPC, quirk_tigerpoint_bm_sts);
#endif

/* Chipsets where PCI->PCI transfers vanish or hang */
static void quirk_nopcipci(struct pci_dev *dev)
{
	if ((pci_pci_problems & PCIPCI_FAIL) == 0) {
		pci_info(dev, "Disabling direct PCI/PCI transfers\n");
		pci_pci_problems |= PCIPCI_FAIL;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_5597,		quirk_nopcipci);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_496,		quirk_nopcipci);

static void quirk_nopciamd(struct pci_dev *dev)
{
	u8 rev;
	pci_read_config_byte(dev, 0x08, &rev);
	if (rev == 0x13) {
		/* Erratum 24 */
		pci_info(dev, "Chipset erratum: Disabling direct PCI/AGP transfers\n");
		pci_pci_problems |= PCIAGP_FAIL;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_8151_0,	quirk_nopciamd);

/* Triton requires workarounds to be used by the drivers */
static void quirk_triton(struct pci_dev *dev)
{
	if ((pci_pci_problems&PCIPCI_TRITON) == 0) {
		pci_info(dev, "Limiting direct PCI/PCI transfers\n");
		pci_pci_problems |= PCIPCI_TRITON;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82437,	quirk_triton);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82437VX,	quirk_triton);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82439,	quirk_triton);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82439TX,	quirk_triton);

/*
 * VIA Apollo KT133 needs PCI latency patch
 * Made according to a Windows driver-based patch by George E. Breese;
 * see PCI Latency Adjust on http://www.viahardware.com/download/viatweak.shtm
 * Also see http://www.au-ja.org/review-kt133a-1-en.phtml for the info on
 * which Mr Breese based his work.
 *
 * Updated based on further information from the site and also on
 * information provided by VIA
 */
static void quirk_vialatency(struct pci_dev *dev)
{
	struct pci_dev *p;
	u8 busarb;

	/*
	 * Ok, we have a potential problem chipset here. Now see if we have
	 * a buggy southbridge.
	 */
	p = pci_get_device(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C686, NULL);
	if (p != NULL) {

		/*
		 * 0x40 - 0x4f == 686B, 0x10 - 0x2f == 686A;
		 * thanks Dan Hollis.
		 * Check for buggy part revisions
		 */
		if (p->revision < 0x40 || p->revision > 0x42)
			goto exit;
	} else {
		p = pci_get_device(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8231, NULL);
		if (p == NULL)	/* No problem parts */
			goto exit;

		/* Check for buggy part revisions */
		if (p->revision < 0x10 || p->revision > 0x12)
			goto exit;
	}

	/*
	 * Ok we have the problem. Now set the PCI master grant to occur
	 * every master grant. The apparent bug is that under high PCI load
	 * (quite common in Linux of course) you can get data loss when the
	 * CPU is held off the bus for 3 bus master requests.  This happens
	 * to include the IDE controllers....
	 *
	 * VIA only apply this fix when an SB Live! is present but under
	 * both Linux and Windows this isn't enough, and we have seen
	 * corruption without SB Live! but with things like 3 UDMA IDE
	 * controllers. So we ignore that bit of the VIA recommendation..
	 */
	pci_read_config_byte(dev, 0x76, &busarb);

	/*
	 * Set bit 4 and bit 5 of byte 76 to 0x01
	 * "Master priority rotation on every PCI master grant"
	 */
	busarb &= ~(1<<5);
	busarb |= (1<<4);
	pci_write_config_byte(dev, 0x76, busarb);
	pci_info(dev, "Applying VIA southbridge workaround\n");
exit:
	pci_dev_put(p);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8363_0,	quirk_vialatency);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8371_1,	quirk_vialatency);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8361,		quirk_vialatency);
/* Must restore this on a resume from RAM */
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8363_0,	quirk_vialatency);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8371_1,	quirk_vialatency);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8361,		quirk_vialatency);

/* VIA Apollo VP3 needs ETBF on BT848/878 */
static void quirk_viaetbf(struct pci_dev *dev)
{
	if ((pci_pci_problems&PCIPCI_VIAETBF) == 0) {
		pci_info(dev, "Limiting direct PCI/PCI transfers\n");
		pci_pci_problems |= PCIPCI_VIAETBF;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C597_0,	quirk_viaetbf);

static void quirk_vsfx(struct pci_dev *dev)
{
	if ((pci_pci_problems&PCIPCI_VSFX) == 0) {
		pci_info(dev, "Limiting direct PCI/PCI transfers\n");
		pci_pci_problems |= PCIPCI_VSFX;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C576,	quirk_vsfx);

/*
 * ALi Magik requires workarounds to be used by the drivers that DMA to AGP
 * space. Latency must be set to 0xA and Triton workaround applied too.
 * [Info kindly provided by ALi]
 */
static void quirk_alimagik(struct pci_dev *dev)
{
	if ((pci_pci_problems&PCIPCI_ALIMAGIK) == 0) {
		pci_info(dev, "Limiting direct PCI/PCI transfers\n");
		pci_pci_problems |= PCIPCI_ALIMAGIK|PCIPCI_TRITON;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AL,	PCI_DEVICE_ID_AL_M1647,		quirk_alimagik);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AL,	PCI_DEVICE_ID_AL_M1651,		quirk_alimagik);

/* Natoma has some interesting boundary conditions with Zoran stuff at least */
static void quirk_natoma(struct pci_dev *dev)
{
	if ((pci_pci_problems&PCIPCI_NATOMA) == 0) {
		pci_info(dev, "Limiting direct PCI/PCI transfers\n");
		pci_pci_problems |= PCIPCI_NATOMA;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82441,	quirk_natoma);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82443LX_0,	quirk_natoma);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82443LX_1,	quirk_natoma);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82443BX_0,	quirk_natoma);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82443BX_1,	quirk_natoma);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82443BX_2,	quirk_natoma);

/*
 * This chip can cause PCI parity errors if config register 0xA0 is read
 * while DMAs are occurring.
 */
static void quirk_citrine(struct pci_dev *dev)
{
	dev->cfg_size = 0xA0;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_IBM,	PCI_DEVICE_ID_IBM_CITRINE,	quirk_citrine);

/*
 * This chip can cause bus lockups if config addresses above 0x600
 * are read or written.
 */
static void quirk_nfp6000(struct pci_dev *dev)
{
	dev->cfg_size = 0x600;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NETRONOME,	PCI_DEVICE_ID_NETRONOME_NFP4000,	quirk_nfp6000);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NETRONOME,	PCI_DEVICE_ID_NETRONOME_NFP6000,	quirk_nfp6000);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NETRONOME,	PCI_DEVICE_ID_NETRONOME_NFP5000,	quirk_nfp6000);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NETRONOME,	PCI_DEVICE_ID_NETRONOME_NFP6000_VF,	quirk_nfp6000);

/*  On IBM Crocodile ipr SAS adapters, expand BAR to system page size */
static void quirk_extend_bar_to_page(struct pci_dev *dev)
{
	int i;

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		struct resource *r = &dev->resource[i];
		const char *r_name = pci_resource_name(dev, i);

		if (r->flags & IORESOURCE_MEM && resource_size(r) < PAGE_SIZE) {
			resource_set_range(r, 0, PAGE_SIZE);
			r->flags |= IORESOURCE_UNSET;
			pci_info(dev, "%s %pR: expanded to page size\n",
				 r_name, r);
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_IBM, 0x034a, quirk_extend_bar_to_page);

/*
 * S3 868 and 968 chips report region size equal to 32M, but they decode 64M.
 * If it's needed, re-allocate the region.
 */
static void quirk_s3_64M(struct pci_dev *dev)
{
	struct resource *r = &dev->resource[0];

	if (!IS_ALIGNED(r->start, SZ_64M) || resource_size(r) != SZ_64M) {
		r->flags |= IORESOURCE_UNSET;
		resource_set_range(r, 0, SZ_64M);
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_S3,	PCI_DEVICE_ID_S3_868,		quirk_s3_64M);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_S3,	PCI_DEVICE_ID_S3_968,		quirk_s3_64M);

static void quirk_io(struct pci_dev *dev, int pos, unsigned int size,
		     const char *name)
{
	u32 region;
	struct pci_bus_region bus_region;
	struct resource *res = pci_resource_n(dev, pos);
	const char *res_name = pci_resource_name(dev, pos);

	pci_read_config_dword(dev, PCI_BASE_ADDRESS_0 + (pos << 2), &region);

	if (!region)
		return;

	res->name = pci_name(dev);
	res->flags = region & ~PCI_BASE_ADDRESS_IO_MASK;
	res->flags |=
		(IORESOURCE_IO | IORESOURCE_PCI_FIXED | IORESOURCE_SIZEALIGN);
	region &= ~(size - 1);

	/* Convert from PCI bus to resource space */
	bus_region.start = region;
	bus_region.end = region + size - 1;
	pcibios_bus_to_resource(dev->bus, res, &bus_region);

	pci_info(dev, FW_BUG "%s %pR: %s quirk\n", res_name, res, name);
}

/*
 * Some CS5536 BIOSes (for example, the Soekris NET5501 board w/ comBIOS
 * ver. 1.33  20070103) don't set the correct ISA PCI region header info.
 * BAR0 should be 8 bytes; instead, it may be set to something like 8k
 * (which conflicts w/ BAR1's memory range).
 *
 * CS553x's ISA PCI BARs may also be read-only (ref:
 * https://bugzilla.kernel.org/show_bug.cgi?id=85991 - Comment #4 forward).
 */
static void quirk_cs5536_vsa(struct pci_dev *dev)
{
	static char *name = "CS5536 ISA bridge";

	if (pci_resource_len(dev, 0) != 8) {
		quirk_io(dev, 0,   8, name);	/* SMB */
		quirk_io(dev, 1, 256, name);	/* GPIO */
		quirk_io(dev, 2,  64, name);	/* MFGPT */
		pci_info(dev, "%s bug detected (incorrect header); workaround applied\n",
			 name);
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_CS5536_ISA, quirk_cs5536_vsa);

static void quirk_io_region(struct pci_dev *dev, int port,
			    unsigned int size, int nr, const char *name)
{
	u16 region;
	struct pci_bus_region bus_region;
	struct resource *res = pci_resource_n(dev, nr);

	pci_read_config_word(dev, port, &region);
	region &= ~(size - 1);

	if (!region)
		return;

	res->name = pci_name(dev);
	res->flags = IORESOURCE_IO;

	/* Convert from PCI bus to resource space */
	bus_region.start = region;
	bus_region.end = region + size - 1;
	pcibios_bus_to_resource(dev->bus, res, &bus_region);

	/*
	 * "res" is typically a bridge window resource that's not being
	 * used for a bridge window, so it's just a place to stash this
	 * non-standard resource.  Printing "nr" or pci_resource_name() of
	 * it doesn't really make sense.
	 */
	if (!pci_claim_resource(dev, nr))
		pci_info(dev, "quirk: %pR claimed by %s\n", res, name);
}

/*
 * ATI Northbridge setups MCE the processor if you even read somewhere
 * between 0x3b0->0x3bb or read 0x3d3
 */
static void quirk_ati_exploding_mce(struct pci_dev *dev)
{
	pci_info(dev, "ATI Northbridge, reserving I/O ports 0x3b0 to 0x3bb\n");
	/* Mae rhaid i ni beidio ag edrych ar y lleoliadiau I/O hyn */
	request_region(0x3b0, 0x0C, "RadeonIGP");
	request_region(0x3d3, 0x01, "RadeonIGP");
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI,	PCI_DEVICE_ID_ATI_RS100,   quirk_ati_exploding_mce);

/*
 * In the AMD NL platform, this device ([1022:7912]) has a class code of
 * PCI_CLASS_SERIAL_USB_XHCI (0x0c0330), which means the xhci driver will
 * claim it. The same applies on the VanGogh platform device ([1022:163a]).
 *
 * But the dwc3 driver is a more specific driver for this device, and we'd
 * prefer to use it instead of xhci. To prevent xhci from claiming the
 * device, change the class code to 0x0c03fe, which the PCI r3.0 spec
 * defines as "USB device (not host controller)". The dwc3 driver can then
 * claim it based on its Vendor and Device ID.
 */
static void quirk_amd_dwc_class(struct pci_dev *pdev)
{
	u32 class = pdev->class;

	if (class != PCI_CLASS_SERIAL_USB_DEVICE) {
		/* Use "USB Device (not host controller)" class */
		pdev->class = PCI_CLASS_SERIAL_USB_DEVICE;
		pci_info(pdev,
			"PCI class overridden (%#08x -> %#08x) so dwc3 driver can claim this instead of xhci\n",
			class, pdev->class);
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_NL_USB,
		quirk_amd_dwc_class);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_VANGOGH_USB,
		quirk_amd_dwc_class);

/*
 * Synopsys USB 3.x host HAPS platform has a class code of
 * PCI_CLASS_SERIAL_USB_XHCI, and xhci driver can claim it.  However, these
 * devices should use dwc3-haps driver.  Change these devices' class code to
 * PCI_CLASS_SERIAL_USB_DEVICE to prevent the xhci-pci driver from claiming
 * them.
 */
static void quirk_synopsys_haps(struct pci_dev *pdev)
{
	u32 class = pdev->class;

	switch (pdev->device) {
	case PCI_DEVICE_ID_SYNOPSYS_HAPSUSB3:
	case PCI_DEVICE_ID_SYNOPSYS_HAPSUSB3_AXI:
	case PCI_DEVICE_ID_SYNOPSYS_HAPSUSB31:
		pdev->class = PCI_CLASS_SERIAL_USB_DEVICE;
		pci_info(pdev, "PCI class overridden (%#08x -> %#08x) so dwc3 driver can claim this instead of xhci\n",
			 class, pdev->class);
		break;
	}
}
DECLARE_PCI_FIXUP_CLASS_HEADER(PCI_VENDOR_ID_SYNOPSYS, PCI_ANY_ID,
			       PCI_CLASS_SERIAL_USB_XHCI, 0,
			       quirk_synopsys_haps);

/*
 * Let's make the southbridge information explicit instead of having to
 * worry about people probing the ACPI areas, for example.. (Yes, it
 * happens, and if you read the wrong ACPI register it will put the machine
 * to sleep with no way of waking it up again. Bummer).
 *
 * ALI M7101: Two IO regions pointed to by words at
 *	0xE0 (64 bytes of ACPI registers)
 *	0xE2 (32 bytes of SMB registers)
 */
static void quirk_ali7101_acpi(struct pci_dev *dev)
{
	quirk_io_region(dev, 0xE0, 64, PCI_BRIDGE_RESOURCES, "ali7101 ACPI");
	quirk_io_region(dev, 0xE2, 32, PCI_BRIDGE_RESOURCES+1, "ali7101 SMB");
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AL,	PCI_DEVICE_ID_AL_M7101,		quirk_ali7101_acpi);

static void piix4_io_quirk(struct pci_dev *dev, const char *name, unsigned int port, unsigned int enable)
{
	u32 devres;
	u32 mask, size, base;

	pci_read_config_dword(dev, port, &devres);
	if ((devres & enable) != enable)
		return;
	mask = (devres >> 16) & 15;
	base = devres & 0xffff;
	size = 16;
	for (;;) {
		unsigned int bit = size >> 1;
		if ((bit & mask) == bit)
			break;
		size = bit;
	}
	/*
	 * For now we only print it out. Eventually we'll want to
	 * reserve it (at least if it's in the 0x1000+ range), but
	 * let's get enough confirmation reports first.
	 */
	base &= -size;
	pci_info(dev, "%s PIO at %04x-%04x\n", name, base, base + size - 1);
}

static void piix4_mem_quirk(struct pci_dev *dev, const char *name, unsigned int port, unsigned int enable)
{
	u32 devres;
	u32 mask, size, base;

	pci_read_config_dword(dev, port, &devres);
	if ((devres & enable) != enable)
		return;
	base = devres & 0xffff0000;
	mask = (devres & 0x3f) << 16;
	size = 128 << 16;
	for (;;) {
		unsigned int bit = size >> 1;
		if ((bit & mask) == bit)
			break;
		size = bit;
	}

	/*
	 * For now we only print it out. Eventually we'll want to
	 * reserve it, but let's get enough confirmation reports first.
	 */
	base &= -size;
	pci_info(dev, "%s MMIO at %04x-%04x\n", name, base, base + size - 1);
}

/*
 * PIIX4 ACPI: Two IO regions pointed to by longwords at
 *	0x40 (64 bytes of ACPI registers)
 *	0x90 (16 bytes of SMB registers)
 * and a few strange programmable PIIX4 device resources.
 */
static void quirk_piix4_acpi(struct pci_dev *dev)
{
	u32 res_a;

	quirk_io_region(dev, 0x40, 64, PCI_BRIDGE_RESOURCES, "PIIX4 ACPI");
	quirk_io_region(dev, 0x90, 16, PCI_BRIDGE_RESOURCES+1, "PIIX4 SMB");

	/* Device resource A has enables for some of the other ones */
	pci_read_config_dword(dev, 0x5c, &res_a);

	piix4_io_quirk(dev, "PIIX4 devres B", 0x60, 3 << 21);
	piix4_io_quirk(dev, "PIIX4 devres C", 0x64, 3 << 21);

	/* Device resource D is just bitfields for static resources */

	/* Device 12 enabled? */
	if (res_a & (1 << 29)) {
		piix4_io_quirk(dev, "PIIX4 devres E", 0x68, 1 << 20);
		piix4_mem_quirk(dev, "PIIX4 devres F", 0x6c, 1 << 7);
	}
	/* Device 13 enabled? */
	if (res_a & (1 << 30)) {
		piix4_io_quirk(dev, "PIIX4 devres G", 0x70, 1 << 20);
		piix4_mem_quirk(dev, "PIIX4 devres H", 0x74, 1 << 7);
	}
	piix4_io_quirk(dev, "PIIX4 devres I", 0x78, 1 << 20);
	piix4_io_quirk(dev, "PIIX4 devres J", 0x7c, 1 << 20);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82371AB_3,	quirk_piix4_acpi);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82443MX_3,	quirk_piix4_acpi);

#define ICH_PMBASE	0x40
#define ICH_ACPI_CNTL	0x44
#define  ICH4_ACPI_EN	0x10
#define  ICH6_ACPI_EN	0x80
#define ICH4_GPIOBASE	0x58
#define ICH4_GPIO_CNTL	0x5c
#define  ICH4_GPIO_EN	0x10
#define ICH6_GPIOBASE	0x48
#define ICH6_GPIO_CNTL	0x4c
#define  ICH6_GPIO_EN	0x10

/*
 * ICH4, ICH4-M, ICH5, ICH5-M ACPI: Three IO regions pointed to by longwords at
 *	0x40 (128 bytes of ACPI, GPIO & TCO registers)
 *	0x58 (64 bytes of GPIO I/O space)
 */
static void quirk_ich4_lpc_acpi(struct pci_dev *dev)
{
	u8 enable;

	/*
	 * The check for PCIBIOS_MIN_IO is to ensure we won't create a conflict
	 * with low legacy (and fixed) ports. We don't know the decoding
	 * priority and can't tell whether the legacy device or the one created
	 * here is really at that address.  This happens on boards with broken
	 * BIOSes.
	 */
	pci_read_config_byte(dev, ICH_ACPI_CNTL, &enable);
	if (enable & ICH4_ACPI_EN)
		quirk_io_region(dev, ICH_PMBASE, 128, PCI_BRIDGE_RESOURCES,
				 "ICH4 ACPI/GPIO/TCO");

	pci_read_config_byte(dev, ICH4_GPIO_CNTL, &enable);
	if (enable & ICH4_GPIO_EN)
		quirk_io_region(dev, ICH4_GPIOBASE, 64, PCI_BRIDGE_RESOURCES+1,
				"ICH4 GPIO");
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801AA_0,		quirk_ich4_lpc_acpi);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801AB_0,		quirk_ich4_lpc_acpi);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801BA_0,		quirk_ich4_lpc_acpi);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801BA_10,	quirk_ich4_lpc_acpi);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801CA_0,		quirk_ich4_lpc_acpi);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801CA_12,	quirk_ich4_lpc_acpi);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801DB_0,		quirk_ich4_lpc_acpi);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801DB_12,	quirk_ich4_lpc_acpi);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801EB_0,		quirk_ich4_lpc_acpi);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_ESB_1,		quirk_ich4_lpc_acpi);

static void ich6_lpc_acpi_gpio(struct pci_dev *dev)
{
	u8 enable;

	pci_read_config_byte(dev, ICH_ACPI_CNTL, &enable);
	if (enable & ICH6_ACPI_EN)
		quirk_io_region(dev, ICH_PMBASE, 128, PCI_BRIDGE_RESOURCES,
				 "ICH6 ACPI/GPIO/TCO");

	pci_read_config_byte(dev, ICH6_GPIO_CNTL, &enable);
	if (enable & ICH6_GPIO_EN)
		quirk_io_region(dev, ICH6_GPIOBASE, 64, PCI_BRIDGE_RESOURCES+1,
				"ICH6 GPIO");
}

static void ich6_lpc_generic_decode(struct pci_dev *dev, unsigned int reg,
				    const char *name, int dynsize)
{
	u32 val;
	u32 size, base;

	pci_read_config_dword(dev, reg, &val);

	/* Enabled? */
	if (!(val & 1))
		return;
	base = val & 0xfffc;
	if (dynsize) {
		/*
		 * This is not correct. It is 16, 32 or 64 bytes depending on
		 * register D31:F0:ADh bits 5:4.
		 *
		 * But this gets us at least _part_ of it.
		 */
		size = 16;
	} else {
		size = 128;
	}
	base &= ~(size-1);

	/*
	 * Just print it out for now. We should reserve it after more
	 * debugging.
	 */
	pci_info(dev, "%s PIO at %04x-%04x\n", name, base, base+size-1);
}

static void quirk_ich6_lpc(struct pci_dev *dev)
{
	/* Shared ACPI/GPIO decode with all ICH6+ */
	ich6_lpc_acpi_gpio(dev);

	/* ICH6-specific generic IO decode */
	ich6_lpc_generic_decode(dev, 0x84, "LPC Generic IO decode 1", 0);
	ich6_lpc_generic_decode(dev, 0x88, "LPC Generic IO decode 2", 1);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH6_0, quirk_ich6_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH6_1, quirk_ich6_lpc);

static void ich7_lpc_generic_decode(struct pci_dev *dev, unsigned int reg,
				    const char *name)
{
	u32 val;
	u32 mask, base;

	pci_read_config_dword(dev, reg, &val);

	/* Enabled? */
	if (!(val & 1))
		return;

	/* IO base in bits 15:2, mask in bits 23:18, both are dword-based */
	base = val & 0xfffc;
	mask = (val >> 16) & 0xfc;
	mask |= 3;

	/*
	 * Just print it out for now. We should reserve it after more
	 * debugging.
	 */
	pci_info(dev, "%s PIO at %04x (mask %04x)\n", name, base, mask);
}

/* ICH7-10 has the same common LPC generic IO decode registers */
static void quirk_ich7_lpc(struct pci_dev *dev)
{
	/* We share the common ACPI/GPIO decode with ICH6 */
	ich6_lpc_acpi_gpio(dev);

	/* And have 4 ICH7+ generic decodes */
	ich7_lpc_generic_decode(dev, 0x84, "ICH7 LPC Generic IO decode 1");
	ich7_lpc_generic_decode(dev, 0x88, "ICH7 LPC Generic IO decode 2");
	ich7_lpc_generic_decode(dev, 0x8c, "ICH7 LPC Generic IO decode 3");
	ich7_lpc_generic_decode(dev, 0x90, "ICH7 LPC Generic IO decode 4");
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH7_0, quirk_ich7_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH7_1, quirk_ich7_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH7_31, quirk_ich7_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH8_0, quirk_ich7_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH8_2, quirk_ich7_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH8_3, quirk_ich7_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH8_1, quirk_ich7_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH8_4, quirk_ich7_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH9_2, quirk_ich7_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH9_4, quirk_ich7_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH9_7, quirk_ich7_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH9_8, quirk_ich7_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,   PCI_DEVICE_ID_INTEL_ICH10_1, quirk_ich7_lpc);

/*
 * VIA ACPI: One IO region pointed to by longword at
 *	0x48 or 0x20 (256 bytes of ACPI registers)
 */
static void quirk_vt82c586_acpi(struct pci_dev *dev)
{
	if (dev->revision & 0x10)
		quirk_io_region(dev, 0x48, 256, PCI_BRIDGE_RESOURCES,
				"vt82c586 ACPI");
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C586_3,	quirk_vt82c586_acpi);

/*
 * VIA VT82C686 ACPI: Three IO region pointed to by (long)words at
 *	0x48 (256 bytes of ACPI registers)
 *	0x70 (128 bytes of hardware monitoring register)
 *	0x90 (16 bytes of SMB registers)
 */
static void quirk_vt82c686_acpi(struct pci_dev *dev)
{
	quirk_vt82c586_acpi(dev);

	quirk_io_region(dev, 0x70, 128, PCI_BRIDGE_RESOURCES+1,
				 "vt82c686 HW-mon");

	quirk_io_region(dev, 0x90, 16, PCI_BRIDGE_RESOURCES+2, "vt82c686 SMB");
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C686_4,	quirk_vt82c686_acpi);

/*
 * VIA VT8235 ISA Bridge: Two IO regions pointed to by words at
 *	0x88 (128 bytes of power management registers)
 *	0xd0 (16 bytes of SMB registers)
 */
static void quirk_vt8235_acpi(struct pci_dev *dev)
{
	quirk_io_region(dev, 0x88, 128, PCI_BRIDGE_RESOURCES, "vt8235 PM");
	quirk_io_region(dev, 0xd0, 16, PCI_BRIDGE_RESOURCES+1, "vt8235 SMB");
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8235,	quirk_vt8235_acpi);

/*
 * TI XIO2000a PCIe-PCI Bridge erroneously reports it supports fast
 * back-to-back: Disable fast back-to-back on the secondary bus segment
 */
static void quirk_xio2000a(struct pci_dev *dev)
{
	struct pci_dev *pdev;
	u16 command;

	pci_warn(dev, "TI XIO2000a quirk detected; secondary bus fast back-to-back transfers disabled\n");
	list_for_each_entry(pdev, &dev->subordinate->devices, bus_list) {
		pci_read_config_word(pdev, PCI_COMMAND, &command);
		if (command & PCI_COMMAND_FAST_BACK)
			pci_write_config_word(pdev, PCI_COMMAND, command & ~PCI_COMMAND_FAST_BACK);
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_XIO2000A,
			quirk_xio2000a);

#ifdef CONFIG_X86_IO_APIC

#include <asm/io_apic.h>

/*
 * VIA 686A/B: If an IO-APIC is active, we need to route all on-chip
 * devices to the external APIC.
 *
 * TODO: When we have device-specific interrupt routers, this code will go
 * away from quirks.
 */
static void quirk_via_ioapic(struct pci_dev *dev)
{
	u8 tmp;

	if (nr_ioapics < 1)
		tmp = 0;    /* nothing routed to external APIC */
	else
		tmp = 0x1f; /* all known bits (4-0) routed to external APIC */

	pci_info(dev, "%s VIA external APIC routing\n",
		 tmp ? "Enabling" : "Disabling");

	/* Offset 0x58: External APIC IRQ output control */
	pci_write_config_byte(dev, 0x58, tmp);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C686,	quirk_via_ioapic);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C686,	quirk_via_ioapic);

/*
 * VIA 8237: Some BIOSes don't set the 'Bypass APIC De-Assert Message' Bit.
 * This leads to doubled level interrupt rates.
 * Set this bit to get rid of cycle wastage.
 * Otherwise uncritical.
 */
static void quirk_via_vt8237_bypass_apic_deassert(struct pci_dev *dev)
{
	u8 misc_control2;
#define BYPASS_APIC_DEASSERT 8

	pci_read_config_byte(dev, 0x5B, &misc_control2);
	if (!(misc_control2 & BYPASS_APIC_DEASSERT)) {
		pci_info(dev, "Bypassing VIA 8237 APIC De-Assert Message\n");
		pci_write_config_byte(dev, 0x5B, misc_control2|BYPASS_APIC_DEASSERT);
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8237,		quirk_via_vt8237_bypass_apic_deassert);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8237,		quirk_via_vt8237_bypass_apic_deassert);

/*
 * The AMD IO-APIC can hang the box when an APIC IRQ is masked.
 * We check all revs >= B0 (yet not in the pre production!) as the bug
 * is currently marked NoFix
 *
 * We have multiple reports of hangs with this chipset that went away with
 * noapic specified. For the moment we assume it's the erratum. We may be wrong
 * of course. However the advice is demonstrably good even if so.
 */
static void quirk_amd_ioapic(struct pci_dev *dev)
{
	if (dev->revision >= 0x02) {
		pci_warn(dev, "I/O APIC: AMD Erratum #22 may be present. In the event of instability try\n");
		pci_warn(dev, "        : booting with the \"noapic\" option\n");
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_VIPER_7410,	quirk_amd_ioapic);
#endif /* CONFIG_X86_IO_APIC */

#if defined(CONFIG_ARM64) && defined(CONFIG_PCI_ATS)

static void quirk_cavium_sriov_rnm_link(struct pci_dev *dev)
{
	/* Fix for improper SR-IOV configuration on Cavium cn88xx RNM device */
	if (dev->subsystem_device == 0xa118)
		dev->sriov->link = dev->devfn;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_CAVIUM, 0xa018, quirk_cavium_sriov_rnm_link);
#endif

/*
 * Some settings of MMRBC can lead to data corruption so block changes.
 * See AMD 8131 HyperTransport PCI-X Tunnel Revision Guide
 */
static void quirk_amd_8131_mmrbc(struct pci_dev *dev)
{
	if (dev->subordinate && dev->revision <= 0x12) {
		pci_info(dev, "AMD8131 rev %x detected; disabling PCI-X MMRBC\n",
			 dev->revision);
		dev->subordinate->bus_flags |= PCI_BUS_FLAGS_NO_MMRBC;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_8131_BRIDGE, quirk_amd_8131_mmrbc);

/*
 * FIXME: it is questionable that quirk_via_acpi() is needed.  It shows up
 * as an ISA bridge, and does not support the PCI_INTERRUPT_LINE register
 * at all.  Therefore it seems like setting the pci_dev's IRQ to the value
 * of the ACPI SCI interrupt is only done for convenience.
 *	-jgarzik
 */
static void quirk_via_acpi(struct pci_dev *d)
{
	u8 irq;

	/* VIA ACPI device: SCI IRQ line in PCI config byte 0x42 */
	pci_read_config_byte(d, 0x42, &irq);
	irq &= 0xf;
	if (irq && (irq != 2))
		d->irq = irq;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C586_3,	quirk_via_acpi);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C686_4,	quirk_via_acpi);

/* VIA bridges which have VLink */
static int via_vlink_dev_lo = -1, via_vlink_dev_hi = 18;

static void quirk_via_bridge(struct pci_dev *dev)
{
	/* See what bridge we have and find the device ranges */
	switch (dev->device) {
	case PCI_DEVICE_ID_VIA_82C686:
		/*
		 * The VT82C686 is special; it attaches to PCI and can have
		 * any device number. All its subdevices are functions of
		 * that single device.
		 */
		via_vlink_dev_lo = PCI_SLOT(dev->devfn);
		via_vlink_dev_hi = PCI_SLOT(dev->devfn);
		break;
	case PCI_DEVICE_ID_VIA_8237:
	case PCI_DEVICE_ID_VIA_8237A:
		via_vlink_dev_lo = 15;
		break;
	case PCI_DEVICE_ID_VIA_8235:
		via_vlink_dev_lo = 16;
		break;
	case PCI_DEVICE_ID_VIA_8231:
	case PCI_DEVICE_ID_VIA_8233_0:
	case PCI_DEVICE_ID_VIA_8233A:
	case PCI_DEVICE_ID_VIA_8233C_0:
		via_vlink_dev_lo = 17;
		break;
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C686,	quirk_via_bridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8231,		quirk_via_bridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8233_0,	quirk_via_bridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8233A,	quirk_via_bridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8233C_0,	quirk_via_bridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8235,		quirk_via_bridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8237,		quirk_via_bridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8237A,	quirk_via_bridge);

/*
 * quirk_via_vlink		-	VIA VLink IRQ number update
 * @dev: PCI device
 *
 * If the device we are dealing with is on a PIC IRQ we need to ensure that
 * the IRQ line register which usually is not relevant for PCI cards, is
 * actually written so that interrupts get sent to the right place.
 *
 * We only do this on systems where a VIA south bridge was detected, and
 * only for VIA devices on the motherboard (see quirk_via_bridge above).
 */
static void quirk_via_vlink(struct pci_dev *dev)
{
	u8 irq, new_irq;

	/* Check if we have VLink at all */
	if (via_vlink_dev_lo == -1)
		return;

	new_irq = dev->irq;

	/* Don't quirk interrupts outside the legacy IRQ range */
	if (!new_irq || new_irq > 15)
		return;

	/* Internal device ? */
	if (dev->bus->number != 0 || PCI_SLOT(dev->devfn) > via_vlink_dev_hi ||
	    PCI_SLOT(dev->devfn) < via_vlink_dev_lo)
		return;

	/*
	 * This is an internal VLink device on a PIC interrupt. The BIOS
	 * ought to have set this but may not have, so we redo it.
	 */
	pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
	if (new_irq != irq) {
		pci_info(dev, "VIA VLink IRQ fixup, from %d to %d\n",
			irq, new_irq);
		udelay(15);	/* unknown if delay really needed */
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, new_irq);
	}
}
DECLARE_PCI_FIXUP_ENABLE(PCI_VENDOR_ID_VIA, PCI_ANY_ID, quirk_via_vlink);

/*
 * VIA VT82C598 has its device ID settable and many BIOSes set it to the ID
 * of VT82C597 for backward compatibility.  We need to switch it off to be
 * able to recognize the real type of the chip.
 */
static void quirk_vt82c598_id(struct pci_dev *dev)
{
	pci_write_config_byte(dev, 0xfc, 0);
	pci_read_config_word(dev, PCI_DEVICE_ID, &dev->device);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C597_0,	quirk_vt82c598_id);

/*
 * CardBus controllers have a legacy base address that enables them to
 * respond as i82365 pcmcia controllers.  We don't want them to do this
 * even if the Linux CardBus driver is not loaded, because the Linux i82365
 * driver does not (and should not) handle CardBus.
 */
static void quirk_cardbus_legacy(struct pci_dev *dev)
{
	pci_write_config_dword(dev, PCI_CB_LEGACY_MODE_BASE, 0);
}
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_ANY_ID, PCI_ANY_ID,
			PCI_CLASS_BRIDGE_CARDBUS, 8, quirk_cardbus_legacy);
DECLARE_PCI_FIXUP_CLASS_RESUME_EARLY(PCI_ANY_ID, PCI_ANY_ID,
			PCI_CLASS_BRIDGE_CARDBUS, 8, quirk_cardbus_legacy);

/*
 * Following the PCI ordering rules is optional on the AMD762. I'm not sure
 * what the designers were smoking but let's not inhale...
 *
 * To be fair to AMD, it follows the spec by default, it's BIOS people who
 * turn it off!
 */
static void quirk_amd_ordering(struct pci_dev *dev)
{
	u32 pcic;
	pci_read_config_dword(dev, 0x4C, &pcic);
	if ((pcic & 6) != 6) {
		pcic |= 6;
		pci_warn(dev, "BIOS failed to enable PCI standards compliance; fixing this error\n");
		pci_write_config_dword(dev, 0x4C, pcic);
		pci_read_config_dword(dev, 0x84, &pcic);
		pcic |= (1 << 23);	/* Required in this mode */
		pci_write_config_dword(dev, 0x84, pcic);
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_FE_GATE_700C, quirk_amd_ordering);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_FE_GATE_700C, quirk_amd_ordering);

/*
 * DreamWorks-provided workaround for Dunord I-3000 problem
 *
 * This card decodes and responds to addresses not apparently assigned to
 * it.  We force a larger allocation to ensure that nothing gets put too
 * close to it.
 */
static void quirk_dunord(struct pci_dev *dev)
{
	struct resource *r = &dev->resource[1];

	r->flags |= IORESOURCE_UNSET;
	resource_set_range(r, 0, SZ_16M);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_DUNORD,	PCI_DEVICE_ID_DUNORD_I3000,	quirk_dunord);

/*
 * i82380FB mobile docking controller: its PCI-to-PCI bridge is subtractive
 * decoding (transparent), and does indicate this in the ProgIf.
 * Unfortunately, the ProgIf value is wrong - 0x80 instead of 0x01.
 */
static void quirk_transparent_bridge(struct pci_dev *dev)
{
	dev->transparent = 1;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82380FB,	quirk_transparent_bridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_TOSHIBA,	0x605,	quirk_transparent_bridge);

/*
 * Common misconfiguration of the MediaGX/Geode PCI master that will reduce
 * PCI bandwidth from 70MB/s to 25MB/s.  See the GXM/GXLV/GX1 datasheets
 * found at http://www.national.com/analog for info on what these bits do.
 * <christer@weinigel.se>
 */
static void quirk_mediagx_master(struct pci_dev *dev)
{
	u8 reg;

	pci_read_config_byte(dev, 0x41, &reg);
	if (reg & 2) {
		reg &= ~2;
		pci_info(dev, "Fixup for MediaGX/Geode Slave Disconnect Boundary (0x41=0x%02x)\n",
			 reg);
		pci_write_config_byte(dev, 0x41, reg);
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_CYRIX,	PCI_DEVICE_ID_CYRIX_PCI_MASTER, quirk_mediagx_master);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_CYRIX,	PCI_DEVICE_ID_CYRIX_PCI_MASTER, quirk_mediagx_master);

/*
 * Ensure C0 rev restreaming is off. This is normally done by the BIOS but
 * in the odd case it is not the results are corruption hence the presence
 * of a Linux check.
 */
static void quirk_disable_pxb(struct pci_dev *pdev)
{
	u16 config;

	if (pdev->revision != 0x04)		/* Only C0 requires this */
		return;
	pci_read_config_word(pdev, 0x40, &config);
	if (config & (1<<6)) {
		config &= ~(1<<6);
		pci_write_config_word(pdev, 0x40, config);
		pci_info(pdev, "C0 revision 450NX. Disabling PCI restreaming\n");
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82454NX,	quirk_disable_pxb);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82454NX,	quirk_disable_pxb);

static void quirk_amd_ide_mode(struct pci_dev *pdev)
{
	/* set SBX00/Hudson-2 SATA in IDE mode to AHCI mode */
	u8 tmp;

	pci_read_config_byte(pdev, PCI_CLASS_DEVICE, &tmp);
	if (tmp == 0x01) {
		pci_read_config_byte(pdev, 0x40, &tmp);
		pci_write_config_byte(pdev, 0x40, tmp|1);
		pci_write_config_byte(pdev, 0x9, 1);
		pci_write_config_byte(pdev, 0xa, 6);
		pci_write_config_byte(pdev, 0x40, tmp);

		pdev->class = PCI_CLASS_STORAGE_SATA_AHCI;
		pci_info(pdev, "set SATA to AHCI mode\n");
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_IXP600_SATA, quirk_amd_ide_mode);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_IXP600_SATA, quirk_amd_ide_mode);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_IXP700_SATA, quirk_amd_ide_mode);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_IXP700_SATA, quirk_amd_ide_mode);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_HUDSON2_SATA_IDE, quirk_amd_ide_mode);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_HUDSON2_SATA_IDE, quirk_amd_ide_mode);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AMD, 0x7900, quirk_amd_ide_mode);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_AMD, 0x7900, quirk_amd_ide_mode);

/* Serverworks CSB5 IDE does not fully support native mode */
static void quirk_svwks_csb5ide(struct pci_dev *pdev)
{
	u8 prog;
	pci_read_config_byte(pdev, PCI_CLASS_PROG, &prog);
	if (prog & 5) {
		prog &= ~5;
		pdev->class &= ~5;
		pci_write_config_byte(pdev, PCI_CLASS_PROG, prog);
		/* PCI layer will sort out resources */
	}
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_CSB5IDE, quirk_svwks_csb5ide);

/* Intel 82801CAM ICH3-M datasheet says IDE modes must be the same */
static void quirk_ide_samemode(struct pci_dev *pdev)
{
	u8 prog;

	pci_read_config_byte(pdev, PCI_CLASS_PROG, &prog);

	if (((prog & 1) && !(prog & 4)) || ((prog & 4) && !(prog & 1))) {
		pci_info(pdev, "IDE mode mismatch; forcing legacy mode\n");
		prog &= ~5;
		pdev->class &= ~5;
		pci_write_config_byte(pdev, PCI_CLASS_PROG, prog);
	}
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801CA_10, quirk_ide_samemode);

/* Some ATA devices break if put into D3 */
static void quirk_no_ata_d3(struct pci_dev *pdev)
{
	pdev->dev_flags |= PCI_DEV_FLAGS_NO_D3;
}
/* Quirk the legacy ATA devices only. The AHCI ones are ok */
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_SERVERWORKS, PCI_ANY_ID,
				PCI_CLASS_STORAGE_IDE, 8, quirk_no_ata_d3);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_ATI, PCI_ANY_ID,
				PCI_CLASS_STORAGE_IDE, 8, quirk_no_ata_d3);
/* ALi loses some register settings that we cannot then restore */
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_AL, PCI_ANY_ID,
				PCI_CLASS_STORAGE_IDE, 8, quirk_no_ata_d3);
/* VIA comes back fine but we need to keep it alive or ACPI GTM failures
   occur when mode detecting */
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_VIA, PCI_ANY_ID,
				PCI_CLASS_STORAGE_IDE, 8, quirk_no_ata_d3);

/*
 * This was originally an Alpha-specific thing, but it really fits here.
 * The i82375 PCI/EISA bridge appears as non-classified. Fix that.
 */
static void quirk_eisa_bridge(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_EISA << 8;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82375,	quirk_eisa_bridge);

/*
 * On ASUS P4B boards, the SMBus PCI Device within the ICH2/4 southbridge
 * is not activated. The myth is that Asus said that they do not want the
 * users to be irritated by just another PCI Device in the Win98 device
 * manager. (see the file prog/hotplug/README.p4b in the lm_sensors
 * package 2.7.0 for details)
 *
 * The SMBus PCI Device can be activated by setting a bit in the ICH LPC
 * bridge. Unfortunately, this device has no subvendor/subdevice ID. So it
 * becomes necessary to do this tweak in two steps -- the chosen trigger
 * is either the Host bridge (preferred) or on-board VGA controller.
 *
 * Note that we used to unhide the SMBus that way on Toshiba laptops
 * (Satellite A40 and Tecra M2) but then found that the thermal management
 * was done by SMM code, which could cause unsynchronized concurrent
 * accesses to the SMBus registers, with potentially bad effects. Thus you
 * should be very careful when adding new entries: if SMM is accessing the
 * Intel SMBus, this is a very good reason to leave it hidden.
 *
 * Likewise, many recent laptops use ACPI for thermal management. If the
 * ACPI DSDT code accesses the SMBus, then Linux should not access it
 * natively, and keeping the SMBus hidden is the right thing to do. If you
 * are about to add an entry in the table below, please first disassemble
 * the DSDT and double-check that there is no code accessing the SMBus.
 */
static int asus_hides_smbus;

static void asus_hides_smbus_hostbridge(struct pci_dev *dev)
{
	if (unlikely(dev->subsystem_vendor == PCI_VENDOR_ID_ASUSTEK)) {
		if (dev->device == PCI_DEVICE_ID_INTEL_82845_HB)
			switch (dev->subsystem_device) {
			case 0x8025: /* P4B-LX */
			case 0x8070: /* P4B */
			case 0x8088: /* P4B533 */
			case 0x1626: /* L3C notebook */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82845G_HB)
			switch (dev->subsystem_device) {
			case 0x80b1: /* P4GE-V */
			case 0x80b2: /* P4PE */
			case 0x8093: /* P4B533-V */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82850_HB)
			switch (dev->subsystem_device) {
			case 0x8030: /* P4T533 */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_7205_0)
			switch (dev->subsystem_device) {
			case 0x8070: /* P4G8X Deluxe */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_E7501_MCH)
			switch (dev->subsystem_device) {
			case 0x80c9: /* PU-DLS */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82855GM_HB)
			switch (dev->subsystem_device) {
			case 0x1751: /* M2N notebook */
			case 0x1821: /* M5N notebook */
			case 0x1897: /* A6L notebook */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82855PM_HB)
			switch (dev->subsystem_device) {
			case 0x184b: /* W1N notebook */
			case 0x186a: /* M6Ne notebook */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82865_HB)
			switch (dev->subsystem_device) {
			case 0x80f2: /* P4P800-X */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82915GM_HB)
			switch (dev->subsystem_device) {
			case 0x1882: /* M6V notebook */
			case 0x1977: /* A6VA notebook */
				asus_hides_smbus = 1;
			}
	} else if (unlikely(dev->subsystem_vendor == PCI_VENDOR_ID_HP)) {
		if (dev->device ==  PCI_DEVICE_ID_INTEL_82855PM_HB)
			switch (dev->subsystem_device) {
			case 0x088C: /* HP Compaq nc8000 */
			case 0x0890: /* HP Compaq nc6000 */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82865_HB)
			switch (dev->subsystem_device) {
			case 0x12bc: /* HP D330L */
			case 0x12bd: /* HP D530 */
			case 0x006a: /* HP Compaq nx9500 */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82875_HB)
			switch (dev->subsystem_device) {
			case 0x12bf: /* HP xw4100 */
				asus_hides_smbus = 1;
			}
	} else if (unlikely(dev->subsystem_vendor == PCI_VENDOR_ID_SAMSUNG)) {
		if (dev->device ==  PCI_DEVICE_ID_INTEL_82855PM_HB)
			switch (dev->subsystem_device) {
			case 0xC00C: /* Samsung P35 notebook */
				asus_hides_smbus = 1;
		}
	} else if (unlikely(dev->subsystem_vendor == PCI_VENDOR_ID_COMPAQ)) {
		if (dev->device == PCI_DEVICE_ID_INTEL_82855PM_HB)
			switch (dev->subsystem_device) {
			case 0x0058: /* Compaq Evo N620c */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82810_IG3)
			switch (dev->subsystem_device) {
			case 0xB16C: /* Compaq Deskpro EP 401963-001 (PCA# 010174) */
				/* Motherboard doesn't have Host bridge
				 * subvendor/subdevice IDs, therefore checking
				 * its on-board VGA controller */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82801DB_2)
			switch (dev->subsystem_device) {
			case 0x00b8: /* Compaq Evo D510 CMT */
			case 0x00b9: /* Compaq Evo D510 SFF */
			case 0x00ba: /* Compaq Evo D510 USDT */
				/* Motherboard doesn't have Host bridge
				 * subvendor/subdevice IDs and on-board VGA
				 * controller is disabled if an AGP card is
				 * inserted, therefore checking USB UHCI
				 * Controller #1 */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82815_CGC)
			switch (dev->subsystem_device) {
			case 0x001A: /* Compaq Deskpro EN SSF P667 815E */
				/* Motherboard doesn't have host bridge
				 * subvendor/subdevice IDs, therefore checking
				 * its on-board VGA controller */
				asus_hides_smbus = 1;
			}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82845_HB,	asus_hides_smbus_hostbridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82845G_HB,	asus_hides_smbus_hostbridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82850_HB,	asus_hides_smbus_hostbridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82865_HB,	asus_hides_smbus_hostbridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82875_HB,	asus_hides_smbus_hostbridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_7205_0,	asus_hides_smbus_hostbridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_E7501_MCH,	asus_hides_smbus_hostbridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82855PM_HB,	asus_hides_smbus_hostbridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82855GM_HB,	asus_hides_smbus_hostbridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82915GM_HB, asus_hides_smbus_hostbridge);

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82810_IG3,	asus_hides_smbus_hostbridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801DB_2,	asus_hides_smbus_hostbridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82815_CGC,	asus_hides_smbus_hostbridge);

static void asus_hides_smbus_lpc(struct pci_dev *dev)
{
	u16 val;

	if (likely(!asus_hides_smbus))
		return;

	pci_read_config_word(dev, 0xF2, &val);
	if (val & 0x8) {
		pci_write_config_word(dev, 0xF2, val & (~0x8));
		pci_read_config_word(dev, 0xF2, &val);
		if (val & 0x8)
			pci_info(dev, "i801 SMBus device continues to play 'hide and seek'! 0x%x\n",
				 val);
		else
			pci_info(dev, "Enabled i801 SMBus device\n");
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801AA_0,	asus_hides_smbus_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801DB_0,	asus_hides_smbus_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801BA_0,	asus_hides_smbus_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801CA_0,	asus_hides_smbus_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801CA_12,	asus_hides_smbus_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801DB_12,	asus_hides_smbus_lpc);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801EB_0,	asus_hides_smbus_lpc);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801AA_0,	asus_hides_smbus_lpc);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801DB_0,	asus_hides_smbus_lpc);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801BA_0,	asus_hides_smbus_lpc);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801CA_0,	asus_hides_smbus_lpc);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801CA_12,	asus_hides_smbus_lpc);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801DB_12,	asus_hides_smbus_lpc);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801EB_0,	asus_hides_smbus_lpc);

/* It appears we just have one such device. If not, we have a warning */
static void __iomem *asus_rcba_base;
static void asus_hides_smbus_lpc_ich6_suspend(struct pci_dev *dev)
{
	u32 rcba;

	if (likely(!asus_hides_smbus))
		return;
	WARN_ON(asus_rcba_base);

	pci_read_config_dword(dev, 0xF0, &rcba);
	/* use bits 31:14, 16 kB aligned */
	asus_rcba_base = ioremap(rcba & 0xFFFFC000, 0x4000);
	if (asus_rcba_base == NULL)
		return;
}

static void asus_hides_smbus_lpc_ich6_resume_early(struct pci_dev *dev)
{
	u32 val;

	if (likely(!asus_hides_smbus || !asus_rcba_base))
		return;

	/* read the Function Disable register, dword mode only */
	val = readl(asus_rcba_base + 0x3418);

	/* enable the SMBus device */
	writel(val & 0xFFFFFFF7, asus_rcba_base + 0x3418);
}

static void asus_hides_smbus_lpc_ich6_resume(struct pci_dev *dev)
{
	if (likely(!asus_hides_smbus || !asus_rcba_base))
		return;

	iounmap(asus_rcba_base);
	asus_rcba_base = NULL;
	pci_info(dev, "Enabled ICH6/i801 SMBus device\n");
}

static void asus_hides_smbus_lpc_ich6(struct pci_dev *dev)
{
	asus_hides_smbus_lpc_ich6_suspend(dev);
	asus_hides_smbus_lpc_ich6_resume_early(dev);
	asus_hides_smbus_lpc_ich6_resume(dev);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH6_1,	asus_hides_smbus_lpc_ich6);
DECLARE_PCI_FIXUP_SUSPEND(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH6_1,	asus_hides_smbus_lpc_ich6_suspend);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH6_1,	asus_hides_smbus_lpc_ich6_resume);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH6_1,	asus_hides_smbus_lpc_ich6_resume_early);

/* SiS 96x south bridge: BIOS typically hides SMBus device...  */
static void quirk_sis_96x_smbus(struct pci_dev *dev)
{
	u8 val = 0;
	pci_read_config_byte(dev, 0x77, &val);
	if (val & 0x10) {
		pci_info(dev, "Enabling SiS 96x SMBus\n");
		pci_write_config_byte(dev, 0x77, val & ~0x10);
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_961,		quirk_sis_96x_smbus);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_962,		quirk_sis_96x_smbus);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_963,		quirk_sis_96x_smbus);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_LPC,		quirk_sis_96x_smbus);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_961,		quirk_sis_96x_smbus);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_962,		quirk_sis_96x_smbus);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_963,		quirk_sis_96x_smbus);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_LPC,		quirk_sis_96x_smbus);

/*
 * ... This is further complicated by the fact that some SiS96x south
 * bridges pretend to be 85C503/5513 instead.  In that case see if we
 * spotted a compatible north bridge to make sure.
 * (pci_find_device() doesn't work yet)
 *
 * We can also enable the sis96x bit in the discovery register..
 */
#define SIS_DETECT_REGISTER 0x40

static void quirk_sis_503(struct pci_dev *dev)
{
	u8 reg;
	u16 devid;

	pci_read_config_byte(dev, SIS_DETECT_REGISTER, &reg);
	pci_write_config_byte(dev, SIS_DETECT_REGISTER, reg | (1 << 6));
	pci_read_config_word(dev, PCI_DEVICE_ID, &devid);
	if (((devid & 0xfff0) != 0x0960) && (devid != 0x0018)) {
		pci_write_config_byte(dev, SIS_DETECT_REGISTER, reg);
		return;
	}

	/*
	 * Ok, it now shows up as a 96x.  Run the 96x quirk by hand in case
	 * it has already been processed.  (Depends on link order, which is
	 * apparently not guaranteed)
	 */
	dev->device = devid;
	quirk_sis_96x_smbus(dev);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_503,		quirk_sis_503);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_503,		quirk_sis_503);

/*
 * On ASUS A8V and A8V Deluxe boards, the onboard AC97 audio controller
 * and MC97 modem controller are disabled when a second PCI soundcard is
 * present. This patch, tweaking the VT8237 ISA bridge, enables them.
 * -- bjd
 */
static void asus_hides_ac97_lpc(struct pci_dev *dev)
{
	u8 val;
	int asus_hides_ac97 = 0;

	if (likely(dev->subsystem_vendor == PCI_VENDOR_ID_ASUSTEK)) {
		if (dev->device == PCI_DEVICE_ID_VIA_8237)
			asus_hides_ac97 = 1;
	}

	if (!asus_hides_ac97)
		return;

	pci_read_config_byte(dev, 0x50, &val);
	if (val & 0xc0) {
		pci_write_config_byte(dev, 0x50, val & (~0xc0));
		pci_read_config_byte(dev, 0x50, &val);
		if (val & 0xc0)
			pci_info(dev, "Onboard AC97/MC97 devices continue to play 'hide and seek'! 0x%x\n",
				 val);
		else
			pci_info(dev, "Enabled onboard AC97/MC97 devices\n");
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8237, asus_hides_ac97_lpc);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8237, asus_hides_ac97_lpc);

#if defined(CONFIG_ATA) || defined(CONFIG_ATA_MODULE)

/*
 * If we are using libata we can drive this chip properly but must do this
 * early on to make the additional device appear during the PCI scanning.
 */
static void quirk_jmicron_ata(struct pci_dev *pdev)
{
	u32 conf1, conf5, class;
	u8 hdr;

	/* Only poke fn 0 */
	if (PCI_FUNC(pdev->devfn))
		return;

	pci_read_config_dword(pdev, 0x40, &conf1);
	pci_read_config_dword(pdev, 0x80, &conf5);

	conf1 &= ~0x00CFF302; /* Clear bit 1, 8, 9, 12-19, 22, 23 */
	conf5 &= ~(1 << 24);  /* Clear bit 24 */

	switch (pdev->device) {
	case PCI_DEVICE_ID_JMICRON_JMB360: /* SATA single port */
	case PCI_DEVICE_ID_JMICRON_JMB362: /* SATA dual ports */
	case PCI_DEVICE_ID_JMICRON_JMB364: /* SATA dual ports */
		/* The controller should be in single function ahci mode */
		conf1 |= 0x0002A100; /* Set 8, 13, 15, 17 */
		break;

	case PCI_DEVICE_ID_JMICRON_JMB365:
	case PCI_DEVICE_ID_JMICRON_JMB366:
		/* Redirect IDE second PATA port to the right spot */
		conf5 |= (1 << 24);
		fallthrough;
	case PCI_DEVICE_ID_JMICRON_JMB361:
	case PCI_DEVICE_ID_JMICRON_JMB363:
	case PCI_DEVICE_ID_JMICRON_JMB369:
		/* Enable dual function mode, AHCI on fn 0, IDE fn1 */
		/* Set the class codes correctly and then direct IDE 0 */
		conf1 |= 0x00C2A1B3; /* Set 0, 1, 4, 5, 7, 8, 13, 15, 17, 22, 23 */
		break;

	case PCI_DEVICE_ID_JMICRON_JMB368:
		/* The controller should be in single function IDE mode */
		conf1 |= 0x00C00000; /* Set 22, 23 */
		break;
	}

	pci_write_config_dword(pdev, 0x40, conf1);
	pci_write_config_dword(pdev, 0x80, conf5);

	/* Update pdev accordingly */
	pci_read_config_byte(pdev, PCI_HEADER_TYPE, &hdr);
	pdev->hdr_type = hdr & PCI_HEADER_TYPE_MASK;
	pdev->multifunction = FIELD_GET(PCI_HEADER_TYPE_MFD, hdr);

	pci_read_config_dword(pdev, PCI_CLASS_REVISION, &class);
	pdev->class = class >> 8;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB360, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB361, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB362, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB363, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB364, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB365, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB366, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB368, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB369, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB360, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB361, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB362, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB363, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB364, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB365, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB366, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB368, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB369, quirk_jmicron_ata);

#endif

static void quirk_jmicron_async_suspend(struct pci_dev *dev)
{
	if (dev->multifunction) {
		device_disable_async_suspend(&dev->dev);
		pci_info(dev, "async suspend disabled to avoid multi-function power-on ordering issue\n");
	}
}
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_JMICRON, PCI_ANY_ID, PCI_CLASS_STORAGE_IDE, 8, quirk_jmicron_async_suspend);
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_JMICRON, PCI_ANY_ID, PCI_CLASS_STORAGE_SATA_AHCI, 0, quirk_jmicron_async_suspend);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_JMICRON, 0x2362, quirk_jmicron_async_suspend);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_JMICRON, 0x236f, quirk_jmicron_async_suspend);

#ifdef CONFIG_X86_IO_APIC
static void quirk_alder_ioapic(struct pci_dev *pdev)
{
	int i;

	if ((pdev->class >> 8) != 0xff00)
		return;

	/*
	 * The first BAR is the location of the IO-APIC... we must
	 * not touch this (and it's already covered by the fixmap), so
	 * forcibly insert it into the resource tree.
	 */
	if (pci_resource_start(pdev, 0) && pci_resource_len(pdev, 0))
		insert_resource(&iomem_resource, &pdev->resource[0]);

	/*
	 * The next five BARs all seem to be rubbish, so just clean
	 * them out.
	 */
	for (i = 1; i < PCI_STD_NUM_BARS; i++)
		memset(&pdev->resource[i], 0, sizeof(pdev->resource[i]));
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_EESSC,	quirk_alder_ioapic);
#endif

static void quirk_no_msi(struct pci_dev *dev)
{
	pci_info(dev, "avoiding MSI to work around a hardware defect\n");
	dev->no_msi = 1;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x4386, quirk_no_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x4387, quirk_no_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x4388, quirk_no_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x4389, quirk_no_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x438a, quirk_no_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x438b, quirk_no_msi);

static void quirk_pcie_mch(struct pci_dev *pdev)
{
	pdev->no_msi = 1;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_E7520_MCH,	quirk_pcie_mch);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_E7320_MCH,	quirk_pcie_mch);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_E7525_MCH,	quirk_pcie_mch);

DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_HUAWEI, 0x1610, PCI_CLASS_BRIDGE_PCI, 8, quirk_pcie_mch);

/*
 * HiSilicon KunPeng920 and KunPeng930 have devices appear as PCI but are
 * actually on the AMBA bus. These fake PCI devices can support SVA via
 * SMMU stall feature, by setting dma-can-stall for ACPI platforms.
 *
 * Normally stalling must not be enabled for PCI devices, since it would
 * break the PCI requirement for free-flowing writes and may lead to
 * deadlock.  We expect PCI devices to support ATS and PRI if they want to
 * be fault-tolerant, so there's no ACPI binding to describe anything else,
 * even when a "PCI" device turns out to be a regular old SoC device
 * dressed up as a RCiEP and normal rules don't apply.
 */
static void quirk_huawei_pcie_sva(struct pci_dev *pdev)
{
	struct property_entry properties[] = {
		PROPERTY_ENTRY_BOOL("dma-can-stall"),
		{},
	};

	if (pdev->revision != 0x21 && pdev->revision != 0x30)
		return;

	pdev->pasid_no_tlp = 1;

	/*
	 * Set the dma-can-stall property on ACPI platforms. Device tree
	 * can set it directly.
	 */
	if (!pdev->dev.of_node &&
	    device_create_managed_software_node(&pdev->dev, properties, NULL))
		pci_warn(pdev, "could not add stall property");
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_HUAWEI, 0xa250, quirk_huawei_pcie_sva);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_HUAWEI, 0xa251, quirk_huawei_pcie_sva);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_HUAWEI, 0xa255, quirk_huawei_pcie_sva);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_HUAWEI, 0xa256, quirk_huawei_pcie_sva);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_HUAWEI, 0xa258, quirk_huawei_pcie_sva);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_HUAWEI, 0xa259, quirk_huawei_pcie_sva);

/*
 * It's possible for the MSI to get corrupted if SHPC and ACPI are used
 * together on certain PXH-based systems.
 */
static void quirk_pcie_pxh(struct pci_dev *dev)
{
	dev->no_msi = 1;
	pci_warn(dev, "PXH quirk detected; SHPC device MSI disabled\n");
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_PXHD_0,	quirk_pcie_pxh);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_PXHD_1,	quirk_pcie_pxh);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_PXH_0,	quirk_pcie_pxh);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_PXH_1,	quirk_pcie_pxh);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_PXHV,	quirk_pcie_pxh);

/*
 * Some Intel PCI Express chipsets have trouble with downstream device
 * power management.
 */
static void quirk_intel_pcie_pm(struct pci_dev *dev)
{
	pci_pm_d3hot_delay = 120;
	dev->no_d1d2 = 1;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25e2, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25e3, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25e4, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25e5, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25e6, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25e7, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25f7, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25f8, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25f9, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25fa, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2601, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2602, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2603, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2604, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2605, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2606, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2607, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2608, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2609, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x260a, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x260b, quirk_intel_pcie_pm);

static void quirk_d3hot_delay(struct pci_dev *dev, unsigned int delay)
{
	if (dev->d3hot_delay >= delay)
		return;

	dev->d3hot_delay = delay;
	pci_info(dev, "extending delay after power-on from D3hot to %d msec\n",
		 dev->d3hot_delay);
}

static void quirk_radeon_pm(struct pci_dev *dev)
{
	if (dev->subsystem_vendor == PCI_VENDOR_ID_APPLE &&
	    dev->subsystem_device == 0x00e2)
		quirk_d3hot_delay(dev, 20);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x6741, quirk_radeon_pm);

/*
 * NVIDIA Ampere-based HDA controllers can wedge the whole device if a bus
 * reset is performed too soon after transition to D0, extend d3hot_delay
 * to previous effective default for all NVIDIA HDA controllers.
 */
static void quirk_nvidia_hda_pm(struct pci_dev *dev)
{
	quirk_d3hot_delay(dev, 20);
}
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID,
			      PCI_CLASS_MULTIMEDIA_HD_AUDIO, 8,
			      quirk_nvidia_hda_pm);

/*
 * Ryzen5/7 XHCI controllers fail upon resume from runtime suspend or s2idle.
 * https://bugzilla.kernel.org/show_bug.cgi?id=205587
 *
 * The kernel attempts to transition these devices to D3cold, but that seems
 * to be ineffective on the platforms in question; the PCI device appears to
 * remain on in D3hot state. The D3hot-to-D0 transition then requires an
 * extended delay in order to succeed.
 */
static void quirk_ryzen_xhci_d3hot(struct pci_dev *dev)
{
	quirk_d3hot_delay(dev, 20);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, 0x15e0, quirk_ryzen_xhci_d3hot);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, 0x15e1, quirk_ryzen_xhci_d3hot);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, 0x1639, quirk_ryzen_xhci_d3hot);

#ifdef CONFIG_X86_IO_APIC
static int dmi_disable_ioapicreroute(const struct dmi_system_id *d)
{
	noioapicreroute = 1;
	pr_info("%s detected: disable boot interrupt reroute\n", d->ident);

	return 0;
}

static const struct dmi_system_id boot_interrupt_dmi_table[] = {
	/*
	 * Systems to exclude from boot interrupt reroute quirks
	 */
	{
		.callback = dmi_disable_ioapicreroute,
		.ident = "ASUSTek Computer INC. M2N-LR",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTek Computer INC."),
			DMI_MATCH(DMI_PRODUCT_NAME, "M2N-LR"),
		},
	},
	{}
};

/*
 * Boot interrupts on some chipsets cannot be turned off. For these chipsets,
 * remap the original interrupt in the Linux kernel to the boot interrupt, so
 * that a PCI device's interrupt handler is installed on the boot interrupt
 * line instead.
 */
static void quirk_reroute_to_boot_interrupts_intel(struct pci_dev *dev)
{
	dmi_check_system(boot_interrupt_dmi_table);
	if (noioapicquirk || noioapicreroute)
		return;

	dev->irq_reroute_variant = INTEL_IRQ_REROUTE_VARIANT;
	pci_info(dev, "rerouting interrupts for [%04x:%04x]\n",
		 dev->vendor, dev->device);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_80333_0,	quirk_reroute_to_boot_interrupts_intel);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_80333_1,	quirk_reroute_to_boot_interrupts_intel);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ESB2_0,	quirk_reroute_to_boot_interrupts_intel);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_PXH_0,	quirk_reroute_to_boot_interrupts_intel);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_PXH_1,	quirk_reroute_to_boot_interrupts_intel);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_PXHV,	quirk_reroute_to_boot_interrupts_intel);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_80332_0,	quirk_reroute_to_boot_interrupts_intel);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_80332_1,	quirk_reroute_to_boot_interrupts_intel);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_80333_0,	quirk_reroute_to_boot_interrupts_intel);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_80333_1,	quirk_reroute_to_boot_interrupts_intel);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ESB2_0,	quirk_reroute_to_boot_interrupts_intel);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_PXH_0,	quirk_reroute_to_boot_interrupts_intel);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_PXH_1,	quirk_reroute_to_boot_interrupts_intel);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_PXHV,	quirk_reroute_to_boot_interrupts_intel);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_80332_0,	quirk_reroute_to_boot_interrupts_intel);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_80332_1,	quirk_reroute_to_boot_interrupts_intel);

/*
 * On some chipsets we can disable the generation of legacy INTx boot
 * interrupts.
 */

/*
 * IO-APIC1 on 6300ESB generates boot interrupts, see Intel order no
 * 300641-004US, section 5.7.3.
 *
 * Core IO on Xeon E5 1600/2600/4600, see Intel order no 326509-003.
 * Core IO on Xeon E5 v2, see Intel order no 329188-003.
 * Core IO on Xeon E7 v2, see Intel order no 329595-002.
 * Core IO on Xeon E5 v3, see Intel order no 330784-003.
 * Core IO on Xeon E7 v3, see Intel order no 332315-001US.
 * Core IO on Xeon E5 v4, see Intel order no 333810-002US.
 * Core IO on Xeon E7 v4, see Intel order no 332315-001US.
 * Core IO on Xeon D-1500, see Intel order no 332051-001.
 * Core IO on Xeon Scalable, see Intel order no 610950.
 */
#define INTEL_6300_IOAPIC_ABAR		0x40	/* Bus 0, Dev 29, Func 5 */
#define INTEL_6300_DISABLE_BOOT_IRQ	(1<<14)

#define INTEL_CIPINTRC_CFG_OFFSET	0x14C	/* Bus 0, Dev 5, Func 0 */
#define INTEL_CIPINTRC_DIS_INTX_ICH	(1<<25)

static void quirk_disable_intel_boot_interrupt(struct pci_dev *dev)
{
	u16 pci_config_word;
	u32 pci_config_dword;

	if (noioapicquirk)
		return;

	switch (dev->device) {
	case PCI_DEVICE_ID_INTEL_ESB_10:
		pci_read_config_word(dev, INTEL_6300_IOAPIC_ABAR,
				     &pci_config_word);
		pci_config_word |= INTEL_6300_DISABLE_BOOT_IRQ;
		pci_write_config_word(dev, INTEL_6300_IOAPIC_ABAR,
				      pci_config_word);
		break;
	case 0x3c28:	/* Xeon E5 1600/2600/4600	*/
	case 0x0e28:	/* Xeon E5/E7 V2		*/
	case 0x2f28:	/* Xeon E5/E7 V3,V4		*/
	case 0x6f28:	/* Xeon D-1500			*/
	case 0x2034:	/* Xeon Scalable Family		*/
		pci_read_config_dword(dev, INTEL_CIPINTRC_CFG_OFFSET,
				      &pci_config_dword);
		pci_config_dword |= INTEL_CIPINTRC_DIS_INTX_ICH;
		pci_write_config_dword(dev, INTEL_CIPINTRC_CFG_OFFSET,
				       pci_config_dword);
		break;
	default:
		return;
	}
	pci_info(dev, "disabled boot interrupts on device [%04x:%04x]\n",
		 dev->vendor, dev->device);
}
/*
 * Device 29 Func 5 Device IDs of IO-APIC
 * containing ABAR—APIC1 Alternate Base Address Register
 */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ESB_10,
		quirk_disable_intel_boot_interrupt);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ESB_10,
		quirk_disable_intel_boot_interrupt);

/*
 * Device 5 Func 0 Device IDs of Core IO modules/hubs
 * containing Coherent Interface Protocol Interrupt Control
 *
 * Device IDs obtained from volume 2 datasheets of commented
 * families above.
 */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x3c28,
		quirk_disable_intel_boot_interrupt);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x0e28,
		quirk_disable_intel_boot_interrupt);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2f28,
		quirk_disable_intel_boot_interrupt);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x6f28,
		quirk_disable_intel_boot_interrupt);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2034,
		quirk_disable_intel_boot_interrupt);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	0x3c28,
		quirk_disable_intel_boot_interrupt);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	0x0e28,
		quirk_disable_intel_boot_interrupt);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	0x2f28,
		quirk_disable_intel_boot_interrupt);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	0x6f28,
		quirk_disable_intel_boot_interrupt);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	0x2034,
		quirk_disable_intel_boot_interrupt);

/* Disable boot interrupts on HT-1000 */
#define BC_HT1000_FEATURE_REG		0x64
#define BC_HT1000_PIC_REGS_ENABLE	(1<<0)
#define BC_HT1000_MAP_IDX		0xC00
#define BC_HT1000_MAP_DATA		0xC01

static void quirk_disable_broadcom_boot_interrupt(struct pci_dev *dev)
{
	u32 pci_config_dword;
	u8 irq;

	if (noioapicquirk)
		return;

	pci_read_config_dword(dev, BC_HT1000_FEATURE_REG, &pci_config_dword);
	pci_write_config_dword(dev, BC_HT1000_FEATURE_REG, pci_config_dword |
			BC_HT1000_PIC_REGS_ENABLE);

	for (irq = 0x10; irq < 0x10 + 32; irq++) {
		outb(irq, BC_HT1000_MAP_IDX);
		outb(0x00, BC_HT1000_MAP_DATA);
	}

	pci_write_config_dword(dev, BC_HT1000_FEATURE_REG, pci_config_dword);

	pci_info(dev, "disabled boot interrupts on device [%04x:%04x]\n",
		 dev->vendor, dev->device);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_SERVERWORKS,   PCI_DEVICE_ID_SERVERWORKS_HT1000SB,	quirk_disable_broadcom_boot_interrupt);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_SERVERWORKS,   PCI_DEVICE_ID_SERVERWORKS_HT1000SB,	quirk_disable_broadcom_boot_interrupt);

/* Disable boot interrupts on AMD and ATI chipsets */

/*
 * NOIOAMODE needs to be disabled to disable "boot interrupts". For AMD 8131
 * rev. A0 and B0, NOIOAMODE needs to be disabled anyway to fix IO-APIC mode
 * (due to an erratum).
 */
#define AMD_813X_MISC			0x40
#define AMD_813X_NOIOAMODE		(1<<0)
#define AMD_813X_REV_B1			0x12
#define AMD_813X_REV_B2			0x13

static void quirk_disable_amd_813x_boot_interrupt(struct pci_dev *dev)
{
	u32 pci_config_dword;

	if (noioapicquirk)
		return;
	if ((dev->revision == AMD_813X_REV_B1) ||
	    (dev->revision == AMD_813X_REV_B2))
		return;

	pci_read_config_dword(dev, AMD_813X_MISC, &pci_config_dword);
	pci_config_dword &= ~AMD_813X_NOIOAMODE;
	pci_write_config_dword(dev, AMD_813X_MISC, pci_config_dword);

	pci_info(dev, "disabled boot interrupts on device [%04x:%04x]\n",
		 dev->vendor, dev->device);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_8131_BRIDGE,	quirk_disable_amd_813x_boot_interrupt);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_8131_BRIDGE,	quirk_disable_amd_813x_boot_interrupt);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_8132_BRIDGE,	quirk_disable_amd_813x_boot_interrupt);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_8132_BRIDGE,	quirk_disable_amd_813x_boot_interrupt);

#define AMD_8111_PCI_IRQ_ROUTING	0x56

static void quirk_disable_amd_8111_boot_interrupt(struct pci_dev *dev)
{
	u16 pci_config_word;

	if (noioapicquirk)
		return;

	pci_read_config_word(dev, AMD_8111_PCI_IRQ_ROUTING, &pci_config_word);
	if (!pci_config_word) {
		pci_info(dev, "boot interrupts on device [%04x:%04x] already disabled\n",
			 dev->vendor, dev->device);
		return;
	}
	pci_write_config_word(dev, AMD_8111_PCI_IRQ_ROUTING, 0);
	pci_info(dev, "disabled boot interrupts on device [%04x:%04x]\n",
		 dev->vendor, dev->device);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD,   PCI_DEVICE_ID_AMD_8111_SMBUS,	quirk_disable_amd_8111_boot_interrupt);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_AMD,   PCI_DEVICE_ID_AMD_8111_SMBUS,	quirk_disable_amd_8111_boot_interrupt);
#endif /* CONFIG_X86_IO_APIC */

/*
 * Toshiba TC86C001 IDE controller reports the standard 8-byte BAR0 size
 * but the PIO transfers won't work if BAR0 falls at the odd 8 bytes.
 * Re-allocate the region if needed...
 */
static void quirk_tc86c001_ide(struct pci_dev *dev)
{
	struct resource *r = &dev->resource[0];

	if (r->start & 0x8) {
		r->flags |= IORESOURCE_UNSET;
		resource_set_range(r, 0, SZ_16);
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_TOSHIBA_2,
			 PCI_DEVICE_ID_TOSHIBA_TC86C001_IDE,
			 quirk_tc86c001_ide);

/*
 * PLX PCI 9050 PCI Target bridge controller has an erratum that prevents the
 * local configuration registers accessible via BAR0 (memory) or BAR1 (i/o)
 * being read correctly if bit 7 of the base address is set.
 * The BAR0 or BAR1 region may be disabled (size 0) or enabled (size 128).
 * Re-allocate the regions to a 256-byte boundary if necessary.
 */
static void quirk_plx_pci9050(struct pci_dev *dev)
{
	unsigned int bar;

	/* Fixed in revision 2 (PCI 9052). */
	if (dev->revision >= 2)
		return;
	for (bar = 0; bar <= 1; bar++)
		if (pci_resource_len(dev, bar) == 0x80 &&
		    (pci_resource_start(dev, bar) & 0x80)) {
			struct resource *r = &dev->resource[bar];
			pci_info(dev, "Re-allocating PLX PCI 9050 BAR %u to length 256 to avoid bit 7 bug\n",
				 bar);
			r->flags |= IORESOURCE_UNSET;
			resource_set_range(r, 0, SZ_256);
		}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
			 quirk_plx_pci9050);
/*
 * The following Meilhaus (vendor ID 0x1402) device IDs (amongst others)
 * may be using the PLX PCI 9050: 0x0630, 0x0940, 0x0950, 0x0960, 0x100b,
 * 0x1400, 0x140a, 0x140b, 0x14e0, 0x14ea, 0x14eb, 0x1604, 0x1608, 0x160c,
 * 0x168f, 0x2000, 0x2600, 0x3000, 0x810a, 0x810b.
 *
 * Currently, device IDs 0x2000 and 0x2600 are used by the Comedi "me_daq"
 * driver.
 */
DECLARE_PCI_FIXUP_HEADER(0x1402, 0x2000, quirk_plx_pci9050);
DECLARE_PCI_FIXUP_HEADER(0x1402, 0x2600, quirk_plx_pci9050);

static void quirk_netmos(struct pci_dev *dev)
{
	unsigned int num_parallel = (dev->subsystem_device & 0xf0) >> 4;
	unsigned int num_serial = dev->subsystem_device & 0xf;

	/*
	 * These Netmos parts are multiport serial devices with optional
	 * parallel ports.  Even when parallel ports are present, they
	 * are identified as class SERIAL, which means the serial driver
	 * will claim them.  To prevent this, mark them as class OTHER.
	 * These combo devices should be claimed by parport_serial.
	 *
	 * The subdevice ID is of the form 0x00PS, where <P> is the number
	 * of parallel ports and <S> is the number of serial ports.
	 */
	switch (dev->device) {
	case PCI_DEVICE_ID_NETMOS_9835:
		/* Well, this rule doesn't hold for the following 9835 device */
		if (dev->subsystem_vendor == PCI_VENDOR_ID_IBM &&
				dev->subsystem_device == 0x0299)
			return;
		fallthrough;
	case PCI_DEVICE_ID_NETMOS_9735:
	case PCI_DEVICE_ID_NETMOS_9745:
	case PCI_DEVICE_ID_NETMOS_9845:
	case PCI_DEVICE_ID_NETMOS_9855:
		if (num_parallel) {
			pci_info(dev, "Netmos %04x (%u parallel, %u serial); changing class SERIAL to OTHER (use parport_serial)\n",
				dev->device, num_parallel, num_serial);
			dev->class = (PCI_CLASS_COMMUNICATION_OTHER << 8) |
			    (dev->class & 0xff);
		}
	}
}
DECLARE_PCI_FIXUP_CLASS_HEADER(PCI_VENDOR_ID_NETMOS, PCI_ANY_ID,
			 PCI_CLASS_COMMUNICATION_SERIAL, 8, quirk_netmos);

static void quirk_e100_interrupt(struct pci_dev *dev)
{
	u16 command, pmcsr;
	u8 __iomem *csr;
	u8 cmd_hi;

	switch (dev->device) {
	/* PCI IDs taken from drivers/net/e100.c */
	case 0x1029:
	case 0x1030 ... 0x1034:
	case 0x1038 ... 0x103E:
	case 0x1050 ... 0x1057:
	case 0x1059:
	case 0x1064 ... 0x106B:
	case 0x1091 ... 0x1095:
	case 0x1209:
	case 0x1229:
	case 0x2449:
	case 0x2459:
	case 0x245D:
	case 0x27DC:
		break;
	default:
		return;
	}

	/*
	 * Some firmware hands off the e100 with interrupts enabled,
	 * which can cause a flood of interrupts if packets are
	 * received before the driver attaches to the device.  So
	 * disable all e100 interrupts here.  The driver will
	 * re-enable them when it's ready.
	 */
	pci_read_config_word(dev, PCI_COMMAND, &command);

	if (!(command & PCI_COMMAND_MEMORY) || !pci_resource_start(dev, 0))
		return;

	/*
	 * Check that the device is in the D0 power state. If it's not,
	 * there is no point to look any further.
	 */
	if (dev->pm_cap) {
		pci_read_config_word(dev, dev->pm_cap + PCI_PM_CTRL, &pmcsr);
		if ((pmcsr & PCI_PM_CTRL_STATE_MASK) != PCI_D0)
			return;
	}

	/* Convert from PCI bus to resource space.  */
	csr = ioremap(pci_resource_start(dev, 0), 8);
	if (!csr) {
		pci_warn(dev, "Can't map e100 registers\n");
		return;
	}

	cmd_hi = readb(csr + 3);
	if (cmd_hi == 0) {
		pci_warn(dev, "Firmware left e100 interrupts enabled; disabling\n");
		writeb(1, csr + 3);
	}

	iounmap(csr);
}
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_INTEL, PCI_ANY_ID,
			PCI_CLASS_NETWORK_ETHERNET, 8, quirk_e100_interrupt);

/*
 * The 82575 and 82598 may experience data corruption issues when transitioning
 * out of L0S.  To prevent this we need to disable L0S on the PCIe link.
 */
static void quirk_disable_aspm_l0s(struct pci_dev *dev)
{
	pci_info(dev, "Disabling L0s\n");
	pci_disable_link_state(dev, PCIE_LINK_STATE_L0S);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x10a7, quirk_disable_aspm_l0s);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x10a9, quirk_disable_aspm_l0s);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x10b6, quirk_disable_aspm_l0s);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x10c6, quirk_disable_aspm_l0s);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x10c7, quirk_disable_aspm_l0s);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x10c8, quirk_disable_aspm_l0s);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x10d6, quirk_disable_aspm_l0s);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x10db, quirk_disable_aspm_l0s);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x10dd, quirk_disable_aspm_l0s);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x10e1, quirk_disable_aspm_l0s);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x10ec, quirk_disable_aspm_l0s);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x10f1, quirk_disable_aspm_l0s);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x10f4, quirk_disable_aspm_l0s);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1508, quirk_disable_aspm_l0s);

static void quirk_disable_aspm_l0s_l1(struct pci_dev *dev)
{
	pci_info(dev, "Disabling ASPM L0s/L1\n");
	pci_disable_link_state(dev, PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1);
}

/*
 * ASM1083/1085 PCIe-PCI bridge devices cause AER timeout errors on the
 * upstream PCIe root port when ASPM is enabled. At least L0s mode is affected;
 * disable both L0s and L1 for now to be safe.
 */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ASMEDIA, 0x1080, quirk_disable_aspm_l0s_l1);

/*
 * Some Pericom PCIe-to-PCI bridges in reverse mode need the PCIe Retrain
 * Link bit cleared after starting the link retrain process to allow this
 * process to finish.
 *
 * Affected devices: PI7C9X110, PI7C9X111SL, PI7C9X130.  See also the
 * Pericom Errata Sheet PI7C9X111SLB_errata_rev1.2_102711.pdf.
 */
static void quirk_enable_clear_retrain_link(struct pci_dev *dev)
{
	dev->clear_retrain_link = 1;
	pci_info(dev, "Enable PCIe Retrain Link quirk\n");
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_PERICOM, 0xe110, quirk_enable_clear_retrain_link);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_PERICOM, 0xe111, quirk_enable_clear_retrain_link);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_PERICOM, 0xe130, quirk_enable_clear_retrain_link);

static void fixup_rev1_53c810(struct pci_dev *dev)
{
	u32 class = dev->class;

	/*
	 * rev 1 ncr53c810 chips don't set the class at all which means
	 * they don't get their resources remapped. Fix that here.
	 */
	if (class)
		return;

	dev->class = PCI_CLASS_STORAGE_SCSI << 8;
	pci_info(dev, "NCR 53c810 rev 1 PCI class overridden (%#08x -> %#08x)\n",
		 class, dev->class);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NCR, PCI_DEVICE_ID_NCR_53C810, fixup_rev1_53c810);

/* Enable 1k I/O space granularity on the Intel P64H2 */
static void quirk_p64h2_1k_io(struct pci_dev *dev)
{
	u16 en1k;

	pci_read_config_word(dev, 0x40, &en1k);

	if (en1k & 0x200) {
		pci_info(dev, "Enable I/O Space to 1KB granularity\n");
		dev->io_window_1k = 1;
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x1460, quirk_p64h2_1k_io);

/*
 * Under some circumstances, AER is not linked with extended capabilities.
 * Force it to be linked by setting the corresponding control bit in the
 * config space.
 */
static void quirk_nvidia_ck804_pcie_aer_ext_cap(struct pci_dev *dev)
{
	uint8_t b;

	if (pci_read_config_byte(dev, 0xf41, &b) == 0) {
		if (!(b & 0x20)) {
			pci_write_config_byte(dev, 0xf41, b | 0x20);
			pci_info(dev, "Linking AER extended capability\n");
		}
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NVIDIA,  PCI_DEVICE_ID_NVIDIA_CK804_PCIE,
			quirk_nvidia_ck804_pcie_aer_ext_cap);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_NVIDIA,  PCI_DEVICE_ID_NVIDIA_CK804_PCIE,
			quirk_nvidia_ck804_pcie_aer_ext_cap);

static void quirk_via_cx700_pci_parking_caching(struct pci_dev *dev)
{
	/*
	 * Disable PCI Bus Parking and PCI Master read caching on CX700
	 * which causes unspecified timing errors with a VT6212L on the PCI
	 * bus leading to USB2.0 packet loss.
	 *
	 * This quirk is only enabled if a second (on the external PCI bus)
	 * VT6212L is found -- the CX700 core itself also contains a USB
	 * host controller with the same PCI ID as the VT6212L.
	 */

	/* Count VT6212L instances */
	struct pci_dev *p = pci_get_device(PCI_VENDOR_ID_VIA,
		PCI_DEVICE_ID_VIA_8235_USB_2, NULL);
	uint8_t b;

	/*
	 * p should contain the first (internal) VT6212L -- see if we have
	 * an external one by searching again.
	 */
	p = pci_get_device(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8235_USB_2, p);
	if (!p)
		return;
	pci_dev_put(p);

	if (pci_read_config_byte(dev, 0x76, &b) == 0) {
		if (b & 0x40) {
			/* Turn off PCI Bus Parking */
			pci_write_config_byte(dev, 0x76, b ^ 0x40);

			pci_info(dev, "Disabling VIA CX700 PCI parking\n");
		}
	}

	if (pci_read_config_byte(dev, 0x72, &b) == 0) {
		if (b != 0) {
			/* Turn off PCI Master read caching */
			pci_write_config_byte(dev, 0x72, 0x0);

			/* Set PCI Master Bus time-out to "1x16 PCLK" */
			pci_write_config_byte(dev, 0x75, 0x1);

			/* Disable "Read FIFO Timer" */
			pci_write_config_byte(dev, 0x77, 0x0);

			pci_info(dev, "Disabling VIA CX700 PCI caching\n");
		}
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA, 0x324e, quirk_via_cx700_pci_parking_caching);

static void quirk_brcm_5719_limit_mrrs(struct pci_dev *dev)
{
	u32 rev;

	pci_read_config_dword(dev, 0xf4, &rev);

	/* Only CAP the MRRS if the device is a 5719 A0 */
	if (rev == 0x05719000) {
		int readrq = pcie_get_readrq(dev);
		if (readrq > 2048)
			pcie_set_readrq(dev, 2048);
	}
}
DECLARE_PCI_FIXUP_ENABLE(PCI_VENDOR_ID_BROADCOM,
			 PCI_DEVICE_ID_TIGON3_5719,
			 quirk_brcm_5719_limit_mrrs);

/*
 * Originally in EDAC sources for i82875P: Intel tells BIOS developers to
 * hide device 6 which configures the overflow device access containing the
 * DRBs - this is where we expose device 6.
 * http://www.x86-secret.com/articles/tweak/pat/patsecrets-2.htm
 */
static void quirk_unhide_mch_dev6(struct pci_dev *dev)
{
	u8 reg;

	if (pci_read_config_byte(dev, 0xF4, &reg) == 0 && !(reg & 0x02)) {
		pci_info(dev, "Enabling MCH 'Overflow' Device\n");
		pci_write_config_byte(dev, 0xF4, reg | 0x02);
	}
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82865_HB,
			quirk_unhide_mch_dev6);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82875_HB,
			quirk_unhide_mch_dev6);

#ifdef CONFIG_PCI_MSI
/*
 * Some chipsets do not support MSI. We cannot easily rely on setting
 * PCI_BUS_FLAGS_NO_MSI in its bus flags because there are actually some
 * other buses controlled by the chipset even if Linux is not aware of it.
 * Instead of setting the flag on all buses in the machine, simply disable
 * MSI globally.
 */
static void quirk_disable_all_msi(struct pci_dev *dev)
{
	pci_no_msi();
	pci_warn(dev, "MSI quirk detected; MSI disabled\n");
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_GCNB_LE, quirk_disable_all_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RS400_200, quirk_disable_all_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RS480, quirk_disable_all_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_VT3336, quirk_disable_all_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_VT3351, quirk_disable_all_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_VT3364, quirk_disable_all_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8380_0, quirk_disable_all_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_SI, 0x0761, quirk_disable_all_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_SAMSUNG, 0xa5e3, quirk_disable_all_msi);

/* Disable MSI on chipsets that are known to not support it */
static void quirk_disable_msi(struct pci_dev *dev)
{
	if (dev->subordinate) {
		pci_warn(dev, "MSI quirk detected; subordinate MSI disabled\n");
		dev->subordinate->bus_flags |= PCI_BUS_FLAGS_NO_MSI;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_8131_BRIDGE, quirk_disable_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA, 0xa238, quirk_disable_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x5a3f, quirk_disable_msi);

/*
 * The APC bridge device in AMD 780 family northbridges has some random
 * OEM subsystem ID in its vendor ID register (erratum 18), so instead
 * we use the possible vendor/device IDs of the host bridge for the
 * declared quirk, and search for the APC bridge by slot number.
 */
static void quirk_amd_780_apc_msi(struct pci_dev *host_bridge)
{
	struct pci_dev *apc_bridge;

	apc_bridge = pci_get_slot(host_bridge->bus, PCI_DEVFN(1, 0));
	if (apc_bridge) {
		if (apc_bridge->device == 0x9602)
			quirk_disable_msi(apc_bridge);
		pci_dev_put(apc_bridge);
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, 0x9600, quirk_amd_780_apc_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, 0x9601, quirk_amd_780_apc_msi);

/*
 * Go through the list of HyperTransport capabilities and return 1 if a HT
 * MSI capability is found and enabled.
 */
static int msi_ht_cap_enabled(struct pci_dev *dev)
{
	int pos, ttl = PCI_FIND_CAP_TTL;

	pos = pci_find_ht_capability(dev, HT_CAPTYPE_MSI_MAPPING);
	while (pos && ttl--) {
		u8 flags;

		if (pci_read_config_byte(dev, pos + HT_MSI_FLAGS,
					 &flags) == 0) {
			pci_info(dev, "Found %s HT MSI Mapping\n",
				flags & HT_MSI_FLAGS_ENABLE ?
				"enabled" : "disabled");
			return (flags & HT_MSI_FLAGS_ENABLE) != 0;
		}

		pos = pci_find_next_ht_capability(dev, pos,
						  HT_CAPTYPE_MSI_MAPPING);
	}
	return 0;
}

/* Check the HyperTransport MSI mapping to know whether MSI is enabled or not */
static void quirk_msi_ht_cap(struct pci_dev *dev)
{
	if (!msi_ht_cap_enabled(dev))
		quirk_disable_msi(dev);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_HT2000_PCIE,
			quirk_msi_ht_cap);

/*
 * The nVidia CK804 chipset may have 2 HT MSI mappings.  MSI is supported
 * if the MSI capability is set in any of these mappings.
 */
static void quirk_nvidia_ck804_msi_ht_cap(struct pci_dev *dev)
{
	struct pci_dev *pdev;

	/*
	 * Check HT MSI cap on this chipset and the root one.  A single one
	 * having MSI is enough to be sure that MSI is supported.
	 */
	pdev = pci_get_slot(dev->bus, 0);
	if (!pdev)
		return;
	if (!msi_ht_cap_enabled(pdev))
		quirk_msi_ht_cap(dev);
	pci_dev_put(pdev);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_CK804_PCIE,
			quirk_nvidia_ck804_msi_ht_cap);

/* Force enable MSI mapping capability on HT bridges */
static void ht_enable_msi_mapping(struct pci_dev *dev)
{
	int pos, ttl = PCI_FIND_CAP_TTL;

	pos = pci_find_ht_capability(dev, HT_CAPTYPE_MSI_MAPPING);
	while (pos && ttl--) {
		u8 flags;

		if (pci_read_config_byte(dev, pos + HT_MSI_FLAGS,
					 &flags) == 0) {
			pci_info(dev, "Enabling HT MSI Mapping\n");

			pci_write_config_byte(dev, pos + HT_MSI_FLAGS,
					      flags | HT_MSI_FLAGS_ENABLE);
		}
		pos = pci_find_next_ht_capability(dev, pos,
						  HT_CAPTYPE_MSI_MAPPING);
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SERVERWORKS,
			 PCI_DEVICE_ID_SERVERWORKS_HT1000_PXB,
			 ht_enable_msi_mapping);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_8132_BRIDGE,
			 ht_enable_msi_mapping);

/*
 * The P5N32-SLI motherboards from Asus have a problem with MSI
 * for the MCP55 NIC. It is not yet determined whether the MSI problem
 * also affects other devices. As for now, turn off MSI for this device.
 */
static void nvenet_msi_disable(struct pci_dev *dev)
{
	const char *board_name = dmi_get_system_info(DMI_BOARD_NAME);

	if (board_name &&
	    (strstr(board_name, "P5N32-SLI PREMIUM") ||
	     strstr(board_name, "P5N32-E SLI"))) {
		pci_info(dev, "Disabling MSI for MCP55 NIC on P5N32-SLI\n");
		dev->no_msi = 1;
	}
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_NVIDIA,
			PCI_DEVICE_ID_NVIDIA_NVENET_15,
			nvenet_msi_disable);

/*
 * PCIe spec r6.0 sec 6.1.4.3 says that if MSI/MSI-X is enabled, the device
 * can't use INTx interrupts. Tegra's PCIe Root Ports don't generate MSI
 * interrupts for PME and AER events; instead only INTx interrupts are
 * generated. Though Tegra's PCIe Root Ports can generate MSI interrupts
 * for other events, since PCIe specification doesn't support using a mix of
 * INTx and MSI/MSI-X, it is required to disable MSI interrupts to avoid port
 * service drivers registering their respective ISRs for MSIs.
 */
static void pci_quirk_nvidia_tegra_disable_rp_msi(struct pci_dev *dev)
{
	dev->no_msi = 1;
}
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_NVIDIA, 0x1ad0,
			      PCI_CLASS_BRIDGE_PCI, 8,
			      pci_quirk_nvidia_tegra_disable_rp_msi);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_NVIDIA, 0x1ad1,
			      PCI_CLASS_BRIDGE_PCI, 8,
			      pci_quirk_nvidia_tegra_disable_rp_msi);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_NVIDIA, 0x1ad2,
			      PCI_CLASS_BRIDGE_PCI, 8,
			      pci_quirk_nvidia_tegra_disable_rp_msi);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_NVIDIA, 0x0bf0,
			      PCI_CLASS_BRIDGE_PCI, 8,
			      pci_quirk_nvidia_tegra_disable_rp_msi);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_NVIDIA, 0x0bf1,
			      PCI_CLASS_BRIDGE_PCI, 8,
			      pci_quirk_nvidia_tegra_disable_rp_msi);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_NVIDIA, 0x0e1c,
			      PCI_CLASS_BRIDGE_PCI, 8,
			      pci_quirk_nvidia_tegra_disable_rp_msi);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_NVIDIA, 0x0e1d,
			      PCI_CLASS_BRIDGE_PCI, 8,
			      pci_quirk_nvidia_tegra_disable_rp_msi);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_NVIDIA, 0x0e12,
			      PCI_CLASS_BRIDGE_PCI, 8,
			      pci_quirk_nvidia_tegra_disable_rp_msi);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_NVIDIA, 0x0e13,
			      PCI_CLASS_BRIDGE_PCI, 8,
			      pci_quirk_nvidia_tegra_disable_rp_msi);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_NVIDIA, 0x0fae,
			      PCI_CLASS_BRIDGE_PCI, 8,
			      pci_quirk_nvidia_tegra_disable_rp_msi);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_NVIDIA, 0x0faf,
			      PCI_CLASS_BRIDGE_PCI, 8,
			      pci_quirk_nvidia_tegra_disable_rp_msi);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_NVIDIA, 0x10e5,
			      PCI_CLASS_BRIDGE_PCI, 8,
			      pci_quirk_nvidia_tegra_disable_rp_msi);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_NVIDIA, 0x10e6,
			      PCI_CLASS_BRIDGE_PCI, 8,
			      pci_quirk_nvidia_tegra_disable_rp_msi);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_NVIDIA, 0x229a,
			      PCI_CLASS_BRIDGE_PCI, 8,
			      pci_quirk_nvidia_tegra_disable_rp_msi);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_NVIDIA, 0x229c,
			      PCI_CLASS_BRIDGE_PCI, 8,
			      pci_quirk_nvidia_tegra_disable_rp_msi);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_NVIDIA, 0x229e,
			      PCI_CLASS_BRIDGE_PCI, 8,
			      pci_quirk_nvidia_tegra_disable_rp_msi);

/*
 * Some versions of the MCP55 bridge from Nvidia have a legacy IRQ routing
 * config register.  This register controls the routing of legacy
 * interrupts from devices that route through the MCP55.  If this register
 * is misprogrammed, interrupts are only sent to the BSP, unlike
 * conventional systems where the IRQ is broadcast to all online CPUs.  Not
 * having this register set properly prevents kdump from booting up
 * properly, so let's make sure that we have it set correctly.
 * Note that this is an undocumented register.
 */
static void nvbridge_check_legacy_irq_routing(struct pci_dev *dev)
{
	u32 cfg;

	if (!pci_find_capability(dev, PCI_CAP_ID_HT))
		return;

	pci_read_config_dword(dev, 0x74, &cfg);

	if (cfg & ((1 << 2) | (1 << 15))) {
		pr_info("Rewriting IRQ routing register on MCP55\n");
		cfg &= ~((1 << 2) | (1 << 15));
		pci_write_config_dword(dev, 0x74, cfg);
	}
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_NVIDIA,
			PCI_DEVICE_ID_NVIDIA_MCP55_BRIDGE_V0,
			nvbridge_check_legacy_irq_routing);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_NVIDIA,
			PCI_DEVICE_ID_NVIDIA_MCP55_BRIDGE_V4,
			nvbridge_check_legacy_irq_routing);

static int ht_check_msi_mapping(struct pci_dev *dev)
{
	int pos, ttl = PCI_FIND_CAP_TTL;
	int found = 0;

	/* Check if there is HT MSI cap or enabled on this device */
	pos = pci_find_ht_capability(dev, HT_CAPTYPE_MSI_MAPPING);
	while (pos && ttl--) {
		u8 flags;

		if (found < 1)
			found = 1;
		if (pci_read_config_byte(dev, pos + HT_MSI_FLAGS,
					 &flags) == 0) {
			if (flags & HT_MSI_FLAGS_ENABLE) {
				if (found < 2) {
					found = 2;
					break;
				}
			}
		}
		pos = pci_find_next_ht_capability(dev, pos,
						  HT_CAPTYPE_MSI_MAPPING);
	}

	return found;
}

static int host_bridge_with_leaf(struct pci_dev *host_bridge)
{
	struct pci_dev *dev;
	int pos;
	int i, dev_no;
	int found = 0;

	dev_no = host_bridge->devfn >> 3;
	for (i = dev_no + 1; i < 0x20; i++) {
		dev = pci_get_slot(host_bridge->bus, PCI_DEVFN(i, 0));
		if (!dev)
			continue;

		/* found next host bridge? */
		pos = pci_find_ht_capability(dev, HT_CAPTYPE_SLAVE);
		if (pos != 0) {
			pci_dev_put(dev);
			break;
		}

		if (ht_check_msi_mapping(dev)) {
			found = 1;
			pci_dev_put(dev);
			break;
		}
		pci_dev_put(dev);
	}

	return found;
}

#define PCI_HT_CAP_SLAVE_CTRL0     4    /* link control */
#define PCI_HT_CAP_SLAVE_CTRL1     8    /* link control to */

static int is_end_of_ht_chain(struct pci_dev *dev)
{
	int pos, ctrl_off;
	int end = 0;
	u16 flags, ctrl;

	pos = pci_find_ht_capability(dev, HT_CAPTYPE_SLAVE);

	if (!pos)
		goto out;

	pci_read_config_word(dev, pos + PCI_CAP_FLAGS, &flags);

	ctrl_off = ((flags >> 10) & 1) ?
			PCI_HT_CAP_SLAVE_CTRL0 : PCI_HT_CAP_SLAVE_CTRL1;
	pci_read_config_word(dev, pos + ctrl_off, &ctrl);

	if (ctrl & (1 << 6))
		end = 1;

out:
	return end;
}

static void nv_ht_enable_msi_mapping(struct pci_dev *dev)
{
	struct pci_dev *host_bridge;
	int pos;
	int i, dev_no;
	int found = 0;

	dev_no = dev->devfn >> 3;
	for (i = dev_no; i >= 0; i--) {
		host_bridge = pci_get_slot(dev->bus, PCI_DEVFN(i, 0));
		if (!host_bridge)
			continue;

		pos = pci_find_ht_capability(host_bridge, HT_CAPTYPE_SLAVE);
		if (pos != 0) {
			found = 1;
			break;
		}
		pci_dev_put(host_bridge);
	}

	if (!found)
		return;

	/* don't enable end_device/host_bridge with leaf directly here */
	if (host_bridge == dev && is_end_of_ht_chain(host_bridge) &&
	    host_bridge_with_leaf(host_bridge))
		goto out;

	/* root did that ! */
	if (msi_ht_cap_enabled(host_bridge))
		goto out;

	ht_enable_msi_mapping(dev);

out:
	pci_dev_put(host_bridge);
}

static void ht_disable_msi_mapping(struct pci_dev *dev)
{
	int pos, ttl = PCI_FIND_CAP_TTL;

	pos = pci_find_ht_capability(dev, HT_CAPTYPE_MSI_MAPPING);
	while (pos && ttl--) {
		u8 flags;

		if (pci_read_config_byte(dev, pos + HT_MSI_FLAGS,
					 &flags) == 0) {
			pci_info(dev, "Disabling HT MSI Mapping\n");

			pci_write_config_byte(dev, pos + HT_MSI_FLAGS,
					      flags & ~HT_MSI_FLAGS_ENABLE);
		}
		pos = pci_find_next_ht_capability(dev, pos,
						  HT_CAPTYPE_MSI_MAPPING);
	}
}

static void __nv_msi_ht_cap_quirk(struct pci_dev *dev, int all)
{
	struct pci_dev *host_bridge;
	int pos;
	int found;

	if (!pci_msi_enabled())
		return;

	/* check if there is HT MSI cap or enabled on this device */
	found = ht_check_msi_mapping(dev);

	/* no HT MSI CAP */
	if (found == 0)
		return;

	/*
	 * HT MSI mapping should be disabled on devices that are below
	 * a non-HyperTransport host bridge. Locate the host bridge.
	 */
	host_bridge = pci_get_domain_bus_and_slot(pci_domain_nr(dev->bus), 0,
						  PCI_DEVFN(0, 0));
	if (host_bridge == NULL) {
		pci_warn(dev, "nv_msi_ht_cap_quirk didn't locate host bridge\n");
		return;
	}

	pos = pci_find_ht_capability(host_bridge, HT_CAPTYPE_SLAVE);
	if (pos != 0) {
		/* Host bridge is to HT */
		if (found == 1) {
			/* it is not enabled, try to enable it */
			if (all)
				ht_enable_msi_mapping(dev);
			else
				nv_ht_enable_msi_mapping(dev);
		}
		goto out;
	}

	/* HT MSI is not enabled */
	if (found == 1)
		goto out;

	/* Host bridge is not to HT, disable HT MSI mapping on this device */
	ht_disable_msi_mapping(dev);

out:
	pci_dev_put(host_bridge);
}

static void nv_msi_ht_cap_quirk_all(struct pci_dev *dev)
{
	return __nv_msi_ht_cap_quirk(dev, 1);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AL, PCI_ANY_ID, nv_msi_ht_cap_quirk_all);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_AL, PCI_ANY_ID, nv_msi_ht_cap_quirk_all);

static void nv_msi_ht_cap_quirk_leaf(struct pci_dev *dev)
{
	return __nv_msi_ht_cap_quirk(dev, 0);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID, nv_msi_ht_cap_quirk_leaf);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID, nv_msi_ht_cap_quirk_leaf);

static void quirk_msi_intx_disable_bug(struct pci_dev *dev)
{
	dev->dev_flags |= PCI_DEV_FLAGS_MSI_INTX_DISABLE_BUG;
}

static void quirk_msi_intx_disable_ati_bug(struct pci_dev *dev)
{
	struct pci_dev *p;

	/*
	 * SB700 MSI issue will be fixed at HW level from revision A21;
	 * we need check PCI REVISION ID of SMBus controller to get SB700
	 * revision.
	 */
	p = pci_get_device(PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_SBX00_SMBUS,
			   NULL);
	if (!p)
		return;

	if ((p->revision < 0x3B) && (p->revision >= 0x30))
		dev->dev_flags |= PCI_DEV_FLAGS_MSI_INTX_DISABLE_BUG;
	pci_dev_put(p);
}

static void quirk_msi_intx_disable_qca_bug(struct pci_dev *dev)
{
	/* AR816X/AR817X/E210X MSI is fixed at HW level from revision 0x18 */
	if (dev->revision < 0x18) {
		pci_info(dev, "set MSI_INTX_DISABLE_BUG flag\n");
		dev->dev_flags |= PCI_DEV_FLAGS_MSI_INTX_DISABLE_BUG;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_BROADCOM,
			PCI_DEVICE_ID_TIGON3_5780,
			quirk_msi_intx_disable_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_BROADCOM,
			PCI_DEVICE_ID_TIGON3_5780S,
			quirk_msi_intx_disable_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_BROADCOM,
			PCI_DEVICE_ID_TIGON3_5714,
			quirk_msi_intx_disable_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_BROADCOM,
			PCI_DEVICE_ID_TIGON3_5714S,
			quirk_msi_intx_disable_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_BROADCOM,
			PCI_DEVICE_ID_TIGON3_5715,
			quirk_msi_intx_disable_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_BROADCOM,
			PCI_DEVICE_ID_TIGON3_5715S,
			quirk_msi_intx_disable_bug);

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x4390,
			quirk_msi_intx_disable_ati_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x4391,
			quirk_msi_intx_disable_ati_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x4392,
			quirk_msi_intx_disable_ati_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x4393,
			quirk_msi_intx_disable_ati_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x4394,
			quirk_msi_intx_disable_ati_bug);

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x4373,
			quirk_msi_intx_disable_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x4374,
			quirk_msi_intx_disable_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x4375,
			quirk_msi_intx_disable_bug);

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATTANSIC, 0x1062,
			quirk_msi_intx_disable_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATTANSIC, 0x1063,
			quirk_msi_intx_disable_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATTANSIC, 0x2060,
			quirk_msi_intx_disable_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATTANSIC, 0x2062,
			quirk_msi_intx_disable_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATTANSIC, 0x1073,
			quirk_msi_intx_disable_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATTANSIC, 0x1083,
			quirk_msi_intx_disable_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATTANSIC, 0x1090,
			quirk_msi_intx_disable_qca_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATTANSIC, 0x1091,
			quirk_msi_intx_disable_qca_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATTANSIC, 0x10a0,
			quirk_msi_intx_disable_qca_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATTANSIC, 0x10a1,
			quirk_msi_intx_disable_qca_bug);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATTANSIC, 0xe091,
			quirk_msi_intx_disable_qca_bug);

/*
 * Amazon's Annapurna Labs 1c36:0031 Root Ports don't support MSI-X, so it
 * should be disabled on platforms where the device (mistakenly) advertises it.
 *
 * Notice that this quirk also disables MSI (which may work, but hasn't been
 * tested), since currently there is no standard way to disable only MSI-X.
 *
 * The 0031 device id is reused for other non Root Port device types,
 * therefore the quirk is registered for the PCI_CLASS_BRIDGE_PCI class.
 */
static void quirk_al_msi_disable(struct pci_dev *dev)
{
	dev->no_msi = 1;
	pci_warn(dev, "Disabling MSI/MSI-X\n");
}
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_AMAZON_ANNAPURNA_LABS, 0x0031,
			      PCI_CLASS_BRIDGE_PCI, 8, quirk_al_msi_disable);
#endif /* CONFIG_PCI_MSI */

/*
 * Allow manual resource allocation for PCI hotplug bridges via
 * pci=hpmemsize=nnM and pci=hpiosize=nnM parameters. For some PCI-PCI
 * hotplug bridges, like PLX 6254 (former HINT HB6), kernel fails to
 * allocate resources when hotplug device is inserted and PCI bus is
 * rescanned.
 */
static void quirk_hotplug_bridge(struct pci_dev *dev)
{
	dev->is_hotplug_bridge = 1;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_HINT, 0x0020, quirk_hotplug_bridge);

/*
 * This is a quirk for the Ricoh MMC controller found as a part of some
 * multifunction chips.
 *
 * This is very similar and based on the ricoh_mmc driver written by
 * Philip Langdale. Thank you for these magic sequences.
 *
 * These chips implement the four main memory card controllers (SD, MMC,
 * MS, xD) and one or both of CardBus or FireWire.
 *
 * It happens that they implement SD and MMC support as separate
 * controllers (and PCI functions). The Linux SDHCI driver supports MMC
 * cards but the chip detects MMC cards in hardware and directs them to the
 * MMC controller - so the SDHCI driver never sees them.
 *
 * To get around this, we must disable the useless MMC controller.  At that
 * point, the SDHCI controller will start seeing them.  It seems to be the
 * case that the relevant PCI registers to deactivate the MMC controller
 * live on PCI function 0, which might be the CardBus controller or the
 * FireWire controller, depending on the particular chip in question
 *
 * This has to be done early, because as soon as we disable the MMC controller
 * other PCI functions shift up one level, e.g. function #2 becomes function
 * #1, and this will confuse the PCI core.
 */
#ifdef CONFIG_MMC_RICOH_MMC
static void ricoh_mmc_fixup_rl5c476(struct pci_dev *dev)
{
	u8 write_enable;
	u8 write_target;
	u8 disable;

	/*
	 * Disable via CardBus interface
	 *
	 * This must be done via function #0
	 */
	if (PCI_FUNC(dev->devfn))
		return;

	pci_read_config_byte(dev, 0xB7, &disable);
	if (disable & 0x02)
		return;

	pci_read_config_byte(dev, 0x8E, &write_enable);
	pci_write_config_byte(dev, 0x8E, 0xAA);
	pci_read_config_byte(dev, 0x8D, &write_target);
	pci_write_config_byte(dev, 0x8D, 0xB7);
	pci_write_config_byte(dev, 0xB7, disable | 0x02);
	pci_write_config_byte(dev, 0x8E, write_enable);
	pci_write_config_byte(dev, 0x8D, write_target);

	pci_notice(dev, "proprietary Ricoh MMC controller disabled (via CardBus function)\n");
	pci_notice(dev, "MMC cards are now supported by standard SDHCI controller\n");
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_RICOH, PCI_DEVICE_ID_RICOH_RL5C476, ricoh_mmc_fixup_rl5c476);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_RICOH, PCI_DEVICE_ID_RICOH_RL5C476, ricoh_mmc_fixup_rl5c476);

static void ricoh_mmc_fixup_r5c832(struct pci_dev *dev)
{
	u8 write_enable;
	u8 disable;

	/*
	 * Disable via FireWire interface
	 *
	 * This must be done via function #0
	 */
	if (PCI_FUNC(dev->devfn))
		return;
	/*
	 * RICOH 0xe822 and 0xe823 SD/MMC card readers fail to recognize
	 * certain types of SD/MMC cards. Lowering the SD base clock
	 * frequency from 200Mhz to 50Mhz fixes this issue.
	 *
	 * 0x150 - SD2.0 mode enable for changing base clock
	 *	   frequency to 50Mhz
	 * 0xe1  - Base clock frequency
	 * 0x32  - 50Mhz new clock frequency
	 * 0xf9  - Key register for 0x150
	 * 0xfc  - key register for 0xe1
	 */
	if (dev->device == PCI_DEVICE_ID_RICOH_R5CE822 ||
	    dev->device == PCI_DEVICE_ID_RICOH_R5CE823) {
		pci_write_config_byte(dev, 0xf9, 0xfc);
		pci_write_config_byte(dev, 0x150, 0x10);
		pci_write_config_byte(dev, 0xf9, 0x00);
		pci_write_config_byte(dev, 0xfc, 0x01);
		pci_write_config_byte(dev, 0xe1, 0x32);
		pci_write_config_byte(dev, 0xfc, 0x00);

		pci_notice(dev, "MMC controller base frequency changed to 50Mhz.\n");
	}

	pci_read_config_byte(dev, 0xCB, &disable);

	if (disable & 0x02)
		return;

	pci_read_config_byte(dev, 0xCA, &write_enable);
	pci_write_config_byte(dev, 0xCA, 0x57);
	pci_write_config_byte(dev, 0xCB, disable | 0x02);
	pci_write_config_byte(dev, 0xCA, write_enable);

	pci_notice(dev, "proprietary Ricoh MMC controller disabled (via FireWire function)\n");
	pci_notice(dev, "MMC cards are now supported by standard SDHCI controller\n");

}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_RICOH, PCI_DEVICE_ID_RICOH_R5C832, ricoh_mmc_fixup_r5c832);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_RICOH, PCI_DEVICE_ID_RICOH_R5C832, ricoh_mmc_fixup_r5c832);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_RICOH, PCI_DEVICE_ID_RICOH_R5CE822, ricoh_mmc_fixup_r5c832);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_RICOH, PCI_DEVICE_ID_RICOH_R5CE822, ricoh_mmc_fixup_r5c832);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_RICOH, PCI_DEVICE_ID_RICOH_R5CE823, ricoh_mmc_fixup_r5c832);
DECLARE_PCI_FIXUP_RESUME_EARLY(PCI_VENDOR_ID_RICOH, PCI_DEVICE_ID_RICOH_R5CE823, ricoh_mmc_fixup_r5c832);
#endif /*CONFIG_MMC_RICOH_MMC*/

#ifdef CONFIG_DMAR_TABLE
#define VTUNCERRMSK_REG	0x1ac
#define VTD_MSK_SPEC_ERRORS	(1 << 31)
/*
 * This is a quirk for masking VT-d spec-defined errors to platform error
 * handling logic. Without this, platforms using Intel 7500, 5500 chipsets
 * (and the derivative chipsets like X58 etc) seem to generate NMI/SMI (based
 * on the RAS config settings of the platform) when a VT-d fault happens.
 * The resulting SMI caused the system to hang.
 *
 * VT-d spec-related errors are already handled by the VT-d OS code, so no
 * need to report the same error through other channels.
 */
static void vtd_mask_spec_errors(struct pci_dev *dev)
{
	u32 word;

	pci_read_config_dword(dev, VTUNCERRMSK_REG, &word);
	pci_write_config_dword(dev, VTUNCERRMSK_REG, word | VTD_MSK_SPEC_ERRORS);
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x342e, vtd_mask_spec_errors);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x3c28, vtd_mask_spec_errors);
#endif

static void fixup_ti816x_class(struct pci_dev *dev)
{
	u32 class = dev->class;

	/* TI 816x devices do not have class code set when in PCIe boot mode */
	dev->class = PCI_CLASS_MULTIMEDIA_VIDEO << 8;
	pci_info(dev, "PCI class overridden (%#08x -> %#08x)\n",
		 class, dev->class);
}
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_TI, 0xb800,
			      PCI_CLASS_NOT_DEFINED, 8, fixup_ti816x_class);

/*
 * Some PCIe devices do not work reliably with the claimed maximum
 * payload size supported.
 */
static void fixup_mpss_256(struct pci_dev *dev)
{
	dev->pcie_mpss = 1; /* 256 bytes */
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SOLARFLARE,
			PCI_DEVICE_ID_SOLARFLARE_SFC4000A_0, fixup_mpss_256);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SOLARFLARE,
			PCI_DEVICE_ID_SOLARFLARE_SFC4000A_1, fixup_mpss_256);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SOLARFLARE,
			PCI_DEVICE_ID_SOLARFLARE_SFC4000B, fixup_mpss_256);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_ASMEDIA, 0x0612, fixup_mpss_256);

/*
 * Intel 5000 and 5100 Memory controllers have an erratum with read completion
 * coalescing (which is enabled by default on some BIOSes) and MPS of 256B.
 * Since there is no way of knowing what the PCIe MPS on each fabric will be
 * until all of the devices are discovered and buses walked, read completion
 * coalescing must be disabled.  Unfortunately, it cannot be re-enabled because
 * it is possible to hotplug a device with MPS of 256B.
 */
static void quirk_intel_mc_errata(struct pci_dev *dev)
{
	int err;
	u16 rcc;

	if (pcie_bus_config == PCIE_BUS_TUNE_OFF ||
	    pcie_bus_config == PCIE_BUS_DEFAULT)
		return;

	/*
	 * Intel erratum specifies bits to change but does not say what
	 * they are.  Keeping them magical until such time as the registers
	 * and values can be explained.
	 */
	err = pci_read_config_word(dev, 0x48, &rcc);
	if (err) {
		pci_err(dev, "Error attempting to read the read completion coalescing register\n");
		return;
	}

	if (!(rcc & (1 << 10)))
		return;

	rcc &= ~(1 << 10);

	err = pci_write_config_word(dev, 0x48, rcc);
	if (err) {
		pci_err(dev, "Error attempting to write the read completion coalescing register\n");
		return;
	}

	pr_info_once("Read completion coalescing disabled due to hardware erratum relating to 256B MPS\n");
}
/* Intel 5000 series memory controllers and ports 2-7 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x25c0, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x25d0, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x25d4, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x25d8, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x25e2, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x25e3, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x25e4, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x25e5, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x25e6, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x25e7, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x25f7, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x25f8, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x25f9, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x25fa, quirk_intel_mc_errata);
/* Intel 5100 series memory controllers and ports 2-7 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x65c0, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x65e2, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x65e3, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x65e4, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x65e5, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x65e6, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x65e7, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x65f7, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x65f8, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x65f9, quirk_intel_mc_errata);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x65fa, quirk_intel_mc_errata);

/*
 * Ivytown NTB BAR sizes are misreported by the hardware due to an erratum.
 * To work around this, query the size it should be configured to by the
 * device and modify the resource end to correspond to this new size.
 */
static void quirk_intel_ntb(struct pci_dev *dev)
{
	int rc;
	u8 val;

	rc = pci_read_config_byte(dev, 0x00D0, &val);
	if (rc)
		return;

	resource_set_size(&dev->resource[2], (resource_size_t)1 << val);

	rc = pci_read_config_byte(dev, 0x00D1, &val);
	if (rc)
		return;

	resource_set_size(&dev->resource[4], (resource_size_t)1 << val);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x0e08, quirk_intel_ntb);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x0e0d, quirk_intel_ntb);

/*
 * Some BIOS implementations leave the Intel GPU interrupts enabled, even
 * though no one is handling them (e.g., if the i915 driver is never
 * loaded).  Additionally the interrupt destination is not set up properly
 * and the interrupt ends up -somewhere-.
 *
 * These spurious interrupts are "sticky" and the kernel disables the
 * (shared) interrupt line after 100,000+ generated interrupts.
 *
 * Fix it by disabling the still enabled interrupts.  This resolves crashes
 * often seen on monitor unplug.
 */
#define I915_DEIER_REG 0x4400c
static void disable_igfx_irq(struct pci_dev *dev)
{
	void __iomem *regs = pci_iomap(dev, 0, 0);
	if (regs == NULL) {
		pci_warn(dev, "igfx quirk: Can't iomap PCI device\n");
		return;
	}

	/* Check if any interrupt line is still enabled */
	if (readl(regs + I915_DEIER_REG) != 0) {
		pci_warn(dev, "BIOS left Intel GPU interrupts enabled; disabling\n");

		writel(0, regs + I915_DEIER_REG);
	}

	pci_iounmap(dev, regs);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x0042, disable_igfx_irq);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x0046, disable_igfx_irq);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x004a, disable_igfx_irq);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x0102, disable_igfx_irq);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x0106, disable_igfx_irq);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x010a, disable_igfx_irq);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x0152, disable_igfx_irq);

/*
 * PCI devices which are on Intel chips can skip the 10ms delay
 * before entering D3 mode.
 */
static void quirk_remove_d3hot_delay(struct pci_dev *dev)
{
	dev->d3hot_delay = 0;
}
/* C600 Series devices do not need 10ms d3hot_delay */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x0412, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x0c00, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x0c0c, quirk_remove_d3hot_delay);
/* Lynxpoint-H PCH devices do not need 10ms d3hot_delay */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x8c02, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x8c18, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x8c1c, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x8c20, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x8c22, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x8c26, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x8c2d, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x8c31, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x8c3a, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x8c3d, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x8c4e, quirk_remove_d3hot_delay);
/* Intel Cherrytrail devices do not need 10ms d3hot_delay */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x2280, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x2298, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x229c, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x22b0, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x22b5, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x22b7, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x22b8, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x22d8, quirk_remove_d3hot_delay);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x22dc, quirk_remove_d3hot_delay);

/*
 * Some devices may pass our check in pci_intx_mask_supported() if
 * PCI_COMMAND_INTX_DISABLE works though they actually do not properly
 * support this feature.
 */
static void quirk_broken_intx_masking(struct pci_dev *dev)
{
	dev->broken_intx_masking = 1;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_CHELSIO, 0x0030,
			quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(0x1814, 0x0601, /* Ralink RT2800 802.11n PCI */
			quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(0x1b7c, 0x0004, /* Ceton InfiniTV4 */
			quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_CREATIVE, PCI_DEVICE_ID_CREATIVE_20K2,
			quirk_broken_intx_masking);

/*
 * Realtek RTL8169 PCI Gigabit Ethernet Controller (rev 10)
 * Subsystem: Realtek RTL8169/8110 Family PCI Gigabit Ethernet NIC
 *
 * RTL8110SC - Fails under PCI device assignment using DisINTx masking.
 */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_REALTEK, 0x8169,
			quirk_broken_intx_masking);

/*
 * Intel i40e (XL710/X710) 10/20/40GbE NICs all have broken INTx masking,
 * DisINTx can be set but the interrupt status bit is non-functional.
 */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1572, quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1574, quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1580, quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1581, quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1583, quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1584, quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1585, quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1586, quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1587, quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1588, quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1589, quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x158a, quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x158b, quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x37d0, quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x37d1, quirk_broken_intx_masking);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x37d2, quirk_broken_intx_masking);

static u16 mellanox_broken_intx_devs[] = {
	PCI_DEVICE_ID_MELLANOX_HERMON_SDR,
	PCI_DEVICE_ID_MELLANOX_HERMON_DDR,
	PCI_DEVICE_ID_MELLANOX_HERMON_QDR,
	PCI_DEVICE_ID_MELLANOX_HERMON_DDR_GEN2,
	PCI_DEVICE_ID_MELLANOX_HERMON_QDR_GEN2,
	PCI_DEVICE_ID_MELLANOX_HERMON_EN,
	PCI_DEVICE_ID_MELLANOX_HERMON_EN_GEN2,
	PCI_DEVICE_ID_MELLANOX_CONNECTX_EN,
	PCI_DEVICE_ID_MELLANOX_CONNECTX_EN_T_GEN2,
	PCI_DEVICE_ID_MELLANOX_CONNECTX_EN_GEN2,
	PCI_DEVICE_ID_MELLANOX_CONNECTX_EN_5_GEN2,
	PCI_DEVICE_ID_MELLANOX_CONNECTX2,
	PCI_DEVICE_ID_MELLANOX_CONNECTX3,
	PCI_DEVICE_ID_MELLANOX_CONNECTX3_PRO,
};

#define CONNECTX_4_CURR_MAX_MINOR 99
#define CONNECTX_4_INTX_SUPPORT_MINOR 14

/*
 * Check ConnectX-4/LX FW version to see if it supports legacy interrupts.
 * If so, don't mark it as broken.
 * FW minor > 99 means older FW version format and no INTx masking support.
 * FW minor < 14 means new FW version format and no INTx masking support.
 */
static void mellanox_check_broken_intx_masking(struct pci_dev *pdev)
{
	__be32 __iomem *fw_ver;
	u16 fw_major;
	u16 fw_minor;
	u16 fw_subminor;
	u32 fw_maj_min;
	u32 fw_sub_min;
	int i;

	for (i = 0; i < ARRAY_SIZE(mellanox_broken_intx_devs); i++) {
		if (pdev->device == mellanox_broken_intx_devs[i]) {
			pdev->broken_intx_masking = 1;
			return;
		}
	}

	/*
	 * Getting here means Connect-IB cards and up. Connect-IB has no INTx
	 * support so shouldn't be checked further
	 */
	if (pdev->device == PCI_DEVICE_ID_MELLANOX_CONNECTIB)
		return;

	if (pdev->device != PCI_DEVICE_ID_MELLANOX_CONNECTX4 &&
	    pdev->device != PCI_DEVICE_ID_MELLANOX_CONNECTX4_LX)
		return;

	/* For ConnectX-4 and ConnectX-4LX, need to check FW support */
	if (pci_enable_device_mem(pdev)) {
		pci_warn(pdev, "Can't enable device memory\n");
		return;
	}

	fw_ver = ioremap(pci_resource_start(pdev, 0), 4);
	if (!fw_ver) {
		pci_warn(pdev, "Can't map ConnectX-4 initialization segment\n");
		goto out;
	}

	/* Reading from resource space should be 32b aligned */
	fw_maj_min = ioread32be(fw_ver);
	fw_sub_min = ioread32be(fw_ver + 1);
	fw_major = fw_maj_min & 0xffff;
	fw_minor = fw_maj_min >> 16;
	fw_subminor = fw_sub_min & 0xffff;
	if (fw_minor > CONNECTX_4_CURR_MAX_MINOR ||
	    fw_minor < CONNECTX_4_INTX_SUPPORT_MINOR) {
		pci_warn(pdev, "ConnectX-4: FW %u.%u.%u doesn't support INTx masking, disabling. Please upgrade FW to %d.14.1100 and up for INTx support\n",
			 fw_major, fw_minor, fw_subminor, pdev->device ==
			 PCI_DEVICE_ID_MELLANOX_CONNECTX4 ? 12 : 14);
		pdev->broken_intx_masking = 1;
	}

	iounmap(fw_ver);

out:
	pci_disable_device(pdev);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_MELLANOX, PCI_ANY_ID,
			mellanox_check_broken_intx_masking);

static void quirk_no_bus_reset(struct pci_dev *dev)
{
	dev->dev_flags |= PCI_DEV_FLAGS_NO_BUS_RESET;
}

/*
 * Some NVIDIA GPU devices do not work with bus reset, SBR needs to be
 * prevented for those affected devices.
 */
static void quirk_nvidia_no_bus_reset(struct pci_dev *dev)
{
	if ((dev->device & 0xffc0) == 0x2340)
		quirk_no_bus_reset(dev);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID,
			 quirk_nvidia_no_bus_reset);

/*
 * Some Atheros AR9xxx and QCA988x chips do not behave after a bus reset.
 * The device will throw a Link Down error on AER-capable systems and
 * regardless of AER, config space of the device is never accessible again
 * and typically causes the system to hang or reset when access is attempted.
 * https://lore.kernel.org/r/20140923210318.498dacbd@dualc.maya.org/
 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_ATHEROS, 0x0030, quirk_no_bus_reset);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_ATHEROS, 0x0032, quirk_no_bus_reset);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_ATHEROS, 0x003c, quirk_no_bus_reset);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_ATHEROS, 0x0033, quirk_no_bus_reset);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_ATHEROS, 0x0034, quirk_no_bus_reset);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_ATHEROS, 0x003e, quirk_no_bus_reset);

/*
 * Root port on some Cavium CN8xxx chips do not successfully complete a bus
 * reset when used with certain child devices.  After the reset, config
 * accesses to the child may fail.
 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_CAVIUM, 0xa100, quirk_no_bus_reset);

/*
 * Some TI KeyStone C667X devices do not support bus/hot reset.  The PCIESS
 * automatically disables LTSSM when Secondary Bus Reset is received and
 * the device stops working.  Prevent bus reset for these devices.  With
 * this change, the device can be assigned to VMs with VFIO, but it will
 * leak state between VMs.  Reference
 * https://e2e.ti.com/support/processors/f/791/t/954382
 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_TI, 0xb005, quirk_no_bus_reset);

static void quirk_no_pm_reset(struct pci_dev *dev)
{
	/*
	 * We can't do a bus reset on root bus devices, but an ineffective
	 * PM reset may be better than nothing.
	 */
	if (!pci_is_root_bus(dev->bus))
		dev->dev_flags |= PCI_DEV_FLAGS_NO_PM_RESET;
}

/*
 * Some AMD/ATI GPUS (HD8570 - Oland) report that a D3hot->D0 transition
 * causes a reset (i.e., they advertise NoSoftRst-).  This transition seems
 * to have no effect on the device: it retains the framebuffer contents and
 * monitor sync.  Advertising this support makes other layers, like VFIO,
 * assume pci_reset_function() is viable for this device.  Mark it as
 * unavailable to skip it when testing reset methods.
 */
DECLARE_PCI_FIXUP_CLASS_HEADER(PCI_VENDOR_ID_ATI, PCI_ANY_ID,
			       PCI_CLASS_DISPLAY_VGA, 8, quirk_no_pm_reset);

/*
 * Spectrum-{1,2,3,4} devices report that a D3hot->D0 transition causes a reset
 * (i.e., they advertise NoSoftRst-). However, this transition does not have
 * any effect on the device: It continues to be operational and network ports
 * remain up. Advertising this support makes it seem as if a PM reset is viable
 * for these devices. Mark it as unavailable to skip it when testing reset
 * methods.
 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MELLANOX, 0xcb84, quirk_no_pm_reset);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MELLANOX, 0xcf6c, quirk_no_pm_reset);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MELLANOX, 0xcf70, quirk_no_pm_reset);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MELLANOX, 0xcf80, quirk_no_pm_reset);

/*
 * Thunderbolt controllers with broken MSI hotplug signaling:
 * Entire 1st generation (Light Ridge, Eagle Ridge, Light Peak) and part
 * of the 2nd generation (Cactus Ridge 4C up to revision 1, Port Ridge).
 */
static void quirk_thunderbolt_hotplug_msi(struct pci_dev *pdev)
{
	if (pdev->is_hotplug_bridge &&
	    (pdev->device != PCI_DEVICE_ID_INTEL_CACTUS_RIDGE_4C ||
	     pdev->revision <= 1))
		pdev->no_msi = 1;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_LIGHT_RIDGE,
			quirk_thunderbolt_hotplug_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_EAGLE_RIDGE,
			quirk_thunderbolt_hotplug_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_LIGHT_PEAK,
			quirk_thunderbolt_hotplug_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_CACTUS_RIDGE_4C,
			quirk_thunderbolt_hotplug_msi);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_PORT_RIDGE,
			quirk_thunderbolt_hotplug_msi);

#ifdef CONFIG_ACPI
/*
 * Apple: Shutdown Cactus Ridge Thunderbolt controller.
 *
 * On Apple hardware the Cactus Ridge Thunderbolt controller needs to be
 * shutdown before suspend. Otherwise the native host interface (NHI) will not
 * be present after resume if a device was plugged in before suspend.
 *
 * The Thunderbolt controller consists of a PCIe switch with downstream
 * bridges leading to the NHI and to the tunnel PCI bridges.
 *
 * This quirk cuts power to the whole chip. Therefore we have to apply it
 * during suspend_noirq of the upstream bridge.
 *
 * Power is automagically restored before resume. No action is needed.
 */
static void quirk_apple_poweroff_thunderbolt(struct pci_dev *dev)
{
	acpi_handle bridge, SXIO, SXFP, SXLV;

	if (!x86_apple_machine)
		return;
	if (pci_pcie_type(dev) != PCI_EXP_TYPE_UPSTREAM)
		return;

	/*
	 * SXIO/SXFP/SXLF turns off power to the Thunderbolt controller.
	 * We don't know how to turn it back on again, but firmware does,
	 * so we can only use SXIO/SXFP/SXLF if we're suspending via
	 * firmware.
	 */
	if (!pm_suspend_via_firmware())
		return;

	bridge = ACPI_HANDLE(&dev->dev);
	if (!bridge)
		return;

	/*
	 * SXIO and SXLV are present only on machines requiring this quirk.
	 * Thunderbolt bridges in external devices might have the same
	 * device ID as those on the host, but they will not have the
	 * associated ACPI methods. This implicitly checks that we are at
	 * the right bridge.
	 */
	if (ACPI_FAILURE(acpi_get_handle(bridge, "DSB0.NHI0.SXIO", &SXIO))
	    || ACPI_FAILURE(acpi_get_handle(bridge, "DSB0.NHI0.SXFP", &SXFP))
	    || ACPI_FAILURE(acpi_get_handle(bridge, "DSB0.NHI0.SXLV", &SXLV)))
		return;
	pci_info(dev, "quirk: cutting power to Thunderbolt controller...\n");

	/* magic sequence */
	acpi_execute_simple_method(SXIO, NULL, 1);
	acpi_execute_simple_method(SXFP, NULL, 0);
	msleep(300);
	acpi_execute_simple_method(SXLV, NULL, 0);
	acpi_execute_simple_method(SXIO, NULL, 0);
	acpi_execute_simple_method(SXLV, NULL, 0);
}
DECLARE_PCI_FIXUP_SUSPEND_LATE(PCI_VENDOR_ID_INTEL,
			       PCI_DEVICE_ID_INTEL_CACTUS_RIDGE_4C,
			       quirk_apple_poweroff_thunderbolt);
#endif

/*
 * Following are device-specific reset methods which can be used to
 * reset a single function if other methods (e.g. FLR, PM D0->D3) are
 * not available.
 */
static int reset_intel_82599_sfp_virtfn(struct pci_dev *dev, bool probe)
{
	/*
	 * http://www.intel.com/content/dam/doc/datasheet/82599-10-gbe-controller-datasheet.pdf
	 *
	 * The 82599 supports FLR on VFs, but FLR support is reported only
	 * in the PF DEVCAP (sec 9.3.10.4), not in the VF DEVCAP (sec 9.5).
	 * Thus we must call pcie_flr() directly without first checking if it is
	 * supported.
	 */
	if (!probe)
		pcie_flr(dev);
	return 0;
}

#define SOUTH_CHICKEN2		0xc2004
#define PCH_PP_STATUS		0xc7200
#define PCH_PP_CONTROL		0xc7204
#define MSG_CTL			0x45010
#define NSDE_PWR_STATE		0xd0100
#define IGD_OPERATION_TIMEOUT	10000     /* set timeout 10 seconds */

static int reset_ivb_igd(struct pci_dev *dev, bool probe)
{
	void __iomem *mmio_base;
	unsigned long timeout;
	u32 val;

	if (probe)
		return 0;

	mmio_base = pci_iomap(dev, 0, 0);
	if (!mmio_base)
		return -ENOMEM;

	iowrite32(0x00000002, mmio_base + MSG_CTL);

	/*
	 * Clobbering SOUTH_CHICKEN2 register is fine only if the next
	 * driver loaded sets the right bits. However, this's a reset and
	 * the bits have been set by i915 previously, so we clobber
	 * SOUTH_CHICKEN2 register directly here.
	 */
	iowrite32(0x00000005, mmio_base + SOUTH_CHICKEN2);

	val = ioread32(mmio_base + PCH_PP_CONTROL) & 0xfffffffe;
	iowrite32(val, mmio_base + PCH_PP_CONTROL);

	timeout = jiffies + msecs_to_jiffies(IGD_OPERATION_TIMEOUT);
	do {
		val = ioread32(mmio_base + PCH_PP_STATUS);
		if ((val & 0xb0000000) == 0)
			goto reset_complete;
		msleep(10);
	} while (time_before(jiffies, timeout));
	pci_warn(dev, "timeout during reset\n");

reset_complete:
	iowrite32(0x00000002, mmio_base + NSDE_PWR_STATE);

	pci_iounmap(dev, mmio_base);
	return 0;
}

/* Device-specific reset method for Chelsio T4-based adapters */
static int reset_chelsio_generic_dev(struct pci_dev *dev, bool probe)
{
	u16 old_command;
	u16 msix_flags;

	/*
	 * If this isn't a Chelsio T4-based device, return -ENOTTY indicating
	 * that we have no device-specific reset method.
	 */
	if ((dev->device & 0xf000) != 0x4000)
		return -ENOTTY;

	/*
	 * If this is the "probe" phase, return 0 indicating that we can
	 * reset this device.
	 */
	if (probe)
		return 0;

	/*
	 * T4 can wedge if there are DMAs in flight within the chip and Bus
	 * Master has been disabled.  We need to have it on till the Function
	 * Level Reset completes.  (BUS_MASTER is disabled in
	 * pci_reset_function()).
	 */
	pci_read_config_word(dev, PCI_COMMAND, &old_command);
	pci_write_config_word(dev, PCI_COMMAND,
			      old_command | PCI_COMMAND_MASTER);

	/*
	 * Perform the actual device function reset, saving and restoring
	 * configuration information around the reset.
	 */
	pci_save_state(dev);

	/*
	 * T4 also suffers a Head-Of-Line blocking problem if MSI-X interrupts
	 * are disabled when an MSI-X interrupt message needs to be delivered.
	 * So we briefly re-enable MSI-X interrupts for the duration of the
	 * FLR.  The pci_restore_state() below will restore the original
	 * MSI-X state.
	 */
	pci_read_config_word(dev, dev->msix_cap+PCI_MSIX_FLAGS, &msix_flags);
	if ((msix_flags & PCI_MSIX_FLAGS_ENABLE) == 0)
		pci_write_config_word(dev, dev->msix_cap+PCI_MSIX_FLAGS,
				      msix_flags |
				      PCI_MSIX_FLAGS_ENABLE |
				      PCI_MSIX_FLAGS_MASKALL);

	pcie_flr(dev);

	/*
	 * Restore the configuration information (BAR values, etc.) including
	 * the original PCI Configuration Space Command word, and return
	 * success.
	 */
	pci_restore_state(dev);
	pci_write_config_word(dev, PCI_COMMAND, old_command);
	return 0;
}

#define PCI_DEVICE_ID_INTEL_82599_SFP_VF   0x10ed
#define PCI_DEVICE_ID_INTEL_IVB_M_VGA      0x0156
#define PCI_DEVICE_ID_INTEL_IVB_M2_VGA     0x0166

/*
 * The Samsung SM961/PM961 controller can sometimes enter a fatal state after
 * FLR where config space reads from the device return -1.  We seem to be
 * able to avoid this condition if we disable the NVMe controller prior to
 * FLR.  This quirk is generic for any NVMe class device requiring similar
 * assistance to quiesce the device prior to FLR.
 *
 * NVMe specification: https://nvmexpress.org/resources/specifications/
 * Revision 1.0e:
 *    Chapter 2: Required and optional PCI config registers
 *    Chapter 3: NVMe control registers
 *    Chapter 7.3: Reset behavior
 */
static int nvme_disable_and_flr(struct pci_dev *dev, bool probe)
{
	void __iomem *bar;
	u16 cmd;
	u32 cfg;

	if (dev->class != PCI_CLASS_STORAGE_EXPRESS ||
	    pcie_reset_flr(dev, PCI_RESET_PROBE) || !pci_resource_start(dev, 0))
		return -ENOTTY;

	if (probe)
		return 0;

	bar = pci_iomap(dev, 0, NVME_REG_CC + sizeof(cfg));
	if (!bar)
		return -ENOTTY;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	pci_write_config_word(dev, PCI_COMMAND, cmd | PCI_COMMAND_MEMORY);

	cfg = readl(bar + NVME_REG_CC);

	/* Disable controller if enabled */
	if (cfg & NVME_CC_ENABLE) {
		u32 cap = readl(bar + NVME_REG_CAP);
		unsigned long timeout;

		/*
		 * Per nvme_disable_ctrl() skip shutdown notification as it
		 * could complete commands to the admin queue.  We only intend
		 * to quiesce the device before reset.
		 */
		cfg &= ~(NVME_CC_SHN_MASK | NVME_CC_ENABLE);

		writel(cfg, bar + NVME_REG_CC);

		/*
		 * Some controllers require an additional delay here, see
		 * NVME_QUIRK_DELAY_BEFORE_CHK_RDY.  None of those are yet
		 * supported by this quirk.
		 */

		/* Cap register provides max timeout in 500ms increments */
		timeout = ((NVME_CAP_TIMEOUT(cap) + 1) * HZ / 2) + jiffies;

		for (;;) {
			u32 status = readl(bar + NVME_REG_CSTS);

			/* Ready status becomes zero on disable complete */
			if (!(status & NVME_CSTS_RDY))
				break;

			msleep(100);

			if (time_after(jiffies, timeout)) {
				pci_warn(dev, "Timeout waiting for NVMe ready status to clear after disable\n");
				break;
			}
		}
	}

	pci_iounmap(dev, bar);

	pcie_flr(dev);

	return 0;
}

/*
 * Some NVMe controllers such as Intel DC P3700 and Solidigm P44 Pro will
 * timeout waiting for ready status to change after NVMe enable if the driver
 * starts interacting with the device too soon after FLR.  A 250ms delay after
 * FLR has heuristically proven to produce reliably working results for device
 * assignment cases.
 */
static int delay_250ms_after_flr(struct pci_dev *dev, bool probe)
{
	if (probe)
		return pcie_reset_flr(dev, PCI_RESET_PROBE);

	pcie_reset_flr(dev, PCI_RESET_DO_RESET);

	msleep(250);

	return 0;
}

#define PCI_DEVICE_ID_HINIC_VF      0x375E
#define HINIC_VF_FLR_TYPE           0x1000
#define HINIC_VF_FLR_CAP_BIT        (1UL << 30)
#define HINIC_VF_OP                 0xE80
#define HINIC_VF_FLR_PROC_BIT       (1UL << 18)
#define HINIC_OPERATION_TIMEOUT     15000	/* 15 seconds */

/* Device-specific reset method for Huawei Intelligent NIC virtual functions */
static int reset_hinic_vf_dev(struct pci_dev *pdev, bool probe)
{
	unsigned long timeout;
	void __iomem *bar;
	u32 val;

	if (probe)
		return 0;

	bar = pci_iomap(pdev, 0, 0);
	if (!bar)
		return -ENOTTY;

	/* Get and check firmware capabilities */
	val = ioread32be(bar + HINIC_VF_FLR_TYPE);
	if (!(val & HINIC_VF_FLR_CAP_BIT)) {
		pci_iounmap(pdev, bar);
		return -ENOTTY;
	}

	/* Set HINIC_VF_FLR_PROC_BIT for the start of FLR */
	val = ioread32be(bar + HINIC_VF_OP);
	val = val | HINIC_VF_FLR_PROC_BIT;
	iowrite32be(val, bar + HINIC_VF_OP);

	pcie_flr(pdev);

	/*
	 * The device must recapture its Bus and Device Numbers after FLR
	 * in order generate Completions.  Issue a config write to let the
	 * device capture this information.
	 */
	pci_write_config_word(pdev, PCI_VENDOR_ID, 0);

	/* Firmware clears HINIC_VF_FLR_PROC_BIT when reset is complete */
	timeout = jiffies + msecs_to_jiffies(HINIC_OPERATION_TIMEOUT);
	do {
		val = ioread32be(bar + HINIC_VF_OP);
		if (!(val & HINIC_VF_FLR_PROC_BIT))
			goto reset_complete;
		msleep(20);
	} while (time_before(jiffies, timeout));

	val = ioread32be(bar + HINIC_VF_OP);
	if (!(val & HINIC_VF_FLR_PROC_BIT))
		goto reset_complete;

	pci_warn(pdev, "Reset dev timeout, FLR ack reg: %#010x\n", val);

reset_complete:
	pci_iounmap(pdev, bar);

	return 0;
}

static const struct pci_dev_reset_methods pci_dev_reset_methods[] = {
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82599_SFP_VF,
		 reset_intel_82599_sfp_virtfn },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_IVB_M_VGA,
		reset_ivb_igd },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_IVB_M2_VGA,
		reset_ivb_igd },
	{ PCI_VENDOR_ID_SAMSUNG, 0xa804, nvme_disable_and_flr },
	{ PCI_VENDOR_ID_INTEL, 0x0953, delay_250ms_after_flr },
	{ PCI_VENDOR_ID_INTEL, 0x0a54, delay_250ms_after_flr },
	{ PCI_VENDOR_ID_SOLIDIGM, 0xf1ac, delay_250ms_after_flr },
	{ PCI_VENDOR_ID_CHELSIO, PCI_ANY_ID,
		reset_chelsio_generic_dev },
	{ PCI_VENDOR_ID_HUAWEI, PCI_DEVICE_ID_HINIC_VF,
		reset_hinic_vf_dev },
	{ 0 }
};

/*
 * These device-specific reset methods are here rather than in a driver
 * because when a host assigns a device to a guest VM, the host may need
 * to reset the device but probably doesn't have a driver for it.
 */
int pci_dev_specific_reset(struct pci_dev *dev, bool probe)
{
	const struct pci_dev_reset_methods *i;

	for (i = pci_dev_reset_methods; i->reset; i++) {
		if ((i->vendor == dev->vendor ||
		     i->vendor == (u16)PCI_ANY_ID) &&
		    (i->device == dev->device ||
		     i->device == (u16)PCI_ANY_ID))
			return i->reset(dev, probe);
	}

	return -ENOTTY;
}

static void quirk_dma_func0_alias(struct pci_dev *dev)
{
	if (PCI_FUNC(dev->devfn) != 0)
		pci_add_dma_alias(dev, PCI_DEVFN(PCI_SLOT(dev->devfn), 0), 1);
}

/*
 * https://bugzilla.redhat.com/show_bug.cgi?id=605888
 *
 * Some Ricoh devices use function 0 as the PCIe requester ID for DMA.
 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_RICOH, 0xe832, quirk_dma_func0_alias);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_RICOH, 0xe476, quirk_dma_func0_alias);

/* Some Glenfly chips use function 0 as the PCIe Requester ID for DMA */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_GLENFLY, 0x3d40, quirk_dma_func0_alias);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_GLENFLY, 0x3d41, quirk_dma_func0_alias);

static void quirk_dma_func1_alias(struct pci_dev *dev)
{
	if (PCI_FUNC(dev->devfn) != 1)
		pci_add_dma_alias(dev, PCI_DEVFN(PCI_SLOT(dev->devfn), 1), 1);
}

/*
 * Marvell 88SE9123 uses function 1 as the requester ID for DMA.  In some
 * SKUs function 1 is present and is a legacy IDE controller, in other
 * SKUs this function is not present, making this a ghost requester.
 * https://bugzilla.kernel.org/show_bug.cgi?id=42679
 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL_EXT, 0x9120,
			 quirk_dma_func1_alias);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL_EXT, 0x9123,
			 quirk_dma_func1_alias);
/* https://bugzilla.kernel.org/show_bug.cgi?id=42679#c136 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL_EXT, 0x9125,
			 quirk_dma_func1_alias);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL_EXT, 0x9128,
			 quirk_dma_func1_alias);
/* https://bugzilla.kernel.org/show_bug.cgi?id=42679#c14 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL_EXT, 0x9130,
			 quirk_dma_func1_alias);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL_EXT, 0x9170,
			 quirk_dma_func1_alias);
/* https://bugzilla.kernel.org/show_bug.cgi?id=42679#c47 + c57 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL_EXT, 0x9172,
			 quirk_dma_func1_alias);
/* https://bugzilla.kernel.org/show_bug.cgi?id=42679#c59 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL_EXT, 0x917a,
			 quirk_dma_func1_alias);
/* https://bugzilla.kernel.org/show_bug.cgi?id=42679#c78 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL_EXT, 0x9182,
			 quirk_dma_func1_alias);
/* https://bugzilla.kernel.org/show_bug.cgi?id=42679#c134 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL_EXT, 0x9183,
			 quirk_dma_func1_alias);
/* https://bugzilla.kernel.org/show_bug.cgi?id=42679#c46 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL_EXT, 0x91a0,
			 quirk_dma_func1_alias);
/* https://bugzilla.kernel.org/show_bug.cgi?id=42679#c135 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL_EXT, 0x9215,
			 quirk_dma_func1_alias);
/* https://bugzilla.kernel.org/show_bug.cgi?id=42679#c127 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL_EXT, 0x9220,
			 quirk_dma_func1_alias);
/* https://bugzilla.kernel.org/show_bug.cgi?id=42679#c49 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL_EXT, 0x9230,
			 quirk_dma_func1_alias);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL_EXT, 0x9235,
			 quirk_dma_func1_alias);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_TTI, 0x0642,
			 quirk_dma_func1_alias);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_TTI, 0x0645,
			 quirk_dma_func1_alias);
/* https://bugs.gentoo.org/show_bug.cgi?id=497630 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_JMICRON,
			 PCI_DEVICE_ID_JMICRON_JMB388_ESD,
			 quirk_dma_func1_alias);
/* https://bugzilla.kernel.org/show_bug.cgi?id=42679#c117 */
DECLARE_PCI_FIXUP_HEADER(0x1c28, /* Lite-On */
			 0x0122, /* Plextor M6E (Marvell 88SS9183)*/
			 quirk_dma_func1_alias);

/*
 * Some devices DMA with the wrong devfn, not just the wrong function.
 * quirk_fixed_dma_alias() uses this table to create fixed aliases, where
 * the alias is "fixed" and independent of the device devfn.
 *
 * For example, the Adaptec 3405 is a PCIe card with an Intel 80333 I/O
 * processor.  To software, this appears as a PCIe-to-PCI/X bridge with a
 * single device on the secondary bus.  In reality, the single exposed
 * device at 0e.0 is the Address Translation Unit (ATU) of the controller
 * that provides a bridge to the internal bus of the I/O processor.  The
 * controller supports private devices, which can be hidden from PCI config
 * space.  In the case of the Adaptec 3405, a private device at 01.0
 * appears to be the DMA engine, which therefore needs to become a DMA
 * alias for the device.
 */
static const struct pci_device_id fixed_dma_alias_tbl[] = {
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x0285,
			 PCI_VENDOR_ID_ADAPTEC2, 0x02bb), /* Adaptec 3405 */
	  .driver_data = PCI_DEVFN(1, 0) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_ADAPTEC2, 0x0285,
			 PCI_VENDOR_ID_ADAPTEC2, 0x02bc), /* Adaptec 3805 */
	  .driver_data = PCI_DEVFN(1, 0) },
	{ 0 }
};

static void quirk_fixed_dma_alias(struct pci_dev *dev)
{
	const struct pci_device_id *id;

	id = pci_match_id(fixed_dma_alias_tbl, dev);
	if (id)
		pci_add_dma_alias(dev, id->driver_data, 1);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_ADAPTEC2, 0x0285, quirk_fixed_dma_alias);

/*
 * A few PCIe-to-PCI bridges fail to expose a PCIe capability, resulting in
 * using the wrong DMA alias for the device.  Some of these devices can be
 * used as either forward or reverse bridges, so we need to test whether the
 * device is operating in the correct mode.  We could probably apply this
 * quirk to PCI_ANY_ID, but for now we'll just use known offenders.  The test
 * is for a non-root, non-PCIe bridge where the upstream device is PCIe and
 * is not a PCIe-to-PCI bridge, then @pdev is actually a PCIe-to-PCI bridge.
 */
static void quirk_use_pcie_bridge_dma_alias(struct pci_dev *pdev)
{
	if (!pci_is_root_bus(pdev->bus) &&
	    pdev->hdr_type == PCI_HEADER_TYPE_BRIDGE &&
	    !pci_is_pcie(pdev) && pci_is_pcie(pdev->bus->self) &&
	    pci_pcie_type(pdev->bus->self) != PCI_EXP_TYPE_PCI_BRIDGE)
		pdev->dev_flags |= PCI_DEV_FLAG_PCIE_BRIDGE_ALIAS;
}
/* ASM1083/1085, https://bugzilla.kernel.org/show_bug.cgi?id=44881#c46 */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_ASMEDIA, 0x1080,
			 quirk_use_pcie_bridge_dma_alias);
/* Tundra 8113, https://bugzilla.kernel.org/show_bug.cgi?id=44881#c43 */
DECLARE_PCI_FIXUP_HEADER(0x10e3, 0x8113, quirk_use_pcie_bridge_dma_alias);
/* ITE 8892, https://bugzilla.kernel.org/show_bug.cgi?id=73551 */
DECLARE_PCI_FIXUP_HEADER(0x1283, 0x8892, quirk_use_pcie_bridge_dma_alias);
/* ITE 8893 has the same problem as the 8892 */
DECLARE_PCI_FIXUP_HEADER(0x1283, 0x8893, quirk_use_pcie_bridge_dma_alias);
/* Intel 82801, https://bugzilla.kernel.org/show_bug.cgi?id=44881#c49 */
DECLARE_PCI_FIXUP_HEADER(0x8086, 0x244e, quirk_use_pcie_bridge_dma_alias);

/*
 * MIC x200 NTB forwards PCIe traffic using multiple alien RIDs. They have to
 * be added as aliases to the DMA device in order to allow buffer access
 * when IOMMU is enabled. Following devfns have to match RIT-LUT table
 * programmed in the EEPROM.
 */
static void quirk_mic_x200_dma_alias(struct pci_dev *pdev)
{
	pci_add_dma_alias(pdev, PCI_DEVFN(0x10, 0x0), 1);
	pci_add_dma_alias(pdev, PCI_DEVFN(0x11, 0x0), 1);
	pci_add_dma_alias(pdev, PCI_DEVFN(0x12, 0x3), 1);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x2260, quirk_mic_x200_dma_alias);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x2264, quirk_mic_x200_dma_alias);

/*
 * Intel Visual Compute Accelerator (VCA) is a family of PCIe add-in devices
 * exposing computational units via Non Transparent Bridges (NTB, PEX 87xx).
 *
 * Similarly to MIC x200, we need to add DMA aliases to allow buffer access
 * when IOMMU is enabled.  These aliases allow computational unit access to
 * host memory.  These aliases mark the whole VCA device as one IOMMU
 * group.
 *
 * All possible slot numbers (0x20) are used, since we are unable to tell
 * what slot is used on other side.  This quirk is intended for both host
 * and computational unit sides.  The VCA devices have up to five functions
 * (four for DMA channels and one additional).
 */
static void quirk_pex_vca_alias(struct pci_dev *pdev)
{
	const unsigned int num_pci_slots = 0x20;
	unsigned int slot;

	for (slot = 0; slot < num_pci_slots; slot++)
		pci_add_dma_alias(pdev, PCI_DEVFN(slot, 0x0), 5);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x2954, quirk_pex_vca_alias);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x2955, quirk_pex_vca_alias);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x2956, quirk_pex_vca_alias);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x2958, quirk_pex_vca_alias);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x2959, quirk_pex_vca_alias);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x295A, quirk_pex_vca_alias);

/*
 * The IOMMU and interrupt controller on Broadcom Vulcan/Cavium ThunderX2 are
 * associated not at the root bus, but at a bridge below. This quirk avoids
 * generating invalid DMA aliases.
 */
static void quirk_bridge_cavm_thrx2_pcie_root(struct pci_dev *pdev)
{
	pdev->dev_flags |= PCI_DEV_FLAGS_BRIDGE_XLATE_ROOT;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_BROADCOM, 0x9000,
				quirk_bridge_cavm_thrx2_pcie_root);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_BROADCOM, 0x9084,
				quirk_bridge_cavm_thrx2_pcie_root);

/*
 * Intersil/Techwell TW686[4589]-based video capture cards have an empty (zero)
 * class code.  Fix it.
 */
static void quirk_tw686x_class(struct pci_dev *pdev)
{
	u32 class = pdev->class;

	/* Use "Multimedia controller" class */
	pdev->class = (PCI_CLASS_MULTIMEDIA_OTHER << 8) | 0x01;
	pci_info(pdev, "TW686x PCI class overridden (%#08x -> %#08x)\n",
		 class, pdev->class);
}
DECLARE_PCI_FIXUP_CLASS_EARLY(0x1797, 0x6864, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_tw686x_class);
DECLARE_PCI_FIXUP_CLASS_EARLY(0x1797, 0x6865, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_tw686x_class);
DECLARE_PCI_FIXUP_CLASS_EARLY(0x1797, 0x6868, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_tw686x_class);
DECLARE_PCI_FIXUP_CLASS_EARLY(0x1797, 0x6869, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_tw686x_class);

/*
 * Some devices have problems with Transaction Layer Packets with the Relaxed
 * Ordering Attribute set.  Such devices should mark themselves and other
 * device drivers should check before sending TLPs with RO set.
 */
static void quirk_relaxedordering_disable(struct pci_dev *dev)
{
	dev->dev_flags |= PCI_DEV_FLAGS_NO_RELAXED_ORDERING;
	pci_info(dev, "Disable Relaxed Ordering Attributes to avoid PCIe Completion erratum\n");
}

/*
 * Intel Xeon processors based on Broadwell/Haswell microarchitecture Root
 * Complex have a Flow Control Credit issue which can cause performance
 * problems with Upstream Transaction Layer Packets with Relaxed Ordering set.
 */
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x6f01, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x6f02, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x6f03, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x6f04, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x6f05, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x6f06, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x6f07, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x6f08, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x6f09, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x6f0a, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x6f0b, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x6f0c, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x6f0d, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x6f0e, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x2f01, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x2f02, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x2f03, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x2f04, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x2f05, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x2f06, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x2f07, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x2f08, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x2f09, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x2f0a, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x2f0b, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x2f0c, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x2f0d, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, 0x2f0e, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);

/*
 * The AMD ARM A1100 (aka "SEATTLE") SoC has a bug in its PCIe Root Complex
 * where Upstream Transaction Layer Packets with the Relaxed Ordering
 * Attribute clear are allowed to bypass earlier TLPs with Relaxed Ordering
 * set.  This is a violation of the PCIe 3.0 Transaction Ordering Rules
 * outlined in Section 2.4.1 (PCI Express(r) Base Specification Revision 3.0
 * November 10, 2010).  As a result, on this platform we can't use Relaxed
 * Ordering for Upstream TLPs.
 */
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_AMD, 0x1a00, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_AMD, 0x1a01, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_AMD, 0x1a02, PCI_CLASS_NOT_DEFINED, 8,
			      quirk_relaxedordering_disable);

/*
 * Per PCIe r3.0, sec 2.2.9, "Completion headers must supply the same
 * values for the Attribute as were supplied in the header of the
 * corresponding Request, except as explicitly allowed when IDO is used."
 *
 * If a non-compliant device generates a completion with a different
 * attribute than the request, the receiver may accept it (which itself
 * seems non-compliant based on sec 2.3.2), or it may handle it as a
 * Malformed TLP or an Unexpected Completion, which will probably lead to a
 * device access timeout.
 *
 * If the non-compliant device generates completions with zero attributes
 * (instead of copying the attributes from the request), we can work around
 * this by disabling the "Relaxed Ordering" and "No Snoop" attributes in
 * upstream devices so they always generate requests with zero attributes.
 *
 * This affects other devices under the same Root Port, but since these
 * attributes are performance hints, there should be no functional problem.
 *
 * Note that Configuration Space accesses are never supposed to have TLP
 * Attributes, so we're safe waiting till after any Configuration Space
 * accesses to do the Root Port fixup.
 */
static void quirk_disable_root_port_attributes(struct pci_dev *pdev)
{
	struct pci_dev *root_port = pcie_find_root_port(pdev);

	if (!root_port) {
		pci_warn(pdev, "PCIe Completion erratum may cause device errors\n");
		return;
	}

	pci_info(root_port, "Disabling No Snoop/Relaxed Ordering Attributes to avoid PCIe Completion erratum in %s\n",
		 dev_name(&pdev->dev));
	pcie_capability_clear_word(root_port, PCI_EXP_DEVCTL,
				   PCI_EXP_DEVCTL_RELAX_EN |
				   PCI_EXP_DEVCTL_NOSNOOP_EN);
}

/*
 * The Chelsio T5 chip fails to copy TLP Attributes from a Request to the
 * Completion it generates.
 */
static void quirk_chelsio_T5_disable_root_port_attributes(struct pci_dev *pdev)
{
	/*
	 * This mask/compare operation selects for Physical Function 4 on a
	 * T5.  We only need to fix up the Root Port once for any of the
	 * PFs.  PF[0..3] have PCI Device IDs of 0x50xx, but PF4 is uniquely
	 * 0x54xx so we use that one.
	 */
	if ((pdev->device & 0xff00) == 0x5400)
		quirk_disable_root_port_attributes(pdev);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_CHELSIO, PCI_ANY_ID,
			 quirk_chelsio_T5_disable_root_port_attributes);

/*
 * pci_acs_ctrl_enabled - compare desired ACS controls with those provided
 *			  by a device
 * @acs_ctrl_req: Bitmask of desired ACS controls
 * @acs_ctrl_ena: Bitmask of ACS controls enabled or provided implicitly by
 *		  the hardware design
 *
 * Return 1 if all ACS controls in the @acs_ctrl_req bitmask are included
 * in @acs_ctrl_ena, i.e., the device provides all the access controls the
 * caller desires.  Return 0 otherwise.
 */
static int pci_acs_ctrl_enabled(u16 acs_ctrl_req, u16 acs_ctrl_ena)
{
	if ((acs_ctrl_req & acs_ctrl_ena) == acs_ctrl_req)
		return 1;
	return 0;
}

/*
 * AMD has indicated that the devices below do not support peer-to-peer
 * in any system where they are found in the southbridge with an AMD
 * IOMMU in the system.  Multifunction devices that do not support
 * peer-to-peer between functions can claim to support a subset of ACS.
 * Such devices effectively enable request redirect (RR) and completion
 * redirect (CR) since all transactions are redirected to the upstream
 * root complex.
 *
 * https://lore.kernel.org/r/201207111426.q6BEQTbh002928@mail.maya.org/
 * https://lore.kernel.org/r/20120711165854.GM25282@amd.com/
 * https://lore.kernel.org/r/20121005130857.GX4009@amd.com/
 *
 * 1002:4385 SBx00 SMBus Controller
 * 1002:439c SB7x0/SB8x0/SB9x0 IDE Controller
 * 1002:4383 SBx00 Azalia (Intel HDA)
 * 1002:439d SB7x0/SB8x0/SB9x0 LPC host controller
 * 1002:4384 SBx00 PCI to PCI Bridge
 * 1002:4399 SB7x0/SB8x0/SB9x0 USB OHCI2 Controller
 *
 * https://bugzilla.kernel.org/show_bug.cgi?id=81841#c15
 *
 * 1022:780f [AMD] FCH PCI Bridge
 * 1022:7809 [AMD] FCH USB OHCI Controller
 */
static int pci_quirk_amd_sb_acs(struct pci_dev *dev, u16 acs_flags)
{
#ifdef CONFIG_ACPI
	struct acpi_table_header *header = NULL;
	acpi_status status;

	/* Targeting multifunction devices on the SB (appears on root bus) */
	if (!dev->multifunction || !pci_is_root_bus(dev->bus))
		return -ENODEV;

	/* The IVRS table describes the AMD IOMMU */
	status = acpi_get_table("IVRS", 0, &header);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	acpi_put_table(header);

	/* Filter out flags not applicable to multifunction */
	acs_flags &= (PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_EC | PCI_ACS_DT);

	return pci_acs_ctrl_enabled(acs_flags, PCI_ACS_RR | PCI_ACS_CR);
#else
	return -ENODEV;
#endif
}

static bool pci_quirk_cavium_acs_match(struct pci_dev *dev)
{
	if (!pci_is_pcie(dev) || pci_pcie_type(dev) != PCI_EXP_TYPE_ROOT_PORT)
		return false;

	switch (dev->device) {
	/*
	 * Effectively selects all downstream ports for whole ThunderX1
	 * (which represents 8 SoCs).
	 */
	case 0xa000 ... 0xa7ff: /* ThunderX1 */
	case 0xaf84:  /* ThunderX2 */
	case 0xb884:  /* ThunderX3 */
		return true;
	default:
		return false;
	}
}

static int pci_quirk_cavium_acs(struct pci_dev *dev, u16 acs_flags)
{
	if (!pci_quirk_cavium_acs_match(dev))
		return -ENOTTY;

	/*
	 * Cavium Root Ports don't advertise an ACS capability.  However,
	 * the RTL internally implements similar protection as if ACS had
	 * Source Validation, Request Redirection, Completion Redirection,
	 * and Upstream Forwarding features enabled.  Assert that the
	 * hardware implements and enables equivalent ACS functionality for
	 * these flags.
	 */
	return pci_acs_ctrl_enabled(acs_flags,
		PCI_ACS_SV | PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_UF);
}

static int pci_quirk_xgene_acs(struct pci_dev *dev, u16 acs_flags)
{
	/*
	 * X-Gene Root Ports matching this quirk do not allow peer-to-peer
	 * transactions with others, allowing masking out these bits as if they
	 * were unimplemented in the ACS capability.
	 */
	return pci_acs_ctrl_enabled(acs_flags,
		PCI_ACS_SV | PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_UF);
}

/*
 * Many Zhaoxin Root Ports and Switch Downstream Ports have no ACS capability.
 * But the implementation could block peer-to-peer transactions between them
 * and provide ACS-like functionality.
 */
static int pci_quirk_zhaoxin_pcie_ports_acs(struct pci_dev *dev, u16 acs_flags)
{
	if (!pci_is_pcie(dev) ||
	    ((pci_pcie_type(dev) != PCI_EXP_TYPE_ROOT_PORT) &&
	     (pci_pcie_type(dev) != PCI_EXP_TYPE_DOWNSTREAM)))
		return -ENOTTY;

	/*
	 * Future Zhaoxin Root Ports and Switch Downstream Ports will
	 * implement ACS capability in accordance with the PCIe Spec.
	 */
	switch (dev->device) {
	case 0x0710 ... 0x071e:
	case 0x0721:
	case 0x0723 ... 0x0752:
		return pci_acs_ctrl_enabled(acs_flags,
			PCI_ACS_SV | PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_UF);
	}

	return false;
}

/*
 * Many Intel PCH Root Ports do provide ACS-like features to disable peer
 * transactions and validate bus numbers in requests, but do not provide an
 * actual PCIe ACS capability.  This is the list of device IDs known to fall
 * into that category as provided by Intel in Red Hat bugzilla 1037684.
 */
static const u16 pci_quirk_intel_pch_acs_ids[] = {
	/* Ibexpeak PCH */
	0x3b42, 0x3b43, 0x3b44, 0x3b45, 0x3b46, 0x3b47, 0x3b48, 0x3b49,
	0x3b4a, 0x3b4b, 0x3b4c, 0x3b4d, 0x3b4e, 0x3b4f, 0x3b50, 0x3b51,
	/* Cougarpoint PCH */
	0x1c10, 0x1c11, 0x1c12, 0x1c13, 0x1c14, 0x1c15, 0x1c16, 0x1c17,
	0x1c18, 0x1c19, 0x1c1a, 0x1c1b, 0x1c1c, 0x1c1d, 0x1c1e, 0x1c1f,
	/* Pantherpoint PCH */
	0x1e10, 0x1e11, 0x1e12, 0x1e13, 0x1e14, 0x1e15, 0x1e16, 0x1e17,
	0x1e18, 0x1e19, 0x1e1a, 0x1e1b, 0x1e1c, 0x1e1d, 0x1e1e, 0x1e1f,
	/* Lynxpoint-H PCH */
	0x8c10, 0x8c11, 0x8c12, 0x8c13, 0x8c14, 0x8c15, 0x8c16, 0x8c17,
	0x8c18, 0x8c19, 0x8c1a, 0x8c1b, 0x8c1c, 0x8c1d, 0x8c1e, 0x8c1f,
	/* Lynxpoint-LP PCH */
	0x9c10, 0x9c11, 0x9c12, 0x9c13, 0x9c14, 0x9c15, 0x9c16, 0x9c17,
	0x9c18, 0x9c19, 0x9c1a, 0x9c1b,
	/* Wildcat PCH */
	0x9c90, 0x9c91, 0x9c92, 0x9c93, 0x9c94, 0x9c95, 0x9c96, 0x9c97,
	0x9c98, 0x9c99, 0x9c9a, 0x9c9b,
	/* Patsburg (X79) PCH */
	0x1d10, 0x1d12, 0x1d14, 0x1d16, 0x1d18, 0x1d1a, 0x1d1c, 0x1d1e,
	/* Wellsburg (X99) PCH */
	0x8d10, 0x8d11, 0x8d12, 0x8d13, 0x8d14, 0x8d15, 0x8d16, 0x8d17,
	0x8d18, 0x8d19, 0x8d1a, 0x8d1b, 0x8d1c, 0x8d1d, 0x8d1e,
	/* Lynx Point (9 series) PCH */
	0x8c90, 0x8c92, 0x8c94, 0x8c96, 0x8c98, 0x8c9a, 0x8c9c, 0x8c9e,
};

static bool pci_quirk_intel_pch_acs_match(struct pci_dev *dev)
{
	int i;

	/* Filter out a few obvious non-matches first */
	if (!pci_is_pcie(dev) || pci_pcie_type(dev) != PCI_EXP_TYPE_ROOT_PORT)
		return false;

	for (i = 0; i < ARRAY_SIZE(pci_quirk_intel_pch_acs_ids); i++)
		if (pci_quirk_intel_pch_acs_ids[i] == dev->device)
			return true;

	return false;
}

static int pci_quirk_intel_pch_acs(struct pci_dev *dev, u16 acs_flags)
{
	if (!pci_quirk_intel_pch_acs_match(dev))
		return -ENOTTY;

	if (dev->dev_flags & PCI_DEV_FLAGS_ACS_ENABLED_QUIRK)
		return pci_acs_ctrl_enabled(acs_flags,
			PCI_ACS_SV | PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_UF);

	return pci_acs_ctrl_enabled(acs_flags, 0);
}

/*
 * These QCOM Root Ports do provide ACS-like features to disable peer
 * transactions and validate bus numbers in requests, but do not provide an
 * actual PCIe ACS capability.  Hardware supports source validation but it
 * will report the issue as Completer Abort instead of ACS Violation.
 * Hardware doesn't support peer-to-peer and each Root Port is a Root
 * Complex with unique segment numbers.  It is not possible for one Root
 * Port to pass traffic to another Root Port.  All PCIe transactions are
 * terminated inside the Root Port.
 */
static int pci_quirk_qcom_rp_acs(struct pci_dev *dev, u16 acs_flags)
{
	return pci_acs_ctrl_enabled(acs_flags,
		PCI_ACS_SV | PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_UF);
}

/*
 * Each of these NXP Root Ports is in a Root Complex with a unique segment
 * number and does provide isolation features to disable peer transactions
 * and validate bus numbers in requests, but does not provide an ACS
 * capability.
 */
static int pci_quirk_nxp_rp_acs(struct pci_dev *dev, u16 acs_flags)
{
	return pci_acs_ctrl_enabled(acs_flags,
		PCI_ACS_SV | PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_UF);
}

static int pci_quirk_al_acs(struct pci_dev *dev, u16 acs_flags)
{
	if (pci_pcie_type(dev) != PCI_EXP_TYPE_ROOT_PORT)
		return -ENOTTY;

	/*
	 * Amazon's Annapurna Labs root ports don't include an ACS capability,
	 * but do include ACS-like functionality. The hardware doesn't support
	 * peer-to-peer transactions via the root port and each has a unique
	 * segment number.
	 *
	 * Additionally, the root ports cannot send traffic to each other.
	 */
	acs_flags &= ~(PCI_ACS_SV | PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_UF);

	return acs_flags ? 0 : 1;
}

/*
 * Sunrise Point PCH root ports implement ACS, but unfortunately as shown in
 * the datasheet (Intel 100 Series Chipset Family PCH Datasheet, Vol. 2,
 * 12.1.46, 12.1.47)[1] this chipset uses dwords for the ACS capability and
 * control registers whereas the PCIe spec packs them into words (Rev 3.0,
 * 7.16 ACS Extended Capability).  The bit definitions are correct, but the
 * control register is at offset 8 instead of 6 and we should probably use
 * dword accesses to them.  This applies to the following PCI Device IDs, as
 * found in volume 1 of the datasheet[2]:
 *
 * 0xa110-0xa11f Sunrise Point-H PCI Express Root Port #{0-16}
 * 0xa167-0xa16a Sunrise Point-H PCI Express Root Port #{17-20}
 *
 * N.B. This doesn't fix what lspci shows.
 *
 * The 100 series chipset specification update includes this as errata #23[3].
 *
 * The 200 series chipset (Union Point) has the same bug according to the
 * specification update (Intel 200 Series Chipset Family Platform Controller
 * Hub, Specification Update, January 2017, Revision 001, Document# 335194-001,
 * Errata 22)[4].  Per the datasheet[5], root port PCI Device IDs for this
 * chipset include:
 *
 * 0xa290-0xa29f PCI Express Root port #{0-16}
 * 0xa2e7-0xa2ee PCI Express Root port #{17-24}
 *
 * Mobile chipsets are also affected, 7th & 8th Generation
 * Specification update confirms ACS errata 22, status no fix: (7th Generation
 * Intel Processor Family I/O for U/Y Platforms and 8th Generation Intel
 * Processor Family I/O for U Quad Core Platforms Specification Update,
 * August 2017, Revision 002, Document#: 334660-002)[6]
 * Device IDs from I/O datasheet: (7th Generation Intel Processor Family I/O
 * for U/Y Platforms and 8th Generation Intel ® Processor Family I/O for U
 * Quad Core Platforms, Vol 1 of 2, August 2017, Document#: 334658-003)[7]
 *
 * 0x9d10-0x9d1b PCI Express Root port #{1-12}
 *
 * [1] https://www.intel.com/content/www/us/en/chipsets/100-series-chipset-datasheet-vol-2.html
 * [2] https://www.intel.com/content/www/us/en/chipsets/100-series-chipset-datasheet-vol-1.html
 * [3] https://www.intel.com/content/www/us/en/chipsets/100-series-chipset-spec-update.html
 * [4] https://www.intel.com/content/www/us/en/chipsets/200-series-chipset-pch-spec-update.html
 * [5] https://www.intel.com/content/www/us/en/chipsets/200-series-chipset-pch-datasheet-vol-1.html
 * [6] https://www.intel.com/content/www/us/en/processors/core/7th-gen-core-family-mobile-u-y-processor-lines-i-o-spec-update.html
 * [7] https://www.intel.com/content/www/us/en/processors/core/7th-gen-core-family-mobile-u-y-processor-lines-i-o-datasheet-vol-1.html
 */
static bool pci_quirk_intel_spt_pch_acs_match(struct pci_dev *dev)
{
	if (!pci_is_pcie(dev) || pci_pcie_type(dev) != PCI_EXP_TYPE_ROOT_PORT)
		return false;

	switch (dev->device) {
	case 0xa110 ... 0xa11f: case 0xa167 ... 0xa16a: /* Sunrise Point */
	case 0xa290 ... 0xa29f: case 0xa2e7 ... 0xa2ee: /* Union Point */
	case 0x9d10 ... 0x9d1b: /* 7th & 8th Gen Mobile */
		return true;
	}

	return false;
}

#define INTEL_SPT_ACS_CTRL (PCI_ACS_CAP + 4)

static int pci_quirk_intel_spt_pch_acs(struct pci_dev *dev, u16 acs_flags)
{
	int pos;
	u32 cap, ctrl;

	if (!pci_quirk_intel_spt_pch_acs_match(dev))
		return -ENOTTY;

	pos = dev->acs_cap;
	if (!pos)
		return -ENOTTY;

	/* see pci_acs_flags_enabled() */
	pci_read_config_dword(dev, pos + PCI_ACS_CAP, &cap);
	acs_flags &= (cap | PCI_ACS_EC);

	pci_read_config_dword(dev, pos + INTEL_SPT_ACS_CTRL, &ctrl);

	return pci_acs_ctrl_enabled(acs_flags, ctrl);
}

static int pci_quirk_mf_endpoint_acs(struct pci_dev *dev, u16 acs_flags)
{
	/*
	 * SV, TB, and UF are not relevant to multifunction endpoints.
	 *
	 * Multifunction devices are only required to implement RR, CR, and DT
	 * in their ACS capability if they support peer-to-peer transactions.
	 * Devices matching this quirk have been verified by the vendor to not
	 * perform peer-to-peer with other functions, allowing us to mask out
	 * these bits as if they were unimplemented in the ACS capability.
	 */
	return pci_acs_ctrl_enabled(acs_flags,
		PCI_ACS_SV | PCI_ACS_TB | PCI_ACS_RR |
		PCI_ACS_CR | PCI_ACS_UF | PCI_ACS_DT);
}

static int pci_quirk_rciep_acs(struct pci_dev *dev, u16 acs_flags)
{
	/*
	 * Intel RCiEP's are required to allow p2p only on translated
	 * addresses.  Refer to Intel VT-d specification, r3.1, sec 3.16,
	 * "Root-Complex Peer to Peer Considerations".
	 */
	if (pci_pcie_type(dev) != PCI_EXP_TYPE_RC_END)
		return -ENOTTY;

	return pci_acs_ctrl_enabled(acs_flags,
		PCI_ACS_SV | PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_UF);
}

static int pci_quirk_brcm_acs(struct pci_dev *dev, u16 acs_flags)
{
	/*
	 * iProc PAXB Root Ports don't advertise an ACS capability, but
	 * they do not allow peer-to-peer transactions between Root Ports.
	 * Allow each Root Port to be in a separate IOMMU group by masking
	 * SV/RR/CR/UF bits.
	 */
	return pci_acs_ctrl_enabled(acs_flags,
		PCI_ACS_SV | PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_UF);
}

/*
 * Wangxun 40G/25G/10G/1G NICs have no ACS capability, but on
 * multi-function devices, the hardware isolates the functions by
 * directing all peer-to-peer traffic upstream as though PCI_ACS_RR and
 * PCI_ACS_CR were set.
 * SFxxx 1G NICs(em).
 * RP1000/RP2000 10G NICs(sp).
 * FF5xxx 40G/25G/10G NICs(aml).
 */
static int  pci_quirk_wangxun_nic_acs(struct pci_dev *dev, u16 acs_flags)
{
	switch (dev->device) {
	case 0x0100 ... 0x010F: /* EM */
	case 0x1001: case 0x2001: /* SP */
	case 0x5010: case 0x5025: case 0x5040: /* AML */
	case 0x5110: case 0x5125: case 0x5140: /* AML */
		return pci_acs_ctrl_enabled(acs_flags,
			PCI_ACS_SV | PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_UF);
	}

	return false;
}

static const struct pci_dev_acs_enabled {
	u16 vendor;
	u16 device;
	int (*acs_enabled)(struct pci_dev *dev, u16 acs_flags);
} pci_dev_acs_enabled[] = {
	{ PCI_VENDOR_ID_ATI, 0x4385, pci_quirk_amd_sb_acs },
	{ PCI_VENDOR_ID_ATI, 0x439c, pci_quirk_amd_sb_acs },
	{ PCI_VENDOR_ID_ATI, 0x4383, pci_quirk_amd_sb_acs },
	{ PCI_VENDOR_ID_ATI, 0x439d, pci_quirk_amd_sb_acs },
	{ PCI_VENDOR_ID_ATI, 0x4384, pci_quirk_amd_sb_acs },
	{ PCI_VENDOR_ID_ATI, 0x4399, pci_quirk_amd_sb_acs },
	{ PCI_VENDOR_ID_AMD, 0x780f, pci_quirk_amd_sb_acs },
	{ PCI_VENDOR_ID_AMD, 0x7809, pci_quirk_amd_sb_acs },
	{ PCI_VENDOR_ID_SOLARFLARE, 0x0903, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_SOLARFLARE, 0x0923, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_SOLARFLARE, 0x0A03, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10C6, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10DB, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10DD, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10E1, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10F1, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10F7, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10F8, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10F9, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10FA, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10FB, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10FC, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x1507, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x1514, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x151C, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x1529, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x152A, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x154D, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x154F, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x1551, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x1558, pci_quirk_mf_endpoint_acs },
	/* 82580 */
	{ PCI_VENDOR_ID_INTEL, 0x1509, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x150E, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x150F, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x1510, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x1511, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x1516, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x1527, pci_quirk_mf_endpoint_acs },
	/* 82576 */
	{ PCI_VENDOR_ID_INTEL, 0x10C9, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10E6, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10E7, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10E8, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x150A, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x150D, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x1518, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x1526, pci_quirk_mf_endpoint_acs },
	/* 82575 */
	{ PCI_VENDOR_ID_INTEL, 0x10A7, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10A9, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10D6, pci_quirk_mf_endpoint_acs },
	/* I350 */
	{ PCI_VENDOR_ID_INTEL, 0x1521, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x1522, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x1523, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x1524, pci_quirk_mf_endpoint_acs },
	/* 82571 (Quads omitted due to non-ACS switch) */
	{ PCI_VENDOR_ID_INTEL, 0x105E, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x105F, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x1060, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x10D9, pci_quirk_mf_endpoint_acs },
	/* I219 */
	{ PCI_VENDOR_ID_INTEL, 0x15b7, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, 0x15b8, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_INTEL, PCI_ANY_ID, pci_quirk_rciep_acs },
	/* QCOM QDF2xxx root ports */
	{ PCI_VENDOR_ID_QCOM, 0x0400, pci_quirk_qcom_rp_acs },
	{ PCI_VENDOR_ID_QCOM, 0x0401, pci_quirk_qcom_rp_acs },
	/* QCOM SA8775P root port */
	{ PCI_VENDOR_ID_QCOM, 0x0115, pci_quirk_qcom_rp_acs },
	/* HXT SD4800 root ports. The ACS design is same as QCOM QDF2xxx */
	{ PCI_VENDOR_ID_HXT, 0x0401, pci_quirk_qcom_rp_acs },
	/* Intel PCH root ports */
	{ PCI_VENDOR_ID_INTEL, PCI_ANY_ID, pci_quirk_intel_pch_acs },
	{ PCI_VENDOR_ID_INTEL, PCI_ANY_ID, pci_quirk_intel_spt_pch_acs },
	{ 0x19a2, 0x710, pci_quirk_mf_endpoint_acs }, /* Emulex BE3-R */
	{ 0x10df, 0x720, pci_quirk_mf_endpoint_acs }, /* Emulex Skyhawk-R */
	/* Cavium ThunderX */
	{ PCI_VENDOR_ID_CAVIUM, PCI_ANY_ID, pci_quirk_cavium_acs },
	/* Cavium multi-function devices */
	{ PCI_VENDOR_ID_CAVIUM, 0xA026, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_CAVIUM, 0xA059, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_CAVIUM, 0xA060, pci_quirk_mf_endpoint_acs },
	/* APM X-Gene */
	{ PCI_VENDOR_ID_AMCC, 0xE004, pci_quirk_xgene_acs },
	/* Ampere Computing */
	{ PCI_VENDOR_ID_AMPERE, 0xE005, pci_quirk_xgene_acs },
	{ PCI_VENDOR_ID_AMPERE, 0xE006, pci_quirk_xgene_acs },
	{ PCI_VENDOR_ID_AMPERE, 0xE007, pci_quirk_xgene_acs },
	{ PCI_VENDOR_ID_AMPERE, 0xE008, pci_quirk_xgene_acs },
	{ PCI_VENDOR_ID_AMPERE, 0xE009, pci_quirk_xgene_acs },
	{ PCI_VENDOR_ID_AMPERE, 0xE00A, pci_quirk_xgene_acs },
	{ PCI_VENDOR_ID_AMPERE, 0xE00B, pci_quirk_xgene_acs },
	{ PCI_VENDOR_ID_AMPERE, 0xE00C, pci_quirk_xgene_acs },
	/* Broadcom multi-function device */
	{ PCI_VENDOR_ID_BROADCOM, 0x16D7, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_BROADCOM, 0x1750, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_BROADCOM, 0x1751, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_BROADCOM, 0x1752, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_BROADCOM, 0x1760, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_BROADCOM, 0x1761, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_BROADCOM, 0x1762, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_BROADCOM, 0x1763, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_BROADCOM, 0xD714, pci_quirk_brcm_acs },
	/* Amazon Annapurna Labs */
	{ PCI_VENDOR_ID_AMAZON_ANNAPURNA_LABS, 0x0031, pci_quirk_al_acs },
	/* Zhaoxin multi-function devices */
	{ PCI_VENDOR_ID_ZHAOXIN, 0x3038, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_ZHAOXIN, 0x3104, pci_quirk_mf_endpoint_acs },
	{ PCI_VENDOR_ID_ZHAOXIN, 0x9083, pci_quirk_mf_endpoint_acs },
	/* NXP root ports, xx=16, 12, or 08 cores */
	/* LX2xx0A : without security features + CAN-FD */
	{ PCI_VENDOR_ID_NXP, 0x8d81, pci_quirk_nxp_rp_acs },
	{ PCI_VENDOR_ID_NXP, 0x8da1, pci_quirk_nxp_rp_acs },
	{ PCI_VENDOR_ID_NXP, 0x8d83, pci_quirk_nxp_rp_acs },
	/* LX2xx0C : security features + CAN-FD */
	{ PCI_VENDOR_ID_NXP, 0x8d80, pci_quirk_nxp_rp_acs },
	{ PCI_VENDOR_ID_NXP, 0x8da0, pci_quirk_nxp_rp_acs },
	{ PCI_VENDOR_ID_NXP, 0x8d82, pci_quirk_nxp_rp_acs },
	/* LX2xx0E : security features + CAN */
	{ PCI_VENDOR_ID_NXP, 0x8d90, pci_quirk_nxp_rp_acs },
	{ PCI_VENDOR_ID_NXP, 0x8db0, pci_quirk_nxp_rp_acs },
	{ PCI_VENDOR_ID_NXP, 0x8d92, pci_quirk_nxp_rp_acs },
	/* LX2xx0N : without security features + CAN */
	{ PCI_VENDOR_ID_NXP, 0x8d91, pci_quirk_nxp_rp_acs },
	{ PCI_VENDOR_ID_NXP, 0x8db1, pci_quirk_nxp_rp_acs },
	{ PCI_VENDOR_ID_NXP, 0x8d93, pci_quirk_nxp_rp_acs },
	/* LX2xx2A : without security features + CAN-FD */
	{ PCI_VENDOR_ID_NXP, 0x8d89, pci_quirk_nxp_rp_acs },
	{ PCI_VENDOR_ID_NXP, 0x8da9, pci_quirk_nxp_rp_acs },
	{ PCI_VENDOR_ID_NXP, 0x8d8b, pci_quirk_nxp_rp_acs },
	/* LX2xx2C : security features + CAN-FD */
	{ PCI_VENDOR_ID_NXP, 0x8d88, pci_quirk_nxp_rp_acs },
	{ PCI_VENDOR_ID_NXP, 0x8da8, pci_quirk_nxp_rp_acs },
	{ PCI_VENDOR_ID_NXP, 0x8d8a, pci_quirk_nxp_rp_acs },
	/* LX2xx2E : security features + CAN */
	{ PCI_VENDOR_ID_NXP, 0x8d98, pci_quirk_nxp_rp_acs },
	{ PCI_VENDOR_ID_NXP, 0x8db8, pci_quirk_nxp_rp_acs },
	{ PCI_VENDOR_ID_NXP, 0x8d9a, pci_quirk_nxp_rp_acs },
	/* LX2xx2N : without security features + CAN */
	{ PCI_VENDOR_ID_NXP, 0x8d99, pci_quirk_nxp_rp_acs },
	{ PCI_VENDOR_ID_NXP, 0x8db9, pci_quirk_nxp_rp_acs },
	{ PCI_VENDOR_ID_NXP, 0x8d9b, pci_quirk_nxp_rp_acs },
	/* Zhaoxin Root/Downstream Ports */
	{ PCI_VENDOR_ID_ZHAOXIN, PCI_ANY_ID, pci_quirk_zhaoxin_pcie_ports_acs },
	/* Wangxun nics */
	{ PCI_VENDOR_ID_WANGXUN, PCI_ANY_ID, pci_quirk_wangxun_nic_acs },
	{ 0 }
};

/*
 * pci_dev_specific_acs_enabled - check whether device provides ACS controls
 * @dev:	PCI device
 * @acs_flags:	Bitmask of desired ACS controls
 *
 * Returns:
 *   -ENOTTY:	No quirk applies to this device; we can't tell whether the
 *		device provides the desired controls
 *   0:		Device does not provide all the desired controls
 *   >0:	Device provides all the controls in @acs_flags
 */
int pci_dev_specific_acs_enabled(struct pci_dev *dev, u16 acs_flags)
{
	const struct pci_dev_acs_enabled *i;
	int ret;

	/*
	 * Allow devices that do not expose standard PCIe ACS capabilities
	 * or control to indicate their support here.  Multi-function express
	 * devices which do not allow internal peer-to-peer between functions,
	 * but do not implement PCIe ACS may wish to return true here.
	 */
	for (i = pci_dev_acs_enabled; i->acs_enabled; i++) {
		if ((i->vendor == dev->vendor ||
		     i->vendor == (u16)PCI_ANY_ID) &&
		    (i->device == dev->device ||
		     i->device == (u16)PCI_ANY_ID)) {
			ret = i->acs_enabled(dev, acs_flags);
			if (ret >= 0)
				return ret;
		}
	}

	return -ENOTTY;
}

/* Config space offset of Root Complex Base Address register */
#define INTEL_LPC_RCBA_REG 0xf0
/* 31:14 RCBA address */
#define INTEL_LPC_RCBA_MASK 0xffffc000
/* RCBA Enable */
#define INTEL_LPC_RCBA_ENABLE (1 << 0)

/* Backbone Scratch Pad Register */
#define INTEL_BSPR_REG 0x1104
/* Backbone Peer Non-Posted Disable */
#define INTEL_BSPR_REG_BPNPD (1 << 8)
/* Backbone Peer Posted Disable */
#define INTEL_BSPR_REG_BPPD  (1 << 9)

/* Upstream Peer Decode Configuration Register */
#define INTEL_UPDCR_REG 0x1014
/* 5:0 Peer Decode Enable bits */
#define INTEL_UPDCR_REG_MASK 0x3f

static int pci_quirk_enable_intel_lpc_acs(struct pci_dev *dev)
{
	u32 rcba, bspr, updcr;
	void __iomem *rcba_mem;

	/*
	 * Read the RCBA register from the LPC (D31:F0).  PCH root ports
	 * are D28:F* and therefore get probed before LPC, thus we can't
	 * use pci_get_slot()/pci_read_config_dword() here.
	 */
	pci_bus_read_config_dword(dev->bus, PCI_DEVFN(31, 0),
				  INTEL_LPC_RCBA_REG, &rcba);
	if (!(rcba & INTEL_LPC_RCBA_ENABLE))
		return -EINVAL;

	rcba_mem = ioremap(rcba & INTEL_LPC_RCBA_MASK,
				   PAGE_ALIGN(INTEL_UPDCR_REG));
	if (!rcba_mem)
		return -ENOMEM;

	/*
	 * The BSPR can disallow peer cycles, but it's set by soft strap and
	 * therefore read-only.  If both posted and non-posted peer cycles are
	 * disallowed, we're ok.  If either are allowed, then we need to use
	 * the UPDCR to disable peer decodes for each port.  This provides the
	 * PCIe ACS equivalent of PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_UF
	 */
	bspr = readl(rcba_mem + INTEL_BSPR_REG);
	bspr &= INTEL_BSPR_REG_BPNPD | INTEL_BSPR_REG_BPPD;
	if (bspr != (INTEL_BSPR_REG_BPNPD | INTEL_BSPR_REG_BPPD)) {
		updcr = readl(rcba_mem + INTEL_UPDCR_REG);
		if (updcr & INTEL_UPDCR_REG_MASK) {
			pci_info(dev, "Disabling UPDCR peer decodes\n");
			updcr &= ~INTEL_UPDCR_REG_MASK;
			writel(updcr, rcba_mem + INTEL_UPDCR_REG);
		}
	}

	iounmap(rcba_mem);
	return 0;
}

/* Miscellaneous Port Configuration register */
#define INTEL_MPC_REG 0xd8
/* MPC: Invalid Receive Bus Number Check Enable */
#define INTEL_MPC_REG_IRBNCE (1 << 26)

static void pci_quirk_enable_intel_rp_mpc_acs(struct pci_dev *dev)
{
	u32 mpc;

	/*
	 * When enabled, the IRBNCE bit of the MPC register enables the
	 * equivalent of PCI ACS Source Validation (PCI_ACS_SV), which
	 * ensures that requester IDs fall within the bus number range
	 * of the bridge.  Enable if not already.
	 */
	pci_read_config_dword(dev, INTEL_MPC_REG, &mpc);
	if (!(mpc & INTEL_MPC_REG_IRBNCE)) {
		pci_info(dev, "Enabling MPC IRBNCE\n");
		mpc |= INTEL_MPC_REG_IRBNCE;
		pci_write_config_word(dev, INTEL_MPC_REG, mpc);
	}
}

/*
 * Currently this quirk does the equivalent of
 * PCI_ACS_SV | PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_UF
 *
 * TODO: This quirk also needs to do equivalent of PCI_ACS_TB,
 * if dev->external_facing || dev->untrusted
 */
static int pci_quirk_enable_intel_pch_acs(struct pci_dev *dev)
{
	if (!pci_quirk_intel_pch_acs_match(dev))
		return -ENOTTY;

	if (pci_quirk_enable_intel_lpc_acs(dev)) {
		pci_warn(dev, "Failed to enable Intel PCH ACS quirk\n");
		return 0;
	}

	pci_quirk_enable_intel_rp_mpc_acs(dev);

	dev->dev_flags |= PCI_DEV_FLAGS_ACS_ENABLED_QUIRK;

	pci_info(dev, "Intel PCH root port ACS workaround enabled\n");

	return 0;
}

static int pci_quirk_enable_intel_spt_pch_acs(struct pci_dev *dev)
{
	int pos;
	u32 cap, ctrl;

	if (!pci_quirk_intel_spt_pch_acs_match(dev))
		return -ENOTTY;

	pos = dev->acs_cap;
	if (!pos)
		return -ENOTTY;

	pci_read_config_dword(dev, pos + PCI_ACS_CAP, &cap);
	pci_read_config_dword(dev, pos + INTEL_SPT_ACS_CTRL, &ctrl);

	ctrl |= (cap & PCI_ACS_SV);
	ctrl |= (cap & PCI_ACS_RR);
	ctrl |= (cap & PCI_ACS_CR);
	ctrl |= (cap & PCI_ACS_UF);

	if (pci_ats_disabled() || dev->external_facing || dev->untrusted)
		ctrl |= (cap & PCI_ACS_TB);

	pci_write_config_dword(dev, pos + INTEL_SPT_ACS_CTRL, ctrl);

	pci_info(dev, "Intel SPT PCH root port ACS workaround enabled\n");

	return 0;
}

static int pci_quirk_disable_intel_spt_pch_acs_redir(struct pci_dev *dev)
{
	int pos;
	u32 cap, ctrl;

	if (!pci_quirk_intel_spt_pch_acs_match(dev))
		return -ENOTTY;

	pos = dev->acs_cap;
	if (!pos)
		return -ENOTTY;

	pci_read_config_dword(dev, pos + PCI_ACS_CAP, &cap);
	pci_read_config_dword(dev, pos + INTEL_SPT_ACS_CTRL, &ctrl);

	ctrl &= ~(PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_EC);

	pci_write_config_dword(dev, pos + INTEL_SPT_ACS_CTRL, ctrl);

	pci_info(dev, "Intel SPT PCH root port workaround: disabled ACS redirect\n");

	return 0;
}

static const struct pci_dev_acs_ops {
	u16 vendor;
	u16 device;
	int (*enable_acs)(struct pci_dev *dev);
	int (*disable_acs_redir)(struct pci_dev *dev);
} pci_dev_acs_ops[] = {
	{ PCI_VENDOR_ID_INTEL, PCI_ANY_ID,
	    .enable_acs = pci_quirk_enable_intel_pch_acs,
	},
	{ PCI_VENDOR_ID_INTEL, PCI_ANY_ID,
	    .enable_acs = pci_quirk_enable_intel_spt_pch_acs,
	    .disable_acs_redir = pci_quirk_disable_intel_spt_pch_acs_redir,
	},
};

int pci_dev_specific_enable_acs(struct pci_dev *dev)
{
	const struct pci_dev_acs_ops *p;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(pci_dev_acs_ops); i++) {
		p = &pci_dev_acs_ops[i];
		if ((p->vendor == dev->vendor ||
		     p->vendor == (u16)PCI_ANY_ID) &&
		    (p->device == dev->device ||
		     p->device == (u16)PCI_ANY_ID) &&
		    p->enable_acs) {
			ret = p->enable_acs(dev);
			if (ret >= 0)
				return ret;
		}
	}

	return -ENOTTY;
}

int pci_dev_specific_disable_acs_redir(struct pci_dev *dev)
{
	const struct pci_dev_acs_ops *p;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(pci_dev_acs_ops); i++) {
		p = &pci_dev_acs_ops[i];
		if ((p->vendor == dev->vendor ||
		     p->vendor == (u16)PCI_ANY_ID) &&
		    (p->device == dev->device ||
		     p->device == (u16)PCI_ANY_ID) &&
		    p->disable_acs_redir) {
			ret = p->disable_acs_redir(dev);
			if (ret >= 0)
				return ret;
		}
	}

	return -ENOTTY;
}

/*
 * The PCI capabilities list for Intel DH895xCC VFs (device ID 0x0443) with
 * QuickAssist Technology (QAT) is prematurely terminated in hardware.  The
 * Next Capability pointer in the MSI Capability Structure should point to
 * the PCIe Capability Structure but is incorrectly hardwired as 0 terminating
 * the list.
 */
static void quirk_intel_qat_vf_cap(struct pci_dev *pdev)
{
	int pos, i = 0, ret;
	u8 next_cap;
	u16 reg16, *cap;
	struct pci_cap_saved_state *state;

	/* Bail if the hardware bug is fixed */
	if (pdev->pcie_cap || pci_find_capability(pdev, PCI_CAP_ID_EXP))
		return;

	/* Bail if MSI Capability Structure is not found for some reason */
	pos = pci_find_capability(pdev, PCI_CAP_ID_MSI);
	if (!pos)
		return;

	/*
	 * Bail if Next Capability pointer in the MSI Capability Structure
	 * is not the expected incorrect 0x00.
	 */
	pci_read_config_byte(pdev, pos + 1, &next_cap);
	if (next_cap)
		return;

	/*
	 * PCIe Capability Structure is expected to be at 0x50 and should
	 * terminate the list (Next Capability pointer is 0x00).  Verify
	 * Capability Id and Next Capability pointer is as expected.
	 * Open-code some of set_pcie_port_type() and pci_cfg_space_size_ext()
	 * to correctly set kernel data structures which have already been
	 * set incorrectly due to the hardware bug.
	 */
	pos = 0x50;
	pci_read_config_word(pdev, pos, &reg16);
	if (reg16 == (0x0000 | PCI_CAP_ID_EXP)) {
		u32 status;
#ifndef PCI_EXP_SAVE_REGS
#define PCI_EXP_SAVE_REGS     7
#endif
		int size = PCI_EXP_SAVE_REGS * sizeof(u16);

		pdev->pcie_cap = pos;
		pci_read_config_word(pdev, pos + PCI_EXP_FLAGS, &reg16);
		pdev->pcie_flags_reg = reg16;
		pci_read_config_word(pdev, pos + PCI_EXP_DEVCAP, &reg16);
		pdev->pcie_mpss = reg16 & PCI_EXP_DEVCAP_PAYLOAD;

		pdev->cfg_size = PCI_CFG_SPACE_EXP_SIZE;
		ret = pci_read_config_dword(pdev, PCI_CFG_SPACE_SIZE, &status);
		if ((ret != PCIBIOS_SUCCESSFUL) || (PCI_POSSIBLE_ERROR(status)))
			pdev->cfg_size = PCI_CFG_SPACE_SIZE;

		if (pci_find_saved_cap(pdev, PCI_CAP_ID_EXP))
			return;

		/* Save PCIe cap */
		state = kzalloc(sizeof(*state) + size, GFP_KERNEL);
		if (!state)
			return;

		state->cap.cap_nr = PCI_CAP_ID_EXP;
		state->cap.cap_extended = 0;
		state->cap.size = size;
		cap = (u16 *)&state->cap.data[0];
		pcie_capability_read_word(pdev, PCI_EXP_DEVCTL, &cap[i++]);
		pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &cap[i++]);
		pcie_capability_read_word(pdev, PCI_EXP_SLTCTL, &cap[i++]);
		pcie_capability_read_word(pdev, PCI_EXP_RTCTL,  &cap[i++]);
		pcie_capability_read_word(pdev, PCI_EXP_DEVCTL2, &cap[i++]);
		pcie_capability_read_word(pdev, PCI_EXP_LNKCTL2, &cap[i++]);
		pcie_capability_read_word(pdev, PCI_EXP_SLTCTL2, &cap[i++]);
		hlist_add_head(&state->next, &pdev->saved_cap_space);
	}
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x443, quirk_intel_qat_vf_cap);

/*
 * FLR may cause the following to devices to hang:
 *
 * AMD Starship/Matisse HD Audio Controller 0x1487
 * AMD Starship USB 3.0 Host Controller 0x148c
 * AMD Matisse USB 3.0 Host Controller 0x149c
 * Intel 82579LM Gigabit Ethernet Controller 0x1502
 * Intel 82579V Gigabit Ethernet Controller 0x1503
 * Mediatek MT7922 802.11ax PCI Express Wireless Network Adapter
 */
static void quirk_no_flr(struct pci_dev *dev)
{
	dev->dev_flags |= PCI_DEV_FLAGS_NO_FLR_RESET;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_AMD, 0x1487, quirk_no_flr);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_AMD, 0x148c, quirk_no_flr);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_AMD, 0x149c, quirk_no_flr);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_AMD, 0x7901, quirk_no_flr);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x1502, quirk_no_flr);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x1503, quirk_no_flr);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_MEDIATEK, 0x0616, quirk_no_flr);

/* FLR may cause the SolidRun SNET DPU (rev 0x1) to hang */
static void quirk_no_flr_snet(struct pci_dev *dev)
{
	if (dev->revision == 0x1)
		quirk_no_flr(dev);
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SOLIDRUN, 0x1000, quirk_no_flr_snet);

static void quirk_no_ext_tags(struct pci_dev *pdev)
{
	struct pci_host_bridge *bridge = pci_find_host_bridge(pdev->bus);

	if (!bridge)
		return;

	bridge->no_ext_tags = 1;
	pci_info(pdev, "disabling Extended Tags (this device can't handle them)\n");

	pci_walk_bus(bridge->bus, pci_configure_extended_tags, NULL);
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_3WARE, 0x1004, quirk_no_ext_tags);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SERVERWORKS, 0x0132, quirk_no_ext_tags);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SERVERWORKS, 0x0140, quirk_no_ext_tags);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SERVERWORKS, 0x0141, quirk_no_ext_tags);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SERVERWORKS, 0x0142, quirk_no_ext_tags);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SERVERWORKS, 0x0144, quirk_no_ext_tags);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SERVERWORKS, 0x0420, quirk_no_ext_tags);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SERVERWORKS, 0x0422, quirk_no_ext_tags);

#ifdef CONFIG_PCI_ATS
static void quirk_no_ats(struct pci_dev *pdev)
{
	pci_info(pdev, "disabling ATS\n");
	pdev->ats_cap = 0;
}

/*
 * Some devices require additional driver setup to enable ATS.  Don't use
 * ATS for those devices as ATS will be enabled before the driver has had a
 * chance to load and configure the device.
 */
static void quirk_amd_harvest_no_ats(struct pci_dev *pdev)
{
	if (pdev->device == 0x15d8) {
		if (pdev->revision == 0xcf &&
		    pdev->subsystem_vendor == 0xea50 &&
		    (pdev->subsystem_device == 0xce19 ||
		     pdev->subsystem_device == 0xcc10 ||
		     pdev->subsystem_device == 0xcc08))
			quirk_no_ats(pdev);
	} else {
		quirk_no_ats(pdev);
	}
}

/* AMD Stoney platform GPU */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x98e4, quirk_amd_harvest_no_ats);
/* AMD Iceland dGPU */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x6900, quirk_amd_harvest_no_ats);
/* AMD Navi10 dGPU */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x7310, quirk_amd_harvest_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x7312, quirk_amd_harvest_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x7318, quirk_amd_harvest_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x7319, quirk_amd_harvest_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x731a, quirk_amd_harvest_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x731b, quirk_amd_harvest_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x731e, quirk_amd_harvest_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x731f, quirk_amd_harvest_no_ats);
/* AMD Navi14 dGPU */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x7340, quirk_amd_harvest_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x7341, quirk_amd_harvest_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x7347, quirk_amd_harvest_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x734f, quirk_amd_harvest_no_ats);
/* AMD Raven platform iGPU */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x15d8, quirk_amd_harvest_no_ats);

/*
 * Intel IPU E2000 revisions before C0 implement incorrect endianness
 * in ATS Invalidate Request message body. Disable ATS for those devices.
 */
static void quirk_intel_e2000_no_ats(struct pci_dev *pdev)
{
	if (pdev->revision < 0x20)
		quirk_no_ats(pdev);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1451, quirk_intel_e2000_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1452, quirk_intel_e2000_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1453, quirk_intel_e2000_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1454, quirk_intel_e2000_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1455, quirk_intel_e2000_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1457, quirk_intel_e2000_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x1459, quirk_intel_e2000_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x145a, quirk_intel_e2000_no_ats);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x145c, quirk_intel_e2000_no_ats);
#endif /* CONFIG_PCI_ATS */

/* Freescale PCIe doesn't support MSI in RC mode */
static void quirk_fsl_no_msi(struct pci_dev *pdev)
{
	if (pci_pcie_type(pdev) == PCI_EXP_TYPE_ROOT_PORT)
		pdev->no_msi = 1;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_FREESCALE, PCI_ANY_ID, quirk_fsl_no_msi);

/*
 * Although not allowed by the spec, some multi-function devices have
 * dependencies of one function (consumer) on another (supplier).  For the
 * consumer to work in D0, the supplier must also be in D0.  Create a
 * device link from the consumer to the supplier to enforce this
 * dependency.  Runtime PM is allowed by default on the consumer to prevent
 * it from permanently keeping the supplier awake.
 */
static void pci_create_device_link(struct pci_dev *pdev, unsigned int consumer,
				   unsigned int supplier, unsigned int class,
				   unsigned int class_shift)
{
	struct pci_dev *supplier_pdev;

	if (PCI_FUNC(pdev->devfn) != consumer)
		return;

	supplier_pdev = pci_get_domain_bus_and_slot(pci_domain_nr(pdev->bus),
				pdev->bus->number,
				PCI_DEVFN(PCI_SLOT(pdev->devfn), supplier));
	if (!supplier_pdev || (supplier_pdev->class >> class_shift) != class) {
		pci_dev_put(supplier_pdev);
		return;
	}

	if (device_link_add(&pdev->dev, &supplier_pdev->dev,
			    DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME))
		pci_info(pdev, "D0 power state depends on %s\n",
			 pci_name(supplier_pdev));
	else
		pci_err(pdev, "Cannot enforce power dependency on %s\n",
			pci_name(supplier_pdev));

	pm_runtime_allow(&pdev->dev);
	pci_dev_put(supplier_pdev);
}

/*
 * Create device link for GPUs with integrated HDA controller for streaming
 * audio to attached displays.
 */
static void quirk_gpu_hda(struct pci_dev *hda)
{
	pci_create_device_link(hda, 1, 0, PCI_BASE_CLASS_DISPLAY, 16);
}
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_ATI, PCI_ANY_ID,
			      PCI_CLASS_MULTIMEDIA_HD_AUDIO, 8, quirk_gpu_hda);
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_AMD, PCI_ANY_ID,
			      PCI_CLASS_MULTIMEDIA_HD_AUDIO, 8, quirk_gpu_hda);
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID,
			      PCI_CLASS_MULTIMEDIA_HD_AUDIO, 8, quirk_gpu_hda);

/*
 * Create device link for GPUs with integrated USB xHCI Host
 * controller to VGA.
 */
static void quirk_gpu_usb(struct pci_dev *usb)
{
	pci_create_device_link(usb, 2, 0, PCI_BASE_CLASS_DISPLAY, 16);
}
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID,
			      PCI_CLASS_SERIAL_USB, 8, quirk_gpu_usb);
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_ATI, PCI_ANY_ID,
			      PCI_CLASS_SERIAL_USB, 8, quirk_gpu_usb);

/*
 * Create device link for GPUs with integrated Type-C UCSI controller
 * to VGA. Currently there is no class code defined for UCSI device over PCI
 * so using UNKNOWN class for now and it will be updated when UCSI
 * over PCI gets a class code.
 */
#define PCI_CLASS_SERIAL_UNKNOWN	0x0c80
static void quirk_gpu_usb_typec_ucsi(struct pci_dev *ucsi)
{
	pci_create_device_link(ucsi, 3, 0, PCI_BASE_CLASS_DISPLAY, 16);
}
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID,
			      PCI_CLASS_SERIAL_UNKNOWN, 8,
			      quirk_gpu_usb_typec_ucsi);
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_ATI, PCI_ANY_ID,
			      PCI_CLASS_SERIAL_UNKNOWN, 8,
			      quirk_gpu_usb_typec_ucsi);

/*
 * Enable the NVIDIA GPU integrated HDA controller if the BIOS left it
 * disabled.  https://devtalk.nvidia.com/default/topic/1024022
 */
static void quirk_nvidia_hda(struct pci_dev *gpu)
{
	u8 hdr_type;
	u32 val;

	/* There was no integrated HDA controller before MCP89 */
	if (gpu->device < PCI_DEVICE_ID_NVIDIA_GEFORCE_320M)
		return;

	/* Bit 25 at offset 0x488 enables the HDA controller */
	pci_read_config_dword(gpu, 0x488, &val);
	if (val & BIT(25))
		return;

	pci_info(gpu, "Enabling HDA controller\n");
	pci_write_config_dword(gpu, 0x488, val | BIT(25));

	/* The GPU becomes a multi-function device when the HDA is enabled */
	pci_read_config_byte(gpu, PCI_HEADER_TYPE, &hdr_type);
	gpu->multifunction = FIELD_GET(PCI_HEADER_TYPE_MFD, hdr_type);
}
DECLARE_PCI_FIXUP_CLASS_HEADER(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID,
			       PCI_BASE_CLASS_DISPLAY, 16, quirk_nvidia_hda);
DECLARE_PCI_FIXUP_CLASS_RESUME_EARLY(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID,
			       PCI_BASE_CLASS_DISPLAY, 16, quirk_nvidia_hda);

/*
 * Some IDT switches incorrectly flag an ACS Source Validation error on
 * completions for config read requests even though PCIe r4.0, sec
 * 6.12.1.1, says that completions are never affected by ACS Source
 * Validation.  Here's the text of IDT 89H32H8G3-YC, erratum #36:
 *
 *   Item #36 - Downstream port applies ACS Source Validation to Completions
 *   Section 6.12.1.1 of the PCI Express Base Specification 3.1 states that
 *   completions are never affected by ACS Source Validation.  However,
 *   completions received by a downstream port of the PCIe switch from a
 *   device that has not yet captured a PCIe bus number are incorrectly
 *   dropped by ACS Source Validation by the switch downstream port.
 *
 * The workaround suggested by IDT is to issue a config write to the
 * downstream device before issuing the first config read.  This allows the
 * downstream device to capture its bus and device numbers (see PCIe r4.0,
 * sec 2.2.9), thus avoiding the ACS error on the completion.
 *
 * However, we don't know when the device is ready to accept the config
 * write, so we do config reads until we receive a non-Config Request Retry
 * Status, then do the config write.
 *
 * To avoid hitting the erratum when doing the config reads, we disable ACS
 * SV around this process.
 */
int pci_idt_bus_quirk(struct pci_bus *bus, int devfn, u32 *l, int timeout)
{
	int pos;
	u16 ctrl = 0;
	bool found;
	struct pci_dev *bridge = bus->self;

	pos = bridge->acs_cap;

	/* Disable ACS SV before initial config reads */
	if (pos) {
		pci_read_config_word(bridge, pos + PCI_ACS_CTRL, &ctrl);
		if (ctrl & PCI_ACS_SV)
			pci_write_config_word(bridge, pos + PCI_ACS_CTRL,
					      ctrl & ~PCI_ACS_SV);
	}

	found = pci_bus_generic_read_dev_vendor_id(bus, devfn, l, timeout);

	/* Write Vendor ID (read-only) so the endpoint latches its bus/dev */
	if (found)
		pci_bus_write_config_word(bus, devfn, PCI_VENDOR_ID, 0);

	/* Re-enable ACS_SV if it was previously enabled */
	if (ctrl & PCI_ACS_SV)
		pci_write_config_word(bridge, pos + PCI_ACS_CTRL, ctrl);

	return found;
}

/*
 * Microsemi Switchtec NTB uses devfn proxy IDs to move TLPs between
 * NT endpoints via the internal switch fabric. These IDs replace the
 * originating Requester ID TLPs which access host memory on peer NTB
 * ports. Therefore, all proxy IDs must be aliased to the NTB device
 * to permit access when the IOMMU is turned on.
 */
static void quirk_switchtec_ntb_dma_alias(struct pci_dev *pdev)
{
	void __iomem *mmio;
	struct ntb_info_regs __iomem *mmio_ntb;
	struct ntb_ctrl_regs __iomem *mmio_ctrl;
	u64 partition_map;
	u8 partition;
	int pp;

	if (pci_enable_device(pdev)) {
		pci_err(pdev, "Cannot enable Switchtec device\n");
		return;
	}

	mmio = pci_iomap(pdev, 0, 0);
	if (mmio == NULL) {
		pci_disable_device(pdev);
		pci_err(pdev, "Cannot iomap Switchtec device\n");
		return;
	}

	pci_info(pdev, "Setting Switchtec proxy ID aliases\n");

	mmio_ntb = mmio + SWITCHTEC_GAS_NTB_OFFSET;
	mmio_ctrl = (void __iomem *) mmio_ntb + SWITCHTEC_NTB_REG_CTRL_OFFSET;

	partition = ioread8(&mmio_ntb->partition_id);

	partition_map = ioread32(&mmio_ntb->ep_map);
	partition_map |= ((u64) ioread32(&mmio_ntb->ep_map + 4)) << 32;
	partition_map &= ~(1ULL << partition);

	for (pp = 0; pp < (sizeof(partition_map) * 8); pp++) {
		struct ntb_ctrl_regs __iomem *mmio_peer_ctrl;
		u32 table_sz = 0;
		int te;

		if (!(partition_map & (1ULL << pp)))
			continue;

		pci_dbg(pdev, "Processing partition %d\n", pp);

		mmio_peer_ctrl = &mmio_ctrl[pp];

		table_sz = ioread16(&mmio_peer_ctrl->req_id_table_size);
		if (!table_sz) {
			pci_warn(pdev, "Partition %d table_sz 0\n", pp);
			continue;
		}

		if (table_sz > 512) {
			pci_warn(pdev,
				 "Invalid Switchtec partition %d table_sz %d\n",
				 pp, table_sz);
			continue;
		}

		for (te = 0; te < table_sz; te++) {
			u32 rid_entry;
			u8 devfn;

			rid_entry = ioread32(&mmio_peer_ctrl->req_id_table[te]);
			devfn = (rid_entry >> 1) & 0xFF;
			pci_dbg(pdev,
				"Aliasing Partition %d Proxy ID %02x.%d\n",
				pp, PCI_SLOT(devfn), PCI_FUNC(devfn));
			pci_add_dma_alias(pdev, devfn, 1);
		}
	}

	pci_iounmap(pdev, mmio);
	pci_disable_device(pdev);
}
#define SWITCHTEC_QUIRK(vid) \
	DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_MICROSEMI, vid, \
		PCI_CLASS_BRIDGE_OTHER, 8, quirk_switchtec_ntb_dma_alias)

SWITCHTEC_QUIRK(0x8531);  /* PFX 24xG3 */
SWITCHTEC_QUIRK(0x8532);  /* PFX 32xG3 */
SWITCHTEC_QUIRK(0x8533);  /* PFX 48xG3 */
SWITCHTEC_QUIRK(0x8534);  /* PFX 64xG3 */
SWITCHTEC_QUIRK(0x8535);  /* PFX 80xG3 */
SWITCHTEC_QUIRK(0x8536);  /* PFX 96xG3 */
SWITCHTEC_QUIRK(0x8541);  /* PSX 24xG3 */
SWITCHTEC_QUIRK(0x8542);  /* PSX 32xG3 */
SWITCHTEC_QUIRK(0x8543);  /* PSX 48xG3 */
SWITCHTEC_QUIRK(0x8544);  /* PSX 64xG3 */
SWITCHTEC_QUIRK(0x8545);  /* PSX 80xG3 */
SWITCHTEC_QUIRK(0x8546);  /* PSX 96xG3 */
SWITCHTEC_QUIRK(0x8551);  /* PAX 24XG3 */
SWITCHTEC_QUIRK(0x8552);  /* PAX 32XG3 */
SWITCHTEC_QUIRK(0x8553);  /* PAX 48XG3 */
SWITCHTEC_QUIRK(0x8554);  /* PAX 64XG3 */
SWITCHTEC_QUIRK(0x8555);  /* PAX 80XG3 */
SWITCHTEC_QUIRK(0x8556);  /* PAX 96XG3 */
SWITCHTEC_QUIRK(0x8561);  /* PFXL 24XG3 */
SWITCHTEC_QUIRK(0x8562);  /* PFXL 32XG3 */
SWITCHTEC_QUIRK(0x8563);  /* PFXL 48XG3 */
SWITCHTEC_QUIRK(0x8564);  /* PFXL 64XG3 */
SWITCHTEC_QUIRK(0x8565);  /* PFXL 80XG3 */
SWITCHTEC_QUIRK(0x8566);  /* PFXL 96XG3 */
SWITCHTEC_QUIRK(0x8571);  /* PFXI 24XG3 */
SWITCHTEC_QUIRK(0x8572);  /* PFXI 32XG3 */
SWITCHTEC_QUIRK(0x8573);  /* PFXI 48XG3 */
SWITCHTEC_QUIRK(0x8574);  /* PFXI 64XG3 */
SWITCHTEC_QUIRK(0x8575);  /* PFXI 80XG3 */
SWITCHTEC_QUIRK(0x8576);  /* PFXI 96XG3 */
SWITCHTEC_QUIRK(0x4000);  /* PFX 100XG4 */
SWITCHTEC_QUIRK(0x4084);  /* PFX 84XG4  */
SWITCHTEC_QUIRK(0x4068);  /* PFX 68XG4  */
SWITCHTEC_QUIRK(0x4052);  /* PFX 52XG4  */
SWITCHTEC_QUIRK(0x4036);  /* PFX 36XG4  */
SWITCHTEC_QUIRK(0x4028);  /* PFX 28XG4  */
SWITCHTEC_QUIRK(0x4100);  /* PSX 100XG4 */
SWITCHTEC_QUIRK(0x4184);  /* PSX 84XG4  */
SWITCHTEC_QUIRK(0x4168);  /* PSX 68XG4  */
SWITCHTEC_QUIRK(0x4152);  /* PSX 52XG4  */
SWITCHTEC_QUIRK(0x4136);  /* PSX 36XG4  */
SWITCHTEC_QUIRK(0x4128);  /* PSX 28XG4  */
SWITCHTEC_QUIRK(0x4200);  /* PAX 100XG4 */
SWITCHTEC_QUIRK(0x4284);  /* PAX 84XG4  */
SWITCHTEC_QUIRK(0x4268);  /* PAX 68XG4  */
SWITCHTEC_QUIRK(0x4252);  /* PAX 52XG4  */
SWITCHTEC_QUIRK(0x4236);  /* PAX 36XG4  */
SWITCHTEC_QUIRK(0x4228);  /* PAX 28XG4  */
SWITCHTEC_QUIRK(0x4352);  /* PFXA 52XG4 */
SWITCHTEC_QUIRK(0x4336);  /* PFXA 36XG4 */
SWITCHTEC_QUIRK(0x4328);  /* PFXA 28XG4 */
SWITCHTEC_QUIRK(0x4452);  /* PSXA 52XG4 */
SWITCHTEC_QUIRK(0x4436);  /* PSXA 36XG4 */
SWITCHTEC_QUIRK(0x4428);  /* PSXA 28XG4 */
SWITCHTEC_QUIRK(0x4552);  /* PAXA 52XG4 */
SWITCHTEC_QUIRK(0x4536);  /* PAXA 36XG4 */
SWITCHTEC_QUIRK(0x4528);  /* PAXA 28XG4 */
SWITCHTEC_QUIRK(0x5000);  /* PFX 100XG5 */
SWITCHTEC_QUIRK(0x5084);  /* PFX 84XG5 */
SWITCHTEC_QUIRK(0x5068);  /* PFX 68XG5 */
SWITCHTEC_QUIRK(0x5052);  /* PFX 52XG5 */
SWITCHTEC_QUIRK(0x5036);  /* PFX 36XG5 */
SWITCHTEC_QUIRK(0x5028);  /* PFX 28XG5 */
SWITCHTEC_QUIRK(0x5100);  /* PSX 100XG5 */
SWITCHTEC_QUIRK(0x5184);  /* PSX 84XG5 */
SWITCHTEC_QUIRK(0x5168);  /* PSX 68XG5 */
SWITCHTEC_QUIRK(0x5152);  /* PSX 52XG5 */
SWITCHTEC_QUIRK(0x5136);  /* PSX 36XG5 */
SWITCHTEC_QUIRK(0x5128);  /* PSX 28XG5 */
SWITCHTEC_QUIRK(0x5200);  /* PAX 100XG5 */
SWITCHTEC_QUIRK(0x5284);  /* PAX 84XG5 */
SWITCHTEC_QUIRK(0x5268);  /* PAX 68XG5 */
SWITCHTEC_QUIRK(0x5252);  /* PAX 52XG5 */
SWITCHTEC_QUIRK(0x5236);  /* PAX 36XG5 */
SWITCHTEC_QUIRK(0x5228);  /* PAX 28XG5 */
SWITCHTEC_QUIRK(0x5300);  /* PFXA 100XG5 */
SWITCHTEC_QUIRK(0x5384);  /* PFXA 84XG5 */
SWITCHTEC_QUIRK(0x5368);  /* PFXA 68XG5 */
SWITCHTEC_QUIRK(0x5352);  /* PFXA 52XG5 */
SWITCHTEC_QUIRK(0x5336);  /* PFXA 36XG5 */
SWITCHTEC_QUIRK(0x5328);  /* PFXA 28XG5 */
SWITCHTEC_QUIRK(0x5400);  /* PSXA 100XG5 */
SWITCHTEC_QUIRK(0x5484);  /* PSXA 84XG5 */
SWITCHTEC_QUIRK(0x5468);  /* PSXA 68XG5 */
SWITCHTEC_QUIRK(0x5452);  /* PSXA 52XG5 */
SWITCHTEC_QUIRK(0x5436);  /* PSXA 36XG5 */
SWITCHTEC_QUIRK(0x5428);  /* PSXA 28XG5 */
SWITCHTEC_QUIRK(0x5500);  /* PAXA 100XG5 */
SWITCHTEC_QUIRK(0x5584);  /* PAXA 84XG5 */
SWITCHTEC_QUIRK(0x5568);  /* PAXA 68XG5 */
SWITCHTEC_QUIRK(0x5552);  /* PAXA 52XG5 */
SWITCHTEC_QUIRK(0x5536);  /* PAXA 36XG5 */
SWITCHTEC_QUIRK(0x5528);  /* PAXA 28XG5 */

#define SWITCHTEC_PCI100X_QUIRK(vid) \
	DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_EFAR, vid, \
		PCI_CLASS_BRIDGE_OTHER, 8, quirk_switchtec_ntb_dma_alias)
SWITCHTEC_PCI100X_QUIRK(0x1001);  /* PCI1001XG4 */
SWITCHTEC_PCI100X_QUIRK(0x1002);  /* PCI1002XG4 */
SWITCHTEC_PCI100X_QUIRK(0x1003);  /* PCI1003XG4 */
SWITCHTEC_PCI100X_QUIRK(0x1004);  /* PCI1004XG4 */
SWITCHTEC_PCI100X_QUIRK(0x1005);  /* PCI1005XG4 */
SWITCHTEC_PCI100X_QUIRK(0x1006);  /* PCI1006XG4 */


/*
 * The PLX NTB uses devfn proxy IDs to move TLPs between NT endpoints.
 * These IDs are used to forward responses to the originator on the other
 * side of the NTB.  Alias all possible IDs to the NTB to permit access when
 * the IOMMU is turned on.
 */
static void quirk_plx_ntb_dma_alias(struct pci_dev *pdev)
{
	pci_info(pdev, "Setting PLX NTB proxy ID aliases\n");
	/* PLX NTB may use all 256 devfns */
	pci_add_dma_alias(pdev, 0, 256);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_PLX, 0x87b0, quirk_plx_ntb_dma_alias);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_PLX, 0x87b1, quirk_plx_ntb_dma_alias);

/*
 * On Lenovo Thinkpad P50 SKUs with a Nvidia Quadro M1000M, the BIOS does
 * not always reset the secondary Nvidia GPU between reboots if the system
 * is configured to use Hybrid Graphics mode.  This results in the GPU
 * being left in whatever state it was in during the *previous* boot, which
 * causes spurious interrupts from the GPU, which in turn causes us to
 * disable the wrong IRQ and end up breaking the touchpad.  Unsurprisingly,
 * this also completely breaks nouveau.
 *
 * Luckily, it seems a simple reset of the Nvidia GPU brings it back to a
 * clean state and fixes all these issues.
 *
 * When the machine is configured in Dedicated display mode, the issue
 * doesn't occur.  Fortunately the GPU advertises NoReset+ when in this
 * mode, so we can detect that and avoid resetting it.
 */
static void quirk_reset_lenovo_thinkpad_p50_nvgpu(struct pci_dev *pdev)
{
	void __iomem *map;
	int ret;

	if (pdev->subsystem_vendor != PCI_VENDOR_ID_LENOVO ||
	    pdev->subsystem_device != 0x222e ||
	    !pci_reset_supported(pdev))
		return;

	if (pci_enable_device_mem(pdev))
		return;

	/*
	 * Based on nvkm_device_ctor() in
	 * drivers/gpu/drm/nouveau/nvkm/engine/device/base.c
	 */
	map = pci_iomap(pdev, 0, 0x23000);
	if (!map) {
		pci_err(pdev, "Can't map MMIO space\n");
		goto out_disable;
	}

	/*
	 * Make sure the GPU looks like it's been POSTed before resetting
	 * it.
	 */
	if (ioread32(map + 0x2240c) & 0x2) {
		pci_info(pdev, FW_BUG "GPU left initialized by EFI, resetting\n");
		ret = pci_reset_bus(pdev);
		if (ret < 0)
			pci_err(pdev, "Failed to reset GPU: %d\n", ret);
	}

	iounmap(map);
out_disable:
	pci_disable_device(pdev);
}
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_NVIDIA, 0x13b1,
			      PCI_CLASS_DISPLAY_VGA, 8,
			      quirk_reset_lenovo_thinkpad_p50_nvgpu);

/*
 * Device [1b21:2142]
 * When in D0, PME# doesn't get asserted when plugging USB 3.0 device.
 */
static void pci_fixup_no_d0_pme(struct pci_dev *dev)
{
	pci_info(dev, "PME# does not work under D0, disabling it\n");
	dev->pme_support &= ~(PCI_PM_CAP_PME_D0 >> PCI_PM_CAP_PME_SHIFT);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ASMEDIA, 0x2142, pci_fixup_no_d0_pme);

/*
 * Device 12d8:0x400e [OHCI] and 12d8:0x400f [EHCI]
 *
 * These devices advertise PME# support in all power states but don't
 * reliably assert it.
 *
 * These devices also advertise MSI, but documentation (PI7C9X440SL.pdf)
 * says "The MSI Function is not implemented on this device" in chapters
 * 7.3.27, 7.3.29-7.3.31.
 */
static void pci_fixup_no_msi_no_pme(struct pci_dev *dev)
{
#ifdef CONFIG_PCI_MSI
	pci_info(dev, "MSI is not implemented on this device, disabling it\n");
	dev->no_msi = 1;
#endif
	pci_info(dev, "PME# is unreliable, disabling it\n");
	dev->pme_support = 0;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_PERICOM, 0x400e, pci_fixup_no_msi_no_pme);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_PERICOM, 0x400f, pci_fixup_no_msi_no_pme);

static void apex_pci_fixup_class(struct pci_dev *pdev)
{
	pdev->class = (PCI_CLASS_SYSTEM_OTHER << 8) | pdev->class;
}
DECLARE_PCI_FIXUP_CLASS_HEADER(0x1ac1, 0x089a,
			       PCI_CLASS_NOT_DEFINED, 8, apex_pci_fixup_class);

/*
 * Pericom PI7C9X2G404/PI7C9X2G304/PI7C9X2G303 switch erratum E5 -
 * ACS P2P Request Redirect is not functional
 *
 * When ACS P2P Request Redirect is enabled and bandwidth is not balanced
 * between upstream and downstream ports, packets are queued in an internal
 * buffer until CPLD packet. The workaround is to use the switch in store and
 * forward mode.
 */
#define PI7C9X2Gxxx_MODE_REG		0x74
#define PI7C9X2Gxxx_STORE_FORWARD_MODE	BIT(0)
static void pci_fixup_pericom_acs_store_forward(struct pci_dev *pdev)
{
	struct pci_dev *upstream;
	u16 val;

	/* Downstream ports only */
	if (pci_pcie_type(pdev) != PCI_EXP_TYPE_DOWNSTREAM)
		return;

	/* Check for ACS P2P Request Redirect use */
	if (!pdev->acs_cap)
		return;
	pci_read_config_word(pdev, pdev->acs_cap + PCI_ACS_CTRL, &val);
	if (!(val & PCI_ACS_RR))
		return;

	upstream = pci_upstream_bridge(pdev);
	if (!upstream)
		return;

	pci_read_config_word(upstream, PI7C9X2Gxxx_MODE_REG, &val);
	if (!(val & PI7C9X2Gxxx_STORE_FORWARD_MODE)) {
		pci_info(upstream, "Setting PI7C9X2Gxxx store-forward mode to avoid ACS erratum\n");
		pci_write_config_word(upstream, PI7C9X2Gxxx_MODE_REG, val |
				      PI7C9X2Gxxx_STORE_FORWARD_MODE);
	}
}
/*
 * Apply fixup on enable and on resume, in order to apply the fix up whenever
 * ACS configuration changes or switch mode is reset
 */
DECLARE_PCI_FIXUP_ENABLE(PCI_VENDOR_ID_PERICOM, 0x2404,
			 pci_fixup_pericom_acs_store_forward);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_PERICOM, 0x2404,
			 pci_fixup_pericom_acs_store_forward);
DECLARE_PCI_FIXUP_ENABLE(PCI_VENDOR_ID_PERICOM, 0x2304,
			 pci_fixup_pericom_acs_store_forward);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_PERICOM, 0x2304,
			 pci_fixup_pericom_acs_store_forward);
DECLARE_PCI_FIXUP_ENABLE(PCI_VENDOR_ID_PERICOM, 0x2303,
			 pci_fixup_pericom_acs_store_forward);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_PERICOM, 0x2303,
			 pci_fixup_pericom_acs_store_forward);

static void nvidia_ion_ahci_fixup(struct pci_dev *pdev)
{
	pdev->dev_flags |= PCI_DEV_FLAGS_HAS_MSI_MASKING;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NVIDIA, 0x0ab8, nvidia_ion_ahci_fixup);

static void rom_bar_overlap_defect(struct pci_dev *dev)
{
	pci_info(dev, "working around ROM BAR overlap defect\n");
	dev->rom_bar_overlap = 1;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x1533, rom_bar_overlap_defect);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x1536, rom_bar_overlap_defect);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x1537, rom_bar_overlap_defect);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x1538, rom_bar_overlap_defect);

#ifdef CONFIG_PCIEASPM
/*
 * Several Intel DG2 graphics devices advertise that they can only tolerate
 * 1us latency when transitioning from L1 to L0, which may prevent ASPM L1
 * from being enabled.  But in fact these devices can tolerate unlimited
 * latency.  Override their Device Capabilities value to allow ASPM L1 to
 * be enabled.
 */
static void aspm_l1_acceptable_latency(struct pci_dev *dev)
{
	u32 l1_lat = FIELD_GET(PCI_EXP_DEVCAP_L1, dev->devcap);

	if (l1_lat < 7) {
		dev->devcap |= FIELD_PREP(PCI_EXP_DEVCAP_L1, 7);
		pci_info(dev, "ASPM: overriding L1 acceptable latency from %#x to 0x7\n",
			 l1_lat);
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x4f80, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x4f81, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x4f82, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x4f83, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x4f84, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x4f85, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x4f86, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x4f87, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x4f88, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x5690, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x5691, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x5692, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x5693, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x5694, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x5695, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x56a0, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x56a1, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x56a2, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x56a3, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x56a4, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x56a5, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x56a6, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x56b0, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x56b1, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x56c0, aspm_l1_acceptable_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x56c1, aspm_l1_acceptable_latency);
#endif

#ifdef CONFIG_PCIE_DPC
/*
 * Intel Ice Lake, Tiger Lake and Alder Lake BIOS has a bug that clears
 * the DPC RP PIO Log Size of the integrated Thunderbolt PCIe Root
 * Ports.
 */
static void dpc_log_size(struct pci_dev *dev)
{
	u16 dpc, val;

	dpc = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_DPC);
	if (!dpc)
		return;

	pci_read_config_word(dev, dpc + PCI_EXP_DPC_CAP, &val);
	if (!(val & PCI_EXP_DPC_CAP_RP_EXT))
		return;

	if (FIELD_GET(PCI_EXP_DPC_RP_PIO_LOG_SIZE, val) == 0) {
		pci_info(dev, "Overriding RP PIO Log Size to %d\n",
			 PCIE_STD_NUM_TLP_HEADERLOG);
		dev->dpc_rp_log_size = PCIE_STD_NUM_TLP_HEADERLOG;
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x461f, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x462f, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x463f, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x466e, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x8a1d, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x8a1f, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x8a21, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x8a23, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x9a23, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x9a25, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x9a27, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x9a29, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x9a2b, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x9a2d, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x9a2f, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x9a31, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0xa72f, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0xa73f, dpc_log_size);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0xa76e, dpc_log_size);
#endif

/*
 * For a PCI device with multiple downstream devices, its driver may use
 * a flattened device tree to describe the downstream devices.
 * To overlay the flattened device tree, the PCI device and all its ancestor
 * devices need to have device tree nodes on system base device tree. Thus,
 * before driver probing, it might need to add a device tree node as the final
 * fixup.
 */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_XILINX, 0x5020, of_pci_make_dev_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_XILINX, 0x5021, of_pci_make_dev_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_REDHAT, 0x0005, of_pci_make_dev_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_EFAR, 0x9660, of_pci_make_dev_node);

/*
 * Devices known to require a longer delay before first config space access
 * after reset recovery or resume from D3cold:
 *
 * VideoPropulsion (aka Genroco) Torrent QN16e MPEG QAM Modulator
 */
static void pci_fixup_d3cold_delay_1sec(struct pci_dev *pdev)
{
	pdev->d3cold_delay = 1000;
}
DECLARE_PCI_FIXUP_FINAL(0x5555, 0x0004, pci_fixup_d3cold_delay_1sec);

#ifdef CONFIG_PCIEAER
static void pci_mask_replay_timer_timeout(struct pci_dev *pdev)
{
	struct pci_dev *parent = pci_upstream_bridge(pdev);
	u32 val;

	if (!parent || !parent->aer_cap)
		return;

	pci_info(parent, "mask Replay Timer Timeout Correctable Errors due to %s hardware defect",
		 pci_name(pdev));

	pci_read_config_dword(parent, parent->aer_cap + PCI_ERR_COR_MASK, &val);
	val |= PCI_ERR_COR_REP_TIMER;
	pci_write_config_dword(parent, parent->aer_cap + PCI_ERR_COR_MASK, val);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_GLI, 0x9750, pci_mask_replay_timer_timeout);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_GLI, 0x9755, pci_mask_replay_timer_timeout);
#endif
