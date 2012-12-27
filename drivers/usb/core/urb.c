#include <linux/module.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/log2.h>
#include <linux/usb.h>
#include <linux/wait.h>
#include <linux/usb/hcd.h>

#define to_urb(d) container_of(d, struct urb, kref)


static void urb_destroy(struct kref *kref)
{
	struct urb *urb = to_urb(kref);

	if (urb->transfer_flags & URB_FREE_BUFFER)
		kfree(urb->transfer_buffer);

	kfree(urb);
}

/**
 * usb_init_urb - initializes a urb so that it can be used by a USB driver
 * @urb: pointer to the urb to initialize
 *
 * Initializes a urb so that the USB subsystem can use it properly.
 *
 * If a urb is created with a call to usb_alloc_urb() it is not
 * necessary to call this function.  Only use this if you allocate the
 * space for a struct urb on your own.  If you call this function, be
 * careful when freeing the memory for your urb that it is no longer in
 * use by the USB core.
 *
 * Only use this function if you _really_ understand what you are doing.
 */
void usb_init_urb(struct urb *urb)
{
	if (urb) {
		memset(urb, 0, sizeof(*urb));
		kref_init(&urb->kref);
		INIT_LIST_HEAD(&urb->anchor_list);
	}
}
EXPORT_SYMBOL_GPL(usb_init_urb);

/**
 * usb_alloc_urb - creates a new urb for a USB driver to use
 * @iso_packets: number of iso packets for this urb
 * @mem_flags: the type of memory to allocate, see kmalloc() for a list of
 *	valid options for this.
 *
 * Creates an urb for the USB driver to use, initializes a few internal
 * structures, incrementes the usage counter, and returns a pointer to it.
 *
 * If no memory is available, NULL is returned.
 *
 * If the driver want to use this urb for interrupt, control, or bulk
 * endpoints, pass '0' as the number of iso packets.
 *
 * The driver must call usb_free_urb() when it is finished with the urb.
 */
struct urb *usb_alloc_urb(int iso_packets, gfp_t mem_flags)
{
	struct urb *urb;

	urb = kmalloc(sizeof(struct urb) +
		iso_packets * sizeof(struct usb_iso_packet_descriptor),
		mem_flags);
	if (!urb) {
		printk(KERN_ERR "alloc_urb: kmalloc failed\n");
		return NULL;
	}
	usb_init_urb(urb);
	return urb;
}
EXPORT_SYMBOL_GPL(usb_alloc_urb);

/**
 * usb_free_urb - frees the memory used by a urb when all users of it are finished
 * @urb: pointer to the urb to free, may be NULL
 *
 * Must be called when a user of a urb is finished with it.  When the last user
 * of the urb calls this function, the memory of the urb is freed.
 *
 * Note: The transfer buffer associated with the urb is not freed unless the
 * URB_FREE_BUFFER transfer flag is set.
 */
void usb_free_urb(struct urb *urb)
{
	if (urb)
		kref_put(&urb->kref, urb_destroy);
}
EXPORT_SYMBOL_GPL(usb_free_urb);

/**
 * usb_get_urb - increments the reference count of the urb
 * @urb: pointer to the urb to modify, may be NULL
 *
 * This must be  called whenever a urb is transferred from a device driver to a
 * host controller driver.  This allows proper reference counting to happen
 * for urbs.
 *
 * A pointer to the urb with the incremented reference counter is returned.
 */
struct urb *usb_get_urb(struct urb *urb)
{
	if (urb)
		kref_get(&urb->kref);
	return urb;
}
EXPORT_SYMBOL_GPL(usb_get_urb);

/**
 * usb_anchor_urb - anchors an URB while it is processed
 * @urb: pointer to the urb to anchor
 * @anchor: pointer to the anchor
 *
 * This can be called to have access to URBs which are to be executed
 * without bothering to track them
 */
void usb_anchor_urb(struct urb *urb, struct usb_anchor *anchor)
{
	unsigned long flags;

	spin_lock_irqsave(&anchor->lock, flags);
	usb_get_urb(urb);
	list_add_tail(&urb->anchor_list, &anchor->urb_list);
	urb->anchor = anchor;

	if (unlikely(anchor->poisoned)) {
		atomic_inc(&urb->reject);
	}

	spin_unlock_irqrestore(&anchor->lock, flags);
}
EXPORT_SYMBOL_GPL(usb_anchor_urb);

/* Callers must hold anchor->lock */
static void __usb_unanchor_urb(struct urb *urb, struct usb_anchor *anchor)
{
	urb->anchor = NULL;
	list_del(&urb->anchor_list);
	usb_put_urb(urb);
	if (list_empty(&anchor->urb_list))
		wake_up(&anchor->wait);
}

/**
 * usb_unanchor_urb - unanchors an URB
 * @urb: pointer to the urb to anchor
 *
 * Call this to stop the system keeping track of this URB
 */
void usb_unanchor_urb(struct urb *urb)
{
	unsigned long flags;
	struct usb_anchor *anchor;

	if (!urb)
		return;

	anchor = urb->anchor;
	if (!anchor)
		return;

	spin_lock_irqsave(&anchor->lock, flags);
	/*
	 * At this point, we could be competing with another thread which
	 * has the same intention. To protect the urb from being unanchored
	 * twice, only the winner of the race gets the job.
	 */
	if (likely(anchor == urb->anchor))
		__usb_unanchor_urb(urb, anchor);
	spin_unlock_irqrestore(&anchor->lock, flags);
}
EXPORT_SYMBOL_GPL(usb_unanchor_urb);

/*-------------------------------------------------------------------*/

/**
 * usb_submit_urb - issue an asynchronous transfer request for an endpoint
 * @urb: pointer to the urb describing the request
 * @mem_flags: the type of memory to allocate, see kmalloc() for a list
 *	of valid options for this.
 *
 * This submits a transfer request, and transfers control of the URB
 * describing that request to the USB subsystem.  Request completion will
 * be indicated later, asynchronously, by calling the completion handler.
 * The three types of completion are success, error, and unlink
 * (a software-induced fault, also called "request cancellation").
 *
 * URBs may be submitted in interrupt context.
 *
 * The caller must have correctly initialized the URB before submitting
 * it.  Functions such as usb_fill_bulk_urb() and usb_fill_control_urb() are
 * available to ensure that most fields are correctly initialized, for
 * the particular kind of transfer, although they will not initialize
 * any transfer flags.
 *
 * Successful submissions return 0; otherwise this routine returns a
 * negative error number.  If the submission is successful, the complete()
 * callback from the URB will be called exactly once, when the USB core and
 * Host Controller Driver (HCD) are finished with the URB.  When the completion
 * function is called, control of the URB is returned to the device
 * driver which issued the request.  The completion handler may then
 * immediately free or reuse that URB.
 *
 * With few exceptions, USB device drivers should never access URB fields
 * provided by usbcore or the HCD until its complete() is called.
 * The exceptions relate to periodic transfer scheduling.  For both
 * interrupt and isochronous urbs, as part of successful URB submission
 * urb->interval is modified to reflect the actual transfer period used
 * (normally some power of two units).  And for isochronous urbs,
 * urb->start_frame is modified to reflect when the URB's transfers were
 * scheduled to start.
 *
 * Not all isochronous transfer scheduling policies will work, but most
 * host controller drivers should easily handle ISO queues going from now
 * until 10-200 msec into the future.  Drivers should try to keep at
 * least one or two msec of data in the queue; many controllers require
 * that new transfers start at least 1 msec in the future when they are
 * added.  If the driver is unable to keep up and the queue empties out,
 * the behavior for new submissions is governed by the URB_ISO_ASAP flag.
 * If the flag is set, or if the queue is idle, then the URB is always
 * assigned to the first available (and not yet expired) slot in the
 * endpoint's schedule.  If the flag is not set and the queue is active
 * then the URB is always assigned to the next slot in the schedule
 * following the end of the endpoint's previous URB, even if that slot is
 * in the past.  When a packet is assigned in this way to a slot that has
 * already expired, the packet is not transmitted and the corresponding
 * usb_iso_packet_descriptor's status field will return -EXDEV.  If this
 * would happen to all the packets in the URB, submission fails with a
 * -EXDEV error code.
 *
 * For control endpoints, the synchronous usb_control_msg() call is
 * often used (in non-interrupt context) instead of this call.
 * That is often used through convenience wrappers, for the requests
 * that are standardized in the USB 2.0 specification.  For bulk
 * endpoints, a synchronous usb_bulk_msg() call is available.
 *
 * Request Queuing:
 *
 * URBs may be submitted to endpoints before previous ones complete, to
 * minimize the impact of interrupt latencies and system overhead on data
 * throughput.  With that queuing policy, an endpoint's queue would never
 * be empty.  This is required for continuous isochronous data streams,
 * and may also be required for some kinds of interrupt transfers. Such
 * queuing also maximizes bandwidth utilization by letting USB controllers
 * start work on later requests before driver software has finished the
 * completion processing for earlier (successful) requests.
 *
 * As of Linux 2.6, all USB endpoint transfer queues support depths greater
 * than one.  This was previously a HCD-specific behavior, except for ISO
 * transfers.  Non-isochronous endpoint queues are inactive during cleanup
 * after faults (transfer errors or cancellation).
 *
 * Reserved Bandwidth Transfers:
 *
 * Periodic transfers (interrupt or isochronous) are performed repeatedly,
 * using the interval specified in the urb.  Submitting the first urb to
 * the endpoint reserves the bandwidth necessary to make those transfers.
 * If the USB subsystem can't allocate sufficient bandwidth to perform
 * the periodic request, submitting such a periodic request should fail.
 *
 * For devices under xHCI, the bandwidth is reserved at configuration time, or
 * when the alt setting is selected.  If there is not enough bus bandwidth, the
 * configuration/alt setting request will fail.  Therefore, submissions to
 * periodic endpoints on devices under xHCI should never fail due to bandwidth
 * constraints.
 *
 * Device drivers must explicitly request that repetition, by ensuring that
 * some URB is always on the endpoint's queue (except possibly for short
 * periods during completion callacks).  When there is no longer an urb
 * queued, the endpoint's bandwidth reservation is canceled.  This means
 * drivers can use their completion handlers to ensure they keep bandwidth
 * they need, by reinitializing and resubmitting the just-completed urb
 * until the driver longer needs that periodic bandwidth.
 *
 * Memory Flags:
 *
 * The general rules for how to decide which mem_flags to use
 * are the same as for kmalloc.  There are four
 * different possible values; GFP_KERNEL, GFP_NOFS, GFP_NOIO and
 * GFP_ATOMIC.
 *
 * GFP_NOFS is not ever used, as it has not been implemented yet.
 *
 * GFP_ATOMIC is used when
 *   (a) you are inside a completion handler, an interrupt, bottom half,
 *       tasklet or timer, or
 *   (b) you are holding a spinlock or rwlock (does not apply to
 *       semaphores), or
 *   (c) current->state != TASK_RUNNING, this is the case only after
 *       you've changed it.
 *
 * GFP_NOIO is used in the block io path and error handling of storage
 * devices.
 *
 * All other situations use GFP_KERNEL.
 *
 * Some more specific rules for mem_flags can be inferred, such as
 *  (1) start_xmit, timeout, and receive methods of network drivers must
 *      use GFP_ATOMIC (they are called with a spinlock held);
 *  (2) queuecommand methods of scsi drivers must use GFP_ATOMIC (also
 *      called with a spinlock held);
 *  (3) If you use a kernel thread with a network driver you must use
 *      GFP_NOIO, unless (b) or (c) apply;
 *  (4) after you have done a down() you can use GFP_KERNEL, unless (b) or (c)
 *      apply or your are in a storage driver's block io path;
 *  (5) USB probe and disconnect can use GFP_KERNEL unless (b) or (c) apply; and
 *  (6) changing firmware on a running storage or net device uses
 *      GFP_NOIO, unless b) or c) apply
 *
 */
int usb_submit_urb(struct urb *urb, gfp_t mem_flags)
{
	int				xfertype, max;
	struct usb_device		*dev;
	struct usb_host_endpoint	*ep;
	int				is_out;

	if (!urb || !urb->complete)
		return -EINVAL;
	if (urb->hcpriv) {
		WARN_ONCE(1, "URB %p submitted while active\n", urb);
		return -EBUSY;
	}

	dev = urb->dev;
	if ((!dev) || (dev->state < USB_STATE_UNAUTHENTICATED))
		return -ENODEV;

	/* For now, get the endpoint from the pipe.  Eventually drivers
	 * will be required to set urb->ep directly and we will eliminate
	 * urb->pipe.
	 */
	ep = usb_pipe_endpoint(dev, urb->pipe);
	if (!ep)
		return -ENOENT;

	urb->ep = ep;
	urb->status = -EINPROGRESS;
	urb->actual_length = 0;

	/* Lots of sanity checks, so HCDs can rely on clean data
	 * and don't need to duplicate tests
	 */
	xfertype = usb_endpoint_type(&ep->desc);
	if (xfertype == USB_ENDPOINT_XFER_CONTROL) {
		struct usb_ctrlrequest *setup =
				(struct usb_ctrlrequest *) urb->setup_packet;

		if (!setup)
			return -ENOEXEC;
		is_out = !(setup->bRequestType & USB_DIR_IN) ||
				!setup->wLength;
	} else {
		is_out = usb_endpoint_dir_out(&ep->desc);
	}

	/* Clear the internal flags and cache the direction for later use */
	urb->transfer_flags &= ~(URB_DIR_MASK | URB_DMA_MAP_SINGLE |
			URB_DMA_MAP_PAGE | URB_DMA_MAP_SG | URB_MAP_LOCAL |
			URB_SETUP_MAP_SINGLE | URB_SETUP_MAP_LOCAL |
			URB_DMA_SG_COMBINED);
	urb->transfer_flags |= (is_out ? URB_DIR_OUT : URB_DIR_IN);

	if (xfertype != USB_ENDPOINT_XFER_CONTROL &&
			dev->state < USB_STATE_CONFIGURED)
		return -ENODEV;

	max = usb_endpoint_maxp(&ep->desc);
	if (max <= 0) {
		dev_dbg(&dev->dev,
			"bogus endpoint ep%d%s in %s (bad maxpacket %d)\n",
			usb_endpoint_num(&ep->desc), is_out ? "out" : "in",
			__func__, max);
		return -EMSGSIZE;
	}

	/* periodic transfers limit size per frame/uframe,
	 * but drivers only control those sizes for ISO.
	 * while we're checking, initialize return status.
	 */
	if (xfertype == USB_ENDPOINT_XFER_ISOC) {
		int	n, len;

		/* SuperSpeed isoc endpoints have up to 16 bursts of up to
		 * 3 packets each
		 */
		if (dev->speed == USB_SPEED_SUPER) {
			int     burst = 1 + ep->ss_ep_comp.bMaxBurst;
			int     mult = USB_SS_MULT(ep->ss_ep_comp.bmAttributes);
			max *= burst;
			max *= mult;
		}

		/* "high bandwidth" mode, 1-3 packets/uframe? */
		if (dev->speed == USB_SPEED_HIGH) {
			int	mult = 1 + ((max >> 11) & 0x03);
			max &= 0x07ff;
			max *= mult;
		}

		if (urb->number_of_packets <= 0)
			return -EINVAL;
		for (n = 0; n < urb->number_of_packets; n++) {
			len = urb->iso_frame_desc[n].length;
			if (len < 0 || len > max)
				return -EMSGSIZE;
			urb->iso_frame_desc[n].status = -EXDEV;
			urb->iso_frame_desc[n].actual_length = 0;
		}
	}

	/* the I/O buffer must be mapped/unmapped, except when length=0 */
	if (urb->transfer_buffer_length > INT_MAX)
		return -EMSGSIZE;

#ifdef DEBUG
	/* stuff that drivers shouldn't do, but which shouldn't
	 * cause problems in HCDs if they get it wrong.
	 */
	{
	unsigned int	allowed;
	static int pipetypes[4] = {
		PIPE_CONTROL, PIPE_ISOCHRONOUS, PIPE_BULK, PIPE_INTERRUPT
	};

	/* Check that the pipe's type matches the endpoint's type */
	if (usb_pipetype(urb->pipe) != pipetypes[xfertype])
		dev_WARN(&dev->dev, "BOGUS urb xfer, pipe %x != type %x\n",
			usb_pipetype(urb->pipe), pipetypes[xfertype]);

	/* Check against a simple/standard policy */
	allowed = (URB_NO_TRANSFER_DMA_MAP | URB_NO_INTERRUPT | URB_DIR_MASK |
			URB_FREE_BUFFER);
	switch (xfertype) {
	case USB_ENDPOINT_XFER_BULK:
		if (is_out)
			allowed |= URB_ZERO_PACKET;
		/* FALLTHROUGH */
	case USB_ENDPOINT_XFER_CONTROL:
		allowed |= URB_NO_FSBR;	/* only affects UHCI */
		/* FALLTHROUGH */
	default:			/* all non-iso endpoints */
		if (!is_out)
			allowed |= URB_SHORT_NOT_OK;
		break;
	case USB_ENDPOINT_XFER_ISOC:
		allowed |= URB_ISO_ASAP;
		break;
	}
	allowed &= urb->transfer_flags;

	/* warn if submitter gave bogus flags */
	if (allowed != urb->transfer_flags)
		dev_WARN(&dev->dev, "BOGUS urb flags, %x --> %x\n",
			urb->transfer_flags, allowed);
	}
#endif
	/*
	 * Force periodic transfer intervals to be legal values that are
	 * a power of two (so HCDs don't need to).
	 *
	 * FIXME want bus->{intr,iso}_sched_horizon values here.  Each HC
	 * supports different values... this uses EHCI/UHCI defaults (and
	 * EHCI can use smaller non-default values).
	 */
	switch (xfertype) {
	case USB_ENDPOINT_XFER_ISOC:
	case USB_ENDPOINT_XFER_INT:
		/* too small? */
		switch (dev->speed) {
		case USB_SPEED_WIRELESS:
			if (urb->interval < 6)
				return -EINVAL;
			break;
		default:
			if (urb->interval <= 0)
				return -EINVAL;
			break;
		}
		/* too big? */
		switch (dev->speed) {
		case USB_SPEED_SUPER:	/* units are 125us */
			/* Handle up to 2^(16-1) microframes */
			if (urb->interval > (1 << 15))
				return -EINVAL;
			max = 1 << 15;
			break;
		case USB_SPEED_WIRELESS:
			if (urb->interval > 16)
				return -EINVAL;
			break;
		case USB_SPEED_HIGH:	/* units are microframes */
			/* NOTE usb handles 2^15 */
			if (urb->interval > (1024 * 8))
				urb->interval = 1024 * 8;
			max = 1024 * 8;
			break;
		case USB_SPEED_FULL:	/* units are frames/msec */
		case USB_SPEED_LOW:
			if (xfertype == USB_ENDPOINT_XFER_INT) {
				if (urb->interval > 255)
					return -EINVAL;
				/* NOTE ohci only handles up to 32 */
				max = 128;
			} else {
				if (urb->interval > 1024)
					urb->interval = 1024;
				/* NOTE usb and ohci handle up to 2^15 */
				max = 1024;
			}
			break;
		default:
			return -EINVAL;
		}
		if (dev->speed != USB_SPEED_WIRELESS) {
			/* Round down to a power of 2, no more than max */
			urb->interval = min(max, 1 << ilog2(urb->interval));
		}
	}

	return usb_hcd_submit_urb(urb, mem_flags);
}
EXPORT_SYMBOL_GPL(usb_submit_urb);

/*-------------------------------------------------------------------*/

/**
 * usb_unlink_urb - abort/cancel a transfer request for an endpoint
 * @urb: pointer to urb describing a previously submitted request,
 *	may be NULL
 *
 * This routine cancels an in-progress request.  URBs complete only once
 * per submission, and may be canceled only once per submission.
 * Successful cancellation means termination of @urb will be expedited
 * and the completion handler will be called with a status code
 * indicating that the request has been canceled (rather than any other
 * code).
 *
 * Drivers should not call this routine or related routines, such as
 * usb_kill_urb() or usb_unlink_anchored_urbs(), after their disconnect
 * method has returned.  The disconnect function should synchronize with
 * a driver's I/O routines to insure that all URB-related activity has
 * completed before it returns.
 *
 * This request is asynchronous, however the HCD might call the ->complete()
 * callback during unlink. Therefore when drivers call usb_unlink_urb(), they
 * must not hold any locks that may be taken by the completion function.
 * Success is indicated by returning -EINPROGRESS, at which time the URB will
 * probably not yet have been given back to the device driver. When it is
 * eventually called, the completion function will see @urb->status ==
 * -ECONNRESET.
 * Failure is indicated by usb_unlink_urb() returning any other value.
 * Unlinking will fail when @urb is not currently "linked" (i.e., it was
 * never submitted, or it was unlinked before, or the hardware is already
 * finished with it), even if the completion handler has not yet run.
 *
 * The URB must not be deallocated while this routine is running.  In
 * particular, when a driver calls this routine, it must insure that the
 * completion handler cannot deallocate the URB.
 *
 * Unlinking and Endpoint Queues:
 *
 * [The behaviors and guarantees described below do not apply to virtual
 * root hubs but only to endpoint queues for physical USB devices.]
 *
 * Host Controller Drivers (HCDs) place all the URBs for a particular
 * endpoint in a queue.  Normally the queue advances as the controller
 * hardware processes each request.  But when an URB terminates with an
 * error its queue generally stops (see below), at least until that URB's
 * completion routine returns.  It is guaranteed that a stopped queue
 * will not restart until all its unlinked URBs have been fully retired,
 * with their completion routines run, even if that's not until some time
 * after the original completion handler returns.  The same behavior and
 * guarantee apply when an URB terminates because it was unlinked.
 *
 * Bulk and interrupt endpoint queues are guaranteed to stop whenever an
 * URB terminates with any sort of error, including -ECONNRESET, -ENOENT,
 * and -EREMOTEIO.  Control endpoint queues behave the same way except
 * that they are not guaranteed to stop for -EREMOTEIO errors.  Queues
 * for isochronous endpoints are treated differently, because they must
 * advance at fixed rates.  Such queues do not stop when an URB
 * encounters an error or is unlinked.  An unlinked isochronous URB may
 * leave a gap in the stream of packets; it is undefined whether such
 * gaps can be filled in.
 *
 * Note that early termination of an URB because a short packet was
 * received will generate a -EREMOTEIO error if and only if the
 * URB_SHORT_NOT_OK flag is set.  By setting this flag, USB device
 * drivers can build deep queues for large or complex bulk transfers
 * and clean them up reliably after any sort of aborted transfer by
 * unlinking all pending URBs at the first fault.
 *
 * When a control URB terminates with an error other than -EREMOTEIO, it
 * is quite likely that the status stage of the transfer will not take
 * place.
 */
int usb_unlink_urb(struct urb *urb)
{
	if (!urb)
		return -EINVAL;
	if (!urb->dev)
		return -ENODEV;
	if (!urb->ep)
		return -EIDRM;
	return usb_hcd_unlink_urb(urb, -ECONNRESET);
}
EXPORT_SYMBOL_GPL(usb_unlink_urb);

/**
 * usb_kill_urb - cancel a transfer request and wait for it to finish
 * @urb: pointer to URB describing a previously submitted request,
 *	may be NULL
 *
 * This routine cancels an in-progress request.  It is guaranteed that
 * upon return all completion handlers will have finished and the URB
 * will be totally idle and available for reuse.  These features make
 * this an ideal way to stop I/O in a disconnect() callback or close()
 * function.  If the request has not already finished or been unlinked
 * the completion handler will see urb->status == -ENOENT.
 *
 * While the routine is running, attempts to resubmit the URB will fail
 * with error -EPERM.  Thus even if the URB's completion handler always
 * tries to resubmit, it will not succeed and the URB will become idle.
 *
 * The URB must not be deallocated while this routine is running.  In
 * particular, when a driver calls this routine, it must insure that the
 * completion handler cannot deallocate the URB.
 *
 * This routine may not be used in an interrupt context (such as a bottom
 * half or a completion handler), or when holding a spinlock, or in other
 * situations where the caller can't schedule().
 *
 * This routine should not be called by a driver after its disconnect
 * method has returned.
 */
void usb_kill_urb(struct urb *urb)
{
	might_sleep();
	if (!(urb && urb->dev && urb->ep))
		return;
	atomic_inc(&urb->reject);

	usb_hcd_unlink_urb(urb, -ENOENT);
	wait_event(usb_kill_urb_queue, atomic_read(&urb->use_count) == 0);

	atomic_dec(&urb->reject);
}
EXPORT_SYMBOL_GPL(usb_kill_urb);

/**
 * usb_poison_urb - reliably kill a transfer and prevent further use of an URB
 * @urb: pointer to URB describing a previously submitted request,
 *	may be NULL
 *
 * This routine cancels an in-progress request.  It is guaranteed that
 * upon return all completion handlers will have finished and the URB
 * will be totally idle and cannot be reused.  These features make
 * this an ideal way to stop I/O in a disconnect() callback.
 * If the request has not already finished or been unlinked
 * the completion handler will see urb->status == -ENOENT.
 *
 * After and while the routine runs, attempts to resubmit the URB will fail
 * with error -EPERM.  Thus even if the URB's completion handler always
 * tries to resubmit, it will not succeed and the URB will become idle.
 *
 * The URB must not be deallocated while this routine is running.  In
 * particular, when a driver calls this routine, it must insure that the
 * completion handler cannot deallocate the URB.
 *
 * This routine may not be used in an interrupt context (such as a bottom
 * half or a completion handler), or when holding a spinlock, or in other
 * situations where the caller can't schedule().
 *
 * This routine should not be called by a driver after its disconnect
 * method has returned.
 */
void usb_poison_urb(struct urb *urb)
{
	might_sleep();
	if (!(urb && urb->dev && urb->ep))
		return;
	atomic_inc(&urb->reject);

	usb_hcd_unlink_urb(urb, -ENOENT);
	wait_event(usb_kill_urb_queue, atomic_read(&urb->use_count) == 0);
}
EXPORT_SYMBOL_GPL(usb_poison_urb);

void usb_unpoison_urb(struct urb *urb)
{
	if (!urb)
		return;

	atomic_dec(&urb->reject);
}
EXPORT_SYMBOL_GPL(usb_unpoison_urb);

/**
 * usb_block_urb - reliably prevent further use of an URB
 * @urb: pointer to URB to be blocked, may be NULL
 *
 * After the routine has run, attempts to resubmit the URB will fail
 * with error -EPERM.  Thus even if the URB's completion handler always
 * tries to resubmit, it will not succeed and the URB will become idle.
 *
 * The URB must not be deallocated while this routine is running.  In
 * particular, when a driver calls this routine, it must insure that the
 * completion handler cannot deallocate the URB.
 */
void usb_block_urb(struct urb *urb)
{
	if (!urb)
		return;

	atomic_inc(&urb->reject);
}
EXPORT_SYMBOL_GPL(usb_block_urb);

/**
 * usb_kill_anchored_urbs - cancel transfer requests en masse
 * @anchor: anchor the requests are bound to
 *
 * this allows all outstanding URBs to be killed starting
 * from the back of the queue
 *
 * This routine should not be called by a driver after its disconnect
 * method has returned.
 */
void usb_kill_anchored_urbs(struct usb_anchor *anchor)
{
	struct urb *victim;

	spin_lock_irq(&anchor->lock);
	while (!list_empty(&anchor->urb_list)) {
		victim = list_entry(anchor->urb_list.prev, struct urb,
				    anchor_list);
		/* we must make sure the URB isn't freed before we kill it*/
		usb_get_urb(victim);
		spin_unlock_irq(&anchor->lock);
		/* this will unanchor the URB */
		usb_kill_urb(victim);
		usb_put_urb(victim);
		spin_lock_irq(&anchor->lock);
	}
	spin_unlock_irq(&anchor->lock);
}
EXPORT_SYMBOL_GPL(usb_kill_anchored_urbs);


/**
 * usb_poison_anchored_urbs - cease all traffic from an anchor
 * @anchor: anchor the requests are bound to
 *
 * this allows all outstanding URBs to be poisoned starting
 * from the back of the queue. Newly added URBs will also be
 * poisoned
 *
 * This routine should not be called by a driver after its disconnect
 * method has returned.
 */
void usb_poison_anchored_urbs(struct usb_anchor *anchor)
{
	struct urb *victim;

	spin_lock_irq(&anchor->lock);
	anchor->poisoned = 1;
	while (!list_empty(&anchor->urb_list)) {
		victim = list_entry(anchor->urb_list.prev, struct urb,
				    anchor_list);
		/* we must make sure the URB isn't freed before we kill it*/
		usb_get_urb(victim);
		spin_unlock_irq(&anchor->lock);
		/* this will unanchor the URB */
		usb_poison_urb(victim);
		usb_put_urb(victim);
		spin_lock_irq(&anchor->lock);
	}
	spin_unlock_irq(&anchor->lock);
}
EXPORT_SYMBOL_GPL(usb_poison_anchored_urbs);

/**
 * usb_unpoison_anchored_urbs - let an anchor be used successfully again
 * @anchor: anchor the requests are bound to
 *
 * Reverses the effect of usb_poison_anchored_urbs
 * the anchor can be used normally after it returns
 */
void usb_unpoison_anchored_urbs(struct usb_anchor *anchor)
{
	unsigned long flags;
	struct urb *lazarus;

	spin_lock_irqsave(&anchor->lock, flags);
	list_for_each_entry(lazarus, &anchor->urb_list, anchor_list) {
		usb_unpoison_urb(lazarus);
	}
	anchor->poisoned = 0;
	spin_unlock_irqrestore(&anchor->lock, flags);
}
EXPORT_SYMBOL_GPL(usb_unpoison_anchored_urbs);
/**
 * usb_unlink_anchored_urbs - asynchronously cancel transfer requests en masse
 * @anchor: anchor the requests are bound to
 *
 * this allows all outstanding URBs to be unlinked starting
 * from the back of the queue. This function is asynchronous.
 * The unlinking is just tiggered. It may happen after this
 * function has returned.
 *
 * This routine should not be called by a driver after its disconnect
 * method has returned.
 */
void usb_unlink_anchored_urbs(struct usb_anchor *anchor)
{
	struct urb *victim;

	while ((victim = usb_get_from_anchor(anchor)) != NULL) {
		usb_unlink_urb(victim);
		usb_put_urb(victim);
	}
}
EXPORT_SYMBOL_GPL(usb_unlink_anchored_urbs);

/**
 * usb_wait_anchor_empty_timeout - wait for an anchor to be unused
 * @anchor: the anchor you want to become unused
 * @timeout: how long you are willing to wait in milliseconds
 *
 * Call this is you want to be sure all an anchor's
 * URBs have finished
 */
int usb_wait_anchor_empty_timeout(struct usb_anchor *anchor,
				  unsigned int timeout)
{
	return wait_event_timeout(anchor->wait, list_empty(&anchor->urb_list),
				  msecs_to_jiffies(timeout));
}
EXPORT_SYMBOL_GPL(usb_wait_anchor_empty_timeout);

/**
 * usb_get_from_anchor - get an anchor's oldest urb
 * @anchor: the anchor whose urb you want
 *
 * this will take the oldest urb from an anchor,
 * unanchor and return it
 */
struct urb *usb_get_from_anchor(struct usb_anchor *anchor)
{
	struct urb *victim;
	unsigned long flags;

	spin_lock_irqsave(&anchor->lock, flags);
	if (!list_empty(&anchor->urb_list)) {
		victim = list_entry(anchor->urb_list.next, struct urb,
				    anchor_list);
		usb_get_urb(victim);
		__usb_unanchor_urb(victim, anchor);
	} else {
		victim = NULL;
	}
	spin_unlock_irqrestore(&anchor->lock, flags);

	return victim;
}

EXPORT_SYMBOL_GPL(usb_get_from_anchor);

/**
 * usb_scuttle_anchored_urbs - unanchor all an anchor's urbs
 * @anchor: the anchor whose urbs you want to unanchor
 *
 * use this to get rid of all an anchor's urbs
 */
void usb_scuttle_anchored_urbs(struct usb_anchor *anchor)
{
	struct urb *victim;
	unsigned long flags;

	spin_lock_irqsave(&anchor->lock, flags);
	while (!list_empty(&anchor->urb_list)) {
		victim = list_entry(anchor->urb_list.prev, struct urb,
				    anchor_list);
		__usb_unanchor_urb(victim, anchor);
	}
	spin_unlock_irqrestore(&anchor->lock, flags);
}

EXPORT_SYMBOL_GPL(usb_scuttle_anchored_urbs);

/**
 * usb_anchor_empty - is an anchor empty
 * @anchor: the anchor you want to query
 *
 * returns 1 if the anchor has no urbs associated with it
 */
int usb_anchor_empty(struct usb_anchor *anchor)
{
	return list_empty(&anchor->urb_list);
}

EXPORT_SYMBOL_GPL(usb_anchor_empty);

