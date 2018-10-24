/**************************************************************************
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
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
/** @file ttm_object.h
 *
 * Base- and reference object implementation for the various
 * ttm objects. Implements reference counting, minimal security checks
 * and release on file close.
 */

#ifndef _TTM_OBJECT_H_
#define _TTM_OBJECT_H_

#include <linux/list.h>
#include <drm/drm_hashtab.h>
#include <linux/kref.h>
#include <linux/rcupdate.h>
#include <linux/dma-buf.h>
#include <drm/ttm/ttm_memory.h>

/**
 * enum ttm_ref_type
 *
 * Describes what type of reference a ref object holds.
 *
 * TTM_REF_USAGE is a simple refcount on a base object.
 *
 * TTM_REF_SYNCCPU_READ is a SYNCCPU_READ reference on a
 * buffer object.
 *
 * TTM_REF_SYNCCPU_WRITE is a SYNCCPU_WRITE reference on a
 * buffer object.
 *
 */

enum ttm_ref_type {
	TTM_REF_USAGE,
	TTM_REF_SYNCCPU_READ,
	TTM_REF_SYNCCPU_WRITE,
	TTM_REF_NUM
};

/**
 * enum ttm_object_type
 *
 * One entry per ttm object type.
 * Device-specific types should use the
 * ttm_driver_typex types.
 */

enum ttm_object_type {
	ttm_fence_type,
	ttm_buffer_type,
	ttm_lock_type,
	ttm_prime_type,
	ttm_driver_type0 = 256,
	ttm_driver_type1,
	ttm_driver_type2,
	ttm_driver_type3,
	ttm_driver_type4,
	ttm_driver_type5
};

struct ttm_object_file;
struct ttm_object_device;

/**
 * struct ttm_base_object
 *
 * @hash: hash entry for the per-device object hash.
 * @type: derived type this object is base class for.
 * @shareable: Other ttm_object_files can access this object.
 *
 * @tfile: Pointer to ttm_object_file of the creator.
 * NULL if the object was not created by a user request.
 * (kernel object).
 *
 * @refcount: Number of references to this object, not
 * including the hash entry. A reference to a base object can
 * only be held by a ref object.
 *
 * @refcount_release: A function to be called when there are
 * no more references to this object. This function should
 * destroy the object (or make sure destruction eventually happens),
 * and when it is called, the object has
 * already been taken out of the per-device hash. The parameter
 * "base" should be set to NULL by the function.
 *
 * @ref_obj_release: A function to be called when a reference object
 * with another ttm_ref_type than TTM_REF_USAGE is deleted.
 * This function may, for example, release a lock held by a user-space
 * process.
 *
 * This struct is intended to be used as a base struct for objects that
 * are visible to user-space. It provides a global name, race-safe
 * access and refcounting, minimal access contol and hooks for unref actions.
 */

struct ttm_base_object {
	struct rcu_head rhead;
	struct ttm_object_file *tfile;
	struct kref refcount;
	void (*refcount_release) (struct ttm_base_object **base);
	void (*ref_obj_release) (struct ttm_base_object *base,
				 enum ttm_ref_type ref_type);
	u32 handle;
	enum ttm_object_type object_type;
	u32 shareable;
};


/**
 * struct ttm_prime_object - Modified base object that is prime-aware
 *
 * @base: struct ttm_base_object that we derive from
 * @mutex: Mutex protecting the @dma_buf member.
 * @size: Size of the dma_buf associated with this object
 * @real_type: Type of the underlying object. Needed since we're setting
 * the value of @base::object_type to ttm_prime_type
 * @dma_buf: Non ref-coutned pointer to a struct dma_buf created from this
 * object.
 * @refcount_release: The underlying object's release method. Needed since
 * we set @base::refcount_release to our own release method.
 */

struct ttm_prime_object {
	struct ttm_base_object base;
	struct mutex mutex;
	size_t size;
	enum ttm_object_type real_type;
	struct dma_buf *dma_buf;
	void (*refcount_release) (struct ttm_base_object **);
};

/**
 * ttm_base_object_init
 *
 * @tfile: Pointer to a struct ttm_object_file.
 * @base: The struct ttm_base_object to initialize.
 * @shareable: This object is shareable with other applcations.
 * (different @tfile pointers.)
 * @type: The object type.
 * @refcount_release: See the struct ttm_base_object description.
 * @ref_obj_release: See the struct ttm_base_object description.
 *
 * Initializes a struct ttm_base_object.
 */

extern int ttm_base_object_init(struct ttm_object_file *tfile,
				struct ttm_base_object *base,
				bool shareable,
				enum ttm_object_type type,
				void (*refcount_release) (struct ttm_base_object
							  **),
				void (*ref_obj_release) (struct ttm_base_object
							 *,
							 enum ttm_ref_type
							 ref_type));

/**
 * ttm_base_object_lookup
 *
 * @tfile: Pointer to a struct ttm_object_file.
 * @key: Hash key
 *
 * Looks up a struct ttm_base_object with the key @key.
 */

extern struct ttm_base_object *ttm_base_object_lookup(struct ttm_object_file
						      *tfile, uint32_t key);

/**
 * ttm_base_object_lookup_for_ref
 *
 * @tdev: Pointer to a struct ttm_object_device.
 * @key: Hash key
 *
 * Looks up a struct ttm_base_object with the key @key.
 * This function should only be used when the struct tfile associated with the
 * caller doesn't yet have a reference to the base object.
 */

extern struct ttm_base_object *
ttm_base_object_lookup_for_ref(struct ttm_object_device *tdev, uint32_t key);

/**
 * ttm_base_object_unref
 *
 * @p_base: Pointer to a pointer referencing a struct ttm_base_object.
 *
 * Decrements the base object refcount and clears the pointer pointed to by
 * p_base.
 */

extern void ttm_base_object_unref(struct ttm_base_object **p_base);

/**
 * ttm_ref_object_add.
 *
 * @tfile: A struct ttm_object_file representing the application owning the
 * ref_object.
 * @base: The base object to reference.
 * @ref_type: The type of reference.
 * @existed: Upon completion, indicates that an identical reference object
 * already existed, and the refcount was upped on that object instead.
 * @require_existed: Fail with -EPERM if an identical ref object didn't
 * already exist.
 *
 * Checks that the base object is shareable and adds a ref object to it.
 *
 * Adding a ref object to a base object is basically like referencing the
 * base object, but a user-space application holds the reference. When the
 * file corresponding to @tfile is closed, all its reference objects are
 * deleted. A reference object can have different types depending on what
 * it's intended for. It can be refcounting to prevent object destruction,
 * When user-space takes a lock, it can add a ref object to that lock to
 * make sure the lock is released if the application dies. A ref object
 * will hold a single reference on a base object.
 */
extern int ttm_ref_object_add(struct ttm_object_file *tfile,
			      struct ttm_base_object *base,
			      enum ttm_ref_type ref_type, bool *existed,
			      bool require_existed);

extern bool ttm_ref_object_exists(struct ttm_object_file *tfile,
				  struct ttm_base_object *base);

/**
 * ttm_ref_object_base_unref
 *
 * @key: Key representing the base object.
 * @ref_type: Ref type of the ref object to be dereferenced.
 *
 * Unreference a ref object with type @ref_type
 * on the base object identified by @key. If there are no duplicate
 * references, the ref object will be destroyed and the base object
 * will be unreferenced.
 */
extern int ttm_ref_object_base_unref(struct ttm_object_file *tfile,
				     unsigned long key,
				     enum ttm_ref_type ref_type);

/**
 * ttm_object_file_init - initialize a struct ttm_object file
 *
 * @tdev: A struct ttm_object device this file is initialized on.
 * @hash_order: Order of the hash table used to hold the reference objects.
 *
 * This is typically called by the file_ops::open function.
 */

extern struct ttm_object_file *ttm_object_file_init(struct ttm_object_device
						    *tdev,
						    unsigned int hash_order);

/**
 * ttm_object_file_release - release data held by a ttm_object_file
 *
 * @p_tfile: Pointer to pointer to the ttm_object_file object to release.
 * *p_tfile will be set to NULL by this function.
 *
 * Releases all data associated by a ttm_object_file.
 * Typically called from file_ops::release. The caller must
 * ensure that there are no concurrent users of tfile.
 */

extern void ttm_object_file_release(struct ttm_object_file **p_tfile);

/**
 * ttm_object device init - initialize a struct ttm_object_device
 *
 * @mem_glob: struct ttm_mem_global for memory accounting.
 * @hash_order: Order of hash table used to hash the base objects.
 * @ops: DMA buf ops for prime objects of this device.
 *
 * This function is typically called on device initialization to prepare
 * data structures needed for ttm base and ref objects.
 */

extern struct ttm_object_device *
ttm_object_device_init(struct ttm_mem_global *mem_glob,
		       unsigned int hash_order,
		       const struct dma_buf_ops *ops);

/**
 * ttm_object_device_release - release data held by a ttm_object_device
 *
 * @p_tdev: Pointer to pointer to the ttm_object_device object to release.
 * *p_tdev will be set to NULL by this function.
 *
 * Releases all data associated by a ttm_object_device.
 * Typically called from driver::unload before the destruction of the
 * device private data structure.
 */

extern void ttm_object_device_release(struct ttm_object_device **p_tdev);

#define ttm_base_object_kfree(__object, __base)\
	kfree_rcu(__object, __base.rhead)

extern int ttm_prime_object_init(struct ttm_object_file *tfile,
				 size_t size,
				 struct ttm_prime_object *prime,
				 bool shareable,
				 enum ttm_object_type type,
				 void (*refcount_release)
				 (struct ttm_base_object **),
				 void (*ref_obj_release)
				 (struct ttm_base_object *,
				  enum ttm_ref_type ref_type));

static inline enum ttm_object_type
ttm_base_object_type(struct ttm_base_object *base)
{
	return (base->object_type == ttm_prime_type) ?
		container_of(base, struct ttm_prime_object, base)->real_type :
		base->object_type;
}
extern int ttm_prime_fd_to_handle(struct ttm_object_file *tfile,
				  int fd, u32 *handle);
extern int ttm_prime_handle_to_fd(struct ttm_object_file *tfile,
				  uint32_t handle, uint32_t flags,
				  int *prime_fd);

#define ttm_prime_object_kfree(__obj, __prime)		\
	kfree_rcu(__obj, __prime.base.rhead)

/*
 * Extra memory required by the base object's idr storage, which is allocated
 * separately from the base object itself. We estimate an on-average 128 bytes
 * per idr.
 */
#define TTM_OBJ_EXTRA_SIZE 128

struct ttm_base_object *
ttm_base_object_noref_lookup(struct ttm_object_file *tfile, uint32_t key);

/**
 * ttm_base_object_noref_release - release a base object pointer looked up
 * without reference
 *
 * Releases a base object pointer looked up with ttm_base_object_noref_lookup().
 */
static inline void ttm_base_object_noref_release(void)
{
	__acquire(RCU);
	rcu_read_unlock();
}
#endif
