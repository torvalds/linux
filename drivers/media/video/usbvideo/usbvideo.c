/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
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

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/spinlock.h>

#include <asm/io.h>

#include "usbvideo.h"

#if defined(MAP_NR)
#define	virt_to_page(v)	MAP_NR(v)	/* Kernels 2.2.x */
#endif

static int video_nr = -1;
module_param(video_nr, int, 0);

/*
 * Local prototypes.
 */
static void usbvideo_Disconnect(struct usb_interface *intf);
static void usbvideo_CameraRelease(struct uvd *uvd);

static int usbvideo_v4l_ioctl(struct inode *inode, struct file *file,
			      unsigned int cmd, unsigned long arg);
static int usbvideo_v4l_mmap(struct file *file, struct vm_area_struct *vma);
static int usbvideo_v4l_open(struct inode *inode, struct file *file);
static ssize_t usbvideo_v4l_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos);
static int usbvideo_v4l_close(struct inode *inode, struct file *file);

static int usbvideo_StartDataPump(struct uvd *uvd);
static void usbvideo_StopDataPump(struct uvd *uvd);
static int usbvideo_GetFrame(struct uvd *uvd, int frameNum);
static int usbvideo_NewFrame(struct uvd *uvd, int framenum);
static void usbvideo_SoftwareContrastAdjustment(struct uvd *uvd,
						struct usbvideo_frame *frame);

/*******************************/
/* Memory management functions */
/*******************************/
static void *usbvideo_rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);
	mem = vmalloc_32(size);
	if (!mem)
		return NULL;

	memset(mem, 0, size); /* Clear the ram out, no junk to the user */
	adr = (unsigned long) mem;
	while (size > 0) {
		SetPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return mem;
}

static void usbvideo_rvfree(void *mem, unsigned long size)
{
	unsigned long adr;

	if (!mem)
		return;

	adr = (unsigned long) mem;
	while ((long) size > 0) {
		ClearPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	vfree(mem);
}

static void RingQueue_Initialize(struct RingQueue *rq)
{
	assert(rq != NULL);
	init_waitqueue_head(&rq->wqh);
}

static void RingQueue_Allocate(struct RingQueue *rq, int rqLen)
{
	/* Make sure the requested size is a power of 2 and
	   round up if necessary. This allows index wrapping
	   using masks rather than modulo */

	int i = 1;
	assert(rq != NULL);
	assert(rqLen > 0);

	while(rqLen >> i)
		i++;
	if(rqLen != 1 << (i-1))
		rqLen = 1 << i;

	rq->length = rqLen;
	rq->ri = rq->wi = 0;
	rq->queue = usbvideo_rvmalloc(rq->length);
	assert(rq->queue != NULL);
}

static int RingQueue_IsAllocated(const struct RingQueue *rq)
{
	if (rq == NULL)
		return 0;
	return (rq->queue != NULL) && (rq->length > 0);
}

static void RingQueue_Free(struct RingQueue *rq)
{
	assert(rq != NULL);
	if (RingQueue_IsAllocated(rq)) {
		usbvideo_rvfree(rq->queue, rq->length);
		rq->queue = NULL;
		rq->length = 0;
	}
}

int RingQueue_Dequeue(struct RingQueue *rq, unsigned char *dst, int len)
{
	int rql, toread;

	assert(rq != NULL);
	assert(dst != NULL);

	rql = RingQueue_GetLength(rq);
	if(!rql)
		return 0;

	/* Clip requested length to available data */
	if(len > rql)
		len = rql;

	toread = len;
	if(rq->ri > rq->wi) {
		/* Read data from tail */
		int read = (toread < (rq->length - rq->ri)) ? toread : rq->length - rq->ri;
		memcpy(dst, rq->queue + rq->ri, read);
		toread -= read;
		dst += read;
		rq->ri = (rq->ri + read) & (rq->length-1);
	}
	if(toread) {
		/* Read data from head */
		memcpy(dst, rq->queue + rq->ri, toread);
		rq->ri = (rq->ri + toread) & (rq->length-1);
	}
	return len;
}

EXPORT_SYMBOL(RingQueue_Dequeue);

int RingQueue_Enqueue(struct RingQueue *rq, const unsigned char *cdata, int n)
{
	int enqueued = 0;

	assert(rq != NULL);
	assert(cdata != NULL);
	assert(rq->length > 0);
	while (n > 0) {
		int m, q_avail;

		/* Calculate the largest chunk that fits the tail of the ring */
		q_avail = rq->length - rq->wi;
		if (q_avail <= 0) {
			rq->wi = 0;
			q_avail = rq->length;
		}
		m = n;
		assert(q_avail > 0);
		if (m > q_avail)
			m = q_avail;

		memcpy(rq->queue + rq->wi, cdata, m);
		RING_QUEUE_ADVANCE_INDEX(rq, wi, m);
		cdata += m;
		enqueued += m;
		n -= m;
	}
	return enqueued;
}

EXPORT_SYMBOL(RingQueue_Enqueue);

static void RingQueue_InterruptibleSleepOn(struct RingQueue *rq)
{
	assert(rq != NULL);
	interruptible_sleep_on(&rq->wqh);
}

void RingQueue_WakeUpInterruptible(struct RingQueue *rq)
{
	assert(rq != NULL);
	if (waitqueue_active(&rq->wqh))
		wake_up_interruptible(&rq->wqh);
}

EXPORT_SYMBOL(RingQueue_WakeUpInterruptible);

void RingQueue_Flush(struct RingQueue *rq)
{
	assert(rq != NULL);
	rq->ri = 0;
	rq->wi = 0;
}

EXPORT_SYMBOL(RingQueue_Flush);


/*
 * usbvideo_VideosizeToString()
 *
 * This procedure converts given videosize value to readable string.
 *
 * History:
 * 07-Aug-2000 Created.
 * 19-Oct-2000 Reworked for usbvideo module.
 */
static void usbvideo_VideosizeToString(char *buf, int bufLen, videosize_t vs)
{
	char tmp[40];
	int n;

	n = 1 + sprintf(tmp, "%ldx%ld", VIDEOSIZE_X(vs), VIDEOSIZE_Y(vs));
	assert(n < sizeof(tmp));
	if ((buf == NULL) || (bufLen < n))
		err("usbvideo_VideosizeToString: buffer is too small.");
	else
		memmove(buf, tmp, n);
}

/*
 * usbvideo_OverlayChar()
 *
 * History:
 * 01-Feb-2000 Created.
 */
static void usbvideo_OverlayChar(struct uvd *uvd, struct usbvideo_frame *frame,
				 int x, int y, int ch)
{
	static const unsigned short digits[16] = {
		0xF6DE, /* 0 */
		0x2492, /* 1 */
		0xE7CE, /* 2 */
		0xE79E, /* 3 */
		0xB792, /* 4 */
		0xF39E, /* 5 */
		0xF3DE, /* 6 */
		0xF492, /* 7 */
		0xF7DE, /* 8 */
		0xF79E, /* 9 */
		0x77DA, /* a */
		0xD75C, /* b */
		0xF24E, /* c */
		0xD6DC, /* d */
		0xF34E, /* e */
		0xF348  /* f */
	};
	unsigned short digit;
	int ix, iy;

	if ((uvd == NULL) || (frame == NULL))
		return;

	if (ch >= '0' && ch <= '9')
		ch -= '0';
	else if (ch >= 'A' && ch <= 'F')
		ch = 10 + (ch - 'A');
	else if (ch >= 'a' && ch <= 'f')
		ch = 10 + (ch - 'a');
	else
		return;
	digit = digits[ch];

	for (iy=0; iy < 5; iy++) {
		for (ix=0; ix < 3; ix++) {
			if (digit & 0x8000) {
				if (uvd->paletteBits & (1L << VIDEO_PALETTE_RGB24)) {
/* TODO */				RGB24_PUTPIXEL(frame, x+ix, y+iy, 0xFF, 0xFF, 0xFF);
				}
			}
			digit = digit << 1;
		}
	}
}

/*
 * usbvideo_OverlayString()
 *
 * History:
 * 01-Feb-2000 Created.
 */
static void usbvideo_OverlayString(struct uvd *uvd, struct usbvideo_frame *frame,
				   int x, int y, const char *str)
{
	while (*str) {
		usbvideo_OverlayChar(uvd, frame, x, y, *str);
		str++;
		x += 4; /* 3 pixels character + 1 space */
	}
}

/*
 * usbvideo_OverlayStats()
 *
 * Overlays important debugging information.
 *
 * History:
 * 01-Feb-2000 Created.
 */
static void usbvideo_OverlayStats(struct uvd *uvd, struct usbvideo_frame *frame)
{
	const int y_diff = 8;
	char tmp[16];
	int x = 10, y=10;
	long i, j, barLength;
	const int qi_x1 = 60, qi_y1 = 10;
	const int qi_x2 = VIDEOSIZE_X(frame->request) - 10, qi_h = 10;

	/* Call the user callback, see if we may proceed after that */
	if (VALID_CALLBACK(uvd, overlayHook)) {
		if (GET_CALLBACK(uvd, overlayHook)(uvd, frame) < 0)
			return;
	}

	/*
	 * We draw a (mostly) hollow rectangle with qi_xxx coordinates.
	 * Left edge symbolizes the queue index 0; right edge symbolizes
	 * the full capacity of the queue.
	 */
	barLength = qi_x2 - qi_x1 - 2;
	if ((barLength > 10) && (uvd->paletteBits & (1L << VIDEO_PALETTE_RGB24))) {
/* TODO */	long u_lo, u_hi, q_used;
		long m_ri, m_wi, m_lo, m_hi;

		/*
		 * Determine fill zones (used areas of the queue):
		 * 0 xxxxxxx u_lo ...... uvd->dp.ri xxxxxxxx u_hi ..... uvd->dp.length
		 *
		 * if u_lo < 0 then there is no first filler.
		 */

		q_used = RingQueue_GetLength(&uvd->dp);
		if ((uvd->dp.ri + q_used) >= uvd->dp.length) {
			u_hi = uvd->dp.length;
			u_lo = (q_used + uvd->dp.ri) & (uvd->dp.length-1);
		} else {
			u_hi = (q_used + uvd->dp.ri);
			u_lo = -1;
		}

		/* Convert byte indices into screen units */
		m_ri = qi_x1 + ((barLength * uvd->dp.ri) / uvd->dp.length);
		m_wi = qi_x1 + ((barLength * uvd->dp.wi) / uvd->dp.length);
		m_lo = (u_lo > 0) ? (qi_x1 + ((barLength * u_lo) / uvd->dp.length)) : -1;
		m_hi = qi_x1 + ((barLength * u_hi) / uvd->dp.length);

		for (j=qi_y1; j < (qi_y1 + qi_h); j++) {
			for (i=qi_x1; i < qi_x2; i++) {
				/* Draw border lines */
				if ((j == qi_y1) || (j == (qi_y1 + qi_h - 1)) ||
				    (i == qi_x1) || (i == (qi_x2 - 1))) {
					RGB24_PUTPIXEL(frame, i, j, 0xFF, 0xFF, 0xFF);
					continue;
				}
				/* For all other points the Y coordinate does not matter */
				if ((i >= m_ri) && (i <= (m_ri + 3))) {
					RGB24_PUTPIXEL(frame, i, j, 0x00, 0xFF, 0x00);
				} else if ((i >= m_wi) && (i <= (m_wi + 3))) {
					RGB24_PUTPIXEL(frame, i, j, 0xFF, 0x00, 0x00);
				} else if ((i < m_lo) || ((i > m_ri) && (i < m_hi)))
					RGB24_PUTPIXEL(frame, i, j, 0x00, 0x00, 0xFF);
			}
		}
	}

	sprintf(tmp, "%8lx", uvd->stats.frame_num);
	usbvideo_OverlayString(uvd, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", uvd->stats.urb_count);
	usbvideo_OverlayString(uvd, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", uvd->stats.urb_length);
	usbvideo_OverlayString(uvd, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", uvd->stats.data_count);
	usbvideo_OverlayString(uvd, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", uvd->stats.header_count);
	usbvideo_OverlayString(uvd, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", uvd->stats.iso_skip_count);
	usbvideo_OverlayString(uvd, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8lx", uvd->stats.iso_err_count);
	usbvideo_OverlayString(uvd, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8x", uvd->vpic.colour);
	usbvideo_OverlayString(uvd, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8x", uvd->vpic.hue);
	usbvideo_OverlayString(uvd, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8x", uvd->vpic.brightness >> 8);
	usbvideo_OverlayString(uvd, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8x", uvd->vpic.contrast >> 12);
	usbvideo_OverlayString(uvd, frame, x, y, tmp);
	y += y_diff;

	sprintf(tmp, "%8d", uvd->vpic.whiteness >> 8);
	usbvideo_OverlayString(uvd, frame, x, y, tmp);
	y += y_diff;
}

/*
 * usbvideo_ReportStatistics()
 *
 * This procedure prints packet and transfer statistics.
 *
 * History:
 * 14-Jan-2000 Corrected default multiplier.
 */
static void usbvideo_ReportStatistics(const struct uvd *uvd)
{
	if ((uvd != NULL) && (uvd->stats.urb_count > 0)) {
		unsigned long allPackets, badPackets, goodPackets, percent;
		allPackets = uvd->stats.urb_count * CAMERA_URB_FRAMES;
		badPackets = uvd->stats.iso_skip_count + uvd->stats.iso_err_count;
		goodPackets = allPackets - badPackets;
		/* Calculate percentage wisely, remember integer limits */
		assert(allPackets != 0);
		if (goodPackets < (((unsigned long)-1)/100))
			percent = (100 * goodPackets) / allPackets;
		else
			percent = goodPackets / (allPackets / 100);
		info("Packet Statistics: Total=%lu. Empty=%lu. Usage=%lu%%",
		     allPackets, badPackets, percent);
		if (uvd->iso_packet_len > 0) {
			unsigned long allBytes, xferBytes;
			char multiplier = ' ';
			allBytes = allPackets * uvd->iso_packet_len;
			xferBytes = uvd->stats.data_count;
			assert(allBytes != 0);
			if (xferBytes < (((unsigned long)-1)/100))
				percent = (100 * xferBytes) / allBytes;
			else
				percent = xferBytes / (allBytes / 100);
			/* Scale xferBytes for easy reading */
			if (xferBytes > 10*1024) {
				xferBytes /= 1024;
				multiplier = 'K';
				if (xferBytes > 10*1024) {
					xferBytes /= 1024;
					multiplier = 'M';
					if (xferBytes > 10*1024) {
						xferBytes /= 1024;
						multiplier = 'G';
						if (xferBytes > 10*1024) {
							xferBytes /= 1024;
							multiplier = 'T';
						}
					}
				}
			}
			info("Transfer Statistics: Transferred=%lu%cB Usage=%lu%%",
			     xferBytes, multiplier, percent);
		}
	}
}

/*
 * usbvideo_TestPattern()
 *
 * Procedure forms a test pattern (yellow grid on blue background).
 *
 * Parameters:
 * fullframe: if TRUE then entire frame is filled, otherwise the procedure
 *	      continues from the current scanline.
 * pmode      0: fill the frame with solid blue color (like on VCR or TV)
 *	      1: Draw a colored grid
 *
 * History:
 * 01-Feb-2000 Created.
 */
void usbvideo_TestPattern(struct uvd *uvd, int fullframe, int pmode)
{
	struct usbvideo_frame *frame;
	int num_cell = 0;
	int scan_length = 0;
	static int num_pass = 0;

	if (uvd == NULL) {
		err("%s: uvd == NULL", __FUNCTION__);
		return;
	}
	if ((uvd->curframe < 0) || (uvd->curframe >= USBVIDEO_NUMFRAMES)) {
		err("%s: uvd->curframe=%d.", __FUNCTION__, uvd->curframe);
		return;
	}

	/* Grab the current frame */
	frame = &uvd->frame[uvd->curframe];

	/* Optionally start at the beginning */
	if (fullframe) {
		frame->curline = 0;
		frame->seqRead_Length = 0;
	}
#if 0
	{	/* For debugging purposes only */
		char tmp[20];
		usbvideo_VideosizeToString(tmp, sizeof(tmp), frame->request);
		info("testpattern: frame=%s", tmp);
	}
#endif
	/* Form every scan line */
	for (; frame->curline < VIDEOSIZE_Y(frame->request); frame->curline++) {
		int i;
		unsigned char *f = frame->data +
			(VIDEOSIZE_X(frame->request) * V4L_BYTES_PER_PIXEL * frame->curline);
		for (i=0; i < VIDEOSIZE_X(frame->request); i++) {
			unsigned char cb=0x80;
			unsigned char cg = 0;
			unsigned char cr = 0;

			if (pmode == 1) {
				if (frame->curline % 32 == 0)
					cb = 0, cg = cr = 0xFF;
				else if (i % 32 == 0) {
					if (frame->curline % 32 == 1)
						num_cell++;
					cb = 0, cg = cr = 0xFF;
				} else {
					cb = ((num_cell*7) + num_pass) & 0xFF;
					cg = ((num_cell*5) + num_pass*2) & 0xFF;
					cr = ((num_cell*3) + num_pass*3) & 0xFF;
				}
			} else {
				/* Just the blue screen */
			}

			*f++ = cb;
			*f++ = cg;
			*f++ = cr;
			scan_length += 3;
		}
	}

	frame->frameState = FrameState_Done;
	frame->seqRead_Length += scan_length;
	++num_pass;

	/* We do this unconditionally, regardless of FLAGS_OVERLAY_STATS */
	usbvideo_OverlayStats(uvd, frame);
}

EXPORT_SYMBOL(usbvideo_TestPattern);


#ifdef DEBUG
/*
 * usbvideo_HexDump()
 *
 * A debugging tool. Prints hex dumps.
 *
 * History:
 * 29-Jul-2000 Added printing of offsets.
 */
void usbvideo_HexDump(const unsigned char *data, int len)
{
	const int bytes_per_line = 32;
	char tmp[128]; /* 32*3 + 5 */
	int i, k;

	for (i=k=0; len > 0; i++, len--) {
		if (i > 0 && ((i % bytes_per_line) == 0)) {
			printk("%s\n", tmp);
			k=0;
		}
		if ((i % bytes_per_line) == 0)
			k += sprintf(&tmp[k], "%04x: ", i);
		k += sprintf(&tmp[k], "%02x ", data[i]);
	}
	if (k > 0)
		printk("%s\n", tmp);
}

EXPORT_SYMBOL(usbvideo_HexDump);

#endif

/* ******************************************************************** */

/* XXX: this piece of crap really wants some error handling.. */
static int usbvideo_ClientIncModCount(struct uvd *uvd)
{
	if (uvd == NULL) {
		err("%s: uvd == NULL", __FUNCTION__);
		return -EINVAL;
	}
	if (uvd->handle == NULL) {
		err("%s: uvd->handle == NULL", __FUNCTION__);
		return -EINVAL;
	}
	if (!try_module_get(uvd->handle->md_module)) {
		err("%s: try_module_get() == 0", __FUNCTION__);
		return -ENODEV;
	}
	return 0;
}

static void usbvideo_ClientDecModCount(struct uvd *uvd)
{
	if (uvd == NULL) {
		err("%s: uvd == NULL", __FUNCTION__);
		return;
	}
	if (uvd->handle == NULL) {
		err("%s: uvd->handle == NULL", __FUNCTION__);
		return;
	}
	if (uvd->handle->md_module == NULL) {
		err("%s: uvd->handle->md_module == NULL", __FUNCTION__);
		return;
	}
	module_put(uvd->handle->md_module);
}

int usbvideo_register(
	struct usbvideo **pCams,
	const int num_cams,
	const int num_extra,
	const char *driverName,
	const struct usbvideo_cb *cbTbl,
	struct module *md,
	const struct usb_device_id *id_table)
{
	struct usbvideo *cams;
	int i, base_size, result;

	/* Check parameters for sanity */
	if ((num_cams <= 0) || (pCams == NULL) || (cbTbl == NULL)) {
		err("%s: Illegal call", __FUNCTION__);
		return -EINVAL;
	}

	/* Check registration callback - must be set! */
	if (cbTbl->probe == NULL) {
		err("%s: probe() is required!", __FUNCTION__);
		return -EINVAL;
	}

	base_size = num_cams * sizeof(struct uvd) + sizeof(struct usbvideo);
	cams = kzalloc(base_size, GFP_KERNEL);
	if (cams == NULL) {
		err("Failed to allocate %d. bytes for usbvideo struct", base_size);
		return -ENOMEM;
	}
	dbg("%s: Allocated $%p (%d. bytes) for %d. cameras",
	    __FUNCTION__, cams, base_size, num_cams);

	/* Copy callbacks, apply defaults for those that are not set */
	memmove(&cams->cb, cbTbl, sizeof(cams->cb));
	if (cams->cb.getFrame == NULL)
		cams->cb.getFrame = usbvideo_GetFrame;
	if (cams->cb.disconnect == NULL)
		cams->cb.disconnect = usbvideo_Disconnect;
	if (cams->cb.startDataPump == NULL)
		cams->cb.startDataPump = usbvideo_StartDataPump;
	if (cams->cb.stopDataPump == NULL)
		cams->cb.stopDataPump = usbvideo_StopDataPump;

	cams->num_cameras = num_cams;
	cams->cam = (struct uvd *) &cams[1];
	cams->md_module = md;
	mutex_init(&cams->lock);	/* to 1 == available */

	for (i = 0; i < num_cams; i++) {
		struct uvd *up = &cams->cam[i];

		up->handle = cams;

		/* Allocate user_data separately because of kmalloc's limits */
		if (num_extra > 0) {
			up->user_size = num_cams * num_extra;
			up->user_data = kmalloc(up->user_size, GFP_KERNEL);
			if (up->user_data == NULL) {
				err("%s: Failed to allocate user_data (%d. bytes)",
				    __FUNCTION__, up->user_size);
				while (i) {
					up = &cams->cam[--i];
					kfree(up->user_data);
				}
				kfree(cams);
				return -ENOMEM;
			}
			dbg("%s: Allocated cams[%d].user_data=$%p (%d. bytes)",
			     __FUNCTION__, i, up->user_data, up->user_size);
		}
	}

	/*
	 * Register ourselves with USB stack.
	 */
	strcpy(cams->drvName, (driverName != NULL) ? driverName : "Unknown");
	cams->usbdrv.name = cams->drvName;
	cams->usbdrv.probe = cams->cb.probe;
	cams->usbdrv.disconnect = cams->cb.disconnect;
	cams->usbdrv.id_table = id_table;

	/*
	 * Update global handle to usbvideo. This is very important
	 * because probe() can be called before usb_register() returns.
	 * If the handle is not yet updated then the probe() will fail.
	 */
	*pCams = cams;
	result = usb_register(&cams->usbdrv);
	if (result) {
		for (i = 0; i < num_cams; i++) {
			struct uvd *up = &cams->cam[i];
			kfree(up->user_data);
		}
		kfree(cams);
	}

	return result;
}

EXPORT_SYMBOL(usbvideo_register);

/*
 * usbvideo_Deregister()
 *
 * Procedure frees all usbvideo and user data structures. Be warned that
 * if you had some dynamically allocated components in ->user field then
 * you should free them before calling here.
 */
void usbvideo_Deregister(struct usbvideo **pCams)
{
	struct usbvideo *cams;
	int i;

	if (pCams == NULL) {
		err("%s: pCams == NULL", __FUNCTION__);
		return;
	}
	cams = *pCams;
	if (cams == NULL) {
		err("%s: cams == NULL", __FUNCTION__);
		return;
	}

	dbg("%s: Deregistering %s driver.", __FUNCTION__, cams->drvName);
	usb_deregister(&cams->usbdrv);

	dbg("%s: Deallocating cams=$%p (%d. cameras)", __FUNCTION__, cams, cams->num_cameras);
	for (i=0; i < cams->num_cameras; i++) {
		struct uvd *up = &cams->cam[i];
		int warning = 0;

		if (up->user_data != NULL) {
			if (up->user_size <= 0)
				++warning;
		} else {
			if (up->user_size > 0)
				++warning;
		}
		if (warning) {
			err("%s: Warning: user_data=$%p user_size=%d.",
			    __FUNCTION__, up->user_data, up->user_size);
		} else {
			dbg("%s: Freeing %d. $%p->user_data=$%p",
			    __FUNCTION__, i, up, up->user_data);
			kfree(up->user_data);
		}
	}
	/* Whole array was allocated in one chunk */
	dbg("%s: Freed %d uvd structures",
	    __FUNCTION__, cams->num_cameras);
	kfree(cams);
	*pCams = NULL;
}

EXPORT_SYMBOL(usbvideo_Deregister);

/*
 * usbvideo_Disconnect()
 *
 * This procedure stops all driver activity. Deallocation of
 * the interface-private structure (pointed by 'ptr') is done now
 * (if we don't have any open files) or later, when those files
 * are closed. After that driver should be removable.
 *
 * This code handles surprise removal. The uvd->user is a counter which
 * increments on open() and decrements on close(). If we see here that
 * this counter is not 0 then we have a client who still has us opened.
 * We set uvd->remove_pending flag as early as possible, and after that
 * all access to the camera will gracefully fail. These failures should
 * prompt client to (eventually) close the video device, and then - in
 * usbvideo_v4l_close() - we decrement uvd->uvd_used and usage counter.
 *
 * History:
 * 22-Jan-2000 Added polling of MOD_IN_USE to delay removal until all users gone.
 * 27-Jan-2000 Reworked to allow pending disconnects; see xxx_close()
 * 24-May-2000 Corrected to prevent race condition (MOD_xxx_USE_COUNT).
 * 19-Oct-2000 Moved to usbvideo module.
 */
static void usbvideo_Disconnect(struct usb_interface *intf)
{
	struct uvd *uvd = usb_get_intfdata (intf);
	int i;

	if (uvd == NULL) {
		err("%s($%p): Illegal call.", __FUNCTION__, intf);
		return;
	}

	usb_set_intfdata (intf, NULL);

	usbvideo_ClientIncModCount(uvd);
	if (uvd->debug > 0)
		info("%s(%p.)", __FUNCTION__, intf);

	mutex_lock(&uvd->lock);
	uvd->remove_pending = 1; /* Now all ISO data will be ignored */

	/* At this time we ask to cancel outstanding URBs */
	GET_CALLBACK(uvd, stopDataPump)(uvd);

	for (i=0; i < USBVIDEO_NUMSBUF; i++)
		usb_free_urb(uvd->sbuf[i].urb);

	usb_put_dev(uvd->dev);
	uvd->dev = NULL;    	    /* USB device is no more */

	video_unregister_device(&uvd->vdev);
	if (uvd->debug > 0)
		info("%s: Video unregistered.", __FUNCTION__);

	if (uvd->user)
		info("%s: In use, disconnect pending.", __FUNCTION__);
	else
		usbvideo_CameraRelease(uvd);
	mutex_unlock(&uvd->lock);
	info("USB camera disconnected.");

	usbvideo_ClientDecModCount(uvd);
}

/*
 * usbvideo_CameraRelease()
 *
 * This code does final release of uvd. This happens
 * after the device is disconnected -and- all clients
 * closed their files.
 *
 * History:
 * 27-Jan-2000 Created.
 */
static void usbvideo_CameraRelease(struct uvd *uvd)
{
	if (uvd == NULL) {
		err("%s: Illegal call", __FUNCTION__);
		return;
	}

	RingQueue_Free(&uvd->dp);
	if (VALID_CALLBACK(uvd, userFree))
		GET_CALLBACK(uvd, userFree)(uvd);
	uvd->uvd_used = 0;	/* This is atomic, no need to take mutex */
}

/*
 * usbvideo_find_struct()
 *
 * This code searches the array of preallocated (static) structures
 * and returns index of the first one that isn't in use. Returns -1
 * if there are no free structures.
 *
 * History:
 * 27-Jan-2000 Created.
 */
static int usbvideo_find_struct(struct usbvideo *cams)
{
	int u, rv = -1;

	if (cams == NULL) {
		err("No usbvideo handle?");
		return -1;
	}
	mutex_lock(&cams->lock);
	for (u = 0; u < cams->num_cameras; u++) {
		struct uvd *uvd = &cams->cam[u];
		if (!uvd->uvd_used) /* This one is free */
		{
			uvd->uvd_used = 1;	/* In use now */
			mutex_init(&uvd->lock);	/* to 1 == available */
			uvd->dev = NULL;
			rv = u;
			break;
		}
	}
	mutex_unlock(&cams->lock);
	return rv;
}

static const struct file_operations usbvideo_fops = {
	.owner =  THIS_MODULE,
	.open =   usbvideo_v4l_open,
	.release =usbvideo_v4l_close,
	.read =   usbvideo_v4l_read,
	.mmap =   usbvideo_v4l_mmap,
	.ioctl =  usbvideo_v4l_ioctl,
	.compat_ioctl = v4l_compat_ioctl32,
	.llseek = no_llseek,
};
static const struct video_device usbvideo_template = {
	.owner =      THIS_MODULE,
	.type =       VID_TYPE_CAPTURE,
	.hardware =   VID_HARDWARE_CPIA,
	.fops =       &usbvideo_fops,
};

struct uvd *usbvideo_AllocateDevice(struct usbvideo *cams)
{
	int i, devnum;
	struct uvd *uvd = NULL;

	if (cams == NULL) {
		err("No usbvideo handle?");
		return NULL;
	}

	devnum = usbvideo_find_struct(cams);
	if (devnum == -1) {
		err("IBM USB camera driver: Too many devices!");
		return NULL;
	}
	uvd = &cams->cam[devnum];
	dbg("Device entry #%d. at $%p", devnum, uvd);

	/* Not relying upon caller we increase module counter ourselves */
	usbvideo_ClientIncModCount(uvd);

	mutex_lock(&uvd->lock);
	for (i=0; i < USBVIDEO_NUMSBUF; i++) {
		uvd->sbuf[i].urb = usb_alloc_urb(FRAMES_PER_DESC, GFP_KERNEL);
		if (uvd->sbuf[i].urb == NULL) {
			err("usb_alloc_urb(%d.) failed.", FRAMES_PER_DESC);
			uvd->uvd_used = 0;
			uvd = NULL;
			goto allocate_done;
		}
	}
	uvd->user=0;
	uvd->remove_pending = 0;
	uvd->last_error = 0;
	RingQueue_Initialize(&uvd->dp);

	/* Initialize video device structure */
	uvd->vdev = usbvideo_template;
	sprintf(uvd->vdev.name, "%.20s USB Camera", cams->drvName);
	/*
	 * The client is free to overwrite those because we
	 * return control to the client's probe function right now.
	 */
allocate_done:
	mutex_unlock(&uvd->lock);
	usbvideo_ClientDecModCount(uvd);
	return uvd;
}

EXPORT_SYMBOL(usbvideo_AllocateDevice);

int usbvideo_RegisterVideoDevice(struct uvd *uvd)
{
	char tmp1[20], tmp2[20];	/* Buffers for printing */

	if (uvd == NULL) {
		err("%s: Illegal call.", __FUNCTION__);
		return -EINVAL;
	}
	if (uvd->video_endp == 0) {
		info("%s: No video endpoint specified; data pump disabled.", __FUNCTION__);
	}
	if (uvd->paletteBits == 0) {
		err("%s: No palettes specified!", __FUNCTION__);
		return -EINVAL;
	}
	if (uvd->defaultPalette == 0) {
		info("%s: No default palette!", __FUNCTION__);
	}

	uvd->max_frame_size = VIDEOSIZE_X(uvd->canvas) *
		VIDEOSIZE_Y(uvd->canvas) * V4L_BYTES_PER_PIXEL;
	usbvideo_VideosizeToString(tmp1, sizeof(tmp1), uvd->videosize);
	usbvideo_VideosizeToString(tmp2, sizeof(tmp2), uvd->canvas);

	if (uvd->debug > 0) {
		info("%s: iface=%d. endpoint=$%02x paletteBits=$%08lx",
		     __FUNCTION__, uvd->iface, uvd->video_endp, uvd->paletteBits);
	}
	if (video_register_device(&uvd->vdev, VFL_TYPE_GRABBER, video_nr) == -1) {
		err("%s: video_register_device failed", __FUNCTION__);
		return -EPIPE;
	}
	if (uvd->debug > 1) {
		info("%s: video_register_device() successful", __FUNCTION__);
	}
	if (uvd->dev == NULL) {
		err("%s: uvd->dev == NULL", __FUNCTION__);
		return -EINVAL;
	}

	info("%s on /dev/video%d: canvas=%s videosize=%s",
	     (uvd->handle != NULL) ? uvd->handle->drvName : "???",
	     uvd->vdev.minor, tmp2, tmp1);

	usb_get_dev(uvd->dev);
	return 0;
}

EXPORT_SYMBOL(usbvideo_RegisterVideoDevice);

/* ******************************************************************** */

static int usbvideo_v4l_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct uvd *uvd = file->private_data;
	unsigned long start = vma->vm_start;
	unsigned long size  = vma->vm_end-vma->vm_start;
	unsigned long page, pos;

	if (!CAMERA_IS_OPERATIONAL(uvd))
		return -EFAULT;

	if (size > (((USBVIDEO_NUMFRAMES * uvd->max_frame_size) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)))
		return -EINVAL;

	pos = (unsigned long) uvd->fbuf;
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

/*
 * usbvideo_v4l_open()
 *
 * This is part of Video 4 Linux API. The driver can be opened by one
 * client only (checks internal counter 'uvdser'). The procedure
 * then allocates buffers needed for video processing.
 *
 * History:
 * 22-Jan-2000 Rewrote, moved scratch buffer allocation here. Now the
 *             camera is also initialized here (once per connect), at
 *             expense of V4L client (it waits on open() call).
 * 27-Jan-2000 Used USBVIDEO_NUMSBUF as number of URB buffers.
 * 24-May-2000 Corrected to prevent race condition (MOD_xxx_USE_COUNT).
 */
static int usbvideo_v4l_open(struct inode *inode, struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct uvd *uvd = (struct uvd *) dev;
	const int sb_size = FRAMES_PER_DESC * uvd->iso_packet_len;
	int i, errCode = 0;

	if (uvd->debug > 1)
		info("%s($%p)", __FUNCTION__, dev);

	if (0 < usbvideo_ClientIncModCount(uvd))
		return -ENODEV;
	mutex_lock(&uvd->lock);

	if (uvd->user) {
		err("%s: Someone tried to open an already opened device!", __FUNCTION__);
		errCode = -EBUSY;
	} else {
		/* Clear statistics */
		memset(&uvd->stats, 0, sizeof(uvd->stats));

		/* Clean pointers so we know if we allocated something */
		for (i=0; i < USBVIDEO_NUMSBUF; i++)
			uvd->sbuf[i].data = NULL;

		/* Allocate memory for the frame buffers */
		uvd->fbuf_size = USBVIDEO_NUMFRAMES * uvd->max_frame_size;
		uvd->fbuf = usbvideo_rvmalloc(uvd->fbuf_size);
		RingQueue_Allocate(&uvd->dp, RING_QUEUE_SIZE);
		if ((uvd->fbuf == NULL) ||
		    (!RingQueue_IsAllocated(&uvd->dp))) {
			err("%s: Failed to allocate fbuf or dp", __FUNCTION__);
			errCode = -ENOMEM;
		} else {
			/* Allocate all buffers */
			for (i=0; i < USBVIDEO_NUMFRAMES; i++) {
				uvd->frame[i].frameState = FrameState_Unused;
				uvd->frame[i].data = uvd->fbuf + i*(uvd->max_frame_size);
				/*
				 * Set default sizes in case IOCTL (VIDIOCMCAPTURE)
				 * is not used (using read() instead).
				 */
				uvd->frame[i].canvas = uvd->canvas;
				uvd->frame[i].seqRead_Index = 0;
			}
			for (i=0; i < USBVIDEO_NUMSBUF; i++) {
				uvd->sbuf[i].data = kmalloc(sb_size, GFP_KERNEL);
				if (uvd->sbuf[i].data == NULL) {
					errCode = -ENOMEM;
					break;
				}
			}
		}
		if (errCode != 0) {
			/* Have to free all that memory */
			if (uvd->fbuf != NULL) {
				usbvideo_rvfree(uvd->fbuf, uvd->fbuf_size);
				uvd->fbuf = NULL;
			}
			RingQueue_Free(&uvd->dp);
			for (i=0; i < USBVIDEO_NUMSBUF; i++) {
				kfree(uvd->sbuf[i].data);
				uvd->sbuf[i].data = NULL;
			}
		}
	}

	/* If so far no errors then we shall start the camera */
	if (errCode == 0) {
		/* Start data pump if we have valid endpoint */
		if (uvd->video_endp != 0)
			errCode = GET_CALLBACK(uvd, startDataPump)(uvd);
		if (errCode == 0) {
			if (VALID_CALLBACK(uvd, setupOnOpen)) {
				if (uvd->debug > 1)
					info("%s: setupOnOpen callback", __FUNCTION__);
				errCode = GET_CALLBACK(uvd, setupOnOpen)(uvd);
				if (errCode < 0) {
					err("%s: setupOnOpen callback failed (%d.).",
					    __FUNCTION__, errCode);
				} else if (uvd->debug > 1) {
					info("%s: setupOnOpen callback successful", __FUNCTION__);
				}
			}
			if (errCode == 0) {
				uvd->settingsAdjusted = 0;
				if (uvd->debug > 1)
					info("%s: Open succeeded.", __FUNCTION__);
				uvd->user++;
				file->private_data = uvd;
			}
		}
	}
	mutex_unlock(&uvd->lock);
	if (errCode != 0)
		usbvideo_ClientDecModCount(uvd);
	if (uvd->debug > 0)
		info("%s: Returning %d.", __FUNCTION__, errCode);
	return errCode;
}

/*
 * usbvideo_v4l_close()
 *
 * This is part of Video 4 Linux API. The procedure
 * stops streaming and deallocates all buffers that were earlier
 * allocated in usbvideo_v4l_open().
 *
 * History:
 * 22-Jan-2000 Moved scratch buffer deallocation here.
 * 27-Jan-2000 Used USBVIDEO_NUMSBUF as number of URB buffers.
 * 24-May-2000 Moved MOD_DEC_USE_COUNT outside of code that can sleep.
 */
static int usbvideo_v4l_close(struct inode *inode, struct file *file)
{
	struct video_device *dev = file->private_data;
	struct uvd *uvd = (struct uvd *) dev;
	int i;

	if (uvd->debug > 1)
		info("%s($%p)", __FUNCTION__, dev);

	mutex_lock(&uvd->lock);
	GET_CALLBACK(uvd, stopDataPump)(uvd);
	usbvideo_rvfree(uvd->fbuf, uvd->fbuf_size);
	uvd->fbuf = NULL;
	RingQueue_Free(&uvd->dp);

	for (i=0; i < USBVIDEO_NUMSBUF; i++) {
		kfree(uvd->sbuf[i].data);
		uvd->sbuf[i].data = NULL;
	}

#if USBVIDEO_REPORT_STATS
	usbvideo_ReportStatistics(uvd);
#endif

	uvd->user--;
	if (uvd->remove_pending) {
		if (uvd->debug > 0)
			info("usbvideo_v4l_close: Final disconnect.");
		usbvideo_CameraRelease(uvd);
	}
	mutex_unlock(&uvd->lock);
	usbvideo_ClientDecModCount(uvd);

	if (uvd->debug > 1)
		info("%s: Completed.", __FUNCTION__);
	file->private_data = NULL;
	return 0;
}

/*
 * usbvideo_v4l_ioctl()
 *
 * This is part of Video 4 Linux API. The procedure handles ioctl() calls.
 *
 * History:
 * 22-Jan-2000 Corrected VIDIOCSPICT to reject unsupported settings.
 */
static int usbvideo_v4l_do_ioctl(struct inode *inode, struct file *file,
				 unsigned int cmd, void *arg)
{
	struct uvd *uvd = file->private_data;

	if (!CAMERA_IS_OPERATIONAL(uvd))
		return -EIO;

	switch (cmd) {
		case VIDIOCGCAP:
		{
			struct video_capability *b = arg;
			*b = uvd->vcap;
			return 0;
		}
		case VIDIOCGCHAN:
		{
			struct video_channel *v = arg;
			*v = uvd->vchan;
			return 0;
		}
		case VIDIOCSCHAN:
		{
			struct video_channel *v = arg;
			if (v->channel != 0)
				return -EINVAL;
			return 0;
		}
		case VIDIOCGPICT:
		{
			struct video_picture *pic = arg;
			*pic = uvd->vpic;
			return 0;
		}
		case VIDIOCSPICT:
		{
			struct video_picture *pic = arg;
			/*
			 * Use temporary 'video_picture' structure to preserve our
			 * own settings (such as color depth, palette) that we
			 * aren't allowing everyone (V4L client) to change.
			 */
			uvd->vpic.brightness = pic->brightness;
			uvd->vpic.hue = pic->hue;
			uvd->vpic.colour = pic->colour;
			uvd->vpic.contrast = pic->contrast;
			uvd->settingsAdjusted = 0;	/* Will force new settings */
			return 0;
		}
		case VIDIOCSWIN:
		{
			struct video_window *vw = arg;

			if(VALID_CALLBACK(uvd, setVideoMode)) {
				return GET_CALLBACK(uvd, setVideoMode)(uvd, vw);
			}

			if (vw->flags)
				return -EINVAL;
			if (vw->clipcount)
				return -EINVAL;
			if (vw->width != VIDEOSIZE_X(uvd->canvas))
				return -EINVAL;
			if (vw->height != VIDEOSIZE_Y(uvd->canvas))
				return -EINVAL;

			return 0;
		}
		case VIDIOCGWIN:
		{
			struct video_window *vw = arg;

			vw->x = 0;
			vw->y = 0;
			vw->width = VIDEOSIZE_X(uvd->videosize);
			vw->height = VIDEOSIZE_Y(uvd->videosize);
			vw->chromakey = 0;
			if (VALID_CALLBACK(uvd, getFPS))
				vw->flags = GET_CALLBACK(uvd, getFPS)(uvd);
			else
				vw->flags = 10; /* FIXME: do better! */
			return 0;
		}
		case VIDIOCGMBUF:
		{
			struct video_mbuf *vm = arg;
			int i;

			memset(vm, 0, sizeof(*vm));
			vm->size = uvd->max_frame_size * USBVIDEO_NUMFRAMES;
			vm->frames = USBVIDEO_NUMFRAMES;
			for(i = 0; i < USBVIDEO_NUMFRAMES; i++)
			  vm->offsets[i] = i * uvd->max_frame_size;

			return 0;
		}
		case VIDIOCMCAPTURE:
		{
			struct video_mmap *vm = arg;

			if (uvd->debug >= 1) {
				info("VIDIOCMCAPTURE: frame=%d. size=%dx%d, format=%d.",
				     vm->frame, vm->width, vm->height, vm->format);
			}
			/*
			 * Check if the requested size is supported. If the requestor
			 * requests too big a frame then we may be tricked into accessing
			 * outside of own preallocated frame buffer (in uvd->frame).
			 * This will cause oops or a security hole. Theoretically, we
			 * could only clamp the size down to acceptable bounds, but then
			 * we'd need to figure out how to insert our smaller buffer into
			 * larger caller's buffer... this is not an easy question. So we
			 * here just flatly reject too large requests, assuming that the
			 * caller will resubmit with smaller size. Callers should know
			 * what size we support (returned by VIDIOCGCAP). However vidcat,
			 * for one, does not care and allows to ask for any size.
			 */
			if ((vm->width > VIDEOSIZE_X(uvd->canvas)) ||
			    (vm->height > VIDEOSIZE_Y(uvd->canvas))) {
				if (uvd->debug > 0) {
					info("VIDIOCMCAPTURE: Size=%dx%d too large; "
					     "allowed only up to %ldx%ld", vm->width, vm->height,
					     VIDEOSIZE_X(uvd->canvas), VIDEOSIZE_Y(uvd->canvas));
				}
				return -EINVAL;
			}
			/* Check if the palette is supported */
			if (((1L << vm->format) & uvd->paletteBits) == 0) {
				if (uvd->debug > 0) {
					info("VIDIOCMCAPTURE: format=%d. not supported"
					     " (paletteBits=$%08lx)",
					     vm->format, uvd->paletteBits);
				}
				return -EINVAL;
			}
			if ((vm->frame < 0) || (vm->frame >= USBVIDEO_NUMFRAMES)) {
				err("VIDIOCMCAPTURE: vm.frame=%d. !E [0-%d]", vm->frame, USBVIDEO_NUMFRAMES-1);
				return -EINVAL;
			}
			if (uvd->frame[vm->frame].frameState == FrameState_Grabbing) {
				/* Not an error - can happen */
			}
			uvd->frame[vm->frame].request = VIDEOSIZE(vm->width, vm->height);
			uvd->frame[vm->frame].palette = vm->format;

			/* Mark it as ready */
			uvd->frame[vm->frame].frameState = FrameState_Ready;

			return usbvideo_NewFrame(uvd, vm->frame);
		}
		case VIDIOCSYNC:
		{
			int *frameNum = arg;
			int ret;

			if (*frameNum < 0 || *frameNum >= USBVIDEO_NUMFRAMES)
				return -EINVAL;

			if (uvd->debug >= 1)
				info("VIDIOCSYNC: syncing to frame %d.", *frameNum);
			if (uvd->flags & FLAGS_NO_DECODING)
				ret = usbvideo_GetFrame(uvd, *frameNum);
			else if (VALID_CALLBACK(uvd, getFrame)) {
				ret = GET_CALLBACK(uvd, getFrame)(uvd, *frameNum);
				if ((ret < 0) && (uvd->debug >= 1)) {
					err("VIDIOCSYNC: getFrame() returned %d.", ret);
				}
			} else {
				err("VIDIOCSYNC: getFrame is not set");
				ret = -EFAULT;
			}

			/*
			 * The frame is in FrameState_Done_Hold state. Release it
			 * right now because its data is already mapped into
			 * the user space and it's up to the application to
			 * make use of it until it asks for another frame.
			 */
			uvd->frame[*frameNum].frameState = FrameState_Unused;
			return ret;
		}
		case VIDIOCGFBUF:
		{
			struct video_buffer *vb = arg;

			memset(vb, 0, sizeof(*vb));
			return 0;
		}
		case VIDIOCKEY:
			return 0;

		case VIDIOCCAPTURE:
			return -EINVAL;

		case VIDIOCSFBUF:

		case VIDIOCGTUNER:
		case VIDIOCSTUNER:

		case VIDIOCGFREQ:
		case VIDIOCSFREQ:

		case VIDIOCGAUDIO:
		case VIDIOCSAUDIO:
			return -EINVAL;

		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

static int usbvideo_v4l_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, usbvideo_v4l_do_ioctl);
}

/*
 * usbvideo_v4l_read()
 *
 * This is mostly boring stuff. We simply ask for a frame and when it
 * arrives copy all the video data from it into user space. There is
 * no obvious need to override this method.
 *
 * History:
 * 20-Oct-2000 Created.
 * 01-Nov-2000 Added mutex (uvd->lock).
 */
static ssize_t usbvideo_v4l_read(struct file *file, char __user *buf,
		      size_t count, loff_t *ppos)
{
	struct uvd *uvd = file->private_data;
	int noblock = file->f_flags & O_NONBLOCK;
	int frmx = -1, i;
	struct usbvideo_frame *frame;

	if (!CAMERA_IS_OPERATIONAL(uvd) || (buf == NULL))
		return -EFAULT;

	if (uvd->debug >= 1)
		info("%s: %Zd. bytes, noblock=%d.", __FUNCTION__, count, noblock);

	mutex_lock(&uvd->lock);

	/* See if a frame is completed, then use it. */
	for(i = 0; i < USBVIDEO_NUMFRAMES; i++) {
		if ((uvd->frame[i].frameState == FrameState_Done) ||
		    (uvd->frame[i].frameState == FrameState_Done_Hold) ||
		    (uvd->frame[i].frameState == FrameState_Error)) {
			frmx = i;
			break;
		}
	}

	/* FIXME: If we don't start a frame here then who ever does? */
	if (noblock && (frmx == -1)) {
		count = -EAGAIN;
		goto read_done;
	}

	/*
	 * If no FrameState_Done, look for a FrameState_Grabbing state.
	 * See if a frame is in process (grabbing), then use it.
	 * We will need to wait until it becomes cooked, of course.
	 */
	if (frmx == -1) {
		for(i = 0; i < USBVIDEO_NUMFRAMES; i++) {
			if (uvd->frame[i].frameState == FrameState_Grabbing) {
				frmx = i;
				break;
			}
		}
	}

	/*
	 * If no frame is active, start one. We don't care which one
	 * it will be, so #0 is as good as any.
	 * In read access mode we don't have convenience of VIDIOCMCAPTURE
	 * to specify the requested palette (video format) on per-frame
	 * basis. This means that we have to return data in -some- format
	 * and just hope that the client knows what to do with it.
	 * The default format is configured in uvd->defaultPalette field
	 * as one of VIDEO_PALETTE_xxx values. We stuff it into the new
	 * frame and initiate the frame filling process.
	 */
	if (frmx == -1) {
		if (uvd->defaultPalette == 0) {
			err("%s: No default palette; don't know what to do!", __FUNCTION__);
			count = -EFAULT;
			goto read_done;
		}
		frmx = 0;
		/*
		 * We have no per-frame control over video size.
		 * Therefore we only can use whatever size was
		 * specified as default.
		 */
		uvd->frame[frmx].request = uvd->videosize;
		uvd->frame[frmx].palette = uvd->defaultPalette;
		uvd->frame[frmx].frameState = FrameState_Ready;
		usbvideo_NewFrame(uvd, frmx);
		/* Now frame 0 is supposed to start filling... */
	}

	/*
	 * Get a pointer to the active frame. It is either previously
	 * completed frame or frame in progress but not completed yet.
	 */
	frame = &uvd->frame[frmx];

	/*
	 * Sit back & wait until the frame gets filled and postprocessed.
	 * If we fail to get the picture [in time] then return the error.
	 * In this call we specify that we want the frame to be waited for,
	 * postprocessed and switched into FrameState_Done_Hold state. This
	 * state is used to hold the frame as "fully completed" between
	 * subsequent partial reads of the same frame.
	 */
	if (frame->frameState != FrameState_Done_Hold) {
		long rv = -EFAULT;
		if (uvd->flags & FLAGS_NO_DECODING)
			rv = usbvideo_GetFrame(uvd, frmx);
		else if (VALID_CALLBACK(uvd, getFrame))
			rv = GET_CALLBACK(uvd, getFrame)(uvd, frmx);
		else
			err("getFrame is not set");
		if ((rv != 0) || (frame->frameState != FrameState_Done_Hold)) {
			count = rv;
			goto read_done;
		}
	}

	/*
	 * Copy bytes to user space. We allow for partial reads, which
	 * means that the user application can request read less than
	 * the full frame size. It is up to the application to issue
	 * subsequent calls until entire frame is read.
	 *
	 * First things first, make sure we don't copy more than we
	 * have - even if the application wants more. That would be
	 * a big security embarassment!
	 */
	if ((count + frame->seqRead_Index) > frame->seqRead_Length)
		count = frame->seqRead_Length - frame->seqRead_Index;

	/*
	 * Copy requested amount of data to user space. We start
	 * copying from the position where we last left it, which
	 * will be zero for a new frame (not read before).
	 */
	if (copy_to_user(buf, frame->data + frame->seqRead_Index, count)) {
		count = -EFAULT;
		goto read_done;
	}

	/* Update last read position */
	frame->seqRead_Index += count;
	if (uvd->debug >= 1) {
		err("%s: {copy} count used=%Zd, new seqRead_Index=%ld",
			__FUNCTION__, count, frame->seqRead_Index);
	}

	/* Finally check if the frame is done with and "release" it */
	if (frame->seqRead_Index >= frame->seqRead_Length) {
		/* All data has been read */
		frame->seqRead_Index = 0;

		/* Mark it as available to be used again. */
		uvd->frame[frmx].frameState = FrameState_Unused;
		if (usbvideo_NewFrame(uvd, (frmx + 1) % USBVIDEO_NUMFRAMES)) {
			err("%s: usbvideo_NewFrame failed.", __FUNCTION__);
		}
	}
read_done:
	mutex_unlock(&uvd->lock);
	return count;
}

/*
 * Make all of the blocks of data contiguous
 */
static int usbvideo_CompressIsochronous(struct uvd *uvd, struct urb *urb)
{
	char *cdata;
	int i, totlen = 0;

	for (i = 0; i < urb->number_of_packets; i++) {
		int n = urb->iso_frame_desc[i].actual_length;
		int st = urb->iso_frame_desc[i].status;

		cdata = urb->transfer_buffer + urb->iso_frame_desc[i].offset;

		/* Detect and ignore errored packets */
		if (st < 0) {
			if (uvd->debug >= 1)
				err("Data error: packet=%d. len=%d. status=%d.", i, n, st);
			uvd->stats.iso_err_count++;
			continue;
		}

		/* Detect and ignore empty packets */
		if (n <= 0) {
			uvd->stats.iso_skip_count++;
			continue;
		}
		totlen += n;	/* Little local accounting */
		RingQueue_Enqueue(&uvd->dp, cdata, n);
	}
	return totlen;
}

static void usbvideo_IsocIrq(struct urb *urb)
{
	int i, ret, len;
	struct uvd *uvd = urb->context;

	/* We don't want to do anything if we are about to be removed! */
	if (!CAMERA_IS_OPERATIONAL(uvd))
		return;
#if 0
	if (urb->actual_length > 0) {
		info("urb=$%p status=%d. errcount=%d. length=%d.",
		     urb, urb->status, urb->error_count, urb->actual_length);
	} else {
		static int c = 0;
		if (c++ % 100 == 0)
			info("No Isoc data");
	}
#endif

	if (!uvd->streaming) {
		if (uvd->debug >= 1)
			info("Not streaming, but interrupt!");
		return;
	}

	uvd->stats.urb_count++;
	if (urb->actual_length <= 0)
		goto urb_done_with;

	/* Copy the data received into ring queue */
	len = usbvideo_CompressIsochronous(uvd, urb);
	uvd->stats.urb_length = len;
	if (len <= 0)
		goto urb_done_with;

	/* Here we got some data */
	uvd->stats.data_count += len;
	RingQueue_WakeUpInterruptible(&uvd->dp);

urb_done_with:
	for (i = 0; i < FRAMES_PER_DESC; i++) {
		urb->iso_frame_desc[i].status = 0;
		urb->iso_frame_desc[i].actual_length = 0;
	}
	urb->status = 0;
	urb->dev = uvd->dev;
	ret = usb_submit_urb (urb, GFP_KERNEL);
	if(ret)
		err("usb_submit_urb error (%d)", ret);
	return;
}

/*
 * usbvideo_StartDataPump()
 *
 * History:
 * 27-Jan-2000 Used ibmcam->iface, ibmcam->ifaceAltActive instead
 *             of hardcoded values. Simplified by using for loop,
 *             allowed any number of URBs.
 */
static int usbvideo_StartDataPump(struct uvd *uvd)
{
	struct usb_device *dev = uvd->dev;
	int i, errFlag;

	if (uvd->debug > 1)
		info("%s($%p)", __FUNCTION__, uvd);

	if (!CAMERA_IS_OPERATIONAL(uvd)) {
		err("%s: Camera is not operational", __FUNCTION__);
		return -EFAULT;
	}
	uvd->curframe = -1;

	/* Alternate interface 1 is is the biggest frame size */
	i = usb_set_interface(dev, uvd->iface, uvd->ifaceAltActive);
	if (i < 0) {
		err("%s: usb_set_interface error", __FUNCTION__);
		uvd->last_error = i;
		return -EBUSY;
	}
	if (VALID_CALLBACK(uvd, videoStart))
		GET_CALLBACK(uvd, videoStart)(uvd);
	else
		err("%s: videoStart not set", __FUNCTION__);

	/* We double buffer the Iso lists */
	for (i=0; i < USBVIDEO_NUMSBUF; i++) {
		int j, k;
		struct urb *urb = uvd->sbuf[i].urb;
		urb->dev = dev;
		urb->context = uvd;
		urb->pipe = usb_rcvisocpipe(dev, uvd->video_endp);
		urb->interval = 1;
		urb->transfer_flags = URB_ISO_ASAP;
		urb->transfer_buffer = uvd->sbuf[i].data;
		urb->complete = usbvideo_IsocIrq;
		urb->number_of_packets = FRAMES_PER_DESC;
		urb->transfer_buffer_length = uvd->iso_packet_len * FRAMES_PER_DESC;
		for (j=k=0; j < FRAMES_PER_DESC; j++, k += uvd->iso_packet_len) {
			urb->iso_frame_desc[j].offset = k;
			urb->iso_frame_desc[j].length = uvd->iso_packet_len;
		}
	}

	/* Submit all URBs */
	for (i=0; i < USBVIDEO_NUMSBUF; i++) {
		errFlag = usb_submit_urb(uvd->sbuf[i].urb, GFP_KERNEL);
		if (errFlag)
			err("%s: usb_submit_isoc(%d) ret %d", __FUNCTION__, i, errFlag);
	}

	uvd->streaming = 1;
	if (uvd->debug > 1)
		info("%s: streaming=1 video_endp=$%02x", __FUNCTION__, uvd->video_endp);
	return 0;
}

/*
 * usbvideo_StopDataPump()
 *
 * This procedure stops streaming and deallocates URBs. Then it
 * activates zero-bandwidth alt. setting of the video interface.
 *
 * History:
 * 22-Jan-2000 Corrected order of actions to work after surprise removal.
 * 27-Jan-2000 Used uvd->iface, uvd->ifaceAltInactive instead of hardcoded values.
 */
static void usbvideo_StopDataPump(struct uvd *uvd)
{
	int i, j;

	if ((uvd == NULL) || (!uvd->streaming) || (uvd->dev == NULL))
		return;

	if (uvd->debug > 1)
		info("%s($%p)", __FUNCTION__, uvd);

	/* Unschedule all of the iso td's */
	for (i=0; i < USBVIDEO_NUMSBUF; i++) {
		usb_kill_urb(uvd->sbuf[i].urb);
	}
	if (uvd->debug > 1)
		info("%s: streaming=0", __FUNCTION__);
	uvd->streaming = 0;

	if (!uvd->remove_pending) {
		/* Invoke minidriver's magic to stop the camera */
		if (VALID_CALLBACK(uvd, videoStop))
			GET_CALLBACK(uvd, videoStop)(uvd);
		else
			err("%s: videoStop not set", __FUNCTION__);

		/* Set packet size to 0 */
		j = usb_set_interface(uvd->dev, uvd->iface, uvd->ifaceAltInactive);
		if (j < 0) {
			err("%s: usb_set_interface() error %d.", __FUNCTION__, j);
			uvd->last_error = j;
		}
	}
}

/*
 * usbvideo_NewFrame()
 *
 * History:
 * 29-Mar-00 Added copying of previous frame into the current one.
 * 6-Aug-00  Added model 3 video sizes, removed redundant width, height.
 */
static int usbvideo_NewFrame(struct uvd *uvd, int framenum)
{
	struct usbvideo_frame *frame;
	int n;

	if (uvd->debug > 1)
		info("usbvideo_NewFrame($%p,%d.)", uvd, framenum);

	/* If we're not grabbing a frame right now and the other frame is */
	/*  ready to be grabbed into, then use it instead */
	if (uvd->curframe != -1)
		return 0;

	/* If necessary we adjust picture settings between frames */
	if (!uvd->settingsAdjusted) {
		if (VALID_CALLBACK(uvd, adjustPicture))
			GET_CALLBACK(uvd, adjustPicture)(uvd);
		uvd->settingsAdjusted = 1;
	}

	n = (framenum + 1) % USBVIDEO_NUMFRAMES;
	if (uvd->frame[n].frameState == FrameState_Ready)
		framenum = n;

	frame = &uvd->frame[framenum];

	frame->frameState = FrameState_Grabbing;
	frame->scanstate = ScanState_Scanning;
	frame->seqRead_Length = 0;	/* Accumulated in xxx_parse_data() */
	frame->deinterlace = Deinterlace_None;
	frame->flags = 0; /* No flags yet, up to minidriver (or us) to set them */
	uvd->curframe = framenum;

	/*
	 * Normally we would want to copy previous frame into the current one
	 * before we even start filling it with data; this allows us to stop
	 * filling at any moment; top portion of the frame will be new and
	 * bottom portion will stay as it was in previous frame. If we don't
	 * do that then missing chunks of video stream will result in flickering
	 * portions of old data whatever it was before.
	 *
	 * If we choose not to copy previous frame (to, for example, save few
	 * bus cycles - the frame can be pretty large!) then we have an option
	 * to clear the frame before using. If we experience losses in this
	 * mode then missing picture will be black (no flickering).
	 *
	 * Finally, if user chooses not to clean the current frame before
	 * filling it with data then the old data will be visible if we fail
	 * to refill entire frame with new data.
	 */
	if (!(uvd->flags & FLAGS_SEPARATE_FRAMES)) {
		/* This copies previous frame into this one to mask losses */
		int prev = (framenum - 1 + USBVIDEO_NUMFRAMES) % USBVIDEO_NUMFRAMES;
		memmove(frame->data, uvd->frame[prev].data, uvd->max_frame_size);
	} else {
		if (uvd->flags & FLAGS_CLEAN_FRAMES) {
			/* This provides a "clean" frame but slows things down */
			memset(frame->data, 0, uvd->max_frame_size);
		}
	}
	return 0;
}

/*
 * usbvideo_CollectRawData()
 *
 * This procedure can be used instead of 'processData' callback if you
 * only want to dump the raw data from the camera into the output
 * device (frame buffer). You can look at it with V4L client, but the
 * image will be unwatchable. The main purpose of this code and of the
 * mode FLAGS_NO_DECODING is debugging and capturing of datastreams from
 * new, unknown cameras. This procedure will be automatically invoked
 * instead of the specified callback handler when uvd->flags has bit
 * FLAGS_NO_DECODING set. Therefore, any regular build of any driver
 * based on usbvideo can use this feature at any time.
 */
static void usbvideo_CollectRawData(struct uvd *uvd, struct usbvideo_frame *frame)
{
	int n;

	assert(uvd != NULL);
	assert(frame != NULL);

	/* Try to move data from queue into frame buffer */
	n = RingQueue_GetLength(&uvd->dp);
	if (n > 0) {
		int m;
		/* See how much space we have left */
		m = uvd->max_frame_size - frame->seqRead_Length;
		if (n > m)
			n = m;
		/* Now move that much data into frame buffer */
		RingQueue_Dequeue(
			&uvd->dp,
			frame->data + frame->seqRead_Length,
			m);
		frame->seqRead_Length += m;
	}
	/* See if we filled the frame */
	if (frame->seqRead_Length >= uvd->max_frame_size) {
		frame->frameState = FrameState_Done;
		uvd->curframe = -1;
		uvd->stats.frame_num++;
	}
}

static int usbvideo_GetFrame(struct uvd *uvd, int frameNum)
{
	struct usbvideo_frame *frame = &uvd->frame[frameNum];

	if (uvd->debug >= 2)
		info("%s($%p,%d.)", __FUNCTION__, uvd, frameNum);

	switch (frame->frameState) {
	case FrameState_Unused:
		if (uvd->debug >= 2)
			info("%s: FrameState_Unused", __FUNCTION__);
		return -EINVAL;
	case FrameState_Ready:
	case FrameState_Grabbing:
	case FrameState_Error:
	{
		int ntries, signalPending;
	redo:
		if (!CAMERA_IS_OPERATIONAL(uvd)) {
			if (uvd->debug >= 2)
				info("%s: Camera is not operational (1)", __FUNCTION__);
			return -EIO;
		}
		ntries = 0;
		do {
			RingQueue_InterruptibleSleepOn(&uvd->dp);
			signalPending = signal_pending(current);
			if (!CAMERA_IS_OPERATIONAL(uvd)) {
				if (uvd->debug >= 2)
					info("%s: Camera is not operational (2)", __FUNCTION__);
				return -EIO;
			}
			assert(uvd->fbuf != NULL);
			if (signalPending) {
				if (uvd->debug >= 2)
					info("%s: Signal=$%08x", __FUNCTION__, signalPending);
				if (uvd->flags & FLAGS_RETRY_VIDIOCSYNC) {
					usbvideo_TestPattern(uvd, 1, 0);
					uvd->curframe = -1;
					uvd->stats.frame_num++;
					if (uvd->debug >= 2)
						info("%s: Forced test pattern screen", __FUNCTION__);
					return 0;
				} else {
					/* Standard answer: Interrupted! */
					if (uvd->debug >= 2)
						info("%s: Interrupted!", __FUNCTION__);
					return -EINTR;
				}
			} else {
				/* No signals - we just got new data in dp queue */
				if (uvd->flags & FLAGS_NO_DECODING)
					usbvideo_CollectRawData(uvd, frame);
				else if (VALID_CALLBACK(uvd, processData))
					GET_CALLBACK(uvd, processData)(uvd, frame);
				else
					err("%s: processData not set", __FUNCTION__);
			}
		} while (frame->frameState == FrameState_Grabbing);
		if (uvd->debug >= 2) {
			info("%s: Grabbing done; state=%d. (%lu. bytes)",
			     __FUNCTION__, frame->frameState, frame->seqRead_Length);
		}
		if (frame->frameState == FrameState_Error) {
			int ret = usbvideo_NewFrame(uvd, frameNum);
			if (ret < 0) {
				err("%s: usbvideo_NewFrame() failed (%d.)", __FUNCTION__, ret);
				return ret;
			}
			goto redo;
		}
		/* Note that we fall through to meet our destiny below */
	}
	case FrameState_Done:
		/*
		 * Do all necessary postprocessing of data prepared in
		 * "interrupt" code and the collecting code above. The
		 * frame gets marked as FrameState_Done by queue parsing code.
		 * This status means that we collected enough data and
		 * most likely processed it as we went through. However
		 * the data may need postprocessing, such as deinterlacing
		 * or picture adjustments implemented in software (horror!)
		 *
		 * As soon as the frame becomes "final" it gets promoted to
		 * FrameState_Done_Hold status where it will remain until the
		 * caller consumed all the video data from the frame. Then
		 * the empty shell of ex-frame is thrown out for dogs to eat.
		 * But we, worried about pets, will recycle the frame!
		 */
		uvd->stats.frame_num++;
		if ((uvd->flags & FLAGS_NO_DECODING) == 0) {
			if (VALID_CALLBACK(uvd, postProcess))
				GET_CALLBACK(uvd, postProcess)(uvd, frame);
			if (frame->flags & USBVIDEO_FRAME_FLAG_SOFTWARE_CONTRAST)
				usbvideo_SoftwareContrastAdjustment(uvd, frame);
		}
		frame->frameState = FrameState_Done_Hold;
		if (uvd->debug >= 2)
			info("%s: Entered FrameState_Done_Hold state.", __FUNCTION__);
		return 0;

	case FrameState_Done_Hold:
		/*
		 * We stay in this state indefinitely until someone external,
		 * like ioctl() or read() call finishes digesting the frame
		 * data. Then it will mark the frame as FrameState_Unused and
		 * it will be released back into the wild to roam freely.
		 */
		if (uvd->debug >= 2)
			info("%s: FrameState_Done_Hold state.", __FUNCTION__);
		return 0;
	}

	/* Catch-all for other cases. We shall not be here. */
	err("%s: Invalid state %d.", __FUNCTION__, frame->frameState);
	frame->frameState = FrameState_Unused;
	return 0;
}

/*
 * usbvideo_DeinterlaceFrame()
 *
 * This procedure deinterlaces the given frame. Some cameras produce
 * only half of scanlines - sometimes only even lines, sometimes only
 * odd lines. The deinterlacing method is stored in frame->deinterlace
 * variable.
 *
 * Here we scan the frame vertically and replace missing scanlines with
 * average between surrounding ones - before and after. If we have no
 * line above then we just copy next line. Similarly, if we need to
 * create a last line then preceding line is used.
 */
void usbvideo_DeinterlaceFrame(struct uvd *uvd, struct usbvideo_frame *frame)
{
	if ((uvd == NULL) || (frame == NULL))
		return;

	if ((frame->deinterlace == Deinterlace_FillEvenLines) ||
	    (frame->deinterlace == Deinterlace_FillOddLines))
	{
		const int v4l_linesize = VIDEOSIZE_X(frame->request) * V4L_BYTES_PER_PIXEL;
		int i = (frame->deinterlace == Deinterlace_FillEvenLines) ? 0 : 1;

		for (; i < VIDEOSIZE_Y(frame->request); i += 2) {
			const unsigned char *fs1, *fs2;
			unsigned char *fd;
			int ip, in, j;	/* Previous and next lines */

			/*
			 * Need to average lines before and after 'i'.
			 * If we go out of bounds seeking those lines then
			 * we point back to existing line.
			 */
			ip = i - 1;	/* First, get rough numbers */
			in = i + 1;

			/* Now validate */
			if (ip < 0)
				ip = in;
			if (in >= VIDEOSIZE_Y(frame->request))
				in = ip;

			/* Sanity check */
			if ((ip < 0) || (in < 0) ||
			    (ip >= VIDEOSIZE_Y(frame->request)) ||
			    (in >= VIDEOSIZE_Y(frame->request)))
			{
				err("Error: ip=%d. in=%d. req.height=%ld.",
				    ip, in, VIDEOSIZE_Y(frame->request));
				break;
			}

			/* Now we need to average lines 'ip' and 'in' to produce line 'i' */
			fs1 = frame->data + (v4l_linesize * ip);
			fs2 = frame->data + (v4l_linesize * in);
			fd = frame->data + (v4l_linesize * i);

			/* Average lines around destination */
			for (j=0; j < v4l_linesize; j++) {
				fd[j] = (unsigned char)((((unsigned) fs1[j]) +
							 ((unsigned)fs2[j])) >> 1);
			}
		}
	}

	/* Optionally display statistics on the screen */
	if (uvd->flags & FLAGS_OVERLAY_STATS)
		usbvideo_OverlayStats(uvd, frame);
}

EXPORT_SYMBOL(usbvideo_DeinterlaceFrame);

/*
 * usbvideo_SoftwareContrastAdjustment()
 *
 * This code adjusts the contrast of the frame, assuming RGB24 format.
 * As most software image processing, this job is CPU-intensive.
 * Get a camera that supports hardware adjustment!
 *
 * History:
 * 09-Feb-2001  Created.
 */
static void usbvideo_SoftwareContrastAdjustment(struct uvd *uvd,
						struct usbvideo_frame *frame)
{
	int i, j, v4l_linesize;
	signed long adj;
	const int ccm = 128; /* Color correction median - see below */

	if ((uvd == NULL) || (frame == NULL)) {
		err("%s: Illegal call.", __FUNCTION__);
		return;
	}
	adj = (uvd->vpic.contrast - 0x8000) >> 8; /* -128..+127 = -ccm..+(ccm-1)*/
	RESTRICT_TO_RANGE(adj, -ccm, ccm+1);
	if (adj == 0) {
		/* In rare case of no adjustment */
		return;
	}
	v4l_linesize = VIDEOSIZE_X(frame->request) * V4L_BYTES_PER_PIXEL;
	for (i=0; i < VIDEOSIZE_Y(frame->request); i++) {
		unsigned char *fd = frame->data + (v4l_linesize * i);
		for (j=0; j < v4l_linesize; j++) {
			signed long v = (signed long) fd[j];
			/* Magnify up to 2 times, reduce down to zero */
			v = 128 + ((ccm + adj) * (v - 128)) / ccm;
			RESTRICT_TO_RANGE(v, 0, 0xFF); /* Must flatten tails */
			fd[j] = (unsigned char) v;
		}
	}
}

MODULE_LICENSE("GPL");
