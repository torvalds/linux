/*
 *  intel_sst_interface.c - Intel SST Driver for audio engine
 *
 *  Copyright (C) 2008-10 Intel Corp
 *  Authors:	Vinod Koul <vinod.koul@intel.com>
 *		Harsha Priya <priya.harsha@intel.com>
 *		Dharageswari R <dharageswari.r@intel.com)
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
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
 *  This driver exposes the audio engine functionalities to the ALSA
 *	and middleware.
 *  Upper layer interfaces (MAD driver, MMF) to SST driver
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/firmware.h>
#include <linux/pm_runtime.h>
#include <linux/export.h>
#include "intel_sst.h"
#include "intel_sst_ioctl.h"
#include "intel_sst_fw_ipc.h"
#include "intel_sst_common.h"


/*
 * sst_download_fw - download the audio firmware to DSP
 *
 * This function is called when the FW needs to be downloaded to SST DSP engine
 */
int sst_download_fw(void)
{
	int retval;
	const struct firmware *fw_sst;
	char name[20];

	if (sst_drv_ctx->sst_state != SST_UN_INIT)
		return -EPERM;

	/* Reload firmware is not needed for MRST */
	if ( (sst_drv_ctx->pci_id == SST_MRST_PCI_ID) && sst_drv_ctx->fw_downloaded) {
		pr_debug("FW already downloaded, skip for MRST platform\n");
		sst_drv_ctx->sst_state = SST_FW_RUNNING;
		return 0;
	}

	snprintf(name, sizeof(name), "%s%04x%s", "fw_sst_",
					sst_drv_ctx->pci_id, ".bin");

	pr_debug("Downloading %s FW now...\n", name);
	retval = request_firmware(&fw_sst, name, &sst_drv_ctx->pci->dev);
	if (retval) {
		pr_err("request fw failed %d\n", retval);
		return retval;
	}
	sst_drv_ctx->alloc_block[0].sst_id = FW_DWNL_ID;
	sst_drv_ctx->alloc_block[0].ops_block.condition = false;
	retval = sst_load_fw(fw_sst, NULL);
	if (retval)
		goto end_restore;

	retval = sst_wait_timeout(sst_drv_ctx, &sst_drv_ctx->alloc_block[0]);
	if (retval)
		pr_err("fw download failed %d\n" , retval);
	else
		sst_drv_ctx->fw_downloaded = 1;

end_restore:
	release_firmware(fw_sst);
	sst_drv_ctx->alloc_block[0].sst_id = BLOCK_UNINIT;
	return retval;
}


/*
 * sst_stalled - this function checks if the lpe is in stalled state
 */
int sst_stalled(void)
{
	int retry = 1000;
	int retval = -1;

	while (retry) {
		if (!sst_drv_ctx->lpe_stalled)
			return 0;
		/*wait for time and re-check*/
		msleep(1);

		retry--;
	}
	pr_debug("in Stalled State\n");
	return retval;
}

void free_stream_context(unsigned int str_id)
{
	struct stream_info *stream;

	if (!sst_validate_strid(str_id)) {
		/* str_id is valid, so stream is alloacted */
		stream = &sst_drv_ctx->streams[str_id];
		if (sst_free_stream(str_id))
			sst_clean_stream(&sst_drv_ctx->streams[str_id]);
		if (stream->ops == STREAM_OPS_PLAYBACK ||
				stream->ops == STREAM_OPS_PLAYBACK_DRM) {
			sst_drv_ctx->pb_streams--;
			if (sst_drv_ctx->pci_id == SST_MFLD_PCI_ID)
				sst_drv_ctx->scard_ops->power_down_pmic_pb(
						stream->device);
			else {
				if (sst_drv_ctx->pb_streams == 0)
					sst_drv_ctx->scard_ops->
					power_down_pmic_pb(stream->device);
			}
		} else if (stream->ops == STREAM_OPS_CAPTURE) {
			sst_drv_ctx->cp_streams--;
			if (sst_drv_ctx->cp_streams == 0)
				sst_drv_ctx->scard_ops->power_down_pmic_cp(
						stream->device);
		}
		if (sst_drv_ctx->pb_streams == 0
				&& sst_drv_ctx->cp_streams == 0)
			sst_drv_ctx->scard_ops->power_down_pmic();
	}
}

/*
 * sst_get_stream_allocated - this function gets a stream allocated with
 * the given params
 *
 * @str_param : stream params
 * @lib_dnld : pointer to pointer of lib downlaod struct
 *
 * This creates new stream id for a stream, in case lib is to be downloaded to
 * DSP, it downloads that
 */
int sst_get_stream_allocated(struct snd_sst_params *str_param,
		struct snd_sst_lib_download **lib_dnld)
{
	int retval, str_id;
	struct stream_info *str_info;

	retval = sst_alloc_stream((char *) &str_param->sparams, str_param->ops,
				str_param->codec, str_param->device_type);
	if (retval < 0) {
		pr_err("sst_alloc_stream failed %d\n", retval);
		return retval;
	}
	pr_debug("Stream allocated %d\n", retval);
	str_id = retval;
	str_info = &sst_drv_ctx->streams[str_id];
	/* Block the call for reply */
	retval = sst_wait_interruptible_timeout(sst_drv_ctx,
			&str_info->ctrl_blk, SST_BLOCK_TIMEOUT);
	if ((retval != 0) || (str_info->ctrl_blk.ret_code != 0)) {
		pr_debug("FW alloc failed retval %d, ret_code %d\n",
				retval, str_info->ctrl_blk.ret_code);
		str_id = -str_info->ctrl_blk.ret_code; /*return error*/
		*lib_dnld = str_info->ctrl_blk.data;
		sst_clean_stream(str_info);
	} else
		pr_debug("FW Stream allocated success\n");
	return str_id; /*will ret either error (in above if) or correct str id*/
}

/*
 * sst_get_sfreq - this function returns the frequency of the stream
 *
 * @str_param : stream params
 */
static int sst_get_sfreq(struct snd_sst_params *str_param)
{
	switch (str_param->codec) {
	case SST_CODEC_TYPE_PCM:
		return 48000; /*str_param->sparams.uc.pcm_params.sfreq;*/
	case SST_CODEC_TYPE_MP3:
		return str_param->sparams.uc.mp3_params.sfreq;
	case SST_CODEC_TYPE_AAC:
		return str_param->sparams.uc.aac_params.sfreq;
	case SST_CODEC_TYPE_WMA9:
		return str_param->sparams.uc.wma_params.sfreq;
	default:
		return 0;
	}
}

/*
 * sst_get_stream - this function prepares for stream allocation
 *
 * @str_param : stream param
 */
int sst_get_stream(struct snd_sst_params *str_param)
{
	int i, retval;
	struct stream_info *str_info;
	struct snd_sst_lib_download *lib_dnld;

	/* stream is not allocated, we are allocating */
	retval = sst_get_stream_allocated(str_param, &lib_dnld);
	if (retval == -(SST_LIB_ERR_LIB_DNLD_REQUIRED)) {
		/* codec download is required */
		struct snd_sst_alloc_response *response;

		pr_debug("Codec is required.... trying that\n");
		if (lib_dnld == NULL) {
			pr_err("lib download null!!! abort\n");
			return -EIO;
		}
		i = sst_get_block_stream(sst_drv_ctx);
		response = sst_drv_ctx->alloc_block[i].ops_block.data;
		pr_debug("alloc block allocated = %d\n", i);
		if (i < 0) {
			kfree(lib_dnld);
			return -ENOMEM;
		}
		retval = sst_load_library(lib_dnld, str_param->ops);
		kfree(lib_dnld);

		sst_drv_ctx->alloc_block[i].sst_id = BLOCK_UNINIT;
		if (!retval) {
			pr_debug("codec was downloaded successfully\n");

			retval = sst_get_stream_allocated(str_param, &lib_dnld);
			if (retval <= 0)
				goto err;

			pr_debug("Alloc done stream id %d\n", retval);
		} else {
			pr_debug("codec download failed\n");
			retval = -EIO;
			goto err;
		}
	} else if  (retval <= 0)
		goto err;
	/*else
		set_port_params(str_param, str_param->ops);*/

	/* store sampling freq */
	str_info = &sst_drv_ctx->streams[retval];
	str_info->sfreq = sst_get_sfreq(str_param);

	/* power on the analog, if reqd */
	if (str_param->ops == STREAM_OPS_PLAYBACK ||
			str_param->ops == STREAM_OPS_PLAYBACK_DRM) {
		if (sst_drv_ctx->pci_id == SST_MRST_PCI_ID)
			sst_drv_ctx->scard_ops->power_up_pmic_pb(
					sst_drv_ctx->pmic_port_instance);
		else
			sst_drv_ctx->scard_ops->power_up_pmic_pb(
							str_info->device);
		/*Only if the playback is MP3 - Send a message*/
		sst_drv_ctx->pb_streams++;
	} else if (str_param->ops == STREAM_OPS_CAPTURE) {

		sst_drv_ctx->scard_ops->power_up_pmic_cp(
				sst_drv_ctx->pmic_port_instance);
		/*Send a messageif not sent already*/
		sst_drv_ctx->cp_streams++;
	}

err:
	return retval;
}

void sst_process_mad_ops(struct work_struct *work)
{

	struct mad_ops_wq *mad_ops =
			container_of(work, struct mad_ops_wq, wq);
	int retval = 0;

	switch (mad_ops->control_op) {
	case SST_SND_PAUSE:
		retval = sst_pause_stream(mad_ops->stream_id);
		break;
	case SST_SND_RESUME:
		retval = sst_resume_stream(mad_ops->stream_id);
		break;
	case SST_SND_DROP:
		retval = sst_drop_stream(mad_ops->stream_id);
		break;
	case SST_SND_START:
			pr_debug("SST Debug: start stream\n");
		retval = sst_start_stream(mad_ops->stream_id);
		break;
	case SST_SND_STREAM_PROCESS:
		pr_debug("play/capt frames...\n");
		break;
	default:
		pr_err(" wrong control_ops reported\n");
	}
	return;
}

void send_intial_rx_timeslot(void)
{
	if (sst_drv_ctx->pci_id == SST_MRST_PCI_ID &&
			sst_drv_ctx->rx_time_slot_status != RX_TIMESLOT_UNINIT
			&& sst_drv_ctx->pmic_vendor != SND_NC)
		sst_enable_rx_timeslot(sst_drv_ctx->rx_time_slot_status);
}

/*
 * sst_open_pcm_stream - Open PCM interface
 *
 * @str_param: parameters of pcm stream
 *
 * This function is called by MID sound card driver to open
 * a new pcm interface
 */
int sst_open_pcm_stream(struct snd_sst_params *str_param)
{
	struct stream_info *str_info;
	int retval;

	pm_runtime_get_sync(&sst_drv_ctx->pci->dev);

	if (sst_drv_ctx->sst_state == SST_SUSPENDED) {
		/* LPE is suspended, resume it before proceeding*/
		pr_debug("Resuming from Suspended state\n");
		retval = intel_sst_resume(sst_drv_ctx->pci);
		if (retval) {
			pr_err("Resume Failed = %#x, abort\n", retval);
			pm_runtime_put(&sst_drv_ctx->pci->dev);
			return retval;
		}
	}
	if (sst_drv_ctx->sst_state == SST_UN_INIT) {
		/* FW is not downloaded */
		pr_debug("DSP Downloading FW now...\n");
		retval = sst_download_fw();
		if (retval) {
			pr_err("FW download fail %x, abort\n", retval);
			pm_runtime_put(&sst_drv_ctx->pci->dev);
			return retval;
		}
		send_intial_rx_timeslot();
	}

	if (!str_param) {
		pm_runtime_put(&sst_drv_ctx->pci->dev);
		return -EINVAL;
	}

	retval = sst_get_stream(str_param);
	if (retval > 0) {
		sst_drv_ctx->stream_cnt++;
		str_info = &sst_drv_ctx->streams[retval];
		str_info->src = MAD_DRV;
	} else
		pm_runtime_put(&sst_drv_ctx->pci->dev);

	return retval;
}

/*
 * sst_close_pcm_stream - Close PCM interface
 *
 * @str_id: stream id to be closed
 *
 * This function is called by MID sound card driver to close
 * an existing pcm interface
 */
int sst_close_pcm_stream(unsigned int str_id)
{
	struct stream_info *stream;

	pr_debug("sst: stream free called\n");
	if (sst_validate_strid(str_id))
		return -EINVAL;
	stream = &sst_drv_ctx->streams[str_id];
	free_stream_context(str_id);
	stream->pcm_substream = NULL;
	stream->status = STREAM_UN_INIT;
	stream->period_elapsed = NULL;
	sst_drv_ctx->stream_cnt--;
	pr_debug("sst: will call runtime put now\n");
	pm_runtime_put(&sst_drv_ctx->pci->dev);
	return 0;
}

/*
 * sst_device_control - Set Control params
 *
 * @cmd: control cmd to be set
 * @arg: command argument
 *
 * This function is called by MID sound card driver to set
 * SST/Sound card controls for an opened stream.
 * This is registered with MID driver
 */
int sst_device_control(int cmd, void *arg)
{
	int retval = 0, str_id = 0;

	switch (cmd) {
	case SST_SND_PAUSE:
	case SST_SND_RESUME:
	case SST_SND_DROP:
	case SST_SND_START:
		sst_drv_ctx->mad_ops.control_op = cmd;
		sst_drv_ctx->mad_ops.stream_id = *(int *)arg;
		queue_work(sst_drv_ctx->mad_wq, &sst_drv_ctx->mad_ops.wq);
		break;

	case SST_SND_STREAM_INIT: {
		struct pcm_stream_info *str_info;
		struct stream_info *stream;

		pr_debug("stream init called\n");
		str_info = (struct pcm_stream_info *)arg;
		str_id = str_info->str_id;
		retval = sst_validate_strid(str_id);
		if (retval)
			break;

		stream = &sst_drv_ctx->streams[str_id];
		pr_debug("setting the period ptrs\n");
		stream->pcm_substream = str_info->mad_substream;
		stream->period_elapsed = str_info->period_elapsed;
		stream->sfreq = str_info->sfreq;
		stream->prev = stream->status;
		stream->status = STREAM_INIT;
		break;
	}

	case SST_SND_BUFFER_POINTER: {
		struct pcm_stream_info *stream_info;
		struct snd_sst_tstamp fw_tstamp = {0,};
		struct stream_info *stream;


		stream_info = (struct pcm_stream_info *)arg;
		str_id = stream_info->str_id;
		retval = sst_validate_strid(str_id);
		if (retval)
			break;
		stream = &sst_drv_ctx->streams[str_id];

		if (!stream->pcm_substream)
			break;
		memcpy_fromio(&fw_tstamp,
			((void *)(sst_drv_ctx->mailbox + SST_TIME_STAMP)
			+(str_id * sizeof(fw_tstamp))),
			sizeof(fw_tstamp));

		pr_debug("Pointer Query on strid = %d ops %d\n",
						str_id, stream->ops);

		if (stream->ops == STREAM_OPS_PLAYBACK)
			stream_info->buffer_ptr = fw_tstamp.samples_rendered;
		else
			stream_info->buffer_ptr = fw_tstamp.samples_processed;
		pr_debug("Samples rendered = %llu, buffer ptr %llu\n",
			fw_tstamp.samples_rendered, stream_info->buffer_ptr);
		break;
	}
	case SST_ENABLE_RX_TIME_SLOT: {
		int status = *(int *)arg;
		sst_drv_ctx->rx_time_slot_status = status ;
		sst_enable_rx_timeslot(status);
		break;
	}
	default:
		/* Illegal case */
		pr_warn("illegal req\n");
		return -EINVAL;
	}

	return retval;
}


struct intel_sst_pcm_control pcm_ops = {
	.open = sst_open_pcm_stream,
	.device_control = sst_device_control,
	.close = sst_close_pcm_stream,
};

struct intel_sst_card_ops sst_pmic_ops = {
	.pcm_control = &pcm_ops,
};

/*
 * register_sst_card - function for sound card to register
 *
 * @card: pointer to structure of operations
 *
 * This function is called card driver loads and is ready for registration
 */
int register_sst_card(struct intel_sst_card_ops *card)
{
	if (!sst_drv_ctx) {
		pr_err("No SST driver register card reject\n");
		return -ENODEV;
	}

	if (!card || !card->module_name) {
		pr_err("Null Pointer Passed\n");
		return -EINVAL;
	}
	if (sst_drv_ctx->pmic_state == SND_MAD_UN_INIT) {
		/* register this driver */
		if ((strncmp(SST_CARD_NAMES, card->module_name,
				strlen(SST_CARD_NAMES))) == 0) {
			sst_drv_ctx->pmic_vendor = card->vendor_id;
			sst_drv_ctx->scard_ops =  card->scard_ops;
			sst_pmic_ops.module_name = card->module_name;
			sst_drv_ctx->pmic_state = SND_MAD_INIT_DONE;
			sst_drv_ctx->rx_time_slot_status = 0; /*default AMIC*/
			card->pcm_control = sst_pmic_ops.pcm_control;
			return 0;
		} else {
			pr_err("strcmp fail %s\n", card->module_name);
			return -EINVAL;
		}

	} else {
		/* already registered a driver */
		pr_err("Repeat for registration..denied\n");
		return -EBADRQC;
	}
	/* The ASoC code doesn't set scard_ops */
	if (sst_drv_ctx->scard_ops)
		sst_drv_ctx->scard_ops->card_status = SND_CARD_UN_INIT;
	return 0;
}
EXPORT_SYMBOL_GPL(register_sst_card);

/*
 * unregister_sst_card- function for sound card to un-register
 *
 * @card: pointer to structure of operations
 *
 * This function is called when card driver unloads
 */
void unregister_sst_card(struct intel_sst_card_ops *card)
{
	if (sst_pmic_ops.pcm_control == card->pcm_control) {
		/* unreg */
		sst_pmic_ops.module_name = "";
		sst_drv_ctx->pmic_state = SND_MAD_UN_INIT;
		pr_debug("Unregistered %s\n", card->module_name);
	}
	return;
}
EXPORT_SYMBOL_GPL(unregister_sst_card);
