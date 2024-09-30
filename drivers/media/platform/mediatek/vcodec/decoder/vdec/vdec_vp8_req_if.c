// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#include <linux/slab.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>
#include <uapi/linux/v4l2-controls.h>

#include "../mtk_vcodec_dec.h"
#include "../../common/mtk_vcodec_intr.h"
#include "../vdec_drv_base.h"
#include "../vdec_drv_if.h"
#include "../vdec_vpu_if.h"

/* Decoding picture buffer size (3 reference frames plus current frame) */
#define VP8_DPB_SIZE 4

/* HW working buffer size (bytes) */
#define VP8_SEG_ID_SZ   SZ_256K
#define VP8_PP_WRAPY_SZ SZ_64K
#define VP8_PP_WRAPC_SZ SZ_64K
#define VP8_VLD_PRED_SZ SZ_64K

/**
 * struct vdec_vp8_slice_info - decode misc information
 *
 * @vld_wrapper_dma:	vld wrapper dma address
 * @seg_id_buf_dma:	seg id dma address
 * @wrap_y_dma:	wrap y dma address
 * @wrap_c_dma:	wrap y dma address
 * @cur_y_fb_dma:	current plane Y frame buffer dma address
 * @cur_c_fb_dma:	current plane C frame buffer dma address
 * @bs_dma:		bitstream dma address
 * @bs_sz:		bitstream size
 * @resolution_changed:resolution change flag 1 - changed,  0 - not changed
 * @frame_header_type:	current frame header type
 * @crc:		used to check whether hardware's status is right
 * @reserved:		reserved, currently unused
 */
struct vdec_vp8_slice_info {
	u64 vld_wrapper_dma;
	u64 seg_id_buf_dma;
	u64 wrap_y_dma;
	u64 wrap_c_dma;
	u64 cur_y_fb_dma;
	u64 cur_c_fb_dma;
	u64 bs_dma;
	u32 bs_sz;
	u32 resolution_changed;
	u32 frame_header_type;
	u32 crc[8];
	u32 reserved;
};

/**
 * struct vdec_vp8_slice_dpb_info  - vp8 reference information
 *
 * @y_dma_addr:	Y bitstream physical address
 * @c_dma_addr:	CbCr bitstream physical address
 * @reference_flag:	reference picture flag
 * @reserved:		64bit align
 */
struct vdec_vp8_slice_dpb_info {
	dma_addr_t y_dma_addr;
	dma_addr_t c_dma_addr;
	int reference_flag;
	int reserved;
};

/**
 * struct vdec_vp8_slice_vsi - VPU shared information
 *
 * @dec:		decoding information
 * @pic:		picture information
 * @vp8_dpb_info:	reference buffer information
 */
struct vdec_vp8_slice_vsi {
	struct vdec_vp8_slice_info dec;
	struct vdec_pic_info pic;
	struct vdec_vp8_slice_dpb_info vp8_dpb_info[3];
};

/**
 * struct vdec_vp8_slice_inst - VP8 decoder instance
 *
 * @seg_id_buf:	seg buffer
 * @wrap_y_buf:	wrapper y buffer
 * @wrap_c_buf:	wrapper c buffer
 * @vld_wrapper_buf:	vld wrapper buffer
 * @ctx:		V4L2 context
 * @vpu:		VPU instance for decoder
 * @vsi:		VPU share information
 */
struct vdec_vp8_slice_inst {
	struct mtk_vcodec_mem seg_id_buf;
	struct mtk_vcodec_mem wrap_y_buf;
	struct mtk_vcodec_mem wrap_c_buf;
	struct mtk_vcodec_mem vld_wrapper_buf;
	struct mtk_vcodec_dec_ctx *ctx;
	struct vdec_vpu_inst vpu;
	struct vdec_vp8_slice_vsi *vsi;
};

static void *vdec_vp8_slice_get_ctrl_ptr(struct mtk_vcodec_dec_ctx *ctx, int id)
{
	struct v4l2_ctrl *ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl, id);

	if (!ctrl)
		return ERR_PTR(-EINVAL);

	return ctrl->p_cur.p;
}

static void vdec_vp8_slice_get_pic_info(struct vdec_vp8_slice_inst *inst)
{
	struct mtk_vcodec_dec_ctx *ctx = inst->ctx;
	unsigned int data[3];

	data[0] = ctx->picinfo.pic_w;
	data[1] = ctx->picinfo.pic_h;
	data[2] = ctx->capture_fourcc;
	vpu_dec_get_param(&inst->vpu, data, 3, GET_PARAM_PIC_INFO);

	ctx->picinfo.buf_w = ALIGN(ctx->picinfo.pic_w, 64);
	ctx->picinfo.buf_h = ALIGN(ctx->picinfo.pic_h, 64);
	ctx->picinfo.fb_sz[0] = inst->vpu.fb_sz[0];
	ctx->picinfo.fb_sz[1] = inst->vpu.fb_sz[1];

	inst->vsi->pic.pic_w = ctx->picinfo.pic_w;
	inst->vsi->pic.pic_h = ctx->picinfo.pic_h;
	inst->vsi->pic.buf_w = ctx->picinfo.buf_w;
	inst->vsi->pic.buf_h = ctx->picinfo.buf_h;
	inst->vsi->pic.fb_sz[0] = ctx->picinfo.fb_sz[0];
	inst->vsi->pic.fb_sz[1] = ctx->picinfo.fb_sz[1];
	mtk_vdec_debug(inst->ctx, "pic(%d, %d), buf(%d, %d)",
		       ctx->picinfo.pic_w, ctx->picinfo.pic_h,
		       ctx->picinfo.buf_w, ctx->picinfo.buf_h);
	mtk_vdec_debug(inst->ctx, "fb size: Y(%d), C(%d)",
		       ctx->picinfo.fb_sz[0], ctx->picinfo.fb_sz[1]);
}

static int vdec_vp8_slice_alloc_working_buf(struct vdec_vp8_slice_inst *inst)
{
	int err;
	struct mtk_vcodec_mem *mem;

	mem = &inst->seg_id_buf;
	mem->size = VP8_SEG_ID_SZ;
	err = mtk_vcodec_mem_alloc(inst->ctx, mem);
	if (err) {
		mtk_vdec_err(inst->ctx, "Cannot allocate working buffer");
		return err;
	}
	inst->vsi->dec.seg_id_buf_dma = (u64)mem->dma_addr;

	mem = &inst->wrap_y_buf;
	mem->size = VP8_PP_WRAPY_SZ;
	err = mtk_vcodec_mem_alloc(inst->ctx, mem);
	if (err) {
		mtk_vdec_err(inst->ctx, "cannot allocate WRAP Y buffer");
		return err;
	}
	inst->vsi->dec.wrap_y_dma = (u64)mem->dma_addr;

	mem = &inst->wrap_c_buf;
	mem->size = VP8_PP_WRAPC_SZ;
	err = mtk_vcodec_mem_alloc(inst->ctx, mem);
	if (err) {
		mtk_vdec_err(inst->ctx, "cannot allocate WRAP C buffer");
		return err;
	}
	inst->vsi->dec.wrap_c_dma = (u64)mem->dma_addr;

	mem = &inst->vld_wrapper_buf;
	mem->size = VP8_VLD_PRED_SZ;
	err = mtk_vcodec_mem_alloc(inst->ctx, mem);
	if (err) {
		mtk_vdec_err(inst->ctx, "cannot allocate vld wrapper buffer");
		return err;
	}
	inst->vsi->dec.vld_wrapper_dma = (u64)mem->dma_addr;

	return 0;
}

static void vdec_vp8_slice_free_working_buf(struct vdec_vp8_slice_inst *inst)
{
	struct mtk_vcodec_mem *mem;

	mem = &inst->seg_id_buf;
	if (mem->va)
		mtk_vcodec_mem_free(inst->ctx, mem);
	inst->vsi->dec.seg_id_buf_dma = 0;

	mem = &inst->wrap_y_buf;
	if (mem->va)
		mtk_vcodec_mem_free(inst->ctx, mem);
	inst->vsi->dec.wrap_y_dma = 0;

	mem = &inst->wrap_c_buf;
	if (mem->va)
		mtk_vcodec_mem_free(inst->ctx, mem);
	inst->vsi->dec.wrap_c_dma = 0;

	mem = &inst->vld_wrapper_buf;
	if (mem->va)
		mtk_vcodec_mem_free(inst->ctx, mem);
	inst->vsi->dec.vld_wrapper_dma = 0;
}

static u64 vdec_vp8_slice_get_ref_by_ts(const struct v4l2_ctrl_vp8_frame *frame_header,
					int index)
{
	switch (index) {
	case 0:
		return frame_header->last_frame_ts;
	case 1:
		return frame_header->golden_frame_ts;
	case 2:
		return frame_header->alt_frame_ts;
	default:
		break;
	}

	return -1;
}

static int vdec_vp8_slice_get_decode_parameters(struct vdec_vp8_slice_inst *inst)
{
	const struct v4l2_ctrl_vp8_frame *frame_header;
	struct mtk_vcodec_dec_ctx *ctx = inst->ctx;
	struct vb2_queue *vq;
	struct vb2_buffer *vb;
	u64 referenct_ts;
	int index;

	frame_header = vdec_vp8_slice_get_ctrl_ptr(inst->ctx, V4L2_CID_STATELESS_VP8_FRAME);
	if (IS_ERR(frame_header))
		return PTR_ERR(frame_header);

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	for (index = 0; index < 3; index++) {
		referenct_ts = vdec_vp8_slice_get_ref_by_ts(frame_header, index);
		vb = vb2_find_buffer(vq, referenct_ts);
		if (!vb) {
			if (!V4L2_VP8_FRAME_IS_KEY_FRAME(frame_header))
				mtk_vdec_err(inst->ctx, "reference invalid: index(%d) ts(%lld)",
					     index, referenct_ts);
			inst->vsi->vp8_dpb_info[index].reference_flag = 0;
			continue;
		}
		inst->vsi->vp8_dpb_info[index].reference_flag = 1;

		inst->vsi->vp8_dpb_info[index].y_dma_addr =
			vb2_dma_contig_plane_dma_addr(vb, 0);
		if (ctx->q_data[MTK_Q_DATA_DST].fmt->num_planes == 2)
			inst->vsi->vp8_dpb_info[index].c_dma_addr =
				vb2_dma_contig_plane_dma_addr(vb, 1);
		else
			inst->vsi->vp8_dpb_info[index].c_dma_addr =
				inst->vsi->vp8_dpb_info[index].y_dma_addr +
				ctx->picinfo.fb_sz[0];
	}

	inst->vsi->dec.frame_header_type = frame_header->flags >> 1;

	return 0;
}

static int vdec_vp8_slice_init(struct mtk_vcodec_dec_ctx *ctx)
{
	struct vdec_vp8_slice_inst *inst;
	int err;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	inst->ctx = ctx;

	inst->vpu.id = SCP_IPI_VDEC_LAT;
	inst->vpu.core_id = SCP_IPI_VDEC_CORE;
	inst->vpu.ctx = ctx;
	inst->vpu.codec_type = ctx->current_codec;
	inst->vpu.capture_type = ctx->capture_fourcc;

	err = vpu_dec_init(&inst->vpu);
	if (err) {
		mtk_vdec_err(ctx, "vdec_vp8 init err=%d", err);
		goto error_free_inst;
	}

	inst->vsi = inst->vpu.vsi;
	err = vdec_vp8_slice_alloc_working_buf(inst);
	if (err)
		goto error_deinit;

	mtk_vdec_debug(ctx, "vp8 struct size = %d vsi: %d\n",
		       (int)sizeof(struct v4l2_ctrl_vp8_frame),
		       (int)sizeof(struct vdec_vp8_slice_vsi));
	mtk_vdec_debug(ctx, "vp8:%p, codec_type = 0x%x vsi: 0x%p",
		       inst, inst->vpu.codec_type, inst->vpu.vsi);

	ctx->drv_handle = inst;
	return 0;

error_deinit:
	vpu_dec_deinit(&inst->vpu);
error_free_inst:
	kfree(inst);
	return err;
}

static int vdec_vp8_slice_decode(void *h_vdec, struct mtk_vcodec_mem *bs,
				 struct vdec_fb *fb, bool *res_chg)
{
	struct vdec_vp8_slice_inst *inst = h_vdec;
	struct vdec_vpu_inst *vpu = &inst->vpu;
	struct mtk_video_dec_buf *src_buf_info, *dst_buf_info;
	unsigned int data;
	u64 y_fb_dma, c_fb_dma;
	int err, timeout;

	/* Resolution changes are never initiated by us */
	*res_chg = false;

	/* bs NULL means flush decoder */
	if (!bs)
		return vpu_dec_reset(vpu);

	src_buf_info = container_of(bs, struct mtk_video_dec_buf, bs_buffer);

	fb = inst->ctx->dev->vdec_pdata->get_cap_buffer(inst->ctx);
	if (!fb) {
		mtk_vdec_err(inst->ctx, "fb buffer is NULL");
		return -ENOMEM;
	}

	dst_buf_info = container_of(fb, struct mtk_video_dec_buf, frame_buffer);
	y_fb_dma = fb->base_y.dma_addr;
	if (inst->ctx->q_data[MTK_Q_DATA_DST].fmt->num_planes == 1)
		c_fb_dma = y_fb_dma +
			inst->ctx->picinfo.buf_w * inst->ctx->picinfo.buf_h;
	else
		c_fb_dma = fb->base_c.dma_addr;

	inst->vsi->dec.bs_dma = (u64)bs->dma_addr;
	inst->vsi->dec.bs_sz = bs->size;
	inst->vsi->dec.cur_y_fb_dma = y_fb_dma;
	inst->vsi->dec.cur_c_fb_dma = c_fb_dma;

	mtk_vdec_debug(inst->ctx, "frame[%d] bs(%zu 0x%llx) y/c(0x%llx 0x%llx)",
		       inst->ctx->decoded_frame_cnt,
		       bs->size, (u64)bs->dma_addr,
		       y_fb_dma, c_fb_dma);

	v4l2_m2m_buf_copy_metadata(&src_buf_info->m2m_buf.vb,
				   &dst_buf_info->m2m_buf.vb, true);

	err = vdec_vp8_slice_get_decode_parameters(inst);
	if (err)
		goto error;

	err = vpu_dec_start(vpu, &data, 1);
	if (err) {
		mtk_vdec_debug(inst->ctx, "vp8 dec start err!");
		goto error;
	}

	if (inst->vsi->dec.resolution_changed) {
		mtk_vdec_debug(inst->ctx, "- resolution_changed -");
		*res_chg = true;
		return 0;
	}

	/* wait decode done interrupt */
	timeout = mtk_vcodec_wait_for_done_ctx(inst->ctx, MTK_INST_IRQ_RECEIVED,
					       50, MTK_VDEC_CORE);

	err = vpu_dec_end(vpu);
	if (err || timeout)
		mtk_vdec_debug(inst->ctx, "vp8 dec error timeout:%d err: %d pic_%d",
			       timeout, err, inst->ctx->decoded_frame_cnt);

	mtk_vdec_debug(inst->ctx, "pic[%d] crc: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",
		       inst->ctx->decoded_frame_cnt,
		       inst->vsi->dec.crc[0], inst->vsi->dec.crc[1],
		       inst->vsi->dec.crc[2], inst->vsi->dec.crc[3],
		       inst->vsi->dec.crc[4], inst->vsi->dec.crc[5],
		       inst->vsi->dec.crc[6], inst->vsi->dec.crc[7]);

	inst->ctx->decoded_frame_cnt++;
error:
	return err;
}

static int vdec_vp8_slice_get_param(void *h_vdec, enum vdec_get_param_type type, void *out)
{
	struct vdec_vp8_slice_inst *inst = h_vdec;

	switch (type) {
	case GET_PARAM_PIC_INFO:
		vdec_vp8_slice_get_pic_info(inst);
		break;
	case GET_PARAM_CROP_INFO:
		mtk_vdec_debug(inst->ctx, "No need to get vp8 crop information.");
		break;
	case GET_PARAM_DPB_SIZE:
		*((unsigned int *)out) = VP8_DPB_SIZE;
		break;
	default:
		mtk_vdec_err(inst->ctx, "invalid get parameter type=%d", type);
		return -EINVAL;
	}

	return 0;
}

static void vdec_vp8_slice_deinit(void *h_vdec)
{
	struct vdec_vp8_slice_inst *inst = h_vdec;

	vpu_dec_deinit(&inst->vpu);
	vdec_vp8_slice_free_working_buf(inst);
	kfree(inst);
}

const struct vdec_common_if vdec_vp8_slice_if = {
	.init		= vdec_vp8_slice_init,
	.decode		= vdec_vp8_slice_decode,
	.get_param	= vdec_vp8_slice_get_param,
	.deinit		= vdec_vp8_slice_deinit,
};
