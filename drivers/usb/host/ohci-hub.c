// SPDX-License-Identifier: GPL-1.0+
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
		label, num, value, \
		(value & RH_PS_PRSC) ? " PRSC" : "", \
		(value & RH_PS_OCIC) ? " OCIC" : "", \
		(value & RH_PS_PSSC) ? " PSSC" : "", \
		(value & RH_PS_PESC) ? " PESC" : "", \
		(value & RH_PS_CSC) ? " CSC" : "", \
		\
		(value & RH_PS_LSDA) ? " LSDA" : "", \
		(value & RH_PS_PPS) ? " PPS" : "", \
		(value & RH_PS_PRS) ? " PRS" : "", \
		(value & RH_PS_POCI) ? " POCI" : "", \
		(value & RH_PS_PSS) ? " PSS" : "", \
		\
		(value & RH_PS_PES) ? " PES" : "", \
		(value & RH_PS_CCS) ? " CCS" : "" \
		);

/*-------------------------------------------------------------------------*/

#define OHCI_SCHED_ENABLES \
	(OHCI_CTRL_CLE|OHCI_CTRL_BLE|OHCI_CTRL_PLE|OHCI_CTRL_IE)

static void update_done_list(struct ohci_hcd *);
static void ohci_work(struct ohci_hcd *);

#ifdef	CONFIG_PM
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
	update_done_list(ohci);
	ohci_work(ohci);

	/*
	 * Some controllers don't handle "global" suspend properly if
	 * there are unsuspended ports.  For these controllers, put all
	 * the enabled ports into suspend before suspending the root hub.
	 */
	if (ohci->flags & OHCI_QUIRK_GLOBAL_SUSPEND) {
		__hc32 __iomem	*portstat = ohci->regs->roothub.portstatus;
		int		i;
		unsigned	temp;

		for (i = 0; i < ohci->num_ports; (++i, ++portstat)) {
			temp = ohci_readl(ohci, portstat);
			if ((temp & (RH_PS_PES | RH_PS_PSS)) ==
					RH_PS_PES)
				ohci_writel(ohci, RH_PS_PSS, portstat);
		}
	}

	/* maybe resume can wake root hub */
	if (ohci_to_hcd(ohci)->self.root_hub->do_remote_wakeup || autostop) {
		ohci->hc_control |= OHCI_CTRL_RWE;
	} else {
		ohci_writel(ohci, OHCI_INTR_RHSC | OHCI_INTR_RD,
				&ohci->regs->intrdisable);
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
		ohci->rh_state = OHCI_RH_SUSPENDED;
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
		if (ohci->rh_state != OHCI_RH_RUNNING) {
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
		ohci_dbg(ohci, "%swakeup root hub\n",
				autostopped ? "auto-" : "");
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
	if (status == -EBUSY) {
		if (!autostopped) {
			spin_unlock_irq (&ohci->lock);
			status = ohci_restart (ohci);

			usb_root_hub_lost_power(hcd->self.root_hub);

			spin_lock_irq (&ohci->lock);
		}
		return status;
	}
	if (status != -EINPROGRESS)
		return status;
	if (autostopped)
		goto skip_resume;
	spin_unlock_irq (&ohci->lock);

	/* Some controllers (lucent erratum) need extra-long delays */
	msleep (20 /* usb 11.5.1.10 */ + 12 /* 32 msec counter */ + 1);

	temp = ohci_readl (ohci, &ohci->regs->control);
	temp &= OHCI_CTRL_HCFS;
	if (temp != OHCI_USB_RESUME) {
		ohci_err (ohci, "controller won't resume\n");
		spin_lock_irq(&ohci->lock);
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

	/*
	 * The following code is executed with ohci->lock held and
	 * irqs disabled if and only if autostopped is true.  This
	 * will cause sparse to warn about a "context imbalance".
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

	ohci->rh_state = OHCI_RH_RUNNING;
	return 0;
}

static int ohci_bus_suspend (struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);
	int			rc;

	spin_lock_irq (&ohci->lock);

	if (unlikely(!HCD_HW_ACCESSIBLE(hcd)))
		rc = -ESHUTDOWN;
	else
		rc = ohci_rh_suspend (ohci, 0);
	spin_unlock_irq (&ohci->lock);

	if (rc == 0) {
		del_timer_sync(&ohci->io_watchdog);
		ohci->prev_frame_no = IO_WATCHDOG_OFF;
	}
	return rc;
}

static int ohci_bus_resume (struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);
	int			rc;

	if (time_before (jiffies, ohci->next_statechange))
		msleep(5);

	spin_lock_irq (&ohci->lock);

	if (unlikely(!HCD_HW_ACCESSIBLE(hcd)))
		rc = -ESHUTDOWN;
	else
		rc = ohci_rh_resume (ohci);
	spin_unlock_irq (&ohci->lock);

	/* poll until we know a device is connected or we autostop */
	if (rc == 0)
		usb_hcd_poll_rh_status(hcd);
	return rc;
}

/* Carry out polling-, autostop-, and autoresume-related state changes */
static int ohci_root_hub_state_changes(struct ohci_hcd *ohci, int changed,
		int any_connected, int rhsc_status)
{
	int	poll_rh = 1;
	int	rhsc_enable;

	/* Some broken controllers never turn off RHSC in the interrupt
	 * status register.  For their sake we won't re-enable RHSC
	 * interrupts if the interrupt bit is already active.
	 */
	rhsc_enable = ohci_readl(ohci, &ohci->regs->intrenable) &
			OHCI_INTR_RHSC;

	switch (ohci->hc_control & OHCI_CTRL_HCFS) {
	case OHCI_USB_OPER:
		/* If no status changes are pending, enable RHSC interrupts. */
		if (!rhsc_enable && !rhsc_status && !changed) {
			rhsc_enable = OHCI_INTR_RHSC;
			ohci_writel(ohci, rhsc_enable, &ohci->regs->intrenable);
		}

		/* Keep on polling until we know a device is connected
		 * and RHSC is enabled, or until we autostop.
		 */
		if (!ohci->autostop) {
			if (any_connected ||
					!device_may_wakeup(&ohci_to_hcd(ohci)
						->self.root_hub->dev)) {
				if (rhsc_enable)
					poll_rh = 0;
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
			} else if (time_after_eq(jiffies,
						ohci->next_statechange)
					&& !ohci->ed_rm_list
					&& !(ohci->hc_control &
						OHCI_SCHED_ENABLES)) {
				ohci_rh_suspend(ohci, 1);
				if (rhsc_enable)
					poll_rh = 0;
			}
		}
		break;

	case OHCI_USB_SUSPEND:
	case OHCI_USB_RESUME:
		/* if there is a port change, autostart or ask to be resumed */
		if (changed) {
			if (ohci->autostop)
				ohci_rh_resume(ohci);
			else
				usb_hcd_resume_root_hub(ohci_to_hcd(ohci));

		/* If remote wakeup is disabled, stop polling */
		} else if (!ohci->autostop &&
				!ohci_to_hcd(ohci)->self.root_hub->
					do_remote_wakeup) {
			poll_rh = 0;

		} else {
			/* If no status changes are pending,
			 * enable RHSC interrupts
			 */
			if (!rhsc_enable && !rhsc_status) {
				rhsc_enable = OHCI_INTR_RHSC;
				ohci_writel(ohci, rhsc_enable,
						&ohci->regs->intrenable);
			}
			/* Keep polling until RHSC is enabled */
			if (rhsc_enable)
				poll_rh = 0;
		}
		break;
	}
	return poll_rh;
}

#else	/* CONFIG_PM */

static inline int ohci_rh_resume(struct ohci_hcd *ohci)
{
	return 0;
}

/* Carry out polling-related state changes.
 * autostop isn't used when CONFIG_PM is turned off.
 */
static int ohci_root_hub_state_changes(struct ohci_hcd *ohci, int changed,
		int any_connected, int rhsc_status)
{
	/* If RHSC is enabled, don't poll */
	if (ohci_readl(ohci, &ohci->regs->intrenable) & OHCI_INTR_RHSC)
		return 0;

	/* If status changes are pending, continue polling.
	 * Conversely, if no status changes are pending but the RHSC
	 * status bit was set, then RHSC may be broken so continue polling.
	 */
	if (changed || rhsc_status)
		return 1;

	/* It's safe to re-enable RHSC interrupts */
	ohci_writel(ohci, OHCI_INTR_RHSC, &ohci->regs->intrenable);
	return 0;
}

#endif	/* CONFIG_PM */

/*-------------------------------------------------------------------------*/

/* build "status change" packet (one or two bytes) from HC registers */

int ohci_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	int		i, changed = 0, length = 1;
	int		any_connected = 0;
	int		rhsc_status;
	unsigned long	flags;

	spin_lock_irqsave (&ohci->lock, flags);
	if (!HCD_HW_ACCESSIBLE(hcd))
		goto done;

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

	/* Clear the RHSC status flag before reading the port statuses */
	ohci_writel(ohci, OHCI_INTR_RHSC, &ohci->regs->intrstatus);
	rhsc_status = ohci_readl(ohci, &ohci->regs->intrstatus) &
			OHCI_INTR_RHSC;

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

	if (ohci_root_hub_state_changes(ohci, changed,
			any_connected, rhsc_status))
		set_bit(HCD_FLAG_POLL_RH, &hcd->flags);
	else
		clear_bit(HCD_FLAG_POLL_RH, &hcd->flags);


done:
	spin_unlock_irqrestore (&ohci->lock, flags);

	return changed ? length : 0;
}
EXPORT_SYMBOL_GPL(ohci_hub_status_data);

/*-------------------------------------------------------------------------*/

static void
ohci_hub_descriptor (
	struct ohci_hcd			*ohci,
	struct usb_hub_descriptor	*desc
) {
	u32		rh = roothub_a (ohci);
	u16		temp;

	desc->bDescriptorType = USB_DT_HUB;
	desc->bPwrOn2PwrGood = (rh & RH_A_POTPGT) >> 24;
	desc->bHubContrCurrent = 0;

	desc->bNbrPorts = ohci->num_ports;
	temp = 1 + (ohci->num_ports / 8);
	desc->bDescLength = 7 + 2 * temp;

	temp = HUB_CHAR_COMMON_LPSM | HUB_CHAR_COMMON_OCPM;
	if (rh & RH_A_NPS)		/* no power switching? */
		temp |= HUB_CHAR_NO_LPSM;
	if (rh & RH_A_PSM)		/* per-port power switching? */
		temp |= HUB_CHAR_INDV_PORT_LPSM;
	if (rh & RH_A_NOCP)		/* no overcurrent reporting? */
		temp |= HUB_CHAR_NO_OCPM;
	else if (rh & RH_A_OCPM)	/* per-port overcurrent reporting? */
		temp |= HUB_CHAR_INDV_PORT_OCPM;
	desc->wHubCharacteristics = cpu_to_le16(temp);

	/* ports removable, and usb 1.0 legacy PortPwrCtrlMask */
	rh = roothub_b (ohci);
	memset(desc->u.hs.DeviceRemovable, 0xff,
			sizeof(desc->u.hs.DeviceRemovable));
	desc->u.hs.DeviceRemovable[0] = rh & RH_B_DR;
	if (ohci->num_ports > 7) {
		desc->u.hs.DeviceRemovable[1] = (rh & RH_B_DR) >> 8;
		desc->u.hs.DeviceRemovable[2] = 0xff;
	} else
		desc->u.hs.DeviceRemovable[1] = 0xff;
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

	/* hub_wq will finish the reset later */
	ohci_writel(ohci, RH_PS_PRS, &ohci->regs->roothub.portstatus [port]);
	return 0;
}

#else

#define	ohci_start_port_reset		NULL

#endif

/*-------------------------------------------------------------------------*/


/* See usb 7.1.7.5:  root hubs must issue at least 50 msec reset signaling,
 * not necessarily continuous ... to guard against resume signaling.
 */
#define	PORT_RESET_MSEC		50

/* this timer value might be vendor-specific ... */
#define	PORT_RESET_HW_MSEC	10

/* wrap-aware logic morphed from <linux/jiffies.h> */
#define tick_before(t1,t2) ((s16)(((s16)(t1))-((s16)(t2))) < 0)

/* called from some task, normally hub_wq */
static inline int root_port_reset (struct ohci_hcd *ohci, unsigned port)
{
	__hc32 __iomem *portstat = &ohci->regs->roothub.portstatus [port];
	u32	temp = 0;
	u16	now = ohci_readl(ohci, &ohci->regs->fmnumber);
	u16	reset_done = now + PORT_RESET_MSEC;
	int	limit_1 = DIV_ROUND_UP(PORT_RESET_MSEC, PORT_RESET_HW_MSEC);

	/* build a "continuous enough" reset signal, with up to
	 * 3msec gap between pulses.  scheduler HZ==100 must work;
	 * this might need to be deadline-scheduled.
	 */
	do {
		int limit_2;

		/* spin until any current reset finishes */
		limit_2 = PORT_RESET_HW_MSEC * 2;
		while (--limit_2 >= 0) {
			temp = ohci_readl (ohci, portstat);
			/* handle e.g. CardBus eject */
			if (temp == ~(u32)0)
				return -ESHUTDOWN;
			if (!(temp & RH_PS_PRS))
				break;
			udelay (500);
		}

		/* timeout (a hardware error) has been observed when
		 * EHCI sets CF while this driver is resetting a port;
		 * presumably other disconnect paths might do it too.
		 */
		if (limit_2 < 0) {
			ohci_dbg(ohci,
				"port[%d] reset timeout, stat %08x\n",
				port, temp);
			break;
		}

		if (!(temp & RH_PS_CCS))
			break;
		if (temp & RH_PS_PRSC)
			ohci_writel (ohci, RH_PS_PRSC, portstat);

		/* start the next reset, sleep till it's probably done */
		ohci_writel (ohci, RH_PS_PRS, portstat);
		msleep(PORT_RESET_HW_MSEC);
		now = ohci_readl(ohci, &ohci->regs->fmnumber);
	} while (tick_before(now, reset_done) && --limit_1 >= 0);

	/* caller synchronizes using PRSC ... and handles PRS
	 * still being set when this returns.
	 */

	return 0;
}

int ohci_hub_control(
	struct usb_hcd	*hcd,
	u16		typeReq,
	u16		wValue,
	u16		wIndex,
	char		*buf,
	u16		wLength
) {
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	int		ports = ohci->num_ports;
	u32		temp;
	int		retval = 0;

	if (unlikely(!HCD_HW_ACCESSIBLE(hcd)))
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
		put_unaligned_le32(temp, buf);
		break;
	case GetPortStatus:
		if (!wIndex || wIndex > ports)
			goto error;
		wIndex--;
		temp = roothub_portstatus (ohci, wIndex);
		put_unaligned_le32(temp, buf);

		if (*(u16*)(buf+2))	/* only if wPortChange is interesting */
			dbg_port(ohci, "GetStatus", wIndex, temp);
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
				ohci->start_hnp(ohci);
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
			retval = root_port_reset (ohci, wIndex);
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
EXPORT_SYMBOL_GPL(ohci_hub_control);
