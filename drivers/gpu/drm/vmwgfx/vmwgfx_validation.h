/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright © 2018 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
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
#ifndef _VMWGFX_VALIDATION_H_
#define _VMWGFX_VALIDATION_H_

#include <linux/list.h>
#include <linux/ww_mutex.h>

#include <drm/ttm/ttm_execbuf_util.h>

#include "vmwgfx_hashtab.h"

#define VMW_RES_DIRTY_NONE 0
#define VMW_RES_DIRTY_SET BIT(0)
#define VMW_RES_DIRTY_CLEAR BIT(1)

/**
 * struct vmw_validation_context - Per command submission validation context
 * @ht: Hash table used to find resource- or buffer object duplicates
 * @resource_list: List head for resource validation metadata
 * @resource_ctx_list: List head for resource validation metadata for
 * resources that need to be validated before those in @resource_list
 * @bo_list: List head for buffer objects
 * @page_list: List of pages used by the memory allocator
 * @ticket: Ticked used for ww mutex locking
 * @res_mutex: Pointer to mutex used for resource reserving
 * @merge_dups: Whether to merge metadata for duplicate resources or
 * buffer objects
 * @mem_size_left: Free memory left in the last page in @page_list
 * @page_address: Kernel virtual address of the last page in @page_list
 * @vm: A pointer to the memory reservation interface or NULL if no
 * memory reservation is needed.
 * @vm_size_left: Amount of reserved memory that so far has not been allocated.
 * @total_mem: Amount of reserved memory.
 */
struct vmw_validation_context {
	struct vmwgfx_open_hash *ht;
	struct list_head resource_list;
	struct list_head resource_ctx_list;
	struct list_head bo_list;
	struct list_head page_list;
	struct ww_acquire_ctx ticket;
	struct mutex *res_mutex;
	unsigned int merge_dups;
	unsigned int mem_size_left;
	u8 *page_address;
	struct vmw_validation_mem *vm;
	size_t vm_size_left;
	size_t total_mem;
};

struct vmw_buffer_object;
struct vmw_resource;
struct vmw_fence_obj;

#if 0
/**
 * DECLARE_VAL_CONTEXT - Declare a validation context with initialization
 * @_name: The name of the variable
 * @_ht: The hash table used to find dups or NULL if none
 * @_merge_dups: Whether to merge duplicate buffer object- or resource
 * entries. If set to true, ideally a hash table pointer should be supplied
 * as well unless the number of resources and buffer objects per validation
 * is known to be very small
 */
#endif
#define DECLARE_VAL_CONTEXT(_name, _ht, _merge_dups)			\
	struct vmw_validation_context _name =				\
	{ .ht = _ht,							\
	  .resource_list = LIST_HEAD_INIT((_name).resource_list),	\
	  .resource_ctx_list = LIST_HEAD_INIT((_name).resource_ctx_list), \
	  .bo_list = LIST_HEAD_INIT((_name).bo_list),			\
	  .page_list = LIST_HEAD_INIT((_name).page_list),		\
	  .res_mutex = NULL,						\
	  .merge_dups = _merge_dups,					\
	  .mem_size_left = 0,						\
	}

/**
 * vmw_validation_has_bos - return whether the validation context has
 * any buffer objects registered.
 *
 * @ctx: The validation context
 * Returns: Whether any buffer objects are registered
 */
static inline bool
vmw_validation_has_bos(struct vmw_validation_context *ctx)
{
	return !list_empty(&ctx->bo_list);
}

/**
 * vmw_validation_set_ht - Register a hash table for duplicate finding
 * @ctx: The validation context
 * @ht: Pointer to a hash table to use for duplicate finding
 * This function is intended to be used if the hash table wasn't
 * available at validation context declaration time
 */
static inline void vmw_validation_set_ht(struct vmw_validation_context *ctx,
					 struct vmwgfx_open_hash *ht)
{
	ctx->ht = ht;
}

/**
 * vmw_validation_bo_reserve - Reserve buffer objects registered with a
 * validation context
 * @ctx: The validation context
 * @intr: Perform waits interruptible
 *
 * Return: Zero on success, -ERESTARTSYS when interrupted, negative error
 * code on failure
 */
static inline int
vmw_validation_bo_reserve(struct vmw_validation_context *ctx,
			  bool intr)
{
	return ttm_eu_reserve_buffers(&ctx->ticket, &ctx->bo_list, intr,
				      NULL);
}

/**
 * vmw_validation_bo_fence - Unreserve and fence buffer objects registered
 * with a validation context
 * @ctx: The validation context
 *
 * This function unreserves the buffer objects previously reserved using
 * vmw_validation_bo_reserve, and fences them with a fence object.
 */
static inline void
vmw_validation_bo_fence(struct vmw_validation_context *ctx,
			struct vmw_fence_obj *fence)
{
	ttm_eu_fence_buffer_objects(&ctx->ticket, &ctx->bo_list,
				    (void *) fence);
}

/**
 * vmw_validation_align - Align a validation memory allocation
 * @val: The size to be aligned
 *
 * Returns: @val aligned to the granularity used by the validation memory
 * allocator.
 */
static inline unsigned int vmw_validation_align(unsigned int val)
{
	return ALIGN(val, sizeof(long));
}

int vmw_validation_add_bo(struct vmw_validation_context *ctx,
			  struct vmw_buffer_object *vbo,
			  bool as_mob, bool cpu_blit);
int vmw_validation_bo_validate_single(struct ttm_buffer_object *bo,
				      bool interruptible,
				      bool validate_as_mob);
int vmw_validation_bo_validate(struct vmw_validation_context *ctx, bool intr);
void vmw_validation_unref_lists(struct vmw_validation_context *ctx);
int vmw_validation_add_resource(struct vmw_validation_context *ctx,
				struct vmw_resource *res,
				size_t priv_size,
				u32 dirty,
				void **p_node,
				bool *first_usage);
void vmw_validation_drop_ht(struct vmw_validation_context *ctx);
int vmw_validation_res_reserve(struct vmw_validation_context *ctx,
			       bool intr);
void vmw_validation_res_unreserve(struct vmw_validation_context *ctx,
				  bool backoff);
void vmw_validation_res_switch_backup(struct vmw_validation_context *ctx,
				      void *val_private,
				      struct vmw_buffer_object *vbo,
				      unsigned long backup_offset);
int vmw_validation_res_validate(struct vmw_validation_context *ctx, bool intr);

int vmw_validation_prepare(struct vmw_validation_context *ctx,
			   struct mutex *mutex, bool intr);
void vmw_validation_revert(struct vmw_validation_context *ctx);
void vmw_validation_done(struct vmw_validation_context *ctx,
			 struct vmw_fence_obj *fence);

void *vmw_validation_mem_alloc(struct vmw_validation_context *ctx,
			       unsigned int size);
int vmw_validation_preload_bo(struct vmw_validation_context *ctx);
int vmw_validation_preload_res(struct vmw_validation_context *ctx,
			       unsigned int size);
void vmw_validation_res_set_dirty(struct vmw_validation_context *ctx,
				  void *val_private, u32 dirty);
void vmw_validation_bo_backoff(struct vmw_validation_context *ctx);

#endif
