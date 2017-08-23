/*
 * display_mode_vba.h
 *
 *  Created on: Aug 18, 2017
 *      Author: dlaktyus
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
	mode_evaluation_st me;

	unsigned int MaximumMaxVStartupLines;
	double cursor_bw[DC__NUM_PIPES__MAX];
	double meta_row_bw[DC__NUM_PIPES__MAX];
	double dpte_row_bw[DC__NUM_PIPES__MAX];
	double qual_row_bw[DC__NUM_PIPES__MAX];
	double WritebackDISPCLK;
	double PSCL_THROUGHPUT_LUMA[DC__NUM_PIPES__MAX];
	double PSCL_THROUGHPUT_CHROMA[DC__NUM_PIPES__MAX];
	double DPPCLKUsingSingleDPPLuma;
	double DPPCLKUsingSingleDPPChroma;
	double DPPCLKUsingSingleDPP[DC__NUM_PIPES__MAX];
	double DISPCLKWithRamping;
	double DISPCLKWithoutRamping;
	double GlobalDPPCLK;
	double MaxDispclk;
	double DISPCLKWithRampingRoundedToDFSGranularity;
	double DISPCLKWithoutRampingRoundedToDFSGranularity;
	double MaxDispclkRoundedToDFSGranularity;
	bool DCCEnabledAnyPlane;
	double ReturnBandwidthToDCN;
	unsigned int SwathWidthY[DC__NUM_PIPES__MAX];
	unsigned int SwathWidthSingleDPPY[DC__NUM_PIPES__MAX];
	double BytePerPixelDETY[DC__NUM_PIPES__MAX];
	double BytePerPixelDETC[DC__NUM_PIPES__MAX];
	double ReadBandwidthPlaneLuma[DC__NUM_PIPES__MAX];
	double ReadBandwidthPlaneChroma[DC__NUM_PIPES__MAX];
	unsigned int TotalActiveDPP;
	unsigned int TotalDCCActiveDPP;
	double UrgentRoundTripAndOutOfOrderLatency;
	double DisplayPipeLineDeliveryTimeLuma[DC__NUM_PIPES__MAX];                     // WM
	double DisplayPipeLineDeliveryTimeChroma[DC__NUM_PIPES__MAX];                     // WM
	double LinesInDETY[DC__NUM_PIPES__MAX];                     // WM
	double LinesInDETC[DC__NUM_PIPES__MAX];                     // WM
	unsigned int LinesInDETYRoundedDownToSwath[DC__NUM_PIPES__MAX];                     // WM
	unsigned int LinesInDETCRoundedDownToSwath[DC__NUM_PIPES__MAX];                     // WM
	double FullDETBufferingTimeY[DC__NUM_PIPES__MAX];                     // WM
	double FullDETBufferingTimeC[DC__NUM_PIPES__MAX];                     // WM
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
	double UrgentLatencySupportUs[DC__NUM_PIPES__MAX];
	unsigned int DSCFormatFactor;
	unsigned int BlockHeight256BytesY[DC__NUM_PIPES__MAX];
	unsigned int BlockHeight256BytesC[DC__NUM_PIPES__MAX];
	unsigned int BlockWidth256BytesY[DC__NUM_PIPES__MAX];
	unsigned int BlockWidth256BytesC[DC__NUM_PIPES__MAX];
	double VInitPreFillY[DC__NUM_PIPES__MAX];
	double VInitPreFillC[DC__NUM_PIPES__MAX];
	unsigned int MaxNumSwathY[DC__NUM_PIPES__MAX];
	unsigned int MaxNumSwathC[DC__NUM_PIPES__MAX];
	double PrefetchSourceLinesY[DC__NUM_PIPES__MAX];
	double PrefetchSourceLinesC[DC__NUM_PIPES__MAX];
	double PixelPTEBytesPerRow[DC__NUM_PIPES__MAX];
	double MetaRowByte[DC__NUM_PIPES__MAX];
	bool PTEBufferSizeNotExceeded; // not used
	unsigned int dpte_row_height[DC__NUM_PIPES__MAX];
	unsigned int dpte_row_height_chroma[DC__NUM_PIPES__MAX];
	unsigned int meta_row_height[DC__NUM_PIPES__MAX];
	unsigned int meta_row_height_chroma[DC__NUM_PIPES__MAX];

	unsigned int MacroTileWidthY;
	unsigned int MacroTileWidthC;
	unsigned int MaxVStartupLines[DC__NUM_PIPES__MAX];
	double WritebackDelay[DC__NUM_PIPES__MAX];
	bool PrefetchModeSupported;
	bool AllowDRAMClockChangeDuringVBlank[DC__NUM_PIPES__MAX];
	bool AllowDRAMSelfRefreshDuringVBlank[DC__NUM_PIPES__MAX];
	double RequiredPrefetchPixDataBW[DC__NUM_PIPES__MAX];
	double XFCRemoteSurfaceFlipDelay;
	double TInitXFill;
	double TslvChk;
	double SrcActiveDrainRate;
	double Tno_bw[DC__NUM_PIPES__MAX];
	bool ImmediateFlipSupported;

	double prefetch_vm_bw[DC__NUM_PIPES__MAX];
	double prefetch_row_bw[DC__NUM_PIPES__MAX];
	bool ImmediateFlipSupportedForPipe[DC__NUM_PIPES__MAX];
	unsigned int VStartupLines;
	double DisplayPipeLineDeliveryTimeLumaPrefetch[DC__NUM_PIPES__MAX];
	double DisplayPipeLineDeliveryTimeChromaPrefetch[DC__NUM_PIPES__MAX];
	unsigned int ActiveDPPs;
	unsigned int LBLatencyHidingSourceLinesY;
	unsigned int LBLatencyHidingSourceLinesC;
	double ActiveDRAMClockChangeLatencyMargin[DC__NUM_PIPES__MAX];
	double MinActiveDRAMClockChangeMargin;
	double XFCSlaveVUpdateOffset[DC__NUM_PIPES__MAX];
	double XFCSlaveVupdateWidth[DC__NUM_PIPES__MAX];
	double XFCSlaveVReadyOffset[DC__NUM_PIPES__MAX];
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
	unsigned int ViewportWidth[DC__NUM_DPP];
	unsigned int ViewportHeight[DC__NUM_DPP];
	unsigned int ViewportYStartY[DC__NUM_DPP];
	unsigned int ViewportYStartC[DC__NUM_DPP];
	unsigned int PitchY[DC__NUM_DPP];
	unsigned int PitchC[DC__NUM_DPP];
	double HRatio[DC__NUM_DPP];
	double VRatio[DC__NUM_DPP];
	unsigned int htaps[DC__NUM_DPP];
	unsigned int vtaps[DC__NUM_DPP];
	unsigned int HTAPsChroma[DC__NUM_DPP];
	unsigned int VTAPsChroma[DC__NUM_DPP];
	unsigned int HTotal[DC__NUM_DPP];
	unsigned int VTotal[DC__NUM_DPP];
	unsigned int DPPPerPlane[DC__NUM_DPP];
	double PixelClock[DC__NUM_DPP];
	double PixelClockBackEnd[DC__NUM_DPP];
	double DPPCLK[DC__NUM_DPP];
	bool DCCEnable[DC__NUM_DPP];
	unsigned int DCCMetaPitchY[DC__NUM_DPP];
	enum scan_direction_class SourceScan[DC__NUM_DPP];
	enum source_format_class SourcePixelFormat[DC__NUM_DPP];
	bool WritebackEnable[DC__NUM_DPP];
	double WritebackDestinationWidth[DC__NUM_DPP];
	double WritebackDestinationHeight[DC__NUM_DPP];
	double WritebackSourceHeight[DC__NUM_DPP];
	enum source_format_class WritebackPixelFormat[DC__NUM_DPP];
	unsigned int WritebackLumaHTaps[DC__NUM_DPP];
	unsigned int WritebackLumaVTaps[DC__NUM_DPP];
	unsigned int WritebackChromaHTaps[DC__NUM_DPP];
	unsigned int WritebackChromaVTaps[DC__NUM_DPP];
	double WritebackHRatio[DC__NUM_DPP];
	double WritebackVRatio[DC__NUM_DPP];
	unsigned int HActive[DC__NUM_DPP];
	unsigned int VActive[DC__NUM_DPP];
	bool Interlace[DC__NUM_DPP];
	enum dm_swizzle_mode SurfaceTiling[DC__NUM_DPP];
	unsigned int ScalerRecoutWidth[DC__NUM_DPP];
	bool DynamicMetadataEnable[DC__NUM_DPP];
	unsigned int DynamicMetadataLinesBeforeActiveRequired[DC__NUM_DPP];
	unsigned int DynamicMetadataTransmittedBytes[DC__NUM_DPP];
	double DCCRate[DC__NUM_DPP];
	bool ODMCombineEnabled[DC__NUM_DPP];
	double OutputBpp[DC__NUM_DPP];
	unsigned int NumberOfDSCSlices[DC__NUM_DPP];
	bool DSCEnabled[DC__NUM_DPP];
	unsigned int DSCDelay[DC__NUM_DPP];
	unsigned int DSCInputBitPerComponent[DC__NUM_DPP];
	enum output_format_class OutputFormat[DC__NUM_DPP];
	enum output_encoder_class Output[DC__NUM_DPP];
	unsigned int BlendingAndTiming[DC__NUM_DPP];
	bool SynchronizedVBlank;
	unsigned int NumberOfCursors[DC__NUM_DPP];
	unsigned int CursorWidth[DC__NUM_DPP][DC__NUM_CURSOR];
	unsigned int CursorBPP[DC__NUM_DPP][DC__NUM_CURSOR];
	bool XFCEnabled[DC__NUM_DPP];
	bool ScalerEnabled[DC__NUM_DPP];

	// Intermediates/Informational
	bool ImmediateFlipSupport;
	unsigned int SwathHeightY[DC__NUM_DPP];
	unsigned int SwathHeightC[DC__NUM_DPP];
	unsigned int DETBufferSizeY[DC__NUM_DPP];
	unsigned int DETBufferSizeC[DC__NUM_DPP];
	unsigned int LBBitPerPixel[DC__NUM_DPP];
	double LastPixelOfLineExtraWatermark;
	double TotalDataReadBandwidth;
	unsigned int TotalActiveWriteback;
	unsigned int EffectiveLBLatencyHidingSourceLinesLuma;
	unsigned int EffectiveLBLatencyHidingSourceLinesChroma;
	double BandwidthAvailableForImmediateFlip;
	unsigned int PrefetchMode;
	bool IgnoreViewportPositioning;
	double PrefetchBandwidth[DC__NUM_DPP];
	bool ErrorResult[DC__NUM_DPP];
	double PDEAndMetaPTEBytesFrame[DC__NUM_DPP];

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
	double DSCCLK_calculated[DC__NUM_DPP];
	double DPPCLK_calculated[DC__NUM_DPP];

	unsigned int VStartup[DC__NUM_DPP];
	unsigned int VStartupRequiredWhenNotEnoughTimeForDynamicMetadata;

	double ImmediateFlipBW;
	unsigned int TotImmediateFlipBytes;
	double TCalc;
	double MinTTUVBlank[DC__NUM_DPP];
	double VRatioPrefetchY[DC__NUM_DPP];
	double VRatioPrefetchC[DC__NUM_DPP];
	double DSTXAfterScaler[DC__NUM_DPP];
	double DSTYAfterScaler[DC__NUM_DPP];

	double DestinationLinesToRequestVMInVBlank[DC__NUM_DPP];
	double DestinationLinesToRequestRowInVBlank[DC__NUM_DPP];
	double DestinationLinesForPrefetch[DC__NUM_DPP];
	double DestinationLinesToRequestRowInImmediateFlip[DC__NUM_DPP];
	double DestinationLinesToRequestVMInImmediateFlip[DC__NUM_DPP];

	double XFCTransferDelay[DC__NUM_DPP];
	double XFCPrechargeDelay[DC__NUM_DPP];
	double XFCRemoteSurfaceFlipLatency[DC__NUM_DPP];
	double XFCPrefetchMargin[DC__NUM_DPP];

	display_e2e_pipe_params_st cache_pipes[DC__NUM_DPP];
	unsigned int cache_num_pipes;
	unsigned int pipe_plane[DC__NUM_PIPES__MAX];
};

#endif /* _DML2_DISPLAY_MODE_VBA_H_ */
