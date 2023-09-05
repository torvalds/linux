/*
 * Copyright © 2014-2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifndef _INTEL_DEVICE_INFO_H_
#define _INTEL_DEVICE_INFO_H_

#include <uapi/drm/i915_drm.h>

#include "intel_step.h"

#include "display/intel_display_device.h"

#include "gt/intel_engine_types.h"
#include "gt/intel_context_types.h"
#include "gt/intel_sseu.h"

#include "gem/i915_gem_object_types.h"

struct drm_printer;
struct drm_i915_private;
struct intel_gt_definition;

/* Keep in gen based order, and chronological order within a gen */
enum intel_platform {
	INTEL_PLATFORM_UNINITIALIZED = 0,
	/* gen2 */
	INTEL_I830,
	INTEL_I845G,
	INTEL_I85X,
	INTEL_I865G,
	/* gen3 */
	INTEL_I915G,
	INTEL_I915GM,
	INTEL_I945G,
	INTEL_I945GM,
	INTEL_G33,
	INTEL_PINEVIEW,
	/* gen4 */
	INTEL_I965G,
	INTEL_I965GM,
	INTEL_G45,
	INTEL_GM45,
	/* gen5 */
	INTEL_IRONLAKE,
	/* gen6 */
	INTEL_SANDYBRIDGE,
	/* gen7 */
	INTEL_IVYBRIDGE,
	INTEL_VALLEYVIEW,
	INTEL_HASWELL,
	/* gen8 */
	INTEL_BROADWELL,
	INTEL_CHERRYVIEW,
	/* gen9 */
	INTEL_SKYLAKE,
	INTEL_BROXTON,
	INTEL_KABYLAKE,
	INTEL_GEMINILAKE,
	INTEL_COFFEELAKE,
	INTEL_COMETLAKE,
	/* gen11 */
	INTEL_ICELAKE,
	INTEL_ELKHARTLAKE,
	INTEL_JASPERLAKE,
	/* gen12 */
	INTEL_TIGERLAKE,
	INTEL_ROCKETLAKE,
	INTEL_DG1,
	INTEL_ALDERLAKE_S,
	INTEL_ALDERLAKE_P,
	INTEL_XEHPSDV,
	INTEL_DG2,
	INTEL_PONTEVECCHIO,
	INTEL_METEORLAKE,
	INTEL_MAX_PLATFORMS
};

/*
 * Subplatform bits share the same namespace per parent platform. In other words
 * it is fine for the same bit to be used on multiple parent platforms.
 */

#define INTEL_SUBPLATFORM_BITS (3)
#define INTEL_SUBPLATFORM_MASK (BIT(INTEL_SUBPLATFORM_BITS) - 1)

/* HSW/BDW/SKL/KBL/CFL */
#define INTEL_SUBPLATFORM_ULT	(0)
#define INTEL_SUBPLATFORM_ULX	(1)

/* ICL */
#define INTEL_SUBPLATFORM_PORTF	(0)

/* TGL */
#define INTEL_SUBPLATFORM_UY	(0)

/* DG2 */
#define INTEL_SUBPLATFORM_G10	0
#define INTEL_SUBPLATFORM_G11	1
#define INTEL_SUBPLATFORM_G12	2

/* ADL */
#define INTEL_SUBPLATFORM_RPL	0

/* ADL-P */
/*
 * As #define INTEL_SUBPLATFORM_RPL 0 will apply
 * here too, SUBPLATFORM_N will have different
 * bit set
 */
#define INTEL_SUBPLATFORM_N    1
#define INTEL_SUBPLATFORM_RPLU  2

/* MTL */
#define INTEL_SUBPLATFORM_M	0
#define INTEL_SUBPLATFORM_P	1

enum intel_ppgtt_type {
	INTEL_PPGTT_NONE = I915_GEM_PPGTT_NONE,
	INTEL_PPGTT_ALIASING = I915_GEM_PPGTT_ALIASING,
	INTEL_PPGTT_FULL = I915_GEM_PPGTT_FULL,
};

#define DEV_INFO_FOR_EACH_FLAG(func) \
	func(is_mobile); \
	func(is_lp); \
	func(require_force_probe); \
	func(is_dgfx); \
	/* Keep has_* in alphabetical order */ \
	func(has_64bit_reloc); \
	func(has_64k_pages); \
	func(gpu_reset_clobbers_display); \
	func(has_reset_engine); \
	func(has_3d_pipeline); \
	func(has_4tile); \
	func(has_flat_ccs); \
	func(has_global_mocs); \
	func(has_gmd_id); \
	func(has_gt_uc); \
	func(has_heci_pxp); \
	func(has_heci_gscfi); \
	func(has_guc_deprivilege); \
	func(has_l3_ccs_read); \
	func(has_l3_dpf); \
	func(has_llc); \
	func(has_logical_ring_contexts); \
	func(has_logical_ring_elsq); \
	func(has_media_ratio_mode); \
	func(has_mslice_steering); \
	func(has_oa_bpc_reporting); \
	func(has_oa_slice_contrib_limits); \
	func(has_oam); \
	func(has_one_eu_per_fuse_bit); \
	func(has_pxp); \
	func(has_rc6); \
	func(has_rc6p); \
	func(has_rps); \
	func(has_runtime_pm); \
	func(has_snoop); \
	func(has_coherent_ggtt); \
	func(tuning_thread_rr_after_dep); \
	func(unfenced_needs_alignment); \
	func(hws_needs_physical);

struct intel_ip_version {
	u8 ver;
	u8 rel;
	u8 step;
};

struct intel_runtime_info {
	/*
	 * Single "graphics" IP version that represents
	 * render, compute and copy behavior.
	 */
	struct {
		struct intel_ip_version ip;
	} graphics;
	struct {
		struct intel_ip_version ip;
	} media;

	/*
	 * Platform mask is used for optimizing or-ed IS_PLATFORM calls into
	 * single runtime conditionals, and also to provide groundwork for
	 * future per platform, or per SKU build optimizations.
	 *
	 * Array can be extended when necessary if the corresponding
	 * BUILD_BUG_ON is hit.
	 */
	u32 platform_mask[2];

	u16 device_id;

	intel_engine_mask_t platform_engine_mask; /* Engines supported by the HW */

	u32 rawclk_freq;

	struct intel_step_info step;

	unsigned int page_sizes; /* page sizes supported by the HW */

	enum intel_ppgtt_type ppgtt_type;
	unsigned int ppgtt_size; /* log2, e.g. 31/32/48 bits */

	u32 memory_regions; /* regions supported by the HW */

	bool has_pooled_eu;
};

struct intel_device_info {
	enum intel_platform platform;

	unsigned int dma_mask_size; /* available DMA address bits */

	const struct intel_gt_definition *extra_gt_list;

	u8 gt; /* GT number, 0 if undefined */

#define DEFINE_FLAG(name) u8 name:1
	DEV_INFO_FOR_EACH_FLAG(DEFINE_FLAG);
#undef DEFINE_FLAG

	const struct intel_display_device_info *display;

	/*
	 * Initial runtime info. Do not access outside of i915_driver_create().
	 */
	const struct intel_runtime_info __runtime;

	u32 cachelevel_to_pat[I915_MAX_CACHE_LEVEL];
	u32 max_pat_index;
};

struct intel_driver_caps {
	unsigned int scheduler;
	bool has_logical_contexts:1;
};

const char *intel_platform_name(enum intel_platform platform);

void intel_device_info_driver_create(struct drm_i915_private *i915, u16 device_id,
				     const struct intel_device_info *match_info);
void intel_device_info_runtime_init_early(struct drm_i915_private *dev_priv);
void intel_device_info_runtime_init(struct drm_i915_private *dev_priv);

void intel_device_info_print(const struct intel_device_info *info,
			     const struct intel_runtime_info *runtime,
			     struct drm_printer *p);

void intel_driver_caps_print(const struct intel_driver_caps *caps,
			     struct drm_printer *p);

#endif
