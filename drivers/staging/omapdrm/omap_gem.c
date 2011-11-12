/*
 * drivers/staging/omapdrm/omap_gem.c
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob.clark@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <linux/spinlock.h>
#include <linux/shmem_fs.h>

#include "omap_drv.h"

/* remove these once drm core helpers are merged */
struct page ** _drm_gem_get_pages(struct drm_gem_object *obj, gfp_t gfpmask);
void _drm_gem_put_pages(struct drm_gem_object *obj, struct page **pages,
		bool dirty, bool accessed);

/*
 * GEM buffer object implementation.
 */

#define to_omap_bo(x) container_of(x, struct omap_gem_object, base)

/* note: we use upper 8 bits of flags for driver-internal flags: */
#define OMAP_BO_DMA			0x01000000	/* actually is physically contiguous */
#define OMAP_BO_EXT_SYNC	0x02000000	/* externally allocated sync object */
#define OMAP_BO_EXT_MEM		0x04000000	/* externally allocated memory */


struct omap_gem_object {
	struct drm_gem_object base;

	uint32_t flags;

	/**
	 * If buffer is allocated physically contiguous, the OMAP_BO_DMA flag
	 * is set and the paddr is valid.
	 *
	 * Note that OMAP_BO_SCANOUT is a hint from userspace that DMA capable
	 * buffer is requested, but doesn't mean that it is.  Use the
	 * OMAP_BO_DMA flag to determine if the buffer has a DMA capable
	 * physical address.
	 */
	dma_addr_t paddr;

	/**
	 * Array of backing pages, if allocated.  Note that pages are never
	 * allocated for buffers originally allocated from contiguous memory
	 */
	struct page **pages;

	/**
	 * Virtual address, if mapped.
	 */
	void *vaddr;

	/**
	 * sync-object allocated on demand (if needed)
	 *
	 * Per-buffer sync-object for tracking pending and completed hw/dma
	 * read and write operations.  The layout in memory is dictated by
	 * the SGX firmware, which uses this information to stall the command
	 * stream if a surface is not ready yet.
	 *
	 * Note that when buffer is used by SGX, the sync-object needs to be
	 * allocated from a special heap of sync-objects.  This way many sync
	 * objects can be packed in a page, and not waste GPU virtual address
	 * space.  Because of this we have to have a omap_gem_set_sync_object()
	 * API to allow replacement of the syncobj after it has (potentially)
	 * already been allocated.  A bit ugly but I haven't thought of a
	 * better alternative.
	 */
	struct {
		uint32_t write_pending;
		uint32_t write_complete;
		uint32_t read_pending;
		uint32_t read_complete;
	} *sync;
};

/* GEM objects can either be allocated from contiguous memory (in which
 * case obj->filp==NULL), or w/ shmem backing (obj->filp!=NULL).  But non
 * contiguous buffers can be remapped in TILER/DMM if they need to be
 * contiguous... but we don't do this all the time to reduce pressure
 * on TILER/DMM space when we know at allocation time that the buffer
 * will need to be scanned out.
 */
static inline bool is_shmem(struct drm_gem_object *obj)
{
	return obj->filp != NULL;
}

static int get_pages(struct drm_gem_object *obj, struct page ***pages);

static DEFINE_SPINLOCK(sync_lock);

/** ensure backing pages are allocated */
static int omap_gem_attach_pages(struct drm_gem_object *obj)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	struct page **pages;

	WARN_ON(omap_obj->pages);

	/* TODO: __GFP_DMA32 .. but somehow GFP_HIGHMEM is coming from the
	 * mapping_gfp_mask(mapping) which conflicts w/ GFP_DMA32.. probably
	 * we actually want CMA memory for it all anyways..
	 */
	pages = _drm_gem_get_pages(obj, GFP_KERNEL);
	if (IS_ERR(pages)) {
		dev_err(obj->dev->dev, "could not get pages: %ld\n", PTR_ERR(pages));
		return PTR_ERR(pages);
	}

	omap_obj->pages = pages;
	return 0;
}

/** release backing pages */
static void omap_gem_detach_pages(struct drm_gem_object *obj)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	_drm_gem_put_pages(obj, omap_obj->pages, true, false);
	omap_obj->pages = NULL;
}

/** get mmap offset */
uint64_t omap_gem_mmap_offset(struct drm_gem_object *obj)
{
	if (!obj->map_list.map) {
		/* Make it mmapable */
		int ret = drm_gem_create_mmap_offset(obj);
		if (ret) {
			dev_err(obj->dev->dev, "could not allocate mmap offset");
			return 0;
		}
	}

	return (uint64_t)obj->map_list.hash.key << PAGE_SHIFT;
}

/**
 * omap_gem_fault		-	pagefault handler for GEM objects
 * @vma: the VMA of the GEM object
 * @vmf: fault detail
 *
 * Invoked when a fault occurs on an mmap of a GEM managed area. GEM
 * does most of the work for us including the actual map/unmap calls
 * but we need to do the actual page work.
 *
 * The VMA was set up by GEM. In doing so it also ensured that the
 * vma->vm_private_data points to the GEM object that is backing this
 * mapping.
 */
int omap_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	struct drm_device *dev = obj->dev;
	struct page **pages;
	unsigned long pfn;
	pgoff_t pgoff;
	int ret;

	/* Make sure we don't parallel update on a fault, nor move or remove
	 * something from beneath our feet
	 */
	mutex_lock(&dev->struct_mutex);

	/* if a shmem backed object, make sure we have pages attached now */
	ret = get_pages(obj, &pages);
	if (ret) {
		goto fail;
	}

	/* where should we do corresponding put_pages().. we are mapping
	 * the original page, rather than thru a GART, so we can't rely
	 * on eviction to trigger this.  But munmap() or all mappings should
	 * probably trigger put_pages()?
	 */

	/* We don't use vmf->pgoff since that has the fake offset: */
	pgoff = ((unsigned long)vmf->virtual_address -
			vma->vm_start) >> PAGE_SHIFT;

	if (omap_obj->pages) {
		pfn = page_to_pfn(omap_obj->pages[pgoff]);
	} else {
		BUG_ON(!(omap_obj->flags & OMAP_BO_DMA));
		pfn = (omap_obj->paddr >> PAGE_SHIFT) + pgoff;
	}

	VERB("Inserting %p pfn %lx, pa %lx", vmf->virtual_address,
			pfn, pfn << PAGE_SHIFT);

	ret = vm_insert_mixed(vma, (unsigned long)vmf->virtual_address, pfn);

fail:
	mutex_unlock(&dev->struct_mutex);
	switch (ret) {
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	default:
		return VM_FAULT_SIGBUS;
	}
}

/** We override mainly to fix up some of the vm mapping flags.. */
int omap_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct omap_gem_object *omap_obj;
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret) {
		DBG("mmap failed: %d", ret);
		return ret;
	}

	/* after drm_gem_mmap(), it is safe to access the obj */
	omap_obj = to_omap_bo(vma->vm_private_data);

	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_flags |= VM_MIXEDMAP;

	if (omap_obj->flags & OMAP_BO_WC) {
		vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
	} else if (omap_obj->flags & OMAP_BO_UNCACHED) {
		vma->vm_page_prot = pgprot_noncached(vm_get_page_prot(vma->vm_flags));
	} else {
		vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	}

	return ret;
}

/**
 * omap_gem_dumb_create	-	create a dumb buffer
 * @drm_file: our client file
 * @dev: our device
 * @args: the requested arguments copied from userspace
 *
 * Allocate a buffer suitable for use for a frame buffer of the
 * form described by user space. Give userspace a handle by which
 * to reference it.
 */
int omap_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
		struct drm_mode_create_dumb *args)
{
	union omap_gem_size gsize;

	/* in case someone tries to feed us a completely bogus stride: */
	args->pitch = align_pitch(args->pitch, args->width, args->bpp);
	args->size = PAGE_ALIGN(args->pitch * args->height);

	gsize = (union omap_gem_size){
		.bytes = args->size,
	};

	return omap_gem_new_handle(dev, file, gsize,
			OMAP_BO_SCANOUT | OMAP_BO_WC, &args->handle);
}

/**
 * omap_gem_dumb_destroy	-	destroy a dumb buffer
 * @file: client file
 * @dev: our DRM device
 * @handle: the object handle
 *
 * Destroy a handle that was created via omap_gem_dumb_create.
 */
int omap_gem_dumb_destroy(struct drm_file *file, struct drm_device *dev,
		uint32_t handle)
{
	/* No special work needed, drop the reference and see what falls out */
	return drm_gem_handle_delete(file, handle);
}

/**
 * omap_gem_dumb_map	-	buffer mapping for dumb interface
 * @file: our drm client file
 * @dev: drm device
 * @handle: GEM handle to the object (from dumb_create)
 *
 * Do the necessary setup to allow the mapping of the frame buffer
 * into user memory. We don't have to do much here at the moment.
 */
int omap_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
		uint32_t handle, uint64_t *offset)
{
	struct drm_gem_object *obj;
	int ret = 0;

	mutex_lock(&dev->struct_mutex);

	/* GEM does all our handle to object mapping */
	obj = drm_gem_object_lookup(dev, file, handle);
	if (obj == NULL) {
		ret = -ENOENT;
		goto fail;
	}

	*offset = omap_gem_mmap_offset(obj);

	drm_gem_object_unreference_unlocked(obj);

fail:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/* Get physical address for DMA.. if 'remap' is true, and the buffer is not
 * already contiguous, remap it to pin in physically contiguous memory.. (ie.
 * map in TILER)
 */
int omap_gem_get_paddr(struct drm_gem_object *obj,
		dma_addr_t *paddr, bool remap)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	int ret = 0;

	if (is_shmem(obj)) {
		/* TODO: remap to TILER */
		return -ENOMEM;
	}

	*paddr = omap_obj->paddr;

	return ret;
}

/* Release physical address, when DMA is no longer being performed.. this
 * could potentially unpin and unmap buffers from TILER
 */
int omap_gem_put_paddr(struct drm_gem_object *obj)
{
	/* do something here when remap to TILER is used.. */
	return 0;
}

/* acquire pages when needed (for example, for DMA where physically
 * contiguous buffer is not required
 */
static int get_pages(struct drm_gem_object *obj, struct page ***pages)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	int ret = 0;

	if (is_shmem(obj) && !omap_obj->pages) {
		ret = omap_gem_attach_pages(obj);
		if (ret) {
			dev_err(obj->dev->dev, "could not attach pages\n");
			return ret;
		}
	}

	/* TODO: even phys-contig.. we should have a list of pages? */
	*pages = omap_obj->pages;

	return 0;
}

int omap_gem_get_pages(struct drm_gem_object *obj, struct page ***pages)
{
	int ret;
	mutex_lock(&obj->dev->struct_mutex);
	ret = get_pages(obj, pages);
	mutex_unlock(&obj->dev->struct_mutex);
	return ret;
}

/* release pages when DMA no longer being performed */
int omap_gem_put_pages(struct drm_gem_object *obj)
{
	/* do something here if we dynamically attach/detach pages.. at
	 * least they would no longer need to be pinned if everyone has
	 * released the pages..
	 */
	return 0;
}

/* Get kernel virtual address for CPU access.. only buffers that are
 * allocated contiguously have a kernel virtual address, so this more
 * or less only exists for omap_fbdev
 */
void *omap_gem_vaddr(struct drm_gem_object *obj)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	return omap_obj->vaddr;
}

/* Buffer Synchronization:
 */

struct omap_gem_sync_waiter {
	struct list_head list;
	struct omap_gem_object *omap_obj;
	enum omap_gem_op op;
	uint32_t read_target, write_target;
	/* notify called w/ sync_lock held */
	void (*notify)(void *arg);
	void *arg;
};

/* list of omap_gem_sync_waiter.. the notify fxn gets called back when
 * the read and/or write target count is achieved which can call a user
 * callback (ex. to kick 3d and/or 2d), wakeup blocked task (prep for
 * cpu access), etc.
 */
static LIST_HEAD(waiters);

static inline bool is_waiting(struct omap_gem_sync_waiter *waiter)
{
	struct omap_gem_object *omap_obj = waiter->omap_obj;
	if ((waiter->op & OMAP_GEM_READ) &&
			(omap_obj->sync->read_complete < waiter->read_target))
		return true;
	if ((waiter->op & OMAP_GEM_WRITE) &&
			(omap_obj->sync->write_complete < waiter->write_target))
		return true;
	return false;
}

/* macro for sync debug.. */
#define SYNCDBG 0
#define SYNC(fmt, ...) do { if (SYNCDBG) \
		printk(KERN_ERR "%s:%d: "fmt"\n", \
				__func__, __LINE__, ##__VA_ARGS__); \
	} while (0)


static void sync_op_update(void)
{
	struct omap_gem_sync_waiter *waiter, *n;
	list_for_each_entry_safe(waiter, n, &waiters, list) {
		if (!is_waiting(waiter)) {
			list_del(&waiter->list);
			SYNC("notify: %p", waiter);
			waiter->notify(waiter->arg);
			kfree(waiter);
		}
	}
}

static inline int sync_op(struct drm_gem_object *obj,
		enum omap_gem_op op, bool start)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	int ret = 0;

	spin_lock(&sync_lock);

	if (!omap_obj->sync) {
		omap_obj->sync = kzalloc(sizeof(*omap_obj->sync), GFP_ATOMIC);
		if (!omap_obj->sync) {
			ret = -ENOMEM;
			goto unlock;
		}
	}

	if (start) {
		if (op & OMAP_GEM_READ)
			omap_obj->sync->read_pending++;
		if (op & OMAP_GEM_WRITE)
			omap_obj->sync->write_pending++;
	} else {
		if (op & OMAP_GEM_READ)
			omap_obj->sync->read_complete++;
		if (op & OMAP_GEM_WRITE)
			omap_obj->sync->write_complete++;
		sync_op_update();
	}

unlock:
	spin_unlock(&sync_lock);

	return ret;
}

/* it is a bit lame to handle updates in this sort of polling way, but
 * in case of PVR, the GPU can directly update read/write complete
 * values, and not really tell us which ones it updated.. this also
 * means that sync_lock is not quite sufficient.  So we'll need to
 * do something a bit better when it comes time to add support for
 * separate 2d hw..
 */
void omap_gem_op_update(void)
{
	spin_lock(&sync_lock);
	sync_op_update();
	spin_unlock(&sync_lock);
}

/* mark the start of read and/or write operation */
int omap_gem_op_start(struct drm_gem_object *obj, enum omap_gem_op op)
{
	return sync_op(obj, op, true);
}

int omap_gem_op_finish(struct drm_gem_object *obj, enum omap_gem_op op)
{
	return sync_op(obj, op, false);
}

static DECLARE_WAIT_QUEUE_HEAD(sync_event);

static void sync_notify(void *arg)
{
	struct task_struct **waiter_task = arg;
	*waiter_task = NULL;
	wake_up_all(&sync_event);
}

int omap_gem_op_sync(struct drm_gem_object *obj, enum omap_gem_op op)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	int ret = 0;
	if (omap_obj->sync) {
		struct task_struct *waiter_task = current;
		struct omap_gem_sync_waiter *waiter =
				kzalloc(sizeof(*waiter), GFP_KERNEL);

		if (!waiter) {
			return -ENOMEM;
		}

		waiter->omap_obj = omap_obj;
		waiter->op = op;
		waiter->read_target = omap_obj->sync->read_pending;
		waiter->write_target = omap_obj->sync->write_pending;
		waiter->notify = sync_notify;
		waiter->arg = &waiter_task;

		spin_lock(&sync_lock);
		if (is_waiting(waiter)) {
			SYNC("waited: %p", waiter);
			list_add_tail(&waiter->list, &waiters);
			spin_unlock(&sync_lock);
			ret = wait_event_interruptible(sync_event,
					(waiter_task == NULL));
			spin_lock(&sync_lock);
			if (waiter_task) {
				SYNC("interrupted: %p", waiter);
				/* we were interrupted */
				list_del(&waiter->list);
				waiter_task = NULL;
			} else {
				/* freed in sync_op_update() */
				waiter = NULL;
			}
		}
		spin_unlock(&sync_lock);

		if (waiter) {
			kfree(waiter);
		}
	}
	return ret;
}

/* call fxn(arg), either synchronously or asynchronously if the op
 * is currently blocked..  fxn() can be called from any context
 *
 * (TODO for now fxn is called back from whichever context calls
 * omap_gem_op_update().. but this could be better defined later
 * if needed)
 *
 * TODO more code in common w/ _sync()..
 */
int omap_gem_op_async(struct drm_gem_object *obj, enum omap_gem_op op,
		void (*fxn)(void *arg), void *arg)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	if (omap_obj->sync) {
		struct omap_gem_sync_waiter *waiter =
				kzalloc(sizeof(*waiter), GFP_ATOMIC);

		if (!waiter) {
			return -ENOMEM;
		}

		waiter->omap_obj = omap_obj;
		waiter->op = op;
		waiter->read_target = omap_obj->sync->read_pending;
		waiter->write_target = omap_obj->sync->write_pending;
		waiter->notify = fxn;
		waiter->arg = arg;

		spin_lock(&sync_lock);
		if (is_waiting(waiter)) {
			SYNC("waited: %p", waiter);
			list_add_tail(&waiter->list, &waiters);
			spin_unlock(&sync_lock);
			return 0;
		}

		spin_unlock(&sync_lock);
	}

	/* no waiting.. */
	fxn(arg);

	return 0;
}

/* special API so PVR can update the buffer to use a sync-object allocated
 * from it's sync-obj heap.  Only used for a newly allocated (from PVR's
 * perspective) sync-object, so we overwrite the new syncobj w/ values
 * from the already allocated syncobj (if there is one)
 */
int omap_gem_set_sync_object(struct drm_gem_object *obj, void *syncobj)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	int ret = 0;

	spin_lock(&sync_lock);

	if ((omap_obj->flags & OMAP_BO_EXT_SYNC) && !syncobj) {
		/* clearing a previously set syncobj */
		syncobj = kzalloc(sizeof(*omap_obj->sync), GFP_ATOMIC);
		if (!syncobj) {
			ret = -ENOMEM;
			goto unlock;
		}
		memcpy(syncobj, omap_obj->sync, sizeof(*omap_obj->sync));
		omap_obj->flags &= ~OMAP_BO_EXT_SYNC;
		omap_obj->sync = syncobj;
	} else if (syncobj && !(omap_obj->flags & OMAP_BO_EXT_SYNC)) {
		/* replacing an existing syncobj */
		if (omap_obj->sync) {
			memcpy(syncobj, omap_obj->sync, sizeof(*omap_obj->sync));
			kfree(omap_obj->sync);
		}
		omap_obj->flags |= OMAP_BO_EXT_SYNC;
		omap_obj->sync = syncobj;
	}

unlock:
	spin_unlock(&sync_lock);
	return ret;
}

int omap_gem_init_object(struct drm_gem_object *obj)
{
	return -EINVAL;          /* unused */
}

/* don't call directly.. called from GEM core when it is time to actually
 * free the object..
 */
void omap_gem_free_object(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct omap_gem_object *omap_obj = to_omap_bo(obj);

	if (obj->map_list.map) {
		drm_gem_free_mmap_offset(obj);
	}

	/* don't free externally allocated backing memory */
	if (!(omap_obj->flags & OMAP_BO_EXT_MEM)) {
		if (omap_obj->pages) {
			omap_gem_detach_pages(obj);
		}
		if (!is_shmem(obj)) {
			dma_free_writecombine(dev->dev, obj->size,
					omap_obj->vaddr, omap_obj->paddr);
		}
	}

	/* don't free externally allocated syncobj */
	if (!(omap_obj->flags & OMAP_BO_EXT_SYNC)) {
		kfree(omap_obj->sync);
	}

	drm_gem_object_release(obj);

	kfree(obj);
}

/* convenience method to construct a GEM buffer object, and userspace handle */
int omap_gem_new_handle(struct drm_device *dev, struct drm_file *file,
		union omap_gem_size gsize, uint32_t flags, uint32_t *handle)
{
	struct drm_gem_object *obj;
	int ret;

	obj = omap_gem_new(dev, gsize, flags);
	if (!obj)
		return -ENOMEM;

	ret = drm_gem_handle_create(file, obj, handle);
	if (ret) {
		drm_gem_object_release(obj);
		kfree(obj); /* TODO isn't there a dtor to call? just copying i915 */
		return ret;
	}

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(obj);

	return 0;
}

/* GEM buffer object constructor */
struct drm_gem_object *omap_gem_new(struct drm_device *dev,
		union omap_gem_size gsize, uint32_t flags)
{
	struct omap_gem_object *omap_obj;
	struct drm_gem_object *obj = NULL;
	size_t size;
	int ret;

	if (flags & OMAP_BO_TILED) {
		/* TODO: not implemented yet */
		goto fail;
	}

	size = PAGE_ALIGN(gsize.bytes);

	omap_obj = kzalloc(sizeof(*omap_obj), GFP_KERNEL);
	if (!omap_obj) {
		dev_err(dev->dev, "could not allocate GEM object\n");
		goto fail;
	}

	obj = &omap_obj->base;

	if (flags & OMAP_BO_SCANOUT) {
		/* attempt to allocate contiguous memory */
		omap_obj->vaddr =  dma_alloc_writecombine(dev->dev, size,
				&omap_obj->paddr, GFP_KERNEL);
		if (omap_obj->vaddr) {
			flags |= OMAP_BO_DMA;
		}
	}

	omap_obj->flags = flags;

	if (flags & (OMAP_BO_DMA|OMAP_BO_EXT_MEM)) {
		ret = drm_gem_private_object_init(dev, obj, size);
	} else {
		ret = drm_gem_object_init(dev, obj, size);
	}

	if (ret) {
		goto fail;
	}

	return obj;

fail:
	if (obj) {
		omap_gem_free_object(obj);
	}
	return NULL;
}
