/*
 * HackRF driver
 *
 * Copyright (C) 2014 Antti Palosaari <crope@iki.fi>
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
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-vmalloc.h>

/* HackRF USB API commands (from HackRF Library) */
enum {
	CMD_SET_TRANSCEIVER_MODE           = 0x01,
	CMD_SAMPLE_RATE_SET                = 0x06,
	CMD_BASEBAND_FILTER_BANDWIDTH_SET  = 0x07,
	CMD_BOARD_ID_READ                  = 0x0e,
	CMD_VERSION_STRING_READ            = 0x0f,
	CMD_SET_FREQ                       = 0x10,
	CMD_SET_LNA_GAIN                   = 0x13,
	CMD_SET_VGA_GAIN                   = 0x14,
};

/*
 *       bEndpointAddress     0x81  EP 1 IN
 *         Transfer Type            Bulk
 *       wMaxPacketSize     0x0200  1x 512 bytes
 */
#define MAX_BULK_BUFS            (6)
#define BULK_BUFFER_SIZE         (128 * 512)

static const struct v4l2_frequency_band bands_adc[] = {
	{
		.tuner = 0,
		.type = V4L2_TUNER_ADC,
		.index = 0,
		.capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   =   200000,
		.rangehigh  = 24000000,
	},
};

static const struct v4l2_frequency_band bands_rf[] = {
	{
		.tuner = 1,
		.type = V4L2_TUNER_RF,
		.index = 0,
		.capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   =          1,
		.rangehigh  = 4294967294LL, /* max u32, hw goes over 7GHz */
	},
};

/* stream formats */
struct hackrf_format {
	char	*name;
	u32	pixelformat;
	u32	buffersize;
};

/* format descriptions for capture and preview */
static struct hackrf_format formats[] = {
	{
		.name		= "Complex S8",
		.pixelformat	= V4L2_SDR_FMT_CS8,
		.buffersize	= BULK_BUFFER_SIZE,
	},
};

static const unsigned int NUM_FORMATS = ARRAY_SIZE(formats);

/* intermediate buffers with raw data from the USB device */
struct hackrf_frame_buf {
	struct vb2_buffer vb;   /* common v4l buffer stuff -- must be first */
	struct list_head list;
};

struct hackrf_dev {
#define POWER_ON           (1 << 1)
#define URB_BUF            (1 << 2)
#define USB_STATE_URB_BUF  (1 << 3)
	unsigned long flags;

	struct device *dev;
	struct usb_device *udev;
	struct video_device vdev;
	struct v4l2_device v4l2_dev;

	/* videobuf2 queue and queued buffers list */
	struct vb2_queue vb_queue;
	struct list_head queued_bufs;
	spinlock_t queued_bufs_lock; /* Protects queued_bufs */
	unsigned sequence;	     /* Buffer sequence counter */
	unsigned int vb_full;        /* vb is full and packets dropped */

	/* Note if taking both locks v4l2_lock must always be locked first! */
	struct mutex v4l2_lock;      /* Protects everything else */
	struct mutex vb_queue_lock;  /* Protects vb_queue */

	struct urb     *urb_list[MAX_BULK_BUFS];
	int            buf_num;
	unsigned long  buf_size;
	u8             *buf_list[MAX_BULK_BUFS];
	dma_addr_t     dma_addr[MAX_BULK_BUFS];
	int            urbs_initialized;
	int            urbs_submitted;

	/* USB control message buffer */
	#define BUF_SIZE 24
	u8 buf[BUF_SIZE];

	/* Current configuration */
	unsigned int f_adc;
	unsigned int f_rf;
	u32 pixelformat;
	u32 buffersize;

	/* Controls */
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *bandwidth_auto;
	struct v4l2_ctrl *bandwidth;
	struct v4l2_ctrl *lna_gain;
	struct v4l2_ctrl *if_gain;

	/* Sample rate calc */
	unsigned long jiffies_next;
	unsigned int sample;
	unsigned int sample_measured;
};

#define hackrf_dbg_usb_control_msg(_dev, _r, _t, _v, _i, _b, _l) { \
	char *_direction; \
	if (_t & USB_DIR_IN) \
		_direction = "<<<"; \
	else \
		_direction = ">>>"; \
	dev_dbg(_dev, "%02x %02x %02x %02x %02x %02x %02x %02x %s %*ph\n", \
			_t, _r, _v & 0xff, _v >> 8, _i & 0xff, \
			_i >> 8, _l & 0xff, _l >> 8, _direction, _l, _b); \
}

/* execute firmware command */
static int hackrf_ctrl_msg(struct hackrf_dev *dev, u8 request, u16 value,
		u16 index, u8 *data, u16 size)
{
	int ret;
	unsigned int pipe;
	u8 requesttype;

	switch (request) {
	case CMD_SET_TRANSCEIVER_MODE:
	case CMD_SET_FREQ:
	case CMD_SAMPLE_RATE_SET:
	case CMD_BASEBAND_FILTER_BANDWIDTH_SET:
		pipe = usb_sndctrlpipe(dev->udev, 0);
		requesttype = (USB_TYPE_VENDOR | USB_DIR_OUT);
		break;
	case CMD_BOARD_ID_READ:
	case CMD_VERSION_STRING_READ:
	case CMD_SET_LNA_GAIN:
	case CMD_SET_VGA_GAIN:
		pipe = usb_rcvctrlpipe(dev->udev, 0);
		requesttype = (USB_TYPE_VENDOR | USB_DIR_IN);
		break;
	default:
		dev_err(dev->dev, "Unknown command %02x\n", request);
		ret = -EINVAL;
		goto err;
	}

	/* write request */
	if (!(requesttype & USB_DIR_IN))
		memcpy(dev->buf, data, size);

	ret = usb_control_msg(dev->udev, pipe, request, requesttype, value,
			index, dev->buf, size, 1000);
	hackrf_dbg_usb_control_msg(dev->dev, request, requesttype, value,
			index, dev->buf, size);
	if (ret < 0) {
		dev_err(dev->dev, "usb_control_msg() failed %d request %02x\n",
				ret, request);
		goto err;
	}

	/* read request */
	if (requesttype & USB_DIR_IN)
		memcpy(data, dev->buf, size);

	return 0;
err:
	return ret;
}

/* Private functions */
static struct hackrf_frame_buf *hackrf_get_next_fill_buf(struct hackrf_dev *dev)
{
	unsigned long flags;
	struct hackrf_frame_buf *buf = NULL;

	spin_lock_irqsave(&dev->queued_bufs_lock, flags);
	if (list_empty(&dev->queued_bufs))
		goto leave;

	buf = list_entry(dev->queued_bufs.next, struct hackrf_frame_buf, list);
	list_del(&buf->list);
leave:
	spin_unlock_irqrestore(&dev->queued_bufs_lock, flags);
	return buf;
}

static unsigned int hackrf_convert_stream(struct hackrf_dev *dev,
		void *dst, void *src, unsigned int src_len)
{
	memcpy(dst, src, src_len);

	/* calculate sample rate and output it in 10 seconds intervals */
	if (unlikely(time_is_before_jiffies(dev->jiffies_next))) {
		#define MSECS 10000UL
		unsigned int msecs = jiffies_to_msecs(jiffies -
				dev->jiffies_next + msecs_to_jiffies(MSECS));
		unsigned int samples = dev->sample - dev->sample_measured;

		dev->jiffies_next = jiffies + msecs_to_jiffies(MSECS);
		dev->sample_measured = dev->sample;
		dev_dbg(dev->dev, "slen=%u samples=%u msecs=%u sample rate=%lu\n",
				src_len, samples, msecs,
				samples * 1000UL / msecs);
	}

	/* total number of samples */
	dev->sample += src_len / 2;

	return src_len;
}

/*
 * This gets called for the bulk stream pipe. This is done in interrupt
 * time, so it has to be fast, not crash, and not stall. Neat.
 */
static void hackrf_urb_complete(struct urb *urb)
{
	struct hackrf_dev *dev = urb->context;
	struct hackrf_frame_buf *fbuf;

	dev_dbg_ratelimited(dev->dev, "status=%d length=%d/%d errors=%d\n",
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
		dev_err_ratelimited(dev->dev, "URB failed %d\n", urb->status);
		break;
	}

	if (likely(urb->actual_length > 0)) {
		void *ptr;
		unsigned int len;
		/* get free framebuffer */
		fbuf = hackrf_get_next_fill_buf(dev);
		if (unlikely(fbuf == NULL)) {
			dev->vb_full++;
			dev_notice_ratelimited(dev->dev,
					"videobuf is full, %d packets dropped\n",
					dev->vb_full);
			goto skip;
		}

		/* fill framebuffer */
		ptr = vb2_plane_vaddr(&fbuf->vb, 0);
		len = hackrf_convert_stream(dev, ptr, urb->transfer_buffer,
				urb->actual_length);
		vb2_set_plane_payload(&fbuf->vb, 0, len);
		v4l2_get_timestamp(&fbuf->vb.v4l2_buf.timestamp);
		fbuf->vb.v4l2_buf.sequence = dev->sequence++;
		vb2_buffer_done(&fbuf->vb, VB2_BUF_STATE_DONE);
	}
skip:
	usb_submit_urb(urb, GFP_ATOMIC);
}

static int hackrf_kill_urbs(struct hackrf_dev *dev)
{
	int i;

	for (i = dev->urbs_submitted - 1; i >= 0; i--) {
		dev_dbg(dev->dev, "kill urb=%d\n", i);
		/* stop the URB */
		usb_kill_urb(dev->urb_list[i]);
	}
	dev->urbs_submitted = 0;

	return 0;
}

static int hackrf_submit_urbs(struct hackrf_dev *dev)
{
	int i, ret;

	for (i = 0; i < dev->urbs_initialized; i++) {
		dev_dbg(dev->dev, "submit urb=%d\n", i);
		ret = usb_submit_urb(dev->urb_list[i], GFP_ATOMIC);
		if (ret) {
			dev_err(dev->dev, "Could not submit URB no. %d - get them all back\n",
					i);
			hackrf_kill_urbs(dev);
			return ret;
		}
		dev->urbs_submitted++;
	}

	return 0;
}

static int hackrf_free_stream_bufs(struct hackrf_dev *dev)
{
	if (dev->flags & USB_STATE_URB_BUF) {
		while (dev->buf_num) {
			dev->buf_num--;
			dev_dbg(dev->dev, "free buf=%d\n", dev->buf_num);
			usb_free_coherent(dev->udev, dev->buf_size,
					  dev->buf_list[dev->buf_num],
					  dev->dma_addr[dev->buf_num]);
		}
	}
	dev->flags &= ~USB_STATE_URB_BUF;

	return 0;
}

static int hackrf_alloc_stream_bufs(struct hackrf_dev *dev)
{
	dev->buf_num = 0;
	dev->buf_size = BULK_BUFFER_SIZE;

	dev_dbg(dev->dev, "all in all I will use %u bytes for streaming\n",
			MAX_BULK_BUFS * BULK_BUFFER_SIZE);

	for (dev->buf_num = 0; dev->buf_num < MAX_BULK_BUFS; dev->buf_num++) {
		dev->buf_list[dev->buf_num] = usb_alloc_coherent(dev->udev,
				BULK_BUFFER_SIZE, GFP_ATOMIC,
				&dev->dma_addr[dev->buf_num]);
		if (!dev->buf_list[dev->buf_num]) {
			dev_dbg(dev->dev, "alloc buf=%d failed\n",
					dev->buf_num);
			hackrf_free_stream_bufs(dev);
			return -ENOMEM;
		}

		dev_dbg(dev->dev, "alloc buf=%d %p (dma %llu)\n", dev->buf_num,
				dev->buf_list[dev->buf_num],
				(long long)dev->dma_addr[dev->buf_num]);
		dev->flags |= USB_STATE_URB_BUF;
	}

	return 0;
}

static int hackrf_free_urbs(struct hackrf_dev *dev)
{
	int i;

	hackrf_kill_urbs(dev);

	for (i = dev->urbs_initialized - 1; i >= 0; i--) {
		if (dev->urb_list[i]) {
			dev_dbg(dev->dev, "free urb=%d\n", i);
			/* free the URBs */
			usb_free_urb(dev->urb_list[i]);
		}
	}
	dev->urbs_initialized = 0;

	return 0;
}

static int hackrf_alloc_urbs(struct hackrf_dev *dev)
{
	int i, j;

	/* allocate the URBs */
	for (i = 0; i < MAX_BULK_BUFS; i++) {
		dev_dbg(dev->dev, "alloc urb=%d\n", i);
		dev->urb_list[i] = usb_alloc_urb(0, GFP_ATOMIC);
		if (!dev->urb_list[i]) {
			dev_dbg(dev->dev, "failed\n");
			for (j = 0; j < i; j++)
				usb_free_urb(dev->urb_list[j]);
			return -ENOMEM;
		}
		usb_fill_bulk_urb(dev->urb_list[i],
				dev->udev,
				usb_rcvbulkpipe(dev->udev, 0x81),
				dev->buf_list[i],
				BULK_BUFFER_SIZE,
				hackrf_urb_complete, dev);

		dev->urb_list[i]->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
		dev->urb_list[i]->transfer_dma = dev->dma_addr[i];
		dev->urbs_initialized++;
	}

	return 0;
}

/* Must be called with vb_queue_lock hold */
static void hackrf_cleanup_queued_bufs(struct hackrf_dev *dev)
{
	unsigned long flags;

	dev_dbg(dev->dev, "\n");

	spin_lock_irqsave(&dev->queued_bufs_lock, flags);
	while (!list_empty(&dev->queued_bufs)) {
		struct hackrf_frame_buf *buf;

		buf = list_entry(dev->queued_bufs.next,
				struct hackrf_frame_buf, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&dev->queued_bufs_lock, flags);
}

/* The user yanked out the cable... */
static void hackrf_disconnect(struct usb_interface *intf)
{
	struct v4l2_device *v = usb_get_intfdata(intf);
	struct hackrf_dev *dev = container_of(v, struct hackrf_dev, v4l2_dev);

	dev_dbg(dev->dev, "\n");

	mutex_lock(&dev->vb_queue_lock);
	mutex_lock(&dev->v4l2_lock);
	/* No need to keep the urbs around after disconnection */
	dev->udev = NULL;
	v4l2_device_disconnect(&dev->v4l2_dev);
	video_unregister_device(&dev->vdev);
	mutex_unlock(&dev->v4l2_lock);
	mutex_unlock(&dev->vb_queue_lock);

	v4l2_device_put(&dev->v4l2_dev);
}

/* Videobuf2 operations */
static int hackrf_queue_setup(struct vb2_queue *vq,
		const struct v4l2_format *fmt, unsigned int *nbuffers,
		unsigned int *nplanes, unsigned int sizes[], void *alloc_ctxs[])
{
	struct hackrf_dev *dev = vb2_get_drv_priv(vq);

	dev_dbg(dev->dev, "nbuffers=%d\n", *nbuffers);

	/* Need at least 8 buffers */
	if (vq->num_buffers + *nbuffers < 8)
		*nbuffers = 8 - vq->num_buffers;
	*nplanes = 1;
	sizes[0] = PAGE_ALIGN(dev->buffersize);

	dev_dbg(dev->dev, "nbuffers=%d sizes[0]=%d\n", *nbuffers, sizes[0]);
	return 0;
}

static void hackrf_buf_queue(struct vb2_buffer *vb)
{
	struct hackrf_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct hackrf_frame_buf *buf =
			container_of(vb, struct hackrf_frame_buf, vb);
	unsigned long flags;

	spin_lock_irqsave(&dev->queued_bufs_lock, flags);
	list_add_tail(&buf->list, &dev->queued_bufs);
	spin_unlock_irqrestore(&dev->queued_bufs_lock, flags);
}

static int hackrf_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct hackrf_dev *dev = vb2_get_drv_priv(vq);
	int ret;

	dev_dbg(dev->dev, "\n");

	if (!dev->udev)
		return -ENODEV;

	mutex_lock(&dev->v4l2_lock);

	dev->sequence = 0;

	set_bit(POWER_ON, &dev->flags);

	ret = hackrf_alloc_stream_bufs(dev);
	if (ret)
		goto err;

	ret = hackrf_alloc_urbs(dev);
	if (ret)
		goto err;

	ret = hackrf_submit_urbs(dev);
	if (ret)
		goto err;

	/* start hardware streaming */
	ret = hackrf_ctrl_msg(dev, CMD_SET_TRANSCEIVER_MODE, 1, 0, NULL, 0);
	if (ret)
		goto err;

	goto exit_mutex_unlock;
err:
	hackrf_kill_urbs(dev);
	hackrf_free_urbs(dev);
	hackrf_free_stream_bufs(dev);
	clear_bit(POWER_ON, &dev->flags);

	/* return all queued buffers to vb2 */
	{
		struct hackrf_frame_buf *buf, *tmp;

		list_for_each_entry_safe(buf, tmp, &dev->queued_bufs, list) {
			list_del(&buf->list);
			vb2_buffer_done(&buf->vb, VB2_BUF_STATE_QUEUED);
		}
	}

exit_mutex_unlock:
	mutex_unlock(&dev->v4l2_lock);

	return ret;
}

static void hackrf_stop_streaming(struct vb2_queue *vq)
{
	struct hackrf_dev *dev = vb2_get_drv_priv(vq);

	dev_dbg(dev->dev, "\n");

	mutex_lock(&dev->v4l2_lock);

	/* stop hardware streaming */
	hackrf_ctrl_msg(dev, CMD_SET_TRANSCEIVER_MODE, 0, 0, NULL, 0);

	hackrf_kill_urbs(dev);
	hackrf_free_urbs(dev);
	hackrf_free_stream_bufs(dev);

	hackrf_cleanup_queued_bufs(dev);

	clear_bit(POWER_ON, &dev->flags);

	mutex_unlock(&dev->v4l2_lock);
}

static struct vb2_ops hackrf_vb2_ops = {
	.queue_setup            = hackrf_queue_setup,
	.buf_queue              = hackrf_buf_queue,
	.start_streaming        = hackrf_start_streaming,
	.stop_streaming         = hackrf_stop_streaming,
	.wait_prepare           = vb2_ops_wait_prepare,
	.wait_finish            = vb2_ops_wait_finish,
};

static int hackrf_querycap(struct file *file, void *fh,
		struct v4l2_capability *cap)
{
	struct hackrf_dev *dev = video_drvdata(file);

	dev_dbg(dev->dev, "\n");

	strlcpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	strlcpy(cap->card, dev->vdev.name, sizeof(cap->card));
	usb_make_path(dev->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->device_caps = V4L2_CAP_SDR_CAPTURE | V4L2_CAP_STREAMING |
			V4L2_CAP_READWRITE | V4L2_CAP_TUNER;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int hackrf_s_fmt_sdr_cap(struct file *file, void *priv,
		struct v4l2_format *f)
{
	struct hackrf_dev *dev = video_drvdata(file);
	struct vb2_queue *q = &dev->vb_queue;
	int i;

	dev_dbg(dev->dev, "pixelformat fourcc %4.4s\n",
			(char *)&f->fmt.sdr.pixelformat);

	if (vb2_is_busy(q))
		return -EBUSY;

	memset(f->fmt.sdr.reserved, 0, sizeof(f->fmt.sdr.reserved));
	for (i = 0; i < NUM_FORMATS; i++) {
		if (f->fmt.sdr.pixelformat == formats[i].pixelformat) {
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

static int hackrf_g_fmt_sdr_cap(struct file *file, void *priv,
		struct v4l2_format *f)
{
	struct hackrf_dev *dev = video_drvdata(file);

	dev_dbg(dev->dev, "pixelformat fourcc %4.4s\n",
			(char *)&dev->pixelformat);

	memset(f->fmt.sdr.reserved, 0, sizeof(f->fmt.sdr.reserved));
	f->fmt.sdr.pixelformat = dev->pixelformat;
	f->fmt.sdr.buffersize = dev->buffersize;

	return 0;
}

static int hackrf_try_fmt_sdr_cap(struct file *file, void *priv,
		struct v4l2_format *f)
{
	struct hackrf_dev *dev = video_drvdata(file);
	int i;

	dev_dbg(dev->dev, "pixelformat fourcc %4.4s\n",
			(char *)&f->fmt.sdr.pixelformat);

	memset(f->fmt.sdr.reserved, 0, sizeof(f->fmt.sdr.reserved));
	for (i = 0; i < NUM_FORMATS; i++) {
		if (formats[i].pixelformat == f->fmt.sdr.pixelformat) {
			f->fmt.sdr.buffersize = formats[i].buffersize;
			return 0;
		}
	}

	f->fmt.sdr.pixelformat = formats[0].pixelformat;
	f->fmt.sdr.buffersize = formats[0].buffersize;

	return 0;
}

static int hackrf_enum_fmt_sdr_cap(struct file *file, void *priv,
		struct v4l2_fmtdesc *f)
{
	struct hackrf_dev *dev = video_drvdata(file);

	dev_dbg(dev->dev, "index=%d\n", f->index);

	if (f->index >= NUM_FORMATS)
		return -EINVAL;

	strlcpy(f->description, formats[f->index].name, sizeof(f->description));
	f->pixelformat = formats[f->index].pixelformat;

	return 0;
}

static int hackrf_s_tuner(struct file *file, void *priv,
		const struct v4l2_tuner *v)
{
	struct hackrf_dev *dev = video_drvdata(file);
	int ret;

	dev_dbg(dev->dev, "index=%d\n", v->index);

	if (v->index == 0)
		ret = 0;
	else if (v->index == 1)
		ret = 0;
	else
		ret = -EINVAL;

	return ret;
}

static int hackrf_g_tuner(struct file *file, void *priv, struct v4l2_tuner *v)
{
	struct hackrf_dev *dev = video_drvdata(file);
	int ret;

	dev_dbg(dev->dev, "index=%d\n", v->index);

	if (v->index == 0) {
		strlcpy(v->name, "HackRF ADC", sizeof(v->name));
		v->type = V4L2_TUNER_ADC;
		v->capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS;
		v->rangelow  = bands_adc[0].rangelow;
		v->rangehigh = bands_adc[0].rangehigh;
		ret = 0;
	} else if (v->index == 1) {
		strlcpy(v->name, "HackRF RF", sizeof(v->name));
		v->type = V4L2_TUNER_RF;
		v->capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS;
		v->rangelow  = bands_rf[0].rangelow;
		v->rangehigh = bands_rf[0].rangehigh;
		ret = 0;
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int hackrf_s_frequency(struct file *file, void *priv,
		const struct v4l2_frequency *f)
{
	struct hackrf_dev *dev = video_drvdata(file);
	int ret;
	unsigned int upper, lower;
	u8 buf[8];

	dev_dbg(dev->dev, "tuner=%d type=%d frequency=%u\n",
			f->tuner, f->type, f->frequency);

	if (f->tuner == 0) {
		dev->f_adc = clamp_t(unsigned int, f->frequency,
				bands_adc[0].rangelow, bands_adc[0].rangehigh);
		dev_dbg(dev->dev, "ADC frequency=%u Hz\n", dev->f_adc);
		upper = dev->f_adc;
		lower = 1;
		buf[0] = (upper >>  0) & 0xff;
		buf[1] = (upper >>  8) & 0xff;
		buf[2] = (upper >> 16) & 0xff;
		buf[3] = (upper >> 24) & 0xff;
		buf[4] = (lower >>  0) & 0xff;
		buf[5] = (lower >>  8) & 0xff;
		buf[6] = (lower >> 16) & 0xff;
		buf[7] = (lower >> 24) & 0xff;
		ret = hackrf_ctrl_msg(dev, CMD_SAMPLE_RATE_SET, 0, 0, buf, 8);
	} else if (f->tuner == 1) {
		dev->f_rf = clamp_t(unsigned int, f->frequency,
				bands_rf[0].rangelow, bands_rf[0].rangehigh);
		dev_dbg(dev->dev, "RF frequency=%u Hz\n", dev->f_rf);
		upper = dev->f_rf / 1000000;
		lower = dev->f_rf % 1000000;
		buf[0] = (upper >>  0) & 0xff;
		buf[1] = (upper >>  8) & 0xff;
		buf[2] = (upper >> 16) & 0xff;
		buf[3] = (upper >> 24) & 0xff;
		buf[4] = (lower >>  0) & 0xff;
		buf[5] = (lower >>  8) & 0xff;
		buf[6] = (lower >> 16) & 0xff;
		buf[7] = (lower >> 24) & 0xff;
		ret = hackrf_ctrl_msg(dev, CMD_SET_FREQ, 0, 0, buf, 8);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int hackrf_g_frequency(struct file *file, void *priv,
		struct v4l2_frequency *f)
{
	struct hackrf_dev *dev = video_drvdata(file);
	int ret;

	dev_dbg(dev->dev, "tuner=%d type=%d\n", f->tuner, f->type);

	if (f->tuner == 0) {
		f->type = V4L2_TUNER_ADC;
		f->frequency = dev->f_adc;
		ret = 0;
	} else if (f->tuner == 1) {
		f->type = V4L2_TUNER_RF;
		f->frequency = dev->f_rf;
		ret = 0;
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int hackrf_enum_freq_bands(struct file *file, void *priv,
		struct v4l2_frequency_band *band)
{
	struct hackrf_dev *dev = video_drvdata(file);
	int ret;

	dev_dbg(dev->dev, "tuner=%d type=%d index=%d\n",
			band->tuner, band->type, band->index);

	if (band->tuner == 0) {
		if (band->index >= ARRAY_SIZE(bands_adc)) {
			ret = -EINVAL;
		} else {
			*band = bands_adc[band->index];
			ret = 0;
		}
	} else if (band->tuner == 1) {
		if (band->index >= ARRAY_SIZE(bands_rf)) {
			ret = -EINVAL;
		} else {
			*band = bands_rf[band->index];
			ret = 0;
		}
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_ioctl_ops hackrf_ioctl_ops = {
	.vidioc_querycap          = hackrf_querycap,

	.vidioc_s_fmt_sdr_cap     = hackrf_s_fmt_sdr_cap,
	.vidioc_g_fmt_sdr_cap     = hackrf_g_fmt_sdr_cap,
	.vidioc_enum_fmt_sdr_cap  = hackrf_enum_fmt_sdr_cap,
	.vidioc_try_fmt_sdr_cap   = hackrf_try_fmt_sdr_cap,

	.vidioc_reqbufs           = vb2_ioctl_reqbufs,
	.vidioc_create_bufs       = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf       = vb2_ioctl_prepare_buf,
	.vidioc_querybuf          = vb2_ioctl_querybuf,
	.vidioc_qbuf              = vb2_ioctl_qbuf,
	.vidioc_dqbuf             = vb2_ioctl_dqbuf,

	.vidioc_streamon          = vb2_ioctl_streamon,
	.vidioc_streamoff         = vb2_ioctl_streamoff,

	.vidioc_s_tuner           = hackrf_s_tuner,
	.vidioc_g_tuner           = hackrf_g_tuner,

	.vidioc_s_frequency       = hackrf_s_frequency,
	.vidioc_g_frequency       = hackrf_g_frequency,
	.vidioc_enum_freq_bands   = hackrf_enum_freq_bands,

	.vidioc_subscribe_event   = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
	.vidioc_log_status        = v4l2_ctrl_log_status,
};

static const struct v4l2_file_operations hackrf_fops = {
	.owner                    = THIS_MODULE,
	.open                     = v4l2_fh_open,
	.release                  = vb2_fop_release,
	.read                     = vb2_fop_read,
	.poll                     = vb2_fop_poll,
	.mmap                     = vb2_fop_mmap,
	.unlocked_ioctl           = video_ioctl2,
};

static struct video_device hackrf_template = {
	.name                     = "HackRF One",
	.release                  = video_device_release_empty,
	.fops                     = &hackrf_fops,
	.ioctl_ops                = &hackrf_ioctl_ops,
};

static void hackrf_video_release(struct v4l2_device *v)
{
	struct hackrf_dev *dev = container_of(v, struct hackrf_dev, v4l2_dev);

	v4l2_ctrl_handler_free(&dev->hdl);
	v4l2_device_unregister(&dev->v4l2_dev);
	kfree(dev);
}

static int hackrf_set_bandwidth(struct hackrf_dev *dev)
{
	int ret, i;
	u16 u16tmp, u16tmp2;
	unsigned int bandwidth;

	static const struct {
		u32 freq;
	} bandwidth_lut[] = {
		{ 1750000}, /*  1.75 MHz */
		{ 2500000}, /*  2.5  MHz */
		{ 3500000}, /*  3.5  MHz */
		{ 5000000}, /*  5    MHz */
		{ 5500000}, /*  5.5  MHz */
		{ 6000000}, /*  6    MHz */
		{ 7000000}, /*  7    MHz */
		{ 8000000}, /*  8    MHz */
		{ 9000000}, /*  9    MHz */
		{10000000}, /* 10    MHz */
		{12000000}, /* 12    MHz */
		{14000000}, /* 14    MHz */
		{15000000}, /* 15    MHz */
		{20000000}, /* 20    MHz */
		{24000000}, /* 24    MHz */
		{28000000}, /* 28    MHz */
	};

	dev_dbg(dev->dev, "bandwidth auto=%d->%d val=%d->%d f_adc=%u\n",
			dev->bandwidth_auto->cur.val,
			dev->bandwidth_auto->val, dev->bandwidth->cur.val,
			dev->bandwidth->val, dev->f_adc);

	if (dev->bandwidth_auto->val == true)
		bandwidth = dev->f_adc;
	else
		bandwidth = dev->bandwidth->val;

	for (i = 0; i < ARRAY_SIZE(bandwidth_lut); i++) {
		if (bandwidth <= bandwidth_lut[i].freq) {
			bandwidth = bandwidth_lut[i].freq;
			break;
		}
	}

	dev->bandwidth->val = bandwidth;
	dev->bandwidth->cur.val = bandwidth;

	dev_dbg(dev->dev, "bandwidth selected=%d\n", bandwidth);

	u16tmp = 0;
	u16tmp |= ((bandwidth >> 0) & 0xff) << 0;
	u16tmp |= ((bandwidth >> 8) & 0xff) << 8;
	u16tmp2 = 0;
	u16tmp2 |= ((bandwidth >> 16) & 0xff) << 0;
	u16tmp2 |= ((bandwidth >> 24) & 0xff) << 8;

	ret = hackrf_ctrl_msg(dev, CMD_BASEBAND_FILTER_BANDWIDTH_SET,
				u16tmp, u16tmp2, NULL, 0);
	if (ret)
		dev_dbg(dev->dev, "failed=%d\n", ret);

	return ret;
}

static int hackrf_set_lna_gain(struct hackrf_dev *dev)
{
	int ret;
	u8 u8tmp;

	dev_dbg(dev->dev, "lna val=%d->%d\n",
			dev->lna_gain->cur.val, dev->lna_gain->val);

	ret = hackrf_ctrl_msg(dev, CMD_SET_LNA_GAIN, 0, dev->lna_gain->val,
			&u8tmp, 1);
	if (ret)
		dev_dbg(dev->dev, "failed=%d\n", ret);

	return ret;
}

static int hackrf_set_if_gain(struct hackrf_dev *dev)
{
	int ret;
	u8 u8tmp;

	dev_dbg(dev->dev, "val=%d->%d\n",
			dev->if_gain->cur.val, dev->if_gain->val);

	ret = hackrf_ctrl_msg(dev, CMD_SET_VGA_GAIN, 0, dev->if_gain->val,
			&u8tmp, 1);
	if (ret)
		dev_dbg(dev->dev, "failed=%d\n", ret);

	return ret;
}

static int hackrf_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct hackrf_dev *dev = container_of(ctrl->handler,
			struct hackrf_dev, hdl);
	int ret;

	switch (ctrl->id) {
	case V4L2_CID_RF_TUNER_BANDWIDTH_AUTO:
	case V4L2_CID_RF_TUNER_BANDWIDTH:
		ret = hackrf_set_bandwidth(dev);
		break;
	case  V4L2_CID_RF_TUNER_LNA_GAIN:
		ret = hackrf_set_lna_gain(dev);
		break;
	case  V4L2_CID_RF_TUNER_IF_GAIN:
		ret = hackrf_set_if_gain(dev);
		break;
	default:
		dev_dbg(dev->dev, "unknown ctrl: id=%d name=%s\n",
				ctrl->id, ctrl->name);
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_ctrl_ops hackrf_ctrl_ops = {
	.s_ctrl = hackrf_s_ctrl,
};

static int hackrf_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct hackrf_dev *dev;
	int ret;
	u8 u8tmp, buf[BUF_SIZE];

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL)
		return -ENOMEM;

	mutex_init(&dev->v4l2_lock);
	mutex_init(&dev->vb_queue_lock);
	spin_lock_init(&dev->queued_bufs_lock);
	INIT_LIST_HEAD(&dev->queued_bufs);
	dev->dev = &intf->dev;
	dev->udev = interface_to_usbdev(intf);
	dev->f_adc = bands_adc[0].rangelow;
	dev->f_rf = bands_rf[0].rangelow;
	dev->pixelformat = formats[0].pixelformat;
	dev->buffersize = formats[0].buffersize;

	/* Detect device */
	ret = hackrf_ctrl_msg(dev, CMD_BOARD_ID_READ, 0, 0, &u8tmp, 1);
	if (ret == 0)
		ret = hackrf_ctrl_msg(dev, CMD_VERSION_STRING_READ, 0, 0,
				buf, BUF_SIZE);
	if (ret) {
		dev_err(dev->dev, "Could not detect board\n");
		goto err_free_mem;
	}

	buf[BUF_SIZE - 1] = '\0';

	dev_info(dev->dev, "Board ID: %02x\n", u8tmp);
	dev_info(dev->dev, "Firmware version: %s\n", buf);

	/* Init videobuf2 queue structure */
	dev->vb_queue.type = V4L2_BUF_TYPE_SDR_CAPTURE;
	dev->vb_queue.io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	dev->vb_queue.drv_priv = dev;
	dev->vb_queue.buf_struct_size = sizeof(struct hackrf_frame_buf);
	dev->vb_queue.ops = &hackrf_vb2_ops;
	dev->vb_queue.mem_ops = &vb2_vmalloc_memops;
	dev->vb_queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	ret = vb2_queue_init(&dev->vb_queue);
	if (ret) {
		dev_err(dev->dev, "Could not initialize vb2 queue\n");
		goto err_free_mem;
	}

	/* Init video_device structure */
	dev->vdev = hackrf_template;
	dev->vdev.queue = &dev->vb_queue;
	dev->vdev.queue->lock = &dev->vb_queue_lock;
	video_set_drvdata(&dev->vdev, dev);

	/* Register the v4l2_device structure */
	dev->v4l2_dev.release = hackrf_video_release;
	ret = v4l2_device_register(&intf->dev, &dev->v4l2_dev);
	if (ret) {
		dev_err(dev->dev, "Failed to register v4l2-device (%d)\n", ret);
		goto err_free_mem;
	}

	/* Register controls */
	v4l2_ctrl_handler_init(&dev->hdl, 4);
	dev->bandwidth_auto = v4l2_ctrl_new_std(&dev->hdl, &hackrf_ctrl_ops,
			V4L2_CID_RF_TUNER_BANDWIDTH_AUTO, 0, 1, 1, 1);
	dev->bandwidth = v4l2_ctrl_new_std(&dev->hdl, &hackrf_ctrl_ops,
			V4L2_CID_RF_TUNER_BANDWIDTH,
			1750000, 28000000, 50000, 1750000);
	v4l2_ctrl_auto_cluster(2, &dev->bandwidth_auto, 0, false);
	dev->lna_gain = v4l2_ctrl_new_std(&dev->hdl, &hackrf_ctrl_ops,
			V4L2_CID_RF_TUNER_LNA_GAIN, 0, 40, 8, 0);
	dev->if_gain = v4l2_ctrl_new_std(&dev->hdl, &hackrf_ctrl_ops,
			V4L2_CID_RF_TUNER_IF_GAIN, 0, 62, 2, 0);
	if (dev->hdl.error) {
		ret = dev->hdl.error;
		dev_err(dev->dev, "Could not initialize controls\n");
		goto err_free_controls;
	}

	v4l2_ctrl_handler_setup(&dev->hdl);

	dev->v4l2_dev.ctrl_handler = &dev->hdl;
	dev->vdev.v4l2_dev = &dev->v4l2_dev;
	dev->vdev.lock = &dev->v4l2_lock;

	ret = video_register_device(&dev->vdev, VFL_TYPE_SDR, -1);
	if (ret) {
		dev_err(dev->dev, "Failed to register as video device (%d)\n",
				ret);
		goto err_unregister_v4l2_dev;
	}
	dev_info(dev->dev, "Registered as %s\n",
			video_device_node_name(&dev->vdev));
	dev_notice(dev->dev, "SDR API is still slightly experimental and functionality changes may follow\n");
	return 0;

err_free_controls:
	v4l2_ctrl_handler_free(&dev->hdl);
err_unregister_v4l2_dev:
	v4l2_device_unregister(&dev->v4l2_dev);
err_free_mem:
	kfree(dev);
	return ret;
}

/* USB device ID list */
static struct usb_device_id hackrf_id_table[] = {
	{ USB_DEVICE(0x1d50, 0x6089) }, /* HackRF One */
	{ }
};
MODULE_DEVICE_TABLE(usb, hackrf_id_table);

/* USB subsystem interface */
static struct usb_driver hackrf_driver = {
	.name                     = KBUILD_MODNAME,
	.probe                    = hackrf_probe,
	.disconnect               = hackrf_disconnect,
	.id_table                 = hackrf_id_table,
};

module_usb_driver(hackrf_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("HackRF");
MODULE_LICENSE("GPL");
