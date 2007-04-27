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
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/usb.h>
#include <linux/usb/otg.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/reboot.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>
#include <asm/byteorder.h>
#ifdef CONFIG_PPC_PS3
#include <asm/firmware.h>
#endif

#include "../core/hcd.h"

#define DRIVER_VERSION "2006 August 04"
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
	struct usb_host_endpoint *ep,
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
	urb_print (urb, "SUB", usb_pipein (pipe));
#endif

	/* every endpoint has a ed, locate and maybe (re)initialize it */
	if (! (ed = ed_get (ohci, ep, urb->dev, pipe, urb->interval)))
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
	urb_priv = kmalloc (sizeof (urb_priv_t) + size * sizeof (struct td *),
			mem_flags);
	if (!urb_priv)
		return -ENOMEM;
	memset (urb_priv, 0, sizeof (urb_priv_t) + size * sizeof (struct td *));
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
	if (!test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags)) {
		retval = -ENODEV;
		goto fail;
	}
	if (!HC_IS_RUNNING(hcd->state)) {
		retval = -ENODEV;
		goto fail;
	}

	/* in case of unlink-during-submit */
	spin_lock (&urb->lock);
	if (urb->status != -EINPROGRESS) {
		spin_unlock (&urb->lock);
		urb->hcpriv = urb_priv;
		finish_urb (ohci, urb);
		retval = 0;
		goto fail;
	}

	/* schedule the ed if needed */
	if (ed->state == ED_IDLE) {
		retval = ed_schedule (ohci, ed);
		if (retval < 0)
			goto fail0;
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

fail0:
	spin_unlock (&urb->lock);
fail:
	if (retval)
		urb_free_priv (ohci, urb_priv);
	spin_unlock_irqrestore (&ohci->lock, flags);
	return retval;
}

/*
 * decouple the URB from the HC queues (TDs, urb_priv); it's
 * already marked using urb->status.  reporting is always done
 * asynchronously, and we might be dealing with an urb that's
 * partially transferred, or an ED with other urbs being unlinked.
 */
static int ohci_urb_dequeue (struct usb_hcd *hcd, struct urb *urb)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);
	unsigned long		flags;

#ifdef OHCI_VERBOSE_DEBUG
	urb_print (urb, "UNLINK", 1);
#endif

	spin_lock_irqsave (&ohci->lock, flags);
	if (HC_IS_RUNNING(hcd->state)) {
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
			finish_urb (ohci, urb);
	}
	spin_unlock_irqrestore (&ohci->lock, flags);
	return 0;
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
		finish_unlinks (ohci, 0);
	}

	switch (ed->state) {
	case ED_UNLINK:		/* wait for hw to finish? */
		/* major IRQ delivery trouble loses INTR_SF too... */
		if (limit-- == 0) {
			ohci_warn (ohci, "IRQ INTR_SF lossage\n");
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
	return;
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
	ohci_usb_reset (ohci);
	/* flush the writes */
	(void) ohci_readl (ohci, &ohci->regs->control);
}

/*-------------------------------------------------------------------------*
 * HC functions
 *-------------------------------------------------------------------------*/

/* init memory, and kick BIOS/SMM off */

static int ohci_init (struct ohci_hcd *ohci)
{
	int ret;
	struct usb_hcd *hcd = ohci_to_hcd(ohci);

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
	u32			mask, temp;
	int			first = ohci->fminterval == 0;
	struct usb_hcd		*hcd = ohci_to_hcd(ohci);

	disable (ohci);

	/* boot firmware should have set this up (5.1.1.3.1) */
	if (first) {

		temp = ohci_readl (ohci, &ohci->regs->fminterval);
		ohci->fminterval = temp & 0x3fff;
		if (ohci->fminterval != FI)
			ohci_dbg (ohci, "fminterval delta %d\n",
				ohci->fminterval - FI);
		ohci->fminterval |= FSMP (ohci->fminterval) << 16;
		/* also: power/overcurrent flags in roothub.a */
	}

	/* Reset USB nearly "by the book".  RemoteWakeupConnected was
	 * saved if boot firmware (BIOS/SMM/...) told us it's connected,
	 * or if bus glue did the same (e.g. for PCI add-in cards with
	 * PCI PM support).
	 */
	if ((ohci->hc_control & OHCI_CTRL_RWC) != 0
			&& !device_may_wakeup(hcd->self.controller))
		device_init_wakeup(hcd->self.controller, 1);

	switch (ohci->hc_control & OHCI_CTRL_HCFS) {
	case OHCI_USB_OPER:
		temp = 0;
		break;
	case OHCI_USB_SUSPEND:
	case OHCI_USB_RESUME:
		ohci->hc_control &= OHCI_CTRL_RWC;
		ohci->hc_control |= OHCI_USB_RESUME;
		temp = 10 /* msec wait */;
		break;
	// case OHCI_USB_RESET:
	default:
		ohci->hc_control &= OHCI_CTRL_RWC;
		ohci->hc_control |= OHCI_USB_RESET;
		temp = 50 /* msec wait */;
		break;
	}
	ohci_writel (ohci, ohci->hc_control, &ohci->regs->control);
	// flush the writes
	(void) ohci_readl (ohci, &ohci->regs->control);
	msleep(temp);
	temp = roothub_a (ohci);
	if (!(temp & RH_A_NPS)) {
		/* power down each port */
		for (temp = 0; temp < ohci->num_ports; temp++)
			ohci_writel (ohci, RH_PS_LSDA,
				&ohci->regs->roothub.portstatus [temp]);
	}
	// flush those writes
	(void) ohci_readl (ohci, &ohci->regs->control);
	memset (ohci->hcca, 0, sizeof (struct ohci_hcca));

	/* 2msec timelimit here means no irqs/preempt */
	spin_lock_irq (&ohci->lock);

retry:
	/* HC Reset requires max 10 us delay */
	ohci_writel (ohci, OHCI_HCR,  &ohci->regs->cmdstatus);
	temp = 30;	/* ... allow extra time */
	while ((ohci_readl (ohci, &ohci->regs->cmdstatus) & OHCI_HCR) != 0) {
		if (--temp == 0) {
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
	hcd->poll_rh = 1;
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
	temp = roothub_a (ohci);
	temp &= ~(RH_A_PSM | RH_A_OCPM);
	if (ohci->flags & OHCI_QUIRK_SUPERIO) {
		/* NSC 87560 and maybe others */
		temp |= RH_A_NOCP;
		temp &= ~(RH_A_POTPGT | RH_A_NPS);
		ohci_writel (ohci, temp, &ohci->regs->roothub.a);
	} else if ((ohci->flags & OHCI_QUIRK_AMD756) || distrust_firmware) {
		/* hub power always on; required for AMD-756 and some
		 * Mac platforms.  ganged overcurrent reporting, if any.
		 */
		temp |= RH_A_NPS;
		ohci_writel (ohci, temp, &ohci->regs->roothub.a);
	}
	ohci_writel (ohci, RH_HS_LPSC, &ohci->regs->roothub.status);
	ohci_writel (ohci, (temp & RH_A_NPS) ? 0 : RH_B_PPCM,
						&ohci->regs->roothub.b);
	// flush those writes
	(void) ohci_readl (ohci, &ohci->regs->control);

	ohci->next_statechange = jiffies + STATECHANGE_DELAY;
	spin_unlock_irq (&ohci->lock);

	// POTPGT delay is bits 24-31, in 2 ms units.
	mdelay ((temp >> 23) & 0x1fe);
	hcd->state = HC_STATE_RUNNING;

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

	/* we can eliminate a (slow) ohci_readl()
	   if _only_ WDH caused this irq */
	if ((ohci->hcca->done_head != 0)
			&& ! (hc32_to_cpup (ohci, &ohci->hcca->done_head)
				& 0x01)) {
		ints =  OHCI_INTR_WDH;

	/* cardbus/... hardware gone before remove() */
	} else if ((ints = ohci_readl (ohci, &regs->intrstatus)) == ~(u32)0) {
		disable (ohci);
		ohci_dbg (ohci, "device removed!\n");
		return IRQ_HANDLED;

	/* interrupt for some other device? */
	} else if ((ints &= ohci_readl (ohci, &regs->intrenable)) == 0) {
		return IRQ_NOTMINE;
	}

	if (ints & OHCI_INTR_UE) {
		disable (ohci);
		ohci_err (ohci, "OHCI Unrecoverable Error, disabled\n");
		// e.g. due to PCI Master/Target Abort

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
		hcd->poll_rh = 1;
		if (ohci->autostop) {
			spin_lock (&ohci->lock);
			ohci_rh_resume (ohci);
			spin_unlock (&ohci->lock);
		} else
			usb_hcd_resume_root_hub(hcd);
	}

	if (ints & OHCI_INTR_WDH) {
		if (HC_IS_RUNNING(hcd->state))
			ohci_writel (ohci, OHCI_INTR_WDH, &regs->intrdisable);
		spin_lock (&ohci->lock);
		dl_done_list (ohci);
		spin_unlock (&ohci->lock);
		if (HC_IS_RUNNING(hcd->state))
			ohci_writel (ohci, OHCI_INTR_WDH, &regs->intrenable);
	}

	/* could track INTR_SO to reduce available PCI/... bandwidth */

	/* handle any pending URB/ED unlinks, leaving INTR_SF enabled
	 * when there's still unlinking to be done (next frame).
	 */
	spin_lock (&ohci->lock);
	if (ohci->ed_rm_list)
		finish_unlinks (ohci, ohci_frame_no(ohci));
	if ((ints & OHCI_INTR_SF) != 0 && !ohci->ed_rm_list
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

/* must not be called from interrupt context */

#ifdef	CONFIG_PM

static int ohci_restart (struct ohci_hcd *ohci)
{
	int temp;
	int i;
	struct urb_priv *priv;

	/* mark any devices gone, so they do nothing till khubd disconnects.
	 * recycle any "live" eds/tds (and urbs) right away.
	 * later, khubd disconnect processing will recycle the other state,
	 * (either as disconnect/reconnect, or maybe someday as a reset).
	 */
	spin_lock_irq(&ohci->lock);
	disable (ohci);
	usb_root_hub_lost_power(ohci_to_hcd(ohci)->self.root_hub);
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

		spin_lock (&urb->lock);
		urb->status = -ESHUTDOWN;
		spin_unlock (&urb->lock);
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
	} else {
		/* here we "know" root ports should always stay powered,
		 * and that if we try to turn them back on the root hub
		 * will respond to CSC processing.
		 */
		i = ohci->num_ports;
		while (i--)
			ohci_writel (ohci, RH_PS_PSS,
				&ohci->regs->roothub.portstatus [i]);
		ohci_dbg (ohci, "restart complete\n");
	}
	return 0;
}
#endif

/*-------------------------------------------------------------------------*/

#define DRIVER_INFO DRIVER_VERSION " " DRIVER_DESC

MODULE_AUTHOR (DRIVER_AUTHOR);
MODULE_DESCRIPTION (DRIVER_INFO);
MODULE_LICENSE ("GPL");

#ifdef CONFIG_PCI
#include "ohci-pci.c"
#define PCI_DRIVER		ohci_pci_driver
#endif

#ifdef CONFIG_SA1111
#include "ohci-sa1111.c"
#define SA1111_DRIVER		ohci_hcd_sa1111_driver
#endif

#ifdef CONFIG_ARCH_S3C2410
#include "ohci-s3c2410.c"
#define PLATFORM_DRIVER		ohci_hcd_s3c2410_driver
#endif

#ifdef CONFIG_ARCH_OMAP
#include "ohci-omap.c"
#define PLATFORM_DRIVER		ohci_hcd_omap_driver
#endif

#ifdef CONFIG_ARCH_LH7A404
#include "ohci-lh7a404.c"
#define PLATFORM_DRIVER		ohci_hcd_lh7a404_driver
#endif

#ifdef CONFIG_PXA27x
#include "ohci-pxa27x.c"
#define PLATFORM_DRIVER		ohci_hcd_pxa27x_driver
#endif

#ifdef CONFIG_ARCH_EP93XX
#include "ohci-ep93xx.c"
#define PLATFORM_DRIVER		ohci_hcd_ep93xx_driver
#endif

#ifdef CONFIG_SOC_AU1X00
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


#ifdef CONFIG_USB_OHCI_HCD_PPC_OF
#include "ohci-ppc-of.c"
#define OF_PLATFORM_DRIVER	ohci_hcd_ppc_of_driver
#endif

#ifdef CONFIG_PPC_PS3
#include "ohci-ps3.c"
#define PS3_SYSTEM_BUS_DRIVER	ps3_ohci_sb_driver
#endif

#if	!defined(PCI_DRIVER) &&		\
	!defined(PLATFORM_DRIVER) &&	\
	!defined(OF_PLATFORM_DRIVER) &&	\
	!defined(SA1111_DRIVER) &&	\
	!defined(PS3_SYSTEM_BUS_DRIVER)
#error "missing bus glue for ohci-hcd"
#endif

static int __init ohci_hcd_mod_init(void)
{
	int retval = 0;

	if (usb_disabled())
		return -ENODEV;

	printk (KERN_DEBUG "%s: " DRIVER_INFO "\n", hcd_name);
	pr_debug ("%s: block sizes: ed %Zd td %Zd\n", hcd_name,
		sizeof (struct ed), sizeof (struct td));

#ifdef PS3_SYSTEM_BUS_DRIVER
	if (firmware_has_feature(FW_FEATURE_PS3_LV1)) {
		retval = ps3_system_bus_driver_register(
				&PS3_SYSTEM_BUS_DRIVER);
		if (retval < 0)
			goto error_ps3;
	}
#endif

#ifdef PLATFORM_DRIVER
	retval = platform_driver_register(&PLATFORM_DRIVER);
	if (retval < 0)
		goto error_platform;
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

	return retval;

	/* Error path */
#ifdef PCI_DRIVER
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
#ifdef PS3_SYSTEM_BUS_DRIVER
	if (firmware_has_feature(FW_FEATURE_PS3_LV1))
		ps3_system_bus_driver_unregister(&PS3_SYSTEM_BUS_DRIVER);
 error_ps3:
#endif
	return retval;
}
module_init(ohci_hcd_mod_init);

static void __exit ohci_hcd_mod_exit(void)
{
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
#ifdef PS3_SYSTEM_BUS_DRIVER
	if (firmware_has_feature(FW_FEATURE_PS3_LV1))
		ps3_system_bus_driver_unregister(&PS3_SYSTEM_BUS_DRIVER);
#endif
}
module_exit(ohci_hcd_mod_exit);

