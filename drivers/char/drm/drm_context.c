/**
 * \file drm_context.c
 * IOCTLs for generic contexts
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Fri Nov 24 18:31:37 2000 by gareth@valinux.com
 *
 * Copyright 1999, 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * ChangeLog:
 *  2001-11-16	Torsten Duwe <duwe@caldera.de>
 *		added context constructor/destructor hooks,
 *		needed by SiS driver's memory management.
 */

#include "drmP.h"

/******************************************************************/
/** \name Context bitmap support */
/*@{*/

/**
 * Free a handle from the context bitmap.
 *
 * \param dev DRM device.
 * \param ctx_handle context handle.
 *
 * Clears the bit specified by \p ctx_handle in drm_device::ctx_bitmap and the entry
 * in drm_device::context_sareas, while holding the drm_device::struct_sem
 * lock.
 */
void drm_ctxbitmap_free(drm_device_t * dev, int ctx_handle)
{
	if (ctx_handle < 0)
		goto failed;
	if (!dev->ctx_bitmap)
		goto failed;

	if (ctx_handle < DRM_MAX_CTXBITMAP) {
		down(&dev->struct_sem);
		clear_bit(ctx_handle, dev->ctx_bitmap);
		dev->context_sareas[ctx_handle] = NULL;
		up(&dev->struct_sem);
		return;
	}
      failed:
	DRM_ERROR("Attempt to free invalid context handle: %d\n", ctx_handle);
	return;
}

/**
 * Context bitmap allocation.
 *
 * \param dev DRM device.
 * \return (non-negative) context handle on success or a negative number on failure.
 *
 * Find the first zero bit in drm_device::ctx_bitmap and (re)allocates
 * drm_device::context_sareas to accommodate the new entry while holding the
 * drm_device::struct_sem lock.
 */
static int drm_ctxbitmap_next(drm_device_t * dev)
{
	int bit;

	if (!dev->ctx_bitmap)
		return -1;

	down(&dev->struct_sem);
	bit = find_first_zero_bit(dev->ctx_bitmap, DRM_MAX_CTXBITMAP);
	if (bit < DRM_MAX_CTXBITMAP) {
		set_bit(bit, dev->ctx_bitmap);
		DRM_DEBUG("drm_ctxbitmap_next bit : %d\n", bit);
		if ((bit + 1) > dev->max_context) {
			dev->max_context = (bit + 1);
			if (dev->context_sareas) {
				drm_map_t **ctx_sareas;

				ctx_sareas = drm_realloc(dev->context_sareas,
							 (dev->max_context -
							  1) *
							 sizeof(*dev->
								context_sareas),
							 dev->max_context *
							 sizeof(*dev->
								context_sareas),
							 DRM_MEM_MAPS);
				if (!ctx_sareas) {
					clear_bit(bit, dev->ctx_bitmap);
					up(&dev->struct_sem);
					return -1;
				}
				dev->context_sareas = ctx_sareas;
				dev->context_sareas[bit] = NULL;
			} else {
				/* max_context == 1 at this point */
				dev->context_sareas =
				    drm_alloc(dev->max_context *
					      sizeof(*dev->context_sareas),
					      DRM_MEM_MAPS);
				if (!dev->context_sareas) {
					clear_bit(bit, dev->ctx_bitmap);
					up(&dev->struct_sem);
					return -1;
				}
				dev->context_sareas[bit] = NULL;
			}
		}
		up(&dev->struct_sem);
		return bit;
	}
	up(&dev->struct_sem);
	return -1;
}

/**
 * Context bitmap initialization.
 *
 * \param dev DRM device.
 *
 * Allocates and initialize drm_device::ctx_bitmap and drm_device::context_sareas, while holding
 * the drm_device::struct_sem lock.
 */
int drm_ctxbitmap_init(drm_device_t * dev)
{
	int i;
	int temp;

	down(&dev->struct_sem);
	dev->ctx_bitmap = (unsigned long *)drm_alloc(PAGE_SIZE,
						     DRM_MEM_CTXBITMAP);
	if (dev->ctx_bitmap == NULL) {
		up(&dev->struct_sem);
		return -ENOMEM;
	}
	memset((void *)dev->ctx_bitmap, 0, PAGE_SIZE);
	dev->context_sareas = NULL;
	dev->max_context = -1;
	up(&dev->struct_sem);

	for (i = 0; i < DRM_RESERVED_CONTEXTS; i++) {
		temp = drm_ctxbitmap_next(dev);
		DRM_DEBUG("drm_ctxbitmap_init : %d\n", temp);
	}

	return 0;
}

/**
 * Context bitmap cleanup.
 *
 * \param dev DRM device.
 *
 * Frees drm_device::ctx_bitmap and drm_device::context_sareas, while holding
 * the drm_device::struct_sem lock.
 */
void drm_ctxbitmap_cleanup(drm_device_t * dev)
{
	down(&dev->struct_sem);
	if (dev->context_sareas)
		drm_free(dev->context_sareas,
			 sizeof(*dev->context_sareas) *
			 dev->max_context, DRM_MEM_MAPS);
	drm_free((void *)dev->ctx_bitmap, PAGE_SIZE, DRM_MEM_CTXBITMAP);
	up(&dev->struct_sem);
}

/*@}*/

/******************************************************************/
/** \name Per Context SAREA Support */
/*@{*/

/**
 * Get per-context SAREA.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument pointing to a drm_ctx_priv_map structure.
 * \return zero on success or a negative number on failure.
 *
 * Gets the map from drm_device::context_sareas with the handle specified and
 * returns its handle.
 */
int drm_getsareactx(struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_ctx_priv_map_t __user *argp = (void __user *)arg;
	drm_ctx_priv_map_t request;
	drm_map_t *map;
	drm_map_list_t *_entry;

	if (copy_from_user(&request, argp, sizeof(request)))
		return -EFAULT;

	down(&dev->struct_sem);
	if (dev->max_context < 0
	    || request.ctx_id >= (unsigned)dev->max_context) {
		up(&dev->struct_sem);
		return -EINVAL;
	}

	map = dev->context_sareas[request.ctx_id];
	up(&dev->struct_sem);

	request.handle = NULL;
	list_for_each_entry(_entry, &dev->maplist->head, head) {
		if (_entry->map == map) {
			request.handle =
			    (void *)(unsigned long)_entry->user_token;
			break;
		}
	}
	if (request.handle == NULL)
		return -EINVAL;

	if (copy_to_user(argp, &request, sizeof(request)))
		return -EFAULT;
	return 0;
}

/**
 * Set per-context SAREA.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument pointing to a drm_ctx_priv_map structure.
 * \return zero on success or a negative number on failure.
 *
 * Searches the mapping specified in \p arg and update the entry in
 * drm_device::context_sareas with it.
 */
int drm_setsareactx(struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_ctx_priv_map_t request;
	drm_map_t *map = NULL;
	drm_map_list_t *r_list = NULL;
	struct list_head *list;

	if (copy_from_user(&request,
			   (drm_ctx_priv_map_t __user *) arg, sizeof(request)))
		return -EFAULT;

	down(&dev->struct_sem);
	list_for_each(list, &dev->maplist->head) {
		r_list = list_entry(list, drm_map_list_t, head);
		if (r_list->map
		    && r_list->user_token == (unsigned long)request.handle)
			goto found;
	}
      bad:
	up(&dev->struct_sem);
	return -EINVAL;

      found:
	map = r_list->map;
	if (!map)
		goto bad;
	if (dev->max_context < 0)
		goto bad;
	if (request.ctx_id >= (unsigned)dev->max_context)
		goto bad;
	dev->context_sareas[request.ctx_id] = map;
	up(&dev->struct_sem);
	return 0;
}

/*@}*/

/******************************************************************/
/** \name The actual DRM context handling routines */
/*@{*/

/**
 * Switch context.
 *
 * \param dev DRM device.
 * \param old old context handle.
 * \param new new context handle.
 * \return zero on success or a negative number on failure.
 *
 * Attempt to set drm_device::context_flag.
 */
static int drm_context_switch(drm_device_t * dev, int old, int new)
{
	if (test_and_set_bit(0, &dev->context_flag)) {
		DRM_ERROR("Reentering -- FIXME\n");
		return -EBUSY;
	}

	DRM_DEBUG("Context switch from %d to %d\n", old, new);

	if (new == dev->last_context) {
		clear_bit(0, &dev->context_flag);
		return 0;
	}

	return 0;
}

/**
 * Complete context switch.
 *
 * \param dev DRM device.
 * \param new new context handle.
 * \return zero on success or a negative number on failure.
 *
 * Updates drm_device::last_context and drm_device::last_switch. Verifies the
 * hardware lock is held, clears the drm_device::context_flag and wakes up
 * drm_device::context_wait.
 */
static int drm_context_switch_complete(drm_device_t * dev, int new)
{
	dev->last_context = new;	/* PRE/POST: This is the _only_ writer. */
	dev->last_switch = jiffies;

	if (!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)) {
		DRM_ERROR("Lock isn't held after context switch\n");
	}

	/* If a context switch is ever initiated
	   when the kernel holds the lock, release
	   that lock here. */
	clear_bit(0, &dev->context_flag);
	wake_up(&dev->context_wait);

	return 0;
}

/**
 * Reserve contexts.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument pointing to a drm_ctx_res structure.
 * \return zero on success or a negative number on failure.
 */
int drm_resctx(struct inode *inode, struct file *filp,
	       unsigned int cmd, unsigned long arg)
{
	drm_ctx_res_t res;
	drm_ctx_t __user *argp = (void __user *)arg;
	drm_ctx_t ctx;
	int i;

	if (copy_from_user(&res, argp, sizeof(res)))
		return -EFAULT;

	if (res.count >= DRM_RESERVED_CONTEXTS) {
		memset(&ctx, 0, sizeof(ctx));
		for (i = 0; i < DRM_RESERVED_CONTEXTS; i++) {
			ctx.handle = i;
			if (copy_to_user(&res.contexts[i], &ctx, sizeof(ctx)))
				return -EFAULT;
		}
	}
	res.count = DRM_RESERVED_CONTEXTS;

	if (copy_to_user(argp, &res, sizeof(res)))
		return -EFAULT;
	return 0;
}

/**
 * Add context.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument pointing to a drm_ctx structure.
 * \return zero on success or a negative number on failure.
 *
 * Get a new handle for the context and copy to userspace.
 */
int drm_addctx(struct inode *inode, struct file *filp,
	       unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_ctx_list_t *ctx_entry;
	drm_ctx_t __user *argp = (void __user *)arg;
	drm_ctx_t ctx;

	if (copy_from_user(&ctx, argp, sizeof(ctx)))
		return -EFAULT;

	ctx.handle = drm_ctxbitmap_next(dev);
	if (ctx.handle == DRM_KERNEL_CONTEXT) {
		/* Skip kernel's context and get a new one. */
		ctx.handle = drm_ctxbitmap_next(dev);
	}
	DRM_DEBUG("%d\n", ctx.handle);
	if (ctx.handle == -1) {
		DRM_DEBUG("Not enough free contexts.\n");
		/* Should this return -EBUSY instead? */
		return -ENOMEM;
	}

	if (ctx.handle != DRM_KERNEL_CONTEXT) {
		if (dev->driver->context_ctor)
			if (!dev->driver->context_ctor(dev, ctx.handle)) {
				DRM_DEBUG( "Running out of ctxs or memory.\n");
				return -ENOMEM;
			}
	}

	ctx_entry = drm_alloc(sizeof(*ctx_entry), DRM_MEM_CTXLIST);
	if (!ctx_entry) {
		DRM_DEBUG("out of memory\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&ctx_entry->head);
	ctx_entry->handle = ctx.handle;
	ctx_entry->tag = priv;

	down(&dev->ctxlist_sem);
	list_add(&ctx_entry->head, &dev->ctxlist->head);
	++dev->ctx_count;
	up(&dev->ctxlist_sem);

	if (copy_to_user(argp, &ctx, sizeof(ctx)))
		return -EFAULT;
	return 0;
}

int drm_modctx(struct inode *inode, struct file *filp,
	       unsigned int cmd, unsigned long arg)
{
	/* This does nothing */
	return 0;
}

/**
 * Get context.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument pointing to a drm_ctx structure.
 * \return zero on success or a negative number on failure.
 */
int drm_getctx(struct inode *inode, struct file *filp,
	       unsigned int cmd, unsigned long arg)
{
	drm_ctx_t __user *argp = (void __user *)arg;
	drm_ctx_t ctx;

	if (copy_from_user(&ctx, argp, sizeof(ctx)))
		return -EFAULT;

	/* This is 0, because we don't handle any context flags */
	ctx.flags = 0;

	if (copy_to_user(argp, &ctx, sizeof(ctx)))
		return -EFAULT;
	return 0;
}

/**
 * Switch context.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument pointing to a drm_ctx structure.
 * \return zero on success or a negative number on failure.
 *
 * Calls context_switch().
 */
int drm_switchctx(struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_ctx_t ctx;

	if (copy_from_user(&ctx, (drm_ctx_t __user *) arg, sizeof(ctx)))
		return -EFAULT;

	DRM_DEBUG("%d\n", ctx.handle);
	return drm_context_switch(dev, dev->last_context, ctx.handle);
}

/**
 * New context.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument pointing to a drm_ctx structure.
 * \return zero on success or a negative number on failure.
 *
 * Calls context_switch_complete().
 */
int drm_newctx(struct inode *inode, struct file *filp,
	       unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_ctx_t ctx;

	if (copy_from_user(&ctx, (drm_ctx_t __user *) arg, sizeof(ctx)))
		return -EFAULT;

	DRM_DEBUG("%d\n", ctx.handle);
	drm_context_switch_complete(dev, ctx.handle);

	return 0;
}

/**
 * Remove context.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument pointing to a drm_ctx structure.
 * \return zero on success or a negative number on failure.
 *
 * If not the special kernel context, calls ctxbitmap_free() to free the specified context.
 */
int drm_rmctx(struct inode *inode, struct file *filp,
	      unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_ctx_t ctx;

	if (copy_from_user(&ctx, (drm_ctx_t __user *) arg, sizeof(ctx)))
		return -EFAULT;

	DRM_DEBUG("%d\n", ctx.handle);
	if (ctx.handle == DRM_KERNEL_CONTEXT + 1) {
		priv->remove_auth_on_close = 1;
	}
	if (ctx.handle != DRM_KERNEL_CONTEXT) {
		if (dev->driver->context_dtor)
			dev->driver->context_dtor(dev, ctx.handle);
		drm_ctxbitmap_free(dev, ctx.handle);
	}

	down(&dev->ctxlist_sem);
	if (!list_empty(&dev->ctxlist->head)) {
		drm_ctx_list_t *pos, *n;

		list_for_each_entry_safe(pos, n, &dev->ctxlist->head, head) {
			if (pos->handle == ctx.handle) {
				list_del(&pos->head);
				drm_free(pos, sizeof(*pos), DRM_MEM_CTXLIST);
				--dev->ctx_count;
			}
		}
	}
	up(&dev->ctxlist_sem);

	return 0;
}

/*@}*/
