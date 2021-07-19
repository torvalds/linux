/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_SSEU_H__
#define __INTEL_SSEU_H__

#include <linux/types.h>
#include <linux/kernel.h>

#include "i915_gem.h"

struct drm_i915_private;
struct intel_gt;
struct drm_printer;

#define GEN_MAX_SLICES		(6) /* CNL upper bound */
#define GEN_MAX_SUBSLICES	(8) /* ICL upper bound */
#define GEN_SSEU_STRIDE(max_entries) DIV_ROUND_UP(max_entries, BITS_PER_BYTE)
#define GEN_MAX_SUBSLICE_STRIDE GEN_SSEU_STRIDE(GEN_MAX_SUBSLICES)
#define GEN_MAX_EUS		(16) /* TGL upper bound */
#define GEN_MAX_EU_STRIDE GEN_SSEU_STRIDE(GEN_MAX_EUS)

struct sseu_dev_info {
	u8 slice_mask;
	u8 subslice_mask[GEN_MAX_SLICES * GEN_MAX_SUBSLICE_STRIDE];
	u8 eu_mask[GEN_MAX_SLICES * GEN_MAX_SUBSLICES * GEN_MAX_EU_STRIDE];
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

	u8 ss_stride;
	u8 eu_stride;
};

/*
 * Powergating configuration for a particular (context,engine).
 */
struct intel_sseu {
	u8 slice_mask;
	u8 subslice_mask;
	u8 min_eus_per_subslice;
	u8 max_eus_per_subslice;
};

static inline struct intel_sseu
intel_sseu_from_device_info(const struct sseu_dev_info *sseu)
{
	struct intel_sseu value = {
		.slice_mask = sseu->slice_mask,
		.subslice_mask = sseu->subslice_mask[0],
		.min_eus_per_subslice = sseu->max_eus_per_subslice,
		.max_eus_per_subslice = sseu->max_eus_per_subslice,
	};

	return value;
}

static inline bool
intel_sseu_has_subslice(const struct sseu_dev_info *sseu, int slice,
			int subslice)
{
	u8 mask;
	int ss_idx = subslice / BITS_PER_BYTE;

	GEM_BUG_ON(ss_idx >= sseu->ss_stride);

	mask = sseu->subslice_mask[slice * sseu->ss_stride + ss_idx];

	return mask & BIT(subslice % BITS_PER_BYTE);
}

void intel_sseu_set_info(struct sseu_dev_info *sseu, u8 max_slices,
			 u8 max_subslices, u8 max_eus_per_subslice);

unsigned int
intel_sseu_subslice_total(const struct sseu_dev_info *sseu);

unsigned int
intel_sseu_subslices_per_slice(const struct sseu_dev_info *sseu, u8 slice);

u32  intel_sseu_get_subslices(const struct sseu_dev_info *sseu, u8 slice);

void intel_sseu_set_subslices(struct sseu_dev_info *sseu, int slice,
			      u32 ss_mask);

void intel_sseu_info_init(struct intel_gt *gt);

u32 intel_sseu_make_rpcs(struct intel_gt *gt,
			 const struct intel_sseu *req_sseu);

void intel_sseu_dump(const struct sseu_dev_info *sseu, struct drm_printer *p);
void intel_sseu_print_topology(const struct sseu_dev_info *sseu,
			       struct drm_printer *p);

#endif /* __INTEL_SSEU_H__ */
