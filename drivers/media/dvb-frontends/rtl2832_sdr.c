/*
 * Realtek RTL2832U SDR driver
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
 * GNU Radio plugin "gr-kernel" for device usage will be on:
 * http://git.linuxtv.org/anttip/gr-kernel.git
 *
 */

#include "rtl2832_sdr.h"
#include "dvb_usb.h"

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/math64.h>
#include <linux/regmap.h>

static bool rtl2832_sdr_emulated_fmt;
module_param_named(emulated_formats, rtl2832_sdr_emulated_fmt, bool, 0644);
MODULE_PARM_DESC(emulated_formats, "enable emulated formats (disappears in future)");

/* Original macro does not contain enough null pointer checks for our need */
#define V4L2_SUBDEV_HAS_OP(sd, o, f) \
	((sd) && (sd)->ops && (sd)->ops->o && (sd)->ops->o->f)

#define MAX_BULK_BUFS            (10)
#define BULK_BUFFER_SIZE         (128 * 512)

static const struct v4l2_frequency_band bands_adc[] = {
	{
		.tuner = 0,
		.type = V4L2_TUNER_ADC,
		.index = 0,
		.capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   =  300000,
		.rangehigh  =  300000,
	},
	{
		.tuner = 0,
		.type = V4L2_TUNER_ADC,
		.index = 1,
		.capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   =  900001,
		.rangehigh  = 2800000,
	},
	{
		.tuner = 0,
		.type = V4L2_TUNER_ADC,
		.index = 2,
		.capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   = 3200000,
		.rangehigh  = 3200000,
	},
};

static const struct v4l2_frequency_band bands_fm[] = {
	{
		.tuner = 1,
		.type = V4L2_TUNER_RF,
		.index = 0,
		.capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   =    50000000,
		.rangehigh  =  2000000000,
	},
};

/* stream formats */
struct rtl2832_sdr_format {
	char	*name;
	u32	pixelformat;
	u32	buffersize;
};

static struct rtl2832_sdr_format formats[] = {
	{
		.name		= "Complex U8",
		.pixelformat	= V4L2_SDR_FMT_CU8,
		.buffersize	= BULK_BUFFER_SIZE,
	}, {
		.name		= "Complex U16LE (emulated)",
		.pixelformat	= V4L2_SDR_FMT_CU16LE,
		.buffersize	= BULK_BUFFER_SIZE * 2,
	},
};

static const unsigned int NUM_FORMATS = ARRAY_SIZE(formats);

/* intermediate buffers with raw data from the USB device */
struct rtl2832_sdr_frame_buf {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

struct rtl2832_sdr_dev {
#define POWER_ON           0  /* BIT(0) */
#define URB_BUF            1  /* BIT(1) */
	unsigned long flags;

	struct platform_device *pdev;
	struct regmap *regmap;

	struct video_device vdev;
	struct v4l2_device v4l2_dev;
	struct v4l2_subdev *v4l2_subdev;

	/* videobuf2 queue and queued buffers list */
	struct vb2_queue vb_queue;
	struct list_head queued_bufs;
	spinlock_t queued_bufs_lock; /* Protects queued_bufs */
	unsigned sequence;	     /* buffer sequence counter */

	/* Note if taking both locks v4l2_lock must always be locked first! */
	struct mutex v4l2_lock;      /* Protects everything else */
	struct mutex vb_queue_lock;  /* Protects vb_queue and capt_file */

	/* Pointer to our usb_device, will be NULL after unplug */
	struct usb_device *udev; /* Both mutexes most be hold when setting! */

	unsigned int vb_full; /* vb is full and packets dropped */

	struct urb     *urb_list[MAX_BULK_BUFS];
	int            buf_num;
	unsigned long  buf_size;
	u8             *buf_list[MAX_BULK_BUFS];
	dma_addr_t     dma_addr[MAX_BULK_BUFS];
	int urbs_initialized;
	int urbs_submitted;

	unsigned int f_adc, f_tuner;
	u32 pixelformat;
	u32 buffersize;
	unsigned int num_formats;

	/* Controls */
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *bandwidth_auto;
	struct v4l2_ctrl *bandwidth;

	/* for sample rate calc */
	unsigned int sample;
	unsigned int sample_measured;
	unsigned long jiffies_next;
};

/* Private functions */
static struct rtl2832_sdr_frame_buf *rtl2832_sdr_get_next_fill_buf(
		struct rtl2832_sdr_dev *dev)
{
	unsigned long flags;
	struct rtl2832_sdr_frame_buf *buf = NULL;

	spin_lock_irqsave(&dev->queued_bufs_lock, flags);
	if (list_empty(&dev->queued_bufs))
		goto leave;

	buf = list_entry(dev->queued_bufs.next,
			struct rtl2832_sdr_frame_buf, list);
	list_del(&buf->list);
leave:
	spin_unlock_irqrestore(&dev->queued_bufs_lock, flags);
	return buf;
}

static unsigned int rtl2832_sdr_convert_stream(struct rtl2832_sdr_dev *dev,
		void *dst, const u8 *src, unsigned int src_len)
{
	struct platform_device *pdev = dev->pdev;
	unsigned int dst_len;

	if (dev->pixelformat ==  V4L2_SDR_FMT_CU8) {
		/* native stream, no need to convert */
		memcpy(dst, src, src_len);
		dst_len = src_len;
	} else if (dev->pixelformat == V4L2_SDR_FMT_CU16LE) {
		/* convert u8 to u16 */
		unsigned int i;
		u16 *u16dst = dst;

		for (i = 0; i < src_len; i++)
			*u16dst++ = (src[i] << 8) | (src[i] >> 0);
		dst_len = 2 * src_len;
	} else {
		dst_len = 0;
	}

	/* calculate sample rate and output it in 10 seconds intervals */
	if (unlikely(time_is_before_jiffies(dev->jiffies_next))) {
		#define MSECS 10000UL
		unsigned int msecs = jiffies_to_msecs(jiffies -
				dev->jiffies_next + msecs_to_jiffies(MSECS));
		unsigned int samples = dev->sample - dev->sample_measured;

		dev->jiffies_next = jiffies + msecs_to_jiffies(MSECS);
		dev->sample_measured = dev->sample;
		dev_dbg(&pdev->dev,
			"slen=%u samples=%u msecs=%u sample rate=%lu\n",
			src_len, samples, msecs, samples * 1000UL / msecs);
	}

	/* total number of I+Q pairs */
	dev->sample += src_len / 2;

	return dst_len;
}

/*
 * This gets called for the bulk stream pipe. This is done in interrupt
 * time, so it has to be fast, not crash, and not stall. Neat.
 */
static void rtl2832_sdr_urb_complete(struct urb *urb)
{
	struct rtl2832_sdr_dev *dev = urb->context;
	struct platform_device *pdev = dev->pdev;
	struct rtl2832_sdr_frame_buf *fbuf;

	dev_dbg_ratelimited(&pdev->dev, "status=%d length=%d/%d errors=%d\n",
			    urb->status, urb->actual_length,
			    urb->transfer_buffer_length, urb->error_count);

	switch (urb->status) {
	case 0:             /* success */
	case -ETIMEDOUT:    /* NAK */
		break;
	case -ECONNRESET:   /* kill */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default:            /* error */
		dev_err_ratelimited(&pdev->dev, "urb failed=%d\n", urb->status);
		break;
	}

	if (likely(urb->actual_length > 0)) {
		void *ptr;
		unsigned int len;
		/* get free framebuffer */
		fbuf = rtl2832_sdr_get_next_fill_buf(dev);
		if (unlikely(fbuf == NULL)) {
			dev->vb_full++;
			dev_notice_ratelimited(&pdev->dev,
					       "videobuf is full, %d packets dropped\n",
					       dev->vb_full);
			goto skip;
		}

		/* fill framebuffer */
		ptr = vb2_plane_vaddr(&fbuf->vb.vb2_buf, 0);
		len = rtl2832_sdr_convert_stream(dev, ptr, urb->transfer_buffer,
				urb->actual_length);
		vb2_set_plane_payload(&fbuf->vb.vb2_buf, 0, len);
		fbuf->vb.vb2_buf.timestamp = ktime_get_ns();
		fbuf->vb.sequence = dev->sequence++;
		vb2_buffer_done(&fbuf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}
skip:
	usb_submit_urb(urb, GFP_ATOMIC);
}

static int rtl2832_sdr_kill_urbs(struct rtl2832_sdr_dev *dev)
{
	struct platform_device *pdev = dev->pdev;
	int i;

	for (i = dev->urbs_submitted - 1; i >= 0; i--) {
		dev_dbg(&pdev->dev, "kill urb=%d\n", i);
		/* stop the URB */
		usb_kill_urb(dev->urb_list[i]);
	}
	dev->urbs_submitted = 0;

	return 0;
}

static int rtl2832_sdr_submit_urbs(struct rtl2832_sdr_dev *dev)
{
	struct platform_device *pdev = dev->pdev;
	int i, ret;

	for (i = 0; i < dev->urbs_initialized; i++) {
		dev_dbg(&pdev->dev, "submit urb=%d\n", i);
		ret = usb_submit_urb(dev->urb_list[i], GFP_KERNEL);
		if (ret) {
			dev_err(&pdev->dev,
				"Could not submit urb no. %d - get them all back\n",
				i);
			rtl2832_sdr_kill_urbs(dev);
			return ret;
		}
		dev->urbs_submitted++;
	}

	return 0;
}

static int rtl2832_sdr_free_stream_bufs(struct rtl2832_sdr_dev *dev)
{
	struct platform_device *pdev = dev->pdev;

	if (test_bit(URB_BUF, &dev->flags)) {
		while (dev->buf_num) {
			dev->buf_num--;
			dev_dbg(&pdev->dev, "free buf=%d\n", dev->buf_num);
			usb_free_coherent(dev->udev, dev->buf_size,
					  dev->buf_list[dev->buf_num],
					  dev->dma_addr[dev->buf_num]);
		}
	}
	clear_bit(URB_BUF, &dev->flags);

	return 0;
}

static int rtl2832_sdr_alloc_stream_bufs(struct rtl2832_sdr_dev *dev)
{
	struct platform_device *pdev = dev->pdev;

	dev->buf_num = 0;
	dev->buf_size = BULK_BUFFER_SIZE;

	dev_dbg(&pdev->dev, "all in all I will use %u bytes for streaming\n",
		MAX_BULK_BUFS * BULK_BUFFER_SIZE);

	for (dev->buf_num = 0; dev->buf_num < MAX_BULK_BUFS; dev->buf_num++) {
		dev->buf_list[dev->buf_num] = usb_alloc_coherent(dev->udev,
				BULK_BUFFER_SIZE, GFP_KERNEL,
				&dev->dma_addr[dev->buf_num]);
		if (!dev->buf_list[dev->buf_num]) {
			dev_dbg(&pdev->dev, "alloc buf=%d failed\n",
				dev->buf_num);
			rtl2832_sdr_free_stream_bufs(dev);
			return -ENOMEM;
		}

		dev_dbg(&pdev->dev, "alloc buf=%d %p (dma %llu)\n",
			dev->buf_num, dev->buf_list[dev->buf_num],
			(long long)dev->dma_addr[dev->buf_num]);
		set_bit(URB_BUF, &dev->flags);
	}

	return 0;
}

static int rtl2832_sdr_free_urbs(struct rtl2832_sdr_dev *dev)
{
	struct platform_device *pdev = dev->pdev;
	int i;

	rtl2832_sdr_kill_urbs(dev);

	for (i = dev->urbs_initialized - 1; i >= 0; i--) {
		if (dev->urb_list[i]) {
			dev_dbg(&pdev->dev, "free urb=%d\n", i);
			/* free the URBs */
			usb_free_urb(dev->urb_list[i]);
		}
	}
	dev->urbs_initialized = 0;

	return 0;
}

static int rtl2832_sdr_alloc_urbs(struct rtl2832_sdr_dev *dev)
{
	struct platform_device *pdev = dev->pdev;
	int i, j;

	/* allocate the URBs */
	for (i = 0; i < MAX_BULK_BUFS; i++) {
		dev_dbg(&pdev->dev, "alloc urb=%d\n", i);
		dev->urb_list[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!dev->urb_list[i]) {
			for (j = 0; j < i; j++)
				usb_free_urb(dev->urb_list[j]);
			return -ENOMEM;
		}
		usb_fill_bulk_urb(dev->urb_list[i],
				dev->udev,
				usb_rcvbulkpipe(dev->udev, 0x81),
				dev->buf_list[i],
				BULK_BUFFER_SIZE,
				rtl2832_sdr_urb_complete, dev);

		dev->urb_list[i]->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
		dev->urb_list[i]->transfer_dma = dev->dma_addr[i];
		dev->urbs_initialized++;
	}

	return 0;
}

/* Must be called with vb_queue_lock hold */
static void rtl2832_sdr_cleanup_queued_bufs(struct rtl2832_sdr_dev *dev)
{
	struct platform_device *pdev = dev->pdev;
	unsigned long flags;

	dev_dbg(&pdev->dev, "\n");

	spin_lock_irqsave(&dev->queued_bufs_lock, flags);
	while (!list_empty(&dev->queued_bufs)) {
		struct rtl2832_sdr_frame_buf *buf;

		buf = list_entry(dev->queued_bufs.next,
				struct rtl2832_sdr_frame_buf, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&dev->queued_bufs_lock, flags);
}

static int rtl2832_sdr_querycap(struct file *file, void *fh,
		struct v4l2_capability *cap)
{
	struct rtl2832_sdr_dev *dev = video_drvdata(file);
	struct platform_device *pdev = dev->pdev;

	dev_dbg(&pdev->dev, "\n");

	strscpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	strscpy(cap->card, dev->vdev.name, sizeof(cap->card));
	usb_make_path(dev->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->device_caps = V4L2_CAP_SDR_CAPTURE | V4L2_CAP_STREAMING |
			V4L2_CAP_READWRITE | V4L2_CAP_TUNER;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

/* Videobuf2 operations */
static int rtl2832_sdr_queue_setup(struct vb2_queue *vq,
		unsigned int *nbuffers,
		unsigned int *nplanes, unsigned int sizes[], struct device *alloc_devs[])
{
	struct rtl2832_sdr_dev *dev = vb2_get_drv_priv(vq);
	struct platform_device *pdev = dev->pdev;

	dev_dbg(&pdev->dev, "nbuffers=%d\n", *nbuffers);

	/* Need at least 8 buffers */
	if (vq->num_buffers + *nbuffers < 8)
		*nbuffers = 8 - vq->num_buffers;
	*nplanes = 1;
	sizes[0] = PAGE_ALIGN(dev->buffersize);
	dev_dbg(&pdev->dev, "nbuffers=%d sizes[0]=%d\n", *nbuffers, sizes[0]);
	return 0;
}

static int rtl2832_sdr_buf_prepare(struct vb2_buffer *vb)
{
	struct rtl2832_sdr_dev *dev = vb2_get_drv_priv(vb->vb2_queue);

	/* Don't allow queing new buffers after device disconnection */
	if (!dev->udev)
		return -ENODEV;

	return 0;
}

static void rtl2832_sdr_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rtl2832_sdr_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct rtl2832_sdr_frame_buf *buf =
			container_of(vbuf, struct rtl2832_sdr_frame_buf, vb);
	unsigned long flags;

	/* Check the device has not disconnected between prep and queuing */
	if (!dev->udev) {
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		return;
	}

	spin_lock_irqsave(&dev->queued_bufs_lock, flags);
	list_add_tail(&buf->list, &dev->queued_bufs);
	spin_unlock_irqrestore(&dev->queued_bufs_lock, flags);
}

static int rtl2832_sdr_set_adc(struct rtl2832_sdr_dev *dev)
{
	struct platform_device *pdev = dev->pdev;
	struct rtl2832_sdr_platform_data *pdata = pdev->dev.platform_data;
	struct dvb_frontend *fe = pdata->dvb_frontend;
	int ret;
	unsigned int f_sr, f_if;
	u8 buf[4], u8tmp1, u8tmp2;
	u64 u64tmp;
	u32 u32tmp;

	dev_dbg(&pdev->dev, "f_adc=%u\n", dev->f_adc);

	if (!test_bit(POWER_ON, &dev->flags))
		return 0;

	if (dev->f_adc == 0)
		return 0;

	f_sr = dev->f_adc;

	ret = regmap_bulk_write(dev->regmap, 0x13e, "\x00\x00", 2);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap, 0x115, "\x00\x00\x00\x00", 4);
	if (ret)
		goto err;

	/* get IF from tuner */
	if (fe->ops.tuner_ops.get_if_frequency)
		ret = fe->ops.tuner_ops.get_if_frequency(fe, &f_if);
	else
		ret = -EINVAL;

	if (ret)
		goto err;

	/* program IF */
	u64tmp = f_if % pdata->clk;
	u64tmp *= 0x400000;
	u64tmp = div_u64(u64tmp, pdata->clk);
	u64tmp = -u64tmp;
	u32tmp = u64tmp & 0x3fffff;

	dev_dbg(&pdev->dev, "f_if=%u if_ctl=%08x\n", f_if, u32tmp);

	buf[0] = (u32tmp >> 16) & 0xff;
	buf[1] = (u32tmp >>  8) & 0xff;
	buf[2] = (u32tmp >>  0) & 0xff;

	ret = regmap_bulk_write(dev->regmap, 0x119, buf, 3);
	if (ret)
		goto err;

	/* BB / IF mode */
	/* POR: 0x1b1=0x1f, 0x008=0x0d, 0x006=0x80 */
	if (f_if) {
		u8tmp1 = 0x1a; /* disable Zero-IF */
		u8tmp2 = 0x8d; /* enable ADC I */
	} else {
		u8tmp1 = 0x1b; /* enable Zero-IF, DC, IQ */
		u8tmp2 = 0xcd; /* enable ADC I, ADC Q */
	}

	ret = regmap_write(dev->regmap, 0x1b1, u8tmp1);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x008, u8tmp2);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x006, 0x80);
	if (ret)
		goto err;

	/* program sampling rate (resampling down) */
	u32tmp = div_u64(pdata->clk * 0x400000ULL, f_sr * 4U);
	u32tmp <<= 2;
	buf[0] = (u32tmp >> 24) & 0xff;
	buf[1] = (u32tmp >> 16) & 0xff;
	buf[2] = (u32tmp >>  8) & 0xff;
	buf[3] = (u32tmp >>  0) & 0xff;
	ret = regmap_bulk_write(dev->regmap, 0x19f, buf, 4);
	if (ret)
		goto err;

	/* low-pass filter */
	ret = regmap_bulk_write(dev->regmap, 0x11c,
				"\xca\xdc\xd7\xd8\xe0\xf2\x0e\x35\x06\x50\x9c\x0d\x71\x11\x14\x71\x74\x19\x41\xa5",
				20);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap, 0x017, "\x11\x10", 2);
	if (ret)
		goto err;

	/* mode */
	ret = regmap_write(dev->regmap, 0x019, 0x05);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap, 0x01a,
				"\x1b\x16\x0d\x06\x01\xff", 6);
	if (ret)
		goto err;

	/* FSM */
	ret = regmap_bulk_write(dev->regmap, 0x192, "\x00\xf0\x0f", 3);
	if (ret)
		goto err;

	/* PID filter */
	ret = regmap_write(dev->regmap, 0x061, 0x60);
	if (ret)
		goto err;

	/* used RF tuner based settings */
	switch (pdata->tuner) {
	case RTL2832_SDR_TUNER_E4000:
		ret = regmap_write(dev->regmap, 0x112, 0x5a);
		ret = regmap_write(dev->regmap, 0x102, 0x40);
		ret = regmap_write(dev->regmap, 0x103, 0x5a);
		ret = regmap_write(dev->regmap, 0x1c7, 0x30);
		ret = regmap_write(dev->regmap, 0x104, 0xd0);
		ret = regmap_write(dev->regmap, 0x105, 0xbe);
		ret = regmap_write(dev->regmap, 0x1c8, 0x18);
		ret = regmap_write(dev->regmap, 0x106, 0x35);
		ret = regmap_write(dev->regmap, 0x1c9, 0x21);
		ret = regmap_write(dev->regmap, 0x1ca, 0x21);
		ret = regmap_write(dev->regmap, 0x1cb, 0x00);
		ret = regmap_write(dev->regmap, 0x107, 0x40);
		ret = regmap_write(dev->regmap, 0x1cd, 0x10);
		ret = regmap_write(dev->regmap, 0x1ce, 0x10);
		ret = regmap_write(dev->regmap, 0x108, 0x80);
		ret = regmap_write(dev->regmap, 0x109, 0x7f);
		ret = regmap_write(dev->regmap, 0x10a, 0x80);
		ret = regmap_write(dev->regmap, 0x10b, 0x7f);
		ret = regmap_write(dev->regmap, 0x00e, 0xfc);
		ret = regmap_write(dev->regmap, 0x00e, 0xfc);
		ret = regmap_write(dev->regmap, 0x011, 0xd4);
		ret = regmap_write(dev->regmap, 0x1e5, 0xf0);
		ret = regmap_write(dev->regmap, 0x1d9, 0x00);
		ret = regmap_write(dev->regmap, 0x1db, 0x00);
		ret = regmap_write(dev->regmap, 0x1dd, 0x14);
		ret = regmap_write(dev->regmap, 0x1de, 0xec);
		ret = regmap_write(dev->regmap, 0x1d8, 0x0c);
		ret = regmap_write(dev->regmap, 0x1e6, 0x02);
		ret = regmap_write(dev->regmap, 0x1d7, 0x09);
		ret = regmap_write(dev->regmap, 0x00d, 0x83);
		ret = regmap_write(dev->regmap, 0x010, 0x49);
		ret = regmap_write(dev->regmap, 0x00d, 0x87);
		ret = regmap_write(dev->regmap, 0x00d, 0x85);
		ret = regmap_write(dev->regmap, 0x013, 0x02);
		break;
	case RTL2832_SDR_TUNER_FC0012:
	case RTL2832_SDR_TUNER_FC0013:
		ret = regmap_write(dev->regmap, 0x112, 0x5a);
		ret = regmap_write(dev->regmap, 0x102, 0x40);
		ret = regmap_write(dev->regmap, 0x103, 0x5a);
		ret = regmap_write(dev->regmap, 0x1c7, 0x2c);
		ret = regmap_write(dev->regmap, 0x104, 0xcc);
		ret = regmap_write(dev->regmap, 0x105, 0xbe);
		ret = regmap_write(dev->regmap, 0x1c8, 0x16);
		ret = regmap_write(dev->regmap, 0x106, 0x35);
		ret = regmap_write(dev->regmap, 0x1c9, 0x21);
		ret = regmap_write(dev->regmap, 0x1ca, 0x21);
		ret = regmap_write(dev->regmap, 0x1cb, 0x00);
		ret = regmap_write(dev->regmap, 0x107, 0x40);
		ret = regmap_write(dev->regmap, 0x1cd, 0x10);
		ret = regmap_write(dev->regmap, 0x1ce, 0x10);
		ret = regmap_write(dev->regmap, 0x108, 0x80);
		ret = regmap_write(dev->regmap, 0x109, 0x7f);
		ret = regmap_write(dev->regmap, 0x10a, 0x80);
		ret = regmap_write(dev->regmap, 0x10b, 0x7f);
		ret = regmap_write(dev->regmap, 0x00e, 0xfc);
		ret = regmap_write(dev->regmap, 0x00e, 0xfc);
		ret = regmap_bulk_write(dev->regmap, 0x011, "\xe9\xbf", 2);
		ret = regmap_write(dev->regmap, 0x1e5, 0xf0);
		ret = regmap_write(dev->regmap, 0x1d9, 0x00);
		ret = regmap_write(dev->regmap, 0x1db, 0x00);
		ret = regmap_write(dev->regmap, 0x1dd, 0x11);
		ret = regmap_write(dev->regmap, 0x1de, 0xef);
		ret = regmap_write(dev->regmap, 0x1d8, 0x0c);
		ret = regmap_write(dev->regmap, 0x1e6, 0x02);
		ret = regmap_write(dev->regmap, 0x1d7, 0x09);
		break;
	case RTL2832_SDR_TUNER_R820T:
	case RTL2832_SDR_TUNER_R828D:
		ret = regmap_write(dev->regmap, 0x112, 0x5a);
		ret = regmap_write(dev->regmap, 0x102, 0x40);
		ret = regmap_write(dev->regmap, 0x115, 0x01);
		ret = regmap_write(dev->regmap, 0x103, 0x80);
		ret = regmap_write(dev->regmap, 0x1c7, 0x24);
		ret = regmap_write(dev->regmap, 0x104, 0xcc);
		ret = regmap_write(dev->regmap, 0x105, 0xbe);
		ret = regmap_write(dev->regmap, 0x1c8, 0x14);
		ret = regmap_write(dev->regmap, 0x106, 0x35);
		ret = regmap_write(dev->regmap, 0x1c9, 0x21);
		ret = regmap_write(dev->regmap, 0x1ca, 0x21);
		ret = regmap_write(dev->regmap, 0x1cb, 0x00);
		ret = regmap_write(dev->regmap, 0x107, 0x40);
		ret = regmap_write(dev->regmap, 0x1cd, 0x10);
		ret = regmap_write(dev->regmap, 0x1ce, 0x10);
		ret = regmap_write(dev->regmap, 0x108, 0x80);
		ret = regmap_write(dev->regmap, 0x109, 0x7f);
		ret = regmap_write(dev->regmap, 0x10a, 0x80);
		ret = regmap_write(dev->regmap, 0x10b, 0x7f);
		ret = regmap_write(dev->regmap, 0x00e, 0xfc);
		ret = regmap_write(dev->regmap, 0x00e, 0xfc);
		ret = regmap_write(dev->regmap, 0x011, 0xf4);
		break;
	case RTL2832_SDR_TUNER_FC2580:
		ret = regmap_write(dev->regmap, 0x112, 0x39);
		ret = regmap_write(dev->regmap, 0x102, 0x40);
		ret = regmap_write(dev->regmap, 0x103, 0x5a);
		ret = regmap_write(dev->regmap, 0x1c7, 0x2c);
		ret = regmap_write(dev->regmap, 0x104, 0xcc);
		ret = regmap_write(dev->regmap, 0x105, 0xbe);
		ret = regmap_write(dev->regmap, 0x1c8, 0x16);
		ret = regmap_write(dev->regmap, 0x106, 0x35);
		ret = regmap_write(dev->regmap, 0x1c9, 0x21);
		ret = regmap_write(dev->regmap, 0x1ca, 0x21);
		ret = regmap_write(dev->regmap, 0x1cb, 0x00);
		ret = regmap_write(dev->regmap, 0x107, 0x40);
		ret = regmap_write(dev->regmap, 0x1cd, 0x10);
		ret = regmap_write(dev->regmap, 0x1ce, 0x10);
		ret = regmap_write(dev->regmap, 0x108, 0x80);
		ret = regmap_write(dev->regmap, 0x109, 0x7f);
		ret = regmap_write(dev->regmap, 0x10a, 0x9c);
		ret = regmap_write(dev->regmap, 0x10b, 0x7f);
		ret = regmap_write(dev->regmap, 0x00e, 0xfc);
		ret = regmap_write(dev->regmap, 0x00e, 0xfc);
		ret = regmap_bulk_write(dev->regmap, 0x011, "\xe9\xf4", 2);
		break;
	default:
		dev_notice(&pdev->dev, "Unsupported tuner\n");
	}

	/* software reset */
	ret = regmap_update_bits(dev->regmap, 0x101, 0x04, 0x04);
	if (ret)
		goto err;

	ret = regmap_update_bits(dev->regmap, 0x101, 0x04, 0x00);
	if (ret)
		goto err;
err:
	return ret;
};

static void rtl2832_sdr_unset_adc(struct rtl2832_sdr_dev *dev)
{
	struct platform_device *pdev = dev->pdev;
	int ret;

	dev_dbg(&pdev->dev, "\n");

	/* PID filter */
	ret = regmap_write(dev->regmap, 0x061, 0xe0);
	if (ret)
		goto err;

	/* mode */
	ret = regmap_write(dev->regmap, 0x019, 0x20);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap, 0x017, "\x11\x10", 2);
	if (ret)
		goto err;

	/* FSM */
	ret = regmap_bulk_write(dev->regmap, 0x192, "\x00\x0f\xff", 3);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap, 0x13e, "\x40\x00", 2);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap, 0x115, "\x06\x3f\xce\xcc", 4);
	if (ret)
		goto err;
err:
	return;
};

static int rtl2832_sdr_set_tuner_freq(struct rtl2832_sdr_dev *dev)
{
	struct platform_device *pdev = dev->pdev;
	struct rtl2832_sdr_platform_data *pdata = pdev->dev.platform_data;
	struct dvb_frontend *fe = pdata->dvb_frontend;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct v4l2_ctrl *bandwidth_auto;
	struct v4l2_ctrl *bandwidth;

	/*
	 * tuner RF (Hz)
	 */
	if (dev->f_tuner == 0)
		return 0;

	/*
	 * bandwidth (Hz)
	 */
	bandwidth_auto = v4l2_ctrl_find(&dev->hdl,
					V4L2_CID_RF_TUNER_BANDWIDTH_AUTO);
	bandwidth = v4l2_ctrl_find(&dev->hdl, V4L2_CID_RF_TUNER_BANDWIDTH);
	if (v4l2_ctrl_g_ctrl(bandwidth_auto)) {
		c->bandwidth_hz = dev->f_adc;
		v4l2_ctrl_s_ctrl(bandwidth, dev->f_adc);
	} else {
		c->bandwidth_hz = v4l2_ctrl_g_ctrl(bandwidth);
	}

	c->frequency = dev->f_tuner;
	c->delivery_system = SYS_DVBT;

	dev_dbg(&pdev->dev, "frequency=%u bandwidth=%d\n",
		c->frequency, c->bandwidth_hz);

	if (!test_bit(POWER_ON, &dev->flags))
		return 0;

	if (!V4L2_SUBDEV_HAS_OP(dev->v4l2_subdev, tuner, s_frequency)) {
		if (fe->ops.tuner_ops.set_params)
			fe->ops.tuner_ops.set_params(fe);
	}

	return 0;
};

static int rtl2832_sdr_set_tuner(struct rtl2832_sdr_dev *dev)
{
	struct platform_device *pdev = dev->pdev;
	struct rtl2832_sdr_platform_data *pdata = pdev->dev.platform_data;
	struct dvb_frontend *fe = pdata->dvb_frontend;

	dev_dbg(&pdev->dev, "\n");

	if (fe->ops.tuner_ops.init)
		fe->ops.tuner_ops.init(fe);

	return 0;
};

static void rtl2832_sdr_unset_tuner(struct rtl2832_sdr_dev *dev)
{
	struct platform_device *pdev = dev->pdev;
	struct rtl2832_sdr_platform_data *pdata = pdev->dev.platform_data;
	struct dvb_frontend *fe = pdata->dvb_frontend;

	dev_dbg(&pdev->dev, "\n");

	if (fe->ops.tuner_ops.sleep)
		fe->ops.tuner_ops.sleep(fe);

	return;
};

static int rtl2832_sdr_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct rtl2832_sdr_dev *dev = vb2_get_drv_priv(vq);
	struct platform_device *pdev = dev->pdev;
	struct rtl2832_sdr_platform_data *pdata = pdev->dev.platform_data;
	struct dvb_usb_device *d = pdata->dvb_usb_device;
	int ret;

	dev_dbg(&pdev->dev, "\n");

	if (!dev->udev)
		return -ENODEV;

	if (mutex_lock_interruptible(&dev->v4l2_lock))
		return -ERESTARTSYS;

	if (d->props->power_ctrl)
		d->props->power_ctrl(d, 1);

	/* enable ADC */
	if (d->props->frontend_ctrl)
		d->props->frontend_ctrl(pdata->dvb_frontend, 1);

	set_bit(POWER_ON, &dev->flags);

	/* wake-up tuner */
	if (V4L2_SUBDEV_HAS_OP(dev->v4l2_subdev, core, s_power))
		ret = v4l2_subdev_call(dev->v4l2_subdev, core, s_power, 1);
	else
		ret = rtl2832_sdr_set_tuner(dev);
	if (ret)
		goto err;

	ret = rtl2832_sdr_set_tuner_freq(dev);
	if (ret)
		goto err;

	ret = rtl2832_sdr_set_adc(dev);
	if (ret)
		goto err;

	ret = rtl2832_sdr_alloc_stream_bufs(dev);
	if (ret)
		goto err;

	ret = rtl2832_sdr_alloc_urbs(dev);
	if (ret)
		goto err;

	dev->sequence = 0;

	ret = rtl2832_sdr_submit_urbs(dev);
	if (ret)
		goto err;

err:
	mutex_unlock(&dev->v4l2_lock);

	return ret;
}

static void rtl2832_sdr_stop_streaming(struct vb2_queue *vq)
{
	struct rtl2832_sdr_dev *dev = vb2_get_drv_priv(vq);
	struct platform_device *pdev = dev->pdev;
	struct rtl2832_sdr_platform_data *pdata = pdev->dev.platform_data;
	struct dvb_usb_device *d = pdata->dvb_usb_device;

	dev_dbg(&pdev->dev, "\n");

	mutex_lock(&dev->v4l2_lock);

	rtl2832_sdr_kill_urbs(dev);
	rtl2832_sdr_free_urbs(dev);
	rtl2832_sdr_free_stream_bufs(dev);
	rtl2832_sdr_cleanup_queued_bufs(dev);
	rtl2832_sdr_unset_adc(dev);

	/* sleep tuner */
	if (V4L2_SUBDEV_HAS_OP(dev->v4l2_subdev, core, s_power))
		v4l2_subdev_call(dev->v4l2_subdev, core, s_power, 0);
	else
		rtl2832_sdr_unset_tuner(dev);

	clear_bit(POWER_ON, &dev->flags);

	/* disable ADC */
	if (d->props->frontend_ctrl)
		d->props->frontend_ctrl(pdata->dvb_frontend, 0);

	if (d->props->power_ctrl)
		d->props->power_ctrl(d, 0);

	mutex_unlock(&dev->v4l2_lock);
}

static const struct vb2_ops rtl2832_sdr_vb2_ops = {
	.queue_setup            = rtl2832_sdr_queue_setup,
	.buf_prepare            = rtl2832_sdr_buf_prepare,
	.buf_queue              = rtl2832_sdr_buf_queue,
	.start_streaming        = rtl2832_sdr_start_streaming,
	.stop_streaming         = rtl2832_sdr_stop_streaming,
	.wait_prepare           = vb2_ops_wait_prepare,
	.wait_finish            = vb2_ops_wait_finish,
};

static int rtl2832_sdr_g_tuner(struct file *file, void *priv,
		struct v4l2_tuner *v)
{
	struct rtl2832_sdr_dev *dev = video_drvdata(file);
	struct platform_device *pdev = dev->pdev;
	int ret;

	dev_dbg(&pdev->dev, "index=%d type=%d\n", v->index, v->type);

	if (v->index == 0) {
		strscpy(v->name, "ADC: Realtek RTL2832", sizeof(v->name));
		v->type = V4L2_TUNER_ADC;
		v->capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS;
		v->rangelow =   300000;
		v->rangehigh = 3200000;
		ret = 0;
	} else if (v->index == 1 &&
		   V4L2_SUBDEV_HAS_OP(dev->v4l2_subdev, tuner, g_tuner)) {
		ret = v4l2_subdev_call(dev->v4l2_subdev, tuner, g_tuner, v);
	} else if (v->index == 1) {
		strscpy(v->name, "RF: <unknown>", sizeof(v->name));
		v->type = V4L2_TUNER_RF;
		v->capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS;
		v->rangelow =    50000000;
		v->rangehigh = 2000000000;
		ret = 0;
	} else {
		ret = -EINVAL;
	}
	return ret;
}

static int rtl2832_sdr_s_tuner(struct file *file, void *priv,
		const struct v4l2_tuner *v)
{
	struct rtl2832_sdr_dev *dev = video_drvdata(file);
	struct platform_device *pdev = dev->pdev;
	int ret;

	dev_dbg(&pdev->dev, "\n");

	if (v->index == 0) {
		ret = 0;
	} else if (v->index == 1 &&
		   V4L2_SUBDEV_HAS_OP(dev->v4l2_subdev, tuner, s_tuner)) {
		ret = v4l2_subdev_call(dev->v4l2_subdev, tuner, s_tuner, v);
	} else if (v->index == 1) {
		ret = 0;
	} else {
		ret = -EINVAL;
	}
	return ret;
}

static int rtl2832_sdr_enum_freq_bands(struct file *file, void *priv,
		struct v4l2_frequency_band *band)
{
	struct rtl2832_sdr_dev *dev = video_drvdata(file);
	struct platform_device *pdev = dev->pdev;
	int ret;

	dev_dbg(&pdev->dev, "tuner=%d type=%d index=%d\n",
		band->tuner, band->type, band->index);

	if (band->tuner == 0) {
		if (band->index >= ARRAY_SIZE(bands_adc))
			return -EINVAL;

		*band = bands_adc[band->index];
		ret = 0;
	} else if (band->tuner == 1 &&
		   V4L2_SUBDEV_HAS_OP(dev->v4l2_subdev, tuner, enum_freq_bands)) {
		ret = v4l2_subdev_call(dev->v4l2_subdev, tuner, enum_freq_bands, band);
	} else if (band->tuner == 1) {
		if (band->index >= ARRAY_SIZE(bands_fm))
			return -EINVAL;

		*band = bands_fm[band->index];
		ret = 0;
	} else {
		ret = -EINVAL;
	}
	return ret;
}

static int rtl2832_sdr_g_frequency(struct file *file, void *priv,
		struct v4l2_frequency *f)
{
	struct rtl2832_sdr_dev *dev = video_drvdata(file);
	struct platform_device *pdev = dev->pdev;
	int ret;

	dev_dbg(&pdev->dev, "tuner=%d type=%d\n", f->tuner, f->type);

	if (f->tuner == 0) {
		f->frequency = dev->f_adc;
		f->type = V4L2_TUNER_ADC;
		ret = 0;
	} else if (f->tuner == 1 &&
		   V4L2_SUBDEV_HAS_OP(dev->v4l2_subdev, tuner, g_frequency)) {
		f->type = V4L2_TUNER_RF;
		ret = v4l2_subdev_call(dev->v4l2_subdev, tuner, g_frequency, f);
	} else if (f->tuner == 1) {
		f->frequency = dev->f_tuner;
		f->type = V4L2_TUNER_RF;
		ret = 0;
	} else {
		ret = -EINVAL;
	}
	return ret;
}

static int rtl2832_sdr_s_frequency(struct file *file, void *priv,
		const struct v4l2_frequency *f)
{
	struct rtl2832_sdr_dev *dev = video_drvdata(file);
	struct platform_device *pdev = dev->pdev;
	int ret, band;

	dev_dbg(&pdev->dev, "tuner=%d type=%d frequency=%u\n",
		f->tuner, f->type, f->frequency);

	/* ADC band midpoints */
	#define BAND_ADC_0 ((bands_adc[0].rangehigh + bands_adc[1].rangelow) / 2)
	#define BAND_ADC_1 ((bands_adc[1].rangehigh + bands_adc[2].rangelow) / 2)

	if (f->tuner == 0 && f->type == V4L2_TUNER_ADC) {
		if (f->frequency < BAND_ADC_0)
			band = 0;
		else if (f->frequency < BAND_ADC_1)
			band = 1;
		else
			band = 2;

		dev->f_adc = clamp_t(unsigned int, f->frequency,
				     bands_adc[band].rangelow,
				     bands_adc[band].rangehigh);

		dev_dbg(&pdev->dev, "ADC frequency=%u Hz\n", dev->f_adc);
		ret = rtl2832_sdr_set_adc(dev);
	} else if (f->tuner == 1 &&
		   V4L2_SUBDEV_HAS_OP(dev->v4l2_subdev, tuner, s_frequency)) {
		ret = v4l2_subdev_call(dev->v4l2_subdev, tuner, s_frequency, f);
	} else if (f->tuner == 1) {
		dev->f_tuner = clamp_t(unsigned int, f->frequency,
				bands_fm[0].rangelow,
				bands_fm[0].rangehigh);
		dev_dbg(&pdev->dev, "RF frequency=%u Hz\n", f->frequency);

		ret = rtl2832_sdr_set_tuner_freq(dev);
	} else {
		ret = -EINVAL;
	}
	return ret;
}

static int rtl2832_sdr_enum_fmt_sdr_cap(struct file *file, void *priv,
		struct v4l2_fmtdesc *f)
{
	struct rtl2832_sdr_dev *dev = video_drvdata(file);
	struct platform_device *pdev = dev->pdev;

	dev_dbg(&pdev->dev, "\n");

	if (f->index >= dev->num_formats)
		return -EINVAL;

	strscpy(f->description, formats[f->index].name, sizeof(f->description));
	f->pixelformat = formats[f->index].pixelformat;

	return 0;
}

static int rtl2832_sdr_g_fmt_sdr_cap(struct file *file, void *priv,
		struct v4l2_format *f)
{
	struct rtl2832_sdr_dev *dev = video_drvdata(file);
	struct platform_device *pdev = dev->pdev;

	dev_dbg(&pdev->dev, "\n");

	f->fmt.sdr.pixelformat = dev->pixelformat;
	f->fmt.sdr.buffersize = dev->buffersize;

	memset(f->fmt.sdr.reserved, 0, sizeof(f->fmt.sdr.reserved));

	return 0;
}

static int rtl2832_sdr_s_fmt_sdr_cap(struct file *file, void *priv,
		struct v4l2_format *f)
{
	struct rtl2832_sdr_dev *dev = video_drvdata(file);
	struct platform_device *pdev = dev->pdev;
	struct vb2_queue *q = &dev->vb_queue;
	int i;

	dev_dbg(&pdev->dev, "pixelformat fourcc %4.4s\n",
		(char *)&f->fmt.sdr.pixelformat);

	if (vb2_is_busy(q))
		return -EBUSY;

	memset(f->fmt.sdr.reserved, 0, sizeof(f->fmt.sdr.reserved));
	for (i = 0; i < dev->num_formats; i++) {
		if (formats[i].pixelformat == f->fmt.sdr.pixelformat) {
			dev->pixelformat = formats[i].pixelformat;
			dev->buffersize = formats[i].buffersize;
			f->fmt.sdr.buffersize = formats[i].buffersize;
			return 0;
		}
	}

	dev->pixelformat = formats[0].pixelformat;
	dev->buffersize = formats[0].buffersize;
	f->fmt.sdr.pixelformat = formats[0].pixelformat;
	f->fmt.sdr.buffersize = formats[0].buffersize;

	return 0;
}

static int rtl2832_sdr_try_fmt_sdr_cap(struct file *file, void *priv,
		struct v4l2_format *f)
{
	struct rtl2832_sdr_dev *dev = video_drvdata(file);
	struct platform_device *pdev = dev->pdev;
	int i;

	dev_dbg(&pdev->dev, "pixelformat fourcc %4.4s\n",
		(char *)&f->fmt.sdr.pixelformat);

	memset(f->fmt.sdr.reserved, 0, sizeof(f->fmt.sdr.reserved));
	for (i = 0; i < dev->num_formats; i++) {
		if (formats[i].pixelformat == f->fmt.sdr.pixelformat) {
			f->fmt.sdr.buffersize = formats[i].buffersize;
			return 0;
		}
	}

	f->fmt.sdr.pixelformat = formats[0].pixelformat;
	f->fmt.sdr.buffersize = formats[0].buffersize;

	return 0;
}

static const struct v4l2_ioctl_ops rtl2832_sdr_ioctl_ops = {
	.vidioc_querycap          = rtl2832_sdr_querycap,

	.vidioc_enum_fmt_sdr_cap  = rtl2832_sdr_enum_fmt_sdr_cap,
	.vidioc_g_fmt_sdr_cap     = rtl2832_sdr_g_fmt_sdr_cap,
	.vidioc_s_fmt_sdr_cap     = rtl2832_sdr_s_fmt_sdr_cap,
	.vidioc_try_fmt_sdr_cap   = rtl2832_sdr_try_fmt_sdr_cap,

	.vidioc_reqbufs           = vb2_ioctl_reqbufs,
	.vidioc_create_bufs       = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf       = vb2_ioctl_prepare_buf,
	.vidioc_querybuf          = vb2_ioctl_querybuf,
	.vidioc_qbuf              = vb2_ioctl_qbuf,
	.vidioc_dqbuf             = vb2_ioctl_dqbuf,

	.vidioc_streamon          = vb2_ioctl_streamon,
	.vidioc_streamoff         = vb2_ioctl_streamoff,

	.vidioc_g_tuner           = rtl2832_sdr_g_tuner,
	.vidioc_s_tuner           = rtl2832_sdr_s_tuner,

	.vidioc_enum_freq_bands   = rtl2832_sdr_enum_freq_bands,
	.vidioc_g_frequency       = rtl2832_sdr_g_frequency,
	.vidioc_s_frequency       = rtl2832_sdr_s_frequency,

	.vidioc_subscribe_event   = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
	.vidioc_log_status        = v4l2_ctrl_log_status,
};

static const struct v4l2_file_operations rtl2832_sdr_fops = {
	.owner                    = THIS_MODULE,
	.open                     = v4l2_fh_open,
	.release                  = vb2_fop_release,
	.read                     = vb2_fop_read,
	.poll                     = vb2_fop_poll,
	.mmap                     = vb2_fop_mmap,
	.unlocked_ioctl           = video_ioctl2,
};

static struct video_device rtl2832_sdr_template = {
	.name                     = "Realtek RTL2832 SDR",
	.release                  = video_device_release_empty,
	.fops                     = &rtl2832_sdr_fops,
	.ioctl_ops                = &rtl2832_sdr_ioctl_ops,
};

static int rtl2832_sdr_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct rtl2832_sdr_dev *dev =
			container_of(ctrl->handler, struct rtl2832_sdr_dev,
					hdl);
	struct platform_device *pdev = dev->pdev;
	struct rtl2832_sdr_platform_data *pdata = pdev->dev.platform_data;
	struct dvb_frontend *fe = pdata->dvb_frontend;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;

	dev_dbg(&pdev->dev, "id=%d name=%s val=%d min=%lld max=%lld step=%lld\n",
		ctrl->id, ctrl->name, ctrl->val, ctrl->minimum, ctrl->maximum,
		ctrl->step);

	switch (ctrl->id) {
	case V4L2_CID_RF_TUNER_BANDWIDTH_AUTO:
	case V4L2_CID_RF_TUNER_BANDWIDTH:
		/* TODO: these controls should be moved to tuner drivers */
		if (dev->bandwidth_auto->val) {
			/* Round towards the closest legal value */
			s32 val = dev->f_adc + div_u64(dev->bandwidth->step, 2);
			u32 offset;

			val = clamp_t(s32, val, dev->bandwidth->minimum,
				      dev->bandwidth->maximum);
			offset = val - dev->bandwidth->minimum;
			offset = dev->bandwidth->step *
				div_u64(offset, dev->bandwidth->step);
			dev->bandwidth->val = dev->bandwidth->minimum + offset;
		}
		c->bandwidth_hz = dev->bandwidth->val;

		if (!test_bit(POWER_ON, &dev->flags))
			return 0;

		if (fe->ops.tuner_ops.set_params)
			ret = fe->ops.tuner_ops.set_params(fe);
		else
			ret = 0;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_ctrl_ops rtl2832_sdr_ctrl_ops = {
	.s_ctrl = rtl2832_sdr_s_ctrl,
};

static void rtl2832_sdr_video_release(struct v4l2_device *v)
{
	struct rtl2832_sdr_dev *dev =
			container_of(v, struct rtl2832_sdr_dev, v4l2_dev);
	struct platform_device *pdev = dev->pdev;

	dev_dbg(&pdev->dev, "\n");

	v4l2_ctrl_handler_free(&dev->hdl);
	v4l2_device_unregister(&dev->v4l2_dev);
	kfree(dev);
}

/* Platform driver interface */
static int rtl2832_sdr_probe(struct platform_device *pdev)
{
	struct rtl2832_sdr_dev *dev;
	struct rtl2832_sdr_platform_data *pdata = pdev->dev.platform_data;
	const struct v4l2_ctrl_ops *ops = &rtl2832_sdr_ctrl_ops;
	struct v4l2_subdev *subdev;
	int ret;

	dev_dbg(&pdev->dev, "\n");

	if (!pdata) {
		dev_err(&pdev->dev, "Cannot proceed without platform data\n");
		ret = -EINVAL;
		goto err;
	}
	if (!pdev->dev.parent->driver) {
		dev_dbg(&pdev->dev, "No parent device\n");
		ret = -EINVAL;
		goto err;
	}
	/* try to refcount host drv since we are the consumer */
	if (!try_module_get(pdev->dev.parent->driver->owner)) {
		dev_err(&pdev->dev, "Refcount fail");
		ret = -EINVAL;
		goto err;
	}
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		ret = -ENOMEM;
		goto err_module_put;
	}

	/* setup the state */
	subdev = pdata->v4l2_subdev;
	dev->v4l2_subdev = pdata->v4l2_subdev;
	dev->pdev = pdev;
	dev->regmap = pdata->regmap;
	dev->udev = pdata->dvb_usb_device->udev;
	dev->f_adc = bands_adc[0].rangelow;
	dev->f_tuner = bands_fm[0].rangelow;
	dev->pixelformat = formats[0].pixelformat;
	dev->buffersize = formats[0].buffersize;
	dev->num_formats = NUM_FORMATS;
	if (!rtl2832_sdr_emulated_fmt)
		dev->num_formats -= 1;

	mutex_init(&dev->v4l2_lock);
	mutex_init(&dev->vb_queue_lock);
	spin_lock_init(&dev->queued_bufs_lock);
	INIT_LIST_HEAD(&dev->queued_bufs);

	/* Init videobuf2 queue structure */
	dev->vb_queue.type = V4L2_BUF_TYPE_SDR_CAPTURE;
	dev->vb_queue.io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	dev->vb_queue.drv_priv = dev;
	dev->vb_queue.buf_struct_size = sizeof(struct rtl2832_sdr_frame_buf);
	dev->vb_queue.ops = &rtl2832_sdr_vb2_ops;
	dev->vb_queue.mem_ops = &vb2_vmalloc_memops;
	dev->vb_queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	ret = vb2_queue_init(&dev->vb_queue);
	if (ret) {
		dev_err(&pdev->dev, "Could not initialize vb2 queue\n");
		goto err_kfree;
	}

	/* Register controls */
	switch (pdata->tuner) {
	case RTL2832_SDR_TUNER_E4000:
		v4l2_ctrl_handler_init(&dev->hdl, 9);
		if (subdev)
			v4l2_ctrl_add_handler(&dev->hdl, subdev->ctrl_handler, NULL);
		break;
	case RTL2832_SDR_TUNER_R820T:
	case RTL2832_SDR_TUNER_R828D:
		v4l2_ctrl_handler_init(&dev->hdl, 2);
		dev->bandwidth_auto = v4l2_ctrl_new_std(&dev->hdl, ops,
							V4L2_CID_RF_TUNER_BANDWIDTH_AUTO,
							0, 1, 1, 1);
		dev->bandwidth = v4l2_ctrl_new_std(&dev->hdl, ops,
						   V4L2_CID_RF_TUNER_BANDWIDTH,
						   0, 8000000, 100000, 0);
		v4l2_ctrl_auto_cluster(2, &dev->bandwidth_auto, 0, false);
		break;
	case RTL2832_SDR_TUNER_FC0012:
	case RTL2832_SDR_TUNER_FC0013:
		v4l2_ctrl_handler_init(&dev->hdl, 2);
		dev->bandwidth_auto = v4l2_ctrl_new_std(&dev->hdl, ops,
							V4L2_CID_RF_TUNER_BANDWIDTH_AUTO,
							0, 1, 1, 1);
		dev->bandwidth = v4l2_ctrl_new_std(&dev->hdl, ops,
						   V4L2_CID_RF_TUNER_BANDWIDTH,
						   6000000, 8000000, 1000000,
						   6000000);
		v4l2_ctrl_auto_cluster(2, &dev->bandwidth_auto, 0, false);
		break;
	case RTL2832_SDR_TUNER_FC2580:
		v4l2_ctrl_handler_init(&dev->hdl, 2);
		if (subdev)
			v4l2_ctrl_add_handler(&dev->hdl, subdev->ctrl_handler,
					      NULL);
		break;
	default:
		v4l2_ctrl_handler_init(&dev->hdl, 0);
		dev_err(&pdev->dev, "Unsupported tuner\n");
		goto err_v4l2_ctrl_handler_free;
	}
	if (dev->hdl.error) {
		ret = dev->hdl.error;
		dev_err(&pdev->dev, "Could not initialize controls\n");
		goto err_v4l2_ctrl_handler_free;
	}

	/* Init video_device structure */
	dev->vdev = rtl2832_sdr_template;
	dev->vdev.queue = &dev->vb_queue;
	dev->vdev.queue->lock = &dev->vb_queue_lock;
	video_set_drvdata(&dev->vdev, dev);

	/* Register the v4l2_device structure */
	dev->v4l2_dev.release = rtl2832_sdr_video_release;
	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register v4l2-device %d\n", ret);
		goto err_v4l2_ctrl_handler_free;
	}

	dev->v4l2_dev.ctrl_handler = &dev->hdl;
	dev->vdev.v4l2_dev = &dev->v4l2_dev;
	dev->vdev.lock = &dev->v4l2_lock;
	dev->vdev.vfl_dir = VFL_DIR_RX;

	ret = video_register_device(&dev->vdev, VFL_TYPE_SDR, -1);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register as video device %d\n",
			ret);
		goto err_v4l2_device_unregister;
	}
	dev_info(&pdev->dev, "Registered as %s\n",
		 video_device_node_name(&dev->vdev));
	dev_info(&pdev->dev, "Realtek RTL2832 SDR attached\n");
	dev_notice(&pdev->dev,
		   "SDR API is still slightly experimental and functionality changes may follow\n");
	platform_set_drvdata(pdev, dev);
	return 0;
err_v4l2_device_unregister:
	v4l2_device_unregister(&dev->v4l2_dev);
err_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(&dev->hdl);
err_kfree:
	kfree(dev);
err_module_put:
	module_put(pdev->dev.parent->driver->owner);
err:
	return ret;
}

static int rtl2832_sdr_remove(struct platform_device *pdev)
{
	struct rtl2832_sdr_dev *dev = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "\n");

	mutex_lock(&dev->vb_queue_lock);
	mutex_lock(&dev->v4l2_lock);
	/* No need to keep the urbs around after disconnection */
	dev->udev = NULL;
	v4l2_device_disconnect(&dev->v4l2_dev);
	video_unregister_device(&dev->vdev);
	mutex_unlock(&dev->v4l2_lock);
	mutex_unlock(&dev->vb_queue_lock);
	v4l2_device_put(&dev->v4l2_dev);
	module_put(pdev->dev.parent->driver->owner);

	return 0;
}

static struct platform_driver rtl2832_sdr_driver = {
	.driver = {
		.name   = "rtl2832_sdr",
	},
	.probe          = rtl2832_sdr_probe,
	.remove         = rtl2832_sdr_remove,
};
module_platform_driver(rtl2832_sdr_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Realtek RTL2832 SDR driver");
MODULE_LICENSE("GPL");
