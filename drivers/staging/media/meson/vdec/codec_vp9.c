// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Maxime Jourdan <mjourdan@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 */

#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "dos_regs.h"
#include "hevc_regs.h"
#include "codec_vp9.h"
#include "vdec_helpers.h"
#include "codec_hevc_common.h"

/* HEVC reg mapping */
#define VP9_DEC_STATUS_REG	HEVC_ASSIST_SCRATCH_0
	#define VP9_10B_DECODE_SLICE	5
	#define VP9_HEAD_PARSER_DONE	0xf0
#define VP9_RPM_BUFFER		HEVC_ASSIST_SCRATCH_1
#define VP9_SHORT_TERM_RPS	HEVC_ASSIST_SCRATCH_2
#define VP9_ADAPT_PROB_REG	HEVC_ASSIST_SCRATCH_3
#define VP9_MMU_MAP_BUFFER	HEVC_ASSIST_SCRATCH_4
#define VP9_PPS_BUFFER		HEVC_ASSIST_SCRATCH_5
#define VP9_SAO_UP		HEVC_ASSIST_SCRATCH_6
#define VP9_STREAM_SWAP_BUFFER	HEVC_ASSIST_SCRATCH_7
#define VP9_STREAM_SWAP_BUFFER2 HEVC_ASSIST_SCRATCH_8
#define VP9_PROB_SWAP_BUFFER	HEVC_ASSIST_SCRATCH_9
#define VP9_COUNT_SWAP_BUFFER	HEVC_ASSIST_SCRATCH_A
#define VP9_SEG_MAP_BUFFER	HEVC_ASSIST_SCRATCH_B
#define VP9_SCALELUT		HEVC_ASSIST_SCRATCH_D
#define VP9_WAIT_FLAG		HEVC_ASSIST_SCRATCH_E
#define LMEM_DUMP_ADR		HEVC_ASSIST_SCRATCH_F
#define NAL_SEARCH_CTL		HEVC_ASSIST_SCRATCH_I
#define VP9_DECODE_MODE		HEVC_ASSIST_SCRATCH_J
	#define DECODE_MODE_SINGLE 0
#define DECODE_STOP_POS		HEVC_ASSIST_SCRATCH_K
#define HEVC_DECODE_COUNT	HEVC_ASSIST_SCRATCH_M
#define HEVC_DECODE_SIZE	HEVC_ASSIST_SCRATCH_N

/* VP9 Constants */
#define LCU_SIZE		64
#define MAX_REF_PIC_NUM		24
#define REFS_PER_FRAME		3
#define REF_FRAMES		8
#define MV_MEM_UNIT		0x240
#define ADAPT_PROB_SIZE		0xf80

enum FRAME_TYPE {
	KEY_FRAME = 0,
	INTER_FRAME = 1,
	FRAME_TYPES,
};

/* VP9 Workspace layout */
#define MPRED_MV_BUF_SIZE 0x120000

#define IPP_SIZE	0x4000
#define SAO_ABV_SIZE	0x30000
#define SAO_VB_SIZE	0x30000
#define SH_TM_RPS_SIZE	0x800
#define VPS_SIZE	0x800
#define SPS_SIZE	0x800
#define PPS_SIZE	0x2000
#define SAO_UP_SIZE	0x2800
#define SWAP_BUF_SIZE	0x800
#define SWAP_BUF2_SIZE	0x800
#define SCALELUT_SIZE	0x8000
#define DBLK_PARA_SIZE	0x80000
#define DBLK_DATA_SIZE	0x80000
#define SEG_MAP_SIZE	0xd800
#define PROB_SIZE	0x5000
#define COUNT_SIZE	0x3000
#define MMU_VBH_SIZE	0x5000
#define MPRED_ABV_SIZE	0x10000
#define MPRED_MV_SIZE	(MPRED_MV_BUF_SIZE * MAX_REF_PIC_NUM)
#define RPM_BUF_SIZE	0x100
#define LMEM_SIZE	0x800

#define IPP_OFFSET       0x00
#define SAO_ABV_OFFSET   (IPP_OFFSET + IPP_SIZE)
#define SAO_VB_OFFSET    (SAO_ABV_OFFSET + SAO_ABV_SIZE)
#define SH_TM_RPS_OFFSET (SAO_VB_OFFSET + SAO_VB_SIZE)
#define VPS_OFFSET       (SH_TM_RPS_OFFSET + SH_TM_RPS_SIZE)
#define SPS_OFFSET       (VPS_OFFSET + VPS_SIZE)
#define PPS_OFFSET       (SPS_OFFSET + SPS_SIZE)
#define SAO_UP_OFFSET    (PPS_OFFSET + PPS_SIZE)
#define SWAP_BUF_OFFSET  (SAO_UP_OFFSET + SAO_UP_SIZE)
#define SWAP_BUF2_OFFSET (SWAP_BUF_OFFSET + SWAP_BUF_SIZE)
#define SCALELUT_OFFSET  (SWAP_BUF2_OFFSET + SWAP_BUF2_SIZE)
#define DBLK_PARA_OFFSET (SCALELUT_OFFSET + SCALELUT_SIZE)
#define DBLK_DATA_OFFSET (DBLK_PARA_OFFSET + DBLK_PARA_SIZE)
#define SEG_MAP_OFFSET   (DBLK_DATA_OFFSET + DBLK_DATA_SIZE)
#define PROB_OFFSET      (SEG_MAP_OFFSET + SEG_MAP_SIZE)
#define COUNT_OFFSET     (PROB_OFFSET + PROB_SIZE)
#define MMU_VBH_OFFSET   (COUNT_OFFSET + COUNT_SIZE)
#define MPRED_ABV_OFFSET (MMU_VBH_OFFSET + MMU_VBH_SIZE)
#define MPRED_MV_OFFSET  (MPRED_ABV_OFFSET + MPRED_ABV_SIZE)
#define RPM_OFFSET       (MPRED_MV_OFFSET + MPRED_MV_SIZE)
#define LMEM_OFFSET      (RPM_OFFSET + RPM_BUF_SIZE)

#define SIZE_WORKSPACE	ALIGN(LMEM_OFFSET + LMEM_SIZE, 64 * SZ_1K)

#define NONE           -1
#define INTRA_FRAME     0
#define LAST_FRAME      1
#define GOLDEN_FRAME    2
#define ALTREF_FRAME    3
#define MAX_REF_FRAMES  4

/*
 * Defines, declarations, sub-functions for vp9 de-block loop
	filter Thr/Lvl table update
 * - struct segmentation is for loop filter only (removed something)
 * - function "vp9_loop_filter_init" and "vp9_loop_filter_frame_init" will
	be instantiated in C_Entry
 * - vp9_loop_filter_init run once before decoding start
 * - vp9_loop_filter_frame_init run before every frame decoding start
 * - set video format to VP9 is in vp9_loop_filter_init
 */
#define MAX_LOOP_FILTER		63
#define MAX_REF_LF_DELTAS	4
#define MAX_MODE_LF_DELTAS	2
#define SEGMENT_DELTADATA	0
#define SEGMENT_ABSDATA		1
#define MAX_SEGMENTS		8

/* VP9 PROB processing defines */
#define VP9_PARTITION_START      0
#define VP9_PARTITION_SIZE_STEP  (3 * 4)
#define VP9_PARTITION_ONE_SIZE   (4 * VP9_PARTITION_SIZE_STEP)
#define VP9_PARTITION_KEY_START  0
#define VP9_PARTITION_P_START    VP9_PARTITION_ONE_SIZE
#define VP9_PARTITION_SIZE       (2 * VP9_PARTITION_ONE_SIZE)
#define VP9_SKIP_START           (VP9_PARTITION_START + VP9_PARTITION_SIZE)
#define VP9_SKIP_SIZE            4 /* only use 3*/
#define VP9_TX_MODE_START        (VP9_SKIP_START + VP9_SKIP_SIZE)
#define VP9_TX_MODE_8_0_OFFSET   0
#define VP9_TX_MODE_8_1_OFFSET   1
#define VP9_TX_MODE_16_0_OFFSET  2
#define VP9_TX_MODE_16_1_OFFSET  4
#define VP9_TX_MODE_32_0_OFFSET  6
#define VP9_TX_MODE_32_1_OFFSET  9
#define VP9_TX_MODE_SIZE         12
#define VP9_COEF_START           (VP9_TX_MODE_START + VP9_TX_MODE_SIZE)
#define VP9_COEF_BAND_0_OFFSET   0
#define VP9_COEF_BAND_1_OFFSET   (VP9_COEF_BAND_0_OFFSET + 3 * 3 + 1)
#define VP9_COEF_BAND_2_OFFSET   (VP9_COEF_BAND_1_OFFSET + 6 * 3)
#define VP9_COEF_BAND_3_OFFSET   (VP9_COEF_BAND_2_OFFSET + 6 * 3)
#define VP9_COEF_BAND_4_OFFSET   (VP9_COEF_BAND_3_OFFSET + 6 * 3)
#define VP9_COEF_BAND_5_OFFSET   (VP9_COEF_BAND_4_OFFSET + 6 * 3)
#define VP9_COEF_SIZE_ONE_SET    100 /* ((3 + 5 * 6) * 3 + 1 padding)*/
#define VP9_COEF_4X4_START       (VP9_COEF_START + 0 * VP9_COEF_SIZE_ONE_SET)
#define VP9_COEF_8X8_START       (VP9_COEF_START + 4 * VP9_COEF_SIZE_ONE_SET)
#define VP9_COEF_16X16_START     (VP9_COEF_START + 8 * VP9_COEF_SIZE_ONE_SET)
#define VP9_COEF_32X32_START     (VP9_COEF_START + 12 * VP9_COEF_SIZE_ONE_SET)
#define VP9_COEF_SIZE_PLANE      (2 * VP9_COEF_SIZE_ONE_SET)
#define VP9_COEF_SIZE            (4 * 2 * 2 * VP9_COEF_SIZE_ONE_SET)
#define VP9_INTER_MODE_START     (VP9_COEF_START + VP9_COEF_SIZE)
#define VP9_INTER_MODE_SIZE      24 /* only use 21 (# * 7)*/
#define VP9_INTERP_START         (VP9_INTER_MODE_START + VP9_INTER_MODE_SIZE)
#define VP9_INTERP_SIZE          8
#define VP9_INTRA_INTER_START    (VP9_INTERP_START + VP9_INTERP_SIZE)
#define VP9_INTRA_INTER_SIZE     4
#define VP9_INTERP_INTRA_INTER_START  VP9_INTERP_START
#define VP9_INTERP_INTRA_INTER_SIZE   (VP9_INTERP_SIZE + VP9_INTRA_INTER_SIZE)
#define VP9_COMP_INTER_START     \
		(VP9_INTERP_INTRA_INTER_START + VP9_INTERP_INTRA_INTER_SIZE)
#define VP9_COMP_INTER_SIZE      5
#define VP9_COMP_REF_START       (VP9_COMP_INTER_START + VP9_COMP_INTER_SIZE)
#define VP9_COMP_REF_SIZE        5
#define VP9_SINGLE_REF_START     (VP9_COMP_REF_START + VP9_COMP_REF_SIZE)
#define VP9_SINGLE_REF_SIZE      10
#define VP9_REF_MODE_START       VP9_COMP_INTER_START
#define VP9_REF_MODE_SIZE        \
		(VP9_COMP_INTER_SIZE + VP9_COMP_REF_SIZE + VP9_SINGLE_REF_SIZE)
#define VP9_IF_Y_MODE_START      (VP9_REF_MODE_START + VP9_REF_MODE_SIZE)
#define VP9_IF_Y_MODE_SIZE       36
#define VP9_IF_UV_MODE_START     (VP9_IF_Y_MODE_START + VP9_IF_Y_MODE_SIZE)
#define VP9_IF_UV_MODE_SIZE      92 /* only use 90*/
#define VP9_MV_JOINTS_START      (VP9_IF_UV_MODE_START + VP9_IF_UV_MODE_SIZE)
#define VP9_MV_JOINTS_SIZE       3
#define VP9_MV_SIGN_0_START      (VP9_MV_JOINTS_START + VP9_MV_JOINTS_SIZE)
#define VP9_MV_SIGN_0_SIZE       1
#define VP9_MV_CLASSES_0_START   (VP9_MV_SIGN_0_START + VP9_MV_SIGN_0_SIZE)
#define VP9_MV_CLASSES_0_SIZE    10
#define VP9_MV_CLASS0_0_START    \
		(VP9_MV_CLASSES_0_START + VP9_MV_CLASSES_0_SIZE)
#define VP9_MV_CLASS0_0_SIZE     1
#define VP9_MV_BITS_0_START      (VP9_MV_CLASS0_0_START + VP9_MV_CLASS0_0_SIZE)
#define VP9_MV_BITS_0_SIZE       10
#define VP9_MV_SIGN_1_START      (VP9_MV_BITS_0_START + VP9_MV_BITS_0_SIZE)
#define VP9_MV_SIGN_1_SIZE       1
#define VP9_MV_CLASSES_1_START   \
			(VP9_MV_SIGN_1_START + VP9_MV_SIGN_1_SIZE)
#define VP9_MV_CLASSES_1_SIZE    10
#define VP9_MV_CLASS0_1_START    \
			(VP9_MV_CLASSES_1_START + VP9_MV_CLASSES_1_SIZE)
#define VP9_MV_CLASS0_1_SIZE     1
#define VP9_MV_BITS_1_START      \
			(VP9_MV_CLASS0_1_START + VP9_MV_CLASS0_1_SIZE)
#define VP9_MV_BITS_1_SIZE       10
#define VP9_MV_CLASS0_FP_0_START \
			(VP9_MV_BITS_1_START + VP9_MV_BITS_1_SIZE)
#define VP9_MV_CLASS0_FP_0_SIZE  9
#define VP9_MV_CLASS0_FP_1_START \
			(VP9_MV_CLASS0_FP_0_START + VP9_MV_CLASS0_FP_0_SIZE)
#define VP9_MV_CLASS0_FP_1_SIZE  9
#define VP9_MV_CLASS0_HP_0_START \
			(VP9_MV_CLASS0_FP_1_START + VP9_MV_CLASS0_FP_1_SIZE)
#define VP9_MV_CLASS0_HP_0_SIZE  2
#define VP9_MV_CLASS0_HP_1_START \
			(VP9_MV_CLASS0_HP_0_START + VP9_MV_CLASS0_HP_0_SIZE)
#define VP9_MV_CLASS0_HP_1_SIZE  2
#define VP9_MV_START             VP9_MV_JOINTS_START
#define VP9_MV_SIZE              72 /*only use 69*/

#define VP9_TOTAL_SIZE           (VP9_MV_START + VP9_MV_SIZE)

/* VP9 COUNT mem processing defines */
#define VP9_COEF_COUNT_START           0
#define VP9_COEF_COUNT_BAND_0_OFFSET   0
#define VP9_COEF_COUNT_BAND_1_OFFSET   \
			(VP9_COEF_COUNT_BAND_0_OFFSET + 3 * 5)
#define VP9_COEF_COUNT_BAND_2_OFFSET   \
			(VP9_COEF_COUNT_BAND_1_OFFSET + 6 * 5)
#define VP9_COEF_COUNT_BAND_3_OFFSET   \
			(VP9_COEF_COUNT_BAND_2_OFFSET + 6 * 5)
#define VP9_COEF_COUNT_BAND_4_OFFSET   \
			(VP9_COEF_COUNT_BAND_3_OFFSET + 6 * 5)
#define VP9_COEF_COUNT_BAND_5_OFFSET   \
			(VP9_COEF_COUNT_BAND_4_OFFSET + 6 * 5)
#define VP9_COEF_COUNT_SIZE_ONE_SET    165 /* ((3 + 5 * 6) * 5 */
#define VP9_COEF_COUNT_4X4_START       \
		(VP9_COEF_COUNT_START + 0 * VP9_COEF_COUNT_SIZE_ONE_SET)
#define VP9_COEF_COUNT_8X8_START       \
		(VP9_COEF_COUNT_START + 4 * VP9_COEF_COUNT_SIZE_ONE_SET)
#define VP9_COEF_COUNT_16X16_START     \
		(VP9_COEF_COUNT_START + 8 * VP9_COEF_COUNT_SIZE_ONE_SET)
#define VP9_COEF_COUNT_32X32_START     \
		(VP9_COEF_COUNT_START + 12 * VP9_COEF_COUNT_SIZE_ONE_SET)
#define VP9_COEF_COUNT_SIZE_PLANE      (2 * VP9_COEF_COUNT_SIZE_ONE_SET)
#define VP9_COEF_COUNT_SIZE            (4 * 2 * 2 * VP9_COEF_COUNT_SIZE_ONE_SET)

#define VP9_INTRA_INTER_COUNT_START    \
		(VP9_COEF_COUNT_START + VP9_COEF_COUNT_SIZE)
#define VP9_INTRA_INTER_COUNT_SIZE     (4 * 2)
#define VP9_COMP_INTER_COUNT_START     \
		(VP9_INTRA_INTER_COUNT_START + VP9_INTRA_INTER_COUNT_SIZE)
#define VP9_COMP_INTER_COUNT_SIZE      (5 * 2)
#define VP9_COMP_REF_COUNT_START       \
		(VP9_COMP_INTER_COUNT_START + VP9_COMP_INTER_COUNT_SIZE)
#define VP9_COMP_REF_COUNT_SIZE        (5 * 2)
#define VP9_SINGLE_REF_COUNT_START     \
		(VP9_COMP_REF_COUNT_START + VP9_COMP_REF_COUNT_SIZE)
#define VP9_SINGLE_REF_COUNT_SIZE      (10 * 2)
#define VP9_TX_MODE_COUNT_START        \
		(VP9_SINGLE_REF_COUNT_START + VP9_SINGLE_REF_COUNT_SIZE)
#define VP9_TX_MODE_COUNT_SIZE         (12 * 2)
#define VP9_SKIP_COUNT_START           \
		(VP9_TX_MODE_COUNT_START + VP9_TX_MODE_COUNT_SIZE)
#define VP9_SKIP_COUNT_SIZE            (3 * 2)
#define VP9_MV_SIGN_0_COUNT_START      \
		(VP9_SKIP_COUNT_START + VP9_SKIP_COUNT_SIZE)
#define VP9_MV_SIGN_0_COUNT_SIZE       (1 * 2)
#define VP9_MV_SIGN_1_COUNT_START      \
		(VP9_MV_SIGN_0_COUNT_START + VP9_MV_SIGN_0_COUNT_SIZE)
#define VP9_MV_SIGN_1_COUNT_SIZE       (1 * 2)
#define VP9_MV_BITS_0_COUNT_START      \
		(VP9_MV_SIGN_1_COUNT_START + VP9_MV_SIGN_1_COUNT_SIZE)
#define VP9_MV_BITS_0_COUNT_SIZE       (10 * 2)
#define VP9_MV_BITS_1_COUNT_START      \
		(VP9_MV_BITS_0_COUNT_START + VP9_MV_BITS_0_COUNT_SIZE)
#define VP9_MV_BITS_1_COUNT_SIZE       (10 * 2)
#define VP9_MV_CLASS0_HP_0_COUNT_START \
		(VP9_MV_BITS_1_COUNT_START + VP9_MV_BITS_1_COUNT_SIZE)
#define VP9_MV_CLASS0_HP_0_COUNT_SIZE  (2 * 2)
#define VP9_MV_CLASS0_HP_1_COUNT_START \
		(VP9_MV_CLASS0_HP_0_COUNT_START + VP9_MV_CLASS0_HP_0_COUNT_SIZE)
#define VP9_MV_CLASS0_HP_1_COUNT_SIZE  (2 * 2)

/* Start merge_tree */
#define VP9_INTER_MODE_COUNT_START     \
		(VP9_MV_CLASS0_HP_1_COUNT_START + VP9_MV_CLASS0_HP_1_COUNT_SIZE)
#define VP9_INTER_MODE_COUNT_SIZE      (7 * 4)
#define VP9_IF_Y_MODE_COUNT_START      \
		(VP9_INTER_MODE_COUNT_START + VP9_INTER_MODE_COUNT_SIZE)
#define VP9_IF_Y_MODE_COUNT_SIZE       (10 * 4)
#define VP9_IF_UV_MODE_COUNT_START     \
		(VP9_IF_Y_MODE_COUNT_START + VP9_IF_Y_MODE_COUNT_SIZE)
#define VP9_IF_UV_MODE_COUNT_SIZE      (10 * 10)
#define VP9_PARTITION_P_COUNT_START    \
		(VP9_IF_UV_MODE_COUNT_START + VP9_IF_UV_MODE_COUNT_SIZE)
#define VP9_PARTITION_P_COUNT_SIZE     (4 * 4 * 4)
#define VP9_INTERP_COUNT_START         \
		(VP9_PARTITION_P_COUNT_START + VP9_PARTITION_P_COUNT_SIZE)
#define VP9_INTERP_COUNT_SIZE          (4 * 3)
#define VP9_MV_JOINTS_COUNT_START      \
		(VP9_INTERP_COUNT_START + VP9_INTERP_COUNT_SIZE)
#define VP9_MV_JOINTS_COUNT_SIZE       (1 * 4)
#define VP9_MV_CLASSES_0_COUNT_START   \
		(VP9_MV_JOINTS_COUNT_START + VP9_MV_JOINTS_COUNT_SIZE)
#define VP9_MV_CLASSES_0_COUNT_SIZE    (1 * 11)
#define VP9_MV_CLASS0_0_COUNT_START    \
		(VP9_MV_CLASSES_0_COUNT_START + VP9_MV_CLASSES_0_COUNT_SIZE)
#define VP9_MV_CLASS0_0_COUNT_SIZE     (1 * 2)
#define VP9_MV_CLASSES_1_COUNT_START   \
		(VP9_MV_CLASS0_0_COUNT_START + VP9_MV_CLASS0_0_COUNT_SIZE)
#define VP9_MV_CLASSES_1_COUNT_SIZE    (1 * 11)
#define VP9_MV_CLASS0_1_COUNT_START    \
		(VP9_MV_CLASSES_1_COUNT_START + VP9_MV_CLASSES_1_COUNT_SIZE)
#define VP9_MV_CLASS0_1_COUNT_SIZE     (1 * 2)
#define VP9_MV_CLASS0_FP_0_COUNT_START \
		(VP9_MV_CLASS0_1_COUNT_START + VP9_MV_CLASS0_1_COUNT_SIZE)
#define VP9_MV_CLASS0_FP_0_COUNT_SIZE  (3 * 4)
#define VP9_MV_CLASS0_FP_1_COUNT_START \
		(VP9_MV_CLASS0_FP_0_COUNT_START + VP9_MV_CLASS0_FP_0_COUNT_SIZE)
#define VP9_MV_CLASS0_FP_1_COUNT_SIZE  (3 * 4)

#define DC_PRED    0	/* Average of above and left pixels */
#define V_PRED     1	/* Vertical */
#define H_PRED     2	/* Horizontal */
#define D45_PRED   3	/* Directional 45 deg = round(arctan(1/1) * 180/pi) */
#define D135_PRED  4	/* Directional 135 deg = 180 - 45 */
#define D117_PRED  5	/* Directional 117 deg = 180 - 63 */
#define D153_PRED  6	/* Directional 153 deg = 180 - 27 */
#define D207_PRED  7	/* Directional 207 deg = 180 + 27 */
#define D63_PRED   8	/* Directional 63 deg = round(arctan(2/1) * 180/pi) */
#define TM_PRED    9	/* True-motion */

/* Use a static inline to avoid possible side effect from num being reused */
static inline int round_power_of_two(int value, int num)
{
	return (value + (1 << (num - 1))) >> num;
}

#define MODE_MV_COUNT_SAT 20
static const int count_to_update_factor[MODE_MV_COUNT_SAT + 1] = {
	0, 6, 12, 19, 25, 32, 38, 44, 51, 57, 64,
	70, 76, 83, 89, 96, 102, 108, 115, 121, 128
};

union rpm_param {
	struct {
		u16 data[RPM_BUF_SIZE];
	} l;
	struct {
		u16 profile;
		u16 show_existing_frame;
		u16 frame_to_show_idx;
		u16 frame_type; /*1 bit*/
		u16 show_frame; /*1 bit*/
		u16 error_resilient_mode; /*1 bit*/
		u16 intra_only; /*1 bit*/
		u16 display_size_present; /*1 bit*/
		u16 reset_frame_context;
		u16 refresh_frame_flags;
		u16 width;
		u16 height;
		u16 display_width;
		u16 display_height;
		u16 ref_info;
		u16 same_frame_size;
		u16 mode_ref_delta_enabled;
		u16 ref_deltas[4];
		u16 mode_deltas[2];
		u16 filter_level;
		u16 sharpness_level;
		u16 bit_depth;
		u16 seg_quant_info[8];
		u16 seg_enabled;
		u16 seg_abs_delta;
		/* bit 15: feature enabled; bit 8, sign; bit[5:0], data */
		u16 seg_lf_info[8];
	} p;
};

enum SEG_LVL_FEATURES {
	SEG_LVL_ALT_Q = 0,	/* Use alternate Quantizer */
	SEG_LVL_ALT_LF = 1,	/* Use alternate loop filter value */
	SEG_LVL_REF_FRAME = 2,	/* Optional Segment reference frame */
	SEG_LVL_SKIP = 3,	/* Optional Segment (0,0) + skip mode */
	SEG_LVL_MAX = 4		/* Number of features supported */
};

struct segmentation {
	u8 enabled;
	u8 update_map;
	u8 update_data;
	u8 abs_delta;
	u8 temporal_update;
	s16 feature_data[MAX_SEGMENTS][SEG_LVL_MAX];
	unsigned int feature_mask[MAX_SEGMENTS];
};

struct loop_filter_thresh {
	u8 mblim;
	u8 lim;
	u8 hev_thr;
};

struct loop_filter_info_n {
	struct loop_filter_thresh lfthr[MAX_LOOP_FILTER + 1];
	u8 lvl[MAX_SEGMENTS][MAX_REF_FRAMES][MAX_MODE_LF_DELTAS];
};

struct loopfilter {
	int filter_level;

	int sharpness_level;
	int last_sharpness_level;

	u8 mode_ref_delta_enabled;
	u8 mode_ref_delta_update;

	/*0 = Intra, Last, GF, ARF*/
	signed char ref_deltas[MAX_REF_LF_DELTAS];
	signed char last_ref_deltas[MAX_REF_LF_DELTAS];

	/*0 = ZERO_MV, MV*/
	signed char mode_deltas[MAX_MODE_LF_DELTAS];
	signed char last_mode_deltas[MAX_MODE_LF_DELTAS];
};

struct vp9_frame {
	struct list_head list;
	struct vb2_v4l2_buffer *vbuf;
	int index;
	int intra_only;
	int show;
	int type;
	int done;
	unsigned int width;
	unsigned int height;
};

struct codec_vp9 {
	/* VP9 context lock */
	struct mutex lock;

	/* Common part with the HEVC decoder */
	struct codec_hevc_common common;

	/* Buffer for the VP9 Workspace */
	void      *workspace_vaddr;
	dma_addr_t workspace_paddr;

	/* Contains many information parsed from the bitstream */
	union rpm_param rpm_param;

	/* Whether we detected the bitstream as 10-bit */
	int is_10bit;

	/* Coded resolution reported by the hardware */
	u32 width, height;

	/* All ref frames used by the HW at a given time */
	struct list_head ref_frames_list;
	u32 frames_num;

	/* In case of downsampling (decoding with FBC but outputting in NV12M),
	 * we need to allocate additional buffers for FBC.
	 */
	void      *fbc_buffer_vaddr[MAX_REF_PIC_NUM];
	dma_addr_t fbc_buffer_paddr[MAX_REF_PIC_NUM];

	int ref_frame_map[REF_FRAMES];
	int next_ref_frame_map[REF_FRAMES];
	struct vp9_frame *frame_refs[REFS_PER_FRAME];

	u32 lcu_total;

	/* loop filter */
	int default_filt_lvl;
	struct loop_filter_info_n lfi;
	struct loopfilter lf;
	struct segmentation seg_4lf;

	struct vp9_frame *cur_frame;
	struct vp9_frame *prev_frame;
};

static int div_r32(s64 m, int n)
{
	s64 qu = div_s64(m, n);

	return (int)qu;
}

static int clip_prob(int p)
{
	return clamp_val(p, 1, 255);
}

static int segfeature_active(struct segmentation *seg, int segment_id,
			     enum SEG_LVL_FEATURES feature_id)
{
	return seg->enabled &&
		(seg->feature_mask[segment_id] & (1 << feature_id));
}

static int get_segdata(struct segmentation *seg, int segment_id,
		       enum SEG_LVL_FEATURES feature_id)
{
	return seg->feature_data[segment_id][feature_id];
}

static void vp9_update_sharpness(struct loop_filter_info_n *lfi,
				 int sharpness_lvl)
{
	int lvl;

	/* For each possible value for the loop filter fill out limits*/
	for (lvl = 0; lvl <= MAX_LOOP_FILTER; lvl++) {
		/* Set loop filter parameters that control sharpness.*/
		int block_inside_limit = lvl >> ((sharpness_lvl > 0) +
					(sharpness_lvl > 4));

		if (sharpness_lvl > 0) {
			if (block_inside_limit > (9 - sharpness_lvl))
				block_inside_limit = (9 - sharpness_lvl);
		}

		if (block_inside_limit < 1)
			block_inside_limit = 1;

		lfi->lfthr[lvl].lim = (u8)block_inside_limit;
		lfi->lfthr[lvl].mblim = (u8)(2 * (lvl + 2) +
				block_inside_limit);
	}
}

/* Instantiate this function once when decode is started */
static void
vp9_loop_filter_init(struct amvdec_core *core, struct codec_vp9 *vp9)
{
	struct loop_filter_info_n *lfi = &vp9->lfi;
	struct loopfilter *lf = &vp9->lf;
	struct segmentation *seg_4lf = &vp9->seg_4lf;
	int i;

	memset(lfi, 0, sizeof(struct loop_filter_info_n));
	memset(lf, 0, sizeof(struct loopfilter));
	memset(seg_4lf, 0, sizeof(struct segmentation));
	lf->sharpness_level = 0;
	vp9_update_sharpness(lfi, lf->sharpness_level);
	lf->last_sharpness_level = lf->sharpness_level;

	for (i = 0; i < 32; i++) {
		unsigned int thr;

		thr = ((lfi->lfthr[i * 2 + 1].lim & 0x3f) << 8) |
			(lfi->lfthr[i * 2 + 1].mblim & 0xff);
		thr = (thr << 16) | ((lfi->lfthr[i * 2].lim & 0x3f) << 8) |
			(lfi->lfthr[i * 2].mblim & 0xff);

		amvdec_write_dos(core, HEVC_DBLK_CFG9, thr);
	}

	if (core->platform->revision >= VDEC_REVISION_SM1)
		amvdec_write_dos(core, HEVC_DBLK_CFGB,
				 (0x3 << 14) | /* dw fifo thres r and b */
				 (0x3 << 12) | /* dw fifo thres r or b */
				 (0x3 << 10) | /* dw fifo thres not r/b */
				 BIT(0)); /* VP9 video format */
	else if (core->platform->revision >= VDEC_REVISION_G12A)
		/* VP9 video format */
		amvdec_write_dos(core, HEVC_DBLK_CFGB, (0x54 << 8) | BIT(0));
	else
		amvdec_write_dos(core, HEVC_DBLK_CFGB, 0x40400001);
}

static void
vp9_loop_filter_frame_init(struct amvdec_core *core, struct segmentation *seg,
			   struct loop_filter_info_n *lfi,
			   struct loopfilter *lf, int default_filt_lvl)
{
	int i;
	int seg_id;

	/*
	 * n_shift is the multiplier for lf_deltas
	 * the multiplier is:
	 * - 1 for when filter_lvl is between 0 and 31
	 * - 2 when filter_lvl is between 32 and 63
	 */
	const int scale = 1 << (default_filt_lvl >> 5);

	/* update limits if sharpness has changed */
	if (lf->last_sharpness_level != lf->sharpness_level) {
		vp9_update_sharpness(lfi, lf->sharpness_level);
		lf->last_sharpness_level = lf->sharpness_level;

		/* Write to register */
		for (i = 0; i < 32; i++) {
			unsigned int thr;

			thr = ((lfi->lfthr[i * 2 + 1].lim & 0x3f) << 8) |
			      (lfi->lfthr[i * 2 + 1].mblim & 0xff);
			thr = (thr << 16) |
			      ((lfi->lfthr[i * 2].lim & 0x3f) << 8) |
			      (lfi->lfthr[i * 2].mblim & 0xff);

			amvdec_write_dos(core, HEVC_DBLK_CFG9, thr);
		}
	}

	for (seg_id = 0; seg_id < MAX_SEGMENTS; seg_id++) {
		int lvl_seg = default_filt_lvl;

		if (segfeature_active(seg, seg_id, SEG_LVL_ALT_LF)) {
			const int data = get_segdata(seg, seg_id,
						SEG_LVL_ALT_LF);
			lvl_seg = clamp_t(int,
					  seg->abs_delta == SEGMENT_ABSDATA ?
						data : default_filt_lvl + data,
					  0, MAX_LOOP_FILTER);
		}

		if (!lf->mode_ref_delta_enabled) {
			/*
			 * We could get rid of this if we assume that deltas
			 * are set to zero when not in use.
			 * encoder always uses deltas
			 */
			memset(lfi->lvl[seg_id], lvl_seg,
			       sizeof(lfi->lvl[seg_id]));
		} else {
			int ref, mode;
			const int intra_lvl =
				lvl_seg + lf->ref_deltas[INTRA_FRAME] * scale;
			lfi->lvl[seg_id][INTRA_FRAME][0] =
				clamp_val(intra_lvl, 0, MAX_LOOP_FILTER);

			for (ref = LAST_FRAME; ref < MAX_REF_FRAMES; ++ref) {
				for (mode = 0; mode < MAX_MODE_LF_DELTAS;
				     ++mode) {
					const int inter_lvl =
						lvl_seg +
						lf->ref_deltas[ref] * scale +
						lf->mode_deltas[mode] * scale;
					lfi->lvl[seg_id][ref][mode] =
						clamp_val(inter_lvl, 0,
							  MAX_LOOP_FILTER);
				}
			}
		}
	}

	for (i = 0; i < 16; i++) {
		unsigned int level;

		level = ((lfi->lvl[i >> 1][3][i & 1] & 0x3f) << 24) |
			((lfi->lvl[i >> 1][2][i & 1] & 0x3f) << 16) |
			((lfi->lvl[i >> 1][1][i & 1] & 0x3f) << 8) |
			(lfi->lvl[i >> 1][0][i & 1] & 0x3f);
		if (!default_filt_lvl)
			level = 0;

		amvdec_write_dos(core, HEVC_DBLK_CFGA, level);
	}
}

static void codec_vp9_flush_output(struct amvdec_session *sess)
{
	struct codec_vp9 *vp9 = sess->priv;
	struct vp9_frame *tmp, *n;

	mutex_lock(&vp9->lock);
	list_for_each_entry_safe(tmp, n, &vp9->ref_frames_list, list) {
		if (!tmp->done) {
			if (tmp->show)
				amvdec_dst_buf_done(sess, tmp->vbuf,
						    V4L2_FIELD_NONE);
			else
				v4l2_m2m_buf_queue(sess->m2m_ctx, tmp->vbuf);

			vp9->frames_num--;
		}

		list_del(&tmp->list);
		kfree(tmp);
	}
	mutex_unlock(&vp9->lock);
}

static u32 codec_vp9_num_pending_bufs(struct amvdec_session *sess)
{
	struct codec_vp9 *vp9 = sess->priv;

	if (!vp9)
		return 0;

	return vp9->frames_num;
}

static int codec_vp9_alloc_workspace(struct amvdec_core *core,
				     struct codec_vp9 *vp9)
{
	/* Allocate some memory for the VP9 decoder's state */
	vp9->workspace_vaddr = dma_alloc_coherent(core->dev, SIZE_WORKSPACE,
						  &vp9->workspace_paddr,
						  GFP_KERNEL);
	if (!vp9->workspace_vaddr) {
		dev_err(core->dev, "Failed to allocate VP9 Workspace\n");
		return -ENOMEM;
	}

	return 0;
}

static void codec_vp9_setup_workspace(struct amvdec_session *sess,
				      struct codec_vp9 *vp9)
{
	struct amvdec_core *core = sess->core;
	u32 revision = core->platform->revision;
	dma_addr_t wkaddr = vp9->workspace_paddr;

	amvdec_write_dos(core, HEVCD_IPP_LINEBUFF_BASE, wkaddr + IPP_OFFSET);
	amvdec_write_dos(core, VP9_RPM_BUFFER, wkaddr + RPM_OFFSET);
	amvdec_write_dos(core, VP9_SHORT_TERM_RPS, wkaddr + SH_TM_RPS_OFFSET);
	amvdec_write_dos(core, VP9_PPS_BUFFER, wkaddr + PPS_OFFSET);
	amvdec_write_dos(core, VP9_SAO_UP, wkaddr + SAO_UP_OFFSET);

	amvdec_write_dos(core, VP9_STREAM_SWAP_BUFFER,
			 wkaddr + SWAP_BUF_OFFSET);
	amvdec_write_dos(core, VP9_STREAM_SWAP_BUFFER2,
			 wkaddr + SWAP_BUF2_OFFSET);
	amvdec_write_dos(core, VP9_SCALELUT, wkaddr + SCALELUT_OFFSET);

	if (core->platform->revision >= VDEC_REVISION_G12A)
		amvdec_write_dos(core, HEVC_DBLK_CFGE,
				 wkaddr + DBLK_PARA_OFFSET);

	amvdec_write_dos(core, HEVC_DBLK_CFG4, wkaddr + DBLK_PARA_OFFSET);
	amvdec_write_dos(core, HEVC_DBLK_CFG5, wkaddr + DBLK_DATA_OFFSET);
	amvdec_write_dos(core, VP9_SEG_MAP_BUFFER, wkaddr + SEG_MAP_OFFSET);
	amvdec_write_dos(core, VP9_PROB_SWAP_BUFFER, wkaddr + PROB_OFFSET);
	amvdec_write_dos(core, VP9_COUNT_SWAP_BUFFER, wkaddr + COUNT_OFFSET);
	amvdec_write_dos(core, LMEM_DUMP_ADR, wkaddr + LMEM_OFFSET);

	if (codec_hevc_use_mmu(revision, sess->pixfmt_cap, vp9->is_10bit)) {
		amvdec_write_dos(core, HEVC_SAO_MMU_VH0_ADDR,
				 wkaddr + MMU_VBH_OFFSET);
		amvdec_write_dos(core, HEVC_SAO_MMU_VH1_ADDR,
				 wkaddr + MMU_VBH_OFFSET + (MMU_VBH_SIZE / 2));

		if (revision >= VDEC_REVISION_G12A)
			amvdec_write_dos(core, HEVC_ASSIST_MMU_MAP_ADDR,
					 vp9->common.mmu_map_paddr);
		else
			amvdec_write_dos(core, VP9_MMU_MAP_BUFFER,
					 vp9->common.mmu_map_paddr);
	}
}

static int codec_vp9_start(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_vp9 *vp9;
	u32 val;
	int i;
	int ret;

	vp9 = kzalloc(sizeof(*vp9), GFP_KERNEL);
	if (!vp9)
		return -ENOMEM;

	ret = codec_vp9_alloc_workspace(core, vp9);
	if (ret)
		goto free_vp9;

	codec_vp9_setup_workspace(sess, vp9);
	amvdec_write_dos_bits(core, HEVC_STREAM_CONTROL, BIT(0));
	/* stream_fifo_hole */
	if (core->platform->revision >= VDEC_REVISION_G12A)
		amvdec_write_dos_bits(core, HEVC_STREAM_FIFO_CTL, BIT(29));

	val = amvdec_read_dos(core, HEVC_PARSER_INT_CONTROL) & 0x7fffffff;
	val |= (3 << 29) | BIT(24) | BIT(22) | BIT(7) | BIT(4) | BIT(0);
	amvdec_write_dos(core, HEVC_PARSER_INT_CONTROL, val);
	amvdec_write_dos_bits(core, HEVC_SHIFT_STATUS, BIT(0));
	amvdec_write_dos(core, HEVC_SHIFT_CONTROL, BIT(10) | BIT(9) |
			 (3 << 6) | BIT(5) | BIT(2) | BIT(1) | BIT(0));
	amvdec_write_dos(core, HEVC_CABAC_CONTROL, BIT(0));
	amvdec_write_dos(core, HEVC_PARSER_CORE_CONTROL, BIT(0));
	amvdec_write_dos(core, HEVC_SHIFT_STARTCODE, 0x00000001);

	amvdec_write_dos(core, VP9_DEC_STATUS_REG, 0);

	amvdec_write_dos(core, HEVC_PARSER_CMD_WRITE, BIT(16));
	for (i = 0; i < ARRAY_SIZE(vdec_hevc_parser_cmd); ++i)
		amvdec_write_dos(core, HEVC_PARSER_CMD_WRITE,
				 vdec_hevc_parser_cmd[i]);

	amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_0, PARSER_CMD_SKIP_CFG_0);
	amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_1, PARSER_CMD_SKIP_CFG_1);
	amvdec_write_dos(core, HEVC_PARSER_CMD_SKIP_2, PARSER_CMD_SKIP_CFG_2);
	amvdec_write_dos(core, HEVC_PARSER_IF_CONTROL,
			 BIT(5) | BIT(2) | BIT(0));

	amvdec_write_dos(core, HEVCD_IPP_TOP_CNTL, BIT(0));
	amvdec_write_dos(core, HEVCD_IPP_TOP_CNTL, BIT(1));

	amvdec_write_dos(core, VP9_WAIT_FLAG, 1);

	/* clear mailbox interrupt */
	amvdec_write_dos(core, HEVC_ASSIST_MBOX1_CLR_REG, 1);
	/* enable mailbox interrupt */
	amvdec_write_dos(core, HEVC_ASSIST_MBOX1_MASK, 1);
	/* disable PSCALE for hardware sharing */
	amvdec_write_dos(core, HEVC_PSCALE_CTRL, 0);
	/* Let the uCode do all the parsing */
	amvdec_write_dos(core, NAL_SEARCH_CTL, 0x8);

	amvdec_write_dos(core, DECODE_STOP_POS, 0);
	amvdec_write_dos(core, VP9_DECODE_MODE, DECODE_MODE_SINGLE);

	pr_debug("decode_count: %u; decode_size: %u\n",
		 amvdec_read_dos(core, HEVC_DECODE_COUNT),
		 amvdec_read_dos(core, HEVC_DECODE_SIZE));

	vp9_loop_filter_init(core, vp9);

	INIT_LIST_HEAD(&vp9->ref_frames_list);
	mutex_init(&vp9->lock);
	memset(&vp9->ref_frame_map, -1, sizeof(vp9->ref_frame_map));
	memset(&vp9->next_ref_frame_map, -1, sizeof(vp9->next_ref_frame_map));
	for (i = 0; i < REFS_PER_FRAME; ++i)
		vp9->frame_refs[i] = NULL;
	sess->priv = vp9;

	return 0;

free_vp9:
	kfree(vp9);
	return ret;
}

static int codec_vp9_stop(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_vp9 *vp9 = sess->priv;

	mutex_lock(&vp9->lock);
	if (vp9->workspace_vaddr)
		dma_free_coherent(core->dev, SIZE_WORKSPACE,
				  vp9->workspace_vaddr,
				  vp9->workspace_paddr);

	codec_hevc_free_fbc_buffers(sess, &vp9->common);
	mutex_unlock(&vp9->lock);

	return 0;
}

/*
 * Program LAST & GOLDEN frames into the motion compensation reference cache
 * controller
 */
static void codec_vp9_set_mcrcc(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_vp9 *vp9 = sess->priv;
	u32 val;

	/* Reset mcrcc */
	amvdec_write_dos(core, HEVCD_MCRCC_CTL1, 0x2);
	/* Disable on I-frame */
	if (vp9->cur_frame->type == KEY_FRAME || vp9->cur_frame->intra_only) {
		amvdec_write_dos(core, HEVCD_MCRCC_CTL1, 0x0);
		return;
	}

	amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, BIT(1));
	val = amvdec_read_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR) & 0xffff;
	val |= (val << 16);
	amvdec_write_dos(core, HEVCD_MCRCC_CTL2, val);
	val = amvdec_read_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR) & 0xffff;
	val |= (val << 16);
	amvdec_write_dos(core, HEVCD_MCRCC_CTL3, val);

	/* Enable mcrcc progressive-mode */
	amvdec_write_dos(core, HEVCD_MCRCC_CTL1, 0xff0);
}

static void codec_vp9_set_sao(struct amvdec_session *sess,
			      struct vb2_buffer *vb)
{
	struct amvdec_core *core = sess->core;
	struct codec_vp9 *vp9 = sess->priv;

	dma_addr_t buf_y_paddr;
	dma_addr_t buf_u_v_paddr;
	u32 val;

	if (codec_hevc_use_downsample(sess->pixfmt_cap, vp9->is_10bit))
		buf_y_paddr =
			vp9->common.fbc_buffer_paddr[vb->index];
	else
		buf_y_paddr =
		       vb2_dma_contig_plane_dma_addr(vb, 0);

	if (codec_hevc_use_fbc(sess->pixfmt_cap, vp9->is_10bit)) {
		val = amvdec_read_dos(core, HEVC_SAO_CTRL5) & ~0xff0200;
		amvdec_write_dos(core, HEVC_SAO_CTRL5, val);
		amvdec_write_dos(core, HEVC_CM_BODY_START_ADDR, buf_y_paddr);
	}

	if (sess->pixfmt_cap == V4L2_PIX_FMT_NV12M) {
		buf_y_paddr =
		       vb2_dma_contig_plane_dma_addr(vb, 0);
		buf_u_v_paddr =
		       vb2_dma_contig_plane_dma_addr(vb, 1);
		amvdec_write_dos(core, HEVC_SAO_Y_START_ADDR, buf_y_paddr);
		amvdec_write_dos(core, HEVC_SAO_C_START_ADDR, buf_u_v_paddr);
		amvdec_write_dos(core, HEVC_SAO_Y_WPTR, buf_y_paddr);
		amvdec_write_dos(core, HEVC_SAO_C_WPTR, buf_u_v_paddr);
	}

	if (codec_hevc_use_mmu(core->platform->revision, sess->pixfmt_cap,
			       vp9->is_10bit)) {
		amvdec_write_dos(core, HEVC_CM_HEADER_START_ADDR,
				 vp9->common.mmu_header_paddr[vb->index]);
		/* use HEVC_CM_HEADER_START_ADDR */
		amvdec_write_dos_bits(core, HEVC_SAO_CTRL5, BIT(10));
	}

	amvdec_write_dos(core, HEVC_SAO_Y_LENGTH,
			 amvdec_get_output_size(sess));
	amvdec_write_dos(core, HEVC_SAO_C_LENGTH,
			 (amvdec_get_output_size(sess) / 2));

	if (core->platform->revision >= VDEC_REVISION_G12A) {
		amvdec_clear_dos_bits(core, HEVC_DBLK_CFGB,
				      BIT(4) | BIT(5) | BIT(8) | BIT(9));
		/* enable first, compressed write */
		if (codec_hevc_use_fbc(sess->pixfmt_cap, vp9->is_10bit))
			amvdec_write_dos_bits(core, HEVC_DBLK_CFGB, BIT(8));

		/* enable second, uncompressed write */
		if (sess->pixfmt_cap == V4L2_PIX_FMT_NV12M)
			amvdec_write_dos_bits(core, HEVC_DBLK_CFGB, BIT(9));

		/* dblk pipeline mode=1 for performance */
		if (sess->width >= 1280)
			amvdec_write_dos_bits(core, HEVC_DBLK_CFGB, BIT(4));

		pr_debug("HEVC_DBLK_CFGB: %08X\n",
			 amvdec_read_dos(core, HEVC_DBLK_CFGB));
	}

	val = amvdec_read_dos(core, HEVC_SAO_CTRL1) & ~0x3ff0;
	val |= 0xff0; /* Set endianness for 2-bytes swaps (nv12) */
	if (core->platform->revision < VDEC_REVISION_G12A) {
		val &= ~0x3;
		if (!codec_hevc_use_fbc(sess->pixfmt_cap, vp9->is_10bit))
			val |= BIT(0); /* disable cm compression */
		/* TOFIX: Handle Amlogic Framebuffer compression */
	}

	amvdec_write_dos(core, HEVC_SAO_CTRL1, val);
	pr_debug("HEVC_SAO_CTRL1: %08X\n", val);

	/* no downscale for NV12 */
	val = amvdec_read_dos(core, HEVC_SAO_CTRL5) & ~0xff0000;
	amvdec_write_dos(core, HEVC_SAO_CTRL5, val);

	val = amvdec_read_dos(core, HEVCD_IPP_AXIIF_CONFIG) & ~0x30;
	val |= 0xf;
	val &= ~BIT(12); /* NV12 */
	amvdec_write_dos(core, HEVCD_IPP_AXIIF_CONFIG, val);
}

static dma_addr_t codec_vp9_get_frame_mv_paddr(struct codec_vp9 *vp9,
					       struct vp9_frame *frame)
{
	return vp9->workspace_paddr + MPRED_MV_OFFSET +
	       (frame->index * MPRED_MV_BUF_SIZE);
}

static void codec_vp9_set_mpred_mv(struct amvdec_core *core,
				   struct codec_vp9 *vp9)
{
	int mpred_mv_rd_end_addr;
	int use_prev_frame_mvs = vp9->prev_frame->width ==
					vp9->cur_frame->width &&
				 vp9->prev_frame->height ==
					vp9->cur_frame->height &&
				 !vp9->prev_frame->intra_only &&
				 vp9->prev_frame->show &&
				 vp9->prev_frame->type != KEY_FRAME;

	amvdec_write_dos(core, HEVC_MPRED_CTRL3, 0x24122412);
	amvdec_write_dos(core, HEVC_MPRED_ABV_START_ADDR,
			 vp9->workspace_paddr + MPRED_ABV_OFFSET);

	amvdec_clear_dos_bits(core, HEVC_MPRED_CTRL4, BIT(6));
	if (use_prev_frame_mvs)
		amvdec_write_dos_bits(core, HEVC_MPRED_CTRL4, BIT(6));

	amvdec_write_dos(core, HEVC_MPRED_MV_WR_START_ADDR,
			 codec_vp9_get_frame_mv_paddr(vp9, vp9->cur_frame));
	amvdec_write_dos(core, HEVC_MPRED_MV_WPTR,
			 codec_vp9_get_frame_mv_paddr(vp9, vp9->cur_frame));

	amvdec_write_dos(core, HEVC_MPRED_MV_RD_START_ADDR,
			 codec_vp9_get_frame_mv_paddr(vp9, vp9->prev_frame));
	amvdec_write_dos(core, HEVC_MPRED_MV_RPTR,
			 codec_vp9_get_frame_mv_paddr(vp9, vp9->prev_frame));

	mpred_mv_rd_end_addr =
			codec_vp9_get_frame_mv_paddr(vp9, vp9->prev_frame) +
			(vp9->lcu_total * MV_MEM_UNIT);
	amvdec_write_dos(core, HEVC_MPRED_MV_RD_END_ADDR, mpred_mv_rd_end_addr);
}

static void codec_vp9_update_next_ref(struct codec_vp9 *vp9)
{
	union rpm_param *param = &vp9->rpm_param;
	u32 buf_idx = vp9->cur_frame->index;
	int ref_index = 0;
	int refresh_frame_flags;
	int mask;

	refresh_frame_flags = vp9->cur_frame->type == KEY_FRAME ?
				0xff : param->p.refresh_frame_flags;

	for (mask = refresh_frame_flags; mask; mask >>= 1) {
		pr_debug("mask=%08X; ref_index=%d\n", mask, ref_index);
		if (mask & 1)
			vp9->next_ref_frame_map[ref_index] = buf_idx;
		else
			vp9->next_ref_frame_map[ref_index] =
				vp9->ref_frame_map[ref_index];

		++ref_index;
	}

	for (; ref_index < REF_FRAMES; ++ref_index)
		vp9->next_ref_frame_map[ref_index] =
			vp9->ref_frame_map[ref_index];
}

static void codec_vp9_save_refs(struct codec_vp9 *vp9)
{
	union rpm_param *param = &vp9->rpm_param;
	int i;

	for (i = 0; i < REFS_PER_FRAME; ++i) {
		const int ref = (param->p.ref_info >>
				 (((REFS_PER_FRAME - i - 1) * 4) + 1)) & 0x7;

		if (vp9->ref_frame_map[ref] < 0)
			continue;

		pr_warn("%s: FIXME, would need to save ref %d\n",
			__func__, vp9->ref_frame_map[ref]);
	}
}

static void codec_vp9_update_ref(struct codec_vp9 *vp9)
{
	union rpm_param *param = &vp9->rpm_param;
	int ref_index = 0;
	int mask;
	int refresh_frame_flags;

	if (!vp9->cur_frame)
		return;

	refresh_frame_flags = vp9->cur_frame->type == KEY_FRAME ?
				0xff : param->p.refresh_frame_flags;

	for (mask = refresh_frame_flags; mask; mask >>= 1) {
		vp9->ref_frame_map[ref_index] =
			vp9->next_ref_frame_map[ref_index];
		++ref_index;
	}

	if (param->p.show_existing_frame)
		return;

	for (; ref_index < REF_FRAMES; ++ref_index)
		vp9->ref_frame_map[ref_index] =
			vp9->next_ref_frame_map[ref_index];
}

static struct vp9_frame *codec_vp9_get_frame_by_idx(struct codec_vp9 *vp9,
						    int idx)
{
	struct vp9_frame *frame;

	list_for_each_entry(frame, &vp9->ref_frames_list, list) {
		if (frame->index == idx)
			return frame;
	}

	return NULL;
}

static void codec_vp9_sync_ref(struct codec_vp9 *vp9)
{
	union rpm_param *param = &vp9->rpm_param;
	int i;

	for (i = 0; i < REFS_PER_FRAME; ++i) {
		const int ref = (param->p.ref_info >>
				 (((REFS_PER_FRAME - i - 1) * 4) + 1)) & 0x7;
		const int idx = vp9->ref_frame_map[ref];

		vp9->frame_refs[i] = codec_vp9_get_frame_by_idx(vp9, idx);
		if (!vp9->frame_refs[i])
			pr_warn("%s: couldn't find VP9 ref %d\n", __func__,
				idx);
	}
}

static void codec_vp9_set_refs(struct amvdec_session *sess,
			       struct codec_vp9 *vp9)
{
	struct amvdec_core *core = sess->core;
	int i;

	for (i = 0; i < REFS_PER_FRAME; ++i) {
		struct vp9_frame *frame = vp9->frame_refs[i];
		int id_y;
		int id_u_v;

		if (!frame)
			continue;

		if (codec_hevc_use_fbc(sess->pixfmt_cap, vp9->is_10bit)) {
			id_y = frame->index;
			id_u_v = id_y;
		} else {
			id_y = frame->index * 2;
			id_u_v = id_y + 1;
		}

		amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR,
				 (id_u_v << 16) | (id_u_v << 8) | id_y);
	}
}

static void codec_vp9_set_mc(struct amvdec_session *sess,
			     struct codec_vp9 *vp9)
{
	struct amvdec_core *core = sess->core;
	u32 scale = 0;
	u32 sz;
	int i;

	amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, 1);
	codec_vp9_set_refs(sess, vp9);
	amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR,
			 (16 << 8) | 1);
	codec_vp9_set_refs(sess, vp9);

	amvdec_write_dos(core, VP9D_MPP_REFINFO_TBL_ACCCONFIG, BIT(2));
	for (i = 0; i < REFS_PER_FRAME; ++i) {
		if (!vp9->frame_refs[i])
			continue;

		if (vp9->frame_refs[i]->width != vp9->width ||
		    vp9->frame_refs[i]->height != vp9->height)
			scale = 1;

		sz = amvdec_am21c_body_size(vp9->frame_refs[i]->width,
					    vp9->frame_refs[i]->height);

		amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA,
				 vp9->frame_refs[i]->width);
		amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA,
				 vp9->frame_refs[i]->height);
		amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA,
				 (vp9->frame_refs[i]->width << 14) /
				 vp9->width);
		amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA,
				 (vp9->frame_refs[i]->height << 14) /
				 vp9->height);
		amvdec_write_dos(core, VP9D_MPP_REFINFO_DATA, sz >> 5);
	}

	amvdec_write_dos(core, VP9D_MPP_REF_SCALE_ENBL, scale);
}

static struct vp9_frame *codec_vp9_get_new_frame(struct amvdec_session *sess)
{
	struct codec_vp9 *vp9 = sess->priv;
	union rpm_param *param = &vp9->rpm_param;
	struct vb2_v4l2_buffer *vbuf;
	struct vp9_frame *new_frame;

	new_frame = kzalloc(sizeof(*new_frame), GFP_KERNEL);
	if (!new_frame)
		return NULL;

	vbuf = v4l2_m2m_dst_buf_remove(sess->m2m_ctx);
	if (!vbuf) {
		dev_err(sess->core->dev, "No dst buffer available\n");
		kfree(new_frame);
		return NULL;
	}

	while (codec_vp9_get_frame_by_idx(vp9, vbuf->vb2_buf.index)) {
		struct vb2_v4l2_buffer *old_vbuf = vbuf;

		vbuf = v4l2_m2m_dst_buf_remove(sess->m2m_ctx);
		v4l2_m2m_buf_queue(sess->m2m_ctx, old_vbuf);
		if (!vbuf) {
			dev_err(sess->core->dev, "No dst buffer available\n");
			kfree(new_frame);
			return NULL;
		}
	}

	new_frame->vbuf = vbuf;
	new_frame->index = vbuf->vb2_buf.index;
	new_frame->intra_only = param->p.intra_only;
	new_frame->show = param->p.show_frame;
	new_frame->type = param->p.frame_type;
	new_frame->width = vp9->width;
	new_frame->height = vp9->height;
	list_add_tail(&new_frame->list, &vp9->ref_frames_list);
	vp9->frames_num++;

	return new_frame;
}

static void codec_vp9_show_existing_frame(struct codec_vp9 *vp9)
{
	union rpm_param *param = &vp9->rpm_param;

	if (!param->p.show_existing_frame)
		return;

	pr_debug("showing frame %u\n", param->p.frame_to_show_idx);
}

static void codec_vp9_rm_noshow_frame(struct amvdec_session *sess)
{
	struct codec_vp9 *vp9 = sess->priv;
	struct vp9_frame *tmp;

	list_for_each_entry(tmp, &vp9->ref_frames_list, list) {
		if (tmp->show)
			continue;

		pr_debug("rm noshow: %u\n", tmp->index);
		v4l2_m2m_buf_queue(sess->m2m_ctx, tmp->vbuf);
		list_del(&tmp->list);
		kfree(tmp);
		vp9->frames_num--;
		return;
	}
}

static void codec_vp9_process_frame(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_vp9 *vp9 = sess->priv;
	union rpm_param *param = &vp9->rpm_param;
	int intra_only;

	if (!param->p.show_frame)
		codec_vp9_rm_noshow_frame(sess);

	vp9->cur_frame = codec_vp9_get_new_frame(sess);
	if (!vp9->cur_frame)
		return;

	pr_debug("frame %d: type: %08X; show_exist: %u; show: %u, intra_only: %u\n",
		 vp9->cur_frame->index,
		 param->p.frame_type, param->p.show_existing_frame,
		 param->p.show_frame, param->p.intra_only);

	if (param->p.frame_type != KEY_FRAME)
		codec_vp9_sync_ref(vp9);
	codec_vp9_update_next_ref(vp9);
	codec_vp9_show_existing_frame(vp9);

	if (codec_hevc_use_mmu(core->platform->revision, sess->pixfmt_cap,
			       vp9->is_10bit))
		codec_hevc_fill_mmu_map(sess, &vp9->common,
					&vp9->cur_frame->vbuf->vb2_buf);

	intra_only = param->p.show_frame ? 0 : param->p.intra_only;

	/* clear mpred (for keyframe only) */
	if (param->p.frame_type != KEY_FRAME && !intra_only) {
		codec_vp9_set_mc(sess, vp9);
		codec_vp9_set_mpred_mv(core, vp9);
	} else {
		amvdec_clear_dos_bits(core, HEVC_MPRED_CTRL4, BIT(6));
	}

	amvdec_write_dos(core, HEVC_PARSER_PICTURE_SIZE,
			 (vp9->height << 16) | vp9->width);
	codec_vp9_set_mcrcc(sess);
	codec_vp9_set_sao(sess, &vp9->cur_frame->vbuf->vb2_buf);

	vp9_loop_filter_frame_init(core, &vp9->seg_4lf,
				   &vp9->lfi, &vp9->lf,
				   vp9->default_filt_lvl);

	/* ask uCode to start decoding */
	amvdec_write_dos(core, VP9_DEC_STATUS_REG, VP9_10B_DECODE_SLICE);
}

static void codec_vp9_process_lf(struct codec_vp9 *vp9)
{
	union rpm_param *param = &vp9->rpm_param;
	int i;

	vp9->lf.mode_ref_delta_enabled = param->p.mode_ref_delta_enabled;
	vp9->lf.sharpness_level = param->p.sharpness_level;
	vp9->default_filt_lvl = param->p.filter_level;
	vp9->seg_4lf.enabled = param->p.seg_enabled;
	vp9->seg_4lf.abs_delta = param->p.seg_abs_delta;

	for (i = 0; i < 4; i++)
		vp9->lf.ref_deltas[i] = param->p.ref_deltas[i];

	for (i = 0; i < 2; i++)
		vp9->lf.mode_deltas[i] = param->p.mode_deltas[i];

	for (i = 0; i < MAX_SEGMENTS; i++)
		vp9->seg_4lf.feature_mask[i] =
			(param->p.seg_lf_info[i] & 0x8000) ?
				(1 << SEG_LVL_ALT_LF) : 0;

	for (i = 0; i < MAX_SEGMENTS; i++)
		vp9->seg_4lf.feature_data[i][SEG_LVL_ALT_LF] =
			(param->p.seg_lf_info[i] & 0x100) ?
				-(param->p.seg_lf_info[i] & 0x3f)
				: (param->p.seg_lf_info[i] & 0x3f);
}

static void codec_vp9_resume(struct amvdec_session *sess)
{
	struct codec_vp9 *vp9 = sess->priv;

	mutex_lock(&vp9->lock);
	if (codec_hevc_setup_buffers(sess, &vp9->common, vp9->is_10bit)) {
		mutex_unlock(&vp9->lock);
		amvdec_abort(sess);
		return;
	}

	codec_vp9_setup_workspace(sess, vp9);
	codec_hevc_setup_decode_head(sess, vp9->is_10bit);
	codec_vp9_process_lf(vp9);
	codec_vp9_process_frame(sess);

	mutex_unlock(&vp9->lock);
}

/*
 * The RPM section within the workspace contains
 * many information regarding the parsed bitstream
 */
static void codec_vp9_fetch_rpm(struct amvdec_session *sess)
{
	struct codec_vp9 *vp9 = sess->priv;
	u16 *rpm_vaddr = vp9->workspace_vaddr + RPM_OFFSET;
	int i, j;

	for (i = 0; i < RPM_BUF_SIZE; i += 4)
		for (j = 0; j < 4; j++)
			vp9->rpm_param.l.data[i + j] = rpm_vaddr[i + 3 - j];
}

static int codec_vp9_process_rpm(struct codec_vp9 *vp9)
{
	union rpm_param *param = &vp9->rpm_param;
	int src_changed = 0;
	int is_10bit = 0;
	int pic_width_64 = ALIGN(param->p.width, 64);
	int pic_height_32 = ALIGN(param->p.height, 32);
	int pic_width_lcu  = (pic_width_64 % LCU_SIZE) ?
				pic_width_64 / LCU_SIZE  + 1
				: pic_width_64 / LCU_SIZE;
	int pic_height_lcu = (pic_height_32 % LCU_SIZE) ?
				pic_height_32 / LCU_SIZE + 1
				: pic_height_32 / LCU_SIZE;
	vp9->lcu_total = pic_width_lcu * pic_height_lcu;

	if (param->p.bit_depth == 10)
		is_10bit = 1;

	if (vp9->width != param->p.width || vp9->height != param->p.height ||
	    vp9->is_10bit != is_10bit)
		src_changed = 1;

	vp9->width = param->p.width;
	vp9->height = param->p.height;
	vp9->is_10bit = is_10bit;

	pr_debug("width: %u; height: %u; is_10bit: %d; src_changed: %d\n",
		 vp9->width, vp9->height, is_10bit, src_changed);

	return src_changed;
}

static bool codec_vp9_is_ref(struct codec_vp9 *vp9, struct vp9_frame *frame)
{
	int i;

	for (i = 0; i < REF_FRAMES; ++i)
		if (vp9->ref_frame_map[i] == frame->index)
			return true;

	return false;
}

static void codec_vp9_show_frame(struct amvdec_session *sess)
{
	struct codec_vp9 *vp9 = sess->priv;
	struct vp9_frame *tmp, *n;

	list_for_each_entry_safe(tmp, n, &vp9->ref_frames_list, list) {
		if (!tmp->show || tmp == vp9->cur_frame)
			continue;

		if (!tmp->done) {
			pr_debug("Doning %u\n", tmp->index);
			amvdec_dst_buf_done(sess, tmp->vbuf, V4L2_FIELD_NONE);
			tmp->done = 1;
			vp9->frames_num--;
		}

		if (codec_vp9_is_ref(vp9, tmp) || tmp == vp9->prev_frame)
			continue;

		pr_debug("deleting %d\n", tmp->index);
		list_del(&tmp->list);
		kfree(tmp);
	}
}

static void vp9_tree_merge_probs(unsigned int *prev_prob,
				 unsigned int *cur_prob,
				 int coef_node_start, int tree_left,
				 int tree_right,
				 int tree_i, int node)
{
	int prob_32, prob_res, prob_shift;
	int pre_prob, new_prob;
	int den, m_count, get_prob, factor;

	prob_32 = prev_prob[coef_node_start / 4 * 2];
	prob_res = coef_node_start & 3;
	prob_shift = prob_res * 8;
	pre_prob = (prob_32 >> prob_shift) & 0xff;

	den = tree_left + tree_right;

	if (den == 0) {
		new_prob = pre_prob;
	} else {
		m_count = den < MODE_MV_COUNT_SAT ? den : MODE_MV_COUNT_SAT;
		get_prob =
			clip_prob(div_r32(((int64_t)tree_left * 256 +
					   (den >> 1)),
					  den));

		/* weighted_prob */
		factor = count_to_update_factor[m_count];
		new_prob = round_power_of_two(pre_prob * (256 - factor) +
					      get_prob * factor, 8);
	}

	cur_prob[coef_node_start / 4 * 2] =
		(cur_prob[coef_node_start / 4 * 2] & (~(0xff << prob_shift))) |
		(new_prob << prob_shift);
}

static void adapt_coef_probs_cxt(unsigned int *prev_prob,
				 unsigned int *cur_prob,
				 unsigned int *count,
				 int update_factor,
				 int cxt_num,
				 int coef_cxt_start,
				 int coef_count_cxt_start)
{
	int prob_32, prob_res, prob_shift;
	int pre_prob, new_prob;
	int num, den, m_count, get_prob, factor;
	int node, coef_node_start;
	int count_sat = 24;
	int cxt;

	for (cxt = 0; cxt < cxt_num; cxt++) {
		const int n0 = count[coef_count_cxt_start];
		const int n1 = count[coef_count_cxt_start + 1];
		const int n2 = count[coef_count_cxt_start + 2];
		const int neob = count[coef_count_cxt_start + 3];
		const int nneob = count[coef_count_cxt_start + 4];
		const unsigned int branch_ct[3][2] = {
			{ neob, nneob },
			{ n0, n1 + n2 },
			{ n1, n2 }
		};

		coef_node_start = coef_cxt_start;
		for (node = 0 ; node < 3 ; node++) {
			prob_32 = prev_prob[coef_node_start / 4 * 2];
			prob_res = coef_node_start & 3;
			prob_shift = prob_res * 8;
			pre_prob = (prob_32 >> prob_shift) & 0xff;

			/* get binary prob */
			num = branch_ct[node][0];
			den = branch_ct[node][0] + branch_ct[node][1];
			m_count = den < count_sat ? den : count_sat;

			get_prob = (den == 0) ?
					128u :
					clip_prob(div_r32(((int64_t)num * 256 +
							  (den >> 1)), den));

			factor = update_factor * m_count / count_sat;
			new_prob =
				round_power_of_two(pre_prob * (256 - factor) +
						   get_prob * factor, 8);

			cur_prob[coef_node_start / 4 * 2] =
				(cur_prob[coef_node_start / 4 * 2] &
				 (~(0xff << prob_shift))) |
				(new_prob << prob_shift);

			coef_node_start += 1;
		}

		coef_cxt_start = coef_cxt_start + 3;
		coef_count_cxt_start = coef_count_cxt_start + 5;
	}
}

static void adapt_coef_probs(int prev_kf, int cur_kf, int pre_fc,
			     unsigned int *prev_prob, unsigned int *cur_prob,
			     unsigned int *count)
{
	int tx_size, coef_tx_size_start, coef_count_tx_size_start;
	int plane, coef_plane_start, coef_count_plane_start;
	int type, coef_type_start, coef_count_type_start;
	int band, coef_band_start, coef_count_band_start;
	int cxt_num;
	int coef_cxt_start, coef_count_cxt_start;
	int node, coef_node_start, coef_count_node_start;

	int tree_i, tree_left, tree_right;
	int mvd_i;

	int update_factor = cur_kf ? 112 : (prev_kf ? 128 : 112);

	int prob_32;
	int prob_res;
	int prob_shift;
	int pre_prob;

	int den;
	int get_prob;
	int m_count;
	int factor;

	int new_prob;

	for (tx_size = 0 ; tx_size < 4 ; tx_size++) {
		coef_tx_size_start = VP9_COEF_START +
				tx_size * 4 * VP9_COEF_SIZE_ONE_SET;
		coef_count_tx_size_start = VP9_COEF_COUNT_START +
				tx_size * 4 * VP9_COEF_COUNT_SIZE_ONE_SET;
		coef_plane_start = coef_tx_size_start;
		coef_count_plane_start = coef_count_tx_size_start;

		for (plane = 0 ; plane < 2 ; plane++) {
			coef_type_start = coef_plane_start;
			coef_count_type_start = coef_count_plane_start;

			for (type = 0 ; type < 2 ; type++) {
				coef_band_start = coef_type_start;
				coef_count_band_start = coef_count_type_start;

				for (band = 0 ; band < 6 ; band++) {
					if (band == 0)
						cxt_num = 3;
					else
						cxt_num = 6;
					coef_cxt_start = coef_band_start;
					coef_count_cxt_start =
						coef_count_band_start;

					adapt_coef_probs_cxt(prev_prob,
							     cur_prob,
							     count,
							     update_factor,
							     cxt_num,
							     coef_cxt_start,
							coef_count_cxt_start);

					if (band == 0) {
						coef_band_start += 10;
						coef_count_band_start += 15;
					} else {
						coef_band_start += 18;
						coef_count_band_start += 30;
					}
				}
				coef_type_start += VP9_COEF_SIZE_ONE_SET;
				coef_count_type_start +=
					VP9_COEF_COUNT_SIZE_ONE_SET;
			}

			coef_plane_start += 2 * VP9_COEF_SIZE_ONE_SET;
			coef_count_plane_start +=
				2 * VP9_COEF_COUNT_SIZE_ONE_SET;
		}
	}

	if (cur_kf == 0) {
		/* mode_mv_merge_probs - merge_intra_inter_prob */
		for (coef_count_node_start = VP9_INTRA_INTER_COUNT_START;
		     coef_count_node_start < (VP9_MV_CLASS0_HP_1_COUNT_START +
					      VP9_MV_CLASS0_HP_1_COUNT_SIZE);
		     coef_count_node_start += 2) {
			if (coef_count_node_start ==
					VP9_INTRA_INTER_COUNT_START)
				coef_node_start = VP9_INTRA_INTER_START;
			else if (coef_count_node_start ==
					VP9_COMP_INTER_COUNT_START)
				coef_node_start = VP9_COMP_INTER_START;
			else if (coef_count_node_start ==
					VP9_TX_MODE_COUNT_START)
				coef_node_start = VP9_TX_MODE_START;
			else if (coef_count_node_start ==
					VP9_SKIP_COUNT_START)
				coef_node_start = VP9_SKIP_START;
			else if (coef_count_node_start ==
					VP9_MV_SIGN_0_COUNT_START)
				coef_node_start = VP9_MV_SIGN_0_START;
			else if (coef_count_node_start ==
					VP9_MV_SIGN_1_COUNT_START)
				coef_node_start = VP9_MV_SIGN_1_START;
			else if (coef_count_node_start ==
					VP9_MV_BITS_0_COUNT_START)
				coef_node_start = VP9_MV_BITS_0_START;
			else if (coef_count_node_start ==
					VP9_MV_BITS_1_COUNT_START)
				coef_node_start = VP9_MV_BITS_1_START;
			else if (coef_count_node_start ==
					VP9_MV_CLASS0_HP_0_COUNT_START)
				coef_node_start = VP9_MV_CLASS0_HP_0_START;

			den = count[coef_count_node_start] +
			      count[coef_count_node_start + 1];

			prob_32 = prev_prob[coef_node_start / 4 * 2];
			prob_res = coef_node_start & 3;
			prob_shift = prob_res * 8;
			pre_prob = (prob_32 >> prob_shift) & 0xff;

			if (den == 0) {
				new_prob = pre_prob;
			} else {
				m_count = den < MODE_MV_COUNT_SAT ?
						den : MODE_MV_COUNT_SAT;
				get_prob =
				clip_prob(div_r32(((int64_t)
					count[coef_count_node_start] * 256 +
					(den >> 1)),
					den));

				/* weighted prob */
				factor = count_to_update_factor[m_count];
				new_prob =
					round_power_of_two(pre_prob *
							   (256 - factor) +
							   get_prob * factor,
							   8);
			}

			cur_prob[coef_node_start / 4 * 2] =
				(cur_prob[coef_node_start / 4 * 2] &
				 (~(0xff << prob_shift))) |
				(new_prob << prob_shift);

			coef_node_start = coef_node_start + 1;
		}

		coef_node_start = VP9_INTER_MODE_START;
		coef_count_node_start = VP9_INTER_MODE_COUNT_START;
		for (tree_i = 0 ; tree_i < 7 ; tree_i++) {
			for (node = 0 ; node < 3 ; node++) {
				unsigned int start = coef_count_node_start;

				switch (node) {
				case 2:
					tree_left = count[start + 1];
					tree_right = count[start + 3];
					break;
				case 1:
					tree_left = count[start + 0];
					tree_right = count[start + 1] +
						     count[start + 3];
					break;
				default:
					tree_left = count[start + 2];
					tree_right = count[start + 0] +
						     count[start + 1] +
						     count[start + 3];
					break;
				}

				vp9_tree_merge_probs(prev_prob, cur_prob,
						     coef_node_start,
						     tree_left, tree_right,
						     tree_i, node);

				coef_node_start = coef_node_start + 1;
			}

			coef_count_node_start = coef_count_node_start + 4;
		}

		coef_node_start = VP9_IF_Y_MODE_START;
		coef_count_node_start = VP9_IF_Y_MODE_COUNT_START;
		for (tree_i = 0 ; tree_i < 14 ; tree_i++) {
			for (node = 0 ; node < 9 ; node++) {
				unsigned int start = coef_count_node_start;

				switch (node) {
				case 8:
					tree_left =
						count[start + D153_PRED];
					tree_right =
						count[start + D207_PRED];
					break;
				case 7:
					tree_left =
						count[start + D63_PRED];
					tree_right =
						count[start + D207_PRED] +
						count[start + D153_PRED];
					break;
				case 6:
					tree_left =
						count[start + D45_PRED];
					tree_right =
						count[start + D207_PRED] +
						count[start + D153_PRED] +
						count[start + D63_PRED];
					break;
				case 5:
					tree_left =
						count[start + D135_PRED];
					tree_right =
						count[start + D117_PRED];
					break;
				case 4:
					tree_left =
						count[start + H_PRED];
					tree_right =
						count[start + D117_PRED] +
						count[start + D135_PRED];
					break;
				case 3:
					tree_left =
						count[start + H_PRED] +
						count[start + D117_PRED] +
						count[start + D135_PRED];
					tree_right =
						count[start + D45_PRED] +
						count[start + D207_PRED] +
						count[start + D153_PRED] +
						count[start + D63_PRED];
					break;
				case 2:
					tree_left =
						count[start + V_PRED];
					tree_right =
						count[start + H_PRED] +
						count[start + D117_PRED] +
						count[start + D135_PRED] +
						count[start + D45_PRED] +
						count[start + D207_PRED] +
						count[start + D153_PRED] +
						count[start + D63_PRED];
					break;
				case 1:
					tree_left =
						count[start + TM_PRED];
					tree_right =
						count[start + V_PRED] +
						count[start + H_PRED] +
						count[start + D117_PRED] +
						count[start + D135_PRED] +
						count[start + D45_PRED] +
						count[start + D207_PRED] +
						count[start + D153_PRED] +
						count[start + D63_PRED];
					break;
				default:
					tree_left =
						count[start + DC_PRED];
					tree_right =
						count[start + TM_PRED] +
						count[start + V_PRED] +
						count[start + H_PRED] +
						count[start + D117_PRED] +
						count[start + D135_PRED] +
						count[start + D45_PRED] +
						count[start + D207_PRED] +
						count[start + D153_PRED] +
						count[start + D63_PRED];
					break;
				}

				vp9_tree_merge_probs(prev_prob, cur_prob,
						     coef_node_start,
						     tree_left, tree_right,
						     tree_i, node);

				coef_node_start = coef_node_start + 1;
			}
			coef_count_node_start = coef_count_node_start + 10;
		}

		coef_node_start = VP9_PARTITION_P_START;
		coef_count_node_start = VP9_PARTITION_P_COUNT_START;
		for (tree_i = 0 ; tree_i < 16 ; tree_i++) {
			for (node = 0 ; node < 3 ; node++) {
				unsigned int start = coef_count_node_start;

				switch (node) {
				case 2:
					tree_left = count[start + 2];
					tree_right = count[start + 3];
					break;
				case 1:
					tree_left = count[start + 1];
					tree_right = count[start + 2] +
						     count[start + 3];
					break;
				default:
					tree_left = count[start + 0];
					tree_right = count[start + 1] +
						     count[start + 2] +
						     count[start + 3];
					break;
				}

				vp9_tree_merge_probs(prev_prob, cur_prob,
						     coef_node_start,
						     tree_left, tree_right,
						     tree_i, node);

				coef_node_start = coef_node_start + 1;
			}

			coef_count_node_start = coef_count_node_start + 4;
		}

		coef_node_start = VP9_INTERP_START;
		coef_count_node_start = VP9_INTERP_COUNT_START;
		for (tree_i = 0 ; tree_i < 4 ; tree_i++) {
			for (node = 0 ; node < 2 ; node++) {
				unsigned int start = coef_count_node_start;

				switch (node) {
				case 1:
					tree_left = count[start + 1];
					tree_right = count[start + 2];
					break;
				default:
					tree_left = count[start + 0];
					tree_right = count[start + 1] +
						     count[start + 2];
					break;
				}

				vp9_tree_merge_probs(prev_prob, cur_prob,
						     coef_node_start,
						     tree_left, tree_right,
						     tree_i, node);

				coef_node_start = coef_node_start + 1;
			}
			coef_count_node_start = coef_count_node_start + 3;
		}

		coef_node_start = VP9_MV_JOINTS_START;
		coef_count_node_start = VP9_MV_JOINTS_COUNT_START;
		for (tree_i = 0 ; tree_i < 1 ; tree_i++) {
			for (node = 0 ; node < 3 ; node++) {
				unsigned int start = coef_count_node_start;

				switch (node) {
				case 2:
					tree_left = count[start + 2];
					tree_right = count[start + 3];
					break;
				case 1:
					tree_left = count[start + 1];
					tree_right = count[start + 2] +
						     count[start + 3];
					break;
				default:
					tree_left = count[start + 0];
					tree_right = count[start + 1] +
						     count[start + 2] +
						     count[start + 3];
					break;
				}

				vp9_tree_merge_probs(prev_prob, cur_prob,
						     coef_node_start,
						     tree_left, tree_right,
						     tree_i, node);

				coef_node_start = coef_node_start + 1;
			}
			coef_count_node_start = coef_count_node_start + 4;
		}

		for (mvd_i = 0 ; mvd_i < 2 ; mvd_i++) {
			coef_node_start = mvd_i ? VP9_MV_CLASSES_1_START :
						  VP9_MV_CLASSES_0_START;
			coef_count_node_start = mvd_i ?
					VP9_MV_CLASSES_1_COUNT_START :
					VP9_MV_CLASSES_0_COUNT_START;
			tree_i = 0;
			for (node = 0; node < 10; node++) {
				unsigned int start = coef_count_node_start;

				switch (node) {
				case 9:
					tree_left = count[start + 9];
					tree_right = count[start + 10];
					break;
				case 8:
					tree_left = count[start + 7];
					tree_right = count[start + 8];
					break;
				case 7:
					tree_left = count[start + 7] +
						     count[start + 8];
					tree_right = count[start + 9] +
						     count[start + 10];
					break;
				case 6:
					tree_left = count[start + 6];
					tree_right = count[start + 7] +
						     count[start + 8] +
						     count[start + 9] +
						     count[start + 10];
					break;
				case 5:
					tree_left = count[start + 4];
					tree_right = count[start + 5];
					break;
				case 4:
					tree_left = count[start + 4] +
						    count[start + 5];
					tree_right = count[start + 6] +
						     count[start + 7] +
						     count[start + 8] +
						     count[start + 9] +
						     count[start + 10];
					break;
				case 3:
					tree_left = count[start + 2];
					tree_right = count[start + 3];
					break;
				case 2:
					tree_left = count[start + 2] +
						    count[start + 3];
					tree_right = count[start + 4] +
						     count[start + 5] +
						     count[start + 6] +
						     count[start + 7] +
						     count[start + 8] +
						     count[start + 9] +
						     count[start + 10];
					break;
				case 1:
					tree_left = count[start + 1];
					tree_right = count[start + 2] +
						     count[start + 3] +
						     count[start + 4] +
						     count[start + 5] +
						     count[start + 6] +
						     count[start + 7] +
						     count[start + 8] +
						     count[start + 9] +
						     count[start + 10];
					break;
				default:
					tree_left = count[start + 0];
					tree_right = count[start + 1] +
						     count[start + 2] +
						     count[start + 3] +
						     count[start + 4] +
						     count[start + 5] +
						     count[start + 6] +
						     count[start + 7] +
						     count[start + 8] +
						     count[start + 9] +
						     count[start + 10];
					break;
				}

				vp9_tree_merge_probs(prev_prob, cur_prob,
						     coef_node_start,
						     tree_left, tree_right,
						     tree_i, node);

				coef_node_start = coef_node_start + 1;
			}

			coef_node_start = mvd_i ? VP9_MV_CLASS0_1_START :
						  VP9_MV_CLASS0_0_START;
			coef_count_node_start =	mvd_i ?
						VP9_MV_CLASS0_1_COUNT_START :
						VP9_MV_CLASS0_0_COUNT_START;
			tree_i = 0;
			node = 0;
			tree_left = count[coef_count_node_start + 0];
			tree_right = count[coef_count_node_start + 1];

			vp9_tree_merge_probs(prev_prob, cur_prob,
					     coef_node_start,
					     tree_left, tree_right,
					     tree_i, node);
			coef_node_start = mvd_i ? VP9_MV_CLASS0_FP_1_START :
						  VP9_MV_CLASS0_FP_0_START;
			coef_count_node_start =	mvd_i ?
					VP9_MV_CLASS0_FP_1_COUNT_START :
					VP9_MV_CLASS0_FP_0_COUNT_START;

			for (tree_i = 0; tree_i < 3; tree_i++) {
				for (node = 0; node < 3; node++) {
					unsigned int start =
						coef_count_node_start;
					switch (node) {
					case 2:
						tree_left = count[start + 2];
						tree_right = count[start + 3];
						break;
					case 1:
						tree_left = count[start + 1];
						tree_right = count[start + 2] +
							     count[start + 3];
						break;
					default:
						tree_left = count[start + 0];
						tree_right = count[start + 1] +
							     count[start + 2] +
							     count[start + 3];
						break;
					}

					vp9_tree_merge_probs(prev_prob,
							     cur_prob,
							     coef_node_start,
							     tree_left,
							     tree_right,
							     tree_i, node);

					coef_node_start = coef_node_start + 1;
				}
				coef_count_node_start =
					coef_count_node_start + 4;
			}
		}
	}
}

static irqreturn_t codec_vp9_threaded_isr(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct codec_vp9 *vp9 = sess->priv;
	u32 dec_status = amvdec_read_dos(core, VP9_DEC_STATUS_REG);
	u32 prob_status = amvdec_read_dos(core, VP9_ADAPT_PROB_REG);
	int i;

	if (!vp9)
		return IRQ_HANDLED;

	mutex_lock(&vp9->lock);
	if (dec_status != VP9_HEAD_PARSER_DONE) {
		dev_err(core->dev_dec, "Unrecognized dec_status: %08X\n",
			dec_status);
		amvdec_abort(sess);
		goto unlock;
	}

	pr_debug("ISR: %08X;%08X\n", dec_status, prob_status);
	sess->keyframe_found = 1;

	if ((prob_status & 0xff) == 0xfd && vp9->cur_frame) {
		/* VP9_REQ_ADAPT_PROB */
		u8 *prev_prob_b = ((u8 *)vp9->workspace_vaddr +
					 PROB_OFFSET) +
					((prob_status >> 8) * 0x1000);
		u8 *cur_prob_b = ((u8 *)vp9->workspace_vaddr +
					 PROB_OFFSET) + 0x4000;
		u8 *count_b = (u8 *)vp9->workspace_vaddr +
				   COUNT_OFFSET;
		int last_frame_type = vp9->prev_frame ?
						vp9->prev_frame->type :
						KEY_FRAME;

		adapt_coef_probs(last_frame_type == KEY_FRAME,
				 vp9->cur_frame->type == KEY_FRAME ? 1 : 0,
				 prob_status >> 8,
				 (unsigned int *)prev_prob_b,
				 (unsigned int *)cur_prob_b,
				 (unsigned int *)count_b);

		memcpy(prev_prob_b, cur_prob_b, ADAPT_PROB_SIZE);
		amvdec_write_dos(core, VP9_ADAPT_PROB_REG, 0);
	}

	/* Invalidate first 3 refs */
	for (i = 0; i < REFS_PER_FRAME ; ++i)
		vp9->frame_refs[i] = NULL;

	vp9->prev_frame = vp9->cur_frame;
	codec_vp9_update_ref(vp9);

	codec_vp9_fetch_rpm(sess);
	if (codec_vp9_process_rpm(vp9)) {
		amvdec_src_change(sess, vp9->width, vp9->height, 16);

		/* No frame is actually processed */
		vp9->cur_frame = NULL;

		/* Show the remaining frame */
		codec_vp9_show_frame(sess);

		/* FIXME: Save refs for resized frame */
		if (vp9->frames_num)
			codec_vp9_save_refs(vp9);

		goto unlock;
	}

	codec_vp9_process_lf(vp9);
	codec_vp9_process_frame(sess);
	codec_vp9_show_frame(sess);

unlock:
	mutex_unlock(&vp9->lock);
	return IRQ_HANDLED;
}

static irqreturn_t codec_vp9_isr(struct amvdec_session *sess)
{
	return IRQ_WAKE_THREAD;
}

struct amvdec_codec_ops codec_vp9_ops = {
	.start = codec_vp9_start,
	.stop = codec_vp9_stop,
	.isr = codec_vp9_isr,
	.threaded_isr = codec_vp9_threaded_isr,
	.num_pending_bufs = codec_vp9_num_pending_bufs,
	.drain = codec_vp9_flush_output,
	.resume = codec_vp9_resume,
};
