// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2015-2018 Broadcom */

#include <linux/reservation.h>
#include <linux/mm_types.h>
#include <drm/drmP.h>
#include <drm/drm_encoder.h>
#include <drm/drm_gem.h>
#include <drm/gpu_scheduler.h>

#define GMP_GRANULARITY (128 * 1024)

/* Enum for each of the V3D queues.  We maintain various queue
 * tracking as an array because at some point we'll want to support
 * the TFU (texture formatting unit) as another queue.
 */
enum v3d_queue {
	V3D_BIN,
	V3D_RENDER,
};

#define V3D_MAX_QUEUES (V3D_RENDER + 1)

struct v3d_queue_state {
	struct drm_gpu_scheduler sched;

	u64 fence_context;
	u64 emit_seqno;
};

struct v3d_dev {
	struct drm_device drm;

	/* Short representation (e.g. 33, 41) of the V3D tech version
	 * and revision.
	 */
	int ver;

	struct device *dev;
	struct platform_device *pdev;
	void __iomem *hub_regs;
	void __iomem *core_regs[3];
	void __iomem *bridge_regs;
	void __iomem *gca_regs;
	struct clk *clk;

	/* Virtual and DMA addresses of the single shared page table. */
	volatile u32 *pt;
	dma_addr_t pt_paddr;

	/* Virtual and DMA addresses of the MMU's scratch page.  When
	 * a read or write is invalid in the MMU, it will be
	 * redirected here.
	 */
	void *mmu_scratch;
	dma_addr_t mmu_scratch_paddr;

	/* Number of V3D cores. */
	u32 cores;

	/* Allocator managing the address space.  All units are in
	 * number of pages.
	 */
	struct drm_mm mm;
	spinlock_t mm_lock;

	struct work_struct overflow_mem_work;

	struct v3d_exec_info *bin_job;
	struct v3d_exec_info *render_job;

	struct v3d_queue_state queue[V3D_MAX_QUEUES];

	/* Spinlock used to synchronize the overflow memory
	 * management against bin job submission.
	 */
	spinlock_t job_lock;

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

	struct {
		u32 num_allocated;
		u32 pages_allocated;
	} bo_stats;
};

static inline struct v3d_dev *
to_v3d_dev(struct drm_device *dev)
{
	return (struct v3d_dev *)dev->dev_private;
}

/* The per-fd struct, which tracks the MMU mappings. */
struct v3d_file_priv {
	struct v3d_dev *v3d;

	struct drm_sched_entity sched_entity[V3D_MAX_QUEUES];
};

/* Tracks a mapping of a BO into a per-fd address space */
struct v3d_vma {
	struct v3d_page_table *pt;
	struct list_head list; /* entry in v3d_bo.vmas */
};

struct v3d_bo {
	struct drm_gem_object base;

	struct mutex lock;

	struct drm_mm_node node;

	u32 pages_refcount;
	struct page **pages;
	struct sg_table *sgt;
	void *vaddr;

	struct list_head vmas;    /* list of v3d_vma */

	/* List entry for the BO's position in
	 * v3d_exec_info->unref_list
	 */
	struct list_head unref_head;

	/* normally (resv == &_resv) except for imported bo's */
	struct reservation_object *resv;
	struct reservation_object _resv;
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

	struct v3d_exec_info *exec;

	/* An optional fence userspace can pass in for the job to depend on. */
	struct dma_fence *in_fence;

	/* v3d fence to be signaled by IRQ handler when the job is complete. */
	struct dma_fence *done_fence;

	/* GPU virtual addresses of the start/end of the CL job. */
	u32 start, end;

	u32 timedout_ctca, timedout_ctra;
};

struct v3d_exec_info {
	struct v3d_dev *v3d;

	struct v3d_job bin, render;

	/* Fence for when the scheduler considers the binner to be
	 * done, for render to depend on.
	 */
	struct dma_fence *bin_done_fence;

	struct kref refcount;

	/* This is the array of BOs that were looked up at the start of exec. */
	struct v3d_bo **bo;
	u32 bo_count;

	/* List of overflow BOs used in the job that need to be
	 * released once the job is complete.
	 */
	struct list_head unref_list;

	/* Submitted tile memory allocation start/size, tile state. */
	u32 qma, qms, qts;
};

/**
 * _wait_for - magic (register) wait macro
 *
 * Does the right thing for modeset paths when run under kdgb or similar atomic
 * contexts. Note that it's important that we check the condition again after
 * having timed out, since the timeout could be due to preemption or similar and
 * we've never had a chance to check the condition before the timeout.
 */
#define wait_for(COND, MS) ({ \
	unsigned long timeout__ = jiffies + msecs_to_jiffies(MS) + 1;	\
	int ret__ = 0;							\
	while (!(COND)) {						\
		if (time_after(jiffies, timeout__)) {			\
			if (!(COND))					\
				ret__ = -ETIMEDOUT;			\
			break;						\
		}							\
		msleep(1);					\
	}								\
	ret__;								\
})

static inline unsigned long nsecs_to_jiffies_timeout(const u64 n)
{
	/* nsecs_to_jiffies64() does not guard against overflow */
	if (NSEC_PER_SEC % HZ &&
	    div_u64(n, NSEC_PER_SEC) >= MAX_JIFFY_OFFSET / HZ)
		return MAX_JIFFY_OFFSET;

	return min_t(u64, MAX_JIFFY_OFFSET, nsecs_to_jiffies64(n) + 1);
}

/* v3d_bo.c */
void v3d_free_object(struct drm_gem_object *gem_obj);
struct v3d_bo *v3d_bo_create(struct drm_device *dev, struct drm_file *file_priv,
			     size_t size);
int v3d_create_bo_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int v3d_mmap_bo_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
int v3d_get_bo_offset_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
vm_fault_t v3d_gem_fault(struct vm_fault *vmf);
int v3d_mmap(struct file *filp, struct vm_area_struct *vma);
struct reservation_object *v3d_prime_res_obj(struct drm_gem_object *obj);
int v3d_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma);
struct sg_table *v3d_prime_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *v3d_prime_import_sg_table(struct drm_device *dev,
						 struct dma_buf_attachment *attach,
						 struct sg_table *sgt);

/* v3d_debugfs.c */
int v3d_debugfs_init(struct drm_minor *minor);

/* v3d_fence.c */
extern const struct dma_fence_ops v3d_fence_ops;
struct dma_fence *v3d_fence_create(struct v3d_dev *v3d, enum v3d_queue queue);

/* v3d_gem.c */
int v3d_gem_init(struct drm_device *dev);
void v3d_gem_destroy(struct drm_device *dev);
int v3d_submit_cl_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int v3d_wait_bo_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
void v3d_exec_put(struct v3d_exec_info *exec);
void v3d_reset(struct v3d_dev *v3d);
void v3d_invalidate_caches(struct v3d_dev *v3d);
void v3d_flush_caches(struct v3d_dev *v3d);

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
