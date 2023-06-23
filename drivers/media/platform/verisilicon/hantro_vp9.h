/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Hantro VP9 codec driver
 *
 * Copyright (C) 2021 Collabora Ltd.
 */

struct hantro_g2_mv_probs {
	u8 joint[3];
	u8 sign[2];
	u8 class0_bit[2][1];
	u8 fr[2][3];
	u8 class0_hp[2];
	u8 hp[2];
	u8 classes[2][10];
	u8 class0_fr[2][2][3];
	u8 bits[2][10];
};

struct hantro_g2_probs {
	u8 inter_mode[7][4];
	u8 is_inter[4];
	u8 uv_mode[10][8];
	u8 tx8[2][1];
	u8 tx16[2][2];
	u8 tx32[2][3];
	u8 y_mode_tail[4][1];
	u8 y_mode[4][8];
	u8 partition[2][16][4]; /* [keyframe][][], [inter][][] */
	u8 uv_mode_tail[10][1];
	u8 interp_filter[4][2];
	u8 comp_mode[5];
	u8 skip[3];

	u8 pad1[1];

	struct hantro_g2_mv_probs mv;

	u8 single_ref[5][2];
	u8 comp_ref[5];

	u8 pad2[17];

	u8 coef[4][2][2][6][6][4];
};

struct hantro_g2_all_probs {
	u8 kf_y_mode_prob[10][10][8];

	u8 kf_y_mode_prob_tail[10][10][1];
	u8 ref_pred_probs[3];
	u8 mb_segment_tree_probs[7];
	u8 segment_pred_probs[3];
	u8 ref_scores[4];
	u8 prob_comppred[2];

	u8 pad1[9];

	u8 kf_uv_mode_prob[10][8];
	u8 kf_uv_mode_prob_tail[10][1];

	u8 pad2[6];

	struct hantro_g2_probs probs;
};

struct mv_counts {
	u32 joints[4];
	u32 sign[2][2];
	u32 classes[2][11];
	u32 class0[2][2];
	u32 bits[2][10][2];
	u32 class0_fp[2][2][4];
	u32 fp[2][4];
	u32 class0_hp[2][2];
	u32 hp[2][2];
};

struct symbol_counts {
	u32 inter_mode_counts[7][3][2];
	u32 sb_ymode_counts[4][10];
	u32 uv_mode_counts[10][10];
	u32 partition_counts[16][4];
	u32 switchable_interp_counts[4][3];
	u32 intra_inter_count[4][2];
	u32 comp_inter_count[5][2];
	u32 single_ref_count[5][2][2];
	u32 comp_ref_count[5][2];
	u32 tx32x32_count[2][4];
	u32 tx16x16_count[2][3];
	u32 tx8x8_count[2][2];
	u32 mbskip_count[3][2];

	struct mv_counts mv_counts;

	u32 count_coeffs[2][2][6][6][4];
	u32 count_coeffs8x8[2][2][6][6][4];
	u32 count_coeffs16x16[2][2][6][6][4];
	u32 count_coeffs32x32[2][2][6][6][4];

	u32 count_eobs[4][2][2][6][6];
};
