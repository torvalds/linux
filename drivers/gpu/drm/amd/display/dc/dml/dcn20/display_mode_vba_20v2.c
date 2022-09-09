/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
#include "display_mode_vba_20v2.h"
#include "../dml_inline_defs.h"

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
#define DCN20_MAX_DSC_IMAGE_WIDTH 5184
#define DCN20_MAX_420_IMAGE_WIDTH 4096

static double adjust_ReturnBW(
		struct display_mode_lib *mode_lib,
		double ReturnBW,
		bool DCCEnabledAnyPlane,
		double ReturnBandwidthToDCN);
static unsigned int dscceComputeDelay(
		unsigned int bpc,
		double bpp,
		unsigned int sliceWidth,
		unsigned int numSlices,
		enum output_format_class pixelFormat);
static unsigned int dscComputeDelay(enum output_format_class pixelFormat);
static bool CalculateDelayAfterScaler(
		struct display_mode_lib *mode_lib,
		double ReturnBW,
		double ReadBandwidthPlaneLuma,
		double ReadBandwidthPlaneChroma,
		double TotalDataReadBandwidth,
		double DisplayPipeLineDeliveryTimeLuma,
		double DisplayPipeLineDeliveryTimeChroma,
		double DPPCLK,
		double DISPCLK,
		double PixelClock,
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
		unsigned int HTotal,
		unsigned int SwathWidthSingleDPPY,
		double BytePerPixelDETY,
		double BytePerPixelDETC,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		bool Interlace,
		bool ProgressiveToInterlaceUnitInOPP,
		double *DSTXAfterScaler,
		double *DSTYAfterScaler
		);
// Super monster function with some 45 argument
static bool CalculatePrefetchSchedule(
		struct display_mode_lib *mode_lib,
		double DPPCLK,
		double DISPCLK,
		double PixelClock,
		double DCFCLKDeepSleep,
		unsigned int DPPPerPlane,
		unsigned int NumberOfCursors,
		unsigned int VBlank,
		unsigned int HTotal,
		unsigned int MaxInterDCNTileRepeaters,
		unsigned int VStartup,
		unsigned int PageTableLevels,
		bool GPUVMEnable,
		bool DynamicMetadataEnable,
		unsigned int DynamicMetadataLinesBeforeActiveRequired,
		unsigned int DynamicMetadataTransmittedBytes,
		bool DCCEnable,
		double UrgentLatencyPixelDataOnly,
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
		double DSTXAfterScaler,
		double DSTYAfterScaler,
		double *DestinationLinesForPrefetch,
		double *PrefetchBandwidth,
		double *DestinationLinesToRequestVMInVBlank,
		double *DestinationLinesToRequestRowInVBlank,
		double *VRatioPrefetchY,
		double *VRatioPrefetchC,
		double *RequiredPrefetchPixDataBW,
		double *Tno_bw,
		unsigned int *VUpdateOffsetPix,
		double *VUpdateWidthPix,
		double *VReadyOffsetPix);
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
		bool GPUVMEnable,
		unsigned int VMMPageSize,
		unsigned int PTEBufferSizeInRequestsLuma,
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
		double UrgentLatencyPixelDataOnly,
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
		double *dpte_row_bw,
		double *qual_row_bw);
static void CalculateFlipSchedule(
		struct display_mode_lib *mode_lib,
		double UrgentExtraLatency,
		double UrgentLatencyPixelDataOnly,
		unsigned int GPUVMMaxPageTableLevels,
		bool GPUVMEnable,
		double BandwidthAvailableForImmediateFlip,
		unsigned int TotImmediateFlipBytes,
		enum source_format_class SourcePixelFormat,
		unsigned int ImmediateFlipBytes,
		double LineTime,
		double VRatio,
		double Tno_bw,
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

static void dml20v2_DisplayPipeConfiguration(struct display_mode_lib *mode_lib);
static void dml20v2_DISPCLKDPPCLKDCFCLKDeepSleepPrefetchParametersWatermarksAndPerformanceCalculation(
		struct display_mode_lib *mode_lib);

void dml20v2_recalculate(struct display_mode_lib *mode_lib)
{
	ModeSupportAndSystemConfiguration(mode_lib);
	mode_lib->vba.FabricAndDRAMBandwidth = dml_min(
		mode_lib->vba.DRAMSpeed * mode_lib->vba.NumberOfChannels * mode_lib->vba.DRAMChannelWidth,
		mode_lib->vba.FabricClock * mode_lib->vba.FabricDatapathToDCNDataReturn) / 1000.0;
	PixelClockAdjustmentForProgressiveToInterlaceUnit(mode_lib);
	dml20v2_DisplayPipeConfiguration(mode_lib);
	dml20v2_DISPCLKDPPCLKDCFCLKDeepSleepPrefetchParametersWatermarksAndPerformanceCalculation(mode_lib);
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
										- mode_lib->vba.UrgentLatencyPixelDataOnly
												/ ((mode_lib->vba.ROBBufferSizeInKByte
														- mode_lib->vba.PixelChunkSizeInKByte)
														* 1024
														/ ReturnBandwidthToDCN
														- mode_lib->vba.DCFCLK
																* mode_lib->vba.ReturnBusWidth
																/ 4)
										+ mode_lib->vba.UrgentLatencyPixelDataOnly));

	CriticalCompression = 2.0 * mode_lib->vba.ReturnBusWidth * mode_lib->vba.DCFCLK
			* mode_lib->vba.UrgentLatencyPixelDataOnly
			/ (ReturnBandwidthToDCN * mode_lib->vba.UrgentLatencyPixelDataOnly
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
								* mode_lib->vba.UrgentLatencyPixelDataOnly
								/ dml_pow(
										(ReturnBandwidthToDCN
												* mode_lib->vba.UrgentLatencyPixelDataOnly
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

static bool CalculateDelayAfterScaler(
		struct display_mode_lib *mode_lib,
		double ReturnBW,
		double ReadBandwidthPlaneLuma,
		double ReadBandwidthPlaneChroma,
		double TotalDataReadBandwidth,
		double DisplayPipeLineDeliveryTimeLuma,
		double DisplayPipeLineDeliveryTimeChroma,
		double DPPCLK,
		double DISPCLK,
		double PixelClock,
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
		unsigned int HTotal,
		unsigned int SwathWidthSingleDPPY,
		double BytePerPixelDETY,
		double BytePerPixelDETC,
		unsigned int SwathHeightY,
		unsigned int SwathHeightC,
		bool Interlace,
		bool ProgressiveToInterlaceUnitInOPP,
		double *DSTXAfterScaler,
		double *DSTYAfterScaler
		)
{
	unsigned int DPPCycles, DISPCLKCycles;
	double DataFabricLineDeliveryTimeLuma;
	double DataFabricLineDeliveryTimeChroma;
	double DSTTotalPixelsAfterScaler;

	DataFabricLineDeliveryTimeLuma = SwathWidthSingleDPPY * SwathHeightY * dml_ceil(BytePerPixelDETY, 1) / (mode_lib->vba.ReturnBW * ReadBandwidthPlaneLuma / TotalDataReadBandwidth);
	mode_lib->vba.LastPixelOfLineExtraWatermark = dml_max(mode_lib->vba.LastPixelOfLineExtraWatermark, DataFabricLineDeliveryTimeLuma - DisplayPipeLineDeliveryTimeLuma);

	if (BytePerPixelDETC != 0) {
		DataFabricLineDeliveryTimeChroma = SwathWidthSingleDPPY / 2 * SwathHeightC * dml_ceil(BytePerPixelDETC, 2) / (mode_lib->vba.ReturnBW * ReadBandwidthPlaneChroma / TotalDataReadBandwidth);
		mode_lib->vba.LastPixelOfLineExtraWatermark = dml_max(mode_lib->vba.LastPixelOfLineExtraWatermark, DataFabricLineDeliveryTimeChroma - DisplayPipeLineDeliveryTimeChroma);
	}

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

	if (OutputFormat == dm_420 || (Interlace && ProgressiveToInterlaceUnitInOPP))
		*DSTYAfterScaler = 1;
	else
		*DSTYAfterScaler = 0;

	DSTTotalPixelsAfterScaler = ((double) (*DSTYAfterScaler * HTotal)) + *DSTXAfterScaler;
	*DSTYAfterScaler = dml_floor(DSTTotalPixelsAfterScaler / HTotal, 1);
	*DSTXAfterScaler = DSTTotalPixelsAfterScaler - ((double) (*DSTYAfterScaler * HTotal));

	return true;
}

static bool CalculatePrefetchSchedule(
		struct display_mode_lib *mode_lib,
		double DPPCLK,
		double DISPCLK,
		double PixelClock,
		double DCFCLKDeepSleep,
		unsigned int DPPPerPlane,
		unsigned int NumberOfCursors,
		unsigned int VBlank,
		unsigned int HTotal,
		unsigned int MaxInterDCNTileRepeaters,
		unsigned int VStartup,
		unsigned int PageTableLevels,
		bool GPUVMEnable,
		bool DynamicMetadataEnable,
		unsigned int DynamicMetadataLinesBeforeActiveRequired,
		unsigned int DynamicMetadataTransmittedBytes,
		bool DCCEnable,
		double UrgentLatencyPixelDataOnly,
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
		double DSTXAfterScaler,
		double DSTYAfterScaler,
		double *DestinationLinesForPrefetch,
		double *PrefetchBandwidth,
		double *DestinationLinesToRequestVMInVBlank,
		double *DestinationLinesToRequestRowInVBlank,
		double *VRatioPrefetchY,
		double *VRatioPrefetchC,
		double *RequiredPrefetchPixDataBW,
		double *Tno_bw,
		unsigned int *VUpdateOffsetPix,
		double *VUpdateWidthPix,
		double *VReadyOffsetPix)
{
	bool MyError = false;
	double TotalRepeaterDelayTime;
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

	*VUpdateOffsetPix = dml_ceil(HTotal / 4.0, 1);
	TotalRepeaterDelayTime = MaxInterDCNTileRepeaters * (2.0 / DPPCLK + 3.0 / DISPCLK);
	*VUpdateWidthPix = (14.0 / DCFCLKDeepSleep + 12.0 / DPPCLK + TotalRepeaterDelayTime)
			* PixelClock;

	*VReadyOffsetPix = dml_max(
			150.0 / DPPCLK,
			TotalRepeaterDelayTime + 20.0 / DCFCLKDeepSleep + 10.0 / DPPCLK)
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
		}
	} else
		Tdm = 0;

	if (GPUVMEnable) {
		if (PageTableLevels == 4)
			*Tno_bw = UrgentExtraLatency + UrgentLatencyPixelDataOnly;
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
			- (DSTYAfterScaler + DSTXAfterScaler / HTotal);

	Tsw_oto = dml_max(PrefetchSourceLinesY, PrefetchSourceLinesC) * LineTime;

	prefetch_bw_oto = (MetaRowByte + PixelPTEBytesPerRow
			+ PrefetchSourceLinesY * SwathWidthY * dml_ceil(BytePerPixelDETY, 1)
			+ PrefetchSourceLinesC * SwathWidthY / 2 * dml_ceil(BytePerPixelDETC, 2))
			/ Tsw_oto;

	if (GPUVMEnable == true) {
		Tvm_oto =
				dml_max(
						*Tno_bw + PDEAndMetaPTEBytesFrame / prefetch_bw_oto,
						dml_max(
								UrgentExtraLatency
										+ UrgentLatencyPixelDataOnly
												* (PageTableLevels
														- 1),
								LineTime / 4.0));
	} else
		Tvm_oto = LineTime / 4.0;

	if ((GPUVMEnable == true || DCCEnable == true)) {
		Tr0_oto = dml_max(
				(MetaRowByte + PixelPTEBytesPerRow) / prefetch_bw_oto,
				dml_max(UrgentLatencyPixelDataOnly, dml_max(LineTime - Tvm_oto, LineTime / 4)));
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
	dml_print("DML: DSTYAfterScaler: %f\n", DSTYAfterScaler);
	dml_print("DML: DSTXAfterScaler: %f\n", DSTXAfterScaler);
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
		if (GPUVMEnable) {
			TimeForFetchingMetaPTE =
					dml_max(
							*Tno_bw
									+ (double) PDEAndMetaPTEBytesFrame
											/ *PrefetchBandwidth,
							dml_max(
									UrgentExtraLatency
											+ UrgentLatencyPixelDataOnly
													* (PageTableLevels
															- 1),
									LineTime / 4));
		} else {
			if (NumberOfCursors > 0 || XFCEnabled)
				TimeForFetchingMetaPTE = LineTime / 4;
			else
				TimeForFetchingMetaPTE = 0.0;
		}

		if ((GPUVMEnable == true || DCCEnable == true)) {
			TimeForFetchingRowInVBlank =
					dml_max(
							(MetaRowByte + PixelPTEBytesPerRow)
									/ *PrefetchBandwidth,
							dml_max(
									UrgentLatencyPixelDataOnly,
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
						- ((NumberOfCursors > 0 || GPUVMEnable
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
		bool GPUVMEnable,
		unsigned int VMMPageSize,
		unsigned int PTEBufferSizeInRequestsLuma,
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
		if (GPUVMEnable == true) {
			MetaPTEBytesFrame = (dml_ceil(
					(double) (DCCMetaSurfaceBytes - VMMPageSize)
							/ (8 * VMMPageSize),
					1) + 1) * 64;
			MPDEBytesFrame = 128 * (mode_lib->vba.GPUVMMaxPageTableLevels - 1);
		} else {
			MetaPTEBytesFrame = 0;
			MPDEBytesFrame = 0;
		}
	} else {
		MetaPTEBytesFrame = 0;
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

	if (GPUVMEnable == true && mode_lib->vba.GPUVMMaxPageTableLevels > 1) {
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
		ExtraDPDEBytesFrame = 128 * (mode_lib->vba.GPUVMMaxPageTableLevels - 2);
	} else {
		DPDE0BytesFrame = 0;
		ExtraDPDEBytesFrame = 0;
	}

	PDEAndMetaPTEBytesFrame = MetaPTEBytesFrame + MPDEBytesFrame + DPDE0BytesFrame
			+ ExtraDPDEBytesFrame;

	if (GPUVMEnable == true) {
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
															(double) PTEBufferSizeInRequestsLuma
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
				<= 64 * PTEBufferSizeInRequestsLuma) {
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

static void dml20v2_DISPCLKDPPCLKDCFCLKDeepSleepPrefetchParametersWatermarksAndPerformanceCalculation(
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
			mode_lib->vba.soc.clock_limits[mode_lib->vba.soc.num_states].dispclk_mhz,
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
		if (mode_lib->vba.DPPPerPlane[k] == 0) {
			mode_lib->vba.DPPCLK_calculated[k] = 0;
		} else {
			mode_lib->vba.DPPCLK_calculated[k] = mode_lib->vba.DPPCLKUsingSingleDPP[k]
					/ mode_lib->vba.DPPPerPlane[k]
					* (1 + mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100);
		}
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
			* mode_lib->vba.PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelDataOnly / 100;

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

		if (mode_lib->vba.ODMCombineEnabled[k] == dm_odm_combine_mode_2to1)
			MainPlaneDoesODMCombine = true;
		for (j = 0; j < mode_lib->vba.NumberOfActivePlanes; ++j)
			if (mode_lib->vba.BlendingAndTiming[k] == j
					&& mode_lib->vba.ODMCombineEnabled[k] == dm_odm_combine_mode_2to1)
				MainPlaneDoesODMCombine = true;

		if (MainPlaneDoesODMCombine == true)
			mode_lib->vba.SwathWidthY[k] = dml_min(
					(double) mode_lib->vba.SwathWidthSingleDPPY[k],
					dml_round(
							mode_lib->vba.HActive[k] / 2.0
									* mode_lib->vba.HRatio[k]));
		else {
			if (mode_lib->vba.DPPPerPlane[k] == 0) {
				mode_lib->vba.SwathWidthY[k] = 0;
			} else {
				mode_lib->vba.SwathWidthY[k] = mode_lib->vba.SwathWidthSingleDPPY[k]
						/ mode_lib->vba.DPPPerPlane[k];
			}
		}
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
					+ mode_lib->vba.UrgentOutOfOrderReturnPerChannelPixelDataOnly
							* mode_lib->vba.NumberOfChannels
							/ mode_lib->vba.ReturnBW;

	mode_lib->vba.LastPixelOfLineExtraWatermark = 0;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
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
		}

	mode_lib->vba.UrgentExtraLatency = mode_lib->vba.UrgentRoundTripAndOutOfOrderLatency
			+ (mode_lib->vba.TotalActiveDPP * mode_lib->vba.PixelChunkSizeInKByte
					+ mode_lib->vba.TotalDCCActiveDPP
							* mode_lib->vba.MetaChunkSize) * 1024.0
					/ mode_lib->vba.ReturnBW;

	if (mode_lib->vba.GPUVMEnable)
		mode_lib->vba.UrgentExtraLatency += mode_lib->vba.TotalActiveDPP
				* mode_lib->vba.PTEGroupSize / mode_lib->vba.ReturnBW;

	mode_lib->vba.UrgentWatermark = mode_lib->vba.UrgentLatencyPixelDataOnly
			+ mode_lib->vba.LastPixelOfLineExtraWatermark
			+ mode_lib->vba.UrgentExtraLatency;

	DTRACE("   urgent_extra_latency = %fus", mode_lib->vba.UrgentExtraLatency);
	DTRACE("   wm_urgent = %fus", mode_lib->vba.UrgentWatermark);

	mode_lib->vba.UrgentLatency = mode_lib->vba.UrgentLatencyPixelDataOnly;

	mode_lib->vba.TotalActiveWriteback = 0;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.WritebackEnable[k])
			mode_lib->vba.TotalActiveWriteback = mode_lib->vba.TotalActiveWriteback + mode_lib->vba.ActiveWritebacksPerPlane[k];
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
		if (mode_lib->vba.GPUVMEnable) {
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
	mode_lib->vba.DCFCLKDeepSleep = 8.0;

	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; k++) {
		if (mode_lib->vba.BytePerPixelDETC[k] > 0) {
			mode_lib->vba.DCFCLKDeepSleepPerPlane[k] =
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
			mode_lib->vba.DCFCLKDeepSleepPerPlane[k] = 1.1 * mode_lib->vba.SwathWidthY[k]
					* dml_ceil(mode_lib->vba.BytePerPixelDETY[k], 1) / 64.0
					/ mode_lib->vba.DisplayPipeLineDeliveryTimeLuma[k];
		mode_lib->vba.DCFCLKDeepSleepPerPlane[k] = dml_max(
				mode_lib->vba.DCFCLKDeepSleepPerPlane[k],
				mode_lib->vba.PixelClock[k] / 16.0);
		mode_lib->vba.DCFCLKDeepSleep = dml_max(
				mode_lib->vba.DCFCLKDeepSleep,
				mode_lib->vba.DCFCLKDeepSleepPerPlane[k]);

		DTRACE(
				"   dcfclk_deepsleep_per_plane[%i] = %fMHz",
				k,
				mode_lib->vba.DCFCLKDeepSleepPerPlane[k]);
	}

	DTRACE("   dcfclk_deepsleep_mhz = %fMHz", mode_lib->vba.DCFCLKDeepSleep);

	// Stutter Watermark
	mode_lib->vba.StutterExitWatermark = mode_lib->vba.SRExitTime
			+ mode_lib->vba.LastPixelOfLineExtraWatermark
			+ mode_lib->vba.UrgentExtraLatency + 10 / mode_lib->vba.DCFCLKDeepSleep;
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
				mode_lib->vba.GPUVMEnable,
				mode_lib->vba.VMMPageSize,
				mode_lib->vba.PTEBufferSizeInRequestsLuma,
				mode_lib->vba.PDEProcessingBufIn64KBReqs,
				mode_lib->vba.PitchY[k],
				mode_lib->vba.DCCMetaPitchY[k],
				&mode_lib->vba.MacroTileWidthY[k],
				&MetaRowByteY,
				&PixelPTEBytesPerRowY,
				&mode_lib->vba.PTEBufferSizeNotExceeded[mode_lib->vba.VoltageLevel][0],
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
							mode_lib->vba.GPUVMEnable,
							mode_lib->vba.VMMPageSize,
							mode_lib->vba.PTEBufferSizeInRequestsLuma,
							mode_lib->vba.PDEProcessingBufIn64KBReqs,
							mode_lib->vba.PitchC[k],
							0,
							&mode_lib->vba.MacroTileWidthC[k],
							&MetaRowByteC,
							&PixelPTEBytesPerRowC,
							&mode_lib->vba.PTEBufferSizeNotExceeded[mode_lib->vba.VoltageLevel][0],
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
				mode_lib->vba.GPUVMEnable,
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

	mode_lib->vba.TCalc = 24.0 / mode_lib->vba.DCFCLKDeepSleep;

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
				mode_lib->vba.PrefetchMode[mode_lib->vba.VoltageLevel][mode_lib->vba.maxMpcComb],
				mode_lib->vba.DRAMClockChangeLatency,
				mode_lib->vba.UrgentLatencyPixelDataOnly,
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

			CalculateDelayAfterScaler(mode_lib, mode_lib->vba.ReturnBW, mode_lib->vba.ReadBandwidthPlaneLuma[k], mode_lib->vba.ReadBandwidthPlaneChroma[k], mode_lib->vba.TotalDataReadBandwidth,
					mode_lib->vba.DisplayPipeLineDeliveryTimeLuma[k], mode_lib->vba.DisplayPipeLineDeliveryTimeChroma[k],
					mode_lib->vba.DPPCLK[k], mode_lib->vba.DISPCLK, mode_lib->vba.PixelClock[k], mode_lib->vba.DSCDelay[k], mode_lib->vba.DPPPerPlane[k], mode_lib->vba.ScalerEnabled[k], mode_lib->vba.NumberOfCursors[k],
					mode_lib->vba.DPPCLKDelaySubtotal, mode_lib->vba.DPPCLKDelaySCL, mode_lib->vba.DPPCLKDelaySCLLBOnly, mode_lib->vba.DPPCLKDelayCNVCFormater, mode_lib->vba.DPPCLKDelayCNVCCursor, mode_lib->vba.DISPCLKDelaySubtotal,
					mode_lib->vba.SwathWidthY[k] / mode_lib->vba.HRatio[k], mode_lib->vba.OutputFormat[k], mode_lib->vba.HTotal[k],
					mode_lib->vba.SwathWidthSingleDPPY[k], mode_lib->vba.BytePerPixelDETY[k], mode_lib->vba.BytePerPixelDETC[k], mode_lib->vba.SwathHeightY[k], mode_lib->vba.SwathHeightC[k], mode_lib->vba.Interlace[k],
					mode_lib->vba.ProgressiveToInterlaceUnitInOPP, &mode_lib->vba.DSTXAfterScaler[k], &mode_lib->vba.DSTYAfterScaler[k]);

			mode_lib->vba.ErrorResult[k] =
					CalculatePrefetchSchedule(
							mode_lib,
							mode_lib->vba.DPPCLK[k],
							mode_lib->vba.DISPCLK,
							mode_lib->vba.PixelClock[k],
							mode_lib->vba.DCFCLKDeepSleep,
							mode_lib->vba.DPPPerPlane[k],
							mode_lib->vba.NumberOfCursors[k],
							mode_lib->vba.VTotal[k]
									- mode_lib->vba.VActive[k],
							mode_lib->vba.HTotal[k],
							mode_lib->vba.MaxInterDCNTileRepeaters,
							dml_min(
									mode_lib->vba.VStartupLines,
									mode_lib->vba.MaxVStartupLines[k]),
							mode_lib->vba.GPUVMMaxPageTableLevels,
							mode_lib->vba.GPUVMEnable,
							mode_lib->vba.DynamicMetadataEnable[k],
							mode_lib->vba.DynamicMetadataLinesBeforeActiveRequired[k],
							mode_lib->vba.DynamicMetadataTransmittedBytes[k],
							mode_lib->vba.DCCEnable[k],
							mode_lib->vba.UrgentLatencyPixelDataOnly,
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
							mode_lib->vba.DSTXAfterScaler[k],
							mode_lib->vba.DSTYAfterScaler[k],
							&mode_lib->vba.DestinationLinesForPrefetch[k],
							&mode_lib->vba.PrefetchBandwidth[k],
							&mode_lib->vba.DestinationLinesToRequestVMInVBlank[k],
							&mode_lib->vba.DestinationLinesToRequestRowInVBlank[k],
							&mode_lib->vba.VRatioPrefetchY[k],
							&mode_lib->vba.VRatioPrefetchC[k],
							&mode_lib->vba.RequiredPrefetchPixDataBWLuma[k],
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
													mode_lib->vba.RequiredPrefetchPixDataBWLuma[k])
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
						mode_lib->vba.UrgentLatencyPixelDataOnly,
						mode_lib->vba.GPUVMMaxPageTableLevels,
						mode_lib->vba.GPUVMEnable,
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
																mode_lib->vba.RequiredPrefetchPixDataBWLuma[k])));
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
		if (mode_lib->vba.PrefetchMode[mode_lib->vba.VoltageLevel][mode_lib->vba.maxMpcComb] == 0) {
			mode_lib->vba.AllowDRAMClockChangeDuringVBlank[k] = true;
			mode_lib->vba.AllowDRAMSelfRefreshDuringVBlank[k] = true;
			mode_lib->vba.MinTTUVBlank[k] = dml_max(
					mode_lib->vba.DRAMClockChangeWatermark,
					dml_max(
							mode_lib->vba.StutterEnterPlusExitWatermark,
							mode_lib->vba.UrgentWatermark));
		} else if (mode_lib->vba.PrefetchMode[mode_lib->vba.VoltageLevel][mode_lib->vba.maxMpcComb] == 1) {
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

	{
	float SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank = 999999;
	int PlaneWithMinActiveDRAMClockChangeMargin = -1;

	mode_lib->vba.MinActiveDRAMClockChangeMargin = 999999;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k]
				< mode_lib->vba.MinActiveDRAMClockChangeMargin) {
			mode_lib->vba.MinActiveDRAMClockChangeMargin =
					mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k];
			if (mode_lib->vba.BlendingAndTiming[k] == k) {
				PlaneWithMinActiveDRAMClockChangeMargin = k;
			} else {
				for (j = 0; j < mode_lib->vba.NumberOfActivePlanes; ++j) {
					if (mode_lib->vba.BlendingAndTiming[k] == j) {
						PlaneWithMinActiveDRAMClockChangeMargin = j;
					}
				}
			}
		}
	}

	mode_lib->vba.MinActiveDRAMClockChangeLatencySupported =
			mode_lib->vba.MinActiveDRAMClockChangeMargin
					+ mode_lib->vba.DRAMClockChangeLatency;
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (!((k == PlaneWithMinActiveDRAMClockChangeMargin) && (mode_lib->vba.BlendingAndTiming[k] == k))
				&& !(mode_lib->vba.BlendingAndTiming[k] == PlaneWithMinActiveDRAMClockChangeMargin)
				&& mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k]
						< SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank) {
			SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank =
					mode_lib->vba.ActiveDRAMClockChangeLatencyMargin[k];
		}
	}

	if (mode_lib->vba.DRAMClockChangeSupportsVActive &&
			mode_lib->vba.MinActiveDRAMClockChangeMargin > 60) {
		mode_lib->vba.DRAMClockChangeWatermark += 25;

		for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
			if (mode_lib->vba.PrefetchMode[mode_lib->vba.VoltageLevel][mode_lib->vba.maxMpcComb] == 0) {
				if (mode_lib->vba.DRAMClockChangeWatermark >
					dml_max(mode_lib->vba.StutterEnterPlusExitWatermark, mode_lib->vba.UrgentWatermark))
					mode_lib->vba.MinTTUVBlank[k] += 25;
			}
		}

		mode_lib->vba.DRAMClockChangeSupport[0][0] = dm_dram_clock_change_vactive;
	} else if (mode_lib->vba.DummyPStateCheck &&
			mode_lib->vba.MinActiveDRAMClockChangeMargin > 0) {
		mode_lib->vba.DRAMClockChangeSupport[0][0] = dm_dram_clock_change_vactive;
	} else {
		if ((mode_lib->vba.SynchronizedVBlank
				|| mode_lib->vba.NumberOfActivePlanes == 1
				|| (SecondMinActiveDRAMClockChangeMarginOneDisplayInVBLank > 0 &&
						mode_lib->vba.AllowDramClockChangeOneDisplayVactive))
					&& mode_lib->vba.PrefetchMode[mode_lib->vba.VoltageLevel][mode_lib->vba.maxMpcComb] == 0) {
			mode_lib->vba.DRAMClockChangeSupport[0][0] = dm_dram_clock_change_vblank;
			for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
				if (!mode_lib->vba.AllowDRAMClockChangeDuringVBlank[k]) {
					mode_lib->vba.DRAMClockChangeSupport[0][0] =
							dm_dram_clock_change_unsupported;
				}
			}
		} else {
			mode_lib->vba.DRAMClockChangeSupport[0][0] = dm_dram_clock_change_unsupported;
		}
	}
	}
	for (k = 0; k <= mode_lib->vba.soc.num_states; k++)
		for (j = 0; j < 2; j++)
			mode_lib->vba.DRAMClockChangeSupport[k][j] = mode_lib->vba.DRAMClockChangeSupport[0][0];

	//XFC Parameters:
	for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
		if (mode_lib->vba.XFCEnabled[k] == true) {
			double TWait;

			mode_lib->vba.XFCSlaveVUpdateOffset[k] = mode_lib->vba.XFCTSlvVupdateOffset;
			mode_lib->vba.XFCSlaveVupdateWidth[k] = mode_lib->vba.XFCTSlvVupdateWidth;
			mode_lib->vba.XFCSlaveVReadyOffset[k] = mode_lib->vba.XFCTSlvVreadyOffset;
			TWait = CalculateTWait(
					mode_lib->vba.PrefetchMode[mode_lib->vba.VoltageLevel][mode_lib->vba.maxMpcComb],
					mode_lib->vba.DRAMClockChangeLatency,
					mode_lib->vba.UrgentLatencyPixelDataOnly,
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
	{
		unsigned int VStartupMargin = 0;
		bool FirstMainPlane = true;

		for (k = 0; k < mode_lib->vba.NumberOfActivePlanes; ++k) {
			if (mode_lib->vba.BlendingAndTiming[k] == k) {
				unsigned int Margin = (mode_lib->vba.MaxVStartupLines[k] - mode_lib->vba.VStartup[k])
						* mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k];

				if (FirstMainPlane) {
					VStartupMargin = Margin;
					FirstMainPlane = false;
				} else
					VStartupMargin = dml_min(VStartupMargin, Margin);
		}

		if (mode_lib->vba.UseMaximumVStartup) {
			if (mode_lib->vba.VTotal_Max[k] == mode_lib->vba.VTotal[k]) {
				//only use max vstart if it is not drr or lateflip.
				mode_lib->vba.VStartup[k] = mode_lib->vba.MaxVStartupLines[mode_lib->vba.BlendingAndTiming[k]];
			}
		}
	}
}
}

static void dml20v2_DisplayPipeConfiguration(struct display_mode_lib *mode_lib)
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
			if (mode_lib->vba.DPPPerPlane[k] == 0)
				SwathWidth = 0;
			else
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
				<= mode_lib->vba.DETBufferSizeInKByte[0] * 1024.0 / 2.0) {
			mode_lib->vba.SwathHeightY[k] = MaximumSwathHeightY;
			mode_lib->vba.SwathHeightC[k] = MaximumSwathHeightC;
		} else {
			mode_lib->vba.SwathHeightY[k] = MinimumSwathHeightY;
			mode_lib->vba.SwathHeightC[k] = MinimumSwathHeightC;
		}

		if (mode_lib->vba.SwathHeightC[k] == 0) {
			mode_lib->vba.DETBufferSizeY[k] = mode_lib->vba.DETBufferSizeInKByte[0] * 1024;
			mode_lib->vba.DETBufferSizeC[k] = 0;
		} else if (mode_lib->vba.SwathHeightY[k] <= mode_lib->vba.SwathHeightC[k]) {
			mode_lib->vba.DETBufferSizeY[k] = mode_lib->vba.DETBufferSizeInKByte[0]
					* 1024.0 / 2;
			mode_lib->vba.DETBufferSizeC[k] = mode_lib->vba.DETBufferSizeInKByte[0]
					* 1024.0 / 2;
		} else {
			mode_lib->vba.DETBufferSizeY[k] = mode_lib->vba.DETBufferSizeInKByte[0]
					* 1024.0 * 2 / 3;
			mode_lib->vba.DETBufferSizeC[k] = mode_lib->vba.DETBufferSizeInKByte[0]
					* 1024.0 / 3;
		}
	}
}

static double CalculateTWait(
		unsigned int PrefetchMode,
		double DRAMClockChangeLatency,
		double UrgentLatencyPixelDataOnly,
		double SREnterPlusExitTime)
{
	if (PrefetchMode == 0) {
		return dml_max(
				DRAMClockChangeLatency + UrgentLatencyPixelDataOnly,
				dml_max(SREnterPlusExitTime, UrgentLatencyPixelDataOnly));
	} else if (PrefetchMode == 1) {
		return dml_max(SREnterPlusExitTime, UrgentLatencyPixelDataOnly);
	} else {
		return UrgentLatencyPixelDataOnly;
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

	if (GPUVMEnable != true) {
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
		double UrgentLatencyPixelDataOnly,
		unsigned int GPUVMMaxPageTableLevels,
		bool GPUVMEnable,
		double BandwidthAvailableForImmediateFlip,
		unsigned int TotImmediateFlipBytes,
		enum source_format_class SourcePixelFormat,
		unsigned int ImmediateFlipBytes,
		double LineTime,
		double VRatio,
		double Tno_bw,
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

		if (GPUVMEnable == true) {
			mode_lib->vba.ImmediateFlipBW[0] = BandwidthAvailableForImmediateFlip
					* ImmediateFlipBytes / TotImmediateFlipBytes;
			TimeForFetchingMetaPTEImmediateFlip =
					dml_max(
							Tno_bw
									+ PDEAndMetaPTEBytesFrame
											/ mode_lib->vba.ImmediateFlipBW[0],
							dml_max(
									UrgentExtraLatency
											+ UrgentLatencyPixelDataOnly
													* (GPUVMMaxPageTableLevels
															- 1),
									LineTime / 4.0));
		} else {
			TimeForFetchingMetaPTEImmediateFlip = 0;
		}

		*DestinationLinesToRequestVMInImmediateFlip = dml_floor(
				4.0 * (TimeForFetchingMetaPTEImmediateFlip / LineTime + 0.125),
				1) / 4.0;

		if ((GPUVMEnable == true || DCCEnable == true)) {
			mode_lib->vba.ImmediateFlipBW[0] = BandwidthAvailableForImmediateFlip
					* ImmediateFlipBytes / TotImmediateFlipBytes;
			TimeForFetchingRowInVBlankImmediateFlip = dml_max(
					(MetaRowByte + PixelPTEBytesPerRow)
							/ mode_lib->vba.ImmediateFlipBW[0],
					dml_max(UrgentLatencyPixelDataOnly, LineTime / 4.0));
		} else {
			TimeForFetchingRowInVBlankImmediateFlip = 0;
		}

		*DestinationLinesToRequestRowInImmediateFlip = dml_floor(
				4.0 * (TimeForFetchingRowInVBlankImmediateFlip / LineTime + 0.125),
				1) / 4.0;

		if (GPUVMEnable == true) {
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

		if (GPUVMEnable && !DCCEnable)
			min_row_time = dpte_row_height * LineTime / VRatio;
		else if (!GPUVMEnable && DCCEnable)
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

void dml20v2_ModeSupportAndSystemConfigurationFull(struct display_mode_lib *mode_lib)
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
	mode_lib->vba.UrgentLatency = mode_lib->vba.UrgentLatencyPixelDataOnly;
	for (i = 0; i <= mode_lib->vba.soc.num_states; i++) {
		locals->FabricAndDRAMBandwidthPerState[i] = dml_min(
				mode_lib->vba.DRAMSpeedPerState[i] * mode_lib->vba.NumberOfChannels
						* mode_lib->vba.DRAMChannelWidth,
				mode_lib->vba.FabricClockPerState[i]
						* mode_lib->vba.FabricDatapathToDCNDataReturn) / 1000;
		locals->ReturnBWToDCNPerState = dml_min(locals->ReturnBusWidth * locals->DCFCLKPerState[i],
				locals->FabricAndDRAMBandwidthPerState[i] * 1000)
				* locals->PercentOfIdealDRAMFabricAndSDPPortBWReceivedAfterUrgLatencyPixelDataOnly / 100;

		locals->ReturnBWPerState[i][0] = locals->ReturnBWToDCNPerState;

		if (locals->DCCEnabledInAnyPlane == true && locals->ReturnBWToDCNPerState > locals->DCFCLKPerState[i] * locals->ReturnBusWidth / 4) {
			locals->ReturnBWPerState[i][0] = dml_min(locals->ReturnBWPerState[i][0],
					locals->ReturnBWToDCNPerState * 4 * (1 - locals->UrgentLatency /
					((locals->ROBBufferSizeInKByte - locals->PixelChunkSizeInKByte) * 1024
					/ (locals->ReturnBWToDCNPerState - locals->DCFCLKPerState[i]
					* locals->ReturnBusWidth / 4) + locals->UrgentLatency)));
		}
		locals->CriticalPoint = 2 * locals->ReturnBusWidth * locals->DCFCLKPerState[i] *
				locals->UrgentLatency / (locals->ReturnBWToDCNPerState * locals->UrgentLatency
				+ (locals->ROBBufferSizeInKByte - locals->PixelChunkSizeInKByte) * 1024);

		if (locals->DCCEnabledInAnyPlane && locals->CriticalPoint > 1 && locals->CriticalPoint < 4) {
			locals->ReturnBWPerState[i][0] = dml_min(locals->ReturnBWPerState[i][0],
				4 * locals->ReturnBWToDCNPerState *
				(locals->ROBBufferSizeInKByte - locals->PixelChunkSizeInKByte) * 1024
				* locals->ReturnBusWidth * locals->DCFCLKPerState[i] * locals->UrgentLatency /
				dml_pow((locals->ReturnBWToDCNPerState * locals->UrgentLatency
				+ (locals->ROBBufferSizeInKByte - locals->PixelChunkSizeInKByte) * 1024), 2));
		}

		locals->ReturnBWToDCNPerState = dml_min(locals->ReturnBusWidth *
				locals->DCFCLKPerState[i], locals->FabricAndDRAMBandwidthPerState[i] * 1000);

		if (locals->DCCEnabledInAnyPlane == true && locals->ReturnBWToDCNPerState > locals->DCFCLKPerState[i] * locals->ReturnBusWidth / 4) {
			locals->ReturnBWPerState[i][0] = dml_min(locals->ReturnBWPerState[i][0],
					locals->ReturnBWToDCNPerState * 4 * (1 - locals->UrgentLatency /
					((locals->ROBBufferSizeInKByte - locals->PixelChunkSizeInKByte) * 1024
					/ (locals->ReturnBWToDCNPerState - locals->DCFCLKPerState[i]
					* locals->ReturnBusWidth / 4) + locals->UrgentLatency)));
		}
		locals->CriticalPoint = 2 * locals->ReturnBusWidth * locals->DCFCLKPerState[i] *
				locals->UrgentLatency / (locals->ReturnBWToDCNPerState * locals->UrgentLatency
				+ (locals->ROBBufferSizeInKByte - locals->PixelChunkSizeInKByte) * 1024);

		if (locals->DCCEnabledInAnyPlane && locals->CriticalPoint > 1 && locals->CriticalPoint < 4) {
			locals->ReturnBWPerState[i][0] = dml_min(locals->ReturnBWPerState[i][0],
				4 * locals->ReturnBWToDCNPerState *
				(locals->ROBBufferSizeInKByte - locals->PixelChunkSizeInKByte) * 1024
				* locals->ReturnBusWidth * locals->DCFCLKPerState[i] * locals->UrgentLatency /
				dml_pow((locals->ReturnBWToDCNPerState * locals->UrgentLatency
				+ (locals->ROBBufferSizeInKByte - locals->PixelChunkSizeInKByte) * 1024), 2));
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
				+ locals->UrgentOutOfOrderReturnPerChannel * mode_lib->vba.NumberOfChannels / locals->ReturnBWPerState[i][0];
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
						mode_lib->vba.DETBufferSizeInKByte[0] * 1024.0 / 2.0
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
						mode_lib->vba.PixelClock[k] * (1.0 + mode_lib->vba.DISPCLKDPPCLKDSCCLKDownSpreading / 100.0)
								* (1.0 + mode_lib->vba.DISPCLKRampingMargin / 100.0);
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
					} else if (locals->DSCEnabled[k] && (locals->HActive[k] > DCN20_MAX_DSC_IMAGE_WIDTH)) {
						locals->ODMCombineEnablePerState[i][k] = true;
						mode_lib->vba.PlaneRequiredDISPCLK = mode_lib->vba.PlaneRequiredDISPCLKWithODMCombine;
					} else if (locals->HActive[k] > DCN20_MAX_420_IMAGE_WIDTH && locals->OutputFormat[k] == dm_420) {
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
			locals->RequiresDSC[i][k] = 0;
			locals->RequiresFEC[i][k] = 0;
			if (mode_lib->vba.BlendingAndTiming[k] == k) {
				if (mode_lib->vba.Output[k] == dm_hdmi) {
					locals->RequiresDSC[i][k] = 0;
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
			if (!mode_lib->vba.skip_dio_check[k]
					&& (locals->OutputBppPerState[i][k] == BPP_INVALID
						|| (mode_lib->vba.OutputFormat[k] == dm_420
							&& mode_lib->vba.Interlace[k] == true
							&& mode_lib->vba.ProgressiveToInterlaceUnitInOPP == true))) {
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
	for (i = 0; i <= mode_lib->vba.soc.num_states; i++) {
		for (j = 0; j < 2; j++) {
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				if (locals->ODMCombineEnablePerState[i][k] == dm_odm_combine_mode_2to1)
					locals->SwathWidthYPerState[i][j][k] = dml_min(locals->SwathWidthYSingleDPP[k], dml_round(locals->HActive[k] / 2 * locals->HRatio[k]));
				else
					locals->SwathWidthYPerState[i][j][k] = locals->SwathWidthYSingleDPP[k] / locals->NoOfDPP[i][j][k];
				locals->SwathWidthGranularityY = 256  / dml_ceil(locals->BytePerPixelInDETY[k], 1) / locals->MaxSwathHeightY[k];
				locals->RoundedUpMaxSwathSizeBytesY = (dml_ceil(locals->SwathWidthYPerState[i][j][k] - 1, locals->SwathWidthGranularityY)
						+ locals->SwathWidthGranularityY) * locals->BytePerPixelInDETY[k] * locals->MaxSwathHeightY[k];
				if (locals->SourcePixelFormat[k] == dm_420_10) {
					locals->RoundedUpMaxSwathSizeBytesY = dml_ceil(locals->RoundedUpMaxSwathSizeBytesY, 256) + 256;
				}
				if (locals->MaxSwathHeightC[k] > 0) {
					locals->SwathWidthGranularityC = 256 / dml_ceil(locals->BytePerPixelInDETC[k], 2) / locals->MaxSwathHeightC[k];

					locals->RoundedUpMaxSwathSizeBytesC = (dml_ceil(locals->SwathWidthYPerState[i][j][k] / 2 - 1, locals->SwathWidthGranularityC)
					+ locals->SwathWidthGranularityC) * locals->BytePerPixelInDETC[k] * locals->MaxSwathHeightC[k];
				}
				if (locals->SourcePixelFormat[k] == dm_420_10) {
					locals->RoundedUpMaxSwathSizeBytesC = dml_ceil(locals->RoundedUpMaxSwathSizeBytesC, 256)  + 256;
				} else {
					locals->RoundedUpMaxSwathSizeBytesC = 0;
				}

				if (locals->RoundedUpMaxSwathSizeBytesY + locals->RoundedUpMaxSwathSizeBytesC <= locals->DETBufferSizeInKByte[0] * 1024 / 2) {
					locals->SwathHeightYPerState[i][j][k] = locals->MaxSwathHeightY[k];
					locals->SwathHeightCPerState[i][j][k] = locals->MaxSwathHeightC[k];
				} else {
					locals->SwathHeightYPerState[i][j][k] = locals->MinSwathHeightY[k];
					locals->SwathHeightCPerState[i][j][k] = locals->MinSwathHeightC[k];
				}

				if (locals->BytePerPixelInDETC[k] == 0) {
					locals->LinesInDETLuma = locals->DETBufferSizeInKByte[0] * 1024 / locals->BytePerPixelInDETY[k] / locals->SwathWidthYPerState[i][j][k];
					locals->LinesInDETChroma = 0;
				} else if (locals->SwathHeightYPerState[i][j][k] <= locals->SwathHeightCPerState[i][j][k]) {
					locals->LinesInDETLuma = locals->DETBufferSizeInKByte[0] * 1024 / 2 / locals->BytePerPixelInDETY[k] /
							locals->SwathWidthYPerState[i][j][k];
					locals->LinesInDETChroma = locals->DETBufferSizeInKByte[0] * 1024 / 2 / locals->BytePerPixelInDETC[k] / (locals->SwathWidthYPerState[i][j][k] / 2);
				} else {
					locals->LinesInDETLuma = locals->DETBufferSizeInKByte[0] * 1024 * 2 / 3 / locals->BytePerPixelInDETY[k] / locals->SwathWidthYPerState[i][j][k];
					locals->LinesInDETChroma = locals->DETBufferSizeInKByte[0] * 1024 / 3 / locals->BytePerPixelInDETY[k] / (locals->SwathWidthYPerState[i][j][k] / 2);
				}

				locals->EffectiveLBLatencyHidingSourceLinesLuma = dml_min(locals->MaxLineBufferLines,
					dml_floor(locals->LineBufferSize / locals->LBBitPerPixel[k] / (locals->SwathWidthYPerState[i][j][k]
					/ dml_max(locals->HRatio[k], 1)), 1)) - (locals->vtaps[k] - 1);

				locals->EffectiveLBLatencyHidingSourceLinesChroma =  dml_min(locals->MaxLineBufferLines,
						dml_floor(locals->LineBufferSize / locals->LBBitPerPixel[k]
						/ (locals->SwathWidthYPerState[i][j][k] / 2
						/ dml_max(locals->HRatio[k] / 2, 1)), 1)) - (locals->VTAPsChroma[k] - 1);

				locals->EffectiveDETLBLinesLuma = dml_floor(locals->LinesInDETLuma +  dml_min(
						locals->LinesInDETLuma * locals->RequiredDISPCLK[i][j] * locals->BytePerPixelInDETY[k] *
						locals->PSCL_FACTOR[k] / locals->ReturnBWPerState[i][0],
						locals->EffectiveLBLatencyHidingSourceLinesLuma),
						locals->SwathHeightYPerState[i][j][k]);

				locals->EffectiveDETLBLinesChroma = dml_floor(locals->LinesInDETChroma + dml_min(
						locals->LinesInDETChroma * locals->RequiredDISPCLK[i][j] * locals->BytePerPixelInDETC[k] *
						locals->PSCL_FACTOR_CHROMA[k] / locals->ReturnBWPerState[i][0],
						locals->EffectiveLBLatencyHidingSourceLinesChroma),
						locals->SwathHeightCPerState[i][j][k]);

				if (locals->BytePerPixelInDETC[k] == 0) {
					locals->UrgentLatencySupportUsPerState[i][j][k] = locals->EffectiveDETLBLinesLuma * (locals->HTotal[k] / locals->PixelClock[k])
							/ locals->VRatio[k] - locals->EffectiveDETLBLinesLuma * locals->SwathWidthYPerState[i][j][k] *
								dml_ceil(locals->BytePerPixelInDETY[k], 1) / (locals->ReturnBWPerState[i][0] / locals->NoOfDPP[i][j][k]);
				} else {
					locals->UrgentLatencySupportUsPerState[i][j][k] = dml_min(
						locals->EffectiveDETLBLinesLuma * (locals->HTotal[k] / locals->PixelClock[k])
						/ locals->VRatio[k] - locals->EffectiveDETLBLinesLuma * locals->SwathWidthYPerState[i][j][k] *
						dml_ceil(locals->BytePerPixelInDETY[k], 1) / (locals->ReturnBWPerState[i][0] / locals->NoOfDPP[i][j][k]),
							locals->EffectiveDETLBLinesChroma * (locals->HTotal[k] / locals->PixelClock[k]) / (locals->VRatio[k] / 2) -
							locals->EffectiveDETLBLinesChroma * locals->SwathWidthYPerState[i][j][k] / 2 *
							dml_ceil(locals->BytePerPixelInDETC[k], 2) / (locals->ReturnBWPerState[i][0] / locals->NoOfDPP[i][j][k]));
				}
			}
		}
	}

	for (i = 0; i <= locals->soc.num_states; i++) {
		for (j = 0; j < 2; j++) {
			locals->UrgentLatencySupport[i][j] = true;
			for (k = 0; k < locals->NumberOfActivePlanes; k++) {
				if (locals->UrgentLatencySupportUsPerState[i][j][k] < locals->UrgentLatency)
					locals->UrgentLatencySupport[i][j] = false;
			}
		}
	}


	/*Prefetch Check*/
	for (i = 0; i <= locals->soc.num_states; i++) {
		for (j = 0; j < 2; j++) {
			locals->TotalNumberOfDCCActiveDPP[i][j] = 0;
			for (k = 0; k < locals->NumberOfActivePlanes; k++) {
				if (locals->DCCEnable[k] == true) {
					locals->TotalNumberOfDCCActiveDPP[i][j] =
						locals->TotalNumberOfDCCActiveDPP[i][j] + locals->NoOfDPP[i][j][k];
				}
			}
		}
	}

	CalculateMinAndMaxPrefetchMode(locals->AllowDRAMSelfRefreshOrDRAMClockChangeInVblank, &locals->MinPrefetchMode, &locals->MaxPrefetchMode);

	locals->MaxTotalVActiveRDBandwidth = 0;
	for (k = 0; k < locals->NumberOfActivePlanes; k++) {
		locals->MaxTotalVActiveRDBandwidth = locals->MaxTotalVActiveRDBandwidth + locals->ReadBandwidth[k];
	}

	for (i = 0; i <= locals->soc.num_states; i++) {
		for (j = 0; j < 2; j++) {
			for (k = 0; k < locals->NumberOfActivePlanes; k++) {
				locals->NoOfDPPThisState[k] = locals->NoOfDPP[i][j][k];
				locals->RequiredDPPCLKThisState[k] = locals->RequiredDPPCLK[i][j][k];
				locals->SwathHeightYThisState[k] = locals->SwathHeightYPerState[i][j][k];
				locals->SwathHeightCThisState[k] = locals->SwathHeightCPerState[i][j][k];
				locals->SwathWidthYThisState[k] = locals->SwathWidthYPerState[i][j][k];
				mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0] = dml_max(
						mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0],
						mode_lib->vba.PixelClock[k] / 16.0);
				if (mode_lib->vba.BytePerPixelInDETC[k] == 0.0) {
					if (mode_lib->vba.VRatio[k] <= 1.0) {
						mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0] =
								dml_max(
										mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0],
										1.1
												* dml_ceil(
														mode_lib->vba.BytePerPixelInDETY[k],
														1.0)
												/ 64.0
												* mode_lib->vba.HRatio[k]
												* mode_lib->vba.PixelClock[k]
												/ mode_lib->vba.NoOfDPP[i][j][k]);
					} else {
						mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0] =
								dml_max(
										mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0],
										1.1
												* dml_ceil(
														mode_lib->vba.BytePerPixelInDETY[k],
														1.0)
												/ 64.0
												* mode_lib->vba.PSCL_FACTOR[k]
												* mode_lib->vba.RequiredDPPCLK[i][j][k]);
					}
				} else {
					if (mode_lib->vba.VRatio[k] <= 1.0) {
						mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0] =
								dml_max(
										mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0],
										1.1
												* dml_ceil(
														mode_lib->vba.BytePerPixelInDETY[k],
														1.0)
												/ 32.0
												* mode_lib->vba.HRatio[k]
												* mode_lib->vba.PixelClock[k]
												/ mode_lib->vba.NoOfDPP[i][j][k]);
					} else {
						mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0] =
								dml_max(
										mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0],
										1.1
												* dml_ceil(
														mode_lib->vba.BytePerPixelInDETY[k],
														1.0)
												/ 32.0
												* mode_lib->vba.PSCL_FACTOR[k]
												* mode_lib->vba.RequiredDPPCLK[i][j][k]);
					}
					if (mode_lib->vba.VRatio[k] / 2.0 <= 1.0) {
						mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0] =
								dml_max(
										mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0],
										1.1
												* dml_ceil(
														mode_lib->vba.BytePerPixelInDETC[k],
														2.0)
												/ 32.0
												* mode_lib->vba.HRatio[k]
												/ 2.0
												* mode_lib->vba.PixelClock[k]
												/ mode_lib->vba.NoOfDPP[i][j][k]);
					} else {
						mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0] =
								dml_max(
										mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0],
										1.1
												* dml_ceil(
														mode_lib->vba.BytePerPixelInDETC[k],
														2.0)
												/ 32.0
												* mode_lib->vba.PSCL_FACTOR_CHROMA[k]
												* mode_lib->vba.RequiredDPPCLK[i][j][k]);
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
						mode_lib->vba.SwathWidthYPerState[i][j][k],
						mode_lib->vba.GPUVMEnable,
						mode_lib->vba.VMMPageSize,
						mode_lib->vba.PTEBufferSizeInRequestsLuma,
						mode_lib->vba.PDEProcessingBufIn64KBReqs,
						mode_lib->vba.PitchY[k],
						mode_lib->vba.DCCMetaPitchY[k],
						&mode_lib->vba.MacroTileWidthY[k],
						&mode_lib->vba.MetaRowBytesY,
						&mode_lib->vba.DPTEBytesPerRowY,
						&mode_lib->vba.PTEBufferSizeNotExceededY[i][j][k],
						&mode_lib->vba.dpte_row_height[k],
						&mode_lib->vba.meta_row_height[k]);
				mode_lib->vba.PrefetchLinesY[0][0][k] = CalculatePrefetchSourceLines(
						mode_lib,
						mode_lib->vba.VRatio[k],
						mode_lib->vba.vtaps[k],
						mode_lib->vba.Interlace[k],
						mode_lib->vba.ProgressiveToInterlaceUnitInOPP,
						mode_lib->vba.SwathHeightYPerState[i][j][k],
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
							mode_lib->vba.SwathWidthYPerState[i][j][k] / 2.0,
							mode_lib->vba.GPUVMEnable,
							mode_lib->vba.VMMPageSize,
							mode_lib->vba.PTEBufferSizeInRequestsLuma,
							mode_lib->vba.PDEProcessingBufIn64KBReqs,
							mode_lib->vba.PitchC[k],
							0.0,
							&mode_lib->vba.MacroTileWidthC[k],
							&mode_lib->vba.MetaRowBytesC,
							&mode_lib->vba.DPTEBytesPerRowC,
							&mode_lib->vba.PTEBufferSizeNotExceededC[i][j][k],
							&mode_lib->vba.dpte_row_height_chroma[k],
							&mode_lib->vba.meta_row_height_chroma[k]);
					mode_lib->vba.PrefetchLinesC[0][0][k] = CalculatePrefetchSourceLines(
							mode_lib,
							mode_lib->vba.VRatio[k] / 2.0,
							mode_lib->vba.VTAPsChroma[k],
							mode_lib->vba.Interlace[k],
							mode_lib->vba.ProgressiveToInterlaceUnitInOPP,
							mode_lib->vba.SwathHeightCPerState[i][j][k],
							mode_lib->vba.ViewportYStartC[k],
							&mode_lib->vba.PrefillC[k],
							&mode_lib->vba.MaxNumSwC[k]);
				} else {
					mode_lib->vba.PDEAndMetaPTEBytesPerFrameC = 0.0;
					mode_lib->vba.MetaRowBytesC = 0.0;
					mode_lib->vba.DPTEBytesPerRowC = 0.0;
					locals->PrefetchLinesC[0][0][k] = 0.0;
					locals->PTEBufferSizeNotExceededC[i][j][k] = true;
					locals->PTEBufferSizeInRequestsForLuma = mode_lib->vba.PTEBufferSizeInRequestsLuma + mode_lib->vba.PTEBufferSizeInRequestsChroma;
				}
				locals->PDEAndMetaPTEBytesPerFrame[0][0][k] =
						mode_lib->vba.PDEAndMetaPTEBytesPerFrameY + mode_lib->vba.PDEAndMetaPTEBytesPerFrameC;
				locals->MetaRowBytes[0][0][k] = mode_lib->vba.MetaRowBytesY + mode_lib->vba.MetaRowBytesC;
				locals->DPTEBytesPerRow[0][0][k] = mode_lib->vba.DPTEBytesPerRowY + mode_lib->vba.DPTEBytesPerRowC;

				CalculateActiveRowBandwidth(
						mode_lib->vba.GPUVMEnable,
						mode_lib->vba.SourcePixelFormat[k],
						mode_lib->vba.VRatio[k],
						mode_lib->vba.DCCEnable[k],
						mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k],
						mode_lib->vba.MetaRowBytesY,
						mode_lib->vba.MetaRowBytesC,
						mode_lib->vba.meta_row_height[k],
						mode_lib->vba.meta_row_height_chroma[k],
						mode_lib->vba.DPTEBytesPerRowY,
						mode_lib->vba.DPTEBytesPerRowC,
						mode_lib->vba.dpte_row_height[k],
						mode_lib->vba.dpte_row_height_chroma[k],
						&mode_lib->vba.meta_row_bw[k],
						&mode_lib->vba.dpte_row_bw[k],
						&mode_lib->vba.qual_row_bw[k]);
			}
			mode_lib->vba.ExtraLatency =
					mode_lib->vba.UrgentRoundTripAndOutOfOrderLatencyPerState[i]
							+ (mode_lib->vba.TotalNumberOfActiveDPP[i][j]
									* mode_lib->vba.PixelChunkSizeInKByte
									+ mode_lib->vba.TotalNumberOfDCCActiveDPP[i][j]
											* mode_lib->vba.MetaChunkSize)
									* 1024.0
									/ mode_lib->vba.ReturnBWPerState[i][0];
			if (mode_lib->vba.GPUVMEnable == true) {
				mode_lib->vba.ExtraLatency = mode_lib->vba.ExtraLatency
						+ mode_lib->vba.TotalNumberOfActiveDPP[i][j]
								* mode_lib->vba.PTEGroupSize
								/ mode_lib->vba.ReturnBWPerState[i][0];
			}
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
			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				for (m = 0; m < locals->NumberOfCursors[k]; m++)
					locals->cursor_bw[k] = locals->NumberOfCursors[k] * locals->CursorWidth[k][m] * locals->CursorBPP[k][m]
						/ 8 / (locals->HTotal[k] / locals->PixelClock[k]) * locals->VRatio[k];
			}

			for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
				locals->MaximumVStartup[0][0][k] = mode_lib->vba.VTotal[k] - mode_lib->vba.VActive[k]
					- dml_max(1.0, dml_ceil(locals->WritebackDelay[i][k] / (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k]), 1.0));
			}

			mode_lib->vba.NextPrefetchMode = mode_lib->vba.MinPrefetchMode;
			do {
				mode_lib->vba.PrefetchMode[i][j] = mode_lib->vba.NextPrefetchMode;
				mode_lib->vba.NextPrefetchMode = mode_lib->vba.NextPrefetchMode + 1;

				mode_lib->vba.TWait = CalculateTWait(
						mode_lib->vba.PrefetchMode[i][j],
						mode_lib->vba.DRAMClockChangeLatency,
						mode_lib->vba.UrgentLatency,
						mode_lib->vba.SREnterPlusExitTime);
				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {

					if (mode_lib->vba.XFCEnabled[k] == true) {
						mode_lib->vba.XFCRemoteSurfaceFlipDelay =
								CalculateRemoteSurfaceFlipDelay(
										mode_lib,
										mode_lib->vba.VRatio[k],
										locals->SwathWidthYPerState[i][j][k],
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

					CalculateDelayAfterScaler(mode_lib, mode_lib->vba.ReturnBWPerState[i][0], mode_lib->vba.ReadBandwidthLuma[k], mode_lib->vba.ReadBandwidthChroma[k], mode_lib->vba.MaxTotalVActiveRDBandwidth,
						mode_lib->vba.DisplayPipeLineDeliveryTimeLuma[k], mode_lib->vba.DisplayPipeLineDeliveryTimeChroma[k],
						mode_lib->vba.RequiredDPPCLK[i][j][k], mode_lib->vba.RequiredDISPCLK[i][j], mode_lib->vba.PixelClock[k], mode_lib->vba.DSCDelayPerState[i][k], mode_lib->vba.NoOfDPP[i][j][k], mode_lib->vba.ScalerEnabled[k], mode_lib->vba.NumberOfCursors[k],
						mode_lib->vba.DPPCLKDelaySubtotal, mode_lib->vba.DPPCLKDelaySCL, mode_lib->vba.DPPCLKDelaySCLLBOnly, mode_lib->vba.DPPCLKDelayCNVCFormater, mode_lib->vba.DPPCLKDelayCNVCCursor, mode_lib->vba.DISPCLKDelaySubtotal,
						mode_lib->vba.SwathWidthYPerState[i][j][k] / mode_lib->vba.HRatio[k], mode_lib->vba.OutputFormat[k], mode_lib->vba.HTotal[k],
						mode_lib->vba.SwathWidthYSingleDPP[k], mode_lib->vba.BytePerPixelInDETY[k], mode_lib->vba.BytePerPixelInDETC[k], mode_lib->vba.SwathHeightYThisState[k], mode_lib->vba.SwathHeightCThisState[k], mode_lib->vba.Interlace[k], mode_lib->vba.ProgressiveToInterlaceUnitInOPP,
						&mode_lib->vba.DSTXAfterScaler[k], &mode_lib->vba.DSTYAfterScaler[k]);

					mode_lib->vba.IsErrorResult[i][j][k] =
							CalculatePrefetchSchedule(
									mode_lib,
									mode_lib->vba.RequiredDPPCLK[i][j][k],
									mode_lib->vba.RequiredDISPCLK[i][j],
									mode_lib->vba.PixelClock[k],
									mode_lib->vba.ProjectedDCFCLKDeepSleep[0][0],
									mode_lib->vba.NoOfDPP[i][j][k],
									mode_lib->vba.NumberOfCursors[k],
									mode_lib->vba.VTotal[k]
											- mode_lib->vba.VActive[k],
									mode_lib->vba.HTotal[k],
									mode_lib->vba.MaxInterDCNTileRepeaters,
									mode_lib->vba.MaximumVStartup[0][0][k],
									mode_lib->vba.GPUVMMaxPageTableLevels,
									mode_lib->vba.GPUVMEnable,
									mode_lib->vba.DynamicMetadataEnable[k],
									mode_lib->vba.DynamicMetadataLinesBeforeActiveRequired[k],
									mode_lib->vba.DynamicMetadataTransmittedBytes[k],
									mode_lib->vba.DCCEnable[k],
									mode_lib->vba.UrgentLatencyPixelDataOnly,
									mode_lib->vba.ExtraLatency,
									mode_lib->vba.TimeCalc,
									mode_lib->vba.PDEAndMetaPTEBytesPerFrame[0][0][k],
									mode_lib->vba.MetaRowBytes[0][0][k],
									mode_lib->vba.DPTEBytesPerRow[0][0][k],
									mode_lib->vba.PrefetchLinesY[0][0][k],
									mode_lib->vba.SwathWidthYPerState[i][j][k],
									mode_lib->vba.BytePerPixelInDETY[k],
									mode_lib->vba.PrefillY[k],
									mode_lib->vba.MaxNumSwY[k],
									mode_lib->vba.PrefetchLinesC[0][0][k],
									mode_lib->vba.BytePerPixelInDETC[k],
									mode_lib->vba.PrefillC[k],
									mode_lib->vba.MaxNumSwC[k],
									mode_lib->vba.SwathHeightYPerState[i][j][k],
									mode_lib->vba.SwathHeightCPerState[i][j][k],
									mode_lib->vba.TWait,
									mode_lib->vba.XFCEnabled[k],
									mode_lib->vba.XFCRemoteSurfaceFlipDelay,
									mode_lib->vba.Interlace[k],
									mode_lib->vba.ProgressiveToInterlaceUnitInOPP,
									mode_lib->vba.DSTXAfterScaler[k],
									mode_lib->vba.DSTYAfterScaler[k],
									&mode_lib->vba.LineTimesForPrefetch[k],
									&mode_lib->vba.PrefetchBW[k],
									&mode_lib->vba.LinesForMetaPTE[k],
									&mode_lib->vba.LinesForMetaAndDPTERow[k],
									&mode_lib->vba.VRatioPreY[i][j][k],
									&mode_lib->vba.VRatioPreC[i][j][k],
									&mode_lib->vba.RequiredPrefetchPixelDataBWLuma[i][j][k],
									&mode_lib->vba.Tno_bw[k],
									&mode_lib->vba.VUpdateOffsetPix[k],
									&mode_lib->vba.VUpdateWidthPix[k],
									&mode_lib->vba.VReadyOffsetPix[k]);
				}
				mode_lib->vba.MaximumReadBandwidthWithoutPrefetch = 0.0;
				mode_lib->vba.MaximumReadBandwidthWithPrefetch = 0.0;
				locals->prefetch_vm_bw_valid = true;
				locals->prefetch_row_bw_valid = true;
				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
					if (locals->PDEAndMetaPTEBytesPerFrame[0][0][k] == 0)
						locals->prefetch_vm_bw[k] = 0;
					else if (locals->LinesForMetaPTE[k] > 0)
						locals->prefetch_vm_bw[k] = locals->PDEAndMetaPTEBytesPerFrame[0][0][k]
							/ (locals->LinesForMetaPTE[k] * locals->HTotal[k] / locals->PixelClock[k]);
					else {
						locals->prefetch_vm_bw[k] = 0;
						locals->prefetch_vm_bw_valid = false;
					}
					if (locals->MetaRowBytes[0][0][k] + locals->DPTEBytesPerRow[0][0][k] == 0)
						locals->prefetch_row_bw[k] = 0;
					else if (locals->LinesForMetaAndDPTERow[k] > 0)
						locals->prefetch_row_bw[k] = (locals->MetaRowBytes[0][0][k] + locals->DPTEBytesPerRow[0][0][k])
							/ (locals->LinesForMetaAndDPTERow[k] * locals->HTotal[k] / locals->PixelClock[k]);
					else {
						locals->prefetch_row_bw[k] = 0;
						locals->prefetch_row_bw_valid = false;
					}

					mode_lib->vba.MaximumReadBandwidthWithoutPrefetch = mode_lib->vba.MaximumReadBandwidthWithPrefetch
							+ mode_lib->vba.cursor_bw[k] + mode_lib->vba.ReadBandwidth[k] + mode_lib->vba.meta_row_bw[k] + mode_lib->vba.dpte_row_bw[k];
					mode_lib->vba.MaximumReadBandwidthWithPrefetch =
							mode_lib->vba.MaximumReadBandwidthWithPrefetch
									+ mode_lib->vba.cursor_bw[k]
									+ dml_max3(
											mode_lib->vba.prefetch_vm_bw[k],
											mode_lib->vba.prefetch_row_bw[k],
											dml_max(mode_lib->vba.ReadBandwidth[k],
											mode_lib->vba.RequiredPrefetchPixelDataBWLuma[i][j][k])
											+ mode_lib->vba.meta_row_bw[k] + mode_lib->vba.dpte_row_bw[k]);
				}
				locals->BandwidthWithoutPrefetchSupported[i][0] = true;
				if (mode_lib->vba.MaximumReadBandwidthWithoutPrefetch > locals->ReturnBWPerState[i][0]) {
					locals->BandwidthWithoutPrefetchSupported[i][0] = false;
				}

				locals->PrefetchSupported[i][j] = true;
				if (mode_lib->vba.MaximumReadBandwidthWithPrefetch > locals->ReturnBWPerState[i][0]) {
					locals->PrefetchSupported[i][j] = false;
				}
				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
					if (locals->LineTimesForPrefetch[k] < 2.0
							|| locals->LinesForMetaPTE[k] >= 8.0
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
			} while ((locals->PrefetchSupported[i][j] != true || locals->VRatioInPrefetchSupported[i][j] != true)
					&& mode_lib->vba.NextPrefetchMode < mode_lib->vba.MaxPrefetchMode);

			if (mode_lib->vba.PrefetchSupported[i][j] == true
					&& mode_lib->vba.VRatioInPrefetchSupported[i][j] == true) {
				mode_lib->vba.BandwidthAvailableForImmediateFlip =
						mode_lib->vba.ReturnBWPerState[i][0];
				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
					mode_lib->vba.BandwidthAvailableForImmediateFlip =
							mode_lib->vba.BandwidthAvailableForImmediateFlip
									- mode_lib->vba.cursor_bw[k]
									- dml_max(
											mode_lib->vba.ReadBandwidth[k] + mode_lib->vba.qual_row_bw[k],
											mode_lib->vba.PrefetchBW[k]);
				}
				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
					mode_lib->vba.ImmediateFlipBytes[k] = 0.0;
					if ((mode_lib->vba.SourcePixelFormat[k] != dm_420_8
							&& mode_lib->vba.SourcePixelFormat[k] != dm_420_10)) {
						mode_lib->vba.ImmediateFlipBytes[k] =
								mode_lib->vba.PDEAndMetaPTEBytesPerFrame[0][0][k]
										+ mode_lib->vba.MetaRowBytes[0][0][k]
										+ mode_lib->vba.DPTEBytesPerRow[0][0][k];
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
							mode_lib->vba.UrgentLatencyPixelDataOnly,
							mode_lib->vba.GPUVMMaxPageTableLevels,
							mode_lib->vba.GPUVMEnable,
							mode_lib->vba.BandwidthAvailableForImmediateFlip,
							mode_lib->vba.TotImmediateFlipBytes,
							mode_lib->vba.SourcePixelFormat[k],
							mode_lib->vba.ImmediateFlipBytes[k],
							mode_lib->vba.HTotal[k]
									/ mode_lib->vba.PixelClock[k],
							mode_lib->vba.VRatio[k],
							mode_lib->vba.Tno_bw[k],
							mode_lib->vba.PDEAndMetaPTEBytesPerFrame[0][0][k],
							mode_lib->vba.MetaRowBytes[0][0][k],
							mode_lib->vba.DPTEBytesPerRow[0][0][k],
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
															mode_lib->vba.RequiredPrefetchPixelDataBWLuma[i][j][k]));
				}
				mode_lib->vba.ImmediateFlipSupportedForState[i][j] = true;
				if (mode_lib->vba.total_dcn_read_bw_with_flip
						> mode_lib->vba.ReturnBWPerState[i][0]) {
					mode_lib->vba.ImmediateFlipSupportedForState[i][j] = false;
				}
				for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
					if (mode_lib->vba.ImmediateFlipSupportedForPipe[k] == false) {
						mode_lib->vba.ImmediateFlipSupportedForState[i][j] = false;
					}
				}
			} else {
				mode_lib->vba.ImmediateFlipSupportedForState[i][j] = false;
			}
		}
	}

	/*Vertical Active BW support*/
	for (i = 0; i <= mode_lib->vba.soc.num_states; i++) {
		mode_lib->vba.MaxTotalVerticalActiveAvailableBandwidth[i][0] = dml_min(mode_lib->vba.ReturnBusWidth *
				mode_lib->vba.DCFCLKPerState[i], mode_lib->vba.FabricAndDRAMBandwidthPerState[i] * 1000) *
				mode_lib->vba.MaxAveragePercentOfIdealDRAMBWDisplayCanUseInNormalSystemOperation / 100;
		if (mode_lib->vba.MaxTotalVActiveRDBandwidth <= mode_lib->vba.MaxTotalVerticalActiveAvailableBandwidth[i][0])
			mode_lib->vba.TotalVerticalActiveBandwidthSupport[i][0] = true;
		else
			mode_lib->vba.TotalVerticalActiveBandwidthSupport[i][0] = false;
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
		for (j = 0; j < 2; j++) {
			if (mode_lib->vba.CursorWidth[k][j] > 0.0) {
				if (dml_floor(
						dml_floor(
								mode_lib->vba.CursorBufferSize
										- mode_lib->vba.CursorChunkSize,
								mode_lib->vba.CursorChunkSize) * 1024.0
								/ (mode_lib->vba.CursorWidth[k][j]
										* mode_lib->vba.CursorBPP[k][j]
										/ 8.0),
						1.0)
						* (mode_lib->vba.HTotal[k] / mode_lib->vba.PixelClock[k])
						/ mode_lib->vba.VRatio[k] < mode_lib->vba.UrgentLatencyPixelDataOnly
						|| (mode_lib->vba.CursorBPP[k][j] == 64.0
								&& mode_lib->vba.Cursor64BppSupport == false)) {
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
			} else if (locals->UrgentLatencySupport[i][j] != true) {
				status = DML_FAIL_URGENT_LATENCY;
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
			} else if (locals->PrefetchSupported[i][j] != true) {
				status = DML_FAIL_PREFETCH_SUPPORT;
			} else if (locals->TotalVerticalActiveBandwidthSupport[i][0] != true) {
				status = DML_FAIL_TOTAL_V_ACTIVE_BW;
			} else if (locals->VRatioInPrefetchSupported[i][j] != true) {
				status = DML_FAIL_V_RATIO_PREFETCH;
			} else if (locals->PTEBufferSizeNotExceeded[i][j] != true) {
				status = DML_FAIL_PTE_BUFFER_SIZE;
			} else if (mode_lib->vba.NonsupportedDSCInputBPC != false) {
				status = DML_FAIL_DSC_INPUT_BPC;
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
						|| mode_lib->vba.WhenToDoMPCCombine == dm_mpc_always_when_possible)) {
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
	mode_lib->vba.FabricAndDRAMBandwidth = locals->FabricAndDRAMBandwidthPerState[mode_lib->vba.VoltageLevel];
	for (k = 0; k <= mode_lib->vba.NumberOfActivePlanes - 1; k++) {
		if (mode_lib->vba.BlendingAndTiming[k] == k) {
			mode_lib->vba.ODMCombineEnabled[k] =
					locals->ODMCombineEnablePerState[mode_lib->vba.VoltageLevel][k];
		} else {
			mode_lib->vba.ODMCombineEnabled[k] = 0;
		}
		mode_lib->vba.DSCEnabled[k] =
				locals->RequiresDSC[mode_lib->vba.VoltageLevel][k];
		mode_lib->vba.OutputBpp[k] =
				locals->OutputBppPerState[mode_lib->vba.VoltageLevel][k];
	}
}
