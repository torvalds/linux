/*
 * sound.c - Audio Application Interface Module for Mostcore
 *
 * Copyright (C) 2015 Microchip Technology Germany II GmbH & Co. KG
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This file is licensed under GPLv2.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <mostcore.h>

#define DRIVER_NAME "sound"

static struct list_head dev_list;
static struct most_aim audio_aim;

/**
 * struct channel - private structure to keep channel specific data
 * @substream: stores the substream structure
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

#define MOST_PCM_INFO (SNDRV_PCM_INFO_MMAP | \
		       SNDRV_PCM_INFO_MMAP_VALID | \
		       SNDRV_PCM_INFO_BATCH | \
		       SNDRV_PCM_INFO_INTERLEAVED | \
		       SNDRV_PCM_INFO_BLOCK_TRANSFER)

#define swap16(val) ( \
	(((u16)(val) << 8) & (u16)0xFF00) | \
	(((u16)(val) >> 8) & (u16)0x00FF))

#define swap32(val) ( \
	(((u32)(val) << 24) & (u32)0xFF000000) | \
	(((u32)(val) <<  8) & (u32)0x00FF0000) | \
	(((u32)(val) >>  8) & (u32)0x0000FF00) | \
	(((u32)(val) >> 24) & (u32)0x000000FF))

static void swap_copy16(u16 *dest, const u16 *source, unsigned int bytes)
{
	unsigned int i = 0;

	while (i < (bytes / 2)) {
		dest[i] = swap16(source[i]);
		i++;
	}
}

static void swap_copy24(u8 *dest, const u8 *source, unsigned int bytes)
{
	unsigned int i = 0;

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
		dest[i] = swap32(source[i]);
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
	struct channel *channel, *tmp;

	list_for_each_entry_safe(channel, tmp, &dev_list, list) {
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
					     &audio_aim))));
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

	channel->substream = substream;

	if (cfg->direction == MOST_CH_TX) {
		channel->playback_task = kthread_run(playback_thread, channel,
						     "most_audio_playback");
		if (IS_ERR(channel->playback_task)) {
			pr_err("Couldn't start thread\n");
			return PTR_ERR(channel->playback_task);
		}
	}

	if (most_start_channel(channel->iface, channel->id, &audio_aim)) {
		pr_err("most_start_channel() failed!\n");
		if (cfg->direction == MOST_CH_TX)
			kthread_stop(channel->playback_task);
		return -EBUSY;
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
	most_stop_channel(channel->iface, channel->id, &audio_aim);

	return 0;
}

/**
 * pcm_hw_params - implements hw_params callback function for PCM middle layer
 * @substream: sub-stream pointer
 * @hw_params: contains the hardware parameters set by the application
 *
 * This is called when the hardware parameters is set by the application, that
 * is, once when the buffer size, the period size, the format, etc. are defined
 * for the PCM substream. Many hardware setups should be done is this callback,
 * including the allocation of buffers.
 *
 * Returns 0 on success or error code otherwise.
 */
static int pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *hw_params)
{
	struct channel *channel = substream->private_data;

	if ((params_channels(hw_params) > channel->pcm_hardware.channels_max) ||
	    (params_channels(hw_params) < channel->pcm_hardware.channels_min)) {
		pr_err("Requested number of channels not supported.\n");
		return -EINVAL;
	}
	return snd_pcm_lib_alloc_vmalloc_buffer(substream,
						params_buffer_bytes(hw_params));
}

/**
 * pcm_hw_free - implements hw_free callback function for PCM middle layer
 * @substream: substream pointer
 *
 * This is called to release the resources allocated via hw_params.
 * This function will be always called before the close callback is called.
 *
 * Returns 0 on success or error code otherwise.
 */
static int pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_vmalloc_buffer(substream);
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

	if (!channel->copy_fn) {
		pr_err("unsupported format\n");
		return -EINVAL;
	}

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
		pr_info("%s(), invalid\n", __func__);
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

/**
 * Initialization of struct snd_pcm_ops
 */
static const struct snd_pcm_ops pcm_ops = {
	.open       = pcm_open,
	.close      = pcm_close,
	.ioctl      = snd_pcm_lib_ioctl,
	.hw_params  = pcm_hw_params,
	.hw_free    = pcm_hw_free,
	.prepare    = pcm_prepare,
	.trigger    = pcm_trigger,
	.pointer    = pcm_pointer,
	.page       = snd_pcm_lib_get_vmalloc_page,
	.mmap       = snd_pcm_lib_mmap_vmalloc,
};

static int split_arg_list(char *buf, char **card_name, char **pcm_format)
{
	*card_name = strsep(&buf, ".");
	if (!*card_name)
		return -EIO;
	*pcm_format = strsep(&buf, ".\n");
	if (!*pcm_format)
		return -EIO;
	return 0;
}

static int audio_set_hw_params(struct snd_pcm_hardware *pcm_hw,
			       char *pcm_format,
			       struct most_channel_config *cfg)
{
	pcm_hw->info = MOST_PCM_INFO;
	pcm_hw->rates = SNDRV_PCM_RATE_48000;
	pcm_hw->rate_min = 48000;
	pcm_hw->rate_max = 48000;
	pcm_hw->buffer_bytes_max = cfg->num_buffers * cfg->buffer_size;
	pcm_hw->period_bytes_min = cfg->buffer_size;
	pcm_hw->period_bytes_max = cfg->buffer_size;
	pcm_hw->periods_min = 1;
	pcm_hw->periods_max = cfg->num_buffers;

	if (!strcmp(pcm_format, "1x8")) {
		if (cfg->subbuffer_size != 1)
			goto error;
		pr_info("PCM format is 8-bit mono\n");
		pcm_hw->channels_min = 1;
		pcm_hw->channels_max = 1;
		pcm_hw->formats = SNDRV_PCM_FMTBIT_S8;
	} else if (!strcmp(pcm_format, "2x16")) {
		if (cfg->subbuffer_size != 4)
			goto error;
		pr_info("PCM format is 16-bit stereo\n");
		pcm_hw->channels_min = 2;
		pcm_hw->channels_max = 2;
		pcm_hw->formats = SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S16_BE;
	} else if (!strcmp(pcm_format, "2x24")) {
		if (cfg->subbuffer_size != 6)
			goto error;
		pr_info("PCM format is 24-bit stereo\n");
		pcm_hw->channels_min = 2;
		pcm_hw->channels_max = 2;
		pcm_hw->formats = SNDRV_PCM_FMTBIT_S24_3LE |
				  SNDRV_PCM_FMTBIT_S24_3BE;
	} else if (!strcmp(pcm_format, "2x32")) {
		if (cfg->subbuffer_size != 8)
			goto error;
		pr_info("PCM format is 32-bit stereo\n");
		pcm_hw->channels_min = 2;
		pcm_hw->channels_max = 2;
		pcm_hw->formats = SNDRV_PCM_FMTBIT_S32_LE |
				  SNDRV_PCM_FMTBIT_S32_BE;
	} else if (!strcmp(pcm_format, "6x16")) {
		if (cfg->subbuffer_size != 12)
			goto error;
		pr_info("PCM format is 16-bit 5.1 multi channel\n");
		pcm_hw->channels_min = 6;
		pcm_hw->channels_max = 6;
		pcm_hw->formats = SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S16_BE;
	} else {
		pr_err("PCM format %s not supported\n", pcm_format);
		return -EIO;
	}
	return 0;
error:
	pr_err("Audio resolution doesn't fit subbuffer size\n");
	return -EINVAL;
}

/**
 * audio_probe_channel - probe function of the driver module
 * @iface: pointer to interface instance
 * @channel_id: channel index/ID
 * @cfg: pointer to actual channel configuration
 * @parent: pointer to kobject (needed for sysfs hook-up)
 * @arg_list: string that provides the name of the device to be created in /dev
 *	      plus the desired audio resolution
 *
 * Creates sound card, pcm device, sets pcm ops and registers sound card.
 *
 * Returns 0 on success or error code otherwise.
 */
static int audio_probe_channel(struct most_interface *iface, int channel_id,
			       struct most_channel_config *cfg,
			       struct kobject *parent, char *arg_list)
{
	struct channel *channel;
	struct snd_card *card;
	struct snd_pcm *pcm;
	int playback_count = 0;
	int capture_count = 0;
	int ret;
	int direction;
	char *card_name;
	char *pcm_format;

	if (!iface)
		return -EINVAL;

	if (cfg->data_type != MOST_CH_SYNC) {
		pr_err("Incompatible channel type\n");
		return -EINVAL;
	}

	if (get_channel(iface, channel_id)) {
		pr_err("channel (%s:%d) is already linked\n",
		       iface->description, channel_id);
		return -EINVAL;
	}

	if (cfg->direction == MOST_CH_TX) {
		playback_count = 1;
		direction = SNDRV_PCM_STREAM_PLAYBACK;
	} else {
		capture_count = 1;
		direction = SNDRV_PCM_STREAM_CAPTURE;
	}

	ret = split_arg_list(arg_list, &card_name, &pcm_format);
	if (ret < 0) {
		pr_info("PCM format missing\n");
		return ret;
	}

	ret = snd_card_new(NULL, -1, card_name, THIS_MODULE,
			   sizeof(*channel), &card);
	if (ret < 0)
		return ret;

	channel = card->private_data;
	channel->card = card;
	channel->cfg = cfg;
	channel->iface = iface;
	channel->id = channel_id;
	init_waitqueue_head(&channel->playback_waitq);

	ret = audio_set_hw_params(&channel->pcm_hardware, pcm_format, cfg);
	if (ret)
		goto err_free_card;

	snprintf(card->driver, sizeof(card->driver), "%s", DRIVER_NAME);
	snprintf(card->shortname, sizeof(card->shortname), "Microchip MOST:%d",
		 card->number);
	snprintf(card->longname, sizeof(card->longname), "%s at %s, ch %d",
		 card->shortname, iface->description, channel_id);

	ret = snd_pcm_new(card, card_name, 0, playback_count,
			  capture_count, &pcm);
	if (ret < 0)
		goto err_free_card;

	pcm->private_data = channel;

	snd_pcm_set_ops(pcm, direction, &pcm_ops);

	ret = snd_card_register(card);
	if (ret < 0)
		goto err_free_card;

	list_add_tail(&channel->list, &dev_list);

	return 0;

err_free_card:
	snd_card_free(card);
	return ret;
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

	channel = get_channel(iface, channel_id);
	if (!channel) {
		pr_err("sound_disconnect_channel(), invalid channel %d\n",
		       channel_id);
		return -EINVAL;
	}

	list_del(&channel->list);
	snd_card_free(channel->card);

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

	if (!channel) {
		pr_err("sound_rx_completion(), invalid channel %d\n",
		       mbo->hdm_channel_id);
		return -EINVAL;
	}

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

	if (!channel) {
		pr_err("sound_tx_completion(), invalid channel %d\n",
		       channel_id);
		return -EINVAL;
	}

	wake_up_interruptible(&channel->playback_waitq);

	return 0;
}

/**
 * Initialization of the struct most_aim
 */
static struct most_aim audio_aim = {
	.name = DRIVER_NAME,
	.probe_channel = audio_probe_channel,
	.disconnect_channel = audio_disconnect_channel,
	.rx_completion = audio_rx_completion,
	.tx_completion = audio_tx_completion,
};

static int __init audio_init(void)
{
	pr_info("init()\n");

	INIT_LIST_HEAD(&dev_list);

	return most_register_aim(&audio_aim);
}

static void __exit audio_exit(void)
{
	struct channel *channel, *tmp;

	pr_info("exit()\n");

	list_for_each_entry_safe(channel, tmp, &dev_list, list) {
		list_del(&channel->list);
		snd_card_free(channel->card);
	}

	most_deregister_aim(&audio_aim);
}

module_init(audio_init);
module_exit(audio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Gromm <christian.gromm@microchip.com>");
MODULE_DESCRIPTION("Audio Application Interface Module for MostCore");
