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
#include <linux/syscalls.h>
#include <linux/firmware.h>
#include <linux/sched.h>
#ifdef CONFIG_MRST_RAR_HANDLER
#include <linux/rar_register.h>
#include "../memrar/memrar.h"
#endif
#include "intel_sst_ioctl.h"
#include "intel_sst.h"
#include "intel_sst_fw_ipc.h"
#include "intel_sst_common.h"
/**
* sst_get_stream_params - Send msg to query for stream parameters
* @str_id:	stream id for which the parameters are queried for
* @get_params:	out parameters to which the parameters are copied to
*
* This function is called when the stream parameters are queiried for
*/
int sst_get_stream_params(int str_id,
			struct snd_sst_get_stream_params *get_params)
{
	int retval = 0;
	struct ipc_post *msg = NULL;
	struct stream_info *str_info;
	struct snd_sst_fw_get_stream_params *fw_params;

	pr_debug("get_stream for %d\n", str_id);
	retval = sst_validate_strid(str_id);
	if (retval)
		return retval;

	str_info = &sst_drv_ctx->streams[str_id];
	if (str_info->status != STREAM_UN_INIT) {
		if (str_info->ctrl_blk.on == true) {
			pr_err("control path in use\n");
			return -EINVAL;
		}
		if (sst_create_short_msg(&msg)) {
			pr_err("message creation failed\n");
			return -ENOMEM;
		}
		fw_params = kzalloc(sizeof(*fw_params), GFP_ATOMIC);
		if (!fw_params) {
			pr_err("mem allocation failed\n");
			kfree(msg);
			return -ENOMEM;
		}

		sst_fill_header(&msg->header, IPC_IA_GET_STREAM_PARAMS,
					0, str_id);
		str_info->ctrl_blk.condition = false;
		str_info->ctrl_blk.ret_code = 0;
		str_info->ctrl_blk.on = true;
		str_info->ctrl_blk.data = (void *) fw_params;
		spin_lock(&sst_drv_ctx->list_spin_lock);
		list_add_tail(&msg->node, &sst_drv_ctx->ipc_dispatch_list);
		spin_unlock(&sst_drv_ctx->list_spin_lock);
		sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
		retval = sst_wait_interruptible_timeout(sst_drv_ctx,
				&str_info->ctrl_blk, SST_BLOCK_TIMEOUT);
		if (retval) {
			get_params->codec_params.result = retval;
			kfree(fw_params);
			return -EIO;
		}
		memcpy(&get_params->pcm_params, &fw_params->pcm_params,
				sizeof(fw_params->pcm_params));
		memcpy(&get_params->codec_params.sparams,
				&fw_params->codec_params,
				sizeof(fw_params->codec_params));
		get_params->codec_params.result = 0;
		get_params->codec_params.stream_id = str_id;
		get_params->codec_params.codec = str_info->codec;
		get_params->codec_params.ops = str_info->ops;
		get_params->codec_params.stream_type = str_info->str_type;
		kfree(fw_params);
	} else {
		pr_debug("Stream is not in the init state\n");
	}
	return retval;
}

/**
 * sst_set_stream_param - Send msg for setting stream parameters
 *
 * @str_id: stream id
 * @str_param: stream params
 *
 * This function sets stream params during runtime
 */
int sst_set_stream_param(int str_id, struct snd_sst_params *str_param)
{
	int retval = 0;
	struct ipc_post *msg = NULL;
	struct stream_info *str_info;

	BUG_ON(!str_param);
	if (sst_drv_ctx->streams[str_id].ops != str_param->ops) {
		pr_err("Invalid operation\n");
		return -EINVAL;
	}
	retval = sst_validate_strid(str_id);
	if (retval)
		return retval;
	pr_debug("set_stream for %d\n", str_id);
	str_info =  &sst_drv_ctx->streams[str_id];
	if (sst_drv_ctx->streams[str_id].status == STREAM_INIT) {
		if (str_info->ctrl_blk.on == true) {
			pr_err("control path in use\n");
			return -EAGAIN;
		}
		if (sst_create_large_msg(&msg))
			return -ENOMEM;

		sst_fill_header(&msg->header,
				IPC_IA_SET_STREAM_PARAMS, 1, str_id);
		str_info->ctrl_blk.condition = false;
		str_info->ctrl_blk.ret_code = 0;
		str_info->ctrl_blk.on = true;
		msg->header.part.data = sizeof(u32) +
				sizeof(str_param->sparams);
		memcpy(msg->mailbox_data, &msg->header, sizeof(u32));
		memcpy(msg->mailbox_data + sizeof(u32), &str_param->sparams,
				sizeof(str_param->sparams));
		spin_lock(&sst_drv_ctx->list_spin_lock);
		list_add_tail(&msg->node, &sst_drv_ctx->ipc_dispatch_list);
		spin_unlock(&sst_drv_ctx->list_spin_lock);
		sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
		retval = sst_wait_interruptible_timeout(sst_drv_ctx,
				&str_info->ctrl_blk, SST_BLOCK_TIMEOUT);
		if (retval < 0) {
			retval = -EIO;
			sst_clean_stream(str_info);
		}
	} else {
		retval = -EBADRQC;
		pr_err("BADQRC for stream\n");
	}
	return retval;
}

/**
* sst_get_vol - This fuction allows to get the premix gain or gain of a stream
*
* @get_vol: this is an output param through which the volume
*	structure is passed back to user
*
* This function is called when the premix gain or stream gain is queried for
*/
int sst_get_vol(struct snd_sst_vol *get_vol)
{
	int retval = 0;
	struct ipc_post *msg = NULL;
	struct snd_sst_vol *fw_get_vol;
	int str_id = get_vol->stream_id;

	pr_debug("get vol called\n");

	if (sst_create_short_msg(&msg))
		return -ENOMEM;

	sst_fill_header(&msg->header,
				IPC_IA_GET_STREAM_VOL, 0, str_id);
	sst_drv_ctx->vol_info_blk.condition = false;
	sst_drv_ctx->vol_info_blk.ret_code = 0;
	sst_drv_ctx->vol_info_blk.on = true;
	fw_get_vol = kzalloc(sizeof(*fw_get_vol), GFP_ATOMIC);
	if (!fw_get_vol) {
		pr_err("mem allocation failed\n");
		kfree(msg);
		return -ENOMEM;
	}
	sst_drv_ctx->vol_info_blk.data = (void *)fw_get_vol;
	spin_lock(&sst_drv_ctx->list_spin_lock);
	list_add_tail(&msg->node, &sst_drv_ctx->ipc_dispatch_list);
	spin_unlock(&sst_drv_ctx->list_spin_lock);
	sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
	retval = sst_wait_interruptible_timeout(sst_drv_ctx,
			&sst_drv_ctx->vol_info_blk, SST_BLOCK_TIMEOUT);
	if (retval)
		retval = -EIO;
	else {
		pr_debug("stream id %d\n", fw_get_vol->stream_id);
		pr_debug("volume %d\n", fw_get_vol->volume);
		pr_debug("ramp duration %d\n", fw_get_vol->ramp_duration);
		pr_debug("ramp_type %d\n", fw_get_vol->ramp_type);
		memcpy(get_vol, fw_get_vol, sizeof(*fw_get_vol));
	}
	return retval;
}

/**
* sst_set_vol - This fuction allows to set the premix gain or gain of a stream
*
* @set_vol:	this holds the volume structure that needs to be set
*
* This function is called when premix gain or stream gain is requested to be set
*/
int sst_set_vol(struct snd_sst_vol *set_vol)
{

	int retval = 0;
	struct ipc_post *msg = NULL;

	pr_debug("set vol called\n");

	if (sst_create_large_msg(&msg)) {
		pr_err("message creation failed\n");
		return -ENOMEM;
	}
	sst_fill_header(&msg->header, IPC_IA_SET_STREAM_VOL, 1,
				set_vol->stream_id);

	msg->header.part.data = sizeof(u32) + sizeof(*set_vol);
	memcpy(msg->mailbox_data, &msg->header, sizeof(u32));
	memcpy(msg->mailbox_data + sizeof(u32), set_vol, sizeof(*set_vol));
	sst_drv_ctx->vol_info_blk.condition = false;
	sst_drv_ctx->vol_info_blk.ret_code = 0;
	sst_drv_ctx->vol_info_blk.on = true;
	sst_drv_ctx->vol_info_blk.data = set_vol;
	spin_lock(&sst_drv_ctx->list_spin_lock);
	list_add_tail(&msg->node, &sst_drv_ctx->ipc_dispatch_list);
	spin_unlock(&sst_drv_ctx->list_spin_lock);
	sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
	retval = sst_wait_interruptible_timeout(sst_drv_ctx,
			&sst_drv_ctx->vol_info_blk, SST_BLOCK_TIMEOUT);
	if (retval) {
		pr_err("error in set_vol = %d\n", retval);
		retval = -EIO;
	}
	return retval;
}

/**
* sst_set_mute - This fuction sets premix mute or soft mute of a stream
*
* @set_mute:	this holds the mute structure that needs to be set
*
* This function is called when premix mute or stream mute requested to be set
*/
int sst_set_mute(struct snd_sst_mute *set_mute)
{

	int retval = 0;
	struct ipc_post *msg = NULL;

	pr_debug("set mute called\n");

	if (sst_create_large_msg(&msg)) {
		pr_err("message creation failed\n");
		return -ENOMEM;
	}
	sst_fill_header(&msg->header, IPC_IA_SET_STREAM_MUTE, 1,
				set_mute->stream_id);
	sst_drv_ctx->mute_info_blk.condition = false;
	sst_drv_ctx->mute_info_blk.ret_code = 0;
	sst_drv_ctx->mute_info_blk.on = true;
	sst_drv_ctx->mute_info_blk.data = set_mute;

	msg->header.part.data = sizeof(u32) + sizeof(*set_mute);
	memcpy(msg->mailbox_data, &msg->header, sizeof(u32));
	memcpy(msg->mailbox_data + sizeof(u32), set_mute,
			sizeof(*set_mute));
	spin_lock(&sst_drv_ctx->list_spin_lock);
	list_add_tail(&msg->node, &sst_drv_ctx->ipc_dispatch_list);
	spin_unlock(&sst_drv_ctx->list_spin_lock);
	sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
	retval = sst_wait_interruptible_timeout(sst_drv_ctx,
			&sst_drv_ctx->mute_info_blk, SST_BLOCK_TIMEOUT);
	if (retval) {
		pr_err("error in set_mute = %d\n", retval);
		retval = -EIO;
	}
	return retval;
}

int sst_prepare_target(struct snd_sst_slot_info *slot)
{
	if (slot->target_device == SND_SST_TARGET_PMIC
		&& slot->device_instance == 1) {
			/*music mode*/
			if (sst_drv_ctx->pmic_port_instance == 0)
				sst_drv_ctx->scard_ops->set_voice_port(
					DEACTIVATE);
	} else if ((slot->target_device == SND_SST_TARGET_PMIC ||
			slot->target_device == SND_SST_TARGET_MODEM) &&
			slot->device_instance == 0) {
				/*voip mode where pcm0 is active*/
				if (sst_drv_ctx->pmic_port_instance == 1)
					sst_drv_ctx->scard_ops->set_audio_port(
						DEACTIVATE);
	}
	return 0;
}

int sst_activate_target(struct snd_sst_slot_info *slot)
{
	if (slot->target_device == SND_SST_TARGET_PMIC &&
		slot->device_instance == 1) {
			/*music mode*/
			sst_drv_ctx->pmic_port_instance = 1;
			sst_drv_ctx->scard_ops->set_audio_port(ACTIVATE);
			sst_drv_ctx->scard_ops->set_pcm_audio_params(
				slot->pcm_params.sfreq,
				slot->pcm_params.pcm_wd_sz,
				slot->pcm_params.num_chan);
			if (sst_drv_ctx->pb_streams)
				sst_drv_ctx->scard_ops->power_up_pmic_pb(1);
			if (sst_drv_ctx->cp_streams)
				sst_drv_ctx->scard_ops->power_up_pmic_cp(1);
	} else if ((slot->target_device == SND_SST_TARGET_PMIC ||
			slot->target_device == SND_SST_TARGET_MODEM) &&
			slot->device_instance == 0) {
				/*voip mode where pcm0 is active*/
				sst_drv_ctx->pmic_port_instance = 0;
				sst_drv_ctx->scard_ops->set_voice_port(
					ACTIVATE);
				sst_drv_ctx->scard_ops->power_up_pmic_pb(0);
				/*sst_drv_ctx->scard_ops->power_up_pmic_cp(0);*/
	}
	return 0;
}

int sst_parse_target(struct snd_sst_slot_info *slot)
{
	int retval = 0;

	if (slot->action == SND_SST_PORT_ACTIVATE &&
		slot->device_type == SND_SST_DEVICE_PCM) {
			retval = sst_activate_target(slot);
			if (retval)
				pr_err("SST_Activate_target_fail\n");
			else
				pr_err("SST_Activate_target_pass\n");
		return retval;
	} else if (slot->action == SND_SST_PORT_PREPARE &&
			slot->device_type == SND_SST_DEVICE_PCM) {
				retval = sst_prepare_target(slot);
			if (retval)
				pr_err("SST_prepare_target_fail\n");
			else
				pr_err("SST_prepare_target_pass\n");
			return retval;
	} else {
		pr_err("slot_action : %d, device_type: %d\n",
				slot->action, slot->device_type);
		return retval;
	}
}

int sst_send_target(struct snd_sst_target_device *target)
{
	int retval;
	struct ipc_post *msg;

	if (sst_create_large_msg(&msg)) {
		pr_err("message creation failed\n");
		return -ENOMEM;
	}
	sst_fill_header(&msg->header, IPC_IA_TARGET_DEV_SELECT, 1, 0);
	sst_drv_ctx->tgt_dev_blk.condition = false;
	sst_drv_ctx->tgt_dev_blk.ret_code = 0;
	sst_drv_ctx->tgt_dev_blk.on = true;

	msg->header.part.data = sizeof(u32) + sizeof(*target);
	memcpy(msg->mailbox_data, &msg->header, sizeof(u32));
	memcpy(msg->mailbox_data + sizeof(u32), target,
			sizeof(*target));
	spin_lock(&sst_drv_ctx->list_spin_lock);
	list_add_tail(&msg->node, &sst_drv_ctx->ipc_dispatch_list);
	spin_unlock(&sst_drv_ctx->list_spin_lock);
	sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
	pr_debug("message sent- waiting\n");
	retval = sst_wait_interruptible_timeout(sst_drv_ctx,
			&sst_drv_ctx->tgt_dev_blk, TARGET_DEV_BLOCK_TIMEOUT);
	if (retval)
		pr_err("target device ipc failed = 0x%x\n", retval);
	return retval;

}

int sst_target_device_validate(struct snd_sst_target_device *target)
{
	int retval = 0;
       int i;

	for (i = 0; i < SST_MAX_TARGET_DEVICES; i++) {
		if (target->devices[i].device_type == SND_SST_DEVICE_PCM) {
			/*pcm device, check params*/
			if (target->devices[i].device_instance == 1) {
				if ((target->devices[i].device_mode !=
				SND_SST_DEV_MODE_PCM_MODE4_I2S) &&
				(target->devices[i].device_mode !=
				SND_SST_DEV_MODE_PCM_MODE4_RIGHT_JUSTIFIED)
				&& (target->devices[i].device_mode !=
				SND_SST_DEV_MODE_PCM_MODE1))
					goto err;
			} else if (target->devices[i].device_instance == 0) {
				if ((target->devices[i].device_mode !=
						SND_SST_DEV_MODE_PCM_MODE2)
					&& (target->devices[i].device_mode !=
						SND_SST_DEV_MODE_PCM_MODE4_I2S)
					&& (target->devices[i].device_mode !=
						SND_SST_DEV_MODE_PCM_MODE1))
					goto err;
				if (target->devices[i].pcm_params.sfreq != 8000
				|| target->devices[i].pcm_params.num_chan != 1
				|| target->devices[i].pcm_params.pcm_wd_sz !=
									16)
					goto err;
			} else {
err:
				pr_err("i/p params incorrect\n");
				return -EINVAL;
			}
		}
	}
    return retval;
}

/**
 * sst_target_device_select - This fuction sets the target device configurations
 *
 * @target: this parameter holds the configurations to be set
 *
 * This function is called when the user layer wants to change the target
 * device's configurations
 */

int sst_target_device_select(struct snd_sst_target_device *target)
{
	int retval, i, prepare_count = 0;

	pr_debug("Target Device Select\n");

	if (target->device_route < 0 || target->device_route > 2) {
		pr_err("device route is invalid\n");
		return -EINVAL;
	}

	if (target->device_route != 0) {
		pr_err("Unsupported config\n");
		return -EIO;
	}
	retval = sst_target_device_validate(target);
	if (retval)
		return retval;

	retval = sst_send_target(target);
	if (retval)
		return retval;
	for (i = 0; i < SST_MAX_TARGET_DEVICES; i++) {
		if (target->devices[i].action == SND_SST_PORT_ACTIVATE) {
			pr_debug("activate called in %d\n", i);
			retval = sst_parse_target(&target->devices[i]);
			if (retval)
				return retval;
		} else if (target->devices[i].action == SND_SST_PORT_PREPARE) {
			pr_debug("PREPARE in %d, Forwarding\n", i);
			retval = sst_parse_target(&target->devices[i]);
			if (retval) {
				pr_err("Parse Target fail %d\n", retval);
				return retval;
			}
			pr_debug("Parse Target successful %d\n", retval);
			if (target->devices[i].device_type ==
						SND_SST_DEVICE_PCM)
				prepare_count++;
		}
	}
	if (target->devices[0].action == SND_SST_PORT_PREPARE &&
		prepare_count == 0)
			sst_drv_ctx->scard_ops->power_down_pmic();

	return retval;
}
#ifdef CONFIG_MRST_RAR_HANDLER
/*This function gets the physical address of the secure memory from the handle*/
static inline int sst_get_RAR(struct RAR_buffer *buffers, int count)
{
	int retval = 0, rar_status = 0;

	rar_status = rar_handle_to_bus(buffers, count);

	if (count != rar_status) {
		pr_err("The rar CALL Failed");
		retval = -EIO;
	}
	if (buffers->info.type != RAR_TYPE_AUDIO) {
		pr_err("Invalid RAR type\n");
		return -EINVAL;
	}
	return retval;
}

#endif

/* This function creates the scatter gather list to be sent to firmware to
capture/playback data*/
static int sst_create_sg_list(struct stream_info *stream,
		struct sst_frame_info *sg_list)
{
	struct sst_stream_bufs *kbufs = NULL;
#ifdef CONFIG_MRST_RAR_HANDLER
	struct RAR_buffer rar_buffers;
	int retval = 0;
#endif
	int i = 0;
	list_for_each_entry(kbufs, &stream->bufs, node) {
		if (kbufs->in_use == false) {
#ifdef CONFIG_MRST_RAR_HANDLER
			if (stream->ops == STREAM_OPS_PLAYBACK_DRM) {
				pr_debug("DRM playback handling\n");
				rar_buffers.info.handle = (__u32)kbufs->addr;
				rar_buffers.info.size = kbufs->size;
				pr_debug("rar handle 0x%x size=0x%x\n",
					rar_buffers.info.handle,
					rar_buffers.info.size);
				retval =  sst_get_RAR(&rar_buffers, 1);

				if (retval)
					return retval;
				sg_list->addr[i].addr = rar_buffers.bus_address;
				/* rar_buffers.info.size; */
				sg_list->addr[i].size = (__u32)kbufs->size;
				pr_debug("phyaddr[%d] 0x%x Size:0x%x\n"
					, i, sg_list->addr[i].addr,
					sg_list->addr[i].size);
			}
#endif
			if (stream->ops != STREAM_OPS_PLAYBACK_DRM) {
				sg_list->addr[i].addr =
					virt_to_phys((void *)
						kbufs->addr + kbufs->offset);
				sg_list->addr[i].size = kbufs->size;
				pr_debug("phyaddr[%d]:0x%x Size:0x%x\n"
				, i , sg_list->addr[i].addr, kbufs->size);
			}
			stream->curr_bytes += sg_list->addr[i].size;
			kbufs->in_use = true;
			i++;
		}
		if (i >= MAX_NUM_SCATTER_BUFFERS)
			break;
	}

	sg_list->num_entries = i;
	pr_debug("sg list entries = %d\n", sg_list->num_entries);
	return i;
}


/**
 * sst_play_frame - Send msg for sending stream frames
 *
 * @str_id:	ID of stream
 *
 * This function is called to send data to be played out
 * to the firmware
 */
int sst_play_frame(int str_id)
{
	int i = 0, retval = 0;
	struct ipc_post *msg = NULL;
	struct sst_frame_info sg_list = {0};
	struct sst_stream_bufs *kbufs = NULL, *_kbufs;
	struct stream_info *stream;

	pr_debug("play frame for %d\n", str_id);
	retval = sst_validate_strid(str_id);
	if (retval)
		return retval;

	stream = &sst_drv_ctx->streams[str_id];
	/* clear prev sent buffers */
	list_for_each_entry_safe(kbufs, _kbufs, &stream->bufs, node) {
		if (kbufs->in_use == true) {
			spin_lock(&stream->pcm_lock);
			list_del(&kbufs->node);
			spin_unlock(&stream->pcm_lock);
			kfree(kbufs);
		}
	}
	/* update bytes sent */
	stream->cumm_bytes += stream->curr_bytes;
	stream->curr_bytes = 0;
	if (list_empty(&stream->bufs)) {
		/* no user buffer available */
		pr_debug("Null buffer stream status %d\n", stream->status);
		stream->prev = stream->status;
		stream->status = STREAM_INIT;
		pr_debug("new stream status = %d\n", stream->status);
		if (stream->need_draining == true) {
			pr_debug("draining stream\n");
			if (sst_create_short_msg(&msg)) {
				pr_err("mem allocation failed\n");
				return -ENOMEM;
			}
			sst_fill_header(&msg->header, IPC_IA_DRAIN_STREAM,
						0, str_id);
			spin_lock(&sst_drv_ctx->list_spin_lock);
			list_add_tail(&msg->node,
					&sst_drv_ctx->ipc_dispatch_list);
			spin_unlock(&sst_drv_ctx->list_spin_lock);
			sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
		} else if (stream->data_blk.on == true) {
			pr_debug("user list empty.. wake\n");
			/* unblock */
			stream->data_blk.ret_code = 0;
			stream->data_blk.condition = true;
			stream->data_blk.on = false;
			wake_up(&sst_drv_ctx->wait_queue);
		}
		return 0;
	}

	/* create list */
	i = sst_create_sg_list(stream, &sg_list);

	/* post msg */
	if (sst_create_large_msg(&msg))
		return -ENOMEM;

	sst_fill_header(&msg->header, IPC_IA_PLAY_FRAMES, 1, str_id);
	msg->header.part.data = sizeof(u32) + sizeof(sg_list);
	memcpy(msg->mailbox_data, &msg->header, sizeof(u32));
	memcpy(msg->mailbox_data + sizeof(u32), &sg_list, sizeof(sg_list));
	spin_lock(&sst_drv_ctx->list_spin_lock);
	list_add_tail(&msg->node, &sst_drv_ctx->ipc_dispatch_list);
	spin_unlock(&sst_drv_ctx->list_spin_lock);
	sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
	return 0;

}

/**
 * sst_capture_frame - Send msg for sending stream frames
 *
 * @str_id:	ID of stream
 *
 * This function is called to capture data from the firmware
 */
int sst_capture_frame(int str_id)
{
	int i = 0, retval = 0;
	struct ipc_post *msg = NULL;
	struct sst_frame_info sg_list = {0};
	struct sst_stream_bufs *kbufs = NULL, *_kbufs;
	struct stream_info *stream;


	pr_debug("capture frame for %d\n", str_id);
	retval = sst_validate_strid(str_id);
	if (retval)
		return retval;
	stream = &sst_drv_ctx->streams[str_id];
	/* clear prev sent buffers */
	list_for_each_entry_safe(kbufs, _kbufs, &stream->bufs, node) {
		if (kbufs->in_use == true) {
			list_del(&kbufs->node);
			kfree(kbufs);
			pr_debug("del node\n");
		}
	}
	if (list_empty(&stream->bufs)) {
		/* no user buffer available */
		pr_debug("Null buffer!!!!stream status %d\n",
			       stream->status);
		stream->prev = stream->status;
		stream->status = STREAM_INIT;
		pr_debug("new stream status = %d\n",
			       stream->status);
		if (stream->data_blk.on == true) {
			pr_debug("user list empty.. wake\n");
			/* unblock */
			stream->data_blk.ret_code = 0;
			stream->data_blk.condition = true;
			stream->data_blk.on = false;
			wake_up(&sst_drv_ctx->wait_queue);

		}
		return 0;
	}
	/* create new sg list */
	i = sst_create_sg_list(stream, &sg_list);

	/* post msg */
	if (sst_create_large_msg(&msg))
		return -ENOMEM;

	sst_fill_header(&msg->header, IPC_IA_CAPT_FRAMES, 1, str_id);
	msg->header.part.data = sizeof(u32) + sizeof(sg_list);
	memcpy(msg->mailbox_data, &msg->header, sizeof(u32));
	memcpy(msg->mailbox_data + sizeof(u32), &sg_list, sizeof(sg_list));
	spin_lock(&sst_drv_ctx->list_spin_lock);
	list_add_tail(&msg->node, &sst_drv_ctx->ipc_dispatch_list);
	spin_unlock(&sst_drv_ctx->list_spin_lock);
	sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);


	/*update bytes recevied*/
	stream->cumm_bytes += stream->curr_bytes;
	stream->curr_bytes = 0;

    pr_debug("Cum bytes  = %d\n", stream->cumm_bytes);
	return 0;
}

/*This function is used to calculate the minimum size of input buffers given*/
static unsigned int calculate_min_size(struct snd_sst_buffs *bufs)
{
	int i, min_val = bufs->buff_entry[0].size;
	for (i = 1 ; i < bufs->entries; i++) {
		if (bufs->buff_entry[i].size < min_val)
			min_val = bufs->buff_entry[i].size;
	}
	pr_debug("min_val = %d\n", min_val);
	return min_val;
}

static unsigned int calculate_max_size(struct snd_sst_buffs *bufs)
{
	int i, max_val = bufs->buff_entry[0].size;
	for (i = 1 ; i < bufs->entries; i++) {
		if (bufs->buff_entry[i].size > max_val)
			max_val = bufs->buff_entry[i].size;
	}
	pr_debug("max_val = %d\n", max_val);
	return max_val;
}

/*This function is used to allocate input and output buffers to be sent to
the firmware that will take encoded data and return decoded data*/
static int sst_allocate_decode_buf(struct stream_info *str_info,
				struct snd_sst_dbufs *dbufs,
				unsigned int cum_input_given,
				unsigned int cum_output_given)
{
#ifdef CONFIG_MRST_RAR_HANDLER
	if (str_info->ops == STREAM_OPS_PLAYBACK_DRM) {

		if (dbufs->ibufs->type == SST_BUF_RAR  &&
			dbufs->obufs->type == SST_BUF_RAR) {
			if (dbufs->ibufs->entries == dbufs->obufs->entries)
				return 0;
			else {
				pr_err("RAR entries dont match\n");
				 return -EINVAL;
			}
		} else
			str_info->decode_osize = cum_output_given;
		return 0;

	}
#endif
	if (!str_info->decode_ibuf) {
		pr_debug("no i/p buffers, trying full size\n");
		str_info->decode_isize = cum_input_given;
		str_info->decode_ibuf = kzalloc(str_info->decode_isize,
						GFP_KERNEL);
		str_info->idecode_alloc = str_info->decode_isize;
	}
	if (!str_info->decode_ibuf) {
		pr_debug("buff alloc failed, try max size\n");
		str_info->decode_isize = calculate_max_size(dbufs->ibufs);
		str_info->decode_ibuf = kzalloc(
				str_info->decode_isize, GFP_KERNEL);
		str_info->idecode_alloc = str_info->decode_isize;
	}
	if (!str_info->decode_ibuf) {
		pr_debug("buff alloc failed, try min size\n");
		str_info->decode_isize = calculate_min_size(dbufs->ibufs);
		str_info->decode_ibuf = kzalloc(str_info->decode_isize,
						GFP_KERNEL);
		if (!str_info->decode_ibuf) {
			pr_err("mem allocation failed\n");
			return -ENOMEM;
		}
		str_info->idecode_alloc = str_info->decode_isize;
	}
	str_info->decode_osize = cum_output_given;
	if (str_info->decode_osize > sst_drv_ctx->mmap_len)
		str_info->decode_osize = sst_drv_ctx->mmap_len;
	return 0;
}

/*This function is used to send the message to firmware to decode the data*/
static int sst_send_decode_mess(int str_id, struct stream_info *str_info,
				struct snd_sst_decode_info *dec_info)
{
	struct ipc_post *msg = NULL;
	int retval = 0;

	pr_debug("SST DBG:sst_set_mute:called\n");

	if (str_info->decode_ibuf_type == SST_BUF_RAR) {
#ifdef CONFIG_MRST_RAR_HANDLER
			dec_info->frames_in.addr[0].addr =
				(unsigned long)str_info->decode_ibuf;
			dec_info->frames_in.addr[0].size =
							str_info->decode_isize;
#endif

	} else {
		dec_info->frames_in.addr[0].addr = virt_to_phys((void *)
					str_info->decode_ibuf);
		dec_info->frames_in.addr[0].size = str_info->decode_isize;
	}


	if (str_info->decode_obuf_type == SST_BUF_RAR) {
#ifdef CONFIG_MRST_RAR_HANDLER
		dec_info->frames_out.addr[0].addr =
				(unsigned long)str_info->decode_obuf;
		dec_info->frames_out.addr[0].size = str_info->decode_osize;
#endif

	} else {
		dec_info->frames_out.addr[0].addr = virt_to_phys((void *)
						str_info->decode_obuf) ;
		dec_info->frames_out.addr[0].size = str_info->decode_osize;
	}

	dec_info->frames_in.num_entries = 1;
	dec_info->frames_out.num_entries = 1;
	dec_info->frames_in.rsrvd = 0;
	dec_info->frames_out.rsrvd = 0;
	dec_info->input_bytes_consumed = 0;
	dec_info->output_bytes_produced = 0;
	if (sst_create_large_msg(&msg)) {
		pr_err("message creation failed\n");
		return -ENOMEM;
	}

	sst_fill_header(&msg->header, IPC_IA_DECODE_FRAMES, 1, str_id);
	msg->header.part.data = sizeof(u32) + sizeof(*dec_info);
	memcpy(msg->mailbox_data, &msg->header, sizeof(u32));
	memcpy(msg->mailbox_data + sizeof(u32), dec_info,
			sizeof(*dec_info));
	spin_lock(&sst_drv_ctx->list_spin_lock);
	list_add_tail(&msg->node, &sst_drv_ctx->ipc_dispatch_list);
	spin_unlock(&sst_drv_ctx->list_spin_lock);
	str_info->data_blk.condition = false;
	str_info->data_blk.ret_code = 0;
	str_info->data_blk.on = true;
	str_info->data_blk.data = dec_info;
	sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
	retval = sst_wait_interruptible(sst_drv_ctx, &str_info->data_blk);
	return retval;
}

#ifdef CONFIG_MRST_RAR_HANDLER
static int sst_prepare_input_buffers_rar(struct stream_info *str_info,
			struct snd_sst_dbufs *dbufs,
			int *input_index, int *in_copied,
			int *input_index_valid_size, int *new_entry_flag)
{
	int retval = 0;
	int i;

	if (str_info->ops == STREAM_OPS_PLAYBACK_DRM) {
		struct RAR_buffer rar_buffers;
		__u32 info;
		retval = copy_from_user((void *) &info,
				dbufs->ibufs->buff_entry[i].buffer,
				sizeof(__u32));
		if (retval) {
			pr_err("cpy from user fail\n");
			return -EAGAIN;
		}
		rar_buffers.info.type = dbufs->ibufs->type;
		rar_buffers.info.size = dbufs->ibufs->buff_entry[i].size;
		rar_buffers.info.handle =  info;
		pr_debug("rar in DnR(input buffer function)=0x%x size=0x%x",
				rar_buffers.info.handle,
				rar_buffers.info.size);
		retval =  sst_get_RAR(&rar_buffers, 1);
		if (retval) {
			pr_debug("SST ERR: RAR API failed\n");
			return retval;
		}
		str_info->decode_ibuf =
		(void *) ((unsigned long) rar_buffers.bus_address);
		pr_debug("RAR buf addr in DnR (input buffer function)0x%lu",
				 (unsigned long) str_info->decode_ibuf);
		pr_debug("rar in DnR decode funtion/output b_add rar =0x%lu",
				(unsigned long) rar_buffers.bus_address);
		*input_index = i + 1;
		str_info->decode_isize = dbufs->ibufs->buff_entry[i].size;
		str_info->decode_ibuf_type = dbufs->ibufs->type;
		*in_copied = str_info->decode_isize;
	}
	return retval;
}
#endif

/*This function is used to prepare the kernel input buffers with contents
before sending for decode*/
static int sst_prepare_input_buffers(struct stream_info *str_info,
			struct snd_sst_dbufs *dbufs,
			int *input_index, int *in_copied,
			int *input_index_valid_size, int *new_entry_flag)
{
	int i, cpy_size, retval = 0;

	pr_debug("input_index = %d, input entries = %d\n",
			 *input_index, dbufs->ibufs->entries);
	for (i = *input_index; i < dbufs->ibufs->entries; i++) {
#ifdef CONFIG_MRST_RAR_HANDLER
		retval = sst_prepare_input_buffers_rar(str_info,
			dbufs, input_index, in_copied,
				input_index_valid_size, new_entry_flag);
		if (retval) {
			pr_err("In prepare input buffers for RAR\n");
			return -EIO;
		}
#endif
		*input_index = i;
		if (*input_index_valid_size == 0)
			*input_index_valid_size =
				dbufs->ibufs->buff_entry[i].size;
		pr_debug("inout addr = %p, size = %d\n",
			dbufs->ibufs->buff_entry[i].buffer,
			*input_index_valid_size);
		pr_debug("decode_isize = %d, in_copied %d\n",
			str_info->decode_isize, *in_copied);
		if (*input_index_valid_size <=
					(str_info->decode_isize - *in_copied))
			cpy_size = *input_index_valid_size;
		else
			cpy_size = str_info->decode_isize - *in_copied;

		pr_debug("cpy size = %d\n", cpy_size);
		if (!dbufs->ibufs->buff_entry[i].buffer) {
			pr_err("i/p buffer is null\n");
			return -EINVAL;
		}
		pr_debug("Try copy To %p, From %p, size %d\n",
				str_info->decode_ibuf + *in_copied,
				dbufs->ibufs->buff_entry[i].buffer, cpy_size);

		retval =
		copy_from_user((void *)(str_info->decode_ibuf + *in_copied),
				(void *) dbufs->ibufs->buff_entry[i].buffer,
				cpy_size);
		if (retval) {
			pr_err("copy from user failed\n");
			return -EIO;
		}
		*in_copied += cpy_size;
		*input_index_valid_size -= cpy_size;
		pr_debug("in buff size = %d, in_copied = %d\n",
			*input_index_valid_size, *in_copied);
		if (*input_index_valid_size != 0) {
			pr_debug("more input buffers left\n");
			dbufs->ibufs->buff_entry[i].buffer += cpy_size;
			break;
		}
		if (*in_copied == str_info->decode_isize &&
			*input_index_valid_size == 0 &&
			(i+1) <= dbufs->ibufs->entries) {
			pr_debug("all input buffers copied\n");
			*new_entry_flag = true;
			*input_index = i + 1;
			break;
		}
	}
	return retval;
}

/* This function is used to copy the decoded data from kernel buffers to
the user output buffers with contents after decode*/
static int sst_prepare_output_buffers(struct stream_info *str_info,
			struct snd_sst_dbufs *dbufs,
			int *output_index, int output_size,
			int *out_copied)

{
	int i, cpy_size, retval = 0;
	pr_debug("output_index = %d, output entries = %d\n",
				*output_index,
				dbufs->obufs->entries);
	for (i = *output_index; i < dbufs->obufs->entries; i++) {
		*output_index = i;
		pr_debug("output addr = %p, size = %d\n",
			dbufs->obufs->buff_entry[i].buffer,
			dbufs->obufs->buff_entry[i].size);
		pr_debug("output_size = %d, out_copied = %d\n",
				output_size, *out_copied);
		if (dbufs->obufs->buff_entry[i].size <
				(output_size - *out_copied))
			cpy_size = dbufs->obufs->buff_entry[i].size;
		else
			cpy_size = output_size - *out_copied;
		pr_debug("cpy size = %d\n", cpy_size);
		pr_debug("Try copy To: %p, From %p, size %d\n",
				dbufs->obufs->buff_entry[i].buffer,
				sst_drv_ctx->mmap_mem + *out_copied,
				cpy_size);
		retval = copy_to_user(dbufs->obufs->buff_entry[i].buffer,
					sst_drv_ctx->mmap_mem + *out_copied,
					cpy_size);
		if (retval) {
			pr_err("copy to user failed\n");
			return -EIO;
		} else
			pr_debug("copy to user passed\n");
		*out_copied += cpy_size;
		dbufs->obufs->buff_entry[i].size -= cpy_size;
		pr_debug("o/p buff size %d, out_copied %d\n",
			dbufs->obufs->buff_entry[i].size, *out_copied);
		if (dbufs->obufs->buff_entry[i].size != 0) {
			*output_index = i;
			dbufs->obufs->buff_entry[i].buffer += cpy_size;
			break;
		} else if (*out_copied == output_size) {
			*output_index = i + 1;
			break;
		}
	}
	return retval;
}

/**
 * sst_decode - Send msg for decoding frames
 *
 * @str_id: ID of stream
 * @dbufs: param that holds the user input and output buffers and size
 *
 * This function is called to decode data from the firmware
 */
int sst_decode(int str_id, struct snd_sst_dbufs *dbufs)
{
	int retval = 0, i;
	unsigned long long total_input = 0 , total_output = 0;
	unsigned int cum_input_given = 0 , cum_output_given = 0;
	int copy_in_done = false, copy_out_done = false;
	int input_index = 0, output_index = 0;
	int input_index_valid_size = 0;
	int in_copied, out_copied;
	int new_entry_flag;
	u64 output_size;
	struct stream_info *str_info;
	struct snd_sst_decode_info dec_info;
	unsigned long long input_bytes, output_bytes;

	sst_drv_ctx->scard_ops->power_down_pmic();
	pr_debug("Powering_down_PMIC...\n");

	retval = sst_validate_strid(str_id);
	if (retval)
		return retval;

	str_info = &sst_drv_ctx->streams[str_id];
	if (str_info->status != STREAM_INIT) {
		pr_err("invalid stream state = %d\n",
			       str_info->status);
		return -EINVAL;
	}

	str_info->prev = str_info->status;
	str_info->status = STREAM_DECODE;

	for (i = 0; i < dbufs->ibufs->entries; i++)
		cum_input_given += dbufs->ibufs->buff_entry[i].size;
	for (i = 0; i < dbufs->obufs->entries; i++)
		cum_output_given += dbufs->obufs->buff_entry[i].size;

	/* input and output buffer allocation */
	retval =  sst_allocate_decode_buf(str_info, dbufs,
				cum_input_given, cum_output_given);
	if (retval) {
		pr_err("mem allocation failed, abort!!!\n");
		retval = -ENOMEM;
		goto finish;
	}

	str_info->decode_isize = str_info->idecode_alloc;
	str_info->decode_ibuf_type = dbufs->ibufs->type;
	str_info->decode_obuf_type = dbufs->obufs->type;

	while ((copy_out_done == false) && (copy_in_done == false)) {
		in_copied = 0;
		new_entry_flag = false;
		retval = sst_prepare_input_buffers(str_info,\
			dbufs, &input_index, &in_copied,
			&input_index_valid_size, &new_entry_flag);
		if (retval) {
			pr_err("prepare in buffers failed\n");
			goto finish;
		}

		if (str_info->ops != STREAM_OPS_PLAYBACK_DRM)
			str_info->decode_obuf = sst_drv_ctx->mmap_mem;

#ifdef CONFIG_MRST_RAR_HANDLER
		else {
			if (dbufs->obufs->type == SST_BUF_RAR) {
				struct RAR_buffer rar_buffers;
				__u32 info;

				pr_debug("DRM");
				retval = copy_from_user((void *) &info,
						dbufs->obufs->
						buff_entry[output_index].buffer,
						sizeof(__u32));

				rar_buffers.info.size = dbufs->obufs->
					buff_entry[output_index].size;
				rar_buffers.info.handle =  info;
				retval =  sst_get_RAR(&rar_buffers, 1);
				if (retval)
					return retval;

				str_info->decode_obuf = (void *)((unsigned long)
						rar_buffers.bus_address);
				str_info->decode_osize = dbufs->obufs->
					buff_entry[output_index].size;
				str_info->decode_obuf_type = dbufs->obufs->type;
				pr_debug("DRM handling\n");
				pr_debug("o/p_add=0x%lu Size=0x%x\n",
					(unsigned long) str_info->decode_obuf,
					str_info->decode_osize);
			} else {
				str_info->decode_obuf = sst_drv_ctx->mmap_mem;
				str_info->decode_osize = dbufs->obufs->
					buff_entry[output_index].size;

			}
		}
#endif
		if (str_info->ops != STREAM_OPS_PLAYBACK_DRM) {
			if (str_info->decode_isize > in_copied) {
				str_info->decode_isize = in_copied;
				pr_debug("i/p size = %d\n",
						str_info->decode_isize);
			}
		}


		retval = sst_send_decode_mess(str_id, str_info, &dec_info);
		if (retval || dec_info.input_bytes_consumed == 0) {
			pr_err("SST ERR: mess failed or no input consumed\n");
			goto finish;
		}
		input_bytes = dec_info.input_bytes_consumed;
		output_bytes = dec_info.output_bytes_produced;

		pr_debug("in_copied=%d, con=%lld, prod=%lld\n",
			in_copied, input_bytes, output_bytes);
		if (dbufs->obufs->type == SST_BUF_RAR) {
			output_index += 1;
			if (output_index == dbufs->obufs->entries) {
				copy_in_done = true;
				pr_debug("all i/p cpy done\n");
			}
			total_output += output_bytes;
		} else {
			out_copied = 0;
			output_size = output_bytes;
			retval = sst_prepare_output_buffers(str_info, dbufs,
				&output_index, output_size, &out_copied);
			if (retval) {
				pr_err("prep out buff fail\n");
				goto finish;
			}
			if (str_info->ops != STREAM_OPS_PLAYBACK_DRM) {
				if (in_copied != input_bytes) {
					int bytes_left = in_copied -
								input_bytes;
					pr_debug("bytes %d\n",
							bytes_left);
					if (new_entry_flag == true)
						input_index--;
					while (bytes_left) {
						struct snd_sst_buffs *ibufs;
						struct snd_sst_buff_entry
								*buff_entry;
						unsigned int size_sent;

						ibufs = dbufs->ibufs;
						buff_entry =
						&ibufs->buff_entry[input_index];
						size_sent = buff_entry->size -\
							input_index_valid_size;
						if (bytes_left == size_sent) {
								bytes_left = 0;
						} else if (bytes_left <
								size_sent) {
							buff_entry->buffer +=
							 (size_sent -
								bytes_left);
							buff_entry->size -=
							 (size_sent -
								bytes_left);
							bytes_left = 0;
						} else {
							bytes_left -= size_sent;
							input_index--;
							input_index_valid_size =
									0;
						}
					}

				}
			}

			total_output += out_copied;
			if (str_info->decode_osize != out_copied) {
				str_info->decode_osize -= out_copied;
				pr_debug("output size modified = %d\n",
						str_info->decode_osize);
			}
		}
		total_input += input_bytes;

		if (str_info->ops == STREAM_OPS_PLAYBACK_DRM) {
			if (total_input == cum_input_given)
				copy_in_done = true;
			copy_out_done = true;

		} else {
			if (total_output == cum_output_given) {
				copy_out_done = true;
				pr_debug("all o/p cpy done\n");
			}

			if (total_input == cum_input_given) {
				copy_in_done = true;
				pr_debug("all i/p cpy done\n");
			}
		}

		pr_debug("copy_out = %d, copy_in = %d\n",
				copy_out_done, copy_in_done);
	}

finish:
	dbufs->input_bytes_consumed = total_input;
	dbufs->output_bytes_produced = total_output;
	str_info->status = str_info->prev;
	str_info->prev = STREAM_DECODE;
	kfree(str_info->decode_ibuf);
	str_info->decode_ibuf = NULL;
	return retval;
}
