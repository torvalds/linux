/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_DEVICE_H__
#define __INTEL_DISPLAY_DEVICE_H__

#include <linux/bitops.h>
#include <linux/types.h>

#include "intel_display_conversion.h"
#include "intel_display_limits.h"

struct drm_i915_private;
struct drm_printer;

/*
 * Display platforms and subplatforms. Keep platforms in display version based
 * order, chronological order within a version, and subplatforms next to the
 * platform.
 */
#define INTEL_DISPLAY_PLATFORMS(func) \
	/* Display ver 2 */ \
	func(i830) \
	func(i845g) \
	func(i85x) \
	func(i865g) \
	/* Display ver 3 */ \
	func(i915g) \
	func(i915gm) \
	func(i945g) \
	func(i945gm) \
	func(g33) \
	func(pineview) \
	/* Display ver 4 */ \
	func(i965g) \
	func(i965gm) \
	func(g45) \
	func(gm45) \
	func(g4x) /* group alias for g45 and gm45 */ \
	/* Display ver 5 */ \
	func(ironlake) \
	/* Display ver 6 */ \
	func(sandybridge) \
	/* Display ver 7 */ \
	func(ivybridge) \
	func(valleyview) \
	func(haswell) \
	func(haswell_ult) \
	func(haswell_ulx) \
	/* Display ver 8 */ \
	func(broadwell) \
	func(broadwell_ult) \
	func(broadwell_ulx) \
	func(cherryview) \
	/* Display ver 9 */ \
	func(skylake) \
	func(skylake_ult) \
	func(skylake_ulx) \
	func(broxton) \
	func(kabylake) \
	func(kabylake_ult) \
	func(kabylake_ulx) \
	func(geminilake) \
	func(coffeelake) \
	func(coffeelake_ult) \
	func(coffeelake_ulx) \
	func(cometlake) \
	func(cometlake_ult) \
	func(cometlake_ulx) \
	/* Display ver 11 */ \
	func(icelake) \
	func(icelake_port_f) \
	func(jasperlake) \
	func(elkhartlake) \
	/* Display ver 12 */ \
	func(tigerlake) \
	func(tigerlake_uy) \
	func(rocketlake) \
	func(dg1) \
	func(alderlake_s) \
	func(alderlake_s_raptorlake_s) \
	/* Display ver 13 */ \
	func(alderlake_p) \
	func(alderlake_p_alderlake_n) \
	func(alderlake_p_raptorlake_p) \
	func(alderlake_p_raptorlake_u) \
	func(dg2) \
	func(dg2_g10) \
	func(dg2_g11) \
	func(dg2_g12) \
	/* Display ver 14 (based on GMD ID) */ \
	func(meteorlake) \
	/* Display ver 20 (based on GMD ID) */ \
	func(lunarlake) \
	/* Display ver 14.1 (based on GMD ID) */ \
	func(battlemage) \
	/* Display ver 30 (based on GMD ID) */ \
	func(pantherlake)

#define __MEMBER(name) unsigned long name:1;
#define __COUNT(x) 1 +

#define __NUM_PLATFORMS (INTEL_DISPLAY_PLATFORMS(__COUNT) 0)

struct intel_display_platforms {
	union {
		struct {
			INTEL_DISPLAY_PLATFORMS(__MEMBER);
		};
		DECLARE_BITMAP(bitmap, __NUM_PLATFORMS);
	};
};

#undef __MEMBER
#undef __COUNT
#undef __NUM_PLATFORMS

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

#define HAS_4TILE(i915)			(IS_DG2(i915) || DISPLAY_VER(i915) >= 14)
#define HAS_ASYNC_FLIPS(i915)		(DISPLAY_VER(i915) >= 5)
#define HAS_BIGJOINER(i915)		(DISPLAY_VER(i915) >= 11 && HAS_DSC(i915))
#define HAS_CDCLK_CRAWL(i915)		(DISPLAY_INFO(i915)->has_cdclk_crawl)
#define HAS_CDCLK_SQUASH(i915)		(DISPLAY_INFO(i915)->has_cdclk_squash)
#define HAS_CUR_FBC(i915)		(!HAS_GMCH(i915) && IS_DISPLAY_VER(i915, 7, 13))
#define HAS_D12_PLANE_MINIMIZATION(i915) (IS_ROCKETLAKE(i915) || IS_ALDERLAKE_S(i915))
#define HAS_DBUF_OVERLAP_DETECTION(__i915) (DISPLAY_RUNTIME_INFO(__i915)->has_dbuf_overlap_detection)
#define HAS_DDI(i915)			(DISPLAY_INFO(i915)->has_ddi)
#define HAS_DISPLAY(i915)		(DISPLAY_RUNTIME_INFO(i915)->pipe_mask != 0)
#define HAS_DMC(i915)			(DISPLAY_RUNTIME_INFO(i915)->has_dmc)
#define HAS_DOUBLE_BUFFERED_M_N(i915)	(DISPLAY_VER(i915) >= 9 || IS_BROADWELL(i915))
#define HAS_DP_MST(i915)		(DISPLAY_INFO(i915)->has_dp_mst)
#define HAS_DP20(i915)			(IS_DG2(i915) || DISPLAY_VER(i915) >= 14)
#define HAS_DPT(i915)			(DISPLAY_VER(i915) >= 13)
#define HAS_DSB(i915)			(DISPLAY_INFO(i915)->has_dsb)
#define HAS_DSC(__i915)			(DISPLAY_RUNTIME_INFO(__i915)->has_dsc)
#define HAS_DSC_MST(__i915)		(DISPLAY_VER(__i915) >= 12 && HAS_DSC(__i915))
#define HAS_FBC(i915)			(DISPLAY_RUNTIME_INFO(i915)->fbc_mask != 0)
#define HAS_FPGA_DBG_UNCLAIMED(i915)	(DISPLAY_INFO(i915)->has_fpga_dbg)
#define HAS_FW_BLC(i915)		(DISPLAY_VER(i915) >= 3)
#define HAS_GMBUS_IRQ(i915)		(DISPLAY_VER(i915) >= 4)
#define HAS_GMBUS_BURST_READ(i915)	(DISPLAY_VER(i915) >= 10 || IS_KABYLAKE(i915))
#define HAS_GMCH(i915)			(DISPLAY_INFO(i915)->has_gmch)
#define HAS_HW_SAGV_WM(i915)		(DISPLAY_VER(i915) >= 13 && !IS_DGFX(i915))
#define HAS_IPC(i915)			(DISPLAY_INFO(i915)->has_ipc)
#define HAS_IPS(i915)			(IS_HASWELL_ULT(i915) || IS_BROADWELL(i915))
#define HAS_LRR(i915)			(DISPLAY_VER(i915) >= 12)
#define HAS_LSPCON(i915)		(IS_DISPLAY_VER(i915, 9, 10))
#define HAS_MBUS_JOINING(i915)		(IS_ALDERLAKE_P(i915) || DISPLAY_VER(i915) >= 14)
#define HAS_MSO(i915)			(DISPLAY_VER(i915) >= 12)
#define HAS_OVERLAY(i915)		(DISPLAY_INFO(i915)->has_overlay)
#define HAS_PSR(i915)			(DISPLAY_INFO(i915)->has_psr)
#define HAS_PSR_HW_TRACKING(i915)	(DISPLAY_INFO(i915)->has_psr_hw_tracking)
#define HAS_PSR2_SEL_FETCH(i915)	(DISPLAY_VER(i915) >= 12)
#define HAS_SAGV(i915)			(DISPLAY_VER(i915) >= 9 && !IS_BROXTON(i915) && !IS_GEMINILAKE(i915))
#define HAS_TRANSCODER(i915, trans)	((DISPLAY_RUNTIME_INFO(i915)->cpu_transcoder_mask & \
					  BIT(trans)) != 0)
#define HAS_UNCOMPRESSED_JOINER(i915)	(DISPLAY_VER(i915) >= 13)
#define HAS_ULTRAJOINER(i915)		((DISPLAY_VER(i915) >= 20 || \
					  (IS_DGFX(i915) && DISPLAY_VER(i915) == 14)) && \
					 HAS_DSC(i915))
#define HAS_VRR(i915)			(DISPLAY_VER(i915) >= 11)
#define HAS_AS_SDP(i915)		(DISPLAY_VER(i915) >= 13)
#define HAS_CMRR(i915)			(DISPLAY_VER(i915) >= 20)
#define INTEL_NUM_PIPES(i915)		(hweight8(DISPLAY_RUNTIME_INFO(i915)->pipe_mask))
#define I915_HAS_HOTPLUG(i915)		(DISPLAY_INFO(i915)->has_hotplug)
#define OVERLAY_NEEDS_PHYSICAL(i915)	(DISPLAY_INFO(i915)->overlay_needs_physical)
#define SUPPORTS_TV(i915)		(DISPLAY_INFO(i915)->supports_tv)

/* Check that device has a display IP version within the specific range. */
#define IS_DISPLAY_VERx100(__i915, from, until) ( \
	BUILD_BUG_ON_ZERO((from) < 200) + \
	(DISPLAY_VERx100(__i915) >= (from) && \
	 DISPLAY_VERx100(__i915) <= (until)))

/*
 * Check if a device has a specific IP version as well as a stepping within the
 * specified range [from, until).  The lower bound is inclusive, the upper
 * bound is exclusive.  The most common use-case of this macro is for checking
 * bounds for workarounds, which usually have a stepping ("from") at which the
 * hardware issue is first present and another stepping ("until") at which a
 * hardware fix is present and the software workaround is no longer necessary.
 * E.g.,
 *
 *    IS_DISPLAY_VERx100_STEP(i915, 1400, STEP_A0, STEP_B2)
 *    IS_DISPLAY_VERx100_STEP(i915, 1400, STEP_C0, STEP_FOREVER)
 *
 * "STEP_FOREVER" can be passed as "until" for workarounds that have no upper
 * stepping bound for the specified IP version.
 */
#define IS_DISPLAY_VERx100_STEP(__i915, ipver, from, until) \
	(IS_DISPLAY_VERx100((__i915), (ipver), (ipver)) && \
	 IS_DISPLAY_STEP((__i915), (from), (until)))

#define DISPLAY_INFO(i915)		(__to_intel_display(i915)->info.__device_info)
#define DISPLAY_RUNTIME_INFO(i915)	(&__to_intel_display(i915)->info.__runtime_info)

#define DISPLAY_VER(i915)	(DISPLAY_RUNTIME_INFO(i915)->ip.ver)
#define DISPLAY_VERx100(i915)	(DISPLAY_RUNTIME_INFO(i915)->ip.ver * 100 + \
				 DISPLAY_RUNTIME_INFO(i915)->ip.rel)
#define IS_DISPLAY_VER(i915, from, until) \
	(DISPLAY_VER(i915) >= (from) && DISPLAY_VER(i915) <= (until))

#define INTEL_DISPLAY_STEP(__i915) (DISPLAY_RUNTIME_INFO(__i915)->step)

#define IS_DISPLAY_STEP(__i915, since, until) \
	(drm_WARN_ON(__to_intel_display(__i915)->drm, INTEL_DISPLAY_STEP(__i915) == STEP_NONE), \
	 INTEL_DISPLAY_STEP(__i915) >= (since) && INTEL_DISPLAY_STEP(__i915) < (until))

struct intel_display_runtime_info {
	struct intel_display_ip_ver {
		u16 ver;
		u16 rel;
		u16 step; /* hardware */
	} ip;
	int step; /* symbolic */

	u32 rawclk_freq;

	u8 pipe_mask;
	u8 cpu_transcoder_mask;
	u16 port_mask;

	u8 num_sprites[I915_MAX_PIPES];
	u8 num_scalers[I915_MAX_PIPES];

	u8 fbc_mask;

	bool has_hdcp;
	bool has_dmc;
	bool has_dsc;
	bool edp_typec_support;
	bool has_dbuf_overlap_detection;
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

bool intel_display_device_enabled(struct drm_i915_private *i915);
void intel_display_device_probe(struct drm_i915_private *i915);
void intel_display_device_remove(struct drm_i915_private *i915);
void intel_display_device_info_runtime_init(struct drm_i915_private *i915);

void intel_display_device_info_print(const struct intel_display_device_info *info,
				     const struct intel_display_runtime_info *runtime,
				     struct drm_printer *p);

#endif
