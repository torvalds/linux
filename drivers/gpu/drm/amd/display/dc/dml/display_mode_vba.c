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
 *   This file is gcc-parseable HW gospel, coming straight from HW engineers.
 *
 * It doesn't adhere to Linux kernel style and sometimes will do things in odd
 * ways. Unless there is something clearly wrong with it the code should
 * remain as-is as it provides us with a guarantee from HW that it is correct.
 */

#define BPP_INVALID 0
#define BPP_BLENDED_PIPE 0xffffffff
static const unsigned int NumberOfStates = DC__VOLTAGE_STATES;

static void fetch_socbb_params(struct display_mode_lib *mode_lib);
static void fetch_ip_params(struct display_mode_lib *mode_lib);
static void fetch_pipe_params(struct display_mode_lib *mode_lib);
static void recalculate_params(
		struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes);
static void recalculate(struct display_mode_lib *mode_lib);
static double adjust_ReturnBW(
		struct display_mode_lib *mode_lib,
		double ReturnBW,
		bool DCCEnabledAnyPlane,
		double ReturnBandwidthToDCN);
static void ModeSupportAndSystemConfiguration(struct display_mode_lib *mode_lib);
static void DisplayPipeConfiguration(struct display_mode_lib *mode_lib);
static void DISPCLKDPPCLKDCFCLKDeepSleepPrefetchParametersWatermarksAndPerformanceCalculation(
		struct display_mode_lib *mode_lib);
static unsigned int dscceComputeDelay(
		unsigned int bpc,
		double bpp,
		unsigned int sliceWidth,
		unsigned int numSlices,
		enum output_format_class pixelFormat);
static unsigned int dscComputeDelay(enum output_format_class pixelFormat);
// Super monster function with some 45 argument
static bool CalculatePrefetchSchedule(
		struct display_mode_lib *mode_lib,
		double DPPCLK,
		double DISPCLK,
		double PixelClock,
		double DCFClkDeepSleep,
		unsigned int DSCDelay,
		unsigned int DPPPerPlane,
		bool ScalerEnabled,
		unsigned int NumberOfCursors,
		double DPPCLKDelaySubtotal,
		double DPPCLKDelaySCL,
		double DPPCLKDelaySCLLBOnly,
		double DPPCLKDelayCNVCFormater,
		double DPPCLKDelayCNVCCursor,
		double DISPCLKDelaySubtotal,
		unsigned int ScalerRecoutWidth,
		enum output_format_class OutputFormat,
		unsigned int VBlank,
		unsigned int HTotal,
		unsigned int MaxInterDCNTileRepeaters,
		unsigned int VStartup,
		unsigned int PageTableLevels,
		bool VirtualMemoryEnable,
		bool DynamicMetadataEnable,
		unsigned int DynamicMetadataLinesBeforeActiveRequired,
		unsigned int DynamicMetadataTransmittedBytes,
		bool DCCEnable,
		double UrgentLatency,
		double UrgentExtraLatency,
		double TCalc,
		unsigned int PDEAndMetaPTEBytesFrame,
		unsigned int MetaRowByte,
		unsigned int PixelPTEBytesPerRow,
		double PrefetchSourceLinesY,
		unsigned int SwathWidthY,
		double BytePerPixelDETY,
		double VInitPreFillY,
		unsigned int MaxNumSwathY,
		double PrefetchSourceLinesC,
		double BytePerPixelDETC,
		double VInitPreFillC,
		unsigned int MaxNumSwathC,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		double TWait,
		bool XFCEnabled,
		double XFCRemoteSurfaceFlipDelay,
		bool InterlaceEnable,
		bool ProgressiveToInterlaceUnitInOPP,
		double *DSTXAfterScaler,
		double *DSTYAfterScaler,
		double *DestinationLinesForPrefetch,
		double *PrefetchBandwidth,
		double *DestinationLinesToRequestVMInVBlank,
		double *DestinationLinesToRequestRowInVBlank,
		double *VRatioPrefetchY,
		double *VRatioPrefetchC,
		double *RequiredPrefetchPixDataBW,
		unsigned int *VStartupRequiredWhenNotEnoughTimeForDynamicMetadata,
		double *Tno_bw,
		unsigned int *VUpdateOffsetPix,
		unsigned int *VUpdateWidthPix,
		unsigned int *VReadyOffsetPix);
static double RoundToDFSGranularityUp(double Clock, double VCOSpeed);
static double RoundToDFSGranularityDown(double Clock, double VCOSpeed);
static double CalculatePrefetchSourceLines(
		struct display_mode_lib *mode_lib,
		double VRatio,
		double vtaps,
		bool Interlace,
		bool ProgressiveToInterlaceUnitInOPP,
		unsigned int SwathHeight,
		unsigned int ViewportYStart,
		double *VInitPreFill,
		unsigned int *MaxNumSwath);
static unsigned int CalculateVMAndRowBytes(
		struct display_mode_lib *mode_lib,
		bool DCCEnable,
		unsigned int BlockHeight256Bytes,
		unsigned int BlockWidth256Bytes,
		enum source_format_class SourcePixelFormat,
		unsigned int SurfaceTiling,
		unsigned int BytePerPixel,
		enum scan_direction_class ScanDirection,
		unsigned int ViewportWidth,
		unsigned int ViewportHeight,
		unsigned int SwathWidthY,
		bool VirtualMemoryEnable,
		unsigned int VMMPageSize,
		unsigned int PTEBufferSizeInRequests,
		unsigned int PDEProcessingBufIn64KBReqs,
		unsigned int Pitch,
		unsigned int DCCMetaPitch,
		unsigned int *MacroTileWidth,
		unsigned int *MetaRowByte,
		unsigned int *PixelPTEBytesPerRow,
		bool *PTEBufferSizeNotExceeded,
		unsigned int *dpte_row_height,
		unsigned int *meta_row_height);
static double CalculateTWait(
		unsigned int PrefetchMode,
		double DRAMClockChangeLatency,
		double UrgentLatency,
		double SREnterPlusExitTime);
static double CalculateRemoteSurfaceFlipDelay(
		struct display_mode_lib *mode_lib,
		double VRatio,
		double SwathWidth,
		double Bpp,
		double LineTime,
		double XFCTSlvVupdateOffset,
		double XFCTSlvVupdateWidth,
		double XFCTSlvVreadyOffset,
		double XFCXBUFLatencyTolerance,
		double XFCFillBWOverhead,
		double XFCSlvChunkSize,
		double XFCBusTransportTime,
		double TCalc,
		double TWait,
		double *SrcActiveDrainRate,
		double *TInitXFill,
		double *TslvChk);
static double CalculateWriteBackDISPCLK(
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
		unsigned int WritebackChromaLineBufferWidth);
static void CalculateActiveRowBandwidth(
		bool VirtualMemoryEnable,
		enum source_format_class SourcePixelFormat,
		double VRatio,
		bool DCCEnable,
		double LineTime,
		unsigned int MetaRowByteLuma,
		unsigned int MetaRowByteChroma,
		unsigned int meta_row_height_luma,
		unsigned int meta_row_height_chroma,
		unsigned int PixelPTEBytesPerRowLuma,
		unsigned int PixelPTEBytesPerRowChroma,
		unsigned int dpte_row_height_luma,
		unsigned int dpte_row_height_chroma,
		double *meta_row_bw,
		double *dpte_row_bw,
		double *qual_row_bw);
static void CalculateFlipSchedule(
		struct display_mode_lib *mode_lib,
		double UrgentExtraLatency,
		double UrgentLatency,
		unsigned int MaxPageTableLevels,
		bool VirtualMemoryEnable,
		double BandwidthAvailableForImmediateFlip,
		unsigned int TotImmediateFlipBytes,
		enum source_format_class SourcePixelFormat,
		unsigned int ImmediateFlipBytes,
		double LineTime,
		double Tno_bw,
		double VRatio,
		double PDEAndMetaPTEBytesFrame,
		unsigned int MetaRowByte,
		unsigned int PixelPTEBytesPerRow,
		bool DCCEnable,
		unsigned int dpte_row_height,
		unsigned int meta_row_height,
		double qual_row_bw,
		double *DestinationLinesToRequestVMInImmediateFlip,
		double *DestinationLinesToRequestRowInImmediateFlip,
		double *final_flip_bw,
		bool *ImmediateFlipSupportedForPipe);
static double CalculateWriteBackDelay(
		enum source_format_class WritebackPixelFormat,
		double WritebackHRatio,
		double WritebackVRatio,
		unsigned int WritebackLumaHTaps,
		unsigned int WritebackLumaVTaps,
		unsigned int WritebackChromaHTaps,
		unsigned int WritebackChromaVTaps,
		unsigned int WritebackDestinationWidth);
static void PixelClockAdjustmentForProgressiveToInterlaceUnit(struct display_mode_lib *mode_lib);
static unsigned int CursorBppEnumToBits(enum cursor_bpp ebpp);
static void ModeSupportAndSystemConfigurationFull(struct display_mode_lib *mode_lib);

void set_prefetch_mode(
		struct display_mode_lib *mode_lib,
		bool cstate_en,
		bool pstate_en,
		bool ignore_viewport_pos,
		bool immediate_flip_support)
{
	unsigned int prefetch_mode;

	if (cstate_en && pstate_en)
		prefetch_mode = 0;
	else if (cstate_en)
		prefetch_mode = 1;
	else
		prefetch_mode = 2;
	if (prefetch_mode != mode_lib->vba.PrefetchMode
			|| ignore_viewport_pos != mode_lib->vba.IgnoreViewportPositioning
			|| immediate_flip_support != mode_lib->vba.ImmediateFlipSupport) {
		DTRACE(
				"   Prefetch mode has changed from %i to %i. Recalculating.",
				prefetch_mode,
				mode_lib->vba.PrefetchMode);
		mode_lib->vba.PrefetchMode = prefetch_mode;
		mode_lib->vba.IgnoreViewportPositioning = ignore_viewport_pos;
		mode_lib->vba.ImmediateFlipSupport = immediate_flip_support;
		recalculate(mode_lib);
	}
}

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
		recalculate(mode_lib);
	else {
		fetch_socbb_params(mode_lib);
		fetch_ip_params(mode_lib);
		fetch_pipe_params(mode_lib);
	}
	ModeSupportAndSystemConfigurationFull(mode_lib);

	return mode_lib->vba.VoltageLevel;
}

#define dml_get_attr_func(attr, var)  double get_##attr(struct display_mode_lib *mode_lib, const display_e2e_pipe_params_st *pipes, unsigned int num_pipes) \
{ \
	recalculate_params(mode_lib, pipes, num_pipes); \
	return var; \
}

dml_get_attr_func(clk_dcf_deepsleep, mode_lib->vba.DCFClkDeepSleep);
dml_get_attr_func(wm_urgent, mode_lib->vba.UrgentWatermark);
dml_get_attr_func(wm_memory_trip, mode_lib->vba.MemoryTripWatermark);
dml_get_attr_func(wm_writeback_urgent, mode_lib->vba.WritebackUrgentWatermark);
dml_get_attr_func(wm_stutter_exit, mode_lib->vba.StutterExitWatermark);
dml_get_attr_func(wm_stutter_enter_exit, mode_lib->vba.StutterEnterPlusExitWatermark);
dml_get_attr_func(wm_dram_clock_change, mode_lib->vba.DRAMClockChangeWatermark);
dml_get_attr_func(wm_writeback_dram_clock_change, mode_lib->vba.WritebackDRAMClockChangeWatermark);
dml_get_attr_func(wm_xfc_underflow, mode_lib->vba.UrgentWatermark); // xfc_underflow maps to urgent
dml_get_attr_func(stutter_efficiency, mode_lib->vba.StutterEfficiency);
dml_get_attr_func(stutter_efficiency_no_vblank, mode_lib->vba.StutterEfficiencyNotIncludingVBlank);
dml_get_attr_func(urgent_latency, mode_lib->vba.MinUrgentLatencySupportUs);
dml_get_attr_func(urgent_extra_latency, mode_lib->vba.UrgentExtraLatency);
dml_get_attr_func(nonurgent_latency, mode_lib->vba.NonUrgentLatencyTolerance);
dml_get_attr_func(
		dram_clock_change_latency,
		mode_lib->vba.MinActiveDRAMClockChangeLatencySupported);
dml_get_attr_func(dispclk_calculated, mode_lib->vba.DISPCLK_calculated);
dml_get_attr_func(total_data_read_bw, mode_lib->vba.TotalDataReadBandwidth);
dml_get_attr_func(return_bw, mode_lib->vba.ReturnBW);
dml_get_attr_func(tcalc, mode_lib->vba.TCalc);

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
dml_get_pipe_attr_func(vratio_prefetch_l, mode_lib->vba.VRatioPrefetchY);
dml_get_pipe_attr_func(vratio_prefetch_c, mode_lib->vba.VRatioPrefetchC);
dml_get_pipe_attr_func(dst_x_after_scaler, mode_lib->vba.DSTXAfterScaler);
dml_get_pipe_attr_func(dst_y_after_scaler, mode_lib->vba.DSTYAfterScaler);
dml_get_pipe_attr_func(dst_y_per_vm_vblank, mode_lib->vba.DestinationLinesToRequestVMInVBlank);
dml_get_pipe_attr_func(dst_y_per_row_vblank, mode_lib->vba.DestinationLinesToRequestRowInVBlank);
dml_get_pipe_attr_func(dst_y_prefetch, mode_lib->vba.DestinationLinesForPrefetch);
dml_get_pipe_attr_func(dst_y_per_vm_flip, mode_lib->vba.DestinationLinesToRequestVMInImmediateFlip);
dml_get_pipe_attr_func(
		dst_y_per_row_flip,
		mode_lib->vba.DestinationLinesToRequestRowInImmediateFlip);

dml_get_pipe_attr_func(xfc_transfer_delay, mode_lib->vba.XFCTransferDelay);
dml_get_pipe_attr_func(xfc_precharge_delay, mode_lib->vba.XFCPrechargeDelay);
dml_get_pipe_attr_func(xfc_remote_surface_flip_latency, mode_lib->vba.XFCRemoteSurfaceFlipLatency);
dml_get_pipe_attr_func(xfc_prefetch_margin, mode_lib->vba.XFCPrefetchMargin);

unsigned int get_vstartup_calculated(
		struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes,
		unsigned int which_pipe)
{
	unsigned int which_plane;

	recalculate_params(mode_lib, pipes, num_pipes);
	which_plane = mode_lib->vba.pipe_plane[which_pipe];
	return mode_lib->vba.VStartup[which_plane];
}

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
	recalculate_params(mode_lib, pipes, num_pipes);
	return mode_lib->vba.ImmediateFlipBW;
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

static void fetch_socbb_params(struct display_mode_lib *mode_lib)
{
	soc_bounding_box_st *soc = &mode_lib->vba.soc;
	unsigned int i;

	// SOC Bounding Box Parameters
	mode_lib->vba.ReturnBusWidth = soc->return_bus_width_bytes;
	mode_lib->vba.NumberOfChannels = soc->num_chans;
	mode_lib->vba.PercentOfIdealDRAMAndFabricBWReceivedAfterUrgLatency =
			soc->ideal_dram_bw_after_urgent_percent; // there's always that one bastard variable that's so long it throws everything out of alignment!
	mode_lib->vba.UrgentLatency = soc->urgent_latency_us;
	mode_lib->vba.RoundTripPingLatencyCycles = soc->round_trip_ping_latency_dcfclk_cycles;
	mode_lib->vba.UrgentOutOfOrderReturnPerChannel =
			soc->urgent_out_of_order_return_per_channel_bytes;
	mode_lib->vba.WritebackLatency = soc->writeback_latency_us;
	mode_lib->vba.SRExitTime = soc->sr_exit_time_us;
	mode_lib->vba.SREnterPlusExitTime = soc->sr_enter_plus_exit_time_us;
	mode_lib->vba.DRAMClockChangeLatency = soc->dram_clock_change_latency_us;
	mode_lib->vba.Downspreading = soc->downspread_percent;
	mode_lib->vba.DRAMChannelWidth = soc->dram_channel_width_bytes;   // new!
	mode_lib->vba.FabricDatapathToDCNDataReturn = soc->fabric_datapath_to_dcn_data_return_bytes; // new!
	mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading = soc->dcn_downspread_percent;   // new
	mode_lib->vba.DISPCLKDPPCLKVCOSpeed = soc->dispclk_dppclk_vco_speed_mhz;   // new
	mode_lib->vba.VMMPageSize = soc->vmm_page_size_bytes;
	// Set the voltage scaling clocks as the defaults. Most of these will
	// be set to different values by the test
	for (i = 0; i < DC__VOLTAGE_STATES; i++)
		if (soc->clock_limits[i].state == mode_lib->vba.VoltageLevel)
			break;

	mode_lib->vba.DCFCLK = soc->clock_limits[i].dcfclk_mhz;
	mode_lib->vba.SOCCLK = soc->clock_limits[i].socclk_mhz;
	mode_lib->vba.DRAMSpeed = soc->clock_limits[i].dram_speed_mhz;
	mode_lib->vba.FabricClock = soc->clock_limits[i].fabricclk_mhz;

	mode_lib->vba.XFCBusTransportTime = soc->xfc_bus_transport_time_us;
	mode_lib->vba.XFCXBUFLatencyTolerance = soc->xfc_xbuf_latency_tolerance_us;

	mode_lib->vba.SupportGFX7CompatibleTilingIn32bppAnd64bpp = false;
	mode_lib->vba.MaxHSCLRatio = 4;
	mode_lib->vba.MaxVSCLRatio = 4;
	mode_lib->vba.MaxNumWriteback = 0; /*TODO*/
	mode_lib->vba.WritebackLumaAndChromaScalingSupported = true;
	mode_lib->vba.Cursor64BppSupport = true;
	for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
		mode_lib->vba.DCFCLKPerState[i] = soc->clock_limits[i].dcfclk_mhz;
		mode_lib->vba.FabricClockPerState[i] = soc->clock_limits[i].fabricclk_mhz;
		mode_lib->vba.SOCCLKPerState[i] = soc->clock_limits[i].socclk_mhz;
		mode_lib->vba.PHYCLKPerState[i] = soc->clock_limits[i].phyclk_mhz;
		mode_lib->vba.MaxDppclk[i] = soc->clock_limits[i].dppclk_mhz;
		mode_lib->vba.MaxDSCCLK[i] = soc->clock_limits[i].dscclk_mhz;
		mode_lib->vba.DRAMSpeedPerState[i] = soc->clock_limits[i].dram_speed_mhz;
		mode_lib->vba.MaxDispclk[i] = soc->clock_limits[i].dispclk_mhz;
	}
}

static void fetch_ip_params(struct display_mode_lib *mode_lib)
{
	ip_params_st *ip = &mode_lib->vba.ip;

	// IP Parameters
	mode_lib->vba.MaxNumDPP = ip->max_num_dpp;
	mode_lib->vba.MaxNumOTG = ip->max_num_otg;
	mode_lib->vba.CursorChunkSize = ip->cursor_chunk_size;
	mode_lib->vba.CursorBufferSize = ip->cursor_buffer_size;

	mode_lib->vba.MaxDCHUBToPSCLThroughput = ip->max_dchub_pscl_bw_pix_per_clk;
	mode_lib->vba.MaxPSCLToLBThroughput = ip->max_pscl_lb_bw_pix_per_clk;
	mode_lib->vba.ROBBufferSizeInKByte = ip->rob_buffer_size_kbytes;
	mode_lib->vba.DETBufferSizeInKByte = ip->det_buffer_size_kbytes;
	mode_lib->vba.PixelChunkSizeInKByte = ip->pixel_chunk_size_kbytes;
	mode_lib->vba.MetaChunkSize = ip->meta_chunk_size_kbytes;
	mode_lib->vba.PTEChunkSize = ip->pte_chunk_size_kbytes;
	mode_lib->vba.WritebackChunkSize = ip->writeback_chunk_size_kbytes;
	mode_lib->vba.LineBufferSize = ip->line_buffer_size_bits;
	mode_lib->vba.MaxLineBufferLines = ip->max_line_buffer_lines;
	mode_lib->vba.PTEBufferSizeInRequests = ip->dpte_buffer_size_in_pte_reqs;
	mode_lib->vba.DPPOutputBufferPixels = ip->dpp_output_buffer_pixels;
	mode_lib->vba.OPPOutputBufferLines = ip->opp_output_buffer_lines;
	mode_lib->vba.WritebackInterfaceLumaBufferSize = ip->writeback_luma_buffer_size_kbytes;
	mode_lib->vba.WritebackInterfaceChromaBufferSize = ip->writeback_chroma_buffer_size_kbytes;
	mode_lib->vba.WritebackChromaLineBufferWidth =
			ip->writeback_chroma_line_buffer_width_pixels;
	mode_lib->vba.MaxPageTableLevels = ip->max_page_table_levels;
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

	mode_lib->vba.ProgressiveToInterlaceUnitInOPP = ip->ptoi_supported;

	mode_lib->vba.PDEProcessingBufIn64KBReqs = ip->pde_proc_buffer_size_64k_reqs;
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

		mode_lib->vba.pipe_plane[j] = mode_lib->vba.NumberOfActivePlanes;

		mode_lib->vba.DPPPerPlane[mode_lib->vba.NumberOfActivePlanes] = 1;
		mode_lib->vba.SourceScan[mode_lib->vba.NumberOfActivePlanes] =
				(enum scan_direction_class) (src->source_scan);
		mode_lib->vba.ViewportWidth[mode_lib->vba.NumberOfActivePlanes] =
				src->viewport_width;
		mode_lib->vba.ViewportHeight[mode_lib->vba.NumberOfActivePlanes] =
				src->viewport_height;
		mode_lib->vba.ViewportYStartY[mode_lib->vba.NumberOfActivePlanes] =
				src->viewport_y_y;
		mode_lib->vba.ViewportYStartC[mode_lib->vba.NumberOfActivePlanes] =
				src->viewport_y_c;
		mode_lib->vba.PitchY[mode_lib->vba.NumberOfActivePlanes] = src->data_pitch;
		mode_lib->vba.PitchC[mode_lib->vba.NumberOfActivePlanes] = src->data_pitch_c;
		mode_lib->vba.DCCMetaPitchY[mode_lib->vba.NumberOfActivePlanes] = src->meta_pitch;
		mode_lib->vba.HRatio[mode_lib->vba.NumberOfActivePlanes] = scl->hscl_ratio;
		mode_lib->vba.VRatio[mode_lib->vba.NumberOfActivePlanes] = scl->vscl_ratio;
		mode_lib->vba.ScalerEnabled[mode_lib->vba.NumberOfActivePlanes] = scl->scl_enable;
		mode_lib->vba.Interlace[mode_lib->vba.NumberOfActivePlanes] = dst->interlaced;
		if (mode_lib->vba.Interlace[mode_lib->vba.NumberOfActivePlanes])
			mode_lib->vba.VRatio[mode_lib->vba.NumberOfActivePlanes] *= 2.0;
		mode_lib->vba.htaps[mode_lib->vba.NumberOfActivePlanes] = taps->htaps;
		mode_lib->vba.vtaps[mode_lib->vba.NumberOfActivePlanes] = taps->vtaps;
		mode_lib->vba.HTAPsChroma[mode_lib->vba.NumberOfActivePlanes] = taps->htaps_c;
		mode_lib->vba.VTAPsChroma[mode_lib->vba.NumberOfActivePlanes] = taps->vtaps_c;
		mode_lib->vba.HTotal[mode_lib->vba.NumberOfActivePlanes] = dst->htotal;
		mode_lib->vba.VTotal[mode_lib->vba.NumberOfActivePlanes] = dst->vtotal;
		mode_lib->vba.DCCEnable[mode_lib->vba.NumberOfActivePlanes] =
				src->dcc_use_global ?
						ip->dcc_supported : src->dcc && ip->dcc_supported;
		mode_lib->vba.DCCRate[mode_lib->vba.NumberOfActivePlanes] = src->dcc_rate;
		mode_lib->vba.SourcePixelFormat[mode_lib->vba.NumberOfActivePlanes] =
				(enum source_format_class) (src->source_format);
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
		mode_lib->vba.Output[mode_lib->vba.NumberOfActivePlanes] =
				(enum output_encoder_class) (dout->output_type);
		mode_lib->vba.OutputBpp[mode_lib->vba.NumberOfActivePlanes] = dout->output_bpp;
		mode_lib->vba.OutputLinkDPLanes[mode_lib->vba.NumberOfActivePlanes] =
				dout->dp_lanes;
		mode_lib->vba.DSCEnabled[mode_lib->vba.NumberOfActivePlanes] = dout->dsc_enable;
		mode_lib->vba.NumberOfDSCSlices[mode_lib->vba.NumberOfActivePlanes] =
				dout->dsc_slices;
		mode_lib->vba.DSCInputBitPerComponent[mode_lib->vba.NumberOfActivePlanes] =
				dout->opp_input_bpc == 0 ? 12 : dout->opp_input_bpc;
		mode_lib->vba.WritebackEnable[mode_lib->vba.NumberOfActivePlanes] = dout->wb_enable;
		mode_lib->vba.WritebackSourceHeight[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_src_height;
		mode_lib->vba.WritebackDestinationWidth[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_dst_width;
		mode_lib->vba.WritebackDestinationHeight[mode_lib->vba.NumberOfActivePlanes] =
				dout->wb.wb_dst_height;
		mode_lib->vba.WritebackPixelFormat[mode_lib->vba.NumberOfActivePlanes] =
				(enum source_format_class) (dout->wb.wb_pixel_format);
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
		mode_lib->vba.DPPCLK[mode_lib->vba.NumberOfActivePlanes] = clks->dppclk_mhz;
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

		if (dst->odm_combine && !src->is_hsplit)
			dml_print(
					"ERROR: ODM Combine is specified but is_hsplit has not be specified for pipe %i\n",
					j);

		if (src->is_hsplit) {
			for (k = j + 1; k < mode_lib->vba.cache_num_pipes; ++k) {
				display_pipe_source_params_st *src_k = &pipes[k].pipe.src;
				display_output_params_st *dout_k = &pipes[k].dout;

				if (src_k->is_hsplit && !visited[k]
						&& src->hsplit_grp == src_k->hsplit_grp) {
					mode_lib->vba.pipe_plane[k] =
							mode_lib->vba.NumberOfActivePlanes;
					mode_lib->vba.DPPPerPlane[mode_lib->vba.NumberOfActivePlanes]++;
					if (mode_lib->vba.SourceScan[mode_lib->vba.NumberOfActivePlanes]
							== dm_horz)
						mode_lib->vba.ViewportWidth[mode_lib->vba.NumberOfActivePlanes] +=
								src_k->viewport_width;
					else
						mode_lib->vba.ViewportHeight[mode_lib->vba.NumberOfActivePlanes] +=
								src_k->viewport_height;

					mode_lib->vba.NumberOfDSCSlices[mode_lib->vba.NumberOfActivePlanes] +=
							dout_k->dsc_slices;
					visited[k] = true;
				}
			}
		}

		mode_lib->vba.NumberOfActivePlanes++;
	}

	// handle overlays through dml_ml->vba.BlendingAndTiming
	// dml_ml->vba.BlendingAndTiming tells you which instance to look at to get timing, the so called 'master'

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

	// TODO: dml_ml->vba.ODMCombineEnabled => 2 * dml_ml->vba.DPPPerPlane...actually maybe not since all pipes are specified
	// Do we want the dscclk to automatically be halved? Guess not since the value is specified

	mode_lib->vba.SynchronizedVBlank = pipes[0].pipe.dest.synchronized_vblank_all_planes;
	for (k = 1; k < mode_lib->vba.cache_num_pipes; ++k)
		ASSERT(mode_lib->vba.SynchronizedVBlank == pipes[k].pipe.dest.synchronized_vblank_all_planes);

	mode_lib->vba.VirtualMemoryEnable = false;
	mode_lib->vba.OverridePageTableLevels = 0;

	for (k = 0; k < mode_lib->vba.cache_num_pipes; ++k) {
		mode_lib->vba.VirtualMemoryEnable = mode_lib->vba.VirtualMemoryEnable
				|| !!pipes[k].pipe.src.vm;
		mode_lib->vba.OverridePageTableLevels =
				(pipes[k].pipe.src.vm_levels_force_en
						&& mode_lib->vba.OverridePageTableLevels
								< pipes[k].pipe.src.vm_levels_force) ?
						pipes[k].pipe.src.vm_levels_force :
						mode_lib->vba.OverridePageTableLevels;
	}

	if (mode_lib->vba.OverridePageTableLevels)
		mode_lib->vba.MaxPageTableLevels = mode_lib->vba.OverridePageTableLevels;

	mode_lib->vba.VirtualMemoryEnable = mode_lib->vba.VirtualMemoryEnable && !!ip->pte_enable;

	mode_lib->vba.FabricAndDRAMBandwidth = dml_min(
			mode_lib->vba.DRAMSpeed * mode_lib->vba.NumberOfChannels
					* mode_lib->vba.DRAMChannelWidth,
			mode_lib->vba.FabricClock * mode_lib->vba.FabricDatapathToDCNDataReturn)
			/ 1000.0;

	// TODO: Must be consistent across all pipes
	// DCCProgrammingAssumesScanDirectionUnknown = src.dcc_scan_dir_unknown;
}

static void recalculate(struct display_mode_lib *mode_lib)
{
	ModeSupportAndSystemConfiguration(mode_lib);
	PixelClockAdjustmentForProgressiveToInterlaceUnit(mode_lib);
	DisplayPipeConfiguration(mode_lib);
	DISPCLKDPPCLKDCFCLKDeepSleepPrefetchParametersWatermarksAndPerformanceCalculation(mode_lib);
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
		recalculate(mode_lib);
	}
}

static void ModeSupportAndSystemConfiguration(struct display_mode_lib *mode_lib)
{
	soc_bounding_box_st *soc = &mode_lib->vba.soc;
	unsigned int i, k;
	unsigned int total_pipes = 0;

	mode_lib->vba.VoltageLevel = mode_lib->vba.cache_pipes[0].clks_cfg.voltage;
	for (i = 1; i < mode_lib->vba.cache_num_pipes; ++i)
		ASSERT(mode_lib->vba.VoltageLevel == -1 || mode_lib->vba.VoltageLevel == mode_lib->vba.cache_pipes[i].clks_cfg.voltage);

	mode_lib->vba.DCFCLK = mode_lib->vba.cache_pipes[0].clks_cfg.dcfclk_mhz;
	mode_lib->vba.SOCCLK = mode_lib->vba.cache_pipes[0].clks_cfg.socclk_mhz;

	if (mode_lib->vba.cache_pipes[0].clks_cfg.dispclk_mhz > 0.0)
		mode_lib->vba.DISPCLK = mode_lib->vba.cache_pipes[0].clks_cfg.dispclk_mhz;
	else
		mode_lib->vba.DISPCLK = soc->clock_limits[mode_lib->vba.VoltageLevel].dispclk_mhz;

	fetch_socbb_params(mode_lib);
	fetch_ip_params(mode_lib);
	fetch_pipe_params(mode_lib);

	// Total Available Pipes Support Check
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k)
		total_pipes += mode_lib->vba.DPPPerPlane[k];
	ASSERT(total_pipes <= DC__NUM_DPP__MAX);
}

static double adjust_ReturnBW(
		struct display_mode_lib *mode_lib,
		double ReturnBW,
		bool DCCEnabledAnyPlane,
		double ReturnBandwidthToDCN)
{
	double CriticalCompression;

	if (DCCEnabledAnyPlane
			&& ReturnBandwidthToDCN
					> mode_lib->vba.DCFCLK * mode_lib->vba.ReturnBusWidth / 4.0)
		ReturnBW =
				dml_min(
						ReturnBW,
						ReturnBandwidthToDCN * 4
								* (1.0
										- mode_lib->vba.UrgentLatency
												/ ((mode_lib->vba.ROBBufferSizeInKByte
														- mode_lib->vba.PixelChunkSizeInKByte)
														* 1024
														/ ReturnBandwidthToDCN
														- mode_lib->vba.DCFCLK
																* mode_lib->vba.ReturnBusWidth
																/ 4)
										+ mode_lib->vba.UrgentLatency));

	CriticalCompression = 2.0 * mode_lib->vba.ReturnBusWidth * mode_lib->vba.DCFCLK
			* mode_lib->vba.UrgentLatency
			/ (ReturnBandwidthToDCN * mode_lib->vba.UrgentLatency
					+ (mode_lib->vba.ROBBufferSizeInKByte
							- mode_lib->vba.PixelChunkSizeInKByte)
							* 1024);

	if (DCCEnabledAnyPlane && CriticalCompression > 1.0 && CriticalCompression < 4.0)
		ReturnBW =
				dml_min(
						ReturnBW,
						4.0 * ReturnBandwidthToDCN
								* (mode_lib->vba.ROBBufferSizeInKByte
										- mode_lib->vba.PixelChunkSizeInKByte)
								* 1024
								* mode_lib->vba.ReturnBusWidth
								* mode_lib->vba.DCFCLK
								* mode_lib->vba.UrgentLatency
								/ dml_pow(
										(ReturnBandwidthToDCN
												* mode_lib->vba.UrgentLatency
												+ (mode_lib->vba.ROBBufferSizeInKByte
														- mode_lib->vba.PixelChunkSizeInKByte)
														* 1024),
										2));

	return ReturnBW;
}

static unsigned int dscceComputeDelay(
		unsigned int bpc,
		double bpp,
		unsigned int sliceWidth,
		unsigned int numSlices,
		enum output_format_class pixelFormat)
{
	// valid bpc         = source bits per component in the set of {8, 10, 12}
	// valid bpp         = increments of 1/16 of a bit
	//                    min = 6/7/8 in N420/N422/444, respectively
	//                    max = such that compression is 1:1
	//valid sliceWidth  = number of pixels per slice line, must be less than or equal to 5184/numSlices (or 4096/numSlices in 420 mode)
	//valid numSlices   = number of slices in the horiziontal direction per DSC engine in the set of {1, 2, 3, 4}
	//valid pixelFormat = pixel/color format in the set of {:N444_RGB, :S422, :N422, :N420}

	// fixed value
	unsigned int rcModelSize = 8192;

	// N422/N420 operate at 2 pixels per clock
	unsigned int pixelsPerClock, lstall, D, initalXmitDelay, w, s, ix, wx, p, l0, a, ax, l,
			Delay, pixels;

	if (pixelFormat == dm_n422 || pixelFormat == dm_420)
		pixelsPerClock = 2;
	// #all other modes operate at 1 pixel per clock
	else
		pixelsPerClock = 1;

	//initial transmit delay as per PPS
	initalXmitDelay = dml_round(rcModelSize / 2.0 / bpp / pixelsPerClock);

	//compute ssm delay
	if (bpc == 8)
		D = 81;
	else if (bpc == 10)
		D = 89;
	else
		D = 113;

	//divide by pixel per cycle to compute slice width as seen by DSC
	w = sliceWidth / pixelsPerClock;

	//422 mode has an additional cycle of delay
	if (pixelFormat == dm_s422)
		s = 1;
	else
		s = 0;

	//main calculation for the dscce
	ix = initalXmitDelay + 45;
	wx = (w + 2) / 3;
	p = 3 * wx - w;
	l0 = ix / w;
	a = ix + p * l0;
	ax = (a + 2) / 3 + D + 6 + 1;
	l = (ax + wx - 1) / wx;
	if ((ix % w) == 0 && p != 0)
		lstall = 1;
	else
		lstall = 0;
	Delay = l * wx * (numSlices - 1) + ax + s + lstall + 22;

	//dsc processes 3 pixel containers per cycle and a container can contain 1 or 2 pixels
	pixels = Delay * 3 * pixelsPerClock;
	return pixels;
}

static unsigned int dscComputeDelay(enum output_format_class pixelFormat)
{
	unsigned int Delay = 0;

	if (pixelFormat == dm_420) {
		//   sfr
		Delay = Delay + 2;
		//   dsccif
		Delay = Delay + 0;
		//   dscc - input deserializer
		Delay = Delay + 3;
		//   dscc gets pixels every other cycle
		Delay = Delay + 2;
		//   dscc - input cdc fifo
		Delay = Delay + 12;
		//   dscc gets pixels every other cycle
		Delay = Delay + 13;
		//   dscc - cdc uncertainty
		Delay = Delay + 2;
		//   dscc - output cdc fifo
		Delay = Delay + 7;
		//   dscc gets pixels every other cycle
		Delay = Delay + 3;
		//   dscc - cdc uncertainty
		Delay = Delay + 2;
		//   dscc - output serializer
		Delay = Delay + 1;
		//   sft
		Delay = Delay + 1;
	} else if (pixelFormat == dm_n422) {
		//   sfr
		Delay = Delay + 2;
		//   dsccif
		Delay = Delay + 1;
		//   dscc - input deserializer
		Delay = Delay + 5;
		//  dscc - input cdc fifo
		Delay = Delay + 25;
		//   dscc - cdc uncertainty
		Delay = Delay + 2;
		//   dscc - output cdc fifo
		Delay = Delay + 10;
		//   dscc - cdc uncertainty
		Delay = Delay + 2;
		//   dscc - output serializer
		Delay = Delay + 1;
		//   sft
		Delay = Delay + 1;
	} else {
		//   sfr
		Delay = Delay + 2;
		//   dsccif
		Delay = Delay + 0;
		//   dscc - input deserializer
		Delay = Delay + 3;
		//   dscc - input cdc fifo
		Delay = Delay + 12;
		//   dscc - cdc uncertainty
		Delay = Delay + 2;
		//   dscc - output cdc fifo
		Delay = Delay + 7;
		//   dscc - output serializer
		Delay = Delay + 1;
		//   dscc - cdc uncertainty
		Delay = Delay + 2;
		//   sft
		Delay = Delay + 1;
	}

	return Delay;
}

static bool CalculatePrefetchSchedule(
		struct display_mode_lib *mode_lib,
		double DPPCLK,
		double DISPCLK,
		double PixelClock,
		double DCFClkDeepSleep,
		unsigned int DSCDelay,
		unsigned int DPPPerPlane,
		bool ScalerEnabled,
		unsigned int NumberOfCursors,
		double DPPCLKDelaySubtotal,
		double DPPCLKDelaySCL,
		double DPPCLKDelaySCLLBOnly,
		double DPPCLKDelayCNVCFormater,
		double DPPCLKDelayCNVCCursor,
		double DISPCLKDelaySubtotal,
		unsigned int ScalerRecoutWidth,
		enum output_format_class OutputFormat,
		unsigned int VBlank,
		unsigned int HTotal,
		unsigned int MaxInterDCNTileRepeaters,
		unsigned int VStartup,
		unsigned int PageTableLevels,
		bool VirtualMemoryEnable,
		bool DynamicMetadataEnable,
		unsigned int DynamicMetadataLinesBeforeActiveRequired,
		unsigned int DynamicMetadataTransmittedBytes,
		bool DCCEnable,
		double UrgentLatency,
		double UrgentExtraLatency,
		double TCalc,
		unsigned int PDEAndMetaPTEBytesFrame,
		unsigned int MetaRowByte,
		unsigned int PixelPTEBytesPerRow,
		double PrefetchSourceLinesY,
		unsigned int SwathWidthY,
		double BytePerPixelDETY,
		double VInitPreFillY,
		unsigned int MaxNumSwathY,
		double PrefetchSourceLinesC,
		double BytePerPixelDETC,
		double VInitPreFillC,
		unsigned int MaxNumSwathC,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		double TWait,
		bool XFCEnabled,
		double XFCRemoteSurfaceFlipDelay,
		bool InterlaceEnable,
		bool ProgressiveToInterlaceUnitInOPP,
		double *DSTXAfterScaler,
		double *DSTYAfterScaler,
		double *DestinationLinesForPrefetch,
		double *PrefetchBandwidth,
		double *DestinationLinesToRequestVMInVBlank,
		double *DestinationLinesToRequestRowInVBlank,
		double *VRatioPrefetchY,
		double *VRatioPrefetchC,
		double *RequiredPrefetchPixDataBW,
		unsigned int *VStartupRequiredWhenNotEnoughTimeForDynamicMetadata,
		double *Tno_bw,
		unsigned int *VUpdateOffsetPix,
		unsigned int *VUpdateWidthPix,
		unsigned int *VReadyOffsetPix)
{
	bool MyError = false;
	unsigned int DPPCycles, DISPCLKCycles;
	double DSTTotalPixelsAfterScaler, TotalRepeaterDelayTime;
	double Tdm, LineTime, Tsetup;
	double dst_y_prefetch_equ;
	double Tsw_oto;
	double prefetch_bw_oto;
	double Tvm_oto;
	double Tr0_oto;
	double Tpre_oto;
	double dst_y_prefetch_oto;
	double TimeForFetchingMetaPTE = 0;
	double TimeForFetchingRowInVBlank = 0;
	double LinesToRequestPrefetchPixelData = 0;

	if (ScalerEnabled)
		DPPCycles = DPPCLKDelaySubtotal + DPPCLKDelaySCL;
	else
		DPPCycles = DPPCLKDelaySubtotal + DPPCLKDelaySCLLBOnly;

	DPPCycles = DPPCycles + DPPCLKDelayCNVCFormater + NumberOfCursors * DPPCLKDelayCNVCCursor;

	DISPCLKCycles = DISPCLKDelaySubtotal;

	if (DPPCLK == 0.0 || DISPCLK == 0.0)
		return true;

	*DSTXAfterScaler = DPPCycles * PixelClock / DPPCLK + DISPCLKCycles * PixelClock / DISPCLK
			+ DSCDelay;

	if (DPPPerPlane > 1)
		*DSTXAfterScaler = *DSTXAfterScaler + ScalerRecoutWidth;

	if (OutputFormat == dm_420 || (InterlaceEnable && ProgressiveToInterlaceUnitInOPP))
		*DSTYAfterScaler = 1;
	else
		*DSTYAfterScaler = 0;

	DSTTotalPixelsAfterScaler = ((double) (*DSTYAfterScaler * HTotal)) + *DSTXAfterScaler;
	*DSTYAfterScaler = dml_floor(DSTTotalPixelsAfterScaler / HTotal, 1);
	*DSTXAfterScaler = DSTTotalPixelsAfterScaler - ((double) (*DSTYAfterScaler * HTotal));

	*VUpdateOffsetPix = dml_ceil(HTotal / 4.0, 1);
	TotalRepeaterDelayTime = MaxInterDCNTileRepeaters * (2.0 / DPPCLK + 3.0 / DISPCLK);
	*VUpdateWidthPix = (14.0 / DCFClkDeepSleep + 12.0 / DPPCLK + TotalRepeaterDelayTime)
			* PixelClock;

	*VReadyOffsetPix = dml_max(
			150.0 / DPPCLK,
			TotalRepeaterDelayTime + 20.0 / DCFClkDeepSleep + 10.0 / DPPCLK)
			* PixelClock;

	Tsetup = (double) (*VUpdateOffsetPix + *VUpdateWidthPix + *VReadyOffsetPix) / PixelClock;

	LineTime = (double) HTotal / PixelClock;

	if (DynamicMetadataEnable) {
		double Tdmbf, Tdmec, Tdmsks;

		Tdm = dml_max(0.0, UrgentExtraLatency - TCalc);
		Tdmbf = DynamicMetadataTransmittedBytes / 4.0 / DISPCLK;
		Tdmec = LineTime;
		if (DynamicMetadataLinesBeforeActiveRequired == 0)
			Tdmsks = VBlank * LineTime / 2.0;
		else
			Tdmsks = DynamicMetadataLinesBeforeActiveRequired * LineTime;
		if (InterlaceEnable && !ProgressiveToInterlaceUnitInOPP)
			Tdmsks = Tdmsks / 2;
		if (VStartup * LineTime
				< Tsetup + TWait + UrgentExtraLatency + Tdmbf + Tdmec + Tdmsks) {
			MyError = true;
			*VStartupRequiredWhenNotEnoughTimeForDynamicMetadata = (Tsetup + TWait
					+ UrgentExtraLatency + Tdmbf + Tdmec + Tdmsks) / LineTime;
		} else
			*VStartupRequiredWhenNotEnoughTimeForDynamicMetadata = 0.0;
	} else
		Tdm = 0;

	if (VirtualMemoryEnable) {
		if (PageTableLevels == 4)
			*Tno_bw = UrgentExtraLatency + UrgentLatency;
		else if (PageTableLevels == 3)
			*Tno_bw = UrgentExtraLatency;
		else
			*Tno_bw = 0;
	} else if (DCCEnable)
		*Tno_bw = LineTime;
	else
		*Tno_bw = LineTime / 4;

	dst_y_prefetch_equ = VStartup - dml_max(TCalc + TWait, XFCRemoteSurfaceFlipDelay) / LineTime
			- (Tsetup + Tdm) / LineTime
			- (*DSTYAfterScaler + *DSTXAfterScaler / HTotal);

	Tsw_oto = dml_max(PrefetchSourceLinesY, PrefetchSourceLinesC) * LineTime;

	prefetch_bw_oto = (MetaRowByte + PixelPTEBytesPerRow
			+ PrefetchSourceLinesY * SwathWidthY * dml_ceil(BytePerPixelDETY, 1)
			+ PrefetchSourceLinesC * SwathWidthY / 2 * dml_ceil(BytePerPixelDETC, 2))
			/ Tsw_oto;

	if (VirtualMemoryEnable == true) {
		Tvm_oto =
				dml_max(
						*Tno_bw + PDEAndMetaPTEBytesFrame / prefetch_bw_oto,
						dml_max(
								UrgentExtraLatency
										+ UrgentLatency
												* (PageTableLevels
														- 1),
								LineTime / 4.0));
	} else
		Tvm_oto = LineTime / 4.0;

	if ((VirtualMemoryEnable == true || DCCEnable == true)) {
		Tr0_oto = dml_max(
				(MetaRowByte + PixelPTEBytesPerRow) / prefetch_bw_oto,
				dml_max(UrgentLatency, dml_max(LineTime - Tvm_oto, LineTime / 4)));
	} else
		Tr0_oto = LineTime - Tvm_oto;

	Tpre_oto = Tvm_oto + Tr0_oto + Tsw_oto;

	dst_y_prefetch_oto = Tpre_oto / LineTime;

	if (dst_y_prefetch_oto < dst_y_prefetch_equ)
		*DestinationLinesForPrefetch = dst_y_prefetch_oto;
	else
		*DestinationLinesForPrefetch = dst_y_prefetch_equ;

	*DestinationLinesForPrefetch = dml_floor(4.0 * (*DestinationLinesForPrefetch + 0.125), 1)
			/ 4;

	dml_print("DML: VStartup: %d\n", VStartup);
	dml_print("DML: TCalc: %f\n", TCalc);
	dml_print("DML: TWait: %f\n", TWait);
	dml_print("DML: XFCRemoteSurfaceFlipDelay: %f\n", XFCRemoteSurfaceFlipDelay);
	dml_print("DML: LineTime: %f\n", LineTime);
	dml_print("DML: Tsetup: %f\n", Tsetup);
	dml_print("DML: Tdm: %f\n", Tdm);
	dml_print("DML: DSTYAfterScaler: %f\n", *DSTYAfterScaler);
	dml_print("DML: DSTXAfterScaler: %f\n", *DSTXAfterScaler);
	dml_print("DML: HTotal: %d\n", HTotal);

	*PrefetchBandwidth = 0;
	*DestinationLinesToRequestVMInVBlank = 0;
	*DestinationLinesToRequestRowInVBlank = 0;
	*VRatioPrefetchY = 0;
	*VRatioPrefetchC = 0;
	*RequiredPrefetchPixDataBW = 0;
	if (*DestinationLinesForPrefetch > 1) {
		*PrefetchBandwidth = (PDEAndMetaPTEBytesFrame + 2 * MetaRowByte
				+ 2 * PixelPTEBytesPerRow
				+ PrefetchSourceLinesY * SwathWidthY * dml_ceil(BytePerPixelDETY, 1)
				+ PrefetchSourceLinesC * SwathWidthY / 2
						* dml_ceil(BytePerPixelDETC, 2))
				/ (*DestinationLinesForPrefetch * LineTime - *Tno_bw);
		if (VirtualMemoryEnable) {
			TimeForFetchingMetaPTE =
					dml_max(
							*Tno_bw
									+ (double) PDEAndMetaPTEBytesFrame
											/ *PrefetchBandwidth,
							dml_max(
									UrgentExtraLatency
											+ UrgentLatency
													* (PageTableLevels
															- 1),
									LineTime / 4));
		} else {
			if (NumberOfCursors > 0 || XFCEnabled)
				TimeForFetchingMetaPTE = LineTime / 4;
			else
				TimeForFetchingMetaPTE = 0.0;
		}

		if ((VirtualMemoryEnable == true || DCCEnable == true)) {
			TimeForFetchingRowInVBlank =
					dml_max(
							(MetaRowByte + PixelPTEBytesPerRow)
									/ *PrefetchBandwidth,
							dml_max(
									UrgentLatency,
									dml_max(
											LineTime
													- TimeForFetchingMetaPTE,
											LineTime
													/ 4.0)));
		} else {
			if (NumberOfCursors > 0 || XFCEnabled)
				TimeForFetchingRowInVBlank = LineTime - TimeForFetchingMetaPTE;
			else
				TimeForFetchingRowInVBlank = 0.0;
		}

		*DestinationLinesToRequestVMInVBlank = dml_floor(
				4.0 * (TimeForFetchingMetaPTE / LineTime + 0.125),
				1) / 4.0;

		*DestinationLinesToRequestRowInVBlank = dml_floor(
				4.0 * (TimeForFetchingRowInVBlank / LineTime + 0.125),
				1) / 4.0;

		LinesToRequestPrefetchPixelData =
				*DestinationLinesForPrefetch
						- ((NumberOfCursors > 0 || VirtualMemoryEnable
								|| DCCEnable) ?
								(*DestinationLinesToRequestVMInVBlank
										+ *DestinationLinesToRequestRowInVBlank) :
								0.0);

		if (LinesToRequestPrefetchPixelData > 0) {

			*VRatioPrefetchY = (double) PrefetchSourceLinesY
					/ LinesToRequestPrefetchPixelData;
			*VRatioPrefetchY = dml_max(*VRatioPrefetchY, 1.0);
			if ((SwathHeightY > 4) && (VInitPreFillY > 3)) {
				if (LinesToRequestPrefetchPixelData > (VInitPreFillY - 3.0) / 2.0) {
					*VRatioPrefetchY =
							dml_max(
									(double) PrefetchSourceLinesY
											/ LinesToRequestPrefetchPixelData,
									(double) MaxNumSwathY
											* SwathHeightY
											/ (LinesToRequestPrefetchPixelData
													- (VInitPreFillY
															- 3.0)
															/ 2.0));
					*VRatioPrefetchY = dml_max(*VRatioPrefetchY, 1.0);
				} else {
					MyError = true;
					*VRatioPrefetchY = 0;
				}
			}

			*VRatioPrefetchC = (double) PrefetchSourceLinesC
					/ LinesToRequestPrefetchPixelData;
			*VRatioPrefetchC = dml_max(*VRatioPrefetchC, 1.0);

			if ((SwathHeightC > 4)) {
				if (LinesToRequestPrefetchPixelData > (VInitPreFillC - 3.0) / 2.0) {
					*VRatioPrefetchC =
							dml_max(
									*VRatioPrefetchC,
									(double) MaxNumSwathC
											* SwathHeightC
											/ (LinesToRequestPrefetchPixelData
													- (VInitPreFillC
															- 3.0)
															/ 2.0));
					*VRatioPrefetchC = dml_max(*VRatioPrefetchC, 1.0);
				} else {
					MyError = true;
					*VRatioPrefetchC = 0;
				}
			}

			*RequiredPrefetchPixDataBW =
					DPPPerPlane
							* ((double) PrefetchSourceLinesY
									/ LinesToRequestPrefetchPixelData
									* dml_ceil(
											BytePerPixelDETY,
											1)
									+ (double) PrefetchSourceLinesC
											/ LinesToRequestPrefetchPixelData
											* dml_ceil(
													BytePerPixelDETC,
													2)
											/ 2)
							* SwathWidthY / LineTime;
		} else {
			MyError = true;
			*VRatioPrefetchY = 0;
			*VRatioPrefetchC = 0;
			*RequiredPrefetchPixDataBW = 0;
		}

	} else {
		MyError = true;
	}

	if (MyError) {
		*PrefetchBandwidth = 0;
		TimeForFetchingMetaPTE = 0;
		TimeForFetchingRowInVBlank = 0;
		*DestinationLinesToRequestVMInVBlank = 0;
		*DestinationLinesToRequestRowInVBlank = 0;
		*DestinationLinesForPrefetch = 0;
		LinesToRequestPrefetchPixelData = 0;
		*VRatioPrefetchY = 0;
		*VRatioPrefetchC = 0;
		*RequiredPrefetchPixDataBW = 0;
	}

	return MyError;
}

static double RoundToDFSGranularityUp(double Clock, double VCOSpeed)
{
	return VCOSpeed * 4 / dml_floor(VCOSpeed * 4 / Clock, 1);
}

static double RoundToDFSGranularityDown(double Clock, double VCOSpeed)
{
	return VCOSpeed * 4 / dml_ceil(VCOSpeed * 4 / Clock, 1);
}

static double CalculatePrefetchSourceLines(
		struct display_mode_lib *mode_lib,
		double VRatio,
		double vtaps,
		bool Interlace,
		bool ProgressiveToInterlaceUnitInOPP,
		unsigned int SwathHeight,
		unsigned int ViewportYStart,
		double *VInitPreFill,
		unsigned int *MaxNumSwath)
{
	unsigned int MaxPartialSwath;

	if (ProgressiveToInterlaceUnitInOPP)
		*VInitPreFill = dml_floor((VRatio + vtaps + 1) / 2.0, 1);
	else
		*VInitPreFill = dml_floor((VRatio + vtaps + 1 + Interlace * 0.5 * VRatio) / 2.0, 1);

	if (!mode_lib->vba.IgnoreViewportPositioning) {

		*MaxNumSwath = dml_ceil((*VInitPreFill - 1.0) / SwathHeight, 1) + 1.0;

		if (*VInitPreFill > 1.0)
			MaxPartialSwath = (unsigned int) (*VInitPreFill - 2) % SwathHeight;
		else
			MaxPartialSwath = (unsigned int) (*VInitPreFill + SwathHeight - 2)
					% SwathHeight;
		MaxPartialSwath = dml_max(1U, MaxPartialSwath);

	} else {

		if (ViewportYStart != 0)
			dml_print(
					"WARNING DML: using viewport y position of 0 even though actual viewport y position is non-zero in prefetch source lines calculation\n");

		*MaxNumSwath = dml_ceil(*VInitPreFill / SwathHeight, 1);

		if (*VInitPreFill > 1.0)
			MaxPartialSwath = (unsigned int) (*VInitPreFill - 1) % SwathHeight;
		else
			MaxPartialSwath = (unsigned int) (*VInitPreFill + SwathHeight - 1)
					% SwathHeight;
	}

	return *MaxNumSwath * SwathHeight + MaxPartialSwath;
}

static unsigned int CalculateVMAndRowBytes(
		struct display_mode_lib *mode_lib,
		bool DCCEnable,
		unsigned int BlockHeight256Bytes,
		unsigned int BlockWidth256Bytes,
		enum source_format_class SourcePixelFormat,
		unsigned int SurfaceTiling,
		unsigned int BytePerPixel,
		enum scan_direction_class ScanDirection,
		unsigned int ViewportWidth,
		unsigned int ViewportHeight,
		unsigned int SwathWidth,
		bool VirtualMemoryEnable,
		unsigned int VMMPageSize,
		unsigned int PTEBufferSizeInRequests,
		unsigned int PDEProcessingBufIn64KBReqs,
		unsigned int Pitch,
		unsigned int DCCMetaPitch,
		unsigned int *MacroTileWidth,
		unsigned int *MetaRowByte,
		unsigned int *PixelPTEBytesPerRow,
		bool *PTEBufferSizeNotExceeded,
		unsigned int *dpte_row_height,
		unsigned int *meta_row_height)
{
	unsigned int MetaRequestHeight;
	unsigned int MetaRequestWidth;
	unsigned int MetaSurfWidth;
	unsigned int MetaSurfHeight;
	unsigned int MPDEBytesFrame;
	unsigned int MetaPTEBytesFrame;
	unsigned int DCCMetaSurfaceBytes;

	unsigned int MacroTileSizeBytes;
	unsigned int MacroTileHeight;
	unsigned int DPDE0BytesFrame;
	unsigned int ExtraDPDEBytesFrame;
	unsigned int PDEAndMetaPTEBytesFrame;

	if (DCCEnable == true) {
		MetaRequestHeight = 8 * BlockHeight256Bytes;
		MetaRequestWidth = 8 * BlockWidth256Bytes;
		if (ScanDirection == dm_horz) {
			*meta_row_height = MetaRequestHeight;
			MetaSurfWidth = dml_ceil((double) SwathWidth - 1, MetaRequestWidth)
					+ MetaRequestWidth;
			*MetaRowByte = MetaSurfWidth * MetaRequestHeight * BytePerPixel / 256.0;
		} else {
			*meta_row_height = MetaRequestWidth;
			MetaSurfHeight = dml_ceil((double) SwathWidth - 1, MetaRequestHeight)
					+ MetaRequestHeight;
			*MetaRowByte = MetaSurfHeight * MetaRequestWidth * BytePerPixel / 256.0;
		}
		if (ScanDirection == dm_horz) {
			DCCMetaSurfaceBytes = DCCMetaPitch
					* (dml_ceil(ViewportHeight - 1, 64 * BlockHeight256Bytes)
							+ 64 * BlockHeight256Bytes) * BytePerPixel
					/ 256;
		} else {
			DCCMetaSurfaceBytes = DCCMetaPitch
					* (dml_ceil(
							(double) ViewportHeight - 1,
							64 * BlockHeight256Bytes)
							+ 64 * BlockHeight256Bytes) * BytePerPixel
					/ 256;
		}
		if (VirtualMemoryEnable == true) {
			MetaPTEBytesFrame = (dml_ceil(
					(double) (DCCMetaSurfaceBytes - VMMPageSize)
							/ (8 * VMMPageSize),
					1) + 1) * 64;
			MPDEBytesFrame = 128 * (mode_lib->vba.MaxPageTableLevels - 1);
		} else {
			MetaPTEBytesFrame = 0;
			MPDEBytesFrame = 0;
		}
	} else {
		MetaPTEBytesFrame = 0;
		MPDEBytesFrame = 0;
		*MetaRowByte = 0;
	}

	if (SurfaceTiling == dm_sw_linear) {
		MacroTileSizeBytes = 256;
		MacroTileHeight = 1;
	} else if (SurfaceTiling == dm_sw_4kb_s || SurfaceTiling == dm_sw_4kb_s_x
			|| SurfaceTiling == dm_sw_4kb_d || SurfaceTiling == dm_sw_4kb_d_x) {
		MacroTileSizeBytes = 4096;
		MacroTileHeight = 4 * BlockHeight256Bytes;
	} else if (SurfaceTiling == dm_sw_64kb_s || SurfaceTiling == dm_sw_64kb_s_t
			|| SurfaceTiling == dm_sw_64kb_s_x || SurfaceTiling == dm_sw_64kb_d
			|| SurfaceTiling == dm_sw_64kb_d_t || SurfaceTiling == dm_sw_64kb_d_x
			|| SurfaceTiling == dm_sw_64kb_r_x) {
		MacroTileSizeBytes = 65536;
		MacroTileHeight = 16 * BlockHeight256Bytes;
	} else {
		MacroTileSizeBytes = 262144;
		MacroTileHeight = 32 * BlockHeight256Bytes;
	}
	*MacroTileWidth = MacroTileSizeBytes / BytePerPixel / MacroTileHeight;

	if (VirtualMemoryEnable == true && mode_lib->vba.MaxPageTableLevels > 1) {
		if (ScanDirection == dm_horz) {
			DPDE0BytesFrame =
					64
							* (dml_ceil(
									((Pitch
											* (dml_ceil(
													ViewportHeight
															- 1,
													MacroTileHeight)
													+ MacroTileHeight)
											* BytePerPixel)
											- MacroTileSizeBytes)
											/ (8
													* 2097152),
									1) + 1);
		} else {
			DPDE0BytesFrame =
					64
							* (dml_ceil(
									((Pitch
											* (dml_ceil(
													(double) SwathWidth
															- 1,
													MacroTileHeight)
													+ MacroTileHeight)
											* BytePerPixel)
											- MacroTileSizeBytes)
											/ (8
													* 2097152),
									1) + 1);
		}
		ExtraDPDEBytesFrame = 128 * (mode_lib->vba.MaxPageTableLevels - 2);
	} else {
		DPDE0BytesFrame = 0;
		ExtraDPDEBytesFrame = 0;
	}

	PDEAndMetaPTEBytesFrame = MetaPTEBytesFrame + MPDEBytesFrame + DPDE0BytesFrame
			+ ExtraDPDEBytesFrame;

	if (VirtualMemoryEnable == true) {
		unsigned int PTERequestSize;
		unsigned int PixelPTEReqHeight;
		unsigned int PixelPTEReqWidth;
		double FractionOfPTEReturnDrop;
		unsigned int EffectivePDEProcessingBufIn64KBReqs;

		if (SurfaceTiling == dm_sw_linear) {
			PixelPTEReqHeight = 1;
			PixelPTEReqWidth = 8.0 * VMMPageSize / BytePerPixel;
			PTERequestSize = 64;
			FractionOfPTEReturnDrop = 0;
		} else if (MacroTileSizeBytes == 4096) {
			PixelPTEReqHeight = MacroTileHeight;
			PixelPTEReqWidth = 8 * *MacroTileWidth;
			PTERequestSize = 64;
			if (ScanDirection == dm_horz)
				FractionOfPTEReturnDrop = 0;
			else
				FractionOfPTEReturnDrop = 7 / 8;
		} else if (VMMPageSize == 4096 && MacroTileSizeBytes > 4096) {
			PixelPTEReqHeight = 16 * BlockHeight256Bytes;
			PixelPTEReqWidth = 16 * BlockWidth256Bytes;
			PTERequestSize = 128;
			FractionOfPTEReturnDrop = 0;
		} else {
			PixelPTEReqHeight = MacroTileHeight;
			PixelPTEReqWidth = 8 * *MacroTileWidth;
			PTERequestSize = 64;
			FractionOfPTEReturnDrop = 0;
		}

		if (SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10)
			EffectivePDEProcessingBufIn64KBReqs = PDEProcessingBufIn64KBReqs / 2;
		else
			EffectivePDEProcessingBufIn64KBReqs = PDEProcessingBufIn64KBReqs;

		if (SurfaceTiling == dm_sw_linear) {
			*dpte_row_height =
					dml_min(
							128,
							1
									<< (unsigned int) dml_floor(
											dml_log2(
													dml_min(
															(double) PTEBufferSizeInRequests
																	* PixelPTEReqWidth,
															EffectivePDEProcessingBufIn64KBReqs
																	* 65536.0
																	/ BytePerPixel)
															/ Pitch),
											1));
			*PixelPTEBytesPerRow = PTERequestSize
					* (dml_ceil(
							(double) (Pitch * *dpte_row_height - 1)
									/ PixelPTEReqWidth,
							1) + 1);
		} else if (ScanDirection == dm_horz) {
			*dpte_row_height = PixelPTEReqHeight;
			*PixelPTEBytesPerRow = PTERequestSize
					* (dml_ceil(((double) SwathWidth - 1) / PixelPTEReqWidth, 1)
							+ 1);
		} else {
			*dpte_row_height = dml_min(PixelPTEReqWidth, *MacroTileWidth);
			*PixelPTEBytesPerRow = PTERequestSize
					* (dml_ceil(
							((double) SwathWidth - 1)
									/ PixelPTEReqHeight,
							1) + 1);
		}
		if (*PixelPTEBytesPerRow * (1 - FractionOfPTEReturnDrop)
				<= 64 * PTEBufferSizeInRequests) {
			*PTEBufferSizeNotExceeded = true;
		} else {
			*PTEBufferSizeNotExceeded = false;
		}
	} else {
		*PixelPTEBytesPerRow = 0;
		*PTEBufferSizeNotExceeded = true;
	}

	return PDEAndMetaPTEBytesFrame;
}

static void DISPCLKDPPCLKDCFCLKDeepSleepPrefetchParametersWatermarksAndPerformanceCalculation(
		struct display_mode_lib *mode_lib)
{
	unsigned int j, k;

	mode_lib->vba.WritebackDISPCLK = 0.0;
	mode_lib->vba.DISPCLKWithRamping = 0;
	mode_lib->vba.DISPCLKWithoutRamping = 0;
	mode_lib->vba.GlobalDPPCLK = 0.0;

	// dml_ml->vba.DISPCLK and dml_ml->vba.DPPCLK Calculation
	//
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.WritebackEnable[k]) {
			mode_lib->vba.WritebackDISPCLK =
					dml_max(
							mode_lib->vba.WritebackDISPCLK,
							CalculateWriteBackDISPCLK(
									mode_lib->vba.WritebackPixelFormat[k],
									mode_lib->vba.PixelClock[k],
									mode_lib->vba.WritebackHRatio[k],
									mode_lib->vba.WritebackVRatio[k],
									mode_lib->vba.WritebackLumaHTaps[k],
									mode_lib->vba.WritebackLumaVTaps[k],
									mode_lib->vba.WritebackChromaHTaps[k],
									mode_lib->vba.WritebackChromaVTaps[k],
									mode_lib->vba.WritebackDestinationWidth[k],
									mode_lib->vba.HTotal[k],
									mode_lib->vba.WritebackChromaLineBufferWidth));
		}
	}

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.HRatio[k] > 1) {
			mode_lib->vba.PSCL_THROUGHPUT_LUMA[k] = dml_min(
					mode_lib->vba.MaxDCHUBToPSCLThroughput,
					mode_lib->vba.MaxPSCLToLBThroughput
							* mode_lib->vba.HRatio[k]
							/ dml_ceil(
									mode_lib->vba.htaps[k]
											/ 6.0,
									1));
		} else {
			mode_lib->vba.PSCL_THROUGHPUT_LUMA[k] = dml_min(
					mode_lib->vba.MaxDCHUBToPSCLThroughput,
					mode_lib->vba.MaxPSCLToLBThroughput);
		}

		mode_lib->vba.DPPCLKUsingSingleDPPLuma =
				mode_lib->vba.PixelClock[k]
						* dml_max(
								mode_lib->vba.vtaps[k] / 6.0
										* dml_min(
												1.0,
												mode_lib->vba.HRatio[k]),
								dml_max(
										mode_lib->vba.HRatio[k]
												* mode_lib->vba.VRatio[k]
												/ mode_lib->vba.PSCL_THROUGHPUT_LUMA[k],
										1.0));

		if ((mode_lib->vba.htaps[k] > 6 || mode_lib->vba.vtaps[k] > 6)
				&& mode_lib->vba.DPPCLKUsingSingleDPPLuma
						< 2 * mode_lib->vba.PixelClock[k]) {
			mode_lib->vba.DPPCLKUsingSingleDPPLuma = 2 * mode_lib->vba.PixelClock[k];
		}

		if ((mode_lib->vba.SourcePixelFormat[k] != dm_420_8
				&& mode_lib->vba.SourcePixelFormat[k] != dm_420_10)) {
			mode_lib->vba.PSCL_THROUGHPUT_CHROMA[k] = 0.0;
			mode_lib->vba.DPPCLKUsingSingleDPP[k] =
					mode_lib->vba.DPPCLKUsingSingleDPPLuma;
		} else {
			if (mode_lib->vba.HRatio[k] > 1) {
				mode_lib->vba.PSCL_THROUGHPUT_CHROMA[k] =
						dml_min(
								mode_lib->vba.MaxDCHUBToPSCLThroughput,
								mode_lib->vba.MaxPSCLToLBThroughput
										* mode_lib->vba.HRatio[k]
										/ 2
										/ dml_ceil(
												mode_lib->vba.HTAPsChroma[k]
														/ 6.0,
												1.0));
			} else {
				mode_lib->vba.PSCL_THROUGHPUT_CHROMA[k] = dml_min(
						mode_lib->vba.MaxDCHUBToPSCLThroughput,
						mode_lib->vba.MaxPSCLToLBThroughput);
			}
			mode_lib->vba.DPPCLKUsingSingleDPPChroma =
					mode_lib->vba.PixelClock[k]
							* dml_max(
									mode_lib->vba.VTAPsChroma[k]
											/ 6.0
											* dml_min(
													1.0,
													mode_lib->vba.HRatio[k]
															/ 2),
									dml_max(
											mode_lib->vba.HRatio[k]
													* mode_lib->vba.VRatio[k]
													/ 4
													/ mode_lib->vba.PSCL_THROUGHPUT_CHROMA[k],
											1.0));

			if ((mode_lib->vba.HTAPsChroma[k] > 6 || mode_lib->vba.VTAPsChroma[k] > 6)
					&& mode_lib->vba.DPPCLKUsingSingleDPPChroma
							< 2 * mode_lib->vba.PixelClock[k]) {
				mode_lib->vba.DPPCLKUsingSingleDPPChroma = 2
						* mode_lib->vba.PixelClock[k];
			}

			mode_lib->vba.DPPCLKUsingSingleDPP[k] = dml_max(
					mode_lib->vba.DPPCLKUsingSingleDPPLuma,
					mode_lib->vba.DPPCLKUsingSingleDPPChroma);
		}
	}

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.BlendingAndTiming[k] != k)
			continue;
		if (mode_lib->vba.ODMCombineEnabled[k]) {
			mode_lib->vba.DISPCLKWithRamping =
					dml_max(
							mode_lib->vba.DISPCLKWithRamping,
							mode_lib->vba.PixelClock[k] / 2
									* (1
											+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
													/ 100)
									* (1
											+ mode_lib->vba.DISPCLKRampingMargin
													/ 100));
			mode_lib->vba.DISPCLKWithoutRamping =
					dml_max(
							mode_lib->vba.DISPCLKWithoutRamping,
							mode_lib->vba.PixelClock[k] / 2
									* (1
											+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
													/ 100));
		} else if (!mode_lib->vba.ODMCombineEnabled[k]) {
			mode_lib->vba.DISPCLKWithRamping =
					dml_max(
							mode_lib->vba.DISPCLKWithRamping,
							mode_lib->vba.PixelClock[k]
									* (1
											+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
													/ 100)
									* (1
											+ mode_lib->vba.DISPCLKRampingMargin
													/ 100));
			mode_lib->vba.DISPCLKWithoutRamping =
					dml_max(
							mode_lib->vba.DISPCLKWithoutRamping,
							mode_lib->vba.PixelClock[k]
									* (1
											+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
													/ 100));
		}
	}

	mode_lib->vba.DISPCLKWithRamping = dml_max(
			mode_lib->vba.DISPCLKWithRamping,
			mode_lib->vba.WritebackDISPCLK);
	mode_lib->vba.DISPCLKWithoutRamping = dml_max(
			mode_lib->vba.DISPCLKWithoutRamping,
			mode_lib->vba.WritebackDISPCLK);

	ASSERT(mode_lib->vba.DISPCLKDPPCLKVCOSpeed != 0);
	mode_lib->vba.DISPCLKWithRampingRoundedToDFSGranularity = RoundToDFSGranularityUp(
			mode_lib->vba.DISPCLKWithRamping,
			mode_lib->vba.DISPCLKDPPCLKVCOSpeed);
	mode_lib->vba.DISPCLKWithoutRampingRoundedToDFSGranularity = RoundToDFSGranularityUp(
			mode_lib->vba.DISPCLKWithoutRamping,
			mode_lib->vba.DISPCLKDPPCLKVCOSpeed);
	mode_lib->vba.MaxDispclkRoundedToDFSGranularity = RoundToDFSGranularityDown(
			mode_lib->vba.soc.clock_limits[NumberOfStates - 1].dispclk_mhz,
			mode_lib->vba.DISPCLKDPPCLKVCOSpeed);
	if (mode_lib->vba.DISPCLKWithoutRampingRoundedToDFSGranularity
			> mode_lib->vba.MaxDispclkRoundedToDFSGranularity) {
		mode_lib->vba.DISPCLK_calculated =
				mode_lib->vba.DISPCLKWithoutRampingRoundedToDFSGranularity;
	} else if (mode_lib->vba.DISPCLKWithRampingRoundedToDFSGranularity
			> mode_lib->vba.MaxDispclkRoundedToDFSGranularity) {
		mode_lib->vba.DISPCLK_calculated = mode_lib->vba.MaxDispclkRoundedToDFSGranularity;
	} else {
		mode_lib->vba.DISPCLK_calculated =
				mode_lib->vba.DISPCLKWithRampingRoundedToDFSGranularity;
	}
	DTRACE("   dispclk_mhz (calculated) = %f", mode_lib->vba.DISPCLK_calculated);

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		mode_lib->vba.DPPCLK_calculated[k] = mode_lib->vba.DPPCLKUsingSingleDPP[k]
				/ mode_lib->vba.DPPPerPlane[k]
				* (1 + mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100);
		mode_lib->vba.GlobalDPPCLK = dml_max(
				mode_lib->vba.GlobalDPPCLK,
				mode_lib->vba.DPPCLK_calculated[k]);
	}
	mode_lib->vba.GlobalDPPCLK = RoundToDFSGranularityUp(
			mode_lib->vba.GlobalDPPCLK,
			mode_lib->vba.DISPCLKDPPCLKVCOSpeed);
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		mode_lib->vba.DPPCLK_calculated[k] = mode_lib->vba.GlobalDPPCLK / 255
				* dml_ceil(
						mode_lib->vba.DPPCLK_calculated[k] * 255
								/ mode_lib->vba.GlobalDPPCLK,
						1);
		DTRACE("   dppclk_mhz[%i] (calculated) = %f", k, mode_lib->vba.DPPCLK_calculated[k]);
	}

	// Urgent Watermark
	mode_lib->vba.DCCEnabledAnyPlane = false;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k)
		if (mode_lib->vba.DCCEnable[k])
			mode_lib->vba.DCCEnabledAnyPlane = true;

	mode_lib->vba.ReturnBandwidthToDCN = dml_min(
			mode_lib->vba.ReturnBusWidth * mode_lib->vba.DCFCLK,
			mode_lib->vba.FabricAndDRAMBandwidth * 1000)
			* mode_lib->vba.PercentOfIdealDRAMAndFabricBWReceivedAfterUrgLatency / 100;

	mode_lib->vba.ReturnBW = mode_lib->vba.ReturnBandwidthToDCN;
	mode_lib->vba.ReturnBW = adjust_ReturnBW(
			mode_lib,
			mode_lib->vba.ReturnBW,
			mode_lib->vba.DCCEnabledAnyPlane,
			mode_lib->vba.ReturnBandwidthToDCN);

	// Let's do this calculation again??
	mode_lib->vba.ReturnBandwidthToDCN = dml_min(
			mode_lib->vba.ReturnBusWidth * mode_lib->vba.DCFCLK,
			mode_lib->vba.FabricAndDRAMBandwidth * 1000);
	mode_lib->vba.ReturnBW = adjust_ReturnBW(
			mode_lib,
			mode_lib->vba.ReturnBW,
			mode_lib->vba.DCCEnabledAnyPlane,
			mode_lib->vba.ReturnBandwidthToDCN);

	DTRACE("   dcfclk_mhz         = %f", mode_lib->vba.DCFCLK);
	DTRACE("   return_bw_to_dcn   = %f", mode_lib->vba.ReturnBandwidthToDCN);
	DTRACE("   return_bus_bw      = %f", mode_lib->vba.ReturnBW);

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		bool MainPlaneDoesODMCombine = false;

		if (mode_lib->vba.SourceScan[k] == dm_horz)
			mode_lib->vba.SwathWidthSingleDPPY[k] = mode_lib->vba.ViewportWidth[k];
		else
			mode_lib->vba.SwathWidthSingleDPPY[k] = mode_lib->vba.ViewportHeight[k];

		if (mode_lib->vba.ODMCombineEnabled[k] == true)
			MainPlaneDoesODMCombine = true;
		for (j = 0; j < mode_lib->vba.NumberOfActivePlanes; ++j)
			if (mode_lib->vba.BlendingAndTiming[k] == j
					&& mode_lib->vba.ODMCombineEnabled[j] == true)
				MainPlaneDoesODMCombine = true;

		if (MainPlaneDoesODMCombine == true)
			mode_lib->vba.SwathWidthY[k] = dml_min(
					(double) mode_lib->vba.SwathWidthSingleDPPY[k],
					dml_round(
							mode_lib->vba.HActive[k] / 2.0
									* mode_lib->vba.HRatio[k]));
		else
			mode_lib->vba.SwathWidthY[k] = mode_lib->vba.SwathWidthSingleDPPY[k]
					/ mode_lib->vba.DPPPerPlane[k];
	}

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.SourcePixelFormat[k] == dm_444_64) {
			mode_lib->vba.BytePerPixelDETY[k] = 8;
			mode_lib->vba.BytePerPixelDETC[k] = 0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_444_32) {
			mode_lib->vba.BytePerPixelDETY[k] = 4;
			mode_lib->vba.BytePerPixelDETC[k] = 0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_444_16) {
			mode_lib->vba.BytePerPixelDETY[k] = 2;
			mode_lib->vba.BytePerPixelDETC[k] = 0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_444_8) {
			mode_lib->vba.BytePerPixelDETY[k] = 1;
			mode_lib->vba.BytePerPixelDETC[k] = 0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_420_8) {
			mode_lib->vba.BytePerPixelDETY[k] = 1;
			mode_lib->vba.BytePerPixelDETC[k] = 2;
		} else { // dm_420_10
			mode_lib->vba.BytePerPixelDETY[k] = 4.0 / 3.0;
			mode_lib->vba.BytePerPixelDETC[k] = 8.0 / 3.0;
		}
	}

	mode_lib->vba.TotalDataReadBandwidth = 0.0;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		mode_lib->vba.ReadBandwidthPlaneLuma[k] = mode_lib->vba.SwathWidthSingleDPPY[k]
				* dml_ceil(mode_lib->vba.BytePerPixelDETY[k], 1)
				/ (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k])
				* mode_lib->vba.VRatio[k];
		mode_lib->vba.ReadBandwidthPlaneChroma[k] = mode_lib->vba.SwathWidthSingleDPPY[k]
				/ 2 * dml_ceil(mode_lib->vba.BytePerPixelDETC[k], 2)
				/ (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k])
				* mode_lib->vba.VRatio[k] / 2;
		DTRACE(
				"   read_bw[%i] = %fBps",
				k,
				mode_lib->vba.ReadBandwidthPlaneLuma[k]
						+ mode_lib->vba.ReadBandwidthPlaneChroma[k]);
		mode_lib->vba.TotalDataReadBandwidth += mode_lib->vba.ReadBandwidthPlaneLuma[k]
				+ mode_lib->vba.ReadBandwidthPlaneChroma[k];
	}

	mode_lib->vba.TotalDCCActiveDPP = 0;
	mode_lib->vba.TotalActiveDPP = 0;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		mode_lib->vba.TotalActiveDPP = mode_lib->vba.TotalActiveDPP
				+ mode_lib->vba.DPPPerPlane[k];
		if (mode_lib->vba.DCCEnable[k])
			mode_lib->vba.TotalDCCActiveDPP = mode_lib->vba.TotalDCCActiveDPP
					+ mode_lib->vba.DPPPerPlane[k];
	}

	mode_lib->vba.UrgentRoundTripAndOutOfOrderLatency =
			(mode_lib->vba.RoundTripPingLatencyCycles + 32) / mode_lib->vba.DCFCLK
					+ mode_lib->vba.UrgentOutOfOrderReturnPerChannel
							* mode_lib->vba.NumberOfChannels
							/ mode_lib->vba.ReturnBW;

	mode_lib->vba.LastPixelOfLineExtraWatermark = 0;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		double DataFabricLineDeliveryTimeLuma, DataFabricLineDeliveryTimeChroma;

		if (mode_lib->vba.VRatio[k] <= 1.0)
			mode_lib->vba.DisplayPipeLineDeliveryTimeLuma[k] =
					(double) mode_lib->vba.SwathWidthY[k]
							* mode_lib->vba.DPPPerPlane[k]
							/ mode_lib->vba.HRatio[k]
							/ mode_lib->vba.PixelClock[k];
		else
			mode_lib->vba.DisplayPipeLineDeliveryTimeLuma[k] =
					(double) mode_lib->vba.SwathWidthY[k]
							/ mode_lib->vba.PSCL_THROUGHPUT_LUMA[k]
							/ mode_lib->vba.DPPCLK[k];

		DataFabricLineDeliveryTimeLuma = mode_lib->vba.SwathWidthSingleDPPY[k]
				* mode_lib->vba.SwathHeightY[k]
				* dml_ceil(mode_lib->vba.BytePerPixelDETY[k], 1)
				/ (mode_lib->vba.ReturnBW * mode_lib->vba.ReadBandwidthPlaneLuma[k]
						/ mode_lib->vba.TotalDataReadBandwidth);
		mode_lib->vba.LastPixelOfLineExtraWatermark = dml_max(
				mode_lib->vba.LastPixelOfLineExtraWatermark,
				DataFabricLineDeliveryTimeLuma
						- mode_lib->vba.DisplayPipeLineDeliveryTimeLuma[k]);

		if (mode_lib->vba.BytePerPixelDETC[k] == 0)
			mode_lib->vba.DisplayPipeLineDeliveryTimeChroma[k] = 0.0;
		else if (mode_lib->vba.VRatio[k] / 2.0 <= 1.0)
			mode_lib->vba.DisplayPipeLineDeliveryTimeChroma[k] =
					mode_lib->vba.SwathWidthY[k] / 2.0
							* mode_lib->vba.DPPPerPlane[k]
							/ (mode_lib->vba.HRatio[k] / 2.0)
							/ mode_lib->vba.PixelClock[k];
		else
			mode_lib->vba.DisplayPipeLineDeliveryTimeChroma[k] =
					mode_lib->vba.SwathWidthY[k] / 2.0
							/ mode_lib->vba.PSCL_THROUGHPUT_CHROMA[k]
							/ mode_lib->vba.DPPCLK[k];

		DataFabricLineDeliveryTimeChroma = mode_lib->vba.SwathWidthSingleDPPY[k] / 2.0
				* mode_lib->vba.SwathHeightC[k]
				* dml_ceil(mode_lib->vba.BytePerPixelDETC[k], 2)
				/ (mode_lib->vba.ReturnBW
						* mode_lib->vba.ReadBandwidthPlaneChroma[k]
						/ mode_lib->vba.TotalDataReadBandwidth);
		mode_lib->vba.LastPixelOfLineExtraWatermark =
				dml_max(
						mode_lib->vba.LastPixelOfLineExtraWatermark,
						DataFabricLineDeliveryTimeChroma
								- mode_lib->vba.DisplayPipeLineDeliveryTimeChroma[k]);
	}

	mode_lib->vba.UrgentExtraLatency = mode_lib->vba.UrgentRoundTripAndOutOfOrderLatency
			+ (mode_lib->vba.TotalActiveDPP * mode_lib->vba.PixelChunkSizeInKByte
					+ mode_lib->vba.TotalDCCActiveDPP
							* mode_lib->vba.MetaChunkSize) * 1024.0
					/ mode_lib->vba.ReturnBW;

	if (mode_lib->vba.VirtualMemoryEnable)
		mode_lib->vba.UrgentExtraLatency += mode_lib->vba.TotalActiveDPP
				* mode_lib->vba.PTEChunkSize * 1024.0 / mode_lib->vba.ReturnBW;

	mode_lib->vba.UrgentWatermark = mode_lib->vba.UrgentLatency
			+ mode_lib->vba.LastPixelOfLineExtraWatermark
			+ mode_lib->vba.UrgentExtraLatency;

	DTRACE("   urgent_extra_latency = %fus", mode_lib->vba.UrgentExtraLatency);
	DTRACE("   wm_urgent = %fus", mode_lib->vba.UrgentWatermark);

	mode_lib->vba.MemoryTripWatermark = mode_lib->vba.UrgentLatency;

	mode_lib->vba.TotalActiveWriteback = 0;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.WritebackEnable[k])
			mode_lib->vba.TotalActiveWriteback = mode_lib->vba.TotalActiveWriteback + 1;
	}

	if (mode_lib->vba.TotalActiveWriteback <= 1)
		mode_lib->vba.WritebackUrgentWatermark = mode_lib->vba.WritebackLatency;
	else
		mode_lib->vba.WritebackUrgentWatermark = mode_lib->vba.WritebackLatency
				+ mode_lib->vba.WritebackChunkSize * 1024.0 / 32
						/ mode_lib->vba.SOCCLK;

	DTRACE("   wm_wb_urgent = %fus", mode_lib->vba.WritebackUrgentWatermark);

	// NB P-State/DRAM Clock Change Watermark
	mode_lib->vba.DRAMClockChangeWatermark = mode_lib->vba.DRAMClockChangeLatency
			+ mode_lib->vba.UrgentWatermark;

	DTRACE("   wm_pstate_change = %fus", mode_lib->vba.DRAMClockChangeWatermark);

	DTRACE("   calculating wb pstate watermark");
	DTRACE("      total wb outputs %d", mode_lib->vba.TotalActiveWriteback);
	DTRACE("      socclk frequency %f Mhz", mode_lib->vba.SOCCLK);

	if (mode_lib->vba.TotalActiveWriteback <= 1)
		mode_lib->vba.WritebackDRAMClockChangeWatermark =
				mode_lib->vba.DRAMClockChangeLatency
						+ mode_lib->vba.WritebackLatency;
	else
		mode_lib->vba.WritebackDRAMClockChangeWatermark =
				mode_lib->vba.DRAMClockChangeLatency
						+ mode_lib->vba.WritebackLatency
						+ mode_lib->vba.WritebackChunkSize * 1024.0 / 32
								/ mode_lib->vba.SOCCLK;

	DTRACE("   wm_wb_pstate %fus", mode_lib->vba.WritebackDRAMClockChangeWatermark);

	// Stutter Efficiency
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		mode_lib->vba.LinesInDETY[k] = mode_lib->vba.DETBufferSizeY[k]
				/ mode_lib->vba.BytePerPixelDETY[k] / mode_lib->vba.SwathWidthY[k];
		mode_lib->vba.LinesInDETYRoundedDownToSwath[k] = dml_floor(
				mode_lib->vba.LinesInDETY[k],
				mode_lib->vba.SwathHeightY[k]);
		mode_lib->vba.FullDETBufferingTimeY[k] =
				mode_lib->vba.LinesInDETYRoundedDownToSwath[k]
						* (mode_lib->vba.HTotal[k]
								/ mode_lib->vba.PixelClock[k])
						/ mode_lib->vba.VRatio[k];
		if (mode_lib->vba.BytePerPixelDETC[k] > 0) {
			mode_lib->vba.LinesInDETC[k] = mode_lib->vba.DETBufferSizeC[k]
					/ mode_lib->vba.BytePerPixelDETC[k]
					/ (mode_lib->vba.SwathWidthY[k] / 2);
			mode_lib->vba.LinesInDETCRoundedDownToSwath[k] = dml_floor(
					mode_lib->vba.LinesInDETC[k],
					mode_lib->vba.SwathHeightC[k]);
			mode_lib->vba.FullDETBufferingTimeC[k] =
					mode_lib->vba.LinesInDETCRoundedDownToSwath[k]
							* (mode_lib->vba.HTotal[k]
									/ mode_lib->vba.PixelClock[k])
							/ (mode_lib->vba.VRatio[k] / 2);
		} else {
			mode_lib->vba.LinesInDETC[k] = 0;
			mode_lib->vba.LinesInDETCRoundedDownToSwath[k] = 0;
			mode_lib->vba.FullDETBufferingTimeC[k] = 999999;
		}
	}

	mode_lib->vba.MinFullDETBufferingTime = 999999.0;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.FullDETBufferingTimeY[k]
				< mode_lib->vba.MinFullDETBufferingTime) {
			mode_lib->vba.MinFullDETBufferingTime =
					mode_lib->vba.FullDETBufferingTimeY[k];
			mode_lib->vba.FrameTimeForMinFullDETBufferingTime =
					(double) mode_lib->vba.VTotal[k] * mode_lib->vba.HTotal[k]
							/ mode_lib->vba.PixelClock[k];
		}
		if (mode_lib->vba.FullDETBufferingTimeC[k]
				< mode_lib->vba.MinFullDETBufferingTime) {
			mode_lib->vba.MinFullDETBufferingTime =
					mode_lib->vba.FullDETBufferingTimeC[k];
			mode_lib->vba.FrameTimeForMinFullDETBufferingTime =
					(double) mode_lib->vba.VTotal[k] * mode_lib->vba.HTotal[k]
							/ mode_lib->vba.PixelClock[k];
		}
	}

	mode_lib->vba.AverageReadBandwidthGBytePerSecond = 0.0;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.DCCEnable[k]) {
			mode_lib->vba.AverageReadBandwidthGBytePerSecond =
					mode_lib->vba.AverageReadBandwidthGBytePerSecond
							+ mode_lib->vba.ReadBandwidthPlaneLuma[k]
									/ mode_lib->vba.DCCRate[k]
									/ 1000
							+ mode_lib->vba.ReadBandwidthPlaneChroma[k]
									/ mode_lib->vba.DCCRate[k]
									/ 1000;
		} else {
			mode_lib->vba.AverageReadBandwidthGBytePerSecond =
					mode_lib->vba.AverageReadBandwidthGBytePerSecond
							+ mode_lib->vba.ReadBandwidthPlaneLuma[k]
									/ 1000
							+ mode_lib->vba.ReadBandwidthPlaneChroma[k]
									/ 1000;
		}
		if (mode_lib->vba.DCCEnable[k]) {
			mode_lib->vba.AverageReadBandwidthGBytePerSecond =
					mode_lib->vba.AverageReadBandwidthGBytePerSecond
							+ mode_lib->vba.ReadBandwidthPlaneLuma[k]
									/ 1000 / 256
							+ mode_lib->vba.ReadBandwidthPlaneChroma[k]
									/ 1000 / 256;
		}
		if (mode_lib->vba.VirtualMemoryEnable) {
			mode_lib->vba.AverageReadBandwidthGBytePerSecond =
					mode_lib->vba.AverageReadBandwidthGBytePerSecond
							+ mode_lib->vba.ReadBandwidthPlaneLuma[k]
									/ 1000 / 512
							+ mode_lib->vba.ReadBandwidthPlaneChroma[k]
									/ 1000 / 512;
		}
	}

	mode_lib->vba.PartOfBurstThatFitsInROB =
			dml_min(
					mode_lib->vba.MinFullDETBufferingTime
							* mode_lib->vba.TotalDataReadBandwidth,
					mode_lib->vba.ROBBufferSizeInKByte * 1024
							* mode_lib->vba.TotalDataReadBandwidth
							/ (mode_lib->vba.AverageReadBandwidthGBytePerSecond
									* 1000));
	mode_lib->vba.StutterBurstTime = mode_lib->vba.PartOfBurstThatFitsInROB
			* (mode_lib->vba.AverageReadBandwidthGBytePerSecond * 1000)
			/ mode_lib->vba.TotalDataReadBandwidth / mode_lib->vba.ReturnBW
			+ (mode_lib->vba.MinFullDETBufferingTime
					* mode_lib->vba.TotalDataReadBandwidth
					- mode_lib->vba.PartOfBurstThatFitsInROB)
					/ (mode_lib->vba.DCFCLK * 64);
	if (mode_lib->vba.TotalActiveWriteback == 0) {
		mode_lib->vba.StutterEfficiencyNotIncludingVBlank = (1
				- (mode_lib->vba.SRExitTime + mode_lib->vba.StutterBurstTime)
						/ mode_lib->vba.MinFullDETBufferingTime) * 100;
	} else {
		mode_lib->vba.StutterEfficiencyNotIncludingVBlank = 0;
	}

	mode_lib->vba.SmallestVBlank = 999999;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.SynchronizedVBlank || mode_lib->vba.NumberOfActivePlanes == 1) {
			mode_lib->vba.VBlankTime = (double) (mode_lib->vba.VTotal[k]
					- mode_lib->vba.VActive[k]) * mode_lib->vba.HTotal[k]
					/ mode_lib->vba.PixelClock[k];
		} else {
			mode_lib->vba.VBlankTime = 0;
		}
		mode_lib->vba.SmallestVBlank = dml_min(
				mode_lib->vba.SmallestVBlank,
				mode_lib->vba.VBlankTime);
	}

	mode_lib->vba.StutterEfficiency = (mode_lib->vba.StutterEfficiencyNotIncludingVBlank / 100
			* (mode_lib->vba.FrameTimeForMinFullDETBufferingTime
					- mode_lib->vba.SmallestVBlank)
			+ mode_lib->vba.SmallestVBlank)
			/ mode_lib->vba.FrameTimeForMinFullDETBufferingTime * 100;

	// dml_ml->vba.DCFCLK Deep Sleep
	mode_lib->vba.DCFClkDeepSleep = 8.0;

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; k++) {
		if (mode_lib->vba.BytePerPixelDETC[k] > 0) {
			mode_lib->vba.DCFCLKDeepSleepPerPlane =
					dml_max(
							1.1 * mode_lib->vba.SwathWidthY[k]
									* dml_ceil(
											mode_lib->vba.BytePerPixelDETY[k],
											1) / 32
									/ mode_lib->vba.DisplayPipeLineDeliveryTimeLuma[k],
							1.1 * mode_lib->vba.SwathWidthY[k] / 2.0
									* dml_ceil(
											mode_lib->vba.BytePerPixelDETC[k],
											2) / 32
									/ mode_lib->vba.DisplayPipeLineDeliveryTimeChroma[k]);
		} else
			mode_lib->vba.DCFCLKDeepSleepPerPlane = 1.1 * mode_lib->vba.SwathWidthY[k]
					* dml_ceil(mode_lib->vba.BytePerPixelDETY[k], 1) / 64.0
					/ mode_lib->vba.DisplayPipeLineDeliveryTimeLuma[k];
		mode_lib->vba.DCFCLKDeepSleepPerPlane = dml_max(
				mode_lib->vba.DCFCLKDeepSleepPerPlane,
				mode_lib->vba.PixelClock[k] / 16.0);
		mode_lib->vba.DCFClkDeepSleep = dml_max(
				mode_lib->vba.DCFClkDeepSleep,
				mode_lib->vba.DCFCLKDeepSleepPerPlane);

		DTRACE(
				"   dcfclk_deepsleep_per_plane[%i] = %fMHz",
				k,
				mode_lib->vba.DCFCLKDeepSleepPerPlane);
	}

	DTRACE("   dcfclk_deepsleep_mhz = %fMHz", mode_lib->vba.DCFClkDeepSleep);

	// Stutter Watermark
	mode_lib->vba.StutterExitWatermark = mode_lib->vba.SRExitTime
			+ mode_lib->vba.LastPixelOfLineExtraWatermark
			+ mode_lib->vba.UrgentExtraLatency + 10 / mode_lib->vba.DCFClkDeepSleep;
	mode_lib->vba.StutterEnterPlusExitWatermark = mode_lib->vba.SREnterPlusExitTime
			+ mode_lib->vba.LastPixelOfLineExtraWatermark
			+ mode_lib->vba.UrgentExtraLatency;

	DTRACE("   wm_cstate_exit       = %fus", mode_lib->vba.StutterExitWatermark);
	DTRACE("   wm_cstate_enter_exit = %fus", mode_lib->vba.StutterEnterPlusExitWatermark);

	// Urgent Latency Supported
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		mode_lib->vba.EffectiveDETPlusLBLinesLuma =
				dml_floor(
						mode_lib->vba.LinesInDETY[k]
								+ dml_min(
										mode_lib->vba.LinesInDETY[k]
												* mode_lib->vba.DPPCLK[k]
												* mode_lib->vba.BytePerPixelDETY[k]
												* mode_lib->vba.PSCL_THROUGHPUT_LUMA[k]
												/ (mode_lib->vba.ReturnBW
														/ mode_lib->vba.DPPPerPlane[k]),
										(double) mode_lib->vba.EffectiveLBLatencyHidingSourceLinesLuma),
						mode_lib->vba.SwathHeightY[k]);

		mode_lib->vba.UrgentLatencySupportUsLuma = mode_lib->vba.EffectiveDETPlusLBLinesLuma
				* (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k])
				/ mode_lib->vba.VRatio[k]
				- mode_lib->vba.EffectiveDETPlusLBLinesLuma
						* mode_lib->vba.SwathWidthY[k]
						* mode_lib->vba.BytePerPixelDETY[k]
						/ (mode_lib->vba.ReturnBW
								/ mode_lib->vba.DPPPerPlane[k]);

		if (mode_lib->vba.BytePerPixelDETC[k] > 0) {
			mode_lib->vba.EffectiveDETPlusLBLinesChroma =
					dml_floor(
							mode_lib->vba.LinesInDETC[k]
									+ dml_min(
											mode_lib->vba.LinesInDETC[k]
													* mode_lib->vba.DPPCLK[k]
													* mode_lib->vba.BytePerPixelDETC[k]
													* mode_lib->vba.PSCL_THROUGHPUT_CHROMA[k]
													/ (mode_lib->vba.ReturnBW
															/ mode_lib->vba.DPPPerPlane[k]),
											(double) mode_lib->vba.EffectiveLBLatencyHidingSourceLinesChroma),
							mode_lib->vba.SwathHeightC[k]);
			mode_lib->vba.UrgentLatencySupportUsChroma =
					mode_lib->vba.EffectiveDETPlusLBLinesChroma
							* (mode_lib->vba.HTotal[k]
									/ mode_lib->vba.PixelClock[k])
							/ (mode_lib->vba.VRatio[k] / 2)
							- mode_lib->vba.EffectiveDETPlusLBLinesChroma
									* (mode_lib->vba.SwathWidthY[k]
											/ 2)
									* mode_lib->vba.BytePerPixelDETC[k]
									/ (mode_lib->vba.ReturnBW
											/ mode_lib->vba.DPPPerPlane[k]);
			mode_lib->vba.UrgentLatencySupportUs[k] = dml_min(
					mode_lib->vba.UrgentLatencySupportUsLuma,
					mode_lib->vba.UrgentLatencySupportUsChroma);
		} else {
			mode_lib->vba.UrgentLatencySupportUs[k] =
					mode_lib->vba.UrgentLatencySupportUsLuma;
		}
	}

	mode_lib->vba.MinUrgentLatencySupportUs = 999999;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		mode_lib->vba.MinUrgentLatencySupportUs = dml_min(
				mode_lib->vba.MinUrgentLatencySupportUs,
				mode_lib->vba.UrgentLatencySupportUs[k]);
	}

	// Non-Urgent Latency Tolerance
	mode_lib->vba.NonUrgentLatencyTolerance = mode_lib->vba.MinUrgentLatencySupportUs
			- mode_lib->vba.UrgentWatermark;

	// DSCCLK
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if ((mode_lib->vba.BlendingAndTiming[k] != k) || !mode_lib->vba.DSCEnabled[k]) {
			mode_lib->vba.DSCCLK_calculated[k] = 0.0;
		} else {
			if (mode_lib->vba.OutputFormat[k] == dm_420
					|| mode_lib->vba.OutputFormat[k] == dm_n422)
				mode_lib->vba.DSCFormatFactor = 2;
			else
				mode_lib->vba.DSCFormatFactor = 1;
			if (mode_lib->vba.ODMCombineEnabled[k])
				mode_lib->vba.DSCCLK_calculated[k] =
						mode_lib->vba.PixelClockBackEnd[k] / 6
								/ mode_lib->vba.DSCFormatFactor
								/ (1
										- mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
												/ 100);
			else
				mode_lib->vba.DSCCLK_calculated[k] =
						mode_lib->vba.PixelClockBackEnd[k] / 3
								/ mode_lib->vba.DSCFormatFactor
								/ (1
										- mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
												/ 100);
		}
	}

	// DSC Delay
	// TODO
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		double bpp = mode_lib->vba.OutputBpp[k];
		unsigned int slices = mode_lib->vba.NumberOfDSCSlices[k];

		if (mode_lib->vba.DSCEnabled[k] && bpp != 0) {
			if (!mode_lib->vba.ODMCombineEnabled[k]) {
				mode_lib->vba.DSCDelay[k] =
						dscceComputeDelay(
								mode_lib->vba.DSCInputBitPerComponent[k],
								bpp,
								dml_ceil(
										(double) mode_lib->vba.HActive[k]
												/ mode_lib->vba.NumberOfDSCSlices[k],
										1),
								slices,
								mode_lib->vba.OutputFormat[k])
								+ dscComputeDelay(
										mode_lib->vba.OutputFormat[k]);
			} else {
				mode_lib->vba.DSCDelay[k] =
						2
								* (dscceComputeDelay(
										mode_lib->vba.DSCInputBitPerComponent[k],
										bpp,
										dml_ceil(
												(double) mode_lib->vba.HActive[k]
														/ mode_lib->vba.NumberOfDSCSlices[k],
												1),
										slices / 2.0,
										mode_lib->vba.OutputFormat[k])
										+ dscComputeDelay(
												mode_lib->vba.OutputFormat[k]));
			}
			mode_lib->vba.DSCDelay[k] = mode_lib->vba.DSCDelay[k]
					* mode_lib->vba.PixelClock[k]
					/ mode_lib->vba.PixelClockBackEnd[k];
		} else {
			mode_lib->vba.DSCDelay[k] = 0;
		}
	}

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k)
		for (j = 0; j < mode_lib->vba.NumberOfActivePlanes; ++j) // NumberOfPlanes
			if (j != k && mode_lib->vba.BlendingAndTiming[k] == j
					&& mode_lib->vba.DSCEnabled[j])
				mode_lib->vba.DSCDelay[k] = mode_lib->vba.DSCDelay[j];

	// Prefetch
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		unsigned int PDEAndMetaPTEBytesFrameY;
		unsigned int PixelPTEBytesPerRowY;
		unsigned int MetaRowByteY;
		unsigned int MetaRowByteC;
		unsigned int PDEAndMetaPTEBytesFrameC;
		unsigned int PixelPTEBytesPerRowC;

		Calculate256BBlockSizes(
				mode_lib->vba.SourcePixelFormat[k],
				mode_lib->vba.SurfaceTiling[k],
				dml_ceil(mode_lib->vba.BytePerPixelDETY[k], 1),
				dml_ceil(mode_lib->vba.BytePerPixelDETC[k], 2),
				&mode_lib->vba.BlockHeight256BytesY[k],
				&mode_lib->vba.BlockHeight256BytesC[k],
				&mode_lib->vba.BlockWidth256BytesY[k],
				&mode_lib->vba.BlockWidth256BytesC[k]);
		PDEAndMetaPTEBytesFrameY = CalculateVMAndRowBytes(
				mode_lib,
				mode_lib->vba.DCCEnable[k],
				mode_lib->vba.BlockHeight256BytesY[k],
				mode_lib->vba.BlockWidth256BytesY[k],
				mode_lib->vba.SourcePixelFormat[k],
				mode_lib->vba.SurfaceTiling[k],
				dml_ceil(mode_lib->vba.BytePerPixelDETY[k], 1),
				mode_lib->vba.SourceScan[k],
				mode_lib->vba.ViewportWidth[k],
				mode_lib->vba.ViewportHeight[k],
				mode_lib->vba.SwathWidthY[k],
				mode_lib->vba.VirtualMemoryEnable,
				mode_lib->vba.VMMPageSize,
				mode_lib->vba.PTEBufferSizeInRequests,
				mode_lib->vba.PDEProcessingBufIn64KBReqs,
				mode_lib->vba.PitchY[k],
				mode_lib->vba.DCCMetaPitchY[k],
				&mode_lib->vba.MacroTileWidthY[k],
				&MetaRowByteY,
				&PixelPTEBytesPerRowY,
				&mode_lib->vba.PTEBufferSizeNotExceeded[mode_lib->vba.VoltageLevel],
				&mode_lib->vba.dpte_row_height[k],
				&mode_lib->vba.meta_row_height[k]);
		mode_lib->vba.PrefetchSourceLinesY[k] = CalculatePrefetchSourceLines(
				mode_lib,
				mode_lib->vba.VRatio[k],
				mode_lib->vba.vtaps[k],
				mode_lib->vba.Interlace[k],
				mode_lib->vba.ProgressiveToInterlaceUnitInOPP,
				mode_lib->vba.SwathHeightY[k],
				mode_lib->vba.ViewportYStartY[k],
				&mode_lib->vba.VInitPreFillY[k],
				&mode_lib->vba.MaxNumSwathY[k]);

		if ((mode_lib->vba.SourcePixelFormat[k] != dm_444_64
				&& mode_lib->vba.SourcePixelFormat[k] != dm_444_32
				&& mode_lib->vba.SourcePixelFormat[k] != dm_444_16
				&& mode_lib->vba.SourcePixelFormat[k] != dm_444_8)) {
			PDEAndMetaPTEBytesFrameC =
					CalculateVMAndRowBytes(
							mode_lib,
							mode_lib->vba.DCCEnable[k],
							mode_lib->vba.BlockHeight256BytesC[k],
							mode_lib->vba.BlockWidth256BytesC[k],
							mode_lib->vba.SourcePixelFormat[k],
							mode_lib->vba.SurfaceTiling[k],
							dml_ceil(
									mode_lib->vba.BytePerPixelDETC[k],
									2),
							mode_lib->vba.SourceScan[k],
							mode_lib->vba.ViewportWidth[k] / 2,
							mode_lib->vba.ViewportHeight[k] / 2,
							mode_lib->vba.SwathWidthY[k] / 2,
							mode_lib->vba.VirtualMemoryEnable,
							mode_lib->vba.VMMPageSize,
							mode_lib->vba.PTEBufferSizeInRequests,
							mode_lib->vba.PDEProcessingBufIn64KBReqs,
							mode_lib->vba.PitchC[k],
							0,
							&mode_lib->vba.MacroTileWidthC[k],
							&MetaRowByteC,
							&PixelPTEBytesPerRowC,
							&mode_lib->vba.PTEBufferSizeNotExceeded[mode_lib->vba.VoltageLevel],
							&mode_lib->vba.dpte_row_height_chroma[k],
							&mode_lib->vba.meta_row_height_chroma[k]);
			mode_lib->vba.PrefetchSourceLinesC[k] = CalculatePrefetchSourceLines(
					mode_lib,
					mode_lib->vba.VRatio[k] / 2,
					mode_lib->vba.VTAPsChroma[k],
					mode_lib->vba.Interlace[k],
					mode_lib->vba.ProgressiveToInterlaceUnitInOPP,
					mode_lib->vba.SwathHeightC[k],
					mode_lib->vba.ViewportYStartC[k],
					&mode_lib->vba.VInitPreFillC[k],
					&mode_lib->vba.MaxNumSwathC[k]);
		} else {
			PixelPTEBytesPerRowC = 0;
			PDEAndMetaPTEBytesFrameC = 0;
			MetaRowByteC = 0;
			mode_lib->vba.MaxNumSwathC[k] = 0;
			mode_lib->vba.PrefetchSourceLinesC[k] = 0;
		}

		mode_lib->vba.PixelPTEBytesPerRow[k] = PixelPTEBytesPerRowY + PixelPTEBytesPerRowC;
		mode_lib->vba.PDEAndMetaPTEBytesFrame[k] = PDEAndMetaPTEBytesFrameY
				+ PDEAndMetaPTEBytesFrameC;
		mode_lib->vba.MetaRowByte[k] = MetaRowByteY + MetaRowByteC;

		CalculateActiveRowBandwidth(
				mode_lib->vba.VirtualMemoryEnable,
				mode_lib->vba.SourcePixelFormat[k],
				mode_lib->vba.VRatio[k],
				mode_lib->vba.DCCEnable[k],
				mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k],
				MetaRowByteY,
				MetaRowByteC,
				mode_lib->vba.meta_row_height[k],
				mode_lib->vba.meta_row_height_chroma[k],
				PixelPTEBytesPerRowY,
				PixelPTEBytesPerRowC,
				mode_lib->vba.dpte_row_height[k],
				mode_lib->vba.dpte_row_height_chroma[k],
				&mode_lib->vba.meta_row_bw[k],
				&mode_lib->vba.dpte_row_bw[k],
				&mode_lib->vba.qual_row_bw[k]);
	}

	mode_lib->vba.TCalc = 24.0 / mode_lib->vba.DCFClkDeepSleep;

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.BlendingAndTiming[k] == k) {
			if (mode_lib->vba.WritebackEnable[k] == true) {
				mode_lib->vba.WritebackDelay[mode_lib->vba.VoltageLevel][k] =
						mode_lib->vba.WritebackLatency
								+ CalculateWriteBackDelay(
										mode_lib->vba.WritebackPixelFormat[k],
										mode_lib->vba.WritebackHRatio[k],
										mode_lib->vba.WritebackVRatio[k],
										mode_lib->vba.WritebackLumaHTaps[k],
										mode_lib->vba.WritebackLumaVTaps[k],
										mode_lib->vba.WritebackChromaHTaps[k],
										mode_lib->vba.WritebackChromaVTaps[k],
										mode_lib->vba.WritebackDestinationWidth[k])
										/ mode_lib->vba.DISPCLK;
			} else
				mode_lib->vba.WritebackDelay[mode_lib->vba.VoltageLevel][k] = 0;
			for (j = 0; j < mode_lib->vba.NumberOfActivePlanes; ++j) {
				if (mode_lib->vba.BlendingAndTiming[j] == k
						&& mode_lib->vba.WritebackEnable[j] == true) {
					mode_lib->vba.WritebackDelay[mode_lib->vba.VoltageLevel][k] =
							dml_max(
									mode_lib->vba.WritebackDelay[mode_lib->vba.VoltageLevel][k],
									mode_lib->vba.WritebackLatency
											+ CalculateWriteBackDelay(
													mode_lib->vba.WritebackPixelFormat[j],
													mode_lib->vba.WritebackHRatio[j],
													mode_lib->vba.WritebackVRatio[j],
													mode_lib->vba.WritebackLumaHTaps[j],
													mode_lib->vba.WritebackLumaVTaps[j],
													mode_lib->vba.WritebackChromaHTaps[j],
													mode_lib->vba.WritebackChromaVTaps[j],
													mode_lib->vba.WritebackDestinationWidth[j])
													/ mode_lib->vba.DISPCLK);
				}
			}
		}
	}

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k)
		for (j = 0; j < mode_lib->vba.NumberOfActivePlanes; ++j)
			if (mode_lib->vba.BlendingAndTiming[k] == j)
				mode_lib->vba.WritebackDelay[mode_lib->vba.VoltageLevel][k] =
						mode_lib->vba.WritebackDelay[mode_lib->vba.VoltageLevel][j];

	mode_lib->vba.VStartupLines = 13;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		mode_lib->vba.MaxVStartupLines[k] =
				mode_lib->vba.VTotal[k] - mode_lib->vba.VActive[k]
						- dml_max(
								1.0,
								dml_ceil(
										mode_lib->vba.WritebackDelay[mode_lib->vba.VoltageLevel][k]
												/ (mode_lib->vba.HTotal[k]
														/ mode_lib->vba.PixelClock[k]),
										1));
	}

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k)
		mode_lib->vba.MaximumMaxVStartupLines = dml_max(
				mode_lib->vba.MaximumMaxVStartupLines,
				mode_lib->vba.MaxVStartupLines[k]);

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		mode_lib->vba.cursor_bw[k] = 0.0;
		for (j = 0; j < mode_lib->vba.NumberOfCursors[k]; ++j)
			mode_lib->vba.cursor_bw[k] += mode_lib->vba.CursorWidth[k][j]
					* mode_lib->vba.CursorBPP[k][j] / 8.0
					/ (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k])
					* mode_lib->vba.VRatio[k];
	}

	do {
		double MaxTotalRDBandwidth = 0;
		bool DestinationLineTimesForPrefetchLessThan2 = false;
		bool VRatioPrefetchMoreThan4 = false;
		bool prefetch_vm_bw_valid = true;
		bool prefetch_row_bw_valid = true;
		double TWait = CalculateTWait(
				mode_lib->vba.PrefetchMode,
				mode_lib->vba.DRAMClockChangeLatency,
				mode_lib->vba.UrgentLatency,
				mode_lib->vba.SREnterPlusExitTime);

		for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
			if (mode_lib->vba.XFCEnabled[k] == true) {
				mode_lib->vba.XFCRemoteSurfaceFlipDelay =
						CalculateRemoteSurfaceFlipDelay(
								mode_lib,
								mode_lib->vba.VRatio[k],
								mode_lib->vba.SwathWidthY[k],
								dml_ceil(
										mode_lib->vba.BytePerPixelDETY[k],
										1),
								mode_lib->vba.HTotal[k]
										/ mode_lib->vba.PixelClock[k],
								mode_lib->vba.XFCTSlvVupdateOffset,
								mode_lib->vba.XFCTSlvVupdateWidth,
								mode_lib->vba.XFCTSlvVreadyOffset,
								mode_lib->vba.XFCXBUFLatencyTolerance,
								mode_lib->vba.XFCFillBWOverhead,
								mode_lib->vba.XFCSlvChunkSize,
								mode_lib->vba.XFCBusTransportTime,
								mode_lib->vba.TCalc,
								TWait,
								&mode_lib->vba.SrcActiveDrainRate,
								&mode_lib->vba.TInitXFill,
								&mode_lib->vba.TslvChk);
			} else {
				mode_lib->vba.XFCRemoteSurfaceFlipDelay = 0;
			}
			mode_lib->vba.ErrorResult[k] =
					CalculatePrefetchSchedule(
							mode_lib,
							mode_lib->vba.DPPCLK[k],
							mode_lib->vba.DISPCLK,
							mode_lib->vba.PixelClock[k],
							mode_lib->vba.DCFClkDeepSleep,
							mode_lib->vba.DSCDelay[k],
							mode_lib->vba.DPPPerPlane[k],
							mode_lib->vba.ScalerEnabled[k],
							mode_lib->vba.NumberOfCursors[k],
							mode_lib->vba.DPPCLKDelaySubtotal,
							mode_lib->vba.DPPCLKDelaySCL,
							mode_lib->vba.DPPCLKDelaySCLLBOnly,
							mode_lib->vba.DPPCLKDelayCNVCFormater,
							mode_lib->vba.DPPCLKDelayCNVCCursor,
							mode_lib->vba.DISPCLKDelaySubtotal,
							(unsigned int) (mode_lib->vba.SwathWidthY[k]
									/ mode_lib->vba.HRatio[k]),
							mode_lib->vba.OutputFormat[k],
							mode_lib->vba.VTotal[k]
									- mode_lib->vba.VActive[k],
							mode_lib->vba.HTotal[k],
							mode_lib->vba.MaxInterDCNTileRepeaters,
							dml_min(
									mode_lib->vba.VStartupLines,
									mode_lib->vba.MaxVStartupLines[k]),
							mode_lib->vba.MaxPageTableLevels,
							mode_lib->vba.VirtualMemoryEnable,
							mode_lib->vba.DynamicMetadataEnable[k],
							mode_lib->vba.DynamicMetadataLinesBeforeActiveRequired[k],
							mode_lib->vba.DynamicMetadataTransmittedBytes[k],
							mode_lib->vba.DCCEnable[k],
							mode_lib->vba.UrgentLatency,
							mode_lib->vba.UrgentExtraLatency,
							mode_lib->vba.TCalc,
							mode_lib->vba.PDEAndMetaPTEBytesFrame[k],
							mode_lib->vba.MetaRowByte[k],
							mode_lib->vba.PixelPTEBytesPerRow[k],
							mode_lib->vba.PrefetchSourceLinesY[k],
							mode_lib->vba.SwathWidthY[k],
							mode_lib->vba.BytePerPixelDETY[k],
							mode_lib->vba.VInitPreFillY[k],
							mode_lib->vba.MaxNumSwathY[k],
							mode_lib->vba.PrefetchSourceLinesC[k],
							mode_lib->vba.BytePerPixelDETC[k],
							mode_lib->vba.VInitPreFillC[k],
							mode_lib->vba.MaxNumSwathC[k],
							mode_lib->vba.SwathHeightY[k],
							mode_lib->vba.SwathHeightC[k],
							TWait,
							mode_lib->vba.XFCEnabled[k],
							mode_lib->vba.XFCRemoteSurfaceFlipDelay,
							mode_lib->vba.Interlace[k],
							mode_lib->vba.ProgressiveToInterlaceUnitInOPP,
							&mode_lib->vba.DSTXAfterScaler[k],
							&mode_lib->vba.DSTYAfterScaler[k],
							&mode_lib->vba.DestinationLinesForPrefetch[k],
							&mode_lib->vba.PrefetchBandwidth[k],
							&mode_lib->vba.DestinationLinesToRequestVMInVBlank[k],
							&mode_lib->vba.DestinationLinesToRequestRowInVBlank[k],
							&mode_lib->vba.VRatioPrefetchY[k],
							&mode_lib->vba.VRatioPrefetchC[k],
							&mode_lib->vba.RequiredPrefetchPixDataBW[k],
							&mode_lib->vba.VStartupRequiredWhenNotEnoughTimeForDynamicMetadata,
							&mode_lib->vba.Tno_bw[k],
							&mode_lib->vba.VUpdateOffsetPix[k],
							&mode_lib->vba.VUpdateWidthPix[k],
							&mode_lib->vba.VReadyOffsetPix[k]);
			if (mode_lib->vba.BlendingAndTiming[k] == k) {
				mode_lib->vba.VStartup[k] = dml_min(
						mode_lib->vba.VStartupLines,
						mode_lib->vba.MaxVStartupLines[k]);
				if (mode_lib->vba.VStartupRequiredWhenNotEnoughTimeForDynamicMetadata
						!= 0) {
					mode_lib->vba.VStartup[k] =
							mode_lib->vba.VStartupRequiredWhenNotEnoughTimeForDynamicMetadata;
				}
			} else {
				mode_lib->vba.VStartup[k] =
						dml_min(
								mode_lib->vba.VStartupLines,
								mode_lib->vba.MaxVStartupLines[mode_lib->vba.BlendingAndTiming[k]]);
			}
		}

		for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {

			if (mode_lib->vba.PDEAndMetaPTEBytesFrame[k] == 0)
				mode_lib->vba.prefetch_vm_bw[k] = 0;
			else if (mode_lib->vba.DestinationLinesToRequestVMInVBlank[k] > 0) {
				mode_lib->vba.prefetch_vm_bw[k] =
						(double) mode_lib->vba.PDEAndMetaPTEBytesFrame[k]
								/ (mode_lib->vba.DestinationLinesToRequestVMInVBlank[k]
										* mode_lib->vba.HTotal[k]
										/ mode_lib->vba.PixelClock[k]);
			} else {
				mode_lib->vba.prefetch_vm_bw[k] = 0;
				prefetch_vm_bw_valid = false;
			}
			if (mode_lib->vba.MetaRowByte[k] + mode_lib->vba.PixelPTEBytesPerRow[k]
					== 0)
				mode_lib->vba.prefetch_row_bw[k] = 0;
			else if (mode_lib->vba.DestinationLinesToRequestRowInVBlank[k] > 0) {
				mode_lib->vba.prefetch_row_bw[k] =
						(double) (mode_lib->vba.MetaRowByte[k]
								+ mode_lib->vba.PixelPTEBytesPerRow[k])
								/ (mode_lib->vba.DestinationLinesToRequestRowInVBlank[k]
										* mode_lib->vba.HTotal[k]
										/ mode_lib->vba.PixelClock[k]);
			} else {
				mode_lib->vba.prefetch_row_bw[k] = 0;
				prefetch_row_bw_valid = false;
			}

			MaxTotalRDBandwidth =
					MaxTotalRDBandwidth + mode_lib->vba.cursor_bw[k]
							+ dml_max(
									mode_lib->vba.prefetch_vm_bw[k],
									dml_max(
											mode_lib->vba.prefetch_row_bw[k],
											dml_max(
													mode_lib->vba.ReadBandwidthPlaneLuma[k]
															+ mode_lib->vba.ReadBandwidthPlaneChroma[k],
													mode_lib->vba.RequiredPrefetchPixDataBW[k])
													+ mode_lib->vba.meta_row_bw[k]
													+ mode_lib->vba.dpte_row_bw[k]));

			if (mode_lib->vba.DestinationLinesForPrefetch[k] < 2)
				DestinationLineTimesForPrefetchLessThan2 = true;
			if (mode_lib->vba.VRatioPrefetchY[k] > 4
					|| mode_lib->vba.VRatioPrefetchC[k] > 4)
				VRatioPrefetchMoreThan4 = true;
		}

		if (MaxTotalRDBandwidth <= mode_lib->vba.ReturnBW && prefetch_vm_bw_valid
				&& prefetch_row_bw_valid && !VRatioPrefetchMoreThan4
				&& !DestinationLineTimesForPrefetchLessThan2)
			mode_lib->vba.PrefetchModeSupported = true;
		else {
			mode_lib->vba.PrefetchModeSupported = false;
			dml_print(
					"DML: CalculatePrefetchSchedule ***failed***. Bandwidth violation. Results are NOT valid\n");
		}

		if (mode_lib->vba.PrefetchModeSupported == true) {
			double final_flip_bw[DC__NUM_DPP__MAX];
			unsigned int ImmediateFlipBytes[DC__NUM_DPP__MAX];
			double total_dcn_read_bw_with_flip = 0;

			mode_lib->vba.BandwidthAvailableForImmediateFlip = mode_lib->vba.ReturnBW;
			for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
				mode_lib->vba.BandwidthAvailableForImmediateFlip =
						mode_lib->vba.BandwidthAvailableForImmediateFlip
								- mode_lib->vba.cursor_bw[k]
								- dml_max(
										mode_lib->vba.ReadBandwidthPlaneLuma[k]
												+ mode_lib->vba.ReadBandwidthPlaneChroma[k]
												+ mode_lib->vba.qual_row_bw[k],
										mode_lib->vba.PrefetchBandwidth[k]);
			}

			for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
				ImmediateFlipBytes[k] = 0;
				if ((mode_lib->vba.SourcePixelFormat[k] != dm_420_8
						&& mode_lib->vba.SourcePixelFormat[k] != dm_420_10)) {
					ImmediateFlipBytes[k] =
							mode_lib->vba.PDEAndMetaPTEBytesFrame[k]
									+ mode_lib->vba.MetaRowByte[k]
									+ mode_lib->vba.PixelPTEBytesPerRow[k];
				}
			}
			mode_lib->vba.TotImmediateFlipBytes = 0;
			for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
				if ((mode_lib->vba.SourcePixelFormat[k] != dm_420_8
						&& mode_lib->vba.SourcePixelFormat[k] != dm_420_10)) {
					mode_lib->vba.TotImmediateFlipBytes =
							mode_lib->vba.TotImmediateFlipBytes
									+ ImmediateFlipBytes[k];
				}
			}
			for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
				CalculateFlipSchedule(
						mode_lib,
						mode_lib->vba.UrgentExtraLatency,
						mode_lib->vba.UrgentLatency,
						mode_lib->vba.MaxPageTableLevels,
						mode_lib->vba.VirtualMemoryEnable,
						mode_lib->vba.BandwidthAvailableForImmediateFlip,
						mode_lib->vba.TotImmediateFlipBytes,
						mode_lib->vba.SourcePixelFormat[k],
						ImmediateFlipBytes[k],
						mode_lib->vba.HTotal[k]
								/ mode_lib->vba.PixelClock[k],
						mode_lib->vba.VRatio[k],
						mode_lib->vba.Tno_bw[k],
						mode_lib->vba.PDEAndMetaPTEBytesFrame[k],
						mode_lib->vba.MetaRowByte[k],
						mode_lib->vba.PixelPTEBytesPerRow[k],
						mode_lib->vba.DCCEnable[k],
						mode_lib->vba.dpte_row_height[k],
						mode_lib->vba.meta_row_height[k],
						mode_lib->vba.qual_row_bw[k],
						&mode_lib->vba.DestinationLinesToRequestVMInImmediateFlip[k],
						&mode_lib->vba.DestinationLinesToRequestRowInImmediateFlip[k],
						&final_flip_bw[k],
						&mode_lib->vba.ImmediateFlipSupportedForPipe[k]);
			}
			for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
				total_dcn_read_bw_with_flip =
						total_dcn_read_bw_with_flip
								+ mode_lib->vba.cursor_bw[k]
								+ dml_max(
										mode_lib->vba.prefetch_vm_bw[k],
										dml_max(
												mode_lib->vba.prefetch_row_bw[k],
												final_flip_bw[k]
														+ dml_max(
																mode_lib->vba.ReadBandwidthPlaneLuma[k]
																		+ mode_lib->vba.ReadBandwidthPlaneChroma[k],
																mode_lib->vba.RequiredPrefetchPixDataBW[k])));
			}
			mode_lib->vba.ImmediateFlipSupported = true;
			if (total_dcn_read_bw_with_flip > mode_lib->vba.ReturnBW) {
				mode_lib->vba.ImmediateFlipSupported = false;
			}
			for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
				if (mode_lib->vba.ImmediateFlipSupportedForPipe[k] == false) {
					mode_lib->vba.ImmediateFlipSupported = false;
				}
			}
		} else {
			mode_lib->vba.ImmediateFlipSupported = false;
		}

		for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
			if (mode_lib->vba.ErrorResult[k]) {
				mode_lib->vba.PrefetchModeSupported = false;
				dml_print(
						"DML: CalculatePrefetchSchedule ***failed***. Prefetch schedule violation. Results are NOT valid\n");
			}
		}

		mode_lib->vba.VStartupLines = mode_lib->vba.VStartupLines + 1;
	} while (!((mode_lib->vba.PrefetchModeSupported
			&& (!mode_lib->vba.ImmediateFlipSupport
					|| mode_lib->vba.ImmediateFlipSupported))
			|| mode_lib->vba.MaximumMaxVStartupLines < mode_lib->vba.VStartupLines));

	//Display Pipeline Delivery Time in Prefetch
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.VRatioPrefetchY[k] <= 1) {
			mode_lib->vba.DisplayPipeLineDeliveryTimeLumaPrefetch[k] =
					mode_lib->vba.SwathWidthY[k] * mode_lib->vba.DPPPerPlane[k]
							/ mode_lib->vba.HRatio[k]
							/ mode_lib->vba.PixelClock[k];
		} else {
			mode_lib->vba.DisplayPipeLineDeliveryTimeLumaPrefetch[k] =
					mode_lib->vba.SwathWidthY[k]
							/ mode_lib->vba.PSCL_THROUGHPUT_LUMA[k]
							/ mode_lib->vba.DPPCLK[k];
		}
		if (mode_lib->vba.BytePerPixelDETC[k] == 0) {
			mode_lib->vba.DisplayPipeLineDeliveryTimeChromaPrefetch[k] = 0;
		} else {
			if (mode_lib->vba.VRatioPrefetchC[k] <= 1) {
				mode_lib->vba.DisplayPipeLineDeliveryTimeChromaPrefetch[k] =
						mode_lib->vba.SwathWidthY[k]
								* mode_lib->vba.DPPPerPlane[k]
								/ mode_lib->vba.HRatio[k]
								/ mode_lib->vba.PixelClock[k];
			} else {
				mode_lib->vba.DisplayPipeLineDeliveryTimeChromaPrefetch[k] =
						mode_lib->vba.SwathWidthY[k]
								/ mode_lib->vba.PSCL_THROUGHPUT_LUMA[k]
								/ mode_lib->vba.DPPCLK[k];
			}
		}
	}

	// Min TTUVBlank
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.PrefetchMode == 0) {
			mode_lib->vba.AllowDRAMClockChangeDuringVBlank[k] = true;
			mode_lib->vba.AllowDRAMSelfRefreshDuringVBlank[k] = true;
			mode_lib->vba.MinTTUVBlank[k] = dml_max(
					mode_lib->vba.DRAMClockChangeWatermark,
					dml_max(
							mode_lib->vba.StutterEnterPlusExitWatermark,
							mode_lib->vba.UrgentWatermark));
		} else if (mode_lib->vba.PrefetchMode == 1) {
			mode_lib->vba.AllowDRAMClockChangeDuringVBlank[k] = false;
			mode_lib->vba.AllowDRAMSelfRefreshDuringVBlank[k] = true;
			mode_lib->vba.MinTTUVBlank[k] = dml_max(
					mode_lib->vba.StutterEnterPlusExitWatermark,
					mode_lib->vba.UrgentWatermark);
		} else {
			mode_lib->vba.AllowDRAMClockChangeDuringVBlank[k] = false;
			mode_lib->vba.AllowDRAMSelfRefreshDuringVBlank[k] = false;
			mode_lib->vba.MinTTUVBlank[k] = mode_lib->vba.UrgentWatermark;
		}
		if (!mode_lib->vba.DynamicMetadataEnable[k])
			mode_lib->vba.MinTTUVBlank[k] = mode_lib->vba.TCalc
					+ mode_lib->vba.MinTTUVBlank[k];
	}

	// DCC Configuration
	mode_lib->vba.ActiveDPPs = 0;
	// NB P-State/DRAM Clock Change Support
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		mode_lib->vba.ActiveDPPs = mode_lib->vba.ActiveDPPs + mode_lib->vba.DPPPerPlane[k];
	}

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		double EffectiveLBLatencyHidingY;
		double EffectiveLBLatencyHidingC;
		double DPPOutputBufferLinesY;
		double DPPOutputBufferLinesC;
		double DPPOPPBufferingY;
		double MaxDETBufferingTimeY;
		double ActiveDRAMClockChangeLatencyMarginY;

		mode_lib->vba.LBLatencyHidingSourceLinesY =
				dml_min(
						mode_lib->vba.MaxLineBufferLines,
						(unsigned int) dml_floor(
								(double) mode_lib->vba.LineBufferSize
										/ mode_lib->vba.LBBitPerPixel[k]
										/ (mode_lib->vba.SwathWidthY[k]
												/ dml_max(
														mode_lib->vba.HRatio[k],
														1.0)),
								1)) - (mode_lib->vba.vtaps[k] - 1);

		mode_lib->vba.LBLatencyHidingSourceLinesC =
				dml_min(
						mode_lib->vba.MaxLineBufferLines,
						(unsigned int) dml_floor(
								(double) mode_lib->vba.LineBufferSize
										/ mode_lib->vba.LBBitPerPixel[k]
										/ (mode_lib->vba.SwathWidthY[k]
												/ 2.0
												/ dml_max(
														mode_lib->vba.HRatio[k]
																/ 2,
														1.0)),
								1))
						- (mode_lib->vba.VTAPsChroma[k] - 1);

		EffectiveLBLatencyHidingY = mode_lib->vba.LBLatencyHidingSourceLinesY
				/ mode_lib->vba.VRatio[k]
				* (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k]);

		EffectiveLBLatencyHidingC = mode_lib->vba.LBLatencyHidingSourceLinesC
				/ (mode_lib->vba.VRatio[k] / 2)
				* (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k]);

		if (mode_lib->vba.SwathWidthY[k] > 2 * mode_lib->vba.DPPOutputBufferPixels) {
			DPPOutputBufferLinesY = mode_lib->vba.DPPOutputBufferPixels
					/ mode_lib->vba.SwathWidthY[k];
		} else if (mode_lib->vba.SwathWidthY[k] > mode_lib->vba.DPPOutputBufferPixels) {
			DPPOutputBufferLinesY = 0.5;
		} else {
			DPPOutputBufferLinesY = 1;
		}

		if (mode_lib->vba.SwathWidthY[k] / 2 > 2 * mode_lib->vba.DPPOutputBufferPixels) {
			DPPOutputBufferLinesC = mode_lib->vba.DPPOutputBufferPixels
					/ (mode_lib->vba.SwathWidthY[k] / 2);
		} else if (mode_lib->vba.SwathWidthY[k] / 2 > mode_lib->vba.DPPOutputBufferPixels) {
			DPPOutputBufferLinesC = 0.5;
		} else {
			DPPOutputBufferLinesC = 1;
		}

		DPPOPPBufferingY = (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k])
				* (DPPOutputBufferLinesY + mode_lib->vba.OPPOutputBufferLines);
		MaxDETBufferingTimeY = mode_lib->vba.FullDETBufferingTimeY[k]
				+ (mode_lib->vba.LinesInDETY[k]
						- mode_lib->vba.LinesInDETYRoundedDownToSwath[k])
						/ mode_lib->vba.SwathHeightY[k]
						* (mode_lib->vba.HTotal[k]
								/ mode_lib->vba.PixelClock[k]);

		ActiveDRAMClockChangeLatencyMarginY = DPPOPPBufferingY + EffectiveLBLatencyHidingY
				+ MaxDETBufferingTimeY - mode_lib->vba.DRAMClockChangeWatermark;

		if (mode_lib->vba.ActiveDPPs > 1) {
			ActiveDRAMClockChangeLatencyMarginY =
					ActiveDRAMClockChangeLatencyMarginY
							- (1 - 1 / (mode_lib->vba.ActiveDPPs - 1))
									* mode_lib->vba.SwathHeightY[k]
									* (mode_lib->vba.HTotal[k]
											/ mode_lib->vba.PixelClock[k]);
		}

		if (mode_lib->vba.BytePerPixelDETC[k] > 0) {
			double DPPOPPBufferingC = (mode_lib->vba.HTotal[k]
					/ mode_lib->vba.PixelClock[k])
					* (DPPOutputBufferLinesC
							+ mode_lib->vba.OPPOutputBufferLines);
			double MaxDETBufferingTimeC =
					mode_lib->vba.FullDETBufferingTimeC[k]
							+ (mode_lib->vba.LinesInDETC[k]
									- mode_lib->vba.LinesInDETCRoundedDownToSwath[k])
									/ mode_lib->vba.SwathHeightC[k]
									* (mode_lib->vba.HTotal[k]
											/ mode_lib->vba.PixelClock[k]);
			double ActiveDRAMClockChangeLatencyMarginC = DPPOPPBufferingC
					+ EffectiveLBLatencyHidingC + MaxDETBufferingTimeC
					- mode_lib->vba.DRAMClockChangeWatermark;

			if (mode_lib->vba.ActiveDPPs > 1) {
				ActiveDRAMClockChangeLatencyMarginC =
						ActiveDRAMClockChangeLatencyMarginC
								- (1
										- 1
												/ (mode_lib->vba.ActiveDPPs
														- 1))
										* mode_lib->vba.SwathHeightC[k]
										* (mode_lib->vba.HTotal[k]
												/ mode_lib->vba.PixelClock[k]);
			}
			mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k] = dml_min(
					ActiveDRAMClockChangeLatencyMarginY,
					ActiveDRAMClockChangeLatencyMarginC);
		} else {
			mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k] =
					ActiveDRAMClockChangeLatencyMarginY;
		}

		if (mode_lib->vba.WritebackEnable[k]) {
			double WritebackDRAMClockChangeLatencyMargin;

			if (mode_lib->vba.WritebackPixelFormat[k] == dm_444_32) {
				WritebackDRAMClockChangeLatencyMargin =
						(double) (mode_lib->vba.WritebackInterfaceLumaBufferSize
								+ mode_lib->vba.WritebackInterfaceChromaBufferSize)
								/ (mode_lib->vba.WritebackDestinationWidth[k]
										* mode_lib->vba.WritebackDestinationHeight[k]
										/ (mode_lib->vba.WritebackSourceHeight[k]
												* mode_lib->vba.HTotal[k]
												/ mode_lib->vba.PixelClock[k])
										* 4)
								- mode_lib->vba.WritebackDRAMClockChangeWatermark;
			} else if (mode_lib->vba.WritebackPixelFormat[k] == dm_420_10) {
				WritebackDRAMClockChangeLatencyMargin =
						dml_min(
								(double) mode_lib->vba.WritebackInterfaceLumaBufferSize
										* 8.0 / 10,
								2.0
										* mode_lib->vba.WritebackInterfaceChromaBufferSize
										* 8 / 10)
								/ (mode_lib->vba.WritebackDestinationWidth[k]
										* mode_lib->vba.WritebackDestinationHeight[k]
										/ (mode_lib->vba.WritebackSourceHeight[k]
												* mode_lib->vba.HTotal[k]
												/ mode_lib->vba.PixelClock[k]))
								- mode_lib->vba.WritebackDRAMClockChangeWatermark;
			} else {
				WritebackDRAMClockChangeLatencyMargin =
						dml_min(
								(double) mode_lib->vba.WritebackInterfaceLumaBufferSize,
								2.0
										* mode_lib->vba.WritebackInterfaceChromaBufferSize)
								/ (mode_lib->vba.WritebackDestinationWidth[k]
										* mode_lib->vba.WritebackDestinationHeight[k]
										/ (mode_lib->vba.WritebackSourceHeight[k]
												* mode_lib->vba.HTotal[k]
												/ mode_lib->vba.PixelClock[k]))
								- mode_lib->vba.WritebackDRAMClockChangeWatermark;
			}
			mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k] = dml_min(
					mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k],
					WritebackDRAMClockChangeLatencyMargin);
		}
	}

	mode_lib->vba.MinActiveDRAMClockChangeMargin = 999999;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k]
				< mode_lib->vba.MinActiveDRAMClockChangeMargin) {
			mode_lib->vba.MinActiveDRAMClockChangeMargin =
					mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k];
		}
	}

	mode_lib->vba.MinActiveDRAMClockChangeLatencySupported =
			mode_lib->vba.MinActiveDRAMClockChangeMargin
					+ mode_lib->vba.DRAMClockChangeLatency;

	if (mode_lib->vba.MinActiveDRAMClockChangeMargin > 0) {
		mode_lib->vba.DRAMClockChangeSupport = dm_dram_clock_change_vactive;
	} else {
		if (mode_lib->vba.SynchronizedVBlank || mode_lib->vba.NumberOfActivePlanes == 1) {
			mode_lib->vba.DRAMClockChangeSupport = dm_dram_clock_change_vblank;
			for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
				if (!mode_lib->vba.AllowDRAMClockChangeDuringVBlank[k]) {
					mode_lib->vba.DRAMClockChangeSupport =
							dm_dram_clock_change_unsupported;
				}
			}
		} else {
			mode_lib->vba.DRAMClockChangeSupport = dm_dram_clock_change_unsupported;
		}
	}

	//XFC Parameters:
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.XFCEnabled[k] == true) {
			double TWait;

			mode_lib->vba.XFCSlaveVUpdateOffset[k] = mode_lib->vba.XFCTSlvVupdateOffset;
			mode_lib->vba.XFCSlaveVupdateWidth[k] = mode_lib->vba.XFCTSlvVupdateWidth;
			mode_lib->vba.XFCSlaveVReadyOffset[k] = mode_lib->vba.XFCTSlvVreadyOffset;
			TWait = CalculateTWait(
					mode_lib->vba.PrefetchMode,
					mode_lib->vba.DRAMClockChangeLatency,
					mode_lib->vba.UrgentLatency,
					mode_lib->vba.SREnterPlusExitTime);
			mode_lib->vba.XFCRemoteSurfaceFlipDelay = CalculateRemoteSurfaceFlipDelay(
					mode_lib,
					mode_lib->vba.VRatio[k],
					mode_lib->vba.SwathWidthY[k],
					dml_ceil(mode_lib->vba.BytePerPixelDETY[k], 1),
					mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k],
					mode_lib->vba.XFCTSlvVupdateOffset,
					mode_lib->vba.XFCTSlvVupdateWidth,
					mode_lib->vba.XFCTSlvVreadyOffset,
					mode_lib->vba.XFCXBUFLatencyTolerance,
					mode_lib->vba.XFCFillBWOverhead,
					mode_lib->vba.XFCSlvChunkSize,
					mode_lib->vba.XFCBusTransportTime,
					mode_lib->vba.TCalc,
					TWait,
					&mode_lib->vba.SrcActiveDrainRate,
					&mode_lib->vba.TInitXFill,
					&mode_lib->vba.TslvChk);
			mode_lib->vba.XFCRemoteSurfaceFlipLatency[k] =
					dml_floor(
							mode_lib->vba.XFCRemoteSurfaceFlipDelay
									/ (mode_lib->vba.HTotal[k]
											/ mode_lib->vba.PixelClock[k]),
							1);
			mode_lib->vba.XFCTransferDelay[k] =
					dml_ceil(
							mode_lib->vba.XFCBusTransportTime
									/ (mode_lib->vba.HTotal[k]
											/ mode_lib->vba.PixelClock[k]),
							1);
			mode_lib->vba.XFCPrechargeDelay[k] =
					dml_ceil(
							(mode_lib->vba.XFCBusTransportTime
									+ mode_lib->vba.TInitXFill
									+ mode_lib->vba.TslvChk)
									/ (mode_lib->vba.HTotal[k]
											/ mode_lib->vba.PixelClock[k]),
							1);
			mode_lib->vba.InitFillLevel = mode_lib->vba.XFCXBUFLatencyTolerance
					* mode_lib->vba.SrcActiveDrainRate;
			mode_lib->vba.FinalFillMargin =
					(mode_lib->vba.DestinationLinesToRequestVMInVBlank[k]
							+ mode_lib->vba.DestinationLinesToRequestRowInVBlank[k])
							* mode_lib->vba.HTotal[k]
							/ mode_lib->vba.PixelClock[k]
							* mode_lib->vba.SrcActiveDrainRate
							+ mode_lib->vba.XFCFillConstant;
			mode_lib->vba.FinalFillLevel = mode_lib->vba.XFCRemoteSurfaceFlipDelay
					* mode_lib->vba.SrcActiveDrainRate
					+ mode_lib->vba.FinalFillMargin;
			mode_lib->vba.RemainingFillLevel = dml_max(
					0.0,
					mode_lib->vba.FinalFillLevel - mode_lib->vba.InitFillLevel);
			mode_lib->vba.TFinalxFill = mode_lib->vba.RemainingFillLevel
					/ (mode_lib->vba.SrcActiveDrainRate
							* mode_lib->vba.XFCFillBWOverhead / 100);
			mode_lib->vba.XFCPrefetchMargin[k] =
					mode_lib->vba.XFCRemoteSurfaceFlipDelay
							+ mode_lib->vba.TFinalxFill
							+ (mode_lib->vba.DestinationLinesToRequestVMInVBlank[k]
									+ mode_lib->vba.DestinationLinesToRequestRowInVBlank[k])
									* mode_lib->vba.HTotal[k]
									/ mode_lib->vba.PixelClock[k];
		} else {
			mode_lib->vba.XFCSlaveVUpdateOffset[k] = 0;
			mode_lib->vba.XFCSlaveVupdateWidth[k] = 0;
			mode_lib->vba.XFCSlaveVReadyOffset[k] = 0;
			mode_lib->vba.XFCRemoteSurfaceFlipLatency[k] = 0;
			mode_lib->vba.XFCPrechargeDelay[k] = 0;
			mode_lib->vba.XFCTransferDelay[k] = 0;
			mode_lib->vba.XFCPrefetchMargin[k] = 0;
		}
	}
}

static void DisplayPipeConfiguration(struct display_mode_lib *mode_lib)
{
	double BytePerPixDETY;
	double BytePerPixDETC;
	double Read256BytesBlockHeightY;
	double Read256BytesBlockHeightC;
	double Read256BytesBlockWidthY;
	double Read256BytesBlockWidthC;
	double MaximumSwathHeightY;
	double MaximumSwathHeightC;
	double MinimumSwathHeightY;
	double MinimumSwathHeightC;
	double SwathWidth;
	double SwathWidthGranularityY;
	double SwathWidthGranularityC;
	double RoundedUpMaxSwathSizeBytesY;
	double RoundedUpMaxSwathSizeBytesC;
	unsigned int j, k;

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		bool MainPlaneDoesODMCombine = false;

		if (mode_lib->vba.SourcePixelFormat[k] == dm_444_64) {
			BytePerPixDETY = 8;
			BytePerPixDETC = 0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_444_32) {
			BytePerPixDETY = 4;
			BytePerPixDETC = 0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_444_16) {
			BytePerPixDETY = 2;
			BytePerPixDETC = 0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_444_8) {
			BytePerPixDETY = 1;
			BytePerPixDETC = 0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_420_8) {
			BytePerPixDETY = 1;
			BytePerPixDETC = 2;
		} else {
			BytePerPixDETY = 4.0 / 3.0;
			BytePerPixDETC = 8.0 / 3.0;
		}

		if ((mode_lib->vba.SourcePixelFormat[k] == dm_444_64
				|| mode_lib->vba.SourcePixelFormat[k] == dm_444_32
				|| mode_lib->vba.SourcePixelFormat[k] == dm_444_16
				|| mode_lib->vba.SourcePixelFormat[k] == dm_444_8)) {
			if (mode_lib->vba.SurfaceTiling[k] == dm_sw_linear) {
				Read256BytesBlockHeightY = 1;
			} else if (mode_lib->vba.SourcePixelFormat[k] == dm_444_64) {
				Read256BytesBlockHeightY = 4;
			} else if (mode_lib->vba.SourcePixelFormat[k] == dm_444_32
					|| mode_lib->vba.SourcePixelFormat[k] == dm_444_16) {
				Read256BytesBlockHeightY = 8;
			} else {
				Read256BytesBlockHeightY = 16;
			}
			Read256BytesBlockWidthY = 256 / dml_ceil(BytePerPixDETY, 1)
					/ Read256BytesBlockHeightY;
			Read256BytesBlockHeightC = 0;
			Read256BytesBlockWidthC = 0;
		} else {
			if (mode_lib->vba.SurfaceTiling[k] == dm_sw_linear) {
				Read256BytesBlockHeightY = 1;
				Read256BytesBlockHeightC = 1;
			} else if (mode_lib->vba.SourcePixelFormat[k] == dm_420_8) {
				Read256BytesBlockHeightY = 16;
				Read256BytesBlockHeightC = 8;
			} else {
				Read256BytesBlockHeightY = 8;
				Read256BytesBlockHeightC = 8;
			}
			Read256BytesBlockWidthY = 256 / dml_ceil(BytePerPixDETY, 1)
					/ Read256BytesBlockHeightY;
			Read256BytesBlockWidthC = 256 / dml_ceil(BytePerPixDETC, 2)
					/ Read256BytesBlockHeightC;
		}

		if (mode_lib->vba.SourceScan[k] == dm_horz) {
			MaximumSwathHeightY = Read256BytesBlockHeightY;
			MaximumSwathHeightC = Read256BytesBlockHeightC;
		} else {
			MaximumSwathHeightY = Read256BytesBlockWidthY;
			MaximumSwathHeightC = Read256BytesBlockWidthC;
		}

		if ((mode_lib->vba.SourcePixelFormat[k] == dm_444_64
				|| mode_lib->vba.SourcePixelFormat[k] == dm_444_32
				|| mode_lib->vba.SourcePixelFormat[k] == dm_444_16
				|| mode_lib->vba.SourcePixelFormat[k] == dm_444_8)) {
			if (mode_lib->vba.SurfaceTiling[k] == dm_sw_linear
					|| (mode_lib->vba.SourcePixelFormat[k] == dm_444_64
							&& (mode_lib->vba.SurfaceTiling[k]
									== dm_sw_4kb_s
									|| mode_lib->vba.SurfaceTiling[k]
											== dm_sw_4kb_s_x
									|| mode_lib->vba.SurfaceTiling[k]
											== dm_sw_64kb_s
									|| mode_lib->vba.SurfaceTiling[k]
											== dm_sw_64kb_s_t
									|| mode_lib->vba.SurfaceTiling[k]
											== dm_sw_64kb_s_x
									|| mode_lib->vba.SurfaceTiling[k]
											== dm_sw_var_s
									|| mode_lib->vba.SurfaceTiling[k]
											== dm_sw_var_s_x)
							&& mode_lib->vba.SourceScan[k] == dm_horz)) {
				MinimumSwathHeightY = MaximumSwathHeightY;
			} else if (mode_lib->vba.SourcePixelFormat[k] == dm_444_8
					&& mode_lib->vba.SourceScan[k] != dm_horz) {
				MinimumSwathHeightY = MaximumSwathHeightY;
			} else {
				MinimumSwathHeightY = MaximumSwathHeightY / 2.0;
			}
			MinimumSwathHeightC = MaximumSwathHeightC;
		} else {
			if (mode_lib->vba.SurfaceTiling[k] == dm_sw_linear) {
				MinimumSwathHeightY = MaximumSwathHeightY;
				MinimumSwathHeightC = MaximumSwathHeightC;
			} else if (mode_lib->vba.SourcePixelFormat[k] == dm_420_8
					&& mode_lib->vba.SourceScan[k] == dm_horz) {
				MinimumSwathHeightY = MaximumSwathHeightY / 2.0;
				MinimumSwathHeightC = MaximumSwathHeightC;
			} else if (mode_lib->vba.SourcePixelFormat[k] == dm_420_10
					&& mode_lib->vba.SourceScan[k] == dm_horz) {
				MinimumSwathHeightC = MaximumSwathHeightC / 2.0;
				MinimumSwathHeightY = MaximumSwathHeightY;
			} else {
				MinimumSwathHeightY = MaximumSwathHeightY;
				MinimumSwathHeightC = MaximumSwathHeightC;
			}
		}

		if (mode_lib->vba.SourceScan[k] == dm_horz) {
			SwathWidth = mode_lib->vba.ViewportWidth[k];
		} else {
			SwathWidth = mode_lib->vba.ViewportHeight[k];
		}

		if (mode_lib->vba.ODMCombineEnabled[k] == true) {
			MainPlaneDoesODMCombine = true;
		}
		for (j = 0; j < mode_lib->vba.NumberOfActivePlanes; ++j) {
			if (mode_lib->vba.BlendingAndTiming[k] == j
					&& mode_lib->vba.ODMCombineEnabled[j] == true) {
				MainPlaneDoesODMCombine = true;
			}
		}

		if (MainPlaneDoesODMCombine == true) {
			SwathWidth = dml_min(
					SwathWidth,
					mode_lib->vba.HActive[k] / 2.0 * mode_lib->vba.HRatio[k]);
		} else {
			SwathWidth = SwathWidth / mode_lib->vba.DPPPerPlane[k];
		}

		SwathWidthGranularityY = 256 / dml_ceil(BytePerPixDETY, 1) / MaximumSwathHeightY;
		RoundedUpMaxSwathSizeBytesY = (dml_ceil(
				(double) (SwathWidth - 1),
				SwathWidthGranularityY) + SwathWidthGranularityY) * BytePerPixDETY
				* MaximumSwathHeightY;
		if (mode_lib->vba.SourcePixelFormat[k] == dm_420_10) {
			RoundedUpMaxSwathSizeBytesY = dml_ceil(RoundedUpMaxSwathSizeBytesY, 256)
					+ 256;
		}
		if (MaximumSwathHeightC > 0) {
			SwathWidthGranularityC = 256.0 / dml_ceil(BytePerPixDETC, 2)
					/ MaximumSwathHeightC;
			RoundedUpMaxSwathSizeBytesC = (dml_ceil(
					(double) (SwathWidth / 2.0 - 1),
					SwathWidthGranularityC) + SwathWidthGranularityC)
					* BytePerPixDETC * MaximumSwathHeightC;
			if (mode_lib->vba.SourcePixelFormat[k] == dm_420_10) {
				RoundedUpMaxSwathSizeBytesC = dml_ceil(
						RoundedUpMaxSwathSizeBytesC,
						256) + 256;
			}
		} else
			RoundedUpMaxSwathSizeBytesC = 0.0;

		if (RoundedUpMaxSwathSizeBytesY + RoundedUpMaxSwathSizeBytesC
				<= mode_lib->vba.DETBufferSizeInKByte * 1024.0 / 2.0) {
			mode_lib->vba.SwathHeightY[k] = MaximumSwathHeightY;
			mode_lib->vba.SwathHeightC[k] = MaximumSwathHeightC;
		} else {
			mode_lib->vba.SwathHeightY[k] = MinimumSwathHeightY;
			mode_lib->vba.SwathHeightC[k] = MinimumSwathHeightC;
		}

		if (mode_lib->vba.SwathHeightC[k] == 0) {
			mode_lib->vba.DETBufferSizeY[k] = mode_lib->vba.DETBufferSizeInKByte * 1024;
			mode_lib->vba.DETBufferSizeC[k] = 0;
		} else if (mode_lib->vba.SwathHeightY[k] <= mode_lib->vba.SwathHeightC[k]) {
			mode_lib->vba.DETBufferSizeY[k] = mode_lib->vba.DETBufferSizeInKByte
					* 1024.0 / 2;
			mode_lib->vba.DETBufferSizeC[k] = mode_lib->vba.DETBufferSizeInKByte
					* 1024.0 / 2;
		} else {
			mode_lib->vba.DETBufferSizeY[k] = mode_lib->vba.DETBufferSizeInKByte
					* 1024.0 * 2 / 3;
			mode_lib->vba.DETBufferSizeC[k] = mode_lib->vba.DETBufferSizeInKByte
					* 1024.0 / 3;
		}
	}
}

bool Calculate256BBlockSizes(
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
			|| SourcePixelFormat == dm_444_16
			|| SourcePixelFormat == dm_444_8)) {
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
	return true;
}

static double CalculateTWait(
		unsigned int PrefetchMode,
		double DRAMClockChangeLatency,
		double UrgentLatency,
		double SREnterPlusExitTime)
{
	if (PrefetchMode == 0) {
		return dml_max(
				DRAMClockChangeLatency + UrgentLatency,
				dml_max(SREnterPlusExitTime, UrgentLatency));
	} else if (PrefetchMode == 1) {
		return dml_max(SREnterPlusExitTime, UrgentLatency);
	} else {
		return UrgentLatency;
	}
}

static double CalculateRemoteSurfaceFlipDelay(
		struct display_mode_lib *mode_lib,
		double VRatio,
		double SwathWidth,
		double Bpp,
		double LineTime,
		double XFCTSlvVupdateOffset,
		double XFCTSlvVupdateWidth,
		double XFCTSlvVreadyOffset,
		double XFCXBUFLatencyTolerance,
		double XFCFillBWOverhead,
		double XFCSlvChunkSize,
		double XFCBusTransportTime,
		double TCalc,
		double TWait,
		double *SrcActiveDrainRate,
		double *TInitXFill,
		double *TslvChk)
{
	double TSlvSetup, AvgfillRate, result;

	*SrcActiveDrainRate = VRatio * SwathWidth * Bpp / LineTime;
	TSlvSetup = XFCTSlvVupdateOffset + XFCTSlvVupdateWidth + XFCTSlvVreadyOffset;
	*TInitXFill = XFCXBUFLatencyTolerance / (1 + XFCFillBWOverhead / 100);
	AvgfillRate = *SrcActiveDrainRate * (1 + XFCFillBWOverhead / 100);
	*TslvChk = XFCSlvChunkSize / AvgfillRate;
	dml_print(
			"DML::CalculateRemoteSurfaceFlipDelay: SrcActiveDrainRate: %f\n",
			*SrcActiveDrainRate);
	dml_print("DML::CalculateRemoteSurfaceFlipDelay: TSlvSetup: %f\n", TSlvSetup);
	dml_print("DML::CalculateRemoteSurfaceFlipDelay: TInitXFill: %f\n", *TInitXFill);
	dml_print("DML::CalculateRemoteSurfaceFlipDelay: AvgfillRate: %f\n", AvgfillRate);
	dml_print("DML::CalculateRemoteSurfaceFlipDelay: TslvChk: %f\n", *TslvChk);
	result = 2 * XFCBusTransportTime + TSlvSetup + TCalc + TWait + *TslvChk + *TInitXFill; // TODO: This doesn't seem to match programming guide
	dml_print("DML::CalculateRemoteSurfaceFlipDelay: RemoteSurfaceFlipDelay: %f\n", result);
	return result;
}

static double CalculateWriteBackDISPCLK(
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
	double CalculateWriteBackDISPCLK =
			1.01 * PixelClock
					* dml_max(
							dml_ceil(WritebackLumaHTaps / 4.0, 1)
									/ WritebackHRatio,
							dml_max(
									(WritebackLumaVTaps
											* dml_ceil(
													1.0
															/ WritebackVRatio,
													1)
											* dml_ceil(
													WritebackDestinationWidth
															/ 4.0,
													1)
											+ dml_ceil(
													WritebackDestinationWidth
															/ 4.0,
													1))
											/ (double) HTotal
											+ dml_ceil(
													1.0
															/ WritebackVRatio,
													1)
													* (dml_ceil(
															WritebackLumaVTaps
																	/ 4.0,
															1)
															+ 4.0)
													/ (double) HTotal,
									dml_ceil(
											1.0
													/ WritebackVRatio,
											1)
											* WritebackDestinationWidth
											/ (double) HTotal));
	if (WritebackPixelFormat != dm_444_32) {
		CalculateWriteBackDISPCLK =
				dml_max(
						CalculateWriteBackDISPCLK,
						1.01 * PixelClock
								* dml_max(
										dml_ceil(
												WritebackChromaHTaps
														/ 2.0,
												1)
												/ (2
														* WritebackHRatio),
										dml_max(
												(WritebackChromaVTaps
														* dml_ceil(
																1
																		/ (2
																				* WritebackVRatio),
																1)
														* dml_ceil(
																WritebackDestinationWidth
																		/ 2.0
																		/ 2.0,
																1)
														+ dml_ceil(
																WritebackDestinationWidth
																		/ 2.0
																		/ WritebackChromaLineBufferWidth,
																1))
														/ HTotal
														+ dml_ceil(
																1
																		/ (2
																				* WritebackVRatio),
																1)
																* (dml_ceil(
																		WritebackChromaVTaps
																				/ 4.0,
																		1)
																		+ 4)
																/ HTotal,
												dml_ceil(
														1.0
																/ (2
																		* WritebackVRatio),
														1)
														* WritebackDestinationWidth
														/ 2.0
														/ HTotal)));
	}
	return CalculateWriteBackDISPCLK;
}

static double CalculateWriteBackDelay(
		enum source_format_class WritebackPixelFormat,
		double WritebackHRatio,
		double WritebackVRatio,
		unsigned int WritebackLumaHTaps,
		unsigned int WritebackLumaVTaps,
		unsigned int WritebackChromaHTaps,
		unsigned int WritebackChromaVTaps,
		unsigned int WritebackDestinationWidth)
{
	double CalculateWriteBackDelay =
			dml_max(
					dml_ceil(WritebackLumaHTaps / 4.0, 1) / WritebackHRatio,
					WritebackLumaVTaps * dml_ceil(1.0 / WritebackVRatio, 1)
							* dml_ceil(
									WritebackDestinationWidth
											/ 4.0,
									1)
							+ dml_ceil(1.0 / WritebackVRatio, 1)
									* (dml_ceil(
											WritebackLumaVTaps
													/ 4.0,
											1) + 4));

	if (WritebackPixelFormat != dm_444_32) {
		CalculateWriteBackDelay =
				dml_max(
						CalculateWriteBackDelay,
						dml_max(
								dml_ceil(
										WritebackChromaHTaps
												/ 2.0,
										1)
										/ (2
												* WritebackHRatio),
								WritebackChromaVTaps
										* dml_ceil(
												1
														/ (2
																* WritebackVRatio),
												1)
										* dml_ceil(
												WritebackDestinationWidth
														/ 2.0
														/ 2.0,
												1)
										+ dml_ceil(
												1
														/ (2
																* WritebackVRatio),
												1)
												* (dml_ceil(
														WritebackChromaVTaps
																/ 4.0,
														1)
														+ 4)));
	}
	return CalculateWriteBackDelay;
}

static void CalculateActiveRowBandwidth(
		bool VirtualMemoryEnable,
		enum source_format_class SourcePixelFormat,
		double VRatio,
		bool DCCEnable,
		double LineTime,
		unsigned int MetaRowByteLuma,
		unsigned int MetaRowByteChroma,
		unsigned int meta_row_height_luma,
		unsigned int meta_row_height_chroma,
		unsigned int PixelPTEBytesPerRowLuma,
		unsigned int PixelPTEBytesPerRowChroma,
		unsigned int dpte_row_height_luma,
		unsigned int dpte_row_height_chroma,
		double *meta_row_bw,
		double *dpte_row_bw,
		double *qual_row_bw)
{
	if (DCCEnable != true) {
		*meta_row_bw = 0;
	} else if (SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10) {
		*meta_row_bw = VRatio * MetaRowByteLuma / (meta_row_height_luma * LineTime)
				+ VRatio / 2 * MetaRowByteChroma
						/ (meta_row_height_chroma * LineTime);
	} else {
		*meta_row_bw = VRatio * MetaRowByteLuma / (meta_row_height_luma * LineTime);
	}

	if (VirtualMemoryEnable != true) {
		*dpte_row_bw = 0;
	} else if (SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10) {
		*dpte_row_bw = VRatio * PixelPTEBytesPerRowLuma / (dpte_row_height_luma * LineTime)
				+ VRatio / 2 * PixelPTEBytesPerRowChroma
						/ (dpte_row_height_chroma * LineTime);
	} else {
		*dpte_row_bw = VRatio * PixelPTEBytesPerRowLuma / (dpte_row_height_luma * LineTime);
	}

	if ((SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10)) {
		*qual_row_bw = *meta_row_bw + *dpte_row_bw;
	} else {
		*qual_row_bw = 0;
	}
}

static void CalculateFlipSchedule(
		struct display_mode_lib *mode_lib,
		double UrgentExtraLatency,
		double UrgentLatency,
		unsigned int MaxPageTableLevels,
		bool VirtualMemoryEnable,
		double BandwidthAvailableForImmediateFlip,
		unsigned int TotImmediateFlipBytes,
		enum source_format_class SourcePixelFormat,
		unsigned int ImmediateFlipBytes,
		double LineTime,
		double Tno_bw,
		double VRatio,
		double PDEAndMetaPTEBytesFrame,
		unsigned int MetaRowByte,
		unsigned int PixelPTEBytesPerRow,
		bool DCCEnable,
		unsigned int dpte_row_height,
		unsigned int meta_row_height,
		double qual_row_bw,
		double *DestinationLinesToRequestVMInImmediateFlip,
		double *DestinationLinesToRequestRowInImmediateFlip,
		double *final_flip_bw,
		bool *ImmediateFlipSupportedForPipe)
{
	double min_row_time = 0.0;

	if (SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10) {
		*DestinationLinesToRequestVMInImmediateFlip = 0.0;
		*DestinationLinesToRequestRowInImmediateFlip = 0.0;
		*final_flip_bw = qual_row_bw;
		*ImmediateFlipSupportedForPipe = true;
	} else {
		double TimeForFetchingMetaPTEImmediateFlip;
		double TimeForFetchingRowInVBlankImmediateFlip;

		if (VirtualMemoryEnable == true) {
			mode_lib->vba.ImmediateFlipBW = BandwidthAvailableForImmediateFlip
					* ImmediateFlipBytes / TotImmediateFlipBytes;
			TimeForFetchingMetaPTEImmediateFlip =
					dml_max(
							Tno_bw
									+ PDEAndMetaPTEBytesFrame
											/ mode_lib->vba.ImmediateFlipBW,
							dml_max(
									UrgentExtraLatency
											+ UrgentLatency
													* (MaxPageTableLevels
															- 1),
									LineTime / 4.0));
		} else {
			TimeForFetchingMetaPTEImmediateFlip = 0;
		}

		*DestinationLinesToRequestVMInImmediateFlip = dml_floor(
				4.0 * (TimeForFetchingMetaPTEImmediateFlip / LineTime + 0.125),
				1) / 4.0;

		if ((VirtualMemoryEnable == true || DCCEnable == true)) {
			mode_lib->vba.ImmediateFlipBW = BandwidthAvailableForImmediateFlip
					* ImmediateFlipBytes / TotImmediateFlipBytes;
			TimeForFetchingRowInVBlankImmediateFlip = dml_max(
					(MetaRowByte + PixelPTEBytesPerRow)
							/ mode_lib->vba.ImmediateFlipBW,
					dml_max(UrgentLatency, LineTime / 4.0));
		} else {
			TimeForFetchingRowInVBlankImmediateFlip = 0;
		}

		*DestinationLinesToRequestRowInImmediateFlip = dml_floor(
				4.0 * (TimeForFetchingRowInVBlankImmediateFlip / LineTime + 0.125),
				1) / 4.0;

		if (VirtualMemoryEnable == true) {
			*final_flip_bw =
					dml_max(
							PDEAndMetaPTEBytesFrame
									/ (*DestinationLinesToRequestVMInImmediateFlip
											* LineTime),
							(MetaRowByte + PixelPTEBytesPerRow)
									/ (TimeForFetchingRowInVBlankImmediateFlip
											* LineTime));
		} else if (MetaRowByte + PixelPTEBytesPerRow > 0) {
			*final_flip_bw = (MetaRowByte + PixelPTEBytesPerRow)
					/ (TimeForFetchingRowInVBlankImmediateFlip * LineTime);
		} else {
			*final_flip_bw = 0;
		}

		if (VirtualMemoryEnable && !DCCEnable)
			min_row_time = dpte_row_height * LineTime / VRatio;
		else if (!VirtualMemoryEnable && DCCEnable)
			min_row_time = meta_row_height * LineTime / VRatio;
		else
			min_row_time = dml_min(dpte_row_height, meta_row_height) * LineTime
					/ VRatio;

		if (*DestinationLinesToRequestVMInImmediateFlip >= 8
				|| *DestinationLinesToRequestRowInImmediateFlip >= 16
				|| TimeForFetchingMetaPTEImmediateFlip
						+ 2 * TimeForFetchingRowInVBlankImmediateFlip
						> min_row_time)
			*ImmediateFlipSupportedForPipe = false;
		else
			*ImmediateFlipSupportedForPipe = true;
	}
}

static void PixelClockAdjustmentForProgressiveToInterlaceUnit(struct display_mode_lib *mode_lib)
{
	unsigned int k;

	//Progressive To dml_ml->vba.Interlace Unit Effect
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

static unsigned int TruncToValidBPP(
		double DecimalBPP,
		bool DSCEnabled,
		enum output_encoder_class Output,
		enum output_format_class Format,
		unsigned int DSCInputBitPerComponent)
{
	if (Output == dm_hdmi) {
		if (Format == dm_420) {
			if (DecimalBPP >= 18)
				return 18;
			else if (DecimalBPP >= 15)
				return 15;
			else if (DecimalBPP >= 12)
				return 12;
			else
				return BPP_INVALID;
		} else if (Format == dm_444) {
			if (DecimalBPP >= 36)
				return 36;
			else if (DecimalBPP >= 30)
				return 30;
			else if (DecimalBPP >= 24)
				return 24;
			else
				return BPP_INVALID;
		} else {
			if (DecimalBPP / 1.5 >= 24)
				return 24;
			else if (DecimalBPP / 1.5 >= 20)
				return 20;
			else if (DecimalBPP / 1.5 >= 16)
				return 16;
			else
				return BPP_INVALID;
		}
	} else {
		if (DSCEnabled) {
			if (Format == dm_420) {
				if (DecimalBPP < 6)
					return BPP_INVALID;
				else if (DecimalBPP >= 1.5 * DSCInputBitPerComponent - 1 / 16)
					return 1.5 * DSCInputBitPerComponent - 1 / 16;
				else
					return dml_floor(16 * DecimalBPP, 1) / 16;
			} else if (Format == dm_n422) {
				if (DecimalBPP < 7)
					return BPP_INVALID;
				else if (DecimalBPP >= 2 * DSCInputBitPerComponent - 1 / 16)
					return 2 * DSCInputBitPerComponent - 1 / 16;
				else
					return dml_floor(16 * DecimalBPP, 1) / 16;
			} else {
				if (DecimalBPP < 8)
					return BPP_INVALID;
				else if (DecimalBPP >= 3 * DSCInputBitPerComponent - 1 / 16)
					return 3 * DSCInputBitPerComponent - 1 / 16;
				else
					return dml_floor(16 * DecimalBPP, 1) / 16;
			}
		} else if (Format == dm_420) {
			if (DecimalBPP >= 18)
				return 18;
			else if (DecimalBPP >= 15)
				return 15;
			else if (DecimalBPP >= 12)
				return 12;
			else
				return BPP_INVALID;
		} else if (Format == dm_s422 || Format == dm_n422) {
			if (DecimalBPP >= 24)
				return 24;
			else if (DecimalBPP >= 20)
				return 20;
			else if (DecimalBPP >= 16)
				return 16;
			else
				return BPP_INVALID;
		} else {
			if (DecimalBPP >= 36)
				return 36;
			else if (DecimalBPP >= 30)
				return 30;
			else if (DecimalBPP >= 24)
				return 24;
			else
				return BPP_INVALID;
		}
	}
}

static void ModeSupportAndSystemConfigurationFull(struct display_mode_lib *mode_lib)
{
	int i;
	unsigned int j, k;
	/*MODE SUPPORT, VOLTAGE STATE AND SOC CONFIGURATION*/

	/*Scale Ratio, taps Support Check*/

	mode_lib->vba.ScaleRatioAndTapsSupport = true;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.ScalerEnabled[k] == false
				&& ((mode_lib->vba.SourcePixelFormat[k] != dm_444_64
						&& mode_lib->vba.SourcePixelFormat[k] != dm_444_32
						&& mode_lib->vba.SourcePixelFormat[k] != dm_444_16
						&& mode_lib->vba.SourcePixelFormat[k] != dm_mono_16
						&& mode_lib->vba.SourcePixelFormat[k] != dm_mono_8)
						|| mode_lib->vba.HRatio[k] != 1.0
						|| mode_lib->vba.htaps[k] != 1.0
						|| mode_lib->vba.VRatio[k] != 1.0
						|| mode_lib->vba.vtaps[k] != 1.0)) {
			mode_lib->vba.ScaleRatioAndTapsSupport = false;
		} else if (mode_lib->vba.vtaps[k] < 1.0 || mode_lib->vba.vtaps[k] > 8.0
				|| mode_lib->vba.htaps[k] < 1.0 || mode_lib->vba.htaps[k] > 8.0
				|| (mode_lib->vba.htaps[k] > 1.0
						&& (mode_lib->vba.htaps[k] % 2) == 1)
				|| mode_lib->vba.HRatio[k] > mode_lib->vba.MaxHSCLRatio
				|| mode_lib->vba.VRatio[k] > mode_lib->vba.MaxVSCLRatio
				|| mode_lib->vba.HRatio[k] > mode_lib->vba.htaps[k]
				|| mode_lib->vba.VRatio[k] > mode_lib->vba.vtaps[k]
				|| (mode_lib->vba.SourcePixelFormat[k] != dm_444_64
						&& mode_lib->vba.SourcePixelFormat[k] != dm_444_32
						&& mode_lib->vba.SourcePixelFormat[k] != dm_444_16
						&& mode_lib->vba.SourcePixelFormat[k] != dm_mono_16
						&& mode_lib->vba.SourcePixelFormat[k] != dm_mono_8
						&& (mode_lib->vba.HRatio[k] / 2.0
								> mode_lib->vba.HTAPsChroma[k]
								|| mode_lib->vba.VRatio[k] / 2.0
										> mode_lib->vba.VTAPsChroma[k]))) {
			mode_lib->vba.ScaleRatioAndTapsSupport = false;
		}
	}
	/*Source Format, Pixel Format and Scan Support Check*/

	mode_lib->vba.SourceFormatPixelAndScanSupport = true;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if ((mode_lib->vba.SurfaceTiling[k] == dm_sw_linear
				&& mode_lib->vba.SourceScan[k] != dm_horz)
				|| ((mode_lib->vba.SurfaceTiling[k] == dm_sw_4kb_d
						|| mode_lib->vba.SurfaceTiling[k] == dm_sw_4kb_d_x
						|| mode_lib->vba.SurfaceTiling[k] == dm_sw_64kb_d
						|| mode_lib->vba.SurfaceTiling[k] == dm_sw_64kb_d_t
						|| mode_lib->vba.SurfaceTiling[k] == dm_sw_64kb_d_x
						|| mode_lib->vba.SurfaceTiling[k] == dm_sw_var_d
						|| mode_lib->vba.SurfaceTiling[k] == dm_sw_var_d_x)
						&& mode_lib->vba.SourcePixelFormat[k] != dm_444_64)
				|| (mode_lib->vba.SurfaceTiling[k] == dm_sw_64kb_r_x
						&& (mode_lib->vba.SourcePixelFormat[k] == dm_mono_8
								|| mode_lib->vba.SourcePixelFormat[k]
										== dm_420_8
								|| mode_lib->vba.SourcePixelFormat[k]
										== dm_420_10))
				|| (((mode_lib->vba.SurfaceTiling[k]
						== dm_sw_gfx7_2d_thin_gl
						|| mode_lib->vba.SurfaceTiling[k]
								== dm_sw_gfx7_2d_thin_lvp)
						&& !((mode_lib->vba.SourcePixelFormat[k]
								== dm_444_64
								|| mode_lib->vba.SourcePixelFormat[k]
										== dm_444_32)
								&& mode_lib->vba.SourceScan[k]
										== dm_horz
								&& mode_lib->vba.SupportGFX7CompatibleTilingIn32bppAnd64bpp
										== true
								&& mode_lib->vba.DCCEnable[k]
										== false))
						|| (mode_lib->vba.DCCEnable[k] == true
								&& (mode_lib->vba.SurfaceTiling[k]
										== dm_sw_linear
										|| mode_lib->vba.SourcePixelFormat[k]
												== dm_420_8
										|| mode_lib->vba.SourcePixelFormat[k]
												== dm_420_10)))) {
			mode_lib->vba.SourceFormatPixelAndScanSupport = false;
		}
	}
	/*Bandwidth Support Check*/

	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.SourceScan[k] == dm_horz) {
			mode_lib->vba.SwathWidthYSingleDPP[k] = mode_lib->vba.ViewportWidth[k];
		} else {
			mode_lib->vba.SwathWidthYSingleDPP[k] = mode_lib->vba.ViewportHeight[k];
		}
		if (mode_lib->vba.SourcePixelFormat[k] == dm_444_64) {
			mode_lib->vba.BytePerPixelInDETY[k] = 8.0;
			mode_lib->vba.BytePerPixelInDETC[k] = 0.0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_444_32) {
			mode_lib->vba.BytePerPixelInDETY[k] = 4.0;
			mode_lib->vba.BytePerPixelInDETC[k] = 0.0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_444_16
				|| mode_lib->vba.SourcePixelFormat[k] == dm_mono_16) {
			mode_lib->vba.BytePerPixelInDETY[k] = 2.0;
			mode_lib->vba.BytePerPixelInDETC[k] = 0.0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_mono_8) {
			mode_lib->vba.BytePerPixelInDETY[k] = 1.0;
			mode_lib->vba.BytePerPixelInDETC[k] = 0.0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_420_8) {
			mode_lib->vba.BytePerPixelInDETY[k] = 1.0;
			mode_lib->vba.BytePerPixelInDETC[k] = 2.0;
		} else {
			mode_lib->vba.BytePerPixelInDETY[k] = 4.0 / 3;
			mode_lib->vba.BytePerPixelInDETC[k] = 8.0 / 3;
		}
	}
	mode_lib->vba.TotalReadBandwidthConsumedGBytePerSecond = 0.0;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		mode_lib->vba.ReadBandwidth[k] = mode_lib->vba.SwathWidthYSingleDPP[k]
				* (dml_ceil(mode_lib->vba.BytePerPixelInDETY[k], 1.0)
						* mode_lib->vba.VRatio[k]
						+ dml_ceil(mode_lib->vba.BytePerPixelInDETC[k], 2.0)
								/ 2.0 * mode_lib->vba.VRatio[k] / 2)
				/ (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k]);
		if (mode_lib->vba.DCCEnable[k] == true) {
			mode_lib->vba.ReadBandwidth[k] = mode_lib->vba.ReadBandwidth[k]
					* (1 + 1 / 256);
		}
		if (mode_lib->vba.VirtualMemoryEnable == true
				&& mode_lib->vba.SourceScan[k] != dm_horz
				&& (mode_lib->vba.SurfaceTiling[k] == dm_sw_4kb_s
						|| mode_lib->vba.SurfaceTiling[k] == dm_sw_4kb_s_x
						|| mode_lib->vba.SurfaceTiling[k] == dm_sw_4kb_d
						|| mode_lib->vba.SurfaceTiling[k] == dm_sw_4kb_d_x)) {
			mode_lib->vba.ReadBandwidth[k] = mode_lib->vba.ReadBandwidth[k]
					* (1 + 1 / 64);
		} else if (mode_lib->vba.VirtualMemoryEnable == true
				&& mode_lib->vba.SourceScan[k] == dm_horz
				&& (mode_lib->vba.SourcePixelFormat[k] == dm_444_64
						|| mode_lib->vba.SourcePixelFormat[k] == dm_444_32)
				&& (mode_lib->vba.SurfaceTiling[k] == dm_sw_64kb_s
						|| mode_lib->vba.SurfaceTiling[k] == dm_sw_64kb_s_t
						|| mode_lib->vba.SurfaceTiling[k] == dm_sw_64kb_s_x
						|| mode_lib->vba.SurfaceTiling[k] == dm_sw_64kb_d
						|| mode_lib->vba.SurfaceTiling[k] == dm_sw_64kb_d_t
						|| mode_lib->vba.SurfaceTiling[k] == dm_sw_64kb_d_x
						|| mode_lib->vba.SurfaceTiling[k] == dm_sw_64kb_r_x)) {
			mode_lib->vba.ReadBandwidth[k] = mode_lib->vba.ReadBandwidth[k]
					* (1 + 1 / 256);
		} else if (mode_lib->vba.VirtualMemoryEnable == true) {
			mode_lib->vba.ReadBandwidth[k] = mode_lib->vba.ReadBandwidth[k]
					* (1 + 1 / 512);
		}
		mode_lib->vba.TotalReadBandwidthConsumedGBytePerSecond =
				mode_lib->vba.TotalReadBandwidthConsumedGBytePerSecond
						+ mode_lib->vba.ReadBandwidth[k] / 1000.0;
	}
	mode_lib->vba.TotalWriteBandwidthConsumedGBytePerSecond = 0.0;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.WritebackEnable[k] == true
				&& mode_lib->vba.WritebackPixelFormat[k] == dm_444_32) {
			mode_lib->vba.WriteBandwidth[k] = mode_lib->vba.WritebackDestinationWidth[k]
					* mode_lib->vba.WritebackDestinationHeight[k]
					/ (mode_lib->vba.WritebackSourceHeight[k]
							* mode_lib->vba.HTotal[k]
							/ mode_lib->vba.PixelClock[k]) * 4.0;
		} else if (mode_lib->vba.WritebackEnable[k] == true
				&& mode_lib->vba.WritebackPixelFormat[k] == dm_420_10) {
			mode_lib->vba.WriteBandwidth[k] = mode_lib->vba.WritebackDestinationWidth[k]
					* mode_lib->vba.WritebackDestinationHeight[k]
					/ (mode_lib->vba.WritebackSourceHeight[k]
							* mode_lib->vba.HTotal[k]
							/ mode_lib->vba.PixelClock[k]) * 3.0;
		} else if (mode_lib->vba.WritebackEnable[k] == true) {
			mode_lib->vba.WriteBandwidth[k] = mode_lib->vba.WritebackDestinationWidth[k]
					* mode_lib->vba.WritebackDestinationHeight[k]
					/ (mode_lib->vba.WritebackSourceHeight[k]
							* mode_lib->vba.HTotal[k]
							/ mode_lib->vba.PixelClock[k]) * 1.5;
		} else {
			mode_lib->vba.WriteBandwidth[k] = 0.0;
		}
		mode_lib->vba.TotalWriteBandwidthConsumedGBytePerSecond =
				mode_lib->vba.TotalWriteBandwidthConsumedGBytePerSecond
						+ mode_lib->vba.WriteBandwidth[k] / 1000.0;
	}
	mode_lib->vba.TotalBandwidthConsumedGBytePerSecond =
			mode_lib->vba.TotalReadBandwidthConsumedGBytePerSecond
					+ mode_lib->vba.TotalWriteBandwidthConsumedGBytePerSecond;
	mode_lib->vba.DCCEnabledInAnyPlane = false;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.DCCEnable[k] == true) {
			mode_lib->vba.DCCEnabledInAnyPlane = true;
		}
	}
	for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
		mode_lib->vba.FabricAndDRAMBandwidthPerState[i] = dml_min(
				mode_lib->vba.DRAMSpeedPerState[i] * mode_lib->vba.NumberOfChannels
						* mode_lib->vba.DRAMChannelWidth,
				mode_lib->vba.FabricClockPerState[i]
						* mode_lib->vba.FabricDatapathToDCNDataReturn)
				/ 1000;
		mode_lib->vba.ReturnBWToDCNPerState = dml_min(
				mode_lib->vba.ReturnBusWidth * mode_lib->vba.DCFCLKPerState[i],
				mode_lib->vba.FabricAndDRAMBandwidthPerState[i] * 1000.0)
				* mode_lib->vba.PercentOfIdealDRAMAndFabricBWReceivedAfterUrgLatency
				/ 100;
		mode_lib->vba.ReturnBWPerState[i] = mode_lib->vba.ReturnBWToDCNPerState;
		if (mode_lib->vba.DCCEnabledInAnyPlane == true
				&& mode_lib->vba.ReturnBWToDCNPerState
						> mode_lib->vba.DCFCLKPerState[i]
								* mode_lib->vba.ReturnBusWidth
								/ 4.0) {
			mode_lib->vba.ReturnBWPerState[i] =
					dml_min(
							mode_lib->vba.ReturnBWPerState[i],
							mode_lib->vba.ReturnBWToDCNPerState * 4.0
									* (1.0
											- mode_lib->vba.UrgentLatency
													/ ((mode_lib->vba.ROBBufferSizeInKByte
															- mode_lib->vba.PixelChunkSizeInKByte)
															* 1024.0
															/ (mode_lib->vba.ReturnBWToDCNPerState
																	- mode_lib->vba.DCFCLKPerState[i]
																			* mode_lib->vba.ReturnBusWidth
																			/ 4.0)
															+ mode_lib->vba.UrgentLatency)));
		}
		mode_lib->vba.CriticalPoint =
				2.0 * mode_lib->vba.ReturnBusWidth * mode_lib->vba.DCFCLKPerState[i]
						* mode_lib->vba.UrgentLatency
						/ (mode_lib->vba.ReturnBWToDCNPerState
								* mode_lib->vba.UrgentLatency
								+ (mode_lib->vba.ROBBufferSizeInKByte
										- mode_lib->vba.PixelChunkSizeInKByte)
										* 1024.0);
		if (mode_lib->vba.DCCEnabledInAnyPlane == true && mode_lib->vba.CriticalPoint > 1.0
				&& mode_lib->vba.CriticalPoint < 4.0) {
			mode_lib->vba.ReturnBWPerState[i] =
					dml_min(
							mode_lib->vba.ReturnBWPerState[i],
							dml_pow(
									4.0
											* mode_lib->vba.ReturnBWToDCNPerState
											* (mode_lib->vba.ROBBufferSizeInKByte
													- mode_lib->vba.PixelChunkSizeInKByte)
											* 1024.0
											* mode_lib->vba.ReturnBusWidth
											* mode_lib->vba.DCFCLKPerState[i]
											* mode_lib->vba.UrgentLatency
											/ (mode_lib->vba.ReturnBWToDCNPerState
													* mode_lib->vba.UrgentLatency
													+ (mode_lib->vba.ROBBufferSizeInKByte
															- mode_lib->vba.PixelChunkSizeInKByte)
															* 1024.0),
									2));
		}
		mode_lib->vba.ReturnBWToDCNPerState = dml_min(
				mode_lib->vba.ReturnBusWidth * mode_lib->vba.DCFCLKPerState[i],
				mode_lib->vba.FabricAndDRAMBandwidthPerState[i] * 1000.0);
		if (mode_lib->vba.DCCEnabledInAnyPlane == true
				&& mode_lib->vba.ReturnBWToDCNPerState
						> mode_lib->vba.DCFCLKPerState[i]
								* mode_lib->vba.ReturnBusWidth
								/ 4.0) {
			mode_lib->vba.ReturnBWPerState[i] =
					dml_min(
							mode_lib->vba.ReturnBWPerState[i],
							mode_lib->vba.ReturnBWToDCNPerState * 4.0
									* (1.0
											- mode_lib->vba.UrgentLatency
													/ ((mode_lib->vba.ROBBufferSizeInKByte
															- mode_lib->vba.PixelChunkSizeInKByte)
															* 1024.0
															/ (mode_lib->vba.ReturnBWToDCNPerState
																	- mode_lib->vba.DCFCLKPerState[i]
																			* mode_lib->vba.ReturnBusWidth
																			/ 4.0)
															+ mode_lib->vba.UrgentLatency)));
		}
		mode_lib->vba.CriticalPoint =
				2.0 * mode_lib->vba.ReturnBusWidth * mode_lib->vba.DCFCLKPerState[i]
						* mode_lib->vba.UrgentLatency
						/ (mode_lib->vba.ReturnBWToDCNPerState
								* mode_lib->vba.UrgentLatency
								+ (mode_lib->vba.ROBBufferSizeInKByte
										- mode_lib->vba.PixelChunkSizeInKByte)
										* 1024.0);
		if (mode_lib->vba.DCCEnabledInAnyPlane == true && mode_lib->vba.CriticalPoint > 1.0
				&& mode_lib->vba.CriticalPoint < 4.0) {
			mode_lib->vba.ReturnBWPerState[i] =
					dml_min(
							mode_lib->vba.ReturnBWPerState[i],
							dml_pow(
									4.0
											* mode_lib->vba.ReturnBWToDCNPerState
											* (mode_lib->vba.ROBBufferSizeInKByte
													- mode_lib->vba.PixelChunkSizeInKByte)
											* 1024.0
											* mode_lib->vba.ReturnBusWidth
											* mode_lib->vba.DCFCLKPerState[i]
											* mode_lib->vba.UrgentLatency
											/ (mode_lib->vba.ReturnBWToDCNPerState
													* mode_lib->vba.UrgentLatency
													+ (mode_lib->vba.ROBBufferSizeInKByte
															- mode_lib->vba.PixelChunkSizeInKByte)
															* 1024.0),
									2));
		}
	}
	for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
		if ((mode_lib->vba.TotalReadBandwidthConsumedGBytePerSecond * 1000.0
				<= mode_lib->vba.ReturnBWPerState[i])
				&& (mode_lib->vba.TotalBandwidthConsumedGBytePerSecond * 1000.0
						<= mode_lib->vba.FabricAndDRAMBandwidthPerState[i]
								* 1000.0
								* mode_lib->vba.PercentOfIdealDRAMAndFabricBWReceivedAfterUrgLatency
								/ 100.0)) {
			mode_lib->vba.BandwidthSupport[i] = true;
		} else {
			mode_lib->vba.BandwidthSupport[i] = false;
		}
	}
	/*Writeback Latency support check*/

	mode_lib->vba.WritebackLatencySupport = true;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.WritebackEnable[k] == true) {
			if (mode_lib->vba.WritebackPixelFormat[k] == dm_444_32) {
				if (mode_lib->vba.WriteBandwidth[k]
						> (mode_lib->vba.WritebackInterfaceLumaBufferSize
								+ mode_lib->vba.WritebackInterfaceChromaBufferSize)
								/ mode_lib->vba.WritebackLatency) {
					mode_lib->vba.WritebackLatencySupport = false;
				}
			} else {
				if (mode_lib->vba.WriteBandwidth[k]
						> 1.5
								* dml_min(
										mode_lib->vba.WritebackInterfaceLumaBufferSize,
										2.0
												* mode_lib->vba.WritebackInterfaceChromaBufferSize)
								/ mode_lib->vba.WritebackLatency) {
					mode_lib->vba.WritebackLatencySupport = false;
				}
			}
		}
	}
	/*Re-ordering Buffer Support Check*/

	for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
		mode_lib->vba.UrgentRoundTripAndOutOfOrderLatencyPerState[i] =
				(mode_lib->vba.RoundTripPingLatencyCycles + 32.0)
						/ mode_lib->vba.DCFCLKPerState[i]
						+ mode_lib->vba.UrgentOutOfOrderReturnPerChannel
								* mode_lib->vba.NumberOfChannels
								/ mode_lib->vba.ReturnBWPerState[i];
		if ((mode_lib->vba.ROBBufferSizeInKByte - mode_lib->vba.PixelChunkSizeInKByte)
				* 1024.0 / mode_lib->vba.ReturnBWPerState[i]
				> mode_lib->vba.UrgentRoundTripAndOutOfOrderLatencyPerState[i]) {
			mode_lib->vba.ROBSupport[i] = true;
		} else {
			mode_lib->vba.ROBSupport[i] = false;
		}
	}
	/*Writeback Mode Support Check*/

	mode_lib->vba.TotalNumberOfActiveWriteback = 0;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.WritebackEnable[k] == true) {
			mode_lib->vba.TotalNumberOfActiveWriteback =
					mode_lib->vba.TotalNumberOfActiveWriteback + 1;
		}
	}
	mode_lib->vba.WritebackModeSupport = true;
	if (mode_lib->vba.TotalNumberOfActiveWriteback > mode_lib->vba.MaxNumWriteback) {
		mode_lib->vba.WritebackModeSupport = false;
	}
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.WritebackEnable[k] == true
				&& mode_lib->vba.Writeback10bpc420Supported != true
				&& mode_lib->vba.WritebackPixelFormat[k] == dm_420_10) {
			mode_lib->vba.WritebackModeSupport = false;
		}
	}
	/*Writeback Scale Ratio and Taps Support Check*/

	mode_lib->vba.WritebackScaleRatioAndTapsSupport = true;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.WritebackEnable[k] == true) {
			if (mode_lib->vba.WritebackLumaAndChromaScalingSupported == false
					&& (mode_lib->vba.WritebackHRatio[k] != 1.0
							|| mode_lib->vba.WritebackVRatio[k] != 1.0)) {
				mode_lib->vba.WritebackScaleRatioAndTapsSupport = false;
			}
			if (mode_lib->vba.WritebackHRatio[k] > mode_lib->vba.WritebackMaxHSCLRatio
					|| mode_lib->vba.WritebackVRatio[k]
							> mode_lib->vba.WritebackMaxVSCLRatio
					|| mode_lib->vba.WritebackHRatio[k]
							< mode_lib->vba.WritebackMinHSCLRatio
					|| mode_lib->vba.WritebackVRatio[k]
							< mode_lib->vba.WritebackMinVSCLRatio
					|| mode_lib->vba.WritebackLumaHTaps[k]
							> mode_lib->vba.WritebackMaxHSCLTaps
					|| mode_lib->vba.WritebackLumaVTaps[k]
							> mode_lib->vba.WritebackMaxVSCLTaps
					|| mode_lib->vba.WritebackHRatio[k]
							> mode_lib->vba.WritebackLumaHTaps[k]
					|| mode_lib->vba.WritebackVRatio[k]
							> mode_lib->vba.WritebackLumaVTaps[k]
					|| (mode_lib->vba.WritebackLumaHTaps[k] > 2.0
							&& ((mode_lib->vba.WritebackLumaHTaps[k] % 2)
									== 1))
					|| (mode_lib->vba.WritebackPixelFormat[k] != dm_444_32
							&& (mode_lib->vba.WritebackChromaHTaps[k]
									> mode_lib->vba.WritebackMaxHSCLTaps
									|| mode_lib->vba.WritebackChromaVTaps[k]
											> mode_lib->vba.WritebackMaxVSCLTaps
									|| 2.0
											* mode_lib->vba.WritebackHRatio[k]
											> mode_lib->vba.WritebackChromaHTaps[k]
									|| 2.0
											* mode_lib->vba.WritebackVRatio[k]
											> mode_lib->vba.WritebackChromaVTaps[k]
									|| (mode_lib->vba.WritebackChromaHTaps[k] > 2.0
										&& ((mode_lib->vba.WritebackChromaHTaps[k] % 2) == 1))))) {
				mode_lib->vba.WritebackScaleRatioAndTapsSupport = false;
			}
			if (mode_lib->vba.WritebackVRatio[k] < 1.0) {
				mode_lib->vba.WritebackLumaVExtra =
						dml_max(1.0 - 2.0 / dml_ceil(1.0 / mode_lib->vba.WritebackVRatio[k], 1.0), 0.0);
			} else {
				mode_lib->vba.WritebackLumaVExtra = -1;
			}
			if ((mode_lib->vba.WritebackPixelFormat[k] == dm_444_32
					&& mode_lib->vba.WritebackLumaVTaps[k]
							> (mode_lib->vba.WritebackLineBufferLumaBufferSize
									+ mode_lib->vba.WritebackLineBufferChromaBufferSize)
									/ 3.0
									/ mode_lib->vba.WritebackDestinationWidth[k]
									- mode_lib->vba.WritebackLumaVExtra)
					|| (mode_lib->vba.WritebackPixelFormat[k] == dm_420_8
							&& mode_lib->vba.WritebackLumaVTaps[k]
									> mode_lib->vba.WritebackLineBufferLumaBufferSize
											/ mode_lib->vba.WritebackDestinationWidth[k]
											- mode_lib->vba.WritebackLumaVExtra)
					|| (mode_lib->vba.WritebackPixelFormat[k] == dm_420_10
							&& mode_lib->vba.WritebackLumaVTaps[k]
									> mode_lib->vba.WritebackLineBufferLumaBufferSize
											* 8.0 / 10.0
											/ mode_lib->vba.WritebackDestinationWidth[k]
											- mode_lib->vba.WritebackLumaVExtra)) {
				mode_lib->vba.WritebackScaleRatioAndTapsSupport = false;
			}
			if (2.0 * mode_lib->vba.WritebackVRatio[k] < 1) {
				mode_lib->vba.WritebackChromaVExtra = 0.0;
			} else {
				mode_lib->vba.WritebackChromaVExtra = -1;
			}
			if ((mode_lib->vba.WritebackPixelFormat[k] == dm_420_8
					&& mode_lib->vba.WritebackChromaVTaps[k]
							> mode_lib->vba.WritebackLineBufferChromaBufferSize
									/ mode_lib->vba.WritebackDestinationWidth[k]
									- mode_lib->vba.WritebackChromaVExtra)
					|| (mode_lib->vba.WritebackPixelFormat[k] == dm_420_10
							&& mode_lib->vba.WritebackChromaVTaps[k]
									> mode_lib->vba.WritebackLineBufferChromaBufferSize
											* 8.0 / 10.0
											/ mode_lib->vba.WritebackDestinationWidth[k]
											- mode_lib->vba.WritebackChromaVExtra)) {
				mode_lib->vba.WritebackScaleRatioAndTapsSupport = false;
			}
		}
	}
	/*Maximum DISPCLK/DPPCLK Support check*/

	mode_lib->vba.WritebackRequiredDISPCLK = 0.0;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.WritebackEnable[k] == true) {
			mode_lib->vba.WritebackRequiredDISPCLK =
					dml_max(
							mode_lib->vba.WritebackRequiredDISPCLK,
							CalculateWriteBackDISPCLK(
									mode_lib->vba.WritebackPixelFormat[k],
									mode_lib->vba.PixelClock[k],
									mode_lib->vba.WritebackHRatio[k],
									mode_lib->vba.WritebackVRatio[k],
									mode_lib->vba.WritebackLumaHTaps[k],
									mode_lib->vba.WritebackLumaVTaps[k],
									mode_lib->vba.WritebackChromaHTaps[k],
									mode_lib->vba.WritebackChromaVTaps[k],
									mode_lib->vba.WritebackDestinationWidth[k],
									mode_lib->vba.HTotal[k],
									mode_lib->vba.WritebackChromaLineBufferWidth));
		}
	}
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.HRatio[k] > 1.0) {
			mode_lib->vba.PSCL_FACTOR[k] = dml_min(
					mode_lib->vba.MaxDCHUBToPSCLThroughput,
					mode_lib->vba.MaxPSCLToLBThroughput
							* mode_lib->vba.HRatio[k]
							/ dml_ceil(
									mode_lib->vba.htaps[k]
											/ 6.0,
									1.0));
		} else {
			mode_lib->vba.PSCL_FACTOR[k] = dml_min(
					mode_lib->vba.MaxDCHUBToPSCLThroughput,
					mode_lib->vba.MaxPSCLToLBThroughput);
		}
		if (mode_lib->vba.BytePerPixelInDETC[k] == 0.0) {
			mode_lib->vba.PSCL_FACTOR_CHROMA[k] = 0.0;
			mode_lib->vba.MinDPPCLKUsingSingleDPP[k] =
					mode_lib->vba.PixelClock[k]
							* dml_max3(
									mode_lib->vba.vtaps[k] / 6.0
											* dml_min(
													1.0,
													mode_lib->vba.HRatio[k]),
									mode_lib->vba.HRatio[k]
											* mode_lib->vba.VRatio[k]
											/ mode_lib->vba.PSCL_FACTOR[k],
									1.0);
			if ((mode_lib->vba.htaps[k] > 6.0 || mode_lib->vba.vtaps[k] > 6.0)
					&& mode_lib->vba.MinDPPCLKUsingSingleDPP[k]
							< 2.0 * mode_lib->vba.PixelClock[k]) {
				mode_lib->vba.MinDPPCLKUsingSingleDPP[k] = 2.0
						* mode_lib->vba.PixelClock[k];
			}
		} else {
			if (mode_lib->vba.HRatio[k] / 2.0 > 1.0) {
				mode_lib->vba.PSCL_FACTOR_CHROMA[k] =
						dml_min(
								mode_lib->vba.MaxDCHUBToPSCLThroughput,
								mode_lib->vba.MaxPSCLToLBThroughput
										* mode_lib->vba.HRatio[k]
										/ 2.0
										/ dml_ceil(
												mode_lib->vba.HTAPsChroma[k]
														/ 6.0,
												1.0));
			} else {
				mode_lib->vba.PSCL_FACTOR_CHROMA[k] = dml_min(
						mode_lib->vba.MaxDCHUBToPSCLThroughput,
						mode_lib->vba.MaxPSCLToLBThroughput);
			}
			mode_lib->vba.MinDPPCLKUsingSingleDPP[k] =
					mode_lib->vba.PixelClock[k]
							* dml_max5(
									mode_lib->vba.vtaps[k] / 6.0
											* dml_min(
													1.0,
													mode_lib->vba.HRatio[k]),
									mode_lib->vba.HRatio[k]
											* mode_lib->vba.VRatio[k]
											/ mode_lib->vba.PSCL_FACTOR[k],
									mode_lib->vba.VTAPsChroma[k]
											/ 6.0
											* dml_min(
													1.0,
													mode_lib->vba.HRatio[k]
															/ 2.0),
									mode_lib->vba.HRatio[k]
											* mode_lib->vba.VRatio[k]
											/ 4.0
											/ mode_lib->vba.PSCL_FACTOR_CHROMA[k],
									1.0);
			if ((mode_lib->vba.htaps[k] > 6.0 || mode_lib->vba.vtaps[k] > 6.0
					|| mode_lib->vba.HTAPsChroma[k] > 6.0
					|| mode_lib->vba.VTAPsChroma[k] > 6.0)
					&& mode_lib->vba.MinDPPCLKUsingSingleDPP[k]
							< 2.0 * mode_lib->vba.PixelClock[k]) {
				mode_lib->vba.MinDPPCLKUsingSingleDPP[k] = 2.0
						* mode_lib->vba.PixelClock[k];
			}
		}
	}
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		Calculate256BBlockSizes(
				mode_lib->vba.SourcePixelFormat[k],
				mode_lib->vba.SurfaceTiling[k],
				dml_ceil(mode_lib->vba.BytePerPixelInDETY[k], 1.0),
				dml_ceil(mode_lib->vba.BytePerPixelInDETC[k], 2.0),
				&mode_lib->vba.Read256BlockHeightY[k],
				&mode_lib->vba.Read256BlockHeightC[k],
				&mode_lib->vba.Read256BlockWidthY[k],
				&mode_lib->vba.Read256BlockWidthC[k]);
		if (mode_lib->vba.SourceScan[k] == dm_horz) {
			mode_lib->vba.MaxSwathHeightY[k] = mode_lib->vba.Read256BlockHeightY[k];
			mode_lib->vba.MaxSwathHeightC[k] = mode_lib->vba.Read256BlockHeightC[k];
		} else {
			mode_lib->vba.MaxSwathHeightY[k] = mode_lib->vba.Read256BlockWidthY[k];
			mode_lib->vba.MaxSwathHeightC[k] = mode_lib->vba.Read256BlockWidthC[k];
		}
		if ((mode_lib->vba.SourcePixelFormat[k] == dm_444_64
				|| mode_lib->vba.SourcePixelFormat[k] == dm_444_32
				|| mode_lib->vba.SourcePixelFormat[k] == dm_444_16
				|| mode_lib->vba.SourcePixelFormat[k] == dm_mono_16
				|| mode_lib->vba.SourcePixelFormat[k] == dm_mono_8)) {
			if (mode_lib->vba.SurfaceTiling[k] == dm_sw_linear
					|| (mode_lib->vba.SourcePixelFormat[k] == dm_444_64
							&& (mode_lib->vba.SurfaceTiling[k]
									== dm_sw_4kb_s
									|| mode_lib->vba.SurfaceTiling[k]
											== dm_sw_4kb_s_x
									|| mode_lib->vba.SurfaceTiling[k]
											== dm_sw_64kb_s
									|| mode_lib->vba.SurfaceTiling[k]
											== dm_sw_64kb_s_t
									|| mode_lib->vba.SurfaceTiling[k]
											== dm_sw_64kb_s_x
									|| mode_lib->vba.SurfaceTiling[k]
											== dm_sw_var_s
									|| mode_lib->vba.SurfaceTiling[k]
											== dm_sw_var_s_x)
							&& mode_lib->vba.SourceScan[k] == dm_horz)) {
				mode_lib->vba.MinSwathHeightY[k] = mode_lib->vba.MaxSwathHeightY[k];
			} else {
				mode_lib->vba.MinSwathHeightY[k] = mode_lib->vba.MaxSwathHeightY[k]
						/ 2.0;
			}
			mode_lib->vba.MinSwathHeightC[k] = mode_lib->vba.MaxSwathHeightC[k];
		} else {
			if (mode_lib->vba.SurfaceTiling[k] == dm_sw_linear) {
				mode_lib->vba.MinSwathHeightY[k] = mode_lib->vba.MaxSwathHeightY[k];
				mode_lib->vba.MinSwathHeightC[k] = mode_lib->vba.MaxSwathHeightC[k];
			} else if (mode_lib->vba.SourcePixelFormat[k] == dm_420_8
					&& mode_lib->vba.SourceScan[k] == dm_horz) {
				mode_lib->vba.MinSwathHeightY[k] = mode_lib->vba.MaxSwathHeightY[k]
						/ 2.0;
				mode_lib->vba.MinSwathHeightC[k] = mode_lib->vba.MaxSwathHeightC[k];
			} else if (mode_lib->vba.SourcePixelFormat[k] == dm_420_10
					&& mode_lib->vba.SourceScan[k] == dm_horz) {
				mode_lib->vba.MinSwathHeightC[k] = mode_lib->vba.MaxSwathHeightC[k]
						/ 2.0;
				mode_lib->vba.MinSwathHeightY[k] = mode_lib->vba.MaxSwathHeightY[k];
			} else {
				mode_lib->vba.MinSwathHeightY[k] = mode_lib->vba.MaxSwathHeightY[k];
				mode_lib->vba.MinSwathHeightC[k] = mode_lib->vba.MaxSwathHeightC[k];
			}
		}
		if (mode_lib->vba.SurfaceTiling[k] == dm_sw_linear) {
			mode_lib->vba.MaximumSwathWidthSupport = 8192.0;
		} else {
			mode_lib->vba.MaximumSwathWidthSupport = 5120.0;
		}
		mode_lib->vba.MaximumSwathWidthInDETBuffer =
				dml_min(
						mode_lib->vba.MaximumSwathWidthSupport,
						mode_lib->vba.DETBufferSizeInKByte * 1024.0 / 2.0
								/ (mode_lib->vba.BytePerPixelInDETY[k]
										* mode_lib->vba.MinSwathHeightY[k]
										+ mode_lib->vba.BytePerPixelInDETC[k]
												/ 2.0
												* mode_lib->vba.MinSwathHeightC[k]));
		if (mode_lib->vba.BytePerPixelInDETC[k] == 0.0) {
			mode_lib->vba.MaximumSwathWidthInLineBuffer =
					mode_lib->vba.LineBufferSize
							* dml_max(mode_lib->vba.HRatio[k], 1.0)
							/ mode_lib->vba.LBBitPerPixel[k]
							/ (mode_lib->vba.vtaps[k]
									+ dml_max(
											dml_ceil(
													mode_lib->vba.VRatio[k],
													1.0)
													- 2,
											0.0));
		} else {
			mode_lib->vba.MaximumSwathWidthInLineBuffer =
					dml_min(
							mode_lib->vba.LineBufferSize
									* dml_max(
											mode_lib->vba.HRatio[k],
											1.0)
									/ mode_lib->vba.LBBitPerPixel[k]
									/ (mode_lib->vba.vtaps[k]
											+ dml_max(
													dml_ceil(
															mode_lib->vba.VRatio[k],
															1.0)
															- 2,
													0.0)),
							2.0 * mode_lib->vba.LineBufferSize
									* dml_max(
											mode_lib->vba.HRatio[k]
													/ 2.0,
											1.0)
									/ mode_lib->vba.LBBitPerPixel[k]
									/ (mode_lib->vba.VTAPsChroma[k]
											+ dml_max(
													dml_ceil(
															mode_lib->vba.VRatio[k]
																	/ 2.0,
															1.0)
															- 2,
													0.0)));
		}
		mode_lib->vba.MaximumSwathWidth[k] = dml_min(
				mode_lib->vba.MaximumSwathWidthInDETBuffer,
				mode_lib->vba.MaximumSwathWidthInLineBuffer);
	}
	for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
		mode_lib->vba.MaxDispclkRoundedDownToDFSGranularity = RoundToDFSGranularityDown(
				mode_lib->vba.MaxDispclk[i],
				mode_lib->vba.DISPCLKDPPCLKVCOSpeed);
		mode_lib->vba.MaxDppclkRoundedDownToDFSGranularity = RoundToDFSGranularityDown(
				mode_lib->vba.MaxDppclk[i],
				mode_lib->vba.DISPCLKDPPCLKVCOSpeed);
		mode_lib->vba.RequiredDISPCLK[i] = 0.0;
		mode_lib->vba.DISPCLK_DPPCLK_Support[i] = true;
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			mode_lib->vba.PlaneRequiredDISPCLKWithoutODMCombine =
					mode_lib->vba.PixelClock[k]
							* (1.0
									+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
											/ 100.0)
							* (1.0
									+ mode_lib->vba.DISPCLKRampingMargin
											/ 100.0);
			if (mode_lib->vba.ODMCapability == true
					&& mode_lib->vba.PlaneRequiredDISPCLKWithoutODMCombine
							> mode_lib->vba.MaxDispclkRoundedDownToDFSGranularity) {
				mode_lib->vba.ODMCombineEnablePerState[i][k] = true;
				mode_lib->vba.PlaneRequiredDISPCLK =
						mode_lib->vba.PlaneRequiredDISPCLKWithoutODMCombine
								/ 2.0;
			} else {
				mode_lib->vba.ODMCombineEnablePerState[i][k] = false;
				mode_lib->vba.PlaneRequiredDISPCLK =
						mode_lib->vba.PlaneRequiredDISPCLKWithoutODMCombine;
			}
			if (mode_lib->vba.MinDPPCLKUsingSingleDPP[k]
					* (1.0
							+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
									/ 100.0)
					<= mode_lib->vba.MaxDppclkRoundedDownToDFSGranularity
					&& mode_lib->vba.SwathWidthYSingleDPP[k]
							<= mode_lib->vba.MaximumSwathWidth[k]
					&& mode_lib->vba.ODMCombineEnablePerState[i][k] == false) {
				mode_lib->vba.NoOfDPP[i][k] = 1;
				mode_lib->vba.RequiredDPPCLK[i][k] =
						mode_lib->vba.MinDPPCLKUsingSingleDPP[k]
								* (1.0
										+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
												/ 100.0);
			} else {
				mode_lib->vba.NoOfDPP[i][k] = 2;
				mode_lib->vba.RequiredDPPCLK[i][k] =
						mode_lib->vba.MinDPPCLKUsingSingleDPP[k]
								* (1.0
										+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
												/ 100.0)
								/ 2.0;
			}
			mode_lib->vba.RequiredDISPCLK[i] = dml_max(
					mode_lib->vba.RequiredDISPCLK[i],
					mode_lib->vba.PlaneRequiredDISPCLK);
			if ((mode_lib->vba.MinDPPCLKUsingSingleDPP[k] / mode_lib->vba.NoOfDPP[i][k]
					* (1.0
							+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
									/ 100.0)
					> mode_lib->vba.MaxDppclkRoundedDownToDFSGranularity)
					|| (mode_lib->vba.PlaneRequiredDISPCLK
							> mode_lib->vba.MaxDispclkRoundedDownToDFSGranularity)) {
				mode_lib->vba.DISPCLK_DPPCLK_Support[i] = false;
			}
		}
		mode_lib->vba.TotalNumberOfActiveDPP[i] = 0.0;
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			mode_lib->vba.TotalNumberOfActiveDPP[i] =
					mode_lib->vba.TotalNumberOfActiveDPP[i]
							+ mode_lib->vba.NoOfDPP[i][k];
		}
		if ((mode_lib->vba.MaxDispclk[i] == mode_lib->vba.MaxDispclk[DC__VOLTAGE_STATES]
				&& mode_lib->vba.MaxDppclk[i]
						== mode_lib->vba.MaxDppclk[DC__VOLTAGE_STATES])
				&& (mode_lib->vba.TotalNumberOfActiveDPP[i]
						> mode_lib->vba.MaxNumDPP
						|| mode_lib->vba.DISPCLK_DPPCLK_Support[i] == false)) {
			mode_lib->vba.RequiredDISPCLK[i] = 0.0;
			mode_lib->vba.DISPCLK_DPPCLK_Support[i] = true;
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				mode_lib->vba.PlaneRequiredDISPCLKWithoutODMCombine =
						mode_lib->vba.PixelClock[k]
								* (1.0
										+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
												/ 100.0);
				if (mode_lib->vba.ODMCapability == true
						&& mode_lib->vba.PlaneRequiredDISPCLKWithoutODMCombine
								> mode_lib->vba.MaxDispclkRoundedDownToDFSGranularity) {
					mode_lib->vba.ODMCombineEnablePerState[i][k] = true;
					mode_lib->vba.PlaneRequiredDISPCLK =
							mode_lib->vba.PlaneRequiredDISPCLKWithoutODMCombine
									/ 2.0;
				} else {
					mode_lib->vba.ODMCombineEnablePerState[i][k] = false;
					mode_lib->vba.PlaneRequiredDISPCLK =
							mode_lib->vba.PlaneRequiredDISPCLKWithoutODMCombine;
				}
				if (mode_lib->vba.MinDPPCLKUsingSingleDPP[k]
						* (1.0
								+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
										/ 100.0)
						<= mode_lib->vba.MaxDppclkRoundedDownToDFSGranularity
						&& mode_lib->vba.SwathWidthYSingleDPP[k]
								<= mode_lib->vba.MaximumSwathWidth[k]
						&& mode_lib->vba.ODMCombineEnablePerState[i][k]
								== false) {
					mode_lib->vba.NoOfDPP[i][k] = 1;
					mode_lib->vba.RequiredDPPCLK[i][k] =
							mode_lib->vba.MinDPPCLKUsingSingleDPP[k]
									* (1.0
											+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
													/ 100.0);
				} else {
					mode_lib->vba.NoOfDPP[i][k] = 2;
					mode_lib->vba.RequiredDPPCLK[i][k] =
							mode_lib->vba.MinDPPCLKUsingSingleDPP[k]
									* (1.0
											+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
													/ 100.0)
									/ 2.0;
				}
				mode_lib->vba.RequiredDISPCLK[i] = dml_max(
						mode_lib->vba.RequiredDISPCLK[i],
						mode_lib->vba.PlaneRequiredDISPCLK);
				if ((mode_lib->vba.MinDPPCLKUsingSingleDPP[k]
						/ mode_lib->vba.NoOfDPP[i][k]
						* (1.0
								+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
										/ 100.0)
						> mode_lib->vba.MaxDppclkRoundedDownToDFSGranularity)
						|| (mode_lib->vba.PlaneRequiredDISPCLK
								> mode_lib->vba.MaxDispclkRoundedDownToDFSGranularity)) {
					mode_lib->vba.DISPCLK_DPPCLK_Support[i] = false;
				}
			}
			mode_lib->vba.TotalNumberOfActiveDPP[i] = 0.0;
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				mode_lib->vba.TotalNumberOfActiveDPP[i] =
						mode_lib->vba.TotalNumberOfActiveDPP[i]
								+ mode_lib->vba.NoOfDPP[i][k];
			}
		}
		if (mode_lib->vba.TotalNumberOfActiveDPP[i] > mode_lib->vba.MaxNumDPP) {
			mode_lib->vba.RequiredDISPCLK[i] = 0.0;
			mode_lib->vba.DISPCLK_DPPCLK_Support[i] = true;
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				mode_lib->vba.ODMCombineEnablePerState[i][k] = false;
				if (mode_lib->vba.SwathWidthYSingleDPP[k]
						<= mode_lib->vba.MaximumSwathWidth[k]) {
					mode_lib->vba.NoOfDPP[i][k] = 1;
					mode_lib->vba.RequiredDPPCLK[i][k] =
							mode_lib->vba.MinDPPCLKUsingSingleDPP[k]
									* (1.0
											+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
													/ 100.0);
				} else {
					mode_lib->vba.NoOfDPP[i][k] = 2;
					mode_lib->vba.RequiredDPPCLK[i][k] =
							mode_lib->vba.MinDPPCLKUsingSingleDPP[k]
									* (1.0
											+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
													/ 100.0)
									/ 2.0;
				}
				if (!(mode_lib->vba.MaxDispclk[i]
						== mode_lib->vba.MaxDispclk[DC__VOLTAGE_STATES]
						&& mode_lib->vba.MaxDppclk[i]
								== mode_lib->vba.MaxDppclk[DC__VOLTAGE_STATES])) {
					mode_lib->vba.PlaneRequiredDISPCLK =
							mode_lib->vba.PixelClock[k]
									* (1.0
											+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
													/ 100.0)
									* (1.0
											+ mode_lib->vba.DISPCLKRampingMargin
													/ 100.0);
				} else {
					mode_lib->vba.PlaneRequiredDISPCLK =
							mode_lib->vba.PixelClock[k]
									* (1.0
											+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
													/ 100.0);
				}
				mode_lib->vba.RequiredDISPCLK[i] = dml_max(
						mode_lib->vba.RequiredDISPCLK[i],
						mode_lib->vba.PlaneRequiredDISPCLK);
				if ((mode_lib->vba.MinDPPCLKUsingSingleDPP[k]
						/ mode_lib->vba.NoOfDPP[i][k]
						* (1.0
								+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
										/ 100.0)
						> mode_lib->vba.MaxDppclkRoundedDownToDFSGranularity)
						|| (mode_lib->vba.PlaneRequiredDISPCLK
								> mode_lib->vba.MaxDispclkRoundedDownToDFSGranularity)) {
					mode_lib->vba.DISPCLK_DPPCLK_Support[i] = false;
				}
			}
			mode_lib->vba.TotalNumberOfActiveDPP[i] = 0.0;
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				mode_lib->vba.TotalNumberOfActiveDPP[i] =
						mode_lib->vba.TotalNumberOfActiveDPP[i]
								+ mode_lib->vba.NoOfDPP[i][k];
			}
		}
		mode_lib->vba.RequiredDISPCLK[i] = dml_max(
				mode_lib->vba.RequiredDISPCLK[i],
				mode_lib->vba.WritebackRequiredDISPCLK);
		if (mode_lib->vba.MaxDispclkRoundedDownToDFSGranularity
				< mode_lib->vba.WritebackRequiredDISPCLK) {
			mode_lib->vba.DISPCLK_DPPCLK_Support[i] = false;
		}
	}
	/*Viewport Size Check*/

	for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
		mode_lib->vba.ViewportSizeSupport[i] = true;
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			if (mode_lib->vba.ODMCombineEnablePerState[i][k] == true) {
				if (dml_min(mode_lib->vba.SwathWidthYSingleDPP[k], dml_round(mode_lib->vba.HActive[k] / 2.0 * mode_lib->vba.HRatio[k]))
						> mode_lib->vba.MaximumSwathWidth[k]) {
					mode_lib->vba.ViewportSizeSupport[i] = false;
				}
			} else {
				if (mode_lib->vba.SwathWidthYSingleDPP[k] / 2.0
						> mode_lib->vba.MaximumSwathWidth[k]) {
					mode_lib->vba.ViewportSizeSupport[i] = false;
				}
			}
		}
	}
	/*Total Available Pipes Support Check*/

	for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
		if (mode_lib->vba.TotalNumberOfActiveDPP[i] <= mode_lib->vba.MaxNumDPP) {
			mode_lib->vba.TotalAvailablePipesSupport[i] = true;
		} else {
			mode_lib->vba.TotalAvailablePipesSupport[i] = false;
		}
	}
	/*Total Available OTG Support Check*/

	mode_lib->vba.TotalNumberOfActiveOTG = 0.0;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.BlendingAndTiming[k] == k) {
			mode_lib->vba.TotalNumberOfActiveOTG = mode_lib->vba.TotalNumberOfActiveOTG
					+ 1.0;
		}
	}
	if (mode_lib->vba.TotalNumberOfActiveOTG <= mode_lib->vba.MaxNumOTG) {
		mode_lib->vba.NumberOfOTGSupport = true;
	} else {
		mode_lib->vba.NumberOfOTGSupport = false;
	}
	/*Display IO and DSC Support Check*/

	mode_lib->vba.NonsupportedDSCInputBPC = false;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (!(mode_lib->vba.DSCInputBitPerComponent[k] == 12.0
				|| mode_lib->vba.DSCInputBitPerComponent[k] == 10.0
				|| mode_lib->vba.DSCInputBitPerComponent[k] == 8.0)) {
			mode_lib->vba.NonsupportedDSCInputBPC = true;
		}
	}
	for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			mode_lib->vba.RequiresDSC[i][k] = 0;
			mode_lib->vba.RequiresFEC[i][k] = 0;
			if (mode_lib->vba.BlendingAndTiming[k] == k) {
				if (mode_lib->vba.Output[k] == dm_hdmi) {
					mode_lib->vba.RequiresDSC[i][k] = 0;
					mode_lib->vba.RequiresFEC[i][k] = 0;
					mode_lib->vba.OutputBppPerState[i][k] =
							TruncToValidBPP(dml_min(600.0, mode_lib->vba.PHYCLKPerState[i])
										/ mode_lib->vba.PixelClockBackEnd[k] * 24,
									false,
									mode_lib->vba.Output[k],
									mode_lib->vba.OutputFormat[k],
									mode_lib->vba.DSCInputBitPerComponent[k]);
				} else if (mode_lib->vba.Output[k] == dm_dp
						|| mode_lib->vba.Output[k] == dm_edp) {
					if (mode_lib->vba.Output[k] == dm_edp) {
						mode_lib->vba.EffectiveFECOverhead = 0.0;
					} else {
						mode_lib->vba.EffectiveFECOverhead =
								mode_lib->vba.FECOverhead;
					}
					if (mode_lib->vba.PHYCLKPerState[i] >= 270.0) {
						mode_lib->vba.Outbpp =
								TruncToValidBPP((1.0 - mode_lib->vba.Downspreading / 100.0) * 270.0
											* mode_lib->vba.OutputLinkDPLanes[k] / mode_lib->vba.PixelClockBackEnd[k] * 8.0,
										false,
										mode_lib->vba.Output[k],
										mode_lib->vba.OutputFormat[k],
										mode_lib->vba.DSCInputBitPerComponent[k]);
						mode_lib->vba.OutbppDSC =
								TruncToValidBPP((1.0 - mode_lib->vba.Downspreading / 100.0)
											* (1.0 - mode_lib->vba.EffectiveFECOverhead / 100.0) * 270.0
											* mode_lib->vba.OutputLinkDPLanes[k] / mode_lib->vba.PixelClockBackEnd[k] * 8.0,
										true,
										mode_lib->vba.Output[k],
										mode_lib->vba.OutputFormat[k],
										mode_lib->vba.DSCInputBitPerComponent[k]);
						if (mode_lib->vba.DSCEnabled[k] == true) {
							mode_lib->vba.RequiresDSC[i][k] = true;
							if (mode_lib->vba.Output[k] == dm_dp) {
								mode_lib->vba.RequiresFEC[i][k] =
										true;
							} else {
								mode_lib->vba.RequiresFEC[i][k] =
										false;
							}
							mode_lib->vba.Outbpp =
									mode_lib->vba.OutbppDSC;
						} else {
							mode_lib->vba.RequiresDSC[i][k] = false;
							mode_lib->vba.RequiresFEC[i][k] = false;
						}
						mode_lib->vba.OutputBppPerState[i][k] =
								mode_lib->vba.Outbpp;
					}
					if (mode_lib->vba.Outbpp == BPP_INVALID) {
						mode_lib->vba.Outbpp =
								TruncToValidBPP((1.0 - mode_lib->vba.Downspreading / 100.0) * 540.0
											* mode_lib->vba.OutputLinkDPLanes[k] / mode_lib->vba.PixelClockBackEnd[k] * 8.0,
										false,
										mode_lib->vba.Output[k],
										mode_lib->vba.OutputFormat[k],
										mode_lib->vba.DSCInputBitPerComponent[k]);
						mode_lib->vba.OutbppDSC =
								TruncToValidBPP((1.0 - mode_lib->vba.Downspreading / 100.0)
											* (1.0 - mode_lib->vba.EffectiveFECOverhead / 100.0) * 540.0
											* mode_lib->vba.OutputLinkDPLanes[k] / mode_lib->vba.PixelClockBackEnd[k] * 8.0,
										true,
										mode_lib->vba.Output[k],
										mode_lib->vba.OutputFormat[k],
										mode_lib->vba.DSCInputBitPerComponent[k]);
						if (mode_lib->vba.DSCEnabled[k] == true) {
							mode_lib->vba.RequiresDSC[i][k] = true;
							if (mode_lib->vba.Output[k] == dm_dp) {
								mode_lib->vba.RequiresFEC[i][k] =
										true;
							} else {
								mode_lib->vba.RequiresFEC[i][k] =
										false;
							}
							mode_lib->vba.Outbpp =
									mode_lib->vba.OutbppDSC;
						} else {
							mode_lib->vba.RequiresDSC[i][k] = false;
							mode_lib->vba.RequiresFEC[i][k] = false;
						}
						mode_lib->vba.OutputBppPerState[i][k] =
								mode_lib->vba.Outbpp;
					}
					if (mode_lib->vba.Outbpp == BPP_INVALID
							&& mode_lib->vba.PHYCLKPerState[i]
									>= 810.0) {
						mode_lib->vba.Outbpp =
								TruncToValidBPP((1.0 - mode_lib->vba.Downspreading / 100.0) * 810.0
											* mode_lib->vba.OutputLinkDPLanes[k] / mode_lib->vba.PixelClockBackEnd[k] * 8.0,
										false,
										mode_lib->vba.Output[k],
										mode_lib->vba.OutputFormat[k],
										mode_lib->vba.DSCInputBitPerComponent[k]);
						mode_lib->vba.OutbppDSC =
								TruncToValidBPP((1.0 - mode_lib->vba.Downspreading / 100.0)
											* (1.0 - mode_lib->vba.EffectiveFECOverhead / 100.0) * 810.0
											* mode_lib->vba.OutputLinkDPLanes[k] / mode_lib->vba.PixelClockBackEnd[k] * 8.0,
										true,
										mode_lib->vba.Output[k],
										mode_lib->vba.OutputFormat[k],
										mode_lib->vba.DSCInputBitPerComponent[k]);
						if (mode_lib->vba.DSCEnabled[k] == true
								|| mode_lib->vba.Outbpp == BPP_INVALID) {
							mode_lib->vba.RequiresDSC[i][k] = true;
							if (mode_lib->vba.Output[k] == dm_dp) {
								mode_lib->vba.RequiresFEC[i][k] =
										true;
							} else {
								mode_lib->vba.RequiresFEC[i][k] =
										false;
							}
							mode_lib->vba.Outbpp =
									mode_lib->vba.OutbppDSC;
						} else {
							mode_lib->vba.RequiresDSC[i][k] = false;
							mode_lib->vba.RequiresFEC[i][k] = false;
						}
						mode_lib->vba.OutputBppPerState[i][k] =
								mode_lib->vba.Outbpp;
					}
				}
			} else {
				mode_lib->vba.OutputBppPerState[i][k] = BPP_BLENDED_PIPE;
			}
		}
	}
	for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
		mode_lib->vba.DIOSupport[i] = true;
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			if (mode_lib->vba.OutputBppPerState[i][k] == BPP_INVALID
					|| (mode_lib->vba.OutputFormat[k] == dm_420
							&& mode_lib->vba.ProgressiveToInterlaceUnitInOPP
									== true)) {
				mode_lib->vba.DIOSupport[i] = false;
			}
		}
	}
	for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			mode_lib->vba.DSCCLKRequiredMoreThanSupported[i] = false;
			if (mode_lib->vba.BlendingAndTiming[k] == k) {
				if ((mode_lib->vba.Output[k] == dm_dp
						|| mode_lib->vba.Output[k] == dm_edp)) {
					if (mode_lib->vba.OutputFormat[k] == dm_420
							|| mode_lib->vba.OutputFormat[k]
									== dm_n422) {
						mode_lib->vba.DSCFormatFactor = 2;
					} else {
						mode_lib->vba.DSCFormatFactor = 1;
					}
					if (mode_lib->vba.RequiresDSC[i][k] == true) {
						if (mode_lib->vba.ODMCombineEnablePerState[i][k]
								== true) {
							if (mode_lib->vba.PixelClockBackEnd[k] / 6.0
									/ mode_lib->vba.DSCFormatFactor
									> (1.0
											- mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
													/ 100.0)
											* mode_lib->vba.MaxDSCCLK[i]) {
								mode_lib->vba.DSCCLKRequiredMoreThanSupported[i] =
										true;
							}
						} else {
							if (mode_lib->vba.PixelClockBackEnd[k] / 3.0
									/ mode_lib->vba.DSCFormatFactor
									> (1.0
											- mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
													/ 100.0)
											* mode_lib->vba.MaxDSCCLK[i]) {
								mode_lib->vba.DSCCLKRequiredMoreThanSupported[i] =
										true;
							}
						}
					}
				}
			}
		}
	}
	for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
		mode_lib->vba.NotEnoughDSCUnits[i] = false;
		mode_lib->vba.TotalDSCUnitsRequired = 0.0;
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			if (mode_lib->vba.RequiresDSC[i][k] == true) {
				if (mode_lib->vba.ODMCombineEnablePerState[i][k] == true) {
					mode_lib->vba.TotalDSCUnitsRequired =
							mode_lib->vba.TotalDSCUnitsRequired + 2.0;
				} else {
					mode_lib->vba.TotalDSCUnitsRequired =
							mode_lib->vba.TotalDSCUnitsRequired + 1.0;
				}
			}
		}
		if (mode_lib->vba.TotalDSCUnitsRequired > mode_lib->vba.NumberOfDSC) {
			mode_lib->vba.NotEnoughDSCUnits[i] = true;
		}
	}
	/*DSC Delay per state*/

	for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			if (mode_lib->vba.BlendingAndTiming[k] != k) {
				mode_lib->vba.slices = 0;
			} else if (mode_lib->vba.RequiresDSC[i][k] == 0
					|| mode_lib->vba.RequiresDSC[i][k] == false) {
				mode_lib->vba.slices = 0;
			} else if (mode_lib->vba.PixelClockBackEnd[k] > 3200.0) {
				mode_lib->vba.slices = dml_ceil(
						mode_lib->vba.PixelClockBackEnd[k] / 400.0,
						4.0);
			} else if (mode_lib->vba.PixelClockBackEnd[k] > 1360.0) {
				mode_lib->vba.slices = 8.0;
			} else if (mode_lib->vba.PixelClockBackEnd[k] > 680.0) {
				mode_lib->vba.slices = 4.0;
			} else if (mode_lib->vba.PixelClockBackEnd[k] > 340.0) {
				mode_lib->vba.slices = 2.0;
			} else {
				mode_lib->vba.slices = 1.0;
			}
			if (mode_lib->vba.OutputBppPerState[i][k] == BPP_BLENDED_PIPE
					|| mode_lib->vba.OutputBppPerState[i][k] == BPP_INVALID) {
				mode_lib->vba.bpp = 0.0;
			} else {
				mode_lib->vba.bpp = mode_lib->vba.OutputBppPerState[i][k];
			}
			if (mode_lib->vba.RequiresDSC[i][k] == true && mode_lib->vba.bpp != 0.0) {
				if (mode_lib->vba.ODMCombineEnablePerState[i][k] == false) {
					mode_lib->vba.DSCDelayPerState[i][k] =
							dscceComputeDelay(
									mode_lib->vba.DSCInputBitPerComponent[k],
									mode_lib->vba.bpp,
									dml_ceil(
											mode_lib->vba.HActive[k]
													/ mode_lib->vba.slices,
											1.0),
									mode_lib->vba.slices,
									mode_lib->vba.OutputFormat[k])
									+ dscComputeDelay(
											mode_lib->vba.OutputFormat[k]);
				} else {
					mode_lib->vba.DSCDelayPerState[i][k] =
							2.0
									* (dscceComputeDelay(
											mode_lib->vba.DSCInputBitPerComponent[k],
											mode_lib->vba.bpp,
											dml_ceil(
													mode_lib->vba.HActive[k]
															/ mode_lib->vba.slices,
													1.0),
											mode_lib->vba.slices
													/ 2,
											mode_lib->vba.OutputFormat[k])
											+ dscComputeDelay(
													mode_lib->vba.OutputFormat[k]));
				}
				mode_lib->vba.DSCDelayPerState[i][k] =
						mode_lib->vba.DSCDelayPerState[i][k]
								* mode_lib->vba.PixelClock[k]
								/ mode_lib->vba.PixelClockBackEnd[k];
			} else {
				mode_lib->vba.DSCDelayPerState[i][k] = 0.0;
			}
		}
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			for (j = 0; j <= mode_lib->vba.NumberOfActivePlanes - 1; j++) {
				if (mode_lib->vba.BlendingAndTiming[k] == j
						&& mode_lib->vba.RequiresDSC[i][j] == true) {
					mode_lib->vba.DSCDelayPerState[i][k] =
							mode_lib->vba.DSCDelayPerState[i][j];
				}
			}
		}
	}
	/*Urgent Latency Support Check*/

	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
			if (mode_lib->vba.ODMCombineEnablePerState[i][k] == true) {
				mode_lib->vba.SwathWidthYPerState[i][k] =
						dml_min(
								mode_lib->vba.SwathWidthYSingleDPP[k],
								dml_round(
										mode_lib->vba.HActive[k]
												/ 2.0
												* mode_lib->vba.HRatio[k]));
			} else {
				mode_lib->vba.SwathWidthYPerState[i][k] =
						mode_lib->vba.SwathWidthYSingleDPP[k]
								/ mode_lib->vba.NoOfDPP[i][k];
			}
			mode_lib->vba.SwathWidthGranularityY = 256.0
					/ dml_ceil(mode_lib->vba.BytePerPixelInDETY[k], 1.0)
					/ mode_lib->vba.MaxSwathHeightY[k];
			mode_lib->vba.RoundedUpMaxSwathSizeBytesY = (dml_ceil(
					mode_lib->vba.SwathWidthYPerState[i][k] - 1.0,
					mode_lib->vba.SwathWidthGranularityY)
					+ mode_lib->vba.SwathWidthGranularityY)
					* mode_lib->vba.BytePerPixelInDETY[k]
					* mode_lib->vba.MaxSwathHeightY[k];
			if (mode_lib->vba.SourcePixelFormat[k] == dm_420_10) {
				mode_lib->vba.RoundedUpMaxSwathSizeBytesY = dml_ceil(
						mode_lib->vba.RoundedUpMaxSwathSizeBytesY,
						256.0) + 256;
			}
			if (mode_lib->vba.MaxSwathHeightC[k] > 0.0) {
				mode_lib->vba.SwathWidthGranularityC = 256.0
						/ dml_ceil(mode_lib->vba.BytePerPixelInDETC[k], 2.0)
						/ mode_lib->vba.MaxSwathHeightC[k];
				mode_lib->vba.RoundedUpMaxSwathSizeBytesC = (dml_ceil(
						mode_lib->vba.SwathWidthYPerState[i][k] / 2.0 - 1.0,
						mode_lib->vba.SwathWidthGranularityC)
						+ mode_lib->vba.SwathWidthGranularityC)
						* mode_lib->vba.BytePerPixelInDETC[k]
						* mode_lib->vba.MaxSwathHeightC[k];
				if (mode_lib->vba.SourcePixelFormat[k] == dm_420_10) {
					mode_lib->vba.RoundedUpMaxSwathSizeBytesC = dml_ceil(
							mode_lib->vba.RoundedUpMaxSwathSizeBytesC,
							256.0) + 256;
				}
			} else {
				mode_lib->vba.RoundedUpMaxSwathSizeBytesC = 0.0;
			}
			if (mode_lib->vba.RoundedUpMaxSwathSizeBytesY
					+ mode_lib->vba.RoundedUpMaxSwathSizeBytesC
					<= mode_lib->vba.DETBufferSizeInKByte * 1024.0 / 2.0) {
				mode_lib->vba.SwathHeightYPerState[i][k] =
						mode_lib->vba.MaxSwathHeightY[k];
				mode_lib->vba.SwathHeightCPerState[i][k] =
						mode_lib->vba.MaxSwathHeightC[k];
			} else {
				mode_lib->vba.SwathHeightYPerState[i][k] =
						mode_lib->vba.MinSwathHeightY[k];
				mode_lib->vba.SwathHeightCPerState[i][k] =
						mode_lib->vba.MinSwathHeightC[k];
			}
			if (mode_lib->vba.BytePerPixelInDETC[k] == 0.0) {
				mode_lib->vba.LinesInDETLuma = mode_lib->vba.DETBufferSizeInKByte
						* 1024.0 / mode_lib->vba.BytePerPixelInDETY[k]
						/ mode_lib->vba.SwathWidthYPerState[i][k];
				mode_lib->vba.LinesInDETChroma = 0.0;
			} else if (mode_lib->vba.SwathHeightYPerState[i][k]
					<= mode_lib->vba.SwathHeightCPerState[i][k]) {
				mode_lib->vba.LinesInDETLuma = mode_lib->vba.DETBufferSizeInKByte
						* 1024.0 / 2.0 / mode_lib->vba.BytePerPixelInDETY[k]
						/ mode_lib->vba.SwathWidthYPerState[i][k];
				mode_lib->vba.LinesInDETChroma = mode_lib->vba.DETBufferSizeInKByte
						* 1024.0 / 2.0 / mode_lib->vba.BytePerPixelInDETC[k]
						/ (mode_lib->vba.SwathWidthYPerState[i][k] / 2.0);
			} else {
				mode_lib->vba.LinesInDETLuma = mode_lib->vba.DETBufferSizeInKByte
						* 1024.0 * 2.0 / 3.0
						/ mode_lib->vba.BytePerPixelInDETY[k]
						/ mode_lib->vba.SwathWidthYPerState[i][k];
				mode_lib->vba.LinesInDETChroma = mode_lib->vba.DETBufferSizeInKByte
						* 1024.0 / 3.0 / mode_lib->vba.BytePerPixelInDETY[k]
						/ (mode_lib->vba.SwathWidthYPerState[i][k] / 2.0);
			}
			mode_lib->vba.EffectiveLBLatencyHidingSourceLinesLuma =
					dml_min(
							mode_lib->vba.MaxLineBufferLines,
							dml_floor(
									mode_lib->vba.LineBufferSize
											/ mode_lib->vba.LBBitPerPixel[k]
											/ (mode_lib->vba.SwathWidthYPerState[i][k]
													/ dml_max(
															mode_lib->vba.HRatio[k],
															1.0)),
									1.0))
							- (mode_lib->vba.vtaps[k] - 1.0);
			mode_lib->vba.EffectiveLBLatencyHidingSourceLinesChroma =
					dml_min(
							mode_lib->vba.MaxLineBufferLines,
							dml_floor(
									mode_lib->vba.LineBufferSize
											/ mode_lib->vba.LBBitPerPixel[k]
											/ (mode_lib->vba.SwathWidthYPerState[i][k]
													/ 2.0
													/ dml_max(
															mode_lib->vba.HRatio[k]
																	/ 2.0,
															1.0)),
									1.0))
							- (mode_lib->vba.VTAPsChroma[k] - 1.0);
			mode_lib->vba.EffectiveDETLBLinesLuma =
					dml_floor(
							mode_lib->vba.LinesInDETLuma
									+ dml_min(
											mode_lib->vba.LinesInDETLuma
													* mode_lib->vba.RequiredDISPCLK[i]
													* mode_lib->vba.BytePerPixelInDETY[k]
													* mode_lib->vba.PSCL_FACTOR[k]
													/ mode_lib->vba.ReturnBWPerState[i],
											mode_lib->vba.EffectiveLBLatencyHidingSourceLinesLuma),
							mode_lib->vba.SwathHeightYPerState[i][k]);
			mode_lib->vba.EffectiveDETLBLinesChroma =
					dml_floor(
							mode_lib->vba.LinesInDETChroma
									+ dml_min(
											mode_lib->vba.LinesInDETChroma
													* mode_lib->vba.RequiredDISPCLK[i]
													* mode_lib->vba.BytePerPixelInDETC[k]
													* mode_lib->vba.PSCL_FACTOR_CHROMA[k]
													/ mode_lib->vba.ReturnBWPerState[i],
											mode_lib->vba.EffectiveLBLatencyHidingSourceLinesChroma),
							mode_lib->vba.SwathHeightCPerState[i][k]);
			if (mode_lib->vba.BytePerPixelInDETC[k] == 0.0) {
				mode_lib->vba.UrgentLatencySupportUsPerState[i][k] =
						mode_lib->vba.EffectiveDETLBLinesLuma
								* (mode_lib->vba.HTotal[k]
										/ mode_lib->vba.PixelClock[k])
								/ mode_lib->vba.VRatio[k]
								- mode_lib->vba.EffectiveDETLBLinesLuma
										* mode_lib->vba.SwathWidthYPerState[i][k]
										* dml_ceil(
												mode_lib->vba.BytePerPixelInDETY[k],
												1.0)
										/ (mode_lib->vba.ReturnBWPerState[i]
												/ mode_lib->vba.NoOfDPP[i][k]);
			} else {
				mode_lib->vba.UrgentLatencySupportUsPerState[i][k] =
						dml_min(
								mode_lib->vba.EffectiveDETLBLinesLuma
										* (mode_lib->vba.HTotal[k]
												/ mode_lib->vba.PixelClock[k])
										/ mode_lib->vba.VRatio[k]
										- mode_lib->vba.EffectiveDETLBLinesLuma
												* mode_lib->vba.SwathWidthYPerState[i][k]
												* dml_ceil(
														mode_lib->vba.BytePerPixelInDETY[k],
														1.0)
												/ (mode_lib->vba.ReturnBWPerState[i]
														/ mode_lib->vba.NoOfDPP[i][k]),
								mode_lib->vba.EffectiveDETLBLinesChroma
										* (mode_lib->vba.HTotal[k]
												/ mode_lib->vba.PixelClock[k])
										/ (mode_lib->vba.VRatio[k]
												/ 2.0)
										- mode_lib->vba.EffectiveDETLBLinesChroma
												* mode_lib->vba.SwathWidthYPerState[i][k]
												/ 2.0
												* dml_ceil(
														mode_lib->vba.BytePerPixelInDETC[k],
														2.0)
												/ (mode_lib->vba.ReturnBWPerState[i]
														/ mode_lib->vba.NoOfDPP[i][k]));
			}
		}
	}
	for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
		mode_lib->vba.UrgentLatencySupport[i] = true;
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			if (mode_lib->vba.UrgentLatencySupportUsPerState[i][k]
					< mode_lib->vba.UrgentLatency / 1.0) {
				mode_lib->vba.UrgentLatencySupport[i] = false;
			}
		}
	}
	/*Prefetch Check*/

	for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
		mode_lib->vba.TotalNumberOfDCCActiveDPP[i] = 0.0;
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			if (mode_lib->vba.DCCEnable[k] == true) {
				mode_lib->vba.TotalNumberOfDCCActiveDPP[i] =
						mode_lib->vba.TotalNumberOfDCCActiveDPP[i]
								+ mode_lib->vba.NoOfDPP[i][k];
			}
		}
	}
	for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
		mode_lib->vba.ProjectedDCFCLKDeepSleep = 8.0;
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			mode_lib->vba.ProjectedDCFCLKDeepSleep = dml_max(
					mode_lib->vba.ProjectedDCFCLKDeepSleep,
					mode_lib->vba.PixelClock[k] / 16.0);
			if (mode_lib->vba.BytePerPixelInDETC[k] == 0.0) {
				if (mode_lib->vba.VRatio[k] <= 1.0) {
					mode_lib->vba.ProjectedDCFCLKDeepSleep =
							dml_max(
									mode_lib->vba.ProjectedDCFCLKDeepSleep,
									1.1
											* dml_ceil(
													mode_lib->vba.BytePerPixelInDETY[k],
													1.0)
											/ 64.0
											* mode_lib->vba.HRatio[k]
											* mode_lib->vba.PixelClock[k]
											/ mode_lib->vba.NoOfDPP[i][k]);
				} else {
					mode_lib->vba.ProjectedDCFCLKDeepSleep =
							dml_max(
									mode_lib->vba.ProjectedDCFCLKDeepSleep,
									1.1
											* dml_ceil(
													mode_lib->vba.BytePerPixelInDETY[k],
													1.0)
											/ 64.0
											* mode_lib->vba.PSCL_FACTOR[k]
											* mode_lib->vba.RequiredDPPCLK[i][k]);
				}
			} else {
				if (mode_lib->vba.VRatio[k] <= 1.0) {
					mode_lib->vba.ProjectedDCFCLKDeepSleep =
							dml_max(
									mode_lib->vba.ProjectedDCFCLKDeepSleep,
									1.1
											* dml_ceil(
													mode_lib->vba.BytePerPixelInDETY[k],
													1.0)
											/ 32.0
											* mode_lib->vba.HRatio[k]
											* mode_lib->vba.PixelClock[k]
											/ mode_lib->vba.NoOfDPP[i][k]);
				} else {
					mode_lib->vba.ProjectedDCFCLKDeepSleep =
							dml_max(
									mode_lib->vba.ProjectedDCFCLKDeepSleep,
									1.1
											* dml_ceil(
													mode_lib->vba.BytePerPixelInDETY[k],
													1.0)
											/ 32.0
											* mode_lib->vba.PSCL_FACTOR[k]
											* mode_lib->vba.RequiredDPPCLK[i][k]);
				}
				if (mode_lib->vba.VRatio[k] / 2.0 <= 1.0) {
					mode_lib->vba.ProjectedDCFCLKDeepSleep =
							dml_max(
									mode_lib->vba.ProjectedDCFCLKDeepSleep,
									1.1
											* dml_ceil(
													mode_lib->vba.BytePerPixelInDETC[k],
													2.0)
											/ 32.0
											* mode_lib->vba.HRatio[k]
											/ 2.0
											* mode_lib->vba.PixelClock[k]
											/ mode_lib->vba.NoOfDPP[i][k]);
				} else {
					mode_lib->vba.ProjectedDCFCLKDeepSleep =
							dml_max(
									mode_lib->vba.ProjectedDCFCLKDeepSleep,
									1.1
											* dml_ceil(
													mode_lib->vba.BytePerPixelInDETC[k],
													2.0)
											/ 32.0
											* mode_lib->vba.PSCL_FACTOR_CHROMA[k]
											* mode_lib->vba.RequiredDPPCLK[i][k]);
				}
			}
		}
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			mode_lib->vba.PDEAndMetaPTEBytesPerFrameY = CalculateVMAndRowBytes(
					mode_lib,
					mode_lib->vba.DCCEnable[k],
					mode_lib->vba.Read256BlockHeightY[k],
					mode_lib->vba.Read256BlockWidthY[k],
					mode_lib->vba.SourcePixelFormat[k],
					mode_lib->vba.SurfaceTiling[k],
					dml_ceil(mode_lib->vba.BytePerPixelInDETY[k], 1.0),
					mode_lib->vba.SourceScan[k],
					mode_lib->vba.ViewportWidth[k],
					mode_lib->vba.ViewportHeight[k],
					mode_lib->vba.SwathWidthYPerState[i][k],
					mode_lib->vba.VirtualMemoryEnable,
					mode_lib->vba.VMMPageSize,
					mode_lib->vba.PTEBufferSizeInRequests,
					mode_lib->vba.PDEProcessingBufIn64KBReqs,
					mode_lib->vba.PitchY[k],
					mode_lib->vba.DCCMetaPitchY[k],
					&mode_lib->vba.MacroTileWidthY[k],
					&mode_lib->vba.MetaRowBytesY,
					&mode_lib->vba.DPTEBytesPerRowY,
					&mode_lib->vba.PTEBufferSizeNotExceededY[i][k],
					&mode_lib->vba.dpte_row_height[k],
					&mode_lib->vba.meta_row_height[k]);
			mode_lib->vba.PrefetchLinesY[k] = CalculatePrefetchSourceLines(
					mode_lib,
					mode_lib->vba.VRatio[k],
					mode_lib->vba.vtaps[k],
					mode_lib->vba.Interlace[k],
					mode_lib->vba.ProgressiveToInterlaceUnitInOPP,
					mode_lib->vba.SwathHeightYPerState[i][k],
					mode_lib->vba.ViewportYStartY[k],
					&mode_lib->vba.PrefillY[k],
					&mode_lib->vba.MaxNumSwY[k]);
			if ((mode_lib->vba.SourcePixelFormat[k] != dm_444_64
					&& mode_lib->vba.SourcePixelFormat[k] != dm_444_32
					&& mode_lib->vba.SourcePixelFormat[k] != dm_444_16
					&& mode_lib->vba.SourcePixelFormat[k] != dm_mono_16
					&& mode_lib->vba.SourcePixelFormat[k] != dm_mono_8)) {
				mode_lib->vba.PDEAndMetaPTEBytesPerFrameC = CalculateVMAndRowBytes(
						mode_lib,
						mode_lib->vba.DCCEnable[k],
						mode_lib->vba.Read256BlockHeightY[k],
						mode_lib->vba.Read256BlockWidthY[k],
						mode_lib->vba.SourcePixelFormat[k],
						mode_lib->vba.SurfaceTiling[k],
						dml_ceil(mode_lib->vba.BytePerPixelInDETC[k], 2.0),
						mode_lib->vba.SourceScan[k],
						mode_lib->vba.ViewportWidth[k] / 2.0,
						mode_lib->vba.ViewportHeight[k] / 2.0,
						mode_lib->vba.SwathWidthYPerState[i][k] / 2.0,
						mode_lib->vba.VirtualMemoryEnable,
						mode_lib->vba.VMMPageSize,
						mode_lib->vba.PTEBufferSizeInRequests,
						mode_lib->vba.PDEProcessingBufIn64KBReqs,
						mode_lib->vba.PitchC[k],
						0.0,
						&mode_lib->vba.MacroTileWidthC[k],
						&mode_lib->vba.MetaRowBytesC,
						&mode_lib->vba.DPTEBytesPerRowC,
						&mode_lib->vba.PTEBufferSizeNotExceededC[i][k],
						&mode_lib->vba.dpte_row_height_chroma[k],
						&mode_lib->vba.meta_row_height_chroma[k]);
				mode_lib->vba.PrefetchLinesC[k] = CalculatePrefetchSourceLines(
						mode_lib,
						mode_lib->vba.VRatio[k] / 2.0,
						mode_lib->vba.VTAPsChroma[k],
						mode_lib->vba.Interlace[k],
						mode_lib->vba.ProgressiveToInterlaceUnitInOPP,
						mode_lib->vba.SwathHeightCPerState[i][k],
						mode_lib->vba.ViewportYStartC[k],
						&mode_lib->vba.PrefillC[k],
						&mode_lib->vba.MaxNumSwC[k]);
			} else {
				mode_lib->vba.PDEAndMetaPTEBytesPerFrameC = 0.0;
				mode_lib->vba.MetaRowBytesC = 0.0;
				mode_lib->vba.DPTEBytesPerRowC = 0.0;
				mode_lib->vba.PrefetchLinesC[k] = 0.0;
				mode_lib->vba.PTEBufferSizeNotExceededC[i][k] = true;
			}
			mode_lib->vba.PDEAndMetaPTEBytesPerFrame[k] =
					mode_lib->vba.PDEAndMetaPTEBytesPerFrameY
							+ mode_lib->vba.PDEAndMetaPTEBytesPerFrameC;
			mode_lib->vba.MetaRowBytes[k] = mode_lib->vba.MetaRowBytesY
					+ mode_lib->vba.MetaRowBytesC;
			mode_lib->vba.DPTEBytesPerRow[k] = mode_lib->vba.DPTEBytesPerRowY
					+ mode_lib->vba.DPTEBytesPerRowC;
		}
		mode_lib->vba.ExtraLatency =
				mode_lib->vba.UrgentRoundTripAndOutOfOrderLatencyPerState[i]
						+ (mode_lib->vba.TotalNumberOfActiveDPP[i]
								* mode_lib->vba.PixelChunkSizeInKByte
								+ mode_lib->vba.TotalNumberOfDCCActiveDPP[i]
										* mode_lib->vba.MetaChunkSize)
								* 1024.0
								/ mode_lib->vba.ReturnBWPerState[i];
		if (mode_lib->vba.VirtualMemoryEnable == true) {
			mode_lib->vba.ExtraLatency = mode_lib->vba.ExtraLatency
					+ mode_lib->vba.TotalNumberOfActiveDPP[i]
							* mode_lib->vba.PTEChunkSize * 1024.0
							/ mode_lib->vba.ReturnBWPerState[i];
		}
		mode_lib->vba.TimeCalc = 24.0 / mode_lib->vba.ProjectedDCFCLKDeepSleep;
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			if (mode_lib->vba.BlendingAndTiming[k] == k) {
				if (mode_lib->vba.WritebackEnable[k] == true) {
					mode_lib->vba.WritebackDelay[i][k] =
							mode_lib->vba.WritebackLatency
									+ CalculateWriteBackDelay(
											mode_lib->vba.WritebackPixelFormat[k],
											mode_lib->vba.WritebackHRatio[k],
											mode_lib->vba.WritebackVRatio[k],
											mode_lib->vba.WritebackLumaHTaps[k],
											mode_lib->vba.WritebackLumaVTaps[k],
											mode_lib->vba.WritebackChromaHTaps[k],
											mode_lib->vba.WritebackChromaVTaps[k],
											mode_lib->vba.WritebackDestinationWidth[k])
											/ mode_lib->vba.RequiredDISPCLK[i];
				} else {
					mode_lib->vba.WritebackDelay[i][k] = 0.0;
				}
				for (j = 0; j <= mode_lib->vba.NumberOfActivePlanes - 1; j++) {
					if (mode_lib->vba.BlendingAndTiming[j] == k
							&& mode_lib->vba.WritebackEnable[j]
									== true) {
						mode_lib->vba.WritebackDelay[i][k] =
								dml_max(
										mode_lib->vba.WritebackDelay[i][k],
										mode_lib->vba.WritebackLatency
												+ CalculateWriteBackDelay(
														mode_lib->vba.WritebackPixelFormat[j],
														mode_lib->vba.WritebackHRatio[j],
														mode_lib->vba.WritebackVRatio[j],
														mode_lib->vba.WritebackLumaHTaps[j],
														mode_lib->vba.WritebackLumaVTaps[j],
														mode_lib->vba.WritebackChromaHTaps[j],
														mode_lib->vba.WritebackChromaVTaps[j],
														mode_lib->vba.WritebackDestinationWidth[j])
														/ mode_lib->vba.RequiredDISPCLK[i]);
					}
				}
			}
		}
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			for (j = 0; j <= mode_lib->vba.NumberOfActivePlanes - 1; j++) {
				if (mode_lib->vba.BlendingAndTiming[k] == j) {
					mode_lib->vba.WritebackDelay[i][k] =
							mode_lib->vba.WritebackDelay[i][j];
				}
			}
		}
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			mode_lib->vba.MaximumVStartup[k] =
					mode_lib->vba.VTotal[k] - mode_lib->vba.VActive[k]
							- dml_max(
									1.0,
									dml_ceil(
											mode_lib->vba.WritebackDelay[i][k]
													/ (mode_lib->vba.HTotal[k]
															/ mode_lib->vba.PixelClock[k]),
											1.0));
		}
		mode_lib->vba.TWait = CalculateTWait(
				mode_lib->vba.PrefetchMode,
				mode_lib->vba.DRAMClockChangeLatency,
				mode_lib->vba.UrgentLatency,
				mode_lib->vba.SREnterPlusExitTime);
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			if (mode_lib->vba.XFCEnabled[k] == true) {
				mode_lib->vba.XFCRemoteSurfaceFlipDelay =
						CalculateRemoteSurfaceFlipDelay(
								mode_lib,
								mode_lib->vba.VRatio[k],
								mode_lib->vba.SwathWidthYPerState[i][k],
								dml_ceil(
										mode_lib->vba.BytePerPixelInDETY[k],
										1.0),
								mode_lib->vba.HTotal[k]
										/ mode_lib->vba.PixelClock[k],
								mode_lib->vba.XFCTSlvVupdateOffset,
								mode_lib->vba.XFCTSlvVupdateWidth,
								mode_lib->vba.XFCTSlvVreadyOffset,
								mode_lib->vba.XFCXBUFLatencyTolerance,
								mode_lib->vba.XFCFillBWOverhead,
								mode_lib->vba.XFCSlvChunkSize,
								mode_lib->vba.XFCBusTransportTime,
								mode_lib->vba.TimeCalc,
								mode_lib->vba.TWait,
								&mode_lib->vba.SrcActiveDrainRate,
								&mode_lib->vba.TInitXFill,
								&mode_lib->vba.TslvChk);
			} else {
				mode_lib->vba.XFCRemoteSurfaceFlipDelay = 0.0;
			}
			mode_lib->vba.IsErrorResult[i][k] =
					CalculatePrefetchSchedule(
							mode_lib,
							mode_lib->vba.RequiredDPPCLK[i][k],
							mode_lib->vba.RequiredDISPCLK[i],
							mode_lib->vba.PixelClock[k],
							mode_lib->vba.ProjectedDCFCLKDeepSleep,
							mode_lib->vba.DSCDelayPerState[i][k],
							mode_lib->vba.NoOfDPP[i][k],
							mode_lib->vba.ScalerEnabled[k],
							mode_lib->vba.NumberOfCursors[k],
							mode_lib->vba.DPPCLKDelaySubtotal,
							mode_lib->vba.DPPCLKDelaySCL,
							mode_lib->vba.DPPCLKDelaySCLLBOnly,
							mode_lib->vba.DPPCLKDelayCNVCFormater,
							mode_lib->vba.DPPCLKDelayCNVCCursor,
							mode_lib->vba.DISPCLKDelaySubtotal,
							mode_lib->vba.SwathWidthYPerState[i][k]
									/ mode_lib->vba.HRatio[k],
							mode_lib->vba.OutputFormat[k],
							mode_lib->vba.VTotal[k]
									- mode_lib->vba.VActive[k],
							mode_lib->vba.HTotal[k],
							mode_lib->vba.MaxInterDCNTileRepeaters,
							mode_lib->vba.MaximumVStartup[k],
							mode_lib->vba.MaxPageTableLevels,
							mode_lib->vba.VirtualMemoryEnable,
							mode_lib->vba.DynamicMetadataEnable[k],
							mode_lib->vba.DynamicMetadataLinesBeforeActiveRequired[k],
							mode_lib->vba.DynamicMetadataTransmittedBytes[k],
							mode_lib->vba.DCCEnable[k],
							mode_lib->vba.UrgentLatency,
							mode_lib->vba.ExtraLatency,
							mode_lib->vba.TimeCalc,
							mode_lib->vba.PDEAndMetaPTEBytesPerFrame[k],
							mode_lib->vba.MetaRowBytes[k],
							mode_lib->vba.DPTEBytesPerRow[k],
							mode_lib->vba.PrefetchLinesY[k],
							mode_lib->vba.SwathWidthYPerState[i][k],
							mode_lib->vba.BytePerPixelInDETY[k],
							mode_lib->vba.PrefillY[k],
							mode_lib->vba.MaxNumSwY[k],
							mode_lib->vba.PrefetchLinesC[k],
							mode_lib->vba.BytePerPixelInDETC[k],
							mode_lib->vba.PrefillC[k],
							mode_lib->vba.MaxNumSwC[k],
							mode_lib->vba.SwathHeightYPerState[i][k],
							mode_lib->vba.SwathHeightCPerState[i][k],
							mode_lib->vba.TWait,
							mode_lib->vba.XFCEnabled[k],
							mode_lib->vba.XFCRemoteSurfaceFlipDelay,
							mode_lib->vba.Interlace[k],
							mode_lib->vba.ProgressiveToInterlaceUnitInOPP,
							mode_lib->vba.DSTXAfterScaler,
							mode_lib->vba.DSTYAfterScaler,
							&mode_lib->vba.LineTimesForPrefetch[k],
							&mode_lib->vba.PrefetchBW[k],
							&mode_lib->vba.LinesForMetaPTE[k],
							&mode_lib->vba.LinesForMetaAndDPTERow[k],
							&mode_lib->vba.VRatioPreY[i][k],
							&mode_lib->vba.VRatioPreC[i][k],
							&mode_lib->vba.RequiredPrefetchPixelDataBW[i][k],
							&mode_lib->vba.VStartupRequiredWhenNotEnoughTimeForDynamicMetadata,
							&mode_lib->vba.Tno_bw[k],
							&mode_lib->vba.VUpdateOffsetPix[k],
							&mode_lib->vba.VUpdateWidthPix[k],
							&mode_lib->vba.VReadyOffsetPix[k]);
		}
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			mode_lib->vba.cursor_bw[k] = mode_lib->vba.NumberOfCursors[k]
					* mode_lib->vba.CursorWidth[k][0]
					* mode_lib->vba.CursorBPP[k][0] / 8.0
					/ (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k])
					* mode_lib->vba.VRatio[k];
		}
		mode_lib->vba.MaximumReadBandwidthWithPrefetch = 0.0;
		mode_lib->vba.prefetch_vm_bw_valid = true;
		mode_lib->vba.prefetch_row_bw_valid = true;
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			if (mode_lib->vba.PDEAndMetaPTEBytesPerFrame[k] == 0.0) {
				mode_lib->vba.prefetch_vm_bw[k] = 0.0;
			} else if (mode_lib->vba.LinesForMetaPTE[k] > 0.0) {
				mode_lib->vba.prefetch_vm_bw[k] =
						mode_lib->vba.PDEAndMetaPTEBytesPerFrame[k]
								/ (mode_lib->vba.LinesForMetaPTE[k]
										* mode_lib->vba.HTotal[k]
										/ mode_lib->vba.PixelClock[k]);
			} else {
				mode_lib->vba.prefetch_vm_bw[k] = 0.0;
				mode_lib->vba.prefetch_vm_bw_valid = false;
			}
			if (mode_lib->vba.MetaRowBytes[k] + mode_lib->vba.DPTEBytesPerRow[k]
					== 0.0) {
				mode_lib->vba.prefetch_row_bw[k] = 0.0;
			} else if (mode_lib->vba.LinesForMetaAndDPTERow[k] > 0.0) {
				mode_lib->vba.prefetch_row_bw[k] = (mode_lib->vba.MetaRowBytes[k]
						+ mode_lib->vba.DPTEBytesPerRow[k])
						/ (mode_lib->vba.LinesForMetaAndDPTERow[k]
								* mode_lib->vba.HTotal[k]
								/ mode_lib->vba.PixelClock[k]);
			} else {
				mode_lib->vba.prefetch_row_bw[k] = 0.0;
				mode_lib->vba.prefetch_row_bw_valid = false;
			}
			mode_lib->vba.MaximumReadBandwidthWithPrefetch =
					mode_lib->vba.MaximumReadBandwidthWithPrefetch
							+ mode_lib->vba.cursor_bw[k]
							+ dml_max4(
									mode_lib->vba.prefetch_vm_bw[k],
									mode_lib->vba.prefetch_row_bw[k],
									mode_lib->vba.ReadBandwidth[k],
									mode_lib->vba.RequiredPrefetchPixelDataBW[i][k]);
		}
		mode_lib->vba.PrefetchSupported[i] = true;
		if (mode_lib->vba.MaximumReadBandwidthWithPrefetch
				> mode_lib->vba.ReturnBWPerState[i]
				|| mode_lib->vba.prefetch_vm_bw_valid == false
				|| mode_lib->vba.prefetch_row_bw_valid == false) {
			mode_lib->vba.PrefetchSupported[i] = false;
		}
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			if (mode_lib->vba.LineTimesForPrefetch[k] < 2.0
					|| mode_lib->vba.LinesForMetaPTE[k] >= 8.0
					|| mode_lib->vba.LinesForMetaAndDPTERow[k] >= 16.0
					|| mode_lib->vba.IsErrorResult[i][k] == true) {
				mode_lib->vba.PrefetchSupported[i] = false;
			}
		}
		mode_lib->vba.VRatioInPrefetchSupported[i] = true;
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			if (mode_lib->vba.VRatioPreY[i][k] > 4.0
					|| mode_lib->vba.VRatioPreC[i][k] > 4.0
					|| mode_lib->vba.IsErrorResult[i][k] == true) {
				mode_lib->vba.VRatioInPrefetchSupported[i] = false;
			}
		}
		if (mode_lib->vba.PrefetchSupported[i] == true
				&& mode_lib->vba.VRatioInPrefetchSupported[i] == true) {
			mode_lib->vba.BandwidthAvailableForImmediateFlip =
					mode_lib->vba.ReturnBWPerState[i];
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				mode_lib->vba.BandwidthAvailableForImmediateFlip =
						mode_lib->vba.BandwidthAvailableForImmediateFlip
								- mode_lib->vba.cursor_bw[k]
								- dml_max(
										mode_lib->vba.ReadBandwidth[k],
										mode_lib->vba.PrefetchBW[k]);
			}
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				mode_lib->vba.ImmediateFlipBytes[k] = 0.0;
				if ((mode_lib->vba.SourcePixelFormat[k] != dm_420_8
						&& mode_lib->vba.SourcePixelFormat[k] != dm_420_10)) {
					mode_lib->vba.ImmediateFlipBytes[k] =
							mode_lib->vba.PDEAndMetaPTEBytesPerFrame[k]
									+ mode_lib->vba.MetaRowBytes[k]
									+ mode_lib->vba.DPTEBytesPerRow[k];
				}
			}
			mode_lib->vba.TotImmediateFlipBytes = 0.0;
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				if ((mode_lib->vba.SourcePixelFormat[k] != dm_420_8
						&& mode_lib->vba.SourcePixelFormat[k] != dm_420_10)) {
					mode_lib->vba.TotImmediateFlipBytes =
							mode_lib->vba.TotImmediateFlipBytes
									+ mode_lib->vba.ImmediateFlipBytes[k];
				}
			}
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				CalculateFlipSchedule(
						mode_lib,
						mode_lib->vba.ExtraLatency,
						mode_lib->vba.UrgentLatency,
						mode_lib->vba.MaxPageTableLevels,
						mode_lib->vba.VirtualMemoryEnable,
						mode_lib->vba.BandwidthAvailableForImmediateFlip,
						mode_lib->vba.TotImmediateFlipBytes,
						mode_lib->vba.SourcePixelFormat[k],
						mode_lib->vba.ImmediateFlipBytes[k],
						mode_lib->vba.HTotal[k]
								/ mode_lib->vba.PixelClock[k],
						mode_lib->vba.VRatio[k],
						mode_lib->vba.Tno_bw[k],
						mode_lib->vba.PDEAndMetaPTEBytesPerFrame[k],
						mode_lib->vba.MetaRowBytes[k],
						mode_lib->vba.DPTEBytesPerRow[k],
						mode_lib->vba.DCCEnable[k],
						mode_lib->vba.dpte_row_height[k],
						mode_lib->vba.meta_row_height[k],
						mode_lib->vba.qual_row_bw[k],
						&mode_lib->vba.DestinationLinesToRequestVMInImmediateFlip[k],
						&mode_lib->vba.DestinationLinesToRequestRowInImmediateFlip[k],
						&mode_lib->vba.final_flip_bw[k],
						&mode_lib->vba.ImmediateFlipSupportedForPipe[k]);
			}
			mode_lib->vba.total_dcn_read_bw_with_flip = 0.0;
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				mode_lib->vba.total_dcn_read_bw_with_flip =
						mode_lib->vba.total_dcn_read_bw_with_flip
								+ mode_lib->vba.cursor_bw[k]
								+ dml_max3(
										mode_lib->vba.prefetch_vm_bw[k],
										mode_lib->vba.prefetch_row_bw[k],
										mode_lib->vba.final_flip_bw[k]
												+ dml_max(
														mode_lib->vba.ReadBandwidth[k],
														mode_lib->vba.RequiredPrefetchPixelDataBW[i][k]));
			}
			mode_lib->vba.ImmediateFlipSupportedForState[i] = true;
			if (mode_lib->vba.total_dcn_read_bw_with_flip
					> mode_lib->vba.ReturnBWPerState[i]) {
				mode_lib->vba.ImmediateFlipSupportedForState[i] = false;
			}
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				if (mode_lib->vba.ImmediateFlipSupportedForPipe[k] == false) {
					mode_lib->vba.ImmediateFlipSupportedForState[i] = false;
				}
			}
		} else {
			mode_lib->vba.ImmediateFlipSupportedForState[i] = false;
		}
	}
	/*PTE Buffer Size Check*/

	for (i = 0; i <= DC__VOLTAGE_STATES; i++) {
		mode_lib->vba.PTEBufferSizeNotExceeded[i] = true;
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			if (mode_lib->vba.PTEBufferSizeNotExceededY[i][k] == false
					|| mode_lib->vba.PTEBufferSizeNotExceededC[i][k] == false) {
				mode_lib->vba.PTEBufferSizeNotExceeded[i] = false;
			}
		}
	}
	/*Cursor Support Check*/

	mode_lib->vba.CursorSupport = true;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.CursorWidth[k][0] > 0.0) {
			if (dml_floor(
					dml_floor(
							mode_lib->vba.CursorBufferSize
									- mode_lib->vba.CursorChunkSize,
							mode_lib->vba.CursorChunkSize) * 1024.0
							/ (mode_lib->vba.CursorWidth[k][0]
									* mode_lib->vba.CursorBPP[k][0]
									/ 8.0),
					1.0)
					* (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k])
					/ mode_lib->vba.VRatio[k] < mode_lib->vba.UrgentLatency
					|| (mode_lib->vba.CursorBPP[k][0] == 64.0
							&& mode_lib->vba.Cursor64BppSupport == false)) {
				mode_lib->vba.CursorSupport = false;
			}
		}
	}
	/*Valid Pitch Check*/

	mode_lib->vba.PitchSupport = true;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		mode_lib->vba.AlignedYPitch[k] = dml_ceil(
				dml_max(mode_lib->vba.PitchY[k], mode_lib->vba.ViewportWidth[k]),
				mode_lib->vba.MacroTileWidthY[k]);
		if (mode_lib->vba.AlignedYPitch[k] > mode_lib->vba.PitchY[k]) {
			mode_lib->vba.PitchSupport = false;
		}
		if (mode_lib->vba.DCCEnable[k] == true) {
			mode_lib->vba.AlignedDCCMetaPitch[k] = dml_ceil(
					dml_max(
							mode_lib->vba.DCCMetaPitchY[k],
							mode_lib->vba.ViewportWidth[k]),
					64.0 * mode_lib->vba.Read256BlockWidthY[k]);
		} else {
			mode_lib->vba.AlignedDCCMetaPitch[k] = mode_lib->vba.DCCMetaPitchY[k];
		}
		if (mode_lib->vba.AlignedDCCMetaPitch[k] > mode_lib->vba.DCCMetaPitchY[k]) {
			mode_lib->vba.PitchSupport = false;
		}
		if (mode_lib->vba.SourcePixelFormat[k] != dm_444_64
				&& mode_lib->vba.SourcePixelFormat[k] != dm_444_32
				&& mode_lib->vba.SourcePixelFormat[k] != dm_444_16
				&& mode_lib->vba.SourcePixelFormat[k] != dm_mono_16
				&& mode_lib->vba.SourcePixelFormat[k] != dm_mono_8) {
			mode_lib->vba.AlignedCPitch[k] = dml_ceil(
					dml_max(
							mode_lib->vba.PitchC[k],
							mode_lib->vba.ViewportWidth[k] / 2.0),
					mode_lib->vba.MacroTileWidthC[k]);
		} else {
			mode_lib->vba.AlignedCPitch[k] = mode_lib->vba.PitchC[k];
		}
		if (mode_lib->vba.AlignedCPitch[k] > mode_lib->vba.PitchC[k]) {
			mode_lib->vba.PitchSupport = false;
		}
	}
	/*Mode Support, Voltage State and SOC Configuration*/

	for (i = DC__VOLTAGE_STATES; i >= 0; i--) {
		if (mode_lib->vba.ScaleRatioAndTapsSupport == true
				&& mode_lib->vba.SourceFormatPixelAndScanSupport == true
				&& mode_lib->vba.ViewportSizeSupport[i] == true
				&& mode_lib->vba.BandwidthSupport[i] == true
				&& mode_lib->vba.DIOSupport[i] == true
				&& mode_lib->vba.NotEnoughDSCUnits[i] == false
				&& mode_lib->vba.DSCCLKRequiredMoreThanSupported[i] == false
				&& mode_lib->vba.UrgentLatencySupport[i] == true
				&& mode_lib->vba.ROBSupport[i] == true
				&& mode_lib->vba.DISPCLK_DPPCLK_Support[i] == true
				&& mode_lib->vba.TotalAvailablePipesSupport[i] == true
				&& mode_lib->vba.NumberOfOTGSupport == true
				&& mode_lib->vba.WritebackModeSupport == true
				&& mode_lib->vba.WritebackLatencySupport == true
				&& mode_lib->vba.WritebackScaleRatioAndTapsSupport == true
				&& mode_lib->vba.CursorSupport == true
				&& mode_lib->vba.PitchSupport == true
				&& mode_lib->vba.PrefetchSupported[i] == true
				&& mode_lib->vba.VRatioInPrefetchSupported[i] == true
				&& mode_lib->vba.PTEBufferSizeNotExceeded[i] == true
				&& mode_lib->vba.NonsupportedDSCInputBPC == false) {
			mode_lib->vba.ModeSupport[i] = true;
		} else {
			mode_lib->vba.ModeSupport[i] = false;
		}
	}
	for (i = DC__VOLTAGE_STATES; i >= 0; i--) {
		if (i == DC__VOLTAGE_STATES || mode_lib->vba.ModeSupport[i] == true) {
			mode_lib->vba.VoltageLevel = i;
		}
	}
	mode_lib->vba.DCFCLK = mode_lib->vba.DCFCLKPerState[mode_lib->vba.VoltageLevel];
	mode_lib->vba.DRAMSpeed = mode_lib->vba.DRAMSpeedPerState[mode_lib->vba.VoltageLevel];
	mode_lib->vba.FabricClock = mode_lib->vba.FabricClockPerState[mode_lib->vba.VoltageLevel];
	mode_lib->vba.SOCCLK = mode_lib->vba.SOCCLKPerState[mode_lib->vba.VoltageLevel];
	mode_lib->vba.FabricAndDRAMBandwidth =
			mode_lib->vba.FabricAndDRAMBandwidthPerState[mode_lib->vba.VoltageLevel];
	mode_lib->vba.ImmediateFlipSupport =
			mode_lib->vba.ImmediateFlipSupportedForState[mode_lib->vba.VoltageLevel];
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		mode_lib->vba.DPPPerPlane[k] = mode_lib->vba.NoOfDPP[mode_lib->vba.VoltageLevel][k];
	}
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.BlendingAndTiming[k] == k) {
			mode_lib->vba.ODMCombineEnabled[k] =
					mode_lib->vba.ODMCombineEnablePerState[mode_lib->vba.VoltageLevel][k];
		} else {
			mode_lib->vba.ODMCombineEnabled[k] = 0;
		}
		mode_lib->vba.DSCEnabled[k] =
				mode_lib->vba.RequiresDSC[mode_lib->vba.VoltageLevel][k];
		mode_lib->vba.OutputBpp[k] =
				mode_lib->vba.OutputBppPerState[mode_lib->vba.VoltageLevel][k];
	}
}
