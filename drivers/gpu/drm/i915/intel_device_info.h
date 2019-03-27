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

#include "intel_display.h"

struct drm_printer;
struct drm_i915_private;

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
	/* gen10 */
	INTEL_CANNONLAKE,
	/* gen11 */
	INTEL_ICELAKE,
	INTEL_ELKHARTLAKE,
	INTEL_MAX_PLATFORMS
};

/*
 * Subplatform bits share the same namespace per parent platform. In other words
 * it is fine for the same bit to be used on multiple parent platforms.
 */

#define INTEL_SUBPLATFORM_BITS (3)

/* HSW/BDW/SKL/KBL/CFL */
#define INTEL_SUBPLATFORM_ULT	(0)
#define INTEL_SUBPLATFORM_ULX	(1)
#define INTEL_SUBPLATFORM_AML	(2)

/* CNL/ICL */
#define INTEL_SUBPLATFORM_PORTF	(0)

enum intel_ppgtt_type {
	INTEL_PPGTT_NONE = I915_GEM_PPGTT_NONE,
	INTEL_PPGTT_ALIASING = I915_GEM_PPGTT_ALIASING,
	INTEL_PPGTT_FULL = I915_GEM_PPGTT_FULL,
};

#define DEV_INFO_FOR_EACH_FLAG(func) \
	func(is_mobile); \
	func(is_lp); \
	func(is_alpha_support); \
	/* Keep has_* in alphabetical order */ \
	func(has_64bit_reloc); \
	func(gpu_reset_clobbers_display); \
	func(has_reset_engine); \
	func(has_fpga_dbg); \
	func(has_guc); \
	func(has_guc_ct); \
	func(has_l3_dpf); \
	func(has_llc); \
	func(has_logical_ring_contexts); \
	func(has_logical_ring_elsq); \
	func(has_logical_ring_preemption); \
	func(has_pooled_eu); \
	func(has_rc6); \
	func(has_rc6p); \
	func(has_runtime_pm); \
	func(has_snoop); \
	func(has_coherent_ggtt); \
	func(unfenced_needs_alignment); \
	func(hws_needs_physical);

#define DEV_INFO_DISPLAY_FOR_EACH_FLAG(func) \
	/* Keep in alphabetical order */ \
	func(cursor_needs_physical); \
	func(has_csr); \
	func(has_ddi); \
	func(has_dp_mst); \
	func(has_fbc); \
	func(has_gmch); \
	func(has_hotplug); \
	func(has_ipc); \
	func(has_overlay); \
	func(has_psr); \
	func(overlay_needs_physical); \
	func(supports_tv);

#define GEN_MAX_SLICES		(6) /* CNL upper bound */
#define GEN_MAX_SUBSLICES	(8) /* ICL upper bound */

struct sseu_dev_info {
	u8 slice_mask;
	u8 subslice_mask[GEN_MAX_SLICES];
	u16 eu_total;
	u8 eu_per_subslice;
	u8 min_eu_in_pool;
	/* For each slice, which subslice(s) has(have) 7 EUs (bitfield)? */
	u8 subslice_7eu[3];
	u8 has_slice_pg:1;
	u8 has_subslice_pg:1;
	u8 has_eu_pg:1;

	/* Topology fields */
	u8 max_slices;
	u8 max_subslices;
	u8 max_eus_per_subslice;

	/* We don't have more than 8 eus per subslice at the moment and as we
	 * store eus enabled using bits, no need to multiply by eus per
	 * subslice.
	 */
	u8 eu_mask[GEN_MAX_SLICES * GEN_MAX_SUBSLICES];
};

typedef u8 intel_engine_mask_t;

struct intel_device_info {
	u16 gen_mask;

	u8 gen;
	u8 gt; /* GT number, 0 if undefined */
	intel_engine_mask_t engine_mask; /* Engines supported by the HW */

	enum intel_platform platform;

	enum intel_ppgtt_type ppgtt_type;
	unsigned int ppgtt_size; /* log2, e.g. 31/32/48 bits */

	unsigned int page_sizes; /* page sizes supported by the HW */

	u32 display_mmio_offset;

	u8 num_pipes;

#define DEFINE_FLAG(name) u8 name:1
	DEV_INFO_FOR_EACH_FLAG(DEFINE_FLAG);
#undef DEFINE_FLAG

	struct {
#define DEFINE_FLAG(name) u8 name:1
		DEV_INFO_DISPLAY_FOR_EACH_FLAG(DEFINE_FLAG);
#undef DEFINE_FLAG
	} display;

	u16 ddb_size; /* in blocks */

	/* Register offsets for the various display pipes and transcoders */
	int pipe_offsets[I915_MAX_TRANSCODERS];
	int trans_offsets[I915_MAX_TRANSCODERS];
	int cursor_offsets[I915_MAX_PIPES];

	struct color_luts {
		u16 degamma_lut_size;
		u16 gamma_lut_size;
		u32 degamma_lut_tests;
		u32 gamma_lut_tests;
	} color;
};

struct intel_runtime_info {
	/*
	 * Platform mask is used for optimizing or-ed IS_PLATFORM calls into
	 * into single runtime conditionals, and also to provide groundwork
	 * for future per platform, or per SKU build optimizations.
	 *
	 * Array can be extended when necessary if the corresponding
	 * BUILD_BUG_ON is hit.
	 */
	u32 platform_mask[2];

	u16 device_id;

	u8 num_sprites[I915_MAX_PIPES];
	u8 num_scalers[I915_MAX_PIPES];

	u8 num_engines;

	/* Slice/subslice/EU info */
	struct sseu_dev_info sseu;

	u32 cs_timestamp_frequency_khz;

	/* Media engine access to SFC per instance */
	u8 vdbox_sfc_access;
};

struct intel_driver_caps {
	unsigned int scheduler;
	bool has_logical_contexts:1;
};

static inline unsigned int sseu_subslice_total(const struct sseu_dev_info *sseu)
{
	unsigned int i, total = 0;

	for (i = 0; i < ARRAY_SIZE(sseu->subslice_mask); i++)
		total += hweight8(sseu->subslice_mask[i]);

	return total;
}

static inline int sseu_eu_idx(const struct sseu_dev_info *sseu,
			      int slice, int subslice)
{
	int subslice_stride = DIV_ROUND_UP(sseu->max_eus_per_subslice,
					   BITS_PER_BYTE);
	int slice_stride = sseu->max_subslices * subslice_stride;

	return slice * slice_stride + subslice * subslice_stride;
}

static inline u16 sseu_get_eus(const struct sseu_dev_info *sseu,
			       int slice, int subslice)
{
	int i, offset = sseu_eu_idx(sseu, slice, subslice);
	u16 eu_mask = 0;

	for (i = 0;
	     i < DIV_ROUND_UP(sseu->max_eus_per_subslice, BITS_PER_BYTE); i++) {
		eu_mask |= ((u16) sseu->eu_mask[offset + i]) <<
			(i * BITS_PER_BYTE);
	}

	return eu_mask;
}

static inline void sseu_set_eus(struct sseu_dev_info *sseu,
				int slice, int subslice, u16 eu_mask)
{
	int i, offset = sseu_eu_idx(sseu, slice, subslice);

	for (i = 0;
	     i < DIV_ROUND_UP(sseu->max_eus_per_subslice, BITS_PER_BYTE); i++) {
		sseu->eu_mask[offset + i] =
			(eu_mask >> (BITS_PER_BYTE * i)) & 0xff;
	}
}

const char *intel_platform_name(enum intel_platform platform);

void intel_device_info_subplatform_init(struct drm_i915_private *dev_priv);
void intel_device_info_runtime_init(struct drm_i915_private *dev_priv);
void intel_device_info_dump_flags(const struct intel_device_info *info,
				  struct drm_printer *p);
void intel_device_info_dump_runtime(const struct intel_runtime_info *info,
				    struct drm_printer *p);
void intel_device_info_dump_topology(const struct sseu_dev_info *sseu,
				     struct drm_printer *p);

void intel_device_info_init_mmio(struct drm_i915_private *dev_priv);

void intel_driver_caps_print(const struct intel_driver_caps *caps,
			     struct drm_printer *p);

#endif
