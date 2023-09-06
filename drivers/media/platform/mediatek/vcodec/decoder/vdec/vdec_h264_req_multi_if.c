// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <media/v4l2-h264.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "../mtk_vcodec_dec.h"
#include "../../common/mtk_vcodec_intr.h"
#include "../vdec_drv_base.h"
#include "../vdec_drv_if.h"
#include "../vdec_vpu_if.h"
#include "vdec_h264_req_common.h"

/**
 * enum vdec_h264_core_dec_err_type  - core decode error type
 *
 * @TRANS_BUFFER_FULL: trans buffer is full
 * @SLICE_HEADER_FULL: slice header buffer is full
 */
enum vdec_h264_core_dec_err_type {
	TRANS_BUFFER_FULL = 1,
	SLICE_HEADER_FULL,
};

/**
 * struct vdec_h264_slice_lat_dec_param  - parameters for decode current frame
 *
 * @sps:		h264 sps syntax parameters
 * @pps:		h264 pps syntax parameters
 * @slice_header:	h264 slice header syntax parameters
 * @scaling_matrix:	h264 scaling list parameters
 * @decode_params:	decoder parameters of each frame used for hardware decode
 * @h264_dpb_info:	dpb reference list
 */
struct vdec_h264_slice_lat_dec_param {
	struct mtk_h264_sps_param sps;
	struct mtk_h264_pps_param pps;
	struct mtk_h264_slice_hd_param slice_header;
	struct slice_api_h264_scaling_matrix scaling_matrix;
	struct slice_api_h264_decode_param decode_params;
	struct mtk_h264_dpb_info h264_dpb_info[V4L2_H264_NUM_DPB_ENTRIES];
};

/**
 * struct vdec_h264_slice_info - decode information
 *
 * @nal_info:		nal info of current picture
 * @timeout:		Decode timeout: 1 timeout, 0 no timeount
 * @bs_buf_size:	bitstream size
 * @bs_buf_addr:	bitstream buffer dma address
 * @y_fb_dma:		Y frame buffer dma address
 * @c_fb_dma:		C frame buffer dma address
 * @vdec_fb_va:	VDEC frame buffer struct virtual address
 * @crc:		Used to check whether hardware's status is right
 */
struct vdec_h264_slice_info {
	u16 nal_info;
	u16 timeout;
	u32 bs_buf_size;
	u64 bs_buf_addr;
	u64 y_fb_dma;
	u64 c_fb_dma;
	u64 vdec_fb_va;
	u32 crc[8];
};

/**
 * struct vdec_h264_slice_vsi - shared memory for decode information exchange
 *        between SCP and Host.
 *
 * @wdma_err_addr:		wdma error dma address
 * @wdma_start_addr:		wdma start dma address
 * @wdma_end_addr:		wdma end dma address
 * @slice_bc_start_addr:	slice bc start dma address
 * @slice_bc_end_addr:		slice bc end dma address
 * @row_info_start_addr:	row info start dma address
 * @row_info_end_addr:		row info end dma address
 * @trans_start:		trans start dma address
 * @trans_end:			trans end dma address
 * @wdma_end_addr_offset:	wdma end address offset
 *
 * @mv_buf_dma:		HW working motion vector buffer
 *				dma address (AP-W, VPU-R)
 * @dec:			decode information (AP-R, VPU-W)
 * @h264_slice_params:		decode parameters for hw used
 */
struct vdec_h264_slice_vsi {
	/* LAT dec addr */
	u64 wdma_err_addr;
	u64 wdma_start_addr;
	u64 wdma_end_addr;
	u64 slice_bc_start_addr;
	u64 slice_bc_end_addr;
	u64 row_info_start_addr;
	u64 row_info_end_addr;
	u64 trans_start;
	u64 trans_end;
	u64 wdma_end_addr_offset;

	u64 mv_buf_dma[H264_MAX_MV_NUM];
	struct vdec_h264_slice_info dec;
	struct vdec_h264_slice_lat_dec_param h264_slice_params;
};

/**
 * struct vdec_h264_slice_share_info - shared information used to exchange
 *                                     message between lat and core
 *
 * @sps:		sequence header information from user space
 * @dec_params:	decoder params from user space
 * @h264_slice_params:	decoder params used for hardware
 * @trans_start:	trans start dma address
 * @trans_end:		trans end dma address
 * @nal_info:		nal info of current picture
 */
struct vdec_h264_slice_share_info {
	struct v4l2_ctrl_h264_sps sps;
	struct v4l2_ctrl_h264_decode_params dec_params;
	struct vdec_h264_slice_lat_dec_param h264_slice_params;
	u64 trans_start;
	u64 trans_end;
	u16 nal_info;
};

/**
 * struct vdec_h264_slice_inst - h264 decoder instance
 *
 * @slice_dec_num:	how many picture be decoded
 * @ctx:		point to mtk_vcodec_dec_ctx
 * @pred_buf:		HW working predication buffer
 * @mv_buf:		HW working motion vector buffer
 * @vpu:		VPU instance
 * @vsi:		vsi used for lat
 * @vsi_core:		vsi used for core
 *
 * @vsi_ctx:		Local VSI data for this decoding context
 * @h264_slice_param:	the parameters that hardware use to decode
 *
 * @resolution_changed:resolution changed
 * @realloc_mv_buf:	reallocate mv buffer
 * @cap_num_planes:	number of capture queue plane
 *
 * @dpb:		decoded picture buffer used to store reference
 *			buffer information
 *@is_field_bitstream:	is field bitstream
 */
struct vdec_h264_slice_inst {
	unsigned int slice_dec_num;
	struct mtk_vcodec_dec_ctx *ctx;
	struct mtk_vcodec_mem pred_buf;
	struct mtk_vcodec_mem mv_buf[H264_MAX_MV_NUM];
	struct vdec_vpu_inst vpu;
	struct vdec_h264_slice_vsi *vsi;
	struct vdec_h264_slice_vsi *vsi_core;

	struct vdec_h264_slice_vsi vsi_ctx;
	struct vdec_h264_slice_lat_dec_param h264_slice_param;

	unsigned int resolution_changed;
	unsigned int realloc_mv_buf;
	unsigned int cap_num_planes;

	struct v4l2_h264_dpb_entry dpb[16];
	bool is_field_bitstream;
};

static int vdec_h264_slice_fill_decode_parameters(struct vdec_h264_slice_inst *inst,
						  struct vdec_h264_slice_share_info *share_info)
{
	struct vdec_h264_slice_lat_dec_param *slice_param = &inst->vsi->h264_slice_params;
	const struct v4l2_ctrl_h264_decode_params *dec_params;
	const struct v4l2_ctrl_h264_scaling_matrix *src_matrix;
	const struct v4l2_ctrl_h264_sps *sps;
	const struct v4l2_ctrl_h264_pps *pps;

	dec_params =
		mtk_vdec_h264_get_ctrl_ptr(inst->ctx, V4L2_CID_STATELESS_H264_DECODE_PARAMS);
	if (IS_ERR(dec_params))
		return PTR_ERR(dec_params);

	src_matrix =
		mtk_vdec_h264_get_ctrl_ptr(inst->ctx, V4L2_CID_STATELESS_H264_SCALING_MATRIX);
	if (IS_ERR(src_matrix))
		return PTR_ERR(src_matrix);

	sps = mtk_vdec_h264_get_ctrl_ptr(inst->ctx, V4L2_CID_STATELESS_H264_SPS);
	if (IS_ERR(sps))
		return PTR_ERR(sps);

	pps = mtk_vdec_h264_get_ctrl_ptr(inst->ctx, V4L2_CID_STATELESS_H264_PPS);
	if (IS_ERR(pps))
		return PTR_ERR(pps);

	if (dec_params->flags & V4L2_H264_DECODE_PARAM_FLAG_FIELD_PIC) {
		mtk_vdec_err(inst->ctx, "No support for H.264 field decoding.");
		inst->is_field_bitstream = true;
		return -EINVAL;
	}

	mtk_vdec_h264_copy_sps_params(&slice_param->sps, sps);
	mtk_vdec_h264_copy_pps_params(&slice_param->pps, pps);
	mtk_vdec_h264_copy_scaling_matrix(&slice_param->scaling_matrix, src_matrix);

	memcpy(&share_info->sps, sps, sizeof(*sps));
	memcpy(&share_info->dec_params, dec_params, sizeof(*dec_params));

	return 0;
}

static int get_vdec_sig_decode_parameters(struct vdec_h264_slice_inst *inst)
{
	const struct v4l2_ctrl_h264_decode_params *dec_params;
	const struct v4l2_ctrl_h264_sps *sps;
	const struct v4l2_ctrl_h264_pps *pps;
	const struct v4l2_ctrl_h264_scaling_matrix *scaling_matrix;
	struct vdec_h264_slice_lat_dec_param *slice_param = &inst->h264_slice_param;
	struct v4l2_h264_reflist_builder reflist_builder;
	struct v4l2_h264_reference v4l2_p0_reflist[V4L2_H264_REF_LIST_LEN];
	struct v4l2_h264_reference v4l2_b0_reflist[V4L2_H264_REF_LIST_LEN];
	struct v4l2_h264_reference v4l2_b1_reflist[V4L2_H264_REF_LIST_LEN];
	u8 *p0_reflist = slice_param->decode_params.ref_pic_list_p0;
	u8 *b0_reflist = slice_param->decode_params.ref_pic_list_b0;
	u8 *b1_reflist = slice_param->decode_params.ref_pic_list_b1;

	dec_params =
		mtk_vdec_h264_get_ctrl_ptr(inst->ctx, V4L2_CID_STATELESS_H264_DECODE_PARAMS);
	if (IS_ERR(dec_params))
		return PTR_ERR(dec_params);

	sps = mtk_vdec_h264_get_ctrl_ptr(inst->ctx, V4L2_CID_STATELESS_H264_SPS);
	if (IS_ERR(sps))
		return PTR_ERR(sps);

	pps = mtk_vdec_h264_get_ctrl_ptr(inst->ctx, V4L2_CID_STATELESS_H264_PPS);
	if (IS_ERR(pps))
		return PTR_ERR(pps);

	scaling_matrix =
		mtk_vdec_h264_get_ctrl_ptr(inst->ctx, V4L2_CID_STATELESS_H264_SCALING_MATRIX);
	if (IS_ERR(scaling_matrix))
		return PTR_ERR(scaling_matrix);

	mtk_vdec_h264_update_dpb(dec_params, inst->dpb);

	mtk_vdec_h264_copy_sps_params(&slice_param->sps, sps);
	mtk_vdec_h264_copy_pps_params(&slice_param->pps, pps);
	mtk_vdec_h264_copy_scaling_matrix(&slice_param->scaling_matrix, scaling_matrix);

	mtk_vdec_h264_copy_decode_params(&slice_param->decode_params, dec_params, inst->dpb);
	mtk_vdec_h264_fill_dpb_info(inst->ctx, &slice_param->decode_params,
				    slice_param->h264_dpb_info);

	/* Build the reference lists */
	v4l2_h264_init_reflist_builder(&reflist_builder, dec_params, sps, inst->dpb);
	v4l2_h264_build_p_ref_list(&reflist_builder, v4l2_p0_reflist);
	v4l2_h264_build_b_ref_lists(&reflist_builder, v4l2_b0_reflist, v4l2_b1_reflist);

	/* Adapt the built lists to the firmware's expectations */
	mtk_vdec_h264_get_ref_list(p0_reflist, v4l2_p0_reflist, reflist_builder.num_valid);
	mtk_vdec_h264_get_ref_list(b0_reflist, v4l2_b0_reflist, reflist_builder.num_valid);
	mtk_vdec_h264_get_ref_list(b1_reflist, v4l2_b1_reflist, reflist_builder.num_valid);

	memcpy(&inst->vsi_ctx.h264_slice_params, slice_param,
	       sizeof(inst->vsi_ctx.h264_slice_params));

	return 0;
}

static void vdec_h264_slice_fill_decode_reflist(struct vdec_h264_slice_inst *inst,
						struct vdec_h264_slice_lat_dec_param *slice_param,
						struct vdec_h264_slice_share_info *share_info)
{
	struct v4l2_ctrl_h264_decode_params *dec_params = &share_info->dec_params;
	struct v4l2_ctrl_h264_sps *sps = &share_info->sps;
	struct v4l2_h264_reflist_builder reflist_builder;
	struct v4l2_h264_reference v4l2_p0_reflist[V4L2_H264_REF_LIST_LEN];
	struct v4l2_h264_reference v4l2_b0_reflist[V4L2_H264_REF_LIST_LEN];
	struct v4l2_h264_reference v4l2_b1_reflist[V4L2_H264_REF_LIST_LEN];
	u8 *p0_reflist = slice_param->decode_params.ref_pic_list_p0;
	u8 *b0_reflist = slice_param->decode_params.ref_pic_list_b0;
	u8 *b1_reflist = slice_param->decode_params.ref_pic_list_b1;

	mtk_vdec_h264_update_dpb(dec_params, inst->dpb);

	mtk_vdec_h264_copy_decode_params(&slice_param->decode_params, dec_params,
					 inst->dpb);
	mtk_vdec_h264_fill_dpb_info(inst->ctx, &slice_param->decode_params,
				    slice_param->h264_dpb_info);

	mtk_v4l2_vdec_dbg(3, inst->ctx, "cur poc = %d\n", dec_params->bottom_field_order_cnt);
	/* Build the reference lists */
	v4l2_h264_init_reflist_builder(&reflist_builder, dec_params, sps,
				       inst->dpb);
	v4l2_h264_build_p_ref_list(&reflist_builder, v4l2_p0_reflist);
	v4l2_h264_build_b_ref_lists(&reflist_builder, v4l2_b0_reflist, v4l2_b1_reflist);

	/* Adapt the built lists to the firmware's expectations */
	mtk_vdec_h264_get_ref_list(p0_reflist, v4l2_p0_reflist, reflist_builder.num_valid);
	mtk_vdec_h264_get_ref_list(b0_reflist, v4l2_b0_reflist, reflist_builder.num_valid);
	mtk_vdec_h264_get_ref_list(b1_reflist, v4l2_b1_reflist, reflist_builder.num_valid);
}

static int vdec_h264_slice_alloc_mv_buf(struct vdec_h264_slice_inst *inst,
					struct vdec_pic_info *pic)
{
	unsigned int buf_sz = mtk_vdec_h264_get_mv_buf_size(pic->buf_w, pic->buf_h);
	struct mtk_vcodec_mem *mem;
	int i, err;

	mtk_v4l2_vdec_dbg(3, inst->ctx, "size = 0x%x", buf_sz);
	for (i = 0; i < H264_MAX_MV_NUM; i++) {
		mem = &inst->mv_buf[i];
		if (mem->va)
			mtk_vcodec_mem_free(inst->ctx, mem);
		mem->size = buf_sz;
		err = mtk_vcodec_mem_alloc(inst->ctx, mem);
		if (err) {
			mtk_vdec_err(inst->ctx, "failed to allocate mv buf");
			return err;
		}
	}

	return 0;
}

static void vdec_h264_slice_free_mv_buf(struct vdec_h264_slice_inst *inst)
{
	int i;
	struct mtk_vcodec_mem *mem;

	for (i = 0; i < H264_MAX_MV_NUM; i++) {
		mem = &inst->mv_buf[i];
		if (mem->va)
			mtk_vcodec_mem_free(inst->ctx, mem);
	}
}

static void vdec_h264_slice_get_pic_info(struct vdec_h264_slice_inst *inst)
{
	struct mtk_vcodec_dec_ctx *ctx = inst->ctx;
	u32 data[3];

	data[0] = ctx->picinfo.pic_w;
	data[1] = ctx->picinfo.pic_h;
	data[2] = ctx->capture_fourcc;
	vpu_dec_get_param(&inst->vpu, data, 3, GET_PARAM_PIC_INFO);

	ctx->picinfo.buf_w = ALIGN(ctx->picinfo.pic_w, VCODEC_DEC_ALIGNED_64);
	ctx->picinfo.buf_h = ALIGN(ctx->picinfo.pic_h, VCODEC_DEC_ALIGNED_64);
	ctx->picinfo.fb_sz[0] = inst->vpu.fb_sz[0];
	ctx->picinfo.fb_sz[1] = inst->vpu.fb_sz[1];
	inst->cap_num_planes =
		ctx->q_data[MTK_Q_DATA_DST].fmt->num_planes;

	mtk_vdec_debug(ctx, "pic(%d, %d), buf(%d, %d)",
		       ctx->picinfo.pic_w, ctx->picinfo.pic_h,
		       ctx->picinfo.buf_w, ctx->picinfo.buf_h);
	mtk_vdec_debug(ctx, "Y/C(%d, %d)", ctx->picinfo.fb_sz[0],
		       ctx->picinfo.fb_sz[1]);

	if (ctx->last_decoded_picinfo.pic_w != ctx->picinfo.pic_w ||
	    ctx->last_decoded_picinfo.pic_h != ctx->picinfo.pic_h) {
		inst->resolution_changed = true;
		if (ctx->last_decoded_picinfo.buf_w != ctx->picinfo.buf_w ||
		    ctx->last_decoded_picinfo.buf_h != ctx->picinfo.buf_h)
			inst->realloc_mv_buf = true;

		mtk_v4l2_vdec_dbg(1, inst->ctx, "resChg: (%d %d) : old(%d, %d) -> new(%d, %d)",
				  inst->resolution_changed,
				  inst->realloc_mv_buf,
				  ctx->last_decoded_picinfo.pic_w,
				  ctx->last_decoded_picinfo.pic_h,
				  ctx->picinfo.pic_w, ctx->picinfo.pic_h);
	}
}

static void vdec_h264_slice_get_crop_info(struct vdec_h264_slice_inst *inst,
					  struct v4l2_rect *cr)
{
	cr->left = 0;
	cr->top = 0;
	cr->width = inst->ctx->picinfo.pic_w;
	cr->height = inst->ctx->picinfo.pic_h;

	mtk_vdec_debug(inst->ctx, "l=%d, t=%d, w=%d, h=%d",
		       cr->left, cr->top, cr->width, cr->height);
}

static int vdec_h264_slice_init(struct mtk_vcodec_dec_ctx *ctx)
{
	struct vdec_h264_slice_inst *inst;
	int err, vsi_size;

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
		mtk_vdec_err(ctx, "vdec_h264 init err=%d", err);
		goto error_free_inst;
	}

	vsi_size = round_up(sizeof(struct vdec_h264_slice_vsi), VCODEC_DEC_ALIGNED_64);
	inst->vsi = inst->vpu.vsi;
	inst->vsi_core =
		(struct vdec_h264_slice_vsi *)(((char *)inst->vpu.vsi) + vsi_size);
	inst->resolution_changed = true;
	inst->realloc_mv_buf = true;

	mtk_vdec_debug(ctx, "lat struct size = %d,%d,%d,%d vsi: %d\n",
		       (int)sizeof(struct mtk_h264_sps_param),
		       (int)sizeof(struct mtk_h264_pps_param),
		       (int)sizeof(struct vdec_h264_slice_lat_dec_param),
		       (int)sizeof(struct mtk_h264_dpb_info),
		       vsi_size);
	mtk_vdec_debug(ctx, "lat H264 instance >> %p, codec_type = 0x%x",
		       inst, inst->vpu.codec_type);

	ctx->drv_handle = inst;
	return 0;

error_free_inst:
	kfree(inst);
	return err;
}

static void vdec_h264_slice_deinit(void *h_vdec)
{
	struct vdec_h264_slice_inst *inst = h_vdec;

	vpu_dec_deinit(&inst->vpu);
	vdec_h264_slice_free_mv_buf(inst);
	vdec_msg_queue_deinit(&inst->ctx->msg_queue, inst->ctx);

	kfree(inst);
}

static int vdec_h264_slice_core_decode(struct vdec_lat_buf *lat_buf)
{
	struct vdec_fb *fb;
	u64 vdec_fb_va;
	u64 y_fb_dma, c_fb_dma;
	int err, timeout, i;
	struct mtk_vcodec_dec_ctx *ctx = lat_buf->ctx;
	struct vdec_h264_slice_inst *inst = ctx->drv_handle;
	struct vb2_v4l2_buffer *vb2_v4l2;
	struct vdec_h264_slice_share_info *share_info = lat_buf->private_data;
	struct mtk_vcodec_mem *mem;
	struct vdec_vpu_inst *vpu = &inst->vpu;

	mtk_vdec_debug(ctx, "[h264-core] vdec_h264 core decode");
	memcpy(&inst->vsi_core->h264_slice_params, &share_info->h264_slice_params,
	       sizeof(share_info->h264_slice_params));

	fb = ctx->dev->vdec_pdata->get_cap_buffer(ctx);
	if (!fb) {
		err = -EBUSY;
		mtk_vdec_err(ctx, "fb buffer is NULL");
		goto vdec_dec_end;
	}

	vdec_fb_va = (unsigned long)fb;
	y_fb_dma = (u64)fb->base_y.dma_addr;
	if (ctx->q_data[MTK_Q_DATA_DST].fmt->num_planes == 1)
		c_fb_dma =
			y_fb_dma + inst->ctx->picinfo.buf_w * inst->ctx->picinfo.buf_h;
	else
		c_fb_dma = (u64)fb->base_c.dma_addr;

	mtk_vdec_debug(ctx, "[h264-core] y/c addr = 0x%llx 0x%llx", y_fb_dma, c_fb_dma);

	inst->vsi_core->dec.y_fb_dma = y_fb_dma;
	inst->vsi_core->dec.c_fb_dma = c_fb_dma;
	inst->vsi_core->dec.vdec_fb_va = vdec_fb_va;
	inst->vsi_core->dec.nal_info = share_info->nal_info;
	inst->vsi_core->wdma_start_addr =
		lat_buf->ctx->msg_queue.wdma_addr.dma_addr;
	inst->vsi_core->wdma_end_addr =
		lat_buf->ctx->msg_queue.wdma_addr.dma_addr +
		lat_buf->ctx->msg_queue.wdma_addr.size;
	inst->vsi_core->wdma_err_addr = lat_buf->wdma_err_addr.dma_addr;
	inst->vsi_core->slice_bc_start_addr = lat_buf->slice_bc_addr.dma_addr;
	inst->vsi_core->slice_bc_end_addr = lat_buf->slice_bc_addr.dma_addr +
		lat_buf->slice_bc_addr.size;
	inst->vsi_core->trans_start = share_info->trans_start;
	inst->vsi_core->trans_end = share_info->trans_end;
	for (i = 0; i < H264_MAX_MV_NUM; i++) {
		mem = &inst->mv_buf[i];
		inst->vsi_core->mv_buf_dma[i] = mem->dma_addr;
	}

	vb2_v4l2 = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
	v4l2_m2m_buf_copy_metadata(&lat_buf->ts_info, vb2_v4l2, true);

	vdec_h264_slice_fill_decode_reflist(inst, &inst->vsi_core->h264_slice_params,
					    share_info);

	err = vpu_dec_core(vpu);
	if (err) {
		mtk_vdec_err(ctx, "core decode err=%d", err);
		goto vdec_dec_end;
	}

	/* wait decoder done interrupt */
	timeout = mtk_vcodec_wait_for_done_ctx(inst->ctx, MTK_INST_IRQ_RECEIVED,
					       WAIT_INTR_TIMEOUT_MS, MTK_VDEC_CORE);
	if (timeout)
		mtk_vdec_err(ctx, "core decode timeout: pic_%d", ctx->decoded_frame_cnt);
	inst->vsi_core->dec.timeout = !!timeout;

	vpu_dec_core_end(vpu);
	mtk_vdec_debug(ctx, "pic[%d] crc: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",
		       ctx->decoded_frame_cnt,
		       inst->vsi_core->dec.crc[0], inst->vsi_core->dec.crc[1],
		       inst->vsi_core->dec.crc[2], inst->vsi_core->dec.crc[3],
		       inst->vsi_core->dec.crc[4], inst->vsi_core->dec.crc[5],
		       inst->vsi_core->dec.crc[6], inst->vsi_core->dec.crc[7]);

vdec_dec_end:
	vdec_msg_queue_update_ube_rptr(&lat_buf->ctx->msg_queue, share_info->trans_end);
	ctx->dev->vdec_pdata->cap_to_disp(ctx, !!err, lat_buf->src_buf_req);
	mtk_vdec_debug(ctx, "core decode done err=%d", err);
	ctx->decoded_frame_cnt++;
	return 0;
}

static void vdec_h264_insert_startcode(struct mtk_vcodec_dec_dev *vcodec_dev, unsigned char *buf,
				       size_t *bs_size, struct mtk_h264_pps_param *pps)
{
	struct device *dev = &vcodec_dev->plat_dev->dev;

	/* Need to add pending data at the end of bitstream when bs_sz is small than
	 * 20 bytes for cavlc bitstream, or lat will decode fail. This pending data is
	 * useful for mt8192 and mt8195 platform.
	 *
	 * cavlc bitstream when entropy_coding_mode_flag is false.
	 */
	if (pps->entropy_coding_mode_flag || *bs_size > 20 ||
	    !(of_device_is_compatible(dev->of_node, "mediatek,mt8192-vcodec-dec") ||
	    of_device_is_compatible(dev->of_node, "mediatek,mt8195-vcodec-dec")))
		return;

	buf[*bs_size] = 0;
	buf[*bs_size + 1] = 0;
	buf[*bs_size + 2] = 1;
	buf[*bs_size + 3] = 0xff;
	(*bs_size) += 4;
}

static int vdec_h264_slice_lat_decode(void *h_vdec, struct mtk_vcodec_mem *bs,
				      struct vdec_fb *fb, bool *res_chg)
{
	struct vdec_h264_slice_inst *inst = h_vdec;
	struct vdec_vpu_inst *vpu = &inst->vpu;
	struct mtk_video_dec_buf *src_buf_info;
	int nal_start_idx, err, timeout = 0, i;
	unsigned int data[2];
	struct vdec_lat_buf *lat_buf;
	struct vdec_h264_slice_share_info *share_info;
	unsigned char *buf;
	struct mtk_vcodec_mem *mem;

	if (vdec_msg_queue_init(&inst->ctx->msg_queue, inst->ctx,
				vdec_h264_slice_core_decode,
				sizeof(*share_info)))
		return -ENOMEM;

	/* bs NULL means flush decoder */
	if (!bs) {
		vdec_msg_queue_wait_lat_buf_full(&inst->ctx->msg_queue);
		return vpu_dec_reset(vpu);
	}

	if (inst->is_field_bitstream)
		return -EINVAL;

	lat_buf = vdec_msg_queue_dqbuf(&inst->ctx->msg_queue.lat_ctx);
	if (!lat_buf) {
		mtk_vdec_debug(inst->ctx, "failed to get lat buffer");
		return -EAGAIN;
	}
	share_info = lat_buf->private_data;
	src_buf_info = container_of(bs, struct mtk_video_dec_buf, bs_buffer);

	buf = (unsigned char *)bs->va;
	nal_start_idx = mtk_vdec_h264_find_start_code(buf, bs->size);
	if (nal_start_idx < 0) {
		err = -EINVAL;
		goto err_free_fb_out;
	}

	inst->vsi->dec.nal_info = buf[nal_start_idx];
	lat_buf->src_buf_req = src_buf_info->m2m_buf.vb.vb2_buf.req_obj.req;
	v4l2_m2m_buf_copy_metadata(&src_buf_info->m2m_buf.vb, &lat_buf->ts_info, true);

	err = vdec_h264_slice_fill_decode_parameters(inst, share_info);
	if (err)
		goto err_free_fb_out;

	vdec_h264_insert_startcode(inst->ctx->dev, buf, &bs->size,
				   &share_info->h264_slice_params.pps);

	inst->vsi->dec.bs_buf_addr = (uint64_t)bs->dma_addr;
	inst->vsi->dec.bs_buf_size = bs->size;

	*res_chg = inst->resolution_changed;
	if (inst->resolution_changed) {
		mtk_vdec_debug(inst->ctx, "- resolution changed -");
		if (inst->realloc_mv_buf) {
			err = vdec_h264_slice_alloc_mv_buf(inst, &inst->ctx->picinfo);
			inst->realloc_mv_buf = false;
			if (err)
				goto err_free_fb_out;
		}
		inst->resolution_changed = false;
	}
	for (i = 0; i < H264_MAX_MV_NUM; i++) {
		mem = &inst->mv_buf[i];
		inst->vsi->mv_buf_dma[i] = mem->dma_addr;
	}
	inst->vsi->wdma_start_addr = lat_buf->ctx->msg_queue.wdma_addr.dma_addr;
	inst->vsi->wdma_end_addr = lat_buf->ctx->msg_queue.wdma_addr.dma_addr +
		lat_buf->ctx->msg_queue.wdma_addr.size;
	inst->vsi->wdma_err_addr = lat_buf->wdma_err_addr.dma_addr;
	inst->vsi->slice_bc_start_addr = lat_buf->slice_bc_addr.dma_addr;
	inst->vsi->slice_bc_end_addr = lat_buf->slice_bc_addr.dma_addr +
		lat_buf->slice_bc_addr.size;

	inst->vsi->trans_end = inst->ctx->msg_queue.wdma_rptr_addr;
	inst->vsi->trans_start = inst->ctx->msg_queue.wdma_wptr_addr;
	mtk_vdec_debug(inst->ctx, "lat:trans(0x%llx 0x%llx) err:0x%llx",
		       inst->vsi->wdma_start_addr,
		       inst->vsi->wdma_end_addr,
		       inst->vsi->wdma_err_addr);

	mtk_vdec_debug(inst->ctx, "slice(0x%llx 0x%llx) rprt((0x%llx 0x%llx))",
		       inst->vsi->slice_bc_start_addr,
		       inst->vsi->slice_bc_end_addr,
		       inst->vsi->trans_start,
		       inst->vsi->trans_end);
	err = vpu_dec_start(vpu, data, 2);
	if (err) {
		mtk_vdec_debug(inst->ctx, "lat decode err: %d", err);
		goto err_free_fb_out;
	}

	share_info->trans_end = inst->ctx->msg_queue.wdma_addr.dma_addr +
		inst->vsi->wdma_end_addr_offset;
	share_info->trans_start = inst->ctx->msg_queue.wdma_wptr_addr;
	share_info->nal_info = inst->vsi->dec.nal_info;

	if (IS_VDEC_INNER_RACING(inst->ctx->dev->dec_capability)) {
		memcpy(&share_info->h264_slice_params, &inst->vsi->h264_slice_params,
		       sizeof(share_info->h264_slice_params));
		vdec_msg_queue_qbuf(&inst->ctx->msg_queue.core_ctx, lat_buf);
	}

	/* wait decoder done interrupt */
	timeout = mtk_vcodec_wait_for_done_ctx(inst->ctx, MTK_INST_IRQ_RECEIVED,
					       WAIT_INTR_TIMEOUT_MS, MTK_VDEC_LAT0);
	if (timeout)
		mtk_vdec_err(inst->ctx, "lat decode timeout: pic_%d", inst->slice_dec_num);
	inst->vsi->dec.timeout = !!timeout;

	err = vpu_dec_end(vpu);
	if (err == SLICE_HEADER_FULL || err == TRANS_BUFFER_FULL) {
		if (!IS_VDEC_INNER_RACING(inst->ctx->dev->dec_capability))
			vdec_msg_queue_qbuf(&inst->ctx->msg_queue.lat_ctx, lat_buf);
		inst->slice_dec_num++;
		mtk_vdec_err(inst->ctx, "lat dec fail: pic_%d err:%d", inst->slice_dec_num, err);
		return -EINVAL;
	}

	share_info->trans_end = inst->ctx->msg_queue.wdma_addr.dma_addr +
		inst->vsi->wdma_end_addr_offset;
	vdec_msg_queue_update_ube_wptr(&lat_buf->ctx->msg_queue, share_info->trans_end);

	if (!IS_VDEC_INNER_RACING(inst->ctx->dev->dec_capability)) {
		memcpy(&share_info->h264_slice_params, &inst->vsi->h264_slice_params,
		       sizeof(share_info->h264_slice_params));
		vdec_msg_queue_qbuf(&inst->ctx->msg_queue.core_ctx, lat_buf);
	}
	mtk_vdec_debug(inst->ctx, "dec num: %d lat crc: 0x%x 0x%x 0x%x", inst->slice_dec_num,
		       inst->vsi->dec.crc[0], inst->vsi->dec.crc[1], inst->vsi->dec.crc[2]);

	inst->slice_dec_num++;
	return 0;
err_free_fb_out:
	vdec_msg_queue_qbuf(&inst->ctx->msg_queue.lat_ctx, lat_buf);
	mtk_vdec_err(inst->ctx, "slice dec number: %d err: %d", inst->slice_dec_num, err);
	return err;
}

static int vdec_h264_slice_single_decode(void *h_vdec, struct mtk_vcodec_mem *bs,
					 struct vdec_fb *unused, bool *res_chg)
{
	struct vdec_h264_slice_inst *inst = h_vdec;
	struct vdec_vpu_inst *vpu = &inst->vpu;
	struct mtk_video_dec_buf *src_buf_info, *dst_buf_info;
	struct vdec_fb *fb;
	unsigned char *buf;
	unsigned int data[2], i;
	u64 y_fb_dma, c_fb_dma;
	struct mtk_vcodec_mem *mem;
	int err, nal_start_idx;

	/* bs NULL means flush decoder */
	if (!bs)
		return vpu_dec_reset(vpu);

	fb = inst->ctx->dev->vdec_pdata->get_cap_buffer(inst->ctx);
	src_buf_info = container_of(bs, struct mtk_video_dec_buf, bs_buffer);
	dst_buf_info = container_of(fb, struct mtk_video_dec_buf, frame_buffer);

	y_fb_dma = fb ? (u64)fb->base_y.dma_addr : 0;
	c_fb_dma = fb ? (u64)fb->base_c.dma_addr : 0;
	mtk_vdec_debug(inst->ctx, "[h264-dec] [%d] y_dma=%llx c_dma=%llx",
		       inst->ctx->decoded_frame_cnt, y_fb_dma, c_fb_dma);

	inst->vsi_ctx.dec.bs_buf_addr = (u64)bs->dma_addr;
	inst->vsi_ctx.dec.bs_buf_size = bs->size;
	inst->vsi_ctx.dec.y_fb_dma = y_fb_dma;
	inst->vsi_ctx.dec.c_fb_dma = c_fb_dma;
	inst->vsi_ctx.dec.vdec_fb_va = (u64)(uintptr_t)fb;

	v4l2_m2m_buf_copy_metadata(&src_buf_info->m2m_buf.vb,
				   &dst_buf_info->m2m_buf.vb, true);
	err = get_vdec_sig_decode_parameters(inst);
	if (err)
		goto err_free_fb_out;

	buf = (unsigned char *)bs->va;
	nal_start_idx = mtk_vdec_h264_find_start_code(buf, bs->size);
	if (nal_start_idx < 0) {
		err = -EINVAL;
		goto err_free_fb_out;
	}
	inst->vsi_ctx.dec.nal_info = buf[nal_start_idx];

	*res_chg = inst->resolution_changed;
	if (inst->resolution_changed) {
		mtk_vdec_debug(inst->ctx, "- resolution changed -");
		if (inst->realloc_mv_buf) {
			err = vdec_h264_slice_alloc_mv_buf(inst, &inst->ctx->picinfo);
			inst->realloc_mv_buf = false;
			if (err)
				goto err_free_fb_out;
		}
		inst->resolution_changed = false;

		for (i = 0; i < H264_MAX_MV_NUM; i++) {
			mem = &inst->mv_buf[i];
			inst->vsi_ctx.mv_buf_dma[i] = mem->dma_addr;
		}
	}

	memcpy(inst->vpu.vsi, &inst->vsi_ctx, sizeof(inst->vsi_ctx));
	err = vpu_dec_start(vpu, data, 2);
	if (err)
		goto err_free_fb_out;

	/* wait decoder done interrupt */
	err = mtk_vcodec_wait_for_done_ctx(inst->ctx, MTK_INST_IRQ_RECEIVED,
					   WAIT_INTR_TIMEOUT_MS, MTK_VDEC_CORE);
	if (err)
		mtk_vdec_err(inst->ctx, "decode timeout: pic_%d", inst->ctx->decoded_frame_cnt);

	inst->vsi->dec.timeout = !!err;
	err = vpu_dec_end(vpu);
	if (err)
		goto err_free_fb_out;

	memcpy(&inst->vsi_ctx, inst->vpu.vsi, sizeof(inst->vsi_ctx));
	mtk_vdec_debug(inst->ctx, "pic[%d] crc: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",
		       inst->ctx->decoded_frame_cnt,
		       inst->vsi_ctx.dec.crc[0], inst->vsi_ctx.dec.crc[1],
		       inst->vsi_ctx.dec.crc[2], inst->vsi_ctx.dec.crc[3],
		       inst->vsi_ctx.dec.crc[4], inst->vsi_ctx.dec.crc[5],
		       inst->vsi_ctx.dec.crc[6], inst->vsi_ctx.dec.crc[7]);

	inst->ctx->decoded_frame_cnt++;
	return 0;

err_free_fb_out:
	mtk_vdec_err(inst->ctx, "dec frame number: %d err: %d", inst->ctx->decoded_frame_cnt, err);
	return err;
}

static int vdec_h264_slice_decode(void *h_vdec, struct mtk_vcodec_mem *bs,
				  struct vdec_fb *unused, bool *res_chg)
{
	struct vdec_h264_slice_inst *inst = h_vdec;
	int ret;

	if (!h_vdec)
		return -EINVAL;

	if (inst->ctx->dev->vdec_pdata->hw_arch == MTK_VDEC_PURE_SINGLE_CORE)
		ret = vdec_h264_slice_single_decode(h_vdec, bs, unused, res_chg);
	else
		ret = vdec_h264_slice_lat_decode(h_vdec, bs, unused, res_chg);

	return ret;
}

static int vdec_h264_slice_get_param(void *h_vdec, enum vdec_get_param_type type,
				     void *out)
{
	struct vdec_h264_slice_inst *inst = h_vdec;

	switch (type) {
	case GET_PARAM_PIC_INFO:
		vdec_h264_slice_get_pic_info(inst);
		break;
	case GET_PARAM_DPB_SIZE:
		*(unsigned int *)out = 6;
		break;
	case GET_PARAM_CROP_INFO:
		vdec_h264_slice_get_crop_info(inst, out);
		break;
	default:
		mtk_vdec_err(inst->ctx, "invalid get parameter type=%d", type);
		return -EINVAL;
	}
	return 0;
}

const struct vdec_common_if vdec_h264_slice_multi_if = {
	.init		= vdec_h264_slice_init,
	.decode		= vdec_h264_slice_decode,
	.get_param	= vdec_h264_slice_get_param,
	.deinit		= vdec_h264_slice_deinit,
};
