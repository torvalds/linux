/*
 * Open Host Controller Interface (OHCI) driver for USB.
 *
 * Maintainer: Alan Stern <stern@rowland.harvard.edu>
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
#include <asm/unaligned.h>
#include <asm/byteorder.h>


#define DRIVER_AUTHOR "Roman Weissgaerber, David Brownell"
#define DRIVER_DESC "USB 1.1 'Open' Host Controller (OHCI) Driver"

/*-------------------------------------------------------------------------*/

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
#define	IO_WATCHDOG_DELAY	msecs_to_jiffies(275)
#define	IO_WATCHDOG_OFF		0xffffff00

#include "ohci.h"
#include "pci-quirks.h"

static void ohci_dump(struct ohci_hcd *ohci);
static void ohci_stop(struct usb_hcd *hcd);
static void io_watchdog_func(unsigned long _ohci);

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
static bool distrust_firmware = true;
module_param (distrust_firmware, bool, 0);
MODULE_PARM_DESC (distrust_firmware,
	"true to distrust firmware power/overcurrent setup");

/* Some boards leave IR set wrongly, since they fail BIOS/SMM handshakes */
static bool no_handshake;
module_param (no_handshake, bool, 0);
MODULE_PARM_DESC (no_handshake, "true (not default) disables BIOS handshake");

/*-------------------------------------------------------------------------*/

static int number_of_tds(struct urb *urb)
{
	int			len, i, num, this_sg_len;
	struct scatterlist	*sg;

	len = urb->transfer_buffer_length;
	i = urb->num_mapped_sgs;

	if (len > 0 && i > 0) {		/* Scatter-gather transfer */
		num = 0;
		sg = urb->sg;
		for (;;) {
			this_sg_len = min_t(int, sg_dma_len(sg), len);
			num += DIV_ROUND_UP(this_sg_len, 4096);
			len -= this_sg_len;
			if (--i <= 0 || len <= 0)
				break;
			sg = sg_next(sg);
		}

	} else {			/* Non-SG transfer */
		/* one TD for every 4096 Bytes (could be up to 8K) */
		num = DIV_ROUND_UP(len, 4096);
	}
	return num;
}

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

	/* every endpoint has a ed, locate and maybe (re)initialize it */
	ed = ed_get(ohci, urb->ep, urb->dev, pipe, urb->interval);
	if (! ed)
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
			size += number_of_tds(urb);
			/* maybe a zero-length packet to wrap it up */
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
	if (ohci->rh_state != OHCI_RH_RUNNING) {
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

		/* Start up the I/O watchdog timer, if it's not running */
		if (ohci->prev_frame_no == IO_WATCHDOG_OFF &&
				list_empty(&ohci->eds_in_use) &&
				!(ohci->flags & OHCI_QUIRK_QEMU)) {
			ohci->prev_frame_no = ohci_frame_no(ohci);
			mod_timer(&ohci->io_watchdog,
					jiffies + IO_WATCHDOG_DELAY);
		}
		list_add(&ed->in_use_list, &ohci->eds_in_use);

		if (ed->type == PIPE_ISOCHRONOUS) {
			u16	frame = ohci_frame_no(ohci);

			/* delay a few frames before the first TD */
			frame += max_t (u16, 8, ed->interval);
			frame &= ~(ed->interval - 1);
			frame |= ed->branch;
			urb->start_frame = frame;
			ed->last_iso = frame + ed->interval * (size - 1);
		}
	} else if (ed->type == PIPE_ISOCHRONOUS) {
		u16	next = ohci_frame_no(ohci) + 1;
		u16	frame = ed->last_iso + ed->interval;
		u16	length = ed->interval * (size - 1);

		/* Behind the scheduling threshold? */
		if (unlikely(tick_before(frame, next))) {

			/* URB_ISO_ASAP: Round up to the first available slot */
			if (urb->transfer_flags & URB_ISO_ASAP) {
				frame += (next - frame + ed->interval - 1) &
						-ed->interval;

			/*
			 * Not ASAP: Use the next slot in the stream,
			 * no matter what.
			 */
			} else {
				/*
				 * Some OHCI hardware doesn't handle late TDs
				 * correctly.  After retiring them it proceeds
				 * to the next ED instead of the next TD.
				 * Therefore we have to omit the late TDs
				 * entirely.
				 */
				urb_priv->td_cnt = DIV_ROUND_UP(
						(u16) (next - frame),
						ed->interval);
				if (urb_priv->td_cnt >= urb_priv->length) {
					++urb_priv->td_cnt;	/* Mark it */
					ohci_dbg(ohci, "iso underrun %p (%u+%u < %u)\n",
							urb, frame, length,
							next);
				}
			}
		}
		urb->start_frame = frame;
		ed->last_iso = frame + length;
	}

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
	urb_priv_t		*urb_priv;

	spin_lock_irqsave (&ohci->lock, flags);
	rc = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (rc == 0) {

		/* Unless an IRQ completed the unlink while it was being
		 * handed to us, flag it for unlink and giveback, and force
		 * some upcoming INTR_SF to call finish_unlinks()
		 */
		urb_priv = urb->hcpriv;
		if (urb_priv->ed->state == ED_OPER)
			start_ed_unlink(ohci, urb_priv->ed);

		if (ohci->rh_state != OHCI_RH_RUNNING) {
			/* With HC dead, we can clean up right away */
			ohci_work(ohci);
		}
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

	if (ohci->rh_state != OHCI_RH_RUNNING) {
sanitize:
		ed->state = ED_IDLE;
		ohci_work(ohci);
	}

	switch (ed->state) {
	case ED_UNLINK:		/* wait for hw to finish? */
		/* major IRQ delivery trouble loses INTR_SF too... */
		if (limit-- == 0) {
			ohci_warn(ohci, "ED unlink timeout\n");
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
	ohci->rh_state = OHCI_RH_HALTED;
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
	ohci_writel(ohci, (u32) ~0, &ohci->regs->intrdisable);

	/* Software reset, after which the controller goes into SUSPEND */
	ohci_writel(ohci, OHCI_HCR, &ohci->regs->cmdstatus);
	ohci_readl(ohci, &ohci->regs->cmdstatus);	/* flush the writes */
	udelay(10);

	ohci_writel(ohci, ohci->fminterval, &ohci->regs->fminterval);
	ohci->rh_state = OHCI_RH_HALTED;
}

/*-------------------------------------------------------------------------*
 * HC functions
 *-------------------------------------------------------------------------*/

/* init memory, and kick BIOS/SMM off */

static int ohci_init (struct ohci_hcd *ohci)
{
	int ret;
	struct usb_hcd *hcd = ohci_to_hcd(ohci);

	/* Accept arbitrarily long scatter-gather lists */
	hcd->self.sg_tablesize = ~0;

	if (distrust_firmware)
		ohci->flags |= OHCI_QUIRK_HUB_POWER;

	ohci->rh_state = OHCI_RH_HALTED;
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

	setup_timer(&ohci->io_watchdog, io_watchdog_func,
			(unsigned long) ohci);
	ohci->prev_frame_no = IO_WATCHDOG_OFF;

	ohci->hcca = dma_alloc_coherent (hcd->self.controller,
			sizeof(*ohci->hcca), &ohci->hcca_dma, GFP_KERNEL);
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

	ohci->rh_state = OHCI_RH_HALTED;

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

	/* use rhsc irqs after hub_wq is allocated */
	set_bit(HCD_FLAG_POLL_RH, &hcd->flags);
	hcd->uses_new_polling = 1;

	/* start controller operations */
	ohci->hc_control &= OHCI_CTRL_RWC;
	ohci->hc_control |= OHCI_CONTROL_INIT | OHCI_USB_OPER;
	ohci_writel (ohci, ohci->hc_control, &ohci->regs->control);
	ohci->rh_state = OHCI_RH_RUNNING;

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

	ohci_dump(ohci);

	return 0;
}

/* ohci_setup routine for generic controller initialization */

int ohci_setup(struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci(hcd);

	ohci_hcd_init(ohci);
	
	return ohci_init(ohci);
}
EXPORT_SYMBOL_GPL(ohci_setup);

/* ohci_start routine for generic controller start of all OHCI bus glue */
static int ohci_start(struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci(hcd);
	int	ret;

	ret = ohci_run(ohci);
	if (ret < 0) {
		ohci_err(ohci, "can't start\n");
		ohci_stop(hcd);
	}
	return ret;
}

/*-------------------------------------------------------------------------*/

/*
 * Some OHCI controllers are known to lose track of completed TDs.  They
 * don't add the TDs to the hardware done queue, which means we never see
 * them as being completed.
 *
 * This watchdog routine checks for such problems.  Without some way to
 * tell when those TDs have completed, we would never take their EDs off
 * the unlink list.  As a result, URBs could never be dequeued and
 * endpoints could never be released.
 */
static void io_watchdog_func(unsigned long _ohci)
{
	struct ohci_hcd	*ohci = (struct ohci_hcd *) _ohci;
	bool		takeback_all_pending = false;
	u32		status;
	u32		head;
	struct ed	*ed;
	struct td	*td, *td_start, *td_next;
	unsigned	frame_no, prev_frame_no = IO_WATCHDOG_OFF;
	unsigned long	flags;

	spin_lock_irqsave(&ohci->lock, flags);

	/*
	 * One way to lose track of completed TDs is if the controller
	 * never writes back the done queue head.  If it hasn't been
	 * written back since the last time this function ran and if it
	 * was non-empty at that time, something is badly wrong with the
	 * hardware.
	 */
	status = ohci_readl(ohci, &ohci->regs->intrstatus);
	if (!(status & OHCI_INTR_WDH) && ohci->wdh_cnt == ohci->prev_wdh_cnt) {
		if (ohci->prev_donehead) {
			ohci_err(ohci, "HcDoneHead not written back; disabled\n");
 died:
			usb_hc_died(ohci_to_hcd(ohci));
			ohci_dump(ohci);
			ohci_shutdown(ohci_to_hcd(ohci));
			goto done;
		} else {
			/* No write back because the done queue was empty */
			takeback_all_pending = true;
		}
	}

	/* Check every ED which might have pending TDs */
	list_for_each_entry(ed, &ohci->eds_in_use, in_use_list) {
		if (ed->pending_td) {
			if (takeback_all_pending ||
					OKAY_TO_TAKEBACK(ohci, ed)) {
				unsigned tmp = hc32_to_cpu(ohci, ed->hwINFO);

				ohci_dbg(ohci, "takeback pending TD for dev %d ep 0x%x\n",
						0x007f & tmp,
						(0x000f & (tmp >> 7)) +
							((tmp & ED_IN) >> 5));
				add_to_done_list(ohci, ed->pending_td);
			}
		}

		/* Starting from the latest pending TD, */
		td = ed->pending_td;

		/* or the last TD on the done list, */
		if (!td) {
			list_for_each_entry(td_next, &ed->td_list, td_list) {
				if (!td_next->next_dl_td)
					break;
				td = td_next;
			}
		}

		/* find the last TD processed by the controller. */
		head = hc32_to_cpu(ohci, ACCESS_ONCE(ed->hwHeadP)) & TD_MASK;
		td_start = td;
		td_next = list_prepare_entry(td, &ed->td_list, td_list);
		list_for_each_entry_continue(td_next, &ed->td_list, td_list) {
			if (head == (u32) td_next->td_dma)
				break;
			td = td_next;	/* head pointer has passed this TD */
		}
		if (td != td_start) {
			/*
			 * In case a WDH cycle is in progress, we will wait
			 * for the next two cycles to complete before assuming
			 * this TD will never get on the done queue.
			 */
			ed->takeback_wdh_cnt = ohci->wdh_cnt + 2;
			ed->pending_td = td;
		}
	}

	ohci_work(ohci);

	if (ohci->rh_state == OHCI_RH_RUNNING) {

		/*
		 * Sometimes a controller just stops working.  We can tell
		 * by checking that the frame counter has advanced since
		 * the last time we ran.
		 *
		 * But be careful: Some controllers violate the spec by
		 * stopping their frame counter when no ports are active.
		 */
		frame_no = ohci_frame_no(ohci);
		if (frame_no == ohci->prev_frame_no) {
			int		active_cnt = 0;
			int		i;
			unsigned	tmp;

			for (i = 0; i < ohci->num_ports; ++i) {
				tmp = roothub_portstatus(ohci, i);
				/* Enabled and not suspended? */
				if ((tmp & RH_PS_PES) && !(tmp & RH_PS_PSS))
					++active_cnt;
			}

			if (active_cnt > 0) {
				ohci_err(ohci, "frame counter not updating; disabled\n");
				goto died;
			}
		}
		if (!list_empty(&ohci->eds_in_use)) {
			prev_frame_no = frame_no;
			ohci->prev_wdh_cnt = ohci->wdh_cnt;
			ohci->prev_donehead = ohci_readl(ohci,
					&ohci->regs->donehead);
			mod_timer(&ohci->io_watchdog,
					jiffies + IO_WATCHDOG_DELAY);
		}
	}

 done:
	ohci->prev_frame_no = prev_frame_no;
	spin_unlock_irqrestore(&ohci->lock, flags);
}

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
		ohci->rh_state = OHCI_RH_HALTED;
		ohci_dbg (ohci, "device removed!\n");
		usb_hc_died(hcd);
		return IRQ_HANDLED;
	}

	/* We only care about interrupts that are enabled */
	ints &= ohci_readl(ohci, &regs->intrenable);

	/* interrupt for some other device? */
	if (ints == 0 || unlikely(ohci->rh_state == OHCI_RH_HALTED))
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
			ohci_err (ohci, "OHCI Unrecoverable Error, disabled\n");
			ohci->rh_state = OHCI_RH_HALTED;
			usb_hc_died(hcd);
		}

		ohci_dump(ohci);
		ohci_usb_reset (ohci);
	}

	if (ints & OHCI_INTR_RHSC) {
		ohci_dbg(ohci, "rhsc\n");
		ohci->next_statechange = jiffies + STATECHANGE_DELAY;
		ohci_writel(ohci, OHCI_INTR_RD | OHCI_INTR_RHSC,
				&regs->intrstatus);

		/* NOTE: Vendors didn't always make the same implementation
		 * choices for RHSC.  Many followed the spec; RHSC triggers
		 * on an edge, like setting and maybe clearing a port status
		 * change bit.  With others it's level-triggered, active
		 * until hub_wq clears all the port status change bits.  We'll
		 * always disable it here and rely on polling until hub_wq
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
		ohci_dbg(ohci, "resume detect\n");
		ohci_writel(ohci, OHCI_INTR_RD, &regs->intrstatus);
		set_bit(HCD_FLAG_POLL_RH, &hcd->flags);
		if (ohci->autostop) {
			spin_lock (&ohci->lock);
			ohci_rh_resume (ohci);
			spin_unlock (&ohci->lock);
		} else
			usb_hcd_resume_root_hub(hcd);
	}

	spin_lock(&ohci->lock);
	if (ints & OHCI_INTR_WDH)
		update_done_list(ohci);

	/* could track INTR_SO to reduce available PCI/... bandwidth */

	/* handle any pending URB/ED unlinks, leaving INTR_SF enabled
	 * when there's still unlinking to be done (next frame).
	 */
	ohci_work(ohci);
	if ((ints & OHCI_INTR_SF) != 0 && !ohci->ed_rm_list
			&& ohci->rh_state == OHCI_RH_RUNNING)
		ohci_writel (ohci, OHCI_INTR_SF, &regs->intrdisable);

	if (ohci->rh_state == OHCI_RH_RUNNING) {
		ohci_writel (ohci, ints, &regs->intrstatus);
		if (ints & OHCI_INTR_WDH)
			++ohci->wdh_cnt;

		ohci_writel (ohci, OHCI_INTR_MIE, &regs->intrenable);
		// flush those writes
		(void) ohci_readl (ohci, &ohci->regs->control);
	}
	spin_unlock(&ohci->lock);

	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/

static void ohci_stop (struct usb_hcd *hcd)
{
	struct ohci_hcd		*ohci = hcd_to_ohci (hcd);

	ohci_dump(ohci);

	if (quirk_nec(ohci))
		flush_work(&ohci->nec_work);
	del_timer_sync(&ohci->io_watchdog);
	ohci->prev_frame_no = IO_WATCHDOG_OFF;

	ohci_writel (ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
	ohci_usb_reset(ohci);
	free_irq(hcd->irq, hcd);
	hcd->irq = 0;

	if (quirk_amdiso(ohci))
		usb_amd_dev_put();

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

#if defined(CONFIG_PM) || defined(CONFIG_USB_PCI)

/* must not be called from interrupt context */
int ohci_restart(struct ohci_hcd *ohci)
{
	int temp;
	int i;
	struct urb_priv *priv;

	ohci_init(ohci);
	spin_lock_irq(&ohci->lock);
	ohci->rh_state = OHCI_RH_HALTED;

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
	ohci_work(ohci);
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
EXPORT_SYMBOL_GPL(ohci_restart);

#endif

#ifdef CONFIG_PM

int ohci_suspend(struct usb_hcd *hcd, bool do_wakeup)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	unsigned long	flags;
	int		rc = 0;

	/* Disable irq emission and mark HW unaccessible. Use
	 * the spinlock to properly synchronize with possible pending
	 * RH suspend or resume activity.
	 */
	spin_lock_irqsave (&ohci->lock, flags);
	ohci_writel(ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
	(void)ohci_readl(ohci, &ohci->regs->intrdisable);

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	spin_unlock_irqrestore (&ohci->lock, flags);

	synchronize_irq(hcd->irq);

	if (do_wakeup && HCD_WAKEUP_PENDING(hcd)) {
		ohci_resume(hcd, false);
		rc = -EBUSY;
	}
	return rc;
}
EXPORT_SYMBOL_GPL(ohci_suspend);


int ohci_resume(struct usb_hcd *hcd, bool hibernated)
{
	struct ohci_hcd		*ohci = hcd_to_ohci(hcd);
	int			port;
	bool			need_reinit = false;

	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	/* Make sure resume from hibernation re-enumerates everything */
	if (hibernated)
		ohci_usb_reset(ohci);

	/* See if the controller is already running or has been reset */
	ohci->hc_control = ohci_readl(ohci, &ohci->regs->control);
	if (ohci->hc_control & (OHCI_CTRL_IR | OHCI_SCHED_ENABLES)) {
		need_reinit = true;
	} else {
		switch (ohci->hc_control & OHCI_CTRL_HCFS) {
		case OHCI_USB_OPER:
		case OHCI_USB_RESET:
			need_reinit = true;
		}
	}

	/* If needed, reinitialize and suspend the root hub */
	if (need_reinit) {
		spin_lock_irq(&ohci->lock);
		ohci_rh_resume(ohci);
		ohci_rh_suspend(ohci, 0);
		spin_unlock_irq(&ohci->lock);
	}

	/* Normally just turn on port power and enable interrupts */
	else {
		ohci_dbg(ohci, "powerup ports\n");
		for (port = 0; port < ohci->num_ports; port++)
			ohci_writel(ohci, RH_PS_PPS,
					&ohci->regs->roothub.portstatus[port]);

		ohci_writel(ohci, OHCI_INTR_MIE, &ohci->regs->intrenable);
		ohci_readl(ohci, &ohci->regs->intrenable);
		msleep(20);
	}

	usb_hcd_resume_root_hub(hcd);

	return 0;
}
EXPORT_SYMBOL_GPL(ohci_resume);

#endif

/*-------------------------------------------------------------------------*/

/*
 * Generic structure: This gets copied for platform drivers so that
 * individual entries can be overridden as needed.
 */

static const struct hc_driver ohci_hc_driver = {
	.description =          hcd_name,
	.product_desc =         "OHCI Host Controller",
	.hcd_priv_size =        sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	*/
	.irq =                  ohci_irq,
	.flags =                HCD_MEMORY | HCD_USB11,

	/*
	* basic lifecycle operations
	*/
	.reset =                ohci_setup,
	.start =                ohci_start,
	.stop =                 ohci_stop,
	.shutdown =             ohci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	*/
	.urb_enqueue =          ohci_urb_enqueue,
	.urb_dequeue =          ohci_urb_dequeue,
	.endpoint_disable =     ohci_endpoint_disable,

	/*
	* scheduling support
	*/
	.get_frame_number =     ohci_get_frame,

	/*
	* root hub support
	*/
	.hub_status_data =      ohci_hub_status_data,
	.hub_control =          ohci_hub_control,
#ifdef CONFIG_PM
	.bus_suspend =          ohci_bus_suspend,
	.bus_resume =           ohci_bus_resume,
#endif
	.start_port_reset =	ohci_start_port_reset,
};

void ohci_init_driver(struct hc_driver *drv,
		const struct ohci_driver_overrides *over)
{
	/* Copy the generic table to drv and then apply the overrides */
	*drv = ohci_hc_driver;

	if (over) {
		drv->product_desc = over->product_desc;
		drv->hcd_priv_size += over->extra_priv_size;
		if (over->reset)
			drv->reset = over->reset;
	}
}
EXPORT_SYMBOL_GPL(ohci_init_driver);

/*-------------------------------------------------------------------------*/

MODULE_AUTHOR (DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE ("GPL");

#if defined(CONFIG_ARCH_SA1100) && defined(CONFIG_SA1111)
#include "ohci-sa1111.c"
#define SA1111_DRIVER		ohci_hcd_sa1111_driver
#endif

#ifdef CONFIG_USB_OHCI_HCD_PPC_OF
#include "ohci-ppc-of.c"
#define OF_PLATFORM_DRIVER	ohci_hcd_ppc_of_driver
#endif

#ifdef CONFIG_PPC_PS3
#include "ohci-ps3.c"
#define PS3_SYSTEM_BUS_DRIVER	ps3_ohci_driver
#endif

#ifdef CONFIG_MFD_SM501
#include "ohci-sm501.c"
#define SM501_OHCI_DRIVER	ohci_hcd_sm501_driver
#endif

#ifdef CONFIG_MFD_TC6393XB
#include "ohci-tmio.c"
#define TMIO_OHCI_DRIVER	ohci_hcd_tmio_driver
#endif

#ifdef CONFIG_TILE_USB
#include "ohci-tilegx.c"
#define PLATFORM_DRIVER		ohci_hcd_tilegx_driver
#endif

static int __init ohci_hcd_mod_init(void)
{
	int retval = 0;

	if (usb_disabled())
		return -ENODEV;

	printk(KERN_INFO "%s: " DRIVER_DESC "\n", hcd_name);
	pr_debug ("%s: block sizes: ed %zd td %zd\n", hcd_name,
		sizeof (struct ed), sizeof (struct td));
	set_bit(USB_OHCI_LOADED, &usb_hcds_loaded);

	ohci_debug_root = debugfs_create_dir("ohci", usb_debug_root);
	if (!ohci_debug_root) {
		retval = -ENOENT;
		goto error_debug;
	}

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

#ifdef OF_PLATFORM_DRIVER
	retval = platform_driver_register(&OF_PLATFORM_DRIVER);
	if (retval < 0)
		goto error_of_platform;
#endif

#ifdef SA1111_DRIVER
	retval = sa1111_driver_register(&SA1111_DRIVER);
	if (retval < 0)
		goto error_sa1111;
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
#ifdef SA1111_DRIVER
	sa1111_driver_unregister(&SA1111_DRIVER);
 error_sa1111:
#endif
#ifdef OF_PLATFORM_DRIVER
	platform_driver_unregister(&OF_PLATFORM_DRIVER);
 error_of_platform:
#endif
#ifdef PLATFORM_DRIVER
	platform_driver_unregister(&PLATFORM_DRIVER);
 error_platform:
#endif
#ifdef PS3_SYSTEM_BUS_DRIVER
	ps3_ohci_driver_unregister(&PS3_SYSTEM_BUS_DRIVER);
 error_ps3:
#endif
	debugfs_remove(ohci_debug_root);
	ohci_debug_root = NULL;
 error_debug:

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
#ifdef SA1111_DRIVER
	sa1111_driver_unregister(&SA1111_DRIVER);
#endif
#ifdef OF_PLATFORM_DRIVER
	platform_driver_unregister(&OF_PLATFORM_DRIVER);
#endif
#ifdef PLATFORM_DRIVER
	platform_driver_unregister(&PLATFORM_DRIVER);
#endif
#ifdef PS3_SYSTEM_BUS_DRIVER
	ps3_ohci_driver_unregister(&PS3_SYSTEM_BUS_DRIVER);
#endif
	debugfs_remove(ohci_debug_root);
	clear_bit(USB_OHCI_LOADED, &usb_hcds_loaded);
}
module_exit(ohci_hcd_mod_exit);

