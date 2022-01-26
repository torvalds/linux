// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: PoChun Lin <pochun.lin@mediatek.com>
 */

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_fw.h"
#include "venc_ipi_msg.h"
#include "venc_vpu_if.h"

static void handle_enc_init_msg(struct venc_vpu_inst *vpu, const void *data)
{
	const struct venc_vpu_ipi_msg_init *msg = data;

	vpu->inst_addr = msg->vpu_inst_addr;
	vpu->vsi = mtk_vcodec_fw_map_dm_addr(vpu->ctx->dev->fw_handler,
					     msg->vpu_inst_addr);

	/* Firmware version field value is unspecified on MT8173. */
	if (vpu->ctx->dev->venc_pdata->chip == MTK_MT8173)
		return;

	/* Check firmware version. */
	mtk_vcodec_debug(vpu, "firmware version: 0x%x\n",
			 msg->venc_abi_version);
	switch (msg->venc_abi_version) {
	case 1:
		break;
	default:
		mtk_vcodec_err(vpu, "unhandled firmware version 0x%x\n",
			       msg->venc_abi_version);
		vpu->failure = 1;
		break;
	}
}

static void handle_enc_encode_msg(struct venc_vpu_inst *vpu, const void *data)
{
	const struct venc_vpu_ipi_msg_enc *msg = data;

	vpu->state = msg->state;
	vpu->bs_size = msg->bs_size;
	vpu->is_key_frm = msg->is_key_frm;
}

static void vpu_enc_ipi_handler(void *data, unsigned int len, void *priv)
{
	const struct venc_vpu_ipi_msg_common *msg = data;
	struct venc_vpu_inst *vpu =
		(struct venc_vpu_inst *)(unsigned long)msg->venc_inst;

	mtk_vcodec_debug(vpu, "msg_id %x inst %p status %d",
			 msg->msg_id, vpu, msg->status);

	vpu->signaled = 1;
	vpu->failure = (msg->status != VENC_IPI_MSG_STATUS_OK);
	if (vpu->failure)
		goto failure;

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

failure:
	mtk_vcodec_debug_leave(vpu);
}

static int vpu_enc_send_msg(struct venc_vpu_inst *vpu, void *msg,
			    int len)
{
	int status;

	mtk_vcodec_debug_enter(vpu);

	if (!vpu->ctx->dev->fw_handler) {
		mtk_vcodec_err(vpu, "inst dev is NULL");
		return -EINVAL;
	}

	status = mtk_vcodec_fw_ipi_send(vpu->ctx->dev->fw_handler, vpu->id, msg,
					len, 2000);
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

	status = mtk_vcodec_fw_ipi_register(vpu->ctx->dev->fw_handler, vpu->id,
					    vpu_enc_ipi_handler, "venc", NULL);

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

static unsigned int venc_enc_param_crop_right(struct venc_vpu_inst *vpu,
					      struct venc_enc_param *enc_prm)
{
	unsigned int img_crop_right = enc_prm->buf_width - enc_prm->width;

	return img_crop_right % 16;
}

static unsigned int venc_enc_param_crop_bottom(struct venc_enc_param *enc_prm)
{
	return round_up(enc_prm->height, 16) - enc_prm->height;
}

static unsigned int venc_enc_param_num_mb(struct venc_enc_param *enc_prm)
{
	return DIV_ROUND_UP(enc_prm->width, 16) *
	       DIV_ROUND_UP(enc_prm->height, 16);
}

int vpu_enc_set_param(struct venc_vpu_inst *vpu,
		      enum venc_set_param_type id,
		      struct venc_enc_param *enc_param)
{
	const bool is_ext = MTK_ENC_CTX_IS_EXT(vpu->ctx);
	size_t msg_size = is_ext ?
		sizeof(struct venc_ap_ipi_msg_set_param_ext) :
		sizeof(struct venc_ap_ipi_msg_set_param);
	struct venc_ap_ipi_msg_set_param_ext out;

	mtk_vcodec_debug(vpu, "id %d ->", id);

	memset(&out, 0, sizeof(out));
	out.base.msg_id = AP_IPIMSG_ENC_SET_PARAM;
	out.base.vpu_inst_addr = vpu->inst_addr;
	out.base.param_id = id;
	switch (id) {
	case VENC_SET_PARAM_ENC:
		if (is_ext) {
			out.base.data_item = 3;
			out.base.data[0] =
				venc_enc_param_crop_right(vpu, enc_param);
			out.base.data[1] =
				venc_enc_param_crop_bottom(enc_param);
			out.base.data[2] = venc_enc_param_num_mb(enc_param);
		} else {
			out.base.data_item = 0;
		}
		break;
	case VENC_SET_PARAM_FORCE_INTRA:
		out.base.data_item = 0;
		break;
	case VENC_SET_PARAM_ADJUST_BITRATE:
		out.base.data_item = 1;
		out.base.data[0] = enc_param->bitrate;
		break;
	case VENC_SET_PARAM_ADJUST_FRAMERATE:
		out.base.data_item = 1;
		out.base.data[0] = enc_param->frm_rate;
		break;
	case VENC_SET_PARAM_GOP_SIZE:
		out.base.data_item = 1;
		out.base.data[0] = enc_param->gop_size;
		break;
	case VENC_SET_PARAM_INTRA_PERIOD:
		out.base.data_item = 1;
		out.base.data[0] = enc_param->intra_period;
		break;
	case VENC_SET_PARAM_SKIP_FRAME:
		out.base.data_item = 0;
		break;
	default:
		mtk_vcodec_err(vpu, "id %d not supported", id);
		return -EINVAL;
	}
	if (vpu_enc_send_msg(vpu, &out, msg_size)) {
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
		   struct venc_frame_info *frame_info)
{
	const bool is_ext = MTK_ENC_CTX_IS_EXT(vpu->ctx);
	size_t msg_size = is_ext ?
		sizeof(struct venc_ap_ipi_msg_enc_ext) :
		sizeof(struct venc_ap_ipi_msg_enc);
	struct venc_ap_ipi_msg_enc_ext out;

	mtk_vcodec_debug(vpu, "bs_mode %d ->", bs_mode);

	memset(&out, 0, sizeof(out));
	out.base.msg_id = AP_IPIMSG_ENC_ENCODE;
	out.base.vpu_inst_addr = vpu->inst_addr;
	out.base.bs_mode = bs_mode;
	if (frm_buf) {
		if ((frm_buf->fb_addr[0].dma_addr % 16 == 0) &&
		    (frm_buf->fb_addr[1].dma_addr % 16 == 0) &&
		    (frm_buf->fb_addr[2].dma_addr % 16 == 0)) {
			out.base.input_addr[0] = frm_buf->fb_addr[0].dma_addr;
			out.base.input_addr[1] = frm_buf->fb_addr[1].dma_addr;
			out.base.input_addr[2] = frm_buf->fb_addr[2].dma_addr;
		} else {
			mtk_vcodec_err(vpu, "dma_addr not align to 16");
			return -EINVAL;
		}
	}
	if (bs_buf) {
		out.base.bs_addr = bs_buf->dma_addr;
		out.base.bs_size = bs_buf->size;
	}
	if (is_ext && frame_info) {
		out.data_item = 3;
		out.data[0] = frame_info->frm_count;
		out.data[1] = frame_info->skip_frm_count;
		out.data[2] = frame_info->frm_type;
	}
	if (vpu_enc_send_msg(vpu, &out, msg_size)) {
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
