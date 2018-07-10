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

int uverbs_ns_idx(u16 *id, unsigned int ns_count)
{
	int ret = (*id & UVERBS_ID_NS_MASK) >> UVERBS_ID_NS_SHIFT;

	if (ret >= ns_count)
		return -EINVAL;

	*id &= ~UVERBS_ID_NS_MASK;
	return ret;
}

const struct uverbs_object_spec *uverbs_get_object(struct ib_uverbs_file *ufile,
						   uint16_t object)
{
	const struct uverbs_root_spec *object_hash = ufile->device->specs_root;
	const struct uverbs_object_spec_hash *objects;
	int ret = uverbs_ns_idx(&object, object_hash->num_buckets);

	if (ret < 0)
		return NULL;

	objects = object_hash->object_buckets[ret];

	if (object >= objects->num_objects)
		return NULL;

	return objects->objects[object];
}

const struct uverbs_method_spec *uverbs_get_method(const struct uverbs_object_spec *object,
						   uint16_t method)
{
	const struct uverbs_method_spec_hash *methods;
	int ret = uverbs_ns_idx(&method, object->num_buckets);

	if (ret < 0)
		return NULL;

	methods = object->method_buckets[ret];
	if (method >= methods->num_methods)
		return NULL;

	return methods->methods[method];
}

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

/*
 * Does both rdma_lookup_get_uobject() and rdma_remove_commit_uobject(), then
 * returns success_res on success (negative errno on failure). For use by
 * callers that do not need the uobj.
 */
int __uobj_perform_destroy(const struct uverbs_obj_type *type, u32 id,
			   struct ib_uverbs_file *ufile, int success_res)
{
	struct ib_uobject *uobj;
	int ret;

	uobj = rdma_lookup_get_uobject(type, ufile, id, true);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	ret = rdma_remove_commit_uobject(uobj);
	if (ret)
		return ret;

	return success_res;
}

static struct ib_uobject *alloc_uobj(struct ib_uverbs_file *ufile,
				     const struct uverbs_obj_type *type)
{
	struct ib_uobject *uobj;
	struct ib_ucontext *ucontext;

	ucontext = ib_uverbs_get_ucontext(ufile);
	if (IS_ERR(ucontext))
		return ERR_CAST(ucontext);

	uobj = kzalloc(type->obj_size, GFP_KERNEL);
	if (!uobj)
		return ERR_PTR(-ENOMEM);
	/*
	 * user_handle should be filled by the handler,
	 * The object is added to the list in the commit stage.
	 */
	uobj->ufile = ufile;
	uobj->context = ucontext;
	INIT_LIST_HEAD(&uobj->list);
	uobj->type = type;
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
lookup_get_idr_uobject(const struct uverbs_obj_type *type,
		       struct ib_uverbs_file *ufile, s64 id, bool exclusive)
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

static struct ib_uobject *lookup_get_fd_uobject(const struct uverbs_obj_type *type,
						struct ib_uverbs_file *ufile,
						s64 id, bool exclusive)
{
	struct file *f;
	struct ib_uobject *uobject;
	int fdno = id;
	const struct uverbs_obj_fd_type *fd_type =
		container_of(type, struct uverbs_obj_fd_type, type);

	if (fdno != id)
		return ERR_PTR(-EINVAL);

	if (exclusive)
		return ERR_PTR(-EOPNOTSUPP);

	f = fget(fdno);
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
					   struct ib_uverbs_file *ufile, s64 id,
					   bool exclusive)
{
	struct ib_uobject *uobj;
	int ret;

	uobj = type->type_class->lookup_get(type, ufile, id, exclusive);
	if (IS_ERR(uobj))
		return uobj;

	if (uobj->type != type) {
		ret = -EINVAL;
		goto free;
	}

	ret = uverbs_try_lock_object(uobj, exclusive);
	if (ret)
		goto free;

	return uobj;
free:
	uobj->type->type_class->lookup_put(uobj, exclusive);
	uverbs_uobject_put(uobj);
	return ERR_PTR(ret);
}

static struct ib_uobject *alloc_begin_idr_uobject(const struct uverbs_obj_type *type,
						  struct ib_uverbs_file *ufile)
{
	int ret;
	struct ib_uobject *uobj;

	uobj = alloc_uobj(ufile, type);
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

static struct ib_uobject *alloc_begin_fd_uobject(const struct uverbs_obj_type *type,
						 struct ib_uverbs_file *ufile)
{
	int new_fd;
	struct ib_uobject *uobj;

	new_fd = get_unused_fd_flags(O_CLOEXEC);
	if (new_fd < 0)
		return ERR_PTR(new_fd);

	uobj = alloc_uobj(ufile, type);
	if (IS_ERR(uobj)) {
		put_unused_fd(new_fd);
		return uobj;
	}

	uobj->id = new_fd;
	uobj->ufile = ufile;

	return uobj;
}

struct ib_uobject *rdma_alloc_begin_uobject(const struct uverbs_obj_type *type,
					    struct ib_uverbs_file *ufile)
{
	return type->type_class->alloc_begin(type, ufile);
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
	 * object or when a retry may be called upon an error.
	 * In the rest of the cases, just remove whatever you can.
	 */
	if (ib_is_destroy_retryable(ret, why, uobj))
		return ret;

	ib_rdmacg_uncharge(&uobj->cg_obj, uobj->context->device,
			   RDMACG_RESOURCE_HCA_OBJECT);

	spin_lock(&uobj->ufile->idr_lock);
	idr_remove(&uobj->ufile->idr, uobj->id);
	spin_unlock(&uobj->ufile->idr_lock);

	/* Matches the kref in alloc_commit_idr_uobject */
	uverbs_uobject_put(uobj);

	return ret;
}

static void alloc_abort_fd_uobject(struct ib_uobject *uobj)
{
	put_unused_fd(uobj->id);

	/* Pairs with the kref from alloc_begin_idr_uobject */
	uverbs_uobject_put(uobj);
}

static int __must_check remove_commit_fd_uobject(struct ib_uobject *uobj,
						 enum rdma_remove_reason why)
{
	const struct uverbs_obj_fd_type *fd_type =
		container_of(uobj->type, struct uverbs_obj_fd_type, type);
	int ret = fd_type->context_closed(uobj, why);

	if (ib_is_destroy_retryable(ret, why, uobj))
		return ret;

	if (why == RDMA_REMOVE_DURING_CLEANUP) {
		alloc_abort_fd_uobject(uobj);
		return ret;
	}

	uobj->context = NULL;
	return ret;
}

static void assert_uverbs_usecnt(struct ib_uobject *uobj, bool exclusive)
{
#ifdef CONFIG_LOCKDEP
	if (exclusive)
		WARN_ON(atomic_read(&uobj->usecnt) != -1);
	else
		WARN_ON(atomic_read(&uobj->usecnt) <= 0);
#endif
}

static int __must_check _rdma_remove_commit_uobject(struct ib_uobject *uobj,
						    enum rdma_remove_reason why)
{
	struct ib_uverbs_file *ufile = uobj->ufile;
	int ret;

	if (!uobj->object)
		return 0;

	ret = uobj->type->type_class->remove_commit(uobj, why);
	if (ib_is_destroy_retryable(ret, why, uobj))
		return ret;

	uobj->object = NULL;

	spin_lock_irq(&ufile->uobjects_lock);
	list_del(&uobj->list);
	spin_unlock_irq(&ufile->uobjects_lock);
	/* Pairs with the get in rdma_alloc_commit_uobject() */
	uverbs_uobject_put(uobj);

	return ret;
}

/* This is called only for user requested DESTROY reasons
 * rdma_lookup_get_uobject(exclusive=true) must have been called to get uobj,
 * and after this returns the corresponding put has been done, and the kref
 * for uobj has been consumed.
 */
int __must_check rdma_remove_commit_uobject(struct ib_uobject *uobj)
{
	int ret;

	ret = rdma_explicit_destroy(uobj);
	/* Pairs with the lookup_get done by the caller */
	rdma_lookup_put_uobject(uobj, true);
	return ret;
}

int rdma_explicit_destroy(struct ib_uobject *uobject)
{
	int ret;
	struct ib_uverbs_file *ufile = uobject->ufile;

	/* Cleanup is running. Calling this should have been impossible */
	if (!down_read_trylock(&ufile->hw_destroy_rwsem)) {
		WARN(true, "ib_uverbs: Cleanup is running while removing an uobject\n");
		return 0;
	}
	assert_uverbs_usecnt(uobject, true);
	ret = _rdma_remove_commit_uobject(uobject, RDMA_REMOVE_DESTROY);

	up_read(&ufile->hw_destroy_rwsem);
	return ret;
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
	const struct uverbs_obj_fd_type *fd_type =
		container_of(uobj->type, struct uverbs_obj_fd_type, type);
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

	/* Cleanup is running. Calling this should have been impossible */
	if (!down_read_trylock(&ufile->hw_destroy_rwsem)) {
		WARN(true, "ib_uverbs: Cleanup is running while allocating an uobject\n");
		ret = uobj->type->type_class->remove_commit(uobj,
							    RDMA_REMOVE_DURING_CLEANUP);
		if (ret)
			pr_warn("ib_uverbs: cleanup of idr object %d failed\n",
				uobj->id);
		return ret;
	}

	assert_uverbs_usecnt(uobj, true);

	/* alloc_commit consumes the uobj kref */
	ret = uobj->type->type_class->alloc_commit(uobj);
	if (ret) {
		if (uobj->type->type_class->remove_commit(
			    uobj, RDMA_REMOVE_DURING_CLEANUP))
			pr_warn("ib_uverbs: cleanup of idr object %d failed\n",
				uobj->id);
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

	up_read(&ufile->hw_destroy_rwsem);

	return 0;
}

static void alloc_abort_idr_uobject(struct ib_uobject *uobj)
{
	ib_rdmacg_uncharge(&uobj->cg_obj, uobj->context->device,
			   RDMACG_RESOURCE_HCA_OBJECT);

	spin_lock(&uobj->ufile->idr_lock);
	/* The value of the handle in the IDR is NULL at this point. */
	idr_remove(&uobj->ufile->idr, uobj->id);
	spin_unlock(&uobj->ufile->idr_lock);

	/* Pairs with the kref from alloc_begin_idr_uobject */
	uverbs_uobject_put(uobj);
}

/*
 * This consumes the kref for uobj. It is up to the caller to unwind the HW
 * object and anything else connected to uobj before calling this.
 */
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
	assert_uverbs_usecnt(uobj, exclusive);
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

	/* Pairs with the kref obtained by type->lookup_get */
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
EXPORT_SYMBOL(uverbs_idr_class);

static void _uverbs_close_fd(struct ib_uobject *uobj)
{
	int ret;

	/*
	 * uobject was already cleaned up, remove_commit_fd_uobject
	 * sets this
	 */
	if (!uobj->context)
		return;

	/*
	 * lookup_get_fd_uobject holds the kref on the struct file any time a
	 * FD uobj is locked, which prevents this release method from being
	 * invoked. Meaning we can always get the write lock here, or we have
	 * a kernel bug. If so dangle the pointers and bail.
	 */
	ret = uverbs_try_lock_object(uobj, true);
	if (WARN(ret, "uverbs_close_fd() racing with lookup_get_fd_uobject()"))
		return;

	ret = _rdma_remove_commit_uobject(uobj, RDMA_REMOVE_CLOSE);
	if (ret)
		pr_warn("Unable to clean up uobject file in %s\n", __func__);

	atomic_set(&uobj->usecnt, 0);
}

void uverbs_close_fd(struct file *f)
{
	struct ib_uobject *uobj = f->private_data;
	struct ib_uverbs_file *ufile = uobj->ufile;

	if (down_read_trylock(&ufile->hw_destroy_rwsem)) {
		_uverbs_close_fd(uobj);
		up_read(&ufile->hw_destroy_rwsem);
	}

	uobj->object = NULL;
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
	int err = 0;

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
		WARN_ON(uverbs_try_lock_object(obj, true));
		err = obj->type->type_class->remove_commit(obj, reason);

		if (ib_is_destroy_retryable(err, reason, obj)) {
			pr_debug("ib_uverbs: failed to remove uobject id %d err %d\n",
				 obj->id, err);
			atomic_set(&obj->usecnt, 0);
			continue;
		}

		if (err)
			pr_err("ib_uverbs: unable to remove uobject id %d err %d\n",
				obj->id, err);

		list_del(&obj->list);
		/* Pairs with the get in rdma_alloc_commit_uobject() */
		uverbs_uobject_put(obj);
		ret = 0;
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
	.remove_commit = remove_commit_fd_uobject,
	.needs_kfree_rcu = false,
};
EXPORT_SYMBOL(uverbs_fd_class);

struct ib_uobject *
uverbs_get_uobject_from_file(const struct uverbs_obj_type *type_attrs,
			     struct ib_uverbs_file *ufile,
			     enum uverbs_obj_access access, s64 id)
{
	switch (access) {
	case UVERBS_ACCESS_READ:
		return rdma_lookup_get_uobject(type_attrs, ufile, id, false);
	case UVERBS_ACCESS_DESTROY:
	case UVERBS_ACCESS_WRITE:
		return rdma_lookup_get_uobject(type_attrs, ufile, id, true);
	case UVERBS_ACCESS_NEW:
		return rdma_alloc_begin_uobject(type_attrs, ufile);
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
		rdma_lookup_put_uobject(uobj, false);
		break;
	case UVERBS_ACCESS_WRITE:
		rdma_lookup_put_uobject(uobj, true);
		break;
	case UVERBS_ACCESS_DESTROY:
		if (commit)
			ret = rdma_remove_commit_uobject(uobj);
		else
			rdma_lookup_put_uobject(uobj, true);
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
