/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */


#include "display_mode_lib.h"
#include "display_mode_vba.h"
#include "dml_inline_defs.h"

/*
 * NOTE:
 *   This file is gcc-parsable HW gospel, coming straight from HW engineers.
 *
 * It doesn't adhere to Linux kernel style and sometimes will do things in odd
 * ways. Unless there is something clearly wrong with it the code should
 * remain as-is as it provides us with a guarantee from HW that it is correct.
 */


static void fetch_socbb_params(struct display_mode_lib *mode_lib);
static void fetch_ip_params(struct display_mode_lib *mode_lib);
static void fetch_pipe_params(struct display_mode_lib *mode_lib);
static void recalculate_params(
		struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes);

static unsigned int CursorBppEnumToBits(enum cursor_bpp ebpp);
static void cache_debug_params(struct display_mode_lib *mode_lib);

unsigned int dml_get_voltage_level(
		struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes)
{
	bool need_recalculate = memcmp(&mode_lib->soc, &mode_lib->vba.soc, sizeof(mode_lib->vba.soc)) != 0
			|| memcmp(&mode_lib->ip, &mode_lib->vba.ip, sizeof(mode_lib->vba.ip)) != 0
			|| num_pipes != mode_lib->vba.cache_num_pipes
			|| memcmp(pipes, mode_lib->vba.cache_pipes,
					sizeof(display_e2e_pipe_params_st) * num_pipes) != 0;

	mode_lib->vba.soc = mode_lib->soc;
	mode_lib->vba.ip = mode_lib->ip;
	memcpy(mode_lib->vba.cache_pipes, pipes, sizeof(*pipes) * num_pipes);
	mode_lib->vba.cache_num_pipes = num_pipes;

	if (need_recalculate && pipes[0].clks_cfg.dppclk_mhz != 0)
		mode_lib->funcs.recalculate(mode_lib);
	else {
		fetch_socbb_params(mode_lib);
		fetch_ip_params(mode_lib);
		fetch_pipe_params(mode_lib);
		PixelClockAdjustmentForProgressiveToInterlaceUnit(mode_lib);
	}
	mode_lib->funcs.validate(mode_lib);
	cache_debug_params(mode_lib);

	return mode_lib->vba.VoltageLevel;
}

#define dml_get_attr_func(attr, var)  double get_##attr(struct display_mode_lib *mode_lib, const display_e2e_pipe_params_st *pipes, unsigned int num_pipes) \
{ \
	recalculate_params(mode_lib, pipes, num_pipes); \
	return var; \
}

dml_get_attr_func(clk_dcf_deepsleep, mode_lib->vba.DCFCLKDeepSleep);
dml_get_attr_func(wm_urgent, mode_lib->vba.UrgentWatermark);
dml_get_attr_func(wm_memory_trip, mode_lib->vba.UrgentLatency);
dml_get_attr_func(wm_writeback_urgent, mode_lib->vba.WritebackUrgentWatermark);
dml_get_attr_func(wm_stutter_exit, mode_lib->vba.StutterExitWatermark);
dml_get_attr_func(wm_stutter_enter_exit, mode_lib->vba.StutterEnterPlusExitWatermark);
dml_get_attr_func(wm_z8_stutter_exit, mode_lib->vba.Z8StutterExitWatermark);
dml_get_attr_func(wm_z8_stutter_enter_exit, mode_lib->vba.Z8StutterEnterPlusExitWatermark);
dml_get_attr_func(stutter_efficiency_z8, mode_lib->vba.Z8StutterEfficiency);
dml_get_attr_func(stutter_num_bursts_z8, mode_lib->vba.Z8NumberOfStutterBurstsPerFrame);
dml_get_attr_func(wm_dram_clock_change, mode_lib->vba.DRAMClockChangeWatermark);
dml_get_attr_func(wm_writeback_dram_clock_change, mode_lib->vba.WritebackDRAMClockChangeWatermark);
dml_get_attr_func(stutter_efficiency, mode_lib->vba.StutterEfficiency);
dml_get_attr_func(stutter_efficiency_no_vblank, mode_lib->vba.StutterEfficiencyNotIncludingVBlank);
dml_get_attr_func(stutter_period, mode_lib->vba.StutterPeriod);
dml_get_attr_func(urgent_latency, mode_lib->vba.UrgentLatency);
dml_get_attr_func(urgent_extra_latency, mode_lib->vba.UrgentExtraLatency);
dml_get_attr_func(nonurgent_latency, mode_lib->vba.NonUrgentLatencyTolerance);
dml_get_attr_func(dram_clock_change_latency, mode_lib->vba.MinActiveDRAMClockChangeLatencySupported);
dml_get_attr_func(dispclk_calculated, mode_lib->vba.DISPCLK_calculated);
dml_get_attr_func(total_data_read_bw, mode_lib->vba.TotalDataReadBandwidth);
dml_get_attr_func(return_bw, mode_lib->vba.ReturnBW);
dml_get_attr_func(tcalc, mode_lib->vba.TCalc);
dml_get_attr_func(fraction_of_urgent_bandwidth, mode_lib->vba.FractionOfUrgentBandwidth);
dml_get_attr_func(fraction_of_urgent_bandwidth_imm_flip, mode_lib->vba.FractionOfUrgentBandwidthImmediateFlip);


dml_get_attr_func(cstate_max_cap_mode, mode_lib->vba.DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE);
dml_get_attr_func(comp_buffer_size_kbytes, mode_lib->vba.CompressedBufferSizeInkByte);
dml_get_attr_func(pixel_chunk_size_in_kbyte, mode_lib->vba.PixelChunkSizeInKByte);
dml_get_attr_func(alpha_pixel_chunk_size_in_kbyte, mode_lib->vba.AlphaPixelChunkSizeInKByte);
dml_get_attr_func(meta_chunk_size_in_kbyte, mode_lib->vba.MetaChunkSize);
dml_get_attr_func(min_pixel_chunk_size_in_byte, mode_lib->vba.MinPixelChunkSizeBytes);
dml_get_attr_func(min_meta_chunk_size_in_byte, mode_lib->vba.MinMetaChunkSizeBytes);
dml_get_attr_func(fclk_watermark, mode_lib->vba.Watermark.FCLKChangeWatermark);
dml_get_attr_func(usr_retraining_watermark, mode_lib->vba.Watermark.USRRetrainingWatermark);

dml_get_attr_func(comp_buffer_reserved_space_kbytes, mode_lib->vba.CompBufReservedSpaceKBytes);
dml_get_attr_func(comp_buffer_reserved_space_64bytes, mode_lib->vba.CompBufReservedSpace64B);
dml_get_attr_func(comp_buffer_reserved_space_zs, mode_lib->vba.CompBufReservedSpaceZs);
dml_get_attr_func(unbounded_request_enabled, mode_lib->vba.UnboundedRequestEnabled);

#define dml_get_pipe_attr_func(attr, var)  double get_##attr(struct display_mode_lib *mode_lib, const display_e2e_pipe_params_st *pipes, unsigned int num_pipes, unsigned int which_pipe) \
{\
	unsigned int which_plane; \
	recalculate_params(mode_lib, pipes, num_pipes); \
	which_plane = mode_lib->vba.pipe_plane[which_pipe]; \
	return var[which_plane]; \
}

dml_get_pipe_attr_func(dsc_delay, mode_lib->vba.DSCDelay);
dml_get_pipe_attr_func(dppclk_calculated, mode_lib->vba.DPPCLK_calculated);
dml_get_pipe_attr_func(dscclk_calculated, mode_lib->vba.DSCCLK_calculated);
dml_get_pipe_attr_func(min_ttu_vblank, mode_lib->vba.MinTTUVBlank);
dml_get_pipe_attr_func(min_ttu_vblank_in_us, mode_lib->vba.MinTTUVBlank);
dml_get_pipe_attr_func(vratio_prefetch_l, mode_lib->vba.VRatioPrefetchY);
dml_get_pipe_attr_func(vratio_prefetch_c, mode_lib->vba.VRatioPrefetchC);
dml_get_pipe_attr_func(dst_x_after_scaler, mode_lib->vba.DSTXAfterScaler);
dml_get_pipe_attr_func(dst_y_after_scaler, mode_lib->vba.DSTYAfterScaler);
dml_get_pipe_attr_func(dst_y_per_vm_vblank, mode_lib->vba.DestinationLinesToRequestVMInVBlank);
dml_get_pipe_attr_func(dst_y_per_row_vblank, mode_lib->vba.DestinationLinesToRequestRowInVBlank);
dml_get_pipe_attr_func(dst_y_prefetch, mode_lib->vba.DestinationLinesForPrefetch);
dml_get_pipe_attr_func(dst_y_per_vm_flip, mode_lib->vba.DestinationLinesToRequestVMInImmediateFlip);
dml_get_pipe_attr_func(dst_y_per_row_flip, mode_lib->vba.DestinationLinesToRequestRowInImmediateFlip);
dml_get_pipe_attr_func(refcyc_per_vm_group_vblank, mode_lib->vba.TimePerVMGroupVBlank);
dml_get_pipe_attr_func(refcyc_per_vm_group_flip, mode_lib->vba.TimePerVMGroupFlip);
dml_get_pipe_attr_func(refcyc_per_vm_req_vblank, mode_lib->vba.TimePerVMRequestVBlank);
dml_get_pipe_attr_func(refcyc_per_vm_req_flip, mode_lib->vba.TimePerVMRequestFlip);
dml_get_pipe_attr_func(refcyc_per_vm_group_vblank_in_us, mode_lib->vba.TimePerVMGroupVBlank);
dml_get_pipe_attr_func(refcyc_per_vm_group_flip_in_us, mode_lib->vba.TimePerVMGroupFlip);
dml_get_pipe_attr_func(refcyc_per_vm_req_vblank_in_us, mode_lib->vba.TimePerVMRequestVBlank);
dml_get_pipe_attr_func(refcyc_per_vm_req_flip_in_us, mode_lib->vba.TimePerVMRequestFlip);
dml_get_pipe_attr_func(refcyc_per_vm_dmdata_in_us, mode_lib->vba.Tdmdl_vm);
dml_get_pipe_attr_func(dmdata_dl_delta_in_us, mode_lib->vba.Tdmdl);
dml_get_pipe_attr_func(refcyc_per_line_delivery_l_in_us, mode_lib->vba.DisplayPipeLineDeliveryTimeLuma);
dml_get_pipe_attr_func(refcyc_per_line_delivery_c_in_us, mode_lib->vba.DisplayPipeLineDeliveryTimeChroma);
dml_get_pipe_attr_func(refcyc_per_line_delivery_pre_l_in_us, mode_lib->vba.DisplayPipeLineDeliveryTimeLumaPrefetch);
dml_get_pipe_attr_func(refcyc_per_line_delivery_pre_c_in_us, mode_lib->vba.DisplayPipeLineDeliveryTimeChromaPrefetch);
dml_get_pipe_attr_func(refcyc_per_req_delivery_l_in_us, mode_lib->vba.DisplayPipeRequestDeliveryTimeLuma);
dml_get_pipe_attr_func(refcyc_per_req_delivery_c_in_us, mode_lib->vba.DisplayPipeRequestDeliveryTimeChroma);
dml_get_pipe_attr_func(refcyc_per_req_delivery_pre_l_in_us, mode_lib->vba.DisplayPipeRequestDeliveryTimeLumaPrefetch);
dml_get_pipe_attr_func(refcyc_per_req_delivery_pre_c_in_us, mode_lib->vba.DisplayPipeRequestDeliveryTimeChromaPrefetch);
dml_get_pipe_attr_func(refcyc_per_cursor_req_delivery_in_us, mode_lib->vba.CursorRequestDeliveryTime);
dml_get_pipe_attr_func(refcyc_per_cursor_req_delivery_pre_in_us, mode_lib->vba.CursorRequestDeliveryTimePrefetch);
dml_get_pipe_attr_func(refcyc_per_meta_chunk_nom_l_in_us, mode_lib->vba.TimePerMetaChunkNominal);
dml_get_pipe_attr_func(refcyc_per_meta_chunk_nom_c_in_us, mode_lib->vba.TimePerChromaMetaChunkNominal);
dml_get_pipe_attr_func(refcyc_per_meta_chunk_vblank_l_in_us, mode_lib->vba.TimePerMetaChunkVBlank);
dml_get_pipe_attr_func(refcyc_per_meta_chunk_vblank_c_in_us, mode_lib->vba.TimePerChromaMetaChunkVBlank);
dml_get_pipe_attr_func(refcyc_per_meta_chunk_flip_l_in_us, mode_lib->vba.TimePerMetaChunkFlip);
dml_get_pipe_attr_func(refcyc_per_meta_chunk_flip_c_in_us, mode_lib->vba.TimePerChromaMetaChunkFlip);
dml_get_pipe_attr_func(vstartup, mode_lib->vba.VStartup);
dml_get_pipe_attr_func(vupdate_offset, mode_lib->vba.VUpdateOffsetPix);
dml_get_pipe_attr_func(vupdate_width, mode_lib->vba.VUpdateWidthPix);
dml_get_pipe_attr_func(vready_offset, mode_lib->vba.VReadyOffsetPix);
dml_get_pipe_attr_func(vready_at_or_after_vsync, mode_lib->vba.VREADY_AT_OR_AFTER_VSYNC);
dml_get_pipe_attr_func(min_dst_y_next_start, mode_lib->vba.MIN_DST_Y_NEXT_START);
dml_get_pipe_attr_func(dst_y_per_pte_row_nom_l, mode_lib->vba.DST_Y_PER_PTE_ROW_NOM_L);
dml_get_pipe_attr_func(dst_y_per_pte_row_nom_c, mode_lib->vba.DST_Y_PER_PTE_ROW_NOM_C);
dml_get_pipe_attr_func(dst_y_per_meta_row_nom_l, mode_lib->vba.DST_Y_PER_META_ROW_NOM_L);
dml_get_pipe_attr_func(dst_y_per_meta_row_nom_c, mode_lib->vba.DST_Y_PER_META_ROW_NOM_C);
dml_get_pipe_attr_func(refcyc_per_pte_group_nom_l_in_us, mode_lib->vba.time_per_pte_group_nom_luma);
dml_get_pipe_attr_func(refcyc_per_pte_group_nom_c_in_us, mode_lib->vba.time_per_pte_group_nom_chroma);
dml_get_pipe_attr_func(refcyc_per_pte_group_vblank_l_in_us, mode_lib->vba.time_per_pte_group_vblank_luma);
dml_get_pipe_attr_func(refcyc_per_pte_group_vblank_c_in_us, mode_lib->vba.time_per_pte_group_vblank_chroma);
dml_get_pipe_attr_func(refcyc_per_pte_group_flip_l_in_us, mode_lib->vba.time_per_pte_group_flip_luma);
dml_get_pipe_attr_func(refcyc_per_pte_group_flip_c_in_us, mode_lib->vba.time_per_pte_group_flip_chroma);
dml_get_pipe_attr_func(vstartup_calculated, mode_lib->vba.VStartup);
dml_get_pipe_attr_func(dpte_row_height_linear_c, mode_lib->vba.dpte_row_height_linear_chroma);
dml_get_pipe_attr_func(swath_height_l, mode_lib->vba.SwathHeightY);
dml_get_pipe_attr_func(swath_height_c, mode_lib->vba.SwathHeightC);
dml_get_pipe_attr_func(det_stored_buffer_size_l_bytes, mode_lib->vba.DETBufferSizeY);
dml_get_pipe_attr_func(det_stored_buffer_size_c_bytes, mode_lib->vba.DETBufferSizeC);
dml_get_pipe_attr_func(dpte_group_size_in_bytes, mode_lib->vba.dpte_group_bytes);
dml_get_pipe_attr_func(vm_group_size_in_bytes, mode_lib->vba.vm_group_bytes);
dml_get_pipe_attr_func(dpte_row_height_linear_l, mode_lib->vba.dpte_row_height_linear);
dml_get_pipe_attr_func(pte_buffer_mode, mode_lib->vba.PTE_BUFFER_MODE);
dml_get_pipe_attr_func(subviewport_lines_needed_in_mall, mode_lib->vba.SubViewportLinesNeededInMALL);

double get_total_immediate_flip_bytes(
		struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes)
{
	recalculate_params(mode_lib, pipes, num_pipes);
	return mode_lib->vba.TotImmediateFlipBytes;
}

double get_total_immediate_flip_bw(
		struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes)
{
	unsigned int k;
	double immediate_flip_bw = 0.0;
	recalculate_params(mode_lib, pipes, num_pipes);
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k)
		immediate_flip_bw += mode_lib->vba.ImmediateFlipBW[k];
	return immediate_flip_bw;
}

double get_total_prefetch_bw(
		struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes)
{
	unsigned int k;
	double total_prefetch_bw = 0.0;

	recalculate_params(mode_lib, pipes, num_pipes);
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k)
		total_prefetch_bw += mode_lib->vba.PrefetchBandwidth[k];
	return total_prefetch_bw;
}

unsigned int get_total_surface_size_in_mall_bytes(
		struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes)
{
	unsigned int k;
	unsigned int size = 0.0;
	recalculate_params(mode_lib, pipes, num_pipes);
	for (k = 0; k < mode_lib->vba.NumberOfActiveSurfaces; ++k)
		size += mode_lib->vba.SurfaceSizeInMALL[k];
	return size;
}

static unsigned int get_pipe_idx(struct display_mode_lib *mode_lib, unsigned int plane_idx)
{
	int pipe_idx = -1;
	int i;

	ASSERT(plane_idx < DC__NUM_DPP__MAX);

	for (i = 0; i < DC__NUM_DPP__MAX ; i++) {
		if (plane_idx == mode_lib->vba.pipe_plane[i]) {
			pipe_idx = i;
			break;
		}
	}
	ASSERT(pipe_idx >= 0);

	return pipe_idx;
}


double get_det_buffer_size_kbytes(struct display_mode_lib *mode_lib, const display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes, unsigned int pipe_idx)
{
	unsigned int plane_idx;
	double det_buf_size_kbytes;

	recalculate_params(mode_lib, pipes, num_pipes);
	plane_idx = mode_lib->vba.pipe_plane[pipe_idx];

	dml_print("DML::%s: num_pipes=%d pipe_idx=%d plane_idx=%0d\n", __func__, num_pipes, pipe_idx, plane_idx);
	det_buf_size_kbytes = mode_lib->vba.DETBufferSizeInKByte[plane_idx]; // per hubp DET buffer size

	dml_print("DML::%s: det_buf_size_kbytes=%3.2f\n", __func__, det_buf_size_kbytes);

	return det_buf_size_kbytes;
}

bool get_is_phantom_pipe(struct display_mode_lib *mode_lib, const display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes, unsigned int pipe_idx)
{
	unsigned int plane_idx;

	recalculate_params(mode_lib, pipes, num_pipes);
	plane_idx = mode_lib->vba.pipe_plane[pipe_idx];
	dml_print("DML::%s: num_pipes=%d pipe_idx=%d UseMALLForPStateChange=%0d\n", __func__, num_pipes, pipe_idx,
			mode_lib->vba.UsesMALLForPStateChange[plane_idx]);
	return (mode_lib->vba.UsesMALLForPStateChange[plane_idx] == dm_use_mall_pstate_change_phantom_pipe);
}

static void fetch_socbb_params(struct display_mode_lib *mode_lib)
{
	soc_bounding_box_st *soc = &mode_lib->vba.soc;
	int i;

	// SOC Bounding Box Parameters
	mode_lib->vba.ReturnBusWidth = soc->return_bus_width_bytes;
	mode_lib->vba.NumberOfChannels = soc->num_chans;
	mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelDataOnly =
			soc->pct_ideal_dram_sdp_bw_after_urgent_pixel_only; // there's always that one bastard variable that's so long it throws everything out of alignment!
	mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData =
			soc->pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm;
	mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly =
			soc->pct_ideal_dram_sdp_bw_after_urgent_vm_only;
	mode_lib->vba.MaxAveragePercentOfIdealSDPPortBWDisplayCanUseInNormalSystemOperation =
			soc->max_avg_sdp_bw_use_normal_percent;
	mode_lib->vba.MaxAveragePercentOfIdealDRAMBWDisplayCanUseInNormalSystemOperation =
			soc->max_avg_dram_bw_use_normal_percent;
	mode_lib->vba.UrgentLatencyPixelDataOnly = soc->urgent_latency_pixel_data_only_us;
	mode_lib->vba.UrgentLatencyPixelMixedWithVMData = soc->urgent_latency_pixel_mixed_with_vm_data_us;
	mode_lib->vba.UrgentLatencyVMDataOnly = soc->urgent_latency_vm_data_only_us;
	mode_lib->vba.RoundTripPingLatencyCycles = soc->round_trip_ping_latency_dcfclk_cycles;
	mode_lib->vba.UrgentOutOfOrderReturnPerChannelPixelDataOnly =
			soc->urgent_out_of_order_return_per_channel_pixel_only_bytes;
	mode_lib->vba.UrgentOutOfOrderReturnPerChannelPixelMixedWithVMData =
			soc->urgent_out_of_order_return_per_channel_pixel_and_vm_bytes;
	mode_lib->vba.UrgentOutOfOrderReturnPerChannelVMDataOnly =
			soc->urgent_out_of_order_return_per_channel_vm_only_bytes;
	mode_lib->vba.WritebackLatency = soc->writeback_latency_us;
	mode_lib->vba.SRExitTime = soc->sr_exit_time_us;
	mode_lib->vba.SREnterPlusExitTime = soc->sr_enter_plus_exit_time_us;
	mode_lib->vba.PercentOfIdealFabricAndSDPPortBWReceivedAfterUrgLatency = soc->pct_ideal_sdp_bw_after_urgent;
	mode_lib->vba.PercentOfIdealDRAMBWReceivedAfterUrgLatencyPixelMixedWithVMData = soc->pct_ideal_dram_sdp_bw_after_urgent_pixel_and_vm;
	mode_lib->vba.PercentOfIdealDRAMBWReceivedAfterUrgLatencyPixelDataOnly = soc->pct_ideal_dram_sdp_bw_after_urgent_pixel_only;
	mode_lib->vba.PercentOfIdealDRAMBWReceivedAfterUrgLatencyVMDataOnly = soc->pct_ideal_dram_sdp_bw_after_urgent_vm_only;
	mode_lib->vba.MaxAveragePercentOfIdealFabricAndSDPPortBWDisplayCanUseInNormalSystemOperation =
			soc->max_avg_sdp_bw_use_normal_percent;
	mode_lib->vba.SRExitZ8Time = soc->sr_exit_z8_time_us;
	mode_lib->vba.SREnterPlusExitZ8Time = soc->sr_enter_plus_exit_z8_time_us;
	mode_lib->vba.FCLKChangeLatency = soc->fclk_change_latency_us;
	mode_lib->vba.USRRetrainingLatency = soc->usr_retraining_latency_us;
	mode_lib->vba.SMNLatency = soc->smn_latency_us;
	mode_lib->vba.MALLAllocatedForDCNFinal = soc->mall_allocated_for_dcn_mbytes;

	mode_lib->vba.PercentOfIdealDRAMBWReceivedAfterUrgLatencySTROBE = soc->pct_ideal_dram_bw_after_urgent_strobe;
	mode_lib->vba.MaxAveragePercentOfIdealFabricBWDisplayCanUseInNormalSystemOperation =
			soc->max_avg_fabric_bw_use_normal_percent;
	mode_lib->vba.MaxAveragePercentOfIdealDRAMBWDisplayCanUseInNormalSystemOperationSTROBE =
			soc->max_avg_dram_bw_use_normal_strobe_percent;

	mode_lib->vba.DRAMClockChangeRequirementFinal = soc->dram_clock_change_requirement_final;
	mode_lib->vba.FCLKChangeRequirementFinal = 1;
	mode_lib->vba.USRRetrainingRequiredFinal = 1;
	mode_lib->vba.AllowForPStateChangeOrStutterInVBlankFinal = soc->allow_for_pstate_or_stutter_in_vblank_final;
	mode_lib->vba.DRAMClockChangeLatency = soc->dram_clock_change_latency_us;
	mode_lib->vba.DummyPStateCheck = soc->dram_clock_change_latency_us == soc->dummy_pstate_latency_us;
	mode_lib->vba.DRAMClockChangeSupportsVActive = !soc->disable_dram_clock_change_vactive_support ||
			mode_lib->vba.DummyPStateCheck;
	mode_lib->vba.AllowDramClockChangeOneDisplayVactive = soc->allow_dram_clock_one_display_vactive;
	mode_lib->vba.AllowDRAMSelfRefreshOrDRAMClockChangeInVblank =
		soc->allow_dram_self_refresh_or_dram_clock_change_in_vblank;

	mode_lib->vba.Downspreading = soc->downspread_percent;
	mode_lib->vba.DRAMChannelWidth = soc->dram_channel_width_bytes;   // new!
	mode_lib->vba.FabricDatapathToDCNDataReturn = soc->fabric_datapath_to_dcn_data_return_bytes; // new!
	mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading = soc->dcn_downspread_percent;   // new
	mode_lib->vba.DISPCLKDPPCLKVCOSpeed = soc->dispclk_dppclk_vco_speed_mhz;   // new
	mode_lib->vba.VMMPageSize = soc->vmm_page_size_bytes;
	mode_lib->vba.GPUVMMinPageSize = soc->gpuvm_min_page_size_bytes / 1024;
	mode_lib->vba.HostVMMinPageSize = soc->hostvm_min_page_size_bytes / 1024;
	// Set the voltage scaling clocks as the defaults. Most of these will
	// be set to different values by the test
	for (i = 0; i < mode_lib->vba.soc.num_states; i++)
		if (soc->clock_limits[i].state == mode_lib->vba.VoltageLevel)
			break;

	mode_lib->vba.DCFCLK = soc->clock_limits[i].dcfclk_mhz;
	mode_lib->vba.SOCCLK = soc->clock_limits[i].socclk_mhz;
	mode_lib->vba.DRAMSpeed = soc->clock_limits[i].dram_speed_mts;
	mode_lib->vba.FabricClock = soc->clock_limits[i].fabricclk_mhz;

	mode_lib->vba.XFCBusTransportTime = soc->xfc_bus_transport_time_us;
	mode_lib->vba.XFCXBUFLatencyTolerance = soc->xfc_xbuf_latency_tolerance_us;
	mode_lib->vba.UseUrgentBurstBandwidth = soc->use_urgent_burst_bw;

	mode_lib->vba.SupportGFX7CompatibleTilingIn32bppAnd64bpp = false;
	mode_lib->vba.WritebackLumaAndChromaScalingSupported = true;
	mode_lib->vba.MaxHSCLRatio = 4;
	mode_lib->vba.MaxVSCLRatio = 4;
	mode_lib->vba.Cursor64BppSupport = true;
	for (i = 0; i <= mode_lib->vba.soc.num_states; i++) {
		mode_lib->vba.DCFCLKPerState[i] = soc->clock_limits[i].dcfclk_mhz;
		mode_lib->vba.FabricClockPerState[i] = soc->clock_limits[i].fabricclk_mhz;
		mode_lib->vba.SOCCLKPerState[i] = soc->clock_limits[i].socclk_mhz;
		mode_lib->vba.PHYCLKPerState[i] = soc->clock_limits[i].phyclk_mhz;
		mode_lib->vba.PHYCLKD18PerState[i] = soc->clock_limits[i].phyclk_d18_mhz;
		mode_lib->vba.PHYCLKD32PerState[i] = soc->clock_limits[i].phyclk_d32_mhz;
		mode_lib->vba.MaxDppclk[i] = soc->clock_limits[i].dppclk_mhz;
		mode_lib->vba.MaxDSCCLK[i] = soc->clock_limits[i].dscclk_mhz;
		mode_lib->vba.DRAMSpeedPerState[i] = soc->clock_limits[i].dram_speed_mts;
		//mode_lib->vba.DRAMSpeedPerState[i] = soc->clock_limits[i].dram_speed_mhz;
		mode_lib->vba.MaxDispclk[i] = soc->clock_limits[i].dispclk_mhz;
		mode_lib->vba.DTBCLKPerState[i] = soc->clock_limits[i].dtbclk_mhz;
	}

	mode_lib->vba.DoUrgentLatencyAdjustment =
		soc->do_urgent_latency_adjustment;
	mode_lib->vba.UrgentLatencyAdjustmentFabricClockComponent =
		soc->urgent_latency_adjustment_fabric_clock_component_us;
	mode_lib->vba.UrgentLatencyAdjustmentFabricClockReference =
		soc->urgent_latency_adjustment_fabric_clock_reference_mhz;
}

static void fetch_ip_params(struct display_mode_lib *mode_lib)
{
	ip_params_st *ip = &mode_lib->vba.ip;

	// IP Parameters
	mode_lib->vba.UseMinimumRequiredDCFCLK = ip->use_min_dcfclk;
	mode_lib->vba.ClampMinDCFCLK = ip->clamp_min_dcfclk;
	mode_lib->vba.MaxNumDPP = ip->max_num_dpp;
	mode_lib->vba.MaxNumOTG = ip->max_num_otg;
	mode_lib->vba.MaxNumHDMIFRLOutputs = ip->max_num_hdmi_frl_outputs;
	mode_lib->vba.MaxNumWriteback = ip->max_num_wb;
	mode_lib->vba.CursorChunkSize = ip->cursor_chunk_size;
	mode_lib->vba.CursorBufferSize = ip->cursor_buffer_size;

	mode_lib->vba.MaxDCHUBToPSCLThroughput = ip->max_dchub_pscl_bw_pix_per_clk;
	mode_lib->vba.MaxPSCLToLBThroughput = ip->max_pscl_lb_bw_pix_per_clk;
	mode_lib->vba.ROBBufferSizeInKByte = ip->rob_buffer_size_kbytes;
	mode_lib->vba.DETBufferSizeInKByte[0] = ip->det_buffer_size_kbytes;
	mode_lib->vba.ConfigReturnBufferSizeInKByte = ip->config_return_buffer_size_in_kbytes;
	mode_lib->vba.CompressedBufferSegmentSizeInkByte = ip->compressed_buffer_segment_size_in_kbytes;
	mode_lib->vba.MetaFIFOSizeInKEntries = ip->meta_fifo_size_in_kentries;
	mode_lib->vba.ZeroSizeBufferEntries = ip->zero_size_buffer_entries;
	mode_lib->vba.COMPBUF_RESERVED_SPACE_64B = ip->compbuf_reserved_space_64b;
	mode_lib->vba.COMPBUF_RESERVED_SPACE_ZS = ip->compbuf_reserved_space_zs;
	mode_lib->vba.MaximumDSCBitsPerComponent = ip->maximum_dsc_bits_per_component;
	mode_lib->vba.DSC422NativeSupport = ip->dsc422_native_support;
    /* In DCN3.2, nomDETInKByte should be initialized correctly. */
	mode_lib->vba.nomDETInKByte = ip->det_buffer_size_kbytes;
	mode_lib->vba.CompbufReservedSpace64B  = ip->compbuf_reserved_space_64b;
	mode_lib->vba.CompbufReservedSpaceZs = ip->compbuf_reserved_space_zs;
	mode_lib->vba.CompressedBufferSegmentSizeInkByteFinal = ip->compressed_buffer_segment_size_in_kbytes;
	mode_lib->vba.LineBufferSizeFinal = ip->line_buffer_size_bits;
	mode_lib->vba.AlphaPixelChunkSizeInKByte = ip->alpha_pixel_chunk_size_kbytes; // not ysed
	mode_lib->vba.MinPixelChunkSizeBytes = ip->min_pixel_chunk_size_bytes; // not used
	mode_lib->vba.MaximumPixelsPerLinePerDSCUnit = ip->maximum_pixels_per_line_per_dsc_unit;
	mode_lib->vba.MaxNumDP2p0Outputs = ip->max_num_dp2p0_outputs;
	mode_lib->vba.MaxNumDP2p0Streams = ip->max_num_dp2p0_streams;
	mode_lib->vba.DCCMetaBufferSizeBytes = ip->dcc_meta_buffer_size_bytes;

	mode_lib->vba.PixelChunkSizeInKByte = ip->pixel_chunk_size_kbytes;
	mode_lib->vba.MetaChunkSize = ip->meta_chunk_size_kbytes;
	mode_lib->vba.MinMetaChunkSizeBytes = ip->min_meta_chunk_size_bytes;
	mode_lib->vba.WritebackChunkSize = ip->writeback_chunk_size_kbytes;
	mode_lib->vba.LineBufferSize = ip->line_buffer_size_bits;
	mode_lib->vba.MaxLineBufferLines = ip->max_line_buffer_lines;
	mode_lib->vba.PTEBufferSizeInRequestsLuma = ip->dpte_buffer_size_in_pte_reqs_luma;
	mode_lib->vba.PTEBufferSizeInRequestsChroma = ip->dpte_buffer_size_in_pte_reqs_chroma;
	mode_lib->vba.DPPOutputBufferPixels = ip->dpp_output_buffer_pixels;
	mode_lib->vba.OPPOutputBufferLines = ip->opp_output_buffer_lines;
	mode_lib->vba.MaxHSCLRatio = ip->max_hscl_ratio;
	mode_lib->vba.MaxVSCLRatio = ip->max_vscl_ratio;
	mode_lib->vba.WritebackInterfaceLumaBufferSize = ip->writeback_luma_buffer_size_kbytes * 1024;
	mode_lib->vba.WritebackInterfaceChromaBufferSize = ip->writeback_chroma_buffer_size_kbytes * 1024;

	mode_lib->vba.WritebackInterfaceBufferSize = ip->writeback_interface_buffer_size_kbytes;
	mode_lib->vba.WritebackLineBufferSize = ip->writeback_line_buffer_buffer_size;

	mode_lib->vba.WritebackChromaLineBufferWidth =
			ip->writeback_chroma_line_buffer_width_pixels;
	mode_lib->vba.WritebackLineBufferLumaBufferSize =
			ip->writeback_line_buffer_luma_buffer_size;
	mode_lib->vba.WritebackLineBufferChromaBufferSize =
			ip->writeback_line_buffer_chroma_buffer_size;
	mode_lib->vba.Writeback10bpc420Supported = ip->writeback_10bpc420_supported;
	mode_lib->vba.WritebackMaxHSCLRatio = ip->writeback_max_hscl_ratio;
	mode_lib->vba.WritebackMaxVSCLRatio = ip->writeback_max_vscl_ratio;
	mode_lib->vba.WritebackMinHSCLRatio = ip->writeback_min_hscl_ratio;
	mode_lib->vba.WritebackMinVSCLRatio = ip->writeback_min_vscl_ratio;
	mode_lib->vba.WritebackMaxHSCLTaps = ip->writeback_max_hscl_taps;
	mode_lib->vba.WritebackMaxVSCLTaps = ip->writeback_max_vscl_taps;
	mode_lib->vba.WritebackConfiguration = dm_normal;
	mode_lib->vba.GPUVMMaxPageTableLevels = ip->gpuvm_max_page_table_levels;
	mode_lib->vba.HostVMMaxNonCachedPageTableLevels = ip->hostvm_max_page_table_levels;
	mode_lib->vba.HostVMMaxPageTableLevels = ip->hostvm_max_page_table_levels;
	mode_lib->vba.HostVMCachedPageTableLevels = ip->hostvm_cached_page_table_levels;
	mode_lib->vba.MaxInterDCNTileRepeaters = ip->max_inter_dcn_tile_repeaters;
	mode_lib->vba.NumberOfDSC = ip->num_dsc;
	mode_lib->vba.ODMCapability = ip->odm_capable;
	mode_lib->vba.DISPCLKRampingMargin = ip->dispclk_ramp_margin_percent;

	mode_lib->vba.XFCSupported = ip->xfc_supported;
	mode_lib->vba.XFCFillBWOverhead = ip->xfc_fill_bw_overhead_percent;
	mode_lib->vba.XFCFillConstant = ip->xfc_fill_constant_bytes;
	mode_lib->vba.DPPCLKDelaySubtotal = ip->dppclk_delay_subtotal;
	mode_lib->vba.DPPCLKDelaySCL = ip->dppclk_delay_scl;
	mode_lib->vba.DPPCLKDelaySCLLBOnly = ip->dppclk_delay_scl_lb_only;
	mode_lib->vba.DPPCLKDelayCNVCFormater = ip->dppclk_delay_cnvc_formatter;
	mode_lib->vba.DPPCLKDelayCNVCCursor = ip->dppclk_delay_cnvc_cursor;
	mode_lib->vba.DISPCLKDelaySubtotal = ip->dispclk_delay_subtotal;
	mode_lib->vba.DynamicMetadataVMEnabled = ip->dynamic_metadata_vm_enabled;
	mode_lib->vba.ODMCombine4To1Supported = ip->odm_combine_4to1_supported;
	mode_lib->vba.ProgressiveToInterlaceUnitInOPP = ip->ptoi_supported;
	mode_lib->vba.PDEProcessingBufIn64KBReqs = ip->pde_proc_buffer_size_64k_reqs;
	mode_lib->vba.PTEGroupSize = ip->pte_group_size_bytes;
	mode_lib->vba.SupportGFX7CompatibleTilingIn32bppAnd64bpp = ip->gfx7_compat_tiling_supported;
}

static void fetch_pipe_params(struct display_mode_lib *mode_lib)
{
	display_e2e_pipe_params_st *pipes = mode_lib->vba.cache_pipes;
	ip_params_st *ip = &mode_lib->vba.ip;

	unsigned int OTGInstPlane[DC__NUM_DPP__MAX];
	unsigned int j, k;
	bool PlaneVisited[DC__NUM_DPP__MAX];
	bool visited[DC__NUM_DPP__MAX];

	// Convert Pipes to Planes
	for (k = 0; k < mode_lib->vba.cache_num_pipes; ++k)
		visited[k] = false;

	mode_lib->vba.NumberOfActivePlanes = 0;
	mode_lib->vba.NumberOfActiveSurfaces = 0;
	mode_lib->vba.ImmediateFlipSupport = false;
	for (j = 0; j < mode_lib->vba.cache_num_pipes; ++j) {
		display_pipe_source_params_st *src = &pipes[j].pipe.src;
		display_pipe_dest_params_st *dst = &pipes[j].pipe.dest;
		scaler_ratio_depth_st *scl = &pipes[j].pipe.scale_ratio_depth;
		scaler_taps_st *taps = &pipes[j].pipe.scale_taps;
		display_output_params_st *dout = &pipes[j].dout;
		display_clocks_and_cfg_st *clks = &pipes[j].clks_cfg;

		if (visited[j])
			continue;
		visited[j] = true;

		mode_lib->vba.ImmediateFlipRequirement[j] = dm_immediate_flip_not_required;
		mode_lib->vba.pipe_plane[j] = mode_lib->vba.NumberOfActivePlanes;
		mode_lib->vba.DPPPerPlane[mode_lib->vba.NumberOfActivePlanes] = 1;
		mode_lib->vba.SourceScan[mode_lib->vba.NumberOfActivePlanes] =
				(enum scan_direction_class) (src->source_scan);
		mode_lib->vba.ViewportWidth[mode_lib->vba.NumberOfActivePlanes] =
				src->viewport_width;
		mode_lib->vba.ViewportWidthChroma[mode_lib->vba.NumberOfActivePlanes] =
				src->viewport_width_c;
		mode_lib->vba.ViewportHeight[mode_lib->vba.NumberOfActivePlanes] =
				src->viewport_height;
		mode_lib->vba.ViewportHeightChroma[mode_lib->vba.NumberOfActivePlanes] =
				src->viewport_height_c;
		mode_lib->vba.ViewportYStartY[mode_lib->vba.NumberOfActivePlanes] =
				src->viewport_y_y;
		mode_lib->vba.ViewportYStartC[mode_lib->vba.NumberOfActivePlanes] =
				src->viewport_y_c;
		mode_lib->vba.SourceRotation[mode_lib->vba.NumberOfActiveSurfaces] = src->source_rotation;
		mode_lib->vba.ViewportXStartY[mode_lib->vba.NumberOfActiveSurfaces] = src->viewport_x_y;
		mode_lib->vba.ViewportXStartC[mode_lib->vba.NumberOfActiveSurfaces] = src->viewport_x_c;
		// TODO: Assign correct value to viewport_stationary
		mode_lib->vba.ViewportStationary[mode_lib->vba.NumberOfActivePlanes] =
				src->viewport_stationary;
		mode_lib->vba.UsesMALLForPStateChange[mode_lib->vba.NumberOfActivePlanes] = src->use_mall_for_pstate_change;
		mode_lib->vba.UseMALLForStaticScreen[mode_lib->vba.NumberOfActivePlanes] = src->use_mall_for_static_screen;
		mode_lib->vba.GPUVMMinPageSizeKBytes[mode_lib->vba.NumberOfActivePlanes] = src->gpuvm_min_page_size_kbytes;
		mode_lib->vba.RefreshRate[mode_lib->vba.NumberOfActivePlanes] = dst->refresh_rate; //todo remove this
		mode_lib->vba.OutputLinkDPRate[mode_lib->vba.NumberOfActivePlanes] = dout->dp_rate;
		mode_lib->vba.ODMUse[mode_lib->vba.NumberOfActivePlanes] = dst->odm_combine_policy;
		mode_lib->vba.DETSizeOverride[mode_lib->vba.NumberOfActivePlanes] = src->det_size_override;
		if (src->det_size_override)
			mode_lib->vba.DETBufferSizeInKByte[mode_lib->vba.NumberOfActivePlanes] = src->det_size_override;
		else
			mode_lib->vba.DETBufferSizeInKByte[mode_lib->vba.NumberOfActivePlanes] = ip->det_buffer_size_kbytes;
		//TODO: Need to assign correct values to dp_multistream vars
		mode_lib->vba.OutputMultistreamEn[mode_lib->vba.NumberOfActiveSurfaces] = dout->dp_multistream_en;
		mode_lib->vba.OutputMultistreamId[mode_lib->vba.NumberOfActiveSurfaces] = dout->dp_multistream_id;
		mode_lib->vba.PitchY[mode_lib->vba.NumberOfActivePlanes] = src->data_pitch;
		mode_lib->vba.SurfaceWidthY[mode_lib->vba.NumberOfActivePlanes] = src->surface_width_y;
		mode_lib->vba.SurfaceHeightY[mode_lib->vba.NumberOfActivePlanes] = src->surface_height_y;
		mode_lib->vba.PitchC[mode_lib->vba.NumberOfActivePlanes] = src->data_pitch_c;
		mode_lib->vba.SurfaceHeightC[mode_lib->vba.NumberOfActivePlanes] = src->surface_height_c;
		mode_lib->vba.SurfaceWidthC[mode_lib->vba.NumberOfActivePlanes] = src->surface_width_c;
		mode_lib->vba.DCCMetaPitchY[mode_lib->vba.NumberOfActivePlanes] = src->meta_pitch;
		mode_lib->vba.DCCMetaPitchC[mode_lib->vba.NumberOfActivePlanes] = src->meta_pitch_c;
		mode_lib->vba.HRatio[mode_lib->vba.NumberOfActivePlanes] = scl->hscl_ratio;
		mode_lib->vba.HRatioChroma[mode_lib->vba.NumberOfActivePlanes] = scl->hscl_ratio_c;
		mode_lib->vba.VRatio[mode_lib->vba.NumberOfActivePlanes] = scl->vscl_ratio;
		mode_lib->vba.VRatioChroma[mode_lib->vba.NumberOfActivePlanes] = scl->vscl_ratio_c;
		mode_lib->vba.ScalerEnabled[mode_lib->vba.NumberOfActivePlanes] = scl->scl_enable;
		mode_lib->vba.Interlace[mode_lib->vba.NumberOfActivePlanes] = dst->interlaced;
		if (dst->interlaced && !ip->ptoi_supported) {
			mode_lib->vba.VRatio[mode_lib->vba.NumberOfActivePlanes] *= 2.0;
			mode_lib->vba.VRatioChroma[mode_lib->vba.NumberOfActivePlanes] *= 2.0;
		}
		mode_lib->vba.htaps[mode_lib->vba.NumberOfActivePlanes] = taps->htaps;
		mode_lib->vba.vtaps[mode_lib->vba.NumberOfActivePlanes] = taps->vtaps;
		mode_lib->vba.HTAPsChroma[mode_lib->vba.NumberOfActivePlanes] = taps->htaps_c;
		mode_lib->vba.VTAPsChroma[mode_lib->vba.NumberOfActivePlanes] = taps->vtaps_c;
		mode_lib->vba.HTotal[mode_lib->vba.NumberOfActivePlanes] = dst->htotal;
		mode_lib->vba.VTotal[mode_lib->vba.NumberOfActivePlanes] = dst->vtotal;
		mode_lib->vba.VFrontPorch[mode_lib->vba.NumberOfActivePlanes] = dst->vfront_porch;
		mode_lib->vba.VBlankNom[mode_lib->vba.NumberOfActivePlanes] = dst->vblank_nom;
		mode_lib->vba.DCCFractionOfZeroSizeRequestsLuma[mode_lib->vba.NumberOfActivePlanes] = src->dcc_fraction_of_zs_req_luma;
		mode_lib->vba.DCCFractionOfZeroSizeRequestsChroma[mode_lib->vba.NumberOfActivePlanes] = src->dcc_fraction_of_zs_req_chroma;
		mode_lib->vba.DCCEnable[mode_lib->vba.NumberOfActivePlanes] =
				src->dcc_use_global ?
						ip->dcc_supported : src->dcc && ip->dcc_supported;
		mode_lib->vba.DCCRate[mode_lib->vba.NumberOfActivePlanes] = src->dcc_rate;
		/* TODO: Needs to be set based on src->dcc_rate_luma/chroma */
		mode_lib->vba.DCCRateLuma[mode_lib->vba.NumberOfActivePlanes] = src->dcc_rate;
		mode_lib->vba.DCCRateChroma[mode_lib->vba.NumberOfActivePlanes] = src->dcc_rate_chroma;
		mode_lib->vba.SourcePixelFormat[mode_lib->vba.NumberOfActivePlanes] = (enum source_format_class) (src->source_format);
		mode_lib->vba.HActive[mode_lib->vba.NumberOfActivePlanes] = dst->hactive;
		mode_lib->vba.VActive[mode_lib->vba.NumberOfActivePlanes] = dst->vactive;
		mode_lib->vba.SurfaceTiling[mode_lib->vba.NumberOfActivePlanes] =
				(enum dm_swizzle_mode) (src->sw_mode);
		mode_lib->vba.ScalerRecoutWidth[mode_lib->vba.NumberOfActivePlanes] =
				dst->recout_width; // TODO: or should this be full_recout_width???...maybe only when in hsplit mode?
		mode_lib->vba.ODMCombineEnabled[mode_lib->vba.NumberOfActivePlanes] =
				dst->odm_combine;
		mode_lib->vba.OutputFormat[mode_lib->vba.NumberOfActivePlanes] =
				(enum output_format_class) (dout->output_format);
		mode_lib->vba.OutputBpp[mode_lib->vba.NumberOfActivePlanes] =
				dout->output_bpp;
		mode_lib->vba.Output[mode_lib->vba.NumberOfActivePlanes] =
				(enum output_encoder_class) (dout->output_type);
		mode_lib->vba.skip_dio_check[mode_lib->vba.NumberOfActivePlanes] =
				dout->is_virtual;

		if (dout->dsc_enable)
			mode_lib->vba.ForcedOutputLinkBPP[mode_lib->vba.NumberOfActivePlanes] = dout->output_bpp;
		else
			mode_lib->vba.ForcedOutputLinkBPP[mode_lib->vba.NumberOfActivePlanes] = 0.0;

		mode_lib->vba.OutputLinkDPLanes[mode_lib->vba.NumberOfActivePlanes] =
				dout->dp_lanes;
		/* TODO: Needs to be set based on dout->audio.audio_sample_rate_khz/sample_layout */
		mode_lib->vba.AudioSampleRate[mode_lib->vba.NumberOfActivePlanes] =
			dout->max_audio_sample_rate;
		mode_lib->vba.AudioSampleLayout[mode_lib->vba.NumberOfActivePlanes] =
			1;
		mode_lib->vba.DRAMClockChangeLatencyOverride = 0.0;
		mode_lib->vba.DSCEnabled[mode_lib->vba.NumberOfActivePlanes] = dout->dsc_enable;
		mode_lib->vba.DSCEnable[mode_lib->vba.NumberOfActivePlanes] = dout->dsc_enable;
		mode_lib->vba.NumberOfDSCSlices[mode_lib->vba.NumberOfActivePlanes] =
				dout->dsc_slices;
		if (!dout->dsc_input_bpc) {
			mode_lib->vba.DSCInputBitPerComponent[mode_lib->vba.NumberOfActivePlanes] =
				ip->maximum_dsc_bits_per_component;
		} else {
			mode_lib->vba.DSCInputBitPerComponent[mode_lib->vba.NumberOfActivePlanes] =
				dout->dsc_input_bpc;
		}
		mode_lib->vba.WritebackEnable[mode_lib->vba.NumberOfActivePlanes] = dout->wb_enable;
		mode_lib->vba.ActiveWritebacksPerPlane[mode_lib->vba.NumberOfActivePlanes] =
				dout->num_active_wb;
		mode_lib->vba.WritebackSourceHeight[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_src_height;
		mode_lib->vba.WritebackSourceWidth[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_src_width;
		mode_lib->vba.WritebackDestinationWidth[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_dst_width;
		mode_lib->vba.WritebackDestinationHeight[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_dst_height;
		mode_lib->vba.WritebackHRatio[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_hratio;
		mode_lib->vba.WritebackVRatio[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_vratio;
		mode_lib->vba.WritebackPixelFormat[mode_lib->vba.NumberOfActivePlanes] =
				(enum source_format_class) (dout->wb.wb_pixel_format);
		mode_lib->vba.WritebackHTaps[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_htaps_luma;
		mode_lib->vba.WritebackVTaps[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_vtaps_luma;
		mode_lib->vba.WritebackLumaHTaps[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_htaps_luma;
		mode_lib->vba.WritebackLumaVTaps[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_vtaps_luma;
		mode_lib->vba.WritebackChromaHTaps[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_htaps_chroma;
		mode_lib->vba.WritebackChromaVTaps[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_vtaps_chroma;
		mode_lib->vba.WritebackHRatio[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_hratio;
		mode_lib->vba.WritebackVRatio[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_vratio;

		mode_lib->vba.DynamicMetadataEnable[mode_lib->vba.NumberOfActivePlanes] =
				src->dynamic_metadata_enable;
		mode_lib->vba.DynamicMetadataLinesBeforeActiveRequired[mode_lib->vba.NumberOfActivePlanes] =
				src->dynamic_metadata_lines_before_active;
		mode_lib->vba.DynamicMetadataTransmittedBytes[mode_lib->vba.NumberOfActivePlanes] =
				src->dynamic_metadata_xmit_bytes;

		mode_lib->vba.XFCEnabled[mode_lib->vba.NumberOfActivePlanes] = src->xfc_enable
				&& ip->xfc_supported;
		mode_lib->vba.XFCSlvChunkSize = src->xfc_params.xfc_slv_chunk_size_bytes;
		mode_lib->vba.XFCTSlvVupdateOffset = src->xfc_params.xfc_tslv_vupdate_offset_us;
		mode_lib->vba.XFCTSlvVupdateWidth = src->xfc_params.xfc_tslv_vupdate_width_us;
		mode_lib->vba.XFCTSlvVreadyOffset = src->xfc_params.xfc_tslv_vready_offset_us;
		mode_lib->vba.PixelClock[mode_lib->vba.NumberOfActivePlanes] = dst->pixel_rate_mhz;
		mode_lib->vba.PixelClockBackEnd[mode_lib->vba.NumberOfActivePlanes] = dst->pixel_rate_mhz;
		mode_lib->vba.DPPCLK[mode_lib->vba.NumberOfActivePlanes] = clks->dppclk_mhz;
		mode_lib->vba.DRRDisplay[mode_lib->vba.NumberOfActiveSurfaces] = dst->drr_display;
		if (ip->is_line_buffer_bpp_fixed)
			mode_lib->vba.LBBitPerPixel[mode_lib->vba.NumberOfActivePlanes] =
					ip->line_buffer_fixed_bpp;
		else {
			unsigned int lb_depth;

			switch (scl->lb_depth) {
			case dm_lb_6:
				lb_depth = 18;
				break;
			case dm_lb_8:
				lb_depth = 24;
				break;
			case dm_lb_10:
				lb_depth = 30;
				break;
			case dm_lb_12:
				lb_depth = 36;
				break;
			case dm_lb_16:
				lb_depth = 48;
				break;
			case dm_lb_19:
				lb_depth = 57;
				break;
			default:
				lb_depth = 36;
			}
			mode_lib->vba.LBBitPerPixel[mode_lib->vba.NumberOfActivePlanes] = lb_depth;
		}
		mode_lib->vba.NumberOfCursors[mode_lib->vba.NumberOfActivePlanes] = 0;
		// The DML spreadsheet assumes that the two cursors utilize the same amount of bandwidth. We'll
		// calculate things a little more accurately
		for (k = 0; k < DC__NUM_CURSOR__MAX; ++k) {
			switch (k) {
			case 0:
				mode_lib->vba.CursorBPP[mode_lib->vba.NumberOfActivePlanes][0] =
						CursorBppEnumToBits(
								(enum cursor_bpp) (src->cur0_bpp));
				mode_lib->vba.CursorWidth[mode_lib->vba.NumberOfActivePlanes][0] =
						src->cur0_src_width;
				if (src->cur0_src_width > 0)
					mode_lib->vba.NumberOfCursors[mode_lib->vba.NumberOfActivePlanes]++;
				break;
			case 1:
				mode_lib->vba.CursorBPP[mode_lib->vba.NumberOfActivePlanes][1] =
						CursorBppEnumToBits(
								(enum cursor_bpp) (src->cur1_bpp));
				mode_lib->vba.CursorWidth[mode_lib->vba.NumberOfActivePlanes][1] =
						src->cur1_src_width;
				if (src->cur1_src_width > 0)
					mode_lib->vba.NumberOfCursors[mode_lib->vba.NumberOfActivePlanes]++;
				break;
			default:
				dml_print(
						"ERROR: Number of cursors specified exceeds supported maximum\n")
				;
			}
		}

		OTGInstPlane[mode_lib->vba.NumberOfActivePlanes] = dst->otg_inst;

		if (j == 0)
			mode_lib->vba.UseMaximumVStartup = dst->use_maximum_vstartup;
		else
			mode_lib->vba.UseMaximumVStartup = mode_lib->vba.UseMaximumVStartup
									|| dst->use_maximum_vstartup;

		if (dst->odm_combine && !src->is_hsplit)
			dml_print(
					"ERROR: ODM Combine is specified but is_hsplit has not be specified for pipe %i\n",
					j);

		if (src->is_hsplit) {
			for (k = j + 1; k < mode_lib->vba.cache_num_pipes; ++k) {
				display_pipe_source_params_st *src_k = &pipes[k].pipe.src;
				display_pipe_dest_params_st *dst_k = &pipes[k].pipe.dest;

				if (src_k->is_hsplit && !visited[k]
						&& src->hsplit_grp == src_k->hsplit_grp) {
					mode_lib->vba.pipe_plane[k] =
							mode_lib->vba.NumberOfActivePlanes;
					mode_lib->vba.DPPPerPlane[mode_lib->vba.NumberOfActivePlanes]++;
					if (src_k->det_size_override)
						mode_lib->vba.DETBufferSizeInKByte[mode_lib->vba.NumberOfActivePlanes] = src_k->det_size_override;
					if (mode_lib->vba.SourceScan[mode_lib->vba.NumberOfActivePlanes]
							== dm_horz) {
						mode_lib->vba.ViewportWidth[mode_lib->vba.NumberOfActivePlanes] +=
								src_k->viewport_width;
						mode_lib->vba.ViewportWidthChroma[mode_lib->vba.NumberOfActivePlanes] +=
								src_k->viewport_width_c;
						mode_lib->vba.ScalerRecoutWidth[mode_lib->vba.NumberOfActivePlanes] +=
								dst_k->recout_width;
					} else {
						mode_lib->vba.ViewportHeight[mode_lib->vba.NumberOfActivePlanes] +=
								src_k->viewport_height;
						mode_lib->vba.ViewportHeightChroma[mode_lib->vba.NumberOfActivePlanes] +=
								src_k->viewport_height_c;
					}

					visited[k] = true;
				}
			}
		}
		if (src->viewport_width_max) {
			int hdiv_c = src->source_format >= dm_420_8 && src->source_format <= dm_422_10 ? 2 : 1;
			int vdiv_c = src->source_format >= dm_420_8 && src->source_format <= dm_420_12 ? 2 : 1;

			if (mode_lib->vba.ViewportWidth[mode_lib->vba.NumberOfActivePlanes] > src->viewport_width_max)
				mode_lib->vba.ViewportWidth[mode_lib->vba.NumberOfActivePlanes] = src->viewport_width_max;
			if (mode_lib->vba.ViewportHeight[mode_lib->vba.NumberOfActivePlanes] > src->viewport_height_max)
				mode_lib->vba.ViewportHeight[mode_lib->vba.NumberOfActivePlanes] = src->viewport_height_max;
			if (mode_lib->vba.ViewportWidthChroma[mode_lib->vba.NumberOfActivePlanes] > src->viewport_width_max / hdiv_c)
				mode_lib->vba.ViewportWidthChroma[mode_lib->vba.NumberOfActivePlanes] = src->viewport_width_max / hdiv_c;
			if (mode_lib->vba.ViewportHeightChroma[mode_lib->vba.NumberOfActivePlanes] > src->viewport_height_max / vdiv_c)
				mode_lib->vba.ViewportHeightChroma[mode_lib->vba.NumberOfActivePlanes] = src->viewport_height_max / vdiv_c;
		}

		if (pipes[j].pipe.src.immediate_flip) {
			mode_lib->vba.ImmediateFlipSupport = true;
			mode_lib->vba.ImmediateFlipRequirement[j] = dm_immediate_flip_required;
		}

		mode_lib->vba.NumberOfActivePlanes++;
		mode_lib->vba.NumberOfActiveSurfaces++;
	}

	// handle overlays through BlendingAndTiming
	// BlendingAndTiming tells you which instance to look at to get timing, the so called 'master'

	for (j = 0; j < mode_lib->vba.NumberOfActivePlanes; ++j)
		PlaneVisited[j] = false;

	for (j = 0; j < mode_lib->vba.NumberOfActivePlanes; ++j) {
		for (k = j + 1; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
			if (!PlaneVisited[k] && OTGInstPlane[j] == OTGInstPlane[k]) {
				// doesn't matter, so choose the smaller one
				mode_lib->vba.BlendingAndTiming[j] = j;
				PlaneVisited[j] = true;
				mode_lib->vba.BlendingAndTiming[k] = j;
				PlaneVisited[k] = true;
			}
		}

		if (!PlaneVisited[j]) {
			mode_lib->vba.BlendingAndTiming[j] = j;
			PlaneVisited[j] = true;
		}
	}

	mode_lib->vba.SynchronizeTimingsFinal = pipes[0].pipe.dest.synchronize_timings;
	mode_lib->vba.DCCProgrammingAssumesScanDirectionUnknownFinal = false;

	mode_lib->vba.DisableUnboundRequestIfCompBufReservedSpaceNeedAdjustment = 0;

	mode_lib->vba.UseUnboundedRequesting = dm_unbounded_requesting;
	for (k = 0; k < mode_lib->vba.cache_num_pipes; ++k) {
		if (pipes[k].pipe.src.unbounded_req_mode == 0)
			mode_lib->vba.UseUnboundedRequesting = dm_unbounded_requesting_disable;
	}
	// TODO: ODMCombineEnabled => 2 * DPPPerPlane...actually maybe not since all pipes are specified
	// Do we want the dscclk to automatically be halved? Guess not since the value is specified
	mode_lib->vba.SynchronizedVBlank = pipes[0].pipe.dest.synchronized_vblank_all_planes;
	for (k = 1; k < mode_lib->vba.cache_num_pipes; ++k) {
		ASSERT(mode_lib->vba.SynchronizedVBlank == pipes[k].pipe.dest.synchronized_vblank_all_planes);
	}

	mode_lib->vba.GPUVMEnable = false;
	mode_lib->vba.HostVMEnable = false;
	mode_lib->vba.OverrideGPUVMPageTableLevels = 0;
	mode_lib->vba.OverrideHostVMPageTableLevels = 0;

	for (k = 0; k < mode_lib->vba.cache_num_pipes; ++k) {
		mode_lib->vba.GPUVMEnable = mode_lib->vba.GPUVMEnable || !!pipes[k].pipe.src.gpuvm || !!pipes[k].pipe.src.vm;
		mode_lib->vba.OverrideGPUVMPageTableLevels =
				(pipes[k].pipe.src.gpuvm_levels_force_en
						&& mode_lib->vba.OverrideGPUVMPageTableLevels
								< pipes[k].pipe.src.gpuvm_levels_force) ?
						pipes[k].pipe.src.gpuvm_levels_force :
						mode_lib->vba.OverrideGPUVMPageTableLevels;

		mode_lib->vba.HostVMEnable = mode_lib->vba.HostVMEnable || !!pipes[k].pipe.src.hostvm || !!pipes[k].pipe.src.vm;
		mode_lib->vba.OverrideHostVMPageTableLevels =
				(pipes[k].pipe.src.hostvm_levels_force_en
						&& mode_lib->vba.OverrideHostVMPageTableLevels
								< pipes[k].pipe.src.hostvm_levels_force) ?
						pipes[k].pipe.src.hostvm_levels_force :
						mode_lib->vba.OverrideHostVMPageTableLevels;
	}

	if (mode_lib->vba.OverrideGPUVMPageTableLevels)
		mode_lib->vba.GPUVMMaxPageTableLevels = mode_lib->vba.OverrideGPUVMPageTableLevels;

	if (mode_lib->vba.OverrideHostVMPageTableLevels)
		mode_lib->vba.HostVMMaxPageTableLevels = mode_lib->vba.OverrideHostVMPageTableLevels;

	mode_lib->vba.GPUVMEnable = mode_lib->vba.GPUVMEnable && !!ip->gpuvm_enable;
	mode_lib->vba.HostVMEnable = mode_lib->vba.HostVMEnable && !!ip->hostvm_enable;

	for (k = 0; k < mode_lib->vba.cache_num_pipes; ++k) {
		mode_lib->vba.ForceOneRowForFrame[k] = pipes[k].pipe.src.force_one_row_for_frame;
		mode_lib->vba.PteBufferMode[k] = pipes[k].pipe.src.pte_buffer_mode;

		if (mode_lib->vba.PteBufferMode[k] == 0 && mode_lib->vba.GPUVMEnable) {
			if (mode_lib->vba.ForceOneRowForFrame[k] ||
				(mode_lib->vba.GPUVMMinPageSizeKBytes[k] > 64*1024) ||
				(mode_lib->vba.UsesMALLForPStateChange[k] != dm_use_mall_pstate_change_disable) ||
				(mode_lib->vba.UseMALLForStaticScreen[k] != dm_use_mall_static_screen_disable)) {
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: ERROR: Invalid PteBufferMode=%d for plane %0d!\n",
						__func__, mode_lib->vba.PteBufferMode[k], k);
				dml_print("DML::%s:  -  ForceOneRowForFrame     = %d\n",
						__func__, mode_lib->vba.ForceOneRowForFrame[k]);
				dml_print("DML::%s:  -  GPUVMMinPageSizeKBytes  = %d\n",
						__func__, mode_lib->vba.GPUVMMinPageSizeKBytes[k]);
				dml_print("DML::%s:  -  UseMALLForPStateChange  = %d\n",
						__func__, (int) mode_lib->vba.UsesMALLForPStateChange[k]);
				dml_print("DML::%s:  -  UseMALLForStaticScreen  = %d\n",
						__func__, (int) mode_lib->vba.UseMALLForStaticScreen[k]);
#endif
				ASSERT(0);
			}
		}
	}
}

/**
 * ********************************************************************************************
 * cache_debug_params: Cache any params that needed to be maintained from the initial validation
 * for debug purposes.
 *
 * The DML getters can modify some of the VBA params that we are interested in (for example when
 * calculating with dummy p-state latency), so cache any params here that we want for debugging
 *
 * @param [in] mode_lib: mode_lib input/output of validate call
 *
 * @return: void
 *
 * ********************************************************************************************
 */
static void cache_debug_params(struct display_mode_lib *mode_lib)
{
	int k = 0;

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; k++)
		mode_lib->vba.CachedActiveDRAMClockChangeLatencyMargin[k] = mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k];
}

// in wm mode we pull the parameters needed from the display_e2e_pipe_params_st structs
// rather than working them out as in recalculate_ms
static void recalculate_params(
		struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes)
{
	// This is only safe to use memcmp because there are non-POD types in struct display_mode_lib
	if (memcmp(&mode_lib->soc, &mode_lib->vba.soc, sizeof(mode_lib->vba.soc)) != 0
			|| memcmp(&mode_lib->ip, &mode_lib->vba.ip, sizeof(mode_lib->vba.ip)) != 0
			|| num_pipes != mode_lib->vba.cache_num_pipes
			|| memcmp(
					pipes,
					mode_lib->vba.cache_pipes,
					sizeof(display_e2e_pipe_params_st) * num_pipes) != 0) {
		mode_lib->vba.soc = mode_lib->soc;
		mode_lib->vba.ip = mode_lib->ip;
		memcpy(mode_lib->vba.cache_pipes, pipes, sizeof(*pipes) * num_pipes);
		mode_lib->vba.cache_num_pipes = num_pipes;
		mode_lib->funcs.recalculate(mode_lib);
	}
}

void Calculate256BBlockSizes(
		enum source_format_class SourcePixelFormat,
		enum dm_swizzle_mode SurfaceTiling,
		unsigned int BytePerPixelY,
		unsigned int BytePerPixelC,
		unsigned int *BlockHeight256BytesY,
		unsigned int *BlockHeight256BytesC,
		unsigned int *BlockWidth256BytesY,
		unsigned int *BlockWidth256BytesC)
{
	if ((SourcePixelFormat == dm_444_64 || SourcePixelFormat == dm_444_32
			|| SourcePixelFormat == dm_444_16 || SourcePixelFormat == dm_444_8)) {
		if (SurfaceTiling == dm_sw_linear) {
			*BlockHeight256BytesY = 1;
		} else if (SourcePixelFormat == dm_444_64) {
			*BlockHeight256BytesY = 4;
		} else if (SourcePixelFormat == dm_444_8) {
			*BlockHeight256BytesY = 16;
		} else {
			*BlockHeight256BytesY = 8;
		}
		*BlockWidth256BytesY = 256 / BytePerPixelY / *BlockHeight256BytesY;
		*BlockHeight256BytesC = 0;
		*BlockWidth256BytesC = 0;
	} else {
		if (SurfaceTiling == dm_sw_linear) {
			*BlockHeight256BytesY = 1;
			*BlockHeight256BytesC = 1;
		} else if (SourcePixelFormat == dm_420_8) {
			*BlockHeight256BytesY = 16;
			*BlockHeight256BytesC = 8;
		} else {
			*BlockHeight256BytesY = 8;
			*BlockHeight256BytesC = 8;
		}
		*BlockWidth256BytesY = 256 / BytePerPixelY / *BlockHeight256BytesY;
		*BlockWidth256BytesC = 256 / BytePerPixelC / *BlockHeight256BytesC;
	}
}

bool CalculateMinAndMaxPrefetchMode(
		enum self_refresh_affinity AllowDRAMSelfRefreshOrDRAMClockChangeInVblank,
		unsigned int *MinPrefetchMode,
		unsigned int *MaxPrefetchMode)
{
	if (AllowDRAMSelfRefreshOrDRAMClockChangeInVblank
			== dm_neither_self_refresh_nor_mclk_switch) {
		*MinPrefetchMode = 2;
		*MaxPrefetchMode = 2;
		return false;
	} else if (AllowDRAMSelfRefreshOrDRAMClockChangeInVblank == dm_allow_self_refresh) {
		*MinPrefetchMode = 1;
		*MaxPrefetchMode = 1;
		return false;
	} else if (AllowDRAMSelfRefreshOrDRAMClockChangeInVblank
			== dm_allow_self_refresh_and_mclk_switch) {
		*MinPrefetchMode = 0;
		*MaxPrefetchMode = 0;
		return false;
	} else if (AllowDRAMSelfRefreshOrDRAMClockChangeInVblank
			== dm_try_to_allow_self_refresh_and_mclk_switch) {
		*MinPrefetchMode = 0;
		*MaxPrefetchMode = 2;
		return false;
	}
	*MinPrefetchMode = 0;
	*MaxPrefetchMode = 2;
	return true;
}

void PixelClockAdjustmentForProgressiveToInterlaceUnit(struct display_mode_lib *mode_lib)
{
	unsigned int k;

	//Progressive To Interlace Unit Effect
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		mode_lib->vba.PixelClockBackEnd[k] = mode_lib->vba.PixelClock[k];
		if (mode_lib->vba.Interlace[k] == 1
				&& mode_lib->vba.ProgressiveToInterlaceUnitInOPP == true) {
			mode_lib->vba.PixelClock[k] = 2 * mode_lib->vba.PixelClock[k];
		}
	}
}

static unsigned int CursorBppEnumToBits(enum cursor_bpp ebpp)
{
	switch (ebpp) {
	case dm_cur_2bit:
		return 2;
	case dm_cur_32bit:
		return 32;
	case dm_cur_64bit:
		return 64;
	default:
		return 0;
	}
}

void ModeSupportAndSystemConfiguration(struct display_mode_lib *mode_lib)
{
	soc_bounding_box_st *soc = &mode_lib->vba.soc;
	unsigned int k;
	unsigned int total_pipes = 0;
	unsigned int pipe_idx = 0;

	mode_lib->vba.VoltageLevel = mode_lib->vba.cache_pipes[0].clks_cfg.voltage;
	mode_lib->vba.ReturnBW = mode_lib->vba.ReturnBWPerState[mode_lib->vba.VoltageLevel][mode_lib->vba.maxMpcComb];
	if (mode_lib->vba.ReturnBW == 0)
		mode_lib->vba.ReturnBW = mode_lib->vba.ReturnBWPerState[mode_lib->vba.VoltageLevel][0];
	mode_lib->vba.FabricAndDRAMBandwidth = mode_lib->vba.FabricAndDRAMBandwidthPerState[mode_lib->vba.VoltageLevel];

	fetch_socbb_params(mode_lib);
	fetch_ip_params(mode_lib);
	fetch_pipe_params(mode_lib);

	mode_lib->vba.DCFCLK = mode_lib->vba.cache_pipes[0].clks_cfg.dcfclk_mhz;
	mode_lib->vba.SOCCLK = mode_lib->vba.cache_pipes[0].clks_cfg.socclk_mhz;
	if (mode_lib->vba.cache_pipes[0].clks_cfg.dispclk_mhz > 0.0)
		mode_lib->vba.DISPCLK = mode_lib->vba.cache_pipes[0].clks_cfg.dispclk_mhz;
	else
		mode_lib->vba.DISPCLK = soc->clock_limits[mode_lib->vba.VoltageLevel].dispclk_mhz;

	// Total Available Pipes Support Check
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		pipe_idx = get_pipe_idx(mode_lib, k);
		if (pipe_idx == -1) {
			ASSERT(0);
			continue; // skip inactive planes
		}
		total_pipes += mode_lib->vba.DPPPerPlane[k];

		if (mode_lib->vba.cache_pipes[pipe_idx].clks_cfg.dppclk_mhz > 0.0)
			mode_lib->vba.DPPCLK[k] = mode_lib->vba.cache_pipes[pipe_idx].clks_cfg.dppclk_mhz;
		else
			mode_lib->vba.DPPCLK[k] = soc->clock_limits[mode_lib->vba.VoltageLevel].dppclk_mhz;
	}
	ASSERT(total_pipes <= DC__NUM_DPP__MAX);
}

double CalculateWriteBackDISPCLK(
		enum source_format_class WritebackPixelFormat,
		double PixelClock,
		double WritebackHRatio,
		double WritebackVRatio,
		unsigned int WritebackLumaHTaps,
		unsigned int WritebackLumaVTaps,
		unsigned int WritebackChromaHTaps,
		unsigned int WritebackChromaVTaps,
		double WritebackDestinationWidth,
		unsigned int HTotal,
		unsigned int WritebackChromaLineBufferWidth)
{
	double CalculateWriteBackDISPCLK = 1.01 * PixelClock * dml_max(
		dml_ceil(WritebackLumaHTaps / 4.0, 1) / WritebackHRatio,
		dml_max((WritebackLumaVTaps * dml_ceil(1.0 / WritebackVRatio, 1) * dml_ceil(WritebackDestinationWidth / 4.0, 1)
			+ dml_ceil(WritebackDestinationWidth / 4.0, 1)) / (double) HTotal + dml_ceil(1.0 / WritebackVRatio, 1)
			* (dml_ceil(WritebackLumaVTaps / 4.0, 1) + 4.0) / (double) HTotal,
			dml_ceil(1.0 / WritebackVRatio, 1) * WritebackDestinationWidth / (double) HTotal));
	if (WritebackPixelFormat != dm_444_32) {
		CalculateWriteBackDISPCLK = dml_max(CalculateWriteBackDISPCLK, 1.01 * PixelClock * dml_max(
			dml_ceil(WritebackChromaHTaps / 2.0, 1) / (2 * WritebackHRatio),
			dml_max((WritebackChromaVTaps * dml_ceil(1 / (2 * WritebackVRatio), 1) * dml_ceil(WritebackDestinationWidth / 2.0 / 2.0, 1)
				+ dml_ceil(WritebackDestinationWidth / 2.0 / WritebackChromaLineBufferWidth, 1)) / HTotal
				+ dml_ceil(1 / (2 * WritebackVRatio), 1) * (dml_ceil(WritebackChromaVTaps / 4.0, 1) + 4) / HTotal,
				dml_ceil(1.0 / (2 * WritebackVRatio), 1) * WritebackDestinationWidth / 2.0 / HTotal)));
	}
	return CalculateWriteBackDISPCLK;
}

