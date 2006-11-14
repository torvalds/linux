/*
 * OHCI HCD (Host Controller Driver) for USB.
 * 
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2004 David Brownell <dbrownell@users.sourceforge.net>
 * 
 * This file is licenced under GPL
 */

/*-------------------------------------------------------------------------*/

/*
 * OHCI Root Hub ... the nonsharable stuff
 */

#define dbg_port(hc,label,num,value) \
	ohci_dbg (hc, \
		"%s roothub.portstatus [%d] " \
		"= 0x%08x%s%s%s%s%s%s%s%s%s%s%s%s\n", \
		label, num, temp, \
		(temp & RH_PS_PRSC) ? " PRSC" : "", \
		(temp & RH_PS_OCIC) ? " OCIC" : "", \
		(temp & RH_PS_PSSC) ? " PSSC" : "", \
		(temp & RH_PS_PESC) ? " PESC" : "", \
		(temp & RH_PS_CSC) ? " CSC" : "", \
 		\
		(temp & RH_PS_LSDA) ? " LSDA" : "", \
		(temp & RH_PS_PPS) ? " PPS" : "", \
		(temp & RH_PS_PRS) ? " PRS" : "", \
		(temp & RH_PS_POCI) ? " POCI" : "", \
		(temp & RH_PS_PSS) ? " PSS" : "", \
 		\
		(temp & RH_PS_PES) ? " PES" : "", \
		(temp & RH_PS_CCS) ? " CCS" : "" \
		);

/*-------------------------------------------------------------------------*/

/* hcd->hub_irq_enable() */
static void ohci_rhsc_enable (struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);

	ohci_writel (ohci, OHCI_INTR_RHSC, &ohci->regs->intrenable);
}

#define OHCI_SCHED_ENABLES \
	(OHCI_CTRL_CLE|OHCI_CTRL_BLE|OHCI_CTRL_PLE|OHCI_CTRL_IE)

static void dl_done_list (struct ohci_hcd *);
static void finish_unlinks (struct ohci_hcd *, u16);

static int ohci_rh_suspend (struct ohci_hcd *ohci, int autostop)
__releases(ohci->lock)
__acquires(ohci->lock)
{
	int			status = 0;

	ohci->hc_control = ohci_readl (ohci, &ohci->regs->control);
	switch (ohci->hc_control & OHCI_CTRL_HCFS) {
	case OHCI_USB_RESUME:
		ohci_dbg (ohci, "resume/suspend?\n");
		ohci->hc_control &= ~OHCI_CTRL_HCFS;
		ohci->hc_control |= OHCI_USB_RESET;
		ohci_writel (ohci, ohci->hc_control, &ohci->regs->control);
		(void) ohci_readl (ohci, &ohci->regs->control);
		/* FALL THROUGH */
	case OHCI_USB_RESET:
		status = -EBUSY;
		ohci_dbg (ohci, "needs reinit!\n");
		goto done;
	case OHCI_USB_SUSPEND:
		if (!ohci->autostop) {
			ohci_dbg (ohci, "already suspended\n");
			goto done;
		}
	}
	ohci_dbg (ohci, "%s root hub\n",
			autostop ? "auto-stop" : "suspend");

	/* First stop any processing */
	if (!autostop && (ohci->hc_control & OHCI_SCHED_ENABLES)) {
		ohci->hc_control &= ~OHCI_SCHED_ENABLES;
		ohci_writel (ohci, ohci->hc_control, &ohci->regs->control);
		ohci->hc_control = ohci_readl (ohci, &ohci->regs->control);
		ohci_writel (ohci, OHCI_INTR_SF, &ohci->regs->intrstatus);

		/* sched disables take effect on the next frame,
		 * then the last WDH could take 6+ msec
		 */
		ohci_dbg (ohci, "stopping schedules ...\n");
		ohci->autostop = 0;
		spin_unlock_irq (&ohci->lock);
		msleep (8);
		spin_lock_irq (&ohci->lock);
	}
	dl_done_list (ohci);
	finish_unlinks (ohci, ohci_frame_no(ohci));

	/* maybe resume can wake root hub */
	if (device_may_wakeup(&ohci_to_hcd(ohci)->self.root_hub->dev) ||
			autostop)
		ohci->hc_control |= OHCI_CTRL_RWE;
	else {
		ohci_writel (ohci, OHCI_INTR_RHSC, &ohci->regs->intrdisable);
		ohci->hc_control &= ~OHCI_CTRL_RWE;
	}

	/* Suspend hub ... this is the "global (to this bus) suspend" mode,
	 * which doesn't imply ports will first be individually suspended.
	 */
	ohci->hc_control &= ~OHCI_CTRL_HCFS;
	ohci->hc_control |= OHCI_USB_SUSPEND;
	ohci_writel (ohci, ohci->hc_control, &ohci->regs->control);
	(void) ohci_readl (ohci, &ohci->regs->control);

	/* no resumes until devices finish suspending */
	if (!autostop) {
		ohci->next_statechange = jiffies + msecs_to_jiffies (5);
		ohci->autostop = 0;
	}

done:
	return status;
}

static inline struct ed *find_head (struct ed *ed)
{
	/* for bulk and control lists */
	while (ed->ed_prev)
		ed = ed->ed_prev;
	return ed;
}

static int ohci_restart (struct ohci_hcd *ohci);

/* caller has locked the root hub */
static int ohci_rh_resume (struct ohci_hcd *ohci)
__releases(ohci->lock)
__acquires(ohci->lock)
{
	struct usb_hcd		*hcd = ohci_to_hcd (ohci);
	u32			temp, enables;
	int			status = -EINPROGRESS;
	int			autostopped = ohci->autostop;

	ohci->autostop = 0;
	ohci->hc_control = ohci_readl (ohci, &ohci->regs->control);

	if (ohci->hc_control & (OHCI_CTRL_IR | OHCI_SCHED_ENABLES)) {
		/* this can happen after resuming a swsusp snapshot */
		if (hcd->state == HC_STATE_RESUMING) {
			ohci_dbg (ohci, "BIOS/SMM active, control %03x\n",
					ohci->hc_control);
			status = -EBUSY;
		/* this happens when pmcore resumes HC then root */
		} else {
			ohci_dbg (ohci, "duplicate resume\n");
			status = 0;
		}
	} else switch (ohci->hc_control & OHCI_CTRL_HCFS) {
	case OHCI_USB_SUSPEND:
		ohci->hc_control &= ~(OHCI_CTRL_HCFS|OHCI_SCHED_ENABLES);
		ohci->hc_control |= OHCI_USB_RESUME;
		ohci_writel (ohci, ohci->hc_control, &ohci->regs->control);
		(void) ohci_readl (ohci, &ohci->regs->control);
		ohci_dbg (ohci, "%s root hub\n",
				autostopped ? "auto-start" : "resume");
		break;
	case OHCI_USB_RESUME:
		/* HCFS changes sometime after INTR_RD */
		ohci_info (ohci, "wakeup\n");
		break;
	case OHCI_USB_OPER:
		/* this can happen after resuming a swsusp snapshot */
		ohci_dbg (ohci, "snapshot resume? reinit\n");
		status = -EBUSY;
		break;
	default:		/* RESET, we lost power */
		ohci_dbg (ohci, "lost power\n");
		status = -EBUSY;
	}
#ifdef	CONFIG_PM
	if (status == -EBUSY) {
		if (!autostopped) {
			spin_unlock_irq (&ohci->lock);
			(void) ohci_init (ohci);
			status = ohci_restart (ohci);
			spin_lock_irq (&ohci->lock);
		}
		return status;
	}
#endif
	if (status != -EINPROGRESS)
		return status;
	if (autostopped)
		goto skip_resume;
	spin_unlock_irq (&ohci->lock);

	temp = ohci->num_ports;
	while (temp--) {
		u32 stat = ohci_readl (ohci,
				       &ohci->regs->roothub.portstatus [temp]);

		/* force global, not selective, resume */
		if (!(stat & RH_PS_PSS))
			continue;
		ohci_writel (ohci, RH_PS_POCI,
				&ohci->regs->roothub.portstatus [temp]);
	}

	/* Some controllers (lucent erratum) need extra-long delays */
	msleep (20 /* usb 11.5.1.10 */ + 12 /* 32 msec counter */ + 1);

	temp = ohci_readl (ohci, &ohci->regs->control);
	temp &= OHCI_CTRL_HCFS;
	if (temp != OHCI_USB_RESUME) {
		ohci_err (ohci, "controller won't resume\n");
		return -EBUSY;
	}

	/* disable old schedule state, reinit from scratch */
	ohci_writel (ohci, 0, &ohci->regs->ed_controlhead);
	ohci_writel (ohci, 0, &ohci->regs->ed_controlcurrent);
	ohci_writel (ohci, 0, &ohci->regs->ed_bulkhead);
	ohci_writel (ohci, 0, &ohci->regs->ed_bulkcurrent);
	ohci_writel (ohci, 0, &ohci->regs->ed_periodcurrent);
	ohci_writel (ohci, (u32) ohci->hcca_dma, &ohci->regs->hcca);

	/* Sometimes PCI D3 suspend trashes frame timings ... */
	periodic_reinit (ohci);

	/* the following code is executed with ohci->lock held and
	 * irqs disabled if and only if autostopped is true
	 */

skip_resume:
	/* interrupts might have been disabled */
	ohci_writel (ohci, OHCI_INTR_INIT, &ohci->regs->intrenable);
	if (ohci->ed_rm_list)
		ohci_writel (ohci, OHCI_INTR_SF, &ohci->regs->intrenable);

	/* Then re-enable operations */
	ohci_writel (ohci, OHCI_USB_OPER, &ohci->regs->control);
	(void) ohci_readl (ohci, &ohci->regs->control);
	if (!autostopped)
		msleep (3);

	temp = ohci->hc_control;
	temp &= OHCI_CTRL_RWC;
	temp |= OHCI_CONTROL_INIT | OHCI_USB_OPER;
	ohci->hc_control = temp;
	ohci_writel (ohci, temp, &ohci->regs->control);
	(void) ohci_readl (ohci, &ohci->regs->control);

	/* TRSMRCY */
	if (!autostopped) {
		msleep (10);
		spin_lock_irq (&ohci->lock);
	}
	/* now ohci->lock is always held and irqs are always disabled */

	/* keep it alive for more than ~5x suspend + resume costs */
	ohci->next_statechange = jiffies + STATECHANGE_DELAY;

	/* maybe turn schedules back on */
	enables = 0;
	temp = 0;
	if (!ohci->ed_rm_list) {
		if (ohci->ed_controltail) {
			ohci_writel (ohci,
					find_head (ohci->ed_controltail)->dma,
					&ohci->regs->ed_controlhead);
			enables |= OHCI_CTRL_CLE;
			temp |= OHCI_CLF;
		}
		if (ohci->ed_bulktail) {
			ohci_writel (ohci, find_head (ohci->ed_bulktail)->dma,
				&ohci->regs->ed_bulkhead);
			enables |= OHCI_CTRL_BLE;
			temp |= OHCI_BLF;
		}
	}
	if (hcd->self.bandwidth_isoc_reqs || hcd->self.bandwidth_int_reqs)
		enables |= OHCI_CTRL_PLE|OHCI_CTRL_IE;
	if (enables) {
		ohci_dbg (ohci, "restarting schedules ... %08x\n", enables);
		ohci->hc_control |= enables;
		ohci_writel (ohci, ohci->hc_control, &ohci->regs->control);
		if (temp)
			ohci_writel (ohci, temp, &ohci->regs->cmdstatus);
		(void) ohci_readl (ohci, &ohci->regs->control);
	}

	return 0;
}

#ifdef	CONFIG_PM

static int ohci_bus_suspend (struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);
	int			rc;

	spin_lock_irq (&ohci->lock);

	if (unlikely(!test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags)))
		rc = -ESHUTDOWN;
	else
		rc = ohci_rh_suspend (ohci, 0);
	spin_unlock_irq (&ohci->lock);
	return rc;
}

static int ohci_bus_resume (struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);
	int			rc;

	if (time_before (jiffies, ohci->next_statechange))
		msleep(5);

	spin_lock_irq (&ohci->lock);

	if (unlikely(!test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags)))
		rc = -ESHUTDOWN;
	else
		rc = ohci_rh_resume (ohci);
	spin_unlock_irq (&ohci->lock);

	/* poll until we know a device is connected or we autostop */
	if (rc == 0)
		usb_hcd_poll_rh_status(hcd);
	return rc;
}

#endif	/* CONFIG_PM */

/*-------------------------------------------------------------------------*/

/* build "status change" packet (one or two bytes) from HC registers */

static int
ohci_hub_status_data (struct usb_hcd *hcd, char *buf)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	int		i, changed = 0, length = 1;
	int		any_connected = 0, rhsc_enabled = 1;
	unsigned long	flags;

	spin_lock_irqsave (&ohci->lock, flags);

	/* undocumented erratum seen on at least rev D */
	if ((ohci->flags & OHCI_QUIRK_AMD756)
			&& (roothub_a (ohci) & RH_A_NDP) > MAX_ROOT_PORTS) {
		ohci_warn (ohci, "bogus NDP, rereads as NDP=%d\n",
			  ohci_readl (ohci, &ohci->regs->roothub.a) & RH_A_NDP);
		/* retry later; "should not happen" */
		goto done;
	}

	/* init status */
	if (roothub_status (ohci) & (RH_HS_LPSC | RH_HS_OCIC))
		buf [0] = changed = 1;
	else
		buf [0] = 0;
	if (ohci->num_ports > 7) {
		buf [1] = 0;
		length++;
	}

	/* look at each port */
	for (i = 0; i < ohci->num_ports; i++) {
		u32	status = roothub_portstatus (ohci, i);

		/* can't autostop if ports are connected */
		any_connected |= (status & RH_PS_CCS);

		if (status & (RH_PS_CSC | RH_PS_PESC | RH_PS_PSSC
				| RH_PS_OCIC | RH_PS_PRSC)) {
			changed = 1;
			if (i < 7)
			    buf [0] |= 1 << (i + 1);
			else
			    buf [1] |= 1 << (i - 7);
		}
	}

	/* NOTE:  vendors didn't always make the same implementation
	 * choices for RHSC.  Sometimes it triggers on an edge (like
	 * setting and maybe clearing a port status change bit); and
	 * it's level-triggered on other silicon, active until khubd
	 * clears all active port status change bits.  If it's still
	 * set (level-triggered) we must disable it and rely on
	 * polling until khubd re-enables it.
	 */
	if (ohci_readl (ohci, &ohci->regs->intrstatus) & OHCI_INTR_RHSC) {
		ohci_writel (ohci, OHCI_INTR_RHSC, &ohci->regs->intrdisable);
		(void) ohci_readl (ohci, &ohci->regs->intrdisable);
		rhsc_enabled = 0;
	}
	hcd->poll_rh = 1;

	/* carry out appropriate state changes */
	switch (ohci->hc_control & OHCI_CTRL_HCFS) {

	case OHCI_USB_OPER:
		/* keep on polling until we know a device is connected
		 * and RHSC is enabled */
		if (!ohci->autostop) {
			if (any_connected) {
				if (rhsc_enabled)
					hcd->poll_rh = 0;
			} else {
				ohci->autostop = 1;
				ohci->next_statechange = jiffies + HZ;
			}

		/* if no devices have been attached for one second, autostop */
		} else {
			if (changed || any_connected) {
				ohci->autostop = 0;
				ohci->next_statechange = jiffies +
						STATECHANGE_DELAY;
			} else if (device_may_wakeup(&hcd->self.root_hub->dev)
					&& time_after_eq(jiffies,
						ohci->next_statechange)
					&& !ohci->ed_rm_list
					&& !(ohci->hc_control &
						OHCI_SCHED_ENABLES)) {
				ohci_rh_suspend (ohci, 1);
			}
		}
		break;

	/* if there is a port change, autostart or ask to be resumed */
	case OHCI_USB_SUSPEND:
	case OHCI_USB_RESUME:
		if (changed) {
			if (ohci->autostop)
				ohci_rh_resume (ohci);
			else
				usb_hcd_resume_root_hub (hcd);
		} else {
			/* everything is idle, no need for polling */
			hcd->poll_rh = 0;
		}
		break;
	}

done:
	spin_unlock_irqrestore (&ohci->lock, flags);

	return changed ? length : 0;
}

/*-------------------------------------------------------------------------*/

static void
ohci_hub_descriptor (
	struct ohci_hcd			*ohci,
	struct usb_hub_descriptor	*desc
) {
	u32		rh = roothub_a (ohci);
	u16		temp;

	desc->bDescriptorType = 0x29;
	desc->bPwrOn2PwrGood = (rh & RH_A_POTPGT) >> 24;
	desc->bHubContrCurrent = 0;

	desc->bNbrPorts = ohci->num_ports;
	temp = 1 + (ohci->num_ports / 8);
	desc->bDescLength = 7 + 2 * temp;

	temp = 0;
	if (rh & RH_A_NPS)		/* no power switching? */
	    temp |= 0x0002;
	if (rh & RH_A_PSM) 		/* per-port power switching? */
	    temp |= 0x0001;
	if (rh & RH_A_NOCP)		/* no overcurrent reporting? */
	    temp |= 0x0010;
	else if (rh & RH_A_OCPM)	/* per-port overcurrent reporting? */
	    temp |= 0x0008;
	desc->wHubCharacteristics = (__force __u16)cpu_to_hc16(ohci, temp);

	/* two bitmaps:  ports removable, and usb 1.0 legacy PortPwrCtrlMask */
	rh = roothub_b (ohci);
	memset(desc->bitmap, 0xff, sizeof(desc->bitmap));
	desc->bitmap [0] = rh & RH_B_DR;
	if (ohci->num_ports > 7) {
		desc->bitmap [1] = (rh & RH_B_DR) >> 8;
		desc->bitmap [2] = 0xff;
	} else
		desc->bitmap [1] = 0xff;
}

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_USB_OTG

static int ohci_start_port_reset (struct usb_hcd *hcd, unsigned port)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	u32			status;

	if (!port)
		return -EINVAL;
	port--;

	/* start port reset before HNP protocol times out */
	status = ohci_readl(ohci, &ohci->regs->roothub.portstatus [port]);
	if (!(status & RH_PS_CCS))
		return -ENODEV;

	/* khubd will finish the reset later */
	ohci_writel(ohci, RH_PS_PRS, &ohci->regs->roothub.portstatus [port]);
	return 0;
}

static void start_hnp(struct ohci_hcd *ohci);

#else

#define	ohci_start_port_reset		NULL

#endif

/*-------------------------------------------------------------------------*/


/* See usb 7.1.7.5:  root hubs must issue at least 50 msec reset signaling,
 * not necessarily continuous ... to guard against resume signaling.
 * The short timeout is safe for non-root hubs, and is backward-compatible
 * with earlier Linux hosts.
 */
#ifdef	CONFIG_USB_SUSPEND
#define	PORT_RESET_MSEC		50
#else
#define	PORT_RESET_MSEC		10
#endif

/* this timer value might be vendor-specific ... */
#define	PORT_RESET_HW_MSEC	10

/* wrap-aware logic morphed from <linux/jiffies.h> */
#define tick_before(t1,t2) ((s16)(((s16)(t1))-((s16)(t2))) < 0)

/* called from some task, normally khubd */
static inline void root_port_reset (struct ohci_hcd *ohci, unsigned port)
{
	__hc32 __iomem *portstat = &ohci->regs->roothub.portstatus [port];
	u32	temp;
	u16	now = ohci_readl(ohci, &ohci->regs->fmnumber);
	u16	reset_done = now + PORT_RESET_MSEC;

	/* build a "continuous enough" reset signal, with up to
	 * 3msec gap between pulses.  scheduler HZ==100 must work;
	 * this might need to be deadline-scheduled.
	 */
	do {
		/* spin until any current reset finishes */
		for (;;) {
			temp = ohci_readl (ohci, portstat);
			if (!(temp & RH_PS_PRS))
				break;
			udelay (500);
		} 

		if (!(temp & RH_PS_CCS))
			break;
		if (temp & RH_PS_PRSC)
			ohci_writel (ohci, RH_PS_PRSC, portstat);

		/* start the next reset, sleep till it's probably done */
		ohci_writel (ohci, RH_PS_PRS, portstat);
		msleep(PORT_RESET_HW_MSEC);
		now = ohci_readl(ohci, &ohci->regs->fmnumber);
	} while (tick_before(now, reset_done));
	/* caller synchronizes using PRSC */
}

static int ohci_hub_control (
	struct usb_hcd	*hcd,
	u16		typeReq,
	u16		wValue,
	u16		wIndex,
	char		*buf,
	u16		wLength
) {
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	int		ports = hcd_to_bus (hcd)->root_hub->maxchild;
	u32		temp;
	int		retval = 0;

	if (unlikely(!test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags)))
		return -ESHUTDOWN;

	switch (typeReq) {
	case ClearHubFeature:
		switch (wValue) {
		case C_HUB_OVER_CURRENT:
			ohci_writel (ohci, RH_HS_OCIC,
					&ohci->regs->roothub.status);
		case C_HUB_LOCAL_POWER:
			break;
		default:
			goto error;
		}
		break;
	case ClearPortFeature:
		if (!wIndex || wIndex > ports)
			goto error;
		wIndex--;

		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			temp = RH_PS_CCS;
			break;
		case USB_PORT_FEAT_C_ENABLE:
			temp = RH_PS_PESC;
			break;
		case USB_PORT_FEAT_SUSPEND:
			temp = RH_PS_POCI;
			break;
		case USB_PORT_FEAT_C_SUSPEND:
			temp = RH_PS_PSSC;
			break;
		case USB_PORT_FEAT_POWER:
			temp = RH_PS_LSDA;
			break;
		case USB_PORT_FEAT_C_CONNECTION:
			temp = RH_PS_CSC;
			break;
		case USB_PORT_FEAT_C_OVER_CURRENT:
			temp = RH_PS_OCIC;
			break;
		case USB_PORT_FEAT_C_RESET:
			temp = RH_PS_PRSC;
			break;
		default:
			goto error;
		}
		ohci_writel (ohci, temp,
				&ohci->regs->roothub.portstatus [wIndex]);
		// ohci_readl (ohci, &ohci->regs->roothub.portstatus [wIndex]);
		break;
	case GetHubDescriptor:
		ohci_hub_descriptor (ohci, (struct usb_hub_descriptor *) buf);
		break;
	case GetHubStatus:
		temp = roothub_status (ohci) & ~(RH_HS_CRWE | RH_HS_DRWE);
		put_unaligned(cpu_to_le32 (temp), (__le32 *) buf);
		break;
	case GetPortStatus:
		if (!wIndex || wIndex > ports)
			goto error;
		wIndex--;
		temp = roothub_portstatus (ohci, wIndex);
		put_unaligned(cpu_to_le32 (temp), (__le32 *) buf);

#ifndef	OHCI_VERBOSE_DEBUG
	if (*(u16*)(buf+2))	/* only if wPortChange is interesting */
#endif
		dbg_port (ohci, "GetStatus", wIndex, temp);
		break;
	case SetHubFeature:
		switch (wValue) {
		case C_HUB_OVER_CURRENT:
			// FIXME:  this can be cleared, yes?
		case C_HUB_LOCAL_POWER:
			break;
		default:
			goto error;
		}
		break;
	case SetPortFeature:
		if (!wIndex || wIndex > ports)
			goto error;
		wIndex--;
		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
#ifdef	CONFIG_USB_OTG
			if (hcd->self.otg_port == (wIndex + 1)
					&& hcd->self.b_hnp_enable)
				start_hnp(ohci);
			else
#endif
			ohci_writel (ohci, RH_PS_PSS,
				&ohci->regs->roothub.portstatus [wIndex]);
			break;
		case USB_PORT_FEAT_POWER:
			ohci_writel (ohci, RH_PS_PPS,
				&ohci->regs->roothub.portstatus [wIndex]);
			break;
		case USB_PORT_FEAT_RESET:
			root_port_reset (ohci, wIndex);
			break;
		default:
			goto error;
		}
		break;

	default:
error:
		/* "protocol stall" on error */
		retval = -EPIPE;
	}
	return retval;
}

