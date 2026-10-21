// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_core_dcn5_calcs_display_pipe.h"
#include "dml2_core_utils.h"
#include "dml2_core_dcn5_calcs_dsc_latency.h"

#define DML2_MAX_FMT_420_BUFFER_WIDTH 4096

static double dcn5_trunc_to_valid_bpp(
		struct dml2_core_shared_TruncToValidBPP_locals *l,
		double LinkBitRate,
		unsigned int Lanes,
		unsigned int HTotal,
		unsigned int HActive,
		double PixelClock,
		double DesiredBPP,
		bool DSCEnable,
		enum dml2_output_encoder_class Output,
		enum dml2_output_format_class Format,
		unsigned int DSCInputBitPerComponent,
		unsigned int DSCSlices,
		unsigned int AudioRate,
		unsigned int AudioLayout,
		enum dml2_odm_mode ODMModeNoDSC,
		enum dml2_odm_mode ODMModeDSC,

		// Output
		unsigned int *RequiredSlots)
{
	(void)DSCInputBitPerComponent;
	(void)RequiredSlots;
	double MaxLinkBPP;
	unsigned int MinDSCBPP;
	double MaxDSCBPP;
	unsigned int NonDSCBPP0;
	unsigned int NonDSCBPP1;
	unsigned int NonDSCBPP2;
	enum dml2_odm_mode ODMMode;
	unsigned int slice_width = (int)math_ceil2((double)HActive / DSCSlices, 1.0);

	enum lib_frl_cap_check_status hdmifrlresult = LIB_FRL_CAP_CHECK_OK;

	l->hdmifrlparams.lanes = (int)Lanes;
	l->hdmifrlparams.f_pixel_clock_nominal = PixelClock * 1000000;
	l->hdmifrlparams.r_bit_nominal = LinkBitRate * 1000000;
	l->hdmifrlparams.layout = (int)AudioLayout;
	l->hdmifrlparams.f_audio = AudioRate * 1000;
	l->hdmifrlparams.h_active = (int)HActive;
	l->hdmifrlparams.h_blank = (int)(HTotal - HActive);
	l->hdmifrlparams.bpc = (int)(DesiredBPP / 3);
	l->hdmifrlparams.compressed = DSCEnable;
	l->hdmifrlparams.slices = (int)DSCSlices;
	l->hdmifrlparams.slice_width = slice_width;
	l->hdmifrlparams.bpp_target = DesiredBPP;

	if (Format == dml2_420) {
		NonDSCBPP0 = 12;
		NonDSCBPP1 = 15;
		NonDSCBPP2 = 18;
		MinDSCBPP = 6;
		MaxDSCBPP = 16;
		l->hdmifrlparams.pixel_encoding = LIB_FRL_CAP_CHECK_PIXEL_ENCODING_420;
		l->hdmifrlparams.bpc = (int)(DesiredBPP / 1.5);
	} else if (Format == dml2_444) {
		NonDSCBPP0 = 24;
		NonDSCBPP1 = 30;
		NonDSCBPP2 = 36;
		MinDSCBPP = 8;
		MaxDSCBPP = 16;
		l->hdmifrlparams.pixel_encoding = LIB_FRL_CAP_CHECK_PIXEL_ENCODING_444;
		l->hdmifrlparams.bpc = (int)(DesiredBPP / 3.0);
	} else {
		l->hdmifrlparams.pixel_encoding = LIB_FRL_CAP_CHECK_PIXEL_ENCODING_422;
		l->hdmifrlparams.bpc = (int)(DesiredBPP / 2.0);
		if (Output == dml2_hdmi || Output == dml2_hdmifrl) {
			NonDSCBPP0 = 24;
			NonDSCBPP1 = 24;
			NonDSCBPP2 = 24;
		} else {
			NonDSCBPP0 = 16;
			NonDSCBPP1 = 20;
			NonDSCBPP2 = 24;
		}
		if (Format == dml2_n422 || Output == dml2_hdmifrl) {
			MinDSCBPP = 7;
			MaxDSCBPP = 16;
		} else {
			MinDSCBPP = 8;
			MaxDSCBPP = 16;
		}
	}

	if (Output == dml2_hdmifrl) {
		hdmifrlresult = frl_cap_check_intermediates(&l->hdmifrlparams, &l->hdmifrlinter);
		MaxLinkBPP = (1 - l->hdmifrlinter.overhead_max) * math_min2(l->hdmifrlinter.r_frl_char_min * 16.0 * (double)Lanes / l->hdmifrlinter.f_pixel_clock_max + 24.0 * (double)DML2_FRL_CHK_TB_BORROWED_MAX / (double)HActive,
				(l->hdmifrlinter.r_frl_char_min * 16.0 * (double)Lanes / l->hdmifrlinter.f_pixel_clock_max * (double)HTotal - 16.0 * (double)l->hdmifrlinter.blank_audio_min) / (double)HActive);
	} else if (DSCEnable && Output == dml2_dp2p0) {
		MaxLinkBPP = LinkBitRate * Lanes / PixelClock * 128.0 / 132.0 * 383.0 / 384.0 * 65536.0 / 65540.0;
		MaxLinkBPP = math_floor2(MaxLinkBPP * slice_width - 128.0, 128.0) / slice_width;
	} else if (Output == dml2_dp2p0) {
		MaxLinkBPP = LinkBitRate * Lanes / PixelClock * 128.0 / 132.0 * 383.0 / 384.0 * 65536.0 / 65540.0;
	} else if (DSCEnable && Output == dml2_dp) {
		MaxLinkBPP = LinkBitRate / 10.0 * 8.0 * Lanes / PixelClock * (1 - 2.4 / 100);
		MaxLinkBPP = math_floor2(MaxLinkBPP * slice_width - 8.0 * Lanes, 8.0 * Lanes) / slice_width;
	} else {
		MaxLinkBPP = LinkBitRate / 10.0 * 8.0 * Lanes / PixelClock;
	}

	ODMMode = DSCEnable ? ODMModeDSC : ODMModeNoDSC;

	if (ODMMode == dml2_odm_mode_split_1to2) {
		MaxLinkBPP = 2 * MaxLinkBPP;
	}

	if (DesiredBPP == 0) {
		if (DSCEnable) {
			if (MaxLinkBPP < MinDSCBPP) {
				return __DML2_CALCS_DPP_INVALID__;
			} else if (MaxLinkBPP >= MaxDSCBPP) {
				return MaxDSCBPP;
			} else {
				return math_floor2(16.0 * MaxLinkBPP, 1.0) / 16.0;
			}
		} else {
			if (MaxLinkBPP >= NonDSCBPP2) {
				return NonDSCBPP2;
			} else if (MaxLinkBPP >= NonDSCBPP1) {
				return NonDSCBPP1;
			} else if (MaxLinkBPP >= NonDSCBPP0) {
				return NonDSCBPP0;
			} else {
				return __DML2_CALCS_DPP_INVALID__;
			}
		}
	} else {
		if (!((DSCEnable == false && (DesiredBPP == NonDSCBPP2 || DesiredBPP == NonDSCBPP1 || DesiredBPP == NonDSCBPP0)) ||
				(DSCEnable && DesiredBPP >= MinDSCBPP && DesiredBPP <= MaxDSCBPP))) {
			return __DML2_CALCS_DPP_INVALID__;
		} else if ((Output == dml2_hdmifrl && hdmifrlresult != LIB_FRL_CAP_CHECK_OK) || (Output != dml2_hdmifrl && MaxLinkBPP < DesiredBPP)) {
			return __DML2_CALCS_DPP_INVALID__;
		} else {
			return DesiredBPP;
		}
	}
}

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
		unsigned int *RequiredSlots)
{
	bool LinkDSCEnable;
	unsigned int dummy;
	*RequiresDSC = false;
	*RequiresFEC = false;
	*OutBpp = 0;

	*OutputType = dml2_core_internal_output_type_unknown;
	*OutputRate = dml2_core_internal_output_rate_unknown;

#ifdef __DML_VBA_DEBUG__
	DML_LOG_VERBOSE("DML::%s: DSCEnable = %u (dis, en, en_if_necessary)\n", __func__, DSCEnable);
	DML_LOG_VERBOSE("DML::%s: PHYCLK = %f\n", __func__, PHYCLK);
	DML_LOG_VERBOSE("DML::%s: PixelClockBackEnd = %f\n", __func__, PixelClockBackEnd);
	DML_LOG_VERBOSE("DML::%s: AudioSampleRate = %f\n", __func__, AudioSampleRate);
	DML_LOG_VERBOSE("DML::%s: HActive = %u\n", __func__, HActive);
	DML_LOG_VERBOSE("DML::%s: HTotal = %u\n", __func__, HTotal);
	DML_LOG_VERBOSE("DML::%s: ODMModeNoDSC = %u\n", __func__, ODMModeNoDSC);
	DML_LOG_VERBOSE("DML::%s: ODMModeDSC = %u\n", __func__, ODMModeDSC);
	DML_LOG_VERBOSE("DML::%s: ForcedOutputLinkBPP = %f\n", __func__, ForcedOutputLinkBPP);
	DML_LOG_VERBOSE("DML::%s: Output (encoder) = %u\n", __func__, Output);
	DML_LOG_VERBOSE("DML::%s: OutputLinkDPRate = %u\n", __func__, OutputLinkDPRate);
#endif
	{
		if (Output == dml2_hdmi) {
			*RequiresDSC = false;
			*RequiresFEC = false;
			*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, math_min2(600, PHYCLK) * 10, 3, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, false, Output,
					OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
			//OutputTypeAndRate = "HDMI";
			*OutputType = dml2_core_internal_output_type_hdmi;
		} else if (Output == dml2_dp || Output == dml2_dp2p0 || Output == dml2_edp) {
			if (DSCEnable == dml2_dsc_enable) {
				*RequiresDSC = true;
				LinkDSCEnable = true;
				if (Output == dml2_dp || Output == dml2_dp2p0) {
					*RequiresFEC = true;
				} else {
					*RequiresFEC = false;
				}
			} else {
				*RequiresDSC = false;
				LinkDSCEnable = false;
				if (Output == dml2_dp2p0) {
					*RequiresFEC = true;
				} else {
					*RequiresFEC = false;
				}
			}
			if (Output == dml2_dp2p0) {
				*OutBpp = 0;
				if ((OutputLinkDPRate == dml2_dp_rate_na || OutputLinkDPRate == dml2_dp_rate_uhbr10) && PHYCLKD32 >= 10000.0 / 32) {
					*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 10000, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
							OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					if (*OutBpp == 0 && PHYCLKD32 < 13500.0 / 32 && DSCEnable == dml2_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 10000, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
								OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " UHBR10";
					*OutputType = dml2_core_internal_output_type_dp2p0;
					*OutputRate = dml2_core_internal_output_rate_dp_rate_uhbr10;
				}
				if ((OutputLinkDPRate == dml2_dp_rate_na || OutputLinkDPRate == dml2_dp_rate_uhbr13p5) && *OutBpp == 0 && PHYCLKD32 >= 13500.0 / 32) {
					*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 13500, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
							OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);

					if (*OutBpp == 0 && PHYCLKD32 < 20000.0 / 32 && DSCEnable == dml2_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 13500, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
								OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " UHBR13p5";
					*OutputType = dml2_core_internal_output_type_dp2p0;
					*OutputRate = dml2_core_internal_output_rate_dp_rate_uhbr13p5;
				}
				if ((OutputLinkDPRate == dml2_dp_rate_na || OutputLinkDPRate == dml2_dp_rate_uhbr20) && *OutBpp == 0 && PHYCLKD32 >= 20000.0 / 32) {
					*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 20000, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
							OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					if (*OutBpp == 0 && DSCEnable == dml2_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 20000, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
								OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " UHBR20";
					*OutputType = dml2_core_internal_output_type_dp2p0;
					*OutputRate = dml2_core_internal_output_rate_dp_rate_uhbr20;
				}
			} else { // output is dp or edp
				*OutBpp = 0;
				if ((OutputLinkDPRate == dml2_dp_rate_na || OutputLinkDPRate == dml2_dp_rate_hbr) && PHYCLK >= 270) {
					*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 2700, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
							OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					if (*OutBpp == 0 && PHYCLK < 540 && DSCEnable == dml2_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						if (Output == dml2_dp) {
							*RequiresFEC = true;
						}
						*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 2700, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
								OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " HBR";
					*OutputType = (Output == dml2_dp) ? dml2_core_internal_output_type_dp : dml2_core_internal_output_type_edp;
					*OutputRate = dml2_core_internal_output_rate_dp_rate_hbr;
				}
				if ((OutputLinkDPRate == dml2_dp_rate_na || OutputLinkDPRate == dml2_dp_rate_hbr2) && *OutBpp == 0 && PHYCLK >= 540) {
					*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 5400, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
							OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);

					if (*OutBpp == 0 && PHYCLK < 810 && DSCEnable == dml2_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						if (Output == dml2_dp) {
							*RequiresFEC = true;
						}
						*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 5400, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
								OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " HBR2";
					*OutputType = (Output == dml2_dp) ? dml2_core_internal_output_type_dp : dml2_core_internal_output_type_edp;
					*OutputRate = dml2_core_internal_output_rate_dp_rate_hbr2;
				}
				if ((OutputLinkDPRate == dml2_dp_rate_na || OutputLinkDPRate == dml2_dp_rate_hbr3) && *OutBpp == 0 && PHYCLK >= 810) { // VBA_ERROR, vba code doesn't have hbr3 check
					*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 8100, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
							OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);

					if (*OutBpp == 0 && DSCEnable == dml2_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
						*RequiresDSC = true;
						LinkDSCEnable = true;
						if (Output == dml2_dp) {
							*RequiresFEC = true;
						}
						*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, (1 - Downspreading / 100) * 8100, OutputLinkDPLanes, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output,
								OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, RequiredSlots);
					}
					//OutputTypeAndRate = Output & " HBR3";
					*OutputType = (Output == dml2_dp) ? dml2_core_internal_output_type_dp : dml2_core_internal_output_type_edp;
					*OutputRate = dml2_core_internal_output_rate_dp_rate_hbr3;
				}
			}
		} else if (Output == dml2_hdmifrl) {
			if (DSCEnable == dml2_dsc_enable) {
				*RequiresDSC = true;
				LinkDSCEnable = true;
				*RequiresFEC = true;
			} else {
				*RequiresDSC = false;
				LinkDSCEnable = false;
				*RequiresFEC = false;
			}
			*OutBpp = 0;
			if (PHYCLKD18 >= 3000.0 / 18) {
				*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, 3000, 3, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				//OutputTypeAndRate = Output & "3x3";
				*OutputType = dml2_core_internal_output_type_hdmifrl;
				*OutputRate = dml2_core_internal_output_rate_hdmi_rate_3x3;
			}
			if (*OutBpp == 0 && PHYCLKD18 >= 6000.0 / 18) {
				*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, 6000, 3, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				//OutputTypeAndRate = Output & "6x3";
				*OutputType = dml2_core_internal_output_type_hdmifrl;
				*OutputRate = dml2_core_internal_output_rate_hdmi_rate_6x3;
			}
			if (*OutBpp == 0 && PHYCLKD18 >= 6000.0 / 18) {
				*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, 6000, 4, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				//OutputTypeAndRate = Output & "6x4";
				*OutputType = dml2_core_internal_output_type_hdmifrl;
				*OutputRate = dml2_core_internal_output_rate_hdmi_rate_6x4;
			}
			if (*OutBpp == 0 && PHYCLKD18 >= 8000.0 / 18) {
				*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, 8000, 4, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				//OutputTypeAndRate = Output & "8x4";
				*OutputType = dml2_core_internal_output_type_hdmifrl;
				*OutputRate = dml2_core_internal_output_rate_hdmi_rate_8x4;
			}
			if (*OutBpp == 0 && PHYCLKD18 >= 10000.0 / 18) {
				*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, 10000, 4, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				//OutputTypeAndRate = Output & "10x4";
				*OutputType = dml2_core_internal_output_type_hdmifrl;
				*OutputRate = dml2_core_internal_output_rate_hdmi_rate_10x4;
			}
			if (*OutBpp == 0 && PHYCLKD18 >= 12000.0 / 18) {
				*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, 12000, 4, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				//OutputTypeAndRate = Output & "12x4";
				*OutputType = dml2_core_internal_output_type_hdmifrl;
				*OutputRate = dml2_core_internal_output_rate_hdmi_rate_12x4;
			}
			if (*OutBpp == 0 && PHYCLKD18 >= 16000.0 / 18) {
				*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, 16000, 4, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				//OutputTypeAndRate = Output & "16x4";
				*OutputType = dml2_core_internal_output_type_hdmifrl;
				*OutputRate = dml2_core_internal_output_rate_hdmi_rate_16x4;
			}
			if (*OutBpp == 0 && PHYCLKD18 >= 20000.0 / 18) {
				*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, 20000, 4, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				if (*OutBpp == 0 && DSCEnable == dml2_dsc_enable_if_necessary && ForcedOutputLinkBPP == 0) {
					*RequiresDSC = true;
					LinkDSCEnable = true;
					*RequiresFEC = true;
					*OutBpp = dcn5_trunc_to_valid_bpp(&s->TruncToValidBPP_locals, 20000, 4, HTotal, HActive, PixelClockBackEnd, ForcedOutputLinkBPP, LinkDSCEnable, Output, OutputFormat, DSCInputBitPerComponent, NumberOfDSCSlices, (unsigned int)AudioSampleRate, AudioSampleLayout, ODMModeNoDSC, ODMModeDSC, &dummy);
				}
				//OutputTypeAndRate = Output & "20x4";
				*OutputType = dml2_core_internal_output_type_hdmifrl;
				*OutputRate = dml2_core_internal_output_rate_hdmi_rate_20x4;
			}
		}
	}
#ifdef __DML_VBA_DEBUG__
	DML_LOG_VERBOSE("DML::%s: RequiresDSC = %u\n", __func__, *RequiresDSC);
	DML_LOG_VERBOSE("DML::%s: RequiresFEC = %u\n", __func__, *RequiresFEC);
	DML_LOG_VERBOSE("DML::%s: OutBpp = %f\n", __func__, *OutBpp);
#endif
}


static enum dml2_odm_mode dcn5_decide_odm_mode(unsigned int HActive,
		double MaxDispclk,
		unsigned int MaximumPixelsPerLinePerDSCUnit,
		enum dml2_output_format_class OutFormat,
		bool UseDSC,
		unsigned int MaximumSlicesPerDSCUnit,
		unsigned int NumberOfDSCSlices,
		double SurfaceRequiredDISPCLKWithoutODMCombine,
		double SurfaceRequiredDISPCLKWithODMCombineTwoToOne,
		double SurfaceRequiredDISPCLKWithODMCombineThreeToOne,
		double SurfaceRequiredDISPCLKWithODMCombineFourToOne)
{
	(void)SurfaceRequiredDISPCLKWithODMCombineFourToOne;
	enum dml2_odm_mode MinimumRequiredODMModeForMaxDispClock;
	enum dml2_odm_mode MinimumRequiredODMModeForMaxDSCHActive;
	enum dml2_odm_mode MinimumRequiredODMModeForMax420HActive;
	enum dml2_odm_mode ODMMode = dml2_odm_mode_bypass;

	MinimumRequiredODMModeForMaxDispClock =
			(SurfaceRequiredDISPCLKWithoutODMCombine <= MaxDispclk) ? dml2_odm_mode_bypass :
					(SurfaceRequiredDISPCLKWithODMCombineTwoToOne <= MaxDispclk) ? dml2_odm_mode_combine_2to1 :
							(SurfaceRequiredDISPCLKWithODMCombineThreeToOne <= MaxDispclk) ? dml2_odm_mode_combine_3to1 : dml2_odm_mode_combine_4to1;
	if (ODMMode < MinimumRequiredODMModeForMaxDispClock)
		ODMMode = MinimumRequiredODMModeForMaxDispClock;

	if (UseDSC) {
		MinimumRequiredODMModeForMaxDSCHActive =
				(HActive <= 1 * MaximumPixelsPerLinePerDSCUnit) ? dml2_odm_mode_bypass :
						(HActive <= 2 * MaximumPixelsPerLinePerDSCUnit) ? dml2_odm_mode_combine_2to1 :
								(HActive <= 3 * MaximumPixelsPerLinePerDSCUnit) ? dml2_odm_mode_combine_3to1 : dml2_odm_mode_combine_4to1;
		if (ODMMode < MinimumRequiredODMModeForMaxDSCHActive)
			ODMMode = MinimumRequiredODMModeForMaxDSCHActive;
	}

	if (OutFormat == dml2_420) {
		MinimumRequiredODMModeForMax420HActive =
				(HActive <= 1 * DML2_MAX_FMT_420_BUFFER_WIDTH) ? dml2_odm_mode_bypass :
						(HActive <= 2 * DML2_MAX_FMT_420_BUFFER_WIDTH) ? dml2_odm_mode_combine_2to1 :
								(HActive <= 3 * DML2_MAX_FMT_420_BUFFER_WIDTH) ? dml2_odm_mode_combine_3to1 : dml2_odm_mode_combine_4to1;
		if (ODMMode < MinimumRequiredODMModeForMax420HActive)
			ODMMode = MinimumRequiredODMModeForMax420HActive;
	}

	if (UseDSC) {
		if (ODMMode == dml2_odm_mode_bypass && NumberOfDSCSlices > MaximumSlicesPerDSCUnit)
			ODMMode = dml2_odm_mode_combine_2to1;
		if (ODMMode == dml2_odm_mode_combine_2to1 && NumberOfDSCSlices > MaximumSlicesPerDSCUnit * 2)
			ODMMode = dml2_odm_mode_combine_3to1;
		if (ODMMode == dml2_odm_mode_combine_3to1 && NumberOfDSCSlices > MaximumSlicesPerDSCUnit * 3)
			ODMMode = dml2_odm_mode_combine_4to1;
		if (ODMMode == dml2_odm_mode_combine_3to1 && (NumberOfDSCSlices % 2 == 0) && (NumberOfDSCSlices % 3 != 0))
			ODMMode = dml2_odm_mode_combine_4to1;
	}

	return ODMMode;
}

static void dcn5_calculate_odm_constraints(
		enum dml2_odm_mode ODMUse,
		double SurfaceRequiredDISPCLKWithoutODMCombine,
		double SurfaceRequiredDISPCLKWithODMCombineTwoToOne,
		double SurfaceRequiredDISPCLKWithODMCombineThreeToOne,
		double SurfaceRequiredDISPCLKWithODMCombineFourToOne,
		unsigned int MaximumPixelsPerLinePerDSCUnit,
		unsigned int MaximumSlicesPerDSCUnit,
		/* Output */
		double *DISPCLKRequired,
		unsigned int *NumberOfDPPRequired,
		unsigned int *MaxHActiveForDSC,
		unsigned int *MaxDSCSlices,
		unsigned int *MaxHActiveFor420)
{
	switch (ODMUse) {
	case dml2_odm_mode_combine_2to1:
		*DISPCLKRequired = SurfaceRequiredDISPCLKWithODMCombineTwoToOne;
		*NumberOfDPPRequired = 2;
		break;
	case dml2_odm_mode_combine_3to1:
		*DISPCLKRequired = SurfaceRequiredDISPCLKWithODMCombineThreeToOne;
		*NumberOfDPPRequired = 3;
		break;
	case dml2_odm_mode_combine_4to1:
		*DISPCLKRequired = SurfaceRequiredDISPCLKWithODMCombineFourToOne;
		*NumberOfDPPRequired = 4;
		break;
	case dml2_odm_mode_auto:
	case dml2_odm_mode_split_1to2:
	case dml2_odm_mode_mso_1to2:
	case dml2_odm_mode_mso_1to4:
	case dml2_odm_mode_bypass:
	default:
		*DISPCLKRequired = SurfaceRequiredDISPCLKWithoutODMCombine;
		*NumberOfDPPRequired = 1;
		break;
	}
	*MaxDSCSlices = *NumberOfDPPRequired * MaximumSlicesPerDSCUnit;
	*MaxHActiveForDSC = *NumberOfDPPRequired * MaximumPixelsPerLinePerDSCUnit;
	*MaxHActiveFor420 = *NumberOfDPPRequired * DML2_MAX_FMT_420_BUFFER_WIDTH;
}

static bool dcn5_validate_odm_mode(enum dml2_odm_mode ODMMode,
		double MaxDispclk,
		unsigned int HActive,
		enum dml2_output_format_class OutFormat,
		bool UseDSC,
		unsigned int NumberOfDSCSlices,
		unsigned int TotalNumberOfActiveDPP,
		unsigned int MaxNumDPP,
		double DISPCLKRequired,
		unsigned int NumberOfDPPRequired,
		unsigned int MaxHActiveForDSC,
		unsigned int MaxDSCSlices,
		unsigned int MaxHActiveFor420,
		unsigned int odm_combine_support_mask)
{
	bool are_odm_segments_symmetrical = (ODMMode == dml2_odm_mode_combine_3to1) ? UseDSC : true;
	unsigned int pixels_per_clock_cycle = (OutFormat == dml2_420 || OutFormat == dml2_n422) ? 2 : 1;
	unsigned int h_timing_div_mode =
			(ODMMode == dml2_odm_mode_combine_4to1 || ODMMode == dml2_odm_mode_combine_3to1) ? 4 :
					(ODMMode == dml2_odm_mode_combine_2to1) ? 2 : pixels_per_clock_cycle;

	if (DISPCLKRequired > MaxDispclk)
		return false;
	if ((TotalNumberOfActiveDPP + NumberOfDPPRequired) > MaxNumDPP)
		return false;
	if (are_odm_segments_symmetrical) {
		if (HActive % (NumberOfDPPRequired * pixels_per_clock_cycle))
			return false;
	}
	if (HActive % h_timing_div_mode)
		/*
		 * TODO - OTG_H_TOTAL, OTG_H_BLANK_START/END and
		 * OTG_H_SYNC_A_START/END all need to be visible by h timing div
		 * mode. This logic only checks H active.
		 */
		return false;

	if (UseDSC) {
		if (HActive > MaxHActiveForDSC)
			return false;
		if (NumberOfDSCSlices > MaxDSCSlices)
			return false;
		if (HActive % NumberOfDSCSlices)
			return false;
		if (NumberOfDSCSlices % NumberOfDPPRequired)
			return false;
		if (ODMMode == dml2_odm_mode_combine_3to1) {
			if ((NumberOfDSCSlices % 2 == 0) && (NumberOfDSCSlices % 3 != 0))
				return false;
		}
	}

	if (OutFormat == dml2_420) {
		if (HActive > MaxHActiveFor420)
			return false;
	}

	return !!(odm_combine_support_mask & ODMMode);
}

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
		double *RequiredDISPCLKPerSurface)
{
	double SurfaceRequiredDISPCLKWithoutODMCombine;
	double SurfaceRequiredDISPCLKWithODMCombineTwoToOne;
	double SurfaceRequiredDISPCLKWithODMCombineThreeToOne;
	double SurfaceRequiredDISPCLKWithODMCombineFourToOne;
	double DISPCLKRequired;
	unsigned int NumberOfDPPRequired;
	unsigned int MaxHActiveForDSC;
	unsigned int MaxDSCSlices;
	unsigned int MaxHActiveFor420;
	bool success;
	bool UseDSC = DSCEnable && (NumberOfDSCSlices > 0);
	enum dml2_odm_mode DecidedODMMode;
	bool isTMDS420 = (OutFormat == dml2_420 && Output == dml2_hdmi);

	SurfaceRequiredDISPCLKWithoutODMCombine = dcn5_calculate_required_dispclk(dml2_odm_mode_bypass, PixelClock, isTMDS420);
	SurfaceRequiredDISPCLKWithODMCombineTwoToOne = dcn5_calculate_required_dispclk(dml2_odm_mode_combine_2to1, PixelClock, isTMDS420);
	SurfaceRequiredDISPCLKWithODMCombineThreeToOne = dcn5_calculate_required_dispclk(dml2_odm_mode_combine_3to1, PixelClock, isTMDS420);
	SurfaceRequiredDISPCLKWithODMCombineFourToOne = dcn5_calculate_required_dispclk(dml2_odm_mode_combine_4to1, PixelClock, isTMDS420);
#ifdef __DML_VBA_DEBUG__
	DML_LOG_VERBOSE("DML::%s: ODMUse = %d\n", __func__, ODMUse);
	DML_LOG_VERBOSE("DML::%s: Output = %d\n", __func__, Output);
	DML_LOG_VERBOSE("DML::%s: DSCEnable = %d\n", __func__, DSCEnable);
	DML_LOG_VERBOSE("DML::%s: MaxDispclk = %f\n", __func__, MaxDispclk);
	DML_LOG_VERBOSE("DML::%s: MaximumPixelsPerLinePerDSCUnit = %d\n", __func__, MaximumPixelsPerLinePerDSCUnit);
	DML_LOG_VERBOSE("DML::%s: SurfaceRequiredDISPCLKWithoutODMCombine = %f\n", __func__, SurfaceRequiredDISPCLKWithoutODMCombine);
	DML_LOG_VERBOSE("DML::%s: SurfaceRequiredDISPCLKWithODMCombineTwoToOne = %f\n", __func__, SurfaceRequiredDISPCLKWithODMCombineTwoToOne);
	DML_LOG_VERBOSE("DML::%s: SurfaceRequiredDISPCLKWithODMCombineThreeToOne = %f\n", __func__, SurfaceRequiredDISPCLKWithODMCombineThreeToOne);
	DML_LOG_VERBOSE("DML::%s: SurfaceRequiredDISPCLKWithODMCombineFourToOne = %f\n", __func__, SurfaceRequiredDISPCLKWithODMCombineFourToOne);
#endif
	if (ODMUse == dml2_odm_mode_auto)
		DecidedODMMode = dcn5_decide_odm_mode(HActive,
				MaxDispclk,
				MaximumPixelsPerLinePerDSCUnit,
				OutFormat,
				UseDSC,
				MaximumSlicesPerDSCUnit,
				NumberOfDSCSlices,
				SurfaceRequiredDISPCLKWithoutODMCombine,
				SurfaceRequiredDISPCLKWithODMCombineTwoToOne,
				SurfaceRequiredDISPCLKWithODMCombineThreeToOne,
				SurfaceRequiredDISPCLKWithODMCombineFourToOne);
	else
		DecidedODMMode = ODMUse;
	dcn5_calculate_odm_constraints(DecidedODMMode,
			SurfaceRequiredDISPCLKWithoutODMCombine,
			SurfaceRequiredDISPCLKWithODMCombineTwoToOne,
			SurfaceRequiredDISPCLKWithODMCombineThreeToOne,
			SurfaceRequiredDISPCLKWithODMCombineFourToOne,
			MaximumPixelsPerLinePerDSCUnit,
			MaximumSlicesPerDSCUnit,
			&DISPCLKRequired,
			&NumberOfDPPRequired,
			&MaxHActiveForDSC,
			&MaxDSCSlices,
			&MaxHActiveFor420);
	success = dcn5_validate_odm_mode(DecidedODMMode,
			MaxDispclk,
			HActive,
			OutFormat,
			UseDSC,
			NumberOfDSCSlices,
			TotalNumberOfActiveDPP,
			MaxNumDPP,
			DISPCLKRequired,
			NumberOfDPPRequired,
			MaxHActiveForDSC,
			MaxDSCSlices,
			MaxHActiveFor420,
			odm_combine_support_mask);

	*ODMMode = DecidedODMMode;
	*TotalAvailablePipesSupport = success;
	*NumberOfDPP = NumberOfDPPRequired;
	*RequiredDISPCLKPerSurface = success ? DISPCLKRequired : 0;
#ifdef __DML_VBA_DEBUG__
	DML_LOG_VERBOSE("DML::%s: ODMMode = %d\n", __func__, *ODMMode);
	DML_LOG_VERBOSE("DML::%s: NumberOfDPP = %d\n", __func__, *NumberOfDPP);
	DML_LOG_VERBOSE("DML::%s: TotalAvailablePipesSupport = %d\n", __func__, *TotalAvailablePipesSupport);
	DML_LOG_VERBOSE("DML::%s: RequiredDISPCLKPerSurface = %f\n", __func__, *RequiredDISPCLKPerSurface);
#endif
}

double dcn5_calculate_required_dtbclk(
		bool DSCEnable,
		double PixelClock,
		enum dml2_output_format_class OutputFormat,
		double OutputBpp,
		unsigned int DSCSlices,
		unsigned int HTotal,
		unsigned int HActive,
		unsigned int AudioRate,
		unsigned int AudioLayout)
{
	if (DSCEnable != true) {
		return math_max2(PixelClock / 4.0 * OutputBpp / 24.0, 25.0);
	} else {
		double PixelWordRate = PixelClock / (OutputFormat == dml2_444 ? 1 : 2);
		double HCActive = math_ceil2(DSCSlices * math_ceil2(OutputBpp * math_ceil2(HActive / DSCSlices, 1) / 8.0, 1) / 3.0, 1);
		double HCBlank = 64 + 32 * math_ceil2(AudioRate * (AudioLayout == 1 ? 1 : 0.25) * HTotal / (PixelClock * 1000), 1);
		double AverageTribyteRate = PixelWordRate * (HCActive + HCBlank) / HTotal;
		double HActiveTribyteRate = PixelWordRate * HCActive / HActive;
		return math_max4(PixelWordRate / 4.0, AverageTribyteRate / 4.0, HActiveTribyteRate / 4.0, 25.0) * 1.002;
	}
}

double dcn5_calculate_required_dispclk(
		enum dml2_odm_mode ODMMode,
		double PixelClock,
		bool isTMDS420)
{
	double DispClk;

	if (ODMMode == dml2_odm_mode_combine_4to1) {
		DispClk = PixelClock / 4.0;
	} else if (ODMMode == dml2_odm_mode_combine_3to1) {
		DispClk = PixelClock / 3.0;
	} else if (ODMMode == dml2_odm_mode_combine_2to1) {
		DispClk = PixelClock / 2.0;
	} else {
		DispClk = PixelClock;
	}

	if (isTMDS420) {
		double TMDS420MinPixClock = PixelClock / 2.0;
		DispClk = math_max2(DispClk, TMDS420MinPixClock);
	}

	return DispClk;
}

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
		unsigned int WritebackLineBufferSize)
{
	(void)WritebackSourceWidth;
	(void)WritebackLineBufferSize;
	double DISPCLK_H, DISPCLK_V, THROUGHPUT, LoadVcoef;
	double effectivePixelClock;

	if (ODMMode == dml2_odm_mode_combine_4to1)
		effectivePixelClock = PixelClock / 4;
	else if (ODMMode == dml2_odm_mode_combine_3to1)
		effectivePixelClock = PixelClock / 3;
	if (ODMMode == dml2_odm_mode_combine_2to1)
		effectivePixelClock = PixelClock / 2;
	else
		effectivePixelClock = PixelClock;

	DISPCLK_H = effectivePixelClock * math_ceil2((double)WritebackHTaps / 4.0, 1) / WritebackHRatio;
	LoadVcoef = math_ceil2(1 / WritebackVRatio, 1) * (math_ceil2(WritebackVTaps / 4.0, 1) + 4.0);
	DISPCLK_V = effectivePixelClock * (math_ceil2(WritebackDestinationWidth / 4.0, 1) * (WritebackVTaps * math_ceil2(1 / WritebackVRatio, 1) + 1) / HTotal + LoadVcoef / HTotal);
	THROUGHPUT = effectivePixelClock * math_ceil2(1 / WritebackVRatio, 1) * (double)WritebackDestinationWidth / (double)HTotal;

	double v_ratio_chroma;
	double h_ratio_chroma;
	double output_width_chroma;

	if (dml2_core_utils_is_420(WritebackPixelFormat)) {
		v_ratio_chroma = 2.0 * WritebackVRatio;
		h_ratio_chroma = 2.0 * WritebackHRatio;
		output_width_chroma = 0.5 * WritebackDestinationWidth;
	} else if (dml2_core_utils_is_422_packed(WritebackPixelFormat) || dml2_core_utils_is_422_planar(WritebackPixelFormat)) {
		v_ratio_chroma = WritebackVRatio;
		h_ratio_chroma = 2.0 * WritebackHRatio;
		output_width_chroma = 0.5 * WritebackDestinationWidth;
	} else {
		v_ratio_chroma = WritebackVRatio;
		h_ratio_chroma = WritebackHRatio;
		output_width_chroma = WritebackDestinationWidth;
	}

	double DISPCLK_H_CHROMA, DISPCLK_V_CHROMA, THROUGHPUT_CHROMA, LoadVcoef_chroma;
	DISPCLK_H_CHROMA = effectivePixelClock * math_ceil2((double)WritebackHTapsChroma / 4.0, 1) / h_ratio_chroma;
	LoadVcoef_chroma = math_ceil2(1 / v_ratio_chroma, 1) * (math_ceil2(WritebackVTapsChroma / 4.0, 1) + 4.0);
	DISPCLK_V_CHROMA = effectivePixelClock * (math_ceil2(output_width_chroma / 4.0, 1) * (WritebackVTapsChroma * math_ceil2(1 / v_ratio_chroma, 1) + 1) / HTotal + LoadVcoef_chroma / HTotal);
	THROUGHPUT_CHROMA = effectivePixelClock * math_ceil2(1 / v_ratio_chroma, 1) * output_width_chroma / (double)HTotal;

	return math_max2(math_max3(DISPCLK_H, DISPCLK_V, THROUGHPUT), math_max3(DISPCLK_H_CHROMA, DISPCLK_V_CHROMA, THROUGHPUT_CHROMA));
}

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
		bool use_legacy_dsc_delay_formula)
{
	(void)Output;
	unsigned int DSCDelayRequirement_val = 0;
	unsigned int NumberOfDSCSlicesFactor = 1;

	if (DSCEnabled == true && OutputBpp != 0) {

		if (ODMMode == dml2_odm_mode_combine_4to1)
			NumberOfDSCSlicesFactor = 4;
		else if (ODMMode == dml2_odm_mode_combine_3to1)
			NumberOfDSCSlicesFactor = 3;
		else if (ODMMode == dml2_odm_mode_combine_2to1)
			NumberOfDSCSlicesFactor = 2;

		delay_uncertainty_t DscResult;
		unsigned int EffectiveNumSlices = NumberOfDSCSlices / NumberOfDSCSlicesFactor;
		int SliceWidth = (int)math_ceil2((double)HActive / (double)NumberOfDSCSlices, 1.0);

		// ASSUMPTION: Enabling dynamic gating for maximum delay calculation:
		// - dscclk_dynamic_gating_en = 1 (enabled for maximum delay)
		// - dispclk_dynamic_gating_en = 1 (enabled for maximum delay)
		// - initial_xmit_delay_offset = 0 (no additional offset)
		// - group_delay_after_initial_xmit_delay_override_en = 0 (use automatic calculation)
		// - group_delay_after_initial_xmit_delay = 0 (not used when override disabled)

#ifdef __DML_VBA_DEBUG__
		DML_LOG_VERBOSE("DML::%s: use_legacy_dsc_delay_formula= %u\n", __func__, use_legacy_dsc_delay_formula);
#endif
		if (use_legacy_dsc_delay_formula) {
			// Legacy DCN5 DSC delay calculation
			dcn5_dsc_compute_delay_legacy(&DscResult,
								   DSCInputBitPerComponent,  // bpc
								   (float)OutputBpp,         // bpp
								   SliceWidth,               // slice_width
								   EffectiveNumSlices,       // num_slices (adjusted for ODM)
								   OutputFormat,             // pixel_format
								   1,                        // dscclk_dynamic_gating_en (enabled for max delay)
								   1,                        // dispclk_dynamic_gating_en (enabled for max delay)
								   0,                        // initial_xmit_delay_offset (no offset)
								   0,                        // group_delay_after_initial_xmit_delay_override_en (use auto calc)
								   0);                       // group_delay_after_initial_xmit_delay (not used)
		} else {
			// DCN5.1 and DCN6 DSC delay calculation
			dcn5_dsc_compute_delay(&DscResult,
								   DSCInputBitPerComponent,  // bpc
								   (float)OutputBpp,         // bpp
								   SliceWidth,               // slice_width
								   EffectiveNumSlices,       // num_slices (adjusted for ODM)
								   OutputFormat,             // pixel_format
								   1,                        // dscclk_dynamic_gating_en (enabled for max delay)
								   1,                        // dispclk_dynamic_gating_en (enabled for max delay)
								   0,                        // initial_xmit_delay_offset (no offset)
								   0,                        // group_delay_after_initial_xmit_delay_override_en (use auto calc)
								   0);                       // group_delay_after_initial_xmit_delay (not used)
		}

		// Apply ODM factor multiplier and timing adjustments
		DSCDelayRequirement_val = (unsigned int)(DscResult.delay * NumberOfDSCSlicesFactor);

		DSCDelayRequirement_val = (unsigned int)(DSCDelayRequirement_val + (HTotal - HActive) * math_ceil2((double)DSCDelayRequirement_val / (double)HActive, 1.0));
		DSCDelayRequirement_val = (unsigned int)(DSCDelayRequirement_val * PixelClock / PixelClockBackEnd);

	} else {
		DSCDelayRequirement_val = 0;
	}
#ifdef __DML_VBA_DEBUG__
	DML_LOG_VERBOSE("DML::%s: DSCEnabled= %u\n", __func__, DSCEnabled);
	DML_LOG_VERBOSE("DML::%s: ODMMode = %u\n", __func__, ODMMode);
	DML_LOG_VERBOSE("DML::%s: OutputBpp = %f\n", __func__, OutputBpp);
	DML_LOG_VERBOSE("DML::%s: HActive = %u\n", __func__, HActive);
	DML_LOG_VERBOSE("DML::%s: HTotal = %u\n", __func__, HTotal);
	DML_LOG_VERBOSE("DML::%s: PixelClock = %f\n", __func__, PixelClock);
	DML_LOG_VERBOSE("DML::%s: PixelClockBackEnd = %f\n", __func__, PixelClockBackEnd);
	DML_LOG_VERBOSE("DML::%s: OutputFormat = %u\n", __func__, OutputFormat);
	DML_LOG_VERBOSE("DML::%s: DSCInputBitPerComponent = %u\n", __func__, DSCInputBitPerComponent);
	DML_LOG_VERBOSE("DML::%s: NumberOfDSCSlices = %u\n", __func__, NumberOfDSCSlices);
	DML_LOG_VERBOSE("DML::%s: DSCDelayRequirement_val = %u\n", __func__, DSCDelayRequirement_val);
#endif

	return DSCDelayRequirement_val;
}

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
		double *DPPCLKUsingSingleDPP)
{
	double DPPCLKUsingSingleDPPLuma;
	double DPPCLKUsingSingleDPPChroma;

	if (HRatio > 1) {
		*PSCL_THROUGHPUT = math_min2(MaxDCHUBToPSCLThroughput, MaxPSCLToLBThroughput * HRatio / math_ceil2((double)HTaps / 6.0, 1.0));
	} else {
		*PSCL_THROUGHPUT = math_min2(MaxDCHUBToPSCLThroughput, MaxPSCLToLBThroughput);
	}

	DPPCLKUsingSingleDPPLuma = PixelClock * math_max3(VTaps / 6 * math_min2(1, HRatio), HRatio * VRatio / *PSCL_THROUGHPUT, 1);

	if ((HTaps > 6 || VTaps > 6) && DPPCLKUsingSingleDPPLuma < 2 * PixelClock)
		DPPCLKUsingSingleDPPLuma = 2 * PixelClock;

	if (!dml2_core_utils_is_420(SourcePixelFormat) && !dml2_core_utils_is_422_planar(SourcePixelFormat) && SourcePixelFormat != dml2_rgbe_alpha) {
		*PSCL_THROUGHPUT_CHROMA = 0;
		*DPPCLKUsingSingleDPP = DPPCLKUsingSingleDPPLuma;
	} else {
		if (HRatioChroma > 1) {
			*PSCL_THROUGHPUT_CHROMA = math_min2(MaxDCHUBToPSCLThroughput, MaxPSCLToLBThroughput * HRatioChroma / math_ceil2((double)HTapsChroma / 6.0, 1.0));
		} else {
			*PSCL_THROUGHPUT_CHROMA = math_min2(MaxDCHUBToPSCLThroughput, MaxPSCLToLBThroughput);
		}
		DPPCLKUsingSingleDPPChroma = PixelClock * math_max3(VTapsChroma / 6 * math_min2(1, HRatioChroma),
				HRatioChroma * VRatioChroma / *PSCL_THROUGHPUT_CHROMA, 1);
		if ((HTapsChroma > 6 || VTapsChroma > 6) && DPPCLKUsingSingleDPPChroma < 2 * PixelClock)
			DPPCLKUsingSingleDPPChroma = 2 * PixelClock;
		*DPPCLKUsingSingleDPP = math_max2(DPPCLKUsingSingleDPPLuma, DPPCLKUsingSingleDPPChroma);
	}
}

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
		double DisplayPipeRequestDeliveryTimeChromaPrefetch[])
{
	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {
		double pixel_clock_mhz = ((double)display_cfg->stream_descriptors[display_cfg->plane_descriptors[k].stream_index].timing.pixel_clock_khz / 1000);

#ifdef __DML_VBA_DEBUG__
		DML_LOG_VERBOSE("DML::%s: k=%u : HRatio = %f\n", __func__, k, display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio);
		DML_LOG_VERBOSE("DML::%s: k=%u : VRatio = %f\n", __func__, k, display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio);
		DML_LOG_VERBOSE("DML::%s: k=%u : HRatioChroma = %f\n", __func__, k, display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio);
		DML_LOG_VERBOSE("DML::%s: k=%u : VRatioChroma = %f\n", __func__, k, display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio);
		DML_LOG_VERBOSE("DML::%s: k=%u : VRatioPrefetchY = %f\n", __func__, k, VRatioPrefetchY[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u : VRatioPrefetchC = %f\n", __func__, k, VRatioPrefetchC[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u : swath_width_luma_ub = %u\n", __func__, k, swath_width_luma_ub[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u : swath_width_chroma_ub = %u\n", __func__, k, swath_width_chroma_ub[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u : PSCL_THROUGHPUT = %f\n", __func__, k, PSCL_THROUGHPUT[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u : PSCL_THROUGHPUT_CHROMA = %f\n", __func__, k, PSCL_THROUGHPUT_CHROMA[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u : DPPPerSurface = %u\n", __func__, k, NoOfDPP[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u : pixel_clock_mhz = %f\n", __func__, k, pixel_clock_mhz);
		DML_LOG_VERBOSE("DML::%s: k=%u : Dppclk = %f\n", __func__, k, Dppclk[k]);
#endif
		if (display_cfg->plane_descriptors[k].composition.scaler_info.plane0.v_ratio <= 1) {
			DisplayPipeLineDeliveryTimeLuma[k] = swath_width_luma_ub[k] * NoOfDPP[k] / display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio / pixel_clock_mhz;
		} else {
			DisplayPipeLineDeliveryTimeLuma[k] = swath_width_luma_ub[k] / PSCL_THROUGHPUT[k] / Dppclk[k];
		}

		if (BytePerPixelC[k] == 0) {
			DisplayPipeLineDeliveryTimeChroma[k] = 0;
		} else {
			if (display_cfg->plane_descriptors[k].composition.scaler_info.plane1.v_ratio <= 1) {
				DisplayPipeLineDeliveryTimeChroma[k] = swath_width_chroma_ub[k] * NoOfDPP[k] / display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio / pixel_clock_mhz;
			} else {
				DisplayPipeLineDeliveryTimeChroma[k] = swath_width_chroma_ub[k] / PSCL_THROUGHPUT_CHROMA[k] / Dppclk[k];
			}
		}

		if (VRatioPrefetchY[k] <= 1) {
			DisplayPipeLineDeliveryTimeLumaPrefetch[k] = swath_width_luma_ub[k] * NoOfDPP[k] / display_cfg->plane_descriptors[k].composition.scaler_info.plane0.h_ratio / pixel_clock_mhz;
		} else {
			DisplayPipeLineDeliveryTimeLumaPrefetch[k] = swath_width_luma_ub[k] / PSCL_THROUGHPUT[k] / Dppclk[k];
		}

		if (BytePerPixelC[k] == 0) {
			DisplayPipeLineDeliveryTimeChromaPrefetch[k] = 0;
		} else {
			if (VRatioPrefetchC[k] <= 1) {
				DisplayPipeLineDeliveryTimeChromaPrefetch[k] = swath_width_chroma_ub[k] * NoOfDPP[k] / display_cfg->plane_descriptors[k].composition.scaler_info.plane1.h_ratio / pixel_clock_mhz;
			} else {
				DisplayPipeLineDeliveryTimeChromaPrefetch[k] = swath_width_chroma_ub[k] / PSCL_THROUGHPUT_CHROMA[k] / Dppclk[k];
			}
		}

		if (BytePerPixelC[k] > 0) {
			DisplayPipeLineDeliveryTimeLuma[k] = math_max2(DisplayPipeLineDeliveryTimeLuma[k], swath_width_luma_ub[k] * BytePerPixelY[k] / 32.0 / DCFCLKDeepSleep);
			DisplayPipeLineDeliveryTimeChroma[k] = math_max2(DisplayPipeLineDeliveryTimeChroma[k], swath_width_chroma_ub[k] * BytePerPixelC[k] / 32.0 / DCFCLKDeepSleep);
			DisplayPipeLineDeliveryTimeLumaPrefetch[k] = math_max2(DisplayPipeLineDeliveryTimeLumaPrefetch[k], swath_width_luma_ub[k] * BytePerPixelY[k] / 32.0 / DCFCLKDeepSleep);
			DisplayPipeLineDeliveryTimeChromaPrefetch[k] = math_max2(DisplayPipeLineDeliveryTimeChromaPrefetch[k], swath_width_chroma_ub[k] * BytePerPixelC[k] / 32.0 / DCFCLKDeepSleep);
		} else {
			DisplayPipeLineDeliveryTimeLuma[k] = math_max2(DisplayPipeLineDeliveryTimeLuma[k], swath_width_luma_ub[k] * BytePerPixelY[k] / 64.0 / DCFCLKDeepSleep);
			DisplayPipeLineDeliveryTimeLumaPrefetch[k] = math_max2(DisplayPipeLineDeliveryTimeLumaPrefetch[k], swath_width_luma_ub[k] * BytePerPixelY[k] / 64.0 / DCFCLKDeepSleep);
		}

#ifdef __DML_VBA_DEBUG__
		DML_LOG_VERBOSE("DML::%s: k=%u : DisplayPipeLineDeliveryTimeLuma = %f\n", __func__, k, DisplayPipeLineDeliveryTimeLuma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u : DisplayPipeLineDeliveryTimeLumaPrefetch = %f\n", __func__, k, DisplayPipeLineDeliveryTimeLumaPrefetch[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u : DisplayPipeLineDeliveryTimeChroma = %f\n", __func__, k, DisplayPipeLineDeliveryTimeChroma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u : DisplayPipeLineDeliveryTimeChromaPrefetch = %f\n", __func__, k, DisplayPipeLineDeliveryTimeChromaPrefetch[k]);
#endif
	}

	for (unsigned int k = 0; k < NumberOfActiveSurfaces; ++k) {

		DisplayPipeRequestDeliveryTimeLuma[k] = DisplayPipeLineDeliveryTimeLuma[k] / req_per_swath_ub_l[k];
		DisplayPipeRequestDeliveryTimeLumaPrefetch[k] = DisplayPipeLineDeliveryTimeLumaPrefetch[k] / req_per_swath_ub_l[k];
		if (BytePerPixelC[k] == 0) {
			DisplayPipeRequestDeliveryTimeChroma[k] = 0;
			DisplayPipeRequestDeliveryTimeChromaPrefetch[k] = 0;
		} else {
			DisplayPipeRequestDeliveryTimeChroma[k] = DisplayPipeLineDeliveryTimeChroma[k] / req_per_swath_ub_c[k];
			DisplayPipeRequestDeliveryTimeChromaPrefetch[k] = DisplayPipeLineDeliveryTimeChromaPrefetch[k] / req_per_swath_ub_c[k];
		}
#ifdef __DML_VBA_DEBUG__
		DML_LOG_VERBOSE("DML::%s: k=%u : DisplayPipeRequestDeliveryTimeLuma = %f\n", __func__, k, DisplayPipeRequestDeliveryTimeLuma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u : DisplayPipeRequestDeliveryTimeLumaPrefetch = %f\n", __func__, k, DisplayPipeRequestDeliveryTimeLumaPrefetch[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u : req_per_swath_ub_l = %d\n", __func__, k, req_per_swath_ub_l[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u : DisplayPipeRequestDeliveryTimeChroma = %f\n", __func__, k, DisplayPipeRequestDeliveryTimeChroma[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u : DisplayPipeRequestDeliveryTimeChromaPrefetch = %f\n", __func__, k, DisplayPipeRequestDeliveryTimeChromaPrefetch[k]);
		DML_LOG_VERBOSE("DML::%s: k=%u : req_per_swath_ub_c = %d\n", __func__, k, req_per_swath_ub_c[k]);
#endif
	}
}
