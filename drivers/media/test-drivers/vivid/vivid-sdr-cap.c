// SPDX-License-Identifier: GPL-2.0-only
/*
 * vivid-sdr-cap.c - software defined radio support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/math64.h>
#include <linux/videodev2.h>
#include <linux/v4l2-dv-timings.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-dv-timings.h>
#include <linux/fixp-arith.h>
#include <linux/jiffies.h>

#include "vivid-core.h"
#include "vivid-ctrls.h"
#include "vivid-sdr-cap.h"

/* stream formats */
struct vivid_format {
	u32	pixelformat;
	u32	buffersize;
};

/* format descriptions for capture and preview */
static const struct vivid_format formats[] = {
	{
		.pixelformat	= V4L2_SDR_FMT_CU8,
		.buffersize	= SDR_CAP_SAMPLES_PER_BUF * 2,
	}, {
		.pixelformat	= V4L2_SDR_FMT_CS8,
		.buffersize	= SDR_CAP_SAMPLES_PER_BUF * 2,
	},
};

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

/* ADC band midpoints */
#define BAND_ADC_0 ((bands_adc[0].rangehigh + bands_adc[1].rangelow) / 2)
#define BAND_ADC_1 ((bands_adc[1].rangehigh + bands_adc[2].rangelow) / 2)

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

static void vivid_thread_sdr_cap_tick(struct vivid_dev *dev)
{
	struct vivid_buffer *sdr_cap_buf = NULL;

	dprintk(dev, 1, "SDR Capture Thread Tick\n");

	/* Drop a certain percentage of buffers. */
	if (dev->perc_dropped_buffers &&
	    get_random_u32_below(100) < dev->perc_dropped_buffers)
		return;

	spin_lock(&dev->slock);
	if (!list_empty(&dev->sdr_cap_active)) {
		sdr_cap_buf = list_entry(dev->sdr_cap_active.next,
					 struct vivid_buffer, list);
		list_del(&sdr_cap_buf->list);
	}
	spin_unlock(&dev->slock);

	if (sdr_cap_buf) {
		sdr_cap_buf->vb.sequence = dev->sdr_cap_with_seq_wrap_count;
		v4l2_ctrl_request_setup(sdr_cap_buf->vb.vb2_buf.req_obj.req,
					&dev->ctrl_hdl_sdr_cap);
		v4l2_ctrl_request_complete(sdr_cap_buf->vb.vb2_buf.req_obj.req,
					   &dev->ctrl_hdl_sdr_cap);
		vivid_sdr_cap_process(dev, sdr_cap_buf);
		sdr_cap_buf->vb.vb2_buf.timestamp =
			ktime_get_ns() + dev->time_wrap_offset;
		vb2_buffer_done(&sdr_cap_buf->vb.vb2_buf, dev->dqbuf_error ?
				VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
		dev->dqbuf_error = false;
	}
}

static int vivid_thread_sdr_cap(void *data)
{
	struct vivid_dev *dev = data;
	u64 samples_since_start;
	u64 buffers_since_start;
	u64 next_jiffies_since_start;
	unsigned long jiffies_since_start;
	unsigned long cur_jiffies;
	unsigned wait_jiffies;

	dprintk(dev, 1, "SDR Capture Thread Start\n");

	set_freezable();

	/* Resets frame counters */
	dev->sdr_cap_seq_offset = 0;
	dev->sdr_cap_seq_count = 0;
	dev->jiffies_sdr_cap = jiffies;
	dev->sdr_cap_seq_resync = false;
	if (dev->time_wrap)
		dev->time_wrap_offset = dev->time_wrap - ktime_get_ns();
	else
		dev->time_wrap_offset = 0;

	for (;;) {
		try_to_freeze();
		if (kthread_should_stop())
			break;

		if (!mutex_trylock(&dev->mutex)) {
			schedule();
			continue;
		}

		cur_jiffies = jiffies;
		if (dev->sdr_cap_seq_resync) {
			dev->jiffies_sdr_cap = cur_jiffies;
			dev->sdr_cap_seq_offset = dev->sdr_cap_seq_count + 1;
			dev->sdr_cap_seq_count = 0;
			dev->sdr_cap_seq_resync = false;
		}
		/* Calculate the number of jiffies since we started streaming */
		jiffies_since_start = cur_jiffies - dev->jiffies_sdr_cap;
		/* Get the number of buffers streamed since the start */
		buffers_since_start =
			(u64)jiffies_since_start * dev->sdr_adc_freq +
				      (HZ * SDR_CAP_SAMPLES_PER_BUF) / 2;
		do_div(buffers_since_start, HZ * SDR_CAP_SAMPLES_PER_BUF);

		/*
		 * After more than 0xf0000000 (rounded down to a multiple of
		 * 'jiffies-per-day' to ease jiffies_to_msecs calculation)
		 * jiffies have passed since we started streaming reset the
		 * counters and keep track of the sequence offset.
		 */
		if (jiffies_since_start > JIFFIES_RESYNC) {
			dev->jiffies_sdr_cap = cur_jiffies;
			dev->sdr_cap_seq_offset = buffers_since_start;
			buffers_since_start = 0;
		}
		dev->sdr_cap_seq_count =
			buffers_since_start + dev->sdr_cap_seq_offset;
		dev->sdr_cap_with_seq_wrap_count = dev->sdr_cap_seq_count - dev->sdr_cap_seq_start;

		vivid_thread_sdr_cap_tick(dev);
		mutex_unlock(&dev->mutex);

		/*
		 * Calculate the number of samples streamed since we started,
		 * not including the current buffer.
		 */
		samples_since_start = buffers_since_start * SDR_CAP_SAMPLES_PER_BUF;

		/* And the number of jiffies since we started */
		jiffies_since_start = jiffies - dev->jiffies_sdr_cap;

		/* Increase by the number of samples in one buffer */
		samples_since_start += SDR_CAP_SAMPLES_PER_BUF;
		/*
		 * Calculate when that next buffer is supposed to start
		 * in jiffies since we started streaming.
		 */
		next_jiffies_since_start = samples_since_start * HZ +
					   dev->sdr_adc_freq / 2;
		do_div(next_jiffies_since_start, dev->sdr_adc_freq);
		/* If it is in the past, then just schedule asap */
		if (next_jiffies_since_start < jiffies_since_start)
			next_jiffies_since_start = jiffies_since_start;

		wait_jiffies = next_jiffies_since_start - jiffies_since_start;
		while (time_is_after_jiffies(cur_jiffies + wait_jiffies) &&
		       !kthread_should_stop())
			schedule();
	}
	dprintk(dev, 1, "SDR Capture Thread End\n");
	return 0;
}

static int sdr_cap_queue_setup(struct vb2_queue *vq,
		       unsigned *nbuffers, unsigned *nplanes,
		       unsigned sizes[], struct device *alloc_devs[])
{
	/* 2 = max 16-bit sample returned */
	sizes[0] = SDR_CAP_SAMPLES_PER_BUF * 2;
	*nplanes = 1;
	return 0;
}

static int sdr_cap_buf_prepare(struct vb2_buffer *vb)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	unsigned size = SDR_CAP_SAMPLES_PER_BUF * 2;

	dprintk(dev, 1, "%s\n", __func__);

	if (dev->buf_prepare_error) {
		/*
		 * Error injection: test what happens if buf_prepare() returns
		 * an error.
		 */
		dev->buf_prepare_error = false;
		return -EINVAL;
	}
	if (vb2_plane_size(vb, 0) < size) {
		dprintk(dev, 1, "%s data will not fit into plane (%lu < %u)\n",
				__func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}
	vb2_set_plane_payload(vb, 0, size);

	return 0;
}

static void sdr_cap_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vivid_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct vivid_buffer *buf = container_of(vbuf, struct vivid_buffer, vb);

	dprintk(dev, 1, "%s\n", __func__);

	spin_lock(&dev->slock);
	list_add_tail(&buf->list, &dev->sdr_cap_active);
	spin_unlock(&dev->slock);
}

static int sdr_cap_start_streaming(struct vb2_queue *vq, unsigned count)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vq);
	int err = 0;

	dprintk(dev, 1, "%s\n", __func__);
	dev->sdr_cap_seq_start = dev->seq_wrap * 128;
	if (dev->start_streaming_error) {
		dev->start_streaming_error = false;
		err = -EINVAL;
	} else if (dev->kthread_sdr_cap == NULL) {
		dev->kthread_sdr_cap = kthread_run(vivid_thread_sdr_cap, dev,
				"%s-sdr-cap", dev->v4l2_dev.name);

		if (IS_ERR(dev->kthread_sdr_cap)) {
			v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
			err = PTR_ERR(dev->kthread_sdr_cap);
			dev->kthread_sdr_cap = NULL;
		}
	}
	if (err) {
		struct vivid_buffer *buf, *tmp;

		list_for_each_entry_safe(buf, tmp, &dev->sdr_cap_active, list) {
			list_del(&buf->list);
			vb2_buffer_done(&buf->vb.vb2_buf,
					VB2_BUF_STATE_QUEUED);
		}
	}
	return err;
}

/* abort streaming and wait for last buffer */
static void sdr_cap_stop_streaming(struct vb2_queue *vq)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vq);

	if (dev->kthread_sdr_cap == NULL)
		return;

	while (!list_empty(&dev->sdr_cap_active)) {
		struct vivid_buffer *buf;

		buf = list_entry(dev->sdr_cap_active.next,
				struct vivid_buffer, list);
		list_del(&buf->list);
		v4l2_ctrl_request_complete(buf->vb.vb2_buf.req_obj.req,
					   &dev->ctrl_hdl_sdr_cap);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	/* shutdown control thread */
	kthread_stop(dev->kthread_sdr_cap);
	dev->kthread_sdr_cap = NULL;
}

static void sdr_cap_buf_request_complete(struct vb2_buffer *vb)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_ctrl_request_complete(vb->req_obj.req, &dev->ctrl_hdl_sdr_cap);
}

const struct vb2_ops vivid_sdr_cap_qops = {
	.queue_setup		= sdr_cap_queue_setup,
	.buf_prepare		= sdr_cap_buf_prepare,
	.buf_queue		= sdr_cap_buf_queue,
	.start_streaming	= sdr_cap_start_streaming,
	.stop_streaming		= sdr_cap_stop_streaming,
	.buf_request_complete	= sdr_cap_buf_request_complete,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

int vivid_sdr_enum_freq_bands(struct file *file, void *fh,
		struct v4l2_frequency_band *band)
{
	switch (band->tuner) {
	case 0:
		if (band->index >= ARRAY_SIZE(bands_adc))
			return -EINVAL;
		*band = bands_adc[band->index];
		return 0;
	case 1:
		if (band->index >= ARRAY_SIZE(bands_fm))
			return -EINVAL;
		*band = bands_fm[band->index];
		return 0;
	default:
		return -EINVAL;
	}
}

int vivid_sdr_g_frequency(struct file *file, void *fh,
		struct v4l2_frequency *vf)
{
	struct vivid_dev *dev = video_drvdata(file);

	switch (vf->tuner) {
	case 0:
		vf->frequency = dev->sdr_adc_freq;
		vf->type = V4L2_TUNER_ADC;
		return 0;
	case 1:
		vf->frequency = dev->sdr_fm_freq;
		vf->type = V4L2_TUNER_RF;
		return 0;
	default:
		return -EINVAL;
	}
}

int vivid_sdr_s_frequency(struct file *file, void *fh,
		const struct v4l2_frequency *vf)
{
	struct vivid_dev *dev = video_drvdata(file);
	unsigned freq = vf->frequency;
	unsigned band;

	switch (vf->tuner) {
	case 0:
		if (vf->type != V4L2_TUNER_ADC)
			return -EINVAL;
		if (freq < BAND_ADC_0)
			band = 0;
		else if (freq < BAND_ADC_1)
			band = 1;
		else
			band = 2;

		freq = clamp_t(unsigned, freq,
				bands_adc[band].rangelow,
				bands_adc[band].rangehigh);

		if (vb2_is_streaming(&dev->vb_sdr_cap_q) &&
		    freq != dev->sdr_adc_freq) {
			/* resync the thread's timings */
			dev->sdr_cap_seq_resync = true;
		}
		dev->sdr_adc_freq = freq;
		return 0;
	case 1:
		if (vf->type != V4L2_TUNER_RF)
			return -EINVAL;
		dev->sdr_fm_freq = clamp_t(unsigned, freq,
				bands_fm[0].rangelow,
				bands_fm[0].rangehigh);
		return 0;
	default:
		return -EINVAL;
	}
}

int vivid_sdr_g_tuner(struct file *file, void *fh, struct v4l2_tuner *vt)
{
	switch (vt->index) {
	case 0:
		strscpy(vt->name, "ADC", sizeof(vt->name));
		vt->type = V4L2_TUNER_ADC;
		vt->capability =
			V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS;
		vt->rangelow = bands_adc[0].rangelow;
		vt->rangehigh = bands_adc[2].rangehigh;
		return 0;
	case 1:
		strscpy(vt->name, "RF", sizeof(vt->name));
		vt->type = V4L2_TUNER_RF;
		vt->capability =
			V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS;
		vt->rangelow = bands_fm[0].rangelow;
		vt->rangehigh = bands_fm[0].rangehigh;
		return 0;
	default:
		return -EINVAL;
	}
}

int vivid_sdr_s_tuner(struct file *file, void *fh, const struct v4l2_tuner *vt)
{
	if (vt->index > 1)
		return -EINVAL;
	return 0;
}

int vidioc_enum_fmt_sdr_cap(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(formats))
		return -EINVAL;
	f->pixelformat = formats[f->index].pixelformat;
	return 0;
}

int vidioc_g_fmt_sdr_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vivid_dev *dev = video_drvdata(file);

	f->fmt.sdr.pixelformat = dev->sdr_pixelformat;
	f->fmt.sdr.buffersize = dev->sdr_buffersize;
	return 0;
}

int vidioc_s_fmt_sdr_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct vb2_queue *q = &dev->vb_sdr_cap_q;
	int i;

	if (vb2_is_busy(q))
		return -EBUSY;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (formats[i].pixelformat == f->fmt.sdr.pixelformat) {
			dev->sdr_pixelformat = formats[i].pixelformat;
			dev->sdr_buffersize = formats[i].buffersize;
			f->fmt.sdr.buffersize = formats[i].buffersize;
			return 0;
		}
	}
	dev->sdr_pixelformat = formats[0].pixelformat;
	dev->sdr_buffersize = formats[0].buffersize;
	f->fmt.sdr.pixelformat = formats[0].pixelformat;
	f->fmt.sdr.buffersize = formats[0].buffersize;
	return 0;
}

int vidioc_try_fmt_sdr_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (formats[i].pixelformat == f->fmt.sdr.pixelformat) {
			f->fmt.sdr.buffersize = formats[i].buffersize;
			return 0;
		}
	}
	f->fmt.sdr.pixelformat = formats[0].pixelformat;
	f->fmt.sdr.buffersize = formats[0].buffersize;
	return 0;
}

#define FIXP_N    (15)
#define FIXP_FRAC (1 << FIXP_N)
#define FIXP_2PI  ((int)(2 * 3.141592653589 * FIXP_FRAC))
#define M_100000PI (3.14159 * 100000)

void vivid_sdr_cap_process(struct vivid_dev *dev, struct vivid_buffer *buf)
{
	u8 *vbuf = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
	unsigned long i;
	unsigned long plane_size = vb2_plane_size(&buf->vb.vb2_buf, 0);
	s64 s64tmp;
	s32 src_phase_step;
	s32 mod_phase_step;
	s32 fixp_i;
	s32 fixp_q;

	/* calculate phase step */
	#define BEEP_FREQ 1000 /* 1kHz beep */
	src_phase_step = DIV_ROUND_CLOSEST(FIXP_2PI * BEEP_FREQ,
					   dev->sdr_adc_freq);

	for (i = 0; i < plane_size; i += 2) {
		mod_phase_step = fixp_cos32_rad(dev->sdr_fixp_src_phase,
						FIXP_2PI) >> (31 - FIXP_N);

		dev->sdr_fixp_src_phase += src_phase_step;
		s64tmp = (s64) mod_phase_step * dev->sdr_fm_deviation;
		dev->sdr_fixp_mod_phase += div_s64(s64tmp, M_100000PI);

		/*
		 * Transfer phase angle to [0, 2xPI] in order to avoid variable
		 * overflow and make it suitable for cosine implementation
		 * used, which does not support negative angles.
		 */
		dev->sdr_fixp_src_phase %= FIXP_2PI;
		dev->sdr_fixp_mod_phase %= FIXP_2PI;

		if (dev->sdr_fixp_mod_phase < 0)
			dev->sdr_fixp_mod_phase += FIXP_2PI;

		fixp_i = fixp_cos32_rad(dev->sdr_fixp_mod_phase, FIXP_2PI);
		fixp_q = fixp_sin32_rad(dev->sdr_fixp_mod_phase, FIXP_2PI);

		/* Normalize fraction values represented with 32 bit precision
		 * to fixed point representation with FIXP_N bits */
		fixp_i >>= (31 - FIXP_N);
		fixp_q >>= (31 - FIXP_N);

		switch (dev->sdr_pixelformat) {
		case V4L2_SDR_FMT_CU8:
			/* convert 'fixp float' to u8 [0, +255] */
			/* u8 = X * 127.5 + 127.5; X is float [-1.0, +1.0] */
			fixp_i = fixp_i * 1275 + FIXP_FRAC * 1275;
			fixp_q = fixp_q * 1275 + FIXP_FRAC * 1275;
			*vbuf++ = DIV_ROUND_CLOSEST(fixp_i, FIXP_FRAC * 10);
			*vbuf++ = DIV_ROUND_CLOSEST(fixp_q, FIXP_FRAC * 10);
			break;
		case V4L2_SDR_FMT_CS8:
			/* convert 'fixp float' to s8 [-128, +127] */
			/* s8 = X * 127.5 - 0.5; X is float [-1.0, +1.0] */
			fixp_i = fixp_i * 1275 - FIXP_FRAC * 5;
			fixp_q = fixp_q * 1275 - FIXP_FRAC * 5;
			*vbuf++ = DIV_ROUND_CLOSEST(fixp_i, FIXP_FRAC * 10);
			*vbuf++ = DIV_ROUND_CLOSEST(fixp_q, FIXP_FRAC * 10);
			break;
		default:
			break;
		}
	}
}
