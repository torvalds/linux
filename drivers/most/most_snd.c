// SPDX-License-Identifier: GPL-2.0
/*
 * sound.c - Sound component for Mostcore
 *
 * Copyright (C) 2015 Microchip Technology Germany II GmbH & Co. KG
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/most.h>

#define DRIVER_NAME "sound"
#define STRING_SIZE	80

static struct most_component comp;

/**
 * struct channel - private structure to keep channel specific data
 * @substream: stores the substream structure
 * @pcm_hardware: low-level hardware description
 * @iface: interface for which the channel belongs to
 * @cfg: channel configuration
 * @card: registered sound card
 * @list: list for private use
 * @id: channel index
 * @period_pos: current period position (ring buffer)
 * @buffer_pos: current buffer position (ring buffer)
 * @is_stream_running: identifies whether a stream is running or not
 * @opened: set when the stream is opened
 * @playback_task: playback thread
 * @playback_waitq: waitq used by playback thread
 * @copy_fn: copy function for PCM-specific format and width
 */
struct channel {
	struct snd_pcm_substream *substream;
	struct snd_pcm_hardware pcm_hardware;
	struct most_interface *iface;
	struct most_channel_config *cfg;
	struct snd_card *card;
	struct list_head list;
	int id;
	unsigned int period_pos;
	unsigned int buffer_pos;
	bool is_stream_running;
	struct task_struct *playback_task;
	wait_queue_head_t playback_waitq;
	void (*copy_fn)(void *alsa, void *most, unsigned int bytes);
};

struct sound_adapter {
	struct list_head dev_list;
	struct most_interface *iface;
	struct snd_card *card;
	struct list_head list;
	bool registered;
	int pcm_dev_idx;
};

static struct list_head adpt_list;

#define MOST_PCM_INFO (SNDRV_PCM_INFO_MMAP | \
		       SNDRV_PCM_INFO_MMAP_VALID | \
		       SNDRV_PCM_INFO_BATCH | \
		       SNDRV_PCM_INFO_INTERLEAVED | \
		       SNDRV_PCM_INFO_BLOCK_TRANSFER)

static void swap_copy16(u16 *dest, const u16 *source, unsigned int bytes)
{
	unsigned int i = 0;

	while (i < (bytes / 2)) {
		dest[i] = swab16(source[i]);
		i++;
	}
}

static void swap_copy24(u8 *dest, const u8 *source, unsigned int bytes)
{
	unsigned int i = 0;

	if (bytes < 2)
		return;
	while (i < bytes - 2) {
		dest[i] = source[i + 2];
		dest[i + 1] = source[i + 1];
		dest[i + 2] = source[i];
		i += 3;
	}
}

static void swap_copy32(u32 *dest, const u32 *source, unsigned int bytes)
{
	unsigned int i = 0;

	while (i < bytes / 4) {
		dest[i] = swab32(source[i]);
		i++;
	}
}

static void alsa_to_most_memcpy(void *alsa, void *most, unsigned int bytes)
{
	memcpy(most, alsa, bytes);
}

static void alsa_to_most_copy16(void *alsa, void *most, unsigned int bytes)
{
	swap_copy16(most, alsa, bytes);
}

static void alsa_to_most_copy24(void *alsa, void *most, unsigned int bytes)
{
	swap_copy24(most, alsa, bytes);
}

static void alsa_to_most_copy32(void *alsa, void *most, unsigned int bytes)
{
	swap_copy32(most, alsa, bytes);
}

static void most_to_alsa_memcpy(void *alsa, void *most, unsigned int bytes)
{
	memcpy(alsa, most, bytes);
}

static void most_to_alsa_copy16(void *alsa, void *most, unsigned int bytes)
{
	swap_copy16(alsa, most, bytes);
}

static void most_to_alsa_copy24(void *alsa, void *most, unsigned int bytes)
{
	swap_copy24(alsa, most, bytes);
}

static void most_to_alsa_copy32(void *alsa, void *most, unsigned int bytes)
{
	swap_copy32(alsa, most, bytes);
}

/**
 * get_channel - get pointer to channel
 * @iface: interface structure
 * @channel_id: channel ID
 *
 * This traverses the channel list and returns the channel matching the
 * ID and interface.
 *
 * Returns pointer to channel on success or NULL otherwise.
 */
static struct channel *get_channel(struct most_interface *iface,
				   int channel_id)
{
	struct sound_adapter *adpt = iface->priv;
	struct channel *channel;

	list_for_each_entry(channel, &adpt->dev_list, list) {
		if ((channel->iface == iface) && (channel->id == channel_id))
			return channel;
	}
	return NULL;
}

/**
 * copy_data - implements data copying function
 * @channel: channel
 * @mbo: MBO from core
 *
 * Copy data from/to ring buffer to/from MBO and update the buffer position
 */
static bool copy_data(struct channel *channel, struct mbo *mbo)
{
	struct snd_pcm_runtime *const runtime = channel->substream->runtime;
	unsigned int const frame_bytes = channel->cfg->subbuffer_size;
	unsigned int const buffer_size = runtime->buffer_size;
	unsigned int frames;
	unsigned int fr0;

	if (channel->cfg->direction & MOST_CH_RX)
		frames = mbo->processed_length / frame_bytes;
	else
		frames = mbo->buffer_length / frame_bytes;
	fr0 = min(buffer_size - channel->buffer_pos, frames);

	channel->copy_fn(runtime->dma_area + channel->buffer_pos * frame_bytes,
			 mbo->virt_address,
			 fr0 * frame_bytes);

	if (frames > fr0) {
		/* wrap around at end of ring buffer */
		channel->copy_fn(runtime->dma_area,
				 mbo->virt_address + fr0 * frame_bytes,
				 (frames - fr0) * frame_bytes);
	}

	channel->buffer_pos += frames;
	if (channel->buffer_pos >= buffer_size)
		channel->buffer_pos -= buffer_size;
	channel->period_pos += frames;
	if (channel->period_pos >= runtime->period_size) {
		channel->period_pos -= runtime->period_size;
		return true;
	}
	return false;
}

/**
 * playback_thread - function implements the playback thread
 * @data: private data
 *
 * Thread which does the playback functionality in a loop. It waits for a free
 * MBO from mostcore for a particular channel and copy the data from ring buffer
 * to MBO. Submit the MBO back to mostcore, after copying the data.
 *
 * Returns 0 on success or error code otherwise.
 */
static int playback_thread(void *data)
{
	struct channel *const channel = data;

	while (!kthread_should_stop()) {
		struct mbo *mbo = NULL;
		bool period_elapsed = false;

		wait_event_interruptible(
			channel->playback_waitq,
			kthread_should_stop() ||
			(channel->is_stream_running &&
			 (mbo = most_get_mbo(channel->iface, channel->id,
					     &comp))));
		if (!mbo)
			continue;

		if (channel->is_stream_running)
			period_elapsed = copy_data(channel, mbo);
		else
			memset(mbo->virt_address, 0, mbo->buffer_length);

		most_submit_mbo(mbo);
		if (period_elapsed)
			snd_pcm_period_elapsed(channel->substream);
	}
	return 0;
}

/**
 * pcm_open - implements open callback function for PCM middle layer
 * @substream: pointer to ALSA PCM substream
 *
 * This is called when a PCM substream is opened. At least, the function should
 * initialize the runtime->hw record.
 *
 * Returns 0 on success or error code otherwise.
 */
static int pcm_open(struct snd_pcm_substream *substream)
{
	struct channel *channel = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct most_channel_config *cfg = channel->cfg;
	int ret;

	channel->substream = substream;

	if (cfg->direction == MOST_CH_TX) {
		channel->playback_task = kthread_run(playback_thread, channel,
						     "most_audio_playback");
		if (IS_ERR(channel->playback_task)) {
			pr_err("Couldn't start thread\n");
			return PTR_ERR(channel->playback_task);
		}
	}

	ret = most_start_channel(channel->iface, channel->id, &comp);
	if (ret) {
		pr_err("most_start_channel() failed!\n");
		if (cfg->direction == MOST_CH_TX)
			kthread_stop(channel->playback_task);
		return ret;
	}

	runtime->hw = channel->pcm_hardware;
	return 0;
}

/**
 * pcm_close - implements close callback function for PCM middle layer
 * @substream: sub-stream pointer
 *
 * Obviously, this is called when a PCM substream is closed. Any private
 * instance for a PCM substream allocated in the open callback will be
 * released here.
 *
 * Returns 0 on success or error code otherwise.
 */
static int pcm_close(struct snd_pcm_substream *substream)
{
	struct channel *channel = substream->private_data;

	if (channel->cfg->direction == MOST_CH_TX)
		kthread_stop(channel->playback_task);
	most_stop_channel(channel->iface, channel->id, &comp);
	return 0;
}

/**
 * pcm_prepare - implements prepare callback function for PCM middle layer
 * @substream: substream pointer
 *
 * This callback is called when the PCM is "prepared". Format rate, sample rate,
 * etc., can be set here. This callback can be called many times at each setup.
 *
 * Returns 0 on success or error code otherwise.
 */
static int pcm_prepare(struct snd_pcm_substream *substream)
{
	struct channel *channel = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct most_channel_config *cfg = channel->cfg;
	int width = snd_pcm_format_physical_width(runtime->format);

	channel->copy_fn = NULL;

	if (cfg->direction == MOST_CH_TX) {
		if (snd_pcm_format_big_endian(runtime->format) || width == 8)
			channel->copy_fn = alsa_to_most_memcpy;
		else if (width == 16)
			channel->copy_fn = alsa_to_most_copy16;
		else if (width == 24)
			channel->copy_fn = alsa_to_most_copy24;
		else if (width == 32)
			channel->copy_fn = alsa_to_most_copy32;
	} else {
		if (snd_pcm_format_big_endian(runtime->format) || width == 8)
			channel->copy_fn = most_to_alsa_memcpy;
		else if (width == 16)
			channel->copy_fn = most_to_alsa_copy16;
		else if (width == 24)
			channel->copy_fn = most_to_alsa_copy24;
		else if (width == 32)
			channel->copy_fn = most_to_alsa_copy32;
	}

	if (!channel->copy_fn)
		return -EINVAL;
	channel->period_pos = 0;
	channel->buffer_pos = 0;
	return 0;
}

/**
 * pcm_trigger - implements trigger callback function for PCM middle layer
 * @substream: substream pointer
 * @cmd: action to perform
 *
 * This is called when the PCM is started, stopped or paused. The action will be
 * specified in the second argument, SNDRV_PCM_TRIGGER_XXX
 *
 * Returns 0 on success or error code otherwise.
 */
static int pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct channel *channel = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		channel->is_stream_running = true;
		wake_up_interruptible(&channel->playback_waitq);
		return 0;

	case SNDRV_PCM_TRIGGER_STOP:
		channel->is_stream_running = false;
		return 0;

	default:
		return -EINVAL;
	}
	return 0;
}

/**
 * pcm_pointer - implements pointer callback function for PCM middle layer
 * @substream: substream pointer
 *
 * This callback is called when the PCM middle layer inquires the current
 * hardware position on the buffer. The position must be returned in frames,
 * ranging from 0 to buffer_size-1.
 */
static snd_pcm_uframes_t pcm_pointer(struct snd_pcm_substream *substream)
{
	struct channel *channel = substream->private_data;

	return channel->buffer_pos;
}

/*
 * Initialization of struct snd_pcm_ops
 */
static const struct snd_pcm_ops pcm_ops = {
	.open       = pcm_open,
	.close      = pcm_close,
	.prepare    = pcm_prepare,
	.trigger    = pcm_trigger,
	.pointer    = pcm_pointer,
};

static int split_arg_list(char *buf, u16 *ch_num, char **sample_res)
{
	char *num;
	int ret;

	num = strsep(&buf, "x");
	if (!num)
		goto err;
	ret = kstrtou16(num, 0, ch_num);
	if (ret)
		goto err;
	*sample_res = strsep(&buf, ".\n");
	if (!*sample_res)
		goto err;
	return 0;

err:
	pr_err("Bad PCM format\n");
	return -EINVAL;
}

static const struct sample_resolution_info {
	const char *sample_res;
	int bytes;
	u64 formats;
} sinfo[] = {
	{ "8", 1, SNDRV_PCM_FMTBIT_S8 },
	{ "16", 2, SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE },
	{ "24", 3, SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_3BE },
	{ "32", 4, SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE },
};

static int audio_set_hw_params(struct snd_pcm_hardware *pcm_hw,
			       u16 ch_num, char *sample_res,
			       struct most_channel_config *cfg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sinfo); i++) {
		if (!strcmp(sample_res, sinfo[i].sample_res))
			goto found;
	}
	pr_err("Unsupported PCM format\n");
	return -EINVAL;

found:
	if (!ch_num) {
		pr_err("Bad number of channels\n");
		return -EINVAL;
	}

	if (cfg->subbuffer_size != ch_num * sinfo[i].bytes) {
		pr_err("Audio resolution doesn't fit subbuffer size\n");
		return -EINVAL;
	}

	pcm_hw->info = MOST_PCM_INFO;
	pcm_hw->rates = SNDRV_PCM_RATE_48000;
	pcm_hw->rate_min = 48000;
	pcm_hw->rate_max = 48000;
	pcm_hw->buffer_bytes_max = cfg->num_buffers * cfg->buffer_size;
	pcm_hw->period_bytes_min = cfg->buffer_size;
	pcm_hw->period_bytes_max = cfg->buffer_size;
	pcm_hw->periods_min = 1;
	pcm_hw->periods_max = cfg->num_buffers;
	pcm_hw->channels_min = ch_num;
	pcm_hw->channels_max = ch_num;
	pcm_hw->formats = sinfo[i].formats;
	return 0;
}

static void release_adapter(struct sound_adapter *adpt)
{
	struct channel *channel, *tmp;

	list_for_each_entry_safe(channel, tmp, &adpt->dev_list, list) {
		list_del(&channel->list);
		kfree(channel);
	}
	if (adpt->card)
		snd_card_free(adpt->card);
	list_del(&adpt->list);
	kfree(adpt);
}

/**
 * audio_probe_channel - probe function of the driver module
 * @iface: pointer to interface instance
 * @channel_id: channel index/ID
 * @cfg: pointer to actual channel configuration
 * @device_name: name of the device to be created in /dev
 * @arg_list: string that provides the desired audio resolution
 *
 * Creates sound card, pcm device, sets pcm ops and registers sound card.
 *
 * Returns 0 on success or error code otherwise.
 */
static int audio_probe_channel(struct most_interface *iface, int channel_id,
			       struct most_channel_config *cfg,
			       char *device_name, char *arg_list)
{
	struct channel *channel;
	struct sound_adapter *adpt;
	struct snd_pcm *pcm;
	int playback_count = 0;
	int capture_count = 0;
	int ret;
	int direction;
	u16 ch_num;
	char *sample_res;
	char arg_list_cpy[STRING_SIZE];

	if (cfg->data_type != MOST_CH_SYNC) {
		pr_err("Incompatible channel type\n");
		return -EINVAL;
	}
	strscpy(arg_list_cpy, arg_list, STRING_SIZE);
	ret = split_arg_list(arg_list_cpy, &ch_num, &sample_res);
	if (ret < 0)
		return ret;

	list_for_each_entry(adpt, &adpt_list, list) {
		if (adpt->iface != iface)
			continue;
		if (adpt->registered)
			return -ENOSPC;
		adpt->pcm_dev_idx++;
		goto skip_adpt_alloc;
	}
	adpt = kzalloc(sizeof(*adpt), GFP_KERNEL);
	if (!adpt)
		return -ENOMEM;

	adpt->iface = iface;
	INIT_LIST_HEAD(&adpt->dev_list);
	iface->priv = adpt;
	list_add_tail(&adpt->list, &adpt_list);
	ret = snd_card_new(iface->driver_dev, -1, "INIC", THIS_MODULE,
			   sizeof(*channel), &adpt->card);
	if (ret < 0)
		goto err_free_adpt;
	snprintf(adpt->card->driver, sizeof(adpt->card->driver),
		 "%s", DRIVER_NAME);
	snprintf(adpt->card->shortname, sizeof(adpt->card->shortname),
		 "Microchip INIC");
	snprintf(adpt->card->longname, sizeof(adpt->card->longname),
		 "%s at %s", adpt->card->shortname, iface->description);
skip_adpt_alloc:
	if (get_channel(iface, channel_id)) {
		pr_err("channel (%s:%d) is already linked\n",
		       iface->description, channel_id);
		return -EEXIST;
	}

	if (cfg->direction == MOST_CH_TX) {
		playback_count = 1;
		direction = SNDRV_PCM_STREAM_PLAYBACK;
	} else {
		capture_count = 1;
		direction = SNDRV_PCM_STREAM_CAPTURE;
	}
	channel = kzalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel) {
		ret = -ENOMEM;
		goto err_free_adpt;
	}
	channel->card = adpt->card;
	channel->cfg = cfg;
	channel->iface = iface;
	channel->id = channel_id;
	init_waitqueue_head(&channel->playback_waitq);
	list_add_tail(&channel->list, &adpt->dev_list);

	ret = audio_set_hw_params(&channel->pcm_hardware, ch_num, sample_res,
				  cfg);
	if (ret)
		goto err_free_adpt;

	ret = snd_pcm_new(adpt->card, device_name, adpt->pcm_dev_idx,
			  playback_count, capture_count, &pcm);

	if (ret < 0)
		goto err_free_adpt;

	pcm->private_data = channel;
	strscpy(pcm->name, device_name, sizeof(pcm->name));
	snd_pcm_set_ops(pcm, direction, &pcm_ops);
	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_VMALLOC, NULL, 0, 0);
	return 0;

err_free_adpt:
	release_adapter(adpt);
	return ret;
}

static int audio_create_sound_card(void)
{
	int ret;
	struct sound_adapter *adpt;

	list_for_each_entry(adpt, &adpt_list, list) {
		if (!adpt->registered)
			goto adpt_alloc;
	}
	return -ENODEV;
adpt_alloc:
	ret = snd_card_register(adpt->card);
	if (ret < 0) {
		release_adapter(adpt);
		return ret;
	}
	adpt->registered = true;
	return 0;
}

/**
 * audio_disconnect_channel - function to disconnect a channel
 * @iface: pointer to interface instance
 * @channel_id: channel index
 *
 * This frees allocated memory and removes the sound card from ALSA
 *
 * Returns 0 on success or error code otherwise.
 */
static int audio_disconnect_channel(struct most_interface *iface,
				    int channel_id)
{
	struct channel *channel;
	struct sound_adapter *adpt = iface->priv;

	channel = get_channel(iface, channel_id);
	if (!channel)
		return -EINVAL;

	list_del(&channel->list);

	kfree(channel);
	if (list_empty(&adpt->dev_list))
		release_adapter(adpt);
	return 0;
}

/**
 * audio_rx_completion - completion handler for rx channels
 * @mbo: pointer to buffer object that has completed
 *
 * This searches for the channel this MBO belongs to and copy the data from MBO
 * to ring buffer
 *
 * Returns 0 on success or error code otherwise.
 */
static int audio_rx_completion(struct mbo *mbo)
{
	struct channel *channel = get_channel(mbo->ifp, mbo->hdm_channel_id);
	bool period_elapsed = false;

	if (!channel)
		return -EINVAL;
	if (channel->is_stream_running)
		period_elapsed = copy_data(channel, mbo);
	most_put_mbo(mbo);
	if (period_elapsed)
		snd_pcm_period_elapsed(channel->substream);
	return 0;
}

/**
 * audio_tx_completion - completion handler for tx channels
 * @iface: pointer to interface instance
 * @channel_id: channel index/ID
 *
 * This searches the channel that belongs to this combination of interface
 * pointer and channel ID and wakes a process sitting in the wait queue of
 * this channel.
 *
 * Returns 0 on success or error code otherwise.
 */
static int audio_tx_completion(struct most_interface *iface, int channel_id)
{
	struct channel *channel = get_channel(iface, channel_id);

	if (!channel)
		return -EINVAL;

	wake_up_interruptible(&channel->playback_waitq);
	return 0;
}

/*
 * Initialization of the struct most_component
 */
static struct most_component comp = {
	.mod = THIS_MODULE,
	.name = DRIVER_NAME,
	.probe_channel = audio_probe_channel,
	.disconnect_channel = audio_disconnect_channel,
	.rx_completion = audio_rx_completion,
	.tx_completion = audio_tx_completion,
	.cfg_complete = audio_create_sound_card,
};

static int __init audio_init(void)
{
	int ret;

	INIT_LIST_HEAD(&adpt_list);

	ret = most_register_component(&comp);
	if (ret) {
		pr_err("Failed to register %s\n", comp.name);
		return ret;
	}
	ret = most_register_configfs_subsys(&comp);
	if (ret) {
		pr_err("Failed to register %s configfs subsys\n", comp.name);
		most_deregister_component(&comp);
	}
	return ret;
}

static void __exit audio_exit(void)
{
	most_deregister_configfs_subsys(&comp);
	most_deregister_component(&comp);
}

module_init(audio_init);
module_exit(audio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Gromm <christian.gromm@microchip.com>");
MODULE_DESCRIPTION("Sound Component Module for Mostcore");
