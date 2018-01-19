/*
 *  ALSA PCM device for the
 *  ALSA interface to cobalt PCM capture streams
 *
 *  Copyright 2014-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 *
 *  This program is free software; you may redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 *  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 *  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>

#include <media/v4l2-device.h>

#include <sound/core.h>
#include <sound/pcm.h>

#include "cobalt-driver.h"
#include "cobalt-alsa.h"
#include "cobalt-alsa-pcm.h"

static unsigned int pcm_debug;
module_param(pcm_debug, int, 0644);
MODULE_PARM_DESC(pcm_debug, "enable debug messages for pcm");

#define dprintk(fmt, arg...) \
	do { \
		if (pcm_debug) \
			pr_info("cobalt-alsa-pcm %s: " fmt, __func__, ##arg); \
	} while (0)

static const struct snd_pcm_hardware snd_cobalt_hdmi_capture = {
	.info = SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP           |
		SNDRV_PCM_INFO_INTERLEAVED    |
		SNDRV_PCM_INFO_MMAP_VALID,

	.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,

	.rates = SNDRV_PCM_RATE_48000,

	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 8,
	.buffer_bytes_max = 4 * 240 * 8 * 4,	/* 5 ms of data */
	.period_bytes_min = 1920,		/* 1 sample = 8 * 4 bytes */
	.period_bytes_max = 240 * 8 * 4,	/* 5 ms of 8 channel data */
	.periods_min = 1,
	.periods_max = 4,
};

static const struct snd_pcm_hardware snd_cobalt_playback = {
	.info = SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP           |
		SNDRV_PCM_INFO_INTERLEAVED    |
		SNDRV_PCM_INFO_MMAP_VALID,

	.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,

	.rates = SNDRV_PCM_RATE_48000,

	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 8,
	.buffer_bytes_max = 4 * 240 * 8 * 4,	/* 5 ms of data */
	.period_bytes_min = 1920,		/* 1 sample = 8 * 4 bytes */
	.period_bytes_max = 240 * 8 * 4,	/* 5 ms of 8 channel data */
	.periods_min = 1,
	.periods_max = 4,
};

static void sample_cpy(u8 *dst, const u8 *src, u32 len, bool is_s32)
{
	static const unsigned map[8] = { 0, 1, 5, 4, 2, 3, 6, 7 };
	unsigned idx = 0;

	while (len >= (is_s32 ? 4 : 2)) {
		unsigned offset = map[idx] * 4;
		u32 val = src[offset + 1] + (src[offset + 2] << 8) +
			 (src[offset + 3] << 16);

		if (is_s32) {
			*dst++ = 0;
			*dst++ = val & 0xff;
		}
		*dst++ = (val >> 8) & 0xff;
		*dst++ = (val >> 16) & 0xff;
		len -= is_s32 ? 4 : 2;
		idx++;
	}
}

static void cobalt_alsa_announce_pcm_data(struct snd_cobalt_card *cobsc,
					u8 *pcm_data,
					size_t skip,
					size_t samples)
{
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	unsigned long flags;
	unsigned int oldptr;
	unsigned int stride;
	int length = samples;
	int period_elapsed = 0;
	bool is_s32;

	dprintk("cobalt alsa announce ptr=%p data=%p num_bytes=%zd\n", cobsc,
		pcm_data, samples);

	substream = cobsc->capture_pcm_substream;
	if (substream == NULL) {
		dprintk("substream was NULL\n");
		return;
	}

	runtime = substream->runtime;
	if (runtime == NULL) {
		dprintk("runtime was NULL\n");
		return;
	}
	is_s32 = runtime->format == SNDRV_PCM_FORMAT_S32_LE;

	stride = runtime->frame_bits >> 3;
	if (stride == 0) {
		dprintk("stride is zero\n");
		return;
	}

	if (length == 0) {
		dprintk("%s: length was zero\n", __func__);
		return;
	}

	if (runtime->dma_area == NULL) {
		dprintk("dma area was NULL - ignoring\n");
		return;
	}

	oldptr = cobsc->hwptr_done_capture;
	if (oldptr + length >= runtime->buffer_size) {
		unsigned int cnt = runtime->buffer_size - oldptr;
		unsigned i;

		for (i = 0; i < cnt; i++)
			sample_cpy(runtime->dma_area + (oldptr + i) * stride,
					pcm_data + i * skip,
					stride, is_s32);
		for (i = cnt; i < length; i++)
			sample_cpy(runtime->dma_area + (i - cnt) * stride,
					pcm_data + i * skip, stride, is_s32);
	} else {
		unsigned i;

		for (i = 0; i < length; i++)
			sample_cpy(runtime->dma_area + (oldptr + i) * stride,
					pcm_data + i * skip,
					stride, is_s32);
	}
	snd_pcm_stream_lock_irqsave(substream, flags);

	cobsc->hwptr_done_capture += length;
	if (cobsc->hwptr_done_capture >=
	    runtime->buffer_size)
		cobsc->hwptr_done_capture -=
			runtime->buffer_size;

	cobsc->capture_transfer_done += length;
	if (cobsc->capture_transfer_done >=
	    runtime->period_size) {
		cobsc->capture_transfer_done -=
			runtime->period_size;
		period_elapsed = 1;
	}

	snd_pcm_stream_unlock_irqrestore(substream, flags);

	if (period_elapsed)
		snd_pcm_period_elapsed(substream);
}

static int alsa_fnc(struct vb2_buffer *vb, void *priv)
{
	struct cobalt_stream *s = priv;
	unsigned char *p = vb2_plane_vaddr(vb, 0);
	int i;

	if (pcm_debug) {
		pr_info("alsa: ");
		for (i = 0; i < 8 * 4; i++) {
			if (!(i & 3))
				pr_cont(" ");
			pr_cont("%02x", p[i]);
		}
		pr_cont("\n");
	}
	cobalt_alsa_announce_pcm_data(s->alsa,
			vb2_plane_vaddr(vb, 0),
			8 * 4,
			vb2_get_plane_payload(vb, 0) / (8 * 4));
	return 0;
}

static int snd_cobalt_pcm_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_cobalt_card *cobsc = snd_pcm_substream_chip(substream);
	struct cobalt_stream *s = cobsc->s;

	runtime->hw = snd_cobalt_hdmi_capture;
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	cobsc->capture_pcm_substream = substream;
	runtime->private_data = s;
	cobsc->alsa_record_cnt++;
	if (cobsc->alsa_record_cnt == 1) {
		int rc;

		rc = vb2_thread_start(&s->q, alsa_fnc, s, s->vdev.name);
		if (rc) {
			cobsc->alsa_record_cnt--;
			return rc;
		}
	}
	return 0;
}

static int snd_cobalt_pcm_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_cobalt_card *cobsc = snd_pcm_substream_chip(substream);
	struct cobalt_stream *s = cobsc->s;

	cobsc->alsa_record_cnt--;
	if (cobsc->alsa_record_cnt == 0)
		vb2_thread_stop(&s->q);
	return 0;
}

static int snd_cobalt_pcm_ioctl(struct snd_pcm_substream *substream,
		     unsigned int cmd, void *arg)
{
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}


static int snd_pcm_alloc_vmalloc_buffer(struct snd_pcm_substream *subs,
					size_t size)
{
	struct snd_pcm_runtime *runtime = subs->runtime;

	dprintk("Allocating vbuffer\n");
	if (runtime->dma_area) {
		if (runtime->dma_bytes > size)
			return 0;

		vfree(runtime->dma_area);
	}
	runtime->dma_area = vmalloc(size);
	if (!runtime->dma_area)
		return -ENOMEM;

	runtime->dma_bytes = size;

	return 0;
}

static int snd_cobalt_pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params)
{
	dprintk("%s called\n", __func__);

	return snd_pcm_alloc_vmalloc_buffer(substream,
					   params_buffer_bytes(params));
}

static int snd_cobalt_pcm_hw_free(struct snd_pcm_substream *substream)
{
	if (substream->runtime->dma_area) {
		dprintk("freeing pcm capture region\n");
		vfree(substream->runtime->dma_area);
		substream->runtime->dma_area = NULL;
	}

	return 0;
}

static int snd_cobalt_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_cobalt_card *cobsc = snd_pcm_substream_chip(substream);

	cobsc->hwptr_done_capture = 0;
	cobsc->capture_transfer_done = 0;

	return 0;
}

static int snd_cobalt_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_STOP:
		return 0;
	default:
		return -EINVAL;
	}
	return 0;
}

static
snd_pcm_uframes_t snd_cobalt_pcm_pointer(struct snd_pcm_substream *substream)
{
	snd_pcm_uframes_t hwptr_done;
	struct snd_cobalt_card *cobsc = snd_pcm_substream_chip(substream);

	hwptr_done = cobsc->hwptr_done_capture;

	return hwptr_done;
}

static void pb_sample_cpy(u8 *dst, const u8 *src, u32 len, bool is_s32)
{
	static const unsigned map[8] = { 0, 1, 5, 4, 2, 3, 6, 7 };
	unsigned idx = 0;

	while (len >= (is_s32 ? 4 : 2)) {
		unsigned offset = map[idx] * 4;
		u8 *out = dst + offset;

		*out++ = 0;
		if (is_s32) {
			src++;
			*out++ = *src++;
		} else {
			*out++ = 0;
		}
		*out++ = *src++;
		*out = *src++;
		len -= is_s32 ? 4 : 2;
		idx++;
	}
}

static void cobalt_alsa_pb_pcm_data(struct snd_cobalt_card *cobsc,
					u8 *pcm_data,
					size_t skip,
					size_t samples)
{
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	unsigned long flags;
	unsigned int pos;
	unsigned int stride;
	bool is_s32;
	unsigned i;

	dprintk("cobalt alsa pb ptr=%p data=%p samples=%zd\n", cobsc,
		pcm_data, samples);

	substream = cobsc->playback_pcm_substream;
	if (substream == NULL) {
		dprintk("substream was NULL\n");
		return;
	}

	runtime = substream->runtime;
	if (runtime == NULL) {
		dprintk("runtime was NULL\n");
		return;
	}

	is_s32 = runtime->format == SNDRV_PCM_FORMAT_S32_LE;
	stride = runtime->frame_bits >> 3;
	if (stride == 0) {
		dprintk("stride is zero\n");
		return;
	}

	if (samples == 0) {
		dprintk("%s: samples was zero\n", __func__);
		return;
	}

	if (runtime->dma_area == NULL) {
		dprintk("dma area was NULL - ignoring\n");
		return;
	}

	pos = cobsc->pb_pos % cobsc->pb_size;
	for (i = 0; i < cobsc->pb_count / (8 * 4); i++)
		pb_sample_cpy(pcm_data + i * skip,
				runtime->dma_area + pos + i * stride,
				stride, is_s32);
	snd_pcm_stream_lock_irqsave(substream, flags);

	cobsc->pb_pos += i * stride;

	snd_pcm_stream_unlock_irqrestore(substream, flags);
	if (cobsc->pb_pos % cobsc->pb_count == 0)
		snd_pcm_period_elapsed(substream);
}

static int alsa_pb_fnc(struct vb2_buffer *vb, void *priv)
{
	struct cobalt_stream *s = priv;

	if (s->alsa->alsa_pb_channel)
		cobalt_alsa_pb_pcm_data(s->alsa,
				vb2_plane_vaddr(vb, 0),
				8 * 4,
				vb2_get_plane_payload(vb, 0) / (8 * 4));
	return 0;
}

static int snd_cobalt_pcm_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_cobalt_card *cobsc = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct cobalt_stream *s = cobsc->s;

	runtime->hw = snd_cobalt_playback;
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	cobsc->playback_pcm_substream = substream;
	runtime->private_data = s;
	cobsc->alsa_playback_cnt++;
	if (cobsc->alsa_playback_cnt == 1) {
		int rc;

		rc = vb2_thread_start(&s->q, alsa_pb_fnc, s, s->vdev.name);
		if (rc) {
			cobsc->alsa_playback_cnt--;
			return rc;
		}
	}

	return 0;
}

static int snd_cobalt_pcm_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_cobalt_card *cobsc = snd_pcm_substream_chip(substream);
	struct cobalt_stream *s = cobsc->s;

	cobsc->alsa_playback_cnt--;
	if (cobsc->alsa_playback_cnt == 0)
		vb2_thread_stop(&s->q);
	return 0;
}

static int snd_cobalt_pcm_pb_prepare(struct snd_pcm_substream *substream)
{
	struct snd_cobalt_card *cobsc = snd_pcm_substream_chip(substream);

	cobsc->pb_size = snd_pcm_lib_buffer_bytes(substream);
	cobsc->pb_count = snd_pcm_lib_period_bytes(substream);
	cobsc->pb_pos = 0;

	return 0;
}

static int snd_cobalt_pcm_pb_trigger(struct snd_pcm_substream *substream,
				     int cmd)
{
	struct snd_cobalt_card *cobsc = snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (cobsc->alsa_pb_channel)
			return -EBUSY;
		cobsc->alsa_pb_channel = true;
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
		cobsc->alsa_pb_channel = false;
		return 0;
	default:
		return -EINVAL;
	}
}

static
snd_pcm_uframes_t snd_cobalt_pcm_pb_pointer(struct snd_pcm_substream *substream)
{
	struct snd_cobalt_card *cobsc = snd_pcm_substream_chip(substream);
	size_t ptr;

	ptr = cobsc->pb_pos;

	return bytes_to_frames(substream->runtime, ptr) %
	       substream->runtime->buffer_size;
}

static struct page *snd_pcm_get_vmalloc_page(struct snd_pcm_substream *subs,
					     unsigned long offset)
{
	void *pageptr = subs->runtime->dma_area + offset;

	return vmalloc_to_page(pageptr);
}

static const struct snd_pcm_ops snd_cobalt_pcm_capture_ops = {
	.open		= snd_cobalt_pcm_capture_open,
	.close		= snd_cobalt_pcm_capture_close,
	.ioctl		= snd_cobalt_pcm_ioctl,
	.hw_params	= snd_cobalt_pcm_hw_params,
	.hw_free	= snd_cobalt_pcm_hw_free,
	.prepare	= snd_cobalt_pcm_prepare,
	.trigger	= snd_cobalt_pcm_trigger,
	.pointer	= snd_cobalt_pcm_pointer,
	.page		= snd_pcm_get_vmalloc_page,
};

static const struct snd_pcm_ops snd_cobalt_pcm_playback_ops = {
	.open		= snd_cobalt_pcm_playback_open,
	.close		= snd_cobalt_pcm_playback_close,
	.ioctl		= snd_cobalt_pcm_ioctl,
	.hw_params	= snd_cobalt_pcm_hw_params,
	.hw_free	= snd_cobalt_pcm_hw_free,
	.prepare	= snd_cobalt_pcm_pb_prepare,
	.trigger	= snd_cobalt_pcm_pb_trigger,
	.pointer	= snd_cobalt_pcm_pb_pointer,
	.page		= snd_pcm_get_vmalloc_page,
};

int snd_cobalt_pcm_create(struct snd_cobalt_card *cobsc)
{
	struct snd_pcm *sp;
	struct snd_card *sc = cobsc->sc;
	struct cobalt_stream *s = cobsc->s;
	struct cobalt *cobalt = s->cobalt;
	int ret;

	s->q.gfp_flags |= __GFP_ZERO;

	if (!s->is_output) {
		cobalt_s_bit_sysctrl(cobalt,
			COBALT_SYS_CTRL_AUDIO_IPP_RESETN_BIT(s->video_channel),
			0);
		mdelay(2);
		cobalt_s_bit_sysctrl(cobalt,
			COBALT_SYS_CTRL_AUDIO_IPP_RESETN_BIT(s->video_channel),
			1);
		mdelay(1);

		ret = snd_pcm_new(sc, "Cobalt PCM-In HDMI",
			0, /* PCM device 0, the only one for this card */
			0, /* 0 playback substreams */
			1, /* 1 capture substream */
			&sp);
		if (ret) {
			cobalt_err("snd_cobalt_pcm_create() failed for input with err %d\n",
				   ret);
			goto err_exit;
		}

		snd_pcm_set_ops(sp, SNDRV_PCM_STREAM_CAPTURE,
				&snd_cobalt_pcm_capture_ops);
		sp->info_flags = 0;
		sp->private_data = cobsc;
		strlcpy(sp->name, "cobalt", sizeof(sp->name));
	} else {
		cobalt_s_bit_sysctrl(cobalt,
			COBALT_SYS_CTRL_AUDIO_OPP_RESETN_BIT, 0);
		mdelay(2);
		cobalt_s_bit_sysctrl(cobalt,
			COBALT_SYS_CTRL_AUDIO_OPP_RESETN_BIT, 1);
		mdelay(1);

		ret = snd_pcm_new(sc, "Cobalt PCM-Out HDMI",
			0, /* PCM device 0, the only one for this card */
			1, /* 0 playback substreams */
			0, /* 1 capture substream */
			&sp);
		if (ret) {
			cobalt_err("snd_cobalt_pcm_create() failed for output with err %d\n",
				   ret);
			goto err_exit;
		}

		snd_pcm_set_ops(sp, SNDRV_PCM_STREAM_PLAYBACK,
				&snd_cobalt_pcm_playback_ops);
		sp->info_flags = 0;
		sp->private_data = cobsc;
		strlcpy(sp->name, "cobalt", sizeof(sp->name));
	}

	return 0;

err_exit:
	return ret;
}
