// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2015-2018 Broadcom */

#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/spinlock_types.h>
#include <linux/workqueue.h>

#include <drm/drm_encoder.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/gpu_scheduler.h>

#include "uapi/drm/v3d_drm.h"

struct clk;
struct platform_device;
struct reset_control;

#define GMP_GRANULARITY (128 * 1024)

/* Enum for each of the V3D queues. */
enum v3d_queue {
	V3D_BIN,
	V3D_RENDER,
	V3D_TFU,
	V3D_CSD,
	V3D_CACHE_CLEAN,
};

#define V3D_MAX_QUEUES (V3D_CACHE_CLEAN + 1)

struct v3d_queue_state {
	struct drm_gpu_scheduler sched;

	u64 fence_context;
	u64 emit_seqno;
};

/* Performance monitor object. The perform lifetime is controlled by userspace
 * using perfmon related ioctls. A perfmon can be attached to a submit_cl
 * request, and when this is the case, HW perf counters will be activated just
 * before the submit_cl is submitted to the GPU and disabled when the job is
 * done. This way, only events related to a specific job will be counted.
 */
struct v3d_perfmon {
	/* Tracks the number of users of the perfmon, when this counter reaches
	 * zero the perfmon is destroyed.
	 */
	refcount_t refcnt;

	/* Protects perfmon stop, as it can be invoked from multiple places. */
	struct mutex lock;

	/* Number of counters activated in this perfmon instance
	 * (should be less than DRM_V3D_MAX_PERF_COUNTERS).
	 */
	u8 ncounters;

	/* Events counted by the HW perf counters. */
	u8 counters[DRM_V3D_MAX_PERF_COUNTERS];

	/* Storage for counter values. Counters are incremented by the
	 * HW perf counter values every time the perfmon is attached
	 * to a GPU job.  This way, perfmon users don't have to
	 * retrieve the results after each job if they want to track
	 * events covering several submissions.  Note that counter
	 * values can't be reset, but you can fake a reset by
	 * destroying the perfmon and creating a new one.
	 */
	u64 values[];
};

struct v3d_dev {
	struct drm_device drm;

	/* Short representation (e.g. 33, 41) of the V3D tech version
	 * and revision.
	 */
	int ver;
	bool single_irq_line;

	void __iomem *hub_regs;
	void __iomem *core_regs[3];
	void __iomem *bridge_regs;
	void __iomem *gca_regs;
	struct clk *clk;
	struct reset_control *reset;

	/* Virtual and DMA addresses of the single shared page table. */
	volatile u32 *pt;
	dma_addr_t pt_paddr;

	/* Virtual and DMA addresses of the MMU's scratch page.  When
	 * a read or write is invalid in the MMU, it will be
	 * redirected here.
	 */
	void *mmu_scratch;
	dma_addr_t mmu_scratch_paddr;
	/* virtual address bits from V3D to the MMU. */
	int va_width;

	/* Number of V3D cores. */
	u32 cores;

	/* Allocator managing the address space.  All units are in
	 * number of pages.
	 */
	struct drm_mm mm;
	spinlock_t mm_lock;

	struct work_struct overflow_mem_work;

	struct v3d_bin_job *bin_job;
	struct v3d_render_job *render_job;
	struct v3d_tfu_job *tfu_job;
	struct v3d_csd_job *csd_job;

	struct v3d_queue_state queue[V3D_MAX_QUEUES];

	/* Spinlock used to synchronize the overflow memory
	 * management against bin job submission.
	 */
	spinlock_t job_lock;

	/* Used to track the active perfmon if any. */
	struct v3d_perfmon *active_perfmon;

	/* Protects bo_stats */
	struct mutex bo_lock;

	/* Lock taken when resetting the GPU, to keep multiple
	 * processes from trying to park the scheduler threads and
	 * reset at once.
	 */
	struct mutex reset_lock;

	/* Lock taken when creating and pushing the GPU scheduler
	 * jobs, to keep the sched-fence seqnos in order.
	 */
	struct mutex sched_lock;

	/* Lock taken during a cache clean and when initiating an L2
	 * flush, to keep L2 flushes from interfering with the
	 * synchronous L2 cleans.
	 */
	struct mutex cache_clean_lock;

	struct {
		u32 num_allocated;
		u32 pages_allocated;
	} bo_stats;
};

static inline struct v3d_dev *
to_v3d_dev(struct drm_device *dev)
{
	return container_of(dev, struct v3d_dev, drm);
}

static inline bool
v3d_has_csd(struct v3d_dev *v3d)
{
	return v3d->ver >= 41;
}

#define v3d_to_pdev(v3d) to_platform_device((v3d)->drm.dev)

/* The per-fd struct, which tracks the MMU mappings. */
struct v3d_file_priv {
	struct v3d_dev *v3d;

	struct {
		struct idr idr;
		struct mutex lock;
	} perfmon;

	struct drm_sched_entity sched_entity[V3D_MAX_QUEUES];
};

struct v3d_bo {
	struct drm_gem_shmem_object base;

	struct drm_mm_node node;

	/* List entry for the BO's position in
	 * v3d_render_job->unref_list
	 */
	struct list_head unref_head;
};

static inline struct v3d_bo *
to_v3d_bo(struct drm_gem_object *bo)
{
	return (struct v3d_bo *)bo;
}

struct v3d_fence {
	struct dma_fence base;
	struct drm_device *dev;
	/* v3d seqno for signaled() test */
	u64 seqno;
	enum v3d_queue queue;
};

static inline struct v3d_fence *
to_v3d_fence(struct dma_fence *fence)
{
	return (struct v3d_fence *)fence;
}

#define V3D_READ(offset) readl(v3d->hub_regs + offset)
#define V3D_WRITE(offset, val) writel(val, v3d->hub_regs + offset)

#define V3D_BRIDGE_READ(offset) readl(v3d->bridge_regs + offset)
#define V3D_BRIDGE_WRITE(offset, val) writel(val, v3d->bridge_regs + offset)

#define V3D_GCA_READ(offset) readl(v3d->gca_regs + offset)
#define V3D_GCA_WRITE(offset, val) writel(val, v3d->gca_regs + offset)

#define V3D_CORE_READ(core, offset) readl(v3d->core_regs[core] + offset)
#define V3D_CORE_WRITE(core, offset, val) writel(val, v3d->core_regs[core] + offset)

struct v3d_job {
	struct drm_sched_job base;

	struct kref refcount;

	struct v3d_dev *v3d;

	/* This is the array of BOs that were looked up at the start
	 * of submission.
	 */
	struct drm_gem_object **bo;
	u32 bo_count;

	/* Array of struct dma_fence * to block on before submitting this job.
	 */
	struct xarray deps;
	unsigned long last_dep;

	/* v3d fence to be signaled by IRQ handler when the job is complete. */
	struct dma_fence *irq_fence;

	/* scheduler fence for when the job is considered complete and
	 * the BO reservations can be released.
	 */
	struct dma_fence *done_fence;

	/* Pointer to a performance monitor object if the user requested it,
	 * NULL otherwise.
	 */
	struct v3d_perfmon *perfmon;

	/* Callback for the freeing of the job on refcount going to 0. */
	void (*free)(struct kref *ref);
};

struct v3d_bin_job {
	struct v3d_job base;

	/* GPU virtual addresses of the start/end of the CL job. */
	u32 start, end;

	u32 timedout_ctca, timedout_ctra;

	/* Corresponding render job, for attaching our overflow memory. */
	struct v3d_render_job *render;

	/* Submitted tile memory allocation start/size, tile state. */
	u32 qma, qms, qts;
};

struct v3d_render_job {
	struct v3d_job base;

	/* GPU virtual addresses of the start/end of the CL job. */
	u32 start, end;

	u32 timedout_ctca, timedout_ctra;

	/* List of overflow BOs used in the job that need to be
	 * released once the job is complete.
	 */
	struct list_head unref_list;
};

struct v3d_tfu_job {
	struct v3d_job base;

	struct drm_v3d_submit_tfu args;
};

struct v3d_csd_job {
	struct v3d_job base;

	u32 timedout_batches;

	struct drm_v3d_submit_csd args;
};

/**
 * __wait_for - magic wait macro
 *
 * Macro to help avoid open coding check/wait/timeout patterns. Note that it's
 * important that we check the condition again after having timed out, since the
 * timeout could be due to preemption or similar and we've never had a chance to
 * check the condition before the timeout.
 */
#define __wait_for(OP, COND, US, Wmin, Wmax) ({ \
	const ktime_t end__ = ktime_add_ns(ktime_get_raw(), 1000ll * (US)); \
	long wait__ = (Wmin); /* recommended min for usleep is 10 us */	\
	int ret__;							\
	might_sleep();							\
	for (;;) {							\
		const bool expired__ = ktime_after(ktime_get_raw(), end__); \
		OP;							\
		/* Guarantee COND check prior to timeout */		\
		barrier();						\
		if (COND) {						\
			ret__ = 0;					\
			break;						\
		}							\
		if (expired__) {					\
			ret__ = -ETIMEDOUT;				\
			break;						\
		}							\
		usleep_range(wait__, wait__ * 2);			\
		if (wait__ < (Wmax))					\
			wait__ <<= 1;					\
	}								\
	ret__;								\
})

#define _wait_for(COND, US, Wmin, Wmax)	__wait_for(, (COND), (US), (Wmin), \
						   (Wmax))
#define wait_for(COND, MS)		_wait_for((COND), (MS) * 1000, 10, 1000)

static inline unsigned long nsecs_to_jiffies_timeout(const u64 n)
{
	/* nsecs_to_jiffies64() does not guard against overflow */
	if (NSEC_PER_SEC % HZ &&
	    div_u64(n, NSEC_PER_SEC) >= MAX_JIFFY_OFFSET / HZ)
		return MAX_JIFFY_OFFSET;

	return min_t(u64, MAX_JIFFY_OFFSET, nsecs_to_jiffies64(n) + 1);
}

/* v3d_bo.c */
struct drm_gem_object *v3d_create_object(struct drm_device *dev, size_t size);
void v3d_free_object(struct drm_gem_object *gem_obj);
struct v3d_bo *v3d_bo_create(struct drm_device *dev, struct drm_file *file_priv,
			     size_t size);
int v3d_create_bo_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int v3d_mmap_bo_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
int v3d_get_bo_offset_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
struct drm_gem_object *v3d_prime_import_sg_table(struct drm_device *dev,
						 struct dma_buf_attachment *attach,
						 struct sg_table *sgt);

/* v3d_debugfs.c */
void v3d_debugfs_init(struct drm_minor *minor);

/* v3d_fence.c */
extern const struct dma_fence_ops v3d_fence_ops;
struct dma_fence *v3d_fence_create(struct v3d_dev *v3d, enum v3d_queue queue);

/* v3d_gem.c */
int v3d_gem_init(struct drm_device *dev);
void v3d_gem_destroy(struct drm_device *dev);
int v3d_submit_cl_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int v3d_submit_tfu_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int v3d_submit_csd_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int v3d_wait_bo_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
void v3d_job_put(struct v3d_job *job);
void v3d_reset(struct v3d_dev *v3d);
void v3d_invalidate_caches(struct v3d_dev *v3d);
void v3d_clean_caches(struct v3d_dev *v3d);

/* v3d_irq.c */
int v3d_irq_init(struct v3d_dev *v3d);
void v3d_irq_enable(struct v3d_dev *v3d);
void v3d_irq_disable(struct v3d_dev *v3d);
void v3d_irq_reset(struct v3d_dev *v3d);

/* v3d_mmu.c */
int v3d_mmu_get_offset(struct drm_file *file_priv, struct v3d_bo *bo,
		       u32 *offset);
int v3d_mmu_set_page_table(struct v3d_dev *v3d);
void v3d_mmu_insert_ptes(struct v3d_bo *bo);
void v3d_mmu_remove_ptes(struct v3d_bo *bo);

/* v3d_sched.c */
int v3d_sched_init(struct v3d_dev *v3d);
void v3d_sched_fini(struct v3d_dev *v3d);

/* v3d_perfmon.c */
void v3d_perfmon_get(struct v3d_perfmon *perfmon);
void v3d_perfmon_put(struct v3d_perfmon *perfmon);
void v3d_perfmon_start(struct v3d_dev *v3d, struct v3d_perfmon *perfmon);
void v3d_perfmon_stop(struct v3d_dev *v3d, struct v3d_perfmon *perfmon,
		      bool capture);
struct v3d_perfmon *v3d_perfmon_find(struct v3d_file_priv *v3d_priv, int id);
void v3d_perfmon_open_file(struct v3d_file_priv *v3d_priv);
void v3d_perfmon_close_file(struct v3d_file_priv *v3d_priv);
int v3d_perfmon_create_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv);
int v3d_perfmon_destroy_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
int v3d_perfmon_get_values_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
