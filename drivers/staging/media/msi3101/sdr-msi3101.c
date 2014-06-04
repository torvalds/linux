/*
 * Mirics MSi3101 SDR Dongle driver
 *
 * Copyright (C) 2013 Antti Palosaari <crope@iki.fi>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * That driver is somehow based of pwc driver:
 *  (C) 1999-2004 Nemosoft Unv.
 *  (C) 2004-2006 Luc Saillard (luc@saillard.org)
 *  (C) 2011 Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <asm/div64.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <linux/usb.h>
#include <media/videobuf2-vmalloc.h>
#include <linux/spi/spi.h>

/*
 *   iConfiguration          0
 *     bInterfaceNumber        0
 *     bAlternateSetting       1
 *     bNumEndpoints           1
 *       bEndpointAddress     0x81  EP 1 IN
 *       bmAttributes            1
 *         Transfer Type            Isochronous
 *       wMaxPacketSize     0x1400  3x 1024 bytes
 *       bInterval               1
 */
#define MAX_ISO_BUFS            (8)
#define ISO_FRAMES_PER_DESC     (8)
#define ISO_MAX_FRAME_SIZE      (3 * 1024)
#define ISO_BUFFER_SIZE         (ISO_FRAMES_PER_DESC * ISO_MAX_FRAME_SIZE)
#define MAX_ISOC_ERRORS         20

/* TODO: These should be moved to V4L2 API */
#define V4L2_PIX_FMT_SDR_S8     v4l2_fourcc('D', 'S', '0', '8') /* signed 8-bit */
#define V4L2_PIX_FMT_SDR_S12    v4l2_fourcc('D', 'S', '1', '2') /* signed 12-bit */
#define V4L2_PIX_FMT_SDR_S14    v4l2_fourcc('D', 'S', '1', '4') /* signed 14-bit */
#define V4L2_PIX_FMT_SDR_MSI2500_384 v4l2_fourcc('M', '3', '8', '4') /* Mirics MSi2500 format 384 */

static const struct v4l2_frequency_band bands[] = {
	{
		.tuner = 0,
		.type = V4L2_TUNER_ADC,
		.index = 0,
		.capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   =  1200000,
		.rangehigh  = 15000000,
	},
};

/* stream formats */
struct msi3101_format {
	char	*name;
	u32	pixelformat;
};

/* format descriptions for capture and preview */
static struct msi3101_format formats[] = {
	{
		.name		= "IQ U8",
		.pixelformat	= V4L2_SDR_FMT_CU8,
	}, {
		.name		= "IQ U16LE",
		.pixelformat	=  V4L2_SDR_FMT_CU16LE,
#if 0
	}, {
		.name		= "8-bit signed",
		.pixelformat	= V4L2_PIX_FMT_SDR_S8,
	}, {
		.name		= "10+2-bit signed",
		.pixelformat	= V4L2_PIX_FMT_SDR_MSI2500_384,
	}, {
		.name		= "12-bit signed",
		.pixelformat	= V4L2_PIX_FMT_SDR_S12,
	}, {
		.name		= "14-bit signed",
		.pixelformat	= V4L2_PIX_FMT_SDR_S14,
#endif
	},
};

static const unsigned int NUM_FORMATS = ARRAY_SIZE(formats);

/* intermediate buffers with raw data from the USB device */
struct msi3101_frame_buf {
	struct vb2_buffer vb;   /* common v4l buffer stuff -- must be first */
	struct list_head list;
};

struct msi3101_state {
	struct video_device vdev;
	struct v4l2_device v4l2_dev;
	struct v4l2_subdev *v4l2_subdev;
	struct spi_master *master;

	/* videobuf2 queue and queued buffers list */
	struct vb2_queue vb_queue;
	struct list_head queued_bufs;
	spinlock_t queued_bufs_lock; /* Protects queued_bufs */

	/* Note if taking both locks v4l2_lock must always be locked first! */
	struct mutex v4l2_lock;      /* Protects everything else */
	struct mutex vb_queue_lock;  /* Protects vb_queue and capt_file */

	/* Pointer to our usb_device, will be NULL after unplug */
	struct usb_device *udev; /* Both mutexes most be hold when setting! */

	unsigned int f_adc;
	u32 pixelformat;

	unsigned int isoc_errors; /* number of contiguous ISOC errors */
	unsigned int vb_full; /* vb is full and packets dropped */

	struct urb *urbs[MAX_ISO_BUFS];
	int (*convert_stream)(struct msi3101_state *s, u8 *dst, u8 *src,
			unsigned int src_len);

	/* Controls */
	struct v4l2_ctrl_handler hdl;

	u32 next_sample; /* for track lost packets */
	u32 sample; /* for sample rate calc */
	unsigned long jiffies_next;
	unsigned int sample_ctrl_bit[4];
};

/* Private functions */
static struct msi3101_frame_buf *msi3101_get_next_fill_buf(
		struct msi3101_state *s)
{
	unsigned long flags = 0;
	struct msi3101_frame_buf *buf = NULL;

	spin_lock_irqsave(&s->queued_bufs_lock, flags);
	if (list_empty(&s->queued_bufs))
		goto leave;

	buf = list_entry(s->queued_bufs.next, struct msi3101_frame_buf, list);
	list_del(&buf->list);
leave:
	spin_unlock_irqrestore(&s->queued_bufs_lock, flags);
	return buf;
}

/*
 * +===========================================================================
 * |   00-1023 | USB packet type '504'
 * +===========================================================================
 * |   00-  03 | sequence number of first sample in that USB packet
 * +---------------------------------------------------------------------------
 * |   04-  15 | garbage
 * +---------------------------------------------------------------------------
 * |   16-1023 | samples
 * +---------------------------------------------------------------------------
 * signed 8-bit sample
 * 504 * 2 = 1008 samples
 */
static int msi3101_convert_stream_504(struct msi3101_state *s, u8 *dst,
		u8 *src, unsigned int src_len)
{
	int i, i_max, dst_len = 0;
	u32 sample_num[3];

	/* There could be 1-3 1024 bytes URB frames */
	i_max = src_len / 1024;

	for (i = 0; i < i_max; i++) {
		sample_num[i] = src[3] << 24 | src[2] << 16 | src[1] << 8 | src[0] << 0;
		if (i == 0 && s->next_sample != sample_num[0]) {
			dev_dbg_ratelimited(&s->udev->dev,
					"%d samples lost, %d %08x:%08x\n",
					sample_num[0] - s->next_sample,
					src_len, s->next_sample, sample_num[0]);
		}

		/*
		 * Dump all unknown 'garbage' data - maybe we will discover
		 * someday if there is something rational...
		 */
		dev_dbg_ratelimited(&s->udev->dev, "%*ph\n", 12, &src[4]);

		/* 504 x I+Q samples */
		src += 16;
		memcpy(dst, src, 1008);
		src += 1008;
		dst += 1008;
		dst_len += 1008;
	}

	/* calculate samping rate and output it in 10 seconds intervals */
	if ((s->jiffies_next + msecs_to_jiffies(10000)) <= jiffies) {
		unsigned long jiffies_now = jiffies;
		unsigned long msecs = jiffies_to_msecs(jiffies_now) - jiffies_to_msecs(s->jiffies_next);
		unsigned int samples = sample_num[i_max - 1] - s->sample;
		s->jiffies_next = jiffies_now;
		s->sample = sample_num[i_max - 1];
		dev_dbg(&s->udev->dev,
				"slen=%d samples=%u msecs=%lu sampling rate=%lu\n",
				src_len, samples, msecs,
				samples * 1000UL / msecs);
	}

	/* next sample (sample = sample + i * 504) */
	s->next_sample = sample_num[i_max - 1] + 504;

	return dst_len;
}

static int msi3101_convert_stream_504_u8(struct msi3101_state *s, u8 *dst,
		u8 *src, unsigned int src_len)
{
	int i, j, i_max, dst_len = 0;
	u32 sample_num[3];
	s8 *s8src;
	u8 *u8dst;

	/* There could be 1-3 1024 bytes URB frames */
	i_max = src_len / 1024;
	u8dst = (u8 *) dst;

	for (i = 0; i < i_max; i++) {
		sample_num[i] = src[3] << 24 | src[2] << 16 | src[1] << 8 | src[0] << 0;
		if (i == 0 && s->next_sample != sample_num[0]) {
			dev_dbg_ratelimited(&s->udev->dev,
					"%d samples lost, %d %08x:%08x\n",
					sample_num[0] - s->next_sample,
					src_len, s->next_sample, sample_num[0]);
		}

		/*
		 * Dump all unknown 'garbage' data - maybe we will discover
		 * someday if there is something rational...
		 */
		dev_dbg_ratelimited(&s->udev->dev, "%*ph\n", 12, &src[4]);

		/* 504 x I+Q samples */
		src += 16;

		s8src = (s8 *) src;
		for (j = 0; j < 1008; j++)
			*u8dst++ = *s8src++ + 128;

		src += 1008;
		dst += 1008;
		dst_len += 1008;
	}

	/* calculate samping rate and output it in 10 seconds intervals */
	if (unlikely(time_is_before_jiffies(s->jiffies_next))) {
#define MSECS 10000UL
		unsigned int samples = sample_num[i_max - 1] - s->sample;
		s->jiffies_next = jiffies + msecs_to_jiffies(MSECS);
		s->sample = sample_num[i_max - 1];
		dev_dbg(&s->udev->dev,
				"slen=%d samples=%u msecs=%lu sampling rate=%lu\n",
				src_len, samples, MSECS,
				samples * 1000UL / MSECS);
	}

	/* next sample (sample = sample + i * 504) */
	s->next_sample = sample_num[i_max - 1] + 504;

	return dst_len;
}

/*
 * +===========================================================================
 * |   00-1023 | USB packet type '384'
 * +===========================================================================
 * |   00-  03 | sequence number of first sample in that USB packet
 * +---------------------------------------------------------------------------
 * |   04-  15 | garbage
 * +---------------------------------------------------------------------------
 * |   16- 175 | samples
 * +---------------------------------------------------------------------------
 * |  176- 179 | control bits for previous samples
 * +---------------------------------------------------------------------------
 * |  180- 339 | samples
 * +---------------------------------------------------------------------------
 * |  340- 343 | control bits for previous samples
 * +---------------------------------------------------------------------------
 * |  344- 503 | samples
 * +---------------------------------------------------------------------------
 * |  504- 507 | control bits for previous samples
 * +---------------------------------------------------------------------------
 * |  508- 667 | samples
 * +---------------------------------------------------------------------------
 * |  668- 671 | control bits for previous samples
 * +---------------------------------------------------------------------------
 * |  672- 831 | samples
 * +---------------------------------------------------------------------------
 * |  832- 835 | control bits for previous samples
 * +---------------------------------------------------------------------------
 * |  836- 995 | samples
 * +---------------------------------------------------------------------------
 * |  996- 999 | control bits for previous samples
 * +---------------------------------------------------------------------------
 * | 1000-1023 | garbage
 * +---------------------------------------------------------------------------
 *
 * Bytes 4 - 7 could have some meaning?
 *
 * Control bits for previous samples is 32-bit field, containing 16 x 2-bit
 * numbers. This results one 2-bit number for 8 samples. It is likely used for
 * for bit shifting sample by given bits, increasing actual sampling resolution.
 * Number 2 (0b10) was never seen.
 *
 * 6 * 16 * 2 * 4 = 768 samples. 768 * 4 = 3072 bytes
 */
static int msi3101_convert_stream_384(struct msi3101_state *s, u8 *dst,
		u8 *src, unsigned int src_len)
{
	int i, i_max, dst_len = 0;
	u32 sample_num[3];

	/* There could be 1-3 1024 bytes URB frames */
	i_max = src_len / 1024;
	for (i = 0; i < i_max; i++) {
		sample_num[i] = src[3] << 24 | src[2] << 16 | src[1] << 8 | src[0] << 0;
		if (i == 0 && s->next_sample != sample_num[0]) {
			dev_dbg_ratelimited(&s->udev->dev,
					"%d samples lost, %d %08x:%08x\n",
					sample_num[0] - s->next_sample,
					src_len, s->next_sample, sample_num[0]);
		}

		/*
		 * Dump all unknown 'garbage' data - maybe we will discover
		 * someday if there is something rational...
		 */
		dev_dbg_ratelimited(&s->udev->dev,
				"%*ph  %*ph\n", 12, &src[4], 24, &src[1000]);

		/* 384 x I+Q samples */
		src += 16;
		memcpy(dst, src, 984);
		src += 984 + 24;
		dst += 984;
		dst_len += 984;
	}

	/* calculate samping rate and output it in 10 seconds intervals */
	if ((s->jiffies_next + msecs_to_jiffies(10000)) <= jiffies) {
		unsigned long jiffies_now = jiffies;
		unsigned long msecs = jiffies_to_msecs(jiffies_now) - jiffies_to_msecs(s->jiffies_next);
		unsigned int samples = sample_num[i_max - 1] - s->sample;
		s->jiffies_next = jiffies_now;
		s->sample = sample_num[i_max - 1];
		dev_dbg(&s->udev->dev,
				"slen=%d samples=%u msecs=%lu sampling rate=%lu bits=%d.%d.%d.%d\n",
				src_len, samples, msecs,
				samples * 1000UL / msecs,
				s->sample_ctrl_bit[0], s->sample_ctrl_bit[1],
				s->sample_ctrl_bit[2], s->sample_ctrl_bit[3]);
	}

	/* next sample (sample = sample + i * 384) */
	s->next_sample = sample_num[i_max - 1] + 384;

	return dst_len;
}

/*
 * +===========================================================================
 * |   00-1023 | USB packet type '336'
 * +===========================================================================
 * |   00-  03 | sequence number of first sample in that USB packet
 * +---------------------------------------------------------------------------
 * |   04-  15 | garbage
 * +---------------------------------------------------------------------------
 * |   16-1023 | samples
 * +---------------------------------------------------------------------------
 * signed 12-bit sample
 */
static int msi3101_convert_stream_336(struct msi3101_state *s, u8 *dst,
		u8 *src, unsigned int src_len)
{
	int i, i_max, dst_len = 0;
	u32 sample_num[3];

	/* There could be 1-3 1024 bytes URB frames */
	i_max = src_len / 1024;

	for (i = 0; i < i_max; i++) {
		sample_num[i] = src[3] << 24 | src[2] << 16 | src[1] << 8 | src[0] << 0;
		if (i == 0 && s->next_sample != sample_num[0]) {
			dev_dbg_ratelimited(&s->udev->dev,
					"%d samples lost, %d %08x:%08x\n",
					sample_num[0] - s->next_sample,
					src_len, s->next_sample, sample_num[0]);
		}

		/*
		 * Dump all unknown 'garbage' data - maybe we will discover
		 * someday if there is something rational...
		 */
		dev_dbg_ratelimited(&s->udev->dev, "%*ph\n", 12, &src[4]);

		/* 336 x I+Q samples */
		src += 16;
		memcpy(dst, src, 1008);
		src += 1008;
		dst += 1008;
		dst_len += 1008;
	}

	/* calculate samping rate and output it in 10 seconds intervals */
	if ((s->jiffies_next + msecs_to_jiffies(10000)) <= jiffies) {
		unsigned long jiffies_now = jiffies;
		unsigned long msecs = jiffies_to_msecs(jiffies_now) - jiffies_to_msecs(s->jiffies_next);
		unsigned int samples = sample_num[i_max - 1] - s->sample;
		s->jiffies_next = jiffies_now;
		s->sample = sample_num[i_max - 1];
		dev_dbg(&s->udev->dev,
				"slen=%d samples=%u msecs=%lu sampling rate=%lu\n",
				src_len, samples, msecs,
				samples * 1000UL / msecs);
	}

	/* next sample (sample = sample + i * 336) */
	s->next_sample = sample_num[i_max - 1] + 336;

	return dst_len;
}

/*
 * +===========================================================================
 * |   00-1023 | USB packet type '252'
 * +===========================================================================
 * |   00-  03 | sequence number of first sample in that USB packet
 * +---------------------------------------------------------------------------
 * |   04-  15 | garbage
 * +---------------------------------------------------------------------------
 * |   16-1023 | samples
 * +---------------------------------------------------------------------------
 * signed 14-bit sample
 */
static int msi3101_convert_stream_252(struct msi3101_state *s, u8 *dst,
		u8 *src, unsigned int src_len)
{
	int i, i_max, dst_len = 0;
	u32 sample_num[3];

	/* There could be 1-3 1024 bytes URB frames */
	i_max = src_len / 1024;

	for (i = 0; i < i_max; i++) {
		sample_num[i] = src[3] << 24 | src[2] << 16 | src[1] << 8 | src[0] << 0;
		if (i == 0 && s->next_sample != sample_num[0]) {
			dev_dbg_ratelimited(&s->udev->dev,
					"%d samples lost, %d %08x:%08x\n",
					sample_num[0] - s->next_sample,
					src_len, s->next_sample, sample_num[0]);
		}

		/*
		 * Dump all unknown 'garbage' data - maybe we will discover
		 * someday if there is something rational...
		 */
		dev_dbg_ratelimited(&s->udev->dev, "%*ph\n", 12, &src[4]);

		/* 252 x I+Q samples */
		src += 16;
		memcpy(dst, src, 1008);
		src += 1008;
		dst += 1008;
		dst_len += 1008;
	}

	/* calculate samping rate and output it in 10 seconds intervals */
	if ((s->jiffies_next + msecs_to_jiffies(10000)) <= jiffies) {
		unsigned long jiffies_now = jiffies;
		unsigned long msecs = jiffies_to_msecs(jiffies_now) - jiffies_to_msecs(s->jiffies_next);
		unsigned int samples = sample_num[i_max - 1] - s->sample;
		s->jiffies_next = jiffies_now;
		s->sample = sample_num[i_max - 1];
		dev_dbg(&s->udev->dev,
				"slen=%d samples=%u msecs=%lu sampling rate=%lu\n",
				src_len, samples, msecs,
				samples * 1000UL / msecs);
	}

	/* next sample (sample = sample + i * 252) */
	s->next_sample = sample_num[i_max - 1] + 252;

	return dst_len;
}

static int msi3101_convert_stream_252_u16(struct msi3101_state *s, u8 *dst,
		u8 *src, unsigned int src_len)
{
	int i, j, i_max, dst_len = 0;
	u32 sample_num[3];
	u16 *u16dst = (u16 *) dst;
	struct {signed int x:14;} se;

	/* There could be 1-3 1024 bytes URB frames */
	i_max = src_len / 1024;

	for (i = 0; i < i_max; i++) {
		sample_num[i] = src[3] << 24 | src[2] << 16 | src[1] << 8 | src[0] << 0;
		if (i == 0 && s->next_sample != sample_num[0]) {
			dev_dbg_ratelimited(&s->udev->dev,
					"%d samples lost, %d %08x:%08x\n",
					sample_num[0] - s->next_sample,
					src_len, s->next_sample, sample_num[0]);
		}

		/*
		 * Dump all unknown 'garbage' data - maybe we will discover
		 * someday if there is something rational...
		 */
		dev_dbg_ratelimited(&s->udev->dev, "%*ph\n", 12, &src[4]);

		/* 252 x I+Q samples */
		src += 16;

		for (j = 0; j < 1008; j += 4) {
			unsigned int usample[2];
			int ssample[2];

			usample[0] = src[j + 0] >> 0 | src[j + 1] << 8;
			usample[1] = src[j + 2] >> 0 | src[j + 3] << 8;

			/* sign extension from 14-bit to signed int */
			ssample[0] = se.x = usample[0];
			ssample[1] = se.x = usample[1];

			/* from signed to unsigned */
			usample[0] = ssample[0] + 8192;
			usample[1] = ssample[1] + 8192;

			/* from 14-bit to 16-bit */
			*u16dst++ = (usample[0] << 2) | (usample[0] >> 12);
			*u16dst++ = (usample[1] << 2) | (usample[1] >> 12);
		}

		src += 1008;
		dst += 1008;
		dst_len += 1008;
	}

	/* calculate samping rate and output it in 10 seconds intervals */
	if (unlikely(time_is_before_jiffies(s->jiffies_next))) {
#define MSECS 10000UL
		unsigned int samples = sample_num[i_max - 1] - s->sample;
		s->jiffies_next = jiffies + msecs_to_jiffies(MSECS);
		s->sample = sample_num[i_max - 1];
		dev_dbg(&s->udev->dev,
				"slen=%d samples=%u msecs=%lu sampling rate=%lu\n",
				src_len, samples, MSECS,
				samples * 1000UL / MSECS);
	}

	/* next sample (sample = sample + i * 252) */
	s->next_sample = sample_num[i_max - 1] + 252;

	return dst_len;
}

/*
 * This gets called for the Isochronous pipe (stream). This is done in interrupt
 * time, so it has to be fast, not crash, and not stall. Neat.
 */
static void msi3101_isoc_handler(struct urb *urb)
{
	struct msi3101_state *s = (struct msi3101_state *)urb->context;
	int i, flen, fstatus;
	unsigned char *iso_buf = NULL;
	struct msi3101_frame_buf *fbuf;

	if (unlikely(urb->status == -ENOENT || urb->status == -ECONNRESET ||
			urb->status == -ESHUTDOWN)) {
		dev_dbg(&s->udev->dev, "URB (%p) unlinked %ssynchronuously\n",
				urb, urb->status == -ENOENT ? "" : "a");
		return;
	}

	if (unlikely(urb->status != 0)) {
		dev_dbg(&s->udev->dev,
				"msi3101_isoc_handler() called with status %d\n",
				urb->status);
		/* Give up after a number of contiguous errors */
		if (++s->isoc_errors > MAX_ISOC_ERRORS)
			dev_dbg(&s->udev->dev,
					"Too many ISOC errors, bailing out\n");
		goto handler_end;
	} else {
		/* Reset ISOC error counter. We did get here, after all. */
		s->isoc_errors = 0;
	}

	/* Compact data */
	for (i = 0; i < urb->number_of_packets; i++) {
		void *ptr;

		/* Check frame error */
		fstatus = urb->iso_frame_desc[i].status;
		if (unlikely(fstatus)) {
			dev_dbg_ratelimited(&s->udev->dev,
					"frame=%d/%d has error %d skipping\n",
					i, urb->number_of_packets, fstatus);
			continue;
		}

		/* Check if that frame contains data */
		flen = urb->iso_frame_desc[i].actual_length;
		if (unlikely(flen == 0))
			continue;

		iso_buf = urb->transfer_buffer + urb->iso_frame_desc[i].offset;

		/* Get free framebuffer */
		fbuf = msi3101_get_next_fill_buf(s);
		if (unlikely(fbuf == NULL)) {
			s->vb_full++;
			dev_dbg_ratelimited(&s->udev->dev,
					"videobuf is full, %d packets dropped\n",
					s->vb_full);
			continue;
		}

		/* fill framebuffer */
		ptr = vb2_plane_vaddr(&fbuf->vb, 0);
		flen = s->convert_stream(s, ptr, iso_buf, flen);
		vb2_set_plane_payload(&fbuf->vb, 0, flen);
		vb2_buffer_done(&fbuf->vb, VB2_BUF_STATE_DONE);
	}

handler_end:
	i = usb_submit_urb(urb, GFP_ATOMIC);
	if (unlikely(i != 0))
		dev_dbg(&s->udev->dev,
				"Error (%d) re-submitting urb in msi3101_isoc_handler\n",
				i);
}

static void msi3101_iso_stop(struct msi3101_state *s)
{
	int i;
	dev_dbg(&s->udev->dev, "%s:\n", __func__);

	/* Unlinking ISOC buffers one by one */
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		if (s->urbs[i]) {
			dev_dbg(&s->udev->dev, "Unlinking URB %p\n",
					s->urbs[i]);
			usb_kill_urb(s->urbs[i]);
		}
	}
}

static void msi3101_iso_free(struct msi3101_state *s)
{
	int i;
	dev_dbg(&s->udev->dev, "%s:\n", __func__);

	/* Freeing ISOC buffers one by one */
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		if (s->urbs[i]) {
			dev_dbg(&s->udev->dev, "Freeing URB\n");
			if (s->urbs[i]->transfer_buffer) {
				usb_free_coherent(s->udev,
					s->urbs[i]->transfer_buffer_length,
					s->urbs[i]->transfer_buffer,
					s->urbs[i]->transfer_dma);
			}
			usb_free_urb(s->urbs[i]);
			s->urbs[i] = NULL;
		}
	}
}

/* Both v4l2_lock and vb_queue_lock should be locked when calling this */
static void msi3101_isoc_cleanup(struct msi3101_state *s)
{
	dev_dbg(&s->udev->dev, "%s:\n", __func__);

	msi3101_iso_stop(s);
	msi3101_iso_free(s);
}

/* Both v4l2_lock and vb_queue_lock should be locked when calling this */
static int msi3101_isoc_init(struct msi3101_state *s)
{
	struct usb_device *udev;
	struct urb *urb;
	int i, j, ret;
	dev_dbg(&s->udev->dev, "%s:\n", __func__);

	s->isoc_errors = 0;
	udev = s->udev;

	ret = usb_set_interface(s->udev, 0, 1);
	if (ret)
		return ret;

	/* Allocate and init Isochronuous urbs */
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		urb = usb_alloc_urb(ISO_FRAMES_PER_DESC, GFP_KERNEL);
		if (urb == NULL) {
			dev_err(&s->udev->dev,
					"Failed to allocate urb %d\n", i);
			msi3101_isoc_cleanup(s);
			return -ENOMEM;
		}
		s->urbs[i] = urb;
		dev_dbg(&s->udev->dev, "Allocated URB at 0x%p\n", urb);

		urb->interval = 1;
		urb->dev = udev;
		urb->pipe = usb_rcvisocpipe(udev, 0x81);
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		urb->transfer_buffer = usb_alloc_coherent(udev, ISO_BUFFER_SIZE,
				GFP_KERNEL, &urb->transfer_dma);
		if (urb->transfer_buffer == NULL) {
			dev_err(&s->udev->dev,
					"Failed to allocate urb buffer %d\n",
					i);
			msi3101_isoc_cleanup(s);
			return -ENOMEM;
		}
		urb->transfer_buffer_length = ISO_BUFFER_SIZE;
		urb->complete = msi3101_isoc_handler;
		urb->context = s;
		urb->start_frame = 0;
		urb->number_of_packets = ISO_FRAMES_PER_DESC;
		for (j = 0; j < ISO_FRAMES_PER_DESC; j++) {
			urb->iso_frame_desc[j].offset = j * ISO_MAX_FRAME_SIZE;
			urb->iso_frame_desc[j].length = ISO_MAX_FRAME_SIZE;
		}
	}

	/* link */
	for (i = 0; i < MAX_ISO_BUFS; i++) {
		ret = usb_submit_urb(s->urbs[i], GFP_KERNEL);
		if (ret) {
			dev_err(&s->udev->dev,
					"isoc_init() submit_urb %d failed with error %d\n",
					i, ret);
			msi3101_isoc_cleanup(s);
			return ret;
		}
		dev_dbg(&s->udev->dev, "URB 0x%p submitted.\n", s->urbs[i]);
	}

	/* All is done... */
	return 0;
}

/* Must be called with vb_queue_lock hold */
static void msi3101_cleanup_queued_bufs(struct msi3101_state *s)
{
	unsigned long flags = 0;
	dev_dbg(&s->udev->dev, "%s:\n", __func__);

	spin_lock_irqsave(&s->queued_bufs_lock, flags);
	while (!list_empty(&s->queued_bufs)) {
		struct msi3101_frame_buf *buf;

		buf = list_entry(s->queued_bufs.next, struct msi3101_frame_buf,
				 list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&s->queued_bufs_lock, flags);
}

/* The user yanked out the cable... */
static void msi3101_disconnect(struct usb_interface *intf)
{
	struct v4l2_device *v = usb_get_intfdata(intf);
	struct msi3101_state *s =
			container_of(v, struct msi3101_state, v4l2_dev);
	dev_dbg(&s->udev->dev, "%s:\n", __func__);

	mutex_lock(&s->vb_queue_lock);
	mutex_lock(&s->v4l2_lock);
	/* No need to keep the urbs around after disconnection */
	s->udev = NULL;
	v4l2_device_disconnect(&s->v4l2_dev);
	video_unregister_device(&s->vdev);
	spi_unregister_master(s->master);
	mutex_unlock(&s->v4l2_lock);
	mutex_unlock(&s->vb_queue_lock);

	v4l2_device_put(&s->v4l2_dev);
}

static int msi3101_querycap(struct file *file, void *fh,
		struct v4l2_capability *cap)
{
	struct msi3101_state *s = video_drvdata(file);
	dev_dbg(&s->udev->dev, "%s:\n", __func__);

	strlcpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	strlcpy(cap->card, s->vdev.name, sizeof(cap->card));
	usb_make_path(s->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->device_caps = V4L2_CAP_SDR_CAPTURE | V4L2_CAP_STREAMING |
			V4L2_CAP_READWRITE | V4L2_CAP_TUNER;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

/* Videobuf2 operations */
static int msi3101_queue_setup(struct vb2_queue *vq,
		const struct v4l2_format *fmt, unsigned int *nbuffers,
		unsigned int *nplanes, unsigned int sizes[], void *alloc_ctxs[])
{
	struct msi3101_state *s = vb2_get_drv_priv(vq);
	dev_dbg(&s->udev->dev, "%s: *nbuffers=%d\n", __func__, *nbuffers);

	/* Absolute min and max number of buffers available for mmap() */
	*nbuffers = clamp_t(unsigned int, *nbuffers, 8, 32);
	*nplanes = 1;
	/*
	 *   3, wMaxPacketSize 3x 1024 bytes
	 * 504, max IQ sample pairs per 1024 frame
	 *   2, two samples, I and Q
	 *   2, 16-bit is enough for single sample
	 */
	sizes[0] = PAGE_ALIGN(3 * 504 * 2 * 2);
	dev_dbg(&s->udev->dev, "%s: nbuffers=%d sizes[0]=%d\n",
			__func__, *nbuffers, sizes[0]);
	return 0;
}

static void msi3101_buf_queue(struct vb2_buffer *vb)
{
	struct msi3101_state *s = vb2_get_drv_priv(vb->vb2_queue);
	struct msi3101_frame_buf *buf =
			container_of(vb, struct msi3101_frame_buf, vb);
	unsigned long flags = 0;

	/* Check the device has not disconnected between prep and queuing */
	if (unlikely(!s->udev)) {
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
		return;
	}

	spin_lock_irqsave(&s->queued_bufs_lock, flags);
	list_add_tail(&buf->list, &s->queued_bufs);
	spin_unlock_irqrestore(&s->queued_bufs_lock, flags);
}

#define CMD_WREG               0x41
#define CMD_START_STREAMING    0x43
#define CMD_STOP_STREAMING     0x45
#define CMD_READ_UNKNOW        0x48

#define msi3101_dbg_usb_control_msg(udev, r, t, v, _i, b, l) { \
	char *direction; \
	if (t == (USB_TYPE_VENDOR | USB_DIR_OUT)) \
		direction = ">>>"; \
	else \
		direction = "<<<"; \
	dev_dbg(&udev->dev, "%s: %02x %02x %02x %02x %02x %02x %02x %02x " \
			"%s %*ph\n",  __func__, t, r, v & 0xff, v >> 8, \
			_i & 0xff, _i >> 8, l & 0xff, l >> 8, direction, l, b); \
}

static int msi3101_ctrl_msg(struct msi3101_state *s, u8 cmd, u32 data)
{
	int ret;
	u8 request = cmd;
	u8 requesttype = USB_DIR_OUT | USB_TYPE_VENDOR;
	u16 value = (data >> 0) & 0xffff;
	u16 index = (data >> 16) & 0xffff;

	msi3101_dbg_usb_control_msg(s->udev,
			request, requesttype, value, index, NULL, 0);

	ret = usb_control_msg(s->udev, usb_sndctrlpipe(s->udev, 0),
			request, requesttype, value, index, NULL, 0, 2000);

	if (ret)
		dev_err(&s->udev->dev, "%s: failed %d, cmd %02x, data %04x\n",
				__func__, ret, cmd, data);

	return ret;
};

#define F_REF 24000000
#define DIV_R_IN 2
static int msi3101_set_usb_adc(struct msi3101_state *s)
{
	int ret, div_n, div_m, div_r_out, f_sr, f_vco, fract;
	u32 reg3, reg4, reg7;
	struct v4l2_ctrl *bandwidth_auto;
	struct v4l2_ctrl *bandwidth;

	f_sr = s->f_adc;

	/* set tuner, subdev, filters according to sampling rate */
	bandwidth_auto = v4l2_ctrl_find(&s->hdl, V4L2_CID_RF_TUNER_BANDWIDTH_AUTO);
	if (v4l2_ctrl_g_ctrl(bandwidth_auto)) {
		bandwidth = v4l2_ctrl_find(&s->hdl, V4L2_CID_RF_TUNER_BANDWIDTH);
		v4l2_ctrl_s_ctrl(bandwidth, s->f_adc);
	}

	/* select stream format */
	switch (s->pixelformat) {
	case V4L2_SDR_FMT_CU8:
		s->convert_stream = msi3101_convert_stream_504_u8;
		reg7 = 0x000c9407;
		break;
	case  V4L2_SDR_FMT_CU16LE:
		s->convert_stream = msi3101_convert_stream_252_u16;
		reg7 = 0x00009407;
		break;
	case V4L2_PIX_FMT_SDR_S8:
		s->convert_stream = msi3101_convert_stream_504;
		reg7 = 0x000c9407;
		break;
	case V4L2_PIX_FMT_SDR_MSI2500_384:
		s->convert_stream = msi3101_convert_stream_384;
		reg7 = 0x0000a507;
		break;
	case V4L2_PIX_FMT_SDR_S12:
		s->convert_stream = msi3101_convert_stream_336;
		reg7 = 0x00008507;
		break;
	case V4L2_PIX_FMT_SDR_S14:
		s->convert_stream = msi3101_convert_stream_252;
		reg7 = 0x00009407;
		break;
	default:
		s->convert_stream = msi3101_convert_stream_504_u8;
		reg7 = 0x000c9407;
		break;
	}

	/*
	 * Synthesizer config is just a educated guess...
	 *
	 * [7:0]   0x03, register address
	 * [8]     1, power control
	 * [9]     ?, power control
	 * [12:10] output divider
	 * [13]    0 ?
	 * [14]    0 ?
	 * [15]    fractional MSB, bit 20
	 * [16:19] N
	 * [23:20] ?
	 * [24:31] 0x01
	 *
	 * output divider
	 * val   div
	 *   0     - (invalid)
	 *   1     4
	 *   2     6
	 *   3     8
	 *   4    10
	 *   5    12
	 *   6    14
	 *   7    16
	 *
	 * VCO 202000000 - 720000000++
	 */
	reg3 = 0x01000303;
	reg4 = 0x00000004;

	/* XXX: Filters? AGC? */
	if (f_sr < 6000000)
		reg3 |= 0x1 << 20;
	else if (f_sr < 7000000)
		reg3 |= 0x5 << 20;
	else if (f_sr < 8500000)
		reg3 |= 0x9 << 20;
	else
		reg3 |= 0xd << 20;

	for (div_r_out = 4; div_r_out < 16; div_r_out += 2) {
		f_vco = f_sr * div_r_out * 12;
		dev_dbg(&s->udev->dev, "%s: div_r_out=%d f_vco=%d\n",
				__func__, div_r_out, f_vco);
		if (f_vco >= 202000000)
			break;
	}

	div_n = f_vco / (F_REF * DIV_R_IN);
	div_m = f_vco % (F_REF * DIV_R_IN);
	fract = 0x200000ul * div_m / (F_REF * DIV_R_IN);

	reg3 |= div_n << 16;
	reg3 |= (div_r_out / 2 - 1) << 10;
	reg3 |= ((fract >> 20) & 0x000001) << 15; /* [20] */
	reg4 |= ((fract >>  0) & 0x0fffff) <<  8; /* [19:0] */

	dev_dbg(&s->udev->dev,
			"%s: f_sr=%d f_vco=%d div_n=%d div_m=%d div_r_out=%d reg3=%08x reg4=%08x\n",
			__func__, f_sr, f_vco, div_n, div_m, div_r_out, reg3, reg4);

	ret = msi3101_ctrl_msg(s, CMD_WREG, 0x00608008);
	if (ret)
		goto err;

	ret = msi3101_ctrl_msg(s, CMD_WREG, 0x00000c05);
	if (ret)
		goto err;

	ret = msi3101_ctrl_msg(s, CMD_WREG, 0x00020000);
	if (ret)
		goto err;

	ret = msi3101_ctrl_msg(s, CMD_WREG, 0x00480102);
	if (ret)
		goto err;

	ret = msi3101_ctrl_msg(s, CMD_WREG, 0x00f38008);
	if (ret)
		goto err;

	ret = msi3101_ctrl_msg(s, CMD_WREG, reg7);
	if (ret)
		goto err;

	ret = msi3101_ctrl_msg(s, CMD_WREG, reg4);
	if (ret)
		goto err;

	ret = msi3101_ctrl_msg(s, CMD_WREG, reg3);
	if (ret)
		goto err;
err:
	return ret;
};

static int msi3101_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct msi3101_state *s = vb2_get_drv_priv(vq);
	int ret;
	dev_dbg(&s->udev->dev, "%s:\n", __func__);

	if (!s->udev)
		return -ENODEV;

	if (mutex_lock_interruptible(&s->v4l2_lock))
		return -ERESTARTSYS;

	/* wake-up tuner */
	v4l2_subdev_call(s->v4l2_subdev, core, s_power, 1);

	ret = msi3101_set_usb_adc(s);

	ret = msi3101_isoc_init(s);
	if (ret)
		msi3101_cleanup_queued_bufs(s);

	ret = msi3101_ctrl_msg(s, CMD_START_STREAMING, 0);

	mutex_unlock(&s->v4l2_lock);

	return ret;
}

static int msi3101_stop_streaming(struct vb2_queue *vq)
{
	struct msi3101_state *s = vb2_get_drv_priv(vq);
	int ret;
	dev_dbg(&s->udev->dev, "%s:\n", __func__);

	if (mutex_lock_interruptible(&s->v4l2_lock))
		return -ERESTARTSYS;

	if (s->udev)
		msi3101_isoc_cleanup(s);

	msi3101_cleanup_queued_bufs(s);

	/* according to tests, at least 700us delay is required  */
	msleep(20);
	ret = msi3101_ctrl_msg(s, CMD_STOP_STREAMING, 0);
	if (ret)
		goto err_sleep_tuner;

	/* sleep USB IF / ADC */
	ret = msi3101_ctrl_msg(s, CMD_WREG, 0x01000003);
	if (ret)
		goto err_sleep_tuner;

err_sleep_tuner:
	/* sleep tuner */
	ret = v4l2_subdev_call(s->v4l2_subdev, core, s_power, 0);

	mutex_unlock(&s->v4l2_lock);

	return ret;
}

static struct vb2_ops msi3101_vb2_ops = {
	.queue_setup            = msi3101_queue_setup,
	.buf_queue              = msi3101_buf_queue,
	.start_streaming        = msi3101_start_streaming,
	.stop_streaming         = msi3101_stop_streaming,
	.wait_prepare           = vb2_ops_wait_prepare,
	.wait_finish            = vb2_ops_wait_finish,
};

static int msi3101_enum_fmt_sdr_cap(struct file *file, void *priv,
		struct v4l2_fmtdesc *f)
{
	struct msi3101_state *s = video_drvdata(file);
	dev_dbg(&s->udev->dev, "%s: index=%d\n", __func__, f->index);

	if (f->index >= NUM_FORMATS)
		return -EINVAL;

	strlcpy(f->description, formats[f->index].name, sizeof(f->description));
	f->pixelformat = formats[f->index].pixelformat;

	return 0;
}

static int msi3101_g_fmt_sdr_cap(struct file *file, void *priv,
		struct v4l2_format *f)
{
	struct msi3101_state *s = video_drvdata(file);
	dev_dbg(&s->udev->dev, "%s: pixelformat fourcc %4.4s\n", __func__,
			(char *)&s->pixelformat);

	memset(f->fmt.sdr.reserved, 0, sizeof(f->fmt.sdr.reserved));
	f->fmt.sdr.pixelformat = s->pixelformat;

	return 0;
}

static int msi3101_s_fmt_sdr_cap(struct file *file, void *priv,
		struct v4l2_format *f)
{
	struct msi3101_state *s = video_drvdata(file);
	struct vb2_queue *q = &s->vb_queue;
	int i;
	dev_dbg(&s->udev->dev, "%s: pixelformat fourcc %4.4s\n", __func__,
			(char *)&f->fmt.sdr.pixelformat);

	if (vb2_is_busy(q))
		return -EBUSY;

	memset(f->fmt.sdr.reserved, 0, sizeof(f->fmt.sdr.reserved));
	for (i = 0; i < NUM_FORMATS; i++) {
		if (formats[i].pixelformat == f->fmt.sdr.pixelformat) {
			s->pixelformat = f->fmt.sdr.pixelformat;
			return 0;
		}
	}

	f->fmt.sdr.pixelformat = formats[0].pixelformat;
	s->pixelformat = formats[0].pixelformat;

	return 0;
}

static int msi3101_try_fmt_sdr_cap(struct file *file, void *priv,
		struct v4l2_format *f)
{
	struct msi3101_state *s = video_drvdata(file);
	int i;
	dev_dbg(&s->udev->dev, "%s: pixelformat fourcc %4.4s\n", __func__,
			(char *)&f->fmt.sdr.pixelformat);

	memset(f->fmt.sdr.reserved, 0, sizeof(f->fmt.sdr.reserved));
	for (i = 0; i < NUM_FORMATS; i++) {
		if (formats[i].pixelformat == f->fmt.sdr.pixelformat)
			return 0;
	}

	f->fmt.sdr.pixelformat = formats[0].pixelformat;

	return 0;
}

static int msi3101_s_tuner(struct file *file, void *priv,
		const struct v4l2_tuner *v)
{
	struct msi3101_state *s = video_drvdata(file);
	int ret;
	dev_dbg(&s->udev->dev, "%s: index=%d\n", __func__, v->index);

	if (v->index == 0)
		ret = 0;
	else if (v->index == 1)
		ret = v4l2_subdev_call(s->v4l2_subdev, tuner, s_tuner, v);
	else
		ret = -EINVAL;

	return ret;
}

static int msi3101_g_tuner(struct file *file, void *priv, struct v4l2_tuner *v)
{
	struct msi3101_state *s = video_drvdata(file);
	int ret;
	dev_dbg(&s->udev->dev, "%s: index=%d\n", __func__, v->index);

	if (v->index == 0) {
		strlcpy(v->name, "Mirics MSi2500", sizeof(v->name));
		v->type = V4L2_TUNER_ADC;
		v->capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS;
		v->rangelow =   1200000;
		v->rangehigh = 15000000;
		ret = 0;
	} else if (v->index == 1) {
		ret = v4l2_subdev_call(s->v4l2_subdev, tuner, g_tuner, v);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int msi3101_g_frequency(struct file *file, void *priv,
		struct v4l2_frequency *f)
{
	struct msi3101_state *s = video_drvdata(file);
	int ret  = 0;
	dev_dbg(&s->udev->dev, "%s: tuner=%d type=%d\n",
			__func__, f->tuner, f->type);

	if (f->tuner == 0) {
		f->frequency = s->f_adc;
		ret = 0;
	} else if (f->tuner == 1) {
		f->type = V4L2_TUNER_RF;
		ret = v4l2_subdev_call(s->v4l2_subdev, tuner, g_frequency, f);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int msi3101_s_frequency(struct file *file, void *priv,
		const struct v4l2_frequency *f)
{
	struct msi3101_state *s = video_drvdata(file);
	int ret;
	dev_dbg(&s->udev->dev, "%s: tuner=%d type=%d frequency=%u\n",
			__func__, f->tuner, f->type, f->frequency);

	if (f->tuner == 0) {
		s->f_adc = clamp_t(unsigned int, f->frequency,
				bands[0].rangelow,
				bands[0].rangehigh);
		dev_dbg(&s->udev->dev, "%s: ADC frequency=%u Hz\n",
				__func__, s->f_adc);
		ret = msi3101_set_usb_adc(s);
	} else if (f->tuner == 1) {
		ret = v4l2_subdev_call(s->v4l2_subdev, tuner, s_frequency, f);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int msi3101_enum_freq_bands(struct file *file, void *priv,
		struct v4l2_frequency_band *band)
{
	struct msi3101_state *s = video_drvdata(file);
	int ret;
	dev_dbg(&s->udev->dev, "%s: tuner=%d type=%d index=%d\n",
			__func__, band->tuner, band->type, band->index);

	if (band->tuner == 0) {
		if (band->index >= ARRAY_SIZE(bands)) {
			ret = -EINVAL;
		} else {
			*band = bands[band->index];
			ret = 0;
		}
	} else if (band->tuner == 1) {
		ret = v4l2_subdev_call(s->v4l2_subdev, tuner,
				enum_freq_bands, band);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_ioctl_ops msi3101_ioctl_ops = {
	.vidioc_querycap          = msi3101_querycap,

	.vidioc_enum_fmt_sdr_cap  = msi3101_enum_fmt_sdr_cap,
	.vidioc_g_fmt_sdr_cap     = msi3101_g_fmt_sdr_cap,
	.vidioc_s_fmt_sdr_cap     = msi3101_s_fmt_sdr_cap,
	.vidioc_try_fmt_sdr_cap   = msi3101_try_fmt_sdr_cap,

	.vidioc_reqbufs           = vb2_ioctl_reqbufs,
	.vidioc_create_bufs       = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf       = vb2_ioctl_prepare_buf,
	.vidioc_querybuf          = vb2_ioctl_querybuf,
	.vidioc_qbuf              = vb2_ioctl_qbuf,
	.vidioc_dqbuf             = vb2_ioctl_dqbuf,

	.vidioc_streamon          = vb2_ioctl_streamon,
	.vidioc_streamoff         = vb2_ioctl_streamoff,

	.vidioc_g_tuner           = msi3101_g_tuner,
	.vidioc_s_tuner           = msi3101_s_tuner,

	.vidioc_g_frequency       = msi3101_g_frequency,
	.vidioc_s_frequency       = msi3101_s_frequency,
	.vidioc_enum_freq_bands   = msi3101_enum_freq_bands,

	.vidioc_subscribe_event   = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
	.vidioc_log_status        = v4l2_ctrl_log_status,
};

static const struct v4l2_file_operations msi3101_fops = {
	.owner                    = THIS_MODULE,
	.open                     = v4l2_fh_open,
	.release                  = vb2_fop_release,
	.read                     = vb2_fop_read,
	.poll                     = vb2_fop_poll,
	.mmap                     = vb2_fop_mmap,
	.unlocked_ioctl           = video_ioctl2,
};

static struct video_device msi3101_template = {
	.name                     = "Mirics MSi3101 SDR Dongle",
	.release                  = video_device_release_empty,
	.fops                     = &msi3101_fops,
	.ioctl_ops                = &msi3101_ioctl_ops,
};

static void msi3101_video_release(struct v4l2_device *v)
{
	struct msi3101_state *s =
			container_of(v, struct msi3101_state, v4l2_dev);

	v4l2_ctrl_handler_free(&s->hdl);
	v4l2_device_unregister(&s->v4l2_dev);
	kfree(s);
}

static int msi3101_transfer_one_message(struct spi_master *master,
		struct spi_message *m)
{
	struct msi3101_state *s = spi_master_get_devdata(master);
	struct spi_transfer *t;
	int ret = 0;
	u32 data;

	list_for_each_entry(t, &m->transfers, transfer_list) {
		dev_dbg(&s->udev->dev, "%s: msg=%*ph\n",
				__func__, t->len, t->tx_buf);
		data = 0x09; /* reg 9 is SPI adapter */
		data |= ((u8 *)t->tx_buf)[0] << 8;
		data |= ((u8 *)t->tx_buf)[1] << 16;
		data |= ((u8 *)t->tx_buf)[2] << 24;
		ret = msi3101_ctrl_msg(s, CMD_WREG, data);
	}

	m->status = ret;
	spi_finalize_current_message(master);
	return ret;
}

static int msi3101_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct msi3101_state *s = NULL;
	struct v4l2_subdev *sd;
	struct spi_master *master;
	int ret;
	static struct spi_board_info board_info = {
		.modalias		= "msi001",
		.bus_num		= 0,
		.chip_select		= 0,
		.max_speed_hz		= 12000000,
	};

	s = kzalloc(sizeof(struct msi3101_state), GFP_KERNEL);
	if (s == NULL) {
		pr_err("Could not allocate memory for msi3101_state\n");
		return -ENOMEM;
	}

	mutex_init(&s->v4l2_lock);
	mutex_init(&s->vb_queue_lock);
	spin_lock_init(&s->queued_bufs_lock);
	INIT_LIST_HEAD(&s->queued_bufs);
	s->udev = udev;
	s->f_adc = bands[0].rangelow;
	s->pixelformat = V4L2_SDR_FMT_CU8;

	/* Init videobuf2 queue structure */
	s->vb_queue.type = V4L2_BUF_TYPE_SDR_CAPTURE;
	s->vb_queue.io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	s->vb_queue.drv_priv = s;
	s->vb_queue.buf_struct_size = sizeof(struct msi3101_frame_buf);
	s->vb_queue.ops = &msi3101_vb2_ops;
	s->vb_queue.mem_ops = &vb2_vmalloc_memops;
	s->vb_queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	ret = vb2_queue_init(&s->vb_queue);
	if (ret) {
		dev_err(&s->udev->dev, "Could not initialize vb2 queue\n");
		goto err_free_mem;
	}

	/* Init video_device structure */
	s->vdev = msi3101_template;
	s->vdev.queue = &s->vb_queue;
	s->vdev.queue->lock = &s->vb_queue_lock;
	set_bit(V4L2_FL_USE_FH_PRIO, &s->vdev.flags);
	video_set_drvdata(&s->vdev, s);

	/* Register the v4l2_device structure */
	s->v4l2_dev.release = msi3101_video_release;
	ret = v4l2_device_register(&intf->dev, &s->v4l2_dev);
	if (ret) {
		dev_err(&s->udev->dev,
				"Failed to register v4l2-device (%d)\n", ret);
		goto err_free_mem;
	}

	/* SPI master adapter */
	master = spi_alloc_master(&s->udev->dev, 0);
	if (master == NULL) {
		ret = -ENOMEM;
		goto err_unregister_v4l2_dev;
	}

	s->master = master;
	master->bus_num = 0;
	master->num_chipselect = 1;
	master->transfer_one_message = msi3101_transfer_one_message;
	spi_master_set_devdata(master, s);
	ret = spi_register_master(master);
	if (ret) {
		spi_master_put(master);
		goto err_unregister_v4l2_dev;
	}

	/* load v4l2 subdevice */
	sd = v4l2_spi_new_subdev(&s->v4l2_dev, master, &board_info);
	s->v4l2_subdev = sd;
	if (sd == NULL) {
		dev_err(&s->udev->dev, "cannot get v4l2 subdevice\n");
		ret = -ENODEV;
		goto err_unregister_master;
	}

	/* Register controls */
	v4l2_ctrl_handler_init(&s->hdl, 0);
	if (s->hdl.error) {
		ret = s->hdl.error;
		dev_err(&s->udev->dev, "Could not initialize controls\n");
		goto err_free_controls;
	}

	/* currently all controls are from subdev */
	v4l2_ctrl_add_handler(&s->hdl, sd->ctrl_handler, NULL);

	s->v4l2_dev.ctrl_handler = &s->hdl;
	s->vdev.v4l2_dev = &s->v4l2_dev;
	s->vdev.lock = &s->v4l2_lock;

	ret = video_register_device(&s->vdev, VFL_TYPE_SDR, -1);
	if (ret) {
		dev_err(&s->udev->dev,
				"Failed to register as video device (%d)\n",
				ret);
		goto err_unregister_v4l2_dev;
	}
	dev_info(&s->udev->dev, "Registered as %s\n",
			video_device_node_name(&s->vdev));

	return 0;

err_free_controls:
	v4l2_ctrl_handler_free(&s->hdl);
err_unregister_master:
	spi_unregister_master(s->master);
err_unregister_v4l2_dev:
	v4l2_device_unregister(&s->v4l2_dev);
err_free_mem:
	kfree(s);
	return ret;
}

/* USB device ID list */
static struct usb_device_id msi3101_id_table[] = {
	{ USB_DEVICE(0x1df7, 0x2500) }, /* Mirics MSi3101 SDR Dongle */
	{ USB_DEVICE(0x2040, 0xd300) }, /* Hauppauge WinTV 133559 LF */
	{ }
};
MODULE_DEVICE_TABLE(usb, msi3101_id_table);

/* USB subsystem interface */
static struct usb_driver msi3101_driver = {
	.name                     = KBUILD_MODNAME,
	.probe                    = msi3101_probe,
	.disconnect               = msi3101_disconnect,
	.id_table                 = msi3101_id_table,
};

module_usb_driver(msi3101_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Mirics MSi3101 SDR Dongle");
MODULE_LICENSE("GPL");
