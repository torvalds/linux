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
#include <linux/sched/mm.h>
#include <rdma/ib_verbs.h>
#include <rdma/uverbs_types.h>
#include <linux/rcupdate.h>
#include <rdma/uverbs_ioctl.h>
#include <rdma/rdma_user_ioctl.h>
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

	if (uobj->uapi_object->type_class->needs_kfree_rcu)
		kfree_rcu(uobj, rcu);
	else
		kfree(uobj);
}

void uverbs_uobject_put(struct ib_uobject *uobject)
{
	kref_put(&uobject->ref, uverbs_uobject_free);
}

static int uverbs_try_lock_object(struct ib_uobject *uobj,
				  enum rdma_lookup_mode mode)
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
	switch (mode) {
	case UVERBS_LOOKUP_READ:
		return atomic_fetch_add_unless(&uobj->usecnt, 1, -1) == -1 ?
			-EBUSY : 0;
	case UVERBS_LOOKUP_WRITE:
		/* lock is exclusive */
		return atomic_cmpxchg(&uobj->usecnt, 0, -1) == 0 ? 0 : -EBUSY;
	case UVERBS_LOOKUP_DESTROY:
		return 0;
	}
	return 0;
}

static void assert_uverbs_usecnt(struct ib_uobject *uobj,
				 enum rdma_lookup_mode mode)
{
#ifdef CONFIG_LOCKDEP
	switch (mode) {
	case UVERBS_LOOKUP_READ:
		WARN_ON(atomic_read(&uobj->usecnt) <= 0);
		break;
	case UVERBS_LOOKUP_WRITE:
		WARN_ON(atomic_read(&uobj->usecnt) != -1);
		break;
	case UVERBS_LOOKUP_DESTROY:
		break;
	}
#endif
}

/*
 * This must be called with the hw_destroy_rwsem locked for read or write,
 * also the uobject itself must be locked for write.
 *
 * Upon return the HW object is guaranteed to be destroyed.
 *
 * For RDMA_REMOVE_ABORT, the hw_destroy_rwsem is not required to be held,
 * however the type's allocat_commit function cannot have been called and the
 * uobject cannot be on the uobjects_lists
 *
 * For RDMA_REMOVE_DESTROY the caller shold be holding a kref (eg via
 * rdma_lookup_get_uobject) and the object is left in a state where the caller
 * needs to call rdma_lookup_put_uobject.
 *
 * For all other destroy modes this function internally unlocks the uobject
 * and consumes the kref on the uobj.
 */
static int uverbs_destroy_uobject(struct ib_uobject *uobj,
				  enum rdma_remove_reason reason)
{
	struct ib_uverbs_file *ufile = uobj->ufile;
	unsigned long flags;
	int ret;

	lockdep_assert_held(&ufile->hw_destroy_rwsem);
	assert_uverbs_usecnt(uobj, UVERBS_LOOKUP_WRITE);

	if (uobj->object) {
		ret = uobj->uapi_object->type_class->destroy_hw(uobj, reason);
		if (ret) {
			if (ib_is_destroy_retryable(ret, reason, uobj))
				return ret;

			/* Nothing to be done, dangle the memory and move on */
			WARN(true,
			     "ib_uverbs: failed to remove uobject id %d, driver err=%d",
			     uobj->id, ret);
		}

		uobj->object = NULL;
	}

	if (reason == RDMA_REMOVE_ABORT) {
		WARN_ON(!list_empty(&uobj->list));
		WARN_ON(!uobj->context);
		uobj->uapi_object->type_class->alloc_abort(uobj);
	}

	uobj->context = NULL;

	/*
	 * For DESTROY the usecnt is not changed, the caller is expected to
	 * manage it via uobj_put_destroy(). Only DESTROY can remove the IDR
	 * handle.
	 */
	if (reason != RDMA_REMOVE_DESTROY)
		atomic_set(&uobj->usecnt, 0);
	else
		uobj->uapi_object->type_class->remove_handle(uobj);

	if (!list_empty(&uobj->list)) {
		spin_lock_irqsave(&ufile->uobjects_lock, flags);
		list_del_init(&uobj->list);
		spin_unlock_irqrestore(&ufile->uobjects_lock, flags);

		/*
		 * Pairs with the get in rdma_alloc_commit_uobject(), could
		 * destroy uobj.
		 */
		uverbs_uobject_put(uobj);
	}

	/*
	 * When aborting the stack kref remains owned by the core code, and is
	 * not transferred into the type. Pairs with the get in alloc_uobj
	 */
	if (reason == RDMA_REMOVE_ABORT)
		uverbs_uobject_put(uobj);

	return 0;
}

/*
 * This calls uverbs_destroy_uobject() using the RDMA_REMOVE_DESTROY
 * sequence. It should only be used from command callbacks. On success the
 * caller must pair this with uobj_put_destroy(). This
 * version requires the caller to have already obtained an
 * LOOKUP_DESTROY uobject kref.
 */
int uobj_destroy(struct ib_uobject *uobj)
{
	struct ib_uverbs_file *ufile = uobj->ufile;
	int ret;

	down_read(&ufile->hw_destroy_rwsem);

	/*
	 * Once the uobject is destroyed by RDMA_REMOVE_DESTROY then it is left
	 * write locked as the callers put it back with UVERBS_LOOKUP_DESTROY.
	 * This is because any other concurrent thread can still see the object
	 * in the xarray due to RCU. Leaving it locked ensures nothing else will
	 * touch it.
	 */
	ret = uverbs_try_lock_object(uobj, UVERBS_LOOKUP_WRITE);
	if (ret)
		goto out_unlock;

	ret = uverbs_destroy_uobject(uobj, RDMA_REMOVE_DESTROY);
	if (ret) {
		atomic_set(&uobj->usecnt, 0);
		goto out_unlock;
	}

out_unlock:
	up_read(&ufile->hw_destroy_rwsem);
	return ret;
}

/*
 * uobj_get_destroy destroys the HW object and returns a handle to the uobj
 * with a NULL object pointer. The caller must pair this with
 * uobj_put_destroy().
 */
struct ib_uobject *__uobj_get_destroy(const struct uverbs_api_object *obj,
				      u32 id, struct ib_uverbs_file *ufile)
{
	struct ib_uobject *uobj;
	int ret;

	uobj = rdma_lookup_get_uobject(obj, ufile, id, UVERBS_LOOKUP_DESTROY);
	if (IS_ERR(uobj))
		return uobj;

	ret = uobj_destroy(uobj);
	if (ret) {
		rdma_lookup_put_uobject(uobj, UVERBS_LOOKUP_DESTROY);
		return ERR_PTR(ret);
	}

	return uobj;
}

/*
 * Does both uobj_get_destroy() and uobj_put_destroy().  Returns success_res
 * on success (negative errno on failure). For use by callers that do not need
 * the uobj.
 */
int __uobj_perform_destroy(const struct uverbs_api_object *obj, u32 id,
			   struct ib_uverbs_file *ufile, int success_res)
{
	struct ib_uobject *uobj;

	uobj = __uobj_get_destroy(obj, id, ufile);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	uobj_put_destroy(uobj);
	return success_res;
}

/* alloc_uobj must be undone by uverbs_destroy_uobject() */
static struct ib_uobject *alloc_uobj(struct ib_uverbs_file *ufile,
				     const struct uverbs_api_object *obj)
{
	struct ib_uobject *uobj;
	struct ib_ucontext *ucontext;

	ucontext = ib_uverbs_get_ucontext(ufile);
	if (IS_ERR(ucontext))
		return ERR_CAST(ucontext);

	uobj = kzalloc(obj->type_attrs->obj_size, GFP_KERNEL);
	if (!uobj)
		return ERR_PTR(-ENOMEM);
	/*
	 * user_handle should be filled by the handler,
	 * The object is added to the list in the commit stage.
	 */
	uobj->ufile = ufile;
	uobj->context = ucontext;
	INIT_LIST_HEAD(&uobj->list);
	uobj->uapi_object = obj;
	/*
	 * Allocated objects start out as write locked to deny any other
	 * syscalls from accessing them until they are committed. See
	 * rdma_alloc_commit_uobject
	 */
	atomic_set(&uobj->usecnt, -1);
	kref_init(&uobj->ref);

	return uobj;
}

static int idr_add_uobj(struct ib_uobject *uobj)
{
	int ret;

	idr_preload(GFP_KERNEL);
	spin_lock(&uobj->ufile->idr_lock);

	/*
	 * We start with allocating an idr pointing to NULL. This represents an
	 * object which isn't initialized yet. We'll replace it later on with
	 * the real object once we commit.
	 */
	ret = idr_alloc(&uobj->ufile->idr, NULL, 0,
			min_t(unsigned long, U32_MAX - 1, INT_MAX), GFP_NOWAIT);
	if (ret >= 0)
		uobj->id = ret;

	spin_unlock(&uobj->ufile->idr_lock);
	idr_preload_end();

	return ret < 0 ? ret : 0;
}

/* Returns the ib_uobject or an error. The caller should check for IS_ERR. */
static struct ib_uobject *
lookup_get_idr_uobject(const struct uverbs_api_object *obj,
		       struct ib_uverbs_file *ufile, s64 id,
		       enum rdma_lookup_mode mode)
{
	struct ib_uobject *uobj;
	unsigned long idrno = id;

	if (id < 0 || id > ULONG_MAX)
		return ERR_PTR(-EINVAL);

	rcu_read_lock();
	/* object won't be released as we're protected in rcu */
	uobj = idr_find(&ufile->idr, idrno);
	if (!uobj) {
		uobj = ERR_PTR(-ENOENT);
		goto free;
	}

	/*
	 * The idr_find is guaranteed to return a pointer to something that
	 * isn't freed yet, or NULL, as the free after idr_remove goes through
	 * kfree_rcu(). However the object may still have been released and
	 * kfree() could be called at any time.
	 */
	if (!kref_get_unless_zero(&uobj->ref))
		uobj = ERR_PTR(-ENOENT);

free:
	rcu_read_unlock();
	return uobj;
}

static struct ib_uobject *
lookup_get_fd_uobject(const struct uverbs_api_object *obj,
		      struct ib_uverbs_file *ufile, s64 id,
		      enum rdma_lookup_mode mode)
{
	const struct uverbs_obj_fd_type *fd_type;
	struct file *f;
	struct ib_uobject *uobject;
	int fdno = id;

	if (fdno != id)
		return ERR_PTR(-EINVAL);

	if (mode != UVERBS_LOOKUP_READ)
		return ERR_PTR(-EOPNOTSUPP);

	if (!obj->type_attrs)
		return ERR_PTR(-EIO);
	fd_type =
		container_of(obj->type_attrs, struct uverbs_obj_fd_type, type);

	f = fget(fdno);
	if (!f)
		return ERR_PTR(-EBADF);

	uobject = f->private_data;
	/*
	 * fget(id) ensures we are not currently running uverbs_close_fd,
	 * and the caller is expected to ensure that uverbs_close_fd is never
	 * done while a call top lookup is possible.
	 */
	if (f->f_op != fd_type->fops || uobject->ufile != ufile) {
		fput(f);
		return ERR_PTR(-EBADF);
	}

	uverbs_uobject_get(uobject);
	return uobject;
}

struct ib_uobject *rdma_lookup_get_uobject(const struct uverbs_api_object *obj,
					   struct ib_uverbs_file *ufile, s64 id,
					   enum rdma_lookup_mode mode)
{
	struct ib_uobject *uobj;
	int ret;

	if (!obj)
		return ERR_PTR(-EINVAL);

	uobj = obj->type_class->lookup_get(obj, ufile, id, mode);
	if (IS_ERR(uobj))
		return uobj;

	if (uobj->uapi_object != obj) {
		ret = -EINVAL;
		goto free;
	}

	/*
	 * If we have been disassociated block every command except for
	 * DESTROY based commands.
	 */
	if (mode != UVERBS_LOOKUP_DESTROY &&
	    !srcu_dereference(ufile->device->ib_dev,
			      &ufile->device->disassociate_srcu)) {
		ret = -EIO;
		goto free;
	}

	ret = uverbs_try_lock_object(uobj, mode);
	if (ret)
		goto free;

	return uobj;
free:
	obj->type_class->lookup_put(uobj, mode);
	uverbs_uobject_put(uobj);
	return ERR_PTR(ret);
}

static struct ib_uobject *
alloc_begin_idr_uobject(const struct uverbs_api_object *obj,
			struct ib_uverbs_file *ufile)
{
	int ret;
	struct ib_uobject *uobj;

	uobj = alloc_uobj(ufile, obj);
	if (IS_ERR(uobj))
		return uobj;

	ret = idr_add_uobj(uobj);
	if (ret)
		goto uobj_put;

	ret = ib_rdmacg_try_charge(&uobj->cg_obj, uobj->context->device,
				   RDMACG_RESOURCE_HCA_OBJECT);
	if (ret)
		goto idr_remove;

	return uobj;

idr_remove:
	spin_lock(&ufile->idr_lock);
	idr_remove(&ufile->idr, uobj->id);
	spin_unlock(&ufile->idr_lock);
uobj_put:
	uverbs_uobject_put(uobj);
	return ERR_PTR(ret);
}

static struct ib_uobject *
alloc_begin_fd_uobject(const struct uverbs_api_object *obj,
		       struct ib_uverbs_file *ufile)
{
	int new_fd;
	struct ib_uobject *uobj;

	new_fd = get_unused_fd_flags(O_CLOEXEC);
	if (new_fd < 0)
		return ERR_PTR(new_fd);

	uobj = alloc_uobj(ufile, obj);
	if (IS_ERR(uobj)) {
		put_unused_fd(new_fd);
		return uobj;
	}

	uobj->id = new_fd;
	uobj->ufile = ufile;

	return uobj;
}

struct ib_uobject *rdma_alloc_begin_uobject(const struct uverbs_api_object *obj,
					    struct ib_uverbs_file *ufile)
{
	struct ib_uobject *ret;

	if (!obj)
		return ERR_PTR(-EINVAL);

	/*
	 * The hw_destroy_rwsem is held across the entire object creation and
	 * released during rdma_alloc_commit_uobject or
	 * rdma_alloc_abort_uobject
	 */
	if (!down_read_trylock(&ufile->hw_destroy_rwsem))
		return ERR_PTR(-EIO);

	ret = obj->type_class->alloc_begin(obj, ufile);
	if (IS_ERR(ret)) {
		up_read(&ufile->hw_destroy_rwsem);
		return ret;
	}
	return ret;
}

static void alloc_abort_idr_uobject(struct ib_uobject *uobj)
{
	ib_rdmacg_uncharge(&uobj->cg_obj, uobj->context->device,
			   RDMACG_RESOURCE_HCA_OBJECT);

	spin_lock(&uobj->ufile->idr_lock);
	idr_remove(&uobj->ufile->idr, uobj->id);
	spin_unlock(&uobj->ufile->idr_lock);
}

static int __must_check destroy_hw_idr_uobject(struct ib_uobject *uobj,
					       enum rdma_remove_reason why)
{
	const struct uverbs_obj_idr_type *idr_type =
		container_of(uobj->uapi_object->type_attrs,
			     struct uverbs_obj_idr_type, type);
	int ret = idr_type->destroy_object(uobj, why);

	/*
	 * We can only fail gracefully if the user requested to destroy the
	 * object or when a retry may be called upon an error.
	 * In the rest of the cases, just remove whatever you can.
	 */
	if (ib_is_destroy_retryable(ret, why, uobj))
		return ret;

	if (why == RDMA_REMOVE_ABORT)
		return 0;

	ib_rdmacg_uncharge(&uobj->cg_obj, uobj->context->device,
			   RDMACG_RESOURCE_HCA_OBJECT);

	return 0;
}

static void remove_handle_idr_uobject(struct ib_uobject *uobj)
{
	spin_lock(&uobj->ufile->idr_lock);
	idr_remove(&uobj->ufile->idr, uobj->id);
	spin_unlock(&uobj->ufile->idr_lock);
	/* Matches the kref in alloc_commit_idr_uobject */
	uverbs_uobject_put(uobj);
}

static void alloc_abort_fd_uobject(struct ib_uobject *uobj)
{
	put_unused_fd(uobj->id);
}

static int __must_check destroy_hw_fd_uobject(struct ib_uobject *uobj,
					      enum rdma_remove_reason why)
{
	const struct uverbs_obj_fd_type *fd_type = container_of(
		uobj->uapi_object->type_attrs, struct uverbs_obj_fd_type, type);
	int ret = fd_type->context_closed(uobj, why);

	if (ib_is_destroy_retryable(ret, why, uobj))
		return ret;

	return 0;
}

static void remove_handle_fd_uobject(struct ib_uobject *uobj)
{
}

static int alloc_commit_idr_uobject(struct ib_uobject *uobj)
{
	struct ib_uverbs_file *ufile = uobj->ufile;

	spin_lock(&ufile->idr_lock);
	/*
	 * We already allocated this IDR with a NULL object, so
	 * this shouldn't fail.
	 *
	 * NOTE: Once we set the IDR we loose ownership of our kref on uobj.
	 * It will be put by remove_commit_idr_uobject()
	 */
	WARN_ON(idr_replace(&ufile->idr, uobj, uobj->id));
	spin_unlock(&ufile->idr_lock);

	return 0;
}

static int alloc_commit_fd_uobject(struct ib_uobject *uobj)
{
	const struct uverbs_obj_fd_type *fd_type = container_of(
		uobj->uapi_object->type_attrs, struct uverbs_obj_fd_type, type);
	int fd = uobj->id;
	struct file *filp;

	/*
	 * The kref for uobj is moved into filp->private data and put in
	 * uverbs_close_fd(). Once alloc_commit() succeeds uverbs_close_fd()
	 * must be guaranteed to be called from the provided fops release
	 * callback.
	 */
	filp = anon_inode_getfile(fd_type->name,
				  fd_type->fops,
				  uobj,
				  fd_type->flags);
	if (IS_ERR(filp))
		return PTR_ERR(filp);

	uobj->object = filp;

	/* Matching put will be done in uverbs_close_fd() */
	kref_get(&uobj->ufile->ref);

	/* This shouldn't be used anymore. Use the file object instead */
	uobj->id = 0;

	/*
	 * NOTE: Once we install the file we loose ownership of our kref on
	 * uobj. It will be put by uverbs_close_fd()
	 */
	fd_install(fd, filp);

	return 0;
}

/*
 * In all cases rdma_alloc_commit_uobject() consumes the kref to uobj and the
 * caller can no longer assume uobj is valid. If this function fails it
 * destroys the uboject, including the attached HW object.
 */
int __must_check rdma_alloc_commit_uobject(struct ib_uobject *uobj)
{
	struct ib_uverbs_file *ufile = uobj->ufile;
	int ret;

	/* alloc_commit consumes the uobj kref */
	ret = uobj->uapi_object->type_class->alloc_commit(uobj);
	if (ret) {
		uverbs_destroy_uobject(uobj, RDMA_REMOVE_ABORT);
		up_read(&ufile->hw_destroy_rwsem);
		return ret;
	}

	/* kref is held so long as the uobj is on the uobj list. */
	uverbs_uobject_get(uobj);
	spin_lock_irq(&ufile->uobjects_lock);
	list_add(&uobj->list, &ufile->uobjects);
	spin_unlock_irq(&ufile->uobjects_lock);

	/* matches atomic_set(-1) in alloc_uobj */
	atomic_set(&uobj->usecnt, 0);

	/* Matches the down_read in rdma_alloc_begin_uobject */
	up_read(&ufile->hw_destroy_rwsem);

	return 0;
}

/*
 * This consumes the kref for uobj. It is up to the caller to unwind the HW
 * object and anything else connected to uobj before calling this.
 */
void rdma_alloc_abort_uobject(struct ib_uobject *uobj)
{
	struct ib_uverbs_file *ufile = uobj->ufile;

	uobj->object = NULL;
	uverbs_destroy_uobject(uobj, RDMA_REMOVE_ABORT);

	/* Matches the down_read in rdma_alloc_begin_uobject */
	up_read(&ufile->hw_destroy_rwsem);
}

static void lookup_put_idr_uobject(struct ib_uobject *uobj,
				   enum rdma_lookup_mode mode)
{
}

static void lookup_put_fd_uobject(struct ib_uobject *uobj,
				  enum rdma_lookup_mode mode)
{
	struct file *filp = uobj->object;

	WARN_ON(mode != UVERBS_LOOKUP_READ);
	/* This indirectly calls uverbs_close_fd and free the object */
	fput(filp);
}

void rdma_lookup_put_uobject(struct ib_uobject *uobj,
			     enum rdma_lookup_mode mode)
{
	assert_uverbs_usecnt(uobj, mode);
	/*
	 * In order to unlock an object, either decrease its usecnt for
	 * read access or zero it in case of exclusive access. See
	 * uverbs_try_lock_object for locking schema information.
	 */
	switch (mode) {
	case UVERBS_LOOKUP_READ:
		atomic_dec(&uobj->usecnt);
		break;
	case UVERBS_LOOKUP_WRITE:
		atomic_set(&uobj->usecnt, 0);
		break;
	case UVERBS_LOOKUP_DESTROY:
		break;
	}

	uobj->uapi_object->type_class->lookup_put(uobj, mode);
	/* Pairs with the kref obtained by type->lookup_get */
	uverbs_uobject_put(uobj);
}

void setup_ufile_idr_uobject(struct ib_uverbs_file *ufile)
{
	spin_lock_init(&ufile->idr_lock);
	idr_init(&ufile->idr);
}

void release_ufile_idr_uobject(struct ib_uverbs_file *ufile)
{
	struct ib_uobject *entry;
	int id;

	/*
	 * At this point uverbs_cleanup_ufile() is guaranteed to have run, and
	 * there are no HW objects left, however the IDR is still populated
	 * with anything that has not been cleaned up by userspace. Since the
	 * kref on ufile is 0, nothing is allowed to call lookup_get.
	 *
	 * This is an optimized equivalent to remove_handle_idr_uobject
	 */
	idr_for_each_entry(&ufile->idr, entry, id) {
		WARN_ON(entry->object);
		uverbs_uobject_put(entry);
	}

	idr_destroy(&ufile->idr);
}

const struct uverbs_obj_type_class uverbs_idr_class = {
	.alloc_begin = alloc_begin_idr_uobject,
	.lookup_get = lookup_get_idr_uobject,
	.alloc_commit = alloc_commit_idr_uobject,
	.alloc_abort = alloc_abort_idr_uobject,
	.lookup_put = lookup_put_idr_uobject,
	.destroy_hw = destroy_hw_idr_uobject,
	.remove_handle = remove_handle_idr_uobject,
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
EXPORT_SYMBOL(uverbs_idr_class);

void uverbs_close_fd(struct file *f)
{
	struct ib_uobject *uobj = f->private_data;
	struct ib_uverbs_file *ufile = uobj->ufile;

	if (down_read_trylock(&ufile->hw_destroy_rwsem)) {
		/*
		 * lookup_get_fd_uobject holds the kref on the struct file any
		 * time a FD uobj is locked, which prevents this release
		 * method from being invoked. Meaning we can always get the
		 * write lock here, or we have a kernel bug.
		 */
		WARN_ON(uverbs_try_lock_object(uobj, UVERBS_LOOKUP_WRITE));
		uverbs_destroy_uobject(uobj, RDMA_REMOVE_CLOSE);
		up_read(&ufile->hw_destroy_rwsem);
	}

	/* Matches the get in alloc_begin_fd_uobject */
	kref_put(&ufile->ref, ib_uverbs_release_file);

	/* Pairs with filp->private_data in alloc_begin_fd_uobject */
	uverbs_uobject_put(uobj);
}

static void ufile_disassociate_ucontext(struct ib_ucontext *ibcontext)
{
	struct ib_device *ib_dev = ibcontext->device;
	struct task_struct *owning_process  = NULL;
	struct mm_struct   *owning_mm       = NULL;

	owning_process = get_pid_task(ibcontext->tgid, PIDTYPE_PID);
	if (!owning_process)
		return;

	owning_mm = get_task_mm(owning_process);
	if (!owning_mm) {
		pr_info("no mm, disassociate ucontext is pending task termination\n");
		while (1) {
			put_task_struct(owning_process);
			usleep_range(1000, 2000);
			owning_process = get_pid_task(ibcontext->tgid,
						      PIDTYPE_PID);
			if (!owning_process ||
			    owning_process->state == TASK_DEAD) {
				pr_info("disassociate ucontext done, task was terminated\n");
				/* in case task was dead need to release the
				 * task struct.
				 */
				if (owning_process)
					put_task_struct(owning_process);
				return;
			}
		}
	}

	down_write(&owning_mm->mmap_sem);
	ib_dev->disassociate_ucontext(ibcontext);
	up_write(&owning_mm->mmap_sem);
	mmput(owning_mm);
	put_task_struct(owning_process);
}

/*
 * Drop the ucontext off the ufile and completely disconnect it from the
 * ib_device
 */
static void ufile_destroy_ucontext(struct ib_uverbs_file *ufile,
				   enum rdma_remove_reason reason)
{
	struct ib_ucontext *ucontext = ufile->ucontext;
	int ret;

	if (reason == RDMA_REMOVE_DRIVER_REMOVE)
		ufile_disassociate_ucontext(ucontext);

	put_pid(ucontext->tgid);
	ib_rdmacg_uncharge(&ucontext->cg_obj, ucontext->device,
			   RDMACG_RESOURCE_HCA_HANDLE);

	/*
	 * FIXME: Drivers are not permitted to fail dealloc_ucontext, remove
	 * the error return.
	 */
	ret = ucontext->device->dealloc_ucontext(ucontext);
	WARN_ON(ret);

	ufile->ucontext = NULL;
}

static int __uverbs_cleanup_ufile(struct ib_uverbs_file *ufile,
				  enum rdma_remove_reason reason)
{
	struct ib_uobject *obj, *next_obj;
	int ret = -EINVAL;

	/*
	 * This shouldn't run while executing other commands on this
	 * context. Thus, the only thing we should take care of is
	 * releasing a FD while traversing this list. The FD could be
	 * closed and released from the _release fop of this FD.
	 * In order to mitigate this, we add a lock.
	 * We take and release the lock per traversal in order to let
	 * other threads (which might still use the FDs) chance to run.
	 */
	list_for_each_entry_safe(obj, next_obj, &ufile->uobjects, list) {
		/*
		 * if we hit this WARN_ON, that means we are
		 * racing with a lookup_get.
		 */
		WARN_ON(uverbs_try_lock_object(obj, UVERBS_LOOKUP_WRITE));
		if (!uverbs_destroy_uobject(obj, reason))
			ret = 0;
		else
			atomic_set(&obj->usecnt, 0);
	}
	return ret;
}

/*
 * Destroy the uncontext and every uobject associated with it. If called with
 * reason != RDMA_REMOVE_CLOSE this will not return until the destruction has
 * been completed and ufile->ucontext is NULL.
 *
 * This is internally locked and can be called in parallel from multiple
 * contexts.
 */
void uverbs_destroy_ufile_hw(struct ib_uverbs_file *ufile,
			     enum rdma_remove_reason reason)
{
	if (reason == RDMA_REMOVE_CLOSE) {
		/*
		 * During destruction we might trigger something that
		 * synchronously calls release on any file descriptor. For
		 * this reason all paths that come from file_operations
		 * release must use try_lock. They can progress knowing that
		 * there is an ongoing uverbs_destroy_ufile_hw that will clean
		 * up the driver resources.
		 */
		if (!mutex_trylock(&ufile->ucontext_lock))
			return;

	} else {
		mutex_lock(&ufile->ucontext_lock);
	}

	down_write(&ufile->hw_destroy_rwsem);

	/*
	 * If a ucontext was never created then we can't have any uobjects to
	 * cleanup, nothing to do.
	 */
	if (!ufile->ucontext)
		goto done;

	ufile->ucontext->closing = true;
	ufile->ucontext->cleanup_retryable = true;
	while (!list_empty(&ufile->uobjects))
		if (__uverbs_cleanup_ufile(ufile, reason)) {
			/*
			 * No entry was cleaned-up successfully during this
			 * iteration
			 */
			break;
		}

	ufile->ucontext->cleanup_retryable = false;
	if (!list_empty(&ufile->uobjects))
		__uverbs_cleanup_ufile(ufile, reason);

	ufile_destroy_ucontext(ufile, reason);

done:
	up_write(&ufile->hw_destroy_rwsem);
	mutex_unlock(&ufile->ucontext_lock);
}

const struct uverbs_obj_type_class uverbs_fd_class = {
	.alloc_begin = alloc_begin_fd_uobject,
	.lookup_get = lookup_get_fd_uobject,
	.alloc_commit = alloc_commit_fd_uobject,
	.alloc_abort = alloc_abort_fd_uobject,
	.lookup_put = lookup_put_fd_uobject,
	.destroy_hw = destroy_hw_fd_uobject,
	.remove_handle = remove_handle_fd_uobject,
	.needs_kfree_rcu = false,
};
EXPORT_SYMBOL(uverbs_fd_class);

struct ib_uobject *
uverbs_get_uobject_from_file(u16 object_id,
			     struct ib_uverbs_file *ufile,
			     enum uverbs_obj_access access, s64 id)
{
	const struct uverbs_api_object *obj =
		uapi_get_object(ufile->device->uapi, object_id);

	switch (access) {
	case UVERBS_ACCESS_READ:
		return rdma_lookup_get_uobject(obj, ufile, id,
					       UVERBS_LOOKUP_READ);
	case UVERBS_ACCESS_DESTROY:
		/* Actual destruction is done inside uverbs_handle_method */
		return rdma_lookup_get_uobject(obj, ufile, id,
					       UVERBS_LOOKUP_DESTROY);
	case UVERBS_ACCESS_WRITE:
		return rdma_lookup_get_uobject(obj, ufile, id,
					       UVERBS_LOOKUP_WRITE);
	case UVERBS_ACCESS_NEW:
		return rdma_alloc_begin_uobject(obj, ufile);
	default:
		WARN_ON(true);
		return ERR_PTR(-EOPNOTSUPP);
	}
}

int uverbs_finalize_object(struct ib_uobject *uobj,
			   enum uverbs_obj_access access,
			   bool commit)
{
	int ret = 0;

	/*
	 * refcounts should be handled at the object level and not at the
	 * uobject level. Refcounts of the objects themselves are done in
	 * handlers.
	 */

	switch (access) {
	case UVERBS_ACCESS_READ:
		rdma_lookup_put_uobject(uobj, UVERBS_LOOKUP_READ);
		break;
	case UVERBS_ACCESS_WRITE:
		rdma_lookup_put_uobject(uobj, UVERBS_LOOKUP_WRITE);
		break;
	case UVERBS_ACCESS_DESTROY:
		if (uobj)
			rdma_lookup_put_uobject(uobj, UVERBS_LOOKUP_DESTROY);
		break;
	case UVERBS_ACCESS_NEW:
		if (commit)
			ret = rdma_alloc_commit_uobject(uobj);
		else
			rdma_alloc_abort_uobject(uobj);
		break;
	default:
		WARN_ON(true);
		ret = -EOPNOTSUPP;
	}

	return ret;
}
