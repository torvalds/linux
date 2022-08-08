/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __MSM_GEM_H__
#define __MSM_GEM_H__

#include <linux/kref.h>
#include <linux/dma-resv.h>
#include "msm_drv.h"

/* Make all GEM related WARN_ON()s ratelimited.. when things go wrong they
 * tend to go wrong 1000s of times in a short timespan.
 */
#define GEM_WARN_ON(x)  WARN_RATELIMIT(x, "%s", __stringify(x))

/* Additional internal-use only BO flags: */
#define MSM_BO_STOLEN        0x10000000    /* try to use stolen/splash memory */
#define MSM_BO_MAP_PRIV      0x20000000    /* use IOMMU_PRIV when mapping */

struct msm_gem_address_space {
	const char *name;
	/* NOTE: mm managed at the page level, size is in # of pages
	 * and position mm_node->start is in # of pages:
	 */
	struct drm_mm mm;
	spinlock_t lock; /* Protects drm_mm node allocation/removal */
	struct msm_mmu *mmu;
	struct kref kref;

	/* For address spaces associated with a specific process, this
	 * will be non-NULL:
	 */
	struct pid *pid;
};

struct msm_gem_vma {
	struct drm_mm_node node;
	uint64_t iova;
	struct msm_gem_address_space *aspace;
	struct list_head list;    /* node in msm_gem_object::vmas */
	bool mapped;
	int inuse;
};

struct msm_gem_object {
	struct drm_gem_object base;

	uint32_t flags;

	/**
	 * Advice: are the backing pages purgeable?
	 */
	uint8_t madv;

	/**
	 * Is object on inactive_dontneed list (ie. counted in priv->shrinkable_count)?
	 */
	bool dontneed : 1;

	/**
	 * Is object evictable (ie. counted in priv->evictable_count)?
	 */
	bool evictable : 1;

	/**
	 * count of active vmap'ing
	 */
	uint8_t vmap_count;

	/**
	 * Node in list of all objects (mainly for debugfs, protected by
	 * priv->obj_lock
	 */
	struct list_head node;

	/**
	 * An object is either:
	 *  inactive - on priv->inactive_dontneed or priv->inactive_willneed
	 *     (depending on purgeability status)
	 *  active   - on one one of the gpu's active_list..  well, at
	 *     least for now we don't have (I don't think) hw sync between
	 *     2d and 3d one devices which have both, meaning we need to
	 *     block on submit if a bo is already on other ring
	 */
	struct list_head mm_list;

	/* Transiently in the process of submit ioctl, objects associated
	 * with the submit are on submit->bo_list.. this only lasts for
	 * the duration of the ioctl, so one bo can never be on multiple
	 * submit lists.
	 */
	struct list_head submit_entry;

	struct page **pages;
	struct sg_table *sgt;
	void *vaddr;

	struct list_head vmas;    /* list of msm_gem_vma */

	/* For physically contiguous buffers.  Used when we don't have
	 * an IOMMU.  Also used for stolen/splashscreen buffer.
	 */
	struct drm_mm_node *vram_node;

	char name[32]; /* Identifier to print for the debugfs files */

	int active_count;
	int pin_count;
};
#define to_msm_bo(x) container_of(x, struct msm_gem_object, base)

int msm_gem_mmap_obj(struct drm_gem_object *obj,
			struct vm_area_struct *vma);
int msm_gem_mmap(struct file *filp, struct vm_area_struct *vma);
uint64_t msm_gem_mmap_offset(struct drm_gem_object *obj);
int msm_gem_get_iova(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace, uint64_t *iova);
int msm_gem_get_and_pin_iova_range(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace, uint64_t *iova,
		u64 range_start, u64 range_end);
int msm_gem_get_and_pin_iova_locked(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace, uint64_t *iova);
int msm_gem_get_and_pin_iova(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace, uint64_t *iova);
uint64_t msm_gem_iova(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace);
void msm_gem_unpin_iova_locked(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace);
void msm_gem_unpin_iova(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace);
struct page **msm_gem_get_pages(struct drm_gem_object *obj);
void msm_gem_put_pages(struct drm_gem_object *obj);
int msm_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
		struct drm_mode_create_dumb *args);
int msm_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
		uint32_t handle, uint64_t *offset);
void *msm_gem_get_vaddr_locked(struct drm_gem_object *obj);
void *msm_gem_get_vaddr(struct drm_gem_object *obj);
void *msm_gem_get_vaddr_active(struct drm_gem_object *obj);
void msm_gem_put_vaddr_locked(struct drm_gem_object *obj);
void msm_gem_put_vaddr(struct drm_gem_object *obj);
int msm_gem_madvise(struct drm_gem_object *obj, unsigned madv);
int msm_gem_sync_object(struct drm_gem_object *obj,
		struct msm_fence_context *fctx, bool exclusive);
void msm_gem_active_get(struct drm_gem_object *obj, struct msm_gpu *gpu);
void msm_gem_active_put(struct drm_gem_object *obj);
int msm_gem_cpu_prep(struct drm_gem_object *obj, uint32_t op, ktime_t *timeout);
int msm_gem_cpu_fini(struct drm_gem_object *obj);
void msm_gem_free_object(struct drm_gem_object *obj);
int msm_gem_new_handle(struct drm_device *dev, struct drm_file *file,
		uint32_t size, uint32_t flags, uint32_t *handle, char *name);
struct drm_gem_object *msm_gem_new(struct drm_device *dev,
		uint32_t size, uint32_t flags);
struct drm_gem_object *msm_gem_new_locked(struct drm_device *dev,
		uint32_t size, uint32_t flags);
void *msm_gem_kernel_new(struct drm_device *dev, uint32_t size,
		uint32_t flags, struct msm_gem_address_space *aspace,
		struct drm_gem_object **bo, uint64_t *iova);
void *msm_gem_kernel_new_locked(struct drm_device *dev, uint32_t size,
		uint32_t flags, struct msm_gem_address_space *aspace,
		struct drm_gem_object **bo, uint64_t *iova);
void msm_gem_kernel_put(struct drm_gem_object *bo,
		struct msm_gem_address_space *aspace, bool locked);
struct drm_gem_object *msm_gem_import(struct drm_device *dev,
		struct dma_buf *dmabuf, struct sg_table *sgt);
__printf(2, 3)
void msm_gem_object_set_name(struct drm_gem_object *bo, const char *fmt, ...);

#ifdef CONFIG_DEBUG_FS
struct msm_gem_stats {
	struct {
		unsigned count;
		size_t size;
	} all, active, resident, purgeable, purged;
};

void msm_gem_describe(struct drm_gem_object *obj, struct seq_file *m,
		struct msm_gem_stats *stats);
void msm_gem_describe_objects(struct list_head *list, struct seq_file *m);
#endif

static inline void
msm_gem_lock(struct drm_gem_object *obj)
{
	dma_resv_lock(obj->resv, NULL);
}

static inline bool __must_check
msm_gem_trylock(struct drm_gem_object *obj)
{
	return dma_resv_trylock(obj->resv);
}

static inline int
msm_gem_lock_interruptible(struct drm_gem_object *obj)
{
	return dma_resv_lock_interruptible(obj->resv, NULL);
}

static inline void
msm_gem_unlock(struct drm_gem_object *obj)
{
	dma_resv_unlock(obj->resv);
}

static inline bool
msm_gem_is_locked(struct drm_gem_object *obj)
{
	return dma_resv_is_locked(obj->resv);
}

static inline bool is_active(struct msm_gem_object *msm_obj)
{
	GEM_WARN_ON(!msm_gem_is_locked(&msm_obj->base));
	return msm_obj->active_count;
}

/* imported/exported objects are not purgeable: */
static inline bool is_unpurgeable(struct msm_gem_object *msm_obj)
{
	return msm_obj->base.import_attach || msm_obj->pin_count;
}

static inline bool is_purgeable(struct msm_gem_object *msm_obj)
{
	return (msm_obj->madv == MSM_MADV_DONTNEED) && msm_obj->sgt &&
			!is_unpurgeable(msm_obj);
}

static inline bool is_vunmapable(struct msm_gem_object *msm_obj)
{
	GEM_WARN_ON(!msm_gem_is_locked(&msm_obj->base));
	return (msm_obj->vmap_count == 0) && msm_obj->vaddr;
}

static inline void mark_purgeable(struct msm_gem_object *msm_obj)
{
	struct msm_drm_private *priv = msm_obj->base.dev->dev_private;

	GEM_WARN_ON(!mutex_is_locked(&priv->mm_lock));

	if (is_unpurgeable(msm_obj))
		return;

	if (GEM_WARN_ON(msm_obj->dontneed))
		return;

	priv->shrinkable_count += msm_obj->base.size >> PAGE_SHIFT;
	msm_obj->dontneed = true;
}

static inline void mark_unpurgeable(struct msm_gem_object *msm_obj)
{
	struct msm_drm_private *priv = msm_obj->base.dev->dev_private;

	GEM_WARN_ON(!mutex_is_locked(&priv->mm_lock));

	if (is_unpurgeable(msm_obj))
		return;

	if (GEM_WARN_ON(!msm_obj->dontneed))
		return;

	priv->shrinkable_count -= msm_obj->base.size >> PAGE_SHIFT;
	GEM_WARN_ON(priv->shrinkable_count < 0);
	msm_obj->dontneed = false;
}

static inline bool is_unevictable(struct msm_gem_object *msm_obj)
{
	return is_unpurgeable(msm_obj) || msm_obj->vaddr;
}

static inline void mark_evictable(struct msm_gem_object *msm_obj)
{
	struct msm_drm_private *priv = msm_obj->base.dev->dev_private;

	WARN_ON(!mutex_is_locked(&priv->mm_lock));

	if (is_unevictable(msm_obj))
		return;

	if (WARN_ON(msm_obj->evictable))
		return;

	priv->evictable_count += msm_obj->base.size >> PAGE_SHIFT;
	msm_obj->evictable = true;
}

static inline void mark_unevictable(struct msm_gem_object *msm_obj)
{
	struct msm_drm_private *priv = msm_obj->base.dev->dev_private;

	WARN_ON(!mutex_is_locked(&priv->mm_lock));

	if (is_unevictable(msm_obj))
		return;

	if (WARN_ON(!msm_obj->evictable))
		return;

	priv->evictable_count -= msm_obj->base.size >> PAGE_SHIFT;
	WARN_ON(priv->evictable_count < 0);
	msm_obj->evictable = false;
}

void msm_gem_purge(struct drm_gem_object *obj);
void msm_gem_evict(struct drm_gem_object *obj);
void msm_gem_vunmap(struct drm_gem_object *obj);

/* Created per submit-ioctl, to track bo's and cmdstream bufs, etc,
 * associated with the cmdstream submission for synchronization (and
 * make it easier to unwind when things go wrong, etc).  This only
 * lasts for the duration of the submit-ioctl.
 */
struct msm_gem_submit {
	struct kref ref;
	struct drm_device *dev;
	struct msm_gpu *gpu;
	struct msm_gem_address_space *aspace;
	struct list_head node;   /* node in ring submit list */
	struct list_head bo_list;
	struct ww_acquire_ctx ticket;
	uint32_t seqno;		/* Sequence number of the submit on the ring */
	struct dma_fence *fence;
	struct msm_gpu_submitqueue *queue;
	struct pid *pid;    /* submitting process */
	bool valid;         /* true if no cmdstream patching needed */
	bool in_rb;         /* "sudo" mode, copy cmds into RB */
	struct msm_ringbuffer *ring;
	struct msm_file_private *ctx;
	unsigned int nr_cmds;
	unsigned int nr_bos;
	u32 ident;	   /* A "identifier" for the submit for logging */
	struct {
		uint32_t type;
		uint32_t size;  /* in dwords */
		uint64_t iova;
		uint32_t offset;/* in dwords */
		uint32_t idx;   /* cmdstream buffer idx in bos[] */
		uint32_t nr_relocs;
		struct drm_msm_gem_submit_reloc *relocs;
	} *cmd;  /* array of size nr_cmds */
	struct {
		uint32_t flags;
		union {
			struct msm_gem_object *obj;
			uint32_t handle;
		};
		uint64_t iova;
	} bos[];
};

void __msm_gem_submit_destroy(struct kref *kref);

static inline void msm_gem_submit_get(struct msm_gem_submit *submit)
{
	kref_get(&submit->ref);
}

static inline void msm_gem_submit_put(struct msm_gem_submit *submit)
{
	kref_put(&submit->ref, __msm_gem_submit_destroy);
}

/* helper to determine of a buffer in submit should be dumped, used for both
 * devcoredump and debugfs cmdstream dumping:
 */
static inline bool
should_dump(struct msm_gem_submit *submit, int idx)
{
	extern bool rd_full;
	return rd_full || (submit->bos[idx].flags & MSM_SUBMIT_BO_DUMP);
}

#endif /* __MSM_GEM_H__ */
