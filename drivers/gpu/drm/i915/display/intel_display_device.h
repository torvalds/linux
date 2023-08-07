/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_DEVICE_H__
#define __INTEL_DISPLAY_DEVICE_H__

#include <linux/types.h>

#include "intel_display_limits.h"

struct drm_i915_private;
struct drm_printer;

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

#define HAS_ASYNC_FLIPS(i915)		(DISPLAY_VER(i915) >= 5)
#define HAS_CDCLK_CRAWL(i915)		(DISPLAY_INFO(i915)->has_cdclk_crawl)
#define HAS_CDCLK_SQUASH(i915)		(DISPLAY_INFO(i915)->has_cdclk_squash)
#define HAS_CUR_FBC(i915)		(!HAS_GMCH(i915) && DISPLAY_VER(i915) >= 7)
#define HAS_D12_PLANE_MINIMIZATION(i915) (IS_ROCKETLAKE(i915) || IS_ALDERLAKE_S(i915))
#define HAS_DDI(i915)			(DISPLAY_INFO(i915)->has_ddi)
#define HAS_DISPLAY(i915)		(DISPLAY_RUNTIME_INFO(i915)->pipe_mask != 0)
#define HAS_DMC(i915)			(DISPLAY_RUNTIME_INFO(i915)->has_dmc)
#define HAS_DOUBLE_BUFFERED_M_N(i915)	(DISPLAY_VER(i915) >= 9 || IS_BROADWELL(i915))
#define HAS_DP_MST(i915)		(DISPLAY_INFO(i915)->has_dp_mst)
#define HAS_DP20(i915)			(IS_DG2(i915) || DISPLAY_VER(i915) >= 14)
#define HAS_DPT(i915)			(DISPLAY_VER(i915) >= 13)
#define HAS_DSB(i915)			(DISPLAY_INFO(i915)->has_dsb)
#define HAS_DSC(__i915)			(DISPLAY_RUNTIME_INFO(__i915)->has_dsc)
#define HAS_FBC(i915)			(DISPLAY_RUNTIME_INFO(i915)->fbc_mask != 0)
#define HAS_FPGA_DBG_UNCLAIMED(i915)	(DISPLAY_INFO(i915)->has_fpga_dbg)
#define HAS_FW_BLC(i915)		(DISPLAY_VER(i915) > 2)
#define HAS_GMBUS_IRQ(i915)		(DISPLAY_VER(i915) >= 4)
#define HAS_GMBUS_BURST_READ(i915)	(DISPLAY_VER(i915) >= 10 || IS_KABYLAKE(i915))
#define HAS_GMCH(i915)			(DISPLAY_INFO(i915)->has_gmch)
#define HAS_HW_SAGV_WM(i915)		(DISPLAY_VER(i915) >= 13 && !IS_DGFX(i915))
#define HAS_IPC(i915)			(DISPLAY_INFO(i915)->has_ipc)
#define HAS_IPS(i915)			(IS_HSW_ULT(i915) || IS_BROADWELL(i915))
#define HAS_LSPCON(i915)		(IS_DISPLAY_VER(i915, 9, 10))
#define HAS_MBUS_JOINING(i915)		(IS_ALDERLAKE_P(i915) || DISPLAY_VER(i915) >= 14)
#define HAS_MSO(i915)			(DISPLAY_VER(i915) >= 12)
#define HAS_OVERLAY(i915)		(DISPLAY_INFO(i915)->has_overlay)
#define HAS_PSR(i915)			(DISPLAY_INFO(i915)->has_psr)
#define HAS_PSR_HW_TRACKING(i915)	(DISPLAY_INFO(i915)->has_psr_hw_tracking)
#define HAS_PSR2_SEL_FETCH(i915)	(DISPLAY_VER(i915) >= 12)
#define HAS_SAGV(i915)			(DISPLAY_VER(i915) >= 9 && !IS_LP(i915))
#define HAS_TRANSCODER(i915, trans)	((DISPLAY_RUNTIME_INFO(i915)->cpu_transcoder_mask & \
					  BIT(trans)) != 0)
#define HAS_VRR(i915)			(DISPLAY_VER(i915) >= 11)
#define INTEL_NUM_PIPES(i915)		(hweight8(DISPLAY_RUNTIME_INFO(i915)->pipe_mask))
#define I915_HAS_HOTPLUG(i915)		(DISPLAY_INFO(i915)->has_hotplug)
#define OVERLAY_NEEDS_PHYSICAL(i915)	(DISPLAY_INFO(i915)->overlay_needs_physical)
#define SUPPORTS_TV(i915)		(DISPLAY_INFO(i915)->supports_tv)

struct intel_display_runtime_info {
	struct {
		u16 ver;
		u16 rel;
		u16 step;
	} ip;

	u8 pipe_mask;
	u8 cpu_transcoder_mask;
	u16 port_mask;

	u8 num_sprites[I915_MAX_PIPES];
	u8 num_scalers[I915_MAX_PIPES];

	u8 fbc_mask;

	bool has_hdcp;
	bool has_dmc;
	bool has_dsc;
};

struct intel_display_device_info {
	/* Initial runtime info. */
	const struct intel_display_runtime_info __runtime_defaults;

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

const struct intel_display_device_info *
intel_display_device_probe(struct drm_i915_private *i915, bool has_gmdid,
			   u16 *ver, u16 *rel, u16 *step);
void intel_display_device_info_runtime_init(struct drm_i915_private *i915);

void intel_display_device_info_print(const struct intel_display_device_info *info,
				     const struct intel_display_runtime_info *runtime,
				     struct drm_printer *p);

#endif
