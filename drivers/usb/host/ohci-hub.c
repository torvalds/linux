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

#ifdef	CONFIG_PM

#define OHCI_SCHED_ENABLES \
	(OHCI_CTRL_CLE|OHCI_CTRL_BLE|OHCI_CTRL_PLE|OHCI_CTRL_IE)

static void dl_done_list (struct ohci_hcd *, struct pt_regs *);
static void finish_unlinks (struct ohci_hcd *, u16 , struct pt_regs *);
static int ohci_restart (struct ohci_hcd *ohci);

static int ohci_bus_suspend (struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);
	int			status = 0;
	unsigned long		flags;

	spin_lock_irqsave (&ohci->lock, flags);

	if (unlikely(!test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags))) {
		spin_unlock_irqrestore (&ohci->lock, flags);
		return -ESHUTDOWN;
	}

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
		ohci_dbg (ohci, "already suspended\n");
		goto done;
	}
	ohci_dbg (ohci, "suspend root hub\n");

	/* First stop any processing */
	if (ohci->hc_control & OHCI_SCHED_ENABLES) {
		int		limit;

		ohci->hc_control &= ~OHCI_SCHED_ENABLES;
		ohci_writel (ohci, ohci->hc_control, &ohci->regs->control);
		ohci->hc_control = ohci_readl (ohci, &ohci->regs->control);
		ohci_writel (ohci, OHCI_INTR_SF, &ohci->regs->intrstatus);

		/* sched disables take effect on the next frame,
		 * then the last WDH could take 6+ msec
		 */
		ohci_dbg (ohci, "stopping schedules ...\n");
		limit = 2000;
		while (limit > 0) {
			udelay (250);
			limit =- 250;
			if (ohci_readl (ohci, &ohci->regs->intrstatus)
					& OHCI_INTR_SF)
				break;
		}
		dl_done_list (ohci, NULL);
		mdelay (7);
	}
	dl_done_list (ohci, NULL);
	finish_unlinks (ohci, ohci_frame_no(ohci), NULL);
	ohci_writel (ohci, ohci_readl (ohci, &ohci->regs->intrstatus),
			&ohci->regs->intrstatus);

	/* maybe resume can wake root hub */
	if (hcd->remote_wakeup)
		ohci->hc_control |= OHCI_CTRL_RWE;
	else
		ohci->hc_control &= ~OHCI_CTRL_RWE;

	/* Suspend hub ... this is the "global (to this bus) suspend" mode,
	 * which doesn't imply ports will first be individually suspended.
	 */
	ohci->hc_control &= ~OHCI_CTRL_HCFS;
	ohci->hc_control |= OHCI_USB_SUSPEND;
	ohci_writel (ohci, ohci->hc_control, &ohci->regs->control);
	(void) ohci_readl (ohci, &ohci->regs->control);

	/* no resumes until devices finish suspending */
	ohci->next_statechange = jiffies + msecs_to_jiffies (5);

done:
	/* external suspend vs self autosuspend ... same effect */
	if (status == 0)
		usb_hcd_suspend_root_hub(hcd);
	spin_unlock_irqrestore (&ohci->lock, flags);
	return status;
}

static inline struct ed *find_head (struct ed *ed)
{
	/* for bulk and control lists */
	while (ed->ed_prev)
		ed = ed->ed_prev;
	return ed;
}

/* caller has locked the root hub */
static int ohci_bus_resume (struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);
	u32			temp, enables;
	int			status = -EINPROGRESS;
	unsigned long		flags;

	if (time_before (jiffies, ohci->next_statechange))
		msleep(5);

	spin_lock_irqsave (&ohci->lock, flags);

	if (unlikely(!test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags))) {
		spin_unlock_irqrestore (&ohci->lock, flags);
		return -ESHUTDOWN;
	}


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
		ohci_dbg (ohci, "resume root hub\n");
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
	spin_unlock_irqrestore (&ohci->lock, flags);
	if (status == -EBUSY) {
		(void) ohci_init (ohci);
		return ohci_restart (ohci);
	}
	if (status != -EINPROGRESS)
		return status;

	temp = ohci->num_ports;
	enables = 0;
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

	/* interrupts might have been disabled */
	ohci_writel (ohci, OHCI_INTR_INIT, &ohci->regs->intrenable);
	if (ohci->ed_rm_list)
		ohci_writel (ohci, OHCI_INTR_SF, &ohci->regs->intrenable);
	ohci_writel (ohci, ohci_readl (ohci, &ohci->regs->intrstatus),
			&ohci->regs->intrstatus);

	/* Then re-enable operations */
	ohci_writel (ohci, OHCI_USB_OPER, &ohci->regs->control);
	(void) ohci_readl (ohci, &ohci->regs->control);
	msleep (3);

	temp = OHCI_CONTROL_INIT | OHCI_USB_OPER;
	if (hcd->can_wakeup)
		temp |= OHCI_CTRL_RWC;
	ohci->hc_control = temp;
	ohci_writel (ohci, temp, &ohci->regs->control);
	(void) ohci_readl (ohci, &ohci->regs->control);

	/* TRSMRCY */
	msleep (10);

	/* keep it alive for ~5x suspend + resume costs */
	ohci->next_statechange = jiffies + msecs_to_jiffies (250);

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

#endif	/* CONFIG_PM */

/*-------------------------------------------------------------------------*/

/* build "status change" packet (one or two bytes) from HC registers */

static int
ohci_hub_status_data (struct usb_hcd *hcd, char *buf)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	int		i, changed = 0, length = 1;
	int		can_suspend = hcd->can_wakeup;
	unsigned long	flags;

	spin_lock_irqsave (&ohci->lock, flags);

	/* handle autosuspended root:  finish resuming before
	 * letting khubd or root hub timer see state changes.
	 */
	if (unlikely((ohci->hc_control & OHCI_CTRL_HCFS) != OHCI_USB_OPER
		     || !HC_IS_RUNNING(hcd->state))) {
		can_suspend = 0;
		goto done;
	}

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

		if (status & (RH_PS_CSC | RH_PS_PESC | RH_PS_PSSC
				| RH_PS_OCIC | RH_PS_PRSC)) {
			changed = 1;
			if (i < 7)
			    buf [0] |= 1 << (i + 1);
			else
			    buf [1] |= 1 << (i - 7);
			continue;
		}

		/* can suspend if no ports are enabled; or if all all
		 * enabled ports are suspended AND remote wakeup is on.
		 */
		if (!(status & RH_PS_CCS))
			continue;
		if ((status & RH_PS_PSS) && hcd->remote_wakeup)
			continue;
		can_suspend = 0;
	}
done:
	spin_unlock_irqrestore (&ohci->lock, flags);

#ifdef CONFIG_PM
	/* save power by suspending idle root hubs;
	 * INTR_RD wakes us when there's work
	 */
	if (can_suspend
			&& !changed
			&& !ohci->ed_rm_list
			&& ((OHCI_CTRL_HCFS | OHCI_SCHED_ENABLES)
					& ohci->hc_control)
				== OHCI_USB_OPER
			&& time_after (jiffies, ohci->next_statechange)
			&& usb_trylock_device (hcd->self.root_hub) == 0
			) {
		ohci_vdbg (ohci, "autosuspend\n");
		(void) ohci_bus_suspend (hcd);
		usb_unlock_device (hcd->self.root_hub);
	}
#endif

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
			if ((ohci->hc_control & OHCI_CTRL_HCFS)
					!= OHCI_USB_OPER)
				usb_hcd_resume_root_hub(hcd);
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
		*(__le32 *) buf = cpu_to_le32 (temp);
		break;
	case GetPortStatus:
		if (!wIndex || wIndex > ports)
			goto error;
		wIndex--;
		temp = roothub_portstatus (ohci, wIndex);
		*(__le32 *) buf = cpu_to_le32 (temp);

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

