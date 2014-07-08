/*
 * drivers/gpu/drm/omapdrm/omap_gem.c
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
#include <drm/drm_vma_manager.h>

#include "omap_drv.h"
#include "omap_dmm_tiler.h"

/* remove these once drm core helpers are merged */
struct page **_drm_gem_get_pages(struct drm_gem_object *obj, gfp_t gfpmask);
void _drm_gem_put_pages(struct drm_gem_object *obj, struct page **pages,
		bool dirty, bool accessed);
int _drm_gem_create_mmap_offset_size(struct drm_gem_object *obj, size_t size);

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

	struct list_head mm_list;

	uint32_t flags;

	/** width/height for tiled formats (rounded up to slot boundaries) */
	uint16_t width, height;

	/** roll applied when mapping to DMM */
	uint32_t roll;

	/**
	 * If buffer is allocated physically contiguous, the OMAP_BO_DMA flag
	 * is set and the paddr is valid.  Also if the buffer is remapped in
	 * TILER and paddr_cnt > 0, then paddr is valid.  But if you are using
	 * the physical address and OMAP_BO_DMA is not set, then you should
	 * be going thru omap_gem_{get,put}_paddr() to ensure the mapping is
	 * not removed from under your feet.
	 *
	 * Note that OMAP_BO_SCANOUT is a hint from userspace that DMA capable
	 * buffer is requested, but doesn't mean that it is.  Use the
	 * OMAP_BO_DMA flag to determine if the buffer has a DMA capable
	 * physical address.
	 */
	dma_addr_t paddr;

	/**
	 * # of users of paddr
	 */
	uint32_t paddr_cnt;

	/**
	 * tiler block used when buffer is remapped in DMM/TILER.
	 */
	struct tiler_block *block;

	/**
	 * Array of backing pages, if allocated.  Note that pages are never
	 * allocated for buffers originally allocated from contiguous memory
	 */
	struct page **pages;

	/** addresses corresponding to pages in above array */
	dma_addr_t *addrs;

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

static int get_pages(struct drm_gem_object *obj, struct page ***pages);
static uint64_t mmap_offset(struct drm_gem_object *obj);

/* To deal with userspace mmap'ings of 2d tiled buffers, which (a) are
 * not necessarily pinned in TILER all the time, and (b) when they are
 * they are not necessarily page aligned, we reserve one or more small
 * regions in each of the 2d containers to use as a user-GART where we
 * can create a second page-aligned mapping of parts of the buffer
 * being accessed from userspace.
 *
 * Note that we could optimize slightly when we know that multiple
 * tiler containers are backed by the same PAT.. but I'll leave that
 * for later..
 */
#define NUM_USERGART_ENTRIES 2
struct usergart_entry {
	struct tiler_block *block;	/* the reserved tiler block */
	dma_addr_t paddr;
	struct drm_gem_object *obj;	/* the current pinned obj */
	pgoff_t obj_pgoff;		/* page offset of obj currently
					   mapped in */
};
static struct {
	struct usergart_entry entry[NUM_USERGART_ENTRIES];
	int height;				/* height in rows */
	int height_shift;		/* ilog2(height in rows) */
	int slot_shift;			/* ilog2(width per slot) */
	int stride_pfn;			/* stride in pages */
	int last;				/* index of last used entry */
} *usergart;

static void evict_entry(struct drm_gem_object *obj,
		enum tiler_fmt fmt, struct usergart_entry *entry)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	int n = usergart[fmt].height;
	size_t size = PAGE_SIZE * n;
	loff_t off = mmap_offset(obj) +
			(entry->obj_pgoff << PAGE_SHIFT);
	const int m = 1 + ((omap_obj->width << fmt) / PAGE_SIZE);

	if (m > 1) {
		int i;
		/* if stride > than PAGE_SIZE then sparse mapping: */
		for (i = n; i > 0; i--) {
			unmap_mapping_range(obj->dev->anon_inode->i_mapping,
					    off, PAGE_SIZE, 1);
			off += PAGE_SIZE * m;
		}
	} else {
		unmap_mapping_range(obj->dev->anon_inode->i_mapping,
				    off, size, 1);
	}

	entry->obj = NULL;
}

/* Evict a buffer from usergart, if it is mapped there */
static void evict(struct drm_gem_object *obj)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);

	if (omap_obj->flags & OMAP_BO_TILED) {
		enum tiler_fmt fmt = gem2fmt(omap_obj->flags);
		int i;

		if (!usergart)
			return;

		for (i = 0; i < NUM_USERGART_ENTRIES; i++) {
			struct usergart_entry *entry = &usergart[fmt].entry[i];
			if (entry->obj == obj)
				evict_entry(obj, fmt, entry);
		}
	}
}

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

/**
 * shmem buffers that are mapped cached can simulate coherency via using
 * page faulting to keep track of dirty pages
 */
static inline bool is_cached_coherent(struct drm_gem_object *obj)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	return is_shmem(obj) &&
		((omap_obj->flags & OMAP_BO_CACHE_MASK) == OMAP_BO_CACHED);
}

static DEFINE_SPINLOCK(sync_lock);

/** ensure backing pages are allocated */
static int omap_gem_attach_pages(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	struct page **pages;
	int npages = obj->size >> PAGE_SHIFT;
	int i, ret;
	dma_addr_t *addrs;

	WARN_ON(omap_obj->pages);

	pages = drm_gem_get_pages(obj);
	if (IS_ERR(pages)) {
		dev_err(obj->dev->dev, "could not get pages: %ld\n", PTR_ERR(pages));
		return PTR_ERR(pages);
	}

	/* for non-cached buffers, ensure the new pages are clean because
	 * DSS, GPU, etc. are not cache coherent:
	 */
	if (omap_obj->flags & (OMAP_BO_WC|OMAP_BO_UNCACHED)) {
		addrs = kmalloc(npages * sizeof(*addrs), GFP_KERNEL);
		if (!addrs) {
			ret = -ENOMEM;
			goto free_pages;
		}

		for (i = 0; i < npages; i++) {
			addrs[i] = dma_map_page(dev->dev, pages[i],
					0, PAGE_SIZE, DMA_BIDIRECTIONAL);
		}
	} else {
		addrs = kzalloc(npages * sizeof(*addrs), GFP_KERNEL);
		if (!addrs) {
			ret = -ENOMEM;
			goto free_pages;
		}
	}

	omap_obj->addrs = addrs;
	omap_obj->pages = pages;

	return 0;

free_pages:
	drm_gem_put_pages(obj, pages, true, false);

	return ret;
}

/** release backing pages */
static void omap_gem_detach_pages(struct drm_gem_object *obj)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);

	/* for non-cached buffers, ensure the new pages are clean because
	 * DSS, GPU, etc. are not cache coherent:
	 */
	if (omap_obj->flags & (OMAP_BO_WC|OMAP_BO_UNCACHED)) {
		int i, npages = obj->size >> PAGE_SHIFT;
		for (i = 0; i < npages; i++) {
			dma_unmap_page(obj->dev->dev, omap_obj->addrs[i],
					PAGE_SIZE, DMA_BIDIRECTIONAL);
		}
	}

	kfree(omap_obj->addrs);
	omap_obj->addrs = NULL;

	drm_gem_put_pages(obj, omap_obj->pages, true, false);
	omap_obj->pages = NULL;
}

/* get buffer flags */
uint32_t omap_gem_flags(struct drm_gem_object *obj)
{
	return to_omap_bo(obj)->flags;
}

/** get mmap offset */
static uint64_t mmap_offset(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	int ret;
	size_t size;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	/* Make it mmapable */
	size = omap_gem_mmap_size(obj);
	ret = drm_gem_create_mmap_offset_size(obj, size);
	if (ret) {
		dev_err(dev->dev, "could not allocate mmap offset\n");
		return 0;
	}

	return drm_vma_node_offset_addr(&obj->vma_node);
}

uint64_t omap_gem_mmap_offset(struct drm_gem_object *obj)
{
	uint64_t offset;
	mutex_lock(&obj->dev->struct_mutex);
	offset = mmap_offset(obj);
	mutex_unlock(&obj->dev->struct_mutex);
	return offset;
}

/** get mmap size */
size_t omap_gem_mmap_size(struct drm_gem_object *obj)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	size_t size = obj->size;

	if (omap_obj->flags & OMAP_BO_TILED) {
		/* for tiled buffers, the virtual size has stride rounded up
		 * to 4kb.. (to hide the fact that row n+1 might start 16kb or
		 * 32kb later!).  But we don't back the entire buffer with
		 * pages, only the valid picture part.. so need to adjust for
		 * this in the size used to mmap and generate mmap offset
		 */
		size = tiler_vsize(gem2fmt(omap_obj->flags),
				omap_obj->width, omap_obj->height);
	}

	return size;
}

/* get tiled size, returns -EINVAL if not tiled buffer */
int omap_gem_tiled_size(struct drm_gem_object *obj, uint16_t *w, uint16_t *h)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	if (omap_obj->flags & OMAP_BO_TILED) {
		*w = omap_obj->width;
		*h = omap_obj->height;
		return 0;
	}
	return -EINVAL;
}

/* Normal handling for the case of faulting in non-tiled buffers */
static int fault_1d(struct drm_gem_object *obj,
		struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	unsigned long pfn;
	pgoff_t pgoff;

	/* We don't use vmf->pgoff since that has the fake offset: */
	pgoff = ((unsigned long)vmf->virtual_address -
			vma->vm_start) >> PAGE_SHIFT;

	if (omap_obj->pages) {
		omap_gem_cpu_sync(obj, pgoff);
		pfn = page_to_pfn(omap_obj->pages[pgoff]);
	} else {
		BUG_ON(!(omap_obj->flags & OMAP_BO_DMA));
		pfn = (omap_obj->paddr >> PAGE_SHIFT) + pgoff;
	}

	VERB("Inserting %p pfn %lx, pa %lx", vmf->virtual_address,
			pfn, pfn << PAGE_SHIFT);

	return vm_insert_mixed(vma, (unsigned long)vmf->virtual_address, pfn);
}

/* Special handling for the case of faulting in 2d tiled buffers */
static int fault_2d(struct drm_gem_object *obj,
		struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	struct usergart_entry *entry;
	enum tiler_fmt fmt = gem2fmt(omap_obj->flags);
	struct page *pages[64];  /* XXX is this too much to have on stack? */
	unsigned long pfn;
	pgoff_t pgoff, base_pgoff;
	void __user *vaddr;
	int i, ret, slots;

	/*
	 * Note the height of the slot is also equal to the number of pages
	 * that need to be mapped in to fill 4kb wide CPU page.  If the slot
	 * height is 64, then 64 pages fill a 4kb wide by 64 row region.
	 */
	const int n = usergart[fmt].height;
	const int n_shift = usergart[fmt].height_shift;

	/*
	 * If buffer width in bytes > PAGE_SIZE then the virtual stride is
	 * rounded up to next multiple of PAGE_SIZE.. this need to be taken
	 * into account in some of the math, so figure out virtual stride
	 * in pages
	 */
	const int m = 1 + ((omap_obj->width << fmt) / PAGE_SIZE);

	/* We don't use vmf->pgoff since that has the fake offset: */
	pgoff = ((unsigned long)vmf->virtual_address -
			vma->vm_start) >> PAGE_SHIFT;

	/*
	 * Actual address we start mapping at is rounded down to previous slot
	 * boundary in the y direction:
	 */
	base_pgoff = round_down(pgoff, m << n_shift);

	/* figure out buffer width in slots */
	slots = omap_obj->width >> usergart[fmt].slot_shift;

	vaddr = vmf->virtual_address - ((pgoff - base_pgoff) << PAGE_SHIFT);

	entry = &usergart[fmt].entry[usergart[fmt].last];

	/* evict previous buffer using this usergart entry, if any: */
	if (entry->obj)
		evict_entry(entry->obj, fmt, entry);

	entry->obj = obj;
	entry->obj_pgoff = base_pgoff;

	/* now convert base_pgoff to phys offset from virt offset: */
	base_pgoff = (base_pgoff >> n_shift) * slots;

	/* for wider-than 4k.. figure out which part of the slot-row we want: */
	if (m > 1) {
		int off = pgoff % m;
		entry->obj_pgoff += off;
		base_pgoff /= m;
		slots = min(slots - (off << n_shift), n);
		base_pgoff += off << n_shift;
		vaddr += off << PAGE_SHIFT;
	}

	/*
	 * Map in pages. Beyond the valid pixel part of the buffer, we set
	 * pages[i] to NULL to get a dummy page mapped in.. if someone
	 * reads/writes it they will get random/undefined content, but at
	 * least it won't be corrupting whatever other random page used to
	 * be mapped in, or other undefined behavior.
	 */
	memcpy(pages, &omap_obj->pages[base_pgoff],
			sizeof(struct page *) * slots);
	memset(pages + slots, 0,
			sizeof(struct page *) * (n - slots));

	ret = tiler_pin(entry->block, pages, ARRAY_SIZE(pages), 0, true);
	if (ret) {
		dev_err(obj->dev->dev, "failed to pin: %d\n", ret);
		return ret;
	}

	pfn = entry->paddr >> PAGE_SHIFT;

	VERB("Inserting %p pfn %lx, pa %lx", vmf->virtual_address,
			pfn, pfn << PAGE_SHIFT);

	for (i = n; i > 0; i--) {
		vm_insert_mixed(vma, (unsigned long)vaddr, pfn);
		pfn += usergart[fmt].stride_pfn;
		vaddr += PAGE_SIZE * m;
	}

	/* simple round-robin: */
	usergart[fmt].last = (usergart[fmt].last + 1) % NUM_USERGART_ENTRIES;

	return 0;
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
	int ret;

	/* Make sure we don't parallel update on a fault, nor move or remove
	 * something from beneath our feet
	 */
	mutex_lock(&dev->struct_mutex);

	/* if a shmem backed object, make sure we have pages attached now */
	ret = get_pages(obj, &pages);
	if (ret)
		goto fail;

	/* where should we do corresponding put_pages().. we are mapping
	 * the original page, rather than thru a GART, so we can't rely
	 * on eviction to trigger this.  But munmap() or all mappings should
	 * probably trigger put_pages()?
	 */

	if (omap_obj->flags & OMAP_BO_TILED)
		ret = fault_2d(obj, vma, vmf);
	else
		ret = fault_1d(obj, vma, vmf);


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
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret) {
		DBG("mmap failed: %d", ret);
		return ret;
	}

	return omap_gem_mmap_obj(vma->vm_private_data, vma);
}

int omap_gem_mmap_obj(struct drm_gem_object *obj,
		struct vm_area_struct *vma)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);

	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_flags |= VM_MIXEDMAP;

	if (omap_obj->flags & OMAP_BO_WC) {
		vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
	} else if (omap_obj->flags & OMAP_BO_UNCACHED) {
		vma->vm_page_prot = pgprot_noncached(vm_get_page_prot(vma->vm_flags));
	} else {
		/*
		 * We do have some private objects, at least for scanout buffers
		 * on hardware without DMM/TILER.  But these are allocated write-
		 * combine
		 */
		if (WARN_ON(!obj->filp))
			return -EINVAL;

		/*
		 * Shunt off cached objs to shmem file so they have their own
		 * address_space (so unmap_mapping_range does what we want,
		 * in particular in the case of mmap'd dmabufs)
		 */
		fput(vma->vm_file);
		vma->vm_pgoff = 0;
		vma->vm_file  = get_file(obj->filp);

		vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	}

	return 0;
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

	/* GEM does all our handle to object mapping */
	obj = drm_gem_object_lookup(dev, file, handle);
	if (obj == NULL) {
		ret = -ENOENT;
		goto fail;
	}

	*offset = omap_gem_mmap_offset(obj);

	drm_gem_object_unreference_unlocked(obj);

fail:
	return ret;
}

/* Set scrolling position.  This allows us to implement fast scrolling
 * for console.
 *
 * Call only from non-atomic contexts.
 */
int omap_gem_roll(struct drm_gem_object *obj, uint32_t roll)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	uint32_t npages = obj->size >> PAGE_SHIFT;
	int ret = 0;

	if (roll > npages) {
		dev_err(obj->dev->dev, "invalid roll: %d\n", roll);
		return -EINVAL;
	}

	omap_obj->roll = roll;

	mutex_lock(&obj->dev->struct_mutex);

	/* if we aren't mapped yet, we don't need to do anything */
	if (omap_obj->block) {
		struct page **pages;
		ret = get_pages(obj, &pages);
		if (ret)
			goto fail;
		ret = tiler_pin(omap_obj->block, pages, npages, roll, true);
		if (ret)
			dev_err(obj->dev->dev, "could not repin: %d\n", ret);
	}

fail:
	mutex_unlock(&obj->dev->struct_mutex);

	return ret;
}

/* Sync the buffer for CPU access.. note pages should already be
 * attached, ie. omap_gem_get_pages()
 */
void omap_gem_cpu_sync(struct drm_gem_object *obj, int pgoff)
{
	struct drm_device *dev = obj->dev;
	struct omap_gem_object *omap_obj = to_omap_bo(obj);

	if (is_cached_coherent(obj) && omap_obj->addrs[pgoff]) {
		dma_unmap_page(dev->dev, omap_obj->addrs[pgoff],
				PAGE_SIZE, DMA_BIDIRECTIONAL);
		omap_obj->addrs[pgoff] = 0;
	}
}

/* sync the buffer for DMA access */
void omap_gem_dma_sync(struct drm_gem_object *obj,
		enum dma_data_direction dir)
{
	struct drm_device *dev = obj->dev;
	struct omap_gem_object *omap_obj = to_omap_bo(obj);

	if (is_cached_coherent(obj)) {
		int i, npages = obj->size >> PAGE_SHIFT;
		struct page **pages = omap_obj->pages;
		bool dirty = false;

		for (i = 0; i < npages; i++) {
			if (!omap_obj->addrs[i]) {
				omap_obj->addrs[i] = dma_map_page(dev->dev, pages[i], 0,
						PAGE_SIZE, DMA_BIDIRECTIONAL);
				dirty = true;
			}
		}

		if (dirty) {
			unmap_mapping_range(obj->filp->f_mapping, 0,
					omap_gem_mmap_size(obj), 1);
		}
	}
}

/* Get physical address for DMA.. if 'remap' is true, and the buffer is not
 * already contiguous, remap it to pin in physically contiguous memory.. (ie.
 * map in TILER)
 */
int omap_gem_get_paddr(struct drm_gem_object *obj,
		dma_addr_t *paddr, bool remap)
{
	struct omap_drm_private *priv = obj->dev->dev_private;
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	int ret = 0;

	mutex_lock(&obj->dev->struct_mutex);

	if (remap && is_shmem(obj) && priv->has_dmm) {
		if (omap_obj->paddr_cnt == 0) {
			struct page **pages;
			uint32_t npages = obj->size >> PAGE_SHIFT;
			enum tiler_fmt fmt = gem2fmt(omap_obj->flags);
			struct tiler_block *block;

			BUG_ON(omap_obj->block);

			ret = get_pages(obj, &pages);
			if (ret)
				goto fail;

			if (omap_obj->flags & OMAP_BO_TILED) {
				block = tiler_reserve_2d(fmt,
						omap_obj->width,
						omap_obj->height, 0);
			} else {
				block = tiler_reserve_1d(obj->size);
			}

			if (IS_ERR(block)) {
				ret = PTR_ERR(block);
				dev_err(obj->dev->dev,
					"could not remap: %d (%d)\n", ret, fmt);
				goto fail;
			}

			/* TODO: enable async refill.. */
			ret = tiler_pin(block, pages, npages,
					omap_obj->roll, true);
			if (ret) {
				tiler_release(block);
				dev_err(obj->dev->dev,
						"could not pin: %d\n", ret);
				goto fail;
			}

			omap_obj->paddr = tiler_ssptr(block);
			omap_obj->block = block;

			DBG("got paddr: %08x", omap_obj->paddr);
		}

		omap_obj->paddr_cnt++;

		*paddr = omap_obj->paddr;
	} else if (omap_obj->flags & OMAP_BO_DMA) {
		*paddr = omap_obj->paddr;
	} else {
		ret = -EINVAL;
		goto fail;
	}

fail:
	mutex_unlock(&obj->dev->struct_mutex);

	return ret;
}

/* Release physical address, when DMA is no longer being performed.. this
 * could potentially unpin and unmap buffers from TILER
 */
int omap_gem_put_paddr(struct drm_gem_object *obj)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	int ret = 0;

	mutex_lock(&obj->dev->struct_mutex);
	if (omap_obj->paddr_cnt > 0) {
		omap_obj->paddr_cnt--;
		if (omap_obj->paddr_cnt == 0) {
			ret = tiler_unpin(omap_obj->block);
			if (ret) {
				dev_err(obj->dev->dev,
					"could not unpin pages: %d\n", ret);
				goto fail;
			}
			ret = tiler_release(omap_obj->block);
			if (ret) {
				dev_err(obj->dev->dev,
					"could not release unmap: %d\n", ret);
			}
			omap_obj->block = NULL;
		}
	}
fail:
	mutex_unlock(&obj->dev->struct_mutex);
	return ret;
}

/* Get rotated scanout address (only valid if already pinned), at the
 * specified orientation and x,y offset from top-left corner of buffer
 * (only valid for tiled 2d buffers)
 */
int omap_gem_rotated_paddr(struct drm_gem_object *obj, uint32_t orient,
		int x, int y, dma_addr_t *paddr)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	int ret = -EINVAL;

	mutex_lock(&obj->dev->struct_mutex);
	if ((omap_obj->paddr_cnt > 0) && omap_obj->block &&
			(omap_obj->flags & OMAP_BO_TILED)) {
		*paddr = tiler_tsptr(omap_obj->block, orient, x, y);
		ret = 0;
	}
	mutex_unlock(&obj->dev->struct_mutex);
	return ret;
}

/* Get tiler stride for the buffer (only valid for 2d tiled buffers) */
int omap_gem_tiled_stride(struct drm_gem_object *obj, uint32_t orient)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	int ret = -EINVAL;
	if (omap_obj->flags & OMAP_BO_TILED)
		ret = tiler_stride(gem2fmt(omap_obj->flags), orient);
	return ret;
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

/* if !remap, and we don't have pages backing, then fail, rather than
 * increasing the pin count (which we don't really do yet anyways,
 * because we don't support swapping pages back out).  And 'remap'
 * might not be quite the right name, but I wanted to keep it working
 * similarly to omap_gem_get_paddr().  Note though that mutex is not
 * aquired if !remap (because this can be called in atomic ctxt),
 * but probably omap_gem_get_paddr() should be changed to work in the
 * same way.  If !remap, a matching omap_gem_put_pages() call is not
 * required (and should not be made).
 */
int omap_gem_get_pages(struct drm_gem_object *obj, struct page ***pages,
		bool remap)
{
	int ret;
	if (!remap) {
		struct omap_gem_object *omap_obj = to_omap_bo(obj);
		if (!omap_obj->pages)
			return -ENOMEM;
		*pages = omap_obj->pages;
		return 0;
	}
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

/* Get kernel virtual address for CPU access.. this more or less only
 * exists for omap_fbdev.  This should be called with struct_mutex
 * held.
 */
void *omap_gem_vaddr(struct drm_gem_object *obj)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	WARN_ON(!mutex_is_locked(&obj->dev->struct_mutex));
	if (!omap_obj->vaddr) {
		struct page **pages;
		int ret = get_pages(obj, &pages);
		if (ret)
			return ERR_PTR(ret);
		omap_obj->vaddr = vmap(pages, obj->size >> PAGE_SHIFT,
				VM_MAP, pgprot_writecombine(PAGE_KERNEL));
	}
	return omap_obj->vaddr;
}

#ifdef CONFIG_PM
/* re-pin objects in DMM in resume path: */
int omap_gem_resume(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct omap_drm_private *priv = drm_dev->dev_private;
	struct omap_gem_object *omap_obj;
	int ret = 0;

	list_for_each_entry(omap_obj, &priv->obj_list, mm_list) {
		if (omap_obj->block) {
			struct drm_gem_object *obj = &omap_obj->base;
			uint32_t npages = obj->size >> PAGE_SHIFT;
			WARN_ON(!omap_obj->pages);  /* this can't happen */
			ret = tiler_pin(omap_obj->block,
					omap_obj->pages, npages,
					omap_obj->roll, true);
			if (ret) {
				dev_err(dev, "could not repin: %d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}
#endif

#ifdef CONFIG_DEBUG_FS
void omap_gem_describe(struct drm_gem_object *obj, struct seq_file *m)
{
	struct omap_gem_object *omap_obj = to_omap_bo(obj);
	uint64_t off;

	off = drm_vma_node_start(&obj->vma_node);

	seq_printf(m, "%08x: %2d (%2d) %08llx %08Zx (%2d) %p %4d",
			omap_obj->flags, obj->name, obj->refcount.refcount.counter,
			off, omap_obj->paddr, omap_obj->paddr_cnt,
			omap_obj->vaddr, omap_obj->roll);

	if (omap_obj->flags & OMAP_BO_TILED) {
		seq_printf(m, " %dx%d", omap_obj->width, omap_obj->height);
		if (omap_obj->block) {
			struct tcm_area *area = &omap_obj->block->area;
			seq_printf(m, " (%dx%d, %dx%d)",
					area->p0.x, area->p0.y,
					area->p1.x, area->p1.y);
		}
	} else {
		seq_printf(m, " %d", obj->size);
	}

	seq_printf(m, "\n");
}

void omap_gem_describe_objects(struct list_head *list, struct seq_file *m)
{
	struct omap_gem_object *omap_obj;
	int count = 0;
	size_t size = 0;

	list_for_each_entry(omap_obj, list, mm_list) {
		struct drm_gem_object *obj = &omap_obj->base;
		seq_printf(m, "   ");
		omap_gem_describe(obj, m);
		count++;
		size += obj->size;
	}

	seq_printf(m, "Total %d objects, %zu bytes\n", count, size);
}
#endif

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
			(omap_obj->sync->write_complete < waiter->write_target))
		return true;
	if ((waiter->op & OMAP_GEM_WRITE) &&
			(omap_obj->sync->read_complete < waiter->read_target))
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

		if (!waiter)
			return -ENOMEM;

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
		kfree(waiter);
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

		if (!waiter)
			return -ENOMEM;

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

		kfree(waiter);
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
		syncobj = kmemdup(omap_obj->sync, sizeof(*omap_obj->sync),
				  GFP_ATOMIC);
		if (!syncobj) {
			ret = -ENOMEM;
			goto unlock;
		}
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

/* don't call directly.. called from GEM core when it is time to actually
 * free the object..
 */
void omap_gem_free_object(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct omap_gem_object *omap_obj = to_omap_bo(obj);

	evict(obj);

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	list_del(&omap_obj->mm_list);

	drm_gem_free_mmap_offset(obj);

	/* this means the object is still pinned.. which really should
	 * not happen.  I think..
	 */
	WARN_ON(omap_obj->paddr_cnt > 0);

	/* don't free externally allocated backing memory */
	if (!(omap_obj->flags & OMAP_BO_EXT_MEM)) {
		if (omap_obj->pages)
			omap_gem_detach_pages(obj);

		if (!is_shmem(obj)) {
			dma_free_writecombine(dev->dev, obj->size,
					omap_obj->vaddr, omap_obj->paddr);
		} else if (omap_obj->vaddr) {
			vunmap(omap_obj->vaddr);
		}
	}

	/* don't free externally allocated syncobj */
	if (!(omap_obj->flags & OMAP_BO_EXT_SYNC))
		kfree(omap_obj->sync);

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
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_gem_object *omap_obj;
	struct drm_gem_object *obj = NULL;
	struct address_space *mapping;
	size_t size;
	int ret;

	if (flags & OMAP_BO_TILED) {
		if (!usergart) {
			dev_err(dev->dev, "Tiled buffers require DMM\n");
			goto fail;
		}

		/* tiled buffers are always shmem paged backed.. when they are
		 * scanned out, they are remapped into DMM/TILER
		 */
		flags &= ~OMAP_BO_SCANOUT;

		/* currently don't allow cached buffers.. there is some caching
		 * stuff that needs to be handled better
		 */
		flags &= ~(OMAP_BO_CACHED|OMAP_BO_UNCACHED);
		flags |= OMAP_BO_WC;

		/* align dimensions to slot boundaries... */
		tiler_align(gem2fmt(flags),
				&gsize.tiled.width, &gsize.tiled.height);

		/* ...and calculate size based on aligned dimensions */
		size = tiler_size(gem2fmt(flags),
				gsize.tiled.width, gsize.tiled.height);
	} else {
		size = PAGE_ALIGN(gsize.bytes);
	}

	omap_obj = kzalloc(sizeof(*omap_obj), GFP_KERNEL);
	if (!omap_obj)
		goto fail;

	list_add(&omap_obj->mm_list, &priv->obj_list);

	obj = &omap_obj->base;

	if ((flags & OMAP_BO_SCANOUT) && !priv->has_dmm) {
		/* attempt to allocate contiguous memory if we don't
		 * have DMM for remappign discontiguous buffers
		 */
		omap_obj->vaddr =  dma_alloc_writecombine(dev->dev, size,
				&omap_obj->paddr, GFP_KERNEL);
		if (omap_obj->vaddr)
			flags |= OMAP_BO_DMA;

	}

	omap_obj->flags = flags;

	if (flags & OMAP_BO_TILED) {
		omap_obj->width = gsize.tiled.width;
		omap_obj->height = gsize.tiled.height;
	}

	if (flags & (OMAP_BO_DMA|OMAP_BO_EXT_MEM)) {
		drm_gem_private_object_init(dev, obj, size);
	} else {
		ret = drm_gem_object_init(dev, obj, size);
		if (ret)
			goto fail;

		mapping = file_inode(obj->filp)->i_mapping;
		mapping_set_gfp_mask(mapping, GFP_USER | __GFP_DMA32);
	}

	return obj;

fail:
	if (obj)
		omap_gem_free_object(obj);

	return NULL;
}

/* init/cleanup.. if DMM is used, we need to set some stuff up.. */
void omap_gem_init(struct drm_device *dev)
{
	struct omap_drm_private *priv = dev->dev_private;
	const enum tiler_fmt fmts[] = {
			TILFMT_8BIT, TILFMT_16BIT, TILFMT_32BIT
	};
	int i, j;

	if (!dmm_is_available()) {
		/* DMM only supported on OMAP4 and later, so this isn't fatal */
		dev_warn(dev->dev, "DMM not available, disable DMM support\n");
		return;
	}

	usergart = kcalloc(3, sizeof(*usergart), GFP_KERNEL);
	if (!usergart)
		return;

	/* reserve 4k aligned/wide regions for userspace mappings: */
	for (i = 0; i < ARRAY_SIZE(fmts); i++) {
		uint16_t h = 1, w = PAGE_SIZE >> i;
		tiler_align(fmts[i], &w, &h);
		/* note: since each region is 1 4kb page wide, and minimum
		 * number of rows, the height ends up being the same as the
		 * # of pages in the region
		 */
		usergart[i].height = h;
		usergart[i].height_shift = ilog2(h);
		usergart[i].stride_pfn = tiler_stride(fmts[i], 0) >> PAGE_SHIFT;
		usergart[i].slot_shift = ilog2((PAGE_SIZE / h) >> i);
		for (j = 0; j < NUM_USERGART_ENTRIES; j++) {
			struct usergart_entry *entry = &usergart[i].entry[j];
			struct tiler_block *block =
					tiler_reserve_2d(fmts[i], w, h,
							PAGE_SIZE);
			if (IS_ERR(block)) {
				dev_err(dev->dev,
						"reserve failed: %d, %d, %ld\n",
						i, j, PTR_ERR(block));
				return;
			}
			entry->paddr = tiler_ssptr(block);
			entry->block = block;

			DBG("%d:%d: %dx%d: paddr=%08x stride=%d", i, j, w, h,
					entry->paddr,
					usergart[i].stride_pfn << PAGE_SHIFT);
		}
	}

	priv->has_dmm = true;
}

void omap_gem_deinit(struct drm_device *dev)
{
	/* I believe we can rely on there being no more outstanding GEM
	 * objects which could depend on usergart/dmm at this point.
	 */
	kfree(usergart);
}
