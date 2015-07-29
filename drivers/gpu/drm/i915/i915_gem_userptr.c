/*
 * Copyright © 2012-2014 Intel Corporation
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_drv.h"
#include <linux/mmu_context.h>
#include <linux/mmu_notifier.h>
#include <linux/mempolicy.h>
#include <linux/swap.h>

struct i915_mm_struct {
	struct mm_struct *mm;
	struct drm_device *dev;
	struct i915_mmu_notifier *mn;
	struct hlist_node node;
	struct kref kref;
	struct work_struct work;
};

#if defined(CONFIG_MMU_NOTIFIER)
#include <linux/interval_tree.h>

struct i915_mmu_notifier {
	spinlock_t lock;
	struct hlist_node node;
	struct mmu_notifier mn;
	struct rb_root objects;
	struct list_head linear;
	unsigned long serial;
	bool has_linear;
};

struct i915_mmu_object {
	struct i915_mmu_notifier *mn;
	struct interval_tree_node it;
	struct list_head link;
	struct drm_i915_gem_object *obj;
	bool is_linear;
};

static unsigned long cancel_userptr(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	unsigned long end;

	mutex_lock(&dev->struct_mutex);
	/* Cancel any active worker and force us to re-evaluate gup */
	obj->userptr.work = NULL;

	if (obj->pages != NULL) {
		struct drm_i915_private *dev_priv = to_i915(dev);
		struct i915_vma *vma, *tmp;
		bool was_interruptible;

		was_interruptible = dev_priv->mm.interruptible;
		dev_priv->mm.interruptible = false;

		list_for_each_entry_safe(vma, tmp, &obj->vma_list, vma_link) {
			int ret = i915_vma_unbind(vma);
			WARN_ON(ret && ret != -EIO);
		}
		WARN_ON(i915_gem_object_put_pages(obj));

		dev_priv->mm.interruptible = was_interruptible;
	}

	end = obj->userptr.ptr + obj->base.size;

	drm_gem_object_unreference(&obj->base);
	mutex_unlock(&dev->struct_mutex);

	return end;
}

static void *invalidate_range__linear(struct i915_mmu_notifier *mn,
				      struct mm_struct *mm,
				      unsigned long start,
				      unsigned long end)
{
	struct i915_mmu_object *mo;
	unsigned long serial;

restart:
	serial = mn->serial;
	list_for_each_entry(mo, &mn->linear, link) {
		struct drm_i915_gem_object *obj;

		if (mo->it.last < start || mo->it.start > end)
			continue;

		obj = mo->obj;

		if (!kref_get_unless_zero(&obj->base.refcount))
			continue;

		spin_unlock(&mn->lock);

		cancel_userptr(obj);

		spin_lock(&mn->lock);
		if (serial != mn->serial)
			goto restart;
	}

	return NULL;
}

static void i915_gem_userptr_mn_invalidate_range_start(struct mmu_notifier *_mn,
						       struct mm_struct *mm,
						       unsigned long start,
						       unsigned long end)
{
	struct i915_mmu_notifier *mn = container_of(_mn, struct i915_mmu_notifier, mn);
	struct interval_tree_node *it = NULL;
	unsigned long next = start;
	unsigned long serial = 0;

	end--; /* interval ranges are inclusive, but invalidate range is exclusive */
	while (next < end) {
		struct drm_i915_gem_object *obj = NULL;

		spin_lock(&mn->lock);
		if (mn->has_linear)
			it = invalidate_range__linear(mn, mm, start, end);
		else if (serial == mn->serial)
			it = interval_tree_iter_next(it, next, end);
		else
			it = interval_tree_iter_first(&mn->objects, start, end);
		if (it != NULL) {
			obj = container_of(it, struct i915_mmu_object, it)->obj;

			/* The mmu_object is released late when destroying the
			 * GEM object so it is entirely possible to gain a
			 * reference on an object in the process of being freed
			 * since our serialisation is via the spinlock and not
			 * the struct_mutex - and consequently use it after it
			 * is freed and then double free it.
			 */
			if (!kref_get_unless_zero(&obj->base.refcount)) {
				spin_unlock(&mn->lock);
				serial = 0;
				continue;
			}

			serial = mn->serial;
		}
		spin_unlock(&mn->lock);
		if (obj == NULL)
			return;

		next = cancel_userptr(obj);
	}
}

static const struct mmu_notifier_ops i915_gem_userptr_notifier = {
	.invalidate_range_start = i915_gem_userptr_mn_invalidate_range_start,
};

static struct i915_mmu_notifier *
i915_mmu_notifier_create(struct mm_struct *mm)
{
	struct i915_mmu_notifier *mn;
	int ret;

	mn = kmalloc(sizeof(*mn), GFP_KERNEL);
	if (mn == NULL)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&mn->lock);
	mn->mn.ops = &i915_gem_userptr_notifier;
	mn->objects = RB_ROOT;
	mn->serial = 1;
	INIT_LIST_HEAD(&mn->linear);
	mn->has_linear = false;

	 /* Protected by mmap_sem (write-lock) */
	ret = __mmu_notifier_register(&mn->mn, mm);
	if (ret) {
		kfree(mn);
		return ERR_PTR(ret);
	}

	return mn;
}

static void __i915_mmu_notifier_update_serial(struct i915_mmu_notifier *mn)
{
	if (++mn->serial == 0)
		mn->serial = 1;
}

static int
i915_mmu_notifier_add(struct drm_device *dev,
		      struct i915_mmu_notifier *mn,
		      struct i915_mmu_object *mo)
{
	struct interval_tree_node *it;
	int ret = 0;

	/* By this point we have already done a lot of expensive setup that
	 * we do not want to repeat just because the caller (e.g. X) has a
	 * signal pending (and partly because of that expensive setup, X
	 * using an interrupt timer is likely to get stuck in an EINTR loop).
	 */
	mutex_lock(&dev->struct_mutex);

	/* Make sure we drop the final active reference (and thereby
	 * remove the objects from the interval tree) before we do
	 * the check for overlapping objects.
	 */
	i915_gem_retire_requests(dev);

	spin_lock(&mn->lock);
	it = interval_tree_iter_first(&mn->objects,
				      mo->it.start, mo->it.last);
	if (it) {
		struct drm_i915_gem_object *obj;

		/* We only need to check the first object in the range as it
		 * either has cancelled gup work queued and we need to
		 * return back to the user to give time for the gup-workers
		 * to flush their object references upon which the object will
		 * be removed from the interval-tree, or the the range is
		 * still in use by another client and the overlap is invalid.
		 *
		 * If we do have an overlap, we cannot use the interval tree
		 * for fast range invalidation.
		 */

		obj = container_of(it, struct i915_mmu_object, it)->obj;
		if (!obj->userptr.workers)
			mn->has_linear = mo->is_linear = true;
		else
			ret = -EAGAIN;
	} else
		interval_tree_insert(&mo->it, &mn->objects);

	if (ret == 0) {
		list_add(&mo->link, &mn->linear);
		__i915_mmu_notifier_update_serial(mn);
	}
	spin_unlock(&mn->lock);
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

static bool i915_mmu_notifier_has_linear(struct i915_mmu_notifier *mn)
{
	struct i915_mmu_object *mo;

	list_for_each_entry(mo, &mn->linear, link)
		if (mo->is_linear)
			return true;

	return false;
}

static void
i915_mmu_notifier_del(struct i915_mmu_notifier *mn,
		      struct i915_mmu_object *mo)
{
	spin_lock(&mn->lock);
	list_del(&mo->link);
	if (mo->is_linear)
		mn->has_linear = i915_mmu_notifier_has_linear(mn);
	else
		interval_tree_remove(&mo->it, &mn->objects);
	__i915_mmu_notifier_update_serial(mn);
	spin_unlock(&mn->lock);
}

static void
i915_gem_userptr_release__mmu_notifier(struct drm_i915_gem_object *obj)
{
	struct i915_mmu_object *mo;

	mo = obj->userptr.mmu_object;
	if (mo == NULL)
		return;

	i915_mmu_notifier_del(mo->mn, mo);
	kfree(mo);

	obj->userptr.mmu_object = NULL;
}

static struct i915_mmu_notifier *
i915_mmu_notifier_find(struct i915_mm_struct *mm)
{
	struct i915_mmu_notifier *mn = mm->mn;

	mn = mm->mn;
	if (mn)
		return mn;

	down_write(&mm->mm->mmap_sem);
	mutex_lock(&to_i915(mm->dev)->mm_lock);
	if ((mn = mm->mn) == NULL) {
		mn = i915_mmu_notifier_create(mm->mm);
		if (!IS_ERR(mn))
			mm->mn = mn;
	}
	mutex_unlock(&to_i915(mm->dev)->mm_lock);
	up_write(&mm->mm->mmap_sem);

	return mn;
}

static int
i915_gem_userptr_init__mmu_notifier(struct drm_i915_gem_object *obj,
				    unsigned flags)
{
	struct i915_mmu_notifier *mn;
	struct i915_mmu_object *mo;
	int ret;

	if (flags & I915_USERPTR_UNSYNCHRONIZED)
		return capable(CAP_SYS_ADMIN) ? 0 : -EPERM;

	if (WARN_ON(obj->userptr.mm == NULL))
		return -EINVAL;

	mn = i915_mmu_notifier_find(obj->userptr.mm);
	if (IS_ERR(mn))
		return PTR_ERR(mn);

	mo = kzalloc(sizeof(*mo), GFP_KERNEL);
	if (mo == NULL)
		return -ENOMEM;

	mo->mn = mn;
	mo->it.start = obj->userptr.ptr;
	mo->it.last = mo->it.start + obj->base.size - 1;
	mo->obj = obj;

	ret = i915_mmu_notifier_add(obj->base.dev, mn, mo);
	if (ret) {
		kfree(mo);
		return ret;
	}

	obj->userptr.mmu_object = mo;
	return 0;
}

static void
i915_mmu_notifier_free(struct i915_mmu_notifier *mn,
		       struct mm_struct *mm)
{
	if (mn == NULL)
		return;

	mmu_notifier_unregister(&mn->mn, mm);
	kfree(mn);
}

#else

static void
i915_gem_userptr_release__mmu_notifier(struct drm_i915_gem_object *obj)
{
}

static int
i915_gem_userptr_init__mmu_notifier(struct drm_i915_gem_object *obj,
				    unsigned flags)
{
	if ((flags & I915_USERPTR_UNSYNCHRONIZED) == 0)
		return -ENODEV;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return 0;
}

static void
i915_mmu_notifier_free(struct i915_mmu_notifier *mn,
		       struct mm_struct *mm)
{
}

#endif

static struct i915_mm_struct *
__i915_mm_struct_find(struct drm_i915_private *dev_priv, struct mm_struct *real)
{
	struct i915_mm_struct *mm;

	/* Protected by dev_priv->mm_lock */
	hash_for_each_possible(dev_priv->mm_structs, mm, node, (unsigned long)real)
		if (mm->mm == real)
			return mm;

	return NULL;
}

static int
i915_gem_userptr_init__mm_struct(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	struct i915_mm_struct *mm;
	int ret = 0;

	/* During release of the GEM object we hold the struct_mutex. This
	 * precludes us from calling mmput() at that time as that may be
	 * the last reference and so call exit_mmap(). exit_mmap() will
	 * attempt to reap the vma, and if we were holding a GTT mmap
	 * would then call drm_gem_vm_close() and attempt to reacquire
	 * the struct mutex. So in order to avoid that recursion, we have
	 * to defer releasing the mm reference until after we drop the
	 * struct_mutex, i.e. we need to schedule a worker to do the clean
	 * up.
	 */
	mutex_lock(&dev_priv->mm_lock);
	mm = __i915_mm_struct_find(dev_priv, current->mm);
	if (mm == NULL) {
		mm = kmalloc(sizeof(*mm), GFP_KERNEL);
		if (mm == NULL) {
			ret = -ENOMEM;
			goto out;
		}

		kref_init(&mm->kref);
		mm->dev = obj->base.dev;

		mm->mm = current->mm;
		atomic_inc(&current->mm->mm_count);

		mm->mn = NULL;

		/* Protected by dev_priv->mm_lock */
		hash_add(dev_priv->mm_structs,
			 &mm->node, (unsigned long)mm->mm);
	} else
		kref_get(&mm->kref);

	obj->userptr.mm = mm;
out:
	mutex_unlock(&dev_priv->mm_lock);
	return ret;
}

static void
__i915_mm_struct_free__worker(struct work_struct *work)
{
	struct i915_mm_struct *mm = container_of(work, typeof(*mm), work);
	i915_mmu_notifier_free(mm->mn, mm->mm);
	mmdrop(mm->mm);
	kfree(mm);
}

static void
__i915_mm_struct_free(struct kref *kref)
{
	struct i915_mm_struct *mm = container_of(kref, typeof(*mm), kref);

	/* Protected by dev_priv->mm_lock */
	hash_del(&mm->node);
	mutex_unlock(&to_i915(mm->dev)->mm_lock);

	INIT_WORK(&mm->work, __i915_mm_struct_free__worker);
	schedule_work(&mm->work);
}

static void
i915_gem_userptr_release__mm_struct(struct drm_i915_gem_object *obj)
{
	if (obj->userptr.mm == NULL)
		return;

	kref_put_mutex(&obj->userptr.mm->kref,
		       __i915_mm_struct_free,
		       &to_i915(obj->base.dev)->mm_lock);
	obj->userptr.mm = NULL;
}

struct get_pages_work {
	struct work_struct work;
	struct drm_i915_gem_object *obj;
	struct task_struct *task;
};

#if IS_ENABLED(CONFIG_SWIOTLB)
#define swiotlb_active() swiotlb_nr_tbl()
#else
#define swiotlb_active() 0
#endif

static int
st_set_pages(struct sg_table **st, struct page **pvec, int num_pages)
{
	struct scatterlist *sg;
	int ret, n;

	*st = kmalloc(sizeof(**st), GFP_KERNEL);
	if (*st == NULL)
		return -ENOMEM;

	if (swiotlb_active()) {
		ret = sg_alloc_table(*st, num_pages, GFP_KERNEL);
		if (ret)
			goto err;

		for_each_sg((*st)->sgl, sg, num_pages, n)
			sg_set_page(sg, pvec[n], PAGE_SIZE, 0);
	} else {
		ret = sg_alloc_table_from_pages(*st, pvec, num_pages,
						0, num_pages << PAGE_SHIFT,
						GFP_KERNEL);
		if (ret)
			goto err;
	}

	return 0;

err:
	kfree(*st);
	*st = NULL;
	return ret;
}

static int
__i915_gem_userptr_set_pages(struct drm_i915_gem_object *obj,
			     struct page **pvec, int num_pages)
{
	int ret;

	ret = st_set_pages(&obj->pages, pvec, num_pages);
	if (ret)
		return ret;

	ret = i915_gem_gtt_prepare_object(obj);
	if (ret) {
		sg_free_table(obj->pages);
		kfree(obj->pages);
		obj->pages = NULL;
	}

	return ret;
}

static void
__i915_gem_userptr_get_pages_worker(struct work_struct *_work)
{
	struct get_pages_work *work = container_of(_work, typeof(*work), work);
	struct drm_i915_gem_object *obj = work->obj;
	struct drm_device *dev = obj->base.dev;
	const int num_pages = obj->base.size >> PAGE_SHIFT;
	struct page **pvec;
	int pinned, ret;

	ret = -ENOMEM;
	pinned = 0;

	pvec = kmalloc(num_pages*sizeof(struct page *),
		       GFP_TEMPORARY | __GFP_NOWARN | __GFP_NORETRY);
	if (pvec == NULL)
		pvec = drm_malloc_ab(num_pages, sizeof(struct page *));
	if (pvec != NULL) {
		struct mm_struct *mm = obj->userptr.mm->mm;

		down_read(&mm->mmap_sem);
		while (pinned < num_pages) {
			ret = get_user_pages(work->task, mm,
					     obj->userptr.ptr + pinned * PAGE_SIZE,
					     num_pages - pinned,
					     !obj->userptr.read_only, 0,
					     pvec + pinned, NULL);
			if (ret < 0)
				break;

			pinned += ret;
		}
		up_read(&mm->mmap_sem);
	}

	mutex_lock(&dev->struct_mutex);
	if (obj->userptr.work != &work->work) {
		ret = 0;
	} else if (pinned == num_pages) {
		ret = __i915_gem_userptr_set_pages(obj, pvec, num_pages);
		if (ret == 0) {
			list_add_tail(&obj->global_list, &to_i915(dev)->mm.unbound_list);
			obj->get_page.sg = obj->pages->sgl;
			obj->get_page.last = 0;

			pinned = 0;
		}
	}

	obj->userptr.work = ERR_PTR(ret);
	obj->userptr.workers--;
	drm_gem_object_unreference(&obj->base);
	mutex_unlock(&dev->struct_mutex);

	release_pages(pvec, pinned, 0);
	drm_free_large(pvec);

	put_task_struct(work->task);
	kfree(work);
}

static int
i915_gem_userptr_get_pages(struct drm_i915_gem_object *obj)
{
	const int num_pages = obj->base.size >> PAGE_SHIFT;
	struct page **pvec;
	int pinned, ret;

	/* If userspace should engineer that these pages are replaced in
	 * the vma between us binding this page into the GTT and completion
	 * of rendering... Their loss. If they change the mapping of their
	 * pages they need to create a new bo to point to the new vma.
	 *
	 * However, that still leaves open the possibility of the vma
	 * being copied upon fork. Which falls under the same userspace
	 * synchronisation issue as a regular bo, except that this time
	 * the process may not be expecting that a particular piece of
	 * memory is tied to the GPU.
	 *
	 * Fortunately, we can hook into the mmu_notifier in order to
	 * discard the page references prior to anything nasty happening
	 * to the vma (discard or cloning) which should prevent the more
	 * egregious cases from causing harm.
	 */

	pvec = NULL;
	pinned = 0;
	if (obj->userptr.mm->mm == current->mm) {
		pvec = kmalloc(num_pages*sizeof(struct page *),
			       GFP_TEMPORARY | __GFP_NOWARN | __GFP_NORETRY);
		if (pvec == NULL) {
			pvec = drm_malloc_ab(num_pages, sizeof(struct page *));
			if (pvec == NULL)
				return -ENOMEM;
		}

		pinned = __get_user_pages_fast(obj->userptr.ptr, num_pages,
					       !obj->userptr.read_only, pvec);
	}
	if (pinned < num_pages) {
		if (pinned < 0) {
			ret = pinned;
			pinned = 0;
		} else {
			/* Spawn a worker so that we can acquire the
			 * user pages without holding our mutex. Access
			 * to the user pages requires mmap_sem, and we have
			 * a strict lock ordering of mmap_sem, struct_mutex -
			 * we already hold struct_mutex here and so cannot
			 * call gup without encountering a lock inversion.
			 *
			 * Userspace will keep on repeating the operation
			 * (thanks to EAGAIN) until either we hit the fast
			 * path or the worker completes. If the worker is
			 * cancelled or superseded, the task is still run
			 * but the results ignored. (This leads to
			 * complications that we may have a stray object
			 * refcount that we need to be wary of when
			 * checking for existing objects during creation.)
			 * If the worker encounters an error, it reports
			 * that error back to this function through
			 * obj->userptr.work = ERR_PTR.
			 */
			ret = -EAGAIN;
			if (obj->userptr.work == NULL &&
			    obj->userptr.workers < I915_GEM_USERPTR_MAX_WORKERS) {
				struct get_pages_work *work;

				work = kmalloc(sizeof(*work), GFP_KERNEL);
				if (work != NULL) {
					obj->userptr.work = &work->work;
					obj->userptr.workers++;

					work->obj = obj;
					drm_gem_object_reference(&obj->base);

					work->task = current;
					get_task_struct(work->task);

					INIT_WORK(&work->work, __i915_gem_userptr_get_pages_worker);
					schedule_work(&work->work);
				} else
					ret = -ENOMEM;
			} else {
				if (IS_ERR(obj->userptr.work)) {
					ret = PTR_ERR(obj->userptr.work);
					obj->userptr.work = NULL;
				}
			}
		}
	} else {
		ret = __i915_gem_userptr_set_pages(obj, pvec, num_pages);
		if (ret == 0) {
			obj->userptr.work = NULL;
			pinned = 0;
		}
	}

	release_pages(pvec, pinned, 0);
	drm_free_large(pvec);
	return ret;
}

static void
i915_gem_userptr_put_pages(struct drm_i915_gem_object *obj)
{
	struct sg_page_iter sg_iter;

	BUG_ON(obj->userptr.work != NULL);

	if (obj->madv != I915_MADV_WILLNEED)
		obj->dirty = 0;

	i915_gem_gtt_finish_object(obj);

	for_each_sg_page(obj->pages->sgl, &sg_iter, obj->pages->nents, 0) {
		struct page *page = sg_page_iter_page(&sg_iter);

		if (obj->dirty)
			set_page_dirty(page);

		mark_page_accessed(page);
		page_cache_release(page);
	}
	obj->dirty = 0;

	sg_free_table(obj->pages);
	kfree(obj->pages);
}

static void
i915_gem_userptr_release(struct drm_i915_gem_object *obj)
{
	i915_gem_userptr_release__mmu_notifier(obj);
	i915_gem_userptr_release__mm_struct(obj);
}

static int
i915_gem_userptr_dmabuf_export(struct drm_i915_gem_object *obj)
{
	if (obj->userptr.mmu_object)
		return 0;

	return i915_gem_userptr_init__mmu_notifier(obj, 0);
}

static const struct drm_i915_gem_object_ops i915_gem_userptr_ops = {
	.dmabuf_export = i915_gem_userptr_dmabuf_export,
	.get_pages = i915_gem_userptr_get_pages,
	.put_pages = i915_gem_userptr_put_pages,
	.release = i915_gem_userptr_release,
};

/**
 * Creates a new mm object that wraps some normal memory from the process
 * context - user memory.
 *
 * We impose several restrictions upon the memory being mapped
 * into the GPU.
 * 1. It must be page aligned (both start/end addresses, i.e ptr and size).
 * 2. It must be normal system memory, not a pointer into another map of IO
 *    space (e.g. it must not be a GTT mmapping of another object).
 * 3. We only allow a bo as large as we could in theory map into the GTT,
 *    that is we limit the size to the total size of the GTT.
 * 4. The bo is marked as being snoopable. The backing pages are left
 *    accessible directly by the CPU, but reads and writes by the GPU may
 *    incur the cost of a snoop (unless you have an LLC architecture).
 *
 * Synchronisation between multiple users and the GPU is left to userspace
 * through the normal set-domain-ioctl. The kernel will enforce that the
 * GPU relinquishes the VMA before it is returned back to the system
 * i.e. upon free(), munmap() or process termination. However, the userspace
 * malloc() library may not immediately relinquish the VMA after free() and
 * instead reuse it whilst the GPU is still reading and writing to the VMA.
 * Caveat emptor.
 *
 * Also note, that the object created here is not currently a "first class"
 * object, in that several ioctls are banned. These are the CPU access
 * ioctls: mmap(), pwrite and pread. In practice, you are expected to use
 * direct access via your pointer rather than use those ioctls.
 *
 * If you think this is a good interface to use to pass GPU memory between
 * drivers, please use dma-buf instead. In fact, wherever possible use
 * dma-buf instead.
 */
int
i915_gem_userptr_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_i915_gem_userptr *args = data;
	struct drm_i915_gem_object *obj;
	int ret;
	u32 handle;

	if (args->flags & ~(I915_USERPTR_READ_ONLY |
			    I915_USERPTR_UNSYNCHRONIZED))
		return -EINVAL;

	if (offset_in_page(args->user_ptr | args->user_size))
		return -EINVAL;

	if (!access_ok(args->flags & I915_USERPTR_READ_ONLY ? VERIFY_READ : VERIFY_WRITE,
		       (char __user *)(unsigned long)args->user_ptr, args->user_size))
		return -EFAULT;

	if (args->flags & I915_USERPTR_READ_ONLY) {
		/* On almost all of the current hw, we cannot tell the GPU that a
		 * page is readonly, so this is just a placeholder in the uAPI.
		 */
		return -ENODEV;
	}

	obj = i915_gem_object_alloc(dev);
	if (obj == NULL)
		return -ENOMEM;

	drm_gem_private_object_init(dev, &obj->base, args->user_size);
	i915_gem_object_init(obj, &i915_gem_userptr_ops);
	obj->cache_level = I915_CACHE_LLC;
	obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	obj->base.read_domains = I915_GEM_DOMAIN_CPU;

	obj->userptr.ptr = args->user_ptr;
	obj->userptr.read_only = !!(args->flags & I915_USERPTR_READ_ONLY);

	/* And keep a pointer to the current->mm for resolving the user pages
	 * at binding. This means that we need to hook into the mmu_notifier
	 * in order to detect if the mmu is destroyed.
	 */
	ret = i915_gem_userptr_init__mm_struct(obj);
	if (ret == 0)
		ret = i915_gem_userptr_init__mmu_notifier(obj, args->flags);
	if (ret == 0)
		ret = drm_gem_handle_create(file, &obj->base, &handle);

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(&obj->base);
	if (ret)
		return ret;

	args->handle = handle;
	return 0;
}

int
i915_gem_init_userptr(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	mutex_init(&dev_priv->mm_lock);
	hash_init(dev_priv->mm_structs);
	return 0;
}
