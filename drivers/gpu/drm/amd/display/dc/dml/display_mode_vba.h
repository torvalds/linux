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

#ifndef __DML2_DISPLAY_MODE_VBA_H__
#define __DML2_DISPLAY_MODE_VBA_H__

#include "dml_common_defs.h"

struct display_mode_lib;

void set_prefetch_mode(struct display_mode_lib *mode_lib,
		bool cstate_en,
		bool pstate_en,
		bool ignore_viewport_pos,
		bool immediate_flip_support);

#define dml_get_attr_decl(attr) double get_##attr(struct display_mode_lib *mode_lib, const display_e2e_pipe_params_st *pipes, unsigned int num_pipes)

dml_get_attr_decl(clk_dcf_deepsleep);
dml_get_attr_decl(wm_urgent);
dml_get_attr_decl(wm_memory_trip);
dml_get_attr_decl(wm_writeback_urgent);
dml_get_attr_decl(wm_stutter_exit);
dml_get_attr_decl(wm_stutter_enter_exit);
dml_get_attr_decl(wm_dram_clock_change);
dml_get_attr_decl(wm_writeback_dram_clock_change);
dml_get_attr_decl(wm_xfc_underflow);
dml_get_attr_decl(stutter_efficiency_no_vblank);
dml_get_attr_decl(stutter_efficiency);
dml_get_attr_decl(urgent_latency);
dml_get_attr_decl(urgent_extra_latency);
dml_get_attr_decl(nonurgent_latency);
dml_get_attr_decl(dram_clock_change_latency);
dml_get_attr_decl(dispclk_calculated);
dml_get_attr_decl(total_data_read_bw);
dml_get_attr_decl(return_bw);
dml_get_attr_decl(tcalc);

#define dml_get_pipe_attr_decl(attr) double get_##attr(struct display_mode_lib *mode_lib, const display_e2e_pipe_params_st *pipes, unsigned int num_pipes, unsigned int which_pipe)

dml_get_pipe_attr_decl(dsc_delay);
dml_get_pipe_attr_decl(dppclk_calculated);
dml_get_pipe_attr_decl(dscclk_calculated);
dml_get_pipe_attr_decl(min_ttu_vblank);
dml_get_pipe_attr_decl(vratio_prefetch_l);
dml_get_pipe_attr_decl(vratio_prefetch_c);
dml_get_pipe_attr_decl(dst_x_after_scaler);
dml_get_pipe_attr_decl(dst_y_after_scaler);
dml_get_pipe_attr_decl(dst_y_per_vm_vblank);
dml_get_pipe_attr_decl(dst_y_per_row_vblank);
dml_get_pipe_attr_decl(dst_y_prefetch);
dml_get_pipe_attr_decl(dst_y_per_vm_flip);
dml_get_pipe_attr_decl(dst_y_per_row_flip);
dml_get_pipe_attr_decl(xfc_transfer_delay);
dml_get_pipe_attr_decl(xfc_precharge_delay);
dml_get_pipe_attr_decl(xfc_remote_surface_flip_latency);
dml_get_pipe_attr_decl(xfc_prefetch_margin);

unsigned int get_vstartup_calculated(
		struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes,
		unsigned int which_pipe);

double get_total_immediate_flip_bytes(
		struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes);
double get_total_immediate_flip_bw(
		struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes);
double get_total_prefetch_bw(
		struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes);

unsigned int dml_get_voltage_level(
		struct display_mode_lib *mode_lib,
		const display_e2e_pipe_params_st *pipes,
		unsigned int num_pipes);

bool Calculate256BBlockSizes(
		enum source_format_class SourcePixelFormat,
		enum dm_swizzle_mode SurfaceTiling,
		unsigned int BytePerPixelY,
		unsigned int BytePerPixelC,
		unsigned int *BlockHeight256BytesY,
		unsigned int *BlockHeight256BytesC,
		unsigned int *BlockWidth256BytesY,
		unsigned int *BlockWidth256BytesC);


struct vba_vars_st {
	ip_params_st	ip;
	soc_bounding_box_st	soc;

	unsigned int MaximumMaxVStartupLines;
	double cursor_bw[DC__NUM_DPP__MAX];
	double meta_row_bw[DC__NUM_DPP__MAX];
	double dpte_row_bw[DC__NUM_DPP__MAX];
	double qual_row_bw[DC__NUM_DPP__MAX];
	double WritebackDISPCLK;
	double PSCL_THROUGHPUT_LUMA[DC__NUM_DPP__MAX];
	double PSCL_THROUGHPUT_CHROMA[DC__NUM_DPP__MAX];
	double DPPCLKUsingSingleDPPLuma;
	double DPPCLKUsingSingleDPPChroma;
	double DPPCLKUsingSingleDPP[DC__NUM_DPP__MAX];
	double DISPCLKWithRamping;
	double DISPCLKWithoutRamping;
	double GlobalDPPCLK;
	double DISPCLKWithRampingRoundedToDFSGranularity;
	double DISPCLKWithoutRampingRoundedToDFSGranularity;
	double MaxDispclkRoundedToDFSGranularity;
	bool DCCEnabledAnyPlane;
	double ReturnBandwidthToDCN;
	unsigned int SwathWidthY[DC__NUM_DPP__MAX];
	unsigned int SwathWidthSingleDPPY[DC__NUM_DPP__MAX];
	double BytePerPixelDETY[DC__NUM_DPP__MAX];
	double BytePerPixelDETC[DC__NUM_DPP__MAX];
	double ReadBandwidthPlaneLuma[DC__NUM_DPP__MAX];
	double ReadBandwidthPlaneChroma[DC__NUM_DPP__MAX];
	unsigned int TotalActiveDPP;
	unsigned int TotalDCCActiveDPP;
	double UrgentRoundTripAndOutOfOrderLatency;
	double DisplayPipeLineDeliveryTimeLuma[DC__NUM_DPP__MAX];                     // WM
	double DisplayPipeLineDeliveryTimeChroma[DC__NUM_DPP__MAX];                     // WM
	double LinesInDETY[DC__NUM_DPP__MAX];                     // WM
	double LinesInDETC[DC__NUM_DPP__MAX];                     // WM
	unsigned int LinesInDETYRoundedDownToSwath[DC__NUM_DPP__MAX];                     // WM
	unsigned int LinesInDETCRoundedDownToSwath[DC__NUM_DPP__MAX];                     // WM
	double FullDETBufferingTimeY[DC__NUM_DPP__MAX];                     // WM
	double FullDETBufferingTimeC[DC__NUM_DPP__MAX];                     // WM
	double MinFullDETBufferingTime;
	double FrameTimeForMinFullDETBufferingTime;
	double AverageReadBandwidthGBytePerSecond;
	double PartOfBurstThatFitsInROB;
	double StutterBurstTime;
	//unsigned int     NextPrefetchMode;
	double VBlankTime;
	double SmallestVBlank;
	double DCFCLKDeepSleepPerPlane;
	double EffectiveDETPlusLBLinesLuma;
	double EffectiveDETPlusLBLinesChroma;
	double UrgentLatencySupportUsLuma;
	double UrgentLatencySupportUsChroma;
	double UrgentLatencySupportUs[DC__NUM_DPP__MAX];
	unsigned int DSCFormatFactor;
	unsigned int BlockHeight256BytesY[DC__NUM_DPP__MAX];
	unsigned int BlockHeight256BytesC[DC__NUM_DPP__MAX];
	unsigned int BlockWidth256BytesY[DC__NUM_DPP__MAX];
	unsigned int BlockWidth256BytesC[DC__NUM_DPP__MAX];
	double VInitPreFillY[DC__NUM_DPP__MAX];
	double VInitPreFillC[DC__NUM_DPP__MAX];
	unsigned int MaxNumSwathY[DC__NUM_DPP__MAX];
	unsigned int MaxNumSwathC[DC__NUM_DPP__MAX];
	double PrefetchSourceLinesY[DC__NUM_DPP__MAX];
	double PrefetchSourceLinesC[DC__NUM_DPP__MAX];
	double PixelPTEBytesPerRow[DC__NUM_DPP__MAX];
	double MetaRowByte[DC__NUM_DPP__MAX];
	unsigned int dpte_row_height[DC__NUM_DPP__MAX];
	unsigned int dpte_row_height_chroma[DC__NUM_DPP__MAX];
	unsigned int meta_row_height[DC__NUM_DPP__MAX];
	unsigned int meta_row_height_chroma[DC__NUM_DPP__MAX];

	unsigned int MacroTileWidthY[DC__NUM_DPP__MAX];
	unsigned int MacroTileWidthC[DC__NUM_DPP__MAX];
	unsigned int MaxVStartupLines[DC__NUM_DPP__MAX];
	double WritebackDelay[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	bool PrefetchModeSupported;
	bool AllowDRAMClockChangeDuringVBlank[DC__NUM_DPP__MAX];
	bool AllowDRAMSelfRefreshDuringVBlank[DC__NUM_DPP__MAX];
	double RequiredPrefetchPixDataBW[DC__NUM_DPP__MAX];
	double XFCRemoteSurfaceFlipDelay;
	double TInitXFill;
	double TslvChk;
	double SrcActiveDrainRate;
	double Tno_bw[DC__NUM_DPP__MAX];
	bool ImmediateFlipSupported;

	double prefetch_vm_bw[DC__NUM_DPP__MAX];
	double prefetch_row_bw[DC__NUM_DPP__MAX];
	bool ImmediateFlipSupportedForPipe[DC__NUM_DPP__MAX];
	unsigned int VStartupLines;
	double DisplayPipeLineDeliveryTimeLumaPrefetch[DC__NUM_DPP__MAX];
	double DisplayPipeLineDeliveryTimeChromaPrefetch[DC__NUM_DPP__MAX];
	unsigned int ActiveDPPs;
	unsigned int LBLatencyHidingSourceLinesY;
	unsigned int LBLatencyHidingSourceLinesC;
	double ActiveDRAMClockChangeLatencyMargin[DC__NUM_DPP__MAX];
	double MinActiveDRAMClockChangeMargin;
	double XFCSlaveVUpdateOffset[DC__NUM_DPP__MAX];
	double XFCSlaveVupdateWidth[DC__NUM_DPP__MAX];
	double XFCSlaveVReadyOffset[DC__NUM_DPP__MAX];
	double InitFillLevel;
	double FinalFillMargin;
	double FinalFillLevel;
	double RemainingFillLevel;
	double TFinalxFill;


	//
	// SOC Bounding Box Parameters
	//
	double SRExitTime;
	double SREnterPlusExitTime;
	double UrgentLatency;
	double WritebackLatency;
	double PercentOfIdealDRAMAndFabricBWReceivedAfterUrgLatency;
	double NumberOfChannels;
	double DRAMChannelWidth;
	double FabricDatapathToDCNDataReturn;
	double ReturnBusWidth;
	double Downspreading;
	double DISPCLKDPPCLKDSCCLKDownSpreading;
	double DISPCLKDPPCLKVCOSpeed;
	double RoundTripPingLatencyCycles;
	double UrgentOutOfOrderReturnPerChannel;
	unsigned int VMMPageSize;
	double DRAMClockChangeLatency;
	double XFCBusTransportTime;
	double XFCXBUFLatencyTolerance;

	//
	// IP Parameters
	//
	unsigned int ROBBufferSizeInKByte;
	double DETBufferSizeInKByte;
	unsigned int DPPOutputBufferPixels;
	unsigned int OPPOutputBufferLines;
	unsigned int PixelChunkSizeInKByte;
	double ReturnBW;
	bool VirtualMemoryEnable;
	unsigned int MaxPageTableLevels;
	unsigned int OverridePageTableLevels;
	unsigned int PTEChunkSize;
	unsigned int MetaChunkSize;
	unsigned int WritebackChunkSize;
	bool ODMCapability;
	unsigned int NumberOfDSC;
	unsigned int LineBufferSize;
	unsigned int MaxLineBufferLines;
	unsigned int WritebackInterfaceLumaBufferSize;
	unsigned int WritebackInterfaceChromaBufferSize;
	unsigned int WritebackChromaLineBufferWidth;
	double MaxDCHUBToPSCLThroughput;
	double MaxPSCLToLBThroughput;
	unsigned int PTEBufferSizeInRequests;
	double DISPCLKRampingMargin;
	unsigned int MaxInterDCNTileRepeaters;
	bool XFCSupported;
	double XFCSlvChunkSize;
	double XFCFillBWOverhead;
	double XFCFillConstant;
	double XFCTSlvVupdateOffset;
	double XFCTSlvVupdateWidth;
	double XFCTSlvVreadyOffset;
	double DPPCLKDelaySubtotal;
	double DPPCLKDelaySCL;
	double DPPCLKDelaySCLLBOnly;
	double DPPCLKDelayCNVCFormater;
	double DPPCLKDelayCNVCCursor;
	double DISPCLKDelaySubtotal;
	bool ProgressiveToInterlaceUnitInOPP;
	unsigned int PDEProcessingBufIn64KBReqs;

	// Pipe/Plane Parameters
	int VoltageLevel;
	double FabricAndDRAMBandwidth;
	double FabricClock;
	double DRAMSpeed;
	double DISPCLK;
	double SOCCLK;
	double DCFCLK;

	unsigned int NumberOfActivePlanes;
	unsigned int ViewportWidth[DC__NUM_DPP__MAX];
	unsigned int ViewportHeight[DC__NUM_DPP__MAX];
	unsigned int ViewportYStartY[DC__NUM_DPP__MAX];
	unsigned int ViewportYStartC[DC__NUM_DPP__MAX];
	unsigned int PitchY[DC__NUM_DPP__MAX];
	unsigned int PitchC[DC__NUM_DPP__MAX];
	double HRatio[DC__NUM_DPP__MAX];
	double VRatio[DC__NUM_DPP__MAX];
	unsigned int htaps[DC__NUM_DPP__MAX];
	unsigned int vtaps[DC__NUM_DPP__MAX];
	unsigned int HTAPsChroma[DC__NUM_DPP__MAX];
	unsigned int VTAPsChroma[DC__NUM_DPP__MAX];
	unsigned int HTotal[DC__NUM_DPP__MAX];
	unsigned int VTotal[DC__NUM_DPP__MAX];
	unsigned int DPPPerPlane[DC__NUM_DPP__MAX];
	double PixelClock[DC__NUM_DPP__MAX];
	double PixelClockBackEnd[DC__NUM_DPP__MAX];
	double DPPCLK[DC__NUM_DPP__MAX];
	bool DCCEnable[DC__NUM_DPP__MAX];
	unsigned int DCCMetaPitchY[DC__NUM_DPP__MAX];
	enum scan_direction_class SourceScan[DC__NUM_DPP__MAX];
	enum source_format_class SourcePixelFormat[DC__NUM_DPP__MAX];
	bool WritebackEnable[DC__NUM_DPP__MAX];
	double WritebackDestinationWidth[DC__NUM_DPP__MAX];
	double WritebackDestinationHeight[DC__NUM_DPP__MAX];
	double WritebackSourceHeight[DC__NUM_DPP__MAX];
	enum source_format_class WritebackPixelFormat[DC__NUM_DPP__MAX];
	unsigned int WritebackLumaHTaps[DC__NUM_DPP__MAX];
	unsigned int WritebackLumaVTaps[DC__NUM_DPP__MAX];
	unsigned int WritebackChromaHTaps[DC__NUM_DPP__MAX];
	unsigned int WritebackChromaVTaps[DC__NUM_DPP__MAX];
	double WritebackHRatio[DC__NUM_DPP__MAX];
	double WritebackVRatio[DC__NUM_DPP__MAX];
	unsigned int HActive[DC__NUM_DPP__MAX];
	unsigned int VActive[DC__NUM_DPP__MAX];
	bool Interlace[DC__NUM_DPP__MAX];
	enum dm_swizzle_mode SurfaceTiling[DC__NUM_DPP__MAX];
	unsigned int ScalerRecoutWidth[DC__NUM_DPP__MAX];
	bool DynamicMetadataEnable[DC__NUM_DPP__MAX];
	unsigned int DynamicMetadataLinesBeforeActiveRequired[DC__NUM_DPP__MAX];
	unsigned int DynamicMetadataTransmittedBytes[DC__NUM_DPP__MAX];
	double DCCRate[DC__NUM_DPP__MAX];
	bool ODMCombineEnabled[DC__NUM_DPP__MAX];
	double OutputBpp[DC__NUM_DPP__MAX];
	unsigned int NumberOfDSCSlices[DC__NUM_DPP__MAX];
	bool DSCEnabled[DC__NUM_DPP__MAX];
	unsigned int DSCDelay[DC__NUM_DPP__MAX];
	unsigned int DSCInputBitPerComponent[DC__NUM_DPP__MAX];
	enum output_format_class OutputFormat[DC__NUM_DPP__MAX];
	enum output_encoder_class Output[DC__NUM_DPP__MAX];
	unsigned int BlendingAndTiming[DC__NUM_DPP__MAX];
	bool SynchronizedVBlank;
	unsigned int NumberOfCursors[DC__NUM_DPP__MAX];
	unsigned int CursorWidth[DC__NUM_DPP__MAX][DC__NUM_CURSOR__MAX];
	unsigned int CursorBPP[DC__NUM_DPP__MAX][DC__NUM_CURSOR__MAX];
	bool XFCEnabled[DC__NUM_DPP__MAX];
	bool ScalerEnabled[DC__NUM_DPP__MAX];

	// Intermediates/Informational
	bool ImmediateFlipSupport;
	unsigned int SwathHeightY[DC__NUM_DPP__MAX];
	unsigned int SwathHeightC[DC__NUM_DPP__MAX];
	unsigned int DETBufferSizeY[DC__NUM_DPP__MAX];
	unsigned int DETBufferSizeC[DC__NUM_DPP__MAX];
	unsigned int LBBitPerPixel[DC__NUM_DPP__MAX];
	double LastPixelOfLineExtraWatermark;
	double TotalDataReadBandwidth;
	unsigned int TotalActiveWriteback;
	unsigned int EffectiveLBLatencyHidingSourceLinesLuma;
	unsigned int EffectiveLBLatencyHidingSourceLinesChroma;
	double BandwidthAvailableForImmediateFlip;
	unsigned int PrefetchMode;
	bool IgnoreViewportPositioning;
	double PrefetchBandwidth[DC__NUM_DPP__MAX];
	bool ErrorResult[DC__NUM_DPP__MAX];
	double PDEAndMetaPTEBytesFrame[DC__NUM_DPP__MAX];

	//
	// Calculated dml_ml->vba.Outputs
	//
	double DCFClkDeepSleep;
	double UrgentWatermark;
	double UrgentExtraLatency;
	double MemoryTripWatermark;
	double WritebackUrgentWatermark;
	double StutterExitWatermark;
	double StutterEnterPlusExitWatermark;
	double DRAMClockChangeWatermark;
	double WritebackDRAMClockChangeWatermark;
	double StutterEfficiency;
	double StutterEfficiencyNotIncludingVBlank;
	double MinUrgentLatencySupportUs;
	double NonUrgentLatencyTolerance;
	double MinActiveDRAMClockChangeLatencySupported;
	enum clock_change_support DRAMClockChangeSupport;

	// These are the clocks calcuated by the library but they are not actually
	// used explicitly. They are fetched by tests and then possibly used. The
	// ultimate values to use are the ones specified by the parameters to DML
	double DISPCLK_calculated;
	double DSCCLK_calculated[DC__NUM_DPP__MAX];
	double DPPCLK_calculated[DC__NUM_DPP__MAX];

	unsigned int VStartup[DC__NUM_DPP__MAX];
	unsigned int VUpdateOffsetPix[DC__NUM_DPP__MAX];
	unsigned int VUpdateWidthPix[DC__NUM_DPP__MAX];
	unsigned int VReadyOffsetPix[DC__NUM_DPP__MAX];
	unsigned int VStartupRequiredWhenNotEnoughTimeForDynamicMetadata;

	double ImmediateFlipBW;
	unsigned int TotImmediateFlipBytes;
	double TCalc;
	double MinTTUVBlank[DC__NUM_DPP__MAX];
	double VRatioPrefetchY[DC__NUM_DPP__MAX];
	double VRatioPrefetchC[DC__NUM_DPP__MAX];
	double DSTXAfterScaler[DC__NUM_DPP__MAX];
	double DSTYAfterScaler[DC__NUM_DPP__MAX];

	double DestinationLinesToRequestVMInVBlank[DC__NUM_DPP__MAX];
	double DestinationLinesToRequestRowInVBlank[DC__NUM_DPP__MAX];
	double DestinationLinesForPrefetch[DC__NUM_DPP__MAX];
	double DestinationLinesToRequestRowInImmediateFlip[DC__NUM_DPP__MAX];
	double DestinationLinesToRequestVMInImmediateFlip[DC__NUM_DPP__MAX];

	double XFCTransferDelay[DC__NUM_DPP__MAX];
	double XFCPrechargeDelay[DC__NUM_DPP__MAX];
	double XFCRemoteSurfaceFlipLatency[DC__NUM_DPP__MAX];
	double XFCPrefetchMargin[DC__NUM_DPP__MAX];

	display_e2e_pipe_params_st cache_pipes[DC__NUM_DPP__MAX];
	unsigned int cache_num_pipes;
	unsigned int pipe_plane[DC__NUM_DPP__MAX];

	/* vba mode support */
	/*inputs*/
	bool SupportGFX7CompatibleTilingIn32bppAnd64bpp;
	double MaxHSCLRatio;
	double MaxVSCLRatio;
	unsigned int  MaxNumWriteback;
	bool WritebackLumaAndChromaScalingSupported;
	bool Cursor64BppSupport;
	double DCFCLKPerState[DC__VOLTAGE_STATES + 1];
	double FabricClockPerState[DC__VOLTAGE_STATES + 1];
	double SOCCLKPerState[DC__VOLTAGE_STATES + 1];
	double PHYCLKPerState[DC__VOLTAGE_STATES + 1];
	double MaxDppclk[DC__VOLTAGE_STATES + 1];
	double MaxDSCCLK[DC__VOLTAGE_STATES + 1];
	double DRAMSpeedPerState[DC__VOLTAGE_STATES + 1];
	double MaxDispclk[DC__VOLTAGE_STATES + 1];

	/*outputs*/
	bool ScaleRatioAndTapsSupport;
	bool SourceFormatPixelAndScanSupport;
	unsigned int SwathWidthYSingleDPP[DC__NUM_DPP__MAX];
	double BytePerPixelInDETY[DC__NUM_DPP__MAX];
	double BytePerPixelInDETC[DC__NUM_DPP__MAX];
	double TotalReadBandwidthConsumedGBytePerSecond;
	double ReadBandwidth[DC__NUM_DPP__MAX];
	double TotalWriteBandwidthConsumedGBytePerSecond;
	double WriteBandwidth[DC__NUM_DPP__MAX];
	double TotalBandwidthConsumedGBytePerSecond;
	bool DCCEnabledInAnyPlane;
	bool WritebackLatencySupport;
	bool WritebackModeSupport;
	bool Writeback10bpc420Supported;
	bool BandwidthSupport[DC__VOLTAGE_STATES + 1];
	unsigned int TotalNumberOfActiveWriteback;
	double CriticalPoint;
	double ReturnBWToDCNPerState;
	double FabricAndDRAMBandwidthPerState[DC__VOLTAGE_STATES + 1];
	double ReturnBWPerState[DC__VOLTAGE_STATES + 1];
	double UrgentRoundTripAndOutOfOrderLatencyPerState[DC__VOLTAGE_STATES + 1];
	bool ODMCombineEnablePerState[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	bool PTEBufferSizeNotExceededY[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	bool PTEBufferSizeNotExceededC[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	bool PrefetchSupported[DC__VOLTAGE_STATES + 1];
	bool VRatioInPrefetchSupported[DC__VOLTAGE_STATES + 1];
	bool DISPCLK_DPPCLK_Support[DC__VOLTAGE_STATES + 1];
	bool TotalAvailablePipesSupport[DC__VOLTAGE_STATES + 1];
	bool UrgentLatencySupport[DC__VOLTAGE_STATES + 1];
	bool ModeSupport[DC__VOLTAGE_STATES + 1];
	bool DIOSupport[DC__VOLTAGE_STATES + 1];
	bool NotEnoughDSCUnits[DC__VOLTAGE_STATES + 1];
	bool DSCCLKRequiredMoreThanSupported[DC__VOLTAGE_STATES + 1];
	bool ROBSupport[DC__VOLTAGE_STATES + 1];
	bool PTEBufferSizeNotExceeded[DC__VOLTAGE_STATES + 1];
	bool RequiresDSC[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	bool IsErrorResult[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	bool ViewportSizeSupport[DC__VOLTAGE_STATES + 1];
	bool prefetch_vm_bw_valid;
	bool prefetch_row_bw_valid;
	bool NumberOfOTGSupport;
	bool NonsupportedDSCInputBPC;
	bool WritebackScaleRatioAndTapsSupport;
	bool CursorSupport;
	bool PitchSupport;

	double WritebackLineBufferLumaBufferSize;
	double WritebackLineBufferChromaBufferSize;
	double WritebackMinHSCLRatio;
	double WritebackMinVSCLRatio;
	double WritebackMaxHSCLRatio;
	double WritebackMaxVSCLRatio;
	double WritebackMaxHSCLTaps;
	double WritebackMaxVSCLTaps;
	unsigned int MaxNumDPP;
	unsigned int MaxNumOTG;
	double CursorBufferSize;
	double CursorChunkSize;
	unsigned int Mode;
	unsigned int NoOfDPP[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	double OutputLinkDPLanes[DC__NUM_DPP__MAX];
	double SwathWidthYPerState[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	double SwathHeightYPerState[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	double SwathHeightCPerState[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	double UrgentLatencySupportUsPerState[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	double VRatioPreY[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	double VRatioPreC[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	double RequiredPrefetchPixelDataBW[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	double RequiredDPPCLK[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	double RequiredDISPCLK[DC__VOLTAGE_STATES + 1];
	double TotalNumberOfActiveDPP[DC__VOLTAGE_STATES + 1];
	double TotalNumberOfDCCActiveDPP[DC__VOLTAGE_STATES + 1];
	double PrefetchBW[DC__NUM_DPP__MAX];
	double PDEAndMetaPTEBytesPerFrame[DC__NUM_DPP__MAX];
	double MetaRowBytes[DC__NUM_DPP__MAX];
	double DPTEBytesPerRow[DC__NUM_DPP__MAX];
	double PrefetchLinesY[DC__NUM_DPP__MAX];
	double PrefetchLinesC[DC__NUM_DPP__MAX];
	unsigned int MaxNumSwY[DC__NUM_DPP__MAX];
	unsigned int MaxNumSwC[DC__NUM_DPP__MAX];
	double PrefillY[DC__NUM_DPP__MAX];
	double PrefillC[DC__NUM_DPP__MAX];
	double LineTimesForPrefetch[DC__NUM_DPP__MAX];
	double LinesForMetaPTE[DC__NUM_DPP__MAX];
	double LinesForMetaAndDPTERow[DC__NUM_DPP__MAX];
	double MinDPPCLKUsingSingleDPP[DC__NUM_DPP__MAX];
	double RequiresFEC[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	unsigned int OutputBppPerState[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	double DSCDelayPerState[DC__VOLTAGE_STATES + 1][DC__NUM_DPP__MAX];
	unsigned int Read256BlockHeightY[DC__NUM_DPP__MAX];
	unsigned int Read256BlockWidthY[DC__NUM_DPP__MAX];
	unsigned int Read256BlockHeightC[DC__NUM_DPP__MAX];
	unsigned int Read256BlockWidthC[DC__NUM_DPP__MAX];
	unsigned int ImmediateFlipBytes[DC__NUM_DPP__MAX];
	double MaxSwathHeightY[DC__NUM_DPP__MAX];
	double MaxSwathHeightC[DC__NUM_DPP__MAX];
	double MinSwathHeightY[DC__NUM_DPP__MAX];
	double MinSwathHeightC[DC__NUM_DPP__MAX];
	double PSCL_FACTOR[DC__NUM_DPP__MAX];
	double PSCL_FACTOR_CHROMA[DC__NUM_DPP__MAX];
	double MaximumVStartup[DC__NUM_DPP__MAX];
	double AlignedDCCMetaPitch[DC__NUM_DPP__MAX];
	double AlignedYPitch[DC__NUM_DPP__MAX];
	double AlignedCPitch[DC__NUM_DPP__MAX];
	double MaximumSwathWidth[DC__NUM_DPP__MAX];
	double final_flip_bw[DC__NUM_DPP__MAX];
	double ImmediateFlipSupportedForState[DC__VOLTAGE_STATES + 1];

	double WritebackLumaVExtra;
	double WritebackChromaVExtra;
	double WritebackRequiredDISPCLK;
	double MaximumSwathWidthSupport;
	double MaximumSwathWidthInDETBuffer;
	double MaximumSwathWidthInLineBuffer;
	double MaxDispclkRoundedDownToDFSGranularity;
	double MaxDppclkRoundedDownToDFSGranularity;
	double PlaneRequiredDISPCLKWithoutODMCombine;
	double PlaneRequiredDISPCLK;
	double TotalNumberOfActiveOTG;
	double FECOverhead;
	double EffectiveFECOverhead;
	unsigned int Outbpp;
	unsigned int OutbppDSC;
	double TotalDSCUnitsRequired;
	double bpp;
	unsigned int slices;
	double SwathWidthGranularityY;
	double RoundedUpMaxSwathSizeBytesY;
	double SwathWidthGranularityC;
	double RoundedUpMaxSwathSizeBytesC;
	double LinesInDETLuma;
	double LinesInDETChroma;
	double EffectiveDETLBLinesLuma;
	double EffectiveDETLBLinesChroma;
	double ProjectedDCFCLKDeepSleep;
	double PDEAndMetaPTEBytesPerFrameY;
	double PDEAndMetaPTEBytesPerFrameC;
	unsigned int MetaRowBytesY;
	unsigned int MetaRowBytesC;
	unsigned int DPTEBytesPerRowC;
	unsigned int DPTEBytesPerRowY;
	double ExtraLatency;
	double TimeCalc;
	double TWait;
	double MaximumReadBandwidthWithPrefetch;
	double total_dcn_read_bw_with_flip;
};

#endif /* _DML2_DISPLAY_MODE_VBA_H_ */
