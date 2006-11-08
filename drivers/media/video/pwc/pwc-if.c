/* Linux driver for Philips webcam
   USB and Video4Linux interface part.
   (C) 1999-2004 Nemosoft Unv.
   (C) 2004-2006 Luc Saillard (luc@saillard.org)

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
   vdev: struct video_device *
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
*/

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/version.h>
#include <asm/io.h>
#include <linux/moduleparam.h>

#include "pwc.h"
#include "pwc-kiara.h"
#include "pwc-timon.h"
#include "pwc-dec23.h"
#include "pwc-dec1.h"
#include "pwc-uncompress.h"

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
	{ USB_DEVICE(0x046D, 0x08B6) }, /* Logitech (reserved) */
	{ USB_DEVICE(0x046D, 0x08B7) }, /* Logitech (reserved) */
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

static struct usb_driver pwc_driver = {
	.name =			"Philips webcam",	/* name */
	.id_table =		pwc_device_table,
	.probe =		usb_pwc_probe,		/* probe() */
	.disconnect =		usb_pwc_disconnect,	/* disconnect() */
};

#define MAX_DEV_HINTS	20
#define MAX_ISOC_ERRORS	20

static int default_size = PSZ_QCIF;
static int default_fps = 10;
static int default_fbufs = 3;   /* Default number of frame buffers */
       int pwc_mbufs = 2;	/* Default number of mmap() buffers */
#if CONFIG_PWC_DEBUG
       int pwc_trace = PWC_DEBUG_LEVEL;
#endif
static int power_save = 0;
static int led_on = 100, led_off = 0; /* defaults to LED that is on while in use */
static int pwc_preferred_compression = 1; /* 0..3 = uncompressed..high */
static struct {
	int type;
	char serial_number[30];
	int device_node;
	struct pwc_device *pdev;
} device_hint[MAX_DEV_HINTS];

/***/

static int pwc_video_open(struct inode *inode, struct file *file);
static int pwc_video_close(struct inode *inode, struct file *file);
static ssize_t pwc_video_read(struct file *file, char __user *buf,
			  size_t count, loff_t *ppos);
static unsigned int pwc_video_poll(struct file *file, poll_table *wait);
static int  pwc_video_ioctl(struct inode *inode, struct file *file,
			    unsigned int ioctlnr, unsigned long arg);
static int  pwc_video_mmap(struct file *file, struct vm_area_struct *vma);

static struct file_operations pwc_fops = {
	.owner =	THIS_MODULE,
	.open =		pwc_video_open,
	.release =     	pwc_video_close,
	.read =		pwc_video_read,
	.poll =		pwc_video_poll,
	.mmap =		pwc_video_mmap,
	.ioctl =        pwc_video_ioctl,
	.compat_ioctl = v4l_compat_ioctl32,
	.llseek =       no_llseek,
};
static struct video_device pwc_template = {
	.owner =	THIS_MODULE,
	.name =		"Philips Webcam",	/* Filled in later */
	.type =		VID_TYPE_CAPTURE,
	.hardware =	VID_HARDWARE_PWC,
	.release =	video_device_release,
	.fops =         &pwc_fops,
	.minor =        -1,
};

/***************************************************************************/

/* Okay, this is some magic that I worked out and the reasoning behind it...

   The biggest problem with any USB device is of course: "what to do
   when the user unplugs the device while it is in use by an application?"
   We have several options:
   1) Curse them with the 7 plagues when they do (requires divine intervention)
   2) Tell them not to (won't work: they'll do it anyway)
   3) Oops the kernel (this will have a negative effect on a user's uptime)
   4) Do something sensible.

   Of course, we go for option 4.

   It happens that this device will be linked to two times, once from
   usb_device and once from the video_device in their respective 'private'
   pointers. This is done when the device is probed() and all initialization
   succeeded. The pwc_device struct links back to both structures.

   When a device is unplugged while in use it will be removed from the
   list of known USB devices; I also de-register it as a V4L device, but
   unfortunately I can't free the memory since the struct is still in use
   by the file descriptor. This free-ing is then deferend until the first
   opportunity. Crude, but it works.

   A small 'advantage' is that if a user unplugs the cam and plugs it back
   in, it should get assigned the same video device minor, but unfortunately
   it's non-trivial to re-link the cam back to the video device... (that
   would surely be magic! :))
*/

/***************************************************************************/
/* Private functions */

/* Here we want the physical address of the memory.
 * This is used when initializing the contents of the area.
 */



static void *pwc_rvmalloc(unsigned long size)
{
	void * mem;
	unsigned long adr;

	mem=vmalloc_32(size);
	if (!mem)
		return NULL;

	memset(mem, 0, size); /* Clear the ram out, no junk to the user */
	adr=(unsigned long) mem;
	while (size > 0)
	 {
	   SetPageReserved(vmalloc_to_page((void *)adr));
	   adr  += PAGE_SIZE;
	   size -= PAGE_SIZE;
	 }
	return mem;
}

static void pwc_rvfree(void * mem, unsigned long size)
{
	unsigned long adr;

	if (!mem)
		return;

	adr=(unsigned long) mem;
	while ((long) size > 0)
	 {
	   ClearPageReserved(vmalloc_to_page((void *)adr));
	   adr  += PAGE_SIZE;
	   size -= PAGE_SIZE;
	 }
	vfree(mem);
}




static int pwc_allocate_buffers(struct pwc_device *pdev)
{
	int i, err;
	void *kbuf;

	PWC_DEBUG_MEMORY(">> pwc_allocate_buffers(pdev = 0x%p)\n", pdev);

	if (pdev == NULL)
		return -ENXIO;

	/* Allocate Isochronuous pipe buffers */
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		if (pdev->sbuf[i].data == NULL) {
			kbuf = kzalloc(ISO_BUFFER_SIZE, GFP_KERNEL);
			if (kbuf == NULL) {
				PWC_ERROR("Failed to allocate iso buffer %d.\n", i);
				return -ENOMEM;
			}
			PWC_DEBUG_MEMORY("Allocated iso buffer at %p.\n", kbuf);
			pdev->sbuf[i].data = kbuf;
		}
	}

	/* Allocate frame buffer structure */
	if (pdev->fbuf == NULL) {
		kbuf = kzalloc(default_fbufs * sizeof(struct pwc_frame_buf), GFP_KERNEL);
		if (kbuf == NULL) {
			PWC_ERROR("Failed to allocate frame buffer structure.\n");
			return -ENOMEM;
		}
		PWC_DEBUG_MEMORY("Allocated frame buffer structure at %p.\n", kbuf);
		pdev->fbuf = kbuf;
	}

	/* create frame buffers, and make circular ring */
	for (i = 0; i < default_fbufs; i++) {
		if (pdev->fbuf[i].data == NULL) {
			kbuf = vmalloc(PWC_FRAME_SIZE); /* need vmalloc since frame buffer > 128K */
			if (kbuf == NULL) {
				PWC_ERROR("Failed to allocate frame buffer %d.\n", i);
				return -ENOMEM;
			}
			PWC_DEBUG_MEMORY("Allocated frame buffer %d at %p.\n", i, kbuf);
			pdev->fbuf[i].data = kbuf;
			memset(kbuf, 0, PWC_FRAME_SIZE);
		}
	}

	/* Allocate decompressor table space */
	if (DEVICE_USE_CODEC1(pdev->type))
		err = pwc_dec1_alloc(pdev);
	else
		err = pwc_dec23_alloc(pdev);

	if (err) {
		PWC_ERROR("Failed to allocate decompress table.\n");
		return err;
	}

	/* Allocate image buffer; double buffer for mmap() */
	kbuf = pwc_rvmalloc(pwc_mbufs * pdev->len_per_image);
	if (kbuf == NULL) {
		PWC_ERROR("Failed to allocate image buffer(s). needed (%d)\n",
				pwc_mbufs * pdev->len_per_image);
		return -ENOMEM;
	}
	PWC_DEBUG_MEMORY("Allocated image buffer at %p.\n", kbuf);
	pdev->image_data = kbuf;
	for (i = 0; i < pwc_mbufs; i++) {
		pdev->images[i].offset = i * pdev->len_per_image;
		pdev->images[i].vma_use_count = 0;
	}
	for (; i < MAX_IMAGES; i++) {
		pdev->images[i].offset = 0;
	}

	kbuf = NULL;

	PWC_DEBUG_MEMORY("<< pwc_allocate_buffers()\n");
	return 0;
}

static void pwc_free_buffers(struct pwc_device *pdev)
{
	int i;

	PWC_DEBUG_MEMORY("Entering free_buffers(%p).\n", pdev);

	if (pdev == NULL)
		return;
	/* Release Iso-pipe buffers */
	for (i = 0; i < MAX_ISO_BUFS; i++)
		if (pdev->sbuf[i].data != NULL) {
			PWC_DEBUG_MEMORY("Freeing ISO buffer at %p.\n", pdev->sbuf[i].data);
			kfree(pdev->sbuf[i].data);
			pdev->sbuf[i].data = NULL;
		}

	/* The same for frame buffers */
	if (pdev->fbuf != NULL) {
		for (i = 0; i < default_fbufs; i++) {
			if (pdev->fbuf[i].data != NULL) {
				PWC_DEBUG_MEMORY("Freeing frame buffer %d at %p.\n", i, pdev->fbuf[i].data);
				vfree(pdev->fbuf[i].data);
				pdev->fbuf[i].data = NULL;
			}
		}
		kfree(pdev->fbuf);
		pdev->fbuf = NULL;
	}

	/* Intermediate decompression buffer & tables */
	if (pdev->decompress_data != NULL) {
		PWC_DEBUG_MEMORY("Freeing decompression buffer at %p.\n", pdev->decompress_data);
		kfree(pdev->decompress_data);
		pdev->decompress_data = NULL;
	}

	/* Release image buffers */
	if (pdev->image_data != NULL) {
		PWC_DEBUG_MEMORY("Freeing image buffer at %p.\n", pdev->image_data);
		pwc_rvfree(pdev->image_data, pwc_mbufs * pdev->len_per_image);
	}
	pdev->image_data = NULL;

	PWC_DEBUG_MEMORY("Leaving free_buffers().\n");
}

/* The frame & image buffer mess.

   Yes, this is a mess. Well, it used to be simple, but alas...  In this
   module, 3 buffers schemes are used to get the data from the USB bus to
   the user program. The first scheme involves the ISO buffers (called thus
   since they transport ISO data from the USB controller), and not really
   interesting. Suffices to say the data from this buffer is quickly
   gathered in an interrupt handler (pwc_isoc_handler) and placed into the
   frame buffer.

   The frame buffer is the second scheme, and is the central element here.
   It collects the data from a single frame from the camera (hence, the
   name). Frames are delimited by the USB camera with a short USB packet,
   so that's easy to detect. The frame buffers form a list that is filled
   by the camera+USB controller and drained by the user process through
   either read() or mmap().

   The image buffer is the third scheme, in which frames are decompressed
   and converted into planar format. For mmap() there is more than
   one image buffer available.

   The frame buffers provide the image buffering. In case the user process
   is a bit slow, this introduces lag and some undesired side-effects.
   The problem arises when the frame buffer is full. I used to drop the last
   frame, which makes the data in the queue stale very quickly. But dropping
   the frame at the head of the queue proved to be a litte bit more difficult.
   I tried a circular linked scheme, but this introduced more problems than
   it solved.

   Because filling and draining are completely asynchronous processes, this
   requires some fiddling with pointers and mutexes.

   Eventually, I came up with a system with 2 lists: an 'empty' frame list
   and a 'full' frame list:
     * Initially, all frame buffers but one are on the 'empty' list; the one
       remaining buffer is our initial fill frame.
     * If a frame is needed for filling, we try to take it from the 'empty'
       list, unless that list is empty, in which case we take the buffer at
       the head of the 'full' list.
     * When our fill buffer has been filled, it is appended to the 'full'
       list.
     * If a frame is needed by read() or mmap(), it is taken from the head of
       the 'full' list, handled, and then appended to the 'empty' list. If no
       buffer is present on the 'full' list, we wait.
   The advantage is that the buffer that is currently being decompressed/
   converted, is on neither list, and thus not in our way (any other scheme
   I tried had the problem of old data lingering in the queue).

   Whatever strategy you choose, it always remains a tradeoff: with more
   frame buffers the chances of a missed frame are reduced. On the other
   hand, on slower machines it introduces lag because the queue will
   always be full.
 */

/**
  \brief Find next frame buffer to fill. Take from empty or full list, whichever comes first.
 */
static int pwc_next_fill_frame(struct pwc_device *pdev)
{
	int ret;
	unsigned long flags;

	ret = 0;
	spin_lock_irqsave(&pdev->ptrlock, flags);
	if (pdev->fill_frame != NULL) {
		/* append to 'full' list */
		if (pdev->full_frames == NULL) {
			pdev->full_frames = pdev->fill_frame;
			pdev->full_frames_tail = pdev->full_frames;
		}
		else {
			pdev->full_frames_tail->next = pdev->fill_frame;
			pdev->full_frames_tail = pdev->fill_frame;
		}
	}
	if (pdev->empty_frames != NULL) {
		/* We have empty frames available. That's easy */
		pdev->fill_frame = pdev->empty_frames;
		pdev->empty_frames = pdev->empty_frames->next;
	}
	else {
		/* Hmm. Take it from the full list */
		/* sanity check */
		if (pdev->full_frames == NULL) {
			PWC_ERROR("Neither empty or full frames available!\n");
			spin_unlock_irqrestore(&pdev->ptrlock, flags);
			return -EINVAL;
		}
		pdev->fill_frame = pdev->full_frames;
		pdev->full_frames = pdev->full_frames->next;
		ret = 1;
	}
	pdev->fill_frame->next = NULL;
	spin_unlock_irqrestore(&pdev->ptrlock, flags);
	return ret;
}


/**
  \brief Reset all buffers, pointers and lists, except for the image_used[] buffer.

  If the image_used[] buffer is cleared too, mmap()/VIDIOCSYNC will run into trouble.
 */
static void pwc_reset_buffers(struct pwc_device *pdev)
{
	int i;
	unsigned long flags;

	PWC_DEBUG_MEMORY(">> %s __enter__\n", __FUNCTION__);

	spin_lock_irqsave(&pdev->ptrlock, flags);
	pdev->full_frames = NULL;
	pdev->full_frames_tail = NULL;
	for (i = 0; i < default_fbufs; i++) {
		pdev->fbuf[i].filled = 0;
		if (i > 0)
			pdev->fbuf[i].next = &pdev->fbuf[i - 1];
		else
			pdev->fbuf->next = NULL;
	}
	pdev->empty_frames = &pdev->fbuf[default_fbufs - 1];
	pdev->empty_frames_tail = pdev->fbuf;
	pdev->read_frame = NULL;
	pdev->fill_frame = pdev->empty_frames;
	pdev->empty_frames = pdev->empty_frames->next;

	pdev->image_read_pos = 0;
	pdev->fill_image = 0;
	spin_unlock_irqrestore(&pdev->ptrlock, flags);

	PWC_DEBUG_MEMORY("<< %s __leaving__\n", __FUNCTION__);
}


/**
  \brief Do all the handling for getting one frame: get pointer, decompress, advance pointers.
 */
int pwc_handle_frame(struct pwc_device *pdev)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&pdev->ptrlock, flags);
	/* First grab our read_frame; this is removed from all lists, so
	   we can release the lock after this without problems */
	if (pdev->read_frame != NULL) {
		/* This can't theoretically happen */
		PWC_ERROR("Huh? Read frame still in use?\n");
		spin_unlock_irqrestore(&pdev->ptrlock, flags);
		return ret;
	}


	if (pdev->full_frames == NULL) {
		PWC_ERROR("Woops. No frames ready.\n");
	}
	else {
		pdev->read_frame = pdev->full_frames;
		pdev->full_frames = pdev->full_frames->next;
		pdev->read_frame->next = NULL;
	}

	if (pdev->read_frame != NULL) {
		/* Decompression is a lenghty process, so it's outside of the lock.
		   This gives the isoc_handler the opportunity to fill more frames
		   in the mean time.
		*/
		spin_unlock_irqrestore(&pdev->ptrlock, flags);
		ret = pwc_decompress(pdev);
		spin_lock_irqsave(&pdev->ptrlock, flags);

		/* We're done with read_buffer, tack it to the end of the empty buffer list */
		if (pdev->empty_frames == NULL) {
			pdev->empty_frames = pdev->read_frame;
			pdev->empty_frames_tail = pdev->empty_frames;
		}
		else {
			pdev->empty_frames_tail->next = pdev->read_frame;
			pdev->empty_frames_tail = pdev->read_frame;
		}
		pdev->read_frame = NULL;
	}
	spin_unlock_irqrestore(&pdev->ptrlock, flags);
	return ret;
}

/**
  \brief Advance pointers of image buffer (after each user request)
*/
void pwc_next_image(struct pwc_device *pdev)
{
	pdev->image_used[pdev->fill_image] = 0;
	pdev->fill_image = (pdev->fill_image + 1) % pwc_mbufs;
}

/**
 * Print debug information when a frame is discarded because all of our buffer
 * is full
 */
static void pwc_frame_dumped(struct pwc_device *pdev)
{
	pdev->vframes_dumped++;
	if (pdev->vframe_count < FRAME_LOWMARK)
		return;

	if (pdev->vframes_dumped < 20)
		PWC_DEBUG_FLOW("Dumping frame %d\n", pdev->vframe_count);
	else if (pdev->vframes_dumped == 20)
		PWC_DEBUG_FLOW("Dumping frame %d (last message)\n",
				pdev->vframe_count);
}

static int pwc_rcv_short_packet(struct pwc_device *pdev, const struct pwc_frame_buf *fbuf)
{
	int awake = 0;

	/* The ToUCam Fun CMOS sensor causes the firmware to send 2 or 3 bogus
	   frames on the USB wire after an exposure change. This conditition is
	   however detected  in the cam and a bit is set in the header.
	   */
	if (pdev->type == 730) {
		unsigned char *ptr = (unsigned char *)fbuf->data;

		if (ptr[1] == 1 && ptr[0] & 0x10) {
			PWC_TRACE("Hyundai CMOS sensor bug. Dropping frame.\n");
			pdev->drop_frames += 2;
			pdev->vframes_error++;
		}
		if ((ptr[0] ^ pdev->vmirror) & 0x01) {
			if (ptr[0] & 0x01) {
				pdev->snapshot_button_status = 1;
				PWC_TRACE("Snapshot button pressed.\n");
			}
			else {
				PWC_TRACE("Snapshot button released.\n");
			}
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
	}
	else if (pdev->type == 740 || pdev->type == 720) {
		unsigned char *ptr = (unsigned char *)fbuf->data;
		if ((ptr[0] ^ pdev->vmirror) & 0x01) {
			if (ptr[0] & 0x01) {
				pdev->snapshot_button_status = 1;
				PWC_TRACE("Snapshot button pressed.\n");
			}
			else
				PWC_TRACE("Snapshot button released.\n");
		}
		pdev->vmirror = ptr[0] & 0x03;
	}

	/* In case we were instructed to drop the frame, do so silently.
	   The buffer pointers are not updated either (but the counters are reset below).
	   */
	if (pdev->drop_frames > 0)
		pdev->drop_frames--;
	else {
		/* Check for underflow first */
		if (fbuf->filled < pdev->frame_total_size) {
			PWC_DEBUG_FLOW("Frame buffer underflow (%d bytes);"
				       " discarded.\n", fbuf->filled);
			pdev->vframes_error++;
		}
		else {
			/* Send only once per EOF */
			awake = 1; /* delay wake_ups */

			/* Find our next frame to fill. This will always succeed, since we
			 * nick a frame from either empty or full list, but if we had to
			 * take it from the full list, it means a frame got dropped.
			 */
			if (pwc_next_fill_frame(pdev))
				pwc_frame_dumped(pdev);

		}
	} /* !drop_frames */
	pdev->vframe_count++;
	return awake;
}

/* This gets called for the Isochronous pipe (video). This is done in
 * interrupt time, so it has to be fast, not crash, and not stall. Neat.
 */
static void pwc_isoc_handler(struct urb *urb)
{
	struct pwc_device *pdev;
	int i, fst, flen;
	int awake;
	struct pwc_frame_buf *fbuf;
	unsigned char *fillptr = NULL, *iso_buf = NULL;

	awake = 0;
	pdev = (struct pwc_device *)urb->context;
	if (pdev == NULL) {
		PWC_ERROR("isoc_handler() called with NULL device?!\n");
		return;
	}

	if (urb->status == -ENOENT || urb->status == -ECONNRESET) {
		PWC_DEBUG_OPEN("URB (%p) unlinked %ssynchronuously.\n", urb, urb->status == -ENOENT ? "" : "a");
		return;
	}
	if (urb->status != -EINPROGRESS && urb->status != 0) {
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
		PWC_DEBUG_FLOW("pwc_isoc_handler() called with status %d [%s].\n", urb->status, errmsg);
		/* Give up after a number of contiguous errors on the USB bus.
		   Appearantly something is wrong so we simulate an unplug event.
		 */
		if (++pdev->visoc_errors > MAX_ISOC_ERRORS)
		{
			PWC_INFO("Too many ISOC errors, bailing out.\n");
			pdev->error_status = EIO;
			awake = 1;
			wake_up_interruptible(&pdev->frameq);
		}
		goto handler_end; // ugly, but practical
	}

	fbuf = pdev->fill_frame;
	if (fbuf == NULL) {
		PWC_ERROR("pwc_isoc_handler without valid fill frame.\n");
		awake = 1;
		goto handler_end;
	}
	else {
		fillptr = fbuf->data + fbuf->filled;
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
		if (fst == 0) {
			if (flen > 0) { /* if valid data... */
				if (pdev->vsync > 0) { /* ...and we are not sync-hunting... */
					pdev->vsync = 2;

					/* ...copy data to frame buffer, if possible */
					if (flen + fbuf->filled > pdev->frame_total_size) {
						PWC_DEBUG_FLOW("Frame buffer overflow (flen = %d, frame_total_size = %d).\n", flen, pdev->frame_total_size);
						pdev->vsync = 0; /* Hmm, let's wait for an EOF (end-of-frame) */
						pdev->vframes_error++;
					}
					else {
						memmove(fillptr, iso_buf, flen);
						fillptr += flen;
					}
				}
				fbuf->filled += flen;
			} /* ..flen > 0 */

			if (flen < pdev->vlast_packet_size) {
				/* Shorter packet... We probably have the end of an image-frame;
				   wake up read() process and let select()/poll() do something.
				   Decompression is done in user time over there.
				   */
				if (pdev->vsync == 2) {
					if (pwc_rcv_short_packet(pdev, fbuf)) {
						awake = 1;
						fbuf = pdev->fill_frame;
					}
				}
				fbuf->filled = 0;
				fillptr = fbuf->data;
				pdev->vsync = 1;
			}

			pdev->vlast_packet_size = flen;
		} /* ..status == 0 */
		else {
			/* This is normally not interesting to the user, unless
			 * you are really debugging something */
			static int iso_error = 0;
			iso_error++;
			if (iso_error < 20)
				PWC_DEBUG_FLOW("Iso frame %d of USB has error %d\n", i, fst);
		}
	}

handler_end:
	if (awake)
		wake_up_interruptible(&pdev->frameq);

	urb->dev = pdev->udev;
	i = usb_submit_urb(urb, GFP_ATOMIC);
	if (i != 0)
		PWC_ERROR("Error (%d) re-submitting urb in pwc_isoc_handler.\n", i);
}


int pwc_isoc_init(struct pwc_device *pdev)
{
	struct usb_device *udev;
	struct urb *urb;
	int i, j, ret;

	struct usb_interface *intf;
	struct usb_host_interface *idesc = NULL;

	if (pdev == NULL)
		return -EFAULT;
	if (pdev->iso_init)
		return 0;
	pdev->vsync = 0;
	udev = pdev->udev;

	/* Get the current alternate interface, adjust packet size */
	if (!udev->actconfig)
		return -EFAULT;
	intf = usb_ifnum_to_if(udev, 0);
	if (intf)
		idesc = usb_altnum_to_altsetting(intf, pdev->valternate);

	if (!idesc)
		return -EFAULT;

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
	ret = 0;
	PWC_DEBUG_OPEN("Setting alternate interface %d\n", pdev->valternate);
	ret = usb_set_interface(pdev->udev, 0, pdev->valternate);
	if (ret < 0)
		return ret;

	for (i = 0; i < MAX_ISO_BUFS; i++) {
		urb = usb_alloc_urb(ISO_FRAMES_PER_DESC, GFP_KERNEL);
		if (urb == NULL) {
			PWC_ERROR("Failed to allocate urb %d\n", i);
			ret = -ENOMEM;
			break;
		}
		pdev->sbuf[i].urb = urb;
		PWC_DEBUG_MEMORY("Allocated URB at 0x%p\n", urb);
	}
	if (ret) {
		/* De-allocate in reverse order */
		while (i--) {
			usb_free_urb(pdev->sbuf[i].urb);
			pdev->sbuf[i].urb = NULL;
		}
		return ret;
	}

	/* init URB structure */
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		urb = pdev->sbuf[i].urb;

		urb->interval = 1; // devik
		urb->dev = udev;
		urb->pipe = usb_rcvisocpipe(udev, pdev->vendpoint);
		urb->transfer_flags = URB_ISO_ASAP;
		urb->transfer_buffer = pdev->sbuf[i].data;
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
		ret = usb_submit_urb(pdev->sbuf[i].urb, GFP_KERNEL);
		if (ret)
			PWC_ERROR("isoc_init() submit_urb %d failed with error %d\n", i, ret);
		else
			PWC_DEBUG_MEMORY("URB 0x%p submitted.\n", pdev->sbuf[i].urb);
	}

	/* All is done... */
	pdev->iso_init = 1;
	PWC_DEBUG_OPEN("<< pwc_isoc_init()\n");
	return 0;
}

void pwc_isoc_cleanup(struct pwc_device *pdev)
{
	int i;

	PWC_DEBUG_OPEN(">> pwc_isoc_cleanup()\n");
	if (pdev == NULL)
		return;
	if (pdev->iso_init == 0)
		return;

	/* Unlinking ISOC buffers one by one */
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		struct urb *urb;

		urb = pdev->sbuf[i].urb;
		if (urb != 0) {
			if (pdev->iso_init) {
				PWC_DEBUG_MEMORY("Unlinking URB %p\n", urb);
				usb_kill_urb(urb);
			}
			PWC_DEBUG_MEMORY("Freeing URB\n");
			usb_free_urb(urb);
			pdev->sbuf[i].urb = NULL;
		}
	}

	/* Stop camera, but only if we are sure the camera is still there (unplug
	   is signalled by EPIPE)
	 */
	if (pdev->error_status && pdev->error_status != EPIPE) {
		PWC_DEBUG_OPEN("Setting alternate interface 0.\n");
		usb_set_interface(pdev->udev, 0, 0);
	}

	pdev->iso_init = 0;
	PWC_DEBUG_OPEN("<< pwc_isoc_cleanup()\n");
}

int pwc_try_video_mode(struct pwc_device *pdev, int width, int height, int new_fps, int new_compression, int new_snapshot)
{
	int ret, start;

	/* Stop isoc stuff */
	pwc_isoc_cleanup(pdev);
	/* Reset parameters */
	pwc_reset_buffers(pdev);
	/* Try to set video mode... */
	start = ret = pwc_set_video_mode(pdev, width, height, new_fps, new_compression, new_snapshot);
	if (ret) {
		PWC_DEBUG_FLOW("pwc_set_video_mode attempt 1 failed.\n");
		/* That failed... restore old mode (we know that worked) */
		start = pwc_set_video_mode(pdev, pdev->view.x, pdev->view.y, pdev->vframes, pdev->vcompression, pdev->vsnapshot);
		if (start) {
			PWC_DEBUG_FLOW("pwc_set_video_mode attempt 2 failed.\n");
		}
	}
	if (start == 0)
	{
		if (pwc_isoc_init(pdev) < 0)
		{
			PWC_WARNING("Failed to restart ISOC transfers in pwc_try_video_mode.\n");
			ret = -EAGAIN; /* let's try again, who knows if it works a second time */
		}
	}
	pdev->drop_frames++; /* try to avoid garbage during switch */
	return ret; /* Return original error code */
}

/*********
 * sysfs
 *********/
static struct pwc_device *cd_to_pwc(struct class_device *cd)
{
	struct video_device *vdev = to_video_device(cd);
	return video_get_drvdata(vdev);
}

static ssize_t show_pan_tilt(struct class_device *class_dev, char *buf)
{
	struct pwc_device *pdev = cd_to_pwc(class_dev);
	return sprintf(buf, "%d %d\n", pdev->pan_angle, pdev->tilt_angle);
}

static ssize_t store_pan_tilt(struct class_device *class_dev, const char *buf,
			 size_t count)
{
	struct pwc_device *pdev = cd_to_pwc(class_dev);
	int pan, tilt;
	int ret = -EINVAL;

	if (strncmp(buf, "reset", 5) == 0)
		ret = pwc_mpt_reset(pdev, 0x3);

	else if (sscanf(buf, "%d %d", &pan, &tilt) > 0)
		ret = pwc_mpt_set_angle(pdev, pan, tilt);

	if (ret < 0)
		return ret;
	return strlen(buf);
}
static CLASS_DEVICE_ATTR(pan_tilt, S_IRUGO | S_IWUSR, show_pan_tilt,
			 store_pan_tilt);

static ssize_t show_snapshot_button_status(struct class_device *class_dev, char *buf)
{
	struct pwc_device *pdev = cd_to_pwc(class_dev);
	int status = pdev->snapshot_button_status;
	pdev->snapshot_button_status = 0;
	return sprintf(buf, "%d\n", status);
}

static CLASS_DEVICE_ATTR(button, S_IRUGO | S_IWUSR, show_snapshot_button_status,
			 NULL);

static int pwc_create_sysfs_files(struct video_device *vdev)
{
	struct pwc_device *pdev = video_get_drvdata(vdev);
	int rc;

	rc = video_device_create_file(vdev, &class_device_attr_button);
	if (rc)
		goto err;
	if (pdev->features & FEATURE_MOTOR_PANTILT) {
		rc = video_device_create_file(vdev,&class_device_attr_pan_tilt);
		if (rc) goto err_button;
	}

	return 0;

err_button:
	video_device_remove_file(vdev, &class_device_attr_button);
err:
	return rc;
}

static void pwc_remove_sysfs_files(struct video_device *vdev)
{
	struct pwc_device *pdev = video_get_drvdata(vdev);
	if (pdev->features & FEATURE_MOTOR_PANTILT)
		video_device_remove_file(vdev, &class_device_attr_pan_tilt);
	video_device_remove_file(vdev, &class_device_attr_button);
}

#if CONFIG_PWC_DEBUG
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

static int pwc_video_open(struct inode *inode, struct file *file)
{
	int i, ret;
	struct video_device *vdev = video_devdata(file);
	struct pwc_device *pdev;

	PWC_DEBUG_OPEN(">> video_open called(vdev = 0x%p).\n", vdev);

	pdev = (struct pwc_device *)vdev->priv;
	if (pdev == NULL)
		BUG();
	if (pdev->vopen) {
		PWC_DEBUG_OPEN("I'm busy, someone is using the device.\n");
		return -EBUSY;
	}

	down(&pdev->modlock);
	if (!pdev->usb_init) {
		PWC_DEBUG_OPEN("Doing first time initialization.\n");
		pdev->usb_init = 1;

		/* Query sensor type */
		ret = pwc_get_cmos_sensor(pdev, &i);
		if (ret >= 0)
		{
			PWC_DEBUG_OPEN("This %s camera is equipped with a %s (%d).\n",
					pdev->vdev->name,
					pwc_sensor_type_to_string(i), i);
		}
	}

	/* Turn on camera */
	if (power_save) {
		i = pwc_camera_power(pdev, 1);
		if (i < 0)
			PWC_DEBUG_OPEN("Failed to restore power to the camera! (%d)\n", i);
	}
	/* Set LED on/off time */
	if (pwc_set_leds(pdev, led_on, led_off) < 0)
		PWC_DEBUG_OPEN("Failed to set LED on/off time.\n");

	pwc_construct(pdev); /* set min/max sizes correct */

	/* So far, so good. Allocate memory. */
	i = pwc_allocate_buffers(pdev);
	if (i < 0) {
		PWC_DEBUG_OPEN("Failed to allocate buffers memory.\n");
		pwc_free_buffers(pdev);
		up(&pdev->modlock);
		return i;
	}

	/* Reset buffers & parameters */
	pwc_reset_buffers(pdev);
	for (i = 0; i < pwc_mbufs; i++)
		pdev->image_used[i] = 0;
	pdev->vframe_count = 0;
	pdev->vframes_dumped = 0;
	pdev->vframes_error = 0;
	pdev->visoc_errors = 0;
	pdev->error_status = 0;
	pwc_construct(pdev); /* set min/max sizes correct */

	/* Set some defaults */
	pdev->vsnapshot = 0;

	/* Start iso pipe for video; first try the last used video size
	   (or the default one); if that fails try QCIF/10 or QSIF/10;
	   it that fails too, give up.
	 */
	i = pwc_set_video_mode(pdev, pwc_image_sizes[pdev->vsize].x, pwc_image_sizes[pdev->vsize].y, pdev->vframes, pdev->vcompression, 0);
	if (i)	{
		unsigned int default_resolution;
		PWC_DEBUG_OPEN("First attempt at set_video_mode failed.\n");
		if (pdev->type>= 730)
			default_resolution = PSZ_QSIF;
		else
			default_resolution = PSZ_QCIF;

		i = pwc_set_video_mode(pdev,
				       pwc_image_sizes[default_resolution].x,
				       pwc_image_sizes[default_resolution].y,
				       10,
				       pdev->vcompression,
				       0);
	}
	if (i) {
		PWC_DEBUG_OPEN("Second attempt at set_video_mode failed.\n");
		pwc_free_buffers(pdev);
		up(&pdev->modlock);
		return i;
	}

	i = pwc_isoc_init(pdev);
	if (i) {
		PWC_DEBUG_OPEN("Failed to init ISOC stuff = %d.\n", i);
		pwc_isoc_cleanup(pdev);
		pwc_free_buffers(pdev);
		up(&pdev->modlock);
		return i;
	}

	/* Initialize the webcam to sane value */
	pwc_set_brightness(pdev, 0x7fff);
	pwc_set_agc(pdev, 1, 0);

	pdev->vopen++;
	file->private_data = vdev;
	up(&pdev->modlock);
	PWC_DEBUG_OPEN("<< video_open() returns 0.\n");
	return 0;
}

/* Note that all cleanup is done in the reverse order as in _open */
static int pwc_video_close(struct inode *inode, struct file *file)
{
	struct video_device *vdev = file->private_data;
	struct pwc_device *pdev;
	int i;

	PWC_DEBUG_OPEN(">> video_close called(vdev = 0x%p).\n", vdev);

	pdev = (struct pwc_device *)vdev->priv;
	if (pdev->vopen == 0)
		PWC_DEBUG_MODULE("video_close() called on closed device?\n");

	/* Dump statistics, but only if a reasonable amount of frames were
	   processed (to prevent endless log-entries in case of snap-shot
	   programs)
	 */
	if (pdev->vframe_count > 20)
		PWC_DEBUG_MODULE("Closing video device: %d frames received, dumped %d frames, %d frames with errors.\n", pdev->vframe_count, pdev->vframes_dumped, pdev->vframes_error);

	if (DEVICE_USE_CODEC1(pdev->type))
	    pwc_dec1_exit();
	else
	    pwc_dec23_exit();

	pwc_isoc_cleanup(pdev);
	pwc_free_buffers(pdev);

	/* Turn off LEDS and power down camera, but only when not unplugged */
	if (pdev->error_status != EPIPE) {
		/* Turn LEDs off */
		if (pwc_set_leds(pdev, 0, 0) < 0)
			PWC_DEBUG_MODULE("Failed to set LED on/off time.\n");
		if (power_save) {
			i = pwc_camera_power(pdev, 0);
			if (i < 0)
				PWC_ERROR("Failed to power down camera (%d)\n", i);
		}
	}
	pdev->vopen--;
	PWC_DEBUG_OPEN("<< video_close() vopen=%d\n", pdev->vopen);
	return 0;
}

/*
 *	FIXME: what about two parallel reads ????
 *      ANSWER: Not supported. You can't open the device more than once,
		despite what the V4L1 interface says. First, I don't see
		the need, second there's no mechanism of alerting the
		2nd/3rd/... process of events like changing image size.
		And I don't see the point of blocking that for the
		2nd/3rd/... process.
		In multi-threaded environments reading parallel from any
		device is tricky anyhow.
 */

static ssize_t pwc_video_read(struct file *file, char __user *buf,
			  size_t count, loff_t *ppos)
{
	struct video_device *vdev = file->private_data;
	struct pwc_device *pdev;
	int noblock = file->f_flags & O_NONBLOCK;
	DECLARE_WAITQUEUE(wait, current);
	int bytes_to_read;
	void *image_buffer_addr;

	PWC_DEBUG_READ("pwc_video_read(vdev=0x%p, buf=%p, count=%zd) called.\n",
			vdev, buf, count);
	if (vdev == NULL)
		return -EFAULT;
	pdev = vdev->priv;
	if (pdev == NULL)
		return -EFAULT;
	if (pdev->error_status)
		return -pdev->error_status; /* Something happened, report what. */

	/* In case we're doing partial reads, we don't have to wait for a frame */
	if (pdev->image_read_pos == 0) {
		/* Do wait queueing according to the (doc)book */
		add_wait_queue(&pdev->frameq, &wait);
		while (pdev->full_frames == NULL) {
			/* Check for unplugged/etc. here */
			if (pdev->error_status) {
				remove_wait_queue(&pdev->frameq, &wait);
				set_current_state(TASK_RUNNING);
				return -pdev->error_status ;
			}
			if (noblock) {
				remove_wait_queue(&pdev->frameq, &wait);
				set_current_state(TASK_RUNNING);
				return -EWOULDBLOCK;
			}
			if (signal_pending(current)) {
				remove_wait_queue(&pdev->frameq, &wait);
				set_current_state(TASK_RUNNING);
				return -ERESTARTSYS;
			}
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
		}
		remove_wait_queue(&pdev->frameq, &wait);
		set_current_state(TASK_RUNNING);

		/* Decompress and release frame */
		if (pwc_handle_frame(pdev))
			return -EFAULT;
	}

	PWC_DEBUG_READ("Copying data to user space.\n");
	if (pdev->vpalette == VIDEO_PALETTE_RAW)
		bytes_to_read = pdev->frame_size + sizeof(struct pwc_raw_frame);
	else
		bytes_to_read = pdev->view.size;

	/* copy bytes to user space; we allow for partial reads */
	if (count + pdev->image_read_pos > bytes_to_read)
		count = bytes_to_read - pdev->image_read_pos;
	image_buffer_addr = pdev->image_data;
	image_buffer_addr += pdev->images[pdev->fill_image].offset;
	image_buffer_addr += pdev->image_read_pos;
	if (copy_to_user(buf, image_buffer_addr, count))
		return -EFAULT;
	pdev->image_read_pos += count;
	if (pdev->image_read_pos >= bytes_to_read) { /* All data has been read */
		pdev->image_read_pos = 0;
		pwc_next_image(pdev);
	}
	return count;
}

static unsigned int pwc_video_poll(struct file *file, poll_table *wait)
{
	struct video_device *vdev = file->private_data;
	struct pwc_device *pdev;

	if (vdev == NULL)
		return -EFAULT;
	pdev = vdev->priv;
	if (pdev == NULL)
		return -EFAULT;

	poll_wait(file, &pdev->frameq, wait);
	if (pdev->error_status)
		return POLLERR;
	if (pdev->full_frames != NULL) /* we have frames waiting */
		return (POLLIN | POLLRDNORM);

	return 0;
}

static int pwc_video_ioctl(struct inode *inode, struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, pwc_video_do_ioctl);
}

static int pwc_video_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct video_device *vdev = file->private_data;
	struct pwc_device *pdev;
	unsigned long start;
	unsigned long size;
	unsigned long page, pos = 0;
	int index;

	PWC_DEBUG_MEMORY(">> %s\n", __FUNCTION__);
	pdev = vdev->priv;
	size = vma->vm_end - vma->vm_start;
	start = vma->vm_start;

	/* Find the idx buffer for this mapping */
	for (index = 0; index < pwc_mbufs; index++) {
		pos = pdev->images[index].offset;
		if ((pos>>PAGE_SHIFT) == vma->vm_pgoff)
			break;
	}
	if (index == MAX_IMAGES)
		return -EINVAL;
	if (index == 0) {
		/*
		 * Special case for v4l1. In v4l1, we map only one big buffer,
		 * but in v4l2 each buffer is mapped
		 */
		unsigned long total_size;
		total_size = pwc_mbufs * pdev->len_per_image;
		if (size != pdev->len_per_image && size != total_size) {
			PWC_ERROR("Wrong size (%lu) needed to be len_per_image=%d or total_size=%lu\n",
				   size, pdev->len_per_image, total_size);
			return -EINVAL;
		}
	} else if (size > pdev->len_per_image)
		return -EINVAL;

	vma->vm_flags |= VM_IO;	/* from 2.6.9-acX */

	pos += (unsigned long)pdev->image_data;
	while (size > 0) {
		page = vmalloc_to_pfn((void *)pos);
		if (remap_pfn_range(vma, start, page, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}
	return 0;
}

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
	int i, hint, rc;
	int features = 0;
	int video_nr = -1; /* default: use next available device */
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
			type_id = 720;
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
			power_save = 1;
			break;
		case 0x08b5:
			PWC_INFO("Logitech QuickCam Orbit/Sphere USB webcam detected.\n");
			name = "Logitech QuickCam Orbit";
			type_id = 740; /* CCD sensor */
			features |= FEATURE_MOTOR_PANTILT;
			break;
		case 0x08b6:
		case 0x08b7:
		case 0x08b8:
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
	pdev->vsize = default_size;
	pdev->vframes = default_fps;
	strcpy(pdev->serial, serial_number);
	pdev->features = features;
	if (vendor_id == 0x046D && product_id == 0x08B5)
	{
		/* Logitech QuickCam Orbit
		   The ranges have been determined experimentally; they may differ from cam to cam.
		   Also, the exact ranges left-right and up-down are different for my cam
		  */
		pdev->angle_range.pan_min  = -7000;
		pdev->angle_range.pan_max  =  7000;
		pdev->angle_range.tilt_min = -3000;
		pdev->angle_range.tilt_max =  2500;
	}

	init_MUTEX(&pdev->modlock);
	spin_lock_init(&pdev->ptrlock);

	pdev->udev = udev;
	init_waitqueue_head(&pdev->frameq);
	pdev->vcompression = pwc_preferred_compression;

	/* Allocate video_device structure */
	pdev->vdev = video_device_alloc();
	if (pdev->vdev == 0)
	{
		PWC_ERROR("Err, cannot allocate video_device struture. Failing probe.");
		kfree(pdev);
		return -ENOMEM;
	}
	memcpy(pdev->vdev, &pwc_template, sizeof(pwc_template));
	pdev->vdev->dev = &(udev->dev);
	strcpy(pdev->vdev->name, name);
	pdev->vdev->owner = THIS_MODULE;
	video_set_drvdata(pdev->vdev, pdev);

	pdev->release = le16_to_cpu(udev->descriptor.bcdDevice);
	PWC_DEBUG_PROBE("Release: %04x\n", pdev->release);

	/* Now search device_hint[] table for a match, so we can hint a node number. */
	for (hint = 0; hint < MAX_DEV_HINTS; hint++) {
		if (((device_hint[hint].type == -1) || (device_hint[hint].type == pdev->type)) &&
		     (device_hint[hint].pdev == NULL)) {
			/* so far, so good... try serial number */
			if ((device_hint[hint].serial_number[0] == '*') || !strcmp(device_hint[hint].serial_number, serial_number)) {
				/* match! */
				video_nr = device_hint[hint].device_node;
				PWC_DEBUG_PROBE("Found hint, will try to register as /dev/video%d\n", video_nr);
				break;
			}
		}
	}

	pdev->vdev->release = video_device_release;
	i = video_register_device(pdev->vdev, VFL_TYPE_GRABBER, video_nr);
	if (i < 0) {
		PWC_ERROR("Failed to register as video device (%d).\n", i);
		rc = i;
		goto err;
	}
	else {
		PWC_INFO("Registered as /dev/video%d.\n", pdev->vdev->minor & 0x3F);
	}

	/* occupy slot */
	if (hint < MAX_DEV_HINTS)
		device_hint[hint].pdev = pdev;

	PWC_DEBUG_PROBE("probe() function returning struct at 0x%p.\n", pdev);
	usb_set_intfdata (intf, pdev);
	rc = pwc_create_sysfs_files(pdev->vdev);
	if (rc)
		goto err_unreg;

	/* Set the leds off */
	pwc_set_leds(pdev, 0, 0);
	pwc_camera_power(pdev, 0);

	return 0;

err_unreg:
	if (hint < MAX_DEV_HINTS)
		device_hint[hint].pdev = NULL;
	video_unregister_device(pdev->vdev);
err:
	video_device_release(pdev->vdev); /* Drip... drip... drip... */
	kfree(pdev); /* Oops, no memory leaks please */
	return rc;
}

/* The user janked out the cable... */
static void usb_pwc_disconnect(struct usb_interface *intf)
{
	struct pwc_device *pdev;
	int hint;

	lock_kernel();
	pdev = usb_get_intfdata (intf);
	usb_set_intfdata (intf, NULL);
	if (pdev == NULL) {
		PWC_ERROR("pwc_disconnect() Called without private pointer.\n");
		goto disconnect_out;
	}
	if (pdev->udev == NULL) {
		PWC_ERROR("pwc_disconnect() already called for %p\n", pdev);
		goto disconnect_out;
	}
	if (pdev->udev != interface_to_usbdev(intf)) {
		PWC_ERROR("pwc_disconnect() Woops: pointer mismatch udev/pdev.\n");
		goto disconnect_out;
	}

	/* We got unplugged; this is signalled by an EPIPE error code */
	if (pdev->vopen) {
		PWC_INFO("Disconnected while webcam is in use!\n");
		pdev->error_status = EPIPE;
	}

	/* Alert waiting processes */
	wake_up_interruptible(&pdev->frameq);
	/* Wait until device is closed */
	while (pdev->vopen)
		schedule();
	/* Device is now closed, so we can safely unregister it */
	PWC_DEBUG_PROBE("Unregistering video device in disconnect().\n");
	pwc_remove_sysfs_files(pdev->vdev);
	video_unregister_device(pdev->vdev);

	/* Free memory (don't set pdev to 0 just yet) */
	kfree(pdev);

disconnect_out:
	/* search device_hint[] table if we occupy a slot, by any chance */
	for (hint = 0; hint < MAX_DEV_HINTS; hint++)
		if (device_hint[hint].pdev == pdev)
			device_hint[hint].pdev = NULL;

	unlock_kernel();
}


/* *grunt* We have to do atoi ourselves :-( */
static int pwc_atoi(const char *s)
{
	int k = 0;

	k = 0;
	while (*s != '\0' && *s >= '0' && *s <= '9') {
		k = 10 * k + (*s - '0');
		s++;
	}
	return k;
}


/*
 * Initialization code & module stuff
 */

static char *size;
static int fps;
static int fbufs;
static int mbufs;
static int compression = -1;
static int leds[2] = { -1, -1 };
static int leds_nargs;
static char *dev_hint[MAX_DEV_HINTS];
static int dev_hint_nargs;

module_param(size, charp, 0444);
module_param(fps, int, 0444);
module_param(fbufs, int, 0444);
module_param(mbufs, int, 0444);
#if CONFIG_PWC_DEBUG
module_param_named(trace, pwc_trace, int, 0644);
#endif
module_param(power_save, int, 0444);
module_param(compression, int, 0444);
module_param_array(leds, int, &leds_nargs, 0444);
module_param_array(dev_hint, charp, &dev_hint_nargs, 0444);

MODULE_PARM_DESC(size, "Initial image size. One of sqcif, qsif, qcif, sif, cif, vga");
MODULE_PARM_DESC(fps, "Initial frames per second. Varies with model, useful range 5-30");
MODULE_PARM_DESC(fbufs, "Number of internal frame buffers to reserve");
MODULE_PARM_DESC(mbufs, "Number of external (mmap()ed) image buffers");
MODULE_PARM_DESC(trace, "For debugging purposes");
MODULE_PARM_DESC(power_save, "Turn power save feature in camera on or off");
MODULE_PARM_DESC(compression, "Preferred compression quality. Range 0 (uncompressed) to 3 (high compression)");
MODULE_PARM_DESC(leds, "LED on,off time in milliseconds");
MODULE_PARM_DESC(dev_hint, "Device node hints");

MODULE_DESCRIPTION("Philips & OEM USB webcam driver");
MODULE_AUTHOR("Luc Saillard <luc@saillard.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("pwcx");
MODULE_VERSION( PWC_VERSION );

static int __init usb_pwc_init(void)
{
	int i, sz;
	char *sizenames[PSZ_MAX] = { "sqcif", "qsif", "qcif", "sif", "cif", "vga" };

	PWC_INFO("Philips webcam module version " PWC_VERSION " loaded.\n");
	PWC_INFO("Supports Philips PCA645/646, PCVC675/680/690, PCVC720[40]/730/740/750 & PCVC830/840.\n");
	PWC_INFO("Also supports the Askey VC010, various Logitech Quickcams, Samsung MPC-C10 and MPC-C30,\n");
	PWC_INFO("the Creative WebCam 5 & Pro Ex, SOTEC Afina Eye and Visionite VCS-UC300 and VCS-UM100.\n");

	if (fps) {
		if (fps < 4 || fps > 30) {
			PWC_ERROR("Framerate out of bounds (4-30).\n");
			return -EINVAL;
		}
		default_fps = fps;
		PWC_DEBUG_MODULE("Default framerate set to %d.\n", default_fps);
	}

	if (size) {
		/* string; try matching with array */
		for (sz = 0; sz < PSZ_MAX; sz++) {
			if (!strcmp(sizenames[sz], size)) { /* Found! */
				default_size = sz;
				break;
			}
		}
		if (sz == PSZ_MAX) {
			PWC_ERROR("Size not recognized; try size=[sqcif | qsif | qcif | sif | cif | vga].\n");
			return -EINVAL;
		}
		PWC_DEBUG_MODULE("Default image size set to %s [%dx%d].\n", sizenames[default_size], pwc_image_sizes[default_size].x, pwc_image_sizes[default_size].y);
	}
	if (mbufs) {
		if (mbufs < 1 || mbufs > MAX_IMAGES) {
			PWC_ERROR("Illegal number of mmap() buffers; use a number between 1 and %d.\n", MAX_IMAGES);
			return -EINVAL;
		}
		pwc_mbufs = mbufs;
		PWC_DEBUG_MODULE("Number of image buffers set to %d.\n", pwc_mbufs);
	}
	if (fbufs) {
		if (fbufs < 2 || fbufs > MAX_FRAMES) {
			PWC_ERROR("Illegal number of frame buffers; use a number between 2 and %d.\n", MAX_FRAMES);
			return -EINVAL;
		}
		default_fbufs = fbufs;
		PWC_DEBUG_MODULE("Number of frame buffers set to %d.\n", default_fbufs);
	}
#if CONFIG_PWC_DEBUG
	if (pwc_trace >= 0) {
		PWC_DEBUG_MODULE("Trace options: 0x%04x\n", pwc_trace);
	}
#endif
	if (compression >= 0) {
		if (compression > 3) {
			PWC_ERROR("Invalid compression setting; use a number between 0 (uncompressed) and 3 (high).\n");
			return -EINVAL;
		}
		pwc_preferred_compression = compression;
		PWC_DEBUG_MODULE("Preferred compression set to %d.\n", pwc_preferred_compression);
	}
	if (power_save)
		PWC_DEBUG_MODULE("Enabling power save on open/close.\n");
	if (leds[0] >= 0)
		led_on = leds[0];
	if (leds[1] >= 0)
		led_off = leds[1];

	/* Big device node whoopla. Basically, it allows you to assign a
	   device node (/dev/videoX) to a camera, based on its type
	   & serial number. The format is [type[.serialnumber]:]node.

	   Any camera that isn't matched by these rules gets the next
	   available free device node.
	 */
	for (i = 0; i < MAX_DEV_HINTS; i++) {
		char *s, *colon, *dot;

		/* This loop also initializes the array */
		device_hint[i].pdev = NULL;
		s = dev_hint[i];
		if (s != NULL && *s != '\0') {
			device_hint[i].type = -1; /* wildcard */
			strcpy(device_hint[i].serial_number, "*");

			/* parse string: chop at ':' & '/' */
			colon = dot = s;
			while (*colon != '\0' && *colon != ':')
				colon++;
			while (*dot != '\0' && *dot != '.')
				dot++;
			/* Few sanity checks */
			if (*dot != '\0' && dot > colon) {
				PWC_ERROR("Malformed camera hint: the colon must be after the dot.\n");
				return -EINVAL;
			}

			if (*colon == '\0') {
				/* No colon */
				if (*dot != '\0') {
					PWC_ERROR("Malformed camera hint: no colon + device node given.\n");
					return -EINVAL;
				}
				else {
					/* No type or serial number specified, just a number. */
					device_hint[i].device_node = pwc_atoi(s);
				}
			}
			else {
				/* There's a colon, so we have at least a type and a device node */
				device_hint[i].type = pwc_atoi(s);
				device_hint[i].device_node = pwc_atoi(colon + 1);
				if (*dot != '\0') {
					/* There's a serial number as well */
					int k;

					dot++;
					k = 0;
					while (*dot != ':' && k < 29) {
						device_hint[i].serial_number[k++] = *dot;
						dot++;
					}
					device_hint[i].serial_number[k] = '\0';
				}
			}
			PWC_TRACE("device_hint[%d]:\n", i);
			PWC_TRACE("  type    : %d\n", device_hint[i].type);
			PWC_TRACE("  serial# : %s\n", device_hint[i].serial_number);
			PWC_TRACE("  node    : %d\n", device_hint[i].device_node);
		}
		else
			device_hint[i].type = 0; /* not filled */
	} /* ..for MAX_DEV_HINTS */

	PWC_DEBUG_PROBE("Registering driver at address 0x%p.\n", &pwc_driver);
	return usb_register(&pwc_driver);
}

static void __exit usb_pwc_exit(void)
{
	PWC_DEBUG_MODULE("Deregistering driver.\n");
	usb_deregister(&pwc_driver);
	PWC_INFO("Philips webcam module removed.\n");
}

module_init(usb_pwc_init);
module_exit(usb_pwc_exit);

/* vim: set cino= formatoptions=croql cindent shiftwidth=8 tabstop=8: */
