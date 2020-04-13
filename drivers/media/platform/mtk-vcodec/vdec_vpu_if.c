// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: PC Chen <pc.chen@mediatek.com>
 */

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_util.h"
#include "vdec_ipi_msg.h"
#include "vdec_vpu_if.h"

static void handle_init_ack_msg(const struct vdec_vpu_ipi_init_ack *msg)
{
	struct vdec_vpu_inst *vpu = (struct vdec_vpu_inst *)
					(unsigned long)msg->ap_inst_addr;

	mtk_vcodec_debug(vpu, "+ ap_inst_addr = 0x%llx", msg->ap_inst_addr);

	/* mapping VPU address to kernel virtual address */
	/* the content in vsi is initialized to 0 in VPU */
	vpu->vsi = vpu_mapping_dm_addr(vpu->dev, msg->vpu_inst_addr);
	vpu->inst_addr = msg->vpu_inst_addr;

	mtk_vcodec_debug(vpu, "- vpu_inst_addr = 0x%x", vpu->inst_addr);
}

/*
 * vpu_dec_ipi_handler - Handler for VPU ipi message.
 *
 * @data: ipi message
 * @len : length of ipi message
 * @priv: callback private data which is passed by decoder when register.
 *
 * This function runs in interrupt context and it means there's an IPI MSG
 * from VPU.
 */
static void vpu_dec_ipi_handler(const void *data, unsigned int len, void *priv)
{
	const struct vdec_vpu_ipi_ack *msg = data;
	struct vdec_vpu_inst *vpu = (struct vdec_vpu_inst *)
					(unsigned long)msg->ap_inst_addr;

	mtk_vcodec_debug(vpu, "+ id=%X", msg->msg_id);

	if (msg->status == 0) {
		switch (msg->msg_id) {
		case VPU_IPIMSG_DEC_INIT_ACK:
			handle_init_ack_msg(data);
			break;

		case VPU_IPIMSG_DEC_START_ACK:
		case VPU_IPIMSG_DEC_END_ACK:
		case VPU_IPIMSG_DEC_DEINIT_ACK:
		case VPU_IPIMSG_DEC_RESET_ACK:
			break;

		default:
			mtk_vcodec_err(vpu, "invalid msg=%X", msg->msg_id);
			break;
		}
	}

	mtk_vcodec_debug(vpu, "- id=%X", msg->msg_id);
	vpu->failure = msg->status;
	vpu->signaled = 1;
}

static int vcodec_vpu_send_msg(struct vdec_vpu_inst *vpu, void *msg, int len)
{
	int err;

	mtk_vcodec_debug(vpu, "id=%X", *(uint32_t *)msg);

	vpu->failure = 0;
	vpu->signaled = 0;

	err = vpu_ipi_send(vpu->dev, vpu->id, msg, len);
	if (err) {
		mtk_vcodec_err(vpu, "send fail vpu_id=%d msg_id=%X status=%d",
			       vpu->id, *(uint32_t *)msg, err);
		return err;
	}

	return vpu->failure;
}

static int vcodec_send_ap_ipi(struct vdec_vpu_inst *vpu, unsigned int msg_id)
{
	struct vdec_ap_ipi_cmd msg;
	int err = 0;

	mtk_vcodec_debug(vpu, "+ id=%X", msg_id);

	memset(&msg, 0, sizeof(msg));
	msg.msg_id = msg_id;
	msg.vpu_inst_addr = vpu->inst_addr;

	err = vcodec_vpu_send_msg(vpu, &msg, sizeof(msg));
	mtk_vcodec_debug(vpu, "- id=%X ret=%d", msg_id, err);
	return err;
}

int vpu_dec_init(struct vdec_vpu_inst *vpu)
{
	struct vdec_ap_ipi_init msg;
	int err;

	mtk_vcodec_debug_enter(vpu);

	init_waitqueue_head(&vpu->wq);
	vpu->handler = vpu_dec_ipi_handler;

	err = vpu_ipi_register(vpu->dev, vpu->id, vpu->handler, "vdec", NULL);
	if (err != 0) {
		mtk_vcodec_err(vpu, "vpu_ipi_register fail status=%d", err);
		return err;
	}

	memset(&msg, 0, sizeof(msg));
	msg.msg_id = AP_IPIMSG_DEC_INIT;
	msg.ap_inst_addr = (unsigned long)vpu;

	mtk_vcodec_debug(vpu, "vdec_inst=%p", vpu);

	err = vcodec_vpu_send_msg(vpu, (void *)&msg, sizeof(msg));
	mtk_vcodec_debug(vpu, "- ret=%d", err);
	return err;
}

int vpu_dec_start(struct vdec_vpu_inst *vpu, uint32_t *data, unsigned int len)
{
	struct vdec_ap_ipi_dec_start msg;
	int i;
	int err = 0;

	mtk_vcodec_debug_enter(vpu);

	if (len > ARRAY_SIZE(msg.data)) {
		mtk_vcodec_err(vpu, "invalid len = %d\n", len);
		return -EINVAL;
	}

	memset(&msg, 0, sizeof(msg));
	msg.msg_id = AP_IPIMSG_DEC_START;
	msg.vpu_inst_addr = vpu->inst_addr;

	for (i = 0; i < len; i++)
		msg.data[i] = data[i];

	err = vcodec_vpu_send_msg(vpu, (void *)&msg, sizeof(msg));
	mtk_vcodec_debug(vpu, "- ret=%d", err);
	return err;
}

int vpu_dec_end(struct vdec_vpu_inst *vpu)
{
	return vcodec_send_ap_ipi(vpu, AP_IPIMSG_DEC_END);
}

int vpu_dec_deinit(struct vdec_vpu_inst *vpu)
{
	return vcodec_send_ap_ipi(vpu, AP_IPIMSG_DEC_DEINIT);
}

int vpu_dec_reset(struct vdec_vpu_inst *vpu)
{
	return vcodec_send_ap_ipi(vpu, AP_IPIMSG_DEC_RESET);
}
