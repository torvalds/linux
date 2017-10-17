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
#ifndef __DISPLAY_MODE_SUPPORT_H__
#define __DISPLAY_MODE_SUPPORT_H__

#include "dml_common_defs.h"

struct display_mode_lib;

#define NumberOfStates 4
#define NumberOfStatesPlusTwo (NumberOfStates+2)

struct dml_ms_internal_vars {
	double ScaleRatioSupport;
	double SourceFormatPixelAndScanSupport;
	double TotalReadBandwidthConsumedGBytePerSecond;
	double TotalWriteBandwidthConsumedGBytePerSecond;
	double TotalBandwidthConsumedGBytePerSecond;
	double DCCEnabledInAnyPlane;
	double ReturnBWToDCNPerState;
	double CriticalPoint;
	double WritebackLatencySupport;
	double RequiredOutputBW;
	double TotalNumberOfActiveWriteback;
	double TotalAvailableWritebackSupport;
	double MaximumSwathWidth;
	double NumberOfDPPRequiredForDETSize;
	double NumberOfDPPRequiredForLBSize;
	double MinDispclkUsingSingleDPP;
	double MinDispclkUsingDualDPP;
	double ViewportSizeSupport;
	double SwathWidthGranularityY;
	double RoundedUpMaxSwathSizeBytesY;
	double SwathWidthGranularityC;
	double RoundedUpMaxSwathSizeBytesC;
	double LinesInDETLuma;
	double LinesInDETChroma;
	double EffectiveLBLatencyHidingSourceLinesLuma;
	double EffectiveLBLatencyHidingSourceLinesChroma;
	double EffectiveDETLBLinesLuma;
	double EffectiveDETLBLinesChroma;
	double ProjectedDCFCLKDeepSleep;
	double MetaReqHeightY;
	double MetaReqWidthY;
	double MetaSurfaceWidthY;
	double MetaSurfaceHeightY;
	double MetaPteBytesPerFrameY;
	double MetaRowBytesY;
	double MacroTileBlockSizeBytesY;
	double MacroTileBlockHeightY;
	double DataPTEReqHeightY;
	double DataPTEReqWidthY;
	double DPTEBytesPerRowY;
	double MetaReqHeightC;
	double MetaReqWidthC;
	double MetaSurfaceWidthC;
	double MetaSurfaceHeightC;
	double MetaPteBytesPerFrameC;
	double MetaRowBytesC;
	double MacroTileBlockSizeBytesC;
	double MacroTileBlockHeightC;
	double MacroTileBlockWidthC;
	double DataPTEReqHeightC;
	double DataPTEReqWidthC;
	double DPTEBytesPerRowC;
	double VInitY;
	double MaxPartialSwY;
	double VInitC;
	double MaxPartialSwC;
	double dst_x_after_scaler;
	double dst_y_after_scaler;
	double TimeCalc;
	double VUpdateOffset;
	double TotalRepeaterDelay;
	double VUpdateWidth;
	double VReadyOffset;
	double TimeSetup;
	double ExtraLatency;
	double MaximumVStartup;
	double BWAvailableForImmediateFlip;
	double TotalImmediateFlipBytes;
	double TimeForMetaPTEWithImmediateFlip;
	double TimeForMetaPTEWithoutImmediateFlip;
	double TimeForMetaAndDPTERowWithImmediateFlip;
	double TimeForMetaAndDPTERowWithoutImmediateFlip;
	double LineTimesToRequestPrefetchPixelDataWithImmediateFlip;
	double LineTimesToRequestPrefetchPixelDataWithoutImmediateFlip;
	double MaximumReadBandwidthWithPrefetchWithImmediateFlip;
	double MaximumReadBandwidthWithPrefetchWithoutImmediateFlip;
	double VoltageOverrideLevel;
	double VoltageLevelWithImmediateFlip;
	double VoltageLevelWithoutImmediateFlip;
	double ImmediateFlipSupported;
	double VoltageLevel;
	double DCFCLK;
	double FabricAndDRAMBandwidth;
	double SwathWidthYSingleDPP[DC__NUM_PIPES__MAX];
	double BytePerPixelInDETY[DC__NUM_PIPES__MAX];
	double BytePerPixelInDETC[DC__NUM_PIPES__MAX];
	double ReadBandwidth[DC__NUM_PIPES__MAX];
	double WriteBandwidth[DC__NUM_PIPES__MAX];
	double DCFCLKPerState[NumberOfStatesPlusTwo];
	double FabricAndDRAMBandwidthPerState[NumberOfStatesPlusTwo];
	double ReturnBWPerState[NumberOfStatesPlusTwo];
	double BandwidthSupport[NumberOfStatesPlusTwo];
	double UrgentRoundTripAndOutOfOrderLatencyPerState[NumberOfStatesPlusTwo];
	double ROBSupport[NumberOfStatesPlusTwo];
	double RequiredPHYCLK[DC__NUM_PIPES__MAX];
	double DIOSupport[NumberOfStatesPlusTwo];
	double PHYCLKPerState[NumberOfStatesPlusTwo];
	double PSCL_FACTOR[DC__NUM_PIPES__MAX];
	double PSCL_FACTOR_CHROMA[DC__NUM_PIPES__MAX];
	double MinDPPCLKUsingSingleDPP[DC__NUM_PIPES__MAX];
	double Read256BlockHeightY[DC__NUM_PIPES__MAX];
	double Read256BlockWidthY[DC__NUM_PIPES__MAX];
	double Read256BlockHeightC[DC__NUM_PIPES__MAX];
	double Read256BlockWidthC[DC__NUM_PIPES__MAX];
	double MaxSwathHeightY[DC__NUM_PIPES__MAX];
	double MaxSwathHeightC[DC__NUM_PIPES__MAX];
	double MinSwathHeightY[DC__NUM_PIPES__MAX];
	double MinSwathHeightC[DC__NUM_PIPES__MAX];
	double NumberOfDPPRequiredForDETAndLBSize[DC__NUM_PIPES__MAX];
	double TotalNumberOfActiveDPP[NumberOfStatesPlusTwo * 2];
	double RequiredDISPCLK[NumberOfStatesPlusTwo * 2];
	double DISPCLK_DPPCLK_Support[NumberOfStatesPlusTwo * 2];
	double MaxDispclk[NumberOfStatesPlusTwo];
	double MaxDppclk[NumberOfStatesPlusTwo];
	double NoOfDPP[NumberOfStatesPlusTwo * 2 * DC__NUM_PIPES__MAX];
	double TotalAvailablePipesSupport[NumberOfStatesPlusTwo * 2];
	double SwathWidthYPerState[NumberOfStatesPlusTwo * 2 * DC__NUM_PIPES__MAX];
	double SwathHeightYPerState[NumberOfStatesPlusTwo * 2 * DC__NUM_PIPES__MAX];
	double SwathHeightCPerState[NumberOfStatesPlusTwo * 2 * DC__NUM_PIPES__MAX];
	double DETBufferSizeYPerState[NumberOfStatesPlusTwo * 2 * DC__NUM_PIPES__MAX];
	double UrgentLatencySupportUsPerState[NumberOfStatesPlusTwo * 2 * DC__NUM_PIPES__MAX];
	double UrgentLatencySupport[NumberOfStatesPlusTwo * 2];
	double TotalNumberOfDCCActiveDPP[NumberOfStatesPlusTwo * 2];
	double DPTEBytesPerRow[DC__NUM_PIPES__MAX];
	double MetaPTEBytesPerFrame[DC__NUM_PIPES__MAX];
	double MetaRowBytes[DC__NUM_PIPES__MAX];
	double PrefillY[DC__NUM_PIPES__MAX];
	double MaxNumSwY[DC__NUM_PIPES__MAX];
	double PrefetchLinesY[DC__NUM_PIPES__MAX];
	double PrefillC[DC__NUM_PIPES__MAX];
	double MaxNumSwC[DC__NUM_PIPES__MAX];
	double PrefetchLinesC[DC__NUM_PIPES__MAX];
	double LineTimesForPrefetch[DC__NUM_PIPES__MAX];
	double PrefetchBW[DC__NUM_PIPES__MAX];
	double LinesForMetaPTEWithImmediateFlip[DC__NUM_PIPES__MAX];
	double LinesForMetaPTEWithoutImmediateFlip[DC__NUM_PIPES__MAX];
	double LinesForMetaAndDPTERowWithImmediateFlip[DC__NUM_PIPES__MAX];
	double LinesForMetaAndDPTERowWithoutImmediateFlip[DC__NUM_PIPES__MAX];
	double VRatioPreYWithImmediateFlip[NumberOfStatesPlusTwo * 2 * DC__NUM_PIPES__MAX];
	double VRatioPreCWithImmediateFlip[NumberOfStatesPlusTwo * 2 * DC__NUM_PIPES__MAX];
	double RequiredPrefetchPixelDataBWWithImmediateFlip[NumberOfStatesPlusTwo * 2
			* DC__NUM_PIPES__MAX];
	double VRatioPreYWithoutImmediateFlip[NumberOfStatesPlusTwo * 2 * DC__NUM_PIPES__MAX];
	double VRatioPreCWithoutImmediateFlip[NumberOfStatesPlusTwo * 2 * DC__NUM_PIPES__MAX];
	double RequiredPrefetchPixelDataBWWithoutImmediateFlip[NumberOfStatesPlusTwo * 2
			* DC__NUM_PIPES__MAX];
	double PrefetchSupportedWithImmediateFlip[NumberOfStatesPlusTwo * 2];
	double PrefetchSupportedWithoutImmediateFlip[NumberOfStatesPlusTwo * 2];
	double VRatioInPrefetchSupportedWithImmediateFlip[NumberOfStatesPlusTwo * 2];
	double VRatioInPrefetchSupportedWithoutImmediateFlip[NumberOfStatesPlusTwo * 2];
	double ModeSupportWithImmediateFlip[NumberOfStatesPlusTwo * 2];
	double ModeSupportWithoutImmediateFlip[NumberOfStatesPlusTwo * 2];
	double RequiredDISPCLKPerRatio[2];
	double DPPPerPlanePerRatio[2 * DC__NUM_PIPES__MAX];
	double DISPCLK_DPPCLK_SupportPerRatio[2];
	struct _vcs_dpi_wm_calc_pipe_params_st planes[DC__NUM_PIPES__MAX];
};

int dml_ms_check(
		struct display_mode_lib *mode_lib,
		struct _vcs_dpi_display_e2e_pipe_params_st *e2e,
		int num_pipes);

#endif
