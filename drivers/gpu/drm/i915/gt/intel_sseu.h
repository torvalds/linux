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

/*
 * Maximum number of slices on older platforms.  Slices no longer exist
 * starting on Xe_HP ("gslices," "cslices," etc. are a different concept and
 * are not expressed through fusing).
 */
#define GEN_MAX_HSW_SLICES		3

/*
 * Maximum number of subslices that can exist within a HSW-style slice.  This
 * is only relevant to pre-Xe_HP platforms (Xe_HP and beyond use the
 * GEN_MAX_DSS value below).
 */
#define GEN_MAX_SS_PER_HSW_SLICE	6

/* Maximum number of DSS on newer platforms (Xe_HP and beyond). */
#define GEN_MAX_DSS			32

/* Maximum number of EUs that can exist within a subslice or DSS. */
#define GEN_MAX_EUS_PER_SS		16

#define SSEU_MAX(a, b)			((a) > (b) ? (a) : (b))

/* The maximum number of bits needed to express each subslice/DSS independently */
#define GEN_SS_MASK_SIZE		SSEU_MAX(GEN_MAX_DSS, \
						 GEN_MAX_HSW_SLICES * GEN_MAX_SS_PER_HSW_SLICE)

#define GEN_SSEU_STRIDE(max_entries)	DIV_ROUND_UP(max_entries, BITS_PER_BYTE)
#define GEN_MAX_SUBSLICE_STRIDE		GEN_SSEU_STRIDE(GEN_SS_MASK_SIZE)
#define GEN_MAX_EU_STRIDE		GEN_SSEU_STRIDE(GEN_MAX_EUS_PER_SS)

#define GEN_DSS_PER_GSLICE	4
#define GEN_DSS_PER_CSLICE	8
#define GEN_DSS_PER_MSLICE	8

#define GEN_MAX_GSLICES		(GEN_MAX_DSS / GEN_DSS_PER_GSLICE)
#define GEN_MAX_CSLICES		(GEN_MAX_DSS / GEN_DSS_PER_CSLICE)

struct sseu_dev_info {
	u8 slice_mask;
	u8 subslice_mask[GEN_SS_MASK_SIZE];
	u8 geometry_subslice_mask[GEN_SS_MASK_SIZE];
	u8 compute_subslice_mask[GEN_SS_MASK_SIZE];
	u8 eu_mask[GEN_SS_MASK_SIZE * GEN_MAX_EU_STRIDE];
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

	if (slice >= sseu->max_slices ||
	    subslice >= sseu->max_subslices)
		return false;

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

u32 intel_sseu_get_subslices(const struct sseu_dev_info *sseu, u8 slice);

u32 intel_sseu_get_compute_subslices(const struct sseu_dev_info *sseu);

void intel_sseu_set_subslices(struct sseu_dev_info *sseu, int slice,
			      u8 *subslice_mask, u32 ss_mask);

void intel_sseu_info_init(struct intel_gt *gt);

u32 intel_sseu_make_rpcs(struct intel_gt *gt,
			 const struct intel_sseu *req_sseu);

void intel_sseu_dump(const struct sseu_dev_info *sseu, struct drm_printer *p);
void intel_sseu_print_topology(struct drm_i915_private *i915,
			       const struct sseu_dev_info *sseu,
			       struct drm_printer *p);

u16 intel_slicemask_from_dssmask(u64 dss_mask, int dss_per_slice);

#endif /* __INTEL_SSEU_H__ */
