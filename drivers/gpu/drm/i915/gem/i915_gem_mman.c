/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2016 Intel Corporation
 */

#include <linux/anon_inodes.h>
#include <linux/mman.h>
#include <linux/pfn_t.h>
#include <linux/sizes.h>

#include "gt/intel_gt.h"
#include "gt/intel_gt_requests.h"

#include "i915_drv.h"
#include "i915_gem_gtt.h"
#include "i915_gem_ioctls.h"
#include "i915_gem_object.h"
#include "i915_gem_mman.h"
#include "i915_trace.h"
#include "i915_user_extensions.h"
#include "i915_vma.h"

static inline bool
__vma_matches(struct vm_area_struct *vma, struct file *filp,
	      unsigned long addr, unsigned long size)
{
	if (vma->vm_file != filp)
		return false;

	return vma->vm_start == addr &&
	       (vma->vm_end - vma->vm_start) == PAGE_ALIGN(size);
}

/**
 * i915_gem_mmap_ioctl - Maps the contents of an object, returning the address
 *			 it is mapped to.
 * @dev: drm device
 * @data: ioctl data blob
 * @file: drm file
 *
 * While the mapping holds a reference on the contents of the object, it doesn't
 * imply a ref on the object itself.
 *
 * IMPORTANT:
 *
 * DRM driver writers who look a this function as an example for how to do GEM
 * mmap support, please don't implement mmap support like here. The modern way
 * to implement DRM mmap support is with an mmap offset ioctl (like
 * i915_gem_mmap_gtt) and then using the mmap syscall on the DRM fd directly.
 * That way debug tooling like valgrind will understand what's going on, hiding
 * the mmap call in a driver private ioctl will break that. The i915 driver only
 * does cpu mmaps this way because we didn't know better.
 */
int
i915_gem_mmap_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file)
{
	struct drm_i915_gem_mmap *args = data;
	struct drm_i915_gem_object *obj;
	unsigned long addr;

	if (args->flags & ~(I915_MMAP_WC))
		return -EINVAL;

	if (args->flags & I915_MMAP_WC && !boot_cpu_has(X86_FEATURE_PAT))
		return -ENODEV;

	obj = i915_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	/* prime objects have no backing filp to GEM mmap
	 * pages from.
	 */
	if (!obj->base.filp) {
		addr = -ENXIO;
		goto err;
	}

	if (range_overflows(args->offset, args->size, (u64)obj->base.size)) {
		addr = -EINVAL;
		goto err;
	}

	addr = vm_mmap(obj->base.filp, 0, args->size,
		       PROT_READ | PROT_WRITE, MAP_SHARED,
		       args->offset);
	if (IS_ERR_VALUE(addr))
		goto err;

	if (args->flags & I915_MMAP_WC) {
		struct mm_struct *mm = current->mm;
		struct vm_area_struct *vma;

		if (mmap_write_lock_killable(mm)) {
			addr = -EINTR;
			goto err;
		}
		vma = find_vma(mm, addr);
		if (vma && __vma_matches(vma, obj->base.filp, addr, args->size))
			vma->vm_page_prot =
				pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
		else
			addr = -ENOMEM;
		mmap_write_unlock(mm);
		if (IS_ERR_VALUE(addr))
			goto err;
	}
	i915_gem_object_put(obj);

	args->addr_ptr = (u64)addr;
	return 0;

err:
	i915_gem_object_put(obj);
	return addr;
}

static unsigned int tile_row_pages(const struct drm_i915_gem_object *obj)
{
	return i915_gem_object_get_tile_row_size(obj) >> PAGE_SHIFT;
}

/**
 * i915_gem_mmap_gtt_version - report the current feature set for GTT mmaps
 *
 * A history of the GTT mmap interface:
 *
 * 0 - Everything had to fit into the GTT. Both parties of a memcpy had to
 *     aligned and suitable for fencing, and still fit into the available
 *     mappable space left by the pinned display objects. A classic problem
 *     we called the page-fault-of-doom where we would ping-pong between
 *     two objects that could not fit inside the GTT and so the memcpy
 *     would page one object in at the expense of the other between every
 *     single byte.
 *
 * 1 - Objects can be any size, and have any compatible fencing (X Y, or none
 *     as set via i915_gem_set_tiling() [DRM_I915_GEM_SET_TILING]). If the
 *     object is too large for the available space (or simply too large
 *     for the mappable aperture!), a view is created instead and faulted
 *     into userspace. (This view is aligned and sized appropriately for
 *     fenced access.)
 *
 * 2 - Recognise WC as a separate cache domain so that we can flush the
 *     delayed writes via GTT before performing direct access via WC.
 *
 * 3 - Remove implicit set-domain(GTT) and synchronisation on initial
 *     pagefault; swapin remains transparent.
 *
 * 4 - Support multiple fault handlers per object depending on object's
 *     backing storage (a.k.a. MMAP_OFFSET).
 *
 * Restrictions:
 *
 *  * snoopable objects cannot be accessed via the GTT. It can cause machine
 *    hangs on some architectures, corruption on others. An attempt to service
 *    a GTT page fault from a snoopable object will generate a SIGBUS.
 *
 *  * the object must be able to fit into RAM (physical memory, though no
 *    limited to the mappable aperture).
 *
 *
 * Caveats:
 *
 *  * a new GTT page fault will synchronize rendering from the GPU and flush
 *    all data to system memory. Subsequent access will not be synchronized.
 *
 *  * all mappings are revoked on runtime device suspend.
 *
 *  * there are only 8, 16 or 32 fence registers to share between all users
 *    (older machines require fence register for display and blitter access
 *    as well). Contention of the fence registers will cause the previous users
 *    to be unmapped and any new access will generate new page faults.
 *
 *  * running out of memory while servicing a fault may generate a SIGBUS,
 *    rather than the expected SIGSEGV.
 */
int i915_gem_mmap_gtt_version(void)
{
	return 4;
}

static inline struct i915_ggtt_view
compute_partial_view(const struct drm_i915_gem_object *obj,
		     pgoff_t page_offset,
		     unsigned int chunk)
{
	struct i915_ggtt_view view;

	if (i915_gem_object_is_tiled(obj))
		chunk = roundup(chunk, tile_row_pages(obj));

	view.type = I915_GGTT_VIEW_PARTIAL;
	view.partial.offset = rounddown(page_offset, chunk);
	view.partial.size =
		min_t(unsigned int, chunk,
		      (obj->base.size >> PAGE_SHIFT) - view.partial.offset);

	/* If the partial covers the entire object, just create a normal VMA. */
	if (chunk >= obj->base.size >> PAGE_SHIFT)
		view.type = I915_GGTT_VIEW_NORMAL;

	return view;
}

static vm_fault_t i915_error_to_vmf_fault(int err)
{
	switch (err) {
	default:
		WARN_ONCE(err, "unhandled error in %s: %i\n", __func__, err);
		fallthrough;
	case -EIO: /* shmemfs failure from swap device */
	case -EFAULT: /* purged object */
	case -ENODEV: /* bad object, how did you get here! */
	case -ENXIO: /* unable to access backing store (on device) */
		return VM_FAULT_SIGBUS;

	case -ENOMEM: /* our allocation failure */
		return VM_FAULT_OOM;

	case 0:
	case -EAGAIN:
	case -ENOSPC: /* transient failure to evict? */
	case -ERESTARTSYS:
	case -EINTR:
	case -EBUSY:
		/*
		 * EBUSY is ok: this just means that another thread
		 * already did the job.
		 */
		return VM_FAULT_NOPAGE;
	}
}

static vm_fault_t vm_fault_cpu(struct vm_fault *vmf)
{
	struct vm_area_struct *area = vmf->vma;
	struct i915_mmap_offset *mmo = area->vm_private_data;
	struct drm_i915_gem_object *obj = mmo->obj;
	resource_size_t iomap;
	int err;

	/* Sanity check that we allow writing into this object */
	if (unlikely(i915_gem_object_is_readonly(obj) &&
		     area->vm_flags & VM_WRITE))
		return VM_FAULT_SIGBUS;

	err = i915_gem_object_pin_pages(obj);
	if (err)
		goto out;

	iomap = -1;
	if (!i915_gem_object_type_has(obj, I915_GEM_OBJECT_HAS_STRUCT_PAGE)) {
		iomap = obj->mm.region->iomap.base;
		iomap -= obj->mm.region->region.start;
	}

	/* PTEs are revoked in obj->ops->put_pages() */
	err = remap_io_sg(area,
			  area->vm_start, area->vm_end - area->vm_start,
			  obj->mm.pages->sgl, iomap);

	if (area->vm_flags & VM_WRITE) {
		GEM_BUG_ON(!i915_gem_object_has_pinned_pages(obj));
		obj->mm.dirty = true;
	}

	i915_gem_object_unpin_pages(obj);

out:
	return i915_error_to_vmf_fault(err);
}

static vm_fault_t vm_fault_gtt(struct vm_fault *vmf)
{
#define MIN_CHUNK_PAGES (SZ_1M >> PAGE_SHIFT)
	struct vm_area_struct *area = vmf->vma;
	struct i915_mmap_offset *mmo = area->vm_private_data;
	struct drm_i915_gem_object *obj = mmo->obj;
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *i915 = to_i915(dev);
	struct intel_runtime_pm *rpm = &i915->runtime_pm;
	struct i915_ggtt *ggtt = &i915->ggtt;
	bool write = area->vm_flags & VM_WRITE;
	struct i915_gem_ww_ctx ww;
	intel_wakeref_t wakeref;
	struct i915_vma *vma;
	pgoff_t page_offset;
	int srcu;
	int ret;

	/* We don't use vmf->pgoff since that has the fake offset */
	page_offset = (vmf->address - area->vm_start) >> PAGE_SHIFT;

	trace_i915_gem_object_fault(obj, page_offset, true, write);

	wakeref = intel_runtime_pm_get(rpm);

	i915_gem_ww_ctx_init(&ww, true);
retry:
	ret = i915_gem_object_lock(obj, &ww);
	if (ret)
		goto err_rpm;

	/* Sanity check that we allow writing into this object */
	if (i915_gem_object_is_readonly(obj) && write) {
		ret = -EFAULT;
		goto err_rpm;
	}

	ret = i915_gem_object_pin_pages(obj);
	if (ret)
		goto err_rpm;

	ret = intel_gt_reset_trylock(ggtt->vm.gt, &srcu);
	if (ret)
		goto err_pages;

	/* Now pin it into the GTT as needed */
	vma = i915_gem_object_ggtt_pin_ww(obj, &ww, NULL, 0, 0,
					  PIN_MAPPABLE |
					  PIN_NONBLOCK /* NOWARN */ |
					  PIN_NOEVICT);
	if (IS_ERR(vma) && vma != ERR_PTR(-EDEADLK)) {
		/* Use a partial view if it is bigger than available space */
		struct i915_ggtt_view view =
			compute_partial_view(obj, page_offset, MIN_CHUNK_PAGES);
		unsigned int flags;

		flags = PIN_MAPPABLE | PIN_NOSEARCH;
		if (view.type == I915_GGTT_VIEW_NORMAL)
			flags |= PIN_NONBLOCK; /* avoid warnings for pinned */

		/*
		 * Userspace is now writing through an untracked VMA, abandon
		 * all hope that the hardware is able to track future writes.
		 */

		vma = i915_gem_object_ggtt_pin_ww(obj, &ww, &view, 0, 0, flags);
		if (IS_ERR(vma) && vma != ERR_PTR(-EDEADLK)) {
			flags = PIN_MAPPABLE;
			view.type = I915_GGTT_VIEW_PARTIAL;
			vma = i915_gem_object_ggtt_pin_ww(obj, &ww, &view, 0, 0, flags);
		}

		/* The entire mappable GGTT is pinned? Unexpected! */
		GEM_BUG_ON(vma == ERR_PTR(-ENOSPC));
	}
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto err_reset;
	}

	/* Access to snoopable pages through the GTT is incoherent. */
	if (obj->cache_level != I915_CACHE_NONE && !HAS_LLC(i915)) {
		ret = -EFAULT;
		goto err_unpin;
	}

	ret = i915_vma_pin_fence(vma);
	if (ret)
		goto err_unpin;

	/* Finally, remap it using the new GTT offset */
	ret = remap_io_mapping(area,
			       area->vm_start + (vma->ggtt_view.partial.offset << PAGE_SHIFT),
			       (ggtt->gmadr.start + vma->node.start) >> PAGE_SHIFT,
			       min_t(u64, vma->size, area->vm_end - area->vm_start),
			       &ggtt->iomap);
	if (ret)
		goto err_fence;

	assert_rpm_wakelock_held(rpm);

	/* Mark as being mmapped into userspace for later revocation */
	mutex_lock(&i915->ggtt.vm.mutex);
	if (!i915_vma_set_userfault(vma) && !obj->userfault_count++)
		list_add(&obj->userfault_link, &i915->ggtt.userfault_list);
	mutex_unlock(&i915->ggtt.vm.mutex);

	/* Track the mmo associated with the fenced vma */
	vma->mmo = mmo;

	if (IS_ACTIVE(CONFIG_DRM_I915_USERFAULT_AUTOSUSPEND))
		intel_wakeref_auto(&i915->ggtt.userfault_wakeref,
				   msecs_to_jiffies_timeout(CONFIG_DRM_I915_USERFAULT_AUTOSUSPEND));

	if (write) {
		GEM_BUG_ON(!i915_gem_object_has_pinned_pages(obj));
		i915_vma_set_ggtt_write(vma);
		obj->mm.dirty = true;
	}

err_fence:
	i915_vma_unpin_fence(vma);
err_unpin:
	__i915_vma_unpin(vma);
err_reset:
	intel_gt_reset_unlock(ggtt->vm.gt, srcu);
err_pages:
	i915_gem_object_unpin_pages(obj);
err_rpm:
	if (ret == -EDEADLK) {
		ret = i915_gem_ww_ctx_backoff(&ww);
		if (!ret)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);
	intel_runtime_pm_put(rpm, wakeref);
	return i915_error_to_vmf_fault(ret);
}

static int
vm_access(struct vm_area_struct *area, unsigned long addr,
	  void *buf, int len, int write)
{
	struct i915_mmap_offset *mmo = area->vm_private_data;
	struct drm_i915_gem_object *obj = mmo->obj;
	void *vaddr;

	if (i915_gem_object_is_readonly(obj) && write)
		return -EACCES;

	addr -= area->vm_start;
	if (addr >= obj->base.size)
		return -EINVAL;

	/* As this is primarily for debugging, let's focus on simplicity */
	vaddr = i915_gem_object_pin_map(obj, I915_MAP_FORCE_WC);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);

	if (write) {
		memcpy(vaddr + addr, buf, len);
		__i915_gem_object_flush_map(obj, addr, len);
	} else {
		memcpy(buf, vaddr + addr, len);
	}

	i915_gem_object_unpin_map(obj);

	return len;
}

void __i915_gem_object_release_mmap_gtt(struct drm_i915_gem_object *obj)
{
	struct i915_vma *vma;

	GEM_BUG_ON(!obj->userfault_count);

	for_each_ggtt_vma(vma, obj)
		i915_vma_revoke_mmap(vma);

	GEM_BUG_ON(obj->userfault_count);
}

/*
 * It is vital that we remove the page mapping if we have mapped a tiled
 * object through the GTT and then lose the fence register due to
 * resource pressure. Similarly if the object has been moved out of the
 * aperture, than pages mapped into userspace must be revoked. Removing the
 * mapping will then trigger a page fault on the next user access, allowing
 * fixup by vm_fault_gtt().
 */
void i915_gem_object_release_mmap_gtt(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	intel_wakeref_t wakeref;

	/*
	 * Serialisation between user GTT access and our code depends upon
	 * revoking the CPU's PTE whilst the mutex is held. The next user
	 * pagefault then has to wait until we release the mutex.
	 *
	 * Note that RPM complicates somewhat by adding an additional
	 * requirement that operations to the GGTT be made holding the RPM
	 * wakeref.
	 */
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);
	mutex_lock(&i915->ggtt.vm.mutex);

	if (!obj->userfault_count)
		goto out;

	__i915_gem_object_release_mmap_gtt(obj);

	/*
	 * Ensure that the CPU's PTE are revoked and there are not outstanding
	 * memory transactions from userspace before we return. The TLB
	 * flushing implied above by changing the PTE above *should* be
	 * sufficient, an extra barrier here just provides us with a bit
	 * of paranoid documentation about our requirement to serialise
	 * memory writes before touching registers / GSM.
	 */
	wmb();

out:
	mutex_unlock(&i915->ggtt.vm.mutex);
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
}

void i915_gem_object_release_mmap_offset(struct drm_i915_gem_object *obj)
{
	struct i915_mmap_offset *mmo, *mn;

	spin_lock(&obj->mmo.lock);
	rbtree_postorder_for_each_entry_safe(mmo, mn,
					     &obj->mmo.offsets, offset) {
		/*
		 * vma_node_unmap for GTT mmaps handled already in
		 * __i915_gem_object_release_mmap_gtt
		 */
		if (mmo->mmap_type == I915_MMAP_TYPE_GTT)
			continue;

		spin_unlock(&obj->mmo.lock);
		drm_vma_node_unmap(&mmo->vma_node,
				   obj->base.dev->anon_inode->i_mapping);
		spin_lock(&obj->mmo.lock);
	}
	spin_unlock(&obj->mmo.lock);
}

static struct i915_mmap_offset *
lookup_mmo(struct drm_i915_gem_object *obj,
	   enum i915_mmap_type mmap_type)
{
	struct rb_node *rb;

	spin_lock(&obj->mmo.lock);
	rb = obj->mmo.offsets.rb_node;
	while (rb) {
		struct i915_mmap_offset *mmo =
			rb_entry(rb, typeof(*mmo), offset);

		if (mmo->mmap_type == mmap_type) {
			spin_unlock(&obj->mmo.lock);
			return mmo;
		}

		if (mmo->mmap_type < mmap_type)
			rb = rb->rb_right;
		else
			rb = rb->rb_left;
	}
	spin_unlock(&obj->mmo.lock);

	return NULL;
}

static struct i915_mmap_offset *
insert_mmo(struct drm_i915_gem_object *obj, struct i915_mmap_offset *mmo)
{
	struct rb_node *rb, **p;

	spin_lock(&obj->mmo.lock);
	rb = NULL;
	p = &obj->mmo.offsets.rb_node;
	while (*p) {
		struct i915_mmap_offset *pos;

		rb = *p;
		pos = rb_entry(rb, typeof(*pos), offset);

		if (pos->mmap_type == mmo->mmap_type) {
			spin_unlock(&obj->mmo.lock);
			drm_vma_offset_remove(obj->base.dev->vma_offset_manager,
					      &mmo->vma_node);
			kfree(mmo);
			return pos;
		}

		if (pos->mmap_type < mmo->mmap_type)
			p = &rb->rb_right;
		else
			p = &rb->rb_left;
	}
	rb_link_node(&mmo->offset, rb, p);
	rb_insert_color(&mmo->offset, &obj->mmo.offsets);
	spin_unlock(&obj->mmo.lock);

	return mmo;
}

static struct i915_mmap_offset *
mmap_offset_attach(struct drm_i915_gem_object *obj,
		   enum i915_mmap_type mmap_type,
		   struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_mmap_offset *mmo;
	int err;

	mmo = lookup_mmo(obj, mmap_type);
	if (mmo)
		goto out;

	mmo = kmalloc(sizeof(*mmo), GFP_KERNEL);
	if (!mmo)
		return ERR_PTR(-ENOMEM);

	mmo->obj = obj;
	mmo->mmap_type = mmap_type;
	drm_vma_node_reset(&mmo->vma_node);

	err = drm_vma_offset_add(obj->base.dev->vma_offset_manager,
				 &mmo->vma_node, obj->base.size / PAGE_SIZE);
	if (likely(!err))
		goto insert;

	/* Attempt to reap some mmap space from dead objects */
	err = intel_gt_retire_requests_timeout(&i915->gt, MAX_SCHEDULE_TIMEOUT);
	if (err)
		goto err;

	i915_gem_drain_freed_objects(i915);
	err = drm_vma_offset_add(obj->base.dev->vma_offset_manager,
				 &mmo->vma_node, obj->base.size / PAGE_SIZE);
	if (err)
		goto err;

insert:
	mmo = insert_mmo(obj, mmo);
	GEM_BUG_ON(lookup_mmo(obj, mmap_type) != mmo);
out:
	if (file)
		drm_vma_node_allow(&mmo->vma_node, file);
	return mmo;

err:
	kfree(mmo);
	return ERR_PTR(err);
}

static int
__assign_mmap_offset(struct drm_file *file,
		     u32 handle,
		     enum i915_mmap_type mmap_type,
		     u64 *offset)
{
	struct drm_i915_gem_object *obj;
	struct i915_mmap_offset *mmo;
	int err;

	obj = i915_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;

	if (i915_gem_object_never_mmap(obj)) {
		err = -ENODEV;
		goto out;
	}

	if (mmap_type != I915_MMAP_TYPE_GTT &&
	    !i915_gem_object_type_has(obj,
				      I915_GEM_OBJECT_HAS_STRUCT_PAGE |
				      I915_GEM_OBJECT_HAS_IOMEM)) {
		err = -ENODEV;
		goto out;
	}

	mmo = mmap_offset_attach(obj, mmap_type, file);
	if (IS_ERR(mmo)) {
		err = PTR_ERR(mmo);
		goto out;
	}

	*offset = drm_vma_node_offset_addr(&mmo->vma_node);
	err = 0;
out:
	i915_gem_object_put(obj);
	return err;
}

int
i915_gem_dumb_mmap_offset(struct drm_file *file,
			  struct drm_device *dev,
			  u32 handle,
			  u64 *offset)
{
	enum i915_mmap_type mmap_type;

	if (boot_cpu_has(X86_FEATURE_PAT))
		mmap_type = I915_MMAP_TYPE_WC;
	else if (!i915_ggtt_has_aperture(&to_i915(dev)->ggtt))
		return -ENODEV;
	else
		mmap_type = I915_MMAP_TYPE_GTT;

	return __assign_mmap_offset(file, handle, mmap_type, offset);
}

/**
 * i915_gem_mmap_offset_ioctl - prepare an object for GTT mmap'ing
 * @dev: DRM device
 * @data: GTT mapping ioctl data
 * @file: GEM object info
 *
 * Simply returns the fake offset to userspace so it can mmap it.
 * The mmap call will end up in drm_gem_mmap(), which will set things
 * up so we can get faults in the handler above.
 *
 * The fault handler will take care of binding the object into the GTT
 * (since it may have been evicted to make room for something), allocating
 * a fence register, and mapping the appropriate aperture address into
 * userspace.
 */
int
i915_gem_mmap_offset_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct drm_i915_gem_mmap_offset *args = data;
	enum i915_mmap_type type;
	int err;

	/*
	 * Historically we failed to check args.pad and args.offset
	 * and so we cannot use those fields for user input and we cannot
	 * add -EINVAL for them as the ABI is fixed, i.e. old userspace
	 * may be feeding in garbage in those fields.
	 *
	 * if (args->pad) return -EINVAL; is verbotten!
	 */

	err = i915_user_extensions(u64_to_user_ptr(args->extensions),
				   NULL, 0, NULL);
	if (err)
		return err;

	switch (args->flags) {
	case I915_MMAP_OFFSET_GTT:
		if (!i915_ggtt_has_aperture(&i915->ggtt))
			return -ENODEV;
		type = I915_MMAP_TYPE_GTT;
		break;

	case I915_MMAP_OFFSET_WC:
		if (!boot_cpu_has(X86_FEATURE_PAT))
			return -ENODEV;
		type = I915_MMAP_TYPE_WC;
		break;

	case I915_MMAP_OFFSET_WB:
		type = I915_MMAP_TYPE_WB;
		break;

	case I915_MMAP_OFFSET_UC:
		if (!boot_cpu_has(X86_FEATURE_PAT))
			return -ENODEV;
		type = I915_MMAP_TYPE_UC;
		break;

	default:
		return -EINVAL;
	}

	return __assign_mmap_offset(file, args->handle, type, &args->offset);
}

static void vm_open(struct vm_area_struct *vma)
{
	struct i915_mmap_offset *mmo = vma->vm_private_data;
	struct drm_i915_gem_object *obj = mmo->obj;

	GEM_BUG_ON(!obj);
	i915_gem_object_get(obj);
}

static void vm_close(struct vm_area_struct *vma)
{
	struct i915_mmap_offset *mmo = vma->vm_private_data;
	struct drm_i915_gem_object *obj = mmo->obj;

	GEM_BUG_ON(!obj);
	i915_gem_object_put(obj);
}

static const struct vm_operations_struct vm_ops_gtt = {
	.fault = vm_fault_gtt,
	.access = vm_access,
	.open = vm_open,
	.close = vm_close,
};

static const struct vm_operations_struct vm_ops_cpu = {
	.fault = vm_fault_cpu,
	.access = vm_access,
	.open = vm_open,
	.close = vm_close,
};

static int singleton_release(struct inode *inode, struct file *file)
{
	struct drm_i915_private *i915 = file->private_data;

	cmpxchg(&i915->gem.mmap_singleton, file, NULL);
	drm_dev_put(&i915->drm);

	return 0;
}

static const struct file_operations singleton_fops = {
	.owner = THIS_MODULE,
	.release = singleton_release,
};

static struct file *mmap_singleton(struct drm_i915_private *i915)
{
	struct file *file;

	rcu_read_lock();
	file = READ_ONCE(i915->gem.mmap_singleton);
	if (file && !get_file_rcu(file))
		file = NULL;
	rcu_read_unlock();
	if (file)
		return file;

	file = anon_inode_getfile("i915.gem", &singleton_fops, i915, O_RDWR);
	if (IS_ERR(file))
		return file;

	/* Everyone shares a single global address space */
	file->f_mapping = i915->drm.anon_inode->i_mapping;

	smp_store_mb(i915->gem.mmap_singleton, file);
	drm_dev_get(&i915->drm);

	return file;
}

/*
 * This overcomes the limitation in drm_gem_mmap's assignment of a
 * drm_gem_object as the vma->vm_private_data. Since we need to
 * be able to resolve multiple mmap offsets which could be tied
 * to a single gem object.
 */
int i915_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_vma_offset_node *node;
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev = priv->minor->dev;
	struct drm_i915_gem_object *obj = NULL;
	struct i915_mmap_offset *mmo = NULL;
	struct file *anon;

	if (drm_dev_is_unplugged(dev))
		return -ENODEV;

	rcu_read_lock();
	drm_vma_offset_lock_lookup(dev->vma_offset_manager);
	node = drm_vma_offset_exact_lookup_locked(dev->vma_offset_manager,
						  vma->vm_pgoff,
						  vma_pages(vma));
	if (node && drm_vma_node_is_allowed(node, priv)) {
		/*
		 * Skip 0-refcnted objects as it is in the process of being
		 * destroyed and will be invalid when the vma manager lock
		 * is released.
		 */
		mmo = container_of(node, struct i915_mmap_offset, vma_node);
		obj = i915_gem_object_get_rcu(mmo->obj);
	}
	drm_vma_offset_unlock_lookup(dev->vma_offset_manager);
	rcu_read_unlock();
	if (!obj)
		return node ? -EACCES : -EINVAL;

	if (i915_gem_object_is_readonly(obj)) {
		if (vma->vm_flags & VM_WRITE) {
			i915_gem_object_put(obj);
			return -EINVAL;
		}
		vma->vm_flags &= ~VM_MAYWRITE;
	}

	anon = mmap_singleton(to_i915(dev));
	if (IS_ERR(anon)) {
		i915_gem_object_put(obj);
		return PTR_ERR(anon);
	}

	vma->vm_flags |= VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_private_data = mmo;

	/*
	 * We keep the ref on mmo->obj, not vm_file, but we require
	 * vma->vm_file->f_mapping, see vma_link(), for later revocation.
	 * Our userspace is accustomed to having per-file resource cleanup
	 * (i.e. contexts, objects and requests) on their close(fd), which
	 * requires avoiding extraneous references to their filp, hence why
	 * we prefer to use an anonymous file for their mmaps.
	 */
	fput(vma->vm_file);
	vma->vm_file = anon;

	switch (mmo->mmap_type) {
	case I915_MMAP_TYPE_WC:
		vma->vm_page_prot =
			pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
		vma->vm_ops = &vm_ops_cpu;
		break;

	case I915_MMAP_TYPE_WB:
		vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
		vma->vm_ops = &vm_ops_cpu;
		break;

	case I915_MMAP_TYPE_UC:
		vma->vm_page_prot =
			pgprot_noncached(vm_get_page_prot(vma->vm_flags));
		vma->vm_ops = &vm_ops_cpu;
		break;

	case I915_MMAP_TYPE_GTT:
		vma->vm_page_prot =
			pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
		vma->vm_ops = &vm_ops_gtt;
		break;
	}
	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);

	return 0;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/i915_gem_mman.c"
#endif
