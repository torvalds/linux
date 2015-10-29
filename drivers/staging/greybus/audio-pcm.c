/*
 * Greybus audio Pulse Code Modulation (PCM) driver
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include <sound/simple_card.h>

#include "greybus.h"
#include "audio.h"

/*
 * timer/workqueue logic for pushing pcm data.
 *
 * Since when we are playing audio, we don't get any
 * status or feedback from the codec, we have to use a
 * hrtimer to trigger sending data to the remote codec.
 * However since the hrtimer runs in irq context, so we
 * have to schedule a workqueue to actually send the
 * greybus data.
 */

static void gb_pcm_work(struct work_struct *work)
{
	struct gb_snd *snd_dev = container_of(work, struct gb_snd, work);
	struct snd_pcm_substream *substream = snd_dev->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int stride, frames, oldptr;
	int period_elapsed, ret;
	char *address;
	long len;

	if (!snd_dev)
		return;

	if (!atomic_read(&snd_dev->running)) {
		if (snd_dev->cport_active) {
			ret = gb_i2s_mgmt_deactivate_cport(
				snd_dev->mgmt_connection,
				snd_dev->i2s_tx_connection->intf_cport_id);
			if (ret) /* XXX Do what else with failure? */
				pr_err("deactivate_cport failed: %d\n", ret);

			snd_dev->cport_active = false;
			snd_dev->send_data_sample_count = 0;
		}

		return;
	} else if (!snd_dev->cport_active) {
		ret = gb_i2s_mgmt_activate_cport(snd_dev->mgmt_connection,
				snd_dev->i2s_tx_connection->intf_cport_id);
		if (ret)
			pr_err("activate_cport failed: %d\n", ret);

		snd_dev->cport_active = true;
	}

	address = runtime->dma_area + snd_dev->hwptr_done;

	len = frames_to_bytes(runtime,
			      runtime->buffer_size) - snd_dev->hwptr_done;
	len = min(len, MAX_SEND_DATA_LEN);
	gb_i2s_send_data(snd_dev->i2s_tx_connection, snd_dev->send_data_req_buf,
				address, len, snd_dev->send_data_sample_count);

	snd_dev->send_data_sample_count += CONFIG_SAMPLES_PER_MSG;

	stride = runtime->frame_bits >> 3;
	frames = len/stride;

	snd_pcm_stream_lock(substream);
	oldptr = snd_dev->hwptr_done;
	snd_dev->hwptr_done += len;
	if (snd_dev->hwptr_done >= runtime->buffer_size * stride)
		snd_dev->hwptr_done -= runtime->buffer_size * stride;

	frames = (len + (oldptr % stride)) / stride;

	period_elapsed = 0;
	snd_dev->transfer_done += frames;
	if (snd_dev->transfer_done >= runtime->period_size) {
		snd_dev->transfer_done -= runtime->period_size;
		period_elapsed = 1;
	}

	snd_pcm_stream_unlock(substream);
	if (period_elapsed)
		snd_pcm_period_elapsed(snd_dev->substream);
}

static enum hrtimer_restart gb_pcm_timer_function(struct hrtimer *hrtimer)
{
	struct gb_snd *snd_dev = container_of(hrtimer, struct gb_snd, timer);

	if (!atomic_read(&snd_dev->running))
		return HRTIMER_NORESTART;
	queue_work(snd_dev->workqueue, &snd_dev->work);
	hrtimer_forward_now(hrtimer, ns_to_ktime(CONFIG_PERIOD_NS));
	return HRTIMER_RESTART;
}

void gb_pcm_hrtimer_start(struct gb_snd *snd_dev)
{
	atomic_set(&snd_dev->running, 1);
	queue_work(snd_dev->workqueue, &snd_dev->work); /* Activates CPort */
	hrtimer_start(&snd_dev->timer, ns_to_ktime(CONFIG_PERIOD_NS),
						HRTIMER_MODE_REL);
}

void gb_pcm_hrtimer_stop(struct gb_snd *snd_dev)
{
	atomic_set(&snd_dev->running, 0);
	hrtimer_cancel(&snd_dev->timer);
	queue_work(snd_dev->workqueue, &snd_dev->work); /* Deactivates CPort */
}

static int gb_pcm_hrtimer_init(struct gb_snd *snd_dev)
{
	hrtimer_init(&snd_dev->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	snd_dev->timer.function = gb_pcm_timer_function;
	atomic_set(&snd_dev->running, 0);
	snd_dev->workqueue = alloc_workqueue("gb-audio", WQ_HIGHPRI, 0);
	if (!snd_dev->workqueue)
		return -ENOMEM;
	INIT_WORK(&snd_dev->work, gb_pcm_work);
	return 0;
}


/*
 * Core gb pcm structure
 */
static struct snd_pcm_hardware gb_plat_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_MMAP        |
				  SNDRV_PCM_INFO_MMAP_VALID,
	.formats		= GB_FMTS,
	.rates			= GB_RATES,
	.rate_min		= 8000,
	.rate_max		= GB_SAMPLE_RATE,
	.channels_min		= 1,
	.channels_max		= 2,
	/* XXX - All the values below are junk */
	.buffer_bytes_max	= 64 * 1024,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192,
	.periods_min		= 2,
	.periods_max		= 32,
};

static snd_pcm_uframes_t gb_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct gb_snd *snd_dev;

	snd_dev = snd_soc_dai_get_drvdata(rtd->cpu_dai);

	return snd_dev->hwptr_done  / (substream->runtime->frame_bits >> 3);
}

static int gb_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct gb_snd *snd_dev;

	snd_dev = snd_soc_dai_get_drvdata(rtd->cpu_dai);
	snd_dev->hwptr_done = 0;
	snd_dev->transfer_done = 0;
	return 0;
}

static unsigned int rates[] = {GB_SAMPLE_RATE};
static struct snd_pcm_hw_constraint_list constraints_rates = {
	.count = ARRAY_SIZE(rates),
	.list = rates,
	.mask = 0,
};

static int gb_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct gb_snd *snd_dev;
	unsigned long flags;
	int ret;

	snd_dev = snd_soc_dai_get_drvdata(rtd->cpu_dai);

	spin_lock_irqsave(&snd_dev->lock, flags);
	runtime->private_data = snd_dev;
	snd_dev->substream = substream;
	ret = gb_pcm_hrtimer_init(snd_dev);
	spin_unlock_irqrestore(&snd_dev->lock, flags);

	if (ret)
		return ret;

	snd_soc_set_runtime_hwparams(substream, &gb_plat_pcm_hardware);

	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
					SNDRV_PCM_HW_PARAM_RATE,
					&constraints_rates);
	if (ret < 0)
		return ret;

	return snd_pcm_hw_constraint_integer(runtime,
					     SNDRV_PCM_HW_PARAM_PERIODS);
}

static int gb_pcm_close(struct snd_pcm_substream *substream)
{
	substream->runtime->private_data = NULL;
	return 0;
}

static int gb_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *hw_params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct gb_snd *snd_dev;
	int rate, chans, bytes_per_chan, is_le, ret;

	snd_dev = snd_soc_dai_get_drvdata(rtd->cpu_dai);

	rate = params_rate(hw_params);
	chans = params_channels(hw_params);
	bytes_per_chan = snd_pcm_format_width(params_format(hw_params)) / 8;
	is_le = snd_pcm_format_little_endian(params_format(hw_params));

	ret = gb_i2s_mgmt_set_cfg(snd_dev, rate, chans, bytes_per_chan, is_le);
	if (ret)
		return ret;

	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

static int gb_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static struct snd_pcm_ops gb_pcm_ops = {
	.open		= gb_pcm_open,
	.close		= gb_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= gb_pcm_hw_params,
	.hw_free	= gb_pcm_hw_free,
	.prepare	= gb_pcm_prepare,
	.pointer	= gb_pcm_pointer,
};

static void gb_pcm_free(struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int gb_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm *pcm = rtd->pcm;

	return snd_pcm_lib_preallocate_pages_for_all(
			pcm,
			SNDRV_DMA_TYPE_CONTINUOUS,
			snd_dma_continuous_data(GFP_KERNEL),
			PREALLOC_BUFFER, PREALLOC_BUFFER_MAX);
}

static struct snd_soc_platform_driver gb_soc_platform = {
	.ops		= &gb_pcm_ops,
	.pcm_new	= gb_pcm_new,
	.pcm_free	= gb_pcm_free,
};

static int gb_soc_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &gb_soc_platform);
}

static int gb_soc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

struct platform_driver gb_audio_pcm_driver = {
	.driver = {
			.name = "gb-pcm-audio",
			.owner = THIS_MODULE,
	},
	.probe = gb_soc_platform_probe,
	.remove = gb_soc_platform_remove,
};
