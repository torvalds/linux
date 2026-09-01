// SPDX-License-Identifier: MIT
//
// Copyright 2024-2025 Advanced Micro Devices, Inc.

#include "dml2_core_dcn5_funcs_mode_support.h"
#include "dml2_core_utils.h"
#include "dml2_core_dcn5_calcs_dchub.h"
#include "dml2_core_dcn5_calcs_display_pipe.h"

static void dcn5_ms_check_input_sanity(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_plane_parameters *plane;

	mode_lib->ms.support.ViewportExceedsSurface = false;
	if (!display_cfg->overrides.hw.surface_viewport_size_check_disable) {
		for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
			plane = &display_cfg->plane_descriptors[k];
			if (plane->composition.viewport.plane0.width > plane->surface.plane0.width
					|| plane->composition.viewport.plane0.height > plane->surface.plane0.height) {
				mode_lib->ms.support.ViewportExceedsSurface = true;
				DML_LOG_VERBOSE("DML::%s: k=%u ViewportWidth = %ld\n", __func__, k, plane->composition.viewport.plane0.width);
				DML_LOG_VERBOSE("DML::%s: k=%u SurfaceWidthY = %ld\n", __func__, k, plane->surface.plane0.width);
				DML_LOG_VERBOSE("DML::%s: k=%u ViewportHeight = %ld\n", __func__, k, plane->composition.viewport.plane0.height);
				DML_LOG_VERBOSE("DML::%s: k=%u SurfaceHeightY = %ld\n", __func__, k, plane->surface.plane0.height);
				DML_LOG_VERBOSE("DML::%s: k=%u ViewportExceedsSurface = %d\n", __func__, k, mode_lib->ms.support.ViewportExceedsSurface);
			}
			if (dml2_core_utils_is_420(plane->pixel_format) || dml2_core_utils_is_422_planar(plane->pixel_format)
					|| plane->pixel_format == dml2_rgbe_alpha) {
				if (plane->composition.viewport.plane1.width > plane->surface.plane1.width
						|| plane->composition.viewport.plane1.height > plane->surface.plane1.height) {
					mode_lib->ms.support.ViewportExceedsSurface = true;
				}
			}
		}
	}
}

static void dcn5_ms_calculate_max_det_and_min_compressed_buffer_size(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	dcn5_calculate_max_det_and_min_compressed_buffer_size(
			mode_lib->ip.config_return_buffer_size_in_kbytes,
			mode_lib->ip.config_return_buffer_segment_size_in_kbytes,
			mode_lib->ip.rob_buffer_size_kbytes,
			mode_lib->ip.max_num_dpp,
			display_cfg->overrides.hw.force_nom_det_size_kbytes.enable,
			display_cfg->overrides.hw.force_nom_det_size_kbytes.value,
			mode_lib->ip.dcn_mrq_present,

			/* Output */
			&mode_lib->ms.MaxTotalDETInKByte,
			&mode_lib->ms.NomDETInKByte,
			&mode_lib->ms.MinCompressedBufferSizeInKByte);
}

static void dcn5_ms_calculate_effective_pixel_clock(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	/*
	 * This function should probably be removed since ptoi is never true, so the function is a noop; not really
	 * obvious if the comment means DML2.1 doesn't support interlace today.
	 */
	dcn5_adjust_pixel_clock_for_progressive_to_interlace_unit(display_cfg, mode_lib->ip.ptoi_supported, mode_lib->ms.PixelClockBackEnd);
}

static bool dcn5_ms_check_scaler_support(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	/*Scale Ratio, taps Support Check*/
	unsigned int k;
	bool support = true;
	const struct dml2_plane_parameters *plane;

	// Many core tests are still setting scaling parameters "incorrectly"
	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		if (plane->composition.scaler_info.enabled == false
				&& (dml2_core_utils_is_420(plane->pixel_format) || dml2_core_utils_is_422_planar(plane->pixel_format) || dml2_core_utils_is_422_packed(plane->pixel_format)
						|| plane->composition.scaler_info.plane0.h_ratio != 1.0
						|| plane->composition.scaler_info.plane0.h_taps != 1.0
						|| plane->composition.scaler_info.plane0.v_ratio != 1.0
						|| plane->composition.scaler_info.plane0.v_taps != 1.0)) {
			support = false;
		} else if (plane->composition.scaler_info.plane0.v_taps < 1.0
				|| plane->composition.scaler_info.plane0.v_taps > 8.0
				|| plane->composition.scaler_info.plane0.h_taps < 1.0
				|| plane->composition.scaler_info.plane0.h_taps > 8.0
				|| (plane->composition.scaler_info.plane0.h_taps > 1.0
						&& (plane->composition.scaler_info.plane0.h_taps % 2) == 1)
				|| plane->composition.scaler_info.plane0.h_ratio > mode_lib->ip.max_hscl_ratio
				|| plane->composition.scaler_info.plane0.v_ratio > mode_lib->ip.max_vscl_ratio
				|| plane->composition.scaler_info.plane0.h_ratio > plane->composition.scaler_info.plane0.h_taps
				|| plane->composition.scaler_info.plane0.v_ratio > plane->composition.scaler_info.plane0.v_taps
				|| ((dml2_core_utils_is_420(plane->pixel_format) || dml2_core_utils_is_422_planar(plane->pixel_format))
						&& (plane->composition.scaler_info.plane1.v_taps < 1
								|| plane->composition.scaler_info.plane1.v_taps > 8
								|| plane->composition.scaler_info.plane1.h_taps < 1
								|| plane->composition.scaler_info.plane1.h_taps > 8
								|| (plane->composition.scaler_info.plane1.h_taps > 1
										&& plane->composition.scaler_info.plane1.h_taps % 2 == 1)
								|| plane->composition.scaler_info.plane1.h_ratio > mode_lib->ip.max_hscl_ratio
								|| plane->composition.scaler_info.plane1.v_ratio > mode_lib->ip.max_vscl_ratio
								|| plane->composition.scaler_info.plane1.h_ratio > plane->composition.scaler_info.plane1.h_taps
								|| plane->composition.scaler_info.plane1.v_ratio > plane->composition.scaler_info.plane1.v_taps))) {
			support = false;
		}
	}
	return support;
}

static bool dcn5_ms_check_source_format_and_scan_direction(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	/*Source Format, Pixel Format and Scan Support Check*/
	unsigned int k;
	bool support = true;
	const struct dml2_plane_parameters *plane;

	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		plane = &display_cfg->plane_descriptors[k];
		if (plane->surface.tiling == dml2_sw_linear
				&& dml2_core_utils_is_vertical_rotation(plane->composition.rotation_angle)) {
			support = false;
		}
	}
	return support;
}

static void dcn5_ms_calculate_byte_per_pixel_and_block_sizes(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_plane_parameters *plane;

	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		plane = &display_cfg->plane_descriptors[k];
		dcn5_calculate_byte_per_pixel_and_block_sizes(
				plane->pixel_format,
				plane->surface.tiling,
				plane->surface.plane0.pitch,
				plane->surface.plane1.pitch,
				/* Output */
				&mode_lib->ms.BytePerPixelY[k],
				&mode_lib->ms.BytePerPixelC[k],
				&mode_lib->ms.BytePerPixelInDETY[k],
				&mode_lib->ms.BytePerPixelInDETC[k],
				&mode_lib->ms.Read256BlockHeightY[k],
				&mode_lib->ms.Read256BlockHeightC[k],
				&mode_lib->ms.Read256BlockWidthY[k],
				&mode_lib->ms.Read256BlockWidthC[k],
				&mode_lib->ms.MacroTileHeightY[k],
				&mode_lib->ms.MacroTileHeightC[k],
				&mode_lib->ms.MacroTileWidthY[k],
				&mode_lib->ms.MacroTileWidthC[k],
				&mode_lib->ms.surf_linear128_l[k],
				&mode_lib->ms.surf_linear128_c[k]);
	}
}

static void dcn5_ms_calculate_read_bandwidth(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;

	/* Bandwidth Support Check */
	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		if (!dml2_core_utils_is_vertical_rotation(plane->composition.rotation_angle)) {
			mode_lib->ms.SwathWidthYSingleDPP[k] = plane->composition.viewport.plane0.width;
			mode_lib->ms.SwathWidthCSingleDPP[k] = plane->composition.viewport.plane1.width;
		} else {
			mode_lib->ms.SwathWidthYSingleDPP[k] = plane->composition.viewport.plane0.height;
			mode_lib->ms.SwathWidthCSingleDPP[k] = plane->composition.viewport.plane1.height;
		}
	}
	for (k = 0; k < mode_lib->ms.num_active_planes ; k++) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];
		mode_lib->ms.vactive_sw_bw_l[k] = mode_lib->ms.SwathWidthYSingleDPP[k]
				* math_ceil2(mode_lib->ms.BytePerPixelY[k], 1.0)
				/ (stream->timing.h_total / ((double) stream->timing.pixel_clock_khz / 1000))
				* plane->composition.scaler_info.plane0.v_ratio;
		mode_lib->ms.vactive_sw_bw_c[k] = mode_lib->ms.SwathWidthCSingleDPP[k]
				* math_ceil2(mode_lib->ms.BytePerPixelC[k], 2.0)
				/ (stream->timing.h_total / ((double) stream->timing.pixel_clock_khz / 1000))
				* plane->composition.scaler_info.plane1.v_ratio;
		mode_lib->ms.cursor_bw[k] = plane->cursor.num_cursors
				* plane->cursor.cursor_width
				* plane->cursor.cursor_bpp
				/ 8.0
				/ (stream->timing.h_total / ((double) stream->timing.pixel_clock_khz / 1000));
		DML_LOG_VERBOSE("DML::%s: k=%u, old_ReadBandwidthLuma = %f\n", __func__, k, mode_lib->ms.SwathWidthYSingleDPP[k] * math_ceil2(mode_lib->ms.BytePerPixelInDETY[k], 1.0) / (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)) * display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio);
		DML_LOG_VERBOSE("DML::%s: k=%u, old_ReadBandwidthChroma = %f\n", __func__, k, mode_lib->ms.SwathWidthYSingleDPP[k] / 2 * math_ceil2(mode_lib->ms.BytePerPixelInDETC[k], 2.0) / (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)) * display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio / 2.0);
		DML_LOG_VERBOSE("DML::%s: k=%u, vactive_sw_bw_l = %f\n", __func__, k,
				mode_lib->ms.vactive_sw_bw_l[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, vactive_sw_bw_c = %f\n", __func__, k,
				mode_lib->ms.vactive_sw_bw_c[k]);
	}
}

static void dcn5_ms_calculate_writeback_bandwidth(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k, j;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;

	// Writeback bandwidth
	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];
		for (j = 0; j < stream->writeback.active_writebacks_per_stream; j++) {
			mode_lib->ms.WriteBandwidth[k][j] = stream->writeback.writeback_stream[j].output_height * stream->writeback.writeback_stream[j].output_width
				/ (stream->writeback.writeback_stream[j].input_height * stream->timing.h_total / ((double)stream->timing.pixel_clock_khz / 1000));
			if (stream->writeback.writeback_stream[j].pixel_format == dml2_444_64) {
				mode_lib->ms.WriteBandwidth[k][j] *= 8.0;
			} else if (stream->writeback.writeback_stream[j].pixel_format == dml2_444_32) {
				mode_lib->ms.WriteBandwidth[k][j] *= 4.0;
			} else if (stream->writeback.writeback_stream[j].pixel_format == dml2_420_8) {
				mode_lib->ms.WriteBandwidth[k][j] *= 1.5;
			} else if (stream->writeback.writeback_stream[j].pixel_format == dml2_420_10) {
				mode_lib->ms.WriteBandwidth[k][j] *= 3.0;
			} else if (stream->writeback.writeback_stream[j].pixel_format == dml2_422_packed_8) {
				mode_lib->ms.WriteBandwidth[k][j] *= 2.0;
			} else if (stream->writeback.writeback_stream[j].pixel_format == dml2_422_packed_10) {
				mode_lib->ms.WriteBandwidth[k][j] *= 4.0;
			} else {
				mode_lib->ms.WriteBandwidth[k][j] = 0.0;
			}
		}
	}
}

static bool dcn5_ms_check_writeback_bandwidth_latency_support(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	/*Writeback Latency support check*/
	unsigned int k, j;
	bool support = true;
	const struct dml2_stream_parameters *stream;

	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		for (j = 0; j < stream->writeback.active_writebacks_per_stream; j++) {
			if (stream->writeback.writeback_stream[j].pixel_format == dml2_420_8 || stream->writeback.writeback_stream[j].pixel_format == dml2_420_10) {// In planar mode just check luma bw does not exceed half latency hiding buffer
				if ((mode_lib->ms.WriteBandwidth[k][j] / 1.5 >
					mode_lib->ip.writeback_interface_buffer_size_kbytes * 1024 / 2.0 // half buffer for luma
					* ((stream->writeback.writeback_stream[j].pixel_format == dml2_420_10) ? 1.6 : 1.0) // 16 bit frame buffer to 10 bit buffer packing
					/ utm_soc_bb->writeback_base_latency_us))
					support = false;
				else
					if ((mode_lib->ms.WriteBandwidth[k][j] >
						mode_lib->ip.writeback_interface_buffer_size_kbytes * 1024
						* ((stream->writeback.writeback_stream[j].pixel_format == dml2_422_packed_10) ? 1.6 : 1.0)
						/ utm_soc_bb->writeback_base_latency_us))
						support = false;
			}
		}
	}
	return support;
}

static bool dcn5_ms_check_writeback_scale_ratio_and_taps_support(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	/* Writeback Scale Ratio and Taps Support Check */
	unsigned int k;
	unsigned int j;
	bool support = true;
	const struct dml2_stream_parameters *stream;

	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
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

			if (stream->writeback.writeback_stream[j].h_ratio > mode_lib->ip.writeback_max_hscl_ratio
				|| stream->writeback.writeback_stream[j].v_ratio > mode_lib->ip.writeback_max_vscl_ratio
				|| stream->writeback.writeback_stream[j].h_ratio < mode_lib->ip.writeback_min_hscl_ratio
				|| stream->writeback.writeback_stream[j].v_ratio < mode_lib->ip.writeback_min_vscl_ratio
				|| stream->writeback.writeback_stream[j].h_taps > (unsigned int) mode_lib->ip.writeback_max_hscl_taps
				|| stream->writeback.writeback_stream[j].v_taps > (unsigned int) mode_lib->ip.writeback_max_vscl_taps
				|| stream->writeback.writeback_stream[j].h_taps_chroma > (unsigned int)mode_lib->ip.writeback_max_hscl_taps
				|| stream->writeback.writeback_stream[j].v_taps_chroma > (unsigned int)mode_lib->ip.writeback_max_vscl_taps
				|| stream->writeback.writeback_stream[j].h_ratio > (unsigned int)stream->writeback.writeback_stream[j].h_taps
				|| stream->writeback.writeback_stream[j].v_ratio > (unsigned int)stream->writeback.writeback_stream[j].v_taps
				|| h_ratio_chroma > (unsigned int)stream->writeback.writeback_stream[j].h_taps_chroma
				|| v_ratio_chroma > (unsigned int)stream->writeback.writeback_stream[j].v_taps_chroma
				|| (stream->writeback.writeback_stream[j].h_taps > 2.0 && ((stream->writeback.writeback_stream[j].h_taps % 2) == 1))
				|| (stream->writeback.writeback_stream[j].h_taps_chroma > 2.0 && ((stream->writeback.writeback_stream[j].h_taps_chroma % 2) == 1))) {
				DML_LOG_VERBOSE("DML::%s: k=%d, j=%d, not support (%d)\n", __func__, k, j, __LINE__);
				support = false;
			}

			double writeback_luma_vextra = (stream->writeback.writeback_stream[j].v_ratio < 1) ?
				math_max2(1 - 2.0 / math_ceil2(1 / stream->writeback.writeback_stream[j].v_ratio, 1.0), 0.0) : -1.0;

			if (stream->writeback.writeback_stream[j].output_width * (stream->writeback.writeback_stream[j].v_taps + writeback_luma_vextra)
			> mode_lib->ip.writeback_line_buffer_buffer_size / 3.0 / 10.0) { // One third of the buffer per each component Y Cb Cr
				DML_LOG_VERBOSE("DML::%s: k=%d, j=%d, not support (%d)\n", __func__, k, j, __LINE__);
				support = false;
			}

			double writeback_chroma_vextra = (v_ratio_chroma < 1) ? math_max2(1 - 2.0 / math_ceil2(1 / v_ratio_chroma, 1.0), 0.0) : -1.0;

			if (output_width_chroma * (stream->writeback.writeback_stream[j].v_taps_chroma + writeback_chroma_vextra)
			> mode_lib->ip.writeback_line_buffer_buffer_size / 3.0 / 10.0) { // One third of the buffer per each component Y Cb Cr
				DML_LOG_VERBOSE("DML::%s: k=%d, j=%d, not support (%d)\n", __func__, k, j, __LINE__);
				support = false;
			}
		}
	}
	//support = true;
	return support;
}

static void dcn5_ms_calculate_single_pipe_dppclk_and_pscl_factor(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;

	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];
		dcn5_calculate_single_pipe_dppclk_and_scl_throughput(
				plane->composition.scaler_info.plane0.h_ratio,
				plane->composition.scaler_info.plane1.h_ratio,
				plane->composition.scaler_info.plane0.v_ratio,
				plane->composition.scaler_info.plane1.v_ratio,
				mode_lib->ip.max_dchub_pscl_bw_pix_per_clk,
				mode_lib->ip.max_pscl_lb_bw_pix_per_clk,
				((double) stream->timing.pixel_clock_khz / 1000),
				plane->pixel_format,
				plane->composition.scaler_info.plane0.h_taps,
				plane->composition.scaler_info.plane1.h_taps,
				plane->composition.scaler_info.plane0.v_taps,
				plane->composition.scaler_info.plane1.v_taps,

				/* Output */
				&mode_lib->ms.PSCL_FACTOR[k],
				&mode_lib->ms.PSCL_FACTOR_CHROMA[k],
				&mode_lib->ms.MinDPPCLKUsingSingleDPP[k]);
	}
}

static void dcn5_ms_calculate_max_swath_widths(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	// Max Viewport Size support
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	unsigned int lb_buffer_size_bits_luma;
	unsigned int lb_buffer_size_bits_chroma;
	unsigned int maximumSwathWidthSupportLuma;
	unsigned int maximumSwathWidthSupportChroma;

	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		if (plane->surface.tiling == dml2_sw_linear)
			maximumSwathWidthSupportLuma = 15360;
		else if (!dml2_core_utils_is_vertical_rotation(plane->composition.rotation_angle)
				&& mode_lib->ms.BytePerPixelC[k] > 0
				&& plane->pixel_format != dml2_rgbe_alpha)
			// horz video
			maximumSwathWidthSupportLuma = 7680 + 16;
		else if (dml2_core_utils_is_vertical_rotation(plane->composition.rotation_angle)
				&& mode_lib->ms.BytePerPixelC[k] > 0
				&& plane->pixel_format != dml2_rgbe_alpha)
			// vert video
			maximumSwathWidthSupportLuma = 4320 + 16;
		else if (plane->pixel_format == dml2_rgbe_alpha)
			// rgbe + alpha
			maximumSwathWidthSupportLuma = 5120 + 16;
		else if (dml2_core_utils_is_vertical_rotation(plane->composition.rotation_angle)
				&& mode_lib->ms.BytePerPixelY[k] == 8
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

		lb_buffer_size_bits_luma = mode_lib->ip.line_buffer_size_bits;
		lb_buffer_size_bits_chroma = mode_lib->ip.line_buffer_size_bits;

		mode_lib->ms.MaximumSwathWidthInLineBufferLuma = lb_buffer_size_bits_luma
				* math_max2(plane->composition.scaler_info.plane0.h_ratio, 1.0)
				/ 57
				/ (plane->composition.scaler_info.plane0.v_taps
						+ math_max2(math_ceil2(plane->composition.scaler_info.plane0.v_ratio, 1.0) - 2, 0.0));
		if (mode_lib->ms.BytePerPixelC[k] == 0.0)
			mode_lib->ms.MaximumSwathWidthInLineBufferChroma = 0;
		else
			mode_lib->ms.MaximumSwathWidthInLineBufferChroma = lb_buffer_size_bits_chroma
					* math_max2(plane->composition.scaler_info.plane1.h_ratio, 1.0)
					/ 57
					/ (plane->composition.scaler_info.plane1.v_taps
							+ math_max2(math_ceil2(plane->composition.scaler_info.plane1.v_ratio, 1.0) - 2, 0.0));
		mode_lib->ms.MaximumSwathWidthLuma[k] = math_min2(maximumSwathWidthSupportLuma, mode_lib->ms.MaximumSwathWidthInLineBufferLuma);
		mode_lib->ms.MaximumSwathWidthChroma[k] = math_min2(maximumSwathWidthSupportChroma, mode_lib->ms.MaximumSwathWidthInLineBufferChroma);
		DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathWidthLuma=%f\n", __func__, k, mode_lib->ms.MaximumSwathWidthLuma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathWidthSupportLuma=%u\n", __func__, k, maximumSwathWidthSupportLuma);
		DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathWidthInLineBufferLuma=%f\n", __func__, k, mode_lib->ms.MaximumSwathWidthInLineBufferLuma);
		DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathWidthChroma=%f\n", __func__, k, mode_lib->ms.MaximumSwathWidthChroma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathWidthSupportChroma=%u\n", __func__, k, maximumSwathWidthSupportChroma);
		DML_LOG_VERBOSE("DML::%s: k=%u MaximumSwathWidthInLineBufferChroma=%f\n", __func__, k, mode_lib->ms.MaximumSwathWidthInLineBufferChroma);
	}
}

static bool dcn5_ms_check_cursor_support(const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	/* Cursor Support Check */
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	bool support = true;

	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		if (plane->cursor.cursor_width > 0.0) {
			if (plane->cursor.cursor_bpp == 64
					&& mode_lib->ip.cursor_64bpp_support == false) {
				support = false;
			}
		}
	}
	return support;
}

static bool dcn5_ms_check_surface_alginment_requirements(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	bool support = true;

	/* Valid Pitch Check */
	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		// data pitch

		plane = &display_cfg->plane_descriptors[k];
		unsigned int pixel_per_element = dml2_core_utils_is_422_packed(plane->pixel_format) ? 2 : 1;
		unsigned int alignment_l = mode_lib->ms.MacroTileWidthY[k];
		if (mode_lib->ms.surf_linear128_l[k])
			alignment_l = alignment_l / 2;
		DML_LOG_VERBOSE("DML::%s: alignment_l = %d\n", __func__, alignment_l);

		mode_lib->ms.support.AlignedYPitch[k] = (unsigned int)math_ceil2(math_max2(plane->surface.plane0.pitch, plane->surface.plane0.width / pixel_per_element), alignment_l / pixel_per_element);
		if (dml2_core_utils_is_420(plane->pixel_format) || dml2_core_utils_is_422_planar(plane->pixel_format) || plane->pixel_format == dml2_rgbe_alpha) {
			unsigned int alignment_c = mode_lib->ms.MacroTileWidthC[k];

			if (mode_lib->ms.surf_linear128_c[k])
				alignment_c = alignment_c / 2;
			mode_lib->ms.support.AlignedCPitch[k] = (unsigned int)math_ceil2(math_max2(plane->surface.plane1.pitch, plane->surface.plane1.width), alignment_c);
		} else {
			mode_lib->ms.support.AlignedCPitch[k] = plane->surface.plane1.pitch;
		}

		if (mode_lib->ms.support.AlignedYPitch[k] > plane->surface.plane0.pitch ||
			mode_lib->ms.support.AlignedCPitch[k] > plane->surface.plane1.pitch) {
			support = false;
			DML_LOG_VERBOSE("DML::%s: k=%u AlignedYPitch = %d\n", __func__, k, mode_lib->ms.support.AlignedYPitch[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u PitchY = %ld\n", __func__, k, plane->surface.plane0.pitch);
			DML_LOG_VERBOSE("DML::%s: k=%u AlignedCPitch = %d\n", __func__, k, mode_lib->ms.support.AlignedCPitch[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u PitchC = %ld\n", __func__, k, plane->surface.plane1.pitch);
			DML_LOG_VERBOSE("DML::%s: k=%u PitchSupport = %d\n", __func__, k, mode_lib->ms.support.PitchSupport);
		}

		// meta pitch
		if (mode_lib->ip.dcn_mrq_present && plane->surface.dcc.enable) {
			mode_lib->ms.support.AlignedDCCMetaPitchY[k] = (unsigned int)math_ceil2(math_max2(plane->surface.dcc.plane0.pitch,
					plane->surface.plane0.width), 64.0 * mode_lib->ms.Read256BlockWidthY[k]);

			if (mode_lib->ms.support.AlignedDCCMetaPitchY[k] > plane->surface.dcc.plane0.pitch)
				support = false;

			if (dml2_core_utils_is_420(plane->pixel_format) || dml2_core_utils_is_422_planar(plane->pixel_format) || plane->pixel_format == dml2_rgbe_alpha) {
				mode_lib->ms.support.AlignedDCCMetaPitchC[k] = (unsigned int)math_ceil2(math_max2(plane->surface.dcc.plane1.pitch,
						plane->surface.plane1.width), 64.0 * mode_lib->ms.Read256BlockWidthC[k]);

				if (mode_lib->ms.support.AlignedDCCMetaPitchC[k] > plane->surface.dcc.plane1.pitch)
					support = false;
			}
		} else {
			mode_lib->ms.support.AlignedDCCMetaPitchY[k] = 0;
			mode_lib->ms.support.AlignedDCCMetaPitchC[k] = 0;
		}
	}

	return support;
}

static void dcn5_ms_calculate_swath_and_det_configuration_for_single_dpp(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib,
		struct dml2_core_calcs_mode_support_locals *s)
{
	/*
	 * FIXME - The whole point of this call seems to be to figure out SingleDPPViewportSizeSupportPerSurface, which
	 * if 0, means you need 2 DPPs.
	 */
	struct dml2_core_calcs_CalculateSwathAndDETConfiguration_params *p =
			&mode_lib->scratch.CalculateSwathAndDETConfiguration_params;

	p->display_cfg = display_cfg;
	p->ConfigReturnBufferSizeInKByte = mode_lib->ip.config_return_buffer_size_in_kbytes;
	p->MaxTotalDETInKByte = mode_lib->ms.MaxTotalDETInKByte;
	p->MinCompressedBufferSizeInKByte = mode_lib->ms.MinCompressedBufferSizeInKByte;
	p->rob_buffer_size_kbytes = mode_lib->ip.rob_buffer_size_kbytes;
	p->pixel_chunk_size_kbytes = mode_lib->ip.pixel_chunk_size_kbytes;
	p->rob_buffer_size_kbytes = mode_lib->ip.rob_buffer_size_kbytes;
	p->pixel_chunk_size_kbytes = mode_lib->ip.pixel_chunk_size_kbytes;
	p->ForceSingleDPP = 1;
	p->NumberOfActiveSurfaces = mode_lib->ms.num_active_planes;
	p->nomDETInKByte = mode_lib->ms.NomDETInKByte;
	p->ConfigReturnBufferSegmentSizeInkByte = mode_lib->ip.config_return_buffer_segment_size_in_kbytes;
	p->CompressedBufferSegmentSizeInkByte = mode_lib->ip.compressed_buffer_segment_size_in_kbytes;
	p->ReadBandwidthLuma = mode_lib->ms.vactive_sw_bw_l;
	p->ReadBandwidthChroma = mode_lib->ms.vactive_sw_bw_c;
	p->MaximumSwathWidthLuma = mode_lib->ms.MaximumSwathWidthLuma;
	p->MaximumSwathWidthChroma = mode_lib->ms.MaximumSwathWidthChroma;
	p->Read256BytesBlockHeightY = mode_lib->ms.Read256BlockHeightY;
	p->Read256BytesBlockHeightC = mode_lib->ms.Read256BlockHeightC;
	p->Read256BytesBlockWidthY = mode_lib->ms.Read256BlockWidthY;
	p->Read256BytesBlockWidthC = mode_lib->ms.Read256BlockWidthC;
	p->surf_linear128_l = mode_lib->ms.surf_linear128_l;
	p->surf_linear128_c = mode_lib->ms.surf_linear128_c;
	p->ODMMode = s->dummy_odm_mode;
	p->BytePerPixY = mode_lib->ms.BytePerPixelY;
	p->BytePerPixC = mode_lib->ms.BytePerPixelC;
	p->BytePerPixDETY = mode_lib->ms.BytePerPixelInDETY;
	p->BytePerPixDETC = mode_lib->ms.BytePerPixelInDETC;
	p->DPPPerSurface = s->dummy_integer_array[2];
	p->mrq_present = mode_lib->ip.dcn_mrq_present;
	// output
	p->req_per_swath_ub_l = s->dummy_integer_array[0];
	p->req_per_swath_ub_c = s->dummy_integer_array[1];
	p->swath_width_luma_ub = s->dummy_integer_array[3];
	p->swath_width_chroma_ub = s->dummy_integer_array[4];
	p->SwathWidth = s->dummy_integer_array[5];
	p->SwathWidthChroma = s->dummy_integer_array[6];
	p->SwathHeightY = s->dummy_integer_array[7];
	p->SwathHeightC = s->dummy_integer_array[8];
	p->request_size_bytes_luma = s->dummy_integer_array[26];
	p->request_size_bytes_chroma = s->dummy_integer_array[27];
	p->DETBufferSizeInKByte = s->dummy_integer_array[9];
	p->DETBufferSizeY = s->dummy_integer_array[10];
	p->DETBufferSizeC = s->dummy_integer_array[11];
	p->full_swath_bytes_l = mode_lib->ms.full_swath_bytes_l;
	p->full_swath_bytes_c = mode_lib->ms.full_swath_bytes_c;
	p->full_swath_bytes_single_dpp_l = s->dummy_integer_array[28];
	p->full_swath_bytes_single_dpp_c = s->dummy_integer_array[29];
	p->UnboundedRequestEnabled = &s->dummy_boolean[0];
	p->compbuf_reserved_space_64b = &s->dummy_integer[1];
	p->hw_debug5 = &s->dummy_boolean[2];
	p->CompressedBufferSizeInkByte = &s->dummy_integer[0];
	p->ViewportSizeSupportPerSurface = mode_lib->ms.SingleDPPViewportSizeSupportPerSurface;
	p->ViewportSizeSupport = &s->dummy_boolean[1];
	// This calls is just to find out if there is enough DET space to support full vp in 1 pipe.
	dcn5_calculate_swath_and_det_configuration(&mode_lib->scratch, p);
}

static void dcn5_ms_calculate_dsc_slices_per_plane(
		unsigned int k,
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	const struct dml2_stream_parameters *stream =
			&display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];

	/*Number Of DSC Slices*/
	if (stream->timing.dsc.enable == dml2_dsc_enable
			|| stream->timing.dsc.enable == dml2_dsc_enable_if_necessary) {
		if (stream->timing.dsc.overrides.num_slices != 0)
			mode_lib->ms.support.NumberOfDSCSlices[k] = stream->timing.dsc.overrides.num_slices;
		else {
			if (mode_lib->ms.PixelClockBackEnd[k] > 7200) {
				mode_lib->ms.support.NumberOfDSCSlices[k] = 16;
			} else if (mode_lib->ms.PixelClockBackEnd[k] > 3200) {
				mode_lib->ms.support.NumberOfDSCSlices[k] = 12;
			} else if (mode_lib->ms.PixelClockBackEnd[k] > 1360) {
				mode_lib->ms.support.NumberOfDSCSlices[k] = 8;
			} else if (mode_lib->ms.PixelClockBackEnd[k] > 680) {
				mode_lib->ms.support.NumberOfDSCSlices[k] = 4;
			} else if (mode_lib->ms.PixelClockBackEnd[k] > 340) {
				mode_lib->ms.support.NumberOfDSCSlices[k] = 2;
			} else {
				mode_lib->ms.support.NumberOfDSCSlices[k] = 1;
			}
		}
	} else {
		mode_lib->ms.support.NumberOfDSCSlices[k] = 0;
	}
}

static void dcn5_ms_calculate_odm_mode_per_plane(
		unsigned int k,
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	const struct dml2_stream_parameters *stream =
			&display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];

    DML_LOG_VERBOSE("DML_CORE::%s: k=%d stream_index=%d overrides.odm_mode=%d\n", __func__, k,
		display_cfg->plane_descriptors[k].stream_index, stream->overrides.odm_mode);
	dcn5_calculate_odm_mode(
		mode_lib->ip.maximum_pixels_per_line_per_dsc_unit,
		stream->timing.h_active,
		stream->output.output_format,
		stream->output.output_encoder,
		stream->overrides.odm_mode,
		mode_lib->ms.max_dispclk_freq_mhz,
		false, // DSCEnable
		mode_lib->ms.TotalNumberOfActiveDPP,
		mode_lib->ip.max_num_dpp,
		((double)stream->timing.pixel_clock_khz / 1000),
		mode_lib->ip.maximum_dsc_slices_per_pipe,
		mode_lib->ms.support.NumberOfDSCSlices[k],
		mode_lib->ip.odm_combine_support_mask,

		/* Output */
		&mode_lib->ms.TotalAvailablePipesSupportNoDSC,
		&mode_lib->ms.NumberOfDPPNoDSC,
		&mode_lib->ms.ODMModeNoDSC,
		&mode_lib->ms.RequiredDISPCLKPerSurfaceNoDSC);

	dcn5_calculate_odm_mode(
		mode_lib->ip.maximum_pixels_per_line_per_dsc_unit,
		stream->timing.h_active,
		stream->output.output_format,
		stream->output.output_encoder,
		stream->overrides.odm_mode,
		mode_lib->ms.max_dispclk_freq_mhz,
		true, // DSCEnable
		mode_lib->ms.TotalNumberOfActiveDPP,
		mode_lib->ip.max_num_dpp,
		((double)stream->timing.pixel_clock_khz / 1000),
		mode_lib->ip.maximum_dsc_slices_per_pipe,
		mode_lib->ms.support.NumberOfDSCSlices[k],
		mode_lib->ip.odm_combine_support_mask,

		/* Output */
		&mode_lib->ms.TotalAvailablePipesSupportDSC,
		&mode_lib->ms.NumberOfDPPDSC,
		&mode_lib->ms.ODMModeDSC,
		&mode_lib->ms.RequiredDISPCLKPerSurfaceDSC);
}

static void dcn5_ms_calculate_output_link(
		unsigned int k,
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	const struct dml2_stream_parameters *stream =
			&display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];

	dcn5_calculate_output_link(
			&mode_lib->scratch,
			((double) utm_soc_bb->max_phyclk_khz / 1000),
			((double) utm_soc_bb->max_phyclk_d18_khz / 1000),
			((double) utm_soc_bb->max_phyclk_d32_khz / 1000),
			utm_soc_bb->phy_downspread_percent,
			stream->output.output_encoder,
			stream->output.output_format,
			stream->timing.h_total,
			stream->timing.h_active,
			mode_lib->ms.PixelClockBackEnd[k],
			mode_lib->ms.DesiredOutputBpp[k],
			mode_lib->ip.maximum_dsc_bits_per_component,
			mode_lib->ms.support.NumberOfDSCSlices[k],
			stream->output.audio_sample_rate,
			stream->output.audio_sample_layout,
			mode_lib->ms.ODMModeNoDSC,
			mode_lib->ms.ODMModeDSC,
			stream->timing.dsc.enable,
			stream->output.output_dp_lane_count,
			stream->output.output_dp_link_rate,
			/* Output */
			&mode_lib->ms.RequiresDSC[k],
			&mode_lib->ms.RequiresFEC[k],
			&mode_lib->ms.OutputBpp[k],
			&mode_lib->ms.OutputType[k],
			&mode_lib->ms.OutputRate[k],
			&mode_lib->ms.RequiredSlots[k]);
	if (mode_lib->ms.DesiredOutputBpp[k] == 0.0)
		mode_lib->ms.DesiredOutputBpp[k] = mode_lib->ms.OutputBpp[k];
}

static void dcn5_ms_calculate_final_odm_mode_per_plane(unsigned int k, struct dml2_core_internal_display_mode_lib *mode_lib)
{
	if (mode_lib->ms.RequiresDSC[k] == false) {
		mode_lib->ms.ODMMode[k] = mode_lib->ms.ODMModeNoDSC;
		mode_lib->ms.RequiredDISPCLKPerSurface[k] = mode_lib->ms.RequiredDISPCLKPerSurfaceNoDSC;
		if (!mode_lib->ms.TotalAvailablePipesSupportNoDSC)
			/* FIXME: ODM Mode decision should not depend on pipe availability to avoid circular dependency */
			mode_lib->ms.support.TotalAvailablePipesSupport = false;
		mode_lib->ms.TotalNumberOfActiveDPP = mode_lib->ms.TotalNumberOfActiveDPP + mode_lib->ms.NumberOfDPPNoDSC;
	} else {
		mode_lib->ms.ODMMode[k] = mode_lib->ms.ODMModeDSC;
		mode_lib->ms.RequiredDISPCLKPerSurface[k] = mode_lib->ms.RequiredDISPCLKPerSurfaceDSC;
		if (!mode_lib->ms.TotalAvailablePipesSupportDSC)
			/* FIXME: ODM Mode decision should not depend on pipe availability to avoid circular dependency */
			mode_lib->ms.support.TotalAvailablePipesSupport = false;
		mode_lib->ms.TotalNumberOfActiveDPP = mode_lib->ms.TotalNumberOfActiveDPP + mode_lib->ms.NumberOfDPPDSC;
	}
	DML_LOG_VERBOSE("DML::%s: k=%d RequiresDSC = %d\n", __func__, k, mode_lib->ms.RequiresDSC[k]);
	DML_LOG_VERBOSE("DML::%s: k=%d ODMMode = %d\n", __func__, k, mode_lib->ms.ODMMode[k]);
}

static void dcn5_ms_calculate_final_dsc_slices_per_plane(unsigned int k,
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	const struct dml2_stream_parameters *stream =
			&display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];

	// ensure the number dsc slices is integer multiple based on ODM mode
	mode_lib->ms.support.DSCSlicesODMModeSupported = true;
	if (mode_lib->ms.RequiresDSC[k]) {
		// fail a ms check if the override num_slices doesn't align with odm mode setting
		if (stream->timing.dsc.overrides.num_slices != 0) {
			if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_2to1)
				mode_lib->ms.support.DSCSlicesODMModeSupported =
						((mode_lib->ms.support.NumberOfDSCSlices[k] % 2) == 0);
			else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_3to1)
				mode_lib->ms.support.DSCSlicesODMModeSupported =
						(mode_lib->ms.support.NumberOfDSCSlices[k] == 12);
			else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_4to1)
				mode_lib->ms.support.DSCSlicesODMModeSupported =
						((mode_lib->ms.support.NumberOfDSCSlices[k] % 4) == 0);
			if (!mode_lib->ms.support.DSCSlicesODMModeSupported) {
				DML_LOG_VERBOSE("DML::%s: k=%d Invalid dsc num_slices and ODM mode setting\n", __func__, k);
				DML_LOG_VERBOSE("DML::%s: k=%d num_slices = %d\n", __func__, k, stream->timing.dsc.overrides.num_slices);
				DML_LOG_VERBOSE("DML::%s: k=%d ODMMode = %d\n", __func__, k, mode_lib->ms.ODMMode[k]);
			}
		} else {
			// safe guard to ensure the dml derived dsc slices and odm setting are compatible
			if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_2to1)
				mode_lib->ms.support.NumberOfDSCSlices[k] = 2 * (unsigned int) math_ceil2(mode_lib->ms.support.NumberOfDSCSlices[k] / 2.0, 1.0);
			else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_3to1)
				mode_lib->ms.support.NumberOfDSCSlices[k] = 12;
			else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_4to1)
				mode_lib->ms.support.NumberOfDSCSlices[k] = 4 * (unsigned int) math_ceil2(mode_lib->ms.support.NumberOfDSCSlices[k] / 4.0, 1.0);
		}
	} else {
		mode_lib->ms.support.NumberOfDSCSlices[k] = 0;
	}
}

static void dcn5_ms_calculate_num_of_dpp_required(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_plane_parameters *plane;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		plane = &display_cfg->plane_descriptors[k];
		mode_lib->ms.MPCCombine[k] = false;
		mode_lib->ms.NoOfDPP[k] = 1;
		if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_4to1) {
			mode_lib->ms.MPCCombine[k] = false;
			mode_lib->ms.NoOfDPP[k] = 4;
		} else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_3to1) {
			mode_lib->ms.MPCCombine[k] = false;
			mode_lib->ms.NoOfDPP[k] = 3;
		} else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_2to1) {
			mode_lib->ms.MPCCombine[k] = false;
			mode_lib->ms.NoOfDPP[k] = 2;
		} else if (plane->overrides.mpcc_combine_factor == 2) {
			mode_lib->ms.MPCCombine[k] = true;
			mode_lib->ms.NoOfDPP[k] = 2;
			mode_lib->ms.TotalNumberOfActiveDPP++;
		} else if (plane->overrides.mpcc_combine_factor == 1) {
			mode_lib->ms.MPCCombine[k] = false;
			mode_lib->ms.NoOfDPP[k] = 1;
			if (!mode_lib->ms.SingleDPPViewportSizeSupportPerSurface[k]) {
				DML_LOG_VERBOSE("WARNING: DML::%s: MPCC is override to disable but viewport is too large to be supported with single pipe!\n", __func__);
			}
		} else {
			if ((mode_lib->ms.MinDPPCLKUsingSingleDPP[k] > mode_lib->ms.max_dppclk_freq_mhz)
					|| !mode_lib->ms.SingleDPPViewportSizeSupportPerSurface[k]) {
				mode_lib->ms.MPCCombine[k] = true;
				mode_lib->ms.NoOfDPP[k] = 2;
				mode_lib->ms.TotalNumberOfActiveDPP++;
			}
		}
		DML_LOG_VERBOSE("DML::%s: k=%d, NoOfDPP = %d\n", __func__, k,
				mode_lib->ms.NoOfDPP[k]);
	}
}

static bool dcn5_ms_check_total_available_pipes_support(
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	return mode_lib->ms.TotalNumberOfActiveDPP <= (unsigned int) mode_lib->ip.max_num_dpp;
}

static void dcn5_ms_calculate_total_num_of_single_dpp_surfaces(
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;

	for (k = 0; k < (unsigned int) mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.NoOfDPP[k] == 1)
			mode_lib->ms.TotalNumberOfSingleDPPSurfaces =
					mode_lib->ms.TotalNumberOfSingleDPPSurfaces + 1;
	}
}

static void dcn5_ms_calculate_dispclk_and_dppclk_required(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k, j;
	const struct dml2_stream_parameters *stream;
	double writeback_required_dispclk;

	//DISPCLK/DPPCLK
	mode_lib->ms.WritebackRequiredDISPCLK = 0;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		for (j = 0; j < stream->writeback.active_writebacks_per_stream; ++j) {
			writeback_required_dispclk = dcn5_calculate_write_back_dispclk(
				stream->writeback.writeback_stream[j].pixel_format,
				((double)stream->timing.pixel_clock_khz / 1000),
				mode_lib->ms.ODMMode[k],
				stream->writeback.writeback_stream[j].h_ratio,
				stream->writeback.writeback_stream[j].v_ratio,
				stream->writeback.writeback_stream[j].h_taps,
				stream->writeback.writeback_stream[j].v_taps,
				stream->writeback.writeback_stream[j].h_taps_chroma,
				stream->writeback.writeback_stream[j].v_taps_chroma,
				stream->writeback.writeback_stream[j].input_width,
				stream->writeback.writeback_stream[j].output_width,
				stream->timing.h_total,
				mode_lib->ip.writeback_line_buffer_buffer_size);
			mode_lib->ms.WritebackRequiredDISPCLK = math_max2(
				mode_lib->ms.WritebackRequiredDISPCLK,
				writeback_required_dispclk);
		}
	}

	mode_lib->ms.RequiredDISPCLK = mode_lib->ms.WritebackRequiredDISPCLK;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		mode_lib->ms.RequiredDISPCLK = math_max2(
				mode_lib->ms.RequiredDISPCLK,
				mode_lib->ms.RequiredDISPCLKPerSurface[k]);
	}

	mode_lib->ms.GlobalDPPCLK = 0;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		mode_lib->ms.RequiredDPPCLK[k] = mode_lib->ms.MinDPPCLKUsingSingleDPP[k] / mode_lib->ms.NoOfDPP[k];
		mode_lib->ms.GlobalDPPCLK = math_max2(mode_lib->ms.GlobalDPPCLK, mode_lib->ms.RequiredDPPCLK[k]);
	}
}

static bool dcn5_ms_check_dispclk_and_dppclk_support(struct dml2_core_internal_display_mode_lib *mode_lib)
{

	return (mode_lib->ms.RequiredDISPCLK <= mode_lib->ms.max_dispclk_freq_mhz)
			&& (mode_lib->ms.GlobalDPPCLK <= mode_lib->ms.max_dppclk_freq_mhz);
}

static bool dcn5_ms_check_otg_count_support(
		const struct dml2_display_cfg *display_cfg,
		const struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int TotalNumberOfActiveOTG = 0;
	bool stream_visited[DML2_MAX_PLANES] = { 0 };

	for (unsigned int k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (!stream_visited[display_cfg->plane_descriptors[k].stream_index]) {
			stream_visited[display_cfg->plane_descriptors[k].stream_index] = true;
			TotalNumberOfActiveOTG = TotalNumberOfActiveOTG + 1;
		}
	}
	return TotalNumberOfActiveOTG <= mode_lib->ip.max_num_otg;
}

static bool dcn5_ms_check_hpo_frl_encoder_count_support(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	bool stream_visited[DML2_MAX_PLANES] = { 0 };
	unsigned int totalNumberOfActiveHDMIFRL = 0;

	for (unsigned int k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (!stream_visited[display_cfg->plane_descriptors[k].stream_index]) {
			stream_visited[display_cfg->plane_descriptors[k].stream_index] = true;
			if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_hdmifrl)
				totalNumberOfActiveHDMIFRL++;
		}
	}
	return totalNumberOfActiveHDMIFRL <= mode_lib->ip.max_num_hdmi_frl_outputs;
}

static bool dcn5_ms_check_hpo_dp_encoder_count_support(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	bool stream_visited[DML2_MAX_PLANES] = { 0 };
	unsigned int totalNumberOfActiveDP2p0 = 0;
	unsigned int totalNumberOfActiveDP2p0Outputs = 0;

	for (unsigned int k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (!stream_visited[display_cfg->plane_descriptors[k].stream_index]) {
			stream_visited[display_cfg->plane_descriptors[k].stream_index] = true;
			if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder == dml2_dp2p0) {
				totalNumberOfActiveDP2p0++;
				// FIXME_STAGE2: SW not using backend related stuff, need mapping for mst setup
				//if (display_cfg->output.OutputMultistreamId[k] == k || display_cfg->output.OutputMultistreamEn[k] == false) {
				totalNumberOfActiveDP2p0Outputs++;
			}
		}
	}
	return (totalNumberOfActiveDP2p0 <= mode_lib->ip.max_num_dp2p0_streams)
			&& (totalNumberOfActiveDP2p0Outputs <= mode_lib->ip.max_num_dp2p0_outputs);
}

static bool dcn5_ms_check_writeback_count_support(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int stream_idx;
	unsigned int totalNumberOfActiveWriteback = 0;
	bool writeback_per_stream_supported = true;

	for (stream_idx = 0; stream_idx < display_cfg->num_streams; stream_idx++) {
		totalNumberOfActiveWriteback +=
				display_cfg->stream_descriptors[stream_idx].writeback.active_writebacks_per_stream;

		/* >1 writeback per stream is currently not supported */
		if (display_cfg->stream_descriptors[stream_idx].writeback.active_writebacks_per_stream > 1) {
			writeback_per_stream_supported = false;
		}
	}

	return writeback_per_stream_supported &&
			totalNumberOfActiveWriteback <= mode_lib->ip.max_num_wb;
}

static bool dcn5_ms_check_link_bandwidth_support(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_stream_parameters *stream;
	bool support = true;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		if (!dml2_core_utils_is_stream_encoder_required(stream)
				|| stream->output.output_disabled)
			continue;

		support &= (mode_lib->ms.OutputBpp[k] > 0);
	}
	return support;
}

static void dcn5_ms_check_misc_link_supports(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_stream_parameters *stream;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];

		if (!dml2_core_utils_is_stream_encoder_required(stream))
			continue;

		if (stream->output.output_format == dml2_420
				&& stream->timing.interlaced
				&& mode_lib->ip.ptoi_supported)
			mode_lib->ms.support.P2IWith420 = true;

		if (stream->timing.dsc.enable == dml2_dsc_enable
				|| stream->timing.dsc.enable == dml2_dsc_enable_if_necessary) {
			if (stream->output.output_format == dml2_n422
				&& !mode_lib->ip.dsc422_native_support)
				mode_lib->ms.support.DSC422NativeNotSupported = true;
		}

		if (dml2_core_utils_is_dp_8b_10b_link_rate(stream->output.output_dp_link_rate)
				&& !dml2_core_utils_is_dio_dp_encoder(stream))
			mode_lib->ms.support.LinkRateDoesNotMatchDPVersion = true;
		else if (dml2_core_utils_is_dp_128b_132b_link_rate(stream->output.output_dp_link_rate)
				&& !dml2_core_utils_is_hpo_dp_encoder(stream))
			mode_lib->ms.support.LinkRateDoesNotMatchDPVersion = true;

		if (dml2_core_utils_is_odm_split(stream->overrides.odm_mode)
				&& !dml2_core_utils_is_dp_encoder(stream))
			mode_lib->ms.support.MSOOrODMSplitWithNonDPLink = true;

		if (stream->overrides.odm_mode == dml2_odm_mode_mso_1to2
				&& stream->output.output_dp_lane_count < 2)
			mode_lib->ms.support.NotEnoughLanesForMSO = true;
		else if (stream->overrides.odm_mode == dml2_odm_mode_mso_1to4
				&& stream->output.output_dp_lane_count < 4)
			mode_lib->ms.support.NotEnoughLanesForMSO = true;
	}
}

static void dcn5_ms_calculate_dtbclk_required(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_stream_parameters *stream;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];

		if (stream->output.output_encoder == dml2_hdmifrl) {
			mode_lib->ms.RequiredDTBCLK[k] = dcn5_calculate_required_dtbclk(
				mode_lib->ms.RequiresDSC[k],
				mode_lib->ms.PixelClockBackEnd[k],
				stream->output.output_format,
				mode_lib->ms.OutputBpp[k],
				mode_lib->ms.support.NumberOfDSCSlices[k],
				stream->timing.h_total,
				stream->timing.h_active,
				stream->output.audio_sample_rate,
				stream->output.audio_sample_layout);
		}
	}
}

static bool dcn5_ms_check_dtbclk_support(
		struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	unsigned int k;
	bool support = true;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k)
		if (mode_lib->ms.RequiredDTBCLK[k] > ((double) utm_soc_bb->max_dtbclk_khz / 1000))
			support = false;
	return support;
}

static void dcn5_ms_calculate_dscclk_required(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_stream_parameters *stream;
	unsigned int DSCFormatFactor;
	double pixelClockBackEndFactor;

	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		if (!dml2_core_utils_is_encoder_dsc_capable(stream))
			continue;

		if (stream->output.output_format == dml2_420
				|| stream->output.output_format == dml2_n422)
			DSCFormatFactor = 2;
		else
			DSCFormatFactor = 1;

		DML_LOG_VERBOSE("DML::%s: k=%u, RequiresDSC = %u\n", __func__, k, mode_lib->ms.RequiresDSC[k]);
		if (!mode_lib->ms.RequiresDSC[k])
			continue;

		unsigned int num_dsc_units;

		if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_4to1)
			num_dsc_units = 4;
		else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_3to1)
			num_dsc_units = 3;
		else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_2to1)
			num_dsc_units = 2;
		else
			num_dsc_units = 1;

		pixelClockBackEndFactor = 3.0 * num_dsc_units;

		if (mode_lib->ms.support.NumberOfDSCSlices[k] > num_dsc_units)
			pixelClockBackEndFactor *= 2;

		mode_lib->ms.required_dscclk_freq_mhz[k] = mode_lib->ms.PixelClockBackEnd[k] / pixelClockBackEndFactor / (double) DSCFormatFactor;
		if (mode_lib->ms.required_dscclk_freq_mhz[k] > mode_lib->ms.max_dscclk_freq_mhz)
			mode_lib->ms.support.DSCCLKRequiredMoreThanSupported = true;
		DML_LOG_VERBOSE("DML::%s: k=%u, PixelClockBackEnd = %f\n", __func__, k, mode_lib->ms.PixelClockBackEnd[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, required_dscclk_freq_mhz = %f\n", __func__, k, mode_lib->ms.required_dscclk_freq_mhz[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, DSCFormatFactor = %u\n", __func__, k, DSCFormatFactor);
		DML_LOG_VERBOSE("DML::%s: k=%u, DSCCLKRequiredMoreThanSupported = %u\n", __func__, k, mode_lib->ms.support.DSCCLKRequiredMoreThanSupported);
	}
}

static bool dcn5_ms_check_dscclk_support(
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	bool support = true;

	for (k = 0; k < mode_lib->ms.num_active_planes; k++)
		if (mode_lib->ms.required_dscclk_freq_mhz[k] > mode_lib->ms.max_dscclk_freq_mhz)
			support = false;
	return support;
}

static void dcn5_ms_check_dsc_engine_supports(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_stream_parameters *stream;
	unsigned int totalDSCUnitsRequired = 0;
	unsigned int numDSCUnitRequired;
	unsigned int stream_visited_bit_map = 0;
	unsigned int stream_index;

	mode_lib->ms.support.PixelsPerLinePerDSCUnitSupport = true;
	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		stream_index = display_cfg->plane_descriptors[k].stream_index;

		/* Check if stream has been visited */
		if (stream_visited_bit_map & (1 << stream_index))
			continue;

		/* Mark stream as visited */
		stream_visited_bit_map |= (1 << stream_index);

		if (!mode_lib->ms.RequiresDSC[k])
			continue;

		if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_4to1)
			numDSCUnitRequired = 4;
		else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_3to1)
			numDSCUnitRequired = 3;
		else if (mode_lib->ms.ODMMode[k] == dml2_odm_mode_combine_2to1)
			numDSCUnitRequired = 2;
		else
			numDSCUnitRequired = 1;

		stream = &display_cfg->stream_descriptors[stream_index];

		if (stream->timing.h_active > numDSCUnitRequired * mode_lib->ip.maximum_pixels_per_line_per_dsc_unit)
			mode_lib->ms.support.PixelsPerLinePerDSCUnitSupport = false;
		totalDSCUnitsRequired += numDSCUnitRequired;

		if (mode_lib->ms.support.NumberOfDSCSlices[k] > numDSCUnitRequired * mode_lib->ip.maximum_dsc_slices_per_pipe)
			mode_lib->ms.support.NotEnoughDSCSlices = true;
	}

	if (totalDSCUnitsRequired > mode_lib->ip.num_dsc)
		mode_lib->ms.support.NotEnoughDSCUnits = true;
}

static void dcn5_ms_calculate_dsc_delay(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_stream_parameters *stream;

	/*DSC Delay per state*/
	for (k = 0; k < display_cfg->num_planes; ++k) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		mode_lib->ms.DSCDelay[k] = dcn5_calculate_dsc_delay_requirement(
				mode_lib->ms.RequiresDSC[k],
				mode_lib->ms.ODMMode[k],
				mode_lib->ip.maximum_dsc_bits_per_component,
				mode_lib->ms.DesiredOutputBpp[k],
				stream->timing.h_active,
				stream->timing.h_total,
				mode_lib->ms.support.NumberOfDSCSlices[k],
				stream->output.output_format,
				stream->output.output_encoder,
				((double) stream->timing.pixel_clock_khz / 1000),
				mode_lib->ms.PixelClockBackEnd[k],
				mode_lib->ms.use_legacy_dsc_delay_formula);
	}
}

static void dcn5_ms_calculate_swath_and_det_configuration(
		struct dml2_core_internal_display_mode_lib *mode_lib,
		struct dml2_core_calcs_mode_support_locals *s)
{
	struct dml2_core_calcs_CalculateSwathAndDETConfiguration_params *p =
				&mode_lib->scratch.CalculateSwathAndDETConfiguration_params;

	// Figure out the swath and DET configuration after the num dpp per plane is figured out
	p->ForceSingleDPP = false;
	p->ODMMode = mode_lib->ms.ODMMode;
	p->DPPPerSurface = mode_lib->ms.NoOfDPP;
	// output
	p->req_per_swath_ub_l = s->dummy_integer_array[0];
	p->req_per_swath_ub_c = s->dummy_integer_array[1];
	p->swath_width_luma_ub = mode_lib->ms.swath_width_luma_ub;
	p->swath_width_chroma_ub = mode_lib->ms.swath_width_chroma_ub;
	p->SwathWidth = mode_lib->ms.SwathWidthY;
	p->SwathWidthChroma = mode_lib->ms.SwathWidthC;
	p->SwathHeightY = mode_lib->ms.SwathHeightY;
	p->SwathHeightC = mode_lib->ms.SwathHeightC;
	p->request_size_bytes_luma = mode_lib->ms.support.request_size_bytes_luma;
	p->request_size_bytes_chroma = mode_lib->ms.support.request_size_bytes_chroma;
	p->DETBufferSizeInKByte = mode_lib->ms.DETBufferSizeInKByte; // FIXME: This is per pipe but the pipes in plane will use that
	p->DETBufferSizeY = mode_lib->ms.DETBufferSizeY;
	p->DETBufferSizeC = mode_lib->ms.DETBufferSizeC;
	p->UnboundedRequestEnabled = &mode_lib->ms.UnboundedRequestEnabled;
	p->compbuf_reserved_space_64b = s->dummy_integer_array[3];
	p->hw_debug5 = s->dummy_boolean_array[1];
	p->CompressedBufferSizeInkByte = &mode_lib->ms.CompressedBufferSizeInkByte;
	p->ViewportSizeSupportPerSurface = s->dummy_boolean_array[0];
	p->ViewportSizeSupport = &mode_lib->ms.support.ViewportSizeSupport;
	dcn5_calculate_swath_and_det_configuration(&mode_lib->scratch, p);
}

static void dcn5_ms_calculate_total_num_of_dcc_active_dpp(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_plane_parameters *plane;

	mode_lib->ms.TotalNumberOfDCCActiveDPP = 0;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		plane = &display_cfg->plane_descriptors[k];
		if (plane->surface.dcc.enable == true) {
			mode_lib->ms.TotalNumberOfDCCActiveDPP =
					mode_lib->ms.TotalNumberOfDCCActiveDPP + mode_lib->ms.NoOfDPP[k];
		}
	}
}

static void dcn5_ms_calculate_vm_row_and_swath_and_calculate_dcc_meta_cache_requirements(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib,
		struct dml2_core_calcs_mode_support_locals *s)
{
	unsigned int k;
	struct dml2_core_calcs_CalculateVMRowAndSwath_params *p;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;
	struct dml2_core_internal_DmlPipe *surfParameters;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];
		surfParameters = &s->SurfParameters[k];

		surfParameters->PixelClock = ((double) stream->timing.pixel_clock_khz / 1000);
		surfParameters->DPPPerSurface = mode_lib->ms.NoOfDPP[k];
		surfParameters->RotationAngle = plane->composition.rotation_angle;
		surfParameters->ViewportHeight = plane->composition.viewport.plane0.height;
		surfParameters->ViewportHeightC = plane->composition.viewport.plane1.height;
		surfParameters->BlockWidth256BytesY = mode_lib->ms.Read256BlockWidthY[k];
		surfParameters->BlockHeight256BytesY = mode_lib->ms.Read256BlockHeightY[k];
		surfParameters->BlockWidth256BytesC = mode_lib->ms.Read256BlockWidthC[k];
		surfParameters->BlockHeight256BytesC = mode_lib->ms.Read256BlockHeightC[k];
		surfParameters->BlockWidthY = mode_lib->ms.MacroTileWidthY[k];
		surfParameters->BlockHeightY = mode_lib->ms.MacroTileHeightY[k];
		surfParameters->BlockWidthC = mode_lib->ms.MacroTileWidthC[k];
		surfParameters->BlockHeightC = mode_lib->ms.MacroTileHeightC[k];
		surfParameters->InterlaceEnable = stream->timing.interlaced;
		surfParameters->HTotal = stream->timing.h_total;
		surfParameters->DCCEnable = plane->surface.dcc.enable;
		surfParameters->SourcePixelFormat = plane->pixel_format;
		surfParameters->SurfaceTiling = plane->surface.tiling;
		surfParameters->BytePerPixelY = mode_lib->ms.BytePerPixelY[k];
		surfParameters->BytePerPixelC = mode_lib->ms.BytePerPixelC[k];
		surfParameters->ProgressiveToInterlaceUnitInOPP = mode_lib->ip.ptoi_supported;
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
		surfParameters->SwathHeightY = mode_lib->ms.SwathHeightY[k];
		surfParameters->SwathHeightC = mode_lib->ms.SwathHeightC[k];
		surfParameters->DCCMetaPitchY = plane->surface.dcc.plane0.pitch;
		surfParameters->DCCMetaPitchC = plane->surface.dcc.plane1.pitch;
		surfParameters->UPSPEnabled = plane->composition.scaler_info.upsp_enabled;
		surfParameters->UPSPVTaps = plane->composition.scaler_info.upsp_vtaps;
		surfParameters->UPSPSamplePositioning = plane->composition.scaler_info.upsp_sample_positioning;
	}

	p = &mode_lib->scratch.CalculateVMRowAndSwath_params;
	p->display_cfg = display_cfg;
	p->uclk_pstate_switch_modes = mode_lib->ms.uclk_pstate_switch_modes;
	p->NumberOfActiveSurfaces = mode_lib->ms.num_active_planes;
	p->myPipe = s->SurfParameters;
	p->PTEBufferSizeInRequestsLuma = mode_lib->ip.dpte_buffer_size_in_pte_reqs_luma;
	p->PTEBufferSizeInRequestsChroma = mode_lib->ip.dpte_buffer_size_in_pte_reqs_chroma;
	p->SwathWidthY = mode_lib->ms.SwathWidthY;
	p->SwathWidthC = mode_lib->ms.SwathWidthC;
	p->DCCMetaBufferSizeBytes = mode_lib->ip.dcc_meta_buffer_size_bytes;
	p->mrq_present = mode_lib->ip.dcn_mrq_present;
	// output
	p->PTEBufferSizeNotExceeded = mode_lib->ms.PTEBufferSizeNotExceeded;
	p->dpte_row_width_luma_ub = s->dummy_integer_array[12];
	p->dpte_row_width_chroma_ub = s->dummy_integer_array[13];
	p->dpte_row_height_luma = mode_lib->ms.dpte_row_height;
	p->dpte_row_height_chroma = mode_lib->ms.dpte_row_height_chroma;
	p->dpte_row_height_linear_luma = s->dummy_integer_array[14]; // VBA_DELTA
	p->dpte_row_height_linear_chroma = s->dummy_integer_array[15]; // VBA_DELTA
	p->vm_group_bytes = s->dummy_integer_array[16];
	p->dpte_group_bytes = mode_lib->ms.dpte_group_bytes;
	p->PixelPTEReqWidthY = s->dummy_integer_array[17];
	p->PixelPTEReqHeightY = s->dummy_integer_array[18];
	p->PTERequestSizeY = s->dummy_integer_array[19];
	p->PixelPTEReqWidthC = s->dummy_integer_array[20];
	p->PixelPTEReqHeightC = s->dummy_integer_array[21];
	p->PTERequestSizeC = s->dummy_integer_array[22];
	p->vmpg_width_y = mode_lib->ms.vmpg_width_y;
	p->vmpg_height_y = mode_lib->ms.vmpg_height_y;
	p->vmpg_width_c = mode_lib->ms.vmpg_width_c;
	p->vmpg_height_c = mode_lib->ms.vmpg_height_c;
	p->dpde0_bytes_per_frame_ub_l = s->dummy_integer_array[23];
	p->dpde0_bytes_per_frame_ub_c = s->dummy_integer_array[24];
	p->PrefetchSourceLinesY = mode_lib->ms.PrefetchLinesY;
	p->PrefetchSourceLinesC = mode_lib->ms.PrefetchLinesC;
	p->VInitPreFillY = mode_lib->ms.PrefillY;
	p->VInitPreFillC = mode_lib->ms.PrefillC;
	p->MaxNumSwathY = mode_lib->ms.MaxNumSwathY;
	p->MaxNumSwathC = mode_lib->ms.MaxNumSwathC;
	p->dpte_row_bw = mode_lib->ms.dpte_row_bw;
	p->PixelPTEBytesPerRow = mode_lib->ms.DPTEBytesPerRow;
	p->dpte_row_bytes_per_row_l = mode_lib->ms.dpte_row_bytes_per_row_l;
	p->dpte_row_bytes_per_row_c = mode_lib->ms.dpte_row_bytes_per_row_c;
	p->vm_bytes = mode_lib->ms.vm_bytes;
	p->use_one_row_for_frame = mode_lib->ms.use_one_row_for_frame;
	p->use_one_row_for_frame_flip = mode_lib->ms.use_one_row_for_frame_flip;
	p->PTE_BUFFER_MODE = s->dummy_boolean_array[1];
	p->BIGK_FRAGMENT_SIZE = s->dummy_integer_array[25];
	p->DCCMetaBufferSizeNotExceeded = mode_lib->ms.DCCMetaBufferSizeNotExceeded;
	p->meta_row_bw = mode_lib->ms.meta_row_bw;
	p->meta_row_bytes = mode_lib->ms.meta_row_bytes;
	p->meta_row_bytes_per_row_ub_l = mode_lib->ms.meta_row_bytes_per_row_ub_l;
	p->meta_row_bytes_per_row_ub_c = mode_lib->ms.meta_row_bytes_per_row_ub_c;
	p->meta_req_width_luma = s->dummy_integer_array[26];
	p->meta_req_height_luma = s->dummy_integer_array[27];
	p->meta_row_width_luma = s->dummy_integer_array[28];
	p->meta_row_height_luma = mode_lib->ms.meta_row_height_luma;
	p->meta_pte_bytes_per_frame_ub_l = s->dummy_integer_array[29];
	p->meta_req_width_chroma = s->dummy_integer_array[30];
	p->meta_req_height_chroma = s->dummy_integer_array[31];
	p->meta_row_width_chroma = s->dummy_integer_array[32];
	p->meta_row_height_chroma = mode_lib->ms.meta_row_height_chroma;
	p->meta_pte_bytes_per_frame_ub_c = s->dummy_integer_array[33];

	dcn5_calculate_vm_row_and_swath(&mode_lib->scratch, p);
}

static bool dcn5_ms_check_pte_buffer_size_support(
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	bool support = true;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.PTEBufferSizeNotExceeded[k] == false)
			support = false;

		DML_LOG_VERBOSE("DML::%s: k=%u, PTEBufferSizeNotExceeded = %u\n", __func__, k,
				mode_lib->ms.PTEBufferSizeNotExceeded[k]);
	}
	DML_LOG_VERBOSE("DML::%s: PTEBufferSizeNotExceeded = %u\n", __func__, support);
	return support;
}

static bool dcn5_ms_check_dcc_meta_cache_support(
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	bool support = true;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.DCCMetaBufferSizeNotExceeded[k] == false)
			support = false;

		DML_LOG_VERBOSE("DML::%s: k=%u, DCCMetaBufferSizeNotExceeded = %u\n", __func__, k,
				mode_lib->ms.DCCMetaBufferSizeNotExceeded[k]);
	}
	DML_LOG_VERBOSE("DML::%s: DCCMetaBufferSizeNotExceeded = %u\n", __func__, support);
	return support;
}

static void dcn5_ms_calculate_vactive_uclk_pstate_requirements(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	unsigned int k;
	struct dml2_core_calcs_calculate_bytes_to_fetch_required_to_hide_latency_params *p =
			&mode_lib->scratch.calculate_bytes_to_fetch_required_to_hide_latency_params;

	/* VActive bytes to fetch for UCLK P-State */
	p->display_cfg = display_cfg;
	p->mrq_present = mode_lib->ip.dcn_mrq_present;
	p->num_active_planes = mode_lib->ms.num_active_planes;
	p->num_of_dpp = mode_lib->ms.NoOfDPP;
	p->meta_row_height_l = mode_lib->ms.meta_row_height_luma;
	p->meta_row_height_c = mode_lib->ms.meta_row_height_chroma;
	p->meta_row_bytes_per_row_ub_l = mode_lib->ms.meta_row_bytes_per_row_ub_l;
	p->meta_row_bytes_per_row_ub_c = mode_lib->ms.meta_row_bytes_per_row_ub_c;
	p->dpte_row_height_l = mode_lib->ms.dpte_row_height;
	p->dpte_row_height_c = mode_lib->ms.dpte_row_height_chroma;
	p->dpte_bytes_per_row_l = mode_lib->ms.dpte_row_bytes_per_row_l;
	p->dpte_bytes_per_row_c = mode_lib->ms.dpte_row_bytes_per_row_c;
	p->byte_per_pix_l = mode_lib->ms.BytePerPixelY;
	p->byte_per_pix_c = mode_lib->ms.BytePerPixelC;
	p->swath_width_l = mode_lib->ms.SwathWidthY;
	p->swath_width_c = mode_lib->ms.SwathWidthC;
	p->swath_height_l = mode_lib->ms.SwathHeightY;
	p->swath_height_c = mode_lib->ms.SwathHeightC;
	for (k = 0; k < display_cfg->num_planes; k++) {
		p->latency_to_hide_us[k] = utm_soc_bb->power_management_parameters.dram_clk_change_blackout_us;
	}
	/* outputs */
	p->bytes_required_l = mode_lib->ms.pstate_bytes_required_l[dml2_pstate_type_uclk];
	p->bytes_required_c = mode_lib->ms.pstate_bytes_required_c[dml2_pstate_type_uclk];
	dcn5_calculate_bytes_to_fetch_required_to_hide_latency(p);

	/* Excess VActive bandwidth required to fill DET */
	dcn5_calculate_excess_vactive_bandwidth_required(display_cfg,
			mode_lib->ms.num_active_planes,
			mode_lib->ms.pstate_bytes_required_l[dml2_pstate_type_uclk],
			mode_lib->ms.pstate_bytes_required_c[dml2_pstate_type_uclk],

			/* outputs */
			mode_lib->ms.excess_vactive_fill_bw_l,
			mode_lib->ms.excess_vactive_fill_bw_c);
}

/* FIXME - break it down according the function name */
static void dcn5_ms_calculate_det_buffer_time_value_urgent_burst_factor_and_urgent_latency_hiding(
		const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;
	double line_time_us;
	bool cursor_not_enough_urgent_latency_hiding;
	unsigned int cursor_lines_per_chunk;
	unsigned int cursor_bytes;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];
		line_time_us = stream->timing.h_total / ((double) stream->timing.pixel_clock_khz / 1000);
		cursor_not_enough_urgent_latency_hiding = 0;
		dcn5_calculate_cursor_req_attributes(
				plane->cursor.cursor_width,
				plane->cursor.cursor_bpp,
				// output
				&cursor_lines_per_chunk,
				&mode_lib->ms.cursor_bytes_per_line[k],
				&mode_lib->ms.cursor_bytes_per_chunk[k],
				&cursor_bytes);
		dcn5_calculate_cursor_urgent_burst_factor(
				mode_lib->ip.cursor_buffer_size,
				plane->cursor.cursor_width,
				mode_lib->ms.cursor_bytes_per_chunk[k],
				cursor_lines_per_chunk, line_time_us,
				mode_lib->ms.UrgLatency,

				// output
				&mode_lib->ms.UrgentBurstFactorCursor[k],
				&cursor_not_enough_urgent_latency_hiding);
		mode_lib->ms.UrgentBurstFactorCursorPre[k] = mode_lib->ms.UrgentBurstFactorCursor[k];
		DML_LOG_VERBOSE("DML::%s: k=%d, Calling CalculateUrgentBurstFactor\n", __func__, k);
		DML_LOG_VERBOSE("DML::%s: k=%d, VRatio=%f\n", __func__, k,
				plane->composition.scaler_info.plane0.v_ratio);
		DML_LOG_VERBOSE("DML::%s: k=%d, VRatioChroma=%f\n", __func__, k,
				plane->composition.scaler_info.plane1.v_ratio);
		dcn5_calculate_urgent_burst_factor(
				&display_cfg->plane_descriptors[k],
				mode_lib->ms.swath_width_luma_ub[k],
				mode_lib->ms.swath_width_chroma_ub[k],
				mode_lib->ms.SwathHeightY[k],
				mode_lib->ms.SwathHeightC[k],
				line_time_us,
				mode_lib->ms.UrgLatency,
				plane->composition.scaler_info.plane0.v_ratio,
				plane->composition.scaler_info.plane1.v_ratio,
				mode_lib->ms.BytePerPixelInDETY[k],
				mode_lib->ms.BytePerPixelInDETC[k],
				mode_lib->ms.DETBufferSizeY[k],
				mode_lib->ms.DETBufferSizeC[k],

				// Output
				&mode_lib->ms.UrgentBurstFactorLuma[k],
				&mode_lib->ms.UrgentBurstFactorChroma[k],
				&mode_lib->ms.NotEnoughUrgentLatencyHiding[k]);

		mode_lib->ms.NotEnoughUrgentLatencyHiding[k] = mode_lib->ms.NotEnoughUrgentLatencyHiding[k]
						|| cursor_not_enough_urgent_latency_hiding;
	}
}

static void dcn5_ms_calculate_min_dcfclk_deepsleep_clock(const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	dcn5_calculate_dcfclk_deep_sleep(
			display_cfg,
			mode_lib->ms.num_active_planes,
			mode_lib->ms.BytePerPixelY,
			mode_lib->ms.BytePerPixelC,
			mode_lib->ms.SwathWidthY,
			mode_lib->ms.SwathWidthC,
			mode_lib->ms.NoOfDPP,
			mode_lib->ms.PSCL_FACTOR,
			mode_lib->ms.PSCL_FACTOR_CHROMA,
			mode_lib->ms.RequiredDPPCLK,
			mode_lib->ms.vactive_sw_bw_l,
			mode_lib->ms.vactive_sw_bw_c,
			utm_soc_bb->return_bus_width_bytes,
			/* Output */
			&mode_lib->ms.dcfclk_deepsleep);
}

static void dcn5_ms_calculate_writeback_delay(const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	unsigned int k;
	unsigned int j;
	const struct dml2_stream_parameters *stream;

	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		mode_lib->ms.WritebackDelayTime[k] = 0.0;
		for (j = 0; j < stream->writeback.active_writebacks_per_stream; j++) {
			mode_lib->ms.WritebackDelayTime[k] = math_max2(mode_lib->ms.WritebackDelayTime[k],
					utm_soc_bb->writeback_base_latency_us
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
							/ mode_lib->ms.RequiredDISPCLK);
		}
	}
}

static void dcn5_ms_calculate_max_vstartup(const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_stream_parameters *stream;

	// MaximumVStartup is actually Tvstartup_min in DCN4 programming guide
	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		mode_lib->ms.MaximumVStartup[k] = dcn5_calculate_max_vstartup(
				mode_lib->ip.ptoi_supported,
				mode_lib->ip.vblank_nom_default_us,
				&stream->timing,
				mode_lib->ms.WritebackDelayTime[k]);
	}
	DML_LOG_VERBOSE("DML::%s: k=%u, MaximumVStartup = %u\n", __func__, k, mode_lib->ms.MaximumVStartup[k]);
}

static void dcn5_ms_check_average_latency_supports(struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	double outstanding_latency_us = 0;
	unsigned int k;

	mode_lib->ms.support.OutstandingRequestsSupport = true;
	mode_lib->ms.support.OutstandingRequestsUrgencyAvoidance = true;
	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		outstanding_latency_us = utm_soc_bb->max_outstanding_reqs
				* mode_lib->ms.support.request_size_bytes_luma[k]
				/ (mode_lib->ms.DCFCLK * utm_soc_bb->return_bus_width_bytes);
		if (outstanding_latency_us < mode_lib->ms.support.avg_urgent_latency_us) {
			mode_lib->ms.support.OutstandingRequestsSupport = false;
			DML_LOG_VERBOSE("DML::%s: DCFCLK = %f\n", __func__, mode_lib->ms.DCFCLK);
		}
		if (outstanding_latency_us < mode_lib->ms.support.avg_non_urgent_latency_us) {
			mode_lib->ms.support.OutstandingRequestsUrgencyAvoidance = false;
		}
		DML_LOG_VERBOSE("DML::%s: avg_urgent_latency_us = %f\n", __func__, mode_lib->ms.support.avg_urgent_latency_us);
		DML_LOG_VERBOSE("DML::%s: avg_non_urgent_latency_us = %f\n", __func__, mode_lib->ms.support.avg_non_urgent_latency_us);
		DML_LOG_VERBOSE("DML::%s: k=%d, request_size_bytes_luma = %d\n", __func__, k, mode_lib->ms.support.request_size_bytes_luma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, outstanding_latency_us = %f (luma)\n", __func__, k, outstanding_latency_us);
		if (mode_lib->ms.BytePerPixelC[k] > 0) {
			outstanding_latency_us = utm_soc_bb->max_outstanding_reqs
					* mode_lib->ms.support.request_size_bytes_chroma[k]
					/ (mode_lib->ms.DCFCLK * utm_soc_bb->return_bus_width_bytes);
			if (outstanding_latency_us < mode_lib->ms.support.avg_urgent_latency_us) {
				mode_lib->ms.support.OutstandingRequestsSupport = false;
			}
			if (outstanding_latency_us < mode_lib->ms.support.avg_non_urgent_latency_us) {
				mode_lib->ms.support.OutstandingRequestsUrgencyAvoidance = false;
			}
			DML_LOG_VERBOSE("DML::%s: k=%d, request_size_bytes_chroma = %d\n", __func__, k, mode_lib->ms.support.request_size_bytes_chroma[k]);
			DML_LOG_VERBOSE("DML::%s: k=%d, outstanding_latency_us = %f (chroma)\n", __func__, k, outstanding_latency_us);
		}
	}
}

static void dcn5_ms_calculate_mcache_setting(const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	struct dml2_core_calcs_calculate_mcache_setting_params *p;

	if (utm_soc_bb->mcache_size_bytes == 0 || mode_lib->ip.dcn_mrq_present) {
		for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
			mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p0[k] = 1.0;
			mode_lib->ms.dcc_dram_bw_pref_overhead_factor_p0[k] = 1.0;
			mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p1[k] = 1.0;
			mode_lib->ms.dcc_dram_bw_pref_overhead_factor_p1[k] = 1.0;
		}
	} else {
		p = &mode_lib->scratch.calculate_mcache_setting_params;
		memset(p, 0, sizeof(struct dml2_core_calcs_calculate_mcache_setting_params));
		for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
			plane = &display_cfg->plane_descriptors[k];
			p->dcc_enable = plane->surface.dcc.enable;
			p->num_chans = utm_soc_bb->dram_config.channel_count;
			p->mem_word_bytes = utm_soc_bb->mem_word_bytes;
			p->mcache_size_bytes = utm_soc_bb->mcache_size_bytes;
			p->mcache_line_size_bytes = utm_soc_bb->mcache_line_size_bytes;
			p->gpuvm_enable = display_cfg->gpuvm_enable;
			p->gpuvm_page_size_kbytes = plane->overrides.gpuvm_min_page_size_kbytes;
			p->source_format = plane->pixel_format;
			p->surf_vert = dml2_core_utils_is_vertical_rotation(plane->composition.rotation_angle);
			p->vp_stationary = plane->composition.viewport.stationary;
			p->tiling_mode = plane->surface.tiling;
			p->imall_enable = mode_lib->ip.imall_supported;
			p->vp_start_x_l = plane->composition.viewport.plane0.x_start;
			p->vp_start_y_l = plane->composition.viewport.plane0.y_start;
			p->full_vp_width_l = plane->composition.viewport.plane0.width;
			p->full_vp_height_l = plane->composition.viewport.plane0.height;
			p->blk_width_l = mode_lib->ms.MacroTileWidthY[k];
			p->blk_height_l = mode_lib->ms.MacroTileHeightY[k];
			p->vmpg_width_l = mode_lib->ms.vmpg_width_y[k];
			p->vmpg_height_l = mode_lib->ms.vmpg_height_y[k];
			p->full_swath_bytes_l = mode_lib->ms.full_swath_bytes_l[k];
			p->bytes_per_pixel_l = mode_lib->ms.BytePerPixelY[k];
			p->vp_start_x_c = plane->composition.viewport.plane1.x_start;
			p->vp_start_y_c = plane->composition.viewport.plane1.y_start;
			p->full_vp_width_c = plane->composition.viewport.plane1.width;
			p->full_vp_height_c = plane->composition.viewport.plane1.height;
			p->blk_width_c = mode_lib->ms.MacroTileWidthC[k];
			p->blk_height_c = mode_lib->ms.MacroTileHeightC[k];
			p->vmpg_width_c = mode_lib->ms.vmpg_width_c[k];
			p->vmpg_height_c = mode_lib->ms.vmpg_height_c[k];
			p->full_swath_bytes_c = mode_lib->ms.full_swath_bytes_c[k];
			p->bytes_per_pixel_c = mode_lib->ms.BytePerPixelC[k];
			// output
			p->dcc_dram_bw_nom_overhead_factor_l = &mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p0[k];
			p->dcc_dram_bw_pref_overhead_factor_l = &mode_lib->ms.dcc_dram_bw_pref_overhead_factor_p0[k];
			p->dcc_dram_bw_nom_overhead_factor_c = &mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p1[k];
			p->dcc_dram_bw_pref_overhead_factor_c = &mode_lib->ms.dcc_dram_bw_pref_overhead_factor_p1[k];
			p->num_mcaches_l = &mode_lib->ms.num_mcaches_l[k];
			p->mcache_row_bytes_l = &mode_lib->ms.mcache_row_bytes_l[k];
			p->mcache_row_bytes_per_channel_l = &mode_lib->ms.mcache_row_bytes_per_channel_l[k];
			p->mcache_offsets_l = mode_lib->ms.mcache_offsets_l[k];
			p->mcache_shift_granularity_l = &mode_lib->ms.mcache_shift_granularity_l[k];
			p->num_mcaches_c = &mode_lib->ms.num_mcaches_c[k];
			p->mcache_row_bytes_c = &mode_lib->ms.mcache_row_bytes_c[k];
			p->mcache_row_bytes_per_channel_c = &mode_lib->ms.mcache_row_bytes_per_channel_c[k];
			p->mcache_offsets_c = mode_lib->ms.mcache_offsets_c[k];
			p->mcache_shift_granularity_c = &mode_lib->ms.mcache_shift_granularity_c[k];
			p->mall_comb_mcache_l = &mode_lib->ms.mall_comb_mcache_l[k];
			p->mall_comb_mcache_c = &mode_lib->ms.mall_comb_mcache_c[k];
			p->lc_comb_mcache = &mode_lib->ms.lc_comb_mcache[k];
			dcn5_calculate_mcache_setting(&mode_lib->scratch, p);
		}
	}
}

static void dcn5_ms_calculate_avg_bandwidth_and_dcfclk_lb_required(const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib, const struct dml2_utm_soc_bb *utm_soc_bb)
{
	(void)display_cfg;
	// Average BW support check
	dcn5_calculate_avg_bandwidth_required(
			*mode_lib->ms.support.avg_bandwidth_required,
			// input
			mode_lib->ms.num_active_planes,
			mode_lib->ms.vactive_sw_bw_l,
			mode_lib->ms.vactive_sw_bw_c,
			mode_lib->ms.cursor_bw,
			mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p0,
			mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p1);

	DML_LOG_VERBOSE("DML::%s: lower_bound_bandwidth_dchub=%f\n", __func__, utm_soc_bb->lower_bound_bandwidth_dchub);
	DML_LOG_VERBOSE("DML::%s: avg_bandwidth_required=%f\n", __func__, mode_lib->ms.support.avg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp]);

	mode_lib->ms.DCFCLK = math_max2(utm_soc_bb->lower_bound_bandwidth_dchub * 1000.0, mode_lib->ms.support.avg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp])
		/ utm_soc_bb->return_bus_width_bytes;

	DML_LOG_VERBOSE("DML::%s: DCFCLK=%f\n", __func__, mode_lib->ms.DCFCLK);
}

static bool dcn5_ms_check_urgent_latency_hiding_support(
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	bool support = true;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.NotEnoughUrgentLatencyHiding[k]) {
			support = false;
			DML_LOG_VERBOSE("DML::%s: k=%u NotEnoughUrgentLatencyHiding set\n", __func__, k);
		}
	}
	return support;
}

static void dcn5_ms_calculate_t_calc(struct dml2_core_internal_display_mode_lib *mode_lib)
{
	mode_lib->ms.TimeCalc = 24 / mode_lib->ms.dcfclk_deepsleep;
}

static void dcn5_ms_calculate_hostvm_inefficiency_factor(const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	dcn5_calculate_hostvm_inefficiency_factor(
			&mode_lib->ms.HostVMInefficiencyFactor,
			&mode_lib->ms.HostVMInefficiencyFactorPrefetch,
			display_cfg->gpuvm_enable,
			display_cfg->hostvm_enable,
			mode_lib->ip.remote_iommu_outstanding_translations,
			utm_soc_bb->max_outstanding_reqs,
			1.0,
			0.5);
}

// Using approximate ratio for VM bandwidth

static void dcn5_ms_calculate_3dlut_settings(const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	struct dml2_core_calcs_calculate_tdlut_setting_params *p = &mode_lib->scratch.calculate_tdlut_setting_params;
	unsigned int tdlut_groups_per_2row_ub;

	mode_lib->ms.Total3dlutActive = 0;
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		plane = &display_cfg->plane_descriptors[k];
		if (plane->tdlut.setup_for_tdlut)
			mode_lib->ms.Total3dlutActive = mode_lib->ms.Total3dlutActive + 1;

		// Calculate tdlut schedule related terms
		p->dispclk_mhz = mode_lib->ms.RequiredDISPCLK;
		p->setup_for_tdlut = plane->tdlut.setup_for_tdlut;
		p->tdlut_width_mode = plane->tdlut.tdlut_width_mode;
		p->tdlut_addressing_mode = plane->tdlut.tdlut_addressing_mode;
		p->cursor_buffer_size = mode_lib->ip.cursor_buffer_size;
		p->gpuvm_enable = display_cfg->gpuvm_enable;
		p->gpuvm_page_size_kbytes = plane->overrides.gpuvm_min_page_size_kbytes;
		p->tdlut_mpc_width_flag = plane->tdlut.tdlut_mpc_width_flag;
		/* TODO: The logic casts integer version value into boolean. This doesn't seem to be correct. */
		p->is_gfx11 = dml2_core_utils_get_gfx_version(plane->surface.tiling);
		// output
		p->tdlut_pte_bytes_per_frame = &mode_lib->ms.tdlut_pte_bytes_per_frame[k];
		p->tdlut_bytes_per_frame = &mode_lib->ms.tdlut_bytes_per_frame[k];
		p->tdlut_groups_per_2row_ub = &tdlut_groups_per_2row_ub;
		p->tdlut_opt_time = &mode_lib->ms.tdlut_opt_time[k];
		p->tdlut_drain_time = &mode_lib->ms.tdlut_drain_time[k];
		p->tdlut_bytes_per_group = &mode_lib->ms.tdlut_bytes_per_group[k];
		dcn5_calculate_tdlut_setting(&mode_lib->scratch, p);
	}
}

static void dcn5_ms_calculate_urgent_latency(const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	double min_return_bw_for_latency = mode_lib->ms.DCFCLK * utm_soc_bb->return_bus_width_bytes;

	DML_LOG_VERBOSE("DML::%s: DCFCLK=%f\n", __func__, mode_lib->ms.DCFCLK);
	DML_LOG_VERBOSE("DML::%s: return_bus_width_bytes=%ld\n", __func__, utm_soc_bb->return_bus_width_bytes);
	DML_LOG_VERBOSE("DML::%s: min_return_bw_for_latency=%f\n", __func__, min_return_bw_for_latency);

	dcn5_calculate_extra_latency(
			display_cfg,
			mode_lib->ip.rob_buffer_size_kbytes,
			0,
			0,
			mode_lib->ms.DCFCLK,
			0.0,
			mode_lib->ip.pixel_chunk_size_kbytes,
			min_return_bw_for_latency,
			mode_lib->ms.num_active_planes,
			mode_lib->ms.NoOfDPP,
			mode_lib->ms.dpte_group_bytes,
			mode_lib->ms.tdlut_bytes_per_group,
			mode_lib->ms.HostVMInefficiencyFactor,
			mode_lib->ms.HostVMInefficiencyFactorPrefetch,
			dml2_qos_param_type_dcn4x,
			!(display_cfg->overrides.max_outstanding_when_urgent_expected_disable),
			utm_soc_bb->max_outstanding_reqs,
			mode_lib->ms.support.request_size_bytes_luma,
			mode_lib->ms.support.request_size_bytes_chroma,
			mode_lib->ip.meta_chunk_size_kbytes,
			mode_lib->ip.dchub_arb_to_ret_delay,
			mode_lib->ms.TripToMemory,
			mode_lib->ip.hostvm_mode,
			// output
			&mode_lib->ms.ExtraLatency,
			&mode_lib->ms.ExtraLatency_sr,
			&mode_lib->ms.ExtraLatencyPrefetch);
}

static void dcn5_ms_calculate_prefetch_schedule(const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_core_calcs_mode_support_locals *s,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;
	struct dml2_core_internal_DmlPipe *pipe = &s->myPipe;
	struct dml2_core_calcs_CalculatePrefetchSchedule_params *p = &mode_lib->scratch.CalculatePrefetchSchedule_params;
	double Tvm_trip;
	double Tr0_trip;
	double prefetch_sw_bytes;
	double Tpre_rounded;
	double Tpre_oto;

	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];
		mode_lib->ms.TWait[k] = dcn5_calculate_t_wait(
				plane->overrides.reserved_vblank_time_ns,
				mode_lib->ms.UrgLatency,
				mode_lib->ms.TripToMemory,
				utm_soc_bb->power_management_parameters.g7_ppt_blackout_us,
				display_cfg->stream_descriptors->timing.drr_config.enabled);
		pipe->Dppclk = mode_lib->ms.RequiredDPPCLK[k];
		pipe->Dispclk = mode_lib->ms.RequiredDISPCLK;
		pipe->PixelClock = ((double) stream->timing.pixel_clock_khz / 1000);
		pipe->DCFClkDeepSleep = mode_lib->ms.dcfclk_deepsleep;
		pipe->DPPPerSurface = mode_lib->ms.NoOfDPP[k];
		pipe->ScalerEnabled = plane->composition.scaler_info.enabled;
		pipe->VRatio = plane->composition.scaler_info.plane0.v_ratio;
		pipe->VRatioChroma = plane->composition.scaler_info.plane1.v_ratio;
		pipe->VTaps = plane->composition.scaler_info.plane0.v_taps;
		pipe->VTapsChroma = plane->composition.scaler_info.plane1.v_taps;
		pipe->RotationAngle = plane->composition.rotation_angle;
		pipe->mirrored = plane->composition.mirrored;
		pipe->BlockWidth256BytesY = mode_lib->ms.Read256BlockWidthY[k];
		pipe->BlockHeight256BytesY = mode_lib->ms.Read256BlockHeightY[k];
		pipe->BlockWidth256BytesC = mode_lib->ms.Read256BlockWidthC[k];
		pipe->BlockHeight256BytesC = mode_lib->ms.Read256BlockHeightC[k];
		pipe->InterlaceEnable = stream->timing.interlaced;
		pipe->NumberOfCursors = plane->cursor.num_cursors;
		pipe->VBlank = stream->timing.v_total - stream->timing.v_active;
		pipe->HTotal = stream->timing.h_total;
		pipe->HActive = stream->timing.h_active;
		pipe->DCCEnable = plane->surface.dcc.enable;
		pipe->ODMMode = mode_lib->ms.ODMMode[k];
		pipe->SourcePixelFormat = plane->pixel_format;
		pipe->BytePerPixelY = mode_lib->ms.BytePerPixelY[k];
		pipe->BytePerPixelC = mode_lib->ms.BytePerPixelC[k];
		pipe->ProgressiveToInterlaceUnitInOPP = mode_lib->ip.ptoi_supported;
		DML_LOG_VERBOSE("DML::%s: Calling CalculatePrefetchSchedule for k=%u\n", __func__, k);
		DML_LOG_VERBOSE("DML::%s: MaximumVStartup = %u\n", __func__, mode_lib->ms.MaximumVStartup[k]);
		p->display_cfg = display_cfg;
		p->HostVMInefficiencyFactor = mode_lib->ms.HostVMInefficiencyFactorPrefetch;
		p->myPipe = pipe;
		p->DSCDelay = mode_lib->ms.DSCDelay[k];
		p->DPPCLKDelaySubtotalPlusCNVCFormater = mode_lib->ip.dppclk_delay_subtotal + mode_lib->ip.dppclk_delay_cnvc_formatter;
		p->DPPCLKDelaySCL = mode_lib->ip.dppclk_delay_scl;
		p->DPPCLKDelaySCLLBOnly = mode_lib->ip.dppclk_delay_scl_lb_only;
		p->DPPCLKDelayCNVCCursor = mode_lib->ip.dppclk_delay_cnvc_cursor;
		p->DISPCLKDelaySubtotal = mode_lib->ip.dispclk_delay_subtotal;
		p->DPP_RECOUT_WIDTH = (unsigned int) (mode_lib->ms.SwathWidthY[k] / plane->composition.scaler_info.plane0.h_ratio);
		p->OutputFormat = stream->output.output_format;
		p->MaxInterDCNTileRepeaters = mode_lib->ip.max_inter_dcn_tile_repeaters;
		p->VStartup = mode_lib->ms.MaximumVStartup[k];
		p->HostVMMinPageSize = plane->overrides.hostvm_min_page_size_kbytes;
		p->DynamicMetadataEnable = plane->dynamic_meta_data.enable;
		p->DynamicMetadataVMEnabled = mode_lib->ip.dynamic_metadata_vm_enabled;
		p->DynamicMetadataLinesBeforeActiveRequired = plane->dynamic_meta_data.lines_before_active_required;
		p->DynamicMetadataTransmittedBytes = plane->dynamic_meta_data.transmitted_bytes;
		p->ExtraLatencyPrefetch = mode_lib->ms.ExtraLatencyPrefetch;
		p->TCalc = mode_lib->ms.TimeCalc;
		p->vm_bytes = mode_lib->ms.vm_bytes[k];
		p->PixelPTEBytesPerRow = mode_lib->ms.DPTEBytesPerRow[k];
		p->PrefetchSourceLinesY = mode_lib->ms.PrefetchLinesY[k];
		p->VInitPreFillY = mode_lib->ms.PrefillY[k];
		p->MaxNumSwathY = mode_lib->ms.MaxNumSwathY[k];
		p->PrefetchSourceLinesC = mode_lib->ms.PrefetchLinesC[k];
		p->VInitPreFillC = mode_lib->ms.PrefillC[k];
		p->MaxNumSwathC = mode_lib->ms.MaxNumSwathC[k];
		p->swath_width_luma_ub = mode_lib->ms.swath_width_luma_ub[k];
		p->swath_width_chroma_ub = mode_lib->ms.swath_width_chroma_ub[k];
		p->SwathHeightY = mode_lib->ms.SwathHeightY[k];
		p->SwathHeightC = mode_lib->ms.SwathHeightC[k];
		p->TWait = mode_lib->ms.TWait[k];
		p->Ttrip = mode_lib->ms.TripToMemory;
		p->Turg = mode_lib->ms.UrgLatency;
		p->setup_for_tdlut = plane->tdlut.setup_for_tdlut;
		p->tdlut_pte_bytes_per_frame = mode_lib->ms.tdlut_pte_bytes_per_frame[k];
		p->tdlut_bytes_per_frame = mode_lib->ms.tdlut_bytes_per_frame[k];
		p->tdlut_opt_time = mode_lib->ms.tdlut_opt_time[k];
		p->tdlut_drain_time = mode_lib->ms.tdlut_drain_time[k];
		p->num_cursors = (plane->cursor.cursor_width > 0);
		p->cursor_bytes_per_chunk = mode_lib->ms.cursor_bytes_per_chunk[k];
		p->cursor_bytes_per_line = mode_lib->ms.cursor_bytes_per_line[k];
		p->dcc_enable = plane->surface.dcc.enable;
		p->mrq_present = mode_lib->ip.dcn_mrq_present;
		p->meta_row_bytes = mode_lib->ms.meta_row_bytes[k];
		// output
		p->DSTXAfterScaler = &mode_lib->ms.DSTXAfterScaler[k];
		p->DSTYAfterScaler = &mode_lib->ms.DSTYAfterScaler[k];
		p->dst_y_prefetch = &mode_lib->ms.dst_y_prefetch[k];
		p->dst_y_per_vm_vblank = &mode_lib->ms.LinesForVM[k];
		p->dst_y_per_row_vblank = &mode_lib->ms.LinesForDPTERow[k];
		p->VRatioPrefetchY = &mode_lib->ms.VRatioPreY[k];
		p->VRatioPrefetchC = &mode_lib->ms.VRatioPreC[k];
		p->RequiredPrefetchPixelDataBWLuma = &mode_lib->ms.RequiredPrefetchPixelDataBWLuma[k]; // prefetch_sw_bw_l
		p->RequiredPrefetchPixelDataBWChroma = &mode_lib->ms.RequiredPrefetchPixelDataBWChroma[k]; // prefetch_sw_bw_c
		p->NotEnoughTimeForDynamicMetadata = &mode_lib->ms.NoTimeForDynamicMetadata[k];
		p->Tno_bw = &mode_lib->ms.Tno_bw[k];
		p->Tno_bw_flip = &mode_lib->ms.Tno_bw_flip[k];
		p->prefetch_vmrow_bw = &mode_lib->ms.prefetch_vmrow_bw[k];
		p->Tdmdl_vm = &s->dummy_single[0];
		p->Tdmdl = &s->dummy_single[1];
		p->TSetup = &s->dummy_single[2];
		p->Tvm_trips = &Tvm_trip;
		p->Tr0_trips = &Tr0_trip;
		p->Tvm_trips_flip = &mode_lib->ms.Tvm_trips_flip[k];
		p->Tr0_trips_flip = &mode_lib->ms.Tr0_trips_flip[k];
		p->Tvm_trips_flip_rounded = &mode_lib->ms.Tvm_trips_flip_rounded[k];
		p->Tr0_trips_flip_rounded = &mode_lib->ms.Tr0_trips_flip_rounded[k];
		p->VUpdateOffsetPix = &s->dummy_integer[0];
		p->VUpdateWidthPix = &s->dummy_integer[1];
		p->VReadyOffsetPix = &s->dummy_integer[2];
		p->prefetch_cursor_bw = &mode_lib->ms.prefetch_cursor_bw[k];
		p->prefetch_sw_bytes = &prefetch_sw_bytes;
		p->Tpre_rounded = &Tpre_rounded;
		p->Tpre_oto = &Tpre_oto;

		mode_lib->ms.NoTimeForPrefetch[k] = dcn5_calculate_prefetch_schedule(&mode_lib->scratch, p);
		DML_LOG_VERBOSE("DML::%s: k=%d, dst_y_per_vm_vblank = %f\n", __func__, k, *p->dst_y_per_vm_vblank);
		DML_LOG_VERBOSE("DML::%s: k=%d, dst_y_per_row_vblank = %f\n", __func__, k, *p->dst_y_per_row_vblank);
	}
}

static bool dcn5_ms_check_prefetch_support(struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	bool support = true;
	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		if (mode_lib->ms.dst_y_prefetch[k] < 2.0
				|| mode_lib->ms.LinesForVM[k] >= 32.0
				|| mode_lib->ms.LinesForDPTERow[k] >= 16.0
				|| mode_lib->ms.NoTimeForPrefetch[k] == true
				|| mode_lib->ms.DSTYAfterScaler[k] > 8) {
			support = false;
			DML_LOG_VERBOSE("DML::%s: k=%d, dst_y_prefetch=%f (should not be < 2)\n", __func__, k, mode_lib->ms.dst_y_prefetch[k]);
			DML_LOG_VERBOSE("DML::%s: k=%d, LinesForVM=%f (should not be >= 32)\n", __func__, k, mode_lib->ms.LinesForVM[k]);
			DML_LOG_VERBOSE("DML::%s: k=%d, LinesForDPTERow=%f (should not be >= 16)\n", __func__, k, mode_lib->ms.LinesForDPTERow[k]);
			DML_LOG_VERBOSE("DML::%s: k=%d, DSTYAfterScaler=%d (should be <= 8)\n", __func__, k, mode_lib->ms.DSTYAfterScaler[k]);
			DML_LOG_VERBOSE("DML::%s: k=%d, NoTimeForPrefetch=%d\n", __func__, k, mode_lib->ms.NoTimeForPrefetch[k]);
		}
	}
	return support;
}

static bool dcn5_ms_check_dynamic_metadata_support(struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	bool support;

	support = true;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.NoTimeForDynamicMetadata[k] == true) {
			support = false;
		}
	}
	return support;
}

static bool dcn5_ms_check_v_ratio_in_prefetch_support(
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	bool support = true;
	unsigned int k;

	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (mode_lib->ms.VRatioPreY[k] > __DML2_CALCS_MAX_VRATIO_PRE__
				|| mode_lib->ms.VRatioPreC[k] > __DML2_CALCS_MAX_VRATIO_PRE__) {
			support = false;
			DML_LOG_VERBOSE("DML::%s: k=%d VRatioPreY = %f (should be <= %f)\n", __func__, k, mode_lib->ms.VRatioPreY[k], __DML2_CALCS_MAX_VRATIO_PRE__);
			DML_LOG_VERBOSE("DML::%s: k=%d VRatioPreC = %f (should be <= %f)\n", __func__, k, mode_lib->ms.VRatioPreC[k], __DML2_CALCS_MAX_VRATIO_PRE__);
			DML_LOG_VERBOSE("DML::%s: VRatioInPrefetchSupported = %u\n", __func__, mode_lib->ms.support.VRatioInPrefetchSupported);
		}
	}
	return support;
}

static void dcn5_ms_calculate_urgent_burst_factor_for_prefetch(
		const struct dml2_display_cfg *display_cfg, struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	double line_time_us;
	const struct dml2_stream_parameters *stream;

	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		stream = &display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index];
		line_time_us = stream->timing.h_total / ((double) stream->timing.pixel_clock_khz / 1000);
		DML_LOG_VERBOSE("DML::%s: k=%d, Calling CalculateUrgentBurstFactor (for prefetch)\n", __func__, k);
		DML_LOG_VERBOSE("DML::%s: k=%d, VRatioPreY=%f\n", __func__, k, mode_lib->ms.VRatioPreY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, VRatioPreC=%f\n", __func__, k, mode_lib->ms.VRatioPreC[k]);
		dcn5_calculate_urgent_burst_factor(
				&display_cfg->plane_descriptors[k],
				mode_lib->ms.swath_width_luma_ub[k],
				mode_lib->ms.swath_width_chroma_ub[k],
				mode_lib->ms.SwathHeightY[k],
				mode_lib->ms.SwathHeightC[k],
				line_time_us,
				mode_lib->ms.UrgLatency,
				mode_lib->ms.VRatioPreY[k],
				mode_lib->ms.VRatioPreC[k],
				mode_lib->ms.BytePerPixelInDETY[k],
				mode_lib->ms.BytePerPixelInDETC[k],
				mode_lib->ms.DETBufferSizeY[k],
				mode_lib->ms.DETBufferSizeC[k],
				/* Output */
				&mode_lib->ms.UrgentBurstFactorLumaPre[k],
				&mode_lib->ms.UrgentBurstFactorChromaPre[k],
				&mode_lib->ms.NotEnoughUrgentLatencyHidingPre[k]);
	}
}

static void dcn5_ms_calculate_peak_bandwidth_required(const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	struct dml2_core_calcs_calculate_peak_bandwidth_required_params *p =
			&mode_lib->scratch.calculate_peak_bandwidth_params;

	// Calculate urgent bandwidth required, both urg and non urg peak bandwidth
	// assume flip bw is 0 at this point
	for (k = 0; k < mode_lib->ms.num_active_planes; k++)
		mode_lib->ms.final_flip_bw[k] = 0;
	p->urg_vactive_bandwidth_required = mode_lib->ms.support.urg_vactive_bandwidth_required;
	p->urg_bandwidth_required = mode_lib->ms.support.urg_bandwidth_required;
	p->urg_bandwidth_required_qual = mode_lib->ms.support.urg_bandwidth_required_qual;
	p->non_urg_bandwidth_required = mode_lib->ms.support.non_urg_bandwidth_required;
	p->surface_avg_vactive_required_bw = mode_lib->ms.surface_avg_vactive_required_bw;
	p->surface_peak_required_bw = mode_lib->ms.surface_peak_required_bw;
	p->display_cfg = display_cfg;
	p->inc_flip_bw = 0;
	p->num_active_planes = mode_lib->ms.num_active_planes;
	p->num_of_dpp = mode_lib->ms.NoOfDPP;
	p->dcc_dram_bw_nom_overhead_factor_p0 = mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p0;
	p->dcc_dram_bw_nom_overhead_factor_p1 = mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p1;
	p->dcc_dram_bw_pref_overhead_factor_p0 = mode_lib->ms.dcc_dram_bw_pref_overhead_factor_p0;
	p->dcc_dram_bw_pref_overhead_factor_p1 = mode_lib->ms.dcc_dram_bw_pref_overhead_factor_p1;
	p->surface_read_bandwidth_l = mode_lib->ms.vactive_sw_bw_l;
	p->surface_read_bandwidth_c = mode_lib->ms.vactive_sw_bw_c;
	p->prefetch_bandwidth_l = mode_lib->ms.RequiredPrefetchPixelDataBWLuma;
	p->prefetch_bandwidth_c = mode_lib->ms.RequiredPrefetchPixelDataBWChroma;
	p->excess_vactive_fill_bw_l = mode_lib->ms.excess_vactive_fill_bw_l;
	p->excess_vactive_fill_bw_c = mode_lib->ms.excess_vactive_fill_bw_c;
	p->cursor_bw = mode_lib->ms.cursor_bw;
	p->dpte_row_bw = mode_lib->ms.dpte_row_bw;
	p->meta_row_bw = mode_lib->ms.meta_row_bw;
	p->prefetch_cursor_bw = mode_lib->ms.prefetch_cursor_bw;
	p->prefetch_vmrow_bw = mode_lib->ms.prefetch_vmrow_bw;
	p->flip_bw = mode_lib->ms.final_flip_bw;
	p->urgent_burst_factor_l = mode_lib->ms.UrgentBurstFactorLuma;
	p->urgent_burst_factor_c = mode_lib->ms.UrgentBurstFactorChroma;
	p->urgent_burst_factor_cursor = mode_lib->ms.UrgentBurstFactorCursor;
	p->urgent_burst_factor_prefetch_l = mode_lib->ms.UrgentBurstFactorLumaPre;
	p->urgent_burst_factor_prefetch_c = mode_lib->ms.UrgentBurstFactorChromaPre;
	p->urgent_burst_factor_prefetch_cursor = mode_lib->ms.UrgentBurstFactorCursorPre;
	dcn5_calculate_peak_bandwidth_required(&mode_lib->scratch, p);
}

static bool dcn5_ms_check_final_prefetch_support(struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	bool support;
	support = true;

	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		if (mode_lib->ms.NotEnoughUrgentLatencyHidingPre[k]) {
			support = false;
			DML_LOG_VERBOSE("DML::%s: k=%d, NotEnoughUrgentLatencyHidingPre=%d\n", __func__, k, mode_lib->ms.NotEnoughUrgentLatencyHidingPre[k]);
		}
	}
	return support;
}

static void dcn5_ms_calculate_flip_schedule(const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;
	unsigned int per_pipe_flip_bytes;

	mode_lib->ms.TotImmediateFlipBytes = 0;
	for (k = 0; k < mode_lib->ms.num_active_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];

		per_pipe_flip_bytes = 0;
		if (plane->immediate_flip)
			per_pipe_flip_bytes = dcn5_get_pipe_flip_bytes(mode_lib->ms.HostVMInefficiencyFactor,
				mode_lib->ms.vm_bytes[k], mode_lib->ms.DPTEBytesPerRow[k],
				mode_lib->ms.meta_row_bytes[k]);

		mode_lib->ms.TotImmediateFlipBytes += per_pipe_flip_bytes * mode_lib->ms.NoOfDPP[k];

		dcn5_calculate_flip_schedule(
				&mode_lib->scratch,
				display_cfg->plane_descriptors[k].immediate_flip,
				1, // use_lb_flip_bw
				mode_lib->ms.HostVMInefficiencyFactor,
				mode_lib->ms.Tvm_trips_flip[k],
				mode_lib->ms.Tr0_trips_flip[k],
				mode_lib->ms.Tvm_trips_flip_rounded[k],
				mode_lib->ms.Tr0_trips_flip_rounded[k],
				display_cfg->gpuvm_enable,
				mode_lib->ms.vm_bytes[k],
				mode_lib->ms.DPTEBytesPerRow[k],
				0.0,
				mode_lib->ms.TotImmediateFlipBytes,
				plane->pixel_format,
				(stream->timing.h_total / ((double) stream->timing.pixel_clock_khz / 1000)),
				plane->composition.scaler_info.plane0.v_ratio,
				plane->composition.scaler_info.plane1.v_ratio,
				mode_lib->ms.Tno_bw_flip[k],
				mode_lib->ms.dpte_row_height[k],
				mode_lib->ms.dpte_row_height_chroma[k],
				mode_lib->ms.use_one_row_for_frame_flip[k],
				mode_lib->ip.max_flip_time_us,
				mode_lib->ip.max_flip_time_lines,
				per_pipe_flip_bytes,
				mode_lib->ms.meta_row_bytes[k],
				mode_lib->ms.meta_row_height_luma[k],
				mode_lib->ms.meta_row_height_chroma[k],
				mode_lib->ip.dcn_mrq_present && plane->surface.dcc.enable,
				/* Output */
				&mode_lib->ms.dst_y_per_vm_flip[k],
				&mode_lib->ms.dst_y_per_row_flip[k],
				&mode_lib->ms.final_flip_bw[k],
				&mode_lib->ms.ImmediateFlipSupportedForPipe[k]);
	}
}

static void dcn5_ms_calculate_bandwidth_required_for_flip(const struct dml2_display_cfg *display_cfg,
		struct dml2_core_calcs_mode_support_locals *s,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	struct dml2_core_calcs_calculate_peak_bandwidth_required_params *p =
			&mode_lib->scratch.calculate_peak_bandwidth_params;

	p->urg_vactive_bandwidth_required = s->dummy_bw;
	p->urg_bandwidth_required = mode_lib->ms.support.urg_bandwidth_required_flip;
	p->urg_bandwidth_required_qual = s->dummy_bw;
	p->non_urg_bandwidth_required = mode_lib->ms.support.non_urg_bandwidth_required_flip;
	p->surface_avg_vactive_required_bw = s->surface_dummy_bw;
	p->surface_peak_required_bw = mode_lib->ms.surface_peak_required_bw;
	p->display_cfg = display_cfg;
	p->inc_flip_bw = 1;
	p->num_active_planes = mode_lib->ms.num_active_planes;
	p->num_of_dpp = mode_lib->ms.NoOfDPP;
	p->dcc_dram_bw_nom_overhead_factor_p0 = mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p0;
	p->dcc_dram_bw_nom_overhead_factor_p1 = mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p1;
	p->dcc_dram_bw_pref_overhead_factor_p0 = mode_lib->ms.dcc_dram_bw_pref_overhead_factor_p0;
	p->dcc_dram_bw_pref_overhead_factor_p1 = mode_lib->ms.dcc_dram_bw_pref_overhead_factor_p1;
	p->surface_read_bandwidth_l = mode_lib->ms.vactive_sw_bw_l;
	p->surface_read_bandwidth_c = mode_lib->ms.vactive_sw_bw_c;
	p->prefetch_bandwidth_l = mode_lib->ms.RequiredPrefetchPixelDataBWLuma;
	p->prefetch_bandwidth_c = mode_lib->ms.RequiredPrefetchPixelDataBWChroma;
	p->excess_vactive_fill_bw_l = mode_lib->ms.excess_vactive_fill_bw_l;
	p->excess_vactive_fill_bw_c = mode_lib->ms.excess_vactive_fill_bw_c;
	p->cursor_bw = mode_lib->ms.cursor_bw;
	p->dpte_row_bw = mode_lib->ms.dpte_row_bw;
	p->meta_row_bw = mode_lib->ms.meta_row_bw;
	p->prefetch_cursor_bw = mode_lib->ms.prefetch_cursor_bw;
	p->prefetch_vmrow_bw = mode_lib->ms.prefetch_vmrow_bw;
	p->flip_bw = mode_lib->ms.final_flip_bw;
	p->urgent_burst_factor_l = mode_lib->ms.UrgentBurstFactorLuma;
	p->urgent_burst_factor_c = mode_lib->ms.UrgentBurstFactorChroma;
	p->urgent_burst_factor_cursor = mode_lib->ms.UrgentBurstFactorCursor;
	p->urgent_burst_factor_prefetch_l = mode_lib->ms.UrgentBurstFactorLumaPre;
	p->urgent_burst_factor_prefetch_c = mode_lib->ms.UrgentBurstFactorChromaPre;
	p->urgent_burst_factor_prefetch_cursor = mode_lib->ms.UrgentBurstFactorCursorPre;
	dcn5_calculate_peak_bandwidth_required(&mode_lib->scratch, p);
}

static bool dcn5_ms_check_reordering_support(struct dml2_core_internal_display_mode_lib *mode_lib)
{
	bool support = true;

	//Re-ordering Buffer Support Check
	if (((mode_lib->ip.rob_buffer_size_kbytes - mode_lib->ip.pixel_chunk_size_kbytes) * 1024
			/ mode_lib->ms.support.non_urg_bandwidth_required_flip[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp]) >= mode_lib->ms.support.max_urgent_latency_us) {
		support = true;
	} else {
		support = false;
	}

	DML_LOG_VERBOSE("DML::%s: max_urgent_latency_us = %f\n", __func__, mode_lib->ms.support.max_urgent_latency_us);
	DML_LOG_VERBOSE("DML::%s: ROBSupport = %u\n", __func__, support);
	return support;
}

static void dcn5_ms_calculate_vactive_det_fill_latency(const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	/* VActive fill time calculations (informative) */
	dcn5_calculate_vactive_det_fill_latency(
			display_cfg,
			mode_lib->ms.num_active_planes,
			mode_lib->ms.pstate_bytes_required_l[dml2_pstate_type_uclk],
			mode_lib->ms.pstate_bytes_required_c[dml2_pstate_type_uclk],
			mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p0,
			mode_lib->ms.dcc_dram_bw_nom_overhead_factor_p1,
			mode_lib->ms.vactive_sw_bw_l,
			mode_lib->ms.vactive_sw_bw_c,
			**mode_lib->ms.surface_avg_vactive_required_bw,
			**mode_lib->ms.surface_peak_required_bw,
			/* outputs */
			mode_lib->ms.pstate_vactive_det_fill_delay_us[dml2_pstate_type_uclk]);
}

static bool dcn5_ms_check_mode_support(const struct dml2_display_cfg *display_cfg,
		struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;
	bool immediateFlipRequired = false;

	for (k = 0; k < mode_lib->ms.num_active_planes; k++)
		immediateFlipRequired = immediateFlipRequired
				|| display_cfg->plane_descriptors[k].immediate_flip;

	/*Mode Support, Voltage State and SOC Configuration*/
	if (mode_lib->ms.support.ScaleRatioAndTapsSupport == false)
		goto fail;
	if (mode_lib->ms.support.SourceFormatPixelAndScanSupport == false)
		goto fail;
	if (mode_lib->ms.support.ViewportSizeSupport == false)
		goto fail;
	if (mode_lib->ms.support.LinkRateDoesNotMatchDPVersion)
		goto fail;
	if (mode_lib->ms.support.LinkRateForMultistreamNotIndicated)
		goto fail;
	if (mode_lib->ms.support.BPPForMultistreamNotIndicated)
		goto fail;
	if (mode_lib->ms.support.MultistreamWithHDMIOreDP)
		goto fail;
	if (mode_lib->ms.support.ExceededMultistreamSlots)
		goto fail;
	if (mode_lib->ms.support.MSOOrODMSplitWithNonDPLink)
		goto fail;
	if (mode_lib->ms.support.NotEnoughLanesForMSO)
		goto fail;
	if (mode_lib->ms.support.P2IWith420)
		goto fail;
	if (mode_lib->ms.support.DSC422NativeNotSupported)
		goto fail;
	if (mode_lib->ms.support.DSCSlicesODMModeSupported == false)
		goto fail;
	if (mode_lib->ms.support.NotEnoughDSCUnits)
		goto fail;
	if (mode_lib->ms.support.NotEnoughDSCSlices)
		goto fail;
	if (mode_lib->ms.support.DSCCLKRequiredMoreThanSupported)
		goto fail;
	if (mode_lib->ms.support.PixelsPerLinePerDSCUnitSupport == false)
		goto fail;
	if (mode_lib->ms.support.DTBCLKRequiredMoreThanSupported)
		goto fail;
	if (mode_lib->ms.support.ROBSupport == false)
		goto fail;
	if (mode_lib->ms.support.OutstandingRequestsSupport == false)
		goto fail;
	if (mode_lib->ms.support.OutstandingRequestsUrgencyAvoidance == false)
		goto fail;
	if (mode_lib->ms.support.DISPCLK_DPPCLK_Support == false)
		goto fail;
	if (mode_lib->ms.support.TotalAvailablePipesSupport == false)
		goto fail;
	if (mode_lib->ms.support.NumberOfOTGSupport == false)
		goto fail;
	if (mode_lib->ms.support.NumberOfHDMIFRLSupport == false)
		goto fail;
	if (mode_lib->ms.support.NumberOfDP2p0Support == false)
		goto fail;
	if (mode_lib->ms.support.EnoughWritebackUnits == false)
		goto fail;
	if (mode_lib->ms.support.WritebackLatencySupport == false)
		goto fail;
	if (mode_lib->ms.support.WritebackScaleRatioAndTapsSupport == false)
		goto fail;
	if (mode_lib->ms.support.CursorSupport == false)
		goto fail;
	if (mode_lib->ms.support.PitchSupport == false)
		goto fail;
	if (mode_lib->ms.support.ViewportExceedsSurface)
		goto fail;
	if (mode_lib->ms.support.PrefetchSupported == false)
		goto fail;
	if (mode_lib->ms.support.EnoughUrgentLatencyHidingSupport == false)
		goto fail;
	if (mode_lib->ms.support.AvgBandwidthSupport == false)
		goto fail;
	if (mode_lib->ms.support.DynamicMetadataSupported == false)
		goto fail;
	if (mode_lib->ms.support.VRatioInPrefetchSupported == false)
		goto fail;
	if (mode_lib->ms.support.PTEBufferSizeNotExceeded == false)
		goto fail;
	if (mode_lib->ms.support.DCCMetaBufferSizeNotExceeded == false)
		goto fail;
	if (mode_lib->ms.support.global_temp_read_or_ppt_supported == false)
		goto fail;
	if (mode_lib->ms.support.global_dram_clock_change_supported == false)
		if (mode_lib->ms.support.global_dram_clock_change_support_required)
			goto fail;
	if (mode_lib->ms.support.ImmediateFlipSupport == false) {
		if (immediateFlipRequired)
			goto fail;
		if (display_cfg->hostvm_enable)
			goto fail;
	}
	DML_LOG_VERBOSE("DML::%s: mode is supported\n", __func__);
	return true;
fail:
	dml2_core_utils_print_mode_support_info(&mode_lib->ms.support, true);
	DML_LOG_VERBOSE("DML::%s: mode is NOT supported\n", __func__);
	return false;
}

static void dcn5_ms_populate_informative(struct dml2_core_internal_display_mode_lib *mode_lib)
{
	unsigned int k;

	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		mode_lib->ms.support.MPCCombineEnable[k] = mode_lib->ms.MPCCombine[k];
		mode_lib->ms.support.DPPPerSurface[k] = mode_lib->ms.NoOfDPP[k];
	}
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		mode_lib->ms.support.ODMMode[k] = mode_lib->ms.ODMMode[k];
		mode_lib->ms.support.DSCEnabled[k] = mode_lib->ms.RequiresDSC[k];
		mode_lib->ms.support.FECEnabled[k] = mode_lib->ms.RequiresFEC[k];
		mode_lib->ms.support.OutputBpp[k] = mode_lib->ms.OutputBpp[k];
		mode_lib->ms.support.OutputType[k] = mode_lib->ms.OutputType[k];
		mode_lib->ms.support.OutputRate[k] = mode_lib->ms.OutputRate[k];
		DML_LOG_VERBOSE("DML::%s: k=%d, ODMMode = %u\n", __func__, k, mode_lib->ms.support.ODMMode[k]);
		DML_LOG_VERBOSE("DML::%s: k=%d, DSCEnabled = %u\n", __func__, k, mode_lib->ms.support.DSCEnabled[k]);
	}
}

static void dcn5_ms_get_plane_support_info(const struct dml2_display_cfg *display_cfg,
		const struct dml2_core_internal_display_mode_lib *mode_lib,
		struct core_plane_support_info *out,
		int plane_idx)
{
	const struct dml2_stream_parameters *stream =
			&display_cfg->stream_descriptors[display_cfg->plane_descriptors[plane_idx].stream_index];

	out->nominal_vblank_pstate_latency_hiding_us = (int)(
			stream->timing.h_total
			/ ((double)stream->timing.pixel_clock_khz / 1000)
			* mode_lib->ms.TWait[plane_idx]);

	DML_LOG_VERBOSE("DML::%s: plane_idx=%d, VActiveLatencyHidingMargin = %f\n", __func__, plane_idx, mode_lib->ms.VActiveLatencyHidingMargin[plane_idx]);
	out->dram_change_latency_hiding_margin_in_active = (int)mode_lib->ms.VActiveLatencyHidingMargin[plane_idx];

	out->active_latency_hiding_us = (int)mode_lib->ms.VActiveLatencyHidingUs[plane_idx];

	out->vactive_det_fill_delay_us[dml2_pstate_type_uclk] = (unsigned int)math_ceil(
		mode_lib->ms.pstate_vactive_det_fill_delay_us[dml2_pstate_type_uclk][plane_idx]);
}

static void dcn5_ms_get_stream_support_info(const struct dml2_display_cfg *display_cfg,
		const struct dml2_core_internal_display_mode_lib *mode_lib,
		struct core_stream_support_info *out,
		int plane_index)
{
	(void)mode_lib;
	const struct dml2_plane_parameters *plane = &display_cfg->plane_descriptors[plane_index];

	out->vblank_reserved_time_us = plane->overrides.reserved_vblank_time_ns / 1000;
	DML_LOG_VERBOSE("DML::%s: vblank_reserved_time_us = %u\n", __func__, out->vblank_reserved_time_us);
}

static void dcn5_ms_setup_mode_lib_constants(struct dml2_core_calcs_mode_support_ex *in_out_params)
{
	struct dml2_core_internal_display_mode_lib *mode_lib = in_out_params->mode_lib;
	const struct dml2_display_cfg *display_cfg = in_out_params->in_display_cfg;
	const struct dml2_utm_soc_bb *utm_soc_bb = in_out_params->utm_soc_bb;
	unsigned int k;

	/*
	 * This function should be refactored away so that funcs/calcs that rely on these mode_lib constants take them
	 * as input, alternatively, these constants should be defined as their own structure and stored inside mode lib
	 * so it becomes obvious that these are constants that must/will always be valid when calling calcs
	 */
	memset(&mode_lib->scratch, 0,
			sizeof(struct dml2_core_internal_scratch));
	memset(&mode_lib->ms, 0,
			sizeof(struct dml2_core_internal_mode_support));
	mode_lib->ms.use_legacy_dsc_delay_formula = mode_lib->ip.use_legacy_dsc_delay_formula;
	mode_lib->ms.num_active_planes = display_cfg->num_planes;
	mode_lib->ms.max_dispclk_freq_mhz = (double) utm_soc_bb->max_dispclk_khz / 1000;
	mode_lib->ms.max_dscclk_freq_mhz = (double) utm_soc_bb->max_dscclk_khz / 1000;
	mode_lib->ms.max_dppclk_freq_mhz = (double) utm_soc_bb->max_dppclk_khz / 1000;

	DML_LOG_VERBOSE("DML::%s: --- START --- \n", __func__);
	DML_LOG_VERBOSE("DML::%s: num_active_planes = %u\n", __func__,
			mode_lib->ms.num_active_planes);
	DML_LOG_VERBOSE("DML::%s: max_dispclk_freq_mhz = %f\n", __func__,
			mode_lib->ms.max_dispclk_freq_mhz);
	DML_LOG_VERBOSE("DML::%s: max_dscclk_freq_mhz = %f\n", __func__,
			mode_lib->ms.max_dscclk_freq_mhz);
	DML_LOG_VERBOSE("DML::%s: max_dppclk_freq_mhz = %f\n", __func__,
			mode_lib->ms.max_dppclk_freq_mhz);
	DML_LOG_VERBOSE("DML::%s: ip.compressed_buffer_segment_size_in_kbytes = %u\n",
			__func__,
			mode_lib->ip.compressed_buffer_segment_size_in_kbytes);
	DML_LOG_VERBOSE("DML::%s: ip.dcn_mrq_present = %u\n", __func__,
			mode_lib->ip.dcn_mrq_present);
	for (k = 0; k < mode_lib->ms.num_active_planes; k++)
		DML_LOG_VERBOSE("DML::%s: plane_%d: reserved_vblank_time_ns = %lu\n", __func__, k,
				display_cfg->plane_descriptors[k].overrides.reserved_vblank_time_ns);
}

static void dcn5_ms_populate_mode_support_result(
		struct dml2_core_instance *core,
		struct dml2_core_calcs_mode_support_ex *mode_support_ex_params,
		struct dml2_core_mode_support_result *result)
{
	unsigned int i, stream_index, stream_bitmask;
	int unsigned odm_count, num_odm_output_segments, dpp_count;

	*mode_support_ex_params->out_evaluation_info = mode_support_ex_params->mode_lib->ms.support;

	result->global.dispclk_khz = (unsigned int)(core->clean_me_up.mode_lib.ms.RequiredDISPCLK * 1000);
	result->global.dcfclk_deepsleep_khz = (unsigned int)(core->clean_me_up.mode_lib.ms.dcfclk_deepsleep * 1000);

	result->global.fclk_pstate_supported = mode_support_ex_params->out_evaluation_info->global_fclk_change_supported;
	result->global.uclk_pstate_supported = mode_support_ex_params->out_evaluation_info->global_dram_clock_change_supported;

	result->global.active.average_bw_sdp_kbps = 0;
	result->global.active.urgent_bw_dram_kbps = 0;

	result->global.active.average_bw_sdp_kbps = (unsigned long)math_ceil2((mode_support_ex_params->out_evaluation_info->avg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp] * 1000), 1.0);
	result->global.active.urgent_bw_sdp_kbps = (unsigned long)math_ceil2((mode_support_ex_params->out_evaluation_info->urg_bandwidth_required_flip[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp] * 1000), 1.0);

	result->global.active.average_bw_dram_kbps = (unsigned long)math_ceil2((mode_support_ex_params->out_evaluation_info->avg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram] * 1000), 1.0);
	result->global.active.urgent_bw_dram_kbps = (unsigned long)math_ceil2((mode_support_ex_params->out_evaluation_info->urg_bandwidth_required_flip[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram] * 1000), 1.0);
	DML_LOG_VERBOSE("DML::%s: result->global.active.urgent_bw_sdp_kbps = %ld\n", __func__, result->global.active.urgent_bw_sdp_kbps);
	DML_LOG_VERBOSE("DML::%s: result->global.active.urgent_bw_dram_kbps = %ld\n", __func__, result->global.active.urgent_bw_dram_kbps);

	for (i = 0; i < mode_support_ex_params->in_display_cfg->num_planes; i++) {
		result->per_plane[i].dppclk_khz = (unsigned int)(core->clean_me_up.mode_lib.ms.RequiredDPPCLK[i] * 1000);
	}

	stream_bitmask = 0;
	for (i = 0; i < mode_support_ex_params->in_display_cfg->num_planes; i++) {
		odm_count = 1;
		dpp_count = mode_support_ex_params->out_evaluation_info->DPPPerSurface[i];
		num_odm_output_segments = 1;

		switch (mode_support_ex_params->out_evaluation_info->ODMMode[i]) {
		case dml2_odm_mode_bypass:
			odm_count = 1;
			dpp_count = mode_support_ex_params->out_evaluation_info->DPPPerSurface[i];
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
			dpp_count = mode_support_ex_params->out_evaluation_info->DPPPerSurface[i];
			break;
		}

		result->cfg_support_info.plane_support_info[i].dpps_used = dpp_count;

		dcn5_ms_get_plane_support_info(mode_support_ex_params->in_display_cfg, &core->clean_me_up.mode_lib, &result->cfg_support_info.plane_support_info[i], i);

		stream_index = mode_support_ex_params->in_display_cfg->plane_descriptors[i].stream_index;

		result->per_stream[stream_index].dscclk_khz = (unsigned int)core->clean_me_up.mode_lib.ms.required_dscclk_freq_mhz[i] * 1000;
		DML_LOG_VERBOSE("CORE_DCN4::%s: i=%d stream_index=%d, result->per_stream[stream_index].dscclk_khz = %u\n", __func__, i, stream_index, result->per_stream[stream_index].dscclk_khz);

		if (!((stream_bitmask >> stream_index) & 0x1)) {
			result->cfg_support_info.stream_support_info[stream_index].odms_used = odm_count;
			result->cfg_support_info.stream_support_info[stream_index].num_odm_output_segments = num_odm_output_segments;
			result->cfg_support_info.stream_support_info[stream_index].dsc_enable = mode_support_ex_params->out_evaluation_info->DSCEnabled[i];
			result->cfg_support_info.stream_support_info[stream_index].num_dsc_slices = mode_support_ex_params->out_evaluation_info->NumberOfDSCSlices[i];
			dcn5_ms_get_stream_support_info(mode_support_ex_params->in_display_cfg, &core->clean_me_up.mode_lib, &result->cfg_support_info.stream_support_info[stream_index], i);
			result->per_stream[stream_index].dtbclk_khz = (unsigned int)(core->clean_me_up.mode_lib.ms.RequiredDTBCLK[i] * 1000);
			stream_bitmask |= 0x1 << stream_index;
		}
	}
	result->bandwidth_upper_bound.dcn5.urgent_bandwidth_kbps = (unsigned long)(mode_support_ex_params->mode_lib->ms.support.urg_bandwidth_required_flip[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp] * 1000);
	result->bandwidth_upper_bound.dcn5.non_urgent_bandwidth_kbps = (unsigned long)(mode_support_ex_params->mode_lib->ms.support.non_urg_bandwidth_required_flip[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp] * 1000);
}

static void dcn5_ms_calculate_watermarks(const struct dml2_display_cfg *display_cfg,
		struct dml2_core_calcs_mode_support_locals *s,
		struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	struct dml2_core_calcs_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params *p =
			&mode_lib->scratch.CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params;

	s->mSOCParameters.UrgentLatency = mode_lib->ms.UrgLatency;
	s->mSOCParameters.ExtraLatency = mode_lib->ms.ExtraLatency;
	s->mSOCParameters.ExtraLatency_sr = mode_lib->ms.ExtraLatency_sr;
	s->mSOCParameters.WritebackLatency = utm_soc_bb->writeback_base_latency_us;
	s->mSOCParameters.DRAMClockChangeLatency =
			utm_soc_bb->power_management_parameters.dram_clk_change_blackout_us;
	s->mSOCParameters.FCLKChangeLatency = utm_soc_bb->power_management_parameters.fclk_change_blackout_us;
	s->mSOCParameters.SRExitTime = utm_soc_bb->power_management_parameters.stutter_exit_latency_us;
	s->mSOCParameters.SREnterPlusExitTime =
			utm_soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us;
	s->mSOCParameters.SRExitZ8Time = utm_soc_bb->power_management_parameters.z8_stutter_exit_latency_us;
	s->mSOCParameters.SREnterPlusExitZ8Time =
			utm_soc_bb->power_management_parameters.z8_stutter_enter_plus_exit_latency_us;
	s->mSOCParameters.USRRetrainingLatency = 0;
	s->mSOCParameters.SMNLatency = 0;
	s->mSOCParameters.temp_read_or_ppt_blackout_us = utm_soc_bb->power_management_parameters.g7_ppt_blackout_us;
	s->mSOCParameters.max_urgent_latency_us = mode_lib->ms.support.max_urgent_latency_us;
	s->mSOCParameters.df_response_time_us = mode_lib->ms.support.df_response_time_us;
	s->mSOCParameters.qos_type = dml2_qos_param_type_dcn4x;

	p->display_cfg = display_cfg;
	p->USRRetrainingRequired = false;
	p->NumberOfActiveSurfaces = mode_lib->ms.num_active_planes;
	p->MaxLineBufferLines = mode_lib->ip.max_line_buffer_lines;
	p->LineBufferSize = mode_lib->ip.line_buffer_size_bits;
	p->WritebackInterfaceBufferSize = mode_lib->ip.writeback_interface_buffer_size_kbytes;
	p->DCFCLK = mode_lib->ms.DCFCLK;
	p->SynchronizeTimings = display_cfg->overrides.synchronize_timings;
	p->SynchronizeDRRDisplaysForUCLKPStateChange =
			display_cfg->overrides.synchronize_ddr_displays_for_uclk_pstate_change;
	p->dpte_group_bytes = mode_lib->ms.dpte_group_bytes;
	p->mmSOCParameters = s->mSOCParameters;
	p->WritebackChunkSize = mode_lib->ip.writeback_chunk_size_kbytes;
	p->SOCCLK = utm_soc_bb->qos_model_dchub_v1.socclks_khz[0] / 1000.0;
	p->DCFClkDeepSleep = mode_lib->ms.dcfclk_deepsleep;
	p->DETBufferSizeY = mode_lib->ms.DETBufferSizeY;
	p->DETBufferSizeC = mode_lib->ms.DETBufferSizeC;
	p->SwathHeightY = mode_lib->ms.SwathHeightY;
	p->SwathHeightC = mode_lib->ms.SwathHeightC;
	//CalculateWatermarks_params->LBBitPerPixel = 57; // FIXME_STAGE2, need a new ip param?
	p->SwathWidthY = mode_lib->ms.SwathWidthY;
	p->SwathWidthC = mode_lib->ms.SwathWidthC;
	p->DPPPerSurface = mode_lib->ms.NoOfDPP;
	p->BytePerPixelDETY = mode_lib->ms.BytePerPixelInDETY;
	p->BytePerPixelDETC = mode_lib->ms.BytePerPixelInDETC;
	p->DSTXAfterScaler = mode_lib->ms.DSTXAfterScaler;
	p->DSTYAfterScaler = mode_lib->ms.DSTYAfterScaler;
	p->UnboundedRequestEnabled = mode_lib->ms.UnboundedRequestEnabled;
	p->CompressedBufferSizeInkByte = mode_lib->ms.CompressedBufferSizeInkByte;
	p->meta_row_height_l = mode_lib->ms.meta_row_height_luma;
	p->meta_row_height_c = mode_lib->ms.meta_row_height_chroma;
	p->uclk_pstate_switch_modes = mode_lib->ms.uclk_pstate_switch_modes;
	// Output
	p->Watermark = &mode_lib->ms.support.watermarks; // Watermarks *Watermark
	p->DRAMClockChangeSupport = mode_lib->ms.support.DRAMClockChangeSupport;
	p->global_dram_clock_change_support_required = &mode_lib->ms.support.global_dram_clock_change_support_required;
	p->global_dram_clock_change_supported = &mode_lib->ms.support.global_dram_clock_change_supported;
	p->MaxActiveDRAMClockChangeLatencySupported = &s->dummy_single_array[0]; // double *MaxActiveDRAMClockChangeLatencySupported[]
	p->FCLKChangeSupport = mode_lib->ms.support.FCLKChangeSupport;
	p->global_fclk_change_supported = &mode_lib->ms.support.global_fclk_change_supported;
	p->MaxActiveFCLKChangeLatencySupported = &s->dummy_single[0]; // double *MaxActiveFCLKChangeLatencySupported
	p->USRRetrainingSupport = &mode_lib->ms.support.USRRetrainingSupport;
	p->temp_read_or_ppt_support = mode_lib->ms.support.temp_read_or_ppt_support;
	p->global_temp_read_or_ppt_supported = &mode_lib->ms.support.global_temp_read_or_ppt_supported;
	p->VActiveLatencyHidingMargin = mode_lib->ms.VActiveLatencyHidingMargin;
	p->VActiveLatencyHidingUs = mode_lib->ms.VActiveLatencyHidingUs;
	dcn5_calculate_watermarks_and_dram_speed_change_support(&mode_lib->scratch, p);
}

static bool dcn5_ms_validate_prefetch(struct dml2_core_calcs_mode_support_ex *in_out_params)
{
	struct dml2_core_internal_display_mode_lib *mode_lib = in_out_params->mode_lib;
	const struct dml2_display_cfg *display_cfg = in_out_params->in_display_cfg;
	struct dml2_core_calcs_mode_support_locals *s = &mode_lib->scratch.dml_core_mode_support_locals;
	const struct dml2_utm_soc_bb *utm_soc_bb = in_out_params->utm_soc_bb;

	// dcn5_ms_calculate_avg_bandwidth_and_dcfclk_lb_required(display_cfg, mode_lib);

	/* FIXME - break it down according the function name */
	dcn5_ms_calculate_det_buffer_time_value_urgent_burst_factor_and_urgent_latency_hiding(display_cfg, mode_lib);

	mode_lib->ms.support.EnoughUrgentLatencyHidingSupport = dcn5_ms_check_urgent_latency_hiding_support(mode_lib);

	dcn5_ms_calculate_min_dcfclk_deepsleep_clock(display_cfg, mode_lib, utm_soc_bb);

	dcn5_ms_calculate_writeback_delay(display_cfg, mode_lib, utm_soc_bb);

	dcn5_ms_calculate_max_vstartup(display_cfg, mode_lib);

	dcn5_ms_calculate_mcache_setting(display_cfg, mode_lib, utm_soc_bb);

	dcn5_ms_calculate_avg_bandwidth_and_dcfclk_lb_required(display_cfg, mode_lib, utm_soc_bb);

	dcn5_ms_check_average_latency_supports(mode_lib, utm_soc_bb);

	mode_lib->ms.support.AvgBandwidthSupport = true;
	/* Prefetch Check */
	{
		dcn5_ms_calculate_t_calc(mode_lib);

		dcn5_ms_calculate_hostvm_inefficiency_factor(display_cfg, mode_lib, utm_soc_bb);

		dcn5_ms_calculate_3dlut_settings(display_cfg, mode_lib);

		dcn5_ms_calculate_urgent_latency(display_cfg, mode_lib, utm_soc_bb);
		{
			dcn5_ms_calculate_prefetch_schedule(display_cfg, mode_lib, s, utm_soc_bb);

			mode_lib->ms.support.PrefetchSupported = dcn5_ms_check_prefetch_support(mode_lib);

			mode_lib->ms.support.DynamicMetadataSupported = dcn5_ms_check_dynamic_metadata_support(mode_lib);

			mode_lib->ms.support.VRatioInPrefetchSupported = dcn5_ms_check_v_ratio_in_prefetch_support(mode_lib);

			// Only do urg vs prefetch bandwidth check, flip schedule check, power saving feature support check IF the Prefetch Schedule Check is ok
			if (mode_lib->ms.support.PrefetchSupported) {
				dcn5_ms_calculate_urgent_burst_factor_for_prefetch(display_cfg, mode_lib);

				dcn5_ms_calculate_peak_bandwidth_required(display_cfg, mode_lib);

				mode_lib->ms.support.PrefetchSupported = dcn5_ms_check_final_prefetch_support(mode_lib);

				// Both prefetch schedule and BW okay
				if (mode_lib->ms.support.PrefetchSupported == true
						&& mode_lib->ms.support.VRatioInPrefetchSupported == true) {

					dcn5_ms_calculate_flip_schedule(display_cfg, mode_lib);

					dcn5_ms_calculate_bandwidth_required_for_flip(display_cfg, s, mode_lib);

					mode_lib->ms.support.ImmediateFlipSupport = true;
				} else { // if prefetch not support, assume iflip is not supported too
					mode_lib->ms.support.ImmediateFlipSupport = false;
				}
			} // prefetch schedule
		}
		dcn5_ms_calculate_watermarks(display_cfg, s, mode_lib, utm_soc_bb);

		DML_LOG_VERBOSE("DML::%s: Done prefetch calculation\n", __func__);
	} // End of Prefetch Check

	mode_lib->ms.support.ROBSupport = dcn5_ms_check_reordering_support(mode_lib);

	dcn5_ms_calculate_vactive_det_fill_latency(display_cfg, mode_lib);

	/* Immediate Flip parameters */
	mode_lib->ms.support.ModeSupport = dcn5_ms_check_mode_support(display_cfg, mode_lib);

	dcn5_ms_populate_informative(mode_lib);

	return mode_lib->ms.support.ModeSupport;
}

static bool dcn5_mode_support(struct dml2_core_calcs_mode_support_ex *in_out_params)
{
	struct dml2_core_internal_display_mode_lib *mode_lib = in_out_params->mode_lib;
	const struct dml2_display_cfg *display_cfg = in_out_params->in_display_cfg;
	struct dml2_core_calcs_mode_support_locals *s = &mode_lib->scratch.dml_core_mode_support_locals;
	const struct dml2_utm_soc_bb *utm_soc_bb = in_out_params->utm_soc_bb;
	unsigned int k;

	dcn5_ms_check_input_sanity(display_cfg, mode_lib);

	mode_lib->ms.support.ScaleRatioAndTapsSupport = dcn5_ms_check_scaler_support(display_cfg, mode_lib);

	mode_lib->ms.support.SourceFormatPixelAndScanSupport = dcn5_ms_check_source_format_and_scan_direction(
			display_cfg, mode_lib);

	dcn5_ms_calculate_byte_per_pixel_and_block_sizes(display_cfg, mode_lib);

	dcn5_ms_calculate_read_bandwidth(display_cfg, mode_lib);

	dcn5_ms_calculate_writeback_bandwidth(display_cfg, mode_lib);

	mode_lib->ms.support.WritebackLatencySupport = dcn5_ms_check_writeback_bandwidth_latency_support(display_cfg, mode_lib, utm_soc_bb);

	mode_lib->ms.support.WritebackScaleRatioAndTapsSupport = dcn5_ms_check_writeback_scale_ratio_and_taps_support(display_cfg, mode_lib);

	dcn5_ms_calculate_single_pipe_dppclk_and_pscl_factor(display_cfg, mode_lib);

	dcn5_ms_calculate_max_swath_widths(display_cfg, mode_lib);

	mode_lib->ms.support.CursorSupport = dcn5_ms_check_cursor_support(display_cfg, mode_lib);

	mode_lib->ms.support.PitchSupport = dcn5_ms_check_surface_alginment_requirements(display_cfg, mode_lib);

	mode_lib->ms.support.TotalAvailablePipesSupport = true;

	dcn5_ms_calculate_effective_pixel_clock(display_cfg, mode_lib);

	dml2_core_utils_get_stream_output_bpp(mode_lib->ms.DesiredOutputBpp, display_cfg);

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		dcn5_ms_calculate_dsc_slices_per_plane(k,  display_cfg, mode_lib);

		dcn5_ms_calculate_odm_mode_per_plane(k, display_cfg, mode_lib);

		dcn5_ms_calculate_output_link(k, display_cfg, mode_lib, utm_soc_bb);

		dcn5_ms_calculate_final_odm_mode_per_plane(k, mode_lib);

		dcn5_ms_calculate_final_dsc_slices_per_plane(k, display_cfg, mode_lib);
	}

	dcn5_ms_calculate_max_det_and_min_compressed_buffer_size(display_cfg, mode_lib);

	dcn5_ms_calculate_swath_and_det_configuration_for_single_dpp(display_cfg, mode_lib, s);

	dcn5_ms_calculate_num_of_dpp_required(display_cfg, mode_lib);

	/* FIXME: TotalAvailablePipesSupport is also modified in dcn5_ms_calculate_final_dsc_slices_per_plane */
	mode_lib->ms.support.TotalAvailablePipesSupport &= dcn5_ms_check_total_available_pipes_support(mode_lib);

	dcn5_ms_calculate_total_num_of_single_dpp_surfaces(mode_lib);

	dcn5_ms_calculate_dispclk_and_dppclk_required(display_cfg, mode_lib);

	mode_lib->ms.support.DISPCLK_DPPCLK_Support = dcn5_ms_check_dispclk_and_dppclk_support(mode_lib);

	mode_lib->ms.support.NumberOfOTGSupport = dcn5_ms_check_otg_count_support(display_cfg, mode_lib);

	mode_lib->ms.support.NumberOfHDMIFRLSupport = dcn5_ms_check_hpo_frl_encoder_count_support(display_cfg, mode_lib);

	mode_lib->ms.support.NumberOfDP2p0Support = dcn5_ms_check_hpo_dp_encoder_count_support(display_cfg, mode_lib);

	mode_lib->ms.support.EnoughWritebackUnits = dcn5_ms_check_writeback_count_support(display_cfg, mode_lib);

	mode_lib->ms.support.LinkCapacitySupport = dcn5_ms_check_link_bandwidth_support(display_cfg, mode_lib);

	dcn5_ms_check_misc_link_supports(display_cfg, mode_lib);

	dcn5_ms_calculate_dtbclk_required(display_cfg, mode_lib);

	mode_lib->ms.support.DTBCLKRequiredMoreThanSupported = !dcn5_ms_check_dtbclk_support(mode_lib, utm_soc_bb);

	dcn5_ms_calculate_dscclk_required(display_cfg, mode_lib);

	mode_lib->ms.support.DSCCLKRequiredMoreThanSupported = !dcn5_ms_check_dscclk_support(mode_lib);

	dcn5_ms_check_dsc_engine_supports(display_cfg, mode_lib);

	dcn5_ms_calculate_dsc_delay(display_cfg, mode_lib);

	dcn5_ms_calculate_swath_and_det_configuration(mode_lib, s);

	dcn5_ms_calculate_total_num_of_dcc_active_dpp(display_cfg, mode_lib);

	dcn5_ms_calculate_vm_row_and_swath_and_calculate_dcc_meta_cache_requirements(display_cfg, mode_lib, s);

	mode_lib->ms.support.PTEBufferSizeNotExceeded = dcn5_ms_check_pte_buffer_size_support(mode_lib);

	mode_lib->ms.support.DCCMetaBufferSizeNotExceeded = dcn5_ms_check_dcc_meta_cache_support(mode_lib);

	dcn5_ms_calculate_vactive_uclk_pstate_requirements(display_cfg, mode_lib, utm_soc_bb);

	dcn5_ms_validate_prefetch(in_out_params);

	DML_LOG_VERBOSE("DML::%s: done\n", __func__);
	return mode_lib->ms.support.ModeSupport;
}

static void dcn5_ms_assign_sop_constraint(struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_sop_constraint *sop_constraint)
{
	mode_lib->ms.UrgLatency = sop_constraint->dcn5.latency.dcn5.urgent_ramp;
	mode_lib->ms.TripToMemory = math_max2(sop_constraint->dcn5.latency.dcn5.t_trip,
		sop_constraint->dcn5.latency.dcn5.urgent_ramp);
	mode_lib->ms.support.avg_urgent_latency_us = sop_constraint->dcn5.latency.dcn5.avg_req_latency_urg;
	mode_lib->ms.support.avg_non_urgent_latency_us = sop_constraint->dcn5.latency.dcn5.avg_req_latency_non_urg;
	mode_lib->ms.support.max_urgent_latency_us = sop_constraint->dcn5.latency.dcn5.max_req_latency_urg;
	mode_lib->ms.support.max_non_urgent_latency_us = sop_constraint->dcn5.latency.dcn5.max_req_latency_non_urg;
	mode_lib->ms.support.df_response_time_us = sop_constraint->dcn5.latency.dcn5.df_response_time_us;

	DML_LOG_VERBOSE("DML::%s: urgent_ramp=%f\n", __func__, sop_constraint->dcn5.latency.dcn5.urgent_ramp);
	DML_LOG_VERBOSE("DML::%s: t_trip=%f\n", __func__, sop_constraint->dcn5.latency.dcn5.t_trip);
	DML_LOG_VERBOSE("DML::%s: avg_req_latency_urg=%f\n", __func__,
			sop_constraint->dcn5.latency.dcn5.avg_req_latency_urg);
	DML_LOG_VERBOSE("DML::%s: avg_req_latency_non_urg=%f\n", __func__,
			sop_constraint->dcn5.latency.dcn5.avg_req_latency_non_urg);
	DML_LOG_VERBOSE("DML::%s: max_req_latency_urg=%f\n", __func__,
			sop_constraint->dcn5.latency.dcn5.max_req_latency_urg);
	DML_LOG_VERBOSE("DML::%s: max_req_latency_non_urg=%f\n", __func__,
			sop_constraint->dcn5.latency.dcn5.max_req_latency_non_urg);
}

static void dcn5_ms_setup_pstate_options(struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_display_solution *solution)
{
	memcpy(mode_lib->ms.uclk_pstate_switch_modes,
			solution->uclk_pstate_params.pstate_switch_modes,
			sizeof(solution->uclk_pstate_params.pstate_switch_modes));
}

enum dml2_status dml2_core_dcn5_funcs_validate_solution(struct dml2_core_instance *core,
		const struct dml2_display_solution *solution,
		struct dml2_validation_result *result)
{
	enum dml2_status status = DML2_STATUS_OK;
	struct dml2_core_mode_support_locals *l = &core->scratch.mode_support_locals;
	const struct dml2_sop_table *sop_table = &core->utm_soc_bb->sop_table;
	unsigned int i;

	if (solution->unvalidated_change.bits.mpc_combine_overrides
			|| solution->unvalidated_change.bits.odm_combine_overrides
			|| solution->unvalidated_change.bits.reserved_vblank_time
			|| solution->unvalidated_change.bits.uclk_pstate_method)
		result->is_mode_support_valid = false;
	if (solution->unvalidated_change.bits.sop_index)
		result->is_prefetch_valid = false;

	if (!result->is_mode_support_valid) {
		l->mode_support_ex_params.mode_lib = &core->clean_me_up.mode_lib;
		l->mode_support_ex_params.in_display_cfg = &solution->dispcfg;
		l->mode_support_ex_params.out_evaluation_info = &result->mode_support.cfg_support_info.clean_me_up.support_info;
		l->mode_support_ex_params.utm_soc_bb = core->utm_soc_bb;
		dcn5_ms_setup_mode_lib_constants(&l->mode_support_ex_params);
		dcn5_ms_assign_sop_constraint(l->mode_support_ex_params.mode_lib, &solution->sop_constraint);
		dcn5_ms_setup_pstate_options(l->mode_support_ex_params.mode_lib, solution);
		result->is_mode_support_valid = dcn5_mode_support(&l->mode_support_ex_params);
		result->mode_support.cfg_support_info.is_supported = result->is_mode_support_valid;
		if (result->is_mode_support_valid) {
			dcn5_ms_populate_mode_support_result(core, &l->mode_support_ex_params, &result->mode_support);
			result->is_prefetch_valid = sop_table->is_bw_supported_at_index(sop_table,
					&result->mode_support.bandwidth_upper_bound, solution->sop_constraint.dcn5.min_sop_index);
		} else {
			result->is_prefetch_valid = false;
		}
	} else if (!result->is_prefetch_valid) {
		dcn5_ms_assign_sop_constraint(l->mode_support_ex_params.mode_lib, &solution->sop_constraint);
		if (dcn5_ms_validate_prefetch(&l->mode_support_ex_params)) {
			dcn5_ms_populate_mode_support_result(core, &l->mode_support_ex_params, &result->mode_support);
			result->is_prefetch_valid = sop_table->is_bw_supported_at_index(sop_table,
					&result->mode_support.bandwidth_upper_bound, solution->sop_constraint.dcn5.min_sop_index);
		}
	}

	if (!result->is_mcache_allocation_valid) {
		result->is_mcache_allocation_valid = true;
		for (i = 0; i < solution->dispcfg.num_planes; i++) {
			if (!solution->dispcfg.plane_descriptors[i].surface.dcc.enable) {
				memset(&result->mcache_allocations[i], 0, sizeof(struct dml2_mcache_surface_allocation));
				continue;
			}

			l->calc_mcache_allocation_params.instance = core;
			l->calc_mcache_allocation_params.plane_descriptor = &solution->dispcfg.plane_descriptors[i];
			l->calc_mcache_allocation_params.mcache_allocation = &result->mcache_allocations[i];
			l->calc_mcache_allocation_params.plane_index = i;
			if (!core->calculate_mcache_allocation(&l->calc_mcache_allocation_params)) {
				result->is_mcache_allocation_valid = false;
				break;
			}
		}
	}

	DML_LOG_VERBOSE("DML::%s: result->is_mode_support_valid = %d\n", __func__, result->is_mode_support_valid);
	DML_LOG_VERBOSE("DML::%s: result->is_prefetch_valid = %d\n", __func__, result->is_prefetch_valid);
	DML_LOG_VERBOSE("DML::%s: result->is_mcache_allocation_valid = %d\n", __func__, result->is_mcache_allocation_valid);
	DML_LOG_VERBOSE("DML::%s: done\n", __func__);

	status = !result->is_mode_support_valid ? DML2_STATUS_VALIDATE_FAIL_MODE_SUPPORT :
			!result->is_prefetch_valid ? DML2_STATUS_VALIDATE_FAIL_PREFETCH :
			!result->is_mcache_allocation_valid ? DML2_STATUS_VALIDATE_FAIL_MCACHE : DML2_STATUS_OK;

	return status;
}
