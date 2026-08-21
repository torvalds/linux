// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_CORE_CALCS_DSC_SHARED_TYPES_H__
#define __DML2_CORE_CALCS_DSC_SHARED_TYPES_H__

#include "dml_top_display_cfg_types.h"

// Delay and uncertainty structure
typedef struct {
	int delay;
	int uncertainty;
} delay_uncertainty_t;

// Latency structure with group, pipeline, and pixel delays
typedef struct {
	int groups;	  // latency in groups - Number of groups needed to be sent before output can begin
	int pipeline;	// pipeline delay latency - Propagation delay through the bitstream construction layer in number of pixel containers
	int pixels;	  // latency in pixels - Number of groups multiplied by cycles per group

	// Extra variables needed for functional coverage
	int additional_group_delay;
	int lines_to_reach_ixd;
	int groups_to_reach_ixd;
	int slice_width_groups;
	int initial_xmit_delay;
	int number_of_lines_to_reach_ixd;
	int slice_width_modified;
} latency_t;

#endif /* __DML2_CORE_CALCS_DSC_SHARED_TYPES_H__ */
