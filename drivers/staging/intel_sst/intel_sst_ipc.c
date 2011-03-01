/*
 *  intel_sst_ipc.c - Intel SST Driver for audio engine
 *
 *  Copyright (C) 2008-10 Intel Corporation
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
 *  This file defines all ipc functions
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/sched.h>
#include "intel_sst.h"
#include "intel_sst_ioctl.h"
#include "intel_sst_fw_ipc.h"
#include "intel_sst_common.h"

/*
 * sst_send_sound_card_type - send sound card type
 *
 * this function sends the sound card type to sst dsp engine
 */
static void sst_send_sound_card_type(void)
{
	struct ipc_post *msg = NULL;

	if (sst_create_short_msg(&msg))
		return;

	sst_fill_header(&msg->header, IPC_IA_SET_PMIC_TYPE, 0, 0);
	msg->header.part.data = sst_drv_ctx->pmic_vendor;
	spin_lock(&sst_drv_ctx->list_spin_lock);
	list_add_tail(&msg->node, &sst_drv_ctx->ipc_dispatch_list);
	spin_unlock(&sst_drv_ctx->list_spin_lock);
	sst_post_message(&sst_drv_ctx->ipc_post_msg_wq);
	return;
}

/**
* sst_post_message - Posts message to SST
*
* @work: Pointer to work structure
*
* This function is called by any component in driver which
* wants to send an IPC message. This will post message only if
* busy bit is free
*/
void sst_post_message(struct work_struct *work)
{
	struct ipc_post *msg;
	union ipc_header header;
	union  interrupt_reg imr;
	int retval = 0;
	imr.full = 0;

	/*To check if LPE is in stalled state.*/
	retval = sst_stalled();
	if (retval < 0) {
		pr_err("in stalled state\n");
		return;
	}
	pr_debug("post message called\n");
	spin_lock(&sst_drv_ctx->list_spin_lock);

	/* check list */
	if (list_empty(&sst_drv_ctx->ipc_dispatch_list)) {
		/* list is empty, mask imr */
		pr_debug("Empty msg queue... masking\n");
		imr.full = readl(sst_drv_ctx->shim + SST_IMRX);
		imr.part.done_interrupt = 1;
		/* dummy register for shim workaround */
		sst_shim_write(sst_drv_ctx->shim, SST_IMRX, imr.full);
		spin_unlock(&sst_drv_ctx->list_spin_lock);
		return;
	}

	/* check busy bit */
	header.full = sst_shim_read(sst_drv_ctx->shim, SST_IPCX);
	if (header.part.busy) {
		/* busy, unmask */
		pr_debug("Busy not free... unmasking\n");
		imr.full = readl(sst_drv_ctx->shim + SST_IMRX);
		imr.part.done_interrupt = 0;
		/* dummy register for shim workaround */
		sst_shim_write(sst_drv_ctx->shim, SST_IMRX, imr.full);
		spin_unlock(&sst_drv_ctx->list_spin_lock);
		return;
	}
	/* copy msg from list */
	msg = list_entry(sst_drv_ctx->ipc_dispatch_list.next,
			struct ipc_post, node);
	list_del(&msg->node);
	pr_debug("Post message: header = %x\n", msg->header.full);
	pr_debug("size: = %x\n", msg->header.part.data);
	if (msg->header.part.large)
		memcpy_toio(sst_drv_ctx->mailbox + SST_MAILBOX_SEND,
			msg->mailbox_data, msg->header.part.data);
	/* dummy register for shim workaround */

	sst_shim_write(sst_drv_ctx->shim, SST_IPCX, msg->header.full);
	spin_unlock(&sst_drv_ctx->list_spin_lock);

	kfree(msg->mailbox_data);
	kfree(msg);
	return;
}

/*
 * sst_clear_interrupt - clear the SST FW interrupt
 *
 * This function clears the interrupt register after the interrupt
 * bottom half is complete allowing next interrupt to arrive
 */
void sst_clear_interrupt(void)
{
	union interrupt_reg isr;
	union interrupt_reg imr;
	union ipc_header clear_ipc;

	imr.full = sst_shim_read(sst_drv_ctx->shim, SST_IMRX);
	isr.full = sst_shim_read(sst_drv_ctx->shim, SST_ISRX);
	/*  write 1 to clear  */;
	isr.part.busy_interrupt = 1;
	sst_shim_write(sst_drv_ctx->shim, SST_ISRX, isr.full);
	/* Set IA done bit */
	clear_ipc.full = sst_shim_read(sst_drv_ctx->shim, SST_IPCD);
	clear_ipc.part.busy = 0;
	clear_ipc.part.done = 1;
	clear_ipc.part.data = IPC_ACK_SUCCESS;
	sst_shim_write(sst_drv_ctx->shim, SST_IPCD, clear_ipc.full);
	/* un mask busy interrupt */
	imr.part.busy_interrupt = 0;
	sst_shim_write(sst_drv_ctx->shim, SST_IMRX, imr.full);
}

/*
 * process_fw_init - process the FW init msg
 *
 * @msg: IPC message from FW
 *
 * This function processes the FW init msg from FW
 * marks FW state and prints debug info of loaded FW
 */
int process_fw_init(struct sst_ipc_msg_wq *msg)
{
	struct ipc_header_fw_init *init =
		(struct ipc_header_fw_init *)msg->mailbox;
	int retval = 0;

	pr_debug("*** FW Init msg came***\n");
	if (init->result) {
		mutex_lock(&sst_drv_ctx->sst_lock);
		sst_drv_ctx->sst_state = SST_ERROR;
		mutex_unlock(&sst_drv_ctx->sst_lock);
		pr_debug("FW Init failed, Error %x\n", init->result);
		pr_err("FW Init failed, Error %x\n", init->result);
		retval = -init->result;
		return retval;
	}
	if (sst_drv_ctx->pci_id == SST_MRST_PCI_ID)
		sst_send_sound_card_type();
	mutex_lock(&sst_drv_ctx->sst_lock);
	sst_drv_ctx->sst_state = SST_FW_RUNNING;
	sst_drv_ctx->lpe_stalled = 0;
	mutex_unlock(&sst_drv_ctx->sst_lock);
	pr_debug("FW Version %x.%x\n",
			init->fw_version.major, init->fw_version.minor);
	pr_debug("Build No %x Type %x\n",
			init->fw_version.build, init->fw_version.type);
	pr_debug(" Build date %s Time %s\n",
			init->build_info.date, init->build_info.time);
	sst_wake_up_alloc_block(sst_drv_ctx, FW_DWNL_ID, retval, NULL);
	return retval;
}
/**
* sst_process_message - Processes message from SST
*
* @work:	Pointer to work structure
*
* This function is scheduled by ISR
* It take a msg from process_queue and does action based on msg
*/
void sst_process_message(struct work_struct *work)
{
	struct sst_ipc_msg_wq *msg =
			container_of(work, struct sst_ipc_msg_wq, wq);
	int str_id = msg->header.part.str_id;

	pr_debug("IPC process for %x\n", msg->header.full);

	/* based on msg in list call respective handler */
	switch (msg->header.part.msg_id) {
	case IPC_SST_BUF_UNDER_RUN:
	case IPC_SST_BUF_OVER_RUN:
		if (sst_validate_strid(str_id)) {
			pr_err("stream id %d invalid\n", str_id);
			break;
		}
		pr_err("Buffer under/overrun for %d\n",
				msg->header.part.str_id);
		pr_err("Got Underrun & not to send data...ignore\n");
		break;

	case IPC_SST_GET_PLAY_FRAMES:
		if (sst_drv_ctx->pci_id == SST_MRST_PCI_ID) {
			struct stream_info *stream ;

			if (sst_validate_strid(str_id)) {
				pr_err("strid %d invalid\n", str_id);
				break;
			}
			/* call sst_play_frame */
			stream = &sst_drv_ctx->streams[str_id];
			pr_debug("sst_play_frames for %d\n",
					msg->header.part.str_id);
			mutex_lock(&sst_drv_ctx->streams[str_id].lock);
			sst_play_frame(msg->header.part.str_id);
			mutex_unlock(&sst_drv_ctx->streams[str_id].lock);
			break;
		} else
			pr_err("sst_play_frames for Penwell!!\n");

	case IPC_SST_GET_CAPT_FRAMES:
		if (sst_drv_ctx->pci_id == SST_MRST_PCI_ID) {
			struct stream_info *stream;
			/* call sst_capture_frame */
			if (sst_validate_strid(str_id)) {
				pr_err("str id %d invalid\n", str_id);
				break;
			}
			stream = &sst_drv_ctx->streams[str_id];
			pr_debug("sst_capture_frames for %d\n",
					msg->header.part.str_id);
			mutex_lock(&stream->lock);
			if (stream->mmapped == false &&
					stream->src == SST_DRV) {
				pr_debug("waking up block for copy.\n");
				stream->data_blk.ret_code = 0;
				stream->data_blk.condition = true;
				stream->data_blk.on = false;
				wake_up(&sst_drv_ctx->wait_queue);
			} else
				sst_capture_frame(msg->header.part.str_id);
			mutex_unlock(&stream->lock);
		} else
			pr_err("sst_play_frames for Penwell!!\n");
		break;

	case IPC_IA_PRINT_STRING:
		pr_debug("been asked to print something by fw\n");
		/* TBD */
		break;

	case IPC_IA_FW_INIT_CMPLT: {
		/* send next data to FW */
		process_fw_init(msg);
		break;
	}

	case IPC_SST_STREAM_PROCESS_FATAL_ERR:
		if (sst_validate_strid(str_id)) {
			pr_err("stream id %d invalid\n", str_id);
			break;
		}
		pr_err("codec fatal error %x stream %d...\n",
				msg->header.full, msg->header.part.str_id);
		pr_err("Dropping the stream\n");
		sst_drop_stream(msg->header.part.str_id);
		break;
	case IPC_IA_LPE_GETTING_STALLED:
		sst_drv_ctx->lpe_stalled = 1;
		break;
	case IPC_IA_LPE_UNSTALLED:
		sst_drv_ctx->lpe_stalled = 0;
		break;
	default:
		/* Illegal case */
		pr_err("Unhandled msg %x header %x\n",
		msg->header.part.msg_id, msg->header.full);
	}
	sst_clear_interrupt();
	return;
}

/**
* sst_process_reply - Processes reply message from SST
*
* @work:	Pointer to work structure
*
* This function is scheduled by ISR
* It take a reply msg from response_queue and
* does action based on msg
*/
void sst_process_reply(struct work_struct *work)
{
	struct sst_ipc_msg_wq *msg =
			container_of(work, struct sst_ipc_msg_wq, wq);

	int str_id = msg->header.part.str_id;
	struct stream_info *str_info;

	switch (msg->header.part.msg_id) {
	case IPC_IA_TARGET_DEV_SELECT:
		if (!msg->header.part.data) {
			sst_drv_ctx->tgt_dev_blk.ret_code = 0;
		} else {
			pr_err(" Msg %x reply error %x\n",
			msg->header.part.msg_id, msg->header.part.data);
			sst_drv_ctx->tgt_dev_blk.ret_code =
					-msg->header.part.data;
		}

		if (sst_drv_ctx->tgt_dev_blk.on == true) {
				sst_drv_ctx->tgt_dev_blk.condition = true;
				wake_up(&sst_drv_ctx->wait_queue);
		}
		break;
	case IPC_IA_ALG_PARAMS: {
		pr_debug("sst:IPC_ALG_PARAMS response %x\n", msg->header.full);
		pr_debug("sst: data value %x\n", msg->header.part.data);
		pr_debug("sst: large value %x\n", msg->header.part.large);

		if (!msg->header.part.large) {
			if (!msg->header.part.data) {
				pr_debug("sst: alg set success\n");
				sst_drv_ctx->ppp_params_blk.ret_code = 0;
			} else {
				pr_debug("sst: alg set failed\n");
				sst_drv_ctx->ppp_params_blk.ret_code =
							-msg->header.part.data;
			}

		} else if (msg->header.part.data) {
			struct snd_ppp_params *mailbox_params, *get_params;
			char *params;

			pr_debug("sst: alg get success\n");
			mailbox_params = (struct snd_ppp_params *)msg->mailbox;
			get_params = kzalloc(sizeof(*get_params), GFP_KERNEL);
			if (get_params == NULL) {
				pr_err("sst: out of memory for ALG PARAMS");
				break;
			}
			memcpy_fromio(get_params, mailbox_params,
							sizeof(*get_params));
			get_params->params = kzalloc(mailbox_params->size,
							GFP_KERNEL);
			if (get_params->params == NULL) {
				kfree(get_params);
				pr_err("sst: out of memory for ALG PARAMS block");
				break;
			}
			params = msg->mailbox;
			params = params + sizeof(*mailbox_params) - sizeof(u32);
			memcpy_fromio(get_params->params, params,
							get_params->size);
			sst_drv_ctx->ppp_params_blk.ret_code = 0;
			sst_drv_ctx->ppp_params_blk.data = get_params;
		}

		if (sst_drv_ctx->ppp_params_blk.on == true) {
			sst_drv_ctx->ppp_params_blk.condition = true;
			wake_up(&sst_drv_ctx->wait_queue);
		}
		break;
	}
	case IPC_IA_GET_FW_INFO: {
		struct snd_sst_fw_info *fw_info =
			(struct snd_sst_fw_info *)msg->mailbox;
		if (msg->header.part.large) {
			int major = fw_info->fw_version.major;
			int minor = fw_info->fw_version.minor;
			int build = fw_info->fw_version.build;
			pr_debug("Msg succeeded %x\n",
				       msg->header.part.msg_id);
			pr_debug("INFO: ***FW*** = %02d.%02d.%02d\n",
					major, minor, build);
			memcpy_fromio(sst_drv_ctx->fw_info_blk.data,
				((struct snd_sst_fw_info *)(msg->mailbox)),
				sizeof(struct snd_sst_fw_info));
			sst_drv_ctx->fw_info_blk.ret_code = 0;
		} else {
			pr_err(" Msg %x reply error %x\n",
			msg->header.part.msg_id, msg->header.part.data);
			sst_drv_ctx->fw_info_blk.ret_code =
					-msg->header.part.data;
		}
		if (sst_drv_ctx->fw_info_blk.on == true) {
			pr_debug("Memcopy succeeded\n");
			sst_drv_ctx->fw_info_blk.on = false;
			sst_drv_ctx->fw_info_blk.condition = true;
			wake_up(&sst_drv_ctx->wait_queue);
		}
		break;
	}
	case IPC_IA_SET_STREAM_MUTE:
		if (!msg->header.part.data) {
			pr_debug("Msg succeeded %x\n",
				       msg->header.part.msg_id);
			sst_drv_ctx->mute_info_blk.ret_code = 0;
		} else {
			pr_err(" Msg %x reply error %x\n",
			msg->header.part.msg_id, msg->header.part.data);
			sst_drv_ctx->mute_info_blk.ret_code =
					-msg->header.part.data;

		}
		if (sst_drv_ctx->mute_info_blk.on == true) {
			sst_drv_ctx->mute_info_blk.on = false;
			sst_drv_ctx->mute_info_blk.condition = true;
			wake_up(&sst_drv_ctx->wait_queue);
		}
		break;
	case IPC_IA_SET_STREAM_VOL:
		if (!msg->header.part.data) {
			pr_debug("Msg succeeded %x\n",
				       msg->header.part.msg_id);
			sst_drv_ctx->vol_info_blk.ret_code = 0;
		} else {
			pr_err(" Msg %x reply error %x\n",
					msg->header.part.msg_id,
			msg->header.part.data);
			sst_drv_ctx->vol_info_blk.ret_code =
					-msg->header.part.data;

		}

		if (sst_drv_ctx->vol_info_blk.on == true) {
			sst_drv_ctx->vol_info_blk.on = false;
			sst_drv_ctx->vol_info_blk.condition = true;
			wake_up(&sst_drv_ctx->wait_queue);
		}
		break;
	case IPC_IA_GET_STREAM_VOL:
		if (msg->header.part.large) {
			pr_debug("Large Msg Received Successfully\n");
			pr_debug("Msg succeeded %x\n",
				       msg->header.part.msg_id);
			memcpy_fromio(sst_drv_ctx->vol_info_blk.data,
				(void *) msg->mailbox,
				sizeof(struct snd_sst_vol));
			sst_drv_ctx->vol_info_blk.ret_code = 0;
		} else {
			pr_err("Msg %x reply error %x\n",
			msg->header.part.msg_id, msg->header.part.data);
			sst_drv_ctx->vol_info_blk.ret_code =
					-msg->header.part.data;
		}
		if (sst_drv_ctx->vol_info_blk.on == true) {
			sst_drv_ctx->vol_info_blk.on = false;
			sst_drv_ctx->vol_info_blk.condition = true;
			wake_up(&sst_drv_ctx->wait_queue);
		}
		break;

	case IPC_IA_GET_STREAM_PARAMS:
		if (sst_validate_strid(str_id)) {
			pr_err("stream id %d invalid\n", str_id);
			break;
		}
		str_info = &sst_drv_ctx->streams[str_id];
		if (msg->header.part.large) {
			pr_debug("Get stream large success\n");
			memcpy_fromio(str_info->ctrl_blk.data,
				((void *)(msg->mailbox)),
				sizeof(struct snd_sst_fw_get_stream_params));
			str_info->ctrl_blk.ret_code = 0;
		} else {
			pr_err("Msg %x reply error %x\n",
				msg->header.part.msg_id, msg->header.part.data);
			str_info->ctrl_blk.ret_code = -msg->header.part.data;
		}
		if (str_info->ctrl_blk.on == true) {
			str_info->ctrl_blk.on = false;
			str_info->ctrl_blk.condition = true;
			wake_up(&sst_drv_ctx->wait_queue);
		}
		break;
	case IPC_IA_DECODE_FRAMES:
		if (sst_validate_strid(str_id)) {
			pr_err("stream id %d invalid\n", str_id);
			break;
		}
		str_info = &sst_drv_ctx->streams[str_id];
		if (msg->header.part.large) {
			pr_debug("Msg succeeded %x\n",
				       msg->header.part.msg_id);
			memcpy_fromio(str_info->data_blk.data,
					((void *)(msg->mailbox)),
					sizeof(struct snd_sst_decode_info));
			str_info->data_blk.ret_code = 0;
		} else {
			pr_err("Msg %x reply error %x\n",
				msg->header.part.msg_id, msg->header.part.data);
			str_info->data_blk.ret_code = -msg->header.part.data;
		}
		if (str_info->data_blk.on == true) {
			str_info->data_blk.on = false;
			str_info->data_blk.condition = true;
			wake_up(&sst_drv_ctx->wait_queue);
		}
		break;
	case IPC_IA_DRAIN_STREAM:
		if (sst_validate_strid(str_id)) {
			pr_err("stream id %d invalid\n", str_id);
			break;
		}
		str_info = &sst_drv_ctx->streams[str_id];
		if (!msg->header.part.data) {
			pr_debug("Msg succeeded %x\n",
					msg->header.part.msg_id);
			str_info->ctrl_blk.ret_code = 0;

		} else {
			pr_err(" Msg %x reply error %x\n",
				msg->header.part.msg_id, msg->header.part.data);
			str_info->ctrl_blk.ret_code = -msg->header.part.data;

		}
		str_info = &sst_drv_ctx->streams[str_id];
		if (str_info->data_blk.on == true) {
			str_info->data_blk.on = false;
			str_info->data_blk.condition = true;
			wake_up(&sst_drv_ctx->wait_queue);
		}
		break;

	case IPC_IA_DROP_STREAM:
		if (sst_validate_strid(str_id)) {
			pr_err("str id %d invalid\n", str_id);
			break;
		}
		str_info = &sst_drv_ctx->streams[str_id];
		if (msg->header.part.large) {
			struct snd_sst_drop_response *drop_resp =
				(struct snd_sst_drop_response *)msg->mailbox;

			pr_debug("Drop ret bytes %x\n", drop_resp->bytes);

			str_info->curr_bytes = drop_resp->bytes;
			str_info->ctrl_blk.ret_code =  0;
		} else {
			pr_err(" Msg %x reply error %x\n",
				msg->header.part.msg_id, msg->header.part.data);
			str_info->ctrl_blk.ret_code = -msg->header.part.data;
		}
		if (str_info->ctrl_blk.on == true) {
			str_info->ctrl_blk.on = false;
			str_info->ctrl_blk.condition = true;
			wake_up(&sst_drv_ctx->wait_queue);
		}
		break;
	case IPC_IA_ENABLE_RX_TIME_SLOT:
		if (!msg->header.part.data) {
			pr_debug("RX_TIME_SLOT success\n");
			sst_drv_ctx->hs_info_blk.ret_code = 0;
		} else {
			pr_err(" Msg %x reply error %x\n",
				msg->header.part.msg_id,
				msg->header.part.data);
			sst_drv_ctx->hs_info_blk.ret_code =
				-msg->header.part.data;
		}
		if (sst_drv_ctx->hs_info_blk.on == true) {
			sst_drv_ctx->hs_info_blk.on = false;
			sst_drv_ctx->hs_info_blk.condition = true;
			wake_up(&sst_drv_ctx->wait_queue);
		}
		break;
	case IPC_IA_PAUSE_STREAM:
	case IPC_IA_RESUME_STREAM:
	case IPC_IA_SET_STREAM_PARAMS:
		str_info = &sst_drv_ctx->streams[str_id];
		if (!msg->header.part.data) {
			pr_debug("Msg succeeded %x\n",
					msg->header.part.msg_id);
			str_info->ctrl_blk.ret_code = 0;
		} else {
			pr_err(" Msg %x reply error %x\n",
					msg->header.part.msg_id,
					msg->header.part.data);
			str_info->ctrl_blk.ret_code = -msg->header.part.data;
		}
		if (sst_validate_strid(str_id)) {
			pr_err(" stream id %d invalid\n", str_id);
			break;
		}

		if (str_info->ctrl_blk.on == true) {
			str_info->ctrl_blk.on = false;
			str_info->ctrl_blk.condition = true;
			wake_up(&sst_drv_ctx->wait_queue);
		}
		break;

	case IPC_IA_FREE_STREAM:
		if (!msg->header.part.data) {
			pr_debug("Stream %d freed\n", str_id);
		} else {
			pr_err("Free for %d ret error %x\n",
				       str_id, msg->header.part.data);
		}
		break;
	case IPC_IA_ALLOC_STREAM: {
		/* map to stream, call play */
		struct snd_sst_alloc_response *resp =
				(struct snd_sst_alloc_response *)msg->mailbox;
		if (resp->str_type.result)
			pr_err("error alloc stream = %x\n",
				       resp->str_type.result);
		sst_alloc_stream_response(str_id, resp);
		break;
	}

	case IPC_IA_PLAY_FRAMES:
	case IPC_IA_CAPT_FRAMES:
		if (sst_validate_strid(str_id)) {
			pr_err("stream id %d invalid\n", str_id);
			break;
		}
		pr_debug("Ack for play/capt frames received\n");
		break;

	case IPC_IA_PREP_LIB_DNLD: {
		struct snd_sst_str_type *str_type =
			(struct snd_sst_str_type *)msg->mailbox;
		pr_debug("Prep Lib download %x\n",
				msg->header.part.msg_id);
		if (str_type->result)
			pr_err("Prep lib download %x\n", str_type->result);
		else
			pr_debug("Can download codec now...\n");
		sst_wake_up_alloc_block(sst_drv_ctx, str_id,
				str_type->result, NULL);
		break;
	}

	case IPC_IA_LIB_DNLD_CMPLT: {
		struct snd_sst_lib_download_info *resp =
			(struct snd_sst_lib_download_info *)msg->mailbox;
		int retval = resp->result;

		pr_debug("Lib downloaded %x\n", msg->header.part.msg_id);
		if (resp->result) {
			pr_err("err in lib dload %x\n", resp->result);
		} else {
			pr_debug("Codec download complete...\n");
			pr_debug("codec Type %d Ver %d Built %s: %s\n",
				resp->dload_lib.lib_info.lib_type,
				resp->dload_lib.lib_info.lib_version,
				resp->dload_lib.lib_info.b_date,
				resp->dload_lib.lib_info.b_time);
		}
		sst_wake_up_alloc_block(sst_drv_ctx, str_id,
						retval, NULL);
		break;
	}

	case IPC_IA_GET_FW_VERSION: {
		struct ipc_header_fw_init *version =
				(struct ipc_header_fw_init *)msg->mailbox;
		int major = version->fw_version.major;
		int minor = version->fw_version.minor;
		int build = version->fw_version.build;
		dev_info(&sst_drv_ctx->pci->dev,
			"INFO: ***LOADED SST FW VERSION*** = %02d.%02d.%02d\n",
		major, minor, build);
		break;
	}
	case IPC_IA_GET_FW_BUILD_INF: {
		struct sst_fw_build_info *build =
			(struct sst_fw_build_info *)msg->mailbox;
		pr_debug("Build date:%sTime:%s", build->date, build->time);
		break;
	}
	case IPC_IA_SET_PMIC_TYPE:
		break;
	case IPC_IA_START_STREAM:
		pr_debug("reply for START STREAM %x\n", msg->header.full);
		break;
	default:
		/* Illegal case */
		pr_err("process reply:default = %x\n", msg->header.full);
	}
	sst_clear_interrupt();
	return;
}
