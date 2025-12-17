// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip Video Decoder HEVC backend
 *
 * Copyright (C) 2023 Collabora, Ltd.
 *      Sebastian Fricke <sebastian.fricke@collabora.com>
 *
 * Copyright (C) 2019 Collabora, Ltd.
 *	Boris Brezillon <boris.brezillon@collabora.com>
 *
 * Copyright (C) 2016 Rockchip Electronics Co., Ltd.
 *	Jeffy Chen <jeffy.chen@rock-chips.com>
 */

#include <media/v4l2-mem2mem.h>

#include "rkvdec.h"
#include "rkvdec-regs.h"
#include "rkvdec-hevc-data.c"

/* Size in u8/u32 units. */
#define RKV_SCALING_LIST_SIZE		1360
#define RKV_PPS_SIZE			(80 / 4)
#define RKV_PPS_LEN			64
#define RKV_RPS_SIZE			(32 / 4)
#define RKV_RPS_LEN			600

struct rkvdec_sps_pps_packet {
	u32 info[RKV_PPS_SIZE];
};

struct rkvdec_rps_packet {
	u32 info[RKV_RPS_SIZE];
};

struct rkvdec_ps_field {
	u16 offset;
	u8 len;
};

#define PS_FIELD(_offset, _len) \
	((struct rkvdec_ps_field){ _offset, _len })

/* SPS */
#define VIDEO_PARAMETER_SET_ID				PS_FIELD(0, 4)
#define SEQ_PARAMETER_SET_ID				PS_FIELD(4, 4)
#define CHROMA_FORMAT_IDC				PS_FIELD(8, 2)
#define PIC_WIDTH_IN_LUMA_SAMPLES			PS_FIELD(10, 13)
#define PIC_HEIGHT_IN_LUMA_SAMPLES			PS_FIELD(23, 13)
#define BIT_DEPTH_LUMA					PS_FIELD(36, 4)
#define BIT_DEPTH_CHROMA				PS_FIELD(40, 4)
#define LOG2_MAX_PIC_ORDER_CNT_LSB			PS_FIELD(44, 5)
#define LOG2_DIFF_MAX_MIN_LUMA_CODING_BLOCK_SIZE	PS_FIELD(49, 2)
#define LOG2_MIN_LUMA_CODING_BLOCK_SIZE			PS_FIELD(51, 3)
#define LOG2_MIN_TRANSFORM_BLOCK_SIZE			PS_FIELD(54, 3)
#define LOG2_DIFF_MAX_MIN_LUMA_TRANSFORM_BLOCK_SIZE	PS_FIELD(57, 2)
#define MAX_TRANSFORM_HIERARCHY_DEPTH_INTER		PS_FIELD(59, 3)
#define MAX_TRANSFORM_HIERARCHY_DEPTH_INTRA		PS_FIELD(62, 3)
#define SCALING_LIST_ENABLED_FLAG			PS_FIELD(65, 1)
#define AMP_ENABLED_FLAG				PS_FIELD(66, 1)
#define SAMPLE_ADAPTIVE_OFFSET_ENABLED_FLAG		PS_FIELD(67, 1)
#define PCM_ENABLED_FLAG				PS_FIELD(68, 1)
#define PCM_SAMPLE_BIT_DEPTH_LUMA			PS_FIELD(69, 4)
#define PCM_SAMPLE_BIT_DEPTH_CHROMA			PS_FIELD(73, 4)
#define PCM_LOOP_FILTER_DISABLED_FLAG			PS_FIELD(77, 1)
#define LOG2_DIFF_MAX_MIN_PCM_LUMA_CODING_BLOCK_SIZE	PS_FIELD(78, 3)
#define LOG2_MIN_PCM_LUMA_CODING_BLOCK_SIZE		PS_FIELD(81, 3)
#define NUM_SHORT_TERM_REF_PIC_SETS			PS_FIELD(84, 7)
#define LONG_TERM_REF_PICS_PRESENT_FLAG			PS_FIELD(91, 1)
#define NUM_LONG_TERM_REF_PICS_SPS			PS_FIELD(92, 6)
#define SPS_TEMPORAL_MVP_ENABLED_FLAG			PS_FIELD(98, 1)
#define STRONG_INTRA_SMOOTHING_ENABLED_FLAG		PS_FIELD(99, 1)
/* PPS */
#define PIC_PARAMETER_SET_ID				PS_FIELD(128, 6)
#define PPS_SEQ_PARAMETER_SET_ID			PS_FIELD(134, 4)
#define DEPENDENT_SLICE_SEGMENTS_ENABLED_FLAG		PS_FIELD(138, 1)
#define OUTPUT_FLAG_PRESENT_FLAG			PS_FIELD(139, 1)
#define NUM_EXTRA_SLICE_HEADER_BITS			PS_FIELD(140, 13)
#define SIGN_DATA_HIDING_ENABLED_FLAG			PS_FIELD(153, 1)
#define CABAC_INIT_PRESENT_FLAG				PS_FIELD(154, 1)
#define NUM_REF_IDX_L0_DEFAULT_ACTIVE			PS_FIELD(155, 4)
#define NUM_REF_IDX_L1_DEFAULT_ACTIVE			PS_FIELD(159, 4)
#define INIT_QP_MINUS26					PS_FIELD(163, 7)
#define CONSTRAINED_INTRA_PRED_FLAG			PS_FIELD(170, 1)
#define TRANSFORM_SKIP_ENABLED_FLAG			PS_FIELD(171, 1)
#define CU_QP_DELTA_ENABLED_FLAG			PS_FIELD(172, 1)
#define LOG2_MIN_CU_QP_DELTA_SIZE			PS_FIELD(173, 3)
#define PPS_CB_QP_OFFSET				PS_FIELD(176, 5)
#define PPS_CR_QP_OFFSET				PS_FIELD(181, 5)
#define PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT_FLAG	PS_FIELD(186, 1)
#define WEIGHTED_PRED_FLAG				PS_FIELD(187, 1)
#define WEIGHTED_BIPRED_FLAG				PS_FIELD(188, 1)
#define TRANSQUANT_BYPASS_ENABLED_FLAG			PS_FIELD(189, 1)
#define TILES_ENABLED_FLAG				PS_FIELD(190, 1)
#define ENTROPY_CODING_SYNC_ENABLED_FLAG		PS_FIELD(191, 1)
#define PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG	PS_FIELD(192, 1)
#define LOOP_FILTER_ACROSS_TILES_ENABLED_FLAG		PS_FIELD(193, 1)
#define DEBLOCKING_FILTER_OVERRIDE_ENABLED_FLAG		PS_FIELD(194, 1)
#define PPS_DEBLOCKING_FILTER_DISABLED_FLAG		PS_FIELD(195, 1)
#define PPS_BETA_OFFSET_DIV2				PS_FIELD(196, 4)
#define PPS_TC_OFFSET_DIV2				PS_FIELD(200, 4)
#define LISTS_MODIFICATION_PRESENT_FLAG			PS_FIELD(204, 1)
#define LOG2_PARALLEL_MERGE_LEVEL			PS_FIELD(205, 3)
#define SLICE_SEGMENT_HEADER_EXTENSION_PRESENT_FLAG	PS_FIELD(208, 1)
#define NUM_TILE_COLUMNS				PS_FIELD(212, 5)
#define NUM_TILE_ROWS					PS_FIELD(217, 5)
#define COLUMN_WIDTH(i)					PS_FIELD(256 + ((i) * 8), 8)
#define ROW_HEIGHT(i)					PS_FIELD(416 + ((i) * 8), 8)
#define SCALING_LIST_ADDRESS				PS_FIELD(592, 32)

/* Data structure describing auxiliary buffer format. */
struct rkvdec_hevc_priv_tbl {
	u8 cabac_table[RKV_CABAC_TABLE_SIZE];
	u8 scaling_list[RKV_SCALING_LIST_SIZE];
	struct rkvdec_sps_pps_packet param_set[RKV_PPS_LEN];
	struct rkvdec_rps_packet rps[RKV_RPS_LEN];
};

struct rkvdec_hevc_run {
	struct rkvdec_run base;
	const struct v4l2_ctrl_hevc_slice_params *slices_params;
	const struct v4l2_ctrl_hevc_decode_params *decode_params;
	const struct v4l2_ctrl_hevc_sps *sps;
	const struct v4l2_ctrl_hevc_pps *pps;
	const struct v4l2_ctrl_hevc_scaling_matrix *scaling_matrix;
	int num_slices;
};

struct rkvdec_hevc_ctx {
	struct rkvdec_aux_buf priv_tbl;
	struct v4l2_ctrl_hevc_scaling_matrix scaling_matrix_cache;
};

struct scaling_factor {
	u8 scalingfactor0[1248];
	u8 scalingfactor1[96];		/*4X4 TU Rotate, total 16X4*/
	u8 scalingdc[12];		/*N1005 Vienna Meeting*/
	u8 reserved[4];		/*16Bytes align*/
};

static void set_ps_field(u32 *buf, struct rkvdec_ps_field field, u32 value)
{
	u8 bit = field.offset % 32, word = field.offset / 32;
	u64 mask = GENMASK_ULL(bit + field.len - 1, bit);
	u64 val = ((u64)value << bit) & mask;

	buf[word] &= ~mask;
	buf[word] |= val;
	if (bit + field.len > 32) {
		buf[word + 1] &= ~(mask >> 32);
		buf[word + 1] |= val >> 32;
	}
}

static void assemble_hw_pps(struct rkvdec_ctx *ctx,
			    struct rkvdec_hevc_run *run)
{
	struct rkvdec_hevc_ctx *hevc_ctx = ctx->priv;
	const struct v4l2_ctrl_hevc_sps *sps = run->sps;
	const struct v4l2_ctrl_hevc_pps *pps = run->pps;
	struct rkvdec_hevc_priv_tbl *priv_tbl = hevc_ctx->priv_tbl.cpu;
	struct rkvdec_sps_pps_packet *hw_ps;
	u32 min_cb_log2_size_y, ctb_log2_size_y, ctb_size_y;
	u32 log2_min_cu_qp_delta_size, scaling_distance;
	dma_addr_t scaling_list_address;
	int i;

	/*
	 * HW read the SPS/PPS information from PPS packet index by PPS id.
	 * offset from the base can be calculated by PPS_id * 80 (size per PPS
	 * packet unit). so the driver copy SPS/PPS information to the exact PPS
	 * packet unit for HW accessing.
	 */
	hw_ps = &priv_tbl->param_set[pps->pic_parameter_set_id];
	memset(hw_ps, 0, sizeof(*hw_ps));

#define WRITE_PPS(value, field) set_ps_field(hw_ps->info, field, value)
	/* write sps */
	WRITE_PPS(sps->video_parameter_set_id, VIDEO_PARAMETER_SET_ID);
	WRITE_PPS(sps->seq_parameter_set_id, SEQ_PARAMETER_SET_ID);
	WRITE_PPS(sps->chroma_format_idc, CHROMA_FORMAT_IDC);
	WRITE_PPS(sps->pic_width_in_luma_samples, PIC_WIDTH_IN_LUMA_SAMPLES);
	WRITE_PPS(sps->pic_height_in_luma_samples, PIC_HEIGHT_IN_LUMA_SAMPLES);
	WRITE_PPS(sps->bit_depth_luma_minus8 + 8, BIT_DEPTH_LUMA);
	WRITE_PPS(sps->bit_depth_chroma_minus8 + 8, BIT_DEPTH_CHROMA);
	WRITE_PPS(sps->log2_max_pic_order_cnt_lsb_minus4 + 4,
		  LOG2_MAX_PIC_ORDER_CNT_LSB);
	WRITE_PPS(sps->log2_diff_max_min_luma_coding_block_size,
		  LOG2_DIFF_MAX_MIN_LUMA_CODING_BLOCK_SIZE);
	WRITE_PPS(sps->log2_min_luma_coding_block_size_minus3 + 3,
		  LOG2_MIN_LUMA_CODING_BLOCK_SIZE);
	WRITE_PPS(sps->log2_min_luma_transform_block_size_minus2 + 2,
		  LOG2_MIN_TRANSFORM_BLOCK_SIZE);
	WRITE_PPS(sps->log2_diff_max_min_luma_transform_block_size,
		  LOG2_DIFF_MAX_MIN_LUMA_TRANSFORM_BLOCK_SIZE);
	WRITE_PPS(sps->max_transform_hierarchy_depth_inter,
		  MAX_TRANSFORM_HIERARCHY_DEPTH_INTER);
	WRITE_PPS(sps->max_transform_hierarchy_depth_intra,
		  MAX_TRANSFORM_HIERARCHY_DEPTH_INTRA);
	WRITE_PPS(!!(sps->flags & V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED),
		  SCALING_LIST_ENABLED_FLAG);
	WRITE_PPS(!!(sps->flags & V4L2_HEVC_SPS_FLAG_AMP_ENABLED),
		  AMP_ENABLED_FLAG);
	WRITE_PPS(!!(sps->flags & V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET),
		  SAMPLE_ADAPTIVE_OFFSET_ENABLED_FLAG);
	if (sps->flags & V4L2_HEVC_SPS_FLAG_PCM_ENABLED) {
		WRITE_PPS(1, PCM_ENABLED_FLAG);
		WRITE_PPS(sps->pcm_sample_bit_depth_luma_minus1 + 1,
			  PCM_SAMPLE_BIT_DEPTH_LUMA);
		WRITE_PPS(sps->pcm_sample_bit_depth_chroma_minus1 + 1,
			  PCM_SAMPLE_BIT_DEPTH_CHROMA);
		WRITE_PPS(!!(sps->flags & V4L2_HEVC_SPS_FLAG_PCM_LOOP_FILTER_DISABLED),
			  PCM_LOOP_FILTER_DISABLED_FLAG);
		WRITE_PPS(sps->log2_diff_max_min_pcm_luma_coding_block_size,
			  LOG2_DIFF_MAX_MIN_PCM_LUMA_CODING_BLOCK_SIZE);
		WRITE_PPS(sps->log2_min_pcm_luma_coding_block_size_minus3 + 3,
			  LOG2_MIN_PCM_LUMA_CODING_BLOCK_SIZE);
	}
	WRITE_PPS(sps->num_short_term_ref_pic_sets, NUM_SHORT_TERM_REF_PIC_SETS);
	WRITE_PPS(!!(sps->flags & V4L2_HEVC_SPS_FLAG_LONG_TERM_REF_PICS_PRESENT),
		  LONG_TERM_REF_PICS_PRESENT_FLAG);
	WRITE_PPS(sps->num_long_term_ref_pics_sps, NUM_LONG_TERM_REF_PICS_SPS);
	WRITE_PPS(!!(sps->flags & V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED),
		  SPS_TEMPORAL_MVP_ENABLED_FLAG);
	WRITE_PPS(!!(sps->flags & V4L2_HEVC_SPS_FLAG_STRONG_INTRA_SMOOTHING_ENABLED),
		  STRONG_INTRA_SMOOTHING_ENABLED_FLAG);

	/* write pps */
	WRITE_PPS(pps->pic_parameter_set_id, PIC_PARAMETER_SET_ID);
	WRITE_PPS(sps->seq_parameter_set_id, PPS_SEQ_PARAMETER_SET_ID);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_DEPENDENT_SLICE_SEGMENT_ENABLED),
		  DEPENDENT_SLICE_SEGMENTS_ENABLED_FLAG);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_OUTPUT_FLAG_PRESENT),
		  OUTPUT_FLAG_PRESENT_FLAG);
	WRITE_PPS(pps->num_extra_slice_header_bits, NUM_EXTRA_SLICE_HEADER_BITS);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_SIGN_DATA_HIDING_ENABLED),
		  SIGN_DATA_HIDING_ENABLED_FLAG);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_CABAC_INIT_PRESENT),
		  CABAC_INIT_PRESENT_FLAG);
	WRITE_PPS(pps->num_ref_idx_l0_default_active_minus1 + 1,
		  NUM_REF_IDX_L0_DEFAULT_ACTIVE);
	WRITE_PPS(pps->num_ref_idx_l1_default_active_minus1 + 1,
		  NUM_REF_IDX_L1_DEFAULT_ACTIVE);
	WRITE_PPS(pps->init_qp_minus26, INIT_QP_MINUS26);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_CONSTRAINED_INTRA_PRED),
		  CONSTRAINED_INTRA_PRED_FLAG);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_TRANSFORM_SKIP_ENABLED),
		  TRANSFORM_SKIP_ENABLED_FLAG);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED),
		  CU_QP_DELTA_ENABLED_FLAG);

	min_cb_log2_size_y = sps->log2_min_luma_coding_block_size_minus3 + 3;
	ctb_log2_size_y = min_cb_log2_size_y +
		sps->log2_diff_max_min_luma_coding_block_size;
	ctb_size_y = 1 << ctb_log2_size_y;
	log2_min_cu_qp_delta_size = ctb_log2_size_y - pps->diff_cu_qp_delta_depth;
	WRITE_PPS(log2_min_cu_qp_delta_size, LOG2_MIN_CU_QP_DELTA_SIZE);
	WRITE_PPS(pps->pps_cb_qp_offset, PPS_CB_QP_OFFSET);
	WRITE_PPS(pps->pps_cr_qp_offset, PPS_CR_QP_OFFSET);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT),
		  PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT_FLAG);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_WEIGHTED_PRED),
		  WEIGHTED_PRED_FLAG);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_WEIGHTED_BIPRED),
		  WEIGHTED_BIPRED_FLAG);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_TRANSQUANT_BYPASS_ENABLED),
		  TRANSQUANT_BYPASS_ENABLED_FLAG);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_TILES_ENABLED),
		  TILES_ENABLED_FLAG);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED),
		  ENTROPY_CODING_SYNC_ENABLED_FLAG);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED),
		  PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_LOOP_FILTER_ACROSS_TILES_ENABLED),
		  LOOP_FILTER_ACROSS_TILES_ENABLED_FLAG);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_OVERRIDE_ENABLED),
		  DEBLOCKING_FILTER_OVERRIDE_ENABLED_FLAG);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_PPS_DISABLE_DEBLOCKING_FILTER),
		  PPS_DEBLOCKING_FILTER_DISABLED_FLAG);
	WRITE_PPS(pps->pps_beta_offset_div2, PPS_BETA_OFFSET_DIV2);
	WRITE_PPS(pps->pps_tc_offset_div2, PPS_TC_OFFSET_DIV2);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_LISTS_MODIFICATION_PRESENT),
		  LISTS_MODIFICATION_PRESENT_FLAG);
	WRITE_PPS(pps->log2_parallel_merge_level_minus2 + 2, LOG2_PARALLEL_MERGE_LEVEL);
	WRITE_PPS(!!(pps->flags & V4L2_HEVC_PPS_FLAG_SLICE_SEGMENT_HEADER_EXTENSION_PRESENT),
		  SLICE_SEGMENT_HEADER_EXTENSION_PRESENT_FLAG);
	WRITE_PPS(pps->num_tile_columns_minus1 + 1, NUM_TILE_COLUMNS);
	WRITE_PPS(pps->num_tile_rows_minus1 + 1, NUM_TILE_ROWS);

	if (pps->flags & V4L2_HEVC_PPS_FLAG_TILES_ENABLED) {
		/* Userspace also provide column width and row height for uniform spacing */
		for (i = 0; i <= pps->num_tile_columns_minus1; i++)
			WRITE_PPS(pps->column_width_minus1[i], COLUMN_WIDTH(i));
		for (i = 0; i <= pps->num_tile_rows_minus1; i++)
			WRITE_PPS(pps->row_height_minus1[i], ROW_HEIGHT(i));
	} else {
		WRITE_PPS(((sps->pic_width_in_luma_samples + ctb_size_y - 1) / ctb_size_y) - 1,
			  COLUMN_WIDTH(0));
		WRITE_PPS(((sps->pic_height_in_luma_samples + ctb_size_y - 1) / ctb_size_y) - 1,
			  ROW_HEIGHT(0));
	}

	scaling_distance = offsetof(struct rkvdec_hevc_priv_tbl, scaling_list);
	scaling_list_address = hevc_ctx->priv_tbl.dma + scaling_distance;
	WRITE_PPS(scaling_list_address, SCALING_LIST_ADDRESS);
}

/*
 * Creation of the Reference Picture Set memory blob for the hardware.
 * The layout looks like this:
 * [0] 32 bits for L0 (6 references + 2 bits of the 7th reference)
 * [1] 32 bits for L0 (remaining 3 bits of the 7th reference + 5 references
 *     + 4 bits of the 13th reference)
 * [2] 11 bits for L0 (remaining bit for 13 and 2 references) and
 *     21 bits for L1 (4 references + first bit of 5)
 * [3] 32 bits of padding with 0s
 * [4] 32 bits for L1 (remaining 4 bits for 5 + 5 references + 3 bits of 11)
 * [5] 22 bits for L1 (remaining 2 bits of 11 and 4 references)
 *     lowdelay flag (bit 23), rps bit offset long term (bit 24 - 32)
 * [6] rps bit offset long term (bit 1 - 3),  rps bit offset short term (bit 4 - 12)
 *     number of references (bit 13 - 16), remaining 16 bits of padding with 0s
 * [7] 32 bits of padding with 0s
 *
 * Thus we have to set up padding in between reference 5 of the L1 list.
 */
static void assemble_sw_rps(struct rkvdec_ctx *ctx,
			    struct rkvdec_hevc_run *run)
{
	const struct v4l2_ctrl_hevc_decode_params *decode_params = run->decode_params;
	const struct v4l2_ctrl_hevc_sps *sps = run->sps;
	const struct v4l2_ctrl_hevc_slice_params *sl_params;
	const struct v4l2_hevc_dpb_entry *dpb;
	struct rkvdec_hevc_ctx *hevc_ctx = ctx->priv;
	struct rkvdec_hevc_priv_tbl *priv_tbl = hevc_ctx->priv_tbl.cpu;
	struct rkvdec_rps_packet *hw_ps;
	int i, j;
	unsigned int lowdelay;

#define WRITE_RPS(value, field) set_ps_field(hw_ps->info, field, value)

#define REF_PIC_LONG_TERM_L0(i)			PS_FIELD((i) * 5, 1)
#define REF_PIC_IDX_L0(i)			PS_FIELD(1 + ((i) * 5), 4)
#define REF_PIC_LONG_TERM_L1(i)			PS_FIELD(((i) < 5 ? 75 : 132) + ((i) * 5), 1)
#define REF_PIC_IDX_L1(i)			PS_FIELD(((i) < 4 ? 76 : 128) + ((i) * 5), 4)

#define LOWDELAY				PS_FIELD(182, 1)
#define LONG_TERM_RPS_BIT_OFFSET		PS_FIELD(183, 10)
#define SHORT_TERM_RPS_BIT_OFFSET		PS_FIELD(193, 9)
#define NUM_RPS_POC				PS_FIELD(202, 4)

	for (j = 0; j < run->num_slices; j++) {
		uint st_bit_offset = 0;
		uint num_l0_refs = 0;
		uint num_l1_refs = 0;

		sl_params = &run->slices_params[j];
		dpb = decode_params->dpb;

		if (sl_params->slice_type != V4L2_HEVC_SLICE_TYPE_I) {
			num_l0_refs = sl_params->num_ref_idx_l0_active_minus1 + 1;

			if (sl_params->slice_type == V4L2_HEVC_SLICE_TYPE_B)
				num_l1_refs = sl_params->num_ref_idx_l1_active_minus1 + 1;

			lowdelay = 1;
		} else {
			lowdelay = 0;
		}

		hw_ps = &priv_tbl->rps[j];
		memset(hw_ps, 0, sizeof(*hw_ps));

		for (i = 0; i < num_l0_refs; i++) {
			const struct v4l2_hevc_dpb_entry dpb_l0 = dpb[sl_params->ref_idx_l0[i]];

			WRITE_RPS(!!(dpb_l0.flags & V4L2_HEVC_DPB_ENTRY_LONG_TERM_REFERENCE),
				  REF_PIC_LONG_TERM_L0(i));
			WRITE_RPS(sl_params->ref_idx_l0[i], REF_PIC_IDX_L0(i));

			if (dpb_l0.pic_order_cnt_val > sl_params->slice_pic_order_cnt)
				lowdelay = 0;
		}

		for (i = 0; i < num_l1_refs; i++) {
			const struct v4l2_hevc_dpb_entry dpb_l1 = dpb[sl_params->ref_idx_l1[i]];
			int is_long_term =
				!!(dpb_l1.flags & V4L2_HEVC_DPB_ENTRY_LONG_TERM_REFERENCE);

			WRITE_RPS(is_long_term, REF_PIC_LONG_TERM_L1(i));
			WRITE_RPS(sl_params->ref_idx_l1[i], REF_PIC_IDX_L1(i));

			if (dpb_l1.pic_order_cnt_val > sl_params->slice_pic_order_cnt)
				lowdelay = 0;
		}

		WRITE_RPS(lowdelay, LOWDELAY);

		if (!(decode_params->flags & V4L2_HEVC_DECODE_PARAM_FLAG_IDR_PIC)) {
			if (sl_params->short_term_ref_pic_set_size)
				st_bit_offset = sl_params->short_term_ref_pic_set_size;
			else if (sps->num_short_term_ref_pic_sets > 1)
				st_bit_offset = fls(sps->num_short_term_ref_pic_sets - 1);
		}

		WRITE_RPS(st_bit_offset + sl_params->long_term_ref_pic_set_size,
			  LONG_TERM_RPS_BIT_OFFSET);
		WRITE_RPS(sl_params->short_term_ref_pic_set_size,
			  SHORT_TERM_RPS_BIT_OFFSET);

		WRITE_RPS(decode_params->num_poc_st_curr_before +
			  decode_params->num_poc_st_curr_after +
			  decode_params->num_poc_lt_curr,
			  NUM_RPS_POC);
	}
}

/*
 * Flip one or more matrices along their main diagonal and flatten them
 * before writing it to the memory.
 * Convert:
 * ABCD         AEIM
 * EFGH     =>  BFJN     =>     AEIMBFJNCGKODHLP
 * IJKL         CGKO
 * MNOP         DHLP
 */
static void transpose_and_flatten_matrices(u8 *output, const u8 *input,
					   int matrices, int row_length)
{
	int i, j, row, x_offset, matrix_offset, rot_index, y_offset, matrix_size, new_value;

	matrix_size = row_length * row_length;
	for (i = 0; i < matrices; i++) {
		row = 0;
		x_offset = 0;
		matrix_offset = i * matrix_size;
		for (j = 0; j < matrix_size; j++) {
			y_offset = j - (row * row_length);
			rot_index = y_offset * row_length + x_offset;
			new_value = *(input + i * matrix_size + j);
			output[matrix_offset + rot_index] = new_value;
			if ((j + 1) % row_length == 0) {
				row += 1;
				x_offset += 1;
			}
		}
	}
}

static void assemble_scalingfactor0(u8 *output, const struct v4l2_ctrl_hevc_scaling_matrix *input)
{
	int offset = 0;

	transpose_and_flatten_matrices(output, (const u8 *)input->scaling_list_4x4, 6, 4);
	offset = 6 * 16 * sizeof(u8);
	transpose_and_flatten_matrices(output + offset, (const u8 *)input->scaling_list_8x8, 6, 8);
	offset += 6 * 64 * sizeof(u8);
	transpose_and_flatten_matrices(output + offset,
				       (const u8 *)input->scaling_list_16x16, 6, 8);
	offset += 6 * 64 * sizeof(u8);
	/* Add a 128 byte padding with 0s between the two 32x32 matrices */
	transpose_and_flatten_matrices(output + offset,
				       (const u8 *)input->scaling_list_32x32, 1, 8);
	offset += 64 * sizeof(u8);
	memset(output + offset, 0, 128);
	offset += 128 * sizeof(u8);
	transpose_and_flatten_matrices(output + offset,
				       (const u8 *)input->scaling_list_32x32 + (64 * sizeof(u8)),
				       1, 8);
	offset += 64 * sizeof(u8);
	memset(output + offset, 0, 128);
}

/*
 * Required layout:
 * A = scaling_list_dc_coef_16x16
 * B = scaling_list_dc_coef_32x32
 * 0 = Padding
 *
 * A, A, A, A, A, A, B, 0, 0, B, 0, 0
 */
static void assemble_scalingdc(u8 *output, const struct v4l2_ctrl_hevc_scaling_matrix *input)
{
	u8 list_32x32[6] = {0};

	memcpy(output, input->scaling_list_dc_coef_16x16, 6 * sizeof(u8));
	list_32x32[0] = input->scaling_list_dc_coef_32x32[0];
	list_32x32[3] = input->scaling_list_dc_coef_32x32[1];
	memcpy(output + 6 * sizeof(u8), list_32x32, 6 * sizeof(u8));
}

static void translate_scaling_list(struct scaling_factor *output,
				   const struct v4l2_ctrl_hevc_scaling_matrix *input)
{
	assemble_scalingfactor0(output->scalingfactor0, input);
	memcpy(output->scalingfactor1, (const u8 *)input->scaling_list_4x4, 96);
	assemble_scalingdc(output->scalingdc, input);
	memset(output->reserved, 0, 4 * sizeof(u8));
}

static void assemble_hw_scaling_list(struct rkvdec_ctx *ctx,
				     struct rkvdec_hevc_run *run)
{
	const struct v4l2_ctrl_hevc_scaling_matrix *scaling = run->scaling_matrix;
	struct rkvdec_hevc_ctx *hevc_ctx = ctx->priv;
	struct rkvdec_hevc_priv_tbl *tbl = hevc_ctx->priv_tbl.cpu;
	u8 *dst;

	if (!memcmp((void *)&hevc_ctx->scaling_matrix_cache, scaling,
		    sizeof(struct v4l2_ctrl_hevc_scaling_matrix)))
		return;

	dst = tbl->scaling_list;
	translate_scaling_list((struct scaling_factor *)dst, scaling);

	memcpy((void *)&hevc_ctx->scaling_matrix_cache, scaling,
	       sizeof(struct v4l2_ctrl_hevc_scaling_matrix));
}

static struct vb2_buffer *
get_ref_buf(struct rkvdec_ctx *ctx, struct rkvdec_hevc_run *run,
	    unsigned int dpb_idx)
{
	struct v4l2_m2m_ctx *m2m_ctx = ctx->fh.m2m_ctx;
	const struct v4l2_ctrl_hevc_decode_params *decode_params = run->decode_params;
	const struct v4l2_hevc_dpb_entry *dpb = decode_params->dpb;
	struct vb2_queue *cap_q = &m2m_ctx->cap_q_ctx.q;
	struct vb2_buffer *buf = NULL;

	if (dpb_idx < decode_params->num_active_dpb_entries)
		buf = vb2_find_buffer(cap_q, dpb[dpb_idx].timestamp);

	/*
	 * If a DPB entry is unused or invalid, the address of current destination
	 * buffer is returned.
	 */
	if (!buf)
		return &run->base.bufs.dst->vb2_buf;

	return buf;
}

static void config_registers(struct rkvdec_ctx *ctx,
			     struct rkvdec_hevc_run *run)
{
	struct rkvdec_dev *rkvdec = ctx->dev;
	const struct v4l2_ctrl_hevc_decode_params *decode_params = run->decode_params;
	const struct v4l2_ctrl_hevc_sps *sps = run->sps;
	const struct v4l2_ctrl_hevc_slice_params *sl_params = &run->slices_params[0];
	const struct v4l2_hevc_dpb_entry *dpb = decode_params->dpb;
	struct rkvdec_hevc_ctx *hevc_ctx = ctx->priv;
	dma_addr_t priv_start_addr = hevc_ctx->priv_tbl.dma;
	const struct v4l2_pix_format_mplane *dst_fmt;
	struct vb2_v4l2_buffer *src_buf = run->base.bufs.src;
	struct vb2_v4l2_buffer *dst_buf = run->base.bufs.dst;
	const struct v4l2_format *f;
	dma_addr_t rlc_addr;
	dma_addr_t refer_addr;
	u32 rlc_len;
	u32 hor_virstride;
	u32 ver_virstride;
	u32 y_virstride;
	u32 yuv_virstride = 0;
	u32 offset;
	dma_addr_t dst_addr;
	u32 reg, i;

	reg = RKVDEC_MODE(RKVDEC_MODE_HEVC);
	writel_relaxed(reg, rkvdec->regs + RKVDEC_REG_SYSCTRL);

	f = &ctx->decoded_fmt;
	dst_fmt = &f->fmt.pix_mp;
	hor_virstride = dst_fmt->plane_fmt[0].bytesperline;
	ver_virstride = dst_fmt->height;
	y_virstride = hor_virstride * ver_virstride;

	if (sps->chroma_format_idc == 0)
		yuv_virstride = y_virstride;
	else if (sps->chroma_format_idc == 1)
		yuv_virstride = y_virstride + y_virstride / 2;
	else if (sps->chroma_format_idc == 2)
		yuv_virstride = 2 * y_virstride;

	reg = RKVDEC_Y_HOR_VIRSTRIDE(hor_virstride / 16) |
	      RKVDEC_UV_HOR_VIRSTRIDE(hor_virstride / 16) |
	      RKVDEC_SLICE_NUM_LOWBITS(run->num_slices);
	writel_relaxed(reg, rkvdec->regs + RKVDEC_REG_PICPAR);

	/* config rlc base address */
	rlc_addr = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	writel_relaxed(rlc_addr, rkvdec->regs + RKVDEC_REG_STRM_RLC_BASE);

	rlc_len = vb2_get_plane_payload(&src_buf->vb2_buf, 0);
	reg = RKVDEC_STRM_LEN(round_up(rlc_len, 16) + 64);
	writel_relaxed(reg, rkvdec->regs + RKVDEC_REG_STRM_LEN);

	/* config cabac table */
	offset = offsetof(struct rkvdec_hevc_priv_tbl, cabac_table);
	writel_relaxed(priv_start_addr + offset,
		       rkvdec->regs + RKVDEC_REG_CABACTBL_PROB_BASE);

	/* config output base address */
	dst_addr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	writel_relaxed(dst_addr, rkvdec->regs + RKVDEC_REG_DECOUT_BASE);

	reg = RKVDEC_Y_VIRSTRIDE(y_virstride / 16);
	writel_relaxed(reg, rkvdec->regs + RKVDEC_REG_Y_VIRSTRIDE);

	reg = RKVDEC_YUV_VIRSTRIDE(yuv_virstride / 16);
	writel_relaxed(reg, rkvdec->regs + RKVDEC_REG_YUV_VIRSTRIDE);

	/* config ref pic address */
	for (i = 0; i < 15; i++) {
		struct vb2_buffer *vb_buf = get_ref_buf(ctx, run, i);

		if (i < 4 && decode_params->num_active_dpb_entries) {
			reg = GENMASK(decode_params->num_active_dpb_entries - 1, 0);
			reg = (reg >> (i * 4)) & 0xf;
		} else {
			reg = 0;
		}

		refer_addr = vb2_dma_contig_plane_dma_addr(vb_buf, 0);
		writel_relaxed(refer_addr | reg,
			       rkvdec->regs + RKVDEC_REG_H264_BASE_REFER(i));

		reg = RKVDEC_POC_REFER(i < decode_params->num_active_dpb_entries ?
			dpb[i].pic_order_cnt_val : 0);
		writel_relaxed(reg,
			       rkvdec->regs + RKVDEC_REG_H264_POC_REFER0(i));
	}

	reg = RKVDEC_CUR_POC(sl_params->slice_pic_order_cnt);
	writel_relaxed(reg, rkvdec->regs + RKVDEC_REG_CUR_POC0);

	/* config hw pps address */
	offset = offsetof(struct rkvdec_hevc_priv_tbl, param_set);
	writel_relaxed(priv_start_addr + offset,
		       rkvdec->regs + RKVDEC_REG_PPS_BASE);

	/* config hw rps address */
	offset = offsetof(struct rkvdec_hevc_priv_tbl, rps);
	writel_relaxed(priv_start_addr + offset,
		       rkvdec->regs + RKVDEC_REG_RPS_BASE);

	reg = RKVDEC_AXI_DDR_RDATA(0);
	writel_relaxed(reg, rkvdec->regs + RKVDEC_REG_AXI_DDR_RDATA);

	reg = RKVDEC_AXI_DDR_WDATA(0);
	writel_relaxed(reg, rkvdec->regs + RKVDEC_REG_AXI_DDR_WDATA);
}

#define RKVDEC_HEVC_MAX_DEPTH_IN_BYTES		2

static int rkvdec_hevc_adjust_fmt(struct rkvdec_ctx *ctx,
				  struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *fmt = &f->fmt.pix_mp;

	fmt->num_planes = 1;
	if (!fmt->plane_fmt[0].sizeimage)
		fmt->plane_fmt[0].sizeimage = fmt->width * fmt->height *
					      RKVDEC_HEVC_MAX_DEPTH_IN_BYTES;
	return 0;
}

static enum rkvdec_image_fmt rkvdec_hevc_get_image_fmt(struct rkvdec_ctx *ctx,
						       struct v4l2_ctrl *ctrl)
{
	const struct v4l2_ctrl_hevc_sps *sps = ctrl->p_new.p_hevc_sps;

	if (ctrl->id != V4L2_CID_STATELESS_HEVC_SPS)
		return RKVDEC_IMG_FMT_ANY;

	if (sps->bit_depth_luma_minus8 == 0) {
		if (sps->chroma_format_idc == 2)
			return RKVDEC_IMG_FMT_422_8BIT;
		else
			return RKVDEC_IMG_FMT_420_8BIT;
	} else if (sps->bit_depth_luma_minus8 == 2) {
		if (sps->chroma_format_idc == 2)
			return RKVDEC_IMG_FMT_422_10BIT;
		else
			return RKVDEC_IMG_FMT_420_10BIT;
	}

	return RKVDEC_IMG_FMT_ANY;
}

static int rkvdec_hevc_validate_sps(struct rkvdec_ctx *ctx,
				    const struct v4l2_ctrl_hevc_sps *sps)
{
	if (sps->chroma_format_idc > 1)
		/* Only 4:0:0 and 4:2:0 are supported */
		return -EINVAL;
	if (sps->bit_depth_luma_minus8 != sps->bit_depth_chroma_minus8)
		/* Luma and chroma bit depth mismatch */
		return -EINVAL;
	if (sps->bit_depth_luma_minus8 != 0 && sps->bit_depth_luma_minus8 != 2)
		/* Only 8-bit and 10-bit is supported */
		return -EINVAL;

	if (sps->pic_width_in_luma_samples > ctx->coded_fmt.fmt.pix_mp.width ||
	    sps->pic_height_in_luma_samples > ctx->coded_fmt.fmt.pix_mp.height)
		return -EINVAL;

	return 0;
}

static int rkvdec_hevc_start(struct rkvdec_ctx *ctx)
{
	struct rkvdec_dev *rkvdec = ctx->dev;
	struct rkvdec_hevc_priv_tbl *priv_tbl;
	struct rkvdec_hevc_ctx *hevc_ctx;

	hevc_ctx = kzalloc(sizeof(*hevc_ctx), GFP_KERNEL);
	if (!hevc_ctx)
		return -ENOMEM;

	priv_tbl = dma_alloc_coherent(rkvdec->dev, sizeof(*priv_tbl),
				      &hevc_ctx->priv_tbl.dma, GFP_KERNEL);
	if (!priv_tbl) {
		kfree(hevc_ctx);
		return -ENOMEM;
	}

	hevc_ctx->priv_tbl.size = sizeof(*priv_tbl);
	hevc_ctx->priv_tbl.cpu = priv_tbl;
	memcpy(priv_tbl->cabac_table, rkvdec_hevc_cabac_table,
	       sizeof(rkvdec_hevc_cabac_table));

	ctx->priv = hevc_ctx;
	return 0;
}

static void rkvdec_hevc_stop(struct rkvdec_ctx *ctx)
{
	struct rkvdec_hevc_ctx *hevc_ctx = ctx->priv;
	struct rkvdec_dev *rkvdec = ctx->dev;

	dma_free_coherent(rkvdec->dev, hevc_ctx->priv_tbl.size,
			  hevc_ctx->priv_tbl.cpu, hevc_ctx->priv_tbl.dma);
	kfree(hevc_ctx);
}

static void rkvdec_hevc_run_preamble(struct rkvdec_ctx *ctx,
				     struct rkvdec_hevc_run *run)
{
	struct v4l2_ctrl *ctrl;

	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_DECODE_PARAMS);
	run->decode_params = ctrl ? ctrl->p_cur.p : NULL;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_SLICE_PARAMS);
	run->slices_params = ctrl ? ctrl->p_cur.p : NULL;
	run->num_slices = ctrl ? ctrl->new_elems : 0;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_SPS);
	run->sps = ctrl ? ctrl->p_cur.p : NULL;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_PPS);
	run->pps = ctrl ? ctrl->p_cur.p : NULL;
	ctrl = v4l2_ctrl_find(&ctx->ctrl_hdl,
			      V4L2_CID_STATELESS_HEVC_SCALING_MATRIX);
	run->scaling_matrix = ctrl ? ctrl->p_cur.p : NULL;

	rkvdec_run_preamble(ctx, &run->base);
}

static int rkvdec_hevc_run(struct rkvdec_ctx *ctx)
{
	struct rkvdec_dev *rkvdec = ctx->dev;
	struct rkvdec_hevc_run run;
	u32 reg;

	rkvdec_hevc_run_preamble(ctx, &run);

	assemble_hw_scaling_list(ctx, &run);
	assemble_hw_pps(ctx, &run);
	assemble_sw_rps(ctx, &run);
	config_registers(ctx, &run);

	rkvdec_run_postamble(ctx, &run.base);

	schedule_delayed_work(&rkvdec->watchdog_work, msecs_to_jiffies(2000));

	writel(0, rkvdec->regs + RKVDEC_REG_STRMD_ERR_EN);
	writel(0, rkvdec->regs + RKVDEC_REG_H264_ERR_E);
	writel(1, rkvdec->regs + RKVDEC_REG_PREF_LUMA_CACHE_COMMAND);
	writel(1, rkvdec->regs + RKVDEC_REG_PREF_CHR_CACHE_COMMAND);

	if (rkvdec->variant->quirks & RKVDEC_QUIRK_DISABLE_QOS)
		rkvdec_quirks_disable_qos(ctx);

	/* Start decoding! */
	reg = (run.pps->flags & V4L2_HEVC_PPS_FLAG_TILES_ENABLED) ?
		0 : RKVDEC_WR_DDR_ALIGN_EN;
	writel(RKVDEC_INTERRUPT_DEC_E | RKVDEC_CONFIG_DEC_CLK_GATE_E |
	       RKVDEC_TIMEOUT_E | RKVDEC_BUF_EMPTY_E | reg,
	       rkvdec->regs + RKVDEC_REG_INTERRUPT);

	return 0;
}

static int rkvdec_hevc_try_ctrl(struct rkvdec_ctx *ctx, struct v4l2_ctrl *ctrl)
{
	if (ctrl->id == V4L2_CID_STATELESS_HEVC_SPS)
		return rkvdec_hevc_validate_sps(ctx, ctrl->p_new.p_hevc_sps);

	return 0;
}

const struct rkvdec_coded_fmt_ops rkvdec_hevc_fmt_ops = {
	.adjust_fmt = rkvdec_hevc_adjust_fmt,
	.start = rkvdec_hevc_start,
	.stop = rkvdec_hevc_stop,
	.run = rkvdec_hevc_run,
	.try_ctrl = rkvdec_hevc_try_ctrl,
	.get_image_fmt = rkvdec_hevc_get_image_fmt,
};
