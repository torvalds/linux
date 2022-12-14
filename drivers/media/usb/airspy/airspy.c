// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AirSpy SDR driver
 *
 * Copyright (C) 2014 Antti Palosaari <crope@iki.fi>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

/* AirSpy USB API commands (from AirSpy Library) */
enum {
	CMD_INVALID                       = 0x00,
	CMD_RECEIVER_MODE                 = 0x01,
	CMD_SI5351C_WRITE                 = 0x02,
	CMD_SI5351C_READ                  = 0x03,
	CMD_R820T_WRITE                   = 0x04,
	CMD_R820T_READ                    = 0x05,
	CMD_SPIFLASH_ERASE                = 0x06,
	CMD_SPIFLASH_WRITE                = 0x07,
	CMD_SPIFLASH_READ                 = 0x08,
	CMD_BOARD_ID_READ                 = 0x09,
	CMD_VERSION_STRING_READ           = 0x0a,
	CMD_BOARD_PARTID_SERIALNO_READ    = 0x0b,
	CMD_SET_SAMPLE_RATE               = 0x0c,
	CMD_SET_FREQ                      = 0x0d,
	CMD_SET_LNA_GAIN                  = 0x0e,
	CMD_SET_MIXER_GAIN                = 0x0f,
	CMD_SET_VGA_GAIN                  = 0x10,
	CMD_SET_LNA_AGC                   = 0x11,
	CMD_SET_MIXER_AGC                 = 0x12,
	CMD_SET_PACKING                   = 0x13,
};

/*
 *       bEndpointAddress     0x81  EP 1 IN
 *         Transfer Type            Bulk
 *       wMaxPacketSize     0x0200  1x 512 bytes
 */
#define MAX_BULK_BUFS            (6)
#define BULK_BUFFER_SIZE         (128 * 512)

static const struct v4l2_frequency_band bands[] = {
	{
		.tuner = 0,
		.type = V4L2_TUNER_ADC,
		.index = 0,
		.capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   = 20000000,
		.rangehigh  = 20000000,
	},
};

static const struct v4l2_frequency_band bands_rf[] = {
	{
		.tuner = 1,
		.type = V4L2_TUNER_RF,
		.index = 0,
		.capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   =   24000000,
		.rangehigh  = 1750000000,
	},
};

/* stream formats */
struct airspy_format {
	u32	pixelformat;
	u32	buffersize;
};

/* format descriptions for capture and preview */
static struct airspy_format formats[] = {
	{
		.pixelformat	= V4L2_SDR_FMT_RU12LE,
		.buffersize	= BULK_BUFFER_SIZE,
	},
};

static const unsigned int NUM_FORMATS = ARRAY_SIZE(formats);

/* intermediate buffers with raw data from the USB device */
struct airspy_frame_buf {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

struct airspy {
#define POWER_ON	   1
#define USB_STATE_URB_BUF  2
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
	struct mutex vb_queue_lock;  /* Protects vb_queue and capt_file */

	struct urb     *urb_list[MAX_BULK_BUFS];
	int            buf_num;
	unsigned long  buf_size;
	u8             *buf_list[MAX_BULK_BUFS];
	dma_addr_t     dma_addr[MAX_BULK_BUFS];
	int            urbs_initialized;
	int            urbs_submitted;

	/* USB control message buffer */
	#define BUF_SIZE 128
	u8 *buf;

	/* Current configuration */
	unsigned int f_adc;
	unsigned int f_rf;
	u32 pixelformat;
	u32 buffersize;

	/* Controls */
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *lna_gain_auto;
	struct v4l2_ctrl *lna_gain;
	struct v4l2_ctrl *mixer_gain_auto;
	struct v4l2_ctrl *mixer_gain;
	struct v4l2_ctrl *if_gain;

	/* Sample rate calc */
	unsigned long jiffies_next;
	unsigned int sample;
	unsigned int sample_measured;
};

#define airspy_dbg_usb_control_msg(_dev, _r, _t, _v, _i, _b, _l) { \
	char *_direction; \
	if (_t & USB_DIR_IN) \
		_direction = "<<<"; \
	else \
		_direction = ">>>"; \
	dev_dbg(_dev, "%02x %02x %02x %02x %02x %02x %02x %02x %s %*ph\n", \
			_t, _r, _v & 0xff, _v >> 8, _i & 0xff, _i >> 8, \
			_l & 0xff, _l >> 8, _direction, _l, _b); \
}

/* execute firmware command */
static int airspy_ctrl_msg(struct airspy *s, u8 request, u16 value, u16 index,
		u8 *data, u16 size)
{
	int ret;
	unsigned int pipe;
	u8 requesttype;

	switch (request) {
	case CMD_RECEIVER_MODE:
	case CMD_SET_FREQ:
		pipe = usb_sndctrlpipe(s->udev, 0);
		requesttype = (USB_TYPE_VENDOR | USB_DIR_OUT);
		break;
	case CMD_BOARD_ID_READ:
	case CMD_VERSION_STRING_READ:
	case CMD_BOARD_PARTID_SERIALNO_READ:
	case CMD_SET_LNA_GAIN:
	case CMD_SET_MIXER_GAIN:
	case CMD_SET_VGA_GAIN:
	case CMD_SET_LNA_AGC:
	case CMD_SET_MIXER_AGC:
		pipe = usb_rcvctrlpipe(s->udev, 0);
		requesttype = (USB_TYPE_VENDOR | USB_DIR_IN);
		break;
	default:
		dev_err(s->dev, "Unknown command %02x\n", request);
		ret = -EINVAL;
		goto err;
	}

	/* write request */
	if (!(requesttype & USB_DIR_IN))
		memcpy(s->buf, data, size);

	ret = usb_control_msg(s->udev, pipe, request, requesttype, value,
			index, s->buf, size, 1000);
	airspy_dbg_usb_control_msg(s->dev, request, requesttype, value,
			index, s->buf, size);
	if (ret < 0) {
		dev_err(s->dev, "usb_control_msg() failed %d request %02x\n",
				ret, request);
		goto err;
	}

	/* read request */
	if (requesttype & USB_DIR_IN)
		memcpy(data, s->buf, size);

	return 0;
err:
	return ret;
}

/* Private functions */
static struct airspy_frame_buf *airspy_get_next_fill_buf(struct airspy *s)
{
	unsigned long flags;
	struct airspy_frame_buf *buf = NULL;

	spin_lock_irqsave(&s->queued_bufs_lock, flags);
	if (list_empty(&s->queued_bufs))
		goto leave;

	buf = list_entry(s->queued_bufs.next,
			struct airspy_frame_buf, list);
	list_del(&buf->list);
leave:
	spin_unlock_irqrestore(&s->queued_bufs_lock, flags);
	return buf;
}

static unsigned int airspy_convert_stream(struct airspy *s,
		void *dst, void *src, unsigned int src_len)
{
	unsigned int dst_len;

	if (s->pixelformat == V4L2_SDR_FMT_RU12LE) {
		memcpy(dst, src, src_len);
		dst_len = src_len;
	} else {
		dst_len = 0;
	}

	/* calculate sample rate and output it in 10 seconds intervals */
	if (unlikely(time_is_before_jiffies(s->jiffies_next))) {
		#define MSECS 10000UL
		unsigned int msecs = jiffies_to_msecs(jiffies -
				s->jiffies_next + msecs_to_jiffies(MSECS));
		unsigned int samples = s->sample - s->sample_measured;

		s->jiffies_next = jiffies + msecs_to_jiffies(MSECS);
		s->sample_measured = s->sample;
		dev_dbg(s->dev, "slen=%u samples=%u msecs=%u sample rate=%lu\n",
				src_len, samples, msecs,
				samples * 1000UL / msecs);
	}

	/* total number of samples */
	s->sample += src_len / 2;

	return dst_len;
}

/*
 * This gets called for the bulk stream pipe. This is done in interrupt
 * time, so it has to be fast, not crash, and not stall. Neat.
 */
static void airspy_urb_complete(struct urb *urb)
{
	struct airspy *s = urb->context;
	struct airspy_frame_buf *fbuf;

	dev_dbg_ratelimited(s->dev, "status=%d length=%d/%d errors=%d\n",
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
		dev_err_ratelimited(s->dev, "URB failed %d\n", urb->status);
		break;
	}

	if (likely(urb->actual_length > 0)) {
		void *ptr;
		unsigned int len;
		/* get free framebuffer */
		fbuf = airspy_get_next_fill_buf(s);
		if (unlikely(fbuf == NULL)) {
			s->vb_full++;
			dev_notice_ratelimited(s->dev,
					"video buffer is full, %d packets dropped\n",
					s->vb_full);
			goto skip;
		}

		/* fill framebuffer */
		ptr = vb2_plane_vaddr(&fbuf->vb.vb2_buf, 0);
		len = airspy_convert_stream(s, ptr, urb->transfer_buffer,
				urb->actual_length);
		vb2_set_plane_payload(&fbuf->vb.vb2_buf, 0, len);
		fbuf->vb.vb2_buf.timestamp = ktime_get_ns();
		fbuf->vb.sequence = s->sequence++;
		vb2_buffer_done(&fbuf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}
skip:
	usb_submit_urb(urb, GFP_ATOMIC);
}

static int airspy_kill_urbs(struct airspy *s)
{
	int i;

	for (i = s->urbs_submitted - 1; i >= 0; i--) {
		dev_dbg(s->dev, "kill urb=%d\n", i);
		/* stop the URB */
		usb_kill_urb(s->urb_list[i]);
	}
	s->urbs_submitted = 0;

	return 0;
}

static int airspy_submit_urbs(struct airspy *s)
{
	int i, ret;

	for (i = 0; i < s->urbs_initialized; i++) {
		dev_dbg(s->dev, "submit urb=%d\n", i);
		ret = usb_submit_urb(s->urb_list[i], GFP_ATOMIC);
		if (ret) {
			dev_err(s->dev, "Could not submit URB no. %d - get them all back\n",
					i);
			airspy_kill_urbs(s);
			return ret;
		}
		s->urbs_submitted++;
	}

	return 0;
}

static int airspy_free_stream_bufs(struct airspy *s)
{
	if (test_bit(USB_STATE_URB_BUF, &s->flags)) {
		while (s->buf_num) {
			s->buf_num--;
			dev_dbg(s->dev, "free buf=%d\n", s->buf_num);
			usb_free_coherent(s->udev, s->buf_size,
					  s->buf_list[s->buf_num],
					  s->dma_addr[s->buf_num]);
		}
	}
	clear_bit(USB_STATE_URB_BUF, &s->flags);

	return 0;
}

static int airspy_alloc_stream_bufs(struct airspy *s)
{
	s->buf_num = 0;
	s->buf_size = BULK_BUFFER_SIZE;

	dev_dbg(s->dev, "all in all I will use %u bytes for streaming\n",
			MAX_BULK_BUFS * BULK_BUFFER_SIZE);

	for (s->buf_num = 0; s->buf_num < MAX_BULK_BUFS; s->buf_num++) {
		s->buf_list[s->buf_num] = usb_alloc_coherent(s->udev,
				BULK_BUFFER_SIZE, GFP_ATOMIC,
				&s->dma_addr[s->buf_num]);
		if (!s->buf_list[s->buf_num]) {
			dev_dbg(s->dev, "alloc buf=%d failed\n", s->buf_num);
			airspy_free_stream_bufs(s);
			return -ENOMEM;
		}

		dev_dbg(s->dev, "alloc buf=%d %p (dma %llu)\n", s->buf_num,
				s->buf_list[s->buf_num],
				(long long)s->dma_addr[s->buf_num]);
		set_bit(USB_STATE_URB_BUF, &s->flags);
	}

	return 0;
}

static int airspy_free_urbs(struct airspy *s)
{
	int i;

	airspy_kill_urbs(s);

	for (i = s->urbs_initialized - 1; i >= 0; i--) {
		if (s->urb_list[i]) {
			dev_dbg(s->dev, "free urb=%d\n", i);
			/* free the URBs */
			usb_free_urb(s->urb_list[i]);
		}
	}
	s->urbs_initialized = 0;

	return 0;
}

static int airspy_alloc_urbs(struct airspy *s)
{
	int i, j;

	/* allocate the URBs */
	for (i = 0; i < MAX_BULK_BUFS; i++) {
		dev_dbg(s->dev, "alloc urb=%d\n", i);
		s->urb_list[i] = usb_alloc_urb(0, GFP_ATOMIC);
		if (!s->urb_list[i]) {
			for (j = 0; j < i; j++) {
				usb_free_urb(s->urb_list[j]);
				s->urb_list[j] = NULL;
			}
			s->urbs_initialized = 0;
			return -ENOMEM;
		}
		usb_fill_bulk_urb(s->urb_list[i],
				s->udev,
				usb_rcvbulkpipe(s->udev, 0x81),
				s->buf_list[i],
				BULK_BUFFER_SIZE,
				airspy_urb_complete, s);

		s->urb_list[i]->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
		s->urb_list[i]->transfer_dma = s->dma_addr[i];
		s->urbs_initialized++;
	}

	return 0;
}

/* Must be called with vb_queue_lock hold */
static void airspy_cleanup_queued_bufs(struct airspy *s)
{
	unsigned long flags;

	dev_dbg(s->dev, "\n");

	spin_lock_irqsave(&s->queued_bufs_lock, flags);
	while (!list_empty(&s->queued_bufs)) {
		struct airspy_frame_buf *buf;

		buf = list_entry(s->queued_bufs.next,
				struct airspy_frame_buf, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&s->queued_bufs_lock, flags);
}

/* The user yanked out the cable... */
static void airspy_disconnect(struct usb_interface *intf)
{
	struct v4l2_device *v = usb_get_intfdata(intf);
	struct airspy *s = container_of(v, struct airspy, v4l2_dev);

	dev_dbg(s->dev, "\n");

	mutex_lock(&s->vb_queue_lock);
	mutex_lock(&s->v4l2_lock);
	/* No need to keep the urbs around after disconnection */
	s->udev = NULL;
	v4l2_device_disconnect(&s->v4l2_dev);
	video_unregister_device(&s->vdev);
	mutex_unlock(&s->v4l2_lock);
	mutex_unlock(&s->vb_queue_lock);

	v4l2_device_put(&s->v4l2_dev);
}

/* Videobuf2 operations */
static int airspy_queue_setup(struct vb2_queue *vq,
		unsigned int *nbuffers,
		unsigned int *nplanes, unsigned int sizes[], struct device *alloc_devs[])
{
	struct airspy *s = vb2_get_drv_priv(vq);

	dev_dbg(s->dev, "nbuffers=%d\n", *nbuffers);

	/* Need at least 8 buffers */
	if (vq->num_buffers + *nbuffers < 8)
		*nbuffers = 8 - vq->num_buffers;
	*nplanes = 1;
	sizes[0] = PAGE_ALIGN(s->buffersize);

	dev_dbg(s->dev, "nbuffers=%d sizes[0]=%d\n", *nbuffers, sizes[0]);
	return 0;
}

static void airspy_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct airspy *s = vb2_get_drv_priv(vb->vb2_queue);
	struct airspy_frame_buf *buf =
			container_of(vbuf, struct airspy_frame_buf, vb);
	unsigned long flags;

	/* Check the device has not disconnected between prep and queuing */
	if (unlikely(!s->udev)) {
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		return;
	}

	spin_lock_irqsave(&s->queued_bufs_lock, flags);
	list_add_tail(&buf->list, &s->queued_bufs);
	spin_unlock_irqrestore(&s->queued_bufs_lock, flags);
}

static int airspy_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct airspy *s = vb2_get_drv_priv(vq);
	int ret;

	dev_dbg(s->dev, "\n");

	if (!s->udev)
		return -ENODEV;

	mutex_lock(&s->v4l2_lock);

	s->sequence = 0;

	set_bit(POWER_ON, &s->flags);

	ret = airspy_alloc_stream_bufs(s);
	if (ret)
		goto err_clear_bit;

	ret = airspy_alloc_urbs(s);
	if (ret)
		goto err_free_stream_bufs;

	ret = airspy_submit_urbs(s);
	if (ret)
		goto err_free_urbs;

	/* start hardware streaming */
	ret = airspy_ctrl_msg(s, CMD_RECEIVER_MODE, 1, 0, NULL, 0);
	if (ret)
		goto err_kill_urbs;

	goto exit_mutex_unlock;

err_kill_urbs:
	airspy_kill_urbs(s);
err_free_urbs:
	airspy_free_urbs(s);
err_free_stream_bufs:
	airspy_free_stream_bufs(s);
err_clear_bit:
	clear_bit(POWER_ON, &s->flags);

	/* return all queued buffers to vb2 */
	{
		struct airspy_frame_buf *buf, *tmp;

		list_for_each_entry_safe(buf, tmp, &s->queued_bufs, list) {
			list_del(&buf->list);
			vb2_buffer_done(&buf->vb.vb2_buf,
					VB2_BUF_STATE_QUEUED);
		}
	}

exit_mutex_unlock:
	mutex_unlock(&s->v4l2_lock);

	return ret;
}

static void airspy_stop_streaming(struct vb2_queue *vq)
{
	struct airspy *s = vb2_get_drv_priv(vq);

	dev_dbg(s->dev, "\n");

	mutex_lock(&s->v4l2_lock);

	/* stop hardware streaming */
	airspy_ctrl_msg(s, CMD_RECEIVER_MODE, 0, 0, NULL, 0);

	airspy_kill_urbs(s);
	airspy_free_urbs(s);
	airspy_free_stream_bufs(s);

	airspy_cleanup_queued_bufs(s);

	clear_bit(POWER_ON, &s->flags);

	mutex_unlock(&s->v4l2_lock);
}

static const struct vb2_ops airspy_vb2_ops = {
	.queue_setup            = airspy_queue_setup,
	.buf_queue              = airspy_buf_queue,
	.start_streaming        = airspy_start_streaming,
	.stop_streaming         = airspy_stop_streaming,
	.wait_prepare           = vb2_ops_wait_prepare,
	.wait_finish            = vb2_ops_wait_finish,
};

static int airspy_querycap(struct file *file, void *fh,
		struct v4l2_capability *cap)
{
	struct airspy *s = video_drvdata(file);

	strscpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	strscpy(cap->card, s->vdev.name, sizeof(cap->card));
	usb_make_path(s->udev, cap->bus_info, sizeof(cap->bus_info));
	return 0;
}

static int airspy_enum_fmt_sdr_cap(struct file *file, void *priv,
		struct v4l2_fmtdesc *f)
{
	if (f->index >= NUM_FORMATS)
		return -EINVAL;

	f->pixelformat = formats[f->index].pixelformat;

	return 0;
}

static int airspy_g_fmt_sdr_cap(struct file *file, void *priv,
		struct v4l2_format *f)
{
	struct airspy *s = video_drvdata(file);

	f->fmt.sdr.pixelformat = s->pixelformat;
	f->fmt.sdr.buffersize = s->buffersize;

	return 0;
}

static int airspy_s_fmt_sdr_cap(struct file *file, void *priv,
		struct v4l2_format *f)
{
	struct airspy *s = video_drvdata(file);
	struct vb2_queue *q = &s->vb_queue;
	int i;

	if (vb2_is_busy(q))
		return -EBUSY;

	for (i = 0; i < NUM_FORMATS; i++) {
		if (formats[i].pixelformat == f->fmt.sdr.pixelformat) {
			s->pixelformat = formats[i].pixelformat;
			s->buffersize = formats[i].buffersize;
			f->fmt.sdr.buffersize = formats[i].buffersize;
			return 0;
		}
	}

	s->pixelformat = formats[0].pixelformat;
	s->buffersize = formats[0].buffersize;
	f->fmt.sdr.pixelformat = formats[0].pixelformat;
	f->fmt.sdr.buffersize = formats[0].buffersize;

	return 0;
}

static int airspy_try_fmt_sdr_cap(struct file *file, void *priv,
		struct v4l2_format *f)
{
	int i;

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

static int airspy_s_tuner(struct file *file, void *priv,
		const struct v4l2_tuner *v)
{
	int ret;

	if (v->index == 0)
		ret = 0;
	else if (v->index == 1)
		ret = 0;
	else
		ret = -EINVAL;

	return ret;
}

static int airspy_g_tuner(struct file *file, void *priv, struct v4l2_tuner *v)
{
	int ret;

	if (v->index == 0) {
		strscpy(v->name, "AirSpy ADC", sizeof(v->name));
		v->type = V4L2_TUNER_ADC;
		v->capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS;
		v->rangelow  = bands[0].rangelow;
		v->rangehigh = bands[0].rangehigh;
		ret = 0;
	} else if (v->index == 1) {
		strscpy(v->name, "AirSpy RF", sizeof(v->name));
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

static int airspy_g_frequency(struct file *file, void *priv,
		struct v4l2_frequency *f)
{
	struct airspy *s = video_drvdata(file);
	int ret;

	if (f->tuner == 0) {
		f->type = V4L2_TUNER_ADC;
		f->frequency = s->f_adc;
		dev_dbg(s->dev, "ADC frequency=%u Hz\n", s->f_adc);
		ret = 0;
	} else if (f->tuner == 1) {
		f->type = V4L2_TUNER_RF;
		f->frequency = s->f_rf;
		dev_dbg(s->dev, "RF frequency=%u Hz\n", s->f_rf);
		ret = 0;
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int airspy_s_frequency(struct file *file, void *priv,
		const struct v4l2_frequency *f)
{
	struct airspy *s = video_drvdata(file);
	int ret;
	u8 buf[4];

	if (f->tuner == 0) {
		s->f_adc = clamp_t(unsigned int, f->frequency,
				bands[0].rangelow,
				bands[0].rangehigh);
		dev_dbg(s->dev, "ADC frequency=%u Hz\n", s->f_adc);
		ret = 0;
	} else if (f->tuner == 1) {
		s->f_rf = clamp_t(unsigned int, f->frequency,
				bands_rf[0].rangelow,
				bands_rf[0].rangehigh);
		dev_dbg(s->dev, "RF frequency=%u Hz\n", s->f_rf);
		buf[0] = (s->f_rf >>  0) & 0xff;
		buf[1] = (s->f_rf >>  8) & 0xff;
		buf[2] = (s->f_rf >> 16) & 0xff;
		buf[3] = (s->f_rf >> 24) & 0xff;
		ret = airspy_ctrl_msg(s, CMD_SET_FREQ, 0, 0, buf, 4);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int airspy_enum_freq_bands(struct file *file, void *priv,
		struct v4l2_frequency_band *band)
{
	int ret;

	if (band->tuner == 0) {
		if (band->index >= ARRAY_SIZE(bands)) {
			ret = -EINVAL;
		} else {
			*band = bands[band->index];
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

static const struct v4l2_ioctl_ops airspy_ioctl_ops = {
	.vidioc_querycap          = airspy_querycap,

	.vidioc_enum_fmt_sdr_cap  = airspy_enum_fmt_sdr_cap,
	.vidioc_g_fmt_sdr_cap     = airspy_g_fmt_sdr_cap,
	.vidioc_s_fmt_sdr_cap     = airspy_s_fmt_sdr_cap,
	.vidioc_try_fmt_sdr_cap   = airspy_try_fmt_sdr_cap,

	.vidioc_reqbufs           = vb2_ioctl_reqbufs,
	.vidioc_create_bufs       = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf       = vb2_ioctl_prepare_buf,
	.vidioc_querybuf          = vb2_ioctl_querybuf,
	.vidioc_qbuf              = vb2_ioctl_qbuf,
	.vidioc_dqbuf             = vb2_ioctl_dqbuf,

	.vidioc_streamon          = vb2_ioctl_streamon,
	.vidioc_streamoff         = vb2_ioctl_streamoff,

	.vidioc_g_tuner           = airspy_g_tuner,
	.vidioc_s_tuner           = airspy_s_tuner,

	.vidioc_g_frequency       = airspy_g_frequency,
	.vidioc_s_frequency       = airspy_s_frequency,
	.vidioc_enum_freq_bands   = airspy_enum_freq_bands,

	.vidioc_subscribe_event   = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
	.vidioc_log_status        = v4l2_ctrl_log_status,
};

static const struct v4l2_file_operations airspy_fops = {
	.owner                    = THIS_MODULE,
	.open                     = v4l2_fh_open,
	.release                  = vb2_fop_release,
	.read                     = vb2_fop_read,
	.poll                     = vb2_fop_poll,
	.mmap                     = vb2_fop_mmap,
	.unlocked_ioctl           = video_ioctl2,
};

static const struct video_device airspy_template = {
	.name                     = "AirSpy SDR",
	.release                  = video_device_release_empty,
	.fops                     = &airspy_fops,
	.ioctl_ops                = &airspy_ioctl_ops,
};

static void airspy_video_release(struct v4l2_device *v)
{
	struct airspy *s = container_of(v, struct airspy, v4l2_dev);

	v4l2_ctrl_handler_free(&s->hdl);
	v4l2_device_unregister(&s->v4l2_dev);
	kfree(s->buf);
	kfree(s);
}

static int airspy_set_lna_gain(struct airspy *s)
{
	int ret;
	u8 u8tmp;

	dev_dbg(s->dev, "lna auto=%d->%d val=%d->%d\n",
			s->lna_gain_auto->cur.val, s->lna_gain_auto->val,
			s->lna_gain->cur.val, s->lna_gain->val);

	ret = airspy_ctrl_msg(s, CMD_SET_LNA_AGC, 0, s->lna_gain_auto->val,
			&u8tmp, 1);
	if (ret)
		goto err;

	if (s->lna_gain_auto->val == false) {
		ret = airspy_ctrl_msg(s, CMD_SET_LNA_GAIN, 0, s->lna_gain->val,
				&u8tmp, 1);
		if (ret)
			goto err;
	}
err:
	if (ret)
		dev_dbg(s->dev, "failed=%d\n", ret);

	return ret;
}

static int airspy_set_mixer_gain(struct airspy *s)
{
	int ret;
	u8 u8tmp;

	dev_dbg(s->dev, "mixer auto=%d->%d val=%d->%d\n",
			s->mixer_gain_auto->cur.val, s->mixer_gain_auto->val,
			s->mixer_gain->cur.val, s->mixer_gain->val);

	ret = airspy_ctrl_msg(s, CMD_SET_MIXER_AGC, 0, s->mixer_gain_auto->val,
			&u8tmp, 1);
	if (ret)
		goto err;

	if (s->mixer_gain_auto->val == false) {
		ret = airspy_ctrl_msg(s, CMD_SET_MIXER_GAIN, 0,
				s->mixer_gain->val, &u8tmp, 1);
		if (ret)
			goto err;
	}
err:
	if (ret)
		dev_dbg(s->dev, "failed=%d\n", ret);

	return ret;
}

static int airspy_set_if_gain(struct airspy *s)
{
	int ret;
	u8 u8tmp;

	dev_dbg(s->dev, "val=%d->%d\n", s->if_gain->cur.val, s->if_gain->val);

	ret = airspy_ctrl_msg(s, CMD_SET_VGA_GAIN, 0, s->if_gain->val,
			&u8tmp, 1);
	if (ret)
		dev_dbg(s->dev, "failed=%d\n", ret);

	return ret;
}

static int airspy_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct airspy *s = container_of(ctrl->handler, struct airspy, hdl);
	int ret;

	switch (ctrl->id) {
	case  V4L2_CID_RF_TUNER_LNA_GAIN_AUTO:
	case  V4L2_CID_RF_TUNER_LNA_GAIN:
		ret = airspy_set_lna_gain(s);
		break;
	case  V4L2_CID_RF_TUNER_MIXER_GAIN_AUTO:
	case  V4L2_CID_RF_TUNER_MIXER_GAIN:
		ret = airspy_set_mixer_gain(s);
		break;
	case  V4L2_CID_RF_TUNER_IF_GAIN:
		ret = airspy_set_if_gain(s);
		break;
	default:
		dev_dbg(s->dev, "unknown ctrl: id=%d name=%s\n",
				ctrl->id, ctrl->name);
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_ctrl_ops airspy_ctrl_ops = {
	.s_ctrl = airspy_s_ctrl,
};

static int airspy_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct airspy *s;
	int ret;
	u8 u8tmp, *buf;

	buf = NULL;
	ret = -ENOMEM;

	s = kzalloc(sizeof(struct airspy), GFP_KERNEL);
	if (s == NULL) {
		dev_err(&intf->dev, "Could not allocate memory for state\n");
		return -ENOMEM;
	}

	s->buf = kzalloc(BUF_SIZE, GFP_KERNEL);
	if (!s->buf)
		goto err_free_mem;
	buf = kzalloc(BUF_SIZE, GFP_KERNEL);
	if (!buf)
		goto err_free_mem;

	mutex_init(&s->v4l2_lock);
	mutex_init(&s->vb_queue_lock);
	spin_lock_init(&s->queued_bufs_lock);
	INIT_LIST_HEAD(&s->queued_bufs);
	s->dev = &intf->dev;
	s->udev = interface_to_usbdev(intf);
	s->f_adc = bands[0].rangelow;
	s->f_rf = bands_rf[0].rangelow;
	s->pixelformat = formats[0].pixelformat;
	s->buffersize = formats[0].buffersize;

	/* Detect device */
	ret = airspy_ctrl_msg(s, CMD_BOARD_ID_READ, 0, 0, &u8tmp, 1);
	if (ret == 0)
		ret = airspy_ctrl_msg(s, CMD_VERSION_STRING_READ, 0, 0,
				buf, BUF_SIZE);
	if (ret) {
		dev_err(s->dev, "Could not detect board\n");
		goto err_free_mem;
	}

	buf[BUF_SIZE - 1] = '\0';

	dev_info(s->dev, "Board ID: %02x\n", u8tmp);
	dev_info(s->dev, "Firmware version: %s\n", buf);

	/* Init videobuf2 queue structure */
	s->vb_queue.type = V4L2_BUF_TYPE_SDR_CAPTURE;
	s->vb_queue.io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	s->vb_queue.drv_priv = s;
	s->vb_queue.buf_struct_size = sizeof(struct airspy_frame_buf);
	s->vb_queue.ops = &airspy_vb2_ops;
	s->vb_queue.mem_ops = &vb2_vmalloc_memops;
	s->vb_queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	ret = vb2_queue_init(&s->vb_queue);
	if (ret) {
		dev_err(s->dev, "Could not initialize vb2 queue\n");
		goto err_free_mem;
	}

	/* Init video_device structure */
	s->vdev = airspy_template;
	s->vdev.queue = &s->vb_queue;
	s->vdev.queue->lock = &s->vb_queue_lock;
	video_set_drvdata(&s->vdev, s);

	/* Register the v4l2_device structure */
	s->v4l2_dev.release = airspy_video_release;
	ret = v4l2_device_register(&intf->dev, &s->v4l2_dev);
	if (ret) {
		dev_err(s->dev, "Failed to register v4l2-device (%d)\n", ret);
		goto err_free_mem;
	}

	/* Register controls */
	v4l2_ctrl_handler_init(&s->hdl, 5);
	s->lna_gain_auto = v4l2_ctrl_new_std(&s->hdl, &airspy_ctrl_ops,
			V4L2_CID_RF_TUNER_LNA_GAIN_AUTO, 0, 1, 1, 0);
	s->lna_gain = v4l2_ctrl_new_std(&s->hdl, &airspy_ctrl_ops,
			V4L2_CID_RF_TUNER_LNA_GAIN, 0, 14, 1, 8);
	v4l2_ctrl_auto_cluster(2, &s->lna_gain_auto, 0, false);
	s->mixer_gain_auto = v4l2_ctrl_new_std(&s->hdl, &airspy_ctrl_ops,
			V4L2_CID_RF_TUNER_MIXER_GAIN_AUTO, 0, 1, 1, 0);
	s->mixer_gain = v4l2_ctrl_new_std(&s->hdl, &airspy_ctrl_ops,
			V4L2_CID_RF_TUNER_MIXER_GAIN, 0, 15, 1, 8);
	v4l2_ctrl_auto_cluster(2, &s->mixer_gain_auto, 0, false);
	s->if_gain = v4l2_ctrl_new_std(&s->hdl, &airspy_ctrl_ops,
			V4L2_CID_RF_TUNER_IF_GAIN, 0, 15, 1, 0);
	if (s->hdl.error) {
		ret = s->hdl.error;
		dev_err(s->dev, "Could not initialize controls\n");
		goto err_free_controls;
	}

	v4l2_ctrl_handler_setup(&s->hdl);

	s->v4l2_dev.ctrl_handler = &s->hdl;
	s->vdev.v4l2_dev = &s->v4l2_dev;
	s->vdev.lock = &s->v4l2_lock;
	s->vdev.device_caps = V4L2_CAP_SDR_CAPTURE | V4L2_CAP_STREAMING |
			      V4L2_CAP_READWRITE | V4L2_CAP_TUNER;

	ret = video_register_device(&s->vdev, VFL_TYPE_SDR, -1);
	if (ret) {
		dev_err(s->dev, "Failed to register as video device (%d)\n",
				ret);
		goto err_free_controls;
	}

	/* Free buf if success*/
	kfree(buf);

	dev_info(s->dev, "Registered as %s\n",
			video_device_node_name(&s->vdev));
	dev_notice(s->dev, "SDR API is still slightly experimental and functionality changes may follow\n");
	return 0;

err_free_controls:
	v4l2_ctrl_handler_free(&s->hdl);
	v4l2_device_unregister(&s->v4l2_dev);
err_free_mem:
	kfree(buf);
	kfree(s->buf);
	kfree(s);
	return ret;
}

/* USB device ID list */
static const struct usb_device_id airspy_id_table[] = {
	{ USB_DEVICE(0x1d50, 0x60a1) }, /* AirSpy */
	{ }
};
MODULE_DEVICE_TABLE(usb, airspy_id_table);

/* USB subsystem interface */
static struct usb_driver airspy_driver = {
	.name                     = KBUILD_MODNAME,
	.probe                    = airspy_probe,
	.disconnect               = airspy_disconnect,
	.id_table                 = airspy_id_table,
};

module_usb_driver(airspy_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("AirSpy SDR");
MODULE_LICENSE("GPL");
