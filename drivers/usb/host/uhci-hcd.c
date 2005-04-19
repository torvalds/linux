/*
 * Universal Host Controller Interface driver for USB.
 *
 * Maintainer: Alan Stern <stern@rowland.harvard.edu>
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999-2002 Johannes Erdfelt, johannes@erdfelt.com
 * (C) Copyright 1999 Randy Dunlap
 * (C) Copyright 1999 Georg Acher, acher@in.tum.de
 * (C) Copyright 1999 Deti Fliegl, deti@fliegl.de
 * (C) Copyright 1999 Thomas Sailer, sailer@ife.ee.ethz.ch
 * (C) Copyright 1999 Roman Weissgaerber, weissg@vienna.at
 * (C) Copyright 2000 Yggdrasil Computing, Inc. (port of new PCI interface
 *               support from usb-ohci.c by Adam Richter, adam@yggdrasil.com).
 * (C) Copyright 1999 Gregory P. Smith (from usb-ohci.c)
 * (C) Copyright 2004 Alan Stern, stern@rowland.harvard.edu
 *
 * Intel documents this fairly well, and as far as I know there
 * are no royalties or anything like that, but even so there are
 * people who decided that they want to do the same thing in a
 * completely different way.
 *
 * WARNING! The USB documentation is downright evil. Most of it
 * is just crap, written by a committee. You're better off ignoring
 * most of it, the important stuff is:
 *  - the low-level protocol (fairly simple but lots of small details)
 *  - working around the horridness of the rest
 */

#include <linux/config.h>
#ifdef CONFIG_USB_DEBUG
#define DEBUG
#else
#undef DEBUG
#endif
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/pm.h>
#include <linux/dmapool.h>
#include <linux/dma-mapping.h>
#include <linux/usb.h>
#include <linux/bitops.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

#include "../core/hcd.h"
#include "uhci-hcd.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v2.2"
#define DRIVER_AUTHOR "Linus 'Frodo Rabbit' Torvalds, Johannes Erdfelt, \
Randy Dunlap, Georg Acher, Deti Fliegl, Thomas Sailer, Roman Weissgaerber, \
Alan Stern"
#define DRIVER_DESC "USB Universal Host Controller Interface driver"

/*
 * debug = 0, no debugging messages
 * debug = 1, dump failed URB's except for stalls
 * debug = 2, dump all failed URB's (including stalls)
 *            show all queues in /debug/uhci/[pci_addr]
 * debug = 3, show all TD's in URB's when dumping
 */
#ifdef DEBUG
static int debug = 1;
#else
static int debug = 0;
#endif
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug level");
static char *errbuf;
#define ERRBUF_LEN    (32 * 1024)

static kmem_cache_t *uhci_up_cachep;	/* urb_priv */

static void uhci_get_current_frame_number(struct uhci_hcd *uhci);
static void hc_state_transitions(struct uhci_hcd *uhci);

/* If a transfer is still active after this much time, turn off FSBR */
#define IDLE_TIMEOUT	msecs_to_jiffies(50)
#define FSBR_DELAY	msecs_to_jiffies(50)

/* When we timeout an idle transfer for FSBR, we'll switch it over to */
/* depth first traversal. We'll do it in groups of this number of TD's */
/* to make sure it doesn't hog all of the bandwidth */
#define DEPTH_INTERVAL 5

#include "uhci-hub.c"
#include "uhci-debug.c"
#include "uhci-q.c"

static int init_stall_timer(struct usb_hcd *hcd);

static void stall_callback(unsigned long ptr)
{
	struct usb_hcd *hcd = (struct usb_hcd *)ptr;
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);
	struct urb_priv *up;
	unsigned long flags;

	spin_lock_irqsave(&uhci->lock, flags);
	uhci_scan_schedule(uhci, NULL);

	list_for_each_entry(up, &uhci->urb_list, urb_list) {
		struct urb *u = up->urb;

		spin_lock(&u->lock);

		/* Check if the FSBR timed out */
		if (up->fsbr && !up->fsbr_timeout && time_after_eq(jiffies, up->fsbrtime + IDLE_TIMEOUT))
			uhci_fsbr_timeout(uhci, u);

		spin_unlock(&u->lock);
	}

	/* Really disable FSBR */
	if (!uhci->fsbr && uhci->fsbrtimeout && time_after_eq(jiffies, uhci->fsbrtimeout)) {
		uhci->fsbrtimeout = 0;
		uhci->skel_term_qh->link = UHCI_PTR_TERM;
	}

	/* Poll for and perform state transitions */
	hc_state_transitions(uhci);
	if (unlikely(uhci->suspended_ports && uhci->state != UHCI_SUSPENDED))
		uhci_check_ports(uhci);

	init_stall_timer(hcd);
	spin_unlock_irqrestore(&uhci->lock, flags);
}

static int init_stall_timer(struct usb_hcd *hcd)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);

	init_timer(&uhci->stall_timer);
	uhci->stall_timer.function = stall_callback;
	uhci->stall_timer.data = (unsigned long)hcd;
	uhci->stall_timer.expires = jiffies + msecs_to_jiffies(100);
	add_timer(&uhci->stall_timer);

	return 0;
}

static irqreturn_t uhci_irq(struct usb_hcd *hcd, struct pt_regs *regs)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);
	unsigned long io_addr = uhci->io_addr;
	unsigned short status;

	/*
	 * Read the interrupt status, and write it back to clear the
	 * interrupt cause.  Contrary to the UHCI specification, the
	 * "HC Halted" status bit is persistent: it is RO, not R/WC.
	 */
	status = inw(io_addr + USBSTS);
	if (!(status & ~USBSTS_HCH))	/* shared interrupt, not mine */
		return IRQ_NONE;
	outw(status, io_addr + USBSTS);		/* Clear it */

	if (status & ~(USBSTS_USBINT | USBSTS_ERROR | USBSTS_RD)) {
		if (status & USBSTS_HSE)
			dev_err(uhci_dev(uhci), "host system error, "
					"PCI problems?\n");
		if (status & USBSTS_HCPE)
			dev_err(uhci_dev(uhci), "host controller process "
					"error, something bad happened!\n");
		if ((status & USBSTS_HCH) && uhci->state > 0) {
			dev_err(uhci_dev(uhci), "host controller halted, "
					"very bad!\n");
			/* FIXME: Reset the controller, fix the offending TD */
		}
	}

	if (status & USBSTS_RD)
		uhci->resume_detect = 1;

	spin_lock(&uhci->lock);
	uhci_scan_schedule(uhci, regs);
	spin_unlock(&uhci->lock);

	return IRQ_HANDLED;
}

static void reset_hc(struct uhci_hcd *uhci)
{
	unsigned long io_addr = uhci->io_addr;

	/* Turn off PIRQ, SMI, and all interrupts.  This also turns off
	 * the BIOS's USB Legacy Support.
	 */
	pci_write_config_word(to_pci_dev(uhci_dev(uhci)), USBLEGSUP, 0);
	outw(0, uhci->io_addr + USBINTR);

	/* Global reset for 50ms */
	uhci->state = UHCI_RESET;
	outw(USBCMD_GRESET, io_addr + USBCMD);
	msleep(50);
	outw(0, io_addr + USBCMD);

	/* Another 10ms delay */
	msleep(10);
	uhci->resume_detect = 0;
	uhci->is_stopped = UHCI_IS_STOPPED;
}

static void suspend_hc(struct uhci_hcd *uhci)
{
	unsigned long io_addr = uhci->io_addr;

	dev_dbg(uhci_dev(uhci), "%s\n", __FUNCTION__);
	uhci->state = UHCI_SUSPENDED;
	uhci->resume_detect = 0;
	outw(USBCMD_EGSM, io_addr + USBCMD);

	/* FIXME: Wait for the controller to actually stop */
	uhci_get_current_frame_number(uhci);
	uhci->is_stopped = UHCI_IS_STOPPED;

	uhci_scan_schedule(uhci, NULL);
}

static void wakeup_hc(struct uhci_hcd *uhci)
{
	unsigned long io_addr = uhci->io_addr;

	switch (uhci->state) {
		case UHCI_SUSPENDED:		/* Start the resume */
			dev_dbg(uhci_dev(uhci), "%s\n", __FUNCTION__);

			/* Global resume for >= 20ms */
			outw(USBCMD_FGR | USBCMD_EGSM, io_addr + USBCMD);
			uhci->state = UHCI_RESUMING_1;
			uhci->state_end = jiffies + msecs_to_jiffies(20);
			uhci->is_stopped = 0;
			break;

		case UHCI_RESUMING_1:		/* End global resume */
			uhci->state = UHCI_RESUMING_2;
			outw(0, io_addr + USBCMD);
			/* Falls through */

		case UHCI_RESUMING_2:		/* Wait for EOP to be sent */
			if (inw(io_addr + USBCMD) & USBCMD_FGR)
				break;

			/* Run for at least 1 second, and
			 * mark it configured with a 64-byte max packet */
			uhci->state = UHCI_RUNNING_GRACE;
			uhci->state_end = jiffies + HZ;
			outw(USBCMD_RS | USBCMD_CF | USBCMD_MAXP,
					io_addr + USBCMD);
			break;

		case UHCI_RUNNING_GRACE:	/* Now allowed to suspend */
			uhci->state = UHCI_RUNNING;
			break;

		default:
			break;
	}
}

static int ports_active(struct uhci_hcd *uhci)
{
	unsigned long io_addr = uhci->io_addr;
	int connection = 0;
	int i;

	for (i = 0; i < uhci->rh_numports; i++)
		connection |= (inw(io_addr + USBPORTSC1 + i * 2) & USBPORTSC_CCS);

	return connection;
}

static int suspend_allowed(struct uhci_hcd *uhci)
{
	unsigned long io_addr = uhci->io_addr;
	int i;

	if (to_pci_dev(uhci_dev(uhci))->vendor != PCI_VENDOR_ID_INTEL)
		return 1;

	/* Some of Intel's USB controllers have a bug that causes false
	 * resume indications if any port has an over current condition.
	 * To prevent problems, we will not allow a global suspend if
	 * any ports are OC.
	 *
	 * Some motherboards using Intel's chipsets (but not using all
	 * the USB ports) appear to hardwire the over current inputs active
	 * to disable the USB ports.
	 */

	/* check for over current condition on any port */
	for (i = 0; i < uhci->rh_numports; i++) {
		if (inw(io_addr + USBPORTSC1 + i * 2) & USBPORTSC_OC)
			return 0;
	}

	return 1;
}

static void hc_state_transitions(struct uhci_hcd *uhci)
{
	switch (uhci->state) {
		case UHCI_RUNNING:

			/* global suspend if nothing connected for 1 second */
			if (!ports_active(uhci) && suspend_allowed(uhci)) {
				uhci->state = UHCI_SUSPENDING_GRACE;
				uhci->state_end = jiffies + HZ;
			}
			break;

		case UHCI_SUSPENDING_GRACE:
			if (ports_active(uhci))
				uhci->state = UHCI_RUNNING;
			else if (time_after_eq(jiffies, uhci->state_end))
				suspend_hc(uhci);
			break;

		case UHCI_SUSPENDED:

			/* wakeup if requested by a device */
			if (uhci->resume_detect)
				wakeup_hc(uhci);
			break;

		case UHCI_RESUMING_1:
		case UHCI_RESUMING_2:
		case UHCI_RUNNING_GRACE:
			if (time_after_eq(jiffies, uhci->state_end))
				wakeup_hc(uhci);
			break;

		default:
			break;
	}
}

/*
 * Store the current frame number in uhci->frame_number if the controller
 * is runnning
 */
static void uhci_get_current_frame_number(struct uhci_hcd *uhci)
{
	if (!uhci->is_stopped)
		uhci->frame_number = inw(uhci->io_addr + USBFRNUM);
}

static int start_hc(struct uhci_hcd *uhci)
{
	unsigned long io_addr = uhci->io_addr;
	int timeout = 10;

	/*
	 * Reset the HC - this will force us to get a
	 * new notification of any already connected
	 * ports due to the virtual disconnect that it
	 * implies.
	 */
	outw(USBCMD_HCRESET, io_addr + USBCMD);
	while (inw(io_addr + USBCMD) & USBCMD_HCRESET) {
		if (--timeout < 0) {
			dev_err(uhci_dev(uhci), "USBCMD_HCRESET timed out!\n");
			return -ETIMEDOUT;
		}
		msleep(1);
	}

	/* Mark controller as running before we enable interrupts */
	uhci_to_hcd(uhci)->state = HC_STATE_RUNNING;

	/* Turn on PIRQ and all interrupts */
	pci_write_config_word(to_pci_dev(uhci_dev(uhci)), USBLEGSUP,
			USBLEGSUP_DEFAULT);
	outw(USBINTR_TIMEOUT | USBINTR_RESUME | USBINTR_IOC | USBINTR_SP,
		io_addr + USBINTR);

	/* Start at frame 0 */
	outw(0, io_addr + USBFRNUM);
	outl(uhci->fl->dma_handle, io_addr + USBFLBASEADD);

	/* Run and mark it configured with a 64-byte max packet */
	uhci->state = UHCI_RUNNING_GRACE;
	uhci->state_end = jiffies + HZ;
	outw(USBCMD_RS | USBCMD_CF | USBCMD_MAXP, io_addr + USBCMD);
	uhci->is_stopped = 0;

	return 0;
}

/*
 * De-allocate all resources
 */
static void release_uhci(struct uhci_hcd *uhci)
{
	int i;

	for (i = 0; i < UHCI_NUM_SKELQH; i++)
		if (uhci->skelqh[i]) {
			uhci_free_qh(uhci, uhci->skelqh[i]);
			uhci->skelqh[i] = NULL;
		}

	if (uhci->term_td) {
		uhci_free_td(uhci, uhci->term_td);
		uhci->term_td = NULL;
	}

	if (uhci->qh_pool) {
		dma_pool_destroy(uhci->qh_pool);
		uhci->qh_pool = NULL;
	}

	if (uhci->td_pool) {
		dma_pool_destroy(uhci->td_pool);
		uhci->td_pool = NULL;
	}

	if (uhci->fl) {
		dma_free_coherent(uhci_dev(uhci), sizeof(*uhci->fl),
				uhci->fl, uhci->fl->dma_handle);
		uhci->fl = NULL;
	}

	if (uhci->dentry) {
		debugfs_remove(uhci->dentry);
		uhci->dentry = NULL;
	}
}

static int uhci_reset(struct usb_hcd *hcd)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);

	uhci->io_addr = (unsigned long) hcd->rsrc_start;

	/* Kick BIOS off this hardware and reset, so we won't get
	 * interrupts from any previous setup.
	 */
	reset_hc(uhci);
	return 0;
}

/*
 * Allocate a frame list, and then setup the skeleton
 *
 * The hardware doesn't really know any difference
 * in the queues, but the order does matter for the
 * protocols higher up. The order is:
 *
 *  - any isochronous events handled before any
 *    of the queues. We don't do that here, because
 *    we'll create the actual TD entries on demand.
 *  - The first queue is the interrupt queue.
 *  - The second queue is the control queue, split into low- and full-speed
 *  - The third queue is bulk queue.
 *  - The fourth queue is the bandwidth reclamation queue, which loops back
 *    to the full-speed control queue.
 */
static int uhci_start(struct usb_hcd *hcd)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);
	int retval = -EBUSY;
	int i, port;
	unsigned io_size;
	dma_addr_t dma_handle;
	struct usb_device *udev;
	struct dentry *dentry;

	io_size = (unsigned) hcd->rsrc_len;

	dentry = debugfs_create_file(hcd->self.bus_name, S_IFREG|S_IRUGO|S_IWUSR, uhci_debugfs_root, uhci, &uhci_debug_operations);
	if (!dentry) {
		dev_err(uhci_dev(uhci), "couldn't create uhci debugfs entry\n");
		retval = -ENOMEM;
		goto err_create_debug_entry;
	}
	uhci->dentry = dentry;

	uhci->fsbr = 0;
	uhci->fsbrtimeout = 0;

	spin_lock_init(&uhci->lock);
	INIT_LIST_HEAD(&uhci->qh_remove_list);

	INIT_LIST_HEAD(&uhci->td_remove_list);

	INIT_LIST_HEAD(&uhci->urb_remove_list);

	INIT_LIST_HEAD(&uhci->urb_list);

	INIT_LIST_HEAD(&uhci->complete_list);

	init_waitqueue_head(&uhci->waitqh);

	uhci->fl = dma_alloc_coherent(uhci_dev(uhci), sizeof(*uhci->fl),
			&dma_handle, 0);
	if (!uhci->fl) {
		dev_err(uhci_dev(uhci), "unable to allocate "
				"consistent memory for frame list\n");
		goto err_alloc_fl;
	}

	memset((void *)uhci->fl, 0, sizeof(*uhci->fl));

	uhci->fl->dma_handle = dma_handle;

	uhci->td_pool = dma_pool_create("uhci_td", uhci_dev(uhci),
			sizeof(struct uhci_td), 16, 0);
	if (!uhci->td_pool) {
		dev_err(uhci_dev(uhci), "unable to create td dma_pool\n");
		goto err_create_td_pool;
	}

	uhci->qh_pool = dma_pool_create("uhci_qh", uhci_dev(uhci),
			sizeof(struct uhci_qh), 16, 0);
	if (!uhci->qh_pool) {
		dev_err(uhci_dev(uhci), "unable to create qh dma_pool\n");
		goto err_create_qh_pool;
	}

	/* Initialize the root hub */

	/* UHCI specs says devices must have 2 ports, but goes on to say */
	/*  they may have more but give no way to determine how many they */
	/*  have. However, according to the UHCI spec, Bit 7 is always set */
	/*  to 1. So we try to use this to our advantage */
	for (port = 0; port < (io_size - 0x10) / 2; port++) {
		unsigned int portstatus;

		portstatus = inw(uhci->io_addr + 0x10 + (port * 2));
		if (!(portstatus & 0x0080))
			break;
	}
	if (debug)
		dev_info(uhci_dev(uhci), "detected %d ports\n", port);

	/* This is experimental so anything less than 2 or greater than 8 is */
	/*  something weird and we'll ignore it */
	if (port < 2 || port > UHCI_RH_MAXCHILD) {
		dev_info(uhci_dev(uhci), "port count misdetected? "
				"forcing to 2 ports\n");
		port = 2;
	}

	uhci->rh_numports = port;

	udev = usb_alloc_dev(NULL, &hcd->self, 0);
	if (!udev) {
		dev_err(uhci_dev(uhci), "unable to allocate root hub\n");
		goto err_alloc_root_hub;
	}

	uhci->term_td = uhci_alloc_td(uhci, udev);
	if (!uhci->term_td) {
		dev_err(uhci_dev(uhci), "unable to allocate terminating TD\n");
		goto err_alloc_term_td;
	}

	for (i = 0; i < UHCI_NUM_SKELQH; i++) {
		uhci->skelqh[i] = uhci_alloc_qh(uhci, udev);
		if (!uhci->skelqh[i]) {
			dev_err(uhci_dev(uhci), "unable to allocate QH\n");
			goto err_alloc_skelqh;
		}
	}

	/*
	 * 8 Interrupt queues; link all higher int queues to int1,
	 * then link int1 to control and control to bulk
	 */
	uhci->skel_int128_qh->link =
			uhci->skel_int64_qh->link =
			uhci->skel_int32_qh->link =
			uhci->skel_int16_qh->link =
			uhci->skel_int8_qh->link =
			uhci->skel_int4_qh->link =
			uhci->skel_int2_qh->link =
			cpu_to_le32(uhci->skel_int1_qh->dma_handle) | UHCI_PTR_QH;
	uhci->skel_int1_qh->link = cpu_to_le32(uhci->skel_ls_control_qh->dma_handle) | UHCI_PTR_QH;

	uhci->skel_ls_control_qh->link = cpu_to_le32(uhci->skel_fs_control_qh->dma_handle) | UHCI_PTR_QH;
	uhci->skel_fs_control_qh->link = cpu_to_le32(uhci->skel_bulk_qh->dma_handle) | UHCI_PTR_QH;
	uhci->skel_bulk_qh->link = cpu_to_le32(uhci->skel_term_qh->dma_handle) | UHCI_PTR_QH;

	/* This dummy TD is to work around a bug in Intel PIIX controllers */
	uhci_fill_td(uhci->term_td, 0, (UHCI_NULL_DATA_SIZE << 21) |
		(0x7f << TD_TOKEN_DEVADDR_SHIFT) | USB_PID_IN, 0);
	uhci->term_td->link = cpu_to_le32(uhci->term_td->dma_handle);

	uhci->skel_term_qh->link = UHCI_PTR_TERM;
	uhci->skel_term_qh->element = cpu_to_le32(uhci->term_td->dma_handle);

	/*
	 * Fill the frame list: make all entries point to the proper
	 * interrupt queue.
	 *
	 * The interrupt queues will be interleaved as evenly as possible.
	 * There's not much to be done about period-1 interrupts; they have
	 * to occur in every frame.  But we can schedule period-2 interrupts
	 * in odd-numbered frames, period-4 interrupts in frames congruent
	 * to 2 (mod 4), and so on.  This way each frame only has two
	 * interrupt QHs, which will help spread out bandwidth utilization.
	 */
	for (i = 0; i < UHCI_NUMFRAMES; i++) {
		int irq;

		/*
		 * ffs (Find First bit Set) does exactly what we need:
		 * 1,3,5,...  => ffs = 0 => use skel_int2_qh = skelqh[6],
		 * 2,6,10,... => ffs = 1 => use skel_int4_qh = skelqh[5], etc.
		 * ffs > 6 => not on any high-period queue, so use
		 *	skel_int1_qh = skelqh[7].
		 * Add UHCI_NUMFRAMES to insure at least one bit is set.
		 */
		irq = 6 - (int) __ffs(i + UHCI_NUMFRAMES);
		if (irq < 0)
			irq = 7;

		/* Only place we don't use the frame list routines */
		uhci->fl->frame[i] = UHCI_PTR_QH |
				cpu_to_le32(uhci->skelqh[irq]->dma_handle);
	}

	/*
	 * Some architectures require a full mb() to enforce completion of
	 * the memory writes above before the I/O transfers in start_hc().
	 */
	mb();
	if ((retval = start_hc(uhci)) != 0)
		goto err_alloc_skelqh;

	init_stall_timer(hcd);

	udev->speed = USB_SPEED_FULL;

	if (usb_hcd_register_root_hub(udev, hcd) != 0) {
		dev_err(uhci_dev(uhci), "unable to start root hub\n");
		retval = -ENOMEM;
		goto err_start_root_hub;
	}

	return 0;

/*
 * error exits:
 */
err_start_root_hub:
	reset_hc(uhci);

	del_timer_sync(&uhci->stall_timer);

err_alloc_skelqh:
	for (i = 0; i < UHCI_NUM_SKELQH; i++)
		if (uhci->skelqh[i]) {
			uhci_free_qh(uhci, uhci->skelqh[i]);
			uhci->skelqh[i] = NULL;
		}

	uhci_free_td(uhci, uhci->term_td);
	uhci->term_td = NULL;

err_alloc_term_td:
	usb_put_dev(udev);

err_alloc_root_hub:
	dma_pool_destroy(uhci->qh_pool);
	uhci->qh_pool = NULL;

err_create_qh_pool:
	dma_pool_destroy(uhci->td_pool);
	uhci->td_pool = NULL;

err_create_td_pool:
	dma_free_coherent(uhci_dev(uhci), sizeof(*uhci->fl),
			uhci->fl, uhci->fl->dma_handle);
	uhci->fl = NULL;

err_alloc_fl:
	debugfs_remove(uhci->dentry);
	uhci->dentry = NULL;

err_create_debug_entry:
	return retval;
}

static void uhci_stop(struct usb_hcd *hcd)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);

	del_timer_sync(&uhci->stall_timer);
	reset_hc(uhci);

	spin_lock_irq(&uhci->lock);
	uhci_scan_schedule(uhci, NULL);
	spin_unlock_irq(&uhci->lock);
	
	release_uhci(uhci);
}

#ifdef CONFIG_PM
static int uhci_suspend(struct usb_hcd *hcd, pm_message_t message)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);

	spin_lock_irq(&uhci->lock);

	/* Don't try to suspend broken motherboards, reset instead */
	if (suspend_allowed(uhci))
		suspend_hc(uhci);
	else {
		spin_unlock_irq(&uhci->lock);
		reset_hc(uhci);
		spin_lock_irq(&uhci->lock);
		uhci_scan_schedule(uhci, NULL);
	}

	spin_unlock_irq(&uhci->lock);
	return 0;
}

static int uhci_resume(struct usb_hcd *hcd)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);
	int rc;

	pci_set_master(to_pci_dev(uhci_dev(uhci)));

	spin_lock_irq(&uhci->lock);

	if (uhci->state == UHCI_SUSPENDED) {

		/*
		 * Some systems don't maintain the UHCI register values
		 * during a PM suspend/resume cycle, so reinitialize
		 * the Frame Number, Framelist Base Address, Interrupt
		 * Enable, and Legacy Support registers.
		 */
		pci_write_config_word(to_pci_dev(uhci_dev(uhci)), USBLEGSUP,
				0);
		outw(uhci->frame_number, uhci->io_addr + USBFRNUM);
		outl(uhci->fl->dma_handle, uhci->io_addr + USBFLBASEADD);
		outw(USBINTR_TIMEOUT | USBINTR_RESUME | USBINTR_IOC |
				USBINTR_SP, uhci->io_addr + USBINTR);
		uhci->resume_detect = 1;
		pci_write_config_word(to_pci_dev(uhci_dev(uhci)), USBLEGSUP,
				USBLEGSUP_DEFAULT);
	} else {
		spin_unlock_irq(&uhci->lock);
		reset_hc(uhci);
		if ((rc = start_hc(uhci)) != 0)
			return rc;
		spin_lock_irq(&uhci->lock);
	}
	hcd->state = HC_STATE_RUNNING;

	spin_unlock_irq(&uhci->lock);
	return 0;
}
#endif

/* Wait until all the URBs for a particular device/endpoint are gone */
static void uhci_hcd_endpoint_disable(struct usb_hcd *hcd,
		struct usb_host_endpoint *ep)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);

	wait_event_interruptible(uhci->waitqh, list_empty(&ep->urb_list));
}

static int uhci_hcd_get_frame_number(struct usb_hcd *hcd)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);
	int frame_number;
	unsigned long flags;

	/* Minimize latency by avoiding the spinlock */
	local_irq_save(flags);
	rmb();
	frame_number = (uhci->is_stopped ? uhci->frame_number :
			inw(uhci->io_addr + USBFRNUM));
	local_irq_restore(flags);
	return frame_number;
}

static const char hcd_name[] = "uhci_hcd";

static const struct hc_driver uhci_driver = {
	.description =		hcd_name,
	.product_desc =		"UHCI Host Controller",
	.hcd_priv_size =	sizeof(struct uhci_hcd),

	/* Generic hardware linkage */
	.irq =			uhci_irq,
	.flags =		HCD_USB11,

	/* Basic lifecycle operations */
	.reset =		uhci_reset,
	.start =		uhci_start,
#ifdef CONFIG_PM
	.suspend =		uhci_suspend,
	.resume =		uhci_resume,
#endif
	.stop =			uhci_stop,

	.urb_enqueue =		uhci_urb_enqueue,
	.urb_dequeue =		uhci_urb_dequeue,

	.endpoint_disable =	uhci_hcd_endpoint_disable,
	.get_frame_number =	uhci_hcd_get_frame_number,

	.hub_status_data =	uhci_hub_status_data,
	.hub_control =		uhci_hub_control,
};

static const struct pci_device_id uhci_pci_ids[] = { {
	/* handle any USB UHCI controller */
	PCI_DEVICE_CLASS(((PCI_CLASS_SERIAL_USB << 8) | 0x00), ~0),
	.driver_data =	(unsigned long) &uhci_driver,
	}, { /* end: all zeroes */ }
};

MODULE_DEVICE_TABLE(pci, uhci_pci_ids);

static struct pci_driver uhci_pci_driver = {
	.name =		(char *)hcd_name,
	.id_table =	uhci_pci_ids,

	.probe =	usb_hcd_pci_probe,
	.remove =	usb_hcd_pci_remove,

#ifdef	CONFIG_PM
	.suspend =	usb_hcd_pci_suspend,
	.resume =	usb_hcd_pci_resume,
#endif	/* PM */
};
 
static int __init uhci_hcd_init(void)
{
	int retval = -ENOMEM;

	printk(KERN_INFO DRIVER_DESC " " DRIVER_VERSION "\n");

	if (usb_disabled())
		return -ENODEV;

	if (debug) {
		errbuf = kmalloc(ERRBUF_LEN, GFP_KERNEL);
		if (!errbuf)
			goto errbuf_failed;
	}

	uhci_debugfs_root = debugfs_create_dir("uhci", NULL);
	if (!uhci_debugfs_root)
		goto debug_failed;

	uhci_up_cachep = kmem_cache_create("uhci_urb_priv",
		sizeof(struct urb_priv), 0, 0, NULL, NULL);
	if (!uhci_up_cachep)
		goto up_failed;

	retval = pci_register_driver(&uhci_pci_driver);
	if (retval)
		goto init_failed;

	return 0;

init_failed:
	if (kmem_cache_destroy(uhci_up_cachep))
		warn("not all urb_priv's were freed!");

up_failed:
	debugfs_remove(uhci_debugfs_root);

debug_failed:
	kfree(errbuf);

errbuf_failed:

	return retval;
}

static void __exit uhci_hcd_cleanup(void) 
{
	pci_unregister_driver(&uhci_pci_driver);
	
	if (kmem_cache_destroy(uhci_up_cachep))
		warn("not all urb_priv's were freed!");

	debugfs_remove(uhci_debugfs_root);
	kfree(errbuf);
}

module_init(uhci_hcd_init);
module_exit(uhci_hcd_cleanup);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
