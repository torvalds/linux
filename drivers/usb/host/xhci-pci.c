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
#define PCI_VENDOR_ID_FRESCO_LOGIC	0x1b73
#define PCI_DEVICE_ID_FRESCO_LOGIC_PDK	0x1000
#define PCI_DEVICE_ID_FRESCO_LOGIC_FL1009	0x1009
#define PCI_DEVICE_ID_FRESCO_LOGIC_FL1100	0x1100
#define PCI_DEVICE_ID_FRESCO_LOGIC_FL1400	0x1400

#define PCI_VENDOR_ID_ETRON		0x1b6f
#define PCI_DEVICE_ID_EJ168		0x7023

#define PCI_DEVICE_ID_INTEL_LYNXPOINT_XHCI	0x8c31
#define PCI_DEVICE_ID_INTEL_LYNXPOINT_LP_XHCI	0x9c31
#define PCI_DEVICE_ID_INTEL_WILDCATPOINT_LP_XHCI	0x9cb1
#define PCI_DEVICE_ID_INTEL_CHERRYVIEW_XHCI		0x22b5
#define PCI_DEVICE_ID_INTEL_SUNRISEPOINT_H_XHCI		0xa12f
#define PCI_DEVICE_ID_INTEL_SUNRISEPOINT_LP_XHCI	0x9d2f
#define PCI_DEVICE_ID_INTEL_BROXTON_M_XHCI		0x0aa8
#define PCI_DEVICE_ID_INTEL_BROXTON_B_XHCI		0x1aa8
#define PCI_DEVICE_ID_INTEL_APL_XHCI			0x5aa8
#define PCI_DEVICE_ID_INTEL_DNV_XHCI			0x19d0
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_2C_XHCI	0x15b5
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_4C_XHCI	0x15b6
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_XHCI	0x15c1
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_XHCI	0x15db
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_XHCI	0x15d4
#define PCI_DEVICE_ID_INTEL_TITAN_RIDGE_2C_XHCI		0x15e9
#define PCI_DEVICE_ID_INTEL_TITAN_RIDGE_4C_XHCI		0x15ec
#define PCI_DEVICE_ID_INTEL_TITAN_RIDGE_DD_XHCI		0x15f0
#define PCI_DEVICE_ID_INTEL_ICE_LAKE_XHCI		0x8a13
#define PCI_DEVICE_ID_INTEL_CML_XHCI			0xa3af
#define PCI_DEVICE_ID_INTEL_TIGER_LAKE_XHCI		0x9a13
#define PCI_DEVICE_ID_INTEL_MAPLE_RIDGE_XHCI		0x1138
#define PCI_DEVICE_ID_INTEL_ALDER_LAKE_XHCI		0x461e
#define PCI_DEVICE_ID_INTEL_ALDER_LAKE_N_XHCI		0x464e
#define PCI_DEVICE_ID_INTEL_ALDER_LAKE_PCH_XHCI	0x51ed
#define PCI_DEVICE_ID_INTEL_RAPTOR_LAKE_XHCI		0xa71e
#define PCI_DEVICE_ID_INTEL_METEOR_LAKE_XHCI		0x7ec0

#define PCI_DEVICE_ID_AMD_RENOIR_XHCI			0x1639
#define PCI_DEVICE_ID_AMD_PROMONTORYA_4			0x43b9
#define PCI_DEVICE_ID_AMD_PROMONTORYA_3			0x43ba
#define PCI_DEVICE_ID_AMD_PROMONTORYA_2			0x43bb
#define PCI_DEVICE_ID_AMD_PROMONTORYA_1			0x43bc
#define PCI_DEVICE_ID_AMD_YELLOW_CARP_XHCI_1		0x161a
#define PCI_DEVICE_ID_AMD_YELLOW_CARP_XHCI_2		0x161b
#define PCI_DEVICE_ID_AMD_YELLOW_CARP_XHCI_3		0x161d
#define PCI_DEVICE_ID_AMD_YELLOW_CARP_XHCI_4		0x161e
#define PCI_DEVICE_ID_AMD_YELLOW_CARP_XHCI_5		0x15d6
#define PCI_DEVICE_ID_AMD_YELLOW_CARP_XHCI_6		0x15d7
#define PCI_DEVICE_ID_AMD_YELLOW_CARP_XHCI_7		0x161c
#define PCI_DEVICE_ID_AMD_YELLOW_CARP_XHCI_8		0x161f

#define PCI_DEVICE_ID_ASMEDIA_1042_XHCI			0x1042
#define PCI_DEVICE_ID_ASMEDIA_1042A_XHCI		0x1142
#define PCI_DEVICE_ID_ASMEDIA_1142_XHCI			0x1242
#define PCI_DEVICE_ID_ASMEDIA_2142_XHCI			0x2142
#define PCI_DEVICE_ID_ASMEDIA_3242_XHCI			0x3242

static const char hcd_name[] = "xhci_hcd";

static struct hc_driver __read_mostly xhci_pci_hc_driver;

static int xhci_pci_setup(struct usb_hcd *hcd);

static const struct xhci_driver_overrides xhci_pci_overrides __initconst = {
	.reset = xhci_pci_setup,
};

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
	struct xhci_driver_data         *driver_data;
	const struct pci_device_id      *id;

	id = pci_match_id(to_pci_driver(pdev->dev.driver)->id_table, pdev);

	if (id && id->driver_data) {
		driver_data = (struct xhci_driver_data *)id->driver_data;
		xhci->quirks |= driver_data->quirks;
	}

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
		xhci->quirks |= XHCI_TRUST_TX_LENGTH;
	}

	if (pdev->vendor == PCI_VENDOR_ID_FRESCO_LOGIC &&
			pdev->device == PCI_DEVICE_ID_FRESCO_LOGIC_FL1009)
		xhci->quirks |= XHCI_BROKEN_STREAMS;

	if (pdev->vendor == PCI_VENDOR_ID_FRESCO_LOGIC &&
			pdev->device == PCI_DEVICE_ID_FRESCO_LOGIC_FL1100)
		xhci->quirks |= XHCI_TRUST_TX_LENGTH;

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

	if (pdev->vendor == PCI_VENDOR_ID_AMD)
		xhci->quirks |= XHCI_TRUST_TX_LENGTH;

	if ((pdev->vendor == PCI_VENDOR_ID_AMD) &&
		((pdev->device == PCI_DEVICE_ID_AMD_PROMONTORYA_4) ||
		(pdev->device == PCI_DEVICE_ID_AMD_PROMONTORYA_3) ||
		(pdev->device == PCI_DEVICE_ID_AMD_PROMONTORYA_2) ||
		(pdev->device == PCI_DEVICE_ID_AMD_PROMONTORYA_1)))
		xhci->quirks |= XHCI_U2_DISABLE_WAKE;

	if (pdev->vendor == PCI_VENDOR_ID_AMD &&
		pdev->device == PCI_DEVICE_ID_AMD_RENOIR_XHCI)
		xhci->quirks |= XHCI_BROKEN_D3COLD;

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
		 pdev->device == PCI_DEVICE_ID_INTEL_APL_XHCI ||
		 pdev->device == PCI_DEVICE_ID_INTEL_DNV_XHCI ||
		 pdev->device == PCI_DEVICE_ID_INTEL_CML_XHCI)) {
		xhci->quirks |= XHCI_PME_STUCK_QUIRK;
	}
	if (pdev->vendor == PCI_VENDOR_ID_INTEL &&
	    pdev->device == PCI_DEVICE_ID_INTEL_CHERRYVIEW_XHCI)
		xhci->quirks |= XHCI_SSIC_PORT_UNUSED;
	if (pdev->vendor == PCI_VENDOR_ID_INTEL &&
	    (pdev->device == PCI_DEVICE_ID_INTEL_CHERRYVIEW_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_SUNRISEPOINT_LP_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_APL_XHCI))
		xhci->quirks |= XHCI_INTEL_USB_ROLE_SW;
	if (pdev->vendor == PCI_VENDOR_ID_INTEL &&
	    (pdev->device == PCI_DEVICE_ID_INTEL_CHERRYVIEW_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_SUNRISEPOINT_LP_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_SUNRISEPOINT_H_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_APL_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_DNV_XHCI))
		xhci->quirks |= XHCI_MISSING_CAS;

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
	     pdev->device == PCI_DEVICE_ID_INTEL_MAPLE_RIDGE_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_ALDER_LAKE_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_ALDER_LAKE_N_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_ALDER_LAKE_PCH_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_RAPTOR_LAKE_XHCI ||
	     pdev->device == PCI_DEVICE_ID_INTEL_METEOR_LAKE_XHCI))
		xhci->quirks |= XHCI_DEFAULT_PM_RUNTIME_ALLOW;

	if (pdev->vendor == PCI_VENDOR_ID_ETRON &&
			pdev->device == PCI_DEVICE_ID_EJ168) {
		xhci->quirks |= XHCI_RESET_ON_RESUME;
		xhci->quirks |= XHCI_TRUST_TX_LENGTH;
		xhci->quirks |= XHCI_BROKEN_STREAMS;
	}
	if (pdev->vendor == PCI_VENDOR_ID_RENESAS &&
	    pdev->device == 0x0014) {
		xhci->quirks |= XHCI_TRUST_TX_LENGTH;
		xhci->quirks |= XHCI_ZERO_64B_REGS;
	}
	if (pdev->vendor == PCI_VENDOR_ID_RENESAS &&
	    pdev->device == 0x0015) {
		xhci->quirks |= XHCI_RESET_ON_RESUME;
		xhci->quirks |= XHCI_ZERO_64B_REGS;
	}
	if (pdev->vendor == PCI_VENDOR_ID_VIA)
		xhci->quirks |= XHCI_RESET_ON_RESUME;

	/* See https://bugzilla.kernel.org/show_bug.cgi?id=79511 */
	if (pdev->vendor == PCI_VENDOR_ID_VIA &&
			pdev->device == 0x3432)
		xhci->quirks |= XHCI_BROKEN_STREAMS;

	if (pdev->vendor == PCI_VENDOR_ID_VIA && pdev->device == 0x3483) {
		xhci->quirks |= XHCI_LPM_SUPPORT;
		xhci->quirks |= XHCI_EP_CTX_BROKEN_DCS;
	}

	if (pdev->vendor == PCI_VENDOR_ID_ASMEDIA &&
		pdev->device == PCI_DEVICE_ID_ASMEDIA_1042_XHCI)
		xhci->quirks |= XHCI_BROKEN_STREAMS;
	if (pdev->vendor == PCI_VENDOR_ID_ASMEDIA &&
		pdev->device == PCI_DEVICE_ID_ASMEDIA_1042A_XHCI) {
		xhci->quirks |= XHCI_TRUST_TX_LENGTH;
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

	if (pdev->vendor == PCI_VENDOR_ID_AMD &&
	    (pdev->device == PCI_DEVICE_ID_AMD_YELLOW_CARP_XHCI_1 ||
	    pdev->device == PCI_DEVICE_ID_AMD_YELLOW_CARP_XHCI_2 ||
	    pdev->device == PCI_DEVICE_ID_AMD_YELLOW_CARP_XHCI_3 ||
	    pdev->device == PCI_DEVICE_ID_AMD_YELLOW_CARP_XHCI_4 ||
	    pdev->device == PCI_DEVICE_ID_AMD_YELLOW_CARP_XHCI_5 ||
	    pdev->device == PCI_DEVICE_ID_AMD_YELLOW_CARP_XHCI_6 ||
	    pdev->device == PCI_DEVICE_ID_AMD_YELLOW_CARP_XHCI_7 ||
	    pdev->device == PCI_DEVICE_ID_AMD_YELLOW_CARP_XHCI_8))
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
#else
static void xhci_pme_acpi_rtd3_enable(struct pci_dev *dev) { }
#endif /* CONFIG_ACPI */

/* called during probe() after chip reset completes */
static int xhci_pci_setup(struct usb_hcd *hcd)
{
	struct xhci_hcd		*xhci;
	struct pci_dev		*pdev = to_pci_dev(hcd->self.controller);
	int			retval;

	xhci = hcd_to_xhci(hcd);
	if (!xhci->sbrn)
		pci_read_config_byte(pdev, XHCI_SBRN_OFFSET, &xhci->sbrn);

	/* imod_interval is the interrupt moderation value in nanoseconds. */
	xhci->imod_interval = 40000;

	retval = xhci_gen_setup(hcd, xhci_pci_quirks);
	if (retval)
		return retval;

	if (!usb_hcd_is_primary_hcd(hcd))
		return 0;

	if (xhci->quirks & XHCI_PME_STUCK_QUIRK)
		xhci_pme_acpi_rtd3_enable(pdev);

	xhci_dbg(xhci, "Got SBRN %u\n", (unsigned int) xhci->sbrn);

	/* Find any debug ports */
	return xhci_pci_reinit(xhci, pdev);
}

/*
 * We need to register our own PCI probe function (instead of the USB core's
 * function) in order to create a second roothub under xHCI.
 */
static int xhci_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int retval;
	struct xhci_hcd *xhci;
	struct usb_hcd *hcd;
	struct xhci_driver_data *driver_data;
	struct reset_control *reset;

	driver_data = (struct xhci_driver_data *)id->driver_data;
	if (driver_data && driver_data->quirks & XHCI_RENESAS_FW_QUIRK) {
		retval = renesas_xhci_check_request_fw(dev, id);
		if (retval)
			return retval;
	}

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

	if (xhci->quirks & XHCI_DEFAULT_PM_RUNTIME_ALLOW)
		pm_runtime_allow(&dev->dev);

	return 0;

put_usb3_hcd:
	usb_put_hcd(xhci->shared_hcd);
dealloc_usb2_hcd:
	usb_hcd_pci_remove(dev);
put_runtime_pm:
	pm_runtime_put_noidle(&dev->dev);
	return retval;
}

static void xhci_pci_remove(struct pci_dev *dev)
{
	struct xhci_hcd *xhci;

	xhci = hcd_to_xhci(pci_get_drvdata(dev));

	xhci->xhc_state |= XHCI_STATE_REMOVING;

	if (xhci->quirks & XHCI_DEFAULT_PM_RUNTIME_ALLOW)
		pm_runtime_forbid(&dev->dev);

	if (xhci->shared_hcd) {
		usb_remove_hcd(xhci->shared_hcd);
		usb_put_hcd(xhci->shared_hcd);
		xhci->shared_hcd = NULL;
	}

	/* Workaround for spurious wakeups at shutdown with HSW */
	if (xhci->quirks & XHCI_SPURIOUS_WAKEUP)
		pci_set_power_state(dev, PCI_D3hot);

	usb_hcd_pci_remove(dev);
}

#ifdef CONFIG_PM
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
	if (xhci->quirks & (XHCI_COMP_MODE_QUIRK | XHCI_BROKEN_D3COLD))
		pci_d3cold_disable(pdev);

	if (xhci->quirks & XHCI_PME_STUCK_QUIRK)
		xhci_pme_quirk(hcd);

	if (xhci->quirks & XHCI_SSIC_PORT_UNUSED)
		xhci_ssic_port_unused_quirk(hcd, true);

	if (xhci->quirks & XHCI_DISABLE_SPARSE)
		xhci_sparse_control_quirk(hcd);

	ret = xhci_suspend(xhci, do_wakeup);
	if (ret && (xhci->quirks & XHCI_SSIC_PORT_UNUSED))
		xhci_ssic_port_unused_quirk(hcd, false);

	return ret;
}

static int xhci_pci_resume(struct usb_hcd *hcd, bool hibernated)
{
	struct xhci_hcd		*xhci = hcd_to_xhci(hcd);
	struct pci_dev		*pdev = to_pci_dev(hcd->self.controller);
	int			retval = 0;

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

	retval = xhci_resume(xhci, hibernated);
	return retval;
}

static void xhci_pci_shutdown(struct usb_hcd *hcd)
{
	struct xhci_hcd		*xhci = hcd_to_xhci(hcd);
	struct pci_dev		*pdev = to_pci_dev(hcd->self.controller);

	xhci_shutdown(hcd);

	/* Yet another workaround for spurious wakeups at shutdown with HSW */
	if (xhci->quirks & XHCI_SPURIOUS_WAKEUP)
		pci_set_power_state(pdev, PCI_D3hot);
}
#endif /* CONFIG_PM */

/*-------------------------------------------------------------------------*/

static const struct xhci_driver_data reneses_data = {
	.quirks  = XHCI_RENESAS_FW_QUIRK,
	.firmware = "renesas_usb_fw.mem",
};

/* PCI driver selection metadata; PCI hotplugging uses this */
static const struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(0x1912, 0x0014),
		.driver_data =  (unsigned long)&reneses_data,
	},
	{ PCI_DEVICE(0x1912, 0x0015),
		.driver_data =  (unsigned long)&reneses_data,
	},
	/* handle any USB 3.0 xHCI controller */
	{ PCI_DEVICE_CLASS(PCI_CLASS_SERIAL_USB_XHCI, ~0),
	},
	{ /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, pci_ids);

/*
 * Without CONFIG_USB_XHCI_PCI_RENESAS renesas_xhci_check_request_fw() won't
 * load firmware, so don't encumber the xhci-pci driver with it.
 */
#if IS_ENABLED(CONFIG_USB_XHCI_PCI_RENESAS)
MODULE_FIRMWARE("renesas_usb_fw.mem");
#endif

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver xhci_pci_driver = {
	.name =		hcd_name,
	.id_table =	pci_ids,

	.probe =	xhci_pci_probe,
	.remove =	xhci_pci_remove,
	/* suspend and resume implemented later */

	.shutdown = 	usb_hcd_pci_shutdown,
#ifdef CONFIG_PM
	.driver = {
		.pm = &usb_hcd_pci_pm_ops
	},
#endif
};

static int __init xhci_pci_init(void)
{
	xhci_init_driver(&xhci_pci_hc_driver, &xhci_pci_overrides);
#ifdef CONFIG_PM
	xhci_pci_hc_driver.pci_suspend = xhci_pci_suspend;
	xhci_pci_hc_driver.pci_resume = xhci_pci_resume;
	xhci_pci_hc_driver.shutdown = xhci_pci_shutdown;
#endif
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
