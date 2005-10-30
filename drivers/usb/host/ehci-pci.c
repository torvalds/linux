/*
 * EHCI HCD (Host Controller Driver) PCI Bus Glue.
 *
 * Copyright (c) 2000-2004 by David Brownell
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef CONFIG_PCI
#error "This file is PCI bus glue.  CONFIG_PCI must be defined."
#endif

/*-------------------------------------------------------------------------*/

/* EHCI 0.96 (and later) section 5.1 says how to kick BIOS/SMM/...
 * off the controller (maybe it can boot from highspeed USB disks).
 */
static int bios_handoff (struct ehci_hcd *ehci, int where, u32 cap)
{
	struct pci_dev *pdev = to_pci_dev(ehci_to_hcd(ehci)->self.controller);

	/* always say Linux will own the hardware */
	pci_write_config_byte(pdev, where + 3, 1);

	/* maybe wait a while for BIOS to respond */
	if (cap & (1 << 16)) {
		int msec = 5000;

		do {
			msleep(10);
			msec -= 10;
			pci_read_config_dword(pdev, where, &cap);
		} while ((cap & (1 << 16)) && msec);
		if (cap & (1 << 16)) {
			ehci_err(ehci, "BIOS handoff failed (%d, %08x)\n",
				where, cap);
			// some BIOS versions seem buggy...
			// return 1;
			ehci_warn (ehci, "continuing after BIOS bug...\n");
			/* disable all SMIs, and clear "BIOS owns" flag */
			pci_write_config_dword(pdev, where + 4, 0);
			pci_write_config_byte(pdev, where + 2, 0);
		} else
			ehci_dbg(ehci, "BIOS handoff succeeded\n");
	}
	return 0;
}

/* called by khubd or root hub init threads */
static int ehci_pci_reset (struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	u32			temp;
	unsigned		count = 256/4;

	spin_lock_init (&ehci->lock);

	ehci->caps = hcd->regs;
	ehci->regs = hcd->regs + HC_LENGTH (readl (&ehci->caps->hc_capbase));
	dbg_hcs_params (ehci, "reset");
	dbg_hcc_params (ehci, "reset");

	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = readl (&ehci->caps->hcs_params);

	if (hcd->self.controller->bus == &pci_bus_type) {
		struct pci_dev	*pdev = to_pci_dev(hcd->self.controller);

		switch (pdev->vendor) {
		case PCI_VENDOR_ID_TDI:
			if (pdev->device == PCI_DEVICE_ID_TDI_EHCI) {
				ehci->is_tdi_rh_tt = 1;
				tdi_reset (ehci);
			}
			break;
		case PCI_VENDOR_ID_AMD:
			/* AMD8111 EHCI doesn't work, according to AMD errata */
			if (pdev->device == 0x7463) {
				ehci_info (ehci, "ignoring AMD8111 (errata)\n");
				return -EIO;
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
				if (pci_set_consistent_dma_mask(pdev,
							DMA_31BIT_MASK) < 0)
					ehci_warn (ehci, "can't enable NVidia "
						"workaround for >2GB RAM\n");
				break;
			}
			break;
		}

		/* optional debug port, normally in the first BAR */
		temp = pci_find_capability (pdev, 0x0a);
		if (temp) {
			pci_read_config_dword(pdev, temp, &temp);
			temp >>= 16;
			if ((temp & (3 << 13)) == (1 << 13)) {
				temp &= 0x1fff;
				ehci->debug = hcd->regs + temp;
				temp = readl (&ehci->debug->control);
				ehci_info (ehci, "debug port %d%s\n",
					HCS_DEBUG_PORT(ehci->hcs_params),
					(temp & DBGP_ENABLED)
						? " IN USE"
						: "");
				if (!(temp & DBGP_ENABLED))
					ehci->debug = NULL;
			}
		}

		temp = HCC_EXT_CAPS (readl (&ehci->caps->hcc_params));
	} else
		temp = 0;

	/* EHCI 0.96 and later may have "extended capabilities" */
	while (temp && count--) {
		u32		cap;

		pci_read_config_dword (to_pci_dev(hcd->self.controller),
				temp, &cap);
		ehci_dbg (ehci, "capability %04x at %02x\n", cap, temp);
		switch (cap & 0xff) {
		case 1:			/* BIOS/SMM/... handoff */
			if (bios_handoff (ehci, temp, cap) != 0)
				return -EOPNOTSUPP;
			break;
		case 0:			/* illegal reserved capability */
			ehci_warn (ehci, "illegal capability!\n");
			cap = 0;
			/* FALLTHROUGH */
		default:		/* unknown */
			break;
		}
		temp = (cap >> 8) & 0xff;
	}
	if (!count) {
		ehci_err (ehci, "bogus capabilities ... PCI problems!\n");
		return -EIO;
	}
	if (ehci_is_TDI(ehci))
		ehci_reset (ehci);

	ehci_port_power (ehci, 0);

	/* at least the Genesys GL880S needs fixup here */
	temp = HCS_N_CC(ehci->hcs_params) * HCS_N_PCC(ehci->hcs_params);
	temp &= 0x0f;
	if (temp && HCS_N_PORTS(ehci->hcs_params) > temp) {
		ehci_dbg (ehci, "bogus port configuration: "
			"cc=%d x pcc=%d < ports=%d\n",
			HCS_N_CC(ehci->hcs_params),
			HCS_N_PCC(ehci->hcs_params),
			HCS_N_PORTS(ehci->hcs_params));

		if (hcd->self.controller->bus == &pci_bus_type) {
			struct pci_dev	*pdev;

			pdev = to_pci_dev(hcd->self.controller);
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
	}

	/* force HC to halt state */
	return ehci_halt (ehci);
}

static int ehci_pci_start (struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	int result = 0;

	if (hcd->self.controller->bus == &pci_bus_type) {
		struct pci_dev		*pdev;
		u16			port_wake;

		pdev = to_pci_dev(hcd->self.controller);

		/* Serial Bus Release Number is at PCI 0x60 offset */
		pci_read_config_byte(pdev, 0x60, &ehci->sbrn);

		/* port wake capability, reported by boot firmware */
		pci_read_config_word(pdev, 0x62, &port_wake);
		hcd->can_wakeup = (port_wake & 1) != 0;

		/* help hc dma work well with cachelines */
		result = pci_set_mwi(pdev);
		if (result)
			ehci_dbg(ehci, "unable to enable MWI - not fatal.\n");
	}

	return ehci_run (hcd);
}

/* always called by thread; normally rmmod */

static void ehci_pci_stop (struct usb_hcd *hcd)
{
	ehci_stop (hcd);
}

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_PM

/* suspend/resume, section 4.3 */

/* These routines rely on the bus (pci, platform, etc)
 * to handle powerdown and wakeup, and currently also on
 * transceivers that don't need any software attention to set up
 * the right sort of wakeup.
 */

static int ehci_pci_suspend (struct usb_hcd *hcd, pm_message_t message)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);

	if (time_before (jiffies, ehci->next_statechange))
		msleep (100);

#ifdef	CONFIG_USB_SUSPEND
	(void) usb_suspend_device (hcd->self.root_hub);
#else
	usb_lock_device (hcd->self.root_hub);
	(void) ehci_bus_suspend (hcd);
	usb_unlock_device (hcd->self.root_hub);
#endif

	// save (PCI) FLADJ in case of Vaux power loss
	// ... we'd only use it to handle clock skew

	return 0;
}

static int ehci_pci_resume (struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	unsigned		port;
	struct usb_device	*root = hcd->self.root_hub;
	int			retval = -EINVAL;

	// maybe restore (PCI) FLADJ

	if (time_before (jiffies, ehci->next_statechange))
		msleep (100);

	/* If any port is suspended (or owned by the companion),
	 * we know we can/must resume the HC (and mustn't reset it).
	 */
	for (port = HCS_N_PORTS (ehci->hcs_params); port > 0; ) {
		u32	status;
		port--;
		status = readl (&ehci->regs->port_status [port]);
		if (!(status & PORT_POWER))
			continue;
		if (status & (PORT_SUSPEND | PORT_OWNER)) {
			down (&hcd->self.root_hub->serialize);
			retval = ehci_bus_resume (hcd);
			up (&hcd->self.root_hub->serialize);
			break;
		}
		if (!root->children [port])
			continue;
		dbg_port (ehci, __FUNCTION__, port + 1, status);
		usb_set_device_state (root->children[port],
					USB_STATE_NOTATTACHED);
	}

	/* Else reset, to cope with power loss or flush-to-storage
	 * style "resume" having activated BIOS during reboot.
	 */
	if (port == 0) {
		(void) ehci_halt (ehci);
		(void) ehci_reset (ehci);
		(void) ehci_pci_reset (hcd);

		/* emptying the schedule aborts any urbs */
		spin_lock_irq (&ehci->lock);
		if (ehci->reclaim)
			ehci->reclaim_ready = 1;
		ehci_work (ehci, NULL);
		spin_unlock_irq (&ehci->lock);

		/* restart; khubd will disconnect devices */
		retval = ehci_run (hcd);

		/* here we "know" root ports should always stay powered;
		 * but some controllers may lose all power.
		 */
		ehci_port_power (ehci, 1);
	}

	return retval;
}
#endif

static const struct hc_driver ehci_pci_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"EHCI Host Controller",
	.hcd_priv_size =	sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq =			ehci_irq,
	.flags =		HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset =		ehci_pci_reset,
	.start =		ehci_pci_start,
#ifdef	CONFIG_PM
	.suspend =		ehci_pci_suspend,
	.resume =		ehci_pci_resume,
#endif
	.stop =			ehci_pci_stop,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		ehci_urb_enqueue,
	.urb_dequeue =		ehci_urb_dequeue,
	.endpoint_disable =	ehci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number =	ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data =	ehci_hub_status_data,
	.hub_control =		ehci_hub_control,
	.bus_suspend =		ehci_bus_suspend,
	.bus_resume =		ehci_bus_resume,
};

/*-------------------------------------------------------------------------*/

/* PCI driver selection metadata; PCI hotplugging uses this */
static const struct pci_device_id pci_ids [] = { {
	/* handle any USB 2.0 EHCI controller */
	PCI_DEVICE_CLASS(((PCI_CLASS_SERIAL_USB << 8) | 0x20), ~0),
	.driver_data =	(unsigned long) &ehci_pci_hc_driver,
	},
	{ /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE (pci, pci_ids);

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver ehci_pci_driver = {
	.name =		(char *) hcd_name,
	.id_table =	pci_ids,
	.owner = 	THIS_MODULE,

	.probe =	usb_hcd_pci_probe,
	.remove =	usb_hcd_pci_remove,

#ifdef	CONFIG_PM
	.suspend =	usb_hcd_pci_suspend,
	.resume =	usb_hcd_pci_resume,
#endif
};

static int __init ehci_hcd_pci_init (void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_debug ("%s: block sizes: qh %Zd qtd %Zd itd %Zd sitd %Zd\n",
		hcd_name,
		sizeof (struct ehci_qh), sizeof (struct ehci_qtd),
		sizeof (struct ehci_itd), sizeof (struct ehci_sitd));

	return pci_register_driver (&ehci_pci_driver);
}
module_init (ehci_hcd_pci_init);

static void __exit ehci_hcd_pci_cleanup (void)
{
	pci_unregister_driver (&ehci_pci_driver);
}
module_exit (ehci_hcd_pci_cleanup);
