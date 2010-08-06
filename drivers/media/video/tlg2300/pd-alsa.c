#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/init.h>
#include <linux/sound.h>
#include <linux/spinlock.h>
#include <linux/soundcard.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/control.h>
#include <media/v4l2-common.h>
#include "pd-common.h"
#include "vendorcmds.h"

static void complete_handler_audio(struct urb *urb);
#define AUDIO_EP	(0x83)
#define AUDIO_BUF_SIZE	(512)
#define PERIOD_SIZE	(1024 * 8)
#define PERIOD_MIN	(4)
#define PERIOD_MAX 	PERIOD_MIN

static struct snd_pcm_hardware snd_pd_hw_capture = {
	.info = SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP           |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP_VALID,

	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_48000,

	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = PERIOD_SIZE * PERIOD_MIN,
	.period_bytes_min = PERIOD_SIZE,
	.period_bytes_max = PERIOD_SIZE,
	.periods_min = PERIOD_MIN,
	.periods_max = PERIOD_MAX,
	/*
	.buffer_bytes_max = 62720 * 8,
	.period_bytes_min = 64,
	.period_bytes_max = 12544,
	.periods_min = 2,
	.periods_max = 98
	*/
};

static int snd_pd_capture_open(struct snd_pcm_substream *substream)
{
	struct poseidon *p = snd_pcm_substream_chip(substream);
	struct poseidon_audio *pa = &p->audio;
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (!p)
		return -ENODEV;
	pa->users++;
	pa->card_close 		= 0;
	pa->capture_pcm_substream	= substream;
	runtime->private_data		= p;

	runtime->hw = snd_pd_hw_capture;
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	usb_autopm_get_interface(p->interface);
	kref_get(&p->kref);
	return 0;
}

static int snd_pd_pcm_close(struct snd_pcm_substream *substream)
{
	struct poseidon *p = snd_pcm_substream_chip(substream);
	struct poseidon_audio *pa = &p->audio;

	pa->users--;
	pa->card_close 		= 1;
	usb_autopm_put_interface(p->interface);
	kref_put(&p->kref, poseidon_delete);
	return 0;
}

static int snd_pd_hw_capture_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int size;

	size = params_buffer_bytes(hw_params);
	if (runtime->dma_area) {
		if (runtime->dma_bytes > size)
			return 0;
		vfree(runtime->dma_area);
	}
	runtime->dma_area = vmalloc(size);
	if (!runtime->dma_area)
		return -ENOMEM;
	else
		runtime->dma_bytes = size;
	return 0;
}

static int audio_buf_free(struct poseidon *p)
{
	struct poseidon_audio *pa = &p->audio;
	int i;

	for (i = 0; i < AUDIO_BUFS; i++)
		if (pa->urb_array[i])
			usb_kill_urb(pa->urb_array[i]);
	free_all_urb_generic(pa->urb_array, AUDIO_BUFS);
	logpm();
	return 0;
}

static int snd_pd_hw_capture_free(struct snd_pcm_substream *substream)
{
	struct poseidon *p = snd_pcm_substream_chip(substream);

	logpm();
	audio_buf_free(p);
	return 0;
}

static int snd_pd_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}

#define AUDIO_TRAILER_SIZE	(16)
static inline void handle_audio_data(struct urb *urb, int *period_elapsed)
{
	struct poseidon_audio *pa = urb->context;
	struct snd_pcm_runtime *runtime = pa->capture_pcm_substream->runtime;

	int stride	= runtime->frame_bits >> 3;
	int len		= urb->actual_length / stride;
	unsigned char *cp	= urb->transfer_buffer;
	unsigned int oldptr	= pa->rcv_position;

	if (urb->actual_length == AUDIO_BUF_SIZE - 4)
		len -= (AUDIO_TRAILER_SIZE / stride);

	/* do the copy */
	if (oldptr + len >= runtime->buffer_size) {
		unsigned int cnt = runtime->buffer_size - oldptr;

		memcpy(runtime->dma_area + oldptr * stride, cp, cnt * stride);
		memcpy(runtime->dma_area, (cp + cnt * stride),
					(len * stride - cnt * stride));
	} else
		memcpy(runtime->dma_area + oldptr * stride, cp, len * stride);

	/* update the statas */
	snd_pcm_stream_lock(pa->capture_pcm_substream);
	pa->rcv_position	+= len;
	if (pa->rcv_position >= runtime->buffer_size)
		pa->rcv_position -= runtime->buffer_size;

	pa->copied_position += (len);
	if (pa->copied_position >= runtime->period_size) {
		pa->copied_position -= runtime->period_size;
		*period_elapsed = 1;
	}
	snd_pcm_stream_unlock(pa->capture_pcm_substream);
}

static void complete_handler_audio(struct urb *urb)
{
	struct poseidon_audio *pa = urb->context;
	struct snd_pcm_substream *substream = pa->capture_pcm_substream;
	int    period_elapsed = 0;
	int    ret;

	if (1 == pa->card_close || pa->capture_stream != STREAM_ON)
		return;

	if (urb->status != 0) {
		/*if (urb->status == -ESHUTDOWN)*/
			return;
	}

	if (substream) {
		if (urb->actual_length) {
			handle_audio_data(urb, &period_elapsed);
			if (period_elapsed)
				snd_pcm_period_elapsed(substream);
		}
	}

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret < 0)
		log("audio urb failed (errcod = %i)", ret);
	return;
}

static int fire_audio_urb(struct poseidon *p)
{
	int i, ret = 0;
	struct poseidon_audio *pa = &p->audio;

	alloc_bulk_urbs_generic(pa->urb_array, AUDIO_BUFS,
			p->udev, AUDIO_EP,
			AUDIO_BUF_SIZE, GFP_ATOMIC,
			complete_handler_audio, pa);

	for (i = 0; i < AUDIO_BUFS; i++) {
		ret = usb_submit_urb(pa->urb_array[i], GFP_KERNEL);
		if (ret)
			log("urb err : %d", ret);
	}
	log();
	return ret;
}

static int snd_pd_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct poseidon *p = snd_pcm_substream_chip(substream);
	struct poseidon_audio *pa = &p->audio;

	if (debug_mode)
		log("cmd %d, audio stat : %d\n", cmd, pa->capture_stream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_START:
		if (pa->capture_stream == STREAM_ON)
			return 0;

		pa->rcv_position = pa->copied_position = 0;
		pa->capture_stream = STREAM_ON;

		if (in_hibernation(p))
			return 0;
		fire_audio_urb(p);
		return 0;

	case SNDRV_PCM_TRIGGER_SUSPEND:
		pa->capture_stream = STREAM_SUSPEND;
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
		pa->capture_stream = STREAM_OFF;
		return 0;
	default:
		return -EINVAL;
	}
}

static snd_pcm_uframes_t
snd_pd_capture_pointer(struct snd_pcm_substream *substream)
{
	struct poseidon *p = snd_pcm_substream_chip(substream);
	struct poseidon_audio *pa = &p->audio;
	return pa->rcv_position;
}

static struct page *snd_pcm_pd_get_page(struct snd_pcm_substream *subs,
					     unsigned long offset)
{
	void *pageptr = subs->runtime->dma_area + offset;
	return vmalloc_to_page(pageptr);
}

static struct snd_pcm_ops pcm_capture_ops = {
	.open      = snd_pd_capture_open,
	.close     = snd_pd_pcm_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = snd_pd_hw_capture_params,
	.hw_free   = snd_pd_hw_capture_free,
	.prepare   = snd_pd_prepare,
	.trigger   = snd_pd_capture_trigger,
	.pointer   = snd_pd_capture_pointer,
	.page      = snd_pcm_pd_get_page,
};

#ifdef CONFIG_PM
int pm_alsa_suspend(struct poseidon *p)
{
	logpm(p);
	audio_buf_free(p);
	return 0;
}

int pm_alsa_resume(struct poseidon *p)
{
	logpm(p);
	fire_audio_urb(p);
	return 0;
}
#endif

int poseidon_audio_init(struct poseidon *p)
{
	struct poseidon_audio *pa = &p->audio;
	struct snd_card *card;
	struct snd_pcm *pcm;
	int ret;

	ret = snd_card_create(-1, "Telegent", THIS_MODULE, 0, &card);
	if (ret != 0)
		return ret;

	ret = snd_pcm_new(card, "poseidon audio", 0, 0, 1, &pcm);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &pcm_capture_ops);
	pcm->info_flags   = 0;
	pcm->private_data = p;
	strcpy(pcm->name, "poseidon audio capture");

	strcpy(card->driver, "ALSA driver");
	strcpy(card->shortname, "poseidon Audio");
	strcpy(card->longname, "poseidon ALSA Audio");

	if (snd_card_register(card)) {
		snd_card_free(card);
		return -ENOMEM;
	}
	pa->card = card;
	return 0;
}

int poseidon_audio_free(struct poseidon *p)
{
	struct poseidon_audio *pa = &p->audio;

	if (pa->card)
		snd_card_free(pa->card);
	return 0;
}
