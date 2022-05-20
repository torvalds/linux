// SPDX-License-Identifier: GPL-2.0+
/*
 * EHCI HCD (Host Controller Driver) PCI Bus Glue.
 *
 * Copyright (c) 2000-2004 by David Brownell
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ehci.h"
#include "pci-quirks.h"

#define DRIVER_DESC "EHCI PCI platform driver"

static const char hcd_name[] = "ehci-pci";

/* defined here to avoid adding to pci_ids.h for single instance use */
#define PCI_DEVICE_ID_INTEL_CE4100_USB	0x2e70

#define PCI_VENDOR_ID_ASPEED		0x1a03
#define PCI_DEVICE_ID_ASPEED_EHCI	0x2603

/*-------------------------------------------------------------------------*/
#define PCI_DEVICE_ID_INTEL_QUARK_X1000_SOC		0x0939
static inline bool is_intel_quark_x1000(struct pci_dev *pdev)
{
	return pdev->vendor == PCI_VENDOR_ID_INTEL &&
		pdev->device == PCI_DEVICE_ID_INTEL_QUARK_X1000_SOC;
}

/*
 * This is the list of PCI IDs for the devices that have EHCI USB class and
 * specific drivers for that. One of the example is a ChipIdea device installed
 * on some Intel MID platforms.
 */
static const struct pci_device_id bypass_pci_id_table[] = {
	/* ChipIdea on Intel MID platform */
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x0811), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x0829), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe006), },
	{}
};

static inline bool is_bypassed_id(struct pci_dev *pdev)
{
	return !!pci_match_id(bypass_pci_id_table, pdev);
}

/*
 * 0x84 is the offset of in/out threshold register,
 * and it is the same offset as the register of 'hostpc'.
 */
#define	intel_quark_x1000_insnreg01	hostpc

/* Maximum usable threshold value is 0x7f dwords for both IN and OUT */
#define INTEL_QUARK_X1000_EHCI_MAX_THRESHOLD	0x007f007f

/* called after powerup, by probe or system-pm "wakeup" */
static int ehci_pci_reinit(struct ehci_hcd *ehci, struct pci_dev *pdev)
{
	int			retval;

	/* we expect static quirk code to handle the "extended capabilities"
	 * (currently just BIOS handoff) allowed starting with EHCI 0.96
	 */

	/* PCI Memory-Write-Invalidate cycle support is optional (uncommon) */
	retval = pci_set_mwi(pdev);
	if (!retval)
		ehci_dbg(ehci, "MWI active\n");

	/* Reset the threshold limit */
	if (is_intel_quark_x1000(pdev)) {
		/*
		 * For the Intel QUARK X1000, raise the I/O threshold to the
		 * maximum usable value in order to improve performance.
		 */
		ehci_writel(ehci, INTEL_QUARK_X1000_EHCI_MAX_THRESHOLD,
			ehci->regs->intel_quark_x1000_insnreg01);
	}

	return 0;
}

/* called during probe() after chip reset completes */
static int ehci_pci_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci(hcd);
	struct pci_dev		*pdev = to_pci_dev(hcd->self.controller);
	u32			temp;
	int			retval;

	ehci->caps = hcd->regs;

	/*
	 * ehci_init() causes memory for DMA transfers to be
	 * allocated.  Thus, any vendor-specific workarounds based on
	 * limiting the type of memory used for DMA transfers must
	 * happen before ehci_setup() is called.
	 *
	 * Most other workarounds can be done either before or after
	 * init and reset; they are located here too.
	 */
	switch (pdev->vendor) {
	case PCI_VENDOR_ID_TOSHIBA_2:
		/* celleb's companion chip */
		if (pdev->device == 0x01b5) {
#ifdef CONFIG_USB_EHCI_BIG_ENDIAN_MMIO
			ehci->big_endian_mmio = 1;
#else
			ehci_warn(ehci,
				  "unsupported big endian Toshiba quirk\n");
#endif
		}
		break;
	case PCI_VENDOR_ID_NVIDIA:
		/* NVidia reports that certain chips don't handle
		 * QH, ITD, or SITD addresses above 2GB.  (But TD,
		 * data buffer, and periodic schedule are normal.)
		 */
		switch (pdev->device) {
		case 0x003c:	/* MCP04 */
		case 0x005b:	/* CK804 */
		case 0x00d8:	/* CK8 */
		case 0x00e8:	/* CK8S */
			if (dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(31)) < 0)
				ehci_warn(ehci, "can't enable NVidia "
					"workaround for >2GB RAM\n");
			break;

		/* Some NForce2 chips have problems with selective suspend;
		 * fixed in newer silicon.
		 */
		case 0x0068:
			if (pdev->revision < 0xa4)
				ehci->no_selective_suspend = 1;
			break;
		}
		break;
	case PCI_VENDOR_ID_INTEL:
		if (pdev->device == PCI_DEVICE_ID_INTEL_CE4100_USB)
			hcd->has_tt = 1;
		break;
	case PCI_VENDOR_ID_TDI:
		if (pdev->device == PCI_DEVICE_ID_TDI_EHCI)
			hcd->has_tt = 1;
		break;
	case PCI_VENDOR_ID_AMD:
		/* AMD PLL quirk */
		if (usb_amd_quirk_pll_check())
			ehci->amd_pll_fix = 1;
		/* AMD8111 EHCI doesn't work, according to AMD errata */
		if (pdev->device == 0x7463) {
			ehci_info(ehci, "ignoring AMD8111 (errata)\n");
			retval = -EIO;
			goto done;
		}

		/*
		 * EHCI controller on AMD SB700/SB800/Hudson-2/3 platforms may
		 * read/write memory space which does not belong to it when
		 * there is NULL pointer with T-bit set to 1 in the frame list
		 * table. To avoid the issue, the frame list link pointer
		 * should always contain a valid pointer to a inactive qh.
		 */
		if (pdev->device == 0x7808) {
			ehci->use_dummy_qh = 1;
			ehci_info(ehci, "applying AMD SB700/SB800/Hudson-2/3 EHCI dummy qh workaround\n");
		}
		break;
	case PCI_VENDOR_ID_VIA:
		if (pdev->device == 0x3104 && (pdev->revision & 0xf0) == 0x60) {
			u8 tmp;

			/* The VT6212 defaults to a 1 usec EHCI sleep time which
			 * hogs the PCI bus *badly*. Setting bit 5 of 0x4B makes
			 * that sleep time use the conventional 10 usec.
			 */
			pci_read_config_byte(pdev, 0x4b, &tmp);
			if (tmp & 0x20)
				break;
			pci_write_config_byte(pdev, 0x4b, tmp | 0x20);
		}
		break;
	case PCI_VENDOR_ID_ATI:
		/* AMD PLL quirk */
		if (usb_amd_quirk_pll_check())
			ehci->amd_pll_fix = 1;

		/*
		 * EHCI controller on AMD SB700/SB800/Hudson-2/3 platforms may
		 * read/write memory space which does not belong to it when
		 * there is NULL pointer with T-bit set to 1 in the frame list
		 * table. To avoid the issue, the frame list link pointer
		 * should always contain a valid pointer to a inactive qh.
		 */
		if (pdev->device == 0x4396) {
			ehci->use_dummy_qh = 1;
			ehci_info(ehci, "applying AMD SB700/SB800/Hudson-2/3 EHCI dummy qh workaround\n");
		}
		/* SB600 and old version of SB700 have a bug in EHCI controller,
		 * which causes usb devices lose response in some cases.
		 */
		if ((pdev->device == 0x4386 || pdev->device == 0x4396) &&
				usb_amd_hang_symptom_quirk()) {
			u8 tmp;
			ehci_info(ehci, "applying AMD SB600/SB700 USB freeze workaround\n");
			pci_read_config_byte(pdev, 0x53, &tmp);
			pci_write_config_byte(pdev, 0x53, tmp | (1<<3));
		}
		break;
	case PCI_VENDOR_ID_NETMOS:
		/* MosChip frame-index-register bug */
		ehci_info(ehci, "applying MosChip frame-index workaround\n");
		ehci->frame_index_bug = 1;
		break;
	case PCI_VENDOR_ID_HUAWEI:
		/* Synopsys HC bug */
		if (pdev->device == 0xa239) {
			ehci_info(ehci, "applying Synopsys HC workaround\n");
			ehci->has_synopsys_hc_bug = 1;
		}
		break;
	case PCI_VENDOR_ID_ASPEED:
		if (pdev->device == PCI_DEVICE_ID_ASPEED_EHCI) {
			ehci_info(ehci, "applying Aspeed HC workaround\n");
			ehci->is_aspeed = 1;
		}
		break;
	case PCI_VENDOR_ID_ZHAOXIN:
		if (pdev->device == 0x3104 && (pdev->revision & 0xf0) == 0x90)
			ehci->zx_wakeup_clear_needed = 1;
		break;
	}

	/* optional debug port, normally in the first BAR */
	temp = pci_find_capability(pdev, PCI_CAP_ID_DBG);
	if (temp) {
		pci_read_config_dword(pdev, temp, &temp);
		temp >>= 16;
		if (((temp >> 13) & 7) == 1) {
			u32 hcs_params = ehci_readl(ehci,
						    &ehci->caps->hcs_params);

			temp &= 0x1fff;
			ehci->debug = hcd->regs + temp;
			temp = ehci_readl(ehci, &ehci->debug->control);
			ehci_info(ehci, "debug port %d%s\n",
				  HCS_DEBUG_PORT(hcs_params),
				  (temp & DBGP_ENABLED) ? " IN USE" : "");
			if (!(temp & DBGP_ENABLED))
				ehci->debug = NULL;
		}
	}

	retval = ehci_setup(hcd);
	if (retval)
		return retval;

	/* These workarounds need to be applied after ehci_setup() */
	switch (pdev->vendor) {
	case PCI_VENDOR_ID_NEC:
	case PCI_VENDOR_ID_INTEL:
	case PCI_VENDOR_ID_AMD:
		ehci->need_io_watchdog = 0;
		break;
	case PCI_VENDOR_ID_NVIDIA:
		switch (pdev->device) {
		/* MCP89 chips on the MacBookAir3,1 give EPROTO when
		 * fetching device descriptors unless LPM is disabled.
		 * There are also intermittent problems enumerating
		 * devices with PPCD enabled.
		 */
		case 0x0d9d:
			ehci_info(ehci, "disable ppcd for nvidia mcp89\n");
			ehci->has_ppcd = 0;
			ehci->command &= ~CMD_PPCEE;
			break;
		}
		break;
	}

	/* at least the Genesys GL880S needs fixup here */
	temp = HCS_N_CC(ehci->hcs_params) * HCS_N_PCC(ehci->hcs_params);
	temp &= 0x0f;
	if (temp && HCS_N_PORTS(ehci->hcs_params) > temp) {
		ehci_dbg(ehci, "bogus port configuration: "
			"cc=%d x pcc=%d < ports=%d\n",
			HCS_N_CC(ehci->hcs_params),
			HCS_N_PCC(ehci->hcs_params),
			HCS_N_PORTS(ehci->hcs_params));

		switch (pdev->vendor) {
		case 0x17a0:		/* GENESYS */
			/* GL880S: should be PORTS=2 */
			temp |= (ehci->hcs_params & ~0xf);
			ehci->hcs_params = temp;
			break;
		case PCI_VENDOR_ID_NVIDIA:
			/* NF4: should be PCC=10 */
			break;
		}
	}

	/* Serial Bus Release Number is at PCI 0x60 offset */
	if (pdev->vendor == PCI_VENDOR_ID_STMICRO
	    && pdev->device == PCI_DEVICE_ID_STMICRO_USB_HOST)
		;	/* ConneXT has no sbrn register */
	else if (pdev->vendor == PCI_VENDOR_ID_HUAWEI
			 && pdev->device == 0xa239)
		;	/* HUAWEI Kunpeng920 USB EHCI has no sbrn register */
	else
		pci_read_config_byte(pdev, 0x60, &ehci->sbrn);

	/* Keep this around for a while just in case some EHCI
	 * implementation uses legacy PCI PM support.  This test
	 * can be removed on 17 Dec 2009 if the dev_warn() hasn't
	 * been triggered by then.
	 */
	if (!device_can_wakeup(&pdev->dev)) {
		u16	port_wake;

		pci_read_config_word(pdev, 0x62, &port_wake);
		if (port_wake & 0x0001) {
			dev_warn(&pdev->dev, "Enabling legacy PCI PM\n");
			device_set_wakeup_capable(&pdev->dev, 1);
		}
	}

#ifdef	CONFIG_PM
	if (ehci->no_selective_suspend && device_can_wakeup(&pdev->dev))
		ehci_warn(ehci, "selective suspend/wakeup unavailable\n");
#endif

	retval = ehci_pci_reinit(ehci, pdev);
done:
	return retval;
}

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_PM

/* suspend/resume, section 4.3 */

/* These routines rely on the PCI bus glue
 * to handle powerdown and wakeup, and currently also on
 * transceivers that don't need any software attention to set up
 * the right sort of wakeup.
 * Also they depend on separate root hub suspend/resume.
 */

static int ehci_pci_resume(struct usb_hcd *hcd, bool hibernated)
{
	struct ehci_hcd		*ehci = hcd_to_ehci(hcd);
	struct pci_dev		*pdev = to_pci_dev(hcd->self.controller);

	if (ehci_resume(hcd, hibernated) != 0)
		(void) ehci_pci_reinit(ehci, pdev);
	return 0;
}

#else

#define ehci_suspend		NULL
#define ehci_pci_resume		NULL
#endif	/* CONFIG_PM */

static struct hc_driver __read_mostly ehci_pci_hc_driver;

static const struct ehci_driver_overrides pci_overrides __initconst = {
	.reset =		ehci_pci_setup,
};

/*-------------------------------------------------------------------------*/

static int ehci_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	if (is_bypassed_id(pdev))
		return -ENODEV;
	return usb_hcd_pci_probe(pdev, id, &ehci_pci_hc_driver);
}

static void ehci_pci_remove(struct pci_dev *pdev)
{
	pci_clear_mwi(pdev);
	usb_hcd_pci_remove(pdev);
}

/* PCI driver selection metadata; PCI hotplugging uses this */
static const struct pci_device_id pci_ids [] = { {
	/* handle any USB 2.0 EHCI controller */
	PCI_DEVICE_CLASS(PCI_CLASS_SERIAL_USB_EHCI, ~0),
	}, {
	PCI_VDEVICE(STMICRO, PCI_DEVICE_ID_STMICRO_USB_HOST),
	},
	{ /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, pci_ids);

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver ehci_pci_driver = {
	.name =		hcd_name,
	.id_table =	pci_ids,

	.probe =	ehci_pci_probe,
	.remove =	ehci_pci_remove,
	.shutdown = 	usb_hcd_pci_shutdown,

#ifdef CONFIG_PM
	.driver =	{
		.pm =	&usb_hcd_pci_pm_ops
	},
#endif
};

static int __init ehci_pci_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);

	ehci_init_driver(&ehci_pci_hc_driver, &pci_overrides);

	/* Entries for the PCI suspend/resume callbacks are special */
	ehci_pci_hc_driver.pci_suspend = ehci_suspend;
	ehci_pci_hc_driver.pci_resume = ehci_pci_resume;

	return pci_register_driver(&ehci_pci_driver);
}
module_init(ehci_pci_init);

static void __exit ehci_pci_cleanup(void)
{
	pci_unregister_driver(&ehci_pci_driver);
}
module_exit(ehci_pci_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("David Brownell");
MODULE_AUTHOR("Alan Stern");
MODULE_LICENSE("GPL");
