// SPDX-License-Identifier: MIT
//
// Copyright 2024-2025 Advanced Micro Devices, Inc.

#include "dml2_core_dcn5_funcs_mode_programming.h"
#include "dml2_core_dcn5_calcs_dchub.h"
#include "dml2_core_dcn5_calcs_display_pipe.h"
#include "dml2_core_utils.h"
#include "dml2_debug.h"

static bool dcn5_mode_programming(struct dml2_core_calcs_mode_programming_ex *in_out_params)
{
	const struct dml2_display_cfg *display_cfg = in_out_params->in_display_cfg;
	const struct dml2_utm_soc_bb *utm_soc_bb = in_out_params->utm_soc_bb;
	const struct dml2_sop_table *sop_table = &in_out_params->utm_soc_bb->sop_table;
	const struct core_display_cfg_support_info *cfg_support_info = in_out_params->cfg_support_info;
	struct dml2_core_internal_display_mode_lib *mode_lib = in_out_params->mode_lib;
	const struct dml2_display_cfg_programming *programming = in_out_params->programming;
	struct dml2_core_calcs_mode_programming_locals *s = &mode_lib->scratch.dml_core_mode_programming_locals;
	struct dml2_core_calcs_CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params *CalculateWatermarks_params = &mode_lib->scratch.CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params;
	struct dml2_core_calcs_CalculateVMRowAndSwath_params *CalculateVMRowAndSwath_params = &mode_lib->scratch.CalculateVMRowAndSwath_params;
	struct dml2_core_calcs_CalculateSwathAndDETConfiguration_params *CalculateSwathAndDETConfiguration_params = &mode_lib->scratch.CalculateSwathAndDETConfiguration_params;
	struct dml2_core_calcs_CalculateStutterEfficiency_params *CalculateStutterEfficiency_params = &mode_lib->scratch.CalculateStutterEfficiency_params;
	struct dml2_core_calcs_CalculatePrefetchSchedule_params *CalculatePrefetchSchedule_params = &mode_lib->scratch.CalculatePrefetchSchedule_params;
	struct dml2_core_calcs_calculate_mcache_setting_params *calculate_mcache_setting_params = &mode_lib->scratch.calculate_mcache_setting_params;
	struct dml2_core_calcs_calculate_tdlut_setting_params *calculate_tdlut_setting_params = &mode_lib->scratch.calculate_tdlut_setting_params;
	struct dml2_core_shared_CalculateMetaAndPTETimes_params *CalculateMetaAndPTETimes_params = &mode_lib->scratch.CalculateMetaAndPTETimes_params;
	struct dml2_core_calcs_calculate_peak_bandwidth_required_params *calculate_peak_bandwidth_params = &mode_lib->scratch.calculate_peak_bandwidth_params;
	struct dml2_core_calcs_calculate_bytes_to_fetch_required_to_hide_latency_params *calculate_bytes_to_fetch_required_to_hide_latency_params = &mode_lib->scratch.calculate_bytes_to_fetch_required_to_hide_latency_params;

	unsigned int k, j;
	bool must_support_iflip;
	const long min_return_uclk_cycles = 83;
	const long min_return_fclk_cycles = 75;
	struct dml2_soc_operating_point max_sop;
	struct dml2_soc_operating_point min_sop;
	double min_return_latency_in_DCFCLK_cycles = 0;

	DML_LOG_VERBOSE("DML::%s: --- START --- \n", __func__);

	s->num_active_planes = display_cfg->num_planes;
	dml2_core_utils_get_stream_output_bpp(s->OutputBpp, display_cfg);

	mode_lib->mp.num_active_pipes = dml2_core_util_get_num_active_pipes(display_cfg->num_planes, cfg_support_info);
	dml2_core_utils_pipe_plane_mapping(cfg_support_info, mode_lib->mp.pipe_plane);

	mode_lib->mp.GlobalDPPCLK = programming->min_clocks.dcn4x.dpprefclk_khz / 1000.0;
	sop_table->get_max_sop(sop_table, &max_sop);
	sop_table->get_min_sop(sop_table, &min_sop);
	s->SOCCLK = min_sop.socclk_khz / 1000.0;

	for (k = 0; k < s->num_active_planes; ++k) {
		unsigned int stream_index = display_cfg->plane_descriptors[k].stream_index;
		DML_ASSERT(cfg_support_info->stream_support_info[stream_index].odms_used <= 4);
		DML_ASSERT(cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 4 ||
					cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 2 ||
					cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 1);

		if (cfg_support_info->stream_support_info[stream_index].odms_used > 1)
			DML_ASSERT(cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 1);

		switch (cfg_support_info->stream_support_info[stream_index].odms_used) {
		case (4):
			mode_lib->mp.ODMMode[k] = dml2_odm_mode_combine_4to1;
			break;
		case (3):
			mode_lib->mp.ODMMode[k] = dml2_odm_mode_combine_3to1;
			break;
		case (2):
			mode_lib->mp.ODMMode[k] = dml2_odm_mode_combine_2to1;
			break;
		default:
			if (cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 4)
				mode_lib->mp.ODMMode[k] = dml2_odm_mode_mso_1to4;
			else if (cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 2)
				mode_lib->mp.ODMMode[k] = dml2_odm_mode_mso_1to2;
			else
				mode_lib->mp.ODMMode[k] = dml2_odm_mode_bypass;
			break;
		}
	}

	for (k = 0; k < s->num_active_planes; ++k) {
		mode_lib->mp.NoOfDPP[k] = cfg_support_info->plane_support_info[k].dpps_used;
		mode_lib->mp.Dppclk[k] = programming->plane_programming[k].min_clocks.dcn4x.dppclk_khz / 1000.0;
		DML_ASSERT(mode_lib->mp.Dppclk[k] > 0);
	}

	for (k = 0; k < s->num_active_planes; ++k) {
		unsigned int stream_index = display_cfg->plane_descriptors[k].stream_index;
		mode_lib->mp.DSCCLK[k] = programming->stream_programming[stream_index].min_clocks.dcn4x.dscclk_khz / 1000.0;
		DML_LOG_VERBOSE("DML::%s: k=%d stream_index=%d, mode_lib->mp.DSCCLK = %f\n", __func__, k, stream_index, mode_lib->mp.DSCCLK[k]);
	}

	mode_lib->mp.Dispclk = programming->min_clocks.dcn4x.dispclk_khz / 1000.0;
	mode_lib->mp.DCFCLKDeepSleep = programming->min_clocks.dcn4x.deepsleep_dcfclk_khz / 1000.0;

	memcpy(mode_lib->mp.uclk_pstate_switch_modes,
			in_out_params->uclk_params->pstate_switch_modes,
			sizeof(in_out_params->uclk_params->pstate_switch_modes));

	DML_ASSERT(mode_lib->mp.Dcfclk > 0);
	DML_ASSERT(mode_lib->mp.FabricClock > 0);
	DML_ASSERT(mode_lib->mp.uclk_freq_mhz > 0);
	DML_ASSERT(mode_lib->mp.GlobalDPPCLK > 0);
	DML_ASSERT(mode_lib->mp.Dispclk > 0);
	DML_ASSERT(mode_lib->mp.DCFCLKDeepSleep > 0);
	DML_ASSERT(s->SOCCLK > 0);

	DML_LOG_VERBOSE("DML::%s: num_active_planes = %u\n", __func__, s->num_active_planes);
	DML_LOG_VERBOSE("DML::%s: num_active_pipes = %u\n", __func__, mode_lib->mp.num_active_pipes);
	DML_LOG_VERBOSE("DML::%s: Dcfclk = %f\n", __func__, mode_lib->mp.Dcfclk);
	DML_LOG_VERBOSE("DML::%s: FabricClock = %f\n", __func__, mode_lib->mp.FabricClock);
	DML_LOG_VERBOSE("DML::%s: uclk_freq_mhz = %f\n", __func__, mode_lib->mp.uclk_freq_mhz);
	DML_LOG_VERBOSE("DML::%s: Dispclk = %f\n", __func__, mode_lib->mp.Dispclk);
	for (k = 0; k < s->num_active_planes; ++k) {
		DML_LOG_VERBOSE("DML::%s: Dppclk[%0d] = %f\n", __func__, k, mode_lib->mp.Dppclk[k]);
	}
	DML_LOG_VERBOSE("DML::%s: GlobalDPPCLK = %f\n", __func__, mode_lib->mp.GlobalDPPCLK);
	DML_LOG_VERBOSE("DML::%s: DCFCLKDeepSleep = %f\n", __func__, mode_lib->mp.DCFCLKDeepSleep);
	DML_LOG_VERBOSE("DML::%s: SOCCLK = %f\n", __func__, s->SOCCLK);
	for (k = 0; k < mode_lib->mp.num_active_pipes; ++k) {
		DML_LOG_VERBOSE("DML::%s: pipe=%d is in plane=%d\n", __func__, k, mode_lib->mp.pipe_plane[k]);
		DML_LOG_VERBOSE("DML::%s: Per-plane DPPPerSurface[%0d] = %d\n", __func__, k, mode_lib->mp.NoOfDPP[k]);
	}

	for (k = 0; k < s->num_active_planes; k++)
		DML_LOG_VERBOSE("DML::%s: plane_%d: reserved_vblank_time_ns = %lu\n", __func__, k, display_cfg->plane_descriptors[k].overrides.reserved_vblank_time_ns);

	dcn5_calculate_max_det_and_min_compressed_buffer_size(
		mode_lib->ip.config_return_buffer_size_in_kbytes,
		mode_lib->ip.config_return_buffer_segment_size_in_kbytes,
		mode_lib->ip.rob_buffer_size_kbytes,
		mode_lib->ip.max_num_dpp,
		display_cfg->overrides.hw.force_nom_det_size_kbytes.enable,
		display_cfg->overrides.hw.force_nom_det_size_kbytes.value,
		mode_lib->ip.dcn_mrq_present,

		/* Output */
		&s->MaxTotalDETInKByte,
		&s->NomDETInKByte,
		&s->MinCompressedBufferSizeInKByte);


	dcn5_adjust_pixel_clock_for_progressive_to_interlace_unit(display_cfg, mode_lib->ip.ptoi_supported, s->PixelClockBackEnd);

	for (k = 0; k < s->num_active_planes; ++k) {
		dcn5_calculate_single_pipe_dppclk_and_scl_throughput(
			display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio,
			mode_lib->ip.max_dchub_pscl_bw_pix_per_clk,
			mode_lib->ip.max_pscl_lb_bw_pix_per_clk,
			((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000),
			display_cfg->plane_descriptors[k].pixel_format,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_taps,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_taps,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_taps,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_taps,

			/* Output */
			&mode_lib->mp.PSCL_THROUGHPUT[k],
			&mode_lib->mp.PSCL_THROUGHPUT_CHROMA[k],
			&mode_lib->mp.DPPCLKUsingSingleDPP[k]);
	}

	for (k = 0; k < s->num_active_planes; ++k) {
		dcn5_calculate_byte_per_pixel_and_block_sizes(
			display_cfg->plane_descriptors[k].pixel_format,
			display_cfg->plane_descriptors[k].surface.tiling,
			display_cfg->plane_descriptors[k].surface.plane0.pitch,
			display_cfg->plane_descriptors[k].surface.plane1.pitch,

			// Output
			&mode_lib->mp.BytePerPixelY[k],
			&mode_lib->mp.BytePerPixelC[k],
			&mode_lib->mp.BytePerPixelInDETY[k],
			&mode_lib->mp.BytePerPixelInDETC[k],
			&mode_lib->mp.Read256BlockHeightY[k],
			&mode_lib->mp.Read256BlockHeightC[k],
			&mode_lib->mp.Read256BlockWidthY[k],
			&mode_lib->mp.Read256BlockWidthC[k],
			&mode_lib->mp.MacroTileHeightY[k],
			&mode_lib->mp.MacroTileHeightC[k],
			&mode_lib->mp.MacroTileWidthY[k],
			&mode_lib->mp.MacroTileWidthC[k],
			&mode_lib->mp.surf_linear128_l[k],
			&mode_lib->mp.surf_linear128_c[k]);
	}

	dcn5_calculate_swath_width(
		display_cfg,
		false, // ForceSingleDPP
		s->num_active_planes,
		mode_lib->mp.ODMMode,
		mode_lib->mp.BytePerPixelY,
		mode_lib->mp.BytePerPixelC,
		mode_lib->mp.Read256BlockHeightY,
		mode_lib->mp.Read256BlockHeightC,
		mode_lib->mp.Read256BlockWidthY,
		mode_lib->mp.Read256BlockWidthC,
		mode_lib->mp.surf_linear128_l,
		mode_lib->mp.surf_linear128_c,
		mode_lib->mp.NoOfDPP,

		/* Output */
		mode_lib->mp.req_per_swath_ub_l,
		mode_lib->mp.req_per_swath_ub_c,
		mode_lib->mp.SwathWidthSingleDPPY,
		mode_lib->mp.SwathWidthSingleDPPC,
		mode_lib->mp.SwathWidthY,
		mode_lib->mp.SwathWidthC,
		s->dummy_integer_array[0], // unsigned int MaximumSwathHeightY[]
		s->dummy_integer_array[1], // unsigned int MaximumSwathHeightC[]
		mode_lib->mp.swath_width_luma_ub,
		mode_lib->mp.swath_width_chroma_ub,
		s->dummy_integer_array[2],
		s->dummy_integer_array[3]);

	for (k = 0; k < s->num_active_planes; ++k) {
		mode_lib->mp.cursor_bw[k] = display_cfg->plane_descriptors[k].cursor.num_cursors * display_cfg->plane_descriptors[k].cursor.cursor_width * display_cfg->plane_descriptors[k].cursor.cursor_bpp / 8.0 /
			((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000));
		mode_lib->mp.vactive_sw_bw_l[k] = mode_lib->mp.SwathWidthSingleDPPY[k] * mode_lib->mp.BytePerPixelY[k] / (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)) * display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		mode_lib->mp.vactive_sw_bw_c[k] = mode_lib->mp.SwathWidthSingleDPPC[k] * mode_lib->mp.BytePerPixelC[k] / (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)) * display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
		DML_LOG_VERBOSE("DML::%s: vactive_sw_bw_l[%i] = %fBps\n", __func__, k, mode_lib->mp.vactive_sw_bw_l[k]);
		DML_LOG_VERBOSE("DML::%s: vactive_sw_bw_c[%i] = %fBps\n", __func__, k, mode_lib->mp.vactive_sw_bw_c[k]);
	}

	CalculateSwathAndDETConfiguration_params->display_cfg = display_cfg;
	CalculateSwathAndDETConfiguration_params->ConfigReturnBufferSizeInKByte = mode_lib->ip.config_return_buffer_size_in_kbytes;
	CalculateSwathAndDETConfiguration_params->MaxTotalDETInKByte = s->MaxTotalDETInKByte;
	CalculateSwathAndDETConfiguration_params->MinCompressedBufferSizeInKByte = s->MinCompressedBufferSizeInKByte;
	CalculateSwathAndDETConfiguration_params->rob_buffer_size_kbytes = mode_lib->ip.rob_buffer_size_kbytes;
	CalculateSwathAndDETConfiguration_params->pixel_chunk_size_kbytes = mode_lib->ip.pixel_chunk_size_kbytes;
	CalculateSwathAndDETConfiguration_params->rob_buffer_size_kbytes = mode_lib->ip.rob_buffer_size_kbytes;
	CalculateSwathAndDETConfiguration_params->pixel_chunk_size_kbytes = mode_lib->ip.pixel_chunk_size_kbytes;
	CalculateSwathAndDETConfiguration_params->ForceSingleDPP = false;
	CalculateSwathAndDETConfiguration_params->NumberOfActiveSurfaces = s->num_active_planes;
	CalculateSwathAndDETConfiguration_params->nomDETInKByte = s->NomDETInKByte;
	CalculateSwathAndDETConfiguration_params->ConfigReturnBufferSegmentSizeInkByte = mode_lib->ip.config_return_buffer_segment_size_in_kbytes;
	CalculateSwathAndDETConfiguration_params->CompressedBufferSegmentSizeInkByte = mode_lib->ip.compressed_buffer_segment_size_in_kbytes;
	CalculateSwathAndDETConfiguration_params->ReadBandwidthLuma = mode_lib->mp.vactive_sw_bw_l;
	CalculateSwathAndDETConfiguration_params->ReadBandwidthChroma = mode_lib->mp.vactive_sw_bw_c;
	CalculateSwathAndDETConfiguration_params->MaximumSwathWidthLuma = s->dummy_single_array[0];
	CalculateSwathAndDETConfiguration_params->MaximumSwathWidthChroma = s->dummy_single_array[1];
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockHeightY = mode_lib->mp.Read256BlockHeightY;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockHeightC = mode_lib->mp.Read256BlockHeightC;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockWidthY = mode_lib->mp.Read256BlockWidthY;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockWidthC = mode_lib->mp.Read256BlockWidthC;
	CalculateSwathAndDETConfiguration_params->surf_linear128_l = mode_lib->mp.surf_linear128_l;
	CalculateSwathAndDETConfiguration_params->surf_linear128_c = mode_lib->mp.surf_linear128_c;
	CalculateSwathAndDETConfiguration_params->ODMMode = mode_lib->mp.ODMMode;
	CalculateSwathAndDETConfiguration_params->DPPPerSurface = mode_lib->mp.NoOfDPP;
	CalculateSwathAndDETConfiguration_params->BytePerPixY = mode_lib->mp.BytePerPixelY;
	CalculateSwathAndDETConfiguration_params->BytePerPixC = mode_lib->mp.BytePerPixelC;
	CalculateSwathAndDETConfiguration_params->BytePerPixDETY = mode_lib->mp.BytePerPixelInDETY;
	CalculateSwathAndDETConfiguration_params->BytePerPixDETC = mode_lib->mp.BytePerPixelInDETC;
	CalculateSwathAndDETConfiguration_params->mrq_present = mode_lib->ip.dcn_mrq_present;

	// output
	CalculateSwathAndDETConfiguration_params->req_per_swath_ub_l = mode_lib->mp.req_per_swath_ub_l;
	CalculateSwathAndDETConfiguration_params->req_per_swath_ub_c = mode_lib->mp.req_per_swath_ub_c;
	CalculateSwathAndDETConfiguration_params->swath_width_luma_ub = s->dummy_long_array[0];
	CalculateSwathAndDETConfiguration_params->swath_width_chroma_ub = s->dummy_long_array[1];
	CalculateSwathAndDETConfiguration_params->SwathWidth = s->dummy_long_array[2];
	CalculateSwathAndDETConfiguration_params->SwathWidthChroma = s->dummy_long_array[3];
	CalculateSwathAndDETConfiguration_params->SwathHeightY = mode_lib->mp.SwathHeightY;
	CalculateSwathAndDETConfiguration_params->SwathHeightC = mode_lib->mp.SwathHeightC;
	CalculateSwathAndDETConfiguration_params->request_size_bytes_luma = mode_lib->mp.request_size_bytes_luma;
	CalculateSwathAndDETConfiguration_params->request_size_bytes_chroma = mode_lib->mp.request_size_bytes_chroma;
	CalculateSwathAndDETConfiguration_params->DETBufferSizeInKByte = mode_lib->mp.DETBufferSizeInKByte;
	CalculateSwathAndDETConfiguration_params->DETBufferSizeY = mode_lib->mp.DETBufferSizeY;
	CalculateSwathAndDETConfiguration_params->DETBufferSizeC = mode_lib->mp.DETBufferSizeC;
	CalculateSwathAndDETConfiguration_params->full_swath_bytes_l = s->full_swath_bytes_l;
	CalculateSwathAndDETConfiguration_params->full_swath_bytes_c = s->full_swath_bytes_c;
	CalculateSwathAndDETConfiguration_params->full_swath_bytes_single_dpp_l = s->dummy_long_array[4];
	CalculateSwathAndDETConfiguration_params->full_swath_bytes_single_dpp_c = s->dummy_long_array[5];
	CalculateSwathAndDETConfiguration_params->UnboundedRequestEnabled = &mode_lib->mp.UnboundedRequestEnabled;
	CalculateSwathAndDETConfiguration_params->compbuf_reserved_space_64b = &mode_lib->mp.compbuf_reserved_space_64b;
	CalculateSwathAndDETConfiguration_params->hw_debug5 = &mode_lib->mp.hw_debug5;
	CalculateSwathAndDETConfiguration_params->CompressedBufferSizeInkByte = &mode_lib->mp.CompressedBufferSizeInkByte;
	CalculateSwathAndDETConfiguration_params->ViewportSizeSupportPerSurface = &s->dummy_boolean_array[0][0];
	CalculateSwathAndDETConfiguration_params->ViewportSizeSupport = &s->dummy_boolean[0];

	// Calculate DET size, swath height here.
	dcn5_calculate_swath_and_det_configuration(&mode_lib->scratch, CalculateSwathAndDETConfiguration_params);

	// DSC Delay
	mode_lib->mp.use_legacy_dsc_delay_formula = mode_lib->ip.use_legacy_dsc_delay_formula;
	for (k = 0; k < s->num_active_planes; ++k) {
		mode_lib->mp.DSCDelay[k] = dcn5_calculate_dsc_delay_requirement(cfg_support_info->stream_support_info[display_cfg->plane_descriptors[k].stream_index].dsc_enable,
			mode_lib->mp.ODMMode[k],
			mode_lib->ip.maximum_dsc_bits_per_component,
			s->OutputBpp[k],
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_active,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total,
			cfg_support_info->stream_support_info[display_cfg->plane_descriptors[k].stream_index].num_dsc_slices,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_format,
			display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_encoder,
			((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000),
			s->PixelClockBackEnd[k],
			mode_lib->mp.use_legacy_dsc_delay_formula);
	}

	for (k = 0; k < s->num_active_planes; ++k) {
		s->SurfaceParameters[k].PixelClock = ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
		s->SurfaceParameters[k].DPPPerSurface = mode_lib->mp.NoOfDPP[k];
		s->SurfaceParameters[k].RotationAngle = display_cfg->plane_descriptors[k].composition.rotation_angle;
		s->SurfaceParameters[k].ViewportHeight = display_cfg->plane_descriptors[k].composition.viewport.plane0.height;
		s->SurfaceParameters[k].ViewportHeightC = display_cfg->plane_descriptors[k].composition.viewport.plane1.height;
		s->SurfaceParameters[k].BlockWidth256BytesY = mode_lib->mp.Read256BlockWidthY[k];
		s->SurfaceParameters[k].BlockHeight256BytesY = mode_lib->mp.Read256BlockHeightY[k];
		s->SurfaceParameters[k].BlockWidth256BytesC = mode_lib->mp.Read256BlockWidthC[k];
		s->SurfaceParameters[k].BlockHeight256BytesC = mode_lib->mp.Read256BlockHeightC[k];
		s->SurfaceParameters[k].BlockWidthY = mode_lib->mp.MacroTileWidthY[k];
		s->SurfaceParameters[k].BlockHeightY = mode_lib->mp.MacroTileHeightY[k];
		s->SurfaceParameters[k].BlockWidthC = mode_lib->mp.MacroTileWidthC[k];
		s->SurfaceParameters[k].BlockHeightC = mode_lib->mp.MacroTileHeightC[k];
		s->SurfaceParameters[k].InterlaceEnable = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.interlaced;
		s->SurfaceParameters[k].HTotal = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total;
		s->SurfaceParameters[k].DCCEnable = display_cfg->plane_descriptors[k].surface.dcc.enable;
		s->SurfaceParameters[k].SourcePixelFormat = display_cfg->plane_descriptors[k].pixel_format;
		s->SurfaceParameters[k].SurfaceTiling = display_cfg->plane_descriptors[k].surface.tiling;
		s->SurfaceParameters[k].BytePerPixelY = mode_lib->mp.BytePerPixelY[k];
		s->SurfaceParameters[k].BytePerPixelC = mode_lib->mp.BytePerPixelC[k];
		s->SurfaceParameters[k].ProgressiveToInterlaceUnitInOPP = mode_lib->ip.ptoi_supported;
		s->SurfaceParameters[k].VRatio = display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
		s->SurfaceParameters[k].VRatioChroma = display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
		s->SurfaceParameters[k].VTaps = display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_taps;
		s->SurfaceParameters[k].VTapsChroma = display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_taps;
		s->SurfaceParameters[k].PitchY = display_cfg->plane_descriptors[k].surface.plane0.pitch;
		s->SurfaceParameters[k].PitchC = display_cfg->plane_descriptors[k].surface.plane1.pitch;
		s->SurfaceParameters[k].ViewportStationary = display_cfg->plane_descriptors[k].composition.viewport.stationary;
		s->SurfaceParameters[k].ViewportXStart = display_cfg->plane_descriptors[k].composition.viewport.plane0.x_start;
		s->SurfaceParameters[k].ViewportYStart = display_cfg->plane_descriptors[k].composition.viewport.plane0.y_start;
		s->SurfaceParameters[k].ViewportXStartC = display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start;
		s->SurfaceParameters[k].ViewportYStartC = display_cfg->plane_descriptors[k].composition.viewport.plane1.y_start;
		s->SurfaceParameters[k].FORCE_ONE_ROW_FOR_FRAME = display_cfg->plane_descriptors[k].overrides.hw.force_one_row_for_frame;
		s->SurfaceParameters[k].SwathHeightY = mode_lib->mp.SwathHeightY[k];
		s->SurfaceParameters[k].SwathHeightC = mode_lib->mp.SwathHeightC[k];
		s->SurfaceParameters[k].DCCMetaPitchY = display_cfg->plane_descriptors[k].surface.dcc.plane0.pitch;
		s->SurfaceParameters[k].DCCMetaPitchC = display_cfg->plane_descriptors[k].surface.dcc.plane1.pitch;
		s->SurfaceParameters[k].UPSPEnabled = display_cfg->plane_descriptors[k].composition.scaler_info.upsp_enabled;
	}

	CalculateVMRowAndSwath_params->display_cfg = display_cfg;
	CalculateVMRowAndSwath_params->uclk_pstate_switch_modes = mode_lib->ms.uclk_pstate_switch_modes;
	CalculateVMRowAndSwath_params->NumberOfActiveSurfaces = s->num_active_planes;
	CalculateVMRowAndSwath_params->myPipe = s->SurfaceParameters;
	CalculateVMRowAndSwath_params->PTEBufferSizeInRequestsLuma = mode_lib->ip.dpte_buffer_size_in_pte_reqs_luma;
	CalculateVMRowAndSwath_params->PTEBufferSizeInRequestsChroma = mode_lib->ip.dpte_buffer_size_in_pte_reqs_chroma;
	CalculateVMRowAndSwath_params->SwathWidthY = mode_lib->mp.SwathWidthY;
	CalculateVMRowAndSwath_params->SwathWidthC = mode_lib->mp.SwathWidthC;
	CalculateVMRowAndSwath_params->DCCMetaBufferSizeBytes = mode_lib->ip.dcc_meta_buffer_size_bytes;
	CalculateVMRowAndSwath_params->mrq_present = mode_lib->ip.dcn_mrq_present;

	// output
	CalculateVMRowAndSwath_params->PTEBufferSizeNotExceeded = s->dummy_boolean_array[0];
	CalculateVMRowAndSwath_params->dpte_row_width_luma_ub = mode_lib->mp.dpte_row_width_luma_ub;
	CalculateVMRowAndSwath_params->dpte_row_width_chroma_ub = mode_lib->mp.dpte_row_width_chroma_ub;
	CalculateVMRowAndSwath_params->dpte_row_height_luma = mode_lib->mp.dpte_row_height;
	CalculateVMRowAndSwath_params->dpte_row_height_chroma = mode_lib->mp.dpte_row_height_chroma;
	CalculateVMRowAndSwath_params->dpte_row_height_linear_luma = mode_lib->mp.dpte_row_height_linear;
	CalculateVMRowAndSwath_params->dpte_row_height_linear_chroma = mode_lib->mp.dpte_row_height_linear_chroma;
	CalculateVMRowAndSwath_params->vm_group_bytes = mode_lib->mp.vm_group_bytes;
	CalculateVMRowAndSwath_params->dpte_group_bytes = mode_lib->mp.dpte_group_bytes;
	CalculateVMRowAndSwath_params->PixelPTEReqWidthY = mode_lib->mp.PixelPTEReqWidthY;
	CalculateVMRowAndSwath_params->PixelPTEReqHeightY = mode_lib->mp.PixelPTEReqHeightY;
	CalculateVMRowAndSwath_params->PTERequestSizeY = mode_lib->mp.PTERequestSizeY;
	CalculateVMRowAndSwath_params->PixelPTEReqWidthC = mode_lib->mp.PixelPTEReqWidthC;
	CalculateVMRowAndSwath_params->PixelPTEReqHeightC = mode_lib->mp.PixelPTEReqHeightC;
	CalculateVMRowAndSwath_params->PTERequestSizeC = mode_lib->mp.PTERequestSizeC;
	CalculateVMRowAndSwath_params->vmpg_width_y = s->vmpg_width_y;
	CalculateVMRowAndSwath_params->vmpg_height_y = s->vmpg_height_y;
	CalculateVMRowAndSwath_params->vmpg_width_c = s->vmpg_width_c;
	CalculateVMRowAndSwath_params->vmpg_height_c = s->vmpg_height_c;
	CalculateVMRowAndSwath_params->dpde0_bytes_per_frame_ub_l = mode_lib->mp.dpde0_bytes_per_frame_ub_l;
	CalculateVMRowAndSwath_params->dpde0_bytes_per_frame_ub_c = mode_lib->mp.dpde0_bytes_per_frame_ub_c;
	CalculateVMRowAndSwath_params->PrefetchSourceLinesY = mode_lib->mp.PrefetchSourceLinesY;
	CalculateVMRowAndSwath_params->PrefetchSourceLinesC = mode_lib->mp.PrefetchSourceLinesC;
	CalculateVMRowAndSwath_params->VInitPreFillY = mode_lib->mp.VInitPreFillY;
	CalculateVMRowAndSwath_params->VInitPreFillC = mode_lib->mp.VInitPreFillC;
	CalculateVMRowAndSwath_params->MaxNumSwathY = mode_lib->mp.MaxNumSwathY;
	CalculateVMRowAndSwath_params->MaxNumSwathC = mode_lib->mp.MaxNumSwathC;
	CalculateVMRowAndSwath_params->dpte_row_bw = mode_lib->mp.dpte_row_bw;
	CalculateVMRowAndSwath_params->PixelPTEBytesPerRow = mode_lib->mp.PixelPTEBytesPerRow;
	CalculateVMRowAndSwath_params->dpte_row_bytes_per_row_l = s->dpte_row_bytes_per_row_l;
	CalculateVMRowAndSwath_params->dpte_row_bytes_per_row_c = s->dpte_row_bytes_per_row_c;
	CalculateVMRowAndSwath_params->vm_bytes = mode_lib->mp.vm_bytes;
	CalculateVMRowAndSwath_params->use_one_row_for_frame = mode_lib->mp.use_one_row_for_frame;
	CalculateVMRowAndSwath_params->use_one_row_for_frame_flip = mode_lib->mp.use_one_row_for_frame_flip;
	CalculateVMRowAndSwath_params->PTE_BUFFER_MODE = mode_lib->mp.PTE_BUFFER_MODE;
	CalculateVMRowAndSwath_params->BIGK_FRAGMENT_SIZE = mode_lib->mp.BIGK_FRAGMENT_SIZE;
	CalculateVMRowAndSwath_params->DCCMetaBufferSizeNotExceeded = s->dummy_boolean_array[1];
	CalculateVMRowAndSwath_params->meta_row_bw = mode_lib->mp.meta_row_bw;
	CalculateVMRowAndSwath_params->meta_row_bytes = mode_lib->mp.meta_row_bytes;
	CalculateVMRowAndSwath_params->meta_row_bytes_per_row_ub_l = s->meta_row_bytes_per_row_ub_l;
	CalculateVMRowAndSwath_params->meta_row_bytes_per_row_ub_c = s->meta_row_bytes_per_row_ub_c;
	CalculateVMRowAndSwath_params->meta_req_width_luma = mode_lib->mp.meta_req_width;
	CalculateVMRowAndSwath_params->meta_req_height_luma = mode_lib->mp.meta_req_height;
	CalculateVMRowAndSwath_params->meta_row_width_luma = mode_lib->mp.meta_row_width;
	CalculateVMRowAndSwath_params->meta_row_height_luma = mode_lib->mp.meta_row_height;
	CalculateVMRowAndSwath_params->meta_pte_bytes_per_frame_ub_l = mode_lib->mp.meta_pte_bytes_per_frame_ub_l;
	CalculateVMRowAndSwath_params->meta_req_width_chroma = mode_lib->mp.meta_req_width_chroma;
	CalculateVMRowAndSwath_params->meta_row_height_chroma = mode_lib->mp.meta_row_height_chroma;
	CalculateVMRowAndSwath_params->meta_row_width_chroma = mode_lib->mp.meta_row_width_chroma;
	CalculateVMRowAndSwath_params->meta_req_height_chroma = mode_lib->mp.meta_req_height_chroma;
	CalculateVMRowAndSwath_params->meta_pte_bytes_per_frame_ub_c = mode_lib->mp.meta_pte_bytes_per_frame_ub_c;

	dcn5_calculate_vm_row_and_swath(&mode_lib->scratch, CalculateVMRowAndSwath_params);

	memset(calculate_mcache_setting_params, 0, sizeof(struct dml2_core_calcs_calculate_mcache_setting_params));
	for (k = 0; k < s->num_active_planes; k++) {
		mode_lib->mp.dcc_dram_bw_nom_overhead_factor_p0[k] = 1.0;
		mode_lib->mp.dcc_dram_bw_pref_overhead_factor_p0[k] = 1.0;
		mode_lib->mp.dcc_dram_bw_nom_overhead_factor_p1[k] = 1.0;
		mode_lib->mp.dcc_dram_bw_pref_overhead_factor_p1[k] = 1.0;
	}

	dcn5_calculate_hostvm_inefficiency_factor(
		&s->HostVMInefficiencyFactor,
		&s->HostVMInefficiencyFactorPrefetch,

		display_cfg->gpuvm_enable,
		display_cfg->hostvm_enable,
		mode_lib->ip.remote_iommu_outstanding_translations,
		utm_soc_bb->max_outstanding_reqs,
		1.0,
		0.5);

	s->TotalDCCActiveDPP = 0;
	s->TotalActiveDPP = 0;
	for (k = 0; k < s->num_active_planes; ++k) {
		s->TotalActiveDPP = s->TotalActiveDPP + mode_lib->mp.NoOfDPP[k];
		if (display_cfg->plane_descriptors[k].surface.dcc.enable)
			s->TotalDCCActiveDPP = s->TotalDCCActiveDPP + mode_lib->mp.NoOfDPP[k];
	}
	// Calculate tdlut schedule related terms
	for (k = 0; k <= s->num_active_planes - 1; k++) {
		calculate_tdlut_setting_params->dispclk_mhz = mode_lib->mp.Dispclk;
		calculate_tdlut_setting_params->setup_for_tdlut = display_cfg->plane_descriptors[k].tdlut.setup_for_tdlut;
		calculate_tdlut_setting_params->tdlut_width_mode = display_cfg->plane_descriptors[k].tdlut.tdlut_width_mode;
		calculate_tdlut_setting_params->tdlut_addressing_mode = display_cfg->plane_descriptors[k].tdlut.tdlut_addressing_mode;
		calculate_tdlut_setting_params->cursor_buffer_size = mode_lib->ip.cursor_buffer_size;
		calculate_tdlut_setting_params->gpuvm_enable = display_cfg->gpuvm_enable;
		calculate_tdlut_setting_params->gpuvm_page_size_kbytes = display_cfg->plane_descriptors[k].overrides.gpuvm_min_page_size_kbytes;

		// output
		calculate_tdlut_setting_params->tdlut_pte_bytes_per_frame = &s->tdlut_pte_bytes_per_frame[k];
		calculate_tdlut_setting_params->tdlut_bytes_per_frame = &s->tdlut_bytes_per_frame[k];
		calculate_tdlut_setting_params->tdlut_groups_per_2row_ub = &s->tdlut_groups_per_2row_ub[k];
		calculate_tdlut_setting_params->tdlut_opt_time = &s->tdlut_opt_time[k];
		calculate_tdlut_setting_params->tdlut_drain_time = &s->tdlut_drain_time[k];
		calculate_tdlut_setting_params->tdlut_bytes_per_group = &s->tdlut_bytes_per_group[k];

		dcn5_calculate_tdlut_setting(&mode_lib->scratch, calculate_tdlut_setting_params);
	}

	dcn5_calculate_extra_latency(
		display_cfg,
		mode_lib->ip.rob_buffer_size_kbytes,
		0,
		s->ReorderingBytes,
		mode_lib->mp.Dcfclk,
		mode_lib->mp.FabricClock,
		mode_lib->ip.pixel_chunk_size_kbytes,
		mode_lib->mp.dram_bw_mbps,
		s->num_active_planes,
		mode_lib->mp.NoOfDPP,
		mode_lib->mp.dpte_group_bytes,
		s->tdlut_bytes_per_group,
		s->HostVMInefficiencyFactor,
		s->HostVMInefficiencyFactorPrefetch,
		dml2_qos_param_type_dcn4x,
		!(display_cfg->overrides.max_outstanding_when_urgent_expected_disable),
		utm_soc_bb->max_outstanding_reqs,
		mode_lib->mp.request_size_bytes_luma,
		mode_lib->mp.request_size_bytes_chroma,
		mode_lib->ip.meta_chunk_size_kbytes,
		mode_lib->ip.dchub_arb_to_ret_delay,
		mode_lib->mp.TripToMemory,
		mode_lib->ip.hostvm_mode,

		// output
		&mode_lib->mp.ExtraLatency,
		&mode_lib->mp.ExtraLatency_sr,
		&mode_lib->mp.ExtraLatencyPrefetch);

	mode_lib->mp.TCalc = 24.0 / mode_lib->mp.DCFCLKDeepSleep;

	for (k = 0; k < s->num_active_planes; ++k) {
		mode_lib->mp.WritebackDelay[k] = 0.0;
		for (j = 0; j < display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream; ++j) {
			mode_lib->mp.WritebackDelay[k] = math_max2(mode_lib->mp.WritebackDelay[k],
					utm_soc_bb->writeback_base_latency_us
					+ dcn5_calculate_write_back_delay(
						display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[j].pixel_format,
						display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[j].h_ratio,
						display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[j].v_ratio,
						display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[j].v_taps,
						display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[j].v_taps_chroma,
						display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[j].output_width,
						display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[j].output_height,
						display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[j].input_width,
						display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.writeback_stream[j].input_height,
						display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total)
						/ mode_lib->mp.Dispclk);
		}
	}

	/* VActive bytes to fetch for UCLK P-State */
	calculate_bytes_to_fetch_required_to_hide_latency_params->display_cfg = display_cfg;
	calculate_bytes_to_fetch_required_to_hide_latency_params->mrq_present = mode_lib->ip.dcn_mrq_present;

	calculate_bytes_to_fetch_required_to_hide_latency_params->num_active_planes = s->num_active_planes;
	calculate_bytes_to_fetch_required_to_hide_latency_params->num_of_dpp = mode_lib->mp.NoOfDPP;
	calculate_bytes_to_fetch_required_to_hide_latency_params->meta_row_height_l = mode_lib->mp.meta_row_height;
	calculate_bytes_to_fetch_required_to_hide_latency_params->meta_row_height_c = mode_lib->mp.meta_row_height_chroma;
	calculate_bytes_to_fetch_required_to_hide_latency_params->meta_row_bytes_per_row_ub_l = s->meta_row_bytes_per_row_ub_l;
	calculate_bytes_to_fetch_required_to_hide_latency_params->meta_row_bytes_per_row_ub_c = s->meta_row_bytes_per_row_ub_c;
	calculate_bytes_to_fetch_required_to_hide_latency_params->dpte_row_height_l = mode_lib->mp.dpte_row_height;
	calculate_bytes_to_fetch_required_to_hide_latency_params->dpte_row_height_c = mode_lib->mp.dpte_row_height_chroma;
	calculate_bytes_to_fetch_required_to_hide_latency_params->dpte_bytes_per_row_l = s->dpte_row_bytes_per_row_l;
	calculate_bytes_to_fetch_required_to_hide_latency_params->dpte_bytes_per_row_c = s->dpte_row_bytes_per_row_c;
	calculate_bytes_to_fetch_required_to_hide_latency_params->byte_per_pix_l = mode_lib->mp.BytePerPixelY;
	calculate_bytes_to_fetch_required_to_hide_latency_params->byte_per_pix_c = mode_lib->mp.BytePerPixelC;
	calculate_bytes_to_fetch_required_to_hide_latency_params->swath_width_l = mode_lib->mp.SwathWidthY;
	calculate_bytes_to_fetch_required_to_hide_latency_params->swath_width_c = mode_lib->mp.SwathWidthC;
	calculate_bytes_to_fetch_required_to_hide_latency_params->swath_height_l = mode_lib->mp.SwathHeightY;
	calculate_bytes_to_fetch_required_to_hide_latency_params->swath_height_c = mode_lib->mp.SwathHeightC;
	for (k = 0; k < s->num_active_planes; ++k) {
		calculate_bytes_to_fetch_required_to_hide_latency_params->latency_to_hide_us[k] = utm_soc_bb->power_management_parameters.dram_clk_change_blackout_us;
	}

	/* outputs */
	calculate_bytes_to_fetch_required_to_hide_latency_params->bytes_required_l = s->pstate_bytes_required_l[dml2_pstate_type_uclk];
	calculate_bytes_to_fetch_required_to_hide_latency_params->bytes_required_c = s->pstate_bytes_required_c[dml2_pstate_type_uclk];

	dcn5_calculate_bytes_to_fetch_required_to_hide_latency(calculate_bytes_to_fetch_required_to_hide_latency_params);

	/* Excess VActive bandwidth required to fill DET */
	dcn5_calculate_excess_vactive_bandwidth_required(
			display_cfg,
			s->num_active_planes,
			s->pstate_bytes_required_l[dml2_pstate_type_uclk],
			s->pstate_bytes_required_c[dml2_pstate_type_uclk],
			/* outputs */
			mode_lib->mp.excess_vactive_fill_bw_l,
			mode_lib->mp.excess_vactive_fill_bw_c);

	mode_lib->mp.TripToMemory = math_max2(mode_lib->mp.UrgentLatency, mode_lib->mp.TripToMemory);

	for (k = 0; k < s->num_active_planes; ++k) {
		bool cursor_not_enough_urgent_latency_hiding = 0;
		double line_time_us;

		dcn5_calculate_cursor_req_attributes(
			display_cfg->plane_descriptors[k].cursor.cursor_width,
			display_cfg->plane_descriptors[k].cursor.cursor_bpp,

			// output
			&s->cursor_lines_per_chunk[k],
			&s->cursor_bytes_per_line[k],
			&s->cursor_bytes_per_chunk[k],
			&s->cursor_bytes[k]);

		line_time_us = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);

		dcn5_calculate_cursor_urgent_burst_factor(
			mode_lib->ip.cursor_buffer_size,
			display_cfg->plane_descriptors[k].cursor.cursor_width,
			s->cursor_bytes_per_chunk[k],
			s->cursor_lines_per_chunk[k],
			line_time_us,
			mode_lib->mp.UrgentLatency,

			// output
			&mode_lib->mp.UrgentBurstFactorCursor[k],
			&cursor_not_enough_urgent_latency_hiding);
		mode_lib->mp.UrgentBurstFactorCursorPre[k] = mode_lib->mp.UrgentBurstFactorCursor[k];

		dcn5_calculate_urgent_burst_factor(
			&display_cfg->plane_descriptors[k],
			mode_lib->mp.swath_width_luma_ub[k],
			mode_lib->mp.swath_width_chroma_ub[k],
			mode_lib->mp.SwathHeightY[k],
			mode_lib->mp.SwathHeightC[k],
			line_time_us,
			mode_lib->mp.UrgentLatency,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio,
			display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio,
			mode_lib->mp.BytePerPixelInDETY[k],
			mode_lib->mp.BytePerPixelInDETC[k],
			mode_lib->mp.DETBufferSizeY[k],
			mode_lib->mp.DETBufferSizeC[k],

			/* output */
			&mode_lib->mp.UrgentBurstFactorLuma[k],
			&mode_lib->mp.UrgentBurstFactorChroma[k],
			&mode_lib->mp.NotEnoughUrgentLatencyHiding[k]);

		mode_lib->mp.NotEnoughUrgentLatencyHiding[k] = mode_lib->mp.NotEnoughUrgentLatencyHiding[k] || cursor_not_enough_urgent_latency_hiding;
	}

	for (k = 0; k < s->num_active_planes; ++k) {
		s->MaxVStartupLines[k] = dcn5_calculate_max_vstartup(
			mode_lib->ip.ptoi_supported,
			mode_lib->ip.vblank_nom_default_us,
			&display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing,
			mode_lib->mp.WritebackDelay[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u MaxVStartupLines = %u\n", __func__, k, s->MaxVStartupLines[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u WritebackDelay = %f\n", __func__, k, mode_lib->mp.WritebackDelay[k]);
	}

	s->immediate_flip_required = false;
	for (k = 0; k < s->num_active_planes; ++k) {
		s->immediate_flip_required = s->immediate_flip_required || display_cfg->plane_descriptors[k].immediate_flip;
	}
	DML_LOG_VERBOSE("DML::%s: immediate_flip_required = %u\n", __func__, s->immediate_flip_required);

	{
		s->DestinationLineTimesForPrefetchLessThan2 = false;
		s->VRatioPrefetchMoreThanMax = false;

		DML_LOG_VERBOSE("DML::%s: Start one iteration of prefetch schedule evaluation\n", __func__);

		for (k = 0; k < s->num_active_planes; ++k) {
			struct dml2_core_internal_DmlPipe *myPipe = &s->myPipe;

			DML_LOG_VERBOSE("DML::%s: k=%d MaxVStartupLines = %u\n", __func__, k, s->MaxVStartupLines[k]);
			mode_lib->mp.TWait[k] = dcn5_calculate_t_wait(
				display_cfg->plane_descriptors[k].overrides.reserved_vblank_time_ns,
				mode_lib->mp.UrgentLatency,
				mode_lib->mp.TripToMemory,
				utm_soc_bb->power_management_parameters.g7_ppt_blackout_us,
				display_cfg->stream_descriptors->timing.drr_config.enabled);

			myPipe->Dppclk = mode_lib->mp.Dppclk[k];
			myPipe->Dispclk = mode_lib->mp.Dispclk;
			myPipe->PixelClock = ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
			myPipe->DCFClkDeepSleep = mode_lib->mp.DCFCLKDeepSleep;
			myPipe->DPPPerSurface = mode_lib->mp.NoOfDPP[k];
			myPipe->ScalerEnabled = display_cfg->plane_descriptors[k].composition.scaler_info.enabled;
			myPipe->VRatio = display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio;
			myPipe->VRatioChroma = display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio;
			myPipe->VTaps = display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_taps;
			myPipe->VTapsChroma = display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_taps;
			myPipe->RotationAngle = display_cfg->plane_descriptors[k].composition.rotation_angle;
			myPipe->mirrored = display_cfg->plane_descriptors[k].composition.mirrored;
			myPipe->BlockWidth256BytesY = mode_lib->mp.Read256BlockWidthY[k];
			myPipe->BlockHeight256BytesY = mode_lib->mp.Read256BlockHeightY[k];
			myPipe->BlockWidth256BytesC = mode_lib->mp.Read256BlockWidthC[k];
			myPipe->BlockHeight256BytesC = mode_lib->mp.Read256BlockHeightC[k];
			myPipe->InterlaceEnable = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.interlaced;
			myPipe->NumberOfCursors = display_cfg->plane_descriptors[k].cursor.num_cursors;
			myPipe->VBlank = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_total - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_active;
			myPipe->HTotal = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total;
			myPipe->HActive = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_active;
			myPipe->DCCEnable = display_cfg->plane_descriptors[k].surface.dcc.enable;
			myPipe->ODMMode = mode_lib->mp.ODMMode[k];
			myPipe->SourcePixelFormat = display_cfg->plane_descriptors[k].pixel_format;
			myPipe->BytePerPixelY = mode_lib->mp.BytePerPixelY[k];
			myPipe->BytePerPixelC = mode_lib->mp.BytePerPixelC[k];
			myPipe->ProgressiveToInterlaceUnitInOPP = mode_lib->ip.ptoi_supported;
			DML_LOG_VERBOSE("DML::%s: Calling CalculatePrefetchSchedule for k=%u\n", __func__, k);

			CalculatePrefetchSchedule_params->display_cfg = display_cfg;
			CalculatePrefetchSchedule_params->HostVMInefficiencyFactor = s->HostVMInefficiencyFactorPrefetch;
			CalculatePrefetchSchedule_params->myPipe = myPipe;
			CalculatePrefetchSchedule_params->DSCDelay = mode_lib->mp.DSCDelay[k];
			CalculatePrefetchSchedule_params->DPPCLKDelaySubtotalPlusCNVCFormater = mode_lib->ip.dppclk_delay_subtotal + mode_lib->ip.dppclk_delay_cnvc_formatter;
			CalculatePrefetchSchedule_params->DPPCLKDelaySCL = mode_lib->ip.dppclk_delay_scl;
			CalculatePrefetchSchedule_params->DPPCLKDelaySCLLBOnly = mode_lib->ip.dppclk_delay_scl_lb_only;
			CalculatePrefetchSchedule_params->DPPCLKDelayCNVCCursor = mode_lib->ip.dppclk_delay_cnvc_cursor;
			CalculatePrefetchSchedule_params->DISPCLKDelaySubtotal = mode_lib->ip.dispclk_delay_subtotal;
			CalculatePrefetchSchedule_params->DPP_RECOUT_WIDTH = (unsigned int)(mode_lib->mp.SwathWidthY[k] / display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio);
			CalculatePrefetchSchedule_params->OutputFormat = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].output.output_format;
			CalculatePrefetchSchedule_params->MaxInterDCNTileRepeaters = mode_lib->ip.max_inter_dcn_tile_repeaters;
			CalculatePrefetchSchedule_params->VStartup = s->MaxVStartupLines[k];
			CalculatePrefetchSchedule_params->HostVMMinPageSize = display_cfg->plane_descriptors[k].overrides.hostvm_min_page_size_kbytes;
			CalculatePrefetchSchedule_params->DynamicMetadataEnable = display_cfg->plane_descriptors[k].dynamic_meta_data.enable;
			CalculatePrefetchSchedule_params->DynamicMetadataVMEnabled = mode_lib->ip.dynamic_metadata_vm_enabled;
			CalculatePrefetchSchedule_params->DynamicMetadataLinesBeforeActiveRequired = display_cfg->plane_descriptors[k].dynamic_meta_data.lines_before_active_required;
			CalculatePrefetchSchedule_params->DynamicMetadataTransmittedBytes = display_cfg->plane_descriptors[k].dynamic_meta_data.transmitted_bytes;
			CalculatePrefetchSchedule_params->ExtraLatencyPrefetch = mode_lib->mp.ExtraLatencyPrefetch;
			CalculatePrefetchSchedule_params->TCalc = mode_lib->mp.TCalc;
			CalculatePrefetchSchedule_params->vm_bytes = mode_lib->mp.vm_bytes[k];
			CalculatePrefetchSchedule_params->PixelPTEBytesPerRow = mode_lib->mp.PixelPTEBytesPerRow[k];
			CalculatePrefetchSchedule_params->PrefetchSourceLinesY = mode_lib->mp.PrefetchSourceLinesY[k];
			CalculatePrefetchSchedule_params->VInitPreFillY = mode_lib->mp.VInitPreFillY[k];
			CalculatePrefetchSchedule_params->MaxNumSwathY = mode_lib->mp.MaxNumSwathY[k];
			CalculatePrefetchSchedule_params->PrefetchSourceLinesC = mode_lib->mp.PrefetchSourceLinesC[k];
			CalculatePrefetchSchedule_params->VInitPreFillC = mode_lib->mp.VInitPreFillC[k];
			CalculatePrefetchSchedule_params->MaxNumSwathC = mode_lib->mp.MaxNumSwathC[k];
			CalculatePrefetchSchedule_params->swath_width_luma_ub = mode_lib->mp.swath_width_luma_ub[k];
			CalculatePrefetchSchedule_params->swath_width_chroma_ub = mode_lib->mp.swath_width_chroma_ub[k];
			CalculatePrefetchSchedule_params->SwathHeightY = mode_lib->mp.SwathHeightY[k];
			CalculatePrefetchSchedule_params->SwathHeightC = mode_lib->mp.SwathHeightC[k];
			CalculatePrefetchSchedule_params->TWait = mode_lib->mp.TWait[k];
			CalculatePrefetchSchedule_params->Ttrip = mode_lib->mp.TripToMemory;
			CalculatePrefetchSchedule_params->Turg = mode_lib->mp.UrgentLatency;
			CalculatePrefetchSchedule_params->setup_for_tdlut = display_cfg->plane_descriptors[k].tdlut.setup_for_tdlut;
			CalculatePrefetchSchedule_params->tdlut_pte_bytes_per_frame = s->tdlut_pte_bytes_per_frame[k];
			CalculatePrefetchSchedule_params->tdlut_bytes_per_frame = s->tdlut_bytes_per_frame[k];
			CalculatePrefetchSchedule_params->tdlut_opt_time = s->tdlut_opt_time[k];
			CalculatePrefetchSchedule_params->tdlut_drain_time = s->tdlut_drain_time[k];
			CalculatePrefetchSchedule_params->num_cursors = (display_cfg->plane_descriptors[k].cursor.cursor_width > 0);
			CalculatePrefetchSchedule_params->cursor_bytes_per_chunk = s->cursor_bytes_per_chunk[k];
			CalculatePrefetchSchedule_params->cursor_bytes_per_line = s->cursor_bytes_per_line[k];
			CalculatePrefetchSchedule_params->dcc_enable = display_cfg->plane_descriptors[k].surface.dcc.enable;
			CalculatePrefetchSchedule_params->mrq_present = mode_lib->ip.dcn_mrq_present;
			CalculatePrefetchSchedule_params->meta_row_bytes = mode_lib->mp.meta_row_bytes[k];

			// output
			CalculatePrefetchSchedule_params->DSTXAfterScaler = &mode_lib->mp.DSTXAfterScaler[k];
			CalculatePrefetchSchedule_params->DSTYAfterScaler = &mode_lib->mp.DSTYAfterScaler[k];
			CalculatePrefetchSchedule_params->dst_y_prefetch = &mode_lib->mp.dst_y_prefetch[k];
			CalculatePrefetchSchedule_params->dst_y_per_vm_vblank = &mode_lib->mp.dst_y_per_vm_vblank[k];
			CalculatePrefetchSchedule_params->dst_y_per_row_vblank = &mode_lib->mp.dst_y_per_row_vblank[k];
			CalculatePrefetchSchedule_params->VRatioPrefetchY = &mode_lib->mp.VRatioPrefetchY[k];
			CalculatePrefetchSchedule_params->VRatioPrefetchC = &mode_lib->mp.VRatioPrefetchC[k];
			CalculatePrefetchSchedule_params->RequiredPrefetchPixelDataBWLuma = &mode_lib->mp.RequiredPrefetchPixelDataBWLuma[k];
			CalculatePrefetchSchedule_params->RequiredPrefetchPixelDataBWChroma = &mode_lib->mp.RequiredPrefetchPixelDataBWChroma[k];
			CalculatePrefetchSchedule_params->NotEnoughTimeForDynamicMetadata = &mode_lib->mp.NotEnoughTimeForDynamicMetadata[k];
			CalculatePrefetchSchedule_params->Tno_bw = &mode_lib->mp.Tno_bw[k];
			CalculatePrefetchSchedule_params->Tno_bw_flip = &mode_lib->mp.Tno_bw_flip[k];
			CalculatePrefetchSchedule_params->prefetch_vmrow_bw = &mode_lib->mp.prefetch_vmrow_bw[k];
			CalculatePrefetchSchedule_params->Tdmdl_vm = &mode_lib->mp.Tdmdl_vm[k];
			CalculatePrefetchSchedule_params->Tdmdl = &mode_lib->mp.Tdmdl[k];
			CalculatePrefetchSchedule_params->TSetup = &mode_lib->mp.TSetup[k];
			CalculatePrefetchSchedule_params->Tvm_trips = &s->Tvm_trips[k];
			CalculatePrefetchSchedule_params->Tr0_trips = &s->Tr0_trips[k];
			CalculatePrefetchSchedule_params->Tvm_trips_flip = &s->Tvm_trips_flip[k];
			CalculatePrefetchSchedule_params->Tr0_trips_flip = &s->Tr0_trips_flip[k];
			CalculatePrefetchSchedule_params->Tvm_trips_flip_rounded = &s->Tvm_trips_flip_rounded[k];
			CalculatePrefetchSchedule_params->Tr0_trips_flip_rounded = &s->Tr0_trips_flip_rounded[k];
			CalculatePrefetchSchedule_params->VUpdateOffsetPix = &mode_lib->mp.VUpdateOffsetPix[k];
			CalculatePrefetchSchedule_params->VUpdateWidthPix = &mode_lib->mp.VUpdateWidthPix[k];
			CalculatePrefetchSchedule_params->VReadyOffsetPix = &mode_lib->mp.VReadyOffsetPix[k];
			CalculatePrefetchSchedule_params->prefetch_cursor_bw = &mode_lib->mp.prefetch_cursor_bw[k];
			CalculatePrefetchSchedule_params->prefetch_sw_bytes = &s->prefetch_sw_bytes[k];
			CalculatePrefetchSchedule_params->Tpre_rounded = &s->Tpre_rounded[k];
			CalculatePrefetchSchedule_params->Tpre_oto = &s->Tpre_oto[k];

			mode_lib->mp.NoTimeToPrefetch[k] = dcn5_calculate_prefetch_schedule(&mode_lib->scratch, CalculatePrefetchSchedule_params);
			DML_LOG_VERBOSE("DML::%s: k=%0u NoTimeToPrefetch=%0d\n", __func__, k, mode_lib->mp.NoTimeToPrefetch[k]);
			mode_lib->mp.VStartupMin[k] = s->MaxVStartupLines[k];
		} // for k

		mode_lib->mp.PrefetchModeSupported = true;
		for (k = 0; k < s->num_active_planes; ++k) {
			if (mode_lib->mp.NoTimeToPrefetch[k] == true ||
				mode_lib->mp.NotEnoughTimeForDynamicMetadata[k] ||
				mode_lib->mp.DSTYAfterScaler[k] > 8) {
				DML_LOG_VERBOSE("DML::%s: k=%u, NoTimeToPrefetch = %0d\n", __func__, k, mode_lib->mp.NoTimeToPrefetch[k]);
				DML_LOG_VERBOSE("DML::%s: k=%u, NotEnoughTimeForDynamicMetadata=%u\n", __func__, k, mode_lib->mp.NotEnoughTimeForDynamicMetadata[k]);
				DML_LOG_VERBOSE("DML::%s: k=%u, DSTYAfterScaler=%u (should be <= 0)\n", __func__, k, mode_lib->mp.DSTYAfterScaler[k]);
				mode_lib->mp.PrefetchModeSupported = false;
			}
			if (mode_lib->mp.dst_y_prefetch[k] < 2)
				s->DestinationLineTimesForPrefetchLessThan2 = true;

			if (mode_lib->mp.VRatioPrefetchY[k] > __DML2_CALCS_MAX_VRATIO_PRE__ ||
				mode_lib->mp.VRatioPrefetchC[k] > __DML2_CALCS_MAX_VRATIO_PRE__) {
				s->VRatioPrefetchMoreThanMax = true;
				DML_LOG_VERBOSE("DML::%s: k=%d, VRatioPrefetchY=%f (should not be < %f)\n", __func__, k, mode_lib->mp.VRatioPrefetchY[k], __DML2_CALCS_MAX_VRATIO_PRE__);
				DML_LOG_VERBOSE("DML::%s: k=%d, VRatioPrefetchC=%f (should not be < %f)\n", __func__, k, mode_lib->mp.VRatioPrefetchC[k], __DML2_CALCS_MAX_VRATIO_PRE__);
				DML_LOG_VERBOSE("DML::%s: VRatioPrefetchMoreThanMax = %u\n", __func__, s->VRatioPrefetchMoreThanMax);
			}

			if (mode_lib->mp.NotEnoughUrgentLatencyHiding[k]) {
				DML_LOG_VERBOSE("DML::%s: k=%u, NotEnoughUrgentLatencyHiding = %u\n", __func__, k, mode_lib->mp.NotEnoughUrgentLatencyHiding[k]);
				mode_lib->mp.PrefetchModeSupported = false;
			}
		}

		if (s->VRatioPrefetchMoreThanMax == true || s->DestinationLineTimesForPrefetchLessThan2 == true) {
			DML_LOG_VERBOSE("DML::%s: VRatioPrefetchMoreThanMax = %u\n", __func__, s->VRatioPrefetchMoreThanMax);
			DML_LOG_VERBOSE("DML::%s: DestinationLineTimesForPrefetchLessThan2 = %u\n", __func__, s->DestinationLineTimesForPrefetchLessThan2);
			mode_lib->mp.PrefetchModeSupported = false;
		}

		DML_LOG_VERBOSE("DML::%s: Prefetch schedule is %sOK at vstartup = %u\n", __func__,
			mode_lib->mp.PrefetchModeSupported ? "" : "NOT ", CalculatePrefetchSchedule_params->VStartup);

		// Prefetch schedule OK, now check prefetch bw
		if (mode_lib->mp.PrefetchModeSupported == true) {
			for (k = 0; k < s->num_active_planes; ++k) {
				double line_time_us = display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total /
					((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
				dcn5_calculate_urgent_burst_factor(
					&display_cfg->plane_descriptors[k],
					mode_lib->mp.swath_width_luma_ub[k],
					mode_lib->mp.swath_width_chroma_ub[k],
					mode_lib->mp.SwathHeightY[k],
					mode_lib->mp.SwathHeightC[k],
					line_time_us,
					mode_lib->mp.UrgentLatency,
					mode_lib->mp.VRatioPrefetchY[k],
					mode_lib->mp.VRatioPrefetchC[k],
					mode_lib->mp.BytePerPixelInDETY[k],
					mode_lib->mp.BytePerPixelInDETC[k],
					mode_lib->mp.DETBufferSizeY[k],
					mode_lib->mp.DETBufferSizeC[k],
					/* Output */
					&mode_lib->mp.UrgentBurstFactorLumaPre[k],
					&mode_lib->mp.UrgentBurstFactorChromaPre[k],
					&mode_lib->mp.NotEnoughUrgentLatencyHidingPre[k]);

				DML_LOG_VERBOSE("DML::%s: k=%0u DPPPerSurface=%u\n", __func__, k, mode_lib->mp.NoOfDPP[k]);
				DML_LOG_VERBOSE("DML::%s: k=%0u UrgentBurstFactorLuma=%f\n", __func__, k, mode_lib->mp.UrgentBurstFactorLuma[k]);
				DML_LOG_VERBOSE("DML::%s: k=%0u UrgentBurstFactorChroma=%f\n", __func__, k, mode_lib->mp.UrgentBurstFactorChroma[k]);
				DML_LOG_VERBOSE("DML::%s: k=%0u UrgentBurstFactorLumaPre=%f\n", __func__, k, mode_lib->mp.UrgentBurstFactorLumaPre[k]);
				DML_LOG_VERBOSE("DML::%s: k=%0u UrgentBurstFactorChromaPre=%f\n", __func__, k, mode_lib->mp.UrgentBurstFactorChromaPre[k]);

				DML_LOG_VERBOSE("DML::%s: k=%0u VRatioPrefetchY=%f\n", __func__, k, mode_lib->mp.VRatioPrefetchY[k]);
				DML_LOG_VERBOSE("DML::%s: k=%0u VRatioY=%f\n", __func__, k, display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio);

				DML_LOG_VERBOSE("DML::%s: k=%0u prefetch_vmrow_bw=%f\n", __func__, k, mode_lib->mp.prefetch_vmrow_bw[k]);
				DML_LOG_VERBOSE("DML::%s: k=%0u vactive_sw_bw_l=%f\n", __func__, k, mode_lib->mp.vactive_sw_bw_l[k]);
				DML_LOG_VERBOSE("DML::%s: k=%0u vactive_sw_bw_c=%f\n", __func__, k, mode_lib->mp.vactive_sw_bw_c[k]);
				DML_LOG_VERBOSE("DML::%s: k=%0u cursor_bw=%f\n", __func__, k, mode_lib->mp.cursor_bw[k]);
				DML_LOG_VERBOSE("DML::%s: k=%0u dpte_row_bw=%f\n", __func__, k, mode_lib->mp.dpte_row_bw[k]);
				DML_LOG_VERBOSE("DML::%s: k=%0u meta_row_bw=%f\n", __func__, k, mode_lib->mp.meta_row_bw[k]);
				DML_LOG_VERBOSE("DML::%s: k=%0u RequiredPrefetchPixelDataBWLuma=%f\n", __func__, k, mode_lib->mp.RequiredPrefetchPixelDataBWLuma[k]);
				DML_LOG_VERBOSE("DML::%s: k=%0u RequiredPrefetchPixelDataBWChroma=%f\n", __func__, k, mode_lib->mp.RequiredPrefetchPixelDataBWChroma[k]);
				DML_LOG_VERBOSE("DML::%s: k=%0u prefetch_cursor_bw=%f\n", __func__, k, mode_lib->mp.prefetch_cursor_bw[k]);
			}

			for (k = 0; k <= s->num_active_planes - 1; k++)
				mode_lib->mp.final_flip_bw[k] = 0;

			calculate_peak_bandwidth_params->urg_vactive_bandwidth_required = mode_lib->mp.urg_vactive_bandwidth_required;
			calculate_peak_bandwidth_params->urg_bandwidth_required = mode_lib->mp.urg_bandwidth_required;
			calculate_peak_bandwidth_params->urg_bandwidth_required_qual = mode_lib->mp.urg_bandwidth_required_qual;
			calculate_peak_bandwidth_params->non_urg_bandwidth_required = mode_lib->mp.non_urg_bandwidth_required;
			calculate_peak_bandwidth_params->surface_avg_vactive_required_bw = s->surface_dummy_bw;
			calculate_peak_bandwidth_params->surface_peak_required_bw = s->surface_dummy_bw0;

			calculate_peak_bandwidth_params->display_cfg = display_cfg;
			calculate_peak_bandwidth_params->inc_flip_bw = 0;
			calculate_peak_bandwidth_params->num_active_planes = s->num_active_planes;
			calculate_peak_bandwidth_params->num_of_dpp = mode_lib->mp.NoOfDPP;
			calculate_peak_bandwidth_params->dcc_dram_bw_nom_overhead_factor_p0 = mode_lib->mp.dcc_dram_bw_nom_overhead_factor_p0;
			calculate_peak_bandwidth_params->dcc_dram_bw_nom_overhead_factor_p1 = mode_lib->mp.dcc_dram_bw_nom_overhead_factor_p1;
			calculate_peak_bandwidth_params->dcc_dram_bw_pref_overhead_factor_p0 = mode_lib->mp.dcc_dram_bw_pref_overhead_factor_p0;
			calculate_peak_bandwidth_params->dcc_dram_bw_pref_overhead_factor_p1 = mode_lib->mp.dcc_dram_bw_pref_overhead_factor_p1;

			calculate_peak_bandwidth_params->surface_read_bandwidth_l = mode_lib->mp.vactive_sw_bw_l;
			calculate_peak_bandwidth_params->surface_read_bandwidth_c = mode_lib->mp.vactive_sw_bw_c;
			calculate_peak_bandwidth_params->prefetch_bandwidth_l = mode_lib->mp.RequiredPrefetchPixelDataBWLuma;
			calculate_peak_bandwidth_params->prefetch_bandwidth_c = mode_lib->mp.RequiredPrefetchPixelDataBWChroma;
			calculate_peak_bandwidth_params->excess_vactive_fill_bw_l = mode_lib->mp.excess_vactive_fill_bw_l;
			calculate_peak_bandwidth_params->excess_vactive_fill_bw_c = mode_lib->mp.excess_vactive_fill_bw_c;
			calculate_peak_bandwidth_params->cursor_bw = mode_lib->mp.cursor_bw;
			calculate_peak_bandwidth_params->dpte_row_bw = mode_lib->mp.dpte_row_bw;
			calculate_peak_bandwidth_params->meta_row_bw = mode_lib->mp.meta_row_bw;
			calculate_peak_bandwidth_params->prefetch_cursor_bw = mode_lib->mp.prefetch_cursor_bw;
			calculate_peak_bandwidth_params->prefetch_vmrow_bw = mode_lib->mp.prefetch_vmrow_bw;
			calculate_peak_bandwidth_params->flip_bw = mode_lib->mp.final_flip_bw;
			calculate_peak_bandwidth_params->urgent_burst_factor_l = mode_lib->mp.UrgentBurstFactorLuma;
			calculate_peak_bandwidth_params->urgent_burst_factor_c = mode_lib->mp.UrgentBurstFactorChroma;
			calculate_peak_bandwidth_params->urgent_burst_factor_cursor = mode_lib->mp.UrgentBurstFactorCursor;
			calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_l = mode_lib->mp.UrgentBurstFactorLumaPre;
			calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_c = mode_lib->mp.UrgentBurstFactorChromaPre;
			calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_cursor = mode_lib->mp.UrgentBurstFactorCursorPre;

			dcn5_calculate_peak_bandwidth_required(
					&mode_lib->scratch,
					calculate_peak_bandwidth_params);

			// Check urg peak bandwidth against available urg bw
			// check at SDP and DRAM, for all soc states (Sys Active)
			dcn5_check_urgent_bandwidth_support(
				&mode_lib->mp.FractionOfUrgentBandwidth, // double* frac_urg_bandwidth
				&mode_lib->mp.PrefetchModeSupported, // prefetch bw ok

				**mode_lib->mp.non_urg_bandwidth_required,
				**mode_lib->mp.urg_bandwidth_required,
				mode_lib->mp.dram_bw_mbps);

			if (!mode_lib->mp.PrefetchModeSupported)
				DML_LOG_VERBOSE("DML::%s: Bandwidth not sufficient for prefetch!\n", __func__);

			for (k = 0; k < s->num_active_planes; ++k) {
				if (mode_lib->mp.NotEnoughUrgentLatencyHidingPre[k]) {
					DML_LOG_VERBOSE("DML::%s: k=%u, NotEnoughUrgentLatencyHidingPre = %u\n", __func__, k, mode_lib->mp.NotEnoughUrgentLatencyHidingPre[k]);
					mode_lib->mp.PrefetchModeSupported = false;
				}
			}
		} // prefetch schedule ok

		// Prefetch schedule and prefetch bw ok, now check flip bw
		if (mode_lib->mp.PrefetchModeSupported == true) { // prefetch schedule and prefetch bw ok, now check flip bw

			mode_lib->mp.BandwidthAvailableForImmediateFlip =
				dcn5_get_bandwidth_available_for_immediate_flip(
					**mode_lib->mp.urg_bandwidth_required_qual, // no flip
					mode_lib->mp.dram_bw_mbps);

			mode_lib->mp.TotImmediateFlipBytes = 0;

			for (k = 0; k < s->num_active_planes; ++k) {
				if (display_cfg->plane_descriptors[k].immediate_flip) {
					s->per_pipe_flip_bytes[k] =  dcn5_get_pipe_flip_bytes(s->HostVMInefficiencyFactor,
											mode_lib->mp.vm_bytes[k],
											mode_lib->mp.PixelPTEBytesPerRow[k],
											mode_lib->mp.meta_row_bytes[k]);
				} else {
					s->per_pipe_flip_bytes[k] = 0;
				}
				mode_lib->mp.TotImmediateFlipBytes += s->per_pipe_flip_bytes[k] * mode_lib->mp.NoOfDPP[k];
				DML_LOG_VERBOSE("DML::%s: k = %u\n", __func__, k);
				DML_LOG_VERBOSE("DML::%s: DPPPerSurface = %u\n", __func__, mode_lib->mp.NoOfDPP[k]);
				DML_LOG_VERBOSE("DML::%s: vm_bytes = %u\n", __func__, mode_lib->mp.vm_bytes[k]);
				DML_LOG_VERBOSE("DML::%s: PixelPTEBytesPerRow = %u\n", __func__, mode_lib->mp.PixelPTEBytesPerRow[k]);
				DML_LOG_VERBOSE("DML::%s: meta_row_bytes = %u\n", __func__, mode_lib->mp.meta_row_bytes[k]);
				DML_LOG_VERBOSE("DML::%s: TotImmediateFlipBytes = %u\n", __func__, mode_lib->mp.TotImmediateFlipBytes);
			}
			for (k = 0; k < s->num_active_planes; ++k) {
				dcn5_calculate_flip_schedule(
					&mode_lib->scratch,
					display_cfg->plane_descriptors[k].immediate_flip,
					0, // use_lb_flip_bw
					s->HostVMInefficiencyFactor,
					s->Tvm_trips_flip[k],
					s->Tr0_trips_flip[k],
					s->Tvm_trips_flip_rounded[k],
					s->Tr0_trips_flip_rounded[k],
					display_cfg->gpuvm_enable,
					mode_lib->mp.vm_bytes[k],
					mode_lib->mp.PixelPTEBytesPerRow[k],
					mode_lib->mp.BandwidthAvailableForImmediateFlip,
					mode_lib->mp.TotImmediateFlipBytes,
					display_cfg->plane_descriptors[k].pixel_format,
					display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000),
					display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio,
					display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio,
					mode_lib->mp.Tno_bw[k],
					mode_lib->mp.dpte_row_height[k],
					mode_lib->mp.dpte_row_height_chroma[k],
					mode_lib->mp.use_one_row_for_frame_flip[k],
					mode_lib->ip.max_flip_time_us,
					mode_lib->ip.max_flip_time_lines,
					s->per_pipe_flip_bytes[k],
					mode_lib->mp.meta_row_bytes[k],
					mode_lib->mp.meta_row_height[k],
					mode_lib->mp.meta_row_height_chroma[k],
					mode_lib->ip.dcn_mrq_present && display_cfg->plane_descriptors[k].surface.dcc.enable,

					// Output
					&mode_lib->mp.dst_y_per_vm_flip[k],
					&mode_lib->mp.dst_y_per_row_flip[k],
					&mode_lib->mp.final_flip_bw[k],
					&mode_lib->mp.ImmediateFlipSupportedForPipe[k]);
			}

			calculate_peak_bandwidth_params->urg_vactive_bandwidth_required = s->dummy_bw;
			calculate_peak_bandwidth_params->urg_bandwidth_required = mode_lib->mp.urg_bandwidth_required_flip;
			calculate_peak_bandwidth_params->urg_bandwidth_required_qual = s->dummy_bw;
			calculate_peak_bandwidth_params->non_urg_bandwidth_required = mode_lib->mp.non_urg_bandwidth_required_flip;
			calculate_peak_bandwidth_params->surface_avg_vactive_required_bw = s->surface_dummy_bw;
			calculate_peak_bandwidth_params->surface_peak_required_bw = s->surface_dummy_bw0;

			calculate_peak_bandwidth_params->display_cfg = display_cfg;
			calculate_peak_bandwidth_params->inc_flip_bw = 1;
			calculate_peak_bandwidth_params->num_active_planes = s->num_active_planes;
			calculate_peak_bandwidth_params->num_of_dpp = mode_lib->mp.NoOfDPP;
			calculate_peak_bandwidth_params->dcc_dram_bw_nom_overhead_factor_p0 = mode_lib->mp.dcc_dram_bw_nom_overhead_factor_p0;
			calculate_peak_bandwidth_params->dcc_dram_bw_nom_overhead_factor_p1 = mode_lib->mp.dcc_dram_bw_nom_overhead_factor_p1;
			calculate_peak_bandwidth_params->dcc_dram_bw_pref_overhead_factor_p0 = mode_lib->mp.dcc_dram_bw_pref_overhead_factor_p0;
			calculate_peak_bandwidth_params->dcc_dram_bw_pref_overhead_factor_p1 = mode_lib->mp.dcc_dram_bw_pref_overhead_factor_p1;

			calculate_peak_bandwidth_params->surface_read_bandwidth_l = mode_lib->mp.vactive_sw_bw_l;
			calculate_peak_bandwidth_params->surface_read_bandwidth_c = mode_lib->mp.vactive_sw_bw_c;
			calculate_peak_bandwidth_params->prefetch_bandwidth_l = mode_lib->mp.RequiredPrefetchPixelDataBWLuma;
			calculate_peak_bandwidth_params->prefetch_bandwidth_c = mode_lib->mp.RequiredPrefetchPixelDataBWChroma;
			calculate_peak_bandwidth_params->excess_vactive_fill_bw_l = mode_lib->mp.excess_vactive_fill_bw_l;
			calculate_peak_bandwidth_params->excess_vactive_fill_bw_c = mode_lib->mp.excess_vactive_fill_bw_c;
			calculate_peak_bandwidth_params->cursor_bw = mode_lib->mp.cursor_bw;
			calculate_peak_bandwidth_params->dpte_row_bw = mode_lib->mp.dpte_row_bw;
			calculate_peak_bandwidth_params->meta_row_bw = mode_lib->mp.meta_row_bw;
			calculate_peak_bandwidth_params->prefetch_cursor_bw = mode_lib->mp.prefetch_cursor_bw;
			calculate_peak_bandwidth_params->prefetch_vmrow_bw = mode_lib->mp.prefetch_vmrow_bw;
			calculate_peak_bandwidth_params->flip_bw = mode_lib->mp.final_flip_bw;
			calculate_peak_bandwidth_params->urgent_burst_factor_l = mode_lib->mp.UrgentBurstFactorLuma;
			calculate_peak_bandwidth_params->urgent_burst_factor_c = mode_lib->mp.UrgentBurstFactorChroma;
			calculate_peak_bandwidth_params->urgent_burst_factor_cursor = mode_lib->mp.UrgentBurstFactorCursor;
			calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_l = mode_lib->mp.UrgentBurstFactorLumaPre;
			calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_c = mode_lib->mp.UrgentBurstFactorChromaPre;
			calculate_peak_bandwidth_params->urgent_burst_factor_prefetch_cursor = mode_lib->mp.UrgentBurstFactorCursorPre;

			dcn5_calculate_peak_bandwidth_required(
					&mode_lib->scratch,
					calculate_peak_bandwidth_params);

			dcn5_check_immediate_flip_bandwidth_support(
				&mode_lib->mp.FractionOfUrgentBandwidthImmediateFlip, // double* frac_urg_bandwidth_flip
				&mode_lib->mp.ImmediateFlipSupported, // bool* flip_bandwidth_support_ok
				**mode_lib->mp.urg_bandwidth_required_flip,
				**mode_lib->mp.non_urg_bandwidth_required_flip,
				mode_lib->mp.dram_bw_mbps);

			if (!mode_lib->mp.ImmediateFlipSupported)
				DML_LOG_VERBOSE("DML::%s: Bandwidth not sufficient for flip!", __func__);

			for (k = 0; k < s->num_active_planes; ++k) {
				if (display_cfg->plane_descriptors[k].immediate_flip && mode_lib->mp.ImmediateFlipSupportedForPipe[k] == false) {
					mode_lib->mp.ImmediateFlipSupported = false;
					DML_LOG_VERBOSE("DML::%s: Pipe %0d not supporting iflip!\n", __func__, k);
				}
			}
		} else { // flip or prefetch not support
			mode_lib->mp.ImmediateFlipSupported = false;
		}

		// consider flip support is okay if the flip bw is ok or (when user does't require a iflip and there is no host vm)
		must_support_iflip = display_cfg->hostvm_enable || s->immediate_flip_required;
		mode_lib->mp.PrefetchAndImmediateFlipSupported = (mode_lib->mp.PrefetchModeSupported == true && (!must_support_iflip || mode_lib->mp.ImmediateFlipSupported));

		DML_LOG_VERBOSE("DML::%s: PrefetchModeSupported = %u\n", __func__, mode_lib->mp.PrefetchModeSupported);
		for (k = 0; k < s->num_active_planes; ++k)
			DML_LOG_VERBOSE("DML::%s: immediate_flip_required[%u] = %u\n", __func__, k, display_cfg->plane_descriptors[k].immediate_flip);
		DML_LOG_VERBOSE("DML::%s: HostVMEnable = %u\n", __func__, display_cfg->hostvm_enable);
		DML_LOG_VERBOSE("DML::%s: ImmediateFlipSupported = %u\n", __func__, mode_lib->mp.ImmediateFlipSupported);
		DML_LOG_VERBOSE("DML::%s: PrefetchAndImmediateFlipSupported = %u\n", __func__, mode_lib->mp.PrefetchAndImmediateFlipSupported);
		DML_LOG_VERBOSE("DML::%s: Done one iteration: k=%d, MaxVStartupLines=%u\n", __func__, k, s->MaxVStartupLines[k]);
	}

	for (k = 0; k < s->num_active_planes; ++k)
		DML_LOG_VERBOSE("DML::%s: k=%d MaxVStartupLines = %u\n", __func__, k, s->MaxVStartupLines[k]);

	if (!mode_lib->mp.PrefetchAndImmediateFlipSupported) {
		DML_LOG_VERBOSE("DML::%s: Bad, Prefetch and flip scheduling solution NOT found!\n", __func__);
	} else {
		DML_LOG_VERBOSE("DML::%s: Good, Prefetch and flip scheduling solution found\n", __func__);

		// DCC Configuration
		for (k = 0; k < s->num_active_planes; ++k) {
			DML_LOG_VERBOSE("DML::%s: Calculate DCC configuration for surface k=%u\n", __func__, k);
			dcn5_calculate_dcc_configuration(
				display_cfg->plane_descriptors[k].surface.dcc.enable,
				display_cfg->overrides.dcc_programming_assumes_scan_direction_unknown,
				display_cfg->plane_descriptors[k].pixel_format,
				display_cfg->plane_descriptors[k].surface.plane0.width,
				display_cfg->plane_descriptors[k].surface.plane1.width,
				display_cfg->plane_descriptors[k].surface.plane0.height,
				display_cfg->plane_descriptors[k].surface.plane1.height,
				s->NomDETInKByte,
				mode_lib->mp.Read256BlockHeightY[k],
				mode_lib->mp.Read256BlockHeightC[k],
				display_cfg->plane_descriptors[k].surface.tiling,
				mode_lib->mp.BytePerPixelY[k],
				mode_lib->mp.BytePerPixelC[k],
				mode_lib->mp.BytePerPixelInDETY[k],
				mode_lib->mp.BytePerPixelInDETC[k],
				display_cfg->plane_descriptors[k].composition.rotation_angle,

				/* Output */
				&mode_lib->mp.RequestLuma[k],
				&mode_lib->mp.RequestChroma[k],
				&mode_lib->mp.DCCYMaxUncompressedBlock[k],
				&mode_lib->mp.DCCCMaxUncompressedBlock[k],
				&mode_lib->mp.DCCYMaxCompressedBlock[k],
				&mode_lib->mp.DCCCMaxCompressedBlock[k],
				&mode_lib->mp.DCCYIndependentBlock[k],
				&mode_lib->mp.DCCCIndependentBlock[k]);
		}

		//Watermarks and NB P-State/DRAM Clock Change Support
		s->mmSOCParameters.UrgentLatency = mode_lib->mp.UrgentLatency;
		s->mmSOCParameters.ExtraLatency = mode_lib->mp.ExtraLatency;
		s->mmSOCParameters.ExtraLatency_sr = mode_lib->mp.ExtraLatency_sr;
		s->mmSOCParameters.WritebackLatency = utm_soc_bb->writeback_base_latency_us;
		s->mmSOCParameters.DRAMClockChangeLatency = utm_soc_bb->power_management_parameters.dram_clk_change_blackout_us;
		s->mmSOCParameters.FCLKChangeLatency = utm_soc_bb->power_management_parameters.fclk_change_blackout_us;
		s->mmSOCParameters.SRExitTime = utm_soc_bb->power_management_parameters.stutter_exit_latency_us;
		s->mmSOCParameters.SREnterPlusExitTime = utm_soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us;
		s->mmSOCParameters.SRExitZ8Time = utm_soc_bb->power_management_parameters.z8_stutter_exit_latency_us;
		s->mmSOCParameters.SREnterPlusExitZ8Time = utm_soc_bb->power_management_parameters.z8_stutter_enter_plus_exit_latency_us;
		s->mmSOCParameters.USRRetrainingLatency = 0;
		s->mmSOCParameters.SMNLatency = 0;
		s->mmSOCParameters.temp_read_or_ppt_blackout_us = utm_soc_bb->power_management_parameters.g7_ppt_blackout_us;
		s->mmSOCParameters.qos_type = dml2_qos_param_type_dcn4x;

		CalculateWatermarks_params->display_cfg = display_cfg;
		CalculateWatermarks_params->USRRetrainingRequired = false;
		CalculateWatermarks_params->NumberOfActiveSurfaces = s->num_active_planes;
		CalculateWatermarks_params->MaxLineBufferLines = mode_lib->ip.max_line_buffer_lines;
		CalculateWatermarks_params->LineBufferSize = mode_lib->ip.line_buffer_size_bits;
		CalculateWatermarks_params->WritebackInterfaceBufferSize = mode_lib->ip.writeback_interface_buffer_size_kbytes;
		CalculateWatermarks_params->DCFCLK = mode_lib->mp.Dcfclk;
		CalculateWatermarks_params->SynchronizeTimings = display_cfg->overrides.synchronize_timings;
		CalculateWatermarks_params->SynchronizeDRRDisplaysForUCLKPStateChange = display_cfg->overrides.synchronize_ddr_displays_for_uclk_pstate_change;
		CalculateWatermarks_params->dpte_group_bytes = mode_lib->mp.dpte_group_bytes;
		CalculateWatermarks_params->mmSOCParameters = s->mmSOCParameters;
		CalculateWatermarks_params->WritebackChunkSize = mode_lib->ip.writeback_chunk_size_kbytes;
		CalculateWatermarks_params->SOCCLK = s->SOCCLK;
		CalculateWatermarks_params->DCFClkDeepSleep = mode_lib->mp.DCFCLKDeepSleep;
		CalculateWatermarks_params->DETBufferSizeY = mode_lib->mp.DETBufferSizeY;
		CalculateWatermarks_params->DETBufferSizeC = mode_lib->mp.DETBufferSizeC;
		CalculateWatermarks_params->SwathHeightY = mode_lib->mp.SwathHeightY;
		CalculateWatermarks_params->SwathHeightC = mode_lib->mp.SwathHeightC;
		//CalculateWatermarks_params->LBBitPerPixel = 57; //FIXME_STAGE2
		CalculateWatermarks_params->SwathWidthY = mode_lib->mp.SwathWidthY;
		CalculateWatermarks_params->SwathWidthC = mode_lib->mp.SwathWidthC;
		CalculateWatermarks_params->BytePerPixelDETY = mode_lib->mp.BytePerPixelInDETY;
		CalculateWatermarks_params->BytePerPixelDETC = mode_lib->mp.BytePerPixelInDETC;
		CalculateWatermarks_params->DSTXAfterScaler = mode_lib->mp.DSTXAfterScaler;
		CalculateWatermarks_params->DSTYAfterScaler = mode_lib->mp.DSTYAfterScaler;
		CalculateWatermarks_params->UnboundedRequestEnabled = mode_lib->mp.UnboundedRequestEnabled;
		CalculateWatermarks_params->CompressedBufferSizeInkByte = mode_lib->mp.CompressedBufferSizeInkByte;
		CalculateWatermarks_params->meta_row_height_l = mode_lib->mp.meta_row_height;
		CalculateWatermarks_params->meta_row_height_c = mode_lib->mp.meta_row_height_chroma;
		CalculateWatermarks_params->DPPPerSurface = mode_lib->mp.NoOfDPP;
		CalculateWatermarks_params->uclk_pstate_switch_modes = mode_lib->mp.uclk_pstate_switch_modes;

		// Output
		CalculateWatermarks_params->Watermark = &mode_lib->mp.Watermark;
		CalculateWatermarks_params->DRAMClockChangeSupport = mode_lib->mp.DRAMClockChangeSupport;
		CalculateWatermarks_params->global_dram_clock_change_support_required = &s->dummy_boolean[0];
		CalculateWatermarks_params->global_dram_clock_change_supported = &mode_lib->mp.global_dram_clock_change_supported;
		CalculateWatermarks_params->MaxActiveDRAMClockChangeLatencySupported = mode_lib->mp.MaxActiveDRAMClockChangeLatencySupported;
		CalculateWatermarks_params->FCLKChangeSupport = mode_lib->mp.FCLKChangeSupport;
		CalculateWatermarks_params->global_fclk_change_supported = &mode_lib->mp.global_fclk_change_supported;
		CalculateWatermarks_params->MaxActiveFCLKChangeLatencySupported = &mode_lib->mp.MaxActiveFCLKChangeLatencySupported;
		CalculateWatermarks_params->USRRetrainingSupport = &mode_lib->mp.USRRetrainingSupport;
		CalculateWatermarks_params->temp_read_or_ppt_support = mode_lib->mp.temp_read_or_ppt_support;
		CalculateWatermarks_params->global_temp_read_or_ppt_supported = &mode_lib->mp.global_temp_read_or_ppt_supported;
		CalculateWatermarks_params->VActiveLatencyHidingMargin = NULL;
		CalculateWatermarks_params->VActiveLatencyHidingUs = NULL;

		dcn5_calculate_watermarks_and_dram_speed_change_support(&mode_lib->scratch, CalculateWatermarks_params);

		for (k = 0; k < s->num_active_planes; ++k) {
			if (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].writeback.active_writebacks_per_stream > 0) {
				mode_lib->mp.WritebackAllowDRAMClockChangeEndPosition[k] = math_max2(0, mode_lib->mp.VStartupMin[k] * display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total /
					((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) - mode_lib->mp.Watermark.WritebackDRAMClockChangeWatermark);
				mode_lib->mp.WritebackAllowFCLKChangeEndPosition[k] = math_max2(0, mode_lib->mp.VStartupMin[k] * display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total /
					((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000) - mode_lib->mp.Watermark.WritebackFCLKChangeWatermark);
			} else {
				mode_lib->mp.WritebackAllowDRAMClockChangeEndPosition[k] = 0;
				mode_lib->mp.WritebackAllowFCLKChangeEndPosition[k] = 0;
			}
		}

		dcn5_calculate_pstate_keepout_dst_lines(display_cfg, &mode_lib->mp.Watermark, mode_lib->mp.pstate_keepout_dst_lines);

		DML_LOG_VERBOSE("DML::%s: DEBUG stream_index = %0d\n", __func__, display_cfg->plane_descriptors[0].stream_index);
		DML_LOG_VERBOSE("DML::%s: DEBUG PixelClock = %ld kHz\n", __func__, (display_cfg->stream_descriptors[display_cfg->plane_descriptors[0].stream_index].timing.pixel_clock_khz));

		//Display Pipeline Delivery Time in Prefetch, Groups
		dcn5_calculate_pixel_delivery_times(
			display_cfg,
			mode_lib->mp.NoOfDPP,
			s->num_active_planes,
			mode_lib->mp.VRatioPrefetchY,
			mode_lib->mp.VRatioPrefetchC,
			mode_lib->mp.swath_width_luma_ub,
			mode_lib->mp.swath_width_chroma_ub,
			mode_lib->mp.PSCL_THROUGHPUT,
			mode_lib->mp.PSCL_THROUGHPUT_CHROMA,
			mode_lib->mp.Dppclk,
			mode_lib->mp.DCFCLKDeepSleep,
			mode_lib->mp.BytePerPixelY,
			mode_lib->mp.BytePerPixelC,
			mode_lib->mp.req_per_swath_ub_l,
			mode_lib->mp.req_per_swath_ub_c,

			/* Output */
			mode_lib->mp.DisplayPipeLineDeliveryTimeLuma,
			mode_lib->mp.DisplayPipeLineDeliveryTimeChroma,
			mode_lib->mp.DisplayPipeLineDeliveryTimeLumaPrefetch,
			mode_lib->mp.DisplayPipeLineDeliveryTimeChromaPrefetch,
			mode_lib->mp.DisplayPipeRequestDeliveryTimeLuma,
			mode_lib->mp.DisplayPipeRequestDeliveryTimeChroma,
			mode_lib->mp.DisplayPipeRequestDeliveryTimeLumaPrefetch,
			mode_lib->mp.DisplayPipeRequestDeliveryTimeChromaPrefetch);

		CalculateMetaAndPTETimes_params->scratch = &mode_lib->scratch;
		CalculateMetaAndPTETimes_params->display_cfg = display_cfg;
		CalculateMetaAndPTETimes_params->NumberOfActiveSurfaces = s->num_active_planes;
		CalculateMetaAndPTETimes_params->use_one_row_for_frame = mode_lib->mp.use_one_row_for_frame;
		CalculateMetaAndPTETimes_params->dst_y_per_row_vblank = mode_lib->mp.dst_y_per_row_vblank;
		CalculateMetaAndPTETimes_params->dst_y_per_row_flip = mode_lib->mp.dst_y_per_row_flip;
		CalculateMetaAndPTETimes_params->BytePerPixelY = mode_lib->mp.BytePerPixelY;
		CalculateMetaAndPTETimes_params->BytePerPixelC = mode_lib->mp.BytePerPixelC;
		CalculateMetaAndPTETimes_params->dpte_row_height = mode_lib->mp.dpte_row_height;
		CalculateMetaAndPTETimes_params->dpte_row_height_chroma = mode_lib->mp.dpte_row_height_chroma;
		CalculateMetaAndPTETimes_params->dpte_group_bytes = mode_lib->mp.dpte_group_bytes;
		CalculateMetaAndPTETimes_params->PTERequestSizeY = mode_lib->mp.PTERequestSizeY;
		CalculateMetaAndPTETimes_params->PTERequestSizeC = mode_lib->mp.PTERequestSizeC;
		CalculateMetaAndPTETimes_params->PixelPTEReqWidthY = mode_lib->mp.PixelPTEReqWidthY;
		CalculateMetaAndPTETimes_params->PixelPTEReqHeightY = mode_lib->mp.PixelPTEReqHeightY;
		CalculateMetaAndPTETimes_params->PixelPTEReqWidthC = mode_lib->mp.PixelPTEReqWidthC;
		CalculateMetaAndPTETimes_params->PixelPTEReqHeightC = mode_lib->mp.PixelPTEReqHeightC;
		CalculateMetaAndPTETimes_params->dpte_row_width_luma_ub = mode_lib->mp.dpte_row_width_luma_ub;
		CalculateMetaAndPTETimes_params->dpte_row_width_chroma_ub = mode_lib->mp.dpte_row_width_chroma_ub;
		CalculateMetaAndPTETimes_params->tdlut_groups_per_2row_ub = s->tdlut_groups_per_2row_ub;
		CalculateMetaAndPTETimes_params->mrq_present = mode_lib->ip.dcn_mrq_present;

		CalculateMetaAndPTETimes_params->MetaChunkSize = mode_lib->ip.meta_chunk_size_kbytes;
		CalculateMetaAndPTETimes_params->MinMetaChunkSizeBytes = mode_lib->ip.min_meta_chunk_size_bytes;
		CalculateMetaAndPTETimes_params->meta_row_width = mode_lib->mp.meta_row_width;
		CalculateMetaAndPTETimes_params->meta_row_width_chroma = mode_lib->mp.meta_row_width_chroma;
		CalculateMetaAndPTETimes_params->meta_row_height = mode_lib->mp.meta_row_height;
		CalculateMetaAndPTETimes_params->meta_row_height_chroma = mode_lib->mp.meta_row_height_chroma;
		CalculateMetaAndPTETimes_params->meta_req_width = mode_lib->mp.meta_req_width;
		CalculateMetaAndPTETimes_params->meta_req_width_chroma = mode_lib->mp.meta_req_width_chroma;
		CalculateMetaAndPTETimes_params->meta_req_height = mode_lib->mp.meta_req_height;
		CalculateMetaAndPTETimes_params->meta_req_height_chroma = mode_lib->mp.meta_req_height_chroma;

		CalculateMetaAndPTETimes_params->time_per_tdlut_group = mode_lib->mp.time_per_tdlut_group;
		CalculateMetaAndPTETimes_params->DST_Y_PER_PTE_ROW_NOM_L = mode_lib->mp.DST_Y_PER_PTE_ROW_NOM_L;
		CalculateMetaAndPTETimes_params->DST_Y_PER_PTE_ROW_NOM_C = mode_lib->mp.DST_Y_PER_PTE_ROW_NOM_C;
		CalculateMetaAndPTETimes_params->time_per_pte_group_nom_luma = mode_lib->mp.time_per_pte_group_nom_luma;
		CalculateMetaAndPTETimes_params->time_per_pte_group_vblank_luma = mode_lib->mp.time_per_pte_group_vblank_luma;
		CalculateMetaAndPTETimes_params->time_per_pte_group_flip_luma = mode_lib->mp.time_per_pte_group_flip_luma;
		CalculateMetaAndPTETimes_params->time_per_pte_group_nom_chroma = mode_lib->mp.time_per_pte_group_nom_chroma;
		CalculateMetaAndPTETimes_params->time_per_pte_group_vblank_chroma = mode_lib->mp.time_per_pte_group_vblank_chroma;
		CalculateMetaAndPTETimes_params->time_per_pte_group_flip_chroma = mode_lib->mp.time_per_pte_group_flip_chroma;
		CalculateMetaAndPTETimes_params->DST_Y_PER_META_ROW_NOM_L = mode_lib->mp.DST_Y_PER_META_ROW_NOM_L;
		CalculateMetaAndPTETimes_params->DST_Y_PER_META_ROW_NOM_C = mode_lib->mp.DST_Y_PER_META_ROW_NOM_C;
		CalculateMetaAndPTETimes_params->TimePerMetaChunkNominal = mode_lib->mp.TimePerMetaChunkNominal;
		CalculateMetaAndPTETimes_params->TimePerChromaMetaChunkNominal = mode_lib->mp.TimePerChromaMetaChunkNominal;
		CalculateMetaAndPTETimes_params->TimePerMetaChunkVBlank = mode_lib->mp.TimePerMetaChunkVBlank;
		CalculateMetaAndPTETimes_params->TimePerChromaMetaChunkVBlank = mode_lib->mp.TimePerChromaMetaChunkVBlank;
		CalculateMetaAndPTETimes_params->TimePerMetaChunkFlip = mode_lib->mp.TimePerMetaChunkFlip;
		CalculateMetaAndPTETimes_params->TimePerChromaMetaChunkFlip = mode_lib->mp.TimePerChromaMetaChunkFlip;

		dcn5_calculate_meta_and_pte_times(CalculateMetaAndPTETimes_params);

		dcn5_calculate_vm_group_and_request_times(
			display_cfg,
			s->num_active_planes,
			mode_lib->mp.BytePerPixelC,
			mode_lib->mp.dst_y_per_vm_vblank,
			mode_lib->mp.dst_y_per_vm_flip,
			mode_lib->mp.dpte_row_width_luma_ub,
			mode_lib->mp.dpte_row_width_chroma_ub,
			mode_lib->mp.vm_group_bytes,
			mode_lib->mp.dpde0_bytes_per_frame_ub_l,
			mode_lib->mp.dpde0_bytes_per_frame_ub_c,
			s->tdlut_pte_bytes_per_frame,
			mode_lib->mp.meta_pte_bytes_per_frame_ub_l,
			mode_lib->mp.meta_pte_bytes_per_frame_ub_c,
			mode_lib->ip.dcn_mrq_present,

			/* Output */
			mode_lib->mp.TimePerVMGroupVBlank,
			mode_lib->mp.TimePerVMGroupFlip,
			mode_lib->mp.TimePerVMRequestVBlank,
			mode_lib->mp.TimePerVMRequestFlip);

		// VStartup Adjustment
		for (k = 0; k < s->num_active_planes; ++k) {
			bool isInterlaceTiming;

			mode_lib->mp.MinTTUVBlank[k] = mode_lib->mp.TWait[k] + mode_lib->mp.ExtraLatency;
			if (!display_cfg->plane_descriptors[k].dynamic_meta_data.enable)
				mode_lib->mp.MinTTUVBlank[k] = mode_lib->mp.TCalc + mode_lib->mp.MinTTUVBlank[k];

			DML_LOG_VERBOSE("DML::%s: k=%u, MinTTUVBlank = %f (before vstartup margin)\n", __func__, k, mode_lib->mp.MinTTUVBlank[k]);
			s->Tvstartup_margin = (s->MaxVStartupLines[k] - mode_lib->mp.VStartupMin[k]) * display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);
			mode_lib->mp.MinTTUVBlank[k] = mode_lib->mp.MinTTUVBlank[k] + s->Tvstartup_margin;

			DML_LOG_VERBOSE("DML::%s: k=%u, Tvstartup_margin = %f\n", __func__, k, s->Tvstartup_margin);
			DML_LOG_VERBOSE("DML::%s: k=%u, MaxVStartupLines = %u\n", __func__, k, s->MaxVStartupLines[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, MinTTUVBlank = %f\n", __func__, k, mode_lib->mp.MinTTUVBlank[k]);

			mode_lib->mp.Tdmdl[k] = mode_lib->mp.Tdmdl[k] + s->Tvstartup_margin;
			if (display_cfg->plane_descriptors[k].dynamic_meta_data.enable && mode_lib->ip.dynamic_metadata_vm_enabled) {
				mode_lib->mp.Tdmdl_vm[k] = mode_lib->mp.Tdmdl_vm[k] + s->Tvstartup_margin;
			}

			isInterlaceTiming = (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.interlaced && !mode_lib->ip.ptoi_supported);

			// The actual positioning of the vstartup
			mode_lib->mp.VStartup[k] = (isInterlaceTiming ? (2 * s->MaxVStartupLines[k]) : s->MaxVStartupLines[k]);

			s->dlg_vblank_start = ((isInterlaceTiming ? math_floor2((display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_total - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_front_porch) / 2.0, 1.0) :
				display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_total) - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_front_porch);
			s->LSetup = math_floor2(4.0 * mode_lib->mp.TSetup[k] / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total / ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000)), 1.0) / 4.0;
			s->blank_lines_remaining = (display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_total - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_active) - mode_lib->mp.VStartup[k];

			if (s->blank_lines_remaining < 0) {
				DML_LOG_VERBOSE("ERROR: Vstartup is larger than vblank!?\n");
				s->blank_lines_remaining = 0;
				DML_ASSERT(0);
			}
			mode_lib->mp.MIN_DST_Y_NEXT_START[k] = s->dlg_vblank_start + s->blank_lines_remaining + s->LSetup;

			// debug only
			if (((mode_lib->mp.VUpdateOffsetPix[k] + mode_lib->mp.VUpdateWidthPix[k] + mode_lib->mp.VReadyOffsetPix[k]) / (double) display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total) <=
				(isInterlaceTiming ?
					math_floor2((display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_total - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_active - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_front_porch - mode_lib->mp.VStartup[k]) / 2.0, 1.0) :
					(int)(display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_total - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_active - display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_front_porch - mode_lib->mp.VStartup[k]))) {
				mode_lib->mp.VREADY_AT_OR_AFTER_VSYNC[k] = true;
			} else {
				mode_lib->mp.VREADY_AT_OR_AFTER_VSYNC[k] = false;
			}
			DML_LOG_VERBOSE("DML::%s: k=%u, VStartup = %u (max)\n", __func__, k, mode_lib->mp.VStartup[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, VStartupMin = %u (max)\n", __func__, k, mode_lib->mp.VStartupMin[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, VUpdateOffsetPix = %u\n", __func__, k, mode_lib->mp.VUpdateOffsetPix[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, VUpdateWidthPix = %u\n", __func__, k, mode_lib->mp.VUpdateWidthPix[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, VReadyOffsetPix = %u\n", __func__, k, mode_lib->mp.VReadyOffsetPix[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, HTotal = %lu\n", __func__, k, display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.h_total);
			DML_LOG_VERBOSE("DML::%s: k=%u, VTotal = %lu\n", __func__, k, display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_total);
			DML_LOG_VERBOSE("DML::%s: k=%u, VActive = %lu\n", __func__, k, display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_active);
			DML_LOG_VERBOSE("DML::%s: k=%u, VFrontPorch = %lu\n", __func__, k, display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.v_front_porch);
			DML_LOG_VERBOSE("DML::%s: k=%u, TSetup = %f\n", __func__, k, mode_lib->mp.TSetup[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, MIN_DST_Y_NEXT_START = %f\n", __func__, k, mode_lib->mp.MIN_DST_Y_NEXT_START[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, VREADY_AT_OR_AFTER_VSYNC = %u\n", __func__, k, mode_lib->mp.VREADY_AT_OR_AFTER_VSYNC[k]);
		}

		//Maximum Bandwidth Used
		mode_lib->mp.TotalWRBandwidth = 0;
		for (k = 0; k < display_cfg->num_streams; ++k) {
			for (j = 0; j < display_cfg->stream_descriptors[k].writeback.active_writebacks_per_stream; ++j) {
				double writeback_bytes_per_pixel;
				if (display_cfg->stream_descriptors[k].writeback.writeback_stream[j].pixel_format == dml2_444_64)
					writeback_bytes_per_pixel = 8.0;
				else if (display_cfg->stream_descriptors[k].writeback.writeback_stream[j].pixel_format == dml2_444_32)
					writeback_bytes_per_pixel = 4.0;
				else if (display_cfg->stream_descriptors[k].writeback.writeback_stream[j].pixel_format == dml2_420_8)
					writeback_bytes_per_pixel = 1.5;
				else if (display_cfg->stream_descriptors[k].writeback.writeback_stream[j].pixel_format == dml2_420_10)
					writeback_bytes_per_pixel = 3.0;
				else if (display_cfg->stream_descriptors[k].writeback.writeback_stream[j].pixel_format == dml2_422_packed_8)
					writeback_bytes_per_pixel = 2.0;
				else if (display_cfg->stream_descriptors[k].writeback.writeback_stream[j].pixel_format == dml2_422_packed_10)
					writeback_bytes_per_pixel = 4.0;
				else
					writeback_bytes_per_pixel = 0.0;
				s->WRBandwidth = display_cfg->stream_descriptors[k].writeback.writeback_stream[j].output_height
					* display_cfg->stream_descriptors[k].writeback.writeback_stream[j].output_width
					/ (display_cfg->stream_descriptors[k].timing.h_total
					* display_cfg->stream_descriptors[k].writeback.writeback_stream[j].input_height
					/ ((double)display_cfg->stream_descriptors[k].timing.pixel_clock_khz / 1000)) * writeback_bytes_per_pixel;
				mode_lib->mp.TotalWRBandwidth = mode_lib->mp.TotalWRBandwidth + s->WRBandwidth;
			}
		}

		mode_lib->mp.TotalDataReadBandwidth = 0;
		for (k = 0; k < s->num_active_planes; ++k) {
			mode_lib->mp.TotalDataReadBandwidth = mode_lib->mp.TotalDataReadBandwidth + mode_lib->mp.vactive_sw_bw_l[k] + mode_lib->mp.vactive_sw_bw_c[k];
			DML_LOG_VERBOSE("DML::%s: k=%u, TotalDataReadBandwidth = %f\n", __func__, k, mode_lib->mp.TotalDataReadBandwidth);
			DML_LOG_VERBOSE("DML::%s: k=%u, vactive_sw_bw_l = %f\n", __func__, k, mode_lib->mp.vactive_sw_bw_l[k]);
			DML_LOG_VERBOSE("DML::%s: k=%u, vactive_sw_bw_c = %f\n", __func__, k, mode_lib->mp.vactive_sw_bw_c[k]);
		}

		CalculateStutterEfficiency_params->display_cfg = display_cfg;
		CalculateStutterEfficiency_params->CompressedBufferSizeInkByte = mode_lib->mp.CompressedBufferSizeInkByte;
		CalculateStutterEfficiency_params->UnboundedRequestEnabled = mode_lib->mp.UnboundedRequestEnabled;
		CalculateStutterEfficiency_params->MetaFIFOSizeInKEntries = mode_lib->ip.meta_fifo_size_in_kentries;
		CalculateStutterEfficiency_params->ZeroSizeBufferEntries = mode_lib->ip.zero_size_buffer_entries;
		CalculateStutterEfficiency_params->PixelChunkSizeInKByte = mode_lib->ip.pixel_chunk_size_kbytes;
		CalculateStutterEfficiency_params->NumberOfActiveSurfaces = s->num_active_planes;
		CalculateStutterEfficiency_params->ROBBufferSizeInKByte = mode_lib->ip.rob_buffer_size_kbytes;
		CalculateStutterEfficiency_params->TotalDataReadBandwidth = mode_lib->mp.TotalDataReadBandwidth;
		CalculateStutterEfficiency_params->DCFCLK = mode_lib->mp.Dcfclk;
		CalculateStutterEfficiency_params->ReturnBW = mode_lib->mp.dram_bw_mbps;
		CalculateStutterEfficiency_params->CompbufReservedSpace64B = mode_lib->mp.compbuf_reserved_space_64b;
		CalculateStutterEfficiency_params->CompbufReservedSpaceZs = mode_lib->ip.compbuf_reserved_space_zs;
		CalculateStutterEfficiency_params->SRExitTime = utm_soc_bb->power_management_parameters.stutter_exit_latency_us;
		CalculateStutterEfficiency_params->SRExitZ8Time = utm_soc_bb->power_management_parameters.z8_stutter_exit_latency_us;
		CalculateStutterEfficiency_params->SynchronizeTimings = display_cfg->overrides.synchronize_timings;
		CalculateStutterEfficiency_params->StutterEnterPlusExitWatermark = mode_lib->mp.Watermark.StutterEnterPlusExitWatermark;
		CalculateStutterEfficiency_params->Z8StutterEnterPlusExitWatermark = mode_lib->mp.Watermark.Z8StutterEnterPlusExitWatermark;
		CalculateStutterEfficiency_params->ProgressiveToInterlaceUnitInOPP = mode_lib->ip.ptoi_supported;
		CalculateStutterEfficiency_params->MinTTUVBlank = mode_lib->mp.MinTTUVBlank;
		CalculateStutterEfficiency_params->DPPPerSurface = mode_lib->mp.NoOfDPP;
		CalculateStutterEfficiency_params->DETBufferSizeY = mode_lib->mp.DETBufferSizeY;
		CalculateStutterEfficiency_params->BytePerPixelY = mode_lib->mp.BytePerPixelY;
		CalculateStutterEfficiency_params->BytePerPixelDETY = mode_lib->mp.BytePerPixelInDETY;
		CalculateStutterEfficiency_params->SwathWidthY = mode_lib->mp.SwathWidthY;
		CalculateStutterEfficiency_params->SwathHeightY = mode_lib->mp.SwathHeightY;
		CalculateStutterEfficiency_params->SwathHeightC = mode_lib->mp.SwathHeightC;
		CalculateStutterEfficiency_params->BlockHeight256BytesY = mode_lib->mp.Read256BlockHeightY;
		CalculateStutterEfficiency_params->BlockWidth256BytesY = mode_lib->mp.Read256BlockWidthY;
		CalculateStutterEfficiency_params->BlockHeight256BytesC = mode_lib->mp.Read256BlockHeightC;
		CalculateStutterEfficiency_params->BlockWidth256BytesC = mode_lib->mp.Read256BlockWidthC;
		CalculateStutterEfficiency_params->DCCYMaxUncompressedBlock = mode_lib->mp.DCCYMaxUncompressedBlock;
		CalculateStutterEfficiency_params->DCCCMaxUncompressedBlock = mode_lib->mp.DCCCMaxUncompressedBlock;
		CalculateStutterEfficiency_params->ReadBandwidthSurfaceLuma = mode_lib->mp.vactive_sw_bw_l;
		CalculateStutterEfficiency_params->ReadBandwidthSurfaceChroma = mode_lib->mp.vactive_sw_bw_c;
		CalculateStutterEfficiency_params->dpte_row_bw = mode_lib->mp.dpte_row_bw;
		CalculateStutterEfficiency_params->meta_row_bw = mode_lib->mp.meta_row_bw;
		CalculateStutterEfficiency_params->rob_alloc_compressed = mode_lib->ip.dcn_mrq_present;

		// output
		CalculateStutterEfficiency_params->StutterEfficiencyNotIncludingVBlank = &mode_lib->mp.StutterEfficiencyNotIncludingVBlank;
		CalculateStutterEfficiency_params->StutterEfficiency = &mode_lib->mp.StutterEfficiency;
		CalculateStutterEfficiency_params->NumberOfStutterBurstsPerFrame = &mode_lib->mp.NumberOfStutterBurstsPerFrame;
		CalculateStutterEfficiency_params->Z8StutterEfficiencyNotIncludingVBlank = &mode_lib->mp.Z8StutterEfficiencyNotIncludingVBlank;
		CalculateStutterEfficiency_params->Z8StutterEfficiency = &mode_lib->mp.Z8StutterEfficiency;
		CalculateStutterEfficiency_params->Z8NumberOfStutterBurstsPerFrame = &mode_lib->mp.Z8NumberOfStutterBurstsPerFrame;
		CalculateStutterEfficiency_params->StutterPeriod = &mode_lib->mp.StutterPeriod;
		CalculateStutterEfficiency_params->DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE = &mode_lib->mp.DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE;

		// Stutter Efficiency
		dcn5_calculate_stutter_efficiency(&mode_lib->scratch, CalculateStutterEfficiency_params);

#ifdef __DML_VBA_ALLOW_DELTA__
		// Calculate z8 stutter eff assuming 0 reserved space
		CalculateStutterEfficiency_params->CompbufReservedSpace64B = 0;
		CalculateStutterEfficiency_params->CompbufReservedSpaceZs = 0;

		CalculateStutterEfficiency_params->Z8StutterEfficiencyNotIncludingVBlank = &mode_lib->mp.Z8StutterEfficiencyNotIncludingVBlankBestCase;
		CalculateStutterEfficiency_params->Z8StutterEfficiency = &mode_lib->mp.Z8StutterEfficiencyBestCase;
		CalculateStutterEfficiency_params->Z8NumberOfStutterBurstsPerFrame = &mode_lib->mp.Z8NumberOfStutterBurstsPerFrameBestCase;
		CalculateStutterEfficiency_params->StutterPeriod = &mode_lib->mp.StutterPeriodBestCase;

		// Stutter Efficiency
		dcn5_calculate_stutter_efficiency(&mode_lib->scratch, CalculateStutterEfficiency_params);
#else
		mode_lib->mp.Z8StutterEfficiencyNotIncludingVBlankBestCase = mode_lib->mp.Z8StutterEfficiencyNotIncludingVBlank;
		mode_lib->mp.Z8StutterEfficiencyBestCase = mode_lib->mp.Z8StutterEfficiency;
		mode_lib->mp.Z8NumberOfStutterBurstsPerFrameBestCase = mode_lib->mp.Z8NumberOfStutterBurstsPerFrame;
		mode_lib->mp.StutterPeriodBestCase = mode_lib->mp.StutterPeriod;
#endif
	} // PrefetchAndImmediateFlipSupported

	min_return_latency_in_DCFCLK_cycles = (min_return_uclk_cycles / (max_sop.uclk_khz / 1000.0) + min_return_fclk_cycles / (max_sop.fclk_khz / 1000.0)) * mode_lib->mp.Dcfclk;
	mode_lib->mp.min_return_latency_in_dcfclk = (unsigned int)min_return_latency_in_DCFCLK_cycles;
	mode_lib->mp.dcfclk_deep_sleep_hysteresis = (unsigned int)math_max2(32, (double)mode_lib->ip.pixel_chunk_size_kbytes * 1024 * 3 / 4 / 64 - min_return_latency_in_DCFCLK_cycles);
	DML_ASSERT(mode_lib->mp.dcfclk_deep_sleep_hysteresis < 256);

	DML_LOG_VERBOSE("DML::%s: max_sop.fclk_khz = %d\n", __func__, max_sop.fclk_khz);
	DML_LOG_VERBOSE("DML::%s: max_sop.dcfclk_khz = %d\n", __func__, max_sop.dcfclk_khz);
	DML_LOG_VERBOSE("DML::%s: max_sop.uclk_khz = %d\n", __func__, max_sop.uclk_khz);
	DML_LOG_VERBOSE("DML::%s: min_sop.fclk_khz = %d\n", __func__, min_sop.fclk_khz);
	DML_LOG_VERBOSE("DML::%s: min_sop.dcfclk_khz = %d\n", __func__, min_sop.dcfclk_khz);
	DML_LOG_VERBOSE("DML::%s: min_sop.uclk_khz = %d\n", __func__, min_sop.uclk_khz);
	DML_LOG_VERBOSE("DML::%s: min_return_uclk_cycles = %ld\n", __func__, min_return_uclk_cycles);
	DML_LOG_VERBOSE("DML::%s: min_return_fclk_cycles = %ld\n", __func__, min_return_fclk_cycles);
	DML_LOG_VERBOSE("DML::%s: min_return_latency_in_DCFCLK_cycles = %f\n", __func__, min_return_latency_in_DCFCLK_cycles);
	DML_LOG_VERBOSE("DML::%s: dcfclk_deep_sleep_hysteresis = %d \n", __func__, mode_lib->mp.dcfclk_deep_sleep_hysteresis);
	DML_LOG_VERBOSE("DML::%s: --- END --- \n", __func__);
	return in_out_params->mode_lib->mp.PrefetchAndImmediateFlipSupported;
}

static void dcn5_get_global_sync_programming(const struct dml2_core_internal_display_mode_lib *mode_lib, union dml2_global_sync_programming *out, int pipe_index)
{
	out->dcn4x.vready_offset_pixels = mode_lib->mp.VReadyOffsetPix[mode_lib->mp.pipe_plane[pipe_index]];
	out->dcn4x.vstartup_lines = mode_lib->mp.VStartup[mode_lib->mp.pipe_plane[pipe_index]];
	out->dcn4x.vupdate_offset_pixels = mode_lib->mp.VUpdateOffsetPix[mode_lib->mp.pipe_plane[pipe_index]];
	out->dcn4x.vupdate_vupdate_width_pixels = mode_lib->mp.VUpdateWidthPix[mode_lib->mp.pipe_plane[pipe_index]];
	out->dcn4x.pstate_keepout_start_lines = mode_lib->mp.pstate_keepout_dst_lines[mode_lib->mp.pipe_plane[pipe_index]];
}

static void dcn5_get_stream_programming(const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_per_stream_programming *out, int pipe_index)
{
	dcn5_get_global_sync_programming(mode_lib, &out->global_sync, pipe_index);
}

static void dcn5_populate_fams2_programming(const struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_core_calcs_mode_programming_ex *mode_prog,
		union dmub_cmd_fams2_config *fams2_base_programming,
		union dmub_cmd_fams2_config *fams2_sub_programming,
		enum dml2_pstate_method pstate_method,
		int plane_index)
{
	const struct dml2_display_cfg *disp_cfg = mode_prog->in_display_cfg;
	const struct dml2_plane_parameters *plane_descriptor = &disp_cfg->plane_descriptors[plane_index];
	const struct dml2_stream_parameters *stream_descriptor = &disp_cfg->stream_descriptors[plane_descriptor->stream_index];
	const struct dml2_pstate_meta *stream_pstate_meta = &mode_prog->uclk_params->stream_pstate_meta[plane_descriptor->stream_index];

	struct dmub_fams2_cmd_stream_static_base_state *base_programming = &fams2_base_programming->stream_v1.base;
	union dmub_fams2_cmd_stream_static_sub_state *sub_programming = &fams2_sub_programming->stream_v1.sub_state;

	unsigned int i;

	if (disp_cfg->overrides.all_streams_blanked) {
		/* stream is blanked, so do nothing */
		return;
	}

	/* from display configuration */
	base_programming->htotal = (uint16_t)stream_descriptor->timing.h_total;
	base_programming->vtotal = (uint16_t)stream_descriptor->timing.v_total;
	base_programming->vblank_start = (uint16_t)(stream_pstate_meta->nom_vtotal -
		stream_descriptor->timing.v_front_porch);
	base_programming->vblank_end = (uint16_t)(stream_pstate_meta->nom_vtotal -
		stream_descriptor->timing.v_front_porch -
		stream_descriptor->timing.v_active);
	base_programming->config.bits.is_drr = stream_descriptor->timing.drr_config.enabled;

	/* from meta */
	base_programming->otg_vline_time_ns =
		(unsigned int)(stream_pstate_meta->otg_vline_time_us * 1000.0);
	base_programming->scheduling_delay_otg_vlines = (uint8_t)stream_pstate_meta->scheduling_delay_otg_vlines;
	base_programming->contention_delay_otg_vlines = (uint8_t)stream_pstate_meta->contention_delay_otg_vlines;
	base_programming->vline_int_ack_delay_otg_vlines = (uint8_t)stream_pstate_meta->vertical_interrupt_ack_delay_otg_vlines;
	base_programming->drr_keepout_otg_vline = (uint16_t)(stream_pstate_meta->nom_vtotal -
		stream_descriptor->timing.v_front_porch -
		stream_pstate_meta->method_drr.programming_delay_otg_vlines);
	base_programming->allow_to_target_delay_otg_vlines = (uint8_t)stream_pstate_meta->allow_to_target_delay_otg_vlines;
	base_programming->max_vtotal = (uint16_t)stream_pstate_meta->max_vtotal;

	/* from core */
	base_programming->config.bits.min_ttu_vblank_usable = true;
	for (i = 0; i < disp_cfg->num_planes; i++) {
		/* check if all planes support p-state in blank */
		if (disp_cfg->plane_descriptors[i].stream_index == plane_descriptor->stream_index &&
			mode_lib->mp.MinTTUVBlank[i] <= mode_lib->mp.Watermark.DRAMClockChangeWatermark) {
			base_programming->config.bits.min_ttu_vblank_usable = false;
			break;
		}
	}

	switch (pstate_method) {
	case dml2_pstate_method_vactive:
	case dml2_pstate_method_fw_vactive_drr:
		/* legacy vactive */
		base_programming->type = FAMS2_STREAM_TYPE_VACTIVE;
		sub_programming->legacy.vactive_det_fill_delay_otg_vlines =
			(uint8_t)stream_pstate_meta->method_vactive.max_vactive_det_fill_delay_otg_vlines;
		base_programming->allow_start_otg_vline =
			(uint16_t)stream_pstate_meta->method_vactive.common.allow_start_otg_vline;
		base_programming->allow_end_otg_vline =
			(uint16_t)stream_pstate_meta->method_vactive.common.allow_end_otg_vline;
		base_programming->config.bits.clamp_vtotal_min = true;
		break;
	case dml2_pstate_method_vblank:
	case dml2_pstate_method_fw_vblank_drr:
		/* legacy vblank */
		base_programming->type = FAMS2_STREAM_TYPE_VBLANK;
		base_programming->allow_start_otg_vline =
			(uint16_t)stream_pstate_meta->method_vblank.common.allow_start_otg_vline;
		base_programming->allow_end_otg_vline =
			(uint16_t)stream_pstate_meta->method_vblank.common.allow_end_otg_vline;
		base_programming->config.bits.clamp_vtotal_min = true;
		break;
	case dml2_pstate_method_fw_drr:
		/* drr */
		base_programming->type = FAMS2_STREAM_TYPE_DRR;
		sub_programming->drr.programming_delay_otg_vlines =
			(uint8_t)stream_pstate_meta->method_drr.programming_delay_otg_vlines;
		sub_programming->drr.nom_stretched_vtotal =
			(uint16_t)stream_pstate_meta->method_drr.stretched_vtotal;
		base_programming->allow_start_otg_vline =
			(uint16_t)stream_pstate_meta->method_drr.common.allow_start_otg_vline;
		base_programming->allow_end_otg_vline =
			(uint16_t)stream_pstate_meta->method_drr.common.allow_end_otg_vline;
		/* drr only clamps to vtotal min for single display */
		base_programming->config.bits.clamp_vtotal_min = disp_cfg->num_streams == 1;
		sub_programming->drr.only_stretch_if_required = true;
		break;
	case dml2_pstate_method_fw_svp:
	case dml2_pstate_method_fw_svp_drr:
	case dml2_pstate_method_alternate:
	case dml2_pstate_method_reserved_hw:
	case dml2_pstate_method_reserved_fw:
	case dml2_pstate_method_reserved_fw_drr_clamped:
	case dml2_pstate_method_reserved_fw_drr_var:
	case dml2_pstate_method_na:
	case dml2_pstate_method_count:
	default:
		/* this should never happen */
		break;
	}
}

static void dcn5_populate_global_fams2_programming(struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_core_calcs_mode_programming_ex *mode_prog,
		struct dmub_cmd_fams2_global_config *fams2_global_config)
{
	fams2_global_config->features.bits.enable = mode_prog->uclk_params->fams2_required;

	if (fams2_global_config->features.bits.enable) {
		fams2_global_config->features.bits.enable_stall_recovery = true;
		fams2_global_config->features.bits.allow_delay_check_mode = FAMS2_ALLOW_DELAY_CHECK_FROM_START;

		fams2_global_config->max_allow_delay_us = mode_lib->ip_caps.fams2.max_allow_delay_us;
		fams2_global_config->lock_wait_time_us = mode_lib->ip_caps.fams2.lock_timeout_us;
		fams2_global_config->recovery_timeout_us = mode_lib->ip_caps.fams2.recovery_timeout_us;
		fams2_global_config->hwfq_flip_programming_delay_us = mode_lib->ip_caps.fams2.flip_programming_delay_us;

		fams2_global_config->num_streams = mode_prog->in_display_cfg->num_streams;
	}
}

static void dcn5_populate_mcache_allocation(struct dml2_display_cfg_programming *programming,
	const struct dml2_display_solution *solution)
{
	unsigned int i;

	for (i = 0; i < solution->dispcfg.num_planes; i++)
		programming->plane_programming[i].mcache_allocation = solution->mcache_allocations[i];
}

static void dcn5_pack_mode_programming_params(
		struct dml2_core_instance *core,
		struct dml2_core_calcs_mode_programming_ex *mode_prog,
		struct dml2_display_cfg_programming *programming)
{
	struct dml2_core_internal_display_mode_lib *mode_lib = mode_prog->mode_lib;
	const struct dml2_utm_soc_bb *utm_soc_bb = core->utm_soc_bb;
	unsigned int pipe_offset;
	int dml_internal_pipe_index;
	int total_pipe_regs_copied = 0;
	int total_dwb_regs_copied = 0;
	int stream_already_populated_mask = 0;

	int main_stream_index;
	unsigned int plane_index;
	unsigned int dwb_index;

	memcpy(&programming->display_config, mode_prog->in_display_cfg, sizeof(struct dml2_display_cfg));

	dcn5_get_arb_params(&programming->display_config, mode_lib, utm_soc_bb, &programming->global_regs.arb_regs);
	programming->global_regs.num_watermark_sets = 1;
	dcn5_get_watermarks(&programming->display_config, mode_lib, utm_soc_bb, &programming->global_regs.wm_regs[0]);
	dcn5_populate_mcache_allocation(programming, mode_prog->solution);

	dcn5_get_mcif_arb_params(mode_lib, &programming->mcif_global_regs);
	programming->mcif_global_regs.num_watermark_sets = 1;

	dml_internal_pipe_index = 0;

	if (mode_prog->uclk_params) {
		if (programming->uclk_pstate_supported) {
			programming->fams2_required = mode_prog->uclk_params->fams2_required;
			dcn5_populate_global_fams2_programming(mode_lib, mode_prog, &programming->fams2_global_config);
		}
	}

	DML_LOG_VERBOSE("DML::%s: num_planes=%d\n", __func__, programming->display_config.num_planes);

	for (plane_index = 0; plane_index < programming->display_config.num_planes; plane_index++) {
		programming->plane_programming[plane_index].num_dpps_required = mode_lib->mp.NoOfDPP[plane_index];

		// Setup the appropriate p-state strategy for each plane
		if (mode_prog->uclk_params) {
			if (programming->uclk_pstate_supported)
				programming->plane_programming[plane_index].uclk_pstate_support_method = mode_prog->uclk_params->pstate_switch_modes[plane_index];
			else
				programming->plane_programming[plane_index].uclk_pstate_support_method = dml2_pstate_method_na;
		} else {
			if (mode_lib->mp.MaxActiveDRAMClockChangeLatencySupported[plane_index] >= utm_soc_bb->power_management_parameters.dram_clk_change_blackout_us)
				programming->plane_programming[plane_index].uclk_pstate_support_method = dml2_pstate_method_vactive;
			else if (mode_lib->mp.TWait[plane_index] >= utm_soc_bb->power_management_parameters.dram_clk_change_blackout_us)
				programming->plane_programming[plane_index].uclk_pstate_support_method = dml2_pstate_method_vblank;
			else
				programming->plane_programming[plane_index].uclk_pstate_support_method = dml2_pstate_method_na;
		}

		for (pipe_offset = 0; pipe_offset < programming->plane_programming[plane_index].num_dpps_required; pipe_offset++) {
			programming->plane_programming[plane_index].plane_descriptor = &programming->display_config.plane_descriptors[plane_index];

			// Assign storage for this pipe's register values
			programming->plane_programming[plane_index].pipe_regs[pipe_offset] = &programming->pipe_regs[total_pipe_regs_copied];
			memset(programming->plane_programming[plane_index].pipe_regs[pipe_offset], 0, sizeof(struct dml2_dchub_per_pipe_register_set));
			total_pipe_regs_copied++;

			// Populate
			dcn5_get_pipe_regs(&programming->display_config, mode_lib, programming->plane_programming[plane_index].pipe_regs[pipe_offset], dml_internal_pipe_index, utm_soc_bb, &mode_lib->scratch);

			main_stream_index = programming->display_config.plane_descriptors[plane_index].stream_index;

			// Multiple planes can refer to the same stream index, so it's only necessary to populate it once
			if (!(stream_already_populated_mask & (0x1 << main_stream_index))) {
				programming->stream_programming[main_stream_index].uclk_pstate_method = programming->plane_programming[plane_index].uclk_pstate_support_method;
				programming->stream_programming[main_stream_index].stream_descriptor = &programming->display_config.stream_descriptors[main_stream_index];
				programming->stream_programming[main_stream_index].num_odms_required = mode_prog->cfg_support_info->stream_support_info[main_stream_index].odms_used;
				dcn5_get_stream_programming(mode_lib, &programming->stream_programming[main_stream_index], dml_internal_pipe_index);

				if (mode_prog->uclk_params) {
					dcn5_populate_fams2_programming(mode_lib,
							mode_prog,
							&programming->stream_programming[main_stream_index].fams2_base_params,
							&programming->stream_programming[main_stream_index].fams2_sub_params,
							programming->stream_programming[main_stream_index].uclk_pstate_method,
							plane_index);
				}

				/* populate DWB */
				for (dwb_index = 0; dwb_index < programming->display_config.stream_descriptors[main_stream_index].writeback.active_writebacks_per_stream; dwb_index++) {
					programming->stream_programming[main_stream_index].mcif_regs[dwb_index] = &programming->mcif_regs[total_dwb_regs_copied];
					memset(programming->stream_programming[main_stream_index].mcif_regs[dwb_index], 0, sizeof(struct dml2_mcif_per_pipe_register_set));
					total_dwb_regs_copied++;

					dcn5_get_per_dwb_params(&programming->display_config,
							mode_lib,
							programming->stream_programming[main_stream_index].mcif_regs[dwb_index],
							main_stream_index,
							dwb_index);
				}

				stream_already_populated_mask |= (0x1 << main_stream_index);
			}
			dml_internal_pipe_index++;
		}
	}

	// DCN5: Calculate reduced viewport pstate recout reduction lines per plane
	// Complete the formula calculation using memory clock and base calculation from prefetch schedule

	// Initialize all planes to default value
	for (unsigned int plane_idx = 0; plane_idx < DML2_MAX_PLANES; plane_idx++) {
		programming->informative.misc.pstate_recout_reduction_lines[plane_idx] = 0;
	}

	// DCN5: Calculate pstate recout reduction lines feature
	// Get DRAM clock change latency from SOC parameters
	double dram_clk_change_latency_us = utm_soc_bb->power_management_parameters.dram_clk_change_blackout_us;

	if (programming->display_config.num_planes == 1) {
		// Calculate for all planes (inactive planes should naturally yield 0)
		for (unsigned int plane_idx = 0; plane_idx < programming->display_config.num_planes; plane_idx++) {
			// Use original equation from specifications
			// Formula: (Tmclk â€“ (Tvstartup â€“ (Tsetup + Tcalc + Tpre + Tout))) / LineTime
			// Where Tpre = dst_y_prefetch * line_time (as specified)

			int stream_index = programming->display_config.plane_descriptors[plane_idx].stream_index;

			// Calculate line time from display config timing (HTotal / PixelClock)
			double pixel_clock_mhz = (double)programming->display_config.stream_descriptors[stream_index].timing.pixel_clock_khz / 1000.0;
			double htotal = (double)programming->display_config.stream_descriptors[stream_index].timing.h_total;
			double line_time_us = htotal / pixel_clock_mhz;

			// Use actual calculated values from mode library (not hardcoded defaults)
			double tmclk_us = dram_clk_change_latency_us;
			double tvstartup_us = mode_lib->mp.VStartup[plane_idx] * line_time_us;
			double tsetup_us = mode_lib->mp.TSetup[plane_idx];
			double tcalc_us = mode_lib->mp.TCalc;
			double tpre_us = mode_lib->mp.dst_y_prefetch[plane_idx] * line_time_us; // As specified: dst_y_prefetch * line_time
			double tout_us = (mode_lib->mp.DSTYAfterScaler[plane_idx] +
				((double)mode_lib->mp.DSTXAfterScaler[plane_idx] / htotal)) * line_time_us; // delay_after_scaler per spec

			// Formula: (Tmclk - (Tvstartup - (Tsetup + Tcalc + Tpre + Tout))) / LineTime
			programming->informative.misc.pstate_recout_reduction_lines[plane_idx] =
				(unsigned int) math_max2(0.0, (tmclk_us - (tvstartup_us - (tsetup_us + tcalc_us + tpre_us + tout_us))) / line_time_us);
		}
	}
}

static void dcn5_mp_assign_qos_bound(struct dml2_core_instance *core,
		struct dml2_display_cfg_programming *programming)
{
	struct dml2_core_internal_display_mode_lib *mode_lib = &core->clean_me_up.mode_lib;
	struct dml2_core_calcs_mode_programming_locals *s = &mode_lib->scratch.dml_core_mode_programming_locals;

	mode_lib->mp.Dcfclk = programming->min_clocks.dcn4x.active.dcfclk_khz / 1000.0;
	mode_lib->mp.FabricClock = programming->min_clocks.dcn4x.active.fclk_khz / 1000.0;
	mode_lib->mp.uclk_freq_mhz = programming->min_clocks.dcn4x.active.uclk_khz / 1000.0;
	mode_lib->mp.dram_bw_mbps = programming->qos_bound.bandwidth_lb.dcn5.urgent_bandwidth_kbps / 1000.0;
	mode_lib->mp.urg_bandwidth_available[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_dram] = mode_lib->mp.dram_bw_mbps;
	mode_lib->mp.UrgentLatency = programming->qos_bound.latency_ub.dcn5.urgent_ramp;
	mode_lib->mp.TripToMemory = programming->qos_bound.latency_ub.dcn5.t_trip;
	mode_lib->mp.MetaTripToMemory = programming->qos_bound.latency_ub.dcn5.meta_trip_to_mem;
	s->mmSOCParameters.max_urgent_latency_us = programming->qos_bound.latency_ub.dcn5.max_req_latency_urg;
	s->mmSOCParameters.df_response_time_us = programming->qos_bound.latency_ub.dcn5.df_response_time_us;

	DML_LOG_VERBOSE("DML::%s: Dcfclk = %f\n", __func__, mode_lib->mp.Dcfclk);
	DML_LOG_VERBOSE("DML::%s: FabricClock = %f\n", __func__, mode_lib->mp.FabricClock);
	DML_LOG_VERBOSE("DML::%s: uclk_freq_mhz = %f\n", __func__, mode_lib->mp.uclk_freq_mhz);
	DML_LOG_VERBOSE("DML::%s: dram_bw_mbps = %f\n", __func__, mode_lib->mp.dram_bw_mbps);
	DML_LOG_VERBOSE("DML::%s: UrgentLatency (urgent ramp) = %f\n", __func__, programming->qos_bound.latency_ub.dcn5.urgent_ramp);
	DML_LOG_VERBOSE("DML::%s: TripToMemory = %f\n", __func__, mode_lib->mp.TripToMemory);
	DML_LOG_VERBOSE("DML::%s: max_urgent_latency_us = %f\n", __func__, s->mmSOCParameters.max_urgent_latency_us);
	DML_LOG_VERBOSE("DML::%s: GlobalDPPCLK = %f\n", __func__, mode_lib->mp.GlobalDPPCLK);
}

enum dml2_status dml2_core_dcn5_funcs_populate_programming(struct dml2_core_instance *core,
		const struct dml2_display_solution *solution,
		struct dml2_display_cfg_programming *programming)
{
	struct dml2_core_calcs_mode_programming_ex *params = &core->scratch.mode_programming_locals.mode_programming_ex_params;
	struct dml2_core_internal_display_mode_lib *mode_lib = &core->clean_me_up.mode_lib;
	bool result;

	memset(&mode_lib->scratch, 0, sizeof(struct dml2_core_internal_scratch));
	memset(&mode_lib->mp, 0, sizeof(struct dml2_core_internal_mode_program));

	dcn5_mp_assign_qos_bound(core, programming);
	params->mode_lib = mode_lib;
	params->in_display_cfg = &solution->dispcfg;
	params->cfg_support_info = &solution->validation_result.mode_support.cfg_support_info;
	params->programming = programming;
	params->uclk_params = &solution->uclk_pstate_params;
	params->utm_soc_bb = core->utm_soc_bb;
	params->solution = solution;
	result = dcn5_mode_programming(params);
	if (result)
		dcn5_pack_mode_programming_params(core, params, programming);

	return result ? DML2_STATUS_OK : DML2_STATUS_POPULATE_FAIL_PROGRAMMING;
}
