/*
 *   intelmid_pvt.h - Intel Sound card driver for MID
 *
 *  Copyright (C) 2008-10 Intel Corp
 *  Authors:	Harsha Priya <priya.harsha@intel.com>
 *		Vinod Koul <vinod.koul@intel.com>
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
 * ALSA driver for Intel MID sound card chipset - holding private functions
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <asm/intel_scu_ipc.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include "intel_sst.h"
#include "intel_sst_ioctl.h"
#include "intelmid_snd_control.h"
#include "intelmid.h"


void period_elapsed(void *mad_substream)
{
	struct snd_pcm_substream *substream = mad_substream;
	struct mad_stream_pvt *stream;



	if (!substream || !substream->runtime)
		return;
	stream = substream->runtime->private_data;
	if (!stream)
		return;

	if (stream->stream_status != RUNNING)
		return;
	pr_debug("calling period elapsed\n");
	snd_pcm_period_elapsed(substream);
	return;
}


int snd_intelmad_alloc_stream(struct snd_pcm_substream *substream)
{
	struct snd_intelmad *intelmaddata = snd_pcm_substream_chip(substream);
	struct mad_stream_pvt *stream = substream->runtime->private_data;
	struct snd_sst_stream_params param = {{{0,},},};
	struct snd_sst_params str_params = {0};
	int ret_val;

	/* set codec params and inform SST driver the same */

	param.uc.pcm_params.codec = SST_CODEC_TYPE_PCM;
	param.uc.pcm_params.num_chan = (u8) substream->runtime->channels;
	param.uc.pcm_params.pcm_wd_sz = substream->runtime->sample_bits;
	param.uc.pcm_params.reserved = 0;
	param.uc.pcm_params.sfreq = substream->runtime->rate;
	param.uc.pcm_params.ring_buffer_size =
					snd_pcm_lib_buffer_bytes(substream);
	param.uc.pcm_params.period_count = substream->runtime->period_size;
	param.uc.pcm_params.ring_buffer_addr =
				virt_to_phys(substream->runtime->dma_area);
	pr_debug("period_cnt = %d\n", param.uc.pcm_params.period_count);
	pr_debug("sfreq= %d, wd_sz = %d\n",
		 param.uc.pcm_params.sfreq, param.uc.pcm_params.pcm_wd_sz);

	str_params.sparams = param;
	str_params.codec = SST_CODEC_TYPE_PCM;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		str_params.ops = STREAM_OPS_PLAYBACK;
		pr_debug("Playbck stream,Device %d\n", stream->device);
	} else {
		str_params.ops = STREAM_OPS_CAPTURE;
		stream->device = SND_SST_DEVICE_CAPTURE;
		pr_debug("Capture stream,Device %d\n", stream->device);
	}
	str_params.device_type = stream->device;
	ret_val = intelmaddata->sstdrv_ops->pcm_control->open(&str_params);
	pr_debug("sst: SST_SND_PLAY/CAPTURE ret_val = %x\n", ret_val);
	if (ret_val < 0)
		return ret_val;

	stream->stream_info.str_id = ret_val;
	stream->stream_status = INIT;
	stream->stream_info.buffer_ptr = 0;
	pr_debug("str id :  %d\n", stream->stream_info.str_id);

	return ret_val;
}

int snd_intelmad_init_stream(struct snd_pcm_substream *substream)
{
	struct mad_stream_pvt *stream = substream->runtime->private_data;
	struct snd_intelmad *intelmaddata = snd_pcm_substream_chip(substream);
	int ret_val;

	pr_debug("setting buffer ptr param\n");
	stream->stream_info.period_elapsed = period_elapsed;
	stream->stream_info.mad_substream = substream;
	stream->stream_info.buffer_ptr = 0;
	stream->stream_info.sfreq = substream->runtime->rate;
	ret_val = intelmaddata->sstdrv_ops->pcm_control->device_control(
			SST_SND_STREAM_INIT, &stream->stream_info);
	if (ret_val)
		pr_err("control_set ret error %d\n", ret_val);
	return ret_val;

}


/**
 * sst_sc_reg_access - IPC read/write wrapper
 *
 * @sc_access:  array of data, addresses and mask
 * @type: operation type
 * @num_val: number of reg to opertae on
 *
 * Reads/writes/read-modify operations on registers accessed through SCU (sound
 * card and few SST DSP regsisters that are not accissible to IA)
 */
int sst_sc_reg_access(struct sc_reg_access *sc_access,
					int type, int num_val)
{
	int i, retval = 0;
	if (type == PMIC_WRITE) {
		for (i = 0; i < num_val; i++) {
			retval = intel_scu_ipc_iowrite8(sc_access[i].reg_addr,
							sc_access[i].value);
			if (retval)
				goto err;
		}
	} else if (type == PMIC_READ) {
		for (i = 0; i < num_val; i++) {
			retval = intel_scu_ipc_ioread8(sc_access[i].reg_addr,
							&(sc_access[i].value));
			if (retval)
				goto err;
		}
	} else {
		for (i = 0; i < num_val; i++) {
			retval = intel_scu_ipc_update_register(
				sc_access[i].reg_addr, sc_access[i].value,
				sc_access[i].mask);
			if (retval)
				goto err;
		}
	}
	return 0;
err:
	pr_err("IPC failed for cmd %d, %d\n", retval, type);
	pr_err("reg:0x%2x addr:0x%2x\n",
		sc_access[i].reg_addr, sc_access[i].value);
 	return retval;
}
