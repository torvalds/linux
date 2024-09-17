// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 NXP
 */

#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include "vpu.h"
#include "vpu_core.h"
#include "vpu_rpc.h"
#include "vpu_mbox.h"
#include "vpu_defs.h"
#include "vpu_cmds.h"
#include "vpu_msgs.h"
#include "vpu_v4l2.h"

#define VPU_PKT_HEADER_LENGTH		3

struct vpu_msg_handler {
	u32 id;
	void (*done)(struct vpu_inst *inst, struct vpu_rpc_event *pkt);
	u32 is_str;
};

static void vpu_session_handle_start_done(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	vpu_trace(inst->dev, "[%d]\n", inst->id);
}

static void vpu_session_handle_mem_request(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	struct vpu_pkt_mem_req_data req_data = { 0 };

	vpu_iface_unpack_msg_data(inst->core, pkt, (void *)&req_data);
	vpu_trace(inst->dev, "[%d] %d:%d %d:%d %d:%d\n",
		  inst->id,
		  req_data.enc_frame_size,
		  req_data.enc_frame_num,
		  req_data.ref_frame_size,
		  req_data.ref_frame_num,
		  req_data.act_buf_size,
		  req_data.act_buf_num);
	vpu_inst_lock(inst);
	call_void_vop(inst, mem_request,
		      req_data.enc_frame_size,
		      req_data.enc_frame_num,
		      req_data.ref_frame_size,
		      req_data.ref_frame_num,
		      req_data.act_buf_size,
		      req_data.act_buf_num);
	vpu_inst_unlock(inst);
}

static void vpu_session_handle_stop_done(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	vpu_trace(inst->dev, "[%d]\n", inst->id);

	call_void_vop(inst, stop_done);
}

static void vpu_session_handle_seq_hdr(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	struct vpu_dec_codec_info info;
	const struct vpu_core_resources *res;

	memset(&info, 0, sizeof(info));
	res = vpu_get_resource(inst);
	info.stride = res ? res->stride : 1;
	vpu_iface_unpack_msg_data(inst->core, pkt, (void *)&info);
	call_void_vop(inst, event_notify, VPU_MSG_ID_SEQ_HDR_FOUND, &info);
}

static void vpu_session_handle_resolution_change(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	call_void_vop(inst, event_notify, VPU_MSG_ID_RES_CHANGE, NULL);
}

static void vpu_session_handle_enc_frame_done(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	struct vpu_enc_pic_info info = { 0 };

	vpu_iface_unpack_msg_data(inst->core, pkt, (void *)&info);
	dev_dbg(inst->dev, "[%d] frame id = %d, wptr = 0x%x, size = %d\n",
		inst->id, info.frame_id, info.wptr, info.frame_size);
	call_void_vop(inst, get_one_frame, &info);
}

static void vpu_session_handle_frame_request(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	struct vpu_fs_info fs = { 0 };

	vpu_iface_unpack_msg_data(inst->core, pkt, &fs);
	call_void_vop(inst, event_notify, VPU_MSG_ID_FRAME_REQ, &fs);
}

static void vpu_session_handle_frame_release(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	if (inst->core->type == VPU_CORE_TYPE_ENC) {
		struct vpu_frame_info info;

		memset(&info, 0, sizeof(info));
		vpu_iface_unpack_msg_data(inst->core, pkt, (void *)&info.sequence);
		dev_dbg(inst->dev, "[%d] %d\n", inst->id, info.sequence);
		info.type = inst->out_format.type;
		call_void_vop(inst, buf_done, &info);
	} else if (inst->core->type == VPU_CORE_TYPE_DEC) {
		struct vpu_fs_info fs = { 0 };

		vpu_iface_unpack_msg_data(inst->core, pkt, &fs);
		call_void_vop(inst, event_notify, VPU_MSG_ID_FRAME_RELEASE, &fs);
	}
}

static void vpu_session_handle_input_done(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	dev_dbg(inst->dev, "[%d]\n", inst->id);
	call_void_vop(inst, input_done);
}

static void vpu_session_handle_pic_decoded(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	struct vpu_dec_pic_info info = { 0 };

	vpu_iface_unpack_msg_data(inst->core, pkt, (void *)&info);
	call_void_vop(inst, get_one_frame, &info);
}

static void vpu_session_handle_pic_done(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	struct vpu_dec_pic_info info = { 0 };
	struct vpu_frame_info frame;

	memset(&frame, 0, sizeof(frame));
	vpu_iface_unpack_msg_data(inst->core, pkt, (void *)&info);
	if (inst->core->type == VPU_CORE_TYPE_DEC)
		frame.type = inst->cap_format.type;
	frame.id = info.id;
	frame.luma = info.luma;
	frame.skipped = info.skipped;
	frame.timestamp = info.timestamp;

	call_void_vop(inst, buf_done, &frame);
}

static void vpu_session_handle_eos(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	call_void_vop(inst, event_notify, VPU_MSG_ID_PIC_EOS, NULL);
}

static void vpu_session_handle_error(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	char *str = (char *)pkt->data;

	if (*str)
		dev_err(inst->dev, "instance %d firmware error : %s\n", inst->id, str);
	else
		dev_err(inst->dev, "instance %d is unsupported stream\n", inst->id);
	call_void_vop(inst, event_notify, VPU_MSG_ID_UNSUPPORTED, NULL);
	vpu_v4l2_set_error(inst);
}

static void vpu_session_handle_firmware_xcpt(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	char *str = (char *)pkt->data;

	dev_err(inst->dev, "%s firmware xcpt: %s\n",
		vpu_core_type_desc(inst->core->type), str);
	call_void_vop(inst, event_notify, VPU_MSG_ID_FIRMWARE_XCPT, NULL);
	set_bit(inst->id, &inst->core->hang_mask);
	vpu_v4l2_set_error(inst);
}

static void vpu_session_handle_pic_skipped(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	vpu_inst_lock(inst);
	vpu_skip_frame(inst, 1);
	vpu_inst_unlock(inst);
}

static void vpu_session_handle_dbg_msg(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	char *str = (char *)pkt->data;

	if (*str)
		dev_info(inst->dev, "instance %d firmware dbg msg : %s\n", inst->id, str);
}

static void vpu_terminate_string_msg(struct vpu_rpc_event *pkt)
{
	if (pkt->hdr.num == ARRAY_SIZE(pkt->data))
		pkt->hdr.num--;
	pkt->data[pkt->hdr.num] = 0;
}

static struct vpu_msg_handler handlers[] = {
	{VPU_MSG_ID_START_DONE, vpu_session_handle_start_done},
	{VPU_MSG_ID_STOP_DONE, vpu_session_handle_stop_done},
	{VPU_MSG_ID_MEM_REQUEST, vpu_session_handle_mem_request},
	{VPU_MSG_ID_SEQ_HDR_FOUND, vpu_session_handle_seq_hdr},
	{VPU_MSG_ID_RES_CHANGE, vpu_session_handle_resolution_change},
	{VPU_MSG_ID_FRAME_INPUT_DONE, vpu_session_handle_input_done},
	{VPU_MSG_ID_FRAME_REQ, vpu_session_handle_frame_request},
	{VPU_MSG_ID_FRAME_RELEASE, vpu_session_handle_frame_release},
	{VPU_MSG_ID_ENC_DONE, vpu_session_handle_enc_frame_done},
	{VPU_MSG_ID_PIC_DECODED, vpu_session_handle_pic_decoded},
	{VPU_MSG_ID_DEC_DONE, vpu_session_handle_pic_done},
	{VPU_MSG_ID_PIC_EOS, vpu_session_handle_eos},
	{VPU_MSG_ID_UNSUPPORTED, vpu_session_handle_error, true},
	{VPU_MSG_ID_FIRMWARE_XCPT, vpu_session_handle_firmware_xcpt, true},
	{VPU_MSG_ID_PIC_SKIPPED, vpu_session_handle_pic_skipped},
	{VPU_MSG_ID_DBG_MSG, vpu_session_handle_dbg_msg, true},
};

static int vpu_session_handle_msg(struct vpu_inst *inst, struct vpu_rpc_event *msg)
{
	int ret;
	u32 msg_id;
	struct vpu_msg_handler *handler = NULL;
	unsigned int i;

	ret = vpu_iface_convert_msg_id(inst->core, msg->hdr.id);
	if (ret < 0)
		return -EINVAL;

	msg_id = ret;
	dev_dbg(inst->dev, "[%d] receive event(%s)\n", inst->id, vpu_id_name(msg_id));

	for (i = 0; i < ARRAY_SIZE(handlers); i++) {
		if (handlers[i].id == msg_id) {
			handler = &handlers[i];
			break;
		}
	}

	if (handler) {
		if (handler->is_str)
			vpu_terminate_string_msg(msg);
		if (handler->done)
			handler->done(inst, msg);
	}

	vpu_response_cmd(inst, msg_id, 1);

	return 0;
}

static bool vpu_inst_receive_msg(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	unsigned long bytes = sizeof(struct vpu_rpc_event_header);
	u32 ret;

	memset(pkt, 0, sizeof(*pkt));
	if (kfifo_len(&inst->msg_fifo) < bytes)
		return false;

	ret = kfifo_out(&inst->msg_fifo, pkt, bytes);
	if (ret != bytes)
		return false;

	if (pkt->hdr.num > 0) {
		bytes = pkt->hdr.num * sizeof(u32);
		ret = kfifo_out(&inst->msg_fifo, pkt->data, bytes);
		if (ret != bytes)
			return false;
	}

	return true;
}

void vpu_inst_run_work(struct work_struct *work)
{
	struct vpu_inst *inst = container_of(work, struct vpu_inst, msg_work);
	struct vpu_rpc_event pkt;

	while (vpu_inst_receive_msg(inst, &pkt))
		vpu_session_handle_msg(inst, &pkt);
}

static void vpu_inst_handle_msg(struct vpu_inst *inst, struct vpu_rpc_event *pkt)
{
	unsigned long bytes;
	u32 id = pkt->hdr.id;
	int ret;

	if (!inst->workqueue)
		return;

	bytes = sizeof(pkt->hdr) + pkt->hdr.num * sizeof(u32);
	ret = kfifo_in(&inst->msg_fifo, pkt, bytes);
	if (ret != bytes)
		dev_err(inst->dev, "[%d:%d]overflow: %d\n", inst->core->id, inst->id, id);
	queue_work(inst->workqueue, &inst->msg_work);
}

static int vpu_handle_msg(struct vpu_core *core)
{
	struct vpu_rpc_event pkt;
	struct vpu_inst *inst;
	int ret;

	memset(&pkt, 0, sizeof(pkt));
	while (!vpu_iface_receive_msg(core, &pkt)) {
		dev_dbg(core->dev, "event index = %d, id = %d, num = %d\n",
			pkt.hdr.index, pkt.hdr.id, pkt.hdr.num);

		ret = vpu_iface_convert_msg_id(core, pkt.hdr.id);
		if (ret < 0)
			continue;

		inst = vpu_core_find_instance(core, pkt.hdr.index);
		if (inst) {
			vpu_response_cmd(inst, ret, 0);
			mutex_lock(&core->cmd_lock);
			vpu_inst_record_flow(inst, ret);
			mutex_unlock(&core->cmd_lock);

			vpu_inst_handle_msg(inst, &pkt);
			vpu_inst_put(inst);
		}
		memset(&pkt, 0, sizeof(pkt));
	}

	return 0;
}

static int vpu_isr_thread(struct vpu_core *core, u32 irq_code)
{
	dev_dbg(core->dev, "irq code = 0x%x\n", irq_code);
	switch (irq_code) {
	case VPU_IRQ_CODE_SYNC:
		vpu_mbox_send_msg(core, PRC_BUF_OFFSET, core->rpc.phys - core->fw.phys);
		vpu_mbox_send_msg(core, BOOT_ADDRESS, core->fw.phys);
		vpu_mbox_send_msg(core, INIT_DONE, 2);
		break;
	case VPU_IRQ_CODE_BOOT_DONE:
		break;
	case VPU_IRQ_CODE_SNAPSHOT_DONE:
		break;
	default:
		vpu_handle_msg(core);
		break;
	}

	return 0;
}

static void vpu_core_run_msg_work(struct vpu_core *core)
{
	const unsigned int SIZE = sizeof(u32);

	while (kfifo_len(&core->msg_fifo) >= SIZE) {
		u32 data = 0;

		if (kfifo_out(&core->msg_fifo, &data, SIZE) == SIZE)
			vpu_isr_thread(core, data);
	}
}

void vpu_msg_run_work(struct work_struct *work)
{
	struct vpu_core *core = container_of(work, struct vpu_core, msg_work);
	unsigned long delay = msecs_to_jiffies(10);

	vpu_core_run_msg_work(core);
	queue_delayed_work(core->workqueue, &core->msg_delayed_work, delay);
}

void vpu_msg_delayed_work(struct work_struct *work)
{
	struct vpu_core *core;
	struct delayed_work *dwork;
	unsigned long bytes = sizeof(u32);
	u32 i;

	if (!work)
		return;

	dwork = to_delayed_work(work);
	core = container_of(dwork, struct vpu_core, msg_delayed_work);
	if (kfifo_len(&core->msg_fifo) >= bytes)
		vpu_core_run_msg_work(core);

	bytes = sizeof(struct vpu_rpc_event_header);
	for (i = 0; i < core->supported_instance_count; i++) {
		struct vpu_inst *inst = vpu_core_find_instance(core, i);

		if (!inst)
			continue;

		if (inst->workqueue && kfifo_len(&inst->msg_fifo) >= bytes)
			queue_work(inst->workqueue, &inst->msg_work);

		vpu_inst_put(inst);
	}
}

int vpu_isr(struct vpu_core *core, u32 irq)
{
	switch (irq) {
	case VPU_IRQ_CODE_SYNC:
		break;
	case VPU_IRQ_CODE_BOOT_DONE:
		complete(&core->cmp);
		break;
	case VPU_IRQ_CODE_SNAPSHOT_DONE:
		complete(&core->cmp);
		break;
	default:
		break;
	}

	if (kfifo_in(&core->msg_fifo, &irq, sizeof(irq)) != sizeof(irq))
		dev_err(core->dev, "[%d]overflow: %d\n", core->id, irq);
	queue_work(core->workqueue, &core->msg_work);

	return 0;
}
