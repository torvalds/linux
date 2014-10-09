/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 *
 * [ Initialisation is based on Linus'  ]
 * [ uhci code and gregs ohci fragments ]
 * [ (C) Copyright 1999 Linus Torvalds  ]
 * [ (C) Copyright 1999 Gregory P. Smith]
 *
 * PCI Bus Glue
 *
 * This file is licenced under the GPL.
 */

#ifndef CONFIG_PCI
#error "This file is PCI bus glue.  CONFIG_PCI must be defined."
#endif

#include <linux/pci.h>
#include <linux/io.h>


/*-------------------------------------------------------------------------*/

static int broken_suspend(struct usb_hcd *hcd)
{
	device_init_wakeup(&hcd->self.root_hub->dev, 0);
	return 0;
}

/* AMD 756, for most chips (early revs), corrupts register
 * values on read ... so enable the vendor workaround.
 */
static int ohci_quirk_amd756(struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);

	ohci->flags = OHCI_QUIRK_AMD756;
	ohci_dbg (ohci, "AMD756 erratum 4 workaround\n");

	/* also erratum 10 (suspend/resume issues) */
	return broken_suspend(hcd);
}

/* Apple's OHCI driver has a lot of bizarre workarounds
 * for this chip.  Evidently control and bulk lists
 * can get confused.  (B&W G3 models, and ...)
 */
static int ohci_quirk_opti(struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);

	ohci_dbg (ohci, "WARNING: OPTi workarounds unavailable\n");

	return 0;
}

/* Check for NSC87560. We have to look at the bridge (fn1) to
 * identify the USB (fn2). This quirk might apply to more or
 * even all NSC stuff.
 */
static int ohci_quirk_ns(struct usb_hcd *hcd)
{
	struct pci_dev *pdev = to_pci_dev(hcd->self.controller);
	struct pci_dev	*b;

	b  = pci_get_slot (pdev->bus, PCI_DEVFN (PCI_SLOT (pdev->devfn), 1));
	if (b && b->device == PCI_DEVICE_ID_NS_87560_LIO
	    && b->vendor == PCI_VENDOR_ID_NS) {
		struct ohci_hcd	*ohci = hcd_to_ohci (hcd);

		ohci->flags |= OHCI_QUIRK_SUPERIO;
		ohci_dbg (ohci, "Using NSC SuperIO setup\n");
	}
	pci_dev_put(b);

	return 0;
}

/* Check for Compaq's ZFMicro chipset, which needs short
 * delays before control or bulk queues get re-activated
 * in finish_unlinks()
 */
static int ohci_quirk_zfmicro(struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);

	ohci->flags |= OHCI_QUIRK_ZFMICRO;
	ohci_dbg(ohci, "enabled Compaq ZFMicro chipset quirks\n");

	return 0;
}

/* Check for Toshiba SCC OHCI which has big endian registers
 * and little endian in memory data structures
 */
static int ohci_quirk_toshiba_scc(struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);

	/* That chip is only present in the southbridge of some
	 * cell based platforms which are supposed to select
	 * CONFIG_USB_OHCI_BIG_ENDIAN_MMIO. We verify here if
	 * that was the case though.
	 */
#ifdef CONFIG_USB_OHCI_BIG_ENDIAN_MMIO
	ohci->flags |= OHCI_QUIRK_BE_MMIO;
	ohci_dbg (ohci, "enabled big endian Toshiba quirk\n");
	return 0;
#else
	ohci_err (ohci, "unsupported big endian Toshiba quirk\n");
	return -ENXIO;
#endif
}

/* Check for NEC chip and apply quirk for allegedly lost interrupts.
 */

static void ohci_quirk_nec_worker(struct work_struct *work)
{
	struct ohci_hcd *ohci = container_of(work, struct ohci_hcd, nec_work);
	int status;

	status = ohci_init(ohci);
	if (status != 0) {
		ohci_err(ohci, "Restarting NEC controller failed in %s, %d\n",
			 "ohci_init", status);
		return;
	}

	status = ohci_restart(ohci);
	if (status != 0)
		ohci_err(ohci, "Restarting NEC controller failed in %s, %d\n",
			 "ohci_restart", status);
}

static int ohci_quirk_nec(struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);

	ohci->flags |= OHCI_QUIRK_NEC;
	INIT_WORK(&ohci->nec_work, ohci_quirk_nec_worker);
	ohci_dbg (ohci, "enabled NEC chipset lost interrupt quirk\n");

	return 0;
}

static int ohci_quirk_amd700(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	struct pci_dev *amd_smbus_dev;
	u8 rev;

	if (usb_amd_find_chipset_info())
		ohci->flags |= OHCI_QUIRK_AMD_PLL;

	amd_smbus_dev = pci_get_device(PCI_VENDOR_ID_ATI,
			PCI_DEVICE_ID_ATI_SBX00_SMBUS, NULL);
	if (!amd_smbus_dev)
		return 0;

	rev = amd_smbus_dev->revision;

	/* SB800 needs pre-fetch fix */
	if ((rev >= 0x40) && (rev <= 0x4f)) {
		ohci->flags |= OHCI_QUIRK_AMD_PREFETCH;
		ohci_dbg(ohci, "enabled AMD prefetch quirk\n");
	}

	pci_dev_put(amd_smbus_dev);
	amd_smbus_dev = NULL;

	return 0;
}

static void sb800_prefetch(struct ohci_hcd *ohci, int on)
{
	struct pci_dev *pdev;
	u16 misc;

	pdev = to_pci_dev(ohci_to_hcd(ohci)->self.controller);
	pci_read_config_word(pdev, 0x50, &misc);
	if (on == 0)
		pci_write_config_word(pdev, 0x50, misc & 0xfcff);
	else
		pci_write_config_word(pdev, 0x50, misc | 0x0300);
}

/* List of quirks for OHCI */
static const struct pci_device_id ohci_pci_quirks[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_AMD, 0x740c),
		.driver_data = (unsigned long)ohci_quirk_amd756,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_OPTI, 0xc861),
		.driver_data = (unsigned long)ohci_quirk_opti,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_NS, PCI_ANY_ID),
		.driver_data = (unsigned long)ohci_quirk_ns,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_COMPAQ, 0xa0f8),
		.driver_data = (unsigned long)ohci_quirk_zfmicro,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_TOSHIBA_2, 0x01b6),
		.driver_data = (unsigned long)ohci_quirk_toshiba_scc,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_NEC, PCI_DEVICE_ID_NEC_USB),
		.driver_data = (unsigned long)ohci_quirk_nec,
	},
	{
		/* Toshiba portege 4000 */
		.vendor		= PCI_VENDOR_ID_AL,
		.device		= 0x5237,
		.subvendor	= PCI_VENDOR_ID_TOSHIBA,
		.subdevice	= 0x0004,
		.driver_data	= (unsigned long) broken_suspend,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_ITE, 0x8152),
		.driver_data = (unsigned long) broken_suspend,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_ATI, 0x4397),
		.driver_data = (unsigned long)ohci_quirk_amd700,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_ATI, 0x4398),
		.driver_data = (unsigned long)ohci_quirk_amd700,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_ATI, 0x4399),
		.driver_data = (unsigned long)ohci_quirk_amd700,
	},

	/* FIXME for some of the early AMD 760 southbridges, OHCI
	 * won't work at all.  blacklist them.
	 */

	{},
};

static int ohci_pci_reset (struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	int ret = 0;

	if (hcd->self.controller) {
		struct pci_dev *pdev = to_pci_dev(hcd->self.controller);
		const struct pci_device_id *quirk_id;

		quirk_id = pci_match_id(ohci_pci_quirks, pdev);
		if (quirk_id != NULL) {
			int (*quirk)(struct usb_hcd *ohci);
			quirk = (void *)quirk_id->driver_data;
			ret = quirk(hcd);
		}
	}
	if (ret == 0) {
		ohci_hcd_init (ohci);
		return ohci_init (ohci);
	}
	return ret;
}


static int ohci_pci_start (struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	int		ret;

#ifdef CONFIG_PM /* avoid warnings about unused pdev */
	if (hcd->self.controller) {
		struct pci_dev *pdev = to_pci_dev(hcd->self.controller);

		/* RWC may not be set for add-in PCI cards, since boot
		 * firmware probably ignored them.  This transfers PCI
		 * PM wakeup capabilities.
		 */
		if (device_can_wakeup(&pdev->dev))
			ohci->hc_control |= OHCI_CTRL_RWC;
	}
#endif /* CONFIG_PM */

	ret = ohci_run (ohci);
	if (ret < 0) {
		ohci_err (ohci, "can't start\n");
		ohci_stop (hcd);
	}
	return ret;
}


/*-------------------------------------------------------------------------*/

static const struct hc_driver ohci_pci_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"OHCI Host Controller",
	.hcd_priv_size =	sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq =			ohci_irq,
	.flags =		HCD_MEMORY | HCD_USB11,

	/*
	 * basic lifecycle operations
	 */
	.reset =		ohci_pci_reset,
	.start =		ohci_pci_start,
	.stop =			ohci_stop,
	.shutdown =		ohci_shutdown,

#ifdef	CONFIG_PM
	.pci_suspend =		ohci_suspend,
	.pci_resume =		ohci_resume,
#endif

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		ohci_urb_enqueue,
	.urb_dequeue =		ohci_urb_dequeue,
	.endpoint_disable =	ohci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number =	ohci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data =	ohci_hub_status_data,
	.hub_control =		ohci_hub_control,
#ifdef	CONFIG_PM
	.bus_suspend =		ohci_bus_suspend,
	.bus_resume =		ohci_bus_resume,
#endif
	.start_port_reset =	ohci_start_port_reset,
};

/*-------------------------------------------------------------------------*/


static const struct pci_device_id pci_ids [] = { {
	/* handle any USB OHCI controller */
	PCI_DEVICE_CLASS(PCI_CLASS_SERIAL_USB_OHCI, ~0),
	.driver_data =	(unsigned long) &ohci_pci_hc_driver,
	}, {
	/* The device in the ConneXT I/O hub has no class reg */
	PCI_VDEVICE(STMICRO, PCI_DEVICE_ID_STMICRO_USB_OHCI),
	.driver_data =	(unsigned long) &ohci_pci_hc_driver,
	}, { /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE (pci, pci_ids);

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver ohci_pci_driver = {
	.name =		(char *) hcd_name,
	.id_table =	pci_ids,

	.probe =	usb_hcd_pci_probe,
	.remove =	usb_hcd_pci_remove,
	.shutdown =	usb_hcd_pci_shutdown,

#ifdef CONFIG_PM
	.driver =	{
		.pm =	&usb_hcd_pci_pm_ops
	},
#endif
};
