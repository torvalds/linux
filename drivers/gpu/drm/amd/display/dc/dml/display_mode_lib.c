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

#include "display_mode_lib.h"
#include "dc_features.h"
#include "dcn20/display_mode_vba_20.h"
#include "dcn20/display_rq_dlg_calc_20.h"
#include "dcn20/display_mode_vba_20v2.h"
#include "dcn20/display_rq_dlg_calc_20v2.h"
#include "dcn21/display_mode_vba_21.h"
#include "dcn21/display_rq_dlg_calc_21.h"
#include "dcn30/display_mode_vba_30.h"
#include "dcn30/display_rq_dlg_calc_30.h"
#include "dcn31/display_mode_vba_31.h"
#include "dcn31/display_rq_dlg_calc_31.h"
#include "dcn314/display_mode_vba_314.h"
#include "dcn314/display_rq_dlg_calc_314.h"
#include "dcn32/display_mode_vba_32.h"
#include "dcn32/display_rq_dlg_calc_32.h"
#include "dml_logger.h"

const struct dml_funcs dml20_funcs = {
	.validate = dml20_ModeSupportAndSystemConfigurationFull,
	.recalculate = dml20_recalculate,
	.rq_dlg_get_dlg_reg = dml20_rq_dlg_get_dlg_reg,
	.rq_dlg_get_rq_reg = dml20_rq_dlg_get_rq_reg
};

const struct dml_funcs dml20v2_funcs = {
	.validate = dml20v2_ModeSupportAndSystemConfigurationFull,
	.recalculate = dml20v2_recalculate,
	.rq_dlg_get_dlg_reg = dml20v2_rq_dlg_get_dlg_reg,
	.rq_dlg_get_rq_reg = dml20v2_rq_dlg_get_rq_reg
};

const struct dml_funcs dml21_funcs = {
        .validate = dml21_ModeSupportAndSystemConfigurationFull,
        .recalculate = dml21_recalculate,
        .rq_dlg_get_dlg_reg = dml21_rq_dlg_get_dlg_reg,
        .rq_dlg_get_rq_reg = dml21_rq_dlg_get_rq_reg
};

const struct dml_funcs dml30_funcs = {
	.validate = dml30_ModeSupportAndSystemConfigurationFull,
	.recalculate = dml30_recalculate,
	.rq_dlg_get_dlg_reg = dml30_rq_dlg_get_dlg_reg,
	.rq_dlg_get_rq_reg = dml30_rq_dlg_get_rq_reg
};

const struct dml_funcs dml31_funcs = {
	.validate = dml31_ModeSupportAndSystemConfigurationFull,
	.recalculate = dml31_recalculate,
	.rq_dlg_get_dlg_reg = dml31_rq_dlg_get_dlg_reg,
	.rq_dlg_get_rq_reg = dml31_rq_dlg_get_rq_reg
};

const struct dml_funcs dml314_funcs = {
	.validate = dml314_ModeSupportAndSystemConfigurationFull,
	.recalculate = dml314_recalculate,
	.rq_dlg_get_dlg_reg = dml314_rq_dlg_get_dlg_reg,
	.rq_dlg_get_rq_reg = dml314_rq_dlg_get_rq_reg
};

const struct dml_funcs dml32_funcs = {
	.validate = dml32_ModeSupportAndSystemConfigurationFull,
    .recalculate = dml32_recalculate,
	.rq_dlg_get_dlg_reg_v2 = dml32_rq_dlg_get_dlg_reg,
	.rq_dlg_get_rq_reg_v2 = dml32_rq_dlg_get_rq_reg
};

void dml_init_instance(struct display_mode_lib *lib,
		const struct _vcs_dpi_soc_bounding_box_st *soc_bb,
		const struct _vcs_dpi_ip_params_st *ip_params,
		enum dml_project project)
{
	lib->soc = *soc_bb;
	lib->ip = *ip_params;
	lib->project = project;
	switch (project) {
	case DML_PROJECT_NAVI10:
	case DML_PROJECT_DCN201:
		lib->funcs = dml20_funcs;
		break;
	case DML_PROJECT_NAVI10v2:
		lib->funcs = dml20v2_funcs;
		break;
        case DML_PROJECT_DCN21:
                lib->funcs = dml21_funcs;
                break;
	case DML_PROJECT_DCN30:
		lib->funcs = dml30_funcs;
		break;
	case DML_PROJECT_DCN31:
	case DML_PROJECT_DCN31_FPGA:
		lib->funcs = dml31_funcs;
		break;
	case DML_PROJECT_DCN314:
		lib->funcs = dml314_funcs;
		break;
	case DML_PROJECT_DCN32:
		lib->funcs = dml32_funcs;
		break;

	default:
		break;
	}
}

const char *dml_get_status_message(enum dm_validation_status status)
{
	switch (status) {
	case DML_VALIDATION_OK:                   return "Validation OK";
	case DML_FAIL_SCALE_RATIO_TAP:            return "Scale ratio/tap";
	case DML_FAIL_SOURCE_PIXEL_FORMAT:        return "Source pixel format";
	case DML_FAIL_VIEWPORT_SIZE:              return "Viewport size";
	case DML_FAIL_TOTAL_V_ACTIVE_BW:          return "Total vertical active bandwidth";
	case DML_FAIL_DIO_SUPPORT:                return "DIO support";
	case DML_FAIL_NOT_ENOUGH_DSC:             return "Not enough DSC Units";
	case DML_FAIL_DSC_CLK_REQUIRED:           return "DSC clock required";
	case DML_FAIL_URGENT_LATENCY:             return "Urgent latency";
	case DML_FAIL_REORDERING_BUFFER:          return "Re-ordering buffer";
	case DML_FAIL_DISPCLK_DPPCLK:             return "Dispclk and Dppclk";
	case DML_FAIL_TOTAL_AVAILABLE_PIPES:      return "Total available pipes";
	case DML_FAIL_NUM_OTG:                    return "Number of OTG";
	case DML_FAIL_WRITEBACK_MODE:             return "Writeback mode";
	case DML_FAIL_WRITEBACK_LATENCY:          return "Writeback latency";
	case DML_FAIL_WRITEBACK_SCALE_RATIO_TAP:  return "Writeback scale ratio/tap";
	case DML_FAIL_CURSOR_SUPPORT:             return "Cursor support";
	case DML_FAIL_PITCH_SUPPORT:              return "Pitch support";
	case DML_FAIL_PTE_BUFFER_SIZE:            return "PTE buffer size";
	case DML_FAIL_DSC_INPUT_BPC:              return "DSC input bpc";
	case DML_FAIL_PREFETCH_SUPPORT:           return "Prefetch support";
	case DML_FAIL_V_RATIO_PREFETCH:           return "Vertical ratio prefetch";
	default:                                  return "Unknown Status";
	}
}

void dml_log_pipe_params(
		struct display_mode_lib *mode_lib,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt)
{
	display_pipe_source_params_st *pipe_src;
	display_pipe_dest_params_st   *pipe_dest;
	scaler_ratio_depth_st         *scale_ratio_depth;
	scaler_taps_st                *scale_taps;
	display_output_params_st      *dout;
	display_clocks_and_cfg_st     *clks_cfg;
	int i;

	for (i = 0; i < pipe_cnt; i++) {
		pipe_src = &(pipes[i].pipe.src);
		pipe_dest = &(pipes[i].pipe.dest);
		scale_ratio_depth = &(pipes[i].pipe.scale_ratio_depth);
		scale_taps = &(pipes[i].pipe.scale_taps);
		dout = &(pipes[i].dout);
		clks_cfg = &(pipes[i].clks_cfg);

		dml_print("DML PARAMS: =====================================\n");
		dml_print("DML PARAMS: PIPE [%d] SOURCE PARAMS:\n", i);
		dml_print("DML PARAMS:     source_format              = %d\n", pipe_src->source_format);
		dml_print("DML PARAMS:     dcc                        = %d\n", pipe_src->dcc);
		dml_print("DML PARAMS:     dcc_rate                   = %d\n", pipe_src->dcc_rate);
		dml_print("DML PARAMS:     dcc_use_global             = %d\n", pipe_src->dcc_use_global);
		dml_print("DML PARAMS:     vm                         = %d\n", pipe_src->vm);
		dml_print("DML PARAMS:     gpuvm                      = %d\n", pipe_src->gpuvm);
		dml_print("DML PARAMS:     hostvm                     = %d\n", pipe_src->hostvm);
		dml_print("DML PARAMS:     gpuvm_levels_force_en      = %d\n", pipe_src->gpuvm_levels_force_en);
		dml_print("DML PARAMS:     gpuvm_levels_force         = %d\n", pipe_src->gpuvm_levels_force);
		dml_print("DML PARAMS:     source_scan                = %d\n", pipe_src->source_scan);
		dml_print("DML PARAMS:     sw_mode                    = %d\n", pipe_src->sw_mode);
		dml_print("DML PARAMS:     macro_tile_size            = %d\n", pipe_src->macro_tile_size);
		dml_print("DML PARAMS:     viewport_width             = %d\n", pipe_src->viewport_width);
		dml_print("DML PARAMS:     viewport_height            = %d\n", pipe_src->viewport_height);
		dml_print("DML PARAMS:     viewport_y_y               = %d\n", pipe_src->viewport_y_y);
		dml_print("DML PARAMS:     viewport_y_c               = %d\n", pipe_src->viewport_y_c);
		dml_print("DML PARAMS:     viewport_width_c           = %d\n", pipe_src->viewport_width_c);
		dml_print("DML PARAMS:     viewport_height_c          = %d\n", pipe_src->viewport_height_c);
		dml_print("DML PARAMS:     data_pitch                 = %d\n", pipe_src->data_pitch);
		dml_print("DML PARAMS:     data_pitch_c               = %d\n", pipe_src->data_pitch_c);
		dml_print("DML PARAMS:     meta_pitch                 = %d\n", pipe_src->meta_pitch);
		dml_print("DML PARAMS:     meta_pitch_c               = %d\n", pipe_src->meta_pitch_c);
		dml_print("DML PARAMS:     cur0_src_width             = %d\n", pipe_src->cur0_src_width);
		dml_print("DML PARAMS:     cur0_bpp                   = %d\n", pipe_src->cur0_bpp);
		dml_print("DML PARAMS:     cur1_src_width             = %d\n", pipe_src->cur1_src_width);
		dml_print("DML PARAMS:     cur1_bpp                   = %d\n", pipe_src->cur1_bpp);
		dml_print("DML PARAMS:     num_cursors                = %d\n", pipe_src->num_cursors);
		dml_print("DML PARAMS:     is_hsplit                  = %d\n", pipe_src->is_hsplit);
		dml_print("DML PARAMS:     hsplit_grp                 = %d\n", pipe_src->hsplit_grp);
		dml_print("DML PARAMS:     dynamic_metadata_enable    = %d\n", pipe_src->dynamic_metadata_enable);
		dml_print("DML PARAMS:     dmdata_lines_before_active = %d\n", pipe_src->dynamic_metadata_lines_before_active);
		dml_print("DML PARAMS:     dmdata_xmit_bytes          = %d\n", pipe_src->dynamic_metadata_xmit_bytes);
		dml_print("DML PARAMS:     immediate_flip             = %d\n", pipe_src->immediate_flip);
		dml_print("DML PARAMS:     v_total_min                = %d\n", pipe_src->v_total_min);
		dml_print("DML PARAMS:     v_total_max                = %d\n", pipe_src->v_total_max);
		dml_print("DML PARAMS: =====================================\n");

		dml_print("DML PARAMS: PIPE [%d] DESTINATION PARAMS:\n", i);
		dml_print("DML PARAMS:     recout_width               = %d\n", pipe_dest->recout_width);
		dml_print("DML PARAMS:     recout_height              = %d\n", pipe_dest->recout_height);
		dml_print("DML PARAMS:     full_recout_width          = %d\n", pipe_dest->full_recout_width);
		dml_print("DML PARAMS:     full_recout_height         = %d\n", pipe_dest->full_recout_height);
		dml_print("DML PARAMS:     hblank_start               = %d\n", pipe_dest->hblank_start);
		dml_print("DML PARAMS:     hblank_end                 = %d\n", pipe_dest->hblank_end);
		dml_print("DML PARAMS:     vblank_start               = %d\n", pipe_dest->vblank_start);
		dml_print("DML PARAMS:     vblank_end                 = %d\n", pipe_dest->vblank_end);
		dml_print("DML PARAMS:     htotal                     = %d\n", pipe_dest->htotal);
		dml_print("DML PARAMS:     vtotal                     = %d\n", pipe_dest->vtotal);
		dml_print("DML PARAMS:     vactive                    = %d\n", pipe_dest->vactive);
		dml_print("DML PARAMS:     hactive                    = %d\n", pipe_dest->hactive);
		dml_print("DML PARAMS:     vstartup_start             = %d\n", pipe_dest->vstartup_start);
		dml_print("DML PARAMS:     vupdate_offset             = %d\n", pipe_dest->vupdate_offset);
		dml_print("DML PARAMS:     vupdate_width              = %d\n", pipe_dest->vupdate_width);
		dml_print("DML PARAMS:     vready_offset              = %d\n", pipe_dest->vready_offset);
		dml_print("DML PARAMS:     interlaced                 = %d\n", pipe_dest->interlaced);
		dml_print("DML PARAMS:     pixel_rate_mhz             = %3.2f\n", pipe_dest->pixel_rate_mhz);
		dml_print("DML PARAMS:     sync_vblank_all_planes     = %d\n", pipe_dest->synchronized_vblank_all_planes);
		dml_print("DML PARAMS:     otg_inst                   = %d\n", pipe_dest->otg_inst);
		dml_print("DML PARAMS:     odm_combine                = %d\n", pipe_dest->odm_combine);
		dml_print("DML PARAMS:     use_maximum_vstartup       = %d\n", pipe_dest->use_maximum_vstartup);
		dml_print("DML PARAMS:     vtotal_max                 = %d\n", pipe_dest->vtotal_max);
		dml_print("DML PARAMS:     vtotal_min                 = %d\n", pipe_dest->vtotal_min);
		dml_print("DML PARAMS: =====================================\n");

		dml_print("DML PARAMS: PIPE [%d] SCALER PARAMS:\n", i);
		dml_print("DML PARAMS:     hscl_ratio                 = %3.4f\n", scale_ratio_depth->hscl_ratio);
		dml_print("DML PARAMS:     vscl_ratio                 = %3.4f\n", scale_ratio_depth->vscl_ratio);
		dml_print("DML PARAMS:     hscl_ratio_c               = %3.4f\n", scale_ratio_depth->hscl_ratio_c);
		dml_print("DML PARAMS:     vscl_ratio_c               = %3.4f\n", scale_ratio_depth->vscl_ratio_c);
		dml_print("DML PARAMS:     vinit                      = %3.4f\n", scale_ratio_depth->vinit);
		dml_print("DML PARAMS:     vinit_c                    = %3.4f\n", scale_ratio_depth->vinit_c);
		dml_print("DML PARAMS:     vinit_bot                  = %3.4f\n", scale_ratio_depth->vinit_bot);
		dml_print("DML PARAMS:     vinit_bot_c                = %3.4f\n", scale_ratio_depth->vinit_bot_c);
		dml_print("DML PARAMS:     lb_depth                   = %d\n", scale_ratio_depth->lb_depth);
		dml_print("DML PARAMS:     scl_enable                 = %d\n", scale_ratio_depth->scl_enable);
		dml_print("DML PARAMS:     htaps                      = %d\n", scale_taps->htaps);
		dml_print("DML PARAMS:     vtaps                      = %d\n", scale_taps->vtaps);
		dml_print("DML PARAMS:     htaps_c                    = %d\n", scale_taps->htaps_c);
		dml_print("DML PARAMS:     vtaps_c                    = %d\n", scale_taps->vtaps_c);
		dml_print("DML PARAMS: =====================================\n");

		dml_print("DML PARAMS: PIPE [%d] DISPLAY OUTPUT PARAMS:\n", i);
		dml_print("DML PARAMS:     output_type                = %d\n", dout->output_type);
		dml_print("DML PARAMS:     output_format              = %d\n", dout->output_format);
		dml_print("DML PARAMS:     dsc_input_bpc              = %d\n", dout->dsc_input_bpc);
		dml_print("DML PARAMS:     output_bpp                 = %3.4f\n", dout->output_bpp);
		dml_print("DML PARAMS:     dp_lanes                   = %d\n", dout->dp_lanes);
		dml_print("DML PARAMS:     dsc_enable                 = %d\n", dout->dsc_enable);
		dml_print("DML PARAMS:     dsc_slices                 = %d\n", dout->dsc_slices);
		dml_print("DML PARAMS:     wb_enable                  = %d\n", dout->wb_enable);
		dml_print("DML PARAMS:     num_active_wb              = %d\n", dout->num_active_wb);
		dml_print("DML PARAMS: =====================================\n");

		dml_print("DML PARAMS: PIPE [%d] CLOCK CONFIG PARAMS:\n", i);
		dml_print("DML PARAMS:     voltage                    = %d\n", clks_cfg->voltage);
		dml_print("DML PARAMS:     dppclk_mhz                 = %3.2f\n", clks_cfg->dppclk_mhz);
		dml_print("DML PARAMS:     refclk_mhz                 = %3.2f\n", clks_cfg->refclk_mhz);
		dml_print("DML PARAMS:     dispclk_mhz                = %3.2f\n", clks_cfg->dispclk_mhz);
		dml_print("DML PARAMS:     dcfclk_mhz                 = %3.2f\n", clks_cfg->dcfclk_mhz);
		dml_print("DML PARAMS:     socclk_mhz                 = %3.2f\n", clks_cfg->socclk_mhz);
		dml_print("DML PARAMS: =====================================\n");
	}
}

void dml_log_mode_support_params(struct display_mode_lib *mode_lib)
{
	int i;

	for (i = mode_lib->vba.soc.num_states; i >= 0; i--) {
		dml_print("DML SUPPORT: ===============================================\n");
		dml_print("DML SUPPORT: Voltage State %d\n", i);
		dml_print("DML SUPPORT:     Mode Supported              : %s\n", mode_lib->vba.ModeSupport[i][0] ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     Mode Supported (pipe split) : %s\n", mode_lib->vba.ModeSupport[i][1] ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     Scale Ratio And Taps                : %s\n", mode_lib->vba.ScaleRatioAndTapsSupport ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     Source Format Pixel And Scan        : %s\n", mode_lib->vba.SourceFormatPixelAndScanSupport ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     Viewport Size                       : [%s, %s]\n", mode_lib->vba.ViewportSizeSupport[i][0] ? "Supported" : "NOT Supported", mode_lib->vba.ViewportSizeSupport[i][1] ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     DIO Support                         : %s\n", mode_lib->vba.DIOSupport[i] ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     ODM Combine 4To1 Support Check      : %s\n", mode_lib->vba.ODMCombine4To1SupportCheckOK[i] ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     DSC Units                           : %s\n", mode_lib->vba.NotEnoughDSCUnits[i] ? "Not Supported" : "Supported");
		dml_print("DML SUPPORT:     DSCCLK Required                     : %s\n", mode_lib->vba.DSCCLKRequiredMoreThanSupported[i] ? "Not Supported" : "Supported");
		dml_print("DML SUPPORT:     DTBCLK Required                     : %s\n", mode_lib->vba.DTBCLKRequiredMoreThanSupported[i] ? "Not Supported" : "Supported");
		dml_print("DML SUPPORT:     Re-ordering Buffer                  : [%s, %s]\n", mode_lib->vba.ROBSupport[i][0] ? "Supported" : "NOT Supported", mode_lib->vba.ROBSupport[i][1] ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     DISPCLK and DPPCLK                  : [%s, %s]\n", mode_lib->vba.DISPCLK_DPPCLK_Support[i][0] ? "Supported" : "NOT Supported", mode_lib->vba.DISPCLK_DPPCLK_Support[i][1] ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     Total Available Pipes               : [%s, %s]\n", mode_lib->vba.TotalAvailablePipesSupport[i][0] ? "Supported" : "NOT Supported", mode_lib->vba.TotalAvailablePipesSupport[i][1] ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     Writeback Latency                   : %s\n", mode_lib->vba.WritebackLatencySupport ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     Writeback Scale Ratio And Taps      : %s\n", mode_lib->vba.WritebackScaleRatioAndTapsSupport ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     Cursor                              : %s\n", mode_lib->vba.CursorSupport ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     Pitch                               : %s\n", mode_lib->vba.PitchSupport ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     Prefetch                            : [%s, %s]\n", mode_lib->vba.PrefetchSupported[i][0] ? "Supported" : "NOT Supported", mode_lib->vba.PrefetchSupported[i][1] ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     Dynamic Metadata                    : [%s, %s]\n", mode_lib->vba.DynamicMetadataSupported[i][0] ? "Supported" : "NOT Supported", mode_lib->vba.DynamicMetadataSupported[i][1] ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     Total Vertical Active Bandwidth     : [%s, %s]\n", mode_lib->vba.TotalVerticalActiveBandwidthSupport[i][0] ? "Supported" : "NOT Supported", mode_lib->vba.TotalVerticalActiveBandwidthSupport[i][1] ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     VRatio In Prefetch                  : [%s, %s]\n", mode_lib->vba.VRatioInPrefetchSupported[i][0] ? "Supported" : "NOT Supported", mode_lib->vba.VRatioInPrefetchSupported[i][1] ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     PTE Buffer Size Not Exceeded        : [%s, %s]\n", mode_lib->vba.PTEBufferSizeNotExceeded[i][0] ? "Supported" : "NOT Supported", mode_lib->vba.PTEBufferSizeNotExceeded[i][1] ? "Supported" : "NOT Supported");
		dml_print("DML SUPPORT:     DSC Input BPC                       : %s\n", mode_lib->vba.NonsupportedDSCInputBPC ? "Not Supported" : "Supported");
		dml_print("DML SUPPORT:     HostVMEnable                        : %d\n", mode_lib->vba.HostVMEnable);
		dml_print("DML SUPPORT:     ImmediateFlipSupportedForState      : [%d, %d]\n", mode_lib->vba.ImmediateFlipSupportedForState[i][0], mode_lib->vba.ImmediateFlipSupportedForState[i][1]);
	}
}
