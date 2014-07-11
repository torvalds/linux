/***********************************************************************************
 CED1401 usb driver. This basic loading is based on the usb-skeleton.c code that is:
 Copyright (C) 2001-2004 Greg Kroah-Hartman (greg@kroah.com)
 Copyright (C) 2012 Alois Schloegl <alois.schloegl@ist.ac.at>
 There is not a great deal of the skeleton left.

 All the remainder dealing specifically with the CED1401 is based on drivers written
 by CED for other systems (mainly Windows) and is:
 Copyright (C) 2010 Cambridge Electronic Design Ltd
 Author Greg P Smith (greg@ced.co.uk)

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

Endpoints
*********
There are 4 endpoints plus the control endpoint in the standard interface
provided by most 1401s. The control endpoint is used for standard USB requests,
plus various CED-specific transactions such as start self test, debug and get
the 1401 status. The other endpoints are:

 1 Characters to the 1401
 2 Characters from the 1401
 3 Block data to the 1401
 4 Block data to the host.

inside the driver these are indexed as an array from 0 to 3, transactions
over the control endpoint are carried out using a separate mechanism. The
use of the endpoints is mostly straightforward, with the driver issuing
IO request packets (IRPs) as required to transfer data to and from the 1401.
The handling of endpoint 2 is different because it is used for characters
from the 1401, which can appear spontaneously and without any other driver
activity - for example to repeatedly request DMA transfers in Spike2. The
desired effect is achieved by using an interrupt endpoint which can be
polled to see if it has data available, and writing the driver so that it
always maintains a pending read IRP from that endpoint which will read the
character data and terminate as soon as the 1401 makes data available. This
works very well, some care is taken with when you kick off this character
read IRP to avoid it being active when it is not wanted but generally it
is running all the time.

In the 2270, there are only three endpoints plus the control endpoint. In
addition to the transactions mentioned above, the control endpoint is used
to transfer character data to the 1401. The other endpoints are used as:

 1 Characters from the 1401
 2 Block data to the 1401
 3 Block data to the host.

The type of interface available is specified by the interface subclass field
in the interface descriptor provided by the 1401. See the USB_INT_ constants
for the values that this field can hold.

****************************************************************************
Linux implementation

Although Linux Device Drivers (3rd Edition) was a major source of information,
it is very out of date. A lot of information was gleaned from the latest
usb_skeleton.c code (you need to download the kernel sources to get this).

To match the Windows version, everything is done using ioctl calls. All the
device state is held in the struct ced_data.
Block transfers are done by using get_user_pages() to pin down a list of
pages that we hold a pointer to in the device driver. We also allocate a
coherent transfer buffer of size STAGED_SZ (this must be a multiple of the
bulk endpoint size so that the 1401 does not realise that we break large
transfers down into smaller pieces). We use kmap_atomic() to get a kernel
va for each page, as it is required, for copying; see ced_copy_user_space().

All character and data transfers are done using asynchronous IO. All Urbs are
tracked by anchoring them. Status and debug ioctls are implemented with the
synchronous non-Urb based transfers.
*/

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>

#include "usb1401.h"

/* Define these values to match your devices */
#define USB_CED_VENDOR_ID	0x0525
#define USB_CED_PRODUCT_ID	0xa0f0

/* table of devices that work with this driver */
static const struct usb_device_id ced_table[] = {
	{USB_DEVICE(USB_CED_VENDOR_ID, USB_CED_PRODUCT_ID)},
	{}			/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, ced_table);

/* Get a minor range for your devices from the usb maintainer */
#define USB_CED_MINOR_BASE	192

/* our private defines. if this grows any larger, use your own .h file */
#define MAX_TRANSFER		(PAGE_SIZE - 512)
/* MAX_TRANSFER is chosen so that the VM is not stressed by
   allocations > PAGE_SIZE and the number of packets in a page
   is an integer 512 is the largest possible packet on EHCI */
#define WRITES_IN_FLIGHT	8
/* arbitrarily chosen */

static struct usb_driver ced_driver;

static void ced_delete(struct kref *kref)
{
	struct ced_data *ced = to_ced_data(kref);

	/*  Free up the output buffer, then free the output urb. Note that the interface member */
	/*  of ced will probably be NULL, so cannot be used to get to dev. */
	usb_free_coherent(ced->udev, OUTBUF_SZ, ced->coher_char_out,
			  ced->urb_char_out->transfer_dma);
	usb_free_urb(ced->urb_char_out);

	/*  Do the same for chan input */
	usb_free_coherent(ced->udev, INBUF_SZ, ced->coher_char_in,
			  ced->urb_char_in->transfer_dma);
	usb_free_urb(ced->urb_char_in);

	/*  Do the same for the block transfers */
	usb_free_coherent(ced->udev, STAGED_SZ, ced->coher_staged_io,
			  ced->staged_urb->transfer_dma);
	usb_free_urb(ced->staged_urb);

	usb_put_dev(ced->udev);
	kfree(ced);
}

/*  This is the driver end of the open() call from user space. */
static int ced_open(struct inode *inode, struct file *file)
{
	struct ced_data *ced;
	int retval = 0;
	int subminor = iminor(inode);
	struct usb_interface *interface =
	    usb_find_interface(&ced_driver, subminor);
	if (!interface) {
		pr_err("%s - error, can't find device for minor %d", __func__,
		       subminor);
		retval = -ENODEV;
		goto exit;
	}

	ced = usb_get_intfdata(interface);
	if (!ced) {
		retval = -ENODEV;
		goto exit;
	}

	dev_dbg(&interface->dev, "%s: got ced\n", __func__);

	/* increment our usage count for the device */
	kref_get(&ced->kref);

	/* lock the device to allow correctly handling errors
	 * in resumption */
	mutex_lock(&ced->io_mutex);

	if (!ced->open_count++) {
		retval = usb_autopm_get_interface(interface);
		if (retval) {
			ced->open_count--;
			mutex_unlock(&ced->io_mutex);
			kref_put(&ced->kref, ced_delete);
			goto exit;
		}
	} else {		/* uncomment this block if you want exclusive open */
		dev_err(&interface->dev, "%s: fail: already open\n", __func__);
		retval = -EBUSY;
		ced->open_count--;
		mutex_unlock(&ced->io_mutex);
		kref_put(&ced->kref, ced_delete);
		goto exit;
	}
	/* prevent the device from being autosuspended */

	/* save our object in the file's private structure */
	file->private_data = ced;
	mutex_unlock(&ced->io_mutex);

exit:
	return retval;
}

static int ced_release(struct inode *inode, struct file *file)
{
	struct ced_data *ced = file->private_data;
	if (ced == NULL)
		return -ENODEV;

	dev_dbg(&ced->interface->dev, "%s: called\n", __func__);
	mutex_lock(&ced->io_mutex);
	if (!--ced->open_count && ced->interface)	/*  Allow autosuspend */
		usb_autopm_put_interface(ced->interface);
	mutex_unlock(&ced->io_mutex);

	kref_put(&ced->kref, ced_delete);	/*  decrement the count on our device */
	return 0;
}

static int ced_flush(struct file *file, fl_owner_t id)
{
	int res;
	struct ced_data *ced = file->private_data;
	if (ced == NULL)
		return -ENODEV;

	dev_dbg(&ced->interface->dev, "%s: char in pend=%d\n",
		__func__, ced->read_chars_pending);

	/* wait for io to stop */
	mutex_lock(&ced->io_mutex);
	dev_dbg(&ced->interface->dev, "%s: got io_mutex\n", __func__);
	ced_draw_down(ced);

	/* read out errors, leave subsequent opens a clean slate */
	spin_lock_irq(&ced->err_lock);
	res = ced->errors ? (ced->errors == -EPIPE ? -EPIPE : -EIO) : 0;
	ced->errors = 0;
	spin_unlock_irq(&ced->err_lock);

	mutex_unlock(&ced->io_mutex);
	dev_dbg(&ced->interface->dev, "%s: exit reached\n", __func__);

	return res;
}

/***************************************************************************
** can_accept_io_requests
** If the device is removed, interface is set NULL. We also clear our pointer
** from the interface, so we should make sure that ced is not NULL. This will
** not help with a device extension held by a file.
** return true if can accept new io requests, else false
*/
static bool can_accept_io_requests(struct ced_data *ced)
{
	return ced && ced->interface;	/*  Can we accept IO requests */
}

/****************************************************************************
** Callback routine to complete writes. This may need to fire off another
** urb to complete the transfer.
****************************************************************************/
static void ced_writechar_callback(struct urb *urb)
{
	struct ced_data *ced = urb->context;
	int got = urb->actual_length;	/*  what we transferred */

	if (urb->status) {	/*  sync/async unlink faults aren't errors */
		if (!
		    (urb->status == -ENOENT || urb->status == -ECONNRESET
		     || urb->status == -ESHUTDOWN)) {
			dev_err(&ced->interface->dev,
				"%s: nonzero write bulk status received: %d\n",
				__func__, urb->status);
		}

		spin_lock(&ced->err_lock);
		ced->errors = urb->status;
		spin_unlock(&ced->err_lock);
		got = 0;	/*   and tidy up again if so */

		spin_lock(&ced->char_out_lock);	/* already at irq level */
		ced->out_buff_get = 0;	/*  Reset the output buffer */
		ced->out_buff_put = 0;
		ced->num_output = 0;	/*  Clear the char count */
		ced->pipe_error[0] = 1;	/*  Flag an error for later */
		ced->send_chars_pending = false; /* Allow other threads again */
		spin_unlock(&ced->char_out_lock); /* already at irq level */
		dev_dbg(&ced->interface->dev,
			"%s: char out done, 0 chars sent\n", __func__);
	} else {
		dev_dbg(&ced->interface->dev,
			"%s: char out done, %d chars sent\n", __func__, got);
		spin_lock(&ced->char_out_lock);	/*  already at irq level */
		ced->num_output -= got;	/*  Now adjust the char send buffer */
		ced->out_buff_get += got;	/*  to match what we did */
		if (ced->out_buff_get >= OUTBUF_SZ)	/*  Can't do this any earlier as data could be overwritten */
			ced->out_buff_get = 0;

		if (ced->num_output > 0) {	/*  if more to be done... */
			int pipe = 0;	/*  The pipe number to use */
			int ret;
			char *pDat = &ced->output_buffer[ced->out_buff_get];
			unsigned int dwCount = ced->num_output;	/*  maximum to send */
			if ((ced->out_buff_get + dwCount) > OUTBUF_SZ)	/*  does it cross buffer end? */
				dwCount = OUTBUF_SZ - ced->out_buff_get;

			/* we are done with stuff that changes */
			spin_unlock(&ced->char_out_lock);

			memcpy(ced->coher_char_out, pDat, dwCount);	/*  copy output data to the buffer */
			usb_fill_bulk_urb(ced->urb_char_out, ced->udev,
					  usb_sndbulkpipe(ced->udev,
							  ced->ep_addr[0]),
					  ced->coher_char_out, dwCount,
					  ced_writechar_callback, ced);
			ced->urb_char_out->transfer_flags |=
			    URB_NO_TRANSFER_DMA_MAP;
			usb_anchor_urb(ced->urb_char_out, &ced->submitted);	/*  in case we need to kill it */
			ret = usb_submit_urb(ced->urb_char_out, GFP_ATOMIC);
			dev_dbg(&ced->interface->dev, "%s: n=%d>%s<\n",
				__func__, dwCount, pDat);
			spin_lock(&ced->char_out_lock);	/*  grab lock for errors */
			if (ret) {
				ced->pipe_error[pipe] = 1;	/*  Flag an error to be handled later */
				ced->send_chars_pending = false;	/*  Allow other threads again */
				usb_unanchor_urb(ced->urb_char_out);
				dev_err(&ced->interface->dev,
					"%s: usb_submit_urb() returned %d\n",
					__func__, ret);
			}
		} else
			/* Allow other threads again */
			ced->send_chars_pending = false;

		spin_unlock(&ced->char_out_lock); /* already at irq level */
	}
}

/****************************************************************************
** ced_send_chars
** Transmit the characters in the output buffer to the 1401. This may need
** breaking down into multiple transfers.
****************************************************************************/
int ced_send_chars(struct ced_data *ced)
{
	int retval = U14ERR_NOERROR;

	spin_lock_irq(&ced->char_out_lock);	/*  Protect ourselves */

	if ((!ced->send_chars_pending) &&	/*  Not currently sending */
	    (ced->num_output > 0) &&	/*   has characters to output */
	    (can_accept_io_requests(ced)))	{ /*   and current activity is OK */
		unsigned int count = ced->num_output;	/* Get a copy of the */
							/* character count   */
		ced->send_chars_pending = true;	/*  Set flag to lock out other threads */

		dev_dbg(&ced->interface->dev,
			"Send %d chars to 1401, EP0 flag %d\n",
			count, ced->n_pipes == 3);
		/*  If we have only 3 end points we must send the characters to the 1401 using EP0. */
		if (ced->n_pipes == 3) {
			/*  For EP0 character transmissions to the 1401, we have to hang about until they */
			/*  are gone, as otherwise without more character IO activity they will never go. */
			unsigned int i = count;	/*  Local char counter */
			unsigned int index = 0;	/*  The index into the char buffer */

			spin_unlock_irq(&ced->char_out_lock);	/*  Free spinlock as we call USBD */

			while ((i > 0) && (retval == U14ERR_NOERROR)) {
				/*  We have to break the transfer up into 64-byte chunks because of a 2270 problem */
				int n = i > 64 ? 64 : i;	/*  Chars for this xfer, max of 64 */
				int sent = usb_control_msg(ced->udev,
							    usb_sndctrlpipe(ced->udev, 0),	/*  use end point 0 */
							    DB_CHARS,	/*  bRequest */
							    (H_TO_D | VENDOR | DEVREQ),	/*  to the device, vendor request to the device */
							    0, 0,	/*  value and index are both 0 */
							    &ced->output_buffer[index],	/*  where to send from */
							    n,	/*  how much to send */
							    1000);	/*  timeout in jiffies */
				if (sent <= 0) {
					retval = sent ? sent : -ETIMEDOUT;	/*  if 0 chars says we timed out */
					dev_err(&ced->interface->dev,
						"Send %d chars by EP0 failed: %d\n",
						n, retval);
				} else {
					dev_dbg(&ced->interface->dev,
						"Sent %d chars by EP0\n", n);
					i -= sent;
					index += sent;
				}
			}

			spin_lock_irq(&ced->char_out_lock);	/*  Protect ced changes, released by general code */
			ced->out_buff_get = 0;	/*  so reset the output buffer */
			ced->out_buff_put = 0;
			ced->num_output = 0;	/*  and clear the buffer count */
			ced->send_chars_pending = false;	/*  Allow other threads again */
		} else {	/*  Here for sending chars normally - we hold the spin lock */
			int pipe = 0;	/*  The pipe number to use */
			char *pDat = &ced->output_buffer[ced->out_buff_get];

			if ((ced->out_buff_get + count) > OUTBUF_SZ)	/*  does it cross buffer end? */
				count = OUTBUF_SZ - ced->out_buff_get;
			spin_unlock_irq(&ced->char_out_lock);	/*  we are done with stuff that changes */
			memcpy(ced->coher_char_out, pDat, count);	/*  copy output data to the buffer */
			usb_fill_bulk_urb(ced->urb_char_out, ced->udev,
					  usb_sndbulkpipe(ced->udev,
							  ced->ep_addr[0]),
					  ced->coher_char_out, count,
					  ced_writechar_callback, ced);
			ced->urb_char_out->transfer_flags |=
			    URB_NO_TRANSFER_DMA_MAP;
			usb_anchor_urb(ced->urb_char_out, &ced->submitted);
			retval = usb_submit_urb(ced->urb_char_out, GFP_KERNEL);

			 /* grab lock for errors */
			spin_lock_irq(&ced->char_out_lock);

			if (retval) {
				ced->pipe_error[pipe] = 1;	/*  Flag an error to be handled later */
				ced->send_chars_pending = false;	/*  Allow other threads again */
				usb_unanchor_urb(ced->urb_char_out);	/*  remove from list of active urbs */
			}
		}
	} else if (ced->send_chars_pending && (ced->num_output > 0))
		dev_dbg(&ced->interface->dev,
			"%s: send_chars_pending:true\n", __func__);

	dev_dbg(&ced->interface->dev, "%s: exit code: %d\n", __func__, retval);
	spin_unlock_irq(&ced->char_out_lock); /* Now let go of the spinlock */
	return retval;
}

/***************************************************************************
** ced_copy_user_space
** This moves memory between pinned down user space and the coher_staged_io
** memory buffer we use for transfers. Copy n bytes in the directions that
** is defined by ced->staged_read. The user space is determined by the area
** in ced->staged_id and the offset in ced->staged_done. The user
** area may well not start on a page boundary, so allow for that.
**
** We have a table of physical pages that describe the area, so we can use
** this to get a virtual address that the kernel can use.
**
** ced  Is our device extension which holds all we know about the transfer.
** n    The number of bytes to move one way or the other.
***************************************************************************/
static void ced_copy_user_space(struct ced_data *ced, int n)
{
	unsigned int area = ced->staged_id;
	if (area < MAX_TRANSAREAS) {
		/*  area to be used */
		struct transarea *ta = &ced->trans_def[area];
		unsigned int offset =
		    ced->staged_done + ced->staged_offset + ta->base_offset;
		char *coher_buf = ced->coher_staged_io;	/*  coherent buffer */
		if (!ta->used) {
			dev_err(&ced->interface->dev, "%s: area %d unused\n",
				__func__, area);
			return;
		}

		while (n) {
			/*  page number in table */
			int page = offset >> PAGE_SHIFT;

			if (page < ta->n_pages) {
				char *address =
				    (char *)kmap_atomic(ta->pages[page]);
				if (address) {
					/* offset into the page */
					unsigned int page_off =
						offset & (PAGE_SIZE - 1);
					/* max to transfer on this page */
					size_t xfer = PAGE_SIZE - page_off;

					/* limit byte count if too much */
					/* for the page                 */
					if (xfer > n)
						xfer = n;
					if (ced->staged_read)
						memcpy(address + page_off,
						       coher_buf, xfer);
					else
						memcpy(coher_buf,
						       address + page_off,
						       xfer);
					kunmap_atomic(address);
					offset += xfer;
					coher_buf += xfer;
					n -= xfer;
				} else {
					dev_err(&ced->interface->dev,
						"%s: did not map page %d\n",
						__func__, page);
					return;
				}

			} else {
				dev_err(&ced->interface->dev,
					"%s: exceeded pages %d\n",
					__func__, page);
				return;
			}
		}
	} else
		dev_err(&ced->interface->dev, "%s: bad area %d\n",
			__func__, area);
}

/*  Forward declarations for stuff used circularly */
static int ced_stage_chunk(struct ced_data *ced);

/***************************************************************************
** ReadWrite_Complete
**
**  Completion routine for our staged read/write Irps
*/
static void staged_callback(struct urb *urb)
{
	struct ced_data *ced = urb->context;
	unsigned int got = urb->actual_length;	/*  what we transferred */
	bool cancel = false;
	bool restart_char_input;	/*  used at the end */

	spin_lock(&ced->staged_lock); /* stop ced_read_write_mem() action */
				      /* while this routine is running    */

	 /* clear the flag for staged IRP pending */
	ced->staged_urb_pending = false;

	if (urb->status) {	/*  sync/async unlink faults aren't errors */
		if (!
		    (urb->status == -ENOENT || urb->status == -ECONNRESET
		     || urb->status == -ESHUTDOWN)) {
			dev_err(&ced->interface->dev,
				"%s: nonzero write bulk status received: %d\n",
				__func__, urb->status);
		} else
			dev_info(&ced->interface->dev,
				 "%s: staged xfer cancelled\n", __func__);

		spin_lock(&ced->err_lock);
		ced->errors = urb->status;
		spin_unlock(&ced->err_lock);
		got = 0;	/*   and tidy up again if so */
		cancel = true;
	} else {
		dev_dbg(&ced->interface->dev, "%s: %d chars xferred\n",
			__func__, got);
		if (ced->staged_read)	/* if reading, save to user space */
			/* copy from buffer to user */
			ced_copy_user_space(ced, got);
		if (got == 0)
			dev_dbg(&ced->interface->dev, "%s: ZLP\n", __func__);
	}

	/* Update the transfer length based on the TransferBufferLength value */
	/* in the URB                                                         */
	ced->staged_done += got;

	dev_dbg(&ced->interface->dev, "%s: done %d bytes of %d\n",
		__func__, ced->staged_done, ced->staged_length);

	if ((ced->staged_done == ced->staged_length) ||	/* If no more to do */
	    (cancel)) {		/*  or this IRP was cancelled */
		/*  Transfer area info */
		struct transarea *ta = &ced->trans_def[ced->staged_id];

		dev_dbg(&ced->interface->dev,
			"%s: transfer done, bytes %d, cancel %d\n",
			__func__, ced->staged_done, cancel);

		/* Here is where we sort out what to do with this transfer if */
		/* using a circular buffer. We have a completed transfer that */
		/* can be assumed to fit into the transfer area. We should be */
		/* able to add this to the end of a growing block or to use   */
		/* it to start a new block unless the code that calculates    */
		/* the offset to use (in ced_read_write_mem) is totally duff. */
		if ((ta->circular) &&
		    (ta->circ_to_host) &&
		    (!cancel) && /* Time to sort out circular buffer info? */
		    (ced->staged_read)) {/* Only for tohost transfers for now */
			/* If block 1 is in use we must append to it */
			if (ta->blocks[1].size > 0) {
				if (ced->staged_offset ==
				    (ta->blocks[1].offset +
				     ta->blocks[1].size)) {
					ta->blocks[1].size +=
					    ced->staged_length;
					dev_dbg(&ced->interface->dev,
						"RWM_Complete, circ block 1 "
						"now %d bytes at %d\n",
						ta->blocks[1].size,
						ta->blocks[1].offset);
				} else {
					/* Here things have gone very, very */
					/* wrong, but I cannot see how this */
					/* can actually be achieved         */
					ta->blocks[1].offset =
					    ced->staged_offset;
					ta->blocks[1].size =
					    ced->staged_length;
					dev_err(&ced->interface->dev,
						"%s: ERROR, circ block 1 "
						"re-started %d bytes at %d\n",
						__func__,
						ta->blocks[1].size,
						ta->blocks[1].offset);
				}
			} else { /* If block 1 is not used, we try to add */
				 /*to block 0                             */

				/* Got stored block 0 information? */
				if (ta->blocks[0].size > 0) {
					/*  Must append onto the */
					/*existing block 0       */
					if (ced->staged_offset ==
					    (ta->blocks[0].offset +
					     ta->blocks[0].size)) {
						/* Just add this transfer in */
						ta->blocks[0].size +=
							ced->staged_length;
						dev_dbg(&ced->interface->dev,
							"RWM_Complete, circ "
							"block 0 now %d bytes "
							"at %d\n",
							ta->blocks[0].size,
							ta->blocks[0].offset);

					} else { /* If it doesn't append, put */
						 /* into new block 1          */
						ta->blocks[1].offset =
						    ced->staged_offset;
						ta->blocks[1].size =
						    ced->staged_length;
						dev_dbg(&ced->interface->dev,
							"RWM_Complete, circ "
							"block 1 started %d "
							"bytes at %d\n",
							ta->blocks[1].size,
							ta->blocks[1].offset);
					}
				} else	{ /* No info stored yet, just save */
					  /* in block 0                    */
					ta->blocks[0].offset =
					    ced->staged_offset;
					ta->blocks[0].size =
					    ced->staged_length;
					dev_dbg(&ced->interface->dev,
						"RWM_Complete, circ block 0 "
						"started %d bytes at %d\n",
						ta->blocks[0].size,
						ta->blocks[0].offset);
				}
			}
		}

		if (!cancel) { /*  Don't generate an event if cancelled */
			dev_dbg(&ced->interface->dev,
				"RWM_Complete,  bCircular %d, bToHost %d, "
				"eStart %d, eSize %d\n",
				ta->circular, ta->event_to_host,
				ta->event_st, ta->event_sz);
			/* Set a user-mode event...           */
			/* ...on transfers in this direction? */
			if ((ta->event_sz) &&
			    (ced->staged_read == ta->event_to_host)) {
				int wakeup = 0; /* assume */

				/* If we have completed the right sort of DMA */
				/* transfer then set the event to notify the  */
				/* user code to wake up anyone that is        */
				/* waiting. */
				if ((ta->circular) && /* Circular areas use a simpler test */
				    (ta->circ_to_host)) {	/*  only in supported direction */
					/* Is total data waiting up */
					/* to size limit? */
					unsigned int dwTotal =
					    ta->blocks[0].size +
					    ta->blocks[1].size;
					wakeup = (dwTotal >= ta->event_sz);
				} else {
					unsigned int transEnd =
					    ced->staged_offset +
					    ced->staged_length;
					unsigned int eventEnd =
					    ta->event_st + ta->event_sz;
					wakeup = (ced->staged_offset < eventEnd)
					    && (transEnd > ta->event_st);
				}

				if (wakeup) {
					dev_dbg(&ced->interface->dev,
					  "About to set event to notify app\n");

					/*  wake up waiting processes */
					wake_up_interruptible(&ta->event);
					/* increment wakeup count */
					++ta->wake_up;
				}
			}
		}

		/* Switch back to char mode before ced_read_write_mem call */
		ced->dma_flag = MODE_CHAR;

		 /* Don't look for waiting transfer if cancelled */
		if (!cancel) {
			/*  If we have a transfer waiting, kick it off */
			if (ced->xfer_waiting) {/*  Got a block xfer waiting? */
				int retval;
				dev_info(&ced->interface->dev,
					 "*** RWM_Complete *** pending transfer"
					 " will now be set up!!!\n");
				retval =
				    ced_read_write_mem(ced,
						       !ced->dma_info.outward,
						       ced->dma_info.ident,
						       ced->dma_info.offset,
						       ced->dma_info.size);

				if (retval)
					dev_err(&ced->interface->dev,
						"RWM_Complete rw setup failed %d\n",
						retval);
			}
		}

	} else			/*  Here for more to do */
		ced_stage_chunk(ced);	/*  fire off the next bit */

	/* While we hold the staged_lock, see if we should reallow character */
	/* input ints                                                        */
	/* Don't allow if cancelled, or if a new block has started or if     */
	/* there is a waiting block.                                         */
	/* This feels wrong as we should ask which spin lock protects        */
	/* dma_flag. */
	restart_char_input = !cancel && (ced->dma_flag == MODE_CHAR) &&
			     !ced->xfer_waiting;

	spin_unlock(&ced->staged_lock);	/*  Finally release the lock again */

	/* This is not correct as dma_flag is protected by the staged lock, */
	/* but it is treated in ced_allowi as if it were protected by the   */
	/* char lock. In any case, most systems will not be upset by char   */
	/* input during DMA... sigh. Needs sorting out.                     */
	if (restart_char_input)	/*  may be out of date, but... */
		ced_allowi(ced);	/*  ...ced_allowi tests a lock too. */
	dev_dbg(&ced->interface->dev, "%s: done\n", __func__);
}

/****************************************************************************
** ced_stage_chunk
**
** Generates the next chunk of data making up a staged transfer.
**
** The calling code must have acquired the staging spinlock before calling
** this function, and is responsible for releasing it. We are at callback level.
****************************************************************************/
static int ced_stage_chunk(struct ced_data *ced)
{
	int retval = U14ERR_NOERROR;
	unsigned int chunk_size;
	int pipe = ced->staged_read ? 3 : 2; /* The pipe number to use for */
					     /* reads or writes            */

	if (ced->n_pipes == 3)
		pipe--;	/* Adjust for the 3-pipe case */

	if (pipe < 0)   /* and trap case that should never happen */
		return U14ERR_FAIL;

	if (!can_accept_io_requests(ced)) {	/*  got sudden remove? */
		dev_info(&ced->interface->dev, "%s: sudden remove, giving up\n",
			 __func__);
		return U14ERR_FAIL;	/*  could do with a better error */
	}

	/* transfer length remaining */
	chunk_size = (ced->staged_length - ced->staged_done);
	if (chunk_size > STAGED_SZ)	/*  make sure to keep legal */
		chunk_size = STAGED_SZ;	/*   limit to max allowed */

	if (!ced->staged_read)	/*  if writing... */
		/* ...copy data into the buffer */
		ced_copy_user_space(ced, chunk_size);

	usb_fill_bulk_urb(ced->staged_urb, ced->udev,
			  ced->staged_read ? usb_rcvbulkpipe(ced->udev,
							    ced->
							    ep_addr[pipe]) :
			  usb_sndbulkpipe(ced->udev, ced->ep_addr[pipe]),
					  ced->coher_staged_io, chunk_size,
					  staged_callback, ced);
	ced->staged_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	/* in case we need to kill it */
	usb_anchor_urb(ced->staged_urb, &ced->submitted);
	retval = usb_submit_urb(ced->staged_urb, GFP_ATOMIC);
	if (retval) {
		usb_unanchor_urb(ced->staged_urb);	/*  kill it */
		ced->pipe_error[pipe] = 1; /* Flag an error to be */
					   /* handled later       */
		dev_err(&ced->interface->dev,
			"%s: submit urb failed, code %d\n",
			__func__, retval);
	} else
		/* Set the flag for staged URB pending */
		ced->staged_urb_pending = true;
	dev_dbg(&ced->interface->dev, "%s: done so far:%d, this size:%d\n",
		__func__, ced->staged_done, chunk_size);

	return retval;
}

/***************************************************************************
** ced_read_write_mem
**
** This routine is used generally for block read and write operations.
** Breaks up a read or write in to specified sized chunks, as specified by pipe
** information on maximum transfer size.
**
** Any code that calls this must be holding the staged_lock
**
** Arguments:
**    DeviceObject - pointer to our FDO (Functional Device Object)
**    read - TRUE for read, FALSE for write. This is from POV of the driver
**    ident - the transfer area number - defines memory area and more.
**    offs - the start offset within the transfer area of the start of this
**             transfer.
**    len - the number of bytes to transfer.
*/
int ced_read_write_mem(struct ced_data *ced, bool read, unsigned short ident,
		 unsigned int offs, unsigned int len)
{
	/* Transfer area info */
	struct transarea *ta = &ced->trans_def[ident];

	/*  Are we in a state to accept new requests? */
	if (!can_accept_io_requests(ced)) {
		dev_err(&ced->interface->dev, "%s: can't accept requests\n",
			__func__);
		return U14ERR_FAIL;
	}

	dev_dbg(&ced->interface->dev,
		"%s: xfer %d bytes to %s, offset %d, area %d\n",
		__func__, len, read ? "host" : "1401", offs, ident);

	/* Amazingly, we can get an escape sequence back before the current   */
	/* staged Urb is done, so we have to check for this situation and, if */
	/* so, wait until all is OK. */
	if (ced->staged_urb_pending) {
		ced->xfer_waiting = true;	/*  Flag we are waiting */
		dev_info(&ced->interface->dev,
			 "%s: xfer is waiting, as previous staged pending\n",
			 __func__);
		return U14ERR_NOERROR;
	}

	if (len == 0) {	/* allow 0-len read or write; just return success */
		dev_dbg(&ced->interface->dev,
			"%s: OK; zero-len read/write request\n", __func__);
		return U14ERR_NOERROR;
	}

	if ((ta->circular) &&	/*  Circular transfer? */
	    (ta->circ_to_host) && (read)) {	/*  In a supported direction */
				/*  If so, we sort out offset ourself */
		bool bWait = false;	/*  Flag for transfer having to wait */

		dev_dbg(&ced->interface->dev,
			"Circular buffers are %d at %d and %d at %d\n",
			ta->blocks[0].size, ta->blocks[0].offset,
			ta->blocks[1].size, ta->blocks[1].offset);

		/* Using the second block already? */
		if (ta->blocks[1].size > 0) {
			/* take offset from that */
			offs = ta->blocks[1].offset + ta->blocks[1].size;
			/* Wait if will overwrite block 0? */
			bWait = (offs + len) > ta->blocks[0].offset;
			/* or if it overflows the buffer */
			bWait |= (offs + len) > ta->length;
		} else {	/*  Area 1 not in use, try to use area 0 */
			/* Reset block 0 if not in use */
			if (ta->blocks[0].size == 0)
				ta->blocks[0].offset = 0;
			offs =
			    ta->blocks[0].offset +
			    ta->blocks[0].size;
			 /* Off the end of the buffer? */
			if ((offs + len) > ta->length) {
				/* Set up to use second block */
				ta->blocks[1].offset = 0;
				offs = 0;
				/* Wait if will overwrite block 0? */
				bWait = (offs + len) > ta->blocks[0].offset;
				/* or if it overflows the buffer */
				bWait |= (offs + len) > ta->length;
			}
		}

		if (bWait) {	/*  This transfer will have to wait? */
			ced->xfer_waiting = true;      /* Flag we are waiting */
			dev_dbg(&ced->interface->dev,
				"%s: xfer waiting for circular buffer space\n",
				__func__);
			return U14ERR_NOERROR;
		}

		dev_dbg(&ced->interface->dev,
			"%s: circular xfer, %d bytes starting at %d\n",
			__func__, len, offs);
	}
	/*  Save the parameters for the read\write transfer */
	ced->staged_read = read;	/*  Save the parameters for this read */
	ced->staged_id = ident;	/*  ID allows us to get transfer area info */
	ced->staged_offset = offs;	/*  The area within the transfer area */
	ced->staged_length = len;
	ced->staged_done = 0;	/*  Initialise the byte count */
	ced->dma_flag = MODE_LINEAR;	/*  Set DMA mode flag at this point */
	ced->xfer_waiting = false;      /* Clearly not a transfer waiting now */

/*     KeClearEvent(&ced->StagingDoneEvent); // Clear the transfer done event */
	ced_stage_chunk(ced);	/*  fire off the first chunk */

	return U14ERR_NOERROR;
}

/****************************************************************************
**
** ced_read_char
**
** Reads a character a buffer. If there is no more
**  data we return FALSE. Used as part of decoding a DMA request.
**
****************************************************************************/
static bool ced_read_char(unsigned char *character, char *buf,
			  unsigned int *n_done, unsigned int got)
{
	bool read = false;
	unsigned int done = *n_done;

	if (done < got) {	/* If there is more data */
		/* Extract the next char */
		*character = (unsigned char)buf[done];
		done++;	/* Increment the done count */
		*n_done = done;
		read = true;	/* and flag success */
	}

	return read;
}

#ifdef NOTUSED
/****************************************************************************
**
** ced_read_word
**
** Reads a word from the 1401, just uses ced_read_char twice;
** passes on any error
**
*****************************************************************************/
static bool ced_read_word(unsigned short *word, char *buf, unsigned int *n_done,
		     unsigned int got)
{
	if (ced_read_char((unsigned char *)word, buf, n_done, got))
		return ced_read_char(((unsigned char *)word) + 1, buf, n_done,
				got);
	else
		return false;
}
#endif

/****************************************************************************
** ced_read_huff
**
** Reads a coded number in and returns it, Code is:
** If data is in range 0..127 we receive 1 byte. If data in range 128-16383
** we receive two bytes, top bit of first indicates another on its way. If
** data in range 16384-4194303 we get three bytes, top two bits of first set
** to indicate three byte total.
**
*****************************************************************************/
static bool ced_read_huff(volatile unsigned int *word, char *buf,
		     unsigned int *n_done, unsigned int got)
{
	unsigned char c;	/* for each read to ced_read_char */
	bool retval = true;	/* assume we will succeed */
	unsigned int data = 0;	/* Accumulator for the data */

	if (ced_read_char(&c, buf, n_done, got)) {
		data = c;	/* copy the data */
		if ((data & 0x00000080) != 0) {	/* Bit set for more data ? */
			data &= 0x0000007F;	/* Clear the relevant bit */
			if (ced_read_char(&c, buf, n_done, got)) {
				data = (data << 8) | c;

				/* three byte sequence ? */
				if ((data & 0x00004000) != 0) {
					/* Clear the relevant bit */
					data &= 0x00003FFF;
					if (ced_read_char
					    (&c, buf, n_done, got))
						data = (data << 8) | c;
					else
						retval = false;
				}
			} else
				retval = false;	/* couldn't read data */
		}
	} else
		retval = false;

	*word = data;	/* return the data */
	return retval;
}

/***************************************************************************
**
** ced_read_dma_info
**
** Tries to read info about the dma request from the 1401 and decode it into
** the dma descriptor block. We have at this point had the escape character
** from the 1401 and now we must read in the rest of the information about
** the transfer request. Returns FALSE if 1401 fails to respond or obselete
** code from 1401 or bad parameters.
**
** The buf char pointer does not include the initial escape character, so
**  we start handling the data at offset zero.
**
*****************************************************************************/
static bool ced_read_dma_info(volatile struct dmadesc *dma_desc,
			      struct ced_data *ced,
			      char *buf, unsigned int count)
{
	bool retval = false;	/*  assume we won't succeed */
	unsigned char c;
	unsigned int n_done = 0;	/*  We haven't parsed anything so far */

	dev_dbg(&ced->interface->dev, "%s\n", __func__);

	if (ced_read_char(&c, buf, &n_done, count)) {
		/* get code for transfer type */
		unsigned char trans_code = (c & 0x0F);
		/* and area identifier */
		unsigned short ident = ((c >> 4) & 0x07);

		/*  fill in the structure we were given */
		dma_desc->trans_type = trans_code;	/*  type of transfer */
		dma_desc->ident = ident;	/*  area to use */
		dma_desc->size = 0;	/*  initialise other bits */
		dma_desc->offset = 0;

		dev_dbg(&ced->interface->dev, "%s: type: %d ident: %d\n",
			__func__, dma_desc->trans_type, dma_desc->ident);

		/* set transfer direction */
		dma_desc->outward = (trans_code != TM_EXTTOHOST);

		switch (trans_code) {

		/* Extended linear transfer modes (the only ones!) */
		case TM_EXTTOHOST:
		case TM_EXTTO1401:
			{
				retval =
				    ced_read_huff(&(dma_desc->offset), buf,
					     &n_done, count)
				    && ced_read_huff(&(dma_desc->size), buf,
						&n_done, count);
				if (retval) {
					dev_dbg(&ced->interface->dev,
						"%s: xfer offset & size %d %d\n",
						__func__, dma_desc->offset,
						dma_desc->size);

					if ((ident >= MAX_TRANSAREAS) ||	/*  Illegal area number, or... */
					    (!ced->trans_def[ident].used) ||	/*  area not set up, or... */
					    (dma_desc->offset > ced->trans_def[ident].length) ||	/*  range/size */
					    ((dma_desc->offset +
					      dma_desc->size) >
					     (ced->trans_def[ident].
					      length))) {
						retval = false;	/*  bad parameter(s) */
						dev_dbg(&ced->interface->dev,
							"%s: bad param - id %d, bUsed %d, offset %d, size %d, area length %d\n",
							__func__, ident,
							ced->trans_def[ident].
							used,
							dma_desc->offset,
							dma_desc->size,
							ced->trans_def[ident].
							length);
					}
				}
				break;
			}
		default:
			break;
		}
	} else
		retval = false;

	if (!retval)		/*  now check parameters for validity */
		dev_err(&ced->interface->dev,
			"%s: error reading Esc sequence\n",
			__func__);

	return retval;
}

/****************************************************************************
**
** ced_handle_esc
**
** Deals with an escape sequence coming from the 1401. This can either be
**  a DMA transfer request of various types or a response to an escape sequence
**  sent to the 1401. This is called from a callback.
**
** Parameters are
**
** count - the number of characters in the device extension char in buffer,
**           this is known to be at least 2 or we will not be called.
**
****************************************************************************/
static int ced_handle_esc(struct ced_data *ced, char *ch,
			 unsigned int count)
{
	int retval = U14ERR_FAIL;

	/* I have no idea what this next test is about. '?' is 0x3f, which is */
	/* area 3, code 15. At the moment, this is not used, so it does no    */
	/* harm, but unless someone can tell me what this is for, it should   */
	/* be removed from this and the Windows driver. */
	if (ch[0] == '?') {	/*  Is this an information response */
				/*  Parse and save the information */
	} else {
		spin_lock(&ced->staged_lock);	/*  Lock others out */

		/* Get DMA parameters */
		if (ced_read_dma_info(&ced->dma_info, ced, ch, count)) {
			/* check transfer type */
			unsigned short trans_type = ced->dma_info.trans_type;

			dev_dbg(&ced->interface->dev,
				"%s: xfer to %s, offset %d, length %d\n",
				__func__,
				ced->dma_info.outward ? "1401" : "host",
				ced->dma_info.offset, ced->dma_info.size);

			/* Check here for badly out of kilter... */
			if (ced->xfer_waiting) {
				/*  This can never happen, really */
				dev_err(&ced->interface->dev,
					"ERROR: DMA setup while transfer still waiting\n");
			} else {
				if ((trans_type == TM_EXTTOHOST)
				    || (trans_type == TM_EXTTO1401)) {
					retval =
					    ced_read_write_mem(ced,
							 !ced->dma_info.outward,
							 ced->dma_info.ident,
							 ced->dma_info.offset,
							 ced->dma_info.size);
					if (retval != U14ERR_NOERROR)
						dev_err(&ced->interface->dev,
							"%s: ced_read_write_mem() failed %d\n",
							__func__, retval);
				} else	/* This covers non-linear transfer setup */
					dev_err(&ced->interface->dev,
						"%s: Unknown block xfer type %d\n",
						__func__, trans_type);
			}
		} else		/*  Failed to read parameters */
			dev_err(&ced->interface->dev, "%s: ced_read_dma_info() fail\n",
				__func__);

		spin_unlock(&ced->staged_lock);	/*  OK here */
	}

	dev_dbg(&ced->interface->dev, "%s: returns %d\n", __func__, retval);

	return retval;
}

/****************************************************************************
** Callback for the character read complete or error
****************************************************************************/
static void ced_readchar_callback(struct urb *urb)
{
	struct ced_data *ced = urb->context;
	int got = urb->actual_length;	/*  what we transferred */

	if (urb->status) {	/*  Do we have a problem to handle? */
		/* The pipe number to use for error */
		int pipe = ced->n_pipes == 4 ? 1 : 0;
		/* sync/async unlink faults aren't errors... */
		/* just saying device removed or stopped     */
		if (!
		    (urb->status == -ENOENT || urb->status == -ECONNRESET
		     || urb->status == -ESHUTDOWN)) {
			dev_err(&ced->interface->dev,
				"%s: nonzero write bulk status received: %d\n",
				__func__, urb->status);
		} else
			dev_dbg(&ced->interface->dev,
				"%s: 0 chars urb->status=%d (shutdown?)\n",
				__func__, urb->status);

		spin_lock(&ced->err_lock);
		ced->errors = urb->status;
		spin_unlock(&ced->err_lock);
		got = 0;	/*   and tidy up again if so */

		spin_lock(&ced->char_in_lock);	/*  already at irq level */
		ced->pipe_error[pipe] = 1;	/*  Flag an error for later */
	} else {
		/* Esc sequence? */
		if ((got > 1) && ((ced->coher_char_in[0] & 0x7f) == 0x1b)) {
			/* handle it */
			ced_handle_esc(ced, &ced->coher_char_in[1], got - 1);

			/* already at irq level */
			spin_lock(&ced->char_in_lock);
		} else {
			/* already at irq level */
			spin_lock(&ced->char_in_lock);

			if (got > 0) {
				unsigned int i;
				if (got < INBUF_SZ) {
					/* tidy the string */
					ced->coher_char_in[got] = 0;
					dev_dbg(&ced->interface->dev,
						"%s: got %d chars >%s<\n",
						__func__, got,
						ced->coher_char_in);
				}
				/* We know that whatever we read must fit */
				/* in the input buffer                    */
				for (i = 0; i < got; i++) {
					ced->input_buffer[ced->in_buff_put++] =
					    ced->coher_char_in[i] & 0x7F;
					if (ced->in_buff_put >= INBUF_SZ)
						ced->in_buff_put = 0;
				}

				if ((ced->num_input + got) <= INBUF_SZ)
				       /* Adjust the buffer count accordingly */
					ced->num_input += got;
			} else
				dev_dbg(&ced->interface->dev, "%s: read ZLP\n",
					__func__);
		}
	}

	ced->read_chars_pending = false;  /* No longer have a pending read */
	spin_unlock(&ced->char_in_lock);  /*  already at irq level */

	ced_allowi(ced);	/*  see if we can do the next one */
}

/****************************************************************************
** ced_allowi
**
** This is used to make sure that there is always a pending input transfer so
** we can pick up any inward transfers. This can be called in multiple contexts
** so we use the irqsave version of the spinlock.
****************************************************************************/
int ced_allowi(struct ced_data *ced)
{
	int retval = U14ERR_NOERROR;
	unsigned long flags;

	/* can be called in multiple contexts */
	spin_lock_irqsave(&ced->char_in_lock, flags);

	/* We don't want char input running while DMA is in progress as we    */
	/* know that this can cause sequencing problems for the 2270. So      */
	/* don't. It will also allow the ERR response to get back to the host */
	/* code too early on some PCs, even if there is no actual driver      */
	/* failure, so we don't allow this at all. */
	if (!ced->in_draw_down &&	/* stop input if */
	    !ced->read_chars_pending &&	/* If no read request outstanding */
	    (ced->num_input < (INBUF_SZ / 2)) && /*  and there is some space */
	    (ced->dma_flag == MODE_CHAR) &&	/*   not doing any DMA */
	    (!ced->xfer_waiting) &&               /* no xfer waiting to start */
	    (can_accept_io_requests(ced))) { /* and activity is generally OK */
				/*   then off we go */
		/* max we could read */
		unsigned int max = INBUF_SZ - ced->num_input;
		/* The pipe number to use */
		int pipe = ced->n_pipes == 4 ? 1 : 0;

		dev_dbg(&ced->interface->dev, "%s: %d chars in input buffer\n",
			__func__, ced->num_input);

		usb_fill_int_urb(ced->urb_char_in, ced->udev,
				 usb_rcvintpipe(ced->udev, ced->ep_addr[pipe]),
				 ced->coher_char_in, max, ced_readchar_callback,
				 ced, ced->interval);

		/* short xfers are OK by default */
		ced->urb_char_in->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		/* in case we need to kill it */
		usb_anchor_urb(ced->urb_char_in, &ced->submitted);

		retval = usb_submit_urb(ced->urb_char_in, GFP_ATOMIC);
		if (retval) {
			/* remove from list of active Urbs */
			usb_unanchor_urb(ced->urb_char_in);
			/* Flag an error to be handled later */
			ced->pipe_error[pipe] = 1;
			dev_err(&ced->interface->dev,
				"%s: submit urb failed: %d\n",
				__func__, retval);
		} else
			/* Flag that we are active here */
			ced->read_chars_pending = true;
	}

	spin_unlock_irqrestore(&ced->char_in_lock, flags);

	return retval;
}

/*****************************************************************************
** The ioctl entry point to the driver that is used by us to talk to it.
** inode    The device node (no longer in 3.0.0 kernels)
** file     The file that is open, which holds our ced pointer
** arg    The argument passed in. Note that long is 64-bits in 64-bit system,
**        i.e. it is big enough for a 64-bit pointer.
*****************************************************************************/
static long ced_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	struct ced_data *ced = file->private_data;
	if (!can_accept_io_requests(ced))	/*  check we still exist */
		return -ENODEV;

	/* Check that access is allowed, where is is needed. Anything that */
	/* would have an indeterminate size will be checked by the         */
	/* specific command.						   */
	if (_IOC_DIR(cmd) & _IOC_READ) /* read from point of view of user... */
		/* is kernel write */
		err = !access_ok(VERIFY_WRITE,
				 (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE) /* and write from point of */
					     /* view of user...         */
		/* is kernel read */
		err = !access_ok(VERIFY_READ,
				 (void __user *)arg, _IOC_SIZE(cmd));
	if (err)
		return -EFAULT;

	switch (_IOC_NR(cmd)) {
	case _IOC_NR(IOCTL_CED_SENDSTRING(0)):
		return ced_send_string(ced, (const char __user *)arg,
				  _IOC_SIZE(cmd));

	case _IOC_NR(IOCTL_CED_RESET1401):
		return ced_reset(ced);

	case _IOC_NR(IOCTL_CED_GETCHAR):
		return ced_get_char(ced);

	case _IOC_NR(IOCTL_CED_SENDCHAR):
		return ced_send_char(ced, (char)arg);

	case _IOC_NR(IOCTL_CED_STAT1401):
		return ced_stat_1401(ced);

	case _IOC_NR(IOCTL_CED_LINECOUNT):
		return ced_line_count(ced);

	case _IOC_NR(IOCTL_CED_GETSTRING(0)):
		return ced_get_string(ced, (char __user *)arg, _IOC_SIZE(cmd));

	case _IOC_NR(IOCTL_CED_SETTRANSFER):
		return ced_set_transfer(ced,
				(struct transfer_area_desc __user *) arg);

	case _IOC_NR(IOCTL_CED_UNSETTRANSFER):
		return ced_unset_transfer(ced, (int)arg);

	case _IOC_NR(IOCTL_CED_SETEVENT):
		return ced_set_event(ced,
				     (struct transfer_event __user *) arg);

	case _IOC_NR(IOCTL_CED_GETOUTBUFSPACE):
		return ced_get_out_buf_space(ced);

	case _IOC_NR(IOCTL_CED_GETBASEADDRESS):
		return -1;

	case _IOC_NR(IOCTL_CED_GETDRIVERREVISION):
		/* USB | MAJOR | MINOR */
		return (2 << 24) | (DRIVERMAJREV << 16) | DRIVERMINREV;

	case _IOC_NR(IOCTL_CED_GETTRANSFER):
		return ced_get_transfer(ced, (TGET_TX_BLOCK __user *) arg);

	case _IOC_NR(IOCTL_CED_KILLIO1401):
		return ced_kill_io(ced);

	case _IOC_NR(IOCTL_CED_STATEOF1401):
		return ced_state_of_1401(ced);

	case _IOC_NR(IOCTL_CED_GRAB1401):
	case _IOC_NR(IOCTL_CED_FREE1401):
		return U14ERR_NOERROR;

	case _IOC_NR(IOCTL_CED_STARTSELFTEST):
		return ced_start_self_test(ced);

	case _IOC_NR(IOCTL_CED_CHECKSELFTEST):
		return ced_check_self_test(ced, (TGET_SELFTEST __user *) arg);

	case _IOC_NR(IOCTL_CED_TYPEOF1401):
		return ced_type_of_1401(ced);

	case _IOC_NR(IOCTL_CED_TRANSFERFLAGS):
		return ced_transfer_flags(ced);

	case _IOC_NR(IOCTL_CED_DBGPEEK):
		return ced_dbg_peek(ced, (TDBGBLOCK __user *) arg);

	case _IOC_NR(IOCTL_CED_DBGPOKE):
		return ced_dbg_poke(ced, (TDBGBLOCK __user *) arg);

	case _IOC_NR(IOCTL_CED_DBGRAMPDATA):
		return ced_dbg_ramp_data(ced, (TDBGBLOCK __user *) arg);

	case _IOC_NR(IOCTL_CED_DBGRAMPADDR):
		return ced_dbg_ramp_addr(ced, (TDBGBLOCK __user *) arg);

	case _IOC_NR(IOCTL_CED_DBGGETDATA):
		return ced_dbg_get_data(ced, (TDBGBLOCK __user *) arg);

	case _IOC_NR(IOCTL_CED_DBGSTOPLOOP):
		return ced_dbg_stop_loop(ced);

	case _IOC_NR(IOCTL_CED_FULLRESET):
		ced->force_reset = true; /* Set a flag for a full reset */
		break;

	case _IOC_NR(IOCTL_CED_SETCIRCULAR):
		return ced_set_circular(ced,
				      (struct transfer_area_desc __user *) arg);

	case _IOC_NR(IOCTL_CED_GETCIRCBLOCK):
		return ced_get_circ_block(ced, (TCIRCBLOCK __user *) arg);

	case _IOC_NR(IOCTL_CED_FREECIRCBLOCK):
		return ced_free_circ_block(ced, (TCIRCBLOCK __user *) arg);

	case _IOC_NR(IOCTL_CED_WAITEVENT):
		return ced_wait_event(ced, (int)(arg & 0xff), (int)(arg >> 8));

	case _IOC_NR(IOCTL_CED_TESTEVENT):
		return ced_test_event(ced, (int)arg);

	default:
		return U14ERR_NO_SUCH_FN;
	}
	return U14ERR_NOERROR;
}

static const struct file_operations ced_fops = {
	.owner = THIS_MODULE,
	.open = ced_open,
	.release = ced_release,
	.flush = ced_flush,
	.llseek = noop_llseek,
	.unlocked_ioctl = ced_ioctl,
};

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver ced_class = {
	.name = "cedusb%d",
	.fops = &ced_fops,
	.minor_base = USB_CED_MINOR_BASE,
};

/*  Check that the device that matches a 1401 vendor and product ID is OK to use and */
/*  initialise our struct ced_data. */
static int ced_probe(struct usb_interface *interface,
		     const struct usb_device_id *id)
{
	struct ced_data *ced;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int i, bcdDevice;
	int retval = -ENOMEM;

	/*  allocate memory for our device extension and initialize it */
	ced = kzalloc(sizeof(*ced), GFP_KERNEL);
	if (!ced)
		goto error;

	for (i = 0; i < MAX_TRANSAREAS; ++i) {	/*  Initialise the wait queues */
		init_waitqueue_head(&ced->trans_def[i].event);
	}

	/*  Put initialises for our stuff here. Note that all of *ced is zero, so */
	/*  no need to explicitly zero it. */
	spin_lock_init(&ced->char_out_lock);
	spin_lock_init(&ced->char_in_lock);
	spin_lock_init(&ced->staged_lock);

	/*  Initialises from the skeleton stuff */
	kref_init(&ced->kref);
	mutex_init(&ced->io_mutex);
	spin_lock_init(&ced->err_lock);
	init_usb_anchor(&ced->submitted);

	ced->udev = usb_get_dev(interface_to_usbdev(interface));
	ced->interface = interface;

	/*  Attempt to identify the device */
	bcdDevice = ced->udev->descriptor.bcdDevice;
	i = (bcdDevice >> 8);
	if (i == 0)
		ced->type = TYPEU1401;
	else if ((i >= 1) && (i <= 23))
		ced->type = i + 2;
	else {
		dev_err(&interface->dev, "%s: Unknown device. bcdDevice = %d\n",
			__func__, bcdDevice);
		goto error;
	}
	/*  set up the endpoint information. We only care about the number of EP as */
	/*  we know that we are dealing with a 1401 device. */
	iface_desc = interface->cur_altsetting;
	ced->n_pipes = iface_desc->desc.bNumEndpoints;
	dev_info(&interface->dev, "1401Type=%d with %d End Points\n",
		 ced->type, ced->n_pipes);
	if ((ced->n_pipes < 3) || (ced->n_pipes > 4))
		goto error;

	/*  Allocate the URBs we hold for performing transfers */
	ced->urb_char_out = usb_alloc_urb(0, GFP_KERNEL);	/*  character output URB */
	ced->urb_char_in = usb_alloc_urb(0, GFP_KERNEL);	/*  character input URB */
	ced->staged_urb = usb_alloc_urb(0, GFP_KERNEL);	/*  block transfer URB */
	if (!ced->urb_char_out || !ced->urb_char_in || !ced->staged_urb) {
		dev_err(&interface->dev, "%s: URB alloc failed\n", __func__);
		goto error;
	}

	ced->coher_staged_io =
	    usb_alloc_coherent(ced->udev, STAGED_SZ, GFP_KERNEL,
			       &ced->staged_urb->transfer_dma);
	ced->coher_char_out =
	    usb_alloc_coherent(ced->udev, OUTBUF_SZ, GFP_KERNEL,
			       &ced->urb_char_out->transfer_dma);
	ced->coher_char_in =
	    usb_alloc_coherent(ced->udev, INBUF_SZ, GFP_KERNEL,
			       &ced->urb_char_in->transfer_dma);
	if (!ced->coher_char_out || !ced->coher_char_in ||
	    !ced->coher_staged_io) {
		dev_err(&interface->dev, "%s: Coherent buffer alloc failed\n",
			__func__);
		goto error;
	}

	for (i = 0; i < ced->n_pipes; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		ced->ep_addr[i] = endpoint->bEndpointAddress;
		dev_info(&interface->dev, "Pipe %d, ep address %02x\n",
			 i, ced->ep_addr[i]);
		if (((ced->n_pipes == 3) && (i == 0)) ||	/*  if char input end point */
		    ((ced->n_pipes == 4) && (i == 1))) {
			/* save the endpoint interrupt interval */
			ced->interval = endpoint->bInterval;
			dev_info(&interface->dev, "Pipe %d, interval = %d\n",
				 i, ced->interval);
		}
		/*  Detect USB2 by checking last ep size (64 if USB1) */
		if (i == ced->n_pipes - 1) {	/*  if this is the last ep (bulk) */
			ced->is_usb2 =
			    le16_to_cpu(endpoint->wMaxPacketSize) > 64;
			dev_info(&ced->interface->dev, "USB%d\n",
				 ced->is_usb2 + 1);
		}
	}

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, ced);

	/* we can register the device now, as it is ready */
	retval = usb_register_dev(interface, &ced_class);
	if (retval) {
		/* something prevented us from registering this driver */
		dev_err(&interface->dev,
			"Not able to get a minor for this device\n");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	/* let the user know what node this device is now attached to */
	dev_info(&interface->dev,
		 "USB CEDUSB device now attached to cedusb #%d\n",
		 interface->minor);
	return 0;

error:
	if (ced)
		kref_put(&ced->kref, ced_delete);	/*  frees allocated memory */
	return retval;
}

static void ced_disconnect(struct usb_interface *interface)
{
	struct ced_data *ced = usb_get_intfdata(interface);
	int minor = interface->minor;
	int i;

	usb_set_intfdata(interface, NULL);	/*  remove the ced from the interface */
	usb_deregister_dev(interface, &ced_class);	/*  give back our minor device number */

	mutex_lock(&ced->io_mutex);	/*  stop more I/O starting while... */
	ced_draw_down(ced);	/*  ...wait for then kill any io */
	for (i = 0; i < MAX_TRANSAREAS; ++i) {
		int err = ced_clear_area(ced, i);	/*  ...release any used memory */
		if (err == U14ERR_UNLOCKFAIL)
			dev_err(&ced->interface->dev, "%s: Area %d was in used\n",
				__func__, i);
	}
	ced->interface = NULL;	/*  ...we kill off link to interface */
	mutex_unlock(&ced->io_mutex);

	usb_kill_anchored_urbs(&ced->submitted);

	kref_put(&ced->kref, ced_delete);	/*  decrement our usage count */

	dev_info(&interface->dev, "USB cedusb #%d now disconnected\n", minor);
}

/*  Wait for all the urbs we know of to be done with, then kill off any that */
/*  are left. NBNB we will need to have a mechanism to stop circular xfers */
/*  from trying to fire off more urbs. We will wait up to 3 seconds for Urbs */
/*  to be done. */
void ced_draw_down(struct ced_data *ced)
{
	int time;
	dev_dbg(&ced->interface->dev, "%s: called\n", __func__);

	ced->in_draw_down = true;
	time = usb_wait_anchor_empty_timeout(&ced->submitted, 3000);
	if (!time) {		/*  if we timed out we kill the urbs */
		usb_kill_anchored_urbs(&ced->submitted);
		dev_err(&ced->interface->dev, "%s: timed out\n", __func__);
	}
	ced->in_draw_down = false;
}

static int ced_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct ced_data *ced = usb_get_intfdata(intf);
	if (!ced)
		return 0;
	ced_draw_down(ced);

	dev_dbg(&ced->interface->dev, "%s: called\n", __func__);
	return 0;
}

static int ced_resume(struct usb_interface *intf)
{
	struct ced_data *ced = usb_get_intfdata(intf);
	if (!ced)
		return 0;
	dev_dbg(&ced->interface->dev, "%s: called\n", __func__);
	return 0;
}

static int ced_pre_reset(struct usb_interface *intf)
{
	struct ced_data *ced = usb_get_intfdata(intf);
	dev_dbg(&ced->interface->dev, "%s\n", __func__);
	mutex_lock(&ced->io_mutex);
	ced_draw_down(ced);
	return 0;
}

static int ced_post_reset(struct usb_interface *intf)
{
	struct ced_data *ced = usb_get_intfdata(intf);
	dev_dbg(&ced->interface->dev, "%s\n", __func__);

	/* we are sure no URBs are active - no locking needed */
	ced->errors = -EPIPE;
	mutex_unlock(&ced->io_mutex);

	return 0;
}

static struct usb_driver ced_driver = {
	.name = "cedusb",
	.probe = ced_probe,
	.disconnect = ced_disconnect,
	.suspend = ced_suspend,
	.resume = ced_resume,
	.pre_reset = ced_pre_reset,
	.post_reset = ced_post_reset,
	.id_table = ced_table,
	.supports_autosuspend = 1,
};

module_usb_driver(ced_driver);
MODULE_LICENSE("GPL");
