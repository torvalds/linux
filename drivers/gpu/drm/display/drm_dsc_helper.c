// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corp
 *
 * Author:
 * Manasi Navare <manasi.d.navare@intel.com>
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/byteorder/generic.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_fixed.h>
#include <drm/drm_print.h>

/**
 * DOC: dsc helpers
 *
 * VESA specification for DP 1.4 adds a new feature called Display Stream
 * Compression (DSC) used to compress the pixel bits before sending it on
 * DP/eDP/MIPI DSI interface. DSC is required to be enabled so that the existing
 * display interfaces can support high resolutions at higher frames rates uisng
 * the maximum available link capacity of these interfaces.
 *
 * These functions contain some common logic and helpers to deal with VESA
 * Display Stream Compression standard required for DSC on Display Port/eDP or
 * MIPI display interfaces.
 */

/**
 * drm_dsc_dp_pps_header_init() - Initializes the PPS Header
 * for DisplayPort as per the DP 1.4 spec.
 * @pps_header: Secondary data packet header for DSC Picture
 *              Parameter Set as defined in &struct dp_sdp_header
 *
 * DP 1.4 spec defines the secondary data packet for sending the
 * picture parameter infoframes from the source to the sink.
 * This function populates the SDP header defined in
 * &struct dp_sdp_header.
 */
void drm_dsc_dp_pps_header_init(struct dp_sdp_header *pps_header)
{
	memset(pps_header, 0, sizeof(*pps_header));

	pps_header->HB1 = DP_SDP_PPS;
	pps_header->HB2 = DP_SDP_PPS_HEADER_PAYLOAD_BYTES_MINUS_1;
}
EXPORT_SYMBOL(drm_dsc_dp_pps_header_init);

/**
 * drm_dsc_dp_rc_buffer_size - get rc buffer size in bytes
 * @rc_buffer_block_size: block size code, according to DPCD offset 62h
 * @rc_buffer_size: number of blocks - 1, according to DPCD offset 63h
 *
 * return:
 * buffer size in bytes, or 0 on invalid input
 */
int drm_dsc_dp_rc_buffer_size(u8 rc_buffer_block_size, u8 rc_buffer_size)
{
	int size = 1024 * (rc_buffer_size + 1);

	switch (rc_buffer_block_size) {
	case DP_DSC_RC_BUF_BLK_SIZE_1:
		return 1 * size;
	case DP_DSC_RC_BUF_BLK_SIZE_4:
		return 4 * size;
	case DP_DSC_RC_BUF_BLK_SIZE_16:
		return 16 * size;
	case DP_DSC_RC_BUF_BLK_SIZE_64:
		return 64 * size;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(drm_dsc_dp_rc_buffer_size);

/**
 * drm_dsc_pps_payload_pack() - Populates the DSC PPS
 *
 * @pps_payload:
 * Bitwise struct for DSC Picture Parameter Set. This is defined
 * by &struct drm_dsc_picture_parameter_set
 * @dsc_cfg:
 * DSC Configuration data filled by driver as defined by
 * &struct drm_dsc_config
 *
 * DSC source device sends a picture parameter set (PPS) containing the
 * information required by the sink to decode the compressed frame. Driver
 * populates the DSC PPS struct using the DSC configuration parameters in
 * the order expected by the DSC Display Sink device. For the DSC, the sink
 * device expects the PPS payload in big endian format for fields
 * that span more than 1 byte.
 */
void drm_dsc_pps_payload_pack(struct drm_dsc_picture_parameter_set *pps_payload,
				const struct drm_dsc_config *dsc_cfg)
{
	int i;

	/* Protect against someone accidentally changing struct size */
	BUILD_BUG_ON(sizeof(*pps_payload) !=
		     DP_SDP_PPS_HEADER_PAYLOAD_BYTES_MINUS_1 + 1);

	memset(pps_payload, 0, sizeof(*pps_payload));

	/* PPS 0 */
	pps_payload->dsc_version =
		dsc_cfg->dsc_version_minor |
		dsc_cfg->dsc_version_major << DSC_PPS_VERSION_MAJOR_SHIFT;

	/* PPS 1, 2 is 0 */

	/* PPS 3 */
	pps_payload->pps_3 =
		dsc_cfg->line_buf_depth |
		dsc_cfg->bits_per_component << DSC_PPS_BPC_SHIFT;

	/* PPS 4 */
	pps_payload->pps_4 =
		((dsc_cfg->bits_per_pixel & DSC_PPS_BPP_HIGH_MASK) >>
		 DSC_PPS_MSB_SHIFT) |
		dsc_cfg->vbr_enable << DSC_PPS_VBR_EN_SHIFT |
		dsc_cfg->simple_422 << DSC_PPS_SIMPLE422_SHIFT |
		dsc_cfg->convert_rgb << DSC_PPS_CONVERT_RGB_SHIFT |
		dsc_cfg->block_pred_enable << DSC_PPS_BLOCK_PRED_EN_SHIFT;

	/* PPS 5 */
	pps_payload->bits_per_pixel_low =
		(dsc_cfg->bits_per_pixel & DSC_PPS_LSB_MASK);

	/*
	 * The DSC panel expects the PPS packet to have big endian format
	 * for data spanning 2 bytes. Use a macro cpu_to_be16() to convert
	 * to big endian format. If format is little endian, it will swap
	 * bytes to convert to Big endian else keep it unchanged.
	 */

	/* PPS 6, 7 */
	pps_payload->pic_height = cpu_to_be16(dsc_cfg->pic_height);

	/* PPS 8, 9 */
	pps_payload->pic_width = cpu_to_be16(dsc_cfg->pic_width);

	/* PPS 10, 11 */
	pps_payload->slice_height = cpu_to_be16(dsc_cfg->slice_height);

	/* PPS 12, 13 */
	pps_payload->slice_width = cpu_to_be16(dsc_cfg->slice_width);

	/* PPS 14, 15 */
	pps_payload->chunk_size = cpu_to_be16(dsc_cfg->slice_chunk_size);

	/* PPS 16 */
	pps_payload->initial_xmit_delay_high =
		((dsc_cfg->initial_xmit_delay &
		  DSC_PPS_INIT_XMIT_DELAY_HIGH_MASK) >>
		 DSC_PPS_MSB_SHIFT);

	/* PPS 17 */
	pps_payload->initial_xmit_delay_low =
		(dsc_cfg->initial_xmit_delay & DSC_PPS_LSB_MASK);

	/* PPS 18, 19 */
	pps_payload->initial_dec_delay =
		cpu_to_be16(dsc_cfg->initial_dec_delay);

	/* PPS 20 is 0 */

	/* PPS 21 */
	pps_payload->initial_scale_value =
		dsc_cfg->initial_scale_value;

	/* PPS 22, 23 */
	pps_payload->scale_increment_interval =
		cpu_to_be16(dsc_cfg->scale_increment_interval);

	/* PPS 24 */
	pps_payload->scale_decrement_interval_high =
		((dsc_cfg->scale_decrement_interval &
		  DSC_PPS_SCALE_DEC_INT_HIGH_MASK) >>
		 DSC_PPS_MSB_SHIFT);

	/* PPS 25 */
	pps_payload->scale_decrement_interval_low =
		(dsc_cfg->scale_decrement_interval & DSC_PPS_LSB_MASK);

	/* PPS 26[7:0], PPS 27[7:5] RESERVED */

	/* PPS 27 */
	pps_payload->first_line_bpg_offset =
		dsc_cfg->first_line_bpg_offset;

	/* PPS 28, 29 */
	pps_payload->nfl_bpg_offset =
		cpu_to_be16(dsc_cfg->nfl_bpg_offset);

	/* PPS 30, 31 */
	pps_payload->slice_bpg_offset =
		cpu_to_be16(dsc_cfg->slice_bpg_offset);

	/* PPS 32, 33 */
	pps_payload->initial_offset =
		cpu_to_be16(dsc_cfg->initial_offset);

	/* PPS 34, 35 */
	pps_payload->final_offset = cpu_to_be16(dsc_cfg->final_offset);

	/* PPS 36 */
	pps_payload->flatness_min_qp = dsc_cfg->flatness_min_qp;

	/* PPS 37 */
	pps_payload->flatness_max_qp = dsc_cfg->flatness_max_qp;

	/* PPS 38, 39 */
	pps_payload->rc_model_size = cpu_to_be16(dsc_cfg->rc_model_size);

	/* PPS 40 */
	pps_payload->rc_edge_factor = DSC_RC_EDGE_FACTOR_CONST;

	/* PPS 41 */
	pps_payload->rc_quant_incr_limit0 =
		dsc_cfg->rc_quant_incr_limit0;

	/* PPS 42 */
	pps_payload->rc_quant_incr_limit1 =
		dsc_cfg->rc_quant_incr_limit1;

	/* PPS 43 */
	pps_payload->rc_tgt_offset = DSC_RC_TGT_OFFSET_LO_CONST |
		DSC_RC_TGT_OFFSET_HI_CONST << DSC_PPS_RC_TGT_OFFSET_HI_SHIFT;

	/* PPS 44 - 57 */
	for (i = 0; i < DSC_NUM_BUF_RANGES - 1; i++)
		pps_payload->rc_buf_thresh[i] =
			dsc_cfg->rc_buf_thresh[i];

	/* PPS 58 - 87 */
	/*
	 * For DSC sink programming the RC Range parameter fields
	 * are as follows: Min_qp[15:11], max_qp[10:6], offset[5:0]
	 */
	for (i = 0; i < DSC_NUM_BUF_RANGES; i++) {
		pps_payload->rc_range_parameters[i] =
			cpu_to_be16((dsc_cfg->rc_range_params[i].range_min_qp <<
				     DSC_PPS_RC_RANGE_MINQP_SHIFT) |
				    (dsc_cfg->rc_range_params[i].range_max_qp <<
				     DSC_PPS_RC_RANGE_MAXQP_SHIFT) |
				    (dsc_cfg->rc_range_params[i].range_bpg_offset));
	}

	/* PPS 88 */
	pps_payload->native_422_420 = dsc_cfg->native_422 |
		dsc_cfg->native_420 << DSC_PPS_NATIVE_420_SHIFT;

	/* PPS 89 */
	pps_payload->second_line_bpg_offset =
		dsc_cfg->second_line_bpg_offset;

	/* PPS 90, 91 */
	pps_payload->nsl_bpg_offset =
		cpu_to_be16(dsc_cfg->nsl_bpg_offset);

	/* PPS 92, 93 */
	pps_payload->second_line_offset_adj =
		cpu_to_be16(dsc_cfg->second_line_offset_adj);

	/* PPS 94 - 127 are O */
}
EXPORT_SYMBOL(drm_dsc_pps_payload_pack);

/**
 * drm_dsc_set_const_params() - Set DSC parameters considered typically
 * constant across operation modes
 *
 * @vdsc_cfg:
 * DSC Configuration data partially filled by driver
 */
void drm_dsc_set_const_params(struct drm_dsc_config *vdsc_cfg)
{
	if (!vdsc_cfg->rc_model_size)
		vdsc_cfg->rc_model_size = DSC_RC_MODEL_SIZE_CONST;
	vdsc_cfg->rc_edge_factor = DSC_RC_EDGE_FACTOR_CONST;
	vdsc_cfg->rc_tgt_offset_high = DSC_RC_TGT_OFFSET_HI_CONST;
	vdsc_cfg->rc_tgt_offset_low = DSC_RC_TGT_OFFSET_LO_CONST;

	if (vdsc_cfg->bits_per_component <= 10)
		vdsc_cfg->mux_word_size = DSC_MUX_WORD_SIZE_8_10_BPC;
	else
		vdsc_cfg->mux_word_size = DSC_MUX_WORD_SIZE_12_BPC;
}
EXPORT_SYMBOL(drm_dsc_set_const_params);

/* From DSC_v1.11 spec, rc_parameter_Set syntax element typically constant */
static const u16 drm_dsc_rc_buf_thresh[] = {
	896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616,
	7744, 7872, 8000, 8064
};

/**
 * drm_dsc_set_rc_buf_thresh() - Set thresholds for the RC model
 * in accordance with the DSC 1.2 specification.
 *
 * @vdsc_cfg: DSC Configuration data partially filled by driver
 */
void drm_dsc_set_rc_buf_thresh(struct drm_dsc_config *vdsc_cfg)
{
	int i;

	BUILD_BUG_ON(ARRAY_SIZE(drm_dsc_rc_buf_thresh) !=
		     DSC_NUM_BUF_RANGES - 1);
	BUILD_BUG_ON(ARRAY_SIZE(drm_dsc_rc_buf_thresh) !=
		     ARRAY_SIZE(vdsc_cfg->rc_buf_thresh));

	for (i = 0; i < ARRAY_SIZE(drm_dsc_rc_buf_thresh); i++)
		vdsc_cfg->rc_buf_thresh[i] = drm_dsc_rc_buf_thresh[i] >> 6;

	/*
	 * For 6bpp, RC Buffer threshold 12 and 13 need a different value
	 * as per C Model
	 */
	if (vdsc_cfg->bits_per_pixel == 6 << 4) {
		vdsc_cfg->rc_buf_thresh[12] = 7936 >> 6;
		vdsc_cfg->rc_buf_thresh[13] = 8000 >> 6;
	}
}
EXPORT_SYMBOL(drm_dsc_set_rc_buf_thresh);

struct rc_parameters {
	u16 initial_xmit_delay;
	u8 first_line_bpg_offset;
	u16 initial_offset;
	u8 flatness_min_qp;
	u8 flatness_max_qp;
	u8 rc_quant_incr_limit0;
	u8 rc_quant_incr_limit1;
	struct drm_dsc_rc_range_parameters rc_range_params[DSC_NUM_BUF_RANGES];
};

struct rc_parameters_data {
	u8 bpp;
	u8 bpc;
	struct rc_parameters params;
};

#define DSC_BPP(bpp)	((bpp) << 4)

/*
 * Rate Control Related Parameter Recommended Values from DSC_v1.1 spec prior
 * to DSC 1.1 fractional bpp underflow SCR (DSC_v1.1_E1.pdf)
 *
 * Cross-checked against C Model releases: DSC_model_20161212 and 20210623
 */
static const struct rc_parameters_data rc_parameters_pre_scr[] = {
	{
		.bpp = DSC_BPP(6), .bpc = 8,
		{ 683, 15, 6144, 3, 13, 11, 11, {
			{ 0, 2, 0 }, { 1, 4, -2 }, { 3, 6, -2 }, { 4, 6, -4 },
			{ 5, 7, -6 }, { 5, 7, -6 }, { 6, 7, -6 }, { 6, 8, -8 },
			{ 7, 9, -8 }, { 8, 10, -10 }, { 9, 11, -10 }, { 10, 12, -12 },
			{ 10, 13, -12 }, { 12, 14, -12 }, { 15, 15, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 8,
		{ 512, 12, 6144, 3, 12, 11, 11, {
			{ 0, 4, 2 }, { 0, 4, 0 }, { 1, 5, 0 }, { 1, 6, -2 },
			{ 3, 7, -4 }, { 3, 7, -6 }, { 3, 7, -8 }, { 3, 8, -8 },
			{ 3, 9, -8 }, { 3, 10, -10 }, { 5, 11, -10 }, { 5, 12, -12 },
			{ 5, 13, -12 }, { 7, 13, -12 }, { 13, 15, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 10,
		{ 512, 12, 6144, 7, 16, 15, 15, {
			/*
			 * DSC model/pre-SCR-cfg has 8 for range_max_qp[0], however
			 * VESA DSC 1.1 Table E-5 sets it to 4.
			 */
			{ 0, 4, 2 }, { 4, 8, 0 }, { 5, 9, 0 }, { 5, 10, -2 },
			{ 7, 11, -4 }, { 7, 11, -6 }, { 7, 11, -8 }, { 7, 12, -8 },
			{ 7, 13, -8 }, { 7, 14, -10 }, { 9, 15, -10 }, { 9, 16, -12 },
			{ 9, 17, -12 }, { 11, 17, -12 }, { 17, 19, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 12,
		{ 512, 12, 6144, 11, 20, 19, 19, {
			{ 0, 12, 2 }, { 4, 12, 0 }, { 9, 13, 0 }, { 9, 14, -2 },
			{ 11, 15, -4 }, { 11, 15, -6 }, { 11, 15, -8 }, { 11, 16, -8 },
			{ 11, 17, -8 }, { 11, 18, -10 }, { 13, 19, -10 },
			{ 13, 20, -12 }, { 13, 21, -12 }, { 15, 21, -12 },
			{ 21, 23, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(10), .bpc = 8,
		{ 410, 12, 5632, 3, 12, 11, 11, {
			{ 0, 3, 2 }, { 0, 4, 0 }, { 1, 5, 0 }, { 2, 6, -2 },
			{ 3, 7, -4 }, { 3, 7, -6 }, { 3, 7, -8 }, { 3, 8, -8 },
			{ 3, 9, -8 }, { 3, 9, -10 }, { 5, 10, -10 }, { 5, 11, -10 },
			{ 5, 12, -12 }, { 7, 13, -12 }, { 13, 15, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(10), .bpc = 10,
		{ 410, 12, 5632, 7, 16, 15, 15, {
			{ 0, 7, 2 }, { 4, 8, 0 }, { 5, 9, 0 }, { 6, 10, -2 },
			{ 7, 11, -4 }, { 7, 11, -6 }, { 7, 11, -8 }, { 7, 12, -8 },
			{ 7, 13, -8 }, { 7, 13, -10 }, { 9, 14, -10 }, { 9, 15, -10 },
			{ 9, 16, -12 }, { 11, 17, -12 }, { 17, 19, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(10), .bpc = 12,
		{ 410, 12, 5632, 11, 20, 19, 19, {
			{ 0, 11, 2 }, { 4, 12, 0 }, { 9, 13, 0 }, { 10, 14, -2 },
			{ 11, 15, -4 }, { 11, 15, -6 }, { 11, 15, -8 }, { 11, 16, -8 },
			{ 11, 17, -8 }, { 11, 17, -10 }, { 13, 18, -10 },
			{ 13, 19, -10 }, { 13, 20, -12 }, { 15, 21, -12 },
			{ 21, 23, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(12), .bpc = 8,
		{ 341, 15, 2048, 3, 12, 11, 11, {
			{ 0, 2, 2 }, { 0, 4, 0 }, { 1, 5, 0 }, { 1, 6, -2 },
			{ 3, 7, -4 }, { 3, 7, -6 }, { 3, 7, -8 }, { 3, 8, -8 },
			{ 3, 9, -8 }, { 3, 10, -10 }, { 5, 11, -10 },
			{ 5, 12, -12 }, { 5, 13, -12 }, { 7, 13, -12 }, { 13, 15, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(12), .bpc = 10,
		{ 341, 15, 2048, 7, 16, 15, 15, {
			{ 0, 2, 2 }, { 2, 5, 0 }, { 3, 7, 0 }, { 4, 8, -2 },
			{ 6, 9, -4 }, { 7, 10, -6 }, { 7, 11, -8 }, { 7, 12, -8 },
			{ 7, 13, -8 }, { 7, 14, -10 }, { 9, 15, -10 }, { 9, 16, -12 },
			{ 9, 17, -12 }, { 11, 17, -12 }, { 17, 19, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(12), .bpc = 12,
		{ 341, 15, 2048, 11, 20, 19, 19, {
			{ 0, 6, 2 }, { 4, 9, 0 }, { 7, 11, 0 }, { 8, 12, -2 },
			{ 10, 13, -4 }, { 11, 14, -6 }, { 11, 15, -8 }, { 11, 16, -8 },
			{ 11, 17, -8 }, { 11, 18, -10 }, { 13, 19, -10 },
			{ 13, 20, -12 }, { 13, 21, -12 }, { 15, 21, -12 },
			{ 21, 23, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(15), .bpc = 8,
		{ 273, 15, 2048, 3, 12, 11, 11, {
			{ 0, 0, 10 }, { 0, 1, 8 }, { 0, 1, 6 }, { 0, 2, 4 },
			{ 1, 2, 2 }, { 1, 3, 0 }, { 1, 4, -2 }, { 2, 4, -4 },
			{ 3, 4, -6 }, { 3, 5, -8 }, { 4, 6, -10 }, { 5, 7, -10 },
			{ 5, 8, -12 }, { 7, 13, -12 }, { 13, 15, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(15), .bpc = 10,
		{ 273, 15, 2048, 7, 16, 15, 15, {
			{ 0, 2, 10 }, { 2, 5, 8 }, { 3, 5, 6 }, { 4, 6, 4 },
			{ 5, 6, 2 }, { 5, 7, 0 }, { 5, 8, -2 }, { 6, 8, -4 },
			{ 7, 8, -6 }, { 7, 9, -8 }, { 8, 10, -10 }, { 9, 11, -10 },
			{ 9, 12, -12 }, { 11, 17, -12 }, { 17, 19, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(15), .bpc = 12,
		{ 273, 15, 2048, 11, 20, 19, 19, {
			{ 0, 4, 10 }, { 2, 7, 8 }, { 4, 9, 6 }, { 6, 11, 4 },
			{ 9, 11, 2 }, { 9, 11, 0 }, { 9, 12, -2 }, { 10, 12, -4 },
			{ 11, 12, -6 }, { 11, 13, -8 }, { 12, 14, -10 },
			{ 13, 15, -10 }, { 13, 16, -12 }, { 15, 21, -12 },
			{ 21, 23, -12 }
			}
		}
	},
	{ /* sentinel */ }
};

/*
 * Selected Rate Control Related Parameter Recommended Values from DSC v1.2, v1.2a, v1.2b and
 * DSC_v1.1_E1 specs.
 *
 * Cross-checked against C Model releases: DSC_model_20161212 and 20210623
 */
static const struct rc_parameters_data rc_parameters_1_2_444[] = {
	{
		.bpp = DSC_BPP(6), .bpc = 8,
		{ 768, 15, 6144, 3, 13, 11, 11, {
			{ 0, 4, 0 }, { 1, 6, -2 }, { 3, 8, -2 }, { 4, 8, -4 },
			{ 5, 9, -6 }, { 5, 9, -6 }, { 6, 9, -6 }, { 6, 10, -8 },
			{ 7, 11, -8 }, { 8, 12, -10 }, { 9, 12, -10 }, { 10, 12, -12 },
			{ 10, 12, -12 }, { 11, 12, -12 }, { 13, 14, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(6), .bpc = 10,
		{ 768, 15, 6144, 7, 17, 15, 15, {
			{ 0, 8, 0 }, { 3, 10, -2 }, { 7, 12, -2 }, { 8, 12, -4 },
			{ 9, 13, -6 }, { 9, 13, -6 }, { 10, 13, -6 }, { 10, 14, -8 },
			{ 11, 15, -8 }, { 12, 16, -10 }, { 13, 16, -10 },
			{ 14, 16, -12 }, { 14, 16, -12 }, { 15, 16, -12 },
			{ 17, 18, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(6), .bpc = 12,
		{ 768, 15, 6144, 11, 21, 19, 19, {
			{ 0, 12, 0 }, { 5, 14, -2 }, { 11, 16, -2 }, { 12, 16, -4 },
			{ 13, 17, -6 }, { 13, 17, -6 }, { 14, 17, -6 }, { 14, 18, -8 },
			{ 15, 19, -8 }, { 16, 20, -10 }, { 17, 20, -10 },
			{ 18, 20, -12 }, { 18, 20, -12 }, { 19, 20, -12 },
			{ 21, 22, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(6), .bpc = 14,
		{ 768, 15, 6144, 15, 25, 23, 23, {
			{ 0, 16, 0 }, { 7, 18, -2 }, { 15, 20, -2 }, { 16, 20, -4 },
			{ 17, 21, -6 }, { 17, 21, -6 }, { 18, 21, -6 }, { 18, 22, -8 },
			{ 19, 23, -8 }, { 20, 24, -10 }, { 21, 24, -10 },
			{ 22, 24, -12 }, { 22, 24, -12 }, { 23, 24, -12 },
			{ 25, 26, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(6), .bpc = 16,
		{ 768, 15, 6144, 19, 29, 27, 27, {
			{ 0, 20, 0 }, { 9, 22, -2 }, { 19, 24, -2 }, { 20, 24, -4 },
			{ 21, 25, -6 }, { 21, 25, -6 }, { 22, 25, -6 }, { 22, 26, -8 },
			{ 23, 27, -8 }, { 24, 28, -10 }, { 25, 28, -10 },
			{ 26, 28, -12 }, { 26, 28, -12 }, { 27, 28, -12 },
			{ 29, 30, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 8,
		{ 512, 12, 6144, 3, 12, 11, 11, {
			{ 0, 4, 2 }, { 0, 4, 0 }, { 1, 5, 0 }, { 1, 6, -2 },
			{ 3, 7, -4 }, { 3, 7, -6 }, { 3, 7, -8 }, { 3, 8, -8 },
			{ 3, 9, -8 }, { 3, 10, -10 }, { 5, 10, -10 }, { 5, 11, -12 },
			{ 5, 11, -12 }, { 9, 12, -12 }, { 12, 13, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 10,
		{ 512, 12, 6144, 7, 16, 15, 15, {
			{ 0, 8, 2 }, { 4, 8, 0 }, { 5, 9, 0 }, { 5, 10, -2 },
			{ 7, 11, -4 }, { 7, 11, -6 }, { 7, 11, -8 }, { 7, 12, -8 },
			{ 7, 13, -8 }, { 7, 14, -10 }, { 9, 14, -10 }, { 9, 15, -12 },
			{ 9, 15, -12 }, { 13, 16, -12 }, { 16, 17, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 12,
		{ 512, 12, 6144, 11, 20, 19, 19, {
			{ 0, 12, 2 }, { 4, 12, 0 }, { 9, 13, 0 }, { 9, 14, -2 },
			{ 11, 15, -4 }, { 11, 15, -6 }, { 11, 15, -8 }, { 11, 16, -8 },
			{ 11, 17, -8 }, { 11, 18, -10 }, { 13, 18, -10 },
			{ 13, 19, -12 }, { 13, 19, -12 }, { 17, 20, -12 },
			{ 20, 21, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 14,
		{ 512, 12, 6144, 15, 24, 23, 23, {
			{ 0, 12, 2 }, { 5, 13, 0 }, { 11, 15, 0 }, { 12, 17, -2 },
			{ 15, 19, -4 }, { 15, 19, -6 }, { 15, 19, -8 }, { 15, 20, -8 },
			{ 15, 21, -8 }, { 15, 22, -10 }, { 17, 22, -10 },
			{ 17, 23, -12 }, { 17, 23, -12 }, { 21, 24, -12 },
			{ 24, 25, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 16,
		{ 512, 12, 6144, 19, 28, 27, 27, {
			{ 0, 12, 2 }, { 6, 14, 0 }, { 13, 17, 0 }, { 15, 20, -2 },
			{ 19, 23, -4 }, { 19, 23, -6 }, { 19, 23, -8 }, { 19, 24, -8 },
			{ 19, 25, -8 }, { 19, 26, -10 }, { 21, 26, -10 },
			{ 21, 27, -12 }, { 21, 27, -12 }, { 25, 28, -12 },
			{ 28, 29, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(10), .bpc = 8,
		{ 410, 15, 5632, 3, 12, 11, 11, {
			{ 0, 3, 2 }, { 0, 4, 0 }, { 1, 5, 0 }, { 2, 6, -2 },
			{ 3, 7, -4 }, { 3, 7, -6 }, { 3, 7, -8 }, { 3, 8, -8 },
			{ 3, 9, -8 }, { 3, 9, -10 }, { 5, 10, -10 }, { 5, 10, -10 },
			{ 5, 11, -12 }, { 7, 11, -12 }, { 11, 12, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(10), .bpc = 10,
		{ 410, 15, 5632, 7, 16, 15, 15, {
			{ 0, 7, 2 }, { 4, 8, 0 }, { 5, 9, 0 }, { 6, 10, -2 },
			{ 7, 11, -4 }, { 7, 11, -6 }, { 7, 11, -8 }, { 7, 12, -8 },
			{ 7, 13, -8 }, { 7, 13, -10 }, { 9, 14, -10 }, { 9, 14, -10 },
			{ 9, 15, -12 }, { 11, 15, -12 }, { 15, 16, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(10), .bpc = 12,
		{ 410, 15, 5632, 11, 20, 19, 19, {
			{ 0, 11, 2 }, { 4, 12, 0 }, { 9, 13, 0 }, { 10, 14, -2 },
			{ 11, 15, -4 }, { 11, 15, -6 }, { 11, 15, -8 }, { 11, 16, -8 },
			{ 11, 17, -8 }, { 11, 17, -10 }, { 13, 18, -10 },
			{ 13, 18, -10 }, { 13, 19, -12 }, { 15, 19, -12 },
			{ 19, 20, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(10), .bpc = 14,
		{ 410, 15, 5632, 15, 24, 23, 23, {
			{ 0, 11, 2 }, { 5, 13, 0 }, { 11, 15, 0 }, { 13, 18, -2 },
			{ 15, 19, -4 }, { 15, 19, -6 }, { 15, 19, -8 }, { 15, 20, -8 },
			{ 15, 21, -8 }, { 15, 21, -10 }, { 17, 22, -10 },
			{ 17, 22, -10 }, { 17, 23, -12 }, { 19, 23, -12 },
			{ 23, 24, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(10), .bpc = 16,
		{ 410, 15, 5632, 19, 28, 27, 27, {
			{ 0, 11, 2 }, { 6, 14, 0 }, { 13, 17, 0 }, { 16, 20, -2 },
			{ 19, 23, -4 }, { 19, 23, -6 }, { 19, 23, -8 }, { 19, 24, -8 },
			{ 19, 25, -8 }, { 19, 25, -10 }, { 21, 26, -10 },
			{ 21, 26, -10 }, { 21, 27, -12 }, { 23, 27, -12 },
			{ 27, 28, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(12), .bpc = 8,
		{ 341, 15, 2048, 3, 12, 11, 11, {
			{ 0, 2, 2 }, { 0, 4, 0 }, { 1, 5, 0 }, { 1, 6, -2 },
			{ 3, 7, -4 }, { 3, 7, -6 }, { 3, 7, -8 }, { 3, 8, -8 },
			{ 3, 8, -8 }, { 3, 9, -10 }, { 5, 9, -10 }, { 5, 9, -12 },
			{ 5, 9, -12 }, { 7, 10, -12 }, { 10, 11, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(12), .bpc = 10,
		{ 341, 15, 2048, 7, 16, 15, 15, {
			{ 0, 2, 2 }, { 2, 5, 0 }, { 3, 7, 0 }, { 4, 8, -2 },
			{ 6, 9, -4 }, { 7, 10, -6 }, { 7, 11, -8 }, { 7, 12, -8 },
			{ 7, 12, -8 }, { 7, 13, -10 }, { 9, 13, -10 }, { 9, 13, -12 },
			{ 9, 13, -12 }, { 11, 14, -12 }, { 14, 15, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(12), .bpc = 12,
		{ 341, 15, 2048, 11, 20, 19, 19, {
			{ 0, 6, 2 }, { 4, 9, 0 }, { 7, 11, 0 }, { 8, 12, -2 },
			{ 10, 13, -4 }, { 11, 14, -6 }, { 11, 15, -8 }, { 11, 16, -8 },
			{ 11, 16, -8 }, { 11, 17, -10 }, { 13, 17, -10 },
			{ 13, 17, -12 }, { 13, 17, -12 }, { 15, 18, -12 },
			{ 18, 19, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(12), .bpc = 14,
		{ 341, 15, 2048, 15, 24, 23, 23, {
			{ 0, 6, 2 }, { 7, 10, 0 }, { 9, 13, 0 }, { 11, 16, -2 },
			{ 14, 17, -4 }, { 15, 18, -6 }, { 15, 19, -8 }, { 15, 20, -8 },
			{ 15, 20, -8 }, { 15, 21, -10 }, { 17, 21, -10 },
			{ 17, 21, -12 }, { 17, 21, -12 }, { 19, 22, -12 },
			{ 22, 23, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(12), .bpc = 16,
		{ 341, 15, 2048, 19, 28, 27, 27, {
			{ 0, 6, 2 }, { 6, 11, 0 }, { 11, 15, 0 }, { 14, 18, -2 },
			{ 18, 21, -4 }, { 19, 22, -6 }, { 19, 23, -8 }, { 19, 24, -8 },
			{ 19, 24, -8 }, { 19, 25, -10 }, { 21, 25, -10 },
			{ 21, 25, -12 }, { 21, 25, -12 }, { 23, 26, -12 },
			{ 26, 27, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(15), .bpc = 8,
		{ 273, 15, 2048, 3, 12, 11, 11, {
			{ 0, 0, 10 }, { 0, 1, 8 }, { 0, 1, 6 }, { 0, 2, 4 },
			{ 1, 2, 2 }, { 1, 3, 0 }, { 1, 3, -2 }, { 2, 4, -4 },
			{ 2, 5, -6 }, { 3, 5, -8 }, { 4, 6, -10 }, { 4, 7, -10 },
			{ 5, 7, -12 }, { 7, 8, -12 }, { 8, 9, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(15), .bpc = 10,
		{ 273, 15, 2048, 7, 16, 15, 15, {
			{ 0, 2, 10 }, { 2, 5, 8 }, { 3, 5, 6 }, { 4, 6, 4 },
			{ 5, 6, 2 }, { 5, 7, 0 }, { 5, 7, -2 }, { 6, 8, -4 },
			{ 6, 9, -6 }, { 7, 9, -8 }, { 8, 10, -10 }, { 8, 11, -10 },
			{ 9, 11, -12 }, { 11, 12, -12 }, { 12, 13, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(15), .bpc = 12,
		{ 273, 15, 2048, 11, 20, 19, 19, {
			{ 0, 4, 10 }, { 2, 7, 8 }, { 4, 9, 6 }, { 6, 11, 4 },
			{ 9, 11, 2 }, { 9, 11, 0 }, { 9, 12, -2 }, { 10, 12, -4 },
			{ 11, 13, -6 }, { 11, 13, -8 }, { 12, 14, -10 },
			{ 13, 15, -10 }, { 13, 15, -12 }, { 15, 16, -12 },
			{ 16, 17, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(15), .bpc = 14,
		{ 273, 15, 2048, 15, 24, 23, 23, {
			{ 0, 4, 10 }, { 3, 8, 8 }, { 6, 11, 6 }, { 9, 14, 4 },
			{ 13, 15, 2 }, { 13, 15, 0 }, { 13, 16, -2 }, { 14, 16, -4 },
			{ 15, 17, -6 }, { 15, 17, -8 }, { 16, 18, -10 },
			{ 17, 19, -10 }, { 17, 19, -12 }, { 19, 20, -12 },
			{ 20, 21, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(15), .bpc = 16,
		{ 273, 15, 2048, 19, 28, 27, 27, {
			{ 0, 4, 10 }, { 4, 9, 8 }, { 8, 13, 6 }, { 12, 17, 4 },
			{ 17, 19, 2 }, { 17, 20, 0 }, { 17, 20, -2 }, { 18, 20, -4 },
			{ 19, 21, -6 }, { 19, 21, -8 }, { 20, 22, -10 },
			{ 21, 23, -10 }, { 21, 23, -12 }, { 23, 24, -12 },
			{ 24, 25, -12 }
			}
		}
	},
	{ /* sentinel */ }
};

/*
 * Selected Rate Control Related Parameter Recommended Values for 4:2:2 from
 * DSC v1.2, v1.2a, v1.2b
 *
 * Cross-checked against C Model releases: DSC_model_20161212 and 20210623
 */
static const struct rc_parameters_data rc_parameters_1_2_422[] = {
	{
		.bpp = DSC_BPP(6), .bpc = 8,
		{ 512, 15, 6144, 3, 12, 11, 11, {
			{ 0, 4, 2 }, { 0, 4, 0 }, { 1, 5, 0 }, { 1, 6, -2 },
			{ 3, 7, -4 }, { 3, 7, -6 }, { 3, 7, -8 }, { 3, 8, -8 },
			{ 3, 9, -8 }, { 3, 10, -10 }, { 5, 10, -10 }, { 5, 11, -12 },
			{ 5, 11, -12 }, { 9, 12, -12 }, { 12, 13, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(6), .bpc = 10,
		{ 512, 15, 6144, 7, 16, 15, 15, {
			{ 0, 8, 2 }, { 4, 8, 0 }, { 5, 9, 0 }, { 5, 10, -2 },
			{ 7, 11, -4 }, { 7, 11, -6 }, { 7, 11, -8 }, { 7, 12, -8 },
			{ 7, 13, -8 }, { 7, 14, -10 }, { 9, 14, -10 }, { 9, 15, -12 },
			{ 9, 15, -12 }, { 13, 16, -12 }, { 16, 17, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(6), .bpc = 12,
		{ 512, 15, 6144, 11, 20, 19, 19, {
			{ 0, 12, 2 }, { 4, 12, 0 }, { 9, 13, 0 }, { 9, 14, -2 },
			{ 11, 15, -4 }, { 11, 15, -6 }, { 11, 15, -8 }, { 11, 16, -8 },
			{ 11, 17, -8 }, { 11, 18, -10 }, { 13, 18, -10 },
			{ 13, 19, -12 }, { 13, 19, -12 }, { 17, 20, -12 },
			{ 20, 21, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(6), .bpc = 14,
		{ 512, 15, 6144, 15, 24, 23, 23, {
			{ 0, 12, 2 }, { 5, 13, 0 }, { 11, 15, 0 }, { 12, 17, -2 },
			{ 15, 19, -4 }, { 15, 19, -6 }, { 15, 19, -8 }, { 15, 20, -8 },
			{ 15, 21, -8 }, { 15, 22, -10 }, { 17, 22, -10 },
			{ 17, 23, -12 }, { 17, 23, -12 }, { 21, 24, -12 },
			{ 24, 25, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(6), .bpc = 16,
		{ 512, 15, 6144, 19, 28, 27, 27, {
			{ 0, 12, 2 }, { 6, 14, 0 }, { 13, 17, 0 }, { 15, 20, -2 },
			{ 19, 23, -4 }, { 19, 23, -6 }, { 19, 23, -8 }, { 19, 24, -8 },
			{ 19, 25, -8 }, { 19, 26, -10 }, { 21, 26, -10 },
			{ 21, 27, -12 }, { 21, 27, -12 }, { 25, 28, -12 },
			{ 28, 29, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(7), .bpc = 8,
		{ 410, 15, 5632, 3, 12, 11, 11, {
			{ 0, 3, 2 }, { 0, 4, 0 }, { 1, 5, 0 }, { 2, 6, -2 },
			{ 3, 7, -4 }, { 3, 7, -6 }, { 3, 7, -8 }, { 3, 8, -8 },
			{ 3, 9, -8 }, { 3, 9, -10 }, { 5, 10, -10 }, { 5, 10, -10 },
			{ 5, 11, -12 }, { 7, 11, -12 }, { 11, 12, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(7), .bpc = 10,
		{ 410, 15, 5632, 7, 16, 15, 15, {
			{ 0, 7, 2 }, { 4, 8, 0 }, { 5, 9, 0 }, { 6, 10, -2 },
			{ 7, 11, -4 }, { 7, 11, -6 }, { 7, 11, -8 }, { 7, 12, -8 },
			{ 7, 13, -8 }, { 7, 13, -10 }, { 9, 14, -10 }, { 9, 14, -10 },
			{ 9, 15, -12 }, { 11, 15, -12 }, { 15, 16, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(7), .bpc = 12,
		{ 410, 15, 5632, 11, 20, 19, 19, {
			{ 0, 11, 2 }, { 4, 12, 0 }, { 9, 13, 0 }, { 10, 14, -2 },
			{ 11, 15, -4 }, { 11, 15, -6 }, { 11, 15, -8 }, { 11, 16, -8 },
			{ 11, 17, -8 }, { 11, 17, -10 }, { 13, 18, -10 },
			{ 13, 18, -10 }, { 13, 19, -12 }, { 15, 19, -12 },
			{ 19, 20, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(7), .bpc = 14,
		{ 410, 15, 5632, 15, 24, 23, 23, {
			{ 0, 11, 2 }, { 5, 13, 0 }, { 11, 15, 0 }, { 13, 18, -2 },
			{ 15, 19, -4 }, { 15, 19, -6 }, { 15, 19, -8 }, { 15, 20, -8 },
			{ 15, 21, -8 }, { 15, 21, -10 }, { 17, 22, -10 },
			{ 17, 22, -10 }, { 17, 23, -12 }, { 19, 23, -12 },
			{ 23, 24, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(7), .bpc = 16,
		{ 410, 15, 5632, 19, 28, 27, 27, {
			{ 0, 11, 2 }, { 6, 14, 0 }, { 13, 17, 0 }, { 16, 20, -2 },
			{ 19, 23, -4 }, { 19, 23, -6 }, { 19, 23, -8 }, { 19, 24, -8 },
			{ 19, 25, -8 }, { 19, 25, -10 }, { 21, 26, -10 },
			{ 21, 26, -10 }, { 21, 27, -12 }, { 23, 27, -12 },
			{ 27, 28, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 8,
		{ 341, 15, 2048, 3, 12, 11, 11, {
			{ 0, 2, 2 }, { 0, 4, 0 }, { 1, 5, 0 }, { 1, 6, -2 },
			{ 3, 7, -4 }, { 3, 7, -6 }, { 3, 7, -8 }, { 3, 8, -8 },
			{ 3, 8, -8 }, { 3, 9, -10 }, { 5, 9, -10 }, { 5, 9, -12 },
			{ 5, 9, -12 }, { 7, 10, -12 }, { 10, 11, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 10,
		{ 341, 15, 2048, 7, 16, 15, 15, {
			{ 0, 2, 2 }, { 2, 5, 0 }, { 3, 7, 0 }, { 4, 8, -2 },
			{ 6, 9, -4 }, { 7, 10, -6 }, { 7, 11, -8 }, { 7, 12, -8 },
			{ 7, 12, -8 }, { 7, 13, -10 }, { 9, 13, -10 }, { 9, 13, -12 },
			{ 9, 13, -12 }, { 11, 14, -12 }, { 14, 15, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 12,
		{ 341, 15, 2048, 11, 20, 19, 19, {
			{ 0, 6, 2 }, { 4, 9, 0 }, { 7, 11, 0 }, { 8, 12, -2 },
			{ 10, 13, -4 }, { 11, 14, -6 }, { 11, 15, -8 }, { 11, 16, -8 },
			{ 11, 16, -8 }, { 11, 17, -10 }, { 13, 17, -10 },
			{ 13, 17, -12 }, { 13, 17, -12 }, { 15, 18, -12 },
			{ 18, 19, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 14,
		{ 341, 15, 2048, 15, 24, 23, 23, {
			{ 0, 6, 2 }, { 7, 10, 0 }, { 9, 13, 0 }, { 11, 16, -2 },
			{ 14, 17, -4 }, { 15, 18, -6 }, { 15, 19, -8 }, { 15, 20, -8 },
			{ 15, 20, -8 }, { 15, 21, -10 }, { 17, 21, -10 },
			{ 17, 21, -12 }, { 17, 21, -12 }, { 19, 22, -12 },
			{ 22, 23, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 16,
		{ 341, 15, 2048, 19, 28, 27, 27, {
			{ 0, 6, 2 }, { 6, 11, 0 }, { 11, 15, 0 }, { 14, 18, -2 },
			{ 18, 21, -4 }, { 19, 22, -6 }, { 19, 23, -8 }, { 19, 24, -8 },
			{ 19, 24, -8 }, { 19, 25, -10 }, { 21, 25, -10 },
			{ 21, 25, -12 }, { 21, 25, -12 }, { 23, 26, -12 },
			{ 26, 27, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(10), .bpc = 8,
		{ 273, 15, 2048, 3, 12, 11, 11, {
			{ 0, 0, 10 }, { 0, 1, 8 }, { 0, 1, 6 }, { 0, 2, 4 },
			{ 1, 2, 2 }, { 1, 3, 0 }, { 1, 3, -2 }, { 2, 4, -4 },
			{ 2, 5, -6 }, { 3, 5, -8 }, { 4, 6, -10 }, { 4, 7, -10 },
			{ 5, 7, -12 }, { 7, 8, -12 }, { 8, 9, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(10), .bpc = 10,
		{ 273, 15, 2048, 7, 16, 15, 15, {
			{ 0, 2, 10 }, { 2, 5, 8 }, { 3, 5, 6 }, { 4, 6, 4 },
			{ 5, 6, 2 }, { 5, 7, 0 }, { 5, 7, -2 }, { 6, 8, -4 },
			{ 6, 9, -6 }, { 7, 9, -8 }, { 8, 10, -10 }, { 8, 11, -10 },
			{ 9, 11, -12 }, { 11, 12, -12 }, { 12, 13, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(10), .bpc = 12,
		{ 273, 15, 2048, 11, 20, 19, 19, {
			{ 0, 4, 10 }, { 2, 7, 8 }, { 4, 9, 6 }, { 6, 11, 4 },
			{ 9, 11, 2 }, { 9, 11, 0 }, { 9, 12, -2 }, { 10, 12, -4 },
			{ 11, 13, -6 }, { 11, 13, -8 }, { 12, 14, -10 },
			{ 13, 15, -10 }, { 13, 15, -12 }, { 15, 16, -12 },
			{ 16, 17, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(10), .bpc = 14,
		{ 273, 15, 2048, 15, 24, 23, 23, {
			{ 0, 4, 10 }, { 3, 8, 8 }, { 6, 11, 6 }, { 9, 14, 4 },
			{ 13, 15, 2 }, { 13, 15, 0 }, { 13, 16, -2 }, { 14, 16, -4 },
			{ 15, 17, -6 }, { 15, 17, -8 }, { 16, 18, -10 },
			{ 17, 19, -10 }, { 17, 19, -12 }, { 19, 20, -12 },
			{ 20, 21, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(10), .bpc = 16,
		{ 273, 15, 2048, 19, 28, 27, 27, {
			{ 0, 4, 10 }, { 4, 9, 8 }, { 8, 13, 6 }, { 12, 17, 4 },
			{ 17, 19, 2 }, { 17, 20, 0 }, { 17, 20, -2 }, { 18, 20, -4 },
			{ 19, 21, -6 }, { 19, 21, -8 }, { 20, 22, -10 },
			{ 21, 23, -10 }, { 21, 23, -12 }, { 23, 24, -12 },
			{ 24, 25, -12 }
			}
		}
	},
	{ /* sentinel */ }
};

/*
 * Selected Rate Control Related Parameter Recommended Values for 4:2:2 from
 * DSC v1.2, v1.2a, v1.2b
 *
 * Cross-checked against C Model releases: DSC_model_20161212 and 20210623
 */
static const struct rc_parameters_data rc_parameters_1_2_420[] = {
	{
		.bpp = DSC_BPP(4), .bpc = 8,
		{ 512, 12, 6144, 3, 12, 11, 11, {
			{ 0, 4, 2 }, { 0, 4, 0 }, { 1, 5, 0 }, { 1, 6, -2 },
			{ 3, 7, -4 }, { 3, 7, -6 }, { 3, 7, -8 }, { 3, 8, -8 },
			{ 3, 9, -8 }, { 3, 10, -10 }, { 5, 10, -10 }, { 5, 11, -12 },
			{ 5, 11, -12 }, { 9, 12, -12 }, { 12, 13, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(4), .bpc = 10,
		{ 512, 12, 6144, 7, 16, 15, 15, {
			{ 0, 8, 2 }, { 4, 8, 0 }, { 5, 9, 0 }, { 5, 10, -2 },
			{ 7, 11, -4 }, { 7, 11, -6 }, { 7, 11, -8 }, { 7, 12, -8 },
			{ 7, 13, -8 }, { 7, 14, -10 }, { 9, 14, -10 }, { 9, 15, -12 },
			{ 9, 15, -12 }, { 13, 16, -12 }, { 16, 17, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(4), .bpc = 12,
		{ 512, 12, 6144, 11, 20, 19, 19, {
			{ 0, 12, 2 }, { 4, 12, 0 }, { 9, 13, 0 }, { 9, 14, -2 },
			{ 11, 15, -4 }, { 11, 15, -6 }, { 11, 15, -8 }, { 11, 16, -8 },
			{ 11, 17, -8 }, { 11, 18, -10 }, { 13, 18, -10 },
			{ 13, 19, -12 }, { 13, 19, -12 }, { 17, 20, -12 },
			{ 20, 21, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(4), .bpc = 14,
		{ 512, 12, 6144, 15, 24, 23, 23, {
			{ 0, 12, 2 }, { 5, 13, 0 }, { 11, 15, 0 }, { 12, 17, -2 },
			{ 15, 19, -4 }, { 15, 19, -6 }, { 15, 19, -8 }, { 15, 20, -8 },
			{ 15, 21, -8 }, { 15, 22, -10 }, { 17, 22, -10 },
			{ 17, 23, -12 }, { 17, 23, -12 }, { 21, 24, -12 },
			{ 24, 25, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(4), .bpc = 16,
		{ 512, 12, 6144, 19, 28, 27, 27, {
			{ 0, 12, 2 }, { 6, 14, 0 }, { 13, 17, 0 }, { 15, 20, -2 },
			{ 19, 23, -4 }, { 19, 23, -6 }, { 19, 23, -8 }, { 19, 24, -8 },
			{ 19, 25, -8 }, { 19, 26, -10 }, { 21, 26, -10 },
			{ 21, 27, -12 }, { 21, 27, -12 }, { 25, 28, -12 },
			{ 28, 29, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(5), .bpc = 8,
		{ 410, 15, 5632, 3, 12, 11, 11, {
			{ 0, 3, 2 }, { 0, 4, 0 }, { 1, 5, 0 }, { 2, 6, -2 },
			{ 3, 7, -4 }, { 3, 7, -6 }, { 3, 7, -8 }, { 3, 8, -8 },
			{ 3, 9, -8 }, { 3, 9, -10 }, { 5, 10, -10 }, { 5, 10, -10 },
			{ 5, 11, -12 }, { 7, 11, -12 }, { 11, 12, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(5), .bpc = 10,
		{ 410, 15, 5632, 7, 16, 15, 15, {
			{ 0, 7, 2 }, { 4, 8, 0 }, { 5, 9, 0 }, { 6, 10, -2 },
			{ 7, 11, -4 }, { 7, 11, -6 }, { 7, 11, -8 }, { 7, 12, -8 },
			{ 7, 13, -8 }, { 7, 13, -10 }, { 9, 14, -10 }, { 9, 14, -10 },
			{ 9, 15, -12 }, { 11, 15, -12 }, { 15, 16, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(5), .bpc = 12,
		{ 410, 15, 5632, 11, 20, 19, 19, {
			{ 0, 11, 2 }, { 4, 12, 0 }, { 9, 13, 0 }, { 10, 14, -2 },
			{ 11, 15, -4 }, { 11, 15, -6 }, { 11, 15, -8 }, { 11, 16, -8 },
			{ 11, 17, -8 }, { 11, 17, -10 }, { 13, 18, -10 },
			{ 13, 18, -10 }, { 13, 19, -12 }, { 15, 19, -12 },
			{ 19, 20, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(5), .bpc = 14,
		{ 410, 15, 5632, 15, 24, 23, 23, {
			{ 0, 11, 2 }, { 5, 13, 0 }, { 11, 15, 0 }, { 13, 18, -2 },
			{ 15, 19, -4 }, { 15, 19, -6 }, { 15, 19, -8 }, { 15, 20, -8 },
			{ 15, 21, -8 }, { 15, 21, -10 }, { 17, 22, -10 },
			{ 17, 22, -10 }, { 17, 23, -12 }, { 19, 23, -12 },
			{ 23, 24, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(5), .bpc = 16,
		{ 410, 15, 5632, 19, 28, 27, 27, {
			{ 0, 11, 2 }, { 6, 14, 0 }, { 13, 17, 0 }, { 16, 20, -2 },
			{ 19, 23, -4 }, { 19, 23, -6 }, { 19, 23, -8 }, { 19, 24, -8 },
			{ 19, 25, -8 }, { 19, 25, -10 }, { 21, 26, -10 },
			{ 21, 26, -10 }, { 21, 27, -12 }, { 23, 27, -12 },
			{ 27, 28, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(6), .bpc = 8,
		{ 341, 15, 2048, 3, 12, 11, 11, {
			{ 0, 2, 2 }, { 0, 4, 0 }, { 1, 5, 0 }, { 1, 6, -2 },
			{ 3, 7, -4 }, { 3, 7, -6 }, { 3, 7, -8 }, { 3, 8, -8 },
			{ 3, 8, -8 }, { 3, 9, -10 }, { 5, 9, -10 }, { 5, 9, -12 },
			{ 5, 9, -12 }, { 7, 10, -12 }, { 10, 12, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(6), .bpc = 10,
		{ 341, 15, 2048, 7, 16, 15, 15, {
			{ 0, 2, 2 }, { 2, 5, 0 }, { 3, 7, 0 }, { 4, 8, -2 },
			{ 6, 9, -4 }, { 7, 10, -6 }, { 7, 11, -8 }, { 7, 12, -8 },
			{ 7, 12, -8 }, { 7, 13, -10 }, { 9, 13, -10 }, { 9, 13, -12 },
			{ 9, 13, -12 }, { 11, 14, -12 }, { 14, 15, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(6), .bpc = 12,
		{ 341, 15, 2048, 11, 20, 19, 19, {
			{ 0, 6, 2 }, { 4, 9, 0 }, { 7, 11, 0 }, { 8, 12, -2 },
			{ 10, 13, -4 }, { 11, 14, -6 }, { 11, 15, -8 }, { 11, 16, -8 },
			{ 11, 16, -8 }, { 11, 17, -10 }, { 13, 17, -10 },
			{ 13, 17, -12 }, { 13, 17, -12 }, { 15, 18, -12 },
			{ 18, 19, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(6), .bpc = 14,
		{ 341, 15, 2048, 15, 24, 23, 23, {
			{ 0, 6, 2 }, { 7, 10, 0 }, { 9, 13, 0 }, { 11, 16, -2 },
			{ 14, 17, -4 }, { 15, 18, -6 }, { 15, 19, -8 }, { 15, 20, -8 },
			{ 15, 20, -8 }, { 15, 21, -10 }, { 17, 21, -10 },
			{ 17, 21, -12 }, { 17, 21, -12 }, { 19, 22, -12 },
			{ 22, 23, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(6), .bpc = 16,
		{ 341, 15, 2048, 19, 28, 27, 27, {
			{ 0, 6, 2 }, { 6, 11, 0 }, { 11, 15, 0 }, { 14, 18, -2 },
			{ 18, 21, -4 }, { 19, 22, -6 }, { 19, 23, -8 }, { 19, 24, -8 },
			{ 19, 24, -8 }, { 19, 25, -10 }, { 21, 25, -10 },
			{ 21, 25, -12 }, { 21, 25, -12 }, { 23, 26, -12 },
			{ 26, 27, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 8,
		{ 256, 15, 2048, 3, 12, 11, 11, {
			{ 0, 0, 10 }, { 0, 1, 8 }, { 0, 1, 6 }, { 0, 2, 4 },
			{ 1, 2, 2 }, { 1, 3, 0 }, { 1, 3, -2 }, { 2, 4, -4 },
			{ 2, 5, -6 }, { 3, 5, -8 }, { 4, 6, -10 }, { 4, 7, -10 },
			{ 5, 7, -12 }, { 7, 8, -12 }, { 8, 9, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 10,
		{ 256, 15, 2048, 7, 16, 15, 15, {
			{ 0, 2, 10 }, { 2, 5, 8 }, { 3, 5, 6 }, { 4, 6, 4 },
			{ 5, 6, 2 }, { 5, 7, 0 }, { 5, 7, -2 }, { 6, 8, -4 },
			{ 6, 9, -6 }, { 7, 9, -8 }, { 8, 10, -10 }, { 8, 11, -10 },
			{ 9, 11, -12 }, { 11, 12, -12 }, { 12, 13, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 12,
		{ 256, 15, 2048, 11, 20, 19, 19, {
			{ 0, 4, 10 }, { 2, 7, 8 }, { 4, 9, 6 }, { 6, 11, 4 },
			{ 9, 11, 2 }, { 9, 11, 0 }, { 9, 12, -2 }, { 10, 12, -4 },
			{ 11, 13, -6 }, { 11, 13, -8 }, { 12, 14, -10 },
			{ 13, 15, -10 }, { 13, 15, -12 }, { 15, 16, -12 },
			{ 16, 17, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 14,
		{ 256, 15, 2048, 15, 24, 23, 23, {
			{ 0, 4, 10 }, { 3, 8, 8 }, { 6, 11, 6 }, { 9, 14, 4 },
			{ 13, 15, 2 }, { 13, 15, 0 }, { 13, 16, -2 }, { 14, 16, -4 },
			{ 15, 17, -6 }, { 15, 17, -8 }, { 16, 18, -10 },
			{ 17, 19, -10 }, { 17, 19, -12 }, { 19, 20, -12 },
			{ 20, 21, -12 }
			}
		}
	},
	{
		.bpp = DSC_BPP(8), .bpc = 16,
		{ 256, 15, 2048, 19, 28, 27, 27, {
			{ 0, 4, 10 }, { 4, 9, 8 }, { 8, 13, 6 }, { 12, 17, 4 },
			{ 17, 19, 2 }, { 17, 20, 0 }, { 17, 20, -2 }, { 18, 20, -4 },
			{ 19, 21, -6 }, { 19, 21, -8 }, { 20, 22, -10 },
			{ 21, 23, -10 }, { 21, 23, -12 }, { 23, 24, -12 },
			{ 24, 25, -12 }
			}
		}
	},
	{ /* sentinel */ }
};

static const struct rc_parameters *get_rc_params(const struct rc_parameters_data *rc_parameters,
						 u16 dsc_bpp,
						 u8 bits_per_component)
{
	int i;

	for (i = 0; rc_parameters[i].bpp; i++)
		if (rc_parameters[i].bpp == dsc_bpp &&
		    rc_parameters[i].bpc == bits_per_component)
			return &rc_parameters[i].params;

	return NULL;
}

/**
 * drm_dsc_setup_rc_params() - Set parameters and limits for RC model in
 * accordance with the DSC 1.1 or 1.2 specification and DSC C Model
 * Required bits_per_pixel and bits_per_component to be set before calling this
 * function.
 *
 * @vdsc_cfg: DSC Configuration data partially filled by driver
 * @type: operating mode and standard to follow
 *
 * Return: 0 or -error code in case of an error
 */
int drm_dsc_setup_rc_params(struct drm_dsc_config *vdsc_cfg, enum drm_dsc_params_type type)
{
	const struct rc_parameters_data *data;
	const struct rc_parameters *rc_params;
	int i;

	if (WARN_ON_ONCE(!vdsc_cfg->bits_per_pixel ||
			 !vdsc_cfg->bits_per_component))
		return -EINVAL;

	switch (type) {
	case DRM_DSC_1_2_444:
		data = rc_parameters_1_2_444;
		break;
	case DRM_DSC_1_1_PRE_SCR:
		data = rc_parameters_pre_scr;
		break;
	case DRM_DSC_1_2_422:
		data = rc_parameters_1_2_422;
		break;
	case DRM_DSC_1_2_420:
		data = rc_parameters_1_2_420;
		break;
	default:
		return -EINVAL;
	}

	rc_params = get_rc_params(data,
				  vdsc_cfg->bits_per_pixel,
				  vdsc_cfg->bits_per_component);
	if (!rc_params)
		return -EINVAL;

	vdsc_cfg->first_line_bpg_offset = rc_params->first_line_bpg_offset;
	vdsc_cfg->initial_xmit_delay = rc_params->initial_xmit_delay;
	vdsc_cfg->initial_offset = rc_params->initial_offset;
	vdsc_cfg->flatness_min_qp = rc_params->flatness_min_qp;
	vdsc_cfg->flatness_max_qp = rc_params->flatness_max_qp;
	vdsc_cfg->rc_quant_incr_limit0 = rc_params->rc_quant_incr_limit0;
	vdsc_cfg->rc_quant_incr_limit1 = rc_params->rc_quant_incr_limit1;

	for (i = 0; i < DSC_NUM_BUF_RANGES; i++) {
		vdsc_cfg->rc_range_params[i].range_min_qp =
			rc_params->rc_range_params[i].range_min_qp;
		vdsc_cfg->rc_range_params[i].range_max_qp =
			rc_params->rc_range_params[i].range_max_qp;
		/*
		 * Range BPG Offset uses 2's complement and is only a 6 bits. So
		 * mask it to get only 6 bits.
		 */
		vdsc_cfg->rc_range_params[i].range_bpg_offset =
			rc_params->rc_range_params[i].range_bpg_offset &
			DSC_RANGE_BPG_OFFSET_MASK;
	}

	return 0;
}
EXPORT_SYMBOL(drm_dsc_setup_rc_params);

/**
 * drm_dsc_compute_rc_parameters() - Write rate control
 * parameters to the dsc configuration defined in
 * &struct drm_dsc_config in accordance with the DSC 1.2
 * specification. Some configuration fields must be present
 * beforehand.
 *
 * @vdsc_cfg:
 * DSC Configuration data partially filled by driver
 */
int drm_dsc_compute_rc_parameters(struct drm_dsc_config *vdsc_cfg)
{
	unsigned long groups_per_line = 0;
	unsigned long groups_total = 0;
	unsigned long num_extra_mux_bits = 0;
	unsigned long slice_bits = 0;
	unsigned long hrd_delay = 0;
	unsigned long final_scale = 0;
	unsigned long rbs_min = 0;

	if (vdsc_cfg->native_420 || vdsc_cfg->native_422) {
		/* Number of groups used to code each line of a slice */
		groups_per_line = DIV_ROUND_UP(vdsc_cfg->slice_width / 2,
					       DSC_RC_PIXELS_PER_GROUP);

		/* chunksize in Bytes */
		vdsc_cfg->slice_chunk_size = DIV_ROUND_UP(vdsc_cfg->slice_width / 2 *
							  vdsc_cfg->bits_per_pixel,
							  (8 * 16));
	} else {
		/* Number of groups used to code each line of a slice */
		groups_per_line = DIV_ROUND_UP(vdsc_cfg->slice_width,
					       DSC_RC_PIXELS_PER_GROUP);

		/* chunksize in Bytes */
		vdsc_cfg->slice_chunk_size = DIV_ROUND_UP(vdsc_cfg->slice_width *
							  vdsc_cfg->bits_per_pixel,
							  (8 * 16));
	}

	if (vdsc_cfg->convert_rgb)
		num_extra_mux_bits = 3 * (vdsc_cfg->mux_word_size +
					  (4 * vdsc_cfg->bits_per_component + 4)
					  - 2);
	else if (vdsc_cfg->native_422)
		num_extra_mux_bits = 4 * vdsc_cfg->mux_word_size +
			(4 * vdsc_cfg->bits_per_component + 4) +
			3 * (4 * vdsc_cfg->bits_per_component) - 2;
	else
		num_extra_mux_bits = 3 * vdsc_cfg->mux_word_size +
			(4 * vdsc_cfg->bits_per_component + 4) +
			2 * (4 * vdsc_cfg->bits_per_component) - 2;
	/* Number of bits in one Slice */
	slice_bits = 8 * vdsc_cfg->slice_chunk_size * vdsc_cfg->slice_height;

	while ((num_extra_mux_bits > 0) &&
	       ((slice_bits - num_extra_mux_bits) % vdsc_cfg->mux_word_size))
		num_extra_mux_bits--;

	if (groups_per_line < vdsc_cfg->initial_scale_value - 8)
		vdsc_cfg->initial_scale_value = groups_per_line + 8;

	/* scale_decrement_interval calculation according to DSC spec 1.11 */
	if (vdsc_cfg->initial_scale_value > 8)
		vdsc_cfg->scale_decrement_interval = groups_per_line /
			(vdsc_cfg->initial_scale_value - 8);
	else
		vdsc_cfg->scale_decrement_interval = DSC_SCALE_DECREMENT_INTERVAL_MAX;

	vdsc_cfg->final_offset = vdsc_cfg->rc_model_size -
		(vdsc_cfg->initial_xmit_delay *
		 vdsc_cfg->bits_per_pixel + 8) / 16 + num_extra_mux_bits;

	if (vdsc_cfg->final_offset >= vdsc_cfg->rc_model_size) {
		DRM_DEBUG_KMS("FinalOfs < RcModelSze for this InitialXmitDelay\n");
		return -ERANGE;
	}

	final_scale = (vdsc_cfg->rc_model_size * 8) /
		(vdsc_cfg->rc_model_size - vdsc_cfg->final_offset);
	if (vdsc_cfg->slice_height > 1)
		/*
		 * NflBpgOffset is 16 bit value with 11 fractional bits
		 * hence we multiply by 2^11 for preserving the
		 * fractional part
		 */
		vdsc_cfg->nfl_bpg_offset = DIV_ROUND_UP((vdsc_cfg->first_line_bpg_offset << 11),
							(vdsc_cfg->slice_height - 1));
	else
		vdsc_cfg->nfl_bpg_offset = 0;

	/* Number of groups used to code the entire slice */
	groups_total = groups_per_line * vdsc_cfg->slice_height;

	/* slice_bpg_offset is 16 bit value with 11 fractional bits */
	vdsc_cfg->slice_bpg_offset = DIV_ROUND_UP(((vdsc_cfg->rc_model_size -
						    vdsc_cfg->initial_offset +
						    num_extra_mux_bits) << 11),
						  groups_total);

	if (final_scale > 9) {
		/*
		 * ScaleIncrementInterval =
		 * finaloffset/((NflBpgOffset + SliceBpgOffset)*8(finalscale - 1.125))
		 * as (NflBpgOffset + SliceBpgOffset) has 11 bit fractional value,
		 * we need divide by 2^11 from pstDscCfg values
		 */
		vdsc_cfg->scale_increment_interval =
				(vdsc_cfg->final_offset * (1 << 11)) /
				((vdsc_cfg->nfl_bpg_offset +
				vdsc_cfg->slice_bpg_offset) *
				(final_scale - 9));
	} else {
		/*
		 * If finalScaleValue is less than or equal to 9, a value of 0 should
		 * be used to disable the scale increment at the end of the slice
		 */
		vdsc_cfg->scale_increment_interval = 0;
	}

	/*
	 * DSC spec mentions that bits_per_pixel specifies the target
	 * bits/pixel (bpp) rate that is used by the encoder,
	 * in steps of 1/16 of a bit per pixel
	 */
	rbs_min = vdsc_cfg->rc_model_size - vdsc_cfg->initial_offset +
		DIV_ROUND_UP(vdsc_cfg->initial_xmit_delay *
			     vdsc_cfg->bits_per_pixel, 16) +
		groups_per_line * vdsc_cfg->first_line_bpg_offset;

	hrd_delay = DIV_ROUND_UP((rbs_min * 16), vdsc_cfg->bits_per_pixel);
	vdsc_cfg->rc_bits = (hrd_delay * vdsc_cfg->bits_per_pixel) / 16;
	vdsc_cfg->initial_dec_delay = hrd_delay - vdsc_cfg->initial_xmit_delay;

	return 0;
}
EXPORT_SYMBOL(drm_dsc_compute_rc_parameters);

/**
 * drm_dsc_get_bpp_int() - Get integer bits per pixel value for the given DRM DSC config
 * @vdsc_cfg: Pointer to DRM DSC config struct
 *
 * Return: Integer BPP value
 */
u32 drm_dsc_get_bpp_int(const struct drm_dsc_config *vdsc_cfg)
{
	WARN_ON_ONCE(vdsc_cfg->bits_per_pixel & 0xf);
	return vdsc_cfg->bits_per_pixel >> 4;
}
EXPORT_SYMBOL(drm_dsc_get_bpp_int);

/**
 * drm_dsc_initial_scale_value() - Calculate the initial scale value for the given DSC config
 * @dsc: Pointer to DRM DSC config struct
 *
 * Return: Calculated initial scale value
 */
u8 drm_dsc_initial_scale_value(const struct drm_dsc_config *dsc)
{
	return 8 * dsc->rc_model_size / (dsc->rc_model_size - dsc->initial_offset);
}
EXPORT_SYMBOL(drm_dsc_initial_scale_value);

/**
 * drm_dsc_flatness_det_thresh() - Calculate the flatness_det_thresh for the given DSC config
 * @dsc: Pointer to DRM DSC config struct
 *
 * Return: Calculated flatness det thresh value
 */
u32 drm_dsc_flatness_det_thresh(const struct drm_dsc_config *dsc)
{
	return 2 << (dsc->bits_per_component - 8);
}
EXPORT_SYMBOL(drm_dsc_flatness_det_thresh);

static void drm_dsc_dump_config_main_params(struct drm_printer *p, int indent,
					    const struct drm_dsc_config *cfg)
{
	drm_printf_indent(p, indent,
			  "dsc-cfg: version: %d.%d, picture: w=%d, h=%d, slice: count=%d, w=%d, h=%d, size=%d\n",
			  cfg->dsc_version_major, cfg->dsc_version_minor,
			  cfg->pic_width, cfg->pic_height,
			  cfg->slice_count, cfg->slice_width, cfg->slice_height, cfg->slice_chunk_size);
	drm_printf_indent(p, indent,
			  "dsc-cfg: mode: block-pred=%s, vbr=%s, rgb=%s, simple-422=%s, native-422=%s, native-420=%s\n",
			  str_yes_no(cfg->block_pred_enable), str_yes_no(cfg->vbr_enable),
			  str_yes_no(cfg->convert_rgb),
			  str_yes_no(cfg->simple_422), str_yes_no(cfg->native_422), str_yes_no(cfg->native_420));
	drm_printf_indent(p, indent,
			  "dsc-cfg: color-depth: uncompressed-bpc=%d, compressed-bpp=" FXP_Q4_FMT " line-buf-bpp=%d\n",
			  cfg->bits_per_component, FXP_Q4_ARGS(cfg->bits_per_pixel), cfg->line_buf_depth);
	drm_printf_indent(p, indent,
			  "dsc-cfg: rc-model: size=%d, bits=%d, mux-word-size: %d, initial-delays: xmit=%d, dec=%d\n",
			  cfg->rc_model_size, cfg->rc_bits, cfg->mux_word_size,
			  cfg->initial_xmit_delay, cfg->initial_dec_delay);
	drm_printf_indent(p, indent,
			  "dsc-cfg: offsets: initial=%d, final=%d, slice-bpg=%d\n",
			  cfg->initial_offset, cfg->final_offset, cfg->slice_bpg_offset);
	drm_printf_indent(p, indent,
			  "dsc-cfg: line-bpg-offsets: first=%d, non-first=%d, second=%d, non-second=%d, second-adj=%d\n",
			  cfg->first_line_bpg_offset, cfg->nfl_bpg_offset,
			  cfg->second_line_bpg_offset, cfg->nsl_bpg_offset, cfg->second_line_offset_adj);
	drm_printf_indent(p, indent,
			  "dsc-cfg: rc-tgt-offsets: low=%d, high=%d, rc-edge-factor: %d, rc-quant-incr-limits: [0]=%d, [1]=%d\n",
			  cfg->rc_tgt_offset_low, cfg->rc_tgt_offset_high,
			  cfg->rc_edge_factor, cfg->rc_quant_incr_limit0, cfg->rc_quant_incr_limit1);
	drm_printf_indent(p, indent,
			  "dsc-cfg: initial-scale: %d, scale-intervals: increment=%d, decrement=%d\n",
			  cfg->initial_scale_value, cfg->scale_increment_interval, cfg->scale_decrement_interval);
	drm_printf_indent(p, indent,
			  "dsc-cfg: flatness: min-qp=%d, max-qp=%d\n",
			  cfg->flatness_min_qp, cfg->flatness_max_qp);
}

static void drm_dsc_dump_config_rc_params(struct drm_printer *p, int indent,
					  const struct drm_dsc_config *cfg)
{
	const u16 *bt = cfg->rc_buf_thresh;
	const struct drm_dsc_rc_range_parameters *rp = cfg->rc_range_params;

	BUILD_BUG_ON(ARRAY_SIZE(cfg->rc_buf_thresh) != 14);
	BUILD_BUG_ON(ARRAY_SIZE(cfg->rc_range_params) != 15);

	drm_printf_indent(p, indent,
			  "dsc-cfg: rc-level:         0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14\n");
	drm_printf_indent(p, indent,
			  "dsc-cfg: rc-buf-thresh:  %3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d\n",
			  bt[0], bt[1], bt[2],  bt[3],  bt[4],  bt[5], bt[6], bt[7],
			  bt[8], bt[9], bt[10], bt[11], bt[12], bt[13]);
	drm_printf_indent(p, indent,
			  "dsc-cfg: rc-min-qp:      %3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d\n",
			  rp[0].range_min_qp,  rp[1].range_min_qp,  rp[2].range_min_qp,  rp[3].range_min_qp,
			  rp[4].range_min_qp,  rp[5].range_min_qp,  rp[6].range_min_qp,  rp[7].range_min_qp,
			  rp[8].range_min_qp,  rp[9].range_min_qp,  rp[10].range_min_qp, rp[11].range_min_qp,
			  rp[12].range_min_qp, rp[13].range_min_qp, rp[14].range_min_qp);
	drm_printf_indent(p, indent,
			  "dsc-cfg: rc-max-qp:      %3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d\n",
			  rp[0].range_max_qp,  rp[1].range_max_qp,  rp[2].range_max_qp,  rp[3].range_max_qp,
			  rp[4].range_max_qp,  rp[5].range_max_qp,  rp[6].range_max_qp,  rp[7].range_max_qp,
			  rp[8].range_max_qp,  rp[9].range_max_qp,  rp[10].range_max_qp, rp[11].range_max_qp,
			  rp[12].range_max_qp, rp[13].range_max_qp, rp[14].range_max_qp);
	drm_printf_indent(p, indent,
			  "dsc-cfg: rc-bpg-offset:  %3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d,%3d\n",
			  rp[0].range_bpg_offset,  rp[1].range_bpg_offset,  rp[2].range_bpg_offset,  rp[3].range_bpg_offset,
			  rp[4].range_bpg_offset,  rp[5].range_bpg_offset,  rp[6].range_bpg_offset,  rp[7].range_bpg_offset,
			  rp[8].range_bpg_offset,  rp[9].range_bpg_offset,  rp[10].range_bpg_offset, rp[11].range_bpg_offset,
			  rp[12].range_bpg_offset, rp[13].range_bpg_offset, rp[14].range_bpg_offset);
}

/**
 * drm_dsc_dump_config - Dump the provided DSC configuration
 * @p: The printer used for output
 * @indent: Tab indentation level (max 5)
 * @cfg: DSC configuration to print
 *
 * Print the provided DSC configuration in @cfg.
 */
void drm_dsc_dump_config(struct drm_printer *p, int indent,
			 const struct drm_dsc_config *cfg)
{
	drm_dsc_dump_config_main_params(p, indent, cfg);
	drm_dsc_dump_config_rc_params(p, indent, cfg);
}
EXPORT_SYMBOL(drm_dsc_dump_config);
