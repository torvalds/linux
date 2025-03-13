// SPDX-License-Identifier: GPL-2.0
/*
 * xHCI host controller driver PCI Bus Glue.
 *
 * Copyright (C) 2008 Intel Corp.
 *
 * Author: Sarah Sharp
 * Some code borrowed from the Linux EHCI driver.
 */

#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/reset.h>
#include <linux/suspend.h>

#include "xhci.h"
#include "xhci-trace.h"
#include "xhci-pci.h"

#define SSIC_PORT_NUM		2
#define SSIC_PORT_CFG2		0x880c
#define SSIC_PORT_CFG2_OFFSET	0x30
#define PROG_DONE		(1 << 30)
#define SSIC_PORT_UNUSED	(1 << 31)
#define SPARSE_DISABLE_BIT	17
#define SPARSE_CNTL_ENABLE	0xC12C

/* Device for a quirk */
#define PCI_VENDOR_ID_FRESCO_LOGIC		0x1b73
#define PCI_DEVICE_ID_FRESCO_LOGIC_PDK		0x1000
#define PCI_DEVICE_ID_FRESCO_LOGIC_FL1009	0x1009
#define PCI_DEVICE_ID_FRESCO_LOGIC_FL1100	0x1100
#define PCI_DEVICE_ID_FRESCO_LOGIC_FL1400	0x1400

#define PCI_VENDOR_ID_ETRON			0x1b6f
#define PCI_DEVICE_ID_ETRON_EJ168		0x7023
#define PCI_DEVICE_ID_ETRON_EJ188		0x7052

#define PCI_DEVICE_ID_VIA_VL805			0x3483

#define PCI_DEVICE_ID_INTEL_LYNXPOINT_XHCI		0x8c31
#define PCI_DEVICE_ID_INTEL_LYNXPOINT_LP_XHCI		0x9c31
#define PCI_DEVICE_ID_INTEL_WILDCATPOINT_LP_XHCI	0x9cb1
#define PCI_DEVICE_ID_INTEL_CHERRYVIEW_XHCI		0x22b5
#define PCI_DEVICE_ID_INTEL_SUNRISEPOINT_H_XHCI		0xa12f
#define PCI_DEVICE_ID_INTEL_SUNRISEPOINT_LP_XHCI	0x9d2f
#define PCI_DEVICE_ID_INTEL_BROXTON_M_XHCI		0x0aa8
#define PCI_DEVICE_ID_INTEL_BROXTON_B_XHCI		0x1aa8
#define PCI_DEVICE_ID_INTEL_APOLLO_LAKE_XHCI		0x5aa8
#define PCI_DEVICE_ID_INTEL_DENVERTON_XHCI		0x19d0
#define PCI_DEVICE_ID_INTEL_ICE_LAKE_XHCI		0x8a13
#define PCI_DEVICE_ID_INTEL_TIGER_LAKE_XHCI		0x9a13
#define PCI_DEVICE_ID_INTEL_TIGER_LAKE_PCH_XHCI		0xa0ed
#define PCI_DEVICE_ID_INTEL_COMET_LAKE_XHCI		0xa3af
#define PCI_DEVICE_ID_INTEL_ALDER_LAKE_PCH_XHCI		0x51ed
#define PCI_DEVICE_ID_INTEL_ALDER_LAKE_N_PCH_XHCI	0x54ed

#define PCI_VENDOR_ID_PHYTIUM		0x1db7
#define PCI_DEVICE_ID_PHYTIUM_XHCI			0xdc27

/* Thunderbolt */
#define PCI_DEVICE_ID_INTEL_MAPLE_RIDGE_XHCI		0x1138
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_2C_XHCI	0x15b5
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_4C_XHCI	0x15b6
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_XHCI	0x15c1
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_XHCI	0x15db
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_XHCI	0x15d4
#define PCI_DEVICE_ID_INTEL_TITAN_RIDGE_2C_XHCI		0x15e9
#define PCI_DEVICE_ID_INTEL_TITAN_RIDGE_4C_XHCI		0x15ec
#define PCI_DEVICE_ID_INTEL_TITAN_RIDGE_DD_XHCI		0x15f0

#define PCI_DEVICE_ID_AMD_RENOIR_XHCI			0x1639
#define PCI_DEVICE_ID_AMD_PROMONTORYA_4			0x43b9
#define PCI_DEVICE_ID_AMD_PROMONTORYA_3			0x43ba
#define PCI_DEVICE_ID_AMD_PROMONTORYA_2			0x43bb
#define PCI_DEVICE_ID_AMD_PROMONTORYA_1			0x43bc

#define PCI_DEVICE_ID_ASMEDIA_1042_XHCI			0x1042
#define PCI_DEVICE_ID_ASMEDIA_1042A_XHCI		0x1142
#define PCI_DEVICE_ID_ASMEDIA_1142_XHCI			0x1242
#define PCI_DEVICE_ID_ASMEDIA_2142_XHCI			0x2142
#define PCI_DEVICE_ID_ASMEDIA_3042_XHCI			0x3042
#define PCI_DEVICE_ID_ASMEDIA_3242_XHCI			0x3242

static const char hcd_name[] = "xhci_hcd";

static struct hc_driver __read_mostly xhci_pci_hc_driver;

static int xhci_pci_setup(struct usb_hcd *hcd);
static int xhci_pci_run(struct usb_hcd *hcd);
static int xhci_pci_update_hub_device(struct usb_hcd *hcd, struct usb_device *hdev,
				      struct usb_tt *tt, gfp_t mem_flags);

static const struct xhci_driver_overrides xhci_pci_overrides __initconst = {
	.reset = xhci_pci_setup,
	.start = xhci_pci_run,
	.update_hub_device = xhci_pci_update_hub_device,
};

/*
 * Primary Legacy and MSI IRQ are synced in suspend_common().
 * All MSI-X IRQs and secondary MSI IRQs should be synced here.
 */
static void xhci_msix_sync_irqs(struct xhci_hcd *xhci)
{
	struct usb_hcd *hcd = xhci_to_hcd(xhci);

	if (hcd->msix_enabled) {
		struct pci_dev *pdev = to_pci_dev(hcd->self.controller);

		/* for now, the driver only supports one primary interrupter */
		synchronize_irq(pci_irq_vector(pdev, 0));
	}
}

/* Legacy IRQ is freed by usb_remove_hcd() or usb_hcd_pci_shutdown() */
static void xhci_cleanup_msix(struct xhci_hcd *xhci)
{
	struct usb_hcd *hcd = xhci_to_hcd(xhci);
	struct pci_dev *pdev = to_pci_dev(hcd->self.controller);

	if (hcd->irq > 0)
		return;

	free_irq(pci_irq_vector(pdev, 0), xhci_to_hcd(xhci));
	pci_free_irq_vectors(pdev);
	hcd->msix_enabled = 0;
}

/* Try enabling MSI-X with MSI and legacy IRQ as fallback */
static int xhci_try_enable_msi(struct usb_hcd *hcd)
{
	struct pci_dev *pdev = to_pci_dev(hcd->self.controller);
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	int ret;

	/*
	 * Some Fresco Logic host controllers advertise MSI, but fail to
	 * generate interrupts.  Don't even try to enable MSI.
	 */
	if (xhci->quirks & XHCI_BROKEN_MSI)
		goto legacy_irq;

	/* unregister the legacy interrupt */
	if (hcd->irq)
		free_irq(hcd->irq, hcd);
	hcd->irq = 0;

	/*
	 * Calculate number of MSI/MSI-X vectors supported.
	 * - max_interrupters: the max number of interrupts requested, capped to xhci HCSPARAMS1.
	 * - num_online_cpus: one vector per CPUs core, with at least one overall.
	 */
	xhci->nvecs = min(num_online_cpus() + 1, xhci->max_interrupters);

	/* TODO: Check with MSI Soc for sysdev */
	xhci->nvecs = pci_alloc_irq_vectors(pdev, 1, xhci->nvecs,
					    PCI_IRQ_MSIX | PCI_IRQ_MSI);
	if (xhci->nvecs < 0) {
		xhci_dbg_trace(xhci, trace_xhci_dbg_init,
			       "failed to allocate IRQ vectors");
		goto legacy_irq;
	}

	ret = request_irq(pci_irq_vector(pdev, 0), xhci_msi_irq, 0, "xhci_hcd",
			  xhci_to_hcd(xhci));
	if (ret)
		goto free_irq_vectors;

	hcd->msi_enabled = 1;
	hcd->msix_enabled = pdev->msix_enabled;
	return 0;

free_irq_vectors:
	xhci_dbg_trace(xhci, trace_xhci_dbg_init, "disable %s interrupt",
		       pdev->msix_enabled ? "MSI-X" : "MSI");
	pci_free_irq_vectors(pdev);

legacy_irq:
	if (!pdev->irq) {
		xhci_err(xhci, "No msi-x/msi found and no IRQ in BIOS\n");
		return -EINVAL;
	}

	if (!strlen(hcd->irq_descr))
		snprintf(hcd->irq_descr, sizeof(hcd->irq_descr), "%s:usb%d",
			 hcd->driver->description, hcd->self.busnum);

	/* fall back to legacy interrupt */
	ret = request_irq(pdev->irq, &usb_hcd_irq, IRQF_SHARED, hcd->irq_descr, hcd);
	if (ret) {
		xhci_err(xhci, "request interrupt %d failed\n", pdev->irq);
		return ret;
	}
	hcd->irq = pdev->irq;
	return 0;
}

static int xhci_pci_run(struct usb_hcd *hcd)
{
	int ret;

	if (usb_hcd_is_primary_hcd(hcd)) {
		ret = xhci_try_enable_msi(hcd);
		if (ret)
			return ret;
	}

	return xhci_run(hcd);
}

static void xhci_pci_stop(struct usb_hcd *hcd)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);

	xhci_stop(hcd);

	if (usb_hcd_is_primary_hcd(hcd))
		xhci_cleanup_msix(xhci);
}

/* called after powerup, by probe or system-pm "wakeup" */
static int xhci_pci_reinit(struct xhci_hcd *xhci, struct pci_dev *pdev)
{
	/*
	 * TODO: Implement finding debug ports later.
	 * TODO: see if there are any quirks that need to be added to handle
	 * new extended capabilities.
	 */

	/* PCI Memory-Write-Invalidate cycle support is optional (uncommon) */
	if (!pci_set_mwi(pdev))
		xhci_dbg(xhci, "MWI active\n");

	xhci_dbg(xhci, "Finished xhci_pci_reinit\n");
	return 0;
}

static void xhci_pci_quirks(struct device *dev, struct xhci_hcd *xhci)
{
	struct pci_dev                  *pdev = to_pci_dev(dev);

	/* Look for vendor-specific quirks */
	if (pdev->vendor == PCI_VENDOR_ID_FRESCO_LOGIC &&
			(pdev->device == PCI_DEVICE_ID_FRESCO_LOGIC_PDK ||
			 pdev->device == PCI_DEVICE_ID_FRESCO_LOGIC_FL1400)) {
		if (pdev->device == PCI_DEVICE_ID_FRESCO_LOGIC_PDK &&
				pdev->revision == 0x0) {
			xhci->quirks |= XHCI_RESET_EP_QUIRK;
			xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
				"XHCI_RESET_EP_QUIRK for this evaluation HW is deprecated");
		}
		if (pdev->device == PCI_DEVICE_ID_FRESCO_LOGIC_PDK &&
				pdev->revision == 0x4) {
			xhci->quirks |= XHCI_SLOW_SUSPEND;
			xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
				"QUIRK: Fresco Logic xHC revision %u"
				"must be suspended extra slowly",
				pdev->revision);
		}
		if (pdev->device == PCI_DEVICE_ID_FRESCO_LOGIC_PDK)
			xhci->quirks |= XHCI_BROKEN_STREAMS;
		/* Fresco Logic confirms: all revisions of this chip do not
		 * support MSI, even though some of them claim to in their PCI
		 * capabilities.
		 */
		xhci->quirks |= XHCI_BROKEN_MSI;
		xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
				"QUIRK: Fresco Logic revision %u "
				"has broken MSI implementation",
				pdev->revision);
	}

	if (pdev->vendor == PCI_VENDOR_ID_FRESCO_LOGIC &&
			pdev->device == PCI_DEVICE_ID_FRESCO_LOGIC_FL1009)
		xhci->quirks |= XHCI_BROKEN_STREAMS;

	if (pdev->vendor == PCI_VENDOR_ID_NEC)
		xhci->quirks |= XHCI_NEC_HOST;

	if (pdev->vendor == PCI_VENDOR_ID_AMD && xhci->hci_version == 0x96)
		xhci->quirks |= XHCI_AMD_0x96_HOST;

	/* AMD PLL quirk */
	if (pdev->vendor == PCI_VENDOR_ID_AMD && usb_amd_quirk_pll_check())
		xhci->quirks |= XHCI_AMD_PLL_FIX;

	if (pdev->vendor == PCI_VENDOR_ID_AMD &&
		(pdev->device == 0x145c ||
		 pdev->device == 0x15e0 ||
		 pdev->device == 0x15e1 ||
		 pdev->device == 0x43bb))
		xhci->quirks |= XHCI_SUSPEND_DELAY;

	if (pdev->vendor == PCI_VENDOR_ID_AMD &&
	    (pdev->device == 0x15e0 || pdev->device == 0x15e1))
		xhci->quirks |= XHCI_SNPS_BROKEN_SUSPEND;

	if (pdev->vendor == PCI_VENDOR_ID_AMD && pdev->device == 0x15e5) {
		xhci->quirks |= XHCI_DISABLE_SPARSE;
		xhci->quirks |= XHCI_RESET_ON_RESUME;
	}

	if (pdev->vendor == PCI_VENDOR_ID_AMD && pdev->device == 0x43f7)
		xhci->quirks |= XHCI_DEFAULT_PM_RUNTIME_ALLOW;

	if ((pdev->vendor == PCI_VENDOR_ID_AMD) &&
		((pdev->device == PCI_DEVICE_ID_AMD_PROMONTORYA_4) ||
		(pdev->device == PCI_DEVICE_ID_AMD_PROMONTORYA_3) ||
		(pdev->device == PCI_DEVICE_ID_AMD_PROMONTORYA_2) ||
		(pdev->device == PCI_DEVICE_ID_AMD_PROMONTORYA_1)))
		xhci->quirks |= XHCI_U2_DISABLE_WAKE;

	if (pdev->vendor == PCI_VENDOR_ID_AMD &&
		pdev->device == PCI_DEVICE_ID_AMD_RENOIR_XHCI)
		xhci->quirks |= XHCI_BROKEN_D3COLD_S2I;

	if (pdev->vendor == PCI_VENDOR_ID_INTEL) {
		xhci->quirks |= XHCI_LPM_SUPPORT;
		xhci->quirks |= XHCI_INTEL_HOST;
		xhci->quirks |= XHCI_AVOID_BEI;
	}
	if (pdev->vendor == PCI_VENDOR_ID_INTEL &&
			pdev->device == PCI_DEVICE_ID_INTEL_PANTHERPOINT_XHCI) {
		xhci->quirks |= XHCI_EP_LIMIT_QUIRK;
		xhci->limit_active_eps = 64;
		xhci->quirks |= XHCI_SW_BW_CHECKING;
		/*
		 * PPT desktop boards DH77EB and DH77DF will power back on after
		 * a few seconds of being shutdown.  The fix for this is to
		 * switch the ports from xHCI to EHCI on shutdown.  We can't use
		 * DMI information to find those particular boards (since each
		 * vendor will change the board name), so we have to key off all
		 * PPT chipsets.
		 */
		xhci->quirks |= XHCI_SPURIOUS_REBOOT;
	}
	if (pdev->vendor == PCI_VENDOR_ID_INTEL &&
		(pdev->device == PCI_DEVICE_ID_INTEL_LYNXPOINT_LP_XHCI ||
		 pdev->device == PCI_DEVICE_ID_INTEL_WILDCATPOINT_LP_XHCI)) {
		xhci->quirks |= XHCI_SPURIOUS_REBOOT;
		xhci->quirks |= XHCI_SPURIOUS_WAKEUP;
	}
	if (pdev->vendor == PCI_VENDOR_ID_INTEL &&
		(pdev->device == PCI_DEVICE_ID_INTEL_SUNRISEPOINT_LP_XHCI ||
		 pdev->device == PCI_DEVICE_ID_INTEL_SUNRISEPOINT_H_XHCI ||
		 pdev->device == PCI_DEVICE_ID_INTEL_CHERRYVIEW_XHCI ||
		 pdev->device == PCI_DEVICE_ID_INTEL_BROXTON_M_XHCI ||
		 pdev->device == PCI_DEVICE_ID_INTEL_BROXTON_B_XHCI ||
		 pdev->device == PCI_DEVICE_ID_INTEL_APOLLO_LAKE_XHCI ||
		 pdev->device == PCI_DEVICE_ID_INTEL_DENVERTON_XHCI ||
		 pdev->device == PCI_DEVICE_ID_INTEL_COMET_LAKE_XHCI)) {
		xhci->quirks |= XHCI_PME_STUCK_QUIRK;
	}
	if (pdev->vendor == PCI_VENDOR_ID_INTEL &&
	    pdev->device == PCI_DEVICE_ID_INTEL_CHERRYVIEW_XHCI)
		xhci->quirks |= XHCI_SSIC_PORT_UNUSED;
	if (pdev->vendor == PCI_VENDOR_ID_INTEL &&
	    (pdev->device == PCI_DEVICE_ID_INTEL_CHERRYVIEW_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_SUNRISEPOINT_LP_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_APOLLO_LAKE_XHCI))
		xhci->quirks |= XHCI_INTEL_USB_ROLE_SW;
	if (pdev->vendor == PCI_VENDOR_ID_INTEL &&
	    (pdev->device == PCI_DEVICE_ID_INTEL_CHERRYVIEW_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_SUNRISEPOINT_LP_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_SUNRISEPOINT_H_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_APOLLO_LAKE_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_DENVERTON_XHCI))
		xhci->quirks |= XHCI_MISSING_CAS;

	if (pdev->vendor == PCI_VENDOR_ID_INTEL &&
	    (pdev->device == PCI_DEVICE_ID_INTEL_TIGER_LAKE_PCH_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_ALDER_LAKE_PCH_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_ALDER_LAKE_N_PCH_XHCI))
		xhci->quirks |= XHCI_RESET_TO_DEFAULT;

	if (pdev->vendor == PCI_VENDOR_ID_INTEL &&
	    (pdev->device == PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_2C_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_4C_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_TITAN_RIDGE_2C_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_TITAN_RIDGE_4C_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_TITAN_RIDGE_DD_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_ICE_LAKE_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_TIGER_LAKE_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_MAPLE_RIDGE_XHCI))
		xhci->quirks |= XHCI_DEFAULT_PM_RUNTIME_ALLOW;

	if (pdev->vendor == PCI_VENDOR_ID_ETRON &&
	    (pdev->device == PCI_DEVICE_ID_ETRON_EJ168 ||
	     pdev->device == PCI_DEVICE_ID_ETRON_EJ188)) {
		xhci->quirks |= XHCI_ETRON_HOST;
		xhci->quirks |= XHCI_RESET_ON_RESUME;
		xhci->quirks |= XHCI_BROKEN_STREAMS;
		xhci->quirks |= XHCI_NO_SOFT_RETRY;
	}

	if (pdev->vendor == PCI_VENDOR_ID_RENESAS &&
	    pdev->device == 0x0014) {
		xhci->quirks |= XHCI_ZERO_64B_REGS;
	}
	if (pdev->vendor == PCI_VENDOR_ID_RENESAS &&
	    pdev->device == 0x0015) {
		xhci->quirks |= XHCI_RESET_ON_RESUME;
		xhci->quirks |= XHCI_ZERO_64B_REGS;
	}
	if (pdev->vendor == PCI_VENDOR_ID_VIA)
		xhci->quirks |= XHCI_RESET_ON_RESUME;

	if (pdev->vendor == PCI_VENDOR_ID_PHYTIUM &&
	    pdev->device == PCI_DEVICE_ID_PHYTIUM_XHCI)
		xhci->quirks |= XHCI_RESET_ON_RESUME;

	/* See https://bugzilla.kernel.org/show_bug.cgi?id=79511 */
	if (pdev->vendor == PCI_VENDOR_ID_VIA &&
			pdev->device == 0x3432)
		xhci->quirks |= XHCI_BROKEN_STREAMS;

	if (pdev->vendor == PCI_VENDOR_ID_VIA && pdev->device == PCI_DEVICE_ID_VIA_VL805) {
		xhci->quirks |= XHCI_LPM_SUPPORT;
		xhci->quirks |= XHCI_TRB_OVERFETCH;
	}

	if (pdev->vendor == PCI_VENDOR_ID_ASMEDIA &&
		pdev->device == PCI_DEVICE_ID_ASMEDIA_1042_XHCI) {
		/*
		 * try to tame the ASMedia 1042 controller which reports 0.96
		 * but appears to behave more like 1.0
		 */
		xhci->quirks |= XHCI_SPURIOUS_SUCCESS;
		xhci->quirks |= XHCI_BROKEN_STREAMS;
	}
	if (pdev->vendor == PCI_VENDOR_ID_ASMEDIA &&
		pdev->device == PCI_DEVICE_ID_ASMEDIA_1042A_XHCI) {
		xhci->quirks |= XHCI_NO_64BIT_SUPPORT;
	}
	if (pdev->vendor == PCI_VENDOR_ID_ASMEDIA &&
	    (pdev->device == PCI_DEVICE_ID_ASMEDIA_1142_XHCI ||
	     pdev->device == PCI_DEVICE_ID_ASMEDIA_2142_XHCI ||
	     pdev->device == PCI_DEVICE_ID_ASMEDIA_3242_XHCI))
		xhci->quirks |= XHCI_NO_64BIT_SUPPORT;

	if (pdev->vendor == PCI_VENDOR_ID_ASMEDIA &&
		pdev->device == PCI_DEVICE_ID_ASMEDIA_1042A_XHCI)
		xhci->quirks |= XHCI_ASMEDIA_MODIFY_FLOWCONTROL;

	if (pdev->vendor == PCI_VENDOR_ID_ASMEDIA &&
	    pdev->device == PCI_DEVICE_ID_ASMEDIA_3042_XHCI)
		xhci->quirks |= XHCI_RESET_ON_RESUME;

	if (pdev->vendor == PCI_VENDOR_ID_TI && pdev->device == 0x8241)
		xhci->quirks |= XHCI_LIMIT_ENDPOINT_INTERVAL_7;

	if ((pdev->vendor == PCI_VENDOR_ID_BROADCOM ||
	     pdev->vendor == PCI_VENDOR_ID_CAVIUM) &&
	     pdev->device == 0x9026)
		xhci->quirks |= XHCI_RESET_PLL_ON_DISCONNECT;

	if (pdev->vendor == PCI_VENDOR_ID_AMD &&
	    (pdev->device == PCI_DEVICE_ID_AMD_PROMONTORYA_2 ||
	     pdev->device == PCI_DEVICE_ID_AMD_PROMONTORYA_4))
		xhci->quirks |= XHCI_NO_SOFT_RETRY;

	if (pdev->vendor == PCI_VENDOR_ID_ZHAOXIN) {
		xhci->quirks |= XHCI_ZHAOXIN_HOST;
		xhci->quirks |= XHCI_LPM_SUPPORT;

		if (pdev->device == 0x9202) {
			xhci->quirks |= XHCI_RESET_ON_RESUME;
			xhci->quirks |= XHCI_TRB_OVERFETCH;
		}

		if (pdev->device == 0x9203)
			xhci->quirks |= XHCI_TRB_OVERFETCH;
	}

	if (pdev->vendor == PCI_VENDOR_ID_CDNS &&
	    pdev->device == PCI_DEVICE_ID_CDNS_USBSSP)
		xhci->quirks |= XHCI_CDNS_SCTX_QUIRK;

	/* xHC spec requires PCI devices to support D3hot and D3cold */
	if (xhci->hci_version >= 0x120)
		xhci->quirks |= XHCI_DEFAULT_PM_RUNTIME_ALLOW;

	if (xhci->quirks & XHCI_RESET_ON_RESUME)
		xhci_dbg_trace(xhci, trace_xhci_dbg_quirks,
				"QUIRK: Resetting on resume");
}

#ifdef CONFIG_ACPI
static void xhci_pme_acpi_rtd3_enable(struct pci_dev *dev)
{
	static const guid_t intel_dsm_guid =
		GUID_INIT(0xac340cb7, 0xe901, 0x45bf,
			  0xb7, 0xe6, 0x2b, 0x34, 0xec, 0x93, 0x1e, 0x23);
	union acpi_object *obj;

	obj = acpi_evaluate_dsm(ACPI_HANDLE(&dev->dev), &intel_dsm_guid, 3, 1,
				NULL);
	ACPI_FREE(obj);
}

static void xhci_find_lpm_incapable_ports(struct usb_hcd *hcd, struct usb_device *hdev)
{
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	struct xhci_hub *rhub = &xhci->usb3_rhub;
	int ret;
	int i;

	/* This is not the usb3 roothub we are looking for */
	if (hcd != rhub->hcd)
		return;

	if (hdev->maxchild > rhub->num_ports) {
		dev_err(&hdev->dev, "USB3 roothub port number mismatch\n");
		return;
	}

	for (i = 0; i < hdev->maxchild; i++) {
		ret = usb_acpi_port_lpm_incapable(hdev, i);

		dev_dbg(&hdev->dev, "port-%d disable U1/U2 _DSM: %d\n", i + 1, ret);

		if (ret >= 0) {
			rhub->ports[i]->lpm_incapable = ret;
			continue;
		}
	}
}

#else
static void xhci_pme_acpi_rtd3_enable(struct pci_dev *dev) { }
static void xhci_find_lpm_incapable_ports(struct usb_hcd *hcd, struct usb_device *hdev) { }
#endif /* CONFIG_ACPI */

/* called during probe() after chip reset completes */
static int xhci_pci_setup(struct usb_hcd *hcd)
{
	struct xhci_hcd		*xhci;
	struct pci_dev		*pdev = to_pci_dev(hcd->self.controller);
	int			retval;
	u8			sbrn;

	xhci = hcd_to_xhci(hcd);

	/* imod_interval is the interrupt moderation value in nanoseconds. */
	xhci->imod_interval = 40000;

	retval = xhci_gen_setup(hcd, xhci_pci_quirks);
	if (retval)
		return retval;

	if (!usb_hcd_is_primary_hcd(hcd))
		return 0;

	if (xhci->quirks & XHCI_PME_STUCK_QUIRK)
		xhci_pme_acpi_rtd3_enable(pdev);

	pci_read_config_byte(pdev, XHCI_SBRN_OFFSET, &sbrn);
	xhci_dbg(xhci, "Got SBRN %u\n", (unsigned int)sbrn);

	/* Find any debug ports */
	return xhci_pci_reinit(xhci, pdev);
}

static int xhci_pci_update_hub_device(struct usb_hcd *hcd, struct usb_device *hdev,
				      struct usb_tt *tt, gfp_t mem_flags)
{
	/* Check if acpi claims some USB3 roothub ports are lpm incapable */
	if (!hdev->parent)
		xhci_find_lpm_incapable_ports(hcd, hdev);

	return xhci_update_hub_device(hcd, hdev, tt, mem_flags);
}

/*
 * We need to register our own PCI probe function (instead of the USB core's
 * function) in order to create a second roothub under xHCI.
 */
int xhci_pci_common_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int retval;
	struct xhci_hcd *xhci;
	struct usb_hcd *hcd;
	struct reset_control *reset;

	reset = devm_reset_control_get_optional_exclusive(&dev->dev, NULL);
	if (IS_ERR(reset))
		return PTR_ERR(reset);
	reset_control_reset(reset);

	/* Prevent runtime suspending between USB-2 and USB-3 initialization */
	pm_runtime_get_noresume(&dev->dev);

	/* Register the USB 2.0 roothub.
	 * FIXME: USB core must know to register the USB 2.0 roothub first.
	 * This is sort of silly, because we could just set the HCD driver flags
	 * to say USB 2.0, but I'm not sure what the implications would be in
	 * the other parts of the HCD code.
	 */
	retval = usb_hcd_pci_probe(dev, &xhci_pci_hc_driver);

	if (retval)
		goto put_runtime_pm;

	/* USB 2.0 roothub is stored in the PCI device now. */
	hcd = dev_get_drvdata(&dev->dev);
	xhci = hcd_to_xhci(hcd);
	xhci->reset = reset;
	xhci->shared_hcd = usb_create_shared_hcd(&xhci_pci_hc_driver, &dev->dev,
						 pci_name(dev), hcd);
	if (!xhci->shared_hcd) {
		retval = -ENOMEM;
		goto dealloc_usb2_hcd;
	}

	retval = xhci_ext_cap_init(xhci);
	if (retval)
		goto put_usb3_hcd;

	retval = usb_add_hcd(xhci->shared_hcd, dev->irq,
			IRQF_SHARED);
	if (retval)
		goto put_usb3_hcd;
	/* Roothub already marked as USB 3.0 speed */

	if (!(xhci->quirks & XHCI_BROKEN_STREAMS) &&
			HCC_MAX_PSA(xhci->hcc_params) >= 4)
		xhci->shared_hcd->can_do_streams = 1;

	/* USB-2 and USB-3 roothubs initialized, allow runtime pm suspend */
	pm_runtime_put_noidle(&dev->dev);

	if (pci_choose_state(dev, PMSG_SUSPEND) == PCI_D0)
		pm_runtime_get(&dev->dev);
	else if (xhci->quirks & XHCI_DEFAULT_PM_RUNTIME_ALLOW)
		pm_runtime_allow(&dev->dev);

	dma_set_max_seg_size(&dev->dev, UINT_MAX);

	if (device_property_read_bool(&dev->dev, "ti,pwron-active-high"))
		pci_clear_and_set_config_dword(dev, 0xE0, 0, 1 << 22);

	return 0;

put_usb3_hcd:
	usb_put_hcd(xhci->shared_hcd);
dealloc_usb2_hcd:
	usb_hcd_pci_remove(dev);
put_runtime_pm:
	pm_runtime_put_noidle(&dev->dev);
	return retval;
}
EXPORT_SYMBOL_NS_GPL(xhci_pci_common_probe, "xhci");

/* handled by xhci-pci-renesas if enabled */
static const struct pci_device_id pci_ids_renesas[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_RENESAS, 0x0014) },
	{ PCI_DEVICE(PCI_VENDOR_ID_RENESAS, 0x0015) },
	{ /* end: all zeroes */ }
};

static int xhci_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	if (IS_ENABLED(CONFIG_USB_XHCI_PCI_RENESAS) &&
			pci_match_id(pci_ids_renesas, dev))
		return -ENODEV;

	return xhci_pci_common_probe(dev, id);
}

void xhci_pci_remove(struct pci_dev *dev)
{
	struct xhci_hcd *xhci;
	bool set_power_d3;

	xhci = hcd_to_xhci(pci_get_drvdata(dev));
	set_power_d3 = xhci->quirks & XHCI_SPURIOUS_WAKEUP;

	xhci->xhc_state |= XHCI_STATE_REMOVING;

	if (pci_choose_state(dev, PMSG_SUSPEND) == PCI_D0)
		pm_runtime_put(&dev->dev);
	else if (xhci->quirks & XHCI_DEFAULT_PM_RUNTIME_ALLOW)
		pm_runtime_forbid(&dev->dev);

	if (xhci->shared_hcd) {
		usb_remove_hcd(xhci->shared_hcd);
		usb_put_hcd(xhci->shared_hcd);
		xhci->shared_hcd = NULL;
	}

	usb_hcd_pci_remove(dev);

	/* Workaround for spurious wakeups at shutdown with HSW */
	if (set_power_d3)
		pci_set_power_state(dev, PCI_D3hot);
}
EXPORT_SYMBOL_NS_GPL(xhci_pci_remove, "xhci");

/*
 * In some Intel xHCI controllers, in order to get D3 working,
 * through a vendor specific SSIC CONFIG register at offset 0x883c,
 * SSIC PORT need to be marked as "unused" before putting xHCI
 * into D3. After D3 exit, the SSIC port need to be marked as "used".
 * Without this change, xHCI might not enter D3 state.
 */
static void xhci_ssic_port_unused_quirk(struct usb_hcd *hcd, bool suspend)
{
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	u32 val;
	void __iomem *reg;
	int i;

	for (i = 0; i < SSIC_PORT_NUM; i++) {
		reg = (void __iomem *) xhci->cap_regs +
				SSIC_PORT_CFG2 +
				i * SSIC_PORT_CFG2_OFFSET;

		/* Notify SSIC that SSIC profile programming is not done. */
		val = readl(reg) & ~PROG_DONE;
		writel(val, reg);

		/* Mark SSIC port as unused(suspend) or used(resume) */
		val = readl(reg);
		if (suspend)
			val |= SSIC_PORT_UNUSED;
		else
			val &= ~SSIC_PORT_UNUSED;
		writel(val, reg);

		/* Notify SSIC that SSIC profile programming is done */
		val = readl(reg) | PROG_DONE;
		writel(val, reg);
		readl(reg);
	}
}

/*
 * Make sure PME works on some Intel xHCI controllers by writing 1 to clear
 * the Internal PME flag bit in vendor specific PMCTRL register at offset 0x80a4
 */
static void xhci_pme_quirk(struct usb_hcd *hcd)
{
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	void __iomem *reg;
	u32 val;

	reg = (void __iomem *) xhci->cap_regs + 0x80a4;
	val = readl(reg);
	writel(val | BIT(28), reg);
	readl(reg);
}

static void xhci_sparse_control_quirk(struct usb_hcd *hcd)
{
	u32 reg;

	reg = readl(hcd->regs + SPARSE_CNTL_ENABLE);
	reg &= ~BIT(SPARSE_DISABLE_BIT);
	writel(reg, hcd->regs + SPARSE_CNTL_ENABLE);
}

static int xhci_pci_suspend(struct usb_hcd *hcd, bool do_wakeup)
{
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	struct pci_dev		*pdev = to_pci_dev(hcd->self.controller);
	int			ret;

	/*
	 * Systems with the TI redriver that loses port status change events
	 * need to have the registers polled during D3, so avoid D3cold.
	 */
	if (xhci->quirks & XHCI_COMP_MODE_QUIRK)
		pci_d3cold_disable(pdev);

#ifdef CONFIG_SUSPEND
	/* d3cold is broken, but only when s2idle is used */
	if (pm_suspend_target_state == PM_SUSPEND_TO_IDLE &&
	    xhci->quirks & (XHCI_BROKEN_D3COLD_S2I))
		pci_d3cold_disable(pdev);
#endif

	if (xhci->quirks & XHCI_PME_STUCK_QUIRK)
		xhci_pme_quirk(hcd);

	if (xhci->quirks & XHCI_SSIC_PORT_UNUSED)
		xhci_ssic_port_unused_quirk(hcd, true);

	if (xhci->quirks & XHCI_DISABLE_SPARSE)
		xhci_sparse_control_quirk(hcd);

	ret = xhci_suspend(xhci, do_wakeup);

	/* synchronize irq when using MSI-X */
	xhci_msix_sync_irqs(xhci);

	if (ret && (xhci->quirks & XHCI_SSIC_PORT_UNUSED))
		xhci_ssic_port_unused_quirk(hcd, false);

	return ret;
}

static int xhci_pci_resume(struct usb_hcd *hcd, pm_message_t msg)
{
	struct xhci_hcd		*xhci = hcd_to_xhci(hcd);
	struct pci_dev		*pdev = to_pci_dev(hcd->self.controller);

	reset_control_reset(xhci->reset);

	/* The BIOS on systems with the Intel Panther Point chipset may or may
	 * not support xHCI natively.  That means that during system resume, it
	 * may switch the ports back to EHCI so that users can use their
	 * keyboard to select a kernel from GRUB after resume from hibernate.
	 *
	 * The BIOS is supposed to remember whether the OS had xHCI ports
	 * enabled before resume, and switch the ports back to xHCI when the
	 * BIOS/OS semaphore is written, but we all know we can't trust BIOS
	 * writers.
	 *
	 * Unconditionally switch the ports back to xHCI after a system resume.
	 * It should not matter whether the EHCI or xHCI controller is
	 * resumed first. It's enough to do the switchover in xHCI because
	 * USB core won't notice anything as the hub driver doesn't start
	 * running again until after all the devices (including both EHCI and
	 * xHCI host controllers) have been resumed.
	 */

	if (pdev->vendor == PCI_VENDOR_ID_INTEL)
		usb_enable_intel_xhci_ports(pdev);

	if (xhci->quirks & XHCI_SSIC_PORT_UNUSED)
		xhci_ssic_port_unused_quirk(hcd, false);

	if (xhci->quirks & XHCI_PME_STUCK_QUIRK)
		xhci_pme_quirk(hcd);

	return xhci_resume(xhci, msg);
}

static int xhci_pci_poweroff_late(struct usb_hcd *hcd, bool do_wakeup)
{
	struct xhci_hcd		*xhci = hcd_to_xhci(hcd);
	struct xhci_port	*port;
	struct usb_device	*udev;
	u32			portsc;
	int			i;

	/*
	 * Systems with XHCI_RESET_TO_DEFAULT quirk have boot firmware that
	 * cause significant boot delay if usb ports are in suspended U3 state
	 * during boot. Some USB devices survive in U3 state over S4 hibernate
	 *
	 * Disable ports that are in U3 if remote wake is not enabled for either
	 * host controller or connected device
	 */

	if (!(xhci->quirks & XHCI_RESET_TO_DEFAULT))
		return 0;

	for (i = 0; i < HCS_MAX_PORTS(xhci->hcs_params1); i++) {
		port = &xhci->hw_ports[i];
		portsc = readl(port->addr);

		if ((portsc & PORT_PLS_MASK) != XDEV_U3)
			continue;

		if (!port->slot_id || !xhci->devs[port->slot_id]) {
			xhci_err(xhci, "No dev for slot_id %d for port %d-%d in U3\n",
				 port->slot_id, port->rhub->hcd->self.busnum,
				 port->hcd_portnum + 1);
			continue;
		}

		udev = xhci->devs[port->slot_id]->udev;

		/* if wakeup is enabled then don't disable the port */
		if (udev->do_remote_wakeup && do_wakeup)
			continue;

		xhci_dbg(xhci, "port %d-%d in U3 without wakeup, disable it\n",
			 port->rhub->hcd->self.busnum, port->hcd_portnum + 1);
		portsc = xhci_port_state_to_neutral(portsc);
		writel(portsc | PORT_PE, port->addr);
	}

	return 0;
}

static void xhci_pci_shutdown(struct usb_hcd *hcd)
{
	struct xhci_hcd		*xhci = hcd_to_xhci(hcd);
	struct pci_dev		*pdev = to_pci_dev(hcd->self.controller);

	xhci_shutdown(hcd);
	xhci_cleanup_msix(xhci);

	/* Yet another workaround for spurious wakeups at shutdown with HSW */
	if (xhci->quirks & XHCI_SPURIOUS_WAKEUP)
		pci_set_power_state(pdev, PCI_D3hot);
}

/*-------------------------------------------------------------------------*/

/* PCI driver selection metadata; PCI hotplugging uses this */
static const struct pci_device_id pci_ids[] = {
	/* handle any USB 3.0 xHCI controller */
	{ PCI_DEVICE_CLASS(PCI_CLASS_SERIAL_USB_XHCI, ~0),
	},
	{ /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, pci_ids);

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver xhci_pci_driver = {
	.name =		hcd_name,
	.id_table =	pci_ids,

	.probe =	xhci_pci_probe,
	.remove =	xhci_pci_remove,
	/* suspend and resume implemented later */

	.shutdown = 	usb_hcd_pci_shutdown,
	.driver = {
		.pm = pm_ptr(&usb_hcd_pci_pm_ops),
	},
};

static int __init xhci_pci_init(void)
{
	xhci_init_driver(&xhci_pci_hc_driver, &xhci_pci_overrides);
	xhci_pci_hc_driver.pci_suspend = pm_ptr(xhci_pci_suspend);
	xhci_pci_hc_driver.pci_resume = pm_ptr(xhci_pci_resume);
	xhci_pci_hc_driver.pci_poweroff_late = pm_ptr(xhci_pci_poweroff_late);
	xhci_pci_hc_driver.shutdown = pm_ptr(xhci_pci_shutdown);
	xhci_pci_hc_driver.stop = xhci_pci_stop;
	return pci_register_driver(&xhci_pci_driver);
}
module_init(xhci_pci_init);

static void __exit xhci_pci_exit(void)
{
	pci_unregister_driver(&xhci_pci_driver);
}
module_exit(xhci_pci_exit);

MODULE_DESCRIPTION("xHCI PCI Host Controller Driver");
MODULE_LICENSE("GPL");
