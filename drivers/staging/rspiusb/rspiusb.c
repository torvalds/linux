/*
 * rspiusb.c
 *
 * Copyright (C) 2005, 2006 Princeton Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/vmalloc.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>
#include <linux/scatterlist.h>
#include <linux/usb.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/ioctl.h>
#include "rspiusb.h"

#ifdef CONFIG_USB_DEBUG
static int debug = 1;
#else
static int debug;
#endif
/* Use our own dbg macro */
#undef dbg
#define dbg(format, arg...) \
	do { \
		if (debug) \
			printk(KERN_DEBUG __FILE__ ": " format "\n" , ##arg); \
	} while (0)

/* Version Information */
#define DRIVER_VERSION "V1.0.1"
#define DRIVER_AUTHOR  "Princeton Instruments"
#define DRIVER_DESC    "PI USB2.0 Device Driver for Linux"

/* Define these values to match your devices */
#define VENDOR_ID   0x0BD7
#define ST133_PID   0xA010
#define PIXIS_PID   0xA026

/* Get a minor range for your devices from the usb maintainer */
#ifdef CONFIG_USB_DYNAMIC_MINORS
#define PIUSB_MINOR_BASE    0
#else
#define PIUSB_MINOR_BASE    192
#endif

/* prevent races between open() and disconnect() */
static DECLARE_MUTEX(disconnect_sem);

/* Structure to hold all of our device specific stuff */
struct device_extension {
	struct usb_device *udev;	 /* save off the usb device pointer */
	struct usb_interface *interface; /* the interface for this device */
	unsigned char minor;		 /* the starting minor number
					  * for this device
					  */
	size_t bulk_in_size_returned;
	int bulk_in_byte_trk;
	struct urb ***PixelUrb;
	int frameIdx;
	int urbIdx;
	unsigned int *maplist_numPagesMapped;
	int open;		  /* if the port is open or not */
	int present;		  /* if the device is not disconnected */
	int userBufMapped;	  /* has the user buffer been mapped ? */
	struct scatterlist **sgl; /* scatter-gather list for user buffer */
	unsigned int *sgEntries;
	struct kref kref;
	int gotPixelData;
	int pendingWrite;
	char **pendedPixelUrbs;
	int iama;		 /* PIXIS or ST133 */
	int num_frames;		 /* the number of frames that will fit
				  * in the user buffer
				  */
	int active_frame;
	unsigned long frameSize;
	struct semaphore sem;
	unsigned int hEP[8];	 /* FX2 specific endpoints */
};

#define to_pi_dev(d) container_of(d, struct device_extension, kref)

/* Prototypes */
static int MapUserBuffer(struct ioctl_struct *, struct device_extension *);
static int UnMapUserBuffer(struct device_extension *);
static int piusb_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		       unsigned long arg);
static int piusb_output(struct ioctl_struct *, unsigned char *, int,
		struct device_extension *);
static struct usb_driver piusb_driver;

/* table of devices that work with this driver */
static struct usb_device_id pi_device_table[] = {
	{USB_DEVICE(VENDOR_ID, ST133_PID)},
	{USB_DEVICE(VENDOR_ID, PIXIS_PID)},
	{0, } /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, pi_device_table);

static int lastErr;
static int errCnt;

static void piusb_delete(struct kref *kref)
{
	struct device_extension *pdx = to_pi_dev(kref);

	dev_dbg(&pdx->udev->dev, "%s\n", __func__);
	usb_put_dev(pdx->udev);
	kfree(pdx);
}

static int piusb_open(struct inode *inode, struct file *file)
{
	struct device_extension *pdx = NULL;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	dbg("Piusb_Open()");
	subminor = iminor(inode);
	interface = usb_find_interface(&piusb_driver, subminor);
	if (!interface) {
		printk(KERN_ERR "%s - error, can't find device for minor %d\n",
		       __func__, subminor);
		retval = -ENODEV;
		goto exit_no_device;
	}

	pdx = usb_get_intfdata(interface);
	if (!pdx) {
		retval = -ENODEV;
		goto exit_no_device;
	}
	dbg("Alternate Setting = %d", interface->num_altsetting);

	pdx->bulk_in_size_returned = 0;
	pdx->bulk_in_byte_trk = 0;
	pdx->PixelUrb = NULL;
	pdx->frameIdx = 0;
	pdx->urbIdx = 0;
	pdx->maplist_numPagesMapped = NULL;
	pdx->userBufMapped = 0;
	pdx->sgl = NULL;
	pdx->sgEntries = NULL;
	pdx->gotPixelData = 0;
	pdx->pendingWrite = 0;
	pdx->pendedPixelUrbs = NULL;
	pdx->num_frames = 0;
	pdx->active_frame = 0;
	pdx->frameSize = 0;

	/* increment our usage count for the device */
	kref_get(&pdx->kref);

	/* save our object in the file's private structure */
	file->private_data = pdx;

exit_no_device:
	return retval;
}

static int piusb_release(struct inode *inode, struct file *file)
{
	struct device_extension *pdx;
	int retval = 0;

	dbg("Piusb_Release()");
	pdx = (struct device_extension *)file->private_data;
	if (pdx == NULL) {
		dbg("%s - object is NULL", __func__);
		retval = -ENODEV;
		goto object_null;
	}
	/* decrement the count on our device */
	kref_put(&pdx->kref, piusb_delete);

object_null:
	return retval;
}

static int pixis_io(struct ioctl_struct *ctrl, struct device_extension *pdx,
		struct ioctl_struct *arg)
{
	unsigned int numToRead = 0;
	unsigned int totalRead = 0;
	unsigned char *uBuf;
	int numbytes;
	int i;

	uBuf = kmalloc(ctrl->numbytes, GFP_KERNEL);
	if (!uBuf) {
		dbg("Alloc for uBuf failed");
		return 0;
	}
	numbytes = (int) ctrl->numbytes;
	numToRead = (unsigned int) ctrl->numbytes;
	dbg("numbytes to read = %d", numbytes);
	dbg("endpoint # %d", ctrl->endpoint);

	if (copy_from_user(uBuf, ctrl->pData, numbytes))
		dbg("copying ctrl->pData to dummyBuf failed");

	do {
		i = usb_bulk_msg(pdx->udev, pdx->hEP[ctrl->endpoint],
				(uBuf + totalRead),
				/* EP0 can only handle 64 bytes at a time */
				(numToRead > 64) ? 64 : numToRead,
				&numbytes, HZ * 10);
		if (i) {
			dbg("CMD = %s, Address = 0x%02X",
					((uBuf[3] == 0x02) ? "WRITE" : "READ"),
					uBuf[1]);
			dbg("Number of bytes Attempted to read = %d",
					(int)ctrl->numbytes);
			dbg("Blocking ReadI/O Failed with status %d", i);
			kfree(uBuf);
			return -1;
		}
		dbg("Pixis EP0 Read %d bytes", numbytes);
		totalRead += numbytes;
		numToRead -= numbytes;
	} while (numToRead);

	memcpy(ctrl->pData, uBuf, totalRead);
	dbg("Total Bytes Read from PIXIS EP0 = %d", totalRead);
	ctrl->numbytes = totalRead;

	if (copy_to_user(arg, ctrl, sizeof(struct ioctl_struct)))
		dbg("copy_to_user failed in IORB");

	kfree(uBuf);
	return ctrl->numbytes;
}

static int pixel_data(struct ioctl_struct *ctrl, struct device_extension *pdx)
{
	int i;

	if (!pdx->gotPixelData)
		return 0;

	pdx->gotPixelData = 0;
	ctrl->numbytes = pdx->bulk_in_size_returned;
	pdx->bulk_in_size_returned -= pdx->frameSize;

	for (i = 0; i < pdx->maplist_numPagesMapped[pdx->active_frame]; i++)
		SetPageDirty(sg_page(&pdx->sgl[pdx->active_frame][i]));

	pdx->active_frame = ((pdx->active_frame + 1) % pdx->num_frames);

	return ctrl->numbytes;
}

/**
 *	piusb_ioctl
 */
static int piusb_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		       unsigned long arg)
{
	struct device_extension *pdx;
	char dummyCtlBuf[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	unsigned long devRB = 0;
	int err = 0;
	int retval = 0;
	struct ioctl_struct ctrl;
	unsigned short controlData = 0;

	pdx = (struct device_extension *)file->private_data;
	/* verify that the device wasn't unplugged */
	if (!pdx->present) {
		dbg("No Device Present\n");
		return -ENODEV;
	}
	/* fill in your device specific stuff here */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg,
				_IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ, (void __user *)arg,
			       _IOC_SIZE(cmd));
	if (err) {
		dev_err(&pdx->udev->dev, "return with error = %d\n", err);
		return -EFAULT;
	}
	switch (cmd) {
	case PIUSB_GETVNDCMD:
		if (copy_from_user
		    (&ctrl, (void __user *)arg, sizeof(struct ioctl_struct)))
			dev_err(&pdx->udev->dev, "copy_from_user failed\n");
		dbg("%s %x\n", "Get Vendor Command = ", ctrl.cmd);
		retval =
		    usb_control_msg(pdx->udev, usb_rcvctrlpipe(pdx->udev, 0),
				    ctrl.cmd, USB_DIR_IN, 0, 0, &devRB,
				    ctrl.numbytes, HZ * 10);
		if (ctrl.cmd == 0xF1) {
			dbg("FW Version returned from HW = %ld.%ld",
			    (devRB >> 8), (devRB & 0xFF));
		}
		if (retval >= 0)
			retval = (int)devRB;
		return retval;

	case PIUSB_SETVNDCMD:
		if (copy_from_user
		    (&ctrl, (void __user *)arg, sizeof(struct ioctl_struct)))
			dev_err(&pdx->udev->dev, "copy_from_user failed\n");
		/* dbg( "%s %x", "Set Vendor Command = ",ctrl.cmd ); */
		controlData = ctrl.pData[0];
		controlData |= (ctrl.pData[1] << 8);
		/* dbg( "%s %d", "Vendor Data =",controlData ); */
		retval = usb_control_msg(pdx->udev,
				usb_sndctrlpipe(pdx->udev, 0),
				ctrl.cmd,
				(USB_DIR_OUT | USB_TYPE_VENDOR
				 /* | USB_RECIP_ENDPOINT */),
				controlData, 0,
				&dummyCtlBuf, ctrl.numbytes, HZ * 10);
		return retval;

	case PIUSB_ISHIGHSPEED:
		return ((pdx->udev->speed == USB_SPEED_HIGH) ? 1 : 0);

	case PIUSB_WRITEPIPE:
		if (copy_from_user(&ctrl, (void __user *)arg, _IOC_SIZE(cmd)))
			dev_err(&pdx->udev->dev,
					"copy_from_user WRITE_DUMMY failed\n");
		if (!access_ok(VERIFY_READ, ctrl.pData, ctrl.numbytes)) {
			dbg("can't access pData");
			return 0;
		}
		piusb_output(&ctrl, ctrl.pData /* uBuf */, ctrl.numbytes, pdx);
		return ctrl.numbytes;

	case PIUSB_USERBUFFER:
		if (copy_from_user
		    (&ctrl, (void __user *)arg, sizeof(struct ioctl_struct)))
			dev_err(&pdx->udev->dev, "copy_from_user failed\n");
		return MapUserBuffer((struct ioctl_struct *) &ctrl, pdx);

	case PIUSB_UNMAP_USERBUFFER:
		retval = UnMapUserBuffer(pdx);
		return retval;

	case PIUSB_READPIPE:
		if (copy_from_user(&ctrl, (void __user *)arg,
					sizeof(struct ioctl_struct)))
			dev_err(&pdx->udev->dev, "copy_from_user failed\n");

		if (((0 == ctrl.endpoint) && (PIXIS_PID == pdx->iama)) ||
				(1 == ctrl.endpoint) ||	/* ST133IO */
				(4 == ctrl.endpoint))	/* PIXIS IO */
			return pixis_io(&ctrl, pdx,
					(struct ioctl_struct *)arg);
		else if ((0 == ctrl.endpoint) || /* ST133 Pixel Data */
				(2 == ctrl.endpoint) || /* PIXIS Ping */
				(3 == ctrl.endpoint))	/* PIXIS Pong */
			return pixel_data(&ctrl, pdx);

		break;

	case PIUSB_WHATCAMERA:
		return pdx->iama;

	case PIUSB_SETFRAMESIZE:
		dbg("PIUSB_SETFRAMESIZE");
		if (copy_from_user
		    (&ctrl, (void __user *)arg, sizeof(struct ioctl_struct)))
			dev_err(&pdx->udev->dev, "copy_from_user failed\n");
		pdx->frameSize = ctrl.numbytes;
		pdx->num_frames = ctrl.numFrames;
		if (!pdx->sgl)
			pdx->sgl =
			    kmalloc(sizeof(struct scatterlist *) *
				    pdx->num_frames, GFP_KERNEL);
		if (!pdx->sgEntries)
			pdx->sgEntries =
			    kmalloc(sizeof(unsigned int) * pdx->num_frames,
				    GFP_KERNEL);
		if (!pdx->PixelUrb)
			pdx->PixelUrb =
			    kmalloc(sizeof(struct urb **) * pdx->num_frames,
				    GFP_KERNEL);
		if (!pdx->maplist_numPagesMapped)
			pdx->maplist_numPagesMapped =
			    vmalloc(sizeof(unsigned int) * pdx->num_frames);
		if (!pdx->pendedPixelUrbs)
			pdx->pendedPixelUrbs =
			    kmalloc(sizeof(char *) * pdx->num_frames,
				    GFP_KERNEL);
		return 0;

	default:
		dbg("%s\n", "No IOCTL found");
		break;

	}
	/* return that we did not understand this ioctl call */
	dbg("Returning -ENOTTY");
	return -ENOTTY;
}

static void piusb_write_bulk_callback(struct urb *urb)
{
	struct device_extension *pdx = urb->context;
	int status = urb->status;

	/* sync/async unlink faults aren't errors */
	if (status && !(status == -ENOENT || status == -ECONNRESET))
		dev_dbg(&urb->dev->dev,
			"%s - nonzero write bulk status received: %d",
			__func__, status);

	pdx->pendingWrite = 0;
	usb_buffer_free(urb->dev, urb->transfer_buffer_length,
			urb->transfer_buffer, urb->transfer_dma);
}

int piusb_output(struct ioctl_struct *io, unsigned char *uBuf, int len,
		 struct device_extension *pdx)
{
	struct urb *urb = NULL;
	int err = 0;
	unsigned char *kbuf = NULL;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (urb != NULL) {
		kbuf =
		    usb_buffer_alloc(pdx->udev, len, GFP_KERNEL,
				     &urb->transfer_dma);
		if (!kbuf) {
			dev_err(&pdx->udev->dev, "buffer_alloc failed\n");
			return -ENOMEM;
		}
		memcpy(kbuf, uBuf, len);
		usb_fill_bulk_urb(urb, pdx->udev, pdx->hEP[io->endpoint], kbuf,
				  len, piusb_write_bulk_callback, pdx);
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		err = usb_submit_urb(urb, GFP_KERNEL);
		if (err) {
			dev_err(&pdx->udev->dev,
				"WRITE ERROR:submit urb error = %d\n", err);
		}
		pdx->pendingWrite = 1;
		usb_free_urb(urb);
	}
	return -EINPROGRESS;
}

static int UnMapUserBuffer(struct device_extension *pdx)
{
	int i = 0;
	int k = 0;
	unsigned int epAddr;

	for (k = 0; k < pdx->num_frames; k++) {
		dbg("Killing Urbs for Frame %d", k);
		for (i = 0; i < pdx->sgEntries[k]; i++) {
			usb_kill_urb(pdx->PixelUrb[k][i]);
			usb_free_urb(pdx->PixelUrb[k][i]);
			pdx->pendedPixelUrbs[k][i] = 0;
		}
		dbg("Urb error count = %d", errCnt);
		errCnt = 0;
		dbg("Urbs free'd and Killed for Frame %d", k);
	}

	for (k = 0; k < pdx->num_frames; k++) {
		if (pdx->iama == PIXIS_PID)
			/* which EP should we map this frame to ? */
			/* PONG, odd frames: hEP[3] */
			/* PING, even frames and zero hEP[2] */
			epAddr = (k % 2) ? pdx->hEP[3] : pdx->hEP[2];
		else
			/* ST133 only has 1 endpoint for Pixel data transfer */
			epAddr = pdx->hEP[0];

		usb_buffer_unmap_sg(pdx->udev, epAddr, pdx->sgl[k],
				    pdx->maplist_numPagesMapped[k]);
		for (i = 0; i < pdx->maplist_numPagesMapped[k]; i++)
			page_cache_release(sg_page(&pdx->sgl[k][i]));
		kfree(pdx->sgl[k]);
		kfree(pdx->PixelUrb[k]);
		kfree(pdx->pendedPixelUrbs[k]);
		pdx->sgl[k] = NULL;
		pdx->PixelUrb[k] = NULL;
		pdx->pendedPixelUrbs[k] = NULL;
	}

	kfree(pdx->sgEntries);
	vfree(pdx->maplist_numPagesMapped);
	pdx->sgEntries = NULL;
	pdx->maplist_numPagesMapped = NULL;
	kfree(pdx->sgl);
	kfree(pdx->pendedPixelUrbs);
	kfree(pdx->PixelUrb);
	pdx->sgl = NULL;
	pdx->pendedPixelUrbs = NULL;
	pdx->PixelUrb = NULL;

	return 0;
}

static void piusb_readPIXEL_callback(struct urb *urb)
{
	int i = 0;
	struct device_extension *pdx = urb->context;
	int status = urb->status;

	if (status && !(status == -ENOENT || status == -ECONNRESET)) {
		dbg("%s - nonzero read bulk status received: %d", __func__,
		    status);
		dbg("Error in read EP2 callback");
		dbg("FrameIndex = %d", pdx->frameIdx);
		dbg("Bytes received before problem occurred = %d",
		    pdx->bulk_in_byte_trk);
		dbg("Urb Idx = %d", pdx->urbIdx);
		pdx->pendedPixelUrbs[pdx->frameIdx][pdx->urbIdx] = 0;
	} else {
		pdx->bulk_in_byte_trk += urb->actual_length;
		i = usb_submit_urb(urb, GFP_ATOMIC);	/* resubmit the URB */
		if (i) {
			errCnt++;
			if (i != lastErr) {
				dbg("submit urb in callback failed "
						"with error code %d", i);
				lastErr = i;
			}
		} else {
			pdx->urbIdx++; /* point to next URB when we callback */
			if (pdx->bulk_in_byte_trk >= pdx->frameSize) {
				pdx->bulk_in_size_returned =
					pdx->bulk_in_byte_trk;
				pdx->bulk_in_byte_trk = 0;
				pdx->gotPixelData = 1;
				pdx->frameIdx =
					((pdx->frameIdx +
					  1) % pdx->num_frames);
				pdx->urbIdx = 0;
			}
		}
	}
}

/* MapUserBuffer(
	inputs:
	struct ioctl_struct *io - structure containing user address,
				frame #, and size
	struct device_extension *pdx - the PIUSB device extension

	returns:
	int - status of the task

	Notes:
	MapUserBuffer maps a buffer passed down through an ioctl.
	The user buffer is Page Aligned by the app and then passed down.
	The function get_free_pages(...) does the actual mapping of the buffer
	from user space to kernel space.
	From there a scatterlist is created from all the pages.
	The next function called is to usb_buffer_map_sg which allocated
	DMA addresses for each page, even coalescing them if possible.
	The DMA address is placed in the scatterlist structure.
	The function returns the number of DMA addresses.
	This may or may not be equal to the number of pages that
	the user buffer uses.
	We then build an URB for each DMA address and then submit them.
*/

/*
int MapUserBuffer(unsigned long uaddr, unsigned long numbytes,
		unsigned long frameInfo, struct device_extension *pdx)
*/
static int MapUserBuffer(struct ioctl_struct *io, struct device_extension *pdx)
{
	unsigned long uaddr;
	unsigned long numbytes;
	int frameInfo;	/* which frame we're mapping */
	unsigned int epAddr = 0;
	unsigned long count = 0;
	int i = 0;
	int k = 0;
	int err = 0;
	struct page **maplist_p;
	int numPagesRequired;

	frameInfo = io->numFrames;
	uaddr = (unsigned long)io->pData;
	numbytes = io->numbytes;

	if (pdx->iama == PIXIS_PID) {
		/* which EP should we map this frame to ? */
		/* PONG, odd frames: hEP[3] */
		/* PING, even frames and zero hEP[2] */
		epAddr = (frameInfo % 2) ? pdx->hEP[3] : pdx->hEP[2];
		dbg("Pixis Frame #%d: EP=%d", frameInfo,
		    (epAddr == pdx->hEP[2]) ? 2 : 4);
	} else { /* ST133 only has 1 endpoint for Pixel data transfer */
		epAddr = pdx->hEP[0];
		dbg("ST133 Frame #%d: EP=2", frameInfo);
	}
	count = numbytes;
	dbg("UserAddress = 0x%08lX", uaddr);
	dbg("numbytes = %d", (int)numbytes);

	/* number of pages to map the entire user space DMA buffer */
	numPagesRequired =
	    ((uaddr & ~PAGE_MASK) + count + ~PAGE_MASK) >> PAGE_SHIFT;
	dbg("Number of pages needed = %d", numPagesRequired);
	maplist_p = vmalloc(numPagesRequired * sizeof(struct page));
	if (!maplist_p) {
		dbg("Can't Allocate Memory for maplist_p");
		return -ENOMEM;
	}

	/* map the user buffer to kernel memory */
	down_write(&current->mm->mmap_sem);
	pdx->maplist_numPagesMapped[frameInfo] = get_user_pages(current,
			current->mm, (uaddr & PAGE_MASK), numPagesRequired,
			WRITE, 0 /* Don't Force*/, maplist_p, NULL);
	up_write(&current->mm->mmap_sem);
	dbg("Number of pages mapped = %d",
	    pdx->maplist_numPagesMapped[frameInfo]);

	for (i = 0; i < pdx->maplist_numPagesMapped[frameInfo]; i++)
		flush_dcache_page(maplist_p[i]);
	if (!pdx->maplist_numPagesMapped[frameInfo]) {
		dbg("get_user_pages() failed");
		vfree(maplist_p);
		return -ENOMEM;
	}

	/* need to create a scatterlist that spans each frame
	 * that can fit into the mapped buffer
	 */
	pdx->sgl[frameInfo] =
	    kmalloc((pdx->maplist_numPagesMapped[frameInfo] *
		     sizeof(struct scatterlist)), GFP_ATOMIC);
	if (!pdx->sgl[frameInfo]) {
		vfree(maplist_p);
		dbg("can't allocate mem for sgl");
		return -ENOMEM;
	}
	sg_assign_page(&pdx->sgl[frameInfo][0], maplist_p[0]);
	pdx->sgl[frameInfo][0].offset = uaddr & ~PAGE_MASK;
	if (pdx->maplist_numPagesMapped[frameInfo] > 1) {
		pdx->sgl[frameInfo][0].length =
		    PAGE_SIZE - pdx->sgl[frameInfo][0].offset;
		count -= pdx->sgl[frameInfo][0].length;
		for (k = 1; k < pdx->maplist_numPagesMapped[frameInfo]; k++) {
			pdx->sgl[frameInfo][k].offset = 0;
			sg_assign_page(&pdx->sgl[frameInfo][k], maplist_p[k]);
			pdx->sgl[frameInfo][k].length =
			    (count < PAGE_SIZE) ? count : PAGE_SIZE;
			count -= PAGE_SIZE; /* example had PAGE_SIZE here */
		}
	} else {
		pdx->sgl[frameInfo][0].length = count;
	}
	pdx->sgEntries[frameInfo] =
	    usb_buffer_map_sg(pdx->udev, epAddr, pdx->sgl[frameInfo],
			      pdx->maplist_numPagesMapped[frameInfo]);
	dbg("number of sgEntries = %d", pdx->sgEntries[frameInfo]);
	pdx->userBufMapped = 1;
	vfree(maplist_p);

	/* Create and Send the URB's for each s/g entry */
	pdx->PixelUrb[frameInfo] =
	    kmalloc(pdx->sgEntries[frameInfo] * sizeof(struct urb *),
		    GFP_KERNEL);
	if (!pdx->PixelUrb[frameInfo]) {
		dbg("Can't Allocate Memory for Urb");
		return -ENOMEM;
	}
	for (i = 0; i < pdx->sgEntries[frameInfo]; i++) {
		/* 0 iso packets because we're using BULK transfers */
		pdx->PixelUrb[frameInfo][i] = usb_alloc_urb(0, GFP_KERNEL);
		usb_fill_bulk_urb(pdx->PixelUrb[frameInfo][i],
				  pdx->udev,
				  epAddr,
				  (dma_addr_t *) sg_dma_address(&pdx->
								sgl[frameInfo]
								[i]),
				  sg_dma_len(&pdx->sgl[frameInfo][i]),
				  piusb_readPIXEL_callback, (void *)pdx);
		pdx->PixelUrb[frameInfo][i]->transfer_dma =
		    sg_dma_address(&pdx->sgl[frameInfo][i]);
		pdx->PixelUrb[frameInfo][i]->transfer_flags =
		    URB_NO_TRANSFER_DMA_MAP | URB_NO_INTERRUPT;
	}
	/* only interrupt when last URB completes */
	pdx->PixelUrb[frameInfo][--i]->transfer_flags &= ~URB_NO_INTERRUPT;
	pdx->pendedPixelUrbs[frameInfo] =
	    kmalloc((pdx->sgEntries[frameInfo] * sizeof(char)), GFP_KERNEL);
	if (!pdx->pendedPixelUrbs[frameInfo])
		dbg("Can't allocate Memory for pendedPixelUrbs");
	for (i = 0; i < pdx->sgEntries[frameInfo]; i++) {
		err = usb_submit_urb(pdx->PixelUrb[frameInfo][i], GFP_ATOMIC);
		if (err) {
			dbg("%s %d\n", "submit urb error =", err);
			pdx->pendedPixelUrbs[frameInfo][i] = 0;
			return err;
		}
		pdx->pendedPixelUrbs[frameInfo][i] = 1;
	}
	return 0;
}

static const struct file_operations piusb_fops = {
	.owner = THIS_MODULE,
	.ioctl = piusb_ioctl,
	.open = piusb_open,
	.release = piusb_release,
};

static struct usb_class_driver piusb_class = {
	.name = "usb/rspiusb%d",
	.fops = &piusb_fops,
	.minor_base = PIUSB_MINOR_BASE,
};

/**
 *	piusb_probe
 *
 *	Called by the usb core when a new device is connected that it thinks
 *	this driver might be interested in.
 */
static int piusb_probe(struct usb_interface *interface,
		       const struct usb_device_id *id)
{
	struct device_extension *pdx = NULL;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int i;
	int retval = -ENOMEM;

	dev_dbg(&interface->dev, "%s - Looking for PI USB Hardware", __func__);

	pdx = kzalloc(sizeof(struct device_extension), GFP_KERNEL);
	if (pdx == NULL) {
		dev_err(&interface->dev, "Out of memory\n");
		goto error;
	}
	kref_init(&pdx->kref);
	pdx->udev = usb_get_dev(interface_to_usbdev(interface));
	pdx->interface = interface;
	iface_desc = interface->cur_altsetting;

	/* See if the device offered us matches what we can accept */
	if ((pdx->udev->descriptor.idVendor != VENDOR_ID)
	    || ((pdx->udev->descriptor.idProduct != PIXIS_PID)
		&& (pdx->udev->descriptor.idProduct != ST133_PID)))
		return -ENODEV;

	pdx->iama = pdx->udev->descriptor.idProduct;

	if (debug) {
		if (pdx->udev->descriptor.idProduct == PIXIS_PID)
			dbg("PIUSB:Pixis Camera Found");
		else
			dbg("PIUSB:ST133 USB Controller Found");
		if (pdx->udev->speed == USB_SPEED_HIGH)
			dbg("Highspeed(USB2.0) Device Attached");
		else
			dbg("Lowspeed (USB1.1) Device Attached");

		dbg("NumEndpoints in Configuration: %d",
		    iface_desc->desc.bNumEndpoints);
	}
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		if (debug) {
			dbg("Endpoint[%d]->bDescriptorType = %d", i,
			    endpoint->bDescriptorType);
			dbg("Endpoint[%d]->bEndpointAddress = 0x%02X", i,
			    endpoint->bEndpointAddress);
			dbg("Endpoint[%d]->bbmAttributes = %d", i,
			    endpoint->bmAttributes);
			dbg("Endpoint[%d]->MaxPacketSize = %d\n", i,
			    endpoint->wMaxPacketSize);
		}
		if (usb_endpoint_xfer_bulk(endpoint)) {
			if (usb_endpoint_dir_in(endpoint))
				pdx->hEP[i] =
				    usb_rcvbulkpipe(pdx->udev,
						    endpoint->bEndpointAddress);
			else
				pdx->hEP[i] =
				    usb_sndbulkpipe(pdx->udev,
						    endpoint->bEndpointAddress);
		}
	}
	usb_set_intfdata(interface, pdx);
	retval = usb_register_dev(interface, &piusb_class);
	if (retval) {
		err("Not able to get a minor for this device.");
		usb_set_intfdata(interface, NULL);
		goto error;
	}
	pdx->present = 1;

	/* we can register the device now, as it is ready */
	pdx->minor = interface->minor;
	/* let the user know what node this device is now attached to */
	dbg("PI USB2.0 device now attached to piusb-%d", pdx->minor);
	return 0;

error:
	if (pdx)
		kref_put(&pdx->kref, piusb_delete);
	return retval;
}

/**
 *	piusb_disconnect
 *
 *	Called by the usb core when the device is removed from the system.
 *
 *	This routine guarantees that the driver will not submit any more urbs
 *	by clearing pdx->udev.  It is also supposed to terminate any currently
 *	active urbs.  Unfortunately, usb_bulk_msg(), used in piusb_read(), does
 *	not provide any way to do this.  But at least we can cancel an active
 *	write.
 */
static void piusb_disconnect(struct usb_interface *interface)
{
	struct device_extension *pdx;
	int minor = interface->minor;

	lock_kernel();

	pdx = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	/* give back our minor */
	usb_deregister_dev(interface, &piusb_class);

	unlock_kernel();

	/* prevent device read, write and ioctl */
	pdx->present = 0;
	kref_put(&pdx->kref, piusb_delete);
	dbg("PI USB2.0 device #%d now disconnected\n", minor);
}

static struct usb_driver piusb_driver = {
	.name = "sub",
	.probe = piusb_probe,
	.disconnect = piusb_disconnect,
	.id_table = pi_device_table,
};

/**
 *	piusb_init
 */
static int __init piusb_init(void)
{
	int result;

	lastErr = 0;
	errCnt = 0;

	/* register this driver with the USB subsystem */
	result = usb_register(&piusb_driver);
	if (result)
		printk(KERN_ERR KBUILD_MODNAME
				": usb_register failed. Error number %d\n",
				result);
	else
		printk(KERN_INFO KBUILD_MODNAME ":%s: %s\n", DRIVER_DESC,
				DRIVER_VERSION);
	return result;
}

/**
 *	piusb_exit
 */
static void __exit piusb_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&piusb_driver);
}

module_init(piusb_init);
module_exit(piusb_exit);

/* Module parameters */
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug enabled or not");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");
