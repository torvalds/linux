// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_CORE_DCN5_CALCS_DISPLAY_PIPE_H__
#define __DML2_CORE_DCN5_CALCS_DISPLAY_PIPE_H__
#include "dml2_internal_shared_types.h"

void dcn5_calculate_output_link(
		struct dml2_core_internal_scratch *s,
		double PHYCLK,
		double PHYCLKD18,
		double PHYCLKD32,
		double Downspreading,
		enum dml2_output_encoder_class Output,
		enum dml2_output_format_class OutputFormat,
		unsigned int HTotal,
		unsigned int HActive,
		double PixelClockBackEnd,
		double ForcedOutputLinkBPP,
		unsigned int DSCInputBitPerComponent,
		unsigned int NumberOfDSCSlices,
		double AudioSampleRate,
		unsigned int AudioSampleLayout,
		enum dml2_odm_mode ODMModeNoDSC,
		enum dml2_odm_mode ODMModeDSC,
		enum dml2_dsc_enable_option DSCEnable,
		unsigned int OutputLinkDPLanes,
		enum dml2_output_link_dp_rate OutputLinkDPRate,

		// Output
		bool *RequiresDSC,
		bool *RequiresFEC,
		double *OutBpp,
		enum dml2_core_internal_output_type *OutputType,
		enum dml2_core_internal_output_type_rate *OutputRate,
		unsigned int *RequiredSlots);

void dcn5_calculate_odm_mode(
		unsigned int MaximumPixelsPerLinePerDSCUnit,
		unsigned int HActive,
		enum dml2_output_format_class OutFormat,
		enum dml2_output_encoder_class Output,
		enum dml2_odm_mode ODMUse,
		double MaxDispclk,
		bool DSCEnable,
		unsigned int TotalNumberOfActiveDPP,
		unsigned int MaxNumDPP,
		double PixelClock,
		unsigned int MaximumSlicesPerDSCUnit,
		unsigned int NumberOfDSCSlices,
		unsigned int odm_combine_support_mask,

		// Output
		bool *TotalAvailablePipesSupport,
		unsigned int *NumberOfDPP,
		enum dml2_odm_mode *ODMMode,
		double *RequiredDISPCLKPerSurface);

double dcn5_calculate_required_dtbclk(
		bool DSCEnable,
		double PixelClock,
		enum dml2_output_format_class OutputFormat,
		double OutputBpp,
		unsigned int DSCSlices,
		unsigned int HTotal,
		unsigned int HActive,
		unsigned int AudioRate,
		unsigned int AudioLayout);

double dcn5_calculate_required_dispclk(
		enum dml2_odm_mode ODMMode,
		double PixelClock,
		bool isTMDS420);

double dcn5_calculate_write_back_dispclk(
		enum dml2_source_format_class WritebackPixelFormat,
		double PixelClock,
		enum dml2_odm_mode ODMMode,
		double WritebackHRatio,
		double WritebackVRatio,
		unsigned int WritebackHTaps,
		unsigned int WritebackVTaps,
		unsigned int WritebackHTapsChroma,
		unsigned int WritebackVTapsChroma,
		unsigned int WritebackSourceWidth,
		unsigned int WritebackDestinationWidth,
		unsigned int HTotal,
		unsigned int WritebackLineBufferSize);

unsigned int dcn5_calculate_dsc_delay_requirement(
		bool DSCEnabled,
		enum dml2_odm_mode ODMMode,
		unsigned int DSCInputBitPerComponent,
		double OutputBpp,
		unsigned int HActive,
		unsigned int HTotal,
		unsigned int NumberOfDSCSlices,
		enum dml2_output_format_class OutputFormat,
		enum dml2_output_encoder_class Output,
		double PixelClock,
		double PixelClockBackEnd,
		bool use_legacy_dsc_delay_formula);

void dcn5_calculate_single_pipe_dppclk_and_scl_throughput(
		double HRatio,
		double HRatioChroma,
		double VRatio,
		double VRatioChroma,
		double MaxDCHUBToPSCLThroughput,
		double MaxPSCLToLBThroughput,
		double PixelClock,
		enum dml2_source_format_class SourcePixelFormat,
		unsigned int HTaps,
		unsigned int HTapsChroma,
		unsigned int VTaps,
		unsigned int VTapsChroma,

		// Output
		double *PSCL_THROUGHPUT,
		double *PSCL_THROUGHPUT_CHROMA,
		double *DPPCLKUsingSingleDPP);

void dcn5_calculate_pixel_delivery_times(
		const struct dml2_display_cfg *display_cfg,
		unsigned int NoOfDPP[DML2_MAX_PLANES],
		unsigned int NumberOfActiveSurfaces,
		double VRatioPrefetchY[],
		double VRatioPrefetchC[],
		unsigned int swath_width_luma_ub[],
		unsigned int swath_width_chroma_ub[],
		double PSCL_THROUGHPUT[],
		double PSCL_THROUGHPUT_CHROMA[],
		double Dppclk[],
		double DCFCLKDeepSleep,
		unsigned int BytePerPixelY[],
		unsigned int BytePerPixelC[],
		unsigned int req_per_swath_ub_l[],
		unsigned int req_per_swath_ub_c[],

		// Output
		double DisplayPipeLineDeliveryTimeLuma[],
		double DisplayPipeLineDeliveryTimeChroma[],
		double DisplayPipeLineDeliveryTimeLumaPrefetch[],
		double DisplayPipeLineDeliveryTimeChromaPrefetch[],
		double DisplayPipeRequestDeliveryTimeLuma[],
		double DisplayPipeRequestDeliveryTimeChroma[],
		double DisplayPipeRequestDeliveryTimeLumaPrefetch[],
		double DisplayPipeRequestDeliveryTimeChromaPrefetch[]);

#endif /* __DML2_CORE_DCN5_CALCS_DISPLAY_PIPE_H__ */
