/*
 * vivid-sdr-cap.c - software defined radio support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/videodev2.h>
#include <linux/v4l2-dv-timings.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-dv-timings.h>

#include "vivid-core.h"
#include "vivid-ctrls.h"
#include "vivid-sdr-cap.h"

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
	    prandom_u32_max(100) < dev->perc_dropped_buffers)
		return;

	spin_lock(&dev->slock);
	if (!list_empty(&dev->sdr_cap_active)) {
		sdr_cap_buf = list_entry(dev->sdr_cap_active.next,
					 struct vivid_buffer, list);
		list_del(&sdr_cap_buf->list);
	}
	spin_unlock(&dev->slock);

	if (sdr_cap_buf) {
		sdr_cap_buf->vb.v4l2_buf.sequence = dev->sdr_cap_seq_count;
		vivid_sdr_cap_process(dev, sdr_cap_buf);
		v4l2_get_timestamp(&sdr_cap_buf->vb.v4l2_buf.timestamp);
		sdr_cap_buf->vb.v4l2_buf.timestamp.tv_sec += dev->time_wrap_offset;
		vb2_buffer_done(&sdr_cap_buf->vb, dev->dqbuf_error ?
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
	if (dev->seq_wrap)
		dev->sdr_cap_seq_offset = 0xffffff80U;
	dev->jiffies_sdr_cap = jiffies;
	dev->sdr_cap_seq_resync = false;

	for (;;) {
		try_to_freeze();
		if (kthread_should_stop())
			break;

		mutex_lock(&dev->mutex);
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
		buffers_since_start = (u64)jiffies_since_start * dev->sdr_adc_freq +
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
		dev->sdr_cap_seq_count = buffers_since_start + dev->sdr_cap_seq_offset;

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
		schedule_timeout_interruptible(wait_jiffies ? wait_jiffies : 1);
	}
	dprintk(dev, 1, "SDR Capture Thread End\n");
	return 0;
}

static int sdr_cap_queue_setup(struct vb2_queue *vq, const struct v4l2_format *fmt,
		       unsigned *nbuffers, unsigned *nplanes,
		       unsigned sizes[], void *alloc_ctxs[])
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
	struct vivid_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct vivid_buffer *buf = container_of(vb, struct vivid_buffer, vb);

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
	dev->sdr_cap_seq_count = 0;
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
			vb2_buffer_done(&buf->vb, VB2_BUF_STATE_QUEUED);
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

		buf = list_entry(dev->sdr_cap_active.next, struct vivid_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}

	/* shutdown control thread */
	mutex_unlock(&dev->mutex);
	kthread_stop(dev->kthread_sdr_cap);
	dev->kthread_sdr_cap = NULL;
	mutex_lock(&dev->mutex);
}

const struct vb2_ops vivid_sdr_cap_qops = {
	.queue_setup		= sdr_cap_queue_setup,
	.buf_prepare		= sdr_cap_buf_prepare,
	.buf_queue		= sdr_cap_buf_queue,
	.start_streaming	= sdr_cap_start_streaming,
	.stop_streaming		= sdr_cap_stop_streaming,
	.wait_prepare		= vivid_unlock,
	.wait_finish		= vivid_lock,
};

int vivid_sdr_enum_freq_bands(struct file *file, void *fh, struct v4l2_frequency_band *band)
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

int vivid_sdr_g_frequency(struct file *file, void *fh, struct v4l2_frequency *vf)
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

int vivid_sdr_s_frequency(struct file *file, void *fh, const struct v4l2_frequency *vf)
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
		strlcpy(vt->name, "ADC", sizeof(vt->name));
		vt->type = V4L2_TUNER_ADC;
		vt->capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS;
		vt->rangelow = bands_adc[0].rangelow;
		vt->rangehigh = bands_adc[2].rangehigh;
		return 0;
	case 1:
		strlcpy(vt->name, "RF", sizeof(vt->name));
		vt->type = V4L2_TUNER_RF;
		vt->capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS;
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
	if (f->index)
		return -EINVAL;
	f->pixelformat = V4L2_SDR_FMT_CU8;
	strlcpy(f->description, "IQ U8", sizeof(f->description));
	return 0;
}

int vidioc_g_fmt_sdr_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	f->fmt.sdr.pixelformat = V4L2_SDR_FMT_CU8;
	f->fmt.sdr.buffersize = SDR_CAP_SAMPLES_PER_BUF * 2;
	memset(f->fmt.sdr.reserved, 0, sizeof(f->fmt.sdr.reserved));
	return 0;
}

#define FIXP_FRAC    (1 << 15)
#define FIXP_PI      ((int)(FIXP_FRAC * 3.141592653589))

/* cos() from cx88 driver: cx88-dsp.c */
static s32 fixp_cos(unsigned int x)
{
	u32 t2, t4, t6, t8;
	u16 period = x / FIXP_PI;

	if (period % 2)
		return -fixp_cos(x - FIXP_PI);
	x = x % FIXP_PI;
	if (x > FIXP_PI/2)
		return -fixp_cos(FIXP_PI/2 - (x % (FIXP_PI/2)));
	/* Now x is between 0 and FIXP_PI/2.
	 * To calculate cos(x) we use it's Taylor polinom. */
	t2 = x*x/FIXP_FRAC/2;
	t4 = t2*x/FIXP_FRAC*x/FIXP_FRAC/3/4;
	t6 = t4*x/FIXP_FRAC*x/FIXP_FRAC/5/6;
	t8 = t6*x/FIXP_FRAC*x/FIXP_FRAC/7/8;
	return FIXP_FRAC-t2+t4-t6+t8;
}

static inline s32 fixp_sin(unsigned int x)
{
	return -fixp_cos(x + (FIXP_PI / 2));
}

void vivid_sdr_cap_process(struct vivid_dev *dev, struct vivid_buffer *buf)
{
	u8 *vbuf = vb2_plane_vaddr(&buf->vb, 0);
	unsigned long i;
	unsigned long plane_size = vb2_plane_size(&buf->vb, 0);
	int fixp_src_phase_step, fixp_i, fixp_q;

	/*
	 * TODO: Generated beep tone goes very crackly when sample rate is
	 * increased to ~1Msps or more. That is because of huge rounding error
	 * of phase angle caused by used cosine implementation.
	 */

	/* calculate phase step */
	#define BEEP_FREQ 1000 /* 1kHz beep */
	fixp_src_phase_step = DIV_ROUND_CLOSEST(2 * FIXP_PI * BEEP_FREQ,
			dev->sdr_adc_freq);

	for (i = 0; i < plane_size; i += 2) {
		dev->sdr_fixp_mod_phase += fixp_cos(dev->sdr_fixp_src_phase);
		dev->sdr_fixp_src_phase += fixp_src_phase_step;

		/*
		 * Transfer phases to [0 / 2xPI] in order to avoid variable
		 * overflow and make it suitable for cosine implementation
		 * used, which does not support negative angles.
		 */
		while (dev->sdr_fixp_mod_phase < (0 * FIXP_PI))
			dev->sdr_fixp_mod_phase += (2 * FIXP_PI);
		while (dev->sdr_fixp_mod_phase > (2 * FIXP_PI))
			dev->sdr_fixp_mod_phase -= (2 * FIXP_PI);

		while (dev->sdr_fixp_src_phase > (2 * FIXP_PI))
			dev->sdr_fixp_src_phase -= (2 * FIXP_PI);

		fixp_i = fixp_cos(dev->sdr_fixp_mod_phase);
		fixp_q = fixp_sin(dev->sdr_fixp_mod_phase);

		/* convert 'fixp float' to u8 */
		/* u8 = X * 127.5f + 127.5f; where X is float [-1.0 / +1.0] */
		fixp_i = fixp_i * 1275 + FIXP_FRAC * 1275;
		fixp_q = fixp_q * 1275 + FIXP_FRAC * 1275;
		*vbuf++ = DIV_ROUND_CLOSEST(fixp_i, FIXP_FRAC * 10);
		*vbuf++ = DIV_ROUND_CLOSEST(fixp_q, FIXP_FRAC * 10);
	}
}
