/*
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dmapool.h>
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
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/usb.h>
#include <linux/moduleparam.h>
#include <linux/dma-mapping.h>

#include "../core/hcd.h"

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>


/*-------------------------------------------------------------------------*/

/*
 * EHCI hc_driver implementation ... experimental, incomplete.
 * Based on the final 1.0 register interface specification.
 *
 * USB 2.0 shows up in upcoming www.pcmcia.org technology.
 * First was PCMCIA, like ISA; then CardBus, which is PCI.
 * Next comes "CardBay", using USB 2.0 signals.
 *
 * Contains additional contributions by Brad Hards, Rory Bolt, and others.
 * Special thanks to Intel and VIA for providing host controllers to
 * test this driver on, and Cypress (including In-System Design) for
 * providing early devices for those host controllers to talk to!
 *
 * HISTORY:
 *
 * 2004-05-10 Root hub and PCI suspend/resume support; remote wakeup. (db)
 * 2004-02-24 Replace pci_* with generic dma_* API calls (dsaxena@plexity.net)
 * 2003-12-29 Rewritten high speed iso transfer support (by Michal Sojka,
 *	<sojkam@centrum.cz>, updates by DB).
 *
 * 2002-11-29	Correct handling for hw async_next register.
 * 2002-08-06	Handling for bulk and interrupt transfers is mostly shared;
 *	only scheduling is different, no arbitrary limitations.
 * 2002-07-25	Sanity check PCI reads, mostly for better cardbus support,
 * 	clean up HC run state handshaking.
 * 2002-05-24	Preliminary FS/LS interrupts, using scheduling shortcuts
 * 2002-05-11	Clear TT errors for FS/LS ctrl/bulk.  Fill in some other
 *	missing pieces:  enabling 64bit dma, handoff from BIOS/SMM.
 * 2002-05-07	Some error path cleanups to report better errors; wmb();
 *	use non-CVS version id; better iso bandwidth claim.
 * 2002-04-19	Control/bulk/interrupt submit no longer uses giveback() on
 *	errors in submit path.  Bugfixes to interrupt scheduling/processing.
 * 2002-03-05	Initial high-speed ISO support; reduce ITD memory; shift
 *	more checking to generic hcd framework (db).  Make it work with
 *	Philips EHCI; reduce PCI traffic; shorten IRQ path (Rory Bolt).
 * 2002-01-14	Minor cleanup; version synch.
 * 2002-01-08	Fix roothub handoff of FS/LS to companion controllers.
 * 2002-01-04	Control/Bulk queuing behaves.
 *
 * 2001-12-12	Initial patch version for Linux 2.5.1 kernel.
 * 2001-June	Works with usb-storage and NEC EHCI on 2.4
 */

#define DRIVER_VERSION "10 Dec 2004"
#define DRIVER_AUTHOR "David Brownell"
#define DRIVER_DESC "USB 2.0 'Enhanced' Host Controller (EHCI) Driver"

static const char	hcd_name [] = "ehci_hcd";


#undef EHCI_VERBOSE_DEBUG
#undef EHCI_URB_TRACE

#ifdef DEBUG
#define EHCI_STATS
#endif

/* magic numbers that can affect system performance */
#define	EHCI_TUNE_CERR		3	/* 0-3 qtd retries; 0 == don't stop */
#define	EHCI_TUNE_RL_HS		4	/* nak throttle; see 4.9 */
#define	EHCI_TUNE_RL_TT		0
#define	EHCI_TUNE_MULT_HS	1	/* 1-3 transactions/uframe; 4.10.3 */
#define	EHCI_TUNE_MULT_TT	1
#define	EHCI_TUNE_FLS		2	/* (small) 256 frame schedule */

#define EHCI_IAA_JIFFIES	(HZ/100)	/* arbitrary; ~10 msec */
#define EHCI_IO_JIFFIES		(HZ/10)		/* io watchdog > irq_thresh */
#define EHCI_ASYNC_JIFFIES	(HZ/20)		/* async idle timeout */
#define EHCI_SHRINK_JIFFIES	(HZ/200)	/* async qh unlink delay */

/* Initial IRQ latency:  faster than hw default */
static int log2_irq_thresh = 0;		// 0 to 6
module_param (log2_irq_thresh, int, S_IRUGO);
MODULE_PARM_DESC (log2_irq_thresh, "log2 IRQ latency, 1-64 microframes");

/* initial park setting:  slower than hw default */
static unsigned park = 0;
module_param (park, uint, S_IRUGO);
MODULE_PARM_DESC (park, "park setting; 1-3 back-to-back async packets");

#define	INTR_MASK (STS_IAA | STS_FATAL | STS_PCD | STS_ERR | STS_INT)

/*-------------------------------------------------------------------------*/

#include "ehci.h"
#include "ehci-dbg.c"

/*-------------------------------------------------------------------------*/

/*
 * handshake - spin reading hc until handshake completes or fails
 * @ptr: address of hc register to be read
 * @mask: bits to look at in result of read
 * @done: value of those bits when handshake succeeds
 * @usec: timeout in microseconds
 *
 * Returns negative errno, or zero on success
 *
 * Success happens when the "mask" bits have the specified value (hardware
 * handshake done).  There are two failure modes:  "usec" have passed (major
 * hardware flakeout), or the register reads as all-ones (hardware removed).
 *
 * That last failure should_only happen in cases like physical cardbus eject
 * before driver shutdown. But it also seems to be caused by bugs in cardbus
 * bridge shutdown:  shutting down the bridge before the devices using it.
 */
static int handshake (void __iomem *ptr, u32 mask, u32 done, int usec)
{
	u32	result;

	do {
		result = readl (ptr);
		if (result == ~(u32)0)		/* card removed */
			return -ENODEV;
		result &= mask;
		if (result == done)
			return 0;
		udelay (1);
		usec--;
	} while (usec > 0);
	return -ETIMEDOUT;
}

/* force HC to halt state from unknown (EHCI spec section 2.3) */
static int ehci_halt (struct ehci_hcd *ehci)
{
	u32	temp = readl (&ehci->regs->status);

	/* disable any irqs left enabled by previous code */
	writel (0, &ehci->regs->intr_enable);

	if ((temp & STS_HALT) != 0)
		return 0;

	temp = readl (&ehci->regs->command);
	temp &= ~CMD_RUN;
	writel (temp, &ehci->regs->command);
	return handshake (&ehci->regs->status, STS_HALT, STS_HALT, 16 * 125);
}

/* put TDI/ARC silicon into EHCI mode */
static void tdi_reset (struct ehci_hcd *ehci)
{
	u32 __iomem	*reg_ptr;
	u32		tmp;

	reg_ptr = (u32 __iomem *)(((u8 __iomem *)ehci->regs) + 0x68);
	tmp = readl (reg_ptr);
	tmp |= 0x3;
	writel (tmp, reg_ptr);
}

/* reset a non-running (STS_HALT == 1) controller */
static int ehci_reset (struct ehci_hcd *ehci)
{
	int	retval;
	u32	command = readl (&ehci->regs->command);

	command |= CMD_RESET;
	dbg_cmd (ehci, "reset", command);
	writel (command, &ehci->regs->command);
	ehci_to_hcd(ehci)->state = HC_STATE_HALT;
	ehci->next_statechange = jiffies;
	retval = handshake (&ehci->regs->command, CMD_RESET, 0, 250 * 1000);

	if (retval)
		return retval;

	if (ehci_is_TDI(ehci))
		tdi_reset (ehci);

	return retval;
}

/* idle the controller (from running) */
static void ehci_quiesce (struct ehci_hcd *ehci)
{
	u32	temp;

#ifdef DEBUG
	if (!HC_IS_RUNNING (ehci_to_hcd(ehci)->state))
		BUG ();
#endif

	/* wait for any schedule enables/disables to take effect */
	temp = readl (&ehci->regs->command) << 10;
	temp &= STS_ASS | STS_PSS;
	if (handshake (&ehci->regs->status, STS_ASS | STS_PSS,
				temp, 16 * 125) != 0) {
		ehci_to_hcd(ehci)->state = HC_STATE_HALT;
		return;
	}

	/* then disable anything that's still active */
	temp = readl (&ehci->regs->command);
	temp &= ~(CMD_ASE | CMD_IAAD | CMD_PSE);
	writel (temp, &ehci->regs->command);

	/* hardware can take 16 microframes to turn off ... */
	if (handshake (&ehci->regs->status, STS_ASS | STS_PSS,
				0, 16 * 125) != 0) {
		ehci_to_hcd(ehci)->state = HC_STATE_HALT;
		return;
	}
}

/*-------------------------------------------------------------------------*/

static void ehci_work(struct ehci_hcd *ehci, struct pt_regs *regs);

#include "ehci-hub.c"
#include "ehci-mem.c"
#include "ehci-q.c"
#include "ehci-sched.c"

/*-------------------------------------------------------------------------*/

static void ehci_watchdog (unsigned long param)
{
	struct ehci_hcd		*ehci = (struct ehci_hcd *) param;
	unsigned long		flags;

	spin_lock_irqsave (&ehci->lock, flags);

	/* lost IAA irqs wedge things badly; seen with a vt8235 */
	if (ehci->reclaim) {
		u32		status = readl (&ehci->regs->status);

		if (status & STS_IAA) {
			ehci_vdbg (ehci, "lost IAA\n");
			COUNT (ehci->stats.lost_iaa);
			writel (STS_IAA, &ehci->regs->status);
			ehci->reclaim_ready = 1;
		}
	}

 	/* stop async processing after it's idled a bit */
	if (test_bit (TIMER_ASYNC_OFF, &ehci->actions))
 		start_unlink_async (ehci, ehci->async);

	/* ehci could run by timer, without IRQs ... */
	ehci_work (ehci, NULL);

	spin_unlock_irqrestore (&ehci->lock, flags);
}

/* Reboot notifiers kick in for silicon on any bus (not just pci, etc).
 * This forcibly disables dma and IRQs, helping kexec and other cases
 * where the next system software may expect clean state.
 */
static int
ehci_reboot (struct notifier_block *self, unsigned long code, void *null)
{
	struct ehci_hcd		*ehci;

	ehci = container_of (self, struct ehci_hcd, reboot_notifier);
	(void) ehci_halt (ehci);

	/* make BIOS/etc use companion controller during reboot */
	writel (0, &ehci->regs->configured_flag);
	return 0;
}

static void ehci_port_power (struct ehci_hcd *ehci, int is_on)
{
	unsigned port;

	if (!HCS_PPC (ehci->hcs_params))
		return;

	ehci_dbg (ehci, "...power%s ports...\n", is_on ? "up" : "down");
	for (port = HCS_N_PORTS (ehci->hcs_params); port > 0; )
		(void) ehci_hub_control(ehci_to_hcd(ehci),
				is_on ? SetPortFeature : ClearPortFeature,
				USB_PORT_FEAT_POWER,
				port--, NULL, 0);
	msleep(20);
}

/*-------------------------------------------------------------------------*/

/*
 * ehci_work is called from some interrupts, timers, and so on.
 * it calls driver completion functions, after dropping ehci->lock.
 */
static void ehci_work (struct ehci_hcd *ehci, struct pt_regs *regs)
{
	timer_action_done (ehci, TIMER_IO_WATCHDOG);
	if (ehci->reclaim_ready)
		end_unlink_async (ehci, regs);

	/* another CPU may drop ehci->lock during a schedule scan while
	 * it reports urb completions.  this flag guards against bogus
	 * attempts at re-entrant schedule scanning.
	 */
	if (ehci->scanning)
		return;
	ehci->scanning = 1;
	scan_async (ehci, regs);
	if (ehci->next_uframe != -1)
		scan_periodic (ehci, regs);
	ehci->scanning = 0;

	/* the IO watchdog guards against hardware or driver bugs that
	 * misplace IRQs, and should let us run completely without IRQs.
	 * such lossage has been observed on both VT6202 and VT8235.
	 */
	if (HC_IS_RUNNING (ehci_to_hcd(ehci)->state) &&
			(ehci->async->qh_next.ptr != NULL ||
			 ehci->periodic_sched != 0))
		timer_action (ehci, TIMER_IO_WATCHDOG);
}

static void ehci_stop (struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);

	ehci_dbg (ehci, "stop\n");

	/* Turn off port power on all root hub ports. */
	ehci_port_power (ehci, 0);

	/* no more interrupts ... */
	del_timer_sync (&ehci->watchdog);

	spin_lock_irq(&ehci->lock);
	if (HC_IS_RUNNING (hcd->state))
		ehci_quiesce (ehci);

	ehci_reset (ehci);
	writel (0, &ehci->regs->intr_enable);
	spin_unlock_irq(&ehci->lock);

	/* let companion controllers work when we aren't */
	writel (0, &ehci->regs->configured_flag);
	unregister_reboot_notifier (&ehci->reboot_notifier);

	remove_debug_files (ehci);

	/* root hub is shut down separately (first, when possible) */
	spin_lock_irq (&ehci->lock);
	if (ehci->async)
		ehci_work (ehci, NULL);
	spin_unlock_irq (&ehci->lock);
	ehci_mem_cleanup (ehci);

#ifdef	EHCI_STATS
	ehci_dbg (ehci, "irq normal %ld err %ld reclaim %ld (lost %ld)\n",
		ehci->stats.normal, ehci->stats.error, ehci->stats.reclaim,
		ehci->stats.lost_iaa);
	ehci_dbg (ehci, "complete %ld unlink %ld\n",
		ehci->stats.complete, ehci->stats.unlink);
#endif

	dbg_status (ehci, "ehci_stop completed", readl (&ehci->regs->status));
}

/* one-time init, only for memory state */
static int ehci_init(struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci(hcd);
	u32			temp;
	int			retval;
	u32			hcc_params;

	spin_lock_init(&ehci->lock);

	init_timer(&ehci->watchdog);
	ehci->watchdog.function = ehci_watchdog;
	ehci->watchdog.data = (unsigned long) ehci;

	/*
	 * hw default: 1K periodic list heads, one per frame.
	 * periodic_size can shrink by USBCMD update if hcc_params allows.
	 */
	ehci->periodic_size = DEFAULT_I_TDPS;
	if ((retval = ehci_mem_init(ehci, GFP_KERNEL)) < 0)
		return retval;

	/* controllers may cache some of the periodic schedule ... */
	hcc_params = readl(&ehci->caps->hcc_params);
	if (HCC_ISOC_CACHE(hcc_params)) 	// full frame cache
		ehci->i_thresh = 8;
	else					// N microframes cached
		ehci->i_thresh = 2 + HCC_ISOC_THRES(hcc_params);

	ehci->reclaim = NULL;
	ehci->reclaim_ready = 0;
	ehci->next_uframe = -1;

	/*
	 * dedicate a qh for the async ring head, since we couldn't unlink
	 * a 'real' qh without stopping the async schedule [4.8].  use it
	 * as the 'reclamation list head' too.
	 * its dummy is used in hw_alt_next of many tds, to prevent the qh
	 * from automatically advancing to the next td after short reads.
	 */
	ehci->async->qh_next.qh = NULL;
	ehci->async->hw_next = QH_NEXT(ehci->async->qh_dma);
	ehci->async->hw_info1 = cpu_to_le32(QH_HEAD);
	ehci->async->hw_token = cpu_to_le32(QTD_STS_HALT);
	ehci->async->hw_qtd_next = EHCI_LIST_END;
	ehci->async->qh_state = QH_STATE_LINKED;
	ehci->async->hw_alt_next = QTD_NEXT(ehci->async->dummy->qtd_dma);

	/* clear interrupt enables, set irq latency */
	if (log2_irq_thresh < 0 || log2_irq_thresh > 6)
		log2_irq_thresh = 0;
	temp = 1 << (16 + log2_irq_thresh);
	if (HCC_CANPARK(hcc_params)) {
		/* HW default park == 3, on hardware that supports it (like
		 * NVidia and ALI silicon), maximizes throughput on the async
		 * schedule by avoiding QH fetches between transfers.
		 *
		 * With fast usb storage devices and NForce2, "park" seems to
		 * make problems:  throughput reduction (!), data errors...
		 */
		if (park) {
			park = min(park, (unsigned) 3);
			temp |= CMD_PARK;
			temp |= park << 8;
		}
		ehci_dbg(ehci, "park %d\n", park);
	}
	if (HCC_PGM_FRAMELISTLEN(hcc_params)) {
		/* periodic schedule size can be smaller than default */
		temp &= ~(3 << 2);
		temp |= (EHCI_TUNE_FLS << 2);
		switch (EHCI_TUNE_FLS) {
		case 0: ehci->periodic_size = 1024; break;
		case 1: ehci->periodic_size = 512; break;
		case 2: ehci->periodic_size = 256; break;
		default:	BUG();
		}
	}
	ehci->command = temp;

	ehci->reboot_notifier.notifier_call = ehci_reboot;
	register_reboot_notifier(&ehci->reboot_notifier);

	return 0;
}

/* start HC running; it's halted, ehci_init() has been run (once) */
static int ehci_run (struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	int			retval;
	u32			temp;
	u32			hcc_params;

	/* EHCI spec section 4.1 */
	if ((retval = ehci_reset(ehci)) != 0) {
		unregister_reboot_notifier(&ehci->reboot_notifier);
		ehci_mem_cleanup(ehci);
		return retval;
	}
	writel(ehci->periodic_dma, &ehci->regs->frame_list);
	writel((u32)ehci->async->qh_dma, &ehci->regs->async_next);

	/*
	 * hcc_params controls whether ehci->regs->segment must (!!!)
	 * be used; it constrains QH/ITD/SITD and QTD locations.
	 * pci_pool consistent memory always uses segment zero.
	 * streaming mappings for I/O buffers, like pci_map_single(),
	 * can return segments above 4GB, if the device allows.
	 *
	 * NOTE:  the dma mask is visible through dma_supported(), so
	 * drivers can pass this info along ... like NETIF_F_HIGHDMA,
	 * Scsi_Host.highmem_io, and so forth.  It's readonly to all
	 * host side drivers though.
	 */
	hcc_params = readl(&ehci->caps->hcc_params);
	if (HCC_64BIT_ADDR(hcc_params)) {
		writel(0, &ehci->regs->segment);
#if 0
// this is deeply broken on almost all architectures
		if (!dma_set_mask(hcd->self.controller, DMA_64BIT_MASK))
			ehci_info(ehci, "enabled 64bit DMA\n");
#endif
	}


	// Philips, Intel, and maybe others need CMD_RUN before the
	// root hub will detect new devices (why?); NEC doesn't
	ehci->command &= ~(CMD_LRESET|CMD_IAAD|CMD_PSE|CMD_ASE|CMD_RESET);
	ehci->command |= CMD_RUN;
	writel (ehci->command, &ehci->regs->command);
	dbg_cmd (ehci, "init", ehci->command);

	/*
	 * Start, enabling full USB 2.0 functionality ... usb 1.1 devices
	 * are explicitly handed to companion controller(s), so no TT is
	 * involved with the root hub.  (Except where one is integrated,
	 * and there's no companion controller unless maybe for USB OTG.)
	 */
	hcd->state = HC_STATE_RUNNING;
	writel (FLAG_CF, &ehci->regs->configured_flag);
	readl (&ehci->regs->command);	/* unblock posted writes */

	temp = HC_VERSION(readl (&ehci->caps->hc_capbase));
	ehci_info (ehci,
		"USB %x.%x started, EHCI %x.%02x, driver %s\n",
		((ehci->sbrn & 0xf0)>>4), (ehci->sbrn & 0x0f),
		temp >> 8, temp & 0xff, DRIVER_VERSION);

	writel (INTR_MASK, &ehci->regs->intr_enable); /* Turn On Interrupts */

	/* GRR this is run-once init(), being done every time the HC starts.
	 * So long as they're part of class devices, we can't do it init()
	 * since the class device isn't created that early.
	 */
	create_debug_files(ehci);

	return 0;
}

/*-------------------------------------------------------------------------*/

static irqreturn_t ehci_irq (struct usb_hcd *hcd, struct pt_regs *regs)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	u32			status;
	int			bh;

	spin_lock (&ehci->lock);

	status = readl (&ehci->regs->status);

	/* e.g. cardbus physical eject */
	if (status == ~(u32) 0) {
		ehci_dbg (ehci, "device removed\n");
		goto dead;
	}

	status &= INTR_MASK;
	if (!status) {			/* irq sharing? */
		spin_unlock(&ehci->lock);
		return IRQ_NONE;
	}

	/* clear (just) interrupts */
	writel (status, &ehci->regs->status);
	readl (&ehci->regs->command);	/* unblock posted write */
	bh = 0;

#ifdef	EHCI_VERBOSE_DEBUG
	/* unrequested/ignored: Frame List Rollover */
	dbg_status (ehci, "irq", status);
#endif

	/* INT, ERR, and IAA interrupt rates can be throttled */

	/* normal [4.15.1.2] or error [4.15.1.1] completion */
	if (likely ((status & (STS_INT|STS_ERR)) != 0)) {
		if (likely ((status & STS_ERR) == 0))
			COUNT (ehci->stats.normal);
		else
			COUNT (ehci->stats.error);
		bh = 1;
	}

	/* complete the unlinking of some qh [4.15.2.3] */
	if (status & STS_IAA) {
		COUNT (ehci->stats.reclaim);
		ehci->reclaim_ready = 1;
		bh = 1;
	}

	/* remote wakeup [4.3.1] */
	if (status & STS_PCD) {
		unsigned	i = HCS_N_PORTS (ehci->hcs_params);

		/* resume root hub? */
		status = readl (&ehci->regs->command);
		if (!(status & CMD_RUN))
			writel (status | CMD_RUN, &ehci->regs->command);

		while (i--) {
			status = readl (&ehci->regs->port_status [i]);
			if (status & PORT_OWNER)
				continue;
			if (!(status & PORT_RESUME)
					|| ehci->reset_done [i] != 0)
				continue;

			/* start 20 msec resume signaling from this port,
			 * and make khubd collect PORT_STAT_C_SUSPEND to
			 * stop that signaling.
			 */
			ehci->reset_done [i] = jiffies + msecs_to_jiffies (20);
			ehci_dbg (ehci, "port %d remote wakeup\n", i + 1);
			usb_hcd_resume_root_hub(hcd);
		}
	}

	/* PCI errors [4.15.2.4] */
	if (unlikely ((status & STS_FATAL) != 0)) {
		/* bogus "fatal" IRQs appear on some chips... why?  */
		status = readl (&ehci->regs->status);
		dbg_cmd (ehci, "fatal", readl (&ehci->regs->command));
		dbg_status (ehci, "fatal", status);
		if (status & STS_HALT) {
			ehci_err (ehci, "fatal error\n");
dead:
			ehci_reset (ehci);
			writel (0, &ehci->regs->configured_flag);
			/* generic layer kills/unlinks all urbs, then
			 * uses ehci_stop to clean up the rest
			 */
			bh = 1;
		}
	}

	if (bh)
		ehci_work (ehci, regs);
	spin_unlock (&ehci->lock);
	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/

/*
 * non-error returns are a promise to giveback() the urb later
 * we drop ownership so next owner (or urb unlink) can get it
 *
 * urb + dev is in hcd.self.controller.urb_list
 * we're queueing TDs onto software and hardware lists
 *
 * hcd-specific init for hcpriv hasn't been done yet
 *
 * NOTE:  control, bulk, and interrupt share the same code to append TDs
 * to a (possibly active) QH, and the same QH scanning code.
 */
static int ehci_urb_enqueue (
	struct usb_hcd	*hcd,
	struct usb_host_endpoint *ep,
	struct urb	*urb,
	gfp_t		mem_flags
) {
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	struct list_head	qtd_list;

	INIT_LIST_HEAD (&qtd_list);

	switch (usb_pipetype (urb->pipe)) {
	// case PIPE_CONTROL:
	// case PIPE_BULK:
	default:
		if (!qh_urb_transaction (ehci, urb, &qtd_list, mem_flags))
			return -ENOMEM;
		return submit_async (ehci, ep, urb, &qtd_list, mem_flags);

	case PIPE_INTERRUPT:
		if (!qh_urb_transaction (ehci, urb, &qtd_list, mem_flags))
			return -ENOMEM;
		return intr_submit (ehci, ep, urb, &qtd_list, mem_flags);

	case PIPE_ISOCHRONOUS:
		if (urb->dev->speed == USB_SPEED_HIGH)
			return itd_submit (ehci, urb, mem_flags);
		else
			return sitd_submit (ehci, urb, mem_flags);
	}
}

static void unlink_async (struct ehci_hcd *ehci, struct ehci_qh *qh)
{
	/* if we need to use IAA and it's busy, defer */
	if (qh->qh_state == QH_STATE_LINKED
			&& ehci->reclaim
			&& HC_IS_RUNNING (ehci_to_hcd(ehci)->state)) {
		struct ehci_qh		*last;

		for (last = ehci->reclaim;
				last->reclaim;
				last = last->reclaim)
			continue;
		qh->qh_state = QH_STATE_UNLINK_WAIT;
		last->reclaim = qh;

	/* bypass IAA if the hc can't care */
	} else if (!HC_IS_RUNNING (ehci_to_hcd(ehci)->state) && ehci->reclaim)
		end_unlink_async (ehci, NULL);

	/* something else might have unlinked the qh by now */
	if (qh->qh_state == QH_STATE_LINKED)
		start_unlink_async (ehci, qh);
}

/* remove from hardware lists
 * completions normally happen asynchronously
 */

static int ehci_urb_dequeue (struct usb_hcd *hcd, struct urb *urb)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	struct ehci_qh		*qh;
	unsigned long		flags;

	spin_lock_irqsave (&ehci->lock, flags);
	switch (usb_pipetype (urb->pipe)) {
	// case PIPE_CONTROL:
	// case PIPE_BULK:
	default:
		qh = (struct ehci_qh *) urb->hcpriv;
		if (!qh)
			break;
		unlink_async (ehci, qh);
		break;

	case PIPE_INTERRUPT:
		qh = (struct ehci_qh *) urb->hcpriv;
		if (!qh)
			break;
		switch (qh->qh_state) {
		case QH_STATE_LINKED:
			intr_deschedule (ehci, qh);
			/* FALL THROUGH */
		case QH_STATE_IDLE:
			qh_completions (ehci, qh, NULL);
			break;
		default:
			ehci_dbg (ehci, "bogus qh %p state %d\n",
					qh, qh->qh_state);
			goto done;
		}

		/* reschedule QH iff another request is queued */
		if (!list_empty (&qh->qtd_list)
				&& HC_IS_RUNNING (hcd->state)) {
			int status;

			status = qh_schedule (ehci, qh);
			spin_unlock_irqrestore (&ehci->lock, flags);

			if (status != 0) {
				// shouldn't happen often, but ...
				// FIXME kill those tds' urbs
				err ("can't reschedule qh %p, err %d",
					qh, status);
			}
			return status;
		}
		break;

	case PIPE_ISOCHRONOUS:
		// itd or sitd ...

		// wait till next completion, do it then.
		// completion irqs can wait up to 1024 msec,
		break;
	}
done:
	spin_unlock_irqrestore (&ehci->lock, flags);
	return 0;
}

/*-------------------------------------------------------------------------*/

// bulk qh holds the data toggle

static void
ehci_endpoint_disable (struct usb_hcd *hcd, struct usb_host_endpoint *ep)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	unsigned long		flags;
	struct ehci_qh		*qh, *tmp;

	/* ASSERT:  any requests/urbs are being unlinked */
	/* ASSERT:  nobody can be submitting urbs for this any more */

rescan:
	spin_lock_irqsave (&ehci->lock, flags);
	qh = ep->hcpriv;
	if (!qh)
		goto done;

	/* endpoints can be iso streams.  for now, we don't
	 * accelerate iso completions ... so spin a while.
	 */
	if (qh->hw_info1 == 0) {
		ehci_vdbg (ehci, "iso delay\n");
		goto idle_timeout;
	}

	if (!HC_IS_RUNNING (hcd->state))
		qh->qh_state = QH_STATE_IDLE;
	switch (qh->qh_state) {
	case QH_STATE_LINKED:
		for (tmp = ehci->async->qh_next.qh;
				tmp && tmp != qh;
				tmp = tmp->qh_next.qh)
			continue;
		/* periodic qh self-unlinks on empty */
		if (!tmp)
			goto nogood;
		unlink_async (ehci, qh);
		/* FALL THROUGH */
	case QH_STATE_UNLINK:		/* wait for hw to finish? */
idle_timeout:
		spin_unlock_irqrestore (&ehci->lock, flags);
		schedule_timeout_uninterruptible(1);
		goto rescan;
	case QH_STATE_IDLE:		/* fully unlinked */
		if (list_empty (&qh->qtd_list)) {
			qh_put (qh);
			break;
		}
		/* else FALL THROUGH */
	default:
nogood:
		/* caller was supposed to have unlinked any requests;
		 * that's not our job.  just leak this memory.
		 */
		ehci_err (ehci, "qh %p (#%02x) state %d%s\n",
			qh, ep->desc.bEndpointAddress, qh->qh_state,
			list_empty (&qh->qtd_list) ? "" : "(has tds)");
		break;
	}
	ep->hcpriv = NULL;
done:
	spin_unlock_irqrestore (&ehci->lock, flags);
	return;
}

static int ehci_get_frame (struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
	return (readl (&ehci->regs->frame_index) >> 3) % ehci->periodic_size;
}

/*-------------------------------------------------------------------------*/

#define DRIVER_INFO DRIVER_VERSION " " DRIVER_DESC

MODULE_DESCRIPTION (DRIVER_INFO);
MODULE_AUTHOR (DRIVER_AUTHOR);
MODULE_LICENSE ("GPL");

#ifdef CONFIG_PCI
#include "ehci-pci.c"
#endif

#if !defined(CONFIG_PCI)
#error "missing bus glue for ehci-hcd"
#endif
