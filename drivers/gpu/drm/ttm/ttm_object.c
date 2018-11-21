/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright (c) 2009-2013 VMware, Inc., Palo Alto, CA., USA
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
 */

#define pr_fmt(fmt) "[TTM] " fmt

#include <drm/ttm/ttm_object.h>
#include <drm/ttm/ttm_module.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/atomic.h>

struct ttm_object_file {
	struct ttm_object_device *tdev;
	spinlock_t lock;
	struct list_head ref_list;
	struct drm_open_hash ref_hash[TTM_REF_NUM];
	struct kref refcount;
};

/**
 * struct ttm_object_device
 *
 * @object_lock: lock that protects the object_hash hash table.
 *
 * @object_hash: hash table for fast lookup of object global names.
 *
 * @object_count: Per device object count.
 *
 * This is the per-device data structure needed for ttm object management.
 */

struct ttm_object_device {
	spinlock_t object_lock;
	struct drm_open_hash object_hash;
	atomic_t object_count;
	struct ttm_mem_global *mem_glob;
	struct dma_buf_ops ops;
	void (*dmabuf_release)(struct dma_buf *dma_buf);
	size_t dma_buf_size;
};

/**
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
	struct drm_hash_item hash;
	struct list_head head;
	struct kref kref;
	enum ttm_ref_type ref_type;
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
			 void (*refcount_release) (struct ttm_base_object **),
			 void (*ref_obj_release) (struct ttm_base_object *,
						  enum ttm_ref_type ref_type))
{
	struct ttm_object_device *tdev = tfile->tdev;
	int ret;

	base->shareable = shareable;
	base->tfile = ttm_object_file_ref(tfile);
	base->refcount_release = refcount_release;
	base->ref_obj_release = ref_obj_release;
	base->object_type = object_type;
	kref_init(&base->refcount);
	spin_lock(&tdev->object_lock);
	ret = drm_ht_just_insert_please_rcu(&tdev->object_hash,
					    &base->hash,
					    (unsigned long)base, 31, 0, 0);
	spin_unlock(&tdev->object_lock);
	if (unlikely(ret != 0))
		goto out_err0;

	ret = ttm_ref_object_add(tfile, base, TTM_REF_USAGE, NULL, false);
	if (unlikely(ret != 0))
		goto out_err1;

	ttm_base_object_unref(&base);

	return 0;
out_err1:
	spin_lock(&tdev->object_lock);
	(void)drm_ht_remove_item_rcu(&tdev->object_hash, &base->hash);
	spin_unlock(&tdev->object_lock);
out_err0:
	return ret;
}
EXPORT_SYMBOL(ttm_base_object_init);

static void ttm_release_base(struct kref *kref)
{
	struct ttm_base_object *base =
	    container_of(kref, struct ttm_base_object, refcount);
	struct ttm_object_device *tdev = base->tfile->tdev;

	spin_lock(&tdev->object_lock);
	(void)drm_ht_remove_item_rcu(&tdev->object_hash, &base->hash);
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
EXPORT_SYMBOL(ttm_base_object_unref);

struct ttm_base_object *ttm_base_object_lookup(struct ttm_object_file *tfile,
					       uint32_t key)
{
	struct ttm_base_object *base = NULL;
	struct drm_hash_item *hash;
	struct drm_open_hash *ht = &tfile->ref_hash[TTM_REF_USAGE];
	int ret;

	rcu_read_lock();
	ret = drm_ht_find_item_rcu(ht, key, &hash);

	if (likely(ret == 0)) {
		base = drm_hash_entry(hash, struct ttm_ref_object, hash)->obj;
		if (!kref_get_unless_zero(&base->refcount))
			base = NULL;
	}
	rcu_read_unlock();

	return base;
}
EXPORT_SYMBOL(ttm_base_object_lookup);

struct ttm_base_object *
ttm_base_object_lookup_for_ref(struct ttm_object_device *tdev, uint32_t key)
{
	struct ttm_base_object *base = NULL;
	struct drm_hash_item *hash;
	struct drm_open_hash *ht = &tdev->object_hash;
	int ret;

	rcu_read_lock();
	ret = drm_ht_find_item_rcu(ht, key, &hash);

	if (likely(ret == 0)) {
		base = drm_hash_entry(hash, struct ttm_base_object, hash);
		if (!kref_get_unless_zero(&base->refcount))
			base = NULL;
	}
	rcu_read_unlock();

	return base;
}
EXPORT_SYMBOL(ttm_base_object_lookup_for_ref);

/**
 * ttm_ref_object_exists - Check whether a caller has a valid ref object
 * (has opened) a base object.
 *
 * @tfile: Pointer to a struct ttm_object_file identifying the caller.
 * @base: Pointer to a struct base object.
 *
 * Checks wether the caller identified by @tfile has put a valid USAGE
 * reference object on the base object identified by @base.
 */
bool ttm_ref_object_exists(struct ttm_object_file *tfile,
			   struct ttm_base_object *base)
{
	struct drm_open_hash *ht = &tfile->ref_hash[TTM_REF_USAGE];
	struct drm_hash_item *hash;
	struct ttm_ref_object *ref;

	rcu_read_lock();
	if (unlikely(drm_ht_find_item_rcu(ht, base->hash.key, &hash) != 0))
		goto out_false;

	/*
	 * Verify that the ref object is really pointing to our base object.
	 * Our base object could actually be dead, and the ref object pointing
	 * to another base object with the same handle.
	 */
	ref = drm_hash_entry(hash, struct ttm_ref_object, hash);
	if (unlikely(base != ref->obj))
		goto out_false;

	/*
	 * Verify that the ref->obj pointer was actually valid!
	 */
	rmb();
	if (unlikely(kref_read(&ref->kref) == 0))
		goto out_false;

	rcu_read_unlock();
	return true;

 out_false:
	rcu_read_unlock();
	return false;
}
EXPORT_SYMBOL(ttm_ref_object_exists);

int ttm_ref_object_add(struct ttm_object_file *tfile,
		       struct ttm_base_object *base,
		       enum ttm_ref_type ref_type, bool *existed,
		       bool require_existed)
{
	struct drm_open_hash *ht = &tfile->ref_hash[ref_type];
	struct ttm_ref_object *ref;
	struct drm_hash_item *hash;
	struct ttm_mem_global *mem_glob = tfile->tdev->mem_glob;
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.no_wait_gpu = false
	};
	int ret = -EINVAL;

	if (base->tfile != tfile && !base->shareable)
		return -EPERM;

	if (existed != NULL)
		*existed = true;

	while (ret == -EINVAL) {
		rcu_read_lock();
		ret = drm_ht_find_item_rcu(ht, base->hash.key, &hash);

		if (ret == 0) {
			ref = drm_hash_entry(hash, struct ttm_ref_object, hash);
			if (kref_get_unless_zero(&ref->kref)) {
				rcu_read_unlock();
				break;
			}
		}

		rcu_read_unlock();
		if (require_existed)
			return -EPERM;

		ret = ttm_mem_global_alloc(mem_glob, sizeof(*ref),
					   &ctx);
		if (unlikely(ret != 0))
			return ret;
		ref = kmalloc(sizeof(*ref), GFP_KERNEL);
		if (unlikely(ref == NULL)) {
			ttm_mem_global_free(mem_glob, sizeof(*ref));
			return -ENOMEM;
		}

		ref->hash.key = base->hash.key;
		ref->obj = base;
		ref->tfile = tfile;
		ref->ref_type = ref_type;
		kref_init(&ref->kref);

		spin_lock(&tfile->lock);
		ret = drm_ht_insert_item_rcu(ht, &ref->hash);

		if (likely(ret == 0)) {
			list_add_tail(&ref->head, &tfile->ref_list);
			kref_get(&base->refcount);
			spin_unlock(&tfile->lock);
			if (existed != NULL)
				*existed = false;
			break;
		}

		spin_unlock(&tfile->lock);
		BUG_ON(ret != -EINVAL);

		ttm_mem_global_free(mem_glob, sizeof(*ref));
		kfree(ref);
	}

	return ret;
}
EXPORT_SYMBOL(ttm_ref_object_add);

static void ttm_ref_object_release(struct kref *kref)
{
	struct ttm_ref_object *ref =
	    container_of(kref, struct ttm_ref_object, kref);
	struct ttm_base_object *base = ref->obj;
	struct ttm_object_file *tfile = ref->tfile;
	struct drm_open_hash *ht;
	struct ttm_mem_global *mem_glob = tfile->tdev->mem_glob;

	ht = &tfile->ref_hash[ref->ref_type];
	(void)drm_ht_remove_item_rcu(ht, &ref->hash);
	list_del(&ref->head);
	spin_unlock(&tfile->lock);

	if (ref->ref_type != TTM_REF_USAGE && base->ref_obj_release)
		base->ref_obj_release(base, ref->ref_type);

	ttm_base_object_unref(&ref->obj);
	ttm_mem_global_free(mem_glob, sizeof(*ref));
	kfree_rcu(ref, rcu_head);
	spin_lock(&tfile->lock);
}

int ttm_ref_object_base_unref(struct ttm_object_file *tfile,
			      unsigned long key, enum ttm_ref_type ref_type)
{
	struct drm_open_hash *ht = &tfile->ref_hash[ref_type];
	struct ttm_ref_object *ref;
	struct drm_hash_item *hash;
	int ret;

	spin_lock(&tfile->lock);
	ret = drm_ht_find_item(ht, key, &hash);
	if (unlikely(ret != 0)) {
		spin_unlock(&tfile->lock);
		return -EINVAL;
	}
	ref = drm_hash_entry(hash, struct ttm_ref_object, hash);
	kref_put(&ref->kref, ttm_ref_object_release);
	spin_unlock(&tfile->lock);
	return 0;
}
EXPORT_SYMBOL(ttm_ref_object_base_unref);

void ttm_object_file_release(struct ttm_object_file **p_tfile)
{
	struct ttm_ref_object *ref;
	struct list_head *list;
	unsigned int i;
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
	for (i = 0; i < TTM_REF_NUM; ++i)
		drm_ht_remove(&tfile->ref_hash[i]);

	ttm_object_file_unref(&tfile);
}
EXPORT_SYMBOL(ttm_object_file_release);

struct ttm_object_file *ttm_object_file_init(struct ttm_object_device *tdev,
					     unsigned int hash_order)
{
	struct ttm_object_file *tfile = kmalloc(sizeof(*tfile), GFP_KERNEL);
	unsigned int i;
	unsigned int j = 0;
	int ret;

	if (unlikely(tfile == NULL))
		return NULL;

	spin_lock_init(&tfile->lock);
	tfile->tdev = tdev;
	kref_init(&tfile->refcount);
	INIT_LIST_HEAD(&tfile->ref_list);

	for (i = 0; i < TTM_REF_NUM; ++i) {
		ret = drm_ht_create(&tfile->ref_hash[i], hash_order);
		if (ret) {
			j = i;
			goto out_err;
		}
	}

	return tfile;
out_err:
	for (i = 0; i < j; ++i)
		drm_ht_remove(&tfile->ref_hash[i]);

	kfree(tfile);

	return NULL;
}
EXPORT_SYMBOL(ttm_object_file_init);

struct ttm_object_device *
ttm_object_device_init(struct ttm_mem_global *mem_glob,
		       unsigned int hash_order,
		       const struct dma_buf_ops *ops)
{
	struct ttm_object_device *tdev = kmalloc(sizeof(*tdev), GFP_KERNEL);
	int ret;

	if (unlikely(tdev == NULL))
		return NULL;

	tdev->mem_glob = mem_glob;
	spin_lock_init(&tdev->object_lock);
	atomic_set(&tdev->object_count, 0);
	ret = drm_ht_create(&tdev->object_hash, hash_order);
	if (ret != 0)
		goto out_no_object_hash;

	tdev->ops = *ops;
	tdev->dmabuf_release = tdev->ops.release;
	tdev->ops.release = ttm_prime_dmabuf_release;
	tdev->dma_buf_size = ttm_round_pot(sizeof(struct dma_buf)) +
		ttm_round_pot(sizeof(struct file));
	return tdev;

out_no_object_hash:
	kfree(tdev);
	return NULL;
}
EXPORT_SYMBOL(ttm_object_device_init);

void ttm_object_device_release(struct ttm_object_device **p_tdev)
{
	struct ttm_object_device *tdev = *p_tdev;

	*p_tdev = NULL;

	drm_ht_remove(&tdev->object_hash);

	kfree(tdev);
}
EXPORT_SYMBOL(ttm_object_device_release);

/**
 * get_dma_buf_unless_doomed - get a dma_buf reference if possible.
 *
 * @dma_buf: Non-refcounted pointer to a struct dma-buf.
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
	ttm_mem_global_free(tdev->mem_glob, tdev->dma_buf_size);
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
	*handle = base->hash.key;
	ret = ttm_ref_object_add(tfile, base, TTM_REF_USAGE, NULL, false);

	dma_buf_put(dma_buf);

	return ret;
}
EXPORT_SYMBOL_GPL(ttm_prime_fd_to_handle);

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
		struct ttm_operation_ctx ctx = {
			.interruptible = true,
			.no_wait_gpu = false
		};
		exp_info.ops = &tdev->ops;
		exp_info.size = prime->size;
		exp_info.flags = flags;
		exp_info.priv = prime;

		/*
		 * Need to create a new dma_buf, with memory accounting.
		 */
		ret = ttm_mem_global_alloc(tdev->mem_glob, tdev->dma_buf_size,
					   &ctx);
		if (unlikely(ret != 0)) {
			mutex_unlock(&prime->mutex);
			goto out_unref;
		}

		dma_buf = dma_buf_export(&exp_info);
		if (IS_ERR(dma_buf)) {
			ret = PTR_ERR(dma_buf);
			ttm_mem_global_free(tdev->mem_glob,
					    tdev->dma_buf_size);
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
EXPORT_SYMBOL_GPL(ttm_prime_handle_to_fd);

/**
 * ttm_prime_object_init - Initialize a ttm_prime_object
 *
 * @tfile: struct ttm_object_file identifying the caller
 * @size: The size of the dma_bufs we export.
 * @prime: The object to be initialized.
 * @shareable: See ttm_base_object_init
 * @type: See ttm_base_object_init
 * @refcount_release: See ttm_base_object_init
 * @ref_obj_release: See ttm_base_object_init
 *
 * Initializes an object which is compatible with the drm_prime model
 * for data sharing between processes and devices.
 */
int ttm_prime_object_init(struct ttm_object_file *tfile, size_t size,
			  struct ttm_prime_object *prime, bool shareable,
			  enum ttm_object_type type,
			  void (*refcount_release) (struct ttm_base_object **),
			  void (*ref_obj_release) (struct ttm_base_object *,
						   enum ttm_ref_type ref_type))
{
	mutex_init(&prime->mutex);
	prime->size = PAGE_ALIGN(size);
	prime->real_type = type;
	prime->dma_buf = NULL;
	prime->refcount_release = refcount_release;
	return ttm_base_object_init(tfile, &prime->base, shareable,
				    ttm_prime_type,
				    ttm_prime_refcount_release,
				    ref_obj_release);
}
EXPORT_SYMBOL(ttm_prime_object_init);
