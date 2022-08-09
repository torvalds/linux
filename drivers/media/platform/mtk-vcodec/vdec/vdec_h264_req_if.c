// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/slab.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-h264.h>
#include <media/videobuf2-dma-contig.h>

#include "../mtk_vcodec_util.h"
#include "../mtk_vcodec_dec.h"
#include "../mtk_vcodec_intr.h"
#include "../vdec_drv_base.h"
#include "../vdec_drv_if.h"
#include "../vdec_vpu_if.h"

#define BUF_PREDICTION_SZ			(64 * 4096)
#define MB_UNIT_LEN				16

/* get used parameters for sps/pps */
#define GET_MTK_VDEC_FLAG(cond, flag) \
	{ dst_param->cond = ((src_param->flags & (flag)) ? (1) : (0)); }
#define GET_MTK_VDEC_PARAM(param) \
	{ dst_param->param = src_param->param; }
/* motion vector size (bytes) for every macro block */
#define HW_MB_STORE_SZ				64

#define H264_MAX_FB_NUM				17
#define H264_MAX_MV_NUM				32
#define HDR_PARSING_BUF_SZ			1024

/**
 * struct mtk_h264_dpb_info  - h264 dpb information
 * @y_dma_addr: Y bitstream physical address
 * @c_dma_addr: CbCr bitstream physical address
 * @reference_flag: reference picture flag (short/long term reference picture)
 * @field: field picture flag
 */
struct mtk_h264_dpb_info {
	dma_addr_t y_dma_addr;
	dma_addr_t c_dma_addr;
	int reference_flag;
	int field;
};

/*
 * struct mtk_h264_sps_param  - parameters for sps
 */
struct mtk_h264_sps_param {
	unsigned char chroma_format_idc;
	unsigned char bit_depth_luma_minus8;
	unsigned char bit_depth_chroma_minus8;
	unsigned char log2_max_frame_num_minus4;
	unsigned char pic_order_cnt_type;
	unsigned char log2_max_pic_order_cnt_lsb_minus4;
	unsigned char max_num_ref_frames;
	unsigned char separate_colour_plane_flag;
	unsigned short pic_width_in_mbs_minus1;
	unsigned short pic_height_in_map_units_minus1;
	unsigned int max_frame_nums;
	unsigned char qpprime_y_zero_transform_bypass_flag;
	unsigned char delta_pic_order_always_zero_flag;
	unsigned char frame_mbs_only_flag;
	unsigned char mb_adaptive_frame_field_flag;
	unsigned char direct_8x8_inference_flag;
	unsigned char reserved[3];
};

/*
 * struct mtk_h264_pps_param  - parameters for pps
 */
struct mtk_h264_pps_param {
	unsigned char num_ref_idx_l0_default_active_minus1;
	unsigned char num_ref_idx_l1_default_active_minus1;
	unsigned char weighted_bipred_idc;
	char pic_init_qp_minus26;
	char chroma_qp_index_offset;
	char second_chroma_qp_index_offset;
	unsigned char entropy_coding_mode_flag;
	unsigned char pic_order_present_flag;
	unsigned char deblocking_filter_control_present_flag;
	unsigned char constrained_intra_pred_flag;
	unsigned char weighted_pred_flag;
	unsigned char redundant_pic_cnt_present_flag;
	unsigned char transform_8x8_mode_flag;
	unsigned char scaling_matrix_present_flag;
	unsigned char reserved[2];
};

struct slice_api_h264_scaling_matrix {
	unsigned char scaling_list_4x4[6][16];
	unsigned char scaling_list_8x8[6][64];
};

struct slice_h264_dpb_entry {
	unsigned long long reference_ts;
	unsigned short frame_num;
	unsigned short pic_num;
	/* Note that field is indicated by v4l2_buffer.field */
	int top_field_order_cnt;
	int bottom_field_order_cnt;
	unsigned int flags; /* V4L2_H264_DPB_ENTRY_FLAG_* */
};

/*
 * struct slice_api_h264_decode_param - parameters for decode.
 */
struct slice_api_h264_decode_param {
	struct slice_h264_dpb_entry dpb[16];
	unsigned short num_slices;
	unsigned short nal_ref_idc;
	unsigned char ref_pic_list_p0[32];
	unsigned char ref_pic_list_b0[32];
	unsigned char ref_pic_list_b1[32];
	int top_field_order_cnt;
	int bottom_field_order_cnt;
	unsigned int flags; /* V4L2_H264_DECODE_PARAM_FLAG_* */
};

/*
 * struct mtk_h264_dec_slice_param  - parameters for decode current frame
 */
struct mtk_h264_dec_slice_param {
	struct mtk_h264_sps_param			sps;
	struct mtk_h264_pps_param			pps;
	struct slice_api_h264_scaling_matrix		scaling_matrix;
	struct slice_api_h264_decode_param		decode_params;
	struct mtk_h264_dpb_info h264_dpb_info[16];
};

/**
 * struct h264_fb - h264 decode frame buffer information
 * @vdec_fb_va  : virtual address of struct vdec_fb
 * @y_fb_dma    : dma address of Y frame buffer (luma)
 * @c_fb_dma    : dma address of C frame buffer (chroma)
 * @poc         : picture order count of frame buffer
 * @reserved    : for 8 bytes alignment
 */
struct h264_fb {
	u64 vdec_fb_va;
	u64 y_fb_dma;
	u64 c_fb_dma;
	s32 poc;
	u32 reserved;
};

/**
 * struct vdec_h264_dec_info - decode information
 * @dpb_sz		: decoding picture buffer size
 * @resolution_changed  : resoltion change happen
 * @realloc_mv_buf	: flag to notify driver to re-allocate mv buffer
 * @cap_num_planes	: number planes of capture buffer
 * @bs_dma		: Input bit-stream buffer dma address
 * @y_fb_dma		: Y frame buffer dma address
 * @c_fb_dma		: C frame buffer dma address
 * @vdec_fb_va		: VDEC frame buffer struct virtual address
 */
struct vdec_h264_dec_info {
	u32 dpb_sz;
	u32 resolution_changed;
	u32 realloc_mv_buf;
	u32 cap_num_planes;
	u64 bs_dma;
	u64 y_fb_dma;
	u64 c_fb_dma;
	u64 vdec_fb_va;
};

/**
 * struct vdec_h264_vsi - shared memory for decode information exchange
 *                        between VPU and Host.
 *                        The memory is allocated by VPU then mapping to Host
 *                        in vpu_dec_init() and freed in vpu_dec_deinit()
 *                        by VPU.
 *                        AP-W/R : AP is writer/reader on this item
 *                        VPU-W/R: VPU is write/reader on this item
 * @pred_buf_dma : HW working predication buffer dma address (AP-W, VPU-R)
 * @mv_buf_dma   : HW working motion vector buffer dma address (AP-W, VPU-R)
 * @dec          : decode information (AP-R, VPU-W)
 * @pic          : picture information (AP-R, VPU-W)
 * @crop         : crop information (AP-R, VPU-W)
 * @h264_slice_params : the parameters that hardware use to decode
 */
struct vdec_h264_vsi {
	u64 pred_buf_dma;
	u64 mv_buf_dma[H264_MAX_MV_NUM];
	struct vdec_h264_dec_info dec;
	struct vdec_pic_info pic;
	struct v4l2_rect crop;
	struct mtk_h264_dec_slice_param h264_slice_params;
};

/**
 * struct vdec_h264_slice_inst - h264 decoder instance
 * @num_nalu : how many nalus be decoded
 * @ctx      : point to mtk_vcodec_ctx
 * @pred_buf : HW working predication buffer
 * @mv_buf   : HW working motion vector buffer
 * @vpu      : VPU instance
 * @vsi_ctx  : Local VSI data for this decoding context
 * @h264_slice_param : the parameters that hardware use to decode
 * @dpb : decoded picture buffer used to store reference buffer information
 */
struct vdec_h264_slice_inst {
	unsigned int num_nalu;
	struct mtk_vcodec_ctx *ctx;
	struct mtk_vcodec_mem pred_buf;
	struct mtk_vcodec_mem mv_buf[H264_MAX_MV_NUM];
	struct vdec_vpu_inst vpu;
	struct vdec_h264_vsi vsi_ctx;
	struct mtk_h264_dec_slice_param h264_slice_param;

	struct v4l2_h264_dpb_entry dpb[16];
};

static void *get_ctrl_ptr(struct mtk_vcodec_ctx *ctx, int id)
{
	struct v4l2_ctrl *ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl, id);

	return ctrl->p_cur.p;
}

static void get_h264_dpb_list(struct vdec_h264_slice_inst *inst,
			      struct mtk_h264_dec_slice_param *slice_param)
{
	struct vb2_queue *vq;
	struct vb2_buffer *vb;
	struct vb2_v4l2_buffer *vb2_v4l2;
	u64 index;

	vq = v4l2_m2m_get_vq(inst->ctx->m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	for (index = 0; index < ARRAY_SIZE(slice_param->decode_params.dpb); index++) {
		const struct slice_h264_dpb_entry *dpb;
		int vb2_index;

		dpb = &slice_param->decode_params.dpb[index];
		if (!(dpb->flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE)) {
			slice_param->h264_dpb_info[index].reference_flag = 0;
			continue;
		}

		vb2_index = vb2_find_timestamp(vq, dpb->reference_ts, 0);
		if (vb2_index < 0) {
			mtk_vcodec_err(inst, "Reference invalid: dpb_index(%lld) reference_ts(%lld)",
				       index, dpb->reference_ts);
			continue;
		}
		/* 1 for short term reference, 2 for long term reference */
		if (!(dpb->flags & V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM))
			slice_param->h264_dpb_info[index].reference_flag = 1;
		else
			slice_param->h264_dpb_info[index].reference_flag = 2;

		vb = vq->bufs[vb2_index];
		vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
		slice_param->h264_dpb_info[index].field = vb2_v4l2->field;

		slice_param->h264_dpb_info[index].y_dma_addr =
			vb2_dma_contig_plane_dma_addr(vb, 0);
		if (inst->ctx->q_data[MTK_Q_DATA_DST].fmt->num_planes == 2) {
			slice_param->h264_dpb_info[index].c_dma_addr =
				vb2_dma_contig_plane_dma_addr(vb, 1);
		}
	}
}

static void get_h264_sps_parameters(struct mtk_h264_sps_param *dst_param,
				    const struct v4l2_ctrl_h264_sps *src_param)
{
	GET_MTK_VDEC_PARAM(chroma_format_idc);
	GET_MTK_VDEC_PARAM(bit_depth_luma_minus8);
	GET_MTK_VDEC_PARAM(bit_depth_chroma_minus8);
	GET_MTK_VDEC_PARAM(log2_max_frame_num_minus4);
	GET_MTK_VDEC_PARAM(pic_order_cnt_type);
	GET_MTK_VDEC_PARAM(log2_max_pic_order_cnt_lsb_minus4);
	GET_MTK_VDEC_PARAM(max_num_ref_frames);
	GET_MTK_VDEC_PARAM(pic_width_in_mbs_minus1);
	GET_MTK_VDEC_PARAM(pic_height_in_map_units_minus1);

	GET_MTK_VDEC_FLAG(separate_colour_plane_flag,
			  V4L2_H264_SPS_FLAG_SEPARATE_COLOUR_PLANE);
	GET_MTK_VDEC_FLAG(qpprime_y_zero_transform_bypass_flag,
			  V4L2_H264_SPS_FLAG_QPPRIME_Y_ZERO_TRANSFORM_BYPASS);
	GET_MTK_VDEC_FLAG(delta_pic_order_always_zero_flag,
			  V4L2_H264_SPS_FLAG_DELTA_PIC_ORDER_ALWAYS_ZERO);
	GET_MTK_VDEC_FLAG(frame_mbs_only_flag,
			  V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY);
	GET_MTK_VDEC_FLAG(mb_adaptive_frame_field_flag,
			  V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD);
	GET_MTK_VDEC_FLAG(direct_8x8_inference_flag,
			  V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE);
}

static void get_h264_pps_parameters(struct mtk_h264_pps_param *dst_param,
				    const struct v4l2_ctrl_h264_pps *src_param)
{
	GET_MTK_VDEC_PARAM(num_ref_idx_l0_default_active_minus1);
	GET_MTK_VDEC_PARAM(num_ref_idx_l1_default_active_minus1);
	GET_MTK_VDEC_PARAM(weighted_bipred_idc);
	GET_MTK_VDEC_PARAM(pic_init_qp_minus26);
	GET_MTK_VDEC_PARAM(chroma_qp_index_offset);
	GET_MTK_VDEC_PARAM(second_chroma_qp_index_offset);

	GET_MTK_VDEC_FLAG(entropy_coding_mode_flag,
			  V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE);
	GET_MTK_VDEC_FLAG(pic_order_present_flag,
			  V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT);
	GET_MTK_VDEC_FLAG(weighted_pred_flag,
			  V4L2_H264_PPS_FLAG_WEIGHTED_PRED);
	GET_MTK_VDEC_FLAG(deblocking_filter_control_present_flag,
			  V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT);
	GET_MTK_VDEC_FLAG(constrained_intra_pred_flag,
			  V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED);
	GET_MTK_VDEC_FLAG(redundant_pic_cnt_present_flag,
			  V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT);
	GET_MTK_VDEC_FLAG(transform_8x8_mode_flag,
			  V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE);
	GET_MTK_VDEC_FLAG(scaling_matrix_present_flag,
			  V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT);
}

static void
get_h264_scaling_matrix(struct slice_api_h264_scaling_matrix *dst_matrix,
			const struct v4l2_ctrl_h264_scaling_matrix *src_matrix)
{
	memcpy(dst_matrix->scaling_list_4x4, src_matrix->scaling_list_4x4,
	       sizeof(dst_matrix->scaling_list_4x4));

	memcpy(dst_matrix->scaling_list_8x8, src_matrix->scaling_list_8x8,
	       sizeof(dst_matrix->scaling_list_8x8));
}

static void
get_h264_decode_parameters(struct slice_api_h264_decode_param *dst_params,
			   const struct v4l2_ctrl_h264_decode_params *src_params,
			   const struct v4l2_h264_dpb_entry dpb[V4L2_H264_NUM_DPB_ENTRIES])
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dst_params->dpb); i++) {
		struct slice_h264_dpb_entry *dst_entry = &dst_params->dpb[i];
		const struct v4l2_h264_dpb_entry *src_entry = &dpb[i];

		dst_entry->reference_ts = src_entry->reference_ts;
		dst_entry->frame_num = src_entry->frame_num;
		dst_entry->pic_num = src_entry->pic_num;
		dst_entry->top_field_order_cnt = src_entry->top_field_order_cnt;
		dst_entry->bottom_field_order_cnt =
			src_entry->bottom_field_order_cnt;
		dst_entry->flags = src_entry->flags;
	}

	/*
	 * num_slices is a leftover from the old H.264 support and is ignored
	 * by the firmware.
	 */
	dst_params->num_slices = 0;
	dst_params->nal_ref_idc = src_params->nal_ref_idc;
	dst_params->top_field_order_cnt = src_params->top_field_order_cnt;
	dst_params->bottom_field_order_cnt = src_params->bottom_field_order_cnt;
	dst_params->flags = src_params->flags;
}

static bool dpb_entry_match(const struct v4l2_h264_dpb_entry *a,
			    const struct v4l2_h264_dpb_entry *b)
{
	return a->top_field_order_cnt == b->top_field_order_cnt &&
	       a->bottom_field_order_cnt == b->bottom_field_order_cnt;
}

/*
 * Move DPB entries of dec_param that refer to a frame already existing in dpb
 * into the already existing slot in dpb, and move other entries into new slots.
 *
 * This function is an adaptation of the similarly-named function in
 * hantro_h264.c.
 */
static void update_dpb(const struct v4l2_ctrl_h264_decode_params *dec_param,
		       struct v4l2_h264_dpb_entry *dpb)
{
	DECLARE_BITMAP(new, ARRAY_SIZE(dec_param->dpb)) = { 0, };
	DECLARE_BITMAP(in_use, ARRAY_SIZE(dec_param->dpb)) = { 0, };
	DECLARE_BITMAP(used, ARRAY_SIZE(dec_param->dpb)) = { 0, };
	unsigned int i, j;

	/* Disable all entries by default, and mark the ones in use. */
	for (i = 0; i < ARRAY_SIZE(dec_param->dpb); i++) {
		if (dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE)
			set_bit(i, in_use);
		dpb[i].flags &= ~V4L2_H264_DPB_ENTRY_FLAG_ACTIVE;
	}

	/* Try to match new DPB entries with existing ones by their POCs. */
	for (i = 0; i < ARRAY_SIZE(dec_param->dpb); i++) {
		const struct v4l2_h264_dpb_entry *ndpb = &dec_param->dpb[i];

		if (!(ndpb->flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE))
			continue;

		/*
		 * To cut off some comparisons, iterate only on target DPB
		 * entries were already used.
		 */
		for_each_set_bit(j, in_use, ARRAY_SIZE(dec_param->dpb)) {
			struct v4l2_h264_dpb_entry *cdpb;

			cdpb = &dpb[j];
			if (!dpb_entry_match(cdpb, ndpb))
				continue;

			*cdpb = *ndpb;
			set_bit(j, used);
			/* Don't reiterate on this one. */
			clear_bit(j, in_use);
			break;
		}

		if (j == ARRAY_SIZE(dec_param->dpb))
			set_bit(i, new);
	}

	/* For entries that could not be matched, use remaining free slots. */
	for_each_set_bit(i, new, ARRAY_SIZE(dec_param->dpb)) {
		const struct v4l2_h264_dpb_entry *ndpb = &dec_param->dpb[i];
		struct v4l2_h264_dpb_entry *cdpb;

		/*
		 * Both arrays are of the same sizes, so there is no way
		 * we can end up with no space in target array, unless
		 * something is buggy.
		 */
		j = find_first_zero_bit(used, ARRAY_SIZE(dec_param->dpb));
		if (WARN_ON(j >= ARRAY_SIZE(dec_param->dpb)))
			return;

		cdpb = &dpb[j];
		*cdpb = *ndpb;
		set_bit(j, used);
	}
}

/*
 * The firmware expects unused reflist entries to have the value 0x20.
 */
static void fixup_ref_list(u8 *ref_list, size_t num_valid)
{
	memset(&ref_list[num_valid], 0x20, 32 - num_valid);
}

static void get_vdec_decode_parameters(struct vdec_h264_slice_inst *inst)
{
	const struct v4l2_ctrl_h264_decode_params *dec_params =
		get_ctrl_ptr(inst->ctx, V4L2_CID_STATELESS_H264_DECODE_PARAMS);
	const struct v4l2_ctrl_h264_sps *sps =
		get_ctrl_ptr(inst->ctx, V4L2_CID_STATELESS_H264_SPS);
	const struct v4l2_ctrl_h264_pps *pps =
		get_ctrl_ptr(inst->ctx, V4L2_CID_STATELESS_H264_PPS);
	const struct v4l2_ctrl_h264_scaling_matrix *scaling_matrix =
		get_ctrl_ptr(inst->ctx, V4L2_CID_STATELESS_H264_SCALING_MATRIX);
	struct mtk_h264_dec_slice_param *slice_param = &inst->h264_slice_param;
	struct v4l2_h264_reflist_builder reflist_builder;
	u8 *p0_reflist = slice_param->decode_params.ref_pic_list_p0;
	u8 *b0_reflist = slice_param->decode_params.ref_pic_list_b0;
	u8 *b1_reflist = slice_param->decode_params.ref_pic_list_b1;

	update_dpb(dec_params, inst->dpb);

	get_h264_sps_parameters(&slice_param->sps, sps);
	get_h264_pps_parameters(&slice_param->pps, pps);
	get_h264_scaling_matrix(&slice_param->scaling_matrix, scaling_matrix);
	get_h264_decode_parameters(&slice_param->decode_params, dec_params,
				   inst->dpb);
	get_h264_dpb_list(inst, slice_param);

	/* Build the reference lists */
	v4l2_h264_init_reflist_builder(&reflist_builder, dec_params, sps,
				       inst->dpb);
	v4l2_h264_build_p_ref_list(&reflist_builder, p0_reflist);
	v4l2_h264_build_b_ref_lists(&reflist_builder, b0_reflist, b1_reflist);
	/* Adapt the built lists to the firmware's expectations */
	fixup_ref_list(p0_reflist, reflist_builder.num_valid);
	fixup_ref_list(b0_reflist, reflist_builder.num_valid);
	fixup_ref_list(b1_reflist, reflist_builder.num_valid);

	memcpy(&inst->vsi_ctx.h264_slice_params, slice_param,
	       sizeof(inst->vsi_ctx.h264_slice_params));
}

static unsigned int get_mv_buf_size(unsigned int width, unsigned int height)
{
	int unit_size = (width / MB_UNIT_LEN) * (height / MB_UNIT_LEN) + 8;

	return HW_MB_STORE_SZ * unit_size;
}

static int allocate_predication_buf(struct vdec_h264_slice_inst *inst)
{
	int err;

	inst->pred_buf.size = BUF_PREDICTION_SZ;
	err = mtk_vcodec_mem_alloc(inst->ctx, &inst->pred_buf);
	if (err) {
		mtk_vcodec_err(inst, "failed to allocate ppl buf");
		return err;
	}

	inst->vsi_ctx.pred_buf_dma = inst->pred_buf.dma_addr;
	return 0;
}

static void free_predication_buf(struct vdec_h264_slice_inst *inst)
{
	struct mtk_vcodec_mem *mem = &inst->pred_buf;

	mtk_vcodec_debug_enter(inst);

	inst->vsi_ctx.pred_buf_dma = 0;
	if (mem->va)
		mtk_vcodec_mem_free(inst->ctx, mem);
}

static int alloc_mv_buf(struct vdec_h264_slice_inst *inst,
			struct vdec_pic_info *pic)
{
	int i;
	int err;
	struct mtk_vcodec_mem *mem = NULL;
	unsigned int buf_sz = get_mv_buf_size(pic->buf_w, pic->buf_h);

	mtk_v4l2_debug(3, "size = 0x%x", buf_sz);
	for (i = 0; i < H264_MAX_MV_NUM; i++) {
		mem = &inst->mv_buf[i];
		if (mem->va)
			mtk_vcodec_mem_free(inst->ctx, mem);
		mem->size = buf_sz;
		err = mtk_vcodec_mem_alloc(inst->ctx, mem);
		if (err) {
			mtk_vcodec_err(inst, "failed to allocate mv buf");
			return err;
		}
		inst->vsi_ctx.mv_buf_dma[i] = mem->dma_addr;
	}

	return 0;
}

static void free_mv_buf(struct vdec_h264_slice_inst *inst)
{
	int i;
	struct mtk_vcodec_mem *mem;

	for (i = 0; i < H264_MAX_MV_NUM; i++) {
		inst->vsi_ctx.mv_buf_dma[i] = 0;
		mem = &inst->mv_buf[i];
		if (mem->va)
			mtk_vcodec_mem_free(inst->ctx, mem);
	}
}

static void get_pic_info(struct vdec_h264_slice_inst *inst,
			 struct vdec_pic_info *pic)
{
	struct mtk_vcodec_ctx *ctx = inst->ctx;

	ctx->picinfo.buf_w = ALIGN(ctx->picinfo.pic_w, VCODEC_DEC_ALIGNED_64);
	ctx->picinfo.buf_h = ALIGN(ctx->picinfo.pic_h, VCODEC_DEC_ALIGNED_64);
	ctx->picinfo.fb_sz[0] = ctx->picinfo.buf_w * ctx->picinfo.buf_h;
	ctx->picinfo.fb_sz[1] = ctx->picinfo.fb_sz[0] >> 1;
	inst->vsi_ctx.dec.cap_num_planes =
		ctx->q_data[MTK_Q_DATA_DST].fmt->num_planes;

	*pic = ctx->picinfo;
	mtk_vcodec_debug(inst, "pic(%d, %d), buf(%d, %d)",
			 ctx->picinfo.pic_w, ctx->picinfo.pic_h,
			 ctx->picinfo.buf_w, ctx->picinfo.buf_h);
	mtk_vcodec_debug(inst, "Y/C(%d, %d)", ctx->picinfo.fb_sz[0],
			 ctx->picinfo.fb_sz[1]);

	if (ctx->last_decoded_picinfo.pic_w != ctx->picinfo.pic_w ||
	    ctx->last_decoded_picinfo.pic_h != ctx->picinfo.pic_h) {
		inst->vsi_ctx.dec.resolution_changed = true;
		if (ctx->last_decoded_picinfo.buf_w != ctx->picinfo.buf_w ||
		    ctx->last_decoded_picinfo.buf_h != ctx->picinfo.buf_h)
			inst->vsi_ctx.dec.realloc_mv_buf = true;

		mtk_v4l2_debug(1, "ResChg: (%d %d) : old(%d, %d) -> new(%d, %d)",
			       inst->vsi_ctx.dec.resolution_changed,
			       inst->vsi_ctx.dec.realloc_mv_buf,
			       ctx->last_decoded_picinfo.pic_w,
			       ctx->last_decoded_picinfo.pic_h,
			       ctx->picinfo.pic_w, ctx->picinfo.pic_h);
	}
}

static void get_crop_info(struct vdec_h264_slice_inst *inst, struct v4l2_rect *cr)
{
	cr->left = inst->vsi_ctx.crop.left;
	cr->top = inst->vsi_ctx.crop.top;
	cr->width = inst->vsi_ctx.crop.width;
	cr->height = inst->vsi_ctx.crop.height;

	mtk_vcodec_debug(inst, "l=%d, t=%d, w=%d, h=%d",
			 cr->left, cr->top, cr->width, cr->height);
}

static void get_dpb_size(struct vdec_h264_slice_inst *inst, unsigned int *dpb_sz)
{
	*dpb_sz = inst->vsi_ctx.dec.dpb_sz;
	mtk_vcodec_debug(inst, "sz=%d", *dpb_sz);
}

static int vdec_h264_slice_init(struct mtk_vcodec_ctx *ctx)
{
	struct vdec_h264_slice_inst *inst;
	int err;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	inst->ctx = ctx;

	inst->vpu.id = SCP_IPI_VDEC_H264;
	inst->vpu.ctx = ctx;

	err = vpu_dec_init(&inst->vpu);
	if (err) {
		mtk_vcodec_err(inst, "vdec_h264 init err=%d", err);
		goto error_free_inst;
	}

	memcpy(&inst->vsi_ctx, inst->vpu.vsi, sizeof(inst->vsi_ctx));
	inst->vsi_ctx.dec.resolution_changed = true;
	inst->vsi_ctx.dec.realloc_mv_buf = true;

	err = allocate_predication_buf(inst);
	if (err)
		goto error_deinit;

	mtk_vcodec_debug(inst, "struct size = %zu,%zu,%zu,%zu\n",
			 sizeof(struct mtk_h264_sps_param),
			 sizeof(struct mtk_h264_pps_param),
			 sizeof(struct mtk_h264_dec_slice_param),
			 sizeof(struct mtk_h264_dpb_info));

	mtk_vcodec_debug(inst, "H264 Instance >> %p", inst);

	ctx->drv_handle = inst;
	return 0;

error_deinit:
	vpu_dec_deinit(&inst->vpu);

error_free_inst:
	kfree(inst);
	return err;
}

static void vdec_h264_slice_deinit(void *h_vdec)
{
	struct vdec_h264_slice_inst *inst = h_vdec;

	mtk_vcodec_debug_enter(inst);

	vpu_dec_deinit(&inst->vpu);
	free_predication_buf(inst);
	free_mv_buf(inst);

	kfree(inst);
}

static int vdec_h264_slice_decode(void *h_vdec, struct mtk_vcodec_mem *bs,
				  struct vdec_fb *fb, bool *res_chg)
{
	struct vdec_h264_slice_inst *inst = h_vdec;
	const struct v4l2_ctrl_h264_decode_params *dec_params =
		get_ctrl_ptr(inst->ctx, V4L2_CID_STATELESS_H264_DECODE_PARAMS);
	struct vdec_vpu_inst *vpu = &inst->vpu;
	u32 data[2];
	u64 y_fb_dma;
	u64 c_fb_dma;
	int err;

	/* bs NULL means flush decoder */
	if (!bs)
		return vpu_dec_reset(vpu);

	y_fb_dma = fb ? (u64)fb->base_y.dma_addr : 0;
	c_fb_dma = fb ? (u64)fb->base_c.dma_addr : 0;

	mtk_vcodec_debug(inst, "+ [%d] FB y_dma=%llx c_dma=%llx va=%p",
			 ++inst->num_nalu, y_fb_dma, c_fb_dma, fb);

	inst->vsi_ctx.dec.bs_dma = (uint64_t)bs->dma_addr;
	inst->vsi_ctx.dec.y_fb_dma = y_fb_dma;
	inst->vsi_ctx.dec.c_fb_dma = c_fb_dma;
	inst->vsi_ctx.dec.vdec_fb_va = (u64)(uintptr_t)fb;

	get_vdec_decode_parameters(inst);
	data[0] = bs->size;
	/*
	 * Reconstruct the first byte of the NAL unit, as the firmware requests
	 * that information to be passed even though it is present in the stream
	 * itself...
	 */
	data[1] = (dec_params->nal_ref_idc << 5) |
		  ((dec_params->flags & V4L2_H264_DECODE_PARAM_FLAG_IDR_PIC)
			? 0x5 : 0x1);

	*res_chg = inst->vsi_ctx.dec.resolution_changed;
	if (*res_chg) {
		mtk_vcodec_debug(inst, "- resolution changed -");
		if (inst->vsi_ctx.dec.realloc_mv_buf) {
			err = alloc_mv_buf(inst, &inst->ctx->picinfo);
			inst->vsi_ctx.dec.realloc_mv_buf = false;
			if (err)
				goto err_free_fb_out;
		}
		*res_chg = false;
	}

	memcpy(inst->vpu.vsi, &inst->vsi_ctx, sizeof(inst->vsi_ctx));
	err = vpu_dec_start(vpu, data, 2);
	if (err)
		goto err_free_fb_out;

	/* wait decoder done interrupt */
	err = mtk_vcodec_wait_for_done_ctx(inst->ctx,
					   MTK_INST_IRQ_RECEIVED,
					   WAIT_INTR_TIMEOUT_MS);
	if (err)
		goto err_free_fb_out;
	vpu_dec_end(vpu);

	memcpy(&inst->vsi_ctx, inst->vpu.vsi, sizeof(inst->vsi_ctx));
	mtk_vcodec_debug(inst, "\n - NALU[%d]", inst->num_nalu);
	return 0;

err_free_fb_out:
	mtk_vcodec_err(inst, "\n - NALU[%d] err=%d -\n", inst->num_nalu, err);
	return err;
}

static int vdec_h264_slice_get_param(void *h_vdec, enum vdec_get_param_type type, void *out)
{
	struct vdec_h264_slice_inst *inst = h_vdec;

	switch (type) {
	case GET_PARAM_PIC_INFO:
		get_pic_info(inst, out);
		break;

	case GET_PARAM_DPB_SIZE:
		get_dpb_size(inst, out);
		break;

	case GET_PARAM_CROP_INFO:
		get_crop_info(inst, out);
		break;

	default:
		mtk_vcodec_err(inst, "invalid get parameter type=%d", type);
		return -EINVAL;
	}

	return 0;
}

const struct vdec_common_if vdec_h264_slice_if = {
	.init		= vdec_h264_slice_init,
	.decode		= vdec_h264_slice_decode,
	.get_param	= vdec_h264_slice_get_param,
	.deinit		= vdec_h264_slice_deinit,
};
