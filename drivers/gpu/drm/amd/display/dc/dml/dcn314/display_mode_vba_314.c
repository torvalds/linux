// SPDX-License-Identifier: MIT
/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#define UNIT_TEST 0
#if !UNIT_TEST
#include "dc.h"
#include "dc_link.h"
#endif
#include "../display_mode_lib.h"
#include "display_mode_vba_314.h"
#include "../dml_inline_defs.h"

/*
 * NOTE:
 *   This file is gcc-parsable HW gospel, coming straight from HW engineers.
 *
 * It doesn't adhere to Linux kernel style and sometimes will do things in odd
 * ways. Unless there is something clearly wrong with it the code should
 * remain as-is as it provides us with a guarantee from HW that it is correct.
 */

#define BPP_INVALID 0
#define BPP_BLENDED_PIPE 0xffffffff
#define DCN314_MAX_DSC_IMAGE_WIDTH 5184
#define DCN314_MAX_FMT_420_BUFFER_WIDTH 4096

// For DML-C changes that hasn't been propagated to VBA yet
//#define __DML_VBA_ALLOW_DELTA__

// Move these to ip parameters/constant

// At which vstartup the DML start to try if the mode can be supported
#define __DML_VBA_MIN_VSTARTUP__    9

// Delay in DCFCLK from ARB to DET (1st num is ARB to SDPIF, 2nd number is SDPIF to DET)
#define __DML_ARB_TO_RET_DELAY__    (7 + 95)

// fudge factor for min dcfclk calclation
#define __DML_MIN_DCFCLK_FACTOR__   1.15

typedef struct {
	double DPPCLK;
	double DISPCLK;
	double PixelClock;
	double DCFCLKDeepSleep;
	unsigned int DPPPerPlane;
	bool ScalerEnabled;
	double VRatio;
	double VRatioChroma;
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
	bool ODMCombineIsEnabled;
	enum source_format_class SourcePixelFormat;
	int BytePerPixelY;
	int BytePerPixelC;
	bool ProgressiveToInterlaceUnitInOPP;
} Pipe;

#define BPP_INVALID 0
#define BPP_BLENDED_PIPE 0xffffffff

static bool CalculateBytePerPixelAnd256BBlockSizes(
		enum source_format_class SourcePixelFormat,
		enum dm_swizzle_mode SurfaceTiling,
		unsigned int *BytePerPixelY,
		unsigned int *BytePerPixelC,
		double *BytePerPixelDETY,
		double *BytePerPixelDETC,
		unsigned int *BlockHeight256BytesY,
		unsigned int *BlockHeight256BytesC,
		unsigned int *BlockWidth256BytesY,
		unsigned int *BlockWidth256BytesC);
static void DisplayPipeConfiguration(struct display_mode_lib *mode_lib);
static void DISPCLKDPPCLKDCFCLKDeepSleepPrefetchParametersWatermarksAndPerformanceCalculation(struct display_mode_lib *mode_lib);
static unsigned int dscceComputeDelay(
		unsigned int bpc,
		double BPP,
		unsigned int sliceWidth,
		unsigned int numSlices,
		enum output_format_class pixelFormat,
		enum output_encoder_class Output);
static unsigned int dscComputeDelay(enum output_format_class pixelFormat, enum output_encoder_class Output);
static bool CalculatePrefetchSchedule(
		struct display_mode_lib *mode_lib,
		double HostVMInefficiencyFactor,
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
		double VInitPreFillY,
		unsigned int MaxNumSwathY,
		double PrefetchSourceLinesC,
		unsigned int SwathWidthC,
		double VInitPreFillC,
		unsigned int MaxNumSwathC,
		int swath_width_luma_ub,
		int swath_width_chroma_ub,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		double TWait,
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
		double *TSetup,
		int *VUpdateOffsetPix,
		double *VUpdateWidthPix,
		double *VReadyOffsetPix);
static double RoundToDFSGranularityUp(double Clock, double VCOSpeed);
static double RoundToDFSGranularityDown(double Clock, double VCOSpeed);
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
		int *dpte_row_width_ub,
		unsigned int *dpte_row_height,
		unsigned int *MetaRequestWidth,
		unsigned int *MetaRequestHeight,
		unsigned int *meta_row_width,
		unsigned int *meta_row_height,
		int *vm_group_bytes,
		unsigned int *dpte_group_bytes,
		unsigned int *PixelPTEReqWidth,
		unsigned int *PixelPTEReqHeight,
		unsigned int *PTERequestSize,
		int *DPDE0BytesFrame,
		int *MetaPTEBytesFrame);
static double CalculateTWait(unsigned int PrefetchMode, double DRAMClockChangeLatency, double UrgentLatency, double SREnterPlusExitTime);
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
		unsigned int k,
		double HostVMInefficiencyFactor,
		double UrgentExtraLatency,
		double UrgentLatency,
		double PDEAndMetaPTEBytesPerFrame,
		double MetaRowBytes,
		double DPTEBytesPerRow);
static double CalculateWriteBackDelay(
		enum source_format_class WritebackPixelFormat,
		double WritebackHRatio,
		double WritebackVRatio,
		unsigned int WritebackVTaps,
		int WritebackDestinationWidth,
		int WritebackDestinationHeight,
		int WritebackSourceHeight,
		unsigned int HTotal);

static void CalculateVupdateAndDynamicMetadataParameters(
		int MaxInterDCNTileRepeaters,
		double DPPCLK,
		double DISPCLK,
		double DCFClkDeepSleep,
		double PixelClock,
		int HTotal,
		int VBlank,
		int DynamicMetadataTransmittedBytes,
		int DynamicMetadataLinesBeforeActiveRequired,
		int InterlaceEnable,
		bool ProgressiveToInterlaceUnitInOPP,
		double *TSetup,
		double *Tdmbf,
		double *Tdmec,
		double *Tdmsks,
		int *VUpdateOffsetPix,
		double *VUpdateWidthPix,
		double *VReadyOffsetPix);

static void CalculateWatermarksAndDRAMSpeedChangeSupport(
		struct display_mode_lib *mode_lib,
		unsigned int PrefetchMode,
		double DCFCLK,
		double ReturnBW,
		double UrgentLatency,
		double ExtraLatency,
		double SOCCLK,
		double DCFCLKDeepSleep,
		unsigned int DETBufferSizeY[],
		unsigned int DETBufferSizeC[],
		unsigned int SwathHeightY[],
		unsigned int SwathHeightC[],
		double SwathWidthY[],
		double SwathWidthC[],
		unsigned int DPPPerPlane[],
		double BytePerPixelDETY[],
		double BytePerPixelDETC[],
		bool UnboundedRequestEnabled,
		unsigned int CompressedBufferSizeInkByte,
		enum clock_change_support *DRAMClockChangeSupport,
		double *StutterExitWatermark,
		double *StutterEnterPlusExitWatermark,
		double *Z8StutterExitWatermark,
		double *Z8StutterEnterPlusExitWatermark);

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
		int swath_width_luma_ub,
		int swath_width_chroma_ub,
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
		unsigned int CursorWidth[][DC__NUM_CURSOR__MAX],
		unsigned int CursorBPP[][DC__NUM_CURSOR__MAX],
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
		struct display_mode_lib *mode_lib,
		int CompressedBufferSizeInkByte,
		bool UnboundedRequestEnabled,
		int ConfigReturnBufferSizeInKByte,
		int MetaFIFOSizeInKEntries,
		int ZeroSizeBufferEntries,
		int NumberOfActivePlanes,
		int ROBBufferSizeInKByte,
		double TotalDataReadBandwidth,
		double DCFCLK,
		double ReturnBW,
		double COMPBUF_RESERVED_SPACE_64B,
		double COMPBUF_RESERVED_SPACE_ZS,
		double SRExitTime,
		double SRExitZ8Time,
		bool SynchronizedVBlank,
		double Z8StutterEnterPlusExitWatermark,
		double StutterEnterPlusExitWatermark,
		bool ProgressiveToInterlaceUnitInOPP,
		bool Interlace[],
		double MinTTUVBlank[],
		int DPPPerPlane[],
		unsigned int DETBufferSizeY[],
		int BytePerPixelY[],
		double BytePerPixelDETY[],
		double SwathWidthY[],
		int SwathHeightY[],
		int SwathHeightC[],
		double NetDCCRateLuma[],
		double NetDCCRateChroma[],
		double DCCFractionOfZeroSizeRequestsLuma[],
		double DCCFractionOfZeroSizeRequestsChroma[],
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
		int *NumberOfStutterBurstsPerFrame,
		double *Z8StutterEfficiencyNotIncludingVBlank,
		double *Z8StutterEfficiency,
		int *Z8NumberOfStutterBurstsPerFrame,
		double *StutterPeriod);

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
		int ViewportWidth[],
		int ViewportHeight[],
		int SurfaceWidthY[],
		int SurfaceWidthC[],
		int SurfaceHeightY[],
		int SurfaceHeightC[],
		enum odm_combine_mode ODMCombineEnabled[],
		int BytePerPixY[],
		int BytePerPixC[],
		int Read256BytesBlockHeightY[],
		int Read256BytesBlockHeightC[],
		int Read256BytesBlockWidthY[],
		int Read256BytesBlockWidthC[],
		int BlendingAndTiming[],
		int HActive[],
		double HRatio[],
		int DPPPerPlane[],
		double SwathWidthSingleDPPY[],
		double SwathWidthSingleDPPC[],
		double SwathWidthY[],
		double SwathWidthC[],
		int MaximumSwathHeightY[],
		int MaximumSwathHeightC[],
		int swath_width_luma_ub[],
		int swath_width_chroma_ub[]);

static double CalculateExtraLatency(
		int RoundTripPingLatencyCycles,
		int ReorderingBytes,
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
		double HostVMInefficiencyFactor,
		double HostVMMinPageSize,
		int HostVMMaxNonCachedPageTableLevels);

static double CalculateExtraLatencyBytes(
		int ReorderingBytes,
		int TotalNumberOfActiveDPP,
		int PixelChunkSizeInKByte,
		int TotalNumberOfDCCActiveDPP,
		int MetaChunkSize,
		bool GPUVMEnable,
		bool HostVMEnable,
		int NumberOfActivePlanes,
		int NumberOfDPP[],
		int dpte_group_bytes[],
		double HostVMInefficiencyFactor,
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

static void CalculateUnboundedRequestAndCompressedBufferSize(
		unsigned int DETBufferSizeInKByte,
		int ConfigReturnBufferSizeInKByte,
		enum unbounded_requesting_policy UseUnboundedRequestingFinal,
		int TotalActiveDPP,
		bool NoChromaPlanes,
		int MaxNumDPP,
		int CompressedBufferSegmentSizeInkByteFinal,
		enum output_encoder_class *Output,
		bool *UnboundedRequestEnabled,
		int *CompressedBufferSizeInkByte);

static bool UnboundedRequest(enum unbounded_requesting_policy UseUnboundedRequestingFinal, int TotalNumberOfActiveDPP, bool NoChroma, enum output_encoder_class Output);
static unsigned int CalculateMaxVStartup(
		unsigned int VTotal,
		unsigned int VActive,
		unsigned int VBlankNom,
		unsigned int HTotal,
		double PixelClock,
		bool ProgressiveTointerlaceUnitinOPP,
		bool Interlace,
		unsigned int VBlankNomDefaultUS,
		double WritebackDelayTime);

void dml314_recalculate(struct display_mode_lib *mode_lib)
{
	ModeSupportAndSystemConfiguration(mode_lib);
	PixelClockAdjustmentForProgressiveToInterlaceUnit(mode_lib);
	DisplayPipeConfiguration(mode_lib);
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: Calling DISPCLKDPPCLKDCFCLKDeepSleepPrefetchParametersWatermarksAndPerformanceCalculation\n", __func__);
#endif
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
	unsigned int pixelsPerClock = 0, lstall, D, initalXmitDelay, w, s, ix, wx, P, l0, a, ax, L, Delay, pixels;

	if (pixelFormat == dm_420)
		pixelsPerClock = 2;
	else if (pixelFormat == dm_444)
		pixelsPerClock = 1;
	else if (pixelFormat == dm_n422)
		pixelsPerClock = 2;
	// #all other modes operate at 1 pixel per clock
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
		double HostVMInefficiencyFactor,
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
		double VInitPreFillY,
		unsigned int MaxNumSwathY,
		double PrefetchSourceLinesC,
		unsigned int SwathWidthC,
		double VInitPreFillC,
		unsigned int MaxNumSwathC,
		int swath_width_luma_ub,
		int swath_width_chroma_ub,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		double TWait,
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
		double *TSetup,
		int *VUpdateOffsetPix,
		double *VUpdateWidthPix,
		double *VReadyOffsetPix)
{
	bool MyError = false;
	unsigned int DPPCycles, DISPCLKCycles;
	double DSTTotalPixelsAfterScaler;
	double LineTime;
	double dst_y_prefetch_equ;
	double Tsw_oto;
	double prefetch_bw_oto;
	double prefetch_bw_pr;
	double Tvm_oto;
	double Tr0_oto;
	double Tvm_oto_lines;
	double Tr0_oto_lines;
	double dst_y_prefetch_oto;
	double TimeForFetchingMetaPTE = 0;
	double TimeForFetchingRowInVBlank = 0;
	double LinesToRequestPrefetchPixelData = 0;
	unsigned int HostVMDynamicLevelsTrips;
	double trip_to_mem;
	double Tvm_trips;
	double Tr0_trips;
	double Tvm_trips_rounded;
	double Tr0_trips_rounded;
	double Lsw_oto;
	double Tpre_rounded;
	double prefetch_bw_equ;
	double Tvm_equ;
	double Tr0_equ;
	double Tdmbf;
	double Tdmec;
	double Tdmsks;
	double prefetch_sw_bytes;
	double bytes_pp;
	double dep_bytes;
	int max_vratio_pre = 4;
	double min_Lsw;
	double Tsw_est1 = 0;
	double Tsw_est3 = 0;
	double  max_Tsw = 0;

	if (GPUVMEnable == true && HostVMEnable == true) {
		HostVMDynamicLevelsTrips = HostVMMaxNonCachedPageTableLevels;
	} else {
		HostVMDynamicLevelsTrips = 0;
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: GPUVMEnable=%d HostVMEnable=%d HostVMInefficiencyFactor=%f\n", __func__, GPUVMEnable, HostVMEnable, HostVMInefficiencyFactor);
#endif
	CalculateVupdateAndDynamicMetadataParameters(
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
			myPipe->ProgressiveToInterlaceUnitInOPP,
			TSetup,
			&Tdmbf,
			&Tdmec,
			&Tdmsks,
			VUpdateOffsetPix,
			VUpdateWidthPix,
			VReadyOffsetPix);

	LineTime = myPipe->HTotal / myPipe->PixelClock;
	trip_to_mem = UrgentLatency;
	Tvm_trips = UrgentExtraLatency + trip_to_mem * (GPUVMPageTableLevels * (HostVMDynamicLevelsTrips + 1) - 1);

#ifdef __DML_VBA_ALLOW_DELTA__
	if (DynamicMetadataVMEnabled == true && GPUVMEnable == true) {
#else
	if (DynamicMetadataVMEnabled == true) {
#endif
		*Tdmdl = TWait + Tvm_trips + trip_to_mem;
	} else {
		*Tdmdl = TWait + UrgentExtraLatency;
	}

#ifdef __DML_VBA_ALLOW_DELTA__
	if (DynamicMetadataEnable == false) {
		*Tdmdl = 0.0;
	}
#endif

	if (DynamicMetadataEnable == true) {
		if (VStartup * LineTime < *TSetup + *Tdmdl + Tdmbf + Tdmec + Tdmsks) {
			*NotEnoughTimeForDynamicMetadata = true;
			dml_print("DML::%s: Not Enough Time for Dynamic Meta!\n", __func__);
			dml_print("DML::%s: Tdmbf: %fus - time for dmd transfer from dchub to dio output buffer\n", __func__, Tdmbf);
			dml_print("DML::%s: Tdmec: %fus - time dio takes to transfer dmd\n", __func__, Tdmec);
			dml_print("DML::%s: Tdmsks: %fus - time before active dmd must complete transmission at dio\n", __func__, Tdmsks);
			dml_print("DML::%s: Tdmdl: %fus - time for fabric to become ready and fetch dmd\n", __func__, *Tdmdl);
		} else {
			*NotEnoughTimeForDynamicMetadata = false;
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

	*DSTXAfterScaler = DPPCycles * myPipe->PixelClock / myPipe->DPPCLK + DISPCLKCycles * myPipe->PixelClock / myPipe->DISPCLK + DSCDelay;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DPPCycles: %d\n", __func__, DPPCycles);
	dml_print("DML::%s: PixelClock: %f\n", __func__, myPipe->PixelClock);
	dml_print("DML::%s: DPPCLK: %f\n", __func__, myPipe->DPPCLK);
	dml_print("DML::%s: DISPCLKCycles: %d\n", __func__, DISPCLKCycles);
	dml_print("DML::%s: DISPCLK: %f\n", __func__, myPipe->DISPCLK);
	dml_print("DML::%s: DSCDelay: %d\n", __func__, DSCDelay);
	dml_print("DML::%s: DSTXAfterScaler: %d\n", __func__, *DSTXAfterScaler);
	dml_print("DML::%s: ODMCombineIsEnabled: %d\n", __func__, myPipe->ODMCombineIsEnabled);
#endif

	*DSTXAfterScaler = *DSTXAfterScaler + ((myPipe->ODMCombineIsEnabled) ? 18 : 0) + (myPipe->DPPPerPlane - 1) * DPP_RECOUT_WIDTH;

	if (OutputFormat == dm_420 || (myPipe->InterlaceEnable && myPipe->ProgressiveToInterlaceUnitInOPP))
		*DSTYAfterScaler = 1;
	else
		*DSTYAfterScaler = 0;

	DSTTotalPixelsAfterScaler = *DSTYAfterScaler * myPipe->HTotal + *DSTXAfterScaler;
	*DSTYAfterScaler = dml_floor(DSTTotalPixelsAfterScaler / myPipe->HTotal, 1);
	*DSTXAfterScaler = DSTTotalPixelsAfterScaler - ((double) (*DSTYAfterScaler * myPipe->HTotal));

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DSTXAfterScaler: %d (final)\n", __func__, *DSTXAfterScaler);
#endif

	MyError = false;

	Tr0_trips = trip_to_mem * (HostVMDynamicLevelsTrips + 1);
	Tvm_trips_rounded = dml_ceil(4.0 * Tvm_trips / LineTime, 1) / 4 * LineTime;
	Tr0_trips_rounded = dml_ceil(4.0 * Tr0_trips / LineTime, 1) / 4 * LineTime;

#ifdef __DML_VBA_ALLOW_DELTA__
	if (!myPipe->DCCEnable) {
		Tr0_trips = 0.0;
		Tr0_trips_rounded = 0.0;
	}
#endif

	if (!GPUVMEnable) {
		Tvm_trips = 0.0;
		Tvm_trips_rounded = 0.0;
	}

	if (GPUVMEnable) {
		if (GPUVMPageTableLevels >= 3) {
			*Tno_bw = UrgentExtraLatency + trip_to_mem * ((GPUVMPageTableLevels - 2) - 1);
		} else {
			*Tno_bw = 0;
		}
	} else if (!myPipe->DCCEnable) {
		*Tno_bw = LineTime;
	} else {
		*Tno_bw = LineTime / 4;
	}

	if (myPipe->SourcePixelFormat == dm_420_8 || myPipe->SourcePixelFormat == dm_420_10 || myPipe->SourcePixelFormat == dm_420_12)
		bytes_pp = myPipe->BytePerPixelY + myPipe->BytePerPixelC / 4;
	else
		bytes_pp = myPipe->BytePerPixelY + myPipe->BytePerPixelC;
	/*rev 99*/
	prefetch_bw_pr = dml_min(1, bytes_pp * myPipe->PixelClock / (double) myPipe->DPPPerPlane);
	max_Tsw = dml_max(PrefetchSourceLinesY, PrefetchSourceLinesC) * LineTime;
	prefetch_sw_bytes = PrefetchSourceLinesY * swath_width_luma_ub * myPipe->BytePerPixelY + PrefetchSourceLinesC * swath_width_chroma_ub * myPipe->BytePerPixelC;
	prefetch_bw_oto = dml_max(bytes_pp * myPipe->PixelClock / myPipe->DPPPerPlane, prefetch_sw_bytes / (dml_max(PrefetchSourceLinesY, PrefetchSourceLinesC) * LineTime));
	prefetch_bw_oto = dml_max(prefetch_bw_pr, prefetch_sw_bytes / max_Tsw);

	min_Lsw = dml_max(1, dml_max(PrefetchSourceLinesY, PrefetchSourceLinesC) / max_vratio_pre);
	Lsw_oto = dml_ceil(4 * dml_max(prefetch_sw_bytes / prefetch_bw_oto / LineTime, min_Lsw), 1) / 4;
	Tsw_oto = Lsw_oto * LineTime;

	prefetch_bw_oto = (PrefetchSourceLinesY * swath_width_luma_ub * myPipe->BytePerPixelY + PrefetchSourceLinesC * swath_width_chroma_ub * myPipe->BytePerPixelC) / Tsw_oto;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML: HTotal: %d\n", myPipe->HTotal);
	dml_print("DML: prefetch_bw_oto: %f\n", prefetch_bw_oto);
	dml_print("DML: PrefetchSourceLinesY: %f\n", PrefetchSourceLinesY);
	dml_print("DML: swath_width_luma_ub: %d\n", swath_width_luma_ub);
	dml_print("DML: BytePerPixelY: %d\n", myPipe->BytePerPixelY);
	dml_print("DML: Tsw_oto: %f\n", Tsw_oto);
#endif

	if (GPUVMEnable == true)
		Tvm_oto = dml_max3(*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / prefetch_bw_oto, Tvm_trips, LineTime / 4.0);
	else
		Tvm_oto = LineTime / 4.0;

	if ((GPUVMEnable == true || myPipe->DCCEnable == true)) {
		Tr0_oto = dml_max4((MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / prefetch_bw_oto, Tr0_trips, // PREVIOUS_ERROR (missing this term)
				LineTime - Tvm_oto,
				LineTime / 4);
	} else {
		Tr0_oto = (LineTime - Tvm_oto) / 2.0;
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: Tvm_trips = %f\n", __func__, Tvm_trips);
	dml_print("DML::%s: Tr0_trips = %f\n", __func__, Tr0_trips);
	dml_print("DML::%s: PDEAndMetaPTEBytesFrame = %d\n", __func__, MetaRowByte);
	dml_print("DML::%s: MetaRowByte = %d\n", __func__, MetaRowByte);
	dml_print("DML::%s: PixelPTEBytesPerRow = %d\n", __func__, PixelPTEBytesPerRow);
	dml_print("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, HostVMInefficiencyFactor);
	dml_print("DML::%s: prefetch_bw_oto = %f\n", __func__, prefetch_bw_oto);
	dml_print("DML::%s: Tr0_oto = %f\n", __func__, Tr0_oto);
	dml_print("DML::%s: Tvm_oto = %f\n", __func__, Tvm_oto);
#endif

	Tvm_oto_lines = dml_ceil(4.0 * Tvm_oto / LineTime, 1) / 4.0;
	Tr0_oto_lines = dml_ceil(4.0 * Tr0_oto / LineTime, 1) / 4.0;
	dst_y_prefetch_oto = Tvm_oto_lines + 2 * Tr0_oto_lines + Lsw_oto;
	dst_y_prefetch_equ =  VStartup - (*TSetup + dml_max(TWait + TCalc, *Tdmdl)) / LineTime - (*DSTYAfterScaler + *DSTXAfterScaler / myPipe->HTotal);
	dst_y_prefetch_equ = dml_floor(4.0 * (dst_y_prefetch_equ + 0.125), 1) / 4.0;
	Tpre_rounded = dst_y_prefetch_equ * LineTime;

	dep_bytes = dml_max(PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor, MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor);

	if (prefetch_sw_bytes < dep_bytes)
		prefetch_sw_bytes = 2 * dep_bytes;

	dml_print("DML: dst_y_prefetch_oto: %f\n", dst_y_prefetch_oto);
	dml_print("DML: Tvm_oto_lines: %f\n", Tvm_oto_lines);
	dml_print("DML: Tr0_oto_lines: %f\n", Tr0_oto_lines);
	dml_print("DML: Lsw_oto: %f\n", Lsw_oto);
	dml_print("DML: LineTime: %f\n", LineTime);
	dml_print("DML: dst_y_prefetch_equ: %f (after round)\n", dst_y_prefetch_equ);

	dml_print("DML: LineTime: %f\n", LineTime);
	dml_print("DML: VStartup: %d\n", VStartup);
	dml_print("DML: Tvstartup: %fus - time between vstartup and first pixel of active\n", VStartup * LineTime);
	dml_print("DML: TSetup: %fus - time from vstartup to vready\n", *TSetup);
	dml_print("DML: TCalc: %fus - time for calculations in dchub starting at vready\n", TCalc);
	dml_print("DML: TWait: %fus - time for fabric to become ready max(pstate exit,cstate enter/exit, urgent latency) after TCalc\n", TWait);
	dml_print("DML: Tdmbf: %fus - time for dmd transfer from dchub to dio output buffer\n", Tdmbf);
	dml_print("DML: Tdmec: %fus - time dio takes to transfer dmd\n", Tdmec);
	dml_print("DML: Tdmsks: %fus - time before active dmd must complete transmission at dio\n", Tdmsks);
	dml_print("DML: Tdmdl_vm: %fus - time for vm stages of dmd\n", *Tdmdl_vm);
	dml_print("DML: Tdmdl: %fus - time for fabric to become ready and fetch dmd\n", *Tdmdl);
	dml_print("DML: DSTXAfterScaler: %f pixels - number of pixel clocks pipeline and buffer delay after scaler\n", *DSTXAfterScaler);
	dml_print("DML: DSTYAfterScaler: %f lines - number of lines of pipeline and buffer delay after scaler\n", *DSTYAfterScaler);

	*PrefetchBandwidth = 0;
	*DestinationLinesToRequestVMInVBlank = 0;
	*DestinationLinesToRequestRowInVBlank = 0;
	*VRatioPrefetchY = 0;
	*VRatioPrefetchC = 0;
	*RequiredPrefetchPixDataBWLuma = 0;
	if (dst_y_prefetch_equ > 1) {
		double PrefetchBandwidth1;
		double PrefetchBandwidth2;
		double PrefetchBandwidth3;
		double PrefetchBandwidth4;

		if (Tpre_rounded - *Tno_bw > 0) {
			PrefetchBandwidth1 = (PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor + 2 * MetaRowByte + 2 * PixelPTEBytesPerRow * HostVMInefficiencyFactor
					+ prefetch_sw_bytes) / (Tpre_rounded - *Tno_bw);
			Tsw_est1 = prefetch_sw_bytes / PrefetchBandwidth1;
		} else {
			PrefetchBandwidth1 = 0;
		}

		if (VStartup == MaxVStartup && Tsw_est1 / LineTime < min_Lsw && Tpre_rounded - min_Lsw * LineTime - 0.75 * LineTime - *Tno_bw > 0) {
			PrefetchBandwidth1 = (PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor + 2 * MetaRowByte + 2 * PixelPTEBytesPerRow * HostVMInefficiencyFactor)
					/ (Tpre_rounded - min_Lsw * LineTime - 0.75 * LineTime - *Tno_bw);
		}

		if (Tpre_rounded - *Tno_bw - 2 * Tr0_trips_rounded > 0)
			PrefetchBandwidth2 = (PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor + prefetch_sw_bytes) / (Tpre_rounded - *Tno_bw - 2 * Tr0_trips_rounded);
		else
			PrefetchBandwidth2 = 0;

		if (Tpre_rounded - Tvm_trips_rounded > 0) {
			PrefetchBandwidth3 = (2 * MetaRowByte + 2 * PixelPTEBytesPerRow * HostVMInefficiencyFactor
					+ prefetch_sw_bytes) / (Tpre_rounded - Tvm_trips_rounded);
			Tsw_est3 = prefetch_sw_bytes / PrefetchBandwidth3;
		} else {
			PrefetchBandwidth3 = 0;
		}

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: Tpre_rounded: %f\n", __func__, Tpre_rounded);
		dml_print("DML::%s: Tvm_trips_rounded: %f\n", __func__, Tvm_trips_rounded);
		dml_print("DML::%s: PrefetchBandwidth3: %f\n", __func__, PrefetchBandwidth3);
#endif
		if (VStartup == MaxVStartup && Tsw_est3 / LineTime < min_Lsw && Tpre_rounded - min_Lsw * LineTime - 0.75 * LineTime - Tvm_trips_rounded > 0) {
			PrefetchBandwidth3 = (2 * MetaRowByte + 2 * PixelPTEBytesPerRow * HostVMInefficiencyFactor)
					/ (Tpre_rounded - min_Lsw * LineTime - 0.75 * LineTime - Tvm_trips_rounded);
		}

		if (Tpre_rounded - Tvm_trips_rounded - 2 * Tr0_trips_rounded > 0)
			PrefetchBandwidth4 = prefetch_sw_bytes / (Tpre_rounded - Tvm_trips_rounded - 2 * Tr0_trips_rounded);
		else
			PrefetchBandwidth4 = 0;

		{
			bool Case1OK;
			bool Case2OK;
			bool Case3OK;

			if (PrefetchBandwidth1 > 0) {
				if (*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / PrefetchBandwidth1 >= Tvm_trips_rounded
						&& (MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / PrefetchBandwidth1 >= Tr0_trips_rounded) {
					Case1OK = true;
				} else {
					Case1OK = false;
				}
			} else {
				Case1OK = false;
			}

			if (PrefetchBandwidth2 > 0) {
				if (*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / PrefetchBandwidth2 >= Tvm_trips_rounded
						&& (MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / PrefetchBandwidth2 < Tr0_trips_rounded) {
					Case2OK = true;
				} else {
					Case2OK = false;
				}
			} else {
				Case2OK = false;
			}

			if (PrefetchBandwidth3 > 0) {
				if (*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / PrefetchBandwidth3 < Tvm_trips_rounded
						&& (MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / PrefetchBandwidth3 >= Tr0_trips_rounded) {
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

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: Case1OK: %d\n", __func__, Case1OK);
			dml_print("DML::%s: Case2OK: %d\n", __func__, Case2OK);
			dml_print("DML::%s: Case3OK: %d\n", __func__, Case3OK);
			dml_print("DML::%s: prefetch_bw_equ: %f\n", __func__, prefetch_bw_equ);
#endif

			if (prefetch_bw_equ > 0) {
				if (GPUVMEnable == true) {
					Tvm_equ = dml_max3(*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / prefetch_bw_equ, Tvm_trips, LineTime / 4);
				} else {
					Tvm_equ = LineTime / 4;
				}

				if ((GPUVMEnable == true || myPipe->DCCEnable == true)) {
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

#ifdef __DML_VBA_ALLOW_DELTA__
		LinesToRequestPrefetchPixelData = *DestinationLinesForPrefetch
		// See note above dated 5/30/2018
		//                      - ((NumberOfCursors > 0 || GPUVMEnable || DCCEnable) ?
				- ((GPUVMEnable || myPipe->DCCEnable) ? (*DestinationLinesToRequestVMInVBlank + 2 * *DestinationLinesToRequestRowInVBlank) : 0.0); // TODO: Did someone else add this??
#else
		LinesToRequestPrefetchPixelData = *DestinationLinesForPrefetch - *DestinationLinesToRequestVMInVBlank - 2 * *DestinationLinesToRequestRowInVBlank;
#endif

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: DestinationLinesForPrefetch = %f\n", __func__, *DestinationLinesForPrefetch);
		dml_print("DML::%s: DestinationLinesToRequestVMInVBlank = %f\n", __func__, *DestinationLinesToRequestVMInVBlank);
		dml_print("DML::%s: TimeForFetchingRowInVBlank = %f\n", __func__, TimeForFetchingRowInVBlank);
		dml_print("DML::%s: LineTime = %f\n", __func__, LineTime);
		dml_print("DML::%s: DestinationLinesToRequestRowInVBlank = %f\n", __func__, *DestinationLinesToRequestRowInVBlank);
		dml_print("DML::%s: PrefetchSourceLinesY = %f\n", __func__, PrefetchSourceLinesY);
		dml_print("DML::%s: LinesToRequestPrefetchPixelData = %f\n", __func__, LinesToRequestPrefetchPixelData);
#endif

		if (LinesToRequestPrefetchPixelData > 0 && prefetch_bw_equ > 0) {

			*VRatioPrefetchY = (double) PrefetchSourceLinesY / LinesToRequestPrefetchPixelData;
			*VRatioPrefetchY = dml_max(*VRatioPrefetchY, 1.0);
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: VRatioPrefetchY = %f\n", __func__, *VRatioPrefetchY);
			dml_print("DML::%s: SwathHeightY = %d\n", __func__, SwathHeightY);
			dml_print("DML::%s: VInitPreFillY = %f\n", __func__, VInitPreFillY);
#endif
			if ((SwathHeightY > 4) && (VInitPreFillY > 3)) {
				if (LinesToRequestPrefetchPixelData > (VInitPreFillY - 3.0) / 2.0) {
					*VRatioPrefetchY = dml_max(
							(double) PrefetchSourceLinesY / LinesToRequestPrefetchPixelData,
							(double) MaxNumSwathY * SwathHeightY / (LinesToRequestPrefetchPixelData - (VInitPreFillY - 3.0) / 2.0));
					*VRatioPrefetchY = dml_max(*VRatioPrefetchY, 1.0);
				} else {
					MyError = true;
					dml_print("DML: MyErr set %s:%d\n", __FILE__, __LINE__);
					*VRatioPrefetchY = 0;
				}
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: VRatioPrefetchY = %f\n", __func__, *VRatioPrefetchY);
				dml_print("DML::%s: PrefetchSourceLinesY = %f\n", __func__, PrefetchSourceLinesY);
				dml_print("DML::%s: MaxNumSwathY = %d\n", __func__, MaxNumSwathY);
#endif
			}

			*VRatioPrefetchC = (double) PrefetchSourceLinesC / LinesToRequestPrefetchPixelData;
			*VRatioPrefetchC = dml_max(*VRatioPrefetchC, 1.0);

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: VRatioPrefetchC = %f\n", __func__, *VRatioPrefetchC);
			dml_print("DML::%s: SwathHeightC = %d\n", __func__, SwathHeightC);
			dml_print("DML::%s: VInitPreFillC = %f\n", __func__, VInitPreFillC);
#endif
			if ((SwathHeightC > 4) || VInitPreFillC > 3) {
				if (LinesToRequestPrefetchPixelData > (VInitPreFillC - 3.0) / 2.0) {
					*VRatioPrefetchC = dml_max(
							*VRatioPrefetchC,
							(double) MaxNumSwathC * SwathHeightC / (LinesToRequestPrefetchPixelData - (VInitPreFillC - 3.0) / 2.0));
					*VRatioPrefetchC = dml_max(*VRatioPrefetchC, 1.0);
				} else {
					MyError = true;
					dml_print("DML: MyErr set %s:%d\n", __FILE__, __LINE__);
					*VRatioPrefetchC = 0;
				}
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: VRatioPrefetchC = %f\n", __func__, *VRatioPrefetchC);
				dml_print("DML::%s: PrefetchSourceLinesC = %f\n", __func__, PrefetchSourceLinesC);
				dml_print("DML::%s: MaxNumSwathC = %d\n", __func__, MaxNumSwathC);
#endif
			}

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: BytePerPixelY = %d\n", __func__, myPipe->BytePerPixelY);
			dml_print("DML::%s: swath_width_luma_ub = %d\n", __func__, swath_width_luma_ub);
			dml_print("DML::%s: LineTime = %f\n", __func__, LineTime);
#endif

			*RequiredPrefetchPixDataBWLuma = (double) PrefetchSourceLinesY / LinesToRequestPrefetchPixelData * myPipe->BytePerPixelY * swath_width_luma_ub / LineTime;

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: RequiredPrefetchPixDataBWLuma = %f\n", __func__, *RequiredPrefetchPixDataBWLuma);
#endif

			*RequiredPrefetchPixDataBWChroma = (double) PrefetchSourceLinesC / LinesToRequestPrefetchPixelData * myPipe->BytePerPixelC * swath_width_chroma_ub
					/ LineTime;
		} else {
			MyError = true;
			dml_print("DML: MyErr set %s:%d\n", __FILE__, __LINE__);
			dml_print("DML: LinesToRequestPrefetchPixelData: %f, should be > 0\n", LinesToRequestPrefetchPixelData);
			*VRatioPrefetchY = 0;
			*VRatioPrefetchC = 0;
			*RequiredPrefetchPixDataBWLuma = 0;
			*RequiredPrefetchPixDataBWChroma = 0;
		}

		dml_print(
				"DML: Tpre: %fus - sum of time to request meta pte, 2 x data pte + meta data, swaths\n",
				(double) LinesToRequestPrefetchPixelData * LineTime + 2.0 * TimeForFetchingRowInVBlank + TimeForFetchingMetaPTE);
		dml_print("DML:  Tvm: %fus - time to fetch page tables for meta surface\n", TimeForFetchingMetaPTE);
		dml_print("DML:  Tr0: %fus - time to fetch first row of data pagetables and first row of meta data (done in parallel)\n", TimeForFetchingRowInVBlank);
		dml_print(
				"DML:  Tsw: %fus = time to fetch enough pixel data and cursor data to feed the scalers init position and detile\n",
				(double) LinesToRequestPrefetchPixelData * LineTime);
		dml_print("DML: To: %fus - time for propagation from scaler to optc\n", (*DSTYAfterScaler + ((double) (*DSTXAfterScaler) / (double) myPipe->HTotal)) * LineTime);
		dml_print("DML: Tvstartup - TSetup - Tcalc - Twait - Tpre - To > 0\n");
		dml_print(
				"DML: Tslack(pre): %fus - time left over in schedule\n",
				VStartup * LineTime - TimeForFetchingMetaPTE - 2 * TimeForFetchingRowInVBlank
						- (*DSTYAfterScaler + ((double) (*DSTXAfterScaler) / (double) myPipe->HTotal)) * LineTime - TWait - TCalc - *TSetup);
		dml_print("DML: row_bytes = dpte_row_bytes (per_pipe) = PixelPTEBytesPerRow = : %d\n", PixelPTEBytesPerRow);

	} else {
		MyError = true;
		dml_print("DML: MyErr set %s:%d\n", __FILE__, __LINE__);
	}

	{
		double prefetch_vm_bw;
		double prefetch_row_bw;

		if (PDEAndMetaPTEBytesFrame == 0) {
			prefetch_vm_bw = 0;
		} else if (*DestinationLinesToRequestVMInVBlank > 0) {
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: PDEAndMetaPTEBytesFrame = %d\n", __func__, PDEAndMetaPTEBytesFrame);
			dml_print("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, HostVMInefficiencyFactor);
			dml_print("DML::%s: DestinationLinesToRequestVMInVBlank = %f\n", __func__, *DestinationLinesToRequestVMInVBlank);
			dml_print("DML::%s: LineTime = %f\n", __func__, LineTime);
#endif
			prefetch_vm_bw = PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / (*DestinationLinesToRequestVMInVBlank * LineTime);
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: prefetch_vm_bw = %f\n", __func__, prefetch_vm_bw);
#endif
		} else {
			prefetch_vm_bw = 0;
			MyError = true;
			dml_print("DML: MyErr set %s:%d\n", __FILE__, __LINE__);
		}

		if (MetaRowByte + PixelPTEBytesPerRow == 0) {
			prefetch_row_bw = 0;
		} else if (*DestinationLinesToRequestRowInVBlank > 0) {
			prefetch_row_bw = (MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / (*DestinationLinesToRequestRowInVBlank * LineTime);

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: MetaRowByte = %d\n", __func__, MetaRowByte);
			dml_print("DML::%s: PixelPTEBytesPerRow = %d\n", __func__, PixelPTEBytesPerRow);
			dml_print("DML::%s: DestinationLinesToRequestRowInVBlank = %f\n", __func__, *DestinationLinesToRequestRowInVBlank);
			dml_print("DML::%s: prefetch_row_bw = %f\n", __func__, prefetch_row_bw);
#endif
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
	int yuv420;
	int horz_div_l;
	int horz_div_c;
	int vert_div_l;
	int vert_div_c;

	int swath_buf_size;
	double detile_buf_vp_horz_limit;
	double detile_buf_vp_vert_limit;

	int MAS_vp_horz_limit;
	int MAS_vp_vert_limit;
	int max_vp_horz_width;
	int max_vp_vert_height;
	int eff_surf_width_l;
	int eff_surf_width_c;
	int eff_surf_height_l;
	int eff_surf_height_c;

	int full_swath_bytes_horz_wc_l;
	int full_swath_bytes_horz_wc_c;
	int full_swath_bytes_vert_wc_l;
	int full_swath_bytes_vert_wc_c;
	int req128_horz_wc_l;
	int req128_horz_wc_c;
	int req128_vert_wc_l;
	int req128_vert_wc_c;
	int segment_order_horz_contiguous_luma;
	int segment_order_horz_contiguous_chroma;
	int segment_order_vert_contiguous_luma;
	int segment_order_vert_contiguous_chroma;

	typedef enum {
		REQ_256Bytes, REQ_128BytesNonContiguous, REQ_128BytesContiguous, REQ_NA
	} RequestType;
	RequestType RequestLuma;
	RequestType RequestChroma;

	yuv420 = ((SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10 || SourcePixelFormat == dm_420_12) ? 1 : 0);
	horz_div_l = 1;
	horz_div_c = 1;
	vert_div_l = 1;
	vert_div_c = 1;

	if (BytePerPixelY == 1)
		vert_div_l = 0;
	if (BytePerPixelC == 1)
		vert_div_c = 0;
	if (BytePerPixelY == 8 && (TilingFormat == dm_sw_64kb_s || TilingFormat == dm_sw_64kb_s_t || TilingFormat == dm_sw_64kb_s_x))
		horz_div_l = 0;
	if (BytePerPixelC == 8 && (TilingFormat == dm_sw_64kb_s || TilingFormat == dm_sw_64kb_s_t || TilingFormat == dm_sw_64kb_s_x))
		horz_div_c = 0;

	if (BytePerPixelC == 0) {
		swath_buf_size = DETBufferSize / 2 - 2 * 256;
		detile_buf_vp_horz_limit = (double) swath_buf_size / ((double) RequestHeight256ByteLuma * BytePerPixelY / (1 + horz_div_l));
		detile_buf_vp_vert_limit = (double) swath_buf_size / (256.0 / RequestHeight256ByteLuma / (1 + vert_div_l));
	} else {
		swath_buf_size = DETBufferSize / 2 - 2 * 2 * 256;
		detile_buf_vp_horz_limit = (double) swath_buf_size
				/ ((double) RequestHeight256ByteLuma * BytePerPixelY / (1 + horz_div_l)
						+ (double) RequestHeight256ByteChroma * BytePerPixelC / (1 + horz_div_c) / (1 + yuv420));
		detile_buf_vp_vert_limit = (double) swath_buf_size
				/ (256.0 / RequestHeight256ByteLuma / (1 + vert_div_l) + 256.0 / RequestHeight256ByteChroma / (1 + vert_div_c) / (1 + yuv420));
	}

	if (SourcePixelFormat == dm_420_10) {
		detile_buf_vp_horz_limit = 1.5 * detile_buf_vp_horz_limit;
		detile_buf_vp_vert_limit = 1.5 * detile_buf_vp_vert_limit;
	}

	detile_buf_vp_horz_limit = dml_floor(detile_buf_vp_horz_limit - 1, 16);
	detile_buf_vp_vert_limit = dml_floor(detile_buf_vp_vert_limit - 1, 16);

	MAS_vp_horz_limit = SourcePixelFormat == dm_rgbe_alpha ? 3840 : 5760;
	MAS_vp_vert_limit = (BytePerPixelC > 0 ? 2880 : 5760);
	max_vp_horz_width = dml_min((double) MAS_vp_horz_limit, detile_buf_vp_horz_limit);
	max_vp_vert_height = dml_min((double) MAS_vp_vert_limit, detile_buf_vp_vert_limit);
	eff_surf_width_l = (SurfaceWidthLuma > max_vp_horz_width ? max_vp_horz_width : SurfaceWidthLuma);
	eff_surf_width_c = eff_surf_width_l / (1 + yuv420);
	eff_surf_height_l = (SurfaceHeightLuma > max_vp_vert_height ? max_vp_vert_height : SurfaceHeightLuma);
	eff_surf_height_c = eff_surf_height_l / (1 + yuv420);

	full_swath_bytes_horz_wc_l = eff_surf_width_l * RequestHeight256ByteLuma * BytePerPixelY;
	full_swath_bytes_vert_wc_l = eff_surf_height_l * 256 / RequestHeight256ByteLuma;
	if (BytePerPixelC > 0) {
		full_swath_bytes_horz_wc_c = eff_surf_width_c * RequestHeight256ByteChroma * BytePerPixelC;
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
	} else if (full_swath_bytes_horz_wc_l < 1.5 * full_swath_bytes_horz_wc_c && 2 * full_swath_bytes_horz_wc_l + full_swath_bytes_horz_wc_c <= DETBufferSize) {
		req128_horz_wc_l = 0;
		req128_horz_wc_c = 1;
	} else if (full_swath_bytes_horz_wc_l >= 1.5 * full_swath_bytes_horz_wc_c && full_swath_bytes_horz_wc_l + 2 * full_swath_bytes_horz_wc_c <= DETBufferSize) {
		req128_horz_wc_l = 1;
		req128_horz_wc_c = 0;
	} else {
		req128_horz_wc_l = 1;
		req128_horz_wc_c = 1;
	}

	if (2 * full_swath_bytes_vert_wc_l + 2 * full_swath_bytes_vert_wc_c <= DETBufferSize) {
		req128_vert_wc_l = 0;
		req128_vert_wc_c = 0;
	} else if (full_swath_bytes_vert_wc_l < 1.5 * full_swath_bytes_vert_wc_c && 2 * full_swath_bytes_vert_wc_l + full_swath_bytes_vert_wc_c <= DETBufferSize) {
		req128_vert_wc_l = 0;
		req128_vert_wc_c = 1;
	} else if (full_swath_bytes_vert_wc_l >= 1.5 * full_swath_bytes_vert_wc_c && full_swath_bytes_vert_wc_l + 2 * full_swath_bytes_vert_wc_c <= DETBufferSize) {
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
	if ((BytePerPixelY == 8 && (TilingFormat == dm_sw_64kb_d || TilingFormat == dm_sw_64kb_d_x || TilingFormat == dm_sw_64kb_d_t || TilingFormat == dm_sw_64kb_r_x))
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
	if ((BytePerPixelC == 8 && (TilingFormat == dm_sw_64kb_d || TilingFormat == dm_sw_64kb_d_x || TilingFormat == dm_sw_64kb_d_t || TilingFormat == dm_sw_64kb_r_x))
			|| (BytePerPixelC == 4 && TilingFormat == dm_sw_64kb_r_x)) {
		segment_order_vert_contiguous_chroma = 0;
	} else {
		segment_order_vert_contiguous_chroma = 1;
	}

	if (DCCProgrammingAssumesScanDirectionUnknown == true) {
		if (req128_horz_wc_l == 0 && req128_vert_wc_l == 0) {
			RequestLuma = REQ_256Bytes;
		} else if ((req128_horz_wc_l == 1 && segment_order_horz_contiguous_luma == 0) || (req128_vert_wc_l == 1 && segment_order_vert_contiguous_luma == 0)) {
			RequestLuma = REQ_128BytesNonContiguous;
		} else {
			RequestLuma = REQ_128BytesContiguous;
		}
		if (req128_horz_wc_c == 0 && req128_vert_wc_c == 0) {
			RequestChroma = REQ_256Bytes;
		} else if ((req128_horz_wc_c == 1 && segment_order_horz_contiguous_chroma == 0) || (req128_vert_wc_c == 1 && segment_order_vert_contiguous_chroma == 0)) {
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
	struct vba_vars_st *v = &mode_lib->vba;
	unsigned int MaxPartialSwath;

	if (ProgressiveToInterlaceUnitInOPP)
		*VInitPreFill = dml_floor((VRatio + vtaps + 1) / 2.0, 1);
	else
		*VInitPreFill = dml_floor((VRatio + vtaps + 1 + Interlace * 0.5 * VRatio) / 2.0, 1);

	if (!v->IgnoreViewportPositioning) {

		*MaxNumSwath = dml_ceil((*VInitPreFill - 1.0) / SwathHeight, 1) + 1.0;

		if (*VInitPreFill > 1.0)
			MaxPartialSwath = (unsigned int) (*VInitPreFill - 2) % SwathHeight;
		else
			MaxPartialSwath = (unsigned int) (*VInitPreFill + SwathHeight - 2) % SwathHeight;
		MaxPartialSwath = dml_max(1U, MaxPartialSwath);

	} else {

		if (ViewportYStart != 0)
			dml_print("WARNING DML: using viewport y position of 0 even though actual viewport y position is non-zero in prefetch source lines calculation\n");

		*MaxNumSwath = dml_ceil(*VInitPreFill / SwathHeight, 1);

		if (*VInitPreFill > 1.0)
			MaxPartialSwath = (unsigned int) (*VInitPreFill - 1) % SwathHeight;
		else
			MaxPartialSwath = (unsigned int) (*VInitPreFill + SwathHeight - 1) % SwathHeight;
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: VRatio = %f\n", __func__, VRatio);
	dml_print("DML::%s: vtaps = %f\n", __func__, vtaps);
	dml_print("DML::%s: VInitPreFill = %f\n", __func__, *VInitPreFill);
	dml_print("DML::%s: ProgressiveToInterlaceUnitInOPP = %d\n", __func__, ProgressiveToInterlaceUnitInOPP);
	dml_print("DML::%s: IgnoreViewportPositioning = %d\n", __func__, v->IgnoreViewportPositioning);
	dml_print("DML::%s: SwathHeight = %d\n", __func__, SwathHeight);
	dml_print("DML::%s: MaxPartialSwath = %d\n", __func__, MaxPartialSwath);
	dml_print("DML::%s: MaxNumSwath = %d\n", __func__, *MaxNumSwath);
	dml_print("DML::%s: Prefetch source lines = %d\n", __func__, *MaxNumSwath * SwathHeight + MaxPartialSwath);
#endif
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
		int *dpte_row_width_ub,
		unsigned int *dpte_row_height,
		unsigned int *MetaRequestWidth,
		unsigned int *MetaRequestHeight,
		unsigned int *meta_row_width,
		unsigned int *meta_row_height,
		int *vm_group_bytes,
		unsigned int *dpte_group_bytes,
		unsigned int *PixelPTEReqWidth,
		unsigned int *PixelPTEReqHeight,
		unsigned int *PTERequestSize,
		int *DPDE0BytesFrame,
		int *MetaPTEBytesFrame)
{
	struct vba_vars_st *v = &mode_lib->vba;
	unsigned int MPDEBytesFrame;
	unsigned int DCCMetaSurfaceBytes;
	unsigned int MacroTileSizeBytes;
	unsigned int MacroTileHeight;
	unsigned int ExtraDPDEBytesFrame;
	unsigned int PDEAndMetaPTEBytesFrame;
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
		*meta_row_width = dml_ceil((double) SwathWidth - 1, *MetaRequestWidth) + *MetaRequestWidth;
		*MetaRowByte = *meta_row_width * *MetaRequestHeight * BytePerPixel / 256.0;
	} else {
		*meta_row_height = *MetaRequestWidth;
		*meta_row_width = dml_ceil((double) SwathWidth - 1, *MetaRequestHeight) + *MetaRequestHeight;
		*MetaRowByte = *meta_row_width * *MetaRequestWidth * BytePerPixel / 256.0;
	}
	DCCMetaSurfaceBytes = DCCMetaPitch * (dml_ceil(ViewportHeight - 1, 64 * BlockHeight256Bytes) + 64 * BlockHeight256Bytes) * BytePerPixel / 256;
	if (GPUVMEnable == true) {
		*MetaPTEBytesFrame = (dml_ceil((double) (DCCMetaSurfaceBytes - 4.0 * 1024.0) / (8 * 4.0 * 1024), 1) + 1) * 64;
		MPDEBytesFrame = 128 * (v->GPUVMMaxPageTableLevels - 1);
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

	if (GPUVMEnable == true && v->GPUVMMaxPageTableLevels > 1) {
		if (ScanDirection != dm_vert) {
			*DPDE0BytesFrame = 64
					* (dml_ceil(
							((Pitch * (dml_ceil(ViewportHeight - 1, MacroTileHeight) + MacroTileHeight) * BytePerPixel) - MacroTileSizeBytes)
									/ (8 * 2097152),
							1) + 1);
		} else {
			*DPDE0BytesFrame = 64
					* (dml_ceil(
							((Pitch * (dml_ceil((double) SwathWidth - 1, MacroTileHeight) + MacroTileHeight) * BytePerPixel) - MacroTileSizeBytes)
									/ (8 * 2097152),
							1) + 1);
		}
		ExtraDPDEBytesFrame = 128 * (v->GPUVMMaxPageTableLevels - 2);
	} else {
		*DPDE0BytesFrame = 0;
		ExtraDPDEBytesFrame = 0;
	}

	PDEAndMetaPTEBytesFrame = *MetaPTEBytesFrame + MPDEBytesFrame + *DPDE0BytesFrame + ExtraDPDEBytesFrame;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: MetaPTEBytesFrame = %d\n", __func__, *MetaPTEBytesFrame);
	dml_print("DML::%s: MPDEBytesFrame = %d\n", __func__, MPDEBytesFrame);
	dml_print("DML::%s: DPDE0BytesFrame = %d\n", __func__, *DPDE0BytesFrame);
	dml_print("DML::%s: ExtraDPDEBytesFrame= %d\n", __func__, ExtraDPDEBytesFrame);
	dml_print("DML::%s: PDEAndMetaPTEBytesFrame = %d\n", __func__, PDEAndMetaPTEBytesFrame);
#endif

	if (HostVMEnable == true) {
		PDEAndMetaPTEBytesFrame = PDEAndMetaPTEBytesFrame * (1 + 8 * HostVMDynamicLevels);
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: PDEAndMetaPTEBytesFrame = %d\n", __func__, PDEAndMetaPTEBytesFrame);
#endif

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
			FractionOfPTEReturnDrop = 7 / 8;
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
		*dpte_row_height = dml_min(128, 1 << (unsigned int) dml_floor(dml_log2(PTEBufferSizeInRequests * *PixelPTEReqWidth / Pitch), 1));
		*dpte_row_width_ub = (dml_ceil((double)(Pitch * *dpte_row_height - 1) / *PixelPTEReqWidth, 1) + 1) * *PixelPTEReqWidth;
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

	if (*PixelPTEBytesPerRow * (1 - FractionOfPTEReturnDrop) <= 64 * PTEBufferSizeInRequests) {
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

static void DISPCLKDPPCLKDCFCLKDeepSleepPrefetchParametersWatermarksAndPerformanceCalculation(struct display_mode_lib *mode_lib)
{
	struct vba_vars_st *v = &mode_lib->vba;
	unsigned int j, k;
	double HostVMInefficiencyFactor = 1.0;
	bool NoChromaPlanes = true;
	int ReorderBytes;
	double VMDataOnlyReturnBW;
	double MaxTotalRDBandwidth = 0;
	int PrefetchMode = v->PrefetchModePerState[v->VoltageLevel][v->maxMpcComb];

	v->WritebackDISPCLK = 0.0;
	v->DISPCLKWithRamping = 0;
	v->DISPCLKWithoutRamping = 0;
	v->GlobalDPPCLK = 0.0;
	/* DAL custom code: need to update ReturnBW in case min dcfclk is overridden */
	{
	double IdealFabricAndSDPPortBandwidthPerState = dml_min(
			v->ReturnBusWidth * v->DCFCLKState[v->VoltageLevel][v->maxMpcComb],
			v->FabricClockPerState[v->VoltageLevel] * v->FabricDatapathToDCNDataReturn);
	double IdealDRAMBandwidthPerState = v->DRAMSpeedPerState[v->VoltageLevel] * v->NumberOfChannels * v->DRAMChannelWidth;

	if (v->HostVMEnable != true) {
		v->ReturnBW = dml_min(
				IdealFabricAndSDPPortBandwidthPerState * v->PercentOfIdealFabricAndSDPPortBWReceivedAfterUrgLatency / 100.0,
				IdealDRAMBandwidthPerState * v->PercentOfIdealDRAMBWReceivedAfterUrgLatencyPixelDataOnly / 100.0);
	} else {
		v->ReturnBW = dml_min(
				IdealFabricAndSDPPortBandwidthPerState * v->PercentOfIdealFabricAndSDPPortBWReceivedAfterUrgLatency / 100.0,
				IdealDRAMBandwidthPerState * v->PercentOfIdealDRAMBWReceivedAfterUrgLatencyPixelMixedWithVMData / 100.0);
	}
	}
	/* End DAL custom code */

	// DISPCLK and DPPCLK Calculation
	//
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		if (v->WritebackEnable[k]) {
			v->WritebackDISPCLK = dml_max(
					v->WritebackDISPCLK,
					dml314_CalculateWriteBackDISPCLK(
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
			v->PSCL_THROUGHPUT_LUMA[k] = dml_min(
					v->MaxDCHUBToPSCLThroughput,
					v->MaxPSCLToLBThroughput * v->HRatio[k] / dml_ceil(v->htaps[k] / 6.0, 1));
		} else {
			v->PSCL_THROUGHPUT_LUMA[k] = dml_min(v->MaxDCHUBToPSCLThroughput, v->MaxPSCLToLBThroughput);
		}

		v->DPPCLKUsingSingleDPPLuma = v->PixelClock[k]
				* dml_max(
						v->vtaps[k] / 6.0 * dml_min(1.0, v->HRatio[k]),
						dml_max(v->HRatio[k] * v->VRatio[k] / v->PSCL_THROUGHPUT_LUMA[k], 1.0));

		if ((v->htaps[k] > 6 || v->vtaps[k] > 6) && v->DPPCLKUsingSingleDPPLuma < 2 * v->PixelClock[k]) {
			v->DPPCLKUsingSingleDPPLuma = 2 * v->PixelClock[k];
		}

		if ((v->SourcePixelFormat[k] != dm_420_8 && v->SourcePixelFormat[k] != dm_420_10 && v->SourcePixelFormat[k] != dm_420_12
				&& v->SourcePixelFormat[k] != dm_rgbe_alpha)) {
			v->PSCL_THROUGHPUT_CHROMA[k] = 0.0;
			v->DPPCLKUsingSingleDPP[k] = v->DPPCLKUsingSingleDPPLuma;
		} else {
			if (v->HRatioChroma[k] > 1) {
				v->PSCL_THROUGHPUT_CHROMA[k] = dml_min(
						v->MaxDCHUBToPSCLThroughput,
						v->MaxPSCLToLBThroughput * v->HRatioChroma[k] / dml_ceil(v->HTAPsChroma[k] / 6.0, 1.0));
			} else {
				v->PSCL_THROUGHPUT_CHROMA[k] = dml_min(v->MaxDCHUBToPSCLThroughput, v->MaxPSCLToLBThroughput);
			}
			v->DPPCLKUsingSingleDPPChroma = v->PixelClock[k]
					* dml_max3(
							v->VTAPsChroma[k] / 6.0 * dml_min(1.0, v->HRatioChroma[k]),
							v->HRatioChroma[k] * v->VRatioChroma[k] / v->PSCL_THROUGHPUT_CHROMA[k],
							1.0);

			if ((v->HTAPsChroma[k] > 6 || v->VTAPsChroma[k] > 6) && v->DPPCLKUsingSingleDPPChroma < 2 * v->PixelClock[k]) {
				v->DPPCLKUsingSingleDPPChroma = 2 * v->PixelClock[k];
			}

			v->DPPCLKUsingSingleDPP[k] = dml_max(v->DPPCLKUsingSingleDPPLuma, v->DPPCLKUsingSingleDPPChroma);
		}
	}

	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		if (v->BlendingAndTiming[k] != k)
			continue;
		if (v->ODMCombineEnabled[k] == dm_odm_combine_mode_4to1) {
			v->DISPCLKWithRamping = dml_max(
					v->DISPCLKWithRamping,
					v->PixelClock[k] / 4 * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100)
							* (1 + v->DISPCLKRampingMargin / 100));
			v->DISPCLKWithoutRamping = dml_max(
					v->DISPCLKWithoutRamping,
					v->PixelClock[k] / 4 * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100));
		} else if (v->ODMCombineEnabled[k] == dm_odm_combine_mode_2to1) {
			v->DISPCLKWithRamping = dml_max(
					v->DISPCLKWithRamping,
					v->PixelClock[k] / 2 * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100)
							* (1 + v->DISPCLKRampingMargin / 100));
			v->DISPCLKWithoutRamping = dml_max(
					v->DISPCLKWithoutRamping,
					v->PixelClock[k] / 2 * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100));
		} else {
			v->DISPCLKWithRamping = dml_max(
					v->DISPCLKWithRamping,
					v->PixelClock[k] * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100) * (1 + v->DISPCLKRampingMargin / 100));
			v->DISPCLKWithoutRamping = dml_max(
					v->DISPCLKWithoutRamping,
					v->PixelClock[k] * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100));
		}
	}

	v->DISPCLKWithRamping = dml_max(v->DISPCLKWithRamping, v->WritebackDISPCLK);
	v->DISPCLKWithoutRamping = dml_max(v->DISPCLKWithoutRamping, v->WritebackDISPCLK);

	ASSERT(v->DISPCLKDPPCLKVCOSpeed != 0);
	v->DISPCLKWithRampingRoundedToDFSGranularity = RoundToDFSGranularityUp(v->DISPCLKWithRamping, v->DISPCLKDPPCLKVCOSpeed);
	v->DISPCLKWithoutRampingRoundedToDFSGranularity = RoundToDFSGranularityUp(v->DISPCLKWithoutRamping, v->DISPCLKDPPCLKVCOSpeed);
	v->MaxDispclkRoundedToDFSGranularity = RoundToDFSGranularityDown(
			v->soc.clock_limits[v->soc.num_states - 1].dispclk_mhz,
			v->DISPCLKDPPCLKVCOSpeed);
	if (v->DISPCLKWithoutRampingRoundedToDFSGranularity > v->MaxDispclkRoundedToDFSGranularity) {
		v->DISPCLK_calculated = v->DISPCLKWithoutRampingRoundedToDFSGranularity;
	} else if (v->DISPCLKWithRampingRoundedToDFSGranularity > v->MaxDispclkRoundedToDFSGranularity) {
		v->DISPCLK_calculated = v->MaxDispclkRoundedToDFSGranularity;
	} else {
		v->DISPCLK_calculated = v->DISPCLKWithRampingRoundedToDFSGranularity;
	}
	v->DISPCLK = v->DISPCLK_calculated;
	DTRACE("   dispclk_mhz (calculated) = %f", v->DISPCLK_calculated);

	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		v->DPPCLK_calculated[k] = v->DPPCLKUsingSingleDPP[k] / v->DPPPerPlane[k] * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100);
		v->GlobalDPPCLK = dml_max(v->GlobalDPPCLK, v->DPPCLK_calculated[k]);
	}
	v->GlobalDPPCLK = RoundToDFSGranularityUp(v->GlobalDPPCLK, v->DISPCLKDPPCLKVCOSpeed);
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		v->DPPCLK_calculated[k] = v->GlobalDPPCLK / 255 * dml_ceil(v->DPPCLK_calculated[k] * 255.0 / v->GlobalDPPCLK, 1);
		DTRACE("   dppclk_mhz[%i] (calculated) = %f", k, v->DPPCLK_calculated[k]);
	}

	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		v->DPPCLK[k] = v->DPPCLK_calculated[k];
	}

	// Urgent and B P-State/DRAM Clock Change Watermark
	DTRACE("   dcfclk_mhz         = %f", v->DCFCLK);
	DTRACE("   return_bus_bw      = %f", v->ReturnBW);

	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		CalculateBytePerPixelAnd256BBlockSizes(
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
		v->ReadBandwidthPlaneLuma[k] = v->SwathWidthSingleDPPY[k] * v->BytePerPixelY[k] / (v->HTotal[k] / v->PixelClock[k])
				* v->VRatio[k];
		v->ReadBandwidthPlaneChroma[k] = v->SwathWidthSingleDPPC[k] * v->BytePerPixelC[k] / (v->HTotal[k] / v->PixelClock[k])
				* v->VRatioChroma[k];
		DTRACE("   read_bw[%i] = %fBps", k, v->ReadBandwidthPlaneLuma[k] + v->ReadBandwidthPlaneChroma[k]);
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
				v->DSCCLK_calculated[k] = v->PixelClockBackEnd[k] / 12 / v->DSCFormatFactor
						/ (1 - v->DISPCLKDPPCLKDSCCLKDownSpreading / 100);
			else if (v->ODMCombineEnabled[k] == dm_odm_combine_mode_2to1)
				v->DSCCLK_calculated[k] = v->PixelClockBackEnd[k] / 6 / v->DSCFormatFactor
						/ (1 - v->DISPCLKDPPCLKDSCCLKDownSpreading / 100);
			else
				v->DSCCLK_calculated[k] = v->PixelClockBackEnd[k] / 3 / v->DSCFormatFactor
						/ (1 - v->DISPCLKDPPCLKDSCCLKDownSpreading / 100);
		}
	}

	// DSC Delay
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		double BPP = v->OutputBpp[k];

		if (v->DSCEnabled[k] && BPP != 0) {
			if (v->ODMCombineEnabled[k] == dm_odm_combine_mode_disabled) {
				v->DSCDelay[k] = dscceComputeDelay(
						v->DSCInputBitPerComponent[k],
						BPP,
						dml_ceil((double) v->HActive[k] / v->NumberOfDSCSlices[k], 1),
						v->NumberOfDSCSlices[k],
						v->OutputFormat[k],
						v->Output[k]) + dscComputeDelay(v->OutputFormat[k], v->Output[k]);
			} else if (v->ODMCombineEnabled[k] == dm_odm_combine_mode_2to1) {
				v->DSCDelay[k] = 2
						* (dscceComputeDelay(
								v->DSCInputBitPerComponent[k],
								BPP,
								dml_ceil((double) v->HActive[k] / v->NumberOfDSCSlices[k], 1),
								v->NumberOfDSCSlices[k] / 2.0,
								v->OutputFormat[k],
								v->Output[k]) + dscComputeDelay(v->OutputFormat[k], v->Output[k]));
			} else {
				v->DSCDelay[k] = 4
						* (dscceComputeDelay(
								v->DSCInputBitPerComponent[k],
								BPP,
								dml_ceil((double) v->HActive[k] / v->NumberOfDSCSlices[k], 1),
								v->NumberOfDSCSlices[k] / 4.0,
								v->OutputFormat[k],
								v->Output[k]) + dscComputeDelay(v->OutputFormat[k], v->Output[k]));
			}
			v->DSCDelay[k] = v->DSCDelay[k] * v->PixelClock[k] / v->PixelClockBackEnd[k];
		} else {
			v->DSCDelay[k] = 0;
		}
	}

	for (k = 0; k < v->NumberOfActivePlanes; ++k)
		for (j = 0; j < v->NumberOfActivePlanes; ++j) // NumberOfPlanes
			if (j != k && v->BlendingAndTiming[k] == j && v->DSCEnabled[j])
				v->DSCDelay[k] = v->DSCDelay[j];

	// Prefetch
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		unsigned int PDEAndMetaPTEBytesFrameY;
		unsigned int PixelPTEBytesPerRowY;
		unsigned int MetaRowByteY;
		unsigned int MetaRowByteC;
		unsigned int PDEAndMetaPTEBytesFrameC;
		unsigned int PixelPTEBytesPerRowC;
		bool PTEBufferSizeNotExceededY;
		bool PTEBufferSizeNotExceededC;

		if (v->SourcePixelFormat[k] == dm_420_8 || v->SourcePixelFormat[k] == dm_420_10 || v->SourcePixelFormat[k] == dm_420_12
				|| v->SourcePixelFormat[k] == dm_rgbe_alpha) {
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
		v->PDEAndMetaPTEBytesFrame[k] = PDEAndMetaPTEBytesFrameY + PDEAndMetaPTEBytesFrameC;
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
		v->TotalActiveDPP = v->TotalActiveDPP + v->DPPPerPlane[k];
		if (v->DCCEnable[k])
			v->TotalDCCActiveDPP = v->TotalDCCActiveDPP + v->DPPPerPlane[k];
		if (v->SourcePixelFormat[k] == dm_420_8 || v->SourcePixelFormat[k] == dm_420_10 || v->SourcePixelFormat[k] == dm_420_12
				|| v->SourcePixelFormat[k] == dm_rgbe_alpha)
			NoChromaPlanes = false;
	}

	ReorderBytes = v->NumberOfChannels
			* dml_max3(
					v->UrgentOutOfOrderReturnPerChannelPixelDataOnly,
					v->UrgentOutOfOrderReturnPerChannelPixelMixedWithVMData,
					v->UrgentOutOfOrderReturnPerChannelVMDataOnly);

	VMDataOnlyReturnBW = dml_min(
			dml_min(v->ReturnBusWidth * v->DCFCLK, v->FabricClock * v->FabricDatapathToDCNDataReturn)
					* v->PercentOfIdealFabricAndSDPPortBWReceivedAfterUrgLatency / 100.0,
			v->DRAMSpeed * v->NumberOfChannels * v->DRAMChannelWidth
					* v->PercentOfIdealDRAMBWReceivedAfterUrgLatencyVMDataOnly / 100.0);

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: v->ReturnBusWidth = %f\n", __func__, v->ReturnBusWidth);
	dml_print("DML::%s: v->DCFCLK = %f\n", __func__, v->DCFCLK);
	dml_print("DML::%s: v->FabricClock = %f\n", __func__, v->FabricClock);
	dml_print("DML::%s: v->FabricDatapathToDCNDataReturn = %f\n", __func__, v->FabricDatapathToDCNDataReturn);
	dml_print("DML::%s: v->PercentOfIdealFabricAndSDPPortBWReceivedAfterUrgLatency = %f\n", __func__, v->PercentOfIdealFabricAndSDPPortBWReceivedAfterUrgLatency);
	dml_print("DML::%s: v->DRAMSpeed = %f\n", __func__, v->DRAMSpeed);
	dml_print("DML::%s: v->NumberOfChannels = %f\n", __func__, v->NumberOfChannels);
	dml_print("DML::%s: v->DRAMChannelWidth = %f\n", __func__, v->DRAMChannelWidth);
	dml_print("DML::%s: v->PercentOfIdealDRAMBWReceivedAfterUrgLatencyVMDataOnly = %f\n", __func__, v->PercentOfIdealDRAMBWReceivedAfterUrgLatencyVMDataOnly);
	dml_print("DML::%s: VMDataOnlyReturnBW = %f\n", __func__, VMDataOnlyReturnBW);
	dml_print("DML::%s: ReturnBW = %f\n", __func__, v->ReturnBW);
#endif

	if (v->GPUVMEnable && v->HostVMEnable)
		HostVMInefficiencyFactor = v->ReturnBW / VMDataOnlyReturnBW;

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
			HostVMInefficiencyFactor,
			v->HostVMMinPageSize,
			v->HostVMMaxNonCachedPageTableLevels);

	v->TCalc = 24.0 / v->DCFCLKDeepSleep;

	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		if (v->BlendingAndTiming[k] == k) {
			if (v->WritebackEnable[k] == true) {
				v->WritebackDelay[v->VoltageLevel][k] = v->WritebackLatency
						+ CalculateWriteBackDelay(
								v->WritebackPixelFormat[k],
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
				if (v->BlendingAndTiming[j] == k && v->WritebackEnable[j] == true) {
					v->WritebackDelay[v->VoltageLevel][k] = dml_max(
							v->WritebackDelay[v->VoltageLevel][k],
							v->WritebackLatency
									+ CalculateWriteBackDelay(
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
		v->MaxVStartupLines[k] =
			CalculateMaxVStartup(
					v->VTotal[k],
					v->VActive[k],
					v->VBlankNom[k],
					v->HTotal[k],
					v->PixelClock[k],
					v->ProgressiveToInterlaceUnitInOPP,
					v->Interlace[k],
					v->ip.VBlankNomDefaultUS,
					v->WritebackDelay[v->VoltageLevel][k]);

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d MaxVStartupLines = %d\n", __func__, k, v->MaxVStartupLines[k]);
		dml_print("DML::%s: k=%d VoltageLevel = %d\n", __func__, k, v->VoltageLevel);
		dml_print("DML::%s: k=%d WritebackDelay = %f\n", __func__, k, v->WritebackDelay[v->VoltageLevel][k]);
#endif
	}

	v->MaximumMaxVStartupLines = 0;
	for (k = 0; k < v->NumberOfActivePlanes; ++k)
		v->MaximumMaxVStartupLines = dml_max(v->MaximumMaxVStartupLines, v->MaxVStartupLines[k]);

	// VBA_DELTA
	// We don't really care to iterate between the various prefetch modes
	//v->PrefetchERROR = CalculateMinAndMaxPrefetchMode(v->AllowDRAMSelfRefreshOrDRAMClockChangeInVblank, &v->MinPrefetchMode, &v->MaxPrefetchMode);

	v->UrgentLatency = CalculateUrgentLatency(
			v->UrgentLatencyPixelDataOnly,
			v->UrgentLatencyPixelMixedWithVMData,
			v->UrgentLatencyVMDataOnly,
			v->DoUrgentLatencyAdjustment,
			v->UrgentLatencyAdjustmentFabricClockComponent,
			v->UrgentLatencyAdjustmentFabricClockReference,
			v->FabricClock);

	v->FractionOfUrgentBandwidth = 0.0;
	v->FractionOfUrgentBandwidthImmediateFlip = 0.0;

	v->VStartupLines = __DML_VBA_MIN_VSTARTUP__;

	do {
		double MaxTotalRDBandwidthNoUrgentBurst = 0.0;
		bool DestinationLineTimesForPrefetchLessThan2 = false;
		bool VRatioPrefetchMoreThan4 = false;
		double TWait = CalculateTWait(PrefetchMode, v->DRAMClockChangeLatency, v->UrgentLatency, v->SREnterPlusExitTime);

		MaxTotalRDBandwidth = 0;

		dml_print("DML::%s: Start loop: VStartup = %d\n", __func__, v->VStartupLines);

		for (k = 0; k < v->NumberOfActivePlanes; ++k) {
			Pipe myPipe;

			myPipe.DPPCLK = v->DPPCLK[k];
			myPipe.DISPCLK = v->DISPCLK;
			myPipe.PixelClock = v->PixelClock[k];
			myPipe.DCFCLKDeepSleep = v->DCFCLKDeepSleep;
			myPipe.DPPPerPlane = v->DPPPerPlane[k];
			myPipe.ScalerEnabled = v->ScalerEnabled[k];
			myPipe.VRatio = v->VRatio[k];
			myPipe.VRatioChroma = v->VRatioChroma[k];
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
			myPipe.ODMCombineIsEnabled = v->ODMCombineEnabled[k] == dm_odm_combine_mode_4to1
					|| v->ODMCombineEnabled[k] == dm_odm_combine_mode_2to1;
			myPipe.SourcePixelFormat = v->SourcePixelFormat[k];
			myPipe.BytePerPixelY = v->BytePerPixelY[k];
			myPipe.BytePerPixelC = v->BytePerPixelC[k];
			myPipe.ProgressiveToInterlaceUnitInOPP = v->ProgressiveToInterlaceUnitInOPP;
			v->ErrorResult[k] = CalculatePrefetchSchedule(
					mode_lib,
					HostVMInefficiencyFactor,
					&myPipe,
					v->DSCDelay[k],
					v->DPPCLKDelaySubtotal + v->DPPCLKDelayCNVCFormater,
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
					v->VInitPreFillY[k],
					v->MaxNumSwathY[k],
					v->PrefetchSourceLinesC[k],
					v->SwathWidthC[k],
					v->VInitPreFillC[k],
					v->MaxNumSwathC[k],
					v->swath_width_luma_ub[k],
					v->swath_width_chroma_ub[k],
					v->SwathHeightY[k],
					v->SwathHeightC[k],
					TWait,
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
					&v->TSetup[k],
					&v->VUpdateOffsetPix[k],
					&v->VUpdateWidthPix[k],
					&v->VReadyOffsetPix[k]);

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%0d Prefetch cal result=%0d\n", __func__, k, v->ErrorResult[k]);
#endif
			v->VStartup[k] = dml_min(v->VStartupLines, v->MaxVStartupLines[k]);
		}

		v->NoEnoughUrgentLatencyHiding = false;
		v->NoEnoughUrgentLatencyHidingPre = false;

		for (k = 0; k < v->NumberOfActivePlanes; ++k) {
			v->cursor_bw[k] = v->NumberOfCursors[k] * v->CursorWidth[k][0] * v->CursorBPP[k][0] / 8.0
					/ (v->HTotal[k] / v->PixelClock[k]) * v->VRatio[k];
			v->cursor_bw_pre[k] = v->NumberOfCursors[k] * v->CursorWidth[k][0] * v->CursorBPP[k][0] / 8.0
					/ (v->HTotal[k] / v->PixelClock[k]) * v->VRatioPrefetchY[k];

			CalculateUrgentBurstFactor(
					v->swath_width_luma_ub[k],
					v->swath_width_chroma_ub[k],
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
					&v->UrgBurstFactorCursor[k],
					&v->UrgBurstFactorLuma[k],
					&v->UrgBurstFactorChroma[k],
					&v->NoUrgentLatencyHiding[k]);

			CalculateUrgentBurstFactor(
					v->swath_width_luma_ub[k],
					v->swath_width_chroma_ub[k],
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
					&v->UrgBurstFactorCursorPre[k],
					&v->UrgBurstFactorLumaPre[k],
					&v->UrgBurstFactorChromaPre[k],
					&v->NoUrgentLatencyHidingPre[k]);

			MaxTotalRDBandwidth = MaxTotalRDBandwidth
					+ dml_max3(
							v->DPPPerPlane[k] * v->prefetch_vmrow_bw[k],
							v->ReadBandwidthPlaneLuma[k] * v->UrgBurstFactorLuma[k]
									+ v->ReadBandwidthPlaneChroma[k] * v->UrgBurstFactorChroma[k]
									+ v->cursor_bw[k] * v->UrgBurstFactorCursor[k]
									+ v->DPPPerPlane[k] * (v->meta_row_bw[k] + v->dpte_row_bw[k]),
							v->DPPPerPlane[k]
									* (v->RequiredPrefetchPixDataBWLuma[k] * v->UrgBurstFactorLumaPre[k]
											+ v->RequiredPrefetchPixDataBWChroma[k] * v->UrgBurstFactorChromaPre[k])
									+ v->cursor_bw_pre[k] * v->UrgBurstFactorCursorPre[k]);

			MaxTotalRDBandwidthNoUrgentBurst = MaxTotalRDBandwidthNoUrgentBurst
					+ dml_max3(
							v->DPPPerPlane[k] * v->prefetch_vmrow_bw[k],
							v->ReadBandwidthPlaneLuma[k] + v->ReadBandwidthPlaneChroma[k] + v->cursor_bw[k]
									+ v->DPPPerPlane[k] * (v->meta_row_bw[k] + v->dpte_row_bw[k]),
							v->DPPPerPlane[k] * (v->RequiredPrefetchPixDataBWLuma[k] + v->RequiredPrefetchPixDataBWChroma[k])
									+ v->cursor_bw_pre[k]);

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%0d DPPPerPlane=%d\n", __func__, k, v->DPPPerPlane[k]);
			dml_print("DML::%s: k=%0d UrgBurstFactorLuma=%f\n", __func__, k, v->UrgBurstFactorLuma[k]);
			dml_print("DML::%s: k=%0d UrgBurstFactorChroma=%f\n", __func__, k, v->UrgBurstFactorChroma[k]);
			dml_print("DML::%s: k=%0d UrgBurstFactorLumaPre=%f\n", __func__, k, v->UrgBurstFactorLumaPre[k]);
			dml_print("DML::%s: k=%0d UrgBurstFactorChromaPre=%f\n", __func__, k, v->UrgBurstFactorChromaPre[k]);

			dml_print("DML::%s: k=%0d VRatioPrefetchY=%f\n", __func__, k, v->VRatioPrefetchY[k]);
			dml_print("DML::%s: k=%0d VRatioY=%f\n", __func__, k, v->VRatio[k]);

			dml_print("DML::%s: k=%0d prefetch_vmrow_bw=%f\n", __func__, k, v->prefetch_vmrow_bw[k]);
			dml_print("DML::%s: k=%0d ReadBandwidthPlaneLuma=%f\n", __func__, k, v->ReadBandwidthPlaneLuma[k]);
			dml_print("DML::%s: k=%0d ReadBandwidthPlaneChroma=%f\n", __func__, k, v->ReadBandwidthPlaneChroma[k]);
			dml_print("DML::%s: k=%0d cursor_bw=%f\n", __func__, k, v->cursor_bw[k]);
			dml_print("DML::%s: k=%0d meta_row_bw=%f\n", __func__, k, v->meta_row_bw[k]);
			dml_print("DML::%s: k=%0d dpte_row_bw=%f\n", __func__, k, v->dpte_row_bw[k]);
			dml_print("DML::%s: k=%0d RequiredPrefetchPixDataBWLuma=%f\n", __func__, k, v->RequiredPrefetchPixDataBWLuma[k]);
			dml_print("DML::%s: k=%0d RequiredPrefetchPixDataBWChroma=%f\n", __func__, k, v->RequiredPrefetchPixDataBWChroma[k]);
			dml_print("DML::%s: k=%0d cursor_bw_pre=%f\n", __func__, k, v->cursor_bw_pre[k]);
			dml_print("DML::%s: k=%0d MaxTotalRDBandwidthNoUrgentBurst=%f\n", __func__, k, MaxTotalRDBandwidthNoUrgentBurst);
#endif

			if (v->DestinationLinesForPrefetch[k] < 2)
				DestinationLineTimesForPrefetchLessThan2 = true;

			if (v->VRatioPrefetchY[k] > 4 || v->VRatioPrefetchC[k] > 4)
				VRatioPrefetchMoreThan4 = true;

			if (v->NoUrgentLatencyHiding[k] == true)
				v->NoEnoughUrgentLatencyHiding = true;

			if (v->NoUrgentLatencyHidingPre[k] == true)
				v->NoEnoughUrgentLatencyHidingPre = true;
		}

		v->FractionOfUrgentBandwidth = MaxTotalRDBandwidthNoUrgentBurst / v->ReturnBW;

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: MaxTotalRDBandwidthNoUrgentBurst=%f\n", __func__, MaxTotalRDBandwidthNoUrgentBurst);
		dml_print("DML::%s: ReturnBW=%f\n", __func__, v->ReturnBW);
		dml_print("DML::%s: FractionOfUrgentBandwidth=%f\n", __func__, v->FractionOfUrgentBandwidth);
#endif

		if (MaxTotalRDBandwidth <= v->ReturnBW && v->NoEnoughUrgentLatencyHiding == 0 && v->NoEnoughUrgentLatencyHidingPre == 0
				&& !VRatioPrefetchMoreThan4 && !DestinationLineTimesForPrefetchLessThan2)
			v->PrefetchModeSupported = true;
		else {
			v->PrefetchModeSupported = false;
			dml_print("DML::%s: ***failed***. Bandwidth violation. Results are NOT valid\n", __func__);
			dml_print("DML::%s: MaxTotalRDBandwidth:%f AvailReturnBandwidth:%f\n", __func__, MaxTotalRDBandwidth, v->ReturnBW);
			dml_print("DML::%s: VRatioPrefetch %s more than 4\n", __func__, (VRatioPrefetchMoreThan4) ? "is" : "is not");
			dml_print("DML::%s: DestinationLines for Prefetch %s less than 2\n", __func__, (DestinationLineTimesForPrefetchLessThan2) ? "is" : "is not");
		}

		// PREVIOUS_ERROR
		// This error result check was done after the PrefetchModeSupported. So we will
		// still try to calculate flip schedule even prefetch mode not supported
		for (k = 0; k < v->NumberOfActivePlanes; ++k) {
			if (v->ErrorResult[k] == true || v->NotEnoughTimeForDynamicMetadata[k] == true) {
				v->PrefetchModeSupported = false;
				dml_print("DML::%s: ***failed***. Prefetch schedule violation. Results are NOT valid\n", __func__);
			}
		}

		if (v->PrefetchModeSupported == true && v->ImmediateFlipSupport == true) {
			v->BandwidthAvailableForImmediateFlip = v->ReturnBW;
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				v->BandwidthAvailableForImmediateFlip = v->BandwidthAvailableForImmediateFlip
						- dml_max(
								v->ReadBandwidthPlaneLuma[k] * v->UrgBurstFactorLuma[k]
										+ v->ReadBandwidthPlaneChroma[k] * v->UrgBurstFactorChroma[k]
										+ v->cursor_bw[k] * v->UrgBurstFactorCursor[k],
								v->DPPPerPlane[k]
										* (v->RequiredPrefetchPixDataBWLuma[k] * v->UrgBurstFactorLumaPre[k]
												+ v->RequiredPrefetchPixDataBWChroma[k] * v->UrgBurstFactorChromaPre[k])
										+ v->cursor_bw_pre[k] * v->UrgBurstFactorCursorPre[k]);
			}

			v->TotImmediateFlipBytes = 0;
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				v->TotImmediateFlipBytes = v->TotImmediateFlipBytes
						+ v->DPPPerPlane[k] * (v->PDEAndMetaPTEBytesFrame[k] + v->MetaRowByte[k] + v->PixelPTEBytesPerRow[k]);
			}
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				CalculateFlipSchedule(
						mode_lib,
						k,
						HostVMInefficiencyFactor,
						v->UrgentExtraLatency,
						v->UrgentLatency,
						v->PDEAndMetaPTEBytesFrame[k],
						v->MetaRowByte[k],
						v->PixelPTEBytesPerRow[k]);
			}

			v->total_dcn_read_bw_with_flip = 0.0;
			v->total_dcn_read_bw_with_flip_no_urgent_burst = 0.0;
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				v->total_dcn_read_bw_with_flip = v->total_dcn_read_bw_with_flip
						+ dml_max3(
								v->DPPPerPlane[k] * v->prefetch_vmrow_bw[k],
								v->DPPPerPlane[k] * v->final_flip_bw[k]
										+ v->ReadBandwidthLuma[k] * v->UrgBurstFactorLuma[k]
										+ v->ReadBandwidthChroma[k] * v->UrgBurstFactorChroma[k]
										+ v->cursor_bw[k] * v->UrgBurstFactorCursor[k],
								v->DPPPerPlane[k]
										* (v->final_flip_bw[k]
												+ v->RequiredPrefetchPixDataBWLuma[k] * v->UrgBurstFactorLumaPre[k]
												+ v->RequiredPrefetchPixDataBWChroma[k] * v->UrgBurstFactorChromaPre[k])
										+ v->cursor_bw_pre[k] * v->UrgBurstFactorCursorPre[k]);
				v->total_dcn_read_bw_with_flip_no_urgent_burst = v->total_dcn_read_bw_with_flip_no_urgent_burst
						+ dml_max3(
								v->DPPPerPlane[k] * v->prefetch_vmrow_bw[k],
								v->DPPPerPlane[k] * v->final_flip_bw[k] + v->ReadBandwidthPlaneLuma[k]
										+ v->ReadBandwidthPlaneChroma[k] + v->cursor_bw[k],
								v->DPPPerPlane[k]
										* (v->final_flip_bw[k] + v->RequiredPrefetchPixDataBWLuma[k]
												+ v->RequiredPrefetchPixDataBWChroma[k]) + v->cursor_bw_pre[k]);
			}
			v->FractionOfUrgentBandwidthImmediateFlip = v->total_dcn_read_bw_with_flip_no_urgent_burst / v->ReturnBW;

			v->ImmediateFlipSupported = true;
			if (v->total_dcn_read_bw_with_flip > v->ReturnBW) {
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: total_dcn_read_bw_with_flip %f (bw w/ flip too high!)\n", __func__, v->total_dcn_read_bw_with_flip);
#endif
				v->ImmediateFlipSupported = false;
				v->total_dcn_read_bw_with_flip = MaxTotalRDBandwidth;
			}
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				if (v->ImmediateFlipSupportedForPipe[k] == false) {
#ifdef __DML_VBA_DEBUG__
					dml_print("DML::%s: Pipe %0d not supporting iflip\n", __func__, k);
#endif
					v->ImmediateFlipSupported = false;
				}
			}
		} else {
			v->ImmediateFlipSupported = false;
		}

		v->PrefetchAndImmediateFlipSupported =
				(v->PrefetchModeSupported == true && ((!v->ImmediateFlipSupport && !v->HostVMEnable
				&& v->ImmediateFlipRequirement[0] != dm_immediate_flip_required) ||
				v->ImmediateFlipSupported)) ? true : false;
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: PrefetchModeSupported %d\n", __func__, v->PrefetchModeSupported);
		dml_print("DML::%s: ImmediateFlipRequirement %d\n", __func__, v->ImmediateFlipRequirement == dm_immediate_flip_required);
		dml_print("DML::%s: ImmediateFlipSupported %d\n", __func__, v->ImmediateFlipSupported);
		dml_print("DML::%s: ImmediateFlipSupport %d\n", __func__, v->ImmediateFlipSupport);
		dml_print("DML::%s: HostVMEnable %d\n", __func__, v->HostVMEnable);
		dml_print("DML::%s: PrefetchAndImmediateFlipSupported %d\n", __func__, v->PrefetchAndImmediateFlipSupported);
#endif
		dml_print("DML::%s: Done loop: Vstartup=%d, Max Vstartup is %d\n", __func__, v->VStartupLines, v->MaximumMaxVStartupLines);

		v->VStartupLines = v->VStartupLines + 1;
	} while (!v->PrefetchAndImmediateFlipSupported && v->VStartupLines <= v->MaximumMaxVStartupLines);
	ASSERT(v->PrefetchAndImmediateFlipSupported);

	// Unbounded Request Enabled
	CalculateUnboundedRequestAndCompressedBufferSize(
			v->DETBufferSizeInKByte[0],
			v->ConfigReturnBufferSizeInKByte,
			v->UseUnboundedRequesting,
			v->TotalActiveDPP,
			NoChromaPlanes,
			v->MaxNumDPP,
			v->CompressedBufferSegmentSizeInkByte,
			v->Output,
			&v->UnboundedRequestEnabled,
			&v->CompressedBufferSizeInkByte);

	//Watermarks and NB P-State/DRAM Clock Change Support
	{
		enum clock_change_support DRAMClockChangeSupport; // dummy

		CalculateWatermarksAndDRAMSpeedChangeSupport(
				mode_lib,
				PrefetchMode,
				v->DCFCLK,
				v->ReturnBW,
				v->UrgentLatency,
				v->UrgentExtraLatency,
				v->SOCCLK,
				v->DCFCLKDeepSleep,
				v->DETBufferSizeY,
				v->DETBufferSizeC,
				v->SwathHeightY,
				v->SwathHeightC,
				v->SwathWidthY,
				v->SwathWidthC,
				v->DPPPerPlane,
				v->BytePerPixelDETY,
				v->BytePerPixelDETC,
				v->UnboundedRequestEnabled,
				v->CompressedBufferSizeInkByte,
				&DRAMClockChangeSupport,
				&v->StutterExitWatermark,
				&v->StutterEnterPlusExitWatermark,
				&v->Z8StutterExitWatermark,
				&v->Z8StutterEnterPlusExitWatermark);

		for (k = 0; k < v->NumberOfActivePlanes; ++k) {
			if (v->WritebackEnable[k] == true) {
				v->WritebackAllowDRAMClockChangeEndPosition[k] = dml_max(
						0,
						v->VStartup[k] * v->HTotal[k] / v->PixelClock[k] - v->WritebackDRAMClockChangeWatermark);
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
					dml_max(v->StutterEnterPlusExitWatermark, v->UrgentWatermark));
		} else if (PrefetchMode == 1) {
			v->AllowDRAMClockChangeDuringVBlank[k] = false;
			v->AllowDRAMSelfRefreshDuringVBlank[k] = true;
			v->MinTTUVBlank[k] = dml_max(v->StutterEnterPlusExitWatermark, v->UrgentWatermark);
		} else {
			v->AllowDRAMClockChangeDuringVBlank[k] = false;
			v->AllowDRAMSelfRefreshDuringVBlank[k] = false;
			v->MinTTUVBlank[k] = v->UrgentWatermark;
		}
		if (!v->DynamicMetadataEnable[k])
			v->MinTTUVBlank[k] = v->TCalc + v->MinTTUVBlank[k];
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

	// VStartup Adjustment
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		bool isInterlaceTiming;
		double Tvstartup_margin = (v->MaxVStartupLines[k] - v->VStartup[k]) * v->HTotal[k] / v->PixelClock[k];
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d, MinTTUVBlank = %f (before margin)\n", __func__, k, v->MinTTUVBlank[k]);
#endif

		v->MinTTUVBlank[k] = v->MinTTUVBlank[k] + Tvstartup_margin;

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d, Tvstartup_margin = %f\n", __func__, k, Tvstartup_margin);
		dml_print("DML::%s: k=%d, MaxVStartupLines = %d\n", __func__, k, v->MaxVStartupLines[k]);
		dml_print("DML::%s: k=%d, VStartup = %d\n", __func__, k, v->VStartup[k]);
		dml_print("DML::%s: k=%d, MinTTUVBlank = %f\n", __func__, k, v->MinTTUVBlank[k]);
#endif

		v->Tdmdl[k] = v->Tdmdl[k] + Tvstartup_margin;
		if (v->DynamicMetadataEnable[k] && v->DynamicMetadataVMEnabled) {
			v->Tdmdl_vm[k] = v->Tdmdl_vm[k] + Tvstartup_margin;
		}

		isInterlaceTiming = (v->Interlace[k] && !v->ProgressiveToInterlaceUnitInOPP);
		v->VStartup[k] = (isInterlaceTiming ? (2 * v->MaxVStartupLines[k]) : v->MaxVStartupLines[k]);
		if (v->Interlace[k] && !v->ProgressiveToInterlaceUnitInOPP) {
			v->MIN_DST_Y_NEXT_START[k] = dml_floor((v->VTotal[k] - v->VFrontPorch[k] + v->VTotal[k] - v->VActive[k] - v->VStartup[k]) / 2.0, 1.0);
		} else {
			v->MIN_DST_Y_NEXT_START[k] = v->VTotal[k] - v->VFrontPorch[k] + v->VTotal[k] - v->VActive[k] - v->VStartup[k];
		}
		v->MIN_DST_Y_NEXT_START[k] += dml_floor(4.0 * v->TSetup[k] / ((double)v->HTotal[k] / v->PixelClock[k]), 1.0) / 4.0;
		if (((v->VUpdateOffsetPix[k] + v->VUpdateWidthPix[k] + v->VReadyOffsetPix[k]) / v->HTotal[k])
				<= (isInterlaceTiming ?
						dml_floor((v->VTotal[k] - v->VActive[k] - v->VFrontPorch[k] - v->VStartup[k]) / 2.0, 1.0) :
						(int) (v->VTotal[k] - v->VActive[k] - v->VFrontPorch[k] - v->VStartup[k]))) {
			v->VREADY_AT_OR_AFTER_VSYNC[k] = true;
		} else {
			v->VREADY_AT_OR_AFTER_VSYNC[k] = false;
		}
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d, VStartup = %d (max)\n", __func__, k, v->VStartup[k]);
		dml_print("DML::%s: k=%d, VUpdateOffsetPix = %d\n", __func__, k, v->VUpdateOffsetPix[k]);
		dml_print("DML::%s: k=%d, VUpdateWidthPix = %d\n", __func__, k, v->VUpdateWidthPix[k]);
		dml_print("DML::%s: k=%d, VReadyOffsetPix = %d\n", __func__, k, v->VReadyOffsetPix[k]);
		dml_print("DML::%s: k=%d, HTotal = %d\n", __func__, k, v->HTotal[k]);
		dml_print("DML::%s: k=%d, VTotal = %d\n", __func__, k, v->VTotal[k]);
		dml_print("DML::%s: k=%d, VActive = %d\n", __func__, k, v->VActive[k]);
		dml_print("DML::%s: k=%d, VFrontPorch = %d\n", __func__, k, v->VFrontPorch[k]);
		dml_print("DML::%s: k=%d, VStartup = %d\n", __func__, k, v->VStartup[k]);
		dml_print("DML::%s: k=%d, MIN_DST_Y_NEXT_START = %f\n", __func__, k, v->MIN_DST_Y_NEXT_START[k]);
		dml_print("DML::%s: k=%d, VREADY_AT_OR_AFTER_VSYNC = %d\n", __func__, k, v->VREADY_AT_OR_AFTER_VSYNC[k]);
#endif
	}

	{
		//Maximum Bandwidth Used
		double TotalWRBandwidth = 0;
		double MaxPerPlaneVActiveWRBandwidth = 0;
		double WRBandwidth = 0;

		for (k = 0; k < v->NumberOfActivePlanes; ++k) {
			if (v->WritebackEnable[k] == true && v->WritebackPixelFormat[k] == dm_444_32) {
				WRBandwidth = v->WritebackDestinationWidth[k] * v->WritebackDestinationHeight[k]
						/ (v->HTotal[k] * v->WritebackSourceHeight[k] / v->PixelClock[k]) * 4;
			} else if (v->WritebackEnable[k] == true) {
				WRBandwidth = v->WritebackDestinationWidth[k] * v->WritebackDestinationHeight[k]
						/ (v->HTotal[k] * v->WritebackSourceHeight[k] / v->PixelClock[k]) * 8;
			}
			TotalWRBandwidth = TotalWRBandwidth + WRBandwidth;
			MaxPerPlaneVActiveWRBandwidth = dml_max(MaxPerPlaneVActiveWRBandwidth, WRBandwidth);
		}

		v->TotalDataReadBandwidth = 0;
		for (k = 0; k < v->NumberOfActivePlanes; ++k) {
			v->TotalDataReadBandwidth = v->TotalDataReadBandwidth + v->ReadBandwidthPlaneLuma[k] + v->ReadBandwidthPlaneChroma[k];
		}
	}
	// Stutter Efficiency
	CalculateStutterEfficiency(
			mode_lib,
			v->CompressedBufferSizeInkByte,
			v->UnboundedRequestEnabled,
			v->ConfigReturnBufferSizeInKByte,
			v->MetaFIFOSizeInKEntries,
			v->ZeroSizeBufferEntries,
			v->NumberOfActivePlanes,
			v->ROBBufferSizeInKByte,
			v->TotalDataReadBandwidth,
			v->DCFCLK,
			v->ReturnBW,
			v->COMPBUF_RESERVED_SPACE_64B,
			v->COMPBUF_RESERVED_SPACE_ZS,
			v->SRExitTime,
			v->SRExitZ8Time,
			v->SynchronizedVBlank,
			v->StutterEnterPlusExitWatermark,
			v->Z8StutterEnterPlusExitWatermark,
			v->ProgressiveToInterlaceUnitInOPP,
			v->Interlace,
			v->MinTTUVBlank,
			v->DPPPerPlane,
			v->DETBufferSizeY,
			v->BytePerPixelY,
			v->BytePerPixelDETY,
			v->SwathWidthY,
			v->SwathHeightY,
			v->SwathHeightC,
			v->DCCRateLuma,
			v->DCCRateChroma,
			v->DCCFractionOfZeroSizeRequestsLuma,
			v->DCCFractionOfZeroSizeRequestsChroma,
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
			&v->NumberOfStutterBurstsPerFrame,
			&v->Z8StutterEfficiencyNotIncludingVBlank,
			&v->Z8StutterEfficiency,
			&v->Z8NumberOfStutterBurstsPerFrame,
			&v->StutterPeriod);
}

static void DisplayPipeConfiguration(struct display_mode_lib *mode_lib)
{
	struct vba_vars_st *v = &mode_lib->vba;
	// Display Pipe Configuration
	double BytePerPixDETY[DC__NUM_DPP__MAX];
	double BytePerPixDETC[DC__NUM_DPP__MAX];
	int BytePerPixY[DC__NUM_DPP__MAX];
	int BytePerPixC[DC__NUM_DPP__MAX];
	int Read256BytesBlockHeightY[DC__NUM_DPP__MAX];
	int Read256BytesBlockHeightC[DC__NUM_DPP__MAX];
	int Read256BytesBlockWidthY[DC__NUM_DPP__MAX];
	int Read256BytesBlockWidthC[DC__NUM_DPP__MAX];
	double dummy1[DC__NUM_DPP__MAX];
	double dummy2[DC__NUM_DPP__MAX];
	double dummy3[DC__NUM_DPP__MAX];
	double dummy4[DC__NUM_DPP__MAX];
	int dummy5[DC__NUM_DPP__MAX];
	int dummy6[DC__NUM_DPP__MAX];
	bool dummy7[DC__NUM_DPP__MAX];
	bool dummysinglestring;

	unsigned int k;

	for (k = 0; k < v->NumberOfActivePlanes; ++k) {

		CalculateBytePerPixelAnd256BBlockSizes(
				v->SourcePixelFormat[k],
				v->SurfaceTiling[k],
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
			v->NumberOfActivePlanes,
			v->DETBufferSizeInKByte[0],
			dummy1,
			dummy2,
			v->SourceScan,
			v->SourcePixelFormat,
			v->SurfaceTiling,
			v->ViewportWidth,
			v->ViewportHeight,
			v->SurfaceWidthY,
			v->SurfaceWidthC,
			v->SurfaceHeightY,
			v->SurfaceHeightC,
			Read256BytesBlockHeightY,
			Read256BytesBlockHeightC,
			Read256BytesBlockWidthY,
			Read256BytesBlockWidthC,
			v->ODMCombineEnabled,
			v->BlendingAndTiming,
			BytePerPixY,
			BytePerPixC,
			BytePerPixDETY,
			BytePerPixDETC,
			v->HActive,
			v->HRatio,
			v->HRatioChroma,
			v->DPPPerPlane,
			dummy5,
			dummy6,
			dummy3,
			dummy4,
			v->SwathHeightY,
			v->SwathHeightC,
			v->DETBufferSizeY,
			v->DETBufferSizeC,
			dummy7,
			&dummysinglestring);
}

static bool CalculateBytePerPixelAnd256BBlockSizes(
		enum source_format_class SourcePixelFormat,
		enum dm_swizzle_mode SurfaceTiling,
		unsigned int *BytePerPixelY,
		unsigned int *BytePerPixelC,
		double *BytePerPixelDETY,
		double *BytePerPixelDETC,
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

	if ((SourcePixelFormat == dm_444_64 || SourcePixelFormat == dm_444_32 || SourcePixelFormat == dm_444_16 || SourcePixelFormat == dm_444_8 || SourcePixelFormat == dm_mono_16
			|| SourcePixelFormat == dm_mono_8 || SourcePixelFormat == dm_rgbe)) {
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
	return true;
}

static double CalculateTWait(unsigned int PrefetchMode, double DRAMClockChangeLatency, double UrgentLatency, double SREnterPlusExitTime)
{
	if (PrefetchMode == 0) {
		return dml_max(DRAMClockChangeLatency + UrgentLatency, dml_max(SREnterPlusExitTime, UrgentLatency));
	} else if (PrefetchMode == 1) {
		return dml_max(SREnterPlusExitTime, UrgentLatency);
	} else {
		return UrgentLatency;
	}
}

double dml314_CalculateWriteBackDISPCLK(
		enum source_format_class WritebackPixelFormat,
		double PixelClock,
		double WritebackHRatio,
		double WritebackVRatio,
		unsigned int WritebackHTaps,
		unsigned int WritebackVTaps,
		long WritebackSourceWidth,
		long WritebackDestinationWidth,
		unsigned int HTotal,
		unsigned int WritebackLineBufferSize)
{
	double DISPCLK_H, DISPCLK_V, DISPCLK_HB;

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
		int WritebackDestinationWidth,
		int WritebackDestinationHeight,
		int WritebackSourceHeight,
		unsigned int HTotal)
{
	double CalculateWriteBackDelay;
	double Line_length;
	double Output_lines_last_notclamped;
	double WritebackVInit;

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

static void CalculateVupdateAndDynamicMetadataParameters(
		int MaxInterDCNTileRepeaters,
		double DPPCLK,
		double DISPCLK,
		double DCFClkDeepSleep,
		double PixelClock,
		int HTotal,
		int VBlank,
		int DynamicMetadataTransmittedBytes,
		int DynamicMetadataLinesBeforeActiveRequired,
		int InterlaceEnable,
		bool ProgressiveToInterlaceUnitInOPP,
		double *TSetup,
		double *Tdmbf,
		double *Tdmec,
		double *Tdmsks,
		int *VUpdateOffsetPix,
		double *VUpdateWidthPix,
		double *VReadyOffsetPix)
{
	double TotalRepeaterDelayTime;

	TotalRepeaterDelayTime = MaxInterDCNTileRepeaters * (2 / DPPCLK + 3 / DISPCLK);
	*VUpdateWidthPix = dml_ceil((14.0 / DCFClkDeepSleep + 12.0 / DPPCLK + TotalRepeaterDelayTime) * PixelClock, 1.0);
	*VReadyOffsetPix = dml_ceil(dml_max(150.0 / DPPCLK, TotalRepeaterDelayTime + 20.0 / DCFClkDeepSleep + 10.0 / DPPCLK) * PixelClock, 1.0);
	*VUpdateOffsetPix = dml_ceil(HTotal / 4.0, 1);
	*TSetup = (*VUpdateOffsetPix + *VUpdateWidthPix + *VReadyOffsetPix) / PixelClock;
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
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: VUpdateWidthPix = %d\n", __func__, *VUpdateWidthPix);
	dml_print("DML::%s: VReadyOffsetPix = %d\n", __func__, *VReadyOffsetPix);
	dml_print("DML::%s: VUpdateOffsetPix = %d\n", __func__, *VUpdateOffsetPix);
#endif
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
		*meta_row_bw = VRatio * MetaRowByteLuma / (meta_row_height_luma * LineTime) + VRatioChroma * MetaRowByteChroma / (meta_row_height_chroma * LineTime);
	} else {
		*meta_row_bw = VRatio * MetaRowByteLuma / (meta_row_height_luma * LineTime);
	}

	if (GPUVMEnable != true) {
		*dpte_row_bw = 0;
	} else if (SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10 || SourcePixelFormat == dm_420_12 || SourcePixelFormat == dm_rgbe_alpha) {
		*dpte_row_bw = VRatio * PixelPTEBytesPerRowLuma / (dpte_row_height_luma * LineTime)
				+ VRatioChroma * PixelPTEBytesPerRowChroma / (dpte_row_height_chroma * LineTime);
	} else {
		*dpte_row_bw = VRatio * PixelPTEBytesPerRowLuma / (dpte_row_height_luma * LineTime);
	}
}

static void CalculateFlipSchedule(
		struct display_mode_lib *mode_lib,
		unsigned int k,
		double HostVMInefficiencyFactor,
		double UrgentExtraLatency,
		double UrgentLatency,
		double PDEAndMetaPTEBytesPerFrame,
		double MetaRowBytes,
		double DPTEBytesPerRow)
{
	struct vba_vars_st *v = &mode_lib->vba;
	double min_row_time = 0.0;
	unsigned int HostVMDynamicLevelsTrips;
	double TimeForFetchingMetaPTEImmediateFlip;
	double TimeForFetchingRowInVBlankImmediateFlip;
	double ImmediateFlipBW;
	double LineTime = v->HTotal[k] / v->PixelClock[k];

	if (v->GPUVMEnable == true && v->HostVMEnable == true) {
		HostVMDynamicLevelsTrips = v->HostVMMaxNonCachedPageTableLevels;
	} else {
		HostVMDynamicLevelsTrips = 0;
	}

	if (v->GPUVMEnable == true || v->DCCEnable[k] == true) {
		ImmediateFlipBW = (PDEAndMetaPTEBytesPerFrame + MetaRowBytes + DPTEBytesPerRow) * v->BandwidthAvailableForImmediateFlip / v->TotImmediateFlipBytes;
	}

	if (v->GPUVMEnable == true) {
		TimeForFetchingMetaPTEImmediateFlip = dml_max3(
				v->Tno_bw[k] + PDEAndMetaPTEBytesPerFrame * HostVMInefficiencyFactor / ImmediateFlipBW,
				UrgentExtraLatency + UrgentLatency * (v->GPUVMMaxPageTableLevels * (HostVMDynamicLevelsTrips + 1) - 1),
				LineTime / 4.0);
	} else {
		TimeForFetchingMetaPTEImmediateFlip = 0;
	}

	v->DestinationLinesToRequestVMInImmediateFlip[k] = dml_ceil(4.0 * (TimeForFetchingMetaPTEImmediateFlip / LineTime), 1) / 4.0;
	if ((v->GPUVMEnable == true || v->DCCEnable[k] == true)) {
		TimeForFetchingRowInVBlankImmediateFlip = dml_max3(
				(MetaRowBytes + DPTEBytesPerRow * HostVMInefficiencyFactor) / ImmediateFlipBW,
				UrgentLatency * (HostVMDynamicLevelsTrips + 1),
				LineTime / 4);
	} else {
		TimeForFetchingRowInVBlankImmediateFlip = 0;
	}

	v->DestinationLinesToRequestRowInImmediateFlip[k] = dml_ceil(4.0 * (TimeForFetchingRowInVBlankImmediateFlip / LineTime), 1) / 4.0;

	if (v->GPUVMEnable == true) {
		v->final_flip_bw[k] = dml_max(
				PDEAndMetaPTEBytesPerFrame * HostVMInefficiencyFactor / (v->DestinationLinesToRequestVMInImmediateFlip[k] * LineTime),
				(MetaRowBytes + DPTEBytesPerRow * HostVMInefficiencyFactor) / (v->DestinationLinesToRequestRowInImmediateFlip[k] * LineTime));
	} else if ((v->GPUVMEnable == true || v->DCCEnable[k] == true)) {
		v->final_flip_bw[k] = (MetaRowBytes + DPTEBytesPerRow * HostVMInefficiencyFactor) / (v->DestinationLinesToRequestRowInImmediateFlip[k] * LineTime);
	} else {
		v->final_flip_bw[k] = 0;
	}

	if (v->SourcePixelFormat[k] == dm_420_8 || v->SourcePixelFormat[k] == dm_420_10 || v->SourcePixelFormat[k] == dm_rgbe_alpha) {
		if (v->GPUVMEnable == true && v->DCCEnable[k] != true) {
			min_row_time = dml_min(v->dpte_row_height[k] * LineTime / v->VRatio[k], v->dpte_row_height_chroma[k] * LineTime / v->VRatioChroma[k]);
		} else if (v->GPUVMEnable != true && v->DCCEnable[k] == true) {
			min_row_time = dml_min(v->meta_row_height[k] * LineTime / v->VRatio[k], v->meta_row_height_chroma[k] * LineTime / v->VRatioChroma[k]);
		} else {
			min_row_time = dml_min4(
					v->dpte_row_height[k] * LineTime / v->VRatio[k],
					v->meta_row_height[k] * LineTime / v->VRatio[k],
					v->dpte_row_height_chroma[k] * LineTime / v->VRatioChroma[k],
					v->meta_row_height_chroma[k] * LineTime / v->VRatioChroma[k]);
		}
	} else {
		if (v->GPUVMEnable == true && v->DCCEnable[k] != true) {
			min_row_time = v->dpte_row_height[k] * LineTime / v->VRatio[k];
		} else if (v->GPUVMEnable != true && v->DCCEnable[k] == true) {
			min_row_time = v->meta_row_height[k] * LineTime / v->VRatio[k];
		} else {
			min_row_time = dml_min(v->dpte_row_height[k] * LineTime / v->VRatio[k], v->meta_row_height[k] * LineTime / v->VRatio[k]);
		}
	}

	if (v->DestinationLinesToRequestVMInImmediateFlip[k] >= 32 || v->DestinationLinesToRequestRowInImmediateFlip[k] >= 16
			|| TimeForFetchingMetaPTEImmediateFlip + 2 * TimeForFetchingRowInVBlankImmediateFlip > min_row_time) {
		v->ImmediateFlipSupportedForPipe[k] = false;
	} else {
		v->ImmediateFlipSupportedForPipe[k] = true;
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DestinationLinesToRequestVMInImmediateFlip = %f\n", __func__, v->DestinationLinesToRequestVMInImmediateFlip[k]);
	dml_print("DML::%s: DestinationLinesToRequestRowInImmediateFlip = %f\n", __func__, v->DestinationLinesToRequestRowInImmediateFlip[k]);
	dml_print("DML::%s: TimeForFetchingMetaPTEImmediateFlip = %f\n", __func__, TimeForFetchingMetaPTEImmediateFlip);
	dml_print("DML::%s: TimeForFetchingRowInVBlankImmediateFlip = %f\n", __func__, TimeForFetchingRowInVBlankImmediateFlip);
	dml_print("DML::%s: min_row_time = %f\n", __func__, min_row_time);
	dml_print("DML::%s: ImmediateFlipSupportedForPipe = %d\n", __func__, v->ImmediateFlipSupportedForPipe[k]);
#endif

}

static double TruncToValidBPP(
		double LinkBitRate,
		int Lanes,
		int HTotal,
		int HActive,
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
	double MaxLinkBPP;
	int MinDSCBPP;
	double MaxDSCBPP;
	int NonDSCBPP0;
	int NonDSCBPP1;
	int NonDSCBPP2;

	if (Format == dm_420) {
		NonDSCBPP0 = 12;
		NonDSCBPP1 = 15;
		NonDSCBPP2 = 18;
		MinDSCBPP = 6;
		MaxDSCBPP = 1.5 * DSCInputBitPerComponent - 1 / 16;
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
				return 16.0;
			} else {
				return BPP_INVALID;
			}
		}
	} else {
		if (!((DSCEnable == false && (DesiredBPP == NonDSCBPP2 || DesiredBPP == NonDSCBPP1 || DesiredBPP <= NonDSCBPP0))
				|| (DSCEnable && DesiredBPP >= MinDSCBPP && DesiredBPP <= MaxDSCBPP))) {
			return BPP_INVALID;
		} else {
			return DesiredBPP;
		}
	}
	return BPP_INVALID;
}

static noinline void CalculatePrefetchSchedulePerPlane(
		struct display_mode_lib *mode_lib,
		double HostVMInefficiencyFactor,
		int i,
		unsigned int j,
		unsigned int k)
{
	struct vba_vars_st *v = &mode_lib->vba;
	Pipe myPipe;

	myPipe.DPPCLK = v->RequiredDPPCLK[i][j][k];
	myPipe.DISPCLK = v->RequiredDISPCLK[i][j];
	myPipe.PixelClock = v->PixelClock[k];
	myPipe.DCFCLKDeepSleep = v->ProjectedDCFCLKDeepSleep[i][j];
	myPipe.DPPPerPlane = v->NoOfDPP[i][j][k];
	myPipe.ScalerEnabled = v->ScalerEnabled[k];
	myPipe.VRatio = mode_lib->vba.VRatio[k];
	myPipe.VRatioChroma = mode_lib->vba.VRatioChroma[k];

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
	myPipe.ODMCombineIsEnabled = v->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_4to1
		|| v->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_2to1;
	myPipe.SourcePixelFormat = v->SourcePixelFormat[k];
	myPipe.BytePerPixelY = v->BytePerPixelY[k];
	myPipe.BytePerPixelC = v->BytePerPixelC[k];
	myPipe.ProgressiveToInterlaceUnitInOPP = v->ProgressiveToInterlaceUnitInOPP;
	v->NoTimeForPrefetch[i][j][k] = CalculatePrefetchSchedule(
		mode_lib,
		HostVMInefficiencyFactor,
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
		v->PrefillY[k],
		v->MaxNumSwY[k],
		v->PrefetchLinesC[i][j][k],
		v->SwathWidthCThisState[k],
		v->PrefillC[k],
		v->MaxNumSwC[k],
		v->swath_width_luma_ub_this_state[k],
		v->swath_width_chroma_ub_this_state[k],
		v->SwathHeightYThisState[k],
		v->SwathHeightCThisState[k],
		v->TWait,
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
		&v->dummy7[k],
		&v->dummy8[k],
		&v->dummy13[k],
		&v->VUpdateOffsetPix[k],
		&v->VUpdateWidthPix[k],
		&v->VReadyOffsetPix[k]);
}

void dml314_ModeSupportAndSystemConfigurationFull(struct display_mode_lib *mode_lib)
{
	struct vba_vars_st *v = &mode_lib->vba;

	int i, j;
	unsigned int k, m;
	int ReorderingBytes;
	int MinPrefetchMode = 0, MaxPrefetchMode = 2;
	bool NoChroma = true;
	bool EnoughWritebackUnits = true;
	bool P2IWith420 = false;
	bool DSCOnlyIfNecessaryWithBPP = false;
	bool DSC422NativeNotSupported = false;
	double MaxTotalVActiveRDBandwidth;
	bool ViewportExceedsSurface = false;
	bool FMTBufferExceeded = false;

	/*MODE SUPPORT, VOLTAGE STATE AND SOC CONFIGURATION*/

	CalculateMinAndMaxPrefetchMode(
		mode_lib->vba.AllowDRAMSelfRefreshOrDRAMClockChangeInVblank,
		&MinPrefetchMode, &MaxPrefetchMode);

	/*Scale Ratio, taps Support Check*/

	v->ScaleRatioAndTapsSupport = true;
	for (k = 0; k < v->NumberOfActivePlanes; k++) {
		if (v->ScalerEnabled[k] == false
				&& ((v->SourcePixelFormat[k] != dm_444_64 && v->SourcePixelFormat[k] != dm_444_32
						&& v->SourcePixelFormat[k] != dm_444_16 && v->SourcePixelFormat[k] != dm_mono_16
						&& v->SourcePixelFormat[k] != dm_mono_8 && v->SourcePixelFormat[k] != dm_rgbe
						&& v->SourcePixelFormat[k] != dm_rgbe_alpha) || v->HRatio[k] != 1.0 || v->htaps[k] != 1.0
						|| v->VRatio[k] != 1.0 || v->vtaps[k] != 1.0)) {
			v->ScaleRatioAndTapsSupport = false;
		} else if (v->vtaps[k] < 1.0 || v->vtaps[k] > 8.0 || v->htaps[k] < 1.0 || v->htaps[k] > 8.0
				|| (v->htaps[k] > 1.0 && (v->htaps[k] % 2) == 1) || v->HRatio[k] > v->MaxHSCLRatio
				|| v->VRatio[k] > v->MaxVSCLRatio || v->HRatio[k] > v->htaps[k]
				|| v->VRatio[k] > v->vtaps[k]
				|| (v->SourcePixelFormat[k] != dm_444_64 && v->SourcePixelFormat[k] != dm_444_32
						&& v->SourcePixelFormat[k] != dm_444_16 && v->SourcePixelFormat[k] != dm_mono_16
						&& v->SourcePixelFormat[k] != dm_mono_8 && v->SourcePixelFormat[k] != dm_rgbe
						&& (v->VTAPsChroma[k] < 1 || v->VTAPsChroma[k] > 8 || v->HTAPsChroma[k] < 1
								|| v->HTAPsChroma[k] > 8 || (v->HTAPsChroma[k] > 1 && v->HTAPsChroma[k] % 2 == 1)
								|| v->HRatioChroma[k] > v->MaxHSCLRatio
								|| v->VRatioChroma[k] > v->MaxVSCLRatio
								|| v->HRatioChroma[k] > v->HTAPsChroma[k]
								|| v->VRatioChroma[k] > v->VTAPsChroma[k]))) {
			v->ScaleRatioAndTapsSupport = false;
		}
	}
	/*Source Format, Pixel Format and Scan Support Check*/

	v->SourceFormatPixelAndScanSupport = true;
	for (k = 0; k < v->NumberOfActivePlanes; k++) {
		if (v->SurfaceTiling[k] == dm_sw_linear && (!(v->SourceScan[k] != dm_vert) || v->DCCEnable[k] == true)) {
			v->SourceFormatPixelAndScanSupport = false;
		}
	}
	/*Bandwidth Support Check*/

	for (k = 0; k < v->NumberOfActivePlanes; k++) {
		CalculateBytePerPixelAnd256BBlockSizes(
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
	for (k = 0; k < v->NumberOfActivePlanes; k++) {
		if (v->SourceScan[k] != dm_vert) {
			v->SwathWidthYSingleDPP[k] = v->ViewportWidth[k];
			v->SwathWidthCSingleDPP[k] = v->ViewportWidthChroma[k];
		} else {
			v->SwathWidthYSingleDPP[k] = v->ViewportHeight[k];
			v->SwathWidthCSingleDPP[k] = v->ViewportHeightChroma[k];
		}
	}
	for (k = 0; k < v->NumberOfActivePlanes; k++) {
		v->ReadBandwidthLuma[k] = v->SwathWidthYSingleDPP[k] * dml_ceil(v->BytePerPixelInDETY[k], 1.0)
				/ (v->HTotal[k] / v->PixelClock[k]) * v->VRatio[k];
		v->ReadBandwidthChroma[k] = v->SwathWidthYSingleDPP[k] / 2 * dml_ceil(v->BytePerPixelInDETC[k], 2.0)
				/ (v->HTotal[k] / v->PixelClock[k]) * v->VRatio[k] / 2.0;
	}
	for (k = 0; k < v->NumberOfActivePlanes; k++) {
		if (v->WritebackEnable[k] == true && v->WritebackPixelFormat[k] == dm_444_64) {
			v->WriteBandwidth[k] = v->WritebackDestinationWidth[k] * v->WritebackDestinationHeight[k]
					/ (v->WritebackSourceHeight[k] * v->HTotal[k] / v->PixelClock[k]) * 8.0;
		} else if (v->WritebackEnable[k] == true) {
			v->WriteBandwidth[k] = v->WritebackDestinationWidth[k] * v->WritebackDestinationHeight[k]
					/ (v->WritebackSourceHeight[k] * v->HTotal[k] / v->PixelClock[k]) * 4.0;
		} else {
			v->WriteBandwidth[k] = 0.0;
		}
	}

	/*Writeback Latency support check*/

	v->WritebackLatencySupport = true;
	for (k = 0; k < v->NumberOfActivePlanes; k++) {
		if (v->WritebackEnable[k] == true && (v->WriteBandwidth[k] > v->WritebackInterfaceBufferSize * 1024 / v->WritebackLatency)) {
			v->WritebackLatencySupport = false;
		}
	}

	/*Writeback Mode Support Check*/

	v->TotalNumberOfActiveWriteback = 0;
	for (k = 0; k < v->NumberOfActivePlanes; k++) {
		if (v->WritebackEnable[k] == true) {
			v->TotalNumberOfActiveWriteback = v->TotalNumberOfActiveWriteback + 1;
		}
	}

	if (v->TotalNumberOfActiveWriteback > v->MaxNumWriteback) {
		EnoughWritebackUnits = false;
	}

	/*Writeback Scale Ratio and Taps Support Check*/

	v->WritebackScaleRatioAndTapsSupport = true;
	for (k = 0; k < v->NumberOfActivePlanes; k++) {
		if (v->WritebackEnable[k] == true) {
			if (v->WritebackHRatio[k] > v->WritebackMaxHSCLRatio || v->WritebackVRatio[k] > v->WritebackMaxVSCLRatio
					|| v->WritebackHRatio[k] < v->WritebackMinHSCLRatio
					|| v->WritebackVRatio[k] < v->WritebackMinVSCLRatio
					|| v->WritebackHTaps[k] > v->WritebackMaxHSCLTaps
					|| v->WritebackVTaps[k] > v->WritebackMaxVSCLTaps
					|| v->WritebackHRatio[k] > v->WritebackHTaps[k] || v->WritebackVRatio[k] > v->WritebackVTaps[k]
					|| (v->WritebackHTaps[k] > 2.0 && ((v->WritebackHTaps[k] % 2) == 1))) {
				v->WritebackScaleRatioAndTapsSupport = false;
			}
			if (2.0 * v->WritebackDestinationWidth[k] * (v->WritebackVTaps[k] - 1) * 57 > v->WritebackLineBufferSize) {
				v->WritebackScaleRatioAndTapsSupport = false;
			}
		}
	}
	/*Maximum DISPCLK/DPPCLK Support check*/

	v->WritebackRequiredDISPCLK = 0.0;
	for (k = 0; k < v->NumberOfActivePlanes; k++) {
		if (v->WritebackEnable[k] == true) {
			v->WritebackRequiredDISPCLK = dml_max(
					v->WritebackRequiredDISPCLK,
					dml314_CalculateWriteBackDISPCLK(
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
	for (k = 0; k < v->NumberOfActivePlanes; k++) {
		if (v->HRatio[k] > 1.0) {
			v->PSCL_FACTOR[k] = dml_min(
					v->MaxDCHUBToPSCLThroughput,
					v->MaxPSCLToLBThroughput * v->HRatio[k] / dml_ceil(v->htaps[k] / 6.0, 1.0));
		} else {
			v->PSCL_FACTOR[k] = dml_min(v->MaxDCHUBToPSCLThroughput, v->MaxPSCLToLBThroughput);
		}
		if (v->BytePerPixelC[k] == 0.0) {
			v->PSCL_FACTOR_CHROMA[k] = 0.0;
			v->MinDPPCLKUsingSingleDPP[k] = v->PixelClock[k]
					* dml_max3(
							v->vtaps[k] / 6.0 * dml_min(1.0, v->HRatio[k]),
							v->HRatio[k] * v->VRatio[k] / v->PSCL_FACTOR[k],
							1.0);
			if ((v->htaps[k] > 6.0 || v->vtaps[k] > 6.0) && v->MinDPPCLKUsingSingleDPP[k] < 2.0 * v->PixelClock[k]) {
				v->MinDPPCLKUsingSingleDPP[k] = 2.0 * v->PixelClock[k];
			}
		} else {
			if (v->HRatioChroma[k] > 1.0) {
				v->PSCL_FACTOR_CHROMA[k] = dml_min(
						v->MaxDCHUBToPSCLThroughput,
						v->MaxPSCLToLBThroughput * v->HRatioChroma[k] / dml_ceil(v->HTAPsChroma[k] / 6.0, 1.0));
			} else {
				v->PSCL_FACTOR_CHROMA[k] = dml_min(v->MaxDCHUBToPSCLThroughput, v->MaxPSCLToLBThroughput);
			}
			v->MinDPPCLKUsingSingleDPP[k] = v->PixelClock[k]
					* dml_max5(
							v->vtaps[k] / 6.0 * dml_min(1.0, v->HRatio[k]),
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
	for (k = 0; k < v->NumberOfActivePlanes; k++) {
		int MaximumSwathWidthSupportLuma;
		int MaximumSwathWidthSupportChroma;

		if (v->SurfaceTiling[k] == dm_sw_linear) {
			MaximumSwathWidthSupportLuma = 8192.0;
		} else if (v->SourceScan[k] == dm_vert && v->BytePerPixelC[k] > 0) {
			MaximumSwathWidthSupportLuma = 2880.0;
		} else if (v->SourcePixelFormat[k] == dm_rgbe_alpha) {
			MaximumSwathWidthSupportLuma = 3840.0;
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
			v->NoOfDPPThisState,
			v->swath_width_luma_ub_this_state,
			v->swath_width_chroma_ub_this_state,
			v->SwathWidthYThisState,
			v->SwathWidthCThisState,
			v->SwathHeightYThisState,
			v->SwathHeightCThisState,
			v->DETBufferSizeYThisState,
			v->DETBufferSizeCThisState,
			v->SingleDPPViewportSizeSupportPerPlane,
			&v->ViewportSizeSupport[0][0]);

	for (i = 0; i < v->soc.num_states; i++) {
		for (j = 0; j < 2; j++) {
			v->MaxDispclkRoundedDownToDFSGranularity = RoundToDFSGranularityDown(v->MaxDispclk[i], v->DISPCLKDPPCLKVCOSpeed);
			v->MaxDppclkRoundedDownToDFSGranularity = RoundToDFSGranularityDown(v->MaxDppclk[i], v->DISPCLKDPPCLKVCOSpeed);
			v->RequiredDISPCLK[i][j] = 0.0;
			v->DISPCLK_DPPCLK_Support[i][j] = true;
			for (k = 0; k < v->NumberOfActivePlanes; k++) {
				v->PlaneRequiredDISPCLKWithoutODMCombine = v->PixelClock[k] * (1.0 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0)
						* (1.0 + v->DISPCLKRampingMargin / 100.0);
				if ((v->PlaneRequiredDISPCLKWithoutODMCombine >= v->MaxDispclk[i]
						&& v->MaxDispclk[i] == v->MaxDispclk[v->soc.num_states - 1]
						&& v->MaxDppclk[i] == v->MaxDppclk[v->soc.num_states - 1])) {
					v->PlaneRequiredDISPCLKWithoutODMCombine = v->PixelClock[k]
							* (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);
				}
				v->PlaneRequiredDISPCLKWithODMCombine2To1 = v->PixelClock[k] / 2 * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0)
						* (1 + v->DISPCLKRampingMargin / 100.0);
				if ((v->PlaneRequiredDISPCLKWithODMCombine2To1 >= v->MaxDispclk[i]
						&& v->MaxDispclk[i] == v->MaxDispclk[v->soc.num_states - 1]
						&& v->MaxDppclk[i] == v->MaxDppclk[v->soc.num_states - 1])) {
					v->PlaneRequiredDISPCLKWithODMCombine2To1 = v->PixelClock[k] / 2
							* (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);
				}
				v->PlaneRequiredDISPCLKWithODMCombine4To1 = v->PixelClock[k] / 4 * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0)
						* (1 + v->DISPCLKRampingMargin / 100.0);
				if ((v->PlaneRequiredDISPCLKWithODMCombine4To1 >= v->MaxDispclk[i]
						&& v->MaxDispclk[i] == v->MaxDispclk[v->soc.num_states - 1]
						&& v->MaxDppclk[i] == v->MaxDppclk[v->soc.num_states - 1])) {
					v->PlaneRequiredDISPCLKWithODMCombine4To1 = v->PixelClock[k] / 4
							* (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);
				}

				if (v->ODMCombinePolicy == dm_odm_combine_policy_none
						|| !(v->Output[k] == dm_dp ||
						     v->Output[k] == dm_dp2p0 ||
						     v->Output[k] == dm_edp)) {
					v->ODMCombineEnablePerState[i][k] = dm_odm_combine_mode_disabled;
					v->PlaneRequiredDISPCLK = v->PlaneRequiredDISPCLKWithoutODMCombine;

					if (v->HActive[k] / 2 > DCN314_MAX_FMT_420_BUFFER_WIDTH)
						FMTBufferExceeded = true;
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
				if (v->DSCEnabled[k] && v->HActive[k] > DCN314_MAX_DSC_IMAGE_WIDTH
						&& v->ODMCombineEnablePerState[i][k] != dm_odm_combine_mode_4to1) {
					if (v->HActive[k] / 2 > DCN314_MAX_DSC_IMAGE_WIDTH) {
						v->ODMCombineEnablePerState[i][k] = dm_odm_combine_mode_4to1;
						v->PlaneRequiredDISPCLK = v->PlaneRequiredDISPCLKWithODMCombine4To1;
					} else {
						v->ODMCombineEnablePerState[i][k] = dm_odm_combine_mode_2to1;
						v->PlaneRequiredDISPCLK = v->PlaneRequiredDISPCLKWithODMCombine2To1;
					}
				}
				if (v->OutputFormat[k] == dm_420 && v->HActive[k] > DCN314_MAX_FMT_420_BUFFER_WIDTH
						&& v->ODMCombineEnablePerState[i][k] != dm_odm_combine_mode_4to1) {
					if (v->HActive[k] / 2 > DCN314_MAX_FMT_420_BUFFER_WIDTH) {
						v->ODMCombineEnablePerState[i][k] = dm_odm_combine_mode_4to1;
						v->PlaneRequiredDISPCLK = v->PlaneRequiredDISPCLKWithODMCombine4To1;

						if (v->HActive[k] / 4 > DCN314_MAX_FMT_420_BUFFER_WIDTH)
							FMTBufferExceeded = true;
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
						|| (v->MinDPPCLKUsingSingleDPP[k] * (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0)
								<= v->MaxDppclkRoundedDownToDFSGranularity && v->SingleDPPViewportSizeSupportPerPlane[k] == true))) {
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
						> v->MaxDppclkRoundedDownToDFSGranularity)
						|| (v->PlaneRequiredDISPCLK > v->MaxDispclkRoundedDownToDFSGranularity)) {
					v->DISPCLK_DPPCLK_Support[i][j] = false;
				}
			}
			v->TotalNumberOfActiveDPP[i][j] = 0;
			v->TotalNumberOfSingleDPPPlanes[i][j] = 0;
			for (k = 0; k < v->NumberOfActivePlanes; k++) {
				v->TotalNumberOfActiveDPP[i][j] = v->TotalNumberOfActiveDPP[i][j] + v->NoOfDPP[i][j][k];
				if (v->NoOfDPP[i][j][k] == 1)
					v->TotalNumberOfSingleDPPPlanes[i][j] = v->TotalNumberOfSingleDPPPlanes[i][j] + 1;
				if (v->SourcePixelFormat[k] == dm_420_8 || v->SourcePixelFormat[k] == dm_420_10
						|| v->SourcePixelFormat[k] == dm_420_12 || v->SourcePixelFormat[k] == dm_rgbe_alpha)
					NoChroma = false;
			}

			// UPTO
			if (j == 1 && v->WhenToDoMPCCombine != dm_mpc_never
					&& !UnboundedRequest(v->UseUnboundedRequesting, v->TotalNumberOfActiveDPP[i][j], NoChroma, v->Output[0])) {
				while (!(v->TotalNumberOfActiveDPP[i][j] >= v->MaxNumDPP || v->TotalNumberOfSingleDPPPlanes[i][j] == 0)) {
					double BWOfNonSplitPlaneOfMaximumBandwidth;
					unsigned int NumberOfNonSplitPlaneOfMaximumBandwidth;

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
					v->RequiredDPPCLK[i][j][NumberOfNonSplitPlaneOfMaximumBandwidth] =
							v->MinDPPCLKUsingSingleDPP[NumberOfNonSplitPlaneOfMaximumBandwidth]
									* (1 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100) / 2;
					v->TotalNumberOfActiveDPP[i][j] = v->TotalNumberOfActiveDPP[i][j] + 1;
					v->TotalNumberOfSingleDPPPlanes[i][j] = v->TotalNumberOfSingleDPPPlanes[i][j] - 1;
				}
			}
			if (v->TotalNumberOfActiveDPP[i][j] > v->MaxNumDPP) {
				v->RequiredDISPCLK[i][j] = 0.0;
				v->DISPCLK_DPPCLK_Support[i][j] = true;
				for (k = 0; k < v->NumberOfActivePlanes; k++) {
					v->ODMCombineEnablePerState[i][k] = dm_odm_combine_mode_disabled;
					if (v->SingleDPPViewportSizeSupportPerPlane[k] == false && v->WhenToDoMPCCombine != dm_mpc_never) {
						v->MPCCombine[i][j][k] = true;
						v->NoOfDPP[i][j][k] = 2;
						v->RequiredDPPCLK[i][j][k] = v->MinDPPCLKUsingSingleDPP[k]
								* (1.0 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0) / 2.0;
					} else {
						v->MPCCombine[i][j][k] = false;
						v->NoOfDPP[i][j][k] = 1;
						v->RequiredDPPCLK[i][j][k] = v->MinDPPCLKUsingSingleDPP[k]
								* (1.0 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);
					}
					if (!(v->MaxDispclk[i] == v->MaxDispclk[v->soc.num_states - 1]
							&& v->MaxDppclk[i] == v->MaxDppclk[v->soc.num_states - 1])) {
						v->PlaneRequiredDISPCLK = v->PixelClock[k] * (1.0 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0)
								* (1.0 + v->DISPCLKRampingMargin / 100.0);
					} else {
						v->PlaneRequiredDISPCLK = v->PixelClock[k] * (1.0 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);
					}
					v->RequiredDISPCLK[i][j] = dml_max(v->RequiredDISPCLK[i][j], v->PlaneRequiredDISPCLK);
					if ((v->MinDPPCLKUsingSingleDPP[k] / v->NoOfDPP[i][j][k] * (1.0 + v->DISPCLKDPPCLKDSCCLKDownSpreading / 100.0)
							> v->MaxDppclkRoundedDownToDFSGranularity)
							|| (v->PlaneRequiredDISPCLK > v->MaxDispclkRoundedDownToDFSGranularity)) {
						v->DISPCLK_DPPCLK_Support[i][j] = false;
					}
				}
				v->TotalNumberOfActiveDPP[i][j] = 0.0;
				for (k = 0; k < v->NumberOfActivePlanes; k++) {
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

	for (i = 0; i < v->soc.num_states; i++) {
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
	for (k = 0; k < v->NumberOfActivePlanes; k++) {
		if (!(v->DSCInputBitPerComponent[k] == 12.0 || v->DSCInputBitPerComponent[k] == 10.0 || v->DSCInputBitPerComponent[k] == 8.0)
				|| v->DSCInputBitPerComponent[k] > v->MaximumDSCBitsPerComponent) {
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

	for (i = 0; i < v->soc.num_states; i++) {
		for (k = 0; k < v->NumberOfActivePlanes; k++) {
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
						v->OutputBppPerState[i][k] = v->Outbpp;
						// TODO: Need some other way to handle this nonsense
						// v->OutputTypeAndRatePerState[i][k] = v->Output[k] & " HBR3"
					}
					if (v->Outbpp == BPP_INVALID && v->PHYCLKD18PerState[i] >= 10000.0 / 18) {
						v->Outbpp = TruncToValidBPP(
								(1.0 - v->Downspreading / 100.0) * 10000,
								4,
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
						//v->OutputTypeAndRatePerState[i][k] = v->Output[k] & "10x4";
					}
					if (v->Outbpp == BPP_INVALID && v->PHYCLKD18PerState[i] >= 12000.0 / 18) {
						v->Outbpp = TruncToValidBPP(
								12000,
								4,
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
						//v->OutputTypeAndRatePerState[i][k] = v->Output[k] & "12x4";
					}
				}
			} else {
				v->OutputBppPerState[i][k] = 0;
			}
		}
	}

	for (i = 0; i < v->soc.num_states; i++) {
		v->LinkCapacitySupport[i] = true;
		for (k = 0; k < v->NumberOfActivePlanes; k++) {
			if (v->BlendingAndTiming[k] == k
					&& (v->Output[k] == dm_dp ||
					    v->Output[k] == dm_edp ||
					    v->Output[k] == dm_hdmi) && v->OutputBppPerState[i][k] == 0) {
				v->LinkCapacitySupport[i] = false;
			}
		}
	}

	// UPTO 2172
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		if (v->BlendingAndTiming[k] == k
				&& (v->Output[k] == dm_dp ||
				    v->Output[k] == dm_edp ||
				    v->Output[k] == dm_hdmi)) {
			if (v->OutputFormat[k] == dm_420 && v->Interlace[k] == 1 && v->ProgressiveToInterlaceUnitInOPP == true) {
				P2IWith420 = true;
			}
			if (v->DSCEnable[k] == true && v->OutputFormat[k] == dm_n422
					&& !v->DSC422NativeSupport) {
				DSC422NativeNotSupported = true;
			}
		}
	}


	for (i = 0; i < v->soc.num_states; ++i) {
		v->ODMCombine4To1SupportCheckOK[i] = true;
		for (k = 0; k < v->NumberOfActivePlanes; ++k) {
			if (v->BlendingAndTiming[k] == k && v->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_4to1
					&& (v->ODMCombine4To1Supported == false || v->Output[k] == dm_dp || v->Output[k] == dm_edp
							|| v->Output[k] == dm_hdmi)) {
				v->ODMCombine4To1SupportCheckOK[i] = false;
			}
		}
	}

	/* Skip dscclk validation: as long as dispclk is supported, dscclk is also implicitly supported */

	for (i = 0; i < v->soc.num_states; i++) {
		v->NotEnoughDSCUnits[i] = false;
		v->TotalDSCUnitsRequired = 0.0;
		for (k = 0; k < v->NumberOfActivePlanes; k++) {
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

	for (i = 0; i < v->soc.num_states; i++) {
		for (k = 0; k < v->NumberOfActivePlanes; k++) {
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
							* (dscceComputeDelay(
									v->DSCInputBitPerComponent[k],
									v->BPP,
									dml_ceil(1.0 * v->HActive[k] / v->NumberOfDSCSlices[k], 1.0),
									v->NumberOfDSCSlices[k] / 2,
									v->OutputFormat[k],
									v->Output[k]) + dscComputeDelay(v->OutputFormat[k], v->Output[k]));
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
		for (k = 0; k < v->NumberOfActivePlanes; k++) {
			for (m = 0; m < v->NumberOfActivePlanes; m++) {
				if (v->BlendingAndTiming[k] == m && v->RequiresDSC[i][m] == true) {
					v->DSCDelayPerState[i][k] = v->DSCDelayPerState[i][m];
				}
			}
		}
	}

	//Calculate Swath, DET Configuration, DCFCLKDeepSleep
	//
	for (i = 0; i < v->soc.num_states; ++i) {
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
		v->cursor_bw[k] = v->NumberOfCursors[k] * v->CursorWidth[k][0] * v->CursorBPP[k][0] / 8.0
				/ (v->HTotal[k] / v->PixelClock[k]) * v->VRatio[k];
	}

	for (i = 0; i < v->soc.num_states; i++) {
		for (j = 0; j < 2; j++) {
			bool NotUrgentLatencyHiding[DC__NUM_DPP__MAX];

			for (k = 0; k < v->NumberOfActivePlanes; k++) {
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

			for (k = 0; k < v->NumberOfActivePlanes; k++) {
				if (v->SourcePixelFormat[k] == dm_420_8 || v->SourcePixelFormat[k] == dm_420_10
						|| v->SourcePixelFormat[k] == dm_420_12 || v->SourcePixelFormat[k] == dm_rgbe_alpha) {

					if ((v->SourcePixelFormat[k] == dm_420_10 || v->SourcePixelFormat[k] == dm_420_12)
							&& v->SourceScan[k] != dm_vert) {
						v->PTEBufferSizeInRequestsForLuma = (v->PTEBufferSizeInRequestsLuma + v->PTEBufferSizeInRequestsChroma)
								/ 2;
						v->PTEBufferSizeInRequestsForChroma = v->PTEBufferSizeInRequestsForLuma;
					} else {
						v->PTEBufferSizeInRequestsForLuma = v->PTEBufferSizeInRequestsLuma;
						v->PTEBufferSizeInRequestsForChroma = v->PTEBufferSizeInRequestsChroma;
					}

					v->PDEAndMetaPTEBytesPerFrameC = CalculateVMAndRowBytes(
							mode_lib,
							v->DCCEnable[k],
							v->Read256BlockHeightC[k],
							v->Read256BlockWidthC[k],
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
						&v->dummyinteger7,
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
			/*
			 * DCCMetaBufferSizeSupport(i, j) = True
			 * For k = 0 To NumberOfActivePlanes - 1
			 * If MetaRowBytes(i, j, k) > 24064 Then
			 * DCCMetaBufferSizeSupport(i, j) = False
			 * End If
			 * Next k
			 */
			v->DCCMetaBufferSizeSupport[i][j] = true;
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				if (v->MetaRowBytes[i][j][k] > 24064)
					v->DCCMetaBufferSizeSupport[i][j] = false;
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

			v->NotEnoughUrgentLatencyHidingA[i][j] = false;
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				if (NotUrgentLatencyHiding[k]) {
					v->NotEnoughUrgentLatencyHidingA[i][j] = true;
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
		}
	}

	//Calculate Return BW
	for (i = 0; i < v->soc.num_states; ++i) {
		for (j = 0; j <= 1; ++j) {
			for (k = 0; k < v->NumberOfActivePlanes; k++) {
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
					for (m = 0; m < v->NumberOfActivePlanes; m++) {
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
			for (k = 0; k < v->NumberOfActivePlanes; k++) {
				for (m = 0; m < v->NumberOfActivePlanes; m++) {
					if (v->BlendingAndTiming[k] == m) {
						v->WritebackDelayTime[k] = v->WritebackDelayTime[m];
					}
				}
			}
			v->MaxMaxVStartup[i][j] = 0;
			for (k = 0; k < v->NumberOfActivePlanes; k++) {
				v->MaximumVStartup[i][j][k] =
					CalculateMaxVStartup(
						v->VTotal[k],
						v->VActive[k],
						v->VBlankNom[k],
						v->HTotal[k],
						v->PixelClock[k],
						v->ProgressiveToInterlaceUnitInOPP,
						v->Interlace[k],
						v->ip.VBlankNomDefaultUS,
						v->WritebackDelayTime[k]);
				v->MaxMaxVStartup[i][j] = dml_max(v->MaxMaxVStartup[i][j], v->MaximumVStartup[i][j][k]);
				}
		}
	}

	ReorderingBytes = v->NumberOfChannels
			* dml_max3(
					v->UrgentOutOfOrderReturnPerChannelPixelDataOnly,
					v->UrgentOutOfOrderReturnPerChannelPixelMixedWithVMData,
					v->UrgentOutOfOrderReturnPerChannelVMDataOnly);

	for (i = 0; i < v->soc.num_states; ++i) {
		for (j = 0; j <= 1; ++j) {
			v->DCFCLKState[i][j] = v->DCFCLKPerState[i];
		}
	}

	if (v->UseMinimumRequiredDCFCLK == true)
		UseMinimumDCFCLK(mode_lib, MaxPrefetchMode, ReorderingBytes);

	for (i = 0; i < v->soc.num_states; ++i) {
		for (j = 0; j <= 1; ++j) {
			double IdealFabricAndSDPPortBandwidthPerState = dml_min(
					v->ReturnBusWidth * v->DCFCLKState[i][j],
					v->FabricClockPerState[i] * v->FabricDatapathToDCNDataReturn);
			double IdealDRAMBandwidthPerState = v->DRAMSpeedPerState[i] * v->NumberOfChannels * v->DRAMChannelWidth;
			double PixelDataOnlyReturnBWPerState = dml_min(
					IdealFabricAndSDPPortBandwidthPerState * v->PercentOfIdealFabricAndSDPPortBWReceivedAfterUrgLatency / 100.0,
					IdealDRAMBandwidthPerState * v->PercentOfIdealDRAMBWReceivedAfterUrgLatencyPixelDataOnly / 100.0);
			double PixelMixedWithVMDataReturnBWPerState = dml_min(
					IdealFabricAndSDPPortBandwidthPerState * v->PercentOfIdealFabricAndSDPPortBWReceivedAfterUrgLatency / 100.0,
					IdealDRAMBandwidthPerState * v->PercentOfIdealDRAMBWReceivedAfterUrgLatencyPixelMixedWithVMData / 100.0);

			if (v->HostVMEnable != true) {
				v->ReturnBWPerState[i][j] = PixelDataOnlyReturnBWPerState;
			} else {
				v->ReturnBWPerState[i][j] = PixelMixedWithVMDataReturnBWPerState;
			}
		}
	}

	//Re-ordering Buffer Support Check
	for (i = 0; i < v->soc.num_states; ++i) {
		for (j = 0; j <= 1; ++j) {
			if ((v->ROBBufferSizeInKByte - v->PixelChunkSizeInKByte) * 1024 / v->ReturnBWPerState[i][j]
					> (v->RoundTripPingLatencyCycles + __DML_ARB_TO_RET_DELAY__) / v->DCFCLKState[i][j] + ReorderingBytes / v->ReturnBWPerState[i][j]) {
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

	for (i = 0; i < v->soc.num_states; ++i) {
		for (j = 0; j <= 1; ++j) {
			v->MaxTotalVerticalActiveAvailableBandwidth[i][j] = dml_min(
					dml_min(
							v->ReturnBusWidth * v->DCFCLKState[i][j],
							v->FabricClockPerState[i] * v->FabricDatapathToDCNDataReturn)
							* v->MaxAveragePercentOfIdealFabricAndSDPPortBWDisplayCanUseInNormalSystemOperation / 100,
					v->DRAMSpeedPerState[i] * v->NumberOfChannels * v->DRAMChannelWidth
							* v->MaxAveragePercentOfIdealDRAMBWDisplayCanUseInNormalSystemOperation / 100);

			if (MaxTotalVActiveRDBandwidth <= v->MaxTotalVerticalActiveAvailableBandwidth[i][j]) {
				v->TotalVerticalActiveBandwidthSupport[i][j] = true;
			} else {
				v->TotalVerticalActiveBandwidthSupport[i][j] = false;
			}
		}
	}

	v->UrgentLatency = CalculateUrgentLatency(
			v->UrgentLatencyPixelDataOnly,
			v->UrgentLatencyPixelMixedWithVMData,
			v->UrgentLatencyVMDataOnly,
			v->DoUrgentLatencyAdjustment,
			v->UrgentLatencyAdjustmentFabricClockComponent,
			v->UrgentLatencyAdjustmentFabricClockReference,
			v->FabricClock);
	//Prefetch Check
	for (i = 0; i < v->soc.num_states; ++i) {
		for (j = 0; j <= 1; ++j) {
			double VMDataOnlyReturnBWPerState;
			double HostVMInefficiencyFactor = 1;
			int NextPrefetchModeState = MinPrefetchMode;
			bool UnboundedRequestEnabledThisState = false;
			int CompressedBufferSizeInkByteThisState = 0;
			double dummy;

			v->TimeCalc = 24 / v->ProjectedDCFCLKDeepSleep[i][j];

			v->BandwidthWithoutPrefetchSupported[i][j] = true;
			if (v->TotalVActivePixelBandwidth[i][j] + v->TotalVActiveCursorBandwidth[i][j] + v->TotalMetaRowBandwidth[i][j]
					+ v->TotalDPTERowBandwidth[i][j] > v->ReturnBWPerState[i][j] || v->NotEnoughUrgentLatencyHidingA[i][j]) {
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
			}

			VMDataOnlyReturnBWPerState = dml_min(
					dml_min(
							v->ReturnBusWidth * v->DCFCLKState[i][j],
							v->FabricClockPerState[i] * v->FabricDatapathToDCNDataReturn)
							* v->PercentOfIdealFabricAndSDPPortBWReceivedAfterUrgLatency / 100.0,
					v->DRAMSpeedPerState[i] * v->NumberOfChannels * v->DRAMChannelWidth
							* v->PercentOfIdealDRAMBWReceivedAfterUrgLatencyVMDataOnly / 100.0);
			if (v->GPUVMEnable && v->HostVMEnable)
				HostVMInefficiencyFactor = v->ReturnBWPerState[i][j] / VMDataOnlyReturnBWPerState;

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
					HostVMInefficiencyFactor,
					v->HostVMMinPageSize,
					v->HostVMMaxNonCachedPageTableLevels);

			v->NextMaxVStartup = v->MaxMaxVStartup[i][j];
			do {
				v->PrefetchModePerState[i][j] = NextPrefetchModeState;
				v->MaxVStartup = v->NextMaxVStartup;

				v->TWait = CalculateTWait(
						v->PrefetchModePerState[i][j],
						v->DRAMClockChangeLatency,
						v->UrgLatency[i],
						v->SREnterPlusExitTime);

				for (k = 0; k < v->NumberOfActivePlanes; k++) {
					CalculatePrefetchSchedulePerPlane(mode_lib,
									  HostVMInefficiencyFactor,
									  i, j,	k);
				}

				for (k = 0; k < v->NumberOfActivePlanes; k++) {
					CalculateUrgentBurstFactor(
							v->swath_width_luma_ub_this_state[k],
							v->swath_width_chroma_ub_this_state[k],
							v->SwathHeightYThisState[k],
							v->SwathHeightCThisState[k],
							v->HTotal[k] / v->PixelClock[k],
							v->UrgentLatency,
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
							&v->UrgentBurstFactorChroma[k],
							&v->NotUrgentLatencyHidingPre[k]);
				}

				v->MaximumReadBandwidthWithPrefetch = 0.0;
				for (k = 0; k < v->NumberOfActivePlanes; k++) {
					v->cursor_bw_pre[k] = v->NumberOfCursors[k] * v->CursorWidth[k][0] * v->CursorBPP[k][0] / 8.0
							/ (v->HTotal[k] / v->PixelClock[k]) * v->VRatioPreY[i][j][k];

					v->MaximumReadBandwidthWithPrefetch =
							v->MaximumReadBandwidthWithPrefetch
									+ dml_max3(
											v->VActivePixelBandwidth[i][j][k]
													+ v->VActiveCursorBandwidth[i][j][k]
													+ v->NoOfDPP[i][j][k]
															* (v->meta_row_bandwidth[i][j][k]
																	+ v->dpte_row_bandwidth[i][j][k]),
											v->NoOfDPP[i][j][k] * v->prefetch_vmrow_bw[k],
											v->NoOfDPP[i][j][k]
													* (v->RequiredPrefetchPixelDataBWLuma[i][j][k]
															* v->UrgentBurstFactorLumaPre[k]
															+ v->RequiredPrefetchPixelDataBWChroma[i][j][k]
																	* v->UrgentBurstFactorChromaPre[k])
													+ v->cursor_bw_pre[k] * v->UrgentBurstFactorCursorPre[k]);
				}

				v->NotEnoughUrgentLatencyHidingPre = false;
				for (k = 0; k < v->NumberOfActivePlanes; k++) {
					if (v->NotUrgentLatencyHidingPre[k] == true) {
						v->NotEnoughUrgentLatencyHidingPre = true;
					}
				}

				v->PrefetchSupported[i][j] = true;
				if (v->BandwidthWithoutPrefetchSupported[i][j] == false || v->MaximumReadBandwidthWithPrefetch > v->ReturnBWPerState[i][j]
						|| v->NotEnoughUrgentLatencyHidingPre == 1) {
					v->PrefetchSupported[i][j] = false;
				}
				for (k = 0; k < v->NumberOfActivePlanes; k++) {
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
				for (k = 0; k < v->NumberOfActivePlanes; k++) {
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

				v->NextPrefetchMode = v->NextPrefetchMode + 1;

				if (v->PrefetchSupported[i][j] == true && v->VRatioInPrefetchSupported[i][j] == true) {
					v->BandwidthAvailableForImmediateFlip = v->ReturnBWPerState[i][j];
					for (k = 0; k < v->NumberOfActivePlanes; k++) {
						v->BandwidthAvailableForImmediateFlip = v->BandwidthAvailableForImmediateFlip
								- dml_max(
										v->VActivePixelBandwidth[i][j][k] + v->VActiveCursorBandwidth[i][j][k],
										v->NoOfDPP[i][j][k]
												* (v->RequiredPrefetchPixelDataBWLuma[i][j][k]
														* v->UrgentBurstFactorLumaPre[k]
														+ v->RequiredPrefetchPixelDataBWChroma[i][j][k]
																* v->UrgentBurstFactorChromaPre[k])
												+ v->cursor_bw_pre[k] * v->UrgentBurstFactorCursorPre[k]);
					}
					v->TotImmediateFlipBytes = 0.0;
					for (k = 0; k < v->NumberOfActivePlanes; k++) {
						v->TotImmediateFlipBytes = v->TotImmediateFlipBytes
								+ v->NoOfDPP[i][j][k] * v->PDEAndMetaPTEBytesPerFrame[i][j][k] + v->MetaRowBytes[i][j][k]
								+ v->DPTEBytesPerRow[i][j][k];
					}

					for (k = 0; k < v->NumberOfActivePlanes; k++) {
						CalculateFlipSchedule(
								mode_lib,
								k,
								HostVMInefficiencyFactor,
								v->ExtraLatency,
								v->UrgLatency[i],
								v->PDEAndMetaPTEBytesPerFrame[i][j][k],
								v->MetaRowBytes[i][j][k],
								v->DPTEBytesPerRow[i][j][k]);
					}
					v->total_dcn_read_bw_with_flip = 0.0;
					for (k = 0; k < v->NumberOfActivePlanes; k++) {
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
					for (k = 0; k < v->NumberOfActivePlanes; k++) {
						if (v->ImmediateFlipSupportedForPipe[k] == false) {
							v->ImmediateFlipSupportedForState[i][j] = false;
						}
					}
				} else {
					v->ImmediateFlipSupportedForState[i][j] = false;
				}

				if (v->MaxVStartup <= __DML_VBA_MIN_VSTARTUP__ || v->AnyLinesForVMOrRowTooLarge == false) {
					v->NextMaxVStartup = v->MaxMaxVStartup[i][j];
					NextPrefetchModeState = NextPrefetchModeState + 1;
				} else {
					v->NextMaxVStartup = v->NextMaxVStartup - 1;
				}
				v->NextPrefetchMode = v->NextPrefetchMode + 1;
			} while (!((v->PrefetchSupported[i][j] == true && v->DynamicMetadataSupported[i][j] == true && v->VRatioInPrefetchSupported[i][j] == true
					&& ((v->HostVMEnable == false &&
							v->ImmediateFlipRequirement[0] != dm_immediate_flip_required)
							|| v->ImmediateFlipSupportedForState[i][j] == true))
					|| (v->NextMaxVStartup == v->MaxMaxVStartup[i][j] && NextPrefetchModeState > MaxPrefetchMode)));

			CalculateUnboundedRequestAndCompressedBufferSize(
					v->DETBufferSizeInKByte[0],
					v->ConfigReturnBufferSizeInKByte,
					v->UseUnboundedRequesting,
					v->TotalNumberOfActiveDPP[i][j],
					NoChroma,
					v->MaxNumDPP,
					v->CompressedBufferSegmentSizeInkByte,
					v->Output,
					&UnboundedRequestEnabledThisState,
					&CompressedBufferSizeInkByteThisState);

			CalculateWatermarksAndDRAMSpeedChangeSupport(
					mode_lib,
					v->PrefetchModePerState[i][j],
					v->DCFCLKState[i][j],
					v->ReturnBWPerState[i][j],
					v->UrgLatency[i],
					v->ExtraLatency,
					v->SOCCLKPerState[i],
					v->ProjectedDCFCLKDeepSleep[i][j],
					v->DETBufferSizeYThisState,
					v->DETBufferSizeCThisState,
					v->SwathHeightYThisState,
					v->SwathHeightCThisState,
					v->SwathWidthYThisState,
					v->SwathWidthCThisState,
					v->NoOfDPPThisState,
					v->BytePerPixelInDETY,
					v->BytePerPixelInDETC,
					UnboundedRequestEnabledThisState,
					CompressedBufferSizeInkByteThisState,
					&v->DRAMClockChangeSupport[i][j],
					&dummy,
					&dummy,
					&dummy,
					&dummy);
		}
	}

	/*PTE Buffer Size Check*/
	for (i = 0; i < v->soc.num_states; i++) {
		for (j = 0; j < 2; j++) {
			v->PTEBufferSizeNotExceeded[i][j] = true;
			for (k = 0; k < v->NumberOfActivePlanes; k++) {
				if (v->PTEBufferSizeNotExceededY[i][j][k] == false || v->PTEBufferSizeNotExceededC[i][j][k] == false) {
					v->PTEBufferSizeNotExceeded[i][j] = false;
				}
			}
		}
	}

	/*Cursor Support Check*/
	v->CursorSupport = true;
	for (k = 0; k < v->NumberOfActivePlanes; k++) {
		if (v->CursorWidth[k][0] > 0.0) {
			if (v->CursorBPP[k][0] == 64 && v->Cursor64BppSupport == false) {
				v->CursorSupport = false;
			}
		}
	}

	/*Valid Pitch Check*/
	v->PitchSupport = true;
	for (k = 0; k < v->NumberOfActivePlanes; k++) {
		v->AlignedYPitch[k] = dml_ceil(dml_max(v->PitchY[k], v->SurfaceWidthY[k]), v->MacroTileWidthY[k]);
		if (v->DCCEnable[k] == true) {
			v->AlignedDCCMetaPitchY[k] = dml_ceil(dml_max(v->DCCMetaPitchY[k], v->SurfaceWidthY[k]), 64.0 * v->Read256BlockWidthY[k]);
		} else {
			v->AlignedDCCMetaPitchY[k] = v->DCCMetaPitchY[k];
		}
		if (v->SourcePixelFormat[k] != dm_444_64 && v->SourcePixelFormat[k] != dm_444_32 && v->SourcePixelFormat[k] != dm_444_16
				&& v->SourcePixelFormat[k] != dm_mono_16 && v->SourcePixelFormat[k] != dm_rgbe
				&& v->SourcePixelFormat[k] != dm_mono_8) {
			v->AlignedCPitch[k] = dml_ceil(dml_max(v->PitchC[k], v->SurfaceWidthC[k]), v->MacroTileWidthC[k]);
			if (v->DCCEnable[k] == true) {
				v->AlignedDCCMetaPitchC[k] = dml_ceil(
						dml_max(v->DCCMetaPitchC[k], v->SurfaceWidthC[k]),
						64.0 * v->Read256BlockWidthC[k]);
			} else {
				v->AlignedDCCMetaPitchC[k] = v->DCCMetaPitchC[k];
			}
		} else {
			v->AlignedCPitch[k] = v->PitchC[k];
			v->AlignedDCCMetaPitchC[k] = v->DCCMetaPitchC[k];
		}
		if (v->AlignedYPitch[k] > v->PitchY[k] || v->AlignedCPitch[k] > v->PitchC[k]
				|| v->AlignedDCCMetaPitchY[k] > v->DCCMetaPitchY[k] || v->AlignedDCCMetaPitchC[k] > v->DCCMetaPitchC[k]) {
			v->PitchSupport = false;
		}
	}

	for (k = 0; k < v->NumberOfActivePlanes; k++) {
		if (v->ViewportWidth[k] > v->SurfaceWidthY[k] || v->ViewportHeight[k] > v->SurfaceHeightY[k]) {
			ViewportExceedsSurface = true;
			if (v->SourcePixelFormat[k] != dm_444_64 && v->SourcePixelFormat[k] != dm_444_32
					&& v->SourcePixelFormat[k] != dm_444_16 && v->SourcePixelFormat[k] != dm_444_8
					&& v->SourcePixelFormat[k] != dm_rgbe) {
				if (v->ViewportWidthChroma[k] > v->SurfaceWidthC[k]
						|| v->ViewportHeightChroma[k] > v->SurfaceHeightC[k]) {
					ViewportExceedsSurface = true;
				}
			}
		}
	}

	/*Mode Support, Voltage State and SOC Configuration*/
	for (i = v->soc.num_states - 1; i >= 0; i--) {
		for (j = 0; j < 2; j++) {
			if (v->ScaleRatioAndTapsSupport == true && v->SourceFormatPixelAndScanSupport == true && v->ViewportSizeSupport[i][j] == true
					&& v->LinkCapacitySupport[i] == true && !P2IWith420 && !DSCOnlyIfNecessaryWithBPP
					&& !DSC422NativeNotSupported && v->ODMCombine4To1SupportCheckOK[i] == true && v->NotEnoughDSCUnits[i] == false
					&& v->DTBCLKRequiredMoreThanSupported[i] == false
					&& v->ROBSupport[i][j] == true && v->DISPCLK_DPPCLK_Support[i][j] == true
					&& v->TotalAvailablePipesSupport[i][j] == true && EnoughWritebackUnits == true
					&& v->WritebackLatencySupport == true && v->WritebackScaleRatioAndTapsSupport == true
					&& v->CursorSupport == true && v->PitchSupport == true && ViewportExceedsSurface == false
					&& v->PrefetchSupported[i][j] == true && v->DynamicMetadataSupported[i][j] == true
					&& v->TotalVerticalActiveBandwidthSupport[i][j] == true && v->VRatioInPrefetchSupported[i][j] == true
					&& v->PTEBufferSizeNotExceeded[i][j] == true && v->NonsupportedDSCInputBPC == false
					&& ((v->HostVMEnable == false
					&& v->ImmediateFlipRequirement[0] != dm_immediate_flip_required)
							|| v->ImmediateFlipSupportedForState[i][j] == true)
					&& FMTBufferExceeded == false) {
				v->ModeSupport[i][j] = true;
			} else {
				v->ModeSupport[i][j] = false;
			}
		}
	}

	{
		unsigned int MaximumMPCCombine = 0;

		for (i = v->soc.num_states; i >= 0; i--) {
			if (i == v->soc.num_states || v->ModeSupport[i][0] == true || v->ModeSupport[i][1] == true) {
				v->VoltageLevel = i;
				v->ModeIsSupported = v->ModeSupport[i][0] == true || v->ModeSupport[i][1] == true;
				if (v->ModeSupport[i][0] == true) {
					MaximumMPCCombine = 0;
				} else {
					MaximumMPCCombine = 1;
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
		double DCFCLK,
		double ReturnBW,
		double UrgentLatency,
		double ExtraLatency,
		double SOCCLK,
		double DCFCLKDeepSleep,
		unsigned int DETBufferSizeY[],
		unsigned int DETBufferSizeC[],
		unsigned int SwathHeightY[],
		unsigned int SwathHeightC[],
		double SwathWidthY[],
		double SwathWidthC[],
		unsigned int DPPPerPlane[],
		double BytePerPixelDETY[],
		double BytePerPixelDETC[],
		bool UnboundedRequestEnabled,
		unsigned int CompressedBufferSizeInkByte,
		enum clock_change_support *DRAMClockChangeSupport,
		double *StutterExitWatermark,
		double *StutterEnterPlusExitWatermark,
		double *Z8StutterExitWatermark,
		double *Z8StutterEnterPlusExitWatermark)
{
	struct vba_vars_st *v = &mode_lib->vba;
	double EffectiveLBLatencyHidingY;
	double EffectiveLBLatencyHidingC;
	double LinesInDETY[DC__NUM_DPP__MAX];
	double LinesInDETC;
	unsigned int LinesInDETYRoundedDownToSwath[DC__NUM_DPP__MAX];
	unsigned int LinesInDETCRoundedDownToSwath;
	double FullDETBufferingTimeY;
	double FullDETBufferingTimeC;
	double ActiveDRAMClockChangeLatencyMarginY;
	double ActiveDRAMClockChangeLatencyMarginC;
	double WritebackDRAMClockChangeLatencyMargin;
	double PlaneWithMinActiveDRAMClockChangeMargin;
	double SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank;
	double WritebackDRAMClockChangeLatencyHiding;
	double TotalPixelBW = 0.0;
	int k, j;

	v->UrgentWatermark = UrgentLatency + ExtraLatency;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: UrgentLatency = %f\n", __func__, UrgentLatency);
	dml_print("DML::%s: ExtraLatency = %f\n", __func__, ExtraLatency);
	dml_print("DML::%s: UrgentWatermark = %f\n", __func__, v->UrgentWatermark);
#endif

	v->DRAMClockChangeWatermark = v->DRAMClockChangeLatency + v->UrgentWatermark;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: v->DRAMClockChangeLatency = %f\n", __func__, v->DRAMClockChangeLatency);
	dml_print("DML::%s: DRAMClockChangeWatermark = %f\n", __func__, v->DRAMClockChangeWatermark);
#endif

	v->TotalActiveWriteback = 0;
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		if (v->WritebackEnable[k] == true) {
			v->TotalActiveWriteback = v->TotalActiveWriteback + 1;
		}
	}

	if (v->TotalActiveWriteback <= 1) {
		v->WritebackUrgentWatermark = v->WritebackLatency;
	} else {
		v->WritebackUrgentWatermark = v->WritebackLatency + v->WritebackChunkSize * 1024.0 / 32.0 / SOCCLK;
	}

	if (v->TotalActiveWriteback <= 1) {
		v->WritebackDRAMClockChangeWatermark = v->DRAMClockChangeLatency + v->WritebackLatency;
	} else {
		v->WritebackDRAMClockChangeWatermark = v->DRAMClockChangeLatency + v->WritebackLatency + v->WritebackChunkSize * 1024.0 / 32.0 / SOCCLK;
	}

	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		TotalPixelBW = TotalPixelBW
				+ DPPPerPlane[k] * (SwathWidthY[k] * BytePerPixelDETY[k] * v->VRatio[k] + SwathWidthC[k] * BytePerPixelDETC[k] * v->VRatioChroma[k])
						/ (v->HTotal[k] / v->PixelClock[k]);
	}

	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		double EffectiveDETBufferSizeY = DETBufferSizeY[k];

		v->LBLatencyHidingSourceLinesY = dml_min(
				(double) v->MaxLineBufferLines,
				dml_floor(v->LineBufferSize / v->LBBitPerPixel[k] / (SwathWidthY[k] / dml_max(v->HRatio[k], 1.0)), 1)) - (v->vtaps[k] - 1);

		v->LBLatencyHidingSourceLinesC = dml_min(
				(double) v->MaxLineBufferLines,
				dml_floor(v->LineBufferSize / v->LBBitPerPixel[k] / (SwathWidthC[k] / dml_max(v->HRatioChroma[k], 1.0)), 1)) - (v->VTAPsChroma[k] - 1);

		EffectiveLBLatencyHidingY = v->LBLatencyHidingSourceLinesY / v->VRatio[k] * (v->HTotal[k] / v->PixelClock[k]);

		EffectiveLBLatencyHidingC = v->LBLatencyHidingSourceLinesC / v->VRatioChroma[k] * (v->HTotal[k] / v->PixelClock[k]);

		if (UnboundedRequestEnabled) {
			EffectiveDETBufferSizeY = EffectiveDETBufferSizeY
					+ CompressedBufferSizeInkByte * 1024 * SwathWidthY[k] * BytePerPixelDETY[k] * v->VRatio[k] / (v->HTotal[k] / v->PixelClock[k]) / TotalPixelBW;
		}

		LinesInDETY[k] = (double) EffectiveDETBufferSizeY / BytePerPixelDETY[k] / SwathWidthY[k];
		LinesInDETYRoundedDownToSwath[k] = dml_floor(LinesInDETY[k], SwathHeightY[k]);
		FullDETBufferingTimeY = LinesInDETYRoundedDownToSwath[k] * (v->HTotal[k] / v->PixelClock[k]) / v->VRatio[k];
		if (BytePerPixelDETC[k] > 0) {
			LinesInDETC = v->DETBufferSizeC[k] / BytePerPixelDETC[k] / SwathWidthC[k];
			LinesInDETCRoundedDownToSwath = dml_floor(LinesInDETC, SwathHeightC[k]);
			FullDETBufferingTimeC = LinesInDETCRoundedDownToSwath * (v->HTotal[k] / v->PixelClock[k]) / v->VRatioChroma[k];
		} else {
			LinesInDETC = 0;
			FullDETBufferingTimeC = 999999;
		}

		ActiveDRAMClockChangeLatencyMarginY = EffectiveLBLatencyHidingY + FullDETBufferingTimeY
				- ((double) v->DSTXAfterScaler[k] / v->HTotal[k] + v->DSTYAfterScaler[k]) * v->HTotal[k] / v->PixelClock[k] - v->UrgentWatermark - v->DRAMClockChangeWatermark;

		if (v->NumberOfActivePlanes > 1) {
			ActiveDRAMClockChangeLatencyMarginY = ActiveDRAMClockChangeLatencyMarginY
					- (1 - 1.0 / v->NumberOfActivePlanes) * SwathHeightY[k] * v->HTotal[k] / v->PixelClock[k] / v->VRatio[k];
		}

		if (BytePerPixelDETC[k] > 0) {
			ActiveDRAMClockChangeLatencyMarginC = EffectiveLBLatencyHidingC + FullDETBufferingTimeC
					- ((double) v->DSTXAfterScaler[k] / v->HTotal[k] + v->DSTYAfterScaler[k]) * v->HTotal[k] / v->PixelClock[k] - v->UrgentWatermark - v->DRAMClockChangeWatermark;

			if (v->NumberOfActivePlanes > 1) {
				ActiveDRAMClockChangeLatencyMarginC = ActiveDRAMClockChangeLatencyMarginC
						- (1 - 1.0 / v->NumberOfActivePlanes) * SwathHeightC[k] * v->HTotal[k] / v->PixelClock[k] / v->VRatioChroma[k];
			}
			v->ActiveDRAMClockChangeLatencyMargin[k] = dml_min(ActiveDRAMClockChangeLatencyMarginY, ActiveDRAMClockChangeLatencyMarginC);
		} else {
			v->ActiveDRAMClockChangeLatencyMargin[k] = ActiveDRAMClockChangeLatencyMarginY;
		}

		if (v->WritebackEnable[k] == true) {
			WritebackDRAMClockChangeLatencyHiding = v->WritebackInterfaceBufferSize * 1024
					/ (v->WritebackDestinationWidth[k] * v->WritebackDestinationHeight[k] / (v->WritebackSourceHeight[k] * v->HTotal[k] / v->PixelClock[k]) * 4);
			if (v->WritebackPixelFormat[k] == dm_444_64) {
				WritebackDRAMClockChangeLatencyHiding = WritebackDRAMClockChangeLatencyHiding / 2;
			}
			WritebackDRAMClockChangeLatencyMargin = WritebackDRAMClockChangeLatencyHiding - v->WritebackDRAMClockChangeWatermark;
			v->ActiveDRAMClockChangeLatencyMargin[k] = dml_min(v->ActiveDRAMClockChangeLatencyMargin[k], WritebackDRAMClockChangeLatencyMargin);
		}
	}

	v->MinActiveDRAMClockChangeMargin = 999999;
	PlaneWithMinActiveDRAMClockChangeMargin = 0;
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		if (v->ActiveDRAMClockChangeLatencyMargin[k] < v->MinActiveDRAMClockChangeMargin) {
			v->MinActiveDRAMClockChangeMargin = v->ActiveDRAMClockChangeLatencyMargin[k];
			if (v->BlendingAndTiming[k] == k) {
				PlaneWithMinActiveDRAMClockChangeMargin = k;
			} else {
				for (j = 0; j < v->NumberOfActivePlanes; ++j) {
					if (v->BlendingAndTiming[k] == j) {
						PlaneWithMinActiveDRAMClockChangeMargin = j;
					}
				}
			}
		}
	}

	v->MinActiveDRAMClockChangeLatencySupported = v->MinActiveDRAMClockChangeMargin + v->DRAMClockChangeLatency ;

	SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank = 999999;
	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		if (!((k == PlaneWithMinActiveDRAMClockChangeMargin) && (v->BlendingAndTiming[k] == k)) && !(v->BlendingAndTiming[k] == PlaneWithMinActiveDRAMClockChangeMargin)
				&& v->ActiveDRAMClockChangeLatencyMargin[k] < SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank) {
			SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank = v->ActiveDRAMClockChangeLatencyMargin[k];
		}
	}

	v->TotalNumberOfActiveOTG = 0;

	for (k = 0; k < v->NumberOfActivePlanes; ++k) {
		if (v->BlendingAndTiming[k] == k) {
			v->TotalNumberOfActiveOTG = v->TotalNumberOfActiveOTG + 1;
		}
	}

	if (v->MinActiveDRAMClockChangeMargin > 0 && PrefetchMode == 0) {
		*DRAMClockChangeSupport = dm_dram_clock_change_vactive;
	} else if ((v->SynchronizedVBlank == true || v->TotalNumberOfActiveOTG == 1
			|| SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank > 0) && PrefetchMode == 0) {
		*DRAMClockChangeSupport = dm_dram_clock_change_vblank;
	} else {
		*DRAMClockChangeSupport = dm_dram_clock_change_unsupported;
	}

	*StutterExitWatermark = v->SRExitTime + ExtraLatency + 10 / DCFCLKDeepSleep;
	*StutterEnterPlusExitWatermark = (v->SREnterPlusExitTime + ExtraLatency + 10 / DCFCLKDeepSleep);
	*Z8StutterExitWatermark = v->SRExitZ8Time + ExtraLatency + 10 / DCFCLKDeepSleep;
	*Z8StutterEnterPlusExitWatermark = v->SREnterPlusExitZ8Time + ExtraLatency + 10 / DCFCLKDeepSleep;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: StutterExitWatermark = %f\n", __func__, *StutterExitWatermark);
	dml_print("DML::%s: StutterEnterPlusExitWatermark = %f\n", __func__, *StutterEnterPlusExitWatermark);
	dml_print("DML::%s: Z8StutterExitWatermark = %f\n", __func__, *Z8StutterExitWatermark);
	dml_print("DML::%s: Z8StutterEnterPlusExitWatermark = %f\n", __func__, *Z8StutterEnterPlusExitWatermark);
#endif
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
	struct vba_vars_st *v = &mode_lib->vba;
	double DisplayPipeLineDeliveryTimeLuma;
	double DisplayPipeLineDeliveryTimeChroma;
	double ReadBandwidth = 0.0;
	int k;

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
			v->DCFCLKDeepSleepPerPlane[k] = dml_max(__DML_MIN_DCFCLK_FACTOR__ * SwathWidthY[k] * BytePerPixelY[k] / 32.0 / DisplayPipeLineDeliveryTimeLuma,
			__DML_MIN_DCFCLK_FACTOR__ * SwathWidthC[k] * BytePerPixelC[k] / 32.0 / DisplayPipeLineDeliveryTimeChroma);
		} else {
			v->DCFCLKDeepSleepPerPlane[k] = __DML_MIN_DCFCLK_FACTOR__ * SwathWidthY[k] * BytePerPixelY[k] / 64.0 / DisplayPipeLineDeliveryTimeLuma;
		}
		v->DCFCLKDeepSleepPerPlane[k] = dml_max(v->DCFCLKDeepSleepPerPlane[k], PixelClock[k] / 16);

	}

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		ReadBandwidth = ReadBandwidth + ReadBandwidthLuma[k] + ReadBandwidthChroma[k];
	}

	*DCFCLKDeepSleep = dml_max(8.0, __DML_MIN_DCFCLK_FACTOR__ * ReadBandwidth / ReturnBusWidth);

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		*DCFCLKDeepSleep = dml_max(*DCFCLKDeepSleep, v->DCFCLKDeepSleepPerPlane[k]);
	}
}

static void CalculateUrgentBurstFactor(
		int swath_width_luma_ub,
		int swath_width_chroma_ub,
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
	double LinesInDETLuma;
	double LinesInDETChroma;
	unsigned int LinesInCursorBuffer;
	double CursorBufferSizeInTime;
	double DETBufferSizeInTimeLuma;
	double DETBufferSizeInTimeChroma;

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
		unsigned int CursorWidth[][DC__NUM_CURSOR__MAX],
		unsigned int CursorBPP[][DC__NUM_CURSOR__MAX],
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
	double req_per_swath_ub;
	int k;

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
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d : HRatio = %f\n", __func__, k, HRatio[k]);
		dml_print("DML::%s: k=%d : VRatio = %f\n", __func__, k, VRatio[k]);
		dml_print("DML::%s: k=%d : HRatioChroma = %f\n", __func__, k, HRatioChroma[k]);
		dml_print("DML::%s: k=%d : VRatioChroma = %f\n", __func__, k, VRatioChroma[k]);
		dml_print("DML::%s: k=%d : DisplayPipeLineDeliveryTimeLuma = %f\n", __func__, k, DisplayPipeLineDeliveryTimeLuma[k]);
		dml_print("DML::%s: k=%d : DisplayPipeLineDeliveryTimeLumaPrefetch = %f\n", __func__, k, DisplayPipeLineDeliveryTimeLumaPrefetch[k]);
		dml_print("DML::%s: k=%d : DisplayPipeLineDeliveryTimeChroma = %f\n", __func__, k, DisplayPipeLineDeliveryTimeChroma[k]);
		dml_print("DML::%s: k=%d : DisplayPipeLineDeliveryTimeChromaPrefetch = %f\n", __func__, k, DisplayPipeLineDeliveryTimeChromaPrefetch[k]);
		dml_print("DML::%s: k=%d : DisplayPipeRequestDeliveryTimeLuma = %f\n", __func__, k, DisplayPipeRequestDeliveryTimeLuma[k]);
		dml_print("DML::%s: k=%d : DisplayPipeRequestDeliveryTimeLumaPrefetch = %f\n", __func__, k, DisplayPipeRequestDeliveryTimeLumaPrefetch[k]);
		dml_print("DML::%s: k=%d : DisplayPipeRequestDeliveryTimeChroma = %f\n", __func__, k, DisplayPipeRequestDeliveryTimeChroma[k]);
		dml_print("DML::%s: k=%d : DisplayPipeRequestDeliveryTimeChromaPrefetch = %f\n", __func__, k, DisplayPipeRequestDeliveryTimeChromaPrefetch[k]);
#endif
	}

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		int cursor_req_per_width;

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
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d : NumberOfCursors = %d\n", __func__, k, NumberOfCursors[k]);
		dml_print("DML::%s: k=%d : CursorRequestDeliveryTime = %f\n", __func__, k, CursorRequestDeliveryTime[k]);
		dml_print("DML::%s: k=%d : CursorRequestDeliveryTimePrefetch = %f\n", __func__, k, CursorRequestDeliveryTimePrefetch[k]);
#endif
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
	unsigned int meta_chunk_width;
	unsigned int min_meta_chunk_width;
	unsigned int meta_chunk_per_row_int;
	unsigned int meta_row_remainder;
	unsigned int meta_chunk_threshold;
	unsigned int meta_chunks_per_row_ub;
	unsigned int meta_chunk_width_chroma;
	unsigned int min_meta_chunk_width_chroma;
	unsigned int meta_chunk_per_row_int_chroma;
	unsigned int meta_row_remainder_chroma;
	unsigned int meta_chunk_threshold_chroma;
	unsigned int meta_chunks_per_row_ub_chroma;
	unsigned int dpte_group_width_luma;
	unsigned int dpte_groups_per_row_luma_ub;
	unsigned int dpte_group_width_chroma;
	unsigned int dpte_groups_per_row_chroma_ub;
	int k;

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
	int num_group_per_lower_vm_stage;
	int num_req_per_lower_vm_stage;
	int k;

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (GPUVMEnable == true && (DCCEnable[k] == true || GPUVMMaxPageTableLevels > 1)) {
			if (DCCEnable[k] == false) {
				if (BytePerPixelC[k] > 0) {
					num_group_per_lower_vm_stage = dml_ceil((double) (dpde0_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1)
							+ dml_ceil((double) (dpde0_bytes_per_frame_ub_c[k]) / (double) (vm_group_bytes[k]), 1);
				} else {
					num_group_per_lower_vm_stage = dml_ceil((double) (dpde0_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1);
				}
			} else {
				if (GPUVMMaxPageTableLevels == 1) {
					if (BytePerPixelC[k] > 0) {
						num_group_per_lower_vm_stage = dml_ceil((double) (meta_pte_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1)
								+ dml_ceil((double) (meta_pte_bytes_per_frame_ub_c[k]) / (double) (vm_group_bytes[k]), 1);
					} else {
						num_group_per_lower_vm_stage = dml_ceil((double) (meta_pte_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1);
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
						num_req_per_lower_vm_stage = meta_pte_bytes_per_frame_ub_l[k] / 64 + meta_pte_bytes_per_frame_ub_c[k] / 64;
					} else {
						num_req_per_lower_vm_stage = meta_pte_bytes_per_frame_ub_l[k] / 64;
					}
				} else {
					if (BytePerPixelC[k] > 0) {
						num_req_per_lower_vm_stage = dpde0_bytes_per_frame_ub_l[k] / 64 + dpde0_bytes_per_frame_ub_c[k] / 64
								+ meta_pte_bytes_per_frame_ub_l[k] / 64 + meta_pte_bytes_per_frame_ub_c[k] / 64;
					} else {
						num_req_per_lower_vm_stage = dpde0_bytes_per_frame_ub_l[k] / 64 + meta_pte_bytes_per_frame_ub_l[k] / 64;
					}
				}
			}

			TimePerVMGroupVBlank[k] = DestinationLinesToRequestVMInVBlank[k] * HTotal[k] / PixelClock[k] / num_group_per_lower_vm_stage;
			TimePerVMGroupFlip[k] = DestinationLinesToRequestVMInImmediateFlip[k] * HTotal[k] / PixelClock[k] / num_group_per_lower_vm_stage;
			TimePerVMRequestVBlank[k] = DestinationLinesToRequestVMInVBlank[k] * HTotal[k] / PixelClock[k] / num_req_per_lower_vm_stage;
			TimePerVMRequestFlip[k] = DestinationLinesToRequestVMInImmediateFlip[k] * HTotal[k] / PixelClock[k] / num_req_per_lower_vm_stage;

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
		struct display_mode_lib *mode_lib,
		int CompressedBufferSizeInkByte,
		bool UnboundedRequestEnabled,
		int ConfigReturnBufferSizeInKByte,
		int MetaFIFOSizeInKEntries,
		int ZeroSizeBufferEntries,
		int NumberOfActivePlanes,
		int ROBBufferSizeInKByte,
		double TotalDataReadBandwidth,
		double DCFCLK,
		double ReturnBW,
		double COMPBUF_RESERVED_SPACE_64B,
		double COMPBUF_RESERVED_SPACE_ZS,
		double SRExitTime,
		double SRExitZ8Time,
		bool SynchronizedVBlank,
		double Z8StutterEnterPlusExitWatermark,
		double StutterEnterPlusExitWatermark,
		bool ProgressiveToInterlaceUnitInOPP,
		bool Interlace[],
		double MinTTUVBlank[],
		int DPPPerPlane[],
		unsigned int DETBufferSizeY[],
		int BytePerPixelY[],
		double BytePerPixelDETY[],
		double SwathWidthY[],
		int SwathHeightY[],
		int SwathHeightC[],
		double NetDCCRateLuma[],
		double NetDCCRateChroma[],
		double DCCFractionOfZeroSizeRequestsLuma[],
		double DCCFractionOfZeroSizeRequestsChroma[],
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
		int *NumberOfStutterBurstsPerFrame,
		double *Z8StutterEfficiencyNotIncludingVBlank,
		double *Z8StutterEfficiency,
		int *Z8NumberOfStutterBurstsPerFrame,
		double *StutterPeriod)
{
	struct vba_vars_st *v = &mode_lib->vba;

	double DETBufferingTimeY;
	double SwathWidthYCriticalPlane = 0;
	double VActiveTimeCriticalPlane = 0;
	double FrameTimeCriticalPlane = 0;
	int BytePerPixelYCriticalPlane = 0;
	double LinesToFinishSwathTransferStutterCriticalPlane = 0;
	double MinTTUVBlankCriticalPlane = 0;
	double TotalCompressedReadBandwidth;
	double TotalRowReadBandwidth;
	double AverageDCCCompressionRate;
	double EffectiveCompressedBufferSize;
	double PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer;
	double StutterBurstTime;
	int TotalActiveWriteback;
	double LinesInDETY;
	double LinesInDETYRoundedDownToSwath;
	double MaximumEffectiveCompressionLuma;
	double MaximumEffectiveCompressionChroma;
	double TotalZeroSizeRequestReadBandwidth;
	double TotalZeroSizeCompressedReadBandwidth;
	double AverageDCCZeroSizeFraction;
	double AverageZeroSizeCompressionRate;
	int TotalNumberOfActiveOTG = 0;
	double LastStutterPeriod = 0.0;
	double LastZ8StutterPeriod = 0.0;
	int k;

	TotalZeroSizeRequestReadBandwidth = 0;
	TotalZeroSizeCompressedReadBandwidth = 0;
	TotalRowReadBandwidth = 0;
	TotalCompressedReadBandwidth = 0;

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (DCCEnable[k] == true) {
			if ((SourceScan[k] == dm_vert && BlockWidth256BytesY[k] > SwathHeightY[k]) || (SourceScan[k] != dm_vert && BlockHeight256BytesY[k] > SwathHeightY[k])
					|| DCCYMaxUncompressedBlock[k] < 256) {
				MaximumEffectiveCompressionLuma = 2;
			} else {
				MaximumEffectiveCompressionLuma = 4;
			}
			TotalCompressedReadBandwidth = TotalCompressedReadBandwidth + ReadBandwidthPlaneLuma[k] / dml_min(NetDCCRateLuma[k], MaximumEffectiveCompressionLuma);
			TotalZeroSizeRequestReadBandwidth = TotalZeroSizeRequestReadBandwidth + ReadBandwidthPlaneLuma[k] * DCCFractionOfZeroSizeRequestsLuma[k];
			TotalZeroSizeCompressedReadBandwidth = TotalZeroSizeCompressedReadBandwidth
					+ ReadBandwidthPlaneLuma[k] * DCCFractionOfZeroSizeRequestsLuma[k] / MaximumEffectiveCompressionLuma;
			if (ReadBandwidthPlaneChroma[k] > 0) {
				if ((SourceScan[k] == dm_vert && BlockWidth256BytesC[k] > SwathHeightC[k])
						|| (SourceScan[k] != dm_vert && BlockHeight256BytesC[k] > SwathHeightC[k]) || DCCCMaxUncompressedBlock[k] < 256) {
					MaximumEffectiveCompressionChroma = 2;
				} else {
					MaximumEffectiveCompressionChroma = 4;
				}
				TotalCompressedReadBandwidth = TotalCompressedReadBandwidth
						+ ReadBandwidthPlaneChroma[k] / dml_min(NetDCCRateChroma[k], MaximumEffectiveCompressionChroma);
				TotalZeroSizeRequestReadBandwidth = TotalZeroSizeRequestReadBandwidth + ReadBandwidthPlaneChroma[k] * DCCFractionOfZeroSizeRequestsChroma[k];
				TotalZeroSizeCompressedReadBandwidth = TotalZeroSizeCompressedReadBandwidth
						+ ReadBandwidthPlaneChroma[k] * DCCFractionOfZeroSizeRequestsChroma[k] / MaximumEffectiveCompressionChroma;
			}
		} else {
			TotalCompressedReadBandwidth = TotalCompressedReadBandwidth + ReadBandwidthPlaneLuma[k] + ReadBandwidthPlaneChroma[k];
		}
		TotalRowReadBandwidth = TotalRowReadBandwidth + DPPPerPlane[k] * (meta_row_bw[k] + dpte_row_bw[k]);
	}

	AverageDCCCompressionRate = TotalDataReadBandwidth / TotalCompressedReadBandwidth;
	AverageDCCZeroSizeFraction = TotalZeroSizeRequestReadBandwidth / TotalDataReadBandwidth;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: TotalCompressedReadBandwidth = %f\n", __func__, TotalCompressedReadBandwidth);
	dml_print("DML::%s: TotalZeroSizeRequestReadBandwidth = %f\n", __func__, TotalZeroSizeRequestReadBandwidth);
	dml_print("DML::%s: TotalZeroSizeCompressedReadBandwidth = %f\n", __func__, TotalZeroSizeCompressedReadBandwidth);
	dml_print("DML::%s: MaximumEffectiveCompressionLuma = %f\n", __func__, MaximumEffectiveCompressionLuma);
	dml_print("DML::%s: MaximumEffectiveCompressionChroma = %f\n", __func__, MaximumEffectiveCompressionChroma);
	dml_print("DML::%s: AverageDCCCompressionRate = %f\n", __func__, AverageDCCCompressionRate);
	dml_print("DML::%s: AverageDCCZeroSizeFraction = %f\n", __func__, AverageDCCZeroSizeFraction);
	dml_print("DML::%s: CompressedBufferSizeInkByte = %d\n", __func__, CompressedBufferSizeInkByte);
#endif

	if (AverageDCCZeroSizeFraction == 1) {
		AverageZeroSizeCompressionRate = TotalZeroSizeRequestReadBandwidth / TotalZeroSizeCompressedReadBandwidth;
		EffectiveCompressedBufferSize = MetaFIFOSizeInKEntries * 1024 * 64 * AverageZeroSizeCompressionRate + (ZeroSizeBufferEntries - COMPBUF_RESERVED_SPACE_ZS) * 64 * AverageZeroSizeCompressionRate;
	} else if (AverageDCCZeroSizeFraction > 0) {
		AverageZeroSizeCompressionRate = TotalZeroSizeRequestReadBandwidth / TotalZeroSizeCompressedReadBandwidth;
		EffectiveCompressedBufferSize = dml_min(
				CompressedBufferSizeInkByte * 1024 * AverageDCCCompressionRate,
				MetaFIFOSizeInKEntries * 1024 * 64 / (AverageDCCZeroSizeFraction / AverageZeroSizeCompressionRate + 1 / AverageDCCCompressionRate))
					+ dml_min((ROBBufferSizeInKByte * 1024 - COMPBUF_RESERVED_SPACE_64B * 64) * AverageDCCCompressionRate,
						(ZeroSizeBufferEntries - COMPBUF_RESERVED_SPACE_ZS) * 64 / (AverageDCCZeroSizeFraction / AverageZeroSizeCompressionRate));
		dml_print("DML::%s: min 1 = %f\n", __func__, CompressedBufferSizeInkByte * 1024 * AverageDCCCompressionRate);
		dml_print(
				"DML::%s: min 2 = %f\n",
				__func__,
				MetaFIFOSizeInKEntries * 1024 * 64 / (AverageDCCZeroSizeFraction / AverageZeroSizeCompressionRate + 1 / AverageDCCCompressionRate));
		dml_print("DML::%s: min 3 = %f\n", __func__, ROBBufferSizeInKByte * 1024 * AverageDCCCompressionRate);
		dml_print("DML::%s: min 4 = %f\n", __func__, ZeroSizeBufferEntries * 64 / (AverageDCCZeroSizeFraction / AverageZeroSizeCompressionRate));
	} else {
		EffectiveCompressedBufferSize = dml_min(
				CompressedBufferSizeInkByte * 1024 * AverageDCCCompressionRate,
				MetaFIFOSizeInKEntries * 1024 * 64 * AverageDCCCompressionRate) + (ROBBufferSizeInKByte * 1024 - COMPBUF_RESERVED_SPACE_64B * 64) * AverageDCCCompressionRate;
		dml_print("DML::%s: min 1 = %f\n", __func__, CompressedBufferSizeInkByte * 1024 * AverageDCCCompressionRate);
		dml_print("DML::%s: min 2 = %f\n", __func__, MetaFIFOSizeInKEntries * 1024 * 64 * AverageDCCCompressionRate);
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: MetaFIFOSizeInKEntries = %d\n", __func__, MetaFIFOSizeInKEntries);
	dml_print("DML::%s: AverageZeroSizeCompressionRate = %f\n", __func__, AverageZeroSizeCompressionRate);
	dml_print("DML::%s: EffectiveCompressedBufferSize = %f\n", __func__, EffectiveCompressedBufferSize);
#endif

	*StutterPeriod = 0;
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		LinesInDETY = (DETBufferSizeY[k] + (UnboundedRequestEnabled == true ? EffectiveCompressedBufferSize : 0) * ReadBandwidthPlaneLuma[k] / TotalDataReadBandwidth)
				/ BytePerPixelDETY[k] / SwathWidthY[k];
		LinesInDETYRoundedDownToSwath = dml_floor(LinesInDETY, SwathHeightY[k]);
		DETBufferingTimeY = LinesInDETYRoundedDownToSwath * (HTotal[k] / PixelClock[k]) / VRatio[k];
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%0d DETBufferSizeY = %f\n", __func__, k, DETBufferSizeY[k]);
		dml_print("DML::%s: k=%0d BytePerPixelDETY = %f\n", __func__, k, BytePerPixelDETY[k]);
		dml_print("DML::%s: k=%0d SwathWidthY = %f\n", __func__, k, SwathWidthY[k]);
		dml_print("DML::%s: k=%0d ReadBandwidthPlaneLuma = %f\n", __func__, k, ReadBandwidthPlaneLuma[k]);
		dml_print("DML::%s: k=%0d TotalDataReadBandwidth = %f\n", __func__, k, TotalDataReadBandwidth);
		dml_print("DML::%s: k=%0d LinesInDETY = %f\n", __func__, k, LinesInDETY);
		dml_print("DML::%s: k=%0d LinesInDETYRoundedDownToSwath = %f\n", __func__, k, LinesInDETYRoundedDownToSwath);
		dml_print("DML::%s: k=%0d HTotal = %d\n", __func__, k, HTotal[k]);
		dml_print("DML::%s: k=%0d PixelClock = %f\n", __func__, k, PixelClock[k]);
		dml_print("DML::%s: k=%0d VRatio = %f\n", __func__, k, VRatio[k]);
		dml_print("DML::%s: k=%0d DETBufferingTimeY = %f\n", __func__, k, DETBufferingTimeY);
		dml_print("DML::%s: k=%0d PixelClock = %f\n", __func__, k, PixelClock[k]);
#endif

		if (k == 0 || DETBufferingTimeY < *StutterPeriod) {
			bool isInterlaceTiming = Interlace[k] && !ProgressiveToInterlaceUnitInOPP;

			*StutterPeriod = DETBufferingTimeY;
			FrameTimeCriticalPlane = (isInterlaceTiming ? dml_floor(VTotal[k] / 2.0, 1.0) : VTotal[k]) * HTotal[k] / PixelClock[k];
			VActiveTimeCriticalPlane = (isInterlaceTiming ? dml_floor(VActive[k] / 2.0, 1.0) : VActive[k]) * HTotal[k] / PixelClock[k];
			BytePerPixelYCriticalPlane = BytePerPixelY[k];
			SwathWidthYCriticalPlane = SwathWidthY[k];
			LinesToFinishSwathTransferStutterCriticalPlane = SwathHeightY[k] - (LinesInDETY - LinesInDETYRoundedDownToSwath);
			MinTTUVBlankCriticalPlane = MinTTUVBlank[k];

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: StutterPeriod = %f\n", __func__, *StutterPeriod);
			dml_print("DML::%s: MinTTUVBlankCriticalPlane = %f\n", __func__, MinTTUVBlankCriticalPlane);
			dml_print("DML::%s: FrameTimeCriticalPlane = %f\n", __func__, FrameTimeCriticalPlane);
			dml_print("DML::%s: VActiveTimeCriticalPlane = %f\n", __func__, VActiveTimeCriticalPlane);
			dml_print("DML::%s: BytePerPixelYCriticalPlane = %d\n", __func__, BytePerPixelYCriticalPlane);
			dml_print("DML::%s: SwathWidthYCriticalPlane = %f\n", __func__, SwathWidthYCriticalPlane);
			dml_print("DML::%s: LinesToFinishSwathTransferStutterCriticalPlane = %f\n", __func__, LinesToFinishSwathTransferStutterCriticalPlane);
#endif
		}
	}

	PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer = dml_min(*StutterPeriod * TotalDataReadBandwidth, EffectiveCompressedBufferSize);
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: ROBBufferSizeInKByte = %d\n", __func__, ROBBufferSizeInKByte);
	dml_print("DML::%s: AverageDCCCompressionRate = %f\n", __func__, AverageDCCCompressionRate);
	dml_print("DML::%s: StutterPeriod * TotalDataReadBandwidth = %f\n", __func__, *StutterPeriod * TotalDataReadBandwidth);
	dml_print("DML::%s: ROBBufferSizeInKByte * 1024 * AverageDCCCompressionRate + EffectiveCompressedBufferSize = %f\n", __func__, ROBBufferSizeInKByte * 1024 * AverageDCCCompressionRate + EffectiveCompressedBufferSize);
	dml_print("DML::%s: EffectiveCompressedBufferSize = %f\n", __func__, EffectiveCompressedBufferSize);
	dml_print("DML::%s: PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer = %f\n", __func__, PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer);
	dml_print("DML::%s: ReturnBW = %f\n", __func__, ReturnBW);
	dml_print("DML::%s: TotalDataReadBandwidth = %f\n", __func__, TotalDataReadBandwidth);
	dml_print("DML::%s: TotalRowReadBandwidth = %f\n", __func__, TotalRowReadBandwidth);
	dml_print("DML::%s: DCFCLK = %f\n", __func__, DCFCLK);
#endif

	StutterBurstTime = PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer / AverageDCCCompressionRate / ReturnBW
			+ (*StutterPeriod * TotalDataReadBandwidth - PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer) / (DCFCLK * 64)
			+ *StutterPeriod * TotalRowReadBandwidth / ReturnBW;
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: Part 1 = %f\n", __func__, PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer / AverageDCCCompressionRate / ReturnBW);
	dml_print("DML::%s: StutterPeriod * TotalDataReadBandwidth = %f\n", __func__, (*StutterPeriod * TotalDataReadBandwidth));
	dml_print("DML::%s: Part 2 = %f\n", __func__, (*StutterPeriod * TotalDataReadBandwidth - PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer) / (DCFCLK * 64));
	dml_print("DML::%s: Part 3 = %f\n", __func__, *StutterPeriod * TotalRowReadBandwidth / ReturnBW);
	dml_print("DML::%s: StutterBurstTime = %f\n", __func__, StutterBurstTime);
#endif
	StutterBurstTime = dml_max(StutterBurstTime, LinesToFinishSwathTransferStutterCriticalPlane * BytePerPixelYCriticalPlane * SwathWidthYCriticalPlane / ReturnBW);

	dml_print(
			"DML::%s: Time to finish residue swath=%f\n",
			__func__,
			LinesToFinishSwathTransferStutterCriticalPlane * BytePerPixelYCriticalPlane * SwathWidthYCriticalPlane / ReturnBW);

	TotalActiveWriteback = 0;
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (WritebackEnable[k]) {
			TotalActiveWriteback = TotalActiveWriteback + 1;
		}
	}

	if (TotalActiveWriteback == 0) {
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: SRExitTime = %f\n", __func__, SRExitTime);
		dml_print("DML::%s: SRExitZ8Time = %f\n", __func__, SRExitZ8Time);
		dml_print("DML::%s: StutterBurstTime = %f (final)\n", __func__, StutterBurstTime);
		dml_print("DML::%s: StutterPeriod = %f\n", __func__, *StutterPeriod);
#endif
		*StutterEfficiencyNotIncludingVBlank = dml_max(0., 1 - (SRExitTime + StutterBurstTime) / *StutterPeriod) * 100;
		*Z8StutterEfficiencyNotIncludingVBlank = dml_max(0., 1 - (SRExitZ8Time + StutterBurstTime) / *StutterPeriod) * 100;
		*NumberOfStutterBurstsPerFrame = (*StutterEfficiencyNotIncludingVBlank > 0 ? dml_ceil(VActiveTimeCriticalPlane / *StutterPeriod, 1) : 0);
		*Z8NumberOfStutterBurstsPerFrame = (*Z8StutterEfficiencyNotIncludingVBlank > 0 ? dml_ceil(VActiveTimeCriticalPlane / *StutterPeriod, 1) : 0);
	} else {
		*StutterEfficiencyNotIncludingVBlank = 0.;
		*Z8StutterEfficiencyNotIncludingVBlank = 0.;
		*NumberOfStutterBurstsPerFrame = 0;
		*Z8NumberOfStutterBurstsPerFrame = 0;
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: VActiveTimeCriticalPlane = %f\n", __func__, VActiveTimeCriticalPlane);
	dml_print("DML::%s: StutterEfficiencyNotIncludingVBlank = %f\n", __func__, *StutterEfficiencyNotIncludingVBlank);
	dml_print("DML::%s: Z8StutterEfficiencyNotIncludingVBlank = %f\n", __func__, *Z8StutterEfficiencyNotIncludingVBlank);
	dml_print("DML::%s: NumberOfStutterBurstsPerFrame = %d\n", __func__, *NumberOfStutterBurstsPerFrame);
	dml_print("DML::%s: Z8NumberOfStutterBurstsPerFrame = %d\n", __func__, *Z8NumberOfStutterBurstsPerFrame);
#endif

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (v->BlendingAndTiming[k] == k) {
			TotalNumberOfActiveOTG = TotalNumberOfActiveOTG + 1;
		}
	}

	if (*StutterEfficiencyNotIncludingVBlank > 0) {
		LastStutterPeriod = VActiveTimeCriticalPlane - (*NumberOfStutterBurstsPerFrame - 1) * *StutterPeriod;

		if ((SynchronizedVBlank || TotalNumberOfActiveOTG == 1) && LastStutterPeriod + MinTTUVBlankCriticalPlane > StutterEnterPlusExitWatermark) {
			*StutterEfficiency = (1 - (*NumberOfStutterBurstsPerFrame * SRExitTime + StutterBurstTime * VActiveTimeCriticalPlane
					/ *StutterPeriod) / FrameTimeCriticalPlane) * 100;
		} else {
			*StutterEfficiency = *StutterEfficiencyNotIncludingVBlank;
		}
	} else {
		*StutterEfficiency = 0;
	}

	if (*Z8StutterEfficiencyNotIncludingVBlank > 0) {
		LastZ8StutterPeriod = VActiveTimeCriticalPlane - (*NumberOfStutterBurstsPerFrame - 1) * *StutterPeriod;
		if ((SynchronizedVBlank || TotalNumberOfActiveOTG == 1) && LastZ8StutterPeriod + MinTTUVBlankCriticalPlane > Z8StutterEnterPlusExitWatermark) {
			*Z8StutterEfficiency = (1 - (*NumberOfStutterBurstsPerFrame * SRExitZ8Time + StutterBurstTime * VActiveTimeCriticalPlane
					/ *StutterPeriod) / FrameTimeCriticalPlane) * 100;
		} else {
			*Z8StutterEfficiency = *Z8StutterEfficiencyNotIncludingVBlank;
		}
	} else {
		*Z8StutterEfficiency = 0.;
	}

	dml_print("DML::%s: LastZ8StutterPeriod = %f\n", __func__, LastZ8StutterPeriod);
	dml_print("DML::%s: Z8StutterEnterPlusExitWatermark = %f\n", __func__, Z8StutterEnterPlusExitWatermark);
	dml_print("DML::%s: StutterBurstTime = %f\n", __func__, StutterBurstTime);
	dml_print("DML::%s: StutterPeriod = %f\n", __func__, *StutterPeriod);
	dml_print("DML::%s: StutterEfficiency = %f\n", __func__, *StutterEfficiency);
	dml_print("DML::%s: Z8StutterEfficiency = %f\n", __func__, *Z8StutterEfficiency);
	dml_print("DML::%s: StutterEfficiencyNotIncludingVBlank = %f\n", __func__, *StutterEfficiencyNotIncludingVBlank);
	dml_print("DML::%s: Z8NumberOfStutterBurstsPerFrame = %d\n", __func__, *Z8NumberOfStutterBurstsPerFrame);
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
	int MaximumSwathHeightY[DC__NUM_DPP__MAX];
	int MaximumSwathHeightC[DC__NUM_DPP__MAX];
	int MinimumSwathHeightY;
	int MinimumSwathHeightC;
	int RoundedUpMaxSwathSizeBytesY;
	int RoundedUpMaxSwathSizeBytesC;
	int RoundedUpMinSwathSizeBytesY;
	int RoundedUpMinSwathSizeBytesC;
	int RoundedUpSwathSizeBytesY;
	int RoundedUpSwathSizeBytesC;
	double SwathWidthSingleDPP[DC__NUM_DPP__MAX];
	double SwathWidthSingleDPPChroma[DC__NUM_DPP__MAX];
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
		if ((SourcePixelFormat[k] == dm_444_64 || SourcePixelFormat[k] == dm_444_32 || SourcePixelFormat[k] == dm_444_16 || SourcePixelFormat[k] == dm_mono_16
				|| SourcePixelFormat[k] == dm_mono_8 || SourcePixelFormat[k] == dm_rgbe)) {
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
			} else if (SourcePixelFormat[k] == dm_rgbe_alpha && SourceScan[k] == dm_vert) {
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

		RoundedUpMaxSwathSizeBytesY = swath_width_luma_ub[k] * BytePerPixDETY[k] * MaximumSwathHeightY[k];
		RoundedUpMinSwathSizeBytesY = swath_width_luma_ub[k] * BytePerPixDETY[k] * MinimumSwathHeightY;
		if (SourcePixelFormat[k] == dm_420_10) {
			RoundedUpMaxSwathSizeBytesY = dml_ceil((double) RoundedUpMaxSwathSizeBytesY, 256);
			RoundedUpMinSwathSizeBytesY = dml_ceil((double) RoundedUpMinSwathSizeBytesY, 256);
		}
		RoundedUpMaxSwathSizeBytesC = swath_width_chroma_ub[k] * BytePerPixDETC[k] * MaximumSwathHeightC[k];
		RoundedUpMinSwathSizeBytesC = swath_width_chroma_ub[k] * BytePerPixDETC[k] * MinimumSwathHeightC;
		if (SourcePixelFormat[k] == dm_420_10) {
			RoundedUpMaxSwathSizeBytesC = dml_ceil(RoundedUpMaxSwathSizeBytesC, 256);
			RoundedUpMinSwathSizeBytesC = dml_ceil(RoundedUpMinSwathSizeBytesC, 256);
		}

		if (RoundedUpMaxSwathSizeBytesY + RoundedUpMaxSwathSizeBytesC <= DETBufferSizeInKByte * 1024 / 2) {
			SwathHeightY[k] = MaximumSwathHeightY[k];
			SwathHeightC[k] = MaximumSwathHeightC[k];
			RoundedUpSwathSizeBytesY = RoundedUpMaxSwathSizeBytesY;
			RoundedUpSwathSizeBytesC = RoundedUpMaxSwathSizeBytesC;
		} else if (RoundedUpMaxSwathSizeBytesY >= 1.5 * RoundedUpMaxSwathSizeBytesC
				&& RoundedUpMinSwathSizeBytesY + RoundedUpMaxSwathSizeBytesC <= DETBufferSizeInKByte * 1024 / 2) {
			SwathHeightY[k] = MinimumSwathHeightY;
			SwathHeightC[k] = MaximumSwathHeightC[k];
			RoundedUpSwathSizeBytesY = RoundedUpMinSwathSizeBytesY;
			RoundedUpSwathSizeBytesC = RoundedUpMaxSwathSizeBytesC;
		} else if (RoundedUpMaxSwathSizeBytesY < 1.5 * RoundedUpMaxSwathSizeBytesC
				&& RoundedUpMaxSwathSizeBytesY + RoundedUpMinSwathSizeBytesC <= DETBufferSizeInKByte * 1024 / 2) {
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
		{
		double actDETBufferSizeInKByte = dml_ceil(DETBufferSizeInKByte, 64);

		if (SwathHeightC[k] == 0) {
			DETBufferSizeY[k] = actDETBufferSizeInKByte * 1024;
			DETBufferSizeC[k] = 0;
		} else if (RoundedUpSwathSizeBytesY <= 1.5 * RoundedUpSwathSizeBytesC) {
			DETBufferSizeY[k] = actDETBufferSizeInKByte * 1024 / 2;
			DETBufferSizeC[k] = actDETBufferSizeInKByte * 1024 / 2;
		} else {
			DETBufferSizeY[k] = dml_floor(actDETBufferSizeInKByte * 1024 * 2 / 3, 1024);
			DETBufferSizeC[k] = actDETBufferSizeInKByte * 1024 / 3;
		}

		if (RoundedUpMinSwathSizeBytesY + RoundedUpMinSwathSizeBytesC > actDETBufferSizeInKByte * 1024 / 2 || SwathWidth[k] > MaximumSwathWidthLuma[k]
				|| (SwathHeightC[k] > 0 && SwathWidthChroma[k] > MaximumSwathWidthChroma[k])) {
			*ViewportSizeSupport = false;
			ViewportSizeSupportPerPlane[k] = false;
		} else {
			ViewportSizeSupportPerPlane[k] = true;
		}
		}
	}
}

static void CalculateSwathWidth(
		bool ForceSingleDPP,
		int NumberOfActivePlanes,
		enum source_format_class SourcePixelFormat[],
		enum scan_direction_class SourceScan[],
		int ViewportWidth[],
		int ViewportHeight[],
		int SurfaceWidthY[],
		int SurfaceWidthC[],
		int SurfaceHeightY[],
		int SurfaceHeightC[],
		enum odm_combine_mode ODMCombineEnabled[],
		int BytePerPixY[],
		int BytePerPixC[],
		int Read256BytesBlockHeightY[],
		int Read256BytesBlockHeightC[],
		int Read256BytesBlockWidthY[],
		int Read256BytesBlockWidthC[],
		int BlendingAndTiming[],
		int HActive[],
		double HRatio[],
		int DPPPerPlane[],
		double SwathWidthSingleDPPY[],
		double SwathWidthSingleDPPC[],
		double SwathWidthY[],
		double SwathWidthC[],
		int MaximumSwathHeightY[],
		int MaximumSwathHeightC[],
		int swath_width_luma_ub[],
		int swath_width_chroma_ub[])
{
	enum odm_combine_mode MainPlaneODMCombine;
	int j, k;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: NumberOfActivePlanes = %d\n", __func__, NumberOfActivePlanes);
#endif

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (SourceScan[k] != dm_vert) {
			SwathWidthSingleDPPY[k] = ViewportWidth[k];
		} else {
			SwathWidthSingleDPPY[k] = ViewportHeight[k];
		}

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d ViewportWidth=%d\n", __func__, k, ViewportWidth[k]);
		dml_print("DML::%s: k=%d ViewportHeight=%d\n", __func__, k, ViewportHeight[k]);
#endif

		MainPlaneODMCombine = ODMCombineEnabled[k];
		for (j = 0; j < NumberOfActivePlanes; ++j) {
			if (BlendingAndTiming[k] == j) {
				MainPlaneODMCombine = ODMCombineEnabled[j];
			}
		}

		if (MainPlaneODMCombine == dm_odm_combine_mode_4to1)
			SwathWidthY[k] = dml_min(SwathWidthSingleDPPY[k], dml_round(HActive[k] / 4.0 * HRatio[k]));
		else if (MainPlaneODMCombine == dm_odm_combine_mode_2to1)
			SwathWidthY[k] = dml_min(SwathWidthSingleDPPY[k], dml_round(HActive[k] / 2.0 * HRatio[k]));
		else if (DPPPerPlane[k] == 2)
			SwathWidthY[k] = SwathWidthSingleDPPY[k] / 2;
		else
			SwathWidthY[k] = SwathWidthSingleDPPY[k];

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d SwathWidthSingleDPPY=%f\n", __func__, k, SwathWidthSingleDPPY[k]);
		dml_print("DML::%s: k=%d SwathWidthY=%f\n", __func__, k, SwathWidthY[k]);
#endif

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
		{
		int surface_width_ub_l = dml_ceil(SurfaceWidthY[k], Read256BytesBlockWidthY[k]);
		int surface_height_ub_l = dml_ceil(SurfaceHeightY[k], Read256BytesBlockHeightY[k]);

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%d surface_width_ub_l=%0d\n", __func__, k, surface_width_ub_l);
#endif

		if (SourceScan[k] != dm_vert) {
			MaximumSwathHeightY[k] = Read256BytesBlockHeightY[k];
			MaximumSwathHeightC[k] = Read256BytesBlockHeightC[k];
			swath_width_luma_ub[k] = dml_min(surface_width_ub_l, (int) dml_ceil(SwathWidthY[k] - 1, Read256BytesBlockWidthY[k]) + Read256BytesBlockWidthY[k]);
			if (BytePerPixC[k] > 0) {
				int surface_width_ub_c = dml_ceil(SurfaceWidthC[k], Read256BytesBlockWidthC[k]);

				swath_width_chroma_ub[k] = dml_min(
						surface_width_ub_c,
						(int) dml_ceil(SwathWidthC[k] - 1, Read256BytesBlockWidthC[k]) + Read256BytesBlockWidthC[k]);
			} else {
				swath_width_chroma_ub[k] = 0;
			}
		} else {
			MaximumSwathHeightY[k] = Read256BytesBlockWidthY[k];
			MaximumSwathHeightC[k] = Read256BytesBlockWidthC[k];
			swath_width_luma_ub[k] = dml_min(surface_height_ub_l, (int) dml_ceil(SwathWidthY[k] - 1, Read256BytesBlockHeightY[k]) + Read256BytesBlockHeightY[k]);
			if (BytePerPixC[k] > 0) {
				int surface_height_ub_c = dml_ceil(SurfaceHeightC[k], Read256BytesBlockHeightC[k]);

				swath_width_chroma_ub[k] = dml_min(
						surface_height_ub_c,
						(int) dml_ceil(SwathWidthC[k] - 1, Read256BytesBlockHeightC[k]) + Read256BytesBlockHeightC[k]);
			} else {
				swath_width_chroma_ub[k] = 0;
			}
		}
		}
	}
}

static double CalculateExtraLatency(
		int RoundTripPingLatencyCycles,
		int ReorderingBytes,
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
		double HostVMInefficiencyFactor,
		double HostVMMinPageSize,
		int HostVMMaxNonCachedPageTableLevels)
{
	double ExtraLatencyBytes;
	double ExtraLatency;

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
			HostVMInefficiencyFactor,
			HostVMMinPageSize,
			HostVMMaxNonCachedPageTableLevels);

	ExtraLatency = (RoundTripPingLatencyCycles + __DML_ARB_TO_RET_DELAY__) / DCFCLK + ExtraLatencyBytes / ReturnBW;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: RoundTripPingLatencyCycles=%d\n", __func__, RoundTripPingLatencyCycles);
	dml_print("DML::%s: DCFCLK=%f\n", __func__, DCFCLK);
	dml_print("DML::%s: ExtraLatencyBytes=%f\n", __func__, ExtraLatencyBytes);
	dml_print("DML::%s: ReturnBW=%f\n", __func__, ReturnBW);
	dml_print("DML::%s: ExtraLatency=%f\n", __func__, ExtraLatency);
#endif

	return ExtraLatency;
}

static double CalculateExtraLatencyBytes(
		int ReorderingBytes,
		int TotalNumberOfActiveDPP,
		int PixelChunkSizeInKByte,
		int TotalNumberOfDCCActiveDPP,
		int MetaChunkSize,
		bool GPUVMEnable,
		bool HostVMEnable,
		int NumberOfActivePlanes,
		int NumberOfDPP[],
		int dpte_group_bytes[],
		double HostVMInefficiencyFactor,
		double HostVMMinPageSize,
		int HostVMMaxNonCachedPageTableLevels)
{
	double ret;
	int HostVMDynamicLevels = 0, k;

	if (GPUVMEnable == true && HostVMEnable == true) {
		if (HostVMMinPageSize < 2048)
			HostVMDynamicLevels = HostVMMaxNonCachedPageTableLevels;
		else if (HostVMMinPageSize >= 2048 && HostVMMinPageSize < 1048576)
			HostVMDynamicLevels = dml_max(0, (int) HostVMMaxNonCachedPageTableLevels - 1);
		else
			HostVMDynamicLevels = dml_max(0, (int) HostVMMaxNonCachedPageTableLevels - 2);
	} else {
		HostVMDynamicLevels = 0;
	}

	ret = ReorderingBytes + (TotalNumberOfActiveDPP * PixelChunkSizeInKByte + TotalNumberOfDCCActiveDPP * MetaChunkSize) * 1024.0;

	if (GPUVMEnable == true) {
		for (k = 0; k < NumberOfActivePlanes; ++k)
			ret = ret + NumberOfDPP[k] * dpte_group_bytes[k] * (1 + 8 * HostVMDynamicLevels) * HostVMInefficiencyFactor;
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
	if (DoUrgentLatencyAdjustment == true)
		ret = ret + UrgentLatencyAdjustmentFabricClockComponent * (UrgentLatencyAdjustmentFabricClockReference / FabricClock - 1);
	return ret;
}

static void UseMinimumDCFCLK(
		struct display_mode_lib *mode_lib,
		int MaxPrefetchMode,
		int ReorderingBytes)
{
	struct vba_vars_st *v = &mode_lib->vba;
	int dummy1, i, j, k;
	double NormalEfficiency,  dummy2, dummy3;
	double TotalMaxPrefetchFlipDPTERowBandwidth[DC__VOLTAGE_STATES][2];

	NormalEfficiency = v->PercentOfIdealFabricAndSDPPortBWReceivedAfterUrgLatency / 100.0;
	for (i = 0; i < v->soc.num_states; ++i) {
		for (j = 0; j <= 1; ++j) {
			double PixelDCFCLKCyclesRequiredInPrefetch[DC__NUM_DPP__MAX];
			double PrefetchPixelLinesTime[DC__NUM_DPP__MAX];
			double DCFCLKRequiredForPeakBandwidthPerPlane[DC__NUM_DPP__MAX];
			double DynamicMetadataVMExtraLatency[DC__NUM_DPP__MAX];
			double MinimumTWait;
			double NonDPTEBandwidth;
			double DPTEBandwidth;
			double DCFCLKRequiredForAverageBandwidth;
			double ExtraLatencyBytes;
			double ExtraLatencyCycles;
			double DCFCLKRequiredForPeakBandwidth;
			int NoOfDPPState[DC__NUM_DPP__MAX];
			double MinimumTvmPlus2Tr0;

			TotalMaxPrefetchFlipDPTERowBandwidth[i][j] = 0;
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				TotalMaxPrefetchFlipDPTERowBandwidth[i][j] = TotalMaxPrefetchFlipDPTERowBandwidth[i][j]
						+ v->NoOfDPP[i][j][k] * v->DPTEBytesPerRow[i][j][k] / (15.75 * v->HTotal[k] / v->PixelClock[k]);
			}

			for (k = 0; k <= v->NumberOfActivePlanes - 1; ++k)
				NoOfDPPState[k] = v->NoOfDPP[i][j][k];

			MinimumTWait = CalculateTWait(MaxPrefetchMode, v->FinalDRAMClockChangeLatency, v->UrgLatency[i], v->SREnterPlusExitTime);
			NonDPTEBandwidth = v->TotalVActivePixelBandwidth[i][j] + v->TotalVActiveCursorBandwidth[i][j] + v->TotalMetaRowBandwidth[i][j];
			DPTEBandwidth = (v->HostVMEnable == true || v->ImmediateFlipRequirement[0] == dm_immediate_flip_required) ?
					TotalMaxPrefetchFlipDPTERowBandwidth[i][j] : v->TotalDPTERowBandwidth[i][j];
			DCFCLKRequiredForAverageBandwidth = dml_max3(
					v->ProjectedDCFCLKDeepSleep[i][j],
					(NonDPTEBandwidth + v->TotalDPTERowBandwidth[i][j]) / v->ReturnBusWidth
							/ (v->MaxAveragePercentOfIdealFabricAndSDPPortBWDisplayCanUseInNormalSystemOperation / 100),
					(NonDPTEBandwidth + DPTEBandwidth / NormalEfficiency) / NormalEfficiency / v->ReturnBusWidth);

			ExtraLatencyBytes = CalculateExtraLatencyBytes(
					ReorderingBytes,
					v->TotalNumberOfActiveDPP[i][j],
					v->PixelChunkSizeInKByte,
					v->TotalNumberOfDCCActiveDPP[i][j],
					v->MetaChunkSize,
					v->GPUVMEnable,
					v->HostVMEnable,
					v->NumberOfActivePlanes,
					NoOfDPPState,
					v->dpte_group_bytes,
					1,
					v->HostVMMinPageSize,
					v->HostVMMaxNonCachedPageTableLevels);
			ExtraLatencyCycles = v->RoundTripPingLatencyCycles + __DML_ARB_TO_RET_DELAY__ + ExtraLatencyBytes / NormalEfficiency / v->ReturnBusWidth;
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				double DCFCLKCyclesRequiredInPrefetch;
				double ExpectedPrefetchBWAcceleration;
				double PrefetchTime;

				PixelDCFCLKCyclesRequiredInPrefetch[k] = (v->PrefetchLinesY[i][j][k] * v->swath_width_luma_ub_all_states[i][j][k] * v->BytePerPixelY[k]
						+ v->PrefetchLinesC[i][j][k] * v->swath_width_chroma_ub_all_states[i][j][k] * v->BytePerPixelC[k]) / NormalEfficiency / v->ReturnBusWidth;
				DCFCLKCyclesRequiredInPrefetch = 2 * ExtraLatencyCycles / NoOfDPPState[k]
						+ v->PDEAndMetaPTEBytesPerFrame[i][j][k] / NormalEfficiency / NormalEfficiency / v->ReturnBusWidth * (v->GPUVMMaxPageTableLevels > 2 ? 1 : 0)
						+ 2 * v->DPTEBytesPerRow[i][j][k] / NormalEfficiency / NormalEfficiency / v->ReturnBusWidth
						+ 2 * v->MetaRowBytes[i][j][k] / NormalEfficiency / v->ReturnBusWidth + PixelDCFCLKCyclesRequiredInPrefetch[k];
				PrefetchPixelLinesTime[k] = dml_max(v->PrefetchLinesY[i][j][k], v->PrefetchLinesC[i][j][k]) * v->HTotal[k] / v->PixelClock[k];
				ExpectedPrefetchBWAcceleration = (v->VActivePixelBandwidth[i][j][k] + v->VActiveCursorBandwidth[i][j][k])
						/ (v->ReadBandwidthLuma[k] + v->ReadBandwidthChroma[k]);
				DynamicMetadataVMExtraLatency[k] =
						(v->GPUVMEnable == true && v->DynamicMetadataEnable[k] == true && v->DynamicMetadataVMEnabled == true) ?
								v->UrgLatency[i] * v->GPUVMMaxPageTableLevels * (v->HostVMEnable == true ? v->HostVMMaxNonCachedPageTableLevels + 1 : 1) : 0;
				PrefetchTime = (v->MaximumVStartup[i][j][k] - 1) * v->HTotal[k] / v->PixelClock[k] - MinimumTWait
						- v->UrgLatency[i]
								* ((v->GPUVMMaxPageTableLevels <= 2 ? v->GPUVMMaxPageTableLevels : v->GPUVMMaxPageTableLevels - 2)
										* (v->HostVMEnable == true ? v->HostVMMaxNonCachedPageTableLevels + 1 : 1) - 1)
						- DynamicMetadataVMExtraLatency[k];

				if (PrefetchTime > 0) {
					double ExpectedVRatioPrefetch;

					ExpectedVRatioPrefetch = PrefetchPixelLinesTime[k]
							/ (PrefetchTime * PixelDCFCLKCyclesRequiredInPrefetch[k] / DCFCLKCyclesRequiredInPrefetch);
					DCFCLKRequiredForPeakBandwidthPerPlane[k] = NoOfDPPState[k] * PixelDCFCLKCyclesRequiredInPrefetch[k] / PrefetchPixelLinesTime[k]
							* dml_max(1.0, ExpectedVRatioPrefetch) * dml_max(1.0, ExpectedVRatioPrefetch / 4) * ExpectedPrefetchBWAcceleration;
					if (v->HostVMEnable == true || v->ImmediateFlipRequirement[0] == dm_immediate_flip_required) {
						DCFCLKRequiredForPeakBandwidthPerPlane[k] = DCFCLKRequiredForPeakBandwidthPerPlane[k]
								+ NoOfDPPState[k] * DPTEBandwidth / NormalEfficiency / NormalEfficiency / v->ReturnBusWidth;
					}
				} else {
					DCFCLKRequiredForPeakBandwidthPerPlane[k] = v->DCFCLKPerState[i];
				}
				if (v->DynamicMetadataEnable[k] == true) {
					double TSetupPipe;
					double TdmbfPipe;
					double TdmsksPipe;
					double TdmecPipe;
					double AllowedTimeForUrgentExtraLatency;

					CalculateVupdateAndDynamicMetadataParameters(
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
							&TSetupPipe,
							&TdmbfPipe,
							&TdmecPipe,
							&TdmsksPipe,
							&dummy1,
							&dummy2,
							&dummy3);
					AllowedTimeForUrgentExtraLatency = v->MaximumVStartup[i][j][k] * v->HTotal[k] / v->PixelClock[k] - MinimumTWait - TSetupPipe - TdmbfPipe - TdmecPipe
							- TdmsksPipe - DynamicMetadataVMExtraLatency[k];
					if (AllowedTimeForUrgentExtraLatency > 0) {
						DCFCLKRequiredForPeakBandwidthPerPlane[k] = dml_max(
								DCFCLKRequiredForPeakBandwidthPerPlane[k],
								ExtraLatencyCycles / AllowedTimeForUrgentExtraLatency);
					} else {
						DCFCLKRequiredForPeakBandwidthPerPlane[k] = v->DCFCLKPerState[i];
					}
				}
			}
			DCFCLKRequiredForPeakBandwidth = 0;
			for (k = 0; k <= v->NumberOfActivePlanes - 1; ++k)
				DCFCLKRequiredForPeakBandwidth = DCFCLKRequiredForPeakBandwidth + DCFCLKRequiredForPeakBandwidthPerPlane[k];

			MinimumTvmPlus2Tr0 = v->UrgLatency[i]
					* (v->GPUVMEnable == true ?
							(v->HostVMEnable == true ?
									(v->GPUVMMaxPageTableLevels + 2) * (v->HostVMMaxNonCachedPageTableLevels + 1) - 1 : v->GPUVMMaxPageTableLevels + 1) :
							0);
			for (k = 0; k < v->NumberOfActivePlanes; ++k) {
				double MaximumTvmPlus2Tr0PlusTsw;

				MaximumTvmPlus2Tr0PlusTsw = (v->MaximumVStartup[i][j][k] - 2) * v->HTotal[k] / v->PixelClock[k] - MinimumTWait - DynamicMetadataVMExtraLatency[k];
				if (MaximumTvmPlus2Tr0PlusTsw <= MinimumTvmPlus2Tr0 + PrefetchPixelLinesTime[k] / 4) {
					DCFCLKRequiredForPeakBandwidth = v->DCFCLKPerState[i];
				} else {
					DCFCLKRequiredForPeakBandwidth = dml_max3(
							DCFCLKRequiredForPeakBandwidth,
							2 * ExtraLatencyCycles / (MaximumTvmPlus2Tr0PlusTsw - MinimumTvmPlus2Tr0 - PrefetchPixelLinesTime[k] / 4),
							(2 * ExtraLatencyCycles + PixelDCFCLKCyclesRequiredInPrefetch[k]) / (MaximumTvmPlus2Tr0PlusTsw - MinimumTvmPlus2Tr0));
				}
			}
			v->DCFCLKState[i][j] = dml_min(v->DCFCLKPerState[i], 1.05 * dml_max(DCFCLKRequiredForAverageBandwidth, DCFCLKRequiredForPeakBandwidth));
		}
	}
}

static void CalculateUnboundedRequestAndCompressedBufferSize(
		unsigned int DETBufferSizeInKByte,
		int ConfigReturnBufferSizeInKByte,
		enum unbounded_requesting_policy UseUnboundedRequestingFinal,
		int TotalActiveDPP,
		bool NoChromaPlanes,
		int MaxNumDPP,
		int CompressedBufferSegmentSizeInkByteFinal,
		enum output_encoder_class *Output,
		bool *UnboundedRequestEnabled,
		int *CompressedBufferSizeInkByte)
{
	double actDETBufferSizeInKByte = dml_ceil(DETBufferSizeInKByte, 64);

	*UnboundedRequestEnabled = UnboundedRequest(UseUnboundedRequestingFinal, TotalActiveDPP, NoChromaPlanes, Output[0]);
	*CompressedBufferSizeInkByte = (
			*UnboundedRequestEnabled == true ?
					ConfigReturnBufferSizeInKByte - TotalActiveDPP * actDETBufferSizeInKByte :
					ConfigReturnBufferSizeInKByte - MaxNumDPP * actDETBufferSizeInKByte);
	*CompressedBufferSizeInkByte = *CompressedBufferSizeInkByte * CompressedBufferSegmentSizeInkByteFinal / 64;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: TotalActiveDPP = %d\n", __func__, TotalActiveDPP);
	dml_print("DML::%s: DETBufferSizeInKByte = %d\n", __func__, DETBufferSizeInKByte);
	dml_print("DML::%s: ConfigReturnBufferSizeInKByte = %d\n", __func__, ConfigReturnBufferSizeInKByte);
	dml_print("DML::%s: UseUnboundedRequestingFinal = %d\n", __func__, UseUnboundedRequestingFinal);
	dml_print("DML::%s: actDETBufferSizeInKByte = %f\n", __func__, actDETBufferSizeInKByte);
	dml_print("DML::%s: UnboundedRequestEnabled = %d\n", __func__, *UnboundedRequestEnabled);
	dml_print("DML::%s: CompressedBufferSizeInkByte = %d\n", __func__, *CompressedBufferSizeInkByte);
#endif
}

static bool UnboundedRequest(enum unbounded_requesting_policy UseUnboundedRequestingFinal, int TotalNumberOfActiveDPP, bool NoChroma, enum output_encoder_class Output)
{
	bool ret_val = false;

	ret_val = (UseUnboundedRequestingFinal != dm_unbounded_requesting_disable && TotalNumberOfActiveDPP == 1 && NoChroma);
	if (UseUnboundedRequestingFinal == dm_unbounded_requesting_edp_only && Output != dm_edp)
		ret_val = false;
	return ret_val;
}

static unsigned int CalculateMaxVStartup(
		unsigned int VTotal,
		unsigned int VActive,
		unsigned int VBlankNom,
		unsigned int HTotal,
		double PixelClock,
		bool ProgressiveTointerlaceUnitinOPP,
		bool Interlace,
		unsigned int VBlankNomDefaultUS,
		double WritebackDelayTime)
{
	unsigned int MaxVStartup = 0;
	unsigned int vblank_size = 0;
	double line_time_us = HTotal / PixelClock;
	unsigned int vblank_actual = VTotal - VActive;
	unsigned int vblank_nom_default_in_line = dml_floor(VBlankNomDefaultUS / line_time_us, 1.0);
	unsigned int vblank_nom_input = VBlankNom; //dml_min(VBlankNom, vblank_nom_default_in_line);
	unsigned int vblank_avail = vblank_nom_input == 0 ? vblank_nom_default_in_line : vblank_nom_input;

	vblank_size = (unsigned int) dml_min(vblank_actual, vblank_avail);
	if (Interlace && !ProgressiveTointerlaceUnitinOPP)
		MaxVStartup = dml_floor(vblank_size / 2.0, 1.0);
	else
		MaxVStartup = vblank_size - dml_max(1.0, dml_ceil(WritebackDelayTime / line_time_us, 1.0));
	if (MaxVStartup > 1023)
		MaxVStartup = 1023;
	return MaxVStartup;
}
