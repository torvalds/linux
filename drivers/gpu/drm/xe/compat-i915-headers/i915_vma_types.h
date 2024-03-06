/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/types.h>
#include <linux/build_bug.h>

/* XX: Figure out how to handle this vma mapping in xe */
struct intel_remapped_plane_info {
	/* in gtt pages */
	u32 offset:31;
	u32 linear:1;
	union {
		/* in gtt pages for !linear */
		struct {
			u16 width;
			u16 height;
			u16 src_stride;
			u16 dst_stride;
		};

		/* in gtt pages for linear */
		u32 size;
	};
} __packed;

struct intel_remapped_info {
	struct intel_remapped_plane_info plane[4];
	/* in gtt pages */
	u32 plane_alignment;
} __packed;

struct intel_rotation_info {
	struct intel_remapped_plane_info plane[2];
} __packed;

enum i915_gtt_view_type {
	I915_GTT_VIEW_NORMAL = 0,
	I915_GTT_VIEW_ROTATED = sizeof(struct intel_rotation_info),
	I915_GTT_VIEW_REMAPPED = sizeof(struct intel_remapped_info),
};

static inline void assert_i915_gem_gtt_types(void)
{
	BUILD_BUG_ON(sizeof(struct intel_rotation_info) != 2 * sizeof(u32) + 8 * sizeof(u16));
	BUILD_BUG_ON(sizeof(struct intel_remapped_info) != 5 * sizeof(u32) + 16 * sizeof(u16));

	/* Check that rotation/remapped shares offsets for simplicity */
	BUILD_BUG_ON(offsetof(struct intel_remapped_info, plane[0]) !=
		     offsetof(struct intel_rotation_info, plane[0]));
	BUILD_BUG_ON(offsetofend(struct intel_remapped_info, plane[1]) !=
		     offsetofend(struct intel_rotation_info, plane[1]));

	/* As we encode the size of each branch inside the union into its type,
	 * we have to be careful that each branch has a unique size.
	 */
	switch ((enum i915_gtt_view_type)0) {
	case I915_GTT_VIEW_NORMAL:
	case I915_GTT_VIEW_ROTATED:
	case I915_GTT_VIEW_REMAPPED:
		/* gcc complains if these are identical cases */
		break;
	}
}

struct i915_gtt_view {
	enum i915_gtt_view_type type;
	union {
		/* Members need to contain no holes/padding */
		struct intel_rotation_info rotated;
		struct intel_remapped_info remapped;
	};
};
