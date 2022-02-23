/* SPDX-License-Identifier: GPL-2.0 OR MIT */

#include <drm/ttm/ttm_resource.h>
#include <drm/ttm/ttm_device.h>
#include <drm/ttm/ttm_placement.h>
#include <linux/slab.h>

#include "ttm_module.h"

static int ttm_sys_man_alloc(struct ttm_resource_manager *man,
			     struct ttm_buffer_object *bo,
			     const struct ttm_place *place,
			     struct ttm_resource **res)
{
	*res = kzalloc(sizeof(**res), GFP_KERNEL);
	if (!*res)
		return -ENOMEM;

	ttm_resource_init(bo, place, *res);
	return 0;
}

static void ttm_sys_man_free(struct ttm_resource_manager *man,
			     struct ttm_resource *res)
{
	ttm_resource_fini(man, res);
	kfree(res);
}

static const struct ttm_resource_manager_func ttm_sys_manager_func = {
	.alloc = ttm_sys_man_alloc,
	.free = ttm_sys_man_free,
};

void ttm_sys_man_init(struct ttm_device *bdev)
{
	struct ttm_resource_manager *man = &bdev->sysman;

	/*
	 * Initialize the system memory buffer type.
	 * Other types need to be driver / IOCTL initialized.
	 */
	man->use_tt = true;
	man->func = &ttm_sys_manager_func;

	ttm_resource_manager_init(man, bdev, 0);
	ttm_set_driver_manager(bdev, TTM_PL_SYSTEM, man);
	ttm_resource_manager_set_used(man, true);
}
