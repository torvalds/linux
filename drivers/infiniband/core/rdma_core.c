/*
 * Copyright (c) 2016, Mellanox Technologies inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/file.h>
#include <linux/anon_inodes.h>
#include <rdma/ib_verbs.h>
#include <rdma/uverbs_types.h>
#include <linux/rcupdate.h>
#include "uverbs.h"
#include "core_priv.h"
#include "rdma_core.h"

void uverbs_uobject_get(struct ib_uobject *uobject)
{
	kref_get(&uobject->ref);
}

static void uverbs_uobject_free(struct kref *ref)
{
	struct ib_uobject *uobj =
		container_of(ref, struct ib_uobject, ref);

	if (uobj->type->type_class->needs_kfree_rcu)
		kfree_rcu(uobj, rcu);
	else
		kfree(uobj);
}

void uverbs_uobject_put(struct ib_uobject *uobject)
{
	kref_put(&uobject->ref, uverbs_uobject_free);
}

static int uverbs_try_lock_object(struct ib_uobject *uobj, bool exclusive)
{
	/*
	 * When a shared access is required, we use a positive counter. Each
	 * shared access request checks that the value != -1 and increment it.
	 * Exclusive access is required for operations like write or destroy.
	 * In exclusive access mode, we check that the counter is zero (nobody
	 * claimed this object) and we set it to -1. Releasing a shared access
	 * lock is done simply by decreasing the counter. As for exclusive
	 * access locks, since only a single one of them is is allowed
	 * concurrently, setting the counter to zero is enough for releasing
	 * this lock.
	 */
	if (!exclusive)
		return __atomic_add_unless(&uobj->usecnt, 1, -1) == -1 ?
			-EBUSY : 0;

	/* lock is either WRITE or DESTROY - should be exclusive */
	return atomic_cmpxchg(&uobj->usecnt, 0, -1) == 0 ? 0 : -EBUSY;
}

static struct ib_uobject *alloc_uobj(struct ib_ucontext *context,
				     const struct uverbs_obj_type *type)
{
	struct ib_uobject *uobj = kzalloc(type->obj_size, GFP_KERNEL);

	if (!uobj)
		return ERR_PTR(-ENOMEM);
	/*
	 * user_handle should be filled by the handler,
	 * The object is added to the list in the commit stage.
	 */
	uobj->context = context;
	uobj->type = type;
	atomic_set(&uobj->usecnt, 0);
	kref_init(&uobj->ref);

	return uobj;
}

static int idr_add_uobj(struct ib_uobject *uobj)
{
	int ret;

	idr_preload(GFP_KERNEL);
	spin_lock(&uobj->context->ufile->idr_lock);

	/*
	 * We start with allocating an idr pointing to NULL. This represents an
	 * object which isn't initialized yet. We'll replace it later on with
	 * the real object once we commit.
	 */
	ret = idr_alloc(&uobj->context->ufile->idr, NULL, 0,
			min_t(unsigned long, U32_MAX - 1, INT_MAX), GFP_NOWAIT);
	if (ret >= 0)
		uobj->id = ret;

	spin_unlock(&uobj->context->ufile->idr_lock);
	idr_preload_end();

	return ret < 0 ? ret : 0;
}

/*
 * It only removes it from the uobjects list, uverbs_uobject_put() is still
 * required.
 */
static void uverbs_idr_remove_uobj(struct ib_uobject *uobj)
{
	spin_lock(&uobj->context->ufile->idr_lock);
	idr_remove(&uobj->context->ufile->idr, uobj->id);
	spin_unlock(&uobj->context->ufile->idr_lock);
}

/* Returns the ib_uobject or an error. The caller should check for IS_ERR. */
static struct ib_uobject *lookup_get_idr_uobject(const struct uverbs_obj_type *type,
						 struct ib_ucontext *ucontext,
						 int id, bool exclusive)
{
	struct ib_uobject *uobj;

	rcu_read_lock();
	/* object won't be released as we're protected in rcu */
	uobj = idr_find(&ucontext->ufile->idr, id);
	if (!uobj) {
		uobj = ERR_PTR(-ENOENT);
		goto free;
	}

	uverbs_uobject_get(uobj);
free:
	rcu_read_unlock();
	return uobj;
}

static struct ib_uobject *lookup_get_fd_uobject(const struct uverbs_obj_type *type,
						struct ib_ucontext *ucontext,
						int id, bool exclusive)
{
	struct file *f;
	struct ib_uobject *uobject;
	const struct uverbs_obj_fd_type *fd_type =
		container_of(type, struct uverbs_obj_fd_type, type);

	if (exclusive)
		return ERR_PTR(-EOPNOTSUPP);

	f = fget(id);
	if (!f)
		return ERR_PTR(-EBADF);

	uobject = f->private_data;
	/*
	 * fget(id) ensures we are not currently running uverbs_close_fd,
	 * and the caller is expected to ensure that uverbs_close_fd is never
	 * done while a call top lookup is possible.
	 */
	if (f->f_op != fd_type->fops) {
		fput(f);
		return ERR_PTR(-EBADF);
	}

	uverbs_uobject_get(uobject);
	return uobject;
}

struct ib_uobject *rdma_lookup_get_uobject(const struct uverbs_obj_type *type,
					   struct ib_ucontext *ucontext,
					   int id, bool exclusive)
{
	struct ib_uobject *uobj;
	int ret;

	uobj = type->type_class->lookup_get(type, ucontext, id, exclusive);
	if (IS_ERR(uobj))
		return uobj;

	if (uobj->type != type) {
		ret = -EINVAL;
		goto free;
	}

	ret = uverbs_try_lock_object(uobj, exclusive);
	if (ret) {
		WARN(ucontext->cleanup_reason,
		     "ib_uverbs: Trying to lookup_get while cleanup context\n");
		goto free;
	}

	return uobj;
free:
	uobj->type->type_class->lookup_put(uobj, exclusive);
	uverbs_uobject_put(uobj);
	return ERR_PTR(ret);
}

static struct ib_uobject *alloc_begin_idr_uobject(const struct uverbs_obj_type *type,
						  struct ib_ucontext *ucontext)
{
	int ret;
	struct ib_uobject *uobj;

	uobj = alloc_uobj(ucontext, type);
	if (IS_ERR(uobj))
		return uobj;

	ret = idr_add_uobj(uobj);
	if (ret)
		goto uobj_put;

	ret = ib_rdmacg_try_charge(&uobj->cg_obj, ucontext->device,
				   RDMACG_RESOURCE_HCA_OBJECT);
	if (ret)
		goto idr_remove;

	return uobj;

idr_remove:
	uverbs_idr_remove_uobj(uobj);
uobj_put:
	uverbs_uobject_put(uobj);
	return ERR_PTR(ret);
}

static struct ib_uobject *alloc_begin_fd_uobject(const struct uverbs_obj_type *type,
						 struct ib_ucontext *ucontext)
{
	const struct uverbs_obj_fd_type *fd_type =
		container_of(type, struct uverbs_obj_fd_type, type);
	int new_fd;
	struct ib_uobject *uobj;
	struct ib_uobject_file *uobj_file;
	struct file *filp;

	new_fd = get_unused_fd_flags(O_CLOEXEC);
	if (new_fd < 0)
		return ERR_PTR(new_fd);

	uobj = alloc_uobj(ucontext, type);
	if (IS_ERR(uobj)) {
		put_unused_fd(new_fd);
		return uobj;
	}

	uobj_file = container_of(uobj, struct ib_uobject_file, uobj);
	filp = anon_inode_getfile(fd_type->name,
				  fd_type->fops,
				  uobj_file,
				  fd_type->flags);
	if (IS_ERR(filp)) {
		put_unused_fd(new_fd);
		uverbs_uobject_put(uobj);
		return (void *)filp;
	}

	uobj_file->uobj.id = new_fd;
	uobj_file->uobj.object = filp;
	uobj_file->ufile = ucontext->ufile;
	INIT_LIST_HEAD(&uobj->list);
	kref_get(&uobj_file->ufile->ref);

	return uobj;
}

struct ib_uobject *rdma_alloc_begin_uobject(const struct uverbs_obj_type *type,
					    struct ib_ucontext *ucontext)
{
	return type->type_class->alloc_begin(type, ucontext);
}

static void uverbs_uobject_add(struct ib_uobject *uobject)
{
	mutex_lock(&uobject->context->uobjects_lock);
	list_add(&uobject->list, &uobject->context->uobjects);
	mutex_unlock(&uobject->context->uobjects_lock);
}

static int __must_check remove_commit_idr_uobject(struct ib_uobject *uobj,
						  enum rdma_remove_reason why)
{
	const struct uverbs_obj_idr_type *idr_type =
		container_of(uobj->type, struct uverbs_obj_idr_type,
			     type);
	int ret = idr_type->destroy_object(uobj, why);

	/*
	 * We can only fail gracefully if the user requested to destroy the
	 * object. In the rest of the cases, just remove whatever you can.
	 */
	if (why == RDMA_REMOVE_DESTROY && ret)
		return ret;

	ib_rdmacg_uncharge(&uobj->cg_obj, uobj->context->device,
			   RDMACG_RESOURCE_HCA_OBJECT);
	uverbs_idr_remove_uobj(uobj);

	return ret;
}

static void alloc_abort_fd_uobject(struct ib_uobject *uobj)
{
	struct ib_uobject_file *uobj_file =
		container_of(uobj, struct ib_uobject_file, uobj);
	struct file *filp = uobj->object;
	int id = uobj_file->uobj.id;

	/* Unsuccessful NEW */
	fput(filp);
	put_unused_fd(id);
}

static int __must_check remove_commit_fd_uobject(struct ib_uobject *uobj,
						 enum rdma_remove_reason why)
{
	const struct uverbs_obj_fd_type *fd_type =
		container_of(uobj->type, struct uverbs_obj_fd_type, type);
	struct ib_uobject_file *uobj_file =
		container_of(uobj, struct ib_uobject_file, uobj);
	int ret = fd_type->context_closed(uobj_file, why);

	if (why == RDMA_REMOVE_DESTROY && ret)
		return ret;

	if (why == RDMA_REMOVE_DURING_CLEANUP) {
		alloc_abort_fd_uobject(uobj);
		return ret;
	}

	uobj_file->uobj.context = NULL;
	return ret;
}

static void lockdep_check(struct ib_uobject *uobj, bool exclusive)
{
#ifdef CONFIG_LOCKDEP
	if (exclusive)
		WARN_ON(atomic_read(&uobj->usecnt) > 0);
	else
		WARN_ON(atomic_read(&uobj->usecnt) == -1);
#endif
}

static int __must_check _rdma_remove_commit_uobject(struct ib_uobject *uobj,
						    enum rdma_remove_reason why)
{
	int ret;
	struct ib_ucontext *ucontext = uobj->context;

	ret = uobj->type->type_class->remove_commit(uobj, why);
	if (ret && why == RDMA_REMOVE_DESTROY) {
		/* We couldn't remove the object, so just unlock the uobject */
		atomic_set(&uobj->usecnt, 0);
		uobj->type->type_class->lookup_put(uobj, true);
	} else {
		mutex_lock(&ucontext->uobjects_lock);
		list_del(&uobj->list);
		mutex_unlock(&ucontext->uobjects_lock);
		/* put the ref we took when we created the object */
		uverbs_uobject_put(uobj);
	}

	return ret;
}

/* This is called only for user requested DESTROY reasons */
int __must_check rdma_remove_commit_uobject(struct ib_uobject *uobj)
{
	int ret;
	struct ib_ucontext *ucontext = uobj->context;

	/* put the ref count we took at lookup_get */
	uverbs_uobject_put(uobj);
	/* Cleanup is running. Calling this should have been impossible */
	if (!down_read_trylock(&ucontext->cleanup_rwsem)) {
		WARN(true, "ib_uverbs: Cleanup is running while removing an uobject\n");
		return 0;
	}
	lockdep_check(uobj, true);
	ret = _rdma_remove_commit_uobject(uobj, RDMA_REMOVE_DESTROY);

	up_read(&ucontext->cleanup_rwsem);
	return ret;
}

static void alloc_commit_idr_uobject(struct ib_uobject *uobj)
{
	uverbs_uobject_add(uobj);
	spin_lock(&uobj->context->ufile->idr_lock);
	/*
	 * We already allocated this IDR with a NULL object, so
	 * this shouldn't fail.
	 */
	WARN_ON(idr_replace(&uobj->context->ufile->idr,
			    uobj, uobj->id));
	spin_unlock(&uobj->context->ufile->idr_lock);
}

static void alloc_commit_fd_uobject(struct ib_uobject *uobj)
{
	struct ib_uobject_file *uobj_file =
		container_of(uobj, struct ib_uobject_file, uobj);

	uverbs_uobject_add(&uobj_file->uobj);
	fd_install(uobj_file->uobj.id, uobj->object);
	/* This shouldn't be used anymore. Use the file object instead */
	uobj_file->uobj.id = 0;
	/* Get another reference as we export this to the fops */
	uverbs_uobject_get(&uobj_file->uobj);
}

int rdma_alloc_commit_uobject(struct ib_uobject *uobj)
{
	/* Cleanup is running. Calling this should have been impossible */
	if (!down_read_trylock(&uobj->context->cleanup_rwsem)) {
		int ret;

		WARN(true, "ib_uverbs: Cleanup is running while allocating an uobject\n");
		ret = uobj->type->type_class->remove_commit(uobj,
							    RDMA_REMOVE_DURING_CLEANUP);
		if (ret)
			pr_warn("ib_uverbs: cleanup of idr object %d failed\n",
				uobj->id);
		return ret;
	}

	uobj->type->type_class->alloc_commit(uobj);
	up_read(&uobj->context->cleanup_rwsem);

	return 0;
}

static void alloc_abort_idr_uobject(struct ib_uobject *uobj)
{
	uverbs_idr_remove_uobj(uobj);
	ib_rdmacg_uncharge(&uobj->cg_obj, uobj->context->device,
			   RDMACG_RESOURCE_HCA_OBJECT);
	uverbs_uobject_put(uobj);
}

void rdma_alloc_abort_uobject(struct ib_uobject *uobj)
{
	uobj->type->type_class->alloc_abort(uobj);
}

static void lookup_put_idr_uobject(struct ib_uobject *uobj, bool exclusive)
{
}

static void lookup_put_fd_uobject(struct ib_uobject *uobj, bool exclusive)
{
	struct file *filp = uobj->object;

	WARN_ON(exclusive);
	/* This indirectly calls uverbs_close_fd and free the object */
	fput(filp);
}

void rdma_lookup_put_uobject(struct ib_uobject *uobj, bool exclusive)
{
	lockdep_check(uobj, exclusive);
	uobj->type->type_class->lookup_put(uobj, exclusive);
	/*
	 * In order to unlock an object, either decrease its usecnt for
	 * read access or zero it in case of exclusive access. See
	 * uverbs_try_lock_object for locking schema information.
	 */
	if (!exclusive)
		atomic_dec(&uobj->usecnt);
	else
		atomic_set(&uobj->usecnt, 0);

	uverbs_uobject_put(uobj);
}

const struct uverbs_obj_type_class uverbs_idr_class = {
	.alloc_begin = alloc_begin_idr_uobject,
	.lookup_get = lookup_get_idr_uobject,
	.alloc_commit = alloc_commit_idr_uobject,
	.alloc_abort = alloc_abort_idr_uobject,
	.lookup_put = lookup_put_idr_uobject,
	.remove_commit = remove_commit_idr_uobject,
	/*
	 * When we destroy an object, we first just lock it for WRITE and
	 * actually DESTROY it in the finalize stage. So, the problematic
	 * scenario is when we just started the finalize stage of the
	 * destruction (nothing was executed yet). Now, the other thread
	 * fetched the object for READ access, but it didn't lock it yet.
	 * The DESTROY thread continues and starts destroying the object.
	 * When the other thread continue - without the RCU, it would
	 * access freed memory. However, the rcu_read_lock delays the free
	 * until the rcu_read_lock of the READ operation quits. Since the
	 * exclusive lock of the object is still taken by the DESTROY flow, the
	 * READ operation will get -EBUSY and it'll just bail out.
	 */
	.needs_kfree_rcu = true,
};

static void _uverbs_close_fd(struct ib_uobject_file *uobj_file)
{
	struct ib_ucontext *ucontext;
	struct ib_uverbs_file *ufile = uobj_file->ufile;
	int ret;

	mutex_lock(&uobj_file->ufile->cleanup_mutex);

	/* uobject was either already cleaned up or is cleaned up right now anyway */
	if (!uobj_file->uobj.context ||
	    !down_read_trylock(&uobj_file->uobj.context->cleanup_rwsem))
		goto unlock;

	ucontext = uobj_file->uobj.context;
	ret = _rdma_remove_commit_uobject(&uobj_file->uobj, RDMA_REMOVE_CLOSE);
	up_read(&ucontext->cleanup_rwsem);
	if (ret)
		pr_warn("uverbs: unable to clean up uobject file in uverbs_close_fd.\n");
unlock:
	mutex_unlock(&ufile->cleanup_mutex);
}

void uverbs_close_fd(struct file *f)
{
	struct ib_uobject_file *uobj_file = f->private_data;
	struct kref *uverbs_file_ref = &uobj_file->ufile->ref;

	_uverbs_close_fd(uobj_file);
	uverbs_uobject_put(&uobj_file->uobj);
	kref_put(uverbs_file_ref, ib_uverbs_release_file);
}

void uverbs_cleanup_ucontext(struct ib_ucontext *ucontext, bool device_removed)
{
	enum rdma_remove_reason reason = device_removed ?
		RDMA_REMOVE_DRIVER_REMOVE : RDMA_REMOVE_CLOSE;
	unsigned int cur_order = 0;

	ucontext->cleanup_reason = reason;
	/*
	 * Waits for all remove_commit and alloc_commit to finish. Logically, We
	 * want to hold this forever as the context is going to be destroyed,
	 * but we'll release it since it causes a "held lock freed" BUG message.
	 */
	down_write(&ucontext->cleanup_rwsem);

	while (!list_empty(&ucontext->uobjects)) {
		struct ib_uobject *obj, *next_obj;
		unsigned int next_order = UINT_MAX;

		/*
		 * This shouldn't run while executing other commands on this
		 * context. Thus, the only thing we should take care of is
		 * releasing a FD while traversing this list. The FD could be
		 * closed and released from the _release fop of this FD.
		 * In order to mitigate this, we add a lock.
		 * We take and release the lock per order traversal in order
		 * to let other threads (which might still use the FDs) chance
		 * to run.
		 */
		mutex_lock(&ucontext->uobjects_lock);
		list_for_each_entry_safe(obj, next_obj, &ucontext->uobjects,
					 list) {
			if (obj->type->destroy_order == cur_order) {
				int ret;

				/*
				 * if we hit this WARN_ON, that means we are
				 * racing with a lookup_get.
				 */
				WARN_ON(uverbs_try_lock_object(obj, true));
				ret = obj->type->type_class->remove_commit(obj,
									   reason);
				list_del(&obj->list);
				if (ret)
					pr_warn("ib_uverbs: failed to remove uobject id %d order %u\n",
						obj->id, cur_order);
				/* put the ref we took when we created the object */
				uverbs_uobject_put(obj);
			} else {
				next_order = min(next_order,
						 obj->type->destroy_order);
			}
		}
		mutex_unlock(&ucontext->uobjects_lock);
		cur_order = next_order;
	}
	up_write(&ucontext->cleanup_rwsem);
}

void uverbs_initialize_ucontext(struct ib_ucontext *ucontext)
{
	ucontext->cleanup_reason = 0;
	mutex_init(&ucontext->uobjects_lock);
	INIT_LIST_HEAD(&ucontext->uobjects);
	init_rwsem(&ucontext->cleanup_rwsem);
}

const struct uverbs_obj_type_class uverbs_fd_class = {
	.alloc_begin = alloc_begin_fd_uobject,
	.lookup_get = lookup_get_fd_uobject,
	.alloc_commit = alloc_commit_fd_uobject,
	.alloc_abort = alloc_abort_fd_uobject,
	.lookup_put = lookup_put_fd_uobject,
	.remove_commit = remove_commit_fd_uobject,
	.needs_kfree_rcu = false,
};

