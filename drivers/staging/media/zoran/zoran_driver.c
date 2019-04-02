/*
 * Zoran zr36057/zr36067 PCI controller driver, for the
 * Pinnacle/Miro DC10/DC10+/DC30/DC30+, Iomega Buz, Linux
 * Media Labs LML33/LML33R10.
 *
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 *
 * Changes for BUZ by Wolfgang Scherr <scherr@net4you.net>
 *
 * Changes for DC10/DC30 by Laurent Pinchart <laurent.pinchart@skynet.be>
 *
 * Changes for LML33R10 by Maxim Yevtyushkin <max@linuxmedialabs.com>
 *
 * Changes for videodev2/v4l2 by Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * Based on
 *
 * Miro DC10 driver
 * Copyright (C) 1999 Wolfgang Scherr <scherr@net4you.net>
 *
 * Iomega Buz driver version 1.0
 * Copyright (C) 1999 Rainer Johanni <Rainer@Johanni.de>
 *
 * buz.0.0.3
 * Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 *
 * bttv - Bt848 frame grabber driver
 * Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
 *                        & Marcus Metzler (mocm@thp.uni-koeln.de)
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

#include <linux/spinlock.h>

#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include "videocodec.h"

#include <asm/byteorder.h>
#include <asm/io.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>

#include <linux/mutex.h>
#include "zoran.h"
#include "zoran_device.h"
#include "zoran_card.h"


const struct zoran_format zoran_formats[] = {
	{
		.name = "15-bit RGB LE",
		.fourcc = V4L2_PIX_FMT_RGB555,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.depth = 15,
		.flags = ZORAN_FORMAT_CAPTURE |
			 ZORAN_FORMAT_OVERLAY,
		.vfespfr = ZR36057_VFESPFR_RGB555|ZR36057_VFESPFR_ErrDif|
			   ZR36057_VFESPFR_LittleEndian,
	}, {
		.name = "15-bit RGB BE",
		.fourcc = V4L2_PIX_FMT_RGB555X,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.depth = 15,
		.flags = ZORAN_FORMAT_CAPTURE |
			 ZORAN_FORMAT_OVERLAY,
		.vfespfr = ZR36057_VFESPFR_RGB555|ZR36057_VFESPFR_ErrDif,
	}, {
		.name = "16-bit RGB LE",
		.fourcc = V4L2_PIX_FMT_RGB565,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.depth = 16,
		.flags = ZORAN_FORMAT_CAPTURE |
			 ZORAN_FORMAT_OVERLAY,
		.vfespfr = ZR36057_VFESPFR_RGB565|ZR36057_VFESPFR_ErrDif|
			   ZR36057_VFESPFR_LittleEndian,
	}, {
		.name = "16-bit RGB BE",
		.fourcc = V4L2_PIX_FMT_RGB565X,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.depth = 16,
		.flags = ZORAN_FORMAT_CAPTURE |
			 ZORAN_FORMAT_OVERLAY,
		.vfespfr = ZR36057_VFESPFR_RGB565|ZR36057_VFESPFR_ErrDif,
	}, {
		.name = "24-bit RGB",
		.fourcc = V4L2_PIX_FMT_BGR24,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.depth = 24,
		.flags = ZORAN_FORMAT_CAPTURE |
			 ZORAN_FORMAT_OVERLAY,
		.vfespfr = ZR36057_VFESPFR_RGB888|ZR36057_VFESPFR_Pack24,
	}, {
		.name = "32-bit RGB LE",
		.fourcc = V4L2_PIX_FMT_BGR32,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.depth = 32,
		.flags = ZORAN_FORMAT_CAPTURE |
			 ZORAN_FORMAT_OVERLAY,
		.vfespfr = ZR36057_VFESPFR_RGB888|ZR36057_VFESPFR_LittleEndian,
	}, {
		.name = "32-bit RGB BE",
		.fourcc = V4L2_PIX_FMT_RGB32,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.depth = 32,
		.flags = ZORAN_FORMAT_CAPTURE |
			 ZORAN_FORMAT_OVERLAY,
		.vfespfr = ZR36057_VFESPFR_RGB888,
	}, {
		.name = "4:2:2, packed, YUYV",
		.fourcc = V4L2_PIX_FMT_YUYV,
		.colorspace = V4L2_COLORSPACE_SMPTE170M,
		.depth = 16,
		.flags = ZORAN_FORMAT_CAPTURE |
			 ZORAN_FORMAT_OVERLAY,
		.vfespfr = ZR36057_VFESPFR_YUV422,
	}, {
		.name = "4:2:2, packed, UYVY",
		.fourcc = V4L2_PIX_FMT_UYVY,
		.colorspace = V4L2_COLORSPACE_SMPTE170M,
		.depth = 16,
		.flags = ZORAN_FORMAT_CAPTURE |
			 ZORAN_FORMAT_OVERLAY,
		.vfespfr = ZR36057_VFESPFR_YUV422|ZR36057_VFESPFR_LittleEndian,
	}, {
		.name = "Hardware-encoded Motion-JPEG",
		.fourcc = V4L2_PIX_FMT_MJPEG,
		.colorspace = V4L2_COLORSPACE_SMPTE170M,
		.depth = 0,
		.flags = ZORAN_FORMAT_CAPTURE |
			 ZORAN_FORMAT_PLAYBACK |
			 ZORAN_FORMAT_COMPRESSED,
	}
};
#define NUM_FORMATS ARRAY_SIZE(zoran_formats)

	/* small helper function for calculating buffersizes for v4l2
	 * we calculate the nearest higher power-of-two, which
	 * will be the recommended buffersize */
static __u32
zoran_v4l2_calc_bufsize (struct zoran_jpg_settings *settings)
{
	__u8 div = settings->VerDcm * settings->HorDcm * settings->TmpDcm;
	__u32 num = (1024 * 512) / (div);
	__u32 result = 2;

	num--;
	while (num) {
		num >>= 1;
		result <<= 1;
	}

	if (result > jpg_bufsize)
		return jpg_bufsize;
	if (result < 8192)
		return 8192;
	return result;
}

/* forward references */
static void v4l_fbuffer_free(struct zoran_fh *fh);
static void jpg_fbuffer_free(struct zoran_fh *fh);

/* Set mapping mode */
static void map_mode_raw(struct zoran_fh *fh)
{
	fh->map_mode = ZORAN_MAP_MODE_RAW;
	fh->buffers.buffer_size = v4l_bufsize;
	fh->buffers.num_buffers = v4l_nbufs;
}
static void map_mode_jpg(struct zoran_fh *fh, int play)
{
	fh->map_mode = play ? ZORAN_MAP_MODE_JPG_PLAY : ZORAN_MAP_MODE_JPG_REC;
	fh->buffers.buffer_size = jpg_bufsize;
	fh->buffers.num_buffers = jpg_nbufs;
}
static inline const char *mode_name(enum zoran_map_mode mode)
{
	return mode == ZORAN_MAP_MODE_RAW ? "V4L" : "JPG";
}

/*
 *   Allocate the V4L grab buffers
 *
 *   These have to be pysically contiguous.
 */

static int v4l_fbuffer_alloc(struct zoran_fh *fh)
{
	struct zoran *zr = fh->zr;
	int i, off;
	unsigned char *mem;

	for (i = 0; i < fh->buffers.num_buffers; i++) {
		if (fh->buffers.buffer[i].v4l.fbuffer)
			dprintk(2,
				KERN_WARNING
				"%s: %s - buffer %d already allocated!?\n",
				ZR_DEVNAME(zr), __func__, i);

		//udelay(20);
		mem = kmalloc(fh->buffers.buffer_size,
			      GFP_KERNEL | __GFP_NOWARN);
		if (!mem) {
			v4l_fbuffer_free(fh);
			return -ENOBUFS;
		}
		fh->buffers.buffer[i].v4l.fbuffer = mem;
		fh->buffers.buffer[i].v4l.fbuffer_phys = virt_to_phys(mem);
		fh->buffers.buffer[i].v4l.fbuffer_bus = virt_to_bus(mem);
		for (off = 0; off < fh->buffers.buffer_size;
		     off += PAGE_SIZE)
			SetPageReserved(virt_to_page(mem + off));
		dprintk(4,
			KERN_INFO
			"%s: %s - V4L frame %d mem %p (bus: 0x%llx)\n",
			ZR_DEVNAME(zr), __func__, i, mem,
			(unsigned long long)virt_to_bus(mem));
	}

	fh->buffers.allocated = 1;

	return 0;
}

/* free the V4L grab buffers */
static void v4l_fbuffer_free(struct zoran_fh *fh)
{
	struct zoran *zr = fh->zr;
	int i, off;
	unsigned char *mem;

	dprintk(4, KERN_INFO "%s: %s\n", ZR_DEVNAME(zr), __func__);

	for (i = 0; i < fh->buffers.num_buffers; i++) {
		if (!fh->buffers.buffer[i].v4l.fbuffer)
			continue;

		mem = fh->buffers.buffer[i].v4l.fbuffer;
		for (off = 0; off < fh->buffers.buffer_size;
		     off += PAGE_SIZE)
			ClearPageReserved(virt_to_page(mem + off));
		kfree(fh->buffers.buffer[i].v4l.fbuffer);
		fh->buffers.buffer[i].v4l.fbuffer = NULL;
	}

	fh->buffers.allocated = 0;
}

/*
 *   Allocate the MJPEG grab buffers.
 *
 *   If a Natoma chipset is present and this is a revision 1 zr36057,
 *   each MJPEG buffer needs to be physically contiguous.
 *   (RJ: This statement is from Dave Perks' original driver,
 *   I could never check it because I have a zr36067)
 *
 *   RJ: The contents grab buffers needs never be accessed in the driver.
 *       Therefore there is no need to allocate them with vmalloc in order
 *       to get a contiguous virtual memory space.
 *       I don't understand why many other drivers first allocate them with
 *       vmalloc (which uses internally also get_zeroed_page, but delivers you
 *       virtual addresses) and then again have to make a lot of efforts
 *       to get the physical address.
 *
 *   Ben Capper:
 *       On big-endian architectures (such as ppc) some extra steps
 *       are needed. When reading and writing to the stat_com array
 *       and fragment buffers, the device expects to see little-
 *       endian values. The use of cpu_to_le32() and le32_to_cpu()
 *       in this function (and one or two others in zoran_device.c)
 *       ensure that these values are always stored in little-endian
 *       form, regardless of architecture. The zr36057 does Very Bad
 *       Things on big endian architectures if the stat_com array
 *       and fragment buffers are not little-endian.
 */

static int jpg_fbuffer_alloc(struct zoran_fh *fh)
{
	struct zoran *zr = fh->zr;
	int i, j, off;
	u8 *mem;

	for (i = 0; i < fh->buffers.num_buffers; i++) {
		if (fh->buffers.buffer[i].jpg.frag_tab)
			dprintk(2,
				KERN_WARNING
				"%s: %s - buffer %d already allocated!?\n",
				ZR_DEVNAME(zr), __func__, i);

		/* Allocate fragment table for this buffer */

		mem = (void *)get_zeroed_page(GFP_KERNEL);
		if (!mem) {
			dprintk(1,
				KERN_ERR
				"%s: %s - get_zeroed_page (frag_tab) failed for buffer %d\n",
				ZR_DEVNAME(zr), __func__, i);
			jpg_fbuffer_free(fh);
			return -ENOBUFS;
		}
		fh->buffers.buffer[i].jpg.frag_tab = (__le32 *)mem;
		fh->buffers.buffer[i].jpg.frag_tab_bus = virt_to_bus(mem);

		if (fh->buffers.need_contiguous) {
			mem = kmalloc(fh->buffers.buffer_size, GFP_KERNEL);
			if (!mem) {
				dprintk(1,
					KERN_ERR
					"%s: %s - kmalloc failed for buffer %d\n",
					ZR_DEVNAME(zr), __func__, i);
				jpg_fbuffer_free(fh);
				return -ENOBUFS;
			}
			fh->buffers.buffer[i].jpg.frag_tab[0] =
				cpu_to_le32(virt_to_bus(mem));
			fh->buffers.buffer[i].jpg.frag_tab[1] =
				cpu_to_le32((fh->buffers.buffer_size >> 1) | 1);
			for (off = 0; off < fh->buffers.buffer_size; off += PAGE_SIZE)
				SetPageReserved(virt_to_page(mem + off));
		} else {
			/* jpg_bufsize is already page aligned */
			for (j = 0; j < fh->buffers.buffer_size / PAGE_SIZE; j++) {
				mem = (void *)get_zeroed_page(GFP_KERNEL);
				if (mem == NULL) {
					dprintk(1,
						KERN_ERR
						"%s: %s - get_zeroed_page failed for buffer %d\n",
						ZR_DEVNAME(zr), __func__, i);
					jpg_fbuffer_free(fh);
					return -ENOBUFS;
				}

				fh->buffers.buffer[i].jpg.frag_tab[2 * j] =
					cpu_to_le32(virt_to_bus(mem));
				fh->buffers.buffer[i].jpg.frag_tab[2 * j + 1] =
					cpu_to_le32((PAGE_SIZE >> 2) << 1);
				SetPageReserved(virt_to_page(mem));
			}

			fh->buffers.buffer[i].jpg.frag_tab[2 * j - 1] |= cpu_to_le32(1);
		}
	}

	dprintk(4,
		KERN_DEBUG "%s: %s - %d KB allocated\n",
		ZR_DEVNAME(zr), __func__,
		(fh->buffers.num_buffers * fh->buffers.buffer_size) >> 10);

	fh->buffers.allocated = 1;

	return 0;
}

/* free the MJPEG grab buffers */
static void jpg_fbuffer_free(struct zoran_fh *fh)
{
	struct zoran *zr = fh->zr;
	int i, j, off;
	unsigned char *mem;
	__le32 frag_tab;
	struct zoran_buffer *buffer;

	dprintk(4, KERN_DEBUG "%s: %s\n", ZR_DEVNAME(zr), __func__);

	for (i = 0, buffer = &fh->buffers.buffer[0];
	     i < fh->buffers.num_buffers; i++, buffer++) {
		if (!buffer->jpg.frag_tab)
			continue;

		if (fh->buffers.need_contiguous) {
			frag_tab = buffer->jpg.frag_tab[0];

			if (frag_tab) {
				mem = bus_to_virt(le32_to_cpu(frag_tab));
				for (off = 0; off < fh->buffers.buffer_size; off += PAGE_SIZE)
					ClearPageReserved(virt_to_page(mem + off));
				kfree(mem);
				buffer->jpg.frag_tab[0] = 0;
				buffer->jpg.frag_tab[1] = 0;
			}
		} else {
			for (j = 0; j < fh->buffers.buffer_size / PAGE_SIZE; j++) {
				frag_tab = buffer->jpg.frag_tab[2 * j];

				if (!frag_tab)
					break;
				ClearPageReserved(virt_to_page(bus_to_virt(le32_to_cpu(frag_tab))));
				free_page((unsigned long)bus_to_virt(le32_to_cpu(frag_tab)));
				buffer->jpg.frag_tab[2 * j] = 0;
				buffer->jpg.frag_tab[2 * j + 1] = 0;
			}
		}

		free_page((unsigned long)buffer->jpg.frag_tab);
		buffer->jpg.frag_tab = NULL;
	}

	fh->buffers.allocated = 0;
}

/*
 *   V4L Buffer grabbing
 */

static int
zoran_v4l_set_format (struct zoran_fh           *fh,
		      int                        width,
		      int                        height,
		      const struct zoran_format *format)
{
	struct zoran *zr = fh->zr;
	int bpp;

	/* Check size and format of the grab wanted */

	if (height < BUZ_MIN_HEIGHT || width < BUZ_MIN_WIDTH ||
	    height > BUZ_MAX_HEIGHT || width > BUZ_MAX_WIDTH) {
		dprintk(1,
			KERN_ERR
			"%s: %s - wrong frame size (%dx%d)\n",
			ZR_DEVNAME(zr), __func__, width, height);
		return -EINVAL;
	}

	bpp = (format->depth + 7) / 8;

	/* Check against available buffer size */
	if (height * width * bpp > fh->buffers.buffer_size) {
		dprintk(1,
			KERN_ERR
			"%s: %s - video buffer size (%d kB) is too small\n",
			ZR_DEVNAME(zr), __func__, fh->buffers.buffer_size >> 10);
		return -EINVAL;
	}

	/* The video front end needs 4-byte alinged line sizes */

	if ((bpp == 2 && (width & 1)) || (bpp == 3 && (width & 3))) {
		dprintk(1,
			KERN_ERR
			"%s: %s - wrong frame alignment\n",
			ZR_DEVNAME(zr), __func__);
		return -EINVAL;
	}

	fh->v4l_settings.width = width;
	fh->v4l_settings.height = height;
	fh->v4l_settings.format = format;
	fh->v4l_settings.bytesperline = bpp * fh->v4l_settings.width;

	return 0;
}

static int zoran_v4l_queue_frame(struct zoran_fh *fh, int num)
{
	struct zoran *zr = fh->zr;
	unsigned long flags;
	int res = 0;

	if (!fh->buffers.allocated) {
		dprintk(1,
			KERN_ERR
			"%s: %s - buffers not yet allocated\n",
			ZR_DEVNAME(zr), __func__);
		res = -ENOMEM;
	}

	/* No grabbing outside the buffer range! */
	if (num >= fh->buffers.num_buffers || num < 0) {
		dprintk(1,
			KERN_ERR
			"%s: %s - buffer %d is out of range\n",
			ZR_DEVNAME(zr), __func__, num);
		res = -EINVAL;
	}

	spin_lock_irqsave(&zr->spinlock, flags);

	if (fh->buffers.active == ZORAN_FREE) {
		if (zr->v4l_buffers.active == ZORAN_FREE) {
			zr->v4l_buffers = fh->buffers;
			fh->buffers.active = ZORAN_ACTIVE;
		} else {
			dprintk(1,
				KERN_ERR
				"%s: %s - another session is already capturing\n",
				ZR_DEVNAME(zr), __func__);
			res = -EBUSY;
		}
	}

	/* make sure a grab isn't going on currently with this buffer */
	if (!res) {
		switch (zr->v4l_buffers.buffer[num].state) {
		default:
		case BUZ_STATE_PEND:
			if (zr->v4l_buffers.active == ZORAN_FREE) {
				fh->buffers.active = ZORAN_FREE;
				zr->v4l_buffers.allocated = 0;
			}
			res = -EBUSY;	/* what are you doing? */
			break;
		case BUZ_STATE_DONE:
			dprintk(2,
				KERN_WARNING
				"%s: %s - queueing buffer %d in state DONE!?\n",
				ZR_DEVNAME(zr), __func__, num);
			/* fall through */
		case BUZ_STATE_USER:
			/* since there is at least one unused buffer there's room for at least
			 * one more pend[] entry */
			zr->v4l_pend[zr->v4l_pend_head++ & V4L_MASK_FRAME] = num;
			zr->v4l_buffers.buffer[num].state = BUZ_STATE_PEND;
			zr->v4l_buffers.buffer[num].bs.length =
			    fh->v4l_settings.bytesperline *
			    zr->v4l_settings.height;
			fh->buffers.buffer[num] = zr->v4l_buffers.buffer[num];
			break;
		}
	}

	spin_unlock_irqrestore(&zr->spinlock, flags);

	if (!res && zr->v4l_buffers.active == ZORAN_FREE)
		zr->v4l_buffers.active = fh->buffers.active;

	return res;
}

/*
 * Sync on a V4L buffer
 */

static int v4l_sync(struct zoran_fh *fh, int frame)
{
	struct zoran *zr = fh->zr;
	unsigned long flags;

	if (fh->buffers.active == ZORAN_FREE) {
		dprintk(1,
			KERN_ERR
			"%s: %s - no grab active for this session\n",
			ZR_DEVNAME(zr), __func__);
		return -EINVAL;
	}

	/* check passed-in frame number */
	if (frame >= fh->buffers.num_buffers || frame < 0) {
		dprintk(1,
			KERN_ERR "%s: %s - frame %d is invalid\n",
			ZR_DEVNAME(zr), __func__, frame);
		return -EINVAL;
	}

	/* Check if is buffer was queued at all */
	if (zr->v4l_buffers.buffer[frame].state == BUZ_STATE_USER) {
		dprintk(1,
			KERN_ERR
			"%s: %s - attempt to sync on a buffer which was not queued?\n",
			ZR_DEVNAME(zr), __func__);
		return -EPROTO;
	}

	mutex_unlock(&zr->lock);
	/* wait on this buffer to get ready */
	if (!wait_event_interruptible_timeout(zr->v4l_capq,
		(zr->v4l_buffers.buffer[frame].state != BUZ_STATE_PEND), 10*HZ)) {
		mutex_lock(&zr->lock);
		return -ETIME;
	}
	mutex_lock(&zr->lock);
	if (signal_pending(current))
		return -ERESTARTSYS;

	/* buffer should now be in BUZ_STATE_DONE */
	if (zr->v4l_buffers.buffer[frame].state != BUZ_STATE_DONE)
		dprintk(2,
			KERN_ERR "%s: %s - internal state error\n",
			ZR_DEVNAME(zr), __func__);

	zr->v4l_buffers.buffer[frame].state = BUZ_STATE_USER;
	fh->buffers.buffer[frame] = zr->v4l_buffers.buffer[frame];

	spin_lock_irqsave(&zr->spinlock, flags);

	/* Check if streaming capture has finished */
	if (zr->v4l_pend_tail == zr->v4l_pend_head) {
		zr36057_set_memgrab(zr, 0);
		if (zr->v4l_buffers.active == ZORAN_ACTIVE) {
			fh->buffers.active = zr->v4l_buffers.active = ZORAN_FREE;
			zr->v4l_buffers.allocated = 0;
		}
	}

	spin_unlock_irqrestore(&zr->spinlock, flags);

	return 0;
}

/*
 *   Queue a MJPEG buffer for capture/playback
 */

static int zoran_jpg_queue_frame(struct zoran_fh *fh, int num,
				 enum zoran_codec_mode mode)
{
	struct zoran *zr = fh->zr;
	unsigned long flags;
	int res = 0;

	/* Check if buffers are allocated */
	if (!fh->buffers.allocated) {
		dprintk(1,
			KERN_ERR
			"%s: %s - buffers not yet allocated\n",
			ZR_DEVNAME(zr), __func__);
		return -ENOMEM;
	}

	/* No grabbing outside the buffer range! */
	if (num >= fh->buffers.num_buffers || num < 0) {
		dprintk(1,
			KERN_ERR
			"%s: %s - buffer %d out of range\n",
			ZR_DEVNAME(zr), __func__, num);
		return -EINVAL;
	}

	/* what is the codec mode right now? */
	if (zr->codec_mode == BUZ_MODE_IDLE) {
		zr->jpg_settings = fh->jpg_settings;
	} else if (zr->codec_mode != mode) {
		/* wrong codec mode active - invalid */
		dprintk(1,
			KERN_ERR
			"%s: %s - codec in wrong mode\n",
			ZR_DEVNAME(zr), __func__);
		return -EINVAL;
	}

	if (fh->buffers.active == ZORAN_FREE) {
		if (zr->jpg_buffers.active == ZORAN_FREE) {
			zr->jpg_buffers = fh->buffers;
			fh->buffers.active = ZORAN_ACTIVE;
		} else {
			dprintk(1,
				KERN_ERR
				"%s: %s - another session is already capturing\n",
				ZR_DEVNAME(zr), __func__);
			res = -EBUSY;
		}
	}

	if (!res && zr->codec_mode == BUZ_MODE_IDLE) {
		/* Ok load up the jpeg codec */
		zr36057_enable_jpg(zr, mode);
	}

	spin_lock_irqsave(&zr->spinlock, flags);

	if (!res) {
		switch (zr->jpg_buffers.buffer[num].state) {
		case BUZ_STATE_DONE:
			dprintk(2,
				KERN_WARNING
				"%s: %s - queuing frame in BUZ_STATE_DONE state!?\n",
				ZR_DEVNAME(zr), __func__);
			/* fall through */
		case BUZ_STATE_USER:
			/* since there is at least one unused buffer there's room for at
			 *least one more pend[] entry */
			zr->jpg_pend[zr->jpg_que_head++ & BUZ_MASK_FRAME] = num;
			zr->jpg_buffers.buffer[num].state = BUZ_STATE_PEND;
			fh->buffers.buffer[num] = zr->jpg_buffers.buffer[num];
			zoran_feed_stat_com(zr);
			break;
		default:
		case BUZ_STATE_DMA:
		case BUZ_STATE_PEND:
			if (zr->jpg_buffers.active == ZORAN_FREE) {
				fh->buffers.active = ZORAN_FREE;
				zr->jpg_buffers.allocated = 0;
			}
			res = -EBUSY;	/* what are you doing? */
			break;
		}
	}

	spin_unlock_irqrestore(&zr->spinlock, flags);

	if (!res && zr->jpg_buffers.active == ZORAN_FREE)
		zr->jpg_buffers.active = fh->buffers.active;

	return res;
}

static int jpg_qbuf(struct zoran_fh *fh, int frame, enum zoran_codec_mode mode)
{
	struct zoran *zr = fh->zr;
	int res = 0;

	/* Does the user want to stop streaming? */
	if (frame < 0) {
		if (zr->codec_mode == mode) {
			if (fh->buffers.active == ZORAN_FREE) {
				dprintk(1,
					KERN_ERR
					"%s: %s(-1) - session not active\n",
					ZR_DEVNAME(zr), __func__);
				return -EINVAL;
			}
			fh->buffers.active = zr->jpg_buffers.active = ZORAN_FREE;
			zr->jpg_buffers.allocated = 0;
			zr36057_enable_jpg(zr, BUZ_MODE_IDLE);
			return 0;
		} else {
			dprintk(1,
				KERN_ERR
				"%s: %s - stop streaming but not in streaming mode\n",
				ZR_DEVNAME(zr), __func__);
			return -EINVAL;
		}
	}

	if ((res = zoran_jpg_queue_frame(fh, frame, mode)))
		return res;

	/* Start the jpeg codec when the first frame is queued  */
	if (!res && zr->jpg_que_head == 1)
		jpeg_start(zr);

	return res;
}

/*
 *   Sync on a MJPEG buffer
 */

static int jpg_sync(struct zoran_fh *fh, struct zoran_sync *bs)
{
	struct zoran *zr = fh->zr;
	unsigned long flags;
	int frame;

	if (fh->buffers.active == ZORAN_FREE) {
		dprintk(1,
			KERN_ERR
			"%s: %s - capture is not currently active\n",
			ZR_DEVNAME(zr), __func__);
		return -EINVAL;
	}
	if (zr->codec_mode != BUZ_MODE_MOTION_DECOMPRESS &&
	    zr->codec_mode != BUZ_MODE_MOTION_COMPRESS) {
		dprintk(1,
			KERN_ERR
			"%s: %s - codec not in streaming mode\n",
			ZR_DEVNAME(zr), __func__);
		return -EINVAL;
	}
	mutex_unlock(&zr->lock);
	if (!wait_event_interruptible_timeout(zr->jpg_capq,
			(zr->jpg_que_tail != zr->jpg_dma_tail ||
			 zr->jpg_dma_tail == zr->jpg_dma_head),
			10*HZ)) {
		int isr;

		btand(~ZR36057_JMC_Go_en, ZR36057_JMC);
		udelay(1);
		zr->codec->control(zr->codec, CODEC_G_STATUS,
					   sizeof(isr), &isr);
		mutex_lock(&zr->lock);
		dprintk(1,
			KERN_ERR
			"%s: %s - timeout: codec isr=0x%02x\n",
			ZR_DEVNAME(zr), __func__, isr);

		return -ETIME;

	}
	mutex_lock(&zr->lock);
	if (signal_pending(current))
		return -ERESTARTSYS;

	spin_lock_irqsave(&zr->spinlock, flags);

	if (zr->jpg_dma_tail != zr->jpg_dma_head)
		frame = zr->jpg_pend[zr->jpg_que_tail++ & BUZ_MASK_FRAME];
	else
		frame = zr->jpg_pend[zr->jpg_que_tail & BUZ_MASK_FRAME];

	/* buffer should now be in BUZ_STATE_DONE */
	if (zr->jpg_buffers.buffer[frame].state != BUZ_STATE_DONE)
		dprintk(2,
			KERN_ERR "%s: %s - internal state error\n",
			ZR_DEVNAME(zr), __func__);

	*bs = zr->jpg_buffers.buffer[frame].bs;
	bs->frame = frame;
	zr->jpg_buffers.buffer[frame].state = BUZ_STATE_USER;
	fh->buffers.buffer[frame] = zr->jpg_buffers.buffer[frame];

	spin_unlock_irqrestore(&zr->spinlock, flags);

	return 0;
}

static void zoran_open_init_session(struct zoran_fh *fh)
{
	int i;
	struct zoran *zr = fh->zr;

	/* Per default, map the V4L Buffers */
	map_mode_raw(fh);

	/* take over the card's current settings */
	fh->overlay_settings = zr->overlay_settings;
	fh->overlay_settings.is_set = 0;
	fh->overlay_settings.format = zr->overlay_settings.format;
	fh->overlay_active = ZORAN_FREE;

	/* v4l settings */
	fh->v4l_settings = zr->v4l_settings;
	/* jpg settings */
	fh->jpg_settings = zr->jpg_settings;

	/* buffers */
	memset(&fh->buffers, 0, sizeof(fh->buffers));
	for (i = 0; i < MAX_FRAME; i++) {
		fh->buffers.buffer[i].state = BUZ_STATE_USER;	/* nothing going on */
		fh->buffers.buffer[i].bs.frame = i;
	}
	fh->buffers.allocated = 0;
	fh->buffers.active = ZORAN_FREE;
}

static void zoran_close_end_session(struct zoran_fh *fh)
{
	struct zoran *zr = fh->zr;

	/* overlay */
	if (fh->overlay_active != ZORAN_FREE) {
		fh->overlay_active = zr->overlay_active = ZORAN_FREE;
		zr->v4l_overlay_active = 0;
		if (!zr->v4l_memgrab_active)
			zr36057_overlay(zr, 0);
		zr->overlay_mask = NULL;
	}

	if (fh->map_mode == ZORAN_MAP_MODE_RAW) {
		/* v4l capture */
		if (fh->buffers.active != ZORAN_FREE) {
			unsigned long flags;

			spin_lock_irqsave(&zr->spinlock, flags);
			zr36057_set_memgrab(zr, 0);
			zr->v4l_buffers.allocated = 0;
			zr->v4l_buffers.active = fh->buffers.active = ZORAN_FREE;
			spin_unlock_irqrestore(&zr->spinlock, flags);
		}

		/* v4l buffers */
		if (fh->buffers.allocated)
			v4l_fbuffer_free(fh);
	} else {
		/* jpg capture */
		if (fh->buffers.active != ZORAN_FREE) {
			zr36057_enable_jpg(zr, BUZ_MODE_IDLE);
			zr->jpg_buffers.allocated = 0;
			zr->jpg_buffers.active = fh->buffers.active = ZORAN_FREE;
		}

		/* jpg buffers */
		if (fh->buffers.allocated)
			jpg_fbuffer_free(fh);
	}
}

/*
 *   Open a zoran card. Right now the flags stuff is just playing
 */

static int zoran_open(struct file *file)
{
	struct zoran *zr = video_drvdata(file);
	struct zoran_fh *fh;
	int res, first_open = 0;

	dprintk(2, KERN_INFO "%s: %s(%s, pid=[%d]), users(-)=%d\n",
		ZR_DEVNAME(zr), __func__, current->comm, task_pid_nr(current), zr->user + 1);

	mutex_lock(&zr->lock);

	if (zr->user >= 2048) {
		dprintk(1, KERN_ERR "%s: too many users (%d) on device\n",
			ZR_DEVNAME(zr), zr->user);
		res = -EBUSY;
		goto fail_unlock;
	}

	/* now, create the open()-specific file_ops struct */
	fh = kzalloc(sizeof(struct zoran_fh), GFP_KERNEL);
	if (!fh) {
		dprintk(1,
			KERN_ERR
			"%s: %s - allocation of zoran_fh failed\n",
			ZR_DEVNAME(zr), __func__);
		res = -ENOMEM;
		goto fail_unlock;
	}
	v4l2_fh_init(&fh->fh, video_devdata(file));

	/* used to be BUZ_MAX_WIDTH/HEIGHT, but that gives overflows
	 * on norm-change! */
	fh->overlay_mask =
	    kmalloc(array3_size((768 + 31) / 32, 576, 4), GFP_KERNEL);
	if (!fh->overlay_mask) {
		dprintk(1,
			KERN_ERR
			"%s: %s - allocation of overlay_mask failed\n",
			ZR_DEVNAME(zr), __func__);
		res = -ENOMEM;
		goto fail_fh;
	}

	if (zr->user++ == 0)
		first_open = 1;

	/* default setup - TODO: look at flags */
	if (first_open) {	/* First device open */
		zr36057_restart(zr);
		zoran_open_init_params(zr);
		zoran_init_hardware(zr);

		btor(ZR36057_ICR_IntPinEn, ZR36057_ICR);
	}

	/* set file_ops stuff */
	file->private_data = fh;
	fh->zr = zr;
	zoran_open_init_session(fh);
	v4l2_fh_add(&fh->fh);
	mutex_unlock(&zr->lock);

	return 0;

fail_fh:
	v4l2_fh_exit(&fh->fh);
	kfree(fh);
fail_unlock:
	mutex_unlock(&zr->lock);

	dprintk(2, KERN_INFO "%s: open failed (%d), users(-)=%d\n",
		ZR_DEVNAME(zr), res, zr->user);

	return res;
}

static int
zoran_close(struct file  *file)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;

	dprintk(2, KERN_INFO "%s: %s(%s, pid=[%d]), users(+)=%d\n",
		ZR_DEVNAME(zr), __func__, current->comm, task_pid_nr(current), zr->user - 1);

	/* kernel locks (fs/device.c), so don't do that ourselves
	 * (prevents deadlocks) */
	mutex_lock(&zr->lock);

	zoran_close_end_session(fh);

	if (zr->user-- == 1) {	/* Last process */
		/* Clean up JPEG process */
		wake_up_interruptible(&zr->jpg_capq);
		zr36057_enable_jpg(zr, BUZ_MODE_IDLE);
		zr->jpg_buffers.allocated = 0;
		zr->jpg_buffers.active = ZORAN_FREE;

		/* disable interrupts */
		btand(~ZR36057_ICR_IntPinEn, ZR36057_ICR);

		if (zr36067_debug > 1)
			print_interrupts(zr);

		/* Overlay off */
		zr->v4l_overlay_active = 0;
		zr36057_overlay(zr, 0);
		zr->overlay_mask = NULL;

		/* capture off */
		wake_up_interruptible(&zr->v4l_capq);
		zr36057_set_memgrab(zr, 0);
		zr->v4l_buffers.allocated = 0;
		zr->v4l_buffers.active = ZORAN_FREE;
		zoran_set_pci_master(zr, 0);

		if (!pass_through) {	/* Switch to color bar */
			decoder_call(zr, video, s_stream, 0);
			encoder_call(zr, video, s_routing, 2, 0, 0);
		}
	}
	mutex_unlock(&zr->lock);

	v4l2_fh_del(&fh->fh);
	v4l2_fh_exit(&fh->fh);
	kfree(fh->overlay_mask);
	kfree(fh);

	dprintk(4, KERN_INFO "%s: %s done\n", ZR_DEVNAME(zr), __func__);

	return 0;
}

static int setup_fbuffer(struct zoran_fh *fh,
	       void                      *base,
	       const struct zoran_format *fmt,
	       int                        width,
	       int                        height,
	       int                        bytesperline)
{
	struct zoran *zr = fh->zr;

	/* (Ronald) v4l/v4l2 guidelines */
	if (!capable(CAP_SYS_ADMIN) && !capable(CAP_SYS_RAWIO))
		return -EPERM;

	/* Don't allow frame buffer overlay if PCI or AGP is buggy, or on
	   ALi Magik (that needs very low latency while the card needs a
	   higher value always) */

	if (pci_pci_problems & (PCIPCI_FAIL | PCIAGP_FAIL | PCIPCI_ALIMAGIK))
		return -ENXIO;

	/* we need a bytesperline value, even if not given */
	if (!bytesperline)
		bytesperline = width * ((fmt->depth + 7) & ~7) / 8;

#if 0
	if (zr->overlay_active) {
		/* dzjee... stupid users... don't even bother to turn off
		 * overlay before changing the memory location...
		 * normally, we would return errors here. However, one of
		 * the tools that does this is... xawtv! and since xawtv
		 * is used by +/- 99% of the users, we'd rather be user-
		 * friendly and silently do as if nothing went wrong */
		dprintk(3,
			KERN_ERR
			"%s: %s - forced overlay turnoff because framebuffer changed\n",
			ZR_DEVNAME(zr), __func__);
		zr36057_overlay(zr, 0);
	}
#endif

	if (!(fmt->flags & ZORAN_FORMAT_OVERLAY)) {
		dprintk(1,
			KERN_ERR
			"%s: %s - no valid overlay format given\n",
			ZR_DEVNAME(zr), __func__);
		return -EINVAL;
	}
	if (height <= 0 || width <= 0 || bytesperline <= 0) {
		dprintk(1,
			KERN_ERR
			"%s: %s - invalid height/width/bpl value (%d|%d|%d)\n",
			ZR_DEVNAME(zr), __func__, width, height, bytesperline);
		return -EINVAL;
	}
	if (bytesperline & 3) {
		dprintk(1,
			KERN_ERR
			"%s: %s - bytesperline (%d) must be 4-byte aligned\n",
			ZR_DEVNAME(zr), __func__, bytesperline);
		return -EINVAL;
	}

	zr->vbuf_base = (void *) ((unsigned long) base & ~3);
	zr->vbuf_height = height;
	zr->vbuf_width = width;
	zr->vbuf_depth = fmt->depth;
	zr->overlay_settings.format = fmt;
	zr->vbuf_bytesperline = bytesperline;

	/* The user should set new window parameters */
	zr->overlay_settings.is_set = 0;

	return 0;
}


static int setup_window(struct zoran_fh *fh,
			int x,
			int y,
			int width,
			int height,
			struct v4l2_clip __user *clips,
			unsigned int clipcount,
			void __user *bitmap)
{
	struct zoran *zr = fh->zr;
	struct v4l2_clip *vcp = NULL;
	int on, end;


	if (!zr->vbuf_base) {
		dprintk(1,
			KERN_ERR
			"%s: %s - frame buffer has to be set first\n",
			ZR_DEVNAME(zr), __func__);
		return -EINVAL;
	}

	if (!fh->overlay_settings.format) {
		dprintk(1,
			KERN_ERR
			"%s: %s - no overlay format set\n",
			ZR_DEVNAME(zr), __func__);
		return -EINVAL;
	}

	if (clipcount > 2048) {
		dprintk(1,
			KERN_ERR
			"%s: %s - invalid clipcount\n",
			 ZR_DEVNAME(zr), __func__);
		return -EINVAL;
	}

	/*
	 * The video front end needs 4-byte alinged line sizes, we correct that
	 * silently here if necessary
	 */
	if (zr->vbuf_depth == 15 || zr->vbuf_depth == 16) {
		end = (x + width) & ~1;	/* round down */
		x = (x + 1) & ~1;	/* round up */
		width = end - x;
	}

	if (zr->vbuf_depth == 24) {
		end = (x + width) & ~3;	/* round down */
		x = (x + 3) & ~3;	/* round up */
		width = end - x;
	}

	if (width > BUZ_MAX_WIDTH)
		width = BUZ_MAX_WIDTH;
	if (height > BUZ_MAX_HEIGHT)
		height = BUZ_MAX_HEIGHT;

	/* Check for invalid parameters */
	if (width < BUZ_MIN_WIDTH || height < BUZ_MIN_HEIGHT ||
	    width > BUZ_MAX_WIDTH || height > BUZ_MAX_HEIGHT) {
		dprintk(1,
			KERN_ERR
			"%s: %s - width = %d or height = %d invalid\n",
			ZR_DEVNAME(zr), __func__, width, height);
		return -EINVAL;
	}

	fh->overlay_settings.x = x;
	fh->overlay_settings.y = y;
	fh->overlay_settings.width = width;
	fh->overlay_settings.height = height;
	fh->overlay_settings.clipcount = clipcount;

	/*
	 * If an overlay is running, we have to switch it off
	 * and switch it on again in order to get the new settings in effect.
	 *
	 * We also want to avoid that the overlay mask is written
	 * when an overlay is running.
	 */

	on = zr->v4l_overlay_active && !zr->v4l_memgrab_active &&
	    zr->overlay_active != ZORAN_FREE &&
	    fh->overlay_active != ZORAN_FREE;
	if (on)
		zr36057_overlay(zr, 0);

	/*
	 *   Write the overlay mask if clips are wanted.
	 *   We prefer a bitmap.
	 */
	if (bitmap) {
		/* fake value - it just means we want clips */
		fh->overlay_settings.clipcount = 1;

		if (copy_from_user(fh->overlay_mask, bitmap,
				   (width * height + 7) / 8)) {
			return -EFAULT;
		}
	} else if (clipcount) {
		/* write our own bitmap from the clips */
		vcp = vmalloc(array_size(sizeof(struct v4l2_clip),
					 clipcount + 4));
		if (vcp == NULL) {
			dprintk(1,
				KERN_ERR
				"%s: %s - Alloc of clip mask failed\n",
				ZR_DEVNAME(zr), __func__);
			return -ENOMEM;
		}
		if (copy_from_user
		    (vcp, clips, sizeof(struct v4l2_clip) * clipcount)) {
			vfree(vcp);
			return -EFAULT;
		}
		write_overlay_mask(fh, vcp, clipcount);
		vfree(vcp);
	}

	fh->overlay_settings.is_set = 1;
	if (fh->overlay_active != ZORAN_FREE &&
	    zr->overlay_active != ZORAN_FREE)
		zr->overlay_settings = fh->overlay_settings;

	if (on)
		zr36057_overlay(zr, 1);

	/* Make sure the changes come into effect */
	return wait_grab_pending(zr);
}

static int setup_overlay(struct zoran_fh *fh, int on)
{
	struct zoran *zr = fh->zr;

	/* If there is nothing to do, return immediately */
	if ((on && fh->overlay_active != ZORAN_FREE) ||
	    (!on && fh->overlay_active == ZORAN_FREE))
		return 0;

	/* check whether we're touching someone else's overlay */
	if (on && zr->overlay_active != ZORAN_FREE &&
	    fh->overlay_active == ZORAN_FREE) {
		dprintk(1,
			KERN_ERR
			"%s: %s - overlay is already active for another session\n",
			ZR_DEVNAME(zr), __func__);
		return -EBUSY;
	}
	if (!on && zr->overlay_active != ZORAN_FREE &&
	    fh->overlay_active == ZORAN_FREE) {
		dprintk(1,
			KERN_ERR
			"%s: %s - you cannot cancel someone else's session\n",
			ZR_DEVNAME(zr), __func__);
		return -EPERM;
	}

	if (on == 0) {
		zr->overlay_active = fh->overlay_active = ZORAN_FREE;
		zr->v4l_overlay_active = 0;
		/* When a grab is running, the video simply
		 * won't be switched on any more */
		if (!zr->v4l_memgrab_active)
			zr36057_overlay(zr, 0);
		zr->overlay_mask = NULL;
	} else {
		if (!zr->vbuf_base || !fh->overlay_settings.is_set) {
			dprintk(1,
				KERN_ERR
				"%s: %s - buffer or window not set\n",
				ZR_DEVNAME(zr), __func__);
			return -EINVAL;
		}
		if (!fh->overlay_settings.format) {
			dprintk(1,
				KERN_ERR
				"%s: %s - no overlay format set\n",
				ZR_DEVNAME(zr), __func__);
			return -EINVAL;
		}
		zr->overlay_active = fh->overlay_active = ZORAN_LOCKED;
		zr->v4l_overlay_active = 1;
		zr->overlay_mask = fh->overlay_mask;
		zr->overlay_settings = fh->overlay_settings;
		if (!zr->v4l_memgrab_active)
			zr36057_overlay(zr, 1);
		/* When a grab is running, the video will be
		 * switched on when grab is finished */
	}

	/* Make sure the changes come into effect */
	return wait_grab_pending(zr);
}

/* get the status of a buffer in the clients buffer queue */
static int zoran_v4l2_buffer_status(struct zoran_fh *fh,
				    struct v4l2_buffer *buf, int num)
{
	struct zoran *zr = fh->zr;
	unsigned long flags;

	buf->flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	switch (fh->map_mode) {
	case ZORAN_MAP_MODE_RAW:
		/* check range */
		if (num < 0 || num >= fh->buffers.num_buffers ||
		    !fh->buffers.allocated) {
			dprintk(1,
				KERN_ERR
				"%s: %s - wrong number or buffers not allocated\n",
				ZR_DEVNAME(zr), __func__);
			return -EINVAL;
		}

		spin_lock_irqsave(&zr->spinlock, flags);
		dprintk(3,
			KERN_DEBUG
			"%s: %s() - raw active=%c, buffer %d: state=%c, map=%c\n",
			ZR_DEVNAME(zr), __func__,
			"FAL"[fh->buffers.active], num,
			"UPMD"[zr->v4l_buffers.buffer[num].state],
			fh->buffers.buffer[num].map ? 'Y' : 'N');
		spin_unlock_irqrestore(&zr->spinlock, flags);

		buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf->length = fh->buffers.buffer_size;

		/* get buffer */
		buf->bytesused = fh->buffers.buffer[num].bs.length;
		if (fh->buffers.buffer[num].state == BUZ_STATE_DONE ||
		    fh->buffers.buffer[num].state == BUZ_STATE_USER) {
			buf->sequence = fh->buffers.buffer[num].bs.seq;
			buf->flags |= V4L2_BUF_FLAG_DONE;
			buf->timestamp = ns_to_timeval(fh->buffers.buffer[num].bs.ts);
		} else {
			buf->flags |= V4L2_BUF_FLAG_QUEUED;
		}

		if (fh->v4l_settings.height <= BUZ_MAX_HEIGHT / 2)
			buf->field = V4L2_FIELD_TOP;
		else
			buf->field = V4L2_FIELD_INTERLACED;

		break;

	case ZORAN_MAP_MODE_JPG_REC:
	case ZORAN_MAP_MODE_JPG_PLAY:

		/* check range */
		if (num < 0 || num >= fh->buffers.num_buffers ||
		    !fh->buffers.allocated) {
			dprintk(1,
				KERN_ERR
				"%s: %s - wrong number or buffers not allocated\n",
				ZR_DEVNAME(zr), __func__);
			return -EINVAL;
		}

		buf->type = (fh->map_mode == ZORAN_MAP_MODE_JPG_REC) ?
			      V4L2_BUF_TYPE_VIDEO_CAPTURE :
			      V4L2_BUF_TYPE_VIDEO_OUTPUT;
		buf->length = fh->buffers.buffer_size;

		/* these variables are only written after frame has been captured */
		if (fh->buffers.buffer[num].state == BUZ_STATE_DONE ||
		    fh->buffers.buffer[num].state == BUZ_STATE_USER) {
			buf->sequence = fh->buffers.buffer[num].bs.seq;
			buf->timestamp = ns_to_timeval(fh->buffers.buffer[num].bs.ts);
			buf->bytesused = fh->buffers.buffer[num].bs.length;
			buf->flags |= V4L2_BUF_FLAG_DONE;
		} else {
			buf->flags |= V4L2_BUF_FLAG_QUEUED;
		}

		/* which fields are these? */
		if (fh->jpg_settings.TmpDcm != 1)
			buf->field = fh->jpg_settings.odd_even ?
				V4L2_FIELD_TOP : V4L2_FIELD_BOTTOM;
		else
			buf->field = fh->jpg_settings.odd_even ?
				V4L2_FIELD_SEQ_TB : V4L2_FIELD_SEQ_BT;

		break;

	default:

		dprintk(5,
			KERN_ERR
			"%s: %s - invalid buffer type|map_mode (%d|%d)\n",
			ZR_DEVNAME(zr), __func__, buf->type, fh->map_mode);
		return -EINVAL;
	}

	buf->memory = V4L2_MEMORY_MMAP;
	buf->index = num;
	buf->m.offset = buf->length * num;

	return 0;
}

static int
zoran_set_norm (struct zoran *zr,
		v4l2_std_id norm)
{
	int on;

	if (zr->v4l_buffers.active != ZORAN_FREE ||
	    zr->jpg_buffers.active != ZORAN_FREE) {
		dprintk(1,
			KERN_WARNING
			"%s: %s called while in playback/capture mode\n",
			ZR_DEVNAME(zr), __func__);
		return -EBUSY;
	}

	if (!(norm & zr->card.norms)) {
		dprintk(1,
			KERN_ERR "%s: %s - unsupported norm %llx\n",
			ZR_DEVNAME(zr), __func__, norm);
		return -EINVAL;
	}

	if (norm & V4L2_STD_SECAM)
		zr->timing = zr->card.tvn[2];
	else if (norm & V4L2_STD_NTSC)
		zr->timing = zr->card.tvn[1];
	else
		zr->timing = zr->card.tvn[0];

	/* We switch overlay off and on since a change in the
	 * norm needs different VFE settings */
	on = zr->overlay_active && !zr->v4l_memgrab_active;
	if (on)
		zr36057_overlay(zr, 0);

	decoder_call(zr, video, s_std, norm);
	encoder_call(zr, video, s_std_output, norm);

	if (on)
		zr36057_overlay(zr, 1);

	/* Make sure the changes come into effect */
	zr->norm = norm;

	return 0;
}

static int
zoran_set_input (struct zoran *zr,
		 int           input)
{
	if (input == zr->input) {
		return 0;
	}

	if (zr->v4l_buffers.active != ZORAN_FREE ||
	    zr->jpg_buffers.active != ZORAN_FREE) {
		dprintk(1,
			KERN_WARNING
			"%s: %s called while in playback/capture mode\n",
			ZR_DEVNAME(zr), __func__);
		return -EBUSY;
	}

	if (input < 0 || input >= zr->card.inputs) {
		dprintk(1,
			KERN_ERR
			"%s: %s - unsupported input %d\n",
			ZR_DEVNAME(zr), __func__, input);
		return -EINVAL;
	}

	zr->input = input;

	decoder_call(zr, video, s_routing,
			zr->card.input[input].muxsel, 0, 0);

	return 0;
}

/*
 *   ioctl routine
 */

static int zoran_querycap(struct file *file, void *__fh, struct v4l2_capability *cap)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;

	strscpy(cap->card, ZR_DEVNAME(zr), sizeof(cap->card));
	strscpy(cap->driver, "zoran", sizeof(cap->driver));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI:%s",
		 pci_name(zr->pci_dev));
	cap->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE |
			   V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_VIDEO_OVERLAY;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int zoran_enum_fmt(struct zoran *zr, struct v4l2_fmtdesc *fmt, int flag)
{
	unsigned int num, i;

	for (num = i = 0; i < NUM_FORMATS; i++) {
		if (zoran_formats[i].flags & flag && num++ == fmt->index) {
			strncpy(fmt->description, zoran_formats[i].name,
				sizeof(fmt->description) - 1);
			/* fmt struct pre-zeroed, so adding '\0' not needed */
			fmt->pixelformat = zoran_formats[i].fourcc;
			if (zoran_formats[i].flags & ZORAN_FORMAT_COMPRESSED)
				fmt->flags |= V4L2_FMT_FLAG_COMPRESSED;
			return 0;
		}
	}
	return -EINVAL;
}

static int zoran_enum_fmt_vid_cap(struct file *file, void *__fh,
					    struct v4l2_fmtdesc *f)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;

	return zoran_enum_fmt(zr, f, ZORAN_FORMAT_CAPTURE);
}

static int zoran_enum_fmt_vid_out(struct file *file, void *__fh,
					    struct v4l2_fmtdesc *f)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;

	return zoran_enum_fmt(zr, f, ZORAN_FORMAT_PLAYBACK);
}

static int zoran_enum_fmt_vid_overlay(struct file *file, void *__fh,
					    struct v4l2_fmtdesc *f)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;

	return zoran_enum_fmt(zr, f, ZORAN_FORMAT_OVERLAY);
}

static int zoran_g_fmt_vid_out(struct file *file, void *__fh,
					struct v4l2_format *fmt)
{
	struct zoran_fh *fh = __fh;

	fmt->fmt.pix.width = fh->jpg_settings.img_width / fh->jpg_settings.HorDcm;
	fmt->fmt.pix.height = fh->jpg_settings.img_height * 2 /
		(fh->jpg_settings.VerDcm * fh->jpg_settings.TmpDcm);
	fmt->fmt.pix.sizeimage = zoran_v4l2_calc_bufsize(&fh->jpg_settings);
	fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	if (fh->jpg_settings.TmpDcm == 1)
		fmt->fmt.pix.field = (fh->jpg_settings.odd_even ?
				V4L2_FIELD_SEQ_TB : V4L2_FIELD_SEQ_BT);
	else
		fmt->fmt.pix.field = (fh->jpg_settings.odd_even ?
				V4L2_FIELD_TOP : V4L2_FIELD_BOTTOM);
	fmt->fmt.pix.bytesperline = 0;
	fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static int zoran_g_fmt_vid_cap(struct file *file, void *__fh,
					struct v4l2_format *fmt)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;

	if (fh->map_mode != ZORAN_MAP_MODE_RAW)
		return zoran_g_fmt_vid_out(file, fh, fmt);

	fmt->fmt.pix.width = fh->v4l_settings.width;
	fmt->fmt.pix.height = fh->v4l_settings.height;
	fmt->fmt.pix.sizeimage = fh->v4l_settings.bytesperline *
					fh->v4l_settings.height;
	fmt->fmt.pix.pixelformat = fh->v4l_settings.format->fourcc;
	fmt->fmt.pix.colorspace = fh->v4l_settings.format->colorspace;
	fmt->fmt.pix.bytesperline = fh->v4l_settings.bytesperline;
	if (BUZ_MAX_HEIGHT < (fh->v4l_settings.height * 2))
		fmt->fmt.pix.field = V4L2_FIELD_INTERLACED;
	else
		fmt->fmt.pix.field = V4L2_FIELD_TOP;
	return 0;
}

static int zoran_g_fmt_vid_overlay(struct file *file, void *__fh,
					struct v4l2_format *fmt)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;

	fmt->fmt.win.w.left = fh->overlay_settings.x;
	fmt->fmt.win.w.top = fh->overlay_settings.y;
	fmt->fmt.win.w.width = fh->overlay_settings.width;
	fmt->fmt.win.w.height = fh->overlay_settings.height;
	if (fh->overlay_settings.width * 2 > BUZ_MAX_HEIGHT)
		fmt->fmt.win.field = V4L2_FIELD_INTERLACED;
	else
		fmt->fmt.win.field = V4L2_FIELD_TOP;

	return 0;
}

static int zoran_try_fmt_vid_overlay(struct file *file, void *__fh,
					struct v4l2_format *fmt)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;

	if (fmt->fmt.win.w.width > BUZ_MAX_WIDTH)
		fmt->fmt.win.w.width = BUZ_MAX_WIDTH;
	if (fmt->fmt.win.w.width < BUZ_MIN_WIDTH)
		fmt->fmt.win.w.width = BUZ_MIN_WIDTH;
	if (fmt->fmt.win.w.height > BUZ_MAX_HEIGHT)
		fmt->fmt.win.w.height = BUZ_MAX_HEIGHT;
	if (fmt->fmt.win.w.height < BUZ_MIN_HEIGHT)
		fmt->fmt.win.w.height = BUZ_MIN_HEIGHT;

	return 0;
}

static int zoran_try_fmt_vid_out(struct file *file, void *__fh,
					struct v4l2_format *fmt)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;
	struct zoran_jpg_settings settings;
	int res = 0;

	if (fmt->fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG)
		return -EINVAL;

	settings = fh->jpg_settings;

	/* we actually need to set 'real' parameters now */
	if ((fmt->fmt.pix.height * 2) > BUZ_MAX_HEIGHT)
		settings.TmpDcm = 1;
	else
		settings.TmpDcm = 2;
	settings.decimation = 0;
	if (fmt->fmt.pix.height <= fh->jpg_settings.img_height / 2)
		settings.VerDcm = 2;
	else
		settings.VerDcm = 1;
	if (fmt->fmt.pix.width <= fh->jpg_settings.img_width / 4)
		settings.HorDcm = 4;
	else if (fmt->fmt.pix.width <= fh->jpg_settings.img_width / 2)
		settings.HorDcm = 2;
	else
		settings.HorDcm = 1;
	if (settings.TmpDcm == 1)
		settings.field_per_buff = 2;
	else
		settings.field_per_buff = 1;

	if (settings.HorDcm > 1) {
		settings.img_x = (BUZ_MAX_WIDTH == 720) ? 8 : 0;
		settings.img_width = (BUZ_MAX_WIDTH == 720) ? 704 : BUZ_MAX_WIDTH;
	} else {
		settings.img_x = 0;
		settings.img_width = BUZ_MAX_WIDTH;
	}

	/* check */
	res = zoran_check_jpg_settings(zr, &settings, 1);
	if (res)
		return res;

	/* tell the user what we actually did */
	fmt->fmt.pix.width = settings.img_width / settings.HorDcm;
	fmt->fmt.pix.height = settings.img_height * 2 /
		(settings.TmpDcm * settings.VerDcm);
	if (settings.TmpDcm == 1)
		fmt->fmt.pix.field = (fh->jpg_settings.odd_even ?
				V4L2_FIELD_SEQ_TB : V4L2_FIELD_SEQ_BT);
	else
		fmt->fmt.pix.field = (fh->jpg_settings.odd_even ?
				V4L2_FIELD_TOP : V4L2_FIELD_BOTTOM);

	fmt->fmt.pix.sizeimage = zoran_v4l2_calc_bufsize(&settings);
	fmt->fmt.pix.bytesperline = 0;
	fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	return res;
}

static int zoran_try_fmt_vid_cap(struct file *file, void *__fh,
					struct v4l2_format *fmt)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;
	int bpp;
	int i;

	if (fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG)
		return zoran_try_fmt_vid_out(file, fh, fmt);

	for (i = 0; i < NUM_FORMATS; i++)
		if (zoran_formats[i].fourcc == fmt->fmt.pix.pixelformat)
			break;

	if (i == NUM_FORMATS)
		return -EINVAL;

	bpp = DIV_ROUND_UP(zoran_formats[i].depth, 8);
	v4l_bound_align_image(
		&fmt->fmt.pix.width, BUZ_MIN_WIDTH, BUZ_MAX_WIDTH, bpp == 2 ? 1 : 2,
		&fmt->fmt.pix.height, BUZ_MIN_HEIGHT, BUZ_MAX_HEIGHT, 0, 0);
	return 0;
}

static int zoran_s_fmt_vid_overlay(struct file *file, void *__fh,
					struct v4l2_format *fmt)
{
	struct zoran_fh *fh = __fh;

	dprintk(3, "x=%d, y=%d, w=%d, h=%d, cnt=%d, map=0x%p\n",
			fmt->fmt.win.w.left, fmt->fmt.win.w.top,
			fmt->fmt.win.w.width,
			fmt->fmt.win.w.height,
			fmt->fmt.win.clipcount,
			fmt->fmt.win.bitmap);
	return setup_window(fh, fmt->fmt.win.w.left, fmt->fmt.win.w.top,
			   fmt->fmt.win.w.width, fmt->fmt.win.w.height,
			   (struct v4l2_clip __user *)fmt->fmt.win.clips,
			   fmt->fmt.win.clipcount, fmt->fmt.win.bitmap);
}

static int zoran_s_fmt_vid_out(struct file *file, void *__fh,
					struct v4l2_format *fmt)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;
	__le32 printformat = __cpu_to_le32(fmt->fmt.pix.pixelformat);
	struct zoran_jpg_settings settings;
	int res = 0;

	dprintk(3, "size=%dx%d, fmt=0x%x (%4.4s)\n",
			fmt->fmt.pix.width, fmt->fmt.pix.height,
			fmt->fmt.pix.pixelformat,
			(char *) &printformat);
	if (fmt->fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG)
		return -EINVAL;

	if (fh->buffers.allocated) {
		dprintk(1, KERN_ERR "%s: VIDIOC_S_FMT - cannot change capture mode\n",
			ZR_DEVNAME(zr));
		return -EBUSY;
	}

	settings = fh->jpg_settings;

	/* we actually need to set 'real' parameters now */
	if (fmt->fmt.pix.height * 2 > BUZ_MAX_HEIGHT)
		settings.TmpDcm = 1;
	else
		settings.TmpDcm = 2;
	settings.decimation = 0;
	if (fmt->fmt.pix.height <= fh->jpg_settings.img_height / 2)
		settings.VerDcm = 2;
	else
		settings.VerDcm = 1;
	if (fmt->fmt.pix.width <= fh->jpg_settings.img_width / 4)
		settings.HorDcm = 4;
	else if (fmt->fmt.pix.width <= fh->jpg_settings.img_width / 2)
		settings.HorDcm = 2;
	else
		settings.HorDcm = 1;
	if (settings.TmpDcm == 1)
		settings.field_per_buff = 2;
	else
		settings.field_per_buff = 1;

	if (settings.HorDcm > 1) {
		settings.img_x = (BUZ_MAX_WIDTH == 720) ? 8 : 0;
		settings.img_width = (BUZ_MAX_WIDTH == 720) ? 704 : BUZ_MAX_WIDTH;
	} else {
		settings.img_x = 0;
		settings.img_width = BUZ_MAX_WIDTH;
	}

	/* check */
	res = zoran_check_jpg_settings(zr, &settings, 0);
	if (res)
		return res;

	/* it's ok, so set them */
	fh->jpg_settings = settings;

	map_mode_jpg(fh, fmt->type == V4L2_BUF_TYPE_VIDEO_OUTPUT);
	fh->buffers.buffer_size = zoran_v4l2_calc_bufsize(&fh->jpg_settings);

	/* tell the user what we actually did */
	fmt->fmt.pix.width = settings.img_width / settings.HorDcm;
	fmt->fmt.pix.height = settings.img_height * 2 /
		(settings.TmpDcm * settings.VerDcm);
	if (settings.TmpDcm == 1)
		fmt->fmt.pix.field = (fh->jpg_settings.odd_even ?
				V4L2_FIELD_SEQ_TB : V4L2_FIELD_SEQ_BT);
	else
		fmt->fmt.pix.field = (fh->jpg_settings.odd_even ?
				V4L2_FIELD_TOP : V4L2_FIELD_BOTTOM);
	fmt->fmt.pix.bytesperline = 0;
	fmt->fmt.pix.sizeimage = fh->buffers.buffer_size;
	fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	return res;
}

static int zoran_s_fmt_vid_cap(struct file *file, void *__fh,
					struct v4l2_format *fmt)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;
	int i;
	int res = 0;

	if (fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG)
		return zoran_s_fmt_vid_out(file, fh, fmt);

	for (i = 0; i < NUM_FORMATS; i++)
		if (fmt->fmt.pix.pixelformat == zoran_formats[i].fourcc)
			break;
	if (i == NUM_FORMATS) {
		dprintk(1, KERN_ERR "%s: VIDIOC_S_FMT - unknown/unsupported format 0x%x\n",
			ZR_DEVNAME(zr), fmt->fmt.pix.pixelformat);
		return -EINVAL;
	}

	if ((fh->map_mode != ZORAN_MAP_MODE_RAW && fh->buffers.allocated) ||
	    fh->buffers.active != ZORAN_FREE) {
		dprintk(1, KERN_ERR "%s: VIDIOC_S_FMT - cannot change capture mode\n",
				ZR_DEVNAME(zr));
		return -EBUSY;
	}
	if (fmt->fmt.pix.height > BUZ_MAX_HEIGHT)
		fmt->fmt.pix.height = BUZ_MAX_HEIGHT;
	if (fmt->fmt.pix.width > BUZ_MAX_WIDTH)
		fmt->fmt.pix.width = BUZ_MAX_WIDTH;

	map_mode_raw(fh);

	res = zoran_v4l_set_format(fh, fmt->fmt.pix.width, fmt->fmt.pix.height,
				   &zoran_formats[i]);
	if (res)
		return res;

	/* tell the user the results/missing stuff */
	fmt->fmt.pix.bytesperline = fh->v4l_settings.bytesperline;
	fmt->fmt.pix.sizeimage = fh->v4l_settings.height * fh->v4l_settings.bytesperline;
	fmt->fmt.pix.colorspace = fh->v4l_settings.format->colorspace;
	if (BUZ_MAX_HEIGHT < (fh->v4l_settings.height * 2))
		fmt->fmt.pix.field = V4L2_FIELD_INTERLACED;
	else
		fmt->fmt.pix.field = V4L2_FIELD_TOP;
	return res;
}

static int zoran_g_fbuf(struct file *file, void *__fh,
		struct v4l2_framebuffer *fb)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;

	memset(fb, 0, sizeof(*fb));
	fb->base = zr->vbuf_base;
	fb->fmt.width = zr->vbuf_width;
	fb->fmt.height = zr->vbuf_height;
	if (zr->overlay_settings.format)
		fb->fmt.pixelformat = fh->overlay_settings.format->fourcc;
	fb->fmt.bytesperline = zr->vbuf_bytesperline;
	fb->fmt.colorspace = V4L2_COLORSPACE_SRGB;
	fb->fmt.field = V4L2_FIELD_INTERLACED;
	fb->capability = V4L2_FBUF_CAP_LIST_CLIPPING;

	return 0;
}

static int zoran_s_fbuf(struct file *file, void *__fh,
		const struct v4l2_framebuffer *fb)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;
	int i;
	__le32 printformat = __cpu_to_le32(fb->fmt.pixelformat);

	for (i = 0; i < NUM_FORMATS; i++)
		if (zoran_formats[i].fourcc == fb->fmt.pixelformat)
			break;
	if (i == NUM_FORMATS) {
		dprintk(1, KERN_ERR "%s: VIDIOC_S_FBUF - format=0x%x (%4.4s) not allowed\n",
			ZR_DEVNAME(zr), fb->fmt.pixelformat,
			(char *)&printformat);
		return -EINVAL;
	}

	return setup_fbuffer(fh, fb->base, &zoran_formats[i], fb->fmt.width,
			    fb->fmt.height, fb->fmt.bytesperline);
}

static int zoran_overlay(struct file *file, void *__fh, unsigned int on)
{
	struct zoran_fh *fh = __fh;

	return setup_overlay(fh, on);
}

static int zoran_streamoff(struct file *file, void *__fh, enum v4l2_buf_type type);

static int zoran_reqbufs(struct file *file, void *__fh, struct v4l2_requestbuffers *req)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;
	int res = 0;

	if (req->memory != V4L2_MEMORY_MMAP) {
		dprintk(2,
				KERN_ERR
				"%s: only MEMORY_MMAP capture is supported, not %d\n",
				ZR_DEVNAME(zr), req->memory);
		return -EINVAL;
	}

	if (req->count == 0)
		return zoran_streamoff(file, fh, req->type);

	if (fh->buffers.allocated) {
		dprintk(2,
				KERN_ERR
				"%s: VIDIOC_REQBUFS - buffers already allocated\n",
				ZR_DEVNAME(zr));
		return -EBUSY;
	}

	if (fh->map_mode == ZORAN_MAP_MODE_RAW &&
	    req->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		/* control user input */
		if (req->count < 2)
			req->count = 2;
		if (req->count > v4l_nbufs)
			req->count = v4l_nbufs;

		/* The next mmap will map the V4L buffers */
		map_mode_raw(fh);
		fh->buffers.num_buffers = req->count;

		if (v4l_fbuffer_alloc(fh)) {
			return -ENOMEM;
		}
	} else if (fh->map_mode == ZORAN_MAP_MODE_JPG_REC ||
		   fh->map_mode == ZORAN_MAP_MODE_JPG_PLAY) {
		/* we need to calculate size ourselves now */
		if (req->count < 4)
			req->count = 4;
		if (req->count > jpg_nbufs)
			req->count = jpg_nbufs;

		/* The next mmap will map the MJPEG buffers */
		map_mode_jpg(fh, req->type == V4L2_BUF_TYPE_VIDEO_OUTPUT);
		fh->buffers.num_buffers = req->count;
		fh->buffers.buffer_size = zoran_v4l2_calc_bufsize(&fh->jpg_settings);

		if (jpg_fbuffer_alloc(fh)) {
			return -ENOMEM;
		}
	} else {
		dprintk(1,
				KERN_ERR
				"%s: VIDIOC_REQBUFS - unknown type %d\n",
				ZR_DEVNAME(zr), req->type);
		return -EINVAL;
	}
	return res;
}

static int zoran_querybuf(struct file *file, void *__fh, struct v4l2_buffer *buf)
{
	struct zoran_fh *fh = __fh;

	return zoran_v4l2_buffer_status(fh, buf, buf->index);
}

static int zoran_qbuf(struct file *file, void *__fh, struct v4l2_buffer *buf)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;
	int res = 0, codec_mode, buf_type;

	switch (fh->map_mode) {
	case ZORAN_MAP_MODE_RAW:
		if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			dprintk(1, KERN_ERR
				"%s: VIDIOC_QBUF - invalid buf->type=%d for map_mode=%d\n",
				ZR_DEVNAME(zr), buf->type, fh->map_mode);
			return -EINVAL;
		}

		res = zoran_v4l_queue_frame(fh, buf->index);
		if (res)
			return res;
		if (!zr->v4l_memgrab_active && fh->buffers.active == ZORAN_LOCKED)
			zr36057_set_memgrab(zr, 1);
		break;

	case ZORAN_MAP_MODE_JPG_REC:
	case ZORAN_MAP_MODE_JPG_PLAY:
		if (fh->map_mode == ZORAN_MAP_MODE_JPG_PLAY) {
			buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
			codec_mode = BUZ_MODE_MOTION_DECOMPRESS;
		} else {
			buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			codec_mode = BUZ_MODE_MOTION_COMPRESS;
		}

		if (buf->type != buf_type) {
			dprintk(1, KERN_ERR
				"%s: VIDIOC_QBUF - invalid buf->type=%d for map_mode=%d\n",
				ZR_DEVNAME(zr), buf->type, fh->map_mode);
			return -EINVAL;
		}

		res = zoran_jpg_queue_frame(fh, buf->index, codec_mode);
		if (res != 0)
			return res;
		if (zr->codec_mode == BUZ_MODE_IDLE &&
		    fh->buffers.active == ZORAN_LOCKED)
			zr36057_enable_jpg(zr, codec_mode);

		break;

	default:
		dprintk(1, KERN_ERR
			"%s: VIDIOC_QBUF - unsupported type %d\n",
			ZR_DEVNAME(zr), buf->type);
		res = -EINVAL;
		break;
	}
	return res;
}

static int zoran_dqbuf(struct file *file, void *__fh, struct v4l2_buffer *buf)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;
	int res = 0, buf_type, num = -1;	/* compiler borks here (?) */

	switch (fh->map_mode) {
	case ZORAN_MAP_MODE_RAW:
		if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			dprintk(1, KERN_ERR
				"%s: VIDIOC_QBUF - invalid buf->type=%d for map_mode=%d\n",
				ZR_DEVNAME(zr), buf->type, fh->map_mode);
			return -EINVAL;
		}

		num = zr->v4l_pend[zr->v4l_sync_tail & V4L_MASK_FRAME];
		if (file->f_flags & O_NONBLOCK &&
		    zr->v4l_buffers.buffer[num].state != BUZ_STATE_DONE) {
			return -EAGAIN;
		}
		res = v4l_sync(fh, num);
		if (res)
			return res;
		zr->v4l_sync_tail++;
		res = zoran_v4l2_buffer_status(fh, buf, num);
		break;

	case ZORAN_MAP_MODE_JPG_REC:
	case ZORAN_MAP_MODE_JPG_PLAY:
	{
		struct zoran_sync bs;

		if (fh->map_mode == ZORAN_MAP_MODE_JPG_PLAY)
			buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		else
			buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (buf->type != buf_type) {
			dprintk(1, KERN_ERR
				"%s: VIDIOC_QBUF - invalid buf->type=%d for map_mode=%d\n",
				ZR_DEVNAME(zr), buf->type, fh->map_mode);
			return -EINVAL;
		}

		num = zr->jpg_pend[zr->jpg_que_tail & BUZ_MASK_FRAME];

		if (file->f_flags & O_NONBLOCK &&
		    zr->jpg_buffers.buffer[num].state != BUZ_STATE_DONE) {
			return -EAGAIN;
		}
		bs.frame = 0; /* suppress compiler warning */
		res = jpg_sync(fh, &bs);
		if (res)
			return res;
		res = zoran_v4l2_buffer_status(fh, buf, bs.frame);
		break;
	}

	default:
		dprintk(1, KERN_ERR
			"%s: VIDIOC_DQBUF - unsupported type %d\n",
			ZR_DEVNAME(zr), buf->type);
		res = -EINVAL;
		break;
	}
	return res;
}

static int zoran_streamon(struct file *file, void *__fh, enum v4l2_buf_type type)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;
	int res = 0;

	switch (fh->map_mode) {
	case ZORAN_MAP_MODE_RAW:	/* raw capture */
		if (zr->v4l_buffers.active != ZORAN_ACTIVE ||
		    fh->buffers.active != ZORAN_ACTIVE) {
			return -EBUSY;
		}

		zr->v4l_buffers.active = fh->buffers.active = ZORAN_LOCKED;
		zr->v4l_settings = fh->v4l_settings;

		zr->v4l_sync_tail = zr->v4l_pend_tail;
		if (!zr->v4l_memgrab_active &&
		    zr->v4l_pend_head != zr->v4l_pend_tail) {
			zr36057_set_memgrab(zr, 1);
		}
		break;

	case ZORAN_MAP_MODE_JPG_REC:
	case ZORAN_MAP_MODE_JPG_PLAY:
		/* what is the codec mode right now? */
		if (zr->jpg_buffers.active != ZORAN_ACTIVE ||
		    fh->buffers.active != ZORAN_ACTIVE) {
			return -EBUSY;
		}

		zr->jpg_buffers.active = fh->buffers.active = ZORAN_LOCKED;

		if (zr->jpg_que_head != zr->jpg_que_tail) {
			/* Start the jpeg codec when the first frame is queued  */
			jpeg_start(zr);
		}
		break;

	default:
		dprintk(1,
			KERN_ERR
			"%s: VIDIOC_STREAMON - invalid map mode %d\n",
			ZR_DEVNAME(zr), fh->map_mode);
		res = -EINVAL;
		break;
	}
	return res;
}

static int zoran_streamoff(struct file *file, void *__fh, enum v4l2_buf_type type)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;
	int i, res = 0;
	unsigned long flags;

	switch (fh->map_mode) {
	case ZORAN_MAP_MODE_RAW:	/* raw capture */
		if (fh->buffers.active == ZORAN_FREE &&
		    zr->v4l_buffers.active != ZORAN_FREE) {
			return -EPERM;	/* stay off other's settings! */
		}
		if (zr->v4l_buffers.active == ZORAN_FREE)
			return res;

		spin_lock_irqsave(&zr->spinlock, flags);
		/* unload capture */
		if (zr->v4l_memgrab_active) {

			zr36057_set_memgrab(zr, 0);
		}

		for (i = 0; i < fh->buffers.num_buffers; i++)
			zr->v4l_buffers.buffer[i].state = BUZ_STATE_USER;
		fh->buffers = zr->v4l_buffers;

		zr->v4l_buffers.active = fh->buffers.active = ZORAN_FREE;

		zr->v4l_grab_seq = 0;
		zr->v4l_pend_head = zr->v4l_pend_tail = 0;
		zr->v4l_sync_tail = 0;

		spin_unlock_irqrestore(&zr->spinlock, flags);

		break;

	case ZORAN_MAP_MODE_JPG_REC:
	case ZORAN_MAP_MODE_JPG_PLAY:
		if (fh->buffers.active == ZORAN_FREE &&
		    zr->jpg_buffers.active != ZORAN_FREE) {
			return -EPERM;	/* stay off other's settings! */
		}
		if (zr->jpg_buffers.active == ZORAN_FREE)
			return res;

		res = jpg_qbuf(fh, -1,
			     (fh->map_mode == ZORAN_MAP_MODE_JPG_REC) ?
			     BUZ_MODE_MOTION_COMPRESS :
			     BUZ_MODE_MOTION_DECOMPRESS);
		if (res)
			return res;
		break;
	default:
		dprintk(1, KERN_ERR
			"%s: VIDIOC_STREAMOFF - invalid map mode %d\n",
			ZR_DEVNAME(zr), fh->map_mode);
		res = -EINVAL;
		break;
	}
	return res;
}
static int zoran_g_std(struct file *file, void *__fh, v4l2_std_id *std)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;

	*std = zr->norm;
	return 0;
}

static int zoran_s_std(struct file *file, void *__fh, v4l2_std_id std)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;
	int res = 0;

	res = zoran_set_norm(zr, std);
	if (res)
		return res;

	return wait_grab_pending(zr);
}

static int zoran_enum_input(struct file *file, void *__fh,
				 struct v4l2_input *inp)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;

	if (inp->index >= zr->card.inputs)
		return -EINVAL;

	strncpy(inp->name, zr->card.input[inp->index].name,
		sizeof(inp->name) - 1);
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_ALL;

	/* Get status of video decoder */
	decoder_call(zr, video, g_input_status, &inp->status);
	return 0;
}

static int zoran_g_input(struct file *file, void *__fh, unsigned int *input)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;

	*input = zr->input;

	return 0;
}

static int zoran_s_input(struct file *file, void *__fh, unsigned int input)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;
	int res;

	res = zoran_set_input(zr, input);
	if (res)
		return res;

	/* Make sure the changes come into effect */
	return wait_grab_pending(zr);
}

static int zoran_enum_output(struct file *file, void *__fh,
				  struct v4l2_output *outp)
{
	if (outp->index != 0)
		return -EINVAL;

	outp->index = 0;
	outp->type = V4L2_OUTPUT_TYPE_ANALOGVGAOVERLAY;
	strncpy(outp->name, "Autodetect", sizeof(outp->name)-1);

	return 0;
}

static int zoran_g_output(struct file *file, void *__fh, unsigned int *output)
{
	*output = 0;

	return 0;
}

static int zoran_s_output(struct file *file, void *__fh, unsigned int output)
{
	if (output != 0)
		return -EINVAL;

	return 0;
}

/* cropping (sub-frame capture) */
static int zoran_g_selection(struct file *file, void *__fh, struct v4l2_selection *sel)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (fh->map_mode == ZORAN_MAP_MODE_RAW) {
		dprintk(1, KERN_ERR
			"%s: VIDIOC_G_SELECTION - subcapture only supported for compressed capture\n",
			ZR_DEVNAME(zr));
		return -EINVAL;
	}

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r.top = fh->jpg_settings.img_y;
		sel->r.left = fh->jpg_settings.img_x;
		sel->r.width = fh->jpg_settings.img_width;
		sel->r.height = fh->jpg_settings.img_height;
		break;
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.top = sel->r.left = 0;
		sel->r.width = BUZ_MIN_WIDTH;
		sel->r.height = BUZ_MIN_HEIGHT;
		break;
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = sel->r.left = 0;
		sel->r.width = BUZ_MAX_WIDTH;
		sel->r.height = BUZ_MAX_HEIGHT;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int zoran_s_selection(struct file *file, void *__fh, struct v4l2_selection *sel)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;
	struct zoran_jpg_settings settings;
	int res;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	if (fh->map_mode == ZORAN_MAP_MODE_RAW) {
		dprintk(1, KERN_ERR
			"%s: VIDIOC_S_SELECTION - subcapture only supported for compressed capture\n",
			ZR_DEVNAME(zr));
		return -EINVAL;
	}

	settings = fh->jpg_settings;

	if (fh->buffers.allocated) {
		dprintk(1, KERN_ERR
			"%s: VIDIOC_S_SELECTION - cannot change settings while active\n",
			ZR_DEVNAME(zr));
		return -EBUSY;
	}

	/* move into a form that we understand */
	settings.img_x = sel->r.left;
	settings.img_y = sel->r.top;
	settings.img_width = sel->r.width;
	settings.img_height = sel->r.height;

	/* check validity */
	res = zoran_check_jpg_settings(zr, &settings, 0);
	if (res)
		return res;

	/* accept */
	fh->jpg_settings = settings;
	return res;
}

static int zoran_g_jpegcomp(struct file *file, void *__fh,
					struct v4l2_jpegcompression *params)
{
	struct zoran_fh *fh = __fh;
	memset(params, 0, sizeof(*params));

	params->quality = fh->jpg_settings.jpg_comp.quality;
	params->APPn = fh->jpg_settings.jpg_comp.APPn;
	memcpy(params->APP_data,
	       fh->jpg_settings.jpg_comp.APP_data,
	       fh->jpg_settings.jpg_comp.APP_len);
	params->APP_len = fh->jpg_settings.jpg_comp.APP_len;
	memcpy(params->COM_data,
	       fh->jpg_settings.jpg_comp.COM_data,
	       fh->jpg_settings.jpg_comp.COM_len);
	params->COM_len = fh->jpg_settings.jpg_comp.COM_len;
	params->jpeg_markers =
	    fh->jpg_settings.jpg_comp.jpeg_markers;

	return 0;
}

static int zoran_s_jpegcomp(struct file *file, void *__fh,
					const struct v4l2_jpegcompression *params)
{
	struct zoran_fh *fh = __fh;
	struct zoran *zr = fh->zr;
	int res = 0;
	struct zoran_jpg_settings settings;

	settings = fh->jpg_settings;

	settings.jpg_comp = *params;

	if (fh->buffers.active != ZORAN_FREE) {
		dprintk(1, KERN_WARNING
			"%s: VIDIOC_S_JPEGCOMP called while in playback/capture mode\n",
			ZR_DEVNAME(zr));
		return -EBUSY;
	}

	res = zoran_check_jpg_settings(zr, &settings, 0);
	if (res)
		return res;
	if (!fh->buffers.allocated)
		fh->buffers.buffer_size =
			zoran_v4l2_calc_bufsize(&fh->jpg_settings);
	fh->jpg_settings.jpg_comp = settings.jpg_comp;
	return res;
}

static __poll_t
zoran_poll (struct file *file,
	    poll_table  *wait)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	__poll_t res = v4l2_ctrl_poll(file, wait);
	int frame;
	unsigned long flags;

	/* we should check whether buffers are ready to be synced on
	 * (w/o waits - O_NONBLOCK) here
	 * if ready for read (sync), return EPOLLIN|EPOLLRDNORM,
	 * if ready for write (sync), return EPOLLOUT|EPOLLWRNORM,
	 * if error, return EPOLLERR,
	 * if no buffers queued or so, return EPOLLNVAL
	 */

	switch (fh->map_mode) {
	case ZORAN_MAP_MODE_RAW:
		poll_wait(file, &zr->v4l_capq, wait);
		frame = zr->v4l_pend[zr->v4l_sync_tail & V4L_MASK_FRAME];

		spin_lock_irqsave(&zr->spinlock, flags);
		dprintk(3,
			KERN_DEBUG
			"%s: %s() raw - active=%c, sync_tail=%lu/%c, pend_tail=%lu, pend_head=%lu\n",
			ZR_DEVNAME(zr), __func__,
			"FAL"[fh->buffers.active], zr->v4l_sync_tail,
			"UPMD"[zr->v4l_buffers.buffer[frame].state],
			zr->v4l_pend_tail, zr->v4l_pend_head);
		/* Process is the one capturing? */
		if (fh->buffers.active != ZORAN_FREE &&
		    /* Buffer ready to DQBUF? */
		    zr->v4l_buffers.buffer[frame].state == BUZ_STATE_DONE)
			res |= EPOLLIN | EPOLLRDNORM;
		spin_unlock_irqrestore(&zr->spinlock, flags);

		break;

	case ZORAN_MAP_MODE_JPG_REC:
	case ZORAN_MAP_MODE_JPG_PLAY:
		poll_wait(file, &zr->jpg_capq, wait);
		frame = zr->jpg_pend[zr->jpg_que_tail & BUZ_MASK_FRAME];

		spin_lock_irqsave(&zr->spinlock, flags);
		dprintk(3,
			KERN_DEBUG
			"%s: %s() jpg - active=%c, que_tail=%lu/%c, que_head=%lu, dma=%lu/%lu\n",
			ZR_DEVNAME(zr), __func__,
			"FAL"[fh->buffers.active], zr->jpg_que_tail,
			"UPMD"[zr->jpg_buffers.buffer[frame].state],
			zr->jpg_que_head, zr->jpg_dma_tail, zr->jpg_dma_head);
		if (fh->buffers.active != ZORAN_FREE &&
		    zr->jpg_buffers.buffer[frame].state == BUZ_STATE_DONE) {
			if (fh->map_mode == ZORAN_MAP_MODE_JPG_REC)
				res |= EPOLLIN | EPOLLRDNORM;
			else
				res |= EPOLLOUT | EPOLLWRNORM;
		}
		spin_unlock_irqrestore(&zr->spinlock, flags);

		break;

	default:
		dprintk(1,
			KERN_ERR
			"%s: %s - internal error, unknown map_mode=%d\n",
			ZR_DEVNAME(zr), __func__, fh->map_mode);
		res |= EPOLLERR;
	}

	return res;
}


/*
 * This maps the buffers to user space.
 *
 * Depending on the state of fh->map_mode
 * the V4L or the MJPEG buffers are mapped
 * per buffer or all together
 *
 * Note that we need to connect to some
 * unmap signal event to unmap the de-allocate
 * the buffer accordingly (zoran_vm_close())
 */

static void
zoran_vm_open (struct vm_area_struct *vma)
{
	struct zoran_mapping *map = vma->vm_private_data;
	atomic_inc(&map->count);
}

static void
zoran_vm_close (struct vm_area_struct *vma)
{
	struct zoran_mapping *map = vma->vm_private_data;
	struct zoran_fh *fh = map->fh;
	struct zoran *zr = fh->zr;
	int i;

	dprintk(3, KERN_INFO "%s: %s - munmap(%s)\n", ZR_DEVNAME(zr),
		__func__, mode_name(fh->map_mode));

	for (i = 0; i < fh->buffers.num_buffers; i++) {
		if (fh->buffers.buffer[i].map == map)
			fh->buffers.buffer[i].map = NULL;
	}
	kfree(map);

	/* Any buffers still mapped? */
	for (i = 0; i < fh->buffers.num_buffers; i++) {
		if (fh->buffers.buffer[i].map) {
			return;
		}
	}

	dprintk(3, KERN_INFO "%s: %s - free %s buffers\n", ZR_DEVNAME(zr),
		__func__, mode_name(fh->map_mode));

	if (fh->map_mode == ZORAN_MAP_MODE_RAW) {
		if (fh->buffers.active != ZORAN_FREE) {
			unsigned long flags;

			spin_lock_irqsave(&zr->spinlock, flags);
			zr36057_set_memgrab(zr, 0);
			zr->v4l_buffers.allocated = 0;
			zr->v4l_buffers.active = fh->buffers.active = ZORAN_FREE;
			spin_unlock_irqrestore(&zr->spinlock, flags);
		}
		v4l_fbuffer_free(fh);
	} else {
		if (fh->buffers.active != ZORAN_FREE) {
			jpg_qbuf(fh, -1, zr->codec_mode);
			zr->jpg_buffers.allocated = 0;
			zr->jpg_buffers.active = fh->buffers.active = ZORAN_FREE;
		}
		jpg_fbuffer_free(fh);
	}
}

static const struct vm_operations_struct zoran_vm_ops = {
	.open = zoran_vm_open,
	.close = zoran_vm_close,
};

static int
zoran_mmap (struct file           *file,
	    struct vm_area_struct *vma)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	unsigned long size = (vma->vm_end - vma->vm_start);
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	int i, j;
	unsigned long page, start = vma->vm_start, todo, pos, fraglen;
	int first, last;
	struct zoran_mapping *map;
	int res = 0;

	dprintk(3,
		KERN_INFO "%s: %s(%s) of 0x%08lx-0x%08lx (size=%lu)\n",
		ZR_DEVNAME(zr), __func__,
		mode_name(fh->map_mode), vma->vm_start, vma->vm_end, size);

	if (!(vma->vm_flags & VM_SHARED) || !(vma->vm_flags & VM_READ) ||
	    !(vma->vm_flags & VM_WRITE)) {
		dprintk(1,
			KERN_ERR
			"%s: %s - no MAP_SHARED/PROT_{READ,WRITE} given\n",
			ZR_DEVNAME(zr), __func__);
		return -EINVAL;
	}

	if (!fh->buffers.allocated) {
		dprintk(1,
			KERN_ERR
			"%s: %s(%s) - buffers not yet allocated\n",
			ZR_DEVNAME(zr), __func__, mode_name(fh->map_mode));
		return -ENOMEM;
	}

	first = offset / fh->buffers.buffer_size;
	last = first - 1 + size / fh->buffers.buffer_size;
	if (offset % fh->buffers.buffer_size != 0 ||
	    size % fh->buffers.buffer_size != 0 || first < 0 ||
	    last < 0 || first >= fh->buffers.num_buffers ||
	    last >= fh->buffers.buffer_size) {
		dprintk(1,
			KERN_ERR
			"%s: %s(%s) - offset=%lu or size=%lu invalid for bufsize=%d and numbufs=%d\n",
			ZR_DEVNAME(zr), __func__, mode_name(fh->map_mode), offset, size,
			fh->buffers.buffer_size,
			fh->buffers.num_buffers);
		return -EINVAL;
	}

	/* Check if any buffers are already mapped */
	for (i = first; i <= last; i++) {
		if (fh->buffers.buffer[i].map) {
			dprintk(1,
				KERN_ERR
				"%s: %s(%s) - buffer %d already mapped\n",
				ZR_DEVNAME(zr), __func__, mode_name(fh->map_mode), i);
			return -EBUSY;
		}
	}

	/* map these buffers */
	map = kmalloc(sizeof(struct zoran_mapping), GFP_KERNEL);
	if (!map) {
		return -ENOMEM;
	}
	map->fh = fh;
	atomic_set(&map->count, 1);

	vma->vm_ops = &zoran_vm_ops;
	vma->vm_flags |= VM_DONTEXPAND;
	vma->vm_private_data = map;

	if (fh->map_mode == ZORAN_MAP_MODE_RAW) {
		for (i = first; i <= last; i++) {
			todo = size;
			if (todo > fh->buffers.buffer_size)
				todo = fh->buffers.buffer_size;
			page = fh->buffers.buffer[i].v4l.fbuffer_phys;
			if (remap_pfn_range(vma, start, page >> PAGE_SHIFT,
							todo, PAGE_SHARED)) {
				dprintk(1,
					KERN_ERR
					"%s: %s(V4L) - remap_pfn_range failed\n",
					ZR_DEVNAME(zr), __func__);
				return -EAGAIN;
			}
			size -= todo;
			start += todo;
			fh->buffers.buffer[i].map = map;
			if (size == 0)
				break;
		}
	} else {
		for (i = first; i <= last; i++) {
			for (j = 0;
			     j < fh->buffers.buffer_size / PAGE_SIZE;
			     j++) {
				fraglen =
				    (le32_to_cpu(fh->buffers.buffer[i].jpg.
				     frag_tab[2 * j + 1]) & ~1) << 1;
				todo = size;
				if (todo > fraglen)
					todo = fraglen;
				pos =
				    le32_to_cpu(fh->buffers.
				    buffer[i].jpg.frag_tab[2 * j]);
				/* should just be pos on i386 */
				page = virt_to_phys(bus_to_virt(pos))
								>> PAGE_SHIFT;
				if (remap_pfn_range(vma, start, page,
							todo, PAGE_SHARED)) {
					dprintk(1,
						KERN_ERR
						"%s: %s(V4L) - remap_pfn_range failed\n",
						ZR_DEVNAME(zr), __func__);
					return -EAGAIN;
				}
				size -= todo;
				start += todo;
				if (size == 0)
					break;
				if (le32_to_cpu(fh->buffers.buffer[i].jpg.
				    frag_tab[2 * j + 1]) & 1)
					break;	/* was last fragment */
			}
			fh->buffers.buffer[i].map = map;
			if (size == 0)
				break;

		}
	}
	return res;
}

static const struct v4l2_ioctl_ops zoran_ioctl_ops = {
	.vidioc_querycap		    = zoran_querycap,
	.vidioc_s_selection		    = zoran_s_selection,
	.vidioc_g_selection		    = zoran_g_selection,
	.vidioc_enum_input		    = zoran_enum_input,
	.vidioc_g_input			    = zoran_g_input,
	.vidioc_s_input			    = zoran_s_input,
	.vidioc_enum_output		    = zoran_enum_output,
	.vidioc_g_output		    = zoran_g_output,
	.vidioc_s_output		    = zoran_s_output,
	.vidioc_g_fbuf			    = zoran_g_fbuf,
	.vidioc_s_fbuf			    = zoran_s_fbuf,
	.vidioc_g_std			    = zoran_g_std,
	.vidioc_s_std			    = zoran_s_std,
	.vidioc_g_jpegcomp		    = zoran_g_jpegcomp,
	.vidioc_s_jpegcomp		    = zoran_s_jpegcomp,
	.vidioc_overlay			    = zoran_overlay,
	.vidioc_reqbufs			    = zoran_reqbufs,
	.vidioc_querybuf		    = zoran_querybuf,
	.vidioc_qbuf			    = zoran_qbuf,
	.vidioc_dqbuf			    = zoran_dqbuf,
	.vidioc_streamon		    = zoran_streamon,
	.vidioc_streamoff		    = zoran_streamoff,
	.vidioc_enum_fmt_vid_cap	    = zoran_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_out	    = zoran_enum_fmt_vid_out,
	.vidioc_enum_fmt_vid_overlay	    = zoran_enum_fmt_vid_overlay,
	.vidioc_g_fmt_vid_cap		    = zoran_g_fmt_vid_cap,
	.vidioc_g_fmt_vid_out               = zoran_g_fmt_vid_out,
	.vidioc_g_fmt_vid_overlay           = zoran_g_fmt_vid_overlay,
	.vidioc_s_fmt_vid_cap		    = zoran_s_fmt_vid_cap,
	.vidioc_s_fmt_vid_out               = zoran_s_fmt_vid_out,
	.vidioc_s_fmt_vid_overlay           = zoran_s_fmt_vid_overlay,
	.vidioc_try_fmt_vid_cap		    = zoran_try_fmt_vid_cap,
	.vidioc_try_fmt_vid_out		    = zoran_try_fmt_vid_out,
	.vidioc_try_fmt_vid_overlay	    = zoran_try_fmt_vid_overlay,
	.vidioc_subscribe_event             = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event           = v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations zoran_fops = {
	.owner = THIS_MODULE,
	.open = zoran_open,
	.release = zoran_close,
	.unlocked_ioctl = video_ioctl2,
	.mmap = zoran_mmap,
	.poll = zoran_poll,
};

const struct video_device zoran_template = {
	.name = ZORAN_NAME,
	.fops = &zoran_fops,
	.ioctl_ops = &zoran_ioctl_ops,
	.release = &zoran_vdev_release,
	.tvnorms = V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM,
};

