/**************************************************************************
 *
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA
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
	rwlock_t lock;
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
	struct drm_hash_item hash;
	struct list_head head;
	struct kref kref;
	enum ttm_ref_type ref_type;
	struct ttm_base_object *obj;
	struct ttm_object_file *tfile;
};

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
	spin_lock(&tdev->object_lock);
	kref_init(&base->refcount);
	ret = drm_ht_just_insert_please(&tdev->object_hash,
					&base->hash,
					(unsigned long)base, 31, 0, 0);
	spin_unlock(&tdev->object_lock);
	if (unlikely(ret != 0))
		goto out_err0;

	ret = ttm_ref_object_add(tfile, base, TTM_REF_USAGE, NULL);
	if (unlikely(ret != 0))
		goto out_err1;

	ttm_base_object_unref(&base);

	return 0;
out_err1:
	(void)drm_ht_remove_item(&tdev->object_hash, &base->hash);
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
	(void)drm_ht_remove_item(&tdev->object_hash, &base->hash);
	spin_unlock(&tdev->object_lock);
	if (base->refcount_release) {
		ttm_object_file_unref(&base->tfile);
		base->refcount_release(&base);
	}
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
	struct ttm_object_device *tdev = tfile->tdev;
	struct ttm_base_object *base;
	struct drm_hash_item *hash;
	int ret;

	rcu_read_lock();
	ret = drm_ht_find_item(&tdev->object_hash, key, &hash);

	if (likely(ret == 0)) {
		base = drm_hash_entry(hash, struct ttm_base_object, hash);
		ret = kref_get_unless_zero(&base->refcount) ? 0 : -EINVAL;
	}
	rcu_read_unlock();

	if (unlikely(ret != 0))
		return NULL;

	if (tfile != base->tfile && !base->shareable) {
		pr_err("Attempted access of non-shareable object\n");
		ttm_base_object_unref(&base);
		return NULL;
	}

	return base;
}
EXPORT_SYMBOL(ttm_base_object_lookup);

int ttm_ref_object_add(struct ttm_object_file *tfile,
		       struct ttm_base_object *base,
		       enum ttm_ref_type ref_type, bool *existed)
{
	struct drm_open_hash *ht = &tfile->ref_hash[ref_type];
	struct ttm_ref_object *ref;
	struct drm_hash_item *hash;
	struct ttm_mem_global *mem_glob = tfile->tdev->mem_glob;
	int ret = -EINVAL;

	if (existed != NULL)
		*existed = true;

	while (ret == -EINVAL) {
		read_lock(&tfile->lock);
		ret = drm_ht_find_item(ht, base->hash.key, &hash);

		if (ret == 0) {
			ref = drm_hash_entry(hash, struct ttm_ref_object, hash);
			kref_get(&ref->kref);
			read_unlock(&tfile->lock);
			break;
		}

		read_unlock(&tfile->lock);
		ret = ttm_mem_global_alloc(mem_glob, sizeof(*ref),
					   false, false);
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

		write_lock(&tfile->lock);
		ret = drm_ht_insert_item(ht, &ref->hash);

		if (likely(ret == 0)) {
			list_add_tail(&ref->head, &tfile->ref_list);
			kref_get(&base->refcount);
			write_unlock(&tfile->lock);
			if (existed != NULL)
				*existed = false;
			break;
		}

		write_unlock(&tfile->lock);
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
	(void)drm_ht_remove_item(ht, &ref->hash);
	list_del(&ref->head);
	write_unlock(&tfile->lock);

	if (ref->ref_type != TTM_REF_USAGE && base->ref_obj_release)
		base->ref_obj_release(base, ref->ref_type);

	ttm_base_object_unref(&ref->obj);
	ttm_mem_global_free(mem_glob, sizeof(*ref));
	kfree(ref);
	write_lock(&tfile->lock);
}

int ttm_ref_object_base_unref(struct ttm_object_file *tfile,
			      unsigned long key, enum ttm_ref_type ref_type)
{
	struct drm_open_hash *ht = &tfile->ref_hash[ref_type];
	struct ttm_ref_object *ref;
	struct drm_hash_item *hash;
	int ret;

	write_lock(&tfile->lock);
	ret = drm_ht_find_item(ht, key, &hash);
	if (unlikely(ret != 0)) {
		write_unlock(&tfile->lock);
		return -EINVAL;
	}
	ref = drm_hash_entry(hash, struct ttm_ref_object, hash);
	kref_put(&ref->kref, ttm_ref_object_release);
	write_unlock(&tfile->lock);
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
	write_lock(&tfile->lock);

	/*
	 * Since we release the lock within the loop, we have to
	 * restart it from the beginning each time.
	 */

	while (!list_empty(&tfile->ref_list)) {
		list = tfile->ref_list.next;
		ref = list_entry(list, struct ttm_ref_object, head);
		ttm_ref_object_release(&ref->kref);
	}

	for (i = 0; i < TTM_REF_NUM; ++i)
		drm_ht_remove(&tfile->ref_hash[i]);

	write_unlock(&tfile->lock);
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

	rwlock_init(&tfile->lock);
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

struct ttm_object_device *ttm_object_device_init(struct ttm_mem_global
						 *mem_glob,
						 unsigned int hash_order)
{
	struct ttm_object_device *tdev = kmalloc(sizeof(*tdev), GFP_KERNEL);
	int ret;

	if (unlikely(tdev == NULL))
		return NULL;

	tdev->mem_glob = mem_glob;
	spin_lock_init(&tdev->object_lock);
	atomic_set(&tdev->object_count, 0);
	ret = drm_ht_create(&tdev->object_hash, hash_order);

	if (likely(ret == 0))
		return tdev;

	kfree(tdev);
	return NULL;
}
EXPORT_SYMBOL(ttm_object_device_init);

void ttm_object_device_release(struct ttm_object_device **p_tdev)
{
	struct ttm_object_device *tdev = *p_tdev;

	*p_tdev = NULL;

	spin_lock(&tdev->object_lock);
	drm_ht_remove(&tdev->object_hash);
	spin_unlock(&tdev->object_lock);

	kfree(tdev);
}
EXPORT_SYMBOL(ttm_object_device_release);
