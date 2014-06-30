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
device state is held in the DEVICE_EXTENSION (named to match Windows use).
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
	DEVICE_EXTENSION *pdx = to_DEVICE_EXTENSION(kref);

	/*  Free up the output buffer, then free the output urb. Note that the interface member */
	/*  of pdx will probably be NULL, so cannot be used to get to dev. */
	usb_free_coherent(pdx->udev, OUTBUF_SZ, pdx->pCoherCharOut,
			  pdx->pUrbCharOut->transfer_dma);
	usb_free_urb(pdx->pUrbCharOut);

	/*  Do the same for chan input */
	usb_free_coherent(pdx->udev, INBUF_SZ, pdx->pCoherCharIn,
			  pdx->pUrbCharIn->transfer_dma);
	usb_free_urb(pdx->pUrbCharIn);

	/*  Do the same for the block transfers */
	usb_free_coherent(pdx->udev, STAGED_SZ, pdx->pCoherStagedIO,
			  pdx->pStagedUrb->transfer_dma);
	usb_free_urb(pdx->pStagedUrb);

	usb_put_dev(pdx->udev);
	kfree(pdx);
}

/*  This is the driver end of the open() call from user space. */
static int ced_open(struct inode *inode, struct file *file)
{
	DEVICE_EXTENSION *pdx;
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

	pdx = usb_get_intfdata(interface);
	if (!pdx) {
		retval = -ENODEV;
		goto exit;
	}

	dev_dbg(&interface->dev, "%s: got pdx\n", __func__);

	/* increment our usage count for the device */
	kref_get(&pdx->kref);

	/* lock the device to allow correctly handling errors
	 * in resumption */
	mutex_lock(&pdx->io_mutex);

	if (!pdx->open_count++) {
		retval = usb_autopm_get_interface(interface);
		if (retval) {
			pdx->open_count--;
			mutex_unlock(&pdx->io_mutex);
			kref_put(&pdx->kref, ced_delete);
			goto exit;
		}
	} else {		/* uncomment this block if you want exclusive open */
		dev_err(&interface->dev, "%s: fail: already open\n", __func__);
		retval = -EBUSY;
		pdx->open_count--;
		mutex_unlock(&pdx->io_mutex);
		kref_put(&pdx->kref, ced_delete);
		goto exit;
	}
	/* prevent the device from being autosuspended */

	/* save our object in the file's private structure */
	file->private_data = pdx;
	mutex_unlock(&pdx->io_mutex);

exit:
	return retval;
}

static int ced_release(struct inode *inode, struct file *file)
{
	DEVICE_EXTENSION *pdx = file->private_data;
	if (pdx == NULL)
		return -ENODEV;

	dev_dbg(&pdx->interface->dev, "%s: called\n", __func__);
	mutex_lock(&pdx->io_mutex);
	if (!--pdx->open_count && pdx->interface)	/*  Allow autosuspend */
		usb_autopm_put_interface(pdx->interface);
	mutex_unlock(&pdx->io_mutex);

	kref_put(&pdx->kref, ced_delete);	/*  decrement the count on our device */
	return 0;
}

static int ced_flush(struct file *file, fl_owner_t id)
{
	int res;
	DEVICE_EXTENSION *pdx = file->private_data;
	if (pdx == NULL)
		return -ENODEV;

	dev_dbg(&pdx->interface->dev, "%s: char in pend=%d\n",
		__func__, pdx->bReadCharsPending);

	/* wait for io to stop */
	mutex_lock(&pdx->io_mutex);
	dev_dbg(&pdx->interface->dev, "%s: got io_mutex\n", __func__);
	ced_draw_down(pdx);

	/* read out errors, leave subsequent opens a clean slate */
	spin_lock_irq(&pdx->err_lock);
	res = pdx->errors ? (pdx->errors == -EPIPE ? -EPIPE : -EIO) : 0;
	pdx->errors = 0;
	spin_unlock_irq(&pdx->err_lock);

	mutex_unlock(&pdx->io_mutex);
	dev_dbg(&pdx->interface->dev, "%s: exit reached\n", __func__);

	return res;
}

/***************************************************************************
** can_accept_io_requests
** If the device is removed, interface is set NULL. We also clear our pointer
** from the interface, so we should make sure that pdx is not NULL. This will
** not help with a device extension held by a file.
** return true if can accept new io requests, else false
*/
static bool can_accept_io_requests(DEVICE_EXTENSION *pdx)
{
	return pdx && pdx->interface;	/*  Can we accept IO requests */
}

/****************************************************************************
** Callback routine to complete writes. This may need to fire off another
** urb to complete the transfer.
****************************************************************************/
static void ced_writechar_callback(struct urb *pUrb)
{
	DEVICE_EXTENSION *pdx = pUrb->context;
	int nGot = pUrb->actual_length;	/*  what we transferred */

	if (pUrb->status) {	/*  sync/async unlink faults aren't errors */
		if (!
		    (pUrb->status == -ENOENT || pUrb->status == -ECONNRESET
		     || pUrb->status == -ESHUTDOWN)) {
			dev_err(&pdx->interface->dev,
				"%s: nonzero write bulk status received: %d\n",
				__func__, pUrb->status);
		}

		spin_lock(&pdx->err_lock);
		pdx->errors = pUrb->status;
		spin_unlock(&pdx->err_lock);
		nGot = 0;	/*   and tidy up again if so */

		spin_lock(&pdx->charOutLock);	/*  already at irq level */
		pdx->dwOutBuffGet = 0;	/*  Reset the output buffer */
		pdx->dwOutBuffPut = 0;
		pdx->dwNumOutput = 0;	/*  Clear the char count */
		pdx->bPipeError[0] = 1;	/*  Flag an error for later */
		pdx->bSendCharsPending = false;	/*  Allow other threads again */
		spin_unlock(&pdx->charOutLock);	/*  already at irq level */
		dev_dbg(&pdx->interface->dev,
			"%s: char out done, 0 chars sent\n", __func__);
	} else {
		dev_dbg(&pdx->interface->dev,
			"%s: char out done, %d chars sent\n", __func__, nGot);
		spin_lock(&pdx->charOutLock);	/*  already at irq level */
		pdx->dwNumOutput -= nGot;	/*  Now adjust the char send buffer */
		pdx->dwOutBuffGet += nGot;	/*  to match what we did */
		if (pdx->dwOutBuffGet >= OUTBUF_SZ)	/*  Can't do this any earlier as data could be overwritten */
			pdx->dwOutBuffGet = 0;

		if (pdx->dwNumOutput > 0) {	/*  if more to be done... */
			int nPipe = 0;	/*  The pipe number to use */
			int iReturn;
			char *pDat = &pdx->outputBuffer[pdx->dwOutBuffGet];
			unsigned int dwCount = pdx->dwNumOutput;	/*  maximum to send */
			if ((pdx->dwOutBuffGet + dwCount) > OUTBUF_SZ)	/*  does it cross buffer end? */
				dwCount = OUTBUF_SZ - pdx->dwOutBuffGet;
			spin_unlock(&pdx->charOutLock);	/*  we are done with stuff that changes */
			memcpy(pdx->pCoherCharOut, pDat, dwCount);	/*  copy output data to the buffer */
			usb_fill_bulk_urb(pdx->pUrbCharOut, pdx->udev,
					  usb_sndbulkpipe(pdx->udev,
							  pdx->epAddr[0]),
					  pdx->pCoherCharOut, dwCount,
					  ced_writechar_callback, pdx);
			pdx->pUrbCharOut->transfer_flags |=
			    URB_NO_TRANSFER_DMA_MAP;
			usb_anchor_urb(pdx->pUrbCharOut, &pdx->submitted);	/*  in case we need to kill it */
			iReturn = usb_submit_urb(pdx->pUrbCharOut, GFP_ATOMIC);
			dev_dbg(&pdx->interface->dev, "%s: n=%d>%s<\n",
				__func__, dwCount, pDat);
			spin_lock(&pdx->charOutLock);	/*  grab lock for errors */
			if (iReturn) {
				pdx->bPipeError[nPipe] = 1;	/*  Flag an error to be handled later */
				pdx->bSendCharsPending = false;	/*  Allow other threads again */
				usb_unanchor_urb(pdx->pUrbCharOut);
				dev_err(&pdx->interface->dev,
					"%s: usb_submit_urb() returned %d\n",
					__func__, iReturn);
			}
		} else
			pdx->bSendCharsPending = false;	/*  Allow other threads again */
		spin_unlock(&pdx->charOutLock);	/*  already at irq level */
	}
}

/****************************************************************************
** ced_send_chars
** Transmit the characters in the output buffer to the 1401. This may need
** breaking down into multiple transfers.
****************************************************************************/
int ced_send_chars(DEVICE_EXTENSION *pdx)
{
	int iReturn = U14ERR_NOERROR;

	spin_lock_irq(&pdx->charOutLock);	/*  Protect ourselves */

	if ((!pdx->bSendCharsPending) &&	/*  Not currently sending */
	    (pdx->dwNumOutput > 0) &&	/*   has characters to output */
	    (can_accept_io_requests(pdx)))	{ /*   and current activity is OK */
		unsigned int dwCount = pdx->dwNumOutput;	/*  Get a copy of the character count */
		pdx->bSendCharsPending = true;	/*  Set flag to lock out other threads */

		dev_dbg(&pdx->interface->dev,
			"Send %d chars to 1401, EP0 flag %d\n",
			dwCount, pdx->nPipes == 3);
		/*  If we have only 3 end points we must send the characters to the 1401 using EP0. */
		if (pdx->nPipes == 3) {
			/*  For EP0 character transmissions to the 1401, we have to hang about until they */
			/*  are gone, as otherwise without more character IO activity they will never go. */
			unsigned int count = dwCount;	/*  Local char counter */
			unsigned int index = 0;	/*  The index into the char buffer */

			spin_unlock_irq(&pdx->charOutLock);	/*  Free spinlock as we call USBD */

			while ((count > 0) && (iReturn == U14ERR_NOERROR)) {
				/*  We have to break the transfer up into 64-byte chunks because of a 2270 problem */
				int n = count > 64 ? 64 : count;	/*  Chars for this xfer, max of 64 */
				int nSent = usb_control_msg(pdx->udev,
							    usb_sndctrlpipe(pdx->udev, 0),	/*  use end point 0 */
							    DB_CHARS,	/*  bRequest */
							    (H_TO_D | VENDOR | DEVREQ),	/*  to the device, vendor request to the device */
							    0, 0,	/*  value and index are both 0 */
							    &pdx->outputBuffer[index],	/*  where to send from */
							    n,	/*  how much to send */
							    1000);	/*  timeout in jiffies */
				if (nSent <= 0) {
					iReturn = nSent ? nSent : -ETIMEDOUT;	/*  if 0 chars says we timed out */
					dev_err(&pdx->interface->dev,
						"Send %d chars by EP0 failed: %d\n",
						n, iReturn);
				} else {
					dev_dbg(&pdx->interface->dev,
						"Sent %d chars by EP0\n", n);
					count -= nSent;
					index += nSent;
				}
			}

			spin_lock_irq(&pdx->charOutLock);	/*  Protect pdx changes, released by general code */
			pdx->dwOutBuffGet = 0;	/*  so reset the output buffer */
			pdx->dwOutBuffPut = 0;
			pdx->dwNumOutput = 0;	/*  and clear the buffer count */
			pdx->bSendCharsPending = false;	/*  Allow other threads again */
		} else {	/*  Here for sending chars normally - we hold the spin lock */
			int nPipe = 0;	/*  The pipe number to use */
			char *pDat = &pdx->outputBuffer[pdx->dwOutBuffGet];

			if ((pdx->dwOutBuffGet + dwCount) > OUTBUF_SZ)	/*  does it cross buffer end? */
				dwCount = OUTBUF_SZ - pdx->dwOutBuffGet;
			spin_unlock_irq(&pdx->charOutLock);	/*  we are done with stuff that changes */
			memcpy(pdx->pCoherCharOut, pDat, dwCount);	/*  copy output data to the buffer */
			usb_fill_bulk_urb(pdx->pUrbCharOut, pdx->udev,
					  usb_sndbulkpipe(pdx->udev,
							  pdx->epAddr[0]),
					  pdx->pCoherCharOut, dwCount,
					  ced_writechar_callback, pdx);
			pdx->pUrbCharOut->transfer_flags |=
			    URB_NO_TRANSFER_DMA_MAP;
			usb_anchor_urb(pdx->pUrbCharOut, &pdx->submitted);
			iReturn = usb_submit_urb(pdx->pUrbCharOut, GFP_KERNEL);
			spin_lock_irq(&pdx->charOutLock);	/*  grab lock for errors */
			if (iReturn) {
				pdx->bPipeError[nPipe] = 1;	/*  Flag an error to be handled later */
				pdx->bSendCharsPending = false;	/*  Allow other threads again */
				usb_unanchor_urb(pdx->pUrbCharOut);	/*  remove from list of active urbs */
			}
		}
	} else if (pdx->bSendCharsPending && (pdx->dwNumOutput > 0))
		dev_dbg(&pdx->interface->dev,
			"%s: bSendCharsPending:true\n", __func__);

	dev_dbg(&pdx->interface->dev, "%s: exit code: %d\n", __func__, iReturn);
	spin_unlock_irq(&pdx->charOutLock);	/*  Now let go of the spinlock */
	return iReturn;
}

/***************************************************************************
** ced_copy_user_space
** This moves memory between pinned down user space and the pCoherStagedIO
** memory buffer we use for transfers. Copy n bytes in the directions that
** is defined by pdx->StagedRead. The user space is determined by the area
** in pdx->StagedId and the offset in pdx->StagedDone. The user
** area may well not start on a page boundary, so allow for that.
**
** We have a table of physical pages that describe the area, so we can use
** this to get a virtual address that the kernel can use.
**
** pdx  Is our device extension which holds all we know about the transfer.
** n    The number of bytes to move one way or the other.
***************************************************************************/
static void ced_copy_user_space(DEVICE_EXTENSION *pdx, int n)
{
	unsigned int nArea = pdx->StagedId;
	if (nArea < MAX_TRANSAREAS) {
		TRANSAREA *pArea = &pdx->rTransDef[nArea];	/*  area to be used */
		unsigned int dwOffset =
		    pdx->StagedDone + pdx->StagedOffset + pArea->dwBaseOffset;
		char *pCoherBuf = pdx->pCoherStagedIO;	/*  coherent buffer */
		if (!pArea->bUsed) {
			dev_err(&pdx->interface->dev, "%s: area %d unused\n",
				__func__, nArea);
			return;
		}

		while (n) {
			int nPage = dwOffset >> PAGE_SHIFT;	/*  page number in table */
			if (nPage < pArea->nPages) {
				char *pvAddress =
				    (char *)kmap_atomic(pArea->pPages[nPage]);
				if (pvAddress) {
					unsigned int uiPageOff = dwOffset & (PAGE_SIZE - 1);	/*  offset into the page */
					size_t uiXfer = PAGE_SIZE - uiPageOff;	/*  max to transfer on this page */
					if (uiXfer > n)	/*  limit byte count if too much */
						uiXfer = n;	/*  for the page */
					if (pdx->StagedRead)
						memcpy(pvAddress + uiPageOff,
						       pCoherBuf, uiXfer);
					else
						memcpy(pCoherBuf,
						       pvAddress + uiPageOff,
						       uiXfer);
					kunmap_atomic(pvAddress);
					dwOffset += uiXfer;
					pCoherBuf += uiXfer;
					n -= uiXfer;
				} else {
					dev_err(&pdx->interface->dev,
						"%s: did not map page %d\n",
						__func__, nPage);
					return;
				}

			} else {
				dev_err(&pdx->interface->dev,
					"%s: exceeded pages %d\n",
					__func__, nPage);
				return;
			}
		}
	} else
		dev_err(&pdx->interface->dev, "%s: bad area %d\n",
			__func__, nArea);
}

/*  Forward declarations for stuff used circularly */
static int ced_stage_chunk(DEVICE_EXTENSION *pdx);
/***************************************************************************
** ReadWrite_Complete
**
**  Completion routine for our staged read/write Irps
*/
static void staged_callback(struct urb *pUrb)
{
	DEVICE_EXTENSION *pdx = pUrb->context;
	unsigned int nGot = pUrb->actual_length;	/*  what we transferred */
	bool bCancel = false;
	bool bRestartCharInput;	/*  used at the end */

	spin_lock(&pdx->stagedLock);	/*  stop ced_read_write_mem() action while this routine is running */
	pdx->bStagedUrbPending = false;	/*  clear the flag for staged IRP pending */

	if (pUrb->status) {	/*  sync/async unlink faults aren't errors */
		if (!
		    (pUrb->status == -ENOENT || pUrb->status == -ECONNRESET
		     || pUrb->status == -ESHUTDOWN)) {
			dev_err(&pdx->interface->dev,
				"%s: nonzero write bulk status received: %d\n",
				__func__, pUrb->status);
		} else
			dev_info(&pdx->interface->dev,
				 "%s: staged xfer cancelled\n", __func__);

		spin_lock(&pdx->err_lock);
		pdx->errors = pUrb->status;
		spin_unlock(&pdx->err_lock);
		nGot = 0;	/*   and tidy up again if so */
		bCancel = true;
	} else {
		dev_dbg(&pdx->interface->dev, "%s: %d chars xferred\n",
			__func__, nGot);
		if (pdx->StagedRead)	/*  if reading, save to user space */
			ced_copy_user_space(pdx, nGot);	/*  copy from buffer to user */
		if (nGot == 0)
			dev_dbg(&pdx->interface->dev, "%s: ZLP\n", __func__);
	}

	/*  Update the transfer length based on the TransferBufferLength value in the URB */
	pdx->StagedDone += nGot;

	dev_dbg(&pdx->interface->dev, "%s: done %d bytes of %d\n",
		__func__, pdx->StagedDone, pdx->StagedLength);

	if ((pdx->StagedDone == pdx->StagedLength) ||	/*  If no more to do */
	    (bCancel)) {		/*  or this IRP was cancelled */
		TRANSAREA *pArea = &pdx->rTransDef[pdx->StagedId];	/*  Transfer area info */
		dev_dbg(&pdx->interface->dev,
			"%s: transfer done, bytes %d, cancel %d\n",
			__func__, pdx->StagedDone, bCancel);

		/*  Here is where we sort out what to do with this transfer if using a circular buffer. We have */
		/*   a completed transfer that can be assumed to fit into the transfer area. We should be able to */
		/*   add this to the end of a growing block or to use it to start a new block unless the code */
		/*   that calculates the offset to use (in ced_read_write_mem) is totally duff. */
		if ((pArea->bCircular) && (pArea->bCircToHost) && (!bCancel) &&	/*  Time to sort out circular buffer info? */
		    (pdx->StagedRead)) {	/*  Only for tohost transfers for now */
			if (pArea->aBlocks[1].dwSize > 0) {	/*  If block 1 is in use we must append to it */
				if (pdx->StagedOffset ==
				    (pArea->aBlocks[1].dwOffset +
				     pArea->aBlocks[1].dwSize)) {
					pArea->aBlocks[1].dwSize +=
					    pdx->StagedLength;
					dev_dbg(&pdx->interface->dev,
						"RWM_Complete, circ block 1 now %d bytes at %d\n",
						pArea->aBlocks[1].dwSize,
						pArea->aBlocks[1].dwOffset);
				} else {
					/*  Here things have gone very, very, wrong, but I cannot see how this can actually be achieved */
					pArea->aBlocks[1].dwOffset =
					    pdx->StagedOffset;
					pArea->aBlocks[1].dwSize =
					    pdx->StagedLength;
					dev_err(&pdx->interface->dev,
						"%s: ERROR, circ block 1 re-started %d bytes at %d\n",
						__func__,
						pArea->aBlocks[1].dwSize,
						pArea->aBlocks[1].dwOffset);
				}
			} else {	/*  If block 1 is not used, we try to add to block 0 */
				if (pArea->aBlocks[0].dwSize > 0) {	/*  Got stored block 0 information? */
					/*  Must append onto the existing block 0 */
					if (pdx->StagedOffset ==
					    (pArea->aBlocks[0].dwOffset +
					     pArea->aBlocks[0].dwSize)) {
						pArea->aBlocks[0].dwSize += pdx->StagedLength;	/*  Just add this transfer in */
						dev_dbg(&pdx->interface->dev,
							"RWM_Complete, circ block 0 now %d bytes at %d\n",
							pArea->aBlocks[0].
							dwSize,
							pArea->aBlocks[0].
							dwOffset);
					} else {	/*  If it doesn't append, put into new block 1 */
						pArea->aBlocks[1].dwOffset =
						    pdx->StagedOffset;
						pArea->aBlocks[1].dwSize =
						    pdx->StagedLength;
						dev_dbg(&pdx->interface->dev,
							"RWM_Complete, circ block 1 started %d bytes at %d\n",
							pArea->aBlocks[1].
							dwSize,
							pArea->aBlocks[1].
							dwOffset);
					}
				} else	{ /*  No info stored yet, just save in block 0 */
					pArea->aBlocks[0].dwOffset =
					    pdx->StagedOffset;
					pArea->aBlocks[0].dwSize =
					    pdx->StagedLength;
					dev_dbg(&pdx->interface->dev,
						"RWM_Complete, circ block 0 started %d bytes at %d\n",
						pArea->aBlocks[0].dwSize,
						pArea->aBlocks[0].dwOffset);
				}
			}
		}

		if (!bCancel) { /*  Don't generate an event if cancelled */
			dev_dbg(&pdx->interface->dev,
				"RWM_Complete,  bCircular %d, bToHost %d, eStart %d, eSize %d\n",
				pArea->bCircular, pArea->bEventToHost,
				pArea->dwEventSt, pArea->dwEventSz);
			if ((pArea->dwEventSz) &&	/*  Set a user-mode event... */
			    (pdx->StagedRead == pArea->bEventToHost)) {	/*  ...on transfers in this direction? */
				int iWakeUp = 0;	/*  assume */
				/*  If we have completed the right sort of DMA transfer then set the event to notify */
				/*    the user code to wake up anyone that is waiting. */
				if ((pArea->bCircular) &&	/*  Circular areas use a simpler test */
				    (pArea->bCircToHost)) {	/*  only in supported direction */
					/*  Is total data waiting up to size limit? */
					unsigned int dwTotal =
					    pArea->aBlocks[0].dwSize +
					    pArea->aBlocks[1].dwSize;
					iWakeUp = (dwTotal >= pArea->dwEventSz);
				} else {
					unsigned int transEnd =
					    pdx->StagedOffset +
					    pdx->StagedLength;
					unsigned int eventEnd =
					    pArea->dwEventSt + pArea->dwEventSz;
					iWakeUp = (pdx->StagedOffset < eventEnd)
					    && (transEnd > pArea->dwEventSt);
				}

				if (iWakeUp) {
					dev_dbg(&pdx->interface->dev,
						"About to set event to notify app\n");
					wake_up_interruptible(&pArea->wqEvent);	/*  wake up waiting processes */
					++pArea->iWakeUp;	/*  increment wakeup count */
				}
			}
		}

		pdx->dwDMAFlag = MODE_CHAR;	/*  Switch back to char mode before ced_read_write_mem call */

		if (!bCancel) {	/*  Don't look for waiting transfer if cancelled */
			/*  If we have a transfer waiting, kick it off */
			if (pdx->bXFerWaiting) {	/*  Got a block xfer waiting? */
				int iReturn;
				dev_info(&pdx->interface->dev,
					 "*** RWM_Complete *** pending transfer will now be set up!!!\n");
				iReturn =
				    ced_read_write_mem(pdx, !pdx->rDMAInfo.bOutWard,
						 pdx->rDMAInfo.wIdent,
						 pdx->rDMAInfo.dwOffset,
						 pdx->rDMAInfo.dwSize);

				if (iReturn)
					dev_err(&pdx->interface->dev,
						"RWM_Complete rw setup failed %d\n",
						iReturn);
			}
		}

	} else			/*  Here for more to do */
		ced_stage_chunk(pdx);	/*  fire off the next bit */

	/*  While we hold the stagedLock, see if we should reallow character input ints */
	/*  Don't allow if cancelled, or if a new block has started or if there is a waiting block. */
	/*  This feels wrong as we should ask which spin lock protects dwDMAFlag. */
	bRestartCharInput = !bCancel && (pdx->dwDMAFlag == MODE_CHAR)
	    && !pdx->bXFerWaiting;

	spin_unlock(&pdx->stagedLock);	/*  Finally release the lock again */

	/*  This is not correct as dwDMAFlag is protected by the staged lock, but it is treated */
	/*  in ced_allowi as if it were protected by the char lock. In any case, most systems will */
	/*  not be upset by char input during DMA... sigh. Needs sorting out. */
	if (bRestartCharInput)	/*  may be out of date, but... */
		ced_allowi(pdx);	/*  ...ced_allowi tests a lock too. */
	dev_dbg(&pdx->interface->dev, "%s: done\n", __func__);
}

/****************************************************************************
** ced_stage_chunk
**
** Generates the next chunk of data making up a staged transfer.
**
** The calling code must have acquired the staging spinlock before calling
**  this function, and is responsible for releasing it. We are at callback level.
****************************************************************************/
static int ced_stage_chunk(DEVICE_EXTENSION *pdx)
{
	int iReturn = U14ERR_NOERROR;
	unsigned int ChunkSize;
	int nPipe = pdx->StagedRead ? 3 : 2;	/*  The pipe number to use for reads or writes */
	if (pdx->nPipes == 3)
		nPipe--;	/*  Adjust for the 3-pipe case */
	if (nPipe < 0)		/*  and trap case that should never happen */
		return U14ERR_FAIL;

	if (!can_accept_io_requests(pdx)) {	/*  got sudden remove? */
		dev_info(&pdx->interface->dev, "%s: sudden remove, giving up\n",
			 __func__);
		return U14ERR_FAIL;	/*  could do with a better error */
	}

	ChunkSize = (pdx->StagedLength - pdx->StagedDone);	/*  transfer length remaining */
	if (ChunkSize > STAGED_SZ)	/*  make sure to keep legal */
		ChunkSize = STAGED_SZ;	/*   limit to max allowed */

	if (!pdx->StagedRead)	/*  if writing... */
		ced_copy_user_space(pdx, ChunkSize);	/*  ...copy data into the buffer */

	usb_fill_bulk_urb(pdx->pStagedUrb, pdx->udev,
			  pdx->StagedRead ? usb_rcvbulkpipe(pdx->udev,
							    pdx->
							    epAddr[nPipe]) :
			  usb_sndbulkpipe(pdx->udev, pdx->epAddr[nPipe]),
			  pdx->pCoherStagedIO, ChunkSize, staged_callback, pdx);
	pdx->pStagedUrb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	usb_anchor_urb(pdx->pStagedUrb, &pdx->submitted);	/*  in case we need to kill it */
	iReturn = usb_submit_urb(pdx->pStagedUrb, GFP_ATOMIC);
	if (iReturn) {
		usb_unanchor_urb(pdx->pStagedUrb);	/*  kill it */
		pdx->bPipeError[nPipe] = 1;	/*  Flag an error to be handled later */
		dev_err(&pdx->interface->dev, "%s: submit urb failed, code %d\n",
			__func__, iReturn);
	} else
		pdx->bStagedUrbPending = true;	/*  Set the flag for staged URB pending */
	dev_dbg(&pdx->interface->dev, "%s: done so far:%d, this size:%d\n",
		__func__, pdx->StagedDone, ChunkSize);

	return iReturn;
}

/***************************************************************************
** ced_read_write_mem
**
** This routine is used generally for block read and write operations.
** Breaks up a read or write in to specified sized chunks, as specified by pipe
** information on maximum transfer size.
**
** Any code that calls this must be holding the stagedLock
**
** Arguments:
**    DeviceObject - pointer to our FDO (Functional Device Object)
**    Read - TRUE for read, FALSE for write. This is from POV of the driver
**    wIdent - the transfer area number - defines memory area and more.
**    dwOffs - the start offset within the transfer area of the start of this
**             transfer.
**    dwLen - the number of bytes to transfer.
*/
int ced_read_write_mem(DEVICE_EXTENSION *pdx, bool Read, unsigned short wIdent,
		 unsigned int dwOffs, unsigned int dwLen)
{
	TRANSAREA *pArea = &pdx->rTransDef[wIdent];	/*  Transfer area info */

	if (!can_accept_io_requests(pdx)) {	/*  Are we in a state to accept new requests? */
		dev_err(&pdx->interface->dev, "%s: can't accept requests\n",
			__func__);
		return U14ERR_FAIL;
	}

	dev_dbg(&pdx->interface->dev,
		"%s: xfer %d bytes to %s, offset %d, area %d\n",
		__func__, dwLen, Read ? "host" : "1401", dwOffs, wIdent);

	/*  Amazingly, we can get an escape sequence back before the current staged Urb is done, so we */
	/*   have to check for this situation and, if so, wait until all is OK. */
	if (pdx->bStagedUrbPending) {
		pdx->bXFerWaiting = true;	/*  Flag we are waiting */
		dev_info(&pdx->interface->dev,
			 "%s: xfer is waiting, as previous staged pending\n",
			 __func__);
		return U14ERR_NOERROR;
	}

	if (dwLen == 0) {		/*  allow 0-len read or write; just return success */
		dev_dbg(&pdx->interface->dev,
			"%s: OK; zero-len read/write request\n", __func__);
		return U14ERR_NOERROR;
	}

	if ((pArea->bCircular) &&	/*  Circular transfer? */
	    (pArea->bCircToHost) && (Read)) {	/*  In a supported direction */
				/*  If so, we sort out offset ourself */
		bool bWait = false;	/*  Flag for transfer having to wait */

		dev_dbg(&pdx->interface->dev,
			"Circular buffers are %d at %d and %d at %d\n",
			pArea->aBlocks[0].dwSize, pArea->aBlocks[0].dwOffset,
			pArea->aBlocks[1].dwSize, pArea->aBlocks[1].dwOffset);
		if (pArea->aBlocks[1].dwSize > 0) {	/*  Using the second block already? */
			dwOffs = pArea->aBlocks[1].dwOffset + pArea->aBlocks[1].dwSize;	/*  take offset from that */
			bWait = (dwOffs + dwLen) > pArea->aBlocks[0].dwOffset;	/*  Wait if will overwrite block 0? */
			bWait |= (dwOffs + dwLen) > pArea->dwLength;	/*  or if it overflows the buffer */
		} else {		/*  Area 1 not in use, try to use area 0 */
			if (pArea->aBlocks[0].dwSize == 0)	/*  Reset block 0 if not in use */
				pArea->aBlocks[0].dwOffset = 0;
			dwOffs =
			    pArea->aBlocks[0].dwOffset +
			    pArea->aBlocks[0].dwSize;
			if ((dwOffs + dwLen) > pArea->dwLength) {	/*  Off the end of the buffer? */
				pArea->aBlocks[1].dwOffset = 0;	/*  Set up to use second block */
				dwOffs = 0;
				bWait = (dwOffs + dwLen) > pArea->aBlocks[0].dwOffset;	/*  Wait if will overwrite block 0? */
				bWait |= (dwOffs + dwLen) > pArea->dwLength;	/*  or if it overflows the buffer */
			}
		}

		if (bWait) {	/*  This transfer will have to wait? */
			pdx->bXFerWaiting = true;	/*  Flag we are waiting */
			dev_dbg(&pdx->interface->dev,
				"%s: xfer waiting for circular buffer space\n",
				__func__);
			return U14ERR_NOERROR;
		}

		dev_dbg(&pdx->interface->dev,
			"%s: circular xfer, %d bytes starting at %d\n",
			__func__, dwLen, dwOffs);
	}
	/*  Save the parameters for the read\write transfer */
	pdx->StagedRead = Read;	/*  Save the parameters for this read */
	pdx->StagedId = wIdent;	/*  ID allows us to get transfer area info */
	pdx->StagedOffset = dwOffs;	/*  The area within the transfer area */
	pdx->StagedLength = dwLen;
	pdx->StagedDone = 0;	/*  Initialise the byte count */
	pdx->dwDMAFlag = MODE_LINEAR;	/*  Set DMA mode flag at this point */
	pdx->bXFerWaiting = false;	/*  Clearly not a transfer waiting now */

/*     KeClearEvent(&pdx->StagingDoneEvent);           // Clear the transfer done event */
	ced_stage_chunk(pdx);	/*  fire off the first chunk */

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
static bool ced_read_char(unsigned char *pChar, char *pBuf, unsigned int *pdDone,
		     unsigned int dGot)
{
	bool bRead = false;
	unsigned int dDone = *pdDone;

	if (dDone < dGot) {	/*  If there is more data */
		*pChar = (unsigned char)pBuf[dDone];	/*  Extract the next char */
		dDone++;	/*  Increment the done count */
		*pdDone = dDone;
		bRead = true;	/*  and flag success */
	}

	return bRead;
}

#ifdef NOTUSED
/****************************************************************************
**
** ced_read_word
**
** Reads a word from the 1401, just uses ced_read_char twice; passes on any error
**
*****************************************************************************/
static bool ced_read_word(unsigned short *pWord, char *pBuf, unsigned int *pdDone,
		     unsigned int dGot)
{
	if (ced_read_char((unsigned char *)pWord, pBuf, pdDone, dGot))
		return ced_read_char(((unsigned char *)pWord) + 1, pBuf, pdDone,
				dGot);
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
static bool ced_read_huff(volatile unsigned int *pDWord, char *pBuf,
		     unsigned int *pdDone, unsigned int dGot)
{
	unsigned char ucData;	/* for each read to ced_read_char */
	bool bReturn = true;	/* assume we will succeed */
	unsigned int dwData = 0;	/* Accumulator for the data */

	if (ced_read_char(&ucData, pBuf, pdDone, dGot)) {
		dwData = ucData;	/* copy the data */
		if ((dwData & 0x00000080) != 0) {	/* Bit set for more data ? */
			dwData &= 0x0000007F;	/* Clear the relevant bit */
			if (ced_read_char(&ucData, pBuf, pdDone, dGot)) {
				dwData = (dwData << 8) | ucData;
				if ((dwData & 0x00004000) != 0) {	/* three byte sequence ? */
					dwData &= 0x00003FFF;	/* Clear the relevant bit */
					if (ced_read_char
					    (&ucData, pBuf, pdDone, dGot))
						dwData = (dwData << 8) | ucData;
					else
						bReturn = false;
				}
			} else
				bReturn = false;	/* couldn't read data */
		}
	} else
		bReturn = false;

	*pDWord = dwData;	/* return the data */
	return bReturn;
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
** The pBuf char pointer does not include the initial escape character, so
**  we start handling the data at offset zero.
**
*****************************************************************************/
static bool ced_read_dma_info(volatile DMADESC *pDmaDesc, DEVICE_EXTENSION *pdx,
			char *pBuf, unsigned int dwCount)
{
	bool bResult = false;	/*  assume we won't succeed */
	unsigned char ucData;
	unsigned int dDone = 0;	/*  We haven't parsed anything so far */

	dev_dbg(&pdx->interface->dev, "%s\n", __func__);

	if (ced_read_char(&ucData, pBuf, &dDone, dwCount)) {
		unsigned char ucTransCode = (ucData & 0x0F);	/*  get code for transfer type */
		unsigned short wIdent = ((ucData >> 4) & 0x07);	/*  and area identifier */

		/*  fill in the structure we were given */
		pDmaDesc->wTransType = ucTransCode;	/*  type of transfer */
		pDmaDesc->wIdent = wIdent;	/*  area to use */
		pDmaDesc->dwSize = 0;	/*  initialise other bits */
		pDmaDesc->dwOffset = 0;

		dev_dbg(&pdx->interface->dev, "%s: type: %d ident: %d\n",
			__func__, pDmaDesc->wTransType, pDmaDesc->wIdent);

		pDmaDesc->bOutWard = (ucTransCode != TM_EXTTOHOST);	/*  set transfer direction */

		switch (ucTransCode) {
		case TM_EXTTOHOST:	/*  Extended linear transfer modes (the only ones!) */
		case TM_EXTTO1401:
			{
				bResult =
				    ced_read_huff(&(pDmaDesc->dwOffset), pBuf,
					     &dDone, dwCount)
				    && ced_read_huff(&(pDmaDesc->dwSize), pBuf,
						&dDone, dwCount);
				if (bResult) {
					dev_dbg(&pdx->interface->dev,
						"%s: xfer offset & size %d %d\n",
						__func__, pDmaDesc->dwOffset,
						pDmaDesc->dwSize);

					if ((wIdent >= MAX_TRANSAREAS) ||	/*  Illegal area number, or... */
					    (!pdx->rTransDef[wIdent].bUsed) ||	/*  area not set up, or... */
					    (pDmaDesc->dwOffset > pdx->rTransDef[wIdent].dwLength) ||	/*  range/size */
					    ((pDmaDesc->dwOffset +
					      pDmaDesc->dwSize) >
					     (pdx->rTransDef[wIdent].
					      dwLength))) {
						bResult = false;	/*  bad parameter(s) */
						dev_dbg(&pdx->interface->dev,
							"%s: bad param - id %d, bUsed %d, offset %d, size %d, area length %d\n",
							__func__, wIdent,
							pdx->rTransDef[wIdent].
							bUsed,
							pDmaDesc->dwOffset,
							pDmaDesc->dwSize,
							pdx->rTransDef[wIdent].
							dwLength);
					}
				}
				break;
			}
		default:
			break;
		}
	} else
		bResult = false;

	if (!bResult)		/*  now check parameters for validity */
		dev_err(&pdx->interface->dev, "%s: error reading Esc sequence\n",
			__func__);

	return bResult;
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
** dwCount - the number of characters in the device extension char in buffer,
**           this is known to be at least 2 or we will not be called.
**
****************************************************************************/
static int ced_handle_esc(DEVICE_EXTENSION *pdx, char *pCh,
			 unsigned int dwCount)
{
	int iReturn = U14ERR_FAIL;

	/*  I have no idea what this next test is about. '?' is 0x3f, which is area 3, code */
	/*  15. At the moment, this is not used, so it does no harm, but unless someone can */
	/*  tell me what this is for, it should be removed from this and the Windows driver. */
	if (pCh[0] == '?') {	/*  Is this an information response */
				/*  Parse and save the information */
	} else {
		spin_lock(&pdx->stagedLock);	/*  Lock others out */

		if (ced_read_dma_info(&pdx->rDMAInfo, pdx, pCh, dwCount)) {	/*  Get DMA parameters */
			unsigned short wTransType = pdx->rDMAInfo.wTransType;	/*  check transfer type */

			dev_dbg(&pdx->interface->dev,
				"%s: xfer to %s, offset %d, length %d\n",
				__func__,
				pdx->rDMAInfo.bOutWard ? "1401" : "host",
				pdx->rDMAInfo.dwOffset, pdx->rDMAInfo.dwSize);

			if (pdx->bXFerWaiting) { /*  Check here for badly out of kilter... */
				/*  This can never happen, really */
				dev_err(&pdx->interface->dev,
					"ERROR: DMA setup while transfer still waiting\n");
			} else {
				if ((wTransType == TM_EXTTOHOST)
				    || (wTransType == TM_EXTTO1401)) {
					iReturn =
					    ced_read_write_mem(pdx,
							 !pdx->rDMAInfo.
							 bOutWard,
							 pdx->rDMAInfo.wIdent,
							 pdx->rDMAInfo.dwOffset,
							 pdx->rDMAInfo.dwSize);
					if (iReturn != U14ERR_NOERROR)
						dev_err(&pdx->interface->dev,
							"%s: ced_read_write_mem() failed %d\n",
							__func__, iReturn);
				} else	/*  This covers non-linear transfer setup */
					dev_err(&pdx->interface->dev,
						"%s: Unknown block xfer type %d\n",
						__func__, wTransType);
			}
		} else		/*  Failed to read parameters */
			dev_err(&pdx->interface->dev, "%s: ced_read_dma_info() fail\n",
				__func__);

		spin_unlock(&pdx->stagedLock);	/*  OK here */
	}

	dev_dbg(&pdx->interface->dev, "%s: returns %d\n", __func__, iReturn);

	return iReturn;
}

/****************************************************************************
** Callback for the character read complete or error
****************************************************************************/
static void ced_readchar_callback(struct urb *pUrb)
{
	DEVICE_EXTENSION *pdx = pUrb->context;
	int nGot = pUrb->actual_length;	/*  what we transferred */

	if (pUrb->status) {	/*  Do we have a problem to handle? */
		int nPipe = pdx->nPipes == 4 ? 1 : 0;	/*  The pipe number to use for error */
		/*  sync/async unlink faults aren't errors... just saying device removed or stopped */
		if (!
		    (pUrb->status == -ENOENT || pUrb->status == -ECONNRESET
		     || pUrb->status == -ESHUTDOWN)) {
			dev_err(&pdx->interface->dev,
				"%s: nonzero write bulk status received: %d\n",
				__func__, pUrb->status);
		} else
			dev_dbg(&pdx->interface->dev,
				"%s: 0 chars pUrb->status=%d (shutdown?)\n",
				__func__, pUrb->status);

		spin_lock(&pdx->err_lock);
		pdx->errors = pUrb->status;
		spin_unlock(&pdx->err_lock);
		nGot = 0;	/*   and tidy up again if so */

		spin_lock(&pdx->charInLock);	/*  already at irq level */
		pdx->bPipeError[nPipe] = 1;	/*  Flag an error for later */
	} else {
		if ((nGot > 1) && ((pdx->pCoherCharIn[0] & 0x7f) == 0x1b)) {	/*  Esc sequence? */
			ced_handle_esc(pdx, &pdx->pCoherCharIn[1], nGot - 1);	/*  handle it */
			spin_lock(&pdx->charInLock);	/*  already at irq level */
		} else {
			spin_lock(&pdx->charInLock);	/*  already at irq level */
			if (nGot > 0) {
				unsigned int i;
				if (nGot < INBUF_SZ) {
					pdx->pCoherCharIn[nGot] = 0;	/*  tidy the string */
					dev_dbg(&pdx->interface->dev,
						"%s: got %d chars >%s<\n",
						__func__, nGot,
						pdx->pCoherCharIn);
				}
				/*  We know that whatever we read must fit in the input buffer */
				for (i = 0; i < nGot; i++) {
					pdx->inputBuffer[pdx->dwInBuffPut++] =
					    pdx->pCoherCharIn[i] & 0x7F;
					if (pdx->dwInBuffPut >= INBUF_SZ)
						pdx->dwInBuffPut = 0;
				}

				if ((pdx->dwNumInput + nGot) <= INBUF_SZ)
					pdx->dwNumInput += nGot;	/*  Adjust the buffer count accordingly */
			} else
				dev_dbg(&pdx->interface->dev, "%s: read ZLP\n",
					__func__);
		}
	}

	pdx->bReadCharsPending = false;	/*  No longer have a pending read */
	spin_unlock(&pdx->charInLock);	/*  already at irq level */

	ced_allowi(pdx);	/*  see if we can do the next one */
}

/****************************************************************************
** ced_allowi
**
** This is used to make sure that there is always a pending input transfer so
** we can pick up any inward transfers. This can be called in multiple contexts
** so we use the irqsave version of the spinlock.
****************************************************************************/
int ced_allowi(DEVICE_EXTENSION *pdx)
{
	int iReturn = U14ERR_NOERROR;
	unsigned long flags;
	spin_lock_irqsave(&pdx->charInLock, flags);	/*  can be called in multiple contexts */

	/*  We don't want char input running while DMA is in progress as we know that this */
	/*   can cause sequencing problems for the 2270. So don't. It will also allow the */
	/*   ERR response to get back to the host code too early on some PCs, even if there */
	/*   is no actual driver failure, so we don't allow this at all. */
	if (!pdx->bInDrawDown &&	/*  stop input if */
	    !pdx->bReadCharsPending &&	/*  If no read request outstanding */
	    (pdx->dwNumInput < (INBUF_SZ / 2)) &&	/*   and there is some space */
	    (pdx->dwDMAFlag == MODE_CHAR) &&	/*   not doing any DMA */
	    (!pdx->bXFerWaiting) &&	/*   no xfer waiting to start */
	    (can_accept_io_requests(pdx)))	{ /*   and activity is generally OK */
				/*   then off we go */
		unsigned int nMax = INBUF_SZ - pdx->dwNumInput;	/*  max we could read */
		int nPipe = pdx->nPipes == 4 ? 1 : 0;	/*  The pipe number to use */

		dev_dbg(&pdx->interface->dev, "%s: %d chars in input buffer\n",
			__func__, pdx->dwNumInput);

		usb_fill_int_urb(pdx->pUrbCharIn, pdx->udev,
				 usb_rcvintpipe(pdx->udev, pdx->epAddr[nPipe]),
				 pdx->pCoherCharIn, nMax, ced_readchar_callback,
				 pdx, pdx->bInterval);
		pdx->pUrbCharIn->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;	/*  short xfers are OK by default */
		usb_anchor_urb(pdx->pUrbCharIn, &pdx->submitted);	/*  in case we need to kill it */
		iReturn = usb_submit_urb(pdx->pUrbCharIn, GFP_ATOMIC);
		if (iReturn) {
			usb_unanchor_urb(pdx->pUrbCharIn);	/*  remove from list of active Urbs */
			pdx->bPipeError[nPipe] = 1;	/*  Flag an error to be handled later */
			dev_err(&pdx->interface->dev,
				"%s: submit urb failed: %d\n",
				__func__, iReturn);
		} else
			pdx->bReadCharsPending = true;	/*  Flag that we are active here */
	}

	spin_unlock_irqrestore(&pdx->charInLock, flags);

	return iReturn;

}

/*****************************************************************************
** The ioctl entry point to the driver that is used by us to talk to it.
** inode    The device node (no longer in 3.0.0 kernels)
** file     The file that is open, which holds our pdx pointer
** ulArg    The argument passed in. Note that long is 64-bits in 64-bit system, i.e. it is big
**          enough for a 64-bit pointer.
*****************************************************************************/
static long ced_ioctl(struct file *file, unsigned int cmd, unsigned long ulArg)
{
	int err = 0;
	DEVICE_EXTENSION *pdx = file->private_data;
	if (!can_accept_io_requests(pdx))	/*  check we still exist */
		return -ENODEV;

	/*  Check that access is allowed, where is is needed. Anything that would have an indeterminate */
	/*  size will be checked by the specific command. */
	if (_IOC_DIR(cmd) & _IOC_READ)	/*  read from point of view of user... */
		err = !access_ok(VERIFY_WRITE, (void __user *)ulArg, _IOC_SIZE(cmd));	/*  is kernel write */
	else if (_IOC_DIR(cmd) & _IOC_WRITE)	/*  and write from point of view of user... */
		err = !access_ok(VERIFY_READ, (void __user *)ulArg, _IOC_SIZE(cmd));	/*  is kernel read */
	if (err)
		return -EFAULT;

	switch (_IOC_NR(cmd)) {
	case _IOC_NR(IOCTL_CED_SENDSTRING(0)):
		return ced_send_string(pdx, (const char __user *)ulArg,
				  _IOC_SIZE(cmd));

	case _IOC_NR(IOCTL_CED_RESET1401):
		return ced_reset(pdx);

	case _IOC_NR(IOCTL_CED_GETCHAR):
		return ced_get_char(pdx);

	case _IOC_NR(IOCTL_CED_SENDCHAR):
		return ced_send_char(pdx, (char)ulArg);

	case _IOC_NR(IOCTL_CED_STAT1401):
		return ced_stat_1401(pdx);

	case _IOC_NR(IOCTL_CED_LINECOUNT):
		return ced_line_count(pdx);

	case _IOC_NR(IOCTL_CED_GETSTRING(0)):
		return ced_get_string(pdx, (char __user *)ulArg, _IOC_SIZE(cmd));

	case _IOC_NR(IOCTL_CED_SETTRANSFER):
		return ced_set_transfer(pdx, (struct transfer_area_desc __user *) ulArg);

	case _IOC_NR(IOCTL_CED_UNSETTRANSFER):
		return ced_unset_transfer(pdx, (int)ulArg);

	case _IOC_NR(IOCTL_CED_SETEVENT):
		return ced_set_event(pdx, (struct transfer_event __user *) ulArg);

	case _IOC_NR(IOCTL_CED_GETOUTBUFSPACE):
		return ced_get_out_buf_space(pdx);

	case _IOC_NR(IOCTL_CED_GETBASEADDRESS):
		return -1;

	case _IOC_NR(IOCTL_CED_GETDRIVERREVISION):
		return (2 << 24) | (DRIVERMAJREV << 16) | DRIVERMINREV;	/*  USB | MAJOR | MINOR */

	case _IOC_NR(IOCTL_CED_GETTRANSFER):
		return GetTransfer(pdx, (TGET_TX_BLOCK __user *) ulArg);

	case _IOC_NR(IOCTL_CED_KILLIO1401):
		return KillIO1401(pdx);

	case _IOC_NR(IOCTL_CED_STATEOF1401):
		return StateOf1401(pdx);

	case _IOC_NR(IOCTL_CED_GRAB1401):
	case _IOC_NR(IOCTL_CED_FREE1401):
		return U14ERR_NOERROR;

	case _IOC_NR(IOCTL_CED_STARTSELFTEST):
		return StartSelfTest(pdx);

	case _IOC_NR(IOCTL_CED_CHECKSELFTEST):
		return CheckSelfTest(pdx, (TGET_SELFTEST __user *) ulArg);

	case _IOC_NR(IOCTL_CED_TYPEOF1401):
		return TypeOf1401(pdx);

	case _IOC_NR(IOCTL_CED_TRANSFERFLAGS):
		return TransferFlags(pdx);

	case _IOC_NR(IOCTL_CED_DBGPEEK):
		return DbgPeek(pdx, (TDBGBLOCK __user *) ulArg);

	case _IOC_NR(IOCTL_CED_DBGPOKE):
		return DbgPoke(pdx, (TDBGBLOCK __user *) ulArg);

	case _IOC_NR(IOCTL_CED_DBGRAMPDATA):
		return DbgRampData(pdx, (TDBGBLOCK __user *) ulArg);

	case _IOC_NR(IOCTL_CED_DBGRAMPADDR):
		return DbgRampAddr(pdx, (TDBGBLOCK __user *) ulArg);

	case _IOC_NR(IOCTL_CED_DBGGETDATA):
		return DbgGetData(pdx, (TDBGBLOCK __user *) ulArg);

	case _IOC_NR(IOCTL_CED_DBGSTOPLOOP):
		return DbgStopLoop(pdx);

	case _IOC_NR(IOCTL_CED_FULLRESET):
		pdx->bForceReset = true;	/*  Set a flag for a full reset */
		break;

	case _IOC_NR(IOCTL_CED_SETCIRCULAR):
		return SetCircular(pdx, (struct transfer_area_desc __user *) ulArg);

	case _IOC_NR(IOCTL_CED_GETCIRCBLOCK):
		return GetCircBlock(pdx, (TCIRCBLOCK __user *) ulArg);

	case _IOC_NR(IOCTL_CED_FREECIRCBLOCK):
		return FreeCircBlock(pdx, (TCIRCBLOCK __user *) ulArg);

	case _IOC_NR(IOCTL_CED_WAITEVENT):
		return WaitEvent(pdx, (int)(ulArg & 0xff), (int)(ulArg >> 8));

	case _IOC_NR(IOCTL_CED_TESTEVENT):
		return TestEvent(pdx, (int)ulArg);

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
/*  initialise our DEVICE_EXTENSION. */
static int ced_probe(struct usb_interface *interface,
		     const struct usb_device_id *id)
{
	DEVICE_EXTENSION *pdx;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int i, bcdDevice;
	int retval = -ENOMEM;

	/*  allocate memory for our device extension and initialize it */
	pdx = kzalloc(sizeof(*pdx), GFP_KERNEL);
	if (!pdx)
		goto error;

	for (i = 0; i < MAX_TRANSAREAS; ++i) {	/*  Initialise the wait queues */
		init_waitqueue_head(&pdx->rTransDef[i].wqEvent);
	}

	/*  Put initialises for our stuff here. Note that all of *pdx is zero, so */
	/*  no need to explicitly zero it. */
	spin_lock_init(&pdx->charOutLock);
	spin_lock_init(&pdx->charInLock);
	spin_lock_init(&pdx->stagedLock);

	/*  Initialises from the skeleton stuff */
	kref_init(&pdx->kref);
	mutex_init(&pdx->io_mutex);
	spin_lock_init(&pdx->err_lock);
	init_usb_anchor(&pdx->submitted);

	pdx->udev = usb_get_dev(interface_to_usbdev(interface));
	pdx->interface = interface;

	/*  Attempt to identify the device */
	bcdDevice = pdx->udev->descriptor.bcdDevice;
	i = (bcdDevice >> 8);
	if (i == 0)
		pdx->s1401Type = TYPEU1401;
	else if ((i >= 1) && (i <= 23))
		pdx->s1401Type = i + 2;
	else {
		dev_err(&interface->dev, "%s: Unknown device. bcdDevice = %d\n",
			__func__, bcdDevice);
		goto error;
	}
	/*  set up the endpoint information. We only care about the number of EP as */
	/*  we know that we are dealing with a 1401 device. */
	iface_desc = interface->cur_altsetting;
	pdx->nPipes = iface_desc->desc.bNumEndpoints;
	dev_info(&interface->dev, "1401Type=%d with %d End Points\n",
		 pdx->s1401Type, pdx->nPipes);
	if ((pdx->nPipes < 3) || (pdx->nPipes > 4))
		goto error;

	/*  Allocate the URBs we hold for performing transfers */
	pdx->pUrbCharOut = usb_alloc_urb(0, GFP_KERNEL);	/*  character output URB */
	pdx->pUrbCharIn = usb_alloc_urb(0, GFP_KERNEL);	/*  character input URB */
	pdx->pStagedUrb = usb_alloc_urb(0, GFP_KERNEL);	/*  block transfer URB */
	if (!pdx->pUrbCharOut || !pdx->pUrbCharIn || !pdx->pStagedUrb) {
		dev_err(&interface->dev, "%s: URB alloc failed\n", __func__);
		goto error;
	}

	pdx->pCoherStagedIO =
	    usb_alloc_coherent(pdx->udev, STAGED_SZ, GFP_KERNEL,
			       &pdx->pStagedUrb->transfer_dma);
	pdx->pCoherCharOut =
	    usb_alloc_coherent(pdx->udev, OUTBUF_SZ, GFP_KERNEL,
			       &pdx->pUrbCharOut->transfer_dma);
	pdx->pCoherCharIn =
	    usb_alloc_coherent(pdx->udev, INBUF_SZ, GFP_KERNEL,
			       &pdx->pUrbCharIn->transfer_dma);
	if (!pdx->pCoherCharOut || !pdx->pCoherCharIn || !pdx->pCoherStagedIO) {
		dev_err(&interface->dev, "%s: Coherent buffer alloc failed\n",
			__func__);
		goto error;
	}

	for (i = 0; i < pdx->nPipes; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		pdx->epAddr[i] = endpoint->bEndpointAddress;
		dev_info(&interface->dev, "Pipe %d, ep address %02x\n",
			 i, pdx->epAddr[i]);
		if (((pdx->nPipes == 3) && (i == 0)) ||	/*  if char input end point */
		    ((pdx->nPipes == 4) && (i == 1))) {
			pdx->bInterval = endpoint->bInterval;	/*  save the endpoint interrupt interval */
			dev_info(&interface->dev, "Pipe %d, bInterval = %d\n",
				 i, pdx->bInterval);
		}
		/*  Detect USB2 by checking last ep size (64 if USB1) */
		if (i == pdx->nPipes - 1) {	/*  if this is the last ep (bulk) */
			pdx->bIsUSB2 =
			    le16_to_cpu(endpoint->wMaxPacketSize) > 64;
			dev_info(&pdx->interface->dev, "USB%d\n",
				 pdx->bIsUSB2 + 1);
		}
	}

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, pdx);

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
	if (pdx)
		kref_put(&pdx->kref, ced_delete);	/*  frees allocated memory */
	return retval;
}

static void ced_disconnect(struct usb_interface *interface)
{
	DEVICE_EXTENSION *pdx = usb_get_intfdata(interface);
	int minor = interface->minor;
	int i;

	usb_set_intfdata(interface, NULL);	/*  remove the pdx from the interface */
	usb_deregister_dev(interface, &ced_class);	/*  give back our minor device number */

	mutex_lock(&pdx->io_mutex);	/*  stop more I/O starting while... */
	ced_draw_down(pdx);	/*  ...wait for then kill any io */
	for (i = 0; i < MAX_TRANSAREAS; ++i) {
		int iErr = ced_clear_area(pdx, i);	/*  ...release any used memory */
		if (iErr == U14ERR_UNLOCKFAIL)
			dev_err(&pdx->interface->dev, "%s: Area %d was in used\n",
				__func__, i);
	}
	pdx->interface = NULL;	/*  ...we kill off link to interface */
	mutex_unlock(&pdx->io_mutex);

	usb_kill_anchored_urbs(&pdx->submitted);

	kref_put(&pdx->kref, ced_delete);	/*  decrement our usage count */

	dev_info(&interface->dev, "USB cedusb #%d now disconnected\n", minor);
}

/*  Wait for all the urbs we know of to be done with, then kill off any that */
/*  are left. NBNB we will need to have a mechanism to stop circular xfers */
/*  from trying to fire off more urbs. We will wait up to 3 seconds for Urbs */
/*  to be done. */
void ced_draw_down(DEVICE_EXTENSION *pdx)
{
	int time;
	dev_dbg(&pdx->interface->dev, "%s: called\n", __func__);

	pdx->bInDrawDown = true;
	time = usb_wait_anchor_empty_timeout(&pdx->submitted, 3000);
	if (!time) {		/*  if we timed out we kill the urbs */
		usb_kill_anchored_urbs(&pdx->submitted);
		dev_err(&pdx->interface->dev, "%s: timed out\n", __func__);
	}
	pdx->bInDrawDown = false;
}

static int ced_suspend(struct usb_interface *intf, pm_message_t message)
{
	DEVICE_EXTENSION *pdx = usb_get_intfdata(intf);
	if (!pdx)
		return 0;
	ced_draw_down(pdx);

	dev_dbg(&pdx->interface->dev, "%s: called\n", __func__);
	return 0;
}

static int ced_resume(struct usb_interface *intf)
{
	DEVICE_EXTENSION *pdx = usb_get_intfdata(intf);
	if (!pdx)
		return 0;
	dev_dbg(&pdx->interface->dev, "%s: called\n", __func__);
	return 0;
}

static int ced_pre_reset(struct usb_interface *intf)
{
	DEVICE_EXTENSION *pdx = usb_get_intfdata(intf);
	dev_dbg(&pdx->interface->dev, "%s\n", __func__);
	mutex_lock(&pdx->io_mutex);
	ced_draw_down(pdx);
	return 0;
}

static int ced_post_reset(struct usb_interface *intf)
{
	DEVICE_EXTENSION *pdx = usb_get_intfdata(intf);
	dev_dbg(&pdx->interface->dev, "%s\n", __func__);

	/* we are sure no URBs are active - no locking needed */
	pdx->errors = -EPIPE;
	mutex_unlock(&pdx->io_mutex);

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
