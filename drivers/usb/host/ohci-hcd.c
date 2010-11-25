/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2004 David Brownell <dbrownell@users.sourceforge.net>
 *
 * [ Initialisation is based on Linus'  ]
 * [ uhci code and gregs ohci fragments ]
 * [ (C) Copyright 1999 Linus Torvalds  ]
 * [ (C) Copyright 1999 Gregory P. Smith]
 *
 *
 * OHCI is the main "non-Intel/VIA" standard for USB 1.1 host controller
 * interfaces (though some non-x86 Intel chips use it).  It supports
 * smarter hardware than UHCI.  A download link for the spec available
 * through the http://www.usb.org website.
 *
 * This file is licenced under the GPL.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/usb.h>
#include <linux/usb/otg.h>
#include <linux/usb/hcd.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>
#include <asm/byteorder.h>


#define DRIVER_AUTHOR "Roman Weissgaerber, David Brownell"
#define DRIVER_DESC "USB 1.1 'Open' Host Controller (OHCI) Driver"

/*-------------------------------------------------------------------------*/

#undef OHCI_VERBOSE_DEBUG	/* not always helpful */

/* For initializing controller (mask in an HCFS mode too) */
#define	OHCI_CONTROL_INIT	OHCI_CTRL_CBSR
#define	OHCI_INTR_INIT \
		(OHCI_INTR_MIE | OHCI_INTR_RHSC | OHCI_INTR_UE \
		| OHCI_INTR_RD | OHCI_INTR_WDH)

#ifdef __hppa__
/* On PA-RISC, PDC can leave IR set incorrectly; ignore it there. */
#define	IR_DISABLE
#endif

#ifdef CONFIG_ARCH_OMAP
/* OMAP doesn't support IR (no SMM; not needed) */
#define	IR_DISABLE
#endif

/*-------------------------------------------------------------------------*/

static const char	hcd_name [] = "ohci_hcd";

#define	STATECHANGE_DELAY	msecs_to_jiffies(300)

#include "ohci.h"

static void ohci_dump (struct ohci_hcd *ohci, int verbose);
static int ohci_init (struct ohci_hcd *ohci);
static void ohci_stop (struct usb_hcd *hcd);

#if defined(CONFIG_PM) || defined(CONFIG_PCI)
static int ohci_restart (struct ohci_hcd *ohci);
#endif

#ifdef CONFIG_PCI
static void quirk_amd_pll(int state);
static void amd_iso_dev_put(void);
static void sb800_prefetch(struct ohci_hcd *ohci, int on);
#else
static inline void quirk_amd_pll(int state)
{
	return;
}
static inline void amd_iso_dev_put(void)
{
	return;
}
static inline void sb800_prefetch(struct ohci_hcd *ohci, int on)
{
	return;
}
#endif


#include "ohci-hub.c"
#include "ohci-dbg.c"
#include "ohci-mem.c"
#include "ohci-q.c"


/*
 * On architectures with edge-triggered interrupts we must never return
 * IRQ_NONE.
 */
#if defined(CONFIG_SA1111)  /* ... or other edge-triggered systems */
#define IRQ_NOTMINE	IRQ_HANDLED
#else
#define IRQ_NOTMINE	IRQ_NONE
#endif


/* Some boards misreport power switching/overcurrent */
static int distrust_firmware = 1;
module_param (distrust_firmware, bool, 0);
MODULE_PARM_DESC (distrust_firmware,
	"true to distrust firmware power/overcurrent setup");

/* Some boards leave IR set wrongly, since they fail BIOS/SMM handshakes */
static int no_handshake = 0;
module_param (no_handshake, bool, 0);
MODULE_PARM_DESC (no_handshake, "true (not default) disables BIOS handshake");

/*-------------------------------------------------------------------------*/

/*
 * queue up an urb for anything except the root hub
 */
static int ohci_urb_enqueue (
	struct usb_hcd	*hcd,
	struct urb	*urb,
	gfp_t		mem_flags
) {
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	struct ed	*ed;
	urb_priv_t	*urb_priv;
	unsigned int	pipe = urb->pipe;
	int		i, size = 0;
	unsigned long	flags;
	int		retval = 0;

#ifdef OHCI_VERBOSE_DEBUG
	urb_print(urb, "SUB", usb_pipein(pipe), -EINPROGRESS);
#endif

	/* every endpoint has a ed, locate and maybe (re)initialize it */
	if (! (ed = ed_get (ohci, urb->ep, urb->dev, pipe, urb->interval)))
		return -ENOMEM;

	/* for the private part of the URB we need the number of TDs (size) */
	switch (ed->type) {
		case PIPE_CONTROL:
			/* td_submit_urb() doesn't yet handle these */
			if (urb->transfer_buffer_length > 4096)
				return -EMSGSIZE;

			/* 1 TD for setup, 1 for ACK, plus ... */
			size = 2;
			/* FALLTHROUGH */
		// case PIPE_INTERRUPT:
		// case PIPE_BULK:
		default:
			/* one TD for every 4096 Bytes (can be upto 8K) */
			size += urb->transfer_buffer_length / 4096;
			/* ... and for any remaining bytes ... */
			if ((urb->transfer_buffer_length % 4096) != 0)
				size++;
			/* ... and maybe a zero length packet to wrap it up */
			if (size == 0)
				size++;
			else if ((urb->transfer_flags & URB_ZERO_PACKET) != 0
				&& (urb->transfer_buffer_length
					% usb_maxpacket (urb->dev, pipe,
						usb_pipeout (pipe))) == 0)
				size++;
			break;
		case PIPE_ISOCHRONOUS: /* number of packets from URB */
			size = urb->number_of_packets;
			break;
	}

	/* allocate the private part of the URB */
	urb_priv = kzalloc (sizeof (urb_priv_t) + size * sizeof (struct td *),
			mem_flags);
	if (!urb_priv)
		return -ENOMEM;
	INIT_LIST_HEAD (&urb_priv->pending);
	urb_priv->length = size;
	urb_priv->ed = ed;

	/* allocate the TDs (deferring hash chain updates) */
	for (i = 0; i < size; i++) {
		urb_priv->td [i] = td_alloc (ohci, mem_flags);
		if (!urb_priv->td [i]) {
			urb_priv->length = i;
			urb_free_priv (ohci, urb_priv);
			return -ENOMEM;
		}
	}

	spin_lock_irqsave (&ohci->lock, flags);

	/* don't submit to a dead HC */
	if (!HCD_HW_ACCESSIBLE(hcd)) {
		retval = -ENODEV;
		goto fail;
	}
	if (!HC_IS_RUNNING(hcd->state)) {
		retval = -ENODEV;
		goto fail;
	}
	retval = usb_hcd_link_urb_to_ep(hcd, urb);
	if (retval)
		goto fail;

	/* schedule the ed if needed */
	if (ed->state == ED_IDLE) {
		retval = ed_schedule (ohci, ed);
		if (retval < 0) {
			usb_hcd_unlink_urb_from_ep(hcd, urb);
			goto fail;
		}
		if (ed->type == PIPE_ISOCHRONOUS) {
			u16	frame = ohci_frame_no(ohci);

			/* delay a few frames before the first TD */
			frame += max_t (u16, 8, ed->interval);
			frame &= ~(ed->interval - 1);
			frame |= ed->branch;
			urb->start_frame = frame;

			/* yes, only URB_ISO_ASAP is supported, and
			 * urb->start_frame is never used as input.
			 */
		}
	} else if (ed->type == PIPE_ISOCHRONOUS)
		urb->start_frame = ed->last_iso + ed->interval;

	/* fill the TDs and link them to the ed; and
	 * enable that part of the schedule, if needed
	 * and update count of queued periodic urbs
	 */
	urb->hcpriv = urb_priv;
	td_submit_urb (ohci, urb);

fail:
	if (retval)
		urb_free_priv (ohci, urb_priv);
	spin_unlock_irqrestore (&ohci->lock, flags);
	return retval;
}

/*
 * decouple the URB from the HC queues (TDs, urb_priv).
 * reporting is always done
 * asynchronously, and we might be dealing with an urb that's
 * partially transferred, or an ED with other urbs being unlinked.
 */
static int ohci_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);
	unsigned long		flags;
	int			rc;

#ifdef OHCI_VERBOSE_DEBUG
	urb_print(urb, "UNLINK", 1, status);
#endif

	spin_lock_irqsave (&ohci->lock, flags);
	rc = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (rc) {
		;	/* Do nothing */
	} else if (HC_IS_RUNNING(hcd->state)) {
		urb_priv_t  *urb_priv;

		/* Unless an IRQ completed the unlink while it was being
		 * handed to us, flag it for unlink and giveback, and force
		 * some upcoming INTR_SF to call finish_unlinks()
		 */
		urb_priv = urb->hcpriv;
		if (urb_priv) {
			if (urb_priv->ed->state == ED_OPER)
				start_ed_unlink (ohci, urb_priv->ed);
		}
	} else {
		/*
		 * with HC dead, we won't respect hc queue pointers
		 * any more ... just clean up every urb's memory.
		 */
		if (urb->hcpriv)
			finish_urb(ohci, urb, status);
	}
	spin_unlock_irqrestore (&ohci->lock, flags);
	return rc;
}

/*-------------------------------------------------------------------------*/

/* frees config/altsetting state for endpoints,
 * including ED memory, dummy TD, and bulk/intr data toggle
 */

static void
ohci_endpoint_disable (struct usb_hcd *hcd, struct usb_host_endpoint *ep)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);
	unsigned long		flags;
	struct ed		*ed = ep->hcpriv;
	unsigned		limit = 1000;

	/* ASSERT:  any requests/urbs are being unlinked */
	/* ASSERT:  nobody can be submitting urbs for this any more */

	if (!ed)
		return;

rescan:
	spin_lock_irqsave (&ohci->lock, flags);

	if (!HC_IS_RUNNING (hcd->state)) {
sanitize:
		ed->state = ED_IDLE;
		if (quirk_zfmicro(ohci) && ed->type == PIPE_INTERRUPT)
			ohci->eds_scheduled--;
		finish_unlinks (ohci, 0);
	}

	switch (ed->state) {
	case ED_UNLINK:		/* wait for hw to finish? */
		/* major IRQ delivery trouble loses INTR_SF too... */
		if (limit-- == 0) {
			ohci_warn(ohci, "ED unlink timeout\n");
			if (quirk_zfmicro(ohci)) {
				ohci_warn(ohci, "Attempting ZF TD recovery\n");
				ohci->ed_to_check = ed;
				ohci->zf_delay = 2;
			}
			goto sanitize;
		}
		spin_unlock_irqrestore (&ohci->lock, flags);
		schedule_timeout_uninterruptible(1);
		goto rescan;
	case ED_IDLE:		/* fully unlinked */
		if (list_empty (&ed->td_list)) {
			td_free (ohci, ed->dummy);
			ed_free (ohci, ed);
			break;
		}
		/* else FALL THROUGH */
	default:
		/* caller was supposed to have unlinked any requests;
		 * that's not our job.  can't recover; must leak ed.
		 */
		ohci_err (ohci, "leak ed %p (#%02x) state %d%s\n",
			ed, ep->desc.bEndpointAddress, ed->state,
			list_empty (&ed->td_list) ? "" : " (has tds)");
		td_free (ohci, ed->dummy);
		break;
	}
	ep->hcpriv = NULL;
	spin_unlock_irqrestore (&ohci->lock, flags);
}

static int ohci_get_frame (struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);

	return ohci_frame_no(ohci);
}

static void ohci_usb_reset (struct ohci_hcd *ohci)
{
	ohci->hc_control = ohci_readl (ohci, &ohci->regs->control);
	ohci->hc_control &= OHCI_CTRL_RWC;
	ohci_writel (ohci, ohci->hc_control, &ohci->regs->control);
}

/* ohci_shutdown forcibly disables IRQs and DMA, helping kexec and
 * other cases where the next software may expect clean state from the
 * "firmware".  this is bus-neutral, unlike shutdown() methods.
 */
static void
ohci_shutdown (struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci;

	ohci = hcd_to_ohci (hcd);
	ohci_writel (ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
	ohci->hc_control = ohci_readl(ohci, &ohci->regs->control);

	/* If the SHUTDOWN quirk is set, don't put the controller in RESET */
	ohci->hc_control &= (ohci->flags & OHCI_QUIRK_SHUTDOWN ?
			OHCI_CTRL_RWC | OHCI_CTRL_HCFS :
			OHCI_CTRL_RWC);
	ohci_writel(ohci, ohci->hc_control, &ohci->regs->control);

	/* flush the writes */
	(void) ohci_readl (ohci, &ohci->regs->control);
}

static int check_ed(struct ohci_hcd *ohci, struct ed *ed)
{
	return (hc32_to_cpu(ohci, ed->hwINFO) & ED_IN) != 0
		&& (hc32_to_cpu(ohci, ed->hwHeadP) & TD_MASK)
			== (hc32_to_cpu(ohci, ed->hwTailP) & TD_MASK)
		&& !list_empty(&ed->td_list);
}

/* ZF Micro watchdog timer callback. The ZF Micro chipset sometimes completes
 * an interrupt TD but neglects to add it to the donelist.  On systems with
 * this chipset, we need to periodically check the state of the queues to look
 * for such "lost" TDs.
 */
static void unlink_watchdog_func(unsigned long _ohci)
{
	unsigned long	flags;
	unsigned	max;
	unsigned	seen_count = 0;
	unsigned	i;
	struct ed	**seen = NULL;
	struct ohci_hcd	*ohci = (struct ohci_hcd *) _ohci;

	spin_lock_irqsave(&ohci->lock, flags);
	max = ohci->eds_scheduled;
	if (!max)
		goto done;

	if (ohci->ed_to_check)
		goto out;

	seen = kcalloc(max, sizeof *seen, GFP_ATOMIC);
	if (!seen)
		goto out;

	for (i = 0; i < NUM_INTS; i++) {
		struct ed	*ed = ohci->periodic[i];

		while (ed) {
			unsigned	temp;

			/* scan this branch of the periodic schedule tree */
			for (temp = 0; temp < seen_count; temp++) {
				if (seen[temp] == ed) {
					/* we've checked it and what's after */
					ed = NULL;
					break;
				}
			}
			if (!ed)
				break;
			seen[seen_count++] = ed;
			if (!check_ed(ohci, ed)) {
				ed = ed->ed_next;
				continue;
			}

			/* HC's TD list is empty, but HCD sees at least one
			 * TD that's not been sent through the donelist.
			 */
			ohci->ed_to_check = ed;
			ohci->zf_delay = 2;

			/* The HC may wait until the next frame to report the
			 * TD as done through the donelist and INTR_WDH.  (We
			 * just *assume* it's not a multi-TD interrupt URB;
			 * those could defer the IRQ more than one frame, using
			 * DI...)  Check again after the next INTR_SF.
			 */
			ohci_writel(ohci, OHCI_INTR_SF,
					&ohci->regs->intrstatus);
			ohci_writel(ohci, OHCI_INTR_SF,
					&ohci->regs->intrenable);

			/* flush those writes */
			(void) ohci_readl(ohci, &ohci->regs->control);

			goto out;
		}
	}
out:
	kfree(seen);
	if (ohci->eds_scheduled)
		mod_timer(&ohci->unlink_watchdog, round_jiffies(jiffies + HZ));
done:
	spin_unlock_irqrestore(&ohci->lock, flags);
}

/*-------------------------------------------------------------------------*
 * HC functions
 *-------------------------------------------------------------------------*/

/* init memory, and kick BIOS/SMM off */

static int ohci_init (struct ohci_hcd *ohci)
{
	int ret;
	struct usb_hcd *hcd = ohci_to_hcd(ohci);

	if (distrust_firmware)
		ohci->flags |= OHCI_QUIRK_HUB_POWER;

	disable (ohci);
	ohci->regs = hcd->regs;

	/* REVISIT this BIOS handshake is now moved into PCI "quirks", and
	 * was never needed for most non-PCI systems ... remove the code?
	 */

#ifndef IR_DISABLE
	/* SMM owns the HC?  not for long! */
	if (!no_handshake && ohci_readl (ohci,
					&ohci->regs->control) & OHCI_CTRL_IR) {
		u32 temp;

		ohci_dbg (ohci, "USB HC TakeOver from BIOS/SMM\n");

		/* this timeout is arbitrary.  we make it long, so systems
		 * depending on usb keyboards may be usable even if the
		 * BIOS/SMM code seems pretty broken.
		 */
		temp = 500;	/* arbitrary: five seconds */

		ohci_writel (ohci, OHCI_INTR_OC, &ohci->regs->intrenable);
		ohci_writel (ohci, OHCI_OCR, &ohci->regs->cmdstatus);
		while (ohci_readl (ohci, &ohci->regs->control) & OHCI_CTRL_IR) {
			msleep (10);
			if (--temp == 0) {
				ohci_err (ohci, "USB HC takeover failed!"
					"  (BIOS/SMM bug)\n");
				return -EBUSY;
			}
		}
		ohci_usb_reset (ohci);
	}
#endif

	/* Disable HC interrupts */
	ohci_writel (ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);

	/* flush the writes, and save key bits like RWC */
	if (ohci_readl (ohci, &ohci->regs->control) & OHCI_CTRL_RWC)
		ohci->hc_control |= OHCI_CTRL_RWC;

	/* Read the number of ports unless overridden */
	if (ohci->num_ports == 0)
		ohci->num_ports = roothub_a(ohci) & RH_A_NDP;

	if (ohci->hcca)
		return 0;

	ohci->hcca = dma_alloc_coherent (hcd->self.controller,
			sizeof *ohci->hcca, &ohci->hcca_dma, 0);
	if (!ohci->hcca)
		return -ENOMEM;

	if ((ret = ohci_mem_init (ohci)) < 0)
		ohci_stop (hcd);
	else {
		create_debug_files (ohci);
	}

	return ret;
}

/*-------------------------------------------------------------------------*/

/* Start an OHCI controller, set the BUS operational
 * resets USB and controller
 * enable interrupts
 */
static int ohci_run (struct ohci_hcd *ohci)
{
	u32			mask, val;
	int			first = ohci->fminterval == 0;
	struct usb_hcd		*hcd = ohci_to_hcd(ohci);

	disable (ohci);

	/* boot firmware should have set this up (5.1.1.3.1) */
	if (first) {

		val = ohci_readl (ohci, &ohci->regs->fminterval);
		ohci->fminterval = val & 0x3fff;
		if (ohci->fminterval != FI)
			ohci_dbg (ohci, "fminterval delta %d\n",
				ohci->fminterval - FI);
		ohci->fminterval |= FSMP (ohci->fminterval) << 16;
		/* also: power/overcurrent flags in roothub.a */
	}

	/* Reset USB nearly "by the book".  RemoteWakeupConnected has
	 * to be checked in case boot firmware (BIOS/SMM/...) has set up
	 * wakeup in a way the bus isn't aware of (e.g., legacy PCI PM).
	 * If the bus glue detected wakeup capability then it should
	 * already be enabled; if so we'll just enable it again.
	 */
	if ((ohci->hc_control & OHCI_CTRL_RWC) != 0)
		device_set_wakeup_capable(hcd->self.controller, 1);

	switch (ohci->hc_control & OHCI_CTRL_HCFS) {
	case OHCI_USB_OPER:
		val = 0;
		break;
	case OHCI_USB_SUSPEND:
	case OHCI_USB_RESUME:
		ohci->hc_control &= OHCI_CTRL_RWC;
		ohci->hc_control |= OHCI_USB_RESUME;
		val = 10 /* msec wait */;
		break;
	// case OHCI_USB_RESET:
	default:
		ohci->hc_control &= OHCI_CTRL_RWC;
		ohci->hc_control |= OHCI_USB_RESET;
		val = 50 /* msec wait */;
		break;
	}
	ohci_writel (ohci, ohci->hc_control, &ohci->regs->control);
	// flush the writes
	(void) ohci_readl (ohci, &ohci->regs->control);
	msleep(val);

	memset (ohci->hcca, 0, sizeof (struct ohci_hcca));

	/* 2msec timelimit here means no irqs/preempt */
	spin_lock_irq (&ohci->lock);

retry:
	/* HC Reset requires max 10 us delay */
	ohci_writel (ohci, OHCI_HCR,  &ohci->regs->cmdstatus);
	val = 30;	/* ... allow extra time */
	while ((ohci_readl (ohci, &ohci->regs->cmdstatus) & OHCI_HCR) != 0) {
		if (--val == 0) {
			spin_unlock_irq (&ohci->lock);
			ohci_err (ohci, "USB HC reset timed out!\n");
			return -1;
		}
		udelay (1);
	}

	/* now we're in the SUSPEND state ... must go OPERATIONAL
	 * within 2msec else HC enters RESUME
	 *
	 * ... but some hardware won't init fmInterval "by the book"
	 * (SiS, OPTi ...), so reset again instead.  SiS doesn't need
	 * this if we write fmInterval after we're OPERATIONAL.
	 * Unclear about ALi, ServerWorks, and others ... this could
	 * easily be a longstanding bug in chip init on Linux.
	 */
	if (ohci->flags & OHCI_QUIRK_INITRESET) {
		ohci_writel (ohci, ohci->hc_control, &ohci->regs->control);
		// flush those writes
		(void) ohci_readl (ohci, &ohci->regs->control);
	}

	/* Tell the controller where the control and bulk lists are
	 * The lists are empty now. */
	ohci_writel (ohci, 0, &ohci->regs->ed_controlhead);
	ohci_writel (ohci, 0, &ohci->regs->ed_bulkhead);

	/* a reset clears this */
	ohci_writel (ohci, (u32) ohci->hcca_dma, &ohci->regs->hcca);

	periodic_reinit (ohci);

	/* some OHCI implementations are finicky about how they init.
	 * bogus values here mean not even enumeration could work.
	 */
	if ((ohci_readl (ohci, &ohci->regs->fminterval) & 0x3fff0000) == 0
			|| !ohci_readl (ohci, &ohci->regs->periodicstart)) {
		if (!(ohci->flags & OHCI_QUIRK_INITRESET)) {
			ohci->flags |= OHCI_QUIRK_INITRESET;
			ohci_dbg (ohci, "enabling initreset quirk\n");
			goto retry;
		}
		spin_unlock_irq (&ohci->lock);
		ohci_err (ohci, "init err (%08x %04x)\n",
			ohci_readl (ohci, &ohci->regs->fminterval),
			ohci_readl (ohci, &ohci->regs->periodicstart));
		return -EOVERFLOW;
	}

	/* use rhsc irqs after khubd is fully initialized */
	set_bit(HCD_FLAG_POLL_RH, &hcd->flags);
	hcd->uses_new_polling = 1;

	/* start controller operations */
	ohci->hc_control &= OHCI_CTRL_RWC;
	ohci->hc_control |= OHCI_CONTROL_INIT | OHCI_USB_OPER;
	ohci_writel (ohci, ohci->hc_control, &ohci->regs->control);
	hcd->state = HC_STATE_RUNNING;

	/* wake on ConnectStatusChange, matching external hubs */
	ohci_writel (ohci, RH_HS_DRWE, &ohci->regs->roothub.status);

	/* Choose the interrupts we care about now, others later on demand */
	mask = OHCI_INTR_INIT;
	ohci_writel (ohci, ~0, &ohci->regs->intrstatus);
	ohci_writel (ohci, mask, &ohci->regs->intrenable);

	/* handle root hub init quirks ... */
	val = roothub_a (ohci);
	val &= ~(RH_A_PSM | RH_A_OCPM);
	if (ohci->flags & OHCI_QUIRK_SUPERIO) {
		/* NSC 87560 and maybe others */
		val |= RH_A_NOCP;
		val &= ~(RH_A_POTPGT | RH_A_NPS);
		ohci_writel (ohci, val, &ohci->regs->roothub.a);
	} else if ((ohci->flags & OHCI_QUIRK_AMD756) ||
			(ohci->flags & OHCI_QUIRK_HUB_POWER)) {
		/* hub power always on; required for AMD-756 and some
		 * Mac platforms.  ganged overcurrent reporting, if any.
		 */
		val |= RH_A_NPS;
		ohci_writel (ohci, val, &ohci->regs->roothub.a);
	}
	ohci_writel (ohci, RH_HS_LPSC, &ohci->regs->roothub.status);
	ohci_writel (ohci, (val & RH_A_NPS) ? 0 : RH_B_PPCM,
						&ohci->regs->roothub.b);
	// flush those writes
	(void) ohci_readl (ohci, &ohci->regs->control);

	ohci->next_statechange = jiffies + STATECHANGE_DELAY;
	spin_unlock_irq (&ohci->lock);

	// POTPGT delay is bits 24-31, in 2 ms units.
	mdelay ((val >> 23) & 0x1fe);
	hcd->state = HC_STATE_RUNNING;

	if (quirk_zfmicro(ohci)) {
		/* Create timer to watch for bad queue state on ZF Micro */
		setup_timer(&ohci->unlink_watchdog, unlink_watchdog_func,
				(unsigned long) ohci);

		ohci->eds_scheduled = 0;
		ohci->ed_to_check = NULL;
	}

	ohci_dump (ohci, 1);

	return 0;
}

/*-------------------------------------------------------------------------*/

/* an interrupt happens */

static irqreturn_t ohci_irq (struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);
	struct ohci_regs __iomem *regs = ohci->regs;
	int			ints;

	/* Read interrupt status (and flush pending writes).  We ignore the
	 * optimization of checking the LSB of hcca->done_head; it doesn't
	 * work on all systems (edge triggering for OHCI can be a factor).
	 */
	ints = ohci_readl(ohci, &regs->intrstatus);

	/* Check for an all 1's result which is a typical consequence
	 * of dead, unclocked, or unplugged (CardBus...) devices
	 */
	if (ints == ~(u32)0) {
		disable (ohci);
		ohci_dbg (ohci, "device removed!\n");
		return IRQ_HANDLED;
	}

	/* We only care about interrupts that are enabled */
	ints &= ohci_readl(ohci, &regs->intrenable);

	/* interrupt for some other device? */
	if (ints == 0)
		return IRQ_NOTMINE;

	if (ints & OHCI_INTR_UE) {
		// e.g. due to PCI Master/Target Abort
		if (quirk_nec(ohci)) {
			/* Workaround for a silicon bug in some NEC chips used
			 * in Apple's PowerBooks. Adapted from Darwin code.
			 */
			ohci_err (ohci, "OHCI Unrecoverable Error, scheduling NEC chip restart\n");

			ohci_writel (ohci, OHCI_INTR_UE, &regs->intrdisable);

			schedule_work (&ohci->nec_work);
		} else {
			disable (ohci);
			ohci_err (ohci, "OHCI Unrecoverable Error, disabled\n");
		}

		ohci_dump (ohci, 1);
		ohci_usb_reset (ohci);
	}

	if (ints & OHCI_INTR_RHSC) {
		ohci_vdbg(ohci, "rhsc\n");
		ohci->next_statechange = jiffies + STATECHANGE_DELAY;
		ohci_writel(ohci, OHCI_INTR_RD | OHCI_INTR_RHSC,
				&regs->intrstatus);

		/* NOTE: Vendors didn't always make the same implementation
		 * choices for RHSC.  Many followed the spec; RHSC triggers
		 * on an edge, like setting and maybe clearing a port status
		 * change bit.  With others it's level-triggered, active
		 * until khubd clears all the port status change bits.  We'll
		 * always disable it here and rely on polling until khubd
		 * re-enables it.
		 */
		ohci_writel(ohci, OHCI_INTR_RHSC, &regs->intrdisable);
		usb_hcd_poll_rh_status(hcd);
	}

	/* For connect and disconnect events, we expect the controller
	 * to turn on RHSC along with RD.  But for remote wakeup events
	 * this might not happen.
	 */
	else if (ints & OHCI_INTR_RD) {
		ohci_vdbg(ohci, "resume detect\n");
		ohci_writel(ohci, OHCI_INTR_RD, &regs->intrstatus);
		set_bit(HCD_FLAG_POLL_RH, &hcd->flags);
		if (ohci->autostop) {
			spin_lock (&ohci->lock);
			ohci_rh_resume (ohci);
			spin_unlock (&ohci->lock);
		} else
			usb_hcd_resume_root_hub(hcd);
	}

	if (ints & OHCI_INTR_WDH) {
		spin_lock (&ohci->lock);
		dl_done_list (ohci);
		spin_unlock (&ohci->lock);
	}

	if (quirk_zfmicro(ohci) && (ints & OHCI_INTR_SF)) {
		spin_lock(&ohci->lock);
		if (ohci->ed_to_check) {
			struct ed *ed = ohci->ed_to_check;

			if (check_ed(ohci, ed)) {
				/* HC thinks the TD list is empty; HCD knows
				 * at least one TD is outstanding
				 */
				if (--ohci->zf_delay == 0) {
					struct td *td = list_entry(
						ed->td_list.next,
						struct td, td_list);
					ohci_warn(ohci,
						  "Reclaiming orphan TD %p\n",
						  td);
					takeback_td(ohci, td);
					ohci->ed_to_check = NULL;
				}
			} else
				ohci->ed_to_check = NULL;
		}
		spin_unlock(&ohci->lock);
	}

	/* could track INTR_SO to reduce available PCI/... bandwidth */

	/* handle any pending URB/ED unlinks, leaving INTR_SF enabled
	 * when there's still unlinking to be done (next frame).
	 */
	spin_lock (&ohci->lock);
	if (ohci->ed_rm_list)
		finish_unlinks (ohci, ohci_frame_no(ohci));
	if ((ints & OHCI_INTR_SF) != 0
			&& !ohci->ed_rm_list
			&& !ohci->ed_to_check
			&& HC_IS_RUNNING(hcd->state))
		ohci_writel (ohci, OHCI_INTR_SF, &regs->intrdisable);
	spin_unlock (&ohci->lock);

	if (HC_IS_RUNNING(hcd->state)) {
		ohci_writel (ohci, ints, &regs->intrstatus);
		ohci_writel (ohci, OHCI_INTR_MIE, &regs->intrenable);
		// flush those writes
		(void) ohci_readl (ohci, &ohci->regs->control);
	}

	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/

static void ohci_stop (struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);

	ohci_dump (ohci, 1);

	flush_scheduled_work();

	ohci_usb_reset (ohci);
	ohci_writel (ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
	free_irq(hcd->irq, hcd);
	hcd->irq = -1;

	if (quirk_zfmicro(ohci))
		del_timer(&ohci->unlink_watchdog);
	if (quirk_amdiso(ohci))
		amd_iso_dev_put();

	remove_debug_files (ohci);
	ohci_mem_cleanup (ohci);
	if (ohci->hcca) {
		dma_free_coherent (hcd->self.controller,
				sizeof *ohci->hcca,
				ohci->hcca, ohci->hcca_dma);
		ohci->hcca = NULL;
		ohci->hcca_dma = 0;
	}
}

/*-------------------------------------------------------------------------*/

#if defined(CONFIG_PM) || defined(CONFIG_PCI)

/* must not be called from interrupt context */
static int ohci_restart (struct ohci_hcd *ohci)
{
	int temp;
	int i;
	struct urb_priv *priv;

	spin_lock_irq(&ohci->lock);
	disable (ohci);

	/* Recycle any "live" eds/tds (and urbs). */
	if (!list_empty (&ohci->pending))
		ohci_dbg(ohci, "abort schedule...\n");
	list_for_each_entry (priv, &ohci->pending, pending) {
		struct urb	*urb = priv->td[0]->urb;
		struct ed	*ed = priv->ed;

		switch (ed->state) {
		case ED_OPER:
			ed->state = ED_UNLINK;
			ed->hwINFO |= cpu_to_hc32(ohci, ED_DEQUEUE);
			ed_deschedule (ohci, ed);

			ed->ed_next = ohci->ed_rm_list;
			ed->ed_prev = NULL;
			ohci->ed_rm_list = ed;
			/* FALLTHROUGH */
		case ED_UNLINK:
			break;
		default:
			ohci_dbg(ohci, "bogus ed %p state %d\n",
					ed, ed->state);
		}

		if (!urb->unlinked)
			urb->unlinked = -ESHUTDOWN;
	}
	finish_unlinks (ohci, 0);
	spin_unlock_irq(&ohci->lock);

	/* paranoia, in case that didn't work: */

	/* empty the interrupt branches */
	for (i = 0; i < NUM_INTS; i++) ohci->load [i] = 0;
	for (i = 0; i < NUM_INTS; i++) ohci->hcca->int_table [i] = 0;

	/* no EDs to remove */
	ohci->ed_rm_list = NULL;

	/* empty control and bulk lists */
	ohci->ed_controltail = NULL;
	ohci->ed_bulktail    = NULL;

	if ((temp = ohci_run (ohci)) < 0) {
		ohci_err (ohci, "can't restart, %d\n", temp);
		return temp;
	}
	ohci_dbg(ohci, "restart complete\n");
	return 0;
}

#endif

/*-------------------------------------------------------------------------*/

MODULE_AUTHOR (DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE ("GPL");

#ifdef CONFIG_PCI
#include "ohci-pci.c"
#define PCI_DRIVER		ohci_pci_driver
#endif

#if defined(CONFIG_ARCH_SA1100) && defined(CONFIG_SA1111)
#include "ohci-sa1111.c"
#define SA1111_DRIVER		ohci_hcd_sa1111_driver
#endif

#if defined(CONFIG_ARCH_S3C2410) || defined(CONFIG_ARCH_S3C64XX)
#include "ohci-s3c2410.c"
#define PLATFORM_DRIVER		ohci_hcd_s3c2410_driver
#endif

#ifdef CONFIG_USB_OHCI_HCD_OMAP1
#include "ohci-omap.c"
#define OMAP1_PLATFORM_DRIVER	ohci_hcd_omap_driver
#endif

#ifdef CONFIG_USB_OHCI_HCD_OMAP3
#include "ohci-omap3.c"
#define OMAP3_PLATFORM_DRIVER	ohci_hcd_omap3_driver
#endif

#ifdef CONFIG_ARCH_LH7A404
#include "ohci-lh7a404.c"
#define PLATFORM_DRIVER		ohci_hcd_lh7a404_driver
#endif

#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)
#include "ohci-pxa27x.c"
#define PLATFORM_DRIVER		ohci_hcd_pxa27x_driver
#endif

#ifdef CONFIG_ARCH_EP93XX
#include "ohci-ep93xx.c"
#define PLATFORM_DRIVER		ohci_hcd_ep93xx_driver
#endif

#ifdef CONFIG_MIPS_ALCHEMY
#include "ohci-au1xxx.c"
#define PLATFORM_DRIVER		ohci_hcd_au1xxx_driver
#endif

#ifdef CONFIG_PNX8550
#include "ohci-pnx8550.c"
#define PLATFORM_DRIVER		ohci_hcd_pnx8550_driver
#endif

#ifdef CONFIG_USB_OHCI_HCD_PPC_SOC
#include "ohci-ppc-soc.c"
#define PLATFORM_DRIVER		ohci_hcd_ppc_soc_driver
#endif

#ifdef CONFIG_ARCH_AT91
#include "ohci-at91.c"
#define PLATFORM_DRIVER		ohci_hcd_at91_driver
#endif

#ifdef CONFIG_ARCH_PNX4008
#include "ohci-pnx4008.c"
#define PLATFORM_DRIVER		usb_hcd_pnx4008_driver
#endif

#ifdef CONFIG_ARCH_DAVINCI_DA8XX
#include "ohci-da8xx.c"
#define PLATFORM_DRIVER		ohci_hcd_da8xx_driver
#endif

#if defined(CONFIG_CPU_SUBTYPE_SH7720) || \
    defined(CONFIG_CPU_SUBTYPE_SH7721) || \
    defined(CONFIG_CPU_SUBTYPE_SH7763) || \
    defined(CONFIG_CPU_SUBTYPE_SH7786)
#include "ohci-sh.c"
#define PLATFORM_DRIVER		ohci_hcd_sh_driver
#endif


#ifdef CONFIG_USB_OHCI_HCD_PPC_OF
#include "ohci-ppc-of.c"
#define OF_PLATFORM_DRIVER	ohci_hcd_ppc_of_driver
#endif

#ifdef CONFIG_PPC_PS3
#include "ohci-ps3.c"
#define PS3_SYSTEM_BUS_DRIVER	ps3_ohci_driver
#endif

#ifdef CONFIG_USB_OHCI_HCD_SSB
#include "ohci-ssb.c"
#define SSB_OHCI_DRIVER		ssb_ohci_driver
#endif

#ifdef CONFIG_MFD_SM501
#include "ohci-sm501.c"
#define SM501_OHCI_DRIVER	ohci_hcd_sm501_driver
#endif

#ifdef CONFIG_MFD_TC6393XB
#include "ohci-tmio.c"
#define TMIO_OHCI_DRIVER	ohci_hcd_tmio_driver
#endif

#ifdef CONFIG_MACH_JZ4740
#include "ohci-jz4740.c"
#define PLATFORM_DRIVER	ohci_hcd_jz4740_driver
#endif

#ifdef CONFIG_USB_OCTEON_OHCI
#include "ohci-octeon.c"
#define PLATFORM_DRIVER		ohci_octeon_driver
#endif

#ifdef CONFIG_USB_CNS3XXX_OHCI
#include "ohci-cns3xxx.c"
#define PLATFORM_DRIVER		ohci_hcd_cns3xxx_driver
#endif

#if	!defined(PCI_DRIVER) &&		\
	!defined(PLATFORM_DRIVER) &&	\
	!defined(OMAP1_PLATFORM_DRIVER) &&	\
	!defined(OMAP3_PLATFORM_DRIVER) &&	\
	!defined(OF_PLATFORM_DRIVER) &&	\
	!defined(SA1111_DRIVER) &&	\
	!defined(PS3_SYSTEM_BUS_DRIVER) && \
	!defined(SM501_OHCI_DRIVER) && \
	!defined(TMIO_OHCI_DRIVER) && \
	!defined(SSB_OHCI_DRIVER)
#error "missing bus glue for ohci-hcd"
#endif

static int __init ohci_hcd_mod_init(void)
{
	int retval = 0;

	if (usb_disabled())
		return -ENODEV;

	printk(KERN_INFO "%s: " DRIVER_DESC "\n", hcd_name);
	pr_debug ("%s: block sizes: ed %Zd td %Zd\n", hcd_name,
		sizeof (struct ed), sizeof (struct td));
	set_bit(USB_OHCI_LOADED, &usb_hcds_loaded);

#ifdef DEBUG
	ohci_debug_root = debugfs_create_dir("ohci", usb_debug_root);
	if (!ohci_debug_root) {
		retval = -ENOENT;
		goto error_debug;
	}
#endif

#ifdef PS3_SYSTEM_BUS_DRIVER
	retval = ps3_ohci_driver_register(&PS3_SYSTEM_BUS_DRIVER);
	if (retval < 0)
		goto error_ps3;
#endif

#ifdef PLATFORM_DRIVER
	retval = platform_driver_register(&PLATFORM_DRIVER);
	if (retval < 0)
		goto error_platform;
#endif

#ifdef OMAP1_PLATFORM_DRIVER
	retval = platform_driver_register(&OMAP1_PLATFORM_DRIVER);
	if (retval < 0)
		goto error_omap1_platform;
#endif

#ifdef OMAP3_PLATFORM_DRIVER
	retval = platform_driver_register(&OMAP3_PLATFORM_DRIVER);
	if (retval < 0)
		goto error_omap3_platform;
#endif

#ifdef OF_PLATFORM_DRIVER
	retval = of_register_platform_driver(&OF_PLATFORM_DRIVER);
	if (retval < 0)
		goto error_of_platform;
#endif

#ifdef SA1111_DRIVER
	retval = sa1111_driver_register(&SA1111_DRIVER);
	if (retval < 0)
		goto error_sa1111;
#endif

#ifdef PCI_DRIVER
	retval = pci_register_driver(&PCI_DRIVER);
	if (retval < 0)
		goto error_pci;
#endif

#ifdef SSB_OHCI_DRIVER
	retval = ssb_driver_register(&SSB_OHCI_DRIVER);
	if (retval)
		goto error_ssb;
#endif

#ifdef SM501_OHCI_DRIVER
	retval = platform_driver_register(&SM501_OHCI_DRIVER);
	if (retval < 0)
		goto error_sm501;
#endif

#ifdef TMIO_OHCI_DRIVER
	retval = platform_driver_register(&TMIO_OHCI_DRIVER);
	if (retval < 0)
		goto error_tmio;
#endif

	return retval;

	/* Error path */
#ifdef TMIO_OHCI_DRIVER
	platform_driver_unregister(&TMIO_OHCI_DRIVER);
 error_tmio:
#endif
#ifdef SM501_OHCI_DRIVER
	platform_driver_unregister(&SM501_OHCI_DRIVER);
 error_sm501:
#endif
#ifdef SSB_OHCI_DRIVER
	ssb_driver_unregister(&SSB_OHCI_DRIVER);
 error_ssb:
#endif
#ifdef PCI_DRIVER
	pci_unregister_driver(&PCI_DRIVER);
 error_pci:
#endif
#ifdef SA1111_DRIVER
	sa1111_driver_unregister(&SA1111_DRIVER);
 error_sa1111:
#endif
#ifdef OF_PLATFORM_DRIVER
	of_unregister_platform_driver(&OF_PLATFORM_DRIVER);
 error_of_platform:
#endif
#ifdef PLATFORM_DRIVER
	platform_driver_unregister(&PLATFORM_DRIVER);
 error_platform:
#endif
#ifdef OMAP1_PLATFORM_DRIVER
	platform_driver_unregister(&OMAP1_PLATFORM_DRIVER);
 error_omap1_platform:
#endif
#ifdef OMAP3_PLATFORM_DRIVER
	platform_driver_unregister(&OMAP3_PLATFORM_DRIVER);
 error_omap3_platform:
#endif
#ifdef PS3_SYSTEM_BUS_DRIVER
	ps3_ohci_driver_unregister(&PS3_SYSTEM_BUS_DRIVER);
 error_ps3:
#endif
#ifdef DEBUG
	debugfs_remove(ohci_debug_root);
	ohci_debug_root = NULL;
 error_debug:
#endif

	clear_bit(USB_OHCI_LOADED, &usb_hcds_loaded);
	return retval;
}
module_init(ohci_hcd_mod_init);

static void __exit ohci_hcd_mod_exit(void)
{
#ifdef TMIO_OHCI_DRIVER
	platform_driver_unregister(&TMIO_OHCI_DRIVER);
#endif
#ifdef SM501_OHCI_DRIVER
	platform_driver_unregister(&SM501_OHCI_DRIVER);
#endif
#ifdef SSB_OHCI_DRIVER
	ssb_driver_unregister(&SSB_OHCI_DRIVER);
#endif
#ifdef PCI_DRIVER
	pci_unregister_driver(&PCI_DRIVER);
#endif
#ifdef SA1111_DRIVER
	sa1111_driver_unregister(&SA1111_DRIVER);
#endif
#ifdef OF_PLATFORM_DRIVER
	of_unregister_platform_driver(&OF_PLATFORM_DRIVER);
#endif
#ifdef PLATFORM_DRIVER
	platform_driver_unregister(&PLATFORM_DRIVER);
#endif
#ifdef OMAP3_PLATFORM_DRIVER
	platform_driver_unregister(&OMAP3_PLATFORM_DRIVER);
#endif
#ifdef PS3_SYSTEM_BUS_DRIVER
	ps3_ohci_driver_unregister(&PS3_SYSTEM_BUS_DRIVER);
#endif
#ifdef DEBUG
	debugfs_remove(ohci_debug_root);
#endif
	clear_bit(USB_OHCI_LOADED, &usb_hcds_loaded);
}
module_exit(ohci_hcd_mod_exit);

