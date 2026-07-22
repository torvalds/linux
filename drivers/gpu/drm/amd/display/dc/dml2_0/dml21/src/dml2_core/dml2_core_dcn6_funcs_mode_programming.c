// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.
#include "dml2_core_dcn6_funcs_mode_programming.h"
#include "dml2_core_dcn5_calcs_dchub.h"
#include "dml2_core_dcn6_calcs_dchub.h"
#include "dml2_core_dcn5_calcs_display_pipe.h"
#include "dml2_core_utils.h"

static void dcn6_mp_populate_odm_mode(const struct dml2_display_solution *solution,
		struct dml2_core_internal_mode_program *outputs)
{
	unsigned int k;
	const struct core_display_cfg_support_info *cfg_support_info =
			&solution->validation_result.mode_support.cfg_support_info;
	const struct dml2_display_cfg *display_cfg = &solution->dispcfg;
	unsigned int stream_index;

	for (k = 0; k < solution->dispcfg.num_planes; ++k) {
		stream_index = display_cfg->plane_descriptors[k].stream_index;
		DML_ASSERT(cfg_support_info->stream_support_info[stream_index].odms_used <= 4);
		DML_ASSERT(cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 4
				|| cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 2
				|| cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 1);
		if (cfg_support_info->stream_support_info[stream_index].odms_used > 1)
			DML_ASSERT(cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 1);

		switch (cfg_support_info->stream_support_info[stream_index].odms_used) {
		case (4):
			outputs->ODMMode[k] = dml2_odm_mode_combine_4to1;
			break;
		case (3):
			outputs->ODMMode[k] = dml2_odm_mode_combine_3to1;
			break;
		case (2):
			outputs->ODMMode[k] = dml2_odm_mode_combine_2to1;
			break;
		default:
			if (cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 4)
				outputs->ODMMode[k] = dml2_odm_mode_mso_1to4;
			else if (cfg_support_info->stream_support_info[stream_index].num_odm_output_segments == 2)
				outputs->ODMMode[k] = dml2_odm_mode_mso_1to2;
			else
				outputs->ODMMode[k] = dml2_odm_mode_bypass;

			break;
		}
	}
}

static void dcn6_mp_calculate_dcc_configurations(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_core_internal_mode_program *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_program *inputs = states;
	struct dml2_core_internal_mode_program *outputs = states;
	unsigned int k;

	for (k = 0; k < display_cfg->num_planes; ++k) {
		DML_LOG_VERBOSE("DML::%s: Calculate DCC configuration for surface k=%u\n", __func__, k);
		dcn5_calculate_dcc_configuration(
				display_cfg->plane_descriptors[k].surface.dcc.enable,
				display_cfg->overrides.dcc_programming_assumes_scan_direction_unknown,
				display_cfg->plane_descriptors[k].pixel_format,
				display_cfg->plane_descriptors[k].surface.plane0.width,
				display_cfg->plane_descriptors[k].surface.plane1.width,
				display_cfg->plane_descriptors[k].surface.plane0.height,
				display_cfg->plane_descriptors[k].surface.plane1.height,
				inputs->NomDETInKByte,
				inputs->Read256BlockHeightY[k],
				inputs->Read256BlockHeightC[k],
				display_cfg->plane_descriptors[k].surface.tiling,
				inputs->BytePerPixelY[k],
				inputs->BytePerPixelC[k],
				inputs->BytePerPixelInDETY[k],
				inputs->BytePerPixelInDETC[k],
				display_cfg->plane_descriptors[k].composition.rotation_angle,

				/* Output */
				&outputs->RequestLuma[k],
				&outputs->RequestChroma[k],
				&outputs->DCCYMaxUncompressedBlock[k],
				&outputs->DCCCMaxUncompressedBlock[k],
				&outputs->DCCYMaxCompressedBlock[k],
				&outputs->DCCCMaxCompressedBlock[k],
				&outputs->DCCYIndependentBlock[k],
				&outputs->DCCCIndependentBlock[k]);
	}
}

static void dcn6_mp_calculate_pixel_delivery_times(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_core_internal_mode_program *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_program *inputs = states;
	struct dml2_core_internal_mode_program *outputs = states;

	//Display Pipeline Delivery Time in Prefetch, Groups
	dcn5_calculate_pixel_delivery_times(
			display_cfg,
			inputs->NoOfDPP,
			display_cfg->num_planes,
			inputs->VRatioPrefetchY,
			inputs->VRatioPrefetchC,
			inputs->swath_width_luma_ub,
			inputs->swath_width_chroma_ub,
			inputs->PSCL_THROUGHPUT,
			inputs->PSCL_THROUGHPUT_CHROMA,
			inputs->Dppclk,
			inputs->DCFCLKDeepSleep,
			inputs->BytePerPixelY,
			inputs->BytePerPixelC,
			inputs->req_per_swath_ub_l,
			inputs->req_per_swath_ub_c,

			/* Output */
			outputs->DisplayPipeLineDeliveryTimeLuma,
			outputs->DisplayPipeLineDeliveryTimeChroma,
			outputs->DisplayPipeLineDeliveryTimeLumaPrefetch,
			outputs->DisplayPipeLineDeliveryTimeChromaPrefetch,
			outputs->DisplayPipeRequestDeliveryTimeLuma,
			outputs->DisplayPipeRequestDeliveryTimeChroma,
			outputs->DisplayPipeRequestDeliveryTimeLumaPrefetch,
			outputs->DisplayPipeRequestDeliveryTimeChromaPrefetch);
}

static void dcn6_mp_calculate_meta_and_pte_times(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_core_internal_mode_program *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_scratch *func_params = ctx->func_params;
	struct dml2_core_internal_mode_program *inputs = states;
	struct dml2_core_internal_mode_program *outputs = states;
	struct dml2_core_shared_CalculateMetaAndPTETimes_params *p = &func_params->CalculateMetaAndPTETimes_params;

	p->scratch = func_params;
	p->display_cfg = display_cfg;
	p->NumberOfActiveSurfaces = display_cfg->num_planes;
	p->use_one_row_for_frame = inputs->use_one_row_for_frame;
	p->dst_y_per_row_vblank = inputs->dst_y_per_row_vblank;
	p->dst_y_per_row_flip = inputs->dst_y_per_row_flip;
	p->BytePerPixelY = inputs->BytePerPixelY;
	p->BytePerPixelC = inputs->BytePerPixelC;
	p->dpte_row_height = inputs->dpte_row_height;
	p->dpte_row_height_chroma = inputs->dpte_row_height_chroma;
	p->dpte_group_bytes = inputs->dpte_group_bytes;
	p->PTERequestSizeY = inputs->PTERequestSizeY;
	p->PTERequestSizeC = inputs->PTERequestSizeC;
	p->PixelPTEReqWidthY = inputs->PixelPTEReqWidthY;
	p->PixelPTEReqHeightY = inputs->PixelPTEReqHeightY;
	p->PixelPTEReqWidthC = inputs->PixelPTEReqWidthC;
	p->PixelPTEReqHeightC = inputs->PixelPTEReqHeightC;
	p->dpte_row_width_luma_ub = inputs->dpte_row_width_luma_ub;
	p->dpte_row_width_chroma_ub = inputs->dpte_row_width_chroma_ub;
	p->tdlut_groups_per_2row_ub = inputs->tdlut_groups_per_2row_ub;
	p->mrq_present = ip->dcn_mrq_present;

	p->MetaChunkSize = ip->meta_chunk_size_kbytes;
	p->MinMetaChunkSizeBytes = ip->min_meta_chunk_size_bytes;
	p->meta_row_width = inputs->meta_row_width;
	p->meta_row_width_chroma = inputs->meta_row_width_chroma;
	p->meta_row_height = inputs->meta_row_height;
	p->meta_row_height_chroma = inputs->meta_row_height_chroma;
	p->meta_req_width = inputs->meta_req_width;
	p->meta_req_width_chroma = inputs->meta_req_width_chroma;
	p->meta_req_height = inputs->meta_req_height;
	p->meta_req_height_chroma = inputs->meta_req_height_chroma;

	p->time_per_tdlut_group = outputs->time_per_tdlut_group;
	p->DST_Y_PER_PTE_ROW_NOM_L = outputs->DST_Y_PER_PTE_ROW_NOM_L;
	p->DST_Y_PER_PTE_ROW_NOM_C = outputs->DST_Y_PER_PTE_ROW_NOM_C;
	p->time_per_pte_group_nom_luma = outputs->time_per_pte_group_nom_luma;
	p->time_per_pte_group_vblank_luma = outputs->time_per_pte_group_vblank_luma;
	p->time_per_pte_group_flip_luma = outputs->time_per_pte_group_flip_luma;
	p->time_per_pte_group_nom_chroma = outputs->time_per_pte_group_nom_chroma;
	p->time_per_pte_group_vblank_chroma = outputs->time_per_pte_group_vblank_chroma;
	p->time_per_pte_group_flip_chroma = outputs->time_per_pte_group_flip_chroma;
	p->DST_Y_PER_META_ROW_NOM_L = outputs->DST_Y_PER_META_ROW_NOM_L;
	p->DST_Y_PER_META_ROW_NOM_C = outputs->DST_Y_PER_META_ROW_NOM_C;
	p->TimePerMetaChunkNominal = outputs->TimePerMetaChunkNominal;
	p->TimePerChromaMetaChunkNominal = outputs->TimePerChromaMetaChunkNominal;
	p->TimePerMetaChunkVBlank = outputs->TimePerMetaChunkVBlank;
	p->TimePerChromaMetaChunkVBlank = outputs->TimePerChromaMetaChunkVBlank;
	p->TimePerMetaChunkFlip = outputs->TimePerMetaChunkFlip;
	p->TimePerChromaMetaChunkFlip = outputs->TimePerChromaMetaChunkFlip;

	dcn5_calculate_meta_and_pte_times(p);
}

static void dcn6_mp_calculate_vm_group_and_request_times(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_core_internal_mode_program *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_program *inputs = states;
	struct dml2_core_internal_mode_program *outputs = states;

	dcn5_calculate_vm_group_and_request_times(
		display_cfg,
		display_cfg->num_planes,
		inputs->BytePerPixelC,
		inputs->dst_y_per_vm_vblank,
		inputs->dst_y_per_vm_flip,
		inputs->dpte_row_width_luma_ub,
		inputs->dpte_row_width_chroma_ub,
		inputs->vm_group_bytes,
		inputs->dpde0_bytes_per_frame_ub_l,
		inputs->dpde0_bytes_per_frame_ub_c,
		inputs->tdlut_pte_bytes_per_frame,
		inputs->meta_pte_bytes_per_frame_ub_l,
		inputs->meta_pte_bytes_per_frame_ub_c,
		ip->dcn_mrq_present,

		/* Output */
		outputs->TimePerVMGroupVBlank,
		outputs->TimePerVMGroupFlip,
		outputs->TimePerVMRequestVBlank,
		outputs->TimePerVMRequestFlip);
}

static void dcn6_mp_calculate_vstartup_adjustment(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_core_internal_mode_program *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	struct dml2_core_internal_mode_program *inputs = states;
	struct dml2_core_internal_mode_program *outputs = states;
	unsigned int k;
	bool isInterlaceTiming;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;
	double Tvstartup_margin;
	double dlg_vblank_start;
	double LSetup;
	double blank_lines_remaining;

	// VStartup Adjustment
	for (k = 0; k < display_cfg->num_planes; ++k) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];
		isInterlaceTiming = (stream->timing.interlaced && !ip->ptoi_supported);

		outputs->MinTTUVBlank[k] = inputs->TWait[k] + inputs->ExtraLatency;
		if (!display_cfg->plane_descriptors[k].dynamic_meta_data.enable)
			outputs->MinTTUVBlank[k] = inputs->TCalc + outputs->MinTTUVBlank[k];

		DML_LOG_VERBOSE("DML::%s: k=%u, MinTTUVBlank = %f (before vstartup margin)\n",
				__func__, k, outputs->MinTTUVBlank[k]);
		Tvstartup_margin = (inputs->MaxVStartupLines[k] - inputs->VStartupMin[k])
				* stream->timing.h_total
				/ ((double)stream->timing.pixel_clock_khz / 1000);
		outputs->MinTTUVBlank[k] = outputs->MinTTUVBlank[k] + Tvstartup_margin;

		DML_LOG_VERBOSE("DML::%s: k=%u, Tvstartup_margin = %f\n", __func__, k, Tvstartup_margin);
		DML_LOG_VERBOSE("DML::%s: k=%u, MaxVStartupLines = %u\n", __func__, k, inputs->MaxVStartupLines[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, MinTTUVBlank = %f\n", __func__, k, outputs->MinTTUVBlank[k]);

		outputs->Tdmdl[k] = inputs->Tdmdl_raw[k] + Tvstartup_margin;
		if (plane->dynamic_meta_data.enable && ip->dynamic_metadata_vm_enabled)
			outputs->Tdmdl_vm[k] = inputs->Tdmdl_vm_raw[k] + Tvstartup_margin;
		else
			outputs->Tdmdl_vm[k] = inputs->Tdmdl_vm_raw[k];

		// The actual positioning of the vstartup
		outputs->VStartup[k] = (isInterlaceTiming ?
				(2 * inputs->MaxVStartupLines[k]) : inputs->MaxVStartupLines[k]);

		dlg_vblank_start =
				((isInterlaceTiming ?
						math_floor2((stream->timing.v_total - stream->timing.v_front_porch) / 2.0, 1.0) :
						stream->timing.v_total)
				- stream->timing.v_front_porch);
		LSetup = math_floor2(
				4.0 * inputs->TSetup[k]
						/ ((double)stream->timing.h_total
								/ ((double)stream->timing.pixel_clock_khz / 1000)),
				1.0) / 4.0;
		blank_lines_remaining = (stream->timing.v_total - stream->timing.v_active) - outputs->VStartup[k];

		if (blank_lines_remaining < 0) {
			DML_LOG_VERBOSE("ERROR: Vstartup is larger than vblank!?\n");
			blank_lines_remaining = 0;
			DML_ASSERT(0);
		}
		outputs->MIN_DST_Y_NEXT_START[k] = dlg_vblank_start + blank_lines_remaining + LSetup;

		// debug only
		if (((inputs->VUpdateOffsetPix[k] + inputs->VUpdateWidthPix[k] + inputs->VReadyOffsetPix[k]) / (double) stream->timing.h_total)
				<= (isInterlaceTiming ?
						math_floor2((stream->timing.v_total - stream->timing.v_active - stream->timing.v_front_porch - outputs->VStartup[k]) / 2.0, 1.0) :
						(int)(stream->timing.v_total - stream->timing.v_active - stream->timing.v_front_porch - outputs->VStartup[k]))) {
			outputs->VREADY_AT_OR_AFTER_VSYNC[k] = true;
		} else {
			outputs->VREADY_AT_OR_AFTER_VSYNC[k] = false;
		}
		DML_LOG_VERBOSE("DML::%s: k=%u, VStartup = %u (max)\n", __func__, k, outputs->VStartup[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, VStartupMin = %u (max)\n", __func__, k, inputs->VStartupMin[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, VUpdateOffsetPix = %u\n", __func__, k, inputs->VUpdateOffsetPix[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, VUpdateWidthPix = %u\n", __func__, k, inputs->VUpdateWidthPix[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, VReadyOffsetPix = %u\n", __func__, k, inputs->VReadyOffsetPix[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, HTotal = %lu\n", __func__, k, stream->timing.h_total);
		DML_LOG_VERBOSE("DML::%s: k=%u, VTotal = %lu\n", __func__, k, stream->timing.v_total);
		DML_LOG_VERBOSE("DML::%s: k=%u, VActive = %lu\n", __func__, k, stream->timing.v_active);
		DML_LOG_VERBOSE("DML::%s: k=%u, VFrontPorch = %lu\n", __func__, k, stream->timing.v_front_porch);
		DML_LOG_VERBOSE("DML::%s: k=%u, TSetup = %f\n", __func__, k, inputs->TSetup[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, MIN_DST_Y_NEXT_START = %f\n",
				__func__, k, outputs->MIN_DST_Y_NEXT_START[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, VREADY_AT_OR_AFTER_VSYNC = %u\n",
				__func__, k, (unsigned char)outputs->VREADY_AT_OR_AFTER_VSYNC[k]);
	}
}

static void dcn6_mp_calculate_max_bandwidth_used(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_core_internal_mode_program *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_program *inputs = states;
	struct dml2_core_internal_mode_program *outputs = states;
	unsigned int j, k;
	const struct dml2_stream_parameters *stream;
	double WRBandwidth;

	outputs->TotalWRBandwidth = 0;
	for (k = 0; k < display_cfg->num_streams; ++k) {
		stream = &display_cfg->stream_descriptors[k];

		for (j = 0; j < stream->writeback.active_writebacks_per_stream; ++j) {
			double writeback_bytes_per_pixel;
			if (stream->writeback.writeback_stream[j].pixel_format == dml2_444_64)
				writeback_bytes_per_pixel = 8.0;
			else if (stream->writeback.writeback_stream[j].pixel_format == dml2_444_32)
				writeback_bytes_per_pixel = 4.0;
			else if (stream->writeback.writeback_stream[j].pixel_format == dml2_420_8)
				writeback_bytes_per_pixel = 1.5;
			else if (stream->writeback.writeback_stream[j].pixel_format == dml2_420_10)
				writeback_bytes_per_pixel = 3.0;
			else if (stream->writeback.writeback_stream[j].pixel_format == dml2_422_packed_8)
				writeback_bytes_per_pixel = 2.0;
			else if (stream->writeback.writeback_stream[j].pixel_format == dml2_422_packed_10)
				writeback_bytes_per_pixel = 4.0;
			else
				writeback_bytes_per_pixel = 0.0;
			WRBandwidth = stream->writeback.writeback_stream[j].output_height
					* stream->writeback.writeback_stream[j].output_width
					/ (stream->timing.h_total
							* stream->writeback.writeback_stream[j].input_height
							/ ((double)stream->timing.pixel_clock_khz / 1000))
					* writeback_bytes_per_pixel;
			outputs->TotalWRBandwidth = outputs->TotalWRBandwidth + WRBandwidth;
		}
	}

	outputs->TotalDataReadBandwidth = 0;
	for (k = 0; k < display_cfg->num_planes; ++k) {
		outputs->TotalDataReadBandwidth =
				outputs->TotalDataReadBandwidth
				+ inputs->vactive_sw_bw_l[k]
				+ inputs->vactive_sw_bw_c[k];
		DML_LOG_VERBOSE("DML::%s: k=%u, TotalDataReadBandwidth = %f\n",
				__func__, k, outputs->TotalDataReadBandwidth);
		DML_LOG_VERBOSE("DML::%s: k=%u, vactive_sw_bw_l = %f\n", __func__, k, inputs->vactive_sw_bw_l[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u, vactive_sw_bw_c = %f\n", __func__, k, inputs->vactive_sw_bw_c[k]);
	}
}

static void dcn6_mp_calculate_stutter_efficiency(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_core_internal_mode_program *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	const struct dml2_core_ip_params *ip = ctx->ip;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	struct dml2_core_internal_scratch *func_params = ctx->func_params;
	struct dml2_core_internal_mode_program *inputs = states;
	struct dml2_core_internal_mode_program *outputs = states;
	struct dml2_core_calcs_CalculateStutterEfficiency_params *p =
			&func_params->CalculateStutterEfficiency_params;

	p->display_cfg = display_cfg;
	p->CompressedBufferSizeInkByte = inputs->CompressedBufferSizeInkByte;
	p->UnboundedRequestEnabled = inputs->UnboundedRequestEnabled;
	p->MetaFIFOSizeInKEntries = ip->meta_fifo_size_in_kentries;
	p->ZeroSizeBufferEntries = ip->zero_size_buffer_entries;
	p->PixelChunkSizeInKByte = ip->pixel_chunk_size_kbytes;
	p->NumberOfActiveSurfaces = display_cfg->num_planes;
	p->ROBBufferSizeInKByte = ip->rob_buffer_size_kbytes;
	p->TotalDataReadBandwidth = inputs->TotalDataReadBandwidth;
	p->DCFCLK = inputs->Dcfclk;
	p->ReturnBW = **inputs->urg_bandwidth_available;
	p->CompbufReservedSpace64B = inputs->compbuf_reserved_space_64b;
	p->CompbufReservedSpaceZs = ip->compbuf_reserved_space_zs;
	p->SRExitTime = soc_bb->power_management_parameters.stutter_exit_latency_us;
	p->SRExitTimeLowPower = soc_bb->power_management_parameters.low_power_stutter_exit_latency_us;
	p->SRExitZ8Time = soc_bb->power_management_parameters.z8_stutter_exit_latency_us;
	p->SynchronizeTimings = display_cfg->overrides.synchronize_timings;
	p->StutterEnterPlusExitWatermark = inputs->Watermark.StutterEnterPlusExitWatermark;
	p->LowPowerStutterEnterPlusExitWatermark = inputs->Watermark.LowPowerStutterEnterPlusExitWatermark;
	p->Z8StutterEnterPlusExitWatermark = inputs->Watermark.Z8StutterEnterPlusExitWatermark;
	p->ProgressiveToInterlaceUnitInOPP = ip->ptoi_supported;
	p->MinTTUVBlank = inputs->MinTTUVBlank;
	p->DPPPerSurface = inputs->NoOfDPP;
	p->DETBufferSizeY = inputs->DETBufferSizeY;
	p->BytePerPixelY = inputs->BytePerPixelY;
	p->BytePerPixelDETY = inputs->BytePerPixelInDETY;
	p->SwathWidthY = inputs->SwathWidthY;
	p->SwathHeightY = inputs->SwathHeightY;
	p->SwathHeightC = inputs->SwathHeightC;
	p->BlockHeight256BytesY = inputs->Read256BlockHeightY;
	p->BlockWidth256BytesY = inputs->Read256BlockWidthY;
	p->BlockHeight256BytesC = inputs->Read256BlockHeightC;
	p->BlockWidth256BytesC = inputs->Read256BlockWidthC;
	p->DCCYMaxUncompressedBlock = inputs->DCCYMaxUncompressedBlock;
	p->DCCCMaxUncompressedBlock = inputs->DCCCMaxUncompressedBlock;
	p->ReadBandwidthSurfaceLuma = inputs->vactive_sw_bw_l;
	p->ReadBandwidthSurfaceChroma = inputs->vactive_sw_bw_c;
	p->dpte_row_bw = inputs->dpte_row_bw;
	p->meta_row_bw = inputs->meta_row_bw;
	p->rob_alloc_compressed = ip->dcn_mrq_present;

	// output
	p->StutterEfficiencyNotIncludingVBlank = &outputs->StutterEfficiencyNotIncludingVBlank;
	p->StutterEfficiency = &outputs->StutterEfficiency;
	p->NumberOfStutterBurstsPerFrame = &outputs->NumberOfStutterBurstsPerFrame;
	p->LowPowerStutterEfficiencyNotIncludingVBlank = &outputs->LowPowerStutterEfficiencyNotIncludingVBlank;
	p->LowPowerStutterEfficiency = &outputs->LowPowerStutterEfficiency;
	p->LowPowerNumberOfStutterBurstsPerFrame = &outputs->LowPowerNumberOfStutterBurstsPerFrame;
	p->Z8StutterEfficiencyNotIncludingVBlank = &outputs->Z8StutterEfficiencyNotIncludingVBlank;
	p->Z8StutterEfficiency = &outputs->Z8StutterEfficiency;
	p->Z8NumberOfStutterBurstsPerFrame = &outputs->Z8NumberOfStutterBurstsPerFrame;
	p->StutterPeriod = &outputs->StutterPeriod;
	p->DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE = &outputs->DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE;

	// Stutter Efficiency
	dcn6_calculate_stutter_efficiency(func_params, p);

#ifdef __DML_VBA_ALLOW_DELTA__
	// Calculate z8 stutter eff assuming 0 reserved space
	p->CompbufReservedSpace64B = 0;
	p->CompbufReservedSpaceZs = 0;

	p->Z8StutterEfficiencyNotIncludingVBlank = &outputs->Z8StutterEfficiencyNotIncludingVBlankBestCase;
	p->Z8StutterEfficiency = &outputs->Z8StutterEfficiencyBestCase;
	p->Z8NumberOfStutterBurstsPerFrame = &outputs->Z8NumberOfStutterBurstsPerFrameBestCase;
	p->StutterPeriod = &outputs->StutterPeriodBestCase;

	// Stutter Efficiency
	dcn6_calculate_stutter_efficiency(func_params, p);
#else
	outputs->Z8StutterEfficiencyNotIncludingVBlankBestCase = outputs->Z8StutterEfficiencyNotIncludingVBlank;
	outputs->Z8StutterEfficiencyBestCase = outputs->Z8StutterEfficiency;
	outputs->Z8NumberOfStutterBurstsPerFrameBestCase = outputs->Z8NumberOfStutterBurstsPerFrame;
	outputs->StutterPeriodBestCase = outputs->StutterPeriod;
#endif
}

static void dcn6_mp_calculate_fraction_of_urgent_bandwidth(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_core_internal_mode_program *states)
{
	(void)ctx;
	struct dml2_core_internal_mode_program *inputs = states;
	struct dml2_core_internal_mode_program *outputs = states;

	outputs->FractionOfUrgentBandwidth = inputs->non_urg_bandwidth_required[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp]
		/ inputs->min_available_urgent_bandwidth_MBps;

	outputs->FractionOfUrgentBandwidthImmediateFlip = inputs->non_urg_bandwidth_required_flip[dml2_core_internal_soc_state_sys_active][dml2_core_internal_bw_sdp]
		/ inputs->min_available_urgent_bandwidth_MBps;

	DML_LOG_DEBUG_DOUBLE(outputs->FractionOfUrgentBandwidth);
}

static void dcn6_mp_calculate_writeback_allow_clock_change_end_position(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_core_internal_mode_program *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_program *inputs = states;
	struct dml2_core_internal_mode_program *outputs = states;
	unsigned int k;
	const struct dml2_plane_parameters *plane;
	const struct dml2_stream_parameters *stream;
	double line_time_us;

	for (k = 0; k < display_cfg->num_planes; ++k) {
		plane = &display_cfg->plane_descriptors[k];
		stream = &display_cfg->stream_descriptors[plane->stream_index];

		if (stream->writeback.active_writebacks_per_stream > 0) {
			line_time_us = stream->timing.h_total / ((double) stream->timing.pixel_clock_khz / 1000);
			outputs->WritebackAllowDRAMClockChangeEndPosition[k] =
					math_max2(0, inputs->VStartupMin[k] * line_time_us
							- inputs->Watermark.WritebackDRAMClockChangeWatermark);
			outputs->WritebackAllowFCLKChangeEndPosition[k] =
					math_max2(0, inputs->VStartupMin[k] * line_time_us
							- inputs->Watermark.WritebackFCLKChangeWatermark);
		} else {
			outputs->WritebackAllowDRAMClockChangeEndPosition[k] = 0;
			outputs->WritebackAllowFCLKChangeEndPosition[k] = 0;
		}
	}
}

static void dcn6_mp_calculate_pstate_keepout_dst_lines(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_core_internal_mode_program *states)
{
	const struct dml2_display_cfg *display_cfg = ctx->display_cfg;
	struct dml2_core_internal_mode_program *inputs = states;
	struct dml2_core_internal_mode_program *outputs = states;

	dcn5_calculate_pstate_keepout_dst_lines(display_cfg, &inputs->Watermark,
			outputs->pstate_keepout_dst_lines);
}

static void dcn6_mp_calculate_dcfclk_deep_sleep_hysteresis(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_core_internal_mode_program *states)
{
	const struct dml2_core_ip_params *ip = ctx->ip;
	const struct dml2_utm_soc_bb *soc_bb = ctx->soc_bb;
	struct dml2_core_internal_mode_program *inputs = states;
	struct dml2_core_internal_mode_program *outputs = states;
	const struct dml2_sop_table *sop_table = &soc_bb->sop_table;
	struct dml2_soc_operating_point max_sop;
	double min_return_latency_in_DCFCLK_cycles = 0;
	const long min_return_uclk_cycles = 83;
	const long min_return_fclk_cycles = 75;

	sop_table->get_max_sop(sop_table, &max_sop);
	min_return_latency_in_DCFCLK_cycles =
			(min_return_uclk_cycles / (max_sop.uclk_khz / 1000.0)
					+ min_return_fclk_cycles / (max_sop.fclk_khz / 1000.0))
			* inputs->Dcfclk;

	outputs->min_return_latency_in_dcfclk = (unsigned int)min_return_latency_in_DCFCLK_cycles;
	outputs->dcfclk_deep_sleep_hysteresis = (unsigned int)math_max2(
			32,
			(double)ip->pixel_chunk_size_kbytes * 1024 * 3 / 4 / 64
					- min_return_latency_in_DCFCLK_cycles);
}

static void dcn6_calculate_mode_programming(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_core_internal_mode_program *states)
{
	dcn6_mp_calculate_fraction_of_urgent_bandwidth(ctx, states);

	dcn6_mp_calculate_pstate_keepout_dst_lines(ctx, states);

	dcn6_mp_calculate_dcc_configurations(ctx, states);

	dcn6_mp_calculate_writeback_allow_clock_change_end_position(ctx, states);

	dcn6_mp_calculate_pixel_delivery_times(ctx, states);

	dcn6_mp_calculate_meta_and_pte_times(ctx, states);

	dcn6_mp_calculate_vm_group_and_request_times(ctx, states);

	dcn6_mp_calculate_vstartup_adjustment(ctx, states);

	dcn6_mp_calculate_max_bandwidth_used(ctx, states);

	dcn6_mp_calculate_stutter_efficiency(ctx, states);

	dcn6_mp_calculate_dcfclk_deep_sleep_hysteresis(ctx, states);

	DML_LOG_VERBOSE("DML::%s: --- END --- \n", __func__);
}

static void dcn6_get_global_sync_programming(const struct dml2_core_internal_display_mode_lib *mode_lib, union dml2_global_sync_programming *out, int pipe_index)
{
	out->dcn4x.vready_offset_pixels = mode_lib->mp.VReadyOffsetPix[mode_lib->mp.pipe_plane[pipe_index]];
	out->dcn4x.vstartup_lines = mode_lib->mp.VStartup[mode_lib->mp.pipe_plane[pipe_index]];
	out->dcn4x.vupdate_offset_pixels = mode_lib->mp.VUpdateOffsetPix[mode_lib->mp.pipe_plane[pipe_index]];
	out->dcn4x.vupdate_vupdate_width_pixels = mode_lib->mp.VUpdateWidthPix[mode_lib->mp.pipe_plane[pipe_index]];
	out->dcn4x.pstate_keepout_start_lines = mode_lib->mp.pstate_keepout_dst_lines[mode_lib->mp.pipe_plane[pipe_index]];
}

static void dcn6_get_stream_programming(const struct dml2_core_internal_display_mode_lib *mode_lib, struct dml2_per_stream_programming *out, int pipe_index)
{
	dcn6_get_global_sync_programming(mode_lib, &out->global_sync, pipe_index);
}

/* 0 : 1 byte per pix
 * 1 : 2 byte per pix
 * 2 : 4 byte per pix
 * 3 : 8 byte per pix
 */
static unsigned int get_element_size_idx(unsigned int byte_per_pix)
{
	unsigned int idx = 0;

	while (byte_per_pix > 1) {
		byte_per_pix >>= 1;
		idx++;
	}
	return idx;
}

/*
 * Swizzle mode values understandable by hardware.
 * Some of sw_swizzle_mode values might not map to existing dml2_swizzle_mode values.
 */
enum sw_swizzle_mode {
	SW_LINEAR     = 0,
	SW_256B_S     = 1,  // No dml2 equivalent
	SW_256B_D     = 2,  // No dml2 equivalent
	SW_256B_R_2D  = 3,
	SW_4KB_S      = 5,  // No dml2 equivalent
	SW_4KB_D      = 6,  // No dml2 equivalent
	SW_4KB_R_2D   = 7,
	SW_64KB_S     = 9,
	SW_64KB_D     = 10, // No dml2 equivalent
	SW_64KB_R_2D  = 11,
	SW_256KB_R_2D = 15,
	SW_64KB_S_T   = 17,
	SW_64KB_D_T   = 18,
	SW_4KB_S_X    = 21, // No dml2 equivalent
	SW_4KB_D_X    = 22, // No dml2 equivalent
	SW_64KB_S_X   = 25,
	SW_64KB_D_X   = 26,
	SW_64KB_R_X   = 27,
	SW_VAR_R_X    = 31, // No dml2 equivalent
};

/*
 * This function maps dml2 swizzle mode used in dml to swizzle mode values
 * understandable by hardware. It is required to prepare correct LSDMA copy command.
 */
static uint8_t dml2_swizzle_mode_to_sw_swizzle_mode(enum dml2_swizzle_mode tiling)
{
	uint8_t sw_swizzle_mode = 0;

	switch (tiling) {
	case dml2_sw_linear:
	case dml2_gfx11_sw_linear:
	case dml2_sw_256b_2d:
		sw_swizzle_mode = SW_256B_R_2D;
		break;
	case dml2_sw_4kb_2d:
		sw_swizzle_mode = SW_4KB_R_2D;
		break;
	case dml2_sw_64kb_2d:
		sw_swizzle_mode = SW_64KB_R_2D;
		break;
	case dml2_sw_256kb_2d:
	case dml2_gfx11_sw_256kb_d_x:
	case dml2_gfx11_sw_256kb_r_x:
		sw_swizzle_mode = SW_256KB_R_2D;
		break;
	case dml2_gfx11_sw_64kb_d_t:
		sw_swizzle_mode = SW_64KB_D_T;
		break;
	case dml2_gfx11_sw_64kb_r_x:
	case dml2_gfx11_sw_64kb_d_x:
		sw_swizzle_mode = SW_64KB_D_X;
		break;
	case dml2_gfx11_sw_64kb_d:
		sw_swizzle_mode = SW_64KB_D;
		break;
	default:
		sw_swizzle_mode = SW_LINEAR;
		break;
	}

	return sw_swizzle_mode;
}

static void dcn6_populate_fams2_programming(const struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_display_solution *solution,
		struct dml2_display_cfg_programming *programming,
		unsigned int main_stream_index)
{
	const struct dml2_uclk_pstate_params *uclk_params = &solution->uclk_pstate_params;
	union dmub_cmd_fams2_config *fams2_base_programming = &programming->stream_programming[main_stream_index].fams2_base_params;
	enum dml2_pstate_method pstate_method = programming->stream_programming[main_stream_index].uclk_pstate_method;
	const struct dml2_display_cfg *disp_cfg = &solution->dispcfg;
	const struct dml2_stream_parameters *stream_descriptor = &disp_cfg->stream_descriptors[main_stream_index];
	const struct dml2_plane_parameters *plane_descriptor;
	const struct dml2_pstate_meta *stream_pstate_meta = &uclk_params->stream_pstate_meta[main_stream_index];

	struct dmub_fams2_cmd_stream_static_base_state *base_programming = &fams2_base_programming->stream_v1.base;
	union dmub_fams2_stream_static_sub_state_v2 *sub_programming = &programming->stream_programming[main_stream_index].fams2_sub_params_v2;

	unsigned int i, vready_offset, per_stream_plane_index = 0;
	bool vertical_access;

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
		if (disp_cfg->plane_descriptors[i].stream_index == main_stream_index &&
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
		sub_programming->legacy.disallow_time_us =
			(uint32_t)(stream_pstate_meta->method_vactive.common.disallow_time_us);
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
		sub_programming->legacy.disallow_time_us =
			(uint32_t)(stream_pstate_meta->method_vblank.common.disallow_time_us);
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
	case dml2_pstate_method_alternate:
		base_programming->type = FAMS2_STREAM_TYPE_ALTERNATE;
		base_programming->allow_start_otg_vline =
				(uint16_t)stream_pstate_meta->method_alternate.common.allow_start_otg_vline;
		base_programming->allow_end_otg_vline =
				(uint16_t)stream_pstate_meta->method_alternate.common.allow_end_otg_vline;
		base_programming->config.bits.clamp_vtotal_min = true;

		sub_programming->alternate.total_bytes_to_copy = mode_lib->mp.svp0_max_bytes + mode_lib->mp.svp1_max_bytes; // global
		sub_programming->alternate.svp0_dst_lines = (uint16_t)mode_lib->mp.svp0_dst_lines[main_stream_index]; // per stream
		sub_programming->alternate.svp1_dst_lines = (uint16_t)mode_lib->mp.svp1_dst_lines[main_stream_index]; // per stream
		sub_programming->alternate.svp_req_limit = (uint16_t)mode_lib->mp.svp_req_limit[main_stream_index]; // per stream
		sub_programming->alternate.min_lead_dst_lines = (uint16_t)mode_lib->mp.min_lead_dst_lines[main_stream_index]; // per stream
		// For now include throttle delay, programming delay, scheduling delay, and contention delay into fw_delays. Can reduce later as needed (this is most likely an overestimate).
		sub_programming->alternate.fw_delays = (uint16_t)(stream_pstate_meta->method_alternate.pmfw_throttle_delay_otg_vlines + stream_pstate_meta->method_alternate.programming_delay_otg_vlines +
															stream_pstate_meta->scheduling_delay_otg_vlines + stream_pstate_meta->contention_delay_otg_vlines);
		sub_programming->alternate.max_cursor_size = 128; // DCN6 limits HW cursor to 128x128 - only matters for alternate channel
		sub_programming->alternate.vready_offset_lines = 0; // initialize to 0, assign actual value in inner loop
		base_programming->allow_to_target_delay_otg_vlines = (uint16_t)math_max2(mode_lib->mp.svp_req_limit[main_stream_index], mode_lib->mp.max_prefetch_in_lines[main_stream_index]);
		for (i = 0; i < programming->display_config.num_planes; i++) {
			if (disp_cfg->plane_descriptors[i].stream_index == main_stream_index) {
				plane_descriptor = &disp_cfg->plane_descriptors[i];

				vertical_access = plane_descriptor->composition.rotation_angle == dml2_rotation_90 || plane_descriptor->composition.rotation_angle == dml2_rotation_270;

				sub_programming->alternate.rec_height[per_stream_plane_index] = vertical_access ? (uint16_t)((double)plane_descriptor->composition.viewport.plane0.width / plane_descriptor->composition.scaler_info.plane0.v_ratio) :
																				(uint16_t)((double)plane_descriptor->composition.viewport.plane0.height / plane_descriptor->composition.scaler_info.plane0.v_ratio);
				sub_programming->alternate.vstartup_start = (uint16_t)mode_lib->mp.VStartup[i]; // per stream, but DML populates for every plane
				/* For now viewport_size indicates the number of lines in the direction that's perpendicular to the scan direction */
				sub_programming->alternate.viewport_start[per_stream_plane_index] = vertical_access ? (uint16_t)plane_descriptor->composition.viewport.plane0.x_start : (uint16_t)plane_descriptor->composition.viewport.plane0.y_start;
				sub_programming->alternate.viewport_size[per_stream_plane_index] = vertical_access ? (uint16_t)plane_descriptor->composition.viewport.plane0.width : (uint16_t)plane_descriptor->composition.viewport.plane0.height;
				sub_programming->alternate.viewport_start_c[per_stream_plane_index] = vertical_access ? (uint16_t)plane_descriptor->composition.viewport.plane1.x_start : (uint16_t)plane_descriptor->composition.viewport.plane1.y_start;
				sub_programming->alternate.viewport_size_c[per_stream_plane_index] = vertical_access ? (uint16_t)plane_descriptor->composition.viewport.plane1.width : (uint16_t)plane_descriptor->composition.viewport.plane1.height;
				sub_programming->alternate.surface_pitch[per_stream_plane_index] = (uint16_t)plane_descriptor->surface.plane0.pitch;
				sub_programming->alternate.surface_pitch_c[per_stream_plane_index] = (uint16_t)plane_descriptor->surface.plane1.pitch;
				sub_programming->alternate.surface_height[per_stream_plane_index] = (uint16_t)plane_descriptor->surface.plane0.height;
				sub_programming->alternate.surface_height_c[per_stream_plane_index] = (uint16_t)plane_descriptor->surface.plane1.height;
				sub_programming->alternate.element_size[per_stream_plane_index] = (uint8_t) get_element_size_idx(mode_lib->mp.BytePerPixelY[i]);
				sub_programming->alternate.element_size_c[per_stream_plane_index] = (uint8_t) get_element_size_idx(mode_lib->mp.BytePerPixelC[i]);
				sub_programming->alternate.swath_height[per_stream_plane_index] = (uint8_t)mode_lib->mp.SwathHeightY[i];
				sub_programming->alternate.swath_height_c[per_stream_plane_index] = (uint8_t)mode_lib->mp.SwathHeightC[i];
				sub_programming->alternate.macro_tile_width[per_stream_plane_index] = (uint16_t)mode_lib->mp.MacroTileWidthY[i];
				sub_programming->alternate.macro_tile_width_c[per_stream_plane_index] = (uint16_t)mode_lib->mp.MacroTileWidthC[i];
				sub_programming->alternate.block_256b_width[per_stream_plane_index] = (uint16_t)mode_lib->mp.Read256BlockWidthY[i];
				sub_programming->alternate.block_256b_height[per_stream_plane_index] = (uint16_t)mode_lib->mp.Read256BlockHeightY[i];
				sub_programming->alternate.block_256b_width_c[per_stream_plane_index] = (uint16_t)mode_lib->mp.Read256BlockWidthC[i];
				sub_programming->alternate.block_256b_height_c[per_stream_plane_index] = (uint16_t)mode_lib->mp.Read256BlockHeightC[i];
				sub_programming->alternate.dst_y_prefetch_x1000[per_stream_plane_index] = (uint16_t)(mode_lib->mp.dst_y_prefetch[i] * 1000);
				sub_programming->alternate.total_swaths[per_stream_plane_index] = (uint16_t)mode_lib->mp.total_swaths[i];
				sub_programming->alternate.total_swaths_c[per_stream_plane_index] = (uint16_t)mode_lib->mp.total_swaths_c[i];
				sub_programming->alternate.prefetch_swaths[per_stream_plane_index] = (uint8_t)mode_lib->mp.prefetch_swaths[i];
				sub_programming->alternate.prefetch_swaths_c[per_stream_plane_index] = (uint8_t)mode_lib->mp.prefetch_swaths_c[i];
				sub_programming->alternate.pre_hdl_delta_x1000[per_stream_plane_index] = (uint16_t)(mode_lib->mp.prefetch_hdl_delta[i] * 1000);
				sub_programming->alternate.rec_hdl_delta_x1000[per_stream_plane_index] = (uint16_t)(mode_lib->mp.recout_hdl_delta[i] * 1000);
				sub_programming->alternate.pre_hdl_delta_c_x1000[per_stream_plane_index] = (uint16_t)(mode_lib->mp.prefetch_hdl_delta_c[i] * 1000);
				sub_programming->alternate.rec_hdl_delta_c_x1000[per_stream_plane_index] = (uint16_t)(mode_lib->mp.recout_hdl_delta_c[i] * 1000);
				sub_programming->alternate.dst_y_per_vm_vblank_x1000[per_stream_plane_index] = (uint16_t)(mode_lib->mp.dst_y_per_vm_vblank[i] * 1000);
				sub_programming->alternate.dst_y_per_row_vblank_x1000[per_stream_plane_index] = (uint16_t)(mode_lib->mp.dst_y_per_row_vblank[i] * 1000);
				sub_programming->alternate.dst_y_after_scaler[per_stream_plane_index] = (uint16_t)mode_lib->mp.DSTYAfterScaler[i];
				sub_programming->alternate.vinit_prefill[per_stream_plane_index] = (uint16_t)mode_lib->mp.VInitPreFillY[i];
				sub_programming->alternate.vinit_prefill_c[per_stream_plane_index] = (uint16_t)mode_lib->mp.VInitPreFillC[i];
				sub_programming->alternate.vratio_x1000[per_stream_plane_index] = (uint16_t)(plane_descriptor->composition.scaler_info.plane0.v_ratio * 1000);
				sub_programming->alternate.vratio_c_x1000[per_stream_plane_index] = (uint16_t)(plane_descriptor->composition.scaler_info.plane1.v_ratio * 1000);
				sub_programming->alternate.swizzle_mode[per_stream_plane_index] = dml2_swizzle_mode_to_sw_swizzle_mode(plane_descriptor->surface.tiling);
				// For now round up vready offset to the next line, and add +1 line. It can be more precise if we confirm that vstartup rising edge is always at the beginning of the line.
				vready_offset = (unsigned int)(math_ceil(((double)mode_lib->mp.VUpdateOffsetPix[i] + mode_lib->mp.VUpdateWidthPix[i] + mode_lib->mp.VReadyOffsetPix[i] + 1) / stream_descriptor->timing.h_total) + 1);
				if (vready_offset > sub_programming->alternate.vready_offset_lines)
					sub_programming->alternate.vready_offset_lines = (uint8_t)vready_offset;
				sub_programming->alternate.config[per_stream_plane_index].bits.prefetch_relative_vblank = 1;
				sub_programming->alternate.config[per_stream_plane_index].bits.dcc = plane_descriptor->surface.dcc.enable;
				sub_programming->alternate.config[per_stream_plane_index].bits.is_multi_planar =
						plane_descriptor->surface.plane1.height > 0;
				sub_programming->alternate.config[per_stream_plane_index].bits.is_yuv420 =
						plane_descriptor->pixel_format == dml2_420_8 ||
						plane_descriptor->pixel_format == dml2_420_10 ||
						plane_descriptor->pixel_format == dml2_420_12;
				sub_programming->alternate.config[per_stream_plane_index].bits.vertical_access = vertical_access ? 1 : 0;
				sub_programming->alternate.config[per_stream_plane_index].bits.access_direction =
						  (plane_descriptor->composition.rotation_angle == dml2_rotation_90 && !plane_descriptor->composition.mirrored) ||
						  (plane_descriptor->composition.rotation_angle == dml2_rotation_270 && plane_descriptor->composition.mirrored) ||
						  (plane_descriptor->composition.rotation_angle == dml2_rotation_180) ? 1 : 0;

				sub_programming->alternate.pipe_copy_max_size[0][per_stream_plane_index] = mode_lib->mp.svp0_max_bytes_per_dpp[i];
				sub_programming->alternate.pipe_copy_max_size[1][per_stream_plane_index] = mode_lib->mp.svp1_max_bytes_per_dpp[i];
				sub_programming->alternate.pipe_copy_max_size_c[0][per_stream_plane_index] = mode_lib->mp.svp0_max_bytes_per_dpp_c[i];
				sub_programming->alternate.pipe_copy_max_size_c[1][per_stream_plane_index] = mode_lib->mp.svp1_max_bytes_per_dpp_c[i];

				per_stream_plane_index++;
			}
		}
		base_programming->num_planes = (uint8_t)per_stream_plane_index;
		break;
	case dml2_pstate_method_reserved_hw:
	case dml2_pstate_method_reserved_fw:
	case dml2_pstate_method_reserved_fw_drr_clamped:
	case dml2_pstate_method_reserved_fw_drr_var:
	case dml2_pstate_method_fw_svp:
	case dml2_pstate_method_fw_svp_drr:
	case dml2_pstate_method_na:
	case dml2_pstate_method_count:
	default:
		/* this should never happen */
		break;
	}
}

static void dcn6_populate_global_fams2_programming(const struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_display_solution *solution,
		struct dmub_cmd_fams2_global_config *fams2_global_config)
{
	unsigned int i, allow_to_target_delta_us;
	double line_time_us;
	fams2_global_config->features.bits.enable = solution->uclk_pstate_params.fams2_required;
	fams2_global_config->features.bits.legacy_method_no_fams2 = solution->uclk_pstate_params.legacy_pstate_info_for_dmu;

	if (fams2_global_config->features.bits.enable || fams2_global_config->features.bits.legacy_method_no_fams2) {
		fams2_global_config->features.bits.enable_stall_recovery = true;
		fams2_global_config->features.bits.allow_delay_check_mode = FAMS2_ALLOW_DELAY_CHECK_FROM_START;

		fams2_global_config->max_allow_delay_us = mode_lib->ip_caps.fams2.max_allow_delay_us;
		fams2_global_config->lock_wait_time_us = mode_lib->ip_caps.fams2.lock_timeout_us;
		fams2_global_config->recovery_timeout_us = mode_lib->ip_caps.fams2.recovery_timeout_us;
		fams2_global_config->hwfq_flip_programming_delay_us = mode_lib->ip_caps.fams2.flip_programming_delay_us;

		fams2_global_config->num_streams = solution->dispcfg.num_streams;

		for (i = 0; i < solution->dispcfg.num_streams; i++) {
			line_time_us = ((double)solution->dispcfg.stream_descriptors[i].timing.h_total * 1000 / solution->dispcfg.stream_descriptors[i].timing.pixel_clock_khz);
			allow_to_target_delta_us = (unsigned int)math_ceil(mode_lib->mp.svp_req_limit[i] * line_time_us);
			if (fams2_global_config->max_allow_to_target_delta_us < allow_to_target_delta_us)
				fams2_global_config->max_allow_to_target_delta_us = allow_to_target_delta_us;
		}
	}
}

static void dcn6_populate_min_clocks(struct dml2_display_cfg_programming *programming,
		const struct dml2_display_solution *solution, const struct dml2_utm_soc_bb *utm_soc_bb)
{
	unsigned int i;

	programming->min_clocks.dcn4x.dispclk_khz = solution->validation_result.mode_support.global.dispclk_khz;
	programming->min_clocks.dcn4x.dpprefclk_khz = solution->validation_result.mode_support.global.dpprefclk_khz;
	programming->min_clocks.dcn4x.dtbrefclk_khz = solution->validation_result.mode_support.global.dtbrefclk_khz;
	programming->min_clocks.dcn4x.socclk_khz = utm_soc_bb->min_socclk_khz;
	if (solution->dispcfg.overrides.hw.dcfclk_mhz > 0)
		programming->min_clocks.dcn4x.active.dcfclk_khz =
				(unsigned long) math_ceil(solution->dispcfg.overrides.hw.dcfclk_mhz * 1000);
	else
		programming->min_clocks.dcn4x.active.dcfclk_khz = solution->sop_constraint.dcn5.clocks.dcfclk_khz;
	programming->min_clocks.dcn4x.active.fclk_khz = solution->sop_constraint.dcn5.clocks.fclk_khz;
	programming->min_clocks.dcn4x.active.uclk_khz = solution->sop_constraint.dcn5.clocks.uclk_khz;
	programming->min_sop_index = solution->sop_constraint.dcn5.min_sop_index;
	programming->min_clocks.dcn4x.deepsleep_dcfclk_khz =
		(unsigned long) math_min2((double)solution->validation_result.mode_support.global.dcfclk_deepsleep_khz, (double)programming->min_clocks.dcn4x.active.dcfclk_khz);
	for (i = 0; i < solution->dispcfg.num_planes; i++)
		programming->plane_programming[i].min_clocks.dcn4x.dppclk_khz =
				solution->validation_result.mode_support.per_plane[i].dppclk_khz;
	for (i = 0; i < solution->dispcfg.num_streams; i++) {
		programming->stream_programming[i].min_clocks.dcn4x.dscclk_khz =
			solution->validation_result.mode_support.per_stream[i].dscclk_khz;
		programming->stream_programming[i].min_clocks.dcn4x.dtbclk_khz =
			solution->validation_result.mode_support.per_stream[i].dtbclk_khz;
		programming->stream_programming[i].min_clocks.dcn4x.phyclk_khz =
			solution->validation_result.mode_support.per_stream[i].phyclk_khz;
	}
}

static void dcn6_populate_stutter_support(struct dml2_display_cfg_programming *programming,
		const struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_display_solution *solution,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	programming->stutter.supported_in_blank = solution->stutter_support_in_vblank;
	programming->z8_stutter.supported_in_blank = solution->z8_stutter_support_in_vblank;
	programming->z8_stutter.meets_eco = utm_soc_bb->power_management_parameters.z8_min_idle_time > 0
			&& mode_lib->mp.StutterPeriod >= utm_soc_bb->power_management_parameters.z8_min_idle_time;
	programming->stutter.base_percent_efficiency = (uint8_t)(mode_lib->mp.StutterEfficiency);
	programming->stutter.low_power_percent_efficiency = (uint8_t)(mode_lib->mp.LowPowerStutterEfficiency);
}

static void dcn6_populate_mcache_allocation(struct dml2_display_cfg_programming *programming,
		const struct dml2_display_solution *solution)
{
	unsigned int i;

	for (i = 0; i < solution->dispcfg.num_planes; i++)
		programming->plane_programming[i].mcache_allocation = solution->mcache_allocations[i];
}

static void dcn6_populate_qos_bound(struct dml2_display_cfg_programming *programming,
		const struct dml2_display_solution *solution)
{
	programming->qos_bound.latency_ub = solution->sop_constraint.dcn5.latency;
	programming->qos_bound.bandwidth_lb = solution->validation_result.mode_support.bandwidth_upper_bound;
	programming->qos_bound.lsdma_bandwidth_lb_kbps = solution->validation_result.mode_support.global.lsdma_bw_req_for_alt_kbps;
}

static void dcn6_populate_mode_programming(struct dml2_display_cfg_programming *programming,
		struct dml2_core_internal_scratch *s,
		const struct dml2_core_internal_display_mode_lib *mode_lib,
		const struct dml2_display_solution *solution,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	const struct dml2_uclk_pstate_params *uclk_params = &solution->uclk_pstate_params;
	const struct core_display_cfg_support_info *cfg_support_info = &solution->validation_result.mode_support.cfg_support_info;
	unsigned int pipe_offset;
	int dml_internal_pipe_index;
	int total_pipe_regs_copied = 0;
	int stream_already_populated_mask = 0;

	unsigned int main_stream_index;
	unsigned int plane_index;

	memcpy(&programming->display_config, &solution->dispcfg, sizeof(struct dml2_display_cfg));
	dcn6_populate_min_clocks(programming, solution, utm_soc_bb);
	dcn5_get_arb_params(&programming->display_config, mode_lib, utm_soc_bb, &programming->global_regs.arb_regs);
	programming->global_regs.num_watermark_sets = 1;
	dcn6_get_watermarks(&programming->display_config, mode_lib, utm_soc_bb, &programming->global_regs.wm_regs[0]);
	dcn6_populate_stutter_support(programming, mode_lib, solution, utm_soc_bb);
	dcn6_populate_mcache_allocation(programming, solution);
	dcn6_populate_qos_bound(programming, solution);
	dml_internal_pipe_index = 0;

	programming->fclk_pstate_supported = solution->fclk_pstate_support;
	if (uclk_params) {
		programming->uclk_pstate_supported = uclk_params->support;
		if (programming->uclk_pstate_supported) {
			programming->fams2_required = uclk_params->fams2_required;
			programming->legacy_pstate_info_for_dmu = uclk_params->legacy_pstate_info_for_dmu;
			dcn6_populate_global_fams2_programming(mode_lib, solution, &programming->fams2_global_config);
		}
	}

	DML_LOG_VERBOSE("DML_PMO::%s: num_planes=%u\n", __func__, programming->display_config.num_planes);

	for (plane_index = 0; plane_index < programming->display_config.num_planes; plane_index++) {
		programming->plane_programming[plane_index].num_dpps_required = mode_lib->mp.NoOfDPP[plane_index];

		// Setup the appropriate p-state strategy for each plane
		if (uclk_params) {
			if (programming->uclk_pstate_supported)
				programming->plane_programming[plane_index].uclk_pstate_support_method = uclk_params->pstate_switch_modes[plane_index];
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
			dcn6_get_pipe_regs(&programming->display_config, mode_lib, programming->plane_programming[plane_index].pipe_regs[pipe_offset], dml_internal_pipe_index, utm_soc_bb, s);

			main_stream_index = programming->display_config.plane_descriptors[plane_index].stream_index;

			// Multiple planes can refer to the same stream index, so it's only necessary to populate it once
			if (!(stream_already_populated_mask & (0x1 << main_stream_index))) {
				programming->stream_programming[main_stream_index].uclk_pstate_method = programming->plane_programming[plane_index].uclk_pstate_support_method;
				programming->stream_programming[main_stream_index].stream_descriptor = &programming->display_config.stream_descriptors[main_stream_index];
				programming->stream_programming[main_stream_index].num_odms_required = cfg_support_info->stream_support_info[main_stream_index].odms_used;
				dcn6_get_stream_programming(mode_lib, &programming->stream_programming[main_stream_index], dml_internal_pipe_index);

				stream_already_populated_mask |= (0x1 << main_stream_index);
			}
			dml_internal_pipe_index++;
		}
	}
	/* Loop through per stream to populate FAMS programming. The internal function will loop through
	 * per plane as needed (specifically for alternate scenarios).
	 */
	for (main_stream_index = 0; main_stream_index < programming->display_config.num_streams; main_stream_index++) {
		if (uclk_params) {
			dcn6_populate_fams2_programming(mode_lib,
				solution,
				programming,
				main_stream_index);
		}
	}
	// TODO: Add DML_LOG_DEBUG for entire programming structure to log out calculated values
}

/*
 * This function converts display_config and cfg_support_info into mode_lib.mp data.
 *
 * To move mode support result to mode programming, the data needs to be saved in cfg_support_info in the end of
 * mode support, so we can access cfg_support_info and populate mode programming data in this function based on it.
 */
static void dcn6_mp_initialize_from_solution(struct dml2_core_internal_mode_program *outputs,
		const struct dml2_display_solution *solution,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	const struct dml2_display_cfg *display_cfg = &solution->dispcfg;
	const struct core_display_cfg_support_info *cfg_support_info =
			&solution->validation_result.mode_support.cfg_support_info;
	const struct core_plane_support_info *plane_support_info;
	const struct core_stream_support_info *stream_support_info;
	unsigned int k;
	const struct dml2_plane_parameters *plane;

	outputs->num_active_pipes = dml2_core_util_get_num_active_pipes(display_cfg->num_planes, cfg_support_info);

	dml2_core_utils_pipe_plane_mapping(cfg_support_info, outputs->pipe_plane);

	dml2_core_utils_get_stream_output_bpp(outputs->OutputBpp, display_cfg);

	dcn6_mp_populate_odm_mode(solution, outputs);

	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		plane_support_info = &cfg_support_info->plane_support_info[k];
		stream_support_info = &cfg_support_info->stream_support_info[plane->stream_index];

		outputs->immediate_flip_required = outputs->immediate_flip_required
				|| plane->immediate_flip;
		outputs->NoOfDPP[k] = plane_support_info->dpps_used;
		outputs->dsc_enable[k] = stream_support_info->dsc_enable;
		outputs->num_dsc_slices[k] = stream_support_info->num_dsc_slices;
	}

	/* min clocks */
	outputs->GlobalDPPCLK = solution->validation_result.mode_support.global.dpprefclk_khz / 1000.0;
	if (solution->dispcfg.overrides.hw.dcfclk_mhz > 0)
		outputs->Dcfclk = solution->dispcfg.overrides.hw.dcfclk_mhz;
	else
		outputs->Dcfclk = solution->sop_constraint.dcn5.clocks.dcfclk_khz / 1000.0;
	outputs->FabricClock = solution->sop_constraint.dcn5.clocks.fclk_khz / 1000.0;
	outputs->uclk_freq_mhz = solution->sop_constraint.dcn5.clocks.uclk_khz / 1000.0;
	outputs->SOCCLK = utm_soc_bb->min_socclk_khz / 1000.0;
	for (k = 0; k < display_cfg->num_planes; k++) {
		plane = &display_cfg->plane_descriptors[k];
		outputs->Dppclk[k] = solution->validation_result.mode_support.per_plane[k].dppclk_khz / 1000.0;
		outputs->DSCCLK[k] = solution->validation_result.mode_support.per_stream[plane->stream_index].dscclk_khz / 1000.0;
	}

	/* qos bound */
	outputs->min_available_urgent_bandwidth_MBps = solution->validation_result.mode_support.bandwidth_upper_bound.dcn5.urgent_bandwidth_kbps / 1000.0;
	**outputs->urg_bandwidth_available = math_min2(solution->sop_constraint.dcn5.min_available_urgent_bandwidth_KBps / 1000.0,
		outputs->Dcfclk * utm_soc_bb->urgent_sdp_derate_percent / 100.0 * utm_soc_bb->return_bus_width_bytes);

	outputs->UrgentLatency = solution->sop_constraint.dcn5.latency.dcn5.urgent_ramp;
	outputs->TripToMemory = math_max2(solution->sop_constraint.dcn5.latency.dcn5.urgent_ramp,
			solution->sop_constraint.dcn5.latency.dcn5.t_trip);
	outputs->MetaTripToMemory = solution->sop_constraint.dcn5.latency.dcn5.meta_trip_to_mem;
	outputs->max_urgent_latency_us = solution->sop_constraint.dcn5.latency.dcn5.max_req_latency_urg;
	outputs->df_response_time_us = solution->sop_constraint.dcn5.latency.dcn5.df_response_time_us;

	/* pstate */
	memcpy(outputs->uclk_pstate_switch_modes,
			solution->uclk_pstate_params.pstate_switch_modes,
			sizeof(solution->uclk_pstate_params.pstate_switch_modes));
}

static void dcn6_mp_build_calculate_mp_context(struct dml2_core_calculate_mp_context *ctx,
		struct dml2_core_instance *core,
		const struct dml2_display_solution *solution)
{
	struct dml2_core_internal_display_mode_lib *mode_lib = &core->clean_me_up.mode_lib;

	ctx->display_cfg = &solution->dispcfg;
	ctx->ip = &mode_lib->ip;
	ctx->soc_bb = core->utm_soc_bb;
	ctx->ms = &mode_lib->ms;
	ctx->dummies = &mode_lib->scratch.dml_core_mode_programming_locals;
	ctx->func_params = &mode_lib->scratch;
}

static void dcn6_mp_initialize_from_ms(struct dml2_core_internal_mode_program *outputs,
		const struct dml2_core_internal_mode_support *ms)
{
	outputs->DCFCLKDeepSleep = ms->dcfclk_deepsleep;
	outputs->Dispclk = ms->RequiredDISPCLK;
	outputs->MaxTotalDETInKByte = ms->MaxTotalDETInKByte;
	outputs->NomDETInKByte = ms->NomDETInKByte;
	outputs->MinCompressedBufferSizeInKByte = ms->MinCompressedBufferSizeInKByte;
	memcpy(outputs->PixelClockBackEnd, ms->PixelClockBackEnd, sizeof(outputs->PixelClockBackEnd));
	memcpy(outputs->DPPCLKUsingSingleDPP, ms->MinDPPCLKUsingSingleDPP, sizeof(outputs->DPPCLKUsingSingleDPP));
	memcpy(outputs->PSCL_THROUGHPUT, ms->PSCL_FACTOR, sizeof(outputs->PSCL_THROUGHPUT));
	memcpy(outputs->PSCL_THROUGHPUT_CHROMA, ms->PSCL_FACTOR_CHROMA, sizeof(outputs->PSCL_THROUGHPUT_CHROMA));
	memcpy(outputs->BytePerPixelY, ms->BytePerPixelY, sizeof(outputs->BytePerPixelY));
	memcpy(outputs->BytePerPixelC, ms->BytePerPixelC, sizeof(outputs->BytePerPixelC));
	memcpy(outputs->BytePerPixelInDETY, ms->BytePerPixelInDETY, sizeof(outputs->BytePerPixelInDETY));
	memcpy(outputs->BytePerPixelInDETC, ms->BytePerPixelInDETC, sizeof(outputs->BytePerPixelInDETC));
	memcpy(outputs->Read256BlockHeightY, ms->Read256BlockHeightY, sizeof(outputs->Read256BlockHeightY));
	memcpy(outputs->Read256BlockHeightC, ms->Read256BlockHeightC, sizeof(outputs->Read256BlockHeightC));
	memcpy(outputs->Read256BlockWidthY, ms->Read256BlockWidthY, sizeof(outputs->Read256BlockWidthY));
	memcpy(outputs->Read256BlockWidthC, ms->Read256BlockWidthC, sizeof(outputs->Read256BlockWidthC));
	memcpy(outputs->MacroTileHeightY, ms->MacroTileHeightY, sizeof(outputs->MacroTileHeightY));
	memcpy(outputs->MacroTileHeightC, ms->MacroTileHeightC, sizeof(outputs->MacroTileHeightC));
	memcpy(outputs->MacroTileWidthY, ms->MacroTileWidthY, sizeof(outputs->MacroTileWidthY));
	memcpy(outputs->MacroTileWidthC, ms->MacroTileWidthC, sizeof(outputs->MacroTileWidthC));
	memcpy(outputs->surf_linear128_l, ms->surf_linear128_l, sizeof(outputs->surf_linear128_l));
	memcpy(outputs->surf_linear128_c, ms->surf_linear128_c, sizeof(outputs->surf_linear128_c));
	memcpy(outputs->MaximumSwathWidthChroma, ms->MaximumSwathWidthChroma, sizeof(outputs->MaximumSwathWidthChroma));
	outputs->MaximumSwathWidthInLineBufferChroma = ms->MaximumSwathWidthInLineBufferChroma;
	memcpy(outputs->MaximumSwathWidthLuma, ms->MaximumSwathWidthLuma, sizeof(outputs->MaximumSwathWidthLuma));
	outputs->MaximumSwathWidthInLineBufferLuma = ms->MaximumSwathWidthInLineBufferLuma;
	memcpy(outputs->cursor_bw, ms->cursor_bw, sizeof(outputs->cursor_bw));
	memcpy(outputs->vactive_sw_bw_c, ms->vactive_sw_bw_c, sizeof(outputs->vactive_sw_bw_c));
	memcpy(outputs->vactive_sw_bw_l, ms->vactive_sw_bw_l, sizeof(outputs->vactive_sw_bw_l));
	memcpy(outputs->SwathWidthSingleDPPC, ms->SwathWidthCSingleDPP, sizeof(outputs->SwathWidthSingleDPPC));
	memcpy(outputs->SwathWidthSingleDPPY, ms->SwathWidthYSingleDPP, sizeof(outputs->SwathWidthSingleDPPY));
	memcpy(outputs->req_per_swath_ub_l, ms->req_per_swath_ub_l, sizeof(outputs->req_per_swath_ub_l));
	memcpy(outputs->req_per_swath_ub_c, ms->req_per_swath_ub_c, sizeof(outputs->req_per_swath_ub_c));
	memcpy(outputs->swath_width_luma_ub, ms->swath_width_luma_ub, sizeof(outputs->swath_width_luma_ub));
	memcpy(outputs->swath_width_chroma_ub, ms->swath_width_chroma_ub, sizeof(outputs->swath_width_chroma_ub));
	memcpy(outputs->SwathWidthY, ms->SwathWidthY, sizeof(outputs->SwathWidthY));
	memcpy(outputs->SwathWidthC, ms->SwathWidthC, sizeof(outputs->SwathWidthC));
	memcpy(outputs->SwathHeightY, ms->SwathHeightY, sizeof(outputs->SwathHeightY));
	memcpy(outputs->SwathHeightC, ms->SwathHeightC, sizeof(outputs->SwathHeightC));
	memcpy(outputs->request_size_bytes_luma, ms->support.request_size_bytes_luma, sizeof(outputs->request_size_bytes_luma));
	memcpy(outputs->request_size_bytes_chroma, ms->support.request_size_bytes_chroma, sizeof(outputs->request_size_bytes_chroma));
	memcpy(outputs->DETBufferSizeInKByte, ms->DETBufferSizeInKByte, sizeof(outputs->DETBufferSizeInKByte));
	memcpy(outputs->DETBufferSizeY, ms->DETBufferSizeY, sizeof(outputs->DETBufferSizeY));
	memcpy(outputs->DETBufferSizeC, ms->DETBufferSizeC, sizeof(outputs->DETBufferSizeC));
	memcpy(outputs->full_swath_bytes_l, ms->full_swath_bytes_l, sizeof(outputs->full_swath_bytes_l));
	memcpy(outputs->full_swath_bytes_c, ms->full_swath_bytes_c, sizeof(outputs->full_swath_bytes_c));
	outputs->UnboundedRequestEnabled = ms->UnboundedRequestEnabled;
	outputs->compbuf_reserved_space_64b = ms->compbuf_reserved_space_64b;
	outputs->hw_debug5 = ms->hw_debug5;
	outputs->CompressedBufferSizeInkByte = ms->CompressedBufferSizeInkByte;
	memcpy(outputs->DSCDelay, ms->DSCDelay, sizeof(outputs->DSCDelay));
	memcpy(outputs->vmpg_height_y, ms->vmpg_height_y, sizeof(outputs->vmpg_height_y));
	memcpy(outputs->VInitPreFillC, ms->PrefillC, sizeof(outputs->VInitPreFillC));
	memcpy(outputs->is_using_mall_for_ss, ms->is_using_mall_for_ss, sizeof(outputs->is_using_mall_for_ss));
	memcpy(outputs->meta_row_width_chroma, ms->meta_row_width_chroma, sizeof(outputs->meta_row_width_chroma));
	memcpy(outputs->dpte_row_bytes_per_row_c, ms->dpte_row_bytes_per_row_c, sizeof(outputs->dpte_row_bytes_per_row_c));
	memcpy(outputs->PixelPTEReqHeightC, ms->PixelPTEReqHeightC, sizeof(outputs->PixelPTEReqHeightC));
	memcpy(outputs->meta_row_height, ms->meta_row_height_luma, sizeof(outputs->meta_row_height));
	memcpy(outputs->MaxNumSwathY, ms->MaxNumSwathY, sizeof(outputs->MaxNumSwathY));
	memcpy(outputs->meta_row_bytes_per_row_ub_l, ms->meta_row_bytes_per_row_ub_l, sizeof(outputs->meta_row_bytes_per_row_ub_l));
	memcpy(outputs->PTE_BUFFER_MODE, ms->PTE_BUFFER_MODE, sizeof(outputs->PTE_BUFFER_MODE));
	memcpy(outputs->dpte_row_bytes_per_row_l, ms->dpte_row_bytes_per_row_l, sizeof(outputs->dpte_row_bytes_per_row_l));
	memcpy(outputs->dpte_row_height, ms->dpte_row_height, sizeof(outputs->dpte_row_height));
	memcpy(outputs->meta_req_height_chroma, ms->meta_req_height_chroma, sizeof(outputs->meta_req_height_chroma));
	memcpy(outputs->PixelPTEBytesPerRow, ms->DPTEBytesPerRow, sizeof(outputs->PixelPTEBytesPerRow));
	memcpy(outputs->vmpg_width_y, ms->vmpg_width_y, sizeof(outputs->vmpg_width_y));
	memcpy(outputs->meta_pte_bytes_per_frame_ub_c, ms->meta_pte_bytes_per_frame_ub_c, sizeof(outputs->meta_pte_bytes_per_frame_ub_c));
	memcpy(outputs->dpde0_bytes_per_frame_ub_c, ms->dpde0_bytes_per_frame_ub_c, sizeof(outputs->dpde0_bytes_per_frame_ub_c));
	memcpy(outputs->dpte_group_bytes, ms->dpte_group_bytes, sizeof(outputs->dpte_group_bytes));
	memcpy(outputs->dpte_row_width_luma_ub, ms->dpte_row_width_luma_ub, sizeof(outputs->dpte_row_width_luma_ub));
	memcpy(outputs->meta_req_width, ms->meta_req_width, sizeof(outputs->meta_req_width));
	memcpy(outputs->PrefetchSourceLinesY, ms->PrefetchLinesY, sizeof(outputs->PrefetchSourceLinesY));
	memcpy(outputs->vmpg_width_c, ms->vmpg_width_c, sizeof(outputs->vmpg_width_c));
	memcpy(outputs->meta_row_bytes, ms->meta_row_bytes, sizeof(outputs->meta_row_bytes));
	memcpy(outputs->PrefetchSourceLinesC, ms->PrefetchLinesC, sizeof(outputs->PrefetchSourceLinesC));
	memcpy(outputs->meta_row_bw, ms->meta_row_bw, sizeof(outputs->meta_row_bw));
	memcpy(outputs->meta_row_width, ms->meta_row_width, sizeof(outputs->meta_row_width));
	memcpy(outputs->PixelPTEReqWidthY, ms->PixelPTEReqWidthY, sizeof(outputs->PixelPTEReqWidthY));
	memcpy(outputs->dpte_row_bw, ms->dpte_row_bw, sizeof(outputs->dpte_row_bw));
	memcpy(outputs->dpte_row_height_linear, ms->dpte_row_height_linear, sizeof(outputs->dpte_row_height_linear));
	memcpy(outputs->PTERequestSizeY, ms->PTERequestSizeY, sizeof(outputs->PTERequestSizeY));
	memcpy(outputs->dpte_row_height_chroma, ms->dpte_row_height_chroma, sizeof(outputs->dpte_row_height_chroma));
	memcpy(outputs->VInitPreFillY, ms->PrefillY, sizeof(outputs->VInitPreFillY));
	memcpy(outputs->use_one_row_for_frame, ms->use_one_row_for_frame, sizeof(outputs->use_one_row_for_frame));
	memcpy(outputs->vmpg_height_c, ms->vmpg_height_c, sizeof(outputs->vmpg_height_c));
	memcpy(outputs->dpte_row_width_chroma_ub, ms->dpte_row_width_chroma_ub, sizeof(outputs->dpte_row_width_chroma_ub));
	memcpy(outputs->PixelPTEReqWidthC, ms->PixelPTEReqWidthC, sizeof(outputs->PixelPTEReqWidthC));
	memcpy(outputs->meta_pte_bytes_per_frame_ub_l, ms->meta_pte_bytes_per_frame_ub_l, sizeof(outputs->meta_pte_bytes_per_frame_ub_l));
	memcpy(outputs->meta_row_height_chroma, ms->meta_row_height_chroma, sizeof(outputs->meta_row_height_chroma));
	memcpy(outputs->MaxNumSwathC, ms->MaxNumSwathC, sizeof(outputs->MaxNumSwathC));
	memcpy(outputs->dpte_row_height_linear_chroma, ms->dpte_row_height_linear_chroma, sizeof(outputs->dpte_row_height_linear_chroma));
	memcpy(outputs->PTERequestSizeC, ms->PTERequestSizeC, sizeof(outputs->PTERequestSizeC));
	memcpy(outputs->meta_req_height, ms->meta_req_height, sizeof(outputs->meta_req_height));
	memcpy(outputs->vm_bytes, ms->vm_bytes, sizeof(outputs->vm_bytes));
	memcpy(outputs->meta_row_bytes_per_row_ub_c, ms->meta_row_bytes_per_row_ub_c, sizeof(outputs->meta_row_bytes_per_row_ub_c));
	memcpy(outputs->dpde0_bytes_per_frame_ub_l, ms->dpde0_bytes_per_frame_ub_l, sizeof(outputs->dpde0_bytes_per_frame_ub_l));
	memcpy(outputs->meta_req_width_chroma, ms->meta_req_width_chroma, sizeof(outputs->meta_req_width_chroma));
	memcpy(outputs->PixelPTEReqHeightY, ms->PixelPTEReqHeightY, sizeof(outputs->PixelPTEReqHeightY));
	memcpy(outputs->BIGK_FRAGMENT_SIZE, ms->BIGK_FRAGMENT_SIZE, sizeof(outputs->BIGK_FRAGMENT_SIZE));
	memcpy(outputs->vm_group_bytes, ms->vm_group_bytes, sizeof(outputs->vm_group_bytes));
	memcpy(outputs->use_one_row_for_frame_flip, ms->use_one_row_for_frame_flip, sizeof(outputs->use_one_row_for_frame_flip));
	memcpy(outputs->mall_comb_mcache_l, ms->mall_comb_mcache_l, sizeof(outputs->mall_comb_mcache_l));
	memcpy(outputs->dcc_dram_bw_nom_overhead_factor_p0, ms->dcc_dram_bw_nom_overhead_factor_p0, sizeof(outputs->dcc_dram_bw_nom_overhead_factor_p0));
	memcpy(outputs->mcache_row_bytes_l, ms->mcache_row_bytes_l, sizeof(outputs->mcache_row_bytes_l));
	memcpy(outputs->dcc_dram_bw_pref_overhead_factor_p1, ms->dcc_dram_bw_pref_overhead_factor_p1, sizeof(outputs->dcc_dram_bw_pref_overhead_factor_p1));
	memcpy(outputs->mcache_offsets_l, ms->mcache_offsets_l, sizeof(outputs->mcache_offsets_l));
	memcpy(outputs->mcache_shift_granularity_l, ms->mcache_shift_granularity_l, sizeof(outputs->mcache_shift_granularity_l));
	memcpy(outputs->mcache_offsets_c, ms->mcache_offsets_c, sizeof(outputs->mcache_offsets_c));
	memcpy(outputs->lc_comb_mcache, ms->lc_comb_mcache, sizeof(outputs->lc_comb_mcache));
	memcpy(outputs->num_mcaches_c, ms->num_mcaches_c, sizeof(outputs->num_mcaches_c));
	memcpy(outputs->mcache_row_bytes_per_channel_l, ms->mcache_row_bytes_per_channel_l, sizeof(outputs->mcache_row_bytes_per_channel_l));
	memcpy(outputs->mcache_shift_granularity_c, ms->mcache_shift_granularity_c, sizeof(outputs->mcache_shift_granularity_c));
	memcpy(outputs->mcache_row_bytes_per_channel_c, ms->mcache_row_bytes_per_channel_c, sizeof(outputs->mcache_row_bytes_per_channel_c));
	memcpy(outputs->mcache_row_bytes_c, ms->mcache_row_bytes_c, sizeof(outputs->mcache_row_bytes_c));
	memcpy(outputs->num_mcaches_l, ms->num_mcaches_l, sizeof(outputs->num_mcaches_l));
	memcpy(outputs->mall_comb_mcache_c, ms->mall_comb_mcache_c, sizeof(outputs->mall_comb_mcache_c));
	memcpy(outputs->dcc_dram_bw_nom_overhead_factor_p1, ms->dcc_dram_bw_nom_overhead_factor_p1, sizeof(outputs->dcc_dram_bw_nom_overhead_factor_p1));
	memcpy(outputs->dcc_dram_bw_pref_overhead_factor_p0, ms->dcc_dram_bw_pref_overhead_factor_p0, sizeof(outputs->dcc_dram_bw_pref_overhead_factor_p0));
	outputs->TCalc = ms->TimeCalc;
	outputs->HostVMInefficiencyFactor = ms->HostVMInefficiencyFactor;
	outputs->HostVMInefficiencyFactorPrefetch = ms->HostVMInefficiencyFactorPrefetch;
	memcpy(outputs->tdlut_pte_bytes_per_frame, ms->tdlut_pte_bytes_per_frame, sizeof(outputs->tdlut_pte_bytes_per_frame));
	memcpy(outputs->tdlut_bytes_per_frame, ms->tdlut_bytes_per_frame, sizeof(outputs->tdlut_bytes_per_frame));
	memcpy(outputs->tdlut_drain_time, ms->tdlut_drain_time, sizeof(outputs->tdlut_drain_time));
	memcpy(outputs->tdlut_opt_time, ms->tdlut_opt_time, sizeof(outputs->tdlut_opt_time));
	memcpy(outputs->tdlut_bytes_per_group, ms->tdlut_bytes_per_group, sizeof(outputs->tdlut_bytes_per_group));
	memcpy(outputs->tdlut_groups_per_2row_ub, ms->tdlut_groups_per_2row_ub, sizeof(outputs->tdlut_groups_per_2row_ub));
	outputs->ExtraLatencyPrefetch = ms->ExtraLatencyPrefetch;
	outputs->ExtraLatency = ms->ExtraLatency;
	outputs->ExtraLatency_sr = ms->ExtraLatency_sr;
	memcpy(outputs->WritebackDelay, ms->WritebackDelayTime, sizeof(outputs->WritebackDelay));
	memcpy(outputs->excess_vactive_fill_bw_c, ms->excess_vactive_fill_bw_c, sizeof(outputs->excess_vactive_fill_bw_c));
	memcpy(outputs->excess_vactive_fill_bw_l, ms->excess_vactive_fill_bw_l, sizeof(outputs->excess_vactive_fill_bw_l));
	memcpy(outputs->NotEnoughUrgentLatencyHiding, ms->NotEnoughUrgentLatencyHiding, sizeof(outputs->NotEnoughUrgentLatencyHiding));
	memcpy(outputs->UrgentBurstFactorCursor, ms->UrgentBurstFactorCursor, sizeof(outputs->UrgentBurstFactorCursor));
	memcpy(outputs->UrgentBurstFactorChroma, ms->UrgentBurstFactorChroma, sizeof(outputs->UrgentBurstFactorChroma));
	memcpy(outputs->cursor_bytes_per_chunk, ms->cursor_bytes_per_chunk, sizeof(outputs->cursor_bytes_per_chunk));
	memcpy(outputs->cursor_bytes_per_line, ms->cursor_bytes_per_line, sizeof(outputs->cursor_bytes_per_line));
	memcpy(outputs->UrgentBurstFactorCursorPre, ms->UrgentBurstFactorCursorPre, sizeof(outputs->UrgentBurstFactorCursorPre));
	memcpy(outputs->UrgentBurstFactorLuma, ms->UrgentBurstFactorLuma, sizeof(outputs->UrgentBurstFactorLuma));
	memcpy(outputs->MaxVStartupLines, ms->MaxVStartupLines, sizeof(outputs->MaxVStartupLines));
	memcpy(outputs->Tr0_trips_flip_rounded, ms->Tr0_trips_flip_rounded, sizeof(outputs->Tr0_trips_flip_rounded));
	memcpy(outputs->Tno_bw_flip, ms->Tno_bw_flip, sizeof(outputs->Tno_bw_flip));
	memcpy(outputs->dst_y_prefetch, ms->dst_y_prefetch, sizeof(outputs->dst_y_prefetch));
	memcpy(outputs->DSTXAfterScaler, ms->DSTXAfterScaler, sizeof(outputs->DSTXAfterScaler));
	memcpy(outputs->DSTYAfterScaler, ms->DSTYAfterScaler, sizeof(outputs->DSTYAfterScaler));
	memcpy(outputs->Tr0_trips_flip, ms->Tr0_trips_flip, sizeof(outputs->Tr0_trips_flip));
	memcpy(outputs->VReadyOffsetPix, ms->VReadyOffsetPix, sizeof(outputs->VReadyOffsetPix));
	memcpy(outputs->prefetch_vmrow_bw, ms->prefetch_vmrow_bw, sizeof(outputs->prefetch_vmrow_bw));
	memcpy(outputs->dst_y_per_vm_vblank, ms->LinesForVM, sizeof(outputs->dst_y_per_vm_vblank));
	memcpy(outputs->VUpdateOffsetPix, ms->VUpdateOffsetPix, sizeof(outputs->VUpdateOffsetPix));
	memcpy(outputs->RequiredPrefetchPixelDataBWLuma, ms->RequiredPrefetchPixelDataBWLuma, sizeof(outputs->RequiredPrefetchPixelDataBWLuma));
	memcpy(outputs->Tno_bw, ms->Tno_bw, sizeof(outputs->Tno_bw));
	memcpy(outputs->TSetup, ms->TSetup, sizeof(outputs->TSetup));
	memcpy(outputs->Tdmdl_vm_raw, ms->Tdmdl_vm_raw, sizeof(outputs->Tdmdl_vm_raw));
	memcpy(outputs->VRatioPrefetchY, ms->VRatioPreY, sizeof(outputs->VRatioPrefetchY));
	memcpy(outputs->prefetch_cursor_bw, ms->prefetch_cursor_bw, sizeof(outputs->prefetch_cursor_bw));
	memcpy(outputs->NotEnoughTimeForDynamicMetadata, ms->NoTimeForDynamicMetadata, sizeof(outputs->NotEnoughTimeForDynamicMetadata));
	memcpy(outputs->RequiredPrefetchPixelDataBWChroma, ms->RequiredPrefetchPixelDataBWChroma, sizeof(outputs->RequiredPrefetchPixelDataBWChroma));
	memcpy(outputs->VUpdateWidthPix, ms->VUpdateWidthPix, sizeof(outputs->VUpdateWidthPix));
	memcpy(outputs->NoTimeToPrefetch, ms->NoTimeForPrefetch, sizeof(outputs->NoTimeToPrefetch));
	memcpy(outputs->dst_y_per_row_vblank, ms->LinesForDPTERow, sizeof(outputs->dst_y_per_row_vblank));
	memcpy(outputs->Tdmdl_raw, ms->Tdmdl_raw, sizeof(outputs->Tdmdl_raw));
	memcpy(outputs->Tvm_trips_flip, ms->Tvm_trips_flip, sizeof(outputs->Tvm_trips_flip));
	memcpy(outputs->TWait, ms->TWait, sizeof(outputs->TWait));
	memcpy(outputs->VRatioPrefetchC, ms->VRatioPreC, sizeof(outputs->VRatioPrefetchC));
	memcpy(outputs->Tvm_trips_flip_rounded, ms->Tvm_trips_flip_rounded, sizeof(outputs->Tvm_trips_flip_rounded));
	memcpy(outputs->VStartupMin, ms->VStartupMin, sizeof(outputs->VStartupMin));
	outputs->PrefetchScheduleSupported = ms->support.PrefetchScheduleSupported;
	memcpy(outputs->UrgentBurstFactorLumaPre, ms->UrgentBurstFactorLumaPre, sizeof(outputs->UrgentBurstFactorLumaPre));
	memcpy(outputs->UrgentBurstFactorChromaPre, ms->UrgentBurstFactorChromaPre, sizeof(outputs->UrgentBurstFactorChromaPre));
	memcpy(outputs->NotEnoughUrgentLatencyHidingPre, ms->NotEnoughUrgentLatencyHidingPre, sizeof(outputs->NotEnoughUrgentLatencyHidingPre));
	memcpy(outputs->urg_vactive_bandwidth_required, ms->support.urg_vactive_bandwidth_required, sizeof(outputs->urg_vactive_bandwidth_required));
	memcpy(outputs->urg_bandwidth_required, ms->support.urg_bandwidth_required, sizeof(outputs->urg_bandwidth_required));
	memcpy(outputs->non_urg_bandwidth_required, ms->support.non_urg_bandwidth_required, sizeof(outputs->non_urg_bandwidth_required));
	memcpy(outputs->urg_bandwidth_required_flip, ms->support.urg_bandwidth_required_flip, sizeof(outputs->urg_bandwidth_required_flip));
	memcpy(outputs->non_urg_bandwidth_required_flip, ms->support.non_urg_bandwidth_required_flip, sizeof(outputs->non_urg_bandwidth_required_flip));
	outputs->PrefetchModeSupported = ms->support.PrefetchSupported;
	memcpy(outputs->final_flip_bw, ms->final_flip_bw, sizeof(outputs->final_flip_bw));
	memcpy(outputs->ImmediateFlipSupportedForPipe, ms->ImmediateFlipSupportedForPipe, sizeof(outputs->ImmediateFlipSupportedForPipe));
	memcpy(outputs->dst_y_per_vm_flip, ms->dst_y_per_vm_flip, sizeof(outputs->dst_y_per_vm_flip));
	memcpy(outputs->dst_y_per_row_flip, ms->dst_y_per_row_flip, sizeof(outputs->dst_y_per_row_flip));
	outputs->Watermark = ms->support.watermarks;
	memcpy(outputs->DRAMClockChangeSupport, ms->support.DRAMClockChangeSupport, sizeof(outputs->DRAMClockChangeSupport));
	outputs->global_dram_clock_change_supported = ms->support.global_dram_clock_change_supported;
	memcpy(outputs->MaxActiveDRAMClockChangeLatencySupported, ms->MaxActiveDRAMClockChangeLatencySupported, sizeof(outputs->MaxActiveDRAMClockChangeLatencySupported));
	memcpy(outputs->SubViewportLinesNeededInMALL, ms->SubViewportLinesNeededInMALL, sizeof(outputs->SubViewportLinesNeededInMALL));
	memcpy(outputs->FCLKChangeSupport, ms->support.FCLKChangeSupport, sizeof(outputs->FCLKChangeSupport));
	outputs->global_fclk_change_supported = ms->support.global_fclk_change_supported;
	outputs->MaxActiveFCLKChangeLatencySupported = ms->MaxActiveFCLKChangeLatencySupported;
	outputs->USRRetrainingSupport = ms->support.USRRetrainingSupport;
	outputs->global_temp_read_or_ppt_supported = ms->support.global_temp_read_or_ppt_supported;
	memcpy(outputs->VActiveLatencyHidingUs, ms->VActiveLatencyHidingUs, sizeof(outputs->VActiveLatencyHidingUs));
	outputs->ImmediateFlipSupported = ms->support.ImmediateFlipSupport;

	outputs->svp0_max_bytes = ms->svp0_max_bytes;
	outputs->svp1_max_bytes = ms->svp1_max_bytes;
	memcpy(outputs->svp0_max_bytes_per_dpp, ms->svp0_max_bytes_per_dpp, sizeof(outputs->svp0_max_bytes_per_dpp));
	memcpy(outputs->svp0_max_bytes_per_dpp_c, ms->svp0_max_bytes_per_dpp_c, sizeof(outputs->svp0_max_bytes_per_dpp_c));
	memcpy(outputs->svp1_max_bytes_per_dpp, ms->svp1_max_bytes_per_dpp, sizeof(outputs->svp1_max_bytes_per_dpp));
	memcpy(outputs->svp1_max_bytes_per_dpp_c, ms->svp1_max_bytes_per_dpp_c, sizeof(outputs->svp1_max_bytes_per_dpp_c));
	memcpy(outputs->svp0_dst_lines, ms->svp0_dst_lines, sizeof(outputs->svp0_dst_lines));
	memcpy(outputs->svp1_dst_lines, ms->svp1_dst_lines, sizeof(outputs->svp1_dst_lines));
	memcpy(outputs->svp_req_limit, ms->svp_req_limit, sizeof(outputs->svp_req_limit));
	memcpy(outputs->nom_req_limit_alt, ms->nom_req_limit_alt, sizeof(outputs->nom_req_limit_alt));
	memcpy(outputs->min_lead_dst_lines, ms->min_lead_dst_lines, sizeof(outputs->min_lead_dst_lines));
	memcpy(outputs->total_swaths, ms->total_swaths, sizeof(outputs->total_swaths));
	memcpy(outputs->total_swaths_c, ms->total_swaths_c, sizeof(outputs->total_swaths_c));
	memcpy(outputs->prefetch_swaths, ms->prefetch_swaths, sizeof(outputs->prefetch_swaths));
	memcpy(outputs->prefetch_swaths_c, ms->prefetch_swaths_c, sizeof(outputs->prefetch_swaths_c));
	memcpy(outputs->prefetch_hdl_delta, ms->prefetch_hdl_delta, sizeof(outputs->prefetch_hdl_delta));
	memcpy(outputs->recout_hdl_delta, ms->recout_hdl_delta, sizeof(outputs->recout_hdl_delta));
	memcpy(outputs->prefetch_hdl_delta_c, ms->prefetch_hdl_delta_c, sizeof(outputs->prefetch_hdl_delta_c));
	memcpy(outputs->recout_hdl_delta_c, ms->recout_hdl_delta_c, sizeof(outputs->recout_hdl_delta_c));
	memcpy(outputs->max_prefetch_in_lines, ms->max_prefetch_in_lines, sizeof(outputs->max_prefetch_in_lines));
}

enum dml2_status dml2_core_dcn6_funcs_populate_programming(struct dml2_core_instance *core,
		const struct dml2_display_solution *solution,
		struct dml2_display_cfg_programming *programming)
{
	struct dml2_core_internal_display_mode_lib *mode_lib = &core->clean_me_up.mode_lib;
	struct dml2_core_calculate_mp_context *calc_mp_ctx = &core->scratch.mode_programming_locals.calc_mp_ctx;
	enum dml2_status status;

	DML_LOG_COMP_IF_ENTER();
	memset(&mode_lib->scratch, 0, sizeof(struct dml2_core_internal_scratch));
	memset(&mode_lib->mp, 0, sizeof(struct dml2_core_internal_mode_program));
	memset(&core->scratch.mode_programming_locals.temp_result, 0, sizeof(struct dml2_validation_result));

	status = core->validate_solution(core, solution, &core->scratch.mode_programming_locals.temp_result);
	DML_ASSERT_MSG(status == DML2_STATUS_OK,
			"an invalid solution is passed into populate_programming interface! (status = %s)\n",
			dml2_status_str(status));

	dcn6_mp_initialize_from_ms(&mode_lib->mp, &mode_lib->ms);
	dcn6_mp_initialize_from_solution(&mode_lib->mp, solution, core->utm_soc_bb);
	dcn6_mp_build_calculate_mp_context(calc_mp_ctx, core, solution);
	dcn6_calculate_mode_programming(calc_mp_ctx, &mode_lib->mp);
	dcn6_populate_mode_programming(programming, &mode_lib->scratch, mode_lib, solution, core->utm_soc_bb);

	DML_LOG_DEBUG("%s exit\n", __func__);
	DML_LOG_COMP_IF_EXIT();
	return status;
}
