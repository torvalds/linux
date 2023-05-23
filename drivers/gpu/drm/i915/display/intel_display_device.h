/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_DEVICE_H__
#define __INTEL_DISPLAY_DEVICE_H__

#include <linux/types.h>

#include "display/intel_display_limits.h"

#define DEV_INFO_DISPLAY_FOR_EACH_FLAG(func) \
	/* Keep in alphabetical order */ \
	func(cursor_needs_physical); \
	func(has_cdclk_crawl); \
	func(has_cdclk_squash); \
	func(has_ddi); \
	func(has_dp_mst); \
	func(has_dsb); \
	func(has_fpga_dbg); \
	func(has_gmch); \
	func(has_hotplug); \
	func(has_hti); \
	func(has_ipc); \
	func(has_overlay); \
	func(has_psr); \
	func(has_psr_hw_tracking); \
	func(overlay_needs_physical); \
	func(supports_tv);

struct intel_display_device_info {
	u8 abox_mask;

	struct {
		u16 size; /* in blocks */
		u8 slice_mask;
	} dbuf;

#define DEFINE_FLAG(name) u8 name:1
	DEV_INFO_DISPLAY_FOR_EACH_FLAG(DEFINE_FLAG);
#undef DEFINE_FLAG

	/* Global register offset for the display engine */
	u32 mmio_offset;

	/* Register offsets for the various display pipes and transcoders */
	u32 pipe_offsets[I915_MAX_TRANSCODERS];
	u32 trans_offsets[I915_MAX_TRANSCODERS];
	u32 cursor_offsets[I915_MAX_PIPES];

	struct {
		u32 degamma_lut_size;
		u32 gamma_lut_size;
		u32 degamma_lut_tests;
		u32 gamma_lut_tests;
	} color;
};

#endif
