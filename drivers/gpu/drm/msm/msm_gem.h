/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __MSM_GEM_H__
#define __MSM_GEM_H__

#include "msm_mmu.h"
#include <linux/kref.h>
#include <linux/dma-resv.h>
#include "drm/drm_exec.h"
#include "drm/drm_gpuvm.h"
#include "drm/gpu_scheduler.h"
#include "msm_drv.h"

/* Make all GEM related WARN_ON()s ratelimited.. when things go wrong they
 * tend to go wrong 1000s of times in a short timespan.
 */
#define GEM_WARN_ON(x)  WARN_RATELIMIT(x, "%s", __stringify(x))

/* Additional internal-use only BO flags: */
#define MSM_BO_STOLEN        0x10000000    /* try to use stolen/splash memory */
#define MSM_BO_MAP_PRIV      0x20000000    /* use IOMMU_PRIV when mapping */

/**
 * struct msm_gem_vm_log_entry - An entry in the VM log
 *
 * For userspace managed VMs, a log of recent VM updates is tracked and
 * captured in GPU devcore dumps, to aid debugging issues caused by (for
 * example) incorrectly synchronized VM updates
 */
struct msm_gem_vm_log_entry {
	const char *op;
	uint64_t iova;
	uint64_t range;
	int queue_id;
};

/**
 * struct msm_gem_vm - VM object
 *
 * A VM object representing a GPU (or display or GMU or ...) virtual address
 * space.
 *
 * In the case of GPU, if per-process address spaces are supported, the address
 * space is split into two VMs, which map to TTBR0 and TTBR1 in the SMMU.  TTBR0
 * is used for userspace objects, and is unique per msm_context/drm_file, while
 * TTBR1 is the same for all processes.  (The kernel controlled ringbuffer and
 * a few other kernel controlled buffers live in TTBR1.)
 *
 * The GPU TTBR0 vm can be managed by userspace or by the kernel, depending on
 * whether userspace supports VM_BIND.  All other vm's are managed by the kernel.
 * (Managed by kernel means the kernel is responsible for VA allocation.)
 *
 * Note that because VM_BIND allows a given BO to be mapped multiple times in
 * a VM, and therefore have multiple VMA's in a VM, there is an extra object
 * provided by drm_gpuvm infrastructure.. the drm_gpuvm_bo, which is not
 * embedded in any larger driver structure.  The GEM object holds a list of
 * drm_gpuvm_bo, which in turn holds a list of msm_gem_vma.  A linked vma
 * holds a reference to the vm_bo, and drops it when the vma is unlinked.
 * So we just need to call drm_gpuvm_bo_obtain() to return a ref to an
 * existing vm_bo, or create a new one.  Once the vma is linked, the ref
 * to the vm_bo can be dropped (since the vma is holding one).
 */
struct msm_gem_vm {
	/** @base: Inherit from drm_gpuvm. */
	struct drm_gpuvm base;

	/**
	 * @sched: Scheduler used for asynchronous VM_BIND request.
	 *
	 * Unused for kernel managed VMs (where all operations are synchronous).
	 */
	struct drm_gpu_scheduler sched;

	/**
	 * @prealloc_throttle: Used to throttle VM_BIND ops if too much pre-
	 * allocated memory is in flight.
	 *
	 * Because we have to pre-allocate pgtable pages for the worst case
	 * (ie. new mappings do not share any PTEs with existing mappings)
	 * we could end up consuming a lot of resources transiently.  The
	 * prealloc_throttle puts an upper bound on that.
	 */
	struct {
		/** @wait: Notified when preallocated resources are released */
		wait_queue_head_t wait;

		/**
		 * @in_flight: The # of preallocated pgtable pages in-flight
		 * for queued VM_BIND jobs.
		 */
		atomic_t in_flight;
	} prealloc_throttle;

	/**
	 * @mm: Memory management for kernel managed VA allocations
	 *
	 * Only used for kernel managed VMs, unused for user managed VMs.
	 *
	 * Protected by vm lock.  See msm_gem_lock_vm_and_obj(), for ex.
	 */
	struct drm_mm mm;

	/** @mmu: The mmu object which manages the pgtables */
	struct msm_mmu *mmu;

	/** @mmu_lock: Protects access to the mmu */
	struct mutex mmu_lock;

	/**
	 * @pid: For address spaces associated with a specific process, this
	 * will be non-NULL:
	 */
	struct pid *pid;

	/** @last_fence: Fence for last pending work scheduled on the VM */
	struct dma_fence *last_fence;

	/** @log: A log of recent VM updates */
	struct msm_gem_vm_log_entry *log;

	/** @log_shift: length of @log is (1 << @log_shift) */
	uint32_t log_shift;

	/** @log_idx: index of next @log entry to write */
	uint32_t log_idx;

	/** @faults: the number of GPU hangs associated with this address space */
	int faults;

	/** @managed: is this a kernel managed VM? */
	bool managed;

	/**
	 * @unusable: True if the VM has turned unusable because something
	 * bad happened during an asynchronous request.
	 *
	 * We don't try to recover from such failures, because this implies
	 * informing userspace about the specific operation that failed, and
	 * hoping the userspace driver can replay things from there. This all
	 * sounds very complicated for little gain.
	 *
	 * Instead, we should just flag the VM as unusable, and fail any
	 * further request targeting this VM.
	 *
	 * As an analogy, this would be mapped to a VK_ERROR_DEVICE_LOST
	 * situation, where the logical device needs to be re-created.
	 */
	bool unusable;
};
#define to_msm_vm(x) container_of(x, struct msm_gem_vm, base)

struct drm_gpuvm *
msm_gem_vm_create(struct drm_device *drm, struct msm_mmu *mmu, const char *name,
		  u64 va_start, u64 va_size, bool managed);

void msm_gem_vm_close(struct drm_gpuvm *gpuvm);
void msm_gem_vm_unusable(struct drm_gpuvm *gpuvm);

struct msm_fence_context;

#define MSM_VMA_DUMP (DRM_GPUVA_USERBITS << 0)

/**
 * struct msm_gem_vma - a VMA mapping
 *
 * Represents a combination of a GEM object plus a VM.
 */
struct msm_gem_vma {
	/** @base: inherit from drm_gpuva */
	struct drm_gpuva base;

	/**
	 * @node: mm node for VA allocation
	 *
	 * Only used by kernel managed VMs
	 */
	struct drm_mm_node node;

	/** @mapped: Is this VMA mapped? */
	bool mapped;
};
#define to_msm_vma(x) container_of(x, struct msm_gem_vma, base)

struct drm_gpuva *
msm_gem_vma_new(struct drm_gpuvm *vm, struct drm_gem_object *obj,
		u64 offset, u64 range_start, u64 range_end);
void msm_gem_vma_unmap(struct drm_gpuva *vma, const char *reason);
int msm_gem_vma_map(struct drm_gpuva *vma, int prot, struct sg_table *sgt);
void msm_gem_vma_close(struct drm_gpuva *vma);

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

	char name[32]; /* Identifier to print for the debugfs files */

	/* userspace metadata backchannel */
	void *metadata;
	u32 metadata_size;

	/**
	 * pin_count: Number of times the pages are pinned
	 *
	 * Protected by LRU lock.
	 */
	int pin_count;

	/**
	 * @vma_ref: Reference count of VMA users.
	 *
	 * With the vm_bo/vma holding a reference to the GEM object, we'd
	 * otherwise have to actively tear down a VMA when, for example,
	 * a buffer is unpinned for scanout, vs. the pre-drm_gpuvm approach
	 * where a VMA did not hold a reference to the BO, but instead was
	 * implicitly torn down when the BO was freed.
	 *
	 * To regain the lazy VMA teardown, we use the @vma_ref.  It is
	 * incremented for any of the following:
	 *
	 * 1) the BO is exported as a dma_buf
	 * 2) the BO has open userspace handle
	 *
	 * All of those conditions will hold an reference to the BO,
	 * preventing it from being freed.  So lazily keeping around the
	 * VMA will not prevent the BO from being freed.  (Or rather, the
	 * reference loop is harmless in this case.)
	 *
	 * When the @vma_ref drops to zero, then kms->vm VMA will be
	 * torn down.
	 */
	atomic_t vma_ref;
};
#define to_msm_bo(x) container_of(x, struct msm_gem_object, base)

void msm_gem_vma_get(struct drm_gem_object *obj);
void msm_gem_vma_put(struct drm_gem_object *obj);

uint64_t msm_gem_mmap_offset(struct drm_gem_object *obj);
int msm_gem_prot(struct drm_gem_object *obj);
int msm_gem_pin_vma_locked(struct drm_gem_object *obj, struct drm_gpuva *vma);
void msm_gem_unpin_locked(struct drm_gem_object *obj);
void msm_gem_unpin_active(struct drm_gem_object *obj);
struct drm_gpuva *msm_gem_get_vma_locked(struct drm_gem_object *obj,
					 struct drm_gpuvm *vm);
int msm_gem_get_iova(struct drm_gem_object *obj, struct drm_gpuvm *vm,
		     uint64_t *iova);
int msm_gem_set_iova(struct drm_gem_object *obj, struct drm_gpuvm *vm,
		     uint64_t iova);
int msm_gem_get_and_pin_iova_range(struct drm_gem_object *obj,
				   struct drm_gpuvm *vm, uint64_t *iova,
				   u64 range_start, u64 range_end);
int msm_gem_get_and_pin_iova(struct drm_gem_object *obj, struct drm_gpuvm *vm,
			     uint64_t *iova);
void msm_gem_unpin_iova(struct drm_gem_object *obj, struct drm_gpuvm *vm);
void msm_gem_pin_obj_locked(struct drm_gem_object *obj);
struct page **msm_gem_get_pages_locked(struct drm_gem_object *obj, unsigned madv);
struct page **msm_gem_pin_pages_locked(struct drm_gem_object *obj);
void msm_gem_unpin_pages_locked(struct drm_gem_object *obj);
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
		size_t size, uint32_t flags, uint32_t *handle, char *name);
struct drm_gem_object *msm_gem_new(struct drm_device *dev,
		size_t size, uint32_t flags);
void *msm_gem_kernel_new(struct drm_device *dev, size_t size, uint32_t flags,
			 struct drm_gpuvm *vm, struct drm_gem_object **bo,
			 uint64_t *iova);
void msm_gem_kernel_put(struct drm_gem_object *bo, struct drm_gpuvm *vm);
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

/**
 * msm_gem_lock_vm_and_obj() - Helper to lock an obj + VM
 * @exec: the exec context helper which will be initalized
 * @obj: the GEM object to lock
 * @vm: the VM to lock
 *
 * Operations which modify a VM frequently need to lock both the VM and
 * the object being mapped/unmapped/etc.  This helper uses drm_exec to
 * acquire both locks, dealing with potential deadlock/backoff scenarios
 * which arise when multiple locks are involved.
 */
static inline int
msm_gem_lock_vm_and_obj(struct drm_exec *exec,
			struct drm_gem_object *obj,
			struct drm_gpuvm *vm)
{
	int ret = 0;

	drm_exec_init(exec, 0, 2);
	drm_exec_until_all_locked (exec) {
		ret = drm_exec_lock_obj(exec, drm_gpuvm_resv_obj(vm));
		if (!ret && (obj->resv != drm_gpuvm_resv(vm)))
			ret = drm_exec_lock_obj(exec, obj);
		drm_exec_retry_on_contention(exec);
		if (GEM_WARN_ON(ret))
			break;
	}

	return ret;
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
	return drm_gem_is_imported(&msm_obj->base) || msm_obj->pin_count;
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
	struct drm_gpuvm *vm;
	struct list_head node;   /* node in ring submit list */
	struct drm_exec exec;
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
	bool bos_pinned : 1;
	bool fault_dumped:1;/* Limit devcoredump dumping to one per submit */
	bool in_rb : 1;     /* "sudo" mode, copy cmds into RB */
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
		uint32_t flags;
		union {
			struct drm_gem_object *obj;
			uint32_t handle;
		};
		struct drm_gpuvm_bo *vm_bo;
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

#endif /* __MSM_GEM_H__ */
