/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: PoChun Lin <pochun.lin@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "mtk_vpu.h"
#include "venc_ipi_msg.h"
#include "venc_vpu_if.h"

static void handle_enc_init_msg(struct venc_vpu_inst *vpu, void *data)
{
	struct venc_vpu_ipi_msg_init *msg = data;

	vpu->inst_addr = msg->vpu_inst_addr;
	vpu->vsi = vpu_mapping_dm_addr(vpu->dev, msg->vpu_inst_addr);
}

static void handle_enc_encode_msg(struct venc_vpu_inst *vpu, void *data)
{
	struct venc_vpu_ipi_msg_enc *msg = data;

	vpu->state = msg->state;
	vpu->bs_size = msg->bs_size;
	vpu->is_key_frm = msg->is_key_frm;
}

static void vpu_enc_ipi_handler(void *data, unsigned int len, void *priv)
{
	struct venc_vpu_ipi_msg_common *msg = data;
	struct venc_vpu_inst *vpu =
		(struct venc_vpu_inst *)(unsigned long)msg->venc_inst;

	mtk_vcodec_debug(vpu, "msg_id %x inst %p status %d",
			 msg->msg_id, vpu, msg->status);

	switch (msg->msg_id) {
	case VPU_IPIMSG_ENC_INIT_DONE:
		handle_enc_init_msg(vpu, data);
		break;
	case VPU_IPIMSG_ENC_SET_PARAM_DONE:
		break;
	case VPU_IPIMSG_ENC_ENCODE_DONE:
		handle_enc_encode_msg(vpu, data);
		break;
	case VPU_IPIMSG_ENC_DEINIT_DONE:
		break;
	default:
		mtk_vcodec_err(vpu, "unknown msg id %x", msg->msg_id);
		break;
	}

	vpu->signaled = 1;
	vpu->failure = (msg->status != VENC_IPI_MSG_STATUS_OK);

	mtk_vcodec_debug_leave(vpu);
}

static int vpu_enc_send_msg(struct venc_vpu_inst *vpu, void *msg,
			    int len)
{
	int status;

	mtk_vcodec_debug_enter(vpu);

	if (!vpu->dev) {
		mtk_vcodec_err(vpu, "inst dev is NULL");
		return -EINVAL;
	}

	status = vpu_ipi_send(vpu->dev, vpu->id, msg, len);
	if (status) {
		mtk_vcodec_err(vpu, "vpu_ipi_send msg_id %x len %d fail %d",
			       *(uint32_t *)msg, len, status);
		return -EINVAL;
	}
	if (vpu->failure)
		return -EINVAL;

	mtk_vcodec_debug_leave(vpu);

	return 0;
}

int vpu_enc_init(struct venc_vpu_inst *vpu)
{
	int status;
	struct venc_ap_ipi_msg_init out;

	mtk_vcodec_debug_enter(vpu);

	init_waitqueue_head(&vpu->wq_hd);
	vpu->signaled = 0;
	vpu->failure = 0;

	status = vpu_ipi_register(vpu->dev, vpu->id, vpu_enc_ipi_handler,
				  NULL, NULL);
	if (status) {
		mtk_vcodec_err(vpu, "vpu_ipi_register fail %d", status);
		return -EINVAL;
	}

	memset(&out, 0, sizeof(out));
	out.msg_id = AP_IPIMSG_ENC_INIT;
	out.venc_inst = (unsigned long)vpu;
	if (vpu_enc_send_msg(vpu, &out, sizeof(out))) {
		mtk_vcodec_err(vpu, "AP_IPIMSG_ENC_INIT fail");
		return -EINVAL;
	}

	mtk_vcodec_debug_leave(vpu);

	return 0;
}

int vpu_enc_set_param(struct venc_vpu_inst *vpu,
		      enum venc_set_param_type id,
		      struct venc_enc_param *enc_param)
{
	struct venc_ap_ipi_msg_set_param out;

	mtk_vcodec_debug(vpu, "id %d ->", id);

	memset(&out, 0, sizeof(out));
	out.msg_id = AP_IPIMSG_ENC_SET_PARAM;
	out.vpu_inst_addr = vpu->inst_addr;
	out.param_id = id;
	switch (id) {
	case VENC_SET_PARAM_ENC:
		out.data_item = 0;
		break;
	case VENC_SET_PARAM_FORCE_INTRA:
		out.data_item = 0;
		break;
	case VENC_SET_PARAM_ADJUST_BITRATE:
		out.data_item = 1;
		out.data[0] = enc_param->bitrate;
		break;
	case VENC_SET_PARAM_ADJUST_FRAMERATE:
		out.data_item = 1;
		out.data[0] = enc_param->frm_rate;
		break;
	case VENC_SET_PARAM_GOP_SIZE:
		out.data_item = 1;
		out.data[0] = enc_param->gop_size;
		break;
	case VENC_SET_PARAM_INTRA_PERIOD:
		out.data_item = 1;
		out.data[0] = enc_param->intra_period;
		break;
	case VENC_SET_PARAM_SKIP_FRAME:
		out.data_item = 0;
		break;
	default:
		mtk_vcodec_err(vpu, "id %d not supported", id);
		return -EINVAL;
	}
	if (vpu_enc_send_msg(vpu, &out, sizeof(out))) {
		mtk_vcodec_err(vpu,
			       "AP_IPIMSG_ENC_SET_PARAM %d fail", id);
		return -EINVAL;
	}

	mtk_vcodec_debug(vpu, "id %d <-", id);

	return 0;
}

int vpu_enc_encode(struct venc_vpu_inst *vpu, unsigned int bs_mode,
		   struct venc_frm_buf *frm_buf,
		   struct mtk_vcodec_mem *bs_buf,
		   unsigned int *bs_size)
{
	struct venc_ap_ipi_msg_enc out;

	mtk_vcodec_debug(vpu, "bs_mode %d ->", bs_mode);

	memset(&out, 0, sizeof(out));
	out.msg_id = AP_IPIMSG_ENC_ENCODE;
	out.vpu_inst_addr = vpu->inst_addr;
	out.bs_mode = bs_mode;
	if (frm_buf) {
		if ((frm_buf->fb_addr[0].dma_addr % 16 == 0) &&
		    (frm_buf->fb_addr[1].dma_addr % 16 == 0) &&
		    (frm_buf->fb_addr[2].dma_addr % 16 == 0)) {
			out.input_addr[0] = frm_buf->fb_addr[0].dma_addr;
			out.input_addr[1] = frm_buf->fb_addr[1].dma_addr;
			out.input_addr[2] = frm_buf->fb_addr[2].dma_addr;
		} else {
			mtk_vcodec_err(vpu, "dma_addr not align to 16");
			return -EINVAL;
		}
	}
	if (bs_buf) {
		out.bs_addr = bs_buf->dma_addr;
		out.bs_size = bs_buf->size;
	}
	if (vpu_enc_send_msg(vpu, &out, sizeof(out))) {
		mtk_vcodec_err(vpu, "AP_IPIMSG_ENC_ENCODE %d fail",
			       bs_mode);
		return -EINVAL;
	}

	mtk_vcodec_debug(vpu, "bs_mode %d state %d size %d key_frm %d <-",
			 bs_mode, vpu->state, vpu->bs_size, vpu->is_key_frm);

	return 0;
}

int vpu_enc_deinit(struct venc_vpu_inst *vpu)
{
	struct venc_ap_ipi_msg_deinit out;

	mtk_vcodec_debug_enter(vpu);

	memset(&out, 0, sizeof(out));
	out.msg_id = AP_IPIMSG_ENC_DEINIT;
	out.vpu_inst_addr = vpu->inst_addr;
	if (vpu_enc_send_msg(vpu, &out, sizeof(out))) {
		mtk_vcodec_err(vpu, "AP_IPIMSG_ENC_DEINIT fail");
		return -EINVAL;
	}

	mtk_vcodec_debug_leave(vpu);

	return 0;
}
