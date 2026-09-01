// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.
#include "dml2_core_dcn6_funcs_mode_support.h"
#include "dml2_core_utils.h"
#include "dml2_core_dcn5_calcs_dchub.h"
#include "dml2_core_dcn5_calcs_display_pipe.h"
#include "dml2_core_dcn6_calcs_dchub.h"
#include "dml_top_types.h"

static void dcn6_ms_check_input_sanity(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	outputs->support.ViewportExceedsSurface = false;
	if (!ctx->display_cfg->overrides.hw.surface_viewport_size_check_disable) {
		for (k = 0; k < ctx->display_cfg->num_planes; k++) {
			plane = &ctx->display_cfg->plane_descriptors[k];
			if (plane->composition.viewport.plane0.width > plane->surface.plane0.width
					|| plane->composition.viewport.plane0.height > plane->surface.plane0.height) {
				outputs->support.ViewportExceedsSurface = true;
				DML_LOG_VERBOSE("DML::%s: k=%u ViewportWidth = %ld\n", __func__, k, plane->composition.viewport.plane0.width);
				DML_LOG_VERBOSE("DML::%s: k=%u SurfaceWidthY = %ld\n", __func__, k, plane->surface.plane0.width);
				DML_LOG_VERBOSE("DML::%s: k=%u ViewportHeight = %ld\n", __func__, k, plane->composition.viewport.plane0.height);
				DML_LOG_VERBOSE("DML::%s: k=%u SurfaceHeightY = %ld\n", __func__, k, plane->surface.plane0.height);
				DML_LOG_VERBOSE("DML::%s: k=%u ViewportExceedsSurface = %d\n", __func__, k, outputs->support.ViewportExceedsSurface);
			}
			if (dml2_core_utils_is_420(plane->pixel_format) || dml2_core_utils_is_422_planar(plane->pixel_format)
					|| plane->pixel_format == dml2_rgbe_alpha) {
				if (plane->composition.viewport.plane1.width > plane->surface.plane1.width
						|| plane->composition.viewport.plane1.height > plane->surface.plane1.height) {
					outputs->support.ViewportExceedsSurface = true;
				}
			}
		}
	}

	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_desired_output_bpp(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	dml2_core_utils_get_stream_output_bpp(outputs->DesiredOutputBpp, display_cfg);

	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->DesiredOutputBpp, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_max_det_and_min_compressed_buffer_size(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	dcn5_calculate_max_det_and_min_compressed_buffer_size(
			ip->config_return_buffer_size_in_kbytes,
			ip->config_return_buffer_segment_size_in_kbytes,
			ip->rob_buffer_size_kbytes,
			ip->max_num_dpp,
			display_cfg->overrides.hw.force_nom_det_size_kbytes.enable,
			display_cfg->overrides.hw.force_nom_det_size_kbytes.value,
			ip->dcn_mrq_present,

			/* Output */
			&outputs->MaxTotalDETInKByte,
			&outputs->NomDETInKByte,
			&outputs->MinCompressedBufferSizeInKByte);

	DML_LOG_DEBUG_UINT(outputs->MaxTotalDETInKByte);
	DML_LOG_DEBUG_UINT(outputs->MinCompressedBufferSizeInKByte);
	DML_LOG_DEBUG_UINT(outputs->NomDETInKByte);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_effective_pixel_clock(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	/*
	 * This function should probably be removed since ptoi is never true, so the function is a noop; not really
	 * obvious if the comment means DML2.1 doesn't support interlace today.
	 */
	dcn5_adjust_pixel_clock_for_progressive_to_interlace_unit(display_cfg, ip->ptoi_supported, outputs->PixelClockBackEnd);

	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->PixelClockBackEnd, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_scaler_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	/*Scale Ratio, taps Support Check*/
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	const struct dml2_plane_parameters *plane;

	DML_LOG_FUNC_ENTER();
	outputs->support.ScaleRatioAndTapsSupport = true;
	// Many core tests are still setting scaling parameters "incorrectly"
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		if (plane->composition.scaler_info.enabled == false
				&& (dml2_core_utils_is_420(plane->pixel_format) || dml2_core_utils_is_422_planar(plane->pixel_format) || dml2_core_utils_is_422_packed(plane->pixel_format)
						|| plane->composition.scaler_info.plane0.h_ratio != 1.0
						|| plane->composition.scaler_info.plane0.h_taps != 1.0
						|| plane->composition.scaler_info.plane0.v_ratio != 1.0
						|| plane->composition.scaler_info.plane0.v_taps != 1.0)) {
			outputs->support.ScaleRatioAndTapsSupport = false;
		} else if (plane->composition.scaler_info.plane0.v_taps < 1.0
				|| plane->composition.scaler_info.plane0.v_taps > 8.0
				|| plane->composition.scaler_info.plane0.h_taps < 1.0
				|| plane->composition.scaler_info.plane0.h_taps > 8.0
				|| (plane->composition.scaler_info.plane0.h_taps > 1.0
						&& (plane->composition.scaler_info.plane0.h_taps % 2) == 1)
				|| plane->composition.scaler_info.plane0.h_ratio > ip->max_hscl_ratio
				|| plane->composition.scaler_info.plane0.v_ratio > ip->max_vscl_ratio
				|| plane->composition.scaler_info.plane0.h_ratio > plane->composition.scaler_info.plane0.h_taps
				|| plane->composition.scaler_info.plane0.v_ratio > plane->composition.scaler_info.plane0.v_taps
				|| ((dml2_core_utils_is_420(plane->pixel_format) || dml2_core_utils_is_422_planar(plane->pixel_format))
						&& (plane->composition.scaler_info.plane1.v_taps < 1
								|| plane->composition.scaler_info.plane1.v_taps > 8
								|| plane->composition.scaler_info.plane1.h_taps < 1
								|| plane->composition.scaler_info.plane1.h_taps > 8
								|| (plane->composition.scaler_info.plane1.h_taps > 1
										&& plane->composition.scaler_info.plane1.h_taps % 2 == 1)
								|| plane->composition.scaler_info.plane1.h_ratio > ip->max_hscl_ratio
								|| plane->composition.scaler_info.plane1.v_ratio > ip->max_vscl_ratio
								|| plane->composition.scaler_info.plane1.h_ratio > plane->composition.scaler_info.plane1.h_taps
								|| plane->composition.scaler_info.plane1.v_ratio > plane->composition.scaler_info.plane1.v_taps))) {
			outputs->support.ScaleRatioAndTapsSupport = false;
		}
	}

	DML_LOG_DEBUG_BOOL(outputs->support.ScaleRatioAndTapsSupport);
	DML_LOG_FUNC_EXIT();

	return outputs->support.ScaleRatioAndTapsSupport;
}

static bool dcn6_ms_check_source_format_and_scan_direction(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	/*Source Format, Pixel Format and Scan Support Check*/
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	const struct dml2_plane_parameters *plane;

	DML_LOG_FUNC_ENTER();
	outputs->support.SourceFormatPixelAndScanSupport = true;
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		if (plane->surface.tiling == dml2_sw_linear
				&& dml2_core_utils_is_vertical_rotation(plane->composition.rotation_angle)) {
			outputs->support.SourceFormatPixelAndScanSupport = false;
		}
	}

	DML_LOG_DEBUG_BOOL(outputs->support.SourceFormatPixelAndScanSupport);
	DML_LOG_FUNC_EXIT();

	return outputs->support.SourceFormatPixelAndScanSupport;
}

static void dcn6_ms_calculate_byte_per_pixel_and_block_sizes(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	const struct dml2_plane_parameters *plane;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		dcn5_calculate_byte_per_pixel_and_block_sizes(
				plane->pixel_format,
				plane->surface.tiling,
				plane->surface.plane0.pitch,
				plane->surface.plane1.pitch,
				/* Output */
				&outputs->BytePerPixelY[k],
				&outputs->BytePerPixelC[k],
				&outputs->BytePerPixelInDETY[k],
				&outputs->BytePerPixelInDETC[k],
				&outputs->Read256BlockHeightY[k],
				&outputs->Read256BlockHeightC[k],
				&outputs->Read256BlockWidthY[k],
				&outputs->Read256BlockWidthC[k],
				&outputs->MacroTileHeightY[k],
				&outputs->MacroTileHeightC[k],
				&outputs->MacroTileWidthY[k],
				&outputs->MacroTileWidthC[k],
				&outputs->surf_linear128_l[k],
				&outputs->surf_linear128_c[k]);
	}

	DML_LOG_DEBUG_ARRAY_BOOL(outputs->surf_linear128_c, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->MacroTileWidthY, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->BytePerPixelInDETY, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->Read256BlockWidthY, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->Read256BlockHeightC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->BytePerPixelInDETC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->BytePerPixelY, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->Read256BlockWidthC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->MacroTileHeightY, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->BytePerPixelC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->MacroTileHeightC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->surf_linear128_l, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->Read256BlockHeightY, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->MacroTileWidthC, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_read_bandwidth(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *outputs = states;
	struct dml2_core_internal_mode_support *inputs = states;
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;

	DML_LOG_FUNC_ENTER();
	/* Bandwidth Support Check */
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		if (!dml2_core_utils_is_vertical_rotation(plane->composition.rotation_angle)) {
			outputs->SwathWidthYSingleDPP[k] = plane->composition.viewport.plane0.width;
			outputs->SwathWidthCSingleDPP[k] = plane->composition.viewport.plane1.width;
		} else {
			outputs->SwathWidthYSingleDPP[k] = plane->composition.viewport.plane0.height;
			outputs->SwathWidthCSingleDPP[k] = plane->composition.viewport.plane1.height;
		}
	}
	for (k = 0; k < display_cfg->num_planes ; k++) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];
		outputs->vactive_sw_bw_l[k] = outputs->SwathWidthYSingleDPP[k]
				* math_ceil2(inputs->BytePerPixelY[k], 1.0)
				/ (stream->timing.h_total / ((double) stream->timing.pixel_clock_khz / 1000))
				* plane->composition.scaler_info.plane0.v_ratio;
		outputs->vactive_sw_bw_c[k] = outputs->SwathWidthCSingleDPP[k]
				* math_ceil2(inputs->BytePerPixelC[k], 2.0)
				/ (stream->timing.h_total / ((double) stream->timing.pixel_clock_khz / 1000))
				* plane->composition.scaler_info.plane1.v_ratio;
		outputs->cursor_bw[k] = plane->cursor.num_cursors
				* plane->cursor.cursor_width
				* plane->cursor.cursor_bpp
				/ 8.0
				/ (stream->timing.h_total / ((double) stream->timing.pixel_clock_khz / 1000));
		DML_LOG_VERBOSE("DML::%s: k=%u, vactive_sw_bw_l = %f\n", __func__, k,
				outputs->vactive_sw_bw_l[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, vactive_sw_bw_c = %f\n", __func__, k,
				outputs->vactive_sw_bw_c[k]);
	}
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->vactive_sw_bw_c, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->SwathWidthYSingleDPP, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->vactive_sw_bw_l, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->cursor_bw, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->SwathWidthCSingleDPP, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_writeback_bandwidth(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k, j;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;

	DML_LOG_FUNC_ENTER();
	// Writeback bandwidth
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];
		for (j = 0; j < stream->writeback.active_writebacks_per_stream; j++) {
			outputs->WriteBandwidth[k][j] = stream->writeback.writeback_stream[j].output_height * stream->writeback.writeback_stream[j].output_width
				/ (stream->writeback.writeback_stream[j].input_height * stream->timing.h_total / ((double)stream->timing.pixel_clock_khz / 1000));
			if (stream->writeback.writeback_stream[j].pixel_format == dml2_444_64) {
				outputs->WriteBandwidth[k][j] *= 8.0;
			} else if (stream->writeback.writeback_stream[j].pixel_format == dml2_444_32) {
				outputs->WriteBandwidth[k][j] *= 4.0;
			} else if (stream->writeback.writeback_stream[j].pixel_format == dml2_420_8) {
				outputs->WriteBandwidth[k][j] *= 1.5;
			} else if (stream->writeback.writeback_stream[j].pixel_format == dml2_420_10) {
				outputs->WriteBandwidth[k][j] *= 3.0;
			} else if (stream->writeback.writeback_stream[j].pixel_format == dml2_422_packed_8) {
				outputs->WriteBandwidth[k][j] *= 2.0;
			} else if (stream->writeback.writeback_stream[j].pixel_format == dml2_422_packed_10) {
				outputs->WriteBandwidth[k][j] *= 4.0;
			} else {
				outputs->WriteBandwidth[k][j] = 0.0;
			}
		}
	}

	DML_LOG_DEBUG_2D_ARRAY_DOUBLE(outputs->WriteBandwidth, display_cfg->num_planes, DML2_MAX_WRITEBACK);;
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_writeback_bandwidth_latency_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	/*Writeback Latency support check*/
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k, j;
	const struct dml2_stream_parameters *stream;

	DML_LOG_FUNC_ENTER();
	outputs->support.WritebackLatencySupport = true;
	for (k = 0; k < display_cfg->num_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		for (j = 0; j < stream->writeback.active_writebacks_per_stream; j++) {
			if (stream->writeback.writeback_stream[j].pixel_format == dml2_420_8 || stream->writeback.writeback_stream[j].pixel_format == dml2_420_10) {// In planar mode just check luma bw does not exceed half latency hiding buffer
				if ((inputs->WriteBandwidth[k][j] / 1.5 >
					ip->writeback_interface_buffer_size_kbytes * 1024 / 2.0 // half buffer for luma
					* ((stream->writeback.writeback_stream[j].pixel_format == dml2_420_10) ? 1.6 : 1.0) // 16 bit frame buffer to 10 bit buffer packing
					/ soc_bb->writeback_base_latency_us))
					outputs->support.WritebackLatencySupport = false;
				else
					if ((inputs->WriteBandwidth[k][j] >
						ip->writeback_interface_buffer_size_kbytes * 1024
						* ((stream->writeback.writeback_stream[j].pixel_format == dml2_422_packed_10) ? 1.6 : 1.0)
						/ soc_bb->writeback_base_latency_us))
						outputs->support.WritebackLatencySupport = false;
			}
		}
	}

	DML_LOG_DEBUG_BOOL(outputs->support.WritebackLatencySupport);
	DML_LOG_FUNC_EXIT();

	return outputs->support.WritebackLatencySupport;
}

static bool dcn6_ms_check_writeback_scale_ratio_and_taps_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	/* Writeback Scale Ratio and Taps Support Check */
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	unsigned int j;
	const struct dml2_stream_parameters *stream;

	DML_LOG_FUNC_ENTER();
	outputs->support.WritebackScaleRatioAndTapsSupport = true;
	for (k = 0; k <= display_cfg->num_planes - 1; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];

		for (j = 0; j < stream->writeback.active_writebacks_per_stream; j++) {

			double h_ratio_chroma;
			double output_width_chroma;

			if (stream->writeback.writeback_stream[j].pixel_format == dml2_420_8 || stream->writeback.writeback_stream[j].pixel_format == dml2_422_packed_8
				|| stream->writeback.writeback_stream[j].pixel_format == dml2_420_10 || stream->writeback.writeback_stream[j].pixel_format == dml2_422_packed_10) {
				h_ratio_chroma = 2.0 * stream->writeback.writeback_stream[j].h_ratio;
				output_width_chroma = 0.5 * stream->writeback.writeback_stream[j].output_width;
			} else {
				h_ratio_chroma = stream->writeback.writeback_stream[j].h_ratio;
				output_width_chroma = stream->writeback.writeback_stream[j].output_width;
			}

			double v_ratio_chroma = ((stream->writeback.writeback_stream[j].pixel_format == dml2_420_8 || stream->writeback.writeback_stream[j].pixel_format == dml2_420_10) ? 2.0 : 1.0)
				* stream->writeback.writeback_stream[j].v_ratio;

			if (stream->writeback.writeback_stream[j].h_ratio > ip->writeback_max_hscl_ratio
				|| stream->writeback.writeback_stream[j].v_ratio > ip->writeback_max_vscl_ratio
				|| stream->writeback.writeback_stream[j].h_ratio < ip->writeback_min_hscl_ratio
				|| stream->writeback.writeback_stream[j].v_ratio < ip->writeback_min_vscl_ratio
				|| stream->writeback.writeback_stream[j].h_taps > (unsigned int) ip->writeback_max_hscl_taps
				|| stream->writeback.writeback_stream[j].v_taps > (unsigned int) ip->writeback_max_vscl_taps
				|| stream->writeback.writeback_stream[j].h_taps_chroma > (unsigned int)ip->writeback_max_hscl_taps
				|| stream->writeback.writeback_stream[j].v_taps_chroma > (unsigned int)ip->writeback_max_vscl_taps
				|| stream->writeback.writeback_stream[j].h_ratio > (unsigned int)stream->writeback.writeback_stream[j].h_taps
				|| stream->writeback.writeback_stream[j].v_ratio > (unsigned int)stream->writeback.writeback_stream[j].v_taps
				|| h_ratio_chroma > (unsigned int)stream->writeback.writeback_stream[j].h_taps_chroma
				|| v_ratio_chroma > (unsigned int)stream->writeback.writeback_stream[j].v_taps_chroma
				|| (stream->writeback.writeback_stream[j].h_taps > 2.0 && ((stream->writeback.writeback_stream[j].h_taps % 2) == 1))
				|| (stream->writeback.writeback_stream[j].h_taps_chroma > 2.0 && ((stream->writeback.writeback_stream[j].h_taps_chroma % 2) == 1))) {
				DML_LOG_VERBOSE("DML::%s: k=%d, j=%d, not support (%d)\n", __func__, k, j, __LINE__);
				outputs->support.WritebackScaleRatioAndTapsSupport = false;
			}

			double writeback_luma_vextra = (stream->writeback.writeback_stream[j].v_ratio < 1) ?
				math_max2(1 - 2.0 / math_ceil2(1 / stream->writeback.writeback_stream[j].v_ratio, 1.0), 0.0) : -1.0;

			if (stream->writeback.writeback_stream[j].output_width * (stream->writeback.writeback_stream[j].v_taps + writeback_luma_vextra)
			> ip->writeback_line_buffer_buffer_size / 3.0 / 10.0) { // One third of the buffer per each component Y Cb Cr
				DML_LOG_VERBOSE("DML::%s: k=%d, j=%d, not support (%d)\n", __func__, k, j, __LINE__);
				outputs->support.WritebackScaleRatioAndTapsSupport = false;
			}

			double writeback_chroma_vextra = (v_ratio_chroma < 1) ? math_max2(1 - 2.0 / math_ceil2(1 / v_ratio_chroma, 1.0), 0.0) : -1.0;

			if (output_width_chroma * (stream->writeback.writeback_stream[j].v_taps_chroma + writeback_chroma_vextra)
			> ip->writeback_line_buffer_buffer_size / 3.0 / 10.0) { // One third of the buffer per each component Y Cb Cr
				DML_LOG_VERBOSE("DML::%s: k=%d, j=%d, not support (%d)\n", __func__, k, j, __LINE__);
				outputs->support.WritebackScaleRatioAndTapsSupport = false;
			}
		}
	}

	DML_LOG_DEBUG_BOOL(outputs->support.WritebackScaleRatioAndTapsSupport);
	DML_LOG_FUNC_EXIT();

	return outputs->support.WritebackScaleRatioAndTapsSupport;
}

static void dcn6_ms_calculate_single_pipe_dppclk_and_pscl_factor(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];
		dcn5_calculate_single_pipe_dppclk_and_scl_throughput(
				plane->composition.scaler_info.plane0.h_ratio,
				plane->composition.scaler_info.plane1.h_ratio,
				plane->composition.scaler_info.plane0.v_ratio,
				plane->composition.scaler_info.plane1.v_ratio,
				ip->max_dchub_pscl_bw_pix_per_clk,
				ip->max_pscl_lb_bw_pix_per_clk,
				((double) stream->timing.pixel_clock_khz / 1000),
				plane->pixel_format,
				plane->composition.scaler_info.plane0.h_taps,
				plane->composition.scaler_info.plane1.h_taps,
				plane->composition.scaler_info.plane0.v_taps,
				plane->composition.scaler_info.plane1.v_taps,

				/* Output */
				&outputs->PSCL_FACTOR[k],
				&outputs->PSCL_FACTOR_CHROMA[k],
				&outputs->MinDPPCLKUsingSingleDPP[k]);
	}

	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->MinDPPCLKUsingSingleDPP, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->PSCL_FACTOR_CHROMA, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->PSCL_FACTOR, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_max_swath_widths(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	// Max Viewport Size support
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	unsigned int lb_buffer_size_bits_luma;
	unsigned int lb_buffer_size_bits_chroma;
	unsigned int maximumSwathWidthSupportLuma;
	unsigned int maximumSwathWidthSupportChroma;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		if (plane->surface.tiling == dml2_sw_linear)
			maximumSwathWidthSupportLuma = 15360;
		else if (!dml2_core_utils_is_vertical_rotation(plane->composition.rotation_angle)
				&& inputs->BytePerPixelC[k] > 0
				&& plane->pixel_format != dml2_rgbe_alpha)
			// horz video
			maximumSwathWidthSupportLuma = 7680 + 16;
		else if (dml2_core_utils_is_vertical_rotation(plane->composition.rotation_angle)
				&& inputs->BytePerPixelC[k] > 0
				&& plane->pixel_format != dml2_rgbe_alpha)
			// vert video
			maximumSwathWidthSupportLuma = 4320 + 16;
		else if (plane->pixel_format == dml2_rgbe_alpha)
			// rgbe + alpha
			maximumSwathWidthSupportLuma = 5120 + 16;
		else if (dml2_core_utils_is_vertical_rotation(plane->composition.rotation_angle)
				&& inputs->BytePerPixelY[k] == 8
				&& plane->surface.dcc.enable == true)
			// vert 64bpp
			maximumSwathWidthSupportLuma = 3072 + 16;
		else
			maximumSwathWidthSupportLuma = 6144 + 16;

		if (!dml2_core_utils_is_vertical_rotation(plane->composition.rotation_angle) && dml2_core_utils_is_420(plane->pixel_format))
			maximumSwathWidthSupportChroma = (unsigned int) (maximumSwathWidthSupportLuma / 2.0);
		else if (!dml2_core_utils_is_vertical_rotation(plane->composition.rotation_angle) && dml2_core_utils_is_422_planar(plane->pixel_format))
			maximumSwathWidthSupportChroma = (unsigned int)(maximumSwathWidthSupportLuma / 2.0);
		else if (dml2_core_utils_is_vertical_rotation(plane->composition.rotation_angle) && dml2_core_utils_is_420(plane->pixel_format))
			maximumSwathWidthSupportChroma = (unsigned int)(maximumSwathWidthSupportLuma / 2.0);
		else
			maximumSwathWidthSupportChroma = maximumSwathWidthSupportLuma;

		lb_buffer_size_bits_luma = ip->line_buffer_size_bits;
		lb_buffer_size_bits_chroma = ip->line_buffer_size_bits;

		outputs->MaximumSwathWidthInLineBufferLuma = lb_buffer_size_bits_luma
				* math_max2(plane->composition.scaler_info.plane0.h_ratio, 1.0)
				/ 57
				/ (plane->composition.scaler_info.plane0.v_taps
						+ math_max2(math_ceil2(plane->composition.scaler_info.plane0.v_ratio, 1.0) - 2, 0.0));
		if (inputs->BytePerPixelC[k] == 0.0)
			outputs->MaximumSwathWidthInLineBufferChroma = 0;
		else
			outputs->MaximumSwathWidthInLineBufferChroma = lb_buffer_size_bits_chroma
					* math_max2(plane->composition.scaler_info.plane1.h_ratio, 1.0)
					/ 57
					/ (plane->composition.scaler_info.plane1.v_taps
							+ math_max2(math_ceil2(plane->composition.scaler_info.plane1.v_ratio, 1.0) - 2, 0.0));
		outputs->MaximumSwathWidthLuma[k] = math_min2(maximumSwathWidthSupportLuma, outputs->MaximumSwathWidthInLineBufferLuma);
		outputs->MaximumSwathWidthChroma[k] = math_min2(maximumSwathWidthSupportChroma, outputs->MaximumSwathWidthInLineBufferChroma);
		DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathWidthLuma=%f\n", __func__, k, outputs->MaximumSwathWidthLuma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathWidthSupportLuma=%u\n", __func__, k, maximumSwathWidthSupportLuma);
		DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathWidthInLineBufferLuma=%f\n", __func__, k, outputs->MaximumSwathWidthInLineBufferLuma);
		DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathWidthChroma=%f\n", __func__, k, outputs->MaximumSwathWidthChroma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathWidthSupportChroma=%u\n", __func__, k, maximumSwathWidthSupportChroma);
		DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathWidthInLineBufferChroma=%f\n", __func__, k, outputs->MaximumSwathWidthInLineBufferChroma);
	}

	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->MaximumSwathWidthLuma, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->MaximumSwathWidthChroma, display_cfg->num_planes);
	DML_LOG_DEBUG_DOUBLE(outputs->MaximumSwathWidthInLineBufferChroma);
	DML_LOG_DEBUG_DOUBLE(outputs->MaximumSwathWidthInLineBufferLuma);
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_cursor_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	/* Cursor Support Check */
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	const struct dml2_plane_parameters *plane;

	DML_LOG_FUNC_ENTER();
	outputs->support.CursorSupport = true;
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		if (plane->cursor.cursor_width > 0.0) {
			if (plane->cursor.cursor_bpp == 64
					&& ip->cursor_64bpp_support == false) {
				outputs->support.CursorSupport = false;
			}
		}
	}

	DML_LOG_DEBUG_BOOL(outputs->support.CursorSupport);
	DML_LOG_FUNC_EXIT();

	return outputs->support.CursorSupport;
}

static bool dcn6_ms_check_surface_alginment_requirements(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	const struct dml2_plane_parameters *plane;

	DML_LOG_FUNC_ENTER();
	outputs->support.PitchSupport = true;
	/* Valid Pitch Check */
	for (k = 0; k < display_cfg->num_planes; k++) {
		// data pitch

		plane = &display_cfg->plane_descriptors[k];
		unsigned int pixel_per_element = dml2_core_utils_is_422_packed(plane->pixel_format) ? 2 : 1;
		unsigned int alignment_l = inputs->MacroTileWidthY[k];
		if (inputs->surf_linear128_l[k])
			alignment_l = alignment_l / 2;
		DML_LOG_VERBOSE("DML::%s: alignment_l = %d\n", __func__, alignment_l);

		outputs->support.AlignedYPitch[k] = (unsigned int)math_ceil2(math_max2(plane->surface.plane0.pitch, plane->surface.plane0.width / pixel_per_element), alignment_l / pixel_per_element);
		if (dml2_core_utils_is_420(plane->pixel_format) || dml2_core_utils_is_422_planar(plane->pixel_format) || plane->pixel_format == dml2_rgbe_alpha) {
			unsigned int alignment_c = inputs->MacroTileWidthC[k];

			if (inputs->surf_linear128_c[k])
				alignment_c = alignment_c / 2;
			outputs->support.AlignedCPitch[k] = (unsigned int)math_ceil2(math_max2(plane->surface.plane1.pitch, plane->surface.plane1.width), alignment_c);
		} else {
			outputs->support.AlignedCPitch[k] = plane->surface.plane1.pitch;
		}

		if (outputs->support.AlignedYPitch[k] > plane->surface.plane0.pitch ||
			outputs->support.AlignedCPitch[k] > plane->surface.plane1.pitch) {
			outputs->support.PitchSupport = false;
			DML_LOG_VERBOSE("DML::%s: k=%u AlignedYPitch = %d\n", __func__, k, outputs->support.AlignedYPitch[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u PitchY = %ld\n", __func__, k, plane->surface.plane0.pitch);
			DML_LOG_VERBOSE("DML::%s: k=%u AlignedCPitch = %d\n", __func__, k, outputs->support.AlignedCPitch[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u PitchC = %ld\n", __func__, k, plane->surface.plane1.pitch);
			DML_LOG_VERBOSE("DML::%s: k=%u PitchSupport = %d\n", __func__, k, outputs->support.PitchSupport);
		}

		// meta pitch
		if (ip->dcn_mrq_present && plane->surface.dcc.enable) {
			outputs->support.AlignedDCCMetaPitchY[k] = (unsigned int)math_ceil2(math_max2(plane->surface.dcc.plane0.pitch,
					plane->surface.plane0.width), 64.0 * inputs->Read256BlockWidthY[k]);

			if (outputs->support.AlignedDCCMetaPitchY[k] > plane->surface.dcc.plane0.pitch)
				outputs->support.PitchSupport = false;

			if (dml2_core_utils_is_420(plane->pixel_format) || dml2_core_utils_is_422_planar(plane->pixel_format) || plane->pixel_format == dml2_rgbe_alpha) {
				outputs->support.AlignedDCCMetaPitchC[k] = (unsigned int)math_ceil2(math_max2(plane->surface.dcc.plane1.pitch,
						plane->surface.plane1.width), 64.0 * inputs->Read256BlockWidthC[k]);

				if (outputs->support.AlignedDCCMetaPitchC[k] > plane->surface.dcc.plane1.pitch)
					outputs->support.PitchSupport = false;
			}
		} else {
			outputs->support.AlignedDCCMetaPitchY[k] = 0;
			outputs->support.AlignedDCCMetaPitchC[k] = 0;
		}
	}

	DML_LOG_DEBUG_BOOL(outputs->support.PitchSupport);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->support.AlignedDCCMetaPitchY, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->support.AlignedDCCMetaPitchC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->support.AlignedYPitch, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->support.AlignedCPitch, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();

	return outputs->support.PitchSupport;
}

static void dcn6_ms_calculate_swath_and_det_configuration_for_single_dpp(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	/*
	 * FIXME - The whole point of this call seems to be to figure out SingleDPPViewportSizeSupportPerSurface, which
	 * if 0, means you need 2 DPPs.
	 */
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	struct dml2_core_calcs_mode_support_locals *dummies = ctx->dummies;
	struct dml2_core_calcs_CalculateSwathAndDETConfiguration_params *p =
			&ctx->func_params->CalculateSwathAndDETConfiguration_params;

	DML_LOG_FUNC_ENTER();
	p->display_cfg = display_cfg;
	p->ConfigReturnBufferSizeInKByte = ip->config_return_buffer_size_in_kbytes;
	p->MaxTotalDETInKByte = inputs->MaxTotalDETInKByte;
	p->MinCompressedBufferSizeInKByte = inputs->MinCompressedBufferSizeInKByte;
	p->rob_buffer_size_kbytes = ip->rob_buffer_size_kbytes;
	p->pixel_chunk_size_kbytes = ip->pixel_chunk_size_kbytes;
	p->rob_buffer_size_kbytes = ip->rob_buffer_size_kbytes;
	p->pixel_chunk_size_kbytes = ip->pixel_chunk_size_kbytes;
	p->ForceSingleDPP = 1;
	p->NumberOfActiveSurfaces = display_cfg->num_planes;
	p->nomDETInKByte = inputs->NomDETInKByte;
	p->ConfigReturnBufferSegmentSizeInkByte = ip->config_return_buffer_segment_size_in_kbytes;
	p->CompressedBufferSegmentSizeInkByte = ip->compressed_buffer_segment_size_in_kbytes;
	p->ReadBandwidthLuma = inputs->vactive_sw_bw_l;
	p->ReadBandwidthChroma = inputs->vactive_sw_bw_c;
	p->MaximumSwathWidthLuma = inputs->MaximumSwathWidthLuma;
	p->MaximumSwathWidthChroma = inputs->MaximumSwathWidthChroma;
	p->Read256BytesBlockHeightY = inputs->Read256BlockHeightY;
	p->Read256BytesBlockHeightC = inputs->Read256BlockHeightC;
	p->Read256BytesBlockWidthY = inputs->Read256BlockWidthY;
	p->Read256BytesBlockWidthC = inputs->Read256BlockWidthC;
	p->surf_linear128_l = inputs->surf_linear128_l;
	p->surf_linear128_c = inputs->surf_linear128_c;
	p->ODMMode = dummies->dummy_odm_mode;
	p->BytePerPixY = inputs->BytePerPixelY;
	p->BytePerPixC = inputs->BytePerPixelC;
	p->BytePerPixDETY = inputs->BytePerPixelInDETY;
	p->BytePerPixDETC = inputs->BytePerPixelInDETC;
	p->DPPPerSurface = dummies->dummy_integer_array[2];
	p->mrq_present = ip->dcn_mrq_present;
	// output
	p->req_per_swath_ub_l = dummies->dummy_integer_array[0];
	p->req_per_swath_ub_c = dummies->dummy_integer_array[1];
	p->swath_width_luma_ub = dummies->dummy_integer_array[3];
	p->swath_width_chroma_ub = dummies->dummy_integer_array[4];
	p->SwathWidth = dummies->dummy_integer_array[5];
	p->SwathWidthChroma = dummies->dummy_integer_array[6];
	p->SwathHeightY = dummies->dummy_integer_array[7];
	p->SwathHeightC = dummies->dummy_integer_array[8];
	p->request_size_bytes_luma = dummies->dummy_integer_array[26];
	p->request_size_bytes_chroma = dummies->dummy_integer_array[27];
	p->DETBufferSizeInKByte = dummies->dummy_integer_array[9];
	p->DETBufferSizeY = dummies->dummy_integer_array[10];
	p->DETBufferSizeC = dummies->dummy_integer_array[11];
	p->full_swath_bytes_l = dummies->dummy_integer_array[12];
	p->full_swath_bytes_c = dummies->dummy_integer_array[13];
	p->full_swath_bytes_single_dpp_l = dummies->dummy_integer_array[14];
	p->full_swath_bytes_single_dpp_c = dummies->dummy_integer_array[15];
	p->UnboundedRequestEnabled = &dummies->dummy_boolean[0];
	p->compbuf_reserved_space_64b = &dummies->dummy_integer[1];
	p->hw_debug5 = &dummies->dummy_boolean[2];
	p->CompressedBufferSizeInkByte = &dummies->dummy_integer[0];
	p->ViewportSizeSupportPerSurface = outputs->SingleDPPViewportSizeSupportPerSurface;
	p->ViewportSizeSupport = &dummies->dummy_boolean[1];
	// This calls is just to find out if there is enough DET space to support full vp in 1 pipe.
	dcn5_calculate_swath_and_det_configuration(ctx->func_params, p);

	DML_LOG_DEBUG_ARRAY_BOOL(outputs->SingleDPPViewportSizeSupportPerSurface, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();

}

static void dcn6_ms_calculate_estimated_num_of_dsc_slices(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_stream_parameters *stream;
	unsigned int k;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		/*Number Of DSC Slices*/
		if (stream->timing.dsc.enable == dml2_dsc_enable
				|| stream->timing.dsc.enable == dml2_dsc_enable_if_necessary) {
			if (stream->timing.dsc.overrides.num_slices != 0)
				outputs->EstimatedNumberOfDSCSlices[k] = stream->timing.dsc.overrides.num_slices;
			else {
				if (inputs->PixelClockBackEnd[k] > 7200) {
					outputs->EstimatedNumberOfDSCSlices[k] = 16;
				} else if (inputs->PixelClockBackEnd[k] > 3200) {
					outputs->EstimatedNumberOfDSCSlices[k] = 12;
				} else if (inputs->PixelClockBackEnd[k] > 1360) {
					outputs->EstimatedNumberOfDSCSlices[k] = 8;
				} else if (inputs->PixelClockBackEnd[k] > 680) {
					outputs->EstimatedNumberOfDSCSlices[k] = 4;
				} else if (inputs->PixelClockBackEnd[k] > 340) {
					outputs->EstimatedNumberOfDSCSlices[k] = 2;
				} else {
					outputs->EstimatedNumberOfDSCSlices[k] = 1;
				}
			}
		}
	}

	DML_LOG_DEBUG_ARRAY_UINT(outputs->EstimatedNumberOfDSCSlices, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_output_link(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	const struct dml2_stream_parameters *stream;
	unsigned int k;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		dcn5_calculate_output_link(
				ctx->func_params,
				((double) soc_bb->max_phyclk_khz / 1000),
				((double) soc_bb->max_phyclk_d18_khz / 1000),
				((double) soc_bb->max_phyclk_d32_khz / 1000),
				soc_bb->phy_downspread_percent,
				stream->output.output_encoder,
				stream->output.output_format,
				stream->timing.h_total,
				stream->timing.h_active,
				inputs->PixelClockBackEnd[k],
				inputs->DesiredOutputBpp[k],
				ip->maximum_dsc_bits_per_component,
				inputs->EstimatedNumberOfDSCSlices[k],
				stream->output.audio_sample_rate,
				stream->output.audio_sample_layout,
				stream->overrides.odm_mode,
				stream->overrides.odm_mode,
				stream->timing.dsc.enable,
				stream->output.output_dp_lane_count,
				stream->output.output_dp_link_rate,
				/* Output */
				&outputs->RequiresDSC[k],
				&outputs->RequiresFEC[k],
				&outputs->OutputBpp[k],
				&outputs->OutputType[k],
				&outputs->OutputRate[k],
				&outputs->RequiredSlots[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d RequiresDSC = %d\n", __func__, k, outputs->RequiresDSC[k]);
	}

	DML_LOG_DEBUG_ARRAY_UINT(outputs->RequiredSlots, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->OutputBpp, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_INT(outputs->OutputType, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_INT(outputs->OutputRate, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->RequiresDSC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->RequiresFEC, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_odm_mode(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_stream_parameters *stream;
	unsigned int k;
	unsigned int totalNumberOfActiveDPP = 0;
	unsigned int numOfDPP = 0;
	bool ODMSupport = true;

	DML_LOG_FUNC_ENTER();
	outputs->support.ODMSupport = true;
	for (k = 0; k < display_cfg->num_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];

		dcn5_calculate_odm_mode(
				ip->maximum_pixels_per_line_per_dsc_unit,
				stream->timing.h_active,
				stream->output.output_format,
				stream->output.output_encoder,
				stream->overrides.odm_mode,
				((double) soc_bb->max_dispclk_khz / 1000),
				inputs->RequiresDSC[k], // DSCEnable
				totalNumberOfActiveDPP,
				ip->max_num_dpp,
				((double) stream->timing.pixel_clock_khz / 1000),
				ip->maximum_dsc_slices_per_pipe,
				inputs->EstimatedNumberOfDSCSlices[k],
				ip->odm_combine_support_mask,

				/* Output */
				&ODMSupport,
				&numOfDPP,
				&outputs->ODMMode[k],
				&outputs->RequiredDISPCLKPerSurface[k]);

		totalNumberOfActiveDPP += numOfDPP;
		if (!ODMSupport)
			outputs->support.ODMSupport = false;
		DML_LOG_VERBOSE("DML::%s: k=%d ODMMode = %d\n", __func__, k, outputs->ODMMode[k]);
	}

	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->RequiredDISPCLKPerSurface, display_cfg->num_planes);
	DML_LOG_DEBUG_BOOL(outputs->support.ODMSupport);
	DML_LOG_DEBUG_ARRAY_INT(outputs->ODMMode, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_num_of_dsc_slices(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_stream_parameters *stream;
	unsigned int k;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		// ensure the number dsc slices is integer multiple based on ODM mode
		if (inputs->RequiresDSC[k]) {
			outputs->support.NumberOfDSCSlices[k] = inputs->EstimatedNumberOfDSCSlices[k];
			// fail a ms check if the override num_slices doesn't align with odm mode setting
			if (stream->timing.dsc.overrides.num_slices == 0) {
				// safe guard to ensure the dml derived dsc slices and odm setting are compatible
				if (inputs->ODMMode[k] == dml2_odm_mode_combine_2to1)
					outputs->support.NumberOfDSCSlices[k] = 2 * (unsigned int) math_ceil2(outputs->support.NumberOfDSCSlices[k] / 2.0, 1.0);
				else if (inputs->ODMMode[k] == dml2_odm_mode_combine_3to1)
					outputs->support.NumberOfDSCSlices[k] = 12;
				else if (inputs->ODMMode[k] == dml2_odm_mode_combine_4to1)
					outputs->support.NumberOfDSCSlices[k] = 4 * (unsigned int) math_ceil2(outputs->support.NumberOfDSCSlices[k] / 4.0, 1.0);
			}
		}
	}

	DML_LOG_DEBUG_ARRAY_UINT(outputs->support.NumberOfDSCSlices, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_num_of_dsc_slices_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	outputs->support.DSCSlicesODMModeSupported = true;
	for (unsigned int k = 0; k < ctx->display_cfg->num_planes; k++) {
		if (inputs->RequiresDSC[k]) {
			if (inputs->support.NumberOfDSCSlices[k] == 0)
				outputs->support.DSCSlicesODMModeSupported = false;

			if (inputs->ODMMode[k] == dml2_odm_mode_combine_2to1)
				outputs->support.DSCSlicesODMModeSupported =
						((inputs->support.NumberOfDSCSlices[k] % 2) == 0);
			else if (inputs->ODMMode[k] == dml2_odm_mode_combine_3to1)
				outputs->support.DSCSlicesODMModeSupported =
						(inputs->support.NumberOfDSCSlices[k] == 12);
			else if (inputs->ODMMode[k] == dml2_odm_mode_combine_4to1)
				outputs->support.DSCSlicesODMModeSupported =
						((inputs->support.NumberOfDSCSlices[k] % 4) == 0);
			if (!outputs->support.DSCSlicesODMModeSupported) {
				DML_LOG_VERBOSE("DML::%s: k=%d Invalid dsc num_slices and ODM mode setting\n", __func__, k);
				DML_LOG_VERBOSE("DML::%s: k=%d num_slices = %d\n", __func__, k, inputs->support.NumberOfDSCSlices[k]);
				DML_LOG_VERBOSE("DML::%s: k=%d ODMMode = %d\n", __func__, k, inputs->ODMMode[k]);
				break;
			}
		}
	}

	DML_LOG_DEBUG_BOOL(outputs->support.DSCSlicesODMModeSupported);
	DML_LOG_FUNC_EXIT();

	return outputs->support.DSCSlicesODMModeSupported;
}

static void dcn6_ms_calculate_num_of_dpp_required(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	unsigned int k;
	const struct dml2_plane_parameters *plane;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		outputs->NoOfDPP[k] = 1;
		if (inputs->ODMMode[k] == dml2_odm_mode_combine_4to1) {
			outputs->NoOfDPP[k] = 4;
		} else if (inputs->ODMMode[k] == dml2_odm_mode_combine_3to1) {
			outputs->NoOfDPP[k] = 3;
		} else if (inputs->ODMMode[k] == dml2_odm_mode_combine_2to1) {
			outputs->NoOfDPP[k] = 2;
		} else if (plane->overrides.mpcc_combine_factor == 2) {
			outputs->MPCCombine[k] = true;
			outputs->NoOfDPP[k] = 2;
		} else if (plane->overrides.mpcc_combine_factor == 1) {
			outputs->NoOfDPP[k] = 1;
			if (!inputs->SingleDPPViewportSizeSupportPerSurface[k]) {
				DML_LOG_VERBOSE("WARNING: DML::%s: MPCC is override to disable but viewport is too large to be supported with single pipe!\n", __func__);
			}
		} else {
			if ((inputs->MinDPPCLKUsingSingleDPP[k] > ((double) soc_bb->max_dppclk_khz / 1000))
					|| !inputs->SingleDPPViewportSizeSupportPerSurface[k]) {
				outputs->MPCCombine[k] = true;
				outputs->NoOfDPP[k] = 2;
			}
		}
		DML_LOG_VERBOSE("DML::%s: k=%d, NoOfDPP = %d\n", __func__, k,
			outputs->NoOfDPP[k]);
	}

	DML_LOG_DEBUG_ARRAY_UINT(outputs->NoOfDPP, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->MPCCombine, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_total_available_pipes_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_core_ip_params *ip = ctx->ip;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int totalNumOfActiveDPP = 0;
	unsigned int k;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++)
		totalNumOfActiveDPP += inputs->NoOfDPP[k];
	outputs->support.TotalAvailablePipesSupport = totalNumOfActiveDPP <= (unsigned int)ip->max_num_dpp;

	DML_LOG_DEBUG_BOOL(outputs->support.TotalAvailablePipesSupport);
	DML_LOG_FUNC_EXIT();

	return outputs->support.TotalAvailablePipesSupport;
}

static bool dcn6_ms_check_total_available_TDLUT_33cube_support(
	const struct dml2_core_calculate_ms_context *ctx,
	struct dml2_core_internal_mode_support *states)
{
	const struct dml2_core_ip_params *ip = ctx->ip;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int total_TDLUT_33cube = 0;
	unsigned int k;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++)
		if ((display_cfg->plane_descriptors[k].tdlut.tdlut_width_mode == dml2_tdlut_width_33_cube) && display_cfg->plane_descriptors[k].tdlut.setup_for_tdlut)
			total_TDLUT_33cube += inputs->NoOfDPP[k];
	outputs->support.NumberOfTDLUT33cubeSupport = total_TDLUT_33cube <= (unsigned int)ip->TDLUT_33cube_count;

	DML_LOG_DEBUG_BOOL(outputs->support.NumberOfTDLUT33cubeSupport);
	DML_LOG_FUNC_EXIT();

	return outputs->support.NumberOfTDLUT33cubeSupport;
}

static void dcn6_ms_calculate_total_num_of_single_dpp_surfaces(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < ctx->display_cfg->num_planes; k++) {
		if (inputs->NoOfDPP[k] == 1)
			outputs->TotalNumberOfSingleDPPSurfaces =
					outputs->TotalNumberOfSingleDPPSurfaces + 1;
	}

	DML_LOG_DEBUG_UINT(outputs->TotalNumberOfSingleDPPSurfaces);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_dispclk_and_dppclk_required(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	const struct dml2_clock_granularity_adjuster *clock_adjuster = ctx->clock_adjuster;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k, j;
	const struct dml2_stream_parameters *stream;
	double writeback_required_dispclk;

	DML_LOG_FUNC_ENTER();
	//DISPCLK/DPPCLK
	outputs->WritebackRequiredDISPCLK = 0;
	for (k = 0; k < display_cfg->num_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		for (j = 0; j < stream->writeback.active_writebacks_per_stream; ++j) {
			writeback_required_dispclk = dcn5_calculate_write_back_dispclk(
				stream->writeback.writeback_stream[j].pixel_format,
				((double)stream->timing.pixel_clock_khz / 1000),
				inputs->ODMMode[k],
				stream->writeback.writeback_stream[j].h_ratio,
				stream->writeback.writeback_stream[j].v_ratio,
				stream->writeback.writeback_stream[j].h_taps,
				stream->writeback.writeback_stream[j].v_taps,
				stream->writeback.writeback_stream[j].h_taps_chroma,
				stream->writeback.writeback_stream[j].v_taps_chroma,
				stream->writeback.writeback_stream[j].input_width,
				stream->writeback.writeback_stream[j].output_width,
				stream->timing.h_total,
				ip->writeback_line_buffer_buffer_size);
			outputs->WritebackRequiredDISPCLK = math_max2(
				outputs->WritebackRequiredDISPCLK,
				writeback_required_dispclk);
		}
	}

	outputs->RequiredDISPCLK = outputs->WritebackRequiredDISPCLK;
	for (k = 0; k < display_cfg->num_planes; k++) {
		outputs->RequiredDISPCLK = math_max2(
				outputs->RequiredDISPCLK,
				inputs->RequiredDISPCLKPerSurface[k]);
	}
	outputs->RequiredDISPCLK = clock_adjuster->adjust_dispclk_mhz(clock_adjuster, outputs->RequiredDISPCLK);

	for (k = 0; k < display_cfg->num_planes; k++)
		outputs->RequiredDPPCLK[k] = inputs->MinDPPCLKUsingSingleDPP[k] / inputs->NoOfDPP[k];
	clock_adjuster->adjust_dppclks_mhz(clock_adjuster, display_cfg->num_planes, outputs->RequiredDPPCLK,
			outputs->RequiredDPPCLK, &outputs->GlobalDPPCLK);

	DML_LOG_DEBUG_DOUBLE(outputs->GlobalDPPCLK);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->RequiredDPPCLK, display_cfg->num_planes);
	DML_LOG_DEBUG_DOUBLE(outputs->RequiredDISPCLK);
	DML_LOG_DEBUG_DOUBLE(outputs->WritebackRequiredDISPCLK);
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_dispclk_and_dppclk_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	outputs->support.DISPCLK_DPPCLK_Support = (inputs->RequiredDISPCLK <= ((double) soc_bb->max_dispclk_khz / 1000))
			&& (inputs->GlobalDPPCLK <= ((double) soc_bb->max_dppclk_khz / 1000));

	DML_LOG_DEBUG_BOOL(outputs->support.DISPCLK_DPPCLK_Support);
	DML_LOG_FUNC_EXIT();

	return outputs->support.DISPCLK_DPPCLK_Support;
}

static bool dcn6_ms_check_otg_count_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int TotalNumberOfActiveOTG = 0;
	unsigned int stream_visited_bit_map = 0;

	DML_LOG_FUNC_ENTER();
	for (unsigned int k = 0; k < display_cfg->num_planes; k++) {
		/* Check if stream has been visited */
		if (stream_visited_bit_map & (1 << display_cfg->plane_descriptors[k].stream_index))
			continue;
		/* Mark stream as visited */
		stream_visited_bit_map |= (1 << display_cfg->plane_descriptors[k].stream_index);

		TotalNumberOfActiveOTG = TotalNumberOfActiveOTG + 1;
	}
	outputs->support.NumberOfOTGSupport = TotalNumberOfActiveOTG <= ip->max_num_otg;

	DML_LOG_DEBUG_BOOL(outputs->support.NumberOfOTGSupport);
	DML_LOG_FUNC_EXIT();

	return outputs->support.NumberOfOTGSupport;
}

static bool dcn6_ms_check_hpo_frl_encoder_count_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int stream_visited_bit_map = 0;
	unsigned int totalNumberOfActiveHDMIFRL = 0;

	DML_LOG_FUNC_ENTER();
	for (unsigned int k = 0; k < display_cfg->num_planes; k++) {
		/* Check if stream has been visited */
		if (stream_visited_bit_map & (1 << display_cfg->plane_descriptors[k].stream_index))
			continue;
		/* Mark stream as visited */
		stream_visited_bit_map |= (1 << display_cfg->plane_descriptors[k].stream_index);

		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_hdmifrl)
			totalNumberOfActiveHDMIFRL++;
	}
	outputs->support.NumberOfHDMIFRLSupport = totalNumberOfActiveHDMIFRL <= ip->max_num_hdmi_frl_outputs;

	DML_LOG_DEBUG_BOOL(outputs->support.NumberOfHDMIFRLSupport);
	DML_LOG_FUNC_EXIT();

	return outputs->support.NumberOfHDMIFRLSupport;
}

static bool dcn6_ms_check_hpo_dp_encoder_count_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int stream_visited_bit_map = 0;
	unsigned int totalNumberOfActiveDP2p0 = 0;
	unsigned int totalNumberOfActiveDP2p0Outputs = 0;

	DML_LOG_FUNC_ENTER();
	for (unsigned int k = 0; k < display_cfg->num_planes; k++) {
		/* Check if stream has been visited */
		if (stream_visited_bit_map & (1 << display_cfg->plane_descriptors[k].stream_index))
			continue;
		/* Mark stream as visited */
		stream_visited_bit_map |= (1 << display_cfg->plane_descriptors[k].stream_index);

		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_dp2p0) {
			totalNumberOfActiveDP2p0++;
			// FIXME_STAGE2: SW not using backend related stuff, need mapping for mst setup
			//if (display_cfg->output.OutputMultistreamId[k] == k || display_cfg->output.OutputMultistreamEn[k] == false) {
			totalNumberOfActiveDP2p0Outputs++;
		}
	}
	outputs->support.NumberOfDP2p0Support = (totalNumberOfActiveDP2p0 <= ip->max_num_dp2p0_streams)
			&& (totalNumberOfActiveDP2p0Outputs <= ip->max_num_dp2p0_outputs);

	DML_LOG_DEBUG_BOOL(outputs->support.NumberOfDP2p0Support);
	DML_LOG_FUNC_EXIT();

	return outputs->support.NumberOfDP2p0Support;
}

static bool dcn6_ms_check_writeback_count_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int stream_visited_bit_map = 0;
	unsigned int totalNumberOfActiveWriteback = 0;
	bool writeback_per_stream_supported = true;

	DML_LOG_FUNC_ENTER();
	for (unsigned int k = 0; k < display_cfg->num_planes; k++) {
		/* Check if stream has been visited */
		if (stream_visited_bit_map & (1 << display_cfg->plane_descriptors[k].stream_index))
			continue;
		/* Mark stream as visited */
		stream_visited_bit_map |= (1 << display_cfg->plane_descriptors[k].stream_index);

		totalNumberOfActiveWriteback +=
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream;

		/* >1 writeback per stream is currently not supported */
		if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream > 1)
			writeback_per_stream_supported = false;
	}
	outputs->support.EnoughWritebackUnits = writeback_per_stream_supported &&
			totalNumberOfActiveWriteback <= ip->max_num_wb;

	DML_LOG_DEBUG_BOOL(outputs->support.EnoughWritebackUnits);
	DML_LOG_FUNC_EXIT();

	return outputs->support.EnoughWritebackUnits;
}

static bool dcn6_ms_check_link_bandwidth_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	unsigned int k;
	const struct dml2_stream_parameters *stream;

	DML_LOG_FUNC_ENTER();
	outputs->support.LinkCapacitySupport = true;

	for (k = 0; k < display_cfg->num_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		if (!dml2_core_utils_is_stream_encoder_required(stream)
				|| stream->output.output_disabled || !stream->output.validate_output)
			continue;

		outputs->support.LinkCapacitySupport &= (inputs->OutputBpp[k] > 0);
	}

	DML_LOG_DEBUG_BOOL(outputs->support.LinkCapacitySupport);
	DML_LOG_FUNC_EXIT();

	return outputs->support.LinkCapacitySupport;
}

static void dcn6_ms_check_misc_link_supports(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	const struct dml2_stream_parameters *stream;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];

		if (!dml2_core_utils_is_stream_encoder_required(stream))
			continue;

		if (stream->output.output_format == dml2_420
				&& stream->timing.interlaced
				&& ip->ptoi_supported)
			outputs->support.P2IWith420 = true;

		if (stream->timing.dsc.enable == dml2_dsc_enable
				|| stream->timing.dsc.enable == dml2_dsc_enable_if_necessary) {
			if (stream->output.output_format == dml2_n422
				&& !ip->dsc422_native_support)
				outputs->support.DSC422NativeNotSupported = true;
		}

		if (dml2_core_utils_is_dp_8b_10b_link_rate(stream->output.output_dp_link_rate)
				&& !dml2_core_utils_is_dio_dp_encoder(stream))
			outputs->support.LinkRateDoesNotMatchDPVersion = true;
		else if (dml2_core_utils_is_dp_128b_132b_link_rate(stream->output.output_dp_link_rate)
				&& !dml2_core_utils_is_hpo_dp_encoder(stream))
			outputs->support.LinkRateDoesNotMatchDPVersion = true;

		if (dml2_core_utils_is_odm_split(stream->overrides.odm_mode)
				&& !dml2_core_utils_is_dp_encoder(stream))
			outputs->support.MSOOrODMSplitWithNonDPLink = true;

		if (stream->overrides.odm_mode == dml2_odm_mode_mso_1to2
				&& stream->output.output_dp_lane_count < 2)
			outputs->support.NotEnoughLanesForMSO = true;
		else if (stream->overrides.odm_mode == dml2_odm_mode_mso_1to4
				&& stream->output.output_dp_lane_count < 4)
			outputs->support.NotEnoughLanesForMSO = true;
	}

	DML_LOG_DEBUG_BOOL(outputs->support.P2IWith420);
	DML_LOG_DEBUG_BOOL(outputs->support.NotEnoughLanesForMSO);
	DML_LOG_DEBUG_BOOL(outputs->support.DSC422NativeNotSupported);
	DML_LOG_DEBUG_BOOL(outputs->support.MSOOrODMSplitWithNonDPLink);
	DML_LOG_DEBUG_BOOL(outputs->support.LinkRateDoesNotMatchDPVersion);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_dtbclk_required(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_clock_granularity_adjuster *clock_adjuster = ctx->clock_adjuster;
	unsigned int k;
	const struct dml2_stream_parameters *stream;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		if (stream->output.output_encoder == dml2_hdmifrl) {
			outputs->RequiredDTBCLK[k] = dcn5_calculate_required_dtbclk(
				inputs->RequiresDSC[k],
				inputs->PixelClockBackEnd[k],
				stream->output.output_format,
				inputs->OutputBpp[k],
				inputs->support.NumberOfDSCSlices[k],
				stream->timing.h_total,
				stream->timing.h_active,
				stream->output.audio_sample_rate,
				stream->output.audio_sample_layout);
		}
	}

	clock_adjuster->adjust_dtbclks_mhz(clock_adjuster, display_cfg->num_planes, outputs->RequiredDTBCLK,
			outputs->RequiredDTBCLK, &outputs->GlobalDTBCLK);

	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->RequiredDTBCLK, display_cfg->num_planes);
	DML_LOG_DEBUG_DOUBLE(outputs->GlobalDTBCLK);
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_dtbclk_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	bool support = true;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++)
		if (inputs->RequiredDTBCLK[k] > ((double)soc_bb->max_dtbclk_khz / 1000))
			support = false;

	outputs->support.DTBCLKRequiredMoreThanSupported = !support;

	DML_LOG_DEBUG_BOOL(outputs->support.DTBCLKRequiredMoreThanSupported);
	DML_LOG_FUNC_EXIT();

	return support;
}

static void dcn6_ms_calculate_dscclk_required(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	unsigned int k;
	const struct dml2_stream_parameters *stream;
	unsigned int DSCFormatFactor;
	double pixelClockBackEndFactor;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		if (!dml2_core_utils_is_encoder_dsc_capable(stream))
			continue;

		if (stream->output.output_format == dml2_420
				|| stream->output.output_format == dml2_n422)
			DSCFormatFactor = 2;
		else
			DSCFormatFactor = 1;

		DML_LOG_VERBOSE("DML::%s: k=%u, RequiresDSC = %u\n", __func__, k, inputs->RequiresDSC[k]);
		if (!inputs->RequiresDSC[k])
			continue;

		unsigned int num_dsc_units;

		if (inputs->ODMMode[k] == dml2_odm_mode_combine_4to1)
			num_dsc_units = 4;
		else if (inputs->ODMMode[k] == dml2_odm_mode_combine_3to1)
			num_dsc_units = 3;
		else if (inputs->ODMMode[k] == dml2_odm_mode_combine_2to1)
			num_dsc_units = 2;
		else
			num_dsc_units = 1;

		pixelClockBackEndFactor = 3.0 * num_dsc_units;

		if (inputs->support.NumberOfDSCSlices[k] > num_dsc_units)
			pixelClockBackEndFactor *= 2;

		outputs->required_dscclk_freq_mhz[k] = inputs->PixelClockBackEnd[k] / pixelClockBackEndFactor / (double) DSCFormatFactor;
		DML_LOG_VERBOSE("DML::%s: k=%u, PixelClockBackEnd = %f\n", __func__, k, inputs->PixelClockBackEnd[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, required_dscclk_freq_mhz = %f\n", __func__, k, outputs->required_dscclk_freq_mhz[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, DSCFormatFactor = %u\n", __func__, k, DSCFormatFactor);
	}

	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->required_dscclk_freq_mhz, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_dscclk_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++)
		if (inputs->required_dscclk_freq_mhz[k] > (double) soc_bb->max_dscclk_khz / 1000) {
			outputs->support.DSCCLKRequiredMoreThanSupported = true;
			DML_LOG_VERBOSE("DML::%s: k=%u, DSCCLKRequiredMoreThanSupported = %u\n",
					__func__, k, outputs->support.DSCCLKRequiredMoreThanSupported);
			break;
		}

	DML_LOG_DEBUG_BOOL(outputs->support.DSCCLKRequiredMoreThanSupported);
	DML_LOG_FUNC_EXIT();

	return !outputs->support.DSCCLKRequiredMoreThanSupported;
}

static void dcn6_ms_check_dsc_engine_supports(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	unsigned int k;
	const struct dml2_stream_parameters *stream;
	unsigned int totalDSCUnitsRequired = 0;
	unsigned int numDSCUnitRequired;
	unsigned int stream_visited_bit_map = 0;
	unsigned int stream_index;

	DML_LOG_FUNC_ENTER();
	outputs->support.PixelsPerLinePerDSCUnitSupport = true;
	for (k = 0; k < display_cfg->num_planes; k++) {
		stream_index = display_cfg->plane_descriptors[k].stream_index;

		/* Check if stream has been visited */
		if (stream_visited_bit_map & (1 << stream_index))
			continue;

		/* Mark stream as visited */
		stream_visited_bit_map |= (1 << stream_index);

		if (!inputs->RequiresDSC[k])
			continue;

		if (inputs->ODMMode[k] == dml2_odm_mode_combine_4to1)
			numDSCUnitRequired = 4;
		else if (inputs->ODMMode[k] == dml2_odm_mode_combine_3to1)
			numDSCUnitRequired = 3;
		else if (inputs->ODMMode[k] == dml2_odm_mode_combine_2to1)
			numDSCUnitRequired = 2;
		else
			numDSCUnitRequired = 1;

		stream = &display_cfg->stream_descriptors[stream_index];

		if (stream->timing.h_active > numDSCUnitRequired * ip->maximum_pixels_per_line_per_dsc_unit)
			outputs->support.PixelsPerLinePerDSCUnitSupport = false;
		totalDSCUnitsRequired += numDSCUnitRequired;

		if (inputs->support.NumberOfDSCSlices[k] > numDSCUnitRequired * ip->maximum_dsc_slices_per_pipe)
			outputs->support.NotEnoughDSCSlices = true;
	}

	if (totalDSCUnitsRequired > ip->num_dsc)
		outputs->support.NotEnoughDSCUnits = true;

	DML_LOG_DEBUG_BOOL(outputs->support.NotEnoughDSCUnits);
	DML_LOG_DEBUG_BOOL(outputs->support.NotEnoughDSCSlices);
	DML_LOG_DEBUG_BOOL(outputs->support.PixelsPerLinePerDSCUnitSupport);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_dsc_delay(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	const struct dml2_stream_parameters *stream;

	DML_LOG_FUNC_ENTER();
	/*DSC Delay per state*/
	for (k = 0; k < display_cfg->num_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		outputs->DSCDelay[k] = dcn5_calculate_dsc_delay_requirement(
				inputs->RequiresDSC[k],
				inputs->ODMMode[k],
				ip->maximum_dsc_bits_per_component,
				inputs->DesiredOutputBpp[k] > 0 ? inputs->DesiredOutputBpp[k] : inputs->OutputBpp[k],
				stream->timing.h_active,
				stream->timing.h_total,
				inputs->support.NumberOfDSCSlices[k],
				stream->output.output_format,
				stream->output.output_encoder,
				((double) stream->timing.pixel_clock_khz / 1000),
				inputs->PixelClockBackEnd[k],
				inputs->use_legacy_dsc_delay_formula);
	}

	DML_LOG_DEBUG_ARRAY_UINT(outputs->DSCDelay, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_swath_and_det_configuration(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_calcs_mode_support_locals *dummies = ctx->dummies;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	struct dml2_core_calcs_CalculateSwathAndDETConfiguration_params *p =
			&ctx->func_params->CalculateSwathAndDETConfiguration_params;

	DML_LOG_FUNC_ENTER();
	// Figure out the swath and DET configuration after the num dpp per plane is figured out
	p->display_cfg = display_cfg;
	p->ConfigReturnBufferSizeInKByte = ip->config_return_buffer_size_in_kbytes;
	p->MaxTotalDETInKByte = inputs->MaxTotalDETInKByte;
	p->MinCompressedBufferSizeInKByte = inputs->MinCompressedBufferSizeInKByte;
	p->rob_buffer_size_kbytes = ip->rob_buffer_size_kbytes;
	p->pixel_chunk_size_kbytes = ip->pixel_chunk_size_kbytes;
	p->rob_buffer_size_kbytes = ip->rob_buffer_size_kbytes;
	p->pixel_chunk_size_kbytes = ip->pixel_chunk_size_kbytes;
	p->ForceSingleDPP = false;
	p->NumberOfActiveSurfaces = display_cfg->num_planes;
	p->nomDETInKByte = inputs->NomDETInKByte;
	p->ConfigReturnBufferSegmentSizeInkByte = ip->config_return_buffer_segment_size_in_kbytes;
	p->CompressedBufferSegmentSizeInkByte = ip->compressed_buffer_segment_size_in_kbytes;
	p->ReadBandwidthLuma = inputs->vactive_sw_bw_l;
	p->ReadBandwidthChroma = inputs->vactive_sw_bw_c;
	p->MaximumSwathWidthLuma = inputs->MaximumSwathWidthLuma;
	p->MaximumSwathWidthChroma = inputs->MaximumSwathWidthChroma;
	p->Read256BytesBlockHeightY = inputs->Read256BlockHeightY;
	p->Read256BytesBlockHeightC = inputs->Read256BlockHeightC;
	p->Read256BytesBlockWidthY = inputs->Read256BlockWidthY;
	p->Read256BytesBlockWidthC = inputs->Read256BlockWidthC;
	p->surf_linear128_l = inputs->surf_linear128_l;
	p->surf_linear128_c = inputs->surf_linear128_c;
	p->ODMMode = inputs->ODMMode;
	p->DPPPerSurface = inputs->NoOfDPP;
	p->BytePerPixY = inputs->BytePerPixelY;
	p->BytePerPixC = inputs->BytePerPixelC;
	p->BytePerPixDETY = inputs->BytePerPixelInDETY;
	p->BytePerPixDETC = inputs->BytePerPixelInDETC;
	p->mrq_present = ip->dcn_mrq_present;
	// output
	p->req_per_swath_ub_l = outputs->req_per_swath_ub_l;
	p->req_per_swath_ub_c = outputs->req_per_swath_ub_c;
	p->swath_width_luma_ub = outputs->swath_width_luma_ub;
	p->swath_width_chroma_ub = outputs->swath_width_chroma_ub;
	p->SwathWidth = outputs->SwathWidthY;
	p->SwathWidthChroma = outputs->SwathWidthC;
	p->SwathHeightY = outputs->SwathHeightY;
	p->SwathHeightC = outputs->SwathHeightC;
	p->request_size_bytes_luma = outputs->support.request_size_bytes_luma;
	p->request_size_bytes_chroma = outputs->support.request_size_bytes_chroma;
	p->DETBufferSizeInKByte = outputs->DETBufferSizeInKByte; // FIXME: This is per pipe but the pipes in plane will use that
	p->DETBufferSizeY = outputs->DETBufferSizeY;
	p->DETBufferSizeC = outputs->DETBufferSizeC;
	p->full_swath_bytes_l = dummies->dummy_integer_array[2];
	p->full_swath_bytes_c = dummies->dummy_integer_array[3];
	p->full_swath_bytes_single_dpp_l = outputs->full_swath_bytes_l;
	p->full_swath_bytes_single_dpp_c = outputs->full_swath_bytes_c;
	p->UnboundedRequestEnabled = &outputs->UnboundedRequestEnabled;
	p->compbuf_reserved_space_64b = &outputs->compbuf_reserved_space_64b;
	p->hw_debug5 = &outputs->hw_debug5;
	p->CompressedBufferSizeInkByte = &outputs->CompressedBufferSizeInkByte;
	p->ViewportSizeSupportPerSurface = dummies->dummy_boolean_array[0];
	p->ViewportSizeSupport = &outputs->support.ViewportSizeSupport;
	dcn5_calculate_swath_and_det_configuration(ctx->func_params, p);

	DML_LOG_DEBUG_ARRAY_UINT(outputs->SwathWidthY, display_cfg->num_planes);
	DML_LOG_DEBUG_UINT(outputs->CompressedBufferSizeInkByte);
	DML_LOG_DEBUG_BOOL(outputs->UnboundedRequestEnabled);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->SwathHeightY, display_cfg->num_planes);
	DML_LOG_DEBUG_BOOL(outputs->support.ViewportSizeSupport);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->SwathWidthC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->DETBufferSizeC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->support.request_size_bytes_chroma, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->swath_width_luma_ub, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->swath_width_chroma_ub, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->SwathHeightC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->DETBufferSizeInKByte, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->DETBufferSizeY, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->support.request_size_bytes_luma, display_cfg->num_planes);
	DML_LOG_DEBUG_UINT(outputs->compbuf_reserved_space_64b);
	DML_LOG_DEBUG_BOOL(outputs->hw_debug5);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->req_per_swath_ub_l, display_cfg->num_planes);

	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_total_num_of_dcc_active_dpp(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	unsigned int k;
	const struct dml2_plane_parameters *plane;

	DML_LOG_FUNC_ENTER();
	outputs->TotalNumberOfDCCActiveDPP = 0;
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		if (plane->surface.dcc.enable == true) {
			outputs->TotalNumberOfDCCActiveDPP =
					outputs->TotalNumberOfDCCActiveDPP + inputs->NoOfDPP[k];
		}
	}

	DML_LOG_DEBUG_UINT(outputs->TotalNumberOfDCCActiveDPP);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_vm_row_and_swath_and_calculate_dcc_meta_cache_requirements(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_calcs_mode_support_locals *dummies = ctx->dummies;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	struct dml2_core_calcs_CalculateVMRowAndSwath_params *p;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;
	struct dml2_core_internal_DmlPipe *surfParameters;

	DML_LOG_FUNC_ENTER();
	memset(dummies->dummy_integer_array[0], 0, sizeof(dummies->dummy_integer_array[0]));

	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];
		surfParameters = &dummies->SurfParameters[k];

		surfParameters->PixelClock = ((double) stream->timing.pixel_clock_khz / 1000);
		surfParameters->DPPPerSurface = inputs->NoOfDPP[k];
		surfParameters->RotationAngle = plane->composition.rotation_angle;
		surfParameters->ViewportHeight = plane->composition.viewport.plane0.height;
		surfParameters->ViewportHeightC = plane->composition.viewport.plane1.height;
		surfParameters->BlockWidth256BytesY = inputs->Read256BlockWidthY[k];
		surfParameters->BlockHeight256BytesY = inputs->Read256BlockHeightY[k];
		surfParameters->BlockWidth256BytesC = inputs->Read256BlockWidthC[k];
		surfParameters->BlockHeight256BytesC = inputs->Read256BlockHeightC[k];
		surfParameters->BlockWidthY = inputs->MacroTileWidthY[k];
		surfParameters->BlockHeightY = inputs->MacroTileHeightY[k];
		surfParameters->BlockWidthC = inputs->MacroTileWidthC[k];
		surfParameters->BlockHeightC = inputs->MacroTileHeightC[k];
		surfParameters->InterlaceEnable = stream->timing.interlaced;
		surfParameters->HTotal = stream->timing.h_total;
		surfParameters->DCCEnable = plane->surface.dcc.enable;
		surfParameters->SourcePixelFormat = plane->pixel_format;
		surfParameters->SurfaceTiling = plane->surface.tiling;
		surfParameters->BytePerPixelY = inputs->BytePerPixelY[k];
		surfParameters->BytePerPixelC = inputs->BytePerPixelC[k];
		surfParameters->ProgressiveToInterlaceUnitInOPP = ip->ptoi_supported;
		surfParameters->VRatio = plane->composition.scaler_info.plane0.v_ratio;
		surfParameters->VRatioChroma = plane->composition.scaler_info.plane1.v_ratio;
		surfParameters->VTaps = plane->composition.scaler_info.plane0.v_taps;
		surfParameters->VTapsChroma = plane->composition.scaler_info.plane1.v_taps;
		surfParameters->PitchY = plane->surface.plane0.pitch;
		surfParameters->PitchC = plane->surface.plane1.pitch;
		surfParameters->ViewportStationary = plane->composition.viewport.stationary;
		surfParameters->ViewportXStart = plane->composition.viewport.plane0.x_start;
		surfParameters->ViewportYStart = plane->composition.viewport.plane0.y_start;
		surfParameters->ViewportXStartC = plane->composition.viewport.plane1.y_start;
		surfParameters->ViewportYStartC = plane->composition.viewport.plane1.y_start;
		surfParameters->FORCE_ONE_ROW_FOR_FRAME = plane->overrides.hw.force_one_row_for_frame;
		surfParameters->SwathHeightY = inputs->SwathHeightY[k];
		surfParameters->SwathHeightC = inputs->SwathHeightC[k];
		surfParameters->DCCMetaPitchY = plane->surface.dcc.plane0.pitch;
		surfParameters->DCCMetaPitchC = plane->surface.dcc.plane1.pitch;
		surfParameters->UPSPEnabled = plane->composition.scaler_info.upsp_enabled;
		surfParameters->UPSPVTaps = plane->composition.scaler_info.upsp_vtaps;
		surfParameters->UPSPSamplePositioning = plane->composition.scaler_info.upsp_sample_positioning;
	}

	p = &ctx->func_params->CalculateVMRowAndSwath_params;
	p->display_cfg = display_cfg;
	p->uclk_pstate_switch_modes = inputs->uclk_pstate_switch_modes;
	p->NumberOfActiveSurfaces = display_cfg->num_planes;
	p->myPipe = dummies->SurfParameters;
	p->PTEBufferSizeInRequestsLuma = ip->dpte_buffer_size_in_pte_reqs_luma;
	p->PTEBufferSizeInRequestsChroma = ip->dpte_buffer_size_in_pte_reqs_chroma;
	p->SwathWidthY = inputs->SwathWidthY;
	p->SwathWidthC = inputs->SwathWidthC;
	p->DCCMetaBufferSizeBytes = ip->dcc_meta_buffer_size_bytes;
	p->mrq_present = ip->dcn_mrq_present;
	// output
	p->PTEBufferSizeNotExceeded = outputs->PTEBufferSizeNotExceeded;
	p->dpte_row_width_luma_ub = outputs->dpte_row_width_luma_ub;
	p->dpte_row_width_chroma_ub = outputs->dpte_row_width_chroma_ub;
	p->dpte_row_height_luma = outputs->dpte_row_height;
	p->dpte_row_height_chroma = outputs->dpte_row_height_chroma;
	p->dpte_row_height_linear_luma = outputs->dpte_row_height_linear; // VBA_DELTA
	p->dpte_row_height_linear_chroma = outputs->dpte_row_height_linear_chroma; // VBA_DELTA
	p->vm_group_bytes = outputs->vm_group_bytes;
	p->dpte_group_bytes = outputs->dpte_group_bytes;
	p->PixelPTEReqWidthY = outputs->PixelPTEReqWidthY;
	p->PixelPTEReqHeightY = outputs->PixelPTEReqHeightY;
	p->PTERequestSizeY = outputs->PTERequestSizeY;
	p->PixelPTEReqWidthC = outputs->PixelPTEReqWidthC;
	p->PixelPTEReqHeightC = outputs->PixelPTEReqHeightC;
	p->PTERequestSizeC = outputs->PTERequestSizeC;
	p->vmpg_width_y = outputs->vmpg_width_y;
	p->vmpg_height_y = outputs->vmpg_height_y;
	p->vmpg_width_c = outputs->vmpg_width_c;
	p->vmpg_height_c = outputs->vmpg_height_c;
	p->dpde0_bytes_per_frame_ub_l = outputs->dpde0_bytes_per_frame_ub_l;
	p->dpde0_bytes_per_frame_ub_c = outputs->dpde0_bytes_per_frame_ub_c;
	p->PrefetchSourceLinesY = outputs->PrefetchLinesY;
	p->PrefetchSourceLinesC = outputs->PrefetchLinesC;
	p->VInitPreFillY = outputs->PrefillY;
	p->VInitPreFillC = outputs->PrefillC;
	p->MaxNumSwathY = outputs->MaxNumSwathY;
	p->MaxNumSwathC = outputs->MaxNumSwathC;
	p->dpte_row_bw = outputs->dpte_row_bw;
	p->PixelPTEBytesPerRow = outputs->DPTEBytesPerRow;
	p->dpte_row_bytes_per_row_l = outputs->dpte_row_bytes_per_row_l;
	p->dpte_row_bytes_per_row_c = outputs->dpte_row_bytes_per_row_c;
	p->vm_bytes = outputs->vm_bytes;
	p->use_one_row_for_frame = outputs->use_one_row_for_frame;
	p->use_one_row_for_frame_flip = outputs->use_one_row_for_frame_flip;
	p->PTE_BUFFER_MODE = outputs->PTE_BUFFER_MODE;
	p->BIGK_FRAGMENT_SIZE = outputs->BIGK_FRAGMENT_SIZE;
	p->DCCMetaBufferSizeNotExceeded = outputs->DCCMetaBufferSizeNotExceeded;
	p->meta_row_bw = outputs->meta_row_bw;
	p->meta_row_bytes = outputs->meta_row_bytes;
	p->meta_row_bytes_per_row_ub_l = outputs->meta_row_bytes_per_row_ub_l;
	p->meta_row_bytes_per_row_ub_c = outputs->meta_row_bytes_per_row_ub_c;
	p->meta_req_width_luma = outputs->meta_req_width;
	p->meta_req_height_luma = outputs->meta_req_height;
	p->meta_row_width_luma = outputs->meta_row_width;
	p->meta_row_height_luma = outputs->meta_row_height_luma;
	p->meta_pte_bytes_per_frame_ub_l = outputs->meta_pte_bytes_per_frame_ub_l;
	p->meta_req_width_chroma = outputs->meta_req_width_chroma;
	p->meta_req_height_chroma = outputs->meta_req_height_chroma;
	p->meta_row_width_chroma = outputs->meta_row_width_chroma;
	p->meta_row_height_chroma = outputs->meta_row_height_chroma;
	p->meta_pte_bytes_per_frame_ub_c = outputs->meta_pte_bytes_per_frame_ub_c;
	dcn5_calculate_vm_row_and_swath(ctx->func_params, p);

	DML_LOG_DEBUG_ARRAY_BOOL(outputs->PTEBufferSizeNotExceeded, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->dpte_row_width_luma_ub, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->dpte_row_width_chroma_ub, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->dpte_row_height, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->dpte_row_height_chroma, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->dpte_row_height_linear, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->dpte_row_height_linear_chroma, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->vm_group_bytes, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->dpte_group_bytes, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->PixelPTEReqWidthY, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->PixelPTEReqHeightY, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->PTERequestSizeY, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->PixelPTEReqWidthC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->PixelPTEReqHeightC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->PTERequestSizeC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->vmpg_width_y, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->vmpg_height_y, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->vmpg_width_c, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->vmpg_height_c, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->dpde0_bytes_per_frame_ub_l, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->dpde0_bytes_per_frame_ub_c, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->PrefetchLinesY, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->PrefetchLinesC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->PrefillY, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->PrefillC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->MaxNumSwathY, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->MaxNumSwathC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->dpte_row_bw, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->DPTEBytesPerRow, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->dpte_row_bytes_per_row_l, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->dpte_row_bytes_per_row_c, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->vm_bytes, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->use_one_row_for_frame, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->use_one_row_for_frame_flip, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->is_using_mall_for_ss, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->PTE_BUFFER_MODE, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->BIGK_FRAGMENT_SIZE, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->DCCMetaBufferSizeNotExceeded, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->meta_row_bw, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->meta_row_bytes, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->meta_row_bytes_per_row_ub_l, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->meta_row_bytes_per_row_ub_c, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->meta_req_width, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->meta_req_height, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->meta_row_width, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->meta_row_height_luma, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->meta_pte_bytes_per_frame_ub_l, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->meta_req_width_chroma, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->meta_req_height_chroma, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->meta_row_width_chroma, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->meta_row_height_chroma, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->meta_pte_bytes_per_frame_ub_c, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_pte_buffer_size_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;

	DML_LOG_FUNC_ENTER();
	outputs->support.PTEBufferSizeNotExceeded = true;
	for (k = 0; k < display_cfg->num_planes; k++) {
		if (inputs->PTEBufferSizeNotExceeded[k] == false)
			outputs->support.PTEBufferSizeNotExceeded = false;

		DML_LOG_VERBOSE("DML::%s: k=%u, PTEBufferSizeNotExceeded = %u\n", __func__, k,
			inputs->PTEBufferSizeNotExceeded[k]);
	}
	DML_LOG_VERBOSE("DML::%s: PTEBufferSizeNotExceeded = %u\n", __func__, outputs->support.PTEBufferSizeNotExceeded);
	DML_LOG_DEBUG_BOOL(outputs->support.PTEBufferSizeNotExceeded);
	DML_LOG_FUNC_EXIT();

	return outputs->support.PTEBufferSizeNotExceeded;
}

static bool dcn6_ms_check_dcc_meta_cache_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;

	DML_LOG_FUNC_ENTER();
	outputs->support.DCCMetaBufferSizeNotExceeded = true;
	for (k = 0; k < display_cfg->num_planes; k++) {
		if (inputs->DCCMetaBufferSizeNotExceeded[k] == false)
			outputs->support.DCCMetaBufferSizeNotExceeded = false;

		DML_LOG_VERBOSE("DML::%s: k=%u, DCCMetaBufferSizeNotExceeded = %u\n", __func__, k,
			inputs->DCCMetaBufferSizeNotExceeded[k]);
	}
	DML_LOG_VERBOSE("DML::%s: DCCMetaBufferSizeNotExceeded = %u\n", __func__,
			outputs->support.DCCMetaBufferSizeNotExceeded);

	DML_LOG_DEBUG_BOOL(outputs->support.DCCMetaBufferSizeNotExceeded);
	DML_LOG_FUNC_EXIT();

	return outputs->support.DCCMetaBufferSizeNotExceeded;
}

static double dcn6_ms_pstate_type_to_blackout_us(
		const struct dml2_core_calculate_ms_context *ctx,
		enum dml2_pstate_type pstate_type)
{
	double blackout_us = 0.0;

	switch (pstate_type) {
	case dml2_pstate_type_uclk:
		blackout_us = ctx->soc_bb->power_management_parameters.dram_clk_change_blackout_us;
		break;
	case dml2_pstate_type_fclk:
		blackout_us = ctx->soc_bb->power_management_parameters.fclk_change_blackout_us;
		break;
	case dml2_pstate_type_ppt:
		blackout_us = ctx->soc_bb->power_management_parameters.g7_ppt_blackout_us;
		break;
	case dml2_pstate_type_temp_read:
		blackout_us = ctx->soc_bb->power_management_parameters.g7_temperature_read_blackout_us;
		break;
	case dml2_pstate_type_dummy_pstate:
	case dml2_pstate_type_count:
	default:
		blackout_us = 0.0;
	}

	return blackout_us;
}

static void dcn6_ms_calculate_vactive_pstate_requirements(
	const struct dml2_core_calculate_ms_context *ctx,
	struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_calcs_calculate_bytes_to_fetch_required_to_hide_latency_params *p =
			&ctx->func_params->calculate_bytes_to_fetch_required_to_hide_latency_params;
	unsigned int plane_index;
	enum dml2_pstate_type pstate_type;
	double blackout_us;

	DML_LOG_FUNC_ENTER();
	/* Max VActive bytes to fetch for any P-State (PPT or UCLK) */
	p->display_cfg = display_cfg;
	p->mrq_present = ip->dcn_mrq_present;
	p->num_active_planes = display_cfg->num_planes;
	p->num_of_dpp = inputs->NoOfDPP;
	p->meta_row_height_l = inputs->meta_row_height_luma;
	p->meta_row_height_c = inputs->meta_row_height_chroma;
	p->meta_row_bytes_per_row_ub_l = inputs->meta_row_bytes_per_row_ub_l;
	p->meta_row_bytes_per_row_ub_c = inputs->meta_row_bytes_per_row_ub_c;
	p->dpte_row_height_l = inputs->dpte_row_height;
	p->dpte_row_height_c = inputs->dpte_row_height_chroma;
	p->dpte_bytes_per_row_l = inputs->dpte_row_bytes_per_row_l;
	p->dpte_bytes_per_row_c = inputs->dpte_row_bytes_per_row_c;
	p->byte_per_pix_l = inputs->BytePerPixelY;
	p->byte_per_pix_c = inputs->BytePerPixelC;
	p->swath_width_l = inputs->SwathWidthY;
	p->swath_width_c = inputs->SwathWidthC;
	p->swath_height_l = inputs->SwathHeightY;
	p->swath_height_c = inputs->SwathHeightC;

	for (pstate_type = 0; pstate_type < dml2_pstate_type_count; pstate_type++) {
		blackout_us = dcn6_ms_pstate_type_to_blackout_us(ctx, pstate_type);

		/* skip unused pstates */
		if (blackout_us <= 0.0) {
			continue;
		}

		/* determine per plane latency */
		for (plane_index = 0; plane_index < display_cfg->num_planes; plane_index++) {
			p->latency_to_hide_us[plane_index] = dcn6_ms_pstate_type_to_blackout_us(ctx, pstate_type);

			/* Only need to consider DRAM blackout time if the plane is using some form of vactive */
			if (pstate_type == dml2_pstate_type_uclk &&
					!(inputs->uclk_pstate_switch_modes[plane_index] == dml2_pstate_method_vactive ||
					inputs->uclk_pstate_switch_modes[plane_index] == dml2_pstate_method_fw_vactive_drr)) {
				p->latency_to_hide_us[plane_index] = 0.0;
			}
		}

		/* outputs */
		p->bytes_required_l = outputs->pstate_bytes_required_l[pstate_type];
		p->bytes_required_c = outputs->pstate_bytes_required_c[pstate_type];

		dcn5_calculate_bytes_to_fetch_required_to_hide_latency(p);
	}

	/* Excess VActive bandwidth required to fill DET */
	dcn6_calculate_excess_vactive_bandwidth_required(display_cfg,
			outputs->pstate_bytes_required_l,
			outputs->pstate_bytes_required_c,

			/* outputs */
			outputs->excess_vactive_fill_bw_l,
			outputs->excess_vactive_fill_bw_c);

	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->excess_vactive_fill_bw_c, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->excess_vactive_fill_bw_l, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->pstate_bytes_required_l[dml2_pstate_type_uclk], display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->pstate_bytes_required_c[dml2_pstate_type_uclk], display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->pstate_bytes_required_l[dml2_pstate_type_fclk], display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->pstate_bytes_required_c[dml2_pstate_type_fclk], display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->pstate_bytes_required_l[dml2_pstate_type_ppt], display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->pstate_bytes_required_c[dml2_pstate_type_ppt], display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->pstate_bytes_required_l[dml2_pstate_type_temp_read], display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->pstate_bytes_required_c[dml2_pstate_type_temp_read], display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

/* FIXME - break it down according the function name */
static void dcn6_ms_calculate_det_buffer_time_value_urgent_burst_factor_and_urgent_latency_hiding(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;
	double line_time_us;
	bool cursor_not_enough_urgent_latency_hiding;
	unsigned int cursor_lines_per_chunk;
	unsigned int cursor_bytes;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];
		line_time_us = stream->timing.h_total / ((double) stream->timing.pixel_clock_khz / 1000);
		cursor_not_enough_urgent_latency_hiding = 0;
		dcn5_calculate_cursor_req_attributes(
				plane->cursor.cursor_width,
				plane->cursor.cursor_bpp,
				// output
				&cursor_lines_per_chunk,
				&outputs->cursor_bytes_per_line[k],
				&outputs->cursor_bytes_per_chunk[k],
				&cursor_bytes);
		dcn5_calculate_cursor_urgent_burst_factor(
				ip->cursor_buffer_size,
				plane->cursor.cursor_width,
				outputs->cursor_bytes_per_chunk[k],
				cursor_lines_per_chunk, line_time_us,
				inputs->UrgLatency,

				// output
				&outputs->UrgentBurstFactorCursor[k],
				&cursor_not_enough_urgent_latency_hiding);
		outputs->UrgentBurstFactorCursorPre[k] = outputs->UrgentBurstFactorCursor[k];
		DML_LOG_VERBOSE("DML::%s: k=%d, Calling CalculateUrgentBurstFactor\n", __func__, k);
		DML_LOG_VERBOSE("DML::%s: k=%d, VRatio=%f\n", __func__, k,
				plane->composition.scaler_info.plane0.v_ratio);
		DML_LOG_VERBOSE("DML::%s: k=%d, VRatioChroma=%f\n", __func__, k,
				plane->composition.scaler_info.plane1.v_ratio);
		dcn5_calculate_urgent_burst_factor(
				&display_cfg->plane_descriptors[k],
				inputs->swath_width_luma_ub[k],
				inputs->swath_width_chroma_ub[k],
				inputs->SwathHeightY[k],
				inputs->SwathHeightC[k],
				line_time_us,
				inputs->UrgLatency,
				plane->composition.scaler_info.plane0.v_ratio,
				plane->composition.scaler_info.plane1.v_ratio,
				inputs->BytePerPixelInDETY[k],
				inputs->BytePerPixelInDETC[k],
				inputs->DETBufferSizeY[k],
				inputs->DETBufferSizeC[k],

				// Output
				&outputs->UrgentBurstFactorLuma[k],
				&outputs->UrgentBurstFactorChroma[k],
				&outputs->NotEnoughUrgentLatencyHiding[k]);

		outputs->NotEnoughUrgentLatencyHiding[k] = outputs->NotEnoughUrgentLatencyHiding[k]
						|| cursor_not_enough_urgent_latency_hiding;
	}

	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->UrgentBurstFactorCursorPre, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->cursor_bytes_per_chunk, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->UrgentBurstFactorCursor, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->cursor_bytes_per_line, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->UrgentBurstFactorChroma, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->UrgentBurstFactorLuma, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->NotEnoughUrgentLatencyHiding, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_min_dcfclk_deepsleep_clock(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	const struct dml2_clock_granularity_adjuster *clock_adjuster = ctx->clock_adjuster;
	double raw_dcfclk_deepsleep_mhz = 0;

	DML_LOG_FUNC_ENTER();
	dcn5_calculate_dcfclk_deep_sleep(
			display_cfg,
			display_cfg->num_planes,
			inputs->BytePerPixelY,
			inputs->BytePerPixelC,
			inputs->SwathWidthY,
			inputs->SwathWidthC,
			inputs->NoOfDPP,
			inputs->PSCL_FACTOR,
			inputs->PSCL_FACTOR_CHROMA,
			inputs->RequiredDPPCLK,
			inputs->vactive_sw_bw_l,
			inputs->vactive_sw_bw_c,
			soc_bb->return_bus_width_bytes,
			/* Output */
			&raw_dcfclk_deepsleep_mhz);
	outputs->dcfclk_deepsleep = clock_adjuster->adjust_dcfclk_deepsleep_mhz(
			clock_adjuster, raw_dcfclk_deepsleep_mhz);
	DML_LOG_DEBUG_DOUBLE(outputs->dcfclk_deepsleep);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_writeback_delay(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	unsigned int k;
	unsigned int j;
	const struct dml2_stream_parameters *stream;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		outputs->WritebackDelayTime[k] = 0.0;
		for (j = 0; j < stream->writeback.active_writebacks_per_stream; j++) {
			outputs->WritebackDelayTime[k] = math_max2(outputs->WritebackDelayTime[k],
					soc_bb->writeback_base_latency_us
					+ dcn5_calculate_write_back_delay(
							stream->writeback.writeback_stream[j].pixel_format,
							stream->writeback.writeback_stream[j].h_ratio,
							stream->writeback.writeback_stream[j].v_ratio,
							stream->writeback.writeback_stream[j].v_taps,
							stream->writeback.writeback_stream[j].v_taps_chroma,
							stream->writeback.writeback_stream[j].output_width,
							stream->writeback.writeback_stream[j].output_height,
							stream->writeback.writeback_stream[j].input_width,
							stream->writeback.writeback_stream[j].input_height,
							stream->timing.h_total)
							/ inputs->RequiredDISPCLK);
		}
	}

	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->WritebackDelayTime, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_alternate_params(const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_scratch *func_params = ctx->func_params;
	struct dml2_core_calcs_calculate_alternate_params *p = &func_params->calculate_alternate_params;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	p->display_cfg = ctx->display_cfg;
	p->dst_y_prefetch = inputs->dst_y_prefetch;
	p->SwathHeightY = inputs->SwathHeightY;
	p->SwathHeightC = inputs->SwathHeightC;
	p->SwathWidthY = inputs->SwathWidthY;
	p->SwathWidthC = inputs->SwathWidthC;
	p->DETBufferSizeY = inputs->DETBufferSizeY;
	p->DETBufferSizeC = inputs->DETBufferSizeC;
	p->BytePerPixelY = inputs->BytePerPixelY;
	p->BytePerPixelC = inputs->BytePerPixelC;
	p->BytePerPixelInDETY = inputs->BytePerPixelInDETY;
	p->BytePerPixelInDETC = inputs->BytePerPixelInDETC;
	p->Read256BlockWidthY = inputs->Read256BlockWidthY;
	p->Read256BlockHeightY = inputs->Read256BlockHeightY;
	p->Read256BlockWidthC = inputs->Read256BlockWidthC;
	p->Read256BlockHeightC = inputs->Read256BlockHeightC;
	p->MacroTileWidthY = inputs->MacroTileWidthY;
	p->MacroTileWidthC = inputs->MacroTileWidthC;
	p->VInitPrefillY = inputs->PrefillY;
	p->VInitPrefillC = inputs->PrefillC;
	p->VRatioPrefetchY = inputs->VRatioPreY;
	p->VRatioPrefetchC = inputs->VRatioPreC;
	p->NoOfDPP = inputs->NoOfDPP;
	p->max_num_dpp = ctx->ip->max_num_dpp;
	p->dram_blackout_us = soc_bb->power_management_parameters.dram_clk_change_blackout_us;
	p->VActiveLatencyHidingUs = inputs->VActiveLatencyHidingUs;
	p->svp0_dst_lines = inputs->svp0_dst_lines;
	p->svp1_dst_lines = inputs->svp1_dst_lines;
	p->svp_req_limit = inputs->svp_req_limit;
	p->dcn_non_urgent_bandwidth_kbps = inputs->support.bandwidth_upper_bound.dcn5.non_urgent_bandwidth_kbps;
	p->alt_chan_fw_delay_us = ctx->ip->alt_chan_fw_delay_us;
	p->dst_y_per_vm_vblank = inputs->LinesForVM;
	p->dst_y_per_row_vblank = inputs->LinesForDPTERow;
	p->DSTYAfterScaler = inputs->DSTYAfterScaler;
	p->ODMMode = inputs->ODMMode;

	p->svp0_max_bytes = &outputs->svp0_max_bytes;
	p->svp1_max_bytes = &outputs->svp1_max_bytes;
	p->svp0_max_bytes_per_dpp = outputs->svp0_max_bytes_per_dpp;
	p->svp0_max_bytes_per_dpp_c = outputs->svp0_max_bytes_per_dpp_c;
	p->svp1_max_bytes_per_dpp = outputs->svp1_max_bytes_per_dpp;
	p->svp1_max_bytes_per_dpp_c = outputs->svp1_max_bytes_per_dpp_c;
	p->nom_req_limit_alt = outputs->nom_req_limit_alt;
	p->min_lead_dst_lines = outputs->min_lead_dst_lines;
	p->total_swaths = outputs->total_swaths;
	p->total_swaths_c = outputs->total_swaths_c;
	p->prefetch_swaths = outputs->prefetch_swaths;
	p->prefetch_swaths_c = outputs->prefetch_swaths_c;
	p->prefetch_hdl_delta = outputs->prefetch_hdl_delta;
	p->recout_hdl_delta = outputs->recout_hdl_delta;
	p->prefetch_hdl_delta_c = outputs->prefetch_hdl_delta_c;
	p->recout_hdl_delta_c = outputs->recout_hdl_delta_c;
	p->max_prefetch_in_lines = outputs->max_prefetch_in_lines;
	p->lsdma_bw_req_for_alt_kbps = &outputs->lsdma_bw_req_for_alt_kbps;

	dcn6_calculate_alternate_params(p);

	DML_LOG_DEBUG_UINT(outputs->svp0_max_bytes);
	DML_LOG_DEBUG_UINT(outputs->svp1_max_bytes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->svp0_max_bytes_per_dpp, ctx->display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->svp0_max_bytes_per_dpp_c, ctx->display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->svp1_max_bytes_per_dpp, ctx->display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->svp1_max_bytes_per_dpp_c, ctx->display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->nom_req_limit_alt, ctx->display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->min_lead_dst_lines, ctx->display_cfg->num_streams);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->total_swaths, ctx->display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->total_swaths_c, ctx->display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->prefetch_swaths, ctx->display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->prefetch_swaths_c, ctx->display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->prefetch_hdl_delta, ctx->display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->recout_hdl_delta, ctx->display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->prefetch_hdl_delta_c, ctx->display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->recout_hdl_delta_c, ctx->display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->max_prefetch_in_lines, ctx->display_cfg->num_streams);
	DML_LOG_DEBUG_DOUBLE(outputs->lsdma_bw_req_for_alt_kbps);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_alternate_svp_lines(const struct dml2_core_calculate_ms_context *ctx,
	struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_scratch *func_params = ctx->func_params;
	struct dml2_core_calcs_calculate_alternate_svp_lines *p = &func_params->calculate_alternate_svp_lines;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	p->display_cfg = ctx->display_cfg;
	p->SwathHeightY = inputs->SwathHeightY;
	p->SwathHeightC = inputs->SwathHeightC;
	p->BytePerPixelInDETC = inputs->BytePerPixelInDETC;
	p->dram_blackout_us = soc_bb->power_management_parameters.dram_clk_change_blackout_us;

	p->svp0_dst_lines = outputs->svp0_dst_lines;
	p->svp1_dst_lines = outputs->svp1_dst_lines;
	p->svp_req_limit = outputs->svp_req_limit;

	dcn6_calculate_alternate_svp_lines(p);

	DML_LOG_DEBUG_ARRAY_UINT(outputs->svp0_dst_lines, ctx->display_cfg->num_streams);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->svp1_dst_lines, ctx->display_cfg->num_streams);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->svp_req_limit, ctx->display_cfg->num_streams);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_max_vstartup(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	unsigned int k, stream_index;
	const struct dml2_stream_parameters *stream;

	DML_LOG_FUNC_ENTER();
	// MaximumVStartup is actually Tvstartup_min in DCN4 programming guide
	for (k = 0; k < display_cfg->num_planes; k++) {
		stream_index = display_cfg->plane_descriptors[k].stream_index;
		stream = &display_cfg->stream_descriptors[stream_index];
		outputs->MaximumVStartup[k] = dcn6_calculate_max_vstartup(
				ip->ptoi_supported,
				ip->vblank_nom_default_us,
				&stream->timing,
				display_cfg->plane_descriptors[k].overrides.uclk_pstate_change_strategy,
				inputs->WritebackDelayTime[k],
				inputs->svp0_dst_lines[stream_index] + inputs->svp1_dst_lines[stream_index]);
		outputs->MaxVStartupLines[k] = outputs->MaximumVStartup[k];
	}
	DML_LOG_VERBOSE("DML::%s: k=%u, MaximumVStartup = %u\n", __func__, k, outputs->MaximumVStartup[k]);

	DML_LOG_DEBUG_ARRAY_UINT(outputs->MaximumVStartup, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->MaxVStartupLines, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_check_average_latency_supports(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	double outstanding_latency_us = 0;
	unsigned int k;

	DML_LOG_FUNC_ENTER();
	outputs->support.OutstandingRequestsSupport = true;
	outputs->support.OutstandingRequestsUrgencyAvoidance = true;
	for (k = 0; k < display_cfg->num_planes; k++) {
		outstanding_latency_us = soc_bb->max_outstanding_reqs
				* inputs->support.request_size_bytes_luma[k]
				/ (inputs->DCFCLK * soc_bb->return_bus_width_bytes);
		if (outstanding_latency_us < inputs->support.avg_urgent_latency_us) {
			outputs->support.OutstandingRequestsSupport = false;
			DML_LOG_VERBOSE("DML::%s: DCFCLK = %f\n", __func__, inputs->DCFCLK);
		}
		if (outstanding_latency_us < inputs->support.avg_non_urgent_latency_us) {
			outputs->support.OutstandingRequestsUrgencyAvoidance = false;
		}
		DML_LOG_VERBOSE("DML::%s: avg_urgent_latency_us = %f\n", __func__, inputs->support.avg_urgent_latency_us);
		DML_LOG_VERBOSE("DML::%s: avg_non_urgent_latency_us = %f\n", __func__, inputs->support.avg_non_urgent_latency_us);
		DML_LOG_VERBOSE("DML::%s: k=%d, request_size_bytes_luma = %d\n", __func__, k, inputs->support.request_size_bytes_luma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, outstanding_latency_us = %f (luma)\n", __func__, k, outstanding_latency_us);
		if (inputs->BytePerPixelC[k] > 0) {
			outstanding_latency_us = soc_bb->max_outstanding_reqs
					* inputs->support.request_size_bytes_chroma[k]
					/ (inputs->DCFCLK * soc_bb->return_bus_width_bytes);
			if (outstanding_latency_us < inputs->support.avg_urgent_latency_us) {
				outputs->support.OutstandingRequestsSupport = false;
			}
			if (outstanding_latency_us < inputs->support.avg_non_urgent_latency_us) {
				outputs->support.OutstandingRequestsUrgencyAvoidance = false;
			}
			DML_LOG_VERBOSE("DML::%s: k=%d, request_size_bytes_chroma = %d\n", __func__, k, inputs->support.request_size_bytes_chroma[k]);
			DML_LOG_VERBOSE("DML::%s: k=%d, outstanding_latency_us = %f (chroma)\n", __func__, k, outstanding_latency_us);
		}
	}

	DML_LOG_DEBUG_BOOL(outputs->support.OutstandingRequestsUrgencyAvoidance);
	DML_LOG_DEBUG_BOOL(outputs->support.OutstandingRequestsSupport);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_mcache_setting(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	struct dml2_core_calcs_calculate_mcache_setting_params *p;

	DML_LOG_FUNC_ENTER();
	if (soc_bb->mcache_size_bytes == 0) {
		for (k = 0; k < display_cfg->num_planes; k++) {
			outputs->dcc_dram_bw_nom_overhead_factor_p0[k] = 1.0;
			outputs->dcc_dram_bw_pref_overhead_factor_p0[k] = 1.0;
			outputs->dcc_dram_bw_nom_overhead_factor_p1[k] = 1.0;
			outputs->dcc_dram_bw_pref_overhead_factor_p1[k] = 1.0;
		}
	} else {
		p = &ctx->func_params->calculate_mcache_setting_params;
		memset(p, 0, sizeof(struct dml2_core_calcs_calculate_mcache_setting_params));
		for (k = 0; k < display_cfg->num_planes; k++) {
			plane = &display_cfg->plane_descriptors[k];
			p->dcc_enable = plane->surface.dcc.enable;
			p->num_chans = soc_bb->dram_config.channel_count;
			p->mem_word_bytes = soc_bb->mem_word_bytes;
			p->mcache_size_bytes = soc_bb->mcache_size_bytes;
			p->mcache_line_size_bytes = soc_bb->mcache_line_size_bytes;
			p->gpuvm_enable = display_cfg->gpuvm_enable;
			p->gpuvm_page_size_kbytes = plane->overrides.gpuvm_min_page_size_kbytes;
			p->source_format = plane->pixel_format;
			p->surf_vert = dml2_core_utils_is_vertical_rotation(plane->composition.rotation_angle);
			p->vp_stationary = plane->composition.viewport.stationary;
			p->tiling_mode = plane->surface.tiling;
			p->imall_enable = ip->imall_supported;
			p->vp_start_x_l = plane->composition.viewport.plane0.x_start;
			p->vp_start_y_l = plane->composition.viewport.plane0.y_start;
			p->full_vp_width_l = plane->composition.viewport.plane0.width;
			p->full_vp_height_l = plane->composition.viewport.plane0.height;
			p->blk_width_l = inputs->MacroTileWidthY[k];
			p->blk_height_l = inputs->MacroTileHeightY[k];
			p->vmpg_width_l = inputs->vmpg_width_y[k];
			p->vmpg_height_l = inputs->vmpg_height_y[k];
			p->full_swath_bytes_l = inputs->full_swath_bytes_l[k];
			p->bytes_per_pixel_l = inputs->BytePerPixelY[k];
			p->vp_start_x_c = plane->composition.viewport.plane1.x_start;
			p->vp_start_y_c = plane->composition.viewport.plane1.y_start;
			p->full_vp_width_c = plane->composition.viewport.plane1.width;
			p->full_vp_height_c = plane->composition.viewport.plane1.height;
			p->blk_width_c = inputs->MacroTileWidthC[k];
			p->blk_height_c = inputs->MacroTileHeightC[k];
			p->vmpg_width_c = inputs->vmpg_width_c[k];
			p->vmpg_height_c = inputs->vmpg_height_c[k];
			p->full_swath_bytes_c = inputs->full_swath_bytes_c[k];
			p->bytes_per_pixel_c = inputs->BytePerPixelC[k];
			// output
			p->dcc_dram_bw_nom_overhead_factor_l = &outputs->dcc_dram_bw_nom_overhead_factor_p0[k];
			p->dcc_dram_bw_pref_overhead_factor_l = &outputs->dcc_dram_bw_pref_overhead_factor_p0[k];
			p->dcc_dram_bw_nom_overhead_factor_c = &outputs->dcc_dram_bw_nom_overhead_factor_p1[k];
			p->dcc_dram_bw_pref_overhead_factor_c = &outputs->dcc_dram_bw_pref_overhead_factor_p1[k];
			p->num_mcaches_l = &outputs->num_mcaches_l[k];
			p->mcache_row_bytes_l = &outputs->mcache_row_bytes_l[k];
			p->mcache_row_bytes_per_channel_l = &outputs->mcache_row_bytes_per_channel_l[k];
			p->mcache_offsets_l = outputs->mcache_offsets_l[k];
			p->mcache_shift_granularity_l = &outputs->mcache_shift_granularity_l[k];
			p->num_mcaches_c = &outputs->num_mcaches_c[k];
			p->mcache_row_bytes_c = &outputs->mcache_row_bytes_c[k];
			p->mcache_row_bytes_per_channel_c = &outputs->mcache_row_bytes_per_channel_c[k];
			p->mcache_offsets_c = outputs->mcache_offsets_c[k];
			p->mcache_shift_granularity_c = &outputs->mcache_shift_granularity_c[k];
			p->mall_comb_mcache_l = &outputs->mall_comb_mcache_l[k];
			p->mall_comb_mcache_c = &outputs->mall_comb_mcache_c[k];
			p->lc_comb_mcache = &outputs->lc_comb_mcache[k];
			dcn5_calculate_mcache_setting(ctx->func_params, p);
		}
	}

	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->dcc_dram_bw_pref_overhead_factor_p1, display_cfg->num_planes);
	DML_LOG_DEBUG_2D_ARRAY_UINT(outputs->mcache_offsets_l, display_cfg->num_planes, DML2_MAX_MCACHES + 1);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->mall_prefetch_dram_overhead_factor, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->mcache_row_bytes_per_channel_l, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->lc_comb_mcache, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->mall_comb_mcache_l, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->mall_prefetch_sdp_overhead_factor, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->mcache_row_bytes_per_channel_c, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->mcache_shift_granularity_l, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->mcache_row_bytes_l, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->dcc_dram_bw_pref_overhead_factor_p0, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->num_mcaches_c, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->mall_comb_mcache_c, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->mcache_shift_granularity_c, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->num_mcaches_l, display_cfg->num_planes);
	DML_LOG_DEBUG_2D_ARRAY_UINT(outputs->mcache_offsets_c, display_cfg->num_planes, DML2_MAX_MCACHES + 1);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->mcache_row_bytes_c, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->dcc_dram_bw_nom_overhead_factor_p0, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->dcc_dram_bw_nom_overhead_factor_p1, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_avg_bandwidth_and_dcfclk_lb_required(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *outputs = states;
	struct dml2_core_internal_mode_support *inputs = states;

	DML_LOG_FUNC_ENTER();
	// Average BW support check
	dcn5_calculate_avg_bandwidth_required(
			*outputs->support.avg_bandwidth_required,
			// input
			display_cfg->num_planes,
			inputs->vactive_sw_bw_l,
			inputs->vactive_sw_bw_c,
			inputs->cursor_bw,
			inputs->dcc_dram_bw_nom_overhead_factor_p0,
			inputs->dcc_dram_bw_nom_overhead_factor_p1);

	DML_LOG_VERBOSE("DML::%s: avg_bandwidth_required=%f\n", __func__, outputs->support.avg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp]);

	DML_LOG_DEBUG_2D_ARRAY_DOUBLE(outputs->support.avg_bandwidth_required,
			dml2_core_internal_soc_state_max, dml2_core_internal_bw_max);
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_urgent_latency_hiding_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	unsigned int k;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	outputs->support.EnoughUrgentLatencyHidingSupport = true;

	for (k = 0; k < display_cfg->num_planes; k++) {
		if (inputs->NotEnoughUrgentLatencyHiding[k]) {
			outputs->support.EnoughUrgentLatencyHidingSupport = false;
			DML_LOG_VERBOSE("DML::%s: k=%u NotEnoughUrgentLatencyHiding set\n", __func__, k);
		}
	}

	DML_LOG_DEBUG_BOOL(outputs->support.EnoughUrgentLatencyHidingSupport);
	DML_LOG_FUNC_EXIT();

	return outputs->support.EnoughUrgentLatencyHidingSupport;
}

static void dcn6_ms_calculate_t_calc(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	(void)ctx;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	outputs->TimeCalc = 24 / inputs->dcfclk_deepsleep;

	DML_LOG_DEBUG_DOUBLE(outputs->TimeCalc);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_hostvm_inefficiency_factor(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	dcn5_calculate_hostvm_inefficiency_factor(
			&outputs->HostVMInefficiencyFactor,
			&outputs->HostVMInefficiencyFactorPrefetch,
			display_cfg->gpuvm_enable,
			display_cfg->hostvm_enable,
			ip->remote_iommu_outstanding_translations,
			soc_bb->max_outstanding_reqs,
			1.0,
			0.5);

	DML_LOG_DEBUG_DOUBLE(outputs->HostVMInefficiencyFactorPrefetch);
	DML_LOG_DEBUG_DOUBLE(outputs->HostVMInefficiencyFactor);
	DML_LOG_FUNC_EXIT();
}

// Using approximate ratio for VM bandwidth

static void dcn6_ms_calculate_3dlut_settings(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	struct dml2_core_calcs_calculate_tdlut_setting_params *p = &ctx->func_params->calculate_tdlut_setting_params;

	DML_LOG_FUNC_ENTER();
	outputs->Total3dlutActive = 0;
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		if (plane->tdlut.setup_for_tdlut)
			outputs->Total3dlutActive = outputs->Total3dlutActive + 1;

		// Calculate tdlut schedule related terms
		p->dispclk_mhz = inputs->RequiredDISPCLK;
		p->setup_for_tdlut = plane->tdlut.setup_for_tdlut;
		p->tdlut_width_mode = plane->tdlut.tdlut_width_mode;
		p->tdlut_addressing_mode = plane->tdlut.tdlut_addressing_mode;
		p->cursor_buffer_size = ip->cursor_buffer_size;
		p->gpuvm_enable = display_cfg->gpuvm_enable;
		p->gpuvm_page_size_kbytes = plane->overrides.gpuvm_min_page_size_kbytes;
		p->tdlut_mpc_width_flag = plane->tdlut.tdlut_mpc_width_flag;
		p->is_gfx11 = dml2_core_utils_get_gfx_version(plane->surface.tiling) == 11;
		// output
		p->tdlut_pte_bytes_per_frame = &outputs->tdlut_pte_bytes_per_frame[k];
		p->tdlut_bytes_per_frame = &outputs->tdlut_bytes_per_frame[k];
		p->tdlut_groups_per_2row_ub = &outputs->tdlut_groups_per_2row_ub[k];
		p->tdlut_opt_time = &outputs->tdlut_opt_time[k];
		p->tdlut_drain_time = &outputs->tdlut_drain_time[k];
		p->tdlut_bytes_per_group = &outputs->tdlut_bytes_per_group[k];
		dcn5_calculate_tdlut_setting(ctx->func_params, p);
	}

	DML_LOG_DEBUG_ARRAY_UINT(outputs->tdlut_bytes_per_group, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->tdlut_bytes_per_frame, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->tdlut_drain_time, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->tdlut_opt_time, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->tdlut_pte_bytes_per_frame, display_cfg->num_planes);
	DML_LOG_DEBUG_UINT(outputs->Total3dlutActive);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_urgent_latency(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	dcn5_calculate_extra_latency(
			display_cfg,
			ip->rob_buffer_size_kbytes,
			0,
			0,
			inputs->DCFCLK,
			0.0,
			ip->pixel_chunk_size_kbytes,
			inputs->min_available_urgent_bandwidth_MBps,
			display_cfg->num_planes,
			inputs->NoOfDPP,
			inputs->dpte_group_bytes,
			inputs->tdlut_bytes_per_group,
			inputs->HostVMInefficiencyFactor,
			inputs->HostVMInefficiencyFactorPrefetch,
			dml2_qos_param_type_dcn4x,
			!(display_cfg->overrides.max_outstanding_when_urgent_expected_disable),
			soc_bb->max_outstanding_reqs,
			inputs->support.request_size_bytes_luma,
			inputs->support.request_size_bytes_chroma,
			ip->meta_chunk_size_kbytes,
			ip->dchub_arb_to_ret_delay,
			inputs->TripToMemory,
			ip->hostvm_mode,
			// output
			&outputs->ExtraLatency,
			&outputs->ExtraLatency_sr,
			&outputs->ExtraLatencyPrefetch);

	DML_LOG_DEBUG_DOUBLE(outputs->ExtraLatencyPrefetch);
	DML_LOG_DEBUG_DOUBLE(outputs->ExtraLatency_sr);
	DML_LOG_DEBUG_DOUBLE(outputs->ExtraLatency);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_prefetch_schedule(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	struct dml2_core_calcs_mode_support_locals *dummies = ctx->dummies;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;
	struct dml2_core_internal_DmlPipe *pipe = &dummies->myPipe;
	struct dml2_core_calcs_CalculatePrefetchSchedule_params *p = &ctx->func_params->CalculatePrefetchSchedule_params;
	double Tvm_trip;
	double Tr0_trip;
	double prefetch_sw_bytes;
	double Tpre_rounded;
	double Tpre_oto;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];
		outputs->TWait[k] = dcn5_calculate_t_wait(
				plane->overrides.reserved_vblank_time_ns,
				inputs->UrgLatency,
				inputs->TripToMemory,
				math_max2(soc_bb->power_management_parameters.g7_ppt_blackout_us, soc_bb->power_management_parameters.g7_temperature_read_blackout_us),
				display_cfg->stream_descriptors->timing.drr_config.enabled);
		pipe->Dppclk = inputs->RequiredDPPCLK[k];
		pipe->Dispclk = inputs->RequiredDISPCLK;
		pipe->PixelClock = ((double) stream->timing.pixel_clock_khz / 1000);
		pipe->DCFClkDeepSleep = inputs->dcfclk_deepsleep;
		pipe->DPPPerSurface = inputs->NoOfDPP[k];
		pipe->ScalerEnabled = plane->composition.scaler_info.enabled;
		pipe->VRatio = plane->composition.scaler_info.plane0.v_ratio;
		pipe->VRatioChroma = plane->composition.scaler_info.plane1.v_ratio;
		pipe->VTaps = plane->composition.scaler_info.plane0.v_taps;
		pipe->VTapsChroma = plane->composition.scaler_info.plane1.v_taps;
		pipe->RotationAngle = plane->composition.rotation_angle;
		pipe->mirrored = plane->composition.mirrored;
		pipe->BlockWidth256BytesY = inputs->Read256BlockWidthY[k];
		pipe->BlockHeight256BytesY = inputs->Read256BlockHeightY[k];
		pipe->BlockWidth256BytesC = inputs->Read256BlockWidthC[k];
		pipe->BlockHeight256BytesC = inputs->Read256BlockHeightC[k];
		pipe->InterlaceEnable = stream->timing.interlaced;
		pipe->NumberOfCursors = plane->cursor.num_cursors;
		pipe->VBlank = stream->timing.v_total - stream->timing.v_active;
		pipe->HTotal = stream->timing.h_total;
		pipe->HActive = stream->timing.h_active;
		pipe->DCCEnable = plane->surface.dcc.enable;
		pipe->ODMMode = inputs->ODMMode[k];
		pipe->SourcePixelFormat = plane->pixel_format;
		pipe->BytePerPixelY = inputs->BytePerPixelY[k];
		pipe->BytePerPixelC = inputs->BytePerPixelC[k];
		pipe->ProgressiveToInterlaceUnitInOPP = ip->ptoi_supported;
		DML_LOG_VERBOSE("DML::%s: Calling CalculatePrefetchSchedule for k=%u\n", __func__, k);
		DML_LOG_VERBOSE("DML::%s: MaximumVStartup = %u\n", __func__, inputs->MaximumVStartup[k]);
		p->display_cfg = display_cfg;
		p->HostVMInefficiencyFactor = inputs->HostVMInefficiencyFactorPrefetch;
		p->myPipe = pipe;
		p->DSCDelay = inputs->DSCDelay[k];
		p->DPPCLKDelaySubtotalPlusCNVCFormater = ip->dppclk_delay_subtotal + ip->dppclk_delay_cnvc_formatter;
		p->DPPCLKDelaySCL = ip->dppclk_delay_scl;
		p->DPPCLKDelaySCLLBOnly = ip->dppclk_delay_scl_lb_only;
		p->DPPCLKDelayCNVCCursor = ip->dppclk_delay_cnvc_cursor;
		p->DISPCLKDelaySubtotal = ip->dispclk_delay_subtotal;
		p->DPP_RECOUT_WIDTH = (unsigned int) (inputs->SwathWidthY[k] / plane->composition.scaler_info.plane0.h_ratio);
		p->OutputFormat = stream->output.output_format;
		p->MaxInterDCNTileRepeaters = ip->max_inter_dcn_tile_repeaters;
		p->VStartup = inputs->MaxVStartupLines[k];
		p->HostVMMinPageSize = plane->overrides.hostvm_min_page_size_kbytes;
		p->DynamicMetadataEnable = plane->dynamic_meta_data.enable;
		p->DynamicMetadataVMEnabled = ip->dynamic_metadata_vm_enabled;
		p->DynamicMetadataLinesBeforeActiveRequired = plane->dynamic_meta_data.lines_before_active_required;
		p->DynamicMetadataTransmittedBytes = plane->dynamic_meta_data.transmitted_bytes;
		p->ExtraLatencyPrefetch = inputs->ExtraLatencyPrefetch;
		p->TCalc = inputs->TimeCalc;
		p->vm_bytes = inputs->vm_bytes[k];
		p->PixelPTEBytesPerRow = inputs->DPTEBytesPerRow[k];
		p->PrefetchSourceLinesY = inputs->PrefetchLinesY[k];
		p->VInitPreFillY = inputs->PrefillY[k];
		p->MaxNumSwathY = inputs->MaxNumSwathY[k];
		p->PrefetchSourceLinesC = inputs->PrefetchLinesC[k];
		p->VInitPreFillC = inputs->PrefillC[k];
		p->MaxNumSwathC = inputs->MaxNumSwathC[k];
		p->swath_width_luma_ub = inputs->swath_width_luma_ub[k];
		p->swath_width_chroma_ub = inputs->swath_width_chroma_ub[k];
		p->SwathHeightY = inputs->SwathHeightY[k];
		p->SwathHeightC = inputs->SwathHeightC[k];
		p->TWait = outputs->TWait[k];
		p->Ttrip = inputs->TripToMemory;
		p->Turg = inputs->UrgLatency;
		p->setup_for_tdlut = plane->tdlut.setup_for_tdlut;
		p->use_max_lsw = plane->overrides.use_max_lsw;
		p->tdlut_pte_bytes_per_frame = inputs->tdlut_pte_bytes_per_frame[k];
		p->tdlut_bytes_per_frame = inputs->tdlut_bytes_per_frame[k];
		p->tdlut_opt_time = inputs->tdlut_opt_time[k];
		p->tdlut_drain_time = inputs->tdlut_drain_time[k];
		p->num_cursors = (plane->cursor.cursor_width > 0);
		p->cursor_bytes_per_chunk = inputs->cursor_bytes_per_chunk[k];
		p->cursor_bytes_per_line = inputs->cursor_bytes_per_line[k];
		p->dcc_enable = plane->surface.dcc.enable;
		p->mrq_present = ip->dcn_mrq_present;
		p->meta_row_bytes = inputs->meta_row_bytes[k];
		// output
		p->DSTXAfterScaler = &outputs->DSTXAfterScaler[k];
		p->DSTYAfterScaler = &outputs->DSTYAfterScaler[k];
		p->dst_y_prefetch = &outputs->dst_y_prefetch[k];
		p->dst_y_per_vm_vblank = &outputs->LinesForVM[k];
		p->dst_y_per_row_vblank = &outputs->LinesForDPTERow[k];
		p->VRatioPrefetchY = &outputs->VRatioPreY[k];
		p->VRatioPrefetchC = &outputs->VRatioPreC[k];
		p->RequiredPrefetchPixelDataBWLuma = &outputs->RequiredPrefetchPixelDataBWLuma[k]; // prefetch_sw_bw_l
		p->RequiredPrefetchPixelDataBWChroma = &outputs->RequiredPrefetchPixelDataBWChroma[k]; // prefetch_sw_bw_c
		p->NotEnoughTimeForDynamicMetadata = &outputs->NoTimeForDynamicMetadata[k];
		p->Tno_bw = &outputs->Tno_bw[k];
		p->Tno_bw_flip = &outputs->Tno_bw_flip[k];
		p->prefetch_vmrow_bw = &outputs->prefetch_vmrow_bw[k];
		p->Tdmdl_vm = &outputs->Tdmdl_vm_raw[k];
		p->Tdmdl = &outputs->Tdmdl_raw[k];
		p->TSetup = &outputs->TSetup[k];
		p->Tvm_trips = &Tvm_trip;
		p->Tr0_trips = &Tr0_trip;
		p->Tvm_trips_flip = &outputs->Tvm_trips_flip[k];
		p->Tr0_trips_flip = &outputs->Tr0_trips_flip[k];
		p->Tvm_trips_flip_rounded = &outputs->Tvm_trips_flip_rounded[k];
		p->Tr0_trips_flip_rounded = &outputs->Tr0_trips_flip_rounded[k];
		p->VUpdateOffsetPix = &outputs->VUpdateOffsetPix[k];
		p->VUpdateWidthPix = &outputs->VUpdateWidthPix[k];
		p->VReadyOffsetPix = &outputs->VReadyOffsetPix[k];
		p->prefetch_cursor_bw = &outputs->prefetch_cursor_bw[k];
		p->prefetch_sw_bytes = &prefetch_sw_bytes;
		p->Tpre_rounded = &Tpre_rounded;
		p->Tpre_oto = &Tpre_oto;

		outputs->NoTimeForPrefetch[k] = dcn5_calculate_prefetch_schedule(ctx->func_params, p);
		DML_LOG_VERBOSE("DML::%s: k=%d, dst_y_per_vm_vblank = %f\n", __func__, k, *p->dst_y_per_vm_vblank);
		DML_LOG_VERBOSE("DML::%s: k=%d, dst_y_per_row_vblank = %f\n", __func__, k, *p->dst_y_per_row_vblank);
		outputs->VStartupMin[k] = inputs->MaxVStartupLines[k];
	}


	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->prefetch_vmrow_bw, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->DSTYAfterScaler, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->Tr0_trips_flip_rounded, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->Tno_bw, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->dst_y_prefetch, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->DSTXAfterScaler, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->Tr0_trips_flip, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->prefetch_cursor_bw, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->LinesForDPTERow, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->LinesForVM, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->NoTimeForPrefetch, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->NoTimeForDynamicMetadata, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->RequiredPrefetchPixelDataBWLuma, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->RequiredPrefetchPixelDataBWChroma, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->Tvm_trips_flip, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->TWait, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->VRatioPreC, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->Tvm_trips_flip_rounded, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->VRatioPreY, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->Tno_bw_flip, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->VReadyOffsetPix, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->TSetup, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->Tdmdl_vm_raw, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->Tdmdl_raw, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->VUpdateWidthPix, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->VStartupMin, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_prefetch_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	unsigned int k;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	outputs->support.PrefetchScheduleSupported = true;

	for (k = 0; k < display_cfg->num_planes; k++) {
		if (inputs->dst_y_prefetch[k] < 2.0
				|| inputs->LinesForVM[k] >= 32.0
				|| inputs->LinesForDPTERow[k] >= 16.0
				|| inputs->NoTimeForPrefetch[k] == true
				|| inputs->DSTYAfterScaler[k] > 8) {
			outputs->support.PrefetchScheduleSupported = false;
			DML_LOG_VERBOSE("DML::%s: k=%d, dst_y_prefetch=%f (should not be < 2)\n", __func__, k, inputs->dst_y_prefetch[k]);
			DML_LOG_VERBOSE("DML::%s: k=%d, LinesForVM=%f (should not be >= 32)\n", __func__, k, inputs->LinesForVM[k]);
			DML_LOG_VERBOSE("DML::%s: k=%d, LinesForDPTERow=%f (should not be >= 16)\n", __func__, k, inputs->LinesForDPTERow[k]);
			DML_LOG_VERBOSE("DML::%s: k=%d, DSTYAfterScaler=%d (should be <= 8)\n", __func__, k, inputs->DSTYAfterScaler[k]);
			DML_LOG_VERBOSE("DML::%s: k=%d, NoTimeForPrefetch=%d\n", __func__, k, inputs->NoTimeForPrefetch[k]);
		}
	}

	DML_LOG_DEBUG_BOOL(outputs->support.PrefetchScheduleSupported);
	DML_LOG_FUNC_EXIT();

	return outputs->support.PrefetchScheduleSupported;
}

static bool dcn6_ms_check_dynamic_metadata_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	unsigned int k;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	outputs->support.DynamicMetadataSupported = true;
	for (k = 0; k < display_cfg->num_planes; k++) {
		if (inputs->NoTimeForDynamicMetadata[k] == true) {
			outputs->support.DynamicMetadataSupported = false;
		}
	}

	DML_LOG_DEBUG_BOOL(outputs->support.DynamicMetadataSupported);
	DML_LOG_FUNC_EXIT();

	return outputs->support.DynamicMetadataSupported;
}

static bool dcn6_ms_check_v_ratio_in_prefetch_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;

	DML_LOG_FUNC_ENTER();
	outputs->support.VRatioInPrefetchSupported = true;
	for (k = 0; k < display_cfg->num_planes; k++) {
		if (inputs->VRatioPreY[k] > __DML2_CALCS_MAX_VRATIO_PRE__
				|| inputs->VRatioPreC[k] > __DML2_CALCS_MAX_VRATIO_PRE__) {
			outputs->support.VRatioInPrefetchSupported = false;
			DML_LOG_VERBOSE("DML::%s: k=%d VRatioPreY = %f (should be <= %f)\n", __func__, k, inputs->VRatioPreY[k], __DML2_CALCS_MAX_VRATIO_PRE__);
			DML_LOG_VERBOSE("DML::%s: k=%d VRatioPreC = %f (should be <= %f)\n", __func__, k, inputs->VRatioPreC[k], __DML2_CALCS_MAX_VRATIO_PRE__);
			DML_LOG_VERBOSE("DML::%s: VRatioInPrefetchSupported = %u\n", __func__, outputs->support.VRatioInPrefetchSupported);
		}
	}

	DML_LOG_DEBUG_BOOL(outputs->support.VRatioInPrefetchSupported);
	DML_LOG_FUNC_EXIT();

	return outputs->support.VRatioInPrefetchSupported;
}

static void dcn6_ms_calculate_urgent_burst_factor_for_prefetch(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	double line_time_us;
	const struct dml2_stream_parameters *stream;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		line_time_us = stream->timing.h_total / ((double) stream->timing.pixel_clock_khz / 1000);
		DML_LOG_VERBOSE("DML::%s: k=%d, Calling CalculateUrgentBurstFactor (for prefetch)\n", __func__, k);
		DML_LOG_VERBOSE("DML::%s: k=%d, VRatioPreY=%f\n", __func__, k, inputs->VRatioPreY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, VRatioPreC=%f\n", __func__, k, inputs->VRatioPreC[k]);
		dcn5_calculate_urgent_burst_factor(
				&display_cfg->plane_descriptors[k],
				inputs->swath_width_luma_ub[k],
				inputs->swath_width_chroma_ub[k],
				inputs->SwathHeightY[k],
				inputs->SwathHeightC[k],
				line_time_us,
				inputs->UrgLatency,
				inputs->VRatioPreY[k],
				inputs->VRatioPreC[k],
				inputs->BytePerPixelInDETY[k],
				inputs->BytePerPixelInDETC[k],
				inputs->DETBufferSizeY[k],
				inputs->DETBufferSizeC[k],
				/* Output */
				&outputs->UrgentBurstFactorLumaPre[k],
				&outputs->UrgentBurstFactorChromaPre[k],
				&outputs->NotEnoughUrgentLatencyHidingPre[k]);
	}

	DML_LOG_DEBUG_ARRAY_BOOL(outputs->NotEnoughUrgentLatencyHidingPre, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->UrgentBurstFactorLumaPre, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->UrgentBurstFactorChromaPre, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_peak_bandwidth_required(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_calcs_mode_support_locals *dummies = ctx->dummies;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	struct dml2_core_calcs_calculate_peak_bandwidth_required_params *p =
			&ctx->func_params->calculate_peak_bandwidth_params;

	DML_LOG_FUNC_ENTER();
	p->urg_vactive_bandwidth_required = outputs->support.urg_vactive_bandwidth_required;
	p->urg_bandwidth_required = outputs->support.urg_bandwidth_required;
	p->urg_bandwidth_required_qual = dummies->dummy_bw;
	p->non_urg_bandwidth_required = outputs->support.non_urg_bandwidth_required;
	p->surface_avg_vactive_required_bw = outputs->surface_avg_vactive_required_bw;
	p->surface_peak_required_bw = dummies->surface_dummy_bw;
	p->display_cfg = display_cfg;
	p->inc_flip_bw = 0;
	p->num_active_planes = display_cfg->num_planes;
	p->num_of_dpp = inputs->NoOfDPP;
	p->dcc_dram_bw_nom_overhead_factor_p0 = inputs->dcc_dram_bw_nom_overhead_factor_p0;
	p->dcc_dram_bw_nom_overhead_factor_p1 = inputs->dcc_dram_bw_nom_overhead_factor_p1;
	p->dcc_dram_bw_pref_overhead_factor_p0 = inputs->dcc_dram_bw_pref_overhead_factor_p0;
	p->dcc_dram_bw_pref_overhead_factor_p1 = inputs->dcc_dram_bw_pref_overhead_factor_p1;
	p->surface_read_bandwidth_l = inputs->vactive_sw_bw_l;
	p->surface_read_bandwidth_c = inputs->vactive_sw_bw_c;
	p->prefetch_bandwidth_l = inputs->RequiredPrefetchPixelDataBWLuma;
	p->prefetch_bandwidth_c = inputs->RequiredPrefetchPixelDataBWChroma;
	p->excess_vactive_fill_bw_l = inputs->excess_vactive_fill_bw_l;
	p->excess_vactive_fill_bw_c = inputs->excess_vactive_fill_bw_c;
	p->cursor_bw = inputs->cursor_bw;
	p->dpte_row_bw = inputs->dpte_row_bw;
	p->meta_row_bw = inputs->meta_row_bw;
	p->prefetch_cursor_bw = inputs->prefetch_cursor_bw;
	p->prefetch_vmrow_bw = inputs->prefetch_vmrow_bw;
	p->flip_bw = inputs->final_flip_bw;
	p->urgent_burst_factor_l = inputs->UrgentBurstFactorLuma;
	p->urgent_burst_factor_c = inputs->UrgentBurstFactorChroma;
	p->urgent_burst_factor_cursor = inputs->UrgentBurstFactorCursor;
	p->urgent_burst_factor_prefetch_l = inputs->UrgentBurstFactorLumaPre;
	p->urgent_burst_factor_prefetch_c = inputs->UrgentBurstFactorChromaPre;
	p->urgent_burst_factor_prefetch_cursor = inputs->UrgentBurstFactorCursorPre;
	dcn5_calculate_peak_bandwidth_required(ctx->func_params, p);

	p->urg_vactive_bandwidth_required = dummies->dummy_bw;
	p->urg_bandwidth_required = outputs->support.urg_bandwidth_required_flip;
	p->non_urg_bandwidth_required = outputs->support.non_urg_bandwidth_required_flip;
	p->surface_avg_vactive_required_bw = dummies->surface_dummy_bw;
	p->surface_peak_required_bw = outputs->surface_peak_required_bw;
	p->inc_flip_bw = 1;
	dcn5_calculate_peak_bandwidth_required(ctx->func_params, p);

	DML_LOG_DEBUG_2D_ARRAY_DOUBLE(outputs->support.urg_bandwidth_required,
				dml2_core_internal_soc_state_max, dml2_core_internal_bw_max);
	DML_LOG_DEBUG_3D_ARRAY_DOUBLE(outputs->surface_avg_vactive_required_bw,
			dml2_core_internal_soc_state_max, dml2_core_internal_bw_max, display_cfg->num_planes);
	DML_LOG_DEBUG_2D_ARRAY_DOUBLE(outputs->support.non_urg_bandwidth_required,
			dml2_core_internal_soc_state_max, dml2_core_internal_bw_max);
	DML_LOG_DEBUG_2D_ARRAY_DOUBLE(outputs->support.urg_vactive_bandwidth_required,
			dml2_core_internal_soc_state_max, dml2_core_internal_bw_max);
	DML_LOG_DEBUG_2D_ARRAY_DOUBLE(outputs->support.urg_bandwidth_required_qual,
			dml2_core_internal_soc_state_max, dml2_core_internal_bw_max);
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_final_prefetch_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	unsigned int k;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	outputs->support.PrefetchSupported = inputs->support.PrefetchScheduleSupported;

	for (k = 0; k < display_cfg->num_planes; k++) {
		if (inputs->NotEnoughUrgentLatencyHidingPre[k]) {
			outputs->support.PrefetchSupported = false;
			DML_LOG_VERBOSE("DML::%s: k=%d, NotEnoughUrgentLatencyHidingPre=%d\n", __func__, k, inputs->NotEnoughUrgentLatencyHidingPre[k]);
		}
	}

	DML_LOG_DEBUG_BOOL(outputs->support.PrefetchSupported);
	DML_LOG_FUNC_EXIT();

	return outputs->support.PrefetchSupported;
}

static void dcn6_ms_calculate_flip_schedule(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;

	DML_LOG_FUNC_ENTER();
	outputs->TotImmediateFlipBytes = 0;
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];
		dcn6_calculate_flip_schedule(
				ctx->func_params,
				display_cfg->plane_descriptors[k].immediate_flip,
				display_cfg->hostvm_enable,
				display_cfg->ffbm_enable,
				inputs->HostVMInefficiencyFactor,
				inputs->Tvm_trips_flip[k],
				inputs->Tr0_trips_flip[k],
				inputs->Tvm_trips_flip_rounded[k],
				inputs->Tr0_trips_flip_rounded[k],
				display_cfg->gpuvm_enable,
				inputs->vm_bytes[k],
				inputs->DPTEBytesPerRow[k],
				plane->pixel_format,
				(stream->timing.h_total / ((double) stream->timing.pixel_clock_khz / 1000)),
				plane->composition.scaler_info.plane0.v_ratio,
				plane->composition.scaler_info.plane1.v_ratio,
				inputs->Tno_bw_flip[k],
				inputs->dpte_row_height[k],
				inputs->dpte_row_height_chroma[k],
				ip->max_flip_time_us,
				ip->max_flip_time_lines,
				inputs->meta_row_height_luma[k],
				inputs->meta_row_height_chroma[k],

				/* Output */
				&outputs->dst_y_per_vm_flip[k],
				&outputs->dst_y_per_row_flip[k],
				&outputs->final_flip_bw[k],
				&outputs->ImmediateFlipSupportedForPipe[k]);
	}

	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->dst_y_per_vm_flip, display_cfg->num_planes);
	DML_LOG_DEBUG_UINT(outputs->TotImmediateFlipBytes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->ImmediateFlipSupportedForPipe, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->final_flip_bw, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->dst_y_per_row_flip, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_immediate_flip_support(const struct dml2_core_calculate_ms_context *ctx,
	struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int k;

	DML_LOG_FUNC_ENTER();
	outputs->support.ImmediateFlipSupport = true;
	for (k = 0; k < display_cfg->num_planes; ++k) {
		if ((display_cfg->plane_descriptors[k].immediate_flip || (display_cfg->gpuvm_enable && (display_cfg->hostvm_enable || display_cfg->ffbm_enable)))
			&& inputs->ImmediateFlipSupportedForPipe[k] == false) {
			outputs->support.ImmediateFlipSupport = false;
			DML_LOG_VERBOSE("DML::%s: Pipe %0u not supporting iflip!\n", __func__, k);
		}
	}

	DML_LOG_DEBUG_BOOL(outputs->support.ImmediateFlipSupport);
	DML_LOG_FUNC_EXIT();
	return outputs->support.ImmediateFlipSupport;
}

static void dcn6_ms_calculate_bandwidth_upper_bound(const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	outputs->support.bandwidth_upper_bound.dcn5.non_urgent_bandwidth_kbps =
		(**inputs->support.non_urg_bandwidth_required_flip * 1000);
	outputs->support.bandwidth_upper_bound.dcn5.urgent_bandwidth_kbps =
		math_max3(**inputs->support.urg_bandwidth_required_flip,
			**inputs->support.non_urg_bandwidth_required / ctx->soc_bb->fraction_of_urgent_bandwidth_nominal_target,
			**inputs->support.non_urg_bandwidth_required_flip / ctx->soc_bb->fraction_of_urgent_bandwidth_flip_target) * 1000;
	DML_LOG_DEBUG_DOUBLE(outputs->support.bandwidth_upper_bound.dcn5.non_urgent_bandwidth_kbps);
	DML_LOG_DEBUG_DOUBLE(outputs->support.bandwidth_upper_bound.dcn5.urgent_bandwidth_kbps);
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_qos_bandwidth_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_sop_table *sop_table = &ctx->soc_bb->sop_table;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	outputs->support.qos_bandwidth_support = sop_table->is_bw_supported_at_index(sop_table,
			&inputs->support.bandwidth_upper_bound, inputs->qos_param_index);

	DML_LOG_DEBUG_BOOL(outputs->support.qos_bandwidth_support);
	DML_LOG_FUNC_EXIT();

	return outputs->support.qos_bandwidth_support;
}

static bool dcn6_ms_check_reordering_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	outputs->support.ROBSupport = true;

	//Re-ordering Buffer Support Check
	if (((ip->rob_buffer_size_kbytes - ip->pixel_chunk_size_kbytes) * 1024
			/ **inputs->support.non_urg_bandwidth_required_flip) >= inputs->support.max_urgent_latency_us) {
		outputs->support.ROBSupport = true;
	} else {
		outputs->support.ROBSupport = false;
	}

	DML_LOG_VERBOSE("DML::%s: max_urgent_latency_us = %f\n", __func__, inputs->support.max_urgent_latency_us);
	DML_LOG_VERBOSE("DML::%s: ROBSupport = %u\n", __func__, outputs->support.ROBSupport);

	DML_LOG_DEBUG_BOOL(outputs->support.ROBSupport);
	DML_LOG_FUNC_EXIT();

	return outputs->support.ROBSupport;
}

static void dcn6_ms_calculate_vactive_det_fill_latency(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	enum dml2_pstate_type pstate_type;

	DML_LOG_FUNC_ENTER();
	/* VActive fill time calculations (informative) */
	for (pstate_type = 0; pstate_type < dml2_pstate_type_count; pstate_type++) {
		dcn5_calculate_vactive_det_fill_latency(
				display_cfg,
				display_cfg->num_planes,
				inputs->pstate_bytes_required_l[pstate_type],
				inputs->pstate_bytes_required_c[pstate_type],
				inputs->dcc_dram_bw_nom_overhead_factor_p0,
				inputs->dcc_dram_bw_nom_overhead_factor_p1,
				inputs->vactive_sw_bw_l,
				inputs->vactive_sw_bw_c,
				**inputs->surface_avg_vactive_required_bw,
				**inputs->surface_peak_required_bw,
				/* outputs */
				outputs->pstate_vactive_det_fill_delay_us[pstate_type]);
	}

	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->pstate_vactive_det_fill_delay_us[dml2_pstate_type_uclk], display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->pstate_vactive_det_fill_delay_us[dml2_pstate_type_fclk], display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->pstate_vactive_det_fill_delay_us[dml2_pstate_type_ppt], display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->pstate_vactive_det_fill_delay_us[dml2_pstate_type_temp_read], display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_mode_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states,
		enum dml2_status status)
{
	(void)ctx;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;

	DML_LOG_FUNC_ENTER();
	if (status != DML2_STATUS_OK)
		goto fail;
	/*Mode Support, Voltage State and SOC Configuration*/
	if (inputs->support.ScaleRatioAndTapsSupport == false)
		goto fail;
	if (inputs->support.SourceFormatPixelAndScanSupport == false)
		goto fail;
	if (inputs->support.ViewportSizeSupport == false)
		goto fail;
	if (inputs->support.LinkCapacitySupport == false)
		goto fail;
	if (inputs->support.LinkRateDoesNotMatchDPVersion)
		goto fail;
	if (inputs->support.MSOOrODMSplitWithNonDPLink)
		goto fail;
	if (inputs->support.NotEnoughLanesForMSO)
		goto fail;
	if (inputs->support.P2IWith420)
		goto fail;
	if (inputs->support.DSC422NativeNotSupported)
		goto fail;
	if (inputs->support.DSCSlicesODMModeSupported == false)
		goto fail;
	if (inputs->support.NotEnoughDSCUnits)
		goto fail;
	if (inputs->support.NotEnoughDSCSlices)
		goto fail;
	if (inputs->support.DSCCLKRequiredMoreThanSupported)
		goto fail;
	if (inputs->support.PixelsPerLinePerDSCUnitSupport == false)
		goto fail;
	if (inputs->support.DTBCLKRequiredMoreThanSupported)
		goto fail;
	if (inputs->support.ROBSupport == false)
		goto fail;
	if (inputs->support.OutstandingRequestsSupport == false)
		goto fail;
	if (inputs->support.OutstandingRequestsUrgencyAvoidance == false)
		goto fail;
	if (inputs->support.DISPCLK_DPPCLK_Support == false)
		goto fail;
	if (inputs->support.TotalAvailablePipesSupport == false)
		goto fail;
	if (inputs->support.NumberOfTDLUT33cubeSupport == false)
		goto fail;
	if (inputs->support.ODMSupport == false)
		goto fail;
	if (inputs->support.NumberOfOTGSupport == false)
		goto fail;
	if (inputs->support.NumberOfHDMIFRLSupport == false)
		goto fail;
	if (inputs->support.NumberOfDP2p0Support == false)
		goto fail;
	if (inputs->support.EnoughWritebackUnits == false)
		goto fail;
	if (inputs->support.WritebackLatencySupport == false)
		goto fail;
	if (inputs->support.WritebackScaleRatioAndTapsSupport == false)
		goto fail;
	if (inputs->support.CursorSupport == false)
		goto fail;
	if (inputs->support.PitchSupport == false)
		goto fail;
	if (inputs->support.ViewportExceedsSurface)
		goto fail;
	if (inputs->support.PrefetchSupported == false)
		goto fail;
	if (inputs->support.EnoughUrgentLatencyHidingSupport == false)
		goto fail;
	if (inputs->support.DynamicMetadataSupported == false)
		goto fail;
	if (inputs->support.VRatioInPrefetchSupported == false)
		goto fail;
	if (inputs->support.PTEBufferSizeNotExceeded == false)
		goto fail;
	if (inputs->support.DCCMetaBufferSizeNotExceeded == false)
		goto fail;
	if (inputs->support.global_temp_read_or_ppt_supported == false)
		goto fail;
	if (inputs->support.dcfclk_support == false)
		goto fail;
	if (inputs->support.qos_bandwidth_support == false)
		goto fail;
	if (inputs->support.global_dram_clock_change_supported == false)
		if (inputs->support.global_dram_clock_change_support_required)
			goto fail;
	if (inputs->support.alternate_channel_size_support == false)
		goto fail;

	DML_LOG_VERBOSE("DML::%s: mode is supported\n", __func__);
	outputs->support.ModeSupport = true;

	DML_LOG_DEBUG_BOOL(outputs->support.ModeSupport);
	DML_LOG_FUNC_EXIT();

	return outputs->support.ModeSupport;
fail:
	dml2_core_utils_print_mode_support_info(&inputs->support, true);
	DML_LOG_VERBOSE("DML::%s: mode is NOT supported\n", __func__);
	outputs->support.ModeSupport = false;

	DML_LOG_DEBUG_BOOL(outputs->support.ModeSupport);
	DML_LOG_FUNC_EXIT();

	return outputs->support.ModeSupport;
}

static void dcn6_ms_populate_informative(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	unsigned int k;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		outputs->support.MPCCombineEnable[k] = inputs->MPCCombine[k];
		outputs->support.DPPPerSurface[k] = inputs->NoOfDPP[k];
	}
	for (k = 0; k < display_cfg->num_planes; k++) {
		outputs->support.ODMMode[k] = inputs->ODMMode[k];
		outputs->support.DSCEnabled[k] = inputs->RequiresDSC[k];
		outputs->support.FECEnabled[k] = inputs->RequiresFEC[k];
		outputs->support.OutputBpp[k] = inputs->OutputBpp[k];
		outputs->support.OutputType[k] = inputs->OutputType[k];
		outputs->support.OutputRate[k] = inputs->OutputRate[k];
		DML_LOG_VERBOSE("DML::%s: k=%d, ODMMode = %u\n", __func__, k, outputs->support.ODMMode[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, DSCEnabled = %u\n", __func__, k, outputs->support.DSCEnabled[k]);
	}

	DML_LOG_DEBUG_ARRAY_BOOL(outputs->support.FECEnabled, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_INT(outputs->support.ODMMode, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_INT(outputs->support.OutputType, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->support.DPPPerSurface, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->support.OutputBpp, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_INT(outputs->support.OutputRate, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->support.MPCCombineEnable, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_BOOL(outputs->support.DSCEnabled, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_get_plane_support_info(
		const struct dml2_core_calculate_ms_context *ctx,
		const struct dml2_core_internal_mode_support *states,
		struct core_plane_support_info *plane_support,
		unsigned int plane_idx)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_internal_mode_support *inputs = states;
	const struct dml2_stream_parameters *stream =
			&display_cfg->stream_descriptors[display_cfg->plane_descriptors[plane_idx].stream_index];

	DML_LOG_FUNC_ENTER();
	plane_support->nominal_vblank_pstate_latency_hiding_us = (int)(
			stream->timing.h_total
			/ ((double)stream->timing.pixel_clock_khz / 1000)
			* inputs->TWait[plane_idx]);

	DML_LOG_VERBOSE("DML::%s: plane_idx=%d, VActiveLatencyHidingMargin = %f\n", __func__, plane_idx, inputs->VActiveLatencyHidingMargin[plane_idx]);
	plane_support->dram_change_latency_hiding_margin_in_active = (int)inputs->VActiveLatencyHidingMargin[plane_idx];

	plane_support->active_latency_hiding_us = (int)inputs->VActiveLatencyHidingUs[plane_idx];

	plane_support->vactive_det_fill_delay_us[dml2_pstate_type_uclk] = (unsigned int)math_ceil(
			inputs->pstate_vactive_det_fill_delay_us[dml2_pstate_type_uclk][plane_idx]);
	plane_support->vactive_det_fill_delay_us[dml2_pstate_type_fclk] = (unsigned int)math_ceil(
		inputs->pstate_vactive_det_fill_delay_us[dml2_pstate_type_fclk][plane_idx]);
	plane_support->vactive_det_fill_delay_us[dml2_pstate_type_ppt] = (unsigned int)math_ceil(
		inputs->pstate_vactive_det_fill_delay_us[dml2_pstate_type_ppt][plane_idx]);
	plane_support->vactive_det_fill_delay_us[dml2_pstate_type_temp_read] = (unsigned int)math_ceil(
		inputs->pstate_vactive_det_fill_delay_us[dml2_pstate_type_temp_read][plane_idx]);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_get_stream_support_info(
		const struct dml2_core_calculate_ms_context *ctx,
		const struct dml2_core_internal_mode_support *states,
		struct core_stream_support_info *stream_support,
		unsigned int plane_index)
{
	(void)states;
	DML_LOG_FUNC_ENTER();
	stream_support->vblank_reserved_time_us =
			ctx->display_cfg->plane_descriptors[plane_index].overrides.reserved_vblank_time_ns / 1000;
	DML_LOG_VERBOSE("DML::%s: vblank_reserved_time_us = %u\n", __func__, stream_support->vblank_reserved_time_us);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_populate_mode_support_result(
		const struct dml2_core_calculate_ms_context *ctx,
		const struct dml2_core_internal_mode_support *states,
		struct dml2_core_mode_support_result *result)
{
	unsigned int i, stream_index, stream_bitmask;
	int unsigned odm_count, num_odm_output_segments, dpp_count;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_internal_mode_support *inputs = states;
	double max_dst_y_pre = 0;
	unsigned int max_dst_y_after_scaler = 0;

	DML_LOG_FUNC_ENTER();
	result->global.dispclk_khz = (unsigned int) math_ceil(inputs->RequiredDISPCLK * 1000);
	result->global.dpprefclk_khz = (unsigned int)math_ceil(inputs->GlobalDPPCLK * 1000);
	result->global.dtbrefclk_khz = (unsigned int)math_ceil(inputs->GlobalDTBCLK * 1000);
	result->global.dcfclk_deepsleep_khz = (unsigned int)math_ceil(inputs->dcfclk_deepsleep * 1000);
	result->global.active.dcfclk_khz = (unsigned int)math_ceil(inputs->DCFCLK * 1000);
	result->global.fclk_pstate_supported = inputs->support.global_fclk_change_supported;
	result->global.uclk_pstate_supported = inputs->support.global_dram_clock_change_supported;

	result->global.active.average_bw_sdp_kbps = (unsigned long)math_ceil2((**inputs->support.avg_bandwidth_required * 1000), 1.0);
	result->global.active.urgent_bw_sdp_kbps = (unsigned long)math_ceil2((**inputs->support.urg_bandwidth_required_flip * 1000), 1.0);

	result->global.active.average_bw_dram_kbps = (unsigned long)math_ceil2((**inputs->support.avg_bandwidth_required * 1000), 1.0);
	result->global.active.urgent_bw_dram_kbps = (unsigned long)math_ceil2((**inputs->support.urg_bandwidth_required_flip * 1000), 1.0);
	result->global.alternate_total_bytes_copy_svp0 = inputs->svp0_max_bytes;
	result->global.alternate_total_bytes_copy_svp1 = inputs->svp1_max_bytes;
	result->global.lsdma_bw_req_for_alt_kbps = (unsigned int)inputs->lsdma_bw_req_for_alt_kbps;
	DML_LOG_VERBOSE("DML::%s: result->global.active.urgent_bw_sdp_kbps = %ld\n", __func__, result->global.active.urgent_bw_sdp_kbps);
	DML_LOG_VERBOSE("DML::%s: result->global.svp_prefetch.urgent_bw_sdp_kbps = %ld\n", __func__, result->global.svp_prefetch.urgent_bw_sdp_kbps);
	DML_LOG_VERBOSE("DML::%s: result->global.active.urgent_bw_dram_kbps = %ld\n", __func__, result->global.active.urgent_bw_dram_kbps);
	DML_LOG_VERBOSE("DML::%s: result->global.svp_prefetch.urgent_bw_dram_kbps = %ld\n", __func__, result->global.svp_prefetch.urgent_bw_dram_kbps);

	memcpy(&result->global.watermarks, &inputs->support.watermarks, sizeof(inputs->support.watermarks));

	for (i = 0; i < display_cfg->num_planes; i++) {
		result->per_plane[i].dppclk_khz = (unsigned int)(inputs->RequiredDPPCLK[i] * 1000);
	}

	for (stream_index = 0; stream_index < display_cfg->num_streams; stream_index++) {
		for (i = 0; i < display_cfg->num_planes; i++) {
			if (inputs->DSTYAfterScaler[i] > max_dst_y_after_scaler)
				max_dst_y_after_scaler = inputs->DSTYAfterScaler[i];
			if (inputs->dst_y_prefetch[i] > max_dst_y_pre)
				max_dst_y_pre = inputs->dst_y_prefetch[i];
		}
		result->cfg_support_info.stream_support_info[stream_index].max_dst_y_after_scaler = (unsigned int)max_dst_y_after_scaler + 1;
		result->cfg_support_info.stream_support_info[stream_index].max_dst_y_prefetch = (unsigned int)max_dst_y_pre + 1;
	}

	stream_bitmask = 0;
	for (i = 0; i < display_cfg->num_planes; i++) {
		odm_count = 1;
		dpp_count = inputs->support.DPPPerSurface[i];
		num_odm_output_segments = 1;

		switch (inputs->support.ODMMode[i]) {
		case dml2_odm_mode_bypass:
			odm_count = 1;
			dpp_count = inputs->support.DPPPerSurface[i];
			break;
		case dml2_odm_mode_combine_2to1:
			odm_count = 2;
			dpp_count = 2;
			break;
		case dml2_odm_mode_combine_3to1:
			odm_count = 3;
			dpp_count = 3;
			break;
		case dml2_odm_mode_combine_4to1:
			odm_count = 4;
			dpp_count = 4;
			break;
		case dml2_odm_mode_split_1to2:
		case dml2_odm_mode_mso_1to2:
			num_odm_output_segments = 2;
			break;
		case dml2_odm_mode_mso_1to4:
			num_odm_output_segments = 4;
			break;
		case dml2_odm_mode_auto:
		default:
			odm_count = 1;
			dpp_count = inputs->support.DPPPerSurface[i];
			break;
		}

		result->cfg_support_info.plane_support_info[i].dpps_used = dpp_count;

		dcn6_ms_get_plane_support_info(ctx, states, &result->cfg_support_info.plane_support_info[i], i);

		stream_index = display_cfg->plane_descriptors[i].stream_index;

		result->per_stream[stream_index].dscclk_khz = (unsigned int)inputs->required_dscclk_freq_mhz[i] * 1000;
		DML_LOG_VERBOSE("CORE_DCN4::%s: i=%d stream_index=%d, result->per_stream[stream_index].dscclk_khz = %u\n", __func__, i, stream_index, result->per_stream[stream_index].dscclk_khz);

		if (!((stream_bitmask >> stream_index) & 0x1)) {
			result->cfg_support_info.stream_support_info[stream_index].odms_used = odm_count;
			result->cfg_support_info.stream_support_info[stream_index].num_odm_output_segments = num_odm_output_segments;
			result->cfg_support_info.stream_support_info[stream_index].dsc_enable = inputs->support.DSCEnabled[i];
			result->cfg_support_info.stream_support_info[stream_index].num_dsc_slices = inputs->support.NumberOfDSCSlices[i];
			result->cfg_support_info.stream_support_info[stream_index].alternate_svp0_dst_lines = inputs->svp0_dst_lines[stream_index];
			result->cfg_support_info.stream_support_info[stream_index].alternate_svp1_dst_lines = inputs->svp1_dst_lines[stream_index];
			result->cfg_support_info.stream_support_info[stream_index].max_vstartup_lines = inputs->MaxVStartupLines[i];
			dcn6_ms_get_stream_support_info(ctx, states, &result->cfg_support_info.stream_support_info[stream_index], i);
			result->per_stream[stream_index].dtbclk_khz = (unsigned int)(inputs->RequiredDTBCLK[i] * 1000);
			stream_bitmask |= 0x1 << stream_index;
		}
	}
	result->bandwidth_upper_bound = inputs->support.bandwidth_upper_bound;
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_dcfclk_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	double urg_bandwidth_required_MBps = inputs->support.bandwidth_upper_bound.dcn5.urgent_bandwidth_kbps / 1000.0;
	double non_urg_bandwidth_required_MBps = inputs->support.bandwidth_upper_bound.dcn5.non_urgent_bandwidth_kbps / 1000.0;
	double min_urgent_dcfclk_mhz = urg_bandwidth_required_MBps
			/ (soc_bb->urgent_sdp_derate_percent / 100.0)
			/ soc_bb->return_bus_width_bytes;
	double min_nominal_dcfclk_mhz = non_urg_bandwidth_required_MBps
			/ (soc_bb->nominal_sdp_derate_percent / 100.0)
			/ soc_bb->return_bus_width_bytes;
	double min_required_dcfclk_mhz = math_max2(min_urgent_dcfclk_mhz, min_nominal_dcfclk_mhz);

	DML_LOG_FUNC_ENTER();
	outputs->support.dcfclk_support = inputs->DCFCLK >= min_required_dcfclk_mhz
			&& inputs->DCFCLK <= soc_bb->max_dcfclk_khz / 1000.0
			&& inputs->DCFCLK >= soc_bb->min_dcfclk_khz / 1000.0;

	DML_LOG_VERBOSE("outputs->support.dcfclk_support = %s\n", outputs->support.dcfclk_support ? "true" : "false");

	DML_LOG_DEBUG_BOOL(outputs->support.dcfclk_support);
	DML_LOG_FUNC_EXIT();

	return outputs->support.dcfclk_support;
}

static void dcn6_ms_check_alternate_channel_size_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	unsigned int i;
	bool alt_chan_in_use = false;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	unsigned int alternate_carveout_size_bytes = soc_bb->power_management_parameters.alternate_dram_carveout_size_mb < 0xFFF ?
												soc_bb->power_management_parameters.alternate_dram_carveout_size_mb << 20 : 0xFFFFFFFF;

	DML_LOG_FUNC_ENTER();
	outputs->support.alternate_channel_size_support = true;

	//Alternate Channel Size Support Check - only fail if alternate channels are used AND exceed carveout limit
	for (i = 0; i < ctx->display_cfg->num_planes; i++) {
		if (ctx->display_cfg->plane_descriptors[i].overrides.uclk_pstate_change_strategy == dml2_uclk_pstate_change_strategy_force_alternate) {
			alt_chan_in_use = true;
			break;
		}
	}

	if (alt_chan_in_use && (inputs->svp0_max_bytes > alternate_carveout_size_bytes ||
					inputs->svp1_max_bytes > alternate_carveout_size_bytes)) {
		outputs->support.alternate_channel_size_support = false;
	}

	DML_LOG_VERBOSE("outputs->support.alternate_channel_size_support = %s\n", outputs->support.alternate_channel_size_support ? "true" : "false");

	DML_LOG_DEBUG_BOOL(outputs->support.alternate_channel_size_support);
	DML_LOG_FUNC_EXIT();

	return;
}

static void dcn6_ms_calculate_watermarks(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	struct dml2_core_calcs_mode_support_locals *dummies = ctx->dummies;
	const struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	struct dml2_core_calcs_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params *p =
			&ctx->func_params->CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params;

	DML_LOG_FUNC_ENTER();
	dummies->mSOCParameters.UrgentLatency = inputs->UrgLatency;
	dummies->mSOCParameters.ExtraLatency = inputs->ExtraLatency;
	dummies->mSOCParameters.ExtraLatency_sr = inputs->ExtraLatency_sr;
	dummies->mSOCParameters.WritebackLatency = soc_bb->writeback_base_latency_us;
	dummies->mSOCParameters.DRAMClockChangeLatency =
			soc_bb->power_management_parameters.dram_clk_change_blackout_us;
	dummies->mSOCParameters.FCLKChangeLatency = soc_bb->power_management_parameters.fclk_change_blackout_us;
	dummies->mSOCParameters.SRExitTime = soc_bb->power_management_parameters.stutter_exit_latency_us;
	dummies->mSOCParameters.SREnterPlusExitTime =
			soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us;
	dummies->mSOCParameters.SRExitZ8Time = soc_bb->power_management_parameters.z8_stutter_exit_latency_us;
	dummies->mSOCParameters.SREnterPlusExitZ8Time =
			soc_bb->power_management_parameters.z8_stutter_enter_plus_exit_latency_us;
	dummies->mSOCParameters.SRExitTimeLowPower = soc_bb->power_management_parameters.low_power_stutter_exit_latency_us;
	dummies->mSOCParameters.SREnterPlusExitTimeLowPower =
			soc_bb->power_management_parameters.low_power_stutter_enter_plus_exit_latency_us;
	dummies->mSOCParameters.USRRetrainingLatency = 0;
	dummies->mSOCParameters.SMNLatency = 0;
	dummies->mSOCParameters.temp_read_or_ppt_blackout_us
		= math_max2(soc_bb->power_management_parameters.g7_ppt_blackout_us, soc_bb->power_management_parameters.g7_temperature_read_blackout_us);
	dummies->mSOCParameters.max_urgent_latency_us = inputs->support.max_urgent_latency_us;
	dummies->mSOCParameters.df_response_time_us = inputs->support.df_response_time_us;
	dummies->mSOCParameters.qos_type = dml2_qos_param_type_dcn4x;

	p->display_cfg = display_cfg;
	p->USRRetrainingRequired = false;
	p->NumberOfActiveSurfaces = display_cfg->num_planes;
	p->MaxLineBufferLines = ip->max_line_buffer_lines;
	p->LineBufferSize = ip->line_buffer_size_bits;
	p->WritebackInterfaceBufferSize = ip->writeback_interface_buffer_size_kbytes;
	p->DCFCLK = inputs->DCFCLK;
	p->SynchronizeTimings = display_cfg->overrides.synchronize_timings;
	p->SynchronizeDRRDisplaysForUCLKPStateChange =
			display_cfg->overrides.synchronize_ddr_displays_for_uclk_pstate_change;
	p->dpte_group_bytes = inputs->dpte_group_bytes;
	p->mmSOCParameters = dummies->mSOCParameters;
	p->WritebackChunkSize = ip->writeback_chunk_size_kbytes;
	p->SOCCLK = 0.0;
	p->DCFClkDeepSleep = inputs->dcfclk_deepsleep;
	p->DETBufferSizeY = inputs->DETBufferSizeY;
	p->DETBufferSizeC = inputs->DETBufferSizeC;
	p->SwathHeightY = inputs->SwathHeightY;
	p->SwathHeightC = inputs->SwathHeightC;
	//CalculateWatermarks_params->LBBitPerPixel = 57; // FIXME_STAGE2, need a new ip param?
	p->SwathWidthY = inputs->SwathWidthY;
	p->SwathWidthC = inputs->SwathWidthC;
	p->DPPPerSurface = inputs->NoOfDPP;
	p->BytePerPixelDETY = inputs->BytePerPixelInDETY;
	p->BytePerPixelDETC = inputs->BytePerPixelInDETC;
	p->DSTXAfterScaler = inputs->DSTXAfterScaler;
	p->DSTYAfterScaler = inputs->DSTYAfterScaler;
	p->UnboundedRequestEnabled = inputs->UnboundedRequestEnabled;
	p->CompressedBufferSizeInkByte = inputs->CompressedBufferSizeInkByte;
	p->meta_row_height_l = inputs->meta_row_height_luma;
	p->meta_row_height_c = inputs->meta_row_height_chroma;
	p->uclk_pstate_switch_modes = inputs->uclk_pstate_switch_modes;
	// Output
	p->Watermark = &outputs->support.watermarks; // Watermarks *Watermark
	p->DRAMClockChangeSupport = outputs->support.DRAMClockChangeSupport;
	p->global_dram_clock_change_support_required = &outputs->support.global_dram_clock_change_support_required;
	p->global_dram_clock_change_supported = &outputs->support.global_dram_clock_change_supported;
	p->MaxActiveDRAMClockChangeLatencySupported = outputs->MaxActiveDRAMClockChangeLatencySupported; // double *MaxActiveDRAMClockChangeLatencySupported[]
	p->FCLKChangeSupport = outputs->support.FCLKChangeSupport;
	p->global_fclk_change_supported = &outputs->support.global_fclk_change_supported;
	p->MaxActiveFCLKChangeLatencySupported = &outputs->MaxActiveFCLKChangeLatencySupported; // double *MaxActiveFCLKChangeLatencySupported
	p->USRRetrainingSupport = &outputs->support.USRRetrainingSupport;
	p->global_temp_read_or_ppt_supported = &outputs->support.global_temp_read_or_ppt_supported;
	p->temp_read_or_ppt_support = outputs->support.temp_read_or_ppt_support;
	p->VActiveLatencyHidingMargin = outputs->VActiveLatencyHidingMargin;
	p->VActiveLatencyHidingUs = outputs->VActiveLatencyHidingUs;
	dcn6_calculate_watermarks_and_dram_speed_change_support(ctx->func_params, p);

	DML_LOG_DEBUG_BOOL(outputs->support.global_dram_clock_change_supported);
	DML_LOG_DEBUG_BOOL(outputs->support.global_fclk_change_supported);
	DML_LOG_DEBUG_DOUBLE(outputs->support.watermarks.UrgentWatermark);
	DML_LOG_DEBUG_DOUBLE(outputs->support.watermarks.WritebackUrgentWatermark);
	DML_LOG_DEBUG_DOUBLE(outputs->support.watermarks.DRAMClockChangeWatermark);
	DML_LOG_DEBUG_DOUBLE(outputs->support.watermarks.FCLKChangeWatermark);
	DML_LOG_DEBUG_DOUBLE(outputs->support.watermarks.WritebackDRAMClockChangeWatermark);
	DML_LOG_DEBUG_DOUBLE(outputs->support.watermarks.WritebackFCLKChangeWatermark);
	DML_LOG_DEBUG_DOUBLE(outputs->support.watermarks.StutterExitWatermark);
	DML_LOG_DEBUG_DOUBLE(outputs->support.watermarks.StutterEnterPlusExitWatermark);
	DML_LOG_DEBUG_DOUBLE(outputs->support.watermarks.LowPowerStutterExitWatermark);
	DML_LOG_DEBUG_DOUBLE(outputs->support.watermarks.LowPowerStutterEnterPlusExitWatermark);
	DML_LOG_DEBUG_DOUBLE(outputs->support.watermarks.Z8StutterExitWatermark);
	DML_LOG_DEBUG_DOUBLE(outputs->support.watermarks.Z8StutterEnterPlusExitWatermark);
	DML_LOG_DEBUG_DOUBLE(outputs->support.watermarks.USRRetrainingWatermark);
	DML_LOG_DEBUG_DOUBLE(outputs->support.watermarks.temp_read_or_ppt_watermark_us);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->VActiveLatencyHidingMargin, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_INT(outputs->support.DRAMClockChangeSupport, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_UINT(outputs->SubViewportLinesNeededInMALL, display_cfg->num_planes);
	DML_LOG_DEBUG_BOOL(outputs->support.global_temp_read_or_ppt_supported);
	DML_LOG_DEBUG_BOOL(outputs->support.USRRetrainingSupport);
	DML_LOG_DEBUG_ARRAY_INT(outputs->support.FCLKChangeSupport, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->VActiveLatencyHidingUs, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->MaxActiveDRAMClockChangeLatencySupported, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static void dcn6_ms_calculate_pstate_schedule_windows(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	const struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	struct dml2_core_calcs_mode_support_locals *dummies = ctx->dummies;
	unsigned int *v_blank_start = dummies->dummy_integer_array[0];
	unsigned int *v_blank_end = dummies->dummy_integer_array[1];
	double *otg_vline_time_us = dummies->dummy_double_array[0];
	double *reserved_vblank_us = dummies->dummy_double_array[1];
	const struct dml2_plane_parameters *plane = NULL;
	const struct dml2_stream_parameters *stream = NULL;
	unsigned int k;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];
		v_blank_start[k] = stream->timing.v_blank_end + stream->timing.v_active;
		v_blank_end[k] = stream->timing.v_blank_end;
		otg_vline_time_us[k] = (double)stream->timing.h_total / stream->timing.pixel_clock_khz * 1000.0;
		reserved_vblank_us[k] = plane->overrides.reserved_vblank_time_ns / 1000.0;
	}

	/* fclk pstate */
	if (inputs->support.global_fclk_change_supported)
		dcn6_calculate_pstate_schedule_windows(
			display_cfg->num_planes,
			v_blank_start,
			v_blank_end,
			otg_vline_time_us,
			inputs->pstate_vactive_det_fill_delay_us[dml2_pstate_type_fclk],
			reserved_vblank_us,
			soc_bb->power_management_parameters.fclk_change_blackout_us,
			// Outputs
			outputs->fclk_pstate_allow_start_us,
			outputs->fclk_pstate_allow_end_us
		);

	/* ppt pstate */
	if (inputs->support.global_temp_read_or_ppt_supported)
		dcn6_calculate_pstate_schedule_windows(
			display_cfg->num_planes,
			v_blank_start,
			v_blank_end,
			otg_vline_time_us,
			inputs->pstate_vactive_det_fill_delay_us[dml2_pstate_type_ppt],
			reserved_vblank_us,
			math_max2(
				soc_bb->power_management_parameters.g7_ppt_blackout_us,
				soc_bb->power_management_parameters.g7_temperature_read_blackout_us),
			// Outputs
			outputs->ppt_pstate_allow_start_us,
			outputs->ppt_pstate_allow_end_us
		);

	/* temp read pstate */
	if (inputs->support.global_temp_read_or_ppt_supported)
		dcn6_calculate_pstate_schedule_windows(
			display_cfg->num_planes,
			v_blank_start,
			v_blank_end,
			otg_vline_time_us,
			inputs->pstate_vactive_det_fill_delay_us[dml2_pstate_type_temp_read],
			reserved_vblank_us,
			math_max2(soc_bb->power_management_parameters.g7_ppt_blackout_us,
					soc_bb->power_management_parameters.g7_temperature_read_blackout_us),
			// Outputs
			outputs->temp_read_pstate_allow_start_us,
			outputs->temp_read_pstate_allow_end_us
		);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->fclk_pstate_allow_start_us, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->fclk_pstate_allow_end_us, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->ppt_pstate_allow_start_us, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->ppt_pstate_allow_end_us, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->temp_read_pstate_allow_start_us, display_cfg->num_planes);
	DML_LOG_DEBUG_ARRAY_DOUBLE(outputs->temp_read_pstate_allow_end_us, display_cfg->num_planes);
	DML_LOG_FUNC_EXIT();
}

static bool dcn6_ms_check_pstate_schedule_admissibility(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	const struct dml2_core_ip_params *ip = ctx->ip;
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_internal_mode_support *inputs = states;
	struct dml2_core_internal_mode_support *outputs = states;
	struct dml2_core_calcs_mode_support_locals *dummies = ctx->dummies;
	// array of booleans indicating whether DRR is enabled for each plane
	bool *is_drr = dummies->dummy_boolean_array[0];
	// array of frame times for each plane in microseconds
	double *frame_time_us = dummies->dummy_double_array[0];
	enum dml2_pstate_method *pstate_method = dummies->dummy_pstate_method_array;
	const struct dml2_stream_parameters *stream = NULL;
	const struct dml2_plane_parameters *plane = NULL;
	unsigned int k;

	DML_LOG_FUNC_ENTER();
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];
		frame_time_us[k] = dml2_core_utils_get_frame_time_us(stream);
		is_drr[k] = stream->timing.drr_config.enabled
				&& !stream->timing.drr_config.disallowed
				&& (stream->timing.drr_config.drr_active_fixed
						|| stream->timing.drr_config.drr_active_variable);
		pstate_method[k] = dml2_pstate_method_vactive;
	}

	/* fclk pstate */
	outputs->support.fclk_pstate_schedule_admissible = false;
	if (inputs->support.global_fclk_change_supported)
		dcn6_calculate_pstate_schedule_admissibility(
			display_cfg->num_planes,
			ip->fams2_max_allow_delay_us,
			ip->fams2_min_allow_width_us,
			inputs->timing_group_id,
			inputs->timing_group_count,
			frame_time_us,
			inputs->fclk_pstate_allow_start_us,
			inputs->fclk_pstate_allow_end_us,
			pstate_method,
			is_drr,
			// Outputs
			dummies->dummy_double_array[1],
			dummies->dummy_double_array[2],
			&outputs->support.fclk_pstate_schedule_admissible
		);

	/* ppt pstate */
	outputs->support.ppt_pstate_schedule_admissible = false;
	if (inputs->support.global_temp_read_or_ppt_supported)
		dcn6_calculate_pstate_schedule_admissibility(
			display_cfg->num_planes,
			ip->ppt_max_allow_delay_us,
			ip->fams2_min_allow_width_us,
			inputs->timing_group_id,
			inputs->timing_group_count,
			frame_time_us,
			inputs->ppt_pstate_allow_start_us,
			inputs->ppt_pstate_allow_end_us,
			pstate_method,
			is_drr,
			// Outputs
			dummies->dummy_double_array[1],
			dummies->dummy_double_array[2],
			&outputs->support.ppt_pstate_schedule_admissible
		);

	/* temp read pstate */
	outputs->support.temp_read_pstate_schedule_admissible = false;
	if (inputs->support.global_temp_read_or_ppt_supported)
		dcn6_calculate_pstate_schedule_admissibility(
			display_cfg->num_planes,
			ip->temp_read_max_allow_delay_us,
			ip->fams2_min_allow_width_us,
			inputs->timing_group_id,
			inputs->timing_group_count,
			frame_time_us,
			inputs->temp_read_pstate_allow_start_us,
			inputs->temp_read_pstate_allow_end_us,
			pstate_method,
			is_drr,
			// Outputs
			dummies->dummy_double_array[1],
			dummies->dummy_double_array[2],
			&outputs->support.temp_read_pstate_schedule_admissible
		);
	DML_LOG_DEBUG_BOOL(outputs->support.fclk_pstate_schedule_admissible);
	DML_LOG_DEBUG_BOOL(outputs->support.ppt_pstate_schedule_admissible);
	DML_LOG_DEBUG_BOOL(outputs->support.temp_read_pstate_schedule_admissible);
	DML_LOG_FUNC_EXIT();
	return (!inputs->fclk_pstate_required || outputs->support.fclk_pstate_schedule_admissible)
			&& (!inputs->ppt_pstate_required || outputs->support.ppt_pstate_schedule_admissible)
			&& (!inputs->temp_read_pstate_required || outputs->support.temp_read_pstate_schedule_admissible);
}

static void dcn6_ms_initialize_from_solution(struct dml2_core_internal_mode_support *outputs,
		const struct dml2_display_solution *solution,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	DML_LOG_FUNC_ENTER();
	outputs->UrgLatency = solution->sop_constraint.dcn5.latency.dcn5.urgent_ramp;
	outputs->TripToMemory = math_max2(solution->sop_constraint.dcn5.latency.dcn5.t_trip,
		solution->sop_constraint.dcn5.latency.dcn5.urgent_ramp);
	outputs->support.avg_urgent_latency_us = solution->sop_constraint.dcn5.latency.dcn5.avg_req_latency_urg;
	outputs->support.avg_non_urgent_latency_us = solution->sop_constraint.dcn5.latency.dcn5.avg_req_latency_non_urg;
	outputs->support.max_urgent_latency_us = solution->sop_constraint.dcn5.latency.dcn5.max_req_latency_urg;
	outputs->support.max_non_urgent_latency_us = solution->sop_constraint.dcn5.latency.dcn5.max_req_latency_non_urg;
	outputs->support.df_response_time_us = solution->sop_constraint.dcn5.latency.dcn5.df_response_time_us;
	if (solution->dispcfg.overrides.hw.dcfclk_mhz > 0)
		outputs->DCFCLK = solution->dispcfg.overrides.hw.dcfclk_mhz;
	else
		outputs->DCFCLK = solution->sop_constraint.dcn5.clocks.dcfclk_khz / 1000.0;
	outputs->min_available_urgent_bandwidth_MBps =
			math_min2(solution->sop_constraint.dcn5.min_available_urgent_bandwidth_KBps / 1000.0,
					outputs->DCFCLK * utm_soc_bb->return_bus_width_bytes);
	outputs->qos_param_index = solution->sop_constraint.dcn5.min_sop_index;
	memcpy(outputs->uclk_pstate_switch_modes, solution->uclk_pstate_params.pstate_switch_modes, sizeof(solution->uclk_pstate_params.pstate_switch_modes));
	outputs->fclk_pstate_required = solution->fclk_pstate_support;
	outputs->ppt_pstate_required = solution->ppt_temp_read_support;
	outputs->temp_read_pstate_required = solution->ppt_temp_read_support;
	outputs->timing_group_count = solution->timing_group_count;
	memcpy(outputs->timing_group_id, solution->timing_group_ids, sizeof(solution->timing_group_ids));

	DML_LOG_VERBOSE("DML::%s: urgent_ramp=%f\n", __func__, solution->sop_constraint.dcn5.latency.dcn5.urgent_ramp);
	DML_LOG_VERBOSE("DML::%s: t_trip=%f\n", __func__, solution->sop_constraint.dcn5.latency.dcn5.t_trip);
	DML_LOG_VERBOSE("DML::%s: avg_req_latency_urg=%f\n", __func__,
			solution->sop_constraint.dcn5.latency.dcn5.avg_req_latency_urg);
	DML_LOG_VERBOSE("DML::%s: avg_req_latency_non_urg=%f\n", __func__,
			solution->sop_constraint.dcn5.latency.dcn5.avg_req_latency_non_urg);
	DML_LOG_VERBOSE("DML::%s: max_req_latency_urg=%f\n", __func__,
			solution->sop_constraint.dcn5.latency.dcn5.max_req_latency_urg);
	DML_LOG_VERBOSE("DML::%s: max_req_latency_non_urg=%f\n", __func__,
			solution->sop_constraint.dcn5.latency.dcn5.max_req_latency_non_urg);
	DML_LOG_VERBOSE("DML::%s: DCFCLK=%f\n", __func__, outputs->DCFCLK);
	DML_LOG_VERBOSE("DML::%s: min_available_urgent_bandwidth_MBps=%f\n",
			__func__, outputs->min_available_urgent_bandwidth_MBps);
	DML_LOG_FUNC_EXIT();
}

static enum dml2_status dcn6_ms_validate_prefetch(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	enum dml2_status status = DML2_STATUS_OK;

	DML_LOG_FUNC_ENTER();
	dcn6_ms_calculate_det_buffer_time_value_urgent_burst_factor_and_urgent_latency_hiding(ctx, states);

	dcn6_ms_calculate_min_dcfclk_deepsleep_clock(ctx, states);

	dcn6_ms_calculate_writeback_delay(ctx, states);

	dcn6_ms_calculate_alternate_svp_lines(ctx, states);

	dcn6_ms_calculate_max_vstartup(ctx, states);

	dcn6_ms_calculate_mcache_setting(ctx, states);

	dcn6_ms_calculate_avg_bandwidth_and_dcfclk_lb_required(ctx, states);

	dcn6_ms_check_average_latency_supports(ctx, states);

	dcn6_ms_calculate_t_calc(ctx, states);

	dcn6_ms_calculate_hostvm_inefficiency_factor(ctx, states);

	dcn6_ms_calculate_3dlut_settings(ctx, states);

	dcn6_ms_calculate_urgent_latency(ctx, states);

	if (status == DML2_STATUS_OK) {

		dcn6_ms_calculate_prefetch_schedule(ctx, states);

		dcn6_ms_check_urgent_latency_hiding_support(ctx, states);

		dcn6_ms_check_dynamic_metadata_support(ctx, states);

		if (!dcn6_ms_check_prefetch_support(ctx, states))
			status = DML2_STATUS_VALIDATE_FAIL_MODE_SUPPORT_PREFETCH;

		if (!dcn6_ms_check_v_ratio_in_prefetch_support(ctx, states))
			status = DML2_STATUS_VALIDATE_FAIL_MODE_SUPPORT_PREFETCH;
	}

	if (status == DML2_STATUS_OK) {
		dcn6_ms_calculate_urgent_burst_factor_for_prefetch(ctx, states);

		if (!dcn6_ms_check_final_prefetch_support(ctx, states))
			status = DML2_STATUS_VALIDATE_FAIL_MODE_SUPPORT_PREFETCH_URGENT;
	}

	if (status == DML2_STATUS_OK) {
		dcn6_ms_calculate_flip_schedule(ctx, states);

		if (!dcn6_ms_check_immediate_flip_support(ctx, states))
			status = DML2_STATUS_VALIDATE_FAIL_MODE_SUPPORT_QOS_BANDWIDTH;

		dcn6_ms_calculate_peak_bandwidth_required(ctx, states);

		dcn6_ms_calculate_bandwidth_upper_bound(ctx, states);

		dcn6_ms_check_qos_bandwidth_support(ctx, states);

		dcn6_ms_check_dcfclk_support(ctx, states);
	}

	if (status == DML2_STATUS_OK) {
		dcn6_ms_calculate_watermarks(ctx, states);

		dcn6_ms_check_reordering_support(ctx, states);

		dcn6_ms_calculate_vactive_det_fill_latency(ctx, states);

		dcn6_ms_calculate_alternate_params(ctx, states);

		dcn6_ms_check_alternate_channel_size_support(ctx, states);

		dcn6_ms_calculate_pstate_schedule_windows(ctx, states);

		if (!dcn6_ms_check_pstate_schedule_admissibility(ctx, states))
			status = DML2_STATUS_VALIDATE_FAIL_PSTATE_SCHEDULE;
	}

	if (!dcn6_ms_check_mode_support(ctx, states, status) && status == DML2_STATUS_OK)
		status = DML2_STATUS_VALIDATE_FAIL_MODE_SUPPORT;

	dcn6_ms_populate_informative(ctx, states);

	DML_LOG_VERBOSE("DML::%s: Done prefetch calculation\n", __func__);
	DML_LOG_FUNC_EXIT();

	return status;
}

static enum dml2_status dcn6_mode_support(
		const struct dml2_core_calculate_ms_context *ctx,
		struct dml2_core_internal_mode_support *states)
{
	enum dml2_status status = DML2_STATUS_OK;

	DML_LOG_FUNC_ENTER();
	dcn6_ms_check_input_sanity(ctx, states);

	dcn6_ms_check_scaler_support(ctx, states);

	dcn6_ms_check_source_format_and_scan_direction(ctx, states);

	dcn6_ms_calculate_byte_per_pixel_and_block_sizes(ctx, states);

	dcn6_ms_calculate_read_bandwidth(ctx, states);

	dcn6_ms_calculate_writeback_bandwidth(ctx, states);

	dcn6_ms_check_writeback_bandwidth_latency_support(ctx, states);

	dcn6_ms_check_writeback_scale_ratio_and_taps_support(ctx, states);

	dcn6_ms_calculate_single_pipe_dppclk_and_pscl_factor(ctx, states);

	dcn6_ms_calculate_max_swath_widths(ctx, states);

	dcn6_ms_check_cursor_support(ctx, states);

	dcn6_ms_check_surface_alginment_requirements(ctx, states);

	dcn6_ms_calculate_effective_pixel_clock(ctx, states);

	dcn6_ms_calculate_estimated_num_of_dsc_slices(ctx, states);

	dcn6_ms_calculate_desired_output_bpp(ctx, states);

	dcn6_ms_calculate_output_link(ctx, states);

	dcn6_ms_calculate_odm_mode(ctx, states);

	dcn6_ms_calculate_num_of_dsc_slices(ctx, states);

	dcn6_ms_check_num_of_dsc_slices_support(ctx, states);

	dcn6_ms_calculate_max_det_and_min_compressed_buffer_size(ctx, states);

	dcn6_ms_calculate_swath_and_det_configuration_for_single_dpp(ctx, states);

	dcn6_ms_calculate_num_of_dpp_required(ctx, states);

	dcn6_ms_check_total_available_pipes_support(ctx, states);

	dcn6_ms_check_total_available_TDLUT_33cube_support(ctx, states);

	dcn6_ms_calculate_total_num_of_single_dpp_surfaces(ctx, states);

	dcn6_ms_calculate_dispclk_and_dppclk_required(ctx, states);

	dcn6_ms_check_dispclk_and_dppclk_support(ctx, states);

	dcn6_ms_calculate_dtbclk_required(ctx, states);

	dcn6_ms_check_dtbclk_support(ctx, states);

	dcn6_ms_check_otg_count_support(ctx, states);

	dcn6_ms_check_hpo_frl_encoder_count_support(ctx, states);

	dcn6_ms_check_hpo_dp_encoder_count_support(ctx, states);

	dcn6_ms_check_writeback_count_support(ctx, states);

	dcn6_ms_check_link_bandwidth_support(ctx, states);

	dcn6_ms_check_misc_link_supports(ctx, states);

	dcn6_ms_calculate_dscclk_required(ctx, states);

	dcn6_ms_check_dscclk_support(ctx, states);

	dcn6_ms_check_dsc_engine_supports(ctx, states);

	dcn6_ms_calculate_dsc_delay(ctx, states);

	dcn6_ms_calculate_swath_and_det_configuration(ctx, states);

	dcn6_ms_calculate_total_num_of_dcc_active_dpp(ctx, states);

	dcn6_ms_calculate_vm_row_and_swath_and_calculate_dcc_meta_cache_requirements(ctx, states);

	dcn6_ms_check_pte_buffer_size_support(ctx, states);

	dcn6_ms_check_dcc_meta_cache_support(ctx, states);

	dcn6_ms_calculate_vactive_pstate_requirements(ctx, states);

	status = dcn6_ms_validate_prefetch(ctx, states);

	DML_LOG_VERBOSE("DML::%s: done\n", __func__);
	DML_LOG_FUNC_EXIT();
	return status;
}

enum dml2_status dml2_core_dcn6_funcs_validate_solution(struct dml2_core_instance *core,
		const struct dml2_display_solution *solution,
		struct dml2_validation_result *result)
{
	enum dml2_status status = DML2_STATUS_OK;
	struct dml2_core_calculate_ms_context *calc_ms_ctx = &core->scratch.mode_support_locals.calc_ms_ctx;
	struct dml2_calculate_mcache_allocation_in_out *calc_mcache_allocation_params =
			&core->scratch.mode_support_locals.calc_mcache_allocation_params;;
	struct dml2_core_internal_display_mode_lib *mode_lib = &core->clean_me_up.mode_lib;
	unsigned int i;

	DML_LOG_COMP_IF_ENTER();
	if (solution->unvalidated_change.bits.mpc_combine_overrides
			|| solution->unvalidated_change.bits.odm_combine_overrides
			|| solution->unvalidated_change.bits.reserved_vblank_time
			|| solution->unvalidated_change.bits.uclk_pstate_method
			|| solution->unvalidated_change.bits.fclk_pstate_support
			|| solution->unvalidated_change.bits.ppt_temp_read_pstate_support)
		result->is_mode_support_valid = false;
	if (solution->unvalidated_change.bits.sop_index
			|| solution->unvalidated_change.bits.dcfclk_override)
		result->is_prefetch_valid = false;

	if (!result->is_mode_support_valid) {
		calc_ms_ctx->display_cfg = &solution->dispcfg;
		calc_ms_ctx->ip = &core->clean_me_up.mode_lib.ip;
		calc_ms_ctx->soc_bb = core->utm_soc_bb;
		calc_ms_ctx->clock_adjuster = core->clock_adjuster;
		calc_ms_ctx->dummies = &mode_lib->scratch.dml_core_mode_support_locals;
		calc_ms_ctx->func_params = &mode_lib->scratch;
		memset(calc_ms_ctx->func_params, 0, sizeof(struct dml2_core_internal_scratch));
		memset(&mode_lib->ms, 0, sizeof(struct dml2_core_internal_mode_support));

		dcn6_ms_initialize_from_solution(&mode_lib->ms, solution, core->utm_soc_bb);
		status = dcn6_mode_support(calc_ms_ctx, &mode_lib->ms);
		result->is_mode_support_valid = status == DML2_STATUS_OK;
		result->is_prefetch_valid = result->is_mode_support_valid;
		result->mode_support.cfg_support_info.is_supported = result->is_mode_support_valid;
		if (result->is_mode_support_valid)
			dcn6_ms_populate_mode_support_result(calc_ms_ctx, &mode_lib->ms, &result->mode_support);
	} else if (!result->is_prefetch_valid) {
		dcn6_ms_initialize_from_solution(&mode_lib->ms, solution, core->utm_soc_bb);
		status = dcn6_ms_validate_prefetch(calc_ms_ctx, &mode_lib->ms);
		if (status == DML2_STATUS_OK) {
			dcn6_ms_populate_mode_support_result(calc_ms_ctx, &mode_lib->ms, &result->mode_support);
			result->is_prefetch_valid = true;
		}
	}

	if (!result->is_mcache_allocation_valid) {
		result->is_mcache_allocation_valid = true;
		for (i = 0; i < solution->dispcfg.num_planes; i++) {
			if (!solution->dispcfg.plane_descriptors[i].surface.dcc.enable) {
				memset(&result->mcache_allocations[i], 0, sizeof(struct dml2_mcache_surface_allocation));
				continue;
			}

			calc_mcache_allocation_params->instance = core;
			calc_mcache_allocation_params->plane_descriptor = &solution->dispcfg.plane_descriptors[i];
			calc_mcache_allocation_params->mcache_allocation = &result->mcache_allocations[i];
			calc_mcache_allocation_params->plane_index = i;
			if (!core->calculate_mcache_allocation(calc_mcache_allocation_params)) {
				status = DML2_STATUS_VALIDATE_FAIL_MCACHE;
				result->is_mcache_allocation_valid = false;
				break;
			}
		}
	}

	DML_LOG_VERBOSE("DML::%s: result->is_mode_support_valid = %d\n", __func__, result->is_mode_support_valid);
	DML_LOG_VERBOSE("DML::%s: result->is_prefetch_valid = %d\n", __func__, result->is_prefetch_valid);
	DML_LOG_VERBOSE("DML::%s: result->is_mcache_allocation_valid = %d\n", __func__, result->is_mcache_allocation_valid);
	if (status == DML2_STATUS_OK)
		DML_ASSERT_MSG(result->is_mode_support_valid
				&& result->is_prefetch_valid
				&& result->is_mcache_allocation_valid,
				"mismatch between status and valid bits detected!\n");
	DML_LOG_DEBUG("%s exit with %s\n", __func__, dml2_status_str(status));
	DML_LOG_COMP_IF_EXIT();
	return status;
}
