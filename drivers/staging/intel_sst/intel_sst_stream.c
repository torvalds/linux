/*
 *  intel_sst_stream.c - Intel SST Driver for audio engine
 *
 *  Copyright (C) 2008-10 Intel Corp
 *  Authors:	Vinod Koul <vinod.koul@intel.com>
 *		Harsha Priya <priya.harsha@intel.com>
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
 *
 *  This file contains the stream operations of SST driver
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/sched.h>
#include "intel_sst_ioctl.h"
#include "intel_sst.h"
#include "intel_sst_fw_ipc.h"
#include "intel_sst_common.h"

/*
 * sst_check_device_type - Check the medfield device type
 *
 * @device: Device to be checked
 * @num_ch: Number of channels queried
 * @pcm_slot: slot to be enabled for this device
 *
 * This checks the deivce against the map and calculates pcm_slot value
 */
int sst_check_device_type(u32 device, u32 num_chan, u32 *pcm_slot)
{
	if (device > MAX_NUM_STREAMS_MFLD) {
		pr_debug("device type invalid %d\n", device);
		return -EINVAL;
	}
	if (sst_drv_ctx->streams[device].status == STREAM_UN_INIT) {
		if (device == SND_SST_DEVICE_VIBRA && num_chan == 1)
			*pcm_slot = 0x10;
		else if (device == SND_SST_DEVICE_HAPTIC && num_chan == 1)
			*pcm_slot = 0x20;
		else if (device == SND_SST_DEVICE_IHF && num_chan == 1)
			*pcm_slot = 0x04;
		else if (device == SND_SST_DEVICE_IHF && num_chan == 2)
			*pcm_slot = 0x0C;
		else if (device == SND_SST_DEVICE_HEADSET && num_chan == 1)
			*pcm_slot = 0x01;
		else if (device == SND_SST_DEVICE_HEADSET && num_chan == 2)
			*pcm_slot = 0x03;
		else if (device == SND_SST_DEVICE_CAPTURE && num_chan == 1)
			*pcm_slot = 0x01;
		else if (device == SND_SST_DEVICE_CAPTURE && num_chan == 2)
			*pcm_slot = 0x03;
		else if (device == SND_SST_DEVICE_CAPTURE && num_chan == 3)
			*pcm_slot = 0x07;
		else if (device == SND_SST_DEVICE_CAPTURE && num_chan == 4)
			*pcm_slot = 0x0F;
		else {
			pr_debug("No condition satisfied.. ret err\n");
			return -EINVAL;
		}
	} else {
		pr_debug("this stream state is not uni-init, is %d\n",
				sst_drv_ctx->streams[device].status);
		return -EBADRQC;
	}
	pr_debug("returning slot %x\n", *pcm_slot);
	return 0;
}
/**
 * get_mrst_stream_id	-	gets a new stream id for use
 *
 * This functions searches the current streams and allocated an empty stream
 * lock stream_lock required to be held before calling this
 */
static unsigned int get_mrst_stream_id(void)
{
	int i;

	for (i = 1; i <= MAX_NUM_STREAMS_MRST; i++) {
		if (sst_drv_ctx->streams[i].status == STREAM_UN_INIT)
			return i;
	}
	pr_debug("Didn't find empty stream for mrst\n");
	return -EBUSY;
}

/**
 * sst_alloc_stream - Send msg for a new stream ID
 *
 * @params:	stream params
 * @stream_ops:	operation of stream PB/capture
 * @codec:	codec for stream
 * @device:	device stream to be allocated for
 *
 * This function is called by any function which wants to start
 * a new stream. This also check if a stream exists which is idle
 * it initializes idle stream id to this request
 */
int sst_alloc_stream(char *params, unsigned int stream_ops,
	       u8 codec, unsigned int device)
{
	struct ipc_post *msg = NULL;
	struct snd_sst_alloc_params alloc_param;
	unsigned int pcm_slot = 0, num_ch;
	int str_id;
	struct snd_sst_stream_params *sparams;
	struct stream_info *str_info;

	pr_debug("SST DBG:entering sst_alloc_stream\n");
	pr_debug("SST DBG:%d %d %d\n", stream_ops, codec, device);

	BUG_ON(!params);
	sparams = (struct snd_sst_stream_params *)params;
	num_ch = sparams->uc.pcm_params.num_chan;
	/*check the device type*/
	if (sst_drv_ctx->pci_id == SST_MFLD_PCI_ID) {
		if (sst_check_device_type(device, num_ch, &pcm_slot))
			return -EINVAL;
		mutex_lock(&sst_drv_ctx->stream_lock);
		str_id = device;
		mutex_unlock(&sst_drv_ctx->stream_lock);
		pr_debug("SST_DBG: slot %x\n", pcm_slot);
	} else {
		mutex_lock(&sst_drv_ctx->stream_lock);
		str_id = get_mrst_stream_id();
		mutex_unlock(&sst_drv_ctx->stream_lock);
		if (str_id <= 0)
			return -EBUSY;
	}
	/*allocate device type context*/
	sst_init_stream(&sst_drv_ctx->streams[str_id], codec,
			str_id, stream_ops, pcm_slot, device);
	/* send msg to FW to allocate a stream */
	if (sst_create_large_msg(&msg))
		return -ENOMEM;

	sst_fill_header(&msg->header, IPC_IA_ALLOC_STREAM, 1, str_id);
	msg->header.part.data = sizeof(alloc_param) + sizeof(u32);
	alloc_param.str_type.codec_type = codec;
	alloc_param.str_type.str_type = SST_STREAM_TYPE_MUSIC;
	alloc_param.str_type.operation = stream_ops;
	alloc_param.str_type.protected_str = 0; /* non drm */
	alloc_param.str_type.time_slots = pcm_slot;
	alloc_param.str_type.result = alloc_param.str_type.reserved = 0;
	memcpy(&alloc_param.stream_params, params,
			sizeof(struct snd_sst_stream_params));

	memcpy(msg->mailbox_data, &msg->header, sizeof(u32));
	memcpy(msg->mailbox_data + sizeof(u32), &alloc_param,
			sizeof(alloc_param));
	str_info = &sst_drv_ctx->streams[str_id];
	str_info->ctrl_blk.condition = false;
	str_info->ctrl_blk.ret_code = 0;
	str_info->ctrl_blk.on = true;
	spin_lock(&sst_drv_ctx->list_spin_lock);
	list_add_tail(&msg->node, &sst_drv_ctx->ipc_dispatch_list);
	spin_unlock(&sst_drv_ctx->list_spin_lock);
	sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
	pr_debug("SST DBG:alloc stream done\n");
	return str_id;
}


/*
 * sst_alloc_stream_response - process alloc reply
 *
 * @str_id:	stream id for which the stream has been allocated
 * @resp		the stream response from firware
 *
 * This function is called by firmware as a response to stream allcoation
 * request
 */
int sst_alloc_stream_response(unsigned int str_id,
				struct snd_sst_alloc_response *resp)
{
	int retval = 0;
	struct stream_info *str_info;
	struct snd_sst_lib_download *lib_dnld;

	pr_debug("SST DEBUG: stream number given = %d\n", str_id);
	str_info = &sst_drv_ctx->streams[str_id];
	if (resp->str_type.result == SST_LIB_ERR_LIB_DNLD_REQUIRED) {
		lib_dnld = kzalloc(sizeof(*lib_dnld), GFP_KERNEL);
		memcpy(lib_dnld, &resp->lib_dnld, sizeof(*lib_dnld));
	} else
		lib_dnld = NULL;
	if (str_info->ctrl_blk.on == true) {
		str_info->ctrl_blk.on = false;
		str_info->ctrl_blk.data = lib_dnld;
		str_info->ctrl_blk.condition = true;
		str_info->ctrl_blk.ret_code = resp->str_type.result;
		pr_debug("SST DEBUG: sst_alloc_stream_response: waking up.\n");
		wake_up(&sst_drv_ctx->wait_queue);
	}
	return retval;
}


/**
* sst_get_fw_info - Send msg to query for firmware configurations
* @info: out param that holds the firmare configurations
*
* This function is called when the firmware configurations are queiried for
*/
int sst_get_fw_info(struct snd_sst_fw_info *info)
{
	int retval = 0;
	struct ipc_post *msg = NULL;

	pr_debug("SST DBG:sst_get_fw_info called\n");

	if (sst_create_short_msg(&msg)) {
		pr_err("SST ERR: message creation failed\n");
		return -ENOMEM;
	}

	sst_fill_header(&msg->header, IPC_IA_GET_FW_INFO, 0, 0);
	sst_drv_ctx->fw_info_blk.condition = false;
	sst_drv_ctx->fw_info_blk.ret_code = 0;
	sst_drv_ctx->fw_info_blk.on = true;
	sst_drv_ctx->fw_info_blk.data = info;
	spin_lock(&sst_drv_ctx->list_spin_lock);
	list_add_tail(&msg->node, &sst_drv_ctx->ipc_dispatch_list);
	spin_unlock(&sst_drv_ctx->list_spin_lock);
	sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
	retval = sst_wait_interruptible_timeout(sst_drv_ctx,
			&sst_drv_ctx->fw_info_blk, SST_BLOCK_TIMEOUT);
	if (retval) {
		pr_err("SST ERR: error in fw_info = %d\n", retval);
		retval = -EIO;
	}
	return retval;
}


/**
* sst_pause_stream - Send msg for a pausing stream
* @str_id:	 stream ID
*
* This function is called by any function which wants to pause
* an already running stream.
*/
int sst_start_stream(int str_id)
{
	int retval = 0;
	struct ipc_post *msg = NULL;
	struct stream_info *str_info;

	pr_debug("sst_start_stream for %d\n", str_id);
	retval = sst_validate_strid(str_id);
	if (retval)
		return retval;
	str_info = &sst_drv_ctx->streams[str_id];
	if (str_info->status != STREAM_INIT)
		return -EBADRQC;
	if (sst_create_short_msg(&msg))
		return -ENOMEM;

	sst_fill_header(&msg->header, IPC_IA_START_STREAM, 0, str_id);
	spin_lock(&sst_drv_ctx->list_spin_lock);
	list_add_tail(&msg->node, &sst_drv_ctx->ipc_dispatch_list);
	spin_unlock(&sst_drv_ctx->list_spin_lock);
	sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
	return retval;
}

/*
 * sst_pause_stream - Send msg for a pausing stream
 * @str_id:	 stream ID
 *
 * This function is called by any function which wants to pause
 * an already running stream.
 */
int sst_pause_stream(int str_id)
{
	int retval = 0;
	struct ipc_post *msg = NULL;
	struct stream_info *str_info;

	pr_debug("SST DBG:sst_pause_stream for %d\n", str_id);
	retval = sst_validate_strid(str_id);
	if (retval)
		return retval;
	str_info = &sst_drv_ctx->streams[str_id];
	if (str_info->status == STREAM_PAUSED)
		return 0;
	if (str_info->status == STREAM_RUNNING ||
		str_info->status == STREAM_INIT) {
		if (str_info->prev == STREAM_UN_INIT)
			return -EBADRQC;
		if (str_info->ctrl_blk.on == true) {
			pr_err("SST ERR: control path is in use\n");
			return -EINVAL;
		}
		if (sst_create_short_msg(&msg))
			return -ENOMEM;

		sst_fill_header(&msg->header, IPC_IA_PAUSE_STREAM, 0, str_id);
		str_info->ctrl_blk.condition = false;
		str_info->ctrl_blk.ret_code = 0;
		str_info->ctrl_blk.on = true;
		spin_lock(&sst_drv_ctx->list_spin_lock);
		list_add_tail(&msg->node,
				&sst_drv_ctx->ipc_dispatch_list);
		spin_unlock(&sst_drv_ctx->list_spin_lock);
		sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
		retval = sst_wait_interruptible_timeout(sst_drv_ctx,
				&str_info->ctrl_blk, SST_BLOCK_TIMEOUT);
		if (retval == 0) {
			str_info->prev = str_info->status;
			str_info->status = STREAM_PAUSED;
		} else if (retval == SST_ERR_INVALID_STREAM_ID) {
			retval = -EINVAL;
			mutex_lock(&sst_drv_ctx->stream_lock);
			sst_clean_stream(str_info);
			mutex_unlock(&sst_drv_ctx->stream_lock);
		}
	} else {
		retval = -EBADRQC;
		pr_err("SST ERR: BADQRC for stream\n");
	}

	return retval;
}

/**
 * sst_resume_stream - Send msg for resuming stream
 * @str_id:		stream ID
 *
 * This function is called by any function which wants to resume
 * an already paused stream.
 */
int sst_resume_stream(int str_id)
{
	int retval = 0;
	struct ipc_post *msg = NULL;
	struct stream_info *str_info;

	pr_debug("SST DBG:sst_resume_stream for %d\n", str_id);
	retval = sst_validate_strid(str_id);
	if (retval)
		return retval;
	str_info = &sst_drv_ctx->streams[str_id];
	if (str_info->status == STREAM_RUNNING)
			return 0;
	if (str_info->status == STREAM_PAUSED) {
		if (str_info->ctrl_blk.on == true) {
			pr_err("SST ERR: control path in use\n");
			return -EINVAL;
		}
		if (sst_create_short_msg(&msg)) {
			pr_err("SST ERR: mem allocation failed\n");
			return -ENOMEM;
		}
		sst_fill_header(&msg->header, IPC_IA_RESUME_STREAM, 0, str_id);
		str_info->ctrl_blk.condition = false;
		str_info->ctrl_blk.ret_code = 0;
		str_info->ctrl_blk.on = true;
		spin_lock(&sst_drv_ctx->list_spin_lock);
		list_add_tail(&msg->node,
				&sst_drv_ctx->ipc_dispatch_list);
		spin_unlock(&sst_drv_ctx->list_spin_lock);
		sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
		retval = sst_wait_interruptible_timeout(sst_drv_ctx,
				&str_info->ctrl_blk, SST_BLOCK_TIMEOUT);
		if (!retval) {
			if (str_info->prev == STREAM_RUNNING)
				str_info->status = STREAM_RUNNING;
			else
				str_info->status = STREAM_INIT;
			str_info->prev = STREAM_PAUSED;
		} else if (retval == -SST_ERR_INVALID_STREAM_ID) {
			retval = -EINVAL;
			mutex_lock(&sst_drv_ctx->stream_lock);
			sst_clean_stream(str_info);
			mutex_unlock(&sst_drv_ctx->stream_lock);
		}
	} else {
		retval = -EBADRQC;
		pr_err("SST ERR: BADQRC for stream\n");
	}

	return retval;
}


/**
 * sst_drop_stream - Send msg for stopping stream
 * @str_id:		stream ID
 *
 * This function is called by any function which wants to stop
 * a stream.
 */
int sst_drop_stream(int str_id)
{
	int retval = 0;
	struct ipc_post *msg = NULL;
	struct sst_stream_bufs *bufs = NULL, *_bufs;
	struct stream_info *str_info;

	pr_debug("SST DBG:sst_drop_stream for %d\n", str_id);
	retval = sst_validate_strid(str_id);
	if (retval)
		return retval;
	str_info = &sst_drv_ctx->streams[str_id];

	if (str_info->status != STREAM_UN_INIT &&
		str_info->status != STREAM_DECODE) {
		if (str_info->ctrl_blk.on == true) {
			pr_err("SST ERR: control path in use\n");
			return -EINVAL;
		}
		if (sst_create_short_msg(&msg)) {
			pr_err("SST ERR: mem allocation failed\n");
			return -ENOMEM;
		}
		sst_fill_header(&msg->header, IPC_IA_DROP_STREAM, 0, str_id);
		str_info->ctrl_blk.condition = false;
		str_info->ctrl_blk.ret_code = 0;
		str_info->ctrl_blk.on = true;
		spin_lock(&sst_drv_ctx->list_spin_lock);
		list_add_tail(&msg->node,
				&sst_drv_ctx->ipc_dispatch_list);
		spin_unlock(&sst_drv_ctx->list_spin_lock);
		sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
		retval = sst_wait_interruptible_timeout(sst_drv_ctx,
				&str_info->ctrl_blk, SST_BLOCK_TIMEOUT);
		if (!retval) {
			pr_debug("SST DBG:drop success\n");
			str_info->prev = STREAM_UN_INIT;
			str_info->status = STREAM_INIT;
			if (str_info->src != MAD_DRV) {
				mutex_lock(&str_info->lock);
				list_for_each_entry_safe(bufs, _bufs,
							&str_info->bufs, node) {
					list_del(&bufs->node);
					kfree(bufs);
				}
				mutex_unlock(&str_info->lock);
			}
			str_info->cumm_bytes += str_info->curr_bytes;
		} else if (retval == -SST_ERR_INVALID_STREAM_ID) {
			retval = -EINVAL;
			mutex_lock(&sst_drv_ctx->stream_lock);
			sst_clean_stream(str_info);
			mutex_unlock(&sst_drv_ctx->stream_lock);
		}
		if (str_info->data_blk.on == true) {
			str_info->data_blk.condition = true;
			str_info->data_blk.ret_code = retval;
			wake_up(&sst_drv_ctx->wait_queue);
		}
	} else {
		retval = -EBADRQC;
		pr_err("SST ERR: BADQRC for stream\n");
	}
	return retval;
}

/**
* sst_drain_stream - Send msg for draining stream
* @str_id:		stream ID
*
* This function is called by any function which wants to drain
* a stream.
*/
int sst_drain_stream(int str_id)
{
	int retval = 0;
	struct ipc_post *msg = NULL;
	struct stream_info *str_info;

	pr_debug("SST DBG:sst_drain_stream for %d\n", str_id);
	retval = sst_validate_strid(str_id);
	if (retval)
		return retval;
	str_info = &sst_drv_ctx->streams[str_id];

	if (str_info->status != STREAM_RUNNING &&
		str_info->status != STREAM_INIT &&
		str_info->status != STREAM_PAUSED) {
			pr_err("SST ERR: BADQRC for stream = %d\n",
				       str_info->status);
			return -EBADRQC;
	}

	if (str_info->status == STREAM_INIT) {
		if (sst_create_short_msg(&msg)) {
			pr_err("SST ERR: mem allocation failed\n");
			return -ENOMEM;
		}
		sst_fill_header(&msg->header, IPC_IA_DRAIN_STREAM, 0, str_id);
		spin_lock(&sst_drv_ctx->list_spin_lock);
		list_add_tail(&msg->node, &sst_drv_ctx->ipc_dispatch_list);
		spin_unlock(&sst_drv_ctx->list_spin_lock);
		sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
	} else
		str_info->need_draining = true;
	str_info->data_blk.condition = false;
	str_info->data_blk.ret_code = 0;
	str_info->data_blk.on = true;
	retval = sst_wait_interruptible(sst_drv_ctx, &str_info->data_blk);
	str_info->need_draining = false;
	if (retval == -SST_ERR_INVALID_STREAM_ID) {
		retval = -EINVAL;
		sst_clean_stream(str_info);
	}
	return retval;
}

/**
 * sst_free_stream - Frees a stream
 * @str_id:		stream ID
 *
 * This function is called by any function which wants to free
 * a stream.
 */
int sst_free_stream(int str_id)
{
	int retval = 0;
	struct ipc_post *msg = NULL;
	struct stream_info *str_info;

	pr_debug("SST DBG:sst_free_stream for %d\n", str_id);

	retval = sst_validate_strid(str_id);
	if (retval)
		return retval;
	str_info = &sst_drv_ctx->streams[str_id];

	if (str_info->status != STREAM_UN_INIT) {
		if (sst_create_short_msg(&msg)) {
			pr_err("SST ERR: mem allocation failed\n");
			return -ENOMEM;
		}
		sst_fill_header(&msg->header, IPC_IA_FREE_STREAM, 0, str_id);
		spin_lock(&sst_drv_ctx->list_spin_lock);
		list_add_tail(&msg->node, &sst_drv_ctx->ipc_dispatch_list);
		spin_unlock(&sst_drv_ctx->list_spin_lock);
		sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
		str_info->prev =  str_info->status;
		str_info->status = STREAM_UN_INIT;
		if (str_info->data_blk.on == true) {
			str_info->data_blk.condition = true;
			str_info->data_blk.ret_code = 0;
			wake_up(&sst_drv_ctx->wait_queue);
		}
		mutex_lock(&sst_drv_ctx->stream_lock);
		sst_clean_stream(str_info);
		mutex_unlock(&sst_drv_ctx->stream_lock);
		pr_debug("SST DBG:Stream freed\n");
	} else {
		retval = -EBADRQC;
		pr_debug("SST DBG:BADQRC for stream\n");
	}

	return retval;
}


