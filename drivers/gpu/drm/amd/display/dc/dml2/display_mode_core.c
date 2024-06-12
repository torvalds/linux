/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#include "display_mode_core.h"
#include "display_mode_util.h"
#include "display_mode_lib_defines.h"

#include "dml_assert.h"

#define DML2_MAX_FMT_420_BUFFER_WIDTH 4096
#define TB_BORROWED_MAX 400

// ---------------------------
//  Declaration Begins
// ---------------------------
static void CalculateBytePerPixelAndBlockSizes(
	enum dml_source_format_class SourcePixelFormat,
	enum dml_swizzle_mode SurfaceTiling,
	// Output
	dml_uint_t *BytePerPixelY,
	dml_uint_t *BytePerPixelC,
	dml_float_t *BytePerPixelDETY,
	dml_float_t *BytePerPixelDETC,
	dml_uint_t *BlockHeight256BytesY,
	dml_uint_t *BlockHeight256BytesC,
	dml_uint_t *BlockWidth256BytesY,
	dml_uint_t *BlockWidth256BytesC,
	dml_uint_t *MacroTileHeightY,
	dml_uint_t *MacroTileHeightC,
	dml_uint_t *MacroTileWidthY,
	dml_uint_t *MacroTileWidthC);

static dml_float_t CalculateWriteBackDISPCLK(
	enum dml_source_format_class WritebackPixelFormat,
	dml_float_t PixelClock,
	dml_float_t WritebackHRatio,
	dml_float_t WritebackVRatio,
	dml_uint_t WritebackHTaps,
	dml_uint_t WritebackVTaps,
	dml_uint_t WritebackSourceWidth,
	dml_uint_t WritebackDestinationWidth,
	dml_uint_t HTotal,
	dml_uint_t WritebackLineBufferSize,
	dml_float_t DISPCLKDPPCLKVCOSpeed);

static void CalculateVMRowAndSwath(
	struct display_mode_lib_scratch_st *s,
	struct CalculateVMRowAndSwath_params_st *p);

static void CalculateOutputLink(
	dml_float_t PHYCLKPerState,
	dml_float_t PHYCLKD18PerState,
	dml_float_t PHYCLKD32PerState,
	dml_float_t Downspreading,
	dml_bool_t IsMainSurfaceUsingTheIndicatedTiming,
	enum dml_output_encoder_class Output,
	enum dml_output_format_class OutputFormat,
	dml_uint_t HTotal,
	dml_uint_t HActive,
	dml_float_t PixelClockBackEnd,
	dml_float_t ForcedOutputLinkBPP,
	dml_uint_t DSCInputBitPerComponent,
	dml_uint_t NumberOfDSCSlices,
	dml_float_t AudioSampleRate,
	dml_uint_t AudioSampleLayout,
	enum dml_odm_mode ODMModeNoDSC,
	enum dml_odm_mode ODMModeDSC,
	enum dml_dsc_enable DSCEnable,
	dml_uint_t OutputLinkDPLanes,
	enum dml_output_link_dp_rate OutputLinkDPRate,

	// Output
	dml_bool_t *RequiresDSC,
	dml_bool_t *RequiresFEC,
	dml_float_t *OutBpp,
	enum dml_output_type_and_rate__type *OutputType,
	enum dml_output_type_and_rate__rate *OutputRate,
	dml_uint_t *RequiredSlots);

static void CalculateODMMode(
	dml_uint_t MaximumPixelsPerLinePerDSCUnit,
	dml_uint_t HActive,
	enum dml_output_encoder_class Output,
	enum dml_output_format_class OutputFormat,
	enum dml_odm_use_policy ODMUse,
	dml_float_t StateDispclk,
	dml_float_t MaxDispclk,
	dml_bool_t DSCEnable,
	dml_uint_t TotalNumberOfActiveDPP,
	dml_uint_t MaxNumDPP,
	dml_float_t PixelClock,
	dml_float_t DISPCLKDPPCLKDSCCLKDownSpreading,
	dml_float_t DISPCLKRampingMargin,
	dml_float_t DISPCLKDPPCLKVCOSpeed,

	// Output
	dml_bool_t *TotalAvailablePipesSupport,
	dml_uint_t *NumberOfDPP,
	enum dml_odm_mode *ODMMode,
	dml_float_t *RequiredDISPCLKPerSurface);

static dml_float_t CalculateRequiredDispclk(
	enum dml_odm_mode ODMMode,
	dml_float_t PixelClock,
	dml_float_t DISPCLKDPPCLKDSCCLKDownSpreading,
	dml_float_t DISPCLKRampingMargin,
	dml_float_t DISPCLKDPPCLKVCOSpeed,
	dml_float_t MaxDispclkSingle);

static void CalculateSinglePipeDPPCLKAndSCLThroughput(
	dml_float_t HRatio,
	dml_float_t HRatioChroma,
	dml_float_t VRatio,
	dml_float_t VRatioChroma,
	dml_float_t MaxDCHUBToPSCLThroughput,
	dml_float_t MaxPSCLToLBThroughput,
	dml_float_t PixelClock,
	enum dml_source_format_class SourcePixelFormat,
	dml_uint_t HTaps,
	dml_uint_t HTapsChroma,
	dml_uint_t VTaps,
	dml_uint_t VTapsChroma,

	// Output
	dml_float_t *PSCL_THROUGHPUT,
	dml_float_t *PSCL_THROUGHPUT_CHROMA,
	dml_float_t *DPPCLKUsingSingleDPP);

static void CalculateDPPCLK(
	dml_uint_t NumberOfActiveSurfaces,
	dml_float_t DISPCLKDPPCLKDSCCLKDownSpreading,
	dml_float_t DISPCLKDPPCLKVCOSpeed,
	dml_float_t DPPCLKUsingSingleDPP[],
	dml_uint_t DPPPerSurface[],

	// Output
	dml_float_t *GlobalDPPCLK,
	dml_float_t Dppclk[]);

static void CalculateMALLUseForStaticScreen(
	dml_uint_t NumberOfActiveSurfaces,
	dml_uint_t MALLAllocatedForDCNFinal,
	enum dml_use_mall_for_static_screen_mode *UseMALLForStaticScreen,
	dml_uint_t SurfaceSizeInMALL[],
	dml_bool_t one_row_per_frame_fits_in_buffer[],

	// Output
	dml_bool_t UsesMALLForStaticScreen[]);

static dml_uint_t dscceComputeDelay(
	dml_uint_t bpc,
	dml_float_t BPP,
	dml_uint_t sliceWidth,
	dml_uint_t numSlices,
	enum dml_output_format_class pixelFormat,
	enum dml_output_encoder_class Output);

static dml_uint_t dscComputeDelay(enum dml_output_format_class pixelFormat,
	enum dml_output_encoder_class Output);

static dml_bool_t CalculatePrefetchSchedule(struct display_mode_lib_scratch_st *scratch,
	struct CalculatePrefetchSchedule_params_st *p);

static dml_float_t RoundToDFSGranularity(dml_float_t Clock, dml_bool_t round_up, dml_float_t VCOSpeed);

static void CalculateDCCConfiguration(
	dml_bool_t DCCEnabled,
	dml_bool_t DCCProgrammingAssumesScanDirectionUnknown,
	enum dml_source_format_class SourcePixelFormat,
	dml_uint_t SurfaceWidthLuma,
	dml_uint_t SurfaceWidthChroma,
	dml_uint_t SurfaceHeightLuma,
	dml_uint_t SurfaceHeightChroma,
	dml_uint_t nomDETInKByte,
	dml_uint_t RequestHeight256ByteLuma,
	dml_uint_t RequestHeight256ByteChroma,
	enum dml_swizzle_mode TilingFormat,
	dml_uint_t BytePerPixelY,
	dml_uint_t BytePerPixelC,
	dml_float_t BytePerPixelDETY,
	dml_float_t BytePerPixelDETC,
	enum dml_rotation_angle SourceScan,
	// Output
	dml_uint_t *MaxUncompressedBlockLuma,
	dml_uint_t *MaxUncompressedBlockChroma,
	dml_uint_t *MaxCompressedBlockLuma,
	dml_uint_t *MaxCompressedBlockChroma,
	dml_uint_t *IndependentBlockLuma,
	dml_uint_t *IndependentBlockChroma);

static dml_uint_t CalculatePrefetchSourceLines(
	dml_float_t VRatio,
	dml_uint_t VTaps,
	dml_bool_t Interlace,
	dml_bool_t ProgressiveToInterlaceUnitInOPP,
	dml_uint_t SwathHeight,
	enum dml_rotation_angle SourceScan,
	dml_bool_t ViewportStationary,
	dml_uint_t SwathWidth,
	dml_uint_t ViewportHeight,
	dml_uint_t ViewportXStart,
	dml_uint_t ViewportYStart,

	// Output
	dml_uint_t *VInitPreFill,
	dml_uint_t *MaxNumSwath);

static dml_uint_t CalculateVMAndRowBytes(
	dml_bool_t ViewportStationary,
	dml_bool_t DCCEnable,
	dml_uint_t NumberOfDPPs,
	dml_uint_t BlockHeight256Bytes,
	dml_uint_t BlockWidth256Bytes,
	enum dml_source_format_class SourcePixelFormat,
	dml_uint_t SurfaceTiling,
	dml_uint_t BytePerPixel,
	enum dml_rotation_angle SourceScan,
	dml_uint_t SwathWidth,
	dml_uint_t ViewportHeight,
	dml_uint_t ViewportXStart,
	dml_uint_t ViewportYStart,
	dml_bool_t GPUVMEnable,
	dml_uint_t GPUVMMaxPageTableLevels,
	dml_uint_t GPUVMMinPageSizeKBytes,
	dml_uint_t PTEBufferSizeInRequests,
	dml_uint_t Pitch,
	dml_uint_t DCCMetaPitch,
	dml_uint_t MacroTileWidth,
	dml_uint_t MacroTileHeight,

	// Output
	dml_uint_t *MetaRowByte,
	dml_uint_t *PixelPTEBytesPerRow,
	dml_uint_t *PixelPTEBytesPerRowStorage, // for PTE buffer size check
	dml_uint_t *dpte_row_width_ub,
	dml_uint_t *dpte_row_height,
	dml_uint_t *dpte_row_height_linear,
	dml_uint_t *PixelPTEBytesPerRow_one_row_per_frame,
	dml_uint_t *dpte_row_width_ub_one_row_per_frame,
	dml_uint_t *dpte_row_height_one_row_per_frame,
	dml_uint_t *MetaRequestWidth,
	dml_uint_t *MetaRequestHeight,
	dml_uint_t *meta_row_width,
	dml_uint_t *meta_row_height,
	dml_uint_t *PixelPTEReqWidth,
	dml_uint_t *PixelPTEReqHeight,
	dml_uint_t *PTERequestSize,
	dml_uint_t *DPDE0BytesFrame,
	dml_uint_t *MetaPTEBytesFrame);

static dml_float_t CalculateTWait(
	dml_uint_t PrefetchMode,
	enum dml_use_mall_for_pstate_change_mode UseMALLForPStateChange,
	dml_bool_t SynchronizeDRRDisplaysForUCLKPStateChangeFinal,
	dml_bool_t DRRDisplay,
	dml_float_t DRAMClockChangeLatency,
	dml_float_t FCLKChangeLatency,
	dml_float_t UrgentLatency,
	dml_float_t SREnterPlusExitTime);

static void CalculatePrefetchMode(
	enum dml_prefetch_modes AllowForPStateChangeOrStutterInVBlank,
	dml_uint_t *MinPrefetchMode,
	dml_uint_t *MaxPrefetchMode);

static void CalculateRowBandwidth(
	dml_bool_t GPUVMEnable,
	enum dml_source_format_class SourcePixelFormat,
	dml_float_t VRatio,
	dml_float_t VRatioChroma,
	dml_bool_t DCCEnable,
	dml_float_t LineTime,
	dml_uint_t MetaRowByteLuma,
	dml_uint_t MetaRowByteChroma,
	dml_uint_t meta_row_height_luma,
	dml_uint_t meta_row_height_chroma,
	dml_uint_t PixelPTEBytesPerRowLuma,
	dml_uint_t PixelPTEBytesPerRowChroma,
	dml_uint_t dpte_row_height_luma,
	dml_uint_t dpte_row_height_chroma,
	// Output
	dml_float_t *meta_row_bw,
	dml_float_t *dpte_row_bw);

static void CalculateFlipSchedule(
	dml_float_t HostVMInefficiencyFactor,
	dml_float_t UrgentExtraLatency,
	dml_float_t UrgentLatency,
	dml_uint_t GPUVMMaxPageTableLevels,
	dml_bool_t HostVMEnable,
	dml_uint_t HostVMMaxNonCachedPageTableLevels,
	dml_bool_t GPUVMEnable,
	dml_uint_t HostVMMinPageSize,
	dml_float_t PDEAndMetaPTEBytesPerFrame,
	dml_float_t MetaRowBytes,
	dml_float_t DPTEBytesPerRow,
	dml_float_t BandwidthAvailableForImmediateFlip,
	dml_uint_t TotImmediateFlipBytes,
	enum dml_source_format_class SourcePixelFormat,
	dml_float_t LineTime,
	dml_float_t VRatio,
	dml_float_t VRatioChroma,
	dml_float_t Tno_bw,
	dml_bool_t DCCEnable,
	dml_uint_t dpte_row_height,
	dml_uint_t meta_row_height,
	dml_uint_t dpte_row_height_chroma,
	dml_uint_t meta_row_height_chroma,
	dml_bool_t use_one_row_for_frame_flip,

	// Output
	dml_float_t *DestinationLinesToRequestVMInImmediateFlip,
	dml_float_t *DestinationLinesToRequestRowInImmediateFlip,
	dml_float_t *final_flip_bw,
	dml_bool_t *ImmediateFlipSupportedForPipe);

static dml_float_t CalculateWriteBackDelay(
	enum dml_source_format_class WritebackPixelFormat,
	dml_float_t WritebackHRatio,
	dml_float_t WritebackVRatio,
	dml_uint_t WritebackVTaps,
	dml_uint_t WritebackDestinationWidth,
	dml_uint_t WritebackDestinationHeight,
	dml_uint_t WritebackSourceHeight,
	dml_uint_t HTotal);

static void CalculateVUpdateAndDynamicMetadataParameters(
	dml_uint_t MaxInterDCNTileRepeaters,
	dml_float_t Dppclk,
	dml_float_t DISPCLK,
	dml_float_t DCFClkDeepSleep,
	dml_float_t PixelClock,
	dml_uint_t HTotal,
	dml_uint_t VBlank,
	dml_uint_t DynamicMetadataTransmittedBytes,
	dml_uint_t DynamicMetadataLinesBeforeActiveRequired,
	dml_uint_t InterlaceEnable,
	dml_bool_t ProgressiveToInterlaceUnitInOPP,
	dml_float_t *TSetup,
	dml_float_t *Tdmbf,
	dml_float_t *Tdmec,
	dml_float_t *Tdmsks,
	dml_uint_t *VUpdateOffsetPix,
	dml_uint_t *VUpdateWidthPix,
	dml_uint_t *VReadyOffsetPix);

static void PixelClockAdjustmentForProgressiveToInterlaceUnit(struct dml_display_cfg_st *display_cfg, dml_bool_t ptoi_supported);

static dml_float_t TruncToValidBPP(
	dml_float_t LinkBitRate,
	dml_uint_t Lanes,
	dml_uint_t HTotal,
	dml_uint_t HActive,
	dml_float_t PixelClock,
	dml_float_t DesiredBPP,
	dml_bool_t DSCEnable,
	enum dml_output_encoder_class Output,
	enum dml_output_format_class Format,
	dml_uint_t DSCInputBitPerComponent,
	dml_uint_t DSCSlices,
	dml_uint_t AudioRate,
	dml_uint_t AudioLayout,
	enum dml_odm_mode ODMModeNoDSC,
	enum dml_odm_mode ODMModeDSC,
	// Output
	dml_uint_t *RequiredSlotsSingle);

static void CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport(
	struct display_mode_lib_scratch_st *s,
	struct CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params_st *p);

static void CalculateDCFCLKDeepSleep(
	dml_uint_t NumberOfActiveSurfaces,
	dml_uint_t BytePerPixelY[],
	dml_uint_t BytePerPixelC[],
	dml_float_t VRatio[],
	dml_float_t VRatioChroma[],
	dml_uint_t SwathWidthY[],
	dml_uint_t SwathWidthC[],
	dml_uint_t DPPPerSurface[],
	dml_float_t HRatio[],
	dml_float_t HRatioChroma[],
	dml_float_t PixelClock[],
	dml_float_t PSCL_THROUGHPUT[],
	dml_float_t PSCL_THROUGHPUT_CHROMA[],
	dml_float_t Dppclk[],
	dml_float_t ReadBandwidthLuma[],
	dml_float_t ReadBandwidthChroma[],
	dml_uint_t ReturnBusWidth,

	// Output
	dml_float_t *DCFCLKDeepSleep);

static void CalculateUrgentBurstFactor(
	enum dml_use_mall_for_pstate_change_mode UseMALLForPStateChange,
	dml_uint_t swath_width_luma_ub,
	dml_uint_t swath_width_chroma_ub,
	dml_uint_t SwathHeightY,
	dml_uint_t SwathHeightC,
	dml_float_t LineTime,
	dml_float_t UrgentLatency,
	dml_float_t CursorBufferSize,
	dml_uint_t CursorWidth,
	dml_uint_t CursorBPP,
	dml_float_t VRatio,
	dml_float_t VRatioC,
	dml_float_t BytePerPixelInDETY,
	dml_float_t BytePerPixelInDETC,
	dml_uint_t DETBufferSizeY,
	dml_uint_t DETBufferSizeC,
	// Output
	dml_float_t *UrgentBurstFactorCursor,
	dml_float_t *UrgentBurstFactorLuma,
	dml_float_t *UrgentBurstFactorChroma,
	dml_bool_t *NotEnoughUrgentLatencyHiding);

static dml_float_t RequiredDTBCLK(
	dml_bool_t DSCEnable,
	dml_float_t PixelClock,
	enum dml_output_format_class OutputFormat,
	dml_float_t OutputBpp,
	dml_uint_t DSCSlices,
	dml_uint_t HTotal,
	dml_uint_t HActive,
	dml_uint_t AudioRate,
	dml_uint_t AudioLayoutSingle);

static void UseMinimumDCFCLK(
	struct display_mode_lib_scratch_st *scratch,
	struct UseMinimumDCFCLK_params_st *p);

static void CalculatePixelDeliveryTimes(
	dml_uint_t NumberOfActiveSurfaces,
	dml_float_t VRatio[],
	dml_float_t VRatioChroma[],
	dml_float_t VRatioPrefetchY[],
	dml_float_t VRatioPrefetchC[],
	dml_uint_t swath_width_luma_ub[],
	dml_uint_t swath_width_chroma_ub[],
	dml_uint_t DPPPerSurface[],
	dml_float_t HRatio[],
	dml_float_t HRatioChroma[],
	dml_float_t PixelClock[],
	dml_float_t PSCL_THROUGHPUT[],
	dml_float_t PSCL_THROUGHPUT_CHROMA[],
	dml_float_t Dppclk[],
	dml_uint_t BytePerPixelC[],
	enum dml_rotation_angle SourceScan[],
	dml_uint_t NumberOfCursors[],
	dml_uint_t CursorWidth[],
	dml_uint_t CursorBPP[],
	dml_uint_t BlockWidth256BytesY[],
	dml_uint_t BlockHeight256BytesY[],
	dml_uint_t BlockWidth256BytesC[],
	dml_uint_t BlockHeight256BytesC[],

	// Output
	dml_float_t DisplayPipeLineDeliveryTimeLuma[],
	dml_float_t DisplayPipeLineDeliveryTimeChroma[],
	dml_float_t DisplayPipeLineDeliveryTimeLumaPrefetch[],
	dml_float_t DisplayPipeLineDeliveryTimeChromaPrefetch[],
	dml_float_t DisplayPipeRequestDeliveryTimeLuma[],
	dml_float_t DisplayPipeRequestDeliveryTimeChroma[],
	dml_float_t DisplayPipeRequestDeliveryTimeLumaPrefetch[],
	dml_float_t DisplayPipeRequestDeliveryTimeChromaPrefetch[],
	dml_float_t CursorRequestDeliveryTime[],
	dml_float_t CursorRequestDeliveryTimePrefetch[]);

static void CalculateMetaAndPTETimes(
	dml_bool_t use_one_row_for_frame[],
	dml_uint_t NumberOfActiveSurfaces,
	dml_bool_t GPUVMEnable,
	dml_uint_t MetaChunkSize,
	dml_uint_t MinMetaChunkSizeBytes,
	dml_uint_t HTotal[],
	dml_float_t VRatio[],
	dml_float_t VRatioChroma[],
	dml_float_t DestinationLinesToRequestRowInVBlank[],
	dml_float_t DestinationLinesToRequestRowInImmediateFlip[],
	dml_bool_t DCCEnable[],
	dml_float_t PixelClock[],
	dml_uint_t BytePerPixelY[],
	dml_uint_t BytePerPixelC[],
	enum dml_rotation_angle SourceScan[],
	dml_uint_t dpte_row_height[],
	dml_uint_t dpte_row_height_chroma[],
	dml_uint_t meta_row_width[],
	dml_uint_t meta_row_width_chroma[],
	dml_uint_t meta_row_height[],
	dml_uint_t meta_row_height_chroma[],
	dml_uint_t meta_req_width[],
	dml_uint_t meta_req_width_chroma[],
	dml_uint_t meta_req_height[],
	dml_uint_t meta_req_height_chroma[],
	dml_uint_t dpte_group_bytes[],
	dml_uint_t PTERequestSizeY[],
	dml_uint_t PTERequestSizeC[],
	dml_uint_t PixelPTEReqWidthY[],
	dml_uint_t PixelPTEReqHeightY[],
	dml_uint_t PixelPTEReqWidthC[],
	dml_uint_t PixelPTEReqHeightC[],
	dml_uint_t dpte_row_width_luma_ub[],
	dml_uint_t dpte_row_width_chroma_ub[],

	// Output
	dml_float_t DST_Y_PER_PTE_ROW_NOM_L[],
	dml_float_t DST_Y_PER_PTE_ROW_NOM_C[],
	dml_float_t DST_Y_PER_META_ROW_NOM_L[],
	dml_float_t DST_Y_PER_META_ROW_NOM_C[],
	dml_float_t TimePerMetaChunkNominal[],
	dml_float_t TimePerChromaMetaChunkNominal[],
	dml_float_t TimePerMetaChunkVBlank[],
	dml_float_t TimePerChromaMetaChunkVBlank[],
	dml_float_t TimePerMetaChunkFlip[],
	dml_float_t TimePerChromaMetaChunkFlip[],
	dml_float_t time_per_pte_group_nom_luma[],
	dml_float_t time_per_pte_group_vblank_luma[],
	dml_float_t time_per_pte_group_flip_luma[],
	dml_float_t time_per_pte_group_nom_chroma[],
	dml_float_t time_per_pte_group_vblank_chroma[],
	dml_float_t time_per_pte_group_flip_chroma[]);

static void CalculateVMGroupAndRequestTimes(
	dml_uint_t NumberOfActiveSurfaces,
	dml_bool_t GPUVMEnable,
	dml_uint_t GPUVMMaxPageTableLevels,
	dml_uint_t HTotal[],
	dml_uint_t BytePerPixelC[],
	dml_float_t DestinationLinesToRequestVMInVBlank[],
	dml_float_t DestinationLinesToRequestVMInImmediateFlip[],
	dml_bool_t DCCEnable[],
	dml_float_t PixelClock[],
	dml_uint_t dpte_row_width_luma_ub[],
	dml_uint_t dpte_row_width_chroma_ub[],
	dml_uint_t vm_group_bytes[],
	dml_uint_t dpde0_bytes_per_frame_ub_l[],
	dml_uint_t dpde0_bytes_per_frame_ub_c[],
	dml_uint_t meta_pte_bytes_per_frame_ub_l[],
	dml_uint_t meta_pte_bytes_per_frame_ub_c[],

	// Output
	dml_float_t TimePerVMGroupVBlank[],
	dml_float_t TimePerVMGroupFlip[],
	dml_float_t TimePerVMRequestVBlank[],
	dml_float_t TimePerVMRequestFlip[]);

static void CalculateStutterEfficiency(
	struct display_mode_lib_scratch_st *scratch,
	struct CalculateStutterEfficiency_params_st *p);

static void CalculateSwathAndDETConfiguration(
	struct display_mode_lib_scratch_st *scratch,
	struct CalculateSwathAndDETConfiguration_params_st *p);

static void CalculateSwathWidth(
	dml_bool_t ForceSingleDPP,
	dml_uint_t NumberOfActiveSurfaces,
	enum dml_source_format_class SourcePixelFormat[],
	enum dml_rotation_angle SourceScan[],
	dml_bool_t ViewportStationary[],
	dml_uint_t ViewportWidth[],
	dml_uint_t ViewportHeight[],
	dml_uint_t ViewportXStart[],
	dml_uint_t ViewportYStart[],
	dml_uint_t ViewportXStartC[],
	dml_uint_t ViewportYStartC[],
	dml_uint_t SurfaceWidthY[],
	dml_uint_t SurfaceWidthC[],
	dml_uint_t SurfaceHeightY[],
	dml_uint_t SurfaceHeightC[],
	enum dml_odm_mode ODMMode[],
	dml_uint_t BytePerPixY[],
	dml_uint_t BytePerPixC[],
	dml_uint_t Read256BytesBlockHeightY[],
	dml_uint_t Read256BytesBlockHeightC[],
	dml_uint_t Read256BytesBlockWidthY[],
	dml_uint_t Read256BytesBlockWidthC[],
	dml_uint_t BlendingAndTiming[],
	dml_uint_t HActive[],
	dml_float_t HRatio[],
	dml_uint_t DPPPerSurface[],

	// Output
	dml_uint_t SwathWidthSingleDPPY[],
	dml_uint_t SwathWidthSingleDPPC[],
	dml_uint_t SwathWidthY[],
	dml_uint_t SwathWidthC[],
	dml_uint_t MaximumSwathHeightY[],
	dml_uint_t MaximumSwathHeightC[],
	dml_uint_t swath_width_luma_ub[],
	dml_uint_t swath_width_chroma_ub[]);

static dml_float_t CalculateExtraLatency(
	dml_uint_t RoundTripPingLatencyCycles,
	dml_uint_t ReorderingBytes,
	dml_float_t DCFCLK,
	dml_uint_t TotalNumberOfActiveDPP,
	dml_uint_t PixelChunkSizeInKByte,
	dml_uint_t TotalNumberOfDCCActiveDPP,
	dml_uint_t MetaChunkSize,
	dml_float_t ReturnBW,
	dml_bool_t GPUVMEnable,
	dml_bool_t HostVMEnable,
	dml_uint_t NumberOfActiveSurfaces,
	dml_uint_t NumberOfDPP[],
	dml_uint_t dpte_group_bytes[],
	dml_float_t HostVMInefficiencyFactor,
	dml_uint_t HostVMMinPageSize,
	dml_uint_t HostVMMaxNonCachedPageTableLevels);

static dml_uint_t CalculateExtraLatencyBytes(
	dml_uint_t ReorderingBytes,
	dml_uint_t TotalNumberOfActiveDPP,
	dml_uint_t PixelChunkSizeInKByte,
	dml_uint_t TotalNumberOfDCCActiveDPP,
	dml_uint_t MetaChunkSize,
	dml_bool_t GPUVMEnable,
	dml_bool_t HostVMEnable,
	dml_uint_t NumberOfActiveSurfaces,
	dml_uint_t NumberOfDPP[],
	dml_uint_t dpte_group_bytes[],
	dml_float_t HostVMInefficiencyFactor,
	dml_uint_t HostVMMinPageSize,
	dml_uint_t HostVMMaxNonCachedPageTableLevels);

static dml_float_t CalculateUrgentLatency(
	dml_float_t UrgentLatencyPixelDataOnly,
	dml_float_t UrgentLatencyPixelMixedWithVMData,
	dml_float_t UrgentLatencyVMDataOnly,
	dml_bool_t DoUrgentLatencyAdjustment,
	dml_float_t UrgentLatencyAdjustmentFabricClockComponent,
	dml_float_t UrgentLatencyAdjustmentFabricClockReference,
	dml_float_t FabricClockSingle);

static dml_bool_t UnboundedRequest(
	enum dml_unbounded_requesting_policy UseUnboundedRequestingFinal,
	dml_uint_t TotalNumberOfActiveDPP,
	dml_bool_t NoChromaOrLinear,
	enum dml_output_encoder_class Output);

static void CalculateSurfaceSizeInMall(
	dml_uint_t NumberOfActiveSurfaces,
	dml_uint_t MALLAllocatedForDCN,
	enum dml_use_mall_for_static_screen_mode UseMALLForStaticScreen[],
	dml_bool_t DCCEnable[],
	dml_bool_t ViewportStationary[],
	dml_uint_t ViewportXStartY[],
	dml_uint_t ViewportYStartY[],
	dml_uint_t ViewportXStartC[],
	dml_uint_t ViewportYStartC[],
	dml_uint_t ViewportWidthY[],
	dml_uint_t ViewportHeightY[],
	dml_uint_t BytesPerPixelY[],
	dml_uint_t ViewportWidthC[],
	dml_uint_t ViewportHeightC[],
	dml_uint_t BytesPerPixelC[],
	dml_uint_t SurfaceWidthY[],
	dml_uint_t SurfaceWidthC[],
	dml_uint_t SurfaceHeightY[],
	dml_uint_t SurfaceHeightC[],
	dml_uint_t Read256BytesBlockWidthY[],
	dml_uint_t Read256BytesBlockWidthC[],
	dml_uint_t Read256BytesBlockHeightY[],
	dml_uint_t Read256BytesBlockHeightC[],
	dml_uint_t ReadBlockWidthY[],
	dml_uint_t ReadBlockWidthC[],
	dml_uint_t ReadBlockHeightY[],
	dml_uint_t ReadBlockHeightC[],

	// Output
	dml_uint_t SurfaceSizeInMALL[],
	dml_bool_t *ExceededMALLSize);

static void CalculateDETBufferSize(
	dml_uint_t DETSizeOverride[],
	enum dml_use_mall_for_pstate_change_mode UseMALLForPStateChange[],
	dml_bool_t ForceSingleDPP,
	dml_uint_t NumberOfActiveSurfaces,
	dml_bool_t UnboundedRequestEnabled,
	dml_uint_t nomDETInKByte,
	dml_uint_t MaxTotalDETInKByte,
	dml_uint_t ConfigReturnBufferSizeInKByte,
	dml_uint_t MinCompressedBufferSizeInKByte,
	dml_uint_t ConfigReturnBufferSegmentSizeInkByte,
	dml_uint_t CompressedBufferSegmentSizeInkByteFinal,
	enum dml_source_format_class SourcePixelFormat[],
	dml_float_t ReadBandwidthLuma[],
	dml_float_t ReadBandwidthChroma[],
	dml_uint_t RotesY[],
	dml_uint_t RoundedUpMaxSwathSizeBytesC[],
	dml_uint_t DPPPerSurface[],
	// Output
	dml_uint_t DETBufferSizeInKByte[],
	dml_uint_t *CompressedBufferSizeInkByte);

static void CalculateMaxDETAndMinCompressedBufferSize(
	dml_uint_t ConfigReturnBufferSizeInKByte,
	dml_uint_t ConfigReturnBufferSegmentSizeInKByte,
	dml_uint_t ROBBufferSizeInKByte,
	dml_uint_t MaxNumDPP,
	dml_bool_t nomDETInKByteOverrideEnable,
	dml_uint_t nomDETInKByteOverrideValue,

	// Output
	dml_uint_t *MaxTotalDETInKByte,
	dml_uint_t *nomDETInKByte,
	dml_uint_t *MinCompressedBufferSizeInKByte);

static dml_uint_t DSCDelayRequirement(
	dml_bool_t DSCEnabled,
	enum dml_odm_mode ODMMode,
	dml_uint_t DSCInputBitPerComponent,
	dml_float_t OutputBpp,
	dml_uint_t HActive,
	dml_uint_t HTotal,
	dml_uint_t NumberOfDSCSlices,
	enum dml_output_format_class OutputFormat,
	enum dml_output_encoder_class Output,
	dml_float_t PixelClock,
	dml_float_t PixelClockBackEnd);

static dml_bool_t CalculateVActiveBandwithSupport(
	dml_uint_t NumberOfActiveSurfaces,
	dml_float_t ReturnBW,
	dml_bool_t NotUrgentLatencyHiding[],
	dml_float_t ReadBandwidthLuma[],
	dml_float_t ReadBandwidthChroma[],
	dml_float_t cursor_bw[],
	dml_float_t meta_row_bandwidth[],
	dml_float_t dpte_row_bandwidth[],
	dml_uint_t NumberOfDPP[],
	dml_float_t UrgentBurstFactorLuma[],
	dml_float_t UrgentBurstFactorChroma[],
	dml_float_t UrgentBurstFactorCursor[]);

static void CalculatePrefetchBandwithSupport(
	dml_uint_t NumberOfActiveSurfaces,
	dml_float_t ReturnBW,
	enum dml_use_mall_for_pstate_change_mode UseMALLForPStateChange[],
	dml_bool_t NotUrgentLatencyHiding[],
	dml_float_t ReadBandwidthLuma[],
	dml_float_t ReadBandwidthChroma[],
	dml_float_t PrefetchBandwidthLuma[],
	dml_float_t PrefetchBandwidthChroma[],
	dml_float_t cursor_bw[],
	dml_float_t meta_row_bandwidth[],
	dml_float_t dpte_row_bandwidth[],
	dml_float_t cursor_bw_pre[],
	dml_float_t prefetch_vmrow_bw[],
	dml_uint_t NumberOfDPP[],
	dml_float_t UrgentBurstFactorLuma[],
	dml_float_t UrgentBurstFactorChroma[],
	dml_float_t UrgentBurstFactorCursor[],
	dml_float_t UrgentBurstFactorLumaPre[],
	dml_float_t UrgentBurstFactorChromaPre[],
	dml_float_t UrgentBurstFactorCursorPre[],

	// Output
	dml_float_t *PrefetchBandwidth,
	dml_float_t *PrefetchBandwidthNotIncludingMALLPrefetch,
	dml_float_t *FractionOfUrgentBandwidth,
	dml_bool_t *PrefetchBandwidthSupport);

static dml_float_t CalculateBandwidthAvailableForImmediateFlip(
	dml_uint_t NumberOfActiveSurfaces,
	dml_float_t ReturnBW,
	dml_float_t ReadBandwidthLuma[],
	dml_float_t ReadBandwidthChroma[],
	dml_float_t PrefetchBandwidthLuma[],
	dml_float_t PrefetchBandwidthChroma[],
	dml_float_t cursor_bw[],
	dml_float_t cursor_bw_pre[],
	dml_uint_t NumberOfDPP[],
	dml_float_t UrgentBurstFactorLuma[],
	dml_float_t UrgentBurstFactorChroma[],
	dml_float_t UrgentBurstFactorCursor[],
	dml_float_t UrgentBurstFactorLumaPre[],
	dml_float_t UrgentBurstFactorChromaPre[],
	dml_float_t UrgentBurstFactorCursorPre[]);

static void CalculateImmediateFlipBandwithSupport(
	dml_uint_t NumberOfActiveSurfaces,
	dml_float_t ReturnBW,
	enum dml_use_mall_for_pstate_change_mode UseMALLForPStateChange[],
	enum dml_immediate_flip_requirement ImmediateFlipRequirement[],
	dml_float_t final_flip_bw[],
	dml_float_t ReadBandwidthLuma[],
	dml_float_t ReadBandwidthChroma[],
	dml_float_t PrefetchBandwidthLuma[],
	dml_float_t PrefetchBandwidthChroma[],
	dml_float_t cursor_bw[],
	dml_float_t meta_row_bandwidth[],
	dml_float_t dpte_row_bandwidth[],
	dml_float_t cursor_bw_pre[],
	dml_float_t prefetch_vmrow_bw[],
	dml_uint_t NumberOfDPP[],
	dml_float_t UrgentBurstFactorLuma[],
	dml_float_t UrgentBurstFactorChroma[],
	dml_float_t UrgentBurstFactorCursor[],
	dml_float_t UrgentBurstFactorLumaPre[],
	dml_float_t UrgentBurstFactorChromaPre[],
	dml_float_t UrgentBurstFactorCursorPre[],

	// Output
	dml_float_t *TotalBandwidth,
	dml_float_t *TotalBandwidthNotIncludingMALLPrefetch,
	dml_float_t *FractionOfUrgentBandwidth,
	dml_bool_t *ImmediateFlipBandwidthSupport);

// ---------------------------
//  Declaration Ends
// ---------------------------

static dml_uint_t dscceComputeDelay(
	dml_uint_t bpc,
	dml_float_t BPP,
	dml_uint_t sliceWidth,
	dml_uint_t numSlices,
	enum dml_output_format_class pixelFormat,
	enum dml_output_encoder_class Output)
{
	// valid bpc = source bits per component in the set of {8, 10, 12}
	// valid bpp = increments of 1/16 of a bit
	// min = 6/7/8 in N420/N422/444, respectively
	// max = such that compression is 1:1
	//valid sliceWidth = number of pixels per slice line, must be less than or equal to 5184/numSlices (or 4096/numSlices in 420 mode)
	//valid numSlices = number of slices in the horiziontal direction per DSC engine in the set of {1, 2, 3, 4}
	//valid pixelFormat = pixel/color format in the set of {:N444_RGB, :S422, :N422, :N420}

	// fixed value
	dml_uint_t rcModelSize = 8192;

	// N422/N420 operate at 2 pixels per clock
	dml_uint_t pixelsPerClock, lstall, D, initalXmitDelay, w, s, ix, wx, p, l0, a, ax, L,
		Delay, pixels;

	if (pixelFormat == dml_420)
		pixelsPerClock = 2;
	// #all other modes operate at 1 pixel per clock
	else if (pixelFormat == dml_444)
		pixelsPerClock = 1;
	else if (pixelFormat == dml_n422)
		pixelsPerClock = 2;
	else
		pixelsPerClock = 1;

	//initial transmit delay as per PPS
	initalXmitDelay = (dml_uint_t)(dml_round(rcModelSize / 2.0 / BPP / pixelsPerClock, 1));

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
	if (pixelFormat == dml_420 || pixelFormat == dml_444 || pixelFormat == dml_n422)
		s = 0;
	else
		s = 1;

	//main calculation for the dscce
	ix = initalXmitDelay + 45;
	wx = (w + 2) / 3;
	p = 3 * wx - w;
	l0 = ix / w;
	a = ix + p * l0;
	ax = (a + 2) / 3 + D + 6 + 1;
	L = (ax + wx - 1) / wx;
	if ((ix % w) == 0 && p != 0)
		lstall = 1;
	else
		lstall = 0;
	Delay = L * wx * (numSlices - 1) + ax + s + lstall + 22;

	//dsc processes 3 pixel containers per cycle and a container can contain 1 or 2 pixels
	pixels = Delay * 3 * pixelsPerClock;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: bpc: %u\n", __func__, bpc);
	dml_print("DML::%s: BPP: %f\n", __func__, BPP);
	dml_print("DML::%s: sliceWidth: %u\n", __func__, sliceWidth);
	dml_print("DML::%s: numSlices: %u\n", __func__, numSlices);
	dml_print("DML::%s: pixelFormat: %u\n", __func__, pixelFormat);
	dml_print("DML::%s: Output: %u\n", __func__, Output);
	dml_print("DML::%s: pixels: %u\n", __func__, pixels);
#endif
	return pixels;
}

static dml_uint_t dscComputeDelay(enum dml_output_format_class pixelFormat, enum dml_output_encoder_class Output)
{
	dml_uint_t Delay = 0;

	if (pixelFormat == dml_420) {
		// sfr
		Delay = Delay + 2;
		// dsccif
		Delay = Delay + 0;
		// dscc - input deserializer
		Delay = Delay + 3;
		// dscc gets pixels every other cycle
		Delay = Delay + 2;
		// dscc - input cdc fifo
		Delay = Delay + 12;
		// dscc gets pixels every other cycle
		Delay = Delay + 13;
		// dscc - cdc uncertainty
		Delay = Delay + 2;
		// dscc - output cdc fifo
		Delay = Delay + 7;
		// dscc gets pixels every other cycle
		Delay = Delay + 3;
		// dscc - cdc uncertainty
		Delay = Delay + 2;
		// dscc - output serializer
		Delay = Delay + 1;
		// sft
		Delay = Delay + 1;
	} else if (pixelFormat == dml_n422) {
	// sfr
	Delay = Delay + 2;
	// dsccif
	Delay = Delay + 1;
	// dscc - input deserializer
	Delay = Delay + 5;
	// dscc - input cdc fifo
	Delay = Delay + 25;
	// dscc - cdc uncertainty
	Delay = Delay + 2;
	// dscc - output cdc fifo
	Delay = Delay + 10;
	// dscc - cdc uncertainty
	Delay = Delay + 2;
	// dscc - output serializer
	Delay = Delay + 1;
	// sft
	Delay = Delay + 1;
	} else {
	// sfr
	Delay = Delay + 2;
	// dsccif
	Delay = Delay + 0;
	// dscc - input deserializer
	Delay = Delay + 3;
	// dscc - input cdc fifo
	Delay = Delay + 12;
	// dscc - cdc uncertainty
	Delay = Delay + 2;
	// dscc - output cdc fifo
	Delay = Delay + 7;
	// dscc - output serializer
	Delay = Delay + 1;
	// dscc - cdc uncertainty
	Delay = Delay + 2;
	// sft
	Delay = Delay + 1;
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: pixelFormat = %u\n", __func__, pixelFormat);
	dml_print("DML::%s: Delay = %u\n", __func__, Delay);
#endif

	return Delay;
}

static dml_bool_t CalculatePrefetchSchedule(struct display_mode_lib_scratch_st *scratch,
	struct CalculatePrefetchSchedule_params_st *p)
{
	struct CalculatePrefetchSchedule_locals_st *s = &scratch->CalculatePrefetchSchedule_locals;

	s->MyError = false;
	s->DPPCycles = 0;
	s->DISPCLKCycles = 0;
	s->DSTTotalPixelsAfterScaler = 0.0;
	s->LineTime = 0.0;
	s->dst_y_prefetch_equ = 0.0;
	s->prefetch_bw_oto = 0.0;
	s->Tvm_oto = 0.0;
	s->Tr0_oto = 0.0;
	s->Tvm_oto_lines = 0.0;
	s->Tr0_oto_lines = 0.0;
	s->dst_y_prefetch_oto = 0.0;
	s->TimeForFetchingMetaPTE = 0.0;
	s->TimeForFetchingRowInVBlank = 0.0;
	s->LinesToRequestPrefetchPixelData = 0.0;
	s->HostVMDynamicLevelsTrips = 0;
	s->trip_to_mem = 0.0;
	s->Tvm_trips = 0.0;
	s->Tr0_trips = 0.0;
	s->Tvm_trips_rounded = 0.0;
	s->Tr0_trips_rounded = 0.0;
	s->max_Tsw = 0.0;
	s->Lsw_oto = 0.0;
	s->Tpre_rounded = 0.0;
	s->prefetch_bw_equ = 0.0;
	s->Tvm_equ = 0.0;
	s->Tr0_equ = 0.0;
	s->Tdmbf = 0.0;
	s->Tdmec = 0.0;
	s->Tdmsks = 0.0;
	s->prefetch_sw_bytes = 0.0;
	s->prefetch_bw_pr = 0.0;
	s->bytes_pp = 0.0;
	s->dep_bytes = 0.0;
	s->min_Lsw_oto = 0.0;
	s->Tsw_est1 = 0.0;
	s->Tsw_est3 = 0.0;

	if (p->GPUVMEnable == true && p->HostVMEnable == true) {
		s->HostVMDynamicLevelsTrips = p->HostVMMaxNonCachedPageTableLevels;
	} else {
		s->HostVMDynamicLevelsTrips = 0;
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: GPUVMEnable = %u\n", __func__, p->GPUVMEnable);
	dml_print("DML::%s: GPUVMPageTableLevels = %u\n", __func__, p->GPUVMPageTableLevels);
	dml_print("DML::%s: DCCEnable = %u\n", __func__, p->myPipe->DCCEnable);
	dml_print("DML::%s: VStartup = %u\n", __func__, p->VStartup);
	dml_print("DML::%s: MaxVStartup = %u\n", __func__, p->MaxVStartup);
	dml_print("DML::%s: HostVMEnable = %u\n", __func__, p->HostVMEnable);
	dml_print("DML::%s: HostVMInefficiencyFactor= %f\n", __func__, p->HostVMInefficiencyFactor);
	dml_print("DML::%s: myPipe->Dppclk = %f\n", __func__, p->myPipe->Dppclk);
#endif
	CalculateVUpdateAndDynamicMetadataParameters(
	p->MaxInterDCNTileRepeaters,
	p->myPipe->Dppclk,
	p->myPipe->Dispclk,
	p->myPipe->DCFClkDeepSleep,
	p->myPipe->PixelClock,
	p->myPipe->HTotal,
	p->myPipe->VBlank,
	p->DynamicMetadataTransmittedBytes,
	p->DynamicMetadataLinesBeforeActiveRequired,
	p->myPipe->InterlaceEnable,
	p->myPipe->ProgressiveToInterlaceUnitInOPP,
	p->TSetup,

	// Output
	&s->Tdmbf,
	&s->Tdmec,
	&s->Tdmsks,
	p->VUpdateOffsetPix,
	p->VUpdateWidthPix,
	p->VReadyOffsetPix);

	s->LineTime = p->myPipe->HTotal / p->myPipe->PixelClock;
	s->trip_to_mem = p->UrgentLatency;
	s->Tvm_trips = p->UrgentExtraLatency + s->trip_to_mem * (p->GPUVMPageTableLevels * (s->HostVMDynamicLevelsTrips + 1) - 1);

	if (p->DynamicMetadataVMEnabled == true) {
		*p->Tdmdl = p->TWait + s->Tvm_trips + s->trip_to_mem;
	} else {
		*p->Tdmdl = p->TWait + p->UrgentExtraLatency;
	}

#ifdef __DML_VBA_ALLOW_DELTA__
	if (DynamicMetadataEnable == false) {
		*Tdmdl = 0.0;
	}
#endif

	if (p->DynamicMetadataEnable == true) {
		if (p->VStartup * s->LineTime < *p->TSetup + *p->Tdmdl + s->Tdmbf + s->Tdmec + s->Tdmsks) {
			*p->NotEnoughTimeForDynamicMetadata = true;
			dml_print("DML::%s: Not Enough Time for Dynamic Meta!\n", __func__);
			dml_print("DML::%s: Tdmbf: %fus - time for dmd transfer from dchub to dio output buffer\n", __func__, s->Tdmbf);
			dml_print("DML::%s: Tdmec: %fus - time dio takes to transfer dmd\n", __func__, s->Tdmec);
			dml_print("DML::%s: Tdmsks: %fus - time before active dmd must complete transmission at dio\n", __func__, s->Tdmsks);
			dml_print("DML::%s: Tdmdl: %fus - time for fabric to become ready and fetch dmd \n", __func__, *p->Tdmdl);
		} else {
			*p->NotEnoughTimeForDynamicMetadata = false;
		}
	} else {
		*p->NotEnoughTimeForDynamicMetadata = false;
	}

	*p->Tdmdl_vm = (p->DynamicMetadataEnable == true && p->DynamicMetadataVMEnabled == true && p->GPUVMEnable == true ? p->TWait + s->Tvm_trips : 0);

	if (p->myPipe->ScalerEnabled)
		s->DPPCycles = (dml_uint_t)(p->DPPCLKDelaySubtotalPlusCNVCFormater + p->DPPCLKDelaySCL);
	else
		s->DPPCycles = (dml_uint_t)(p->DPPCLKDelaySubtotalPlusCNVCFormater + p->DPPCLKDelaySCLLBOnly);

	s->DPPCycles = (dml_uint_t)(s->DPPCycles + p->myPipe->NumberOfCursors * p->DPPCLKDelayCNVCCursor);

	s->DISPCLKCycles = (dml_uint_t)p->DISPCLKDelaySubtotal;

	if (p->myPipe->Dppclk == 0.0 || p->myPipe->Dispclk == 0.0)
		return true;

	*p->DSTXAfterScaler = (dml_uint_t) dml_round(s->DPPCycles * p->myPipe->PixelClock / p->myPipe->Dppclk + s->DISPCLKCycles * p->myPipe->PixelClock / p->myPipe->Dispclk + p->DSCDelay, 1.0);
	*p->DSTXAfterScaler = (dml_uint_t) dml_round(*p->DSTXAfterScaler + (p->myPipe->ODMMode != dml_odm_mode_bypass ? 18 : 0) + (p->myPipe->DPPPerSurface - 1) * p->DPP_RECOUT_WIDTH +
						((p->myPipe->ODMMode == dml_odm_mode_split_1to2 || p->myPipe->ODMMode == dml_odm_mode_mso_1to2) ? (dml_float_t)p->myPipe->HActive / 2.0 : 0) +
						((p->myPipe->ODMMode == dml_odm_mode_mso_1to4) ? (dml_float_t)p->myPipe->HActive * 3.0 / 4.0 : 0), 1.0);

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DPPCycles = %u\n", __func__, s->DPPCycles);
	dml_print("DML::%s: PixelClock = %f\n", __func__, p->myPipe->PixelClock);
	dml_print("DML::%s: Dppclk = %f\n", __func__, p->myPipe->Dppclk);
	dml_print("DML::%s: DISPCLKCycles = %u\n", __func__, s->DISPCLKCycles);
	dml_print("DML::%s: DISPCLK = %f\n", __func__, p->myPipe->Dispclk);
	dml_print("DML::%s: DSCDelay = %u\n", __func__, p->DSCDelay);
	dml_print("DML::%s: ODMMode = %u\n", __func__, p->myPipe->ODMMode);
	dml_print("DML::%s: DPP_RECOUT_WIDTH = %u\n", __func__, p->DPP_RECOUT_WIDTH);
	dml_print("DML::%s: DSTXAfterScaler = %u\n", __func__, *p->DSTXAfterScaler);
#endif

	if (p->OutputFormat == dml_420 || (p->myPipe->InterlaceEnable && p->myPipe->ProgressiveToInterlaceUnitInOPP))
		*p->DSTYAfterScaler = 1;
	else
		*p->DSTYAfterScaler = 0;

	s->DSTTotalPixelsAfterScaler = *p->DSTYAfterScaler * p->myPipe->HTotal + *p->DSTXAfterScaler;
	*p->DSTYAfterScaler = (dml_uint_t)(dml_floor(s->DSTTotalPixelsAfterScaler / p->myPipe->HTotal, 1));
	*p->DSTXAfterScaler = (dml_uint_t)(s->DSTTotalPixelsAfterScaler - ((dml_float_t) (*p->DSTYAfterScaler * p->myPipe->HTotal)));
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DSTXAfterScaler = %u (final)\n", __func__,  *p->DSTXAfterScaler);
	dml_print("DML::%s: DSTYAfterScaler = %u (final)\n", __func__, *p->DSTYAfterScaler);
#endif

	s->MyError = false;

	s->Tr0_trips = s->trip_to_mem * (s->HostVMDynamicLevelsTrips + 1);

	if (p->GPUVMEnable == true) {
		s->Tvm_trips_rounded = dml_ceil(4.0 * s->Tvm_trips / s->LineTime, 1.0) / 4.0 * s->LineTime;
		s->Tr0_trips_rounded = dml_ceil(4.0 * s->Tr0_trips / s->LineTime, 1.0) / 4.0 * s->LineTime;
		if (p->GPUVMPageTableLevels >= 3) {
			*p->Tno_bw = p->UrgentExtraLatency + s->trip_to_mem * (dml_float_t) ((p->GPUVMPageTableLevels - 2) * (s->HostVMDynamicLevelsTrips + 1) - 1);
	} else if (p->GPUVMPageTableLevels == 1 && p->myPipe->DCCEnable != true) {
			s->Tr0_trips_rounded = dml_ceil(4.0 * p->UrgentExtraLatency / s->LineTime, 1.0) / 4.0 * s->LineTime;
			*p->Tno_bw = p->UrgentExtraLatency;
		} else {
			*p->Tno_bw = 0;
		}
	} else if (p->myPipe->DCCEnable == true) {
		s->Tvm_trips_rounded = s->LineTime / 4.0;
		s->Tr0_trips_rounded = dml_ceil(4.0 * s->Tr0_trips / s->LineTime, 1.0) / 4.0 * s->LineTime;
		*p->Tno_bw = 0;
	} else {
		s->Tvm_trips_rounded = s->LineTime / 4.0;
		s->Tr0_trips_rounded = s->LineTime / 2.0;
		*p->Tno_bw = 0;
	}
	s->Tvm_trips_rounded = dml_max(s->Tvm_trips_rounded, s->LineTime / 4.0);
	s->Tr0_trips_rounded = dml_max(s->Tr0_trips_rounded, s->LineTime / 4.0);

	if (p->myPipe->SourcePixelFormat == dml_420_8 || p->myPipe->SourcePixelFormat == dml_420_10 || p->myPipe->SourcePixelFormat == dml_420_12) {
		s->bytes_pp = p->myPipe->BytePerPixelY + p->myPipe->BytePerPixelC / 4;
	} else {
		s->bytes_pp = p->myPipe->BytePerPixelY + p->myPipe->BytePerPixelC;
	}

	s->prefetch_bw_pr = s->bytes_pp * p->myPipe->PixelClock / (dml_float_t)p->myPipe->DPPPerSurface;
	if (p->myPipe->VRatio < 1.0)
		s->prefetch_bw_pr = p->myPipe->VRatio * s->prefetch_bw_pr;

	s->max_Tsw = (dml_max(p->PrefetchSourceLinesY, p->PrefetchSourceLinesC) * s->LineTime);

	s->prefetch_sw_bytes = p->PrefetchSourceLinesY * p->swath_width_luma_ub * p->myPipe->BytePerPixelY + p->PrefetchSourceLinesC * p->swath_width_chroma_ub * p->myPipe->BytePerPixelC;
	s->prefetch_bw_oto = dml_max(s->prefetch_bw_pr, s->prefetch_sw_bytes / s->max_Tsw);

	s->min_Lsw_oto = dml_max(p->PrefetchSourceLinesY, p->PrefetchSourceLinesC) / __DML_MAX_VRATIO_PRE_OTO__;
	s->min_Lsw_oto = dml_max(s->min_Lsw_oto, 1.0);
	s->Lsw_oto = dml_ceil(4.0 * dml_max(s->prefetch_sw_bytes / s->prefetch_bw_oto / s->LineTime, s->min_Lsw_oto), 1.0) / 4.0;

	if (p->GPUVMEnable == true) {
		s->Tvm_oto = dml_max3(
			s->Tvm_trips,
			*p->Tno_bw + p->PDEAndMetaPTEBytesFrame * p->HostVMInefficiencyFactor / s->prefetch_bw_oto,
			s->LineTime / 4.0);
	} else
		s->Tvm_oto = s->LineTime / 4.0;

	if ((p->GPUVMEnable == true || p->myPipe->DCCEnable == true)) {
		s->Tr0_oto = dml_max4(
			s->Tr0_trips,
			(p->MetaRowByte + p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor) / s->prefetch_bw_oto,
			(s->LineTime - s->Tvm_oto)/2.0,
			s->LineTime / 4.0);
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: Tr0_oto max0 = %f\n", __func__, (p->MetaRowByte + p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor) / s->prefetch_bw_oto);
	dml_print("DML::%s: Tr0_oto max1 = %f\n", __func__, s->Tr0_trips);
	dml_print("DML::%s: Tr0_oto max2 = %f\n", __func__, s->LineTime - s->Tvm_oto);
	dml_print("DML::%s: Tr0_oto max3 = %f\n", __func__, s->LineTime / 4);
#endif
	} else
		s->Tr0_oto = (s->LineTime - s->Tvm_oto) / 2.0;

	s->Tvm_oto_lines = dml_ceil(4.0 * s->Tvm_oto / s->LineTime, 1) / 4.0;
	s->Tr0_oto_lines = dml_ceil(4.0 * s->Tr0_oto / s->LineTime, 1) / 4.0;
	s->dst_y_prefetch_oto = s->Tvm_oto_lines + 2 * s->Tr0_oto_lines + s->Lsw_oto;

	s->dst_y_prefetch_equ = p->VStartup - (*p->TSetup + dml_max(p->TWait + p->TCalc, *p->Tdmdl)) / s->LineTime - (*p->DSTYAfterScaler + (dml_float_t) *p->DSTXAfterScaler / (dml_float_t)p->myPipe->HTotal);

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: HTotal = %u\n", __func__, p->myPipe->HTotal);
	dml_print("DML::%s: min_Lsw_oto = %f\n", __func__, s->min_Lsw_oto);
	dml_print("DML::%s: *Tno_bw = %f\n", __func__, *p->Tno_bw);
	dml_print("DML::%s: UrgentExtraLatency = %f\n", __func__, p->UrgentExtraLatency);
	dml_print("DML::%s: trip_to_mem = %f\n", __func__, s->trip_to_mem);
	dml_print("DML::%s: BytePerPixelY = %u\n", __func__, p->myPipe->BytePerPixelY);
	dml_print("DML::%s: PrefetchSourceLinesY = %f\n", __func__, p->PrefetchSourceLinesY);
	dml_print("DML::%s: swath_width_luma_ub = %u\n", __func__, p->swath_width_luma_ub);
	dml_print("DML::%s: BytePerPixelC = %u\n", __func__, p->myPipe->BytePerPixelC);
	dml_print("DML::%s: PrefetchSourceLinesC = %f\n", __func__, p->PrefetchSourceLinesC);
	dml_print("DML::%s: swath_width_chroma_ub = %u\n", __func__, p->swath_width_chroma_ub);
	dml_print("DML::%s: prefetch_sw_bytes = %f\n", __func__, s->prefetch_sw_bytes);
	dml_print("DML::%s: bytes_pp = %f\n", __func__, s->bytes_pp);
	dml_print("DML::%s: PDEAndMetaPTEBytesFrame = %u\n", __func__, p->PDEAndMetaPTEBytesFrame);
	dml_print("DML::%s: MetaRowByte = %u\n", __func__, p->MetaRowByte);
	dml_print("DML::%s: PixelPTEBytesPerRow = %u\n", __func__, p->PixelPTEBytesPerRow);
	dml_print("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, p->HostVMInefficiencyFactor);
	dml_print("DML::%s: Tvm_trips = %f\n", __func__, s->Tvm_trips);
	dml_print("DML::%s: Tr0_trips = %f\n", __func__, s->Tr0_trips);
	dml_print("DML::%s: prefetch_bw_oto = %f\n", __func__, s->prefetch_bw_oto);
	dml_print("DML::%s: Tr0_oto = %f\n", __func__, s->Tr0_oto);
	dml_print("DML::%s: Tvm_oto = %f\n", __func__, s->Tvm_oto);
	dml_print("DML::%s: Tvm_oto_lines = %f\n", __func__, s->Tvm_oto_lines);
	dml_print("DML::%s: Tr0_oto_lines = %f\n", __func__, s->Tr0_oto_lines);
	dml_print("DML::%s: Lsw_oto = %f\n", __func__, s->Lsw_oto);
	dml_print("DML::%s: dst_y_prefetch_oto = %f\n", __func__, s->dst_y_prefetch_oto);
	dml_print("DML::%s: dst_y_prefetch_equ = %f\n", __func__, s->dst_y_prefetch_equ);
#endif

	s->dst_y_prefetch_equ = dml_floor(4.0 * (s->dst_y_prefetch_equ + 0.125), 1) / 4.0;
	s->Tpre_rounded = s->dst_y_prefetch_equ * s->LineTime;

	dml_print("DML::%s: dst_y_prefetch_equ: %f (after round)\n", __func__, s->dst_y_prefetch_equ);

	dml_print("DML::%s: LineTime: %f\n", __func__, s->LineTime);
	dml_print("DML::%s: VStartup: %u\n", __func__, p->VStartup);
	dml_print("DML::%s: Tvstartup: %fus - time between vstartup and first pixel of active\n", __func__, p->VStartup * s->LineTime);
	dml_print("DML::%s: TSetup: %fus - time from vstartup to vready\n", __func__, *p->TSetup);
	dml_print("DML::%s: TCalc: %fus - time for calculations in dchub starting at vready\n", __func__, p->TCalc);
	dml_print("DML::%s: TWait: %fus - time for fabric to become ready max(pstate exit,cstate enter/exit, urgent latency) after TCalc\n", __func__, p->TWait);
	dml_print("DML::%s: Tdmbf: %fus - time for dmd transfer from dchub to dio output buffer\n", __func__, s->Tdmbf);
	dml_print("DML::%s: Tdmec: %fus - time dio takes to transfer dmd\n", __func__, s->Tdmec);
	dml_print("DML::%s: Tdmsks: %fus - time before active dmd must complete transmission at dio\n", __func__, s->Tdmsks);
	dml_print("DML::%s: Tdmdl_vm: %fus - time for vm stages of dmd \n", __func__, *p->Tdmdl_vm);
	dml_print("DML::%s: Tdmdl: %fus - time for fabric to become ready and fetch dmd \n", __func__, *p->Tdmdl);
	dml_print("DML::%s: DSTXAfterScaler: %u pixels - number of pixel clocks pipeline and buffer delay after scaler \n", __func__, *p->DSTXAfterScaler);
	dml_print("DML::%s: DSTYAfterScaler: %u lines - number of lines of pipeline and buffer delay after scaler \n", __func__, *p->DSTYAfterScaler);

	s->dep_bytes = dml_max(p->PDEAndMetaPTEBytesFrame * p->HostVMInefficiencyFactor, p->MetaRowByte + p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor);

	if (s->prefetch_sw_bytes < s->dep_bytes) {
		s->prefetch_sw_bytes = 2 * s->dep_bytes;
	}

	*p->DestinationLinesToRequestVMInVBlank = 0;
	*p->DestinationLinesToRequestRowInVBlank = 0;
	*p->VRatioPrefetchY = 0;
	*p->VRatioPrefetchC = 0;
	*p->RequiredPrefetchPixDataBWLuma = 0;
	if (s->dst_y_prefetch_equ > 1) {

		if (s->Tpre_rounded - *p->Tno_bw > 0) {
		s->PrefetchBandwidth1 = (p->PDEAndMetaPTEBytesFrame * p->HostVMInefficiencyFactor + 2 * p->MetaRowByte
					+ 2 * p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor
					+ s->prefetch_sw_bytes)
					/ (s->Tpre_rounded - *p->Tno_bw);
			s->Tsw_est1 = s->prefetch_sw_bytes / s->PrefetchBandwidth1;
		} else
			s->PrefetchBandwidth1 = 0;

		if (p->VStartup == p->MaxVStartup && (s->Tsw_est1 / s->LineTime < s->min_Lsw_oto) && s->Tpre_rounded - s->min_Lsw_oto * s->LineTime - 0.75 * s->LineTime - *p->Tno_bw > 0) {
			s->PrefetchBandwidth1 = (p->PDEAndMetaPTEBytesFrame * p->HostVMInefficiencyFactor + 2 * p->MetaRowByte + 2 * p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor) /
								(s->Tpre_rounded - s->min_Lsw_oto * s->LineTime - 0.75 * s->LineTime - *p->Tno_bw);
		}

		if (s->Tpre_rounded - *p->Tno_bw - 2 * s->Tr0_trips_rounded > 0)
			s->PrefetchBandwidth2 = (p->PDEAndMetaPTEBytesFrame * p->HostVMInefficiencyFactor + s->prefetch_sw_bytes) /
									(s->Tpre_rounded - *p->Tno_bw - 2 * s->Tr0_trips_rounded);
		else
			s->PrefetchBandwidth2 = 0;

		if (s->Tpre_rounded - s->Tvm_trips_rounded > 0) {
			s->PrefetchBandwidth3 = (2 * p->MetaRowByte + 2 * p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor + s->prefetch_sw_bytes) /
									(s->Tpre_rounded - s->Tvm_trips_rounded);
			s->Tsw_est3 = s->prefetch_sw_bytes / s->PrefetchBandwidth3;
	}
		else
			s->PrefetchBandwidth3 = 0;


		if (p->VStartup == p->MaxVStartup && (s->Tsw_est3 / s->LineTime < s->min_Lsw_oto) && s->Tpre_rounded - s->min_Lsw_oto * s->LineTime - 0.5 * s->LineTime - s->Tvm_trips_rounded > 0) {
			s->PrefetchBandwidth3 = (2 * p->MetaRowByte + 2 * p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor) / (s->Tpre_rounded - s->min_Lsw_oto * s->LineTime - 0.5 * s->LineTime - s->Tvm_trips_rounded);
		}

		if (s->Tpre_rounded - s->Tvm_trips_rounded - 2 * s->Tr0_trips_rounded > 0)
			s->PrefetchBandwidth4 = s->prefetch_sw_bytes / (s->Tpre_rounded - s->Tvm_trips_rounded - 2 * s->Tr0_trips_rounded);
		else
			s->PrefetchBandwidth4 = 0;

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: Tpre_rounded: %f\n", __func__, s->Tpre_rounded);
		dml_print("DML::%s: Tno_bw: %f\n", __func__, *p->Tno_bw);
		dml_print("DML::%s: Tvm_trips_rounded: %f\n", __func__, s->Tvm_trips_rounded);
		dml_print("DML::%s: Tsw_est1: %f\n", __func__, s->Tsw_est1);
		dml_print("DML::%s: Tsw_est3: %f\n", __func__, s->Tsw_est3);
		dml_print("DML::%s: PrefetchBandwidth1: %f\n", __func__, s->PrefetchBandwidth1);
		dml_print("DML::%s: PrefetchBandwidth2: %f\n", __func__, s->PrefetchBandwidth2);
		dml_print("DML::%s: PrefetchBandwidth3: %f\n", __func__, s->PrefetchBandwidth3);
		dml_print("DML::%s: PrefetchBandwidth4: %f\n", __func__, s->PrefetchBandwidth4);
#endif
		{
			dml_bool_t Case1OK;
			dml_bool_t Case2OK;
			dml_bool_t Case3OK;

			if (s->PrefetchBandwidth1 > 0) {
				if (*p->Tno_bw + p->PDEAndMetaPTEBytesFrame * p->HostVMInefficiencyFactor / s->PrefetchBandwidth1 >= s->Tvm_trips_rounded && (p->MetaRowByte + p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor) / s->PrefetchBandwidth1 >= s->Tr0_trips_rounded) {
					Case1OK = true;
				} else {
					Case1OK = false;
				}
			} else {
				Case1OK = false;
			}

			if (s->PrefetchBandwidth2 > 0) {
				if (*p->Tno_bw + p->PDEAndMetaPTEBytesFrame * p->HostVMInefficiencyFactor / s->PrefetchBandwidth2 >= s->Tvm_trips_rounded && (p->MetaRowByte + p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor) / s->PrefetchBandwidth2 < s->Tr0_trips_rounded) {
					Case2OK = true;
				} else {
					Case2OK = false;
				}
			} else {
				Case2OK = false;
			}

			if (s->PrefetchBandwidth3 > 0) {
				if (*p->Tno_bw + p->PDEAndMetaPTEBytesFrame * p->HostVMInefficiencyFactor / s->PrefetchBandwidth3 < s->Tvm_trips_rounded && (p->MetaRowByte + p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor) / s->PrefetchBandwidth3 >= s->Tr0_trips_rounded) {
					Case3OK = true;
				} else {
					Case3OK = false;
				}
			} else {
				Case3OK = false;
			}

			if (Case1OK) {
				s->prefetch_bw_equ = s->PrefetchBandwidth1;
			} else if (Case2OK) {
				s->prefetch_bw_equ = s->PrefetchBandwidth2;
			} else if (Case3OK) {
				s->prefetch_bw_equ = s->PrefetchBandwidth3;
			} else {
				s->prefetch_bw_equ = s->PrefetchBandwidth4;
			}

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: Case1OK: %u\n", __func__, Case1OK);
			dml_print("DML::%s: Case2OK: %u\n", __func__, Case2OK);
			dml_print("DML::%s: Case3OK: %u\n", __func__, Case3OK);
			dml_print("DML::%s: prefetch_bw_equ: %f\n", __func__, s->prefetch_bw_equ);
#endif

			if (s->prefetch_bw_equ > 0) {
				if (p->GPUVMEnable == true) {
					s->Tvm_equ = dml_max3(*p->Tno_bw + p->PDEAndMetaPTEBytesFrame * p->HostVMInefficiencyFactor / s->prefetch_bw_equ, s->Tvm_trips, s->LineTime / 4);
				} else {
					s->Tvm_equ = s->LineTime / 4;
				}

				if ((p->GPUVMEnable == true || p->myPipe->DCCEnable == true)) {
					s->Tr0_equ = dml_max4((p->MetaRowByte + p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor) / s->prefetch_bw_equ, s->Tr0_trips, (s->LineTime - s->Tvm_equ) / 2, s->LineTime / 4);
				} else {
					s->Tr0_equ = (s->LineTime - s->Tvm_equ) / 2;
				}
			} else {
				s->Tvm_equ = 0;
				s->Tr0_equ = 0;
				dml_print("DML::%s: prefetch_bw_equ equals 0!\n", __func__);
			}
		}


		if (s->dst_y_prefetch_oto < s->dst_y_prefetch_equ) {
			*p->DestinationLinesForPrefetch = s->dst_y_prefetch_oto;
			s->TimeForFetchingMetaPTE = s->Tvm_oto;
			s->TimeForFetchingRowInVBlank = s->Tr0_oto;

			*p->DestinationLinesToRequestVMInVBlank = dml_ceil(4.0 * s->TimeForFetchingMetaPTE / s->LineTime, 1.0) / 4.0;
			*p->DestinationLinesToRequestRowInVBlank = dml_ceil(4.0 * s->TimeForFetchingRowInVBlank / s->LineTime, 1.0) / 4.0;
		} else {
			*p->DestinationLinesForPrefetch = s->dst_y_prefetch_equ;
			s->TimeForFetchingMetaPTE = s->Tvm_equ;
			s->TimeForFetchingRowInVBlank = s->Tr0_equ;

			if (p->VStartup == p->MaxVStartup && p->EnhancedPrefetchScheduleAccelerationFinal != 0) {
				*p->DestinationLinesToRequestVMInVBlank = dml_floor(4.0 * s->TimeForFetchingMetaPTE / s->LineTime, 1.0) / 4.0;
				*p->DestinationLinesToRequestRowInVBlank = dml_floor(4.0 * s->TimeForFetchingRowInVBlank / s->LineTime, 1.0) / 4.0;
			} else {
				*p->DestinationLinesToRequestVMInVBlank = dml_ceil(4.0 * s->TimeForFetchingMetaPTE / s->LineTime, 1.0) / 4.0;
				*p->DestinationLinesToRequestRowInVBlank = dml_ceil(4.0 * s->TimeForFetchingRowInVBlank / s->LineTime, 1.0) / 4.0;
			}
		}

		s->LinesToRequestPrefetchPixelData = *p->DestinationLinesForPrefetch - *p->DestinationLinesToRequestVMInVBlank - 2 * *p->DestinationLinesToRequestRowInVBlank;

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: DestinationLinesForPrefetch = %f\n", __func__, *p->DestinationLinesForPrefetch);
		dml_print("DML::%s: DestinationLinesToRequestVMInVBlank = %f\n", __func__, *p->DestinationLinesToRequestVMInVBlank);
		dml_print("DML::%s: TimeForFetchingRowInVBlank = %f\n", __func__, s->TimeForFetchingRowInVBlank);
		dml_print("DML::%s: LineTime = %f\n", __func__, s->LineTime);
		dml_print("DML::%s: DestinationLinesToRequestRowInVBlank = %f\n", __func__, *p->DestinationLinesToRequestRowInVBlank);
		dml_print("DML::%s: PrefetchSourceLinesY = %f\n", __func__, p->PrefetchSourceLinesY);
		dml_print("DML::%s: LinesToRequestPrefetchPixelData = %f\n", __func__, s->LinesToRequestPrefetchPixelData);
#endif

		if (s->LinesToRequestPrefetchPixelData >= 1 && s->prefetch_bw_equ > 0) {
			*p->VRatioPrefetchY = (dml_float_t)p->PrefetchSourceLinesY / s->LinesToRequestPrefetchPixelData;
			*p->VRatioPrefetchY = dml_max(*p->VRatioPrefetchY, 1.0);
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: VRatioPrefetchY = %f\n", __func__, *p->VRatioPrefetchY);
			dml_print("DML::%s: SwathHeightY = %u\n", __func__, p->SwathHeightY);
			dml_print("DML::%s: VInitPreFillY = %u\n", __func__, p->VInitPreFillY);
#endif
			if ((p->SwathHeightY > 4) && (p->VInitPreFillY > 3)) {
				if (s->LinesToRequestPrefetchPixelData > (p->VInitPreFillY - 3.0) / 2.0) {
					*p->VRatioPrefetchY = dml_max(*p->VRatioPrefetchY,
										(dml_float_t)p->MaxNumSwathY * p->SwathHeightY / (s->LinesToRequestPrefetchPixelData - (p->VInitPreFillY - 3.0) / 2.0));
			} else {
				s->MyError = true;
				dml_print("DML::%s: MyErr set. LinesToRequestPrefetchPixelData=%f VinitPreFillY=%u\n", __func__, s->LinesToRequestPrefetchPixelData, p->VInitPreFillY);
				*p->VRatioPrefetchY = 0;
			}
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: VRatioPrefetchY = %f\n", __func__, *p->VRatioPrefetchY);
			dml_print("DML::%s: PrefetchSourceLinesY = %f\n", __func__, p->PrefetchSourceLinesY);
			dml_print("DML::%s: MaxNumSwathY = %u\n", __func__, p->MaxNumSwathY);
#endif
			}

			*p->VRatioPrefetchC = (dml_float_t)p->PrefetchSourceLinesC / s->LinesToRequestPrefetchPixelData;
			*p->VRatioPrefetchC = dml_max(*p->VRatioPrefetchC, 1.0);

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: VRatioPrefetchC = %f\n", __func__, *p->VRatioPrefetchC);
			dml_print("DML::%s: SwathHeightC = %u\n", __func__, p->SwathHeightC);
			dml_print("DML::%s: VInitPreFillC = %u\n", __func__, p->VInitPreFillC);
#endif
			if ((p->SwathHeightC > 4) && (p->VInitPreFillC > 3)) {
				if (s->LinesToRequestPrefetchPixelData > (p->VInitPreFillC - 3.0) / 2.0) {
					*p->VRatioPrefetchC = dml_max(*p->VRatioPrefetchC, (dml_float_t)p->MaxNumSwathC * p->SwathHeightC / (s->LinesToRequestPrefetchPixelData - (p->VInitPreFillC - 3.0) / 2.0));
				} else {
					s->MyError = true;
					dml_print("DML::%s: MyErr set. LinesToRequestPrefetchPixelData=%f VInitPreFillC=%u\n", __func__, s->LinesToRequestPrefetchPixelData, p->VInitPreFillC);
					*p->VRatioPrefetchC = 0;
				}
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: VRatioPrefetchC = %f\n", __func__, *p->VRatioPrefetchC);
				dml_print("DML::%s: PrefetchSourceLinesC = %f\n", __func__, p->PrefetchSourceLinesC);
				dml_print("DML::%s: MaxNumSwathC = %u\n", __func__, p->MaxNumSwathC);
#endif
			}

			*p->RequiredPrefetchPixDataBWLuma = (dml_float_t)p->PrefetchSourceLinesY / s->LinesToRequestPrefetchPixelData
				* p->myPipe->BytePerPixelY
				* p->swath_width_luma_ub / s->LineTime;

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: BytePerPixelY = %u\n", __func__, p->myPipe->BytePerPixelY);
			dml_print("DML::%s: swath_width_luma_ub = %u\n", __func__, p->swath_width_luma_ub);
			dml_print("DML::%s: LineTime = %f\n", __func__, s->LineTime);
			dml_print("DML::%s: RequiredPrefetchPixDataBWLuma = %f\n", __func__, *p->RequiredPrefetchPixDataBWLuma);
#endif
			*p->RequiredPrefetchPixDataBWChroma = (dml_float_t)p->PrefetchSourceLinesC / s->LinesToRequestPrefetchPixelData
				*p->myPipe->BytePerPixelC
				*p->swath_width_chroma_ub / s->LineTime;
		} else {
			s->MyError = true;
			dml_print("DML:%s: MyErr set. LinesToRequestPrefetchPixelData: %f, should be > 0\n", __func__, s->LinesToRequestPrefetchPixelData);
			*p->VRatioPrefetchY = 0;
			*p->VRatioPrefetchC = 0;
			*p->RequiredPrefetchPixDataBWLuma = 0;
			*p->RequiredPrefetchPixDataBWChroma = 0;
		}

		dml_print("DML: Tpre: %fus - sum of time to request meta pte, 2 x data pte + meta data, swaths\n", (dml_float_t)s->LinesToRequestPrefetchPixelData * s->LineTime + 2.0 * s->TimeForFetchingRowInVBlank + s->TimeForFetchingMetaPTE);
		dml_print("DML: Tvm: %fus - time to fetch page tables for meta surface\n", s->TimeForFetchingMetaPTE);
		dml_print("DML: Tr0: %fus - time to fetch first row of data pagetables and first row of meta data (done in parallel)\n", s->TimeForFetchingRowInVBlank);
		dml_print("DML: Tsw: %fus = time to fetch enough pixel data and cursor data to feed the scalers init position and detile\n", (dml_float_t)s->LinesToRequestPrefetchPixelData * s->LineTime);
		dml_print("DML: To: %fus - time for propagation from scaler to optc\n", (*p->DSTYAfterScaler + ((dml_float_t) (*p->DSTXAfterScaler) / (dml_float_t)p->myPipe->HTotal)) * s->LineTime);
		dml_print("DML: Tvstartup - TSetup - Tcalc - Twait - Tpre - To > 0\n");
		dml_print("DML: Tslack(pre): %fus - time left over in schedule\n", p->VStartup * s->LineTime - s->TimeForFetchingMetaPTE - 2 * s->TimeForFetchingRowInVBlank - (*p->DSTYAfterScaler + ((dml_float_t) (*p->DSTXAfterScaler) / (dml_float_t)p->myPipe->HTotal)) * s->LineTime - p->TWait - p->TCalc - *p->TSetup);
		dml_print("DML: row_bytes = dpte_row_bytes (per_pipe) = PixelPTEBytesPerRow = : %u\n", p->PixelPTEBytesPerRow);

	} else {
		s->MyError = true;
		dml_print("DML::%s: MyErr set, dst_y_prefetch_equ = %f (should be > 1)\n", __func__, s->dst_y_prefetch_equ);
		s->TimeForFetchingMetaPTE = 0;
		s->TimeForFetchingRowInVBlank = 0;
		*p->DestinationLinesToRequestVMInVBlank = 0;
		*p->DestinationLinesToRequestRowInVBlank = 0;
		s->LinesToRequestPrefetchPixelData = 0;
		*p->VRatioPrefetchY = 0;
		*p->VRatioPrefetchC = 0;
		*p->RequiredPrefetchPixDataBWLuma = 0;
		*p->RequiredPrefetchPixDataBWChroma = 0;
	}

	{
		dml_float_t prefetch_vm_bw;
		dml_float_t prefetch_row_bw;

		if (p->PDEAndMetaPTEBytesFrame == 0) {
			prefetch_vm_bw = 0;
		} else if (*p->DestinationLinesToRequestVMInVBlank > 0) {
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: PDEAndMetaPTEBytesFrame = %u\n", __func__, p->PDEAndMetaPTEBytesFrame);
			dml_print("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, p->HostVMInefficiencyFactor);
			dml_print("DML::%s: DestinationLinesToRequestVMInVBlank = %f\n", __func__, *p->DestinationLinesToRequestVMInVBlank);
			dml_print("DML::%s: LineTime = %f\n", __func__, s->LineTime);
#endif
		prefetch_vm_bw = p->PDEAndMetaPTEBytesFrame * p->HostVMInefficiencyFactor / (*p->DestinationLinesToRequestVMInVBlank * s->LineTime);
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: prefetch_vm_bw = %f\n", __func__, prefetch_vm_bw);
#endif
		} else {
			prefetch_vm_bw = 0;
			s->MyError = true;
			dml_print("DML::%s: MyErr set. DestinationLinesToRequestVMInVBlank=%f (should be > 0)\n", __func__, *p->DestinationLinesToRequestVMInVBlank);
		}

		if (p->MetaRowByte + p->PixelPTEBytesPerRow == 0) {
			prefetch_row_bw = 0;
		} else if (*p->DestinationLinesToRequestRowInVBlank > 0) {
			prefetch_row_bw = (p->MetaRowByte + p->PixelPTEBytesPerRow * p->HostVMInefficiencyFactor) / (*p->DestinationLinesToRequestRowInVBlank * s->LineTime);

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: MetaRowByte = %u\n", __func__, p->MetaRowByte);
		dml_print("DML::%s: PixelPTEBytesPerRow = %u\n", __func__, p->PixelPTEBytesPerRow);
		dml_print("DML::%s: DestinationLinesToRequestRowInVBlank = %f\n", __func__, *p->DestinationLinesToRequestRowInVBlank);
		dml_print("DML::%s: prefetch_row_bw = %f\n", __func__, prefetch_row_bw);
#endif
		} else {
			prefetch_row_bw = 0;
			s->MyError = true;
			dml_print("DML::%s: MyErr set. DestinationLinesToRequestRowInVBlank=%f (should be > 0)\n", __func__, *p->DestinationLinesToRequestRowInVBlank);
		}

		*p->prefetch_vmrow_bw = dml_max(prefetch_vm_bw, prefetch_row_bw);
	}

	if (s->MyError) {
		s->TimeForFetchingMetaPTE = 0;
		s->TimeForFetchingRowInVBlank = 0;
		*p->DestinationLinesToRequestVMInVBlank = 0;
		*p->DestinationLinesToRequestRowInVBlank = 0;
		*p->DestinationLinesForPrefetch = 0;
		s->LinesToRequestPrefetchPixelData = 0;
		*p->VRatioPrefetchY = 0;
		*p->VRatioPrefetchC = 0;
		*p->RequiredPrefetchPixDataBWLuma = 0;
		*p->RequiredPrefetchPixDataBWChroma = 0;
	}

	return s->MyError;
} // CalculatePrefetchSchedule

static void CalculateBytePerPixelAndBlockSizes(
	enum dml_source_format_class SourcePixelFormat,
	enum dml_swizzle_mode SurfaceTiling,

	// Output
	dml_uint_t *BytePerPixelY,
	dml_uint_t *BytePerPixelC,
	dml_float_t *BytePerPixelDETY,
	dml_float_t *BytePerPixelDETC,
	dml_uint_t *BlockHeight256BytesY,
	dml_uint_t *BlockHeight256BytesC,
	dml_uint_t *BlockWidth256BytesY,
	dml_uint_t *BlockWidth256BytesC,
	dml_uint_t *MacroTileHeightY,
	dml_uint_t *MacroTileHeightC,
	dml_uint_t *MacroTileWidthY,
	dml_uint_t *MacroTileWidthC)
{
	if (SourcePixelFormat == dml_444_64) {
		*BytePerPixelDETY = 8;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 8;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dml_444_32 || SourcePixelFormat == dml_rgbe) {
		*BytePerPixelDETY = 4;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 4;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dml_444_16 || SourcePixelFormat == dml_mono_16) {
		*BytePerPixelDETY = 2;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 2;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dml_444_8 || SourcePixelFormat == dml_mono_8) {
		*BytePerPixelDETY = 1;
		*BytePerPixelDETC = 0;
		*BytePerPixelY = 1;
		*BytePerPixelC = 0;
	} else if (SourcePixelFormat == dml_rgbe_alpha) {
		*BytePerPixelDETY = 4;
		*BytePerPixelDETC = 1;
		*BytePerPixelY = 4;
		*BytePerPixelC = 1;
	} else if (SourcePixelFormat == dml_420_8) {
		*BytePerPixelDETY = 1;
		*BytePerPixelDETC = 2;
		*BytePerPixelY = 1;
		*BytePerPixelC = 2;
	} else if (SourcePixelFormat == dml_420_12) {
		*BytePerPixelDETY = 2;
		*BytePerPixelDETC = 4;
		*BytePerPixelY = 2;
		*BytePerPixelC = 4;
	} else {
		*BytePerPixelDETY = (dml_float_t) (4.0 / 3);
		*BytePerPixelDETC = (dml_float_t) (8.0 / 3);
		*BytePerPixelY = 2;
		*BytePerPixelC = 4;
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: SourcePixelFormat = %u\n", __func__, SourcePixelFormat);
	dml_print("DML::%s: BytePerPixelDETY = %f\n", __func__, *BytePerPixelDETY);
	dml_print("DML::%s: BytePerPixelDETC = %f\n", __func__, *BytePerPixelDETC);
	dml_print("DML::%s: BytePerPixelY = %u\n", __func__, *BytePerPixelY);
	dml_print("DML::%s: BytePerPixelC = %u\n", __func__, *BytePerPixelC);
#endif
	if ((SourcePixelFormat == dml_444_64 || SourcePixelFormat == dml_444_32
		|| SourcePixelFormat == dml_444_16
		|| SourcePixelFormat == dml_444_8
		|| SourcePixelFormat == dml_mono_16
		|| SourcePixelFormat == dml_mono_8
		|| SourcePixelFormat == dml_rgbe)) {
		if (SurfaceTiling == dml_sw_linear) {
			*BlockHeight256BytesY = 1;
		} else if (SourcePixelFormat == dml_444_64) {
			*BlockHeight256BytesY = 4;
		} else if (SourcePixelFormat == dml_444_8) {
			*BlockHeight256BytesY = 16;
		} else {
			*BlockHeight256BytesY = 8;
		}
		*BlockWidth256BytesY = 256U / *BytePerPixelY / *BlockHeight256BytesY;
		*BlockHeight256BytesC = 0;
		*BlockWidth256BytesC = 0;
	} else {
		if (SurfaceTiling == dml_sw_linear) {
			*BlockHeight256BytesY = 1;
			*BlockHeight256BytesC = 1;
		} else if (SourcePixelFormat == dml_rgbe_alpha) {
			*BlockHeight256BytesY = 8;
			*BlockHeight256BytesC = 16;
		} else if (SourcePixelFormat == dml_420_8) {
			*BlockHeight256BytesY = 16;
			*BlockHeight256BytesC = 8;
		} else {
			*BlockHeight256BytesY = 8;
			*BlockHeight256BytesC = 8;
		}
		*BlockWidth256BytesY = 256U / *BytePerPixelY / *BlockHeight256BytesY;
		*BlockWidth256BytesC = 256U / *BytePerPixelC / *BlockHeight256BytesC;
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: BlockWidth256BytesY = %u\n", __func__, *BlockWidth256BytesY);
	dml_print("DML::%s: BlockHeight256BytesY = %u\n", __func__, *BlockHeight256BytesY);
	dml_print("DML::%s: BlockWidth256BytesC = %u\n", __func__, *BlockWidth256BytesC);
	dml_print("DML::%s: BlockHeight256BytesC = %u\n", __func__, *BlockHeight256BytesC);
#endif

	if (SurfaceTiling == dml_sw_linear) {
		*MacroTileHeightY = *BlockHeight256BytesY;
		*MacroTileWidthY = 256 / *BytePerPixelY / *MacroTileHeightY;
		*MacroTileHeightC = *BlockHeight256BytesC;
		if (*MacroTileHeightC == 0) {
			*MacroTileWidthC = 0;
		} else {
			*MacroTileWidthC = 256 / *BytePerPixelC / *MacroTileHeightC;
	}
	} else if (SurfaceTiling == dml_sw_64kb_d || SurfaceTiling == dml_sw_64kb_d_t || SurfaceTiling == dml_sw_64kb_d_x || SurfaceTiling == dml_sw_64kb_r_x) {
		*MacroTileHeightY = 16 * *BlockHeight256BytesY;
		*MacroTileWidthY = 65536 / *BytePerPixelY / *MacroTileHeightY;
		*MacroTileHeightC = 16 * *BlockHeight256BytesC;
		if (*MacroTileHeightC == 0) {
			*MacroTileWidthC = 0;
		} else {
			*MacroTileWidthC = 65536 / *BytePerPixelC / *MacroTileHeightC;
		}
	} else {
		*MacroTileHeightY = 32 * *BlockHeight256BytesY;
		*MacroTileWidthY = 65536 * 4 / *BytePerPixelY / *MacroTileHeightY;
		*MacroTileHeightC = 32 * *BlockHeight256BytesC;
		if (*MacroTileHeightC == 0) {
			*MacroTileWidthC = 0;
		} else {
			*MacroTileWidthC = 65536 * 4 / *BytePerPixelC / *MacroTileHeightC;
		}
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: MacroTileWidthY = %u\n", __func__, *MacroTileWidthY);
	dml_print("DML::%s: MacroTileHeightY = %u\n", __func__, *MacroTileHeightY);
	dml_print("DML::%s: MacroTileWidthC = %u\n", __func__, *MacroTileWidthC);
	dml_print("DML::%s: MacroTileHeightC = %u\n", __func__, *MacroTileHeightC);
#endif
} // CalculateBytePerPixelAndBlockSizes

static dml_float_t CalculateTWait(
		dml_uint_t PrefetchMode,
		enum dml_use_mall_for_pstate_change_mode UseMALLForPStateChange,
		dml_bool_t SynchronizeDRRDisplaysForUCLKPStateChangeFinal,
		dml_bool_t DRRDisplay,
		dml_float_t DRAMClockChangeLatency,
		dml_float_t FCLKChangeLatency,
		dml_float_t UrgentLatency,
		dml_float_t SREnterPlusExitTime)
{
	dml_float_t TWait = 0.0;

	if (PrefetchMode == 0 &&
			!(UseMALLForPStateChange == dml_use_mall_pstate_change_full_frame) && !(UseMALLForPStateChange == dml_use_mall_pstate_change_sub_viewport) &&
			!(UseMALLForPStateChange == dml_use_mall_pstate_change_phantom_pipe) && !(SynchronizeDRRDisplaysForUCLKPStateChangeFinal && DRRDisplay)) {
		TWait = dml_max3(DRAMClockChangeLatency + UrgentLatency, SREnterPlusExitTime, UrgentLatency);
	} else if (PrefetchMode <= 1 && !(UseMALLForPStateChange == dml_use_mall_pstate_change_phantom_pipe)) {
		TWait = dml_max3(FCLKChangeLatency + UrgentLatency, SREnterPlusExitTime, UrgentLatency);
	} else if (PrefetchMode <= 2 && !(UseMALLForPStateChange == dml_use_mall_pstate_change_phantom_pipe)) {
		TWait = dml_max(SREnterPlusExitTime, UrgentLatency);
	} else {
		TWait = UrgentLatency;
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: PrefetchMode = %u\n", __func__, PrefetchMode);
	dml_print("DML::%s: TWait = %f\n", __func__, TWait);
#endif
	return TWait;
} // CalculateTWait


/// @brief Calculate the "starting point" for prefetch calculation
///  if AllowForPStateChangeOrStutterInVBlank is set as a particular requirement, then the mode evalulation
///  will only be done at the given mode. If no specific requirement (i.e. *_if_possible), then will just go from
///  try all the prefetch mode in decreasing order of "difficulty" (start from 0 which means all power saving
///  features).
static void CalculatePrefetchMode(
		enum dml_prefetch_modes AllowForPStateChangeOrStutterInVBlank,
		dml_uint_t *MinPrefetchMode,
		dml_uint_t *MaxPrefetchMode)
{
	if (AllowForPStateChangeOrStutterInVBlank == dml_prefetch_support_uclk_fclk_and_stutter_if_possible) {
		*MinPrefetchMode = 0;   // consider all pwr saving features
		*MaxPrefetchMode = 3;   // consider just urgent latency
	} else {
		if (AllowForPStateChangeOrStutterInVBlank == dml_prefetch_support_none) {
			*MinPrefetchMode = 3;
		} else if (AllowForPStateChangeOrStutterInVBlank == dml_prefetch_support_stutter) {
			*MinPrefetchMode = 2;
		} else if (AllowForPStateChangeOrStutterInVBlank == dml_prefetch_support_fclk_and_stutter) {
			*MinPrefetchMode = 1;
		} else if (AllowForPStateChangeOrStutterInVBlank == dml_prefetch_support_uclk_fclk_and_stutter) {
			*MinPrefetchMode = 0;
		} else {
			dml_print("ERROR: Invalid AllowForPStateChangeOrStutterInVBlank setting! val=%u\n", AllowForPStateChangeOrStutterInVBlank);
			ASSERT(0);
		}
		*MaxPrefetchMode = *MinPrefetchMode;
	}
} // CalculatePrefetchMode

static dml_float_t CalculateWriteBackDISPCLK(
		enum dml_source_format_class WritebackPixelFormat,
		dml_float_t PixelClock,
		dml_float_t WritebackHRatio,
		dml_float_t WritebackVRatio,
		dml_uint_t WritebackHTaps,
		dml_uint_t WritebackVTaps,
		dml_uint_t WritebackSourceWidth,
		dml_uint_t WritebackDestinationWidth,
		dml_uint_t HTotal,
		dml_uint_t WritebackLineBufferSize,
		dml_float_t DISPCLKDPPCLKVCOSpeed)
{
	dml_float_t DISPCLK_H, DISPCLK_V, DISPCLK_HB;

	DISPCLK_H = PixelClock * dml_ceil(WritebackHTaps / 8.0, 1) / WritebackHRatio;
	DISPCLK_V = PixelClock * (WritebackVTaps * dml_ceil(WritebackDestinationWidth / 6.0, 1) + 8.0) / (dml_float_t) HTotal;
	DISPCLK_HB = PixelClock * WritebackVTaps * (WritebackDestinationWidth * WritebackVTaps - WritebackLineBufferSize / 57.0) / 6.0 / (dml_float_t) WritebackSourceWidth;
	return RoundToDFSGranularity(dml_max3(DISPCLK_H, DISPCLK_V, DISPCLK_HB), 1, DISPCLKDPPCLKVCOSpeed);
}

static dml_float_t CalculateWriteBackDelay(
		enum dml_source_format_class WritebackPixelFormat,
		dml_float_t WritebackHRatio,
		dml_float_t WritebackVRatio,
		dml_uint_t WritebackVTaps,
		dml_uint_t WritebackDestinationWidth,
		dml_uint_t WritebackDestinationHeight,
		dml_uint_t WritebackSourceHeight,
		dml_uint_t HTotal)
{
	dml_float_t CalculateWriteBackDelay;
	dml_float_t Line_length;
	dml_float_t Output_lines_last_notclamped;
	dml_float_t WritebackVInit;

	WritebackVInit = (WritebackVRatio + WritebackVTaps + 1) / 2;
	Line_length = dml_max((dml_float_t) WritebackDestinationWidth, dml_ceil((dml_float_t)WritebackDestinationWidth / 6.0, 1.0) * WritebackVTaps);
	Output_lines_last_notclamped = WritebackDestinationHeight - 1 - dml_ceil(((dml_float_t)WritebackSourceHeight - (dml_float_t) WritebackVInit) / (dml_float_t)WritebackVRatio, 1.0);
	if (Output_lines_last_notclamped < 0) {
		CalculateWriteBackDelay = 0;
	} else {
		CalculateWriteBackDelay = Output_lines_last_notclamped * Line_length + (HTotal - WritebackDestinationWidth) + 80;
	}
	return CalculateWriteBackDelay;
}

static void CalculateVUpdateAndDynamicMetadataParameters(
		dml_uint_t MaxInterDCNTileRepeaters,
		dml_float_t Dppclk,
		dml_float_t Dispclk,
		dml_float_t DCFClkDeepSleep,
		dml_float_t PixelClock,
		dml_uint_t HTotal,
		dml_uint_t VBlank,
		dml_uint_t DynamicMetadataTransmittedBytes,
		dml_uint_t DynamicMetadataLinesBeforeActiveRequired,
		dml_uint_t InterlaceEnable,
		dml_bool_t ProgressiveToInterlaceUnitInOPP,

		// Output
		dml_float_t *TSetup,
		dml_float_t *Tdmbf,
		dml_float_t *Tdmec,
		dml_float_t *Tdmsks,
		dml_uint_t *VUpdateOffsetPix,
		dml_uint_t *VUpdateWidthPix,
		dml_uint_t *VReadyOffsetPix)
{
	dml_float_t TotalRepeaterDelayTime;
	TotalRepeaterDelayTime = MaxInterDCNTileRepeaters * (2 / Dppclk + 3 / Dispclk);
	*VUpdateWidthPix = (dml_uint_t)(dml_ceil((14.0 / DCFClkDeepSleep + 12.0 / Dppclk + TotalRepeaterDelayTime) * PixelClock, 1.0));
	*VReadyOffsetPix = (dml_uint_t)(dml_ceil(dml_max(150.0 / Dppclk, TotalRepeaterDelayTime + 20.0 / DCFClkDeepSleep + 10.0 / Dppclk) * PixelClock, 1.0));
	*VUpdateOffsetPix = (dml_uint_t)(dml_ceil(HTotal / 4.0, 1.0));
	*TSetup = (*VUpdateOffsetPix + *VUpdateWidthPix + *VReadyOffsetPix) / PixelClock;
	*Tdmbf = DynamicMetadataTransmittedBytes / 4.0 / Dispclk;
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
	dml_print("DML::%s: DynamicMetadataLinesBeforeActiveRequired = %u\n", __func__, DynamicMetadataLinesBeforeActiveRequired);
	dml_print("DML::%s: VBlank = %u\n", __func__, VBlank);
	dml_print("DML::%s: HTotal = %u\n", __func__, HTotal);
	dml_print("DML::%s: PixelClock = %f\n", __func__, PixelClock);
	dml_print("DML::%s: Dppclk = %f\n", __func__, Dppclk);
	dml_print("DML::%s: DCFClkDeepSleep = %f\n", __func__, DCFClkDeepSleep);
	dml_print("DML::%s: MaxInterDCNTileRepeaters = %u\n", __func__, MaxInterDCNTileRepeaters);
	dml_print("DML::%s: TotalRepeaterDelayTime = %f\n", __func__, TotalRepeaterDelayTime);

	dml_print("DML::%s: VUpdateWidthPix = %u\n", __func__, *VUpdateWidthPix);
	dml_print("DML::%s: VReadyOffsetPix = %u\n", __func__, *VReadyOffsetPix);
	dml_print("DML::%s: VUpdateOffsetPix = %u\n", __func__, *VUpdateOffsetPix);

	dml_print("DML::%s: Tdmsks = %f\n", __func__, *Tdmsks);
#endif
}

static void CalculateRowBandwidth(
		dml_bool_t GPUVMEnable,
		enum dml_source_format_class SourcePixelFormat,
		dml_float_t VRatio,
		dml_float_t VRatioChroma,
		dml_bool_t DCCEnable,
		dml_float_t LineTime,
		dml_uint_t MetaRowByteLuma,
		dml_uint_t MetaRowByteChroma,
		dml_uint_t meta_row_height_luma,
		dml_uint_t meta_row_height_chroma,
		dml_uint_t PixelPTEBytesPerRowLuma,
		dml_uint_t PixelPTEBytesPerRowChroma,
		dml_uint_t dpte_row_height_luma,
		dml_uint_t dpte_row_height_chroma,
		// Output
		dml_float_t *meta_row_bw,
		dml_float_t *dpte_row_bw)
{
	if (DCCEnable != true) {
		*meta_row_bw = 0;
	} else if (SourcePixelFormat == dml_420_8 || SourcePixelFormat == dml_420_10 || SourcePixelFormat == dml_420_12 || SourcePixelFormat == dml_rgbe_alpha) {
		*meta_row_bw = VRatio * MetaRowByteLuma / (meta_row_height_luma * LineTime)
				+ VRatioChroma * MetaRowByteChroma
					/ (meta_row_height_chroma * LineTime);
	} else {
		*meta_row_bw = VRatio * MetaRowByteLuma / (meta_row_height_luma * LineTime);
	}

	if (GPUVMEnable != true) {
		*dpte_row_bw = 0;
	} else if (SourcePixelFormat == dml_420_8 || SourcePixelFormat == dml_420_10 || SourcePixelFormat == dml_420_12 || SourcePixelFormat == dml_rgbe_alpha) {
		*dpte_row_bw = VRatio * PixelPTEBytesPerRowLuma / (dpte_row_height_luma * LineTime)
				+ VRatioChroma * PixelPTEBytesPerRowChroma
					/ (dpte_row_height_chroma * LineTime);
	} else {
		*dpte_row_bw = VRatio * PixelPTEBytesPerRowLuma / (dpte_row_height_luma * LineTime);
	}
}

/// @brief Determine immediate flip schedule given bw remaining after considering the prefetch schedule
/// @param BandwidthAvailableForImmediateFlip Bandwidth available for iflip for all planes
static void CalculateFlipSchedule(
		dml_float_t HostVMInefficiencyFactor,
		dml_float_t UrgentExtraLatency,
		dml_float_t UrgentLatency,
		dml_uint_t GPUVMMaxPageTableLevels,
		dml_bool_t HostVMEnable,
		dml_uint_t HostVMMaxNonCachedPageTableLevels,
		dml_bool_t GPUVMEnable,
		dml_uint_t HostVMMinPageSize,
		dml_float_t PDEAndMetaPTEBytesPerFrame,
		dml_float_t MetaRowBytes,
		dml_float_t DPTEBytesPerRow,
		dml_float_t BandwidthAvailableForImmediateFlip,
		dml_uint_t TotImmediateFlipBytes,
		enum dml_source_format_class SourcePixelFormat,
		dml_float_t LineTime,
		dml_float_t VRatio,
		dml_float_t VRatioChroma,
		dml_float_t Tno_bw,
		dml_bool_t DCCEnable,
		dml_uint_t dpte_row_height,
		dml_uint_t meta_row_height,
		dml_uint_t dpte_row_height_chroma,
		dml_uint_t meta_row_height_chroma,
		dml_bool_t use_one_row_for_frame_flip,

		// Output
		dml_float_t *DestinationLinesToRequestVMInImmediateFlip,
		dml_float_t *DestinationLinesToRequestRowInImmediateFlip,
		dml_float_t *final_flip_bw,
		dml_bool_t *ImmediateFlipSupportedForPipe)
{
	dml_float_t min_row_time = 0.0;
	dml_uint_t HostVMDynamicLevelsTrips = 0;
	dml_float_t TimeForFetchingMetaPTEImmediateFlip = 0;
	dml_float_t TimeForFetchingRowInVBlankImmediateFlip = 0;
	dml_float_t ImmediateFlipBW = 0; // @brief The immediate flip bandwidth for this pipe

	if (GPUVMEnable == true && HostVMEnable == true) {
		HostVMDynamicLevelsTrips = HostVMMaxNonCachedPageTableLevels;
	} else {
		HostVMDynamicLevelsTrips = 0;
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: TotImmediateFlipBytes = %u\n", __func__, TotImmediateFlipBytes);
	dml_print("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, HostVMInefficiencyFactor);
	dml_print("DML::%s: UrgentLatency = %f\n", __func__, UrgentLatency);
	dml_print("DML::%s: BandwidthAvailableForImmediateFlip = %f\n", __func__, BandwidthAvailableForImmediateFlip);
#endif

	if (TotImmediateFlipBytes > 0) {
		if (use_one_row_for_frame_flip) {
			ImmediateFlipBW = (PDEAndMetaPTEBytesPerFrame + MetaRowBytes + 2.0 * DPTEBytesPerRow) * BandwidthAvailableForImmediateFlip / (dml_float_t) TotImmediateFlipBytes;
		} else {
			ImmediateFlipBW = (PDEAndMetaPTEBytesPerFrame + MetaRowBytes + DPTEBytesPerRow) * BandwidthAvailableForImmediateFlip / (dml_float_t) TotImmediateFlipBytes;
		}
		if (GPUVMEnable == true) {
			TimeForFetchingMetaPTEImmediateFlip = dml_max3(Tno_bw + PDEAndMetaPTEBytesPerFrame * HostVMInefficiencyFactor / ImmediateFlipBW,
														UrgentExtraLatency + UrgentLatency * (GPUVMMaxPageTableLevels * (HostVMDynamicLevelsTrips + 1) - 1),
														LineTime / 4.0);
		} else {
			TimeForFetchingMetaPTEImmediateFlip = 0;
		}
		if ((GPUVMEnable == true || DCCEnable == true)) {
			TimeForFetchingRowInVBlankImmediateFlip = dml_max3((MetaRowBytes + DPTEBytesPerRow * HostVMInefficiencyFactor) / ImmediateFlipBW, UrgentLatency * (HostVMDynamicLevelsTrips + 1), LineTime / 4.0);
		} else {
			TimeForFetchingRowInVBlankImmediateFlip = 0;
		}

		*DestinationLinesToRequestVMInImmediateFlip = dml_ceil(4.0 * (TimeForFetchingMetaPTEImmediateFlip / LineTime), 1.0) / 4.0;
		*DestinationLinesToRequestRowInImmediateFlip = dml_ceil(4.0 * (TimeForFetchingRowInVBlankImmediateFlip / LineTime), 1.0) / 4.0;

		if (GPUVMEnable == true) {
			*final_flip_bw = dml_max(PDEAndMetaPTEBytesPerFrame * HostVMInefficiencyFactor / (*DestinationLinesToRequestVMInImmediateFlip * LineTime),
								(MetaRowBytes + DPTEBytesPerRow * HostVMInefficiencyFactor) / (*DestinationLinesToRequestRowInImmediateFlip * LineTime));
		} else if ((GPUVMEnable == true || DCCEnable == true)) {
			*final_flip_bw = (MetaRowBytes + DPTEBytesPerRow * HostVMInefficiencyFactor) / (*DestinationLinesToRequestRowInImmediateFlip * LineTime);
		} else {
			*final_flip_bw = 0;
		}
	} else {
		TimeForFetchingMetaPTEImmediateFlip = 0;
		TimeForFetchingRowInVBlankImmediateFlip = 0;
		*DestinationLinesToRequestVMInImmediateFlip = 0;
		*DestinationLinesToRequestRowInImmediateFlip = 0;
		*final_flip_bw = 0;
	}

	if (SourcePixelFormat == dml_420_8 || SourcePixelFormat == dml_420_10 || SourcePixelFormat == dml_rgbe_alpha) {
		if (GPUVMEnable == true && DCCEnable != true) {
			min_row_time = dml_min(dpte_row_height * LineTime / VRatio, dpte_row_height_chroma * LineTime / VRatioChroma);
		} else if (GPUVMEnable != true && DCCEnable == true) {
			min_row_time = dml_min(meta_row_height * LineTime / VRatio, meta_row_height_chroma * LineTime / VRatioChroma);
		} else {
			min_row_time = dml_min4(dpte_row_height * LineTime / VRatio, meta_row_height * LineTime / VRatio, dpte_row_height_chroma * LineTime / VRatioChroma, meta_row_height_chroma * LineTime / VRatioChroma);
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

	if (*DestinationLinesToRequestVMInImmediateFlip >= 32 || *DestinationLinesToRequestRowInImmediateFlip >= 16 || TimeForFetchingMetaPTEImmediateFlip + 2 * TimeForFetchingRowInVBlankImmediateFlip > min_row_time) {
		*ImmediateFlipSupportedForPipe = false;
	} else {
		*ImmediateFlipSupportedForPipe = true;
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: GPUVMEnable = %u\n", __func__, GPUVMEnable);
	dml_print("DML::%s: DCCEnable = %u\n", __func__, DCCEnable);

	dml_print("DML::%s: MetaRowBytes = %f\n", __func__, MetaRowBytes);
	dml_print("DML::%s: DPTEBytesPerRow = %f\n", __func__, DPTEBytesPerRow);
	dml_print("DML::%s: BandwidthAvailableForImmediateFlip = %f\n", __func__, BandwidthAvailableForImmediateFlip);
	dml_print("DML::%s: TotImmediateFlipBytes = %u\n", __func__, TotImmediateFlipBytes);
	dml_print("DML::%s: ImmediateFlipBW = %f\n", __func__, ImmediateFlipBW);
	dml_print("DML::%s: PDEAndMetaPTEBytesPerFrame = %f\n", __func__, PDEAndMetaPTEBytesPerFrame);
	dml_print("DML::%s: HostVMInefficiencyFactor = %f\n", __func__, HostVMInefficiencyFactor);
	dml_print("DML::%s: LineTime = %f\n", __func__, LineTime);
	dml_print("DML::%s: final_flip_bw = %f\n", __func__, *final_flip_bw);

	dml_print("DML::%s: DestinationLinesToRequestVMInImmediateFlip = %f\n", __func__, *DestinationLinesToRequestVMInImmediateFlip);
	dml_print("DML::%s: DestinationLinesToRequestRowInImmediateFlip = %f\n", __func__, *DestinationLinesToRequestRowInImmediateFlip);
	dml_print("DML::%s: TimeForFetchingMetaPTEImmediateFlip = %f\n", __func__, TimeForFetchingMetaPTEImmediateFlip);
	dml_print("DML::%s: TimeForFetchingRowInVBlankImmediateFlip = %f\n", __func__, TimeForFetchingRowInVBlankImmediateFlip);
	dml_print("DML::%s: min_row_time = %f\n", __func__, min_row_time);
	dml_print("DML::%s: ImmediateFlipSupportedForPipe = %u\n", __func__, *ImmediateFlipSupportedForPipe);
#endif
} // CalculateFlipSchedule

static dml_float_t RoundToDFSGranularity(dml_float_t Clock, dml_bool_t round_up, dml_float_t VCOSpeed)
{
	if (Clock <= 0.0)
		return 0.0;
	else {
		if (round_up)
			return VCOSpeed * 4.0 / dml_floor(VCOSpeed * 4.0 / Clock, 1.0);
		else
			return VCOSpeed * 4.0 / dml_ceil(VCOSpeed * 4.0 / Clock, 1.0);
	}
}

static void CalculateDCCConfiguration(
		dml_bool_t DCCEnabled,
		dml_bool_t DCCProgrammingAssumesScanDirectionUnknown,
		enum dml_source_format_class SourcePixelFormat,
		dml_uint_t SurfaceWidthLuma,
		dml_uint_t SurfaceWidthChroma,
		dml_uint_t SurfaceHeightLuma,
		dml_uint_t SurfaceHeightChroma,
		dml_uint_t nomDETInKByte,
		dml_uint_t RequestHeight256ByteLuma,
		dml_uint_t RequestHeight256ByteChroma,
		enum dml_swizzle_mode TilingFormat,
		dml_uint_t BytePerPixelY,
		dml_uint_t BytePerPixelC,
		dml_float_t BytePerPixelDETY,
		dml_float_t BytePerPixelDETC,
		enum dml_rotation_angle SourceScan,
		// Output
		dml_uint_t *MaxUncompressedBlockLuma,
		dml_uint_t *MaxUncompressedBlockChroma,
		dml_uint_t *MaxCompressedBlockLuma,
		dml_uint_t *MaxCompressedBlockChroma,
		dml_uint_t *IndependentBlockLuma,
		dml_uint_t *IndependentBlockChroma)
{
	dml_uint_t DETBufferSizeForDCC = nomDETInKByte * 1024;

	dml_uint_t yuv420;
	dml_uint_t horz_div_l;
	dml_uint_t horz_div_c;
	dml_uint_t vert_div_l;
	dml_uint_t vert_div_c;

	dml_uint_t swath_buf_size;
	dml_float_t detile_buf_vp_horz_limit;
	dml_float_t detile_buf_vp_vert_limit;

	dml_uint_t MAS_vp_horz_limit;
	dml_uint_t MAS_vp_vert_limit;
	dml_uint_t max_vp_horz_width;
	dml_uint_t max_vp_vert_height;
	dml_uint_t eff_surf_width_l;
	dml_uint_t eff_surf_width_c;
	dml_uint_t eff_surf_height_l;
	dml_uint_t eff_surf_height_c;

	dml_uint_t full_swath_bytes_horz_wc_l;
	dml_uint_t full_swath_bytes_horz_wc_c;
	dml_uint_t full_swath_bytes_vert_wc_l;
	dml_uint_t full_swath_bytes_vert_wc_c;

	dml_uint_t req128_horz_wc_l;
	dml_uint_t req128_horz_wc_c;
	dml_uint_t req128_vert_wc_l;
	dml_uint_t req128_vert_wc_c;

	dml_uint_t   segment_order_horz_contiguous_luma;
	dml_uint_t   segment_order_horz_contiguous_chroma;
	dml_uint_t   segment_order_vert_contiguous_luma;
	dml_uint_t   segment_order_vert_contiguous_chroma;

	typedef enum{
		REQ_256Bytes,
		REQ_128BytesNonContiguous,
		REQ_128BytesContiguous,
		REQ_NA
	} RequestType;

	RequestType   RequestLuma;
	RequestType   RequestChroma;

	yuv420 = ((SourcePixelFormat == dml_420_8 || SourcePixelFormat == dml_420_10 || SourcePixelFormat == dml_420_12) ? 1 : 0);
	horz_div_l = 1;
	horz_div_c = 1;
	vert_div_l = 1;
	vert_div_c = 1;

	if (BytePerPixelY == 1)
		vert_div_l = 0;
	if (BytePerPixelC == 1)
		vert_div_c = 0;

	if (BytePerPixelC == 0) {
		swath_buf_size = DETBufferSizeForDCC / 2 - 2 * 256;
		detile_buf_vp_horz_limit = (dml_float_t) swath_buf_size / ((dml_float_t) RequestHeight256ByteLuma * BytePerPixelY / (1 + horz_div_l));
		detile_buf_vp_vert_limit = (dml_float_t) swath_buf_size / (256.0 / RequestHeight256ByteLuma / (1 + vert_div_l));
	} else {
		swath_buf_size = DETBufferSizeForDCC / 2 - 2 * 2 * 256;
		detile_buf_vp_horz_limit = (dml_float_t) swath_buf_size / ((dml_float_t) RequestHeight256ByteLuma * BytePerPixelY / (1 + horz_div_l) + (dml_float_t) RequestHeight256ByteChroma * BytePerPixelC / (1 + horz_div_c) / (1 + yuv420));
		detile_buf_vp_vert_limit = (dml_float_t) swath_buf_size / (256.0 / RequestHeight256ByteLuma / (1 + vert_div_l) + 256.0 / RequestHeight256ByteChroma / (1 + vert_div_c) / (1 + yuv420));
	}

	if (SourcePixelFormat == dml_420_10) {
		detile_buf_vp_horz_limit = 1.5 * detile_buf_vp_horz_limit;
		detile_buf_vp_vert_limit = 1.5 * detile_buf_vp_vert_limit;
	}

	detile_buf_vp_horz_limit = dml_floor(detile_buf_vp_horz_limit - 1, 16);
	detile_buf_vp_vert_limit = dml_floor(detile_buf_vp_vert_limit - 1, 16);

	MAS_vp_horz_limit = SourcePixelFormat == dml_rgbe_alpha ? 3840 : 6144;
	MAS_vp_vert_limit = SourcePixelFormat == dml_rgbe_alpha ? 3840 : (BytePerPixelY == 8 ? 3072 : 6144);
	max_vp_horz_width = (dml_uint_t)(dml_min((dml_float_t) MAS_vp_horz_limit, detile_buf_vp_horz_limit));
	max_vp_vert_height = (dml_uint_t)(dml_min((dml_float_t) MAS_vp_vert_limit, detile_buf_vp_vert_limit));
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

	if (SourcePixelFormat == dml_420_10) {
		full_swath_bytes_horz_wc_l = (dml_uint_t)(dml_ceil((dml_float_t) full_swath_bytes_horz_wc_l * 2.0 / 3.0, 256.0));
		full_swath_bytes_horz_wc_c = (dml_uint_t)(dml_ceil((dml_float_t) full_swath_bytes_horz_wc_c * 2.0 / 3.0, 256.0));
		full_swath_bytes_vert_wc_l = (dml_uint_t)(dml_ceil((dml_float_t) full_swath_bytes_vert_wc_l * 2.0 / 3.0, 256.0));
		full_swath_bytes_vert_wc_c = (dml_uint_t)(dml_ceil((dml_float_t) full_swath_bytes_vert_wc_c * 2.0 / 3.0, 256.0));
	}

	if (2 * full_swath_bytes_horz_wc_l + 2 * full_swath_bytes_horz_wc_c <= DETBufferSizeForDCC) {
		req128_horz_wc_l = 0;
		req128_horz_wc_c = 0;
	} else if (full_swath_bytes_horz_wc_l < 1.5 * full_swath_bytes_horz_wc_c && 2 * full_swath_bytes_horz_wc_l + full_swath_bytes_horz_wc_c <= DETBufferSizeForDCC) {
		req128_horz_wc_l = 0;
		req128_horz_wc_c = 1;
	} else if (full_swath_bytes_horz_wc_l >= 1.5 * full_swath_bytes_horz_wc_c && full_swath_bytes_horz_wc_l + 2 * full_swath_bytes_horz_wc_c <= DETBufferSizeForDCC) {
		req128_horz_wc_l = 1;
		req128_horz_wc_c = 0;
	} else {
		req128_horz_wc_l = 1;
		req128_horz_wc_c = 1;
	}

	if (2 * full_swath_bytes_vert_wc_l + 2 * full_swath_bytes_vert_wc_c <= DETBufferSizeForDCC) {
		req128_vert_wc_l = 0;
		req128_vert_wc_c = 0;
	} else if (full_swath_bytes_vert_wc_l < 1.5 * full_swath_bytes_vert_wc_c && 2 * full_swath_bytes_vert_wc_l + full_swath_bytes_vert_wc_c <= DETBufferSizeForDCC) {
		req128_vert_wc_l = 0;
		req128_vert_wc_c = 1;
	} else if (full_swath_bytes_vert_wc_l >= 1.5 * full_swath_bytes_vert_wc_c && full_swath_bytes_vert_wc_l + 2 * full_swath_bytes_vert_wc_c <= DETBufferSizeForDCC) {
		req128_vert_wc_l = 1;
		req128_vert_wc_c = 0;
	} else {
		req128_vert_wc_l = 1;
		req128_vert_wc_c = 1;
	}

	if (BytePerPixelY == 2) {
		segment_order_horz_contiguous_luma = 0;
		segment_order_vert_contiguous_luma = 1;
	} else {
		segment_order_horz_contiguous_luma = 1;
		segment_order_vert_contiguous_luma = 0;
	}

	if (BytePerPixelC == 2) {
		segment_order_horz_contiguous_chroma = 0;
		segment_order_vert_contiguous_chroma = 1;
	} else {
		segment_order_horz_contiguous_chroma = 1;
		segment_order_vert_contiguous_chroma = 0;
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DCCEnabled = %u\n", __func__, DCCEnabled);
	dml_print("DML::%s: nomDETInKByte = %u\n", __func__, nomDETInKByte);
	dml_print("DML::%s: DETBufferSizeForDCC = %u\n", __func__, DETBufferSizeForDCC);
	dml_print("DML::%s: req128_horz_wc_l = %u\n", __func__, req128_horz_wc_l);
	dml_print("DML::%s: req128_horz_wc_c = %u\n", __func__, req128_horz_wc_c);
	dml_print("DML::%s: full_swath_bytes_horz_wc_l = %u\n", __func__, full_swath_bytes_horz_wc_l);
	dml_print("DML::%s: full_swath_bytes_vert_wc_c = %u\n", __func__, full_swath_bytes_vert_wc_c);
	dml_print("DML::%s: segment_order_horz_contiguous_luma = %u\n", __func__, segment_order_horz_contiguous_luma);
	dml_print("DML::%s: segment_order_horz_contiguous_chroma = %u\n", __func__, segment_order_horz_contiguous_chroma);
#endif

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
	} else if (!dml_is_vertical_rotation(SourceScan)) {
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

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: MaxUncompressedBlockLuma = %u\n", __func__, *MaxUncompressedBlockLuma);
	dml_print("DML::%s: MaxCompressedBlockLuma = %u\n", __func__, *MaxCompressedBlockLuma);
	dml_print("DML::%s: IndependentBlockLuma = %u\n", __func__, *IndependentBlockLuma);
	dml_print("DML::%s: MaxUncompressedBlockChroma = %u\n", __func__, *MaxUncompressedBlockChroma);
	dml_print("DML::%s: MaxCompressedBlockChroma = %u\n", __func__, *MaxCompressedBlockChroma);
	dml_print("DML::%s: IndependentBlockChroma = %u\n", __func__, *IndependentBlockChroma);
#endif

} // CalculateDCCConfiguration

static dml_uint_t CalculatePrefetchSourceLines(
		dml_float_t VRatio,
		dml_uint_t VTaps,
		dml_bool_t Interlace,
		dml_bool_t ProgressiveToInterlaceUnitInOPP,
		dml_uint_t SwathHeight,
		enum dml_rotation_angle SourceScan,
		dml_bool_t ViewportStationary,
		dml_uint_t SwathWidth,
		dml_uint_t ViewportHeight,
		dml_uint_t ViewportXStart,
		dml_uint_t ViewportYStart,

		// Output
		dml_uint_t *VInitPreFill,
		dml_uint_t *MaxNumSwath)
{

	dml_uint_t vp_start_rot = 0;
	dml_uint_t sw0_tmp = 0;
	dml_uint_t MaxPartialSwath = 0;
	dml_float_t numLines = 0;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: VRatio = %f\n", __func__, VRatio);
	dml_print("DML::%s: VTaps = %u\n", __func__, VTaps);
	dml_print("DML::%s: ViewportXStart = %u\n", __func__, ViewportXStart);
	dml_print("DML::%s: ViewportYStart = %u\n", __func__, ViewportYStart);
	dml_print("DML::%s: ViewportStationary = %u\n", __func__, ViewportStationary);
	dml_print("DML::%s: SwathHeight = %u\n", __func__, SwathHeight);
#endif
	if (ProgressiveToInterlaceUnitInOPP)
		*VInitPreFill = (dml_uint_t)(dml_floor((VRatio + (dml_float_t) VTaps + 1) / 2.0, 1));
	else
		*VInitPreFill = (dml_uint_t)(dml_floor((VRatio + (dml_float_t) VTaps + 1 + Interlace * 0.5 * VRatio) / 2.0, 1));

	if (ViewportStationary) {
		if (SourceScan == dml_rotation_180 || SourceScan == dml_rotation_180m) {
			vp_start_rot = SwathHeight - (((dml_uint_t) (ViewportYStart + ViewportHeight - 1) % SwathHeight) + 1);
		} else if (SourceScan == dml_rotation_270 || SourceScan == dml_rotation_90m) {
			vp_start_rot = ViewportXStart;
		} else if (SourceScan == dml_rotation_90 || SourceScan == dml_rotation_270m) {
			vp_start_rot = SwathHeight - (((dml_uint_t)(ViewportYStart + SwathWidth - 1) % SwathHeight) + 1);
		} else {
			vp_start_rot = ViewportYStart;
		}
		sw0_tmp = SwathHeight - (vp_start_rot % SwathHeight);
		if (sw0_tmp < *VInitPreFill) {
			*MaxNumSwath = (dml_uint_t)(dml_ceil((*VInitPreFill - sw0_tmp) / (dml_float_t) SwathHeight, 1) + 1);
		} else {
			*MaxNumSwath = 1;
		}
		MaxPartialSwath = (dml_uint_t)(dml_max(1, (dml_uint_t) (vp_start_rot + *VInitPreFill - 1) % SwathHeight));
	} else {
		*MaxNumSwath = (dml_uint_t)(dml_ceil((*VInitPreFill - 1.0) / (dml_float_t) SwathHeight, 1) + 1);
		if (*VInitPreFill > 1) {
			MaxPartialSwath = (dml_uint_t)(dml_max(1, (dml_uint_t) (*VInitPreFill - 2) % SwathHeight));
		} else {
			MaxPartialSwath = (dml_uint_t)(dml_max(1, (dml_uint_t) (*VInitPreFill + SwathHeight - 2) % SwathHeight));
		}
	}
	numLines = *MaxNumSwath * SwathHeight + MaxPartialSwath;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: vp_start_rot = %u\n", __func__, vp_start_rot);
	dml_print("DML::%s: VInitPreFill = %u\n", __func__, *VInitPreFill);
	dml_print("DML::%s: MaxPartialSwath = %u\n", __func__, MaxPartialSwath);
	dml_print("DML::%s: MaxNumSwath = %u\n", __func__, *MaxNumSwath);
	dml_print("DML::%s: Prefetch source lines = %3.2f\n", __func__, numLines);
#endif
	return (dml_uint_t)(numLines);

} // CalculatePrefetchSourceLines

static dml_uint_t CalculateVMAndRowBytes(
		dml_bool_t ViewportStationary,
		dml_bool_t DCCEnable,
		dml_uint_t NumberOfDPPs,
		dml_uint_t BlockHeight256Bytes,
		dml_uint_t BlockWidth256Bytes,
		enum dml_source_format_class SourcePixelFormat,
		dml_uint_t SurfaceTiling,
		dml_uint_t BytePerPixel,
		enum dml_rotation_angle SourceScan,
		dml_uint_t SwathWidth,
		dml_uint_t ViewportHeight,
		dml_uint_t ViewportXStart,
		dml_uint_t ViewportYStart,
		dml_bool_t GPUVMEnable,
		dml_uint_t GPUVMMaxPageTableLevels,
		dml_uint_t GPUVMMinPageSizeKBytes,
		dml_uint_t PTEBufferSizeInRequests,
		dml_uint_t Pitch,
		dml_uint_t DCCMetaPitch,
		dml_uint_t MacroTileWidth,
		dml_uint_t MacroTileHeight,

		// Output
		dml_uint_t *MetaRowByte,
		dml_uint_t *PixelPTEBytesPerRow, // for bandwidth calculation
		dml_uint_t *PixelPTEBytesPerRowStorage, // for PTE buffer size check
		dml_uint_t *dpte_row_width_ub,
		dml_uint_t *dpte_row_height,
		dml_uint_t *dpte_row_height_linear,
		dml_uint_t *PixelPTEBytesPerRow_one_row_per_frame,
		dml_uint_t *dpte_row_width_ub_one_row_per_frame,
		dml_uint_t *dpte_row_height_one_row_per_frame,
		dml_uint_t *MetaRequestWidth,
		dml_uint_t *MetaRequestHeight,
		dml_uint_t *meta_row_width,
		dml_uint_t *meta_row_height,
		dml_uint_t *PixelPTEReqWidth,
		dml_uint_t *PixelPTEReqHeight,
		dml_uint_t *PTERequestSize,
		dml_uint_t *DPDE0BytesFrame,
		dml_uint_t *MetaPTEBytesFrame)
{
	dml_uint_t MPDEBytesFrame;
	dml_uint_t DCCMetaSurfaceBytes;
	dml_uint_t ExtraDPDEBytesFrame;
	dml_uint_t PDEAndMetaPTEBytesFrame;
	dml_uint_t MacroTileSizeBytes;
	dml_uint_t vp_height_meta_ub;
	dml_uint_t vp_height_dpte_ub;

	dml_uint_t PixelPTEReqWidth_linear = 0; // VBA_DELTA. VBA doesn't calculate this

	*MetaRequestHeight = 8 * BlockHeight256Bytes;
	*MetaRequestWidth = 8 * BlockWidth256Bytes;
	if (SurfaceTiling == dml_sw_linear) {
		*meta_row_height = 32;
		*meta_row_width = (dml_uint_t)(dml_floor(ViewportXStart + SwathWidth + *MetaRequestWidth - 1, *MetaRequestWidth) - dml_floor(ViewportXStart, *MetaRequestWidth));
	} else if (!dml_is_vertical_rotation(SourceScan)) {
		*meta_row_height = *MetaRequestHeight;
		if (ViewportStationary && NumberOfDPPs == 1) {
			*meta_row_width = (dml_uint_t)(dml_floor(ViewportXStart + SwathWidth + *MetaRequestWidth - 1, *MetaRequestWidth) - dml_floor(ViewportXStart, *MetaRequestWidth));
		} else {
			*meta_row_width = (dml_uint_t)(dml_ceil(SwathWidth - 1, *MetaRequestWidth) + *MetaRequestWidth);
		}
		*MetaRowByte = (dml_uint_t)(*meta_row_width * *MetaRequestHeight * BytePerPixel / 256.0);
	} else {
		*meta_row_height = *MetaRequestWidth;
		if (ViewportStationary && NumberOfDPPs == 1) {
			*meta_row_width = (dml_uint_t)(dml_floor(ViewportYStart + ViewportHeight + *MetaRequestHeight - 1, *MetaRequestHeight) - dml_floor(ViewportYStart, *MetaRequestHeight));
		} else {
			*meta_row_width = (dml_uint_t)(dml_ceil(SwathWidth - 1, *MetaRequestHeight) + *MetaRequestHeight);
		}
		*MetaRowByte = (dml_uint_t)(*meta_row_width * *MetaRequestWidth * BytePerPixel / 256.0);
	}

	if (ViewportStationary && (NumberOfDPPs == 1 || !dml_is_vertical_rotation(SourceScan))) {
		vp_height_meta_ub = (dml_uint_t)(dml_floor(ViewportYStart + ViewportHeight + 64 * BlockHeight256Bytes - 1, 64 * BlockHeight256Bytes) - dml_floor(ViewportYStart, 64 * BlockHeight256Bytes));
	} else if (!dml_is_vertical_rotation(SourceScan)) {
		vp_height_meta_ub = (dml_uint_t)(dml_ceil(ViewportHeight - 1, 64 * BlockHeight256Bytes) + 64 * BlockHeight256Bytes);
	} else {
		vp_height_meta_ub = (dml_uint_t)(dml_ceil(SwathWidth - 1, 64 * BlockHeight256Bytes) + 64 * BlockHeight256Bytes);
	}

	DCCMetaSurfaceBytes = (dml_uint_t)(DCCMetaPitch * vp_height_meta_ub * BytePerPixel / 256.0);

	if (GPUVMEnable == true) {
		*MetaPTEBytesFrame = (dml_uint_t)((dml_ceil((dml_float_t) (DCCMetaSurfaceBytes - 4.0 * 1024.0) / (8 * 4.0 * 1024), 1) + 1) * 64);
		MPDEBytesFrame = 128 * (GPUVMMaxPageTableLevels - 1);
	} else {
		*MetaPTEBytesFrame = 0;
		MPDEBytesFrame = 0;
	}

	if (DCCEnable != true) {
		*MetaPTEBytesFrame = 0;
		MPDEBytesFrame = 0;
		*MetaRowByte = 0;
	}

	MacroTileSizeBytes = MacroTileWidth * BytePerPixel * MacroTileHeight;

	if (ViewportStationary && (NumberOfDPPs == 1 || !dml_is_vertical_rotation(SourceScan))) {
		vp_height_dpte_ub = (dml_uint_t)(dml_floor(ViewportYStart + ViewportHeight + MacroTileHeight - 1, MacroTileHeight) - dml_floor(ViewportYStart, MacroTileHeight));
	} else if (!dml_is_vertical_rotation(SourceScan)) {
		vp_height_dpte_ub = (dml_uint_t)(dml_ceil(ViewportHeight - 1, MacroTileHeight) + MacroTileHeight);
	} else {
		vp_height_dpte_ub = (dml_uint_t)(dml_ceil(SwathWidth - 1, MacroTileHeight) + MacroTileHeight);
	}

	if (GPUVMEnable == true && GPUVMMaxPageTableLevels > 1) {
		*DPDE0BytesFrame = (dml_uint_t)(64 * (dml_ceil((dml_float_t) (Pitch * vp_height_dpte_ub * BytePerPixel - MacroTileSizeBytes) / (dml_float_t) (8 * 2097152), 1) + 1));
		ExtraDPDEBytesFrame = 128 * (GPUVMMaxPageTableLevels - 2);
	} else {
		*DPDE0BytesFrame = 0;
		ExtraDPDEBytesFrame = 0;
	}

	PDEAndMetaPTEBytesFrame = *MetaPTEBytesFrame + MPDEBytesFrame + *DPDE0BytesFrame + ExtraDPDEBytesFrame;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DCCEnable = %u\n", __func__, DCCEnable);
	dml_print("DML::%s: GPUVMEnable = %u\n", __func__, GPUVMEnable);
	dml_print("DML::%s: SwModeLinear = %u\n", __func__, SurfaceTiling == dml_sw_linear);
	dml_print("DML::%s: BytePerPixel = %u\n", __func__, BytePerPixel);
	dml_print("DML::%s: GPUVMMaxPageTableLevels = %u\n", __func__, GPUVMMaxPageTableLevels);
	dml_print("DML::%s: BlockHeight256Bytes = %u\n", __func__, BlockHeight256Bytes);
	dml_print("DML::%s: BlockWidth256Bytes = %u\n", __func__, BlockWidth256Bytes);
	dml_print("DML::%s: MacroTileHeight = %u\n", __func__, MacroTileHeight);
	dml_print("DML::%s: MacroTileWidth = %u\n", __func__, MacroTileWidth);
	dml_print("DML::%s: MetaPTEBytesFrame = %u\n", __func__, *MetaPTEBytesFrame);
	dml_print("DML::%s: MPDEBytesFrame = %u\n", __func__, MPDEBytesFrame);
	dml_print("DML::%s: DPDE0BytesFrame = %u\n", __func__, *DPDE0BytesFrame);
	dml_print("DML::%s: ExtraDPDEBytesFrame= %u\n", __func__, ExtraDPDEBytesFrame);
	dml_print("DML::%s: PDEAndMetaPTEBytesFrame = %u\n", __func__, PDEAndMetaPTEBytesFrame);
	dml_print("DML::%s: ViewportHeight = %u\n", __func__, ViewportHeight);
	dml_print("DML::%s: SwathWidth = %u\n", __func__, SwathWidth);
	dml_print("DML::%s: vp_height_dpte_ub = %u\n", __func__, vp_height_dpte_ub);
#endif

	if (SurfaceTiling == dml_sw_linear) {
		*PixelPTEReqHeight = 1;
		*PixelPTEReqWidth = GPUVMMinPageSizeKBytes * 1024 * 8 / BytePerPixel;
		PixelPTEReqWidth_linear = GPUVMMinPageSizeKBytes * 1024 * 8 / BytePerPixel;
		*PTERequestSize = 64;
	} else if (GPUVMMinPageSizeKBytes == 4) {
		*PixelPTEReqHeight = 16 * BlockHeight256Bytes;
		*PixelPTEReqWidth = 16 * BlockWidth256Bytes;
		*PTERequestSize = 128;
	} else {
		*PixelPTEReqHeight = MacroTileHeight;
		*PixelPTEReqWidth = 8 *  1024 * GPUVMMinPageSizeKBytes / (MacroTileHeight * BytePerPixel);
		*PTERequestSize = 64;
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: GPUVMMinPageSizeKBytes = %u\n", __func__, GPUVMMinPageSizeKBytes);
	dml_print("DML::%s: PDEAndMetaPTEBytesFrame = %u (after HostVM factor)\n", __func__, PDEAndMetaPTEBytesFrame);
	dml_print("DML::%s: PixelPTEReqHeight = %u\n", __func__, *PixelPTEReqHeight);
	dml_print("DML::%s: PixelPTEReqWidth = %u\n", __func__, *PixelPTEReqWidth);
	dml_print("DML::%s: PixelPTEReqWidth_linear = %u\n", __func__, PixelPTEReqWidth_linear);
	dml_print("DML::%s: PTERequestSize = %u\n", __func__, *PTERequestSize);
	dml_print("DML::%s: Pitch = %u\n", __func__, Pitch);
#endif

	*dpte_row_height_one_row_per_frame = vp_height_dpte_ub;
	*dpte_row_width_ub_one_row_per_frame = (dml_uint_t)((dml_ceil(((dml_float_t)Pitch * (dml_float_t) *dpte_row_height_one_row_per_frame / (dml_float_t) *PixelPTEReqHeight - 1) / (dml_float_t) *PixelPTEReqWidth, 1) + 1) * (dml_float_t) *PixelPTEReqWidth);
	*PixelPTEBytesPerRow_one_row_per_frame = (dml_uint_t)((dml_float_t) *dpte_row_width_ub_one_row_per_frame / (dml_float_t) *PixelPTEReqWidth * *PTERequestSize);

	if (SurfaceTiling == dml_sw_linear) {
		*dpte_row_height = (dml_uint_t)(dml_min(128, 1 << (dml_uint_t) dml_floor(dml_log2(PTEBufferSizeInRequests * *PixelPTEReqWidth / Pitch), 1)));
		dml_print("DML::%s: dpte_row_height term 1 = %u\n", __func__, PTEBufferSizeInRequests * *PixelPTEReqWidth / Pitch);
		dml_print("DML::%s: dpte_row_height term 2 = %f\n", __func__, dml_log2(PTEBufferSizeInRequests * *PixelPTEReqWidth / Pitch));
		dml_print("DML::%s: dpte_row_height term 3 = %f\n", __func__, dml_floor(dml_log2(PTEBufferSizeInRequests * *PixelPTEReqWidth / Pitch), 1));
		dml_print("DML::%s: dpte_row_height term 4 = %u\n", __func__, 1 << (dml_uint_t) dml_floor(dml_log2(PTEBufferSizeInRequests * *PixelPTEReqWidth / Pitch), 1));
		dml_print("DML::%s: dpte_row_height = %u\n", __func__, *dpte_row_height);

		*dpte_row_width_ub = (dml_uint_t)(dml_ceil(((dml_float_t) Pitch * (dml_float_t) *dpte_row_height - 1), (dml_float_t) *PixelPTEReqWidth) + *PixelPTEReqWidth);
		*PixelPTEBytesPerRow = (dml_uint_t)((dml_float_t) *dpte_row_width_ub / (dml_float_t) *PixelPTEReqWidth * *PTERequestSize);

		// VBA_DELTA, VBA doesn't have programming value for pte row height linear.
		*dpte_row_height_linear = 1 << (dml_uint_t) dml_floor(dml_log2(PTEBufferSizeInRequests * PixelPTEReqWidth_linear / Pitch), 1);
		if (*dpte_row_height_linear > 128)
			*dpte_row_height_linear = 128;

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: dpte_row_width_ub = %u (linear)\n", __func__, *dpte_row_width_ub);
#endif

	} else if (!dml_is_vertical_rotation(SourceScan)) {
		*dpte_row_height = *PixelPTEReqHeight;

		if (GPUVMMinPageSizeKBytes > 64) {
			*dpte_row_width_ub = (dml_uint_t)((dml_ceil(((dml_float_t) Pitch * (dml_float_t) *dpte_row_height / (dml_float_t) *PixelPTEReqHeight - 1) / (dml_float_t) *PixelPTEReqWidth, 1) + 1) * *PixelPTEReqWidth);
		} else if (ViewportStationary && (NumberOfDPPs == 1)) {
			*dpte_row_width_ub = (dml_uint_t)(dml_floor(ViewportXStart + SwathWidth + *PixelPTEReqWidth - 1, *PixelPTEReqWidth) - dml_floor(ViewportXStart, *PixelPTEReqWidth));
		} else {
			*dpte_row_width_ub = (dml_uint_t)((dml_ceil((dml_float_t) (SwathWidth - 1) / (dml_float_t)*PixelPTEReqWidth, 1) + 1.0) * *PixelPTEReqWidth);
		}
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: dpte_row_width_ub = %u (tiled horz)\n", __func__, *dpte_row_width_ub);
#endif

		ASSERT(*PixelPTEReqWidth);
		if (*PixelPTEReqWidth != 0)
			*PixelPTEBytesPerRow = *dpte_row_width_ub / *PixelPTEReqWidth * *PTERequestSize;
	} else {
		*dpte_row_height = (dml_uint_t)(dml_min(*PixelPTEReqWidth, MacroTileWidth));

		if (ViewportStationary && (NumberOfDPPs == 1)) {
			*dpte_row_width_ub = (dml_uint_t)(dml_floor(ViewportYStart + ViewportHeight + *PixelPTEReqHeight - 1, *PixelPTEReqHeight) - dml_floor(ViewportYStart, *PixelPTEReqHeight));
		} else {
			*dpte_row_width_ub = (dml_uint_t)((dml_ceil((dml_float_t) (SwathWidth - 1) / (dml_float_t) *PixelPTEReqHeight, 1) + 1) * *PixelPTEReqHeight);
		}

		*PixelPTEBytesPerRow = (dml_uint_t)((dml_float_t) *dpte_row_width_ub / (dml_float_t) *PixelPTEReqHeight * *PTERequestSize);
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: dpte_row_width_ub = %u (tiled vert)\n", __func__, *dpte_row_width_ub);
#endif
	}

	if (GPUVMEnable != true)
		*PixelPTEBytesPerRow = 0;

	*PixelPTEBytesPerRowStorage = *PixelPTEBytesPerRow;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: GPUVMMinPageSizeKBytes = %u\n", __func__, GPUVMMinPageSizeKBytes);
	dml_print("DML::%s: GPUVMEnable = %u\n", __func__, GPUVMEnable);
	dml_print("DML::%s: dpte_row_height = %u\n", __func__, *dpte_row_height);
	dml_print("DML::%s: dpte_row_height_linear = %u\n", __func__, *dpte_row_height_linear);
	dml_print("DML::%s: dpte_row_width_ub = %u\n", __func__, *dpte_row_width_ub);
	dml_print("DML::%s: PixelPTEBytesPerRow = %u\n", __func__, *PixelPTEBytesPerRow);
	dml_print("DML::%s: PixelPTEBytesPerRowStorage = %u\n", __func__, *PixelPTEBytesPerRowStorage);
	dml_print("DML::%s: PTEBufferSizeInRequests = %u\n", __func__, PTEBufferSizeInRequests);
	dml_print("DML::%s: dpte_row_height_one_row_per_frame = %u\n", __func__, *dpte_row_height_one_row_per_frame);
	dml_print("DML::%s: dpte_row_width_ub_one_row_per_frame = %u\n", __func__, *dpte_row_width_ub_one_row_per_frame);
	dml_print("DML::%s: PixelPTEBytesPerRow_one_row_per_frame = %u\n", __func__, *PixelPTEBytesPerRow_one_row_per_frame);
#endif

	dml_print("DML: vm_bytes = meta_pte_bytes_per_frame (per_pipe) = MetaPTEBytesFrame = : %i\n", *MetaPTEBytesFrame);

	return PDEAndMetaPTEBytesFrame;
} // CalculateVMAndRowBytes

static void PixelClockAdjustmentForProgressiveToInterlaceUnit(struct dml_display_cfg_st *display_cfg, dml_bool_t ptoi_supported)
{
	dml_uint_t num_active_planes = dml_get_num_active_planes(display_cfg);

	//Progressive To Interlace Unit Effect
	for (dml_uint_t k = 0; k < num_active_planes; ++k) {
		display_cfg->output.PixelClockBackEnd[k] = display_cfg->timing.PixelClock[k];
		if (display_cfg->timing.Interlace[k] == 1 && ptoi_supported == true) {
			display_cfg->timing.PixelClock[k] = 2 * display_cfg->timing.PixelClock[k];
		}
	}
}

static dml_float_t TruncToValidBPP(
		dml_float_t LinkBitRate,
		dml_uint_t Lanes,
		dml_uint_t HTotal,
		dml_uint_t HActive,
		dml_float_t PixelClock,
		dml_float_t DesiredBPP,
		dml_bool_t DSCEnable,
		enum dml_output_encoder_class Output,
		enum dml_output_format_class Format,
		dml_uint_t DSCInputBitPerComponent,
		dml_uint_t DSCSlices,
		dml_uint_t AudioRate,
		dml_uint_t AudioLayout,
		enum dml_odm_mode ODMModeNoDSC,
		enum dml_odm_mode ODMModeDSC,

		// Output
		dml_uint_t *RequiredSlots)
{
	dml_float_t MaxLinkBPP;
	dml_uint_t MinDSCBPP;
	dml_float_t MaxDSCBPP;
	dml_uint_t NonDSCBPP0;
	dml_uint_t NonDSCBPP1;
	dml_uint_t NonDSCBPP2;

	if (Format == dml_420) {
		NonDSCBPP0 = 12;
		NonDSCBPP1 = 15;
		NonDSCBPP2 = 18;
		MinDSCBPP = 6;
		MaxDSCBPP = 1.5 * DSCInputBitPerComponent - 1 / 16;
	} else if (Format == dml_444) {
		NonDSCBPP0 = 24;
		NonDSCBPP1 = 30;
		NonDSCBPP2 = 36;
		MinDSCBPP = 8;
		MaxDSCBPP = 3 * DSCInputBitPerComponent - 1.0 / 16;
	} else {
		if (Output == dml_hdmi) {
			NonDSCBPP0 = 24;
			NonDSCBPP1 = 24;
			NonDSCBPP2 = 24;
		} else {
			NonDSCBPP0 = 16;
			NonDSCBPP1 = 20;
			NonDSCBPP2 = 24;
	}
	if (Format == dml_n422) {
		MinDSCBPP = 7;
			MaxDSCBPP = 2 * DSCInputBitPerComponent - 1.0 / 16.0;
		} else {
			MinDSCBPP = 8;
			MaxDSCBPP = 3 * DSCInputBitPerComponent - 1.0 / 16.0;
		}
	}

	if (Output == dml_dp2p0) {
		MaxLinkBPP = LinkBitRate * Lanes / PixelClock * 128.0 / 132.0 * 383.0 / 384.0 * 65536.0 / 65540.0;
	} else if (DSCEnable && Output == dml_dp) {
		MaxLinkBPP = LinkBitRate / 10.0 * 8.0 * Lanes / PixelClock * (1 - 2.4 / 100);
	} else {
		MaxLinkBPP = LinkBitRate / 10.0 * 8.0 * Lanes / PixelClock;
	}

	if (DSCEnable) {
		if (ODMModeDSC == dml_odm_mode_combine_4to1) {
			MaxLinkBPP = dml_min(MaxLinkBPP, 16);
		} else if (ODMModeDSC == dml_odm_mode_combine_2to1) {
			MaxLinkBPP = dml_min(MaxLinkBPP, 32);
		} else if (ODMModeDSC == dml_odm_mode_split_1to2) {
			MaxLinkBPP = 2 * MaxLinkBPP;
		}
	} else {
		if (ODMModeNoDSC == dml_odm_mode_combine_4to1) {
			MaxLinkBPP = dml_min(MaxLinkBPP, 16);
		} else if (ODMModeNoDSC == dml_odm_mode_combine_2to1) {
			MaxLinkBPP = dml_min(MaxLinkBPP, 32);
		} else if (ODMModeNoDSC == dml_odm_mode_split_1to2) {
			MaxLinkBPP = 2 * MaxLinkBPP;
		}
	}

	*RequiredSlots = (dml_uint_t)(dml_ceil(DesiredBPP / MaxLinkBPP * 64, 1));

	if (DesiredBPP == 0) {
		if (DSCEnable) {
			if (MaxLinkBPP < MinDSCBPP) {
				return __DML_DPP_INVALID__;
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
				return __DML_DPP_INVALID__;
			}
		}
	} else {
		if (!((DSCEnable == false && (DesiredBPP == NonDSCBPP2 || DesiredBPP == NonDSCBPP1 || DesiredBPP == NonDSCBPP0)) ||
				(DSCEnable && DesiredBPP >= MinDSCBPP && DesiredBPP <= MaxDSCBPP))) {
			return __DML_DPP_INVALID__;
		} else {
			return DesiredBPP;
		}
	}
} // TruncToValidBPP

static void CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport(
		struct display_mode_lib_scratch_st *scratch,
		struct CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params_st *p)
{
	struct CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_locals_st *s = &scratch->CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_locals;

	s->TotalActiveWriteback = 0;
	p->Watermark->UrgentWatermark = p->mmSOCParameters.UrgentLatency + p->mmSOCParameters.ExtraLatency;
	p->Watermark->USRRetrainingWatermark = p->mmSOCParameters.UrgentLatency + p->mmSOCParameters.ExtraLatency + p->mmSOCParameters.USRRetrainingLatency + p->mmSOCParameters.SMNLatency;
	p->Watermark->DRAMClockChangeWatermark = p->mmSOCParameters.DRAMClockChangeLatency + p->Watermark->UrgentWatermark;
	p->Watermark->FCLKChangeWatermark = p->mmSOCParameters.FCLKChangeLatency + p->Watermark->UrgentWatermark;
	p->Watermark->StutterExitWatermark = p->mmSOCParameters.SRExitTime + p->mmSOCParameters.ExtraLatency + 10 / p->DCFClkDeepSleep;
	p->Watermark->StutterEnterPlusExitWatermark = p->mmSOCParameters.SREnterPlusExitTime + p->mmSOCParameters.ExtraLatency + 10 / p->DCFClkDeepSleep;
	p->Watermark->Z8StutterExitWatermark = p->mmSOCParameters.SRExitZ8Time + p->mmSOCParameters.ExtraLatency + 10 / p->DCFClkDeepSleep;
	p->Watermark->Z8StutterEnterPlusExitWatermark = p->mmSOCParameters.SREnterPlusExitZ8Time + p->mmSOCParameters.ExtraLatency + 10 / p->DCFClkDeepSleep;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: UrgentLatency = %f\n", __func__, p->mmSOCParameters.UrgentLatency);
	dml_print("DML::%s: ExtraLatency = %f\n", __func__, p->mmSOCParameters.ExtraLatency);
	dml_print("DML::%s: DRAMClockChangeLatency = %f\n", __func__, p->mmSOCParameters.DRAMClockChangeLatency);
	dml_print("DML::%s: UrgentWatermark = %f\n", __func__, p->Watermark->UrgentWatermark);
	dml_print("DML::%s: USRRetrainingWatermark = %f\n", __func__, p->Watermark->USRRetrainingWatermark);
	dml_print("DML::%s: DRAMClockChangeWatermark = %f\n", __func__, p->Watermark->DRAMClockChangeWatermark);
	dml_print("DML::%s: FCLKChangeWatermark = %f\n", __func__, p->Watermark->FCLKChangeWatermark);
	dml_print("DML::%s: StutterExitWatermark = %f\n", __func__, p->Watermark->StutterExitWatermark);
	dml_print("DML::%s: StutterEnterPlusExitWatermark = %f\n", __func__, p->Watermark->StutterEnterPlusExitWatermark);
	dml_print("DML::%s: Z8StutterExitWatermark = %f\n", __func__, p->Watermark->Z8StutterExitWatermark);
	dml_print("DML::%s: Z8StutterEnterPlusExitWatermark = %f\n", __func__, p->Watermark->Z8StutterEnterPlusExitWatermark);
#endif

	s->TotalActiveWriteback = 0;
	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (p->WritebackEnable[k] == true) {
			s->TotalActiveWriteback = s->TotalActiveWriteback + 1;
		}
	}

	if (s->TotalActiveWriteback <= 1) {
		p->Watermark->WritebackUrgentWatermark = p->mmSOCParameters.WritebackLatency;
	} else {
		p->Watermark->WritebackUrgentWatermark = p->mmSOCParameters.WritebackLatency + p->WritebackChunkSize * 1024.0 / 32.0 / p->SOCCLK;
	}
	if (p->USRRetrainingRequiredFinal)
		p->Watermark->WritebackUrgentWatermark = p->Watermark->WritebackUrgentWatermark + p->mmSOCParameters.USRRetrainingLatency;

	if (s->TotalActiveWriteback <= 1) {
		p->Watermark->WritebackDRAMClockChangeWatermark = p->mmSOCParameters.DRAMClockChangeLatency + p->mmSOCParameters.WritebackLatency;
		p->Watermark->WritebackFCLKChangeWatermark = p->mmSOCParameters.FCLKChangeLatency + p->mmSOCParameters.WritebackLatency;
	} else {
		p->Watermark->WritebackDRAMClockChangeWatermark = p->mmSOCParameters.DRAMClockChangeLatency + p->mmSOCParameters.WritebackLatency + p->WritebackChunkSize * 1024.0 / 32.0 / p->SOCCLK;
		p->Watermark->WritebackFCLKChangeWatermark = p->mmSOCParameters.FCLKChangeLatency + p->mmSOCParameters.WritebackLatency + p->WritebackChunkSize * 1024 / 32 / p->SOCCLK;
	}

	if (p->USRRetrainingRequiredFinal)
		p->Watermark->WritebackDRAMClockChangeWatermark = p->Watermark->WritebackDRAMClockChangeWatermark + p->mmSOCParameters.USRRetrainingLatency;

	if (p->USRRetrainingRequiredFinal)
		p->Watermark->WritebackFCLKChangeWatermark = p->Watermark->WritebackFCLKChangeWatermark + p->mmSOCParameters.USRRetrainingLatency;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: WritebackDRAMClockChangeWatermark = %f\n", __func__, p->Watermark->WritebackDRAMClockChangeWatermark);
	dml_print("DML::%s: WritebackFCLKChangeWatermark = %f\n", __func__, p->Watermark->WritebackFCLKChangeWatermark);
	dml_print("DML::%s: WritebackUrgentWatermark = %f\n", __func__, p->Watermark->WritebackUrgentWatermark);
	dml_print("DML::%s: USRRetrainingRequiredFinal = %u\n", __func__, p->USRRetrainingRequiredFinal);
	dml_print("DML::%s: USRRetrainingLatency = %f\n", __func__, p->mmSOCParameters.USRRetrainingLatency);
#endif

	s->TotalPixelBW = 0.0;
	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		s->TotalPixelBW = s->TotalPixelBW + p->DPPPerSurface[k]
					* (p->SwathWidthY[k] * p->BytePerPixelDETY[k] * p->VRatio[k] + p->SwathWidthC[k] * p->BytePerPixelDETC[k] * p->VRatioChroma[k]) / (p->HTotal[k] / p->PixelClock[k]);
	}

	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {

		s->LBLatencyHidingSourceLinesY[k] = (dml_uint_t)(dml_min((dml_float_t)p->MaxLineBufferLines, dml_floor((dml_float_t)p->LineBufferSize / (dml_float_t)p->LBBitPerPixel[k] / ((dml_float_t)p->SwathWidthY[k] / dml_max(p->HRatio[k], 1.0)), 1)) - (p->VTaps[k] - 1));
		s->LBLatencyHidingSourceLinesC[k] = (dml_uint_t)(dml_min((dml_float_t)p->MaxLineBufferLines, dml_floor((dml_float_t)p->LineBufferSize / (dml_float_t)p->LBBitPerPixel[k] / ((dml_float_t)p->SwathWidthC[k] / dml_max(p->HRatioChroma[k], 1.0)), 1)) - (p->VTapsChroma[k] - 1));


#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u, MaxLineBufferLines = %u\n", __func__, k, p->MaxLineBufferLines);
		dml_print("DML::%s: k=%u, LineBufferSize = %u\n", __func__, k, p->LineBufferSize);
		dml_print("DML::%s: k=%u, LBBitPerPixel = %u\n", __func__, k, p->LBBitPerPixel[k]);
		dml_print("DML::%s: k=%u, HRatio = %f\n", __func__, k, p->HRatio[k]);
		dml_print("DML::%s: k=%u, VTaps              = %u\n", __func__, k, p->VTaps[k]);
#endif

		s->EffectiveLBLatencyHidingY = s->LBLatencyHidingSourceLinesY[k] / p->VRatio[k] * (p->HTotal[k] / p->PixelClock[k]);
		s->EffectiveLBLatencyHidingC = s->LBLatencyHidingSourceLinesC[k] / p->VRatioChroma[k] * (p->HTotal[k] / p->PixelClock[k]);

		s->EffectiveDETBufferSizeY = p->DETBufferSizeY[k];
		if (p->UnboundedRequestEnabled) {
			s->EffectiveDETBufferSizeY = s->EffectiveDETBufferSizeY + p->CompressedBufferSizeInkByte * 1024 * (p->SwathWidthY[k] * p->BytePerPixelDETY[k] * p->VRatio[k]) / (p->HTotal[k] / p->PixelClock[k]) / s->TotalPixelBW;
		}

		s->LinesInDETY[k] = (dml_float_t)s->EffectiveDETBufferSizeY / p->BytePerPixelDETY[k] / p->SwathWidthY[k];
		s->LinesInDETYRoundedDownToSwath[k] = (dml_uint_t)(dml_floor(s->LinesInDETY[k], p->SwathHeightY[k]));
		s->FullDETBufferingTimeY = s->LinesInDETYRoundedDownToSwath[k] * (p->HTotal[k] / p->PixelClock[k]) / p->VRatio[k];

		s->ActiveClockChangeLatencyHidingY = s->EffectiveLBLatencyHidingY + s->FullDETBufferingTimeY - ((dml_float_t)p->DSTXAfterScaler[k] / (dml_float_t)p->HTotal[k] + (dml_float_t)p->DSTYAfterScaler[k]) * (dml_float_t)p->HTotal[k] / p->PixelClock[k];

		if (p->NumberOfActiveSurfaces > 1) {
			s->ActiveClockChangeLatencyHidingY = s->ActiveClockChangeLatencyHidingY - (1.0 - 1.0 / (dml_float_t)p->NumberOfActiveSurfaces) * (dml_float_t)p->SwathHeightY[k] * (dml_float_t)p->HTotal[k] / p->PixelClock[k] / p->VRatio[k];
	}

		if (p->BytePerPixelDETC[k] > 0) {
			s->LinesInDETC[k] = p->DETBufferSizeC[k] / p->BytePerPixelDETC[k] / p->SwathWidthC[k];
			s->LinesInDETCRoundedDownToSwath[k] = (dml_uint_t)(dml_floor(s->LinesInDETC[k], p->SwathHeightC[k]));
			s->FullDETBufferingTimeC = s->LinesInDETCRoundedDownToSwath[k] * (p->HTotal[k] / p->PixelClock[k]) / p->VRatioChroma[k];
			s->ActiveClockChangeLatencyHidingC = s->EffectiveLBLatencyHidingC + s->FullDETBufferingTimeC - ((dml_float_t)p->DSTXAfterScaler[k] / (dml_float_t)p->HTotal[k] + (dml_float_t)p->DSTYAfterScaler[k]) * (dml_float_t)p->HTotal[k] / p->PixelClock[k];
			if (p->NumberOfActiveSurfaces > 1) {
			s->ActiveClockChangeLatencyHidingC = s->ActiveClockChangeLatencyHidingC - (1.0 - 1.0 / (dml_float_t)p->NumberOfActiveSurfaces) * (dml_float_t)p->SwathHeightC[k] * (dml_float_t)p->HTotal[k] / p->PixelClock[k] / p->VRatioChroma[k];
			}
			s->ActiveClockChangeLatencyHiding = dml_min(s->ActiveClockChangeLatencyHidingY, s->ActiveClockChangeLatencyHidingC);
		} else {
			s->ActiveClockChangeLatencyHiding = s->ActiveClockChangeLatencyHidingY;
		}

		s->ActiveDRAMClockChangeLatencyMargin[k] = s->ActiveClockChangeLatencyHiding - p->Watermark->UrgentWatermark - p->Watermark->DRAMClockChangeWatermark;
		s->ActiveFCLKChangeLatencyMargin[k] = s->ActiveClockChangeLatencyHiding - p->Watermark->UrgentWatermark - p->Watermark->FCLKChangeWatermark;
		s->USRRetrainingLatencyMargin[k] = s->ActiveClockChangeLatencyHiding - p->Watermark->USRRetrainingWatermark;

		if (p->WritebackEnable[k]) {
			s->WritebackLatencyHiding = (dml_float_t)p->WritebackInterfaceBufferSize * 1024.0 / ((dml_float_t)p->WritebackDestinationWidth[k] * (dml_float_t)p->WritebackDestinationHeight[k] / ((dml_float_t)p->WritebackSourceHeight[k] * (dml_float_t)p->HTotal[k] / p->PixelClock[k]) * 4.0);
			if (p->WritebackPixelFormat[k] == dml_444_64) {
				s->WritebackLatencyHiding = s->WritebackLatencyHiding / 2;
			}
			s->WritebackDRAMClockChangeLatencyMargin = s->WritebackLatencyHiding - p->Watermark->WritebackDRAMClockChangeWatermark;

			s->WritebackFCLKChangeLatencyMargin = s->WritebackLatencyHiding - p->Watermark->WritebackFCLKChangeWatermark;

			s->ActiveDRAMClockChangeLatencyMargin[k] = dml_min(s->ActiveDRAMClockChangeLatencyMargin[k], s->WritebackFCLKChangeLatencyMargin);
			s->ActiveFCLKChangeLatencyMargin[k] = dml_min(s->ActiveFCLKChangeLatencyMargin[k], s->WritebackDRAMClockChangeLatencyMargin);
		}
		p->MaxActiveDRAMClockChangeLatencySupported[k] =  (p->UseMALLForPStateChange[k] == dml_use_mall_pstate_change_phantom_pipe) ? 0 : (s->ActiveDRAMClockChangeLatencyMargin[k] + p->mmSOCParameters.DRAMClockChangeLatency);
		p->ActiveDRAMClockChangeLatencyMargin[k] = s->ActiveDRAMClockChangeLatencyMargin[k];
	}

	*p->USRRetrainingSupport = true;
	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if ((p->UseMALLForPStateChange[k] != dml_use_mall_pstate_change_phantom_pipe) && (s->USRRetrainingLatencyMargin[k] < 0)) {
			*p->USRRetrainingSupport = false;
		}
	}

	s->FoundCriticalSurface = false;
	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if ((p->UseMALLForPStateChange[k] != dml_use_mall_pstate_change_phantom_pipe) && ((!s->FoundCriticalSurface)
			|| ((s->ActiveFCLKChangeLatencyMargin[k] + p->mmSOCParameters.FCLKChangeLatency) < *p->MaxActiveFCLKChangeLatencySupported))) {
			s->FoundCriticalSurface = true;
			*p->MaxActiveFCLKChangeLatencySupported = s->ActiveFCLKChangeLatencyMargin[k] + p->mmSOCParameters.FCLKChangeLatency;
		}
	}

	for (dml_uint_t i = 0; i < p->NumberOfActiveSurfaces; ++i) {
		for (dml_uint_t j = 0; j < p->NumberOfActiveSurfaces; ++j) {
			if (i == j ||
				(p->BlendingAndTiming[i] == i && p->BlendingAndTiming[j] == i) ||
				(p->BlendingAndTiming[j] == j && p->BlendingAndTiming[i] == j) ||
				(p->BlendingAndTiming[i] == p->BlendingAndTiming[j] && p->BlendingAndTiming[i] != i) ||
				(p->SynchronizeTimingsFinal && p->PixelClock[i] == p->PixelClock[j] && p->HTotal[i] == p->HTotal[j] && p->VTotal[i] == p->VTotal[j] && p->VActive[i] == p->VActive[j]) ||
				(p->SynchronizeDRRDisplaysForUCLKPStateChangeFinal && (p->DRRDisplay[i] || p->DRRDisplay[j]))) {
				s->SynchronizedSurfaces[i][j] = true;
			} else {
				s->SynchronizedSurfaces[i][j] = false;
			}
		}
	}

	s->FCLKChangeSupportNumber = 0;
	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if ((p->UseMALLForPStateChange[k] != dml_use_mall_pstate_change_phantom_pipe) &&  (s->ActiveFCLKChangeLatencyMargin[k] < 0)) {
			if (!(p->PrefetchMode[k] <= 1)) {
				s->FCLKChangeSupportNumber = 3;
			} else if (s->FCLKChangeSupportNumber == 0) {
				s->FCLKChangeSupportNumber = ((p->SynchronizeDRRDisplaysForUCLKPStateChangeFinal && p->DRRDisplay[k]) ? 2 : 1);
				s->LastSurfaceWithoutMargin = k;
			} else if (((s->FCLKChangeSupportNumber == 1) && (p->DRRDisplay[k] || (!s->SynchronizedSurfaces[s->LastSurfaceWithoutMargin][k]))) || (s->FCLKChangeSupportNumber == 2))
				s->FCLKChangeSupportNumber = 3;
		}
	}

	if (s->FCLKChangeSupportNumber == 0) {
		*p->FCLKChangeSupport = dml_fclock_change_vactive;
	} else if ((s->FCLKChangeSupportNumber == 1) || (s->FCLKChangeSupportNumber == 2)) {
		*p->FCLKChangeSupport = dml_fclock_change_vblank;
	} else {
		*p->FCLKChangeSupport = dml_fclock_change_unsupported;
	}

	s->DRAMClockChangeMethod = 0;
	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (p->UseMALLForPStateChange[k] == dml_use_mall_pstate_change_full_frame)
			s->DRAMClockChangeMethod = 1;
		else if (p->UseMALLForPStateChange[k] == dml_use_mall_pstate_change_sub_viewport)
			s->DRAMClockChangeMethod = 2;
	}

	s->DRAMClockChangeSupportNumber = 0;
	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (((s->DRAMClockChangeMethod == 0) && (s->ActiveDRAMClockChangeLatencyMargin[k] < 0)) ||
			((s->DRAMClockChangeMethod == 1) && (p->UseMALLForPStateChange[k] != dml_use_mall_pstate_change_full_frame)) ||
			((s->DRAMClockChangeMethod == 2) && (p->UseMALLForPStateChange[k] != dml_use_mall_pstate_change_sub_viewport) && (p->UseMALLForPStateChange[k] != dml_use_mall_pstate_change_phantom_pipe))) {
			if (p->PrefetchMode[k] != 0) { // Don't need to support DRAM clock change, PrefetchMode 0 means needs DRAM clock change support
				s->DRAMClockChangeSupportNumber = 3;
			} else if (s->DRAMClockChangeSupportNumber == 0) {
				s->DRAMClockChangeSupportNumber =  (p->SynchronizeDRRDisplaysForUCLKPStateChangeFinal && p->DRRDisplay[k]) ? 2 : 1;
				s->LastSurfaceWithoutMargin = k;
			} else if (((s->DRAMClockChangeSupportNumber == 1) && (p->DRRDisplay[k] || !s->SynchronizedSurfaces[s->LastSurfaceWithoutMargin][k])) || (s->DRAMClockChangeSupportNumber == 2)) {
				s->DRAMClockChangeSupportNumber = 3;
			}
		}
	}

	if (s->DRAMClockChangeMethod == 0) { // No MALL usage
		if (s->DRAMClockChangeSupportNumber == 0) {
			*p->DRAMClockChangeSupport = dml_dram_clock_change_vactive;
		} else if (s->DRAMClockChangeSupportNumber == 1) {
			*p->DRAMClockChangeSupport = dml_dram_clock_change_vblank;
		} else if (s->DRAMClockChangeSupportNumber == 2) {
			*p->DRAMClockChangeSupport = dml_dram_clock_change_vblank_drr;
		} else {
			*p->DRAMClockChangeSupport = dml_dram_clock_change_unsupported;
		}
	} else if (s->DRAMClockChangeMethod == 1) { // Any pipe using MALL full frame
		if (s->DRAMClockChangeSupportNumber == 0) {
			*p->DRAMClockChangeSupport = dml_dram_clock_change_vactive_w_mall_full_frame;
		} else if (s->DRAMClockChangeSupportNumber == 1) {
			*p->DRAMClockChangeSupport = dml_dram_clock_change_vblank_w_mall_full_frame;
		} else if (s->DRAMClockChangeSupportNumber == 2) {
			*p->DRAMClockChangeSupport = dml_dram_clock_change_vblank_drr_w_mall_full_frame;
		} else {
			*p->DRAMClockChangeSupport = dml_dram_clock_change_unsupported;
		}
	} else { // Any pipe using MALL subviewport
		if (s->DRAMClockChangeSupportNumber == 0) {
			*p->DRAMClockChangeSupport = dml_dram_clock_change_vactive_w_mall_sub_vp;
		} else if (s->DRAMClockChangeSupportNumber == 1) {
			*p->DRAMClockChangeSupport = dml_dram_clock_change_vblank_w_mall_sub_vp;
		} else if (s->DRAMClockChangeSupportNumber == 2) {
			*p->DRAMClockChangeSupport = dml_dram_clock_change_vblank_drr_w_mall_sub_vp;
		} else {
			*p->DRAMClockChangeSupport = dml_dram_clock_change_unsupported;
		}
	}

	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		s->dst_y_pstate = (dml_uint_t)(dml_ceil((p->mmSOCParameters.DRAMClockChangeLatency + p->mmSOCParameters.UrgentLatency) / (p->HTotal[k] / p->PixelClock[k]), 1));
		s->src_y_pstate_l = (dml_uint_t)(dml_ceil(s->dst_y_pstate * p->VRatio[k], p->SwathHeightY[k]));
		s->src_y_ahead_l = (dml_uint_t)(dml_floor(p->DETBufferSizeY[k] / p->BytePerPixelDETY[k] / p->SwathWidthY[k], p->SwathHeightY[k]) + s->LBLatencyHidingSourceLinesY[k]);
		s->sub_vp_lines_l = s->src_y_pstate_l + s->src_y_ahead_l + p->meta_row_height[k];

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u, DETBufferSizeY = %u\n", __func__, k, p->DETBufferSizeY[k]);
		dml_print("DML::%s: k=%u, BytePerPixelDETY = %f\n", __func__, k, p->BytePerPixelDETY[k]);
		dml_print("DML::%s: k=%u, SwathWidthY = %u\n", __func__, k, p->SwathWidthY[k]);
		dml_print("DML::%s: k=%u, SwathHeightY = %u\n", __func__, k, p->SwathHeightY[k]);
		dml_print("DML::%s: k=%u, LBLatencyHidingSourceLinesY = %u\n", __func__, k, s->LBLatencyHidingSourceLinesY[k]);
		dml_print("DML::%s: k=%u, dst_y_pstate = %u\n", __func__, k, s->dst_y_pstate);
		dml_print("DML::%s: k=%u, src_y_pstate_l = %u\n", __func__, k, s->src_y_pstate_l);
		dml_print("DML::%s: k=%u, src_y_ahead_l = %u\n", __func__, k, s->src_y_ahead_l);
		dml_print("DML::%s: k=%u, meta_row_height = %u\n", __func__, k, p->meta_row_height[k]);
		dml_print("DML::%s: k=%u, sub_vp_lines_l  = %u\n", __func__, k, s->sub_vp_lines_l);
#endif
		p->SubViewportLinesNeededInMALL[k] = s->sub_vp_lines_l;

		if (p->BytePerPixelDETC[k] > 0) {
		s->src_y_pstate_c = (dml_uint_t)(dml_ceil(s->dst_y_pstate * p->VRatioChroma[k], p->SwathHeightC[k]));
		s->src_y_ahead_c = (dml_uint_t)(dml_floor(p->DETBufferSizeC[k] / p->BytePerPixelDETC[k] / p->SwathWidthC[k], p->SwathHeightC[k]) + s->LBLatencyHidingSourceLinesC[k]);
		s->sub_vp_lines_c = s->src_y_pstate_c + s->src_y_ahead_c + p->meta_row_height_chroma[k];
		p->SubViewportLinesNeededInMALL[k] = (dml_uint_t)(dml_max(s->sub_vp_lines_l, s->sub_vp_lines_c));

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u, src_y_pstate_c = %u\n", __func__, k, s->src_y_pstate_c);
		dml_print("DML::%s: k=%u, src_y_ahead_c = %u\n", __func__, k, s->src_y_ahead_c);
		dml_print("DML::%s: k=%u, meta_row_height_chroma = %u\n", __func__, k, p->meta_row_height_chroma[k]);
		dml_print("DML::%s: k=%u, sub_vp_lines_c            = %u\n", __func__, k, s->sub_vp_lines_c);
#endif
		}
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DRAMClockChangeSupport = %u\n", __func__, *p->DRAMClockChangeSupport);
	dml_print("DML::%s: FCLKChangeSupport = %u\n", __func__, *p->FCLKChangeSupport);
	dml_print("DML::%s: MaxActiveFCLKChangeLatencySupported = %f\n", __func__, *p->MaxActiveFCLKChangeLatencySupported);
	dml_print("DML::%s: USRRetrainingSupport                        = %u\n", __func__, *p->USRRetrainingSupport);
#endif
} // CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport

static void CalculateDCFCLKDeepSleep(
		dml_uint_t NumberOfActiveSurfaces,
		dml_uint_t BytePerPixelY[],
		dml_uint_t BytePerPixelC[],
		dml_float_t VRatio[],
		dml_float_t VRatioChroma[],
		dml_uint_t SwathWidthY[],
		dml_uint_t SwathWidthC[],
		dml_uint_t DPPPerSurface[],
		dml_float_t HRatio[],
		dml_float_t HRatioChroma[],
		dml_float_t PixelClock[],
		dml_float_t PSCL_THROUGHPUT[],
		dml_float_t PSCL_THROUGHPUT_CHROMA[],
		dml_float_t Dppclk[],
		dml_float_t ReadBandwidthLuma[],
		dml_float_t ReadBandwidthChroma[],
		dml_uint_t ReturnBusWidth,

		// Output
		dml_float_t *DCFClkDeepSleep)
{
	dml_float_t DisplayPipeLineDeliveryTimeLuma;
	dml_float_t DisplayPipeLineDeliveryTimeChroma;
	dml_float_t DCFClkDeepSleepPerSurface[__DML_NUM_PLANES__];
	dml_float_t ReadBandwidth = 0.0;

	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {

		if (VRatio[k] <= 1) {
			DisplayPipeLineDeliveryTimeLuma = SwathWidthY[k] * DPPPerSurface[k] / HRatio[k] / PixelClock[k];
		} else {
			DisplayPipeLineDeliveryTimeLuma = SwathWidthY[k] / PSCL_THROUGHPUT[k] / Dppclk[k];
		}
		if (BytePerPixelC[k] == 0) {
			DisplayPipeLineDeliveryTimeChroma = 0;
		} else {
			if (VRatioChroma[k] <= 1) {
				DisplayPipeLineDeliveryTimeChroma = SwathWidthC[k] * DPPPerSurface[k] / HRatioChroma[k] / PixelClock[k];
			} else {
				DisplayPipeLineDeliveryTimeChroma = SwathWidthC[k] / PSCL_THROUGHPUT_CHROMA[k] / Dppclk[k];
			}
		}

		if (BytePerPixelC[k] > 0) {
			DCFClkDeepSleepPerSurface[k] = dml_max(__DML_MIN_DCFCLK_FACTOR__ * SwathWidthY[k] * BytePerPixelY[k] / 32.0 / DisplayPipeLineDeliveryTimeLuma,
														__DML_MIN_DCFCLK_FACTOR__ * SwathWidthC[k] * BytePerPixelC[k] / 32.0 / DisplayPipeLineDeliveryTimeChroma);
		} else {
			DCFClkDeepSleepPerSurface[k] = __DML_MIN_DCFCLK_FACTOR__ * SwathWidthY[k] * BytePerPixelY[k] / 64.0 / DisplayPipeLineDeliveryTimeLuma;
		}
		DCFClkDeepSleepPerSurface[k] = dml_max(DCFClkDeepSleepPerSurface[k], PixelClock[k] / 16);

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u, PixelClock = %f\n", __func__, k, PixelClock[k]);
		dml_print("DML::%s: k=%u, DCFClkDeepSleepPerSurface = %f\n", __func__, k, DCFClkDeepSleepPerSurface[k]);
#endif
	}

	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		ReadBandwidth = ReadBandwidth + ReadBandwidthLuma[k] + ReadBandwidthChroma[k];
	}

	*DCFClkDeepSleep = dml_max(8.0, __DML_MIN_DCFCLK_FACTOR__ * ReadBandwidth / (dml_float_t) ReturnBusWidth);

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: __DML_MIN_DCFCLK_FACTOR__ = %f\n", __func__, __DML_MIN_DCFCLK_FACTOR__);
		dml_print("DML::%s: ReadBandwidth = %f\n", __func__, ReadBandwidth);
		dml_print("DML::%s: ReturnBusWidth = %u\n", __func__, ReturnBusWidth);
		dml_print("DML::%s: DCFClkDeepSleep = %f\n", __func__, *DCFClkDeepSleep);
#endif

	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		*DCFClkDeepSleep = dml_max(*DCFClkDeepSleep, DCFClkDeepSleepPerSurface[k]);
	}
	dml_print("DML::%s: DCFClkDeepSleep = %f (final)\n", __func__, *DCFClkDeepSleep);
} // CalculateDCFCLKDeepSleep

static void CalculateUrgentBurstFactor(
		enum dml_use_mall_for_pstate_change_mode UseMALLForPStateChange,
		dml_uint_t swath_width_luma_ub,
		dml_uint_t swath_width_chroma_ub,
		dml_uint_t SwathHeightY,
		dml_uint_t SwathHeightC,
		dml_float_t LineTime,
		dml_float_t UrgentLatency,
		dml_float_t CursorBufferSize,
		dml_uint_t CursorWidth,
		dml_uint_t CursorBPP,
		dml_float_t VRatio,
		dml_float_t VRatioC,
		dml_float_t BytePerPixelInDETY,
		dml_float_t BytePerPixelInDETC,
		dml_uint_t DETBufferSizeY,
		dml_uint_t DETBufferSizeC,
		// Output
		dml_float_t *UrgentBurstFactorCursor,
		dml_float_t *UrgentBurstFactorLuma,
		dml_float_t *UrgentBurstFactorChroma,
		dml_bool_t *NotEnoughUrgentLatencyHiding)
{
	dml_float_t LinesInDETLuma;
	dml_float_t LinesInDETChroma;
	dml_uint_t LinesInCursorBuffer;
	dml_float_t CursorBufferSizeInTime;
	dml_float_t DETBufferSizeInTimeLuma;
	dml_float_t DETBufferSizeInTimeChroma;

	*NotEnoughUrgentLatencyHiding = 0;

	if (CursorWidth > 0) {
		LinesInCursorBuffer = 1 << (dml_uint_t) dml_floor(dml_log2(CursorBufferSize * 1024.0 / (CursorWidth * CursorBPP / 8.0)), 1.0);
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

	LinesInDETLuma = (UseMALLForPStateChange == dml_use_mall_pstate_change_phantom_pipe ? 1024*1024 : DETBufferSizeY) / BytePerPixelInDETY / swath_width_luma_ub;

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
		LinesInDETChroma = (UseMALLForPStateChange == dml_use_mall_pstate_change_phantom_pipe ? 1024*1024 : DETBufferSizeC) / BytePerPixelInDETC / swath_width_chroma_ub;

		if (VRatioC > 0) {
			DETBufferSizeInTimeChroma = dml_floor(LinesInDETChroma, SwathHeightC) * LineTime / VRatioC;
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
} // CalculateUrgentBurstFactor

static void CalculatePixelDeliveryTimes(
		dml_uint_t NumberOfActiveSurfaces,
		dml_float_t VRatio[],
		dml_float_t VRatioChroma[],
		dml_float_t VRatioPrefetchY[],
		dml_float_t VRatioPrefetchC[],
		dml_uint_t swath_width_luma_ub[],
		dml_uint_t swath_width_chroma_ub[],
		dml_uint_t DPPPerSurface[],
		dml_float_t HRatio[],
		dml_float_t HRatioChroma[],
		dml_float_t PixelClock[],
		dml_float_t PSCL_THROUGHPUT[],
		dml_float_t PSCL_THROUGHPUT_CHROMA[],
		dml_float_t Dppclk[],
		dml_uint_t BytePerPixelC[],
		enum dml_rotation_angle SourceScan[],
		dml_uint_t NumberOfCursors[],
		dml_uint_t CursorWidth[],
		dml_uint_t CursorBPP[],
		dml_uint_t BlockWidth256BytesY[],
		dml_uint_t BlockHeight256BytesY[],
		dml_uint_t BlockWidth256BytesC[],
		dml_uint_t BlockHeight256BytesC[],

		// Output
		dml_float_t DisplayPipeLineDeliveryTimeLuma[],
		dml_float_t DisplayPipeLineDeliveryTimeChroma[],
		dml_float_t DisplayPipeLineDeliveryTimeLumaPrefetch[],
		dml_float_t DisplayPipeLineDeliveryTimeChromaPrefetch[],
		dml_float_t DisplayPipeRequestDeliveryTimeLuma[],
		dml_float_t DisplayPipeRequestDeliveryTimeChroma[],
		dml_float_t DisplayPipeRequestDeliveryTimeLumaPrefetch[],
		dml_float_t DisplayPipeRequestDeliveryTimeChromaPrefetch[],
		dml_float_t CursorRequestDeliveryTime[],
		dml_float_t CursorRequestDeliveryTimePrefetch[])
{
		dml_float_t req_per_swath_ub;

		for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u : HRatio = %f\n", __func__, k, HRatio[k]);
		dml_print("DML::%s: k=%u : VRatio = %f\n", __func__, k, VRatio[k]);
		dml_print("DML::%s: k=%u : HRatioChroma = %f\n", __func__, k, HRatioChroma[k]);
		dml_print("DML::%s: k=%u : VRatioChroma = %f\n", __func__, k, VRatioChroma[k]);
		dml_print("DML::%s: k=%u : swath_width_luma_ub = %u\n", __func__, k, swath_width_luma_ub[k]);
		dml_print("DML::%s: k=%u : swath_width_chroma_ub = %u\n", __func__, k, swath_width_chroma_ub[k]);
		dml_print("DML::%s: k=%u : PSCL_THROUGHPUT = %f\n", __func__, k, PSCL_THROUGHPUT[k]);
		dml_print("DML::%s: k=%u : PSCL_THROUGHPUT_CHROMA = %f\n", __func__, k, PSCL_THROUGHPUT_CHROMA[k]);
		dml_print("DML::%s: k=%u : DPPPerSurface = %u\n", __func__, k, DPPPerSurface[k]);
		dml_print("DML::%s: k=%u : PixelClock = %f\n", __func__, k, PixelClock[k]);
		dml_print("DML::%s: k=%u : Dppclk = %f\n", __func__, k, Dppclk[k]);
#endif

		if (VRatio[k] <= 1) {
			DisplayPipeLineDeliveryTimeLuma[k] = swath_width_luma_ub[k] * DPPPerSurface[k] / HRatio[k] / PixelClock[k];
		} else {
			DisplayPipeLineDeliveryTimeLuma[k] = swath_width_luma_ub[k] / PSCL_THROUGHPUT[k] / Dppclk[k];
		}

		if (BytePerPixelC[k] == 0) {
			DisplayPipeLineDeliveryTimeChroma[k] = 0;
		} else {
			if (VRatioChroma[k] <= 1) {
				DisplayPipeLineDeliveryTimeChroma[k] = swath_width_chroma_ub[k] * DPPPerSurface[k] / HRatioChroma[k] / PixelClock[k];
			} else {
				DisplayPipeLineDeliveryTimeChroma[k] = swath_width_chroma_ub[k] / PSCL_THROUGHPUT_CHROMA[k] / Dppclk[k];
			}
		}

		if (VRatioPrefetchY[k] <= 1) {
			DisplayPipeLineDeliveryTimeLumaPrefetch[k] = swath_width_luma_ub[k] * DPPPerSurface[k] / HRatio[k] / PixelClock[k];
		} else {
			DisplayPipeLineDeliveryTimeLumaPrefetch[k] = swath_width_luma_ub[k] / PSCL_THROUGHPUT[k] / Dppclk[k];
		}

		if (BytePerPixelC[k] == 0) {
			DisplayPipeLineDeliveryTimeChromaPrefetch[k] = 0;
		} else {
			if (VRatioPrefetchC[k] <= 1) {
				DisplayPipeLineDeliveryTimeChromaPrefetch[k] = swath_width_chroma_ub[k] * DPPPerSurface[k] / HRatioChroma[k] / PixelClock[k];
			} else {
				DisplayPipeLineDeliveryTimeChromaPrefetch[k] = swath_width_chroma_ub[k] / PSCL_THROUGHPUT_CHROMA[k] / Dppclk[k];
			}
		}
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u : DisplayPipeLineDeliveryTimeLuma = %f\n", __func__, k, DisplayPipeLineDeliveryTimeLuma[k]);
		dml_print("DML::%s: k=%u : DisplayPipeLineDeliveryTimeLumaPrefetch = %f\n", __func__, k, DisplayPipeLineDeliveryTimeLumaPrefetch[k]);
		dml_print("DML::%s: k=%u : DisplayPipeLineDeliveryTimeChroma = %f\n", __func__, k, DisplayPipeLineDeliveryTimeChroma[k]);
		dml_print("DML::%s: k=%u : DisplayPipeLineDeliveryTimeChromaPrefetch = %f\n", __func__, k, DisplayPipeLineDeliveryTimeChromaPrefetch[k]);
#endif
	}

	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (!dml_is_vertical_rotation(SourceScan[k])) {
			req_per_swath_ub = swath_width_luma_ub[k] / BlockWidth256BytesY[k];
		} else {
			req_per_swath_ub = swath_width_luma_ub[k] / BlockHeight256BytesY[k];
		}
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u : req_per_swath_ub = %f (Luma)\n", __func__, k, req_per_swath_ub);
#endif

		DisplayPipeRequestDeliveryTimeLuma[k] = DisplayPipeLineDeliveryTimeLuma[k] / req_per_swath_ub;
		DisplayPipeRequestDeliveryTimeLumaPrefetch[k] = DisplayPipeLineDeliveryTimeLumaPrefetch[k] / req_per_swath_ub;
		if (BytePerPixelC[k] == 0) {
			DisplayPipeRequestDeliveryTimeChroma[k] = 0;
			DisplayPipeRequestDeliveryTimeChromaPrefetch[k] = 0;
		} else {
			if (!dml_is_vertical_rotation(SourceScan[k])) {
				req_per_swath_ub = swath_width_chroma_ub[k] / BlockWidth256BytesC[k];
			} else {
				req_per_swath_ub = swath_width_chroma_ub[k] / BlockHeight256BytesC[k];
			}
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%u : req_per_swath_ub = %f (Chroma)\n", __func__, k, req_per_swath_ub);
#endif
			DisplayPipeRequestDeliveryTimeChroma[k] = DisplayPipeLineDeliveryTimeChroma[k] / req_per_swath_ub;
			DisplayPipeRequestDeliveryTimeChromaPrefetch[k] = DisplayPipeLineDeliveryTimeChromaPrefetch[k] / req_per_swath_ub;
		}
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u : DisplayPipeRequestDeliveryTimeLuma = %f\n", __func__, k, DisplayPipeRequestDeliveryTimeLuma[k]);
		dml_print("DML::%s: k=%u : DisplayPipeRequestDeliveryTimeLumaPrefetch = %f\n", __func__, k, DisplayPipeRequestDeliveryTimeLumaPrefetch[k]);
		dml_print("DML::%s: k=%u : DisplayPipeRequestDeliveryTimeChroma = %f\n", __func__, k, DisplayPipeRequestDeliveryTimeChroma[k]);
		dml_print("DML::%s: k=%u : DisplayPipeRequestDeliveryTimeChromaPrefetch = %f\n", __func__, k, DisplayPipeRequestDeliveryTimeChromaPrefetch[k]);
#endif
	}

	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		dml_uint_t cursor_req_per_width;
		cursor_req_per_width = (dml_uint_t)(dml_ceil((dml_float_t) CursorWidth[k] * (dml_float_t) CursorBPP[k] / 256.0 / 8.0, 1.0));
		if (NumberOfCursors[k] > 0) {
			if (VRatio[k] <= 1) {
				CursorRequestDeliveryTime[k] = (dml_float_t) CursorWidth[k] / HRatio[k] / PixelClock[k] / cursor_req_per_width;
			} else {
				CursorRequestDeliveryTime[k] = (dml_float_t) CursorWidth[k] / PSCL_THROUGHPUT[k] / Dppclk[k] / cursor_req_per_width;
			}
			if (VRatioPrefetchY[k] <= 1) {
				CursorRequestDeliveryTimePrefetch[k] = (dml_float_t) CursorWidth[k] / HRatio[k] / PixelClock[k] / cursor_req_per_width;
		} else {
			CursorRequestDeliveryTimePrefetch[k] = (dml_float_t) CursorWidth[k] / PSCL_THROUGHPUT[k] / Dppclk[k] / cursor_req_per_width;
			}
		} else {
			CursorRequestDeliveryTime[k] = 0;
			CursorRequestDeliveryTimePrefetch[k] = 0;
		}
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u : NumberOfCursors = %u\n", __func__, k, NumberOfCursors[k]);
		dml_print("DML::%s: k=%u : CursorRequestDeliveryTime = %f\n", __func__, k, CursorRequestDeliveryTime[k]);
		dml_print("DML::%s: k=%u : CursorRequestDeliveryTimePrefetch = %f\n", __func__, k, CursorRequestDeliveryTimePrefetch[k]);
#endif
	}
} // CalculatePixelDeliveryTimes

static void CalculateMetaAndPTETimes(
		dml_bool_t use_one_row_for_frame[],
		dml_uint_t NumberOfActiveSurfaces,
		dml_bool_t GPUVMEnable,
		dml_uint_t MetaChunkSize,
		dml_uint_t MinMetaChunkSizeBytes,
		dml_uint_t HTotal[],
		dml_float_t VRatio[],
		dml_float_t VRatioChroma[],
		dml_float_t DestinationLinesToRequestRowInVBlank[],
		dml_float_t DestinationLinesToRequestRowInImmediateFlip[],
		dml_bool_t DCCEnable[],
		dml_float_t PixelClock[],
		dml_uint_t BytePerPixelY[],
		dml_uint_t BytePerPixelC[],
		enum dml_rotation_angle SourceScan[],
		dml_uint_t dpte_row_height[],
		dml_uint_t dpte_row_height_chroma[],
		dml_uint_t meta_row_width[],
		dml_uint_t meta_row_width_chroma[],
		dml_uint_t meta_row_height[],
		dml_uint_t meta_row_height_chroma[],
		dml_uint_t meta_req_width[],
		dml_uint_t meta_req_width_chroma[],
		dml_uint_t meta_req_height[],
		dml_uint_t meta_req_height_chroma[],
		dml_uint_t dpte_group_bytes[],
		dml_uint_t PTERequestSizeY[],
		dml_uint_t PTERequestSizeC[],
		dml_uint_t PixelPTEReqWidthY[],
		dml_uint_t PixelPTEReqHeightY[],
		dml_uint_t PixelPTEReqWidthC[],
		dml_uint_t PixelPTEReqHeightC[],
		dml_uint_t dpte_row_width_luma_ub[],
		dml_uint_t dpte_row_width_chroma_ub[],

		// Output
		dml_float_t DST_Y_PER_PTE_ROW_NOM_L[],
		dml_float_t DST_Y_PER_PTE_ROW_NOM_C[],
		dml_float_t DST_Y_PER_META_ROW_NOM_L[],
		dml_float_t DST_Y_PER_META_ROW_NOM_C[],
		dml_float_t TimePerMetaChunkNominal[],
		dml_float_t TimePerChromaMetaChunkNominal[],
		dml_float_t TimePerMetaChunkVBlank[],
		dml_float_t TimePerChromaMetaChunkVBlank[],
		dml_float_t TimePerMetaChunkFlip[],
		dml_float_t TimePerChromaMetaChunkFlip[],
		dml_float_t time_per_pte_group_nom_luma[],
		dml_float_t time_per_pte_group_vblank_luma[],
		dml_float_t time_per_pte_group_flip_luma[],
		dml_float_t time_per_pte_group_nom_chroma[],
		dml_float_t time_per_pte_group_vblank_chroma[],
		dml_float_t time_per_pte_group_flip_chroma[])
{
	dml_uint_t meta_chunk_width;
	dml_uint_t min_meta_chunk_width;
	dml_uint_t meta_chunk_per_row_int;
	dml_uint_t meta_row_remainder;
	dml_uint_t meta_chunk_threshold;
	dml_uint_t meta_chunks_per_row_ub;
	dml_uint_t meta_chunk_width_chroma;
	dml_uint_t min_meta_chunk_width_chroma;
	dml_uint_t meta_chunk_per_row_int_chroma;
	dml_uint_t meta_row_remainder_chroma;
	dml_uint_t meta_chunk_threshold_chroma;
	dml_uint_t meta_chunks_per_row_ub_chroma;
	dml_uint_t dpte_group_width_luma;
	dml_uint_t dpte_groups_per_row_luma_ub;
	dml_uint_t dpte_group_width_chroma;
	dml_uint_t dpte_groups_per_row_chroma_ub;

	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
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

	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (DCCEnable[k] == true) {
			meta_chunk_width = MetaChunkSize * 1024 * 256 / BytePerPixelY[k] / meta_row_height[k];
			min_meta_chunk_width = MinMetaChunkSizeBytes * 256 / BytePerPixelY[k] / meta_row_height[k];
			meta_chunk_per_row_int = meta_row_width[k] / meta_chunk_width;
			meta_row_remainder = meta_row_width[k] % meta_chunk_width;
			if (!dml_is_vertical_rotation(SourceScan[k])) {
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
				meta_chunk_per_row_int_chroma = (dml_uint_t)((dml_float_t) meta_row_width_chroma[k] / meta_chunk_width_chroma);
				meta_row_remainder_chroma = meta_row_width_chroma[k] % meta_chunk_width_chroma;
				if (!dml_is_vertical_rotation(SourceScan[k])) {
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

	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (GPUVMEnable == true) {
			if (!dml_is_vertical_rotation(SourceScan[k])) {
				dpte_group_width_luma = (dml_uint_t)((dml_float_t) dpte_group_bytes[k] / (dml_float_t) PTERequestSizeY[k] * PixelPTEReqWidthY[k]);
			} else {
				dpte_group_width_luma = (dml_uint_t)((dml_float_t) dpte_group_bytes[k] / (dml_float_t) PTERequestSizeY[k] * PixelPTEReqHeightY[k]);
			}

			if (use_one_row_for_frame[k]) {
				dpte_groups_per_row_luma_ub = (dml_uint_t)(dml_ceil((dml_float_t) dpte_row_width_luma_ub[k] / (dml_float_t) dpte_group_width_luma / 2.0, 1.0));
			} else {
				dpte_groups_per_row_luma_ub = (dml_uint_t)(dml_ceil((dml_float_t) dpte_row_width_luma_ub[k] / (dml_float_t) dpte_group_width_luma, 1.0));
			}

			dml_print("DML::%s: k=%u, use_one_row_for_frame = %u\n", __func__, k, use_one_row_for_frame[k]);
			dml_print("DML::%s: k=%u, dpte_group_bytes = %u\n", __func__, k, dpte_group_bytes[k]);
			dml_print("DML::%s: k=%u, PTERequestSizeY = %u\n", __func__, k, PTERequestSizeY[k]);
			dml_print("DML::%s: k=%u, PixelPTEReqWidthY = %u\n", __func__, k, PixelPTEReqWidthY[k]);
			dml_print("DML::%s: k=%u, PixelPTEReqHeightY = %u\n", __func__, k, PixelPTEReqHeightY[k]);
			dml_print("DML::%s: k=%u, dpte_row_width_luma_ub = %u\n", __func__, k, dpte_row_width_luma_ub[k]);
			dml_print("DML::%s: k=%u, dpte_group_width_luma = %u\n", __func__, k, dpte_group_width_luma);
			dml_print("DML::%s: k=%u, dpte_groups_per_row_luma_ub = %u\n", __func__, k, dpte_groups_per_row_luma_ub);

			time_per_pte_group_nom_luma[k] = DST_Y_PER_PTE_ROW_NOM_L[k] * HTotal[k] / PixelClock[k] / dpte_groups_per_row_luma_ub;
			time_per_pte_group_vblank_luma[k] = DestinationLinesToRequestRowInVBlank[k] * HTotal[k] / PixelClock[k] / dpte_groups_per_row_luma_ub;
			time_per_pte_group_flip_luma[k] = DestinationLinesToRequestRowInImmediateFlip[k] * HTotal[k] / PixelClock[k] / dpte_groups_per_row_luma_ub;
			if (BytePerPixelC[k] == 0) {
				time_per_pte_group_nom_chroma[k] = 0;
				time_per_pte_group_vblank_chroma[k] = 0;
				time_per_pte_group_flip_chroma[k] = 0;
			} else {
				if (!dml_is_vertical_rotation(SourceScan[k])) {
					dpte_group_width_chroma = (dml_uint_t)((dml_float_t) dpte_group_bytes[k] / (dml_float_t) PTERequestSizeC[k] * PixelPTEReqWidthC[k]);
				} else {
					dpte_group_width_chroma = (dml_uint_t)((dml_float_t) dpte_group_bytes[k] / (dml_float_t) PTERequestSizeC[k] * PixelPTEReqHeightC[k]);
				}

				if (use_one_row_for_frame[k]) {
					dpte_groups_per_row_chroma_ub = (dml_uint_t)(dml_ceil((dml_float_t) dpte_row_width_chroma_ub[k] / (dml_float_t) dpte_group_width_chroma / 2.0, 1.0));
				} else {
					dpte_groups_per_row_chroma_ub = (dml_uint_t)(dml_ceil((dml_float_t) dpte_row_width_chroma_ub[k] / (dml_float_t) dpte_group_width_chroma, 1.0));
				}
				dml_print("DML::%s: k=%u, dpte_row_width_chroma_ub = %u\n", __func__, k, dpte_row_width_chroma_ub[k]);
				dml_print("DML::%s: k=%u, dpte_group_width_chroma = %u\n", __func__, k, dpte_group_width_chroma);
				dml_print("DML::%s: k=%u, dpte_groups_per_row_chroma_ub = %u\n", __func__, k, dpte_groups_per_row_chroma_ub);

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
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u, DestinationLinesToRequestRowInVBlank = %f\n", __func__, k, DestinationLinesToRequestRowInVBlank[k]);
		dml_print("DML::%s: k=%u, DestinationLinesToRequestRowInImmediateFlip = %f\n", __func__, k, DestinationLinesToRequestRowInImmediateFlip[k]);

		dml_print("DML::%s: k=%u, DST_Y_PER_PTE_ROW_NOM_L = %f\n", __func__, k, DST_Y_PER_PTE_ROW_NOM_L[k]);
		dml_print("DML::%s: k=%u, DST_Y_PER_PTE_ROW_NOM_C = %f\n", __func__, k, DST_Y_PER_PTE_ROW_NOM_C[k]);
		dml_print("DML::%s: k=%u, DST_Y_PER_META_ROW_NOM_L = %f\n", __func__, k, DST_Y_PER_META_ROW_NOM_L[k]);
		dml_print("DML::%s: k=%u, DST_Y_PER_META_ROW_NOM_C = %f\n", __func__, k, DST_Y_PER_META_ROW_NOM_C[k]);
		dml_print("DML::%s: k=%u, TimePerMetaChunkNominal = %f\n", __func__, k, TimePerMetaChunkNominal[k]);
		dml_print("DML::%s: k=%u, TimePerMetaChunkVBlank = %f\n", __func__, k, TimePerMetaChunkVBlank[k]);
		dml_print("DML::%s: k=%u, TimePerMetaChunkFlip = %f\n", __func__, k, TimePerMetaChunkFlip[k]);
		dml_print("DML::%s: k=%u, TimePerChromaMetaChunkNominal = %f\n", __func__, k, TimePerChromaMetaChunkNominal[k]);
		dml_print("DML::%s: k=%u, TimePerChromaMetaChunkVBlank = %f\n", __func__, k, TimePerChromaMetaChunkVBlank[k]);
		dml_print("DML::%s: k=%u, TimePerChromaMetaChunkFlip = %f\n", __func__, k, TimePerChromaMetaChunkFlip[k]);
		dml_print("DML::%s: k=%u, time_per_pte_group_nom_luma = %f\n", __func__, k, time_per_pte_group_nom_luma[k]);
		dml_print("DML::%s: k=%u, time_per_pte_group_vblank_luma = %f\n", __func__, k, time_per_pte_group_vblank_luma[k]);
		dml_print("DML::%s: k=%u, time_per_pte_group_flip_luma = %f\n", __func__, k, time_per_pte_group_flip_luma[k]);
		dml_print("DML::%s: k=%u, time_per_pte_group_nom_chroma = %f\n", __func__, k, time_per_pte_group_nom_chroma[k]);
		dml_print("DML::%s: k=%u, time_per_pte_group_vblank_chroma = %f\n", __func__, k, time_per_pte_group_vblank_chroma[k]);
		dml_print("DML::%s: k=%u, time_per_pte_group_flip_chroma   = %f\n", __func__, k, time_per_pte_group_flip_chroma[k]);
#endif
	}
} // CalculateMetaAndPTETimes

static void CalculateVMGroupAndRequestTimes(
		dml_uint_t NumberOfActiveSurfaces,
		dml_bool_t GPUVMEnable,
		dml_uint_t GPUVMMaxPageTableLevels,
		dml_uint_t HTotal[],
		dml_uint_t BytePerPixelC[],
		dml_float_t DestinationLinesToRequestVMInVBlank[],
		dml_float_t DestinationLinesToRequestVMInImmediateFlip[],
		dml_bool_t DCCEnable[],
		dml_float_t PixelClock[],
		dml_uint_t dpte_row_width_luma_ub[],
		dml_uint_t dpte_row_width_chroma_ub[],
		dml_uint_t vm_group_bytes[],
		dml_uint_t dpde0_bytes_per_frame_ub_l[],
		dml_uint_t dpde0_bytes_per_frame_ub_c[],
		dml_uint_t meta_pte_bytes_per_frame_ub_l[],
		dml_uint_t meta_pte_bytes_per_frame_ub_c[],

		// Output
		dml_float_t TimePerVMGroupVBlank[],
		dml_float_t TimePerVMGroupFlip[],
		dml_float_t TimePerVMRequestVBlank[],
		dml_float_t TimePerVMRequestFlip[])
{
	dml_uint_t num_group_per_lower_vm_stage;
	dml_uint_t num_req_per_lower_vm_stage;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: NumberOfActiveSurfaces = %u\n", __func__, NumberOfActiveSurfaces);
	dml_print("DML::%s: GPUVMEnable = %u\n", __func__, GPUVMEnable);
#endif
	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u, DCCEnable = %u\n", __func__, k, DCCEnable[k]);
		dml_print("DML::%s: k=%u, vm_group_bytes = %u\n", __func__, k, vm_group_bytes[k]);
		dml_print("DML::%s: k=%u, dpde0_bytes_per_frame_ub_l = %u\n", __func__, k, dpde0_bytes_per_frame_ub_l[k]);
		dml_print("DML::%s: k=%u, dpde0_bytes_per_frame_ub_c = %u\n", __func__, k, dpde0_bytes_per_frame_ub_c[k]);
		dml_print("DML::%s: k=%u, meta_pte_bytes_per_frame_ub_l = %u\n", __func__, k, meta_pte_bytes_per_frame_ub_l[k]);
		dml_print("DML::%s: k=%u, meta_pte_bytes_per_frame_ub_c = %u\n", __func__, k, meta_pte_bytes_per_frame_ub_c[k]);
#endif

		if (GPUVMEnable == true && (DCCEnable[k] == true || GPUVMMaxPageTableLevels > 1)) {
			if (DCCEnable[k] == false) {
				if (BytePerPixelC[k] > 0) {
					num_group_per_lower_vm_stage = (dml_uint_t) (dml_ceil((dml_float_t) dpde0_bytes_per_frame_ub_l[k] / (dml_float_t) vm_group_bytes[k], 1.0) +
													dml_ceil((dml_float_t) dpde0_bytes_per_frame_ub_c[k] / (dml_float_t) vm_group_bytes[k], 1.0));
				} else {
					num_group_per_lower_vm_stage = (dml_uint_t) (dml_ceil((dml_float_t) dpde0_bytes_per_frame_ub_l[k] / (dml_float_t) vm_group_bytes[k], 1.0));
				}
			} else {
				if (GPUVMMaxPageTableLevels == 1) {
					if (BytePerPixelC[k] > 0) {
						num_group_per_lower_vm_stage = (dml_uint_t)(dml_ceil((dml_float_t) (meta_pte_bytes_per_frame_ub_l[k]) / (dml_float_t) (vm_group_bytes[k]), 1.0) +
																	dml_ceil((dml_float_t) (meta_pte_bytes_per_frame_ub_c[k]) / (dml_float_t) (vm_group_bytes[k]), 1.0));
					} else {
						num_group_per_lower_vm_stage = (dml_uint_t)(dml_ceil((dml_float_t) (meta_pte_bytes_per_frame_ub_l[k]) / (dml_float_t) (vm_group_bytes[k]), 1.0));
					}
				} else {
					if (BytePerPixelC[k] > 0) {
						num_group_per_lower_vm_stage = (dml_uint_t)(2.0 + dml_ceil((dml_float_t) (dpde0_bytes_per_frame_ub_l[k]) / (dml_float_t) (vm_group_bytes[k]), 1) +
														dml_ceil((dml_float_t) (dpde0_bytes_per_frame_ub_c[k]) / (dml_float_t) (vm_group_bytes[k]), 1) +
														dml_ceil((dml_float_t) (meta_pte_bytes_per_frame_ub_l[k]) / (dml_float_t) (vm_group_bytes[k]), 1) +
														dml_ceil((dml_float_t) (meta_pte_bytes_per_frame_ub_c[k]) / (dml_float_t) (vm_group_bytes[k]), 1));
					} else {
						num_group_per_lower_vm_stage = (dml_uint_t)(1.0 + dml_ceil((dml_float_t) (dpde0_bytes_per_frame_ub_l[k]) / (dml_float_t) (vm_group_bytes[k]), 1) +
																	dml_ceil((dml_float_t) (meta_pte_bytes_per_frame_ub_l[k]) / (dml_float_t) (vm_group_bytes[k]), 1));
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
						num_req_per_lower_vm_stage = dpde0_bytes_per_frame_ub_l[k] / 64 + dpde0_bytes_per_frame_ub_c[k] / 64 + meta_pte_bytes_per_frame_ub_l[k] / 64 + meta_pte_bytes_per_frame_ub_c[k] / 64;
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
				TimePerVMRequestFlip[k]    = TimePerVMRequestFlip[k] / 2;
			}

		} else {
			TimePerVMGroupVBlank[k] = 0;
			TimePerVMGroupFlip[k] = 0;
			TimePerVMRequestVBlank[k] = 0;
			TimePerVMRequestFlip[k] = 0;
		}

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%u, TimePerVMGroupVBlank = %f\n", __func__, k, TimePerVMGroupVBlank[k]);
			dml_print("DML::%s: k=%u, TimePerVMGroupFlip = %f\n", __func__, k, TimePerVMGroupFlip[k]);
			dml_print("DML::%s: k=%u, TimePerVMRequestVBlank = %f\n", __func__, k, TimePerVMRequestVBlank[k]);
			dml_print("DML::%s: k=%u, TimePerVMRequestFlip = %f\n", __func__, k, TimePerVMRequestFlip[k]);
#endif
	}
} // CalculateVMGroupAndRequestTimes

static void CalculateStutterEfficiency(struct display_mode_lib_scratch_st *scratch,
		struct CalculateStutterEfficiency_params_st *p)
{
	dml_float_t DETBufferingTimeY = 0;
	dml_float_t SwathWidthYCriticalSurface = 0;
	dml_float_t SwathHeightYCriticalSurface = 0;
	dml_float_t VActiveTimeCriticalSurface = 0;
	dml_float_t FrameTimeCriticalSurface = 0;
	dml_uint_t BytePerPixelYCriticalSurface = 0;
	dml_float_t LinesToFinishSwathTransferStutterCriticalSurface = 0;
	dml_uint_t DETBufferSizeYCriticalSurface = 0;
	dml_float_t MinTTUVBlankCriticalSurface = 0;
	dml_uint_t BlockWidth256BytesYCriticalSurface = 0;
	dml_bool_t SinglePlaneCriticalSurface = 0;
	dml_bool_t SinglePipeCriticalSurface = 0;
	dml_float_t TotalCompressedReadBandwidth = 0;
	dml_float_t TotalRowReadBandwidth = 0;
	dml_float_t AverageDCCCompressionRate = 0;
	dml_float_t EffectiveCompressedBufferSize = 0;
	dml_float_t PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer = 0;
	dml_float_t StutterBurstTime = 0;
	dml_uint_t TotalActiveWriteback = 0;
	dml_float_t LinesInDETY = 0;
	dml_float_t LinesInDETYRoundedDownToSwath = 0;
	dml_float_t MaximumEffectiveCompressionLuma = 0;
	dml_float_t MaximumEffectiveCompressionChroma = 0;
	dml_float_t TotalZeroSizeRequestReadBandwidth = 0;
	dml_float_t TotalZeroSizeCompressedReadBandwidth = 0;
	dml_float_t AverageDCCZeroSizeFraction = 0;
	dml_float_t AverageZeroSizeCompressionRate = 0;

	dml_bool_t FoundCriticalSurface = false;

	dml_uint_t TotalNumberOfActiveOTG = 0;
	dml_float_t SinglePixelClock = 0;
	dml_uint_t SingleHTotal = 0;
	dml_uint_t SingleVTotal = 0;
	dml_bool_t SameTiming = true;

	dml_float_t LastStutterPeriod = 0.0;
	dml_float_t LastZ8StutterPeriod = 0.0;

	dml_uint_t SwathSizeCriticalSurface;
	dml_uint_t LastChunkOfSwathSize;
	dml_uint_t MissingPartOfLastSwathOfDETSize;

	TotalZeroSizeRequestReadBandwidth = 0;
	TotalZeroSizeCompressedReadBandwidth = 0;
	TotalRowReadBandwidth = 0;
	TotalCompressedReadBandwidth = 0;

	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
	if (p->UseMALLForPStateChange[k] != dml_use_mall_pstate_change_phantom_pipe) {
		if (p->DCCEnable[k] == true) {
			if ((dml_is_vertical_rotation(p->SourceScan[k]) && p->BlockWidth256BytesY[k] > p->SwathHeightY[k]) || (!dml_is_vertical_rotation(p->SourceScan[k]) && p->BlockHeight256BytesY[k] > p->SwathHeightY[k]) || p->DCCYMaxUncompressedBlock[k] < 256) {
				MaximumEffectiveCompressionLuma = 2;
			} else {
				MaximumEffectiveCompressionLuma = 4;
			}
			TotalCompressedReadBandwidth = TotalCompressedReadBandwidth + p->ReadBandwidthSurfaceLuma[k] / dml_min(p->NetDCCRateLuma[k], MaximumEffectiveCompressionLuma);
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%u, ReadBandwidthSurfaceLuma = %f\n", __func__, k, p->ReadBandwidthSurfaceLuma[k]);
			dml_print("DML::%s: k=%u, NetDCCRateLuma = %f\n", __func__, k, p->NetDCCRateLuma[k]);
			dml_print("DML::%s: k=%u, MaximumEffectiveCompressionLuma = %f\n", __func__, k, MaximumEffectiveCompressionLuma);
#endif
			TotalZeroSizeRequestReadBandwidth = TotalZeroSizeRequestReadBandwidth + p->ReadBandwidthSurfaceLuma[k] * p->DCCFractionOfZeroSizeRequestsLuma[k];
			TotalZeroSizeCompressedReadBandwidth = TotalZeroSizeCompressedReadBandwidth + p->ReadBandwidthSurfaceLuma[k] * p->DCCFractionOfZeroSizeRequestsLuma[k] / MaximumEffectiveCompressionLuma;

			if (p->ReadBandwidthSurfaceChroma[k] > 0) {
				if ((dml_is_vertical_rotation(p->SourceScan[k]) && p->BlockWidth256BytesC[k] > p->SwathHeightC[k]) || (!dml_is_vertical_rotation(p->SourceScan[k]) && p->BlockHeight256BytesC[k] > p->SwathHeightC[k]) || p->DCCCMaxUncompressedBlock[k] < 256) {
					MaximumEffectiveCompressionChroma = 2;
				} else {
					MaximumEffectiveCompressionChroma = 4;
				}
				TotalCompressedReadBandwidth = TotalCompressedReadBandwidth + p->ReadBandwidthSurfaceChroma[k] / dml_min(p->NetDCCRateChroma[k], MaximumEffectiveCompressionChroma);
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: k=%u, ReadBandwidthSurfaceChroma = %f\n", __func__, k, p->ReadBandwidthSurfaceChroma[k]);
				dml_print("DML::%s: k=%u, NetDCCRateChroma = %f\n", __func__, k, p->NetDCCRateChroma[k]);
				dml_print("DML::%s: k=%u, MaximumEffectiveCompressionChroma = %f\n", __func__, k, MaximumEffectiveCompressionChroma);
#endif
				TotalZeroSizeRequestReadBandwidth = TotalZeroSizeRequestReadBandwidth + p->ReadBandwidthSurfaceChroma[k] * p->DCCFractionOfZeroSizeRequestsChroma[k];
				TotalZeroSizeCompressedReadBandwidth = TotalZeroSizeCompressedReadBandwidth + p->ReadBandwidthSurfaceChroma[k] * p->DCCFractionOfZeroSizeRequestsChroma[k] / MaximumEffectiveCompressionChroma;
			}
		} else {
			TotalCompressedReadBandwidth = TotalCompressedReadBandwidth + p->ReadBandwidthSurfaceLuma[k] + p->ReadBandwidthSurfaceChroma[k];
		}
		TotalRowReadBandwidth = TotalRowReadBandwidth + p->DPPPerSurface[k] * (p->meta_row_bw[k] + p->dpte_row_bw[k]);
	}
	}

	AverageDCCCompressionRate = p->TotalDataReadBandwidth / TotalCompressedReadBandwidth;
	AverageDCCZeroSizeFraction = TotalZeroSizeRequestReadBandwidth / p->TotalDataReadBandwidth;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: UnboundedRequestEnabled = %u\n", __func__, p->UnboundedRequestEnabled);
	dml_print("DML::%s: TotalCompressedReadBandwidth = %f\n", __func__, TotalCompressedReadBandwidth);
	dml_print("DML::%s: TotalZeroSizeRequestReadBandwidth = %f\n", __func__, TotalZeroSizeRequestReadBandwidth);
	dml_print("DML::%s: TotalZeroSizeCompressedReadBandwidth = %f\n", __func__, TotalZeroSizeCompressedReadBandwidth);
	dml_print("DML::%s: MaximumEffectiveCompressionLuma = %f\n", __func__, MaximumEffectiveCompressionLuma);
	dml_print("DML::%s: MaximumEffectiveCompressionChroma = %f\n", __func__, MaximumEffectiveCompressionChroma);
	dml_print("DML::%s: AverageDCCCompressionRate = %f\n", __func__, AverageDCCCompressionRate);
	dml_print("DML::%s: AverageDCCZeroSizeFraction = %f\n", __func__, AverageDCCZeroSizeFraction);
	dml_print("DML::%s: CompbufReservedSpace64B = %u\n", __func__, p->CompbufReservedSpace64B);
	dml_print("DML::%s: CompbufReservedSpaceZs = %u\n", __func__, p->CompbufReservedSpaceZs);
	dml_print("DML::%s: CompressedBufferSizeInkByte = %u\n", __func__, p->CompressedBufferSizeInkByte);
#endif
	if (AverageDCCZeroSizeFraction == 1) {
		AverageZeroSizeCompressionRate = TotalZeroSizeRequestReadBandwidth / TotalZeroSizeCompressedReadBandwidth;
		EffectiveCompressedBufferSize = (dml_float_t)p->MetaFIFOSizeInKEntries * 1024 * 64 * AverageZeroSizeCompressionRate + ((dml_float_t)p->ZeroSizeBufferEntries - p->CompbufReservedSpaceZs) * 64 * AverageZeroSizeCompressionRate;
	} 	else if (AverageDCCZeroSizeFraction > 0) {
		AverageZeroSizeCompressionRate = TotalZeroSizeRequestReadBandwidth / TotalZeroSizeCompressedReadBandwidth;
		EffectiveCompressedBufferSize = dml_min((dml_float_t)p->CompressedBufferSizeInkByte * 1024 * AverageDCCCompressionRate,
											(dml_float_t)p->MetaFIFOSizeInKEntries * 1024 * 64 / (AverageDCCZeroSizeFraction / AverageZeroSizeCompressionRate + 1 / AverageDCCCompressionRate)) +
										dml_min(((dml_float_t)p->ROBBufferSizeInKByte * 1024 - p->CompbufReservedSpace64B * 64) * AverageDCCCompressionRate,
											((dml_float_t)p->ZeroSizeBufferEntries - p->CompbufReservedSpaceZs) * 64 / (AverageDCCZeroSizeFraction / AverageZeroSizeCompressionRate));

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: min 1 = %f\n", __func__, p->CompressedBufferSizeInkByte * 1024 * AverageDCCCompressionRate);
		dml_print("DML::%s: min 2 = %f\n", __func__, p->MetaFIFOSizeInKEntries * 1024 * 64 / (AverageDCCZeroSizeFraction / AverageZeroSizeCompressionRate + 1 / AverageDCCCompressionRate));
		dml_print("DML::%s: min 3 = %f\n", __func__, (p->ROBBufferSizeInKByte * 1024 - p->CompbufReservedSpace64B * 64) * AverageDCCCompressionRate);
		dml_print("DML::%s: min 4 = %f\n", __func__, (p->ZeroSizeBufferEntries - p->CompbufReservedSpaceZs) * 64 / (AverageDCCZeroSizeFraction / AverageZeroSizeCompressionRate));
#endif
	} else {
		EffectiveCompressedBufferSize = dml_min((dml_float_t)p->CompressedBufferSizeInkByte * 1024 * AverageDCCCompressionRate,
												(dml_float_t)p->MetaFIFOSizeInKEntries * 1024 * 64 * AverageDCCCompressionRate) +
												((dml_float_t)p->ROBBufferSizeInKByte * 1024 - p->CompbufReservedSpace64B * 64) * AverageDCCCompressionRate;

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: min 1 = %f\n", __func__, p->CompressedBufferSizeInkByte * 1024 * AverageDCCCompressionRate);
		dml_print("DML::%s: min 2 = %f\n", __func__, p->MetaFIFOSizeInKEntries * 1024 * 64 * AverageDCCCompressionRate);
#endif
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: MetaFIFOSizeInKEntries = %u\n", __func__, p->MetaFIFOSizeInKEntries);
	dml_print("DML::%s: AverageZeroSizeCompressionRate = %f\n", __func__, AverageZeroSizeCompressionRate);
	dml_print("DML::%s: EffectiveCompressedBufferSize = %f\n", __func__, EffectiveCompressedBufferSize);
#endif

	*p->StutterPeriod = 0;

	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
	if (p->UseMALLForPStateChange[k] != dml_use_mall_pstate_change_phantom_pipe) {
		LinesInDETY = ((dml_float_t)p->DETBufferSizeY[k] + (p->UnboundedRequestEnabled == true ? EffectiveCompressedBufferSize : 0) * p->ReadBandwidthSurfaceLuma[k] / p->TotalDataReadBandwidth) / p->BytePerPixelDETY[k] / p->SwathWidthY[k];
		LinesInDETYRoundedDownToSwath = dml_floor(LinesInDETY, p->SwathHeightY[k]);
		DETBufferingTimeY = LinesInDETYRoundedDownToSwath * ((dml_float_t)p->HTotal[k] / p->PixelClock[k]) / p->VRatio[k];
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u, DETBufferSizeY = %u\n", __func__, k, p->DETBufferSizeY[k]);
		dml_print("DML::%s: k=%u, BytePerPixelDETY = %f\n", __func__, k, p->BytePerPixelDETY[k]);
		dml_print("DML::%s: k=%u, SwathWidthY = %u\n", __func__, k, p->SwathWidthY[k]);
		dml_print("DML::%s: k=%u, ReadBandwidthSurfaceLuma = %f\n", __func__, k, p->ReadBandwidthSurfaceLuma[k]);
		dml_print("DML::%s: k=%u, TotalDataReadBandwidth = %f\n", __func__, k, p->TotalDataReadBandwidth);
		dml_print("DML::%s: k=%u, LinesInDETY = %f\n", __func__, k, LinesInDETY);
		dml_print("DML::%s: k=%u, LinesInDETYRoundedDownToSwath = %f\n", __func__, k, LinesInDETYRoundedDownToSwath);
		dml_print("DML::%s: k=%u, HTotal = %u\n", __func__, k, p->HTotal[k]);
		dml_print("DML::%s: k=%u, PixelClock = %f\n", __func__, k, p->PixelClock[k]);
		dml_print("DML::%s: k=%u, VRatio = %f\n", __func__, k, p->VRatio[k]);
		dml_print("DML::%s: k=%u, DETBufferingTimeY = %f\n", __func__, k, DETBufferingTimeY);
		dml_print("DML::%s: k=%u,PixelClock = %f\n", __func__, k, p->PixelClock[k]);
#endif

		if (!FoundCriticalSurface || DETBufferingTimeY < *p->StutterPeriod) {
			dml_bool_t isInterlaceTiming = p->Interlace[k] && !p->ProgressiveToInterlaceUnitInOPP;

			FoundCriticalSurface = true;
			*p->StutterPeriod = DETBufferingTimeY;
			FrameTimeCriticalSurface = (isInterlaceTiming ? dml_floor((dml_float_t)p->VTotal[k]/2.0, 1.0) : p->VTotal[k]) * (dml_float_t)p->HTotal[k] / p->PixelClock[k];
			VActiveTimeCriticalSurface = (isInterlaceTiming ? dml_floor((dml_float_t)p->VActive[k]/2.0, 1.0) : p->VActive[k]) * (dml_float_t)p->HTotal[k] / p->PixelClock[k];
			BytePerPixelYCriticalSurface = p->BytePerPixelY[k];
			SwathWidthYCriticalSurface = p->SwathWidthY[k];
			SwathHeightYCriticalSurface = p->SwathHeightY[k];
			BlockWidth256BytesYCriticalSurface = p->BlockWidth256BytesY[k];
			LinesToFinishSwathTransferStutterCriticalSurface = p->SwathHeightY[k] - (LinesInDETY - LinesInDETYRoundedDownToSwath);
			DETBufferSizeYCriticalSurface = p->DETBufferSizeY[k];
			MinTTUVBlankCriticalSurface = p->MinTTUVBlank[k];
			SinglePlaneCriticalSurface = (p->ReadBandwidthSurfaceChroma[k] == 0);
			SinglePipeCriticalSurface = (p->DPPPerSurface[k] == 1);

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%u, FoundCriticalSurface = %u\n", __func__, k, FoundCriticalSurface);
			dml_print("DML::%s: k=%u, StutterPeriod = %f\n", __func__, k, *p->StutterPeriod);
			dml_print("DML::%s: k=%u, MinTTUVBlankCriticalSurface = %f\n", __func__, k, MinTTUVBlankCriticalSurface);
			dml_print("DML::%s: k=%u, FrameTimeCriticalSurface = %f\n", __func__, k, FrameTimeCriticalSurface);
			dml_print("DML::%s: k=%u, VActiveTimeCriticalSurface = %f\n", __func__, k, VActiveTimeCriticalSurface);
			dml_print("DML::%s: k=%u, BytePerPixelYCriticalSurface = %u\n", __func__, k, BytePerPixelYCriticalSurface);
			dml_print("DML::%s: k=%u, SwathWidthYCriticalSurface = %f\n", __func__, k, SwathWidthYCriticalSurface);
			dml_print("DML::%s: k=%u, SwathHeightYCriticalSurface = %f\n", __func__, k, SwathHeightYCriticalSurface);
			dml_print("DML::%s: k=%u, BlockWidth256BytesYCriticalSurface = %u\n", __func__, k, BlockWidth256BytesYCriticalSurface);
			dml_print("DML::%s: k=%u, SinglePlaneCriticalSurface = %u\n", __func__, k, SinglePlaneCriticalSurface);
			dml_print("DML::%s: k=%u, SinglePipeCriticalSurface = %u\n", __func__, k, SinglePipeCriticalSurface);
			dml_print("DML::%s: k=%u, LinesToFinishSwathTransferStutterCriticalSurface = %f\n", __func__, k, LinesToFinishSwathTransferStutterCriticalSurface);
#endif
		}
	}
	}

	PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer = dml_min(*p->StutterPeriod * p->TotalDataReadBandwidth, EffectiveCompressedBufferSize);
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: ROBBufferSizeInKByte = %u\n", __func__, p->ROBBufferSizeInKByte);
	dml_print("DML::%s: AverageDCCCompressionRate = %f\n", __func__, AverageDCCCompressionRate);
	dml_print("DML::%s: StutterPeriod * TotalDataReadBandwidth = %f\n", __func__, *p->StutterPeriod * p->TotalDataReadBandwidth);
	dml_print("DML::%s: ROBBufferSizeInKByte * 1024 * AverageDCCCompressionRate + EffectiveCompressedBufferSize = %f\n", __func__, p->ROBBufferSizeInKByte * 1024 * AverageDCCCompressionRate + EffectiveCompressedBufferSize);
	dml_print("DML::%s: EffectiveCompressedBufferSize = %f\n", __func__, EffectiveCompressedBufferSize);
	dml_print("DML::%s: PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer = %f\n", __func__, PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer);
	dml_print("DML::%s: ReturnBW = %f\n", __func__, p->ReturnBW);
	dml_print("DML::%s: TotalDataReadBandwidth = %f\n", __func__, p->TotalDataReadBandwidth);
	dml_print("DML::%s: TotalRowReadBandwidth = %f\n", __func__, TotalRowReadBandwidth);
	dml_print("DML::%s: DCFCLK = %f\n", __func__, p->DCFCLK);
#endif

	StutterBurstTime = PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer / AverageDCCCompressionRate / p->ReturnBW + (*p->StutterPeriod * p->TotalDataReadBandwidth - PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer) / (p->DCFCLK * 64) + *p->StutterPeriod * TotalRowReadBandwidth / p->ReturnBW;
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: Part 1 = %f\n", __func__, PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer / AverageDCCCompressionRate / p->ReturnBW);
	dml_print("DML::%s: StutterPeriod * TotalDataReadBandwidth = %f\n", __func__, (*p->StutterPeriod * p->TotalDataReadBandwidth));
	dml_print("DML::%s: Part 2 = %f\n", __func__, (*p->StutterPeriod * p->TotalDataReadBandwidth - PartOfUncompressedPixelBurstThatFitsInROBAndCompressedBuffer) / (p->DCFCLK * 64));
	dml_print("DML::%s: Part 3 = %f\n", __func__, *p->StutterPeriod * TotalRowReadBandwidth / p->ReturnBW);
	dml_print("DML::%s: StutterBurstTime = %f\n", __func__, StutterBurstTime);
#endif
	StutterBurstTime = dml_max(StutterBurstTime, LinesToFinishSwathTransferStutterCriticalSurface * BytePerPixelYCriticalSurface * SwathWidthYCriticalSurface / p->ReturnBW);

	dml_print("DML::%s: Time to finish residue swath=%f\n", __func__, LinesToFinishSwathTransferStutterCriticalSurface * BytePerPixelYCriticalSurface * SwathWidthYCriticalSurface / p->ReturnBW);

	TotalActiveWriteback = 0;
	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (p->WritebackEnable[k]) {
			TotalActiveWriteback = TotalActiveWriteback + 1;
		}
	}

	if (TotalActiveWriteback == 0) {
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: SRExitTime = %f\n", __func__, p->SRExitTime);
		dml_print("DML::%s: SRExitZ8Time = %f\n", __func__, p->SRExitZ8Time);
		dml_print("DML::%s: StutterBurstTime = %f (final)\n", __func__, StutterBurstTime);
		dml_print("DML::%s: StutterPeriod = %f\n", __func__, *p->StutterPeriod);
#endif
		*p->StutterEfficiencyNotIncludingVBlank = dml_max(0., 1 - (p->SRExitTime + StutterBurstTime) / *p->StutterPeriod) * 100;
		*p->Z8StutterEfficiencyNotIncludingVBlank = dml_max(0., 1 - (p->SRExitZ8Time + StutterBurstTime) / *p->StutterPeriod) * 100;
		*p->NumberOfStutterBurstsPerFrame = (*p->StutterEfficiencyNotIncludingVBlank > 0 ? (dml_uint_t)(dml_ceil(VActiveTimeCriticalSurface / *p->StutterPeriod, 1)) : 0);
		*p->Z8NumberOfStutterBurstsPerFrame = (*p->Z8StutterEfficiencyNotIncludingVBlank > 0 ? (dml_uint_t)(dml_ceil(VActiveTimeCriticalSurface / *p->StutterPeriod, 1)) : 0);
	} else {
		*p->StutterEfficiencyNotIncludingVBlank = 0.;
		*p->Z8StutterEfficiencyNotIncludingVBlank = 0.;
		*p->NumberOfStutterBurstsPerFrame = 0;
		*p->Z8NumberOfStutterBurstsPerFrame = 0;
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: VActiveTimeCriticalSurface = %f\n", __func__, VActiveTimeCriticalSurface);
	dml_print("DML::%s: StutterEfficiencyNotIncludingVBlank = %f\n", __func__, *p->StutterEfficiencyNotIncludingVBlank);
	dml_print("DML::%s: Z8StutterEfficiencyNotIncludingVBlank = %f\n", __func__, *p->Z8StutterEfficiencyNotIncludingVBlank);
	dml_print("DML::%s: NumberOfStutterBurstsPerFrame = %u\n", __func__, *p->NumberOfStutterBurstsPerFrame);
	dml_print("DML::%s: Z8NumberOfStutterBurstsPerFrame = %u\n", __func__, *p->Z8NumberOfStutterBurstsPerFrame);
#endif

	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (p->UseMALLForPStateChange[k] != dml_use_mall_pstate_change_phantom_pipe) {
			if (p->BlendingAndTiming[k] == k) {
				if (TotalNumberOfActiveOTG == 0) {
					SinglePixelClock = p->PixelClock[k];
					SingleHTotal = p->HTotal[k];
					SingleVTotal = p->VTotal[k];
				} else if (SinglePixelClock != p->PixelClock[k] || SingleHTotal != p->HTotal[k] || SingleVTotal != p->VTotal[k]) {
					SameTiming = false;
				}
				TotalNumberOfActiveOTG = TotalNumberOfActiveOTG + 1;
			}
		}
	}

	if (*p->StutterEfficiencyNotIncludingVBlank > 0) {
		LastStutterPeriod = VActiveTimeCriticalSurface - (*p->NumberOfStutterBurstsPerFrame - 1) * *p->StutterPeriod;

		if ((p->SynchronizeTimingsFinal || TotalNumberOfActiveOTG == 1) && SameTiming &&
			LastStutterPeriod + MinTTUVBlankCriticalSurface > p->StutterEnterPlusExitWatermark) {
			*p->StutterEfficiency = (1 - (*p->NumberOfStutterBurstsPerFrame * p->SRExitTime + StutterBurstTime * VActiveTimeCriticalSurface / *p->StutterPeriod) / FrameTimeCriticalSurface) * 100;
		} else {
			*p->StutterEfficiency = *p->StutterEfficiencyNotIncludingVBlank;
		}
	} else {
		*p->StutterEfficiency = 0;
	}

	if (*p->Z8StutterEfficiencyNotIncludingVBlank > 0) {
		LastZ8StutterPeriod = VActiveTimeCriticalSurface - (*p->NumberOfStutterBurstsPerFrame - 1) * *p->StutterPeriod;
		if ((p->SynchronizeTimingsFinal || TotalNumberOfActiveOTG == 1) && SameTiming && LastZ8StutterPeriod + MinTTUVBlankCriticalSurface > p->Z8StutterEnterPlusExitWatermark) {
			*p->Z8StutterEfficiency = (1 - (*p->NumberOfStutterBurstsPerFrame * p->SRExitZ8Time + StutterBurstTime * VActiveTimeCriticalSurface / *p->StutterPeriod) / FrameTimeCriticalSurface) * 100;
		} else {
			*p->Z8StutterEfficiency = *p->Z8StutterEfficiencyNotIncludingVBlank;
		}
	} else {
		*p->Z8StutterEfficiency = 0.;
	}

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: LastZ8StutterPeriod = %f\n", __func__, LastZ8StutterPeriod);
		dml_print("DML::%s: Z8StutterEnterPlusExitWatermark = %f\n", __func__, p->Z8StutterEnterPlusExitWatermark);
		dml_print("DML::%s: StutterBurstTime = %f\n", __func__, StutterBurstTime);
		dml_print("DML::%s: StutterPeriod = %f\n", __func__, *p->StutterPeriod);
		dml_print("DML::%s: StutterEfficiency = %f\n", __func__, *p->StutterEfficiency);
		dml_print("DML::%s: Z8StutterEfficiency = %f\n", __func__, *p->Z8StutterEfficiency);
		dml_print("DML::%s: StutterEfficiencyNotIncludingVBlank = %f\n", __func__, *p->StutterEfficiencyNotIncludingVBlank);
		dml_print("DML::%s: Z8NumberOfStutterBurstsPerFrame = %u\n", __func__, *p->Z8NumberOfStutterBurstsPerFrame);
#endif

		SwathSizeCriticalSurface = (dml_uint_t)(BytePerPixelYCriticalSurface * SwathHeightYCriticalSurface * dml_ceil(SwathWidthYCriticalSurface, BlockWidth256BytesYCriticalSurface));
		LastChunkOfSwathSize = SwathSizeCriticalSurface % (p->PixelChunkSizeInKByte * 1024);
		MissingPartOfLastSwathOfDETSize = (dml_uint_t)(dml_ceil(DETBufferSizeYCriticalSurface, SwathSizeCriticalSurface) - DETBufferSizeYCriticalSurface);

		*p->DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE = !(!p->UnboundedRequestEnabled && (p->NumberOfActiveSurfaces == 1) && SinglePlaneCriticalSurface && SinglePipeCriticalSurface && (LastChunkOfSwathSize > 0) &&
			(LastChunkOfSwathSize <= 4096) && (MissingPartOfLastSwathOfDETSize > 0) && (MissingPartOfLastSwathOfDETSize <= LastChunkOfSwathSize));

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: SwathSizeCriticalSurface = %u\n", __func__, SwathSizeCriticalSurface);
	dml_print("DML::%s: DETBufferSizeYCriticalSurface = %u\n", __func__, DETBufferSizeYCriticalSurface);
	dml_print("DML::%s: PixelChunkSizeInKByte = %u\n", __func__, p->PixelChunkSizeInKByte);
	dml_print("DML::%s: LastChunkOfSwathSize = %u\n", __func__, LastChunkOfSwathSize);
	dml_print("DML::%s: MissingPartOfLastSwathOfDETSize = %u\n", __func__, MissingPartOfLastSwathOfDETSize);
	dml_print("DML::%s: DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE = %u\n", __func__, *p->DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE);
#endif
} // CalculateStutterEfficiency

/// \CalculateSwathAndDETConfiguration
/// @brief Calculates swath width and different return buffers sizing (DET, CDB, etc.)
static void CalculateSwathAndDETConfiguration(struct display_mode_lib_scratch_st *scratch,
	struct CalculateSwathAndDETConfiguration_params_st *p)
{
	dml_uint_t MaximumSwathHeightY[__DML_NUM_PLANES__];
	dml_uint_t MaximumSwathHeightC[__DML_NUM_PLANES__];
	dml_uint_t RoundedUpMaxSwathSizeBytesY[__DML_NUM_PLANES__];
	dml_uint_t RoundedUpMaxSwathSizeBytesC[__DML_NUM_PLANES__];
	dml_uint_t RoundedUpSwathSizeBytesY[__DML_NUM_PLANES__];
	dml_uint_t RoundedUpSwathSizeBytesC[__DML_NUM_PLANES__];
	dml_uint_t SwathWidthSingleDPP[__DML_NUM_PLANES__];
	dml_uint_t SwathWidthSingleDPPChroma[__DML_NUM_PLANES__];

	dml_uint_t TotalActiveDPP = 0;
	dml_bool_t NoChromaOrLinearSurfaces = true;
	dml_uint_t SurfaceDoingUnboundedRequest = 0;

	dml_uint_t DETBufferSizeInKByteForSwathCalculation;

	const long TTUFIFODEPTH = 8;
	const long MAXIMUMCOMPRESSION = 4;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: ForceSingleDPP = %u\n", __func__, p->ForceSingleDPP);
	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		dml_print("DML::%s: DPPPerSurface[%u] = %u\n", __func__, k, p->DPPPerSurface[k]);
	}
#endif
	CalculateSwathWidth(p->ForceSingleDPP,
		p->NumberOfActiveSurfaces,
		p->SourcePixelFormat,
		p->SourceScan,
		p->ViewportStationary,
		p->ViewportWidth,
		p->ViewportHeight,
		p->ViewportXStart,
		p->ViewportYStart,
		p->ViewportXStartC,
		p->ViewportYStartC,
		p->SurfaceWidthY,
		p->SurfaceWidthC,
		p->SurfaceHeightY,
		p->SurfaceHeightC,
		p->ODMMode,
		p->BytePerPixY,
		p->BytePerPixC,
		p->Read256BytesBlockHeightY,
		p->Read256BytesBlockHeightC,
		p->Read256BytesBlockWidthY,
		p->Read256BytesBlockWidthC,
		p->BlendingAndTiming,
		p->HActive,
		p->HRatio,
		p->DPPPerSurface,

		// Output
		SwathWidthSingleDPP,
		SwathWidthSingleDPPChroma,
		p->SwathWidth,
		p->SwathWidthChroma,
		MaximumSwathHeightY,
		MaximumSwathHeightC,
		p->swath_width_luma_ub,
		p->swath_width_chroma_ub);

	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		RoundedUpMaxSwathSizeBytesY[k] = (dml_uint_t)(p->swath_width_luma_ub[k] * p->BytePerPixDETY[k] * MaximumSwathHeightY[k]);
		RoundedUpMaxSwathSizeBytesC[k] = (dml_uint_t)(p->swath_width_chroma_ub[k] * p->BytePerPixDETC[k] * MaximumSwathHeightC[k]);
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u DPPPerSurface = %u\n", __func__, k, p->DPPPerSurface[k]);
		dml_print("DML::%s: k=%u swath_width_luma_ub = %u\n", __func__, k, p->swath_width_luma_ub[k]);
		dml_print("DML::%s: k=%u BytePerPixDETY = %f\n", __func__, k, p->BytePerPixDETY[k]);
		dml_print("DML::%s: k=%u MaximumSwathHeightY = %u\n", __func__, k, MaximumSwathHeightY[k]);
		dml_print("DML::%s: k=%u RoundedUpMaxSwathSizeBytesY = %u\n", __func__, k, RoundedUpMaxSwathSizeBytesY[k]);
		dml_print("DML::%s: k=%u swath_width_chroma_ub = %u\n", __func__, k, p->swath_width_chroma_ub[k]);
		dml_print("DML::%s: k=%u BytePerPixDETC = %f\n", __func__, k, p->BytePerPixDETC[k]);
		dml_print("DML::%s: k=%u MaximumSwathHeightC = %u\n", __func__, k, MaximumSwathHeightC[k]);
		dml_print("DML::%s: k=%u RoundedUpMaxSwathSizeBytesC = %u\n", __func__, k, RoundedUpMaxSwathSizeBytesC[k]);
#endif
		if (p->SourcePixelFormat[k] == dml_420_10) {
			RoundedUpMaxSwathSizeBytesY[k] = (dml_uint_t)(dml_ceil((dml_float_t) RoundedUpMaxSwathSizeBytesY[k], 256));
			RoundedUpMaxSwathSizeBytesC[k] = (dml_uint_t)(dml_ceil((dml_float_t) RoundedUpMaxSwathSizeBytesC[k], 256));
		}
	}

	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		TotalActiveDPP = TotalActiveDPP + (p->ForceSingleDPP ? 1 : p->DPPPerSurface[k]);
		if (p->DPPPerSurface[k] > 0)
			SurfaceDoingUnboundedRequest = k;
		if (p->SourcePixelFormat[k] == dml_420_8 || p->SourcePixelFormat[k] == dml_420_10 ||
				p->SourcePixelFormat[k] == dml_420_12 || p->SourcePixelFormat[k] == dml_rgbe_alpha
				|| p->SurfaceTiling[k] == dml_sw_linear) {
			NoChromaOrLinearSurfaces = false;
		}
	}

	*p->UnboundedRequestEnabled = UnboundedRequest(p->UseUnboundedRequestingFinal, TotalActiveDPP,
			NoChromaOrLinearSurfaces, p->Output[0]);

	CalculateDETBufferSize(p->DETSizeOverride,
		p->UseMALLForPStateChange,
		p->ForceSingleDPP,
		p->NumberOfActiveSurfaces,
		*p->UnboundedRequestEnabled,
		p->nomDETInKByte,
		p->MaxTotalDETInKByte,
		p->ConfigReturnBufferSizeInKByte,
		p->MinCompressedBufferSizeInKByte,
		p->ConfigReturnBufferSegmentSizeInkByte,
		p->CompressedBufferSegmentSizeInkByteFinal,
		p->SourcePixelFormat,
		p->ReadBandwidthLuma,
		p->ReadBandwidthChroma,
		RoundedUpMaxSwathSizeBytesY,
		RoundedUpMaxSwathSizeBytesC,
		p->DPPPerSurface,

		// Output
		p->DETBufferSizeInKByte, // per hubp pipe
		p->CompressedBufferSizeInkByte);

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: TotalActiveDPP = %u\n", __func__, TotalActiveDPP);
		dml_print("DML::%s: nomDETInKByte = %u\n", __func__, p->nomDETInKByte);
		dml_print("DML::%s: ConfigReturnBufferSizeInKByte = %u\n", __func__, p->ConfigReturnBufferSizeInKByte);
		dml_print("DML::%s: UseUnboundedRequestingFinal = %u\n", __func__, p->UseUnboundedRequestingFinal);
		dml_print("DML::%s: UnboundedRequestEnabled = %u\n", __func__, *p->UnboundedRequestEnabled);
		dml_print("DML::%s: CompressedBufferSizeInkByte = %u\n", __func__, *p->CompressedBufferSizeInkByte);
#endif

	*p->ViewportSizeSupport = true;
	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {

		DETBufferSizeInKByteForSwathCalculation =  (p->UseMALLForPStateChange[k] == dml_use_mall_pstate_change_phantom_pipe ? 1024 : p->DETBufferSizeInKByte[k]);
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: k=%u DETBufferSizeInKByteForSwathCalculation = %u\n", __func__, k, DETBufferSizeInKByteForSwathCalculation);
#endif

		if (RoundedUpMaxSwathSizeBytesY[k] + RoundedUpMaxSwathSizeBytesC[k] <= DETBufferSizeInKByteForSwathCalculation * 1024 / 2) {
			p->SwathHeightY[k] = MaximumSwathHeightY[k];
			p->SwathHeightC[k] = MaximumSwathHeightC[k];
			RoundedUpSwathSizeBytesY[k] = RoundedUpMaxSwathSizeBytesY[k];
			RoundedUpSwathSizeBytesC[k] = RoundedUpMaxSwathSizeBytesC[k];
		} else if (RoundedUpMaxSwathSizeBytesY[k] >= 1.5 * RoundedUpMaxSwathSizeBytesC[k] && RoundedUpMaxSwathSizeBytesY[k] / 2 + RoundedUpMaxSwathSizeBytesC[k] <= DETBufferSizeInKByteForSwathCalculation * 1024 / 2) {
			p->SwathHeightY[k] = MaximumSwathHeightY[k] / 2;
			p->SwathHeightC[k] = MaximumSwathHeightC[k];
			RoundedUpSwathSizeBytesY[k] = RoundedUpMaxSwathSizeBytesY[k] / 2;
			RoundedUpSwathSizeBytesC[k] = RoundedUpMaxSwathSizeBytesC[k];
		} else if (RoundedUpMaxSwathSizeBytesY[k] < 1.5 * RoundedUpMaxSwathSizeBytesC[k] && RoundedUpMaxSwathSizeBytesY[k] + RoundedUpMaxSwathSizeBytesC[k] / 2 <= DETBufferSizeInKByteForSwathCalculation * 1024 / 2) {
			p->SwathHeightY[k] = MaximumSwathHeightY[k];
			p->SwathHeightC[k] = MaximumSwathHeightC[k] / 2;
			RoundedUpSwathSizeBytesY[k] = RoundedUpMaxSwathSizeBytesY[k];
			RoundedUpSwathSizeBytesC[k] = RoundedUpMaxSwathSizeBytesC[k] / 2;
		} else {
			p->SwathHeightY[k] = MaximumSwathHeightY[k] / 2;
			p->SwathHeightC[k] = MaximumSwathHeightC[k] / 2;
			RoundedUpSwathSizeBytesY[k] = RoundedUpMaxSwathSizeBytesY[k] / 2;
			RoundedUpSwathSizeBytesC[k] = RoundedUpMaxSwathSizeBytesC[k] / 2;
		}

		if ((RoundedUpMaxSwathSizeBytesY[k] / 2 + RoundedUpMaxSwathSizeBytesC[k] / 2 > DETBufferSizeInKByteForSwathCalculation * 1024 / 2) ||
			p->SwathWidth[k] > p->MaximumSwathWidthLuma[k] || (p->SwathHeightC[k] > 0 && p->SwathWidthChroma[k] > p->MaximumSwathWidthChroma[k])) {
			*p->ViewportSizeSupport = false;
			p->ViewportSizeSupportPerSurface[k] = false;
		} else {
			p->ViewportSizeSupportPerSurface[k] = true;
		}

	if (p->SwathHeightC[k] == 0) {
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%u All DET for plane0\n", __func__, k);
#endif
			p->DETBufferSizeY[k] = p->DETBufferSizeInKByte[k] * 1024;
			p->DETBufferSizeC[k] = 0;
		} else if (RoundedUpSwathSizeBytesY[k] <= 1.5 * RoundedUpSwathSizeBytesC[k]) {
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%u Half DET for plane0, half for plane1\n", __func__, k);
#endif
			p->DETBufferSizeY[k] = p->DETBufferSizeInKByte[k] * 1024 / 2;
			p->DETBufferSizeC[k] = p->DETBufferSizeInKByte[k] * 1024 / 2;
		} else {
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%u 2/3 DET for plane0, 1/3 for plane1\n", __func__, k);
#endif
			p->DETBufferSizeY[k] = (dml_uint_t)(dml_floor(p->DETBufferSizeInKByte[k] * 1024 * 2 / 3, 1024));
			p->DETBufferSizeC[k] = p->DETBufferSizeInKByte[k] * 1024 - p->DETBufferSizeY[k];
		}

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u SwathHeightY = %u\n", __func__, k, p->SwathHeightY[k]);
		dml_print("DML::%s: k=%u SwathHeightC = %u\n", __func__, k, p->SwathHeightC[k]);
		dml_print("DML::%s: k=%u RoundedUpMaxSwathSizeBytesY = %u\n", __func__, k, RoundedUpMaxSwathSizeBytesY[k]);
		dml_print("DML::%s: k=%u RoundedUpMaxSwathSizeBytesC = %u\n", __func__, k, RoundedUpMaxSwathSizeBytesC[k]);
		dml_print("DML::%s: k=%u RoundedUpSwathSizeBytesY = %u\n", __func__, k, RoundedUpSwathSizeBytesY[k]);
		dml_print("DML::%s: k=%u RoundedUpSwathSizeBytesC = %u\n", __func__, k, RoundedUpSwathSizeBytesC[k]);
		dml_print("DML::%s: k=%u DETBufferSizeInKByte = %u\n", __func__, k, p->DETBufferSizeInKByte[k]);
		dml_print("DML::%s: k=%u DETBufferSizeY = %u\n", __func__, k, p->DETBufferSizeY[k]);
		dml_print("DML::%s: k=%u DETBufferSizeC = %u\n", __func__, k, p->DETBufferSizeC[k]);
		dml_print("DML::%s: k=%u ViewportSizeSupportPerSurface = %u\n", __func__, k, p->ViewportSizeSupportPerSurface[k]);
#endif

	}

	*p->compbuf_reserved_space_64b = 2 * p->PixelChunkSizeInKByte * 1024 / 64;
	if (p->UnboundedRequestEnabled) {
		*p->compbuf_reserved_space_64b = dml_max(*p->compbuf_reserved_space_64b,
				(dml_float_t)(p->ROBBufferSizeInKByte * 1024/64)
				- (dml_float_t)(RoundedUpSwathSizeBytesY[SurfaceDoingUnboundedRequest] * TTUFIFODEPTH / MAXIMUMCOMPRESSION/64));
	}
	*p->compbuf_reserved_space_zs = 2 * p->PixelChunkSizeInKByte * 1024 / 256;
} // CalculateSwathAndDETConfiguration

static void CalculateSwathWidth(
		dml_bool_t ForceSingleDPP,
		dml_uint_t NumberOfActiveSurfaces,
		enum dml_source_format_class SourcePixelFormat[],
		enum dml_rotation_angle SourceScan[],
		dml_bool_t ViewportStationary[],
		dml_uint_t ViewportWidth[],
		dml_uint_t ViewportHeight[],
		dml_uint_t ViewportXStart[],
		dml_uint_t ViewportYStart[],
		dml_uint_t ViewportXStartC[],
		dml_uint_t ViewportYStartC[],
		dml_uint_t SurfaceWidthY[],
		dml_uint_t SurfaceWidthC[],
		dml_uint_t SurfaceHeightY[],
		dml_uint_t SurfaceHeightC[],
		enum dml_odm_mode ODMMode[],
		dml_uint_t BytePerPixY[],
		dml_uint_t BytePerPixC[],
		dml_uint_t Read256BytesBlockHeightY[],
		dml_uint_t Read256BytesBlockHeightC[],
		dml_uint_t Read256BytesBlockWidthY[],
		dml_uint_t Read256BytesBlockWidthC[],
		dml_uint_t BlendingAndTiming[],
		dml_uint_t HActive[],
		dml_float_t HRatio[],
		dml_uint_t DPPPerSurface[],

		// Output
		dml_uint_t SwathWidthSingleDPPY[],
		dml_uint_t SwathWidthSingleDPPC[],
		dml_uint_t SwathWidthY[], // per-pipe
		dml_uint_t SwathWidthC[], // per-pipe
		dml_uint_t MaximumSwathHeightY[],
		dml_uint_t MaximumSwathHeightC[],
		dml_uint_t swath_width_luma_ub[], // per-pipe
		dml_uint_t swath_width_chroma_ub[]) // per-pipe
{
	enum dml_odm_mode   MainSurfaceODMMode;
	dml_uint_t surface_width_ub_l;
	dml_uint_t surface_height_ub_l;
	dml_uint_t surface_width_ub_c = 0;
	dml_uint_t surface_height_ub_c = 0;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: ForceSingleDPP = %u\n", __func__, ForceSingleDPP);
	dml_print("DML::%s: NumberOfActiveSurfaces = %u\n", __func__, NumberOfActiveSurfaces);
#endif

	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (!dml_is_vertical_rotation(SourceScan[k])) {
			SwathWidthSingleDPPY[k] = ViewportWidth[k];
		} else {
			SwathWidthSingleDPPY[k] = ViewportHeight[k];
		}

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u ViewportWidth=%u\n", __func__, k, ViewportWidth[k]);
		dml_print("DML::%s: k=%u ViewportHeight=%u\n", __func__, k, ViewportHeight[k]);
		dml_print("DML::%s: k=%u DPPPerSurface=%u\n", __func__, k, DPPPerSurface[k]);
#endif

		MainSurfaceODMMode = ODMMode[k];
		for (dml_uint_t j = 0; j < NumberOfActiveSurfaces; ++j) {
			if (BlendingAndTiming[k] == j) {
				MainSurfaceODMMode = ODMMode[j];
			}
		}

		if (ForceSingleDPP) {
			SwathWidthY[k] = SwathWidthSingleDPPY[k];
		} else {
			if (MainSurfaceODMMode == dml_odm_mode_combine_4to1) {
				SwathWidthY[k] = (dml_uint_t)(dml_min(SwathWidthSingleDPPY[k], dml_round(HActive[k] / 4.0 * HRatio[k], true)));
			} else if (MainSurfaceODMMode == dml_odm_mode_combine_2to1) {
				SwathWidthY[k] = (dml_uint_t)(dml_min(SwathWidthSingleDPPY[k], dml_round(HActive[k] / 2.0 * HRatio[k], true)));
			} else if (DPPPerSurface[k] == 2) {
				SwathWidthY[k] = SwathWidthSingleDPPY[k] / 2;
			} else {
				SwathWidthY[k] = SwathWidthSingleDPPY[k];
			}
		}

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u HActive=%u\n", __func__, k, HActive[k]);
		dml_print("DML::%s: k=%u HRatio=%f\n", __func__, k, HRatio[k]);
		dml_print("DML::%s: k=%u MainSurfaceODMMode=%u\n", __func__, k, MainSurfaceODMMode);
		dml_print("DML::%s: k=%u SwathWidthSingleDPPY=%u\n", __func__, k, SwathWidthSingleDPPY[k]);
		dml_print("DML::%s: k=%u SwathWidthY=%u\n", __func__, k, SwathWidthY[k]);
#endif

		if (SourcePixelFormat[k] == dml_420_8 || SourcePixelFormat[k] == dml_420_10 || SourcePixelFormat[k] == dml_420_12) {
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

		surface_width_ub_l = (dml_uint_t)dml_ceil(SurfaceWidthY[k], Read256BytesBlockWidthY[k]);
		surface_height_ub_l = (dml_uint_t)dml_ceil(SurfaceHeightY[k], Read256BytesBlockHeightY[k]);

		if (!dml_is_vertical_rotation(SourceScan[k])) {
			MaximumSwathHeightY[k] = Read256BytesBlockHeightY[k];
			MaximumSwathHeightC[k] = Read256BytesBlockHeightC[k];
			if (ViewportStationary[k] && DPPPerSurface[k] == 1) {
				swath_width_luma_ub[k] = (dml_uint_t)(dml_min(surface_width_ub_l, dml_floor(ViewportXStart[k] + SwathWidthY[k] + Read256BytesBlockWidthY[k] - 1, Read256BytesBlockWidthY[k]) - dml_floor(ViewportXStart[k], Read256BytesBlockWidthY[k])));
			} else {
				swath_width_luma_ub[k] = (dml_uint_t)(dml_min(surface_width_ub_l, dml_ceil(SwathWidthY[k] - 1, Read256BytesBlockWidthY[k]) + Read256BytesBlockWidthY[k]));
			}
			if (BytePerPixC[k] > 0) {
				surface_width_ub_c = (dml_uint_t)dml_ceil(SurfaceWidthC[k], Read256BytesBlockWidthC[k]);
				if (ViewportStationary[k] && DPPPerSurface[k] == 1) {
					swath_width_chroma_ub[k] = (dml_uint_t)(dml_min(surface_width_ub_c, dml_floor(ViewportXStartC[k] + SwathWidthC[k] + Read256BytesBlockWidthC[k] - 1, Read256BytesBlockWidthC[k]) - dml_floor(ViewportXStartC[k], Read256BytesBlockWidthC[k])));
				} else {
					swath_width_chroma_ub[k] = (dml_uint_t)(dml_min(surface_width_ub_c, dml_ceil(SwathWidthC[k] - 1, Read256BytesBlockWidthC[k]) + Read256BytesBlockWidthC[k]));
				}
			} else {
				swath_width_chroma_ub[k] = 0;
			}
		} else {
			MaximumSwathHeightY[k] = Read256BytesBlockWidthY[k];
			MaximumSwathHeightC[k] = Read256BytesBlockWidthC[k];

			if (ViewportStationary[k] && DPPPerSurface[k] == 1) {
				swath_width_luma_ub[k] = (dml_uint_t)(dml_min(surface_height_ub_l, dml_floor(ViewportYStart[k] + SwathWidthY[k] + Read256BytesBlockHeightY[k] - 1, Read256BytesBlockHeightY[k]) - dml_floor(ViewportYStart[k], Read256BytesBlockHeightY[k])));
			} else {
				swath_width_luma_ub[k] = (dml_uint_t)(dml_min(surface_height_ub_l, dml_ceil(SwathWidthY[k] - 1, Read256BytesBlockHeightY[k]) + Read256BytesBlockHeightY[k]));
			}
			if (BytePerPixC[k] > 0) {
				surface_height_ub_c = (dml_uint_t)dml_ceil(SurfaceHeightC[k], Read256BytesBlockHeightC[k]);
				if (ViewportStationary[k] && DPPPerSurface[k] == 1) {
					swath_width_chroma_ub[k] = (dml_uint_t)(dml_min(surface_height_ub_c, dml_floor(ViewportYStartC[k] + SwathWidthC[k] + Read256BytesBlockHeightC[k] - 1, Read256BytesBlockHeightC[k]) - dml_floor(ViewportYStartC[k], Read256BytesBlockHeightC[k])));
				} else {
					swath_width_chroma_ub[k] = (dml_uint_t)(dml_min(surface_height_ub_c, dml_ceil(SwathWidthC[k] - 1, Read256BytesBlockHeightC[k]) + Read256BytesBlockHeightC[k]));
				}
			} else {
				swath_width_chroma_ub[k] = 0;
			}
		}

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u surface_width_ub_l=%u\n", __func__, k, surface_width_ub_l);
		dml_print("DML::%s: k=%u surface_height_ub_l=%u\n", __func__, k, surface_height_ub_l);
		dml_print("DML::%s: k=%u surface_width_ub_c=%u\n", __func__, k, surface_width_ub_c);
		dml_print("DML::%s: k=%u surface_height_ub_c=%u\n", __func__, k, surface_height_ub_c);
		dml_print("DML::%s: k=%u Read256BytesBlockWidthY=%u\n", __func__, k, Read256BytesBlockWidthY[k]);
		dml_print("DML::%s: k=%u Read256BytesBlockHeightY=%u\n", __func__, k, Read256BytesBlockHeightY[k]);
		dml_print("DML::%s: k=%u Read256BytesBlockWidthC=%u\n", __func__, k, Read256BytesBlockWidthC[k]);
		dml_print("DML::%s: k=%u Read256BytesBlockHeightC=%u\n", __func__, k, Read256BytesBlockHeightC[k]);
		dml_print("DML::%s: k=%u ViewportStationary=%u\n", __func__, k, ViewportStationary[k]);
		dml_print("DML::%s: k=%u DPPPerSurface=%u\n", __func__, k, DPPPerSurface[k]);
		dml_print("DML::%s: k=%u swath_width_luma_ub=%u\n", __func__, k, swath_width_luma_ub[k]);
		dml_print("DML::%s: k=%u swath_width_chroma_ub=%u\n", __func__, k, swath_width_chroma_ub[k]);
		dml_print("DML::%s: k=%u MaximumSwathHeightY=%u\n", __func__, k, MaximumSwathHeightY[k]);
		dml_print("DML::%s: k=%u MaximumSwathHeightC=%u\n", __func__, k, MaximumSwathHeightC[k]);
#endif

	}
} // CalculateSwathWidth

static  dml_float_t CalculateExtraLatency(
		dml_uint_t RoundTripPingLatencyCycles,
		dml_uint_t ReorderingBytes,
		dml_float_t DCFCLK,
		dml_uint_t TotalNumberOfActiveDPP,
		dml_uint_t PixelChunkSizeInKByte,
		dml_uint_t TotalNumberOfDCCActiveDPP,
		dml_uint_t MetaChunkSize,
		dml_float_t ReturnBW,
		dml_bool_t GPUVMEnable,
		dml_bool_t HostVMEnable,
		dml_uint_t NumberOfActiveSurfaces,
		dml_uint_t NumberOfDPP[],
		dml_uint_t dpte_group_bytes[],
		dml_float_t HostVMInefficiencyFactor,
		dml_uint_t HostVMMinPageSize,
		dml_uint_t HostVMMaxNonCachedPageTableLevels)
{
	dml_float_t ExtraLatencyBytes;
	dml_float_t ExtraLatency;

	ExtraLatencyBytes = CalculateExtraLatencyBytes(
			ReorderingBytes,
			TotalNumberOfActiveDPP,
			PixelChunkSizeInKByte,
			TotalNumberOfDCCActiveDPP,
			MetaChunkSize,
			GPUVMEnable,
			HostVMEnable,
			NumberOfActiveSurfaces,
			NumberOfDPP,
			dpte_group_bytes,
			HostVMInefficiencyFactor,
			HostVMMinPageSize,
			HostVMMaxNonCachedPageTableLevels);

	ExtraLatency = (RoundTripPingLatencyCycles + __DML_ARB_TO_RET_DELAY__) / DCFCLK + ExtraLatencyBytes / ReturnBW;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: RoundTripPingLatencyCycles=%u\n", __func__, RoundTripPingLatencyCycles);
	dml_print("DML::%s: DCFCLK=%f\n", __func__, DCFCLK);
	dml_print("DML::%s: ExtraLatencyBytes=%f\n", __func__, ExtraLatencyBytes);
	dml_print("DML::%s: ReturnBW=%f\n", __func__, ReturnBW);
	dml_print("DML::%s: ExtraLatency=%f\n", __func__, ExtraLatency);
#endif

	return ExtraLatency;
} // CalculateExtraLatency

static dml_uint_t CalculateHostVMDynamicLevels(
									dml_bool_t GPUVMEnable,
									dml_bool_t HostVMEnable,
									dml_uint_t HostVMMinPageSize,
									dml_uint_t HostVMMaxNonCachedPageTableLevels)
{
	dml_uint_t HostVMDynamicLevels = 0;

	if (GPUVMEnable && HostVMEnable) {
		if (HostVMMinPageSize < 2048)
			HostVMDynamicLevels = HostVMMaxNonCachedPageTableLevels;
		else if (HostVMMinPageSize >= 2048 && HostVMMinPageSize < 1048576)
			HostVMDynamicLevels = (dml_uint_t) dml_max(0, (dml_float_t) HostVMMaxNonCachedPageTableLevels - 1);
		else
			HostVMDynamicLevels = (dml_uint_t) dml_max(0, (dml_float_t) HostVMMaxNonCachedPageTableLevels - 2);
	} else {
		HostVMDynamicLevels = 0;
	}
	return HostVMDynamicLevels;
}

static dml_uint_t CalculateExtraLatencyBytes(dml_uint_t ReorderingBytes,
										dml_uint_t TotalNumberOfActiveDPP,
										dml_uint_t PixelChunkSizeInKByte,
										dml_uint_t TotalNumberOfDCCActiveDPP,
										dml_uint_t MetaChunkSize,
										dml_bool_t GPUVMEnable,
										dml_bool_t HostVMEnable,
										dml_uint_t NumberOfActiveSurfaces,
										dml_uint_t NumberOfDPP[],
										dml_uint_t dpte_group_bytes[],
										dml_float_t HostVMInefficiencyFactor,
										dml_uint_t HostVMMinPageSize,
										dml_uint_t HostVMMaxNonCachedPageTableLevels)
{
	dml_uint_t  HostVMDynamicLevels = CalculateHostVMDynamicLevels(GPUVMEnable, HostVMEnable, HostVMMinPageSize, HostVMMaxNonCachedPageTableLevels);
	dml_float_t ret                 = ReorderingBytes + (TotalNumberOfActiveDPP * PixelChunkSizeInKByte + TotalNumberOfDCCActiveDPP * MetaChunkSize) * 1024.0;

	if (GPUVMEnable == true) {
		for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
			ret = ret + NumberOfDPP[k] * dpte_group_bytes[k] * (1 + 8 * HostVMDynamicLevels) * HostVMInefficiencyFactor;
		}
	}
	return (dml_uint_t)(ret);
}

static dml_float_t CalculateUrgentLatency(
		dml_float_t UrgentLatencyPixelDataOnly,
		dml_float_t UrgentLatencyPixelMixedWithVMData,
		dml_float_t UrgentLatencyVMDataOnly,
		dml_bool_t DoUrgentLatencyAdjustment,
		dml_float_t UrgentLatencyAdjustmentFabricClockComponent,
		dml_float_t UrgentLatencyAdjustmentFabricClockReference,
		dml_float_t FabricClock)
{
	dml_float_t   ret;

	ret = dml_max3(UrgentLatencyPixelDataOnly, UrgentLatencyPixelMixedWithVMData, UrgentLatencyVMDataOnly);
	if (DoUrgentLatencyAdjustment == true) {
		ret = ret + UrgentLatencyAdjustmentFabricClockComponent * (UrgentLatencyAdjustmentFabricClockReference / FabricClock - 1);
	}
	return ret;
}

static dml_float_t RequiredDTBCLK(
		dml_bool_t DSCEnable,
		dml_float_t PixelClock,
		enum dml_output_format_class OutputFormat,
		dml_float_t OutputBpp,
		dml_uint_t DSCSlices,
		dml_uint_t HTotal,
		dml_uint_t HActive,
		dml_uint_t AudioRate,
		dml_uint_t AudioLayout)
{
	if (DSCEnable != true) {
		return dml_max(PixelClock / 4.0 * OutputBpp / 24.0, 25.0);
	} else {
		dml_float_t PixelWordRate = PixelClock / (OutputFormat == dml_444 ? 1 : 2);
		dml_float_t HCActive = dml_ceil(DSCSlices * dml_ceil(OutputBpp * dml_ceil(HActive / DSCSlices, 1) / 8.0, 1) / 3.0, 1);
		dml_float_t HCBlank = 64 + 32 * dml_ceil(AudioRate * (AudioLayout == 1 ? 1 : 0.25) * HTotal / (PixelClock * 1000), 1);
		dml_float_t AverageTribyteRate = PixelWordRate * (HCActive + HCBlank) / HTotal;
		dml_float_t HActiveTribyteRate = PixelWordRate * HCActive / HActive;
		return dml_max4(PixelWordRate / 4.0, AverageTribyteRate / 4.0, HActiveTribyteRate / 4.0, 25.0) * 1.002;
	}
}

static void UseMinimumDCFCLK(struct display_mode_lib_scratch_st *scratch, struct UseMinimumDCFCLK_params_st *p)
{
	struct UseMinimumDCFCLK_locals_st *s = &scratch->UseMinimumDCFCLK_locals;

	s->NormalEfficiency = p->PercentOfIdealSDPPortBWReceivedAfterUrgLatency / 100.0;
	for (dml_uint_t j = 0; j < 2; ++j) {


		s->TotalMaxPrefetchFlipDPTERowBandwidth[j] = 0;
		for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
			s->TotalMaxPrefetchFlipDPTERowBandwidth[j] = s->TotalMaxPrefetchFlipDPTERowBandwidth[j] + p->NoOfDPP[j][k] * p->DPTEBytesPerRow[j][k] / (15.75 * p->HTotal[k] / p->PixelClock[k]);
		}

		for (dml_uint_t k = 0; k <= p->NumberOfActiveSurfaces - 1; ++k) {
			s->NoOfDPPState[k] = p->NoOfDPP[j][k];
		}

		s->DPTEBandwidth = s->TotalMaxPrefetchFlipDPTERowBandwidth[j];

		s->DCFCLKRequiredForAverageBandwidth = dml_max(p->ProjectedDCFCLKDeepSleep[j], s->DPTEBandwidth / s->NormalEfficiency / p->ReturnBusWidth);

		s->ExtraLatencyBytes = CalculateExtraLatencyBytes(p->ReorderingBytes, p->TotalNumberOfActiveDPP[j], p->PixelChunkSizeInKByte, p->TotalNumberOfDCCActiveDPP[j],
			p->MetaChunkSize, p->GPUVMEnable, p->HostVMEnable, p->NumberOfActiveSurfaces, s->NoOfDPPState, p->dpte_group_bytes,
												1, p->HostVMMinPageSize, p->HostVMMaxNonCachedPageTableLevels);
		s->ExtraLatencyCycles = p->RoundTripPingLatencyCycles + __DML_ARB_TO_RET_DELAY__ + s->ExtraLatencyBytes / s->NormalEfficiency / p->ReturnBusWidth;
		for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
			dml_float_t DCFCLKCyclesRequiredInPrefetch;
			dml_float_t PrefetchTime;

			s->PixelDCFCLKCyclesRequiredInPrefetch[k] = (p->PrefetchLinesY[j][k] * p->swath_width_luma_ub_all_states[j][k] * p->BytePerPixelY[k] + p->PrefetchLinesC[j][k] * p->swath_width_chroma_ub_all_states[j][k] * p->BytePerPixelC[k]) / s->NormalEfficiency / p->ReturnBusWidth;
			DCFCLKCyclesRequiredInPrefetch = 2 * s->ExtraLatencyCycles / s->NoOfDPPState[k] + p->PDEAndMetaPTEBytesPerFrame[j][k] / s->NormalEfficiency / s->NormalEfficiency / p->ReturnBusWidth * (p->GPUVMMaxPageTableLevels > 2 ? 1 : 0) + 2 * p->DPTEBytesPerRow[j][k] / s->NormalEfficiency / s->NormalEfficiency / p->ReturnBusWidth + 2 * p->MetaRowBytes[j][k] / s->NormalEfficiency / p->ReturnBusWidth + s->PixelDCFCLKCyclesRequiredInPrefetch[k];
			s->PrefetchPixelLinesTime[k] = dml_max(p->PrefetchLinesY[j][k], p->PrefetchLinesC[j][k]) * p->HTotal[k] / p->PixelClock[k];
			s->DynamicMetadataVMExtraLatency[k] = (p->GPUVMEnable == true && p->DynamicMetadataEnable[k] == true && p->DynamicMetadataVMEnabled == true) ? p->UrgLatency * p->GPUVMMaxPageTableLevels * (p->HostVMEnable == true ? p->HostVMMaxNonCachedPageTableLevels + 1 : 1) : 0;

			s->MinimumTWait = CalculateTWait(p->MaxPrefetchMode,
				p->UseMALLForPStateChange[k],
				p->SynchronizeDRRDisplaysForUCLKPStateChangeFinal,
				p->DRRDisplay[k],
				p->DRAMClockChangeLatencyFinal,
				p->FCLKChangeLatency,
				p->UrgLatency,
				p->SREnterPlusExitTime);

			PrefetchTime = (p->MaximumVStartup[j][k] - 1) * p->HTotal[k] / p->PixelClock[k] - s->MinimumTWait - p->UrgLatency * ((p->GPUVMMaxPageTableLevels <= 2 ? p->GPUVMMaxPageTableLevels : p->GPUVMMaxPageTableLevels - 2) *  (p->HostVMEnable == true ? p->HostVMMaxNonCachedPageTableLevels + 1 : 1) - 1) - s->DynamicMetadataVMExtraLatency[k];

			if (PrefetchTime > 0) {
				dml_float_t ExpectedVRatioPrefetch;
				ExpectedVRatioPrefetch = s->PrefetchPixelLinesTime[k] / (PrefetchTime * s->PixelDCFCLKCyclesRequiredInPrefetch[k] / DCFCLKCyclesRequiredInPrefetch);
				s->DCFCLKRequiredForPeakBandwidthPerSurface[k] = s->NoOfDPPState[k] * s->PixelDCFCLKCyclesRequiredInPrefetch[k] / s->PrefetchPixelLinesTime[k] * dml_max(1.0, ExpectedVRatioPrefetch) * dml_max(1.0, ExpectedVRatioPrefetch / 4);
				if (p->HostVMEnable == true || p->ImmediateFlipRequirement == true) {
					s->DCFCLKRequiredForPeakBandwidthPerSurface[k] = s->DCFCLKRequiredForPeakBandwidthPerSurface[k] + s->NoOfDPPState[k] * s->DPTEBandwidth / s->NormalEfficiency / s->NormalEfficiency / p->ReturnBusWidth;
				}
			} else {
				s->DCFCLKRequiredForPeakBandwidthPerSurface[k] = p->DCFCLKPerState;
			}
			if (p->DynamicMetadataEnable[k] == true) {
				dml_float_t TSetupPipe;
				dml_float_t TdmbfPipe;
				dml_float_t TdmsksPipe;
				dml_float_t TdmecPipe;
				dml_float_t AllowedTimeForUrgentExtraLatency;

				CalculateVUpdateAndDynamicMetadataParameters(
					p->MaxInterDCNTileRepeaters,
					p->RequiredDPPCLKPerSurface[j][k],
					p->RequiredDISPCLK[j],
					p->ProjectedDCFCLKDeepSleep[j],
					p->PixelClock[k],
					p->HTotal[k],
					p->VTotal[k] - p->VActive[k],
					p->DynamicMetadataTransmittedBytes[k],
					p->DynamicMetadataLinesBeforeActiveRequired[k],
					p->Interlace[k],
					p->ProgressiveToInterlaceUnitInOPP,

					// Output
					&TSetupPipe,
					&TdmbfPipe,
					&TdmecPipe,
					&TdmsksPipe,
					&s->dummy1,
					&s->dummy2,
					&s->dummy3);

				AllowedTimeForUrgentExtraLatency = p->MaximumVStartup[j][k] * p->HTotal[k] / p->PixelClock[k] - s->MinimumTWait - TSetupPipe - TdmbfPipe - TdmecPipe - TdmsksPipe - s->DynamicMetadataVMExtraLatency[k];
				if (AllowedTimeForUrgentExtraLatency > 0) {
					s->DCFCLKRequiredForPeakBandwidthPerSurface[k] = dml_max(s->DCFCLKRequiredForPeakBandwidthPerSurface[k], s->ExtraLatencyCycles / AllowedTimeForUrgentExtraLatency);
				} else {
					s->DCFCLKRequiredForPeakBandwidthPerSurface[k] = p->DCFCLKPerState;
				}
			}
		}
		s->DCFCLKRequiredForPeakBandwidth = 0;
		for (dml_uint_t k = 0; k <= p->NumberOfActiveSurfaces - 1; ++k) {
			s->DCFCLKRequiredForPeakBandwidth = s->DCFCLKRequiredForPeakBandwidth + s->DCFCLKRequiredForPeakBandwidthPerSurface[k];
		}
		s->MinimumTvmPlus2Tr0 = p->UrgLatency * (p->GPUVMEnable == true ? (p->HostVMEnable == true ? (p->GPUVMMaxPageTableLevels + 2) * (p->HostVMMaxNonCachedPageTableLevels + 1) - 1 : p->GPUVMMaxPageTableLevels + 1) : 0);
		for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
			dml_float_t MaximumTvmPlus2Tr0PlusTsw;
			MaximumTvmPlus2Tr0PlusTsw = (p->MaximumVStartup[j][k] - 2) * p->HTotal[k] / p->PixelClock[k] - s->MinimumTWait - s->DynamicMetadataVMExtraLatency[k];
			if (MaximumTvmPlus2Tr0PlusTsw <= s->MinimumTvmPlus2Tr0 + s->PrefetchPixelLinesTime[k] / 4) {
				s->DCFCLKRequiredForPeakBandwidth = p->DCFCLKPerState;
			} else {
				s->DCFCLKRequiredForPeakBandwidth = dml_max3(s->DCFCLKRequiredForPeakBandwidth,
														2 * s->ExtraLatencyCycles / (MaximumTvmPlus2Tr0PlusTsw - s->MinimumTvmPlus2Tr0 - s->PrefetchPixelLinesTime[k] / 4),
														(2 * s->ExtraLatencyCycles + s->PixelDCFCLKCyclesRequiredInPrefetch[k]) / (MaximumTvmPlus2Tr0PlusTsw - s->MinimumTvmPlus2Tr0));
			}
		}
		p->DCFCLKState[j] = dml_min(p->DCFCLKPerState, 1.05 * dml_max(s->DCFCLKRequiredForAverageBandwidth, s->DCFCLKRequiredForPeakBandwidth));
	}
}


static dml_bool_t UnboundedRequest(enum dml_unbounded_requesting_policy UseUnboundedRequestingFinal,
						dml_uint_t TotalNumberOfActiveDPP,
						dml_bool_t NoChromaOrLinear,
						enum dml_output_encoder_class Output)
{
	dml_bool_t ret_val = false;

	ret_val = (UseUnboundedRequestingFinal != dml_unbounded_requesting_disable
			&& TotalNumberOfActiveDPP == 1 && NoChromaOrLinear);
	if (UseUnboundedRequestingFinal == dml_unbounded_requesting_edp_only && Output != dml_edp) {
		ret_val = false;
	}
	return (ret_val);
}

static void CalculateSurfaceSizeInMall(
		dml_uint_t NumberOfActiveSurfaces,
		dml_uint_t MALLAllocatedForDCN,
		enum dml_use_mall_for_static_screen_mode UseMALLForStaticScreen[],
		dml_bool_t DCCEnable[],
		dml_bool_t ViewportStationary[],
		dml_uint_t ViewportXStartY[],
		dml_uint_t ViewportYStartY[],
		dml_uint_t ViewportXStartC[],
		dml_uint_t ViewportYStartC[],
		dml_uint_t ViewportWidthY[],
		dml_uint_t ViewportHeightY[],
		dml_uint_t BytesPerPixelY[],
		dml_uint_t ViewportWidthC[],
		dml_uint_t ViewportHeightC[],
		dml_uint_t BytesPerPixelC[],
		dml_uint_t SurfaceWidthY[],
		dml_uint_t SurfaceWidthC[],
		dml_uint_t SurfaceHeightY[],
		dml_uint_t SurfaceHeightC[],
		dml_uint_t Read256BytesBlockWidthY[],
		dml_uint_t Read256BytesBlockWidthC[],
		dml_uint_t Read256BytesBlockHeightY[],
		dml_uint_t Read256BytesBlockHeightC[],
		dml_uint_t ReadBlockWidthY[],
		dml_uint_t ReadBlockWidthC[],
		dml_uint_t ReadBlockHeightY[],
		dml_uint_t ReadBlockHeightC[],

		// Output
		dml_uint_t SurfaceSizeInMALL[],
		dml_bool_t *ExceededMALLSize)
{
	dml_uint_t TotalSurfaceSizeInMALL  = 0;

	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (ViewportStationary[k]) {
			SurfaceSizeInMALL[k] = (dml_uint_t)(dml_min(dml_ceil(SurfaceWidthY[k], ReadBlockWidthY[k]), dml_floor(ViewportXStartY[k] + ViewportWidthY[k] + ReadBlockWidthY[k] - 1, ReadBlockWidthY[k]) - dml_floor(ViewportXStartY[k], ReadBlockWidthY[k])) *
									dml_min(dml_ceil(SurfaceHeightY[k], ReadBlockHeightY[k]), dml_floor(ViewportYStartY[k] + ViewportHeightY[k] + ReadBlockHeightY[k] - 1, ReadBlockHeightY[k]) - dml_floor(ViewportYStartY[k], ReadBlockHeightY[k])) *
									BytesPerPixelY[k]);

			if (ReadBlockWidthC[k] > 0) {
				SurfaceSizeInMALL[k] = (dml_uint_t)(SurfaceSizeInMALL[k] +
										dml_min(dml_ceil(SurfaceWidthC[k], ReadBlockWidthC[k]), dml_floor(ViewportXStartC[k] + ViewportWidthC[k] + ReadBlockWidthC[k] - 1, ReadBlockWidthC[k]) - dml_floor(ViewportXStartC[k], ReadBlockWidthC[k])) *
										dml_min(dml_ceil(SurfaceHeightC[k], ReadBlockHeightC[k]), dml_floor(ViewportYStartC[k] + ViewportHeightC[k] + ReadBlockHeightC[k] - 1, ReadBlockHeightC[k]) - dml_floor(ViewportYStartC[k], ReadBlockHeightC[k])) * BytesPerPixelC[k]);
			}
			if (DCCEnable[k] == true) {
				SurfaceSizeInMALL[k] = (dml_uint_t)(SurfaceSizeInMALL[k] +
					dml_min(dml_ceil(SurfaceWidthY[k], 8 * Read256BytesBlockWidthY[k]), dml_floor(ViewportXStartY[k] + ViewportWidthY[k] + 8 * Read256BytesBlockWidthY[k] - 1, 8 * Read256BytesBlockWidthY[k]) - dml_floor(ViewportXStartY[k], 8 * Read256BytesBlockWidthY[k])) *
					dml_min(dml_ceil(SurfaceHeightY[k], 8 * Read256BytesBlockHeightY[k]), dml_floor(ViewportYStartY[k] + ViewportHeightY[k] + 8 * Read256BytesBlockHeightY[k] - 1, 8 * Read256BytesBlockHeightY[k]) - dml_floor(ViewportYStartY[k], 8 * Read256BytesBlockHeightY[k])) * BytesPerPixelY[k] / 256);
				if (Read256BytesBlockWidthC[k] > 0) {
					SurfaceSizeInMALL[k] = (dml_uint_t)(SurfaceSizeInMALL[k] +
						dml_min(dml_ceil(SurfaceWidthC[k], 8 * Read256BytesBlockWidthC[k]), dml_floor(ViewportXStartC[k] + ViewportWidthC[k] + 8 * Read256BytesBlockWidthC[k] - 1, 8 * Read256BytesBlockWidthC[k]) - dml_floor(ViewportXStartC[k], 8 * Read256BytesBlockWidthC[k])) *
						dml_min(dml_ceil(SurfaceHeightC[k], 8 * Read256BytesBlockHeightC[k]), dml_floor(ViewportYStartC[k] + ViewportHeightC[k] + 8 * Read256BytesBlockHeightC[k] - 1, 8 * Read256BytesBlockHeightC[k]) - dml_floor(ViewportYStartC[k], 8 * Read256BytesBlockHeightC[k])) * BytesPerPixelC[k] / 256);
				}
			}
		} else {
			SurfaceSizeInMALL[k] = (dml_uint_t)(dml_ceil(dml_min(SurfaceWidthY[k], ViewportWidthY[k] + ReadBlockWidthY[k] - 1), ReadBlockWidthY[k]) * dml_ceil(dml_min(SurfaceHeightY[k], ViewportHeightY[k] + ReadBlockHeightY[k] - 1), ReadBlockHeightY[k]) * BytesPerPixelY[k]);
			if (ReadBlockWidthC[k] > 0) {
				SurfaceSizeInMALL[k] = (dml_uint_t)(SurfaceSizeInMALL[k] +
										dml_ceil(dml_min(SurfaceWidthC[k], ViewportWidthC[k] + ReadBlockWidthC[k] - 1), ReadBlockWidthC[k]) *
										dml_ceil(dml_min(SurfaceHeightC[k], ViewportHeightC[k] + ReadBlockHeightC[k] - 1), ReadBlockHeightC[k]) * BytesPerPixelC[k]);
		}
		if (DCCEnable[k] == true) {
			SurfaceSizeInMALL[k] = (dml_uint_t)(SurfaceSizeInMALL[k] +
					dml_ceil(dml_min(SurfaceWidthY[k], ViewportWidthY[k] + 8 * Read256BytesBlockWidthY[k] - 1), 8 * Read256BytesBlockWidthY[k]) *
					dml_ceil(dml_min(SurfaceHeightY[k], ViewportHeightY[k] + 8 * Read256BytesBlockHeightY[k] - 1), 8 * Read256BytesBlockHeightY[k]) * BytesPerPixelY[k] / 256);

				if (Read256BytesBlockWidthC[k] > 0) {
					SurfaceSizeInMALL[k] = (dml_uint_t)(SurfaceSizeInMALL[k] +
						dml_ceil(dml_min(SurfaceWidthC[k], ViewportWidthC[k] + 8 * Read256BytesBlockWidthC[k] - 1), 8 * Read256BytesBlockWidthC[k]) *
						dml_ceil(dml_min(SurfaceHeightC[k], ViewportHeightC[k] + 8 * Read256BytesBlockHeightC[k] - 1), 8 * Read256BytesBlockHeightC[k]) * BytesPerPixelC[k] / 256);
				}
			}
		}
	}

	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (UseMALLForStaticScreen[k] == dml_use_mall_static_screen_enable)
			TotalSurfaceSizeInMALL = TotalSurfaceSizeInMALL + SurfaceSizeInMALL[k];
	}
	*ExceededMALLSize = (TotalSurfaceSizeInMALL > MALLAllocatedForDCN * 1024 * 1024);
} // CalculateSurfaceSizeInMall

static void CalculateDETBufferSize(
						dml_uint_t DETSizeOverride[],
						enum dml_use_mall_for_pstate_change_mode UseMALLForPStateChange[],
						dml_bool_t ForceSingleDPP,
						dml_uint_t NumberOfActiveSurfaces,
						dml_bool_t UnboundedRequestEnabled,
						dml_uint_t nomDETInKByte,
						dml_uint_t MaxTotalDETInKByte,
						dml_uint_t ConfigReturnBufferSizeInKByte,
						dml_uint_t MinCompressedBufferSizeInKByte,
						dml_uint_t ConfigReturnBufferSegmentSizeInkByte,
						dml_uint_t CompressedBufferSegmentSizeInkByteFinal,
						enum dml_source_format_class SourcePixelFormat[],
						dml_float_t ReadBandwidthLuma[],
						dml_float_t ReadBandwidthChroma[],
						dml_uint_t RoundedUpMaxSwathSizeBytesY[],
						dml_uint_t RoundedUpMaxSwathSizeBytesC[],
						dml_uint_t DPPPerSurface[],
						// Output
						dml_uint_t DETBufferSizeInKByte[],
						dml_uint_t *CompressedBufferSizeInkByte)
{
	dml_uint_t DETBufferSizePoolInKByte;
	dml_uint_t NextDETBufferPieceInKByte;
	dml_bool_t DETPieceAssignedToThisSurfaceAlready[__DML_NUM_PLANES__];
	dml_bool_t NextPotentialSurfaceToAssignDETPieceFound;
	dml_uint_t NextSurfaceToAssignDETPiece;
	dml_float_t TotalBandwidth;
	dml_float_t BandwidthOfSurfacesNotAssignedDETPiece;
	dml_uint_t max_minDET;
	dml_uint_t minDET;
	dml_uint_t minDET_pipe;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: ForceSingleDPP = %u\n", __func__, ForceSingleDPP);
	dml_print("DML::%s: nomDETInKByte = %u\n", __func__, nomDETInKByte);
	dml_print("DML::%s: NumberOfActiveSurfaces = %u\n", __func__, NumberOfActiveSurfaces);
	dml_print("DML::%s: UnboundedRequestEnabled = %u\n", __func__, UnboundedRequestEnabled);
	dml_print("DML::%s: MaxTotalDETInKByte = %u\n", __func__, MaxTotalDETInKByte);
	dml_print("DML::%s: ConfigReturnBufferSizeInKByte = %u\n", __func__, ConfigReturnBufferSizeInKByte);
	dml_print("DML::%s: MinCompressedBufferSizeInKByte = %u\n", __func__, MinCompressedBufferSizeInKByte);
	dml_print("DML::%s: CompressedBufferSegmentSizeInkByteFinal = %u\n", __func__, CompressedBufferSegmentSizeInkByteFinal);
#endif

	// Note: Will use default det size if that fits 2 swaths
	if (UnboundedRequestEnabled) {
		if (DETSizeOverride[0] > 0) {
			DETBufferSizeInKByte[0] = DETSizeOverride[0];
		} else {
			DETBufferSizeInKByte[0] = (dml_uint_t) dml_max(128.0, dml_ceil(2.0 * ((dml_float_t) RoundedUpMaxSwathSizeBytesY[0] + (dml_float_t) RoundedUpMaxSwathSizeBytesC[0]) / 1024.0, ConfigReturnBufferSegmentSizeInkByte));
		}
		*CompressedBufferSizeInkByte = ConfigReturnBufferSizeInKByte - DETBufferSizeInKByte[0];
	} else {
		DETBufferSizePoolInKByte = MaxTotalDETInKByte;
		for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
			DETBufferSizeInKByte[k] = 0;
			if (SourcePixelFormat[k] == dml_420_8 || SourcePixelFormat[k] == dml_420_10 || SourcePixelFormat[k] == dml_420_12) {
				max_minDET = nomDETInKByte - ConfigReturnBufferSegmentSizeInkByte;
			} else {
				max_minDET = nomDETInKByte;
			}
			minDET = 128;
			minDET_pipe = 0;

			// add DET resource until can hold 2 full swaths
			while (minDET <= max_minDET && minDET_pipe == 0) {
				if (2.0 * ((dml_float_t) RoundedUpMaxSwathSizeBytesY[k] + (dml_float_t) RoundedUpMaxSwathSizeBytesC[k]) / 1024.0 <= minDET)
					minDET_pipe = minDET;
				minDET = minDET + ConfigReturnBufferSegmentSizeInkByte;
			}

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%u minDET = %u\n", __func__, k, minDET);
			dml_print("DML::%s: k=%u max_minDET = %u\n", __func__, k, max_minDET);
			dml_print("DML::%s: k=%u minDET_pipe = %u\n", __func__, k, minDET_pipe);
			dml_print("DML::%s: k=%u RoundedUpMaxSwathSizeBytesY = %u\n", __func__, k, RoundedUpMaxSwathSizeBytesY[k]);
			dml_print("DML::%s: k=%u RoundedUpMaxSwathSizeBytesC = %u\n", __func__, k, RoundedUpMaxSwathSizeBytesC[k]);
#endif

			if (minDET_pipe == 0) {
				minDET_pipe = (dml_uint_t)(dml_max(128, dml_ceil(((dml_float_t)RoundedUpMaxSwathSizeBytesY[k] + (dml_float_t)RoundedUpMaxSwathSizeBytesC[k]) / 1024.0, ConfigReturnBufferSegmentSizeInkByte)));
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: k=%u minDET_pipe = %u (assume each plane take half DET)\n", __func__, k, minDET_pipe);
#endif
			}

			if (UseMALLForPStateChange[k] == dml_use_mall_pstate_change_phantom_pipe) {
				DETBufferSizeInKByte[k] = 0;
			} else if (DETSizeOverride[k] > 0) {
				DETBufferSizeInKByte[k] = DETSizeOverride[k];
				DETBufferSizePoolInKByte = DETBufferSizePoolInKByte - (ForceSingleDPP ? 1 : DPPPerSurface[k]) * DETSizeOverride[k];
			} else if ((ForceSingleDPP ? 1 : DPPPerSurface[k]) * minDET_pipe <= DETBufferSizePoolInKByte) {
				DETBufferSizeInKByte[k] = minDET_pipe;
				DETBufferSizePoolInKByte = DETBufferSizePoolInKByte - (ForceSingleDPP ? 1 : DPPPerSurface[k]) * minDET_pipe;
			}

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%u DPPPerSurface = %u\n", __func__, k, DPPPerSurface[k]);
			dml_print("DML::%s: k=%u DETSizeOverride = %u\n", __func__, k, DETSizeOverride[k]);
			dml_print("DML::%s: k=%u DETBufferSizeInKByte = %u\n", __func__, k, DETBufferSizeInKByte[k]);
			dml_print("DML::%s: DETBufferSizePoolInKByte = %u\n", __func__, DETBufferSizePoolInKByte);
#endif
		}

		TotalBandwidth = 0;
		for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
			if (UseMALLForPStateChange[k] != dml_use_mall_pstate_change_phantom_pipe)
				TotalBandwidth = TotalBandwidth + ReadBandwidthLuma[k] + ReadBandwidthChroma[k];
		}
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: --- Before bandwidth adjustment ---\n", __func__);
		for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
			dml_print("DML::%s: k=%u DETBufferSizeInKByte   = %u\n", __func__, k, DETBufferSizeInKByte[k]);
		}
		dml_print("DML::%s: --- DET allocation with bandwidth ---\n", __func__);
#endif
		dml_print("DML::%s: TotalBandwidth = %f\n", __func__, TotalBandwidth);
		BandwidthOfSurfacesNotAssignedDETPiece = TotalBandwidth;
		for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {

			if (UseMALLForPStateChange[k] == dml_use_mall_pstate_change_phantom_pipe) {
				DETPieceAssignedToThisSurfaceAlready[k] = true;
			} else if (DETSizeOverride[k] > 0 || (((dml_float_t) (ForceSingleDPP ? 1 : DPPPerSurface[k]) * (dml_float_t) DETBufferSizeInKByte[k] / (dml_float_t) MaxTotalDETInKByte) >= ((ReadBandwidthLuma[k] + ReadBandwidthChroma[k]) / TotalBandwidth))) {
				DETPieceAssignedToThisSurfaceAlready[k] = true;
				BandwidthOfSurfacesNotAssignedDETPiece = BandwidthOfSurfacesNotAssignedDETPiece - ReadBandwidthLuma[k] - ReadBandwidthChroma[k];
			} else {
				DETPieceAssignedToThisSurfaceAlready[k] = false;
			}
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%u DETPieceAssignedToThisSurfaceAlready = %u\n", __func__, k, DETPieceAssignedToThisSurfaceAlready[k]);
			dml_print("DML::%s: k=%u BandwidthOfSurfacesNotAssignedDETPiece = %f\n", __func__, k, BandwidthOfSurfacesNotAssignedDETPiece);
#endif
		}

		for (dml_uint_t j = 0; j < NumberOfActiveSurfaces; ++j) {
			NextPotentialSurfaceToAssignDETPieceFound = false;
			NextSurfaceToAssignDETPiece = 0;

			for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: j=%u k=%u, ReadBandwidthLuma[k] = %f\n", __func__, j, k, ReadBandwidthLuma[k]);
				dml_print("DML::%s: j=%u k=%u, ReadBandwidthChroma[k] = %f\n", __func__, j, k, ReadBandwidthChroma[k]);
				dml_print("DML::%s: j=%u k=%u, ReadBandwidthLuma[Next] = %f\n", __func__, j, k, ReadBandwidthLuma[NextSurfaceToAssignDETPiece]);
				dml_print("DML::%s: j=%u k=%u, ReadBandwidthChroma[Next] = %f\n", __func__, j, k, ReadBandwidthChroma[NextSurfaceToAssignDETPiece]);
				dml_print("DML::%s: j=%u k=%u, NextSurfaceToAssignDETPiece = %u\n", __func__, j, k, NextSurfaceToAssignDETPiece);
#endif
				if (!DETPieceAssignedToThisSurfaceAlready[k] && (!NextPotentialSurfaceToAssignDETPieceFound ||
					ReadBandwidthLuma[k] + ReadBandwidthChroma[k] < ReadBandwidthLuma[NextSurfaceToAssignDETPiece] + ReadBandwidthChroma[NextSurfaceToAssignDETPiece])) {
					NextSurfaceToAssignDETPiece = k;
					NextPotentialSurfaceToAssignDETPieceFound = true;
				}
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: j=%u k=%u, DETPieceAssignedToThisSurfaceAlready = %u\n", __func__, j, k, DETPieceAssignedToThisSurfaceAlready[k]);
				dml_print("DML::%s: j=%u k=%u, NextPotentialSurfaceToAssignDETPieceFound = %u\n", __func__, j, k, NextPotentialSurfaceToAssignDETPieceFound);
#endif
			}

			if (NextPotentialSurfaceToAssignDETPieceFound) {
				// Note: To show the banker's rounding behavior in VBA and also the fact that the DET buffer size varies due to precision issue
				//
				//dml_float_t tmp1 =  ((dml_float_t) DETBufferSizePoolInKByte * (ReadBandwidthLuma[NextSurfaceToAssignDETPiece] + ReadBandwidthChroma[NextSurfaceToAssignDETPiece]) / BandwidthOfSurfacesNotAssignedDETPiece /
				//                         ((ForceSingleDPP ? 1 : DPPPerSurface[NextSurfaceToAssignDETPiece]) * 64.0));
				//dml_float_t tmp2 =  dml_round((dml_float_t) DETBufferSizePoolInKByte * (ReadBandwidthLuma[NextSurfaceToAssignDETPiece] + ReadBandwidthChroma[NextSurfaceToAssignDETPiece]) / BandwidthOfSurfacesNotAssignedDETPiece /
				//                         ((ForceSingleDPP ? 1 : DPPPerSurface[NextSurfaceToAssignDETPiece]) * 64.0));
				//
				//dml_print("DML::%s: j=%u, tmp1 = %f\n", __func__, j, tmp1);
				//dml_print("DML::%s: j=%u, tmp2 = %f\n", __func__, j, tmp2);

				NextDETBufferPieceInKByte = (dml_uint_t)(dml_min(
											dml_round((dml_float_t) DETBufferSizePoolInKByte * (ReadBandwidthLuma[NextSurfaceToAssignDETPiece] + ReadBandwidthChroma[NextSurfaceToAssignDETPiece]) / BandwidthOfSurfacesNotAssignedDETPiece /
												((ForceSingleDPP ? 1 : DPPPerSurface[NextSurfaceToAssignDETPiece]) * ConfigReturnBufferSegmentSizeInkByte), true)
												* (ForceSingleDPP ? 1 : DPPPerSurface[NextSurfaceToAssignDETPiece]) * ConfigReturnBufferSegmentSizeInkByte,
											dml_floor((dml_float_t) DETBufferSizePoolInKByte, (ForceSingleDPP ? 1 : DPPPerSurface[NextSurfaceToAssignDETPiece]) * ConfigReturnBufferSegmentSizeInkByte)));

#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: j=%u, DETBufferSizePoolInKByte = %u\n", __func__, j, DETBufferSizePoolInKByte);
				dml_print("DML::%s: j=%u, NextSurfaceToAssignDETPiece = %u\n", __func__, j, NextSurfaceToAssignDETPiece);
				dml_print("DML::%s: j=%u, ReadBandwidthLuma[%u] = %f\n", __func__, j, NextSurfaceToAssignDETPiece, ReadBandwidthLuma[NextSurfaceToAssignDETPiece]);
				dml_print("DML::%s: j=%u, ReadBandwidthChroma[%u] = %f\n", __func__, j, NextSurfaceToAssignDETPiece, ReadBandwidthChroma[NextSurfaceToAssignDETPiece]);
				dml_print("DML::%s: j=%u, BandwidthOfSurfacesNotAssignedDETPiece = %f\n", __func__, j, BandwidthOfSurfacesNotAssignedDETPiece);
				dml_print("DML::%s: j=%u, NextDETBufferPieceInKByte = %u\n", __func__, j, NextDETBufferPieceInKByte);
				dml_print("DML::%s: j=%u, DETBufferSizeInKByte[%u] increases from %u ", __func__, j, NextSurfaceToAssignDETPiece, DETBufferSizeInKByte[NextSurfaceToAssignDETPiece]);
#endif

				DETBufferSizeInKByte[NextSurfaceToAssignDETPiece] = DETBufferSizeInKByte[NextSurfaceToAssignDETPiece] + NextDETBufferPieceInKByte / (ForceSingleDPP ? 1 : DPPPerSurface[NextSurfaceToAssignDETPiece]);
#ifdef __DML_VBA_DEBUG__
				dml_print("to %u\n", DETBufferSizeInKByte[NextSurfaceToAssignDETPiece]);
#endif

				DETBufferSizePoolInKByte = DETBufferSizePoolInKByte - NextDETBufferPieceInKByte;
				DETPieceAssignedToThisSurfaceAlready[NextSurfaceToAssignDETPiece] = true;
				BandwidthOfSurfacesNotAssignedDETPiece = BandwidthOfSurfacesNotAssignedDETPiece - (ReadBandwidthLuma[NextSurfaceToAssignDETPiece] + ReadBandwidthChroma[NextSurfaceToAssignDETPiece]);
			}
		}
		*CompressedBufferSizeInkByte = MinCompressedBufferSizeInKByte;
	}
	*CompressedBufferSizeInkByte = *CompressedBufferSizeInkByte * CompressedBufferSegmentSizeInkByteFinal / ConfigReturnBufferSegmentSizeInkByte;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: --- After bandwidth adjustment ---\n", __func__);
	dml_print("DML::%s: CompressedBufferSizeInkByte = %u\n", __func__, *CompressedBufferSizeInkByte);
	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		dml_print("DML::%s: k=%u DETBufferSizeInKByte = %u (TotalReadBandWidth=%f)\n", __func__, k, DETBufferSizeInKByte[k], ReadBandwidthLuma[k] + ReadBandwidthChroma[k]);
	}
#endif
} // CalculateDETBufferSize


/// @brief Calculate the bound for return buffer sizing
static void CalculateMaxDETAndMinCompressedBufferSize(
		dml_uint_t  ConfigReturnBufferSizeInKByte,
		dml_uint_t  ConfigReturnBufferSegmentSizeInKByte,
		dml_uint_t  ROBBufferSizeInKByte,
		dml_uint_t MaxNumDPP,
		dml_bool_t nomDETInKByteOverrideEnable, // VBA_DELTA, allow DV to override default DET size
		dml_uint_t nomDETInKByteOverrideValue,  // VBA_DELTA

		// Output
		dml_uint_t *MaxTotalDETInKByte,
		dml_uint_t *nomDETInKByte,
		dml_uint_t *MinCompressedBufferSizeInKByte)
{
	*MaxTotalDETInKByte = ConfigReturnBufferSizeInKByte - ConfigReturnBufferSegmentSizeInKByte;
	*nomDETInKByte = (dml_uint_t)(dml_floor((dml_float_t) *MaxTotalDETInKByte / (dml_float_t) MaxNumDPP, ConfigReturnBufferSegmentSizeInKByte));
	*MinCompressedBufferSizeInKByte = ConfigReturnBufferSizeInKByte - *MaxTotalDETInKByte;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: ConfigReturnBufferSizeInKByte = %u\n", __func__, ConfigReturnBufferSizeInKByte);
	dml_print("DML::%s: ROBBufferSizeInKByte = %u\n", __func__, ROBBufferSizeInKByte);
	dml_print("DML::%s: MaxNumDPP = %u\n", __func__, MaxNumDPP);
	dml_print("DML::%s: MaxTotalDETInKByte = %u\n", __func__, *MaxTotalDETInKByte);
	dml_print("DML::%s: nomDETInKByte = %u\n", __func__, *nomDETInKByte);
	dml_print("DML::%s: MinCompressedBufferSizeInKByte = %u\n", __func__, *MinCompressedBufferSizeInKByte);
#endif

	if (nomDETInKByteOverrideEnable) {
		*nomDETInKByte = nomDETInKByteOverrideValue;
		dml_print("DML::%s: nomDETInKByte = %u (overrided)\n", __func__, *nomDETInKByte);
	}
} // CalculateMaxDETAndMinCompressedBufferSize

/// @brief Calculate all the RQ request attributes, like row height and # swath
static void CalculateVMRowAndSwath(struct display_mode_lib_scratch_st *scratch,
		struct CalculateVMRowAndSwath_params_st *p)
{
	struct CalculateVMRowAndSwath_locals_st *s = &scratch->CalculateVMRowAndSwath_locals;

	s->HostVMDynamicLevels = CalculateHostVMDynamicLevels(p->GPUVMEnable, p->HostVMEnable, p->HostVMMinPageSize, p->HostVMMaxNonCachedPageTableLevels);

	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (p->HostVMEnable == true) {
			p->vm_group_bytes[k] = 512;
			p->dpte_group_bytes[k] = 512;
		} else if (p->GPUVMEnable == true) {
			p->vm_group_bytes[k] = 2048;
			if (p->GPUVMMinPageSizeKBytes[k] >= 64 && dml_is_vertical_rotation(p->myPipe[k].SourceScan)) {
				p->dpte_group_bytes[k] = 512;
			} else {
				p->dpte_group_bytes[k] = 2048;
			}
		} else {
			p->vm_group_bytes[k] = 0;
			p->dpte_group_bytes[k] = 0;
		}

		if (p->myPipe[k].SourcePixelFormat == dml_420_8 || p->myPipe[k].SourcePixelFormat == dml_420_10 ||
			p->myPipe[k].SourcePixelFormat == dml_420_12 || p->myPipe[k].SourcePixelFormat == dml_rgbe_alpha) {
			if ((p->myPipe[k].SourcePixelFormat == dml_420_10 || p->myPipe[k].SourcePixelFormat == dml_420_12) && !dml_is_vertical_rotation(p->myPipe[k].SourceScan)) {
				s->PTEBufferSizeInRequestsForLuma[k] = (p->PTEBufferSizeInRequestsLuma + p->PTEBufferSizeInRequestsChroma) / 2;
				s->PTEBufferSizeInRequestsForChroma[k] = s->PTEBufferSizeInRequestsForLuma[k];
			} else {
				s->PTEBufferSizeInRequestsForLuma[k] = p->PTEBufferSizeInRequestsLuma;
				s->PTEBufferSizeInRequestsForChroma[k] = p->PTEBufferSizeInRequestsChroma;
			}

			s->PDEAndMetaPTEBytesFrameC = CalculateVMAndRowBytes(
				p->myPipe[k].ViewportStationary,
				p->myPipe[k].DCCEnable,
				p->myPipe[k].DPPPerSurface,
				p->myPipe[k].BlockHeight256BytesC,
				p->myPipe[k].BlockWidth256BytesC,
				p->myPipe[k].SourcePixelFormat,
				p->myPipe[k].SurfaceTiling,
				p->myPipe[k].BytePerPixelC,
				p->myPipe[k].SourceScan,
				p->SwathWidthC[k],
				p->myPipe[k].ViewportHeightChroma,
				p->myPipe[k].ViewportXStartC,
				p->myPipe[k].ViewportYStartC,
				p->GPUVMEnable,
				p->GPUVMMaxPageTableLevels,
				p->GPUVMMinPageSizeKBytes[k],
				s->PTEBufferSizeInRequestsForChroma[k],
				p->myPipe[k].PitchC,
				p->myPipe[k].DCCMetaPitchC,
				p->myPipe[k].BlockWidthC,
				p->myPipe[k].BlockHeightC,

				// Output
				&s->MetaRowByteC[k],
				&s->PixelPTEBytesPerRowC[k],
				&s->PixelPTEBytesPerRowStorageC[k],
				&p->dpte_row_width_chroma_ub[k],
				&p->dpte_row_height_chroma[k],
				&p->dpte_row_height_linear_chroma[k],
				&s->PixelPTEBytesPerRowC_one_row_per_frame[k],
				&s->dpte_row_width_chroma_ub_one_row_per_frame[k],
				&s->dpte_row_height_chroma_one_row_per_frame[k],
				&p->meta_req_width_chroma[k],
				&p->meta_req_height_chroma[k],
				&p->meta_row_width_chroma[k],
				&p->meta_row_height_chroma[k],
				&p->PixelPTEReqWidthC[k],
				&p->PixelPTEReqHeightC[k],
				&p->PTERequestSizeC[k],
				&p->dpde0_bytes_per_frame_ub_c[k],
				&p->meta_pte_bytes_per_frame_ub_c[k]);

			p->PrefetchSourceLinesC[k] = CalculatePrefetchSourceLines (
				p->myPipe[k].VRatioChroma,
				p->myPipe[k].VTapsChroma,
				p->myPipe[k].InterlaceEnable,
				p->myPipe[k].ProgressiveToInterlaceUnitInOPP,
				p->myPipe[k].SwathHeightC,
				p->myPipe[k].SourceScan,
				p->myPipe[k].ViewportStationary,
				p->SwathWidthC[k],
				p->myPipe[k].ViewportHeightChroma,
				p->myPipe[k].ViewportXStartC,
				p->myPipe[k].ViewportYStartC,

				// Output
				&p->VInitPreFillC[k],
				&p->MaxNumSwathC[k]);
		} else {
			s->PTEBufferSizeInRequestsForLuma[k] = p->PTEBufferSizeInRequestsLuma + p->PTEBufferSizeInRequestsChroma;
			s->PTEBufferSizeInRequestsForChroma[k] = 0;
			s->PixelPTEBytesPerRowC[k] = 0;
			s->PixelPTEBytesPerRowStorageC[k] = 0;
			s->PDEAndMetaPTEBytesFrameC = 0;
			s->MetaRowByteC[k] = 0;
			p->MaxNumSwathC[k] = 0;
			p->PrefetchSourceLinesC[k] = 0;
			s->dpte_row_height_chroma_one_row_per_frame[k] = 0;
			s->dpte_row_width_chroma_ub_one_row_per_frame[k] = 0;
			s->PixelPTEBytesPerRowC_one_row_per_frame[k] = 0;
		}

		s->PDEAndMetaPTEBytesFrameY = CalculateVMAndRowBytes(
			p->myPipe[k].ViewportStationary,
			p->myPipe[k].DCCEnable,
			p->myPipe[k].DPPPerSurface,
			p->myPipe[k].BlockHeight256BytesY,
			p->myPipe[k].BlockWidth256BytesY,
			p->myPipe[k].SourcePixelFormat,
			p->myPipe[k].SurfaceTiling,
			p->myPipe[k].BytePerPixelY,
			p->myPipe[k].SourceScan,
			p->SwathWidthY[k],
			p->myPipe[k].ViewportHeight,
			p->myPipe[k].ViewportXStart,
			p->myPipe[k].ViewportYStart,
			p->GPUVMEnable,
			p->GPUVMMaxPageTableLevels,
			p->GPUVMMinPageSizeKBytes[k],
			s->PTEBufferSizeInRequestsForLuma[k],
			p->myPipe[k].PitchY,
			p->myPipe[k].DCCMetaPitchY,
			p->myPipe[k].BlockWidthY,
			p->myPipe[k].BlockHeightY,

			// Output
			&s->MetaRowByteY[k],
			&s->PixelPTEBytesPerRowY[k],
			&s->PixelPTEBytesPerRowStorageY[k],
			&p->dpte_row_width_luma_ub[k],
			&p->dpte_row_height_luma[k],
			&p->dpte_row_height_linear_luma[k],
			&s->PixelPTEBytesPerRowY_one_row_per_frame[k],
			&s->dpte_row_width_luma_ub_one_row_per_frame[k],
			&s->dpte_row_height_luma_one_row_per_frame[k],
			&p->meta_req_width[k],
			&p->meta_req_height[k],
			&p->meta_row_width[k],
			&p->meta_row_height[k],
			&p->PixelPTEReqWidthY[k],
			&p->PixelPTEReqHeightY[k],
			&p->PTERequestSizeY[k],
			&p->dpde0_bytes_per_frame_ub_l[k],
			&p->meta_pte_bytes_per_frame_ub_l[k]);

			p->PrefetchSourceLinesY[k] = CalculatePrefetchSourceLines(
			p->myPipe[k].VRatio,
			p->myPipe[k].VTaps,
			p->myPipe[k].InterlaceEnable,
			p->myPipe[k].ProgressiveToInterlaceUnitInOPP,
			p->myPipe[k].SwathHeightY,
			p->myPipe[k].SourceScan,
			p->myPipe[k].ViewportStationary,
			p->SwathWidthY[k],
			p->myPipe[k].ViewportHeight,
			p->myPipe[k].ViewportXStart,
			p->myPipe[k].ViewportYStart,

			// Output
			&p->VInitPreFillY[k],
			&p->MaxNumSwathY[k]);

		p->PDEAndMetaPTEBytesFrame[k] = (s->PDEAndMetaPTEBytesFrameY + s->PDEAndMetaPTEBytesFrameC) * (1 + 8 * s->HostVMDynamicLevels);
		p->MetaRowByte[k] = s->MetaRowByteY[k] + s->MetaRowByteC[k];

		if (s->PixelPTEBytesPerRowStorageY[k] <= 64 * s->PTEBufferSizeInRequestsForLuma[k] && s->PixelPTEBytesPerRowStorageC[k] <= 64 * s->PTEBufferSizeInRequestsForChroma[k]) {
			p->PTEBufferSizeNotExceeded[k] = true;
		} else {
			p->PTEBufferSizeNotExceeded[k] = false;
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%u, PixelPTEBytesPerRowY = %u\n", __func__, k, s->PixelPTEBytesPerRowY[k]);
			dml_print("DML::%s: k=%u, PixelPTEBytesPerRowC = %u\n", __func__, k, s->PixelPTEBytesPerRowC[k]);
			dml_print("DML::%s: k=%u, PixelPTEBytesPerRowStorageY = %u\n", __func__, k, s->PixelPTEBytesPerRowStorageY[k]);
			dml_print("DML::%s: k=%u, PixelPTEBytesPerRowStorageC = %u\n", __func__, k, s->PixelPTEBytesPerRowStorageC[k]);
			dml_print("DML::%s: k=%u, PTEBufferSizeInRequestsForLuma = %u\n", __func__, k, s->PTEBufferSizeInRequestsForLuma[k]);
			dml_print("DML::%s: k=%u, PTEBufferSizeInRequestsForChroma = %u\n", __func__, k, s->PTEBufferSizeInRequestsForChroma[k]);
			dml_print("DML::%s: k=%u, PTEBufferSizeNotExceeded          = %u\n",  __func__, k, p->PTEBufferSizeNotExceeded[k]);
#endif
		}
		s->one_row_per_frame_fits_in_buffer[k] = (s->PixelPTEBytesPerRowY_one_row_per_frame[k] <= 64 * 2 * s->PTEBufferSizeInRequestsForLuma[k] &&
			s->PixelPTEBytesPerRowC_one_row_per_frame[k] <= 64 * 2 * s->PTEBufferSizeInRequestsForChroma[k]);

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u, PDEAndMetaPTEBytesFrame = %u\n", __func__, k, p->PDEAndMetaPTEBytesFrame[k]);
		dml_print("DML::%s: k=%u, PDEAndMetaPTEBytesFrameY = %u\n", __func__, k, s->PDEAndMetaPTEBytesFrameY);
		dml_print("DML::%s: k=%u, PDEAndMetaPTEBytesFrameC = %u\n", __func__, k, s->PDEAndMetaPTEBytesFrameC);
		dml_print("DML::%s: k=%u, HostVMDynamicLevels = %u\n", __func__, k, s->HostVMDynamicLevels);
		dml_print("DML::%s: k=%u, one_row_per_frame_fits_in_buffer = %u\n", __func__, k, s->one_row_per_frame_fits_in_buffer[k]);
		dml_print("DML::%s: k=%u, PixelPTEBytesPerRowY_one_row_per_frame = %u\n", __func__, k, s->PixelPTEBytesPerRowY_one_row_per_frame[k]);
		dml_print("DML::%s: k=%u, PixelPTEBytesPerRowC_one_row_per_frame    = %u\n",  __func__, k, s->PixelPTEBytesPerRowC_one_row_per_frame[k]);
#endif
	}

	CalculateMALLUseForStaticScreen(
		p->NumberOfActiveSurfaces,
		p->MALLAllocatedForDCN,
		p->UseMALLForStaticScreen,   // mode
		p->SurfaceSizeInMALL,
		s->one_row_per_frame_fits_in_buffer,
		// Output
		p->UsesMALLForStaticScreen); // boolen

	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
		if (p->PTEBufferModeOverrideEn[k] == 1) {
			p->PTE_BUFFER_MODE[k] = p->PTEBufferModeOverrideVal[k];
		}
		p->PTE_BUFFER_MODE[k] = p->myPipe[k].FORCE_ONE_ROW_FOR_FRAME || p->UsesMALLForStaticScreen[k] || (p->UseMALLForPStateChange[k] == dml_use_mall_pstate_change_sub_viewport) ||
			(p->UseMALLForPStateChange[k] == dml_use_mall_pstate_change_phantom_pipe) || (p->GPUVMMinPageSizeKBytes[k] > 64);
		p->BIGK_FRAGMENT_SIZE[k] = (dml_uint_t)(dml_log2(p->GPUVMMinPageSizeKBytes[k] * 1024) - 12);
	}

	for (dml_uint_t k = 0; k < p->NumberOfActiveSurfaces; ++k) {
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u, SurfaceSizeInMALL         = %u\n",  __func__, k, p->SurfaceSizeInMALL[k]);
		dml_print("DML::%s: k=%u, UsesMALLForStaticScreen   = %u\n",  __func__, k, p->UsesMALLForStaticScreen[k]);
#endif
		p->use_one_row_for_frame[k] = p->myPipe[k].FORCE_ONE_ROW_FOR_FRAME || p->UsesMALLForStaticScreen[k] || (p->UseMALLForPStateChange[k] == dml_use_mall_pstate_change_sub_viewport) ||
									(p->UseMALLForPStateChange[k] == dml_use_mall_pstate_change_phantom_pipe) || (p->GPUVMMinPageSizeKBytes[k] > 64 && dml_is_vertical_rotation(p->myPipe[k].SourceScan));

		p->use_one_row_for_frame_flip[k] = p->use_one_row_for_frame[k] && !(p->UseMALLForPStateChange[k] == dml_use_mall_pstate_change_full_frame);

		if (p->use_one_row_for_frame[k]) {
			p->dpte_row_height_luma[k] = s->dpte_row_height_luma_one_row_per_frame[k];
			p->dpte_row_width_luma_ub[k] = s->dpte_row_width_luma_ub_one_row_per_frame[k];
			s->PixelPTEBytesPerRowY[k] = s->PixelPTEBytesPerRowY_one_row_per_frame[k];
			p->dpte_row_height_chroma[k] = s->dpte_row_height_chroma_one_row_per_frame[k];
			p->dpte_row_width_chroma_ub[k] = s->dpte_row_width_chroma_ub_one_row_per_frame[k];
			s->PixelPTEBytesPerRowC[k] = s->PixelPTEBytesPerRowC_one_row_per_frame[k];
			p->PTEBufferSizeNotExceeded[k] = s->one_row_per_frame_fits_in_buffer[k];
		}

		if (p->MetaRowByte[k] <= p->DCCMetaBufferSizeBytes) {
			p->DCCMetaBufferSizeNotExceeded[k] = true;
		} else {
			p->DCCMetaBufferSizeNotExceeded[k] = false;

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%u, MetaRowByte                   = %u\n",  __func__, k, p->MetaRowByte[k]);
			dml_print("DML::%s: k=%u, DCCMetaBufferSizeBytes        = %u\n",  __func__, k, p->DCCMetaBufferSizeBytes);
			dml_print("DML::%s: k=%u, DCCMetaBufferSizeNotExceeded  = %u\n",  __func__, k, p->DCCMetaBufferSizeNotExceeded[k]);
#endif
		}
		s->PixelPTEBytesPerRowY[k] = s->PixelPTEBytesPerRowY[k] * (1 + 8 * s->HostVMDynamicLevels);
		s->PixelPTEBytesPerRowC[k] = s->PixelPTEBytesPerRowC[k] * (1 + 8 * s->HostVMDynamicLevels);
		p->PixelPTEBytesPerRow[k] = s->PixelPTEBytesPerRowY[k] + s->PixelPTEBytesPerRowC[k];
		if (p->use_one_row_for_frame[k])
			p->PixelPTEBytesPerRow[k] = p->PixelPTEBytesPerRow[k] / 2;

		CalculateRowBandwidth(
			p->GPUVMEnable,
			p->myPipe[k].SourcePixelFormat,
			p->myPipe[k].VRatio,
			p->myPipe[k].VRatioChroma,
			p->myPipe[k].DCCEnable,
			p->myPipe[k].HTotal / p->myPipe[k].PixelClock,
			s->MetaRowByteY[k],
			s->MetaRowByteC[k],
			p->meta_row_height[k],
			p->meta_row_height_chroma[k],
			s->PixelPTEBytesPerRowY[k],
			s->PixelPTEBytesPerRowC[k],
			p->dpte_row_height_luma[k],
			p->dpte_row_height_chroma[k],

			// Output
			&p->meta_row_bw[k],
			&p->dpte_row_bw[k]);
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u, use_one_row_for_frame = %u\n", __func__, k, p->use_one_row_for_frame[k]);
		dml_print("DML::%s: k=%u, use_one_row_for_frame_flip = %u\n", __func__, k, p->use_one_row_for_frame_flip[k]);
		dml_print("DML::%s: k=%u, UseMALLForPStateChange = %u\n", __func__, k, p->UseMALLForPStateChange[k]);
		dml_print("DML::%s: k=%u, dpte_row_height_luma = %u\n", __func__, k, p->dpte_row_height_luma[k]);
		dml_print("DML::%s: k=%u, dpte_row_width_luma_ub = %u\n", __func__, k, p->dpte_row_width_luma_ub[k]);
		dml_print("DML::%s: k=%u, PixelPTEBytesPerRowY = %u\n", __func__, k, s->PixelPTEBytesPerRowY[k]);
		dml_print("DML::%s: k=%u, dpte_row_height_chroma = %u\n", __func__, k, p->dpte_row_height_chroma[k]);
		dml_print("DML::%s: k=%u, dpte_row_width_chroma_ub = %u\n", __func__, k, p->dpte_row_width_chroma_ub[k]);
		dml_print("DML::%s: k=%u, PixelPTEBytesPerRowC = %u\n", __func__, k, s->PixelPTEBytesPerRowC[k]);
		dml_print("DML::%s: k=%u, PixelPTEBytesPerRow = %u\n", __func__, k, p->PixelPTEBytesPerRow[k]);
		dml_print("DML::%s: k=%u, PTEBufferSizeNotExceeded = %u\n", __func__, k, p->PTEBufferSizeNotExceeded[k]);
		dml_print("DML::%s: k=%u, PTE_BUFFER_MODE = %u\n", __func__, k, p->PTE_BUFFER_MODE[k]);
		dml_print("DML::%s: k=%u, BIGK_FRAGMENT_SIZE     = %u\n", __func__, k, p->BIGK_FRAGMENT_SIZE[k]);
#endif
	}
}

static void CalculateOutputLink(
		dml_float_t PHYCLKPerState,
		dml_float_t PHYCLKD18PerState,
		dml_float_t PHYCLKD32PerState,
		dml_float_t Downspreading,
		dml_bool_t IsMainSurfaceUsingTheIndicatedTiming,
		enum dml_output_encoder_class Output,
		enum dml_output_format_class OutputFormat,
		dml_uint_t HTotal,
		dml_uint_t HActive,
		dml_float_t PixelClockBackEnd,
		dml_float_t ForcedOutputLinkBPP,
		dml_uint_t DSCInputBitPerComponent,
		dml_uint_t NumberOfDSCSlices,
		dml_float_t AudioSampleRate,
		dml_uint_t AudioSampleLayout,
		enum dml_odm_mode ODMModeNoDSC,
		enum dml_odm_mode ODMModeDSC,
		enum dml_dsc_enable DSCEnable,
		dml_uint_t OutputLinkDPLanes,
		enum dml_output_link_dp_rate OutputLinkDPRate,

		// Output
		dml_bool_t *RequiresDSC,
		dml_bool_t *RequiresFEC,
		dml_float_t *OutBpp,
		enum dml_output_type_and_rate__type *OutputType,
		enum dml_output_type_and_rate__rate *OutputRate,
		dml_uint_t *RequiredSlots)
{
	dml_bool_t LinkDSCEnable;
	dml_uint_t dummy;
	*RequiresDSC = false;
	*RequiresFEC = false;
	*OutBpp = 0;

	*OutputType = dml_output_type_unknown;
	*OutputRate = dml_output_rate_unknown;

	if (IsMainSurfaceUsingTheIndicatedTiming) {
		if (Output == dml_hdmi) {
			*RequiresDSC = false;
			*RequiresFEC = false;
			*OutBpp = TruncToValidBPP(dml_min(600, PHYCLKPerState) * 10, 3, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, false, Output,
									OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (dml_uint_t)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
			//OutputTypeAndRate = "HDMI";
			*OutputType = dml_output_type_hdmi;

		} else if (Output == dml_dp || Output == dml_dp2p0 || Output == dml_edp) {
			if (DSCEnable == dml_dsc_enable) {
				*RequiresDSC = true;
				LinkDSCEnable = true;
				if (Output == dml_dp || Output == dml_dp2p0) {
					*RequiresFEC = true;
				} else {
					*RequiresFEC = false;
				}
			} else {
				*RequiresDSC = false;
				LinkDSCEnable = false;
				if (Output == dml_dp2p0) {
					*RequiresFEC = true;
				} else {
					*RequiresFEC = false;
				}
			}
			if (Output == dml_dp2p0) {
				*OutBpp = 0;
				if ((OutputLinkDPRate == dml_dp_rate_na || OutputLinkDPRate == dml_dp_rate_uhbr10) && PHYCLKD32PerState >= 10000 / 32) {
					*OutBpp = TruncToValidBPP((1 - Downspreading / 100) * 10000, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
												OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (dml_uint_t)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					if (*OutBpp == 0 && PHYCLKD32PerState < 13500 / 32 && DSCEnable == dml_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						*OutBpp = TruncToValidBPP((1 - Downspreading / 100) * 10000, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
													OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (dml_uint_t)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " UHBR10";
					*OutputType = dml_output_type_dp2p0;
					*OutputRate = dml_output_rate_dp_rate_uhbr10;
				}
				if ((OutputLinkDPRate == dml_dp_rate_na || OutputLinkDPRate == dml_dp_rate_uhbr13p5) && *OutBpp == 0 && PHYCLKD32PerState >= 13500 / 32) {
					*OutBpp = TruncToValidBPP((1 - Downspreading / 100) * 13500, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
												OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (dml_uint_t)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);

					if (*OutBpp == 0 && PHYCLKD32PerState < 20000 / 32 && DSCEnable == dml_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						*OutBpp = TruncToValidBPP((1 - Downspreading / 100) * 13500, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
												OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (dml_uint_t)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " UHBR13p5";
					*OutputType = dml_output_type_dp2p0;
					*OutputRate = dml_output_rate_dp_rate_uhbr13p5;
				}
				if ((OutputLinkDPRate == dml_dp_rate_na || OutputLinkDPRate == dml_dp_rate_uhbr20) && *OutBpp == 0 && PHYCLKD32PerState >= 20000 / 32) {
					*OutBpp = TruncToValidBPP((1 - Downspreading / 100) * 20000, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
											OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (dml_uint_t)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					if (*OutBpp == 0 && DSCEnable == dml_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						*OutBpp = TruncToValidBPP((1 - Downspreading / 100) * 20000, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
												OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (dml_uint_t)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " UHBR20";
					*OutputType = dml_output_type_dp2p0;
					*OutputRate = dml_output_rate_dp_rate_uhbr20;
				}
			} else { // output is dp or edp
				*OutBpp = 0;
				if ((OutputLinkDPRate == dml_dp_rate_na || OutputLinkDPRate == dml_dp_rate_hbr) && PHYCLKPerState >= 270) {
					*OutBpp = TruncToValidBPP((1 - Downspreading / 100) * 2700, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
											OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (dml_uint_t)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					if (*OutBpp == 0 && PHYCLKPerState < 540 && DSCEnable == dml_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						if (Output == dml_dp) {
							*RequiresFEC = true;
						}
						*OutBpp = TruncToValidBPP((1 - Downspreading / 100) * 2700, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
												OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (dml_uint_t)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " HBR";
					*OutputType = (Output == dml_dp) ? dml_output_type_dp : dml_output_type_edp;
					*OutputRate = dml_output_rate_dp_rate_hbr;
				}
				if ((OutputLinkDPRate == dml_dp_rate_na || OutputLinkDPRate == dml_dp_rate_hbr2) && *OutBpp == 0 && PHYCLKPerState >= 540) {
					*OutBpp = TruncToValidBPP((1 - Downspreading / 100) * 5400, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
											OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (dml_uint_t)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);

					if (*OutBpp == 0 && PHYCLKPerState < 810 && DSCEnable == dml_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						if (Output == dml_dp) {
							*RequiresFEC = true;
						}
						*OutBpp = TruncToValidBPP((1 - Downspreading / 100) * 5400, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
												OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (dml_uint_t)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " HBR2";
					*OutputType = (Output == dml_dp) ? dml_output_type_dp : dml_output_type_edp;
					*OutputRate = dml_output_rate_dp_rate_hbr2;
				}
				if ((OutputLinkDPRate == dml_dp_rate_na || OutputLinkDPRate == dml_dp_rate_hbr3) && *OutBpp == 0 && PHYCLKPerState >= 810) { // VBA_ERROR, vba code doesn't have hbr3 check
					*OutBpp = TruncToValidBPP((1 - Downspreading / 100) * 8100, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
											OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (dml_uint_t)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);

					if (*OutBpp == 0 && DSCEnable == dml_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						if (Output == dml_dp) {
							*RequiresFEC = true;
						}
						*OutBpp = TruncToValidBPP((1 - Downspreading / 100) * 8100, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
												OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (dml_uint_t)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " HBR3";
					*OutputType = (Output == dml_dp) ? dml_output_type_dp : dml_output_type_edp;
					*OutputRate = dml_output_rate_dp_rate_hbr3;
				}
			}
		}
	}
}

/// @brief Determine the ODM mode and number of DPP used per plane based on dispclk, dsc usage, odm usage policy
static void CalculateODMMode(
		dml_uint_t MaximumPixelsPerLinePerDSCUnit,
		dml_uint_t HActive,
		enum dml_output_encoder_class Output,
		enum dml_output_format_class OutputFormat,
		enum dml_odm_use_policy ODMUse,
		dml_float_t StateDispclk,
		dml_float_t MaxDispclk,
		dml_bool_t DSCEnable,
		dml_uint_t TotalNumberOfActiveDPP,
		dml_uint_t MaxNumDPP,
		dml_float_t PixelClock,
		dml_float_t DISPCLKDPPCLKDSCCLKDownSpreading,
		dml_float_t DISPCLKRampingMargin,
		dml_float_t DISPCLKDPPCLKVCOSpeed,

		// Output
		dml_bool_t *TotalAvailablePipesSupport,
		dml_uint_t *NumberOfDPP,
		enum dml_odm_mode *ODMMode,
		dml_float_t *RequiredDISPCLKPerSurface)
{

	dml_float_t SurfaceRequiredDISPCLKWithoutODMCombine;
	dml_float_t SurfaceRequiredDISPCLKWithODMCombineTwoToOne;
	dml_float_t SurfaceRequiredDISPCLKWithODMCombineFourToOne;

	SurfaceRequiredDISPCLKWithoutODMCombine = CalculateRequiredDispclk(dml_odm_mode_bypass, PixelClock, DISPCLKDPPCLKDSCCLKDownSpreading, DISPCLKRampingMargin, DISPCLKDPPCLKVCOSpeed, MaxDispclk);
	SurfaceRequiredDISPCLKWithODMCombineTwoToOne = CalculateRequiredDispclk(dml_odm_mode_combine_2to1, PixelClock, DISPCLKDPPCLKDSCCLKDownSpreading, DISPCLKRampingMargin, DISPCLKDPPCLKVCOSpeed, MaxDispclk);
	SurfaceRequiredDISPCLKWithODMCombineFourToOne = CalculateRequiredDispclk(dml_odm_mode_combine_4to1, PixelClock, DISPCLKDPPCLKDSCCLKDownSpreading, DISPCLKRampingMargin, DISPCLKDPPCLKVCOSpeed, MaxDispclk);
	*TotalAvailablePipesSupport = true;

	if (OutputFormat == dml_420) {
		if (HActive > 4 * DML2_MAX_FMT_420_BUFFER_WIDTH)
			*TotalAvailablePipesSupport = false;
		else if (HActive > 2 * DML2_MAX_FMT_420_BUFFER_WIDTH)
			ODMUse = dml_odm_use_policy_combine_4to1;
		else if (HActive > DML2_MAX_FMT_420_BUFFER_WIDTH && ODMUse != dml_odm_use_policy_combine_4to1)
			ODMUse = dml_odm_use_policy_combine_2to1;
		if (Output == dml_hdmi && ODMUse == dml_odm_use_policy_combine_2to1)
			*TotalAvailablePipesSupport = false;
		if ((Output == dml_hdmi || Output == dml_dp || Output == dml_edp) && ODMUse == dml_odm_use_policy_combine_4to1)
			*TotalAvailablePipesSupport = false;
	}

	if (ODMUse == dml_odm_use_policy_bypass || ODMUse == dml_odm_use_policy_combine_as_needed)
		*ODMMode = dml_odm_mode_bypass;
	else if (ODMUse == dml_odm_use_policy_combine_2to1)
		*ODMMode = dml_odm_mode_combine_2to1;
	else if (ODMUse == dml_odm_use_policy_combine_4to1)
		*ODMMode = dml_odm_mode_combine_4to1;
	else if (ODMUse == dml_odm_use_policy_split_1to2)
		*ODMMode = dml_odm_mode_split_1to2;
	else if (ODMUse == dml_odm_use_policy_mso_1to2)
		*ODMMode = dml_odm_mode_mso_1to2;
	else if (ODMUse == dml_odm_use_policy_mso_1to4)
		*ODMMode = dml_odm_mode_mso_1to4;

	*RequiredDISPCLKPerSurface = SurfaceRequiredDISPCLKWithoutODMCombine;
	*NumberOfDPP = 0;

	if (!(Output == dml_hdmi || Output == dml_dp || Output == dml_edp) && (ODMUse == dml_odm_use_policy_combine_4to1 || (ODMUse == dml_odm_use_policy_combine_as_needed &&
		(SurfaceRequiredDISPCLKWithODMCombineTwoToOne > StateDispclk || (DSCEnable && (HActive > 2 * MaximumPixelsPerLinePerDSCUnit)))))) {
		if (TotalNumberOfActiveDPP + 4 <= MaxNumDPP) {
			*ODMMode = dml_odm_mode_combine_4to1;
			*RequiredDISPCLKPerSurface = SurfaceRequiredDISPCLKWithODMCombineFourToOne;
			*NumberOfDPP = 4;
		} else {
			*TotalAvailablePipesSupport = false;
		}
	} else if (Output != dml_hdmi && (ODMUse == dml_odm_use_policy_combine_2to1 || (ODMUse == dml_odm_use_policy_combine_as_needed &&
				((SurfaceRequiredDISPCLKWithoutODMCombine > StateDispclk && SurfaceRequiredDISPCLKWithODMCombineTwoToOne <= StateDispclk) ||
				(DSCEnable && (HActive > MaximumPixelsPerLinePerDSCUnit)))))) {
		if (TotalNumberOfActiveDPP + 2 <= MaxNumDPP) {
			*ODMMode = dml_odm_mode_combine_2to1;
			*RequiredDISPCLKPerSurface = SurfaceRequiredDISPCLKWithODMCombineTwoToOne;
			*NumberOfDPP = 2;
		} else {
			*TotalAvailablePipesSupport = false;
		}
	} else {
		if (TotalNumberOfActiveDPP + 1 <= MaxNumDPP) {
			*NumberOfDPP = 1;
		} else {
			*TotalAvailablePipesSupport = false;
		}
	}
}

/// @brief Calculate the required DISPCLK given the odm mode and pixclk
static dml_float_t CalculateRequiredDispclk(
		enum dml_odm_mode ODMMode,
		dml_float_t PixelClock,
		dml_float_t DISPCLKDPPCLKDSCCLKDownSpreading,
		dml_float_t DISPCLKRampingMargin,
		dml_float_t DISPCLKDPPCLKVCOSpeed,
		dml_float_t MaxDispclk)
{
	dml_float_t RequiredDispclk = 0.;
	dml_float_t PixelClockAfterODM;

	dml_float_t DISPCLKWithRampingRoundedToDFSGranularity;
	dml_float_t DISPCLKWithoutRampingRoundedToDFSGranularity;
	dml_float_t MaxDispclkRoundedDownToDFSGranularity;

	if (ODMMode == dml_odm_mode_combine_4to1) {
		PixelClockAfterODM = PixelClock / 4;
	} else if (ODMMode == dml_odm_mode_combine_2to1) {
		PixelClockAfterODM = PixelClock / 2;
	} else {
		PixelClockAfterODM = PixelClock;
	}

	DISPCLKWithRampingRoundedToDFSGranularity = RoundToDFSGranularity(PixelClockAfterODM * (1.0 + DISPCLKDPPCLKDSCCLKDownSpreading / 100.0) * (1 + DISPCLKRampingMargin / 100.0), 1, DISPCLKDPPCLKVCOSpeed);
	DISPCLKWithoutRampingRoundedToDFSGranularity = RoundToDFSGranularity(PixelClockAfterODM * (1.0 + DISPCLKDPPCLKDSCCLKDownSpreading / 100.0), 1, DISPCLKDPPCLKVCOSpeed);
	MaxDispclkRoundedDownToDFSGranularity = RoundToDFSGranularity(MaxDispclk, 0, DISPCLKDPPCLKVCOSpeed);

	if (DISPCLKWithoutRampingRoundedToDFSGranularity > MaxDispclkRoundedDownToDFSGranularity) {
		RequiredDispclk = DISPCLKWithoutRampingRoundedToDFSGranularity;
	} else if (DISPCLKWithRampingRoundedToDFSGranularity > MaxDispclkRoundedDownToDFSGranularity) {
		RequiredDispclk = MaxDispclkRoundedDownToDFSGranularity;
	} else {
		RequiredDispclk = DISPCLKWithRampingRoundedToDFSGranularity;
	}

	return RequiredDispclk;
}

/// @brief Determine DPPCLK if there only one DPP per plane, main factor is the pixel rate and DPP scaling parameter
static void CalculateSinglePipeDPPCLKAndSCLThroughput(
		dml_float_t HRatio,
		dml_float_t HRatioChroma,
		dml_float_t VRatio,
		dml_float_t VRatioChroma,
		dml_float_t MaxDCHUBToPSCLThroughput,
		dml_float_t MaxPSCLToLBThroughput,
		dml_float_t PixelClock,
		enum dml_source_format_class SourcePixelFormat,
		dml_uint_t HTaps,
		dml_uint_t HTapsChroma,
		dml_uint_t VTaps,
		dml_uint_t VTapsChroma,

		// Output
		dml_float_t *PSCL_THROUGHPUT,
		dml_float_t *PSCL_THROUGHPUT_CHROMA,
		dml_float_t *DPPCLKUsingSingleDPP)
{
	dml_float_t DPPCLKUsingSingleDPPLuma;
	dml_float_t DPPCLKUsingSingleDPPChroma;

	if (HRatio > 1) {
		*PSCL_THROUGHPUT = dml_min(MaxDCHUBToPSCLThroughput, MaxPSCLToLBThroughput * HRatio / dml_ceil((dml_float_t) HTaps / 6.0, 1.0));
	} else {
		*PSCL_THROUGHPUT = dml_min(MaxDCHUBToPSCLThroughput, MaxPSCLToLBThroughput);
	}

	DPPCLKUsingSingleDPPLuma = PixelClock * dml_max3(VTaps / 6 * dml_min(1, HRatio), HRatio * VRatio / *PSCL_THROUGHPUT, 1);

	if ((HTaps > 6 || VTaps > 6) && DPPCLKUsingSingleDPPLuma < 2 * PixelClock)
		DPPCLKUsingSingleDPPLuma = 2 * PixelClock;

	if ((SourcePixelFormat != dml_420_8 && SourcePixelFormat != dml_420_10 && SourcePixelFormat != dml_420_12 && SourcePixelFormat != dml_rgbe_alpha)) {
		*PSCL_THROUGHPUT_CHROMA = 0;
		*DPPCLKUsingSingleDPP = DPPCLKUsingSingleDPPLuma;
	} else {
		if (HRatioChroma > 1) {
			*PSCL_THROUGHPUT_CHROMA = dml_min(MaxDCHUBToPSCLThroughput, MaxPSCLToLBThroughput * HRatioChroma / dml_ceil((dml_float_t) HTapsChroma / 6.0, 1.0));
		} else {
			*PSCL_THROUGHPUT_CHROMA = dml_min(MaxDCHUBToPSCLThroughput, MaxPSCLToLBThroughput);
		}
		DPPCLKUsingSingleDPPChroma = PixelClock * dml_max3(VTapsChroma / 6 * dml_min(1, HRatioChroma),
															HRatioChroma * VRatioChroma / *PSCL_THROUGHPUT_CHROMA, 1);
		if ((HTapsChroma > 6 || VTapsChroma > 6) && DPPCLKUsingSingleDPPChroma < 2 * PixelClock)
			DPPCLKUsingSingleDPPChroma = 2 * PixelClock;
		*DPPCLKUsingSingleDPP = dml_max(DPPCLKUsingSingleDPPLuma, DPPCLKUsingSingleDPPChroma);
	}
}

/// @brief Calculate the actual dppclk freq
/// @param DPPCLKUsingSingleDPP DppClk freq required if there is only 1 DPP per plane
/// @param DPPPerSurface Number of DPP for each plane
static void CalculateDPPCLK(
		dml_uint_t NumberOfActiveSurfaces,
		dml_float_t DISPCLKDPPCLKDSCCLKDownSpreading,
		dml_float_t DISPCLKDPPCLKVCOSpeed,
		dml_float_t DPPCLKUsingSingleDPP[],
		dml_uint_t DPPPerSurface[],

		// Output
		dml_float_t *GlobalDPPCLK,
		dml_float_t Dppclk[])
{
	*GlobalDPPCLK = 0;
	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		Dppclk[k] = DPPCLKUsingSingleDPP[k] / DPPPerSurface[k] * (1 + DISPCLKDPPCLKDSCCLKDownSpreading / 100.0);
		*GlobalDPPCLK = dml_max(*GlobalDPPCLK, Dppclk[k]);
	}
	*GlobalDPPCLK = RoundToDFSGranularity(*GlobalDPPCLK, 1, DISPCLKDPPCLKVCOSpeed);

	dml_print("DML::%s: GlobalDPPCLK = %f\n", __func__, *GlobalDPPCLK);
	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		Dppclk[k] = *GlobalDPPCLK / 255.0 * dml_ceil(Dppclk[k] * 255.0 / *GlobalDPPCLK, 1.0);
		dml_print("DML::%s: Dppclk[%0d] = %f\n", __func__, k, Dppclk[k]);
	}
}

static void CalculateMALLUseForStaticScreen(
		dml_uint_t NumberOfActiveSurfaces,
		dml_uint_t MALLAllocatedForDCNFinal,
		enum dml_use_mall_for_static_screen_mode *UseMALLForStaticScreen,
		dml_uint_t SurfaceSizeInMALL[],
		dml_bool_t one_row_per_frame_fits_in_buffer[],

		// Output
		dml_bool_t UsesMALLForStaticScreen[])
{

	dml_uint_t SurfaceToAddToMALL;
	dml_bool_t CanAddAnotherSurfaceToMALL;
	dml_uint_t TotalSurfaceSizeInMALL;

	TotalSurfaceSizeInMALL = 0;
	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		UsesMALLForStaticScreen[k] = (UseMALLForStaticScreen[k] == dml_use_mall_static_screen_enable);
		if (UsesMALLForStaticScreen[k])
			TotalSurfaceSizeInMALL = TotalSurfaceSizeInMALL + SurfaceSizeInMALL[k];
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u, UsesMALLForStaticScreen = %u\n",  __func__, k, UsesMALLForStaticScreen[k]);
		dml_print("DML::%s: k=%u, TotalSurfaceSizeInMALL = %u\n",  __func__, k, TotalSurfaceSizeInMALL);
#endif
	}

	SurfaceToAddToMALL = 0;
	CanAddAnotherSurfaceToMALL = true;
	while (CanAddAnotherSurfaceToMALL) {
		CanAddAnotherSurfaceToMALL = false;
		for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
			if (TotalSurfaceSizeInMALL + SurfaceSizeInMALL[k] <= MALLAllocatedForDCNFinal * 1024 * 1024 &&
				!UsesMALLForStaticScreen[k] && UseMALLForStaticScreen[k] != dml_use_mall_static_screen_disable && one_row_per_frame_fits_in_buffer[k] &&
				(!CanAddAnotherSurfaceToMALL || SurfaceSizeInMALL[k] < SurfaceSizeInMALL[SurfaceToAddToMALL])) {
				CanAddAnotherSurfaceToMALL = true;
				SurfaceToAddToMALL = k;
				dml_print("DML::%s: k=%u, UseMALLForStaticScreen = %u (dis, en, optimize)\n",  __func__, k, UseMALLForStaticScreen[k]);
			}
		}
		if (CanAddAnotherSurfaceToMALL) {
			UsesMALLForStaticScreen[SurfaceToAddToMALL] = true;
			TotalSurfaceSizeInMALL = TotalSurfaceSizeInMALL + SurfaceSizeInMALL[SurfaceToAddToMALL];

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: SurfaceToAddToMALL       = %u\n",  __func__, SurfaceToAddToMALL);
			dml_print("DML::%s: TotalSurfaceSizeInMALL   = %u\n",  __func__, TotalSurfaceSizeInMALL);
#endif
		}
	}
}

// @brief Calculate return bw for VM only traffic
dml_float_t dml_get_return_bw_mbps_vm_only(
								const struct soc_bounding_box_st *soc,
								dml_bool_t use_ideal_dram_bw_strobe,
								dml_bool_t HostVMEnable,
								dml_float_t DCFCLK,
								dml_float_t FabricClock,
								dml_float_t DRAMSpeed)
{
	dml_float_t VMDataOnlyReturnBW =
		dml_min3(soc->return_bus_width_bytes * DCFCLK * soc->pct_ideal_sdp_bw_after_urgent / 100.0,
				FabricClock * soc->fabric_datapath_to_dcn_data_return_bytes * soc->pct_ideal_sdp_bw_after_urgent / 100.0,
				DRAMSpeed * soc->num_chans *  soc->dram_channel_width_bytes *
				((use_ideal_dram_bw_strobe && !HostVMEnable) ? soc->pct_ideal_dram_bw_after_urgent_strobe :  soc->pct_ideal_dram_bw_after_urgent_vm_only) / 100.0);
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: use_ideal_dram_bw_strobe = %u\n", __func__, use_ideal_dram_bw_strobe);
	dml_print("DML::%s: HostVMEnable = %u\n", __func__, HostVMEnable);
	dml_print("DML::%s: DCFCLK = %f\n", __func__, DCFCLK);
	dml_print("DML::%s: FabricClock = %f\n", __func__, FabricClock);
	dml_print("DML::%s: DRAMSpeed = %f\n", __func__, DRAMSpeed);
	dml_print("DML::%s: VMDataOnlyReturnBW = %f\n", __func__, VMDataOnlyReturnBW);
#endif
	return VMDataOnlyReturnBW;
}

// Function: dml_get_return_bw_mbps
// Megabyte per second
dml_float_t dml_get_return_bw_mbps(
						const struct soc_bounding_box_st *soc,
						dml_bool_t use_ideal_dram_bw_strobe,
						dml_bool_t HostVMEnable,
						dml_float_t DCFCLK,
						dml_float_t FabricClock,
						dml_float_t DRAMSpeed)
{
	dml_float_t ReturnBW = 0.;
	dml_float_t IdealSDPPortBandwidth    = soc->return_bus_width_bytes * DCFCLK;
	dml_float_t IdealFabricBandwidth     = FabricClock * soc->fabric_datapath_to_dcn_data_return_bytes;
	dml_float_t IdealDRAMBandwidth       = DRAMSpeed * soc->num_chans * soc->dram_channel_width_bytes;
	dml_float_t PixelDataOnlyReturnBW    = dml_min3(IdealSDPPortBandwidth * soc->pct_ideal_sdp_bw_after_urgent / 100,
												IdealFabricBandwidth * soc->pct_ideal_fabric_bw_after_urgent / 100,
												IdealDRAMBandwidth * ((use_ideal_dram_bw_strobe && !HostVMEnable) ? soc->pct_ideal_dram_bw_after_urgent_strobe :
																						soc->pct_ideal_dram_bw_after_urgent_pixel_only) / 100);
	dml_float_t PixelMixedWithVMDataReturnBW = dml_min3(IdealSDPPortBandwidth * soc->pct_ideal_sdp_bw_after_urgent / 100,
													IdealFabricBandwidth * soc->pct_ideal_fabric_bw_after_urgent / 100,
													IdealDRAMBandwidth * ((use_ideal_dram_bw_strobe && !HostVMEnable) ? soc->pct_ideal_dram_bw_after_urgent_strobe :
																							soc->pct_ideal_dram_bw_after_urgent_pixel_and_vm) / 100);

	if (HostVMEnable != true) {
		ReturnBW = PixelDataOnlyReturnBW;
	} else {
		ReturnBW = PixelMixedWithVMDataReturnBW;
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: use_ideal_dram_bw_strobe = %u\n", __func__, use_ideal_dram_bw_strobe);
	dml_print("DML::%s: HostVMEnable = %u\n", __func__, HostVMEnable);
	dml_print("DML::%s: DCFCLK = %f\n", __func__, DCFCLK);
	dml_print("DML::%s: FabricClock = %f\n", __func__, FabricClock);
	dml_print("DML::%s: DRAMSpeed = %f\n", __func__, DRAMSpeed);
	dml_print("DML::%s: IdealSDPPortBandwidth = %f\n", __func__, IdealSDPPortBandwidth);
	dml_print("DML::%s: IdealFabricBandwidth = %f\n", __func__, IdealFabricBandwidth);
	dml_print("DML::%s: IdealDRAMBandwidth = %f\n", __func__, IdealDRAMBandwidth);
	dml_print("DML::%s: PixelDataOnlyReturnBW = %f\n", __func__, PixelDataOnlyReturnBW);
	dml_print("DML::%s: PixelMixedWithVMDataReturnBW = %f\n", __func__, PixelMixedWithVMDataReturnBW);
	dml_print("DML::%s: ReturnBW                  = %f MBps\n", __func__, ReturnBW);
#endif
	return ReturnBW;
}

// Function: dml_get_return_dram_bw_mbps
// Megabyte per second
static dml_float_t dml_get_return_dram_bw_mbps(
						const struct soc_bounding_box_st *soc,
						dml_bool_t use_ideal_dram_bw_strobe,
						dml_bool_t HostVMEnable,
						dml_float_t DRAMSpeed)
{
	dml_float_t ReturnDRAMBW = 0.;
	dml_float_t IdealDRAMBandwidth       = DRAMSpeed * soc->num_chans * soc->dram_channel_width_bytes;
	dml_float_t PixelDataOnlyReturnBW    = IdealDRAMBandwidth * ((use_ideal_dram_bw_strobe && !HostVMEnable) ? soc->pct_ideal_dram_bw_after_urgent_strobe :
																						soc->pct_ideal_dram_bw_after_urgent_pixel_only) / 100;
	dml_float_t PixelMixedWithVMDataReturnBW =  IdealDRAMBandwidth * ((use_ideal_dram_bw_strobe && !HostVMEnable) ? soc->pct_ideal_dram_bw_after_urgent_strobe :
																							soc->pct_ideal_dram_bw_after_urgent_pixel_and_vm) / 100;

	if (HostVMEnable != true) {
		ReturnDRAMBW = PixelDataOnlyReturnBW;
	} else {
		ReturnDRAMBW = PixelMixedWithVMDataReturnBW;
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: use_ideal_dram_bw_strobe = %u\n", __func__, use_ideal_dram_bw_strobe);
	dml_print("DML::%s: HostVMEnable = %u\n", __func__, HostVMEnable);
	dml_print("DML::%s: DRAMSpeed = %f\n", __func__, DRAMSpeed);
	dml_print("DML::%s: IdealDRAMBandwidth = %f\n", __func__, IdealDRAMBandwidth);
	dml_print("DML::%s: PixelDataOnlyReturnBW = %f\n", __func__, PixelDataOnlyReturnBW);
	dml_print("DML::%s: PixelMixedWithVMDataReturnBW = %f\n", __func__, PixelMixedWithVMDataReturnBW);
	dml_print("DML::%s: ReturnDRAMBW                     = %f MBps\n", __func__, ReturnDRAMBW);
#endif
	return ReturnDRAMBW;
}

/// @brief BACKEND
static dml_uint_t DSCDelayRequirement(
						dml_bool_t DSCEnabled,
						enum dml_odm_mode ODMMode,
						dml_uint_t DSCInputBitPerComponent,
						dml_float_t OutputBpp,
						dml_uint_t HActive,
						dml_uint_t HTotal,
						dml_uint_t NumberOfDSCSlices,
						enum dml_output_format_class OutputFormat,
						enum dml_output_encoder_class Output,
						dml_float_t PixelClock,
						dml_float_t PixelClockBackEnd)
{
	dml_uint_t DSCDelayRequirement_val = 0;

	if (DSCEnabled == true && OutputBpp != 0) {
		if (ODMMode == dml_odm_mode_combine_4to1) {
			DSCDelayRequirement_val = 4 * (dscceComputeDelay(DSCInputBitPerComponent, OutputBpp, (dml_uint_t)(dml_ceil((dml_float_t) HActive / (dml_float_t) NumberOfDSCSlices, 1.0)),
												(dml_uint_t) (NumberOfDSCSlices / 4.0), OutputFormat, Output) + dscComputeDelay(OutputFormat, Output));
		} else if (ODMMode == dml_odm_mode_combine_2to1) {
			DSCDelayRequirement_val = 2 * (dscceComputeDelay(DSCInputBitPerComponent, OutputBpp, (dml_uint_t)(dml_ceil((dml_float_t) HActive / (dml_float_t) NumberOfDSCSlices, 1.0)),
												(dml_uint_t) (NumberOfDSCSlices / 2.0), OutputFormat, Output) + dscComputeDelay(OutputFormat, Output));
		} else {
			DSCDelayRequirement_val = dscceComputeDelay(DSCInputBitPerComponent, OutputBpp, (dml_uint_t)((dml_float_t) dml_ceil(HActive / (dml_float_t) NumberOfDSCSlices, 1.0)),
										NumberOfDSCSlices, OutputFormat, Output) + dscComputeDelay(OutputFormat, Output);
		}
		DSCDelayRequirement_val = (dml_uint_t)(DSCDelayRequirement_val + (HTotal - HActive) * dml_ceil((dml_float_t) DSCDelayRequirement_val / (dml_float_t) HActive, 1.0));
		DSCDelayRequirement_val = (dml_uint_t)(DSCDelayRequirement_val * PixelClock / PixelClockBackEnd);

	} else {
		DSCDelayRequirement_val = 0;
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DSCEnabled = %u\n", __func__, DSCEnabled);
	dml_print("DML::%s: ODMMode = %u\n", __func__, ODMMode);
	dml_print("DML::%s: OutputBpp = %f\n", __func__, OutputBpp);
	dml_print("DML::%s: HActive = %u\n", __func__, HActive);
	dml_print("DML::%s: HTotal = %u\n", __func__, HTotal);
	dml_print("DML::%s: PixelClock = %f\n", __func__, PixelClock);
	dml_print("DML::%s: PixelClockBackEnd = %f\n", __func__, PixelClockBackEnd);
	dml_print("DML::%s: OutputFormat = %u\n", __func__, OutputFormat);
	dml_print("DML::%s: DSCInputBitPerComponent = %u\n", __func__, DSCInputBitPerComponent);
	dml_print("DML::%s: NumberOfDSCSlices = %u\n", __func__, NumberOfDSCSlices);
	dml_print("DML::%s: DSCDelayRequirement_val = %u\n", __func__, DSCDelayRequirement_val);
#endif

	return DSCDelayRequirement_val;
}

static dml_bool_t CalculateVActiveBandwithSupport(dml_uint_t NumberOfActiveSurfaces,
										dml_float_t ReturnBW,
										dml_bool_t NotUrgentLatencyHiding[],
										dml_float_t ReadBandwidthLuma[],
										dml_float_t ReadBandwidthChroma[],
										dml_float_t cursor_bw[],
										dml_float_t meta_row_bandwidth[],
										dml_float_t dpte_row_bandwidth[],
										dml_uint_t NumberOfDPP[],
										dml_float_t UrgentBurstFactorLuma[],
										dml_float_t UrgentBurstFactorChroma[],
										dml_float_t UrgentBurstFactorCursor[])
{
	dml_bool_t NotEnoughUrgentLatencyHiding = false;
	dml_bool_t CalculateVActiveBandwithSupport_val = false;
	dml_float_t VActiveBandwith = 0;

	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (NotUrgentLatencyHiding[k]) {
			NotEnoughUrgentLatencyHiding = true;
		}
	}

	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		VActiveBandwith = VActiveBandwith + ReadBandwidthLuma[k] * UrgentBurstFactorLuma[k] + ReadBandwidthChroma[k] * UrgentBurstFactorChroma[k] + cursor_bw[k] * UrgentBurstFactorCursor[k] + NumberOfDPP[k] * meta_row_bandwidth[k] + NumberOfDPP[k] * dpte_row_bandwidth[k];
	}

	CalculateVActiveBandwithSupport_val = (VActiveBandwith <= ReturnBW) && !NotEnoughUrgentLatencyHiding;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: NotEnoughUrgentLatencyHiding        = %u\n", __func__, NotEnoughUrgentLatencyHiding);
	dml_print("DML::%s: VActiveBandwith                     = %f\n", __func__, VActiveBandwith);
	dml_print("DML::%s: ReturnBW                            = %f\n", __func__, ReturnBW);
	dml_print("DML::%s: CalculateVActiveBandwithSupport_val = %u\n", __func__, CalculateVActiveBandwithSupport_val);
#endif
	return CalculateVActiveBandwithSupport_val;
}

static void CalculatePrefetchBandwithSupport(
										dml_uint_t NumberOfActiveSurfaces,
										dml_float_t ReturnBW,
										enum dml_use_mall_for_pstate_change_mode UseMALLForPStateChange[],
										dml_bool_t NotUrgentLatencyHiding[],
										dml_float_t ReadBandwidthLuma[],
										dml_float_t ReadBandwidthChroma[],
										dml_float_t PrefetchBandwidthLuma[],
										dml_float_t PrefetchBandwidthChroma[],
										dml_float_t cursor_bw[],
										dml_float_t meta_row_bandwidth[],
										dml_float_t dpte_row_bandwidth[],
										dml_float_t cursor_bw_pre[],
										dml_float_t prefetch_vmrow_bw[],
										dml_uint_t NumberOfDPP[],
										dml_float_t UrgentBurstFactorLuma[],
										dml_float_t UrgentBurstFactorChroma[],
										dml_float_t UrgentBurstFactorCursor[],
										dml_float_t UrgentBurstFactorLumaPre[],
										dml_float_t UrgentBurstFactorChromaPre[],
										dml_float_t UrgentBurstFactorCursorPre[],

										// Output
										dml_float_t *PrefetchBandwidth,
										dml_float_t *PrefetchBandwidthNotIncludingMALLPrefetch,
										dml_float_t *FractionOfUrgentBandwidth,
										dml_bool_t *PrefetchBandwidthSupport)
{
	dml_bool_t NotEnoughUrgentLatencyHiding = false;
	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (NotUrgentLatencyHiding[k]) {
			NotEnoughUrgentLatencyHiding = true;
		}
	}

	*PrefetchBandwidth = 0;
	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		*PrefetchBandwidth = *PrefetchBandwidth + dml_max3(NumberOfDPP[k] * prefetch_vmrow_bw[k],
														ReadBandwidthLuma[k] * UrgentBurstFactorLuma[k] + ReadBandwidthChroma[k] * UrgentBurstFactorChroma[k] + cursor_bw[k] * UrgentBurstFactorCursor[k] + NumberOfDPP[k] * (meta_row_bandwidth[k] + dpte_row_bandwidth[k]),
														NumberOfDPP[k] * (PrefetchBandwidthLuma[k] * UrgentBurstFactorLumaPre[k] + PrefetchBandwidthChroma[k] * UrgentBurstFactorChromaPre[k]) + cursor_bw_pre[k] * UrgentBurstFactorCursorPre[k]);
	}

	*PrefetchBandwidthNotIncludingMALLPrefetch = 0;
	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (UseMALLForPStateChange[k] != dml_use_mall_pstate_change_phantom_pipe)
			*PrefetchBandwidthNotIncludingMALLPrefetch = *PrefetchBandwidthNotIncludingMALLPrefetch
				+ dml_max3(NumberOfDPP[k] * prefetch_vmrow_bw[k],
				ReadBandwidthLuma[k] * UrgentBurstFactorLuma[k] + ReadBandwidthChroma[k] * UrgentBurstFactorChroma[k]
				+ cursor_bw[k] * UrgentBurstFactorCursor[k]
				+ NumberOfDPP[k] * (meta_row_bandwidth[k] + dpte_row_bandwidth[k]),
				NumberOfDPP[k] * (PrefetchBandwidthLuma[k] * UrgentBurstFactorLumaPre[k]
				+ PrefetchBandwidthChroma[k] * UrgentBurstFactorChromaPre[k])
				+ cursor_bw_pre[k] * UrgentBurstFactorCursorPre[k]);
	}

	*PrefetchBandwidthSupport = (*PrefetchBandwidth <= ReturnBW) && !NotEnoughUrgentLatencyHiding;
	*FractionOfUrgentBandwidth = *PrefetchBandwidth / ReturnBW;

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: ReturnBW = %f\n", __func__, ReturnBW);
	dml_print("DML::%s: PrefetchBandwidth = %f\n", __func__, *PrefetchBandwidth);
	dml_print("DML::%s: FractionOfUrgentBandwidth = %f\n", __func__, *FractionOfUrgentBandwidth);
   dml_print("DML::%s: PrefetchBandwidthSupport = %u\n", __func__, *PrefetchBandwidthSupport);
#endif
}

static dml_float_t CalculateBandwidthAvailableForImmediateFlip(
													dml_uint_t NumberOfActiveSurfaces,
													dml_float_t ReturnBW,
													dml_float_t ReadBandwidthLuma[],
													dml_float_t ReadBandwidthChroma[],
													dml_float_t PrefetchBandwidthLuma[],
													dml_float_t PrefetchBandwidthChroma[],
													dml_float_t cursor_bw[],
													dml_float_t cursor_bw_pre[],
													dml_uint_t NumberOfDPP[],
													dml_float_t UrgentBurstFactorLuma[],
													dml_float_t UrgentBurstFactorChroma[],
													dml_float_t UrgentBurstFactorCursor[],
													dml_float_t UrgentBurstFactorLumaPre[],
													dml_float_t UrgentBurstFactorChromaPre[],
													dml_float_t UrgentBurstFactorCursorPre[])
{
	dml_float_t ret_val = ReturnBW;

	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		ret_val = ret_val - dml_max(ReadBandwidthLuma[k] * UrgentBurstFactorLuma[k] + ReadBandwidthChroma[k] * UrgentBurstFactorChroma[k] + cursor_bw[k] * UrgentBurstFactorCursor[k],
									NumberOfDPP[k] * (PrefetchBandwidthLuma[k] * UrgentBurstFactorLumaPre[k] + PrefetchBandwidthChroma[k] * UrgentBurstFactorChromaPre[k]) +
									cursor_bw_pre[k] * UrgentBurstFactorCursorPre[k]);
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u\n", __func__, k);
		dml_print("DML::%s: NumberOfDPP = %u\n", __func__, NumberOfDPP[k]);
		dml_print("DML::%s: ReadBandwidthLuma = %f\n", __func__, ReadBandwidthLuma[k]);
		dml_print("DML::%s: UrgentBurstFactorLuma = %f\n", __func__, UrgentBurstFactorLuma[k]);
		dml_print("DML::%s: ReadBandwidthChroma = %f\n", __func__, ReadBandwidthChroma[k]);
		dml_print("DML::%s: UrgentBurstFactorChroma = %f\n", __func__, UrgentBurstFactorChroma[k]);
		dml_print("DML::%s: cursor_bw = %f\n", __func__, cursor_bw[k]);
		dml_print("DML::%s: UrgentBurstFactorCursor = %f\n", __func__, UrgentBurstFactorCursor[k]);

		dml_print("DML::%s: PrefetchBandwidthLuma = %f\n", __func__, PrefetchBandwidthLuma[k]);
		dml_print("DML::%s: UrgentBurstFactorLumaPre = %f\n", __func__, UrgentBurstFactorLumaPre[k]);
		dml_print("DML::%s: PrefetchBandwidthChroma = %f\n", __func__, PrefetchBandwidthChroma[k]);
		dml_print("DML::%s: UrgentBurstFactorChromaPre = %f\n", __func__, UrgentBurstFactorChromaPre[k]);
		dml_print("DML::%s: cursor_bw_pre = %f\n", __func__, cursor_bw_pre[k]);
		dml_print("DML::%s: UrgentBurstFactorCursorPre = %f\n", __func__, UrgentBurstFactorCursorPre[k]);
		dml_print("DML::%s: ret_val              = %f\n", __func__, ret_val);
#endif
	}

	return ret_val;
}

static void CalculateImmediateFlipBandwithSupport(
											dml_uint_t NumberOfActiveSurfaces,
											dml_float_t ReturnBW,
											enum dml_use_mall_for_pstate_change_mode UseMALLForPStateChange[],
											enum dml_immediate_flip_requirement ImmediateFlipRequirement[],
											dml_float_t final_flip_bw[],
											dml_float_t ReadBandwidthLuma[],
											dml_float_t ReadBandwidthChroma[],
											dml_float_t PrefetchBandwidthLuma[],
											dml_float_t PrefetchBandwidthChroma[],
											dml_float_t cursor_bw[],
											dml_float_t meta_row_bandwidth[],
											dml_float_t dpte_row_bandwidth[],
											dml_float_t cursor_bw_pre[],
											dml_float_t prefetch_vmrow_bw[],
											dml_uint_t NumberOfDPP[],
											dml_float_t UrgentBurstFactorLuma[],
											dml_float_t UrgentBurstFactorChroma[],
											dml_float_t UrgentBurstFactorCursor[],
											dml_float_t UrgentBurstFactorLumaPre[],
											dml_float_t UrgentBurstFactorChromaPre[],
											dml_float_t UrgentBurstFactorCursorPre[],

											// Output
											dml_float_t *TotalBandwidth,
											dml_float_t *TotalBandwidthNotIncludingMALLPrefetch,
											dml_float_t *FractionOfUrgentBandwidth,
											dml_bool_t *ImmediateFlipBandwidthSupport)
{
	*TotalBandwidth = 0;
	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (ImmediateFlipRequirement[k] != dml_immediate_flip_not_required) {



			*TotalBandwidth = *TotalBandwidth + dml_max3(NumberOfDPP[k] * prefetch_vmrow_bw[k],
														NumberOfDPP[k] * final_flip_bw[k] + ReadBandwidthLuma[k] * UrgentBurstFactorLuma[k] + ReadBandwidthChroma[k] * UrgentBurstFactorChroma[k] + cursor_bw[k] * UrgentBurstFactorCursor[k],
														NumberOfDPP[k] * (final_flip_bw[k] + PrefetchBandwidthLuma[k] * UrgentBurstFactorLumaPre[k] + PrefetchBandwidthChroma[k] * UrgentBurstFactorChromaPre[k]) + cursor_bw_pre[k] * UrgentBurstFactorCursorPre[k]);
		} else {
			*TotalBandwidth = *TotalBandwidth + dml_max3(NumberOfDPP[k] * prefetch_vmrow_bw[k],
														NumberOfDPP[k] * (meta_row_bandwidth[k] + dpte_row_bandwidth[k]) + ReadBandwidthLuma[k] * UrgentBurstFactorLuma[k] + ReadBandwidthChroma[k] * UrgentBurstFactorChroma[k] + cursor_bw[k] * UrgentBurstFactorCursor[k],
														NumberOfDPP[k] * (PrefetchBandwidthLuma[k] * UrgentBurstFactorLumaPre[k] + PrefetchBandwidthChroma[k] * UrgentBurstFactorChromaPre[k]) + cursor_bw_pre[k] * UrgentBurstFactorCursorPre[k]);
		}
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k = %u\n", __func__, k);
		dml_print("DML::%s: ImmediateFlipRequirement = %u\n", __func__, ImmediateFlipRequirement[k]);
		dml_print("DML::%s: TotalBandwidth = %f\n", __func__, *TotalBandwidth);
		dml_print("DML::%s: NumberOfDPP = %u\n", __func__, NumberOfDPP[k]);
		dml_print("DML::%s: prefetch_vmrow_bw = %f\n", __func__, prefetch_vmrow_bw[k]);
		dml_print("DML::%s: final_flip_bw = %f\n", __func__, final_flip_bw[k]);
		dml_print("DML::%s: ReadBandwidthLuma = %f\n", __func__, ReadBandwidthLuma[k]);
		dml_print("DML::%s: UrgentBurstFactorLuma = %f\n", __func__, UrgentBurstFactorLuma[k]);
		dml_print("DML::%s: ReadBandwidthChroma = %f\n", __func__, ReadBandwidthChroma[k]);
		dml_print("DML::%s: UrgentBurstFactorChroma = %f\n", __func__, UrgentBurstFactorChroma[k]);
		dml_print("DML::%s: cursor_bw = %f\n", __func__, cursor_bw[k]);
		dml_print("DML::%s: UrgentBurstFactorCursor = %f\n", __func__, UrgentBurstFactorCursor[k]);
		dml_print("DML::%s: PrefetchBandwidthLuma = %f\n", __func__, PrefetchBandwidthLuma[k]);
		dml_print("DML::%s: UrgentBurstFactorLumaPre = %f\n", __func__, UrgentBurstFactorLumaPre[k]);
		dml_print("DML::%s: PrefetchBandwidthChroma = %f\n", __func__, PrefetchBandwidthChroma[k]);
		dml_print("DML::%s: UrgentBurstFactorChromaPre = %f\n", __func__, UrgentBurstFactorChromaPre[k]);
		dml_print("DML::%s: cursor_bw_pre = %f\n", __func__, cursor_bw_pre[k]);
		dml_print("DML::%s: UrgentBurstFactorCursorPre = %f\n", __func__, UrgentBurstFactorCursorPre[k]);
		dml_print("DML::%s: meta_row_bandwidth = %f\n", __func__, meta_row_bandwidth[k]);
		dml_print("DML::%s: dpte_row_bandwidth          = %f\n", __func__, dpte_row_bandwidth[k]);
#endif
	}

	*TotalBandwidthNotIncludingMALLPrefetch = 0;
	for (dml_uint_t k = 0; k < NumberOfActiveSurfaces; ++k) {
		if (UseMALLForPStateChange[k] != dml_use_mall_pstate_change_phantom_pipe) {
			if (ImmediateFlipRequirement[k] != dml_immediate_flip_not_required)
				*TotalBandwidthNotIncludingMALLPrefetch = *TotalBandwidthNotIncludingMALLPrefetch + dml_max3(NumberOfDPP[k] * prefetch_vmrow_bw[k],
					NumberOfDPP[k] * final_flip_bw[k] + ReadBandwidthLuma[k] * UrgentBurstFactorLuma[k] + ReadBandwidthChroma[k] * UrgentBurstFactorChroma[k] + cursor_bw[k] * UrgentBurstFactorCursor[k],
					NumberOfDPP[k] * (final_flip_bw[k] + PrefetchBandwidthLuma[k] * UrgentBurstFactorLumaPre[k] + PrefetchBandwidthChroma[k] * UrgentBurstFactorChromaPre[k])
					+ cursor_bw_pre[k] * UrgentBurstFactorCursorPre[k]);
			else
				*TotalBandwidthNotIncludingMALLPrefetch = *TotalBandwidthNotIncludingMALLPrefetch + dml_max3(NumberOfDPP[k] * prefetch_vmrow_bw[k],
					NumberOfDPP[k] * (meta_row_bandwidth[k] + dpte_row_bandwidth[k])
					+ ReadBandwidthLuma[k] * UrgentBurstFactorLuma[k] + ReadBandwidthChroma[k] * UrgentBurstFactorChroma[k] + cursor_bw[k] * UrgentBurstFactorCursor[k],
					NumberOfDPP[k] * (PrefetchBandwidthLuma[k] * UrgentBurstFactorLumaPre[k] + PrefetchBandwidthChroma[k] * UrgentBurstFactorChromaPre[k])
					+ cursor_bw_pre[k] * UrgentBurstFactorCursorPre[k]);
		}
	}

	*ImmediateFlipBandwidthSupport = (*TotalBandwidth <= ReturnBW);
	*FractionOfUrgentBandwidth = *TotalBandwidth / ReturnBW;
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: ReturnBW = %f\n", __func__, ReturnBW);
	dml_print("DML::%s: TotalBandwidth = %f\n", __func__, *TotalBandwidth);
	dml_print("DML::%s: ImmediateFlipBandwidthSupport = %u\n", __func__, *ImmediateFlipBandwidthSupport);
#endif
}

static dml_uint_t MicroSecToVertLines(dml_uint_t num_us, dml_uint_t h_total, dml_float_t pixel_clock)
{
	dml_uint_t lines_time_in_ns = 1000.0 * (h_total * 1000.0) / (pixel_clock * 1000.0);

	return dml_ceil(1000.0 * num_us / lines_time_in_ns, 1.0);
}

/// @brief Calculate the maximum vstartup for mode support and mode programming consideration
///         Bounded by min of actual vblank and input vblank_nom, dont want vstartup/ready to start too early if actual vbllank is huge
static dml_uint_t CalculateMaxVStartup(
			dml_uint_t          plane_idx,
			dml_bool_t          ptoi_supported,
			dml_uint_t          vblank_nom_default_us,
			struct dml_timing_cfg_st  *timing,
			dml_float_t         write_back_delay_us)
{
	dml_uint_t vblank_size = 0;
	dml_uint_t max_vstartup_lines = 0;
	const dml_uint_t max_allowed_vblank_nom = 1023;

	dml_float_t line_time_us = (dml_float_t) timing->HTotal[plane_idx] / timing->PixelClock[plane_idx];
	dml_uint_t vblank_actual = timing->VTotal[plane_idx] - timing->VActive[plane_idx];

	dml_uint_t vblank_nom_default_in_line = MicroSecToVertLines(vblank_nom_default_us, timing->HTotal[plane_idx],
			timing->PixelClock[plane_idx]);
	dml_uint_t vblank_nom_input = (dml_uint_t)dml_min(vblank_actual, vblank_nom_default_in_line);

	// vblank_nom should not be smaller than (VSync (VTotal - VActive - VFrontPorch) + 2)
	// + 2 is because
	// 1 -> VStartup_start should be 1 line before VSync
	// 1 -> always reserve 1 line between start of VBlank to VStartup signal
	dml_uint_t vblank_nom_vsync_capped = dml_max(vblank_nom_input,
			timing->VTotal[plane_idx] - timing->VActive[plane_idx] - timing->VFrontPorch[plane_idx] + 2);
	dml_uint_t vblank_nom_max_allowed_capped = dml_min(vblank_nom_vsync_capped, max_allowed_vblank_nom);
	dml_uint_t vblank_avail = (vblank_nom_max_allowed_capped == 0) ?
			vblank_nom_default_in_line : vblank_nom_max_allowed_capped;

	vblank_size = (dml_uint_t) dml_min(vblank_actual, vblank_avail);

   if (timing->Interlace[plane_idx] && !ptoi_supported)
		max_vstartup_lines = (dml_uint_t) (dml_floor(vblank_size/2.0, 1.0));
	else
		max_vstartup_lines = vblank_size - (dml_uint_t) dml_max(1.0, dml_ceil(write_back_delay_us/line_time_us, 1.0));
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: plane_idx = %u\n", __func__, plane_idx);
	dml_print("DML::%s: VBlankNom = %u\n", __func__, timing->VBlankNom[plane_idx]);
	dml_print("DML::%s: vblank_nom_default_us = %u\n", __func__, vblank_nom_default_us);
	dml_print("DML::%s: line_time_us = %f\n", __func__, line_time_us);
	dml_print("DML::%s: vblank_actual = %u\n", __func__, vblank_actual);
	dml_print("DML::%s: vblank_avail = %u\n", __func__, vblank_avail);
	dml_print("DML::%s: max_vstartup_lines = %u\n", __func__, max_vstartup_lines);
#endif
	return max_vstartup_lines;
}

static void set_calculate_prefetch_schedule_params(struct display_mode_lib_st *mode_lib,
						   struct CalculatePrefetchSchedule_params_st *CalculatePrefetchSchedule_params,
						   dml_uint_t j,
						   dml_uint_t k)
{
				CalculatePrefetchSchedule_params->DSCDelay = mode_lib->ms.DSCDelayPerState[k];
				CalculatePrefetchSchedule_params->EnhancedPrefetchScheduleAccelerationFinal = mode_lib->ms.policy.EnhancedPrefetchScheduleAccelerationFinal;
				CalculatePrefetchSchedule_params->DPPCLKDelaySubtotalPlusCNVCFormater = mode_lib->ms.ip.dppclk_delay_subtotal + mode_lib->ms.ip.dppclk_delay_cnvc_formatter;
				CalculatePrefetchSchedule_params->DPPCLKDelaySCL = mode_lib->ms.ip.dppclk_delay_scl;
				CalculatePrefetchSchedule_params->DPPCLKDelaySCLLBOnly = mode_lib->ms.ip.dppclk_delay_scl_lb_only;
				CalculatePrefetchSchedule_params->DPPCLKDelayCNVCCursor = mode_lib->ms.ip.dppclk_delay_cnvc_cursor;
				CalculatePrefetchSchedule_params->DISPCLKDelaySubtotal = mode_lib->ms.ip.dispclk_delay_subtotal;
				CalculatePrefetchSchedule_params->DPP_RECOUT_WIDTH = (dml_uint_t)(mode_lib->ms.SwathWidthYThisState[k] / mode_lib->ms.cache_display_cfg.plane.HRatio[k]);
				CalculatePrefetchSchedule_params->OutputFormat = mode_lib->ms.cache_display_cfg.output.OutputFormat[k];
				CalculatePrefetchSchedule_params->MaxInterDCNTileRepeaters = mode_lib->ms.ip.max_inter_dcn_tile_repeaters;
				CalculatePrefetchSchedule_params->GPUVMPageTableLevels = mode_lib->ms.cache_display_cfg.plane.GPUVMMaxPageTableLevels;
				CalculatePrefetchSchedule_params->GPUVMEnable = mode_lib->ms.cache_display_cfg.plane.GPUVMEnable;
				CalculatePrefetchSchedule_params->HostVMEnable = mode_lib->ms.cache_display_cfg.plane.HostVMEnable;
				CalculatePrefetchSchedule_params->HostVMMaxNonCachedPageTableLevels = mode_lib->ms.cache_display_cfg.plane.HostVMMaxPageTableLevels;
				CalculatePrefetchSchedule_params->HostVMMinPageSize = mode_lib->ms.soc.hostvm_min_page_size_kbytes * 1024;
				CalculatePrefetchSchedule_params->DynamicMetadataEnable = mode_lib->ms.cache_display_cfg.plane.DynamicMetadataEnable[k];
				CalculatePrefetchSchedule_params->DynamicMetadataVMEnabled = mode_lib->ms.ip.dynamic_metadata_vm_enabled;
				CalculatePrefetchSchedule_params->DynamicMetadataLinesBeforeActiveRequired = mode_lib->ms.cache_display_cfg.plane.DynamicMetadataLinesBeforeActiveRequired[k];
				CalculatePrefetchSchedule_params->DynamicMetadataTransmittedBytes = mode_lib->ms.cache_display_cfg.plane.DynamicMetadataTransmittedBytes[k];
				CalculatePrefetchSchedule_params->UrgentLatency = mode_lib->ms.UrgLatency;
				CalculatePrefetchSchedule_params->UrgentExtraLatency = mode_lib->ms.ExtraLatency;
				CalculatePrefetchSchedule_params->TCalc = mode_lib->ms.TimeCalc;
				CalculatePrefetchSchedule_params->PDEAndMetaPTEBytesFrame = mode_lib->ms.PDEAndMetaPTEBytesPerFrame[j][k];
				CalculatePrefetchSchedule_params->MetaRowByte = mode_lib->ms.MetaRowBytes[j][k];
				CalculatePrefetchSchedule_params->PixelPTEBytesPerRow = mode_lib->ms.DPTEBytesPerRow[j][k];
				CalculatePrefetchSchedule_params->PrefetchSourceLinesY = mode_lib->ms.PrefetchLinesY[j][k];
				CalculatePrefetchSchedule_params->VInitPreFillY = mode_lib->ms.PrefillY[k];
				CalculatePrefetchSchedule_params->MaxNumSwathY = mode_lib->ms.MaxNumSwY[k];
				CalculatePrefetchSchedule_params->PrefetchSourceLinesC = mode_lib->ms.PrefetchLinesC[j][k];
				CalculatePrefetchSchedule_params->VInitPreFillC = mode_lib->ms.PrefillC[k];
				CalculatePrefetchSchedule_params->MaxNumSwathC = mode_lib->ms.MaxNumSwC[k];
				CalculatePrefetchSchedule_params->swath_width_luma_ub = mode_lib->ms.swath_width_luma_ub_this_state[k];
				CalculatePrefetchSchedule_params->swath_width_chroma_ub = mode_lib->ms.swath_width_chroma_ub_this_state[k];
				CalculatePrefetchSchedule_params->SwathHeightY = mode_lib->ms.SwathHeightYThisState[k];
				CalculatePrefetchSchedule_params->SwathHeightC = mode_lib->ms.SwathHeightCThisState[k];
				CalculatePrefetchSchedule_params->TWait = mode_lib->ms.TWait;
				CalculatePrefetchSchedule_params->DestinationLinesForPrefetch = &mode_lib->ms.LineTimesForPrefetch[k];
				CalculatePrefetchSchedule_params->DestinationLinesToRequestVMInVBlank = &mode_lib->ms.LinesForMetaPTE[k];
				CalculatePrefetchSchedule_params->DestinationLinesToRequestRowInVBlank = &mode_lib->ms.LinesForMetaAndDPTERow[k];
				CalculatePrefetchSchedule_params->VRatioPrefetchY = &mode_lib->ms.VRatioPreY[j][k];
				CalculatePrefetchSchedule_params->VRatioPrefetchC = &mode_lib->ms.VRatioPreC[j][k];
				CalculatePrefetchSchedule_params->RequiredPrefetchPixDataBWLuma = &mode_lib->ms.RequiredPrefetchPixelDataBWLuma[k];
				CalculatePrefetchSchedule_params->RequiredPrefetchPixDataBWChroma = &mode_lib->ms.RequiredPrefetchPixelDataBWChroma[k];
				CalculatePrefetchSchedule_params->NotEnoughTimeForDynamicMetadata = &mode_lib->ms.support.NoTimeForDynamicMetadata[j][k];
				CalculatePrefetchSchedule_params->Tno_bw = &mode_lib->ms.Tno_bw[k];
}

static void dml_prefetch_check(struct display_mode_lib_st *mode_lib)
{
	struct dml_core_mode_support_locals_st *s = &mode_lib->scratch.dml_core_mode_support_locals;
	struct CalculatePrefetchSchedule_params_st *CalculatePrefetchSchedule_params = &mode_lib->scratch.CalculatePrefetchSchedule_params;
	struct CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params_st *CalculateWatermarks_params = &mode_lib->scratch.CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params;
	struct DmlPipe *myPipe;
	dml_uint_t j, k;

	for (j = 0; j < 2; ++j) {
		mode_lib->ms.TimeCalc = 24 / mode_lib->ms.ProjectedDCFCLKDeepSleep[j];

		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			mode_lib->ms.NoOfDPPThisState[k] = mode_lib->ms.NoOfDPP[j][k];
			mode_lib->ms.swath_width_luma_ub_this_state[k] = mode_lib->ms.swath_width_luma_ub_all_states[j][k];
			mode_lib->ms.swath_width_chroma_ub_this_state[k] = mode_lib->ms.swath_width_chroma_ub_all_states[j][k];
			mode_lib->ms.SwathWidthYThisState[k] = mode_lib->ms.SwathWidthYAllStates[j][k];
			mode_lib->ms.SwathWidthCThisState[k] = mode_lib->ms.SwathWidthCAllStates[j][k];
			mode_lib->ms.SwathHeightYThisState[k] = mode_lib->ms.SwathHeightYAllStates[j][k];
			mode_lib->ms.SwathHeightCThisState[k] = mode_lib->ms.SwathHeightCAllStates[j][k];
			mode_lib->ms.UnboundedRequestEnabledThisState = mode_lib->ms.UnboundedRequestEnabledAllStates[j];
			mode_lib->ms.CompressedBufferSizeInkByteThisState = mode_lib->ms.CompressedBufferSizeInkByteAllStates[j];
			mode_lib->ms.DETBufferSizeInKByteThisState[k] = mode_lib->ms.DETBufferSizeInKByteAllStates[j][k];
			mode_lib->ms.DETBufferSizeYThisState[k] = mode_lib->ms.DETBufferSizeYAllStates[j][k];
			mode_lib->ms.DETBufferSizeCThisState[k] = mode_lib->ms.DETBufferSizeCAllStates[j][k];
		}

		mode_lib->ms.support.VActiveBandwithSupport[j] = CalculateVActiveBandwithSupport(
			mode_lib->ms.num_active_planes,
			mode_lib->ms.ReturnBWPerState[j],
			mode_lib->ms.NotUrgentLatencyHiding,
			mode_lib->ms.ReadBandwidthLuma,
			mode_lib->ms.ReadBandwidthChroma,
			mode_lib->ms.cursor_bw,
			mode_lib->ms.meta_row_bandwidth_this_state,
			mode_lib->ms.dpte_row_bandwidth_this_state,
			mode_lib->ms.NoOfDPPThisState,
			mode_lib->ms.UrgentBurstFactorLuma,
			mode_lib->ms.UrgentBurstFactorChroma,
			mode_lib->ms.UrgentBurstFactorCursor);

		s->VMDataOnlyReturnBWPerState = dml_get_return_bw_mbps_vm_only(
																	&mode_lib->ms.soc,
																	mode_lib->ms.state.use_ideal_dram_bw_strobe,
																	mode_lib->ms.cache_display_cfg.plane.HostVMEnable,
																	mode_lib->ms.DCFCLKState[j],
																	mode_lib->ms.state.fabricclk_mhz,
																	mode_lib->ms.state.dram_speed_mts);

		s->HostVMInefficiencyFactor = 1;
		if (mode_lib->ms.cache_display_cfg.plane.GPUVMEnable && mode_lib->ms.cache_display_cfg.plane.HostVMEnable)
			s->HostVMInefficiencyFactor = mode_lib->ms.ReturnBWPerState[j] / s->VMDataOnlyReturnBWPerState;

		mode_lib->ms.ExtraLatency = CalculateExtraLatency(
				mode_lib->ms.soc.round_trip_ping_latency_dcfclk_cycles,
				s->ReorderingBytes,
				mode_lib->ms.DCFCLKState[j],
				mode_lib->ms.TotalNumberOfActiveDPP[j],
				mode_lib->ms.ip.pixel_chunk_size_kbytes,
				mode_lib->ms.TotalNumberOfDCCActiveDPP[j],
				mode_lib->ms.ip.meta_chunk_size_kbytes,
				mode_lib->ms.ReturnBWPerState[j],
				mode_lib->ms.cache_display_cfg.plane.GPUVMEnable,
				mode_lib->ms.cache_display_cfg.plane.HostVMEnable,
				mode_lib->ms.num_active_planes,
				mode_lib->ms.NoOfDPPThisState,
				mode_lib->ms.dpte_group_bytes,
				s->HostVMInefficiencyFactor,
				mode_lib->ms.soc.hostvm_min_page_size_kbytes * 1024,
				mode_lib->ms.cache_display_cfg.plane.HostVMMaxPageTableLevels);

		s->NextMaxVStartup = s->MaxVStartupAllPlanes[j];
		s->MaxVStartup = 0;
		s->AllPrefetchModeTested = true;
		for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
			CalculatePrefetchMode(mode_lib->ms.policy.AllowForPStateChangeOrStutterInVBlank[k], &s->MinPrefetchMode[k], &s->MaxPrefetchMode[k]);
			s->NextPrefetchMode[k] = s->MinPrefetchMode[k];
		}

		do {
			s->MaxVStartup = s->NextMaxVStartup;
			s->AllPrefetchModeTested = true;

			for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
				mode_lib->ms.PrefetchMode[k] = s->NextPrefetchMode[k];
				mode_lib->ms.TWait = CalculateTWait(
								mode_lib->ms.PrefetchMode[k],
								mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange[k],
								mode_lib->ms.policy.SynchronizeDRRDisplaysForUCLKPStateChangeFinal,
								mode_lib->ms.cache_display_cfg.timing.DRRDisplay[k],
								mode_lib->ms.state.dram_clock_change_latency_us,
								mode_lib->ms.state.fclk_change_latency_us,
								mode_lib->ms.UrgLatency,
								mode_lib->ms.state.sr_enter_plus_exit_time_us);

				myPipe = &s->myPipe;
				myPipe->Dppclk = mode_lib->ms.RequiredDPPCLKPerSurface[j][k];
				myPipe->Dispclk = mode_lib->ms.RequiredDISPCLK[j];
				myPipe->PixelClock = mode_lib->ms.cache_display_cfg.timing.PixelClock[k];
				myPipe->DCFClkDeepSleep = mode_lib->ms.ProjectedDCFCLKDeepSleep[j];
				myPipe->DPPPerSurface = mode_lib->ms.NoOfDPP[j][k];
				myPipe->ScalerEnabled = mode_lib->ms.cache_display_cfg.plane.ScalerEnabled[k];
				myPipe->SourceScan = mode_lib->ms.cache_display_cfg.plane.SourceScan[k];
				myPipe->BlockWidth256BytesY = mode_lib->ms.Read256BlockWidthY[k];
				myPipe->BlockHeight256BytesY = mode_lib->ms.Read256BlockHeightY[k];
				myPipe->BlockWidth256BytesC = mode_lib->ms.Read256BlockWidthC[k];
				myPipe->BlockHeight256BytesC = mode_lib->ms.Read256BlockHeightC[k];
				myPipe->InterlaceEnable = mode_lib->ms.cache_display_cfg.timing.Interlace[k];
				myPipe->NumberOfCursors = mode_lib->ms.cache_display_cfg.plane.NumberOfCursors[k];
				myPipe->VBlank = mode_lib->ms.cache_display_cfg.timing.VTotal[k] - mode_lib->ms.cache_display_cfg.timing.VActive[k];
				myPipe->HTotal = mode_lib->ms.cache_display_cfg.timing.HTotal[k];
				myPipe->HActive = mode_lib->ms.cache_display_cfg.timing.HActive[k];
				myPipe->DCCEnable = mode_lib->ms.cache_display_cfg.surface.DCCEnable[k];
				myPipe->ODMMode = mode_lib->ms.ODMModePerState[k];
				myPipe->SourcePixelFormat = mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k];
				myPipe->BytePerPixelY = mode_lib->ms.BytePerPixelY[k];
				myPipe->BytePerPixelC = mode_lib->ms.BytePerPixelC[k];
				myPipe->ProgressiveToInterlaceUnitInOPP = mode_lib->ms.ip.ptoi_supported;

#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: Calling CalculatePrefetchSchedule for j=%u, k=%u\n", __func__, j, k);
				dml_print("DML::%s: MaximumVStartup = %u\n", __func__, s->MaximumVStartup[j][k]);
				dml_print("DML::%s: MaxVStartup = %u\n", __func__, s->MaxVStartup);
				dml_print("DML::%s: NextPrefetchMode = %u\n", __func__, s->NextPrefetchMode[k]);
				dml_print("DML::%s: AllowForPStateChangeOrStutterInVBlank = %u\n", __func__, mode_lib->ms.policy.AllowForPStateChangeOrStutterInVBlank[k]);
				dml_print("DML::%s: PrefetchMode = %u\n", __func__, mode_lib->ms.PrefetchMode[k]);
#endif

				CalculatePrefetchSchedule_params->HostVMInefficiencyFactor = s->HostVMInefficiencyFactor;
				CalculatePrefetchSchedule_params->myPipe = myPipe;
				CalculatePrefetchSchedule_params->VStartup = (dml_uint_t)(dml_min(s->MaxVStartup, s->MaximumVStartup[j][k]));
				CalculatePrefetchSchedule_params->MaxVStartup = s->MaximumVStartup[j][k];
				CalculatePrefetchSchedule_params->DSTXAfterScaler = &s->DSTXAfterScaler[k];
				CalculatePrefetchSchedule_params->DSTYAfterScaler = &s->DSTYAfterScaler[k];
				CalculatePrefetchSchedule_params->prefetch_vmrow_bw = &mode_lib->ms.prefetch_vmrow_bw[k];
				CalculatePrefetchSchedule_params->Tdmdl_vm = &s->dummy_single[0];
				CalculatePrefetchSchedule_params->Tdmdl = &s->dummy_single[1];
				CalculatePrefetchSchedule_params->TSetup = &s->dummy_single[2];
				CalculatePrefetchSchedule_params->VUpdateOffsetPix = &s->dummy_integer[0];
				CalculatePrefetchSchedule_params->VUpdateWidthPix = &s->dummy_integer[1];
				CalculatePrefetchSchedule_params->VReadyOffsetPix = &s->dummy_integer[2];

				set_calculate_prefetch_schedule_params(mode_lib, CalculatePrefetchSchedule_params, j, k);

				mode_lib->ms.support.NoTimeForPrefetch[j][k] =
								CalculatePrefetchSchedule(&mode_lib->scratch,
								CalculatePrefetchSchedule_params);
			}

			for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
					CalculateUrgentBurstFactor(
							mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange[k],
							mode_lib->ms.swath_width_luma_ub_this_state[k],
							mode_lib->ms.swath_width_chroma_ub_this_state[k],
							mode_lib->ms.SwathHeightYThisState[k],
							mode_lib->ms.SwathHeightCThisState[k],
							mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k],
							mode_lib->ms.UrgLatency,
							mode_lib->ms.ip.cursor_buffer_size,
							mode_lib->ms.cache_display_cfg.plane.CursorWidth[k],
							mode_lib->ms.cache_display_cfg.plane.CursorBPP[k],
							mode_lib->ms.VRatioPreY[j][k],
							mode_lib->ms.VRatioPreC[j][k],
							mode_lib->ms.BytePerPixelInDETY[k],
							mode_lib->ms.BytePerPixelInDETC[k],
							mode_lib->ms.DETBufferSizeYThisState[k],
							mode_lib->ms.DETBufferSizeCThisState[k],
							/* Output */
							&mode_lib->ms.UrgentBurstFactorCursorPre[k],
							&mode_lib->ms.UrgentBurstFactorLumaPre[k],
							&mode_lib->ms.UrgentBurstFactorChroma[k],
							&mode_lib->ms.NotUrgentLatencyHidingPre[k]);

					mode_lib->ms.cursor_bw_pre[k] = mode_lib->ms.cache_display_cfg.plane.NumberOfCursors[k] * mode_lib->ms.cache_display_cfg.plane.CursorWidth[k] *
													mode_lib->ms.cache_display_cfg.plane.CursorBPP[k] / 8.0 / (mode_lib->ms.cache_display_cfg.timing.HTotal[k] /
													mode_lib->ms.cache_display_cfg.timing.PixelClock[k]) * mode_lib->ms.VRatioPreY[j][k];
			}

			{
			CalculatePrefetchBandwithSupport(
				mode_lib->ms.num_active_planes,
				mode_lib->ms.ReturnBWPerState[j],
				mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange,
				mode_lib->ms.NotUrgentLatencyHidingPre,
				mode_lib->ms.ReadBandwidthLuma,
				mode_lib->ms.ReadBandwidthChroma,
				mode_lib->ms.RequiredPrefetchPixelDataBWLuma,
				mode_lib->ms.RequiredPrefetchPixelDataBWChroma,
				mode_lib->ms.cursor_bw,
				mode_lib->ms.meta_row_bandwidth_this_state,
				mode_lib->ms.dpte_row_bandwidth_this_state,
				mode_lib->ms.cursor_bw_pre,
				mode_lib->ms.prefetch_vmrow_bw,
				mode_lib->ms.NoOfDPPThisState,
				mode_lib->ms.UrgentBurstFactorLuma,
				mode_lib->ms.UrgentBurstFactorChroma,
				mode_lib->ms.UrgentBurstFactorCursor,
				mode_lib->ms.UrgentBurstFactorLumaPre,
				mode_lib->ms.UrgentBurstFactorChromaPre,
				mode_lib->ms.UrgentBurstFactorCursorPre,

				/* output */
				&s->dummy_single[0], // dml_float_t *PrefetchBandwidth
				&s->dummy_single[1], // dml_float_t *PrefetchBandwidthNotIncludingMALLPrefetch
				&mode_lib->mp.FractionOfUrgentBandwidth, // dml_float_t *FractionOfUrgentBandwidth
				&mode_lib->ms.support.PrefetchSupported[j]);
			}

			for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
				if (mode_lib->ms.LineTimesForPrefetch[k] < 2.0
					|| mode_lib->ms.LinesForMetaPTE[k] >= 32.0
					|| mode_lib->ms.LinesForMetaAndDPTERow[k] >= 16.0
					|| mode_lib->ms.support.NoTimeForPrefetch[j][k] == true) {
						mode_lib->ms.support.PrefetchSupported[j] = false;
				}
			}

			mode_lib->ms.support.DynamicMetadataSupported[j] = true;
			for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
				if (mode_lib->ms.support.NoTimeForDynamicMetadata[j][k] == true) {
					mode_lib->ms.support.DynamicMetadataSupported[j] = false;
				}
			}

			mode_lib->ms.support.VRatioInPrefetchSupported[j] = true;
			for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
				if (mode_lib->ms.support.NoTimeForPrefetch[j][k] == true ||
					mode_lib->ms.VRatioPreY[j][k] > __DML_MAX_VRATIO_PRE_ENHANCE_PREFETCH_ACC__ ||
					mode_lib->ms.VRatioPreC[j][k] > __DML_MAX_VRATIO_PRE_ENHANCE_PREFETCH_ACC__ ||
					((s->MaxVStartup < s->MaximumVStartup[j][k] || mode_lib->ms.policy.EnhancedPrefetchScheduleAccelerationFinal == 0) &&
						(mode_lib->ms.VRatioPreY[j][k] > __DML_MAX_VRATIO_PRE__ || mode_lib->ms.VRatioPreC[j][k] > __DML_MAX_VRATIO_PRE__))) {
							mode_lib->ms.support.VRatioInPrefetchSupported[j] = false;
				}
			}

			s->AnyLinesForVMOrRowTooLarge = false;
			for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
				if (mode_lib->ms.LinesForMetaAndDPTERow[k] >= 16 || mode_lib->ms.LinesForMetaPTE[k] >= 32) {
					s->AnyLinesForVMOrRowTooLarge = true;
				}
			}

			if (mode_lib->ms.support.PrefetchSupported[j] == true && mode_lib->ms.support.VRatioInPrefetchSupported[j] == true) {
				mode_lib->ms.BandwidthAvailableForImmediateFlip = CalculateBandwidthAvailableForImmediateFlip(
						mode_lib->ms.num_active_planes,
						mode_lib->ms.ReturnBWPerState[j],
						mode_lib->ms.ReadBandwidthLuma,
						mode_lib->ms.ReadBandwidthChroma,
						mode_lib->ms.RequiredPrefetchPixelDataBWLuma,
						mode_lib->ms.RequiredPrefetchPixelDataBWChroma,
						mode_lib->ms.cursor_bw,
						mode_lib->ms.cursor_bw_pre,
						mode_lib->ms.NoOfDPPThisState,
						mode_lib->ms.UrgentBurstFactorLuma,
						mode_lib->ms.UrgentBurstFactorChroma,
						mode_lib->ms.UrgentBurstFactorCursor,
						mode_lib->ms.UrgentBurstFactorLumaPre,
						mode_lib->ms.UrgentBurstFactorChromaPre,
						mode_lib->ms.UrgentBurstFactorCursorPre);

				mode_lib->ms.TotImmediateFlipBytes = 0;
				for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
					if (!(mode_lib->ms.policy.ImmediateFlipRequirement[k] == dml_immediate_flip_not_required)) {
						mode_lib->ms.TotImmediateFlipBytes = mode_lib->ms.TotImmediateFlipBytes + mode_lib->ms.NoOfDPP[j][k] * mode_lib->ms.PDEAndMetaPTEBytesPerFrame[j][k] + mode_lib->ms.MetaRowBytes[j][k];
						if (mode_lib->ms.use_one_row_for_frame_flip[j][k]) {
							mode_lib->ms.TotImmediateFlipBytes = mode_lib->ms.TotImmediateFlipBytes + mode_lib->ms.NoOfDPP[j][k] * (2 * mode_lib->ms.DPTEBytesPerRow[j][k]);
						} else {
							mode_lib->ms.TotImmediateFlipBytes = mode_lib->ms.TotImmediateFlipBytes + mode_lib->ms.NoOfDPP[j][k] * mode_lib->ms.DPTEBytesPerRow[j][k];
						}
					}
				}

				for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
					CalculateFlipSchedule(
						s->HostVMInefficiencyFactor,
						mode_lib->ms.ExtraLatency,
						mode_lib->ms.UrgLatency,
						mode_lib->ms.cache_display_cfg.plane.GPUVMMaxPageTableLevels,
						mode_lib->ms.cache_display_cfg.plane.HostVMEnable,
						mode_lib->ms.cache_display_cfg.plane.HostVMMaxPageTableLevels,
						mode_lib->ms.cache_display_cfg.plane.GPUVMEnable,
						mode_lib->ms.soc.hostvm_min_page_size_kbytes * 1024,
						mode_lib->ms.PDEAndMetaPTEBytesPerFrame[j][k],
						mode_lib->ms.MetaRowBytes[j][k],
						mode_lib->ms.DPTEBytesPerRow[j][k],
						mode_lib->ms.BandwidthAvailableForImmediateFlip,
						mode_lib->ms.TotImmediateFlipBytes,
						mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k],
						(mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k]),
						mode_lib->ms.cache_display_cfg.plane.VRatio[k],
						mode_lib->ms.cache_display_cfg.plane.VRatioChroma[k],
						mode_lib->ms.Tno_bw[k],
						mode_lib->ms.cache_display_cfg.surface.DCCEnable[k],
						mode_lib->ms.dpte_row_height[k],
						mode_lib->ms.meta_row_height[k],
						mode_lib->ms.dpte_row_height_chroma[k],
						mode_lib->ms.meta_row_height_chroma[k],
						mode_lib->ms.use_one_row_for_frame_flip[j][k], // 24

						/* Output */
						&mode_lib->ms.DestinationLinesToRequestVMInImmediateFlip[k],
						&mode_lib->ms.DestinationLinesToRequestRowInImmediateFlip[k],
						&mode_lib->ms.final_flip_bw[k],
						&mode_lib->ms.ImmediateFlipSupportedForPipe[k]);
				}

				{
				CalculateImmediateFlipBandwithSupport(mode_lib->ms.num_active_planes,
													mode_lib->ms.ReturnBWPerState[j],
													mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange,
													mode_lib->ms.policy.ImmediateFlipRequirement,
													mode_lib->ms.final_flip_bw,
													mode_lib->ms.ReadBandwidthLuma,
													mode_lib->ms.ReadBandwidthChroma,
													mode_lib->ms.RequiredPrefetchPixelDataBWLuma,
													mode_lib->ms.RequiredPrefetchPixelDataBWChroma,
													mode_lib->ms.cursor_bw,
													mode_lib->ms.meta_row_bandwidth_this_state,
													mode_lib->ms.dpte_row_bandwidth_this_state,
													mode_lib->ms.cursor_bw_pre,
													mode_lib->ms.prefetch_vmrow_bw,
													mode_lib->ms.NoOfDPP[j], // VBA_ERROR DPPPerSurface is not assigned at this point, should use NoOfDpp here
													mode_lib->ms.UrgentBurstFactorLuma,
													mode_lib->ms.UrgentBurstFactorChroma,
													mode_lib->ms.UrgentBurstFactorCursor,
													mode_lib->ms.UrgentBurstFactorLumaPre,
													mode_lib->ms.UrgentBurstFactorChromaPre,
													mode_lib->ms.UrgentBurstFactorCursorPre,

													/* output */
													&s->dummy_single[0], // dml_float_t *TotalBandwidth
													&s->dummy_single[1], // dml_float_t *TotalBandwidthNotIncludingMALLPrefetch
													&s->dummy_single[2], // dml_float_t *FractionOfUrgentBandwidth
													&mode_lib->ms.support.ImmediateFlipSupportedForState[j]); // dml_bool_t *ImmediateFlipBandwidthSupport
				}

				for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
					if (!(mode_lib->ms.policy.ImmediateFlipRequirement[k] == dml_immediate_flip_not_required) && (mode_lib->ms.ImmediateFlipSupportedForPipe[k] == false))
						mode_lib->ms.support.ImmediateFlipSupportedForState[j] = false;
				}

			} else { // if prefetch not support, assume iflip not supported
				mode_lib->ms.support.ImmediateFlipSupportedForState[j] = false;
			}

			if (s->MaxVStartup <= __DML_VBA_MIN_VSTARTUP__ || s->AnyLinesForVMOrRowTooLarge == false) {
				s->NextMaxVStartup = s->MaxVStartupAllPlanes[j];
				for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
					s->NextPrefetchMode[k] = s->NextPrefetchMode[k] + 1;

					if (s->NextPrefetchMode[k] <= s->MaxPrefetchMode[k])
						s->AllPrefetchModeTested = false;
				}
			} else {
				s->NextMaxVStartup = s->NextMaxVStartup - 1;
			}
		} while (!((mode_lib->ms.support.PrefetchSupported[j] == true && mode_lib->ms.support.DynamicMetadataSupported[j] == true &&
					mode_lib->ms.support.VRatioInPrefetchSupported[j] == true &&
					// consider flip support is okay if when there is no hostvm and the user does't require a iflip OR the flip bw is ok
					// If there is hostvm, DCN needs to support iflip for invalidation
					((s->ImmediateFlipRequiredFinal) || mode_lib->ms.support.ImmediateFlipSupportedForState[j] == true)) ||
					(s->NextMaxVStartup == s->MaxVStartupAllPlanes[j] && s->AllPrefetchModeTested)));

		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			mode_lib->ms.use_one_row_for_frame_this_state[k] = mode_lib->ms.use_one_row_for_frame[j][k];
		}

		s->mSOCParameters.UrgentLatency = mode_lib->ms.UrgLatency;
		s->mSOCParameters.ExtraLatency = mode_lib->ms.ExtraLatency;
		s->mSOCParameters.WritebackLatency = mode_lib->ms.state.writeback_latency_us;
		s->mSOCParameters.DRAMClockChangeLatency = mode_lib->ms.state.dram_clock_change_latency_us;
		s->mSOCParameters.FCLKChangeLatency = mode_lib->ms.state.fclk_change_latency_us;
		s->mSOCParameters.SRExitTime = mode_lib->ms.state.sr_exit_time_us;
		s->mSOCParameters.SREnterPlusExitTime = mode_lib->ms.state.sr_enter_plus_exit_time_us;
		s->mSOCParameters.SRExitZ8Time = mode_lib->ms.state.sr_exit_z8_time_us;
		s->mSOCParameters.SREnterPlusExitZ8Time = mode_lib->ms.state.sr_enter_plus_exit_z8_time_us;
		s->mSOCParameters.USRRetrainingLatency = mode_lib->ms.state.usr_retraining_latency_us;
		s->mSOCParameters.SMNLatency = mode_lib->ms.soc.smn_latency_us;

		CalculateWatermarks_params->USRRetrainingRequiredFinal = mode_lib->ms.policy.USRRetrainingRequiredFinal;
		CalculateWatermarks_params->UseMALLForPStateChange = mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange;
		CalculateWatermarks_params->PrefetchMode = mode_lib->ms.PrefetchMode;
		CalculateWatermarks_params->NumberOfActiveSurfaces = mode_lib->ms.num_active_planes;
		CalculateWatermarks_params->MaxLineBufferLines = mode_lib->ms.ip.max_line_buffer_lines;
		CalculateWatermarks_params->LineBufferSize = mode_lib->ms.ip.line_buffer_size_bits;
		CalculateWatermarks_params->WritebackInterfaceBufferSize = mode_lib->ms.ip.writeback_interface_buffer_size_kbytes;
		CalculateWatermarks_params->DCFCLK = mode_lib->ms.DCFCLKState[j];
		CalculateWatermarks_params->ReturnBW = mode_lib->ms.ReturnBWPerState[j];
		CalculateWatermarks_params->SynchronizeTimingsFinal = mode_lib->ms.policy.SynchronizeTimingsFinal;
		CalculateWatermarks_params->SynchronizeDRRDisplaysForUCLKPStateChangeFinal = mode_lib->ms.policy.SynchronizeDRRDisplaysForUCLKPStateChangeFinal;
		CalculateWatermarks_params->DRRDisplay = mode_lib->ms.cache_display_cfg.timing.DRRDisplay;
		CalculateWatermarks_params->dpte_group_bytes = mode_lib->ms.dpte_group_bytes;
		CalculateWatermarks_params->meta_row_height = mode_lib->ms.meta_row_height;
		CalculateWatermarks_params->meta_row_height_chroma = mode_lib->ms.meta_row_height_chroma;
		CalculateWatermarks_params->mmSOCParameters = s->mSOCParameters;
		CalculateWatermarks_params->WritebackChunkSize = mode_lib->ms.ip.writeback_chunk_size_kbytes;
		CalculateWatermarks_params->SOCCLK = mode_lib->ms.state.socclk_mhz;
		CalculateWatermarks_params->DCFClkDeepSleep = mode_lib->ms.ProjectedDCFCLKDeepSleep[j];
		CalculateWatermarks_params->DETBufferSizeY = mode_lib->ms.DETBufferSizeYThisState;
		CalculateWatermarks_params->DETBufferSizeC = mode_lib->ms.DETBufferSizeCThisState;
		CalculateWatermarks_params->SwathHeightY = mode_lib->ms.SwathHeightYThisState;
		CalculateWatermarks_params->SwathHeightC = mode_lib->ms.SwathHeightCThisState;
		CalculateWatermarks_params->LBBitPerPixel = mode_lib->ms.cache_display_cfg.plane.LBBitPerPixel;
		CalculateWatermarks_params->SwathWidthY = mode_lib->ms.SwathWidthYThisState;
		CalculateWatermarks_params->SwathWidthC = mode_lib->ms.SwathWidthCThisState;
		CalculateWatermarks_params->HRatio = mode_lib->ms.cache_display_cfg.plane.HRatio;
		CalculateWatermarks_params->HRatioChroma = mode_lib->ms.cache_display_cfg.plane.HRatioChroma;
		CalculateWatermarks_params->VTaps = mode_lib->ms.cache_display_cfg.plane.VTaps;
		CalculateWatermarks_params->VTapsChroma = mode_lib->ms.cache_display_cfg.plane.VTapsChroma;
		CalculateWatermarks_params->VRatio = mode_lib->ms.cache_display_cfg.plane.VRatio;
		CalculateWatermarks_params->VRatioChroma = mode_lib->ms.cache_display_cfg.plane.VRatioChroma;
		CalculateWatermarks_params->HTotal = mode_lib->ms.cache_display_cfg.timing.HTotal;
		CalculateWatermarks_params->VTotal = mode_lib->ms.cache_display_cfg.timing.VTotal;
		CalculateWatermarks_params->VActive = mode_lib->ms.cache_display_cfg.timing.VActive;
		CalculateWatermarks_params->PixelClock = mode_lib->ms.cache_display_cfg.timing.PixelClock;
		CalculateWatermarks_params->BlendingAndTiming = mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming;
		CalculateWatermarks_params->DPPPerSurface = mode_lib->ms.NoOfDPPThisState;
		CalculateWatermarks_params->BytePerPixelDETY = mode_lib->ms.BytePerPixelInDETY;
		CalculateWatermarks_params->BytePerPixelDETC = mode_lib->ms.BytePerPixelInDETC;
		CalculateWatermarks_params->DSTXAfterScaler = s->DSTXAfterScaler;
		CalculateWatermarks_params->DSTYAfterScaler = s->DSTYAfterScaler;
		CalculateWatermarks_params->WritebackEnable = mode_lib->ms.cache_display_cfg.writeback.WritebackEnable;
		CalculateWatermarks_params->WritebackPixelFormat = mode_lib->ms.cache_display_cfg.writeback.WritebackPixelFormat;
		CalculateWatermarks_params->WritebackDestinationWidth = mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationWidth;
		CalculateWatermarks_params->WritebackDestinationHeight = mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationHeight;
		CalculateWatermarks_params->WritebackSourceHeight = mode_lib->ms.cache_display_cfg.writeback.WritebackSourceHeight;
		CalculateWatermarks_params->UnboundedRequestEnabled = mode_lib->ms.UnboundedRequestEnabledThisState;
		CalculateWatermarks_params->CompressedBufferSizeInkByte = mode_lib->ms.CompressedBufferSizeInkByteThisState;

		// Output
		CalculateWatermarks_params->Watermark = &s->dummy_watermark; // Watermarks *Watermark
		CalculateWatermarks_params->DRAMClockChangeSupport = &mode_lib->ms.support.DRAMClockChangeSupport[j];
		CalculateWatermarks_params->MaxActiveDRAMClockChangeLatencySupported = &s->dummy_single_array[0]; // dml_float_t *MaxActiveDRAMClockChangeLatencySupported[]
		CalculateWatermarks_params->SubViewportLinesNeededInMALL = &mode_lib->ms.SubViewportLinesNeededInMALL[j]; // dml_uint_t SubViewportLinesNeededInMALL[]
		CalculateWatermarks_params->FCLKChangeSupport = &mode_lib->ms.support.FCLKChangeSupport[j];
		CalculateWatermarks_params->MaxActiveFCLKChangeLatencySupported = &s->dummy_single[0]; // dml_float_t *MaxActiveFCLKChangeLatencySupported
		CalculateWatermarks_params->USRRetrainingSupport = &mode_lib->ms.support.USRRetrainingSupport[j];
		CalculateWatermarks_params->ActiveDRAMClockChangeLatencyMargin = mode_lib->ms.support.ActiveDRAMClockChangeLatencyMargin;

		CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport(&mode_lib->scratch,
			CalculateWatermarks_params);

	} // for j
}

/// @brief The Mode Support function.
dml_bool_t dml_core_mode_support(struct display_mode_lib_st *mode_lib)
{
	struct dml_core_mode_support_locals_st *s = &mode_lib->scratch.dml_core_mode_support_locals;
	struct UseMinimumDCFCLK_params_st *UseMinimumDCFCLK_params = &mode_lib->scratch.UseMinimumDCFCLK_params;
	struct CalculateSwathAndDETConfiguration_params_st *CalculateSwathAndDETConfiguration_params = &mode_lib->scratch.CalculateSwathAndDETConfiguration_params;
	struct CalculateVMRowAndSwath_params_st *CalculateVMRowAndSwath_params = &mode_lib->scratch.CalculateVMRowAndSwath_params;

	dml_uint_t j, k, m;

	mode_lib->ms.num_active_planes = dml_get_num_active_planes(&mode_lib->ms.cache_display_cfg);
	dml_print("DML::%s: num_active_planes = %u\n", __func__, mode_lib->ms.num_active_planes);

	CalculateMaxDETAndMinCompressedBufferSize(
								mode_lib->ms.ip.config_return_buffer_size_in_kbytes,
								mode_lib->ms.ip.config_return_buffer_segment_size_in_kbytes,
								mode_lib->ms.ip.rob_buffer_size_kbytes,
								mode_lib->ms.ip.max_num_dpp,
								mode_lib->ms.policy.NomDETInKByteOverrideEnable,   // VBA_DELTA
								mode_lib->ms.policy.NomDETInKByteOverrideValue,    // VBA_DELTA

								/* Output */
								&mode_lib->ms.MaxTotalDETInKByte,
								&mode_lib->ms.NomDETInKByte,
								&mode_lib->ms.MinCompressedBufferSizeInKByte);

	PixelClockAdjustmentForProgressiveToInterlaceUnit(&mode_lib->ms.cache_display_cfg, mode_lib->ms.ip.ptoi_supported);


	/*MODE SUPPORT, VOLTAGE STATE AND SOC CONFIGURATION*/

	/*Scale Ratio, taps Support Check*/
	mode_lib->ms.support.ScaleRatioAndTapsSupport = true;
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (mode_lib->ms.cache_display_cfg.plane.ScalerEnabled[k] == false
				&& ((mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_444_64
						&& mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_444_32
						&& mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_444_16
						&& mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_mono_16
						&& mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_mono_8
						&& mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_rgbe
						&& mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_rgbe_alpha)
						|| mode_lib->ms.cache_display_cfg.plane.HRatio[k] != 1.0
						|| mode_lib->ms.cache_display_cfg.plane.HTaps[k] != 1.0
						|| mode_lib->ms.cache_display_cfg.plane.VRatio[k] != 1.0
						|| mode_lib->ms.cache_display_cfg.plane.VTaps[k] != 1.0)) {
			mode_lib->ms.support.ScaleRatioAndTapsSupport = false;
		} else if (mode_lib->ms.cache_display_cfg.plane.VTaps[k] < 1.0 || mode_lib->ms.cache_display_cfg.plane.VTaps[k] > 8.0
				|| mode_lib->ms.cache_display_cfg.plane.HTaps[k] < 1.0 || mode_lib->ms.cache_display_cfg.plane.HTaps[k] > 8.0
				|| (mode_lib->ms.cache_display_cfg.plane.HTaps[k] > 1.0 && (mode_lib->ms.cache_display_cfg.plane.HTaps[k] % 2) == 1)
				|| mode_lib->ms.cache_display_cfg.plane.HRatio[k] > mode_lib->ms.ip.max_hscl_ratio
				|| mode_lib->ms.cache_display_cfg.plane.VRatio[k] > mode_lib->ms.ip.max_vscl_ratio
				|| mode_lib->ms.cache_display_cfg.plane.HRatio[k] > mode_lib->ms.cache_display_cfg.plane.HTaps[k]
				|| mode_lib->ms.cache_display_cfg.plane.VRatio[k] > mode_lib->ms.cache_display_cfg.plane.VTaps[k]
				|| (mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_444_64
					&& mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_444_32
					&& mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_444_16
					&& mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_mono_16
					&& mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_mono_8
					&& mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_rgbe
					&& (mode_lib->ms.cache_display_cfg.plane.VTapsChroma[k] < 1 || mode_lib->ms.cache_display_cfg.plane.VTapsChroma[k] > 8 || mode_lib->ms.cache_display_cfg.plane.HTapsChroma[k] < 1 || mode_lib->ms.cache_display_cfg.plane.HTapsChroma[k] > 8 ||
						(mode_lib->ms.cache_display_cfg.plane.HTapsChroma[k] > 1 && mode_lib->ms.cache_display_cfg.plane.HTapsChroma[k] % 2 == 1) ||
					mode_lib->ms.cache_display_cfg.plane.HRatioChroma[k] > mode_lib->ms.ip.max_hscl_ratio ||
					mode_lib->ms.cache_display_cfg.plane.VRatioChroma[k] > mode_lib->ms.ip.max_vscl_ratio ||
					mode_lib->ms.cache_display_cfg.plane.HRatioChroma[k] > mode_lib->ms.cache_display_cfg.plane.HTapsChroma[k] ||
					mode_lib->ms.cache_display_cfg.plane.VRatioChroma[k] > mode_lib->ms.cache_display_cfg.plane.VTapsChroma[k]))) {
			mode_lib->ms.support.ScaleRatioAndTapsSupport = false;
		}
	}

	/*Source Format, Pixel Format and Scan Support Check*/
	mode_lib->ms.support.SourceFormatPixelAndScanSupport = true;
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
			if (mode_lib->ms.cache_display_cfg.surface.SurfaceTiling[k] == dml_sw_linear && (!(!dml_is_vertical_rotation(mode_lib->ms.cache_display_cfg.plane.SourceScan[k])) || mode_lib->ms.cache_display_cfg.surface.DCCEnable[k] == true)) {
			mode_lib->ms.support.SourceFormatPixelAndScanSupport = false;
		}
	}

	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		CalculateBytePerPixelAndBlockSizes(
								mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k],
								mode_lib->ms.cache_display_cfg.surface.SurfaceTiling[k],

								/* Output */
								&mode_lib->ms.BytePerPixelY[k],
								&mode_lib->ms.BytePerPixelC[k],
								&mode_lib->ms.BytePerPixelInDETY[k],
								&mode_lib->ms.BytePerPixelInDETC[k],
								&mode_lib->ms.Read256BlockHeightY[k],
								&mode_lib->ms.Read256BlockHeightC[k],
								&mode_lib->ms.Read256BlockWidthY[k],
								&mode_lib->ms.Read256BlockWidthC[k],
								&mode_lib->ms.MacroTileHeightY[k],
								&mode_lib->ms.MacroTileHeightC[k],
								&mode_lib->ms.MacroTileWidthY[k],
								&mode_lib->ms.MacroTileWidthC[k]);
	}

	/*Bandwidth Support Check*/
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (!dml_is_vertical_rotation(mode_lib->ms.cache_display_cfg.plane.SourceScan[k])) {
			mode_lib->ms.SwathWidthYSingleDPP[k] = mode_lib->ms.cache_display_cfg.plane.ViewportWidth[k];
			mode_lib->ms.SwathWidthCSingleDPP[k] = mode_lib->ms.cache_display_cfg.plane.ViewportWidthChroma[k];
		} else {
			mode_lib->ms.SwathWidthYSingleDPP[k] = mode_lib->ms.cache_display_cfg.plane.ViewportHeight[k];
			mode_lib->ms.SwathWidthCSingleDPP[k] = mode_lib->ms.cache_display_cfg.plane.ViewportHeightChroma[k];
		}
	}
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		mode_lib->ms.ReadBandwidthLuma[k] = mode_lib->ms.SwathWidthYSingleDPP[k] * dml_ceil(mode_lib->ms.BytePerPixelInDETY[k], 1.0) / (mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k]) * mode_lib->ms.cache_display_cfg.plane.VRatio[k];
		mode_lib->ms.ReadBandwidthChroma[k] = mode_lib->ms.SwathWidthYSingleDPP[k] / 2 * dml_ceil(mode_lib->ms.BytePerPixelInDETC[k], 2.0) / (mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k]) * mode_lib->ms.cache_display_cfg.plane.VRatio[k] / 2.0;
	}
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (mode_lib->ms.cache_display_cfg.writeback.WritebackEnable[k] == true
				&& mode_lib->ms.cache_display_cfg.writeback.WritebackPixelFormat[k] == dml_444_64) {
			mode_lib->ms.WriteBandwidth[k] = mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationWidth[k]
					* mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationHeight[k]
					/ (mode_lib->ms.cache_display_cfg.writeback.WritebackSourceHeight[k]
							* mode_lib->ms.cache_display_cfg.timing.HTotal[k]
							/ mode_lib->ms.cache_display_cfg.timing.PixelClock[k]) * 8.0;
		} else if (mode_lib->ms.cache_display_cfg.writeback.WritebackEnable[k] == true) {
			mode_lib->ms.WriteBandwidth[k] = mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationWidth[k]
					* mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationHeight[k]
					/ (mode_lib->ms.cache_display_cfg.writeback.WritebackSourceHeight[k]
							* mode_lib->ms.cache_display_cfg.timing.HTotal[k]
							/ mode_lib->ms.cache_display_cfg.timing.PixelClock[k]) * 4.0;
		} else {
			mode_lib->ms.WriteBandwidth[k] = 0.0;
		}
	}

	/*Writeback Latency support check*/
	mode_lib->ms.support.WritebackLatencySupport = true;
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (mode_lib->ms.cache_display_cfg.writeback.WritebackEnable[k] == true &&
			(mode_lib->ms.WriteBandwidth[k] > mode_lib->ms.ip.writeback_interface_buffer_size_kbytes * 1024 / mode_lib->ms.state.writeback_latency_us)) {
			mode_lib->ms.support.WritebackLatencySupport = false;
		}
	}

	/*Writeback Mode Support Check*/
	s->TotalNumberOfActiveWriteback = 0;
	for (k = 0; k <= (dml_uint_t) mode_lib->ms.num_active_planes - 1; k++) {
		if (mode_lib->ms.cache_display_cfg.writeback.WritebackEnable[k] == true) {
			s->TotalNumberOfActiveWriteback = s->TotalNumberOfActiveWriteback + 1;
		}
   }

	mode_lib->ms.support.EnoughWritebackUnits = 1;
	if (s->TotalNumberOfActiveWriteback > (dml_uint_t) mode_lib->ms.ip.max_num_wb) {
		mode_lib->ms.support.EnoughWritebackUnits = false;
	}

	/*Writeback Scale Ratio and Taps Support Check*/
	mode_lib->ms.support.WritebackScaleRatioAndTapsSupport = true;
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (mode_lib->ms.cache_display_cfg.writeback.WritebackEnable[k] == true) {
			if (mode_lib->ms.cache_display_cfg.writeback.WritebackHRatio[k] > mode_lib->ms.ip.writeback_max_hscl_ratio
				|| mode_lib->ms.cache_display_cfg.writeback.WritebackVRatio[k] > mode_lib->ms.ip.writeback_max_vscl_ratio
				|| mode_lib->ms.cache_display_cfg.writeback.WritebackHRatio[k] < mode_lib->ms.ip.writeback_min_hscl_ratio
				|| mode_lib->ms.cache_display_cfg.writeback.WritebackVRatio[k] < mode_lib->ms.ip.writeback_min_vscl_ratio
				|| mode_lib->ms.cache_display_cfg.writeback.WritebackHTaps[k] > (dml_uint_t) mode_lib->ms.ip.writeback_max_hscl_taps
				|| mode_lib->ms.cache_display_cfg.writeback.WritebackVTaps[k] > (dml_uint_t) mode_lib->ms.ip.writeback_max_vscl_taps
				|| mode_lib->ms.cache_display_cfg.writeback.WritebackHRatio[k] > (dml_uint_t) mode_lib->ms.cache_display_cfg.writeback.WritebackHTaps[k]
				|| mode_lib->ms.cache_display_cfg.writeback.WritebackVRatio[k] > (dml_uint_t) mode_lib->ms.cache_display_cfg.writeback.WritebackVTaps[k]
				|| (mode_lib->ms.cache_display_cfg.writeback.WritebackHTaps[k] > 2.0 && ((mode_lib->ms.cache_display_cfg.writeback.WritebackHTaps[k] % 2) == 1))) {
				mode_lib->ms.support.WritebackScaleRatioAndTapsSupport = false;
			}
			if (2.0 * mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationWidth[k] * (mode_lib->ms.cache_display_cfg.writeback.WritebackVTaps[k] - 1) * 57 > mode_lib->ms.ip.writeback_line_buffer_buffer_size) {
				mode_lib->ms.support.WritebackScaleRatioAndTapsSupport = false;
			}
		}
	}

	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		CalculateSinglePipeDPPCLKAndSCLThroughput(
				mode_lib->ms.cache_display_cfg.plane.HRatio[k],
				mode_lib->ms.cache_display_cfg.plane.HRatioChroma[k],
				mode_lib->ms.cache_display_cfg.plane.VRatio[k],
				mode_lib->ms.cache_display_cfg.plane.VRatioChroma[k],
				mode_lib->ms.ip.max_dchub_pscl_bw_pix_per_clk,
				mode_lib->ms.ip.max_pscl_lb_bw_pix_per_clk,
				mode_lib->ms.cache_display_cfg.timing.PixelClock[k],
				mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k],
				mode_lib->ms.cache_display_cfg.plane.HTaps[k],
				mode_lib->ms.cache_display_cfg.plane.HTapsChroma[k],
				mode_lib->ms.cache_display_cfg.plane.VTaps[k],
				mode_lib->ms.cache_display_cfg.plane.VTapsChroma[k],
				/* Output */
				&mode_lib->ms.PSCL_FACTOR[k],
				&mode_lib->ms.PSCL_FACTOR_CHROMA[k],
				&mode_lib->ms.MinDPPCLKUsingSingleDPP[k]);
	}

	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (mode_lib->ms.cache_display_cfg.surface.SurfaceTiling[k] == dml_sw_linear) {
			s->MaximumSwathWidthSupportLuma = 8192;
		} else if (!dml_is_vertical_rotation(mode_lib->ms.cache_display_cfg.plane.SourceScan[k]) && mode_lib->ms.BytePerPixelC[k] > 0 && mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_rgbe_alpha) {
			s->MaximumSwathWidthSupportLuma = 7680;
		} else if (dml_is_vertical_rotation(mode_lib->ms.cache_display_cfg.plane.SourceScan[k]) && mode_lib->ms.BytePerPixelC[k] > 0 && mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_rgbe_alpha) {
			s->MaximumSwathWidthSupportLuma = 4320;
		} else if (mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] == dml_rgbe_alpha) {
			s->MaximumSwathWidthSupportLuma = 3840;
		} else if (dml_is_vertical_rotation(mode_lib->ms.cache_display_cfg.plane.SourceScan[k]) && mode_lib->ms.BytePerPixelY[k] == 8 && mode_lib->ms.cache_display_cfg.surface.DCCEnable[k] == true) {
			s->MaximumSwathWidthSupportLuma = 3072;
		} else {
			s->MaximumSwathWidthSupportLuma = 6144;
		}

       if (mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] == dml_420_8 || mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] == dml_420_10 || mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] == dml_420_12) {
			s->MaximumSwathWidthSupportChroma = (dml_uint_t)(s->MaximumSwathWidthSupportLuma / 2.0);
		} else {
			s->MaximumSwathWidthSupportChroma = s->MaximumSwathWidthSupportLuma;
       }
		mode_lib->ms.MaximumSwathWidthInLineBufferLuma = mode_lib->ms.ip.line_buffer_size_bits * dml_max(mode_lib->ms.cache_display_cfg.plane.HRatio[k], 1.0) / mode_lib->ms.cache_display_cfg.plane.LBBitPerPixel[k] /
															(mode_lib->ms.cache_display_cfg.plane.VTaps[k] + dml_max(dml_ceil(mode_lib->ms.cache_display_cfg.plane.VRatio[k], 1.0) - 2, 0.0));
		if (mode_lib->ms.BytePerPixelC[k] == 0.0) {
			mode_lib->ms.MaximumSwathWidthInLineBufferChroma = 0;
		} else {
			mode_lib->ms.MaximumSwathWidthInLineBufferChroma =
							mode_lib->ms.ip.line_buffer_size_bits
									* dml_max(mode_lib->ms.cache_display_cfg.plane.HRatioChroma[k], 1.0)
									/ mode_lib->ms.cache_display_cfg.plane.LBBitPerPixel[k]
									/ (mode_lib->ms.cache_display_cfg.plane.VTapsChroma[k]
									+ dml_max(dml_ceil(mode_lib->ms.cache_display_cfg.plane.VRatioChroma[k], 1.0) - 2, 0.0));
		}
		mode_lib->ms.MaximumSwathWidthLuma[k] = dml_min(s->MaximumSwathWidthSupportLuma, mode_lib->ms.MaximumSwathWidthInLineBufferLuma);
		mode_lib->ms.MaximumSwathWidthChroma[k] = dml_min(s->MaximumSwathWidthSupportChroma, mode_lib->ms.MaximumSwathWidthInLineBufferChroma);
	}

	/*Number Of DSC Slices*/
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == k) {
			if (mode_lib->ms.cache_display_cfg.output.PixelClockBackEnd[k] > 4800) {
				mode_lib->ms.support.NumberOfDSCSlices[k] = (dml_uint_t)(dml_ceil(mode_lib->ms.cache_display_cfg.output.PixelClockBackEnd[k] / 600, 4));
			} else if (mode_lib->ms.cache_display_cfg.output.PixelClockBackEnd[k] > 2400) {
				mode_lib->ms.support.NumberOfDSCSlices[k] = 8;
			} else if (mode_lib->ms.cache_display_cfg.output.PixelClockBackEnd[k] > 1200) {
				mode_lib->ms.support.NumberOfDSCSlices[k] = 4;
			} else if (mode_lib->ms.cache_display_cfg.output.PixelClockBackEnd[k] > 340) {
				mode_lib->ms.support.NumberOfDSCSlices[k] = 2;
			} else {
				mode_lib->ms.support.NumberOfDSCSlices[k] = 1;
			}
		} else {
			mode_lib->ms.support.NumberOfDSCSlices[k] = 0;
		}
	}

	CalculateSwathAndDETConfiguration_params->DETSizeOverride = mode_lib->ms.cache_display_cfg.plane.DETSizeOverride;
	CalculateSwathAndDETConfiguration_params->UseMALLForPStateChange = mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange;
	CalculateSwathAndDETConfiguration_params->ConfigReturnBufferSizeInKByte = mode_lib->ms.ip.config_return_buffer_size_in_kbytes;
	CalculateSwathAndDETConfiguration_params->ROBBufferSizeInKByte = mode_lib->ms.ip.rob_buffer_size_kbytes;
	CalculateSwathAndDETConfiguration_params->MaxTotalDETInKByte = mode_lib->ms.MaxTotalDETInKByte;
	CalculateSwathAndDETConfiguration_params->MinCompressedBufferSizeInKByte = mode_lib->ms.MinCompressedBufferSizeInKByte;
	CalculateSwathAndDETConfiguration_params->PixelChunkSizeInKByte = mode_lib->ms.ip.pixel_chunk_size_kbytes;
	CalculateSwathAndDETConfiguration_params->ForceSingleDPP = 1;
	CalculateSwathAndDETConfiguration_params->NumberOfActiveSurfaces = mode_lib->ms.num_active_planes;
	CalculateSwathAndDETConfiguration_params->nomDETInKByte = mode_lib->ms.NomDETInKByte;
	CalculateSwathAndDETConfiguration_params->UseUnboundedRequestingFinal = mode_lib->ms.policy.UseUnboundedRequesting;
	CalculateSwathAndDETConfiguration_params->ConfigReturnBufferSegmentSizeInkByte = mode_lib->ms.ip.config_return_buffer_segment_size_in_kbytes;
	CalculateSwathAndDETConfiguration_params->CompressedBufferSegmentSizeInkByteFinal = mode_lib->ms.ip.compressed_buffer_segment_size_in_kbytes;
	CalculateSwathAndDETConfiguration_params->Output = mode_lib->ms.cache_display_cfg.output.OutputEncoder;
	CalculateSwathAndDETConfiguration_params->ReadBandwidthLuma = mode_lib->ms.ReadBandwidthLuma;
	CalculateSwathAndDETConfiguration_params->ReadBandwidthChroma = mode_lib->ms.ReadBandwidthChroma;
	CalculateSwathAndDETConfiguration_params->MaximumSwathWidthLuma = mode_lib->ms.MaximumSwathWidthLuma;
	CalculateSwathAndDETConfiguration_params->MaximumSwathWidthChroma = mode_lib->ms.MaximumSwathWidthChroma;
	CalculateSwathAndDETConfiguration_params->SourceScan = mode_lib->ms.cache_display_cfg.plane.SourceScan;
	CalculateSwathAndDETConfiguration_params->ViewportStationary = mode_lib->ms.cache_display_cfg.plane.ViewportStationary;
	CalculateSwathAndDETConfiguration_params->SourcePixelFormat = mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat;
	CalculateSwathAndDETConfiguration_params->SurfaceTiling = mode_lib->ms.cache_display_cfg.surface.SurfaceTiling;
	CalculateSwathAndDETConfiguration_params->ViewportWidth = mode_lib->ms.cache_display_cfg.plane.ViewportWidth;
	CalculateSwathAndDETConfiguration_params->ViewportHeight = mode_lib->ms.cache_display_cfg.plane.ViewportHeight;
	CalculateSwathAndDETConfiguration_params->ViewportXStart = mode_lib->ms.cache_display_cfg.plane.ViewportXStart;
	CalculateSwathAndDETConfiguration_params->ViewportYStart = mode_lib->ms.cache_display_cfg.plane.ViewportYStart;
	CalculateSwathAndDETConfiguration_params->ViewportXStartC = mode_lib->ms.cache_display_cfg.plane.ViewportXStartC;
	CalculateSwathAndDETConfiguration_params->ViewportYStartC = mode_lib->ms.cache_display_cfg.plane.ViewportYStartC;
	CalculateSwathAndDETConfiguration_params->SurfaceWidthY = mode_lib->ms.cache_display_cfg.surface.SurfaceWidthY;
	CalculateSwathAndDETConfiguration_params->SurfaceWidthC = mode_lib->ms.cache_display_cfg.surface.SurfaceWidthC;
	CalculateSwathAndDETConfiguration_params->SurfaceHeightY = mode_lib->ms.cache_display_cfg.surface.SurfaceHeightY;
	CalculateSwathAndDETConfiguration_params->SurfaceHeightC = mode_lib->ms.cache_display_cfg.surface.SurfaceHeightC;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockHeightY = mode_lib->ms.Read256BlockHeightY;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockHeightC = mode_lib->ms.Read256BlockHeightC;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockWidthY = mode_lib->ms.Read256BlockWidthY;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockWidthC = mode_lib->ms.Read256BlockWidthC;
	CalculateSwathAndDETConfiguration_params->ODMMode = s->dummy_odm_mode;
	CalculateSwathAndDETConfiguration_params->BlendingAndTiming = mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming;
	CalculateSwathAndDETConfiguration_params->BytePerPixY = mode_lib->ms.BytePerPixelY;
	CalculateSwathAndDETConfiguration_params->BytePerPixC = mode_lib->ms.BytePerPixelC;
	CalculateSwathAndDETConfiguration_params->BytePerPixDETY = mode_lib->ms.BytePerPixelInDETY;
	CalculateSwathAndDETConfiguration_params->BytePerPixDETC = mode_lib->ms.BytePerPixelInDETC;
	CalculateSwathAndDETConfiguration_params->HActive = mode_lib->ms.cache_display_cfg.timing.HActive;
	CalculateSwathAndDETConfiguration_params->HRatio = mode_lib->ms.cache_display_cfg.plane.HRatio;
	CalculateSwathAndDETConfiguration_params->HRatioChroma = mode_lib->ms.cache_display_cfg.plane.HRatioChroma;
	CalculateSwathAndDETConfiguration_params->DPPPerSurface = s->dummy_integer_array[0];
	CalculateSwathAndDETConfiguration_params->swath_width_luma_ub = s->dummy_integer_array[1];
	CalculateSwathAndDETConfiguration_params->swath_width_chroma_ub = s->dummy_integer_array[2];
	CalculateSwathAndDETConfiguration_params->SwathWidth = s->dummy_integer_array[3];
	CalculateSwathAndDETConfiguration_params->SwathWidthChroma = s->dummy_integer_array[4];
	CalculateSwathAndDETConfiguration_params->SwathHeightY = s->dummy_integer_array[5];
	CalculateSwathAndDETConfiguration_params->SwathHeightC = s->dummy_integer_array[6];
	CalculateSwathAndDETConfiguration_params->DETBufferSizeInKByte = s->dummy_integer_array[7];
	CalculateSwathAndDETConfiguration_params->DETBufferSizeY = mode_lib->ms.DETBufferSizeY;
	CalculateSwathAndDETConfiguration_params->DETBufferSizeC = mode_lib->ms.DETBufferSizeC;
	CalculateSwathAndDETConfiguration_params->UnboundedRequestEnabled = &s->dummy_boolean[0];
	CalculateSwathAndDETConfiguration_params->compbuf_reserved_space_64b = &s->dummy_integer[2];
	CalculateSwathAndDETConfiguration_params->compbuf_reserved_space_zs = &s->dummy_integer[1];
	CalculateSwathAndDETConfiguration_params->CompressedBufferSizeInkByte = &s->dummy_integer[0];
	CalculateSwathAndDETConfiguration_params->ViewportSizeSupportPerSurface = mode_lib->ms.SingleDPPViewportSizeSupportPerSurface;
	CalculateSwathAndDETConfiguration_params->ViewportSizeSupport = &s->dummy_boolean[1];

	CalculateSwathAndDETConfiguration(&mode_lib->scratch,
	CalculateSwathAndDETConfiguration_params); /* dml_bool_t *ViewportSizeSupport */

	s->MPCCombineMethodAsNeededForPStateChangeAndVoltage = false;
	s->MPCCombineMethodAsPossible = false;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.policy.MPCCombineUse[k] == dml_mpc_as_needed_for_pstate_and_voltage)
			s->MPCCombineMethodAsNeededForPStateChangeAndVoltage = true;
		if (mode_lib->ms.policy.MPCCombineUse[k] == dml_mpc_as_possible)
			s->MPCCombineMethodAsPossible = true;
	}
	mode_lib->ms.support.MPCCombineMethodIncompatible = s->MPCCombineMethodAsNeededForPStateChangeAndVoltage && s->MPCCombineMethodAsPossible;

	for (j = 0; j < 2; j++) {
		mode_lib->ms.TotalNumberOfActiveDPP[j] = 0;
		mode_lib->ms.support.TotalAvailablePipesSupport[j] = true;

		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			CalculateODMMode(
					mode_lib->ms.ip.maximum_pixels_per_line_per_dsc_unit,
					mode_lib->ms.cache_display_cfg.timing.HActive[k],
					mode_lib->ms.cache_display_cfg.output.OutputEncoder[k],
					mode_lib->ms.cache_display_cfg.output.OutputFormat[k],
					mode_lib->ms.policy.ODMUse[k],
					mode_lib->ms.state.dispclk_mhz,
					mode_lib->ms.max_state.dispclk_mhz,
					false, // DSCEnable
					mode_lib->ms.TotalNumberOfActiveDPP[j],
					mode_lib->ms.ip.max_num_dpp,
					mode_lib->ms.cache_display_cfg.timing.PixelClock[k],
					mode_lib->ms.soc.dcn_downspread_percent,
					mode_lib->ms.ip.dispclk_ramp_margin_percent,
					mode_lib->ms.soc.dispclk_dppclk_vco_speed_mhz,

					/* Output */
					&s->TotalAvailablePipesSupportNoDSC,
					&s->NumberOfDPPNoDSC,
					&s->ODMModeNoDSC,
					&s->RequiredDISPCLKPerSurfaceNoDSC);

			CalculateODMMode(
					mode_lib->ms.ip.maximum_pixels_per_line_per_dsc_unit,
					mode_lib->ms.cache_display_cfg.timing.HActive[k],
					mode_lib->ms.cache_display_cfg.output.OutputEncoder[k],
					mode_lib->ms.cache_display_cfg.output.OutputFormat[k],
					mode_lib->ms.policy.ODMUse[k],
					mode_lib->ms.state.dispclk_mhz,
					mode_lib->ms.max_state.dispclk_mhz,
					true, // DSCEnable
					mode_lib->ms.TotalNumberOfActiveDPP[j],
					mode_lib->ms.ip.max_num_dpp,
					mode_lib->ms.cache_display_cfg.timing.PixelClock[k],
					mode_lib->ms.soc.dcn_downspread_percent,
					mode_lib->ms.ip.dispclk_ramp_margin_percent,
					mode_lib->ms.soc.dispclk_dppclk_vco_speed_mhz,

					/* Output */
					&s->TotalAvailablePipesSupportDSC,
					&s->NumberOfDPPDSC,
					&s->ODMModeDSC,
					&s->RequiredDISPCLKPerSurfaceDSC);

			CalculateOutputLink(
					mode_lib->ms.state.phyclk_mhz,
					mode_lib->ms.state.phyclk_d18_mhz,
					mode_lib->ms.state.phyclk_d32_mhz,
					mode_lib->ms.soc.phy_downspread_percent,
					(mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == k),
					mode_lib->ms.cache_display_cfg.output.OutputEncoder[k],
					mode_lib->ms.cache_display_cfg.output.OutputFormat[k],
					mode_lib->ms.cache_display_cfg.timing.HTotal[k],
					mode_lib->ms.cache_display_cfg.timing.HActive[k],
					mode_lib->ms.cache_display_cfg.output.PixelClockBackEnd[k],
					mode_lib->ms.cache_display_cfg.output.ForcedOutputLinkBPP[k],
					mode_lib->ms.cache_display_cfg.output.DSCInputBitPerComponent[k],
					mode_lib->ms.support.NumberOfDSCSlices[k],
					mode_lib->ms.cache_display_cfg.output.AudioSampleRate[k],
					mode_lib->ms.cache_display_cfg.output.AudioSampleLayout[k],
					s->ODMModeNoDSC,
					s->ODMModeDSC,
					mode_lib->ms.cache_display_cfg.output.DSCEnable[k],
					mode_lib->ms.cache_display_cfg.output.OutputLinkDPLanes[k],
					mode_lib->ms.cache_display_cfg.output.OutputLinkDPRate[k],

					/* Output */
					&mode_lib->ms.RequiresDSC[k],
					&mode_lib->ms.RequiresFEC[k],
					&mode_lib->ms.OutputBppPerState[k],
					&mode_lib->ms.OutputTypePerState[k], // VBA_DELTA, VBA uses a string to represent type and rate, but DML uses enum, don't want to rely on strng
					&mode_lib->ms.OutputRatePerState[k],
					&mode_lib->ms.RequiredSlots[k]);

			if (mode_lib->ms.RequiresDSC[k] == false) {
				mode_lib->ms.ODMModePerState[k] = s->ODMModeNoDSC;
				mode_lib->ms.RequiredDISPCLKPerSurface[j][k] = s->RequiredDISPCLKPerSurfaceNoDSC;
				if (!s->TotalAvailablePipesSupportNoDSC)
					mode_lib->ms.support.TotalAvailablePipesSupport[j] = false;
				mode_lib->ms.TotalNumberOfActiveDPP[j] = mode_lib->ms.TotalNumberOfActiveDPP[j] + s->NumberOfDPPNoDSC;
			} else {
				mode_lib->ms.ODMModePerState[k] = s->ODMModeDSC;
				mode_lib->ms.RequiredDISPCLKPerSurface[j][k] = s->RequiredDISPCLKPerSurfaceDSC;
				if (!s->TotalAvailablePipesSupportDSC)
					mode_lib->ms.support.TotalAvailablePipesSupport[j] = false;
				mode_lib->ms.TotalNumberOfActiveDPP[j] = mode_lib->ms.TotalNumberOfActiveDPP[j] + s->NumberOfDPPDSC;
			}
		}

		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			if (mode_lib->ms.ODMModePerState[k] == dml_odm_mode_combine_4to1) {
				mode_lib->ms.MPCCombine[j][k] = false;
				mode_lib->ms.NoOfDPP[j][k] = 4;
			} else if (mode_lib->ms.ODMModePerState[k] == dml_odm_mode_combine_2to1) {
				mode_lib->ms.MPCCombine[j][k] = false;
				mode_lib->ms.NoOfDPP[j][k] = 2;
			} else if (mode_lib->ms.policy.MPCCombineUse[k] == dml_mpc_disabled) {
				mode_lib->ms.MPCCombine[j][k] = false;
				mode_lib->ms.NoOfDPP[j][k] = 1;
			} else if (RoundToDFSGranularity(mode_lib->ms.MinDPPCLKUsingSingleDPP[k] * (1 + mode_lib->ms.soc.dcn_downspread_percent / 100),
											1, mode_lib->ms.soc.dispclk_dppclk_vco_speed_mhz) <= mode_lib->ms.state.dppclk_mhz &&
											mode_lib->ms.SingleDPPViewportSizeSupportPerSurface[k] == true) {
				mode_lib->ms.MPCCombine[j][k] = false;
				mode_lib->ms.NoOfDPP[j][k] = 1;
			} else if (mode_lib->ms.TotalNumberOfActiveDPP[j] < (dml_uint_t) mode_lib->ms.ip.max_num_dpp) {
				mode_lib->ms.MPCCombine[j][k] = true;
				mode_lib->ms.NoOfDPP[j][k] = 2;
				mode_lib->ms.TotalNumberOfActiveDPP[j] = (dml_uint_t) mode_lib->ms.TotalNumberOfActiveDPP[j] + 1;
			} else {
				mode_lib->ms.MPCCombine[j][k] = false;
				mode_lib->ms.NoOfDPP[j][k] = 1;
				mode_lib->ms.support.TotalAvailablePipesSupport[j] = false;
			}
		}

		mode_lib->ms.TotalNumberOfSingleDPPSurfaces[j] = 0;
		s->NoChromaOrLinear = true;
		for (k = 0; k < (dml_uint_t) mode_lib->ms.num_active_planes; ++k) {
			if (mode_lib->ms.NoOfDPP[j][k] == 1)
				mode_lib->ms.TotalNumberOfSingleDPPSurfaces[j] = mode_lib->ms.TotalNumberOfSingleDPPSurfaces[j] + 1;
			if (mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] == dml_420_8
					|| mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] == dml_420_10
					|| mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] == dml_420_12
					|| mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] == dml_rgbe_alpha
					|| mode_lib->ms.cache_display_cfg.surface.SurfaceTiling[k] == dml_sw_linear) {
				s->NoChromaOrLinear = false;
			}
		}

		if (j == 1 && !UnboundedRequest(mode_lib->ms.policy.UseUnboundedRequesting,
				mode_lib->ms.TotalNumberOfActiveDPP[j], s->NoChromaOrLinear,
				mode_lib->ms.cache_display_cfg.output.OutputEncoder[0])) {
			while (!(mode_lib->ms.TotalNumberOfActiveDPP[j] >= (dml_uint_t) mode_lib->ms.ip.max_num_dpp || mode_lib->ms.TotalNumberOfSingleDPPSurfaces[j] == 0)) {
				s->BWOfNonCombinedSurfaceOfMaximumBandwidth = 0;
				s->NumberOfNonCombinedSurfaceOfMaximumBandwidth = 0;
				for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
					if (mode_lib->ms.policy.MPCCombineUse[k] != dml_mpc_disabled && mode_lib->ms.policy.MPCCombineUse[k] != dml_mpc_as_needed_for_voltage &&
						mode_lib->ms.ReadBandwidthLuma[k] + mode_lib->ms.ReadBandwidthChroma[k] > s->BWOfNonCombinedSurfaceOfMaximumBandwidth &&
						(mode_lib->ms.ODMModePerState[k] != dml_odm_mode_combine_2to1 && mode_lib->ms.ODMModePerState[k] != dml_odm_mode_combine_4to1) &&
						mode_lib->ms.MPCCombine[j][k] == false) {
						s->BWOfNonCombinedSurfaceOfMaximumBandwidth = mode_lib->ms.ReadBandwidthLuma[k] + mode_lib->ms.ReadBandwidthChroma[k];
						s->NumberOfNonCombinedSurfaceOfMaximumBandwidth = k;
					}
				}
				mode_lib->ms.MPCCombine[j][s->NumberOfNonCombinedSurfaceOfMaximumBandwidth] = true;
				mode_lib->ms.NoOfDPP[j][s->NumberOfNonCombinedSurfaceOfMaximumBandwidth] = 2;
				mode_lib->ms.TotalNumberOfActiveDPP[j] = mode_lib->ms.TotalNumberOfActiveDPP[j] + 1;
				mode_lib->ms.TotalNumberOfSingleDPPSurfaces[j] = mode_lib->ms.TotalNumberOfSingleDPPSurfaces[j] - 1;
			}
		}

		//DISPCLK/DPPCLK
		mode_lib->ms.WritebackRequiredDISPCLK = 0;
		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			if (mode_lib->ms.cache_display_cfg.writeback.WritebackEnable[k]) {
				mode_lib->ms.WritebackRequiredDISPCLK = dml_max(mode_lib->ms.WritebackRequiredDISPCLK,
																	CalculateWriteBackDISPCLK(mode_lib->ms.cache_display_cfg.writeback.WritebackPixelFormat[k],
																							mode_lib->ms.cache_display_cfg.timing.PixelClock[k],
																							mode_lib->ms.cache_display_cfg.writeback.WritebackHRatio[k],
																							mode_lib->ms.cache_display_cfg.writeback.WritebackVRatio[k],
																							mode_lib->ms.cache_display_cfg.writeback.WritebackHTaps[k],
																							mode_lib->ms.cache_display_cfg.writeback.WritebackVTaps[k],
																							mode_lib->ms.cache_display_cfg.writeback.WritebackSourceWidth[k],
																							mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationWidth[k],
																							mode_lib->ms.cache_display_cfg.timing.HTotal[k],
																							mode_lib->ms.ip.writeback_line_buffer_buffer_size,
																							mode_lib->ms.soc.dispclk_dppclk_vco_speed_mhz));
			}
		}

		 mode_lib->ms.RequiredDISPCLK[j] = mode_lib->ms.WritebackRequiredDISPCLK;
		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			mode_lib->ms.RequiredDISPCLK[j] = dml_max(mode_lib->ms.RequiredDISPCLK[j], mode_lib->ms.RequiredDISPCLKPerSurface[j][k]);
		}

		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			mode_lib->ms.NoOfDPPThisState[k] = mode_lib->ms.NoOfDPP[j][k];
		}

		CalculateDPPCLK(mode_lib->ms.num_active_planes,
					mode_lib->ms.soc.dcn_downspread_percent,
					mode_lib->ms.soc.dispclk_dppclk_vco_speed_mhz,
					mode_lib->ms.MinDPPCLKUsingSingleDPP,
					mode_lib->ms.NoOfDPPThisState,
					/* Output */
					&mode_lib->ms.GlobalDPPCLK,
					mode_lib->ms.RequiredDPPCLKThisState);

		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			mode_lib->ms.RequiredDPPCLKPerSurface[j][k] = mode_lib->ms.RequiredDPPCLKThisState[k];
		}

		mode_lib->ms.support.DISPCLK_DPPCLK_Support[j] = !((mode_lib->ms.RequiredDISPCLK[j] > mode_lib->ms.state.dispclk_mhz) || (mode_lib->ms.GlobalDPPCLK > mode_lib->ms.state.dppclk_mhz));

		if (mode_lib->ms.TotalNumberOfActiveDPP[j] > (dml_uint_t) mode_lib->ms.ip.max_num_dpp) {
			mode_lib->ms.support.TotalAvailablePipesSupport[j] = false;
		}
	} // j

	/* Total Available OTG, HDMIFRL, DP Support Check */
	s->TotalNumberOfActiveOTG = 0;
	s->TotalNumberOfActiveHDMIFRL = 0;
	s->TotalNumberOfActiveDP2p0 = 0;
	s->TotalNumberOfActiveDP2p0Outputs = 0;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == k) {
			s->TotalNumberOfActiveOTG = s->TotalNumberOfActiveOTG + 1;
			if (mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_hdmifrl)
				s->TotalNumberOfActiveHDMIFRL = s->TotalNumberOfActiveHDMIFRL + 1;
			if (mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_dp2p0) {
				s->TotalNumberOfActiveDP2p0 = s->TotalNumberOfActiveDP2p0 + 1;
				if (mode_lib->ms.cache_display_cfg.output.OutputMultistreamId[k] == k || mode_lib->ms.cache_display_cfg.output.OutputMultistreamEn[k] == false) {
					s->TotalNumberOfActiveDP2p0Outputs = s->TotalNumberOfActiveDP2p0Outputs + 1;
				}
			}
		}
	}

	mode_lib->ms.support.NumberOfOTGSupport      = (s->TotalNumberOfActiveOTG <= (dml_uint_t) mode_lib->ms.ip.max_num_otg);
	mode_lib->ms.support.NumberOfHDMIFRLSupport  = (s->TotalNumberOfActiveHDMIFRL <= (dml_uint_t) mode_lib->ms.ip.max_num_hdmi_frl_outputs);
	mode_lib->ms.support.NumberOfDP2p0Support    = (s->TotalNumberOfActiveDP2p0 <= (dml_uint_t) mode_lib->ms.ip.max_num_dp2p0_streams && s->TotalNumberOfActiveDP2p0Outputs <= (dml_uint_t) mode_lib->ms.ip.max_num_dp2p0_outputs);

	/* Display IO and DSC Support Check */
	mode_lib->ms.support.NonsupportedDSCInputBPC = false;
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (mode_lib->ms.cache_display_cfg.output.OutputDisabled[k] == false &&
			!(mode_lib->ms.cache_display_cfg.output.DSCInputBitPerComponent[k] == 12.0
				|| mode_lib->ms.cache_display_cfg.output.DSCInputBitPerComponent[k] == 10.0
				|| mode_lib->ms.cache_display_cfg.output.DSCInputBitPerComponent[k] == 8.0
				|| mode_lib->ms.cache_display_cfg.output.DSCInputBitPerComponent[k] > (dml_uint_t) mode_lib->ms.ip.maximum_dsc_bits_per_component
				)) {
			mode_lib->ms.support.NonsupportedDSCInputBPC = true;
		}
	}

	mode_lib->ms.support.ExceededMultistreamSlots = false;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.cache_display_cfg.output.OutputMultistreamId[k] == k) {
			s->TotalSlots = mode_lib->ms.RequiredSlots[k];
			for (j = 0; j < mode_lib->ms.num_active_planes; ++j) {
				if (mode_lib->ms.cache_display_cfg.output.OutputMultistreamId[j] == k)
					s->TotalSlots = s->TotalSlots + mode_lib->ms.RequiredSlots[j];
			}
			if (mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_dp && s->TotalSlots > 63)
				mode_lib->ms.support.ExceededMultistreamSlots = true;
			if (mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_dp2p0 && s->TotalSlots > 64)
				mode_lib->ms.support.ExceededMultistreamSlots = true;
		}
	}
	mode_lib->ms.support.LinkCapacitySupport = true;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.cache_display_cfg.output.OutputDisabled[k] == false &&
			mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == k && (mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_dp || mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_dp2p0 || mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_edp ||
			mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_hdmi || mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_hdmifrl) && mode_lib->ms.OutputBppPerState[k] == 0) {
			mode_lib->ms.support.LinkCapacitySupport = false;
		}
	}

	mode_lib->ms.support.P2IWith420 = false;
	mode_lib->ms.support.DSCOnlyIfNecessaryWithBPP = false;
	mode_lib->ms.support.DSC422NativeNotSupported = false;
	mode_lib->ms.support.LinkRateDoesNotMatchDPVersion = false;
	mode_lib->ms.support.LinkRateForMultistreamNotIndicated = false;
	mode_lib->ms.support.BPPForMultistreamNotIndicated = false;
	mode_lib->ms.support.MultistreamWithHDMIOreDP = false;
	mode_lib->ms.support.MSOOrODMSplitWithNonDPLink = false;
	mode_lib->ms.support.NotEnoughLanesForMSO = false;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == k && (mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_dp || mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_dp2p0 || mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_edp ||
														mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_hdmi || mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_hdmifrl)) {
			if (mode_lib->ms.cache_display_cfg.output.OutputFormat[k] == dml_420 && mode_lib->ms.cache_display_cfg.timing.Interlace[k] == 1 && mode_lib->ms.ip.ptoi_supported == true)
				mode_lib->ms.support.P2IWith420 = true;

			if (mode_lib->ms.cache_display_cfg.output.DSCEnable[k] == dml_dsc_enable_if_necessary && mode_lib->ms.cache_display_cfg.output.ForcedOutputLinkBPP[k] != 0)
				mode_lib->ms.support.DSCOnlyIfNecessaryWithBPP = true;
			if ((mode_lib->ms.cache_display_cfg.output.DSCEnable[k] == dml_dsc_enable || mode_lib->ms.cache_display_cfg.output.DSCEnable[k] == dml_dsc_enable_if_necessary) && mode_lib->ms.cache_display_cfg.output.OutputFormat[k] == dml_n422 && !mode_lib->ms.ip.dsc422_native_support)
				mode_lib->ms.support.DSC422NativeNotSupported = true;

			if (((mode_lib->ms.cache_display_cfg.output.OutputLinkDPRate[k] == dml_dp_rate_hbr || mode_lib->ms.cache_display_cfg.output.OutputLinkDPRate[k] == dml_dp_rate_hbr2 || mode_lib->ms.cache_display_cfg.output.OutputLinkDPRate[k] == dml_dp_rate_hbr3) &&
					mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] != dml_dp && mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] != dml_edp) ||
					((mode_lib->ms.cache_display_cfg.output.OutputLinkDPRate[k] == dml_dp_rate_uhbr10 || mode_lib->ms.cache_display_cfg.output.OutputLinkDPRate[k] == dml_dp_rate_uhbr13p5 || mode_lib->ms.cache_display_cfg.output.OutputLinkDPRate[k] == dml_dp_rate_uhbr20) &&
					mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] != dml_dp2p0))
				mode_lib->ms.support.LinkRateDoesNotMatchDPVersion = true;

			if (mode_lib->ms.cache_display_cfg.output.OutputMultistreamEn[k] == 1) {
				if (mode_lib->ms.cache_display_cfg.output.OutputMultistreamId[k] == k && mode_lib->ms.cache_display_cfg.output.OutputLinkDPRate[k] == dml_dp_rate_na)
					mode_lib->ms.support.LinkRateForMultistreamNotIndicated = true;
				if (mode_lib->ms.cache_display_cfg.output.OutputMultistreamId[k] == k && mode_lib->ms.cache_display_cfg.output.ForcedOutputLinkBPP[k] == 0)
					mode_lib->ms.support.BPPForMultistreamNotIndicated = true;
				for (j = 0; j < mode_lib->ms.num_active_planes; ++j) {
					if (mode_lib->ms.cache_display_cfg.output.OutputMultistreamId[k] == j && mode_lib->ms.cache_display_cfg.output.ForcedOutputLinkBPP[k] == 0)
						mode_lib->ms.support.BPPForMultistreamNotIndicated = true;
				}
			}

			if ((mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_edp || mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_hdmi || mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_hdmifrl)) {
				if (mode_lib->ms.cache_display_cfg.output.OutputMultistreamEn[k] == 1 && mode_lib->ms.cache_display_cfg.output.OutputMultistreamId[k] == k)
					mode_lib->ms.support.MultistreamWithHDMIOreDP = true;
				for (j = 0; j < mode_lib->ms.num_active_planes; ++j) {
					if (mode_lib->ms.cache_display_cfg.output.OutputMultistreamEn[k] == 1 && mode_lib->ms.cache_display_cfg.output.OutputMultistreamId[k] == j)
						mode_lib->ms.support.MultistreamWithHDMIOreDP = true;
				}
			}
			if (mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] != dml_dp && (mode_lib->ms.policy.ODMUse[k] == dml_odm_use_policy_split_1to2 ||
				mode_lib->ms.policy.ODMUse[k] == dml_odm_use_policy_mso_1to2 || mode_lib->ms.policy.ODMUse[k] == dml_odm_use_policy_mso_1to4))
				mode_lib->ms.support.MSOOrODMSplitWithNonDPLink = true;

			if ((mode_lib->ms.policy.ODMUse[k] == dml_odm_use_policy_mso_1to2 && mode_lib->ms.cache_display_cfg.output.OutputLinkDPLanes[k] < 2) ||
				(mode_lib->ms.policy.ODMUse[k] == dml_odm_use_policy_mso_1to4 && mode_lib->ms.cache_display_cfg.output.OutputLinkDPLanes[k] < 4))
				mode_lib->ms.support.NotEnoughLanesForMSO = true;
		}
	}

	mode_lib->ms.support.DTBCLKRequiredMoreThanSupported = false;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == k &&
				mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_hdmifrl &&
				RequiredDTBCLK(
							mode_lib->ms.RequiresDSC[k],
							mode_lib->ms.cache_display_cfg.output.PixelClockBackEnd[k],
							mode_lib->ms.cache_display_cfg.output.OutputFormat[k],
							mode_lib->ms.OutputBppPerState[k],
							mode_lib->ms.support.NumberOfDSCSlices[k],
							mode_lib->ms.cache_display_cfg.timing.HTotal[k],
							mode_lib->ms.cache_display_cfg.timing.HActive[k],
							mode_lib->ms.cache_display_cfg.output.AudioSampleRate[k],
							mode_lib->ms.cache_display_cfg.output.AudioSampleLayout[k]) > mode_lib->ms.state.dtbclk_mhz) {
								mode_lib->ms.support.DTBCLKRequiredMoreThanSupported = true;
							}
	}

	mode_lib->ms.support.ODMCombineTwoToOneSupportCheckOK = true;
	mode_lib->ms.support.ODMCombineFourToOneSupportCheckOK = true;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == k && mode_lib->ms.ODMModePerState[k] == dml_odm_mode_combine_2to1 && mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_hdmi) {
			mode_lib->ms.support.ODMCombineTwoToOneSupportCheckOK = false;
		}
		if (mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == k && mode_lib->ms.ODMModePerState[k] == dml_odm_mode_combine_4to1 && (mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_dp ||
			mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_edp || mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_hdmi)) {
			mode_lib->ms.support.ODMCombineFourToOneSupportCheckOK = false;
		}
	}

	mode_lib->ms.support.DSCCLKRequiredMoreThanSupported = false;
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == k) {
			if (mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_dp ||
				mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_dp2p0 ||
				mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_edp ||
				mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_hdmifrl) {
				if (mode_lib->ms.cache_display_cfg.output.OutputFormat[k] == dml_420) {
					s->DSCFormatFactor = 2;
				} else if (mode_lib->ms.cache_display_cfg.output.OutputFormat[k] == dml_444) {
					s->DSCFormatFactor = 1;
				} else if (mode_lib->ms.cache_display_cfg.output.OutputFormat[k] == dml_n422 || mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_hdmifrl) {
					s->DSCFormatFactor = 2;
				} else {
					s->DSCFormatFactor = 1;
				}
#ifdef __DML_VBA_DEBUG__
				dml_print("DML::%s: k=%u, RequiresDSC = %u\n",  __func__, k, mode_lib->ms.RequiresDSC[k]);
#endif
				if (mode_lib->ms.RequiresDSC[k] == true) {
					if (mode_lib->ms.ODMModePerState[k] == dml_odm_mode_combine_4to1) {
						if (mode_lib->ms.cache_display_cfg.output.PixelClockBackEnd[k] / 12.0 / (dml_float_t)s->DSCFormatFactor > (1.0 - mode_lib->ms.soc.dcn_downspread_percent / 100.0) * mode_lib->ms.state.dscclk_mhz) {
#ifdef __DML_VBA_DEBUG__
							dml_print("DML::%s: k=%u, PixelClockBackEnd     = %f\n",  __func__, k, mode_lib->ms.cache_display_cfg.output.PixelClockBackEnd[k]);
							dml_print("DML::%s: k=%u, DSCCLKPerState        = %f\n",  __func__, k, mode_lib->ms.state.dscclk_mhz);
							dml_print("DML::%s: k=%u, DSCFormatFactor       = %u\n",  __func__, k, s->DSCFormatFactor);
#endif
							mode_lib->ms.support.DSCCLKRequiredMoreThanSupported = true;
						}
					} else if (mode_lib->ms.ODMModePerState[k] == dml_odm_mode_combine_2to1) {
						if (mode_lib->ms.cache_display_cfg.output.PixelClockBackEnd[k] / 6.0 / (dml_float_t)s->DSCFormatFactor > (1.0 - mode_lib->ms.soc.dcn_downspread_percent / 100.0) * mode_lib->ms.state.dscclk_mhz) {
							mode_lib->ms.support.DSCCLKRequiredMoreThanSupported = true;
						}
					} else {
						if (mode_lib->ms.cache_display_cfg.output.PixelClockBackEnd[k] / 3.0 / (dml_float_t)s->DSCFormatFactor > (1.0 - mode_lib->ms.soc.dcn_downspread_percent / 100.0) * mode_lib->ms.state.dscclk_mhz) {
							mode_lib->ms.support.DSCCLKRequiredMoreThanSupported = true;
						}
					}
				}
			}
		}
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: DSCCLKRequiredMoreThanSupported = %u\n",  __func__, mode_lib->ms.support.DSCCLKRequiredMoreThanSupported);
#endif

	/* Check DSC Unit and Slices Support */
	mode_lib->ms.support.NotEnoughDSCUnits = false;
	mode_lib->ms.support.NotEnoughDSCSlices = false;
	s->TotalDSCUnitsRequired = 0;
	mode_lib->ms.support.PixelsPerLinePerDSCUnitSupport = true;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.RequiresDSC[k] == true) {
			if (mode_lib->ms.ODMModePerState[k] == dml_odm_mode_combine_4to1) {
				if (mode_lib->ms.cache_display_cfg.timing.HActive[k] > 4 * (dml_uint_t) mode_lib->ms.ip.maximum_pixels_per_line_per_dsc_unit)
					mode_lib->ms.support.PixelsPerLinePerDSCUnitSupport = false;
				s->TotalDSCUnitsRequired = s->TotalDSCUnitsRequired + 4;
				if (mode_lib->ms.support.NumberOfDSCSlices[k] > 16)
					mode_lib->ms.support.NotEnoughDSCSlices = true;
			} else if (mode_lib->ms.ODMModePerState[k] == dml_odm_mode_combine_2to1) {
				if (mode_lib->ms.cache_display_cfg.timing.HActive[k] > 2 * (dml_uint_t) mode_lib->ms.ip.maximum_pixels_per_line_per_dsc_unit)
					mode_lib->ms.support.PixelsPerLinePerDSCUnitSupport = false;
				s->TotalDSCUnitsRequired = s->TotalDSCUnitsRequired + 2;
				if (mode_lib->ms.support.NumberOfDSCSlices[k] > 8)
					mode_lib->ms.support.NotEnoughDSCSlices = true;
			} else {
				if (mode_lib->ms.cache_display_cfg.timing.HActive[k] > (dml_uint_t) mode_lib->ms.ip.maximum_pixels_per_line_per_dsc_unit)
					mode_lib->ms.support.PixelsPerLinePerDSCUnitSupport = false;
				s->TotalDSCUnitsRequired = s->TotalDSCUnitsRequired + 1;
				if (mode_lib->ms.support.NumberOfDSCSlices[k] > 4)
					mode_lib->ms.support.NotEnoughDSCSlices = true;
			}
		}
	}
   if (s->TotalDSCUnitsRequired > (dml_uint_t) mode_lib->ms.ip.num_dsc) {
		mode_lib->ms.support.NotEnoughDSCUnits = true;
	}

	/*DSC Delay per state*/
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		mode_lib->ms.DSCDelayPerState[k] = DSCDelayRequirement(mode_lib->ms.RequiresDSC[k],
													mode_lib->ms.ODMModePerState[k],
													mode_lib->ms.cache_display_cfg.output.DSCInputBitPerComponent[k],
													mode_lib->ms.OutputBppPerState[k],
													mode_lib->ms.cache_display_cfg.timing.HActive[k],
													mode_lib->ms.cache_display_cfg.timing.HTotal[k],
													mode_lib->ms.support.NumberOfDSCSlices[k],
													mode_lib->ms.cache_display_cfg.output.OutputFormat[k],
													mode_lib->ms.cache_display_cfg.output.OutputEncoder[k],
													mode_lib->ms.cache_display_cfg.timing.PixelClock[k],
													mode_lib->ms.cache_display_cfg.output.PixelClockBackEnd[k]);
	}

	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		for (m = 0; m <= mode_lib->ms.num_active_planes - 1; m++) {
			for (j = 0; j <= mode_lib->ms.num_active_planes - 1; j++) {
				if (mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == m && mode_lib->ms.RequiresDSC[m] == true) {
					mode_lib->ms.DSCDelayPerState[k] = mode_lib->ms.DSCDelayPerState[m];
				}
			}
		}
	}

	//Calculate Swath, DET Configuration, DCFCLKDeepSleep
	//
	for (j = 0; j < 2; ++j) {
		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			mode_lib->ms.RequiredDPPCLKThisState[k] = mode_lib->ms.RequiredDPPCLKPerSurface[j][k];
			mode_lib->ms.NoOfDPPThisState[k] = mode_lib->ms.NoOfDPP[j][k];
			mode_lib->ms.ODMModeThisState[k] = mode_lib->ms.ODMModePerState[k];
		}

		CalculateSwathAndDETConfiguration_params->DETSizeOverride = mode_lib->ms.cache_display_cfg.plane.DETSizeOverride;
		CalculateSwathAndDETConfiguration_params->UseMALLForPStateChange = mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange;
		CalculateSwathAndDETConfiguration_params->ConfigReturnBufferSizeInKByte = mode_lib->ms.ip.config_return_buffer_size_in_kbytes;
		CalculateSwathAndDETConfiguration_params->ROBBufferSizeInKByte = mode_lib->ms.ip.rob_buffer_size_kbytes;
		CalculateSwathAndDETConfiguration_params->MaxTotalDETInKByte = mode_lib->ms.MaxTotalDETInKByte;
		CalculateSwathAndDETConfiguration_params->MinCompressedBufferSizeInKByte = mode_lib->ms.MinCompressedBufferSizeInKByte;
		CalculateSwathAndDETConfiguration_params->PixelChunkSizeInKByte = mode_lib->ms.ip.pixel_chunk_size_kbytes;
		CalculateSwathAndDETConfiguration_params->ForceSingleDPP = false;
		CalculateSwathAndDETConfiguration_params->NumberOfActiveSurfaces = mode_lib->ms.num_active_planes;
		CalculateSwathAndDETConfiguration_params->nomDETInKByte = mode_lib->ms.NomDETInKByte;
		CalculateSwathAndDETConfiguration_params->UseUnboundedRequestingFinal = mode_lib->ms.policy.UseUnboundedRequesting;
		CalculateSwathAndDETConfiguration_params->ConfigReturnBufferSegmentSizeInkByte = mode_lib->ms.ip.config_return_buffer_segment_size_in_kbytes;
		CalculateSwathAndDETConfiguration_params->CompressedBufferSegmentSizeInkByteFinal = mode_lib->ms.ip.compressed_buffer_segment_size_in_kbytes;
		CalculateSwathAndDETConfiguration_params->Output = mode_lib->ms.cache_display_cfg.output.OutputEncoder;
		CalculateSwathAndDETConfiguration_params->ReadBandwidthLuma = mode_lib->ms.ReadBandwidthLuma;
		CalculateSwathAndDETConfiguration_params->ReadBandwidthChroma = mode_lib->ms.ReadBandwidthChroma;
		CalculateSwathAndDETConfiguration_params->MaximumSwathWidthLuma = mode_lib->ms.MaximumSwathWidthLuma;
		CalculateSwathAndDETConfiguration_params->MaximumSwathWidthChroma = mode_lib->ms.MaximumSwathWidthChroma;
		CalculateSwathAndDETConfiguration_params->SourceScan = mode_lib->ms.cache_display_cfg.plane.SourceScan;
		CalculateSwathAndDETConfiguration_params->ViewportStationary = mode_lib->ms.cache_display_cfg.plane.ViewportStationary;
		CalculateSwathAndDETConfiguration_params->SourcePixelFormat = mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat;
		CalculateSwathAndDETConfiguration_params->SurfaceTiling = mode_lib->ms.cache_display_cfg.surface.SurfaceTiling;
		CalculateSwathAndDETConfiguration_params->ViewportWidth = mode_lib->ms.cache_display_cfg.plane.ViewportWidth;
		CalculateSwathAndDETConfiguration_params->ViewportHeight = mode_lib->ms.cache_display_cfg.plane.ViewportHeight;
		CalculateSwathAndDETConfiguration_params->ViewportXStart = mode_lib->ms.cache_display_cfg.plane.ViewportXStart;
		CalculateSwathAndDETConfiguration_params->ViewportYStart = mode_lib->ms.cache_display_cfg.plane.ViewportYStart;
		CalculateSwathAndDETConfiguration_params->ViewportXStartC = mode_lib->ms.cache_display_cfg.plane.ViewportXStartC;
		CalculateSwathAndDETConfiguration_params->ViewportYStartC = mode_lib->ms.cache_display_cfg.plane.ViewportYStartC;
		CalculateSwathAndDETConfiguration_params->SurfaceWidthY = mode_lib->ms.cache_display_cfg.surface.SurfaceWidthY;
		CalculateSwathAndDETConfiguration_params->SurfaceWidthC = mode_lib->ms.cache_display_cfg.surface.SurfaceWidthC;
		CalculateSwathAndDETConfiguration_params->SurfaceHeightY = mode_lib->ms.cache_display_cfg.surface.SurfaceHeightY;
		CalculateSwathAndDETConfiguration_params->SurfaceHeightC = mode_lib->ms.cache_display_cfg.surface.SurfaceHeightC;
		CalculateSwathAndDETConfiguration_params->Read256BytesBlockHeightY = mode_lib->ms.Read256BlockHeightY;
		CalculateSwathAndDETConfiguration_params->Read256BytesBlockHeightC = mode_lib->ms.Read256BlockHeightC;
		CalculateSwathAndDETConfiguration_params->Read256BytesBlockWidthY = mode_lib->ms.Read256BlockWidthY;
		CalculateSwathAndDETConfiguration_params->Read256BytesBlockWidthC = mode_lib->ms.Read256BlockWidthC;
		CalculateSwathAndDETConfiguration_params->ODMMode = mode_lib->ms.ODMModeThisState;
		CalculateSwathAndDETConfiguration_params->BlendingAndTiming = mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming;
		CalculateSwathAndDETConfiguration_params->BytePerPixY = mode_lib->ms.BytePerPixelY;
		CalculateSwathAndDETConfiguration_params->BytePerPixC = mode_lib->ms.BytePerPixelC;
		CalculateSwathAndDETConfiguration_params->BytePerPixDETY = mode_lib->ms.BytePerPixelInDETY;
		CalculateSwathAndDETConfiguration_params->BytePerPixDETC = mode_lib->ms.BytePerPixelInDETC;
		CalculateSwathAndDETConfiguration_params->HActive = mode_lib->ms.cache_display_cfg.timing.HActive;
		CalculateSwathAndDETConfiguration_params->HRatio = mode_lib->ms.cache_display_cfg.plane.HRatio;
		CalculateSwathAndDETConfiguration_params->HRatioChroma = mode_lib->ms.cache_display_cfg.plane.HRatioChroma;
		CalculateSwathAndDETConfiguration_params->DPPPerSurface = mode_lib->ms.NoOfDPPThisState;
		CalculateSwathAndDETConfiguration_params->swath_width_luma_ub = mode_lib->ms.swath_width_luma_ub_this_state;
		CalculateSwathAndDETConfiguration_params->swath_width_chroma_ub = mode_lib->ms.swath_width_chroma_ub_this_state;
		CalculateSwathAndDETConfiguration_params->SwathWidth = mode_lib->ms.SwathWidthYThisState;
		CalculateSwathAndDETConfiguration_params->SwathWidthChroma = mode_lib->ms.SwathWidthCThisState;
		CalculateSwathAndDETConfiguration_params->SwathHeightY = mode_lib->ms.SwathHeightYThisState;
		CalculateSwathAndDETConfiguration_params->SwathHeightC = mode_lib->ms.SwathHeightCThisState;
		CalculateSwathAndDETConfiguration_params->DETBufferSizeInKByte = mode_lib->ms.DETBufferSizeInKByteThisState;
		CalculateSwathAndDETConfiguration_params->DETBufferSizeY = mode_lib->ms.DETBufferSizeYThisState;
		CalculateSwathAndDETConfiguration_params->DETBufferSizeC = mode_lib->ms.DETBufferSizeCThisState;
		CalculateSwathAndDETConfiguration_params->UnboundedRequestEnabled = &mode_lib->ms.UnboundedRequestEnabledThisState;
		CalculateSwathAndDETConfiguration_params->compbuf_reserved_space_64b = &s->dummy_integer[2];
		CalculateSwathAndDETConfiguration_params->compbuf_reserved_space_zs = &s->dummy_integer[1];
		CalculateSwathAndDETConfiguration_params->CompressedBufferSizeInkByte = &mode_lib->ms.CompressedBufferSizeInkByteThisState;
		CalculateSwathAndDETConfiguration_params->ViewportSizeSupportPerSurface = s->dummy_boolean_array[0];
		CalculateSwathAndDETConfiguration_params->ViewportSizeSupport = &mode_lib->ms.support.ViewportSizeSupport[j];

		CalculateSwathAndDETConfiguration(&mode_lib->scratch,
		CalculateSwathAndDETConfiguration_params);

		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			mode_lib->ms.swath_width_luma_ub_all_states[j][k] = mode_lib->ms.swath_width_luma_ub_this_state[k];
			mode_lib->ms.swath_width_chroma_ub_all_states[j][k] = mode_lib->ms.swath_width_chroma_ub_this_state[k];
			mode_lib->ms.SwathWidthYAllStates[j][k] = mode_lib->ms.SwathWidthYThisState[k];
			mode_lib->ms.SwathWidthCAllStates[j][k] = mode_lib->ms.SwathWidthCThisState[k];
			mode_lib->ms.SwathHeightYAllStates[j][k] = mode_lib->ms.SwathHeightYThisState[k];
			mode_lib->ms.SwathHeightCAllStates[j][k] = mode_lib->ms.SwathHeightCThisState[k];
			mode_lib->ms.UnboundedRequestEnabledAllStates[j] = mode_lib->ms.UnboundedRequestEnabledThisState;
			mode_lib->ms.CompressedBufferSizeInkByteAllStates[j] = mode_lib->ms.CompressedBufferSizeInkByteThisState;
			mode_lib->ms.DETBufferSizeInKByteAllStates[j][k] = mode_lib->ms.DETBufferSizeInKByteThisState[k];
			mode_lib->ms.DETBufferSizeYAllStates[j][k] = mode_lib->ms.DETBufferSizeYThisState[k];
			mode_lib->ms.DETBufferSizeCAllStates[j][k] = mode_lib->ms.DETBufferSizeCThisState[k];
		}
	}

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		mode_lib->ms.cursor_bw[k] = mode_lib->ms.cache_display_cfg.plane.NumberOfCursors[k] * mode_lib->ms.cache_display_cfg.plane.CursorWidth[k] * mode_lib->ms.cache_display_cfg.plane.CursorBPP[k] / 8.0 / (mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k]) * mode_lib->ms.cache_display_cfg.plane.VRatio[k];
	}

	CalculateSurfaceSizeInMall(
			mode_lib->ms.num_active_planes,
			mode_lib->ms.soc.mall_allocated_for_dcn_mbytes,
			mode_lib->ms.cache_display_cfg.plane.UseMALLForStaticScreen,
			mode_lib->ms.cache_display_cfg.surface.DCCEnable,
			mode_lib->ms.cache_display_cfg.plane.ViewportStationary,
			mode_lib->ms.cache_display_cfg.plane.ViewportXStart,
			mode_lib->ms.cache_display_cfg.plane.ViewportYStart,
			mode_lib->ms.cache_display_cfg.plane.ViewportXStartC,
			mode_lib->ms.cache_display_cfg.plane.ViewportYStartC,
			mode_lib->ms.cache_display_cfg.plane.ViewportWidth,
			mode_lib->ms.cache_display_cfg.plane.ViewportHeight,
			mode_lib->ms.BytePerPixelY,
			mode_lib->ms.cache_display_cfg.plane.ViewportWidthChroma,
			mode_lib->ms.cache_display_cfg.plane.ViewportHeightChroma,
			mode_lib->ms.BytePerPixelC,
			mode_lib->ms.cache_display_cfg.surface.SurfaceWidthY,
			mode_lib->ms.cache_display_cfg.surface.SurfaceWidthC,
			mode_lib->ms.cache_display_cfg.surface.SurfaceHeightY,
			mode_lib->ms.cache_display_cfg.surface.SurfaceHeightC,
			mode_lib->ms.Read256BlockWidthY,
			mode_lib->ms.Read256BlockWidthC,
			mode_lib->ms.Read256BlockHeightY,
			mode_lib->ms.Read256BlockHeightC,
			mode_lib->ms.MacroTileWidthY,
			mode_lib->ms.MacroTileWidthC,
			mode_lib->ms.MacroTileHeightY,
			mode_lib->ms.MacroTileHeightC,

			/* Output */
			mode_lib->ms.SurfaceSizeInMALL,
			&mode_lib->ms.support.ExceededMALLSize);

	for (j = 0; j < 2; j++) {
		for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
			mode_lib->ms.swath_width_luma_ub_this_state[k] = mode_lib->ms.swath_width_luma_ub_all_states[j][k];
			mode_lib->ms.swath_width_chroma_ub_this_state[k] = mode_lib->ms.swath_width_chroma_ub_all_states[j][k];
			mode_lib->ms.SwathWidthYThisState[k] = mode_lib->ms.SwathWidthYAllStates[j][k];
			mode_lib->ms.SwathWidthCThisState[k] = mode_lib->ms.SwathWidthCAllStates[j][k];
			mode_lib->ms.SwathHeightYThisState[k] = mode_lib->ms.SwathHeightYAllStates[j][k];
			mode_lib->ms.SwathHeightCThisState[k] = mode_lib->ms.SwathHeightCAllStates[j][k];
			mode_lib->ms.DETBufferSizeInKByteThisState[k] = mode_lib->ms.DETBufferSizeInKByteAllStates[j][k];
			mode_lib->ms.DETBufferSizeYThisState[k] = mode_lib->ms.DETBufferSizeYAllStates[j][k];
			mode_lib->ms.DETBufferSizeCThisState[k] = mode_lib->ms.DETBufferSizeCAllStates[j][k];
			mode_lib->ms.RequiredDPPCLKThisState[k] = mode_lib->ms.RequiredDPPCLKPerSurface[j][k];
			mode_lib->ms.NoOfDPPThisState[k] = mode_lib->ms.NoOfDPP[j][k];
		}

		mode_lib->ms.TotalNumberOfDCCActiveDPP[j] = 0;
		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			if (mode_lib->ms.cache_display_cfg.surface.DCCEnable[k] == true) {
				mode_lib->ms.TotalNumberOfDCCActiveDPP[j] = mode_lib->ms.TotalNumberOfDCCActiveDPP[j] + mode_lib->ms.NoOfDPP[j][k];
			}
		}

		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			s->SurfParameters[k].PixelClock = mode_lib->ms.cache_display_cfg.timing.PixelClock[k];
			s->SurfParameters[k].DPPPerSurface = mode_lib->ms.NoOfDPP[j][k];
			s->SurfParameters[k].SourceScan = mode_lib->ms.cache_display_cfg.plane.SourceScan[k];
			s->SurfParameters[k].ViewportHeight = mode_lib->ms.cache_display_cfg.plane.ViewportHeight[k];
			s->SurfParameters[k].ViewportHeightChroma = mode_lib->ms.cache_display_cfg.plane.ViewportHeightChroma[k];
			s->SurfParameters[k].BlockWidth256BytesY = mode_lib->ms.Read256BlockWidthY[k];
			s->SurfParameters[k].BlockHeight256BytesY = mode_lib->ms.Read256BlockHeightY[k];
			s->SurfParameters[k].BlockWidth256BytesC = mode_lib->ms.Read256BlockWidthC[k];
			s->SurfParameters[k].BlockHeight256BytesC = mode_lib->ms.Read256BlockHeightC[k];
			s->SurfParameters[k].BlockWidthY = mode_lib->ms.MacroTileWidthY[k];
			s->SurfParameters[k].BlockHeightY = mode_lib->ms.MacroTileHeightY[k];
			s->SurfParameters[k].BlockWidthC = mode_lib->ms.MacroTileWidthC[k];
			s->SurfParameters[k].BlockHeightC = mode_lib->ms.MacroTileHeightC[k];
			s->SurfParameters[k].InterlaceEnable = mode_lib->ms.cache_display_cfg.timing.Interlace[k];
			s->SurfParameters[k].HTotal = mode_lib->ms.cache_display_cfg.timing.HTotal[k];
			s->SurfParameters[k].DCCEnable = mode_lib->ms.cache_display_cfg.surface.DCCEnable[k];
			s->SurfParameters[k].SourcePixelFormat = mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k];
			s->SurfParameters[k].SurfaceTiling = mode_lib->ms.cache_display_cfg.surface.SurfaceTiling[k];
			s->SurfParameters[k].BytePerPixelY = mode_lib->ms.BytePerPixelY[k];
			s->SurfParameters[k].BytePerPixelC = mode_lib->ms.BytePerPixelC[k];
			s->SurfParameters[k].ProgressiveToInterlaceUnitInOPP = mode_lib->ms.ip.ptoi_supported;
			s->SurfParameters[k].VRatio = mode_lib->ms.cache_display_cfg.plane.VRatio[k];
			s->SurfParameters[k].VRatioChroma = mode_lib->ms.cache_display_cfg.plane.VRatioChroma[k];
			s->SurfParameters[k].VTaps = mode_lib->ms.cache_display_cfg.plane.VTaps[k];
			s->SurfParameters[k].VTapsChroma = mode_lib->ms.cache_display_cfg.plane.VTapsChroma[k];
			s->SurfParameters[k].PitchY = mode_lib->ms.cache_display_cfg.surface.PitchY[k];
			s->SurfParameters[k].DCCMetaPitchY = mode_lib->ms.cache_display_cfg.surface.DCCMetaPitchY[k];
			s->SurfParameters[k].PitchC = mode_lib->ms.cache_display_cfg.surface.PitchC[k];
			s->SurfParameters[k].DCCMetaPitchC = mode_lib->ms.cache_display_cfg.surface.DCCMetaPitchC[k];
			s->SurfParameters[k].ViewportStationary = mode_lib->ms.cache_display_cfg.plane.ViewportStationary[k];
			s->SurfParameters[k].ViewportXStart = mode_lib->ms.cache_display_cfg.plane.ViewportXStart[k];
			s->SurfParameters[k].ViewportYStart = mode_lib->ms.cache_display_cfg.plane.ViewportYStart[k];
			s->SurfParameters[k].ViewportXStartC = mode_lib->ms.cache_display_cfg.plane.ViewportXStartC[k];
			s->SurfParameters[k].ViewportYStartC = mode_lib->ms.cache_display_cfg.plane.ViewportYStartC[k];
			s->SurfParameters[k].FORCE_ONE_ROW_FOR_FRAME = mode_lib->ms.cache_display_cfg.plane.ForceOneRowForFrame[k];
			s->SurfParameters[k].SwathHeightY = mode_lib->ms.SwathHeightYThisState[k];
			s->SurfParameters[k].SwathHeightC = mode_lib->ms.SwathHeightCThisState[k];
		}

		CalculateVMRowAndSwath_params->NumberOfActiveSurfaces = mode_lib->ms.num_active_planes;
		CalculateVMRowAndSwath_params->myPipe = s->SurfParameters;
		CalculateVMRowAndSwath_params->SurfaceSizeInMALL = mode_lib->ms.SurfaceSizeInMALL;
		CalculateVMRowAndSwath_params->PTEBufferSizeInRequestsLuma = mode_lib->ms.ip.dpte_buffer_size_in_pte_reqs_luma;
		CalculateVMRowAndSwath_params->PTEBufferSizeInRequestsChroma = mode_lib->ms.ip.dpte_buffer_size_in_pte_reqs_chroma;
		CalculateVMRowAndSwath_params->DCCMetaBufferSizeBytes = mode_lib->ms.ip.dcc_meta_buffer_size_bytes;
		CalculateVMRowAndSwath_params->UseMALLForStaticScreen = mode_lib->ms.cache_display_cfg.plane.UseMALLForStaticScreen;
		CalculateVMRowAndSwath_params->UseMALLForPStateChange = mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange;
		CalculateVMRowAndSwath_params->MALLAllocatedForDCN = mode_lib->ms.soc.mall_allocated_for_dcn_mbytes;
		CalculateVMRowAndSwath_params->SwathWidthY = mode_lib->ms.SwathWidthYThisState;
		CalculateVMRowAndSwath_params->SwathWidthC = mode_lib->ms.SwathWidthCThisState;
		CalculateVMRowAndSwath_params->GPUVMEnable = mode_lib->ms.cache_display_cfg.plane.GPUVMEnable;
		CalculateVMRowAndSwath_params->HostVMEnable = mode_lib->ms.cache_display_cfg.plane.HostVMEnable;
		CalculateVMRowAndSwath_params->HostVMMaxNonCachedPageTableLevels = mode_lib->ms.cache_display_cfg.plane.HostVMMaxPageTableLevels;
		CalculateVMRowAndSwath_params->GPUVMMaxPageTableLevels = mode_lib->ms.cache_display_cfg.plane.GPUVMMaxPageTableLevels;
		CalculateVMRowAndSwath_params->GPUVMMinPageSizeKBytes = mode_lib->ms.cache_display_cfg.plane.GPUVMMinPageSizeKBytes;
		CalculateVMRowAndSwath_params->HostVMMinPageSize = mode_lib->ms.soc.hostvm_min_page_size_kbytes * 1024;
		CalculateVMRowAndSwath_params->PTEBufferModeOverrideEn = mode_lib->ms.cache_display_cfg.plane.PTEBufferModeOverrideEn;
		CalculateVMRowAndSwath_params->PTEBufferModeOverrideVal = mode_lib->ms.cache_display_cfg.plane.PTEBufferMode;
		CalculateVMRowAndSwath_params->PTEBufferSizeNotExceeded = mode_lib->ms.PTEBufferSizeNotExceededPerState;
		CalculateVMRowAndSwath_params->DCCMetaBufferSizeNotExceeded = mode_lib->ms.DCCMetaBufferSizeNotExceededPerState;
		CalculateVMRowAndSwath_params->dpte_row_width_luma_ub = s->dummy_integer_array[0];
		CalculateVMRowAndSwath_params->dpte_row_width_chroma_ub = s->dummy_integer_array[1];
		CalculateVMRowAndSwath_params->dpte_row_height_luma = mode_lib->ms.dpte_row_height;
		CalculateVMRowAndSwath_params->dpte_row_height_chroma = mode_lib->ms.dpte_row_height_chroma;
		CalculateVMRowAndSwath_params->dpte_row_height_linear_luma = s->dummy_integer_array[2]; // VBA_DELTA
		CalculateVMRowAndSwath_params->dpte_row_height_linear_chroma = s->dummy_integer_array[3]; // VBA_DELTA
		CalculateVMRowAndSwath_params->meta_req_width = s->dummy_integer_array[4];
		CalculateVMRowAndSwath_params->meta_req_width_chroma = s->dummy_integer_array[5];
		CalculateVMRowAndSwath_params->meta_req_height = s->dummy_integer_array[6];
		CalculateVMRowAndSwath_params->meta_req_height_chroma = s->dummy_integer_array[7];
		CalculateVMRowAndSwath_params->meta_row_width = s->dummy_integer_array[8];
		CalculateVMRowAndSwath_params->meta_row_width_chroma = s->dummy_integer_array[9];
		CalculateVMRowAndSwath_params->meta_row_height = mode_lib->ms.meta_row_height;
		CalculateVMRowAndSwath_params->meta_row_height_chroma = mode_lib->ms.meta_row_height_chroma;
		CalculateVMRowAndSwath_params->vm_group_bytes = s->dummy_integer_array[10];
		CalculateVMRowAndSwath_params->dpte_group_bytes = mode_lib->ms.dpte_group_bytes;
		CalculateVMRowAndSwath_params->PixelPTEReqWidthY = s->dummy_integer_array[11];
		CalculateVMRowAndSwath_params->PixelPTEReqHeightY = s->dummy_integer_array[12];
		CalculateVMRowAndSwath_params->PTERequestSizeY = s->dummy_integer_array[13];
		CalculateVMRowAndSwath_params->PixelPTEReqWidthC = s->dummy_integer_array[14];
		CalculateVMRowAndSwath_params->PixelPTEReqHeightC = s->dummy_integer_array[15];
		CalculateVMRowAndSwath_params->PTERequestSizeC = s->dummy_integer_array[16];
		CalculateVMRowAndSwath_params->dpde0_bytes_per_frame_ub_l = s->dummy_integer_array[17];
		CalculateVMRowAndSwath_params->meta_pte_bytes_per_frame_ub_l = s->dummy_integer_array[18];
		CalculateVMRowAndSwath_params->dpde0_bytes_per_frame_ub_c = s->dummy_integer_array[19];
		CalculateVMRowAndSwath_params->meta_pte_bytes_per_frame_ub_c = s->dummy_integer_array[20];
		CalculateVMRowAndSwath_params->PrefetchSourceLinesY = mode_lib->ms.PrefetchLinesYThisState;
		CalculateVMRowAndSwath_params->PrefetchSourceLinesC = mode_lib->ms.PrefetchLinesCThisState;
		CalculateVMRowAndSwath_params->VInitPreFillY = mode_lib->ms.PrefillY;
		CalculateVMRowAndSwath_params->VInitPreFillC = mode_lib->ms.PrefillC;
		CalculateVMRowAndSwath_params->MaxNumSwathY = mode_lib->ms.MaxNumSwY;
		CalculateVMRowAndSwath_params->MaxNumSwathC = mode_lib->ms.MaxNumSwC;
		CalculateVMRowAndSwath_params->meta_row_bw = mode_lib->ms.meta_row_bandwidth_this_state;
		CalculateVMRowAndSwath_params->dpte_row_bw = mode_lib->ms.dpte_row_bandwidth_this_state;
		CalculateVMRowAndSwath_params->PixelPTEBytesPerRow = mode_lib->ms.DPTEBytesPerRowThisState;
		CalculateVMRowAndSwath_params->PDEAndMetaPTEBytesFrame = mode_lib->ms.PDEAndMetaPTEBytesPerFrameThisState;
		CalculateVMRowAndSwath_params->MetaRowByte = mode_lib->ms.MetaRowBytesThisState;
		CalculateVMRowAndSwath_params->use_one_row_for_frame = mode_lib->ms.use_one_row_for_frame_this_state;
		CalculateVMRowAndSwath_params->use_one_row_for_frame_flip = mode_lib->ms.use_one_row_for_frame_flip_this_state;
		CalculateVMRowAndSwath_params->UsesMALLForStaticScreen = s->dummy_boolean_array[0];
		CalculateVMRowAndSwath_params->PTE_BUFFER_MODE = s->dummy_boolean_array[1];
		CalculateVMRowAndSwath_params->BIGK_FRAGMENT_SIZE = s->dummy_integer_array[21];

		CalculateVMRowAndSwath(&mode_lib->scratch,
			CalculateVMRowAndSwath_params);

		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			mode_lib->ms.PrefetchLinesY[j][k] = mode_lib->ms.PrefetchLinesYThisState[k];
			mode_lib->ms.PrefetchLinesC[j][k] = mode_lib->ms.PrefetchLinesCThisState[k];
			mode_lib->ms.meta_row_bandwidth[j][k] = mode_lib->ms.meta_row_bandwidth_this_state[k];
			mode_lib->ms.dpte_row_bandwidth[j][k] = mode_lib->ms.dpte_row_bandwidth_this_state[k];
			mode_lib->ms.DPTEBytesPerRow[j][k] = mode_lib->ms.DPTEBytesPerRowThisState[k];
			mode_lib->ms.PDEAndMetaPTEBytesPerFrame[j][k] = mode_lib->ms.PDEAndMetaPTEBytesPerFrameThisState[k];
			mode_lib->ms.MetaRowBytes[j][k] = mode_lib->ms.MetaRowBytesThisState[k];
			mode_lib->ms.use_one_row_for_frame[j][k] = mode_lib->ms.use_one_row_for_frame_this_state[k];
			mode_lib->ms.use_one_row_for_frame_flip[j][k] = mode_lib->ms.use_one_row_for_frame_flip_this_state[k];
		}

		mode_lib->ms.support.PTEBufferSizeNotExceeded[j] = true;

		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			if (mode_lib->ms.PTEBufferSizeNotExceededPerState[k] == false)
				mode_lib->ms.support.PTEBufferSizeNotExceeded[j] = false;
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: j=%u k=%u, PTEBufferSizeNotExceededPerState[%u] = %u\n",  __func__, j, k, k, mode_lib->ms.PTEBufferSizeNotExceededPerState[k]);
#endif
		}
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: PTEBufferSizeNotExceeded[%u] = %u\n",  __func__, j, mode_lib->ms.support.PTEBufferSizeNotExceeded[j]);
#endif

		mode_lib->ms.support.DCCMetaBufferSizeNotExceeded[j] = true;
		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			if (mode_lib->ms.DCCMetaBufferSizeNotExceededPerState[k] == false)
				mode_lib->ms.support.DCCMetaBufferSizeNotExceeded[j] = false;
		}

		mode_lib->ms.UrgLatency = CalculateUrgentLatency(mode_lib->ms.state.urgent_latency_pixel_data_only_us,
													mode_lib->ms.state.urgent_latency_pixel_mixed_with_vm_data_us,
													mode_lib->ms.state.urgent_latency_vm_data_only_us,
													mode_lib->ms.soc.do_urgent_latency_adjustment,
													mode_lib->ms.state.urgent_latency_adjustment_fabric_clock_component_us,
													mode_lib->ms.state.urgent_latency_adjustment_fabric_clock_reference_mhz,
													mode_lib->ms.state.fabricclk_mhz);

		/* Getter functions work at mp interface so copy the urgent latency to mp*/
		mode_lib->mp.UrgentLatency = mode_lib->ms.UrgLatency;

		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			CalculateUrgentBurstFactor(
				mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange[k],
				mode_lib->ms.swath_width_luma_ub_this_state[k],
				mode_lib->ms.swath_width_chroma_ub_this_state[k],
				mode_lib->ms.SwathHeightYThisState[k],
				mode_lib->ms.SwathHeightCThisState[k],
				(dml_float_t) mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k],
				mode_lib->ms.UrgLatency,
				mode_lib->ms.ip.cursor_buffer_size,
				mode_lib->ms.cache_display_cfg.plane.CursorWidth[k],
				mode_lib->ms.cache_display_cfg.plane.CursorBPP[k],
				mode_lib->ms.cache_display_cfg.plane.VRatio[k],
				mode_lib->ms.cache_display_cfg.plane.VRatioChroma[k],
				mode_lib->ms.BytePerPixelInDETY[k],
				mode_lib->ms.BytePerPixelInDETC[k],
				mode_lib->ms.DETBufferSizeYThisState[k],
				mode_lib->ms.DETBufferSizeCThisState[k],
				/* Output */
				&mode_lib->ms.UrgentBurstFactorCursor[k],
				&mode_lib->ms.UrgentBurstFactorLuma[k],
				&mode_lib->ms.UrgentBurstFactorChroma[k],
				&mode_lib->ms.NotUrgentLatencyHiding[k]);
		}

		CalculateDCFCLKDeepSleep(
				mode_lib->ms.num_active_planes,
				mode_lib->ms.BytePerPixelY,
				mode_lib->ms.BytePerPixelC,
				mode_lib->ms.cache_display_cfg.plane.VRatio,
				mode_lib->ms.cache_display_cfg.plane.VRatioChroma,
				mode_lib->ms.SwathWidthYThisState,
				mode_lib->ms.SwathWidthCThisState,
				mode_lib->ms.NoOfDPPThisState,
				mode_lib->ms.cache_display_cfg.plane.HRatio,
				mode_lib->ms.cache_display_cfg.plane.HRatioChroma,
				mode_lib->ms.cache_display_cfg.timing.PixelClock,
				mode_lib->ms.PSCL_FACTOR,
				mode_lib->ms.PSCL_FACTOR_CHROMA,
				mode_lib->ms.RequiredDPPCLKThisState,
				mode_lib->ms.ReadBandwidthLuma,
				mode_lib->ms.ReadBandwidthChroma,
				mode_lib->ms.soc.return_bus_width_bytes,

				/* Output */
				&mode_lib->ms.ProjectedDCFCLKDeepSleep[j]);
	}

	//Calculate Return BW
	for (j = 0; j < 2; ++j) {
		for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
			if (mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == k) {
				if (mode_lib->ms.cache_display_cfg.writeback.WritebackEnable[k] == true) {
					mode_lib->ms.WritebackDelayTime[k] = mode_lib->ms.state.writeback_latency_us + CalculateWriteBackDelay(
									mode_lib->ms.cache_display_cfg.writeback.WritebackPixelFormat[k],
									mode_lib->ms.cache_display_cfg.writeback.WritebackHRatio[k],
									mode_lib->ms.cache_display_cfg.writeback.WritebackVRatio[k],
									mode_lib->ms.cache_display_cfg.writeback.WritebackVTaps[k],
									mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationWidth[k],
									mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationHeight[k],
									mode_lib->ms.cache_display_cfg.writeback.WritebackSourceHeight[k],
									mode_lib->ms.cache_display_cfg.timing.HTotal[k]) / mode_lib->ms.RequiredDISPCLK[j];
				} else {
					mode_lib->ms.WritebackDelayTime[k] = 0.0;
				}
				for (m = 0; m <= mode_lib->ms.num_active_planes - 1; m++) {
					if (mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[m] == k && mode_lib->ms.cache_display_cfg.writeback.WritebackEnable[m] == true) {
						mode_lib->ms.WritebackDelayTime[k] = dml_max(mode_lib->ms.WritebackDelayTime[k],
											mode_lib->ms.state.writeback_latency_us + CalculateWriteBackDelay(
											mode_lib->ms.cache_display_cfg.writeback.WritebackPixelFormat[m],
											mode_lib->ms.cache_display_cfg.writeback.WritebackHRatio[m],
											mode_lib->ms.cache_display_cfg.writeback.WritebackVRatio[m],
											mode_lib->ms.cache_display_cfg.writeback.WritebackVTaps[m],
											mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationWidth[m],
											mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationHeight[m],
											mode_lib->ms.cache_display_cfg.writeback.WritebackSourceHeight[m],
											mode_lib->ms.cache_display_cfg.timing.HTotal[m]) / mode_lib->ms.RequiredDISPCLK[j]);
					}
				}
			}
		}
		for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
			for (m = 0; m <= mode_lib->ms.num_active_planes - 1; m++) {
				if (mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == m) {
					mode_lib->ms.WritebackDelayTime[k] = mode_lib->ms.WritebackDelayTime[m];
				}
			}
		}
       s->MaxVStartupAllPlanes[j] = 0;  // max vstartup among all planes

		for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
			s->MaximumVStartup[j][k] = CalculateMaxVStartup(k,
														mode_lib->ms.ip.ptoi_supported,
														mode_lib->ms.ip.vblank_nom_default_us,
														&mode_lib->ms.cache_display_cfg.timing,
														mode_lib->ms.WritebackDelayTime[k]);

			s->MaxVStartupAllPlanes[j] = (dml_uint_t)(dml_max(s->MaxVStartupAllPlanes[j], s->MaximumVStartup[j][k]));
#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%u, MaxVStartupAllPlanes[%u] = %u\n", __func__, k, j, s->MaxVStartupAllPlanes[j]);
			dml_print("DML::%s: k=%u, MaximumVStartup[%u][%u] = %u\n", __func__, k, j, k, s->MaximumVStartup[j][k]);
#endif
		}
	}

	s->ReorderingBytes = (dml_uint_t)(mode_lib->ms.soc.num_chans * dml_max3(mode_lib->ms.soc.urgent_out_of_order_return_per_channel_pixel_only_bytes,
																mode_lib->ms.soc.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes,
																mode_lib->ms.soc.urgent_out_of_order_return_per_channel_vm_only_bytes));

	for (j = 0; j < 2; ++j) {
		mode_lib->ms.DCFCLKState[j] = mode_lib->ms.state.dcfclk_mhz;
	}

	/* Immediate Flip and MALL parameters */
	s->ImmediateFlipRequiredFinal = false;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		s->ImmediateFlipRequiredFinal = s->ImmediateFlipRequiredFinal || (mode_lib->ms.policy.ImmediateFlipRequirement[k] == dml_immediate_flip_required);
	}

	mode_lib->ms.support.ImmediateFlipRequiredButTheRequirementForEachSurfaceIsNotSpecified = false;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		mode_lib->ms.support.ImmediateFlipRequiredButTheRequirementForEachSurfaceIsNotSpecified = mode_lib->ms.support.ImmediateFlipRequiredButTheRequirementForEachSurfaceIsNotSpecified ||
																							((mode_lib->ms.policy.ImmediateFlipRequirement[k] != dml_immediate_flip_required) &&
																							(mode_lib->ms.policy.ImmediateFlipRequirement[k] != dml_immediate_flip_not_required));
	}
	mode_lib->ms.support.ImmediateFlipRequiredButTheRequirementForEachSurfaceIsNotSpecified = mode_lib->ms.support.ImmediateFlipRequiredButTheRequirementForEachSurfaceIsNotSpecified && s->ImmediateFlipRequiredFinal;

	mode_lib->ms.support.ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe = false;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		mode_lib->ms.support.ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe =
										mode_lib->ms.support.ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe || ((mode_lib->ms.cache_display_cfg.plane.HostVMEnable == true || mode_lib->ms.policy.ImmediateFlipRequirement[k] != dml_immediate_flip_not_required) &&
										(mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange[k] == dml_use_mall_pstate_change_full_frame || mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange[k] == dml_use_mall_pstate_change_phantom_pipe));
	}

	mode_lib->ms.support.InvalidCombinationOfMALLUseForPStateAndStaticScreen = false;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		mode_lib->ms.support.InvalidCombinationOfMALLUseForPStateAndStaticScreen = mode_lib->ms.support.InvalidCombinationOfMALLUseForPStateAndStaticScreen ||
																((mode_lib->ms.cache_display_cfg.plane.UseMALLForStaticScreen[k] == dml_use_mall_static_screen_enable || mode_lib->ms.cache_display_cfg.plane.UseMALLForStaticScreen[k] == dml_use_mall_static_screen_optimize) && (mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange[k] == dml_use_mall_pstate_change_phantom_pipe)) ||
																((mode_lib->ms.cache_display_cfg.plane.UseMALLForStaticScreen[k] == dml_use_mall_static_screen_disable || mode_lib->ms.cache_display_cfg.plane.UseMALLForStaticScreen[k] == dml_use_mall_static_screen_optimize) && (mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange[k] == dml_use_mall_pstate_change_full_frame));
	}

	s->FullFrameMALLPStateMethod = false;
	s->SubViewportMALLPStateMethod = false;
	s->PhantomPipeMALLPStateMethod = false;
	s->SubViewportMALLRefreshGreaterThan120Hz = false;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange[k] == dml_use_mall_pstate_change_full_frame)
			s->FullFrameMALLPStateMethod = true;
		if (mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange[k] == dml_use_mall_pstate_change_sub_viewport) {
			s->SubViewportMALLPStateMethod = true;
			if (mode_lib->ms.cache_display_cfg.timing.RefreshRate[k] > 120)
				s->SubViewportMALLRefreshGreaterThan120Hz = true;
		}
		if (mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange[k] == dml_use_mall_pstate_change_phantom_pipe)
			s->PhantomPipeMALLPStateMethod = true;
	}
	mode_lib->ms.support.InvalidCombinationOfMALLUseForPState = (s->SubViewportMALLPStateMethod != s->PhantomPipeMALLPStateMethod)
	|| (s->SubViewportMALLPStateMethod && s->FullFrameMALLPStateMethod) || s->SubViewportMALLRefreshGreaterThan120Hz;

    if (mode_lib->ms.policy.UseMinimumRequiredDCFCLK == true) {
		UseMinimumDCFCLK_params->UseMALLForPStateChange = mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange;
		UseMinimumDCFCLK_params->DRRDisplay = mode_lib->ms.cache_display_cfg.timing.DRRDisplay;
		UseMinimumDCFCLK_params->SynchronizeDRRDisplaysForUCLKPStateChangeFinal = mode_lib->ms.policy.SynchronizeDRRDisplaysForUCLKPStateChangeFinal;
		UseMinimumDCFCLK_params->MaxInterDCNTileRepeaters = mode_lib->ms.ip.max_inter_dcn_tile_repeaters;
		UseMinimumDCFCLK_params->MaxPrefetchMode = dml_prefetch_support_stutter;
		UseMinimumDCFCLK_params->DRAMClockChangeLatencyFinal = mode_lib->ms.state.dram_clock_change_latency_us;
		UseMinimumDCFCLK_params->FCLKChangeLatency = mode_lib->ms.state.fclk_change_latency_us;
		UseMinimumDCFCLK_params->SREnterPlusExitTime = mode_lib->ms.state.sr_enter_plus_exit_time_us;
		UseMinimumDCFCLK_params->ReturnBusWidth = mode_lib->ms.soc.return_bus_width_bytes;
		UseMinimumDCFCLK_params->RoundTripPingLatencyCycles = mode_lib->ms.soc.round_trip_ping_latency_dcfclk_cycles;
		UseMinimumDCFCLK_params->ReorderingBytes = s->ReorderingBytes;
		UseMinimumDCFCLK_params->PixelChunkSizeInKByte = mode_lib->ms.ip.pixel_chunk_size_kbytes;
		UseMinimumDCFCLK_params->MetaChunkSize = mode_lib->ms.ip.meta_chunk_size_kbytes;
		UseMinimumDCFCLK_params->GPUVMEnable = mode_lib->ms.cache_display_cfg.plane.GPUVMEnable;
		UseMinimumDCFCLK_params->GPUVMMaxPageTableLevels = mode_lib->ms.cache_display_cfg.plane.GPUVMMaxPageTableLevels;
		UseMinimumDCFCLK_params->HostVMEnable = mode_lib->ms.cache_display_cfg.plane.HostVMEnable;
		UseMinimumDCFCLK_params->NumberOfActiveSurfaces = mode_lib->ms.num_active_planes;
		UseMinimumDCFCLK_params->HostVMMinPageSize = mode_lib->ms.soc.hostvm_min_page_size_kbytes * 1024;
		UseMinimumDCFCLK_params->HostVMMaxNonCachedPageTableLevels = mode_lib->ms.cache_display_cfg.plane.HostVMMaxPageTableLevels;
		UseMinimumDCFCLK_params->DynamicMetadataVMEnabled = mode_lib->ms.ip.dynamic_metadata_vm_enabled;
		UseMinimumDCFCLK_params->ImmediateFlipRequirement = s->ImmediateFlipRequiredFinal;
		UseMinimumDCFCLK_params->ProgressiveToInterlaceUnitInOPP = mode_lib->ms.ip.ptoi_supported;
		UseMinimumDCFCLK_params->MaxAveragePercentOfIdealSDPPortBWDisplayCanUseInNormalSystemOperation = mode_lib->ms.soc.max_avg_sdp_bw_use_normal_percent;
		UseMinimumDCFCLK_params->PercentOfIdealSDPPortBWReceivedAfterUrgLatency = mode_lib->ms.soc.pct_ideal_sdp_bw_after_urgent;
		UseMinimumDCFCLK_params->VTotal = mode_lib->ms.cache_display_cfg.timing.VTotal;
		UseMinimumDCFCLK_params->VActive = mode_lib->ms.cache_display_cfg.timing.VActive;
		UseMinimumDCFCLK_params->DynamicMetadataTransmittedBytes = mode_lib->ms.cache_display_cfg.plane.DynamicMetadataTransmittedBytes;
		UseMinimumDCFCLK_params->DynamicMetadataLinesBeforeActiveRequired = mode_lib->ms.cache_display_cfg.plane.DynamicMetadataLinesBeforeActiveRequired;
		UseMinimumDCFCLK_params->Interlace = mode_lib->ms.cache_display_cfg.timing.Interlace;
		UseMinimumDCFCLK_params->RequiredDPPCLKPerSurface = mode_lib->ms.RequiredDPPCLKPerSurface;
		UseMinimumDCFCLK_params->RequiredDISPCLK = mode_lib->ms.RequiredDISPCLK;
		UseMinimumDCFCLK_params->UrgLatency = mode_lib->ms.UrgLatency;
		UseMinimumDCFCLK_params->NoOfDPP = mode_lib->ms.NoOfDPP;
		UseMinimumDCFCLK_params->ProjectedDCFCLKDeepSleep = mode_lib->ms.ProjectedDCFCLKDeepSleep;
		UseMinimumDCFCLK_params->MaximumVStartup = s->MaximumVStartup;
		UseMinimumDCFCLK_params->TotalNumberOfActiveDPP = mode_lib->ms.TotalNumberOfActiveDPP;
		UseMinimumDCFCLK_params->TotalNumberOfDCCActiveDPP = mode_lib->ms.TotalNumberOfDCCActiveDPP;
		UseMinimumDCFCLK_params->dpte_group_bytes = mode_lib->ms.dpte_group_bytes;
		UseMinimumDCFCLK_params->PrefetchLinesY = mode_lib->ms.PrefetchLinesY;
		UseMinimumDCFCLK_params->PrefetchLinesC = mode_lib->ms.PrefetchLinesC;
		UseMinimumDCFCLK_params->swath_width_luma_ub_all_states = mode_lib->ms.swath_width_luma_ub_all_states;
		UseMinimumDCFCLK_params->swath_width_chroma_ub_all_states = mode_lib->ms.swath_width_chroma_ub_all_states;
		UseMinimumDCFCLK_params->BytePerPixelY = mode_lib->ms.BytePerPixelY;
		UseMinimumDCFCLK_params->BytePerPixelC = mode_lib->ms.BytePerPixelC;
		UseMinimumDCFCLK_params->HTotal = mode_lib->ms.cache_display_cfg.timing.HTotal;
		UseMinimumDCFCLK_params->PixelClock = mode_lib->ms.cache_display_cfg.timing.PixelClock;
		UseMinimumDCFCLK_params->PDEAndMetaPTEBytesPerFrame = mode_lib->ms.PDEAndMetaPTEBytesPerFrame;
		UseMinimumDCFCLK_params->DPTEBytesPerRow = mode_lib->ms.DPTEBytesPerRow;
		UseMinimumDCFCLK_params->MetaRowBytes = mode_lib->ms.MetaRowBytes;
		UseMinimumDCFCLK_params->DynamicMetadataEnable = mode_lib->ms.cache_display_cfg.plane.DynamicMetadataEnable;
		UseMinimumDCFCLK_params->ReadBandwidthLuma = mode_lib->ms.ReadBandwidthLuma;
		UseMinimumDCFCLK_params->ReadBandwidthChroma = mode_lib->ms.ReadBandwidthChroma;
		UseMinimumDCFCLK_params->DCFCLKPerState = mode_lib->ms.state.dcfclk_mhz;
		UseMinimumDCFCLK_params->DCFCLKState = mode_lib->ms.DCFCLKState;

		UseMinimumDCFCLK(&mode_lib->scratch,
		UseMinimumDCFCLK_params);

	 } // UseMinimumRequiredDCFCLK == true

	for (j = 0; j < 2; ++j) {
		mode_lib->ms.ReturnBWPerState[j] = dml_get_return_bw_mbps(&mode_lib->ms.soc, mode_lib->ms.state.use_ideal_dram_bw_strobe,
																mode_lib->ms.cache_display_cfg.plane.HostVMEnable, mode_lib->ms.DCFCLKState[j], mode_lib->ms.state.fabricclk_mhz,
																mode_lib->ms.state.dram_speed_mts);
		mode_lib->ms.ReturnDRAMBWPerState[j] = dml_get_return_dram_bw_mbps(&mode_lib->ms.soc, mode_lib->ms.state.use_ideal_dram_bw_strobe,
																mode_lib->ms.cache_display_cfg.plane.HostVMEnable,
																mode_lib->ms.state.dram_speed_mts);
	}

	//Re-ordering Buffer Support Check
	for (j = 0; j < 2; ++j) {
		if ((mode_lib->ms.ip.rob_buffer_size_kbytes - mode_lib->ms.ip.pixel_chunk_size_kbytes) * 1024 / mode_lib->ms.ReturnBWPerState[j] >
			(mode_lib->ms.soc.round_trip_ping_latency_dcfclk_cycles + 32) / mode_lib->ms.DCFCLKState[j] + s->ReorderingBytes / mode_lib->ms.ReturnBWPerState[j]) {
			mode_lib->ms.support.ROBSupport[j] = true;
		} else {
			mode_lib->ms.support.ROBSupport[j] = false;
		}
		dml_print("DML::%s: DEBUG ROBSupport[%u] = %u (%u)\n",  __func__, j, mode_lib->ms.support.ROBSupport[j], __LINE__);
	}

	//Vertical Active BW support check
	s->MaxTotalVActiveRDBandwidth = 0;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		s->MaxTotalVActiveRDBandwidth = s->MaxTotalVActiveRDBandwidth + mode_lib->ms.ReadBandwidthLuma[k] + mode_lib->ms.ReadBandwidthChroma[k];
	}

	for (j = 0; j < 2; ++j) {
		mode_lib->ms.support.MaxTotalVerticalActiveAvailableBandwidth[j] = dml_min3(mode_lib->ms.soc.return_bus_width_bytes * mode_lib->ms.DCFCLKState[j] * mode_lib->ms.soc.max_avg_sdp_bw_use_normal_percent / 100.0,
																	mode_lib->ms.state.fabricclk_mhz * mode_lib->ms.soc.fabric_datapath_to_dcn_data_return_bytes * mode_lib->ms.soc.max_avg_fabric_bw_use_normal_percent / 100.0,
																	mode_lib->ms.state.dram_speed_mts * mode_lib->ms.soc.num_chans * mode_lib->ms.soc.dram_channel_width_bytes *
																	((mode_lib->ms.state.use_ideal_dram_bw_strobe && !mode_lib->ms.cache_display_cfg.plane.HostVMEnable) ?
																	mode_lib->ms.soc.max_avg_dram_bw_use_normal_strobe_percent : mode_lib->ms.soc.max_avg_dram_bw_use_normal_percent) / 100.0);

		if (s->MaxTotalVActiveRDBandwidth <= mode_lib->ms.support.MaxTotalVerticalActiveAvailableBandwidth[j]) {
			mode_lib->ms.support.TotalVerticalActiveBandwidthSupport[j] = true;
		} else {
			mode_lib->ms.support.TotalVerticalActiveBandwidthSupport[j] = false;
		}
	}

	/* Prefetch Check */
	dml_prefetch_check(mode_lib);

	// End of Prefetch Check
	dml_print("DML::%s: Done prefetch calculation\n", __func__);

	/*Cursor Support Check*/
	mode_lib->ms.support.CursorSupport = true;
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (mode_lib->ms.cache_display_cfg.plane.CursorWidth[k] > 0.0) {
			if (mode_lib->ms.cache_display_cfg.plane.CursorBPP[k] == 64 && mode_lib->ms.ip.cursor_64bpp_support == false) {
				mode_lib->ms.support.CursorSupport = false;
			}
		}
	}

	/*Valid Pitch Check*/
	mode_lib->ms.support.PitchSupport = true;
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		mode_lib->ms.support.AlignedYPitch[k] = dml_ceil(
				dml_max(mode_lib->ms.cache_display_cfg.surface.PitchY[k], mode_lib->ms.cache_display_cfg.surface.SurfaceWidthY[k]),
				mode_lib->ms.MacroTileWidthY[k]);
		if (mode_lib->ms.cache_display_cfg.surface.DCCEnable[k] == true) {
			mode_lib->ms.support.AlignedDCCMetaPitchY[k] = dml_ceil(dml_max(mode_lib->ms.cache_display_cfg.surface.DCCMetaPitchY[k], mode_lib->ms.cache_display_cfg.surface.SurfaceWidthY[k]), 64.0 * mode_lib->ms.Read256BlockWidthY[k]);
		} else {
			mode_lib->ms.support.AlignedDCCMetaPitchY[k] = mode_lib->ms.cache_display_cfg.surface.DCCMetaPitchY[k];
		}
		if (mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_444_64
			&& mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_444_32
			&& mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_444_16
			&& mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_mono_16
			&& mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_rgbe
			&& mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_mono_8) {
			mode_lib->ms.support.AlignedCPitch[k] = dml_ceil(dml_max(mode_lib->ms.cache_display_cfg.surface.PitchC[k], mode_lib->ms.cache_display_cfg.surface.SurfaceWidthC[k]), mode_lib->ms.MacroTileWidthC[k]);
			if (mode_lib->ms.cache_display_cfg.surface.DCCEnable[k] == true) {
				mode_lib->ms.support.AlignedDCCMetaPitchC[k] = dml_ceil(dml_max(mode_lib->ms.cache_display_cfg.surface.DCCMetaPitchC[k], mode_lib->ms.cache_display_cfg.surface.SurfaceWidthC[k]), 64.0 * mode_lib->ms.Read256BlockWidthC[k]);
			} else {
				mode_lib->ms.support.AlignedDCCMetaPitchC[k] = mode_lib->ms.cache_display_cfg.surface.DCCMetaPitchC[k];
			}
		} else {
			mode_lib->ms.support.AlignedCPitch[k] = mode_lib->ms.cache_display_cfg.surface.PitchC[k];
			mode_lib->ms.support.AlignedDCCMetaPitchC[k] = mode_lib->ms.cache_display_cfg.surface.DCCMetaPitchC[k];
		}
		if (mode_lib->ms.support.AlignedYPitch[k] > mode_lib->ms.cache_display_cfg.surface.PitchY[k] || mode_lib->ms.support.AlignedCPitch[k] > mode_lib->ms.cache_display_cfg.surface.PitchC[k] ||
			mode_lib->ms.support.AlignedDCCMetaPitchY[k] > mode_lib->ms.cache_display_cfg.surface.DCCMetaPitchY[k] || mode_lib->ms.support.AlignedDCCMetaPitchC[k] > mode_lib->ms.cache_display_cfg.surface.DCCMetaPitchC[k]) {
			mode_lib->ms.support.PitchSupport = false;
		}
	}

	mode_lib->ms.support.ViewportExceedsSurface = false;
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (mode_lib->ms.cache_display_cfg.plane.ViewportWidth[k] > mode_lib->ms.cache_display_cfg.surface.SurfaceWidthY[k] || mode_lib->ms.cache_display_cfg.plane.ViewportHeight[k] > mode_lib->ms.cache_display_cfg.surface.SurfaceHeightY[k]) {
			mode_lib->ms.support.ViewportExceedsSurface = true;
			if (mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_444_64 && mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_444_32 &&
				mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_444_16 && mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_444_8 && mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k] != dml_rgbe) {
				if (mode_lib->ms.cache_display_cfg.plane.ViewportWidthChroma[k] > mode_lib->ms.cache_display_cfg.surface.SurfaceWidthC[k] || mode_lib->ms.cache_display_cfg.plane.ViewportHeightChroma[k] > mode_lib->ms.cache_display_cfg.surface.SurfaceHeightC[k]) {
					mode_lib->ms.support.ViewportExceedsSurface = true;
				}
			}
		}
	}

	/*Mode Support, Voltage State and SOC Configuration*/
	for (j = 0; j < 2; j++) { // j iterator is for the combine mode off or on
		dml_print("DML::%s: checking support for j=%u\n", __func__, j);
		dml_print("DML::%s: state_idx=%0d max_state_idx=%0d\n", __func__, mode_lib->ms.state_idx, mode_lib->ms.max_state_idx);

		s->is_max_pwr_state = (mode_lib->ms.max_state_idx == mode_lib->ms.state_idx);
		s->is_max_dram_pwr_state = (mode_lib->ms.max_state.dram_speed_mts == mode_lib->ms.state.dram_speed_mts);

		s->dram_clock_change_support = (!mode_lib->ms.policy.DRAMClockChangeRequirementFinal ||
											(s->is_max_dram_pwr_state && mode_lib->policy.AssumeModeSupportAtMaxPwrStateEvenDRAMClockChangeNotSupported) ||
											mode_lib->ms.support.DRAMClockChangeSupport[j] != dml_dram_clock_change_unsupported);
		s->f_clock_change_support = (!mode_lib->ms.policy.FCLKChangeRequirementFinal ||
											(s->is_max_pwr_state && mode_lib->policy.AssumeModeSupportAtMaxPwrStateEvenFClockChangeNotSupported) ||
											mode_lib->ms.support.FCLKChangeSupport[j] != dml_fclock_change_unsupported);

		if (mode_lib->ms.support.ScaleRatioAndTapsSupport == true
			&& mode_lib->ms.support.SourceFormatPixelAndScanSupport == true
			&& mode_lib->ms.support.ViewportSizeSupport[j] == true
			&& !mode_lib->ms.support.LinkRateDoesNotMatchDPVersion
			&& !mode_lib->ms.support.LinkRateForMultistreamNotIndicated
			&& !mode_lib->ms.support.BPPForMultistreamNotIndicated
			&& !mode_lib->ms.support.MultistreamWithHDMIOreDP
			&& !mode_lib->ms.support.ExceededMultistreamSlots
			&& !mode_lib->ms.support.MSOOrODMSplitWithNonDPLink
			&& !mode_lib->ms.support.NotEnoughLanesForMSO
			&& mode_lib->ms.support.LinkCapacitySupport == true
			&& !mode_lib->ms.support.P2IWith420
			&& !mode_lib->ms.support.DSCOnlyIfNecessaryWithBPP
			&& !mode_lib->ms.support.DSC422NativeNotSupported
			&& !mode_lib->ms.support.MPCCombineMethodIncompatible
			&& mode_lib->ms.support.ODMCombineTwoToOneSupportCheckOK == true
			&& mode_lib->ms.support.ODMCombineFourToOneSupportCheckOK == true
			&& mode_lib->ms.support.NotEnoughDSCUnits == false
			&& !mode_lib->ms.support.NotEnoughDSCSlices
			&& !mode_lib->ms.support.ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe
			&& !mode_lib->ms.support.InvalidCombinationOfMALLUseForPStateAndStaticScreen
			&& mode_lib->ms.support.DSCCLKRequiredMoreThanSupported == false
			&& mode_lib->ms.support.PixelsPerLinePerDSCUnitSupport
			&& mode_lib->ms.support.DTBCLKRequiredMoreThanSupported == false
			&& !mode_lib->ms.support.InvalidCombinationOfMALLUseForPState
			&& !mode_lib->ms.support.ImmediateFlipRequiredButTheRequirementForEachSurfaceIsNotSpecified
			&& mode_lib->ms.support.ROBSupport[j] == true
			&& mode_lib->ms.support.DISPCLK_DPPCLK_Support[j] == true
			&& mode_lib->ms.support.TotalAvailablePipesSupport[j] == true
			&& mode_lib->ms.support.NumberOfOTGSupport == true
			&& mode_lib->ms.support.NumberOfHDMIFRLSupport == true
			&& mode_lib->ms.support.NumberOfDP2p0Support == true
			&& mode_lib->ms.support.EnoughWritebackUnits == true
			&& mode_lib->ms.support.WritebackLatencySupport == true
			&& mode_lib->ms.support.WritebackScaleRatioAndTapsSupport == true
			&& mode_lib->ms.support.CursorSupport == true
			&& mode_lib->ms.support.PitchSupport == true
			&& mode_lib->ms.support.ViewportExceedsSurface == false
			&& mode_lib->ms.support.PrefetchSupported[j] == true
			&& mode_lib->ms.support.VActiveBandwithSupport[j] == true
			&& mode_lib->ms.support.DynamicMetadataSupported[j] == true
			&& mode_lib->ms.support.TotalVerticalActiveBandwidthSupport[j] == true
			&& mode_lib->ms.support.VRatioInPrefetchSupported[j] == true
			&& mode_lib->ms.support.PTEBufferSizeNotExceeded[j] == true
			&& mode_lib->ms.support.DCCMetaBufferSizeNotExceeded[j] == true
			&& mode_lib->ms.support.NonsupportedDSCInputBPC == false
			&& !mode_lib->ms.support.ExceededMALLSize
			&& ((mode_lib->ms.cache_display_cfg.plane.HostVMEnable == false && !s->ImmediateFlipRequiredFinal) || mode_lib->ms.support.ImmediateFlipSupportedForState[j])
			&& s->dram_clock_change_support == true
			&& s->f_clock_change_support == true
			&& (!mode_lib->ms.policy.USRRetrainingRequiredFinal || mode_lib->ms.support.USRRetrainingSupport[j])) {
			dml_print("DML::%s: mode is supported\n", __func__);
			mode_lib->ms.support.ModeSupport[j] = true;
		} else {
			dml_print("DML::%s: mode is NOT supported\n", __func__);
			mode_lib->ms.support.ModeSupport[j] = false;
			dml_print_mode_support(mode_lib, j);
		}
	}

	mode_lib->ms.support.MaximumMPCCombine = 0;
	mode_lib->ms.support.ModeIsSupported = 0;
	if (mode_lib->ms.support.ModeSupport[0] == true || mode_lib->ms.support.ModeSupport[1] == true) {  // if the mode is supported by either no combine or mpccombine
		mode_lib->ms.support.ModeIsSupported = mode_lib->ms.support.ModeSupport[0] == true || mode_lib->ms.support.ModeSupport[1] == true;

		// Determine if MPC combine is necessary, depends on if using MPC combine will help dram clock change or fclk change, etc.
		if ((mode_lib->ms.support.ModeSupport[0] == false && mode_lib->ms.support.ModeSupport[1] == true) || s->MPCCombineMethodAsPossible ||
			(s->MPCCombineMethodAsNeededForPStateChangeAndVoltage && mode_lib->ms.policy.DRAMClockChangeRequirementFinal &&
			(((mode_lib->ms.support.DRAMClockChangeSupport[1] == dml_dram_clock_change_vactive || mode_lib->ms.support.DRAMClockChangeSupport[1] == dml_dram_clock_change_vactive_w_mall_full_frame || mode_lib->ms.support.DRAMClockChangeSupport[1] == dml_dram_clock_change_vactive_w_mall_sub_vp) &&
			!(mode_lib->ms.support.DRAMClockChangeSupport[0] == dml_dram_clock_change_vactive || mode_lib->ms.support.DRAMClockChangeSupport[0] == dml_dram_clock_change_vactive_w_mall_full_frame || mode_lib->ms.support.DRAMClockChangeSupport[0] == dml_dram_clock_change_vactive_w_mall_sub_vp)) ||
			((mode_lib->ms.support.DRAMClockChangeSupport[1] == dml_dram_clock_change_vblank || mode_lib->ms.support.DRAMClockChangeSupport[1] == dml_dram_clock_change_vblank_drr
		|| mode_lib->ms.support.DRAMClockChangeSupport[1] == dml_dram_clock_change_vblank_w_mall_full_frame || mode_lib->ms.support.DRAMClockChangeSupport[1] == dml_dram_clock_change_vblank_drr_w_mall_full_frame
		|| mode_lib->ms.support.DRAMClockChangeSupport[1] == dml_dram_clock_change_vblank_w_mall_sub_vp || mode_lib->ms.support.DRAMClockChangeSupport[1] == dml_dram_clock_change_vblank_drr_w_mall_sub_vp
		) &&
				mode_lib->ms.support.DRAMClockChangeSupport[0] == dml_dram_clock_change_unsupported)))
			|| (s->MPCCombineMethodAsNeededForPStateChangeAndVoltage && mode_lib->ms.policy.FCLKChangeRequirementFinal &&
				((mode_lib->ms.support.FCLKChangeSupport[1] == dml_fclock_change_vactive && mode_lib->ms.support.FCLKChangeSupport[0] != dml_fclock_change_vactive) ||
				(mode_lib->ms.support.FCLKChangeSupport[1] == dml_fclock_change_vblank && mode_lib->ms.support.FCLKChangeSupport[0] == dml_fclock_change_unsupported)))) {
			mode_lib->ms.support.MaximumMPCCombine = 1;
		} else {
			mode_lib->ms.support.MaximumMPCCombine = 0;
		}
	}

	// Since now the mode_support work on 1 particular power state, so there is only 1 state idx (index 0).
	mode_lib->ms.support.ImmediateFlipSupport          = mode_lib->ms.support.ImmediateFlipSupportedForState[mode_lib->ms.support.MaximumMPCCombine];   // Consider flip support if max combine support imm flip
	mode_lib->ms.support.UnboundedRequestEnabled       = mode_lib->ms.UnboundedRequestEnabledAllStates[mode_lib->ms.support.MaximumMPCCombine];         // Not used, informational
	mode_lib->ms.support.CompressedBufferSizeInkByte   = mode_lib->ms.CompressedBufferSizeInkByteAllStates[mode_lib->ms.support.MaximumMPCCombine];     // Not used, informational

	dml_print("DML::%s: ModeIsSupported                = %u\n", __func__, mode_lib->ms.support.ModeIsSupported);
	dml_print("DML::%s: MaximumMPCCombine              = %u\n", __func__, mode_lib->ms.support.MaximumMPCCombine);
	dml_print("DML::%s: ImmediateFlipSupport           = %u\n", __func__, mode_lib->ms.support.ImmediateFlipSupport);
	dml_print("DML::%s: UnboundedRequestEnabled        = %u\n", __func__, mode_lib->ms.support.UnboundedRequestEnabled);
	dml_print("DML::%s: CompressedBufferSizeInkByte    = %u\n", __func__, mode_lib->ms.support.CompressedBufferSizeInkByte);

	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		mode_lib->ms.support.MPCCombineEnable[k]   = mode_lib->ms.MPCCombine[mode_lib->ms.support.MaximumMPCCombine][k];
		mode_lib->ms.support.DPPPerSurface[k]      = mode_lib->ms.NoOfDPP[mode_lib->ms.support.MaximumMPCCombine][k];
		mode_lib->ms.SwathHeightY[k]               = mode_lib->ms.SwathHeightYAllStates[mode_lib->ms.support.MaximumMPCCombine][k];
		mode_lib->ms.SwathHeightC[k]               = mode_lib->ms.SwathHeightCAllStates[mode_lib->ms.support.MaximumMPCCombine][k];
		mode_lib->ms.DETBufferSizeInKByte[k]       = mode_lib->ms.DETBufferSizeInKByteAllStates[mode_lib->ms.support.MaximumMPCCombine][k];
		mode_lib->ms.DETBufferSizeY[k]             = mode_lib->ms.DETBufferSizeYAllStates[mode_lib->ms.support.MaximumMPCCombine][k];
		mode_lib->ms.DETBufferSizeC[k]             = mode_lib->ms.DETBufferSizeCAllStates[mode_lib->ms.support.MaximumMPCCombine][k];
	}

	mode_lib->ms.DRAMSpeed     = mode_lib->ms.state.dram_speed_mts;
	mode_lib->ms.FabricClock   = mode_lib->ms.state.fabricclk_mhz;
	mode_lib->ms.SOCCLK        = mode_lib->ms.state.socclk_mhz;
	mode_lib->ms.DCFCLK        = mode_lib->ms.DCFCLKState[mode_lib->ms.support.MaximumMPCCombine];
	mode_lib->ms.ReturnBW      = mode_lib->ms.ReturnBWPerState[mode_lib->ms.support.MaximumMPCCombine];
	mode_lib->ms.ReturnDRAMBW  = mode_lib->ms.ReturnDRAMBWPerState[mode_lib->ms.support.MaximumMPCCombine];

	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		if (mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == k) {
			mode_lib->ms.support.ODMMode[k] = mode_lib->ms.ODMModePerState[k];
		} else {
			mode_lib->ms.support.ODMMode[k] = dml_odm_mode_bypass;
		}

		mode_lib->ms.support.DSCEnabled[k] = mode_lib->ms.RequiresDSC[k];
		mode_lib->ms.support.FECEnabled[k] = mode_lib->ms.RequiresFEC[k];
		mode_lib->ms.support.OutputBpp[k] = mode_lib->ms.OutputBppPerState[k];
		mode_lib->ms.support.OutputType[k] = mode_lib->ms.OutputTypePerState[k];
		mode_lib->ms.support.OutputRate[k] = mode_lib->ms.OutputRatePerState[k];
		mode_lib->ms.support.SubViewportLinesNeededInMALL[k] = mode_lib->ms.SubViewportLinesNeededInMALL[k];
	}

	return mode_lib->ms.support.ModeIsSupported;
} // dml_core_mode_support

/// @brief This function calculates some parameters thats are needed ahead of the mode programming function all
void dml_core_mode_support_partial(struct display_mode_lib_st *mode_lib)
{
	CalculateMaxDETAndMinCompressedBufferSize(
								mode_lib->ms.ip.config_return_buffer_size_in_kbytes,
								mode_lib->ms.ip.config_return_buffer_segment_size_in_kbytes,
								mode_lib->ms.ip.rob_buffer_size_kbytes,
								mode_lib->ms.ip.max_num_dpp,
								mode_lib->ms.policy.NomDETInKByteOverrideEnable,
								mode_lib->ms.policy.NomDETInKByteOverrideValue,

								/* Output */
								&mode_lib->ms.MaxTotalDETInKByte,
								&mode_lib->ms.NomDETInKByte,
								&mode_lib->ms.MinCompressedBufferSizeInKByte);

	PixelClockAdjustmentForProgressiveToInterlaceUnit(&mode_lib->ms.cache_display_cfg, mode_lib->ms.ip.ptoi_supported);

	mode_lib->ms.ReturnBW = dml_get_return_bw_mbps(&mode_lib->ms.soc,
													mode_lib->ms.state.use_ideal_dram_bw_strobe,
													mode_lib->ms.cache_display_cfg.plane.HostVMEnable,
													mode_lib->ms.DCFCLK,
													mode_lib->ms.FabricClock,
													mode_lib->ms.DRAMSpeed);
	dml_print("DML::%s: ReturnBW = %f\n", __func__, mode_lib->ms.ReturnBW);

} // dml_core_mode_support_partial

/// @brief This is the mode programming function. It is assumed the display cfg is support at the given power state
void dml_core_mode_programming(struct display_mode_lib_st *mode_lib, const struct dml_clk_cfg_st *clk_cfg)
{
	struct dml_core_mode_programming_locals_st *s = &mode_lib->scratch.dml_core_mode_programming_locals;
	struct CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params_st *CalculateWatermarks_params = &mode_lib->scratch.CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport_params;
	struct CalculateVMRowAndSwath_params_st *CalculateVMRowAndSwath_params = &mode_lib->scratch.CalculateVMRowAndSwath_params;
	struct CalculateSwathAndDETConfiguration_params_st *CalculateSwathAndDETConfiguration_params = &mode_lib->scratch.CalculateSwathAndDETConfiguration_params;
	struct CalculateStutterEfficiency_params_st *CalculateStutterEfficiency_params = &mode_lib->scratch.CalculateStutterEfficiency_params;
	struct CalculatePrefetchSchedule_params_st *CalculatePrefetchSchedule_params = &mode_lib->scratch.CalculatePrefetchSchedule_params;

	struct mode_program_st   *locals    = &mode_lib->mp;
	struct DmlPipe *myPipe;
	dml_uint_t j = 0, k = 0;
	dml_float_t TWait;
	dml_bool_t isInterlaceTiming;

	mode_lib->ms.num_active_planes = dml_get_num_active_planes(&mode_lib->ms.cache_display_cfg);
	mode_lib->mp.num_active_pipes = dml_get_num_active_pipes(&mode_lib->ms.cache_display_cfg);
	dml_calc_pipe_plane_mapping(&mode_lib->ms.cache_display_cfg.hw, mode_lib->mp.pipe_plane);

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: --- START --- \n",  __func__);
	dml_print("DML::%s: num_active_planes = %u\n", __func__, mode_lib->ms.num_active_planes);
	dml_print("DML::%s: num_active_pipes = %u\n", __func__, mode_lib->mp.num_active_pipes);
#endif

	s->DSCFormatFactor          = 0;

	// Unlike dppclk and dispclk which can be calculated in mode_programming
	// DCFCLK is calculated in mode_support (which is the state bbox dcfclk or min dcfclk if min dcfclk option is used in mode support calculation)
	if (clk_cfg->dcfclk_option != dml_use_override_freq)
		locals->Dcfclk = mode_lib->ms.DCFCLK;
	else
		locals->Dcfclk = clk_cfg->dcfclk_freq_mhz;

#ifdef __DML_VBA_DEBUG__
	dml_print_dml_policy(&mode_lib->ms.policy);
	dml_print_soc_state_bounding_box(&mode_lib->ms.state);
	dml_print_soc_bounding_box(&mode_lib->ms.soc);
	dml_print_clk_cfg(clk_cfg);

	dml_print("DML::%s: ImmediateFlipSupport = %u\n", __func__, mode_lib->ms.support.ImmediateFlipSupport);
	dml_print("DML::%s: Using DCFCLK = %f\n", __func__, locals->Dcfclk);
	dml_print("DML::%s: Using SOCCLK = %f\n", __func__, mode_lib->ms.SOCCLK);
#endif

	locals->WritebackDISPCLK = 0.0;
	locals->GlobalDPPCLK = 0.0;

	// DISPCLK and DPPCLK Calculation
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.cache_display_cfg.writeback.WritebackEnable[k]) {
			locals->WritebackDISPCLK =
					dml_max(
							locals->WritebackDISPCLK,
							CalculateWriteBackDISPCLK(
									mode_lib->ms.cache_display_cfg.writeback.WritebackPixelFormat[k],
									mode_lib->ms.cache_display_cfg.timing.PixelClock[k],
									mode_lib->ms.cache_display_cfg.writeback.WritebackHRatio[k],
									mode_lib->ms.cache_display_cfg.writeback.WritebackVRatio[k],
									mode_lib->ms.cache_display_cfg.writeback.WritebackHTaps[k],
									mode_lib->ms.cache_display_cfg.writeback.WritebackVTaps[k],
									mode_lib->ms.cache_display_cfg.writeback.WritebackSourceWidth[k],
									mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationWidth[k],
									mode_lib->ms.cache_display_cfg.timing.HTotal[k],
									mode_lib->ms.ip.writeback_line_buffer_buffer_size,
									mode_lib->ms.soc.dispclk_dppclk_vco_speed_mhz));
		}
	}

	locals->Dispclk_calculated = locals->WritebackDISPCLK;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == k) {
				locals->Dispclk_calculated = dml_max(locals->Dispclk_calculated, CalculateRequiredDispclk(
																					mode_lib->ms.cache_display_cfg.hw.ODMMode[k],
																					mode_lib->ms.cache_display_cfg.timing.PixelClock[k],
																					mode_lib->ms.soc.dcn_downspread_percent,
																					mode_lib->ms.ip.dispclk_ramp_margin_percent,
																					mode_lib->ms.soc.dispclk_dppclk_vco_speed_mhz,
																					mode_lib->ms.max_state.dispclk_mhz));
		}
	}
	if (clk_cfg->dispclk_option == dml_use_required_freq)
		locals->Dispclk = locals->Dispclk_calculated;
	else if (clk_cfg->dispclk_option == dml_use_override_freq)
		locals->Dispclk = clk_cfg->dispclk_freq_mhz;
	else
		locals->Dispclk = mode_lib->ms.state.dispclk_mhz;
#ifdef __DML_VBA_DEBUG__
	 dml_print("DML::%s: Using Dispclk = %f\n", __func__, locals->Dispclk);
#endif

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		CalculateSinglePipeDPPCLKAndSCLThroughput(
				mode_lib->ms.cache_display_cfg.plane.HRatio[k],
				mode_lib->ms.cache_display_cfg.plane.HRatioChroma[k],
				mode_lib->ms.cache_display_cfg.plane.VRatio[k],
				mode_lib->ms.cache_display_cfg.plane.VRatioChroma[k],
				mode_lib->ms.ip.max_dchub_pscl_bw_pix_per_clk,
				mode_lib->ms.ip.max_pscl_lb_bw_pix_per_clk,
				mode_lib->ms.cache_display_cfg.timing.PixelClock[k],
				mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k],
				mode_lib->ms.cache_display_cfg.plane.HTaps[k],
				mode_lib->ms.cache_display_cfg.plane.HTapsChroma[k],
				mode_lib->ms.cache_display_cfg.plane.VTaps[k],
				mode_lib->ms.cache_display_cfg.plane.VTapsChroma[k],

				/* Output */
				&locals->PSCL_THROUGHPUT[k],
				&locals->PSCL_THROUGHPUT_CHROMA[k],
				&locals->DPPCLKUsingSingleDPP[k]);
	}

	CalculateDPPCLK(mode_lib->ms.num_active_planes,
					mode_lib->ms.soc.dcn_downspread_percent,
					mode_lib->ms.soc.dispclk_dppclk_vco_speed_mhz,
					locals->DPPCLKUsingSingleDPP,
					mode_lib->ms.cache_display_cfg.hw.DPPPerSurface,
					/* Output */
					&locals->GlobalDPPCLK,
					locals->Dppclk_calculated);

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (clk_cfg->dppclk_option[k] == dml_use_required_freq)
			locals->Dppclk[k] = locals->Dppclk_calculated[k];
		else if (clk_cfg->dppclk_option[k] == dml_use_override_freq)
			locals->Dppclk[k] = clk_cfg->dppclk_freq_mhz[k];
		else
			locals->Dppclk[k] = mode_lib->ms.state.dppclk_mhz;
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: Using Dppclk[%0d] = %f\n", __func__, k, locals->Dppclk[k]);
#endif
	}

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		CalculateBytePerPixelAndBlockSizes(
				mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k],
				mode_lib->ms.cache_display_cfg.surface.SurfaceTiling[k],

				/* Output */
				&locals->BytePerPixelY[k],
				&locals->BytePerPixelC[k],
				&locals->BytePerPixelDETY[k],
				&locals->BytePerPixelDETC[k],
				&locals->BlockHeight256BytesY[k],
				&locals->BlockHeight256BytesC[k],
				&locals->BlockWidth256BytesY[k],
				&locals->BlockWidth256BytesC[k],
				&locals->BlockHeightY[k],
				&locals->BlockHeightC[k],
				&locals->BlockWidthY[k],
				&locals->BlockWidthC[k]);
	}


	dml_print("DML::%s: %u\n", __func__, __LINE__);
	CalculateSwathWidth(
		false,  // ForceSingleDPP
		mode_lib->ms.num_active_planes,
		mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat,
		mode_lib->ms.cache_display_cfg.plane.SourceScan,
		mode_lib->ms.cache_display_cfg.plane.ViewportStationary,
		mode_lib->ms.cache_display_cfg.plane.ViewportWidth,
		mode_lib->ms.cache_display_cfg.plane.ViewportHeight,
		mode_lib->ms.cache_display_cfg.plane.ViewportXStart,
		mode_lib->ms.cache_display_cfg.plane.ViewportYStart,
		mode_lib->ms.cache_display_cfg.plane.ViewportXStartC,
		mode_lib->ms.cache_display_cfg.plane.ViewportYStartC,
		mode_lib->ms.cache_display_cfg.surface.SurfaceWidthY,
		mode_lib->ms.cache_display_cfg.surface.SurfaceWidthC,
		mode_lib->ms.cache_display_cfg.surface.SurfaceHeightY,
		mode_lib->ms.cache_display_cfg.surface.SurfaceHeightC,
		mode_lib->ms.cache_display_cfg.hw.ODMMode,
		locals->BytePerPixelY,
		locals->BytePerPixelC,
		locals->BlockHeight256BytesY,
		locals->BlockHeight256BytesC,
		locals->BlockWidth256BytesY,
		locals->BlockWidth256BytesC,
		mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming,
		mode_lib->ms.cache_display_cfg.timing.HActive,
		mode_lib->ms.cache_display_cfg.plane.HRatio,
		mode_lib->ms.cache_display_cfg.hw.DPPPerSurface,

		/* Output */
		locals->SwathWidthSingleDPPY,
		locals->SwathWidthSingleDPPC,
		locals->SwathWidthY,
		locals->SwathWidthC,
		s->dummy_integer_array[0], // dml_uint_t MaximumSwathHeightY[]
		s->dummy_integer_array[1], // dml_uint_t MaximumSwathHeightC[]
		locals->swath_width_luma_ub,
		locals->swath_width_chroma_ub);

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		locals->ReadBandwidthSurfaceLuma[k] = locals->SwathWidthSingleDPPY[k] * locals->BytePerPixelY[k] / (mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k]) * mode_lib->ms.cache_display_cfg.plane.VRatio[k];
		locals->ReadBandwidthSurfaceChroma[k] = locals->SwathWidthSingleDPPC[k] * locals->BytePerPixelC[k] / (mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k]) * mode_lib->ms.cache_display_cfg.plane.VRatioChroma[k];
		dml_print("DML::%s: ReadBandwidthSurfaceLuma[%i] = %fBps\n", __func__, k, locals->ReadBandwidthSurfaceLuma[k]);
		dml_print("DML::%s: ReadBandwidthSurfaceChroma[%i] = %fBps\n", __func__, k, locals->ReadBandwidthSurfaceChroma[k]);
	}

	CalculateSwathAndDETConfiguration_params->DETSizeOverride = mode_lib->ms.cache_display_cfg.plane.DETSizeOverride;
	CalculateSwathAndDETConfiguration_params->UseMALLForPStateChange = mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange;
	CalculateSwathAndDETConfiguration_params->ConfigReturnBufferSizeInKByte = mode_lib->ms.ip.config_return_buffer_size_in_kbytes;
	CalculateSwathAndDETConfiguration_params->ROBBufferSizeInKByte = mode_lib->ms.ip.rob_buffer_size_kbytes;
	CalculateSwathAndDETConfiguration_params->MaxTotalDETInKByte = mode_lib->ms.MaxTotalDETInKByte;
	CalculateSwathAndDETConfiguration_params->MinCompressedBufferSizeInKByte = mode_lib->ms.MinCompressedBufferSizeInKByte;
	CalculateSwathAndDETConfiguration_params->PixelChunkSizeInKByte = mode_lib->ms.ip.pixel_chunk_size_kbytes;
	CalculateSwathAndDETConfiguration_params->ForceSingleDPP = false;
	CalculateSwathAndDETConfiguration_params->NumberOfActiveSurfaces = mode_lib->ms.num_active_planes;
	CalculateSwathAndDETConfiguration_params->nomDETInKByte = mode_lib->ms.NomDETInKByte;
	CalculateSwathAndDETConfiguration_params->UseUnboundedRequestingFinal = mode_lib->ms.policy.UseUnboundedRequesting;
	CalculateSwathAndDETConfiguration_params->ConfigReturnBufferSegmentSizeInkByte = mode_lib->ms.ip.config_return_buffer_segment_size_in_kbytes;
	CalculateSwathAndDETConfiguration_params->CompressedBufferSegmentSizeInkByteFinal = mode_lib->ms.ip.compressed_buffer_segment_size_in_kbytes;
	CalculateSwathAndDETConfiguration_params->Output = s->dummy_output_encoder_array;
	CalculateSwathAndDETConfiguration_params->ReadBandwidthLuma = locals->ReadBandwidthSurfaceLuma;
	CalculateSwathAndDETConfiguration_params->ReadBandwidthChroma = locals->ReadBandwidthSurfaceChroma;
	CalculateSwathAndDETConfiguration_params->MaximumSwathWidthLuma = s->dummy_single_array[0];
	CalculateSwathAndDETConfiguration_params->MaximumSwathWidthChroma = s->dummy_single_array[1];
	CalculateSwathAndDETConfiguration_params->SourceScan = mode_lib->ms.cache_display_cfg.plane.SourceScan;
	CalculateSwathAndDETConfiguration_params->ViewportStationary = mode_lib->ms.cache_display_cfg.plane.ViewportStationary;
	CalculateSwathAndDETConfiguration_params->SourcePixelFormat = mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat;
	CalculateSwathAndDETConfiguration_params->SurfaceTiling = mode_lib->ms.cache_display_cfg.surface.SurfaceTiling;
	CalculateSwathAndDETConfiguration_params->ViewportWidth = mode_lib->ms.cache_display_cfg.plane.ViewportWidth;
	CalculateSwathAndDETConfiguration_params->ViewportHeight = mode_lib->ms.cache_display_cfg.plane.ViewportHeight;
	CalculateSwathAndDETConfiguration_params->ViewportXStart = mode_lib->ms.cache_display_cfg.plane.ViewportXStart;
	CalculateSwathAndDETConfiguration_params->ViewportYStart = mode_lib->ms.cache_display_cfg.plane.ViewportYStart;
	CalculateSwathAndDETConfiguration_params->ViewportXStartC = mode_lib->ms.cache_display_cfg.plane.ViewportXStartC;
	CalculateSwathAndDETConfiguration_params->ViewportYStartC = mode_lib->ms.cache_display_cfg.plane.ViewportYStartC;
	CalculateSwathAndDETConfiguration_params->SurfaceWidthY = mode_lib->ms.cache_display_cfg.surface.SurfaceWidthY;
	CalculateSwathAndDETConfiguration_params->SurfaceWidthC = mode_lib->ms.cache_display_cfg.surface.SurfaceWidthC;
	CalculateSwathAndDETConfiguration_params->SurfaceHeightY = mode_lib->ms.cache_display_cfg.surface.SurfaceHeightY;
	CalculateSwathAndDETConfiguration_params->SurfaceHeightC = mode_lib->ms.cache_display_cfg.surface.SurfaceHeightC;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockHeightY = locals->BlockHeight256BytesY;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockHeightC = locals->BlockHeight256BytesC;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockWidthY = locals->BlockWidth256BytesY;
	CalculateSwathAndDETConfiguration_params->Read256BytesBlockWidthC = locals->BlockWidth256BytesC;
	CalculateSwathAndDETConfiguration_params->ODMMode = mode_lib->ms.cache_display_cfg.hw.ODMMode;
	CalculateSwathAndDETConfiguration_params->BlendingAndTiming = mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming;
	CalculateSwathAndDETConfiguration_params->BytePerPixY = locals->BytePerPixelY;
	CalculateSwathAndDETConfiguration_params->BytePerPixC = locals->BytePerPixelC;
	CalculateSwathAndDETConfiguration_params->BytePerPixDETY = locals->BytePerPixelDETY;
	CalculateSwathAndDETConfiguration_params->BytePerPixDETC = locals->BytePerPixelDETC;
	CalculateSwathAndDETConfiguration_params->HActive = mode_lib->ms.cache_display_cfg.timing.HActive;
	CalculateSwathAndDETConfiguration_params->HRatio = mode_lib->ms.cache_display_cfg.plane.HRatio;
	CalculateSwathAndDETConfiguration_params->HRatioChroma = mode_lib->ms.cache_display_cfg.plane.HRatioChroma;
	CalculateSwathAndDETConfiguration_params->DPPPerSurface = mode_lib->ms.cache_display_cfg.hw.DPPPerSurface;
	CalculateSwathAndDETConfiguration_params->swath_width_luma_ub = s->dummy_long_array[0];
	CalculateSwathAndDETConfiguration_params->swath_width_chroma_ub = s->dummy_long_array[1];
	CalculateSwathAndDETConfiguration_params->SwathWidth = s->dummy_long_array[2];
	CalculateSwathAndDETConfiguration_params->SwathWidthChroma = s->dummy_long_array[3];
	CalculateSwathAndDETConfiguration_params->SwathHeightY = locals->SwathHeightY;
	CalculateSwathAndDETConfiguration_params->SwathHeightC = locals->SwathHeightC;
	CalculateSwathAndDETConfiguration_params->DETBufferSizeInKByte = locals->DETBufferSizeInKByte;
	CalculateSwathAndDETConfiguration_params->DETBufferSizeY = locals->DETBufferSizeY;
	CalculateSwathAndDETConfiguration_params->DETBufferSizeC = locals->DETBufferSizeC;
	CalculateSwathAndDETConfiguration_params->UnboundedRequestEnabled = &locals->UnboundedRequestEnabled;
	CalculateSwathAndDETConfiguration_params->compbuf_reserved_space_64b = &locals->compbuf_reserved_space_64b;
	CalculateSwathAndDETConfiguration_params->compbuf_reserved_space_zs = &locals->compbuf_reserved_space_zs;
	CalculateSwathAndDETConfiguration_params->CompressedBufferSizeInkByte = &locals->CompressedBufferSizeInkByte;
	CalculateSwathAndDETConfiguration_params->ViewportSizeSupportPerSurface = &s->dummy_boolean_array[0][0];
	CalculateSwathAndDETConfiguration_params->ViewportSizeSupport = &s->dummy_boolean[0];

	// VBA_DELTA
	// Calculate DET size, swath height here. In VBA, they are calculated in mode check stage
	CalculateSwathAndDETConfiguration(&mode_lib->scratch,
		CalculateSwathAndDETConfiguration_params);

	// DCFCLK Deep Sleep
	CalculateDCFCLKDeepSleep(
			mode_lib->ms.num_active_planes,
			locals->BytePerPixelY,
			locals->BytePerPixelC,
			mode_lib->ms.cache_display_cfg.plane.VRatio,
			mode_lib->ms.cache_display_cfg.plane.VRatioChroma,
			locals->SwathWidthY,
			locals->SwathWidthC,
			mode_lib->ms.cache_display_cfg.hw.DPPPerSurface,
			mode_lib->ms.cache_display_cfg.plane.HRatio,
			mode_lib->ms.cache_display_cfg.plane.HRatioChroma,
			mode_lib->ms.cache_display_cfg.timing.PixelClock,
			locals->PSCL_THROUGHPUT,
			locals->PSCL_THROUGHPUT_CHROMA,
			locals->Dppclk,
			locals->ReadBandwidthSurfaceLuma,
			locals->ReadBandwidthSurfaceChroma,
			mode_lib->ms.soc.return_bus_width_bytes,

			/* Output */
			&locals->DCFCLKDeepSleep);

	// DSCCLK
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if ((mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] != k) || !mode_lib->ms.cache_display_cfg.hw.DSCEnabled[k]) {
			locals->DSCCLK_calculated[k] = 0.0;
		} else {
			if (mode_lib->ms.cache_display_cfg.output.OutputFormat[k] == dml_420)
				s->DSCFormatFactor = 2;
			else if (mode_lib->ms.cache_display_cfg.output.OutputFormat[k] == dml_444)
				s->DSCFormatFactor = 1;
			else if (mode_lib->ms.cache_display_cfg.output.OutputFormat[k] == dml_n422 || mode_lib->ms.cache_display_cfg.output.OutputEncoder[k] == dml_hdmifrl)
				s->DSCFormatFactor = 2;
			else
				s->DSCFormatFactor = 1;
			if (mode_lib->ms.cache_display_cfg.hw.ODMMode[k] == dml_odm_mode_combine_4to1)
				locals->DSCCLK_calculated[k] = mode_lib->ms.cache_display_cfg.output.PixelClockBackEnd[k] / 12 / s->DSCFormatFactor / (1 - mode_lib->ms.soc.dcn_downspread_percent / 100);
			else if (mode_lib->ms.cache_display_cfg.hw.ODMMode[k] == dml_odm_mode_combine_2to1)
				locals->DSCCLK_calculated[k] = mode_lib->ms.cache_display_cfg.output.PixelClockBackEnd[k] / 6 / s->DSCFormatFactor / (1 - mode_lib->ms.soc.dcn_downspread_percent / 100);
			else
				locals->DSCCLK_calculated[k] = mode_lib->ms.cache_display_cfg.output.PixelClockBackEnd[k] / 3 / s->DSCFormatFactor / (1 - mode_lib->ms.soc.dcn_downspread_percent / 100);
		}
	}

	// DSC Delay
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		locals->DSCDelay[k] = DSCDelayRequirement(mode_lib->ms.cache_display_cfg.hw.DSCEnabled[k],
												mode_lib->ms.cache_display_cfg.hw.ODMMode[k],
												mode_lib->ms.cache_display_cfg.output.DSCInputBitPerComponent[k],
												mode_lib->ms.cache_display_cfg.output.OutputBpp[k],
												mode_lib->ms.cache_display_cfg.timing.HActive[k],
												mode_lib->ms.cache_display_cfg.timing.HTotal[k],
												mode_lib->ms.cache_display_cfg.hw.NumberOfDSCSlices[k],
												mode_lib->ms.cache_display_cfg.output.OutputFormat[k],
												mode_lib->ms.cache_display_cfg.output.OutputEncoder[k],
												mode_lib->ms.cache_display_cfg.timing.PixelClock[k],
												mode_lib->ms.cache_display_cfg.output.PixelClockBackEnd[k]);
	}

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k)
		for (j = 0; j < mode_lib->ms.num_active_planes; ++j) // NumberOfSurfaces
			if (j != k && mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == j && mode_lib->ms.cache_display_cfg.hw.DSCEnabled[j])
				locals->DSCDelay[k] = locals->DSCDelay[j];

	// Prefetch
	CalculateSurfaceSizeInMall(
		mode_lib->ms.num_active_planes,
		mode_lib->ms.soc.mall_allocated_for_dcn_mbytes,
		mode_lib->ms.cache_display_cfg.plane.UseMALLForStaticScreen,
		mode_lib->ms.cache_display_cfg.surface.DCCEnable,
		mode_lib->ms.cache_display_cfg.plane.ViewportStationary,
		mode_lib->ms.cache_display_cfg.plane.ViewportXStart,
		mode_lib->ms.cache_display_cfg.plane.ViewportYStart,
		mode_lib->ms.cache_display_cfg.plane.ViewportXStartC,
		mode_lib->ms.cache_display_cfg.plane.ViewportYStartC,
		mode_lib->ms.cache_display_cfg.plane.ViewportWidth,
		mode_lib->ms.cache_display_cfg.plane.ViewportHeight,
		locals->BytePerPixelY,
		mode_lib->ms.cache_display_cfg.plane.ViewportWidthChroma,
		mode_lib->ms.cache_display_cfg.plane.ViewportHeightChroma,
		locals->BytePerPixelC,
		mode_lib->ms.cache_display_cfg.surface.SurfaceWidthY,
		mode_lib->ms.cache_display_cfg.surface.SurfaceWidthC,
		mode_lib->ms.cache_display_cfg.surface.SurfaceHeightY,
		mode_lib->ms.cache_display_cfg.surface.SurfaceHeightC,
		locals->BlockWidth256BytesY,
		locals->BlockWidth256BytesC,
		locals->BlockHeight256BytesY,
		locals->BlockHeight256BytesC,
		locals->BlockWidthY,
		locals->BlockWidthC,
		locals->BlockHeightY,
		locals->BlockHeightC,

		/* Output */
		locals->SurfaceSizeInTheMALL,
		&s->dummy_boolean[0]); /* dml_bool_t *ExceededMALLSize */

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		s->SurfaceParameters[k].PixelClock = mode_lib->ms.cache_display_cfg.timing.PixelClock[k];
		s->SurfaceParameters[k].DPPPerSurface = mode_lib->ms.cache_display_cfg.hw.DPPPerSurface[k];
		s->SurfaceParameters[k].SourceScan = mode_lib->ms.cache_display_cfg.plane.SourceScan[k];
		s->SurfaceParameters[k].ViewportHeight = mode_lib->ms.cache_display_cfg.plane.ViewportHeight[k];
		s->SurfaceParameters[k].ViewportHeightChroma = mode_lib->ms.cache_display_cfg.plane.ViewportHeightChroma[k];
		s->SurfaceParameters[k].BlockWidth256BytesY = locals->BlockWidth256BytesY[k];
		s->SurfaceParameters[k].BlockHeight256BytesY = locals->BlockHeight256BytesY[k];
		s->SurfaceParameters[k].BlockWidth256BytesC = locals->BlockWidth256BytesC[k];
		s->SurfaceParameters[k].BlockHeight256BytesC = locals->BlockHeight256BytesC[k];
		s->SurfaceParameters[k].BlockWidthY = locals->BlockWidthY[k];
		s->SurfaceParameters[k].BlockHeightY = locals->BlockHeightY[k];
		s->SurfaceParameters[k].BlockWidthC = locals->BlockWidthC[k];
		s->SurfaceParameters[k].BlockHeightC = locals->BlockHeightC[k];
		s->SurfaceParameters[k].InterlaceEnable = mode_lib->ms.cache_display_cfg.timing.Interlace[k];
		s->SurfaceParameters[k].HTotal = mode_lib->ms.cache_display_cfg.timing.HTotal[k];
		s->SurfaceParameters[k].DCCEnable = mode_lib->ms.cache_display_cfg.surface.DCCEnable[k];
		s->SurfaceParameters[k].SourcePixelFormat = mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k];
		s->SurfaceParameters[k].SurfaceTiling = mode_lib->ms.cache_display_cfg.surface.SurfaceTiling[k];
		s->SurfaceParameters[k].BytePerPixelY = locals->BytePerPixelY[k];
		s->SurfaceParameters[k].BytePerPixelC = locals->BytePerPixelC[k];
		s->SurfaceParameters[k].ProgressiveToInterlaceUnitInOPP = mode_lib->ms.ip.ptoi_supported;
		s->SurfaceParameters[k].VRatio = mode_lib->ms.cache_display_cfg.plane.VRatio[k];
		s->SurfaceParameters[k].VRatioChroma = mode_lib->ms.cache_display_cfg.plane.VRatioChroma[k];
		s->SurfaceParameters[k].VTaps = mode_lib->ms.cache_display_cfg.plane.VTaps[k];
		s->SurfaceParameters[k].VTapsChroma = mode_lib->ms.cache_display_cfg.plane.VTapsChroma[k];
		s->SurfaceParameters[k].PitchY = mode_lib->ms.cache_display_cfg.surface.PitchY[k];
		s->SurfaceParameters[k].DCCMetaPitchY = mode_lib->ms.cache_display_cfg.surface.DCCMetaPitchY[k];
		s->SurfaceParameters[k].PitchC = mode_lib->ms.cache_display_cfg.surface.PitchC[k];
		s->SurfaceParameters[k].DCCMetaPitchC = mode_lib->ms.cache_display_cfg.surface.DCCMetaPitchC[k];
		s->SurfaceParameters[k].ViewportStationary = mode_lib->ms.cache_display_cfg.plane.ViewportStationary[k];
		s->SurfaceParameters[k].ViewportXStart = mode_lib->ms.cache_display_cfg.plane.ViewportXStart[k];
		s->SurfaceParameters[k].ViewportYStart = mode_lib->ms.cache_display_cfg.plane.ViewportYStart[k];
		s->SurfaceParameters[k].ViewportXStartC = mode_lib->ms.cache_display_cfg.plane.ViewportXStartC[k];
		s->SurfaceParameters[k].ViewportYStartC = mode_lib->ms.cache_display_cfg.plane.ViewportYStartC[k];
		s->SurfaceParameters[k].FORCE_ONE_ROW_FOR_FRAME = mode_lib->ms.cache_display_cfg.plane.ForceOneRowForFrame[k];
		s->SurfaceParameters[k].SwathHeightY = locals->SwathHeightY[k];
		s->SurfaceParameters[k].SwathHeightC = locals->SwathHeightC[k];
	}

	CalculateVMRowAndSwath_params->NumberOfActiveSurfaces = mode_lib->ms.num_active_planes;
	CalculateVMRowAndSwath_params->myPipe = s->SurfaceParameters;
	CalculateVMRowAndSwath_params->SurfaceSizeInMALL = locals->SurfaceSizeInTheMALL;
	CalculateVMRowAndSwath_params->PTEBufferSizeInRequestsLuma = mode_lib->ms.ip.dpte_buffer_size_in_pte_reqs_luma;
	CalculateVMRowAndSwath_params->PTEBufferSizeInRequestsChroma = mode_lib->ms.ip.dpte_buffer_size_in_pte_reqs_chroma;
	CalculateVMRowAndSwath_params->DCCMetaBufferSizeBytes = mode_lib->ms.ip.dcc_meta_buffer_size_bytes;
	CalculateVMRowAndSwath_params->UseMALLForStaticScreen = mode_lib->ms.cache_display_cfg.plane.UseMALLForStaticScreen;
	CalculateVMRowAndSwath_params->UseMALLForPStateChange = mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange;
	CalculateVMRowAndSwath_params->MALLAllocatedForDCN = mode_lib->ms.soc.mall_allocated_for_dcn_mbytes;
	CalculateVMRowAndSwath_params->SwathWidthY = locals->SwathWidthY;
	CalculateVMRowAndSwath_params->SwathWidthC = locals->SwathWidthC;
	CalculateVMRowAndSwath_params->GPUVMEnable = mode_lib->ms.cache_display_cfg.plane.GPUVMEnable;
	CalculateVMRowAndSwath_params->HostVMEnable = mode_lib->ms.cache_display_cfg.plane.HostVMEnable;
	CalculateVMRowAndSwath_params->HostVMMaxNonCachedPageTableLevels = mode_lib->ms.cache_display_cfg.plane.HostVMMaxPageTableLevels;
	CalculateVMRowAndSwath_params->GPUVMMaxPageTableLevels = mode_lib->ms.cache_display_cfg.plane.GPUVMMaxPageTableLevels;
	CalculateVMRowAndSwath_params->GPUVMMinPageSizeKBytes = mode_lib->ms.cache_display_cfg.plane.GPUVMMinPageSizeKBytes;
	CalculateVMRowAndSwath_params->HostVMMinPageSize = mode_lib->ms.soc.hostvm_min_page_size_kbytes * 1024;
	CalculateVMRowAndSwath_params->PTEBufferModeOverrideEn = mode_lib->ms.cache_display_cfg.plane.PTEBufferModeOverrideEn;
	CalculateVMRowAndSwath_params->PTEBufferModeOverrideVal = mode_lib->ms.cache_display_cfg.plane.PTEBufferMode;
	CalculateVMRowAndSwath_params->PTEBufferSizeNotExceeded = s->dummy_boolean_array[0];
	CalculateVMRowAndSwath_params->DCCMetaBufferSizeNotExceeded = s->dummy_boolean_array[1];
	CalculateVMRowAndSwath_params->dpte_row_width_luma_ub = locals->dpte_row_width_luma_ub;
	CalculateVMRowAndSwath_params->dpte_row_width_chroma_ub = locals->dpte_row_width_chroma_ub;
	CalculateVMRowAndSwath_params->dpte_row_height_luma = locals->dpte_row_height;
	CalculateVMRowAndSwath_params->dpte_row_height_chroma = locals->dpte_row_height_chroma;
	CalculateVMRowAndSwath_params->dpte_row_height_linear_luma = locals->dpte_row_height_linear;
	CalculateVMRowAndSwath_params->dpte_row_height_linear_chroma = locals->dpte_row_height_linear_chroma;
	CalculateVMRowAndSwath_params->meta_req_width = locals->meta_req_width;
	CalculateVMRowAndSwath_params->meta_req_width_chroma = locals->meta_req_width_chroma;
	CalculateVMRowAndSwath_params->meta_req_height = locals->meta_req_height;
	CalculateVMRowAndSwath_params->meta_req_height_chroma = locals->meta_req_height_chroma;
	CalculateVMRowAndSwath_params->meta_row_width = locals->meta_row_width;
	CalculateVMRowAndSwath_params->meta_row_width_chroma = locals->meta_row_width_chroma;
	CalculateVMRowAndSwath_params->meta_row_height = locals->meta_row_height;
	CalculateVMRowAndSwath_params->meta_row_height_chroma = locals->meta_row_height_chroma;
	CalculateVMRowAndSwath_params->vm_group_bytes = locals->vm_group_bytes;
	CalculateVMRowAndSwath_params->dpte_group_bytes = locals->dpte_group_bytes;
	CalculateVMRowAndSwath_params->PixelPTEReqWidthY = locals->PixelPTEReqWidthY;
	CalculateVMRowAndSwath_params->PixelPTEReqHeightY = locals->PixelPTEReqHeightY;
	CalculateVMRowAndSwath_params->PTERequestSizeY = locals->PTERequestSizeY;
	CalculateVMRowAndSwath_params->PixelPTEReqWidthC = locals->PixelPTEReqWidthC;
	CalculateVMRowAndSwath_params->PixelPTEReqHeightC = locals->PixelPTEReqHeightC;
	CalculateVMRowAndSwath_params->PTERequestSizeC = locals->PTERequestSizeC;
	CalculateVMRowAndSwath_params->dpde0_bytes_per_frame_ub_l = locals->dpde0_bytes_per_frame_ub_l;
	CalculateVMRowAndSwath_params->meta_pte_bytes_per_frame_ub_l = locals->meta_pte_bytes_per_frame_ub_l;
	CalculateVMRowAndSwath_params->dpde0_bytes_per_frame_ub_c = locals->dpde0_bytes_per_frame_ub_c;
	CalculateVMRowAndSwath_params->meta_pte_bytes_per_frame_ub_c = locals->meta_pte_bytes_per_frame_ub_c;
	CalculateVMRowAndSwath_params->PrefetchSourceLinesY = locals->PrefetchSourceLinesY;
	CalculateVMRowAndSwath_params->PrefetchSourceLinesC = locals->PrefetchSourceLinesC;
	CalculateVMRowAndSwath_params->VInitPreFillY = locals->VInitPreFillY;
	CalculateVMRowAndSwath_params->VInitPreFillC = locals->VInitPreFillC;
	CalculateVMRowAndSwath_params->MaxNumSwathY = locals->MaxNumSwathY;
	CalculateVMRowAndSwath_params->MaxNumSwathC = locals->MaxNumSwathC;
	CalculateVMRowAndSwath_params->meta_row_bw = locals->meta_row_bw;
	CalculateVMRowAndSwath_params->dpte_row_bw = locals->dpte_row_bw;
	CalculateVMRowAndSwath_params->PixelPTEBytesPerRow = locals->PixelPTEBytesPerRow;
	CalculateVMRowAndSwath_params->PDEAndMetaPTEBytesFrame = locals->PDEAndMetaPTEBytesFrame;
	CalculateVMRowAndSwath_params->MetaRowByte = locals->MetaRowByte;
	CalculateVMRowAndSwath_params->use_one_row_for_frame = locals->use_one_row_for_frame;
	CalculateVMRowAndSwath_params->use_one_row_for_frame_flip = locals->use_one_row_for_frame_flip;
	CalculateVMRowAndSwath_params->UsesMALLForStaticScreen = locals->UsesMALLForStaticScreen;
	CalculateVMRowAndSwath_params->PTE_BUFFER_MODE = locals->PTE_BUFFER_MODE;
	CalculateVMRowAndSwath_params->BIGK_FRAGMENT_SIZE = locals->BIGK_FRAGMENT_SIZE;

	CalculateVMRowAndSwath(&mode_lib->scratch,
	CalculateVMRowAndSwath_params);

	s->ReorderBytes = (dml_uint_t)(mode_lib->ms.soc.num_chans * dml_max3(
			mode_lib->ms.soc.urgent_out_of_order_return_per_channel_pixel_only_bytes,
			mode_lib->ms.soc.urgent_out_of_order_return_per_channel_pixel_and_vm_bytes,
			mode_lib->ms.soc.urgent_out_of_order_return_per_channel_vm_only_bytes));

	s->VMDataOnlyReturnBW = dml_get_return_bw_mbps_vm_only(&mode_lib->ms.soc,
																	mode_lib->ms.state.use_ideal_dram_bw_strobe,
																	mode_lib->ms.cache_display_cfg.plane.HostVMEnable,
																	locals->Dcfclk,
																	mode_lib->ms.FabricClock,
																	mode_lib->ms.DRAMSpeed);

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: locals->Dcfclk = %f\n", __func__, locals->Dcfclk);
	dml_print("DML::%s: mode_lib->ms.soc.return_bus_width_bytes = %u\n", __func__, mode_lib->ms.soc.return_bus_width_bytes);
	dml_print("DML::%s: mode_lib->ms.FabricClock = %f\n", __func__, mode_lib->ms.FabricClock);
	dml_print("DML::%s: mode_lib->ms.soc.fabric_datapath_to_dcn_data_return_bytes = %u\n", __func__, mode_lib->ms.soc.fabric_datapath_to_dcn_data_return_bytes);
	dml_print("DML::%s: mode_lib->ms.soc.pct_ideal_sdp_bw_after_urgent = %f\n", __func__, mode_lib->ms.soc.pct_ideal_sdp_bw_after_urgent);
	dml_print("DML::%s: mode_lib->ms.DRAMSpeed = %f\n", __func__, mode_lib->ms.DRAMSpeed);
	dml_print("DML::%s: mode_lib->ms.soc.num_chans = %u\n", __func__, mode_lib->ms.soc.num_chans);
	dml_print("DML::%s: mode_lib->ms.soc.dram_channel_width_bytes = %u\n", __func__, mode_lib->ms.soc.dram_channel_width_bytes);
	dml_print("DML::%s: mode_lib->ms.state_idx = %u\n", __func__, mode_lib->ms.state_idx);
	dml_print("DML::%s: mode_lib->ms.max_state_idx = %u\n", __func__, mode_lib->ms.max_state_idx);
	dml_print("DML::%s: mode_lib->ms.state.use_ideal_dram_bw_strobe = %u\n", __func__, mode_lib->ms.state.use_ideal_dram_bw_strobe);
	dml_print("DML::%s: VMDataOnlyReturnBW = %f\n", __func__, s->VMDataOnlyReturnBW);
	dml_print("DML::%s: ReturnBW = %f\n",  __func__, mode_lib->ms.ReturnBW);
#endif

	s->HostVMInefficiencyFactor = 1.0;
	if (mode_lib->ms.cache_display_cfg.plane.GPUVMEnable && mode_lib->ms.cache_display_cfg.plane.HostVMEnable)
		s->HostVMInefficiencyFactor = mode_lib->ms.ReturnBW / s->VMDataOnlyReturnBW;

	s->TotalDCCActiveDPP = 0;
	s->TotalActiveDPP = 0;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		s->TotalActiveDPP = s->TotalActiveDPP + mode_lib->ms.cache_display_cfg.hw.DPPPerSurface[k];
		if (mode_lib->ms.cache_display_cfg.surface.DCCEnable[k])
			s->TotalDCCActiveDPP = s->TotalDCCActiveDPP + mode_lib->ms.cache_display_cfg.hw.DPPPerSurface[k];
	}

	locals->UrgentExtraLatency = CalculateExtraLatency(
			mode_lib->ms.soc.round_trip_ping_latency_dcfclk_cycles,
			s->ReorderBytes,
			locals->Dcfclk,
			s->TotalActiveDPP,
			mode_lib->ms.ip.pixel_chunk_size_kbytes,
			s->TotalDCCActiveDPP,
			mode_lib->ms.ip.meta_chunk_size_kbytes,
			mode_lib->ms.ReturnBW,
			mode_lib->ms.cache_display_cfg.plane.GPUVMEnable,
			mode_lib->ms.cache_display_cfg.plane.HostVMEnable,
			mode_lib->ms.num_active_planes,
			mode_lib->ms.cache_display_cfg.hw.DPPPerSurface,
			locals->dpte_group_bytes,
			s->HostVMInefficiencyFactor,
			mode_lib->ms.soc.hostvm_min_page_size_kbytes * 1024,
			mode_lib->ms.cache_display_cfg.plane.HostVMMaxPageTableLevels);

	locals->TCalc = 24.0 / locals->DCFCLKDeepSleep;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == k) {
			if (mode_lib->ms.cache_display_cfg.writeback.WritebackEnable[k] == true) {
				locals->WritebackDelay[k] =
						mode_lib->ms.state.writeback_latency_us
								+ CalculateWriteBackDelay(
										mode_lib->ms.cache_display_cfg.writeback.WritebackPixelFormat[k],
										mode_lib->ms.cache_display_cfg.writeback.WritebackHRatio[k],
										mode_lib->ms.cache_display_cfg.writeback.WritebackVRatio[k],
										mode_lib->ms.cache_display_cfg.writeback.WritebackVTaps[k],
										mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationWidth[k],
										mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationHeight[k],
										mode_lib->ms.cache_display_cfg.writeback.WritebackSourceHeight[k],
										mode_lib->ms.cache_display_cfg.timing.HTotal[k]) / locals->Dispclk;
			} else
				locals->WritebackDelay[k] = 0;
			for (j = 0; j < mode_lib->ms.num_active_planes; ++j) {
				if (mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[j] == k
						&& mode_lib->ms.cache_display_cfg.writeback.WritebackEnable[j] == true) {
					locals->WritebackDelay[k] =
							dml_max(
									locals->WritebackDelay[k],
									mode_lib->ms.state.writeback_latency_us
											+ CalculateWriteBackDelay(
													mode_lib->ms.cache_display_cfg.writeback.WritebackPixelFormat[j],
													mode_lib->ms.cache_display_cfg.writeback.WritebackHRatio[j],
													mode_lib->ms.cache_display_cfg.writeback.WritebackVRatio[j],
													mode_lib->ms.cache_display_cfg.writeback.WritebackVTaps[j],
													mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationWidth[j],
													mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationHeight[j],
													mode_lib->ms.cache_display_cfg.writeback.WritebackSourceHeight[j],
													mode_lib->ms.cache_display_cfg.timing.HTotal[k]) / locals->Dispclk);
				}
			}
		}
	}

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k)
		for (j = 0; j < mode_lib->ms.num_active_planes; ++j)
			if (mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming[k] == j)
				locals->WritebackDelay[k] = locals->WritebackDelay[j];

	locals->UrgentLatency = CalculateUrgentLatency(mode_lib->ms.state.urgent_latency_pixel_data_only_us,
												mode_lib->ms.state.urgent_latency_pixel_mixed_with_vm_data_us,
												mode_lib->ms.state.urgent_latency_vm_data_only_us,
												mode_lib->ms.soc.do_urgent_latency_adjustment,
												mode_lib->ms.state.urgent_latency_adjustment_fabric_clock_component_us,
												mode_lib->ms.state.urgent_latency_adjustment_fabric_clock_reference_mhz,
												mode_lib->ms.FabricClock);

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		CalculateUrgentBurstFactor(mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange[k],
									locals->swath_width_luma_ub[k],
									locals->swath_width_chroma_ub[k],
									locals->SwathHeightY[k],
									locals->SwathHeightC[k],
									mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k],
									locals->UrgentLatency,
									mode_lib->ms.ip.cursor_buffer_size,
									mode_lib->ms.cache_display_cfg.plane.CursorWidth[k],
									mode_lib->ms.cache_display_cfg.plane.CursorBPP[k],
									mode_lib->ms.cache_display_cfg.plane.VRatio[k],
									mode_lib->ms.cache_display_cfg.plane.VRatioChroma[k],
									locals->BytePerPixelDETY[k],
									locals->BytePerPixelDETC[k],
									locals->DETBufferSizeY[k],
									locals->DETBufferSizeC[k],

									/* output */
									&locals->UrgBurstFactorCursor[k],
									&locals->UrgBurstFactorLuma[k],
									&locals->UrgBurstFactorChroma[k],
									&locals->NoUrgentLatencyHiding[k]);

		locals->cursor_bw[k] = mode_lib->ms.cache_display_cfg.plane.NumberOfCursors[k] * mode_lib->ms.cache_display_cfg.plane.CursorWidth[k] * mode_lib->ms.cache_display_cfg.plane.CursorBPP[k] / 8.0 /
								((dml_float_t) mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k]) * mode_lib->ms.cache_display_cfg.plane.VRatio[k];
	}

	s->VStartupLines = __DML_VBA_MIN_VSTARTUP__;
	s->MaxVStartupAllPlanes = 0;

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		s->MaxVStartupLines[k] = CalculateMaxVStartup(k,
													mode_lib->ms.ip.ptoi_supported,
													mode_lib->ms.ip.vblank_nom_default_us,
													&mode_lib->ms.cache_display_cfg.timing,
													locals->WritebackDelay[k]);

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u MaxVStartupLines   = %u\n", __func__, k, s->MaxVStartupLines[k]);
		dml_print("DML::%s: k=%u WritebackDelay     = %f\n", __func__, k, locals->WritebackDelay[k]);
#endif
	}

	for (k = 0; k < mode_lib->ms.num_active_planes; ++k)
		s->MaxVStartupAllPlanes = (dml_uint_t)(dml_max(s->MaxVStartupAllPlanes, s->MaxVStartupLines[k]));

	s->ImmediateFlipRequirementFinal = false;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		s->ImmediateFlipRequirementFinal = s->ImmediateFlipRequirementFinal || (mode_lib->ms.policy.ImmediateFlipRequirement[k] == dml_immediate_flip_required);
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: ImmediateFlipRequirementFinal = %u\n", __func__, s->ImmediateFlipRequirementFinal);
#endif

	// The prefetch scheduling should only be calculated once as per AllowForPStateChangeOrStutterInVBlank requirement
	// If the AllowForPStateChangeOrStutterInVBlank requirement is not strict (i.e. only try those power saving feature
	// if possible, then will try to program for the best power saving features in order of diffculty (dram, fclk, stutter)
	s->iteration = 0;
	s->MaxTotalRDBandwidth = 0;
	s->AllPrefetchModeTested = false;
	for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
		CalculatePrefetchMode(mode_lib->ms.policy.AllowForPStateChangeOrStutterInVBlank[k], &s->MinPrefetchMode[k], &s->MaxPrefetchMode[k]);
		s->NextPrefetchMode[k] = s->MinPrefetchMode[k];
	}

	do {
		s->MaxTotalRDBandwidthNoUrgentBurst = 0.0;
		s->DestinationLineTimesForPrefetchLessThan2 = false;
		s->VRatioPrefetchMoreThanMax = false;

		dml_print("DML::%s: Start one iteration: VStartupLines = %u\n", __func__, s->VStartupLines);

		s->AllPrefetchModeTested = true;
		s->MaxTotalRDBandwidth = 0;
		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			locals->PrefetchMode[k] = s->NextPrefetchMode[k];
			TWait = CalculateTWait(
					locals->PrefetchMode[k],
					mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange[k],
					mode_lib->ms.policy.SynchronizeDRRDisplaysForUCLKPStateChangeFinal,
					mode_lib->ms.cache_display_cfg.timing.DRRDisplay[k],
					mode_lib->ms.state.dram_clock_change_latency_us,
					mode_lib->ms.state.fclk_change_latency_us,
					locals->UrgentLatency,
					mode_lib->ms.state.sr_enter_plus_exit_time_us);

			myPipe = &s->myPipe;
			myPipe->Dppclk = locals->Dppclk[k];
			myPipe->Dispclk = locals->Dispclk;
			myPipe->PixelClock = mode_lib->ms.cache_display_cfg.timing.PixelClock[k];
			myPipe->DCFClkDeepSleep = locals->DCFCLKDeepSleep;
			myPipe->DPPPerSurface = mode_lib->ms.cache_display_cfg.hw.DPPPerSurface[k];
			myPipe->ScalerEnabled = mode_lib->ms.cache_display_cfg.plane.ScalerEnabled[k];
			myPipe->SourceScan = mode_lib->ms.cache_display_cfg.plane.SourceScan[k];
			myPipe->BlockWidth256BytesY = locals->BlockWidth256BytesY[k];
			myPipe->BlockHeight256BytesY = locals->BlockHeight256BytesY[k];
			myPipe->BlockWidth256BytesC = locals->BlockWidth256BytesC[k];
			myPipe->BlockHeight256BytesC = locals->BlockHeight256BytesC[k];
			myPipe->InterlaceEnable = mode_lib->ms.cache_display_cfg.timing.Interlace[k];
			myPipe->NumberOfCursors = mode_lib->ms.cache_display_cfg.plane.NumberOfCursors[k];
			myPipe->VBlank = mode_lib->ms.cache_display_cfg.timing.VTotal[k] - mode_lib->ms.cache_display_cfg.timing.VActive[k];
			myPipe->HTotal = mode_lib->ms.cache_display_cfg.timing.HTotal[k];
			myPipe->HActive = mode_lib->ms.cache_display_cfg.timing.HActive[k];
			myPipe->DCCEnable = mode_lib->ms.cache_display_cfg.surface.DCCEnable[k];
			myPipe->ODMMode = mode_lib->ms.cache_display_cfg.hw.ODMMode[k];
			myPipe->SourcePixelFormat = mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k];
			myPipe->BytePerPixelY = locals->BytePerPixelY[k];
			myPipe->BytePerPixelC = locals->BytePerPixelC[k];
			myPipe->ProgressiveToInterlaceUnitInOPP = mode_lib->ms.ip.ptoi_supported;

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: Calling CalculatePrefetchSchedule for k=%u\n", __func__, k);
			dml_print("DML::%s: AllowForPStateChangeOrStutterInVBlank = %u\n", __func__, mode_lib->ms.policy.AllowForPStateChangeOrStutterInVBlank[k]);
			dml_print("DML::%s: PrefetchMode[k] = %u (Min=%u Max=%u)\n", __func__, locals->PrefetchMode[k], s->MinPrefetchMode[k], s->MaxPrefetchMode[k]);
#endif

			CalculatePrefetchSchedule_params->EnhancedPrefetchScheduleAccelerationFinal = mode_lib->ms.policy.EnhancedPrefetchScheduleAccelerationFinal;
			CalculatePrefetchSchedule_params->HostVMInefficiencyFactor = s->HostVMInefficiencyFactor;
			CalculatePrefetchSchedule_params->myPipe = myPipe;
			CalculatePrefetchSchedule_params->DSCDelay = locals->DSCDelay[k];
			CalculatePrefetchSchedule_params->DPPCLKDelaySubtotalPlusCNVCFormater = mode_lib->ms.ip.dppclk_delay_subtotal + mode_lib->ms.ip.dppclk_delay_cnvc_formatter;
			CalculatePrefetchSchedule_params->DPPCLKDelaySCL = mode_lib->ms.ip.dppclk_delay_scl;
			CalculatePrefetchSchedule_params->DPPCLKDelaySCLLBOnly = mode_lib->ms.ip.dppclk_delay_scl_lb_only;
			CalculatePrefetchSchedule_params->DPPCLKDelayCNVCCursor = mode_lib->ms.ip.dppclk_delay_cnvc_cursor;
			CalculatePrefetchSchedule_params->DISPCLKDelaySubtotal = mode_lib->ms.ip.dispclk_delay_subtotal;
			CalculatePrefetchSchedule_params->DPP_RECOUT_WIDTH = (dml_uint_t)(locals->SwathWidthY[k] / mode_lib->ms.cache_display_cfg.plane.HRatio[k]);
			CalculatePrefetchSchedule_params->OutputFormat = mode_lib->ms.cache_display_cfg.output.OutputFormat[k];
			CalculatePrefetchSchedule_params->MaxInterDCNTileRepeaters = mode_lib->ms.ip.max_inter_dcn_tile_repeaters;
			CalculatePrefetchSchedule_params->VStartup = (dml_uint_t)(dml_min(s->VStartupLines, s->MaxVStartupLines[k]));
			CalculatePrefetchSchedule_params->MaxVStartup = s->MaxVStartupLines[k];
			CalculatePrefetchSchedule_params->GPUVMPageTableLevels = mode_lib->ms.cache_display_cfg.plane.GPUVMMaxPageTableLevels;
			CalculatePrefetchSchedule_params->GPUVMEnable = mode_lib->ms.cache_display_cfg.plane.GPUVMEnable;
			CalculatePrefetchSchedule_params->HostVMEnable = mode_lib->ms.cache_display_cfg.plane.HostVMEnable;
			CalculatePrefetchSchedule_params->HostVMMaxNonCachedPageTableLevels = mode_lib->ms.cache_display_cfg.plane.HostVMMaxPageTableLevels;
			CalculatePrefetchSchedule_params->HostVMMinPageSize = mode_lib->ms.soc.hostvm_min_page_size_kbytes * 1024;
			CalculatePrefetchSchedule_params->DynamicMetadataEnable = mode_lib->ms.cache_display_cfg.plane.DynamicMetadataEnable[k];
			CalculatePrefetchSchedule_params->DynamicMetadataVMEnabled = mode_lib->ms.ip.dynamic_metadata_vm_enabled;
			CalculatePrefetchSchedule_params->DynamicMetadataLinesBeforeActiveRequired = mode_lib->ms.cache_display_cfg.plane.DynamicMetadataLinesBeforeActiveRequired[k];
			CalculatePrefetchSchedule_params->DynamicMetadataTransmittedBytes = mode_lib->ms.cache_display_cfg.plane.DynamicMetadataTransmittedBytes[k];
			CalculatePrefetchSchedule_params->UrgentLatency = locals->UrgentLatency;
			CalculatePrefetchSchedule_params->UrgentExtraLatency = locals->UrgentExtraLatency;
			CalculatePrefetchSchedule_params->TCalc = locals->TCalc;
			CalculatePrefetchSchedule_params->PDEAndMetaPTEBytesFrame = locals->PDEAndMetaPTEBytesFrame[k];
			CalculatePrefetchSchedule_params->MetaRowByte = locals->MetaRowByte[k];
			CalculatePrefetchSchedule_params->PixelPTEBytesPerRow = locals->PixelPTEBytesPerRow[k];
			CalculatePrefetchSchedule_params->PrefetchSourceLinesY = locals->PrefetchSourceLinesY[k];
			CalculatePrefetchSchedule_params->VInitPreFillY = locals->VInitPreFillY[k];
			CalculatePrefetchSchedule_params->MaxNumSwathY = locals->MaxNumSwathY[k];
			CalculatePrefetchSchedule_params->PrefetchSourceLinesC = locals->PrefetchSourceLinesC[k];
			CalculatePrefetchSchedule_params->VInitPreFillC = locals->VInitPreFillC[k];
			CalculatePrefetchSchedule_params->MaxNumSwathC = locals->MaxNumSwathC[k];
			CalculatePrefetchSchedule_params->swath_width_luma_ub = locals->swath_width_luma_ub[k];
			CalculatePrefetchSchedule_params->swath_width_chroma_ub = locals->swath_width_chroma_ub[k];
			CalculatePrefetchSchedule_params->SwathHeightY = locals->SwathHeightY[k];
			CalculatePrefetchSchedule_params->SwathHeightC = locals->SwathHeightC[k];
			CalculatePrefetchSchedule_params->TWait = TWait;
			CalculatePrefetchSchedule_params->DSTXAfterScaler = &locals->DSTXAfterScaler[k];
			CalculatePrefetchSchedule_params->DSTYAfterScaler = &locals->DSTYAfterScaler[k];
			CalculatePrefetchSchedule_params->DestinationLinesForPrefetch = &locals->DestinationLinesForPrefetch[k];
			CalculatePrefetchSchedule_params->DestinationLinesToRequestVMInVBlank = &locals->DestinationLinesToRequestVMInVBlank[k];
			CalculatePrefetchSchedule_params->DestinationLinesToRequestRowInVBlank = &locals->DestinationLinesToRequestRowInVBlank[k];
			CalculatePrefetchSchedule_params->VRatioPrefetchY = &locals->VRatioPrefetchY[k];
			CalculatePrefetchSchedule_params->VRatioPrefetchC = &locals->VRatioPrefetchC[k];
			CalculatePrefetchSchedule_params->RequiredPrefetchPixDataBWLuma = &locals->RequiredPrefetchPixDataBWLuma[k];
			CalculatePrefetchSchedule_params->RequiredPrefetchPixDataBWChroma = &locals->RequiredPrefetchPixDataBWChroma[k];
			CalculatePrefetchSchedule_params->NotEnoughTimeForDynamicMetadata = &locals->NotEnoughTimeForDynamicMetadata[k];
			CalculatePrefetchSchedule_params->Tno_bw = &locals->Tno_bw[k];
			CalculatePrefetchSchedule_params->prefetch_vmrow_bw = &locals->prefetch_vmrow_bw[k];
			CalculatePrefetchSchedule_params->Tdmdl_vm = &locals->Tdmdl_vm[k];
			CalculatePrefetchSchedule_params->Tdmdl = &locals->Tdmdl[k];
			CalculatePrefetchSchedule_params->TSetup = &locals->TSetup[k];
			CalculatePrefetchSchedule_params->VUpdateOffsetPix = &locals->VUpdateOffsetPix[k];
			CalculatePrefetchSchedule_params->VUpdateWidthPix = &locals->VUpdateWidthPix[k];
			CalculatePrefetchSchedule_params->VReadyOffsetPix = &locals->VReadyOffsetPix[k];

			locals->NoTimeToPrefetch[k] =
				CalculatePrefetchSchedule(&mode_lib->scratch,
					CalculatePrefetchSchedule_params);

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%0u NoTimeToPrefetch=%0d\n", __func__, k, locals->NoTimeToPrefetch[k]);
#endif
			locals->VStartup[k] = (dml_uint_t)(dml_min(s->VStartupLines, s->MaxVStartupLines[k]));
			locals->VStartupMin[k] = locals->VStartup[k];
		}

		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			CalculateUrgentBurstFactor(
				mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange[k],
				locals->swath_width_luma_ub[k],
				locals->swath_width_chroma_ub[k],
				locals->SwathHeightY[k],
				locals->SwathHeightC[k],
				mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k],
				locals->UrgentLatency,
				mode_lib->ms.ip.cursor_buffer_size,
				mode_lib->ms.cache_display_cfg.plane.CursorWidth[k],
				mode_lib->ms.cache_display_cfg.plane.CursorBPP[k],
				locals->VRatioPrefetchY[k],
				locals->VRatioPrefetchC[k],
				locals->BytePerPixelDETY[k],
				locals->BytePerPixelDETC[k],
				locals->DETBufferSizeY[k],
				locals->DETBufferSizeC[k],
				/* Output */
				&locals->UrgBurstFactorCursorPre[k],
				&locals->UrgBurstFactorLumaPre[k],
				&locals->UrgBurstFactorChromaPre[k],
				&locals->NoUrgentLatencyHidingPre[k]);

			locals->cursor_bw_pre[k] = mode_lib->ms.cache_display_cfg.plane.NumberOfCursors[k] * mode_lib->ms.cache_display_cfg.plane.CursorWidth[k] * mode_lib->ms.cache_display_cfg.plane.CursorBPP[k] / 8.0 / (mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k]) * locals->VRatioPrefetchY[k];

#ifdef __DML_VBA_DEBUG__
			dml_print("DML::%s: k=%0u DPPPerSurface=%u\n", __func__, k, mode_lib->ms.cache_display_cfg.hw.DPPPerSurface[k]);
			dml_print("DML::%s: k=%0u UrgBurstFactorLuma=%f\n", __func__, k, locals->UrgBurstFactorLuma[k]);
			dml_print("DML::%s: k=%0u UrgBurstFactorChroma=%f\n", __func__, k, locals->UrgBurstFactorChroma[k]);
			dml_print("DML::%s: k=%0u UrgBurstFactorLumaPre=%f\n", __func__, k, locals->UrgBurstFactorLumaPre[k]);
			dml_print("DML::%s: k=%0u UrgBurstFactorChromaPre=%f\n", __func__, k, locals->UrgBurstFactorChromaPre[k]);

			dml_print("DML::%s: k=%0u VRatioPrefetchY=%f\n", __func__, k, locals->VRatioPrefetchY[k]);
			dml_print("DML::%s: k=%0u VRatioY=%f\n", __func__, k, mode_lib->ms.cache_display_cfg.plane.VRatio[k]);

			dml_print("DML::%s: k=%0u prefetch_vmrow_bw=%f\n", __func__, k, locals->prefetch_vmrow_bw[k]);
			dml_print("DML::%s: k=%0u ReadBandwidthSurfaceLuma=%f\n", __func__, k, locals->ReadBandwidthSurfaceLuma[k]);
			dml_print("DML::%s: k=%0u ReadBandwidthSurfaceChroma=%f\n", __func__, k, locals->ReadBandwidthSurfaceChroma[k]);
			dml_print("DML::%s: k=%0u cursor_bw=%f\n", __func__, k, locals->cursor_bw[k]);
			dml_print("DML::%s: k=%0u meta_row_bw=%f\n", __func__, k, locals->meta_row_bw[k]);
			dml_print("DML::%s: k=%0u dpte_row_bw=%f\n", __func__, k, locals->dpte_row_bw[k]);
			dml_print("DML::%s: k=%0u RequiredPrefetchPixDataBWLuma=%f\n", __func__, k, locals->RequiredPrefetchPixDataBWLuma[k]);
			dml_print("DML::%s: k=%0u RequiredPrefetchPixDataBWChroma=%f\n", __func__, k, locals->RequiredPrefetchPixDataBWChroma[k]);
			dml_print("DML::%s: k=%0u cursor_bw_pre=%f\n", __func__, k, locals->cursor_bw_pre[k]);
			dml_print("DML::%s: k=%0u MaxTotalRDBandwidthNoUrgentBurst=%f\n", __func__, k, s->MaxTotalRDBandwidthNoUrgentBurst);
#endif
			if (locals->DestinationLinesForPrefetch[k] < 2)
				s->DestinationLineTimesForPrefetchLessThan2 = true;

			if (locals->VRatioPrefetchY[k] > __DML_MAX_VRATIO_PRE_ENHANCE_PREFETCH_ACC__ ||
				locals->VRatioPrefetchC[k] > __DML_MAX_VRATIO_PRE_ENHANCE_PREFETCH_ACC__ ||
				((s->VStartupLines < s->MaxVStartupLines[k] || mode_lib->ms.policy.EnhancedPrefetchScheduleAccelerationFinal == 0) &&
					(locals->VRatioPrefetchY[k] > __DML_MAX_VRATIO_PRE__ || locals->VRatioPrefetchC[k] > __DML_MAX_VRATIO_PRE__)))
				s->VRatioPrefetchMoreThanMax = true;

			//dml_bool_t DestinationLinesToRequestVMInVBlankEqualOrMoreThan32 = false;
			//dml_bool_t DestinationLinesToRequestRowInVBlankEqualOrMoreThan16 = false;
			//if (locals->DestinationLinesToRequestVMInVBlank[k] >= 32) {
			//    DestinationLinesToRequestVMInVBlankEqualOrMoreThan32 = true;
			//}

			//if (locals->DestinationLinesToRequestRowInVBlank[k] >= 16) {
			//    DestinationLinesToRequestRowInVBlankEqualOrMoreThan16 = true;
			//}
		}

		locals->FractionOfUrgentBandwidth = s->MaxTotalRDBandwidthNoUrgentBurst / mode_lib->ms.ReturnBW;

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: MaxTotalRDBandwidthNoUrgentBurst=%f \n", __func__, s->MaxTotalRDBandwidthNoUrgentBurst);
		dml_print("DML::%s: ReturnBW=%f \n", __func__, mode_lib->ms.ReturnBW);
		dml_print("DML::%s: FractionOfUrgentBandwidth=%f \n", __func__, locals->FractionOfUrgentBandwidth);
#endif

		CalculatePrefetchBandwithSupport(
			mode_lib->ms.num_active_planes,
			mode_lib->ms.ReturnBW,
			mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange,
			locals->NoUrgentLatencyHidingPre,
			locals->ReadBandwidthSurfaceLuma,
			locals->ReadBandwidthSurfaceChroma,
			locals->RequiredPrefetchPixDataBWLuma,
			locals->RequiredPrefetchPixDataBWChroma,
			locals->cursor_bw,
			locals->meta_row_bw,
			locals->dpte_row_bw,
			locals->cursor_bw_pre,
			locals->prefetch_vmrow_bw,
			mode_lib->ms.cache_display_cfg.hw.DPPPerSurface,
			locals->UrgBurstFactorLuma,
			locals->UrgBurstFactorChroma,
			locals->UrgBurstFactorCursor,
			locals->UrgBurstFactorLumaPre,
			locals->UrgBurstFactorChromaPre,
			locals->UrgBurstFactorCursorPre,

			/* output */
			&s->MaxTotalRDBandwidth, // dml_float_t *PrefetchBandwidth
			&s->MaxTotalRDBandwidthNotIncludingMALLPrefetch, // dml_float_t *PrefetchBandwidthNotIncludingMALLPrefetch
			&s->dummy_single[0], // dml_float_t *FractionOfUrgentBandwidth
			&locals->PrefetchModeSupported);

		for (k = 0; k < mode_lib->ms.num_active_planes; ++k)
			s->dummy_unit_vector[k] = 1.0;

		CalculatePrefetchBandwithSupport(mode_lib->ms.num_active_planes,
			mode_lib->ms.ReturnBW,
			mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange,
			locals->NoUrgentLatencyHidingPre,
			locals->ReadBandwidthSurfaceLuma,
			locals->ReadBandwidthSurfaceChroma,
			locals->RequiredPrefetchPixDataBWLuma,
			locals->RequiredPrefetchPixDataBWChroma,
			locals->cursor_bw,
			locals->meta_row_bw,
			locals->dpte_row_bw,
			locals->cursor_bw_pre,
			locals->prefetch_vmrow_bw,
			mode_lib->ms.cache_display_cfg.hw.DPPPerSurface,
			s->dummy_unit_vector,
			s->dummy_unit_vector,
			s->dummy_unit_vector,
			s->dummy_unit_vector,
			s->dummy_unit_vector,
			s->dummy_unit_vector,

			/* output */
			&s->NonUrgentMaxTotalRDBandwidth, // dml_float_t *PrefetchBandwidth
			&s->NonUrgentMaxTotalRDBandwidthNotIncludingMALLPrefetch, // dml_float_t *PrefetchBandwidthNotIncludingMALLPrefetch
			&locals->FractionOfUrgentBandwidth,
			&s->dummy_boolean[0]); // dml_bool_t *PrefetchBandwidthSupport

		if (s->VRatioPrefetchMoreThanMax != false || s->DestinationLineTimesForPrefetchLessThan2 != false) {
			dml_print("DML::%s: VRatioPrefetchMoreThanMax                   = %u\n", __func__, s->VRatioPrefetchMoreThanMax);
			dml_print("DML::%s: DestinationLineTimesForPrefetchLessThan2    = %u\n", __func__, s->DestinationLineTimesForPrefetchLessThan2);
			locals->PrefetchModeSupported = false;
		}

		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			if (locals->NoTimeToPrefetch[k] == true || locals->NotEnoughTimeForDynamicMetadata[k]) {
				dml_print("DML::%s: k=%u, NoTimeToPrefetch = %0d\n", __func__, k, locals->NoTimeToPrefetch[k]);
				dml_print("DML::%s: k=%u, NotEnoughTimeForDynamicMetadata=%u\n", __func__, k, locals->NotEnoughTimeForDynamicMetadata[k]);
				locals->PrefetchModeSupported = false;
			}
		}

		if (locals->PrefetchModeSupported == true && mode_lib->ms.support.ImmediateFlipSupport == true) {
			locals->BandwidthAvailableForImmediateFlip = CalculateBandwidthAvailableForImmediateFlip(
																	mode_lib->ms.num_active_planes,
																	mode_lib->ms.ReturnBW,
																	locals->ReadBandwidthSurfaceLuma,
																	locals->ReadBandwidthSurfaceChroma,
																	locals->RequiredPrefetchPixDataBWLuma,
																	locals->RequiredPrefetchPixDataBWChroma,
																	locals->cursor_bw,
																	locals->cursor_bw_pre,
																	mode_lib->ms.cache_display_cfg.hw.DPPPerSurface,
																	locals->UrgBurstFactorLuma,
																	locals->UrgBurstFactorChroma,
																	locals->UrgBurstFactorCursor,
																	locals->UrgBurstFactorLumaPre,
																	locals->UrgBurstFactorChromaPre,
																	locals->UrgBurstFactorCursorPre);

			locals->TotImmediateFlipBytes = 0;
			for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
				if (mode_lib->ms.policy.ImmediateFlipRequirement[k] != dml_immediate_flip_not_required) {
					locals->TotImmediateFlipBytes = locals->TotImmediateFlipBytes + mode_lib->ms.cache_display_cfg.hw.DPPPerSurface[k] * (locals->PDEAndMetaPTEBytesFrame[k] + locals->MetaRowByte[k]);
					if (locals->use_one_row_for_frame_flip[k]) {
						locals->TotImmediateFlipBytes = locals->TotImmediateFlipBytes + mode_lib->ms.cache_display_cfg.hw.DPPPerSurface[k] * (2 * locals->PixelPTEBytesPerRow[k]);
					} else {
						locals->TotImmediateFlipBytes = locals->TotImmediateFlipBytes + mode_lib->ms.cache_display_cfg.hw.DPPPerSurface[k] * locals->PixelPTEBytesPerRow[k];
					}
#ifdef __DML_VBA_DEBUG__
					dml_print("DML::%s: k = %u\n", __func__, k);
					dml_print("DML::%s: DPPPerSurface = %u\n", __func__, mode_lib->ms.cache_display_cfg.hw.DPPPerSurface[k]);
					dml_print("DML::%s: PDEAndMetaPTEBytesFrame = %u\n", __func__, locals->PDEAndMetaPTEBytesFrame[k]);
					dml_print("DML::%s: MetaRowByte = %u\n", __func__, locals->MetaRowByte[k]);
					dml_print("DML::%s: PixelPTEBytesPerRow = %u\n", __func__, locals->PixelPTEBytesPerRow[k]);
					dml_print("DML::%s: TotImmediateFlipBytes = %u\n", __func__, locals->TotImmediateFlipBytes);
#endif
				}
			}
			for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
				CalculateFlipSchedule(
						s->HostVMInefficiencyFactor,
						locals->UrgentExtraLatency,
						locals->UrgentLatency,
						mode_lib->ms.cache_display_cfg.plane.GPUVMMaxPageTableLevels,
						mode_lib->ms.cache_display_cfg.plane.HostVMEnable,
						mode_lib->ms.cache_display_cfg.plane.HostVMMaxPageTableLevels,
						mode_lib->ms.cache_display_cfg.plane.GPUVMEnable,
						mode_lib->ms.soc.hostvm_min_page_size_kbytes * 1024,
						locals->PDEAndMetaPTEBytesFrame[k],
						locals->MetaRowByte[k],
						locals->PixelPTEBytesPerRow[k],
						locals->BandwidthAvailableForImmediateFlip,
						locals->TotImmediateFlipBytes,
						mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k],
						mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k],
						mode_lib->ms.cache_display_cfg.plane.VRatio[k],
						mode_lib->ms.cache_display_cfg.plane.VRatioChroma[k],
						locals->Tno_bw[k],
						mode_lib->ms.cache_display_cfg.surface.DCCEnable[k],
						locals->dpte_row_height[k],
						locals->meta_row_height[k],
						locals->dpte_row_height_chroma[k],
						locals->meta_row_height_chroma[k],
						locals->use_one_row_for_frame_flip[k],

						/* Output */
						&locals->DestinationLinesToRequestVMInImmediateFlip[k],
						&locals->DestinationLinesToRequestRowInImmediateFlip[k],
						&locals->final_flip_bw[k],
						&locals->ImmediateFlipSupportedForPipe[k]);
			}

			CalculateImmediateFlipBandwithSupport(mode_lib->ms.num_active_planes,
												mode_lib->ms.ReturnBW,
												mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange,
												mode_lib->ms.policy.ImmediateFlipRequirement,
												locals->final_flip_bw,
												locals->ReadBandwidthSurfaceLuma,
												locals->ReadBandwidthSurfaceChroma,
												locals->RequiredPrefetchPixDataBWLuma,
												locals->RequiredPrefetchPixDataBWChroma,
												locals->cursor_bw,
												locals->meta_row_bw,
												locals->dpte_row_bw,
												locals->cursor_bw_pre,
												locals->prefetch_vmrow_bw,
												mode_lib->ms.cache_display_cfg.hw.DPPPerSurface,
												locals->UrgBurstFactorLuma,
												locals->UrgBurstFactorChroma,
												locals->UrgBurstFactorCursor,
												locals->UrgBurstFactorLumaPre,
												locals->UrgBurstFactorChromaPre,
												locals->UrgBurstFactorCursorPre,

												/* output */
												&locals->total_dcn_read_bw_with_flip, // dml_float_t *TotalBandwidth
												&locals->total_dcn_read_bw_with_flip_not_including_MALL_prefetch, // dml_float_t TotalBandwidthNotIncludingMALLPrefetch
												&s->dummy_single[0], // dml_float_t *FractionOfUrgentBandwidth
												&locals->ImmediateFlipSupported); // dml_bool_t *ImmediateFlipBandwidthSupport

			CalculateImmediateFlipBandwithSupport(mode_lib->ms.num_active_planes,
				mode_lib->ms.ReturnBW,
				mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange,
				mode_lib->ms.policy.ImmediateFlipRequirement,
				locals->final_flip_bw,
				locals->ReadBandwidthSurfaceLuma,
				locals->ReadBandwidthSurfaceChroma,
				locals->RequiredPrefetchPixDataBWLuma,
				locals->RequiredPrefetchPixDataBWChroma,
				locals->cursor_bw,
				locals->meta_row_bw,
				locals->dpte_row_bw,
				locals->cursor_bw_pre,
				locals->prefetch_vmrow_bw,
				mode_lib->ms.cache_display_cfg.hw.DPPPerSurface,
				s->dummy_unit_vector,
				s->dummy_unit_vector,
				s->dummy_unit_vector,
				s->dummy_unit_vector,
				s->dummy_unit_vector,
				s->dummy_unit_vector,

				/* output */
				&locals->non_urgent_total_dcn_read_bw_with_flip, // dml_float_t *TotalBandwidth
				&locals->non_urgent_total_dcn_read_bw_with_flip_not_including_MALL_prefetch, // dml_float_t TotalBandwidthNotIncludingMALLPrefetch
				&locals->FractionOfUrgentBandwidthImmediateFlip, // dml_float_t *FractionOfUrgentBandwidth
				&s->dummy_boolean[0]); // dml_bool_t *ImmediateFlipBandwidthSupport

			for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
				if (mode_lib->ms.policy.ImmediateFlipRequirement[k] != dml_immediate_flip_not_required && locals->ImmediateFlipSupportedForPipe[k] == false) {
					locals->ImmediateFlipSupported = false;
#ifdef __DML_VBA_DEBUG__
					dml_print("DML::%s: Pipe %0d not supporting iflip\n", __func__, k);
#endif
				}
			}
		} else {
			locals->ImmediateFlipSupported = false;
			locals->total_dcn_read_bw_with_flip = s->MaxTotalRDBandwidth;
			locals->total_dcn_read_bw_with_flip_not_including_MALL_prefetch = s->MaxTotalRDBandwidthNotIncludingMALLPrefetch;
			locals->non_urgent_total_dcn_read_bw_with_flip = s->NonUrgentMaxTotalRDBandwidth;
			locals->non_urgent_total_dcn_read_bw_with_flip_not_including_MALL_prefetch = s->NonUrgentMaxTotalRDBandwidthNotIncludingMALLPrefetch;
		}

		/* consider flip support is okay if the flip bw is ok or (when user does't require a iflip and there is no host vm) */
		locals->PrefetchAndImmediateFlipSupported = (locals->PrefetchModeSupported == true &&
													((!mode_lib->ms.support.ImmediateFlipSupport && !mode_lib->ms.cache_display_cfg.plane.HostVMEnable && !s->ImmediateFlipRequirementFinal) ||
													locals->ImmediateFlipSupported)) ? true : false;

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: PrefetchModeSupported = %u\n", __func__, locals->PrefetchModeSupported);
		for (k = 0; k < mode_lib->ms.num_active_planes; ++k)
		dml_print("DML::%s: ImmediateFlipRequirement[%u] = %u\n", __func__, k, mode_lib->ms.policy.ImmediateFlipRequirement[k] == dml_immediate_flip_required);
		dml_print("DML::%s: HostVMEnable = %u\n", __func__, mode_lib->ms.cache_display_cfg.plane.HostVMEnable);
		dml_print("DML::%s: ImmediateFlipSupport = %u (from mode_support)\n", __func__, mode_lib->ms.support.ImmediateFlipSupport);
		dml_print("DML::%s: ImmediateFlipSupported = %u\n", __func__, locals->ImmediateFlipSupported);
		dml_print("DML::%s: PrefetchAndImmediateFlipSupported = %u\n", __func__, locals->PrefetchAndImmediateFlipSupported);
#endif
		dml_print("DML::%s: Done one iteration: VStartupLines=%u, MaxVStartupAllPlanes=%u\n", __func__, s->VStartupLines, s->MaxVStartupAllPlanes);

		s->VStartupLines = s->VStartupLines + 1;

		if (s->VStartupLines > s->MaxVStartupAllPlanes) {
			s->VStartupLines = __DML_VBA_MIN_VSTARTUP__;

			for (k = 0; k <= mode_lib->ms.num_active_planes - 1; k++) {
				s->NextPrefetchMode[k] = s->NextPrefetchMode[k] + 1;

				if (s->NextPrefetchMode[k] <= s->MaxPrefetchMode[k])
					s->AllPrefetchModeTested = false;
				dml_print("DML::%s: VStartupLines=%u, reaches max vstartup, try next prefetch mode=%u\n", __func__, s->VStartupLines-1, s->AllPrefetchModeTested);
			}
		} else {
			s->AllPrefetchModeTested = false;
		}
		s->iteration++;
		if (s->iteration > 2500) {
			dml_print("ERROR: DML::%s: Too many errors, exit now\n", __func__);
			ASSERT(0);
		}
	} while (!(locals->PrefetchAndImmediateFlipSupported || s->AllPrefetchModeTested));

	if (locals->PrefetchAndImmediateFlipSupported) {
		dml_print("DML::%s: Good, Prefetch and flip scheduling solution found at VStartupLines=%u (MaxVStartupAllPlanes=%u)\n", __func__, s->VStartupLines-1, s->MaxVStartupAllPlanes);
	} else {
		dml_print("DML::%s: Bad, Prefetch and flip scheduling solution did NOT find solution! (MaxVStartupAllPlanes=%u)\n", __func__, s->MaxVStartupAllPlanes);
	}

	//Watermarks and NB P-State/DRAM Clock Change Support
	{
		s->mmSOCParameters.UrgentLatency = locals->UrgentLatency;
		s->mmSOCParameters.ExtraLatency = locals->UrgentExtraLatency;
		s->mmSOCParameters.WritebackLatency = mode_lib->ms.state.writeback_latency_us;
		s->mmSOCParameters.DRAMClockChangeLatency = mode_lib->ms.state.dram_clock_change_latency_us;
		s->mmSOCParameters.FCLKChangeLatency = mode_lib->ms.state.fclk_change_latency_us;
		s->mmSOCParameters.SRExitTime = mode_lib->ms.state.sr_exit_time_us;
		s->mmSOCParameters.SREnterPlusExitTime = mode_lib->ms.state.sr_enter_plus_exit_time_us;
		s->mmSOCParameters.SRExitZ8Time = mode_lib->ms.state.sr_exit_z8_time_us;
		s->mmSOCParameters.SREnterPlusExitZ8Time = mode_lib->ms.state.sr_enter_plus_exit_z8_time_us;
		s->mmSOCParameters.USRRetrainingLatency = mode_lib->ms.state.usr_retraining_latency_us;
		s->mmSOCParameters.SMNLatency = mode_lib->ms.soc.smn_latency_us;

		CalculateWatermarks_params->USRRetrainingRequiredFinal = mode_lib->ms.policy.USRRetrainingRequiredFinal;
		CalculateWatermarks_params->UseMALLForPStateChange = mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange;
		CalculateWatermarks_params->PrefetchMode = locals->PrefetchMode;
		CalculateWatermarks_params->NumberOfActiveSurfaces = mode_lib->ms.num_active_planes;
		CalculateWatermarks_params->MaxLineBufferLines = mode_lib->ms.ip.max_line_buffer_lines;
		CalculateWatermarks_params->LineBufferSize = mode_lib->ms.ip.line_buffer_size_bits;
		CalculateWatermarks_params->WritebackInterfaceBufferSize = mode_lib->ms.ip.writeback_interface_buffer_size_kbytes;
		CalculateWatermarks_params->DCFCLK = locals->Dcfclk;
		CalculateWatermarks_params->ReturnBW = mode_lib->ms.ReturnBW;
		CalculateWatermarks_params->SynchronizeTimingsFinal = mode_lib->ms.policy.SynchronizeTimingsFinal;
		CalculateWatermarks_params->SynchronizeDRRDisplaysForUCLKPStateChangeFinal = mode_lib->ms.policy.SynchronizeDRRDisplaysForUCLKPStateChangeFinal;
		CalculateWatermarks_params->DRRDisplay = mode_lib->ms.cache_display_cfg.timing.DRRDisplay;
		CalculateWatermarks_params->dpte_group_bytes = locals->dpte_group_bytes;
		CalculateWatermarks_params->meta_row_height = locals->meta_row_height;
		CalculateWatermarks_params->meta_row_height_chroma = locals->meta_row_height_chroma;
		CalculateWatermarks_params->mmSOCParameters = s->mmSOCParameters;
		CalculateWatermarks_params->WritebackChunkSize = mode_lib->ms.ip.writeback_chunk_size_kbytes;
		CalculateWatermarks_params->SOCCLK = mode_lib->ms.SOCCLK;
		CalculateWatermarks_params->DCFClkDeepSleep = locals->DCFCLKDeepSleep;
		CalculateWatermarks_params->DETBufferSizeY = locals->DETBufferSizeY;
		CalculateWatermarks_params->DETBufferSizeC = locals->DETBufferSizeC;
		CalculateWatermarks_params->SwathHeightY = locals->SwathHeightY;
		CalculateWatermarks_params->SwathHeightC = locals->SwathHeightC;
		CalculateWatermarks_params->LBBitPerPixel = mode_lib->ms.cache_display_cfg.plane.LBBitPerPixel;
		CalculateWatermarks_params->SwathWidthY = locals->SwathWidthY;
		CalculateWatermarks_params->SwathWidthC = locals->SwathWidthC;
		CalculateWatermarks_params->HRatio = mode_lib->ms.cache_display_cfg.plane.HRatio;
		CalculateWatermarks_params->HRatioChroma = mode_lib->ms.cache_display_cfg.plane.HRatioChroma;
		CalculateWatermarks_params->VTaps = mode_lib->ms.cache_display_cfg.plane.VTaps;
		CalculateWatermarks_params->VTapsChroma = mode_lib->ms.cache_display_cfg.plane.VTapsChroma;
		CalculateWatermarks_params->VRatio = mode_lib->ms.cache_display_cfg.plane.VRatio;
		CalculateWatermarks_params->VRatioChroma = mode_lib->ms.cache_display_cfg.plane.VRatioChroma;
		CalculateWatermarks_params->HTotal = mode_lib->ms.cache_display_cfg.timing.HTotal;
		CalculateWatermarks_params->VTotal = mode_lib->ms.cache_display_cfg.timing.VTotal;
		CalculateWatermarks_params->VActive = mode_lib->ms.cache_display_cfg.timing.VActive;
		CalculateWatermarks_params->PixelClock = mode_lib->ms.cache_display_cfg.timing.PixelClock;
		CalculateWatermarks_params->BlendingAndTiming = mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming;
		CalculateWatermarks_params->DPPPerSurface = mode_lib->ms.cache_display_cfg.hw.DPPPerSurface;
		CalculateWatermarks_params->BytePerPixelDETY = locals->BytePerPixelDETY;
		CalculateWatermarks_params->BytePerPixelDETC = locals->BytePerPixelDETC;
		CalculateWatermarks_params->DSTXAfterScaler = locals->DSTXAfterScaler;
		CalculateWatermarks_params->DSTYAfterScaler = locals->DSTYAfterScaler;
		CalculateWatermarks_params->WritebackEnable = mode_lib->ms.cache_display_cfg.writeback.WritebackEnable;
		CalculateWatermarks_params->WritebackPixelFormat = mode_lib->ms.cache_display_cfg.writeback.WritebackPixelFormat;
		CalculateWatermarks_params->WritebackDestinationWidth = mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationWidth;
		CalculateWatermarks_params->WritebackDestinationHeight = mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationHeight;
		CalculateWatermarks_params->WritebackSourceHeight = mode_lib->ms.cache_display_cfg.writeback.WritebackSourceHeight;
		CalculateWatermarks_params->UnboundedRequestEnabled = locals->UnboundedRequestEnabled;
		CalculateWatermarks_params->CompressedBufferSizeInkByte = locals->CompressedBufferSizeInkByte;

		// Output
		CalculateWatermarks_params->Watermark = &locals->Watermark; // Watermarks *Watermark
		CalculateWatermarks_params->DRAMClockChangeSupport = &locals->DRAMClockChangeSupport;
		CalculateWatermarks_params->MaxActiveDRAMClockChangeLatencySupported = locals->MaxActiveDRAMClockChangeLatencySupported; // dml_float_t *MaxActiveDRAMClockChangeLatencySupported[]
		CalculateWatermarks_params->SubViewportLinesNeededInMALL = locals->SubViewportLinesNeededInMALL; // dml_uint_t SubViewportLinesNeededInMALL[]
		CalculateWatermarks_params->FCLKChangeSupport = &locals->FCLKChangeSupport;
		CalculateWatermarks_params->MaxActiveFCLKChangeLatencySupported = &locals->MaxActiveFCLKChangeLatencySupported; // dml_float_t *MaxActiveFCLKChangeLatencySupported
		CalculateWatermarks_params->USRRetrainingSupport = &locals->USRRetrainingSupport;

		CalculateWatermarksMALLUseAndDRAMSpeedChangeSupport(
			&mode_lib->scratch,
			CalculateWatermarks_params);

		/* Copy the calculated watermarks to mp.Watermark as the getter functions are
		 * implemented by the DML team to copy the calculated values from the mp.Watermark interface.
		 * &mode_lib->mp.Watermark and &locals->Watermark are the same address, memcpy may lead to
		 * unexpected behavior. memmove should be used.
		 */
		memmove(&mode_lib->mp.Watermark, CalculateWatermarks_params->Watermark, sizeof(struct Watermarks));

		for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
			if (mode_lib->ms.cache_display_cfg.writeback.WritebackEnable[k] == true) {
				locals->WritebackAllowDRAMClockChangeEndPosition[k] = dml_max(0, locals->VStartupMin[k] * mode_lib->ms.cache_display_cfg.timing.HTotal[k] /
																			mode_lib->ms.cache_display_cfg.timing.PixelClock[k] - locals->Watermark.WritebackDRAMClockChangeWatermark);
				locals->WritebackAllowFCLKChangeEndPosition[k] = dml_max(0, locals->VStartupMin[k] * mode_lib->ms.cache_display_cfg.timing.HTotal[k] /
																			mode_lib->ms.cache_display_cfg.timing.PixelClock[k] - locals->Watermark.WritebackFCLKChangeWatermark);
			} else {
				locals->WritebackAllowDRAMClockChangeEndPosition[k] = 0;
				locals->WritebackAllowFCLKChangeEndPosition[k] = 0;
			}
		}
	}

	//Display Pipeline Delivery Time in Prefetch, Groups
	CalculatePixelDeliveryTimes(
			mode_lib->ms.num_active_planes,
			mode_lib->ms.cache_display_cfg.plane.VRatio,
			mode_lib->ms.cache_display_cfg.plane.VRatioChroma,
			locals->VRatioPrefetchY,
			locals->VRatioPrefetchC,
			locals->swath_width_luma_ub,
			locals->swath_width_chroma_ub,
			mode_lib->ms.cache_display_cfg.hw.DPPPerSurface,
			mode_lib->ms.cache_display_cfg.plane.HRatio,
			mode_lib->ms.cache_display_cfg.plane.HRatioChroma,
			mode_lib->ms.cache_display_cfg.timing.PixelClock,
			locals->PSCL_THROUGHPUT,
			locals->PSCL_THROUGHPUT_CHROMA,
			locals->Dppclk,
			locals->BytePerPixelC,
			mode_lib->ms.cache_display_cfg.plane.SourceScan,
			mode_lib->ms.cache_display_cfg.plane.NumberOfCursors,
			mode_lib->ms.cache_display_cfg.plane.CursorWidth,
			mode_lib->ms.cache_display_cfg.plane.CursorBPP,
			locals->BlockWidth256BytesY,
			locals->BlockHeight256BytesY,
			locals->BlockWidth256BytesC,
			locals->BlockHeight256BytesC,

			/* Output */
			locals->DisplayPipeLineDeliveryTimeLuma,
			locals->DisplayPipeLineDeliveryTimeChroma,
			locals->DisplayPipeLineDeliveryTimeLumaPrefetch,
			locals->DisplayPipeLineDeliveryTimeChromaPrefetch,
			locals->DisplayPipeRequestDeliveryTimeLuma,
			locals->DisplayPipeRequestDeliveryTimeChroma,
			locals->DisplayPipeRequestDeliveryTimeLumaPrefetch,
			locals->DisplayPipeRequestDeliveryTimeChromaPrefetch,
			locals->CursorRequestDeliveryTime,
			locals->CursorRequestDeliveryTimePrefetch);

	CalculateMetaAndPTETimes(
			locals->use_one_row_for_frame,
			mode_lib->ms.num_active_planes,
			mode_lib->ms.cache_display_cfg.plane.GPUVMEnable,
			mode_lib->ms.ip.meta_chunk_size_kbytes,
			mode_lib->ms.ip.min_meta_chunk_size_bytes,
			mode_lib->ms.cache_display_cfg.timing.HTotal,
			mode_lib->ms.cache_display_cfg.plane.VRatio,
			mode_lib->ms.cache_display_cfg.plane.VRatioChroma,
			locals->DestinationLinesToRequestRowInVBlank,
			locals->DestinationLinesToRequestRowInImmediateFlip,
			mode_lib->ms.cache_display_cfg.surface.DCCEnable,
			mode_lib->ms.cache_display_cfg.timing.PixelClock,
			locals->BytePerPixelY,
			locals->BytePerPixelC,
			mode_lib->ms.cache_display_cfg.plane.SourceScan,
			locals->dpte_row_height,
			locals->dpte_row_height_chroma,
			locals->meta_row_width,
			locals->meta_row_width_chroma,
			locals->meta_row_height,
			locals->meta_row_height_chroma,
			locals->meta_req_width,
			locals->meta_req_width_chroma,
			locals->meta_req_height,
			locals->meta_req_height_chroma,
			locals->dpte_group_bytes,
			locals->PTERequestSizeY,
			locals->PTERequestSizeC,
			locals->PixelPTEReqWidthY,
			locals->PixelPTEReqHeightY,
			locals->PixelPTEReqWidthC,
			locals->PixelPTEReqHeightC,
			locals->dpte_row_width_luma_ub,
			locals->dpte_row_width_chroma_ub,

			/* Output */
			locals->DST_Y_PER_PTE_ROW_NOM_L,
			locals->DST_Y_PER_PTE_ROW_NOM_C,
			locals->DST_Y_PER_META_ROW_NOM_L,
			locals->DST_Y_PER_META_ROW_NOM_C,
			locals->TimePerMetaChunkNominal,
			locals->TimePerChromaMetaChunkNominal,
			locals->TimePerMetaChunkVBlank,
			locals->TimePerChromaMetaChunkVBlank,
			locals->TimePerMetaChunkFlip,
			locals->TimePerChromaMetaChunkFlip,
			locals->time_per_pte_group_nom_luma,
			locals->time_per_pte_group_vblank_luma,
			locals->time_per_pte_group_flip_luma,
			locals->time_per_pte_group_nom_chroma,
			locals->time_per_pte_group_vblank_chroma,
			locals->time_per_pte_group_flip_chroma);

	CalculateVMGroupAndRequestTimes(
			mode_lib->ms.num_active_planes,
			mode_lib->ms.cache_display_cfg.plane.GPUVMEnable,
			mode_lib->ms.cache_display_cfg.plane.GPUVMMaxPageTableLevels,
			mode_lib->ms.cache_display_cfg.timing.HTotal,
			locals->BytePerPixelC,
			locals->DestinationLinesToRequestVMInVBlank,
			locals->DestinationLinesToRequestVMInImmediateFlip,
			mode_lib->ms.cache_display_cfg.surface.DCCEnable,
			mode_lib->ms.cache_display_cfg.timing.PixelClock,
			locals->dpte_row_width_luma_ub,
			locals->dpte_row_width_chroma_ub,
			locals->vm_group_bytes,
			locals->dpde0_bytes_per_frame_ub_l,
			locals->dpde0_bytes_per_frame_ub_c,
			locals->meta_pte_bytes_per_frame_ub_l,
			locals->meta_pte_bytes_per_frame_ub_c,

			/* Output */
			locals->TimePerVMGroupVBlank,
			locals->TimePerVMGroupFlip,
			locals->TimePerVMRequestVBlank,
			locals->TimePerVMRequestFlip);

	// Min TTUVBlank
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (locals->PrefetchMode[k] == 0) {
			locals->MinTTUVBlank[k] = dml_max4(
					locals->Watermark.DRAMClockChangeWatermark,
					locals->Watermark.FCLKChangeWatermark,
					locals->Watermark.StutterEnterPlusExitWatermark,
					locals->Watermark.UrgentWatermark);
		} else if (locals->PrefetchMode[k] == 1) {
			locals->MinTTUVBlank[k] = dml_max3(
					locals->Watermark.FCLKChangeWatermark,
					locals->Watermark.StutterEnterPlusExitWatermark,
					locals->Watermark.UrgentWatermark);
		} else if (locals->PrefetchMode[k] == 2) {
			locals->MinTTUVBlank[k] = dml_max(
					locals->Watermark.StutterEnterPlusExitWatermark,
					locals->Watermark.UrgentWatermark);
		} else {
			locals->MinTTUVBlank[k] = locals->Watermark.UrgentWatermark;
		}
		if (!mode_lib->ms.cache_display_cfg.plane.DynamicMetadataEnable[k])
			locals->MinTTUVBlank[k] = locals->TCalc + locals->MinTTUVBlank[k];
	}

	// DCC Configuration
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: Calculate DCC configuration for surface k=%u\n", __func__, k);
#endif
		CalculateDCCConfiguration(
			mode_lib->ms.cache_display_cfg.surface.DCCEnable[k],
			mode_lib->ms.policy.DCCProgrammingAssumesScanDirectionUnknownFinal,
			mode_lib->ms.cache_display_cfg.surface.SourcePixelFormat[k],
			mode_lib->ms.cache_display_cfg.surface.SurfaceWidthY[k],
			mode_lib->ms.cache_display_cfg.surface.SurfaceWidthC[k],
			mode_lib->ms.cache_display_cfg.surface.SurfaceHeightY[k],
			mode_lib->ms.cache_display_cfg.surface.SurfaceHeightC[k],
			mode_lib->ms.NomDETInKByte,
			locals->BlockHeight256BytesY[k],
			locals->BlockHeight256BytesC[k],
			mode_lib->ms.cache_display_cfg.surface.SurfaceTiling[k],
			locals->BytePerPixelY[k],
			locals->BytePerPixelC[k],
			locals->BytePerPixelDETY[k],
			locals->BytePerPixelDETC[k],
			mode_lib->ms.cache_display_cfg.plane.SourceScan[k],
			/* Output */
			&locals->DCCYMaxUncompressedBlock[k],
			&locals->DCCCMaxUncompressedBlock[k],
			&locals->DCCYMaxCompressedBlock[k],
			&locals->DCCCMaxCompressedBlock[k],
			&locals->DCCYIndependentBlock[k],
			&locals->DCCCIndependentBlock[k]);
	}

	// VStartup Adjustment
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		s->Tvstartup_margin = (s->MaxVStartupLines[k] - locals->VStartupMin[k]) * mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k];
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u, MinTTUVBlank = %f (before vstartup margin)\n", __func__, k, locals->MinTTUVBlank[k]);
#endif

		locals->MinTTUVBlank[k] = locals->MinTTUVBlank[k] + s->Tvstartup_margin;

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u, Tvstartup_margin = %f\n", __func__, k, s->Tvstartup_margin);
		dml_print("DML::%s: k=%u, MaxVStartupLines = %u\n", __func__, k, s->MaxVStartupLines[k]);
		dml_print("DML::%s: k=%u, MinTTUVBlank = %f\n", __func__, k, locals->MinTTUVBlank[k]);
#endif

		locals->Tdmdl[k] = locals->Tdmdl[k] + s->Tvstartup_margin;
		if (mode_lib->ms.cache_display_cfg.plane.DynamicMetadataEnable[k] && mode_lib->ms.ip.dynamic_metadata_vm_enabled) {
			locals->Tdmdl_vm[k] = locals->Tdmdl_vm[k] + s->Tvstartup_margin;
		}

		isInterlaceTiming        = (mode_lib->ms.cache_display_cfg.timing.Interlace[k] && !mode_lib->ms.ip.ptoi_supported);

		// The actual positioning of the vstartup
		locals->VStartup[k] = (isInterlaceTiming ? (2 * s->MaxVStartupLines[k]) : s->MaxVStartupLines[k]);

		s->dlg_vblank_start        =  ((isInterlaceTiming ? dml_floor((mode_lib->ms.cache_display_cfg.timing.VTotal[k] - mode_lib->ms.cache_display_cfg.timing.VFrontPorch[k]) / 2.0, 1.0) :
																	mode_lib->ms.cache_display_cfg.timing.VTotal[k]) - mode_lib->ms.cache_display_cfg.timing.VFrontPorch[k]);
		s->LSetup                  = dml_floor(4.0 * locals->TSetup[k] / ((dml_float_t) mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k]), 1.0) / 4.0;
		s->blank_lines_remaining   = (mode_lib->ms.cache_display_cfg.timing.VTotal[k] - mode_lib->ms.cache_display_cfg.timing.VActive[k]) - locals->VStartup[k];

		if (s->blank_lines_remaining < 0) {
			dml_print("ERROR: Vstartup is larger than vblank!?\n");
			s->blank_lines_remaining = 0;
			ASSERT(0);
		}
		locals->MIN_DST_Y_NEXT_START[k] = s->dlg_vblank_start + s->blank_lines_remaining + s->LSetup;

		// debug only
		s->old_MIN_DST_Y_NEXT_START = ((isInterlaceTiming ? dml_floor((mode_lib->ms.cache_display_cfg.timing.VTotal[k] - mode_lib->ms.cache_display_cfg.timing.VFrontPorch[k]) / 2.0, 1.0) :
																	mode_lib->ms.cache_display_cfg.timing.VTotal[k]) - mode_lib->ms.cache_display_cfg.timing.VFrontPorch[k])
											+ dml_max(1.0, dml_ceil((dml_float_t) locals->WritebackDelay[k] / ((dml_float_t) mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k]), 1.0))
											+ dml_floor(4.0 * locals->TSetup[k] / ((dml_float_t) mode_lib->ms.cache_display_cfg.timing.HTotal[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k]), 1.0) / 4.0;

		if (((locals->VUpdateOffsetPix[k] + locals->VUpdateWidthPix[k] + locals->VReadyOffsetPix[k]) / mode_lib->ms.cache_display_cfg.timing.HTotal[k]) <=
			(isInterlaceTiming ?
				dml_floor((mode_lib->ms.cache_display_cfg.timing.VTotal[k] - mode_lib->ms.cache_display_cfg.timing.VActive[k] - mode_lib->ms.cache_display_cfg.timing.VFrontPorch[k] - locals->VStartup[k]) / 2.0, 1.0) :
				(int) (mode_lib->ms.cache_display_cfg.timing.VTotal[k] - mode_lib->ms.cache_display_cfg.timing.VActive[k] - mode_lib->ms.cache_display_cfg.timing.VFrontPorch[k] - locals->VStartup[k]))) {
			locals->VREADY_AT_OR_AFTER_VSYNC[k] = true;
		} else {
			locals->VREADY_AT_OR_AFTER_VSYNC[k] = false;
		}
#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u, VStartup = %u (max)\n", __func__, k, locals->VStartup[k]);
		dml_print("DML::%s: k=%u, VStartupMin = %u (max)\n", __func__, k, locals->VStartupMin[k]);
		dml_print("DML::%s: k=%u, VUpdateOffsetPix = %u\n", __func__, k, locals->VUpdateOffsetPix[k]);
		dml_print("DML::%s: k=%u, VUpdateWidthPix = %u\n", __func__, k, locals->VUpdateWidthPix[k]);
		dml_print("DML::%s: k=%u, VReadyOffsetPix = %u\n", __func__, k, locals->VReadyOffsetPix[k]);
		dml_print("DML::%s: k=%u, HTotal = %u\n", __func__, k, mode_lib->ms.cache_display_cfg.timing.HTotal[k]);
		dml_print("DML::%s: k=%u, VTotal = %u\n", __func__, k, mode_lib->ms.cache_display_cfg.timing.VTotal[k]);
		dml_print("DML::%s: k=%u, VActive = %u\n", __func__, k, mode_lib->ms.cache_display_cfg.timing.VActive[k]);
		dml_print("DML::%s: k=%u, VFrontPorch = %u\n", __func__, k, mode_lib->ms.cache_display_cfg.timing.VFrontPorch[k]);
		dml_print("DML::%s: k=%u, TSetup = %f\n", __func__, k, locals->TSetup[k]);
		dml_print("DML::%s: k=%u, MIN_DST_Y_NEXT_START = %f\n", __func__, k, locals->MIN_DST_Y_NEXT_START[k]);
		dml_print("DML::%s: k=%u, MIN_DST_Y_NEXT_START = %f (old)\n", __func__, k, s->old_MIN_DST_Y_NEXT_START);
		dml_print("DML::%s: k=%u, VREADY_AT_OR_AFTER_VSYNC = %u\n", __func__, k, locals->VREADY_AT_OR_AFTER_VSYNC[k]);
#endif
	}

	//Maximum Bandwidth Used
	s->TotalWRBandwidth = 0;
	s->WRBandwidth = 0;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.cache_display_cfg.writeback.WritebackEnable[k] == true && mode_lib->ms.cache_display_cfg.writeback.WritebackPixelFormat[k] == dml_444_32) {
			s->WRBandwidth = mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationWidth[k] * mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationHeight[k] /
							(mode_lib->ms.cache_display_cfg.timing.HTotal[k] * mode_lib->ms.cache_display_cfg.writeback.WritebackSourceHeight[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k]) * 4;
		} else if (mode_lib->ms.cache_display_cfg.writeback.WritebackEnable[k] == true) {
			s->WRBandwidth = mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationWidth[k] * mode_lib->ms.cache_display_cfg.writeback.WritebackDestinationHeight[k] /
							(mode_lib->ms.cache_display_cfg.timing.HTotal[k] * mode_lib->ms.cache_display_cfg.writeback.WritebackSourceHeight[k] / mode_lib->ms.cache_display_cfg.timing.PixelClock[k]) * 8;
		}
		s->TotalWRBandwidth = s->TotalWRBandwidth + s->WRBandwidth;
	}

	locals->TotalDataReadBandwidth = 0;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		locals->TotalDataReadBandwidth = locals->TotalDataReadBandwidth + locals->ReadBandwidthSurfaceLuma[k] + locals->ReadBandwidthSurfaceChroma[k];

#ifdef __DML_VBA_DEBUG__
		dml_print("DML::%s: k=%u, TotalDataReadBandwidth = %f\n", __func__, k, locals->TotalDataReadBandwidth);
		dml_print("DML::%s: k=%u, ReadBandwidthSurfaceLuma = %f\n", __func__, k, locals->ReadBandwidthSurfaceLuma[k]);
		dml_print("DML::%s: k=%u, ReadBandwidthSurfaceChroma = %f\n", __func__, k, locals->ReadBandwidthSurfaceChroma[k]);
#endif
	}

	locals->TotalDataReadBandwidthNotIncludingMALLPrefetch = 0;
	for (k = 0; k < mode_lib->ms.num_active_planes; ++k) {
		if (mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange[k] != dml_use_mall_pstate_change_phantom_pipe) {
			locals->TotalDataReadBandwidthNotIncludingMALLPrefetch = locals->TotalDataReadBandwidthNotIncludingMALLPrefetch
		+ locals->ReadBandwidthSurfaceLuma[k] + locals->ReadBandwidthSurfaceChroma[k];
		}
	}

	CalculateStutterEfficiency_params->CompressedBufferSizeInkByte = locals->CompressedBufferSizeInkByte;
	CalculateStutterEfficiency_params->UseMALLForPStateChange = mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange;
	CalculateStutterEfficiency_params->UnboundedRequestEnabled = locals->UnboundedRequestEnabled;
	CalculateStutterEfficiency_params->MetaFIFOSizeInKEntries = mode_lib->ms.ip.meta_fifo_size_in_kentries;
	CalculateStutterEfficiency_params->ZeroSizeBufferEntries = mode_lib->ms.ip.zero_size_buffer_entries;
	CalculateStutterEfficiency_params->PixelChunkSizeInKByte = mode_lib->ms.ip.pixel_chunk_size_kbytes;
	CalculateStutterEfficiency_params->NumberOfActiveSurfaces = mode_lib->ms.num_active_planes;
	CalculateStutterEfficiency_params->ROBBufferSizeInKByte = mode_lib->ms.ip.rob_buffer_size_kbytes;
	CalculateStutterEfficiency_params->TotalDataReadBandwidth = locals->TotalDataReadBandwidth;
	CalculateStutterEfficiency_params->DCFCLK = locals->Dcfclk;
	CalculateStutterEfficiency_params->ReturnBW = mode_lib->ms.ReturnBW;
	CalculateStutterEfficiency_params->CompbufReservedSpace64B = locals->compbuf_reserved_space_64b;
	CalculateStutterEfficiency_params->CompbufReservedSpaceZs = locals->compbuf_reserved_space_zs;
	CalculateStutterEfficiency_params->SRExitTime = mode_lib->ms.state.sr_exit_time_us;
	CalculateStutterEfficiency_params->SRExitZ8Time = mode_lib->ms.state.sr_exit_z8_time_us;
	CalculateStutterEfficiency_params->SynchronizeTimingsFinal = mode_lib->ms.policy.SynchronizeTimingsFinal;
	CalculateStutterEfficiency_params->BlendingAndTiming = mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming;
	CalculateStutterEfficiency_params->StutterEnterPlusExitWatermark = locals->Watermark.StutterEnterPlusExitWatermark;
	CalculateStutterEfficiency_params->Z8StutterEnterPlusExitWatermark = locals->Watermark.Z8StutterEnterPlusExitWatermark;
	CalculateStutterEfficiency_params->ProgressiveToInterlaceUnitInOPP = mode_lib->ms.ip.ptoi_supported;
	CalculateStutterEfficiency_params->Interlace = mode_lib->ms.cache_display_cfg.timing.Interlace;
	CalculateStutterEfficiency_params->MinTTUVBlank = locals->MinTTUVBlank;
	CalculateStutterEfficiency_params->DPPPerSurface = mode_lib->ms.cache_display_cfg.hw.DPPPerSurface;
	CalculateStutterEfficiency_params->DETBufferSizeY = locals->DETBufferSizeY;
	CalculateStutterEfficiency_params->BytePerPixelY = locals->BytePerPixelY;
	CalculateStutterEfficiency_params->BytePerPixelDETY = locals->BytePerPixelDETY;
	CalculateStutterEfficiency_params->SwathWidthY = locals->SwathWidthY;
	CalculateStutterEfficiency_params->SwathHeightY = locals->SwathHeightY;
	CalculateStutterEfficiency_params->SwathHeightC = locals->SwathHeightC;
	CalculateStutterEfficiency_params->NetDCCRateLuma = mode_lib->ms.cache_display_cfg.surface.DCCRateLuma;
	CalculateStutterEfficiency_params->NetDCCRateChroma = mode_lib->ms.cache_display_cfg.surface.DCCRateChroma;
	CalculateStutterEfficiency_params->DCCFractionOfZeroSizeRequestsLuma = mode_lib->ms.cache_display_cfg.surface.DCCFractionOfZeroSizeRequestsLuma;
	CalculateStutterEfficiency_params->DCCFractionOfZeroSizeRequestsChroma = mode_lib->ms.cache_display_cfg.surface.DCCFractionOfZeroSizeRequestsChroma;
	CalculateStutterEfficiency_params->HTotal = mode_lib->ms.cache_display_cfg.timing.HTotal;
	CalculateStutterEfficiency_params->VTotal = mode_lib->ms.cache_display_cfg.timing.VTotal;
	CalculateStutterEfficiency_params->PixelClock = mode_lib->ms.cache_display_cfg.timing.PixelClock;
	CalculateStutterEfficiency_params->VRatio = mode_lib->ms.cache_display_cfg.plane.VRatio;
	CalculateStutterEfficiency_params->SourceScan = mode_lib->ms.cache_display_cfg.plane.SourceScan;
	CalculateStutterEfficiency_params->BlockHeight256BytesY = locals->BlockHeight256BytesY;
	CalculateStutterEfficiency_params->BlockWidth256BytesY = locals->BlockWidth256BytesY;
	CalculateStutterEfficiency_params->BlockHeight256BytesC = locals->BlockHeight256BytesC;
	CalculateStutterEfficiency_params->BlockWidth256BytesC = locals->BlockWidth256BytesC;
	CalculateStutterEfficiency_params->DCCYMaxUncompressedBlock = locals->DCCYMaxUncompressedBlock;
	CalculateStutterEfficiency_params->DCCCMaxUncompressedBlock = locals->DCCCMaxUncompressedBlock;
	CalculateStutterEfficiency_params->VActive = mode_lib->ms.cache_display_cfg.timing.VActive;
	CalculateStutterEfficiency_params->DCCEnable = mode_lib->ms.cache_display_cfg.surface.DCCEnable;
	CalculateStutterEfficiency_params->WritebackEnable = mode_lib->ms.cache_display_cfg.writeback.WritebackEnable;
	CalculateStutterEfficiency_params->ReadBandwidthSurfaceLuma = locals->ReadBandwidthSurfaceLuma;
	CalculateStutterEfficiency_params->ReadBandwidthSurfaceChroma = locals->ReadBandwidthSurfaceChroma;
	CalculateStutterEfficiency_params->meta_row_bw = locals->meta_row_bw;
	CalculateStutterEfficiency_params->dpte_row_bw = locals->dpte_row_bw;
	CalculateStutterEfficiency_params->StutterEfficiencyNotIncludingVBlank = &locals->StutterEfficiencyNotIncludingVBlank;
	CalculateStutterEfficiency_params->StutterEfficiency = &locals->StutterEfficiency;
	CalculateStutterEfficiency_params->NumberOfStutterBurstsPerFrame = &locals->NumberOfStutterBurstsPerFrame;
	CalculateStutterEfficiency_params->Z8StutterEfficiencyNotIncludingVBlank = &locals->Z8StutterEfficiencyNotIncludingVBlank;
	CalculateStutterEfficiency_params->Z8StutterEfficiency = &locals->Z8StutterEfficiency;
	CalculateStutterEfficiency_params->Z8NumberOfStutterBurstsPerFrame = &locals->Z8NumberOfStutterBurstsPerFrame;
	CalculateStutterEfficiency_params->StutterPeriod = &locals->StutterPeriod;
	CalculateStutterEfficiency_params->DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE = &locals->DCHUBBUB_ARB_CSTATE_MAX_CAP_MODE;

	// Stutter Efficiency
	CalculateStutterEfficiency(&mode_lib->scratch,
		CalculateStutterEfficiency_params);

#ifdef __DML_VBA_ALLOW_DELTA__
	{
	dml_float_t dummy_single[2];
	dml_uint_t dummy_integer[1];
	dml_bool_t dummy_boolean[1];

	// Calculate z8 stutter eff assuming 0 reserved space
	CalculateStutterEfficiency(
			locals->CompressedBufferSizeInkByte,
			mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange,
			locals->UnboundedRequestEnabled,
			mode_lib->ms.ip.meta_fifo_size_in_kentries,
			mode_lib->ms.ip.zero_size_buffer_entries,
			mode_lib->ms.ip.pixel_chunk_size_kbytes,
			mode_lib->ms.num_active_planes,
			mode_lib->ms.ip.rob_buffer_size_kbytes,
			locals->TotalDataReadBandwidth,
			locals->Dcfclk,
			mode_lib->ms.ReturnBW,
			0, //mode_lib->ms.ip.compbuf_reserved_space_64b,
			0, //mode_lib->ms.ip.compbuf_reserved_space_zs,
			mode_lib->ms.state.sr_exit_time_us,
			mode_lib->ms.state.sr_exit_z8_time_us,
			mode_lib->ms.policy.SynchronizeTimingsFinal,
			mode_lib->ms.cache_display_cfg.plane.BlendingAndTiming,
			locals->Watermark.StutterEnterPlusExitWatermark,
			locals->Watermark.Z8StutterEnterPlusExitWatermark,
			mode_lib->ms.ip.ptoi_supported,
			mode_lib->ms.cache_display_cfg.timing.Interlace,
			locals->MinTTUVBlank,
			mode_lib->ms.cache_display_cfg.hw.DPPPerSurface,
			mode_lib->ms.DETBufferSizeY,
			locals->BytePerPixelY,
			locals->BytePerPixelDETY,
			locals->SwathWidthY,
			mode_lib->ms.SwathHeightY,
			mode_lib->ms.SwathHeightC,
			mode_lib->ms.cache_display_cfg.surface.DCCRateLuma,
			mode_lib->ms.cache_display_cfg.surface.DCCRateChroma,
			mode_lib->ms.cache_display_cfg.surface.DCCFractionOfZeroSizeRequestsLuma,
			mode_lib->ms.cache_display_cfg.surface.DCCFractionOfZeroSizeRequestsChroma,
			mode_lib->ms.cache_display_cfg.timing.HTotal,
			mode_lib->ms.cache_display_cfg.timing.VTotal,
			mode_lib->ms.cache_display_cfg.timing.PixelClock,
			mode_lib->ms.cache_display_cfg.plane.VRatio,
			mode_lib->ms.cache_display_cfg.plane.SourceScan,
			locals->BlockHeight256BytesY,
			locals->BlockWidth256BytesY,
			locals->BlockHeight256BytesC,
			locals->BlockWidth256BytesC,
			locals->DCCYMaxUncompressedBlock,
			locals->DCCCMaxUncompressedBlock,
			mode_lib->ms.cache_display_cfg.timing.VActive,
			mode_lib->ms.cache_display_cfg.surface.DCCEnable,
			mode_lib->ms.cache_display_cfg.writeback.WritebackEnable,
			locals->ReadBandwidthSurfaceLuma,
			locals->ReadBandwidthSurfaceChroma,
			locals->meta_row_bw,
			locals->dpte_row_bw,

			/* Output */
			&dummy_single[0],
			&dummy_single[1],
			&dummy_integer[0],
			&locals->Z8StutterEfficiencyNotIncludingVBlankBestCase,
			&locals->Z8StutterEfficiencyBestCase,
			&locals->Z8NumberOfStutterBurstsPerFrameBestCase,
			&locals->StutterPeriodBestCase,
			&dummy_boolean[0]);
	}
#else
	locals->Z8StutterEfficiencyNotIncludingVBlankBestCase = locals->Z8StutterEfficiencyNotIncludingVBlank;
	locals->Z8StutterEfficiencyBestCase                   = locals->Z8StutterEfficiency;
	locals->Z8NumberOfStutterBurstsPerFrameBestCase       = locals->Z8NumberOfStutterBurstsPerFrame;
	locals->StutterPeriodBestCase                         = locals->StutterPeriod;
#endif

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: --- END --- \n",  __func__);
#endif
} // dml_core_mode_programming

/// Function: dml_core_get_row_heights
/// @brief Get row height for DPTE and META with minimal input.
void dml_core_get_row_heights(
						dml_uint_t                         *dpte_row_height,
						dml_uint_t                         *meta_row_height,
						const struct display_mode_lib_st   *mode_lib,
						dml_bool_t                         is_plane1,
						enum dml_source_format_class        SourcePixelFormat,
						enum dml_swizzle_mode            SurfaceTiling,
						enum dml_rotation_angle          ScanDirection,
						dml_uint_t                         pitch,
						dml_uint_t                         GPUVMMinPageSizeKBytes)
{
	dml_uint_t BytePerPixelY;
	dml_uint_t BytePerPixelC;
	dml_float_t BytePerPixelInDETY;
	dml_float_t BytePerPixelInDETC;
	dml_uint_t BlockHeight256BytesY;
	dml_uint_t BlockHeight256BytesC;
	dml_uint_t BlockWidth256BytesY;
	dml_uint_t BlockWidth256BytesC;
	dml_uint_t MacroTileWidthY;
	dml_uint_t MacroTileWidthC;
	dml_uint_t MacroTileHeightY;
	dml_uint_t MacroTileHeightC;

	dml_uint_t BytePerPixel;
	dml_uint_t BlockHeight256Bytes;
	dml_uint_t BlockWidth256Bytes;
	dml_uint_t MacroTileWidth;
	dml_uint_t MacroTileHeight;
	dml_uint_t PTEBufferSizeInRequests;

	dml_uint_t dummy_integer[16];

	CalculateBytePerPixelAndBlockSizes(
		SourcePixelFormat,
		SurfaceTiling,

		/* Output */
		&BytePerPixelY,
		&BytePerPixelC,
		&BytePerPixelInDETY,
		&BytePerPixelInDETC,
		&BlockHeight256BytesY,
		&BlockHeight256BytesC,
		&BlockWidth256BytesY,
		&BlockWidth256BytesC,
		&MacroTileHeightY,
		&MacroTileHeightC,
		&MacroTileWidthY,
		&MacroTileWidthC);

	BytePerPixel = is_plane1 ? BytePerPixelC : BytePerPixelY;
	BlockHeight256Bytes = is_plane1 ? BlockHeight256BytesC : BlockHeight256BytesY;
	BlockWidth256Bytes = is_plane1 ? BlockWidth256BytesC : BlockWidth256BytesY;
	MacroTileWidth = is_plane1 ? MacroTileWidthC : MacroTileWidthY;
	MacroTileHeight = is_plane1 ? MacroTileHeightC : MacroTileHeightY;
	PTEBufferSizeInRequests = is_plane1 ? mode_lib->ip.dpte_buffer_size_in_pte_reqs_chroma : mode_lib->ip.dpte_buffer_size_in_pte_reqs_luma;
#ifdef __DML_RQ_DLG_CALC_DEBUG__
	dml_print("DML_DLG: %s: is_plane1 = %u\n", __func__, is_plane1);
	dml_print("DML_DLG: %s: BytePerPixel = %u\n", __func__, BytePerPixel);
	dml_print("DML_DLG: %s: BlockHeight256Bytes = %u\n", __func__, BlockHeight256Bytes);
	dml_print("DML_DLG: %s: BlockWidth256Bytes = %u\n", __func__, BlockWidth256Bytes);
	dml_print("DML_DLG: %s: MacroTileWidth = %u\n", __func__, MacroTileWidth);
	dml_print("DML_DLG: %s: MacroTileHeight = %u\n", __func__, MacroTileHeight);
	dml_print("DML_DLG: %s: PTEBufferSizeInRequests = %u\n", __func__, PTEBufferSizeInRequests);
	dml_print("DML_DLG: %s: dpte_buffer_size_in_pte_reqs_luma = %u\n", __func__, mode_lib->ip.dpte_buffer_size_in_pte_reqs_luma);
	dml_print("DML_DLG: %s: dpte_buffer_size_in_pte_reqs_chroma = %u\n", __func__, mode_lib->ip.dpte_buffer_size_in_pte_reqs_chroma);
	dml_print("DML_DLG: %s: GPUVMMinPageSizeKBytes = %u\n", __func__, GPUVMMinPageSizeKBytes);
#endif

	// just suppluy with enough parameters to calculate meta and dte
	CalculateVMAndRowBytes(
			0, // dml_bool_t ViewportStationary,
			1, // dml_bool_t DCCEnable,
			1, // dml_uint_t NumberOfDPPs,
			BlockHeight256Bytes,
			BlockWidth256Bytes,
			SourcePixelFormat,
			SurfaceTiling,
			BytePerPixel,
			ScanDirection,
			0, // dml_uint_t SwathWidth,
			0, // dml_uint_t ViewportHeight, (Note: DML calculates one_row_for_frame height regardless, would need test input if that height is useful)
			0, // dml_uint_t ViewportXStart,
			0, // dml_uint_t ViewportYStart,
			1, // dml_bool_t GPUVMEnable,
			4, // dml_uint_t GPUVMMaxPageTableLevels,
			GPUVMMinPageSizeKBytes,
			PTEBufferSizeInRequests,
			pitch,
			0, // dml_uint_t DCCMetaPitch,
			MacroTileWidth,
			MacroTileHeight,

			// /* Output */
			&dummy_integer[0], // dml_uint_t *MetaRowByte,
			&dummy_integer[1], // dml_uint_t *PixelPTEBytesPerRow,
			&dummy_integer[2], // dml_uint_t *PixelPTEBytesPerRowStorage,
			&dummy_integer[3], // dml_uint_t *dpte_row_width_ub,
			dpte_row_height,
			&dummy_integer[4], // dml_uint_t *dpte_row_height_linear
			&dummy_integer[5], // dml_uint_t *PixelPTEBytesPerRow_one_row_per_frame,
			&dummy_integer[6], // dml_uint_t *dpte_row_width_ub_one_row_per_frame,
			&dummy_integer[7], // dml_uint_t *dpte_row_height_one_row_per_frame,
			&dummy_integer[8], // dml_uint_t *MetaRequestWidth,
			&dummy_integer[9], // dml_uint_t *MetaRequestHeight,
			&dummy_integer[10], // dml_uint_t *meta_row_width,
			meta_row_height,
			&dummy_integer[11], // dml_uint_t *PixelPTEReqWidth,
			&dummy_integer[12], // dml_uint_t *PixelPTEReqHeight,
			&dummy_integer[13], // dml_uint_t *PTERequestSize,
			&dummy_integer[14], // dml_uint_t *DPDE0BytesFrame,
			&dummy_integer[15]); // dml_uint_t *MetaPTEBytesFrame)

#ifdef __DML_RQ_DLG_CALC_DEBUG__
	dml_print("DML_DLG: %s: dpte_row_height = %u\n", __func__, *dpte_row_height);
	dml_print("DML_DLG: %s: meta_row_height = %u\n", __func__, *meta_row_height);
#endif
}

static struct soc_state_bounding_box_st dml_get_soc_state_bounding_box(
	const struct soc_states_st *states,
	dml_uint_t state_idx)
{
	dml_print("DML::%s: state_idx=%u (num_states=%u)\n", __func__, state_idx, states->num_states);

	if (state_idx >= (dml_uint_t)states->num_states) {
		dml_print("DML::%s: ERROR: Invalid state_idx=%u! num_states=%u\n", __func__, state_idx, states->num_states);
		ASSERT(0);
	}
	return (states->state_array[state_idx]);
}

/// @brief Copy the parameters to a calculation struct, it actually only need when the DML needs to have
///        the intelligence to re-calculate when any of display cfg, bbox, or policy changes since last calculated.
///
static void cache_ip_soc_cfg(struct display_mode_lib_st *mode_lib,
						dml_uint_t state_idx)
{
	mode_lib->ms.state_idx = state_idx;
	mode_lib->ms.max_state_idx = mode_lib->states.num_states - 1;
	mode_lib->ms.soc = mode_lib->soc;
	mode_lib->ms.ip = mode_lib->ip;
	mode_lib->ms.policy = mode_lib->policy;
	mode_lib->ms.state = dml_get_soc_state_bounding_box(&mode_lib->states, state_idx);
	mode_lib->ms.max_state = dml_get_soc_state_bounding_box(&mode_lib->states, mode_lib->states.num_states - 1);
}

static void cache_display_cfg(struct display_mode_lib_st *mode_lib,
	const struct dml_display_cfg_st *display_cfg)
{
	mode_lib->ms.cache_display_cfg = *display_cfg;
}

static void fetch_socbb_params(struct display_mode_lib_st *mode_lib)
{
	struct soc_state_bounding_box_st *state = &mode_lib->ms.state;

	// Default values, SOCCLK, DRAMSpeed, and FabricClock will be reassigned to the same state value in mode_check step
	// If UseMinimumRequiredDCFCLK is used, the DCFCLK will be the min dcflk for the mode support
	mode_lib->ms.SOCCLK = (dml_float_t)state->socclk_mhz;
	mode_lib->ms.DRAMSpeed = (dml_float_t)state->dram_speed_mts;
	mode_lib->ms.FabricClock = (dml_float_t)state->fabricclk_mhz;
	mode_lib->ms.DCFCLK = (dml_float_t)state->dcfclk_mhz;
}

/// @brief Use display_cfg directly for mode_support calculation
///        Calculated values and informational output are stored in mode_lib.vba data struct
///        The display configuration is described with pipes struct and num_pipes
///        This function is used when physical resource mapping is not finalized (for example,
///        don't know how many pipes to represent a surface)
/// @param mode_lib Contains the bounding box and policy setting.
/// @param state_idx Power state index
/// @param display_cfg Display configurations. A display
dml_bool_t dml_mode_support(
	struct display_mode_lib_st *mode_lib,
	dml_uint_t                        state_idx,
	const struct dml_display_cfg_st *display_cfg)
{
	dml_bool_t is_mode_support;

	dml_print("DML::%s: ------------- START ----------\n", __func__);
	cache_ip_soc_cfg(mode_lib, state_idx);
	cache_display_cfg(mode_lib, display_cfg);

	fetch_socbb_params(mode_lib);

	dml_print("DML::%s: state_idx          = %u\n", __func__, state_idx);

	is_mode_support = dml_core_mode_support(mode_lib);

	dml_print("DML::%s: is_mode_support = %u\n", __func__, is_mode_support);
	dml_print("DML::%s: ------------- DONE ----------\n", __func__);
	return is_mode_support;
}

/// @Brief A function to calculate the programming values for DCN DCHUB (Assume mode is supported)
/// The output will be stored in the mode_lib.mp (mode_program_st) data struct and those can be accessed via the getter functions
/// Calculated values include: watermarks, dlg, rq reg, different clock frequency
/// This function returns 1 when there is no error.
/// Note: In this function, it is assumed that DCFCLK, SOCCLK freq are the state values, and mode_program will just use the DML calculated DPPCLK and DISPCLK
/// @param mode_lib mode_lib data struct that house all the input/output/bbox and calculation values.
/// @param state_idx Power state idx chosen
/// @param display_cfg Display Congiuration
/// @param call_standalone Calling mode_programming without calling mode support.  Some of the "support" struct member will be pre-calculated before doing mode programming
/// TODO: Add clk_cfg input, could be useful for standalone mode
dml_bool_t dml_mode_programming(
	struct display_mode_lib_st *mode_lib,
	dml_uint_t                         state_idx,
	const struct dml_display_cfg_st *display_cfg,
	bool                               call_standalone)
{
	struct dml_clk_cfg_st clk_cfg;
	memset(&clk_cfg, 0, sizeof(clk_cfg));

	clk_cfg.dcfclk_option = dml_use_required_freq;
	clk_cfg.dispclk_option = dml_use_required_freq;
	for (dml_uint_t k = 0; k < __DML_NUM_PLANES__; ++k)
		clk_cfg.dppclk_option[k] = dml_use_required_freq;

	dml_print("DML::%s: ------------- START ----------\n", __func__);
	dml_print("DML::%s: state_idx       = %u\n", __func__, state_idx);
	dml_print("DML::%s: call_standalone = %u\n", __func__, call_standalone);

	cache_ip_soc_cfg(mode_lib, state_idx);
	cache_display_cfg(mode_lib, display_cfg);

	fetch_socbb_params(mode_lib);
	if (call_standalone) {
		mode_lib->ms.support.ImmediateFlipSupport = 1; // assume mode support say immediate flip ok at max state/combine
		dml_core_mode_support_partial(mode_lib);
	}

	dml_core_mode_programming(mode_lib, &clk_cfg);

	dml_print("DML::%s: ------------- DONE ----------\n", __func__);
	dml_print("DML::%s: PrefetchAndImmediateFlipSupported = %0d\n", __func__, mode_lib->mp.PrefetchAndImmediateFlipSupported);
	return mode_lib->mp.PrefetchAndImmediateFlipSupported;
}

static dml_uint_t mode_support_pwr_states(
	dml_uint_t *lowest_state_idx,
	struct display_mode_lib_st *mode_lib,
	const struct dml_display_cfg_st *display_cfg,
	dml_uint_t start_state_idx,
	dml_uint_t end_state_idx)
{
	dml_uint_t state_idx = 0;
	dml_bool_t mode_is_supported = 0;
	*lowest_state_idx = end_state_idx;

	if (end_state_idx < start_state_idx)
		ASSERT(0);

	if (end_state_idx >= mode_lib->states.num_states) // idx is 0-based
		ASSERT(0);

	for (state_idx = start_state_idx; state_idx <= end_state_idx; state_idx++) {
		if (dml_mode_support(mode_lib, state_idx, display_cfg)) {
			dml_print("DML::%s: Mode is supported at power state_idx = %u\n", __func__, state_idx);
			mode_is_supported = 1;
			*lowest_state_idx = state_idx;
			break;
		}
	}

	return mode_is_supported;
}

dml_uint_t dml_mode_support_ex(struct dml_mode_support_ex_params_st *in_out_params)
{
	dml_uint_t result;

	result = mode_support_pwr_states(&in_out_params->out_lowest_state_idx,
		in_out_params->mode_lib,
		in_out_params->in_display_cfg,
		0,
		in_out_params->mode_lib->states.num_states - 1);

	if (result)
		*in_out_params->out_evaluation_info = in_out_params->mode_lib->ms.support;

	return result;
}

dml_bool_t dml_get_is_phantom_pipe(struct display_mode_lib_st *mode_lib, dml_uint_t pipe_idx)
{
	dml_uint_t plane_idx = mode_lib->mp.pipe_plane[pipe_idx];
	dml_print("DML::%s: pipe_idx=%d UseMALLForPStateChange=%0d\n", __func__, pipe_idx, mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange[plane_idx]);
	return (mode_lib->ms.cache_display_cfg.plane.UseMALLForPStateChange[plane_idx] == dml_use_mall_pstate_change_phantom_pipe);
}

#define dml_get_per_surface_var_func(variable, type, interval_var) type dml_get_##variable(struct display_mode_lib_st *mode_lib, dml_uint_t surface_idx) \
{ \
	dml_uint_t plane_idx; \
	plane_idx = mode_lib->mp.pipe_plane[surface_idx]; \
	return (type) interval_var[plane_idx]; \
}

#define dml_get_var_func(var, type, internal_var)  type dml_get_##var(struct display_mode_lib_st *mode_lib) \
{ \
	return (type) internal_var; \
}

dml_get_var_func(wm_urgent, dml_float_t, mode_lib->mp.Watermark.UrgentWatermark);
dml_get_var_func(wm_stutter_exit, dml_float_t, mode_lib->mp.Watermark.StutterExitWatermark);
dml_get_var_func(wm_stutter_enter_exit, dml_float_t, mode_lib->mp.Watermark.StutterEnterPlusExitWatermark);
dml_get_var_func(wm_memory_trip, dml_float_t, mode_lib->mp.UrgentLatency);
dml_get_var_func(wm_fclk_change, dml_float_t, mode_lib->mp.Watermark.FCLKChangeWatermark);
dml_get_var_func(wm_usr_retraining, dml_float_t, mode_lib->mp.Watermark.USRRetrainingWatermark);
dml_get_var_func(wm_dram_clock_change, dml_float_t, mode_lib->mp.Watermark.DRAMClockChangeWatermark);
dml_get_var_func(wm_z8_stutter_enter_exit, dml_float_t, mode_lib->mp.Watermark.Z8StutterEnterPlusExitWatermark);
dml_get_var_func(wm_z8_stutter, dml_float_t, mode_lib->mp.Watermark.Z8StutterExitWatermark);
dml_get_var_func(fraction_of_urgent_bandwidth, dml_float_t, mode_lib->mp.FractionOfUrgentBandwidth);
dml_get_var_func(fraction_of_urgent_bandwidth_imm_flip, dml_float_t, mode_lib->mp.FractionOfUrgentBandwidthImmediateFlip);
dml_get_var_func(urgent_latency, dml_float_t, mode_lib->mp.UrgentLatency);
dml_get_var_func(clk_dcf_deepsleep, dml_float_t, mode_lib->mp.DCFCLKDeepSleep);
dml_get_var_func(wm_writeback_dram_clock_change, dml_float_t, mode_lib->mp.Watermark.WritebackDRAMClockChangeWatermark);
dml_get_var_func(wm_writeback_urgent, dml_float_t, mode_lib->mp.Watermark.WritebackUrgentWatermark);
dml_get_var_func(stutter_efficiency, dml_float_t, mode_lib->mp.StutterEfficiency);
dml_get_var_func(stutter_efficiency_no_vblank, dml_float_t, mode_lib->mp.StutterEfficiencyNotIncludingVBlank);
dml_get_var_func(stutter_efficiency_z8, dml_float_t, mode_lib->mp.Z8StutterEfficiency);
dml_get_var_func(stutter_num_bursts_z8, dml_float_t, mode_lib->mp.Z8NumberOfStutterBurstsPerFrame);
dml_get_var_func(stutter_period, dml_float_t, mode_lib->mp.StutterPeriod);
dml_get_var_func(stutter_efficiency_z8_bestcase, dml_float_t, mode_lib->mp.Z8StutterEfficiencyBestCase);
dml_get_var_func(stutter_num_bursts_z8_bestcase, dml_float_t, mode_lib->mp.Z8NumberOfStutterBurstsPerFrameBestCase);
dml_get_var_func(stutter_period_bestcase, dml_float_t, mode_lib->mp.StutterPeriodBestCase);
dml_get_var_func(urgent_extra_latency, dml_float_t, mode_lib->mp.UrgentExtraLatency);
dml_get_var_func(fclk_change_latency, dml_float_t, mode_lib->mp.MaxActiveFCLKChangeLatencySupported);
dml_get_var_func(dispclk_calculated, dml_float_t, mode_lib->mp.Dispclk_calculated);
dml_get_var_func(total_data_read_bw, dml_float_t, mode_lib->mp.TotalDataReadBandwidth);
dml_get_var_func(return_bw, dml_float_t, mode_lib->ms.ReturnBW);
dml_get_var_func(return_dram_bw, dml_float_t, mode_lib->ms.ReturnDRAMBW);
dml_get_var_func(tcalc, dml_float_t, mode_lib->mp.TCalc);
dml_get_var_func(comp_buffer_size_kbytes, dml_uint_t, mode_lib->mp.CompressedBufferSizeInkByte);
dml_get_var_func(pixel_chunk_size_in_kbyte, dml_uint_t, mode_lib->ms.ip.pixel_chunk_size_kbytes);
dml_get_var_func(alpha_pixel_chunk_size_in_kbyte, dml_uint_t, mode_lib->ms.ip.alpha_pixel_chunk_size_kbytes);
dml_get_var_func(meta_chunk_size_in_kbyte, dml_uint_t, mode_lib->ms.ip.meta_chunk_size_kbytes);
dml_get_var_func(min_pixel_chunk_size_in_byte, dml_uint_t, mode_lib->ms.ip.min_pixel_chunk_size_bytes);
dml_get_var_func(min_meta_chunk_size_in_byte, dml_uint_t, mode_lib->ms.ip.min_meta_chunk_size_bytes);
dml_get_var_func(total_immediate_flip_bytes, dml_uint_t, mode_lib->mp.TotImmediateFlipBytes);

dml_get_per_surface_var_func(dsc_delay, dml_uint_t, mode_lib->mp.DSCDelay); // this is the dsc latency
dml_get_per_surface_var_func(dppclk_calculated, dml_float_t, mode_lib->mp.Dppclk_calculated);
dml_get_per_surface_var_func(dscclk_calculated, dml_float_t, mode_lib->mp.DSCCLK_calculated);
dml_get_per_surface_var_func(min_ttu_vblank_in_us, dml_float_t, mode_lib->mp.MinTTUVBlank);
dml_get_per_surface_var_func(vratio_prefetch_l, dml_float_t, mode_lib->mp.VRatioPrefetchY);
dml_get_per_surface_var_func(vratio_prefetch_c, dml_float_t, mode_lib->mp.VRatioPrefetchC);
dml_get_per_surface_var_func(dst_x_after_scaler, dml_uint_t, mode_lib->mp.DSTXAfterScaler);
dml_get_per_surface_var_func(dst_y_after_scaler, dml_uint_t, mode_lib->mp.DSTYAfterScaler);
dml_get_per_surface_var_func(dst_y_per_vm_vblank, dml_float_t, mode_lib->mp.DestinationLinesToRequestVMInVBlank);
dml_get_per_surface_var_func(dst_y_per_row_vblank, dml_float_t, mode_lib->mp.DestinationLinesToRequestRowInVBlank);
dml_get_per_surface_var_func(dst_y_prefetch, dml_float_t, mode_lib->mp.DestinationLinesForPrefetch);
dml_get_per_surface_var_func(dst_y_per_vm_flip, dml_float_t, mode_lib->mp.DestinationLinesToRequestVMInImmediateFlip);
dml_get_per_surface_var_func(dst_y_per_row_flip, dml_float_t, mode_lib->mp.DestinationLinesToRequestRowInImmediateFlip);
dml_get_per_surface_var_func(dst_y_per_pte_row_nom_l, dml_float_t, mode_lib->mp.DST_Y_PER_PTE_ROW_NOM_L);
dml_get_per_surface_var_func(dst_y_per_pte_row_nom_c, dml_float_t, mode_lib->mp.DST_Y_PER_PTE_ROW_NOM_C);
dml_get_per_surface_var_func(dst_y_per_meta_row_nom_l, dml_float_t, mode_lib->mp.DST_Y_PER_META_ROW_NOM_L);
dml_get_per_surface_var_func(dst_y_per_meta_row_nom_c, dml_float_t, mode_lib->mp.DST_Y_PER_META_ROW_NOM_C);
dml_get_per_surface_var_func(refcyc_per_vm_group_vblank_in_us, dml_float_t, mode_lib->mp.TimePerVMGroupVBlank);
dml_get_per_surface_var_func(refcyc_per_vm_group_flip_in_us, dml_float_t, mode_lib->mp.TimePerVMGroupFlip);
dml_get_per_surface_var_func(refcyc_per_vm_req_vblank_in_us, dml_float_t, mode_lib->mp.TimePerVMRequestVBlank);
dml_get_per_surface_var_func(refcyc_per_vm_req_flip_in_us, dml_float_t, mode_lib->mp.TimePerVMRequestFlip);
dml_get_per_surface_var_func(refcyc_per_vm_dmdata_in_us, dml_float_t, mode_lib->mp.Tdmdl_vm);
dml_get_per_surface_var_func(dmdata_dl_delta_in_us, dml_float_t, mode_lib->mp.Tdmdl);
dml_get_per_surface_var_func(refcyc_per_line_delivery_l_in_us, dml_float_t, mode_lib->mp.DisplayPipeLineDeliveryTimeLuma);
dml_get_per_surface_var_func(refcyc_per_line_delivery_c_in_us, dml_float_t, mode_lib->mp.DisplayPipeLineDeliveryTimeChroma);
dml_get_per_surface_var_func(refcyc_per_line_delivery_pre_l_in_us, dml_float_t, mode_lib->mp.DisplayPipeLineDeliveryTimeLumaPrefetch);
dml_get_per_surface_var_func(refcyc_per_line_delivery_pre_c_in_us, dml_float_t, mode_lib->mp.DisplayPipeLineDeliveryTimeChromaPrefetch);
dml_get_per_surface_var_func(refcyc_per_req_delivery_l_in_us, dml_float_t, mode_lib->mp.DisplayPipeRequestDeliveryTimeLuma);
dml_get_per_surface_var_func(refcyc_per_req_delivery_c_in_us, dml_float_t, mode_lib->mp.DisplayPipeRequestDeliveryTimeChroma);
dml_get_per_surface_var_func(refcyc_per_req_delivery_pre_l_in_us, dml_float_t, mode_lib->mp.DisplayPipeRequestDeliveryTimeLumaPrefetch);
dml_get_per_surface_var_func(refcyc_per_req_delivery_pre_c_in_us, dml_float_t, mode_lib->mp.DisplayPipeRequestDeliveryTimeChromaPrefetch);
dml_get_per_surface_var_func(refcyc_per_cursor_req_delivery_in_us, dml_float_t, mode_lib->mp.CursorRequestDeliveryTime);
dml_get_per_surface_var_func(refcyc_per_cursor_req_delivery_pre_in_us, dml_float_t, mode_lib->mp.CursorRequestDeliveryTimePrefetch);
dml_get_per_surface_var_func(refcyc_per_meta_chunk_nom_l_in_us, dml_float_t, mode_lib->mp.TimePerMetaChunkNominal);
dml_get_per_surface_var_func(refcyc_per_meta_chunk_nom_c_in_us, dml_float_t, mode_lib->mp.TimePerChromaMetaChunkNominal);
dml_get_per_surface_var_func(refcyc_per_meta_chunk_vblank_l_in_us, dml_float_t, mode_lib->mp.TimePerMetaChunkVBlank);
dml_get_per_surface_var_func(refcyc_per_meta_chunk_vblank_c_in_us, dml_float_t, mode_lib->mp.TimePerChromaMetaChunkVBlank);
dml_get_per_surface_var_func(refcyc_per_meta_chunk_flip_l_in_us, dml_float_t, mode_lib->mp.TimePerMetaChunkFlip);
dml_get_per_surface_var_func(refcyc_per_meta_chunk_flip_c_in_us, dml_float_t, mode_lib->mp.TimePerChromaMetaChunkFlip);
dml_get_per_surface_var_func(refcyc_per_pte_group_nom_l_in_us, dml_float_t, mode_lib->mp.time_per_pte_group_nom_luma);
dml_get_per_surface_var_func(refcyc_per_pte_group_nom_c_in_us, dml_float_t, mode_lib->mp.time_per_pte_group_nom_chroma);
dml_get_per_surface_var_func(refcyc_per_pte_group_vblank_l_in_us, dml_float_t, mode_lib->mp.time_per_pte_group_vblank_luma);
dml_get_per_surface_var_func(refcyc_per_pte_group_vblank_c_in_us, dml_float_t, mode_lib->mp.time_per_pte_group_vblank_chroma);
dml_get_per_surface_var_func(refcyc_per_pte_group_flip_l_in_us, dml_float_t, mode_lib->mp.time_per_pte_group_flip_luma);
dml_get_per_surface_var_func(refcyc_per_pte_group_flip_c_in_us, dml_float_t, mode_lib->mp.time_per_pte_group_flip_chroma);
dml_get_per_surface_var_func(dpte_group_size_in_bytes, dml_uint_t, mode_lib->mp.dpte_group_bytes);
dml_get_per_surface_var_func(vm_group_size_in_bytes, dml_uint_t, mode_lib->mp.vm_group_bytes);
dml_get_per_surface_var_func(swath_height_l, dml_uint_t, mode_lib->ms.SwathHeightY);
dml_get_per_surface_var_func(swath_height_c, dml_uint_t, mode_lib->ms.SwathHeightC);
dml_get_per_surface_var_func(dpte_row_height_l, dml_uint_t, mode_lib->mp.dpte_row_height);
dml_get_per_surface_var_func(dpte_row_height_c, dml_uint_t, mode_lib->mp.dpte_row_height_chroma);
dml_get_per_surface_var_func(dpte_row_height_linear_l, dml_uint_t, mode_lib->mp.dpte_row_height_linear);
dml_get_per_surface_var_func(dpte_row_height_linear_c, dml_uint_t, mode_lib->mp.dpte_row_height_linear_chroma);
dml_get_per_surface_var_func(meta_row_height_l, dml_uint_t, mode_lib->mp.meta_row_height);
dml_get_per_surface_var_func(meta_row_height_c, dml_uint_t, mode_lib->mp.meta_row_height_chroma);

dml_get_per_surface_var_func(vstartup_calculated, dml_uint_t, mode_lib->mp.VStartup);
dml_get_per_surface_var_func(vupdate_offset, dml_uint_t, mode_lib->mp.VUpdateOffsetPix);
dml_get_per_surface_var_func(vupdate_width, dml_uint_t, mode_lib->mp.VUpdateWidthPix);
dml_get_per_surface_var_func(vready_offset, dml_uint_t, mode_lib->mp.VReadyOffsetPix);
dml_get_per_surface_var_func(vready_at_or_after_vsync, dml_uint_t, mode_lib->mp.VREADY_AT_OR_AFTER_VSYNC);
dml_get_per_surface_var_func(min_dst_y_next_start, dml_uint_t, mode_lib->mp.MIN_DST_Y_NEXT_START);
dml_get_per_surface_var_func(det_stored_buffer_size_l_bytes, dml_uint_t, mode_lib->ms.DETBufferSizeY);
dml_get_per_surface_var_func(det_stored_buffer_size_c_bytes, dml_uint_t, mode_lib->ms.DETBufferSizeC);
dml_get_per_surface_var_func(use_mall_for_static_screen, dml_uint_t, mode_lib->mp.UsesMALLForStaticScreen);
dml_get_per_surface_var_func(surface_size_for_mall, dml_uint_t, mode_lib->mp.SurfaceSizeInTheMALL);
dml_get_per_surface_var_func(dcc_max_uncompressed_block_l, dml_uint_t, mode_lib->mp.DCCYMaxUncompressedBlock);
dml_get_per_surface_var_func(dcc_max_compressed_block_l, dml_uint_t, mode_lib->mp.DCCYMaxCompressedBlock);
dml_get_per_surface_var_func(dcc_independent_block_l, dml_uint_t, mode_lib->mp.DCCYIndependentBlock);
dml_get_per_surface_var_func(dcc_max_uncompressed_block_c, dml_uint_t, mode_lib->mp.DCCCMaxUncompressedBlock);
dml_get_per_surface_var_func(dcc_max_compressed_block_c, dml_uint_t, mode_lib->mp.DCCCMaxCompressedBlock);
dml_get_per_surface_var_func(dcc_independent_block_c, dml_uint_t, mode_lib->mp.DCCCIndependentBlock);
dml_get_per_surface_var_func(max_active_dram_clock_change_latency_supported, dml_uint_t, mode_lib->mp.MaxActiveDRAMClockChangeLatencySupported);
dml_get_per_surface_var_func(pte_buffer_mode, dml_uint_t, mode_lib->mp.PTE_BUFFER_MODE);
dml_get_per_surface_var_func(bigk_fragment_size, dml_uint_t, mode_lib->mp.BIGK_FRAGMENT_SIZE);
dml_get_per_surface_var_func(dpte_bytes_per_row, dml_uint_t, mode_lib->mp.PixelPTEBytesPerRow);
dml_get_per_surface_var_func(meta_bytes_per_row, dml_uint_t, mode_lib->mp.MetaRowByte);
dml_get_per_surface_var_func(det_buffer_size_kbytes, dml_uint_t, mode_lib->ms.DETBufferSizeInKByte);
