/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2021 VMware, Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "vmwgfx_drv.h"

#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_device.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_resource.h>
#include <linux/slab.h>


static int vmw_sys_man_alloc(struct ttm_resource_manager *man,
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

static void vmw_sys_man_free(struct ttm_resource_manager *man,
			     struct ttm_resource *res)
{
	ttm_resource_fini(man, res);
	kfree(res);
}

static const struct ttm_resource_manager_func vmw_sys_manager_func = {
	.alloc = vmw_sys_man_alloc,
	.free = vmw_sys_man_free,
};

int vmw_sys_man_init(struct vmw_private *dev_priv)
{
	struct ttm_device *bdev = &dev_priv->bdev;
	struct ttm_resource_manager *man =
			kzalloc(sizeof(*man), GFP_KERNEL);

	if (!man)
		return -ENOMEM;

	man->use_tt = true;
	man->func = &vmw_sys_manager_func;

	ttm_resource_manager_init(man, bdev, 0);
	ttm_set_driver_manager(bdev, VMW_PL_SYSTEM, man);
	ttm_resource_manager_set_used(man, true);
	return 0;
}

void vmw_sys_man_fini(struct vmw_private *dev_priv)
{
	struct ttm_resource_manager *man = ttm_manager_type(&dev_priv->bdev,
							    VMW_PL_SYSTEM);

	ttm_resource_manager_evict_all(&dev_priv->bdev, man);

	ttm_resource_manager_set_used(man, false);
	ttm_resource_manager_cleanup(man);

	ttm_set_driver_manager(&dev_priv->bdev, VMW_PL_SYSTEM, NULL);
	kfree(man);
}
