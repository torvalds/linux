// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corp
 *
 * Author:
 * Manasi Navare <manasi.d.navare@intel.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/byteorder/generic.h>
#include <drm/drm_print.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_dsc.h>

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
