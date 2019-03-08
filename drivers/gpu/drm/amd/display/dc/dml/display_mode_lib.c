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

extern const struct _vcs_dpi_ip_params_st dcn1_0_ip;
extern const struct _vcs_dpi_soc_bounding_box_st dcn1_0_soc;

static void set_soc_bounding_box(struct _vcs_dpi_soc_bounding_box_st *soc, enum dml_project project)
{
	switch (project) {
	case DML_PROJECT_RAVEN1:
		*soc = dcn1_0_soc;
		break;
	default:
		ASSERT(0);
		break;
	}
}

static void set_ip_params(struct _vcs_dpi_ip_params_st *ip, enum dml_project project)
{
	switch (project) {
	case DML_PROJECT_RAVEN1:
		*ip = dcn1_0_ip;
		break;
	default:
		ASSERT(0);
		break;
	}
}

void dml_init_instance(struct display_mode_lib *lib, enum dml_project project)
{
	if (lib->project != project) {
		set_soc_bounding_box(&lib->soc, project);
		set_ip_params(&lib->ip, project);
		lib->project = project;
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
