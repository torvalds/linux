// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#ifndef __DML2_CORE_DCN5_CALCS_DSC_LATENCY_H__
#define __DML2_CORE_DCN5_CALCS_DSC_LATENCY_H__

#include "dml2_external_lib_deps.h"
#include "dml2_core_calcs_dsc_shared_types.h"

// dcn5_dsc_compute_delay - DSC delay using the updated formula (DCN5.1 and newer)
void dcn5_dsc_compute_delay(delay_uncertainty_t *p, int bpc, float bpp, int slice_width, int num_slices,
		enum dml2_output_format_class pixel_format, int dscclk_dynamic_gating_en, int dispclk_dynamic_gating_en,
		int initial_xmit_delay_offset, int group_delay_after_initial_xmit_delay_override_en,
		int group_delay_after_initial_xmit_delay);

// dcn5_dsc_compute_delay_legacy - DSC delay using the original DCN5 formula
void dcn5_dsc_compute_delay_legacy(delay_uncertainty_t *p, int bpc, float bpp, int slice_width, int num_slices,
		enum dml2_output_format_class pixel_format, int dscclk_dynamic_gating_en, int dispclk_dynamic_gating_en,
		int initial_xmit_delay_offset, int group_delay_after_initial_xmit_delay_override_en,
		int group_delay_after_initial_xmit_delay);

#endif /* __DML2_CORE_DCN5_CALCS_DSC_LATENCY_H__ */
