/* Linux driver for Philips webcam
   USB and Video4Linux interface part.
   (C) 1999-2004 Nemosoft Unv.
   (C) 2004-2006 Luc Saillard (luc@saillard.org)
   (C) 2011 Hans de Goede <hdegoede@redhat.com>

   NOTE: this version of pwc is an unofficial (modified) release of pwc & pcwx
   driver and thus may have bugs that are not present in the original version.
   Please send bug reports and support requests to <luc@saillard.org>.
   The decompression routines have been implemented by reverse-engineering the
   Nemosoft binary pwcx module. Caveat emptor.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

/*
   This code forms the interface between the USB layers and the Philips
   specific stuff. Some adanved stuff of the driver falls under an
   NDA, signed between me and Philips B.V., Eindhoven, the Netherlands, and
   is thus not distributed in source form. The binary pwcx.o module
   contains the code that falls under the NDA.

   In case you're wondering: 'pwc' stands for "Philips WebCam", but
   I really didn't want to type 'philips_web_cam' every time (I'm lazy as
   any Linux kernel hacker, but I don't like uncomprehensible abbreviations
   without explanation).

   Oh yes, convention: to disctinguish between all the various pointers to
   device-structures, I use these names for the pointer variables:
   udev: struct usb_device *
   vdev: struct video_device (member of pwc_dev)
   pdev: struct pwc_devive *
*/

/* Contributors:
   - Alvarado: adding whitebalance code
   - Alistar Moire: QuickCam 3000 Pro device/product ID
   - Tony Hoyle: Creative Labs Webcam 5 device/product ID
   - Mark Burazin: solving hang in VIDIOCSYNC when camera gets unplugged
   - Jk Fang: Sotec Afina Eye ID
   - Xavier Roche: QuickCam Pro 4000 ID
   - Jens Knudsen: QuickCam Zoom ID
   - J. Debert: QuickCam for Notebooks ID
   - Pham Thanh Nam: webcam snapshot button as an event input device
*/

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#ifdef CONFIG_USB_PWC_INPUT_EVDEV
#include <linux/usb/input.h>
#endif
#include <linux/vmalloc.h>
#include <asm/io.h>
#include <linux/kernel.h>		/* simple_strtol() */

#include "pwc.h"
#include "pwc-kiara.h"
#include "pwc-timon.h"
#include "pwc-dec23.h"
#include "pwc-dec1.h"

/* Function prototypes and driver templates */

/* hotplug device table support */
static const struct usb_device_id pwc_device_table [] = {
	{ USB_DEVICE(0x0471, 0x0302) }, /* Philips models */
	{ USB_DEVICE(0x0471, 0x0303) },
	{ USB_DEVICE(0x0471, 0x0304) },
	{ USB_DEVICE(0x0471, 0x0307) },
	{ USB_DEVICE(0x0471, 0x0308) },
	{ USB_DEVICE(0x0471, 0x030C) },
	{ USB_DEVICE(0x0471, 0x0310) },
	{ USB_DEVICE(0x0471, 0x0311) }, /* Philips ToUcam PRO II */
	{ USB_DEVICE(0x0471, 0x0312) },
	{ USB_DEVICE(0x0471, 0x0313) }, /* the 'new' 720K */
	{ USB_DEVICE(0x0471, 0x0329) }, /* Philips SPC 900NC PC Camera */
	{ USB_DEVICE(0x069A, 0x0001) }, /* Askey */
	{ USB_DEVICE(0x046D, 0x08B0) }, /* Logitech QuickCam Pro 3000 */
	{ USB_DEVICE(0x046D, 0x08B1) }, /* Logitech QuickCam Notebook Pro */
	{ USB_DEVICE(0x046D, 0x08B2) }, /* Logitech QuickCam Pro 4000 */
	{ USB_DEVICE(0x046D, 0x08B3) }, /* Logitech QuickCam Zoom (old model) */
	{ USB_DEVICE(0x046D, 0x08B4) }, /* Logitech QuickCam Zoom (new model) */
	{ USB_DEVICE(0x046D, 0x08B5) }, /* Logitech QuickCam Orbit/Sphere */
	{ USB_DEVICE(0x046D, 0x08B6) }, /* Cisco VT Camera */
	{ USB_DEVICE(0x046D, 0x08B7) }, /* Logitech ViewPort AV 100 */
	{ USB_DEVICE(0x046D, 0x08B8) }, /* Logitech (reserved) */
	{ USB_DEVICE(0x055D, 0x9000) }, /* Samsung MPC-C10 */
	{ USB_DEVICE(0x055D, 0x9001) }, /* Samsung MPC-C30 */
	{ USB_DEVICE(0x055D, 0x9002) },	/* Samsung SNC-35E (Ver3.0) */
	{ USB_DEVICE(0x041E, 0x400C) }, /* Creative Webcam 5 */
	{ USB_DEVICE(0x041E, 0x4011) }, /* Creative Webcam Pro Ex */
	{ USB_DEVICE(0x04CC, 0x8116) }, /* Afina Eye */
	{ USB_DEVICE(0x06BE, 0x8116) }, /* new Afina Eye */
	{ USB_DEVICE(0x0d81, 0x1910) }, /* Visionite */
	{ USB_DEVICE(0x0d81, 0x1900) },
	{ }
};
MODULE_DEVICE_TABLE(usb, pwc_device_table);

static int usb_pwc_probe(struct usb_interface *intf, const struct usb_device_id *id);
static void usb_pwc_disconnect(struct usb_interface *intf);
static void pwc_isoc_cleanup(struct pwc_device *pdev);

static struct usb_driver pwc_driver = {
	.name =			"Philips webcam",	/* name */
	.id_table =		pwc_device_table,
	.probe =		usb_pwc_probe,		/* probe() */
	.disconnect =		usb_pwc_disconnect,	/* disconnect() */
};

#define MAX_DEV_HINTS	20
#define MAX_ISOC_ERRORS	20

#ifdef CONFIG_USB_PWC_DEBUG
	int pwc_trace = PWC_DEBUG_LEVEL;
#endif
static int power_save = -1;
static int leds[2] = { 100, 0 };

/***/

static int pwc_video_close(struct file *file);
static ssize_t pwc_video_read(struct file *file, char __user *buf,
			  size_t count, loff_t *ppos);
static unsigned int pwc_video_poll(struct file *file, poll_table *wait);
static int  pwc_video_mmap(struct file *file, struct vm_area_struct *vma);

static const struct v4l2_file_operations pwc_fops = {
	.owner =	THIS_MODULE,
	.open =		v4l2_fh_open,
	.release =     	pwc_video_close,
	.read =		pwc_video_read,
	.poll =		pwc_video_poll,
	.mmap =		pwc_video_mmap,
	.unlocked_ioctl = video_ioctl2,
};
static struct video_device pwc_template = {
	.name =		"Philips Webcam",	/* Filled in later */
	.release =	video_device_release_empty,
	.fops =         &pwc_fops,
	.ioctl_ops =	&pwc_ioctl_ops,
};

/***************************************************************************/
/* Private functions */

struct pwc_frame_buf *pwc_get_next_fill_buf(struct pwc_device *pdev)
{
	unsigned long flags = 0;
	struct pwc_frame_buf *buf = NULL;

	spin_lock_irqsave(&pdev->queued_bufs_lock, flags);
	if (list_empty(&pdev->queued_bufs))
		goto leave;

	buf = list_entry(pdev->queued_bufs.next, struct pwc_frame_buf, list);
	list_del(&buf->list);
leave:
	spin_unlock_irqrestore(&pdev->queued_bufs_lock, flags);
	return buf;
}

static void pwc_snapshot_button(struct pwc_device *pdev, int down)
{
	if (down) {
		PWC_TRACE("Snapshot button pressed.\n");
	} else {
		PWC_TRACE("Snapshot button released.\n");
	}

#ifdef CONFIG_USB_PWC_INPUT_EVDEV
	if (pdev->button_dev) {
		input_report_key(pdev->button_dev, KEY_CAMERA, down);
		input_sync(pdev->button_dev);
	}
#endif
}

static void pwc_frame_complete(struct pwc_device *pdev)
{
	struct pwc_frame_buf *fbuf = pdev->fill_buf;

	/* The ToUCam Fun CMOS sensor causes the firmware to send 2 or 3 bogus
	   frames on the USB wire after an exposure change. This conditition is
	   however detected  in the cam and a bit is set in the header.
	   */
	if (pdev->type == 730) {
		unsigned char *ptr = (unsigned char *)fbuf->data;

		if (ptr[1] == 1 && ptr[0] & 0x10) {
			PWC_TRACE("Hyundai CMOS sensor bug. Dropping frame.\n");
			pdev->drop_frames += 2;
		}
		if ((ptr[0] ^ pdev->vmirror) & 0x01) {
			pwc_snapshot_button(pdev, ptr[0] & 0x01);
		}
		if ((ptr[0] ^ pdev->vmirror) & 0x02) {
			if (ptr[0] & 0x02)
				PWC_TRACE("Image is mirrored.\n");
			else
				PWC_TRACE("Image is normal.\n");
		}
		pdev->vmirror = ptr[0] & 0x03;
		/* Sometimes the trailer of the 730 is still sent as a 4 byte packet
		   after a short frame; this condition is filtered out specifically. A 4 byte
		   frame doesn't make sense anyway.
		   So we get either this sequence:
		   drop_bit set -> 4 byte frame -> short frame -> good frame
		   Or this one:
		   drop_bit set -> short frame -> good frame
		   So we drop either 3 or 2 frames in all!
		   */
		if (fbuf->filled == 4)
			pdev->drop_frames++;
	} else if (pdev->type == 740 || pdev->type == 720) {
		unsigned char *ptr = (unsigned char *)fbuf->data;
		if ((ptr[0] ^ pdev->vmirror) & 0x01) {
			pwc_snapshot_button(pdev, ptr[0] & 0x01);
		}
		pdev->vmirror = ptr[0] & 0x03;
	}

	/* In case we were instructed to drop the frame, do so silently. */
	if (pdev->drop_frames > 0) {
		pdev->drop_frames--;
	} else {
		/* Check for underflow first */
		if (fbuf->filled < pdev->frame_total_size) {
			PWC_DEBUG_FLOW("Frame buffer underflow (%d bytes);"
				       " discarded.\n", fbuf->filled);
		} else {
			fbuf->vb.v4l2_buf.field = V4L2_FIELD_NONE;
			fbuf->vb.v4l2_buf.sequence = pdev->vframe_count;
			vb2_buffer_done(&fbuf->vb, VB2_BUF_STATE_DONE);
			pdev->fill_buf = NULL;
			pdev->vsync = 0;
		}
	} /* !drop_frames */
	pdev->vframe_count++;
}

/* This gets called for the Isochronous pipe (video). This is done in
 * interrupt time, so it has to be fast, not crash, and not stall. Neat.
 */
static void pwc_isoc_handler(struct urb *urb)
{
	struct pwc_device *pdev = (struct pwc_device *)urb->context;
	int i, fst, flen;
	unsigned char *iso_buf = NULL;

	if (urb->status == -ENOENT || urb->status == -ECONNRESET ||
	    urb->status == -ESHUTDOWN) {
		PWC_DEBUG_OPEN("URB (%p) unlinked %ssynchronuously.\n", urb, urb->status == -ENOENT ? "" : "a");
		return;
	}

	if (pdev->fill_buf == NULL)
		pdev->fill_buf = pwc_get_next_fill_buf(pdev);

	if (urb->status != 0) {
		const char *errmsg;

		errmsg = "Unknown";
		switch(urb->status) {
			case -ENOSR:		errmsg = "Buffer error (overrun)"; break;
			case -EPIPE:		errmsg = "Stalled (device not responding)"; break;
			case -EOVERFLOW:	errmsg = "Babble (bad cable?)"; break;
			case -EPROTO:		errmsg = "Bit-stuff error (bad cable?)"; break;
			case -EILSEQ:		errmsg = "CRC/Timeout (could be anything)"; break;
			case -ETIME:		errmsg = "Device does not respond"; break;
		}
		PWC_ERROR("pwc_isoc_handler() called with status %d [%s].\n",
			  urb->status, errmsg);
		/* Give up after a number of contiguous errors */
		if (++pdev->visoc_errors > MAX_ISOC_ERRORS)
		{
			PWC_ERROR("Too many ISOC errors, bailing out.\n");
			if (pdev->fill_buf) {
				vb2_buffer_done(&pdev->fill_buf->vb,
						VB2_BUF_STATE_ERROR);
				pdev->fill_buf = NULL;
			}
		}
		pdev->vsync = 0; /* Drop the current frame */
		goto handler_end;
	}

	/* Reset ISOC error counter. We did get here, after all. */
	pdev->visoc_errors = 0;

	/* vsync: 0 = don't copy data
		  1 = sync-hunt
		  2 = synched
	 */
	/* Compact data */
	for (i = 0; i < urb->number_of_packets; i++) {
		fst  = urb->iso_frame_desc[i].status;
		flen = urb->iso_frame_desc[i].actual_length;
		iso_buf = urb->transfer_buffer + urb->iso_frame_desc[i].offset;
		if (fst != 0) {
			PWC_ERROR("Iso frame %d has error %d\n", i, fst);
			continue;
		}
		if (flen > 0 && pdev->vsync) {
			struct pwc_frame_buf *fbuf = pdev->fill_buf;

			if (pdev->vsync == 1) {
				do_gettimeofday(&fbuf->vb.v4l2_buf.timestamp);
				pdev->vsync = 2;
			}

			if (flen + fbuf->filled > pdev->frame_total_size) {
				PWC_ERROR("Frame overflow (%d > %d)\n",
					  flen + fbuf->filled,
					  pdev->frame_total_size);
				pdev->vsync = 0; /* Let's wait for an EOF */
			} else {
				memcpy(fbuf->data + fbuf->filled, iso_buf,
				       flen);
				fbuf->filled += flen;
			}
		}
		if (flen < pdev->vlast_packet_size) {
			/* Shorter packet... end of frame */
			if (pdev->vsync == 2)
				pwc_frame_complete(pdev);
			if (pdev->fill_buf == NULL)
				pdev->fill_buf = pwc_get_next_fill_buf(pdev);
			if (pdev->fill_buf) {
				pdev->fill_buf->filled = 0;
				pdev->vsync = 1;
			}
		}
		pdev->vlast_packet_size = flen;
	}

handler_end:
	i = usb_submit_urb(urb, GFP_ATOMIC);
	if (i != 0)
		PWC_ERROR("Error (%d) re-submitting urb in pwc_isoc_handler.\n", i);
}

static int pwc_isoc_init(struct pwc_device *pdev)
{
	struct usb_device *udev;
	struct urb *urb;
	int i, j, ret;
	struct usb_interface *intf;
	struct usb_host_interface *idesc = NULL;
	int compression = 0; /* 0..3 = uncompressed..high */

	if (pdev->iso_init)
		return 0;

	pdev->vsync = 0;
	pdev->vlast_packet_size = 0;
	pdev->fill_buf = NULL;
	pdev->vframe_count = 0;
	pdev->visoc_errors = 0;
	udev = pdev->udev;

retry:
	/* We first try with low compression and then retry with a higher
	   compression setting if there is not enough bandwidth. */
	ret = pwc_set_video_mode(pdev, pdev->width, pdev->height, pdev->pixfmt,
				 pdev->vframes, &compression, 1);

	/* Get the current alternate interface, adjust packet size */
	intf = usb_ifnum_to_if(udev, 0);
	if (intf)
		idesc = usb_altnum_to_altsetting(intf, pdev->valternate);
	if (!idesc)
		return -EIO;

	/* Search video endpoint */
	pdev->vmax_packet_size = -1;
	for (i = 0; i < idesc->desc.bNumEndpoints; i++) {
		if ((idesc->endpoint[i].desc.bEndpointAddress & 0xF) == pdev->vendpoint) {
			pdev->vmax_packet_size = le16_to_cpu(idesc->endpoint[i].desc.wMaxPacketSize);
			break;
		}
	}

	if (pdev->vmax_packet_size < 0 || pdev->vmax_packet_size > ISO_MAX_FRAME_SIZE) {
		PWC_ERROR("Failed to find packet size for video endpoint in current alternate setting.\n");
		return -ENFILE; /* Odd error, that should be noticeable */
	}

	/* Set alternate interface */
	PWC_DEBUG_OPEN("Setting alternate interface %d\n", pdev->valternate);
	ret = usb_set_interface(pdev->udev, 0, pdev->valternate);
	if (ret == -ENOSPC && compression < 3) {
		compression++;
		goto retry;
	}
	if (ret < 0)
		return ret;

	/* Allocate and init Isochronuous urbs */
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		urb = usb_alloc_urb(ISO_FRAMES_PER_DESC, GFP_KERNEL);
		if (urb == NULL) {
			PWC_ERROR("Failed to allocate urb %d\n", i);
			pdev->iso_init = 1;
			pwc_isoc_cleanup(pdev);
			return -ENOMEM;
		}
		pdev->urbs[i] = urb;
		PWC_DEBUG_MEMORY("Allocated URB at 0x%p\n", urb);

		urb->interval = 1; // devik
		urb->dev = udev;
		urb->pipe = usb_rcvisocpipe(udev, pdev->vendpoint);
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		urb->transfer_buffer = usb_alloc_coherent(udev,
							  ISO_BUFFER_SIZE,
							  GFP_KERNEL,
							  &urb->transfer_dma);
		if (urb->transfer_buffer == NULL) {
			PWC_ERROR("Failed to allocate urb buffer %d\n", i);
			pdev->iso_init = 1;
			pwc_isoc_cleanup(pdev);
			return -ENOMEM;
		}
		urb->transfer_buffer_length = ISO_BUFFER_SIZE;
		urb->complete = pwc_isoc_handler;
		urb->context = pdev;
		urb->start_frame = 0;
		urb->number_of_packets = ISO_FRAMES_PER_DESC;
		for (j = 0; j < ISO_FRAMES_PER_DESC; j++) {
			urb->iso_frame_desc[j].offset = j * ISO_MAX_FRAME_SIZE;
			urb->iso_frame_desc[j].length = pdev->vmax_packet_size;
		}
	}

	/* link */
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		ret = usb_submit_urb(pdev->urbs[i], GFP_KERNEL);
		if (ret == -ENOSPC && compression < 3) {
			compression++;
			pdev->iso_init = 1;
			pwc_isoc_cleanup(pdev);
			goto retry;
		}
		if (ret) {
			PWC_ERROR("isoc_init() submit_urb %d failed with error %d\n", i, ret);
			pdev->iso_init = 1;
			pwc_isoc_cleanup(pdev);
			return ret;
		}
		PWC_DEBUG_MEMORY("URB 0x%p submitted.\n", pdev->urbs[i]);
	}

	/* All is done... */
	pdev->iso_init = 1;
	PWC_DEBUG_OPEN("<< pwc_isoc_init()\n");
	return 0;
}

static void pwc_iso_stop(struct pwc_device *pdev)
{
	int i;

	/* Unlinking ISOC buffers one by one */
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		if (pdev->urbs[i]) {
			PWC_DEBUG_MEMORY("Unlinking URB %p\n", pdev->urbs[i]);
			usb_kill_urb(pdev->urbs[i]);
		}
	}
}

static void pwc_iso_free(struct pwc_device *pdev)
{
	int i;

	/* Freeing ISOC buffers one by one */
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		if (pdev->urbs[i]) {
			PWC_DEBUG_MEMORY("Freeing URB\n");
			if (pdev->urbs[i]->transfer_buffer) {
				usb_free_coherent(pdev->udev,
					pdev->urbs[i]->transfer_buffer_length,
					pdev->urbs[i]->transfer_buffer,
					pdev->urbs[i]->transfer_dma);
			}
			usb_free_urb(pdev->urbs[i]);
			pdev->urbs[i] = NULL;
		}
	}
}

static void pwc_isoc_cleanup(struct pwc_device *pdev)
{
	PWC_DEBUG_OPEN(">> pwc_isoc_cleanup()\n");

	if (pdev->iso_init == 0)
		return;

	pwc_iso_stop(pdev);
	pwc_iso_free(pdev);
	usb_set_interface(pdev->udev, 0, 0);

	pdev->iso_init = 0;
	PWC_DEBUG_OPEN("<< pwc_isoc_cleanup()\n");
}

static void pwc_cleanup_queued_bufs(struct pwc_device *pdev)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&pdev->queued_bufs_lock, flags);
	while (!list_empty(&pdev->queued_bufs)) {
		struct pwc_frame_buf *buf;

		buf = list_entry(pdev->queued_bufs.next, struct pwc_frame_buf,
				 list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&pdev->queued_bufs_lock, flags);
}

#ifdef CONFIG_USB_PWC_DEBUG
static const char *pwc_sensor_type_to_string(unsigned int sensor_type)
{
	switch(sensor_type) {
		case 0x00:
			return "Hyundai CMOS sensor";
		case 0x20:
			return "Sony CCD sensor + TDA8787";
		case 0x2E:
			return "Sony CCD sensor + Exas 98L59";
		case 0x2F:
			return "Sony CCD sensor + ADI 9804";
		case 0x30:
			return "Sharp CCD sensor + TDA8787";
		case 0x3E:
			return "Sharp CCD sensor + Exas 98L59";
		case 0x3F:
			return "Sharp CCD sensor + ADI 9804";
		case 0x40:
			return "UPA 1021 sensor";
		case 0x100:
			return "VGA sensor";
		case 0x101:
			return "PAL MR sensor";
		default:
			return "unknown type of sensor";
	}
}
#endif

/***************************************************************************/
/* Video4Linux functions */

int pwc_test_n_set_capt_file(struct pwc_device *pdev, struct file *file)
{
	int r = 0;

	mutex_lock(&pdev->capt_file_lock);
	if (pdev->capt_file != NULL &&
	    pdev->capt_file != file) {
		r = -EBUSY;
		goto leave;
	}
	pdev->capt_file = file;
leave:
	mutex_unlock(&pdev->capt_file_lock);
	return r;
}

static void pwc_video_release(struct v4l2_device *v)
{
	struct pwc_device *pdev = container_of(v, struct pwc_device, v4l2_dev);

	v4l2_ctrl_handler_free(&pdev->ctrl_handler);
	kfree(pdev->ctrl_buf);
	kfree(pdev);
}

static int pwc_video_close(struct file *file)
{
	struct pwc_device *pdev = video_drvdata(file);

	if (pdev->capt_file == file) {
		vb2_queue_release(&pdev->vb_queue);
		pdev->capt_file = NULL;
	}
	return v4l2_fh_release(file);
}

static ssize_t pwc_video_read(struct file *file, char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct pwc_device *pdev = video_drvdata(file);

	if (!pdev->udev)
		return -ENODEV;

	if (pwc_test_n_set_capt_file(pdev, file))
		return -EBUSY;

	return vb2_read(&pdev->vb_queue, buf, count, ppos,
			file->f_flags & O_NONBLOCK);
}

static unsigned int pwc_video_poll(struct file *file, poll_table *wait)
{
	struct pwc_device *pdev = video_drvdata(file);

	if (!pdev->udev)
		return POLL_ERR;

	return vb2_poll(&pdev->vb_queue, file, wait);
}

static int pwc_video_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct pwc_device *pdev = video_drvdata(file);

	if (pdev->capt_file != file)
		return -EBUSY;

	return vb2_mmap(&pdev->vb_queue, vma);
}

/***************************************************************************/
/* Videobuf2 operations */

static int queue_setup(struct vb2_queue *vq, const struct v4l2_format *fmt,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], void *alloc_ctxs[])
{
	struct pwc_device *pdev = vb2_get_drv_priv(vq);
	int size;

	if (*nbuffers < MIN_FRAMES)
		*nbuffers = MIN_FRAMES;
	else if (*nbuffers > MAX_FRAMES)
		*nbuffers = MAX_FRAMES;

	*nplanes = 1;

	size = pwc_get_size(pdev, MAX_WIDTH, MAX_HEIGHT);
	sizes[0] = PAGE_ALIGN(pwc_image_sizes[size][0] *
			      pwc_image_sizes[size][1] * 3 / 2);

	return 0;
}

static int buffer_init(struct vb2_buffer *vb)
{
	struct pwc_frame_buf *buf = container_of(vb, struct pwc_frame_buf, vb);

	/* need vmalloc since frame buffer > 128K */
	buf->data = vzalloc(PWC_FRAME_SIZE);
	if (buf->data == NULL)
		return -ENOMEM;

	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
	struct pwc_device *pdev = vb2_get_drv_priv(vb->vb2_queue);

	/* Don't allow queing new buffers after device disconnection */
	if (!pdev->udev)
		return -ENODEV;

	return 0;
}

static int buffer_finish(struct vb2_buffer *vb)
{
	struct pwc_device *pdev = vb2_get_drv_priv(vb->vb2_queue);
	struct pwc_frame_buf *buf = container_of(vb, struct pwc_frame_buf, vb);

	/*
	 * Application has called dqbuf and is getting back a buffer we've
	 * filled, take the pwc data we've stored in buf->data and decompress
	 * it into a usable format, storing the result in the vb2_buffer
	 */
	return pwc_decompress(pdev, buf);
}

static void buffer_cleanup(struct vb2_buffer *vb)
{
	struct pwc_frame_buf *buf = container_of(vb, struct pwc_frame_buf, vb);

	vfree(buf->data);
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct pwc_device *pdev = vb2_get_drv_priv(vb->vb2_queue);
	struct pwc_frame_buf *buf = container_of(vb, struct pwc_frame_buf, vb);
	unsigned long flags = 0;

	spin_lock_irqsave(&pdev->queued_bufs_lock, flags);
	/* Check the device has not disconnected between prep and queuing */
	if (pdev->udev)
		list_add_tail(&buf->list, &pdev->queued_bufs);
	else
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	spin_unlock_irqrestore(&pdev->queued_bufs_lock, flags);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct pwc_device *pdev = vb2_get_drv_priv(vq);
	int r;

	mutex_lock(&pdev->udevlock);
	if (!pdev->udev) {
		r = -ENODEV;
		goto leave;
	}

	/* Turn on camera and set LEDS on */
	pwc_camera_power(pdev, 1);
	pwc_set_leds(pdev, leds[0], leds[1]);

	r = pwc_isoc_init(pdev);
	if (r) {
		/* If we failed turn camera and LEDS back off */
		pwc_set_leds(pdev, 0, 0);
		pwc_camera_power(pdev, 0);
		/* And cleanup any queued bufs!! */
		pwc_cleanup_queued_bufs(pdev);
	}
leave:
	mutex_unlock(&pdev->udevlock);
	return r;
}

static int stop_streaming(struct vb2_queue *vq)
{
	struct pwc_device *pdev = vb2_get_drv_priv(vq);

	mutex_lock(&pdev->udevlock);
	if (pdev->udev) {
		pwc_set_leds(pdev, 0, 0);
		pwc_camera_power(pdev, 0);
		pwc_isoc_cleanup(pdev);
	}
	mutex_unlock(&pdev->udevlock);

	pwc_cleanup_queued_bufs(pdev);

	return 0;
}

static struct vb2_ops pwc_vb_queue_ops = {
	.queue_setup		= queue_setup,
	.buf_init		= buffer_init,
	.buf_prepare		= buffer_prepare,
	.buf_finish		= buffer_finish,
	.buf_cleanup		= buffer_cleanup,
	.buf_queue		= buffer_queue,
	.start_streaming	= start_streaming,
	.stop_streaming		= stop_streaming,
};

/***************************************************************************/
/* USB functions */

/* This function gets called when a new device is plugged in or the usb core
 * is loaded.
 */

static int usb_pwc_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct pwc_device *pdev = NULL;
	int vendor_id, product_id, type_id;
	int rc;
	int features = 0;
	int compression = 0;
	int my_power_save = power_save;
	char serial_number[30], *name;

	vendor_id = le16_to_cpu(udev->descriptor.idVendor);
	product_id = le16_to_cpu(udev->descriptor.idProduct);

	/* Check if we can handle this device */
	PWC_DEBUG_PROBE("probe() called [%04X %04X], if %d\n",
		vendor_id, product_id,
		intf->altsetting->desc.bInterfaceNumber);

	/* the interfaces are probed one by one. We are only interested in the
	   video interface (0) now.
	   Interface 1 is the Audio Control, and interface 2 Audio itself.
	 */
	if (intf->altsetting->desc.bInterfaceNumber > 0)
		return -ENODEV;

	if (vendor_id == 0x0471) {
		switch (product_id) {
		case 0x0302:
			PWC_INFO("Philips PCA645VC USB webcam detected.\n");
			name = "Philips 645 webcam";
			type_id = 645;
			break;
		case 0x0303:
			PWC_INFO("Philips PCA646VC USB webcam detected.\n");
			name = "Philips 646 webcam";
			type_id = 646;
			break;
		case 0x0304:
			PWC_INFO("Askey VC010 type 2 USB webcam detected.\n");
			name = "Askey VC010 webcam";
			type_id = 646;
			break;
		case 0x0307:
			PWC_INFO("Philips PCVC675K (Vesta) USB webcam detected.\n");
			name = "Philips 675 webcam";
			type_id = 675;
			break;
		case 0x0308:
			PWC_INFO("Philips PCVC680K (Vesta Pro) USB webcam detected.\n");
			name = "Philips 680 webcam";
			type_id = 680;
			break;
		case 0x030C:
			PWC_INFO("Philips PCVC690K (Vesta Pro Scan) USB webcam detected.\n");
			name = "Philips 690 webcam";
			type_id = 690;
			break;
		case 0x0310:
			PWC_INFO("Philips PCVC730K (ToUCam Fun)/PCVC830 (ToUCam II) USB webcam detected.\n");
			name = "Philips 730 webcam";
			type_id = 730;
			break;
		case 0x0311:
			PWC_INFO("Philips PCVC740K (ToUCam Pro)/PCVC840 (ToUCam II) USB webcam detected.\n");
			name = "Philips 740 webcam";
			type_id = 740;
			break;
		case 0x0312:
			PWC_INFO("Philips PCVC750K (ToUCam Pro Scan) USB webcam detected.\n");
			name = "Philips 750 webcam";
			type_id = 750;
			break;
		case 0x0313:
			PWC_INFO("Philips PCVC720K/40 (ToUCam XS) USB webcam detected.\n");
			name = "Philips 720K/40 webcam";
			type_id = 720;
			break;
		case 0x0329:
			PWC_INFO("Philips SPC 900NC USB webcam detected.\n");
			name = "Philips SPC 900NC webcam";
			type_id = 740;
			break;
		default:
			return -ENODEV;
			break;
		}
	}
	else if (vendor_id == 0x069A) {
		switch(product_id) {
		case 0x0001:
			PWC_INFO("Askey VC010 type 1 USB webcam detected.\n");
			name = "Askey VC010 webcam";
			type_id = 645;
			break;
		default:
			return -ENODEV;
			break;
		}
	}
	else if (vendor_id == 0x046d) {
		switch(product_id) {
		case 0x08b0:
			PWC_INFO("Logitech QuickCam Pro 3000 USB webcam detected.\n");
			name = "Logitech QuickCam Pro 3000";
			type_id = 740; /* CCD sensor */
			break;
		case 0x08b1:
			PWC_INFO("Logitech QuickCam Notebook Pro USB webcam detected.\n");
			name = "Logitech QuickCam Notebook Pro";
			type_id = 740; /* CCD sensor */
			break;
		case 0x08b2:
			PWC_INFO("Logitech QuickCam 4000 Pro USB webcam detected.\n");
			name = "Logitech QuickCam Pro 4000";
			type_id = 740; /* CCD sensor */
			if (my_power_save == -1)
				my_power_save = 1;
			break;
		case 0x08b3:
			PWC_INFO("Logitech QuickCam Zoom USB webcam detected.\n");
			name = "Logitech QuickCam Zoom";
			type_id = 740; /* CCD sensor */
			break;
		case 0x08B4:
			PWC_INFO("Logitech QuickCam Zoom (new model) USB webcam detected.\n");
			name = "Logitech QuickCam Zoom";
			type_id = 740; /* CCD sensor */
			if (my_power_save == -1)
				my_power_save = 1;
			break;
		case 0x08b5:
			PWC_INFO("Logitech QuickCam Orbit/Sphere USB webcam detected.\n");
			name = "Logitech QuickCam Orbit";
			type_id = 740; /* CCD sensor */
			if (my_power_save == -1)
				my_power_save = 1;
			features |= FEATURE_MOTOR_PANTILT;
			break;
		case 0x08b6:
			PWC_INFO("Logitech/Cisco VT Camera webcam detected.\n");
			name = "Cisco VT Camera";
			type_id = 740; /* CCD sensor */
			break;
		case 0x08b7:
			PWC_INFO("Logitech ViewPort AV 100 webcam detected.\n");
			name = "Logitech ViewPort AV 100";
			type_id = 740; /* CCD sensor */
			break;
		case 0x08b8: /* Where this released? */
			PWC_INFO("Logitech QuickCam detected (reserved ID).\n");
			name = "Logitech QuickCam (res.)";
			type_id = 730; /* Assuming CMOS */
			break;
		default:
			return -ENODEV;
			break;
		}
	}
	else if (vendor_id == 0x055d) {
		/* I don't know the difference between the C10 and the C30;
		   I suppose the difference is the sensor, but both cameras
		   work equally well with a type_id of 675
		 */
		switch(product_id) {
		case 0x9000:
			PWC_INFO("Samsung MPC-C10 USB webcam detected.\n");
			name = "Samsung MPC-C10";
			type_id = 675;
			break;
		case 0x9001:
			PWC_INFO("Samsung MPC-C30 USB webcam detected.\n");
			name = "Samsung MPC-C30";
			type_id = 675;
			break;
		case 0x9002:
			PWC_INFO("Samsung SNC-35E (v3.0) USB webcam detected.\n");
			name = "Samsung MPC-C30";
			type_id = 740;
			break;
		default:
			return -ENODEV;
			break;
		}
	}
	else if (vendor_id == 0x041e) {
		switch(product_id) {
		case 0x400c:
			PWC_INFO("Creative Labs Webcam 5 detected.\n");
			name = "Creative Labs Webcam 5";
			type_id = 730;
			if (my_power_save == -1)
				my_power_save = 1;
			break;
		case 0x4011:
			PWC_INFO("Creative Labs Webcam Pro Ex detected.\n");
			name = "Creative Labs Webcam Pro Ex";
			type_id = 740;
			break;
		default:
			return -ENODEV;
			break;
		}
	}
	else if (vendor_id == 0x04cc) {
		switch(product_id) {
		case 0x8116:
			PWC_INFO("Sotec Afina Eye USB webcam detected.\n");
			name = "Sotec Afina Eye";
			type_id = 730;
			break;
		default:
			return -ENODEV;
			break;
		}
	}
	else if (vendor_id == 0x06be) {
		switch(product_id) {
		case 0x8116:
			/* This is essentially the same cam as the Sotec Afina Eye */
			PWC_INFO("AME Co. Afina Eye USB webcam detected.\n");
			name = "AME Co. Afina Eye";
			type_id = 750;
			break;
		default:
			return -ENODEV;
			break;
		}

	}
	else if (vendor_id == 0x0d81) {
		switch(product_id) {
		case 0x1900:
			PWC_INFO("Visionite VCS-UC300 USB webcam detected.\n");
			name = "Visionite VCS-UC300";
			type_id = 740; /* CCD sensor */
			break;
		case 0x1910:
			PWC_INFO("Visionite VCS-UM100 USB webcam detected.\n");
			name = "Visionite VCS-UM100";
			type_id = 730; /* CMOS sensor */
			break;
		default:
			return -ENODEV;
			break;
		}
	}
	else
		return -ENODEV; /* Not any of the know types; but the list keeps growing. */

	if (my_power_save == -1)
		my_power_save = 0;

	memset(serial_number, 0, 30);
	usb_string(udev, udev->descriptor.iSerialNumber, serial_number, 29);
	PWC_DEBUG_PROBE("Device serial number is %s\n", serial_number);

	if (udev->descriptor.bNumConfigurations > 1)
		PWC_WARNING("Warning: more than 1 configuration available.\n");

	/* Allocate structure, initialize pointers, mutexes, etc. and link it to the usb_device */
	pdev = kzalloc(sizeof(struct pwc_device), GFP_KERNEL);
	if (pdev == NULL) {
		PWC_ERROR("Oops, could not allocate memory for pwc_device.\n");
		return -ENOMEM;
	}
	pdev->type = type_id;
	pdev->features = features;
	pwc_construct(pdev); /* set min/max sizes correct */

	mutex_init(&pdev->capt_file_lock);
	mutex_init(&pdev->udevlock);
	spin_lock_init(&pdev->queued_bufs_lock);
	INIT_LIST_HEAD(&pdev->queued_bufs);

	pdev->udev = udev;
	pdev->power_save = my_power_save;

	/* Init videobuf2 queue structure */
	memset(&pdev->vb_queue, 0, sizeof(pdev->vb_queue));
	pdev->vb_queue.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	pdev->vb_queue.io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	pdev->vb_queue.drv_priv = pdev;
	pdev->vb_queue.buf_struct_size = sizeof(struct pwc_frame_buf);
	pdev->vb_queue.ops = &pwc_vb_queue_ops;
	pdev->vb_queue.mem_ops = &vb2_vmalloc_memops;
	vb2_queue_init(&pdev->vb_queue);

	/* Init video_device structure */
	memcpy(&pdev->vdev, &pwc_template, sizeof(pwc_template));
	strcpy(pdev->vdev.name, name);
	set_bit(V4L2_FL_USE_FH_PRIO, &pdev->vdev.flags);
	video_set_drvdata(&pdev->vdev, pdev);

	pdev->release = le16_to_cpu(udev->descriptor.bcdDevice);
	PWC_DEBUG_PROBE("Release: %04x\n", pdev->release);

	/* Allocate USB command buffers */
	pdev->ctrl_buf = kmalloc(sizeof(pdev->cmd_buf), GFP_KERNEL);
	if (!pdev->ctrl_buf) {
		PWC_ERROR("Oops, could not allocate memory for pwc_device.\n");
		rc = -ENOMEM;
		goto err_free_mem;
	}

#ifdef CONFIG_USB_PWC_DEBUG
	/* Query sensor type */
	if (pwc_get_cmos_sensor(pdev, &rc) >= 0) {
		PWC_DEBUG_OPEN("This %s camera is equipped with a %s (%d).\n",
				pdev->vdev.name,
				pwc_sensor_type_to_string(rc), rc);
	}
#endif

	/* Set the leds off */
	pwc_set_leds(pdev, 0, 0);

	/* Setup intial videomode */
	rc = pwc_set_video_mode(pdev, MAX_WIDTH, MAX_HEIGHT,
				V4L2_PIX_FMT_YUV420, 30, &compression, 1);
	if (rc)
		goto err_free_mem;

	/* Register controls (and read default values from camera */
	rc = pwc_init_controls(pdev);
	if (rc) {
		PWC_ERROR("Failed to register v4l2 controls (%d).\n", rc);
		goto err_free_mem;
	}

	/* And powerdown the camera until streaming starts */
	pwc_camera_power(pdev, 0);

	/* Register the v4l2_device structure */
	pdev->v4l2_dev.release = pwc_video_release;
	rc = v4l2_device_register(&intf->dev, &pdev->v4l2_dev);
	if (rc) {
		PWC_ERROR("Failed to register v4l2-device (%d).\n", rc);
		goto err_free_controls;
	}

	pdev->v4l2_dev.ctrl_handler = &pdev->ctrl_handler;
	pdev->vdev.v4l2_dev = &pdev->v4l2_dev;

	rc = video_register_device(&pdev->vdev, VFL_TYPE_GRABBER, -1);
	if (rc < 0) {
		PWC_ERROR("Failed to register as video device (%d).\n", rc);
		goto err_unregister_v4l2_dev;
	}
	PWC_INFO("Registered as %s.\n", video_device_node_name(&pdev->vdev));

#ifdef CONFIG_USB_PWC_INPUT_EVDEV
	/* register webcam snapshot button input device */
	pdev->button_dev = input_allocate_device();
	if (!pdev->button_dev) {
		PWC_ERROR("Err, insufficient memory for webcam snapshot button device.");
		rc = -ENOMEM;
		goto err_video_unreg;
	}

	usb_make_path(udev, pdev->button_phys, sizeof(pdev->button_phys));
	strlcat(pdev->button_phys, "/input0", sizeof(pdev->button_phys));

	pdev->button_dev->name = "PWC snapshot button";
	pdev->button_dev->phys = pdev->button_phys;
	usb_to_input_id(pdev->udev, &pdev->button_dev->id);
	pdev->button_dev->dev.parent = &pdev->udev->dev;
	pdev->button_dev->evbit[0] = BIT_MASK(EV_KEY);
	pdev->button_dev->keybit[BIT_WORD(KEY_CAMERA)] = BIT_MASK(KEY_CAMERA);

	rc = input_register_device(pdev->button_dev);
	if (rc) {
		input_free_device(pdev->button_dev);
		pdev->button_dev = NULL;
		goto err_video_unreg;
	}
#endif

	return 0;

err_video_unreg:
	video_unregister_device(&pdev->vdev);
err_unregister_v4l2_dev:
	v4l2_device_unregister(&pdev->v4l2_dev);
err_free_controls:
	v4l2_ctrl_handler_free(&pdev->ctrl_handler);
err_free_mem:
	kfree(pdev->ctrl_buf);
	kfree(pdev);
	return rc;
}

/* The user yanked out the cable... */
static void usb_pwc_disconnect(struct usb_interface *intf)
{
	struct v4l2_device *v = usb_get_intfdata(intf);
	struct pwc_device *pdev = container_of(v, struct pwc_device, v4l2_dev);

	mutex_lock(&pdev->udevlock);
	/* No need to keep the urbs around after disconnection */
	pwc_isoc_cleanup(pdev);
	pdev->udev = NULL;
	mutex_unlock(&pdev->udevlock);

	pwc_cleanup_queued_bufs(pdev);

	video_unregister_device(&pdev->vdev);
	v4l2_device_unregister(&pdev->v4l2_dev);

#ifdef CONFIG_USB_PWC_INPUT_EVDEV
	if (pdev->button_dev)
		input_unregister_device(pdev->button_dev);
#endif

	v4l2_device_put(&pdev->v4l2_dev);
}


/*
 * Initialization code & module stuff
 */

static unsigned int leds_nargs;

#ifdef CONFIG_USB_PWC_DEBUG
module_param_named(trace, pwc_trace, int, 0644);
#endif
module_param(power_save, int, 0644);
module_param_array(leds, int, &leds_nargs, 0444);

#ifdef CONFIG_USB_PWC_DEBUG
MODULE_PARM_DESC(trace, "For debugging purposes");
#endif
MODULE_PARM_DESC(power_save, "Turn power saving for new cameras on or off");
MODULE_PARM_DESC(leds, "LED on,off time in milliseconds");

MODULE_DESCRIPTION("Philips & OEM USB webcam driver");
MODULE_AUTHOR("Luc Saillard <luc@saillard.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("pwcx");
MODULE_VERSION( PWC_VERSION );

static int __init usb_pwc_init(void)
{
	return usb_register(&pwc_driver);
}

static void __exit usb_pwc_exit(void)
{
	usb_deregister(&pwc_driver);
}

module_init(usb_pwc_init);
module_exit(usb_pwc_exit);
