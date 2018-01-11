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
	INTEL_MAX_PLATFORMS
};

#define DEV_INFO_FOR_EACH_FLAG(func) \
	func(is_mobile); \
	func(is_lp); \
	func(is_alpha_support); \
	/* Keep has_* in alphabetical order */ \
	func(has_64bit_reloc); \
	func(has_aliasing_ppgtt); \
	func(has_csr); \
	func(has_ddi); \
	func(has_dp_mst); \
	func(has_reset_engine); \
	func(has_fbc); \
	func(has_fpga_dbg); \
	func(has_full_ppgtt); \
	func(has_full_48bit_ppgtt); \
	func(has_gmch_display); \
	func(has_guc); \
	func(has_guc_ct); \
	func(has_hotplug); \
	func(has_l3_dpf); \
	func(has_llc); \
	func(has_logical_ring_contexts); \
	func(has_logical_ring_preemption); \
	func(has_overlay); \
	func(has_pooled_eu); \
	func(has_psr); \
	func(has_rc6); \
	func(has_rc6p); \
	func(has_resource_streamer); \
	func(has_runtime_pm); \
	func(has_snoop); \
	func(unfenced_needs_alignment); \
	func(cursor_needs_physical); \
	func(hws_needs_physical); \
	func(overlay_needs_physical); \
	func(supports_tv); \
	func(has_ipc);

struct sseu_dev_info {
	u8 slice_mask;
	u8 subslice_mask;
	u8 eu_total;
	u8 eu_per_subslice;
	u8 min_eu_in_pool;
	/* For each slice, which subslice(s) has(have) 7 EUs (bitfield)? */
	u8 subslice_7eu[3];
	u8 has_slice_pg:1;
	u8 has_subslice_pg:1;
	u8 has_eu_pg:1;
};

struct intel_device_info {
	u16 device_id;
	u16 gen_mask;

	u8 gen;
	u8 gt; /* GT number, 0 if undefined */
	u8 num_rings;
	u8 ring_mask; /* Rings supported by the HW */

	enum intel_platform platform;
	u32 platform_mask;

	u32 display_mmio_offset;

	u8 num_pipes;
	u8 num_sprites[I915_MAX_PIPES];
	u8 num_scalers[I915_MAX_PIPES];

	unsigned int page_sizes; /* page sizes supported by the HW */

#define DEFINE_FLAG(name) u8 name:1
	DEV_INFO_FOR_EACH_FLAG(DEFINE_FLAG);
#undef DEFINE_FLAG
	u16 ddb_size; /* in blocks */

	/* Register offsets for the various display pipes and transcoders */
	int pipe_offsets[I915_MAX_TRANSCODERS];
	int trans_offsets[I915_MAX_TRANSCODERS];
	int palette_offsets[I915_MAX_PIPES];
	int cursor_offsets[I915_MAX_PIPES];

	/* Slice/subslice/EU info */
	struct sseu_dev_info sseu;

	u32 cs_timestamp_frequency_khz;

	struct color_luts {
		u16 degamma_lut_size;
		u16 gamma_lut_size;
	} color;
};

static inline unsigned int sseu_subslice_total(const struct sseu_dev_info *sseu)
{
	return hweight8(sseu->slice_mask) * hweight8(sseu->subslice_mask);
}

const char *intel_platform_name(enum intel_platform platform);

void intel_device_info_runtime_init(struct intel_device_info *info);
void intel_device_info_dump(const struct intel_device_info *info,
			    struct drm_printer *p);
void intel_device_info_dump_flags(const struct intel_device_info *info,
				  struct drm_printer *p);
void intel_device_info_dump_runtime(const struct intel_device_info *info,
				    struct drm_printer *p);

#endif
