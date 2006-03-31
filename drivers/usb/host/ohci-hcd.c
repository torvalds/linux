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
 * History:
 * 
 * 2004/03/24 LH7A404 support (Durgesh Pattamatta & Marc Singer)
 * 2004/02/04 use generic dma_* functions instead of pci_* (dsaxena@plexity.net)
 * 2003/02/24 show registers in sysfs (Kevin Brosius)
 *
 * 2002/09/03 get rid of ed hashtables, rework periodic scheduling and
 * 	bandwidth accounting; if debugging, show schedules in driverfs
 * 2002/07/19 fixes to management of ED and schedule state.
 * 2002/06/09 SA-1111 support (Christopher Hoover)
 * 2002/06/01 remember frame when HC won't see EDs any more; use that info
 *	to fix urb unlink races caused by interrupt latency assumptions;
 *	minor ED field and function naming updates
 * 2002/01/18 package as a patch for 2.5.3; this should match the
 *	2.4.17 kernel modulo some bugs being fixed.
 *
 * 2001/10/18 merge pmac cleanup (Benjamin Herrenschmidt) and bugfixes
 *	from post-2.4.5 patches.
 * 2001/09/20 URB_ZERO_PACKET support; hcca_dma portability, OPTi warning
 * 2001/09/07 match PCI PM changes, errnos from Linus' tree
 * 2001/05/05 fork 2.4.5 version into "hcd" framework, cleanup, simplify;
 *	pbook pci quirks gone (please fix pbook pci sw!) (db)
 *
 * 2001/04/08 Identify version on module load (gb)
 * 2001/03/24 td/ed hashing to remove bus_to_virt (Steve Longerbeam);
 	pci_map_single (db)
 * 2001/03/21 td and dev/ed allocation uses new pci_pool API (db)
 * 2001/03/07 hcca allocation uses pci_alloc_consistent (Steve Longerbeam)
 *
 * 2000/09/26 fixed races in removing the private portion of the urb
 * 2000/09/07 disable bulk and control lists when unlinking the last
 *	endpoint descriptor in order to avoid unrecoverable errors on
 *	the Lucent chips. (rwc@sgi)
 * 2000/08/29 use bandwidth claiming hooks (thanks Randy!), fix some
 *	urb unlink probs, indentation fixes
 * 2000/08/11 various oops fixes mostly affecting iso and cleanup from
 *	device unplugs.
 * 2000/06/28 use PCI hotplug framework, for better power management
 *	and for Cardbus support (David Brownell)
 * 2000/earlier:  fixes for NEC/Lucent chips; suspend/resume handling
 *	when the controller loses power; handle UE; cleanup; ...
 *
 * v5.2 1999/12/07 URB 3rd preview, 
 * v5.1 1999/11/30 URB 2nd preview, cpia, (usb-scsi)
 * v5.0 1999/11/22 URB Technical preview, Paul Mackerras powerbook susp/resume 
 * 	i386: HUB, Keyboard, Mouse, Printer 
 *
 * v4.3 1999/10/27 multiple HCs, bulk_request
 * v4.2 1999/09/05 ISO API alpha, new dev alloc, neg Error-codes
 * v4.1 1999/08/27 Randy Dunlap's - ISO API first impl.
 * v4.0 1999/08/18 
 * v3.0 1999/06/25 
 * v2.1 1999/05/09  code clean up
 * v2.0 1999/05/04 
 * v1.0 1999/04/27 initial release
 *
 * This file is licenced under the GPL.
 */
 
#include <linux/config.h>
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
#include <linux/usb_otg.h>
#include <linux/dma-mapping.h> 
#include <linux/dmapool.h>
#include <linux/reboot.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>
#include <asm/byteorder.h>

#include "../core/hcd.h"

#define DRIVER_VERSION "2005 April 22"
#define DRIVER_AUTHOR "Roman Weissgaerber, David Brownell"
#define DRIVER_DESC "USB 1.1 'Open' Host Controller (OHCI) Driver"

/*-------------------------------------------------------------------------*/

#undef OHCI_VERBOSE_DEBUG	/* not always helpful */

/* For initializing controller (mask in an HCFS mode too) */
#define	OHCI_CONTROL_INIT 	OHCI_CTRL_CBSR
#define	OHCI_INTR_INIT \
	(OHCI_INTR_MIE | OHCI_INTR_UE | OHCI_INTR_RD | OHCI_INTR_WDH)

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

#include "ohci.h"

static void ohci_dump (struct ohci_hcd *ohci, int verbose);
static int ohci_init (struct ohci_hcd *ohci);
static void ohci_stop (struct usb_hcd *hcd);
static int ohci_reboot (struct notifier_block *, unsigned long , void *);

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
		finish_urb (ohci, urb, NULL);
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
			finish_urb (ohci, urb, NULL);
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
		finish_unlinks (ohci, 0, NULL);
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

/* reboot notifier forcibly disables IRQs and DMA, helping kexec and
 * other cases where the next software may expect clean state from the
 * "firmware".  this is bus-neutral, unlike shutdown() methods.
 */
static int
ohci_reboot (struct notifier_block *block, unsigned long code, void *null)
{
	struct ohci_hcd *ohci;

	ohci = container_of (block, struct ohci_hcd, reboot_notifier);
	ohci_writel (ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
	ohci_usb_reset (ohci);
	/* flush the writes */
	(void) ohci_readl (ohci, &ohci->regs->control);
	return 0;
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
	ohci->next_statechange = jiffies;

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
		register_reboot_notifier (&ohci->reboot_notifier);
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
	ohci_dbg (ohci, "resetting from state '%s', control = 0x%x\n",
			hcfs2string (ohci->hc_control & OHCI_CTRL_HCFS),
			ohci_readl (ohci, &ohci->regs->control));
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

 	/* start controller operations */
	ohci->hc_control &= OHCI_CTRL_RWC;
 	ohci->hc_control |= OHCI_CONTROL_INIT | OHCI_USB_OPER;
 	ohci_writel (ohci, ohci->hc_control, &ohci->regs->control);
	hcd->state = HC_STATE_RUNNING;

	/* wake on ConnectStatusChange, matching external hubs */
	ohci_writel (ohci, RH_HS_DRWE, &ohci->regs->roothub.status);

	/* Choose the interrupts we care about now, others later on demand */
	mask = OHCI_INTR_INIT;
	ohci_writel (ohci, mask, &ohci->regs->intrstatus);
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

	spin_unlock_irq (&ohci->lock);

	// POTPGT delay is bits 24-31, in 2 ms units.
	mdelay ((temp >> 23) & 0x1fe);
	hcd->state = HC_STATE_RUNNING;

	ohci_dump (ohci, 1);

	return 0;
}

/*-------------------------------------------------------------------------*/

/* an interrupt happens */

static irqreturn_t ohci_irq (struct usb_hcd *hcd, struct pt_regs *ptregs)
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

	if (ints & OHCI_INTR_RD) {
		ohci_vdbg (ohci, "resume detect\n");
		ohci_writel (ohci, OHCI_INTR_RD, &regs->intrstatus);
		if (hcd->state != HC_STATE_QUIESCING)
			usb_hcd_resume_root_hub(hcd);
	}

	if (ints & OHCI_INTR_WDH) {
		if (HC_IS_RUNNING(hcd->state))
			ohci_writel (ohci, OHCI_INTR_WDH, &regs->intrdisable);	
		spin_lock (&ohci->lock);
		dl_done_list (ohci, ptregs);
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
		finish_unlinks (ohci, ohci_frame_no(ohci), ptregs);
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

	ohci_dbg (ohci, "stop %s controller (state 0x%02x)\n",
		hcfs2string (ohci->hc_control & OHCI_CTRL_HCFS),
		hcd->state);
	ohci_dump (ohci, 1);

	flush_scheduled_work();

	ohci_usb_reset (ohci);
	ohci_writel (ohci, OHCI_INTR_MIE, &ohci->regs->intrdisable);
	
	remove_debug_files (ohci);
	unregister_reboot_notifier (&ohci->reboot_notifier);
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
	finish_unlinks (ohci, 0, NULL);
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
				&ohci->regs->roothub.portstatus [temp]);
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
#endif

#ifdef CONFIG_SA1111
#include "ohci-sa1111.c"
#endif

#ifdef CONFIG_ARCH_S3C2410
#include "ohci-s3c2410.c"
#endif

#ifdef CONFIG_ARCH_OMAP
#include "ohci-omap.c"
#endif

#ifdef CONFIG_ARCH_LH7A404
#include "ohci-lh7a404.c"
#endif

#ifdef CONFIG_PXA27x
#include "ohci-pxa27x.c"
#endif

#ifdef CONFIG_SOC_AU1X00
#include "ohci-au1xxx.c"
#endif

#ifdef CONFIG_USB_OHCI_HCD_PPC_SOC
#include "ohci-ppc-soc.c"
#endif

#ifdef CONFIG_ARCH_AT91RM9200
#include "ohci-at91.c"
#endif

#if !(defined(CONFIG_PCI) \
      || defined(CONFIG_SA1111) \
      || defined(CONFIG_ARCH_S3C2410) \
      || defined(CONFIG_ARCH_OMAP) \
      || defined (CONFIG_ARCH_LH7A404) \
      || defined (CONFIG_PXA27x) \
      || defined (CONFIG_SOC_AU1X00) \
      || defined (CONFIG_USB_OHCI_HCD_PPC_SOC) \
      || defined (CONFIG_ARCH_AT91RM9200) \
	)
#error "missing bus glue for ohci-hcd"
#endif
