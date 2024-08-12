/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright (c) 2009-2023 VMware, Inc., Palo Alto, CA., USA
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
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 *
 * While no substantial code is shared, the prime code is inspired by
 * drm_prime.c, with
 * Authors:
 *      Dave Airlie <airlied@redhat.com>
 *      Rob Clark <rob.clark@linaro.org>
 */
/** @file ttm_ref_object.c
 *
 * Base- and reference object implementation for the various
 * ttm objects. Implements reference counting, minimal security checks
 * and release on file close.
 */


#define pr_fmt(fmt) "[TTM] " fmt

#include "ttm_object.h"
#include "vmwgfx_drv.h"

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/module.h>
#include <linux/hashtable.h>

MODULE_IMPORT_NS(DMA_BUF);

#define VMW_TTM_OBJECT_REF_HT_ORDER 10

/**
 * struct ttm_object_file
 *
 * @tdev: Pointer to the ttm_object_device.
 *
 * @lock: Lock that protects the ref_list list and the
 * ref_hash hash tables.
 *
 * @ref_list: List of ttm_ref_objects to be destroyed at
 * file release.
 *
 * @ref_hash: Hash tables of ref objects, one per ttm_ref_type,
 * for fast lookup of ref objects given a base object.
 *
 * @refcount: reference/usage count
 */
struct ttm_object_file {
	struct ttm_object_device *tdev;
	spinlock_t lock;
	struct list_head ref_list;
	DECLARE_HASHTABLE(ref_hash, VMW_TTM_OBJECT_REF_HT_ORDER);
	struct kref refcount;
};

/*
 * struct ttm_object_device
 *
 * @object_lock: lock that protects idr.
 *
 * This is the per-device data structure needed for ttm object management.
 */

struct ttm_object_device {
	spinlock_t object_lock;
	struct dma_buf_ops ops;
	void (*dmabuf_release)(struct dma_buf *dma_buf);
	struct idr idr;
};

/*
 * struct ttm_ref_object
 *
 * @hash: Hash entry for the per-file object reference hash.
 *
 * @head: List entry for the per-file list of ref-objects.
 *
 * @kref: Ref count.
 *
 * @obj: Base object this ref object is referencing.
 *
 * @ref_type: Type of ref object.
 *
 * This is similar to an idr object, but it also has a hash table entry
 * that allows lookup with a pointer to the referenced object as a key. In
 * that way, one can easily detect whether a base object is referenced by
 * a particular ttm_object_file. It also carries a ref count to avoid creating
 * multiple ref objects if a ttm_object_file references the same base
 * object more than once.
 */

struct ttm_ref_object {
	struct rcu_head rcu_head;
	struct vmwgfx_hash_item hash;
	struct list_head head;
	struct kref kref;
	struct ttm_base_object *obj;
	struct ttm_object_file *tfile;
};

static void ttm_prime_dmabuf_release(struct dma_buf *dma_buf);

static inline struct ttm_object_file *
ttm_object_file_ref(struct ttm_object_file *tfile)
{
	kref_get(&tfile->refcount);
	return tfile;
}

static int ttm_tfile_find_ref_rcu(struct ttm_object_file *tfile,
				  uint64_t key,
				  struct vmwgfx_hash_item **p_hash)
{
	struct vmwgfx_hash_item *hash;

	hash_for_each_possible_rcu(tfile->ref_hash, hash, head, key) {
		if (hash->key == key) {
			*p_hash = hash;
			return 0;
		}
	}
	return -EINVAL;
}

static int ttm_tfile_find_ref(struct ttm_object_file *tfile,
			      uint64_t key,
			      struct vmwgfx_hash_item **p_hash)
{
	struct vmwgfx_hash_item *hash;

	hash_for_each_possible(tfile->ref_hash, hash, head, key) {
		if (hash->key == key) {
			*p_hash = hash;
			return 0;
		}
	}
	return -EINVAL;
}

static void ttm_object_file_destroy(struct kref *kref)
{
	struct ttm_object_file *tfile =
		container_of(kref, struct ttm_object_file, refcount);

	kfree(tfile);
}


static inline void ttm_object_file_unref(struct ttm_object_file **p_tfile)
{
	struct ttm_object_file *tfile = *p_tfile;

	*p_tfile = NULL;
	kref_put(&tfile->refcount, ttm_object_file_destroy);
}


int ttm_base_object_init(struct ttm_object_file *tfile,
			 struct ttm_base_object *base,
			 bool shareable,
			 enum ttm_object_type object_type,
			 void (*refcount_release) (struct ttm_base_object **))
{
	struct ttm_object_device *tdev = tfile->tdev;
	int ret;

	base->shareable = shareable;
	base->tfile = ttm_object_file_ref(tfile);
	base->refcount_release = refcount_release;
	base->object_type = object_type;
	kref_init(&base->refcount);
	idr_preload(GFP_KERNEL);
	spin_lock(&tdev->object_lock);
	ret = idr_alloc(&tdev->idr, base, 1, 0, GFP_NOWAIT);
	spin_unlock(&tdev->object_lock);
	idr_preload_end();
	if (ret < 0)
		return ret;

	base->handle = ret;
	ret = ttm_ref_object_add(tfile, base, NULL, false);
	if (unlikely(ret != 0))
		goto out_err1;

	ttm_base_object_unref(&base);

	return 0;
out_err1:
	spin_lock(&tdev->object_lock);
	idr_remove(&tdev->idr, base->handle);
	spin_unlock(&tdev->object_lock);
	return ret;
}

static void ttm_release_base(struct kref *kref)
{
	struct ttm_base_object *base =
	    container_of(kref, struct ttm_base_object, refcount);
	struct ttm_object_device *tdev = base->tfile->tdev;

	spin_lock(&tdev->object_lock);
	idr_remove(&tdev->idr, base->handle);
	spin_unlock(&tdev->object_lock);

	/*
	 * Note: We don't use synchronize_rcu() here because it's far
	 * too slow. It's up to the user to free the object using
	 * call_rcu() or ttm_base_object_kfree().
	 */

	ttm_object_file_unref(&base->tfile);
	if (base->refcount_release)
		base->refcount_release(&base);
}

void ttm_base_object_unref(struct ttm_base_object **p_base)
{
	struct ttm_base_object *base = *p_base;

	*p_base = NULL;

	kref_put(&base->refcount, ttm_release_base);
}

struct ttm_base_object *ttm_base_object_lookup(struct ttm_object_file *tfile,
					       uint64_t key)
{
	struct ttm_base_object *base = NULL;
	struct vmwgfx_hash_item *hash;
	int ret;

	spin_lock(&tfile->lock);
	ret = ttm_tfile_find_ref(tfile, key, &hash);

	if (likely(ret == 0)) {
		base = hlist_entry(hash, struct ttm_ref_object, hash)->obj;
		if (!kref_get_unless_zero(&base->refcount))
			base = NULL;
	}
	spin_unlock(&tfile->lock);


	return base;
}

struct ttm_base_object *
ttm_base_object_lookup_for_ref(struct ttm_object_device *tdev, uint64_t key)
{
	struct ttm_base_object *base;

	rcu_read_lock();
	base = idr_find(&tdev->idr, key);

	if (base && !kref_get_unless_zero(&base->refcount))
		base = NULL;
	rcu_read_unlock();

	return base;
}

int ttm_ref_object_add(struct ttm_object_file *tfile,
		       struct ttm_base_object *base,
		       bool *existed,
		       bool require_existed)
{
	struct ttm_ref_object *ref;
	struct vmwgfx_hash_item *hash;
	int ret = -EINVAL;

	if (base->tfile != tfile && !base->shareable)
		return -EPERM;

	if (existed != NULL)
		*existed = true;

	while (ret == -EINVAL) {
		rcu_read_lock();
		ret = ttm_tfile_find_ref_rcu(tfile, base->handle, &hash);

		if (ret == 0) {
			ref = hlist_entry(hash, struct ttm_ref_object, hash);
			if (kref_get_unless_zero(&ref->kref)) {
				rcu_read_unlock();
				break;
			}
		}

		rcu_read_unlock();
		if (require_existed)
			return -EPERM;

		ref = kmalloc(sizeof(*ref), GFP_KERNEL);
		if (unlikely(ref == NULL)) {
			return -ENOMEM;
		}

		ref->hash.key = base->handle;
		ref->obj = base;
		ref->tfile = tfile;
		kref_init(&ref->kref);

		spin_lock(&tfile->lock);
		hash_add_rcu(tfile->ref_hash, &ref->hash.head, ref->hash.key);
		ret = 0;

		list_add_tail(&ref->head, &tfile->ref_list);
		kref_get(&base->refcount);
		spin_unlock(&tfile->lock);
		if (existed != NULL)
			*existed = false;
	}

	return ret;
}

static void __releases(tfile->lock) __acquires(tfile->lock)
ttm_ref_object_release(struct kref *kref)
{
	struct ttm_ref_object *ref =
	    container_of(kref, struct ttm_ref_object, kref);
	struct ttm_object_file *tfile = ref->tfile;

	hash_del_rcu(&ref->hash.head);
	list_del(&ref->head);
	spin_unlock(&tfile->lock);

	ttm_base_object_unref(&ref->obj);
	kfree_rcu(ref, rcu_head);
	spin_lock(&tfile->lock);
}

int ttm_ref_object_base_unref(struct ttm_object_file *tfile,
			      unsigned long key)
{
	struct ttm_ref_object *ref;
	struct vmwgfx_hash_item *hash;
	int ret;

	spin_lock(&tfile->lock);
	ret = ttm_tfile_find_ref(tfile, key, &hash);
	if (unlikely(ret != 0)) {
		spin_unlock(&tfile->lock);
		return -EINVAL;
	}
	ref = hlist_entry(hash, struct ttm_ref_object, hash);
	kref_put(&ref->kref, ttm_ref_object_release);
	spin_unlock(&tfile->lock);
	return 0;
}

void ttm_object_file_release(struct ttm_object_file **p_tfile)
{
	struct ttm_ref_object *ref;
	struct list_head *list;
	struct ttm_object_file *tfile = *p_tfile;

	*p_tfile = NULL;
	spin_lock(&tfile->lock);

	/*
	 * Since we release the lock within the loop, we have to
	 * restart it from the beginning each time.
	 */

	while (!list_empty(&tfile->ref_list)) {
		list = tfile->ref_list.next;
		ref = list_entry(list, struct ttm_ref_object, head);
		ttm_ref_object_release(&ref->kref);
	}

	spin_unlock(&tfile->lock);

	ttm_object_file_unref(&tfile);
}

struct ttm_object_file *ttm_object_file_init(struct ttm_object_device *tdev)
{
	struct ttm_object_file *tfile = kmalloc(sizeof(*tfile), GFP_KERNEL);

	if (unlikely(tfile == NULL))
		return NULL;

	spin_lock_init(&tfile->lock);
	tfile->tdev = tdev;
	kref_init(&tfile->refcount);
	INIT_LIST_HEAD(&tfile->ref_list);

	hash_init(tfile->ref_hash);

	return tfile;
}

struct ttm_object_device *
ttm_object_device_init(const struct dma_buf_ops *ops)
{
	struct ttm_object_device *tdev = kmalloc(sizeof(*tdev), GFP_KERNEL);

	if (unlikely(tdev == NULL))
		return NULL;

	spin_lock_init(&tdev->object_lock);

	/*
	 * Our base is at VMWGFX_NUM_MOB + 1 because we want to create
	 * a seperate namespace for GEM handles (which are
	 * 1..VMWGFX_NUM_MOB) and the surface handles. Some ioctl's
	 * can take either handle as an argument so we want to
	 * easily be able to tell whether the handle refers to a
	 * GEM buffer or a surface.
	 */
	idr_init_base(&tdev->idr, VMWGFX_NUM_MOB + 1);
	tdev->ops = *ops;
	tdev->dmabuf_release = tdev->ops.release;
	tdev->ops.release = ttm_prime_dmabuf_release;
	return tdev;
}

void ttm_object_device_release(struct ttm_object_device **p_tdev)
{
	struct ttm_object_device *tdev = *p_tdev;

	*p_tdev = NULL;

	WARN_ON_ONCE(!idr_is_empty(&tdev->idr));
	idr_destroy(&tdev->idr);

	kfree(tdev);
}

/**
 * get_dma_buf_unless_doomed - get a dma_buf reference if possible.
 *
 * @dmabuf: Non-refcounted pointer to a struct dma-buf.
 *
 * Obtain a file reference from a lookup structure that doesn't refcount
 * the file, but synchronizes with its release method to make sure it has
 * not been freed yet. See for example kref_get_unless_zero documentation.
 * Returns true if refcounting succeeds, false otherwise.
 *
 * Nobody really wants this as a public API yet, so let it mature here
 * for some time...
 */
static bool __must_check get_dma_buf_unless_doomed(struct dma_buf *dmabuf)
{
	return atomic_long_inc_not_zero(&dmabuf->file->f_count) != 0L;
}

/**
 * ttm_prime_refcount_release - refcount release method for a prime object.
 *
 * @p_base: Pointer to ttm_base_object pointer.
 *
 * This is a wrapper that calls the refcount_release founction of the
 * underlying object. At the same time it cleans up the prime object.
 * This function is called when all references to the base object we
 * derive from are gone.
 */
static void ttm_prime_refcount_release(struct ttm_base_object **p_base)
{
	struct ttm_base_object *base = *p_base;
	struct ttm_prime_object *prime;

	*p_base = NULL;
	prime = container_of(base, struct ttm_prime_object, base);
	BUG_ON(prime->dma_buf != NULL);
	mutex_destroy(&prime->mutex);
	if (prime->refcount_release)
		prime->refcount_release(&base);
}

/**
 * ttm_prime_dmabuf_release - Release method for the dma-bufs we export
 *
 * @dma_buf:
 *
 * This function first calls the dma_buf release method the driver
 * provides. Then it cleans up our dma_buf pointer used for lookup,
 * and finally releases the reference the dma_buf has on our base
 * object.
 */
static void ttm_prime_dmabuf_release(struct dma_buf *dma_buf)
{
	struct ttm_prime_object *prime =
		(struct ttm_prime_object *) dma_buf->priv;
	struct ttm_base_object *base = &prime->base;
	struct ttm_object_device *tdev = base->tfile->tdev;

	if (tdev->dmabuf_release)
		tdev->dmabuf_release(dma_buf);
	mutex_lock(&prime->mutex);
	if (prime->dma_buf == dma_buf)
		prime->dma_buf = NULL;
	mutex_unlock(&prime->mutex);
	ttm_base_object_unref(&base);
}

/**
 * ttm_prime_fd_to_handle - Get a base object handle from a prime fd
 *
 * @tfile: A struct ttm_object_file identifying the caller.
 * @fd: The prime / dmabuf fd.
 * @handle: The returned handle.
 *
 * This function returns a handle to an object that previously exported
 * a dma-buf. Note that we don't handle imports yet, because we simply
 * have no consumers of that implementation.
 */
int ttm_prime_fd_to_handle(struct ttm_object_file *tfile,
			   int fd, u32 *handle)
{
	struct ttm_object_device *tdev = tfile->tdev;
	struct dma_buf *dma_buf;
	struct ttm_prime_object *prime;
	struct ttm_base_object *base;
	int ret;

	dma_buf = dma_buf_get(fd);
	if (IS_ERR(dma_buf))
		return PTR_ERR(dma_buf);

	if (dma_buf->ops != &tdev->ops)
		return -ENOSYS;

	prime = (struct ttm_prime_object *) dma_buf->priv;
	base = &prime->base;
	*handle = base->handle;
	ret = ttm_ref_object_add(tfile, base, NULL, false);

	dma_buf_put(dma_buf);

	return ret;
}

/**
 * ttm_prime_handle_to_fd - Return a dma_buf fd from a ttm prime object
 *
 * @tfile: Struct ttm_object_file identifying the caller.
 * @handle: Handle to the object we're exporting from.
 * @flags: flags for dma-buf creation. We just pass them on.
 * @prime_fd: The returned file descriptor.
 *
 */
int ttm_prime_handle_to_fd(struct ttm_object_file *tfile,
			   uint32_t handle, uint32_t flags,
			   int *prime_fd)
{
	struct ttm_object_device *tdev = tfile->tdev;
	struct ttm_base_object *base;
	struct dma_buf *dma_buf;
	struct ttm_prime_object *prime;
	int ret;

	base = ttm_base_object_lookup(tfile, handle);
	if (unlikely(base == NULL ||
		     base->object_type != ttm_prime_type)) {
		ret = -ENOENT;
		goto out_unref;
	}

	prime = container_of(base, struct ttm_prime_object, base);
	if (unlikely(!base->shareable)) {
		ret = -EPERM;
		goto out_unref;
	}

	ret = mutex_lock_interruptible(&prime->mutex);
	if (unlikely(ret != 0)) {
		ret = -ERESTARTSYS;
		goto out_unref;
	}

	dma_buf = prime->dma_buf;
	if (!dma_buf || !get_dma_buf_unless_doomed(dma_buf)) {
		DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
		exp_info.ops = &tdev->ops;
		exp_info.size = prime->size;
		exp_info.flags = flags;
		exp_info.priv = prime;

		/*
		 * Need to create a new dma_buf
		 */

		dma_buf = dma_buf_export(&exp_info);
		if (IS_ERR(dma_buf)) {
			ret = PTR_ERR(dma_buf);
			mutex_unlock(&prime->mutex);
			goto out_unref;
		}

		/*
		 * dma_buf has taken the base object reference
		 */
		base = NULL;
		prime->dma_buf = dma_buf;
	}
	mutex_unlock(&prime->mutex);

	ret = dma_buf_fd(dma_buf, flags);
	if (ret >= 0) {
		*prime_fd = ret;
		ret = 0;
	} else
		dma_buf_put(dma_buf);

out_unref:
	if (base)
		ttm_base_object_unref(&base);
	return ret;
}

/**
 * ttm_prime_object_init - Initialize a ttm_prime_object
 *
 * @tfile: struct ttm_object_file identifying the caller
 * @size: The size of the dma_bufs we export.
 * @prime: The object to be initialized.
 * @type: See ttm_base_object_init
 * @refcount_release: See ttm_base_object_init
 *
 * Initializes an object which is compatible with the drm_prime model
 * for data sharing between processes and devices.
 */
int ttm_prime_object_init(struct ttm_object_file *tfile, size_t size,
			  struct ttm_prime_object *prime,
			  enum ttm_object_type type,
			  void (*refcount_release) (struct ttm_base_object **))
{
	bool shareable = !!(type == VMW_RES_SURFACE);
	mutex_init(&prime->mutex);
	prime->size = PAGE_ALIGN(size);
	prime->real_type = type;
	prime->dma_buf = NULL;
	prime->refcount_release = refcount_release;
	return ttm_base_object_init(tfile, &prime->base, shareable,
				    ttm_prime_type,
				    ttm_prime_refcount_release);
}
