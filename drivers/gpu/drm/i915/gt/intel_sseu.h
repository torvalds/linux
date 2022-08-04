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
 * I915_MAX_SS_FUSE_BITS value below).
 */
#define GEN_MAX_SS_PER_HSW_SLICE	6

/*
 * Maximum number of 32-bit registers used by hardware to express the
 * enabled/disabled subslices.
 */
#define I915_MAX_SS_FUSE_REGS	2
#define I915_MAX_SS_FUSE_BITS	(I915_MAX_SS_FUSE_REGS * 32)

/* Maximum number of EUs that can exist within a subslice or DSS. */
#define GEN_MAX_EUS_PER_SS		16

#define SSEU_MAX(a, b)			((a) > (b) ? (a) : (b))

/* The maximum number of bits needed to express each subslice/DSS independently */
#define GEN_SS_MASK_SIZE		SSEU_MAX(I915_MAX_SS_FUSE_BITS, \
						 GEN_MAX_HSW_SLICES * GEN_MAX_SS_PER_HSW_SLICE)

#define GEN_SSEU_STRIDE(max_entries)	DIV_ROUND_UP(max_entries, BITS_PER_BYTE)
#define GEN_MAX_SUBSLICE_STRIDE		GEN_SSEU_STRIDE(GEN_SS_MASK_SIZE)
#define GEN_MAX_EU_STRIDE		GEN_SSEU_STRIDE(GEN_MAX_EUS_PER_SS)

#define GEN_DSS_PER_GSLICE	4
#define GEN_DSS_PER_CSLICE	8
#define GEN_DSS_PER_MSLICE	8

#define GEN_MAX_GSLICES		(I915_MAX_SS_FUSE_BITS / GEN_DSS_PER_GSLICE)
#define GEN_MAX_CSLICES		(I915_MAX_SS_FUSE_BITS / GEN_DSS_PER_CSLICE)

typedef union {
	u8 hsw[GEN_MAX_HSW_SLICES];

	/* Bitmap compatible with linux/bitmap.h; may exceed size of u64 */
	unsigned long xehp[BITS_TO_LONGS(I915_MAX_SS_FUSE_BITS)];
} intel_sseu_ss_mask_t;

#define XEHP_BITMAP_BITS(mask)	((int)BITS_PER_TYPE(typeof(mask.xehp)))

struct sseu_dev_info {
	u8 slice_mask;
	intel_sseu_ss_mask_t subslice_mask;
	intel_sseu_ss_mask_t geometry_subslice_mask;
	intel_sseu_ss_mask_t compute_subslice_mask;
	union {
		u16 hsw[GEN_MAX_HSW_SLICES][GEN_MAX_SS_PER_HSW_SLICE];
		u16 xehp[I915_MAX_SS_FUSE_BITS];
	} eu_mask;

	u16 eu_total;
	u8 eu_per_subslice;
	u8 min_eu_in_pool;
	/* For each slice, which subslice(s) has(have) 7 EUs (bitfield)? */
	u8 subslice_7eu[3];
	u8 has_slice_pg:1;
	u8 has_subslice_pg:1;
	u8 has_eu_pg:1;
	/*
	 * For Xe_HP and beyond, the hardware no longer has traditional slices
	 * so we just report the entire DSS pool under a fake "slice 0."
	 */
	u8 has_xehp_dss:1;

	/* Topology fields */
	u8 max_slices;
	u8 max_subslices;
	u8 max_eus_per_subslice;
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
		.subslice_mask = sseu->subslice_mask.hsw[0],
		.min_eus_per_subslice = sseu->max_eus_per_subslice,
		.max_eus_per_subslice = sseu->max_eus_per_subslice,
	};

	return value;
}

static inline bool
intel_sseu_has_subslice(const struct sseu_dev_info *sseu, int slice,
			int subslice)
{
	if (slice >= sseu->max_slices ||
	    subslice >= sseu->max_subslices)
		return false;

	if (sseu->has_xehp_dss)
		return test_bit(subslice, sseu->subslice_mask.xehp);
	else
		return sseu->subslice_mask.hsw[slice] & BIT(subslice);
}

/*
 * Used to obtain the index of the first DSS.  Can start searching from the
 * beginning of a specific dss group (e.g., gslice, cslice, etc.) if
 * groupsize and groupnum are non-zero.
 */
static inline unsigned int
intel_sseu_find_first_xehp_dss(const struct sseu_dev_info *sseu, int groupsize,
			       int groupnum)
{
	return find_next_bit(sseu->subslice_mask.xehp,
			     XEHP_BITMAP_BITS(sseu->subslice_mask),
			     groupnum * groupsize);
}

void intel_sseu_set_info(struct sseu_dev_info *sseu, u8 max_slices,
			 u8 max_subslices, u8 max_eus_per_subslice);

unsigned int
intel_sseu_subslice_total(const struct sseu_dev_info *sseu);

unsigned int
intel_sseu_get_hsw_subslices(const struct sseu_dev_info *sseu, u8 slice);

intel_sseu_ss_mask_t
intel_sseu_get_compute_subslices(const struct sseu_dev_info *sseu);

void intel_sseu_info_init(struct intel_gt *gt);

u32 intel_sseu_make_rpcs(struct intel_gt *gt,
			 const struct intel_sseu *req_sseu);

void intel_sseu_dump(const struct sseu_dev_info *sseu, struct drm_printer *p);
void intel_sseu_print_topology(struct drm_i915_private *i915,
			       const struct sseu_dev_info *sseu,
			       struct drm_printer *p);

u16 intel_slicemask_from_xehp_dssmask(intel_sseu_ss_mask_t dss_mask, int dss_per_slice);

int intel_sseu_copy_eumask_to_user(void __user *to,
				   const struct sseu_dev_info *sseu);
int intel_sseu_copy_ssmask_to_user(void __user *to,
				   const struct sseu_dev_info *sseu);

void intel_sseu_print_ss_info(const char *type,
			      const struct sseu_dev_info *sseu,
			      struct seq_file *m);

#endif /* __INTEL_SSEU_H__ */
