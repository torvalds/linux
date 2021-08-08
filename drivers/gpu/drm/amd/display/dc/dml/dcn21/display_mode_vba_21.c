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


#include "../display_mode_lib.h"
#include "../dml_inline_defs.h"
#include "../display_mode_vba.h"
#include "display_mode_vba_21.h"


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
} Pipe;

typedef struct {
	bool Enable;
	unsigned int MaxPageTableLevels;
	unsigned int CachedPageTableLevels;
} HostVM;

#define BPP_INVALID 0
#define BPP_BLENDED_PIPE 0xffffffff
#define DCN21_MAX_DSC_IMAGE_WIDTH 5184
#define DCN21_MAX_420_IMAGE_WIDTH 4096

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
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
		Pipe *myPipe,
		unsigned int DSCDelay,
		double DPPCLKDelaySubtotal,
		double DPPCLKDelaySCL,
		double DPPCLKDelaySCLLBOnly,
		double DPPCLKDelayCNVCFormater,
		double DPPCLKDelayCNVCCursor,
		double DISPCLKDelaySubtotal,
		unsigned int ScalerRecoutWidth,
		enum output_format_class OutputFormat,
		unsigned int MaxInterDCNTileRepeaters,
		unsigned int VStartup,
		unsigned int MaxVStartup,
		unsigned int GPUVMPageTableLevels,
		bool GPUVMEnable,
		HostVM *myHostVM,
		bool DynamicMetadataEnable,
		int DynamicMetadataLinesBeforeActiveRequired,
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
		unsigned int *VStartupRequiredWhenNotEnoughTimeForDynamicMetadata,
		double *Tno_bw,
		double *prefetch_vmrow_bw,
		unsigned int *swath_width_luma_ub,
		unsigned int *swath_width_chroma_ub,
		unsigned int *VUpdateOffsetPix,
		double *VUpdateWidthPix,
		double *VReadyOffsetPix);
static double RoundToDFSGranularityUp(double Clock, double VCOSpeed);
static double RoundToDFSGranularityDown(double Clock, double VCOSpeed);
static double CalculateDCCConfiguration(
		bool                 DCCEnabled,
		bool                 DCCProgrammingAssumesScanDirectionUnknown,
		unsigned int         ViewportWidth,
		unsigned int         ViewportHeight,
		double               DETBufferSize,
		unsigned int         RequestHeight256Byte,
		unsigned int         SwathHeight,
		enum dm_swizzle_mode TilingFormat,
		unsigned int         BytePerPixel,
		enum scan_direction_class ScanOrientation,
		unsigned int        *MaxUncompressedBlock,
		unsigned int        *MaxCompressedBlock,
		unsigned int        *Independent64ByteBlock);
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
		bool GPUVMEnable,
		bool HostVMEnable,
		unsigned int HostVMMaxPageTableLevels,
		unsigned int HostVMCachedPageTableLevels,
		unsigned int VMMPageSize,
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
static void CalculateActiveRowBandwidth(
		bool GPUVMEnable,
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
		double *dpte_row_bw);
static void CalculateFlipSchedule(
		struct display_mode_lib *mode_lib,
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
		double UrgentExtraLatency,
		double UrgentLatency,
		unsigned int GPUVMMaxPageTableLevels,
		bool HostVMEnable,
		unsigned int HostVMMaxPageTableLevels,
		unsigned int HostVMCachedPageTableLevels,
		bool GPUVMEnable,
		double PDEAndMetaPTEBytesPerFrame,
		double MetaRowBytes,
		double DPTEBytesPerRow,
		double BandwidthAvailableForImmediateFlip,
		unsigned int TotImmediateFlipBytes,
		enum source_format_class SourcePixelFormat,
		double LineTime,
		double VRatio,
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
		unsigned int WritebackLumaHTaps,
		unsigned int WritebackLumaVTaps,
		unsigned int WritebackChromaHTaps,
		unsigned int WritebackChromaVTaps,
		unsigned int WritebackDestinationWidth);
static void CalculateWatermarksAndDRAMSpeedChangeSupport(
		struct display_mode_lib *mode_lib,
		unsigned int PrefetchMode,
		unsigned int NumberOfActivePlanes,
		unsigned int MaxLineBufferLines,
		unsigned int LineBufferSize,
		unsigned int DPPOutputBufferPixels,
		double DETBufferSizeInKByte,
		unsigned int WritebackInterfaceLumaBufferSize,
		unsigned int WritebackInterfaceChromaBufferSize,
		double DCFCLK,
		double UrgentOutOfOrderReturn,
		double ReturnBW,
		bool GPUVMEnable,
		int dpte_group_bytes[],
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
		int DPPPerPlane[],
		bool DCCEnable[],
		double DPPCLK[],
		double SwathWidthSingleDPPY[],
		unsigned int SwathHeightY[],
		double ReadBandwidthPlaneLuma[],
		unsigned int SwathHeightC[],
		double ReadBandwidthPlaneChroma[],
		unsigned int LBBitPerPixel[],
		double SwathWidthY[],
		double HRatio[],
		unsigned int vtaps[],
		unsigned int VTAPsChroma[],
		double VRatio[],
		unsigned int HTotal[],
		double PixelClock[],
		unsigned int BlendingAndTiming[],
		double BytePerPixelDETY[],
		double BytePerPixelDETC[],
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
		double BytePerPixelDETY[],
		double BytePerPixelDETC[],
		double VRatio[],
		double SwathWidthY[],
		int DPPPerPlane[],
		double HRatio[],
		double PixelClock[],
		double PSCL_THROUGHPUT[],
		double PSCL_THROUGHPUT_CHROMA[],
		double DPPCLK[],
		double *DCFCLKDeepSleep);
static void CalculateDETBufferSize(
		double DETBufferSizeInKByte,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		double *DETBufferSizeY,
		double *DETBufferSizeC);
static void CalculateUrgentBurstFactor(
		unsigned int DETBufferSizeInKByte,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		unsigned int SwathWidthY,
		double LineTime,
		double UrgentLatency,
		double CursorBufferSize,
		unsigned int CursorWidth,
		unsigned int CursorBPP,
		double VRatio,
		double VRatioPreY,
		double VRatioPreC,
		double BytePerPixelInDETY,
		double BytePerPixelInDETC,
		double *UrgentBurstFactorCursor,
		double *UrgentBurstFactorCursorPre,
		double *UrgentBurstFactorLuma,
		double *UrgentBurstFactorLumaPre,
		double *UrgentBurstFactorChroma,
		double *UrgentBurstFactorChromaPre,
		unsigned int *NotEnoughUrgentLatencyHiding,
		unsigned int *NotEnoughUrgentLatencyHidingPre);

static void CalculatePixelDeliveryTimes(
		unsigned int           NumberOfActivePlanes,
		double                 VRatio[],
		double                 VRatioPrefetchY[],
		double                 VRatioPrefetchC[],
		unsigned int           swath_width_luma_ub[],
		unsigned int           swath_width_chroma_ub[],
		int                    DPPPerPlane[],
		double                 HRatio[],
		double                 PixelClock[],
		double                 PSCL_THROUGHPUT[],
		double                 PSCL_THROUGHPUT_CHROMA[],
		double                 DPPCLK[],
		double                 BytePerPixelDETC[],
		enum scan_direction_class SourceScan[],
		unsigned int           BlockWidth256BytesY[],
		unsigned int           BlockHeight256BytesY[],
		unsigned int           BlockWidth256BytesC[],
		unsigned int           BlockHeight256BytesC[],
		double                 DisplayPipeLineDeliveryTimeLuma[],
		double                 DisplayPipeLineDeliveryTimeChroma[],
		double                 DisplayPipeLineDeliveryTimeLumaPrefetch[],
		double                 DisplayPipeLineDeliveryTimeChromaPrefetch[],
		double                 DisplayPipeRequestDeliveryTimeLuma[],
		double                 DisplayPipeRequestDeliveryTimeChroma[],
		double                 DisplayPipeRequestDeliveryTimeLumaPrefetch[],
		double                 DisplayPipeRequestDeliveryTimeChromaPrefetch[]);

static void CalculateMetaAndPTETimes(
		unsigned int           NumberOfActivePlanes,
		bool                   GPUVMEnable,
		unsigned int           MetaChunkSize,
		unsigned int           MinMetaChunkSizeBytes,
		unsigned int           GPUVMMaxPageTableLevels,
		unsigned int           HTotal[],
		double                 VRatio[],
		double                 VRatioPrefetchY[],
		double                 VRatioPrefetchC[],
		double                 DestinationLinesToRequestRowInVBlank[],
		double                 DestinationLinesToRequestRowInImmediateFlip[],
		double                 DestinationLinesToRequestVMInVBlank[],
		double                 DestinationLinesToRequestVMInImmediateFlip[],
		bool                   DCCEnable[],
		double                 PixelClock[],
		double                 BytePerPixelDETY[],
		double                 BytePerPixelDETC[],
		enum scan_direction_class SourceScan[],
		unsigned int           dpte_row_height[],
		unsigned int           dpte_row_height_chroma[],
		unsigned int           meta_row_width[],
		unsigned int           meta_row_height[],
		unsigned int           meta_req_width[],
		unsigned int           meta_req_height[],
		int                   dpte_group_bytes[],
		unsigned int           PTERequestSizeY[],
		unsigned int           PTERequestSizeC[],
		unsigned int           PixelPTEReqWidthY[],
		unsigned int           PixelPTEReqHeightY[],
		unsigned int           PixelPTEReqWidthC[],
		unsigned int           PixelPTEReqHeightC[],
		unsigned int           dpte_row_width_luma_ub[],
		unsigned int           dpte_row_width_chroma_ub[],
		unsigned int           vm_group_bytes[],
		unsigned int           dpde0_bytes_per_frame_ub_l[],
		unsigned int           dpde0_bytes_per_frame_ub_c[],
		unsigned int           meta_pte_bytes_per_frame_ub_l[],
		unsigned int           meta_pte_bytes_per_frame_ub_c[],
		double                 DST_Y_PER_PTE_ROW_NOM_L[],
		double                 DST_Y_PER_PTE_ROW_NOM_C[],
		double                 DST_Y_PER_META_ROW_NOM_L[],
		double                 TimePerMetaChunkNominal[],
		double                 TimePerMetaChunkVBlank[],
		double                 TimePerMetaChunkFlip[],
		double                 time_per_pte_group_nom_luma[],
		double                 time_per_pte_group_vblank_luma[],
		double                 time_per_pte_group_flip_luma[],
		double                 time_per_pte_group_nom_chroma[],
		double                 time_per_pte_group_vblank_chroma[],
		double                 time_per_pte_group_flip_chroma[],
		double                 TimePerVMGroupVBlank[],
		double                 TimePerVMGroupFlip[],
		double                 TimePerVMRequestVBlank[],
		double                 TimePerVMRequestFlip[]);

static double CalculateExtraLatency(
		double UrgentRoundTripAndOutOfOrderLatency,
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
		int HostVMMaxPageTableLevels,
		int HostVMCachedPageTableLevels);

void dml21_recalculate(struct display_mode_lib *mode_lib)
{
	ModeSupportAndSystemConfiguration(mode_lib);
	PixelClockAdjustmentForProgressiveToInterlaceUnit(mode_lib);
	DisplayPipeConfiguration(mode_lib);
	DISPCLKDPPCLKDCFCLKDeepSleepPrefetchParametersWatermarksAndPerformanceCalculation(mode_lib);
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
	unsigned int pixelsPerClock, lstall, D, initalXmitDelay, w, S, ix, wx, p, l0, a, ax, l,
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
		S = 1;
	else
		S = 0;

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
	Delay = l * wx * (numSlices - 1) + ax + S + lstall + 22;

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
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
		double PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
		Pipe *myPipe,
		unsigned int DSCDelay,
		double DPPCLKDelaySubtotal,
		double DPPCLKDelaySCL,
		double DPPCLKDelaySCLLBOnly,
		double DPPCLKDelayCNVCFormater,
		double DPPCLKDelayCNVCCursor,
		double DISPCLKDelaySubtotal,
		unsigned int ScalerRecoutWidth,
		enum output_format_class OutputFormat,
		unsigned int MaxInterDCNTileRepeaters,
		unsigned int VStartup,
		unsigned int MaxVStartup,
		unsigned int GPUVMPageTableLevels,
		bool GPUVMEnable,
		HostVM *myHostVM,
		bool DynamicMetadataEnable,
		int DynamicMetadataLinesBeforeActiveRequired,
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
		unsigned int *VStartupRequiredWhenNotEnoughTimeForDynamicMetadata,
		double *Tno_bw,
		double *prefetch_vmrow_bw,
		unsigned int *swath_width_luma_ub,
		unsigned int *swath_width_chroma_ub,
		unsigned int *VUpdateOffsetPix,
		double *VUpdateWidthPix,
		double *VReadyOffsetPix)
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
	double Tvm_oto_lines;
	double Tr0_oto_lines;
	double Tsw_oto_lines;
	double dst_y_prefetch_oto;
	double TimeForFetchingMetaPTE = 0;
	double TimeForFetchingRowInVBlank = 0;
	double LinesToRequestPrefetchPixelData = 0;
	double HostVMInefficiencyFactor;
	unsigned int HostVMDynamicLevels;

	if (GPUVMEnable == true && myHostVM->Enable == true) {
		HostVMInefficiencyFactor =
				PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData
						/ PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly;
		HostVMDynamicLevels = myHostVM->MaxPageTableLevels
				- myHostVM->CachedPageTableLevels;
	} else {
		HostVMInefficiencyFactor = 1;
		HostVMDynamicLevels = 0;
	}

	if (myPipe->ScalerEnabled)
		DPPCycles = DPPCLKDelaySubtotal + DPPCLKDelaySCL;
	else
		DPPCycles = DPPCLKDelaySubtotal + DPPCLKDelaySCLLBOnly;

	DPPCycles = DPPCycles + DPPCLKDelayCNVCFormater + myPipe->NumberOfCursors * DPPCLKDelayCNVCCursor;

	DISPCLKCycles = DISPCLKDelaySubtotal;

	if (myPipe->DPPCLK == 0.0 || myPipe->DISPCLK == 0.0)
		return true;

	*DSTXAfterScaler = DPPCycles * myPipe->PixelClock / myPipe->DPPCLK
			+ DISPCLKCycles * myPipe->PixelClock / myPipe->DISPCLK + DSCDelay;

	if (myPipe->DPPPerPlane > 1)
		*DSTXAfterScaler = *DSTXAfterScaler + ScalerRecoutWidth;

	if (OutputFormat == dm_420 || (myPipe->InterlaceEnable && ProgressiveToInterlaceUnitInOPP))
		*DSTYAfterScaler = 1;
	else
		*DSTYAfterScaler = 0;

	DSTTotalPixelsAfterScaler = ((double) (*DSTYAfterScaler * myPipe->HTotal)) + *DSTXAfterScaler;
	*DSTYAfterScaler = dml_floor(DSTTotalPixelsAfterScaler / myPipe->HTotal, 1);
	*DSTXAfterScaler = DSTTotalPixelsAfterScaler - ((double) (*DSTYAfterScaler * myPipe->HTotal));

	*VUpdateOffsetPix = dml_ceil(myPipe->HTotal / 4.0, 1);
	TotalRepeaterDelayTime = MaxInterDCNTileRepeaters * (2.0 / myPipe->DPPCLK + 3.0 / myPipe->DISPCLK);
	*VUpdateWidthPix = (14.0 / myPipe->DCFCLKDeepSleep + 12.0 / myPipe->DPPCLK + TotalRepeaterDelayTime)
			* myPipe->PixelClock;

	*VReadyOffsetPix = dml_max(
			150.0 / myPipe->DPPCLK,
			TotalRepeaterDelayTime + 20.0 / myPipe->DCFCLKDeepSleep + 10.0 / myPipe->DPPCLK)
			* myPipe->PixelClock;

	Tsetup = (double) (*VUpdateOffsetPix + *VUpdateWidthPix + *VReadyOffsetPix) / myPipe->PixelClock;

	LineTime = (double) myPipe->HTotal / myPipe->PixelClock;

	if (DynamicMetadataEnable) {
		double Tdmbf, Tdmec, Tdmsks;

		Tdm = dml_max(0.0, UrgentExtraLatency - TCalc);
		Tdmbf = DynamicMetadataTransmittedBytes / 4.0 / myPipe->DISPCLK;
		Tdmec = LineTime;
		if (DynamicMetadataLinesBeforeActiveRequired == -1)
			Tdmsks = myPipe->VBlank * LineTime / 2.0;
		else
			Tdmsks = DynamicMetadataLinesBeforeActiveRequired * LineTime;
		if (myPipe->InterlaceEnable && !ProgressiveToInterlaceUnitInOPP)
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

	if (GPUVMEnable) {
		if (GPUVMPageTableLevels >= 3)
			*Tno_bw = UrgentExtraLatency + UrgentLatency * ((GPUVMPageTableLevels - 2) * (myHostVM->MaxPageTableLevels + 1) - 1);
		else
			*Tno_bw = 0;
	} else if (!DCCEnable)
		*Tno_bw = LineTime;
	else
		*Tno_bw = LineTime / 4;

	dst_y_prefetch_equ = VStartup - dml_max(TCalc + TWait, XFCRemoteSurfaceFlipDelay) / LineTime
			- (Tsetup + Tdm) / LineTime
			- (*DSTYAfterScaler + *DSTXAfterScaler / myPipe->HTotal);

	Tsw_oto = dml_max(PrefetchSourceLinesY, PrefetchSourceLinesC) * LineTime;

	if (myPipe->SourceScan == dm_horz) {
		*swath_width_luma_ub = dml_ceil(SwathWidthY - 1, myPipe->BlockWidth256BytesY) + myPipe->BlockWidth256BytesY;
		*swath_width_chroma_ub = dml_ceil(SwathWidthY / 2 - 1, myPipe->BlockWidth256BytesC) + myPipe->BlockWidth256BytesC;
	} else {
		*swath_width_luma_ub = dml_ceil(SwathWidthY - 1, myPipe->BlockHeight256BytesY) + myPipe->BlockHeight256BytesY;
		*swath_width_chroma_ub = dml_ceil(SwathWidthY / 2 - 1, myPipe->BlockHeight256BytesC) + myPipe->BlockHeight256BytesC;
	}

	prefetch_bw_oto = (PrefetchSourceLinesY * *swath_width_luma_ub * dml_ceil(BytePerPixelDETY, 1) + PrefetchSourceLinesC * *swath_width_chroma_ub * dml_ceil(BytePerPixelDETC, 2)) / Tsw_oto;


	if (GPUVMEnable == true) {
		Tvm_oto = dml_max(*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / prefetch_bw_oto,
				dml_max(UrgentExtraLatency + UrgentLatency * (GPUVMPageTableLevels * (HostVMDynamicLevels + 1) - 1),
					LineTime / 4.0));
	} else
		Tvm_oto = LineTime / 4.0;

	if ((GPUVMEnable == true || DCCEnable == true)) {
		Tr0_oto = dml_max(
				(MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / prefetch_bw_oto,
				dml_max(UrgentLatency * (HostVMDynamicLevels + 1), dml_max(LineTime - Tvm_oto, LineTime / 4)));
	} else
		Tr0_oto = (LineTime - Tvm_oto) / 2.0;

	Tvm_oto_lines = dml_ceil(4 * Tvm_oto / LineTime, 1) / 4.0;
	Tr0_oto_lines = dml_ceil(4 * Tr0_oto / LineTime, 1) / 4.0;
	Tsw_oto_lines = dml_ceil(4 * Tsw_oto / LineTime, 1) / 4.0;
	dst_y_prefetch_oto = Tvm_oto_lines + 2 * Tr0_oto_lines + Tsw_oto_lines + 0.75;

	dst_y_prefetch_equ = dml_floor(4.0 * (dst_y_prefetch_equ + 0.125), 1) / 4.0;

	if (dst_y_prefetch_oto < dst_y_prefetch_equ)
		*DestinationLinesForPrefetch = dst_y_prefetch_oto;
	else
		*DestinationLinesForPrefetch = dst_y_prefetch_equ;

	dml_print("DML: VStartup: %d\n", VStartup);
	dml_print("DML: TCalc: %f\n", TCalc);
	dml_print("DML: TWait: %f\n", TWait);
	dml_print("DML: XFCRemoteSurfaceFlipDelay: %f\n", XFCRemoteSurfaceFlipDelay);
	dml_print("DML: LineTime: %f\n", LineTime);
	dml_print("DML: Tsetup: %f\n", Tsetup);
	dml_print("DML: Tdm: %f\n", Tdm);
	dml_print("DML: DSTYAfterScaler: %f\n", *DSTYAfterScaler);
	dml_print("DML: DSTXAfterScaler: %f\n", *DSTXAfterScaler);
	dml_print("DML: HTotal: %d\n", myPipe->HTotal);

	*PrefetchBandwidth = 0;
	*DestinationLinesToRequestVMInVBlank = 0;
	*DestinationLinesToRequestRowInVBlank = 0;
	*VRatioPrefetchY = 0;
	*VRatioPrefetchC = 0;
	*RequiredPrefetchPixDataBWLuma = 0;
	if (*DestinationLinesForPrefetch > 1) {
		double PrefetchBandwidth1 = (PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor + 2 * MetaRowByte
				+ 2 * PixelPTEBytesPerRow * HostVMInefficiencyFactor
				+ PrefetchSourceLinesY * *swath_width_luma_ub * dml_ceil(BytePerPixelDETY, 1)
				+ PrefetchSourceLinesC * *swath_width_chroma_ub * dml_ceil(BytePerPixelDETC, 2))
				/ (*DestinationLinesForPrefetch * LineTime - *Tno_bw);

		double PrefetchBandwidth2 = (PDEAndMetaPTEBytesFrame *
				HostVMInefficiencyFactor + PrefetchSourceLinesY *
				*swath_width_luma_ub * dml_ceil(BytePerPixelDETY, 1) +
				PrefetchSourceLinesC * *swath_width_chroma_ub *
				dml_ceil(BytePerPixelDETC, 2)) /
				(*DestinationLinesForPrefetch * LineTime - *Tno_bw - 2 *
				UrgentLatency * (1 + HostVMDynamicLevels));

		double PrefetchBandwidth3 = (2 * MetaRowByte + 2 * PixelPTEBytesPerRow
				* HostVMInefficiencyFactor + PrefetchSourceLinesY *
				*swath_width_luma_ub * dml_ceil(BytePerPixelDETY, 1) +
				PrefetchSourceLinesC * *swath_width_chroma_ub *
				dml_ceil(BytePerPixelDETC, 2)) /
				(*DestinationLinesForPrefetch * LineTime -
				UrgentExtraLatency - UrgentLatency * (GPUVMPageTableLevels
				* (HostVMDynamicLevels + 1) - 1));

		double PrefetchBandwidth4 = (PrefetchSourceLinesY * *swath_width_luma_ub *
				dml_ceil(BytePerPixelDETY, 1) + PrefetchSourceLinesC *
				*swath_width_chroma_ub * dml_ceil(BytePerPixelDETC, 2)) /
				(*DestinationLinesForPrefetch * LineTime -
				UrgentExtraLatency - UrgentLatency * (GPUVMPageTableLevels
				* (HostVMDynamicLevels + 1) - 1) - 2 * UrgentLatency *
				(1 + HostVMDynamicLevels));

		if (VStartup == MaxVStartup && (PrefetchBandwidth1 > 4 * prefetch_bw_oto) && (*DestinationLinesForPrefetch - dml_ceil(Tsw_oto_lines, 1) / 4.0 - 0.75) * LineTime - *Tno_bw > 0) {
			PrefetchBandwidth1 = (PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor + 2 * MetaRowByte + 2 * PixelPTEBytesPerRow * HostVMInefficiencyFactor) / ((*DestinationLinesForPrefetch - dml_ceil(Tsw_oto_lines, 1) / 4.0 - 0.75) * LineTime - *Tno_bw);
		}
		if (*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / PrefetchBandwidth1 >= UrgentExtraLatency + UrgentLatency * (GPUVMPageTableLevels * (HostVMDynamicLevels + 1) - 1) && (MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / PrefetchBandwidth1 >= UrgentLatency * (1 + HostVMDynamicLevels)) {
			*PrefetchBandwidth = PrefetchBandwidth1;
		} else if (*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / PrefetchBandwidth2 >= UrgentExtraLatency + UrgentLatency * (GPUVMPageTableLevels * (HostVMDynamicLevels + 1) - 1) && (MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / PrefetchBandwidth2 < UrgentLatency * (1 + HostVMDynamicLevels)) {
			*PrefetchBandwidth = PrefetchBandwidth2;
		} else if (*Tno_bw + PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / PrefetchBandwidth3 < UrgentExtraLatency + UrgentLatency * (GPUVMPageTableLevels * (HostVMDynamicLevels + 1) - 1) && (MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / PrefetchBandwidth3 >= UrgentLatency * (1 + HostVMDynamicLevels)) {
			*PrefetchBandwidth = PrefetchBandwidth3;
		} else {
			*PrefetchBandwidth = PrefetchBandwidth4;
		}

		if (GPUVMEnable) {
			TimeForFetchingMetaPTE = dml_max(*Tno_bw + (double) PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / *PrefetchBandwidth,
					dml_max(UrgentExtraLatency + UrgentLatency * (GPUVMPageTableLevels * (HostVMDynamicLevels + 1) - 1), LineTime / 4));
		} else {
// 5/30/2018 - This was an optimization requested from Sy but now NumberOfCursors is no longer a factor
//             so if this needs to be reinstated, then it should be officially done in the VBA code as well.
//			if (mode_lib->NumberOfCursors > 0 || XFCEnabled)
				TimeForFetchingMetaPTE = LineTime / 4;
//			else
//				TimeForFetchingMetaPTE = 0.0;
		}

		if ((GPUVMEnable == true || DCCEnable == true)) {
			TimeForFetchingRowInVBlank =
					dml_max(
							(MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor)
									/ *PrefetchBandwidth,
							dml_max(
									UrgentLatency * (1 + HostVMDynamicLevels),
									dml_max(
											(LineTime
													- TimeForFetchingMetaPTE) / 2.0,
											LineTime
													/ 4.0)));
		} else {
// See note above dated 5/30/2018
//			if (NumberOfCursors > 0 || XFCEnabled)
				TimeForFetchingRowInVBlank = (LineTime - TimeForFetchingMetaPTE) / 2.0;
//			else // TODO: Did someone else add this??
//				TimeForFetchingRowInVBlank = 0.0;
		}

		*DestinationLinesToRequestVMInVBlank = dml_ceil(4.0 * TimeForFetchingMetaPTE / LineTime, 1.0) / 4.0;

		*DestinationLinesToRequestRowInVBlank = dml_ceil(4.0 * TimeForFetchingRowInVBlank / LineTime, 1.0) / 4.0;

		LinesToRequestPrefetchPixelData = *DestinationLinesForPrefetch
// See note above dated 5/30/2018
//						- ((NumberOfCursors > 0 || GPUVMEnable || DCCEnable) ?
						- ((GPUVMEnable || DCCEnable) ?
								(*DestinationLinesToRequestVMInVBlank + 2 * *DestinationLinesToRequestRowInVBlank) :
								0.0); // TODO: Did someone else add this??

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

			*RequiredPrefetchPixDataBWLuma = myPipe->DPPPerPlane
					* (double) PrefetchSourceLinesY / LinesToRequestPrefetchPixelData
					* dml_ceil(BytePerPixelDETY, 1)
					* *swath_width_luma_ub / LineTime;
			*RequiredPrefetchPixDataBWChroma = myPipe->DPPPerPlane
					* (double) PrefetchSourceLinesC / LinesToRequestPrefetchPixelData
					* dml_ceil(BytePerPixelDETC, 2)
					* *swath_width_chroma_ub / LineTime;
		} else {
			MyError = true;
			*VRatioPrefetchY = 0;
			*VRatioPrefetchC = 0;
			*RequiredPrefetchPixDataBWLuma = 0;
			*RequiredPrefetchPixDataBWChroma = 0;
		}

		dml_print("DML: Tvm: %fus\n", TimeForFetchingMetaPTE);
		dml_print("DML: Tr0: %fus\n", TimeForFetchingRowInVBlank);
		dml_print("DML: Tsw: %fus\n", (double)(*DestinationLinesForPrefetch) * LineTime - TimeForFetchingMetaPTE - TimeForFetchingRowInVBlank);
		dml_print("DML: Tpre: %fus\n", (double)(*DestinationLinesForPrefetch) * LineTime);
		dml_print("DML: row_bytes = dpte_row_bytes (per_pipe) = PixelPTEBytesPerRow = : %d\n", PixelPTEBytesPerRow);

	} else {
		MyError = true;
	}

	{
		double prefetch_vm_bw;
		double prefetch_row_bw;

		if (PDEAndMetaPTEBytesFrame == 0) {
			prefetch_vm_bw = 0;
		} else if (*DestinationLinesToRequestVMInVBlank > 0) {
			prefetch_vm_bw = PDEAndMetaPTEBytesFrame * HostVMInefficiencyFactor / (*DestinationLinesToRequestVMInVBlank * LineTime);
		} else {
			prefetch_vm_bw = 0;
			MyError = true;
		}
		if (MetaRowByte + PixelPTEBytesPerRow == 0) {
			prefetch_row_bw = 0;
		} else if (*DestinationLinesToRequestRowInVBlank > 0) {
			prefetch_row_bw = (MetaRowByte + PixelPTEBytesPerRow * HostVMInefficiencyFactor) / (*DestinationLinesToRequestRowInVBlank * LineTime);
		} else {
			prefetch_row_bw = 0;
			MyError = true;
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
	return VCOSpeed * 4 / dml_ceil(VCOSpeed * 4 / Clock, 1);
}

static double CalculateDCCConfiguration(
		bool DCCEnabled,
		bool DCCProgrammingAssumesScanDirectionUnknown,
		unsigned int ViewportWidth,
		unsigned int ViewportHeight,
		double DETBufferSize,
		unsigned int RequestHeight256Byte,
		unsigned int SwathHeight,
		enum dm_swizzle_mode TilingFormat,
		unsigned int BytePerPixel,
		enum scan_direction_class ScanOrientation,
		unsigned int *MaxUncompressedBlock,
		unsigned int *MaxCompressedBlock,
		unsigned int *Independent64ByteBlock)
{
	double MaximumDCCCompressionSurface = 0.0;
	enum {
		REQ_256Bytes,
		REQ_128BytesNonContiguous,
		REQ_128BytesContiguous,
		REQ_NA
	} Request = REQ_NA;

	if (DCCEnabled == true) {
		if (DCCProgrammingAssumesScanDirectionUnknown == true) {
			if (DETBufferSize >= RequestHeight256Byte * ViewportWidth * BytePerPixel
					&& DETBufferSize
							>= 256 / RequestHeight256Byte
									* ViewportHeight) {
				Request = REQ_256Bytes;
			} else if ((DETBufferSize
					< RequestHeight256Byte * ViewportWidth * BytePerPixel
					&& (BytePerPixel == 2 || BytePerPixel == 4))
					|| (DETBufferSize
							< 256 / RequestHeight256Byte
									* ViewportHeight
							&& BytePerPixel == 8
							&& (TilingFormat == dm_sw_4kb_d
									|| TilingFormat
											== dm_sw_4kb_d_x
									|| TilingFormat
											== dm_sw_var_d
									|| TilingFormat
											== dm_sw_var_d_x
									|| TilingFormat
											== dm_sw_64kb_d
									|| TilingFormat
											== dm_sw_64kb_d_x
									|| TilingFormat
											== dm_sw_64kb_d_t
									|| TilingFormat
											== dm_sw_64kb_r_x))) {
				Request = REQ_128BytesNonContiguous;
			} else {
				Request = REQ_128BytesContiguous;
			}
		} else {
			if (BytePerPixel == 1) {
				if (ScanOrientation == dm_vert || SwathHeight == 16) {
					Request = REQ_256Bytes;
				} else {
					Request = REQ_128BytesContiguous;
				}
			} else if (BytePerPixel == 2) {
				if ((ScanOrientation == dm_vert && SwathHeight == 16) || (ScanOrientation != dm_vert && SwathHeight == 8)) {
					Request = REQ_256Bytes;
				} else if (ScanOrientation == dm_vert) {
					Request = REQ_128BytesContiguous;
				} else {
					Request = REQ_128BytesNonContiguous;
				}
			} else if (BytePerPixel == 4) {
				if (SwathHeight == 8) {
					Request = REQ_256Bytes;
				} else if (ScanOrientation == dm_vert) {
					Request = REQ_128BytesContiguous;
				} else {
					Request = REQ_128BytesNonContiguous;
				}
			} else if (BytePerPixel == 8) {
				if (TilingFormat == dm_sw_4kb_d || TilingFormat == dm_sw_4kb_d_x
						|| TilingFormat == dm_sw_var_d
						|| TilingFormat == dm_sw_var_d_x
						|| TilingFormat == dm_sw_64kb_d
						|| TilingFormat == dm_sw_64kb_d_x
						|| TilingFormat == dm_sw_64kb_d_t
						|| TilingFormat == dm_sw_64kb_r_x) {
					if ((ScanOrientation == dm_vert && SwathHeight == 8)
							|| (ScanOrientation != dm_vert
									&& SwathHeight == 4)) {
						Request = REQ_256Bytes;
					} else if (ScanOrientation != dm_vert) {
						Request = REQ_128BytesContiguous;
					} else {
						Request = REQ_128BytesNonContiguous;
					}
				} else {
					if (ScanOrientation != dm_vert || SwathHeight == 8) {
						Request = REQ_256Bytes;
					} else {
						Request = REQ_128BytesContiguous;
					}
				}
			}
		}
	} else {
		Request = REQ_NA;
	}

	if (Request == REQ_256Bytes) {
		*MaxUncompressedBlock = 256;
		*MaxCompressedBlock = 256;
		*Independent64ByteBlock = false;
		MaximumDCCCompressionSurface = 4.0;
	} else if (Request == REQ_128BytesContiguous) {
		*MaxUncompressedBlock = 128;
		*MaxCompressedBlock = 128;
		*Independent64ByteBlock = false;
		MaximumDCCCompressionSurface = 2.0;
	} else if (Request == REQ_128BytesNonContiguous) {
		*MaxUncompressedBlock = 256;
		*MaxCompressedBlock = 64;
		*Independent64ByteBlock = true;
		MaximumDCCCompressionSurface = 4.0;
	} else {
		*MaxUncompressedBlock = 0;
		*MaxCompressedBlock = 0;
		*Independent64ByteBlock = 0;
		MaximumDCCCompressionSurface = 0.0;
	}

	return MaximumDCCCompressionSurface;
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
		bool GPUVMEnable,
		bool HostVMEnable,
		unsigned int HostVMMaxPageTableLevels,
		unsigned int HostVMCachedPageTableLevels,
		unsigned int VMMPageSize,
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
	unsigned int MPDEBytesFrame;
	unsigned int DCCMetaSurfaceBytes;
	unsigned int MacroTileSizeBytes;
	unsigned int MacroTileHeight;
	unsigned int ExtraDPDEBytesFrame;
	unsigned int PDEAndMetaPTEBytesFrame;
	unsigned int PixelPTEReqHeightPTEs = 0;

	if (DCCEnable == true) {
		*MetaRequestHeight = 8 * BlockHeight256Bytes;
		*MetaRequestWidth = 8 * BlockWidth256Bytes;
		if (ScanDirection == dm_horz) {
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
		if (GPUVMEnable == true) {
			*MetaPTEBytesFrame = (dml_ceil(
					(double) (DCCMetaSurfaceBytes - VMMPageSize)
							/ (8 * VMMPageSize),
					1) + 1) * 64;
			MPDEBytesFrame = 128 * ((mode_lib->vba.GPUVMMaxPageTableLevels + 1) * (mode_lib->vba.HostVMMaxPageTableLevels + 1) - 2);
		} else {
			*MetaPTEBytesFrame = 0;
			MPDEBytesFrame = 0;
		}
	} else {
		*MetaPTEBytesFrame = 0;
		MPDEBytesFrame = 0;
		*MetaRowByte = 0;
	}

	if (SurfaceTiling == dm_sw_linear || SurfaceTiling == dm_sw_gfx7_2d_thin_gl || SurfaceTiling == dm_sw_gfx7_2d_thin_l_vp) {
		MacroTileSizeBytes = 256;
		MacroTileHeight = BlockHeight256Bytes;
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

	if (GPUVMEnable == true && (mode_lib->vba.GPUVMMaxPageTableLevels + 1) * (mode_lib->vba.HostVMMaxPageTableLevels + 1) > 2) {
		if (ScanDirection == dm_horz) {
			*DPDE0BytesFrame = 64 * (dml_ceil(((Pitch * (dml_ceil(ViewportHeight - 1, MacroTileHeight) + MacroTileHeight) * BytePerPixel) - MacroTileSizeBytes) / (8 * 2097152), 1) + 1);
		} else {
			*DPDE0BytesFrame = 64 * (dml_ceil(((Pitch * (dml_ceil((double) SwathWidth - 1, MacroTileHeight) + MacroTileHeight) * BytePerPixel) - MacroTileSizeBytes) / (8 * 2097152), 1) + 1);
		}
		ExtraDPDEBytesFrame = 128 * ((mode_lib->vba.GPUVMMaxPageTableLevels + 1) * (mode_lib->vba.HostVMMaxPageTableLevels + 1) - 3);
	} else {
		*DPDE0BytesFrame = 0;
		ExtraDPDEBytesFrame = 0;
	}

	PDEAndMetaPTEBytesFrame = *MetaPTEBytesFrame + MPDEBytesFrame + *DPDE0BytesFrame
			+ ExtraDPDEBytesFrame;

	if (HostVMEnable == true) {
		PDEAndMetaPTEBytesFrame = PDEAndMetaPTEBytesFrame * (1 + 8 * (HostVMMaxPageTableLevels - HostVMCachedPageTableLevels));
	}

	if (GPUVMEnable == true) {
		double FractionOfPTEReturnDrop;

		if (SurfaceTiling == dm_sw_linear) {
			PixelPTEReqHeightPTEs = 1;
			*PixelPTEReqHeight = 1;
			*PixelPTEReqWidth = 8.0 * VMMPageSize / BytePerPixel;
			*PTERequestSize = 64;
			FractionOfPTEReturnDrop = 0;
		} else if (MacroTileSizeBytes == 4096) {
			PixelPTEReqHeightPTEs = 1;
			*PixelPTEReqHeight = MacroTileHeight;
			*PixelPTEReqWidth = 8 * *MacroTileWidth;
			*PTERequestSize = 64;
			if (ScanDirection == dm_horz)
				FractionOfPTEReturnDrop = 0;
			else
				FractionOfPTEReturnDrop = 7 / 8;
		} else if (VMMPageSize == 4096 && MacroTileSizeBytes > 4096) {
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
			*dpte_row_height = dml_min(128,
					1 << (unsigned int) dml_floor(
						dml_log2(
							(double) PTEBufferSizeInRequests * *PixelPTEReqWidth / Pitch),
						1));
			*dpte_row_width_ub = (dml_ceil((double) (Pitch * *dpte_row_height - 1) / *PixelPTEReqWidth, 1) + 1) * *PixelPTEReqWidth;
			*PixelPTEBytesPerRow = *dpte_row_width_ub / *PixelPTEReqWidth * *PTERequestSize;
		} else if (ScanDirection == dm_horz) {
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
	} else {
		*PixelPTEBytesPerRow = 0;
		*PTEBufferSizeNotExceeded = true;
	}
	dml_print("DML: vm_bytes = meta_pte_bytes_per_frame (per_pipe) = MetaPTEBytesFrame = : %d\n", *MetaPTEBytesFrame);

	if (HostVMEnable == true) {
		*PixelPTEBytesPerRow = *PixelPTEBytesPerRow * (1 + 8 * (HostVMMaxPageTableLevels - HostVMCachedPageTableLevels));
	}

	if (HostVMEnable == true) {
		*vm_group_bytes = 512;
		*dpte_group_bytes = 512;
	} else if (GPUVMEnable == true) {
		*vm_group_bytes = 2048;
		if (SurfaceTiling != dm_sw_linear && PixelPTEReqHeightPTEs == 1 && ScanDirection != dm_horz) {
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
	struct vba_vars_st *locals = &mode_lib->vba;
	unsigned int j, k;

	mode_lib->vba.WritebackDISPCLK = 0.0;
	mode_lib->vba.DISPCLKWithRamping = 0;
	mode_lib->vba.DISPCLKWithoutRamping = 0;
	mode_lib->vba.GlobalDPPCLK = 0.0;

	// DISPCLK and DPPCLK Calculation
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
			locals->PSCL_THROUGHPUT_LUMA[k] = dml_min(
					mode_lib->vba.MaxDCHUBToPSCLThroughput,
					mode_lib->vba.MaxPSCLToLBThroughput
							* mode_lib->vba.HRatio[k]
							/ dml_ceil(
									mode_lib->vba.htaps[k]
											/ 6.0,
									1));
		} else {
			locals->PSCL_THROUGHPUT_LUMA[k] = dml_min(
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
												/ locals->PSCL_THROUGHPUT_LUMA[k],
										1.0));

		if ((mode_lib->vba.htaps[k] > 6 || mode_lib->vba.vtaps[k] > 6)
				&& mode_lib->vba.DPPCLKUsingSingleDPPLuma
						< 2 * mode_lib->vba.PixelClock[k]) {
			mode_lib->vba.DPPCLKUsingSingleDPPLuma = 2 * mode_lib->vba.PixelClock[k];
		}

		if ((mode_lib->vba.SourcePixelFormat[k] != dm_420_8
				&& mode_lib->vba.SourcePixelFormat[k] != dm_420_10)) {
			locals->PSCL_THROUGHPUT_CHROMA[k] = 0.0;
			locals->DPPCLKUsingSingleDPP[k] =
					mode_lib->vba.DPPCLKUsingSingleDPPLuma;
		} else {
			if (mode_lib->vba.HRatio[k] > 1) {
				locals->PSCL_THROUGHPUT_CHROMA[k] =
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
				locals->PSCL_THROUGHPUT_CHROMA[k] = dml_min(
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
													/ locals->PSCL_THROUGHPUT_CHROMA[k],
											1.0));

			if ((mode_lib->vba.HTAPsChroma[k] > 6 || mode_lib->vba.VTAPsChroma[k] > 6)
					&& mode_lib->vba.DPPCLKUsingSingleDPPChroma
							< 2 * mode_lib->vba.PixelClock[k]) {
				mode_lib->vba.DPPCLKUsingSingleDPPChroma = 2
						* mode_lib->vba.PixelClock[k];
			}

			locals->DPPCLKUsingSingleDPP[k] = dml_max(
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
			mode_lib->vba.soc.clock_limits[mode_lib->vba.soc.num_states - 1].dispclk_mhz,
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
		mode_lib->vba.DPPCLK_calculated[k] = locals->DPPCLKUsingSingleDPP[k]
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

	// Urgent and B P-State/DRAM Clock Change Watermark
	DTRACE("   dcfclk_mhz         = %f", mode_lib->vba.DCFCLK);
	DTRACE("   return_bw_to_dcn   = %f", mode_lib->vba.ReturnBandwidthToDCN);
	DTRACE("   return_bus_bw      = %f", mode_lib->vba.ReturnBW);

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		bool MainPlaneDoesODMCombine = false;

		if (mode_lib->vba.SourceScan[k] == dm_horz)
			locals->SwathWidthSingleDPPY[k] = mode_lib->vba.ViewportWidth[k];
		else
			locals->SwathWidthSingleDPPY[k] = mode_lib->vba.ViewportHeight[k];

		if (mode_lib->vba.ODMCombineEnabled[k] == dm_odm_combine_mode_2to1)
			MainPlaneDoesODMCombine = true;
		for (j = 0; j < mode_lib->vba.NumberOfActivePlanes; ++j)
			if (mode_lib->vba.BlendingAndTiming[k] == j
					&& mode_lib->vba.ODMCombineEnabled[k] == dm_odm_combine_mode_2to1)
				MainPlaneDoesODMCombine = true;

		if (MainPlaneDoesODMCombine == true)
			locals->SwathWidthY[k] = dml_min(
					(double) locals->SwathWidthSingleDPPY[k],
					dml_round(
							mode_lib->vba.HActive[k] / 2.0
									* mode_lib->vba.HRatio[k]));
		else
			locals->SwathWidthY[k] = locals->SwathWidthSingleDPPY[k]
					/ mode_lib->vba.DPPPerPlane[k];
	}

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.SourcePixelFormat[k] == dm_444_64) {
			locals->BytePerPixelDETY[k] = 8;
			locals->BytePerPixelDETC[k] = 0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_444_32) {
			locals->BytePerPixelDETY[k] = 4;
			locals->BytePerPixelDETC[k] = 0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_444_16 || mode_lib->vba.SourcePixelFormat[k] == dm_mono_16) {
			locals->BytePerPixelDETY[k] = 2;
			locals->BytePerPixelDETC[k] = 0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_444_8 || mode_lib->vba.SourcePixelFormat[k] == dm_mono_8) {
			locals->BytePerPixelDETY[k] = 1;
			locals->BytePerPixelDETC[k] = 0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_420_8) {
			locals->BytePerPixelDETY[k] = 1;
			locals->BytePerPixelDETC[k] = 2;
		} else { // dm_420_10
			locals->BytePerPixelDETY[k] = 4.0 / 3.0;
			locals->BytePerPixelDETC[k] = 8.0 / 3.0;
		}
	}

	mode_lib->vba.TotalDataReadBandwidth = 0.0;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		locals->ReadBandwidthPlaneLuma[k] = locals->SwathWidthSingleDPPY[k]
				* dml_ceil(locals->BytePerPixelDETY[k], 1)
				/ (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k])
				* mode_lib->vba.VRatio[k];
		locals->ReadBandwidthPlaneChroma[k] = locals->SwathWidthSingleDPPY[k]
				/ 2 * dml_ceil(locals->BytePerPixelDETC[k], 2)
				/ (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k])
				* mode_lib->vba.VRatio[k] / 2;
		DTRACE(
				"   read_bw[%i] = %fBps",
				k,
				locals->ReadBandwidthPlaneLuma[k]
						+ locals->ReadBandwidthPlaneChroma[k]);
		mode_lib->vba.TotalDataReadBandwidth += locals->ReadBandwidthPlaneLuma[k]
				+ locals->ReadBandwidthPlaneChroma[k];
	}

	// DCFCLK Deep Sleep
	CalculateDCFCLKDeepSleep(
		mode_lib,
		mode_lib->vba.NumberOfActivePlanes,
		locals->BytePerPixelDETY,
		locals->BytePerPixelDETC,
		mode_lib->vba.VRatio,
		locals->SwathWidthY,
		mode_lib->vba.DPPPerPlane,
		mode_lib->vba.HRatio,
		mode_lib->vba.PixelClock,
		locals->PSCL_THROUGHPUT_LUMA,
		locals->PSCL_THROUGHPUT_CHROMA,
		locals->DPPCLK,
		&mode_lib->vba.DCFCLKDeepSleep);

	// DSCCLK
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if ((mode_lib->vba.BlendingAndTiming[k] != k) || !mode_lib->vba.DSCEnabled[k]) {
			locals->DSCCLK_calculated[k] = 0.0;
		} else {
			if (mode_lib->vba.OutputFormat[k] == dm_420
					|| mode_lib->vba.OutputFormat[k] == dm_n422)
				mode_lib->vba.DSCFormatFactor = 2;
			else
				mode_lib->vba.DSCFormatFactor = 1;
			if (mode_lib->vba.ODMCombineEnabled[k])
				locals->DSCCLK_calculated[k] =
						mode_lib->vba.PixelClockBackEnd[k] / 6
								/ mode_lib->vba.DSCFormatFactor
								/ (1
										- mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
												/ 100);
			else
				locals->DSCCLK_calculated[k] =
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
				locals->DSCDelay[k] =
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
				locals->DSCDelay[k] =
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
			locals->DSCDelay[k] = locals->DSCDelay[k]
					* mode_lib->vba.PixelClock[k]
					/ mode_lib->vba.PixelClockBackEnd[k];
		} else {
			locals->DSCDelay[k] = 0;
		}
	}

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k)
		for (j = 0; j < mode_lib->vba.NumberOfActivePlanes; ++j) // NumberOfPlanes
			if (j != k && mode_lib->vba.BlendingAndTiming[k] == j
					&& mode_lib->vba.DSCEnabled[j])
				locals->DSCDelay[k] = locals->DSCDelay[j];

	// Prefetch
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		unsigned int PDEAndMetaPTEBytesFrameY;
		unsigned int PixelPTEBytesPerRowY;
		unsigned int MetaRowByteY;
		unsigned int MetaRowByteC;
		unsigned int PDEAndMetaPTEBytesFrameC;
		unsigned int PixelPTEBytesPerRowC;
		bool         PTEBufferSizeNotExceededY;
		bool         PTEBufferSizeNotExceededC;

		Calculate256BBlockSizes(
				mode_lib->vba.SourcePixelFormat[k],
				mode_lib->vba.SurfaceTiling[k],
				dml_ceil(locals->BytePerPixelDETY[k], 1),
				dml_ceil(locals->BytePerPixelDETC[k], 2),
				&locals->BlockHeight256BytesY[k],
				&locals->BlockHeight256BytesC[k],
				&locals->BlockWidth256BytesY[k],
				&locals->BlockWidth256BytesC[k]);

		locals->PrefetchSourceLinesY[k] = CalculatePrefetchSourceLines(
				mode_lib,
				mode_lib->vba.VRatio[k],
				mode_lib->vba.vtaps[k],
				mode_lib->vba.Interlace[k],
				mode_lib->vba.ProgressiveToInterlaceUnitInOPP,
				mode_lib->vba.SwathHeightY[k],
				mode_lib->vba.ViewportYStartY[k],
				&locals->VInitPreFillY[k],
				&locals->MaxNumSwathY[k]);

		if ((mode_lib->vba.SourcePixelFormat[k] != dm_444_64
				&& mode_lib->vba.SourcePixelFormat[k] != dm_444_32
				&& mode_lib->vba.SourcePixelFormat[k] != dm_444_16
				&& mode_lib->vba.SourcePixelFormat[k] != dm_444_8)) {
			PDEAndMetaPTEBytesFrameC =
					CalculateVMAndRowBytes(
							mode_lib,
							mode_lib->vba.DCCEnable[k],
							locals->BlockHeight256BytesC[k],
							locals->BlockWidth256BytesC[k],
							mode_lib->vba.SourcePixelFormat[k],
							mode_lib->vba.SurfaceTiling[k],
							dml_ceil(
									locals->BytePerPixelDETC[k],
									2),
							mode_lib->vba.SourceScan[k],
							mode_lib->vba.ViewportWidth[k] / 2,
							mode_lib->vba.ViewportHeight[k] / 2,
							locals->SwathWidthY[k] / 2,
							mode_lib->vba.GPUVMEnable,
							mode_lib->vba.HostVMEnable,
							mode_lib->vba.HostVMMaxPageTableLevels,
							mode_lib->vba.HostVMCachedPageTableLevels,
							mode_lib->vba.VMMPageSize,
							mode_lib->vba.PTEBufferSizeInRequestsChroma,
							mode_lib->vba.PitchC[k],
							mode_lib->vba.DCCMetaPitchC[k],
							&locals->MacroTileWidthC[k],
							&MetaRowByteC,
							&PixelPTEBytesPerRowC,
							&PTEBufferSizeNotExceededC,
							&locals->dpte_row_width_chroma_ub[k],
							&locals->dpte_row_height_chroma[k],
							&locals->meta_req_width_chroma[k],
							&locals->meta_req_height_chroma[k],
							&locals->meta_row_width_chroma[k],
							&locals->meta_row_height_chroma[k],
							&locals->vm_group_bytes_chroma,
							&locals->dpte_group_bytes_chroma,
							&locals->PixelPTEReqWidthC[k],
							&locals->PixelPTEReqHeightC[k],
							&locals->PTERequestSizeC[k],
							&locals->dpde0_bytes_per_frame_ub_c[k],
							&locals->meta_pte_bytes_per_frame_ub_c[k]);

			locals->PrefetchSourceLinesC[k] = CalculatePrefetchSourceLines(
					mode_lib,
					mode_lib->vba.VRatio[k] / 2,
					mode_lib->vba.VTAPsChroma[k],
					mode_lib->vba.Interlace[k],
					mode_lib->vba.ProgressiveToInterlaceUnitInOPP,
					mode_lib->vba.SwathHeightC[k],
					mode_lib->vba.ViewportYStartC[k],
					&locals->VInitPreFillC[k],
					&locals->MaxNumSwathC[k]);
		} else {
			PixelPTEBytesPerRowC = 0;
			PDEAndMetaPTEBytesFrameC = 0;
			MetaRowByteC = 0;
			locals->MaxNumSwathC[k] = 0;
			locals->PrefetchSourceLinesC[k] = 0;
			locals->PTEBufferSizeInRequestsForLuma = mode_lib->vba.PTEBufferSizeInRequestsLuma + mode_lib->vba.PTEBufferSizeInRequestsChroma;
		}

		PDEAndMetaPTEBytesFrameY = CalculateVMAndRowBytes(
				mode_lib,
				mode_lib->vba.DCCEnable[k],
				locals->BlockHeight256BytesY[k],
				locals->BlockWidth256BytesY[k],
				mode_lib->vba.SourcePixelFormat[k],
				mode_lib->vba.SurfaceTiling[k],
				dml_ceil(locals->BytePerPixelDETY[k], 1),
				mode_lib->vba.SourceScan[k],
				mode_lib->vba.ViewportWidth[k],
				mode_lib->vba.ViewportHeight[k],
				locals->SwathWidthY[k],
				mode_lib->vba.GPUVMEnable,
				mode_lib->vba.HostVMEnable,
				mode_lib->vba.HostVMMaxPageTableLevels,
				mode_lib->vba.HostVMCachedPageTableLevels,
				mode_lib->vba.VMMPageSize,
				locals->PTEBufferSizeInRequestsForLuma,
				mode_lib->vba.PitchY[k],
				mode_lib->vba.DCCMetaPitchY[k],
				&locals->MacroTileWidthY[k],
				&MetaRowByteY,
				&PixelPTEBytesPerRowY,
				&PTEBufferSizeNotExceededY,
				&locals->dpte_row_width_luma_ub[k],
				&locals->dpte_row_height[k],
				&locals->meta_req_width[k],
				&locals->meta_req_height[k],
				&locals->meta_row_width[k],
				&locals->meta_row_height[k],
				&locals->vm_group_bytes[k],
				&locals->dpte_group_bytes[k],
				&locals->PixelPTEReqWidthY[k],
				&locals->PixelPTEReqHeightY[k],
				&locals->PTERequestSizeY[k],
				&locals->dpde0_bytes_per_frame_ub_l[k],
				&locals->meta_pte_bytes_per_frame_ub_l[k]);

		locals->PixelPTEBytesPerRow[k] = PixelPTEBytesPerRowY + PixelPTEBytesPerRowC;
		locals->PDEAndMetaPTEBytesFrame[k] = PDEAndMetaPTEBytesFrameY
				+ PDEAndMetaPTEBytesFrameC;
		locals->MetaRowByte[k] = MetaRowByteY + MetaRowByteC;

		CalculateActiveRowBandwidth(
				mode_lib->vba.GPUVMEnable,
				mode_lib->vba.SourcePixelFormat[k],
				mode_lib->vba.VRatio[k],
				mode_lib->vba.DCCEnable[k],
				mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k],
				MetaRowByteY,
				MetaRowByteC,
				locals->meta_row_height[k],
				locals->meta_row_height_chroma[k],
				PixelPTEBytesPerRowY,
				PixelPTEBytesPerRowC,
				locals->dpte_row_height[k],
				locals->dpte_row_height_chroma[k],
				&locals->meta_row_bw[k],
				&locals->dpte_row_bw[k]);
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

	mode_lib->vba.UrgentOutOfOrderReturnPerChannel = dml_max3(
			mode_lib->vba.UrgentOutOfOrderReturnPerChannelPixelDataOnly,
			mode_lib->vba.UrgentOutOfOrderReturnPerChannelPixelMixedWithVMData,
			mode_lib->vba.UrgentOutOfOrderReturnPerChannelVMDataOnly);

	mode_lib->vba.UrgentRoundTripAndOutOfOrderLatency =
			(mode_lib->vba.RoundTripPingLatencyCycles + 32) / mode_lib->vba.DCFCLK
					+ mode_lib->vba.UrgentOutOfOrderReturnPerChannel
							* mode_lib->vba.NumberOfChannels
							/ mode_lib->vba.ReturnBW;

	mode_lib->vba.UrgentExtraLatency = CalculateExtraLatency(
			mode_lib->vba.UrgentRoundTripAndOutOfOrderLatency,
			mode_lib->vba.TotalActiveDPP,
			mode_lib->vba.PixelChunkSizeInKByte,
			mode_lib->vba.TotalDCCActiveDPP,
			mode_lib->vba.MetaChunkSize,
			mode_lib->vba.ReturnBW,
			mode_lib->vba.GPUVMEnable,
			mode_lib->vba.HostVMEnable,
			mode_lib->vba.NumberOfActivePlanes,
			mode_lib->vba.DPPPerPlane,
			locals->dpte_group_bytes,
			mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
			mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
			mode_lib->vba.HostVMMaxPageTableLevels,
			mode_lib->vba.HostVMCachedPageTableLevels);


	mode_lib->vba.TCalc = 24.0 / mode_lib->vba.DCFCLKDeepSleep;

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.BlendingAndTiming[k] == k) {
			if (mode_lib->vba.WritebackEnable[k] == true) {
				locals->WritebackDelay[mode_lib->vba.VoltageLevel][k] =
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
				locals->WritebackDelay[mode_lib->vba.VoltageLevel][k] = 0;
			for (j = 0; j < mode_lib->vba.NumberOfActivePlanes; ++j) {
				if (mode_lib->vba.BlendingAndTiming[j] == k
						&& mode_lib->vba.WritebackEnable[j] == true) {
					locals->WritebackDelay[mode_lib->vba.VoltageLevel][k] =
							dml_max(
									locals->WritebackDelay[mode_lib->vba.VoltageLevel][k],
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
				locals->WritebackDelay[mode_lib->vba.VoltageLevel][k] =
						locals->WritebackDelay[mode_lib->vba.VoltageLevel][j];

	mode_lib->vba.VStartupLines = 13;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		locals->MaxVStartupLines[k] = mode_lib->vba.VTotal[k] - mode_lib->vba.VActive[k] - dml_max(1.0, dml_ceil(locals->WritebackDelay[mode_lib->vba.VoltageLevel][k] / (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k]), 1));
	}

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k)
		locals->MaximumMaxVStartupLines = dml_max(locals->MaximumMaxVStartupLines, locals->MaxVStartupLines[k]);

	// We don't really care to iterate between the various prefetch modes
	//mode_lib->vba.PrefetchERROR = CalculateMinAndMaxPrefetchMode(mode_lib->vba.AllowDRAMSelfRefreshOrDRAMClockChangeInVblank, &mode_lib->vba.MinPrefetchMode, &mode_lib->vba.MaxPrefetchMode);
	mode_lib->vba.UrgentLatency = dml_max3(mode_lib->vba.UrgentLatencyPixelDataOnly, mode_lib->vba.UrgentLatencyPixelMixedWithVMData, mode_lib->vba.UrgentLatencyVMDataOnly);

	do {
		double MaxTotalRDBandwidth = 0;
		double MaxTotalRDBandwidthNoUrgentBurst = 0;
		bool DestinationLineTimesForPrefetchLessThan2 = false;
		bool VRatioPrefetchMoreThan4 = false;
		double TWait = CalculateTWait(
				mode_lib->vba.PrefetchMode[mode_lib->vba.VoltageLevel][mode_lib->vba.maxMpcComb],
				mode_lib->vba.DRAMClockChangeLatency,
				mode_lib->vba.UrgentLatency,
				mode_lib->vba.SREnterPlusExitTime);

		for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
			Pipe   myPipe;
			HostVM myHostVM;

			if (mode_lib->vba.XFCEnabled[k] == true) {
				mode_lib->vba.XFCRemoteSurfaceFlipDelay =
						CalculateRemoteSurfaceFlipDelay(
								mode_lib,
								mode_lib->vba.VRatio[k],
								locals->SwathWidthY[k],
								dml_ceil(
										locals->BytePerPixelDETY[k],
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

			myPipe.DPPCLK = locals->DPPCLK[k];
			myPipe.DISPCLK = mode_lib->vba.DISPCLK;
			myPipe.PixelClock = mode_lib->vba.PixelClock[k];
			myPipe.DCFCLKDeepSleep = mode_lib->vba.DCFCLKDeepSleep;
			myPipe.DPPPerPlane = mode_lib->vba.DPPPerPlane[k];
			myPipe.ScalerEnabled = mode_lib->vba.ScalerEnabled[k];
			myPipe.SourceScan = mode_lib->vba.SourceScan[k];
			myPipe.BlockWidth256BytesY = locals->BlockWidth256BytesY[k];
			myPipe.BlockHeight256BytesY = locals->BlockHeight256BytesY[k];
			myPipe.BlockWidth256BytesC = locals->BlockWidth256BytesC[k];
			myPipe.BlockHeight256BytesC = locals->BlockHeight256BytesC[k];
			myPipe.InterlaceEnable = mode_lib->vba.Interlace[k];
			myPipe.NumberOfCursors = mode_lib->vba.NumberOfCursors[k];
			myPipe.VBlank = mode_lib->vba.VTotal[k] - mode_lib->vba.VActive[k];
			myPipe.HTotal = mode_lib->vba.HTotal[k];


			myHostVM.Enable = mode_lib->vba.HostVMEnable;
			myHostVM.MaxPageTableLevels = mode_lib->vba.HostVMMaxPageTableLevels;
			myHostVM.CachedPageTableLevels = mode_lib->vba.HostVMCachedPageTableLevels;

			mode_lib->vba.ErrorResult[k] =
					CalculatePrefetchSchedule(
							mode_lib,
							mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
							mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
							&myPipe,
							locals->DSCDelay[k],
							mode_lib->vba.DPPCLKDelaySubtotal,
							mode_lib->vba.DPPCLKDelaySCL,
							mode_lib->vba.DPPCLKDelaySCLLBOnly,
							mode_lib->vba.DPPCLKDelayCNVCFormater,
							mode_lib->vba.DPPCLKDelayCNVCCursor,
							mode_lib->vba.DISPCLKDelaySubtotal,
							(unsigned int) (locals->SwathWidthY[k]
									/ mode_lib->vba.HRatio[k]),
							mode_lib->vba.OutputFormat[k],
							mode_lib->vba.MaxInterDCNTileRepeaters,
							dml_min(mode_lib->vba.VStartupLines, locals->MaxVStartupLines[k]),
							locals->MaxVStartupLines[k],
							mode_lib->vba.GPUVMMaxPageTableLevels,
							mode_lib->vba.GPUVMEnable,
							&myHostVM,
							mode_lib->vba.DynamicMetadataEnable[k],
							mode_lib->vba.DynamicMetadataLinesBeforeActiveRequired[k],
							mode_lib->vba.DynamicMetadataTransmittedBytes[k],
							mode_lib->vba.DCCEnable[k],
							mode_lib->vba.UrgentLatency,
							mode_lib->vba.UrgentExtraLatency,
							mode_lib->vba.TCalc,
							locals->PDEAndMetaPTEBytesFrame[k],
							locals->MetaRowByte[k],
							locals->PixelPTEBytesPerRow[k],
							locals->PrefetchSourceLinesY[k],
							locals->SwathWidthY[k],
							locals->BytePerPixelDETY[k],
							locals->VInitPreFillY[k],
							locals->MaxNumSwathY[k],
							locals->PrefetchSourceLinesC[k],
							locals->BytePerPixelDETC[k],
							locals->VInitPreFillC[k],
							locals->MaxNumSwathC[k],
							mode_lib->vba.SwathHeightY[k],
							mode_lib->vba.SwathHeightC[k],
							TWait,
							mode_lib->vba.XFCEnabled[k],
							mode_lib->vba.XFCRemoteSurfaceFlipDelay,
							mode_lib->vba.ProgressiveToInterlaceUnitInOPP,
							&locals->DSTXAfterScaler[k],
							&locals->DSTYAfterScaler[k],
							&locals->DestinationLinesForPrefetch[k],
							&locals->PrefetchBandwidth[k],
							&locals->DestinationLinesToRequestVMInVBlank[k],
							&locals->DestinationLinesToRequestRowInVBlank[k],
							&locals->VRatioPrefetchY[k],
							&locals->VRatioPrefetchC[k],
							&locals->RequiredPrefetchPixDataBWLuma[k],
							&locals->RequiredPrefetchPixDataBWChroma[k],
							&locals->VStartupRequiredWhenNotEnoughTimeForDynamicMetadata,
							&locals->Tno_bw[k],
							&locals->prefetch_vmrow_bw[k],
							&locals->swath_width_luma_ub[k],
							&locals->swath_width_chroma_ub[k],
							&mode_lib->vba.VUpdateOffsetPix[k],
							&mode_lib->vba.VUpdateWidthPix[k],
							&mode_lib->vba.VReadyOffsetPix[k]);
			if (mode_lib->vba.BlendingAndTiming[k] == k) {
				locals->VStartup[k] = dml_min(
						mode_lib->vba.VStartupLines,
						locals->MaxVStartupLines[k]);
				if (locals->VStartupRequiredWhenNotEnoughTimeForDynamicMetadata
						!= 0) {
					locals->VStartup[k] =
							locals->VStartupRequiredWhenNotEnoughTimeForDynamicMetadata;
				}
			} else {
				locals->VStartup[k] =
						dml_min(
								mode_lib->vba.VStartupLines,
								locals->MaxVStartupLines[mode_lib->vba.BlendingAndTiming[k]]);
			}
		}

		for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
			unsigned int m;

			locals->cursor_bw[k] = 0;
			locals->cursor_bw_pre[k] = 0;
			for (m = 0; m < mode_lib->vba.NumberOfCursors[k]; m++) {
				locals->cursor_bw[k] += mode_lib->vba.CursorWidth[k][m] * mode_lib->vba.CursorBPP[k][m] / 8.0 / (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k]) * mode_lib->vba.VRatio[k];
				locals->cursor_bw_pre[k] += mode_lib->vba.CursorWidth[k][m] * mode_lib->vba.CursorBPP[k][m] / 8.0 / (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k]) * locals->VRatioPrefetchY[k];
			}

			CalculateUrgentBurstFactor(
					mode_lib->vba.DETBufferSizeInKByte,
					mode_lib->vba.SwathHeightY[k],
					mode_lib->vba.SwathHeightC[k],
					locals->SwathWidthY[k],
					mode_lib->vba.HTotal[k] /
					mode_lib->vba.PixelClock[k],
					mode_lib->vba.UrgentLatency,
					mode_lib->vba.CursorBufferSize,
					mode_lib->vba.CursorWidth[k][0] + mode_lib->vba.CursorWidth[k][1],
					dml_max(mode_lib->vba.CursorBPP[k][0], mode_lib->vba.CursorBPP[k][1]),
					mode_lib->vba.VRatio[k],
					locals->VRatioPrefetchY[k],
					locals->VRatioPrefetchC[k],
					locals->BytePerPixelDETY[k],
					locals->BytePerPixelDETC[k],
					&locals->UrgentBurstFactorCursor[k],
					&locals->UrgentBurstFactorCursorPre[k],
					&locals->UrgentBurstFactorLuma[k],
					&locals->UrgentBurstFactorLumaPre[k],
					&locals->UrgentBurstFactorChroma[k],
					&locals->UrgentBurstFactorChromaPre[k],
					&locals->NotEnoughUrgentLatencyHiding,
					&locals->NotEnoughUrgentLatencyHidingPre);

			if (mode_lib->vba.UseUrgentBurstBandwidth == false) {
				locals->UrgentBurstFactorLuma[k] = 1;
				locals->UrgentBurstFactorChroma[k] = 1;
				locals->UrgentBurstFactorCursor[k] = 1;
				locals->UrgentBurstFactorLumaPre[k] = 1;
				locals->UrgentBurstFactorChromaPre[k] = 1;
				locals->UrgentBurstFactorCursorPre[k] = 1;
			}

			MaxTotalRDBandwidth = MaxTotalRDBandwidth +
				dml_max3(locals->prefetch_vmrow_bw[k],
					locals->ReadBandwidthPlaneLuma[k] * locals->UrgentBurstFactorLuma[k]
					+ locals->ReadBandwidthPlaneChroma[k] * locals->UrgentBurstFactorChroma[k] + locals->cursor_bw[k]
					* locals->UrgentBurstFactorCursor[k] + locals->meta_row_bw[k] + locals->dpte_row_bw[k],
					locals->RequiredPrefetchPixDataBWLuma[k] * locals->UrgentBurstFactorLumaPre[k] + locals->RequiredPrefetchPixDataBWChroma[k]
					* locals->UrgentBurstFactorChromaPre[k] + locals->cursor_bw_pre[k] * locals->UrgentBurstFactorCursorPre[k]);

			MaxTotalRDBandwidthNoUrgentBurst = MaxTotalRDBandwidthNoUrgentBurst +
				dml_max3(locals->prefetch_vmrow_bw[k],
					locals->ReadBandwidthPlaneLuma[k] + locals->ReadBandwidthPlaneChroma[k] + locals->cursor_bw[k]
					+ locals->meta_row_bw[k] + locals->dpte_row_bw[k],
					locals->RequiredPrefetchPixDataBWLuma[k] + locals->RequiredPrefetchPixDataBWChroma[k] + locals->cursor_bw_pre[k]);

			if (locals->DestinationLinesForPrefetch[k] < 2)
				DestinationLineTimesForPrefetchLessThan2 = true;
			if (locals->VRatioPrefetchY[k] > 4 || locals->VRatioPrefetchC[k] > 4)
				VRatioPrefetchMoreThan4 = true;
		}
		mode_lib->vba.FractionOfUrgentBandwidth = MaxTotalRDBandwidthNoUrgentBurst / mode_lib->vba.ReturnBW;

		if (MaxTotalRDBandwidth <= mode_lib->vba.ReturnBW && locals->NotEnoughUrgentLatencyHiding == 0 && locals->NotEnoughUrgentLatencyHidingPre == 0 && !VRatioPrefetchMoreThan4
				&& !DestinationLineTimesForPrefetchLessThan2)
			mode_lib->vba.PrefetchModeSupported = true;
		else {
			mode_lib->vba.PrefetchModeSupported = false;
			dml_print(
					"DML: CalculatePrefetchSchedule ***failed***. Bandwidth violation. Results are NOT valid\n");
		}

		if (mode_lib->vba.PrefetchModeSupported == true) {
			mode_lib->vba.BandwidthAvailableForImmediateFlip = mode_lib->vba.ReturnBW;
			for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
				mode_lib->vba.BandwidthAvailableForImmediateFlip =
						mode_lib->vba.BandwidthAvailableForImmediateFlip
							- dml_max(
								locals->ReadBandwidthPlaneLuma[k] * locals->UrgentBurstFactorLuma[k]
								+ locals->ReadBandwidthPlaneChroma[k] * locals->UrgentBurstFactorChroma[k]
								+ locals->cursor_bw[k] * locals->UrgentBurstFactorCursor[k],
								locals->RequiredPrefetchPixDataBWLuma[k] * locals->UrgentBurstFactorLumaPre[k] +
								locals->RequiredPrefetchPixDataBWChroma[k] * locals->UrgentBurstFactorChromaPre[k] +
								locals->cursor_bw_pre[k] * locals->UrgentBurstFactorCursorPre[k]);
			}

			mode_lib->vba.TotImmediateFlipBytes = 0;
			for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
				mode_lib->vba.TotImmediateFlipBytes = mode_lib->vba.TotImmediateFlipBytes + locals->PDEAndMetaPTEBytesFrame[k] + locals->MetaRowByte[k] + locals->PixelPTEBytesPerRow[k];
			}
			for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
				CalculateFlipSchedule(
						mode_lib,
						mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
						mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
						mode_lib->vba.UrgentExtraLatency,
						mode_lib->vba.UrgentLatency,
						mode_lib->vba.GPUVMMaxPageTableLevels,
						mode_lib->vba.HostVMEnable,
						mode_lib->vba.HostVMMaxPageTableLevels,
						mode_lib->vba.HostVMCachedPageTableLevels,
						mode_lib->vba.GPUVMEnable,
						locals->PDEAndMetaPTEBytesFrame[k],
						locals->MetaRowByte[k],
						locals->PixelPTEBytesPerRow[k],
						mode_lib->vba.BandwidthAvailableForImmediateFlip,
						mode_lib->vba.TotImmediateFlipBytes,
						mode_lib->vba.SourcePixelFormat[k],
						mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k],
						mode_lib->vba.VRatio[k],
						locals->Tno_bw[k],
						mode_lib->vba.DCCEnable[k],
						locals->dpte_row_height[k],
						locals->meta_row_height[k],
						locals->dpte_row_height_chroma[k],
						locals->meta_row_height_chroma[k],
						&locals->DestinationLinesToRequestVMInImmediateFlip[k],
						&locals->DestinationLinesToRequestRowInImmediateFlip[k],
						&locals->final_flip_bw[k],
						&locals->ImmediateFlipSupportedForPipe[k]);
			}
			mode_lib->vba.total_dcn_read_bw_with_flip = 0.0;
			mode_lib->vba.total_dcn_read_bw_with_flip_no_urgent_burst = 0.0;
			for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
				mode_lib->vba.total_dcn_read_bw_with_flip =
						mode_lib->vba.total_dcn_read_bw_with_flip + dml_max3(
							locals->prefetch_vmrow_bw[k],
							locals->final_flip_bw[k] + locals->ReadBandwidthLuma[k] * locals->UrgentBurstFactorLuma[k]
							+ locals->ReadBandwidthChroma[k] * locals->UrgentBurstFactorChroma[k] + locals->cursor_bw[k] * locals->UrgentBurstFactorCursor[k],
							locals->final_flip_bw[k] + locals->RequiredPrefetchPixDataBWLuma[k] * locals->UrgentBurstFactorLumaPre[k]
							+ locals->RequiredPrefetchPixDataBWChroma[k] * locals->UrgentBurstFactorChromaPre[k]
							+ locals->cursor_bw_pre[k] * locals->UrgentBurstFactorCursorPre[k]);
				mode_lib->vba.total_dcn_read_bw_with_flip_no_urgent_burst =
				mode_lib->vba.total_dcn_read_bw_with_flip_no_urgent_burst +
					dml_max3(locals->prefetch_vmrow_bw[k],
						locals->final_flip_bw[k] + locals->ReadBandwidthPlaneLuma[k] + locals->ReadBandwidthPlaneChroma[k] + locals->cursor_bw[k],
						locals->final_flip_bw[k] + locals->RequiredPrefetchPixDataBWLuma[k] + locals->RequiredPrefetchPixDataBWChroma[k] + locals->cursor_bw_pre[k]);

			}
			mode_lib->vba.FractionOfUrgentBandwidthImmediateFlip = mode_lib->vba.total_dcn_read_bw_with_flip_no_urgent_burst / mode_lib->vba.ReturnBW;

			mode_lib->vba.ImmediateFlipSupported = true;
			if (mode_lib->vba.total_dcn_read_bw_with_flip > mode_lib->vba.ReturnBW) {
				mode_lib->vba.ImmediateFlipSupported = false;
			}
			for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
				if (locals->ImmediateFlipSupportedForPipe[k] == false) {
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
			&& ((!mode_lib->vba.ImmediateFlipSupport && !mode_lib->vba.HostVMEnable)
					|| mode_lib->vba.ImmediateFlipSupported))
			|| locals->MaximumMaxVStartupLines < mode_lib->vba.VStartupLines));

	//Watermarks and NB P-State/DRAM Clock Change Support
	{
		enum clock_change_support DRAMClockChangeSupport; // dummy
		CalculateWatermarksAndDRAMSpeedChangeSupport(
				mode_lib,
				mode_lib->vba.PrefetchMode[mode_lib->vba.VoltageLevel][mode_lib->vba.maxMpcComb],
				mode_lib->vba.NumberOfActivePlanes,
				mode_lib->vba.MaxLineBufferLines,
				mode_lib->vba.LineBufferSize,
				mode_lib->vba.DPPOutputBufferPixels,
				mode_lib->vba.DETBufferSizeInKByte,
				mode_lib->vba.WritebackInterfaceLumaBufferSize,
				mode_lib->vba.WritebackInterfaceChromaBufferSize,
				mode_lib->vba.DCFCLK,
				mode_lib->vba.UrgentOutOfOrderReturnPerChannel * mode_lib->vba.NumberOfChannels,
				mode_lib->vba.ReturnBW,
				mode_lib->vba.GPUVMEnable,
				locals->dpte_group_bytes,
				mode_lib->vba.MetaChunkSize,
				mode_lib->vba.UrgentLatency,
				mode_lib->vba.UrgentExtraLatency,
				mode_lib->vba.WritebackLatency,
				mode_lib->vba.WritebackChunkSize,
				mode_lib->vba.SOCCLK,
				mode_lib->vba.DRAMClockChangeLatency,
				mode_lib->vba.SRExitTime,
				mode_lib->vba.SREnterPlusExitTime,
				mode_lib->vba.DCFCLKDeepSleep,
				mode_lib->vba.DPPPerPlane,
				mode_lib->vba.DCCEnable,
				locals->DPPCLK,
				locals->SwathWidthSingleDPPY,
				mode_lib->vba.SwathHeightY,
				locals->ReadBandwidthPlaneLuma,
				mode_lib->vba.SwathHeightC,
				locals->ReadBandwidthPlaneChroma,
				mode_lib->vba.LBBitPerPixel,
				locals->SwathWidthY,
				mode_lib->vba.HRatio,
				mode_lib->vba.vtaps,
				mode_lib->vba.VTAPsChroma,
				mode_lib->vba.VRatio,
				mode_lib->vba.HTotal,
				mode_lib->vba.PixelClock,
				mode_lib->vba.BlendingAndTiming,
				locals->BytePerPixelDETY,
				locals->BytePerPixelDETC,
				mode_lib->vba.WritebackEnable,
				mode_lib->vba.WritebackPixelFormat,
				mode_lib->vba.WritebackDestinationWidth,
				mode_lib->vba.WritebackDestinationHeight,
				mode_lib->vba.WritebackSourceHeight,
				&DRAMClockChangeSupport,
				&mode_lib->vba.UrgentWatermark,
				&mode_lib->vba.WritebackUrgentWatermark,
				&mode_lib->vba.DRAMClockChangeWatermark,
				&mode_lib->vba.WritebackDRAMClockChangeWatermark,
				&mode_lib->vba.StutterExitWatermark,
				&mode_lib->vba.StutterEnterPlusExitWatermark,
				&mode_lib->vba.MinActiveDRAMClockChangeLatencySupported);
	}


	//Display Pipeline Delivery Time in Prefetch, Groups
	CalculatePixelDeliveryTimes(
		mode_lib->vba.NumberOfActivePlanes,
		mode_lib->vba.VRatio,
		locals->VRatioPrefetchY,
		locals->VRatioPrefetchC,
		locals->swath_width_luma_ub,
		locals->swath_width_chroma_ub,
		mode_lib->vba.DPPPerPlane,
		mode_lib->vba.HRatio,
		mode_lib->vba.PixelClock,
		locals->PSCL_THROUGHPUT_LUMA,
		locals->PSCL_THROUGHPUT_CHROMA,
		locals->DPPCLK,
		locals->BytePerPixelDETC,
		mode_lib->vba.SourceScan,
		locals->BlockWidth256BytesY,
		locals->BlockHeight256BytesY,
		locals->BlockWidth256BytesC,
		locals->BlockHeight256BytesC,
		locals->DisplayPipeLineDeliveryTimeLuma,
		locals->DisplayPipeLineDeliveryTimeChroma,
		locals->DisplayPipeLineDeliveryTimeLumaPrefetch,
		locals->DisplayPipeLineDeliveryTimeChromaPrefetch,
		locals->DisplayPipeRequestDeliveryTimeLuma,
		locals->DisplayPipeRequestDeliveryTimeChroma,
		locals->DisplayPipeRequestDeliveryTimeLumaPrefetch,
		locals->DisplayPipeRequestDeliveryTimeChromaPrefetch);

	CalculateMetaAndPTETimes(
		mode_lib->vba.NumberOfActivePlanes,
		mode_lib->vba.GPUVMEnable,
		mode_lib->vba.MetaChunkSize,
		mode_lib->vba.MinMetaChunkSizeBytes,
		mode_lib->vba.GPUVMMaxPageTableLevels,
		mode_lib->vba.HTotal,
		mode_lib->vba.VRatio,
		locals->VRatioPrefetchY,
		locals->VRatioPrefetchC,
		locals->DestinationLinesToRequestRowInVBlank,
		locals->DestinationLinesToRequestRowInImmediateFlip,
		locals->DestinationLinesToRequestVMInVBlank,
		locals->DestinationLinesToRequestVMInImmediateFlip,
		mode_lib->vba.DCCEnable,
		mode_lib->vba.PixelClock,
		locals->BytePerPixelDETY,
		locals->BytePerPixelDETC,
		mode_lib->vba.SourceScan,
		locals->dpte_row_height,
		locals->dpte_row_height_chroma,
		locals->meta_row_width,
		locals->meta_row_height,
		locals->meta_req_width,
		locals->meta_req_height,
		locals->dpte_group_bytes,
		locals->PTERequestSizeY,
		locals->PTERequestSizeC,
		locals->PixelPTEReqWidthY,
		locals->PixelPTEReqHeightY,
		locals->PixelPTEReqWidthC,
		locals->PixelPTEReqHeightC,
		locals->dpte_row_width_luma_ub,
		locals->dpte_row_width_chroma_ub,
		locals->vm_group_bytes,
		locals->dpde0_bytes_per_frame_ub_l,
		locals->dpde0_bytes_per_frame_ub_c,
		locals->meta_pte_bytes_per_frame_ub_l,
		locals->meta_pte_bytes_per_frame_ub_c,
		locals->DST_Y_PER_PTE_ROW_NOM_L,
		locals->DST_Y_PER_PTE_ROW_NOM_C,
		locals->DST_Y_PER_META_ROW_NOM_L,
		locals->TimePerMetaChunkNominal,
		locals->TimePerMetaChunkVBlank,
		locals->TimePerMetaChunkFlip,
		locals->time_per_pte_group_nom_luma,
		locals->time_per_pte_group_vblank_luma,
		locals->time_per_pte_group_flip_luma,
		locals->time_per_pte_group_nom_chroma,
		locals->time_per_pte_group_vblank_chroma,
		locals->time_per_pte_group_flip_chroma,
		locals->TimePerVMGroupVBlank,
		locals->TimePerVMGroupFlip,
		locals->TimePerVMRequestVBlank,
		locals->TimePerVMRequestFlip);


	// Min TTUVBlank
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.PrefetchMode[mode_lib->vba.VoltageLevel][mode_lib->vba.maxMpcComb] == 0) {
			locals->AllowDRAMClockChangeDuringVBlank[k] = true;
			locals->AllowDRAMSelfRefreshDuringVBlank[k] = true;
			locals->MinTTUVBlank[k] = dml_max(
					mode_lib->vba.DRAMClockChangeWatermark,
					dml_max(
							mode_lib->vba.StutterEnterPlusExitWatermark,
							mode_lib->vba.UrgentWatermark));
		} else if (mode_lib->vba.PrefetchMode[mode_lib->vba.VoltageLevel][mode_lib->vba.maxMpcComb] == 1) {
			locals->AllowDRAMClockChangeDuringVBlank[k] = false;
			locals->AllowDRAMSelfRefreshDuringVBlank[k] = true;
			locals->MinTTUVBlank[k] = dml_max(
					mode_lib->vba.StutterEnterPlusExitWatermark,
					mode_lib->vba.UrgentWatermark);
		} else {
			locals->AllowDRAMClockChangeDuringVBlank[k] = false;
			locals->AllowDRAMSelfRefreshDuringVBlank[k] = false;
			locals->MinTTUVBlank[k] = mode_lib->vba.UrgentWatermark;
		}
		if (!mode_lib->vba.DynamicMetadataEnable[k])
			locals->MinTTUVBlank[k] = mode_lib->vba.TCalc
					+ locals->MinTTUVBlank[k];
	}

	// DCC Configuration
	mode_lib->vba.ActiveDPPs = 0;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		locals->MaximumDCCCompressionYSurface[k] = CalculateDCCConfiguration(
			mode_lib->vba.DCCEnable[k],
			false, // We should always know the direction DCCProgrammingAssumesScanDirectionUnknown,
			mode_lib->vba.ViewportWidth[k],
			mode_lib->vba.ViewportHeight[k],
			mode_lib->vba.DETBufferSizeInKByte * 1024,
			locals->BlockHeight256BytesY[k],
			mode_lib->vba.SwathHeightY[k],
			mode_lib->vba.SurfaceTiling[k],
			locals->BytePerPixelDETY[k],
			mode_lib->vba.SourceScan[k],
			&locals->DCCYMaxUncompressedBlock[k],
			&locals->DCCYMaxCompressedBlock[k],
			&locals->DCCYIndependent64ByteBlock[k]);
	}

	//XFC Parameters:
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.XFCEnabled[k] == true) {
			double TWait;

			locals->XFCSlaveVUpdateOffset[k] = mode_lib->vba.XFCTSlvVupdateOffset;
			locals->XFCSlaveVupdateWidth[k] = mode_lib->vba.XFCTSlvVupdateWidth;
			locals->XFCSlaveVReadyOffset[k] = mode_lib->vba.XFCTSlvVreadyOffset;
			TWait = CalculateTWait(
					mode_lib->vba.PrefetchMode[mode_lib->vba.VoltageLevel][mode_lib->vba.maxMpcComb],
					mode_lib->vba.DRAMClockChangeLatency,
					mode_lib->vba.UrgentLatency,
					mode_lib->vba.SREnterPlusExitTime);
			mode_lib->vba.XFCRemoteSurfaceFlipDelay = CalculateRemoteSurfaceFlipDelay(
					mode_lib,
					mode_lib->vba.VRatio[k],
					locals->SwathWidthY[k],
					dml_ceil(locals->BytePerPixelDETY[k], 1),
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
					locals->XFCRemoteSurfaceFlipLatency[k] =
					dml_floor(
							mode_lib->vba.XFCRemoteSurfaceFlipDelay
									/ (mode_lib->vba.HTotal[k]
											/ mode_lib->vba.PixelClock[k]),
							1);
			locals->XFCTransferDelay[k] =
					dml_ceil(
							mode_lib->vba.XFCBusTransportTime
									/ (mode_lib->vba.HTotal[k]
											/ mode_lib->vba.PixelClock[k]),
							1);
			locals->XFCPrechargeDelay[k] =
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
					(locals->DestinationLinesToRequestVMInVBlank[k]
							+ locals->DestinationLinesToRequestRowInVBlank[k])
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
			locals->XFCPrefetchMargin[k] =
					mode_lib->vba.XFCRemoteSurfaceFlipDelay
							+ mode_lib->vba.TFinalxFill
							+ (locals->DestinationLinesToRequestVMInVBlank[k]
									+ locals->DestinationLinesToRequestRowInVBlank[k])
									* mode_lib->vba.HTotal[k]
									/ mode_lib->vba.PixelClock[k];
		} else {
			locals->XFCSlaveVUpdateOffset[k] = 0;
			locals->XFCSlaveVupdateWidth[k] = 0;
			locals->XFCSlaveVReadyOffset[k] = 0;
			locals->XFCRemoteSurfaceFlipLatency[k] = 0;
			locals->XFCPrechargeDelay[k] = 0;
			locals->XFCTransferDelay[k] = 0;
			locals->XFCPrefetchMargin[k] = 0;
		}
	}

	// Stutter Efficiency
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		CalculateDETBufferSize(
			mode_lib->vba.DETBufferSizeInKByte,
			mode_lib->vba.SwathHeightY[k],
			mode_lib->vba.SwathHeightC[k],
			&locals->DETBufferSizeY[k],
			&locals->DETBufferSizeC[k]);

		locals->LinesInDETY[k] = locals->DETBufferSizeY[k]
				/ locals->BytePerPixelDETY[k] / locals->SwathWidthY[k];
		locals->LinesInDETYRoundedDownToSwath[k] = dml_floor(
				locals->LinesInDETY[k],
				mode_lib->vba.SwathHeightY[k]);
		locals->FullDETBufferingTimeY[k] =
				locals->LinesInDETYRoundedDownToSwath[k]
						* (mode_lib->vba.HTotal[k]
								/ mode_lib->vba.PixelClock[k])
						/ mode_lib->vba.VRatio[k];
	}

	mode_lib->vba.StutterPeriod = 999999.0;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (locals->FullDETBufferingTimeY[k] < mode_lib->vba.StutterPeriod) {
			mode_lib->vba.StutterPeriod = locals->FullDETBufferingTimeY[k];
			mode_lib->vba.FrameTimeForMinFullDETBufferingTime =
				(double) mode_lib->vba.VTotal[k] * mode_lib->vba.HTotal[k]
				/ mode_lib->vba.PixelClock[k];
			locals->BytePerPixelYCriticalPlane = dml_ceil(locals->BytePerPixelDETY[k], 1);
			locals->SwathWidthYCriticalPlane = locals->SwathWidthY[k];
			locals->LinesToFinishSwathTransferStutterCriticalPlane =
				mode_lib->vba.SwathHeightY[k] - (locals->LinesInDETY[k] - locals->LinesInDETYRoundedDownToSwath[k]);
		}
	}

	mode_lib->vba.AverageReadBandwidth = 0.0;
	mode_lib->vba.TotalRowReadBandwidth = 0.0;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		unsigned int DCCRateLimit;

		if (mode_lib->vba.DCCEnable[k]) {
			if (locals->DCCYMaxCompressedBlock[k] == 256)
				DCCRateLimit = 4;
			else
				DCCRateLimit = 2;

			mode_lib->vba.AverageReadBandwidth =
					mode_lib->vba.AverageReadBandwidth
							+ (locals->ReadBandwidthPlaneLuma[k] + locals->ReadBandwidthPlaneChroma[k]) /
								dml_min(mode_lib->vba.DCCRate[k], DCCRateLimit);
		} else {
			mode_lib->vba.AverageReadBandwidth =
					mode_lib->vba.AverageReadBandwidth
							+ locals->ReadBandwidthPlaneLuma[k]
							+ locals->ReadBandwidthPlaneChroma[k];
		}
		mode_lib->vba.TotalRowReadBandwidth = mode_lib->vba.TotalRowReadBandwidth +
		locals->meta_row_bw[k] + locals->dpte_row_bw[k];
	}

	mode_lib->vba.AverageDCCCompressionRate = mode_lib->vba.TotalDataReadBandwidth / mode_lib->vba.AverageReadBandwidth;

	mode_lib->vba.PartOfBurstThatFitsInROB =
			dml_min(
					mode_lib->vba.StutterPeriod
							* mode_lib->vba.TotalDataReadBandwidth,
					mode_lib->vba.ROBBufferSizeInKByte * 1024
							* mode_lib->vba.AverageDCCCompressionRate);
	mode_lib->vba.StutterBurstTime = mode_lib->vba.PartOfBurstThatFitsInROB
			/ mode_lib->vba.AverageDCCCompressionRate / mode_lib->vba.ReturnBW
			+ (mode_lib->vba.StutterPeriod * mode_lib->vba.TotalDataReadBandwidth
					- mode_lib->vba.PartOfBurstThatFitsInROB)
					/ (mode_lib->vba.DCFCLK * 64)
					+ mode_lib->vba.StutterPeriod * mode_lib->vba.TotalRowReadBandwidth / mode_lib->vba.ReturnBW;
	mode_lib->vba.StutterBurstTime = dml_max(
		mode_lib->vba.StutterBurstTime,
		(locals->LinesToFinishSwathTransferStutterCriticalPlane * locals->BytePerPixelYCriticalPlane *
		locals->SwathWidthYCriticalPlane / mode_lib->vba.ReturnBW)
	);

	mode_lib->vba.TotalActiveWriteback = 0;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.WritebackEnable[k] == true) {
			mode_lib->vba.TotalActiveWriteback = mode_lib->vba.TotalActiveWriteback + 1;
		}
	}

	if (mode_lib->vba.TotalActiveWriteback == 0) {
		mode_lib->vba.StutterEfficiencyNotIncludingVBlank = (1
				- (mode_lib->vba.SRExitTime + mode_lib->vba.StutterBurstTime)
						/ mode_lib->vba.StutterPeriod) * 100;
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
}

static void DisplayPipeConfiguration(struct display_mode_lib *mode_lib)
{
	// Display Pipe Configuration
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

		if (mode_lib->vba.ODMCombineEnabled[k] == dm_odm_combine_mode_2to1) {
			MainPlaneDoesODMCombine = true;
		}
		for (j = 0; j < mode_lib->vba.NumberOfActivePlanes; ++j) {
			if (mode_lib->vba.BlendingAndTiming[k] == j
					&& mode_lib->vba.ODMCombineEnabled[k] == dm_odm_combine_mode_2to1) {
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

		CalculateDETBufferSize(
				mode_lib->vba.DETBufferSizeInKByte,
				mode_lib->vba.SwathHeightY[k],
				mode_lib->vba.SwathHeightC[k],
				&mode_lib->vba.DETBufferSizeY[k],
				&mode_lib->vba.DETBufferSizeC[k]);
	}
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
		bool GPUVMEnable,
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
		double *dpte_row_bw)
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

	if (GPUVMEnable != true) {
		*dpte_row_bw = 0;
	} else if (SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10) {
		*dpte_row_bw = VRatio * PixelPTEBytesPerRowLuma / (dpte_row_height_luma * LineTime)
				+ VRatio / 2 * PixelPTEBytesPerRowChroma
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
		unsigned int HostVMMaxPageTableLevels,
		unsigned int HostVMCachedPageTableLevels,
		bool GPUVMEnable,
		double PDEAndMetaPTEBytesPerFrame,
		double MetaRowBytes,
		double DPTEBytesPerRow,
		double BandwidthAvailableForImmediateFlip,
		unsigned int TotImmediateFlipBytes,
		enum source_format_class SourcePixelFormat,
		double LineTime,
		double VRatio,
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
	unsigned int HostVMDynamicLevels;
	double TimeForFetchingMetaPTEImmediateFlip;
	double TimeForFetchingRowInVBlankImmediateFlip;
	double ImmediateFlipBW;
	double HostVMInefficiencyFactor;
	double VRatioClamped;

	if (GPUVMEnable == true && HostVMEnable == true) {
		HostVMInefficiencyFactor =
				PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData
						/ PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly;
		HostVMDynamicLevels = HostVMMaxPageTableLevels - HostVMCachedPageTableLevels;
	} else {
		HostVMInefficiencyFactor = 1;
		HostVMDynamicLevels = 0;
	}

	ImmediateFlipBW = (PDEAndMetaPTEBytesPerFrame + MetaRowBytes + DPTEBytesPerRow)
			* BandwidthAvailableForImmediateFlip / TotImmediateFlipBytes;

	if (GPUVMEnable == true) {
		TimeForFetchingMetaPTEImmediateFlip = dml_max3(
			Tno_bw + PDEAndMetaPTEBytesPerFrame * HostVMInefficiencyFactor / ImmediateFlipBW,
			UrgentExtraLatency + UrgentLatency * (GPUVMMaxPageTableLevels * (HostVMDynamicLevels + 1) - 1),
			LineTime / 4.0);
	} else {
		TimeForFetchingMetaPTEImmediateFlip = 0;
	}

	*DestinationLinesToRequestVMInImmediateFlip = dml_ceil(4.0 * (TimeForFetchingMetaPTEImmediateFlip / LineTime), 1) / 4.0;
	if ((GPUVMEnable == true || DCCEnable == true)) {
		TimeForFetchingRowInVBlankImmediateFlip = dml_max3((MetaRowBytes + DPTEBytesPerRow) * HostVMInefficiencyFactor / ImmediateFlipBW, UrgentLatency * (HostVMDynamicLevels + 1), LineTime / 4);
	} else {
		TimeForFetchingRowInVBlankImmediateFlip = 0;
	}

	*DestinationLinesToRequestRowInImmediateFlip = dml_ceil(4.0 * (TimeForFetchingRowInVBlankImmediateFlip / LineTime), 1) / 4.0;
	*final_flip_bw = dml_max(PDEAndMetaPTEBytesPerFrame * HostVMInefficiencyFactor / (*DestinationLinesToRequestVMInImmediateFlip * LineTime), (MetaRowBytes + DPTEBytesPerRow) * HostVMInefficiencyFactor / (*DestinationLinesToRequestRowInImmediateFlip * LineTime));
	VRatioClamped = (VRatio < 1.0) ? 1.0 : VRatio;
	if (SourcePixelFormat == dm_420_8 || SourcePixelFormat == dm_420_10) {
		if (GPUVMEnable == true && DCCEnable != true) {
			min_row_time = dml_min(
					dpte_row_height * LineTime / VRatioClamped,
					dpte_row_height_chroma * LineTime / (VRatioClamped / 2));
		} else if (GPUVMEnable != true && DCCEnable == true) {
			min_row_time = dml_min(
					meta_row_height * LineTime / VRatioClamped,
					meta_row_height_chroma * LineTime / (VRatioClamped / 2));
		} else {
			min_row_time = dml_min4(
					dpte_row_height * LineTime / VRatioClamped,
					meta_row_height * LineTime / VRatioClamped,
					dpte_row_height_chroma * LineTime / (VRatioClamped / 2),
					meta_row_height_chroma * LineTime / (VRatioClamped / 2));
		}
	} else {
		if (GPUVMEnable == true && DCCEnable != true) {
			min_row_time = dpte_row_height * LineTime / VRatioClamped;
		} else if (GPUVMEnable != true && DCCEnable == true) {
			min_row_time = meta_row_height * LineTime / VRatioClamped;
		} else {
			min_row_time = dml_min(
					dpte_row_height * LineTime / VRatioClamped,
					meta_row_height * LineTime / VRatioClamped);
		}
	}

	if (*DestinationLinesToRequestVMInImmediateFlip >= 32
			|| *DestinationLinesToRequestRowInImmediateFlip >= 16
			|| TimeForFetchingMetaPTEImmediateFlip + 2 * TimeForFetchingRowInVBlankImmediateFlip > min_row_time) {
		*ImmediateFlipSupportedForPipe = false;
	} else {
		*ImmediateFlipSupportedForPipe = true;
	}
}

static unsigned int TruncToValidBPP(
		double DecimalBPP,
		double DesiredBPP,
		bool DSCEnabled,
		enum output_encoder_class Output,
		enum output_format_class Format,
		unsigned int DSCInputBitPerComponent)
{
	if (Output == dm_hdmi) {
		if (Format == dm_420) {
			if (DecimalBPP >= 18 && (DesiredBPP == 0 || DesiredBPP == 18))
				return 18;
			else if (DecimalBPP >= 15 && (DesiredBPP == 0 || DesiredBPP == 15))
				return 15;
			else if (DecimalBPP >= 12 && (DesiredBPP == 0 || DesiredBPP == 12))
				return 12;
			else
				return BPP_INVALID;
		} else if (Format == dm_444) {
			if (DecimalBPP >= 36 && (DesiredBPP == 0 || DesiredBPP == 36))
				return 36;
			else if (DecimalBPP >= 30 && (DesiredBPP == 0 || DesiredBPP == 30))
				return 30;
			else if (DecimalBPP >= 24 && (DesiredBPP == 0 || DesiredBPP == 24))
				return 24;
			else if (DecimalBPP >= 18 && (DesiredBPP == 0 || DesiredBPP == 18))
				return 18;
			else
				return BPP_INVALID;
		} else {
			if (DecimalBPP / 1.5 >= 24 && (DesiredBPP == 0 || DesiredBPP == 24))
				return 24;
			else if (DecimalBPP / 1.5 >= 20 && (DesiredBPP == 0 || DesiredBPP == 20))
				return 20;
			else if (DecimalBPP / 1.5 >= 16 && (DesiredBPP == 0 || DesiredBPP == 16))
				return 16;
			else
				return BPP_INVALID;
		}
	} else {
		if (DSCEnabled) {
			if (Format == dm_420) {
				if (DesiredBPP == 0) {
					if (DecimalBPP < 6)
						return BPP_INVALID;
					else if (DecimalBPP >= 1.5 * DSCInputBitPerComponent - 1.0 / 16.0)
						return 1.5 * DSCInputBitPerComponent - 1.0 / 16.0;
					else
						return dml_floor(16 * DecimalBPP, 1) / 16.0;
				} else {
					if (DecimalBPP < 6
							|| DesiredBPP < 6
							|| DesiredBPP > 1.5 * DSCInputBitPerComponent - 1.0 / 16.0
							|| DecimalBPP < DesiredBPP) {
						return BPP_INVALID;
					} else {
						return DesiredBPP;
					}
				}
			} else if (Format == dm_n422) {
				if (DesiredBPP == 0) {
					if (DecimalBPP < 7)
						return BPP_INVALID;
					else if (DecimalBPP >= 2 * DSCInputBitPerComponent - 1.0 / 16.0)
						return 2 * DSCInputBitPerComponent - 1.0 / 16.0;
					else
						return dml_floor(16 * DecimalBPP, 1) / 16.0;
				} else {
					if (DecimalBPP < 7
							|| DesiredBPP < 7
							|| DesiredBPP > 2 * DSCInputBitPerComponent - 1.0 / 16.0
							|| DecimalBPP < DesiredBPP) {
						return BPP_INVALID;
					} else {
						return DesiredBPP;
					}
				}
			} else {
				if (DesiredBPP == 0) {
					if (DecimalBPP < 8)
						return BPP_INVALID;
					else if (DecimalBPP >= 3 * DSCInputBitPerComponent - 1.0 / 16.0)
						return 3 * DSCInputBitPerComponent - 1.0 / 16.0;
					else
						return dml_floor(16 * DecimalBPP, 1) / 16.0;
				} else {
					if (DecimalBPP < 8
							|| DesiredBPP < 8
							|| DesiredBPP > 3 * DSCInputBitPerComponent - 1.0 / 16.0
							|| DecimalBPP < DesiredBPP) {
						return BPP_INVALID;
					} else {
						return DesiredBPP;
					}
				}
			}
		} else if (Format == dm_420) {
			if (DecimalBPP >= 18 && (DesiredBPP == 0 || DesiredBPP == 18))
				return 18;
			else if (DecimalBPP >= 15 && (DesiredBPP == 0 || DesiredBPP == 15))
				return 15;
			else if (DecimalBPP >= 12 && (DesiredBPP == 0 || DesiredBPP == 12))
				return 12;
			else
				return BPP_INVALID;
		} else if (Format == dm_s422 || Format == dm_n422) {
			if (DecimalBPP >= 24 && (DesiredBPP == 0 || DesiredBPP == 24))
				return 24;
			else if (DecimalBPP >= 20 && (DesiredBPP == 0 || DesiredBPP == 20))
				return 20;
			else if (DecimalBPP >= 16 && (DesiredBPP == 0 || DesiredBPP == 16))
				return 16;
			else
				return BPP_INVALID;
		} else {
			if (DecimalBPP >= 36 && (DesiredBPP == 0 || DesiredBPP == 36))
				return 36;
			else if (DecimalBPP >= 30 && (DesiredBPP == 0 || DesiredBPP == 30))
				return 30;
			else if (DecimalBPP >= 24 && (DesiredBPP == 0 || DesiredBPP == 24))
				return 24;
			else if (DecimalBPP >= 18 && (DesiredBPP == 0 || DesiredBPP == 18))
				return 18;
			else
				return BPP_INVALID;
		}
	}
}

void dml21_ModeSupportAndSystemConfigurationFull(struct display_mode_lib *mode_lib)
{
	struct vba_vars_st *locals = &mode_lib->vba;

	int i;
	unsigned int j, k, m;

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
				|| (((mode_lib->vba.SurfaceTiling[k] == dm_sw_gfx7_2d_thin_gl
						|| mode_lib->vba.SurfaceTiling[k]
								== dm_sw_gfx7_2d_thin_l_vp)
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
		if (mode_lib->vba.SourcePixelFormat[k] == dm_444_64) {
			locals->BytePerPixelInDETY[k] = 8.0;
			locals->BytePerPixelInDETC[k] = 0.0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_444_32) {
			locals->BytePerPixelInDETY[k] = 4.0;
			locals->BytePerPixelInDETC[k] = 0.0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_444_16
				|| mode_lib->vba.SourcePixelFormat[k] == dm_mono_16) {
			locals->BytePerPixelInDETY[k] = 2.0;
			locals->BytePerPixelInDETC[k] = 0.0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_mono_8) {
			locals->BytePerPixelInDETY[k] = 1.0;
			locals->BytePerPixelInDETC[k] = 0.0;
		} else if (mode_lib->vba.SourcePixelFormat[k] == dm_420_8) {
			locals->BytePerPixelInDETY[k] = 1.0;
			locals->BytePerPixelInDETC[k] = 2.0;
		} else {
			locals->BytePerPixelInDETY[k] = 4.0 / 3;
			locals->BytePerPixelInDETC[k] = 8.0 / 3;
		}
		if (mode_lib->vba.SourceScan[k] == dm_horz) {
			locals->SwathWidthYSingleDPP[k] = mode_lib->vba.ViewportWidth[k];
		} else {
			locals->SwathWidthYSingleDPP[k] = mode_lib->vba.ViewportHeight[k];
		}
	}
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		locals->ReadBandwidthLuma[k] = locals->SwathWidthYSingleDPP[k] * dml_ceil(locals->BytePerPixelInDETY[k], 1.0)
				/ (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k]) * mode_lib->vba.VRatio[k];
		locals->ReadBandwidthChroma[k] = locals->SwathWidthYSingleDPP[k] / 2 * dml_ceil(locals->BytePerPixelInDETC[k], 2.0)
				/ (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k]) * mode_lib->vba.VRatio[k] / 2.0;
		locals->ReadBandwidth[k] = locals->ReadBandwidthLuma[k] + locals->ReadBandwidthChroma[k];
	}
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.WritebackEnable[k] == true
				&& mode_lib->vba.WritebackPixelFormat[k] == dm_444_32) {
			locals->WriteBandwidth[k] = mode_lib->vba.WritebackDestinationWidth[k]
					* mode_lib->vba.WritebackDestinationHeight[k]
					/ (mode_lib->vba.WritebackSourceHeight[k]
							* mode_lib->vba.HTotal[k]
							/ mode_lib->vba.PixelClock[k]) * 4.0;
		} else if (mode_lib->vba.WritebackEnable[k] == true
				&& mode_lib->vba.WritebackPixelFormat[k] == dm_420_10) {
			locals->WriteBandwidth[k] = mode_lib->vba.WritebackDestinationWidth[k]
					* mode_lib->vba.WritebackDestinationHeight[k]
					/ (mode_lib->vba.WritebackSourceHeight[k]
							* mode_lib->vba.HTotal[k]
							/ mode_lib->vba.PixelClock[k]) * 3.0;
		} else if (mode_lib->vba.WritebackEnable[k] == true) {
			locals->WriteBandwidth[k] = mode_lib->vba.WritebackDestinationWidth[k]
					* mode_lib->vba.WritebackDestinationHeight[k]
					/ (mode_lib->vba.WritebackSourceHeight[k]
							* mode_lib->vba.HTotal[k]
							/ mode_lib->vba.PixelClock[k]) * 1.5;
		} else {
			locals->WriteBandwidth[k] = 0.0;
		}
	}
	mode_lib->vba.DCCEnabledInAnyPlane = false;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.DCCEnable[k] == true) {
			mode_lib->vba.DCCEnabledInAnyPlane = true;
		}
	}
	for (i = 0; i <= mode_lib->vba.soc.num_states; i++) {
		locals->IdealSDPPortBandwidthPerState[i][0] = dml_min3(
				mode_lib->vba.ReturnBusWidth * mode_lib->vba.DCFCLKPerState[i],
				mode_lib->vba.DRAMSpeedPerState[i] * mode_lib->vba.NumberOfChannels
						* mode_lib->vba.DRAMChannelWidth,
				mode_lib->vba.FabricClockPerState[i]
						* mode_lib->vba.FabricDatapathToDCNDataReturn);
		if (mode_lib->vba.HostVMEnable == false) {
			locals->ReturnBWPerState[i][0] = locals->IdealSDPPortBandwidthPerState[i][0]
					* mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelDataOnly / 100.0;
		} else {
			locals->ReturnBWPerState[i][0] = locals->IdealSDPPortBandwidthPerState[i][0]
					* mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData / 100.0;
		}
	}
	/*Writeback Latency support check*/

	mode_lib->vba.WritebackLatencySupport = true;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.WritebackEnable[k] == true) {
			if (mode_lib->vba.WritebackPixelFormat[k] == dm_444_32) {
				if (locals->WriteBandwidth[k]
						> (mode_lib->vba.WritebackInterfaceLumaBufferSize
								+ mode_lib->vba.WritebackInterfaceChromaBufferSize)
								/ mode_lib->vba.WritebackLatency) {
					mode_lib->vba.WritebackLatencySupport = false;
				}
			} else {
				if (locals->WriteBandwidth[k]
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

	for (i = 0; i <= mode_lib->vba.soc.num_states; i++) {
		locals->UrgentRoundTripAndOutOfOrderLatencyPerState[i] =
				(mode_lib->vba.RoundTripPingLatencyCycles + 32.0) / mode_lib->vba.DCFCLKPerState[i]
				+ dml_max3(mode_lib->vba.UrgentOutOfOrderReturnPerChannelPixelDataOnly,
						mode_lib->vba.UrgentOutOfOrderReturnPerChannelPixelMixedWithVMData,
						mode_lib->vba.UrgentOutOfOrderReturnPerChannelVMDataOnly)
					* mode_lib->vba.NumberOfChannels / locals->ReturnBWPerState[i][0];
		if ((mode_lib->vba.ROBBufferSizeInKByte - mode_lib->vba.PixelChunkSizeInKByte) * 1024.0 / locals->ReturnBWPerState[i][0]
				> locals->UrgentRoundTripAndOutOfOrderLatencyPerState[i]) {
			locals->ROBSupport[i][0] = true;
		} else {
			locals->ROBSupport[i][0] = false;
		}
	}
	/*Writeback Mode Support Check*/

	mode_lib->vba.TotalNumberOfActiveWriteback = 0;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.WritebackEnable[k] == true) {
			if (mode_lib->vba.ActiveWritebacksPerPlane[k] == 0)
				mode_lib->vba.ActiveWritebacksPerPlane[k] = 1;
			mode_lib->vba.TotalNumberOfActiveWriteback =
					mode_lib->vba.TotalNumberOfActiveWriteback
							+ mode_lib->vba.ActiveWritebacksPerPlane[k];
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
											* 8.0 / 10.0 / mode_lib->vba.WritebackDestinationWidth[k]
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
									* 8.0 / 10.0 / mode_lib->vba.WritebackDestinationWidth[k]
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
			locals->PSCL_FACTOR[k] = dml_min(
					mode_lib->vba.MaxDCHUBToPSCLThroughput,
					mode_lib->vba.MaxPSCLToLBThroughput
							* mode_lib->vba.HRatio[k]
							/ dml_ceil(
									mode_lib->vba.htaps[k]
											/ 6.0,
									1.0));
		} else {
			locals->PSCL_FACTOR[k] = dml_min(
					mode_lib->vba.MaxDCHUBToPSCLThroughput,
					mode_lib->vba.MaxPSCLToLBThroughput);
		}
		if (locals->BytePerPixelInDETC[k] == 0.0) {
			locals->PSCL_FACTOR_CHROMA[k] = 0.0;
			locals->MinDPPCLKUsingSingleDPP[k] =
					mode_lib->vba.PixelClock[k]
							* dml_max3(
									mode_lib->vba.vtaps[k] / 6.0
											* dml_min(
													1.0,
													mode_lib->vba.HRatio[k]),
									mode_lib->vba.HRatio[k]
											* mode_lib->vba.VRatio[k]
											/ locals->PSCL_FACTOR[k],
									1.0);
			if ((mode_lib->vba.htaps[k] > 6.0 || mode_lib->vba.vtaps[k] > 6.0)
					&& locals->MinDPPCLKUsingSingleDPP[k]
							< 2.0 * mode_lib->vba.PixelClock[k]) {
				locals->MinDPPCLKUsingSingleDPP[k] = 2.0
						* mode_lib->vba.PixelClock[k];
			}
		} else {
			if (mode_lib->vba.HRatio[k] / 2.0 > 1.0) {
				locals->PSCL_FACTOR_CHROMA[k] =
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
				locals->PSCL_FACTOR_CHROMA[k] = dml_min(
						mode_lib->vba.MaxDCHUBToPSCLThroughput,
						mode_lib->vba.MaxPSCLToLBThroughput);
			}
			locals->MinDPPCLKUsingSingleDPP[k] =
					mode_lib->vba.PixelClock[k]
							* dml_max5(
									mode_lib->vba.vtaps[k] / 6.0
											* dml_min(
													1.0,
													mode_lib->vba.HRatio[k]),
									mode_lib->vba.HRatio[k]
											* mode_lib->vba.VRatio[k]
											/ locals->PSCL_FACTOR[k],
									mode_lib->vba.VTAPsChroma[k]
											/ 6.0
											* dml_min(
													1.0,
													mode_lib->vba.HRatio[k]
															/ 2.0),
									mode_lib->vba.HRatio[k]
											* mode_lib->vba.VRatio[k]
											/ 4.0
											/ locals->PSCL_FACTOR_CHROMA[k],
									1.0);
			if ((mode_lib->vba.htaps[k] > 6.0 || mode_lib->vba.vtaps[k] > 6.0
					|| mode_lib->vba.HTAPsChroma[k] > 6.0
					|| mode_lib->vba.VTAPsChroma[k] > 6.0)
					&& locals->MinDPPCLKUsingSingleDPP[k]
							< 2.0 * mode_lib->vba.PixelClock[k]) {
				locals->MinDPPCLKUsingSingleDPP[k] = 2.0
						* mode_lib->vba.PixelClock[k];
			}
		}
	}
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		Calculate256BBlockSizes(
				mode_lib->vba.SourcePixelFormat[k],
				mode_lib->vba.SurfaceTiling[k],
				dml_ceil(locals->BytePerPixelInDETY[k], 1.0),
				dml_ceil(locals->BytePerPixelInDETC[k], 2.0),
				&locals->Read256BlockHeightY[k],
				&locals->Read256BlockHeightC[k],
				&locals->Read256BlockWidthY[k],
				&locals->Read256BlockWidthC[k]);
		if (mode_lib->vba.SourceScan[k] == dm_horz) {
			locals->MaxSwathHeightY[k] = locals->Read256BlockHeightY[k];
			locals->MaxSwathHeightC[k] = locals->Read256BlockHeightC[k];
		} else {
			locals->MaxSwathHeightY[k] = locals->Read256BlockWidthY[k];
			locals->MaxSwathHeightC[k] = locals->Read256BlockWidthC[k];
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
				locals->MinSwathHeightY[k] = locals->MaxSwathHeightY[k];
			} else {
				locals->MinSwathHeightY[k] = locals->MaxSwathHeightY[k]
						/ 2.0;
			}
			locals->MinSwathHeightC[k] = locals->MaxSwathHeightC[k];
		} else {
			if (mode_lib->vba.SurfaceTiling[k] == dm_sw_linear) {
				locals->MinSwathHeightY[k] = locals->MaxSwathHeightY[k];
				locals->MinSwathHeightC[k] = locals->MaxSwathHeightC[k];
			} else if (mode_lib->vba.SourcePixelFormat[k] == dm_420_8
					&& mode_lib->vba.SourceScan[k] == dm_horz) {
				locals->MinSwathHeightY[k] = locals->MaxSwathHeightY[k]
						/ 2.0;
				locals->MinSwathHeightC[k] = locals->MaxSwathHeightC[k];
			} else if (mode_lib->vba.SourcePixelFormat[k] == dm_420_10
					&& mode_lib->vba.SourceScan[k] == dm_horz) {
				locals->MinSwathHeightC[k] = locals->MaxSwathHeightC[k]
						/ 2.0;
				locals->MinSwathHeightY[k] = locals->MaxSwathHeightY[k];
			} else {
				locals->MinSwathHeightY[k] = locals->MaxSwathHeightY[k];
				locals->MinSwathHeightC[k] = locals->MaxSwathHeightC[k];
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
								/ (locals->BytePerPixelInDETY[k]
										* locals->MinSwathHeightY[k]
										+ locals->BytePerPixelInDETC[k]
												/ 2.0
												* locals->MinSwathHeightC[k]));
		if (locals->BytePerPixelInDETC[k] == 0.0) {
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
		locals->MaximumSwathWidth[k] = dml_min(
				mode_lib->vba.MaximumSwathWidthInDETBuffer,
				mode_lib->vba.MaximumSwathWidthInLineBuffer);
	}
	for (i = 0; i <= mode_lib->vba.soc.num_states; i++) {
		double MaxMaxDispclkRoundedDown = RoundToDFSGranularityDown(
			mode_lib->vba.MaxDispclk[mode_lib->vba.soc.num_states],
			mode_lib->vba.DISPCLKDPPCLKVCOSpeed);

		for (j = 0; j < 2; j++) {
			mode_lib->vba.MaxDispclkRoundedDownToDFSGranularity = RoundToDFSGranularityDown(
				mode_lib->vba.MaxDispclk[i],
				mode_lib->vba.DISPCLKDPPCLKVCOSpeed);
			mode_lib->vba.MaxDppclkRoundedDownToDFSGranularity = RoundToDFSGranularityDown(
				mode_lib->vba.MaxDppclk[i],
				mode_lib->vba.DISPCLKDPPCLKVCOSpeed);
			locals->RequiredDISPCLK[i][j] = 0.0;
			locals->DISPCLK_DPPCLK_Support[i][j] = true;
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				mode_lib->vba.PlaneRequiredDISPCLKWithoutODMCombine =
						mode_lib->vba.PixelClock[k]
								* (1.0
										+ mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading
												/ 100.0)
								* (1.0
										+ mode_lib->vba.DISPCLKRampingMargin
												/ 100.0);
				if (mode_lib->vba.PlaneRequiredDISPCLKWithoutODMCombine >= mode_lib->vba.MaxDispclk[i]
						&& i == mode_lib->vba.soc.num_states)
					mode_lib->vba.PlaneRequiredDISPCLKWithoutODMCombine = mode_lib->vba.PixelClock[k]
							* (1 + mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);

				mode_lib->vba.PlaneRequiredDISPCLKWithODMCombine = mode_lib->vba.PixelClock[k] / 2
					* (1 + mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100.0) * (1 + mode_lib->vba.DISPCLKRampingMargin / 100.0);
				if (mode_lib->vba.PlaneRequiredDISPCLKWithODMCombine >= mode_lib->vba.MaxDispclk[i]
						&& i == mode_lib->vba.soc.num_states)
					mode_lib->vba.PlaneRequiredDISPCLKWithODMCombine = mode_lib->vba.PixelClock[k] / 2
							* (1 + mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);

				locals->ODMCombineEnablePerState[i][k] = false;
				mode_lib->vba.PlaneRequiredDISPCLK = mode_lib->vba.PlaneRequiredDISPCLKWithoutODMCombine;
				if (mode_lib->vba.ODMCapability) {
					if (locals->PlaneRequiredDISPCLKWithoutODMCombine > MaxMaxDispclkRoundedDown) {
						locals->ODMCombineEnablePerState[i][k] = true;
						mode_lib->vba.PlaneRequiredDISPCLK = mode_lib->vba.PlaneRequiredDISPCLKWithODMCombine;
					} else if (locals->DSCEnabled[k] && (locals->HActive[k] > DCN21_MAX_DSC_IMAGE_WIDTH)) {
						locals->ODMCombineEnablePerState[i][k] = true;
						mode_lib->vba.PlaneRequiredDISPCLK = mode_lib->vba.PlaneRequiredDISPCLKWithODMCombine;
					} else if (locals->HActive[k] > DCN21_MAX_420_IMAGE_WIDTH && locals->OutputFormat[k] == dm_420) {
						locals->ODMCombineEnablePerState[i][k] = true;
						mode_lib->vba.PlaneRequiredDISPCLK = mode_lib->vba.PlaneRequiredDISPCLKWithODMCombine;
					}
				}

				if (locals->MinDPPCLKUsingSingleDPP[k] * (1.0 + mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100.0) <= mode_lib->vba.MaxDppclkRoundedDownToDFSGranularity
						&& locals->SwathWidthYSingleDPP[k] <= locals->MaximumSwathWidth[k]
						&& locals->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_disabled) {
					locals->NoOfDPP[i][j][k] = 1;
					locals->RequiredDPPCLK[i][j][k] =
						locals->MinDPPCLKUsingSingleDPP[k] * (1.0 + mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);
				} else {
					locals->NoOfDPP[i][j][k] = 2;
					locals->RequiredDPPCLK[i][j][k] =
						locals->MinDPPCLKUsingSingleDPP[k] * (1.0 + mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100.0) / 2.0;
				}
				locals->RequiredDISPCLK[i][j] = dml_max(
						locals->RequiredDISPCLK[i][j],
						mode_lib->vba.PlaneRequiredDISPCLK);
				if ((locals->MinDPPCLKUsingSingleDPP[k] / locals->NoOfDPP[i][j][k] * (1.0 + mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100.0)
						> mode_lib->vba.MaxDppclkRoundedDownToDFSGranularity)
						|| (mode_lib->vba.PlaneRequiredDISPCLK > mode_lib->vba.MaxDispclkRoundedDownToDFSGranularity)) {
					locals->DISPCLK_DPPCLK_Support[i][j] = false;
				}
			}
			locals->TotalNumberOfActiveDPP[i][j] = 0.0;
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++)
				locals->TotalNumberOfActiveDPP[i][j] = locals->TotalNumberOfActiveDPP[i][j] + locals->NoOfDPP[i][j][k];
			if (j == 1) {
				while (locals->TotalNumberOfActiveDPP[i][j] < mode_lib->vba.MaxNumDPP
						&& locals->TotalNumberOfActiveDPP[i][j] < 2 * mode_lib->vba.NumberOfActivePlanes) {
					double BWOfNonSplitPlaneOfMaximumBandwidth;
					unsigned int NumberOfNonSplitPlaneOfMaximumBandwidth;

					BWOfNonSplitPlaneOfMaximumBandwidth = 0;
					NumberOfNonSplitPlaneOfMaximumBandwidth = 0;
					for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
						if (locals->ReadBandwidth[k] > BWOfNonSplitPlaneOfMaximumBandwidth && locals->NoOfDPP[i][j][k] == 1) {
							BWOfNonSplitPlaneOfMaximumBandwidth = locals->ReadBandwidth[k];
							NumberOfNonSplitPlaneOfMaximumBandwidth = k;
						}
					}
					locals->NoOfDPP[i][j][NumberOfNonSplitPlaneOfMaximumBandwidth] = 2;
					locals->RequiredDPPCLK[i][j][NumberOfNonSplitPlaneOfMaximumBandwidth] =
						locals->MinDPPCLKUsingSingleDPP[NumberOfNonSplitPlaneOfMaximumBandwidth]
							* (1 + mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100) / 2;
					locals->TotalNumberOfActiveDPP[i][j] = locals->TotalNumberOfActiveDPP[i][j] + 1;
				}
			}
			if (locals->TotalNumberOfActiveDPP[i][j] > mode_lib->vba.MaxNumDPP) {
				locals->RequiredDISPCLK[i][j] = 0.0;
				locals->DISPCLK_DPPCLK_Support[i][j] = true;
				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
					locals->ODMCombineEnablePerState[i][k] = false;
					if (locals->SwathWidthYSingleDPP[k] <= locals->MaximumSwathWidth[k]) {
						locals->NoOfDPP[i][j][k] = 1;
						locals->RequiredDPPCLK[i][j][k] = locals->MinDPPCLKUsingSingleDPP[k]
							* (1.0 + mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);
					} else {
						locals->NoOfDPP[i][j][k] = 2;
						locals->RequiredDPPCLK[i][j][k] = locals->MinDPPCLKUsingSingleDPP[k]
										* (1.0 + mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100.0) / 2.0;
					}
					if (i != mode_lib->vba.soc.num_states) {
						mode_lib->vba.PlaneRequiredDISPCLK =
								mode_lib->vba.PixelClock[k]
										* (1.0 + mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100.0)
										* (1.0 + mode_lib->vba.DISPCLKRampingMargin / 100.0);
					} else {
						mode_lib->vba.PlaneRequiredDISPCLK = mode_lib->vba.PixelClock[k]
							* (1.0 + mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);
					}
					locals->RequiredDISPCLK[i][j] = dml_max(
							locals->RequiredDISPCLK[i][j],
							mode_lib->vba.PlaneRequiredDISPCLK);
					if (locals->MinDPPCLKUsingSingleDPP[k] / locals->NoOfDPP[i][j][k] * (1.0 + mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100.0)
							> mode_lib->vba.MaxDppclkRoundedDownToDFSGranularity
							|| mode_lib->vba.PlaneRequiredDISPCLK > mode_lib->vba.MaxDispclkRoundedDownToDFSGranularity)
						locals->DISPCLK_DPPCLK_Support[i][j] = false;
				}
				locals->TotalNumberOfActiveDPP[i][j] = 0.0;
				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++)
					locals->TotalNumberOfActiveDPP[i][j] = locals->TotalNumberOfActiveDPP[i][j] + locals->NoOfDPP[i][j][k];
			}
			locals->RequiredDISPCLK[i][j] = dml_max(
					locals->RequiredDISPCLK[i][j],
					mode_lib->vba.WritebackRequiredDISPCLK);
			if (mode_lib->vba.MaxDispclkRoundedDownToDFSGranularity
					< mode_lib->vba.WritebackRequiredDISPCLK) {
				locals->DISPCLK_DPPCLK_Support[i][j] = false;
			}
		}
	}
	/*Viewport Size Check*/

	for (i = 0; i <= mode_lib->vba.soc.num_states; i++) {
		locals->ViewportSizeSupport[i][0] = true;
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			if (locals->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_2to1) {
				if (dml_min(locals->SwathWidthYSingleDPP[k], dml_round(mode_lib->vba.HActive[k] / 2.0 * mode_lib->vba.HRatio[k]))
						> locals->MaximumSwathWidth[k]) {
					locals->ViewportSizeSupport[i][0] = false;
				}
			} else {
				if (locals->SwathWidthYSingleDPP[k] / 2.0 > locals->MaximumSwathWidth[k]) {
					locals->ViewportSizeSupport[i][0] = false;
				}
			}
		}
	}
	/*Total Available Pipes Support Check*/

	for (i = 0; i <= mode_lib->vba.soc.num_states; i++) {
		for (j = 0; j < 2; j++) {
			if (locals->TotalNumberOfActiveDPP[i][j] <= mode_lib->vba.MaxNumDPP)
				locals->TotalAvailablePipesSupport[i][j] = true;
			else
				locals->TotalAvailablePipesSupport[i][j] = false;
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
	for (i = 0; i <= mode_lib->vba.soc.num_states; i++) {
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			locals->RequiresDSC[i][k] = false;
			locals->RequiresFEC[i][k] = 0;
			if (mode_lib->vba.BlendingAndTiming[k] == k) {
				if (mode_lib->vba.Output[k] == dm_hdmi) {
					locals->RequiresDSC[i][k] = false;
					locals->RequiresFEC[i][k] = 0;
					locals->OutputBppPerState[i][k] = TruncToValidBPP(
							dml_min(600.0, mode_lib->vba.PHYCLKPerState[i]) / mode_lib->vba.PixelClockBackEnd[k] * 24,
							mode_lib->vba.ForcedOutputLinkBPP[k],
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
						mode_lib->vba.Outbpp = TruncToValidBPP(
								(1.0 - mode_lib->vba.Downspreading / 100.0) * 270.0
								* mode_lib->vba.OutputLinkDPLanes[k] / mode_lib->vba.PixelClockBackEnd[k] * 8.0,
								mode_lib->vba.ForcedOutputLinkBPP[k],
								false,
								mode_lib->vba.Output[k],
								mode_lib->vba.OutputFormat[k],
								mode_lib->vba.DSCInputBitPerComponent[k]);
						mode_lib->vba.OutbppDSC = TruncToValidBPP(
								(1.0 - mode_lib->vba.Downspreading / 100.0) * (1.0 - mode_lib->vba.EffectiveFECOverhead / 100.0) * 270.0
								* mode_lib->vba.OutputLinkDPLanes[k] / mode_lib->vba.PixelClockBackEnd[k] * 8.0,
								mode_lib->vba.ForcedOutputLinkBPP[k],
								true,
								mode_lib->vba.Output[k],
								mode_lib->vba.OutputFormat[k],
								mode_lib->vba.DSCInputBitPerComponent[k]);
						if (mode_lib->vba.DSCEnabled[k] == true) {
							locals->RequiresDSC[i][k] = true;
							if (mode_lib->vba.Output[k] == dm_dp) {
								locals->RequiresFEC[i][k] = true;
							} else {
								locals->RequiresFEC[i][k] = false;
							}
							mode_lib->vba.Outbpp = mode_lib->vba.OutbppDSC;
						} else {
							locals->RequiresDSC[i][k] = false;
							locals->RequiresFEC[i][k] = false;
						}
						locals->OutputBppPerState[i][k] = mode_lib->vba.Outbpp;
					}
					if (mode_lib->vba.Outbpp == BPP_INVALID && mode_lib->vba.PHYCLKPerState[i] >= 540.0) {
						mode_lib->vba.Outbpp = TruncToValidBPP(
								(1.0 - mode_lib->vba.Downspreading / 100.0) * 540.0
								* mode_lib->vba.OutputLinkDPLanes[k] / mode_lib->vba.PixelClockBackEnd[k] * 8.0,
								mode_lib->vba.ForcedOutputLinkBPP[k],
									false,
									mode_lib->vba.Output[k],
									mode_lib->vba.OutputFormat[k],
									mode_lib->vba.DSCInputBitPerComponent[k]);
						mode_lib->vba.OutbppDSC = TruncToValidBPP(
								(1.0 - mode_lib->vba.Downspreading / 100.0) * (1.0 - mode_lib->vba.EffectiveFECOverhead / 100.0) * 540.0
								* mode_lib->vba.OutputLinkDPLanes[k] / mode_lib->vba.PixelClockBackEnd[k] * 8.0,
								mode_lib->vba.ForcedOutputLinkBPP[k],
								true,
								mode_lib->vba.Output[k],
								mode_lib->vba.OutputFormat[k],
								mode_lib->vba.DSCInputBitPerComponent[k]);
						if (mode_lib->vba.DSCEnabled[k] == true) {
							locals->RequiresDSC[i][k] = true;
							if (mode_lib->vba.Output[k] == dm_dp) {
								locals->RequiresFEC[i][k] = true;
							} else {
								locals->RequiresFEC[i][k] = false;
							}
							mode_lib->vba.Outbpp = mode_lib->vba.OutbppDSC;
						} else {
							locals->RequiresDSC[i][k] = false;
							locals->RequiresFEC[i][k] = false;
						}
						locals->OutputBppPerState[i][k] = mode_lib->vba.Outbpp;
					}
					if (mode_lib->vba.Outbpp == BPP_INVALID
							&& mode_lib->vba.PHYCLKPerState[i]
									>= 810.0) {
						mode_lib->vba.Outbpp = TruncToValidBPP(
								(1.0 - mode_lib->vba.Downspreading / 100.0) * 810.0
								* mode_lib->vba.OutputLinkDPLanes[k] / mode_lib->vba.PixelClockBackEnd[k] * 8.0,
								mode_lib->vba.ForcedOutputLinkBPP[k],
								false,
								mode_lib->vba.Output[k],
								mode_lib->vba.OutputFormat[k],
								mode_lib->vba.DSCInputBitPerComponent[k]);
						mode_lib->vba.OutbppDSC = TruncToValidBPP(
								(1.0 - mode_lib->vba.Downspreading / 100.0) * (1.0 - mode_lib->vba.EffectiveFECOverhead / 100.0) * 810.0
								* mode_lib->vba.OutputLinkDPLanes[k] / mode_lib->vba.PixelClockBackEnd[k] * 8.0,
								mode_lib->vba.ForcedOutputLinkBPP[k],
								true,
								mode_lib->vba.Output[k],
								mode_lib->vba.OutputFormat[k],
								mode_lib->vba.DSCInputBitPerComponent[k]);
						if (mode_lib->vba.DSCEnabled[k] == true || mode_lib->vba.Outbpp == BPP_INVALID) {
							locals->RequiresDSC[i][k] = true;
							if (mode_lib->vba.Output[k] == dm_dp) {
								locals->RequiresFEC[i][k] = true;
							} else {
								locals->RequiresFEC[i][k] = false;
							}
							mode_lib->vba.Outbpp = mode_lib->vba.OutbppDSC;
						} else {
							locals->RequiresDSC[i][k] = false;
							locals->RequiresFEC[i][k] = false;
						}
						locals->OutputBppPerState[i][k] =
								mode_lib->vba.Outbpp;
					}
				}
			} else {
				locals->OutputBppPerState[i][k] = BPP_BLENDED_PIPE;
			}
		}
	}
	for (i = 0; i <= mode_lib->vba.soc.num_states; i++) {
		locals->DIOSupport[i] = true;
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			if (locals->OutputBppPerState[i][k] == BPP_INVALID
					|| (mode_lib->vba.OutputFormat[k] == dm_420
							&& mode_lib->vba.Interlace[k] == true
							&& mode_lib->vba.ProgressiveToInterlaceUnitInOPP == true)) {
				locals->DIOSupport[i] = false;
			}
		}
	}
	for (i = 0; i <= mode_lib->vba.soc.num_states; i++) {
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			locals->DSCCLKRequiredMoreThanSupported[i] = false;
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
					if (locals->RequiresDSC[i][k] == true) {
						if (locals->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_2to1) {
							if (mode_lib->vba.PixelClockBackEnd[k] / 6.0 / mode_lib->vba.DSCFormatFactor
									> (1.0 - mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100.0) * mode_lib->vba.MaxDSCCLK[i]) {
								locals->DSCCLKRequiredMoreThanSupported[i] =
										true;
							}
						} else {
							if (mode_lib->vba.PixelClockBackEnd[k] / 3.0 / mode_lib->vba.DSCFormatFactor
									> (1.0 - mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100.0) * mode_lib->vba.MaxDSCCLK[i]) {
								locals->DSCCLKRequiredMoreThanSupported[i] =
										true;
							}
						}
					}
				}
			}
		}
	}
	for (i = 0; i <= mode_lib->vba.soc.num_states; i++) {
		locals->NotEnoughDSCUnits[i] = false;
		mode_lib->vba.TotalDSCUnitsRequired = 0.0;
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			if (locals->RequiresDSC[i][k] == true) {
				if (locals->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_2to1) {
					mode_lib->vba.TotalDSCUnitsRequired =
							mode_lib->vba.TotalDSCUnitsRequired + 2.0;
				} else {
					mode_lib->vba.TotalDSCUnitsRequired =
							mode_lib->vba.TotalDSCUnitsRequired + 1.0;
				}
			}
		}
		if (mode_lib->vba.TotalDSCUnitsRequired > mode_lib->vba.NumberOfDSC) {
			locals->NotEnoughDSCUnits[i] = true;
		}
	}
	/*DSC Delay per state*/

	for (i = 0; i <= mode_lib->vba.soc.num_states; i++) {
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			if (mode_lib->vba.BlendingAndTiming[k] != k) {
				mode_lib->vba.slices = 0;
			} else if (locals->RequiresDSC[i][k] == 0
					|| locals->RequiresDSC[i][k] == false) {
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
			if (locals->OutputBppPerState[i][k] == BPP_BLENDED_PIPE
					|| locals->OutputBppPerState[i][k] == BPP_INVALID) {
				mode_lib->vba.bpp = 0.0;
			} else {
				mode_lib->vba.bpp = locals->OutputBppPerState[i][k];
			}
			if (locals->RequiresDSC[i][k] == true && mode_lib->vba.bpp != 0.0) {
				if (locals->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_disabled) {
					locals->DSCDelayPerState[i][k] =
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
					locals->DSCDelayPerState[i][k] =
							2.0 * (dscceComputeDelay(
											mode_lib->vba.DSCInputBitPerComponent[k],
											mode_lib->vba.bpp,
											dml_ceil(mode_lib->vba.HActive[k] / mode_lib->vba.slices, 1.0),
											mode_lib->vba.slices / 2,
											mode_lib->vba.OutputFormat[k])
									+ dscComputeDelay(mode_lib->vba.OutputFormat[k]));
				}
				locals->DSCDelayPerState[i][k] =
						locals->DSCDelayPerState[i][k] * mode_lib->vba.PixelClock[k] / mode_lib->vba.PixelClockBackEnd[k];
			} else {
				locals->DSCDelayPerState[i][k] = 0.0;
			}
		}
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			for (m = 0; m <= mode_lib->vba.NumberOfActivePlanes - 1; m++) {
				for (j = 0; j <= mode_lib->vba.NumberOfActivePlanes - 1; j++) {
					if (mode_lib->vba.BlendingAndTiming[k] == m && locals->RequiresDSC[i][m] == true)
						locals->DSCDelayPerState[i][k] = locals->DSCDelayPerState[i][m];
				}
			}
		}
	}

	//Prefetch Check
	for (i = 0; i <= mode_lib->vba.soc.num_states; ++i) {
		for (j = 0; j <= 1; ++j) {
			locals->TotalNumberOfDCCActiveDPP[i][j] = 0;
			for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
				if (mode_lib->vba.DCCEnable[k] == true)
					locals->TotalNumberOfDCCActiveDPP[i][j] = locals->TotalNumberOfDCCActiveDPP[i][j] + locals->NoOfDPP[i][j][k];
			}
		}
	}

	mode_lib->vba.UrgentLatency = dml_max3(
			mode_lib->vba.UrgentLatencyPixelDataOnly,
			mode_lib->vba.UrgentLatencyPixelMixedWithVMData,
			mode_lib->vba.UrgentLatencyVMDataOnly);
	mode_lib->vba.PrefetchERROR = CalculateMinAndMaxPrefetchMode(
			mode_lib->vba.AllowDRAMSelfRefreshOrDRAMClockChangeInVblank,
			&mode_lib->vba.MinPrefetchMode,
			&mode_lib->vba.MaxPrefetchMode);

	for (i = 0; i <= mode_lib->vba.soc.num_states; i++) {
		for (j = 0; j < 2; j++) {
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				locals->RequiredDPPCLKThisState[k] = locals->RequiredDPPCLK[i][j][k];
				locals->NoOfDPPThisState[k]        = locals->NoOfDPP[i][j][k];
				if (locals->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_2to1) {
					locals->SwathWidthYThisState[k] =
						dml_min(locals->SwathWidthYSingleDPP[k], dml_round(mode_lib->vba.HActive[k] / 2.0 * mode_lib->vba.HRatio[k]));
				} else {
					locals->SwathWidthYThisState[k] = locals->SwathWidthYSingleDPP[k] / locals->NoOfDPP[i][j][k];
				}
				mode_lib->vba.SwathWidthGranularityY = 256.0
					/ dml_ceil(locals->BytePerPixelInDETY[k], 1.0)
					/ locals->MaxSwathHeightY[k];
				mode_lib->vba.RoundedUpMaxSwathSizeBytesY =
					(dml_ceil(locals->SwathWidthYThisState[k] - 1.0, mode_lib->vba.SwathWidthGranularityY)
					+ mode_lib->vba.SwathWidthGranularityY) * locals->BytePerPixelInDETY[k] * locals->MaxSwathHeightY[k];
				if (mode_lib->vba.SourcePixelFormat[k] == dm_420_10) {
					mode_lib->vba.RoundedUpMaxSwathSizeBytesY = dml_ceil(
							mode_lib->vba.RoundedUpMaxSwathSizeBytesY,
							256.0) + 256;
				}
				if (locals->MaxSwathHeightC[k] > 0.0) {
					mode_lib->vba.SwathWidthGranularityC = 256.0 / dml_ceil(locals->BytePerPixelInDETC[k], 2.0) / locals->MaxSwathHeightC[k];
					mode_lib->vba.RoundedUpMaxSwathSizeBytesC = (dml_ceil(locals->SwathWidthYThisState[k] / 2.0 - 1.0, mode_lib->vba.SwathWidthGranularityC)
							+ mode_lib->vba.SwathWidthGranularityC) * locals->BytePerPixelInDETC[k] * locals->MaxSwathHeightC[k];
					if (mode_lib->vba.SourcePixelFormat[k] == dm_420_10) {
						mode_lib->vba.RoundedUpMaxSwathSizeBytesC = dml_ceil(mode_lib->vba.RoundedUpMaxSwathSizeBytesC, 256.0) + 256;
					}
				} else {
					mode_lib->vba.RoundedUpMaxSwathSizeBytesC = 0.0;
				}
				if (mode_lib->vba.RoundedUpMaxSwathSizeBytesY + mode_lib->vba.RoundedUpMaxSwathSizeBytesC
						<= mode_lib->vba.DETBufferSizeInKByte * 1024.0 / 2.0) {
					locals->SwathHeightYThisState[k] = locals->MaxSwathHeightY[k];
					locals->SwathHeightCThisState[k] = locals->MaxSwathHeightC[k];
				} else {
					locals->SwathHeightYThisState[k] =
							locals->MinSwathHeightY[k];
					locals->SwathHeightCThisState[k] =
							locals->MinSwathHeightC[k];
				}
			}

			CalculateDCFCLKDeepSleep(
					mode_lib,
					mode_lib->vba.NumberOfActivePlanes,
					locals->BytePerPixelInDETY,
					locals->BytePerPixelInDETC,
					mode_lib->vba.VRatio,
					locals->SwathWidthYThisState,
					locals->NoOfDPPThisState,
					mode_lib->vba.HRatio,
					mode_lib->vba.PixelClock,
					locals->PSCL_FACTOR,
					locals->PSCL_FACTOR_CHROMA,
					locals->RequiredDPPCLKThisState,
					&mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0]);

			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				if ((mode_lib->vba.SourcePixelFormat[k] != dm_444_64
						&& mode_lib->vba.SourcePixelFormat[k] != dm_444_32
						&& mode_lib->vba.SourcePixelFormat[k] != dm_444_16
						&& mode_lib->vba.SourcePixelFormat[k] != dm_mono_16
						&& mode_lib->vba.SourcePixelFormat[k] != dm_mono_8)) {
					mode_lib->vba.PDEAndMetaPTEBytesPerFrameC = CalculateVMAndRowBytes(
							mode_lib,
							mode_lib->vba.DCCEnable[k],
							locals->Read256BlockHeightC[k],
							locals->Read256BlockWidthC[k],
							mode_lib->vba.SourcePixelFormat[k],
							mode_lib->vba.SurfaceTiling[k],
							dml_ceil(locals->BytePerPixelInDETC[k], 2.0),
							mode_lib->vba.SourceScan[k],
							mode_lib->vba.ViewportWidth[k] / 2.0,
							mode_lib->vba.ViewportHeight[k] / 2.0,
							locals->SwathWidthYThisState[k] / 2.0,
							mode_lib->vba.GPUVMEnable,
							mode_lib->vba.HostVMEnable,
							mode_lib->vba.HostVMMaxPageTableLevels,
							mode_lib->vba.HostVMCachedPageTableLevels,
							mode_lib->vba.VMMPageSize,
							mode_lib->vba.PTEBufferSizeInRequestsChroma,
							mode_lib->vba.PitchC[k],
							0.0,
							&locals->MacroTileWidthC[k],
							&mode_lib->vba.MetaRowBytesC,
							&mode_lib->vba.DPTEBytesPerRowC,
							&locals->PTEBufferSizeNotExceededC[i][j][k],
							locals->dpte_row_width_chroma_ub,
							&locals->dpte_row_height_chroma[k],
							&locals->meta_req_width_chroma[k],
							&locals->meta_req_height_chroma[k],
							&locals->meta_row_width_chroma[k],
							&locals->meta_row_height_chroma[k],
							&locals->vm_group_bytes_chroma,
							&locals->dpte_group_bytes_chroma,
							locals->PixelPTEReqWidthC,
							locals->PixelPTEReqHeightC,
							locals->PTERequestSizeC,
							locals->dpde0_bytes_per_frame_ub_c,
							locals->meta_pte_bytes_per_frame_ub_c);
					locals->PrefetchLinesC[0][0][k] = CalculatePrefetchSourceLines(
							mode_lib,
							mode_lib->vba.VRatio[k]/2,
							mode_lib->vba.VTAPsChroma[k],
							mode_lib->vba.Interlace[k],
							mode_lib->vba.ProgressiveToInterlaceUnitInOPP,
							locals->SwathHeightCThisState[k],
							mode_lib->vba.ViewportYStartC[k],
							&locals->PrefillC[k],
							&locals->MaxNumSwC[k]);
					locals->PTEBufferSizeInRequestsForLuma = mode_lib->vba.PTEBufferSizeInRequestsLuma;
				} else {
					mode_lib->vba.PDEAndMetaPTEBytesPerFrameC = 0.0;
					mode_lib->vba.MetaRowBytesC = 0.0;
					mode_lib->vba.DPTEBytesPerRowC = 0.0;
					locals->PrefetchLinesC[0][0][k] = 0.0;
					locals->PTEBufferSizeNotExceededC[i][j][k] = true;
					locals->PTEBufferSizeInRequestsForLuma = mode_lib->vba.PTEBufferSizeInRequestsLuma + mode_lib->vba.PTEBufferSizeInRequestsChroma;
				}
				mode_lib->vba.PDEAndMetaPTEBytesPerFrameY = CalculateVMAndRowBytes(
						mode_lib,
						mode_lib->vba.DCCEnable[k],
						locals->Read256BlockHeightY[k],
						locals->Read256BlockWidthY[k],
						mode_lib->vba.SourcePixelFormat[k],
						mode_lib->vba.SurfaceTiling[k],
						dml_ceil(locals->BytePerPixelInDETY[k], 1.0),
						mode_lib->vba.SourceScan[k],
						mode_lib->vba.ViewportWidth[k],
						mode_lib->vba.ViewportHeight[k],
						locals->SwathWidthYThisState[k],
						mode_lib->vba.GPUVMEnable,
						mode_lib->vba.HostVMEnable,
						mode_lib->vba.HostVMMaxPageTableLevels,
						mode_lib->vba.HostVMCachedPageTableLevels,
						mode_lib->vba.VMMPageSize,
						locals->PTEBufferSizeInRequestsForLuma,
						mode_lib->vba.PitchY[k],
						mode_lib->vba.DCCMetaPitchY[k],
						&locals->MacroTileWidthY[k],
						&mode_lib->vba.MetaRowBytesY,
						&mode_lib->vba.DPTEBytesPerRowY,
						&locals->PTEBufferSizeNotExceededY[i][j][k],
						locals->dpte_row_width_luma_ub,
						&locals->dpte_row_height[k],
						&locals->meta_req_width[k],
						&locals->meta_req_height[k],
						&locals->meta_row_width[k],
						&locals->meta_row_height[k],
						&locals->vm_group_bytes[k],
						&locals->dpte_group_bytes[k],
						locals->PixelPTEReqWidthY,
						locals->PixelPTEReqHeightY,
						locals->PTERequestSizeY,
						locals->dpde0_bytes_per_frame_ub_l,
						locals->meta_pte_bytes_per_frame_ub_l);
				locals->PrefetchLinesY[0][0][k] = CalculatePrefetchSourceLines(
						mode_lib,
						mode_lib->vba.VRatio[k],
						mode_lib->vba.vtaps[k],
						mode_lib->vba.Interlace[k],
						mode_lib->vba.ProgressiveToInterlaceUnitInOPP,
						locals->SwathHeightYThisState[k],
						mode_lib->vba.ViewportYStartY[k],
						&locals->PrefillY[k],
						&locals->MaxNumSwY[k]);
				locals->PDEAndMetaPTEBytesPerFrame[0][0][k] =
						mode_lib->vba.PDEAndMetaPTEBytesPerFrameY + mode_lib->vba.PDEAndMetaPTEBytesPerFrameC;
				locals->MetaRowBytes[0][0][k] = mode_lib->vba.MetaRowBytesY + mode_lib->vba.MetaRowBytesC;
				locals->DPTEBytesPerRow[0][0][k] = mode_lib->vba.DPTEBytesPerRowY + mode_lib->vba.DPTEBytesPerRowC;

				CalculateActiveRowBandwidth(
						mode_lib->vba.GPUVMEnable,
						mode_lib->vba.SourcePixelFormat[k],
						mode_lib->vba.VRatio[k],
						mode_lib->vba.DCCEnable[k],
						mode_lib->vba.HTotal[k] /
						mode_lib->vba.PixelClock[k],
						mode_lib->vba.MetaRowBytesY,
						mode_lib->vba.MetaRowBytesC,
						locals->meta_row_height[k],
						locals->meta_row_height_chroma[k],
						mode_lib->vba.DPTEBytesPerRowY,
						mode_lib->vba.DPTEBytesPerRowC,
						locals->dpte_row_height[k],
						locals->dpte_row_height_chroma[k],
						&locals->meta_row_bw[k],
						&locals->dpte_row_bw[k]);
			}
			mode_lib->vba.ExtraLatency = CalculateExtraLatency(
					locals->UrgentRoundTripAndOutOfOrderLatencyPerState[i],
					locals->TotalNumberOfActiveDPP[i][j],
					mode_lib->vba.PixelChunkSizeInKByte,
					locals->TotalNumberOfDCCActiveDPP[i][j],
					mode_lib->vba.MetaChunkSize,
					locals->ReturnBWPerState[i][0],
					mode_lib->vba.GPUVMEnable,
					mode_lib->vba.HostVMEnable,
					mode_lib->vba.NumberOfActivePlanes,
					locals->NoOfDPPThisState,
					locals->dpte_group_bytes,
					mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
					mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
					mode_lib->vba.HostVMMaxPageTableLevels,
					mode_lib->vba.HostVMCachedPageTableLevels);

			mode_lib->vba.TimeCalc = 24.0 / mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0];
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				if (mode_lib->vba.BlendingAndTiming[k] == k) {
					if (mode_lib->vba.WritebackEnable[k] == true) {
						locals->WritebackDelay[i][k] = mode_lib->vba.WritebackLatency
								+ CalculateWriteBackDelay(
										mode_lib->vba.WritebackPixelFormat[k],
										mode_lib->vba.WritebackHRatio[k],
										mode_lib->vba.WritebackVRatio[k],
										mode_lib->vba.WritebackLumaHTaps[k],
										mode_lib->vba.WritebackLumaVTaps[k],
										mode_lib->vba.WritebackChromaHTaps[k],
										mode_lib->vba.WritebackChromaVTaps[k],
										mode_lib->vba.WritebackDestinationWidth[k]) / locals->RequiredDISPCLK[i][j];
					} else {
						locals->WritebackDelay[i][k] = 0.0;
					}
					for (m = 0; m <= mode_lib->vba.NumberOfActivePlanes - 1; m++) {
						if (mode_lib->vba.BlendingAndTiming[m] == k
								&& mode_lib->vba.WritebackEnable[m]
										== true) {
							locals->WritebackDelay[i][k] = dml_max(locals->WritebackDelay[i][k],
											mode_lib->vba.WritebackLatency + CalculateWriteBackDelay(
													mode_lib->vba.WritebackPixelFormat[m],
													mode_lib->vba.WritebackHRatio[m],
													mode_lib->vba.WritebackVRatio[m],
													mode_lib->vba.WritebackLumaHTaps[m],
													mode_lib->vba.WritebackLumaVTaps[m],
													mode_lib->vba.WritebackChromaHTaps[m],
													mode_lib->vba.WritebackChromaVTaps[m],
													mode_lib->vba.WritebackDestinationWidth[m]) / locals->RequiredDISPCLK[i][j]);
						}
					}
				}
			}
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				for (m = 0; m <= mode_lib->vba.NumberOfActivePlanes - 1; m++) {
					if (mode_lib->vba.BlendingAndTiming[k] == m) {
						locals->WritebackDelay[i][k] = locals->WritebackDelay[i][m];
					}
				}
			}
			mode_lib->vba.MaxMaxVStartup[0][0] = 0;
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				locals->MaximumVStartup[0][0][k] = mode_lib->vba.VTotal[k] - mode_lib->vba.VActive[k]
					- dml_max(1.0, dml_ceil(locals->WritebackDelay[i][k] / (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k]), 1.0));
				mode_lib->vba.MaxMaxVStartup[0][0] = dml_max(mode_lib->vba.MaxMaxVStartup[0][0], locals->MaximumVStartup[0][0][k]);
			}

			mode_lib->vba.NextPrefetchMode = mode_lib->vba.MinPrefetchMode;
			mode_lib->vba.NextMaxVStartup = mode_lib->vba.MaxMaxVStartup[0][0];
			do {
				mode_lib->vba.PrefetchMode[i][j] = mode_lib->vba.NextPrefetchMode;
				mode_lib->vba.MaxVStartup = mode_lib->vba.NextMaxVStartup;

				mode_lib->vba.TWait = CalculateTWait(
						mode_lib->vba.PrefetchMode[i][j],
						mode_lib->vba.DRAMClockChangeLatency,
						mode_lib->vba.UrgentLatency,
						mode_lib->vba.SREnterPlusExitTime);
				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
					Pipe myPipe;
					HostVM myHostVM;

					if (mode_lib->vba.XFCEnabled[k] == true) {
						mode_lib->vba.XFCRemoteSurfaceFlipDelay =
								CalculateRemoteSurfaceFlipDelay(
										mode_lib,
										mode_lib->vba.VRatio[k],
										locals->SwathWidthYThisState[k],
										dml_ceil(locals->BytePerPixelInDETY[k], 1.0),
										mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k],
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

					myPipe.DPPCLK = locals->RequiredDPPCLK[i][j][k];
					myPipe.DISPCLK = locals->RequiredDISPCLK[i][j];
					myPipe.PixelClock = mode_lib->vba.PixelClock[k];
					myPipe.DCFCLKDeepSleep = mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0];
					myPipe.DPPPerPlane = locals->NoOfDPP[i][j][k];
					myPipe.ScalerEnabled = mode_lib->vba.ScalerEnabled[k];
					myPipe.SourceScan = mode_lib->vba.SourceScan[k];
					myPipe.BlockWidth256BytesY = locals->Read256BlockWidthY[k];
					myPipe.BlockHeight256BytesY = locals->Read256BlockHeightY[k];
					myPipe.BlockWidth256BytesC = locals->Read256BlockWidthC[k];
					myPipe.BlockHeight256BytesC = locals->Read256BlockHeightC[k];
					myPipe.InterlaceEnable = mode_lib->vba.Interlace[k];
					myPipe.NumberOfCursors = mode_lib->vba.NumberOfCursors[k];
					myPipe.VBlank = mode_lib->vba.VTotal[k] - mode_lib->vba.VActive[k];
					myPipe.HTotal = mode_lib->vba.HTotal[k];


					myHostVM.Enable = mode_lib->vba.HostVMEnable;
					myHostVM.MaxPageTableLevels = mode_lib->vba.HostVMMaxPageTableLevels;
					myHostVM.CachedPageTableLevels = mode_lib->vba.HostVMCachedPageTableLevels;


					mode_lib->vba.IsErrorResult[i][j][k] = CalculatePrefetchSchedule(
							mode_lib,
							mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
							mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
							&myPipe,
							locals->DSCDelayPerState[i][k],
							mode_lib->vba.DPPCLKDelaySubtotal,
							mode_lib->vba.DPPCLKDelaySCL,
							mode_lib->vba.DPPCLKDelaySCLLBOnly,
							mode_lib->vba.DPPCLKDelayCNVCFormater,
							mode_lib->vba.DPPCLKDelayCNVCCursor,
							mode_lib->vba.DISPCLKDelaySubtotal,
							locals->SwathWidthYThisState[k] / mode_lib->vba.HRatio[k],
							mode_lib->vba.OutputFormat[k],
							mode_lib->vba.MaxInterDCNTileRepeaters,
							dml_min(mode_lib->vba.MaxVStartup, locals->MaximumVStartup[0][0][k]),
							locals->MaximumVStartup[0][0][k],
							mode_lib->vba.GPUVMMaxPageTableLevels,
							mode_lib->vba.GPUVMEnable,
							&myHostVM,
							mode_lib->vba.DynamicMetadataEnable[k],
							mode_lib->vba.DynamicMetadataLinesBeforeActiveRequired[k],
							mode_lib->vba.DynamicMetadataTransmittedBytes[k],
							mode_lib->vba.DCCEnable[k],
							mode_lib->vba.UrgentLatency,
							mode_lib->vba.ExtraLatency,
							mode_lib->vba.TimeCalc,
							locals->PDEAndMetaPTEBytesPerFrame[0][0][k],
							locals->MetaRowBytes[0][0][k],
							locals->DPTEBytesPerRow[0][0][k],
							locals->PrefetchLinesY[0][0][k],
							locals->SwathWidthYThisState[k],
							locals->BytePerPixelInDETY[k],
							locals->PrefillY[k],
							locals->MaxNumSwY[k],
							locals->PrefetchLinesC[0][0][k],
							locals->BytePerPixelInDETC[k],
							locals->PrefillC[k],
							locals->MaxNumSwC[k],
							locals->SwathHeightYThisState[k],
							locals->SwathHeightCThisState[k],
							mode_lib->vba.TWait,
							mode_lib->vba.XFCEnabled[k],
							mode_lib->vba.XFCRemoteSurfaceFlipDelay,
							mode_lib->vba.ProgressiveToInterlaceUnitInOPP,
							&locals->dst_x_after_scaler,
							&locals->dst_y_after_scaler,
							&locals->LineTimesForPrefetch[k],
							&locals->PrefetchBW[k],
							&locals->LinesForMetaPTE[k],
							&locals->LinesForMetaAndDPTERow[k],
							&locals->VRatioPreY[i][j][k],
							&locals->VRatioPreC[i][j][k],
							&locals->RequiredPrefetchPixelDataBWLuma[i][j][k],
							&locals->RequiredPrefetchPixelDataBWChroma[i][j][k],
							&locals->VStartupRequiredWhenNotEnoughTimeForDynamicMetadata,
							&locals->Tno_bw[k],
							&locals->prefetch_vmrow_bw[k],
							locals->swath_width_luma_ub,
							locals->swath_width_chroma_ub,
							&mode_lib->vba.VUpdateOffsetPix[k],
							&mode_lib->vba.VUpdateWidthPix[k],
							&mode_lib->vba.VReadyOffsetPix[k]);
				}
				mode_lib->vba.MaximumReadBandwidthWithoutPrefetch = 0.0;
				mode_lib->vba.MaximumReadBandwidthWithPrefetch = 0.0;
				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
					unsigned int m;

					locals->cursor_bw[k] = 0;
					locals->cursor_bw_pre[k] = 0;
					for (m = 0; m < mode_lib->vba.NumberOfCursors[k]; m++) {
						locals->cursor_bw[k] = mode_lib->vba.CursorWidth[k][m] * mode_lib->vba.CursorBPP[k][m]
							/ 8.0 / (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k]) * mode_lib->vba.VRatio[k];
						locals->cursor_bw_pre[k] = mode_lib->vba.CursorWidth[k][m] * mode_lib->vba.CursorBPP[k][m]
							/ 8.0 / (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k]) * locals->VRatioPreY[i][j][k];
					}

					CalculateUrgentBurstFactor(
							mode_lib->vba.DETBufferSizeInKByte,
							locals->SwathHeightYThisState[k],
							locals->SwathHeightCThisState[k],
							locals->SwathWidthYThisState[k],
							mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k],
							mode_lib->vba.UrgentLatency,
							mode_lib->vba.CursorBufferSize,
							mode_lib->vba.CursorWidth[k][0] + mode_lib->vba.CursorWidth[k][1],
							dml_max(mode_lib->vba.CursorBPP[k][0], mode_lib->vba.CursorBPP[k][1]),
							mode_lib->vba.VRatio[k],
							locals->VRatioPreY[i][j][k],
							locals->VRatioPreC[i][j][k],
							locals->BytePerPixelInDETY[k],
							locals->BytePerPixelInDETC[k],
							&locals->UrgentBurstFactorCursor[k],
							&locals->UrgentBurstFactorCursorPre[k],
							&locals->UrgentBurstFactorLuma[k],
							&locals->UrgentBurstFactorLumaPre[k],
							&locals->UrgentBurstFactorChroma[k],
							&locals->UrgentBurstFactorChromaPre[k],
							&locals->NotEnoughUrgentLatencyHiding,
							&locals->NotEnoughUrgentLatencyHidingPre);

					if (mode_lib->vba.UseUrgentBurstBandwidth == false) {
						locals->UrgentBurstFactorCursor[k] = 1;
						locals->UrgentBurstFactorCursorPre[k] = 1;
						locals->UrgentBurstFactorLuma[k] = 1;
						locals->UrgentBurstFactorLumaPre[k] = 1;
						locals->UrgentBurstFactorChroma[k] = 1;
						locals->UrgentBurstFactorChromaPre[k] = 1;
					}

					mode_lib->vba.MaximumReadBandwidthWithoutPrefetch = mode_lib->vba.MaximumReadBandwidthWithoutPrefetch
						+ locals->cursor_bw[k] * locals->UrgentBurstFactorCursor[k] + locals->ReadBandwidthLuma[k]
						* locals->UrgentBurstFactorLuma[k] + locals->ReadBandwidthChroma[k]
						* locals->UrgentBurstFactorChroma[k] + locals->meta_row_bw[k] + locals->dpte_row_bw[k];
					mode_lib->vba.MaximumReadBandwidthWithPrefetch = mode_lib->vba.MaximumReadBandwidthWithPrefetch
						+ dml_max3(locals->prefetch_vmrow_bw[k],
						locals->ReadBandwidthLuma[k] * locals->UrgentBurstFactorLuma[k] + locals->ReadBandwidthChroma[k]
						* locals->UrgentBurstFactorChroma[k] + locals->cursor_bw[k] * locals->UrgentBurstFactorCursor[k]
						+ locals->meta_row_bw[k] + locals->dpte_row_bw[k],
						locals->RequiredPrefetchPixelDataBWLuma[i][j][k] * locals->UrgentBurstFactorLumaPre[k]
						+ locals->RequiredPrefetchPixelDataBWChroma[i][j][k] * locals->UrgentBurstFactorChromaPre[k]
						+ locals->cursor_bw_pre[k] * locals->UrgentBurstFactorCursorPre[k]);
				}
				locals->BandwidthWithoutPrefetchSupported[i][0] = true;
				if (mode_lib->vba.MaximumReadBandwidthWithoutPrefetch > locals->ReturnBWPerState[i][0]
						|| locals->NotEnoughUrgentLatencyHiding == 1) {
					locals->BandwidthWithoutPrefetchSupported[i][0] = false;
				}

				locals->PrefetchSupported[i][j] = true;
				if (mode_lib->vba.MaximumReadBandwidthWithPrefetch > locals->ReturnBWPerState[i][0]
						|| locals->NotEnoughUrgentLatencyHiding == 1
						|| locals->NotEnoughUrgentLatencyHidingPre == 1) {
					locals->PrefetchSupported[i][j] = false;
				}
				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
					if (locals->LineTimesForPrefetch[k] < 2.0
							|| locals->LinesForMetaPTE[k] >= 32.0
							|| locals->LinesForMetaAndDPTERow[k] >= 16.0
							|| mode_lib->vba.IsErrorResult[i][j][k] == true) {
						locals->PrefetchSupported[i][j] = false;
					}
				}
				locals->VRatioInPrefetchSupported[i][j] = true;
				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
					if (locals->VRatioPreY[i][j][k] > 4.0
							|| locals->VRatioPreC[i][j][k] > 4.0
							|| mode_lib->vba.IsErrorResult[i][j][k] == true) {
						locals->VRatioInPrefetchSupported[i][j] = false;
					}
				}
				mode_lib->vba.AnyLinesForVMOrRowTooLarge = false;
				for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
					if (locals->LinesForMetaAndDPTERow[k] >= 16 || locals->LinesForMetaPTE[k] >= 32) {
						mode_lib->vba.AnyLinesForVMOrRowTooLarge = true;
					}
				}

				if (mode_lib->vba.MaxVStartup <= 13 || mode_lib->vba.AnyLinesForVMOrRowTooLarge == false) {
					mode_lib->vba.NextMaxVStartup = mode_lib->vba.MaxMaxVStartup[0][0];
					mode_lib->vba.NextPrefetchMode = mode_lib->vba.NextPrefetchMode + 1;
				} else {
					mode_lib->vba.NextMaxVStartup = mode_lib->vba.NextMaxVStartup - 1;
				}
			} while ((locals->PrefetchSupported[i][j] != true || locals->VRatioInPrefetchSupported[i][j] != true)
					&& (mode_lib->vba.NextMaxVStartup != mode_lib->vba.MaxMaxVStartup[0][0]
						|| mode_lib->vba.NextPrefetchMode <= mode_lib->vba.MaxPrefetchMode));

			if (locals->PrefetchSupported[i][j] == true && locals->VRatioInPrefetchSupported[i][j] == true) {
				mode_lib->vba.BandwidthAvailableForImmediateFlip = locals->ReturnBWPerState[i][0];
				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
					mode_lib->vba.BandwidthAvailableForImmediateFlip = mode_lib->vba.BandwidthAvailableForImmediateFlip
						- dml_max(locals->ReadBandwidthLuma[k] * locals->UrgentBurstFactorLuma[k]
							+ locals->ReadBandwidthChroma[k] * locals->UrgentBurstFactorChroma[k]
							+ locals->cursor_bw[k] * locals->UrgentBurstFactorCursor[k],
							locals->RequiredPrefetchPixelDataBWLuma[i][j][k] * locals->UrgentBurstFactorLumaPre[k]
							+ locals->RequiredPrefetchPixelDataBWChroma[i][j][k] * locals->UrgentBurstFactorChromaPre[k]
							+ locals->cursor_bw_pre[k] * locals->UrgentBurstFactorCursorPre[k]);
				}
				mode_lib->vba.TotImmediateFlipBytes = 0.0;
				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
					mode_lib->vba.TotImmediateFlipBytes = mode_lib->vba.TotImmediateFlipBytes
						+ locals->PDEAndMetaPTEBytesPerFrame[0][0][k] + locals->MetaRowBytes[0][0][k] + locals->DPTEBytesPerRow[0][0][k];
				}

				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
					CalculateFlipSchedule(
							mode_lib,
							mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData,
							mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly,
							mode_lib->vba.ExtraLatency,
							mode_lib->vba.UrgentLatency,
							mode_lib->vba.GPUVMMaxPageTableLevels,
							mode_lib->vba.HostVMEnable,
							mode_lib->vba.HostVMMaxPageTableLevels,
							mode_lib->vba.HostVMCachedPageTableLevels,
							mode_lib->vba.GPUVMEnable,
							locals->PDEAndMetaPTEBytesPerFrame[0][0][k],
							locals->MetaRowBytes[0][0][k],
							locals->DPTEBytesPerRow[0][0][k],
							mode_lib->vba.BandwidthAvailableForImmediateFlip,
							mode_lib->vba.TotImmediateFlipBytes,
							mode_lib->vba.SourcePixelFormat[k],
							mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k],
							mode_lib->vba.VRatio[k],
							locals->Tno_bw[k],
							mode_lib->vba.DCCEnable[k],
							locals->dpte_row_height[k],
							locals->meta_row_height[k],
							locals->dpte_row_height_chroma[k],
							locals->meta_row_height_chroma[k],
							&locals->DestinationLinesToRequestVMInImmediateFlip[k],
							&locals->DestinationLinesToRequestRowInImmediateFlip[k],
							&locals->final_flip_bw[k],
							&locals->ImmediateFlipSupportedForPipe[k]);
				}
				mode_lib->vba.total_dcn_read_bw_with_flip = 0.0;
				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
					mode_lib->vba.total_dcn_read_bw_with_flip = mode_lib->vba.total_dcn_read_bw_with_flip + dml_max3(
						locals->prefetch_vmrow_bw[k],
						locals->final_flip_bw[k] +  locals->ReadBandwidthLuma[k] * locals->UrgentBurstFactorLuma[k]
						+ locals->ReadBandwidthChroma[k] * locals->UrgentBurstFactorChroma[k]
						+ locals->cursor_bw[k] * locals->UrgentBurstFactorCursor[k],
						locals->final_flip_bw[k] + locals->RequiredPrefetchPixelDataBWLuma[i][j][k]
						* locals->UrgentBurstFactorLumaPre[k] + locals->RequiredPrefetchPixelDataBWChroma[i][j][k]
						* locals->UrgentBurstFactorChromaPre[k] + locals->cursor_bw_pre[k]
						* locals->UrgentBurstFactorCursorPre[k]);
				}
				locals->ImmediateFlipSupportedForState[i][j] = true;
				if (mode_lib->vba.total_dcn_read_bw_with_flip
						> locals->ReturnBWPerState[i][0]) {
					locals->ImmediateFlipSupportedForState[i][j] = false;
				}
				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
					if (locals->ImmediateFlipSupportedForPipe[k] == false) {
						locals->ImmediateFlipSupportedForState[i][j] = false;
					}
				}
			} else {
				locals->ImmediateFlipSupportedForState[i][j] = false;
			}
			mode_lib->vba.UrgentOutOfOrderReturnPerChannel = dml_max3(
					mode_lib->vba.UrgentOutOfOrderReturnPerChannelPixelDataOnly,
					mode_lib->vba.UrgentOutOfOrderReturnPerChannelPixelMixedWithVMData,
					mode_lib->vba.UrgentOutOfOrderReturnPerChannelVMDataOnly);
			CalculateWatermarksAndDRAMSpeedChangeSupport(
					mode_lib,
					mode_lib->vba.PrefetchMode[i][j],
					mode_lib->vba.NumberOfActivePlanes,
					mode_lib->vba.MaxLineBufferLines,
					mode_lib->vba.LineBufferSize,
					mode_lib->vba.DPPOutputBufferPixels,
					mode_lib->vba.DETBufferSizeInKByte,
					mode_lib->vba.WritebackInterfaceLumaBufferSize,
					mode_lib->vba.WritebackInterfaceChromaBufferSize,
					mode_lib->vba.DCFCLKPerState[i],
					mode_lib->vba.UrgentOutOfOrderReturnPerChannel * mode_lib->vba.NumberOfChannels,
					locals->ReturnBWPerState[i][0],
					mode_lib->vba.GPUVMEnable,
					locals->dpte_group_bytes,
					mode_lib->vba.MetaChunkSize,
					mode_lib->vba.UrgentLatency,
					mode_lib->vba.ExtraLatency,
					mode_lib->vba.WritebackLatency,
					mode_lib->vba.WritebackChunkSize,
					mode_lib->vba.SOCCLKPerState[i],
					mode_lib->vba.DRAMClockChangeLatency,
					mode_lib->vba.SRExitTime,
					mode_lib->vba.SREnterPlusExitTime,
					mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0],
					locals->NoOfDPPThisState,
					mode_lib->vba.DCCEnable,
					locals->RequiredDPPCLKThisState,
					locals->SwathWidthYSingleDPP,
					locals->SwathHeightYThisState,
					locals->ReadBandwidthLuma,
					locals->SwathHeightCThisState,
					locals->ReadBandwidthChroma,
					mode_lib->vba.LBBitPerPixel,
					locals->SwathWidthYThisState,
					mode_lib->vba.HRatio,
					mode_lib->vba.vtaps,
					mode_lib->vba.VTAPsChroma,
					mode_lib->vba.VRatio,
					mode_lib->vba.HTotal,
					mode_lib->vba.PixelClock,
					mode_lib->vba.BlendingAndTiming,
					locals->BytePerPixelInDETY,
					locals->BytePerPixelInDETC,
					mode_lib->vba.WritebackEnable,
					mode_lib->vba.WritebackPixelFormat,
					mode_lib->vba.WritebackDestinationWidth,
					mode_lib->vba.WritebackDestinationHeight,
					mode_lib->vba.WritebackSourceHeight,
					&locals->DRAMClockChangeSupport[i][j],
					&mode_lib->vba.UrgentWatermark,
					&mode_lib->vba.WritebackUrgentWatermark,
					&mode_lib->vba.DRAMClockChangeWatermark,
					&mode_lib->vba.WritebackDRAMClockChangeWatermark,
					&mode_lib->vba.StutterExitWatermark,
					&mode_lib->vba.StutterEnterPlusExitWatermark,
					&mode_lib->vba.MinActiveDRAMClockChangeLatencySupported);
			}
		}

		/*Vertical Active BW support*/
		{
			double MaxTotalVActiveRDBandwidth = 0.0;
			for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
				MaxTotalVActiveRDBandwidth = MaxTotalVActiveRDBandwidth + locals->ReadBandwidth[k];
		}
		for (i = 0; i <= mode_lib->vba.soc.num_states; ++i) {
			locals->MaxTotalVerticalActiveAvailableBandwidth[i][0] = dml_min(
				locals->IdealSDPPortBandwidthPerState[i][0] *
				mode_lib->vba.MaxAveragePercentOfIdealSDPPortBWDisplayCanUseInNormalSystemOperation
				/ 100.0, mode_lib->vba.DRAMSpeedPerState[i] *
				mode_lib->vba.NumberOfChannels *
				mode_lib->vba.DRAMChannelWidth *
				mode_lib->vba.MaxAveragePercentOfIdealDRAMBWDisplayCanUseInNormalSystemOperation
				/ 100.0);

			if (MaxTotalVActiveRDBandwidth <= locals->MaxTotalVerticalActiveAvailableBandwidth[i][0]) {
				locals->TotalVerticalActiveBandwidthSupport[i][0] = true;
			} else {
				locals->TotalVerticalActiveBandwidthSupport[i][0] = false;
			}
		}
	}

	/*PTE Buffer Size Check*/

	for (i = 0; i <= mode_lib->vba.soc.num_states; i++) {
		for (j = 0; j < 2; j++) {
			locals->PTEBufferSizeNotExceeded[i][j] = true;
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				if (locals->PTEBufferSizeNotExceededY[i][j][k] == false
						|| locals->PTEBufferSizeNotExceededC[i][j][k] == false) {
					locals->PTEBufferSizeNotExceeded[i][j] = false;
				}
			}
		}
	}
	/*Cursor Support Check*/

	mode_lib->vba.CursorSupport = true;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.CursorWidth[k][0] > 0.0) {
			for (m = 0; m < mode_lib->vba.NumberOfCursors[k]; m++) {
				if (mode_lib->vba.CursorBPP[k][m] == 64 && mode_lib->vba.Cursor64BppSupport == false) {
					mode_lib->vba.CursorSupport = false;
				}
			}
		}
	}
	/*Valid Pitch Check*/

	mode_lib->vba.PitchSupport = true;
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		locals->AlignedYPitch[k] = dml_ceil(
				dml_max(mode_lib->vba.PitchY[k], mode_lib->vba.ViewportWidth[k]),
				locals->MacroTileWidthY[k]);
		if (locals->AlignedYPitch[k] > mode_lib->vba.PitchY[k]) {
			mode_lib->vba.PitchSupport = false;
		}
		if (mode_lib->vba.DCCEnable[k] == true) {
			locals->AlignedDCCMetaPitch[k] = dml_ceil(
					dml_max(
							mode_lib->vba.DCCMetaPitchY[k],
							mode_lib->vba.ViewportWidth[k]),
					64.0 * locals->Read256BlockWidthY[k]);
		} else {
			locals->AlignedDCCMetaPitch[k] = mode_lib->vba.DCCMetaPitchY[k];
		}
		if (locals->AlignedDCCMetaPitch[k] > mode_lib->vba.DCCMetaPitchY[k]) {
			mode_lib->vba.PitchSupport = false;
		}
		if (mode_lib->vba.SourcePixelFormat[k] != dm_444_64
				&& mode_lib->vba.SourcePixelFormat[k] != dm_444_32
				&& mode_lib->vba.SourcePixelFormat[k] != dm_444_16
				&& mode_lib->vba.SourcePixelFormat[k] != dm_mono_16
				&& mode_lib->vba.SourcePixelFormat[k] != dm_mono_8) {
			locals->AlignedCPitch[k] = dml_ceil(
					dml_max(
							mode_lib->vba.PitchC[k],
							mode_lib->vba.ViewportWidth[k] / 2.0),
					locals->MacroTileWidthC[k]);
		} else {
			locals->AlignedCPitch[k] = mode_lib->vba.PitchC[k];
		}
		if (locals->AlignedCPitch[k] > mode_lib->vba.PitchC[k]) {
			mode_lib->vba.PitchSupport = false;
		}
	}
	/*Mode Support, Voltage State and SOC Configuration*/

	for (i = mode_lib->vba.soc.num_states; i >= 0; i--) {
		for (j = 0; j < 2; j++) {
			enum dm_validation_status status = DML_VALIDATION_OK;

			if (mode_lib->vba.ScaleRatioAndTapsSupport != true) {
				status = DML_FAIL_SCALE_RATIO_TAP;
			} else if (mode_lib->vba.SourceFormatPixelAndScanSupport != true) {
				status = DML_FAIL_SOURCE_PIXEL_FORMAT;
			} else if (locals->ViewportSizeSupport[i][0] != true) {
				status = DML_FAIL_VIEWPORT_SIZE;
			} else if (locals->DIOSupport[i] != true) {
				status = DML_FAIL_DIO_SUPPORT;
			} else if (locals->NotEnoughDSCUnits[i] != false) {
				status = DML_FAIL_NOT_ENOUGH_DSC;
			} else if (locals->DSCCLKRequiredMoreThanSupported[i] != false) {
				status = DML_FAIL_DSC_CLK_REQUIRED;
			} else if (locals->ROBSupport[i][0] != true) {
				status = DML_FAIL_REORDERING_BUFFER;
			} else if (locals->DISPCLK_DPPCLK_Support[i][j] != true) {
				status = DML_FAIL_DISPCLK_DPPCLK;
			} else if (locals->TotalAvailablePipesSupport[i][j] != true) {
				status = DML_FAIL_TOTAL_AVAILABLE_PIPES;
			} else if (mode_lib->vba.NumberOfOTGSupport != true) {
				status = DML_FAIL_NUM_OTG;
			} else if (mode_lib->vba.WritebackModeSupport != true) {
				status = DML_FAIL_WRITEBACK_MODE;
			} else if (mode_lib->vba.WritebackLatencySupport != true) {
				status = DML_FAIL_WRITEBACK_LATENCY;
			} else if (mode_lib->vba.WritebackScaleRatioAndTapsSupport != true) {
				status = DML_FAIL_WRITEBACK_SCALE_RATIO_TAP;
			} else if (mode_lib->vba.CursorSupport != true) {
				status = DML_FAIL_CURSOR_SUPPORT;
			} else if (mode_lib->vba.PitchSupport != true) {
				status = DML_FAIL_PITCH_SUPPORT;
			} else if (locals->TotalVerticalActiveBandwidthSupport[i][0] != true) {
				status = DML_FAIL_TOTAL_V_ACTIVE_BW;
			} else if (locals->PTEBufferSizeNotExceeded[i][j] != true) {
				status = DML_FAIL_PTE_BUFFER_SIZE;
			} else if (mode_lib->vba.NonsupportedDSCInputBPC != false) {
				status = DML_FAIL_DSC_INPUT_BPC;
			} else if ((mode_lib->vba.HostVMEnable != false
					&& locals->ImmediateFlipSupportedForState[i][j] != true)) {
				status = DML_FAIL_HOST_VM_IMMEDIATE_FLIP;
			} else if (locals->PrefetchSupported[i][j] != true) {
				status = DML_FAIL_PREFETCH_SUPPORT;
			} else if (locals->VRatioInPrefetchSupported[i][j] != true) {
				status = DML_FAIL_V_RATIO_PREFETCH;
			}

			if (status == DML_VALIDATION_OK) {
				locals->ModeSupport[i][j] = true;
			} else {
				locals->ModeSupport[i][j] = false;
			}
			locals->ValidationStatus[i] = status;
		}
	}
	{
		unsigned int MaximumMPCCombine = 0;
		mode_lib->vba.VoltageLevel = mode_lib->vba.soc.num_states + 1;
		for (i = mode_lib->vba.VoltageOverrideLevel; i <= mode_lib->vba.soc.num_states; i++) {
			if (locals->ModeSupport[i][0] == true || locals->ModeSupport[i][1] == true) {
				mode_lib->vba.VoltageLevel = i;
				if (locals->ModeSupport[i][1] == true && (locals->ModeSupport[i][0] == false
						|| mode_lib->vba.WhenToDoMPCCombine == dm_mpc_always_when_possible
						|| (mode_lib->vba.WhenToDoMPCCombine == dm_mpc_reduce_voltage_and_clocks
							&& ((locals->DRAMClockChangeSupport[i][1] == dm_dram_clock_change_vactive
								&& locals->DRAMClockChangeSupport[i][0] != dm_dram_clock_change_vactive)
							|| (locals->DRAMClockChangeSupport[i][1] == dm_dram_clock_change_vblank
								&& locals->DRAMClockChangeSupport[i][0] == dm_dram_clock_change_unsupported))))) {
					MaximumMPCCombine = 1;
				} else {
					MaximumMPCCombine = 0;
				}
				break;
			}
		}
		mode_lib->vba.ImmediateFlipSupport =
			locals->ImmediateFlipSupportedForState[mode_lib->vba.VoltageLevel][MaximumMPCCombine];
		for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
			mode_lib->vba.DPPPerPlane[k] = locals->NoOfDPP[mode_lib->vba.VoltageLevel][MaximumMPCCombine][k];
			locals->DPPCLK[k] = locals->RequiredDPPCLK[mode_lib->vba.VoltageLevel][MaximumMPCCombine][k];
		}
		mode_lib->vba.DISPCLK = locals->RequiredDISPCLK[mode_lib->vba.VoltageLevel][MaximumMPCCombine];
		mode_lib->vba.maxMpcComb = MaximumMPCCombine;
	}
	mode_lib->vba.DCFCLK = mode_lib->vba.DCFCLKPerState[mode_lib->vba.VoltageLevel];
	mode_lib->vba.DRAMSpeed = mode_lib->vba.DRAMSpeedPerState[mode_lib->vba.VoltageLevel];
	mode_lib->vba.FabricClock = mode_lib->vba.FabricClockPerState[mode_lib->vba.VoltageLevel];
	mode_lib->vba.SOCCLK = mode_lib->vba.SOCCLKPerState[mode_lib->vba.VoltageLevel];
	mode_lib->vba.ReturnBW = locals->ReturnBWPerState[mode_lib->vba.VoltageLevel][0];
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.BlendingAndTiming[k] == k) {
			mode_lib->vba.ODMCombineEnabled[k] =
					locals->ODMCombineEnablePerState[mode_lib->vba.VoltageLevel][k];
		} else {
			mode_lib->vba.ODMCombineEnabled[k] = false;
		}
		mode_lib->vba.DSCEnabled[k] =
				locals->RequiresDSC[mode_lib->vba.VoltageLevel][k];
		mode_lib->vba.OutputBpp[k] =
				locals->OutputBppPerState[mode_lib->vba.VoltageLevel][k];
	}
}

static void CalculateWatermarksAndDRAMSpeedChangeSupport(
		struct display_mode_lib *mode_lib,
		unsigned int PrefetchMode,
		unsigned int NumberOfActivePlanes,
		unsigned int MaxLineBufferLines,
		unsigned int LineBufferSize,
		unsigned int DPPOutputBufferPixels,
		double DETBufferSizeInKByte,
		unsigned int WritebackInterfaceLumaBufferSize,
		unsigned int WritebackInterfaceChromaBufferSize,
		double DCFCLK,
		double UrgentOutOfOrderReturn,
		double ReturnBW,
		bool GPUVMEnable,
		int dpte_group_bytes[],
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
		int DPPPerPlane[],
		bool DCCEnable[],
		double DPPCLK[],
		double SwathWidthSingleDPPY[],
		unsigned int SwathHeightY[],
		double ReadBandwidthPlaneLuma[],
		unsigned int SwathHeightC[],
		double ReadBandwidthPlaneChroma[],
		unsigned int LBBitPerPixel[],
		double SwathWidthY[],
		double HRatio[],
		unsigned int vtaps[],
		unsigned int VTAPsChroma[],
		double VRatio[],
		unsigned int HTotal[],
		double PixelClock[],
		unsigned int BlendingAndTiming[],
		double BytePerPixelDETY[],
		double BytePerPixelDETC[],
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
	double EffectiveLBLatencyHidingY;
	double EffectiveLBLatencyHidingC;
	double DPPOutputBufferLinesY;
	double DPPOutputBufferLinesC;
	double DETBufferSizeY;
	double DETBufferSizeC;
	double LinesInDETY[DC__NUM_DPP__MAX];
	double LinesInDETC;
	unsigned int LinesInDETYRoundedDownToSwath[DC__NUM_DPP__MAX];
	unsigned int LinesInDETCRoundedDownToSwath;
	double FullDETBufferingTimeY[DC__NUM_DPP__MAX];
	double FullDETBufferingTimeC;
	double ActiveDRAMClockChangeLatencyMarginY;
	double ActiveDRAMClockChangeLatencyMarginC;
	double WritebackDRAMClockChangeLatencyMargin;
	double PlaneWithMinActiveDRAMClockChangeMargin;
	double SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank;
	double FullDETBufferingTimeYStutterCriticalPlane = 0;
	double TimeToFinishSwathTransferStutterCriticalPlane = 0;
	unsigned int k, j;

	mode_lib->vba.TotalActiveDPP = 0;
	mode_lib->vba.TotalDCCActiveDPP = 0;
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		mode_lib->vba.TotalActiveDPP = mode_lib->vba.TotalActiveDPP + DPPPerPlane[k];
		if (DCCEnable[k] == true) {
			mode_lib->vba.TotalDCCActiveDPP = mode_lib->vba.TotalDCCActiveDPP + DPPPerPlane[k];
		}
	}

	mode_lib->vba.TotalDataReadBandwidth = 0;
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		mode_lib->vba.TotalDataReadBandwidth = mode_lib->vba.TotalDataReadBandwidth
				+ ReadBandwidthPlaneLuma[k] + ReadBandwidthPlaneChroma[k];
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
		*WritebackUrgentWatermark = WritebackLatency
				+ WritebackChunkSize * 1024.0 / 32.0 / SOCCLK;
	}

	if (mode_lib->vba.TotalActiveWriteback <= 1) {
		*WritebackDRAMClockChangeWatermark = DRAMClockChangeLatency + WritebackLatency;
	} else {
		*WritebackDRAMClockChangeWatermark = DRAMClockChangeLatency + WritebackLatency
				+ WritebackChunkSize * 1024.0 / 32.0 / SOCCLK;
	}

	for (k = 0; k < NumberOfActivePlanes; ++k) {

		mode_lib->vba.LBLatencyHidingSourceLinesY = dml_min((double) MaxLineBufferLines,
			dml_floor(LineBufferSize / LBBitPerPixel[k] / (SwathWidthY[k] / dml_max(HRatio[k], 1.0)), 1))
				- (vtaps[k] - 1);

		mode_lib->vba.LBLatencyHidingSourceLinesC = dml_min((double) MaxLineBufferLines,
			dml_floor(LineBufferSize / LBBitPerPixel[k] / (SwathWidthY[k] / 2 / dml_max(HRatio[k] / 2, 1.0)), 1))
				- (VTAPsChroma[k] - 1);

		EffectiveLBLatencyHidingY = mode_lib->vba.LBLatencyHidingSourceLinesY / VRatio[k]
				* (HTotal[k] / PixelClock[k]);

		EffectiveLBLatencyHidingC = mode_lib->vba.LBLatencyHidingSourceLinesC
				/ (VRatio[k] / 2) * (HTotal[k] / PixelClock[k]);

		if (SwathWidthY[k] > 2 * DPPOutputBufferPixels) {
			DPPOutputBufferLinesY = (double) DPPOutputBufferPixels / SwathWidthY[k];
		} else if (SwathWidthY[k] > DPPOutputBufferPixels) {
			DPPOutputBufferLinesY = 0.5;
		} else {
			DPPOutputBufferLinesY = 1;
		}

		if (SwathWidthY[k] / 2.0 > 2 * DPPOutputBufferPixels) {
			DPPOutputBufferLinesC = (double) DPPOutputBufferPixels
					/ (SwathWidthY[k] / 2.0);
		} else if (SwathWidthY[k] / 2.0 > DPPOutputBufferPixels) {
			DPPOutputBufferLinesC = 0.5;
		} else {
			DPPOutputBufferLinesC = 1;
		}

		CalculateDETBufferSize(
				DETBufferSizeInKByte,
				SwathHeightY[k],
				SwathHeightC[k],
				&DETBufferSizeY,
				&DETBufferSizeC);

		LinesInDETY[k] = DETBufferSizeY / BytePerPixelDETY[k] / SwathWidthY[k];
		LinesInDETYRoundedDownToSwath[k] = dml_floor(LinesInDETY[k], SwathHeightY[k]);
		FullDETBufferingTimeY[k] = LinesInDETYRoundedDownToSwath[k]
				* (HTotal[k] / PixelClock[k]) / VRatio[k];
		if (BytePerPixelDETC[k] > 0) {
			LinesInDETC = DETBufferSizeC / BytePerPixelDETC[k] / (SwathWidthY[k] / 2.0);
			LinesInDETCRoundedDownToSwath = dml_floor(LinesInDETC, SwathHeightC[k]);
			FullDETBufferingTimeC = LinesInDETCRoundedDownToSwath
					* (HTotal[k] / PixelClock[k]) / (VRatio[k] / 2);
		} else {
			LinesInDETC = 0;
			FullDETBufferingTimeC = 999999;
		}

		ActiveDRAMClockChangeLatencyMarginY = HTotal[k] / PixelClock[k]
				* DPPOutputBufferLinesY + EffectiveLBLatencyHidingY
				+ FullDETBufferingTimeY[k] - *DRAMClockChangeWatermark;

		if (NumberOfActivePlanes > 1) {
			ActiveDRAMClockChangeLatencyMarginY = ActiveDRAMClockChangeLatencyMarginY
				- (1 - 1.0 / NumberOfActivePlanes) * SwathHeightY[k] * HTotal[k] / PixelClock[k] / VRatio[k];
		}

		if (BytePerPixelDETC[k] > 0) {
			ActiveDRAMClockChangeLatencyMarginC = HTotal[k] / PixelClock[k]
					* DPPOutputBufferLinesC + EffectiveLBLatencyHidingC
					+ FullDETBufferingTimeC - *DRAMClockChangeWatermark;
			if (NumberOfActivePlanes > 1) {
				ActiveDRAMClockChangeLatencyMarginC = ActiveDRAMClockChangeLatencyMarginC
					- (1 - 1.0 / NumberOfActivePlanes) * SwathHeightC[k] * HTotal[k] / PixelClock[k] / (VRatio[k] / 2);
			}
			mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k] = dml_min(
					ActiveDRAMClockChangeLatencyMarginY,
					ActiveDRAMClockChangeLatencyMarginC);
		} else {
			mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k] = ActiveDRAMClockChangeLatencyMarginY;
		}

		if (WritebackEnable[k] == true) {
			if (WritebackPixelFormat[k] == dm_444_32) {
				WritebackDRAMClockChangeLatencyMargin = (WritebackInterfaceLumaBufferSize
					+ WritebackInterfaceChromaBufferSize) / (WritebackDestinationWidth[k]
					* WritebackDestinationHeight[k] / (WritebackSourceHeight[k] * HTotal[k]
					/ PixelClock[k]) * 4) - *WritebackDRAMClockChangeWatermark;
			} else {
				WritebackDRAMClockChangeLatencyMargin = dml_min(
						WritebackInterfaceLumaBufferSize * 8.0 / 10,
						2 * WritebackInterfaceChromaBufferSize * 8.0 / 10) / (WritebackDestinationWidth[k]
							* WritebackDestinationHeight[k] / (WritebackSourceHeight[k] * HTotal[k] / PixelClock[k]))
						- *WritebackDRAMClockChangeWatermark;
			}
			mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k] = dml_min(
					mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k],
					WritebackDRAMClockChangeLatencyMargin);
		}
	}

	mode_lib->vba.MinActiveDRAMClockChangeMargin = 999999;
	PlaneWithMinActiveDRAMClockChangeMargin = 0;
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k]
				< mode_lib->vba.MinActiveDRAMClockChangeMargin) {
			mode_lib->vba.MinActiveDRAMClockChangeMargin =
					mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k];
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
		if (!((k == PlaneWithMinActiveDRAMClockChangeMargin) && (BlendingAndTiming[k] == k))
				&& !(BlendingAndTiming[k] == PlaneWithMinActiveDRAMClockChangeMargin)
				&& mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k]
						< SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank) {
			SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank =
					mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k];
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
	} else if (((mode_lib->vba.SynchronizedVBlank == true
			|| mode_lib->vba.TotalNumberOfActiveOTG == 1
			|| SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank > 0)
			&& PrefetchMode == 0)) {
		*DRAMClockChangeSupport = dm_dram_clock_change_vblank;
	} else {
		*DRAMClockChangeSupport = dm_dram_clock_change_unsupported;
	}

	FullDETBufferingTimeYStutterCriticalPlane = FullDETBufferingTimeY[0];
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (FullDETBufferingTimeY[k] <= FullDETBufferingTimeYStutterCriticalPlane) {
			TimeToFinishSwathTransferStutterCriticalPlane = (SwathHeightY[k]
					- (LinesInDETY[k] - LinesInDETYRoundedDownToSwath[k]))
					* (HTotal[k] / PixelClock[k]) / VRatio[k];
		}
	}

	*StutterExitWatermark = SRExitTime + mode_lib->vba.LastPixelOfLineExtraWatermark
			+ ExtraLatency + 10 / DCFCLKDeepSleep;
	*StutterEnterPlusExitWatermark = dml_max(
			SREnterPlusExitTime + mode_lib->vba.LastPixelOfLineExtraWatermark
					+ ExtraLatency + 10 / DCFCLKDeepSleep,
			TimeToFinishSwathTransferStutterCriticalPlane);

}

static void CalculateDCFCLKDeepSleep(
		struct display_mode_lib *mode_lib,
		unsigned int NumberOfActivePlanes,
		double BytePerPixelDETY[],
		double BytePerPixelDETC[],
		double VRatio[],
		double SwathWidthY[],
		int DPPPerPlane[],
		double HRatio[],
		double PixelClock[],
		double PSCL_THROUGHPUT[],
		double PSCL_THROUGHPUT_CHROMA[],
		double DPPCLK[],
		double *DCFCLKDeepSleep)
{
	unsigned int k;
	double DisplayPipeLineDeliveryTimeLuma;
	double DisplayPipeLineDeliveryTimeChroma;
	//double   DCFCLKDeepSleepPerPlane[DC__NUM_DPP__MAX];

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (VRatio[k] <= 1) {
			DisplayPipeLineDeliveryTimeLuma = SwathWidthY[k] * DPPPerPlane[k]
					/ HRatio[k] / PixelClock[k];
		} else {
			DisplayPipeLineDeliveryTimeLuma = SwathWidthY[k] / PSCL_THROUGHPUT[k]
					/ DPPCLK[k];
		}
		if (BytePerPixelDETC[k] == 0) {
			DisplayPipeLineDeliveryTimeChroma = 0;
		} else {
			if (VRatio[k] / 2 <= 1) {
				DisplayPipeLineDeliveryTimeChroma = SwathWidthY[k] / 2.0
						* DPPPerPlane[k] / (HRatio[k] / 2) / PixelClock[k];
			} else {
				DisplayPipeLineDeliveryTimeChroma = SwathWidthY[k] / 2.0
						/ PSCL_THROUGHPUT_CHROMA[k] / DPPCLK[k];
			}
		}

		if (BytePerPixelDETC[k] > 0) {
			mode_lib->vba.DCFCLKDeepSleepPerPlane[k] = dml_max(
					1.1 * SwathWidthY[k] * dml_ceil(BytePerPixelDETY[k], 1)
							/ 32.0 / DisplayPipeLineDeliveryTimeLuma,
					1.1 * SwathWidthY[k] / 2.0
							* dml_ceil(BytePerPixelDETC[k], 2) / 32.0
							/ DisplayPipeLineDeliveryTimeChroma);
		} else {
			mode_lib->vba.DCFCLKDeepSleepPerPlane[k] = 1.1 * SwathWidthY[k]
					* dml_ceil(BytePerPixelDETY[k], 1) / 64.0
					/ DisplayPipeLineDeliveryTimeLuma;
		}
		mode_lib->vba.DCFCLKDeepSleepPerPlane[k] = dml_max(
				mode_lib->vba.DCFCLKDeepSleepPerPlane[k],
				PixelClock[k] / 16);

	}

	*DCFCLKDeepSleep = 8;
	for (k = 0; k < NumberOfActivePlanes; ++k) {
		*DCFCLKDeepSleep = dml_max(
				*DCFCLKDeepSleep,
				mode_lib->vba.DCFCLKDeepSleepPerPlane[k]);
	}
}

static void CalculateDETBufferSize(
		double DETBufferSizeInKByte,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		double *DETBufferSizeY,
		double *DETBufferSizeC)
{
	if (SwathHeightC == 0) {
		*DETBufferSizeY = DETBufferSizeInKByte * 1024;
		*DETBufferSizeC = 0;
	} else if (SwathHeightY <= SwathHeightC) {
		*DETBufferSizeY = DETBufferSizeInKByte * 1024 / 2;
		*DETBufferSizeC = DETBufferSizeInKByte * 1024 / 2;
	} else {
		*DETBufferSizeY = DETBufferSizeInKByte * 1024 * 2 / 3;
		*DETBufferSizeC = DETBufferSizeInKByte * 1024 / 3;
	}
}

static void CalculateUrgentBurstFactor(
		unsigned int DETBufferSizeInKByte,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		unsigned int SwathWidthY,
		double LineTime,
		double UrgentLatency,
		double CursorBufferSize,
		unsigned int CursorWidth,
		unsigned int CursorBPP,
		double VRatio,
		double VRatioPreY,
		double VRatioPreC,
		double BytePerPixelInDETY,
		double BytePerPixelInDETC,
		double *UrgentBurstFactorCursor,
		double *UrgentBurstFactorCursorPre,
		double *UrgentBurstFactorLuma,
		double *UrgentBurstFactorLumaPre,
		double *UrgentBurstFactorChroma,
		double *UrgentBurstFactorChromaPre,
		unsigned int *NotEnoughUrgentLatencyHiding,
		unsigned int *NotEnoughUrgentLatencyHidingPre)
{
	double LinesInDETLuma;
	double LinesInDETChroma;
	unsigned int LinesInCursorBuffer;
	double CursorBufferSizeInTime;
	double CursorBufferSizeInTimePre;
	double DETBufferSizeInTimeLuma;
	double DETBufferSizeInTimeLumaPre;
	double DETBufferSizeInTimeChroma;
	double DETBufferSizeInTimeChromaPre;
	double DETBufferSizeY;
	double DETBufferSizeC;

	*NotEnoughUrgentLatencyHiding = 0;
	*NotEnoughUrgentLatencyHidingPre = 0;

	if (CursorWidth > 0) {
		LinesInCursorBuffer = 1 << (unsigned int) dml_floor(
			dml_log2(CursorBufferSize * 1024.0 / (CursorWidth * CursorBPP / 8.0)), 1.0);
		CursorBufferSizeInTime = LinesInCursorBuffer * LineTime / VRatio;
		if (CursorBufferSizeInTime - UrgentLatency <= 0) {
			*NotEnoughUrgentLatencyHiding = 1;
			*UrgentBurstFactorCursor = 0;
		} else {
			*UrgentBurstFactorCursor = CursorBufferSizeInTime
					/ (CursorBufferSizeInTime - UrgentLatency);
		}
		if (VRatioPreY > 0) {
			CursorBufferSizeInTimePre = LinesInCursorBuffer * LineTime / VRatioPreY;
			if (CursorBufferSizeInTimePre - UrgentLatency <= 0) {
				*NotEnoughUrgentLatencyHidingPre = 1;
				*UrgentBurstFactorCursorPre = 0;
			} else {
				*UrgentBurstFactorCursorPre = CursorBufferSizeInTimePre
						/ (CursorBufferSizeInTimePre - UrgentLatency);
			}
		} else {
			*UrgentBurstFactorCursorPre = 1;
		}
	}

	CalculateDETBufferSize(
			DETBufferSizeInKByte,
			SwathHeightY,
			SwathHeightC,
			&DETBufferSizeY,
			&DETBufferSizeC);

	LinesInDETLuma = DETBufferSizeY / BytePerPixelInDETY / SwathWidthY;
	DETBufferSizeInTimeLuma = dml_floor(LinesInDETLuma, SwathHeightY) * LineTime / VRatio;
	if (DETBufferSizeInTimeLuma - UrgentLatency <= 0) {
		*NotEnoughUrgentLatencyHiding = 1;
		*UrgentBurstFactorLuma = 0;
	} else {
		*UrgentBurstFactorLuma = DETBufferSizeInTimeLuma
				/ (DETBufferSizeInTimeLuma - UrgentLatency);
	}
	if (VRatioPreY > 0) {
		DETBufferSizeInTimeLumaPre = dml_floor(LinesInDETLuma, SwathHeightY) * LineTime
				/ VRatioPreY;
		if (DETBufferSizeInTimeLumaPre - UrgentLatency <= 0) {
			*NotEnoughUrgentLatencyHidingPre = 1;
			*UrgentBurstFactorLumaPre = 0;
		} else {
			*UrgentBurstFactorLumaPre = DETBufferSizeInTimeLumaPre
					/ (DETBufferSizeInTimeLumaPre - UrgentLatency);
		}
	} else {
		*UrgentBurstFactorLumaPre = 1;
	}

	if (BytePerPixelInDETC > 0) {
		LinesInDETChroma = DETBufferSizeC / BytePerPixelInDETC / (SwathWidthY / 2);
		DETBufferSizeInTimeChroma = dml_floor(LinesInDETChroma, SwathHeightC) * LineTime
				/ (VRatio / 2);
		if (DETBufferSizeInTimeChroma - UrgentLatency <= 0) {
			*NotEnoughUrgentLatencyHiding = 1;
			*UrgentBurstFactorChroma = 0;
		} else {
			*UrgentBurstFactorChroma = DETBufferSizeInTimeChroma
					/ (DETBufferSizeInTimeChroma - UrgentLatency);
		}
		if (VRatioPreC > 0) {
			DETBufferSizeInTimeChromaPre = dml_floor(LinesInDETChroma, SwathHeightC)
					* LineTime / VRatioPreC;
			if (DETBufferSizeInTimeChromaPre - UrgentLatency <= 0) {
				*NotEnoughUrgentLatencyHidingPre = 1;
				*UrgentBurstFactorChromaPre = 0;
			} else {
				*UrgentBurstFactorChromaPre = DETBufferSizeInTimeChromaPre
						/ (DETBufferSizeInTimeChromaPre - UrgentLatency);
			}
		} else {
			*UrgentBurstFactorChromaPre = 1;
		}
	}
}

static void CalculatePixelDeliveryTimes(
		unsigned int NumberOfActivePlanes,
		double VRatio[],
		double VRatioPrefetchY[],
		double VRatioPrefetchC[],
		unsigned int swath_width_luma_ub[],
		unsigned int swath_width_chroma_ub[],
		int DPPPerPlane[],
		double HRatio[],
		double PixelClock[],
		double PSCL_THROUGHPUT[],
		double PSCL_THROUGHPUT_CHROMA[],
		double DPPCLK[],
		double BytePerPixelDETC[],
		enum scan_direction_class SourceScan[],
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
		double DisplayPipeRequestDeliveryTimeChromaPrefetch[])
{
	double req_per_swath_ub;
	unsigned int k;

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (VRatio[k] <= 1) {
			DisplayPipeLineDeliveryTimeLuma[k] = swath_width_luma_ub[k] * DPPPerPlane[k]
					/ HRatio[k] / PixelClock[k];
		} else {
			DisplayPipeLineDeliveryTimeLuma[k] = swath_width_luma_ub[k]
					/ PSCL_THROUGHPUT[k] / DPPCLK[k];
		}

		if (BytePerPixelDETC[k] == 0) {
			DisplayPipeLineDeliveryTimeChroma[k] = 0;
		} else {
			if (VRatio[k] / 2 <= 1) {
				DisplayPipeLineDeliveryTimeChroma[k] = swath_width_chroma_ub[k]
						* DPPPerPlane[k] / (HRatio[k] / 2) / PixelClock[k];
			} else {
				DisplayPipeLineDeliveryTimeChroma[k] = swath_width_chroma_ub[k]
						/ PSCL_THROUGHPUT_CHROMA[k] / DPPCLK[k];
			}
		}

		if (VRatioPrefetchY[k] <= 1) {
			DisplayPipeLineDeliveryTimeLumaPrefetch[k] = swath_width_luma_ub[k]
					* DPPPerPlane[k] / HRatio[k] / PixelClock[k];
		} else {
			DisplayPipeLineDeliveryTimeLumaPrefetch[k] = swath_width_luma_ub[k]
					/ PSCL_THROUGHPUT[k] / DPPCLK[k];
		}

		if (BytePerPixelDETC[k] == 0) {
			DisplayPipeLineDeliveryTimeChromaPrefetch[k] = 0;
		} else {
			if (VRatioPrefetchC[k] <= 1) {
				DisplayPipeLineDeliveryTimeChromaPrefetch[k] =
						swath_width_chroma_ub[k] * DPPPerPlane[k]
								/ (HRatio[k] / 2) / PixelClock[k];
			} else {
				DisplayPipeLineDeliveryTimeChromaPrefetch[k] =
						swath_width_chroma_ub[k] / PSCL_THROUGHPUT_CHROMA[k] / DPPCLK[k];
			}
		}
	}

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (SourceScan[k] == dm_horz) {
			req_per_swath_ub = swath_width_luma_ub[k] / BlockWidth256BytesY[k];
		} else {
			req_per_swath_ub = swath_width_luma_ub[k] / BlockHeight256BytesY[k];
		}
		DisplayPipeRequestDeliveryTimeLuma[k] = DisplayPipeLineDeliveryTimeLuma[k]
				/ req_per_swath_ub;
		DisplayPipeRequestDeliveryTimeLumaPrefetch[k] =
				DisplayPipeLineDeliveryTimeLumaPrefetch[k] / req_per_swath_ub;
		if (BytePerPixelDETC[k] == 0) {
			DisplayPipeRequestDeliveryTimeChroma[k] = 0;
			DisplayPipeRequestDeliveryTimeChromaPrefetch[k] = 0;
		} else {
			if (SourceScan[k] == dm_horz) {
				req_per_swath_ub = swath_width_chroma_ub[k]
						/ BlockWidth256BytesC[k];
			} else {
				req_per_swath_ub = swath_width_chroma_ub[k]
						/ BlockHeight256BytesC[k];
			}
			DisplayPipeRequestDeliveryTimeChroma[k] =
					DisplayPipeLineDeliveryTimeChroma[k] / req_per_swath_ub;
			DisplayPipeRequestDeliveryTimeChromaPrefetch[k] =
					DisplayPipeLineDeliveryTimeChromaPrefetch[k] / req_per_swath_ub;
		}
	}
}

static void CalculateMetaAndPTETimes(
		unsigned int NumberOfActivePlanes,
		bool GPUVMEnable,
		unsigned int MetaChunkSize,
		unsigned int MinMetaChunkSizeBytes,
		unsigned int GPUVMMaxPageTableLevels,
		unsigned int HTotal[],
		double VRatio[],
		double VRatioPrefetchY[],
		double VRatioPrefetchC[],
		double DestinationLinesToRequestRowInVBlank[],
		double DestinationLinesToRequestRowInImmediateFlip[],
		double DestinationLinesToRequestVMInVBlank[],
		double DestinationLinesToRequestVMInImmediateFlip[],
		bool DCCEnable[],
		double PixelClock[],
		double BytePerPixelDETY[],
		double BytePerPixelDETC[],
		enum scan_direction_class SourceScan[],
		unsigned int dpte_row_height[],
		unsigned int dpte_row_height_chroma[],
		unsigned int meta_row_width[],
		unsigned int meta_row_height[],
		unsigned int meta_req_width[],
		unsigned int meta_req_height[],
		int dpte_group_bytes[],
		unsigned int PTERequestSizeY[],
		unsigned int PTERequestSizeC[],
		unsigned int PixelPTEReqWidthY[],
		unsigned int PixelPTEReqHeightY[],
		unsigned int PixelPTEReqWidthC[],
		unsigned int PixelPTEReqHeightC[],
		unsigned int dpte_row_width_luma_ub[],
		unsigned int dpte_row_width_chroma_ub[],
		unsigned int vm_group_bytes[],
		unsigned int dpde0_bytes_per_frame_ub_l[],
		unsigned int dpde0_bytes_per_frame_ub_c[],
		unsigned int meta_pte_bytes_per_frame_ub_l[],
		unsigned int meta_pte_bytes_per_frame_ub_c[],
		double DST_Y_PER_PTE_ROW_NOM_L[],
		double DST_Y_PER_PTE_ROW_NOM_C[],
		double DST_Y_PER_META_ROW_NOM_L[],
		double TimePerMetaChunkNominal[],
		double TimePerMetaChunkVBlank[],
		double TimePerMetaChunkFlip[],
		double time_per_pte_group_nom_luma[],
		double time_per_pte_group_vblank_luma[],
		double time_per_pte_group_flip_luma[],
		double time_per_pte_group_nom_chroma[],
		double time_per_pte_group_vblank_chroma[],
		double time_per_pte_group_flip_chroma[],
		double TimePerVMGroupVBlank[],
		double TimePerVMGroupFlip[],
		double TimePerVMRequestVBlank[],
		double TimePerVMRequestFlip[])
{
	unsigned int meta_chunk_width;
	unsigned int min_meta_chunk_width;
	unsigned int meta_chunk_per_row_int;
	unsigned int meta_row_remainder;
	unsigned int meta_chunk_threshold;
	unsigned int meta_chunks_per_row_ub;
	unsigned int dpte_group_width_luma;
	unsigned int dpte_group_width_chroma;
	unsigned int dpte_groups_per_row_luma_ub;
	unsigned int dpte_groups_per_row_chroma_ub;
	unsigned int num_group_per_lower_vm_stage;
	unsigned int num_req_per_lower_vm_stage;
	unsigned int k;

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (GPUVMEnable == true) {
			DST_Y_PER_PTE_ROW_NOM_L[k] = dpte_row_height[k] / VRatio[k];
			if (BytePerPixelDETC[k] == 0) {
				DST_Y_PER_PTE_ROW_NOM_C[k] = 0;
			} else {
				DST_Y_PER_PTE_ROW_NOM_C[k] = dpte_row_height_chroma[k] / (VRatio[k] / 2);
			}
		} else {
			DST_Y_PER_PTE_ROW_NOM_L[k] = 0;
			DST_Y_PER_PTE_ROW_NOM_C[k] = 0;
		}
		if (DCCEnable[k] == true) {
			DST_Y_PER_META_ROW_NOM_L[k] = meta_row_height[k] / VRatio[k];
		} else {
			DST_Y_PER_META_ROW_NOM_L[k] = 0;
		}
	}

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (DCCEnable[k] == true) {
			meta_chunk_width = MetaChunkSize * 1024 * 256
					/ dml_ceil(BytePerPixelDETY[k], 1) / meta_row_height[k];
			min_meta_chunk_width = MinMetaChunkSizeBytes * 256
					/ dml_ceil(BytePerPixelDETY[k], 1) / meta_row_height[k];
			meta_chunk_per_row_int = meta_row_width[k] / meta_chunk_width;
			meta_row_remainder = meta_row_width[k] % meta_chunk_width;
			if (SourceScan[k] == dm_horz) {
				meta_chunk_threshold = 2 * min_meta_chunk_width - meta_req_width[k];
			} else {
				meta_chunk_threshold = 2 * min_meta_chunk_width
						- meta_req_height[k];
			}
			if (meta_row_remainder <= meta_chunk_threshold) {
				meta_chunks_per_row_ub = meta_chunk_per_row_int + 1;
			} else {
				meta_chunks_per_row_ub = meta_chunk_per_row_int + 2;
			}
			TimePerMetaChunkNominal[k] = meta_row_height[k] / VRatio[k] * HTotal[k]
					/ PixelClock[k] / meta_chunks_per_row_ub;
			TimePerMetaChunkVBlank[k] = DestinationLinesToRequestRowInVBlank[k]
					* HTotal[k] / PixelClock[k] / meta_chunks_per_row_ub;
			TimePerMetaChunkFlip[k] = DestinationLinesToRequestRowInImmediateFlip[k]
					* HTotal[k] / PixelClock[k] / meta_chunks_per_row_ub;
		} else {
			TimePerMetaChunkNominal[k] = 0;
			TimePerMetaChunkVBlank[k] = 0;
			TimePerMetaChunkFlip[k] = 0;
		}
	}

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (GPUVMEnable == true) {
			if (SourceScan[k] == dm_horz) {
				dpte_group_width_luma = dpte_group_bytes[k] / PTERequestSizeY[k]
						* PixelPTEReqWidthY[k];
			} else {
				dpte_group_width_luma = dpte_group_bytes[k] / PTERequestSizeY[k]
						* PixelPTEReqHeightY[k];
			}
			dpte_groups_per_row_luma_ub = dml_ceil(
					(float) dpte_row_width_luma_ub[k] / dpte_group_width_luma,
					1);
			time_per_pte_group_nom_luma[k] = DST_Y_PER_PTE_ROW_NOM_L[k] * HTotal[k]
					/ PixelClock[k] / dpte_groups_per_row_luma_ub;
			time_per_pte_group_vblank_luma[k] = DestinationLinesToRequestRowInVBlank[k]
					* HTotal[k] / PixelClock[k] / dpte_groups_per_row_luma_ub;
			time_per_pte_group_flip_luma[k] =
					DestinationLinesToRequestRowInImmediateFlip[k] * HTotal[k]
							/ PixelClock[k]
							/ dpte_groups_per_row_luma_ub;
			if (BytePerPixelDETC[k] == 0) {
				time_per_pte_group_nom_chroma[k] = 0;
				time_per_pte_group_vblank_chroma[k] = 0;
				time_per_pte_group_flip_chroma[k] = 0;
			} else {
				if (SourceScan[k] == dm_horz) {
					dpte_group_width_chroma = dpte_group_bytes[k]
							/ PTERequestSizeC[k] * PixelPTEReqWidthC[k];
				} else {
					dpte_group_width_chroma = dpte_group_bytes[k]
							/ PTERequestSizeC[k]
							* PixelPTEReqHeightC[k];
				}
				dpte_groups_per_row_chroma_ub = dml_ceil(
						(float) dpte_row_width_chroma_ub[k]
								/ dpte_group_width_chroma,
						1);
				time_per_pte_group_nom_chroma[k] = DST_Y_PER_PTE_ROW_NOM_C[k]
						* HTotal[k] / PixelClock[k]
						/ dpte_groups_per_row_chroma_ub;
				time_per_pte_group_vblank_chroma[k] =
						DestinationLinesToRequestRowInVBlank[k] * HTotal[k]
								/ PixelClock[k]
								/ dpte_groups_per_row_chroma_ub;
				time_per_pte_group_flip_chroma[k] =
						DestinationLinesToRequestRowInImmediateFlip[k]
								* HTotal[k] / PixelClock[k]
								/ dpte_groups_per_row_chroma_ub;
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

	for (k = 0; k < NumberOfActivePlanes; ++k) {
		if (GPUVMEnable == true && (DCCEnable[k] == true || GPUVMMaxPageTableLevels > 1)) {
			if (DCCEnable[k] == false) {
				if (BytePerPixelDETC[k] > 0) {
					num_group_per_lower_vm_stage =
						dml_ceil((double) (dpde0_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1)
						+ dml_ceil((double) (dpde0_bytes_per_frame_ub_c[k]) / (double) (vm_group_bytes[k]), 1);
				} else {
					num_group_per_lower_vm_stage =
							dml_ceil((double) (dpde0_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1);
				}
			} else {
				if (GPUVMMaxPageTableLevels == 1) {
					if (BytePerPixelDETC[k] > 0) {
						num_group_per_lower_vm_stage =
							dml_ceil((double) (meta_pte_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1)
							+ dml_ceil((double) (meta_pte_bytes_per_frame_ub_c[k]) / (double) (vm_group_bytes[k]), 1);
					} else {
						num_group_per_lower_vm_stage =
							dml_ceil((double) (meta_pte_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1);
					}
				} else {
					if (BytePerPixelDETC[k] > 0) {
						num_group_per_lower_vm_stage =
							dml_ceil((double) (dpde0_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1)
							+ dml_ceil((double) (dpde0_bytes_per_frame_ub_c[k]) / (double) (vm_group_bytes[k]), 1)
							+ dml_ceil((double) (meta_pte_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1)
							+ dml_ceil((double) (meta_pte_bytes_per_frame_ub_c[k]) / (double) (vm_group_bytes[k]), 1);
					} else {
						num_group_per_lower_vm_stage =
							dml_ceil((double) (dpde0_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1)
							+ dml_ceil((double) (meta_pte_bytes_per_frame_ub_l[k]) / (double) (vm_group_bytes[k]), 1);
					}
				}
			}

			if (DCCEnable[k] == false) {
				if (BytePerPixelDETC[k] > 0) {
					num_req_per_lower_vm_stage = dpde0_bytes_per_frame_ub_l[k]
							/ 64 + dpde0_bytes_per_frame_ub_c[k] / 64;
				} else {
					num_req_per_lower_vm_stage = dpde0_bytes_per_frame_ub_l[k]
							/ 64;
				}
			} else {
				if (GPUVMMaxPageTableLevels == 1) {
					if (BytePerPixelDETC[k] > 0) {
						num_req_per_lower_vm_stage = meta_pte_bytes_per_frame_ub_l[k] / 64
							+ meta_pte_bytes_per_frame_ub_c[k] / 64;
					} else {
						num_req_per_lower_vm_stage = meta_pte_bytes_per_frame_ub_l[k] / 64;
					}
				} else {
					if (BytePerPixelDETC[k] > 0) {
						num_req_per_lower_vm_stage = dpde0_bytes_per_frame_ub_l[k] / 64
							+ dpde0_bytes_per_frame_ub_c[k] / 64
							+ meta_pte_bytes_per_frame_ub_l[k] / 64
							+ meta_pte_bytes_per_frame_ub_c[k] / 64;
					} else {
						num_req_per_lower_vm_stage = dpde0_bytes_per_frame_ub_l[k] / 64
							+ meta_pte_bytes_per_frame_ub_l[k] / 64;
					}
				}
			}

			TimePerVMGroupVBlank[k] = DestinationLinesToRequestVMInVBlank[k] * HTotal[k]
					/ PixelClock[k] / num_group_per_lower_vm_stage;
			TimePerVMGroupFlip[k] = DestinationLinesToRequestVMInImmediateFlip[k]
					* HTotal[k] / PixelClock[k] / num_group_per_lower_vm_stage;
			TimePerVMRequestVBlank[k] = DestinationLinesToRequestVMInVBlank[k]
					* HTotal[k] / PixelClock[k] / num_req_per_lower_vm_stage;
			TimePerVMRequestFlip[k] = DestinationLinesToRequestVMInImmediateFlip[k]
					* HTotal[k] / PixelClock[k] / num_req_per_lower_vm_stage;

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

static double CalculateExtraLatency(
		double UrgentRoundTripAndOutOfOrderLatency,
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
		int HostVMMaxPageTableLevels,
		int HostVMCachedPageTableLevels)
{
	double CalculateExtraLatency;
	double HostVMInefficiencyFactor;
	int HostVMDynamicLevels;

	if (GPUVMEnable && HostVMEnable) {
		HostVMInefficiencyFactor =
				PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelMixedWithVMData
						/ PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyVMDataOnly;
		HostVMDynamicLevels = HostVMMaxPageTableLevels - HostVMCachedPageTableLevels;
	} else {
		HostVMInefficiencyFactor = 1;
		HostVMDynamicLevels = 0;
	}

	CalculateExtraLatency = UrgentRoundTripAndOutOfOrderLatency
			+ (TotalNumberOfActiveDPP * PixelChunkSizeInKByte
					+ TotalNumberOfDCCActiveDPP * MetaChunkSize) * 1024.0
					/ ReturnBW;

	if (GPUVMEnable) {
		int k;

		for (k = 0; k < NumberOfActivePlanes; k++) {
			CalculateExtraLatency = CalculateExtraLatency
					+ NumberOfDPP[k] * dpte_group_bytes[k]
							* (1 + 8 * HostVMDynamicLevels)
							* HostVMInefficiencyFactor / ReturnBW;
		}
	}
	return CalculateExtraLatency;
}

