/*
 * message.c - synchronous message handling
 */

#include <linux/config.h>
#include <linux/pci.h>	/* for scatterlist macros */
#include <linux/usb.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include <asm/byteorder.h>

#include "hcd.h"	/* for usbcore internals */
#include "usb.h"

static void usb_api_blocking_completion(struct urb *urb, struct pt_regs *regs)
{
	complete((struct completion *)urb->context);
}


static void timeout_kill(unsigned long data)
{
	struct urb	*urb = (struct urb *) data;

	usb_unlink_urb(urb);
}

// Starts urb and waits for completion or timeout
// note that this call is NOT interruptible, while
// many device driver i/o requests should be interruptible
static int usb_start_wait_urb(struct urb *urb, int timeout, int* actual_length)
{ 
	struct completion	done;
	struct timer_list	timer;
	int			status;

	init_completion(&done); 	
	urb->context = &done;
	urb->actual_length = 0;
	status = usb_submit_urb(urb, GFP_NOIO);

	if (status == 0) {
		if (timeout > 0) {
			init_timer(&timer);
			timer.expires = jiffies + msecs_to_jiffies(timeout);
			timer.data = (unsigned long)urb;
			timer.function = timeout_kill;
			/* grr.  timeout _should_ include submit delays. */
			add_timer(&timer);
		}
		wait_for_completion(&done);
		status = urb->status;
		/* note:  HCDs return ETIMEDOUT for other reasons too */
		if (status == -ECONNRESET) {
			dev_dbg(&urb->dev->dev,
				"%s timed out on ep%d%s len=%d/%d\n",
				current->comm,
				usb_pipeendpoint(urb->pipe),
				usb_pipein(urb->pipe) ? "in" : "out",
				urb->actual_length,
				urb->transfer_buffer_length
				);
			if (urb->actual_length > 0)
				status = 0;
			else
				status = -ETIMEDOUT;
		}
		if (timeout > 0)
			del_timer_sync(&timer);
	}

	if (actual_length)
		*actual_length = urb->actual_length;
	usb_free_urb(urb);
	return status;
}

/*-------------------------------------------------------------------*/
// returns status (negative) or length (positive)
static int usb_internal_control_msg(struct usb_device *usb_dev,
				    unsigned int pipe, 
				    struct usb_ctrlrequest *cmd,
				    void *data, int len, int timeout)
{
	struct urb *urb;
	int retv;
	int length;

	urb = usb_alloc_urb(0, GFP_NOIO);
	if (!urb)
		return -ENOMEM;
  
	usb_fill_control_urb(urb, usb_dev, pipe, (unsigned char *)cmd, data,
			     len, usb_api_blocking_completion, NULL);

	retv = usb_start_wait_urb(urb, timeout, &length);
	if (retv < 0)
		return retv;
	else
		return length;
}

/**
 *	usb_control_msg - Builds a control urb, sends it off and waits for completion
 *	@dev: pointer to the usb device to send the message to
 *	@pipe: endpoint "pipe" to send the message to
 *	@request: USB message request value
 *	@requesttype: USB message request type value
 *	@value: USB message value
 *	@index: USB message index value
 *	@data: pointer to the data to send
 *	@size: length in bytes of the data to send
 *	@timeout: time in msecs to wait for the message to complete before
 *		timing out (if 0 the wait is forever)
 *	Context: !in_interrupt ()
 *
 *	This function sends a simple control message to a specified endpoint
 *	and waits for the message to complete, or timeout.
 *	
 *	If successful, it returns the number of bytes transferred, otherwise a negative error number.
 *
 *	Don't use this function from within an interrupt context, like a
 *	bottom half handler.  If you need an asynchronous message, or need to send
 *	a message from within interrupt context, use usb_submit_urb()
 *      If a thread in your driver uses this call, make sure your disconnect()
 *      method can wait for it to complete.  Since you don't have a handle on
 *      the URB used, you can't cancel the request.
 */
int usb_control_msg(struct usb_device *dev, unsigned int pipe, __u8 request, __u8 requesttype,
			 __u16 value, __u16 index, void *data, __u16 size, int timeout)
{
	struct usb_ctrlrequest *dr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_NOIO);
	int ret;
	
	if (!dr)
		return -ENOMEM;

	dr->bRequestType= requesttype;
	dr->bRequest = request;
	dr->wValue = cpu_to_le16p(&value);
	dr->wIndex = cpu_to_le16p(&index);
	dr->wLength = cpu_to_le16p(&size);

	//dbg("usb_control_msg");	

	ret = usb_internal_control_msg(dev, pipe, dr, data, size, timeout);

	kfree(dr);

	return ret;
}


/**
 *	usb_bulk_msg - Builds a bulk urb, sends it off and waits for completion
 *	@usb_dev: pointer to the usb device to send the message to
 *	@pipe: endpoint "pipe" to send the message to
 *	@data: pointer to the data to send
 *	@len: length in bytes of the data to send
 *	@actual_length: pointer to a location to put the actual length transferred in bytes
 *	@timeout: time in msecs to wait for the message to complete before
 *		timing out (if 0 the wait is forever)
 *	Context: !in_interrupt ()
 *
 *	This function sends a simple bulk message to a specified endpoint
 *	and waits for the message to complete, or timeout.
 *	
 *	If successful, it returns 0, otherwise a negative error number.
 *	The number of actual bytes transferred will be stored in the 
 *	actual_length paramater.
 *
 *	Don't use this function from within an interrupt context, like a
 *	bottom half handler.  If you need an asynchronous message, or need to
 *	send a message from within interrupt context, use usb_submit_urb()
 *      If a thread in your driver uses this call, make sure your disconnect()
 *      method can wait for it to complete.  Since you don't have a handle on
 *      the URB used, you can't cancel the request.
 *
 *	Because there is no usb_interrupt_msg() and no USBDEVFS_INTERRUPT
 *	ioctl, users are forced to abuse this routine by using it to submit
 *	URBs for interrupt endpoints.  We will take the liberty of creating
 *	an interrupt URB (with the default interval) if the target is an
 *	interrupt endpoint.
 */
int usb_bulk_msg(struct usb_device *usb_dev, unsigned int pipe, 
			void *data, int len, int *actual_length, int timeout)
{
	struct urb *urb;
	struct usb_host_endpoint *ep;

	ep = (usb_pipein(pipe) ? usb_dev->ep_in : usb_dev->ep_out)
			[usb_pipeendpoint(pipe)];
	if (!ep || len < 0)
		return -EINVAL;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;

	if ((ep->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
			USB_ENDPOINT_XFER_INT) {
		pipe = (pipe & ~(3 << 30)) | (PIPE_INTERRUPT << 30);
		usb_fill_int_urb(urb, usb_dev, pipe, data, len,
				usb_api_blocking_completion, NULL,
				ep->desc.bInterval);
	} else
		usb_fill_bulk_urb(urb, usb_dev, pipe, data, len,
				usb_api_blocking_completion, NULL);

	return usb_start_wait_urb(urb, timeout, actual_length);
}

/*-------------------------------------------------------------------*/

static void sg_clean (struct usb_sg_request *io)
{
	if (io->urbs) {
		while (io->entries--)
			usb_free_urb (io->urbs [io->entries]);
		kfree (io->urbs);
		io->urbs = NULL;
	}
	if (io->dev->dev.dma_mask != NULL)
		usb_buffer_unmap_sg (io->dev, io->pipe, io->sg, io->nents);
	io->dev = NULL;
}

static void sg_complete (struct urb *urb, struct pt_regs *regs)
{
	struct usb_sg_request	*io = (struct usb_sg_request *) urb->context;

	spin_lock (&io->lock);

	/* In 2.5 we require hcds' endpoint queues not to progress after fault
	 * reports, until the completion callback (this!) returns.  That lets
	 * device driver code (like this routine) unlink queued urbs first,
	 * if it needs to, since the HC won't work on them at all.  So it's
	 * not possible for page N+1 to overwrite page N, and so on.
	 *
	 * That's only for "hard" faults; "soft" faults (unlinks) sometimes
	 * complete before the HCD can get requests away from hardware,
	 * though never during cleanup after a hard fault.
	 */
	if (io->status
			&& (io->status != -ECONNRESET
				|| urb->status != -ECONNRESET)
			&& urb->actual_length) {
		dev_err (io->dev->bus->controller,
			"dev %s ep%d%s scatterlist error %d/%d\n",
			io->dev->devpath,
			usb_pipeendpoint (urb->pipe),
			usb_pipein (urb->pipe) ? "in" : "out",
			urb->status, io->status);
		// BUG ();
	}

	if (io->status == 0 && urb->status && urb->status != -ECONNRESET) {
		int		i, found, status;

		io->status = urb->status;

		/* the previous urbs, and this one, completed already.
		 * unlink pending urbs so they won't rx/tx bad data.
		 * careful: unlink can sometimes be synchronous...
		 */
		spin_unlock (&io->lock);
		for (i = 0, found = 0; i < io->entries; i++) {
			if (!io->urbs [i] || !io->urbs [i]->dev)
				continue;
			if (found) {
				status = usb_unlink_urb (io->urbs [i]);
				if (status != -EINPROGRESS
						&& status != -ENODEV
						&& status != -EBUSY)
					dev_err (&io->dev->dev,
						"%s, unlink --> %d\n",
						__FUNCTION__, status);
			} else if (urb == io->urbs [i])
				found = 1;
		}
		spin_lock (&io->lock);
	}
	urb->dev = NULL;

	/* on the last completion, signal usb_sg_wait() */
	io->bytes += urb->actual_length;
	io->count--;
	if (!io->count)
		complete (&io->complete);

	spin_unlock (&io->lock);
}


/**
 * usb_sg_init - initializes scatterlist-based bulk/interrupt I/O request
 * @io: request block being initialized.  until usb_sg_wait() returns,
 *	treat this as a pointer to an opaque block of memory,
 * @dev: the usb device that will send or receive the data
 * @pipe: endpoint "pipe" used to transfer the data
 * @period: polling rate for interrupt endpoints, in frames or
 * 	(for high speed endpoints) microframes; ignored for bulk
 * @sg: scatterlist entries
 * @nents: how many entries in the scatterlist
 * @length: how many bytes to send from the scatterlist, or zero to
 * 	send every byte identified in the list.
 * @mem_flags: SLAB_* flags affecting memory allocations in this call
 *
 * Returns zero for success, else a negative errno value.  This initializes a
 * scatter/gather request, allocating resources such as I/O mappings and urb
 * memory (except maybe memory used by USB controller drivers).
 *
 * The request must be issued using usb_sg_wait(), which waits for the I/O to
 * complete (or to be canceled) and then cleans up all resources allocated by
 * usb_sg_init().
 *
 * The request may be canceled with usb_sg_cancel(), either before or after
 * usb_sg_wait() is called.
 */
int usb_sg_init (
	struct usb_sg_request	*io,
	struct usb_device	*dev,
	unsigned		pipe, 
	unsigned		period,
	struct scatterlist	*sg,
	int			nents,
	size_t			length,
	gfp_t			mem_flags
)
{
	int			i;
	int			urb_flags;
	int			dma;

	if (!io || !dev || !sg
			|| usb_pipecontrol (pipe)
			|| usb_pipeisoc (pipe)
			|| nents <= 0)
		return -EINVAL;

	spin_lock_init (&io->lock);
	io->dev = dev;
	io->pipe = pipe;
	io->sg = sg;
	io->nents = nents;

	/* not all host controllers use DMA (like the mainstream pci ones);
	 * they can use PIO (sl811) or be software over another transport.
	 */
	dma = (dev->dev.dma_mask != NULL);
	if (dma)
		io->entries = usb_buffer_map_sg (dev, pipe, sg, nents);
	else
		io->entries = nents;

	/* initialize all the urbs we'll use */
	if (io->entries <= 0)
		return io->entries;

	io->count = io->entries;
	io->urbs = kmalloc (io->entries * sizeof *io->urbs, mem_flags);
	if (!io->urbs)
		goto nomem;

	urb_flags = URB_NO_TRANSFER_DMA_MAP | URB_NO_INTERRUPT;
	if (usb_pipein (pipe))
		urb_flags |= URB_SHORT_NOT_OK;

	for (i = 0; i < io->entries; i++) {
		unsigned		len;

		io->urbs [i] = usb_alloc_urb (0, mem_flags);
		if (!io->urbs [i]) {
			io->entries = i;
			goto nomem;
		}

		io->urbs [i]->dev = NULL;
		io->urbs [i]->pipe = pipe;
		io->urbs [i]->interval = period;
		io->urbs [i]->transfer_flags = urb_flags;

		io->urbs [i]->complete = sg_complete;
		io->urbs [i]->context = io;
		io->urbs [i]->status = -EINPROGRESS;
		io->urbs [i]->actual_length = 0;

		if (dma) {
			/* hc may use _only_ transfer_dma */
			io->urbs [i]->transfer_dma = sg_dma_address (sg + i);
			len = sg_dma_len (sg + i);
		} else {
			/* hc may use _only_ transfer_buffer */
			io->urbs [i]->transfer_buffer =
				page_address (sg [i].page) + sg [i].offset;
			len = sg [i].length;
		}

		if (length) {
			len = min_t (unsigned, len, length);
			length -= len;
			if (length == 0)
				io->entries = i + 1;
		}
		io->urbs [i]->transfer_buffer_length = len;
	}
	io->urbs [--i]->transfer_flags &= ~URB_NO_INTERRUPT;

	/* transaction state */
	io->status = 0;
	io->bytes = 0;
	init_completion (&io->complete);
	return 0;

nomem:
	sg_clean (io);
	return -ENOMEM;
}


/**
 * usb_sg_wait - synchronously execute scatter/gather request
 * @io: request block handle, as initialized with usb_sg_init().
 * 	some fields become accessible when this call returns.
 * Context: !in_interrupt ()
 *
 * This function blocks until the specified I/O operation completes.  It
 * leverages the grouping of the related I/O requests to get good transfer
 * rates, by queueing the requests.  At higher speeds, such queuing can
 * significantly improve USB throughput.
 *
 * There are three kinds of completion for this function.
 * (1) success, where io->status is zero.  The number of io->bytes
 *     transferred is as requested.
 * (2) error, where io->status is a negative errno value.  The number
 *     of io->bytes transferred before the error is usually less
 *     than requested, and can be nonzero.
 * (3) cancellation, a type of error with status -ECONNRESET that
 *     is initiated by usb_sg_cancel().
 *
 * When this function returns, all memory allocated through usb_sg_init() or
 * this call will have been freed.  The request block parameter may still be
 * passed to usb_sg_cancel(), or it may be freed.  It could also be
 * reinitialized and then reused.
 *
 * Data Transfer Rates:
 *
 * Bulk transfers are valid for full or high speed endpoints.
 * The best full speed data rate is 19 packets of 64 bytes each
 * per frame, or 1216 bytes per millisecond.
 * The best high speed data rate is 13 packets of 512 bytes each
 * per microframe, or 52 KBytes per millisecond.
 *
 * The reason to use interrupt transfers through this API would most likely
 * be to reserve high speed bandwidth, where up to 24 KBytes per millisecond
 * could be transferred.  That capability is less useful for low or full
 * speed interrupt endpoints, which allow at most one packet per millisecond,
 * of at most 8 or 64 bytes (respectively).
 */
void usb_sg_wait (struct usb_sg_request *io)
{
	int		i, entries = io->entries;

	/* queue the urbs.  */
	spin_lock_irq (&io->lock);
	for (i = 0; i < entries && !io->status; i++) {
		int	retval;

		io->urbs [i]->dev = io->dev;
		retval = usb_submit_urb (io->urbs [i], SLAB_ATOMIC);

		/* after we submit, let completions or cancelations fire;
		 * we handshake using io->status.
		 */
		spin_unlock_irq (&io->lock);
		switch (retval) {
			/* maybe we retrying will recover */
		case -ENXIO:	// hc didn't queue this one
		case -EAGAIN:
		case -ENOMEM:
			io->urbs[i]->dev = NULL;
			retval = 0;
			i--;
			yield ();
			break;

			/* no error? continue immediately.
			 *
			 * NOTE: to work better with UHCI (4K I/O buffer may
			 * need 3K of TDs) it may be good to limit how many
			 * URBs are queued at once; N milliseconds?
			 */
		case 0:
			cpu_relax ();
			break;

			/* fail any uncompleted urbs */
		default:
			io->urbs [i]->dev = NULL;
			io->urbs [i]->status = retval;
			dev_dbg (&io->dev->dev, "%s, submit --> %d\n",
				__FUNCTION__, retval);
			usb_sg_cancel (io);
		}
		spin_lock_irq (&io->lock);
		if (retval && (io->status == 0 || io->status == -ECONNRESET))
			io->status = retval;
	}
	io->count -= entries - i;
	if (io->count == 0)
		complete (&io->complete);
	spin_unlock_irq (&io->lock);

	/* OK, yes, this could be packaged as non-blocking.
	 * So could the submit loop above ... but it's easier to
	 * solve neither problem than to solve both!
	 */
	wait_for_completion (&io->complete);

	sg_clean (io);
}

/**
 * usb_sg_cancel - stop scatter/gather i/o issued by usb_sg_wait()
 * @io: request block, initialized with usb_sg_init()
 *
 * This stops a request after it has been started by usb_sg_wait().
 * It can also prevents one initialized by usb_sg_init() from starting,
 * so that call just frees resources allocated to the request.
 */
void usb_sg_cancel (struct usb_sg_request *io)
{
	unsigned long	flags;

	spin_lock_irqsave (&io->lock, flags);

	/* shut everything down, if it didn't already */
	if (!io->status) {
		int	i;

		io->status = -ECONNRESET;
		spin_unlock (&io->lock);
		for (i = 0; i < io->entries; i++) {
			int	retval;

			if (!io->urbs [i]->dev)
				continue;
			retval = usb_unlink_urb (io->urbs [i]);
			if (retval != -EINPROGRESS && retval != -EBUSY)
				dev_warn (&io->dev->dev, "%s, unlink --> %d\n",
					__FUNCTION__, retval);
		}
		spin_lock (&io->lock);
	}
	spin_unlock_irqrestore (&io->lock, flags);
}

/*-------------------------------------------------------------------*/

/**
 * usb_get_descriptor - issues a generic GET_DESCRIPTOR request
 * @dev: the device whose descriptor is being retrieved
 * @type: the descriptor type (USB_DT_*)
 * @index: the number of the descriptor
 * @buf: where to put the descriptor
 * @size: how big is "buf"?
 * Context: !in_interrupt ()
 *
 * Gets a USB descriptor.  Convenience functions exist to simplify
 * getting some types of descriptors.  Use
 * usb_get_string() or usb_string() for USB_DT_STRING.
 * Device (USB_DT_DEVICE) and configuration descriptors (USB_DT_CONFIG)
 * are part of the device structure.
 * In addition to a number of USB-standard descriptors, some
 * devices also use class-specific or vendor-specific descriptors.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns the number of bytes received on success, or else the status code
 * returned by the underlying usb_control_msg() call.
 */
int usb_get_descriptor(struct usb_device *dev, unsigned char type, unsigned char index, void *buf, int size)
{
	int i;
	int result;
	
	memset(buf,0,size);	// Make sure we parse really received data

	for (i = 0; i < 3; ++i) {
		/* retry on length 0 or stall; some devices are flakey */
		result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
				USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
				(type << 8) + index, 0, buf, size,
				USB_CTRL_GET_TIMEOUT);
		if (result == 0 || result == -EPIPE)
			continue;
		if (result > 1 && ((u8 *)buf)[1] != type) {
			result = -EPROTO;
			continue;
		}
		break;
	}
	return result;
}

/**
 * usb_get_string - gets a string descriptor
 * @dev: the device whose string descriptor is being retrieved
 * @langid: code for language chosen (from string descriptor zero)
 * @index: the number of the descriptor
 * @buf: where to put the string
 * @size: how big is "buf"?
 * Context: !in_interrupt ()
 *
 * Retrieves a string, encoded using UTF-16LE (Unicode, 16 bits per character,
 * in little-endian byte order).
 * The usb_string() function will often be a convenient way to turn
 * these strings into kernel-printable form.
 *
 * Strings may be referenced in device, configuration, interface, or other
 * descriptors, and could also be used in vendor-specific ways.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns the number of bytes received on success, or else the status code
 * returned by the underlying usb_control_msg() call.
 */
int usb_get_string(struct usb_device *dev, unsigned short langid,
		unsigned char index, void *buf, int size)
{
	int i;
	int result;

	for (i = 0; i < 3; ++i) {
		/* retry on length 0 or stall; some devices are flakey */
		result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
			(USB_DT_STRING << 8) + index, langid, buf, size,
			USB_CTRL_GET_TIMEOUT);
		if (!(result == 0 || result == -EPIPE))
			break;
	}
	return result;
}

static void usb_try_string_workarounds(unsigned char *buf, int *length)
{
	int newlength, oldlength = *length;

	for (newlength = 2; newlength + 1 < oldlength; newlength += 2)
		if (!isprint(buf[newlength]) || buf[newlength + 1])
			break;

	if (newlength > 2) {
		buf[0] = newlength;
		*length = newlength;
	}
}

static int usb_string_sub(struct usb_device *dev, unsigned int langid,
		unsigned int index, unsigned char *buf)
{
	int rc;

	/* Try to read the string descriptor by asking for the maximum
	 * possible number of bytes */
	rc = usb_get_string(dev, langid, index, buf, 255);

	/* If that failed try to read the descriptor length, then
	 * ask for just that many bytes */
	if (rc < 2) {
		rc = usb_get_string(dev, langid, index, buf, 2);
		if (rc == 2)
			rc = usb_get_string(dev, langid, index, buf, buf[0]);
	}

	if (rc >= 2) {
		if (!buf[0] && !buf[1])
			usb_try_string_workarounds(buf, &rc);

		/* There might be extra junk at the end of the descriptor */
		if (buf[0] < rc)
			rc = buf[0];

		rc = rc - (rc & 1); /* force a multiple of two */
	}

	if (rc < 2)
		rc = (rc < 0 ? rc : -EINVAL);

	return rc;
}

/**
 * usb_string - returns ISO 8859-1 version of a string descriptor
 * @dev: the device whose string descriptor is being retrieved
 * @index: the number of the descriptor
 * @buf: where to put the string
 * @size: how big is "buf"?
 * Context: !in_interrupt ()
 * 
 * This converts the UTF-16LE encoded strings returned by devices, from
 * usb_get_string_descriptor(), to null-terminated ISO-8859-1 encoded ones
 * that are more usable in most kernel contexts.  Note that all characters
 * in the chosen descriptor that can't be encoded using ISO-8859-1
 * are converted to the question mark ("?") character, and this function
 * chooses strings in the first language supported by the device.
 *
 * The ASCII (or, redundantly, "US-ASCII") character set is the seven-bit
 * subset of ISO 8859-1. ISO-8859-1 is the eight-bit subset of Unicode,
 * and is appropriate for use many uses of English and several other
 * Western European languages.  (But it doesn't include the "Euro" symbol.)
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns length of the string (>= 0) or usb_control_msg status (< 0).
 */
int usb_string(struct usb_device *dev, int index, char *buf, size_t size)
{
	unsigned char *tbuf;
	int err;
	unsigned int u, idx;

	if (dev->state == USB_STATE_SUSPENDED)
		return -EHOSTUNREACH;
	if (size <= 0 || !buf || !index)
		return -EINVAL;
	buf[0] = 0;
	tbuf = kmalloc(256, GFP_KERNEL);
	if (!tbuf)
		return -ENOMEM;

	/* get langid for strings if it's not yet known */
	if (!dev->have_langid) {
		err = usb_string_sub(dev, 0, 0, tbuf);
		if (err < 0) {
			dev_err (&dev->dev,
				"string descriptor 0 read error: %d\n",
				err);
			goto errout;
		} else if (err < 4) {
			dev_err (&dev->dev, "string descriptor 0 too short\n");
			err = -EINVAL;
			goto errout;
		} else {
			dev->have_langid = -1;
			dev->string_langid = tbuf[2] | (tbuf[3]<< 8);
				/* always use the first langid listed */
			dev_dbg (&dev->dev, "default language 0x%04x\n",
				dev->string_langid);
		}
	}
	
	err = usb_string_sub(dev, dev->string_langid, index, tbuf);
	if (err < 0)
		goto errout;

	size--;		/* leave room for trailing NULL char in output buffer */
	for (idx = 0, u = 2; u < err; u += 2) {
		if (idx >= size)
			break;
		if (tbuf[u+1])			/* high byte */
			buf[idx++] = '?';  /* non ISO-8859-1 character */
		else
			buf[idx++] = tbuf[u];
	}
	buf[idx] = 0;
	err = idx;

	if (tbuf[1] != USB_DT_STRING)
		dev_dbg(&dev->dev, "wrong descriptor type %02x for string %d (\"%s\")\n", tbuf[1], index, buf);

 errout:
	kfree(tbuf);
	return err;
}

/**
 * usb_cache_string - read a string descriptor and cache it for later use
 * @udev: the device whose string descriptor is being read
 * @index: the descriptor index
 *
 * Returns a pointer to a kmalloc'ed buffer containing the descriptor string,
 * or NULL if the index is 0 or the string could not be read.
 */
char *usb_cache_string(struct usb_device *udev, int index)
{
	char *buf;
	char *smallbuf = NULL;
	int len;

	if (index > 0 && (buf = kmalloc(256, GFP_KERNEL)) != NULL) {
		if ((len = usb_string(udev, index, buf, 256)) > 0) {
			if ((smallbuf = kmalloc(++len, GFP_KERNEL)) == NULL)
				return buf;
			memcpy(smallbuf, buf, len);
		}
		kfree(buf);
	}
	return smallbuf;
}

/*
 * usb_get_device_descriptor - (re)reads the device descriptor (usbcore)
 * @dev: the device whose device descriptor is being updated
 * @size: how much of the descriptor to read
 * Context: !in_interrupt ()
 *
 * Updates the copy of the device descriptor stored in the device structure,
 * which dedicates space for this purpose.  Note that several fields are
 * converted to the host CPU's byte order:  the USB version (bcdUSB), and
 * vendors product and version fields (idVendor, idProduct, and bcdDevice).
 * That lets device drivers compare against non-byteswapped constants.
 *
 * Not exported, only for use by the core.  If drivers really want to read
 * the device descriptor directly, they can call usb_get_descriptor() with
 * type = USB_DT_DEVICE and index = 0.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns the number of bytes received on success, or else the status code
 * returned by the underlying usb_control_msg() call.
 */
int usb_get_device_descriptor(struct usb_device *dev, unsigned int size)
{
	struct usb_device_descriptor *desc;
	int ret;

	if (size > sizeof(*desc))
		return -EINVAL;
	desc = kmalloc(sizeof(*desc), GFP_NOIO);
	if (!desc)
		return -ENOMEM;

	ret = usb_get_descriptor(dev, USB_DT_DEVICE, 0, desc, size);
	if (ret >= 0) 
		memcpy(&dev->descriptor, desc, size);
	kfree(desc);
	return ret;
}

/**
 * usb_get_status - issues a GET_STATUS call
 * @dev: the device whose status is being checked
 * @type: USB_RECIP_*; for device, interface, or endpoint
 * @target: zero (for device), else interface or endpoint number
 * @data: pointer to two bytes of bitmap data
 * Context: !in_interrupt ()
 *
 * Returns device, interface, or endpoint status.  Normally only of
 * interest to see if the device is self powered, or has enabled the
 * remote wakeup facility; or whether a bulk or interrupt endpoint
 * is halted ("stalled").
 *
 * Bits in these status bitmaps are set using the SET_FEATURE request,
 * and cleared using the CLEAR_FEATURE request.  The usb_clear_halt()
 * function should be used to clear halt ("stall") status.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns the number of bytes received on success, or else the status code
 * returned by the underlying usb_control_msg() call.
 */
int usb_get_status(struct usb_device *dev, int type, int target, void *data)
{
	int ret;
	u16 *status = kmalloc(sizeof(*status), GFP_KERNEL);

	if (!status)
		return -ENOMEM;

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_STATUS, USB_DIR_IN | type, 0, target, status,
		sizeof(*status), USB_CTRL_GET_TIMEOUT);

	*(u16 *)data = *status;
	kfree(status);
	return ret;
}

/**
 * usb_clear_halt - tells device to clear endpoint halt/stall condition
 * @dev: device whose endpoint is halted
 * @pipe: endpoint "pipe" being cleared
 * Context: !in_interrupt ()
 *
 * This is used to clear halt conditions for bulk and interrupt endpoints,
 * as reported by URB completion status.  Endpoints that are halted are
 * sometimes referred to as being "stalled".  Such endpoints are unable
 * to transmit or receive data until the halt status is cleared.  Any URBs
 * queued for such an endpoint should normally be unlinked by the driver
 * before clearing the halt condition, as described in sections 5.7.5
 * and 5.8.5 of the USB 2.0 spec.
 *
 * Note that control and isochronous endpoints don't halt, although control
 * endpoints report "protocol stall" (for unsupported requests) using the
 * same status code used to report a true stall.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns zero on success, or else the status code returned by the
 * underlying usb_control_msg() call.
 */
int usb_clear_halt(struct usb_device *dev, int pipe)
{
	int result;
	int endp = usb_pipeendpoint(pipe);
	
	if (usb_pipein (pipe))
		endp |= USB_DIR_IN;

	/* we don't care if it wasn't halted first. in fact some devices
	 * (like some ibmcam model 1 units) seem to expect hosts to make
	 * this request for iso endpoints, which can't halt!
	 */
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RECIP_ENDPOINT,
		USB_ENDPOINT_HALT, endp, NULL, 0,
		USB_CTRL_SET_TIMEOUT);

	/* don't un-halt or force to DATA0 except on success */
	if (result < 0)
		return result;

	/* NOTE:  seems like Microsoft and Apple don't bother verifying
	 * the clear "took", so some devices could lock up if you check...
	 * such as the Hagiwara FlashGate DUAL.  So we won't bother.
	 *
	 * NOTE:  make sure the logic here doesn't diverge much from
	 * the copy in usb-storage, for as long as we need two copies.
	 */

	/* toggle was reset by the clear */
	usb_settoggle(dev, usb_pipeendpoint(pipe), usb_pipeout(pipe), 0);

	return 0;
}

/**
 * usb_disable_endpoint -- Disable an endpoint by address
 * @dev: the device whose endpoint is being disabled
 * @epaddr: the endpoint's address.  Endpoint number for output,
 *	endpoint number + USB_DIR_IN for input
 *
 * Deallocates hcd/hardware state for this endpoint ... and nukes all
 * pending urbs.
 *
 * If the HCD hasn't registered a disable() function, this sets the
 * endpoint's maxpacket size to 0 to prevent further submissions.
 */
void usb_disable_endpoint(struct usb_device *dev, unsigned int epaddr)
{
	unsigned int epnum = epaddr & USB_ENDPOINT_NUMBER_MASK;
	struct usb_host_endpoint *ep;

	if (!dev)
		return;

	if (usb_endpoint_out(epaddr)) {
		ep = dev->ep_out[epnum];
		dev->ep_out[epnum] = NULL;
	} else {
		ep = dev->ep_in[epnum];
		dev->ep_in[epnum] = NULL;
	}
	if (ep && dev->bus && dev->bus->op && dev->bus->op->disable)
		dev->bus->op->disable(dev, ep);
}

/**
 * usb_disable_interface -- Disable all endpoints for an interface
 * @dev: the device whose interface is being disabled
 * @intf: pointer to the interface descriptor
 *
 * Disables all the endpoints for the interface's current altsetting.
 */
void usb_disable_interface(struct usb_device *dev, struct usb_interface *intf)
{
	struct usb_host_interface *alt = intf->cur_altsetting;
	int i;

	for (i = 0; i < alt->desc.bNumEndpoints; ++i) {
		usb_disable_endpoint(dev,
				alt->endpoint[i].desc.bEndpointAddress);
	}
}

/*
 * usb_disable_device - Disable all the endpoints for a USB device
 * @dev: the device whose endpoints are being disabled
 * @skip_ep0: 0 to disable endpoint 0, 1 to skip it.
 *
 * Disables all the device's endpoints, potentially including endpoint 0.
 * Deallocates hcd/hardware state for the endpoints (nuking all or most
 * pending urbs) and usbcore state for the interfaces, so that usbcore
 * must usb_set_configuration() before any interfaces could be used.
 */
void usb_disable_device(struct usb_device *dev, int skip_ep0)
{
	int i;

	dev_dbg(&dev->dev, "%s nuking %s URBs\n", __FUNCTION__,
			skip_ep0 ? "non-ep0" : "all");
	for (i = skip_ep0; i < 16; ++i) {
		usb_disable_endpoint(dev, i);
		usb_disable_endpoint(dev, i + USB_DIR_IN);
	}
	dev->toggle[0] = dev->toggle[1] = 0;

	/* getting rid of interfaces will disconnect
	 * any drivers bound to them (a key side effect)
	 */
	if (dev->actconfig) {
		for (i = 0; i < dev->actconfig->desc.bNumInterfaces; i++) {
			struct usb_interface	*interface;

			/* remove this interface if it has been registered */
			interface = dev->actconfig->interface[i];
			if (!device_is_registered(&interface->dev))
				continue;
			dev_dbg (&dev->dev, "unregistering interface %s\n",
				interface->dev.bus_id);
			usb_remove_sysfs_intf_files(interface);
			device_del (&interface->dev);
		}

		/* Now that the interfaces are unbound, nobody should
		 * try to access them.
		 */
		for (i = 0; i < dev->actconfig->desc.bNumInterfaces; i++) {
			put_device (&dev->actconfig->interface[i]->dev);
			dev->actconfig->interface[i] = NULL;
		}
		dev->actconfig = NULL;
		if (dev->state == USB_STATE_CONFIGURED)
			usb_set_device_state(dev, USB_STATE_ADDRESS);
	}
}


/*
 * usb_enable_endpoint - Enable an endpoint for USB communications
 * @dev: the device whose interface is being enabled
 * @ep: the endpoint
 *
 * Resets the endpoint toggle, and sets dev->ep_{in,out} pointers.
 * For control endpoints, both the input and output sides are handled.
 */
static void
usb_enable_endpoint(struct usb_device *dev, struct usb_host_endpoint *ep)
{
	unsigned int epaddr = ep->desc.bEndpointAddress;
	unsigned int epnum = epaddr & USB_ENDPOINT_NUMBER_MASK;
	int is_control;

	is_control = ((ep->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
			== USB_ENDPOINT_XFER_CONTROL);
	if (usb_endpoint_out(epaddr) || is_control) {
		usb_settoggle(dev, epnum, 1, 0);
		dev->ep_out[epnum] = ep;
	}
	if (!usb_endpoint_out(epaddr) || is_control) {
		usb_settoggle(dev, epnum, 0, 0);
		dev->ep_in[epnum] = ep;
	}
}

/*
 * usb_enable_interface - Enable all the endpoints for an interface
 * @dev: the device whose interface is being enabled
 * @intf: pointer to the interface descriptor
 *
 * Enables all the endpoints for the interface's current altsetting.
 */
static void usb_enable_interface(struct usb_device *dev,
				 struct usb_interface *intf)
{
	struct usb_host_interface *alt = intf->cur_altsetting;
	int i;

	for (i = 0; i < alt->desc.bNumEndpoints; ++i)
		usb_enable_endpoint(dev, &alt->endpoint[i]);
}

/**
 * usb_set_interface - Makes a particular alternate setting be current
 * @dev: the device whose interface is being updated
 * @interface: the interface being updated
 * @alternate: the setting being chosen.
 * Context: !in_interrupt ()
 *
 * This is used to enable data transfers on interfaces that may not
 * be enabled by default.  Not all devices support such configurability.
 * Only the driver bound to an interface may change its setting.
 *
 * Within any given configuration, each interface may have several
 * alternative settings.  These are often used to control levels of
 * bandwidth consumption.  For example, the default setting for a high
 * speed interrupt endpoint may not send more than 64 bytes per microframe,
 * while interrupt transfers of up to 3KBytes per microframe are legal.
 * Also, isochronous endpoints may never be part of an
 * interface's default setting.  To access such bandwidth, alternate
 * interface settings must be made current.
 *
 * Note that in the Linux USB subsystem, bandwidth associated with
 * an endpoint in a given alternate setting is not reserved until an URB
 * is submitted that needs that bandwidth.  Some other operating systems
 * allocate bandwidth early, when a configuration is chosen.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 * Also, drivers must not change altsettings while urbs are scheduled for
 * endpoints in that interface; all such urbs must first be completed
 * (perhaps forced by unlinking).
 *
 * Returns zero on success, or else the status code returned by the
 * underlying usb_control_msg() call.
 */
int usb_set_interface(struct usb_device *dev, int interface, int alternate)
{
	struct usb_interface *iface;
	struct usb_host_interface *alt;
	int ret;
	int manual = 0;

	if (dev->state == USB_STATE_SUSPENDED)
		return -EHOSTUNREACH;

	iface = usb_ifnum_to_if(dev, interface);
	if (!iface) {
		dev_dbg(&dev->dev, "selecting invalid interface %d\n",
			interface);
		return -EINVAL;
	}

	alt = usb_altnum_to_altsetting(iface, alternate);
	if (!alt) {
		warn("selecting invalid altsetting %d", alternate);
		return -EINVAL;
	}

	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				   USB_REQ_SET_INTERFACE, USB_RECIP_INTERFACE,
				   alternate, interface, NULL, 0, 5000);

	/* 9.4.10 says devices don't need this and are free to STALL the
	 * request if the interface only has one alternate setting.
	 */
	if (ret == -EPIPE && iface->num_altsetting == 1) {
		dev_dbg(&dev->dev,
			"manual set_interface for iface %d, alt %d\n",
			interface, alternate);
		manual = 1;
	} else if (ret < 0)
		return ret;

	/* FIXME drivers shouldn't need to replicate/bugfix the logic here
	 * when they implement async or easily-killable versions of this or
	 * other "should-be-internal" functions (like clear_halt).
	 * should hcd+usbcore postprocess control requests?
	 */

	/* prevent submissions using previous endpoint settings */
	if (device_is_registered(&iface->dev))
		usb_remove_sysfs_intf_files(iface);
	usb_disable_interface(dev, iface);

	iface->cur_altsetting = alt;

	/* If the interface only has one altsetting and the device didn't
	 * accept the request, we attempt to carry out the equivalent action
	 * by manually clearing the HALT feature for each endpoint in the
	 * new altsetting.
	 */
	if (manual) {
		int i;

		for (i = 0; i < alt->desc.bNumEndpoints; i++) {
			unsigned int epaddr =
				alt->endpoint[i].desc.bEndpointAddress;
			unsigned int pipe =
	__create_pipe(dev, USB_ENDPOINT_NUMBER_MASK & epaddr)
	| (usb_endpoint_out(epaddr) ? USB_DIR_OUT : USB_DIR_IN);

			usb_clear_halt(dev, pipe);
		}
	}

	/* 9.1.1.5: reset toggles for all endpoints in the new altsetting
	 *
	 * Note:
	 * Despite EP0 is always present in all interfaces/AS, the list of
	 * endpoints from the descriptor does not contain EP0. Due to its
	 * omnipresence one might expect EP0 being considered "affected" by
	 * any SetInterface request and hence assume toggles need to be reset.
	 * However, EP0 toggles are re-synced for every individual transfer
	 * during the SETUP stage - hence EP0 toggles are "don't care" here.
	 * (Likewise, EP0 never "halts" on well designed devices.)
	 */
	usb_enable_interface(dev, iface);
	if (device_is_registered(&iface->dev))
		usb_create_sysfs_intf_files(iface);

	return 0;
}

/**
 * usb_reset_configuration - lightweight device reset
 * @dev: the device whose configuration is being reset
 *
 * This issues a standard SET_CONFIGURATION request to the device using
 * the current configuration.  The effect is to reset most USB-related
 * state in the device, including interface altsettings (reset to zero),
 * endpoint halts (cleared), and data toggle (only for bulk and interrupt
 * endpoints).  Other usbcore state is unchanged, including bindings of
 * usb device drivers to interfaces.
 *
 * Because this affects multiple interfaces, avoid using this with composite
 * (multi-interface) devices.  Instead, the driver for each interface may
 * use usb_set_interface() on the interfaces it claims.  Be careful though;
 * some devices don't support the SET_INTERFACE request, and others won't
 * reset all the interface state (notably data toggles).  Resetting the whole
 * configuration would affect other drivers' interfaces.
 *
 * The caller must own the device lock.
 *
 * Returns zero on success, else a negative error code.
 */
int usb_reset_configuration(struct usb_device *dev)
{
	int			i, retval;
	struct usb_host_config	*config;

	if (dev->state == USB_STATE_SUSPENDED)
		return -EHOSTUNREACH;

	/* caller must have locked the device and must own
	 * the usb bus readlock (so driver bindings are stable);
	 * calls during probe() are fine
	 */

	for (i = 1; i < 16; ++i) {
		usb_disable_endpoint(dev, i);
		usb_disable_endpoint(dev, i + USB_DIR_IN);
	}

	config = dev->actconfig;
	retval = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			USB_REQ_SET_CONFIGURATION, 0,
			config->desc.bConfigurationValue, 0,
			NULL, 0, USB_CTRL_SET_TIMEOUT);
	if (retval < 0)
		return retval;

	dev->toggle[0] = dev->toggle[1] = 0;

	/* re-init hc/hcd interface/endpoint state */
	for (i = 0; i < config->desc.bNumInterfaces; i++) {
		struct usb_interface *intf = config->interface[i];
		struct usb_host_interface *alt;

		if (device_is_registered(&intf->dev))
			usb_remove_sysfs_intf_files(intf);
		alt = usb_altnum_to_altsetting(intf, 0);

		/* No altsetting 0?  We'll assume the first altsetting.
		 * We could use a GetInterface call, but if a device is
		 * so non-compliant that it doesn't have altsetting 0
		 * then I wouldn't trust its reply anyway.
		 */
		if (!alt)
			alt = &intf->altsetting[0];

		intf->cur_altsetting = alt;
		usb_enable_interface(dev, intf);
		if (device_is_registered(&intf->dev))
			usb_create_sysfs_intf_files(intf);
	}
	return 0;
}

static void release_interface(struct device *dev)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_interface_cache *intfc =
			altsetting_to_usb_interface_cache(intf->altsetting);

	kref_put(&intfc->ref, usb_release_interface_cache);
	kfree(intf);
}

/*
 * usb_set_configuration - Makes a particular device setting be current
 * @dev: the device whose configuration is being updated
 * @configuration: the configuration being chosen.
 * Context: !in_interrupt(), caller owns the device lock
 *
 * This is used to enable non-default device modes.  Not all devices
 * use this kind of configurability; many devices only have one
 * configuration.
 *
 * USB device configurations may affect Linux interoperability,
 * power consumption and the functionality available.  For example,
 * the default configuration is limited to using 100mA of bus power,
 * so that when certain device functionality requires more power,
 * and the device is bus powered, that functionality should be in some
 * non-default device configuration.  Other device modes may also be
 * reflected as configuration options, such as whether two ISDN
 * channels are available independently; and choosing between open
 * standard device protocols (like CDC) or proprietary ones.
 *
 * Note that USB has an additional level of device configurability,
 * associated with interfaces.  That configurability is accessed using
 * usb_set_interface().
 *
 * This call is synchronous. The calling context must be able to sleep,
 * must own the device lock, and must not hold the driver model's USB
 * bus rwsem; usb device driver probe() methods cannot use this routine.
 *
 * Returns zero on success, or else the status code returned by the
 * underlying call that failed.  On successful completion, each interface
 * in the original device configuration has been destroyed, and each one
 * in the new configuration has been probed by all relevant usb device
 * drivers currently known to the kernel.
 */
int usb_set_configuration(struct usb_device *dev, int configuration)
{
	int i, ret;
	struct usb_host_config *cp = NULL;
	struct usb_interface **new_interfaces = NULL;
	int n, nintf;

	for (i = 0; i < dev->descriptor.bNumConfigurations; i++) {
		if (dev->config[i].desc.bConfigurationValue == configuration) {
			cp = &dev->config[i];
			break;
		}
	}
	if ((!cp && configuration != 0))
		return -EINVAL;

	/* The USB spec says configuration 0 means unconfigured.
	 * But if a device includes a configuration numbered 0,
	 * we will accept it as a correctly configured state.
	 */
	if (cp && configuration == 0)
		dev_warn(&dev->dev, "config 0 descriptor??\n");

	if (dev->state == USB_STATE_SUSPENDED)
		return -EHOSTUNREACH;

	/* Allocate memory for new interfaces before doing anything else,
	 * so that if we run out then nothing will have changed. */
	n = nintf = 0;
	if (cp) {
		nintf = cp->desc.bNumInterfaces;
		new_interfaces = kmalloc(nintf * sizeof(*new_interfaces),
				GFP_KERNEL);
		if (!new_interfaces) {
			dev_err(&dev->dev, "Out of memory");
			return -ENOMEM;
		}

		for (; n < nintf; ++n) {
			new_interfaces[n] = kzalloc(
					sizeof(struct usb_interface),
					GFP_KERNEL);
			if (!new_interfaces[n]) {
				dev_err(&dev->dev, "Out of memory");
				ret = -ENOMEM;
free_interfaces:
				while (--n >= 0)
					kfree(new_interfaces[n]);
				kfree(new_interfaces);
				return ret;
			}
		}
	}

	/* if it's already configured, clear out old state first.
	 * getting rid of old interfaces means unbinding their drivers.
	 */
	if (dev->state != USB_STATE_ADDRESS)
		usb_disable_device (dev, 1);	// Skip ep0

	i = dev->bus_mA - cp->desc.bMaxPower * 2;
	if (i < 0)
		dev_warn(&dev->dev, "new config #%d exceeds power "
				"limit by %dmA\n",
				configuration, -i);

	if ((ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			USB_REQ_SET_CONFIGURATION, 0, configuration, 0,
			NULL, 0, USB_CTRL_SET_TIMEOUT)) < 0)
		goto free_interfaces;

	dev->actconfig = cp;
	if (!cp)
		usb_set_device_state(dev, USB_STATE_ADDRESS);
	else {
		usb_set_device_state(dev, USB_STATE_CONFIGURED);

		/* Initialize the new interface structures and the
		 * hc/hcd/usbcore interface/endpoint state.
		 */
		for (i = 0; i < nintf; ++i) {
			struct usb_interface_cache *intfc;
			struct usb_interface *intf;
			struct usb_host_interface *alt;

			cp->interface[i] = intf = new_interfaces[i];
			intfc = cp->intf_cache[i];
			intf->altsetting = intfc->altsetting;
			intf->num_altsetting = intfc->num_altsetting;
			kref_get(&intfc->ref);

			alt = usb_altnum_to_altsetting(intf, 0);

			/* No altsetting 0?  We'll assume the first altsetting.
			 * We could use a GetInterface call, but if a device is
			 * so non-compliant that it doesn't have altsetting 0
			 * then I wouldn't trust its reply anyway.
			 */
			if (!alt)
				alt = &intf->altsetting[0];

			intf->cur_altsetting = alt;
			usb_enable_interface(dev, intf);
			intf->dev.parent = &dev->dev;
			intf->dev.driver = NULL;
			intf->dev.bus = &usb_bus_type;
			intf->dev.dma_mask = dev->dev.dma_mask;
			intf->dev.release = release_interface;
			device_initialize (&intf->dev);
			mark_quiesced(intf);
			sprintf (&intf->dev.bus_id[0], "%d-%s:%d.%d",
				 dev->bus->busnum, dev->devpath,
				 configuration,
				 alt->desc.bInterfaceNumber);
		}
		kfree(new_interfaces);

		if (cp->string == NULL)
			cp->string = usb_cache_string(dev,
					cp->desc.iConfiguration);

		/* Now that all the interfaces are set up, register them
		 * to trigger binding of drivers to interfaces.  probe()
		 * routines may install different altsettings and may
		 * claim() any interfaces not yet bound.  Many class drivers
		 * need that: CDC, audio, video, etc.
		 */
		for (i = 0; i < nintf; ++i) {
			struct usb_interface *intf = cp->interface[i];

			dev_dbg (&dev->dev,
				"adding %s (config #%d, interface %d)\n",
				intf->dev.bus_id, configuration,
				intf->cur_altsetting->desc.bInterfaceNumber);
			ret = device_add (&intf->dev);
			if (ret != 0) {
				dev_err(&dev->dev,
					"device_add(%s) --> %d\n",
					intf->dev.bus_id,
					ret);
				continue;
			}
			usb_create_sysfs_intf_files (intf);
		}
	}

	return 0;
}

// synchronous request completion model
EXPORT_SYMBOL(usb_control_msg);
EXPORT_SYMBOL(usb_bulk_msg);

EXPORT_SYMBOL(usb_sg_init);
EXPORT_SYMBOL(usb_sg_cancel);
EXPORT_SYMBOL(usb_sg_wait);

// synchronous control message convenience routines
EXPORT_SYMBOL(usb_get_descriptor);
EXPORT_SYMBOL(usb_get_status);
EXPORT_SYMBOL(usb_get_string);
EXPORT_SYMBOL(usb_string);

// synchronous calls that also maintain usbcore state
EXPORT_SYMBOL(usb_clear_halt);
EXPORT_SYMBOL(usb_reset_configuration);
EXPORT_SYMBOL(usb_set_interface);

