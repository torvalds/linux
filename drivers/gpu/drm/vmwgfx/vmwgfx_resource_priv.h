/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright 2012-2014 VMware, Inc., Palo Alto, CA., USA
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

#ifndef _VMWGFX_RESOURCE_PRIV_H_
#define _VMWGFX_RESOURCE_PRIV_H_

#include "vmwgfx_drv.h"

#define VMW_IDA_ACC_SIZE 128

enum vmw_cmdbuf_res_state {
	VMW_CMDBUF_RES_COMMITTED,
	VMW_CMDBUF_RES_ADD,
	VMW_CMDBUF_RES_DEL
};

/**
 * struct vmw_user_resource_conv - Identify a derived user-exported resource
 * type and provide a function to convert its ttm_base_object pointer to
 * a struct vmw_resource
 */
struct vmw_user_resource_conv {
	enum ttm_object_type object_type;
	struct vmw_resource *(*base_obj_to_res)(struct ttm_base_object *base);
	void (*res_free) (struct vmw_resource *res);
};

/**
 * struct vmw_res_func - members and functions common for a resource type
 *
 * @res_type:          Enum that identifies the lru list to use for eviction.
 * @needs_backup:      Whether the resource is guest-backed and needs
 *                     persistent buffer storage.
 * @type_name:         String that identifies the resource type.
 * @backup_placement:  TTM placement for backup buffers.
 * @may_evict          Whether the resource may be evicted.
 * @create:            Create a hardware resource.
 * @destroy:           Destroy a hardware resource.
 * @bind:              Bind a hardware resource to persistent buffer storage.
 * @unbind:            Unbind a hardware resource from persistent
 *                     buffer storage.
 * @commit_notify:     If the resource is a command buffer managed resource,
 *                     callback to notify that a define or remove command
 *                     has been committed to the device.
 */
struct vmw_res_func {
	enum vmw_res_type res_type;
	bool needs_backup;
	const char *type_name;
	struct ttm_placement *backup_placement;
	bool may_evict;

	int (*create) (struct vmw_resource *res);
	int (*destroy) (struct vmw_resource *res);
	int (*bind) (struct vmw_resource *res,
		     struct ttm_validate_buffer *val_buf);
	int (*unbind) (struct vmw_resource *res,
		       bool readback,
		       struct ttm_validate_buffer *val_buf);
	void (*commit_notify)(struct vmw_resource *res,
			      enum vmw_cmdbuf_res_state state);
};

/**
 * struct vmw_simple_resource_func - members and functions common for the
 * simple resource helpers.
 * @res_func:  struct vmw_res_func as described above.
 * @ttm_res_type:  TTM resource type used for handle recognition.
 * @size:  Size of the simple resource information struct.
 * @init:  Initialize the simple resource information.
 * @hw_destroy:  A resource hw_destroy function.
 * @set_arg_handle:  Set the handle output argument of the ioctl create struct.
 */
struct vmw_simple_resource_func {
	const struct vmw_res_func res_func;
	int ttm_res_type;
	size_t size;
	int (*init)(struct vmw_resource *res, void *data);
	void (*hw_destroy)(struct vmw_resource *res);
	void (*set_arg_handle)(void *data, u32 handle);
};

/**
 * struct vmw_simple_resource - Kernel only side simple resource
 * @res: The resource we derive from.
 * @func: The method and member virtual table.
 */
struct vmw_simple_resource {
	struct vmw_resource res;
	const struct vmw_simple_resource_func *func;
};

int vmw_resource_alloc_id(struct vmw_resource *res);
void vmw_resource_release_id(struct vmw_resource *res);
int vmw_resource_init(struct vmw_private *dev_priv, struct vmw_resource *res,
		      bool delay_id,
		      void (*res_free) (struct vmw_resource *res),
		      const struct vmw_res_func *func);
void vmw_resource_activate(struct vmw_resource *res,
			   void (*hw_destroy) (struct vmw_resource *));
int
vmw_simple_resource_create_ioctl(struct drm_device *dev,
				 void *data,
				 struct drm_file *file_priv,
				 const struct vmw_simple_resource_func *func);
struct vmw_resource *
vmw_simple_resource_lookup(struct ttm_object_file *tfile,
			   uint32_t handle,
			   const struct vmw_simple_resource_func *func);
#endif
