/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ROCKCHIP_AV1_ENTROPYMODE_H_
#define _ROCKCHIP_AV1_ENTROPYMODE_H_

#include <linux/types.h>

struct hantro_ctx;

#define AV1_INTER_MODE_CONTEXTS 15
#define AV1_INTRA_MODES 13
#define AV1_REF_CONTEXTS 3
#define AV1_SWITCHABLE_FILTERS 3	/* number of switchable filters */
#define AV1_TX_SIZE_CONTEXTS 3
#define BLOCK_SIZE_GROUPS 4
#define BR_CDF_SIZE 4
#define BWD_REFS 3
#define CFL_ALLOWED_TYPES 2
#define CFL_ALPHA_CONTEXTS 6
#define CFL_ALPHABET_SIZE 16
#define CFL_JOINT_SIGNS 8
#define CDF_SIZE(x) ((x) - 1)
#define COMP_GROUP_IDX_CONTEXTS 7
#define COMP_INDEX_CONTEXTS 6
#define COMP_INTER_CONTEXTS 5
#define COMP_REF_TYPE_CONTEXTS 5
#define COMPOUND_TYPES 3
#define DC_SIGN_CONTEXTS 3
#define DELTA_LF_PROBS 3
#define DELTA_Q_PROBS 3
#define DIRECTIONAL_MODES 8
#define DRL_MODE_CONTEXTS 3
#define EOB_COEF_CONTEXTS 9
#define EXT_TX_SIZES 3
#define EXT_TX_TYPES 16
#define EXTTX_SIZES 4
#define FRAME_LF_COUNT 4
#define FWD_REFS 4
#define GLOBALMV_MODE_CONTEXTS 2
#define ICDF(x) (32768U - (x))
#define INTER_COMPOUND_MODES 8
#define INTERINTRA_MODES 4
#define INTRA_INTER_CONTEXTS 4
#define KF_MODE_CONTEXTS 5
#define LEVEL_CONTEXTS 21
#define MAX_ANGLE_DELTA 3
#define MAX_MB_SEGMENTS 8
#define MAX_SEGMENTS 8
#define MAX_TX_CATS 4
#define MAX_TX_DEPTH 2
#define MBSKIP_CONTEXTS 3
#define MOTION_MODES 3
#define MOTION_MODE_CONTEXTS 10
#define NEWMV_MODE_CONTEXTS 6
#define NUM_BASE_LEVELS 2
#define NUM_REF_FRAMES 8
#define PALETTE_BLOCK_SIZES 7
#define PALETTE_IDX_CONTEXTS 18
#define PALETTE_SIZES 7
#define PALETTE_UV_MODE_CONTEXTS 2
#define PALETTE_Y_MODE_CONTEXTS 3
#define PARTITION_PLOFFSET 4
#define NUM_PARTITION_CONTEXTS (4 * PARTITION_PLOFFSET)
#define PLANE_TYPES 2
#define PREDICTION_PROBS 3
#define REF_CONTEXTS 5
#define REFMV_MODE_CONTEXTS 9
#define SEG_TEMPORAL_PRED_CTXS 3
#define SIG_COEF_CONTEXTS 42
#define SIG_COEF_CONTEXTS_EOB 4
#define SINGLE_REFS 7
#define SKIP_CONTEXTS 3
#define SKIP_MODE_CONTEXTS 3
#define SPATIAL_PREDICTION_PROBS 3
#define SWITCHABLE_FILTER_CONTEXTS ((AV1_SWITCHABLE_FILTERS + 1) * 4)
#define TOKEN_CDF_Q_CTXS 4
#define TX_SIZES 5
#define TX_SIZE_CONTEXTS 2
#define TX_TYPES 4
#define TXB_SKIP_CONTEXTS 13
#define TXFM_PARTITION_CONTEXTS 22
#define UNI_COMP_REF_CONTEXTS 3
#define UNIDIR_COMP_REFS 4
#define UV_INTRA_MODES 14
#define VARTX_PART_CONTEXTS 22
#define ZEROMV_MODE_CONTEXTS 2

enum blocksizetype {
	BLOCK_SIZE_AB4X4,
	BLOCK_SIZE_SB4X8,
	BLOCK_SIZE_SB8X4,
	BLOCK_SIZE_SB8X8,
	BLOCK_SIZE_SB8X16,
	BLOCK_SIZE_SB16X8,
	BLOCK_SIZE_MB16X16,
	BLOCK_SIZE_SB16X32,
	BLOCK_SIZE_SB32X16,
	BLOCK_SIZE_SB32X32,
	BLOCK_SIZE_SB32X64,
	BLOCK_SIZE_SB64X32,
	BLOCK_SIZE_SB64X64,
	BLOCK_SIZE_SB64X128,
	BLOCK_SIZE_SB128X64,
	BLOCK_SIZE_SB128X128,
	BLOCK_SIZE_SB4X16,
	BLOCK_SIZE_SB16X4,
	BLOCK_SIZE_SB8X32,
	BLOCK_SIZE_SB32X8,
	BLOCK_SIZE_SB16X64,
	BLOCK_SIZE_SB64X16,
	BLOCK_SIZE_TYPES,
	BLOCK_SIZES_ALL = BLOCK_SIZE_TYPES
};

enum filterintramodetype {
	FILTER_DC_PRED,
	FILTER_V_PRED,
	FILTER_H_PRED,
	FILTER_D153_PRED,
	FILTER_PAETH_PRED,
	FILTER_INTRA_MODES,
	FILTER_INTRA_UNUSED = 7
};

enum frametype {
	KEY_FRAME = 0,
	INTER_FRAME = 1,
	NUM_FRAME_TYPES,
};

enum txsize {
	TX_4X4 = 0,
	TX_8X8 = 1,
	TX_16X16 = 2,
	TX_32X32 = 3,
	TX_SIZE_MAX_SB,
};

enum { SIMPLE_TRANSLATION, OBMC_CAUSAL, MOTION_MODE_COUNT };

enum mb_prediction_mode {
	DC_PRED,		/* average of above and left pixels */
	V_PRED,			/* vertical prediction */
	H_PRED,			/* horizontal prediction */
	D45_PRED,		/* Directional 45 deg prediction  [anti-clockwise from 0 deg hor] */
	D135_PRED,		/* Directional 135 deg prediction [anti-clockwise from 0 deg hor] */
	D117_PRED,		/* Directional 112 deg prediction [anti-clockwise from 0 deg hor] */
	D153_PRED,		/* Directional 157 deg prediction [anti-clockwise from 0 deg hor] */
	D27_PRED,		/* Directional 22 deg prediction  [anti-clockwise from 0 deg hor] */
	D63_PRED,		/* Directional 67 deg prediction  [anti-clockwise from 0 deg hor] */
	SMOOTH_PRED,
	TM_PRED_AV1 = SMOOTH_PRED,
	SMOOTH_V_PRED,		// Vertical interpolation
	SMOOTH_H_PRED,		// Horizontal interpolation
	TM_PRED,		/* Truemotion prediction */
	PAETH_PRED = TM_PRED,
	NEARESTMV,
	NEARMV,
	ZEROMV,
	NEWMV,
	NEAREST_NEARESTMV,
	NEAR_NEARMV,
	NEAREST_NEWMV,
	NEW_NEARESTMV,
	NEAR_NEWMV,
	NEW_NEARMV,
	ZERO_ZEROMV,
	NEW_NEWMV,
	SPLITMV,
	MB_MODE_COUNT
};

enum partitiontype {
	PARTITION_NONE,
	PARTITION_HORZ,
	PARTITION_VERT,
	PARTITION_SPLIT,
	PARTITION_TYPES
};

struct mvcdfs {
	u16 joint_cdf[3];
	u16 sign_cdf[2];
	u16 clsss_cdf[2][10];
	u16 clsss0_fp_cdf[2][2][3];
	u16 fp_cdf[2][3];
	u16 class0_hp_cdf[2];
	u16 hp_cdf[2];
	u16 class0_cdf[2];
	u16 bits_cdf[2][10];
};

struct av1cdfs {
	u16 partition_cdf[13][16];
	u16 kf_ymode_cdf[KF_MODE_CONTEXTS][KF_MODE_CONTEXTS][AV1_INTRA_MODES - 1];
	u16 segment_pred_cdf[PREDICTION_PROBS];
	u16 spatial_pred_seg_tree_cdf[SPATIAL_PREDICTION_PROBS][MAX_MB_SEGMENTS - 1];
	u16 mbskip_cdf[MBSKIP_CONTEXTS];
	u16 delta_q_cdf[DELTA_Q_PROBS];
	u16 delta_lf_multi_cdf[FRAME_LF_COUNT][DELTA_LF_PROBS];
	u16 delta_lf_cdf[DELTA_LF_PROBS];
	u16 skip_mode_cdf[SKIP_MODE_CONTEXTS];
	u16 vartx_part_cdf[VARTX_PART_CONTEXTS][1];
	u16 tx_size_cdf[MAX_TX_CATS][AV1_TX_SIZE_CONTEXTS][MAX_TX_DEPTH];
	u16 if_ymode_cdf[BLOCK_SIZE_GROUPS][AV1_INTRA_MODES - 1];
	u16 uv_mode_cdf[2][AV1_INTRA_MODES][AV1_INTRA_MODES - 1 + 1];
	u16 intra_inter_cdf[INTRA_INTER_CONTEXTS];
	u16 comp_inter_cdf[COMP_INTER_CONTEXTS];
	u16 single_ref_cdf[AV1_REF_CONTEXTS][SINGLE_REFS - 1];
	u16 comp_ref_type_cdf[COMP_REF_TYPE_CONTEXTS][1];
	u16 uni_comp_ref_cdf[UNI_COMP_REF_CONTEXTS][UNIDIR_COMP_REFS - 1][1];
	u16 comp_ref_cdf[AV1_REF_CONTEXTS][FWD_REFS - 1];
	u16 comp_bwdref_cdf[AV1_REF_CONTEXTS][BWD_REFS - 1];
	u16 newmv_cdf[NEWMV_MODE_CONTEXTS];
	u16 zeromv_cdf[ZEROMV_MODE_CONTEXTS];
	u16 refmv_cdf[REFMV_MODE_CONTEXTS];
	u16 drl_cdf[DRL_MODE_CONTEXTS];
	u16 interp_filter_cdf[SWITCHABLE_FILTER_CONTEXTS][AV1_SWITCHABLE_FILTERS - 1];
	struct mvcdfs mv_cdf;
	u16 obmc_cdf[BLOCK_SIZE_TYPES];
	u16 motion_mode_cdf[BLOCK_SIZE_TYPES][2];
	u16 inter_compound_mode_cdf[AV1_INTER_MODE_CONTEXTS][INTER_COMPOUND_MODES - 1];
	u16 compound_type_cdf[BLOCK_SIZE_TYPES][CDF_SIZE(COMPOUND_TYPES - 1)];
	u16 interintra_cdf[BLOCK_SIZE_GROUPS];
	u16 interintra_mode_cdf[BLOCK_SIZE_GROUPS][INTERINTRA_MODES - 1];
	u16 wedge_interintra_cdf[BLOCK_SIZE_TYPES];
	u16 wedge_idx_cdf[BLOCK_SIZE_TYPES][CDF_SIZE(16)];
	u16 palette_y_mode_cdf[PALETTE_BLOCK_SIZES][PALETTE_Y_MODE_CONTEXTS][1];
	u16 palette_uv_mode_cdf[PALETTE_UV_MODE_CONTEXTS][1];
	u16 palette_y_size_cdf[PALETTE_BLOCK_SIZES][PALETTE_SIZES - 1];
	u16 palette_uv_size_cdf[PALETTE_BLOCK_SIZES][PALETTE_SIZES - 1];
	u16 cfl_sign_cdf[CFL_JOINT_SIGNS - 1];
	u16 cfl_alpha_cdf[CFL_ALPHA_CONTEXTS][CFL_ALPHABET_SIZE - 1];
	u16 intrabc_cdf[1];
	u16 angle_delta_cdf[DIRECTIONAL_MODES][6];
	u16 filter_intra_mode_cdf[FILTER_INTRA_MODES - 1];
	u16 filter_intra_cdf[BLOCK_SIZES_ALL];
	u16 comp_group_idx_cdf[COMP_GROUP_IDX_CONTEXTS][CDF_SIZE(2)];
	u16 compound_idx_cdf[COMP_INDEX_CONTEXTS][CDF_SIZE(2)];
	u16 dummy0[14];
	// Palette index contexts; sizes 1/7, 2/6, 3/5 packed together
	u16 palette_y_color_index_cdf[PALETTE_IDX_CONTEXTS][8];
	u16 palette_uv_color_index_cdf[PALETTE_IDX_CONTEXTS][8];
	u16 tx_type_intra0_cdf[EXTTX_SIZES][AV1_INTRA_MODES][8];
	u16 tx_type_intra1_cdf[EXTTX_SIZES][AV1_INTRA_MODES][4];
	u16 tx_type_inter_cdf[2][EXTTX_SIZES][EXT_TX_TYPES];
	u16 txb_skip_cdf[TX_SIZES][TXB_SKIP_CONTEXTS][CDF_SIZE(2)];
	u16 eob_extra_cdf[TX_SIZES][PLANE_TYPES][EOB_COEF_CONTEXTS][CDF_SIZE(2)];
	u16 dummy1[5];
	u16 eob_flag_cdf16[PLANE_TYPES][2][4];
	u16 eob_flag_cdf32[PLANE_TYPES][2][8];
	u16 eob_flag_cdf64[PLANE_TYPES][2][8];
	u16 eob_flag_cdf128[PLANE_TYPES][2][8];
	u16 eob_flag_cdf256[PLANE_TYPES][2][8];
	u16 eob_flag_cdf512[PLANE_TYPES][2][16];
	u16 eob_flag_cdf1024[PLANE_TYPES][2][16];
	u16 coeff_base_eob_cdf[TX_SIZES][PLANE_TYPES][SIG_COEF_CONTEXTS_EOB][CDF_SIZE(3)];
	u16 coeff_base_cdf[TX_SIZES][PLANE_TYPES][SIG_COEF_CONTEXTS][CDF_SIZE(4) + 1];
	u16 dc_sign_cdf[PLANE_TYPES][DC_SIGN_CONTEXTS][CDF_SIZE(2)];
	u16 dummy2[2];
	u16 coeff_br_cdf[TX_SIZES][PLANE_TYPES][LEVEL_CONTEXTS][CDF_SIZE(BR_CDF_SIZE) + 1];
	u16 dummy3[16];
};

void rockchip_av1_store_cdfs(struct hantro_ctx *ctx,
			     u32 refresh_frame_flags);
void rockchip_av1_get_cdfs(struct hantro_ctx *ctx, u32 ref_idx);
void rockchip_av1_set_default_cdfs(struct av1cdfs *cdfs,
				   struct mvcdfs *cdfs_ndvc);
void rockchip_av1_default_coeff_probs(u32 base_qindex, void *ptr);

#endif /* _ROCKCHIP_AV1_ENTROPYMODE_H_ */
