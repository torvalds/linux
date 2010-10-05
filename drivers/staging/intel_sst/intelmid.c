/*
 *   intelmid.c - Intel Sound card driver for MID
 *
 *  Copyright (C) 2008-10 Intel Corp
 *  Authors:	Harsha Priya <priya.harsha@intel.com>
 *		Vinod Koul <vinod.koul@intel.com>
 *		Dharageswari R <dharageswari.r@intel.com>
 *		KP Jeeja <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * ALSA driver for Intel MID sound card chipset
 */
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <sound/control.h>
#include <asm/mrst.h>
#include <sound/pcm.h>
#include "jack.h"
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include "intel_sst.h"
#include "intel_sst_ioctl.h"
#include "intelmid_snd_control.h"
#include "intelmid.h"

MODULE_AUTHOR("Vinod Koul <vinod.koul@intel.com>");
MODULE_AUTHOR("Harsha Priya <priya.harsha@intel.com>");
MODULE_AUTHOR("Dharageswari R <dharageswari.r@intel.com>");
MODULE_AUTHOR("KP Jeeja <jeeja.kp@intel.com>");
MODULE_DESCRIPTION("Intel MAD Sound card driver");
MODULE_LICENSE("GPL v2");
MODULE_SUPPORTED_DEVICE("{Intel,Intel_MAD}");


static int card_index = SNDRV_DEFAULT_IDX1;/* Index 0-MAX */
static char *card_id = SNDRV_DEFAULT_STR1;	/* ID for this card */

module_param(card_index, int, 0444);
MODULE_PARM_DESC(card_index, "Index value for INTELMAD soundcard.");
module_param(card_id, charp, 0444);
MODULE_PARM_DESC(card_id, "ID string for INTELMAD soundcard.");

int	sst_card_vendor_id;
int intelmid_audio_interrupt_enable;/*checkpatch fix*/

/* Data path functionalities */
static struct snd_pcm_hardware snd_intelmad_stream = {
	.info =	(SNDRV_PCM_INFO_INTERLEAVED |
			SNDRV_PCM_INFO_DOUBLE |
			SNDRV_PCM_INFO_PAUSE |
			SNDRV_PCM_INFO_RESUME |
			SNDRV_PCM_INFO_MMAP|
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_BLOCK_TRANSFER |
			SNDRV_PCM_INFO_SYNC_START),
	.formats = (SNDRV_PCM_FMTBIT_S16 | SNDRV_PCM_FMTBIT_U16 |
			SNDRV_PCM_FMTBIT_S24 | SNDRV_PCM_FMTBIT_U24 |
			SNDRV_PCM_FMTBIT_S32 | SNDRV_PCM_FMTBIT_U32),
	.rates = (SNDRV_PCM_RATE_8000|
			SNDRV_PCM_RATE_44100 |
			SNDRV_PCM_RATE_48000),
	.rate_min = MIN_RATE,

	.rate_max = MAX_RATE,
	.channels_min =	MIN_CHANNEL,
	.channels_max =	MAX_CHANNEL_AMIC,
	.buffer_bytes_max = MAX_BUFFER,
	.period_bytes_min = MIN_PERIOD_BYTES,
	.period_bytes_max = MAX_PERIOD_BYTES,
	.periods_min = MIN_PERIODS,
	.periods_max = MAX_PERIODS,
	.fifo_size = FIFO_SIZE,
};


/**
 * snd_intelmad_pcm_trigger - stream activities are handled here
 *
 * @substream:substream for which the stream function is called
 * @cmd:the stream commamd that requested from upper layer
 *
 * This function is called whenever an a stream activity is invoked
 */
static int snd_intelmad_pcm_trigger(struct snd_pcm_substream *substream,
					int cmd)
{
	int ret_val = 0;
	struct snd_intelmad *intelmaddata;
	struct mad_stream_pvt *stream;
	/*struct stream_buffer buffer_to_sst;*/



	WARN_ON(!substream);

	intelmaddata = snd_pcm_substream_chip(substream);
	stream = substream->runtime->private_data;

	WARN_ON(!intelmaddata->sstdrv_ops);
	WARN_ON(!intelmaddata->sstdrv_ops->scard_ops);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		pr_debug("sst: Trigger Start\n");
		ret_val = intelmaddata->sstdrv_ops->control_set(SST_SND_START,
				&stream->stream_info.str_id);
		if (ret_val)
			return ret_val;
		stream->stream_status = RUNNING;
		stream->substream = substream;
		stream->stream_status = RUNNING;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		pr_debug("sst: in stop\n");
		ret_val = intelmaddata->sstdrv_ops->control_set(SST_SND_DROP,
				&stream->stream_info.str_id);
		if (ret_val)
			return ret_val;
		stream->stream_status = DROPPED;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		pr_debug("sst: in pause\n");
		ret_val = intelmaddata->sstdrv_ops->control_set(SST_SND_PAUSE,
				&stream->stream_info.str_id);
		if (ret_val)
			return ret_val;
		stream->stream_status = PAUSED;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		pr_debug("sst: in pause release\n");
		ret_val = intelmaddata->sstdrv_ops->control_set(SST_SND_RESUME,
						&stream->stream_info.str_id);
		if (ret_val)
			return ret_val;
		stream->stream_status = RUNNING;
		break;
	default:
		return -EINVAL;
	}
	return ret_val;
}

/**
* snd_intelmad_pcm_prepare- internal preparation before starting a stream
*
* @substream:  substream for which the function is called
*
* This function is called when a stream is started for internal preparation.
*/
static int snd_intelmad_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct mad_stream_pvt *stream;
	int ret_val = 0;
	struct snd_intelmad *intelmaddata;

	pr_debug("sst: pcm_prepare called\n");

	WARN_ON(!substream);
	stream = substream->runtime->private_data;
	intelmaddata = snd_pcm_substream_chip(substream);
	pr_debug("sst: pb cnt = %d cap cnt = %d\n",\
		intelmaddata->playback_cnt,
		intelmaddata->capture_cnt);

	if (stream->stream_info.str_id) {
		pr_debug("sst: Prepare called for already set stream\n");
		ret_val = intelmaddata->sstdrv_ops->control_set(SST_SND_DROP,
					&stream->stream_info.str_id);
		return ret_val;
	}

	 ret_val = snd_intelmad_alloc_stream(substream);
	if (ret_val < 0)
		return ret_val;
	stream->dbg_cum_bytes = 0;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			intelmaddata->playback_cnt++;
	else
		intelmaddata->capture_cnt++;
	/* return back the stream id */
	snprintf(substream->pcm->id, sizeof(substream->pcm->id),
			"%d", stream->stream_info.str_id);
	pr_debug("sst: stream id to user = %s\n",
			substream->pcm->id);

	ret_val = snd_intelmad_init_stream(substream);
	if (ret_val)
		return ret_val;
	substream->runtime->hw.info = SNDRV_PCM_INFO_BLOCK_TRANSFER;
	return ret_val;
}

static int snd_intelmad_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *hw_params)
{
	int ret_val;

	pr_debug("sst: snd_intelmad_hw_params called\n");
	ret_val = snd_pcm_lib_malloc_pages(substream,
			params_buffer_bytes(hw_params));
	memset(substream->runtime->dma_area, 0,
			params_buffer_bytes(hw_params));

	return ret_val;
}

static int snd_intelmad_hw_free(struct snd_pcm_substream *substream)
{
	pr_debug("sst: snd_intelmad_hw_free called\n");
	return snd_pcm_lib_free_pages(substream);
}

/**
 * snd_intelmad_pcm_pointer- to send the current buffer pointer processed by hw
 *
 * @substream:  substream for which the function is called
 *
 * This function is called by ALSA framework to get the current hw buffer ptr
 * when a period is elapsed
 */
static snd_pcm_uframes_t snd_intelmad_pcm_pointer
			(struct snd_pcm_substream *substream)
{
	/* struct snd_pcm_runtime *runtime = substream->runtime; */
	struct mad_stream_pvt *stream;
	struct snd_intelmad *intelmaddata;
	int ret_val;

	WARN_ON(!substream);

	intelmaddata = snd_pcm_substream_chip(substream);
	stream = substream->runtime->private_data;
	if (stream->stream_status == INIT)
		return 0;

	ret_val = intelmaddata->sstdrv_ops->control_set(SST_SND_BUFFER_POINTER,
				&stream->stream_info);
	if (ret_val) {
		pr_err("sst: error code = 0x%x\n", ret_val);
		return ret_val;
	}
	pr_debug("sst: samples reported out 0x%llx\n",
			stream->stream_info.buffer_ptr);
	pr_debug("sst: Frame bits:: %d period_count :: %d\n",
			(int)substream->runtime->frame_bits,
			(int)substream->runtime->period_size);

	return stream->stream_info.buffer_ptr;

}

/**
 * snd_intelmad_close- to free parameteres when stream is stopped
 *
 * @substream:  substream for which the function is called
 *
 * This function is called by ALSA framework when stream is stopped
 */
static int snd_intelmad_close(struct snd_pcm_substream *substream)
{
	struct snd_intelmad *intelmaddata;
	struct mad_stream_pvt *stream;
	int ret_val = 0;

	WARN_ON(!substream);

	stream = substream->runtime->private_data;

	pr_debug("sst: snd_intelmad_close called\n");
	intelmaddata = snd_pcm_substream_chip(substream);

	pr_debug("sst: str id = %d\n", stream->stream_info.str_id);
	if (stream->stream_info.str_id) {
		/* SST API to actually stop/free the stream */
		ret_val = intelmaddata->sstdrv_ops->control_set(SST_SND_FREE,
				&stream->stream_info.str_id);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			intelmaddata->playback_cnt--;
		else
			intelmaddata->capture_cnt--;
	}
	pr_debug("sst: snd_intelmad_close : pb cnt = %d cap cnt = %d\n",
		intelmaddata->playback_cnt, intelmaddata->capture_cnt);
	kfree(substream->runtime->private_data);
	return ret_val;
}

/**
 * snd_intelmad_open- to set runtime parameters during stream start
 *
 * @substream:  substream for which the function is called
 * @type: audio device type
 *
 * This function is called by ALSA framework when stream is started
 */
static int snd_intelmad_open(struct snd_pcm_substream *substream,
			enum snd_sst_audio_device_type type)
{
	struct snd_intelmad *intelmaddata;
	struct snd_pcm_runtime *runtime;
	struct mad_stream_pvt *stream;

	WARN_ON(!substream);

	pr_debug("sst: snd_intelmad_open called\n");

	intelmaddata = snd_pcm_substream_chip(substream);
	runtime = substream->runtime;
	/* set the runtime hw parameter with local snd_pcm_hardware struct */
	runtime->hw = snd_intelmad_stream;
	if (intelmaddata->cpu_id == CPU_CHIP_PENWELL) {
		runtime->hw = snd_intelmad_stream;
		runtime->hw.rates = SNDRV_PCM_RATE_48000;
		runtime->hw.rate_min = MAX_RATE;
		runtime->hw.formats = (SNDRV_PCM_FMTBIT_S24 |
						SNDRV_PCM_FMTBIT_U24);
		if (intelmaddata->sstdrv_ops->scard_ops->input_dev_id == AMIC)
			runtime->hw.channels_max = MAX_CHANNEL_AMIC;
		else
			runtime->hw.channels_max = MAX_CHANNEL_DMIC;

	}
	/* setup the internal datastruture stream pointers based on it being
	playback or capture stream */
	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return -ENOMEM;
	stream->stream_info.str_id = 0;
	stream->device = type;
	stream->stream_status = INIT;
	runtime->private_data = stream;
	return snd_pcm_hw_constraint_integer(runtime,
			 SNDRV_PCM_HW_PARAM_PERIODS);
}

static int snd_intelmad_headset_open(struct snd_pcm_substream *substream)
{
	return snd_intelmad_open(substream, SND_SST_DEVICE_HEADSET);
}

static int snd_intelmad_ihf_open(struct snd_pcm_substream *substream)
{
	return snd_intelmad_open(substream, SND_SST_DEVICE_IHF);
}

static int snd_intelmad_vibra_open(struct snd_pcm_substream *substream)
{
	return snd_intelmad_open(substream, SND_SST_DEVICE_VIBRA);
}

static int snd_intelmad_haptic_open(struct snd_pcm_substream *substream)
{
	return snd_intelmad_open(substream, SND_SST_DEVICE_HAPTIC);
}

static struct snd_pcm_ops snd_intelmad_headset_ops = {
	.open = snd_intelmad_headset_open,
	.close = snd_intelmad_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_intelmad_hw_params,
	.hw_free = snd_intelmad_hw_free,
	.prepare = snd_intelmad_pcm_prepare,
	.trigger = snd_intelmad_pcm_trigger,
	.pointer = snd_intelmad_pcm_pointer,
};

static struct snd_pcm_ops snd_intelmad_ihf_ops = {
	.open = snd_intelmad_ihf_open,
	.close = snd_intelmad_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_intelmad_hw_params,
	.hw_free = snd_intelmad_hw_free,
	.prepare = snd_intelmad_pcm_prepare,
	.trigger = snd_intelmad_pcm_trigger,
	.pointer = snd_intelmad_pcm_pointer,
};

static struct snd_pcm_ops snd_intelmad_vibra_ops = {
	.open = snd_intelmad_vibra_open,
	.close = snd_intelmad_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_intelmad_hw_params,
	.hw_free = snd_intelmad_hw_free,
	.prepare = snd_intelmad_pcm_prepare,
	.trigger = snd_intelmad_pcm_trigger,
	.pointer = snd_intelmad_pcm_pointer,
};

static struct snd_pcm_ops snd_intelmad_haptic_ops = {
	.open = snd_intelmad_haptic_open,
	.close = snd_intelmad_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_intelmad_hw_params,
	.hw_free = snd_intelmad_hw_free,
	.prepare = snd_intelmad_pcm_prepare,
	.trigger = snd_intelmad_pcm_trigger,
	.pointer = snd_intelmad_pcm_pointer,
};

static struct snd_pcm_ops snd_intelmad_capture_ops = {
	.open = snd_intelmad_headset_open,
	.close = snd_intelmad_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_intelmad_hw_params,
	.hw_free = snd_intelmad_hw_free,
	.prepare = snd_intelmad_pcm_prepare,
	.trigger = snd_intelmad_pcm_trigger,
	.pointer = snd_intelmad_pcm_pointer,
};


/**
 * snd_intelmad_intr_handler- interrupt handler
 *
 * @irq :  irq number of the interrupt received
 * @dev: device context
 *
 * This function is called when an interrupt is raised at the sound card
 */
static irqreturn_t snd_intelmad_intr_handler(int irq, void *dev)
{
	struct snd_intelmad *intelmaddata =
			(struct snd_intelmad *)dev;
	u8 intsts;

	memcpy_fromio(&intsts,
			((void *)(intelmaddata->int_base)),
			sizeof(u8));
	intelmaddata->mad_jack_msg.intsts = intsts;
	intelmaddata->mad_jack_msg.intelmaddata = intelmaddata;

	queue_work(intelmaddata->mad_jack_wq, &intelmaddata->mad_jack_msg.wq);

	return IRQ_HANDLED;
}

void sst_mad_send_jack_report(struct snd_jack *jack,
				int buttonpressevent , int status)
{

	if (!jack) {
		pr_debug("sst: MAD error jack empty\n");

	} else {
		pr_debug("sst: MAD send jack report for = %d!!!\n", status);
		pr_debug("sst: MAD send jack report %d\n", jack->type);
		snd_jack_report(jack, status);

		/*button pressed and released */
		if (buttonpressevent)
			snd_jack_report(jack, 0);
		pr_debug("sst: MAD sending jack report Done !!!\n");
	}



}

void sst_mad_jackdetection_fs(u8 intsts , struct snd_intelmad *intelmaddata)
{
	struct snd_jack *jack = NULL;
	unsigned int present = 0, jack_event_flag = 0, buttonpressflag = 0;
	struct sc_reg_access sc_access[] = {
				{0x187, 0x00, MASK7},
				{0x188, 0x10, MASK4},
				{0x18b, 0x10, MASK4},
	};

	struct sc_reg_access sc_access_write[] = {
				{0x198, 0x00, 0x0},
	};

	if (intsts & 0x4) {

		if (!(intelmid_audio_interrupt_enable)) {
			pr_debug("sst: Audio interrupt enable\n");
			sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 3);

			sst_sc_reg_access(sc_access_write, PMIC_WRITE, 1);
			intelmid_audio_interrupt_enable = 1;
			intelmaddata->jack[0].jack_status = 0;
			intelmaddata->jack[1].jack_status = 0;

		}
		/* send headphone detect */
		pr_debug("sst: MAD headphone %d\n", intsts & 0x4);
		jack = &intelmaddata->jack[0].jack;
		present = !(intelmaddata->jack[0].jack_status);
		intelmaddata->jack[0].jack_status = present;
		jack_event_flag = 1;

	}

	if (intsts & 0x2) {
		/* send short push */
		pr_debug("sst: MAD short push %d\n", intsts & 0x2);
		jack = &intelmaddata->jack[2].jack;
		present = 1;
		jack_event_flag = 1;
		buttonpressflag = 1;
	}
	if (intsts & 0x1) {
		/* send long push */
		pr_debug("sst: MAD long push %d\n", intsts & 0x1);
		jack = &intelmaddata->jack[3].jack;
		present = 1;
		jack_event_flag = 1;
		buttonpressflag = 1;
	}
	if (intsts & 0x8) {
		if (!(intelmid_audio_interrupt_enable)) {
			pr_debug("sst: Audio interrupt enable\n");
			sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 3);

			sst_sc_reg_access(sc_access_write, PMIC_WRITE, 1);
			intelmid_audio_interrupt_enable = 1;
			intelmaddata->jack[0].jack_status = 0;
			intelmaddata->jack[1].jack_status = 0;
		}
		/* send headset detect */
		pr_debug("sst: MAD headset = %d\n", intsts & 0x8);
		jack = &intelmaddata->jack[1].jack;
		present = !(intelmaddata->jack[1].jack_status);
		intelmaddata->jack[1].jack_status = present;
		jack_event_flag = 1;
	}

	if (jack_event_flag)
		sst_mad_send_jack_report(jack, buttonpressflag, present);
}


void sst_mad_jackdetection_mx(u8 intsts, struct snd_intelmad *intelmaddata)
{
	u8 value = 0, jack_prev_state = 0;
	struct snd_jack *jack = NULL;
	unsigned int present = 0, jack_event_flag = 0, buttonpressflag = 0;
	time_t  timediff;
	struct sc_reg_access sc_access_read = {0,};
	struct snd_pmic_ops *scard_ops;

	scard_ops = intelmaddata->sstdrv_ops->scard_ops;

	pr_debug("sst: previous value: %x\n", intelmaddata->jack_prev_state);

	if (!(intelmid_audio_interrupt_enable)) {
		pr_debug("sst: Audio interrupt enable\n");
		intelmaddata->jack_prev_state = 0xC0;
		intelmid_audio_interrupt_enable = 1;
	}

	if (intsts & 0x2) {
		jack_prev_state = intelmaddata->jack_prev_state;
		if (intelmaddata->pmic_status == PMIC_INIT) {
			sc_access_read.reg_addr = 0x201;
			sst_sc_reg_access(&sc_access_read, PMIC_READ, 1);
			value = (sc_access_read.value);
			pr_debug("sst: value returned = 0x%x\n", value);
		}

		if (jack_prev_state == 0xc0 && value == 0x40) {
			/*headset detected. */
			pr_debug("sst: MAD headset inserted\n");
			jack = &intelmaddata->jack[1].jack;
			present = 1;
			jack_event_flag = 1;
			intelmaddata->jack[1].jack_status = 1;

		}

		if (jack_prev_state == 0xc0 && value == 0x00) {
			/* headphone  detected. */
			pr_debug("sst: MAD headphone inserted\n");
			jack = &intelmaddata->jack[0].jack;
			present = 1;
			jack_event_flag = 1;

		}

		if (jack_prev_state == 0x40 && value == 0xc0) {
			/*headset  removed*/
			pr_debug("sst: Jack headset status %d\n",
				intelmaddata->jack[1].jack_status);
			pr_debug("sst: MAD headset removed\n");
			jack = &intelmaddata->jack[1].jack;
			present = 0;
			jack_event_flag = 1;
			intelmaddata->jack[1].jack_status = 0;
		}

		if (jack_prev_state == 0x00 && value == 0xc0) {
			/* headphone  detected. */
			pr_debug("sst: Jack headphone status %d\n",
					intelmaddata->jack[0].jack_status);
			pr_debug("sst: headphone removed\n");
			jack = &intelmaddata->jack[0].jack;
			present = 0;
			jack_event_flag = 1;
		}

		if (jack_prev_state == 0x40 && value == 0x00) {
			/*button pressed*/
			do_gettimeofday(&intelmaddata->jack[1].buttonpressed);
			pr_debug("sst: MAD button press detected n");
		}


		if (jack_prev_state == 0x00 && value == 0x40) {
			if (intelmaddata->jack[1].jack_status) {
				/*button pressed*/
				do_gettimeofday(
					&intelmaddata->jack[1].buttonreleased);
				/*button pressed */
				pr_debug("sst: Button Released detected\n");
				timediff = intelmaddata->jack[1].
					buttonreleased.tv_sec - intelmaddata->
					jack[1].buttonpressed.tv_sec;
				buttonpressflag = 1;
				if (timediff > 1) {
					pr_debug("sst: long press detected\n");
					/* send headphone detect/undetect */
					jack = &intelmaddata->jack[3].jack;
					present = 1;
					jack_event_flag = 1;
				}	else	{
					pr_debug("sst: short press detected\n");
					/* send headphone detect/undetect */
					jack = &intelmaddata->jack[2].jack;
					present = 1;
					jack_event_flag = 1;
				}
			}

		}
		intelmaddata->jack_prev_state  = value ;

	}
	if (is_aava() && jack) {
		if (present) {
			pr_debug("sst: Jack... YES\n");
			scard_ops->set_output_dev(STEREO_HEADPHONE);

		} else {
			pr_debug("sst: Jack... NO\n");
			scard_ops->set_output_dev(INTERNAL_SPKR);

		}
	}

	if (jack_event_flag)
		sst_mad_send_jack_report(jack, buttonpressflag, present);
}


void sst_mad_jackdetection_nec(u8 intsts, struct snd_intelmad *intelmaddata)
{
	u8 value = 0;
	struct snd_jack *jack = NULL;
	unsigned int present = 0, jack_event_flag = 0, buttonpressflag = 0;
	struct sc_reg_access sc_access_read = {0,};

	if (intelmaddata->pmic_status == PMIC_INIT) {
		sc_access_read.reg_addr = 0x132;
		sst_sc_reg_access(&sc_access_read, PMIC_READ, 1);
		value = (sc_access_read.value);
		pr_debug("sst: value returned = 0x%x\n", value);
	}
	if (intsts & 0x1) {
		pr_debug("sst: headset detected\n");
		/* send headset detect/undetect */
		jack = &intelmaddata->jack[1].jack;
		present = (value == 0x1) ? 1 : 0;
		jack_event_flag = 1;
	}
	if (intsts & 0x2) {
		pr_debug("sst: headphone detected\n");
		/* send headphone detect/undetect */
		jack = &intelmaddata->jack[0].jack;
		present = (value == 0x2) ? 1 : 0;
		jack_event_flag = 1;
	}
	if (intsts & 0x4) {
		pr_debug("sst: short push detected\n");
		/* send short push */
		jack = &intelmaddata->jack[2].jack;
		present = 1;
		jack_event_flag = 1;
		buttonpressflag = 1;
	}
	if (intsts & 0x8) {
		pr_debug("sst: long push detected\n");
		/* send long push */
		jack = &intelmaddata->jack[3].jack;
		present = 1;
		jack_event_flag = 1;
		buttonpressflag = 1;
	}

	if (jack_event_flag)
		sst_mad_send_jack_report(jack, buttonpressflag, present);


}

void sst_process_mad_jack_detection(struct work_struct *work)
{
	u8 intsts;
	struct mad_jack_msg_wq *mad_jack_detect =
			container_of(work, struct mad_jack_msg_wq, wq);

	struct snd_intelmad *intelmaddata =
			mad_jack_detect->intelmaddata;

	intsts = mad_jack_detect->intsts;

	switch (intelmaddata->sstdrv_ops->vendor_id) {
	case SND_FS:
		sst_mad_jackdetection_fs(intsts , intelmaddata);
		break;
	case SND_MX:
		sst_mad_jackdetection_mx(intsts , intelmaddata);
		break;
	case SND_NC:
		sst_mad_jackdetection_nec(intsts , intelmaddata);
		break;
	}
}


static int __devinit snd_intelmad_register_irq(
					struct snd_intelmad *intelmaddata)
{
	int ret_val;
	u32 regbase = AUDINT_BASE, regsize = 8;
	char *drv_name;

	pr_debug("sst: irq reg done, regbase 0x%x, regsize 0x%x\n",
					regbase, regsize);
	intelmaddata->int_base = ioremap_nocache(regbase, regsize);
	if (!intelmaddata->int_base)
		pr_err("sst: Mapping of cache failed\n");
	pr_debug("sst: irq = 0x%x\n", intelmaddata->irq);
	if (intelmaddata->cpu_id == CPU_CHIP_PENWELL)
		drv_name = DRIVER_NAME_MFLD;
	else
		drv_name = DRIVER_NAME_MRST;
	ret_val = request_irq(intelmaddata->irq,
				snd_intelmad_intr_handler,
				IRQF_SHARED, drv_name,
				intelmaddata);
	if (ret_val)
		pr_err("sst: cannot register IRQ\n");
	return ret_val;
}

static int __devinit snd_intelmad_sst_register(
			struct snd_intelmad *intelmaddata)
{
	int ret_val = 0;
	struct snd_pmic_ops *intelmad_vendor_ops[MAX_VENDORS] = {
		&snd_pmic_ops_fs,
		&snd_pmic_ops_mx,
		&snd_pmic_ops_nc,
		&snd_msic_ops
	};

	struct sc_reg_access vendor_addr = {0x00, 0x00, 0x00};

	if (intelmaddata->cpu_id == CPU_CHIP_LINCROFT) {
		ret_val = sst_sc_reg_access(&vendor_addr, PMIC_READ, 1);
		if (ret_val)
			return ret_val;
		sst_card_vendor_id = (vendor_addr.value & (MASK2|MASK1|MASK0));
		pr_debug("sst: orginal n extrated vendor id = 0x%x %d\n",
				vendor_addr.value, sst_card_vendor_id);
		if (sst_card_vendor_id < 0 || sst_card_vendor_id > 2) {
			pr_err("sst: vendor card not supported!!\n");
			return -EIO;
		}
	} else
		sst_card_vendor_id = 0x3;

	intelmaddata->sstdrv_ops->module_name = SST_CARD_NAMES;
	intelmaddata->sstdrv_ops->vendor_id = sst_card_vendor_id;
	BUG_ON(!intelmad_vendor_ops[sst_card_vendor_id]);
	intelmaddata->sstdrv_ops->scard_ops =
			intelmad_vendor_ops[sst_card_vendor_id];

	if (intelmaddata->cpu_id == CPU_CHIP_PENWELL) {
		intelmaddata->sstdrv_ops->scard_ops->pb_on = 0;
		intelmaddata->sstdrv_ops->scard_ops->cap_on = 0;
		intelmaddata->sstdrv_ops->scard_ops->input_dev_id = DMIC;
		intelmaddata->sstdrv_ops->scard_ops->output_dev_id =
							STEREO_HEADPHONE;
	}

	/* registering with SST driver to get access to SST APIs to use */
	ret_val = register_sst_card(intelmaddata->sstdrv_ops);
	if (ret_val) {
		pr_err("sst: sst card registration failed\n");
		return ret_val;
	}

	sst_card_vendor_id = intelmaddata->sstdrv_ops->vendor_id;
	intelmaddata->pmic_status = PMIC_UNINIT;
	return ret_val;
}

/* Driver Init/exit functionalities */
/**
 * snd_intelmad_pcm_new - to setup pcm for the card
 *
 * @card:  pointer to the sound card structure
 * @intelmaddata: pointer to internal context
 * @pb: playback count for this card
 * @cap: capture count for this card
 * @index: device index
 *
 * This function is called from probe function to set up pcm params
 * and functions
 */
static int __devinit snd_intelmad_pcm_new(struct snd_card *card,
			struct snd_intelmad *intelmaddata,
			unsigned int pb, unsigned int cap, unsigned int index)
{
	int ret_val = 0;
	struct snd_pcm *pcm;
	char name[32] = INTEL_MAD;
	struct snd_pcm_ops *pb_ops = NULL, *cap_ops = NULL;

	pr_debug("sst: called for pb %d, cp %d, idx %d\n", pb, cap, index);
	ret_val = snd_pcm_new(card, name, index, pb, cap, &pcm);
	if (ret_val)
		return ret_val;
	/* setup the ops for playback and capture streams */
	switch (index) {
	case 0:
		pb_ops = &snd_intelmad_headset_ops;
		cap_ops = &snd_intelmad_capture_ops;
		break;
	case 1:
		pb_ops = &snd_intelmad_ihf_ops;
		cap_ops = &snd_intelmad_capture_ops;
		break;
	case 2:
		pb_ops = &snd_intelmad_vibra_ops;
		cap_ops = &snd_intelmad_capture_ops;
		break;
	case 3:
		pb_ops = &snd_intelmad_haptic_ops;
		cap_ops = &snd_intelmad_capture_ops;
		break;
	}
	if (pb)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, pb_ops);
	if (cap)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, cap_ops);
	/* setup private data which can be retrieved when required */
	pcm->private_data = intelmaddata;
	pcm->info_flags = 0;
	strncpy(pcm->name, card->shortname, strlen(card->shortname));
	/* allocate dma pages for ALSA stream operations */
	snd_pcm_lib_preallocate_pages_for_all(pcm,
			SNDRV_DMA_TYPE_CONTINUOUS,
			snd_dma_continuous_data(GFP_KERNEL),
			MIN_BUFFER, MAX_BUFFER);
	return ret_val;
}

static int __devinit snd_intelmad_pcm(struct snd_card *card,
				struct snd_intelmad *intelmaddata)
{
	int ret_val = 0;

	WARN_ON(!card);
	WARN_ON(!intelmaddata);
	pr_debug("sst: snd_intelmad_pcm called\n");
	ret_val = snd_intelmad_pcm_new(card, intelmaddata, 1, 1, 0);
	if (intelmaddata->cpu_id == CPU_CHIP_LINCROFT)
		return ret_val;
	ret_val = snd_intelmad_pcm_new(card, intelmaddata, 1, 0, 1);
	if (ret_val)
		return ret_val;
	ret_val = snd_intelmad_pcm_new(card, intelmaddata, 1, 0, 2);
	if (ret_val)
		return ret_val;
	return snd_intelmad_pcm_new(card, intelmaddata, 1, 0, 3);
}

/**
 * snd_intelmad_jack- to setup jack settings of the card
 *
 * @intelmaddata: pointer to internal context
 *
 * This function is called send jack events
 */
static int snd_intelmad_jack(struct snd_intelmad *intelmaddata)
{
	struct snd_jack *jack;
	int retval;

	pr_debug("sst: snd_intelmad_jack called\n");
	jack = &intelmaddata->jack[0].jack;
	retval = snd_jack_new(intelmaddata->card, "Headphone",
				SND_JACK_HEADPHONE, &jack);
	if (retval < 0)
		return retval;
	snd_jack_report(jack, 0);

	jack->private_data = jack;
	intelmaddata->jack[0].jack = *jack;


	jack = &intelmaddata->jack[1].jack;
	retval = snd_jack_new(intelmaddata->card, "Headset",
				SND_JACK_HEADSET, &jack);
	if (retval < 0)
		return retval;



	jack->private_data = jack;
	intelmaddata->jack[1].jack = *jack;


	jack = &intelmaddata->jack[2].jack;
	retval = snd_jack_new(intelmaddata->card, "Short Press",
				SND_JACK_HS_SHORT_PRESS, &jack);
	if (retval < 0)
		return retval;


	jack->private_data = jack;
	intelmaddata->jack[2].jack = *jack;


	jack = &intelmaddata->jack[3].jack;
	retval = snd_jack_new(intelmaddata->card, "Long Press",
				SND_JACK_HS_LONG_PRESS, &jack);
	if (retval < 0)
		return retval;


	jack->private_data = jack;
	intelmaddata->jack[3].jack = *jack;

	return retval;
}

/**
 * snd_intelmad_mixer- to setup mixer settings of the card
 *
 * @intelmaddata: pointer to internal context
 *
 * This function is called from probe function to set up mixer controls
 */
static int __devinit snd_intelmad_mixer(struct snd_intelmad *intelmaddata)
{
	struct snd_card *card;
	unsigned int idx;
	int ret_val = 0, max_controls = 0;
	char *mixername = "IntelMAD Controls";
	struct snd_kcontrol_new *controls;

	WARN_ON(!intelmaddata);

	card = intelmaddata->card;
	strncpy(card->mixername, mixername, sizeof(card->mixername)-1);
	/* add all widget controls and expose the same */
	if (intelmaddata->cpu_id == CPU_CHIP_PENWELL) {
		max_controls = MAX_CTRL_MFLD;
		controls = snd_intelmad_controls_mfld;
	} else {
		max_controls = MAX_CTRL_MRST;
		controls = snd_intelmad_controls_mrst;
	}
	for (idx = 0; idx < max_controls; idx++) {
		ret_val = snd_ctl_add(card,
				snd_ctl_new1(&controls[idx],
				intelmaddata));
		pr_debug("sst: mixer[idx]=%d added\n", idx);
		if (ret_val) {
			pr_err("sst: in adding of control index = %d\n", idx);
			break;
		}
	}
	return ret_val;
}

static int snd_intelmad_dev_free(struct snd_device *device)
{
	struct snd_intelmad *intelmaddata;

	WARN_ON(!device);

	intelmaddata = device->device_data;

	pr_debug("sst: snd_intelmad_dev_free called\n");
	snd_card_free(intelmaddata->card);
	/*genl_unregister_family(&audio_event_genl_family);*/
	unregister_sst_card(intelmaddata->sstdrv_ops);

	/* free allocated memory for internal context */
	destroy_workqueue(intelmaddata->mad_jack_wq);
	kfree(intelmaddata->sstdrv_ops);
	kfree(intelmaddata);
	return 0;
}

static int __devinit snd_intelmad_create(
		struct snd_intelmad *intelmaddata,
		struct snd_card *card)
{
	int ret_val;
	static struct snd_device_ops ops = {
		.dev_free =	snd_intelmad_dev_free,
	};

	WARN_ON(!intelmaddata);
	WARN_ON(!card);
	/* ALSA api to register for the device */
	ret_val = snd_device_new(card, SNDRV_DEV_LOWLEVEL, intelmaddata, &ops);
	return ret_val;
}

/**
* snd_intelmad_probe- function registred for init
* @pdev :  pointer to the device struture
* This function is called when the device is initialized
*/
int __devinit snd_intelmad_probe(struct platform_device *pdev)
{
	struct snd_card *card;
	int ret_val;
	struct snd_intelmad *intelmaddata;
	const struct platform_device_id *id = platform_get_device_id(pdev);
	unsigned int cpu_id = (unsigned int)id->driver_data;

	pr_debug("sst: probe for %s cpu_id %d\n", pdev->name, cpu_id);
	if (!strcmp(pdev->name, DRIVER_NAME_MRST))
		pr_debug("sst: detected MRST\n");
	else if (!strcmp(pdev->name, DRIVER_NAME_MFLD))
		pr_debug("sst: detected MFLD\n");
	else {
		pr_err("sst: detected unknown device abort!!\n");
		return -EIO;
	}
	if ((cpu_id < CPU_CHIP_LINCROFT) || (cpu_id > CPU_CHIP_PENWELL)) {
		pr_err("sst: detected unknown cpu_id abort!!\n");
		return -EIO;
	}
	/* allocate memory for saving internal context and working */
	intelmaddata = kzalloc(sizeof(*intelmaddata), GFP_KERNEL);
	if (!intelmaddata) {
		pr_debug("sst: mem alloctn fail\n");
		return -ENOMEM;
	}

	/* allocate memory for LPE API set */
	intelmaddata->sstdrv_ops = kzalloc(sizeof(struct intel_sst_card_ops),
					GFP_KERNEL);
	if (!intelmaddata->sstdrv_ops) {
		pr_err("sst: mem allocation for ops fail\n");
		kfree(intelmaddata);
		return -ENOMEM;
	}

	intelmaddata->cpu_id = cpu_id;
	/* create a card instance with ALSA framework */
	ret_val = snd_card_create(card_index, card_id, THIS_MODULE, 0, &card);
	if (ret_val) {
		pr_err("sst: snd_card_create fail\n");
		goto free_allocs;
	}

	intelmaddata->pdev = pdev;
	intelmaddata->irq = platform_get_irq(pdev, 0);
	platform_set_drvdata(pdev, intelmaddata);
	intelmaddata->card = card;
	intelmaddata->card_id = card_id;
	intelmaddata->card_index = card_index;
	intelmaddata->master_mute = UNMUTE;
	intelmaddata->playback_cnt =  intelmaddata->capture_cnt = 0;
	strncpy(card->driver, INTEL_MAD, strlen(INTEL_MAD));
	strncpy(card->shortname, INTEL_MAD, strlen(INTEL_MAD));

	intelmaddata->sstdrv_ops->module_name = SST_CARD_NAMES;
	/* registering with LPE driver to get access to SST APIs to use */
	ret_val = snd_intelmad_sst_register(intelmaddata);
	if (ret_val) {
		pr_err("sst: snd_intelmad_sst_register failed\n");
		goto free_allocs;
	}

	intelmaddata->pmic_status = PMIC_INIT;

	ret_val = snd_intelmad_pcm(card, intelmaddata);
	if (ret_val) {
		pr_err("sst: snd_intelmad_pcm failed\n");
		goto free_allocs;
	}

	ret_val = snd_intelmad_mixer(intelmaddata);
	if (ret_val) {
		pr_err("sst: snd_intelmad_mixer failed\n");
		goto free_allocs;
	}

	ret_val = snd_intelmad_jack(intelmaddata);
	if (ret_val) {
		pr_err("sst: snd_intelmad_jack failed\n");
		goto free_allocs;
	}

	/*create work queue for jack interrupt*/
	INIT_WORK(&intelmaddata->mad_jack_msg.wq,
		sst_process_mad_jack_detection);

	intelmaddata->mad_jack_wq = create_workqueue("sst_mad_jack_wq");
	if (!intelmaddata->mad_jack_wq)
		goto free_mad_jack_wq;

	ret_val = snd_intelmad_register_irq(intelmaddata);
	if (ret_val) {
		pr_err("sst: snd_intelmad_register_irq fail\n");
		goto free_allocs;
	}

	/* internal function call to register device with ALSA */
	ret_val = snd_intelmad_create(intelmaddata, card);
	if (ret_val) {
		pr_err("sst: snd_intelmad_create failed\n");
		goto free_allocs;
	}
	card->private_data = &intelmaddata;
	snd_card_set_dev(card, &pdev->dev);
	ret_val = snd_card_register(card);
	if (ret_val) {
		pr_err("sst: snd_card_register failed\n");
		goto free_allocs;
	}

	pr_debug("sst:snd_intelmad_probe complete\n");
	return ret_val;

free_mad_jack_wq:
	destroy_workqueue(intelmaddata->mad_jack_wq);
free_allocs:
	pr_err("sst: probe failed\n");
	snd_card_free(card);
	kfree(intelmaddata->sstdrv_ops);
	kfree(intelmaddata);
	return ret_val;
}


static int snd_intelmad_remove(struct platform_device *pdev)
{
	struct snd_intelmad *intelmaddata = platform_get_drvdata(pdev);

	if (intelmaddata) {
		snd_card_free(intelmaddata->card);
		unregister_sst_card(intelmaddata->sstdrv_ops);
		/* free allocated memory for internal context */
		destroy_workqueue(intelmaddata->mad_jack_wq);
		kfree(intelmaddata->sstdrv_ops);
		kfree(intelmaddata);
	}
	return 0;
}

/*********************************************************************
 *		Driver initialization and exit
 *********************************************************************/
static const struct platform_device_id snd_intelmad_ids[] = {
	{DRIVER_NAME_MRST, CPU_CHIP_LINCROFT},
	{DRIVER_NAME_MFLD, CPU_CHIP_PENWELL},
	{"", 0},

};

static struct platform_driver snd_intelmad_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "intel_mid_sound_card",
	},
	.id_table = snd_intelmad_ids,
	.probe = snd_intelmad_probe,
	.remove = __devexit_p(snd_intelmad_remove),
};

/*
 * alsa_card_intelmad_init- driver init function
 *
 * This function is called when driver module is inserted
 */
static int __init alsa_card_intelmad_init(void)
{
	pr_debug("sst: mad_init called\n");
	return platform_driver_register(&snd_intelmad_driver);
}

/**
 * alsa_card_intelmad_exit- driver exit function
 *
 * This function is called when driver module is removed
 */
static void __exit alsa_card_intelmad_exit(void)
{
	pr_debug("sst:mad_exit called\n");
	return platform_driver_unregister(&snd_intelmad_driver);
}

module_init(alsa_card_intelmad_init)
module_exit(alsa_card_intelmad_exit)

