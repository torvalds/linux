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


/* constants used to work around PM-related transfer
 * glitches in some AMD 700 series southbridges
 */
#define AB_REG_BAR	0xf0
#define AB_INDX(addr)	((addr) + 0x00)
#define AB_DATA(addr)	((addr) + 0x04)
#define AX_INDXC	0X30
#define AX_DATAC	0x34

#define NB_PCIE_INDX_ADDR	0xe0
#define NB_PCIE_INDX_DATA	0xe4
#define PCIE_P_CNTL		0x10040
#define BIF_NB			0x10002

static struct pci_dev *amd_smbus_dev;
static struct pci_dev *amd_hb_dev;
static int amd_ohci_iso_count;

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
	u8 rev = 0;

	if (!amd_smbus_dev)
		amd_smbus_dev = pci_get_device(PCI_VENDOR_ID_ATI,
				PCI_DEVICE_ID_ATI_SBX00_SMBUS, NULL);
	if (!amd_smbus_dev)
		return 0;

	pci_read_config_byte(amd_smbus_dev, PCI_REVISION_ID, &rev);
	if ((rev > 0x3b) || (rev < 0x30)) {
		pci_dev_put(amd_smbus_dev);
		amd_smbus_dev = NULL;
		return 0;
	}

	amd_ohci_iso_count++;

	if (!amd_hb_dev)
		amd_hb_dev = pci_get_device(PCI_VENDOR_ID_AMD, 0x9600, NULL);

	ohci->flags |= OHCI_QUIRK_AMD_ISO;
	ohci_dbg(ohci, "enabled AMD ISO transfers quirk\n");

	return 0;
}

/*
 * The hardware normally enables the A-link power management feature, which
 * lets the system lower the power consumption in idle states.
 *
 * Assume the system is configured to have USB 1.1 ISO transfers going
 * to or from a USB device.  Without this quirk, that stream may stutter
 * or have breaks occasionally.  For transfers going to speakers, this
 * makes a very audible mess...
 *
 * That audio playback corruption is due to the audio stream getting
 * interrupted occasionally when the link goes in lower power state
 * This USB quirk prevents the link going into that lower power state
 * during audio playback or other ISO operations.
 */
static void quirk_amd_pll(int on)
{
	u32 addr;
	u32 val;
	u32 bit = (on > 0) ? 1 : 0;

	pci_read_config_dword(amd_smbus_dev, AB_REG_BAR, &addr);

	/* BIT names/meanings are NDA-protected, sorry ... */

	outl(AX_INDXC, AB_INDX(addr));
	outl(0x40, AB_DATA(addr));
	outl(AX_DATAC, AB_INDX(addr));
	val = inl(AB_DATA(addr));
	val &= ~((1 << 3) | (1 << 4) | (1 << 9));
	val |= (bit << 3) | ((!bit) << 4) | ((!bit) << 9);
	outl(val, AB_DATA(addr));

	if (amd_hb_dev) {
		addr = PCIE_P_CNTL;
		pci_write_config_dword(amd_hb_dev, NB_PCIE_INDX_ADDR, addr);

		pci_read_config_dword(amd_hb_dev, NB_PCIE_INDX_DATA, &val);
		val &= ~(1 | (1 << 3) | (1 << 4) | (1 << 9) | (1 << 12));
		val |= bit | (bit << 3) | (bit << 12);
		val |= ((!bit) << 4) | ((!bit) << 9);
		pci_write_config_dword(amd_hb_dev, NB_PCIE_INDX_DATA, val);

		addr = BIF_NB;
		pci_write_config_dword(amd_hb_dev, NB_PCIE_INDX_ADDR, addr);

		pci_read_config_dword(amd_hb_dev, NB_PCIE_INDX_DATA, &val);
		val &= ~(1 << 8);
		val |= bit << 8;
		pci_write_config_dword(amd_hb_dev, NB_PCIE_INDX_DATA, val);
	}
}

static void amd_iso_dev_put(void)
{
	amd_ohci_iso_count--;
	if (amd_ohci_iso_count == 0) {
		if (amd_smbus_dev) {
			pci_dev_put(amd_smbus_dev);
			amd_smbus_dev = NULL;
		}
		if (amd_hb_dev) {
			pci_dev_put(amd_hb_dev);
			amd_hb_dev = NULL;
		}
	}

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


static int __devinit ohci_pci_start (struct usb_hcd *hcd)
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

#ifdef	CONFIG_PM

static int ohci_pci_suspend (struct usb_hcd *hcd, pm_message_t message)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	unsigned long	flags;
	int		rc = 0;

	/* Root hub was already suspended. Disable irq emission and
	 * mark HW unaccessible, bail out if RH has been resumed. Use
	 * the spinlock to properly synchronize with possible pending
	 * RH suspend or resume activity.
	 *
	 * This is still racy as hcd->state is manipulated outside of
	 * any locks =P But that will be a different fix.
	 */
	spin_lock_irqsave (&ohci->lock, flags);
	if (hcd->state != HC_STATE_SUSPENDED) {
		rc = -EINVAL;
		goto bail;
	}
	ohci_writel(ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
	(void)ohci_readl(ohci, &ohci->regs->intrdisable);

	/* make sure snapshot being resumed re-enumerates everything */
	if (message.event == PM_EVENT_PRETHAW)
		ohci_usb_reset(ohci);

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
 bail:
	spin_unlock_irqrestore (&ohci->lock, flags);

	return rc;
}


static int ohci_pci_resume (struct usb_hcd *hcd)
{
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	ohci_finish_controller_resume(hcd);
	return 0;
}

#endif	/* CONFIG_PM */


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
	.pci_suspend =		ohci_pci_suspend,
	.pci_resume =		ohci_pci_resume,
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

#ifdef CONFIG_PM_SLEEP
	.driver =	{
		.pm =	&usb_hcd_pci_pm_ops
	},
#endif
};
