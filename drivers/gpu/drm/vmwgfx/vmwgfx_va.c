// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright 2012-2016 VMware, Inc., Palo Alto, CA., USA
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

#include "vmwgfx_bo.h"
#include "vmwgfx_drv.h"
#include "vmwgfx_resource_priv.h"

/**
 * struct vmw_stream - Overlay stream simple resource.
 * @sres: The simple resource we derive from.
 * @stream_id: The overlay stream id.
 */
struct vmw_stream {
	struct vmw_simple_resource sres;
	u32 stream_id;
};

/**
 * vmw_stream - Typecast a struct vmw_resource to a struct vmw_stream.
 * @res: Pointer to the struct vmw_resource.
 *
 * Returns: Returns a pointer to the struct vmw_stream.
 */
static struct vmw_stream *
vmw_stream(struct vmw_resource *res)
{
	return container_of(res, struct vmw_stream, sres.res);
}

/***************************************************************************
 * Simple resource callbacks for struct vmw_stream
 **************************************************************************/
static void vmw_stream_hw_destroy(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct vmw_stream *stream = vmw_stream(res);
	int ret;

	ret = vmw_overlay_unref(dev_priv, stream->stream_id);
	WARN_ON_ONCE(ret != 0);
}

static int vmw_stream_init(struct vmw_resource *res, void *data)
{
	struct vmw_stream *stream = vmw_stream(res);

	return vmw_overlay_claim(res->dev_priv, &stream->stream_id);
}

static void vmw_stream_set_arg_handle(void *data, u32 handle)
{
	struct drm_vmw_stream_arg *arg = (struct drm_vmw_stream_arg *)data;

	arg->stream_id = handle;
}

static const struct vmw_simple_resource_func va_stream_func = {
	.res_func = {
		.res_type = vmw_res_stream,
		.needs_guest_memory = false,
		.may_evict = false,
		.type_name = "overlay stream",
		.domain = VMW_BO_DOMAIN_SYS,
		.busy_domain = VMW_BO_DOMAIN_SYS,
		.create = NULL,
		.destroy = NULL,
		.bind = NULL,
		.unbind = NULL
	},
	.ttm_res_type = VMW_RES_STREAM,
	.size = sizeof(struct vmw_stream),
	.init = vmw_stream_init,
	.hw_destroy = vmw_stream_hw_destroy,
	.set_arg_handle = vmw_stream_set_arg_handle,
};

/***************************************************************************
 * End simple resource callbacks for struct vmw_stream
 **************************************************************************/

/**
 * vmw_stream_unref_ioctl - Ioctl to unreference a user-space handle to
 * a struct vmw_stream.
 * @dev: Pointer to the drm device.
 * @data: The ioctl argument
 * @file_priv: Pointer to a struct drm_file identifying the caller.
 *
 * Return:
 *   0 if successful.
 *   Negative error value on failure.
 */
int vmw_stream_unref_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_vmw_stream_arg *arg = (struct drm_vmw_stream_arg *)data;

	return ttm_ref_object_base_unref(vmw_fpriv(file_priv)->tfile,
					 arg->stream_id);
}

/**
 * vmw_stream_claim_ioctl - Ioctl to claim a struct vmw_stream overlay.
 * @dev: Pointer to the drm device.
 * @data: The ioctl argument
 * @file_priv: Pointer to a struct drm_file identifying the caller.
 *
 * Return:
 *   0 if successful.
 *   Negative error value on failure.
 */
int vmw_stream_claim_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	return vmw_simple_resource_create_ioctl(dev, data, file_priv,
						&va_stream_func);
}

/**
 * vmw_user_stream_lookup - Look up a struct vmw_user_stream from a handle.
 * @dev_priv: Pointer to a struct vmw_private.
 * @tfile: struct ttm_object_file identifying the caller.
 * @inout_id: In: The user-space handle. Out: The stream id.
 * @out: On output contains a refcounted pointer to the embedded
 * struct vmw_resource.
 *
 * Return:
 *   0 if successful.
 *   Negative error value on failure.
 */
int vmw_user_stream_lookup(struct vmw_private *dev_priv,
			   struct ttm_object_file *tfile,
			   uint32_t *inout_id, struct vmw_resource **out)
{
	struct vmw_stream *stream;
	struct vmw_resource *res =
		vmw_simple_resource_lookup(tfile, *inout_id, &va_stream_func);

	if (IS_ERR(res))
		return PTR_ERR(res);

	stream = vmw_stream(res);
	*inout_id = stream->stream_id;
	*out = res;

	return 0;
}
