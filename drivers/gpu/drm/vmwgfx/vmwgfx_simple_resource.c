// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright 2016 VMware, Inc., Palo Alto, CA., USA
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "vmwgfx_drv.h"
#include "vmwgfx_resource_priv.h"

/**
 * struct vmw_user_simple_resource - User-space simple resource struct
 *
 * @base: The TTM base object implementing user-space visibility.
 * @account_size: How much memory was accounted for this object.
 * @simple: The embedded struct vmw_simple_resource.
 */
struct vmw_user_simple_resource {
	struct ttm_base_object base;
	size_t account_size;
	struct vmw_simple_resource simple;
/*
 * Nothing to be placed after @simple, since size of @simple is
 * unknown.
 */
};


/**
 * vmw_simple_resource_init - Initialize a simple resource object.
 *
 * @dev_priv: Pointer to a struct device private.
 * @simple: The struct vmw_simple_resource to initialize.
 * @data: Data passed to the information initialization function.
 * @res_free: Function pointer to destroy the simple resource.
 *
 * Returns:
 *   0 if succeeded.
 *   Negative error value if error, in which case the resource will have been
 * freed.
 */
static int vmw_simple_resource_init(struct vmw_private *dev_priv,
				    struct vmw_simple_resource *simple,
				    void *data,
				    void (*res_free)(struct vmw_resource *res))
{
	struct vmw_resource *res = &simple->res;
	int ret;

	ret = vmw_resource_init(dev_priv, res, false, res_free,
				&simple->func->res_func);

	if (ret) {
		res_free(res);
		return ret;
	}

	ret = simple->func->init(res, data);
	if (ret) {
		vmw_resource_unreference(&res);
		return ret;
	}

	simple->res.hw_destroy = simple->func->hw_destroy;

	return 0;
}

/**
 * vmw_simple_resource_free - Free a simple resource object.
 *
 * @res: The struct vmw_resource member of the simple resource object.
 *
 * Frees memory and memory accounting for the object.
 */
static void vmw_simple_resource_free(struct vmw_resource *res)
{
	struct vmw_user_simple_resource *usimple =
		container_of(res, struct vmw_user_simple_resource,
			     simple.res);
	struct vmw_private *dev_priv = res->dev_priv;
	size_t size = usimple->account_size;

	ttm_base_object_kfree(usimple, base);
	ttm_mem_global_free(vmw_mem_glob(dev_priv), size);
}

/**
 * vmw_simple_resource_base_release - TTM object release callback
 *
 * @p_base: The struct ttm_base_object member of the simple resource object.
 *
 * Called when the last reference to the embedded struct ttm_base_object is
 * gone. Typically results in an object free, unless there are other
 * references to the embedded struct vmw_resource.
 */
static void vmw_simple_resource_base_release(struct ttm_base_object **p_base)
{
	struct ttm_base_object *base = *p_base;
	struct vmw_user_simple_resource *usimple =
		container_of(base, struct vmw_user_simple_resource, base);
	struct vmw_resource *res = &usimple->simple.res;

	*p_base = NULL;
	vmw_resource_unreference(&res);
}

/**
 * vmw_simple_resource_create_ioctl - Helper to set up an ioctl function to
 * create a struct vmw_simple_resource.
 *
 * @dev: Pointer to a struct drm device.
 * @data: Ioctl argument.
 * @file_priv: Pointer to a struct drm_file identifying the caller.
 * @func: Pointer to a struct vmw_simple_resource_func identifying the
 * simple resource type.
 *
 * Returns:
 *   0 if success,
 *   Negative error value on error.
 */
int
vmw_simple_resource_create_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv,
				 const struct vmw_simple_resource_func *func)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_user_simple_resource *usimple;
	struct vmw_resource *res;
	struct vmw_resource *tmp;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false
	};
	size_t alloc_size;
	size_t account_size;
	int ret;

	alloc_size = offsetof(struct vmw_user_simple_resource, simple) +
	  func->size;
	account_size = ttm_round_pot(alloc_size) + VMW_IDA_ACC_SIZE +
		TTM_OBJ_EXTRA_SIZE;

	ret = ttm_read_lock(&dev_priv->reservation_sem, true);
	if (ret)
		return ret;

	ret = ttm_mem_global_alloc(vmw_mem_glob(dev_priv), account_size,
				   &ctx);
	ttm_read_unlock(&dev_priv->reservation_sem);
	if (ret) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("Out of graphics memory for %s"
				  " creation.\n", func->res_func.type_name);

		goto out_ret;
	}

	usimple = kzalloc(alloc_size, GFP_KERNEL);
	if (!usimple) {
		ttm_mem_global_free(vmw_mem_glob(dev_priv),
				    account_size);
		ret = -ENOMEM;
		goto out_ret;
	}

	usimple->simple.func = func;
	usimple->account_size = account_size;
	res = &usimple->simple.res;
	usimple->base.shareable = false;
	usimple->base.tfile = NULL;

	/*
	 * From here on, the destructor takes over resource freeing.
	 */
	ret = vmw_simple_resource_init(dev_priv, &usimple->simple,
				       data, vmw_simple_resource_free);
	if (ret)
		goto out_ret;

	tmp = vmw_resource_reference(res);
	ret = ttm_base_object_init(tfile, &usimple->base, false,
				   func->ttm_res_type,
				   &vmw_simple_resource_base_release, NULL);

	if (ret) {
		vmw_resource_unreference(&tmp);
		goto out_err;
	}

	func->set_arg_handle(data, usimple->base.handle);
out_err:
	vmw_resource_unreference(&res);
out_ret:
	return ret;
}

/**
 * vmw_simple_resource_lookup - Look up a simple resource from its user-space
 * handle.
 *
 * @tfile: struct ttm_object_file identifying the caller.
 * @handle: The user-space handle.
 * @func: The struct vmw_simple_resource_func identifying the simple resource
 * type.
 *
 * Returns: Refcounted pointer to the embedded struct vmw_resource if
 * successfule. Error pointer otherwise.
 */
struct vmw_resource *
vmw_simple_resource_lookup(struct ttm_object_file *tfile,
			   uint32_t handle,
			   const struct vmw_simple_resource_func *func)
{
	struct vmw_user_simple_resource *usimple;
	struct ttm_base_object *base;
	struct vmw_resource *res;

	base = ttm_base_object_lookup(tfile, handle);
	if (!base) {
		VMW_DEBUG_USER("Invalid %s handle 0x%08lx.\n",
			       func->res_func.type_name,
			       (unsigned long) handle);
		return ERR_PTR(-ESRCH);
	}

	if (ttm_base_object_type(base) != func->ttm_res_type) {
		ttm_base_object_unref(&base);
		VMW_DEBUG_USER("Invalid type of %s handle 0x%08lx.\n",
			       func->res_func.type_name,
			       (unsigned long) handle);
		return ERR_PTR(-EINVAL);
	}

	usimple = container_of(base, typeof(*usimple), base);
	res = vmw_resource_reference(&usimple->simple.res);
	ttm_base_object_unref(&base);

	return res;
}
