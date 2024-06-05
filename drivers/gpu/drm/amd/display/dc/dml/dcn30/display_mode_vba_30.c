/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#include "dc.h"
#include "../display_mode_lib.h"
#include "display_mode_vba_30.h"
#include "../dml_inline_defs.h"


/*
 * NOTE:
 *   This file is gcc-parsable HW gospel, coming straight from HW engineers.
 *
 * It doesn't adhere to Linux kernel style and sometimes will do things in odd
 * ways. Unless there is something clearly wrong with it the code should
 * remain as-is as it provides us with a guarantee from HW that it is correct.
 */


typedef struct {
	double DPPCLK;
	double DISPCLK;
	double PixelClock;
	double DCFCLKDeepSleep;
	unsigned int DPPPerPlane;
	bool ScalerEnabled;
	enum scan_direction_class SourceScan;
	unsigned int BlockWidth256BytesY;
	unsigned int BlockHeight256BytesY;
	unsigned int BlockWidth256BytesC;
	unsigned int BlockHeight256BytesC;
	unsigned int InterlaceEnable;
	unsigned int NumberOfCursors;
	unsigned int VBlank;
	unsigned int HTotal;
	unsigned int DCCEnable;
	bool ODMCombineEnabled;
} Pipe;

#define BPP_INVALID 0
#define BPP_BLENDED_PIPE 0xffffffff
#define DCN30_MAX_DSC_IMAGE_WIDTH 5184
#define DCN30_MAX_FMT_420_BUFFER_WIDTH 4096

static void DisplayPipeConfiguration(struct display_mode_lib *mode_lib);
static void DISPCLKDPPCLKDCFCLKDeepSleepPrefetchParametersWatermarksAndPerformanceCalculation(
		struct display_mode_lib *mode_lib);
static unsigned int dscceComputeDelay(
		unsigned int bpc,
		double BPP,
		unsigned int sliceWidth,
		unsigned int numSlices,
		enum output_format_class pixelFormat,
		enum output_encoder_class Output);
static unsigned int dscComputeDelay(
		enum output_format_class pixelFormat,
		enum output_encoder_class Output);
// Super monster function with some 45 argument
static bool CalculatePrefetchSchedule(
		struct display_mode_lib *mode_lib,
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
		Pipe *myPipe,
		unsigned int DSCDelay,
		double DPPCLKDelaySubtotalPlusCNVCFormater,
		double DPPCLKDelaySCL,
		double DPPCLKDelaySCLLBOnly,
		double DPPCLKDelayCNVCCursor,
		double DISPCLKDelaySubtotal,
		unsigned int DPP_RECOUT_WIDTH,
		enum output_format_class OutputFormat,
		unsigned int MaxInterDCNTileRepeaters,
		unsigned int VStartup,
		unsigned int MaxVStartup,
		unsigned int GPUVMPageTableLevels,
		bool GPUVMEnable,
		bool HostVMEnable,
		unsigned int HostVMMaxNonCachedPageTableLevels,
		double HostVMMinPageSize,
		bool DynamicMetadataEnable,
		bool DynamicMetadataVMEnabled,
		int DynamicMetadataLinesBeforeActiveRequired,
		unsigned int DynamicMetadataTransmittedBytes,
		double UrgentLatency,
		double UrgentExtraLatency,
		double TCalc,
		unsigned int PDEAndMetaPTEBytesFrame,
		unsigned int MetaRowByte,
		unsigned int PixelPTEBytesPerRow,
		double PrefetchSourceLinesY,
		unsigned int SwathWidthY,
		int BytePerPixelY,
		double VInitPreFillY,
		unsigned int MaxNumSwathY,
		double PrefetchSourceLinesC,
		unsigned int SwathWidthC,
		int BytePerPixelC,
		double VInitPreFillC,
		unsigned int MaxNumSwathC,
		long swath_width_luma_ub,
		long swath_width_chroma_ub,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		double TWait,
		bool ProgressiveToInterlaceUnitInOPP,
		double *DSTXAfterScaler,
		double *DSTYAfterScaler,
		double *DestinationLinesForPrefetch,
		double *PrefetchBandwidth,
		double *DestinationLinesToRequestVMInVBlank,
		double *DestinationLinesToRequestRowInVBlank,
		double *VRatioPrefetchY,
		double *VRatioPrefetchC,
		double *RequiredPrefetchPixDataBWLuma,
		double *RequiredPrefetchPixDataBWChroma,
		bool *NotEnoughTimeForDynamicMetadata,
		double *Tno_bw,
		double *prefetch_vmrow_bw,
		double *Tdmdl_vm,
		double *Tdmdl,
		unsigned int *VUpdateOffsetPix,
		double *VUpdateWidthPix,
		double *VReadyOffsetPix);
static double RoundToDFSGranularityUp(double Clock, double VCOSpeed);
static double RoundToDFSGranularityDown(double Clock, double VCOSpeed);
static void CalculateDCCConfiguration(
		bool DCCEnabled,
		bool DCCProgrammingAssumesScanDirectionUnknown,
		enum source_format_class SourcePixelFormat,
		unsigned int ViewportWidthLuma,
		unsigned int ViewportWidthChroma,
		unsigned int ViewportHeightLuma,
		unsigned int ViewportHeightChroma,
		double DETBufferSize,
		unsigned int RequestHeight256ByteLuma,
		unsigned int RequestHeight256ByteChroma,
		enum dm_swizzle_mode TilingFormat,
		unsigned int BytePerPixelY,
		unsigned int BytePerPixelC,
		double BytePerPixelDETY,
		double BytePerPixelDETC,
		enum scan_direction_class ScanOrientation,
		unsigned int *MaxUncompressedBlockLuma,
		unsigned int *MaxUncompressedBlockChroma,
		unsigned int *MaxCompressedBlockLuma,
		unsigned int *MaxCompressedBlockChroma,
		unsigned int *IndependentBlockLuma,
		unsigned int *IndependentBlockChroma);
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
		unsigned int SwathWidth,
		unsigned int ViewportHeight,
		bool GPUVMEnable,
		bool HostVMEnable,
		unsigned int HostVMMaxNonCachedPageTableLevels,
		unsigned int GPUVMMinPageSize,
		unsigned int HostVMMinPageSize,
		unsigned int PTEBufferSizeInRequests,
		unsigned int Pitch,
		unsigned int DCCMetaPitch,
		unsigned int *MacroTileWidth,
		unsigned int *MetaRowByte,
		unsigned int *PixelPTEBytesPerRow,
		bool *PTEBufferSizeNotExceeded,
		unsigned int *dpte_row_width_ub,
		unsigned int *dpte_row_height,
		unsigned int *MetaRequestWidth,
		unsigned int *MetaRequestHeight,
		unsigned int *meta_row_width,
		unsigned int *meta_row_height,
		unsigned int *vm_group_bytes,
		unsigned int *dpte_group_bytes,
		unsigned int *PixelPTEReqWidth,
		unsigned int *PixelPTEReqHeight,
		unsigned int *PTERequestSize,
		unsigned int *DPDE0BytesFrame,
		unsigned int *MetaPTEBytesFrame);
static double CalculateTWait(
		unsigned int PrefetchMode,
		double DRAMClockChangeLatency,
		double UrgentLatency,
		double SREnterPlusExitTime);
static void CalculateRowBandwidth(
		bool GPUVMEnable,
		enum source_format_class SourcePixelFormat,
		double VRatio,
		double VRatioChroma,
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
		double *dpte_row_bw);
static void CalculateFlipSchedule(
		struct display_mode_lib *mode_lib,
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
		double UrgentExtraLatency,
		double UrgentLatency,
		unsigned int GPUVMMaxPageTableLevels,
		bool HostVMEnable,
		unsigned int HostVMMaxNonCachedPageTableLevels,
		bool GPUVMEnable,
		double HostVMMinPageSize,
		double PDEAndMetaPTEBytesPerFrame,
		double MetaRowBytes,
		double DPTEBytesPerRow,
		double BandwidthAvailableForImmediateFlip,
		unsigned int TotImmediateFlipBytes,
		enum source_format_class SourcePixelFormat,
		double LineTime,
		double VRatio,
		double VRatioChroma,
		double Tno_bw,
		bool DCCEnable,
		unsigned int dpte_row_height,
		unsigned int meta_row_height,
		unsigned int dpte_row_height_chroma,
		unsigned int meta_row_height_chroma,
		double *DestinationLinesToRequestVMInImmediateFlip,
		double *DestinationLinesToRequestRowInImmediateFlip,
		double *final_flip_bw,
		bool *ImmediateFlipSupportedForPipe);
static double CalculateWriteBackDelay(
		enum source_format_class WritebackPixelFormat,
		double WritebackHRatio,
		double WritebackVRatio,
		unsigned int WritebackVTaps,
		long WritebackDestinationWidth,
		long WritebackDestinationHeight,
		long WritebackSourceHeight,
		unsigned int HTotal);
static void CalculateDynamicMetadataParameters(
		int MaxInterDCNTileRepeaters,
		double DPPCLK,
		double DISPCLK,
		double DCFClkDeepSleep,
		double PixelClock,
		long HTotal,
		long VBlank,
		long DynamicMetadataTransmittedBytes,
		long DynamicMetadataLinesBeforeActiveRequired,
		int InterlaceEnable,
		bool ProgressiveToInterlaceUnitInOPP,
		double *Tsetup,
		double *Tdmbf,
		double *Tdmec,
		double *Tdmsks);
static void CalculateWatermarksAndDRAMSpeedChangeSupport(
		struct display_mode_lib *mode_lib,
		unsigned int PrefetchMode,
		unsigned int NumberOfActivePlanes,
		unsigned int MaxLineBufferLines,
		unsigned int LineBufferSize,
		unsigned int DPPOutputBufferPixels,
		unsigned int DETBufferSizeInKByte,
		unsigned int WritebackInterfaceBufferSize,
		double DCFCLK,
		double ReturnBW,
		bool GPUVMEnable,
		unsigned int dpte_group_bytes[],
		unsigned int MetaChunkSize,
		double UrgentLatency,
		double ExtraLatency,
		double WritebackLatency,
		double WritebackChunkSize,
		double SOCCLK,
		double DRAMClockChangeLatency,
		double SRExitTime,
		double SREnterPlusExitTime,
		double DCFCLKDeepSleep,
		unsigned int DPPPerPlane[],
		bool DCCEnable[],
		double DPPCLK[],
		unsigned int DETBufferSizeY[],
		unsigned int DETBufferSizeC[],
		unsigned int SwathHeightY[],
		unsigned int SwathHeightC[],
		unsigned int LBBitPerPixel[],
		double SwathWidthY[],
		double SwathWidthC[],
		double HRatio[],
		double HRatioChroma[],
		unsigned int vtaps[],
		unsigned int VTAPsChroma[],
		double VRatio[],
		double VRatioChroma[],
		unsigned int HTotal[],
		double PixelClock[],
		unsigned int BlendingAndTiming[],
		double BytePerPixelDETY[],
		double BytePerPixelDETC[],
		double DSTXAfterScaler[],
		double DSTYAfterScaler[],
		bool WritebackEnable[],
		enum source_format_class WritebackPixelFormat[],
		double WritebackDestinationWidth[],
		double WritebackDestinationHeight[],
		double WritebackSourceHeight[],
		enum clock_change_support *DRAMClockChangeSupport,
		double *UrgentWatermark,
		double *WritebackUrgentWatermark,
		double *DRAMClockChangeWatermark,
		double *WritebackDRAMClockChangeWatermark,
		double *StutterExitWatermark,
		double *StutterEnterPlusExitWatermark,
		double *MinActiveDRAMClockChangeLatencySupported);
static void CalculateDCFCLKDeepSleep(
		struct display_mode_lib *mode_lib,
		unsigned int NumberOfActivePlanes,
		int BytePerPixelY[],
		int BytePerPixelC[],
		double VRatio[],
		double VRatioChroma[],
		double SwathWidthY[],
		double SwathWidthC[],
		unsigned int DPPPerPlane[],
		double HRatio[],
		double HRatioChroma[],
		double PixelClock[],
		double PSCL_THROUGHPUT[],
		double PSCL_THROUGHPUT_CHROMA[],
		double DPPCLK[],
		double ReadBandwidthLuma[],
		double ReadBandwidthChroma[],
		int ReturnBusWidth,
		double *DCFCLKDeepSleep);
static void CalculateUrgentBurstFactor(
		long swath_width_luma_ub,
		long swath_width_chroma_ub,
		unsigned int DETBufferSizeInKByte,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		double LineTime,
		double UrgentLatency,
		double CursorBufferSize,
		unsigned int CursorWidth,
		unsigned int CursorBPP,
		double VRatio,
		double VRatioC,
		double BytePerPixelInDETY,
		double BytePerPixelInDETC,
		double DETBufferSizeY,
		double DETBufferSizeC,
		double *UrgentBurstFactorCursor,
		double *UrgentBurstFactorLuma,
		double *UrgentBurstFactorChroma,
		bool *NotEnoughUrgentLatencyHiding);

static void UseMinimumDCFCLK(
		struct display_mode_lib *mode_lib,
		struct vba_vars_st *v,
		int MaxPrefetchMode,
		int ReorderingBytes);

static void CalculatePixelDeliveryTimes(
		unsigned int NumberOfActivePlanes,
		double VRatio[],
		double VRatioChroma[],
		double VRatioPrefetchY[],
		double VRatioPrefetchC[],
		unsigned int swath_width_luma_ub[],
		unsigned int swath_width_chroma_ub[],
		unsigned int DPPPerPlane[],
		double HRatio[],
		double HRatioChroma[],
		double PixelClock[],
		double PSCL_THROUGHPUT[],
		double PSCL_THROUGHPUT_CHROMA[],
		double DPPCLK[],
		int BytePerPixelC[],
		enum scan_direction_class SourceScan[],
		unsigned int NumberOfCursors[],
		unsigned int CursorWidth[][2],
		unsigned int CursorBPP[][2],
		unsigned int BlockWidth256BytesY[],
		unsigned int BlockHeight256BytesY[],
		unsigned int BlockWidth256BytesC[],
		unsigned int BlockHeight256BytesC[],
		double DisplayPipeLineDeliveryTimeLuma[],
		double DisplayPipeLineDeliveryTimeChroma[],
		double DisplayPipeLineDeliveryTimeLumaPrefetch[],
		double DisplayPipeLineDeliveryTimeChromaPrefetch[],
		double DisplayPipeRequestDeliveryTimeLuma[],
		double DisplayPipeRequestDeliveryTimeChroma[],
		double DisplayPipeRequestDeliveryTimeLumaPrefetch[],
		double DisplayPipeRequestDeliveryTimeChromaPrefetch[],
		double CursorRequestDeliveryTime[],
		double CursorRequestDeliveryTimePrefetch[]);

static void CalculateMetaAndPTETimes(
		int NumberOfActivePlanes,
		bool GPUVMEnable,
		int MetaChunkSize,
		int MinMetaChunkSizeBytes,
		int HTotal[],
		double VRatio[],
		double VRatioChroma[],
		double DestinationLinesToRequestRowInVBlank[],
		double DestinationLinesToRequestRowInImmediateFlip[],
		bool DCCEnable[],
		double PixelClock[],
		int BytePerPixelY[],
		int BytePerPixelC[],
		enum scan_direction_class SourceScan[],
		int dpte_row_height[],
		int dpte_row_height_chroma[],
		int meta_row_width[],
		int meta_row_width_chroma[],
		int meta_row_height[],
		int meta_row_height_chroma[],
		int meta_req_width[],
		int meta_req_width_chroma[],
		int meta_req_height[],
		int meta_req_height_chroma[],
		int dpte_group_bytes[],
		int PTERequestSizeY[],
		int PTERequestSizeC[],
		int PixelPTEReqWidthY[],
		int PixelPTEReqHeightY[],
		int PixelPTEReqWidthC[],
		int PixelPTEReqHeightC[],
		int dpte_row_width_luma_ub[],
		int dpte_row_width_chroma_ub[],
		double DST_Y_PER_PTE_ROW_NOM_L[],
		double DST_Y_PER_PTE_ROW_NOM_C[],
		double DST_Y_PER_META_ROW_NOM_L[],
		double DST_Y_PER_META_ROW_NOM_C[],
		double TimePerMetaChunkNominal[],
		double TimePerChromaMetaChunkNominal[],
		double TimePerMetaChunkVBlank[],
		double TimePerChromaMetaChunkVBlank[],
		double TimePerMetaChunkFlip[],
		double TimePerChromaMetaChunkFlip[],
		double time_per_pte_group_nom_luma[],
		double time_per_pte_group_vblank_luma[],
		double time_per_pte_group_flip_luma[],
		double time_per_pte_group_nom_chroma[],
		double time_per_pte_group_vblank_chroma[],
		double time_per_pte_group_flip_chroma[]);

static void CalculateVMGroupAndRequestTimes(
		unsigned int NumberOfActivePlanes,
		bool GPUVMEnable,
		unsigned int GPUVMMaxPageTableLevels,
		unsigned int HTotal[],
		int BytePerPixelC[],
		double DestinationLinesToRequestVMInVBlank[],
		double DestinationLinesToRequestVMInImmediateFlip[],
		bool DCCEnable[],
		double PixelClock[],
		int dpte_row_width_luma_ub[],
		int dpte_row_width_chroma_ub[],
		int vm_group_bytes[],
		unsigned int dpde0_bytes_per_frame_ub_l[],
		unsigned int dpde0_bytes_per_frame_ub_c[],
		int meta_pte_bytes_per_frame_ub_l[],
		int meta_pte_bytes_per_frame_ub_c[],
		double TimePerVMGroupVBlank[],
		double TimePerVMGroupFlip[],
		double TimePerVMRequestVBlank[],
		double TimePerVMRequestFlip[]);

static void CalculateStutterEfficiency(
		int NumberOfActivePlanes,
		long ROBBufferSizeInKByte,
		double TotalDataReadBandwidth,
		double DCFCLK,
		double ReturnBW,
		double SRExitTime,
		bool SynchronizedVBlank,
		int DPPPerPlane[],
		unsigned int DETBufferSizeY[],
		int BytePerPixelY[],
		double BytePerPixelDETY[],
		double SwathWidthY[],
		int SwathHeightY[],
		int SwathHeightC[],
		double DCCRateLuma[],
		double DCCRateChroma[],
		int HTotal[],
		int VTotal[],
		double PixelClock[],
		double VRatio[],
		enum scan_direction_class SourceScan[],
		int BlockHeight256BytesY[],
		int BlockWidth256BytesY[],
		int BlockHeight256BytesC[],
		int BlockWidth256BytesC[],
		int DCCYMaxUncompressedBlock[],
		int DCCCMaxUncompressedBlock[],
		int VActive[],
		bool DCCEnable[],
		bool WritebackEnable[],
		double ReadBandwidthPlaneLuma[],
		double ReadBandwidthPlaneChroma[],
		double meta_row_bw[],
		double dpte_row_bw[],
		double *StutterEfficiencyNotIncludingVBlank,
		double *StutterEfficiency,
		double *StutterPeriodOut);

static void CalculateSwathAndDETConfiguration(
		bool ForceSingleDPP,
		int NumberOfActivePlanes,
		unsigned int DETBufferSizeInKByte,
		double MaximumSwathWidthLuma[],
		double MaximumSwathWidthChroma[],
		enum scan_direction_class SourceScan[],
		enum source_format_class SourcePixelFormat[],
		enum dm_swizzle_mode SurfaceTiling[],
		int ViewportWidth[],
		int ViewportHeight[],
		int SurfaceWidthY[],
		int SurfaceWidthC[],
		int SurfaceHeightY[],
		int SurfaceHeightC[],
		int Read256BytesBlockHeightY[],
		int Read256BytesBlockHeightC[],
		int Read256BytesBlockWidthY[],
		int Read256BytesBlockWidthC[],
		enum odm_combine_mode ODMCombineEnabled[],
		int BlendingAndTiming[],
		int BytePerPixY[],
		int BytePerPixC[],
		double BytePerPixDETY[],
		double BytePerPixDETC[],
		int HActive[],
		double HRatio[],
		double HRatioChroma[],
		int DPPPerPlane[],
		int swath_width_luma_ub[],
		int swath_width_chroma_ub[],
		double SwathWidth[],
		double SwathWidthChroma[],
		int SwathHeightY[],
		int SwathHeightC[],
		unsigned int DETBufferSizeY[],
		unsigned int DETBufferSizeC[],
		bool ViewportSizeSupportPerPlane[],
		bool *ViewportSizeSupport);
static void CalculateSwathWidth(
		bool ForceSingleDPP,
		int NumberOfActivePlanes,
		enum source_format_class SourcePixelFormat[],
		enum scan_direction_class SourceScan[],
		unsigned int ViewportWidth[],
		unsigned int ViewportHeight[],
		unsigned int SurfaceWidthY[],
		unsigned int SurfaceWidthC[],
		unsigned int SurfaceHeightY[],
		unsigned int SurfaceHeightC[],
		enum odm_combine_mode ODMCombineEnabled[],
		int BytePerPixY[],
		int BytePerPixC[],
		int Read256BytesBlockHeightY[],
		int Read256BytesBlockHeightC[],
		int Read256BytesBlockWidthY[],
		int Read256BytesBlockWidthC[],
		int BlendingAndTiming[],
		unsigned int HActive[],
		double HRatio[],
		int DPPPerPlane[],
		double SwathWidthSingleDPPY[],
		double SwathWidthSingleDPPC[],
		double SwathWidthY[],
		double SwathWidthC[],
		int MaximumSwathHeightY[],
		int MaximumSwathHeightC[],
		unsigned int swath_width_luma_ub[],
		unsigned int swath_width_chroma_ub[]);
static double CalculateExtraLatency(
		long RoundTripPingLatencyCycles,
		long ReorderingBytes,
		double DCFCLK,
		int TotalNumberOfActiveDPP,
		int PixelChunkSizeInKByte,
		int TotalNumberOfDCCActiveDPP,
		int MetaChunkSize,
		double ReturnBW,
		bool GPUVMEnable,
		bool HostVMEnable,
		int NumberOfActivePlanes,
		int NumberOfDPP[],
		int dpte_group_bytes[],
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
		double HostVMMinPageSize,
		int HostVMMaxNonCachedPageTableLevels);
static double CalculateExtraLatencyBytes(
		long ReorderingBytes,
		int TotalNumberOfActiveDPP,
		int PixelChunkSizeInKByte,
		int TotalNumberOfDCCActiveDPP,
		int MetaChunkSize,
		bool GPUVMEnable,
		bool HostVMEnable,
		int NumberOfActivePlanes,
		int NumberOfDPP[],
		int dpte_group_bytes[],
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
		double HostVMMinPageSize,
		int HostVMMaxNonCachedPageTableLevels);
static double CalculateUrgentLatency(
		double UrgentLatencyPixelDataOnly,
		double UrgentLatencyPixelMixedWithVMData,
		double UrgentLatencyVMDataOnly,
		bool DoUrgentLatencyAdjustment,
		double UrgentLatencyAdjustmentFabricClockComponent,
		double UrgentLatencyAdjustmentFabricClockReference,
		double FabricClockSingle);

void dml30_recalculate(struct display_mode_lib *mode_lib)
{
	ModeSupportAndSystemConfiguration(mode_lib);
	PixelClockAdjustmentForProgressiveToInterlaceUnit(mode_lib);
	DisplayPipeConfiguration(mode_lib);
	DISPCLKDPPCLKDCFCLKDeepSleepPrefetchParametersWatermarksAndPerformanceCalculation(mode_lib);
}

static unsigned int dscceComputeDelay(
		unsigned int bpc,
		double BPP,
		unsigned int sliceWidth,
		unsigned int numSlices,
		enum output_format_class pixelFormat,
		enum output_encoder_class Output)
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
	unsigned int pixelsPerClock, lstall, D, initalXmitDelay, w, s, ix, wx, P, l0, a, ax, L,
			Delay, pixels;

	if (pixelFormat == dm_420)
		pixelsPerClock = 2;
	// #all other modes operate at 1 pixel per clock
	else if (pixelFormat == dm_444)
		pixelsPerClock = 1;
	else if (pixelFormat == dm_n422)
		pixelsPerClock = 2;
	else
		pixelsPerClock = 1;

	//initial transmit delay as per PPS
	initalXmitDelay = dml_round(rcModelSize / 2.0 / BPP / pixelsPerClock);

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
	if (pixelFormat == dm_420 || pixelFormat == dm_444 || pixelFormat == dm_n422)
		s = 0;
	else
		s = 1;

	//main calculation for the dscce
	ix = initalXmitDelay + 45;
	wx = (w + 2) / 3;
	P = 3 * wx - w;
	l0 = ix / w;
	a = ix + P * l0;
	ax = (a + 2) / 3 + D + 6 + 1;
	L = (ax + wx - 1) / wx;
	if ((ix % w) == 0 && P != 0)
		lstall = 1;
	else
		lstall = 0;
	Delay = L * wx * (numSlices - 1) + ax + s + lstall + 22;

	//dsc processes 3 pixel containers per cycle and a container can contain 1 or 2 pixels
	pixels = Delay * 3 * pixelsPerClock;
	return pixels;
}

static unsigned int dscComputeDelay(enum output_format_class pixelFormat, enum output_encoder_class Output)
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
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
		Pipe *myPipe,
		unsigned int DSCDelay,
		double DPPCLKDelaySubtotalPlusCNVCFormater,
		double DPPCLKDelaySCL,
		double DPPCLKDelaySCLLBOnly,
		double DPPCLKDelayCNVCCursor,
		double DISPCLKDelaySubtotal,
		unsigned int DPP_RECOUT_WIDTH,
		enum output_format_class OutputFormat,
		unsigned int MaxInterDCNTileRepeaters,
		unsigned int VStartup,
		unsigned int MaxVStartup,
		unsigned int GPUVMPageTableLevels,
		bool GPUVMEnable,
		bool HostVMEnable,
		unsigned int HostVMMaxNonCachedPageTableLevels,
		double HostVMMinPageSize,
		bool DynamicMetadataEnable,
		bool DynamicMetadataVMEnabled,
		int DynamicMetadataLinesBeforeActiveRequired,
		unsigned int DynamicMetadataTransmittedBytes,
		double UrgentLatency,
		double UrgentExtraLatency,
		double TCalc,
		unsigned int PDEAndMetaPTEBytesFrame,
		unsigned int MetaRowByte,
		unsigned int PixelPTEBytesPerRow,
		double PrefetchSourceLinesY,
		unsigned int SwathWidthY,
		int BytePerPixelY,
		double VInitPreFillY,
		unsigned int MaxNumSwathY,
		double PrefetchSourceLinesC,
		unsigned int SwathWidthC,
		int BytePerPixelC,
		double VInitPreFillC,
		unsigned int MaxNumSwathC,
		long swath_width_luma_ub,
		long swath_width_chroma_ub,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		double TWait,
		bool ProgressiveToInterlaceUnitInOPP,
		double *DSTXAfterScaler,
		double *DSTYAfterScaler,
		double *DestinationLinesForPrefetch,
		double *PrefetchBandwidth,
		double *DestinationLinesToRequestVMInVBlank,
		double *DestinationLinesToRequestRowInVBlank,
		double *VRatioPrefetchY,
		double *VRatioPrefetchC,
		double *RequiredPrefetchPixDataBWLuma,
		double *RequiredPrefetchPixDataBWChroma,
		bool *NotEnoughTimeForDynamicMetadata,
		double *Tno_bw,
		double *prefetch_vmrow_bw,
		double *Tdmdl_vm,
		double *Tdmdl,
		unsigned int *VUpdateOffsetPix,
		double *VUpdateWidthPix,
		double *VReadyOffsetPix)
{
	bool MyError = false;
	unsigned int DPPCycles = 0, DISPCLKCycles = 0;
	double DSTTotalPixelsAfterScaler = 0;
	double LineTime = 0, Tsetup = 0;
	double dst_y_prefetch_equ = 0;
	double Tsw_oto = 0;
	double prefetch_bw_oto = 0;
	double Tvm_oto = 0;
	double Tr0_oto = 0;
	double Tvm_oto_lines = 0;
	double Tr0_oto_lines = 0;
	double dst_y_prefetch_oto = 0;
	double TimeForFetchingMetaPTE = 0;
	double TimeForFetchingRowInVBlank = 0;
	double LinesToRequestPrefetchPixelData = 0;
	double HostVMInefficiencyFactor = 0;
	unsigned int HostVMDynamicLevelsTrips = 0;
	double trip_to_mem = 0;
	double Tvm_trips = 0;
	double Tr0_trips = 0;
	double Tvm_trips_rounded = 0;
	double Tr0_trips_rounded = 0;
	double Lsw_oto = 0;
	double Tpre_rounded = 0;
	double prefetch_bw_equ = 0;
	double Tvm_equ = 0;
	double Tr0_equ = 0;
	double Tdmbf = 0;
	double Tdmec = 0;
	double Tdmsks = 0;

	if (GPUVMEnable == true && HostVMEnable == true) {
		HostVMInefficiencyFactor = PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData / PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly;
		HostVMDynamicLevelsTrips = HostVMMaxNonCachedPageTableLevels;
	} else {
		HostVMInefficiencyFactor = 1;
		HostVMDynamicLevelsTrips = 0;
	}

	CalculateDynamicMetadataParameters(
			MaxInterDCNTileRepeaters,
			myPipe->DPPCLK,
			myPipe->DISPCLK,
			myPipe->DCFCLKDeepSleep,
			myPipe->PixelClock,
			myPipe->HTotal,
			myPipe->VBlank,
			DynamicMetadataTransmittedBytes,
			DynamicMetadataLinesBeforeActiveRequired,
			myPipe->InterlaceEnable,
			ProgressiveToInterlaceUnitInOPP,
			&Tsetup,
			&Tdmbf,
			&Tdmec,
			&Tdmsks);

	LineTime = myPipe->HTotal / myPipe->PixelClock;
	trip_to_mem = UrgentLatency;
	Tvm_trips = UrgentExtraLatency + trip_to_mem * (GPUVMPageTableLevels * (HostVMDynamicLevelsTrips + 1) - 1);

	if (DynamicMetadataVMEnabled == true && GPUVMEnable == true) {
		*Tdmdl = TWait + Tvm_trips + trip_to_mem;
	} else {
		*Tdmdl = TWait + UrgentExtraLatency;
	}

	if (DynamicMetadataEnable == true) {
		if (VStartup * LineTime < Tsetup + *Tdmdl + Tdmbf + Tdmec + Tdmsks) {
			*NotEnoughTimeForDynamicMetadata = true;
		} else {
			*NotEnoughTimeForDynamicMetadata = false;
			dml_print("DML: Not Enough Time for Dynamic Meta!\n");
			dml_print("DML: Tdmbf: %fus - time for dmd transfer from dchub to dio output buffer\n", Tdmbf);
			dml_print("DML: Tdmec: %fus - time dio takes to transfer dmd\n", Tdmec);
			dml_print("DML: Tdmsks: %fus - time before active dmd must complete transmission at dio\n", Tdmsks);
			dml_print("DML: Tdmdl: %fus - time for fabric to become ready and fetch dmd \n", *Tdmdl);
		}
	} else {
		*NotEnoughTimeForDynamicMetadata = false;
	}

	*Tdmdl_vm = (DynamicMetadataEnable == true && DynamicMetadataVMEnabled == true && GPUVMEnable == true ? TWait + Tvm_trips : 0);

	if (myPipe->ScalerEnabled)
		DPPCycles = DPPCLKDelaySubtotalPlusCNVCFormater + DPPCLKDelaySCL;
	else
		DPPCycles = DPPCLKDelaySubtotalPlusCNVCFormater + DPPCLKDelaySCLLBOnly;

	DPPCycles = DPPCycles + myPipe->NumberOfCursors * DPPCLKDelayCNVCCursor;

	DISPCLKCycles = DISPCLKDelaySubtotal;

	if (myPipe->DPPCLK == 0.0 || myPipe->DISPCLK == 0.0)
		return true;

	*DSTXAfterScaler = DPPCycles * myPipe->PixelClock / myPipe->DPPCLK + DISPCLKCycles * myPipe->PixelClock / myPipe->DISPCLK
			+ DSCDelay;

	*DSTXAfterScaler = *DSTXAfterScaler + ((myPipe->ODMCombineEnabled)?18:0) + (myPipe->DPPPerPlane - 1) * DPP_RECOUT_WIDTH;

	if (OutputFormat == dm_420 || (myPipe->InterlaceEnable && ProgressiveToInterlaceUnitInOPP))
		*DSTYAfterScaler = 1;
	else
		*DSTYAfterScaler = 0;

	DSTTotalPixelsAfterScaler = *DSTYAfterScaler * myPipe->HTotal + *DSTXAfterScaler;
	*DSTYAfterScaler = dml_floor(DSTTotalPixelsAfterScaler / myPipe->HTotal, 1);
	*DSTXAfterScaler = DSTTotalPixelsAfterScaler - ((double) (*DSTYAfterScaler * myPipe->HTotal));

	MyError = false;


	Tr0_trips = trip_to_mem * (HostVMDynamicLevelsTrips + 1);
	Tvm_trips_rounded = dml_ceil(4.0 * Tvm_trips / LineTime, 1) / 4 * LineTime;
	Tr0_trips_rounded = dml_ceil(4.0 * Tr0_trips / LineTime, 1) / 4 * LineTime;

	if (GPUVMEnable) {
		if (GPUVMPageTableLevels >= 3) {
			*Tno_bw = UrgentExtraLatency + trip_to_mem * ((GPUVMPageTableLevels - 2) - 1);
		} else
			*Tno_bw = 0;
	} else if (!myPipe->DCCEnable)
		*Tno_bw = LineTime;
	else
		*Tno_bw = LineTime / 4;

	dst_y_prefetch_equ = VStartup - (Tsetup + dml_max(TWait + TCalc, *Tdmdl)) / LineTime
			- (*DSTYAfterScaler + *DSTXAfterScaler / myPipe->HTotal);

	Lsw_oto = dml_max(PrefetchSourceLinesY, PrefetchSourceLinesC);
	Tsw_oto = Lsw_oto * LineTime;

	prefetch_bw_oto = (PrefetchSourceLinesY * swath_width_luma_ub * BytePerPixelY + PrefetchSourceLinesC * swath_width_chroma_ub * BytePerPixelC) / Tsw_oto;

	if (GPUVMEnable == true) {
		Tvm_oto = dml_max3(*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / prefetch_bw_oto,
				Tvm_trips,
				LineTime / 4.0);
	} else
		Tvm_oto = LineTime / 4.0;

	if ((GPUVMEnable == true || myPipe->DCCEnable == true)) {
		Tr0_oto = dml_max3(
				(MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / prefetch_bw_oto,
				LineTime - Tvm_oto, LineTime / 4);
	} else
		Tr0_oto = (LineTime - Tvm_oto) / 2.0;

	Tvm_oto_lines = dml_ceil(4.0 * Tvm_oto / LineTime, 1) / 4.0;
	Tr0_oto_lines = dml_ceil(4.0 * Tr0_oto / LineTime, 1) / 4.0;
	dst_y_prefetch_oto = Tvm_oto_lines + 2 * Tr0_oto_lines + Lsw_oto;

	dst_y_prefetch_equ = dml_floor(4.0 * (dst_y_prefetch_equ + 0.125), 1) / 4.0;
	Tpre_rounded = dst_y_prefetch_equ * LineTime;

	dml_print("DML: dst_y_prefetch_oto: %f\n", dst_y_prefetch_oto);
	dml_print("DML: dst_y_prefetch_equ: %f\n", dst_y_prefetch_equ);

	dml_print("DML: LineTime: %f\n", LineTime);
	dml_print("DML: VStartup: %d\n", VStartup);
	dml_print("DML: Tvstartup: %fus - time between vstartup and first pixel of active\n", VStartup * LineTime);
	dml_print("DML: Tsetup: %fus - time from vstartup to vready\n", Tsetup);
	dml_print("DML: TCalc: %fus - time for calculations in dchub starting at vready\n", TCalc);
	dml_print("DML: TWait: %fus - time for fabric to become ready max(pstate exit,cstate enter/exit, urgent latency) after TCalc\n", TWait);
	dml_print("DML: Tdmbf: %fus - time for dmd transfer from dchub to dio output buffer\n", Tdmbf);
	dml_print("DML: Tdmec: %fus - time dio takes to transfer dmd\n", Tdmec);
	dml_print("DML: Tdmsks: %fus - time before active dmd must complete transmission at dio\n", Tdmsks);
	dml_print("DML: Tdmdl_vm: %fus - time for vm stages of dmd \n", *Tdmdl_vm);
	dml_print("DML: Tdmdl: %fus - time for fabric to become ready and fetch dmd \n", *Tdmdl);
	dml_print("DML: dst_x_after_scl: %f pixels - number of pixel clocks pipeline and buffer delay after scaler \n", *DSTXAfterScaler);
	dml_print("DML: dst_y_after_scl: %d lines - number of lines of pipeline and buffer delay after scaler \n", (int)*DSTYAfterScaler);

	*PrefetchBandwidth = 0;
	*DestinationLinesToRequestVMInVBlank = 0;
	*DestinationLinesToRequestRowInVBlank = 0;
	*VRatioPrefetchY = 0;
	*VRatioPrefetchC = 0;
	*RequiredPrefetchPixDataBWLuma = 0;
	if (dst_y_prefetch_equ > 1) {
		double PrefetchBandwidth1 = 0;
		double PrefetchBandwidth2 = 0;
		double PrefetchBandwidth3 = 0;
		double PrefetchBandwidth4 = 0;

		if (Tpre_rounded - *Tno_bw > 0)
			PrefetchBandwidth1 = (PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor + 2 * MetaRowByte
					+ 2 * PixelPTEBytesPerRow * HostVMInefficiencyFactor
					+ PrefetchSourceLinesY * swath_width_luma_ub * BytePerPixelY
					+ PrefetchSourceLinesC * swath_width_chroma_ub * BytePerPixelC)
					/ (Tpre_rounded - *Tno_bw);
		else
			PrefetchBandwidth1 = 0;

		if (VStartup == MaxVStartup && (PrefetchBandwidth1 > 4 * prefetch_bw_oto) && (Tpre_rounded - Tsw_oto / 4 - 0.75 * LineTime - *Tno_bw) > 0) {
			PrefetchBandwidth1 = (PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor + 2 * MetaRowByte + 2 * PixelPTEBytesPerRow * HostVMInefficiencyFactor) / (Tpre_rounded - Tsw_oto / 4 - 0.75 * LineTime - *Tno_bw);
		}

		if (Tpre_rounded - *Tno_bw - 2 * Tr0_trips_rounded > 0)
			PrefetchBandwidth2 = (PDEAndMetaPTEBytesFrame *
					HostVMInefficiencyFactor + PrefetchSourceLinesY *
					swath_width_luma_ub * BytePerPixelY +
					PrefetchSourceLinesC * swath_width_chroma_ub *
					BytePerPixelC) /
					(Tpre_rounded - *Tno_bw - 2 * Tr0_trips_rounded);
		else
			PrefetchBandwidth2 = 0;

		if (Tpre_rounded - Tvm_trips_rounded > 0)
			PrefetchBandwidth3 = (2 * MetaRowByte + 2 * PixelPTEBytesPerRow *
					HostVMInefficiencyFactor + PrefetchSourceLinesY *
					swath_width_luma_ub * BytePerPixelY + PrefetchSourceLinesC *
					swath_width_chroma_ub * BytePerPixelC) / (Tpre_rounded -
					Tvm_trips_rounded);
		else
			PrefetchBandwidth3 = 0;

		if (VStartup == MaxVStartup && (PrefetchBandwidth3 > 4 * prefetch_bw_oto) && Tpre_rounded - Tsw_oto / 4 - 0.75 * LineTime - Tvm_trips_rounded > 0) {
			PrefetchBandwidth3 = (2 * MetaRowByte + 2 * PixelPTEBytesPerRow * HostVMInefficiencyFactor) / (Tpre_rounded - Tsw_oto / 4 - 0.75 * LineTime - Tvm_trips_rounded);
		}

		if (Tpre_rounded - Tvm_trips_rounded - 2 * Tr0_trips_rounded > 0)
			PrefetchBandwidth4 = (PrefetchSourceLinesY * swath_width_luma_ub * BytePerPixelY + PrefetchSourceLinesC * swath_width_chroma_ub * BytePerPixelC)
					/ (Tpre_rounded - Tvm_trips_rounded - 2 * Tr0_trips_rounded);
		else
			PrefetchBandwidth4 = 0;

		{
			bool Case1OK;
			bool Case2OK;
			bool Case3OK;

			if (PrefetchBandwidth1 > 0) {
				if (*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / PrefetchBandwidth1
						>= Tvm_trips_rounded && (MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / PrefetchBandwidth1 >= Tr0_trips_rounded) {
					Case1OK = true;
				} else {
					Case1OK = false;
				}
			} else {
				Case1OK = false;
			}

			if (PrefetchBandwidth2 > 0) {
				if (*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / PrefetchBandwidth2
						>= Tvm_trips_rounded && (MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / PrefetchBandwidth2 < Tr0_trips_rounded) {
					Case2OK = true;
				} else {
					Case2OK = false;
				}
			} else {
				Case2OK = false;
			}

			if (PrefetchBandwidth3 > 0) {
				if (*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / PrefetchBandwidth3
						< Tvm_trips_rounded && (MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / PrefetchBandwidth3 >= Tr0_trips_rounded) {
					Case3OK = true;
				} else {
					Case3OK = false;
				}
			} else {
				Case3OK = false;
			}

			if (Case1OK) {
				prefetch_bw_equ = PrefetchBandwidth1;
			} else if (Case2OK) {
				prefetch_bw_equ = PrefetchBandwidth2;
			} else if (Case3OK) {
				prefetch_bw_equ = PrefetchBandwidth3;
			} else {
				prefetch_bw_equ = PrefetchBandwidth4;
			}

			dml_print("DML: prefetch_bw_equ: %f\n", prefetch_bw_equ);

			if (prefetch_bw_equ > 0) {
				if (GPUVMEnable) {
					Tvm_equ = dml_max3(*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / prefetch_bw_equ, Tvm_trips, LineTime / 4);
				} else {
					Tvm_equ = LineTime / 4;
				}

				if ((GPUVMEnable || myPipe->DCCEnable)) {
					Tr0_equ = dml_max4(
							(MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / prefetch_bw_equ,
							Tr0_trips,
							(LineTime - Tvm_equ) / 2,
							LineTime / 4);
				} else {
					Tr0_equ = (LineTime - Tvm_equ) / 2;
				}
			} else {
				Tvm_equ = 0;
				Tr0_equ = 0;
				dml_print("DML: prefetch_bw_equ equals 0! %s:%d\n", __FILE__, __LINE__);
			}
		}

		if (dst_y_prefetch_oto < dst_y_prefetch_equ) {
			*DestinationLinesForPrefetch = dst_y_prefetch_oto;
			TimeForFetchingMetaPTE = Tvm_oto;
			TimeForFetchingRowInVBlank = Tr0_oto;
			*PrefetchBandwidth = prefetch_bw_oto;
		} else {
			*DestinationLinesForPrefetch = dst_y_prefetch_equ;
			TimeForFetchingMetaPTE = Tvm_equ;
			TimeForFetchingRowInVBlank = Tr0_equ;
			*PrefetchBandwidth = prefetch_bw_equ;
		}

		*DestinationLinesToRequestVMInVBlank = dml_ceil(4.0 * TimeForFetchingMetaPTE / LineTime, 1.0) / 4.0;

		*DestinationLinesToRequestRowInVBlank = dml_ceil(4.0 * TimeForFetchingRowInVBlank / LineTime, 1.0) / 4.0;


		LinesToRequestPrefetchPixelData = *DestinationLinesForPrefetch - *DestinationLinesToRequestVMInVBlank
				- 2 * *DestinationLinesToRequestRowInVBlank;

		if (LinesToRequestPrefetchPixelData > 0 && prefetch_bw_equ > 0) {

			*VRatioPrefetchY = (double) PrefetchSourceLinesY
					/ LinesToRequestPrefetchPixelData;
			*VRatioPrefetchY = dml_max(*VRatioPrefetchY, 1.0);
			if ((SwathHeightY > 4) && (VInitPreFillY > 3)) {
				if (LinesToRequestPrefetchPixelData > (VInitPreFillY - 3.0) / 2.0) {
					*VRatioPrefetchY = dml_max((double) PrefetchSourceLinesY / LinesToRequestPrefetchPixelData,
						(double) MaxNumSwathY * SwathHeightY / (LinesToRequestPrefetchPixelData - (VInitPreFillY - 3.0) / 2.0));
					*VRatioPrefetchY = dml_max(*VRatioPrefetchY, 1.0);
				} else {
					MyError = true;
					dml_print("DML: MyErr set %s:%d\n", __FILE__, __LINE__);
					*VRatioPrefetchY = 0;
				}
			}

			*VRatioPrefetchC = (double) PrefetchSourceLinesC / LinesToRequestPrefetchPixelData;
			*VRatioPrefetchC = dml_max(*VRatioPrefetchC, 1.0);

			if ((SwathHeightC > 4)) {
				if (LinesToRequestPrefetchPixelData > (VInitPreFillC - 3.0) / 2.0) {
					*VRatioPrefetchC = dml_max(*VRatioPrefetchC,
						(double) MaxNumSwathC * SwathHeightC / (LinesToRequestPrefetchPixelData - (VInitPreFillC - 3.0) / 2.0));
					*VRatioPrefetchC = dml_max(*VRatioPrefetchC, 1.0);
				} else {
					MyError = true;
					dml_print("DML: MyErr set %s:%d\n", __FILE__, __LINE__);
					*VRatioPrefetchC = 0;
				}
			}

			*RequiredPrefetchPixDataBWLuma = (double) PrefetchSourceLinesY / LinesToRequestPrefetchPixelData * BytePerPixelY * swath_width_luma_ub / LineTime;
			*RequiredPrefetchPixDataBWChroma = (double) PrefetchSourceLinesC / LinesToRequestPrefetchPixelData * BytePerPixelC * swath_width_chroma_ub / LineTime;
		} else {
			MyError = true;
			dml_print("DML: MyErr set %s:%d\n", __FILE__, __LINE__);
			dml_print("DML: LinesToRequestPrefetchPixelData: %f, should be > 0\n", LinesToRequestPrefetchPixelData);
			*VRatioPrefetchY = 0;
			*VRatioPrefetchC = 0;
			*RequiredPrefetchPixDataBWLuma = 0;
			*RequiredPrefetchPixDataBWChroma = 0;
		}

		dml_print("DML: Tpre: %fus - sum of tim to request meta pte, 2 x data pte + meta data, swaths\n", (double)LinesToRequestPrefetchPixelData * LineTime + 2.0*TimeForFetchingRowInVBlank + TimeForFetchingMetaPTE);
		dml_print("DML:  Tvm: %fus - time to fetch page tables for meta surface\n", TimeForFetchingMetaPTE);
		dml_print("DML:  Tr0: %fus - time to fetch first row of data pagetables and first row of meta data (done in parallel)\n", TimeForFetchingRowInVBlank);
		dml_print("DML:  Tr1: %fus - time to fetch second row of data pagetables and second row of meta data (done in parallel)\n", TimeForFetchingRowInVBlank);
		dml_print("DML:  Tsw: %fus = time to fetch enough pixel data and cursor data to feed the scalers init position and detile\n", (double)LinesToRequestPrefetchPixelData * LineTime);
		dml_print("DML: To: %fus - time for propagation from scaler to optc\n", (*DSTYAfterScaler + ((*DSTXAfterScaler) / (double) myPipe->HTotal)) * LineTime);
		dml_print("DML: Tvstartup - Tsetup - Tcalc - Twait - Tpre - To > 0\n");
		dml_print("DML: Tslack(pre): %fus - time left over in schedule\n", VStartup * LineTime - TimeForFetchingMetaPTE - 2 * TimeForFetchingRowInVBlank - (*DSTYAfterScaler + ((*DSTXAfterScaler) / (double) myPipe->HTotal)) * LineTime - TWait - TCalc - Tsetup);
		dml_print("DML: row_bytes = dpte_row_bytes (per_pipe) = PixelPTEBytesPerRow = : %d\n", PixelPTEBytesPerRow);

	} else {
		MyError = true;
		dml_print("DML: MyErr set %s:%d\n", __FILE__, __LINE__);
	}

	{
		double prefetch_vm_bw = 0;
		double prefetch_row_bw = 0;

		if (PDEAndMetaPTEBytesFrame == 0) {
			prefetch_vm_bw = 0;
		} else if (*DestinationLinesToRequestVMInVBlank > 0) {
			prefetch_vm_bw = PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / (*DestinationLinesToRequestVMInVBlank * LineTime);
		} else {
			prefetch_vm_bw = 0;
			MyError = true;
			dml_print("DML: MyErr set %s:%d\n", __FILE__, __LINE__);
		}
		if (MetaRowByte + PixelPTEBytesPerRow == 0) {
			prefetch_row_bw = 0;
		} else if (*DestinationLinesToRequestRowInVBlank > 0) {
			prefetch_row_bw = (MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / (*DestinationLinesToRequestRowInVBlank * LineTime);
		} else {
			prefetch_row_bw = 0;
			MyError = true;
			dml_print("DML: MyErr set %s:%d\n", __FILE__, __LINE__);
		}

		*prefetch_vmrow_bw = dml_max(prefetch_vm_bw, prefetch_row_bw);
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
		*RequiredPrefetchPixDataBWLuma = 0;
		*RequiredPrefetchPixDataBWChroma = 0;
	}

	return MyError;
}

static double RoundToDFSGranularityUp(double Clock, double VCOSpeed)
{
	return VCOSpeed * 4 / dml_floor(VCOSpeed * 4 / Clock, 1);
}

static double RoundToDFSGranularityDown(double Clock, double VCOSpeed)
{
	return VCOSpeed * 4 / dml_ceil(VCOSpeed * 4.0 / Clock, 1);
}

static void CalculateDCCConfiguration(
		bool DCCEnabled,
		bool DCCProgrammingAssumesScanDirectionUnknown,
		enum source_format_class SourcePixelFormat,
		unsigned int SurfaceWidthLuma,
		unsigned int SurfaceWidthChroma,
		unsigned int SurfaceHeightLuma,
		unsigned int SurfaceHeightChroma,
		double DETBufferSize,
		unsigned int RequestHeight256ByteLuma,
		unsigned int RequestHeight256ByteChroma,
		enum dm_swizzle_mode TilingFormat,
		unsigned int BytePerPixelY,
		unsigned int BytePerPixelC,
		double BytePerPixelDETY,
		double BytePerPixelDETC,
		enum scan_direction_class ScanOrientation,
		unsigned int *MaxUncompressedBlockLuma,
		unsigned int *MaxUncompressedBlockChroma,
		unsigned int *MaxCompressedBlockLuma,
		unsigned int *MaxCompressedBlockChroma,
		unsigned int *IndependentBlockLuma,
		unsigned int *IndependentBlockChroma)
{
	int yuv420 = 0;
	int horz_div_l = 0;
	int horz_div_c = 0;
	int vert_div_l = 0;
	int vert_div_c = 0;

	int req128_horz_wc_l = 0;
	int req128_horz_wc_c = 0;
	int req128_vert_wc_l = 0;
	int req128_vert_wc_c = 0;
	int segment_order_horz_contiguous_luma = 0;
	int segment_order_horz_contiguous_chroma = 0;
	int segment_order_vert_contiguous_luma = 0;
	int segment_order_vert_contiguous_chroma = 0;

	long full_swath_bytes_horz_wc_l = 0;
	long full_swath_bytes_horz_wc_c = 0;
	long full_swath_bytes_vert_wc_l = 0;
	long full_swath_bytes_vert_wc_c = 0;

	long swath_buf_size = 0;
	double detile_buf_vp_horz_limit = 0;
	double detile_buf_vp_vert_limit = 0;

	long MAS_vp_horz_limit = 0;
	long MAS_vp_vert_limit = 0;
	long max_vp_horz_width = 0;
	long max_vp_vert_height = 0;
	long eff_surf_width_l = 0;
	long eff_surf_width_c = 0;
	long eff_surf_height_l = 0;
	long eff_surf_height_c = 0;

	typedef enum {
		REQ_256Bytes,
		REQ_128BytesNonContiguous,
		REQ_128BytesContiguous,
		REQ_NA
	} RequestType;

	RequestType   RequestLuma;
	RequestType   RequestChroma;

	yuv420 = ((SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10 || SourcePixelFormat == dm_420_12) ? 1 : 0);
	horz_div_l = 1;
	horz_div_c = 1;
	vert_div_l = 1;
	vert_div_c = 1;

	if (BytePerPixelY == 1)
		vert_div_l = 0;
	if (BytePerPixelC == 1)
		vert_div_c = 0;
	if (BytePerPixelY == 8
			&& (TilingFormat == dm_sw_64kb_s || TilingFormat == dm_sw_64kb_s_t
					|| TilingFormat == dm_sw_64kb_s_x))
		horz_div_l = 0;
	if (BytePerPixelC == 8
			&& (TilingFormat == dm_sw_64kb_s || TilingFormat == dm_sw_64kb_s_t
					|| TilingFormat == dm_sw_64kb_s_x))
		horz_div_c = 0;

	if (BytePerPixelC == 0) {
		swath_buf_size = DETBufferSize / 2 - 2 * 256;
		detile_buf_vp_horz_limit = (double) swath_buf_size
				/ ((double) RequestHeight256ByteLuma * BytePerPixelY
						/ (1 + horz_div_l));
		detile_buf_vp_vert_limit = (double) swath_buf_size
				/ (256.0 / RequestHeight256ByteLuma / (1 + vert_div_l));
	} else {
		swath_buf_size = DETBufferSize / 2 - 2 * 2 * 256;
		detile_buf_vp_horz_limit = (double) swath_buf_size
				/ ((double) RequestHeight256ByteLuma * BytePerPixelY
						/ (1 + horz_div_l)
						+ (double) RequestHeight256ByteChroma
								* BytePerPixelC / (1 + horz_div_c)
								/ (1 + yuv420));
		detile_buf_vp_vert_limit = (double) swath_buf_size
				/ (256.0 / RequestHeight256ByteLuma / (1 + vert_div_l)
						+ 256.0 / RequestHeight256ByteChroma
								/ (1 + vert_div_c) / (1 + yuv420));
	}

	if (SourcePixelFormat == dm_420_10) {
		detile_buf_vp_horz_limit = 1.5 * detile_buf_vp_horz_limit;
		detile_buf_vp_vert_limit = 1.5 * detile_buf_vp_vert_limit;
	}

	detile_buf_vp_horz_limit = dml_floor(detile_buf_vp_horz_limit - 1, 16);
	detile_buf_vp_vert_limit = dml_floor(detile_buf_vp_vert_limit - 1, 16);

	MAS_vp_horz_limit = 5760;
	MAS_vp_vert_limit = (BytePerPixelC > 0 ? 2880 : 5760);
	max_vp_horz_width = dml_min((double) MAS_vp_horz_limit, detile_buf_vp_horz_limit);
	max_vp_vert_height = dml_min((double) MAS_vp_vert_limit, detile_buf_vp_vert_limit);
	eff_surf_width_l =
			(SurfaceWidthLuma > max_vp_horz_width ? max_vp_horz_width : SurfaceWidthLuma);
	eff_surf_width_c = eff_surf_width_l / (1 + yuv420);
	eff_surf_height_l = (
			SurfaceHeightLuma > max_vp_vert_height ?
					max_vp_vert_height : SurfaceHeightLuma);
	eff_surf_height_c = eff_surf_height_l / (1 + yuv420);

	full_swath_bytes_horz_wc_l = eff_surf_width_l * RequestHeight256ByteLuma * BytePerPixelY;
	full_swath_bytes_vert_wc_l = eff_surf_height_l * 256 / RequestHeight256ByteLuma;
	if (BytePerPixelC > 0) {
		full_swath_bytes_horz_wc_c = eff_surf_width_c * RequestHeight256ByteChroma
				* BytePerPixelC;
		full_swath_bytes_vert_wc_c = eff_surf_height_c * 256 / RequestHeight256ByteChroma;
	} else {
		full_swath_bytes_horz_wc_c = 0;
		full_swath_bytes_vert_wc_c = 0;
	}

	if (SourcePixelFormat == dm_420_10) {
		full_swath_bytes_horz_wc_l = dml_ceil(full_swath_bytes_horz_wc_l * 2 / 3, 256);
		full_swath_bytes_horz_wc_c = dml_ceil(full_swath_bytes_horz_wc_c * 2 / 3, 256);
		full_swath_bytes_vert_wc_l = dml_ceil(full_swath_bytes_vert_wc_l * 2 / 3, 256);
		full_swath_bytes_vert_wc_c = dml_ceil(full_swath_bytes_vert_wc_c * 2 / 3, 256);
	}

	if (2 * full_swath_bytes_horz_wc_l + 2 * full_swath_bytes_horz_wc_c <= DETBufferSize) {
		req128_horz_wc_l = 0;
		req128_horz_wc_c = 0;
	} else if (full_swath_bytes_horz_wc_l < 1.5 * full_swath_bytes_horz_wc_c
			&& 2 * full_swath_bytes_horz_wc_l + full_swath_bytes_horz_wc_c
					<= DETBufferSize) {
		req128_horz_wc_l = 0;
		req128_horz_wc_c = 1;
	} else if (full_swath_bytes_horz_wc_l >= 1.5 * full_swath_bytes_horz_wc_c
			&& full_swath_bytes_horz_wc_l + 2 * full_swath_bytes_horz_wc_c
					<= DETBufferSize) {
		req128_horz_wc_l = 1;
		req128_horz_wc_c = 0;
	} else {
		req128_horz_wc_l = 1;
		req128_horz_wc_c = 1;
	}

	if (2 * full_swath_bytes_vert_wc_l + 2 * full_swath_bytes_vert_wc_c <= DETBufferSize) {
		req128_vert_wc_l = 0;
		req128_vert_wc_c = 0;
	} else if (full_swath_bytes_vert_wc_l < 1.5 * full_swath_bytes_vert_wc_c
			&& 2 * full_swath_bytes_vert_wc_l + full_swath_bytes_vert_wc_c
					<= DETBufferSize) {
		req128_vert_wc_l = 0;
		req128_vert_wc_c = 1;
	} else if (full_swath_bytes_vert_wc_l >= 1.5 * full_swath_bytes_vert_wc_c
			&& full_swath_bytes_vert_wc_l + 2 * full_swath_bytes_vert_wc_c
					<= DETBufferSize) {
		req128_vert_wc_l = 1;
		req128_vert_wc_c = 0;
	} else {
		req128_vert_wc_l = 1;
		req128_vert_wc_c = 1;
	}

	if (BytePerPixelY == 2 || (BytePerPixelY == 4 && TilingFormat != dm_sw_64kb_r_x)) {
		segment_order_horz_contiguous_luma = 0;
	} else {
		segment_order_horz_contiguous_luma = 1;
	}
	if ((BytePerPixelY == 8
			&& (TilingFormat == dm_sw_64kb_d || TilingFormat == dm_sw_64kb_d_x
					|| TilingFormat == dm_sw_64kb_d_t
					|| TilingFormat == dm_sw_64kb_r_x))
			|| (BytePerPixelY == 4 && TilingFormat == dm_sw_64kb_r_x)) {
		segment_order_vert_contiguous_luma = 0;
	} else {
		segment_order_vert_contiguous_luma = 1;
	}
	if (BytePerPixelC == 2 || (BytePerPixelC == 4 && TilingFormat != dm_sw_64kb_r_x)) {
		segment_order_horz_contiguous_chroma = 0;
	} else {
		segment_order_horz_contiguous_chroma = 1;
	}
	if ((BytePerPixelC == 8
			&& (TilingFormat == dm_sw_64kb_d || TilingFormat == dm_sw_64kb_d_x
					|| TilingFormat == dm_sw_64kb_d_t
					|| TilingFormat == dm_sw_64kb_r_x))
			|| (BytePerPixelC == 4 && TilingFormat == dm_sw_64kb_r_x)) {
		segment_order_vert_contiguous_chroma = 0;
	} else {
		segment_order_vert_contiguous_chroma = 1;
	}

	if (DCCProgrammingAssumesScanDirectionUnknown == true) {
		if (req128_horz_wc_l == 0 && req128_vert_wc_l == 0) {
			RequestLuma = REQ_256Bytes;
		} else if ((req128_horz_wc_l == 1 && segment_order_horz_contiguous_luma == 0)
				|| (req128_vert_wc_l == 1 && segment_order_vert_contiguous_luma == 0)) {
			RequestLuma = REQ_128BytesNonContiguous;
		} else {
			RequestLuma = REQ_128BytesContiguous;
		}
		if (req128_horz_wc_c == 0 && req128_vert_wc_c == 0) {
			RequestChroma = REQ_256Bytes;
		} else if ((req128_horz_wc_c == 1 && segment_order_horz_contiguous_chroma == 0)
				|| (req128_vert_wc_c == 1
						&& segment_order_vert_contiguous_chroma == 0)) {
			RequestChroma = REQ_128BytesNonContiguous;
		} else {
			RequestChroma = REQ_128BytesContiguous;
		}
	} else if (ScanOrientation != dm_vert) {
		if (req128_horz_wc_l == 0) {
			RequestLuma = REQ_256Bytes;
		} else if (segment_order_horz_contiguous_luma == 0) {
			RequestLuma = REQ_128BytesNonContiguous;
		} else {
			RequestLuma = REQ_128BytesContiguous;
		}
		if (req128_horz_wc_c == 0) {
			RequestChroma = REQ_256Bytes;
		} else if (segment_order_horz_contiguous_chroma == 0) {
			RequestChroma = REQ_128BytesNonContiguous;
		} else {
			RequestChroma = REQ_128BytesContiguous;
		}
	} else {
		if (req128_vert_wc_l == 0) {
			RequestLuma = REQ_256Bytes;
		} else if (segment_order_vert_contiguous_luma == 0) {
			RequestLuma = REQ_128BytesNonContiguous;
		} else {
			RequestLuma = REQ_128BytesContiguous;
		}
		if (req128_vert_wc_c == 0) {
			RequestChroma = REQ_256Bytes;
		} else if (segment_order_vert_contiguous_chroma == 0) {
			RequestChroma = REQ_128BytesNonContiguous;
		} else {
			RequestChroma = REQ_128BytesContiguous;
		}
	}

	if (RequestLuma == REQ_256Bytes) {
		*MaxUncompressedBlockLuma = 256;
		*MaxCompressedBlockLuma = 256;
		*IndependentBlockLuma = 0;
	} else if (RequestLuma == REQ_128BytesContiguous) {
		*MaxUncompressedBlockLuma = 256;
		*MaxCompressedBlockLuma = 128;
		*IndependentBlockLuma = 128;
	} else {
		*MaxUncompressedBlockLuma = 256;
		*MaxCompressedBlockLuma = 64;
		*IndependentBlockLuma = 64;
	}

	if (RequestChroma == REQ_256Bytes) {
		*MaxUncompressedBlockChroma = 256;
		*MaxCompressedBlockChroma = 256;
		*IndependentBlockChroma = 0;
	} else if (RequestChroma == REQ_128BytesContiguous) {
		*MaxUncompressedBlockChroma = 256;
		*MaxCompressedBlockChroma = 128;
		*IndependentBlockChroma = 128;
	} else {
		*MaxUncompressedBlockChroma = 256;
		*MaxCompressedBlockChroma = 64;
		*IndependentBlockChroma = 64;
	}

	if (DCCEnabled != true || BytePerPixelC == 0) {
		*MaxUncompressedBlockChroma = 0;
		*MaxCompressedBlockChroma = 0;
		*IndependentBlockChroma = 0;
	}

	if (DCCEnabled != true) {
		*MaxUncompressedBlockLuma = 0;
		*MaxCompressedBlockLuma = 0;
		*IndependentBlockLuma = 0;
	}
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
	unsigned int MaxPartialSwath = 0;

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
		unsigned int SwathWidth,
		unsigned int ViewportHeight,
		bool GPUVMEnable,
		bool HostVMEnable,
		unsigned int HostVMMaxNonCachedPageTableLevels,
		unsigned int GPUVMMinPageSize,
		unsigned int HostVMMinPageSize,
		unsigned int PTEBufferSizeInRequests,
		unsigned int Pitch,
		unsigned int DCCMetaPitch,
		unsigned int *MacroTileWidth,
		unsigned int *MetaRowByte,
		unsigned int *PixelPTEBytesPerRow,
		bool *PTEBufferSizeNotExceeded,
		unsigned int *dpte_row_width_ub,
		unsigned int *dpte_row_height,
		unsigned int *MetaRequestWidth,
		unsigned int *MetaRequestHeight,
		unsigned int *meta_row_width,
		unsigned int *meta_row_height,
		unsigned int *vm_group_bytes,
		unsigned int *dpte_group_bytes,
		unsigned int *PixelPTEReqWidth,
		unsigned int *PixelPTEReqHeight,
		unsigned int *PTERequestSize,
		unsigned int *DPDE0BytesFrame,
		unsigned int *MetaPTEBytesFrame)
{
	unsigned int MPDEBytesFrame = 0;
	unsigned int DCCMetaSurfaceBytes = 0;
	unsigned int MacroTileSizeBytes = 0;
	unsigned int MacroTileHeight = 0;
	unsigned int ExtraDPDEBytesFrame = 0;
	unsigned int PDEAndMetaPTEBytesFrame = 0;
	unsigned int PixelPTEReqHeightPTEs = 0;
	unsigned int HostVMDynamicLevels = 0;

	double FractionOfPTEReturnDrop;

	if (GPUVMEnable == true && HostVMEnable == true) {
		if (HostVMMinPageSize < 2048) {
			HostVMDynamicLevels = HostVMMaxNonCachedPageTableLevels;
		} else if (HostVMMinPageSize >= 2048 && HostVMMinPageSize < 1048576) {
			HostVMDynamicLevels = dml_max(0, (int) HostVMMaxNonCachedPageTableLevels - 1);
		} else {
			HostVMDynamicLevels = dml_max(0, (int) HostVMMaxNonCachedPageTableLevels - 2);
		}
	}

	*MetaRequestHeight = 8 * BlockHeight256Bytes;
	*MetaRequestWidth = 8 * BlockWidth256Bytes;
	if (ScanDirection != dm_vert) {
		*meta_row_height = *MetaRequestHeight;
		*meta_row_width = dml_ceil((double) SwathWidth - 1, *MetaRequestWidth)
				+ *MetaRequestWidth;
		*MetaRowByte = *meta_row_width * *MetaRequestHeight * BytePerPixel / 256.0;
	} else {
		*meta_row_height = *MetaRequestWidth;
		*meta_row_width = dml_ceil((double) SwathWidth - 1, *MetaRequestHeight)
				+ *MetaRequestHeight;
		*MetaRowByte = *meta_row_width * *MetaRequestWidth * BytePerPixel / 256.0;
	}
	DCCMetaSurfaceBytes = DCCMetaPitch * (dml_ceil(ViewportHeight - 1, 64 * BlockHeight256Bytes)
					+ 64 * BlockHeight256Bytes) * BytePerPixel / 256;
	if (GPUVMEnable == true) {
		*MetaPTEBytesFrame = (dml_ceil((double) (DCCMetaSurfaceBytes - 4.0 * 1024.0) / (8 * 4.0 * 1024), 1) + 1) * 64;
		MPDEBytesFrame = 128 * (mode_lib->vba.GPUVMMaxPageTableLevels - 1);
	} else {
		*MetaPTEBytesFrame = 0;
		MPDEBytesFrame = 0;
	}

	if (DCCEnable != true) {
		*MetaPTEBytesFrame = 0;
		MPDEBytesFrame = 0;
		*MetaRowByte = 0;
	}

	if (SurfaceTiling == dm_sw_linear) {
		MacroTileSizeBytes = 256;
		MacroTileHeight = BlockHeight256Bytes;
	} else {
		MacroTileSizeBytes = 65536;
		MacroTileHeight = 16 * BlockHeight256Bytes;
	}
	*MacroTileWidth = MacroTileSizeBytes / BytePerPixel / MacroTileHeight;

	if (GPUVMEnable == true && mode_lib->vba.GPUVMMaxPageTableLevels > 1) {
		if (ScanDirection != dm_vert) {
			*DPDE0BytesFrame = 64 * (dml_ceil(((Pitch * (dml_ceil(ViewportHeight - 1, MacroTileHeight) + MacroTileHeight) * BytePerPixel) - MacroTileSizeBytes) / (8 * 2097152), 1) + 1);
		} else {
			*DPDE0BytesFrame = 64 * (dml_ceil(((Pitch * (dml_ceil((double) SwathWidth - 1, MacroTileHeight) + MacroTileHeight) * BytePerPixel) - MacroTileSizeBytes) / (8 * 2097152), 1) + 1);
		}
		ExtraDPDEBytesFrame = 128 * (mode_lib->vba.GPUVMMaxPageTableLevels - 2);
	} else {
		*DPDE0BytesFrame = 0;
		ExtraDPDEBytesFrame = 0;
	}

	PDEAndMetaPTEBytesFrame = *MetaPTEBytesFrame + MPDEBytesFrame + *DPDE0BytesFrame
			+ ExtraDPDEBytesFrame;

	if (HostVMEnable == true) {
		PDEAndMetaPTEBytesFrame = PDEAndMetaPTEBytesFrame * (1 + 8 * HostVMDynamicLevels);
	}

	if (SurfaceTiling == dm_sw_linear) {
		PixelPTEReqHeightPTEs = 1;
		*PixelPTEReqHeight = 1;
		*PixelPTEReqWidth = 32768.0 / BytePerPixel;
		*PTERequestSize = 64;
		FractionOfPTEReturnDrop = 0;
	} else if (MacroTileSizeBytes == 4096) {
		PixelPTEReqHeightPTEs = 1;
		*PixelPTEReqHeight = MacroTileHeight;
		*PixelPTEReqWidth = 8 * *MacroTileWidth;
		*PTERequestSize = 64;
		if (ScanDirection != dm_vert)
			FractionOfPTEReturnDrop = 0;
		else
			FractionOfPTEReturnDrop = 7.0 / 8;
	} else if (GPUVMMinPageSize == 4 && MacroTileSizeBytes > 4096) {
		PixelPTEReqHeightPTEs = 16;
		*PixelPTEReqHeight = 16 * BlockHeight256Bytes;
		*PixelPTEReqWidth = 16 * BlockWidth256Bytes;
		*PTERequestSize = 128;
		FractionOfPTEReturnDrop = 0;
	} else {
		PixelPTEReqHeightPTEs = 1;
		*PixelPTEReqHeight = MacroTileHeight;
		*PixelPTEReqWidth = 8 * *MacroTileWidth;
		*PTERequestSize = 64;
		FractionOfPTEReturnDrop = 0;
	}

	if (SurfaceTiling == dm_sw_linear) {
		if (PTEBufferSizeInRequests == 0)
			*dpte_row_height = 1;
		else
			*dpte_row_height = dml_min(128, 1 << (unsigned int) dml_floor(dml_log2(PTEBufferSizeInRequests * *PixelPTEReqWidth / Pitch), 1));
		*dpte_row_width_ub = (dml_ceil(((double) SwathWidth - 1) / *PixelPTEReqWidth, 1) + 1) * *PixelPTEReqWidth;
		*PixelPTEBytesPerRow = *dpte_row_width_ub / *PixelPTEReqWidth * *PTERequestSize;
	} else if (ScanDirection != dm_vert) {
		*dpte_row_height = *PixelPTEReqHeight;
		*dpte_row_width_ub = (dml_ceil((double) (SwathWidth - 1) / *PixelPTEReqWidth, 1) + 1) * *PixelPTEReqWidth;
		*PixelPTEBytesPerRow = *dpte_row_width_ub / *PixelPTEReqWidth * *PTERequestSize;
	} else {
		*dpte_row_height = dml_min(*PixelPTEReqWidth, *MacroTileWidth);
		*dpte_row_width_ub = (dml_ceil((double) (SwathWidth - 1) / *PixelPTEReqHeight, 1) + 1) * *PixelPTEReqHeight;
		*PixelPTEBytesPerRow = *dpte_row_width_ub / *PixelPTEReqHeight * *PTERequestSize;
	}
	if (*PixelPTEBytesPerRow * (1 - FractionOfPTEReturnDrop)
			<= 64 * PTEBufferSizeInRequests) {
		*PTEBufferSizeNotExceeded = true;
	} else {
		*PTEBufferSizeNotExceeded = false;
	}

	if (GPUVMEnable != true) {
		*PixelPTEBytesPerRow = 0;
		*PTEBufferSizeNotExceeded = true;
	}
	dml_print("DML: vm_bytes = meta_pte_bytes_per_frame (per_pipe) = MetaPTEBytesFrame = : %i\n", *MetaPTEBytesFrame);

	if (HostVMEnable == true) {
		*PixelPTEBytesPerRow = *PixelPTEBytesPerRow * (1 + 8 * HostVMDynamicLevels);
	}

	if (HostVMEnable == true) {
		*vm_group_bytes = 512;
		*dpte_group_bytes = 512;
	} else if (GPUVMEnable == true) {
		*vm_group_bytes = 2048;
		if (SurfaceTiling != dm_sw_linear && PixelPTEReqHeightPTEs == 1 && ScanDirection == dm_vert) {
			*dpte_group_bytes = 512;
		} else {
			*dpte_group_bytes = 2048;
		}
	} else {
		*vm_group_bytes = 0;
		*dpte_group_bytes = 0;
	}

	return PDEAndMetaPTEBytesFrame;
}

static void DISPCLKDPPCLKDCFCLKDeepSleepPrefetchParametersWatermarksAndPerformanceCalculation(
		struct display_mode_lib *mode_lib)
{
	struct vba_vars_st *v = &mode_lib->vba;
	unsigned int j, k;
	long ReorderBytes = 0;
	unsigned int PrefetchMode = v->PrefetchModePerState[v->VoltageLevel][v->maxMpcComb];
	double MaxTotalRDBandwidth = 0;
	double MaxTotalRDBandwidthNoUrgentBurst = 0;
	bool DestinationLineTimesForPrefetchLessThan2 = false;
	bool VRatioPrefetchMoreThan4 = false;
	double TWait;

	v->WritebackDISPCLK = 0.0;
	v->DISPCLKWithRamping = 0;
	v->DISPCLKWithoutRamping = 0;
	v->GlobalDPPCLK = 0.0;
	/* DAL custom code: need to update ReturnBW in case min dcfclk is overriden */
	v->IdealSDPPortBandwidthPerState[v->VoltageLevel][v->maxMpcComb] = dml_min3(
			v->ReturnBusWidth * v->DCFCLK,
			v->DRAMSpeedPerState[v->VoltageLevel] * v->NumberOfChannels * v->DRAMChannelWidth,
			v->FabricClockPerState[v->VoltageLevel] * v->FabricDatapathToDCNDataReturn);
	if (v->HostVMEnable != true) {
		v->ReturnBW = v->IdealSDPPortBandwidthPerState[v->VoltageLevel][v->maxMpcComb] * v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelDataOnly / 100;
	} else {
		v->ReturnBW = v->IdealSDPPortBandwidthPerState[v->VoltageLevel][v->maxMpcComb] * v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData / 100;
	}
	/* End DAL custom code */

	// DISPCLK and DPPCLK Calculation
	//
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		if (v->WritebackEnable[k]) {
			v->WritebackDISPCLK = dml_max(v->WritebackDISPCLK,
				dml30_CalculateWriteBackDISPCLK(
						v->WritebackPixelFormat[k],
						v->PixelClock[k],
						v->WritebackHRatio[k],
						v->WritebackVRatio[k],
						v->WritebackHTaps[k],
						v->WritebackVTaps[k],
						v->WritebackSourceWidth[k],
						v->WritebackDestinationWidth[k],
						v->HTotal[k],
						v->WritebackLineBufferSize));
		}
	}

	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		if (v->HRatio[k] > 1) {
			v->PSCL_THROUGHPUT_LUMA[k] = dml_min(v->MaxDCHUBToPSCLThroughput,
				v->MaxPSCLToLBThroughput * v->HRatio[k] / dml_ceil(v->htaps[k] / 6.0, 1));
		} else {
			v->PSCL_THROUGHPUT_LUMA[k] = dml_min(
					v->MaxDCHUBToPSCLThroughput,
					v->MaxPSCLToLBThroughput);
		}

		v->DPPCLKUsingSingleDPPLuma = v->PixelClock[k]
			* dml_max(v->vtaps[k] / 6.0 * dml_min(1.0, v->HRatio[k]),
				dml_max(v->HRatio[k] * v->VRatio[k] / v->PSCL_THROUGHPUT_LUMA[k], 1.0));

		if ((v->htaps[k] > 6 || v->vtaps[k] > 6)
				&& v->DPPCLKUsingSingleDPPLuma < 2 * v->PixelClock[k]) {
			v->DPPCLKUsingSingleDPPLuma = 2 * v->PixelClock[k];
		}

		if ((v->SourcePixelFormat[k] != dm_420_8
				&& v->SourcePixelFormat[k] != dm_420_10
				&& v->SourcePixelFormat[k] != dm_420_12
				&& v->SourcePixelFormat[k] != dm_rgbe_alpha)) {
			v->PSCL_THROUGHPUT_CHROMA[k] = 0.0;
			v->DPPCLKUsingSingleDPP[k] = v->DPPCLKUsingSingleDPPLuma;
		} else {
			if (v->HRatioChroma[k] > 1) {
				v->PSCL_THROUGHPUT_CHROMA[k] = dml_min(v->MaxDCHUBToPSCLThroughput,
					v->MaxPSCLToLBThroughput * v->HRatioChroma[k] / dml_ceil(v->HTAPsChroma[k] / 6.0, 1.0));
			} else {
				v->PSCL_THROUGHPUT_CHROMA[k] = dml_min(
						v->MaxDCHUBToPSCLThroughput,
						v->MaxPSCLToLBThroughput);
			}
			v->DPPCLKUsingSingleDPPChroma = v->PixelClock[k]
				* dml_max3(v->VTAPsChroma[k] / 6.0 * dml_min(1.0, v->HRatioChroma[k]),
					v->HRatioChroma[k] * v->VRatioChroma[k] / v->PSCL_THROUGHPUT_CHROMA[k], 1.0);

			if ((v->HTAPsChroma[k] > 6 || v->VTAPsChroma[k] > 6)
					&& v->DPPCLKUsingSingleDPPChroma
							< 2 * v->PixelClock[k]) {
				v->DPPCLKUsingSingleDPPChroma = 2
						* v->PixelClock[k];
			}

			v->DPPCLKUsingSingleDPP[k] = dml_max(
					v->DPPCLKUsingSingleDPPLuma,
					v->DPPCLKUsingSingleDPPChroma);
		}
	}

	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		if (v->BlendingAndTiming[k] != k)
			continue;
		if (v->ODMCombineEnabled[k] == dm_odm_combine_mode_4to1) {
			v->DISPCLKWithRamping = dml_max(v->DISPCLKWithRamping,
				v->PixelClock[k] / 4 * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100)
					* (1 + v->DISPCLKRampingMargin / 100));
			v->DISPCLKWithoutRamping = dml_max(v->DISPCLKWithoutRamping,
				v->PixelClock[k] / 4 * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100));
		} else if (v->ODMCombineEnabled[k] == dm_odm_combine_mode_2to1) {
			v->DISPCLKWithRamping = dml_max(v->DISPCLKWithRamping,
				v->PixelClock[k] / 2 * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100)
					* (1 + v->DISPCLKRampingMargin / 100));
			v->DISPCLKWithoutRamping = dml_max(v->DISPCLKWithoutRamping,
				v->PixelClock[k] / 2 * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100));
		} else {
			v->DISPCLKWithRamping = dml_max(v->DISPCLKWithRamping,
				v->PixelClock[k] * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100)
									* (1 + v->DISPCLKRampingMargin / 100));
			v->DISPCLKWithoutRamping = dml_max(v->DISPCLKWithoutRamping,
				v->PixelClock[k] * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100));
		}
	}

	v->DISPCLKWithRamping = dml_max(
			v->DISPCLKWithRamping,
			v->WritebackDISPCLK);
	v->DISPCLKWithoutRamping = dml_max(
			v->DISPCLKWithoutRamping,
			v->WritebackDISPCLK);

	ASSERT(v->DISPCLKDPPCLKVCOSpeed != 0);
	v->DISPCLKWithRampingRoundedToDFSGranularity = RoundToDFSGranularityUp(
			v->DISPCLKWithRamping,
			v->DISPCLKDPPCLKVCOSpeed);
	v->DISPCLKWithoutRampingRoundedToDFSGranularity = RoundToDFSGranularityUp(
			v->DISPCLKWithoutRamping,
			v->DISPCLKDPPCLKVCOSpeed);
	v->MaxDispclkRoundedToDFSGranularity = RoundToDFSGranularityDown(
			v->soc.clock_limits[mode_lib->soc.num_states - 1].dispclk_mhz,
			v->DISPCLKDPPCLKVCOSpeed);
	if (v->DISPCLKWithoutRampingRoundedToDFSGranularity
			> v->MaxDispclkRoundedToDFSGranularity) {
		v->DISPCLK_calculated =
				v->DISPCLKWithoutRampingRoundedToDFSGranularity;
	} else if (v->DISPCLKWithRampingRoundedToDFSGranularity
			> v->MaxDispclkRoundedToDFSGranularity) {
		v->DISPCLK_calculated = v->MaxDispclkRoundedToDFSGranularity;
	} else {
		v->DISPCLK_calculated =
				v->DISPCLKWithRampingRoundedToDFSGranularity;
	}
	v->DISPCLK = v->DISPCLK_calculated;
	DTRACE("   dispclk_mhz (calculated) = %f", v->DISPCLK_calculated);

	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		v->DPPCLK_calculated[k] = v->DPPCLKUsingSingleDPP[k]
				/ v->DPPPerPlane[k]
				* (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100);
		v->GlobalDPPCLK = dml_max(
				v->GlobalDPPCLK,
				v->DPPCLK_calculated[k]);
	}
	v->GlobalDPPCLK = RoundToDFSGranularityUp(
			v->GlobalDPPCLK,
			v->DISPCLKDPPCLKVCOSpeed);
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		v->DPPCLK_calculated[k] = v->GlobalDPPCLK / 255
				* dml_ceil(
						v->DPPCLK_calculated[k] * 255.0
								/ v->GlobalDPPCLK,
						1);
		DTRACE("   dppclk_mhz[%i] (calculated) = %f", k, v->DPPCLK_calculated[k]);
		v->DPPCLK[k] = v->DPPCLK_calculated[k];
	}

	// Urgent and B P-State/DRAM Clock Change Watermark
	DTRACE("   dcfclk_mhz         = %f", v->DCFCLK);
	DTRACE("   return_bus_bw      = %f", v->ReturnBW);

	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		dml30_CalculateBytePerPixelAnd256BBlockSizes(
				v->SourcePixelFormat[k],
				v->SurfaceTiling[k],
				&v->BytePerPixelY[k],
				&v->BytePerPixelC[k],
				&v->BytePerPixelDETY[k],
				&v->BytePerPixelDETC[k],
				&v->BlockHeight256BytesY[k],
				&v->BlockHeight256BytesC[k],
				&v->BlockWidth256BytesY[k],
				&v->BlockWidth256BytesC[k]);
	}

	CalculateSwathWidth(
			false,
			v->NumberOfActivePlanes,
			v->SourcePixelFormat,
			v->SourceScan,
			v->ViewportWidth,
			v->ViewportHeight,
			v->SurfaceWidthY,
			v->SurfaceWidthC,
			v->SurfaceHeightY,
			v->SurfaceHeightC,
			v->ODMCombineEnabled,
			v->BytePerPixelY,
			v->BytePerPixelC,
			v->BlockHeight256BytesY,
			v->BlockHeight256BytesC,
			v->BlockWidth256BytesY,
			v->BlockWidth256BytesC,
			v->BlendingAndTiming,
			v->HActive,
			v->HRatio,
			v->DPPPerPlane,
			v->SwathWidthSingleDPPY,
			v->SwathWidthSingleDPPC,
			v->SwathWidthY,
			v->SwathWidthC,
			v->dummyinteger3,
			v->dummyinteger4,
			v->swath_width_luma_ub,
			v->swath_width_chroma_ub);


	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		v->ReadBandwidthPlaneLuma[k] = v->SwathWidthSingleDPPY[k] * v->BytePerPixelY[k] / (v->HTotal[k] / v->PixelClock[k]) * v->VRatio[k];
		v->ReadBandwidthPlaneChroma[k] = v->SwathWidthSingleDPPC[k] * v->BytePerPixelC[k] / (v->HTotal[k] / v->PixelClock[k]) * v->VRatioChroma[k];
		DTRACE("read_bw[%i] = %fBps", k, v->ReadBandwidthPlaneLuma[k] + v->ReadBandwidthPlaneChroma[k]);
	}


	// DCFCLK Deep Sleep
	CalculateDCFCLKDeepSleep(
			mode_lib,
			v->NumberOfActivePlanes,
			v->BytePerPixelY,
			v->BytePerPixelC,
			v->VRatio,
			v->VRatioChroma,
			v->SwathWidthY,
			v->SwathWidthC,
			v->DPPPerPlane,
			v->HRatio,
			v->HRatioChroma,
			v->PixelClock,
			v->PSCL_THROUGHPUT_LUMA,
			v->PSCL_THROUGHPUT_CHROMA,
			v->DPPCLK,
			v->ReadBandwidthPlaneLuma,
			v->ReadBandwidthPlaneChroma,
			v->ReturnBusWidth,
			&v->DCFCLKDeepSleep);

	// DSCCLK
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		if ((v->BlendingAndTiming[k] != k) || !v->DSCEnabled[k]) {
			v->DSCCLK_calculated[k] = 0.0;
		} else {
			if (v->OutputFormat[k] == dm_420)
				v->DSCFormatFactor = 2;
			else if (v->OutputFormat[k] == dm_444)
				v->DSCFormatFactor = 1;
			else if (v->OutputFormat[k] == dm_n422)
				v->DSCFormatFactor = 2;
			else
				v->DSCFormatFactor = 1;
			if (v->ODMCombineEnabled[k] == dm_odm_combine_mode_4to1)
				v->DSCCLK_calculated[k] = v->PixelClockBackEnd[k] / 12
					/ v->DSCFormatFactor / (1 - v->DISPCLKDPPCLKDSCCLKDownSpreading / 100);
			else if (v->ODMCombineEnabled[k] == dm_odm_combine_mode_2to1)
				v->DSCCLK_calculated[k] = v->PixelClockBackEnd[k] / 6
					/ v->DSCFormatFactor / (1 - v->DISPCLKDPPCLKDSCCLKDownSpreading / 100);
			else
				v->DSCCLK_calculated[k] = v->PixelClockBackEnd[k] / 3
					/ v->DSCFormatFactor / (1 - v->DISPCLKDPPCLKDSCCLKDownSpreading / 100);
		}
	}

	// DSC Delay
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		double BPP = v->OutputBppPerState[k][v->VoltageLevel];

		if (v->DSCEnabled[k] && BPP != 0) {
			if (v->ODMCombineEnabled[k] == dm_odm_combine_mode_disabled) {
				v->DSCDelay[k] = dscceComputeDelay(v->DSCInputBitPerComponent[k],
						BPP,
						dml_ceil((double) v->HActive[k] / v->NumberOfDSCSlices[k], 1),
						v->NumberOfDSCSlices[k],
						v->OutputFormat[k],
						v->Output[k])
					+ dscComputeDelay(v->OutputFormat[k], v->Output[k]);
			} else if (v->ODMCombineEnabled[k] == dm_odm_combine_mode_2to1) {
				v->DSCDelay[k] = 2 * dscceComputeDelay(v->DSCInputBitPerComponent[k],
						BPP,
						dml_ceil((double) v->HActive[k] / v->NumberOfDSCSlices[k], 1),
						v->NumberOfDSCSlices[k] / 2.0,
						v->OutputFormat[k],
						v->Output[k])
					+ dscComputeDelay(v->OutputFormat[k], v->Output[k]);
			} else {
				v->DSCDelay[k] = 4 * dscceComputeDelay(v->DSCInputBitPerComponent[k],
						BPP,
						dml_ceil((double) v->HActive[k] / v->NumberOfDSCSlices[k], 1),
						v->NumberOfDSCSlices[k] / 4.0,
						v->OutputFormat[k],
						v->Output[k])
					+ dscComputeDelay(v->OutputFormat[k], v->Output[k]);
			}
			v->DSCDelay[k] = v->DSCDelay[k] * v->PixelClock[k] / v->PixelClockBackEnd[k];
		} else {
			v->DSCDelay[k] = 0;
		}
	}

	for (k = 0; k < v->NumberOfActivePlanes; ++k)
		for (j = 0; j < v->NumberOfActivePlanes; ++j) // NumberOfPlanes
			if (j != k && v->BlendingAndTiming[k] == j
					&& v->DSCEnabled[j])
				v->DSCDelay[k] = v->DSCDelay[j];

	// Prefetch
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		unsigned int PDEAndMetaPTEBytesFrameY = 0;
		unsigned int PixelPTEBytesPerRowY = 0;
		unsigned int MetaRowByteY = 0;
		unsigned int MetaRowByteC = 0;
		unsigned int PDEAndMetaPTEBytesFrameC = 0;
		unsigned int PixelPTEBytesPerRowC = 0;
		bool         PTEBufferSizeNotExceededY = 0;
		bool         PTEBufferSizeNotExceededC = 0;


		if (v->SourcePixelFormat[k] == dm_420_8 || v->SourcePixelFormat[k] == dm_420_10 || v->SourcePixelFormat[k] == dm_420_12 || v->SourcePixelFormat[k] == dm_rgbe_alpha) {
			if ((v->SourcePixelFormat[k] == dm_420_10 || v->SourcePixelFormat[k] == dm_420_12) && v->SourceScan[k] != dm_vert) {
				v->PTEBufferSizeInRequestsForLuma = (v->PTEBufferSizeInRequestsLuma + v->PTEBufferSizeInRequestsChroma) / 2;
				v->PTEBufferSizeInRequestsForChroma = v->PTEBufferSizeInRequestsForLuma;
			} else {
				v->PTEBufferSizeInRequestsForLuma = v->PTEBufferSizeInRequestsLuma;
				v->PTEBufferSizeInRequestsForChroma = v->PTEBufferSizeInRequestsChroma;

			}
			PDEAndMetaPTEBytesFrameC = CalculateVMAndRowBytes(
					mode_lib,
					v->DCCEnable[k],
					v->BlockHeight256BytesC[k],
					v->BlockWidth256BytesC[k],
					v->SourcePixelFormat[k],
					v->SurfaceTiling[k],
					v->BytePerPixelC[k],
					v->SourceScan[k],
					v->SwathWidthC[k],
					v->ViewportHeightChroma[k],
					v->GPUVMEnable,
					v->HostVMEnable,
					v->HostVMMaxNonCachedPageTableLevels,
					v->GPUVMMinPageSize,
					v->HostVMMinPageSize,
					v->PTEBufferSizeInRequestsForChroma,
					v->PitchC[k],
					v->DCCMetaPitchC[k],
					&v->MacroTileWidthC[k],
					&MetaRowByteC,
					&PixelPTEBytesPerRowC,
					&PTEBufferSizeNotExceededC,
					&v->dpte_row_width_chroma_ub[k],
					&v->dpte_row_height_chroma[k],
					&v->meta_req_width_chroma[k],
					&v->meta_req_height_chroma[k],
					&v->meta_row_width_chroma[k],
					&v->meta_row_height_chroma[k],
					&v->dummyinteger1,
					&v->dummyinteger2,
					&v->PixelPTEReqWidthC[k],
					&v->PixelPTEReqHeightC[k],
					&v->PTERequestSizeC[k],
					&v->dpde0_bytes_per_frame_ub_c[k],
					&v->meta_pte_bytes_per_frame_ub_c[k]);

			v->PrefetchSourceLinesC[k] = CalculatePrefetchSourceLines(
					mode_lib,
					v->VRatioChroma[k],
					v->VTAPsChroma[k],
					v->Interlace[k],
					v->ProgressiveToInterlaceUnitInOPP,
					v->SwathHeightC[k],
					v->ViewportYStartC[k],
					&v->VInitPreFillC[k],
					&v->MaxNumSwathC[k]);
		} else {
			v->PTEBufferSizeInRequestsForLuma = v->PTEBufferSizeInRequestsLuma + v->PTEBufferSizeInRequestsChroma;
			v->PTEBufferSizeInRequestsForChroma = 0;
			PixelPTEBytesPerRowC = 0;
			PDEAndMetaPTEBytesFrameC = 0;
			MetaRowByteC = 0;
			v->MaxNumSwathC[k] = 0;
			v->PrefetchSourceLinesC[k] = 0;
		}

		PDEAndMetaPTEBytesFrameY = CalculateVMAndRowBytes(
				mode_lib,
				v->DCCEnable[k],
				v->BlockHeight256BytesY[k],
				v->BlockWidth256BytesY[k],
				v->SourcePixelFormat[k],
				v->SurfaceTiling[k],
				v->BytePerPixelY[k],
				v->SourceScan[k],
				v->SwathWidthY[k],
				v->ViewportHeight[k],
				v->GPUVMEnable,
				v->HostVMEnable,
				v->HostVMMaxNonCachedPageTableLevels,
				v->GPUVMMinPageSize,
				v->HostVMMinPageSize,
				v->PTEBufferSizeInRequestsForLuma,
				v->PitchY[k],
				v->DCCMetaPitchY[k],
				&v->MacroTileWidthY[k],
				&MetaRowByteY,
				&PixelPTEBytesPerRowY,
				&PTEBufferSizeNotExceededY,
				&v->dpte_row_width_luma_ub[k],
				&v->dpte_row_height[k],
				&v->meta_req_width[k],
				&v->meta_req_height[k],
				&v->meta_row_width[k],
				&v->meta_row_height[k],
				&v->vm_group_bytes[k],
				&v->dpte_group_bytes[k],
				&v->PixelPTEReqWidthY[k],
				&v->PixelPTEReqHeightY[k],
				&v->PTERequestSizeY[k],
				&v->dpde0_bytes_per_frame_ub_l[k],
				&v->meta_pte_bytes_per_frame_ub_l[k]);

		v->PrefetchSourceLinesY[k] = CalculatePrefetchSourceLines(
				mode_lib,
				v->VRatio[k],
				v->vtaps[k],
				v->Interlace[k],
				v->ProgressiveToInterlaceUnitInOPP,
				v->SwathHeightY[k],
				v->ViewportYStartY[k],
				&v->VInitPreFillY[k],
				&v->MaxNumSwathY[k]);
		v->PixelPTEBytesPerRow[k] = PixelPTEBytesPerRowY + PixelPTEBytesPerRowC;
		v->PDEAndMetaPTEBytesFrame[k] = PDEAndMetaPTEBytesFrameY
				+ PDEAndMetaPTEBytesFrameC;
		v->MetaRowByte[k] = MetaRowByteY + MetaRowByteC;

		CalculateRowBandwidth(
				v->GPUVMEnable,
				v->SourcePixelFormat[k],
				v->VRatio[k],
				v->VRatioChroma[k],
				v->DCCEnable[k],
				v->HTotal[k] / v->PixelClock[k],
				MetaRowByteY,
				MetaRowByteC,
				v->meta_row_height[k],
				v->meta_row_height_chroma[k],
				PixelPTEBytesPerRowY,
				PixelPTEBytesPerRowC,
				v->dpte_row_height[k],
				v->dpte_row_height_chroma[k],
				&v->meta_row_bw[k],
				&v->dpte_row_bw[k]);
	}

	v->TotalDCCActiveDPP = 0;
	v->TotalActiveDPP = 0;
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		v->TotalActiveDPP = v->TotalActiveDPP
				+ v->DPPPerPlane[k];
		if (v->DCCEnable[k])
			v->TotalDCCActiveDPP = v->TotalDCCActiveDPP
					+ v->DPPPerPlane[k];
	}


	ReorderBytes = v->NumberOfChannels * dml_max3(
		v->UrgentOutOfOrderReturnPerChannelPixelDataOnly,
		v->UrgentOutOfOrderReturnPerChannelPixelMixedWithVMData,
		v->UrgentOutOfOrderReturnPerChannelVMDataOnly);

	v->UrgentExtraLatency = CalculateExtraLatency(
		v->RoundTripPingLatencyCycles,
		ReorderBytes,
		v->DCFCLK,
		v->TotalActiveDPP,
		v->PixelChunkSizeInKByte,
		v->TotalDCCActiveDPP,
		v->MetaChunkSize,
		v->ReturnBW,
		v->GPUVMEnable,
		v->HostVMEnable,
		v->NumberOfActivePlanes,
		v->DPPPerPlane,
		v->dpte_group_bytes,
		v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
		v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
		v->HostVMMinPageSize,
		v->HostVMMaxNonCachedPageTableLevels);

	v->TCalc = 24.0 / v->DCFCLKDeepSleep;

	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		if (v->BlendingAndTiming[k] == k) {
			if (v->WritebackEnable[k] == true) {
				v->WritebackDelay[v->VoltageLevel][k] = v->WritebackLatency +
						CalculateWriteBackDelay(v->WritebackPixelFormat[k],
									v->WritebackHRatio[k],
									v->WritebackVRatio[k],
									v->WritebackVTaps[k],
									v->WritebackDestinationWidth[k],
									v->WritebackDestinationHeight[k],
									v->WritebackSourceHeight[k],
									v->HTotal[k]) / v->DISPCLK;
			} else
				v->WritebackDelay[v->VoltageLevel][k] = 0;
			for (j = 0; j < v->NumberOfActivePlanes; ++j) {
				if (v->BlendingAndTiming[j] == k
						&& v->WritebackEnable[j] == true) {
					v->WritebackDelay[v->VoltageLevel][k] = dml_max(v->WritebackDelay[v->VoltageLevel][k],
							v->WritebackLatency + CalculateWriteBackDelay(
											v->WritebackPixelFormat[j],
											v->WritebackHRatio[j],
											v->WritebackVRatio[j],
											v->WritebackVTaps[j],
											v->WritebackDestinationWidth[j],
											v->WritebackDestinationHeight[j],
											v->WritebackSourceHeight[j],
											v->HTotal[k]) / v->DISPCLK);
				}
			}
		}
	}

	for (k = 0; k < v->NumberOfActivePlanes; ++k)
		for (j = 0; j < v->NumberOfActivePlanes; ++j)
			if (v->BlendingAndTiming[k] == j)
				v->WritebackDelay[v->VoltageLevel][k] = v->WritebackDelay[v->VoltageLevel][j];

	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		v->MaxVStartupLines[k] = v->VTotal[k] - v->VActive[k] - dml_max(1.0, dml_ceil((double) v->WritebackDelay[v->VoltageLevel][k] / (v->HTotal[k] / v->PixelClock[k]), 1));
	}

	v->MaximumMaxVStartupLines = 0;
	for (k = 0; k < v->NumberOfActivePlanes; ++k)
		v->MaximumMaxVStartupLines = dml_max(v->MaximumMaxVStartupLines, v->MaxVStartupLines[k]);

	if (v->DRAMClockChangeLatencyOverride > 0.0) {
		v->FinalDRAMClockChangeLatency = v->DRAMClockChangeLatencyOverride;
	} else {
		v->FinalDRAMClockChangeLatency = v->DRAMClockChangeLatency;
	}
	v->UrgentLatency = CalculateUrgentLatency(v->UrgentLatencyPixelDataOnly, v->UrgentLatencyPixelMixedWithVMData, v->UrgentLatencyVMDataOnly, v->DoUrgentLatencyAdjustment, v->UrgentLatencyAdjustmentFabricClockComponent, v->UrgentLatencyAdjustmentFabricClockReference, v->FabricClock);


	v->FractionOfUrgentBandwidth = 0.0;
	v->FractionOfUrgentBandwidthImmediateFlip = 0.0;

	v->VStartupLines = 13;

	do {
		MaxTotalRDBandwidth = 0;
		MaxTotalRDBandwidthNoUrgentBurst = 0;
		DestinationLineTimesForPrefetchLessThan2 = false;
		VRatioPrefetchMoreThan4 = false;
		TWait = CalculateTWait(
				PrefetchMode,
				v->FinalDRAMClockChangeLatency,
				v->UrgentLatency,
				v->SREnterPlusExitTime);

		for (k = 0; k < v->NumberOfActivePlanes; ++k) {
			Pipe myPipe = { 0 };

			myPipe.DPPCLK = v->DPPCLK[k];
			myPipe.DISPCLK = v->DISPCLK;
			myPipe.PixelClock = v->PixelClock[k];
			myPipe.DCFCLKDeepSleep = v->DCFCLKDeepSleep;
			myPipe.DPPPerPlane = v->DPPPerPlane[k];
			myPipe.ScalerEnabled = v->ScalerEnabled[k];
			myPipe.SourceScan = v->SourceScan[k];
			myPipe.BlockWidth256BytesY = v->BlockWidth256BytesY[k];
			myPipe.BlockHeight256BytesY = v->BlockHeight256BytesY[k];
			myPipe.BlockWidth256BytesC = v->BlockWidth256BytesC[k];
			myPipe.BlockHeight256BytesC = v->BlockHeight256BytesC[k];
			myPipe.InterlaceEnable = v->Interlace[k];
			myPipe.NumberOfCursors = v->NumberOfCursors[k];
			myPipe.VBlank = v->VTotal[k] - v->VActive[k];
			myPipe.HTotal = v->HTotal[k];
			myPipe.DCCEnable = v->DCCEnable[k];
			myPipe.ODMCombineEnabled = !!v->ODMCombineEnabled[k];

			v->ErrorResult[k] = CalculatePrefetchSchedule(
					mode_lib,
					v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
					v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
					&myPipe,
					v->DSCDelay[k],
					v->DPPCLKDelaySubtotal
							+ v->DPPCLKDelayCNVCFormater,
					v->DPPCLKDelaySCL,
					v->DPPCLKDelaySCLLBOnly,
					v->DPPCLKDelayCNVCCursor,
					v->DISPCLKDelaySubtotal,
					(unsigned int) (v->SwathWidthY[k] / v->HRatio[k]),
					v->OutputFormat[k],
					v->MaxInterDCNTileRepeaters,
					dml_min(v->VStartupLines, v->MaxVStartupLines[k]),
					v->MaxVStartupLines[k],
					v->GPUVMMaxPageTableLevels,
					v->GPUVMEnable,
					v->HostVMEnable,
					v->HostVMMaxNonCachedPageTableLevels,
					v->HostVMMinPageSize,
					v->DynamicMetadataEnable[k],
					v->DynamicMetadataVMEnabled,
					v->DynamicMetadataLinesBeforeActiveRequired[k],
					v->DynamicMetadataTransmittedBytes[k],
					v->UrgentLatency,
					v->UrgentExtraLatency,
					v->TCalc,
					v->PDEAndMetaPTEBytesFrame[k],
					v->MetaRowByte[k],
					v->PixelPTEBytesPerRow[k],
					v->PrefetchSourceLinesY[k],
					v->SwathWidthY[k],
					v->BytePerPixelY[k],
					v->VInitPreFillY[k],
					v->MaxNumSwathY[k],
					v->PrefetchSourceLinesC[k],
					v->SwathWidthC[k],
					v->BytePerPixelC[k],
					v->VInitPreFillC[k],
					v->MaxNumSwathC[k],
					v->swath_width_luma_ub[k],
					v->swath_width_chroma_ub[k],
					v->SwathHeightY[k],
					v->SwathHeightC[k],
					TWait,
					v->ProgressiveToInterlaceUnitInOPP,
					&v->DSTXAfterScaler[k],
					&v->DSTYAfterScaler[k],
					&v->DestinationLinesForPrefetch[k],
					&v->PrefetchBandwidth[k],
					&v->DestinationLinesToRequestVMInVBlank[k],
					&v->DestinationLinesToRequestRowInVBlank[k],
					&v->VRatioPrefetchY[k],
					&v->VRatioPrefetchC[k],
					&v->RequiredPrefetchPixDataBWLuma[k],
					&v->RequiredPrefetchPixDataBWChroma[k],
					&v->NotEnoughTimeForDynamicMetadata[k],
					&v->Tno_bw[k],
					&v->prefetch_vmrow_bw[k],
					&v->Tdmdl_vm[k],
					&v->Tdmdl[k],
					&v->VUpdateOffsetPix[k],
					&v->VUpdateWidthPix[k],
					&v->VReadyOffsetPix[k]);
			if (v->BlendingAndTiming[k] == k) {
				double TotalRepeaterDelayTime = v->MaxInterDCNTileRepeaters * (2 / v->DPPCLK[k] + 3 / v->DISPCLK);
				v->VUpdateWidthPix[k] = (14 / v->DCFCLKDeepSleep + 12 / v->DPPCLK[k] + TotalRepeaterDelayTime) * v->PixelClock[k];
				v->VReadyOffsetPix[k] = dml_max(150.0 / v->DPPCLK[k], TotalRepeaterDelayTime + 20 / v->DCFCLKDeepSleep + 10 / v->DPPCLK[k]) * v->PixelClock[k];
				v->VUpdateOffsetPix[k] = dml_ceil(v->HTotal[k] / 4.0, 1);
				v->VStartup[k] = dml_min(v->VStartupLines, v->MaxVStartupLines[k]);
			} else {
				int x = v->BlendingAndTiming[k];
				double TotalRepeaterDelayTime = v->MaxInterDCNTileRepeaters * (2 / v->DPPCLK[k] + 3 / v->DISPCLK);
				v->VUpdateWidthPix[k] = (14 / v->DCFCLKDeepSleep + 12 / v->DPPCLK[k] + TotalRepeaterDelayTime) * v->PixelClock[x];
				v->VReadyOffsetPix[k] = dml_max(150.0 / v->DPPCLK[k], TotalRepeaterDelayTime + 20 / v->DCFCLKDeepSleep + 10 / v->DPPCLK[k]) * v->PixelClock[x];
				v->VUpdateOffsetPix[k] = dml_ceil(v->HTotal[x] / 4.0, 1);
				if (!v->MaxVStartupLines[x])
					v->MaxVStartupLines[x] = v->MaxVStartupLines[k];
				v->VStartup[k] = dml_min(v->VStartupLines, v->MaxVStartupLines[x]);
			}
		}

		v->NotEnoughUrgentLatencyHiding[0][0] = false;
		v->NotEnoughUrgentLatencyHidingPre = false;

		for (k = 0; k < v->NumberOfActivePlanes; ++k) {
			v->cursor_bw[k] = v->NumberOfCursors[k]
					* v->CursorWidth[k][0] * v->CursorBPP[k][0]
					/ 8.0
					/ (v->HTotal[k] / v->PixelClock[k])
					* v->VRatio[k];
			v->cursor_bw_pre[k] = v->NumberOfCursors[k]
					* v->CursorWidth[k][0] * v->CursorBPP[k][0]
					/ 8.0
					/ (v->HTotal[k] / v->PixelClock[k])
					* v->VRatioPrefetchY[k];

			CalculateUrgentBurstFactor(
					v->swath_width_luma_ub[k],
					v->swath_width_chroma_ub[k],
					v->DETBufferSizeInKByte[0],
					v->SwathHeightY[k],
					v->SwathHeightC[k],
					v->HTotal[k] / v->PixelClock[k],
					v->UrgentLatency,
					v->CursorBufferSize,
					v->CursorWidth[k][0],
					v->CursorBPP[k][0],
					v->VRatio[k],
					v->VRatioChroma[k],
					v->BytePerPixelDETY[k],
					v->BytePerPixelDETC[k],
					v->DETBufferSizeY[k],
					v->DETBufferSizeC[k],
					&v->UrgentBurstFactorCursor[k],
					&v->UrgentBurstFactorLuma[k],
					&v->UrgentBurstFactorChroma[k],
					&v->NoUrgentLatencyHiding[k]);

			CalculateUrgentBurstFactor(
					v->swath_width_luma_ub[k],
					v->swath_width_chroma_ub[k],
					v->DETBufferSizeInKByte[0],
					v->SwathHeightY[k],
					v->SwathHeightC[k],
					v->HTotal[k] / v->PixelClock[k],
					v->UrgentLatency,
					v->CursorBufferSize,
					v->CursorWidth[k][0],
					v->CursorBPP[k][0],
					v->VRatioPrefetchY[k],
					v->VRatioPrefetchC[k],
					v->BytePerPixelDETY[k],
					v->BytePerPixelDETC[k],
					v->DETBufferSizeY[k],
					v->DETBufferSizeC[k],
					&v->UrgentBurstFactorCursorPre[k],
					&v->UrgentBurstFactorLumaPre[k],
					&v->UrgentBurstFactorChromaPre[k],
					&v->NoUrgentLatencyHidingPre[k]);

			MaxTotalRDBandwidth = MaxTotalRDBandwidth +
				dml_max3(v->DPPPerPlane[k] * v->prefetch_vmrow_bw[k],
					v->ReadBandwidthPlaneLuma[k] *
					v->UrgentBurstFactorLuma[k] +
					v->ReadBandwidthPlaneChroma[k] *
					v->UrgentBurstFactorChroma[k] +
					v->cursor_bw[k] *
					v->UrgentBurstFactorCursor[k] +
					v->DPPPerPlane[k] * (v->meta_row_bw[k] + v->dpte_row_bw[k]),
					v->DPPPerPlane[k] * (v->RequiredPrefetchPixDataBWLuma[k] * v->UrgentBurstFactorLumaPre[k] +
						v->RequiredPrefetchPixDataBWChroma[k] * v->UrgentBurstFactorChromaPre[k]) + v->cursor_bw_pre[k] *
					v->UrgentBurstFactorCursorPre[k]);

			MaxTotalRDBandwidthNoUrgentBurst = MaxTotalRDBandwidthNoUrgentBurst +
				dml_max3(v->DPPPerPlane[k] * v->prefetch_vmrow_bw[k],
					v->ReadBandwidthPlaneLuma[k] +
					v->ReadBandwidthPlaneChroma[k] +
					v->cursor_bw[k] +
					v->DPPPerPlane[k] * (v->meta_row_bw[k] + v->dpte_row_bw[k]),
					v->DPPPerPlane[k] * (v->RequiredPrefetchPixDataBWLuma[k] + v->RequiredPrefetchPixDataBWChroma[k]) + v->cursor_bw_pre[k]);

			if (v->DestinationLinesForPrefetch[k] < 2)
				DestinationLineTimesForPrefetchLessThan2 = true;
			if (v->VRatioPrefetchY[k] > 4 || v->VRatioPrefetchC[k] > 4)
				VRatioPrefetchMoreThan4 = true;
			if (v->NoUrgentLatencyHiding[k] == true)
				v->NotEnoughUrgentLatencyHiding[0][0] = true;

			if (v->NoUrgentLatencyHidingPre[k] == true)
				v->NotEnoughUrgentLatencyHidingPre = true;
		}
		v->FractionOfUrgentBandwidth = MaxTotalRDBandwidthNoUrgentBurst / v->ReturnBW;


		if (MaxTotalRDBandwidth <= v->ReturnBW && v->NotEnoughUrgentLatencyHiding[0][0] == 0
				&& v->NotEnoughUrgentLatencyHidingPre == 0 && !VRatioPrefetchMoreThan4
				&& !DestinationLineTimesForPrefetchLessThan2)
			v->PrefetchModeSupported = true;
		else {
			v->PrefetchModeSupported = false;
			dml_print("DML: CalculatePrefetchSchedule ***failed***. Bandwidth violation. Results are NOT valid\n");
			dml_print("DML: MaxTotalRDBandwidth:%f AvailReturnBandwidth:%f\n", MaxTotalRDBandwidth, v->ReturnBW);
			dml_print("DML: VRatioPrefetch %s more than 4\n", (VRatioPrefetchMoreThan4) ? "is" : "is not");
			dml_print("DML: DestinationLines for Prefetch %s less than 2\n", (DestinationLineTimesForPrefetchLessThan2) ? "is" : "is not");
		}

		if (v->PrefetchModeSupported == true && v->ImmediateFlipSupport == true) {
			v->BandwidthAvailableForImmediateFlip = v->ReturnBW;
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				v->BandwidthAvailableForImmediateFlip =
						v->BandwidthAvailableForImmediateFlip
								- dml_max(
										v->ReadBandwidthPlaneLuma[k] * v->UrgentBurstFactorLuma[k]
												+ v->ReadBandwidthPlaneChroma[k] * v->UrgentBurstFactorChroma[k]
												+ v->cursor_bw[k] * v->UrgentBurstFactorCursor[k],
										v->DPPPerPlane[k] * (v->RequiredPrefetchPixDataBWLuma[k] * v->UrgentBurstFactorLumaPre[k] +
										v->RequiredPrefetchPixDataBWChroma[k] * v->UrgentBurstFactorChromaPre[k]) +
										v->cursor_bw_pre[k] * v->UrgentBurstFactorCursorPre[k]);
			}

			v->TotImmediateFlipBytes = 0;
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				v->TotImmediateFlipBytes = v->TotImmediateFlipBytes + v->DPPPerPlane[k] * (v->PDEAndMetaPTEBytesFrame[k] + v->MetaRowByte[k] + v->PixelPTEBytesPerRow[k]);
			}
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				CalculateFlipSchedule(
						mode_lib,
						v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
						v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
						v->UrgentExtraLatency,
						v->UrgentLatency,
						v->GPUVMMaxPageTableLevels,
						v->HostVMEnable,
						v->HostVMMaxNonCachedPageTableLevels,
						v->GPUVMEnable,
						v->HostVMMinPageSize,
						v->PDEAndMetaPTEBytesFrame[k],
						v->MetaRowByte[k],
						v->PixelPTEBytesPerRow[k],
						v->BandwidthAvailableForImmediateFlip,
						v->TotImmediateFlipBytes,
						v->SourcePixelFormat[k],
						v->HTotal[k] / v->PixelClock[k],
						v->VRatio[k],
						v->VRatioChroma[k],
						v->Tno_bw[k],
						v->DCCEnable[k],
						v->dpte_row_height[k],
						v->meta_row_height[k],
						v->dpte_row_height_chroma[k],
						v->meta_row_height_chroma[k],
						&v->DestinationLinesToRequestVMInImmediateFlip[k],
						&v->DestinationLinesToRequestRowInImmediateFlip[k],
						&v->final_flip_bw[k],
						&v->ImmediateFlipSupportedForPipe[k]);
			}
			v->total_dcn_read_bw_with_flip = 0.0;
			v->total_dcn_read_bw_with_flip_no_urgent_burst = 0.0;
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				v->total_dcn_read_bw_with_flip = v->total_dcn_read_bw_with_flip + dml_max3(
					v->DPPPerPlane[k] * v->prefetch_vmrow_bw[k],
					v->DPPPerPlane[k] * v->final_flip_bw[k] +
					v->ReadBandwidthLuma[k] * v->UrgentBurstFactorLuma[k] +
					v->ReadBandwidthChroma[k] * v->UrgentBurstFactorChroma[k] +
					v->cursor_bw[k] * v->UrgentBurstFactorCursor[k],
					v->DPPPerPlane[k] * (v->final_flip_bw[k] +
					v->RequiredPrefetchPixDataBWLuma[k] * v->UrgentBurstFactorLumaPre[k] +
					v->RequiredPrefetchPixDataBWChroma[k] * v->UrgentBurstFactorChromaPre[k]) +
					v->cursor_bw_pre[k] * v->UrgentBurstFactorCursorPre[k]);
				v->total_dcn_read_bw_with_flip_no_urgent_burst =
					v->total_dcn_read_bw_with_flip_no_urgent_burst +
						dml_max3(v->DPPPerPlane[k] * v->prefetch_vmrow_bw[k],
							v->DPPPerPlane[k] * v->final_flip_bw[k] + v->ReadBandwidthPlaneLuma[k] + v->ReadBandwidthPlaneChroma[k] + v->cursor_bw[k],
							v->DPPPerPlane[k] * (v->final_flip_bw[k] + v->RequiredPrefetchPixDataBWLuma[k] + v->RequiredPrefetchPixDataBWChroma[k]) + v->cursor_bw_pre[k]);

			}
			v->FractionOfUrgentBandwidthImmediateFlip = v->total_dcn_read_bw_with_flip_no_urgent_burst / v->ReturnBW;

			v->ImmediateFlipSupported = true;
			if (v->total_dcn_read_bw_with_flip > v->ReturnBW) {
				v->ImmediateFlipSupported = false;
				v->total_dcn_read_bw_with_flip = MaxTotalRDBandwidth;
			}
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				if (v->ImmediateFlipSupportedForPipe[k] == false) {
					v->ImmediateFlipSupported = false;
				}
			}
		} else {
			v->ImmediateFlipSupported = false;
		}

		for (k = 0; k < v->NumberOfActivePlanes; ++k) {
			if (v->ErrorResult[k] || v->NotEnoughTimeForDynamicMetadata[k]) {
				v->PrefetchModeSupported = false;
				dml_print("DML: CalculatePrefetchSchedule ***failed***. Prefetch schedule violation. Results are NOT valid\n");
			}
		}

		v->VStartupLines = v->VStartupLines + 1;
		v->PrefetchModeSupported = (v->PrefetchModeSupported == true && ((!v->ImmediateFlipSupport &&
				!v->HostVMEnable && v->ImmediateFlipRequirement[0] != dm_immediate_flip_required) ||
				v->ImmediateFlipSupported)) ? true : false;
	} while (!v->PrefetchModeSupported && v->VStartupLines <= v->MaximumMaxVStartupLines);
	ASSERT(v->PrefetchModeSupported);

	//Watermarks and NB P-State/DRAM Clock Change Support
	{
		enum clock_change_support   DRAMClockChangeSupport = 0; // dummy
		CalculateWatermarksAndDRAMSpeedChangeSupport(
			mode_lib,
			PrefetchMode,
			v->NumberOfActivePlanes,
			v->MaxLineBufferLines,
			v->LineBufferSize,
			v->DPPOutputBufferPixels,
			v->DETBufferSizeInKByte[0],
			v->WritebackInterfaceBufferSize,
			v->DCFCLK,
			v->ReturnBW,
			v->GPUVMEnable,
			v->dpte_group_bytes,
			v->MetaChunkSize,
			v->UrgentLatency,
			v->UrgentExtraLatency,
			v->WritebackLatency,
			v->WritebackChunkSize,
			v->SOCCLK,
			v->FinalDRAMClockChangeLatency,
			v->SRExitTime,
			v->SREnterPlusExitTime,
			v->DCFCLKDeepSleep,
			v->DPPPerPlane,
			v->DCCEnable,
			v->DPPCLK,
			v->DETBufferSizeY,
			v->DETBufferSizeC,
			v->SwathHeightY,
			v->SwathHeightC,
			v->LBBitPerPixel,
			v->SwathWidthY,
			v->SwathWidthC,
			v->HRatio,
			v->HRatioChroma,
			v->vtaps,
			v->VTAPsChroma,
			v->VRatio,
			v->VRatioChroma,
			v->HTotal,
			v->PixelClock,
			v->BlendingAndTiming,
			v->BytePerPixelDETY,
			v->BytePerPixelDETC,
			v->DSTXAfterScaler,
			v->DSTYAfterScaler,
			v->WritebackEnable,
			v->WritebackPixelFormat,
			v->WritebackDestinationWidth,
			v->WritebackDestinationHeight,
			v->WritebackSourceHeight,
			&DRAMClockChangeSupport,
			&v->UrgentWatermark,
			&v->WritebackUrgentWatermark,
			&v->DRAMClockChangeWatermark,
			&v->WritebackDRAMClockChangeWatermark,
			&v->StutterExitWatermark,
			&v->StutterEnterPlusExitWatermark,
			&v->MinActiveDRAMClockChangeLatencySupported);

		for (k = 0; k < v->NumberOfActivePlanes; ++k) {
			if (v->WritebackEnable[k] == true) {
				if (v->BlendingAndTiming[k] == k) {
					v->ThisVStartup = v->VStartup[k];
				} else {
					for (j = 0; j < v->NumberOfActivePlanes; ++j) {
						if (v->BlendingAndTiming[k] == j) {
							v->ThisVStartup = v->VStartup[j];
						}
					}
				}
				v->WritebackAllowDRAMClockChangeEndPosition[k] = dml_max(0,
					v->ThisVStartup * v->HTotal[k] / v->PixelClock[k] - v->WritebackDRAMClockChangeWatermark);
			} else {
				v->WritebackAllowDRAMClockChangeEndPosition[k] = 0;
			}
		}

	}


	//Display Pipeline Delivery Time in Prefetch, Groups
	CalculatePixelDeliveryTimes(
			v->NumberOfActivePlanes,
			v->VRatio,
			v->VRatioChroma,
			v->VRatioPrefetchY,
			v->VRatioPrefetchC,
			v->swath_width_luma_ub,
			v->swath_width_chroma_ub,
			v->DPPPerPlane,
			v->HRatio,
			v->HRatioChroma,
			v->PixelClock,
			v->PSCL_THROUGHPUT_LUMA,
			v->PSCL_THROUGHPUT_CHROMA,
			v->DPPCLK,
			v->BytePerPixelC,
			v->SourceScan,
			v->NumberOfCursors,
			v->CursorWidth,
			v->CursorBPP,
			v->BlockWidth256BytesY,
			v->BlockHeight256BytesY,
			v->BlockWidth256BytesC,
			v->BlockHeight256BytesC,
			v->DisplayPipeLineDeliveryTimeLuma,
			v->DisplayPipeLineDeliveryTimeChroma,
			v->DisplayPipeLineDeliveryTimeLumaPrefetch,
			v->DisplayPipeLineDeliveryTimeChromaPrefetch,
			v->DisplayPipeRequestDeliveryTimeLuma,
			v->DisplayPipeRequestDeliveryTimeChroma,
			v->DisplayPipeRequestDeliveryTimeLumaPrefetch,
			v->DisplayPipeRequestDeliveryTimeChromaPrefetch,
			v->CursorRequestDeliveryTime,
			v->CursorRequestDeliveryTimePrefetch);

	CalculateMetaAndPTETimes(
			v->NumberOfActivePlanes,
			v->GPUVMEnable,
			v->MetaChunkSize,
			v->MinMetaChunkSizeBytes,
			v->HTotal,
			v->VRatio,
			v->VRatioChroma,
			v->DestinationLinesToRequestRowInVBlank,
			v->DestinationLinesToRequestRowInImmediateFlip,
			v->DCCEnable,
			v->PixelClock,
			v->BytePerPixelY,
			v->BytePerPixelC,
			v->SourceScan,
			v->dpte_row_height,
			v->dpte_row_height_chroma,
			v->meta_row_width,
			v->meta_row_width_chroma,
			v->meta_row_height,
			v->meta_row_height_chroma,
			v->meta_req_width,
			v->meta_req_width_chroma,
			v->meta_req_height,
			v->meta_req_height_chroma,
			v->dpte_group_bytes,
			v->PTERequestSizeY,
			v->PTERequestSizeC,
			v->PixelPTEReqWidthY,
			v->PixelPTEReqHeightY,
			v->PixelPTEReqWidthC,
			v->PixelPTEReqHeightC,
			v->dpte_row_width_luma_ub,
			v->dpte_row_width_chroma_ub,
			v->DST_Y_PER_PTE_ROW_NOM_L,
			v->DST_Y_PER_PTE_ROW_NOM_C,
			v->DST_Y_PER_META_ROW_NOM_L,
			v->DST_Y_PER_META_ROW_NOM_C,
			v->TimePerMetaChunkNominal,
			v->TimePerChromaMetaChunkNominal,
			v->TimePerMetaChunkVBlank,
			v->TimePerChromaMetaChunkVBlank,
			v->TimePerMetaChunkFlip,
			v->TimePerChromaMetaChunkFlip,
			v->time_per_pte_group_nom_luma,
			v->time_per_pte_group_vblank_luma,
			v->time_per_pte_group_flip_luma,
			v->time_per_pte_group_nom_chroma,
			v->time_per_pte_group_vblank_chroma,
			v->time_per_pte_group_flip_chroma);

	CalculateVMGroupAndRequestTimes(
			v->NumberOfActivePlanes,
			v->GPUVMEnable,
			v->GPUVMMaxPageTableLevels,
			v->HTotal,
			v->BytePerPixelC,
			v->DestinationLinesToRequestVMInVBlank,
			v->DestinationLinesToRequestVMInImmediateFlip,
			v->DCCEnable,
			v->PixelClock,
			v->dpte_row_width_luma_ub,
			v->dpte_row_width_chroma_ub,
			v->vm_group_bytes,
			v->dpde0_bytes_per_frame_ub_l,
			v->dpde0_bytes_per_frame_ub_c,
			v->meta_pte_bytes_per_frame_ub_l,
			v->meta_pte_bytes_per_frame_ub_c,
			v->TimePerVMGroupVBlank,
			v->TimePerVMGroupFlip,
			v->TimePerVMRequestVBlank,
			v->TimePerVMRequestFlip);


	// Min TTUVBlank
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		if (PrefetchMode == 0) {
			v->AllowDRAMClockChangeDuringVBlank[k] = true;
			v->AllowDRAMSelfRefreshDuringVBlank[k] = true;
			v->MinTTUVBlank[k] = dml_max(
					v->DRAMClockChangeWatermark,
					dml_max(
							v->StutterEnterPlusExitWatermark,
							v->UrgentWatermark));
		} else if (PrefetchMode == 1) {
			v->AllowDRAMClockChangeDuringVBlank[k] = false;
			v->AllowDRAMSelfRefreshDuringVBlank[k] = true;
			v->MinTTUVBlank[k] = dml_max(
					v->StutterEnterPlusExitWatermark,
					v->UrgentWatermark);
		} else {
			v->AllowDRAMClockChangeDuringVBlank[k] = false;
			v->AllowDRAMSelfRefreshDuringVBlank[k] = false;
			v->MinTTUVBlank[k] = v->UrgentWatermark;
		}
		if (!v->DynamicMetadataEnable[k])
			v->MinTTUVBlank[k] = v->TCalc
					+ v->MinTTUVBlank[k];
	}

	// DCC Configuration
	v->ActiveDPPs = 0;
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		CalculateDCCConfiguration(v->DCCEnable[k], false, // We should always know the direction DCCProgrammingAssumesScanDirectionUnknown,
				v->SourcePixelFormat[k],
				v->SurfaceWidthY[k],
				v->SurfaceWidthC[k],
				v->SurfaceHeightY[k],
				v->SurfaceHeightC[k],
				v->DETBufferSizeInKByte[0] * 1024,
				v->BlockHeight256BytesY[k],
				v->BlockHeight256BytesC[k],
				v->SurfaceTiling[k],
				v->BytePerPixelY[k],
				v->BytePerPixelC[k],
				v->BytePerPixelDETY[k],
				v->BytePerPixelDETC[k],
				v->SourceScan[k],
				&v->DCCYMaxUncompressedBlock[k],
				&v->DCCCMaxUncompressedBlock[k],
				&v->DCCYMaxCompressedBlock[k],
				&v->DCCCMaxCompressedBlock[k],
				&v->DCCYIndependentBlock[k],
				&v->DCCCIndependentBlock[k]);
	}

	{
		//Maximum Bandwidth Used
		v->TotalDataReadBandwidth = 0;
		for (k = 0; k < v->NumberOfActivePlanes; ++k) {
			v->TotalDataReadBandwidth = v->TotalDataReadBandwidth
					+ v->ReadBandwidthPlaneLuma[k]
					+ v->ReadBandwidthPlaneChroma[k];
		}
	}

	// VStartup Margin
	v->VStartupMargin = 0;
	v->FirstMainPlane = true;
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		if (v->BlendingAndTiming[k] == k) {
			double margin = (v->MaxVStartupLines[k] - v->VStartup[k]) * v->HTotal[k]
					/ v->PixelClock[k];
			if (v->FirstMainPlane == true) {
				v->VStartupMargin = margin;
				v->FirstMainPlane = false;
			} else {
				v->VStartupMargin = dml_min(v->VStartupMargin, margin);
			}
		}
	}

	// Stutter Efficiency
	CalculateStutterEfficiency(
			v->NumberOfActivePlanes,
			v->ROBBufferSizeInKByte,
			v->TotalDataReadBandwidth,
			v->DCFCLK,
			v->ReturnBW,
			v->SRExitTime,
			v->SynchronizedVBlank,
			v->DPPPerPlane,
			v->DETBufferSizeY,
			v->BytePerPixelY,
			v->BytePerPixelDETY,
			v->SwathWidthY,
			v->SwathHeightY,
			v->SwathHeightC,
			v->DCCRateLuma,
			v->DCCRateChroma,
			v->HTotal,
			v->VTotal,
			v->PixelClock,
			v->VRatio,
			v->SourceScan,
			v->BlockHeight256BytesY,
			v->BlockWidth256BytesY,
			v->BlockHeight256BytesC,
			v->BlockWidth256BytesC,
			v->DCCYMaxUncompressedBlock,
			v->DCCCMaxUncompressedBlock,
			v->VActive,
			v->DCCEnable,
			v->WritebackEnable,
			v->ReadBandwidthPlaneLuma,
			v->ReadBandwidthPlaneChroma,
			v->meta_row_bw,
			v->dpte_row_bw,
			&v->StutterEfficiencyNotIncludingVBlank,
			&v->StutterEfficiency,
			&v->StutterPeriod);
}

static void DisplayPipeConfiguration(struct display_mode_lib *mode_lib)
{
	// Display Pipe Configuration
	double BytePerPixDETY[DC__NUM_DPP__MAX] = { 0 };
	double BytePerPixDETC[DC__NUM_DPP__MAX] = { 0 };
	int BytePerPixY[DC__NUM_DPP__MAX] = { 0 };
	int BytePerPixC[DC__NUM_DPP__MAX] = { 0 };
	int Read256BytesBlockHeightY[DC__NUM_DPP__MAX] = { 0 };
	int Read256BytesBlockHeightC[DC__NUM_DPP__MAX] = { 0 };
	int Read256BytesBlockWidthY[DC__NUM_DPP__MAX] = { 0 };
	int Read256BytesBlockWidthC[DC__NUM_DPP__MAX] = { 0 };
	double dummy1[DC__NUM_DPP__MAX] = { 0 };
	double dummy2[DC__NUM_DPP__MAX] = { 0 };
	double dummy3[DC__NUM_DPP__MAX] = { 0 };
	double dummy4[DC__NUM_DPP__MAX] = { 0 };
	int dummy5[DC__NUM_DPP__MAX] = { 0 };
	int dummy6[DC__NUM_DPP__MAX] = { 0 };
	bool dummy7[DC__NUM_DPP__MAX] = { 0 };
	bool dummysinglestring = 0;
	unsigned int k;

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {

		dml30_CalculateBytePerPixelAnd256BBlockSizes(
				mode_lib->vba.SourcePixelFormat[k],
				mode_lib->vba.SurfaceTiling[k],
				&BytePerPixY[k],
				&BytePerPixC[k],
				&BytePerPixDETY[k],
				&BytePerPixDETC[k],
				&Read256BytesBlockHeightY[k],
				&Read256BytesBlockHeightC[k],
				&Read256BytesBlockWidthY[k],
				&Read256BytesBlockWidthC[k]);
	}
	CalculateSwathAndDETConfiguration(
			false,
			mode_lib->vba.NumberOfActivePlanes,
			mode_lib->vba.DETBufferSizeInKByte[0],
			dummy1,
			dummy2,
			mode_lib->vba.SourceScan,
			mode_lib->vba.SourcePixelFormat,
			mode_lib->vba.SurfaceTiling,
			mode_lib->vba.ViewportWidth,
			mode_lib->vba.ViewportHeight,
			mode_lib->vba.SurfaceWidthY,
			mode_lib->vba.SurfaceWidthC,
			mode_lib->vba.SurfaceHeightY,
			mode_lib->vba.SurfaceHeightC,
			Read256BytesBlockHeightY,
			Read256BytesBlockHeightC,
			Read256BytesBlockWidthY,
			Read256BytesBlockWidthC,
			mode_lib->vba.ODMCombineEnabled,
			mode_lib->vba.BlendingAndTiming,
			BytePerPixY,
			BytePerPixC,
			BytePerPixDETY,
			BytePerPixDETC,
			mode_lib->vba.HActive,
			mode_lib->vba.HRatio,
			mode_lib->vba.HRatioChroma,
			mode_lib->vba.DPPPerPlane,
			dummy5,
			dummy6,
			dummy3,
			dummy4,
			mode_lib->vba.SwathHeightY,
			mode_lib->vba.SwathHeightC,
			mode_lib->vba.DETBufferSizeY,
			mode_lib->vba.DETBufferSizeC,
			dummy7,
			&dummysinglestring);
}

void dml30_CalculateBytePerPixelAnd256BBlockSizes(
		enum source_format_class SourcePixelFormat,
		enum dm_swizzle_mode SurfaceTiling,
		unsigned int *BytePerPixelY,
		unsigned int *BytePerPixelC,
		double       *BytePerPixelDETY,
		double       *BytePerPixelDETC,
		unsigned int *BlockHeight256BytesY,
		unsigned int *BlockHeight256BytesC,
		unsigned int *BlockWidth256BytesY,
		unsigned int *BlockWidth256BytesC)
{
	if (SourcePixelFormat == dm_444_64) {
		*BytePerPixelDETY = 8;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 8;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dm_444_32 || SourcePixelFormat == dm_rgbe) {
		*BytePerPixelDETY = 4;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 4;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dm_444_16) {
		*BytePerPixelDETY = 2;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 2;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dm_444_8) {
		*BytePerPixelDETY = 1;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 1;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dm_rgbe_alpha) {
		*BytePerPixelDETY = 4;
		*BytePerPixelDETC = 1;
		*BytePerPixelY = 4;
		*BytePerPixelC = 1;
	} else if (SourcePixelFormat == dm_420_8) {
		*BytePerPixelDETY = 1;
		*BytePerPixelDETC = 2;
		*BytePerPixelY = 1;
		*BytePerPixelC = 2;
	} else if (SourcePixelFormat == dm_420_12) {
		*BytePerPixelDETY = 2;
		*BytePerPixelDETC = 4;
		*BytePerPixelY = 2;
		*BytePerPixelC = 4;
	} else {
		*BytePerPixelDETY = 4.0 / 3;
		*BytePerPixelDETC = 8.0 / 3;
		*BytePerPixelY = 2;
		*BytePerPixelC = 4;
	}

	if ((SourcePixelFormat == dm_444_64 || SourcePixelFormat == dm_444_32
			|| SourcePixelFormat == dm_444_16 || SourcePixelFormat == dm_444_8
			|| SourcePixelFormat == dm_mono_16 || SourcePixelFormat == dm_mono_8
			|| SourcePixelFormat == dm_rgbe)) {
		if (SurfaceTiling == dm_sw_linear) {
			*BlockHeight256BytesY = 1;
		} else if (SourcePixelFormat == dm_444_64) {
			*BlockHeight256BytesY = 4;
		} else if (SourcePixelFormat == dm_444_8) {
			*BlockHeight256BytesY = 16;
		} else {
			*BlockHeight256BytesY = 8;
		}
		*BlockWidth256BytesY = 256U / *BytePerPixelY / *BlockHeight256BytesY;
		*BlockHeight256BytesC = 0;
		*BlockWidth256BytesC = 0;
	} else {
		if (SurfaceTiling == dm_sw_linear) {
			*BlockHeight256BytesY = 1;
			*BlockHeight256BytesC = 1;
		} else if (SourcePixelFormat == dm_rgbe_alpha) {
			*BlockHeight256BytesY = 8;
			*BlockHeight256BytesC = 16;
		} else if (SourcePixelFormat == dm_420_8) {
			*BlockHeight256BytesY = 16;
			*BlockHeight256BytesC = 8;
		} else {
			*BlockHeight256BytesY = 8;
			*BlockHeight256BytesC = 8;
		}
		*BlockWidth256BytesY = 256U / *BytePerPixelY / *BlockHeight256BytesY;
		*BlockWidth256BytesC = 256U / *BytePerPixelC / *BlockHeight256BytesC;
	}
}

static double CalculateTWait(
		unsigned int PrefetchMode,
		double DRAMClockChangeLatency,
		double UrgentLatency,
		double SREnterPlusExitTime)
{
	if (PrefetchMode == 0) {
		return dml_max(DRAMClockChangeLatency + UrgentLatency,
				dml_max(SREnterPlusExitTime, UrgentLatency));
	} else if (PrefetchMode == 1) {
		return dml_max(SREnterPlusExitTime, UrgentLatency);
	} else {
		return UrgentLatency;
	}
}

double dml30_CalculateWriteBackDISPCLK(
		enum source_format_class WritebackPixelFormat,
		double PixelClock,
		double WritebackHRatio,
		double WritebackVRatio,
		unsigned int WritebackHTaps,
		unsigned int WritebackVTaps,
		long   WritebackSourceWidth,
		long   WritebackDestinationWidth,
		unsigned int HTotal,
		unsigned int WritebackLineBufferSize)
{
	double DISPCLK_H = 0, DISPCLK_V = 0, DISPCLK_HB = 0;

	DISPCLK_H = PixelClock * dml_ceil(WritebackHTaps / 8.0, 1) / WritebackHRatio;
	DISPCLK_V = PixelClock * (WritebackVTaps * dml_ceil(WritebackDestinationWidth / 6.0, 1) + 8.0) / HTotal;
	DISPCLK_HB = PixelClock * WritebackVTaps * (WritebackDestinationWidth * WritebackVTaps - WritebackLineBufferSize / 57.0) / 6.0 / WritebackSourceWidth;
	return dml_max3(DISPCLK_H, DISPCLK_V, DISPCLK_HB);
}

static double CalculateWriteBackDelay(
		enum source_format_class WritebackPixelFormat,
		double WritebackHRatio,
		double WritebackVRatio,
		unsigned int WritebackVTaps,
		long         WritebackDestinationWidth,
		long         WritebackDestinationHeight,
		long         WritebackSourceHeight,
		unsigned int HTotal)
{
	double CalculateWriteBackDelay = 0;
	double Line_length = 0;
	double Output_lines_last_notclamped = 0;
	double WritebackVInit = 0;

	WritebackVInit = (WritebackVRatio + WritebackVTaps + 1) / 2;
	Line_length = dml_max((double) WritebackDestinationWidth, dml_ceil(WritebackDestinationWidth / 6.0, 1) * WritebackVTaps);
	Output_lines_last_notclamped = WritebackDestinationHeight - 1 - dml_ceil((WritebackSourceHeight - WritebackVInit) / WritebackVRatio, 1);
	if (Output_lines_last_notclamped < 0) {
		CalculateWriteBackDelay = 0;
	} else {
		CalculateWriteBackDelay = Output_lines_last_notclamped * Line_length + (HTotal - WritebackDestinationWidth) + 80;
	}
	return CalculateWriteBackDelay;
}


static void CalculateDynamicMetadataParameters(int MaxInterDCNTileRepeaters, double DPPCLK, double DISPCLK,
		double DCFClkDeepSleep, double PixelClock, long HTotal, long VBlank, long DynamicMetadataTransmittedBytes,
		long DynamicMetadataLinesBeforeActiveRequired, int InterlaceEnable, bool ProgressiveToInterlaceUnitInOPP,
		double *Tsetup, double *Tdmbf, double *Tdmec, double *Tdmsks)
{
	double TotalRepeaterDelayTime = 0;
	double VUpdateWidthPix = 0;
	double VReadyOffsetPix = 0;
	double VUpdateOffsetPix = 0;
	TotalRepeaterDelayTime = MaxInterDCNTileRepeaters * (2 / DPPCLK + 3 / DISPCLK);
	VUpdateWidthPix = (14 / DCFClkDeepSleep + 12 / DPPCLK + TotalRepeaterDelayTime) * PixelClock;
	VReadyOffsetPix = dml_max(150.0 / DPPCLK, TotalRepeaterDelayTime + 20 / DCFClkDeepSleep + 10 / DPPCLK) * PixelClock;
	VUpdateOffsetPix = dml_ceil(HTotal / 4.0, 1);
	*Tsetup = (VUpdateOffsetPix + VUpdateWidthPix + VReadyOffsetPix) / PixelClock;
	*Tdmbf = DynamicMetadataTransmittedBytes / 4.0 / DISPCLK;
	*Tdmec = HTotal / PixelClock;
	if (DynamicMetadataLinesBeforeActiveRequired == 0) {
		*Tdmsks = VBlank * HTotal / PixelClock / 2.0;
	} else {
		*Tdmsks = DynamicMetadataLinesBeforeActiveRequired * HTotal / PixelClock;
	}
	if (InterlaceEnable == 1 && ProgressiveToInterlaceUnitInOPP == false) {
		*Tdmsks = *Tdmsks / 2;
	}
}

static void CalculateRowBandwidth(
		bool GPUVMEnable,
		enum source_format_class SourcePixelFormat,
		double VRatio,
		double VRatioChroma,
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
		double *dpte_row_bw)
{
	if (DCCEnable != true) {
		*meta_row_bw = 0;
	} else if (SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10 || SourcePixelFormat == dm_420_12 || SourcePixelFormat == dm_rgbe_alpha) {
		*meta_row_bw = VRatio * MetaRowByteLuma / (meta_row_height_luma * LineTime)
				+ VRatioChroma * MetaRowByteChroma
						/ (meta_row_height_chroma * LineTime);
	} else {
		*meta_row_bw = VRatio * MetaRowByteLuma / (meta_row_height_luma * LineTime);
	}

	if (GPUVMEnable != true) {
		*dpte_row_bw = 0;
	} else if (SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10 || SourcePixelFormat == dm_420_12 || SourcePixelFormat == dm_rgbe_alpha) {
		*dpte_row_bw = VRatio * PixelPTEBytesPerRowLuma / (dpte_row_height_luma * LineTime)
				+ VRatioChroma * PixelPTEBytesPerRowChroma
						/ (dpte_row_height_chroma * LineTime);
	} else {
		*dpte_row_bw = VRatio * PixelPTEBytesPerRowLuma / (dpte_row_height_luma * LineTime);
	}
}

static void CalculateFlipSchedule(
		struct display_mode_lib *mode_lib,
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
		double UrgentExtraLatency,
		double UrgentLatency,
		unsigned int GPUVMMaxPageTableLevels,
		bool HostVMEnable,
		unsigned int HostVMMaxNonCachedPageTableLevels,
		bool GPUVMEnable,
		double HostVMMinPageSize,
		double PDEAndMetaPTEBytesPerFrame,
		double MetaRowBytes,
		double DPTEBytesPerRow,
		double BandwidthAvailableForImmediateFlip,
		unsigned int TotImmediateFlipBytes,
		enum source_format_class SourcePixelFormat,
		double LineTime,
		double VRatio,
		double VRatioChroma,
		double Tno_bw,
		bool DCCEnable,
		unsigned int dpte_row_height,
		unsigned int meta_row_height,
		unsigned int dpte_row_height_chroma,
		unsigned int meta_row_height_chroma,
		double *DestinationLinesToRequestVMInImmediateFlip,
		double *DestinationLinesToRequestRowInImmediateFlip,
		double *final_flip_bw,
		bool *ImmediateFlipSupportedForPipe)
{
	double min_row_time = 0.0;
	unsigned int HostVMDynamicLevelsTrips = 0;
	double TimeForFetchingMetaPTEImmediateFlip = 0;
	double TimeForFetchingRowInVBlankImmediateFlip = 0;
	double ImmediateFlipBW = 0;
	double HostVMInefficiencyFactor = 0;

	if (GPUVMEnable == true && HostVMEnable == true) {
		HostVMInefficiencyFactor = PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData / PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly;
		HostVMDynamicLevelsTrips = HostVMMaxNonCachedPageTableLevels;
	} else {
		HostVMInefficiencyFactor = 1;
		HostVMDynamicLevelsTrips = 0;
	}

	if (GPUVMEnable == true || DCCEnable == true) {
		ImmediateFlipBW = (PDEAndMetaPTEBytesPerFrame + MetaRowBytes + DPTEBytesPerRow) * BandwidthAvailableForImmediateFlip / TotImmediateFlipBytes;
	}

	if (GPUVMEnable == true) {
		TimeForFetchingMetaPTEImmediateFlip = dml_max3(Tno_bw + PDEAndMetaPTEBytesPerFrame * HostVMInefficiencyFactor / ImmediateFlipBW,
				UrgentExtraLatency + UrgentLatency * (GPUVMMaxPageTableLevels * (HostVMDynamicLevelsTrips + 1) - 1), LineTime / 4.0);
	} else {
		TimeForFetchingMetaPTEImmediateFlip = 0;
	}

	*DestinationLinesToRequestVMInImmediateFlip = dml_ceil(4.0 * (TimeForFetchingMetaPTEImmediateFlip / LineTime), 1) / 4.0;
	if ((GPUVMEnable == true || DCCEnable == true)) {
		TimeForFetchingRowInVBlankImmediateFlip = dml_max3((MetaRowBytes + DPTEBytesPerRow * HostVMInefficiencyFactor) / ImmediateFlipBW,
				UrgentLatency * (HostVMDynamicLevelsTrips + 1), LineTime / 4);
	} else {
		TimeForFetchingRowInVBlankImmediateFlip = 0;
	}

	*DestinationLinesToRequestRowInImmediateFlip = dml_ceil(4.0 * (TimeForFetchingRowInVBlankImmediateFlip / LineTime), 1) / 4.0;

	if (GPUVMEnable == true) {
		*final_flip_bw = dml_max(PDEAndMetaPTEBytesPerFrame * HostVMInefficiencyFactor / (*DestinationLinesToRequestVMInImmediateFlip * LineTime),
				(MetaRowBytes + DPTEBytesPerRow * HostVMInefficiencyFactor) / (*DestinationLinesToRequestRowInImmediateFlip * LineTime));
	} else if ((GPUVMEnable == true || DCCEnable == true)) {
		*final_flip_bw = (MetaRowBytes + DPTEBytesPerRow * HostVMInefficiencyFactor) / (*DestinationLinesToRequestRowInImmediateFlip * LineTime);
	} else {
		*final_flip_bw = 0;
	}


	if (SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10 || SourcePixelFormat == dm_rgbe_alpha) {
		if (GPUVMEnable == true && DCCEnable != true) {
			min_row_time = dml_min(dpte_row_height * LineTime / VRatio, dpte_row_height_chroma * LineTime / VRatioChroma);
		} else if (GPUVMEnable != true && DCCEnable == true) {
			min_row_time = dml_min(meta_row_height * LineTime / VRatio, meta_row_height_chroma * LineTime / VRatioChroma);
		} else {
			min_row_time = dml_min4(dpte_row_height * LineTime / VRatio, meta_row_height * LineTime / VRatio,
					dpte_row_height_chroma * LineTime / VRatioChroma, meta_row_height_chroma * LineTime / VRatioChroma);
		}
	} else {
		if (GPUVMEnable == true && DCCEnable != true) {
			min_row_time = dpte_row_height * LineTime / VRatio;
		} else if (GPUVMEnable != true && DCCEnable == true) {
			min_row_time = meta_row_height * LineTime / VRatio;
		} else {
			min_row_time = dml_min(dpte_row_height * LineTime / VRatio, meta_row_height * LineTime / VRatio);
		}
	}

	if (*DestinationLinesToRequestVMInImmediateFlip >= 32 || *DestinationLinesToRequestRowInImmediateFlip >= 16
			|| TimeForFetchingMetaPTEImmediateFlip + 2 * TimeForFetchingRowInVBlankImmediateFlip > min_row_time) {
		*ImmediateFlipSupportedForPipe = false;
	} else {
		*ImmediateFlipSupportedForPipe = true;
	}
}

static double TruncToValidBPP(
		double LinkBitRate,
		int Lanes,
		long HTotal,
		long HActive,
		double PixelClock,
		double DesiredBPP,
		bool DSCEnable,
		enum output_encoder_class Output,
		enum output_format_class Format,
		unsigned int DSCInputBitPerComponent,
		int DSCSlices,
		int AudioRate,
		int AudioLayout,
		enum odm_combine_mode ODMCombine)
{
	double MaxLinkBPP = 0;
	int MinDSCBPP = 0;
	double MaxDSCBPP = 0;
	int NonDSCBPP0 = 0;
	int NonDSCBPP1 = 0;
	int NonDSCBPP2 = 0;

	if (Format == dm_420) {
		NonDSCBPP0 = 12;
		NonDSCBPP1 = 15;
		NonDSCBPP2 = 18;
		MinDSCBPP = 6;
		MaxDSCBPP = 1.5 * DSCInputBitPerComponent - 1.0 / 16;
	} else if (Format == dm_444) {
		NonDSCBPP0 = 24;
		NonDSCBPP1 = 30;
		NonDSCBPP2 = 36;
		MinDSCBPP = 8;
		MaxDSCBPP = 3 * DSCInputBitPerComponent - 1.0 / 16;
	} else {
		NonDSCBPP0 = 16;
		NonDSCBPP1 = 20;
		NonDSCBPP2 = 24;

		if (Format == dm_n422) {
			MinDSCBPP = 7;
			MaxDSCBPP = 2 * DSCInputBitPerComponent - 1.0 / 16.0;
		} else {
			MinDSCBPP = 8;
			MaxDSCBPP = 3 * DSCInputBitPerComponent - 1.0 / 16.0;
		}
	}

	if (DSCEnable && Output == dm_dp) {
		MaxLinkBPP = LinkBitRate / 10 * 8 * Lanes / PixelClock * (1 - 2.4 / 100);
	} else {
		MaxLinkBPP = LinkBitRate / 10 * 8 * Lanes / PixelClock;
	}

	if (ODMCombine == dm_odm_combine_mode_4to1 && MaxLinkBPP > 16) {
		MaxLinkBPP = 16;
	} else if (ODMCombine == dm_odm_combine_mode_2to1 && MaxLinkBPP > 32) {
		MaxLinkBPP = 32;
	}


	if (DesiredBPP == 0) {
		if (DSCEnable) {
			if (MaxLinkBPP < MinDSCBPP) {
				return BPP_INVALID;
			} else if (MaxLinkBPP >= MaxDSCBPP) {
				return MaxDSCBPP;
			} else {
				return dml_floor(16.0 * MaxLinkBPP, 1.0) / 16.0;
			}
		} else {
			if (MaxLinkBPP >= NonDSCBPP2) {
				return NonDSCBPP2;
			} else if (MaxLinkBPP >= NonDSCBPP1) {
				return NonDSCBPP1;
			} else if (MaxLinkBPP >= NonDSCBPP0) {
				return NonDSCBPP0;
			} else {
				return BPP_INVALID;
			}
		}
	} else {
		if (!((DSCEnable == false && (DesiredBPP == NonDSCBPP2 || DesiredBPP == NonDSCBPP1 || DesiredBPP == NonDSCBPP0 || DesiredBPP == 18)) ||
				(DSCEnable && DesiredBPP >= MinDSCBPP && DesiredBPP <= MaxDSCBPP))) {
			return BPP_INVALID;
		} else {
			return DesiredBPP;
		}
	}
}

void dml30_ModeSupportAndSystemConfigurationFull(struct display_mode_lib *mode_lib)
{
	struct vba_vars_st *v = &mode_lib->vba;
	int MinPrefetchMode, MaxPrefetchMode;
	int i, start_state;
	unsigned int j, k, m;
	bool   EnoughWritebackUnits = true;
	bool   WritebackModeSupport = true;
	bool   ViewportExceedsSurface = false;
	double MaxTotalVActiveRDBandwidth = 0;
	long ReorderingBytes = 0;
	bool NotUrgentLatencyHiding[DC__NUM_DPP__MAX] = { 0 };

	/*MODE SUPPORT, VOLTAGE STATE AND SOC CONFIGURATION*/

	if (mode_lib->validate_max_state)
		start_state = v->soc.num_states - 1;
	else
		start_state = 0;

	CalculateMinAndMaxPrefetchMode(
		mode_lib->vba.AllowDRAMSelfRefreshOrDRAMClockChangeInVblank,
		&MinPrefetchMode, &MaxPrefetchMode);

	/*Scale Ratio, taps Support Check*/

	v->ScaleRatioAndTapsSupport = true;
	for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
		if (v->ScalerEnabled[k] == false
				&& ((v->SourcePixelFormat[k] != dm_444_64
						&& v->SourcePixelFormat[k] != dm_444_32
						&& v->SourcePixelFormat[k] != dm_444_16
						&& v->SourcePixelFormat[k] != dm_mono_16
						&& v->SourcePixelFormat[k] != dm_mono_8
						&& v->SourcePixelFormat[k] != dm_rgbe
						&& v->SourcePixelFormat[k] != dm_rgbe_alpha)
						|| v->HRatio[k] != 1.0
						|| v->htaps[k] != 1.0
						|| v->VRatio[k] != 1.0
						|| v->vtaps[k] != 1.0)) {
			v->ScaleRatioAndTapsSupport = false;
		} else if (v->vtaps[k] < 1.0 || v->vtaps[k] > 8.0
				|| v->htaps[k] < 1.0 || v->htaps[k] > 8.0
				|| (v->htaps[k] > 1.0
						&& (v->htaps[k] % 2) == 1)
				|| v->HRatio[k] > v->MaxHSCLRatio
				|| v->VRatio[k] > v->MaxVSCLRatio
				|| v->HRatio[k] > v->htaps[k]
				|| v->VRatio[k] > v->vtaps[k]
				|| (v->SourcePixelFormat[k] != dm_444_64
						&& v->SourcePixelFormat[k] != dm_444_32
						&& v->SourcePixelFormat[k] != dm_444_16
						&& v->SourcePixelFormat[k] != dm_mono_16
						&& v->SourcePixelFormat[k] != dm_mono_8
						&& v->SourcePixelFormat[k] != dm_rgbe
						&& (v->VTAPsChroma[k] < 1
							|| v->VTAPsChroma[k] > 8
							|| v->HTAPsChroma[k] < 1
							|| v->HTAPsChroma[k] > 8
							|| (v->HTAPsChroma[k] > 1 && v->HTAPsChroma[k] % 2 == 1)
							|| v->HRatioChroma[k] > v->MaxHSCLRatio
							|| v->VRatioChroma[k] > v->MaxVSCLRatio
							|| v->HRatioChroma[k] > v->HTAPsChroma[k]
							|| v->VRatioChroma[k] > v->VTAPsChroma[k]))) {
			v->ScaleRatioAndTapsSupport = false;
		}
	}
	/*Source Format, Pixel Format and Scan Support Check*/

	v->SourceFormatPixelAndScanSupport = true;
	for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
		if ((v->SurfaceTiling[k] == dm_sw_linear && (!(v->SourceScan[k] != dm_vert) || v->DCCEnable[k] == true))
				|| ((v->SurfaceTiling[k] == dm_sw_64kb_d || v->SurfaceTiling[k] == dm_sw_64kb_d_t || v->SurfaceTiling[k] == dm_sw_64kb_d_x)
						&& !(v->SourcePixelFormat[k] == dm_444_64))) {
			v->SourceFormatPixelAndScanSupport = false;
		}
	}
	/*Bandwidth Support Check*/

	for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
		dml30_CalculateBytePerPixelAnd256BBlockSizes(
				v->SourcePixelFormat[k],
				v->SurfaceTiling[k],
				&v->BytePerPixelY[k],
				&v->BytePerPixelC[k],
				&v->BytePerPixelInDETY[k],
				&v->BytePerPixelInDETC[k],
				&v->Read256BlockHeightY[k],
				&v->Read256BlockHeightC[k],
				&v->Read256BlockWidthY[k],
				&v->Read256BlockWidthC[k]);
	}
	for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
		if (v->SourceScan[k] != dm_vert) {
			v->SwathWidthYSingleDPP[k] = v->ViewportWidth[k];
			v->SwathWidthCSingleDPP[k] = v->ViewportWidthChroma[k];
		} else {
			v->SwathWidthYSingleDPP[k] = v->ViewportHeight[k];
			v->SwathWidthCSingleDPP[k] = v->ViewportHeightChroma[k];
		}
	}
	for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
		v->ReadBandwidthLuma[k] = v->SwathWidthYSingleDPP[k] * dml_ceil(v->BytePerPixelInDETY[k], 1.0) / (v->HTotal[k] / v->PixelClock[k]) * v->VRatio[k];
		v->ReadBandwidthChroma[k] = v->SwathWidthYSingleDPP[k] / 2 * dml_ceil(v->BytePerPixelInDETC[k], 2.0) / (v->HTotal[k] / v->PixelClock[k]) * v->VRatio[k] / 2.0;
	}
	for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
		if (v->WritebackEnable[k] == true
				&& v->WritebackPixelFormat[k] == dm_444_64) {
			v->WriteBandwidth[k] = v->WritebackDestinationWidth[k]
					* v->WritebackDestinationHeight[k]
					/ (v->WritebackSourceHeight[k]
							* v->HTotal[k]
							/ v->PixelClock[k]) * 8.0;
		} else if (v->WritebackEnable[k] == true) {
			v->WriteBandwidth[k] = v->WritebackDestinationWidth[k]
					* v->WritebackDestinationHeight[k]
					/ (v->WritebackSourceHeight[k]
							* v->HTotal[k]
							/ v->PixelClock[k]) * 4.0;
		} else {
			v->WriteBandwidth[k] = 0.0;
		}
	}

	/*Writeback Latency support check*/

	v->WritebackLatencySupport = true;
	for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
		if (v->WritebackEnable[k] == true) {
			if (v->WritebackConfiguration == dm_whole_buffer_for_single_stream_no_interleave ||
			    v->WritebackConfiguration == dm_whole_buffer_for_single_stream_interleave) {
				if (v->WriteBandwidth[k]
						> 2.0 * v->WritebackInterfaceBufferSize * 1024
								/ v->WritebackLatency) {
					v->WritebackLatencySupport = false;
				}
			} else {
				if (v->WriteBandwidth[k]
						> v->WritebackInterfaceBufferSize * 1024
								/ v->WritebackLatency) {
					v->WritebackLatencySupport = false;
				}
			}
		}
	}

	/*Writeback Mode Support Check*/

	v->TotalNumberOfActiveWriteback = 0;
	for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
		if (v->WritebackEnable[k] == true) {
			v->TotalNumberOfActiveWriteback =
					v->TotalNumberOfActiveWriteback + 1;
		}
	}

	if (v->TotalNumberOfActiveWriteback > v->MaxNumWriteback) {
		EnoughWritebackUnits = false;
	}
	if (!v->WritebackSupportInterleaveAndUsingWholeBufferForASingleStream
			&& (v->WritebackConfiguration == dm_whole_buffer_for_single_stream_no_interleave
					|| v->WritebackConfiguration == dm_whole_buffer_for_single_stream_interleave)) {

		WritebackModeSupport = false;
	}
	if (v->WritebackConfiguration == dm_whole_buffer_for_single_stream_no_interleave && v->TotalNumberOfActiveWriteback > 1) {
		WritebackModeSupport = false;
	}

	/*Writeback Scale Ratio and Taps Support Check*/

	v->WritebackScaleRatioAndTapsSupport = true;
	for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
		if (v->WritebackEnable[k] == true) {
			if (v->WritebackHRatio[k] > v->WritebackMaxHSCLRatio
					|| v->WritebackVRatio[k]
							> v->WritebackMaxVSCLRatio
					|| v->WritebackHRatio[k]
							< v->WritebackMinHSCLRatio
					|| v->WritebackVRatio[k]
							< v->WritebackMinVSCLRatio
					|| v->WritebackHTaps[k]
							> v->WritebackMaxHSCLTaps
					|| v->WritebackVTaps[k]
							> v->WritebackMaxVSCLTaps
					|| v->WritebackHRatio[k]
							> v->WritebackHTaps[k]
					|| v->WritebackVRatio[k]
							> v->WritebackVTaps[k]
					|| (v->WritebackHTaps[k] > 2.0
							&& ((v->WritebackHTaps[k] % 2)
									== 1))) {
				v->WritebackScaleRatioAndTapsSupport = false;
			}
			if (2.0 * v->WritebackDestinationWidth[k] * (v->WritebackVTaps[k] - 1) * 57 > v->WritebackLineBufferSize) {
				v->WritebackScaleRatioAndTapsSupport = false;
			}
		}
	}
	/*Maximum DISPCLK/DPPCLK Support check*/

	v->WritebackRequiredDISPCLK = 0.0;
	for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
		if (v->WritebackEnable[k] == true) {
			v->WritebackRequiredDISPCLK = dml_max(v->WritebackRequiredDISPCLK,
					dml30_CalculateWriteBackDISPCLK(
							v->WritebackPixelFormat[k],
							v->PixelClock[k],
							v->WritebackHRatio[k],
							v->WritebackVRatio[k],
							v->WritebackHTaps[k],
							v->WritebackVTaps[k],
							v->WritebackSourceWidth[k],
							v->WritebackDestinationWidth[k],
							v->HTotal[k],
							v->WritebackLineBufferSize));
		}
	}
	for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
		if (v->HRatio[k] > 1.0) {
			v->PSCL_FACTOR[k] = dml_min(v->MaxDCHUBToPSCLThroughput, v->MaxPSCLToLBThroughput * v->HRatio[k] / dml_ceil(v->htaps[k] / 6.0, 1.0));
		} else {
			v->PSCL_FACTOR[k] = dml_min(v->MaxDCHUBToPSCLThroughput, v->MaxPSCLToLBThroughput);
		}
		if (v->BytePerPixelC[k] == 0.0) {
			v->PSCL_FACTOR_CHROMA[k] = 0.0;
			v->MinDPPCLKUsingSingleDPP[k] = v->PixelClock[k]
					* dml_max3(v->vtaps[k] / 6.0 * dml_min(1.0, v->HRatio[k]), v->HRatio[k] * v->VRatio[k] / v->PSCL_FACTOR[k], 1.0);
			if ((v->htaps[k] > 6.0 || v->vtaps[k] > 6.0) && v->MinDPPCLKUsingSingleDPP[k] < 2.0 * v->PixelClock[k]) {
				v->MinDPPCLKUsingSingleDPP[k] = 2.0 * v->PixelClock[k];
			}
		} else {
			if (v->HRatioChroma[k] > 1.0) {
				v->PSCL_FACTOR_CHROMA[k] = dml_min(v->MaxDCHUBToPSCLThroughput,
						v->MaxPSCLToLBThroughput * v->HRatioChroma[k] / dml_ceil(v->HTAPsChroma[k] / 6.0, 1.0));
			} else {
				v->PSCL_FACTOR_CHROMA[k] = dml_min(v->MaxDCHUBToPSCLThroughput, v->MaxPSCLToLBThroughput);
			}
			v->MinDPPCLKUsingSingleDPP[k] = v->PixelClock[k] * dml_max5(v->vtaps[k] / 6.0 * dml_min(1.0, v->HRatio[k]),
							v->HRatio[k] * v->VRatio[k] / v->PSCL_FACTOR[k],
							v->VTAPsChroma[k] / 6.0 * dml_min(1.0, v->HRatioChroma[k]),
							v->HRatioChroma[k] * v->VRatioChroma[k] / v->PSCL_FACTOR_CHROMA[k],
							1.0);
			if ((v->htaps[k] > 6.0 || v->vtaps[k] > 6.0 || v->HTAPsChroma[k] > 6.0 || v->VTAPsChroma[k] > 6.0)
					&& v->MinDPPCLKUsingSingleDPP[k] < 2.0 * v->PixelClock[k]) {
				v->MinDPPCLKUsingSingleDPP[k] = 2.0 * v->PixelClock[k];
			}
		}
	}
	for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
		int MaximumSwathWidthSupportLuma = 0;
		int MaximumSwathWidthSupportChroma = 0;

		if (v->SurfaceTiling[k] == dm_sw_linear) {
			MaximumSwathWidthSupportLuma = 8192.0;
		} else if (v->SourceScan[k] == dm_vert && v->BytePerPixelC[k] > 0) {
			MaximumSwathWidthSupportLuma = 2880.0;
		} else {
			MaximumSwathWidthSupportLuma = 5760.0;
		}

		if (v->SourcePixelFormat[k] == dm_420_8 || v->SourcePixelFormat[k] == dm_420_10 || v->SourcePixelFormat[k] == dm_420_12) {
			MaximumSwathWidthSupportChroma = MaximumSwathWidthSupportLuma / 2.0;
		} else {
			MaximumSwathWidthSupportChroma = MaximumSwathWidthSupportLuma;
		}
		v->MaximumSwathWidthInLineBufferLuma = v->LineBufferSize * dml_max(v->HRatio[k], 1.0) / v->LBBitPerPixel[k]
				/ (v->vtaps[k] + dml_max(dml_ceil(v->VRatio[k], 1.0) - 2, 0.0));
		if (v->BytePerPixelC[k] == 0.0) {
			v->MaximumSwathWidthInLineBufferChroma = 0;
		} else {
			v->MaximumSwathWidthInLineBufferChroma = v->LineBufferSize * dml_max(v->HRatioChroma[k], 1.0) / v->LBBitPerPixel[k]
					/ (v->VTAPsChroma[k] + dml_max(dml_ceil(v->VRatioChroma[k], 1.0) - 2, 0.0));
		}
		v->MaximumSwathWidthLuma[k] = dml_min(MaximumSwathWidthSupportLuma, v->MaximumSwathWidthInLineBufferLuma);
		v->MaximumSwathWidthChroma[k] = dml_min(MaximumSwathWidthSupportChroma, v->MaximumSwathWidthInLineBufferChroma);
	}

	CalculateSwathAndDETConfiguration(
			true,
			v->NumberOfActivePlanes,
			v->DETBufferSizeInKByte[0],
			v->MaximumSwathWidthLuma,
			v->MaximumSwathWidthChroma,
			v->SourceScan,
			v->SourcePixelFormat,
			v->SurfaceTiling,
			v->ViewportWidth,
			v->ViewportHeight,
			v->SurfaceWidthY,
			v->SurfaceWidthC,
			v->SurfaceHeightY,
			v->SurfaceHeightC,
			v->Read256BlockHeightY,
			v->Read256BlockHeightC,
			v->Read256BlockWidthY,
			v->Read256BlockWidthC,
			v->odm_combine_dummy,
			v->BlendingAndTiming,
			v->BytePerPixelY,
			v->BytePerPixelC,
			v->BytePerPixelInDETY,
			v->BytePerPixelInDETC,
			v->HActive,
			v->HRatio,
			v->HRatioChroma,
			v->DPPPerPlane,
			v->swath_width_luma_ub,
			v->swath_width_chroma_ub,
			v->SwathWidthY,
			v->SwathWidthC,
			v->SwathHeightY,
			v->SwathHeightC,
			v->DETBufferSizeY,
			v->DETBufferSizeC,
			v->SingleDPPViewportSizeSupportPerPlane,
			&v->ViewportSizeSupport[0][0]);

	for (i = start_state; i < v->soc.num_states; i++) {
		for (j = 0; j < 2; j++) {
			v->MaxDispclkRoundedDownToDFSGranularity = RoundToDFSGranularityDown(v->MaxDispclk[i], v->DISPCLKDPPCLKVCOSpeed);
			v->MaxDppclkRoundedDownToDFSGranularity = RoundToDFSGranularityDown(v->MaxDppclk[i], v->DISPCLKDPPCLKVCOSpeed);
			v->RequiredDISPCLK[i][j] = 0.0;
			v->DISPCLK_DPPCLK_Support[i][j] = true;
			for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
				v->PlaneRequiredDISPCLKWithoutODMCombine = v->PixelClock[k] * (1.0 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0)
						* (1.0 + v->DISPCLKRampingMargin / 100.0);
				if ((v->PlaneRequiredDISPCLKWithoutODMCombine >= v->MaxDispclk[i] && v->MaxDispclk[i] == v->MaxDispclk[mode_lib->soc.num_states - 1]
						&& v->MaxDppclk[i] == v->MaxDppclk[mode_lib->soc.num_states - 1])) {
					v->PlaneRequiredDISPCLKWithoutODMCombine = v->PixelClock[k] * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);
				}
				v->PlaneRequiredDISPCLKWithODMCombine2To1 = v->PixelClock[k] / 2 * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0)
						* (1 + v->DISPCLKRampingMargin / 100.0);
				if ((v->PlaneRequiredDISPCLKWithODMCombine2To1 >= v->MaxDispclk[i] && v->MaxDispclk[i] == v->MaxDispclk[mode_lib->soc.num_states - 1]
						&& v->MaxDppclk[i] == v->MaxDppclk[mode_lib->soc.num_states - 1])) {
					v->PlaneRequiredDISPCLKWithODMCombine2To1 = v->PixelClock[k] / 2 * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);
				}
				v->PlaneRequiredDISPCLKWithODMCombine4To1 = v->PixelClock[k] / 4 * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0)
						* (1 + v->DISPCLKRampingMargin / 100.0);
				if ((v->PlaneRequiredDISPCLKWithODMCombine4To1 >= v->MaxDispclk[i] && v->MaxDispclk[i] == v->MaxDispclk[mode_lib->soc.num_states - 1]
						&& v->MaxDppclk[i] == v->MaxDppclk[mode_lib->soc.num_states - 1])) {
					v->PlaneRequiredDISPCLKWithODMCombine4To1 = v->PixelClock[k] / 4 * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);
				}

				if (v->ODMCombinePolicy == dm_odm_combine_policy_none) {
					v->ODMCombineEnablePerState[i][k] = dm_odm_combine_mode_disabled;
					v->PlaneRequiredDISPCLK = v->PlaneRequiredDISPCLKWithoutODMCombine;
				} else if (v->ODMCombinePolicy == dm_odm_combine_policy_2to1) {
					v->ODMCombineEnablePerState[i][k] = dm_odm_combine_mode_2to1;
					v->PlaneRequiredDISPCLK = v->PlaneRequiredDISPCLKWithODMCombine2To1;
				} else if (v->ODMCombinePolicy == dm_odm_combine_policy_4to1
						|| v->PlaneRequiredDISPCLKWithODMCombine2To1 > v->MaxDispclkRoundedDownToDFSGranularity) {
					v->ODMCombineEnablePerState[i][k] = dm_odm_combine_mode_4to1;
					v->PlaneRequiredDISPCLK = v->PlaneRequiredDISPCLKWithODMCombine4To1;
				} else if (v->PlaneRequiredDISPCLKWithoutODMCombine > v->MaxDispclkRoundedDownToDFSGranularity) {
					v->ODMCombineEnablePerState[i][k] = dm_odm_combine_mode_2to1;
					v->PlaneRequiredDISPCLK = v->PlaneRequiredDISPCLKWithODMCombine2To1;
				} else {
					v->ODMCombineEnablePerState[i][k] = dm_odm_combine_mode_disabled;
					v->PlaneRequiredDISPCLK = v->PlaneRequiredDISPCLKWithoutODMCombine;
				}
				if (v->DSCEnabled[k] && v->HActive[k] > DCN30_MAX_DSC_IMAGE_WIDTH
						&& v->ODMCombineEnablePerState[i][k] != dm_odm_combine_mode_4to1) {
					if (v->HActive[k] / 2 > DCN30_MAX_DSC_IMAGE_WIDTH) {
						v->ODMCombineEnablePerState[i][k] = dm_odm_combine_mode_4to1;
						v->PlaneRequiredDISPCLK = v->PlaneRequiredDISPCLKWithODMCombine4To1;
					} else {
						v->ODMCombineEnablePerState[i][k] = dm_odm_combine_mode_2to1;
						v->PlaneRequiredDISPCLK = v->PlaneRequiredDISPCLKWithODMCombine2To1;
					}
				}
				if (v->OutputFormat[k] == dm_420 && v->HActive[k] > DCN30_MAX_FMT_420_BUFFER_WIDTH
						&& v->ODMCombineEnablePerState[i][k] != dm_odm_combine_mode_4to1) {
					if (v->HActive[k] / 2 > DCN30_MAX_FMT_420_BUFFER_WIDTH) {
						v->ODMCombineEnablePerState[i][k] = dm_odm_combine_mode_4to1;
						v->PlaneRequiredDISPCLK = v->PlaneRequiredDISPCLKWithODMCombine4To1;
					} else {
						v->ODMCombineEnablePerState[i][k] = dm_odm_combine_mode_2to1;
						v->PlaneRequiredDISPCLK = v->PlaneRequiredDISPCLKWithODMCombine2To1;
					}
				}
				if (v->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_4to1) {
					v->MPCCombine[i][j][k] = false;
					v->NoOfDPP[i][j][k] = 4;
					v->RequiredDPPCLK[i][j][k] = v->MinDPPCLKUsingSingleDPP[k] * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0) / 4;
				} else if (v->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_2to1) {
					v->MPCCombine[i][j][k] = false;
					v->NoOfDPP[i][j][k] = 2;
					v->RequiredDPPCLK[i][j][k] = v->MinDPPCLKUsingSingleDPP[k] * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0) / 2;
				} else if ((v->WhenToDoMPCCombine == dm_mpc_never
						|| (v->MinDPPCLKUsingSingleDPP[k] * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0) <= v->MaxDppclkRoundedDownToDFSGranularity
								&& v->SingleDPPViewportSizeSupportPerPlane[k] == true))) {
					v->MPCCombine[i][j][k] = false;
					v->NoOfDPP[i][j][k] = 1;
					v->RequiredDPPCLK[i][j][k] = v->MinDPPCLKUsingSingleDPP[k] * (1.0 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);
				} else {
					v->MPCCombine[i][j][k] = true;
					v->NoOfDPP[i][j][k] = 2;
					v->RequiredDPPCLK[i][j][k] = v->MinDPPCLKUsingSingleDPP[k] * (1.0 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0) / 2.0;
				}
				v->RequiredDISPCLK[i][j] = dml_max(v->RequiredDISPCLK[i][j], v->PlaneRequiredDISPCLK);
				if ((v->MinDPPCLKUsingSingleDPP[k] / v->NoOfDPP[i][j][k] * (1.0 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0)
						> v->MaxDppclkRoundedDownToDFSGranularity) || (v->PlaneRequiredDISPCLK > v->MaxDispclkRoundedDownToDFSGranularity)) {
					v->DISPCLK_DPPCLK_Support[i][j] = false;
				}
			}
			v->TotalNumberOfActiveDPP[i][j] = 0;
			v->TotalNumberOfSingleDPPPlanes[i][j] = 0;
			for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
				v->TotalNumberOfActiveDPP[i][j] = v->TotalNumberOfActiveDPP[i][j] + v->NoOfDPP[i][j][k];
				if (v->NoOfDPP[i][j][k] == 1)
					v->TotalNumberOfSingleDPPPlanes[i][j] = v->TotalNumberOfSingleDPPPlanes[i][j] + 1;
			}
			if (j == 1 && v->WhenToDoMPCCombine != dm_mpc_never) {
				while (!(v->TotalNumberOfActiveDPP[i][j] >= v->MaxNumDPP || v->TotalNumberOfSingleDPPPlanes[i][j] == 0)) {
					double BWOfNonSplitPlaneOfMaximumBandwidth = 0;
					unsigned int NumberOfNonSplitPlaneOfMaximumBandwidth = 0;
					BWOfNonSplitPlaneOfMaximumBandwidth = 0;
					NumberOfNonSplitPlaneOfMaximumBandwidth = 0;
					for (k = 0; k < v->NumberOfActivePlanes; ++k) {
						if (v->ReadBandwidthLuma[k] + v->ReadBandwidthChroma[k] > BWOfNonSplitPlaneOfMaximumBandwidth
								&& v->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_disabled && v->MPCCombine[i][j][k] == false) {
							BWOfNonSplitPlaneOfMaximumBandwidth = v->ReadBandwidthLuma[k] + v->ReadBandwidthChroma[k];
							NumberOfNonSplitPlaneOfMaximumBandwidth = k;
						}
					}
					v->MPCCombine[i][j][NumberOfNonSplitPlaneOfMaximumBandwidth] = true;
					v->NoOfDPP[i][j][NumberOfNonSplitPlaneOfMaximumBandwidth] = 2;
					v->RequiredDPPCLK[i][j][NumberOfNonSplitPlaneOfMaximumBandwidth] = v->MinDPPCLKUsingSingleDPP[NumberOfNonSplitPlaneOfMaximumBandwidth]
							* (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100) / 2;
					v->TotalNumberOfActiveDPP[i][j] = v->TotalNumberOfActiveDPP[i][j] + 1;
					v->TotalNumberOfSingleDPPPlanes[i][j] = v->TotalNumberOfSingleDPPPlanes[i][j] - 1;
				}
			}
			if (v->TotalNumberOfActiveDPP[i][j] > v->MaxNumDPP) {
				v->RequiredDISPCLK[i][j] = 0.0;
				v->DISPCLK_DPPCLK_Support[i][j] = true;
				for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
					v->ODMCombineEnablePerState[i][k] = dm_odm_combine_mode_disabled;
					if (v->SingleDPPViewportSizeSupportPerPlane[k] == false && v->WhenToDoMPCCombine != dm_mpc_never) {
						v->MPCCombine[i][j][k] = true;
						v->NoOfDPP[i][j][k] = 2;
						v->RequiredDPPCLK[i][j][k] = v->MinDPPCLKUsingSingleDPP[k] * (1.0 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0) / 2.0;
					} else {
						v->MPCCombine[i][j][k] = false;
						v->NoOfDPP[i][j][k] = 1;
						v->RequiredDPPCLK[i][j][k] = v->MinDPPCLKUsingSingleDPP[k] * (1.0 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);
					}
					if (!(v->MaxDispclk[i] == v->MaxDispclk[v->soc.num_states - 1] && v->MaxDppclk[i] == v->MaxDppclk[v->soc.num_states - 1])) {
						v->PlaneRequiredDISPCLK = v->PixelClock[k] * (1.0 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0)
								* (1.0 + v->DISPCLKRampingMargin / 100.0);
					} else {
						v->PlaneRequiredDISPCLK = v->PixelClock[k] * (1.0 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);
					}
					v->RequiredDISPCLK[i][j] = dml_max(v->RequiredDISPCLK[i][j], v->PlaneRequiredDISPCLK);
					if ((v->MinDPPCLKUsingSingleDPP[k] / v->NoOfDPP[i][j][k] * (1.0 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0)
							> v->MaxDppclkRoundedDownToDFSGranularity) || (v->PlaneRequiredDISPCLK > v->MaxDispclkRoundedDownToDFSGranularity)) {
						v->DISPCLK_DPPCLK_Support[i][j] = false;
					}
				}
				v->TotalNumberOfActiveDPP[i][j] = 0.0;
				for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
					v->TotalNumberOfActiveDPP[i][j] = v->TotalNumberOfActiveDPP[i][j] + v->NoOfDPP[i][j][k];
				}
			}
			v->RequiredDISPCLK[i][j] = dml_max(v->RequiredDISPCLK[i][j], v->WritebackRequiredDISPCLK);
			if (v->MaxDispclkRoundedDownToDFSGranularity < v->WritebackRequiredDISPCLK) {
				v->DISPCLK_DPPCLK_Support[i][j] = false;
			}
		}
	}

	/*Total Available Pipes Support Check*/

	for (i = start_state; i < v->soc.num_states; i++) {
		for (j = 0; j < 2; j++) {
			if (v->TotalNumberOfActiveDPP[i][j] <= v->MaxNumDPP) {
				v->TotalAvailablePipesSupport[i][j] = true;
			} else {
				v->TotalAvailablePipesSupport[i][j] = false;
			}
		}
	}
	/*Display IO and DSC Support Check*/

	v->NonsupportedDSCInputBPC = false;
	for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
		if (!(v->DSCInputBitPerComponent[k] == 12.0
				|| v->DSCInputBitPerComponent[k] == 10.0
				|| v->DSCInputBitPerComponent[k] == 8.0)) {
			v->NonsupportedDSCInputBPC = true;
		}
	}

	/*Number Of DSC Slices*/
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		if (v->BlendingAndTiming[k] == k) {
			if (v->PixelClockBackEnd[k] > 3200) {
				v->NumberOfDSCSlices[k] = dml_ceil(v->PixelClockBackEnd[k] / 400.0, 4.0);
			} else if (v->PixelClockBackEnd[k] > 1360) {
				v->NumberOfDSCSlices[k] = 8;
			} else if (v->PixelClockBackEnd[k] > 680) {
				v->NumberOfDSCSlices[k] = 4;
			} else if (v->PixelClockBackEnd[k] > 340) {
				v->NumberOfDSCSlices[k] = 2;
			} else {
				v->NumberOfDSCSlices[k] = 1;
			}
		} else {
			v->NumberOfDSCSlices[k] = 0;
		}
	}

	for (i = start_state; i < v->soc.num_states; i++) {
		for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
			v->RequiresDSC[i][k] = false;
			v->RequiresFEC[i][k] = false;
			if (v->BlendingAndTiming[k] == k) {
				if (v->Output[k] == dm_hdmi) {
					v->RequiresDSC[i][k] = false;
					v->RequiresFEC[i][k] = false;
					v->OutputBppPerState[i][k] = TruncToValidBPP(
							dml_min(600.0, v->PHYCLKPerState[i]) * 10,
							3,
							v->HTotal[k],
							v->HActive[k],
							v->PixelClockBackEnd[k],
							v->ForcedOutputLinkBPP[k],
							false,
							v->Output[k],
							v->OutputFormat[k],
							v->DSCInputBitPerComponent[k],
							v->NumberOfDSCSlices[k],
							v->AudioSampleRate[k],
							v->AudioSampleLayout[k],
							v->ODMCombineEnablePerState[i][k]);
				} else if (v->Output[k] == dm_dp || v->Output[k] == dm_edp) {
					if (v->DSCEnable[k] == true) {
						v->RequiresDSC[i][k] = true;
						v->LinkDSCEnable = true;
						if (v->Output[k] == dm_dp) {
							v->RequiresFEC[i][k] = true;
						} else {
							v->RequiresFEC[i][k] = false;
						}
					} else {
						v->RequiresDSC[i][k] = false;
						v->LinkDSCEnable = false;
						v->RequiresFEC[i][k] = false;
					}

					v->Outbpp = BPP_INVALID;
					if (v->PHYCLKPerState[i] >= 270.0) {
						v->Outbpp = TruncToValidBPP(
								(1.0 - v->Downspreading / 100.0) * 2700,
								v->OutputLinkDPLanes[k],
								v->HTotal[k],
								v->HActive[k],
								v->PixelClockBackEnd[k],
								v->ForcedOutputLinkBPP[k],
								v->LinkDSCEnable,
								v->Output[k],
								v->OutputFormat[k],
								v->DSCInputBitPerComponent[k],
								v->NumberOfDSCSlices[k],
								v->AudioSampleRate[k],
								v->AudioSampleLayout[k],
								v->ODMCombineEnablePerState[i][k]);
						v->OutputBppPerState[i][k] = v->Outbpp;
						// TODO: Need some other way to handle this nonsense
						// v->OutputTypeAndRatePerState[i][k] = v->Output[k] & " HBR"
					}
					if (v->Outbpp == BPP_INVALID && v->PHYCLKPerState[i] >= 540.0) {
						v->Outbpp = TruncToValidBPP(
								(1.0 - v->Downspreading / 100.0) * 5400,
								v->OutputLinkDPLanes[k],
								v->HTotal[k],
								v->HActive[k],
								v->PixelClockBackEnd[k],
								v->ForcedOutputLinkBPP[k],
								v->LinkDSCEnable,
								v->Output[k],
								v->OutputFormat[k],
								v->DSCInputBitPerComponent[k],
								v->NumberOfDSCSlices[k],
								v->AudioSampleRate[k],
								v->AudioSampleLayout[k],
								v->ODMCombineEnablePerState[i][k]);
						v->OutputBppPerState[i][k] = v->Outbpp;
						// TODO: Need some other way to handle this nonsense
						// v->OutputTypeAndRatePerState[i][k] = v->Output[k] & " HBR2"
					}
					if (v->Outbpp == BPP_INVALID && v->PHYCLKPerState[i] >= 810.0) {
						v->Outbpp = TruncToValidBPP(
								(1.0 - v->Downspreading / 100.0) * 8100,
								v->OutputLinkDPLanes[k],
								v->HTotal[k],
								v->HActive[k],
								v->PixelClockBackEnd[k],
								v->ForcedOutputLinkBPP[k],
								v->LinkDSCEnable,
								v->Output[k],
								v->OutputFormat[k],
								v->DSCInputBitPerComponent[k],
								v->NumberOfDSCSlices[k],
								v->AudioSampleRate[k],
								v->AudioSampleLayout[k],
								v->ODMCombineEnablePerState[i][k]);
						if (v->Outbpp == BPP_INVALID && v->ForcedOutputLinkBPP[k] == 0) {
							//if (v->Outbpp == BPP_INVALID && v->DSCEnabled[k] == dm_dsc_enable_only_if_necessary && v->ForcedOutputLinkBPP[k] == 0) {
							v->RequiresDSC[i][k] = true;
							v->LinkDSCEnable = true;
							if (v->Output[k] == dm_dp) {
								v->RequiresFEC[i][k] = true;
							}
							v->Outbpp = TruncToValidBPP(
									(1.0 - v->Downspreading / 100.0) * 8100,
									v->OutputLinkDPLanes[k],
									v->HTotal[k],
									v->HActive[k],
									v->PixelClockBackEnd[k],
									v->ForcedOutputLinkBPP[k],
									v->LinkDSCEnable,
									v->Output[k],
									v->OutputFormat[k],
									v->DSCInputBitPerComponent[k],
									v->NumberOfDSCSlices[k],
									v->AudioSampleRate[k],
									v->AudioSampleLayout[k],
									v->ODMCombineEnablePerState[i][k]);
						}
						v->OutputBppPerState[i][k] = v->Outbpp;
						// TODO: Need some other way to handle this nonsense
						// v->OutputTypeAndRatePerState[i][k] = v->Output[k] & " HBR3"
					}
				}
			} else {
				v->OutputBppPerState[i][k] = 0;
			}
		}
	}
	for (i = start_state; i < v->soc.num_states; i++) {
		v->DIOSupport[i] = true;
		for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
			if (!v->skip_dio_check[k] && v->BlendingAndTiming[k] == k && (v->Output[k] == dm_dp || v->Output[k] == dm_edp || v->Output[k] == dm_hdmi)
					&& (v->OutputBppPerState[i][k] == 0
							|| (v->OutputFormat[k] == dm_420 && v->Interlace[k] == true && v->ProgressiveToInterlaceUnitInOPP == true))) {
				v->DIOSupport[i] = false;
			}
		}
	}

	for (i = start_state; i < v->soc.num_states; ++i) {
		v->ODMCombine4To1SupportCheckOK[i] = true;
		for (k = 0; k < v->NumberOfActivePlanes; ++k) {
			if (v->BlendingAndTiming[k] == k && v->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_4to1
					&& (v->ODMCombine4To1Supported == false || v->Output[k] == dm_dp || v->Output[k] == dm_edp || v->Output[k] == dm_hdmi)) {
				v->ODMCombine4To1SupportCheckOK[i] = false;
			}
		}
	}

	/* Skip dscclk validation: as long as dispclk is supported, dscclk is also implicitly supported */

	for (i = start_state; i < v->soc.num_states; i++) {
		v->NotEnoughDSCUnits[i] = false;
		v->TotalDSCUnitsRequired = 0.0;
		for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
			if (v->RequiresDSC[i][k] == true) {
				if (v->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_4to1) {
					v->TotalDSCUnitsRequired = v->TotalDSCUnitsRequired + 4.0;
				} else if (v->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_2to1) {
					v->TotalDSCUnitsRequired = v->TotalDSCUnitsRequired + 2.0;
				} else {
					v->TotalDSCUnitsRequired = v->TotalDSCUnitsRequired + 1.0;
				}
			}
		}
		if (v->TotalDSCUnitsRequired > v->NumberOfDSC) {
			v->NotEnoughDSCUnits[i] = true;
		}
	}
	/*DSC Delay per state*/

	for (i = start_state; i < v->soc.num_states; i++) {
		for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
			if (v->OutputBppPerState[i][k] == BPP_INVALID) {
				v->BPP = 0.0;
			} else {
				v->BPP = v->OutputBppPerState[i][k];
			}
			if (v->RequiresDSC[i][k] == true && v->BPP != 0.0) {
				if (v->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_disabled) {
					v->DSCDelayPerState[i][k] = dscceComputeDelay(
							v->DSCInputBitPerComponent[k],
							v->BPP,
							dml_ceil(1.0 * v->HActive[k] / v->NumberOfDSCSlices[k], 1.0),
							v->NumberOfDSCSlices[k],
							v->OutputFormat[k],
							v->Output[k]) + dscComputeDelay(v->OutputFormat[k], v->Output[k]);
				} else if (v->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_2to1) {
					v->DSCDelayPerState[i][k] = 2.0
							* dscceComputeDelay(
									v->DSCInputBitPerComponent[k],
									v->BPP,
									dml_ceil(1.0 * v->HActive[k] / v->NumberOfDSCSlices[k], 1.0),
									v->NumberOfDSCSlices[k] / 2,
									v->OutputFormat[k],
									v->Output[k]) + dscComputeDelay(v->OutputFormat[k], v->Output[k]);
				} else {
					v->DSCDelayPerState[i][k] = 4.0
							* (dscceComputeDelay(
									v->DSCInputBitPerComponent[k],
									v->BPP,
									dml_ceil(1.0 * v->HActive[k] / v->NumberOfDSCSlices[k], 1.0),
									v->NumberOfDSCSlices[k] / 4,
									v->OutputFormat[k],
									v->Output[k]) + dscComputeDelay(v->OutputFormat[k], v->Output[k]));
				}
				v->DSCDelayPerState[i][k] = v->DSCDelayPerState[i][k] * v->PixelClock[k] / v->PixelClockBackEnd[k];
			} else {
				v->DSCDelayPerState[i][k] = 0.0;
			}
		}
		for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
			for (m = 0; m <= v->NumberOfActivePlanes - 1; m++) {
				if (v->BlendingAndTiming[k] == m && v->RequiresDSC[i][m] == true) {
					v->DSCDelayPerState[i][k] = v->DSCDelayPerState[i][m];
				}
			}
		}
	}

	//Calculate Swath, DET Configuration, DCFCLKDeepSleep
	//
	for (i = start_state; i < mode_lib->soc.num_states; ++i) {
		for (j = 0; j <= 1; ++j) {
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				v->RequiredDPPCLKThisState[k] = v->RequiredDPPCLK[i][j][k];
				v->NoOfDPPThisState[k] = v->NoOfDPP[i][j][k];
				v->ODMCombineEnableThisState[k] = v->ODMCombineEnablePerState[i][k];
			}

			CalculateSwathAndDETConfiguration(
					false,
					v->NumberOfActivePlanes,
					v->DETBufferSizeInKByte[0],
					v->MaximumSwathWidthLuma,
					v->MaximumSwathWidthChroma,
					v->SourceScan,
					v->SourcePixelFormat,
					v->SurfaceTiling,
					v->ViewportWidth,
					v->ViewportHeight,
					v->SurfaceWidthY,
					v->SurfaceWidthC,
					v->SurfaceHeightY,
					v->SurfaceHeightC,
					v->Read256BlockHeightY,
					v->Read256BlockHeightC,
					v->Read256BlockWidthY,
					v->Read256BlockWidthC,
					v->ODMCombineEnableThisState,
					v->BlendingAndTiming,
					v->BytePerPixelY,
					v->BytePerPixelC,
					v->BytePerPixelInDETY,
					v->BytePerPixelInDETC,
					v->HActive,
					v->HRatio,
					v->HRatioChroma,
					v->NoOfDPPThisState,
					v->swath_width_luma_ub_this_state,
					v->swath_width_chroma_ub_this_state,
					v->SwathWidthYThisState,
					v->SwathWidthCThisState,
					v->SwathHeightYThisState,
					v->SwathHeightCThisState,
					v->DETBufferSizeYThisState,
					v->DETBufferSizeCThisState,
					v->dummystring,
					&v->ViewportSizeSupport[i][j]);

			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				v->swath_width_luma_ub_all_states[i][j][k] = v->swath_width_luma_ub_this_state[k];
				v->swath_width_chroma_ub_all_states[i][j][k] = v->swath_width_chroma_ub_this_state[k];
				v->SwathWidthYAllStates[i][j][k] = v->SwathWidthYThisState[k];
				v->SwathWidthCAllStates[i][j][k] = v->SwathWidthCThisState[k];
				v->SwathHeightYAllStates[i][j][k] = v->SwathHeightYThisState[k];
				v->SwathHeightCAllStates[i][j][k] = v->SwathHeightCThisState[k];
				v->DETBufferSizeYAllStates[i][j][k] = v->DETBufferSizeYThisState[k];
				v->DETBufferSizeCAllStates[i][j][k] = v->DETBufferSizeCThisState[k];
			}

		}
	}
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		v->cursor_bw[k] = v->NumberOfCursors[k] * v->CursorWidth[k][0] * v->CursorBPP[k][0] / 8.0 / (v->HTotal[k] / v->PixelClock[k]) * v->VRatio[k];
	}

	for (i = start_state; i < v->soc.num_states; i++) {
		for (j = 0; j < 2; j++) {
			for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
				v->swath_width_luma_ub_this_state[k] = v->swath_width_luma_ub_all_states[i][j][k];
				v->swath_width_chroma_ub_this_state[k] = v->swath_width_chroma_ub_all_states[i][j][k];
				v->SwathWidthYThisState[k] = v->SwathWidthYAllStates[i][j][k];
				v->SwathWidthCThisState[k] = v->SwathWidthCAllStates[i][j][k];
				v->SwathHeightYThisState[k] = v->SwathHeightYAllStates[i][j][k];
				v->SwathHeightCThisState[k] = v->SwathHeightCAllStates[i][j][k];
				v->DETBufferSizeYThisState[k] = v->DETBufferSizeYAllStates[i][j][k];
				v->DETBufferSizeCThisState[k] = v->DETBufferSizeCAllStates[i][j][k];
			}

			v->TotalNumberOfDCCActiveDPP[i][j] = 0;
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				if (v->DCCEnable[k] == true) {
					v->TotalNumberOfDCCActiveDPP[i][j] = v->TotalNumberOfDCCActiveDPP[i][j] + v->NoOfDPP[i][j][k];
				}
			}

			for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
				if (v->SourcePixelFormat[k] == dm_420_8 || v->SourcePixelFormat[k] == dm_420_10 || v->SourcePixelFormat[k] == dm_420_12
						|| v->SourcePixelFormat[k] == dm_rgbe_alpha) {

					if ((v->SourcePixelFormat[k] == dm_420_10 || v->SourcePixelFormat[k] == dm_420_12) && v->SourceScan[k] != dm_vert) {
						v->PTEBufferSizeInRequestsForLuma = (v->PTEBufferSizeInRequestsLuma + v->PTEBufferSizeInRequestsChroma) / 2;
						v->PTEBufferSizeInRequestsForChroma = v->PTEBufferSizeInRequestsForLuma;
					} else {
						v->PTEBufferSizeInRequestsForLuma = v->PTEBufferSizeInRequestsLuma;
						v->PTEBufferSizeInRequestsForChroma = v->PTEBufferSizeInRequestsChroma;
					}

					v->PDEAndMetaPTEBytesPerFrameC = CalculateVMAndRowBytes(
							mode_lib,
							v->DCCEnable[k],
							v->Read256BlockHeightC[k],
							v->Read256BlockWidthY[k],
							v->SourcePixelFormat[k],
							v->SurfaceTiling[k],
							v->BytePerPixelC[k],
							v->SourceScan[k],
							v->SwathWidthCThisState[k],
							v->ViewportHeightChroma[k],
							v->GPUVMEnable,
							v->HostVMEnable,
							v->HostVMMaxNonCachedPageTableLevels,
							v->GPUVMMinPageSize,
							v->HostVMMinPageSize,
							v->PTEBufferSizeInRequestsForChroma,
							v->PitchC[k],
							0.0,
							&v->MacroTileWidthC[k],
							&v->MetaRowBytesC,
							&v->DPTEBytesPerRowC,
							&v->PTEBufferSizeNotExceededC[i][j][k],
							&v->dummyinteger7,
							&v->dpte_row_height_chroma[k],
							&v->dummyinteger28,
							&v->dummyinteger26,
							&v->dummyinteger23,
							&v->meta_row_height_chroma[k],
							&v->dummyinteger8,
							&v->dummyinteger9,
							&v->dummyinteger19,
							&v->dummyinteger20,
							&v->dummyinteger17,
							&v->dummyinteger10,
							&v->dummyinteger11);

					v->PrefetchLinesC[i][j][k] = CalculatePrefetchSourceLines(
							mode_lib,
							v->VRatioChroma[k],
							v->VTAPsChroma[k],
							v->Interlace[k],
							v->ProgressiveToInterlaceUnitInOPP,
							v->SwathHeightCThisState[k],
							v->ViewportYStartC[k],
							&v->PrefillC[k],
							&v->MaxNumSwC[k]);
				} else {
					v->PTEBufferSizeInRequestsForLuma = v->PTEBufferSizeInRequestsLuma + v->PTEBufferSizeInRequestsChroma;
					v->PTEBufferSizeInRequestsForChroma = 0;
					v->PDEAndMetaPTEBytesPerFrameC = 0.0;
					v->MetaRowBytesC = 0.0;
					v->DPTEBytesPerRowC = 0.0;
					v->PrefetchLinesC[i][j][k] = 0.0;
					v->PTEBufferSizeNotExceededC[i][j][k] = true;
				}
				v->PDEAndMetaPTEBytesPerFrameY = CalculateVMAndRowBytes(
						mode_lib,
						v->DCCEnable[k],
						v->Read256BlockHeightY[k],
						v->Read256BlockWidthY[k],
						v->SourcePixelFormat[k],
						v->SurfaceTiling[k],
						v->BytePerPixelY[k],
						v->SourceScan[k],
						v->SwathWidthYThisState[k],
						v->ViewportHeight[k],
						v->GPUVMEnable,
						v->HostVMEnable,
						v->HostVMMaxNonCachedPageTableLevels,
						v->GPUVMMinPageSize,
						v->HostVMMinPageSize,
						v->PTEBufferSizeInRequestsForLuma,
						v->PitchY[k],
						v->DCCMetaPitchY[k],
						&v->MacroTileWidthY[k],
						&v->MetaRowBytesY,
						&v->DPTEBytesPerRowY,
						&v->PTEBufferSizeNotExceededY[i][j][k],
						v->dummyinteger4,
						&v->dpte_row_height[k],
						&v->dummyinteger29,
						&v->dummyinteger27,
						&v->dummyinteger24,
						&v->meta_row_height[k],
						&v->dummyinteger25,
						&v->dpte_group_bytes[k],
						&v->dummyinteger21,
						&v->dummyinteger22,
						&v->dummyinteger18,
						&v->dummyinteger5,
						&v->dummyinteger6);
				v->PrefetchLinesY[i][j][k] = CalculatePrefetchSourceLines(
						mode_lib,
						v->VRatio[k],
						v->vtaps[k],
						v->Interlace[k],
						v->ProgressiveToInterlaceUnitInOPP,
						v->SwathHeightYThisState[k],
						v->ViewportYStartY[k],
						&v->PrefillY[k],
						&v->MaxNumSwY[k]);
				v->PDEAndMetaPTEBytesPerFrame[i][j][k] = v->PDEAndMetaPTEBytesPerFrameY + v->PDEAndMetaPTEBytesPerFrameC;
				v->MetaRowBytes[i][j][k] = v->MetaRowBytesY + v->MetaRowBytesC;
				v->DPTEBytesPerRow[i][j][k] = v->DPTEBytesPerRowY + v->DPTEBytesPerRowC;

				CalculateRowBandwidth(
						v->GPUVMEnable,
						v->SourcePixelFormat[k],
						v->VRatio[k],
						v->VRatioChroma[k],
						v->DCCEnable[k],
						v->HTotal[k] / v->PixelClock[k],
						v->MetaRowBytesY,
						v->MetaRowBytesC,
						v->meta_row_height[k],
						v->meta_row_height_chroma[k],
						v->DPTEBytesPerRowY,
						v->DPTEBytesPerRowC,
						v->dpte_row_height[k],
						v->dpte_row_height_chroma[k],
						&v->meta_row_bandwidth[i][j][k],
						&v->dpte_row_bandwidth[i][j][k]);
			}
			v->UrgLatency[i] = CalculateUrgentLatency(
					v->UrgentLatencyPixelDataOnly,
					v->UrgentLatencyPixelMixedWithVMData,
					v->UrgentLatencyVMDataOnly,
					v->DoUrgentLatencyAdjustment,
					v->UrgentLatencyAdjustmentFabricClockComponent,
					v->UrgentLatencyAdjustmentFabricClockReference,
					v->FabricClockPerState[i]);

			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				CalculateUrgentBurstFactor(
						v->swath_width_luma_ub_this_state[k],
						v->swath_width_chroma_ub_this_state[k],
						v->DETBufferSizeInKByte[0],
						v->SwathHeightYThisState[k],
						v->SwathHeightCThisState[k],
						v->HTotal[k] / v->PixelClock[k],
						v->UrgLatency[i],
						v->CursorBufferSize,
						v->CursorWidth[k][0],
						v->CursorBPP[k][0],
						v->VRatio[k],
						v->VRatioChroma[k],
						v->BytePerPixelInDETY[k],
						v->BytePerPixelInDETC[k],
						v->DETBufferSizeYThisState[k],
						v->DETBufferSizeCThisState[k],
						&v->UrgentBurstFactorCursor[k],
						&v->UrgentBurstFactorLuma[k],
						&v->UrgentBurstFactorChroma[k],
						&NotUrgentLatencyHiding[k]);
			}

			v->NotUrgentLatencyHiding[i][j] = false;
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				if (NotUrgentLatencyHiding[k]) {
					v->NotUrgentLatencyHiding[i][j] = true;
				}
			}

			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				v->VActivePixelBandwidth[i][j][k] = v->ReadBandwidthLuma[k] * v->UrgentBurstFactorLuma[k]
						+ v->ReadBandwidthChroma[k] * v->UrgentBurstFactorChroma[k];
				v->VActiveCursorBandwidth[i][j][k] = v->cursor_bw[k] * v->UrgentBurstFactorCursor[k];
			}

			v->TotalVActivePixelBandwidth[i][j] = 0;
			v->TotalVActiveCursorBandwidth[i][j] = 0;
			v->TotalMetaRowBandwidth[i][j] = 0;
			v->TotalDPTERowBandwidth[i][j] = 0;
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				v->TotalVActivePixelBandwidth[i][j] = v->TotalVActivePixelBandwidth[i][j] + v->VActivePixelBandwidth[i][j][k];
				v->TotalVActiveCursorBandwidth[i][j] = v->TotalVActiveCursorBandwidth[i][j] + v->VActiveCursorBandwidth[i][j][k];
				v->TotalMetaRowBandwidth[i][j] = v->TotalMetaRowBandwidth[i][j] + v->NoOfDPP[i][j][k] * v->meta_row_bandwidth[i][j][k];
				v->TotalDPTERowBandwidth[i][j] = v->TotalDPTERowBandwidth[i][j] + v->NoOfDPP[i][j][k] * v->dpte_row_bandwidth[i][j][k];
			}

			CalculateDCFCLKDeepSleep(
					mode_lib,
					v->NumberOfActivePlanes,
					v->BytePerPixelY,
					v->BytePerPixelC,
					v->VRatio,
					v->VRatioChroma,
					v->SwathWidthYThisState,
					v->SwathWidthCThisState,
					v->NoOfDPPThisState,
					v->HRatio,
					v->HRatioChroma,
					v->PixelClock,
					v->PSCL_FACTOR,
					v->PSCL_FACTOR_CHROMA,
					v->RequiredDPPCLKThisState,
					v->ReadBandwidthLuma,
					v->ReadBandwidthChroma,
					v->ReturnBusWidth,
					&v->ProjectedDCFCLKDeepSleep[i][j]);
		}
	}

	//Calculate Return BW

	for (i = start_state; i < mode_lib->soc.num_states; ++i) {
		for (j = 0; j <= 1; ++j) {
			for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
				if (v->BlendingAndTiming[k] == k) {
					if (v->WritebackEnable[k] == true) {
						v->WritebackDelayTime[k] = v->WritebackLatency
								+ CalculateWriteBackDelay(
										v->WritebackPixelFormat[k],
										v->WritebackHRatio[k],
										v->WritebackVRatio[k],
										v->WritebackVTaps[k],
										v->WritebackDestinationWidth[k],
										v->WritebackDestinationHeight[k],
										v->WritebackSourceHeight[k],
										v->HTotal[k]) / v->RequiredDISPCLK[i][j];
					} else {
						v->WritebackDelayTime[k] = 0.0;
					}
					for (m = 0; m <= v->NumberOfActivePlanes - 1; m++) {
						if (v->BlendingAndTiming[m] == k && v->WritebackEnable[m] == true) {
							v->WritebackDelayTime[k] = dml_max(
									v->WritebackDelayTime[k],
									v->WritebackLatency
											+ CalculateWriteBackDelay(
													v->WritebackPixelFormat[m],
													v->WritebackHRatio[m],
													v->WritebackVRatio[m],
													v->WritebackVTaps[m],
													v->WritebackDestinationWidth[m],
													v->WritebackDestinationHeight[m],
													v->WritebackSourceHeight[m],
													v->HTotal[m]) / v->RequiredDISPCLK[i][j]);
						}
					}
				}
			}
			for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
				for (m = 0; m <= v->NumberOfActivePlanes - 1; m++) {
					if (v->BlendingAndTiming[k] == m) {
						v->WritebackDelayTime[k] = v->WritebackDelayTime[m];
					}
				}
			}
			v->MaxMaxVStartup[i][j] = 0;
			for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
				v->MaximumVStartup[i][j][k] = v->VTotal[k] - v->VActive[k]
						- dml_max(1.0, dml_ceil(1.0 * v->WritebackDelayTime[k] / (v->HTotal[k] / v->PixelClock[k]), 1.0));
				v->MaxMaxVStartup[i][j] = dml_max(v->MaxMaxVStartup[i][j], v->MaximumVStartup[i][j][k]);
			}
		}
	}

	ReorderingBytes = v->NumberOfChannels
			* dml_max3(
					v->UrgentOutOfOrderReturnPerChannelPixelDataOnly,
					v->UrgentOutOfOrderReturnPerChannelPixelMixedWithVMData,
					v->UrgentOutOfOrderReturnPerChannelVMDataOnly);
	v->FinalDRAMClockChangeLatency = (v->DRAMClockChangeLatencyOverride > 0 ? v->DRAMClockChangeLatencyOverride : v->DRAMClockChangeLatency);

	for (i = start_state; i < mode_lib->soc.num_states; ++i) {
		for (j = 0; j <= 1; ++j) {
			v->DCFCLKState[i][j] = v->DCFCLKPerState[i];
		}
	}

	if (v->UseMinimumRequiredDCFCLK == true) {
		UseMinimumDCFCLK(mode_lib, v, MaxPrefetchMode, ReorderingBytes);

		if (v->ClampMinDCFCLK) {
			/* Clamp calculated values to actual minimum */
			for (i = start_state; i < mode_lib->soc.num_states; ++i) {
				for (j = 0; j <= 1; ++j) {
					if (v->DCFCLKState[i][j] < mode_lib->soc.min_dcfclk) {
						v->DCFCLKState[i][j] = mode_lib->soc.min_dcfclk;
					}
				}
			}
		}
	}

	for (i = start_state; i < mode_lib->soc.num_states; ++i) {
		for (j = 0; j <= 1; ++j) {
			v->IdealSDPPortBandwidthPerState[i][j] = dml_min3(
					v->ReturnBusWidth * v->DCFCLKState[i][j],
					v->DRAMSpeedPerState[i] * v->NumberOfChannels * v->DRAMChannelWidth,
					v->FabricClockPerState[i] * v->FabricDatapathToDCNDataReturn);
			if (v->HostVMEnable != true) {
				v->ReturnBWPerState[i][j] = v->IdealSDPPortBandwidthPerState[i][j] * v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelDataOnly
						/ 100;
			} else {
				v->ReturnBWPerState[i][j] = v->IdealSDPPortBandwidthPerState[i][j]
						* v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData / 100;
			}
		}
	}

	//Re-ordering Buffer Support Check

	for (i = start_state; i < mode_lib->soc.num_states; ++i) {
		for (j = 0; j <= 1; ++j) {
			if ((v->ROBBufferSizeInKByte - v->PixelChunkSizeInKByte) * 1024 / v->ReturnBWPerState[i][j]
					> (v->RoundTripPingLatencyCycles + 32) / v->DCFCLKState[i][j] + ReorderingBytes / v->ReturnBWPerState[i][j]) {
				v->ROBSupport[i][j] = true;
			} else {
				v->ROBSupport[i][j] = false;
			}
		}
	}

	//Vertical Active BW support check

	MaxTotalVActiveRDBandwidth = 0;
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		MaxTotalVActiveRDBandwidth = MaxTotalVActiveRDBandwidth + v->ReadBandwidthLuma[k] + v->ReadBandwidthChroma[k];
	}

	for (i = start_state; i < mode_lib->soc.num_states; ++i) {
		for (j = 0; j <= 1; ++j) {
			v->MaxTotalVerticalActiveAvailableBandwidth[i][j] = dml_min(
					v->IdealSDPPortBandwidthPerState[i][j] * v->MaxAveragePercentOfIdealSDPPortBWDisplayCanUseInNormalSystemOperation / 100,
					v->DRAMSpeedPerState[i] * v->NumberOfChannels * v->DRAMChannelWidth * v->MaxAveragePercentOfIdealDRAMBWDisplayCanUseInNormalSystemOperation
							/ 100);
			if (MaxTotalVActiveRDBandwidth <= v->MaxTotalVerticalActiveAvailableBandwidth[i][j]) {
				v->TotalVerticalActiveBandwidthSupport[i][j] = true;
			} else {
				v->TotalVerticalActiveBandwidthSupport[i][j] = false;
			}
		}
	}

	//Prefetch Check

	for (i = start_state; i < mode_lib->soc.num_states; ++i) {
		for (j = 0; j <= 1; ++j) {
			int NextPrefetchModeState = MinPrefetchMode;

			v->TimeCalc = 24 / v->ProjectedDCFCLKDeepSleep[i][j];

			v->BandwidthWithoutPrefetchSupported[i][j] = true;
			if (v->TotalVActivePixelBandwidth[i][j] + v->TotalVActiveCursorBandwidth[i][j] + v->TotalMetaRowBandwidth[i][j] + v->TotalDPTERowBandwidth[i][j]
					> v->ReturnBWPerState[i][j] || v->NotUrgentLatencyHiding[i][j]) {
				v->BandwidthWithoutPrefetchSupported[i][j] = false;
			}

			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				v->NoOfDPPThisState[k] = v->NoOfDPP[i][j][k];
				v->swath_width_luma_ub_this_state[k] = v->swath_width_luma_ub_all_states[i][j][k];
				v->swath_width_chroma_ub_this_state[k] = v->swath_width_chroma_ub_all_states[i][j][k];
				v->SwathWidthYThisState[k] = v->SwathWidthYAllStates[i][j][k];
				v->SwathWidthCThisState[k] = v->SwathWidthCAllStates[i][j][k];
				v->SwathHeightYThisState[k] = v->SwathHeightYAllStates[i][j][k];
				v->SwathHeightCThisState[k] = v->SwathHeightCAllStates[i][j][k];
				v->DETBufferSizeYThisState[k] = v->DETBufferSizeYAllStates[i][j][k];
				v->DETBufferSizeCThisState[k] = v->DETBufferSizeCAllStates[i][j][k];
				v->ODMCombineEnabled[k] = v->ODMCombineEnablePerState[i][k];
			}

			v->ExtraLatency = CalculateExtraLatency(
					v->RoundTripPingLatencyCycles,
					ReorderingBytes,
					v->DCFCLKState[i][j],
					v->TotalNumberOfActiveDPP[i][j],
					v->PixelChunkSizeInKByte,
					v->TotalNumberOfDCCActiveDPP[i][j],
					v->MetaChunkSize,
					v->ReturnBWPerState[i][j],
					v->GPUVMEnable,
					v->HostVMEnable,
					v->NumberOfActivePlanes,
					v->NoOfDPPThisState,
					v->dpte_group_bytes,
					v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
					v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
					v->HostVMMinPageSize,
					v->HostVMMaxNonCachedPageTableLevels);

			v->NextMaxVStartup = v->MaxMaxVStartup[i][j];
			do {
				v->PrefetchModePerState[i][j] = NextPrefetchModeState;
				v->MaxVStartup = v->NextMaxVStartup;

				v->TWait = CalculateTWait(v->PrefetchModePerState[i][j], v->FinalDRAMClockChangeLatency, v->UrgLatency[i], v->SREnterPlusExitTime);

				for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
					Pipe myPipe = { 0 };

					myPipe.DPPCLK = v->RequiredDPPCLK[i][j][k];
					myPipe.DISPCLK = v->RequiredDISPCLK[i][j];
					myPipe.PixelClock = v->PixelClock[k];
					myPipe.DCFCLKDeepSleep = v->ProjectedDCFCLKDeepSleep[i][j];
					myPipe.DPPPerPlane = v->NoOfDPP[i][j][k];
					myPipe.ScalerEnabled = v->ScalerEnabled[k];
					myPipe.SourceScan = v->SourceScan[k];
					myPipe.BlockWidth256BytesY = v->Read256BlockWidthY[k];
					myPipe.BlockHeight256BytesY = v->Read256BlockHeightY[k];
					myPipe.BlockWidth256BytesC = v->Read256BlockWidthC[k];
					myPipe.BlockHeight256BytesC = v->Read256BlockHeightC[k];
					myPipe.InterlaceEnable = v->Interlace[k];
					myPipe.NumberOfCursors = v->NumberOfCursors[k];
					myPipe.VBlank = v->VTotal[k] - v->VActive[k];
					myPipe.HTotal = v->HTotal[k];
					myPipe.DCCEnable = v->DCCEnable[k];
					myPipe.ODMCombineEnabled = !!v->ODMCombineEnabled[k];

					v->NoTimeForPrefetch[i][j][k] = CalculatePrefetchSchedule(
							mode_lib,
							v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
							v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
							&myPipe,
							v->DSCDelayPerState[i][k],
							v->DPPCLKDelaySubtotal + v->DPPCLKDelayCNVCFormater,
							v->DPPCLKDelaySCL,
							v->DPPCLKDelaySCLLBOnly,
							v->DPPCLKDelayCNVCCursor,
							v->DISPCLKDelaySubtotal,
							v->SwathWidthYThisState[k] / v->HRatio[k],
							v->OutputFormat[k],
							v->MaxInterDCNTileRepeaters,
							dml_min(v->MaxVStartup, v->MaximumVStartup[i][j][k]),
							v->MaximumVStartup[i][j][k],
							v->GPUVMMaxPageTableLevels,
							v->GPUVMEnable,
							v->HostVMEnable,
							v->HostVMMaxNonCachedPageTableLevels,
							v->HostVMMinPageSize,
							v->DynamicMetadataEnable[k],
							v->DynamicMetadataVMEnabled,
							v->DynamicMetadataLinesBeforeActiveRequired[k],
							v->DynamicMetadataTransmittedBytes[k],
							v->UrgLatency[i],
							v->ExtraLatency,
							v->TimeCalc,
							v->PDEAndMetaPTEBytesPerFrame[i][j][k],
							v->MetaRowBytes[i][j][k],
							v->DPTEBytesPerRow[i][j][k],
							v->PrefetchLinesY[i][j][k],
							v->SwathWidthYThisState[k],
							v->BytePerPixelY[k],
							v->PrefillY[k],
							v->MaxNumSwY[k],
							v->PrefetchLinesC[i][j][k],
							v->SwathWidthCThisState[k],
							v->BytePerPixelC[k],
							v->PrefillC[k],
							v->MaxNumSwC[k],
							v->swath_width_luma_ub_this_state[k],
							v->swath_width_chroma_ub_this_state[k],
							v->SwathHeightYThisState[k],
							v->SwathHeightCThisState[k],
							v->TWait,
							v->ProgressiveToInterlaceUnitInOPP,
							&v->DSTXAfterScaler[k],
							&v->DSTYAfterScaler[k],
							&v->LineTimesForPrefetch[k],
							&v->PrefetchBW[k],
							&v->LinesForMetaPTE[k],
							&v->LinesForMetaAndDPTERow[k],
							&v->VRatioPreY[i][j][k],
							&v->VRatioPreC[i][j][k],
							&v->RequiredPrefetchPixelDataBWLuma[i][j][k],
							&v->RequiredPrefetchPixelDataBWChroma[i][j][k],
							&v->NoTimeForDynamicMetadata[i][j][k],
							&v->Tno_bw[k],
							&v->prefetch_vmrow_bw[k],
							&v->Tdmdl_vm[k],
							&v->Tdmdl[k],
							&v->VUpdateOffsetPix[k],
							&v->VUpdateWidthPix[k],
							&v->VReadyOffsetPix[k]);
				}

				for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
					CalculateUrgentBurstFactor(
							v->swath_width_luma_ub_this_state[k],
							v->swath_width_chroma_ub_this_state[k],
							v->DETBufferSizeInKByte[0],
							v->SwathHeightYThisState[k],
							v->SwathHeightCThisState[k],
							v->HTotal[k] / v->PixelClock[k],
							v->UrgLatency[i],
							v->CursorBufferSize,
							v->CursorWidth[k][0],
							v->CursorBPP[k][0],
							v->VRatioPreY[i][j][k],
							v->VRatioPreC[i][j][k],
							v->BytePerPixelInDETY[k],
							v->BytePerPixelInDETC[k],
							v->DETBufferSizeYThisState[k],
							v->DETBufferSizeCThisState[k],
							&v->UrgentBurstFactorCursorPre[k],
							&v->UrgentBurstFactorLumaPre[k],
							&v->UrgentBurstFactorChromaPre[k],
							&v->NoUrgentLatencyHidingPre[k]);
				}

				v->MaximumReadBandwidthWithPrefetch = 0.0;
				for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
					v->cursor_bw_pre[k] = v->NumberOfCursors[k] * v->CursorWidth[k][0] * v->CursorBPP[k][0] / 8.0 / (v->HTotal[k] / v->PixelClock[k])
							* v->VRatioPreY[i][j][k];

					v->MaximumReadBandwidthWithPrefetch = v->MaximumReadBandwidthWithPrefetch
							+ dml_max4(
									v->VActivePixelBandwidth[i][j][k],
									v->VActiveCursorBandwidth[i][j][k]
											+ v->NoOfDPP[i][j][k] * (v->meta_row_bandwidth[i][j][k] + v->dpte_row_bandwidth[i][j][k]),
									v->NoOfDPP[i][j][k] * v->prefetch_vmrow_bw[k],
									v->NoOfDPP[i][j][k]
											* (v->RequiredPrefetchPixelDataBWLuma[i][j][k] * v->UrgentBurstFactorLumaPre[k]
													+ v->RequiredPrefetchPixelDataBWChroma[i][j][k]
															* v->UrgentBurstFactorChromaPre[k])
											+ v->cursor_bw_pre[k] * v->UrgentBurstFactorCursorPre[k]);
				}

				v->NotEnoughUrgentLatencyHidingPre = false;
				for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
					if (v->NoUrgentLatencyHidingPre[k] == true) {
						v->NotEnoughUrgentLatencyHidingPre = true;
					}
				}

				v->PrefetchSupported[i][j] = true;
				if (v->BandwidthWithoutPrefetchSupported[i][j] == false || v->MaximumReadBandwidthWithPrefetch > v->ReturnBWPerState[i][j]
						|| v->NotEnoughUrgentLatencyHidingPre == 1) {
					v->PrefetchSupported[i][j] = false;
				}
				for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
					if (v->LineTimesForPrefetch[k] < 2.0 || v->LinesForMetaPTE[k] >= 32.0 || v->LinesForMetaAndDPTERow[k] >= 16.0
							|| v->NoTimeForPrefetch[i][j][k] == true) {
						v->PrefetchSupported[i][j] = false;
					}
				}

				v->DynamicMetadataSupported[i][j] = true;
				for (k = 0; k < v->NumberOfActivePlanes; ++k) {
					if (v->NoTimeForDynamicMetadata[i][j][k] == true) {
						v->DynamicMetadataSupported[i][j] = false;
					}
				}

				v->VRatioInPrefetchSupported[i][j] = true;
				for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
					if (v->VRatioPreY[i][j][k] > 4.0 || v->VRatioPreC[i][j][k] > 4.0 || v->NoTimeForPrefetch[i][j][k] == true) {
						v->VRatioInPrefetchSupported[i][j] = false;
					}
				}
				v->AnyLinesForVMOrRowTooLarge = false;
				for (k = 0; k < v->NumberOfActivePlanes; ++k) {
					if (v->LinesForMetaAndDPTERow[k] >= 16 || v->LinesForMetaPTE[k] >= 32) {
						v->AnyLinesForVMOrRowTooLarge = true;
					}
				}

				if (v->PrefetchSupported[i][j] == true && v->VRatioInPrefetchSupported[i][j] == true) {
					v->BandwidthAvailableForImmediateFlip = v->ReturnBWPerState[i][j];
					for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
						v->BandwidthAvailableForImmediateFlip = v->BandwidthAvailableForImmediateFlip
								- dml_max(
										v->VActivePixelBandwidth[i][j][k] + v->VActiveCursorBandwidth[i][j][k],
										v->NoOfDPP[i][j][k]
												* (v->RequiredPrefetchPixelDataBWLuma[i][j][k] * v->UrgentBurstFactorLumaPre[k]
														+ v->RequiredPrefetchPixelDataBWChroma[i][j][k]
																* v->UrgentBurstFactorChromaPre[k])
												+ v->cursor_bw_pre[k] * v->UrgentBurstFactorCursorPre[k]);
					}
					v->TotImmediateFlipBytes = 0.0;
					for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
						v->TotImmediateFlipBytes = v->TotImmediateFlipBytes + v->NoOfDPP[i][j][k] * (v->PDEAndMetaPTEBytesPerFrame[i][j][k]
								+ v->MetaRowBytes[i][j][k] + v->DPTEBytesPerRow[i][j][k]);
					}

					for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
						CalculateFlipSchedule(
								mode_lib,
								v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
								v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
								v->ExtraLatency,
								v->UrgLatency[i],
								v->GPUVMMaxPageTableLevels,
								v->HostVMEnable,
								v->HostVMMaxNonCachedPageTableLevels,
								v->GPUVMEnable,
								v->HostVMMinPageSize,
								v->PDEAndMetaPTEBytesPerFrame[i][j][k],
								v->MetaRowBytes[i][j][k],
								v->DPTEBytesPerRow[i][j][k],
								v->BandwidthAvailableForImmediateFlip,
								v->TotImmediateFlipBytes,
								v->SourcePixelFormat[k],
								v->HTotal[k] / v->PixelClock[k],
								v->VRatio[k],
								v->VRatioChroma[k],
								v->Tno_bw[k],
								v->DCCEnable[k],
								v->dpte_row_height[k],
								v->meta_row_height[k],
								v->dpte_row_height_chroma[k],
								v->meta_row_height_chroma[k],
								&v->DestinationLinesToRequestVMInImmediateFlip[k],
								&v->DestinationLinesToRequestRowInImmediateFlip[k],
								&v->final_flip_bw[k],
								&v->ImmediateFlipSupportedForPipe[k]);
					}
					v->total_dcn_read_bw_with_flip = 0.0;
					for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
						v->total_dcn_read_bw_with_flip = v->total_dcn_read_bw_with_flip
								+ dml_max3(
										v->NoOfDPP[i][j][k] * v->prefetch_vmrow_bw[k],
										v->NoOfDPP[i][j][k] * v->final_flip_bw[k] + v->VActivePixelBandwidth[i][j][k]
												+ v->VActiveCursorBandwidth[i][j][k],
										v->NoOfDPP[i][j][k]
												* (v->final_flip_bw[k]
														+ v->RequiredPrefetchPixelDataBWLuma[i][j][k]
																* v->UrgentBurstFactorLumaPre[k]
														+ v->RequiredPrefetchPixelDataBWChroma[i][j][k]
																* v->UrgentBurstFactorChromaPre[k])
												+ v->cursor_bw_pre[k] * v->UrgentBurstFactorCursorPre[k]);
					}
					v->ImmediateFlipSupportedForState[i][j] = true;
					if (v->total_dcn_read_bw_with_flip > v->ReturnBWPerState[i][j]) {
						v->ImmediateFlipSupportedForState[i][j] = false;
					}
					for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
						if (v->ImmediateFlipSupportedForPipe[k] == false) {
							v->ImmediateFlipSupportedForState[i][j] = false;
						}
					}
				} else {
					v->ImmediateFlipSupportedForState[i][j] = false;
				}
				if (v->MaxVStartup <= 13 || v->AnyLinesForVMOrRowTooLarge == false) {
					v->NextMaxVStartup = v->MaxMaxVStartup[i][j];
					NextPrefetchModeState = NextPrefetchModeState + 1;
				} else {
					v->NextMaxVStartup = v->NextMaxVStartup - 1;
				}
			} while (!((v->PrefetchSupported[i][j] == true && v->DynamicMetadataSupported[i][j] == true && v->VRatioInPrefetchSupported[i][j] == true
					&& ((v->HostVMEnable == false && v->ImmediateFlipRequirement[0] != dm_immediate_flip_required)
							|| v->ImmediateFlipSupportedForState[i][j] == true))
					|| (v->NextMaxVStartup == v->MaxMaxVStartup[i][j] && NextPrefetchModeState > MaxPrefetchMode)));

			CalculateWatermarksAndDRAMSpeedChangeSupport(
					mode_lib,
					v->PrefetchModePerState[i][j],
					v->NumberOfActivePlanes,
					v->MaxLineBufferLines,
					v->LineBufferSize,
					v->DPPOutputBufferPixels,
					v->DETBufferSizeInKByte[0],
					v->WritebackInterfaceBufferSize,
					v->DCFCLKState[i][j],
					v->ReturnBWPerState[i][j],
					v->GPUVMEnable,
					v->dpte_group_bytes,
					v->MetaChunkSize,
					v->UrgLatency[i],
					v->ExtraLatency,
					v->WritebackLatency,
					v->WritebackChunkSize,
					v->SOCCLKPerState[i],
					v->FinalDRAMClockChangeLatency,
					v->SRExitTime,
					v->SREnterPlusExitTime,
					v->ProjectedDCFCLKDeepSleep[i][j],
					v->NoOfDPPThisState,
					v->DCCEnable,
					v->RequiredDPPCLKThisState,
					v->DETBufferSizeYThisState,
					v->DETBufferSizeCThisState,
					v->SwathHeightYThisState,
					v->SwathHeightCThisState,
					v->LBBitPerPixel,
					v->SwathWidthYThisState,
					v->SwathWidthCThisState,
					v->HRatio,
					v->HRatioChroma,
					v->vtaps,
					v->VTAPsChroma,
					v->VRatio,
					v->VRatioChroma,
					v->HTotal,
					v->PixelClock,
					v->BlendingAndTiming,
					v->BytePerPixelInDETY,
					v->BytePerPixelInDETC,
					v->DSTXAfterScaler,
					v->DSTYAfterScaler,
					v->WritebackEnable,
					v->WritebackPixelFormat,
					v->WritebackDestinationWidth,
					v->WritebackDestinationHeight,
					v->WritebackSourceHeight,
					&v->DRAMClockChangeSupport[i][j],
					&v->UrgentWatermark,
					&v->WritebackUrgentWatermark,
					&v->DRAMClockChangeWatermark,
					&v->WritebackDRAMClockChangeWatermark,
					&v->StutterExitWatermark,
					&v->StutterEnterPlusExitWatermark,
					&v->MinActiveDRAMClockChangeLatencySupported);
		}
	}

	/*PTE Buffer Size Check*/

	for (i = start_state; i < v->soc.num_states; i++) {
		for (j = 0; j < 2; j++) {
			v->PTEBufferSizeNotExceeded[i][j] = true;
			for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
				if (v->PTEBufferSizeNotExceededY[i][j][k] == false || v->PTEBufferSizeNotExceededC[i][j][k] == false) {
					v->PTEBufferSizeNotExceeded[i][j] = false;
				}
			}
		}
	}
	/*Cursor Support Check*/

	v->CursorSupport = true;
	for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
		if (v->CursorWidth[k][0] > 0.0) {
			if (v->CursorBPP[k][0] == 64 && v->Cursor64BppSupport == false) {
				v->CursorSupport = false;
			}
		}
	}
	/*Valid Pitch Check*/

	v->PitchSupport = true;
	for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
		v->AlignedYPitch[k] = dml_ceil(dml_max(v->PitchY[k], v->SurfaceWidthY[k]), v->MacroTileWidthY[k]);
		if (v->DCCEnable[k] == true) {
			v->AlignedDCCMetaPitchY[k] = dml_ceil(dml_max(v->DCCMetaPitchY[k], v->SurfaceWidthY[k]), 64.0 * v->Read256BlockWidthY[k]);
		} else {
			v->AlignedDCCMetaPitchY[k] = v->DCCMetaPitchY[k];
		}
		if (v->SourcePixelFormat[k] != dm_444_64 && v->SourcePixelFormat[k] != dm_444_32 && v->SourcePixelFormat[k] != dm_444_16 && v->SourcePixelFormat[k] != dm_mono_16
				&& v->SourcePixelFormat[k] != dm_rgbe && v->SourcePixelFormat[k] != dm_mono_8) {
			v->AlignedCPitch[k] = dml_ceil(dml_max(v->PitchC[k], v->SurfaceWidthC[k]), v->MacroTileWidthC[k]);
			if (v->DCCEnable[k] == true) {
				v->AlignedDCCMetaPitchC[k] = dml_ceil(dml_max(v->DCCMetaPitchC[k], v->SurfaceWidthC[k]), 64.0 * v->Read256BlockWidthC[k]);
			} else {
				v->AlignedDCCMetaPitchC[k] = v->DCCMetaPitchC[k];
			}
		} else {
			v->AlignedCPitch[k] = v->PitchC[k];
			v->AlignedDCCMetaPitchC[k] = v->DCCMetaPitchC[k];
		}
		if (v->AlignedYPitch[k] > v->PitchY[k] || v->AlignedCPitch[k] > v->PitchC[k] || v->AlignedDCCMetaPitchY[k] > v->DCCMetaPitchY[k]
				|| v->AlignedDCCMetaPitchC[k] > v->DCCMetaPitchC[k]) {
			v->PitchSupport = false;
		}
	}

	for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
		if (v->ViewportWidth[k] > v->SurfaceWidthY[k] || v->ViewportHeight[k] > v->SurfaceHeightY[k])
			ViewportExceedsSurface = true;

		if (v->SourcePixelFormat[k] != dm_444_64 && v->SourcePixelFormat[k] != dm_444_32 && v->SourcePixelFormat[k] != dm_444_16
				&& v->SourcePixelFormat[k] != dm_444_8 && v->SourcePixelFormat[k] != dm_rgbe) {
			if (v->ViewportWidthChroma[k] > v->SurfaceWidthC[k] || v->ViewportHeightChroma[k] > v->SurfaceHeightC[k]) {
				ViewportExceedsSurface = true;
			}
		}
	}
	/*Mode Support, Voltage State and SOC Configuration*/

	for (i = v->soc.num_states - 1; i >= start_state; i--) {
		for (j = 0; j < 2; j++) {
			if (v->ScaleRatioAndTapsSupport == 1 && v->SourceFormatPixelAndScanSupport == 1 && v->ViewportSizeSupport[i][j] == 1
					&& v->DIOSupport[i] == 1 && v->ODMCombine4To1SupportCheckOK[i] == 1
					&& v->NotEnoughDSCUnits[i] == 0
					&& v->DTBCLKRequiredMoreThanSupported[i] == 0
					&& v->ROBSupport[i][j] == 1 && v->DISPCLK_DPPCLK_Support[i][j] == 1 && v->TotalAvailablePipesSupport[i][j] == 1
					&& EnoughWritebackUnits == 1 && WritebackModeSupport == 1
					&& v->WritebackLatencySupport == 1 && v->WritebackScaleRatioAndTapsSupport == 1 && v->CursorSupport == 1 && v->PitchSupport == 1
					&& ViewportExceedsSurface == 0 && v->PrefetchSupported[i][j] == 1 && v->DynamicMetadataSupported[i][j] == 1
					&& v->TotalVerticalActiveBandwidthSupport[i][j] == 1 && v->VRatioInPrefetchSupported[i][j] == 1
					&& v->PTEBufferSizeNotExceeded[i][j] == 1 && v->NonsupportedDSCInputBPC == 0
					&& ((v->HostVMEnable == 0 && v->ImmediateFlipRequirement[0] != dm_immediate_flip_required)
							|| v->ImmediateFlipSupportedForState[i][j] == true)) {
				v->ModeSupport[i][j] = true;
			} else {
				v->ModeSupport[i][j] = false;
			}
		}
	}
	{
		unsigned int MaximumMPCCombine = 0;
		for (i = v->soc.num_states; i >= start_state; i--) {
			if (i == v->soc.num_states || v->ModeSupport[i][0] == true || v->ModeSupport[i][1] == true) {
				v->VoltageLevel = i;
				v->ModeIsSupported = v->ModeSupport[i][0] == true || v->ModeSupport[i][1] == true;
				if (v->ModeSupport[i][1] == true) {
					MaximumMPCCombine = 1;
				} else {
					MaximumMPCCombine = 0;
				}
			}
		}
		v->ImmediateFlipSupport = v->ImmediateFlipSupportedForState[v->VoltageLevel][MaximumMPCCombine];
		for (k = 0; k <= v->NumberOfActivePlanes - 1; k++) {
			v->MPCCombineEnable[k] = v->MPCCombine[v->VoltageLevel][MaximumMPCCombine][k];
			v->DPPPerPlane[k] = v->NoOfDPP[v->VoltageLevel][MaximumMPCCombine][k];
		}
		v->DCFCLK = v->DCFCLKState[v->VoltageLevel][MaximumMPCCombine];
		v->DRAMSpeed = v->DRAMSpeedPerState[v->VoltageLevel];
		v->FabricClock = v->FabricClockPerState[v->VoltageLevel];
		v->SOCCLK = v->SOCCLKPerState[v->VoltageLevel];
		v->ReturnBW = v->ReturnBWPerState[v->VoltageLevel][MaximumMPCCombine];
		v->maxMpcComb = MaximumMPCCombine;
	}
}

static void CalculateWatermarksAndDRAMSpeedChangeSupport(
		struct display_mode_lib *mode_lib,
		unsigned int PrefetchMode,
		unsigned int NumberOfActivePlanes,
		unsigned int MaxLineBufferLines,
		unsigned int LineBufferSize,
		unsigned int DPPOutputBufferPixels,
		unsigned int DETBufferSizeInKByte,
		unsigned int WritebackInterfaceBufferSize,
		double DCFCLK,
		double ReturnBW,
		bool GPUVMEnable,
		unsigned int dpte_group_bytes[],
		unsigned int MetaChunkSize,
		double UrgentLatency,
		double ExtraLatency,
		double WritebackLatency,
		double WritebackChunkSize,
		double SOCCLK,
		double DRAMClockChangeLatency,
		double SRExitTime,
		double SREnterPlusExitTime,
		double DCFCLKDeepSleep,
		unsigned int DPPPerPlane[],
		bool DCCEnable[],
		double DPPCLK[],
		unsigned int DETBufferSizeY[],
		unsigned int DETBufferSizeC[],
		unsigned int SwathHeightY[],
		unsigned int SwathHeightC[],
		unsigned int LBBitPerPixel[],
		double SwathWidthY[],
		double SwathWidthC[],
		double HRatio[],
		double HRatioChroma[],
		unsigned int vtaps[],
		unsigned int VTAPsChroma[],
		double VRatio[],
		double VRatioChroma[],
		unsigned int HTotal[],
		double PixelClock[],
		unsigned int BlendingAndTiming[],
		double BytePerPixelDETY[],
		double BytePerPixelDETC[],
		double DSTXAfterScaler[],
		double DSTYAfterScaler[],
		bool WritebackEnable[],
		enum source_format_class WritebackPixelFormat[],
		double WritebackDestinationWidth[],
		double WritebackDestinationHeight[],
		double WritebackSourceHeight[],
		enum clock_change_support *DRAMClockChangeSupport,
		double *UrgentWatermark,
		double *WritebackUrgentWatermark,
		double *DRAMClockChangeWatermark,
		double *WritebackDRAMClockChangeWatermark,
		double *StutterExitWatermark,
		double *StutterEnterPlusExitWatermark,
		double *MinActiveDRAMClockChangeLatencySupported)
{
	double EffectiveLBLatencyHidingY = 0;
	double EffectiveLBLatencyHidingC = 0;
	double LinesInDETY[DC__NUM_DPP__MAX] = { 0 };
	double LinesInDETC = 0;
	unsigned int LinesInDETYRoundedDownToSwath[DC__NUM_DPP__MAX] = { 0 };
	unsigned int LinesInDETCRoundedDownToSwath = 0;
	double FullDETBufferingTimeY[DC__NUM_DPP__MAX] = { 0 };
	double FullDETBufferingTimeC = 0;
	double ActiveDRAMClockChangeLatencyMarginY = 0;
	double ActiveDRAMClockChangeLatencyMarginC = 0;
	double WritebackDRAMClockChangeLatencyMargin = 0;
	double PlaneWithMinActiveDRAMClockChangeMargin = 0;
	double SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank = 0;
	double FullDETBufferingTimeYStutterCriticalPlane = 0;
	double TimeToFinishSwathTransferStutterCriticalPlane = 0;
	double WritebackDRAMClockChangeLatencyHiding = 0;
	unsigned int k, j;

	mode_lib->vba.TotalActiveDPP = 0;
	mode_lib->vba.TotalDCCActiveDPP = 0;
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		mode_lib->vba.TotalActiveDPP = mode_lib->vba.TotalActiveDPP + DPPPerPlane[k];
		if (DCCEnable[k] == true) {
			mode_lib->vba.TotalDCCActiveDPP = mode_lib->vba.TotalDCCActiveDPP + DPPPerPlane[k];
		}
	}

	*UrgentWatermark = UrgentLatency + ExtraLatency;

	*DRAMClockChangeWatermark = DRAMClockChangeLatency + *UrgentWatermark;

	mode_lib->vba.TotalActiveWriteback = 0;
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (WritebackEnable[k] == true) {
			mode_lib->vba.TotalActiveWriteback = mode_lib->vba.TotalActiveWriteback + 1;
		}
	}

	if (mode_lib->vba.TotalActiveWriteback <= 1) {
		*WritebackUrgentWatermark = WritebackLatency;
	} else {
		*WritebackUrgentWatermark = WritebackLatency + WritebackChunkSize * 1024.0 / 32.0 / SOCCLK;
	}

	if (mode_lib->vba.TotalActiveWriteback <= 1) {
		*WritebackDRAMClockChangeWatermark = DRAMClockChangeLatency + WritebackLatency;
	} else {
		*WritebackDRAMClockChangeWatermark = DRAMClockChangeLatency + WritebackLatency + WritebackChunkSize * 1024.0 / 32.0 / SOCCLK;
	}

	for (k = 0; k < NumberOfActivePlanes; ++k) {

		mode_lib->vba.LBLatencyHidingSourceLinesY = dml_min((double) MaxLineBufferLines, dml_floor(LineBufferSize / LBBitPerPixel[k] / (SwathWidthY[k] / dml_max(HRatio[k], 1.0)), 1)) - (vtaps[k] - 1);

		mode_lib->vba.LBLatencyHidingSourceLinesC = dml_min((double) MaxLineBufferLines, dml_floor(LineBufferSize / LBBitPerPixel[k] / (SwathWidthC[k] / dml_max(HRatioChroma[k], 1.0)), 1)) - (VTAPsChroma[k] - 1);

		EffectiveLBLatencyHidingY = mode_lib->vba.LBLatencyHidingSourceLinesY / VRatio[k] * (HTotal[k] / PixelClock[k]);

		EffectiveLBLatencyHidingC = mode_lib->vba.LBLatencyHidingSourceLinesC / VRatioChroma[k] * (HTotal[k] / PixelClock[k]);

		LinesInDETY[k] = (double) DETBufferSizeY[k] / BytePerPixelDETY[k] / SwathWidthY[k];
		LinesInDETYRoundedDownToSwath[k] = dml_floor(LinesInDETY[k], SwathHeightY[k]);
		FullDETBufferingTimeY[k] = LinesInDETYRoundedDownToSwath[k] * (HTotal[k] / PixelClock[k]) / VRatio[k];
		if (BytePerPixelDETC[k] > 0) {
			LinesInDETC = mode_lib->vba.DETBufferSizeC[k] / BytePerPixelDETC[k] / SwathWidthC[k];
			LinesInDETCRoundedDownToSwath = dml_floor(LinesInDETC, SwathHeightC[k]);
			FullDETBufferingTimeC = LinesInDETCRoundedDownToSwath * (HTotal[k] / PixelClock[k]) / VRatioChroma[k];
		} else {
			LinesInDETC = 0;
			FullDETBufferingTimeC = 999999;
		}

		ActiveDRAMClockChangeLatencyMarginY = EffectiveLBLatencyHidingY + FullDETBufferingTimeY[k] - *UrgentWatermark - (HTotal[k] / PixelClock[k]) * (DSTXAfterScaler[k] / HTotal[k] + DSTYAfterScaler[k]) - *DRAMClockChangeWatermark;

		if (NumberOfActivePlanes > 1) {
			ActiveDRAMClockChangeLatencyMarginY = ActiveDRAMClockChangeLatencyMarginY - (1 - 1.0 / NumberOfActivePlanes) * SwathHeightY[k] * HTotal[k] / PixelClock[k] / VRatio[k];
		}

		if (BytePerPixelDETC[k] > 0) {
			ActiveDRAMClockChangeLatencyMarginC = EffectiveLBLatencyHidingC + FullDETBufferingTimeC - *UrgentWatermark - (HTotal[k] / PixelClock[k]) * (DSTXAfterScaler[k] / HTotal[k] + DSTYAfterScaler[k]) - *DRAMClockChangeWatermark;

			if (NumberOfActivePlanes > 1) {
				ActiveDRAMClockChangeLatencyMarginC = ActiveDRAMClockChangeLatencyMarginC - (1 - 1.0 / NumberOfActivePlanes) * SwathHeightC[k] * HTotal[k] / PixelClock[k] / VRatioChroma[k];
			}
			mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k] = dml_min(ActiveDRAMClockChangeLatencyMarginY, ActiveDRAMClockChangeLatencyMarginC);
		} else {
			mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k] = ActiveDRAMClockChangeLatencyMarginY;
		}

		if (WritebackEnable[k] == true) {

			WritebackDRAMClockChangeLatencyHiding = WritebackInterfaceBufferSize * 1024 / (WritebackDestinationWidth[k] * WritebackDestinationHeight[k] / (WritebackSourceHeight[k] * HTotal[k] / PixelClock[k]) * 4);
			if (WritebackPixelFormat[k] == dm_444_64) {
				WritebackDRAMClockChangeLatencyHiding = WritebackDRAMClockChangeLatencyHiding / 2;
			}
			if (mode_lib->vba.WritebackConfiguration == dm_whole_buffer_for_single_stream_interleave) {
				WritebackDRAMClockChangeLatencyHiding = WritebackDRAMClockChangeLatencyHiding * 2;
			}
			WritebackDRAMClockChangeLatencyMargin = WritebackDRAMClockChangeLatencyHiding - mode_lib->vba.WritebackDRAMClockChangeWatermark;
			mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k] = dml_min(mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k], WritebackDRAMClockChangeLatencyMargin);
		}
	}

	mode_lib->vba.MinActiveDRAMClockChangeMargin = 999999;
	PlaneWithMinActiveDRAMClockChangeMargin = 0;
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k] < mode_lib->vba.MinActiveDRAMClockChangeMargin) {
			mode_lib->vba.MinActiveDRAMClockChangeMargin = mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k];
			if (BlendingAndTiming[k] == k) {
				PlaneWithMinActiveDRAMClockChangeMargin = k;
			} else {
				for (j = 0; j < NumberOfActivePlanes; ++j) {
					if (BlendingAndTiming[k] == j) {
						PlaneWithMinActiveDRAMClockChangeMargin = j;
					}
				}
			}
		}
	}

	*MinActiveDRAMClockChangeLatencySupported = mode_lib->vba.MinActiveDRAMClockChangeMargin + DRAMClockChangeLatency;

	SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank = 999999;
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (!((k == PlaneWithMinActiveDRAMClockChangeMargin) && (BlendingAndTiming[k] == k)) && !(BlendingAndTiming[k] == PlaneWithMinActiveDRAMClockChangeMargin) && mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k] < SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank) {
			SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank = mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k];
		}
	}

	mode_lib->vba.TotalNumberOfActiveOTG = 0;
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (BlendingAndTiming[k] == k) {
			mode_lib->vba.TotalNumberOfActiveOTG = mode_lib->vba.TotalNumberOfActiveOTG + 1;
		}
	}

	if (mode_lib->vba.MinActiveDRAMClockChangeMargin > 0) {
		*DRAMClockChangeSupport = dm_dram_clock_change_vactive;
	} else if (((mode_lib->vba.SynchronizedVBlank == true || mode_lib->vba.TotalNumberOfActiveOTG == 1 || SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank > 0) && PrefetchMode == 0)) {
		*DRAMClockChangeSupport = dm_dram_clock_change_vblank;
	} else {
		*DRAMClockChangeSupport = dm_dram_clock_change_unsupported;
	}

	FullDETBufferingTimeYStutterCriticalPlane = FullDETBufferingTimeY[0];
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (FullDETBufferingTimeY[k] <= FullDETBufferingTimeYStutterCriticalPlane) {
			FullDETBufferingTimeYStutterCriticalPlane = FullDETBufferingTimeY[k];
			TimeToFinishSwathTransferStutterCriticalPlane = (SwathHeightY[k] - (LinesInDETY[k] - LinesInDETYRoundedDownToSwath[k])) * (HTotal[k] / PixelClock[k]) / VRatio[k];
		}
	}

	*StutterExitWatermark = SRExitTime +  ExtraLatency + 10 / DCFCLKDeepSleep;
	*StutterEnterPlusExitWatermark = dml_max(SREnterPlusExitTime + ExtraLatency + 10 / DCFCLKDeepSleep, TimeToFinishSwathTransferStutterCriticalPlane);

}

static void CalculateDCFCLKDeepSleep(
		struct display_mode_lib *mode_lib,
		unsigned int NumberOfActivePlanes,
		int BytePerPixelY[],
		int BytePerPixelC[],
		double VRatio[],
		double VRatioChroma[],
		double SwathWidthY[],
		double SwathWidthC[],
		unsigned int DPPPerPlane[],
		double HRatio[],
		double HRatioChroma[],
		double PixelClock[],
		double PSCL_THROUGHPUT[],
		double PSCL_THROUGHPUT_CHROMA[],
		double DPPCLK[],
		double ReadBandwidthLuma[],
		double ReadBandwidthChroma[],
		int ReturnBusWidth,
		double *DCFCLKDeepSleep)
{
	double DisplayPipeLineDeliveryTimeLuma = 0;
	double DisplayPipeLineDeliveryTimeChroma = 0;
	unsigned int k;
	double ReadBandwidth = 0.0;

	//double   DCFCLKDeepSleepPerPlane[DC__NUM_DPP__MAX];
	for (k = 0; k < NumberOfActivePlanes; ++k) {

		if (VRatio[k] <= 1) {
			DisplayPipeLineDeliveryTimeLuma = SwathWidthY[k] * DPPPerPlane[k] / HRatio[k] / PixelClock[k];
		} else {
			DisplayPipeLineDeliveryTimeLuma = SwathWidthY[k] / PSCL_THROUGHPUT[k] / DPPCLK[k];
		}
		if (BytePerPixelC[k] == 0) {
			DisplayPipeLineDeliveryTimeChroma = 0;
		} else {
			if (VRatioChroma[k] <= 1) {
				DisplayPipeLineDeliveryTimeChroma = SwathWidthC[k] * DPPPerPlane[k] / HRatioChroma[k] / PixelClock[k];
			} else {
				DisplayPipeLineDeliveryTimeChroma = SwathWidthC[k] / PSCL_THROUGHPUT_CHROMA[k] / DPPCLK[k];
			}
		}

		if (BytePerPixelC[k] > 0) {
			mode_lib->vba.DCFCLKDeepSleepPerPlane[k] = dml_max(1.1 * SwathWidthY[k] * BytePerPixelY[k] / 32.0 / DisplayPipeLineDeliveryTimeLuma, 1.1 * SwathWidthC[k] * BytePerPixelC[k] / 32.0 / DisplayPipeLineDeliveryTimeChroma);
		} else {
			mode_lib->vba.DCFCLKDeepSleepPerPlane[k] = 1.1 * SwathWidthY[k] * BytePerPixelY[k] / 64.0 / DisplayPipeLineDeliveryTimeLuma;
		}
		mode_lib->vba.DCFCLKDeepSleepPerPlane[k] = dml_max(mode_lib->vba.DCFCLKDeepSleepPerPlane[k], PixelClock[k] / 16);

	}

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		ReadBandwidth = ReadBandwidth + ReadBandwidthLuma[k] + ReadBandwidthChroma[k];
	}

	*DCFCLKDeepSleep = dml_max(8.0, ReadBandwidth / ReturnBusWidth);

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		*DCFCLKDeepSleep = dml_max(*DCFCLKDeepSleep, mode_lib->vba.DCFCLKDeepSleepPerPlane[k]);
	}
}

static void CalculateUrgentBurstFactor(
		long swath_width_luma_ub,
		long swath_width_chroma_ub,
		unsigned int DETBufferSizeInKByte,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		double LineTime,
		double UrgentLatency,
		double CursorBufferSize,
		unsigned int CursorWidth,
		unsigned int CursorBPP,
		double VRatio,
		double VRatioC,
		double BytePerPixelInDETY,
		double BytePerPixelInDETC,
		double DETBufferSizeY,
		double DETBufferSizeC,
		double *UrgentBurstFactorCursor,
		double *UrgentBurstFactorLuma,
		double *UrgentBurstFactorChroma,
		bool *NotEnoughUrgentLatencyHiding)
{
	double LinesInDETLuma = 0;
	double LinesInDETChroma = 0;
	unsigned int LinesInCursorBuffer = 0;
	double CursorBufferSizeInTime = 0;
	double DETBufferSizeInTimeLuma = 0;
	double DETBufferSizeInTimeChroma = 0;

	*NotEnoughUrgentLatencyHiding = 0;

	if (CursorWidth > 0) {
		LinesInCursorBuffer = 1 << (unsigned int) dml_floor(dml_log2(CursorBufferSize * 1024.0 / (CursorWidth * CursorBPP / 8.0)), 1.0);
		if (VRatio > 0) {
			CursorBufferSizeInTime = LinesInCursorBuffer * LineTime / VRatio;
			if (CursorBufferSizeInTime - UrgentLatency <= 0) {
				*NotEnoughUrgentLatencyHiding = 1;
				*UrgentBurstFactorCursor = 0;
			} else {
				*UrgentBurstFactorCursor = CursorBufferSizeInTime / (CursorBufferSizeInTime - UrgentLatency);
			}
		} else {
			*UrgentBurstFactorCursor = 1;
		}
	}

	LinesInDETLuma = DETBufferSizeY / BytePerPixelInDETY / swath_width_luma_ub;
	if (VRatio > 0) {
		DETBufferSizeInTimeLuma = dml_floor(LinesInDETLuma, SwathHeightY) * LineTime / VRatio;
		if (DETBufferSizeInTimeLuma - UrgentLatency <= 0) {
			*NotEnoughUrgentLatencyHiding = 1;
			*UrgentBurstFactorLuma = 0;
		} else {
			*UrgentBurstFactorLuma = DETBufferSizeInTimeLuma / (DETBufferSizeInTimeLuma - UrgentLatency);
		}
	} else {
		*UrgentBurstFactorLuma = 1;
	}

	if (BytePerPixelInDETC > 0) {
		LinesInDETChroma = DETBufferSizeC / BytePerPixelInDETC / swath_width_chroma_ub;
		if (VRatio > 0) {
			DETBufferSizeInTimeChroma = dml_floor(LinesInDETChroma, SwathHeightC) * LineTime / VRatio;
			if (DETBufferSizeInTimeChroma - UrgentLatency <= 0) {
				*NotEnoughUrgentLatencyHiding = 1;
				*UrgentBurstFactorChroma = 0;
			} else {
				*UrgentBurstFactorChroma = DETBufferSizeInTimeChroma / (DETBufferSizeInTimeChroma - UrgentLatency);
			}
		} else {
			*UrgentBurstFactorChroma = 1;
		}
	}
}

static void CalculatePixelDeliveryTimes(
		unsigned int NumberOfActivePlanes,
		double VRatio[],
		double VRatioChroma[],
		double VRatioPrefetchY[],
		double VRatioPrefetchC[],
		unsigned int swath_width_luma_ub[],
		unsigned int swath_width_chroma_ub[],
		unsigned int DPPPerPlane[],
		double HRatio[],
		double HRatioChroma[],
		double PixelClock[],
		double PSCL_THROUGHPUT[],
		double PSCL_THROUGHPUT_CHROMA[],
		double DPPCLK[],
		int BytePerPixelC[],
		enum scan_direction_class SourceScan[],
		unsigned int NumberOfCursors[],
		unsigned int CursorWidth[][2],
		unsigned int CursorBPP[][2],
		unsigned int BlockWidth256BytesY[],
		unsigned int BlockHeight256BytesY[],
		unsigned int BlockWidth256BytesC[],
		unsigned int BlockHeight256BytesC[],
		double DisplayPipeLineDeliveryTimeLuma[],
		double DisplayPipeLineDeliveryTimeChroma[],
		double DisplayPipeLineDeliveryTimeLumaPrefetch[],
		double DisplayPipeLineDeliveryTimeChromaPrefetch[],
		double DisplayPipeRequestDeliveryTimeLuma[],
		double DisplayPipeRequestDeliveryTimeChroma[],
		double DisplayPipeRequestDeliveryTimeLumaPrefetch[],
		double DisplayPipeRequestDeliveryTimeChromaPrefetch[],
		double CursorRequestDeliveryTime[],
		double CursorRequestDeliveryTimePrefetch[])
{
	double req_per_swath_ub = 0;
	unsigned int k;

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (VRatio[k] <= 1) {
			DisplayPipeLineDeliveryTimeLuma[k] = swath_width_luma_ub[k] * DPPPerPlane[k] / HRatio[k] / PixelClock[k];
		} else {
			DisplayPipeLineDeliveryTimeLuma[k] = swath_width_luma_ub[k] / PSCL_THROUGHPUT[k] / DPPCLK[k];
		}

		if (BytePerPixelC[k] == 0) {
			DisplayPipeLineDeliveryTimeChroma[k] = 0;
		} else {
			if (VRatioChroma[k] <= 1) {
				DisplayPipeLineDeliveryTimeChroma[k] = swath_width_chroma_ub[k] * DPPPerPlane[k] / HRatioChroma[k] / PixelClock[k];
			} else {
				DisplayPipeLineDeliveryTimeChroma[k] = swath_width_chroma_ub[k] / PSCL_THROUGHPUT_CHROMA[k] / DPPCLK[k];
			}
		}

		if (VRatioPrefetchY[k] <= 1) {
			DisplayPipeLineDeliveryTimeLumaPrefetch[k] = swath_width_luma_ub[k] * DPPPerPlane[k] / HRatio[k] / PixelClock[k];
		} else {
			DisplayPipeLineDeliveryTimeLumaPrefetch[k] = swath_width_luma_ub[k] / PSCL_THROUGHPUT[k] / DPPCLK[k];
		}

		if (BytePerPixelC[k] == 0) {
			DisplayPipeLineDeliveryTimeChromaPrefetch[k] = 0;
		} else {
			if (VRatioPrefetchC[k] <= 1) {
				DisplayPipeLineDeliveryTimeChromaPrefetch[k] = swath_width_chroma_ub[k] * DPPPerPlane[k] / HRatioChroma[k] / PixelClock[k];
			} else {
				DisplayPipeLineDeliveryTimeChromaPrefetch[k] = swath_width_chroma_ub[k] / PSCL_THROUGHPUT_CHROMA[k] / DPPCLK[k];
			}
		}
	}

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (SourceScan[k] != dm_vert) {
			req_per_swath_ub = swath_width_luma_ub[k] / BlockWidth256BytesY[k];
		} else {
			req_per_swath_ub = swath_width_luma_ub[k] / BlockHeight256BytesY[k];
		}
		DisplayPipeRequestDeliveryTimeLuma[k] = DisplayPipeLineDeliveryTimeLuma[k] / req_per_swath_ub;
		DisplayPipeRequestDeliveryTimeLumaPrefetch[k] = DisplayPipeLineDeliveryTimeLumaPrefetch[k] / req_per_swath_ub;
		if (BytePerPixelC[k] == 0) {
			DisplayPipeRequestDeliveryTimeChroma[k] = 0;
			DisplayPipeRequestDeliveryTimeChromaPrefetch[k] = 0;
		} else {
			if (SourceScan[k] != dm_vert) {
				req_per_swath_ub = swath_width_chroma_ub[k] / BlockWidth256BytesC[k];
			} else {
				req_per_swath_ub = swath_width_chroma_ub[k] / BlockHeight256BytesC[k];
			}
			DisplayPipeRequestDeliveryTimeChroma[k] = DisplayPipeLineDeliveryTimeChroma[k] / req_per_swath_ub;
			DisplayPipeRequestDeliveryTimeChromaPrefetch[k] = DisplayPipeLineDeliveryTimeChromaPrefetch[k] / req_per_swath_ub;
		}
	}

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		int cursor_req_per_width = 0;
		cursor_req_per_width = dml_ceil(CursorWidth[k][0] * CursorBPP[k][0] / 256 / 8, 1);
		if (NumberOfCursors[k] > 0) {
			if (VRatio[k] <= 1) {
				CursorRequestDeliveryTime[k] = CursorWidth[k][0] / HRatio[k] / PixelClock[k] / cursor_req_per_width;
			} else {
				CursorRequestDeliveryTime[k] = CursorWidth[k][0] / PSCL_THROUGHPUT[k] / DPPCLK[k] / cursor_req_per_width;
			}
			if (VRatioPrefetchY[k] <= 1) {
				CursorRequestDeliveryTimePrefetch[k] = CursorWidth[k][0] / HRatio[k] / PixelClock[k] / cursor_req_per_width;
			} else {
				CursorRequestDeliveryTimePrefetch[k] = CursorWidth[k][0] / PSCL_THROUGHPUT[k] / DPPCLK[k] / cursor_req_per_width;
			}
		} else {
			CursorRequestDeliveryTime[k] = 0;
			CursorRequestDeliveryTimePrefetch[k] = 0;
		}
	}
}

static void CalculateMetaAndPTETimes(
		int NumberOfActivePlanes,
		bool GPUVMEnable,
		int MetaChunkSize,
		int MinMetaChunkSizeBytes,
		int HTotal[],
		double VRatio[],
		double VRatioChroma[],
		double DestinationLinesToRequestRowInVBlank[],
		double DestinationLinesToRequestRowInImmediateFlip[],
		bool DCCEnable[],
		double PixelClock[],
		int BytePerPixelY[],
		int BytePerPixelC[],
		enum scan_direction_class SourceScan[],
		int dpte_row_height[],
		int dpte_row_height_chroma[],
		int meta_row_width[],
		int meta_row_width_chroma[],
		int meta_row_height[],
		int meta_row_height_chroma[],
		int meta_req_width[],
		int meta_req_width_chroma[],
		int meta_req_height[],
		int meta_req_height_chroma[],
		int dpte_group_bytes[],
		int PTERequestSizeY[],
		int PTERequestSizeC[],
		int PixelPTEReqWidthY[],
		int PixelPTEReqHeightY[],
		int PixelPTEReqWidthC[],
		int PixelPTEReqHeightC[],
		int dpte_row_width_luma_ub[],
		int dpte_row_width_chroma_ub[],
		double DST_Y_PER_PTE_ROW_NOM_L[],
		double DST_Y_PER_PTE_ROW_NOM_C[],
		double DST_Y_PER_META_ROW_NOM_L[],
		double DST_Y_PER_META_ROW_NOM_C[],
		double TimePerMetaChunkNominal[],
		double TimePerChromaMetaChunkNominal[],
		double TimePerMetaChunkVBlank[],
		double TimePerChromaMetaChunkVBlank[],
		double TimePerMetaChunkFlip[],
		double TimePerChromaMetaChunkFlip[],
		double time_per_pte_group_nom_luma[],
		double time_per_pte_group_vblank_luma[],
		double time_per_pte_group_flip_luma[],
		double time_per_pte_group_nom_chroma[],
		double time_per_pte_group_vblank_chroma[],
		double time_per_pte_group_flip_chroma[])
{
	unsigned int meta_chunk_width = 0;
	unsigned int min_meta_chunk_width = 0;
	unsigned int meta_chunk_per_row_int = 0;
	unsigned int meta_row_remainder = 0;
	unsigned int meta_chunk_threshold = 0;
	unsigned int meta_chunks_per_row_ub = 0;
	unsigned int meta_chunk_width_chroma = 0;
	unsigned int min_meta_chunk_width_chroma = 0;
	unsigned int meta_chunk_per_row_int_chroma = 0;
	unsigned int meta_row_remainder_chroma = 0;
	unsigned int meta_chunk_threshold_chroma = 0;
	unsigned int meta_chunks_per_row_ub_chroma = 0;
	unsigned int dpte_group_width_luma = 0;
	unsigned int dpte_groups_per_row_luma_ub = 0;
	unsigned int dpte_group_width_chroma = 0;
	unsigned int dpte_groups_per_row_chroma_ub = 0;
	unsigned int k;

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		DST_Y_PER_PTE_ROW_NOM_L[k] = dpte_row_height[k] / VRatio[k];
		if (BytePerPixelC[k] == 0) {
			DST_Y_PER_PTE_ROW_NOM_C[k] = 0;
		} else {
			DST_Y_PER_PTE_ROW_NOM_C[k] = dpte_row_height_chroma[k] / VRatioChroma[k];
		}
		DST_Y_PER_META_ROW_NOM_L[k] = meta_row_height[k] / VRatio[k];
		if (BytePerPixelC[k] == 0) {
			DST_Y_PER_META_ROW_NOM_C[k] = 0;
		} else {
			DST_Y_PER_META_ROW_NOM_C[k] = meta_row_height_chroma[k] / VRatioChroma[k];
		}
	}

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (DCCEnable[k] == true) {
			meta_chunk_width = MetaChunkSize * 1024 * 256 / BytePerPixelY[k] / meta_row_height[k];
			min_meta_chunk_width = MinMetaChunkSizeBytes * 256 / BytePerPixelY[k] / meta_row_height[k];
			meta_chunk_per_row_int = meta_row_width[k] / meta_chunk_width;
			meta_row_remainder = meta_row_width[k] % meta_chunk_width;
			if (SourceScan[k] != dm_vert) {
				meta_chunk_threshold = 2 * min_meta_chunk_width - meta_req_width[k];
			} else {
				meta_chunk_threshold = 2 * min_meta_chunk_width - meta_req_height[k];
			}
			if (meta_row_remainder <= meta_chunk_threshold) {
				meta_chunks_per_row_ub = meta_chunk_per_row_int + 1;
			} else {
				meta_chunks_per_row_ub = meta_chunk_per_row_int + 2;
			}
			TimePerMetaChunkNominal[k] = meta_row_height[k] / VRatio[k] * HTotal[k] / PixelClock[k] / meta_chunks_per_row_ub;
			TimePerMetaChunkVBlank[k] = DestinationLinesToRequestRowInVBlank[k] * HTotal[k] / PixelClock[k] / meta_chunks_per_row_ub;
			TimePerMetaChunkFlip[k] = DestinationLinesToRequestRowInImmediateFlip[k] * HTotal[k] / PixelClock[k] / meta_chunks_per_row_ub;
			if (BytePerPixelC[k] == 0) {
				TimePerChromaMetaChunkNominal[k] = 0;
				TimePerChromaMetaChunkVBlank[k] = 0;
				TimePerChromaMetaChunkFlip[k] = 0;
			} else {
				meta_chunk_width_chroma = MetaChunkSize * 1024 * 256 / BytePerPixelC[k] / meta_row_height_chroma[k];
				min_meta_chunk_width_chroma = MinMetaChunkSizeBytes * 256 / BytePerPixelC[k] / meta_row_height_chroma[k];
				meta_chunk_per_row_int_chroma = (double) meta_row_width_chroma[k] / meta_chunk_width_chroma;
				meta_row_remainder_chroma = meta_row_width_chroma[k] % meta_chunk_width_chroma;
				if (SourceScan[k] != dm_vert) {
					meta_chunk_threshold_chroma = 2 * min_meta_chunk_width_chroma - meta_req_width_chroma[k];
				} else {
					meta_chunk_threshold_chroma = 2 * min_meta_chunk_width_chroma - meta_req_height_chroma[k];
				}
				if (meta_row_remainder_chroma <= meta_chunk_threshold_chroma) {
					meta_chunks_per_row_ub_chroma = meta_chunk_per_row_int_chroma + 1;
				} else {
					meta_chunks_per_row_ub_chroma = meta_chunk_per_row_int_chroma + 2;
				}
				TimePerChromaMetaChunkNominal[k] = meta_row_height_chroma[k] / VRatioChroma[k] * HTotal[k] / PixelClock[k] / meta_chunks_per_row_ub_chroma;
				TimePerChromaMetaChunkVBlank[k] = DestinationLinesToRequestRowInVBlank[k] * HTotal[k] / PixelClock[k] / meta_chunks_per_row_ub_chroma;
				TimePerChromaMetaChunkFlip[k] = DestinationLinesToRequestRowInImmediateFlip[k] * HTotal[k] / PixelClock[k] / meta_chunks_per_row_ub_chroma;
			}
		} else {
			TimePerMetaChunkNominal[k] = 0;
			TimePerMetaChunkVBlank[k] = 0;
			TimePerMetaChunkFlip[k] = 0;
			TimePerChromaMetaChunkNominal[k] = 0;
			TimePerChromaMetaChunkVBlank[k] = 0;
			TimePerChromaMetaChunkFlip[k] = 0;
		}
	}

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (GPUVMEnable == true) {
			if (SourceScan[k] != dm_vert) {
				dpte_group_width_luma = dpte_group_bytes[k] / PTERequestSizeY[k] * PixelPTEReqWidthY[k];
			} else {
				dpte_group_width_luma = dpte_group_bytes[k] / PTERequestSizeY[k] * PixelPTEReqHeightY[k];
			}
			dpte_groups_per_row_luma_ub = dml_ceil(1.0 * dpte_row_width_luma_ub[k] / dpte_group_width_luma, 1);
			time_per_pte_group_nom_luma[k] = DST_Y_PER_PTE_ROW_NOM_L[k] * HTotal[k] / PixelClock[k] / dpte_groups_per_row_luma_ub;
			time_per_pte_group_vblank_luma[k] = DestinationLinesToRequestRowInVBlank[k] * HTotal[k] / PixelClock[k] / dpte_groups_per_row_luma_ub;
			time_per_pte_group_flip_luma[k] = DestinationLinesToRequestRowInImmediateFlip[k] * HTotal[k] / PixelClock[k] / dpte_groups_per_row_luma_ub;
			if (BytePerPixelC[k] == 0) {
				time_per_pte_group_nom_chroma[k] = 0;
				time_per_pte_group_vblank_chroma[k] = 0;
				time_per_pte_group_flip_chroma[k] = 0;
			} else {
				if (SourceScan[k] != dm_vert) {
					dpte_group_width_chroma = dpte_group_bytes[k] / PTERequestSizeC[k] * PixelPTEReqWidthC[k];
				} else {
					dpte_group_width_chroma = dpte_group_bytes[k] / PTERequestSizeC[k] * PixelPTEReqHeightC[k];
				}
				dpte_groups_per_row_chroma_ub = dml_ceil(1.0 * dpte_row_width_chroma_ub[k] / dpte_group_width_chroma, 1);
				time_per_pte_group_nom_chroma[k] = DST_Y_PER_PTE_ROW_NOM_C[k] * HTotal[k] / PixelClock[k] / dpte_groups_per_row_chroma_ub;
				time_per_pte_group_vblank_chroma[k] = DestinationLinesToRequestRowInVBlank[k] * HTotal[k] / PixelClock[k] / dpte_groups_per_row_chroma_ub;
				time_per_pte_group_flip_chroma[k] = DestinationLinesToRequestRowInImmediateFlip[k] * HTotal[k] / PixelClock[k] / dpte_groups_per_row_chroma_ub;
			}
		} else {
			time_per_pte_group_nom_luma[k] = 0;
			time_per_pte_group_vblank_luma[k] = 0;
			time_per_pte_group_flip_luma[k] = 0;
			time_per_pte_group_nom_chroma[k] = 0;
			time_per_pte_group_vblank_chroma[k] = 0;
			time_per_pte_group_flip_chroma[k] = 0;
		}
	}
}

static void CalculateVMGroupAndRequestTimes(
		unsigned int NumberOfActivePlanes,
		bool GPUVMEnable,
		unsigned int GPUVMMaxPageTableLevels,
		unsigned int HTotal[],
		int BytePerPixelC[],
		double DestinationLinesToRequestVMInVBlank[],
		double DestinationLinesToRequestVMInImmediateFlip[],
		bool DCCEnable[],
		double PixelClock[],
		int dpte_row_width_luma_ub[],
		int dpte_row_width_chroma_ub[],
		int vm_group_bytes[],
		unsigned int dpde0_bytes_per_frame_ub_l[],
		unsigned int dpde0_bytes_per_frame_ub_c[],
		int meta_pte_bytes_per_frame_ub_l[],
		int meta_pte_bytes_per_frame_ub_c[],
		double TimePerVMGroupVBlank[],
		double TimePerVMGroupFlip[],
		double TimePerVMRequestVBlank[],
		double TimePerVMRequestFlip[])
{
	int num_group_per_lower_vm_stage = 0;
	int num_req_per_lower_vm_stage = 0;
	unsigned int k;

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (GPUVMEnable == true && (DCCEnable[k] == true || GPUVMMaxPageTableLevels > 1)) {
			if (DCCEnable[k] == false) {
				if (BytePerPixelC[k] > 0) {
					num_group_per_lower_vm_stage = dml_ceil((double) (dpde0_bytes_per_frame_ub_l[k])
						/ (double) (vm_group_bytes[k]), 1) + dml_ceil((double) (dpde0_bytes_per_frame_ub_c[k])
									/ (double) (vm_group_bytes[k]), 1);
				} else {
					num_group_per_lower_vm_stage = dml_ceil((double) (dpde0_bytes_per_frame_ub_l[k])
							/ (double) (vm_group_bytes[k]), 1);
				}
			} else {
				if (GPUVMMaxPageTableLevels == 1) {
					if (BytePerPixelC[k] > 0) {
						num_group_per_lower_vm_stage = dml_ceil((double) (meta_pte_bytes_per_frame_ub_l[k])
							/ (double) (vm_group_bytes[k]), 1) + dml_ceil((double) (meta_pte_bytes_per_frame_ub_c[k])
									/ (double) (vm_group_bytes[k]), 1);
					} else {
						num_group_per_lower_vm_stage = dml_ceil((double) (meta_pte_bytes_per_frame_ub_l[k])
							/ (double) (vm_group_bytes[k]), 1);
					}
				} else {
					if (BytePerPixelC[k] > 0) {
						num_group_per_lower_vm_stage = 2 + dml_ceil((double) (dpde0_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1)
								+ dml_ceil((double) (dpde0_bytes_per_frame_ub_c[k]) / (double) (vm_group_bytes[k]), 1)
								+ dml_ceil((double) (meta_pte_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1)
								+ dml_ceil((double) (meta_pte_bytes_per_frame_ub_c[k]) / (double) (vm_group_bytes[k]), 1);
					} else {
						num_group_per_lower_vm_stage = 1 + dml_ceil((double) (dpde0_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1)
								+ dml_ceil((double) (meta_pte_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1);
					}
				}
			}

			if (DCCEnable[k] == false) {
				if (BytePerPixelC[k] > 0) {
					num_req_per_lower_vm_stage = dpde0_bytes_per_frame_ub_l[k] / 64 + dpde0_bytes_per_frame_ub_c[k] / 64;
				} else {
					num_req_per_lower_vm_stage = dpde0_bytes_per_frame_ub_l[k] / 64;
				}
			} else {
				if (GPUVMMaxPageTableLevels == 1) {
					if (BytePerPixelC[k] > 0) {
						num_req_per_lower_vm_stage = meta_pte_bytes_per_frame_ub_l[k] / 64
								+ meta_pte_bytes_per_frame_ub_c[k] / 64;
					} else {
						num_req_per_lower_vm_stage = meta_pte_bytes_per_frame_ub_l[k] / 64;
					}
				} else {
					if (BytePerPixelC[k] > 0) {
						num_req_per_lower_vm_stage = dpde0_bytes_per_frame_ub_l[k] / 64
							+ dpde0_bytes_per_frame_ub_c[k] / 64 + meta_pte_bytes_per_frame_ub_l[k]
									/ 64 + meta_pte_bytes_per_frame_ub_c[k] / 64;
					} else {
						num_req_per_lower_vm_stage = dpde0_bytes_per_frame_ub_l[k] / 64
								+ meta_pte_bytes_per_frame_ub_l[k] / 64;
					}
				}
			}

			TimePerVMGroupVBlank[k] = DestinationLinesToRequestVMInVBlank[k] * HTotal[k] / PixelClock[k]
					/ num_group_per_lower_vm_stage;
			TimePerVMGroupFlip[k] = DestinationLinesToRequestVMInImmediateFlip[k] * HTotal[k] / PixelClock[k]
					/ num_group_per_lower_vm_stage;
			TimePerVMRequestVBlank[k] = DestinationLinesToRequestVMInVBlank[k] * HTotal[k] / PixelClock[k]
					/ num_req_per_lower_vm_stage;
			TimePerVMRequestFlip[k] = DestinationLinesToRequestVMInImmediateFlip[k] * HTotal[k] / PixelClock[k]
					/ num_req_per_lower_vm_stage;

			if (GPUVMMaxPageTableLevels > 2) {
				TimePerVMGroupVBlank[k] = TimePerVMGroupVBlank[k] / 2;
				TimePerVMGroupFlip[k] = TimePerVMGroupFlip[k] / 2;
				TimePerVMRequestVBlank[k] = TimePerVMRequestVBlank[k] / 2;
				TimePerVMRequestFlip[k] = TimePerVMRequestFlip[k] / 2;
			}

		} else {
			TimePerVMGroupVBlank[k] = 0;
			TimePerVMGroupFlip[k] = 0;
			TimePerVMRequestVBlank[k] = 0;
			TimePerVMRequestFlip[k] = 0;
		}
	}
}

static void CalculateStutterEfficiency(
		int NumberOfActivePlanes,
		long ROBBufferSizeInKByte,
		double TotalDataReadBandwidth,
		double DCFCLK,
		double ReturnBW,
		double SRExitTime,
		bool SynchronizedVBlank,
		int DPPPerPlane[],
		unsigned int DETBufferSizeY[],
		int BytePerPixelY[],
		double BytePerPixelDETY[],
		double SwathWidthY[],
		int SwathHeightY[],
		int SwathHeightC[],
		double DCCRateLuma[],
		double DCCRateChroma[],
		int HTotal[],
		int VTotal[],
		double PixelClock[],
		double VRatio[],
		enum scan_direction_class SourceScan[],
		int BlockHeight256BytesY[],
		int BlockWidth256BytesY[],
		int BlockHeight256BytesC[],
		int BlockWidth256BytesC[],
		int DCCYMaxUncompressedBlock[],
		int DCCCMaxUncompressedBlock[],
		int VActive[],
		bool DCCEnable[],
		bool WritebackEnable[],
		double ReadBandwidthPlaneLuma[],
		double ReadBandwidthPlaneChroma[],
		double meta_row_bw[],
		double dpte_row_bw[],
		double *StutterEfficiencyNotIncludingVBlank,
		double *StutterEfficiency,
		double *StutterPeriodOut)
{
	double FullDETBufferingTimeY[DC__NUM_DPP__MAX] = { 0 };
	double FrameTimeForMinFullDETBufferingTime = 0;
	double StutterPeriod = 0;
	double AverageReadBandwidth = 0;
	double TotalRowReadBandwidth = 0;
	double AverageDCCCompressionRate = 0;
	double PartOfBurstThatFitsInROB = 0;
	double StutterBurstTime = 0;
	int TotalActiveWriteback = 0;
	double VBlankTime = 0;
	double SmallestVBlank = 0;
	int BytePerPixelYCriticalPlane = 0;
	double SwathWidthYCriticalPlane = 0;
	double LinesInDETY[DC__NUM_DPP__MAX] = { 0 };
	double LinesInDETYRoundedDownToSwath[DC__NUM_DPP__MAX] = { 0 };
	double LinesToFinishSwathTransferStutterCriticalPlane = 0;
	double MaximumEffectiveCompressionLuma = 0;
	double    MaximumEffectiveCompressionChroma = 0;
	unsigned int k;

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		LinesInDETY[k] = DETBufferSizeY[k] / BytePerPixelDETY[k] / SwathWidthY[k];
		LinesInDETYRoundedDownToSwath[k] = dml_floor(LinesInDETY[k], SwathHeightY[k]);
		FullDETBufferingTimeY[k] = LinesInDETYRoundedDownToSwath[k] * (HTotal[k] / PixelClock[k]) / VRatio[k];
	}

	StutterPeriod = FullDETBufferingTimeY[0];
	FrameTimeForMinFullDETBufferingTime = VTotal[0] * HTotal[0] / PixelClock[0];
	BytePerPixelYCriticalPlane = BytePerPixelY[0];
	SwathWidthYCriticalPlane = SwathWidthY[0];
	LinesToFinishSwathTransferStutterCriticalPlane = SwathHeightY[0]
			- (LinesInDETY[0] - LinesInDETYRoundedDownToSwath[0]);

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (FullDETBufferingTimeY[k] < StutterPeriod) {
			StutterPeriod = FullDETBufferingTimeY[k];
			FrameTimeForMinFullDETBufferingTime = VTotal[k] * HTotal[k] / PixelClock[k];
			BytePerPixelYCriticalPlane = BytePerPixelY[k];
			SwathWidthYCriticalPlane = SwathWidthY[k];
			LinesToFinishSwathTransferStutterCriticalPlane = SwathHeightY[k]
					- (LinesInDETY[k] - LinesInDETYRoundedDownToSwath[k]);
		}
	}

	AverageReadBandwidth = 0;
	TotalRowReadBandwidth = 0;
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (DCCEnable[k] == true) {
			if ((SourceScan[k] == dm_vert && BlockWidth256BytesY[k] > SwathHeightY[k])
					|| (SourceScan[k] != dm_vert
							&& BlockHeight256BytesY[k] > SwathHeightY[k])
					|| DCCYMaxUncompressedBlock[k] < 256) {
				MaximumEffectiveCompressionLuma = 2;
			} else {
				MaximumEffectiveCompressionLuma = 4;
			}
			AverageReadBandwidth = AverageReadBandwidth + ReadBandwidthPlaneLuma[k] / dml_min(DCCRateLuma[k], MaximumEffectiveCompressionLuma);

			if (ReadBandwidthPlaneChroma[k] > 0) {
				if ((SourceScan[k] == dm_vert && BlockWidth256BytesC[k] > SwathHeightC[k])
						|| (SourceScan[k] != dm_vert && BlockHeight256BytesC[k] > SwathHeightC[k])
						|| DCCCMaxUncompressedBlock[k] < 256) {
					MaximumEffectiveCompressionChroma = 2;
				} else {
					MaximumEffectiveCompressionChroma = 4;
				}
				AverageReadBandwidth = AverageReadBandwidth + ReadBandwidthPlaneChroma[k] / dml_min(DCCRateChroma[k], MaximumEffectiveCompressionChroma);
			}
		} else {
			AverageReadBandwidth = AverageReadBandwidth + ReadBandwidthPlaneLuma[k] + ReadBandwidthPlaneChroma[k];
		}
		TotalRowReadBandwidth = TotalRowReadBandwidth + DPPPerPlane[k] * (meta_row_bw[k] + dpte_row_bw[k]);
	}

	AverageDCCCompressionRate = TotalDataReadBandwidth / AverageReadBandwidth;
	PartOfBurstThatFitsInROB = dml_min(StutterPeriod * TotalDataReadBandwidth, ROBBufferSizeInKByte * 1024 * AverageDCCCompressionRate);
	StutterBurstTime = PartOfBurstThatFitsInROB / AverageDCCCompressionRate / ReturnBW + (StutterPeriod * TotalDataReadBandwidth
			- PartOfBurstThatFitsInROB) / (DCFCLK * 64) + StutterPeriod * TotalRowReadBandwidth / ReturnBW;
	StutterBurstTime = dml_max(StutterBurstTime, LinesToFinishSwathTransferStutterCriticalPlane * BytePerPixelYCriticalPlane * SwathWidthYCriticalPlane / ReturnBW);

	TotalActiveWriteback = 0;
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (WritebackEnable[k] == true) {
			TotalActiveWriteback = TotalActiveWriteback + 1;
		}
	}

	if (TotalActiveWriteback == 0) {
		*StutterEfficiencyNotIncludingVBlank = (1
				- (SRExitTime + StutterBurstTime) / StutterPeriod) * 100;
	} else {
		*StutterEfficiencyNotIncludingVBlank = 0;
	}

	if (SynchronizedVBlank == true || NumberOfActivePlanes == 1) {
		SmallestVBlank = (VTotal[0] - VActive[0]) * HTotal[0] / PixelClock[0];
	} else {
		SmallestVBlank = 0;
	}
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (SynchronizedVBlank == true || NumberOfActivePlanes == 1) {
			VBlankTime = (VTotal[k] - VActive[k]) * HTotal[k] / PixelClock[k];
		} else {
			VBlankTime = 0;
		}
		SmallestVBlank = dml_min(SmallestVBlank, VBlankTime);
	}

	*StutterEfficiency =  (*StutterEfficiencyNotIncludingVBlank / 100.0 * (FrameTimeForMinFullDETBufferingTime - SmallestVBlank) + SmallestVBlank) / FrameTimeForMinFullDETBufferingTime * 100;

	if (StutterPeriodOut)
		*StutterPeriodOut = StutterPeriod;
}

static void CalculateSwathAndDETConfiguration(
		bool ForceSingleDPP,
		int NumberOfActivePlanes,
		unsigned int DETBufferSizeInKByte,
		double MaximumSwathWidthLuma[],
		double MaximumSwathWidthChroma[],
		enum scan_direction_class SourceScan[],
		enum source_format_class SourcePixelFormat[],
		enum dm_swizzle_mode SurfaceTiling[],
		int ViewportWidth[],
		int ViewportHeight[],
		int SurfaceWidthY[],
		int SurfaceWidthC[],
		int SurfaceHeightY[],
		int SurfaceHeightC[],
		int Read256BytesBlockHeightY[],
		int Read256BytesBlockHeightC[],
		int Read256BytesBlockWidthY[],
		int Read256BytesBlockWidthC[],
		enum odm_combine_mode ODMCombineEnabled[],
		int BlendingAndTiming[],
		int BytePerPixY[],
		int BytePerPixC[],
		double BytePerPixDETY[],
		double BytePerPixDETC[],
		int HActive[],
		double HRatio[],
		double HRatioChroma[],
		int DPPPerPlane[],
		int swath_width_luma_ub[],
		int swath_width_chroma_ub[],
		double SwathWidth[],
		double SwathWidthChroma[],
		int SwathHeightY[],
		int SwathHeightC[],
		unsigned int DETBufferSizeY[],
		unsigned int DETBufferSizeC[],
		bool ViewportSizeSupportPerPlane[],
		bool *ViewportSizeSupport)
{
	int MaximumSwathHeightY[DC__NUM_DPP__MAX] = { 0 };
	int MaximumSwathHeightC[DC__NUM_DPP__MAX] = { 0 };
	int MinimumSwathHeightY = 0;
	int MinimumSwathHeightC = 0;
	long RoundedUpMaxSwathSizeBytesY = 0;
	long RoundedUpMaxSwathSizeBytesC = 0;
	long RoundedUpMinSwathSizeBytesY = 0;
	long RoundedUpMinSwathSizeBytesC = 0;
	long RoundedUpSwathSizeBytesY = 0;
	long RoundedUpSwathSizeBytesC = 0;
	double SwathWidthSingleDPP[DC__NUM_DPP__MAX] = { 0 };
	double SwathWidthSingleDPPChroma[DC__NUM_DPP__MAX] = { 0 };
	int k;

	CalculateSwathWidth(
			ForceSingleDPP,
			NumberOfActivePlanes,
			SourcePixelFormat,
			SourceScan,
			ViewportWidth,
			ViewportHeight,
			SurfaceWidthY,
			SurfaceWidthC,
			SurfaceHeightY,
			SurfaceHeightC,
			ODMCombineEnabled,
			BytePerPixY,
			BytePerPixC,
			Read256BytesBlockHeightY,
			Read256BytesBlockHeightC,
			Read256BytesBlockWidthY,
			Read256BytesBlockWidthC,
			BlendingAndTiming,
			HActive,
			HRatio,
			DPPPerPlane,
			SwathWidthSingleDPP,
			SwathWidthSingleDPPChroma,
			SwathWidth,
			SwathWidthChroma,
			MaximumSwathHeightY,
			MaximumSwathHeightC,
			swath_width_luma_ub,
			swath_width_chroma_ub);

	*ViewportSizeSupport = true;
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if ((SourcePixelFormat[k] == dm_444_64 || SourcePixelFormat[k] == dm_444_32
				|| SourcePixelFormat[k] == dm_444_16
				|| SourcePixelFormat[k] == dm_mono_16
				|| SourcePixelFormat[k] == dm_mono_8
				|| SourcePixelFormat[k] == dm_rgbe)) {
			if (SurfaceTiling[k] == dm_sw_linear
				|| (SourcePixelFormat[k] == dm_444_64
					&& (SurfaceTiling[k] == dm_sw_64kb_s || SurfaceTiling[k] == dm_sw_64kb_s_t || SurfaceTiling[k] == dm_sw_64kb_s_x)
					&& SourceScan[k] != dm_vert)) {
				MinimumSwathHeightY = MaximumSwathHeightY[k];
			} else if (SourcePixelFormat[k] == dm_444_8 && SourceScan[k] == dm_vert) {
				MinimumSwathHeightY = MaximumSwathHeightY[k];
			} else {
				MinimumSwathHeightY = MaximumSwathHeightY[k] / 2;
			}
			MinimumSwathHeightC = MaximumSwathHeightC[k];
		} else {
			if (SurfaceTiling[k] == dm_sw_linear) {
				MinimumSwathHeightY = MaximumSwathHeightY[k];
				MinimumSwathHeightC = MaximumSwathHeightC[k];
			} else if (SourcePixelFormat[k] == dm_rgbe_alpha
					&& SourceScan[k] == dm_vert) {
				MinimumSwathHeightY = MaximumSwathHeightY[k] / 2;
				MinimumSwathHeightC = MaximumSwathHeightC[k];
			} else if (SourcePixelFormat[k] == dm_rgbe_alpha) {
				MinimumSwathHeightY = MaximumSwathHeightY[k] / 2;
				MinimumSwathHeightC = MaximumSwathHeightC[k] / 2;
			} else if (SourcePixelFormat[k] == dm_420_8 && SourceScan[k] == dm_vert) {
				MinimumSwathHeightY = MaximumSwathHeightY[k];
				MinimumSwathHeightC = MaximumSwathHeightC[k] / 2;
			} else {
				MinimumSwathHeightC = MaximumSwathHeightC[k] / 2;
				MinimumSwathHeightY = MaximumSwathHeightY[k] / 2;
			}
		}

		RoundedUpMaxSwathSizeBytesY = swath_width_luma_ub[k] * BytePerPixDETY[k]
				* MaximumSwathHeightY[k];
		RoundedUpMinSwathSizeBytesY = swath_width_luma_ub[k] * BytePerPixDETY[k]
				* MinimumSwathHeightY;
		if (SourcePixelFormat[k] == dm_420_10) {
			RoundedUpMaxSwathSizeBytesY = dml_ceil((double) RoundedUpMaxSwathSizeBytesY, 256);
			RoundedUpMinSwathSizeBytesY = dml_ceil((double) RoundedUpMinSwathSizeBytesY, 256);
		}
		RoundedUpMaxSwathSizeBytesC = swath_width_chroma_ub[k] * BytePerPixDETC[k]
				* MaximumSwathHeightC[k];
		RoundedUpMinSwathSizeBytesC = swath_width_chroma_ub[k] * BytePerPixDETC[k]
				* MinimumSwathHeightC;
		if (SourcePixelFormat[k] == dm_420_10) {
			RoundedUpMaxSwathSizeBytesC = dml_ceil(RoundedUpMaxSwathSizeBytesC, 256);
			RoundedUpMinSwathSizeBytesC = dml_ceil(RoundedUpMinSwathSizeBytesC, 256);
		}

		if (RoundedUpMaxSwathSizeBytesY + RoundedUpMaxSwathSizeBytesC
				<= DETBufferSizeInKByte * 1024 / 2) {
			SwathHeightY[k] = MaximumSwathHeightY[k];
			SwathHeightC[k] = MaximumSwathHeightC[k];
			RoundedUpSwathSizeBytesY = RoundedUpMaxSwathSizeBytesY;
			RoundedUpSwathSizeBytesC = RoundedUpMaxSwathSizeBytesC;
		} else if (RoundedUpMaxSwathSizeBytesY >= 1.5 * RoundedUpMaxSwathSizeBytesC
				&& RoundedUpMinSwathSizeBytesY + RoundedUpMaxSwathSizeBytesC
						<= DETBufferSizeInKByte * 1024 / 2) {
			SwathHeightY[k] = MinimumSwathHeightY;
			SwathHeightC[k] = MaximumSwathHeightC[k];
			RoundedUpSwathSizeBytesY = RoundedUpMinSwathSizeBytesY;
			RoundedUpSwathSizeBytesC = RoundedUpMaxSwathSizeBytesC;
		} else if (RoundedUpMaxSwathSizeBytesY < 1.5 * RoundedUpMaxSwathSizeBytesC
				&& RoundedUpMaxSwathSizeBytesY + RoundedUpMinSwathSizeBytesC
						<= DETBufferSizeInKByte * 1024 / 2) {
			SwathHeightY[k] = MaximumSwathHeightY[k];
			SwathHeightC[k] = MinimumSwathHeightC;
			RoundedUpSwathSizeBytesY = RoundedUpMaxSwathSizeBytesY;
			RoundedUpSwathSizeBytesC = RoundedUpMinSwathSizeBytesC;
		} else {
			SwathHeightY[k] = MinimumSwathHeightY;
			SwathHeightC[k] = MinimumSwathHeightC;
			RoundedUpSwathSizeBytesY = RoundedUpMinSwathSizeBytesY;
			RoundedUpSwathSizeBytesC = RoundedUpMinSwathSizeBytesC;
		}

		if (SwathHeightC[k] == 0) {
			DETBufferSizeY[k] = DETBufferSizeInKByte * 1024;
			DETBufferSizeC[k] = 0;
		} else if (RoundedUpSwathSizeBytesY <= 1.5 * RoundedUpSwathSizeBytesC) {
			DETBufferSizeY[k] = DETBufferSizeInKByte * 1024 / 2;
			DETBufferSizeC[k] = DETBufferSizeInKByte * 1024 / 2;
		} else {
			DETBufferSizeY[k] = DETBufferSizeInKByte * 1024 * 2 / 3;
			DETBufferSizeC[k] = DETBufferSizeInKByte * 1024 / 3;
		}

		if (RoundedUpMinSwathSizeBytesY + RoundedUpMinSwathSizeBytesC
				> DETBufferSizeInKByte * 1024 / 2
				|| SwathWidth[k] > MaximumSwathWidthLuma[k]
				|| (SwathHeightC[k] > 0
						&& SwathWidthChroma[k] > MaximumSwathWidthChroma[k])) {
			*ViewportSizeSupport = false;
			ViewportSizeSupportPerPlane[k] = false;
		} else {
			ViewportSizeSupportPerPlane[k] = true;
		}
	}
}

static void CalculateSwathWidth(
		bool ForceSingleDPP,
		int NumberOfActivePlanes,
		enum source_format_class SourcePixelFormat[],
		enum scan_direction_class SourceScan[],
		unsigned int ViewportWidth[],
		unsigned int ViewportHeight[],
		unsigned int SurfaceWidthY[],
		unsigned int SurfaceWidthC[],
		unsigned int SurfaceHeightY[],
		unsigned int SurfaceHeightC[],
		enum odm_combine_mode ODMCombineEnabled[],
		int BytePerPixY[],
		int BytePerPixC[],
		int Read256BytesBlockHeightY[],
		int Read256BytesBlockHeightC[],
		int Read256BytesBlockWidthY[],
		int Read256BytesBlockWidthC[],
		int BlendingAndTiming[],
		unsigned int HActive[],
		double HRatio[],
		int DPPPerPlane[],
		double SwathWidthSingleDPPY[],
		double SwathWidthSingleDPPC[],
		double SwathWidthY[],
		double SwathWidthC[],
		int MaximumSwathHeightY[],
		int MaximumSwathHeightC[],
		unsigned int swath_width_luma_ub[],
		unsigned int swath_width_chroma_ub[])
{
	unsigned int k, j;
	long surface_width_ub_l;
	long surface_height_ub_l;
	long surface_width_ub_c;
	long surface_height_ub_c;

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		enum odm_combine_mode MainPlaneODMCombine = 0;

		if (SourceScan[k] != dm_vert) {
			SwathWidthSingleDPPY[k] = ViewportWidth[k];
		} else {
			SwathWidthSingleDPPY[k] = ViewportHeight[k];
		}

		MainPlaneODMCombine = ODMCombineEnabled[k];
		for (j = 0; j < NumberOfActivePlanes; ++j) {
			if (BlendingAndTiming[k] == j) {
				MainPlaneODMCombine = ODMCombineEnabled[j];
			}
		}

		if (MainPlaneODMCombine == dm_odm_combine_mode_4to1) {
			SwathWidthY[k] = dml_min(SwathWidthSingleDPPY[k], dml_round(HActive[k] / 4.0 * HRatio[k]));
		} else if (MainPlaneODMCombine == dm_odm_combine_mode_2to1) {
			SwathWidthY[k] = dml_min(SwathWidthSingleDPPY[k], dml_round(HActive[k] / 2.0 * HRatio[k]));
		} else if (DPPPerPlane[k] == 2) {
			SwathWidthY[k] = SwathWidthSingleDPPY[k] / 2;
		} else {
			SwathWidthY[k] = SwathWidthSingleDPPY[k];
		}

		if (SourcePixelFormat[k] == dm_420_8 || SourcePixelFormat[k] == dm_420_10 || SourcePixelFormat[k] == dm_420_12) {
			SwathWidthC[k] = SwathWidthY[k] / 2;
			SwathWidthSingleDPPC[k] = SwathWidthSingleDPPY[k] / 2;
		} else {
			SwathWidthC[k] = SwathWidthY[k];
			SwathWidthSingleDPPC[k] = SwathWidthSingleDPPY[k];
		}

		if (ForceSingleDPP == true) {
			SwathWidthY[k] = SwathWidthSingleDPPY[k];
			SwathWidthC[k] = SwathWidthSingleDPPC[k];
		}

		surface_width_ub_l  = dml_ceil(SurfaceWidthY[k], Read256BytesBlockWidthY[k]);
		surface_height_ub_l = dml_ceil(SurfaceHeightY[k], Read256BytesBlockHeightY[k]);

		if (SourceScan[k] != dm_vert) {
			MaximumSwathHeightY[k] = Read256BytesBlockHeightY[k];
			MaximumSwathHeightC[k] = Read256BytesBlockHeightC[k];
			swath_width_luma_ub[k] = dml_min(surface_width_ub_l, (long) dml_ceil(SwathWidthY[k] - 1,
					Read256BytesBlockWidthY[k]) + Read256BytesBlockWidthY[k]);
			if (BytePerPixC[k] > 0) {
				surface_width_ub_c  = dml_ceil(SurfaceWidthC[k], Read256BytesBlockWidthC[k]);
				swath_width_chroma_ub[k] = dml_min(surface_width_ub_c, (long) dml_ceil(SwathWidthC[k] - 1,
						Read256BytesBlockWidthC[k]) + Read256BytesBlockWidthC[k]);
			} else {
				swath_width_chroma_ub[k] = 0;
			}
		} else {
			MaximumSwathHeightY[k] = Read256BytesBlockWidthY[k];
			MaximumSwathHeightC[k] = Read256BytesBlockWidthC[k];
			swath_width_luma_ub[k] = dml_min(surface_height_ub_l, (long) dml_ceil(SwathWidthY[k] - 1,
					Read256BytesBlockHeightY[k]) + Read256BytesBlockHeightY[k]);
			if (BytePerPixC[k] > 0) {
				surface_height_ub_c = dml_ceil(SurfaceHeightC[k], Read256BytesBlockHeightC[k]);
				swath_width_chroma_ub[k] = dml_min(surface_height_ub_c, (long) dml_ceil(SwathWidthC[k] - 1,
						Read256BytesBlockHeightC[k]) + Read256BytesBlockHeightC[k]);
			} else {
				swath_width_chroma_ub[k] = 0;
			}
		}
	}
}

static double CalculateExtraLatency(
		long RoundTripPingLatencyCycles,
		long ReorderingBytes,
		double DCFCLK,
		int TotalNumberOfActiveDPP,
		int PixelChunkSizeInKByte,
		int TotalNumberOfDCCActiveDPP,
		int MetaChunkSize,
		double ReturnBW,
		bool GPUVMEnable,
		bool HostVMEnable,
		int NumberOfActivePlanes,
		int NumberOfDPP[],
		int dpte_group_bytes[],
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
		double HostVMMinPageSize,
		int HostVMMaxNonCachedPageTableLevels)
{
	double ExtraLatencyBytes = 0;
	ExtraLatencyBytes = CalculateExtraLatencyBytes(
					ReorderingBytes,
					TotalNumberOfActiveDPP,
					PixelChunkSizeInKByte,
					TotalNumberOfDCCActiveDPP,
					MetaChunkSize,
					GPUVMEnable,
					HostVMEnable,
					NumberOfActivePlanes,
					NumberOfDPP,
					dpte_group_bytes,
					PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
					PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
					HostVMMinPageSize,
					HostVMMaxNonCachedPageTableLevels);

	return (RoundTripPingLatencyCycles + 32) / DCFCLK + ExtraLatencyBytes / ReturnBW;
}

static double CalculateExtraLatencyBytes(
		long ReorderingBytes,
		int TotalNumberOfActiveDPP,
		int PixelChunkSizeInKByte,
		int TotalNumberOfDCCActiveDPP,
		int MetaChunkSize,
		bool GPUVMEnable,
		bool HostVMEnable,
		int NumberOfActivePlanes,
		int NumberOfDPP[],
		int dpte_group_bytes[],
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
		double HostVMMinPageSize,
		int HostVMMaxNonCachedPageTableLevels)
{
	double ret = 0;
	double HostVMInefficiencyFactor = 0;
	int HostVMDynamicLevels = 0;
	unsigned int k;

	if (GPUVMEnable == true && HostVMEnable == true) {
		HostVMInefficiencyFactor = PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData / PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly;
		if (HostVMMinPageSize < 2048) {
			HostVMDynamicLevels = HostVMMaxNonCachedPageTableLevels;
		} else if (HostVMMinPageSize >= 2048 && HostVMMinPageSize < 1048576) {
			HostVMDynamicLevels = dml_max(0, (int) HostVMMaxNonCachedPageTableLevels - 1);
		} else {
			HostVMDynamicLevels = dml_max(0, (int) HostVMMaxNonCachedPageTableLevels - 2);
		}
	} else {
		HostVMInefficiencyFactor = 1;
		HostVMDynamicLevels = 0;
	}

	ret = ReorderingBytes + (TotalNumberOfActiveDPP * PixelChunkSizeInKByte + TotalNumberOfDCCActiveDPP * MetaChunkSize) * 1024.0;

	if (GPUVMEnable == true) {
		for (k = 0; k < NumberOfActivePlanes; ++k) {
			ret = ret + NumberOfDPP[k] * dpte_group_bytes[k] * (1 + 8 * HostVMDynamicLevels) * HostVMInefficiencyFactor;
		}
	}
	return ret;
}


static double CalculateUrgentLatency(
		double UrgentLatencyPixelDataOnly,
		double UrgentLatencyPixelMixedWithVMData,
		double UrgentLatencyVMDataOnly,
		bool DoUrgentLatencyAdjustment,
		double UrgentLatencyAdjustmentFabricClockComponent,
		double UrgentLatencyAdjustmentFabricClockReference,
		double FabricClock)
{
	double ret;

	ret = dml_max3(UrgentLatencyPixelDataOnly, UrgentLatencyPixelMixedWithVMData, UrgentLatencyVMDataOnly);
	if (DoUrgentLatencyAdjustment == true) {
		ret = ret + UrgentLatencyAdjustmentFabricClockComponent * (UrgentLatencyAdjustmentFabricClockReference / FabricClock - 1);
	}
	return ret;
}

static noinline_for_stack void UseMinimumDCFCLK(
		struct display_mode_lib *mode_lib,
		struct vba_vars_st *v,
		int MaxPrefetchMode,
		int ReorderingBytes)
{
	double   NormalEfficiency = 0;
	double   PTEEfficiency = 0;
	double   TotalMaxPrefetchFlipDPTERowBandwidth[DC__VOLTAGE_STATES][2] = { { 0 } };
	unsigned int i, j, k;

	NormalEfficiency =  (v->HostVMEnable == true ? v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData
			: v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelDataOnly) / 100.0;
	PTEEfficiency =  (v->HostVMEnable == true ? v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly
			/ v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData : 1.0);
	for (i = 0; i < mode_lib->soc.num_states; ++i) {
		for (j = 0; j <= 1; ++j) {
			double PixelDCFCLKCyclesRequiredInPrefetch[DC__NUM_DPP__MAX] = { 0 };
			double PrefetchPixelLinesTime[DC__NUM_DPP__MAX] = { 0 };
			double DCFCLKRequiredForPeakBandwidthPerPlane[DC__NUM_DPP__MAX] = { 0 };
			double DynamicMetadataVMExtraLatency[DC__NUM_DPP__MAX] = { 0 };
			double MinimumTWait = 0;
			double NonDPTEBandwidth = 0;
			double DPTEBandwidth = 0;
			double DCFCLKRequiredForAverageBandwidth = 0;
			double ExtraLatencyBytes = 0;
			double ExtraLatencyCycles = 0;
			double DCFCLKRequiredForPeakBandwidth = 0;
			int NoOfDPPState[DC__NUM_DPP__MAX] = { 0 };
			double MinimumTvmPlus2Tr0 = 0;

			TotalMaxPrefetchFlipDPTERowBandwidth[i][j] = 0;
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				TotalMaxPrefetchFlipDPTERowBandwidth[i][j] = TotalMaxPrefetchFlipDPTERowBandwidth[i][j]
					+ v->NoOfDPP[i][j][k] * v->DPTEBytesPerRow[i][j][k] / (15.75 * v->HTotal[k] / v->PixelClock[k]);
			}

			for (k = 0; k <= v->NumberOfActivePlanes - 1; ++k) {
				NoOfDPPState[k] = v->NoOfDPP[i][j][k];
			}

			MinimumTWait = CalculateTWait(MaxPrefetchMode, v->FinalDRAMClockChangeLatency, v->UrgLatency[i], v->SREnterPlusExitTime);
			NonDPTEBandwidth = v->TotalVActivePixelBandwidth[i][j] + v->TotalVActiveCursorBandwidth[i][j] + v->TotalMetaRowBandwidth[i][j];
			DPTEBandwidth =  (v->HostVMEnable == true || v->ImmediateFlipRequirement[0] == dm_immediate_flip_required) ?
					TotalMaxPrefetchFlipDPTERowBandwidth[i][j] : v->TotalDPTERowBandwidth[i][j];
			DCFCLKRequiredForAverageBandwidth = dml_max3(v->ProjectedDCFCLKDeepSleep[i][j],
					(NonDPTEBandwidth + v->TotalDPTERowBandwidth[i][j]) / v->ReturnBusWidth / (v->MaxAveragePercentOfIdealSDPPortBWDisplayCanUseInNormalSystemOperation / 100),
					(NonDPTEBandwidth + DPTEBandwidth / PTEEfficiency) / NormalEfficiency / v->ReturnBusWidth);

			ExtraLatencyBytes = CalculateExtraLatencyBytes(ReorderingBytes, v->TotalNumberOfActiveDPP[i][j], v->PixelChunkSizeInKByte, v->TotalNumberOfDCCActiveDPP[i][j],
					v->MetaChunkSize, v->GPUVMEnable, v->HostVMEnable, v->NumberOfActivePlanes, NoOfDPPState, v->dpte_group_bytes,
					v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData, v->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
					v->HostVMMinPageSize, v->HostVMMaxNonCachedPageTableLevels);
			ExtraLatencyCycles = v->RoundTripPingLatencyCycles + 32 + ExtraLatencyBytes / NormalEfficiency / v->ReturnBusWidth;
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				double DCFCLKCyclesRequiredInPrefetch = { 0 };
				double ExpectedPrefetchBWAcceleration = { 0 };
				double PrefetchTime = { 0 };

				PixelDCFCLKCyclesRequiredInPrefetch[k] = (v->PrefetchLinesY[i][j][k] * v->swath_width_luma_ub_all_states[i][j][k] * v->BytePerPixelY[k]
					+ v->PrefetchLinesC[i][j][k] * v->swath_width_chroma_ub_all_states[i][j][k] * v->BytePerPixelC[k]) / NormalEfficiency / v->ReturnBusWidth;
				DCFCLKCyclesRequiredInPrefetch = 2 * ExtraLatencyCycles / NoOfDPPState[k] + v->PDEAndMetaPTEBytesPerFrame[i][j][k] / PTEEfficiency
					/ NormalEfficiency / v->ReturnBusWidth *  (v->GPUVMMaxPageTableLevels > 2 ? 1 : 0) + 2 * v->DPTEBytesPerRow[i][j][k] / PTEEfficiency
					/ NormalEfficiency / v->ReturnBusWidth + 2 * v->MetaRowBytes[i][j][k] / NormalEfficiency / v->ReturnBusWidth + PixelDCFCLKCyclesRequiredInPrefetch[k];
				PrefetchPixelLinesTime[k] = dml_max(v->PrefetchLinesY[i][j][k], v->PrefetchLinesC[i][j][k]) * v->HTotal[k] / v->PixelClock[k];
				ExpectedPrefetchBWAcceleration = (v->VActivePixelBandwidth[i][j][k] + v->VActiveCursorBandwidth[i][j][k]) / (v->ReadBandwidthLuma[k] + v->ReadBandwidthChroma[k]);
				DynamicMetadataVMExtraLatency[k] = (v->GPUVMEnable == true && v->DynamicMetadataEnable[k] == true && v->DynamicMetadataVMEnabled == true) ?
						v->UrgLatency[i] * v->GPUVMMaxPageTableLevels *  (v->HostVMEnable == true ? v->HostVMMaxNonCachedPageTableLevels + 1 : 1) : 0;
				PrefetchTime = (v->MaximumVStartup[i][j][k] - 1) * v->HTotal[k] / v->PixelClock[k] - MinimumTWait - v->UrgLatency[i] * ((v->GPUVMMaxPageTableLevels <= 2 ? v->GPUVMMaxPageTableLevels
						: v->GPUVMMaxPageTableLevels - 2) * (v->HostVMEnable == true ? v->HostVMMaxNonCachedPageTableLevels + 1 : 1) - 1) - DynamicMetadataVMExtraLatency[k];

				if (PrefetchTime > 0) {
					double ExpectedVRatioPrefetch = { 0 };
					ExpectedVRatioPrefetch = PrefetchPixelLinesTime[k] / (PrefetchTime * PixelDCFCLKCyclesRequiredInPrefetch[k] / DCFCLKCyclesRequiredInPrefetch);
					DCFCLKRequiredForPeakBandwidthPerPlane[k] = NoOfDPPState[k] * PixelDCFCLKCyclesRequiredInPrefetch[k] / PrefetchPixelLinesTime[k]
						* dml_max(1.0, ExpectedVRatioPrefetch) * dml_max(1.0, ExpectedVRatioPrefetch / 4) * ExpectedPrefetchBWAcceleration;
					if (v->HostVMEnable == true || v->ImmediateFlipRequirement[0] == dm_immediate_flip_required) {
						DCFCLKRequiredForPeakBandwidthPerPlane[k] = DCFCLKRequiredForPeakBandwidthPerPlane[k]
							+ NoOfDPPState[k] * DPTEBandwidth / PTEEfficiency / NormalEfficiency / v->ReturnBusWidth;
					}
				} else {
					DCFCLKRequiredForPeakBandwidthPerPlane[k] = v->DCFCLKPerState[i];
				}
				if (v->DynamicMetadataEnable[k] == true) {
					double TsetupPipe = { 0 };
					double TdmbfPipe = { 0 };
					double TdmsksPipe = { 0 };
					double TdmecPipe = { 0 };
					double AllowedTimeForUrgentExtraLatency = { 0 };

					CalculateDynamicMetadataParameters(
							v->MaxInterDCNTileRepeaters,
							v->RequiredDPPCLK[i][j][k],
							v->RequiredDISPCLK[i][j],
							v->ProjectedDCFCLKDeepSleep[i][j],
							v->PixelClock[k],
							v->HTotal[k],
							v->VTotal[k] - v->VActive[k],
							v->DynamicMetadataTransmittedBytes[k],
							v->DynamicMetadataLinesBeforeActiveRequired[k],
							v->Interlace[k],
							v->ProgressiveToInterlaceUnitInOPP,
							&TsetupPipe,
							&TdmbfPipe,
							&TdmecPipe,
							&TdmsksPipe);
					AllowedTimeForUrgentExtraLatency = v->MaximumVStartup[i][j][k] * v->HTotal[k] / v->PixelClock[k] - MinimumTWait - TsetupPipe
							- TdmbfPipe - TdmecPipe - TdmsksPipe - DynamicMetadataVMExtraLatency[k];
					if (AllowedTimeForUrgentExtraLatency > 0) {
						DCFCLKRequiredForPeakBandwidthPerPlane[k] = dml_max(DCFCLKRequiredForPeakBandwidthPerPlane[k],
								ExtraLatencyCycles / AllowedTimeForUrgentExtraLatency);
					} else {
						DCFCLKRequiredForPeakBandwidthPerPlane[k] = v->DCFCLKPerState[i];
					}
				}
			}
			DCFCLKRequiredForPeakBandwidth = 0;
			for (k = 0; k <= v->NumberOfActivePlanes - 1; ++k) {
				DCFCLKRequiredForPeakBandwidth = DCFCLKRequiredForPeakBandwidth + DCFCLKRequiredForPeakBandwidthPerPlane[k];
			}
			MinimumTvmPlus2Tr0 = v->UrgLatency[i] * (v->GPUVMEnable == true ? (v->HostVMEnable == true ?
					(v->GPUVMMaxPageTableLevels + 2) * (v->HostVMMaxNonCachedPageTableLevels + 1) - 1 : v->GPUVMMaxPageTableLevels + 1) : 0);
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				double MaximumTvmPlus2Tr0PlusTsw = { 0 };
				MaximumTvmPlus2Tr0PlusTsw = (v->MaximumVStartup[i][j][k] - 2) * v->HTotal[k] / v->PixelClock[k] - MinimumTWait - DynamicMetadataVMExtraLatency[k];
				if (MaximumTvmPlus2Tr0PlusTsw <= MinimumTvmPlus2Tr0 + PrefetchPixelLinesTime[k] / 4) {
					DCFCLKRequiredForPeakBandwidth = v->DCFCLKPerState[i];
				} else {
					DCFCLKRequiredForPeakBandwidth = dml_max3(DCFCLKRequiredForPeakBandwidth, 2 * ExtraLatencyCycles
							/ (MaximumTvmPlus2Tr0PlusTsw - MinimumTvmPlus2Tr0 - PrefetchPixelLinesTime[k] / 4),
						(2 * ExtraLatencyCycles + PixelDCFCLKCyclesRequiredInPrefetch[k]) / (MaximumTvmPlus2Tr0PlusTsw - MinimumTvmPlus2Tr0));
				}
			}
			v->DCFCLKState[i][j] = dml_min(v->DCFCLKPerState[i], 1.05 * (1 + mode_lib->vba.PercentMarginOverMinimumRequiredDCFCLK / 100)
					* dml_max(DCFCLKRequiredForAverageBandwidth, DCFCLKRequiredForPeakBandwidth));
		}
	}
}

