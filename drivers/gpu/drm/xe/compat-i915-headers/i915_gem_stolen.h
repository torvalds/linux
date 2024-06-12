#ifndef _I915_GEM_STOLEN_H_
#define _I915_GEM_STOLEN_H_

#include "xe_ttm_stolen_mgr.h"
#include "xe_res_cursor.h"

struct xe_bo;

struct i915_stolen_fb {
	struct xe_bo *bo;
};

static inline int i915_gem_stolen_insert_node_in_range(struct xe_device *xe,
						       struct i915_stolen_fb *fb,
						       u32 size, u32 align,
						       u32 start, u32 end)
{
	struct xe_bo *bo;
	int err;
	u32 flags = XE_BO_FLAG_PINNED | XE_BO_FLAG_STOLEN;

	if (start < SZ_4K)
		start = SZ_4K;

	if (align) {
		size = ALIGN(size, align);
		start = ALIGN(start, align);
	}

	bo = xe_bo_create_locked_range(xe, xe_device_get_root_tile(xe),
				       NULL, size, start, end,
				       ttm_bo_type_kernel, flags);
	if (IS_ERR(bo)) {
		err = PTR_ERR(bo);
		bo = NULL;
		return err;
	}
	err = xe_bo_pin(bo);
	xe_bo_unlock_vm_held(bo);

	if (err) {
		xe_bo_put(fb->bo);
		bo = NULL;
	}

	fb->bo = bo;

	return err;
}

static inline int i915_gem_stolen_insert_node(struct xe_device *xe,
					      struct i915_stolen_fb *fb,
					      u32 size, u32 align)
{
	/* Not used on xe */
	BUG_ON(1);
	return -ENODEV;
}

static inline void i915_gem_stolen_remove_node(struct xe_device *xe,
					       struct i915_stolen_fb *fb)
{
	xe_bo_unpin_map_no_vm(fb->bo);
	fb->bo = NULL;
}

#define i915_gem_stolen_initialized(xe) (!!ttm_manager_type(&(xe)->ttm, XE_PL_STOLEN))
#define i915_gem_stolen_node_allocated(fb) (!!((fb)->bo))

static inline u32 i915_gem_stolen_node_offset(struct i915_stolen_fb *fb)
{
	struct xe_res_cursor res;

	xe_res_first(fb->bo->ttm.resource, 0, 4096, &res);
	return res.start;
}

/* Used for < gen4. These are not supported by Xe */
#define i915_gem_stolen_area_address(xe) (!WARN_ON(1))
/* Used for gen9 specific WA. Gen9 is not supported by Xe */
#define i915_gem_stolen_area_size(xe) (!WARN_ON(1))

#define i915_gem_stolen_node_address(xe, fb) (xe_ttm_stolen_gpu_offset(xe) + \
					 i915_gem_stolen_node_offset(fb))
#define i915_gem_stolen_node_size(fb) ((u64)((fb)->bo->ttm.base.size))

#endif
