/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __MSM_GEM_H__
#define __MSM_GEM_H__

#include <linux/kref.h>
#include <linux/dma-resv.h>
#include "drm/gpu_scheduler.h"
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

	/* @faults: the number of GPU hangs associated with this address space */
	int faults;

	/** @va_start: lowest possible address to allocate */
	uint64_t va_start;

	/** @va_size: the size of the address space (in bytes) */
	uint64_t va_size;
};

struct msm_gem_address_space *
msm_gem_address_space_get(struct msm_gem_address_space *aspace);

void msm_gem_address_space_put(struct msm_gem_address_space *aspace);

struct msm_gem_address_space *
msm_gem_address_space_create(struct msm_mmu *mmu, const char *name,
		u64 va_start, u64 size);

struct msm_fence_context;

struct msm_gem_vma {
	struct drm_mm_node node;
	uint64_t iova;
	struct msm_gem_address_space *aspace;
	struct list_head list;    /* node in msm_gem_object::vmas */
	bool mapped;
};

struct msm_gem_vma *msm_gem_vma_new(struct msm_gem_address_space *aspace);
int msm_gem_vma_init(struct msm_gem_vma *vma, int size,
		u64 range_start, u64 range_end);
void msm_gem_vma_purge(struct msm_gem_vma *vma);
int msm_gem_vma_map(struct msm_gem_vma *vma, int prot, struct sg_table *sgt, int size);
void msm_gem_vma_close(struct msm_gem_vma *vma);

struct msm_gem_object {
	struct drm_gem_object base;

	uint32_t flags;

	/**
	 * madv: are the backing pages purgeable?
	 *
	 * Protected by obj lock and LRU lock
	 */
	uint8_t madv;

	/**
	 * count of active vmap'ing
	 */
	uint8_t vmap_count;

	/**
	 * Node in list of all objects (mainly for debugfs, protected by
	 * priv->obj_lock
	 */
	struct list_head node;

	struct page **pages;
	struct sg_table *sgt;
	void *vaddr;

	struct list_head vmas;    /* list of msm_gem_vma */

	/* For physically contiguous buffers.  Used when we don't have
	 * an IOMMU.  Also used for stolen/splashscreen buffer.
	 */
	struct drm_mm_node *vram_node;

	char name[32]; /* Identifier to print for the debugfs files */

	/**
	 * pin_count: Number of times the pages are pinned
	 *
	 * Protected by LRU lock.
	 */
	int pin_count;
};
#define to_msm_bo(x) container_of(x, struct msm_gem_object, base)

uint64_t msm_gem_mmap_offset(struct drm_gem_object *obj);
int msm_gem_pin_vma_locked(struct drm_gem_object *obj, struct msm_gem_vma *vma);
void msm_gem_unpin_locked(struct drm_gem_object *obj);
void msm_gem_unpin_active(struct drm_gem_object *obj);
struct msm_gem_vma *msm_gem_get_vma_locked(struct drm_gem_object *obj,
					   struct msm_gem_address_space *aspace);
int msm_gem_get_iova(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace, uint64_t *iova);
int msm_gem_set_iova(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace, uint64_t iova);
int msm_gem_get_and_pin_iova_range(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace, uint64_t *iova,
		u64 range_start, u64 range_end);
int msm_gem_get_and_pin_iova(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace, uint64_t *iova);
void msm_gem_unpin_iova(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace);
void msm_gem_pin_obj_locked(struct drm_gem_object *obj);
struct page **msm_gem_pin_pages(struct drm_gem_object *obj);
void msm_gem_unpin_pages(struct drm_gem_object *obj);
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
bool msm_gem_active(struct drm_gem_object *obj);
int msm_gem_cpu_prep(struct drm_gem_object *obj, uint32_t op, ktime_t *timeout);
int msm_gem_cpu_fini(struct drm_gem_object *obj);
int msm_gem_new_handle(struct drm_device *dev, struct drm_file *file,
		uint32_t size, uint32_t flags, uint32_t *handle, char *name);
struct drm_gem_object *msm_gem_new(struct drm_device *dev,
		uint32_t size, uint32_t flags);
void *msm_gem_kernel_new(struct drm_device *dev, uint32_t size,
		uint32_t flags, struct msm_gem_address_space *aspace,
		struct drm_gem_object **bo, uint64_t *iova);
void msm_gem_kernel_put(struct drm_gem_object *bo,
		struct msm_gem_address_space *aspace);
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

static inline void
msm_gem_assert_locked(struct drm_gem_object *obj)
{
	/*
	 * Destroying the object is a special case.. msm_gem_free_object()
	 * calls many things that WARN_ON if the obj lock is not held.  But
	 * acquiring the obj lock in msm_gem_free_object() can cause a
	 * locking order inversion between reservation_ww_class_mutex and
	 * fs_reclaim.
	 *
	 * This deadlock is not actually possible, because no one should
	 * be already holding the lock when msm_gem_free_object() is called.
	 * Unfortunately lockdep is not aware of this detail.  So when the
	 * refcount drops to zero, we pretend it is already locked.
	 */
	lockdep_assert_once(
		(kref_read(&obj->refcount) == 0) ||
		(lockdep_is_held(&obj->resv->lock.base) != LOCK_STATE_NOT_HELD)
	);
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
	msm_gem_assert_locked(&msm_obj->base);
	return (msm_obj->vmap_count == 0) && msm_obj->vaddr;
}

static inline bool is_unevictable(struct msm_gem_object *msm_obj)
{
	return is_unpurgeable(msm_obj) || msm_obj->vaddr;
}

void msm_gem_purge(struct drm_gem_object *obj);
void msm_gem_evict(struct drm_gem_object *obj);
void msm_gem_vunmap(struct drm_gem_object *obj);

/* Created per submit-ioctl, to track bo's and cmdstream bufs, etc,
 * associated with the cmdstream submission for synchronization (and
 * make it easier to unwind when things go wrong, etc).
 */
struct msm_gem_submit {
	struct drm_sched_job base;
	struct kref ref;
	struct drm_device *dev;
	struct msm_gpu *gpu;
	struct msm_gem_address_space *aspace;
	struct list_head node;   /* node in ring submit list */
	struct ww_acquire_ctx ticket;
	uint32_t seqno;		/* Sequence number of the submit on the ring */

	/* Hw fence, which is created when the scheduler executes the job, and
	 * is signaled when the hw finishes (via seqno write from cmdstream)
	 */
	struct dma_fence *hw_fence;

	/* Userspace visible fence, which is signaled by the scheduler after
	 * the hw_fence is signaled.
	 */
	struct dma_fence *user_fence;

	int fence_id;       /* key into queue->fence_idr */
	struct msm_gpu_submitqueue *queue;
	struct pid *pid;    /* submitting process */
	bool fault_dumped;  /* Limit devcoredump dumping to one per submit */
	bool valid;         /* true if no cmdstream patching needed */
	bool in_rb;         /* "sudo" mode, copy cmds into RB */
	struct msm_ringbuffer *ring;
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
/* make sure these don't conflict w/ MSM_SUBMIT_BO_x */
#define BO_VALID	0x8000	/* is current addr in cmdstream correct/valid? */
#define BO_LOCKED	0x4000	/* obj lock is held */
#define BO_PINNED	0x2000	/* obj (pages) is pinned and on active list */
		uint32_t flags;
		union {
			struct drm_gem_object *obj;
			uint32_t handle;
		};
		uint64_t iova;
	} bos[];
};

static inline struct msm_gem_submit *to_msm_submit(struct drm_sched_job *job)
{
	return container_of(job, struct msm_gem_submit, base);
}

void __msm_gem_submit_destroy(struct kref *kref);

static inline void msm_gem_submit_get(struct msm_gem_submit *submit)
{
	kref_get(&submit->ref);
}

static inline void msm_gem_submit_put(struct msm_gem_submit *submit)
{
	kref_put(&submit->ref, __msm_gem_submit_destroy);
}

void msm_submit_retire(struct msm_gem_submit *submit);

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
