/* SPDX-License-Identifier: GPL-2.0 or MIT */
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */
/* Copyright 2023 Collabora ltd. */

#ifndef __PANTHOR_DEVICE_H__
#define __PANTHOR_DEVICE_H__

#include <linux/atomic.h>
#include <linux/io-pgtable.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#include <drm/drm_device.h>
#include <drm/drm_gem.h>
#include <drm/drm_mm.h>
#include <drm/gpu_scheduler.h>
#include <drm/panthor_drm.h>

struct panthor_csf;
struct panthor_csf_ctx;
struct panthor_device;
struct panthor_gpu;
struct panthor_group_pool;
struct panthor_heap_pool;
struct panthor_hw;
struct panthor_job;
struct panthor_mmu;
struct panthor_fw;
struct panthor_perfcnt;
struct panthor_pwr;
struct panthor_vm;
struct panthor_vm_pool;

/**
 * struct panthor_soc_data - Panthor SoC Data
 */
struct panthor_soc_data {
	/** @asn_hash_enable: True if GPU_L2_CONFIG_ASN_HASH_ENABLE must be set. */
	bool asn_hash_enable;

	/** @asn_hash: ASN_HASH values when asn_hash_enable is true. */
	u32 asn_hash[3];
};

/**
 * enum panthor_device_pm_state - PM state
 */
enum panthor_device_pm_state {
	/** @PANTHOR_DEVICE_PM_STATE_SUSPENDED: Device is suspended. */
	PANTHOR_DEVICE_PM_STATE_SUSPENDED = 0,

	/** @PANTHOR_DEVICE_PM_STATE_RESUMING: Device is being resumed. */
	PANTHOR_DEVICE_PM_STATE_RESUMING,

	/** @PANTHOR_DEVICE_PM_STATE_ACTIVE: Device is active. */
	PANTHOR_DEVICE_PM_STATE_ACTIVE,

	/** @PANTHOR_DEVICE_PM_STATE_SUSPENDING: Device is being suspended. */
	PANTHOR_DEVICE_PM_STATE_SUSPENDING,
};

enum panthor_irq_state {
	/** @PANTHOR_IRQ_STATE_ACTIVE: IRQ is active and ready to process events. */
	PANTHOR_IRQ_STATE_ACTIVE = 0,
	/** @PANTHOR_IRQ_STATE_PROCESSING: IRQ is currently processing events. */
	PANTHOR_IRQ_STATE_PROCESSING,
	/** @PANTHOR_IRQ_STATE_SUSPENDED: IRQ is suspended. */
	PANTHOR_IRQ_STATE_SUSPENDED,
	/** @PANTHOR_IRQ_STATE_SUSPENDING: IRQ is being suspended. */
	PANTHOR_IRQ_STATE_SUSPENDING,
};

/**
 * struct panthor_irq - IRQ data
 *
 * Used to automate IRQ handling for the 3 different IRQs we have in this driver.
 */
struct panthor_irq {
	/** @ptdev: Panthor device */
	struct panthor_device *ptdev;

	/** @iomem: CPU mapping of IRQ base address */
	void __iomem *iomem;

	/** @irq: IRQ number. */
	int irq;

	/** @mask: Values to write to xxx_INT_MASK if active. */
	u32 mask;

	/**
	 * @mask_lock: protects modifications to _INT_MASK and @mask.
	 *
	 * In paths where _INT_MASK is updated based on a state
	 * transition/check, it's crucial for the state update/check to be
	 * inside the locked section, otherwise it introduces a race window
	 * leading to potential _INT_MASK inconsistencies.
	 */
	spinlock_t mask_lock;

	/** @state: one of &enum panthor_irq_state reflecting the current state. */
	atomic_t state;
};

/**
 * enum panthor_device_profiling_mode - Profiling state
 */
enum panthor_device_profiling_flags {
	/** @PANTHOR_DEVICE_PROFILING_DISABLED: Profiling is disabled. */
	PANTHOR_DEVICE_PROFILING_DISABLED = 0,

	/** @PANTHOR_DEVICE_PROFILING_CYCLES: Sampling job cycles. */
	PANTHOR_DEVICE_PROFILING_CYCLES = BIT(0),

	/** @PANTHOR_DEVICE_PROFILING_TIMESTAMP: Sampling job timestamp. */
	PANTHOR_DEVICE_PROFILING_TIMESTAMP = BIT(1),

	/** @PANTHOR_DEVICE_PROFILING_ALL: Sampling everything. */
	PANTHOR_DEVICE_PROFILING_ALL =
	PANTHOR_DEVICE_PROFILING_CYCLES |
	PANTHOR_DEVICE_PROFILING_TIMESTAMP,
};

/**
 * struct panthor_device - Panthor device
 */
struct panthor_device {
	/** @base: Base drm_device. */
	struct drm_device base;

	/** @soc_data: Optional SoC data. */
	const struct panthor_soc_data *soc_data;

	/** @phys_addr: Physical address of the iomem region. */
	phys_addr_t phys_addr;

	/** @iomem: CPU mapping of the IOMEM region. */
	void __iomem *iomem;

	/** @clks: GPU clocks. */
	struct {
		/** @core: Core clock. */
		struct clk *core;

		/** @stacks: Stacks clock. This clock is optional. */
		struct clk *stacks;

		/** @coregroup: Core group clock. This clock is optional. */
		struct clk *coregroup;
	} clks;

	/** @coherent: True if the CPU/GPU are memory coherent. */
	bool coherent;

	/** @gpu_info: GPU information. */
	struct drm_panthor_gpu_info gpu_info;

	/** @csif_info: Command stream interface information. */
	struct drm_panthor_csif_info csif_info;

	/** @hw: GPU-specific data. */
	struct panthor_hw *hw;

	/** @pwr: Power control management data. */
	struct panthor_pwr *pwr;

	/** @gpu: GPU management data. */
	struct panthor_gpu *gpu;

	/** @fw: FW management data. */
	struct panthor_fw *fw;

	/** @mmu: MMU management data. */
	struct panthor_mmu *mmu;

	/** @scheduler: Scheduler management data. */
	struct panthor_scheduler *scheduler;

	/** @devfreq: Device frequency scaling management data. */
	struct panthor_devfreq *devfreq;

	/** @reclaim: Reclaim related stuff */
	struct {
		/** @reclaim.shrinker: Shrinker instance */
		struct shrinker *shrinker;

		/**
		 * @reclaim.unused: BOs with unused pages
		 *
		 * Basically all buffers that got mmapped, vmapped or GPU mapped and
		 * then unmapped. There should be no contention on these buffers,
		 * making them ideal to reclaim.
		 */
		struct drm_gem_lru unused;

		/**
		 * @reclaim.mmapped: mmap()-ed buffers
		 *
		 * Those are relatively easy to reclaim since we don't need user
		 * agreement, we can simply teardown the mapping and let it fault on
		 * the next access.
		 */
		struct drm_gem_lru mmapped;

		/**
		 * @reclaim.gpu_mapped_shared: shared BO LRU list
		 *
		 * That's the most tricky BO type to reclaim, because it involves
		 * tearing down all mappings in all VMs where this BO is mapped,
		 * which increases the risk of contention and thus decreases the
		 * likeliness of success.
		 */
		struct drm_gem_lru gpu_mapped_shared;

		/**
		 * @reclaim.vms: VM LRU list
		 *
		 * VMs that have reclaimable BOs only mapped to a single VM are placed
		 * in this LRU. Reclaiming such BOs implies waiting for VM idleness
		 * (no in-flight GPU jobs targeting this VM), meaning we can't reclaim
		 * those if we're in a context where we can't block/sleep.
		 */
		struct list_head vms;

		/**
		 * @reclaim.gpu_mapped_count: Global counter of pages that are GPU mapped
		 *
		 * Allows us to get the number of reclaimable pages without walking
		 * the vms and gpu_mapped_shared LRUs.
		 */
		long gpu_mapped_count;

		/**
		 * @reclaim.retry_count: Number of times we ran the shrinker without being
		 * able to reclaim stuff
		 *
		 * Used to stop scanning GEMs when too many attempts were made
		 * without progress.
		 */
		atomic_t retry_count;

#ifdef CONFIG_DEBUG_FS
		/**
		 * @reclaim.nr_pages_reclaimed_on_last_scan: Number of pages reclaimed on the last
		 * shrinker scan
		 */
		unsigned long nr_pages_reclaimed_on_last_scan;
#endif
	} reclaim;

	/** @unplug: Device unplug related fields. */
	struct {
		/** @lock: Lock used to serialize unplug operations. */
		struct mutex lock;

		/**
		 * @done: Completion object signaled when the unplug
		 * operation is done.
		 */
		struct completion done;
	} unplug;

	/** @reset: Reset related fields. */
	struct {
		/** @wq: Ordered worqueud used to schedule reset operations. */
		struct workqueue_struct *wq;

		/** @work: Reset work. */
		struct work_struct work;

		/** @pending: Set to true if a reset is pending. */
		atomic_t pending;

		/**
		 * @fast: True if the post_reset logic can proceed with a fast reset.
		 *
		 * A fast reset is just a reset where the driver doesn't reload the FW sections.
		 *
		 * Any time the firmware is properly suspended, a fast reset can take place.
		 * On the other hand, if the halt operation failed, the driver will reload
		 * all FW sections to make sure we start from a fresh state.
		 */
		bool fast;
	} reset;

	/** @pm: Power management related data. */
	struct {
		/** @state: Power state. */
		atomic_t state;

		/**
		 * @mmio_lock: Lock protecting MMIO userspace CPU mappings.
		 *
		 * This is needed to ensure we map the dummy IO pages when
		 * the device is being suspended, and the real IO pages when
		 * the device is being resumed. We can't just do with the
		 * state atomicity to deal with this race.
		 */
		struct mutex mmio_lock;

		/**
		 * @dummy_latest_flush: Dummy LATEST_FLUSH page.
		 *
		 * Used to replace the real LATEST_FLUSH page when the GPU
		 * is suspended.
		 */
		struct page *dummy_latest_flush;

		/** @recovery_needed: True when a resume attempt failed. */
		atomic_t recovery_needed;
	} pm;

	/** @profile_mask: User-set profiling flags for job accounting. */
	u32 profile_mask;

	/** @fast_rate: Maximum device clock frequency. Set by DVFS */
	unsigned long fast_rate;

#ifdef CONFIG_DEBUG_FS
	/** @gems: Device-wide list of GEM objects owned by at least one file. */
	struct {
		/** @gems.lock: Protects the device-wide list of GEM objects. */
		struct mutex lock;

		/** @node: Used to keep track of all the device's DRM objects */
		struct list_head node;
	} gems;
#endif
};

struct panthor_gpu_usage {
	u64 time;
	u64 cycles;
};

/**
 * struct panthor_file - Panthor file
 */
struct panthor_file {
	/** @ptdev: Device attached to this file. */
	struct panthor_device *ptdev;

	/** @user_mmio: User MMIO related fields. */
	struct {
		/**
		 * @offset: Offset used for user MMIO mappings.
		 *
		 * This offset should not be used to check the type of mapping
		 * except in panthor_mmap(). After that point, MMIO mapping
		 * offsets have been adjusted to match
		 * DRM_PANTHOR_USER_MMIO_OFFSET and that macro should be used
		 * instead.
		 * Make sure this rule is followed at all times, because
		 * userspace is in control of the offset, and can change the
		 * value behind our back. Otherwise it can lead to erroneous
		 * branching happening in kernel space.
		 */
		u64 offset;
	} user_mmio;

	/** @vms: VM pool attached to this file. */
	struct panthor_vm_pool *vms;

	/** @groups: Scheduling group pool attached to this file. */
	struct panthor_group_pool *groups;

	/** @stats: cycle and timestamp measures for job execution. */
	struct panthor_gpu_usage stats;
};

int panthor_device_init(struct panthor_device *ptdev);
void panthor_device_unplug(struct panthor_device *ptdev);

/**
 * panthor_device_schedule_reset() - Schedules a reset operation
 */
static inline void panthor_device_schedule_reset(struct panthor_device *ptdev)
{
	if (!atomic_cmpxchg(&ptdev->reset.pending, 0, 1) &&
	    atomic_read(&ptdev->pm.state) == PANTHOR_DEVICE_PM_STATE_ACTIVE)
		queue_work(ptdev->reset.wq, &ptdev->reset.work);
}

/**
 * panthor_device_reset_is_pending() - Checks if a reset is pending.
 *
 * Return: true if a reset is pending, false otherwise.
 */
static inline bool panthor_device_reset_is_pending(struct panthor_device *ptdev)
{
	return atomic_read(&ptdev->reset.pending) != 0;
}

int panthor_device_mmap_io(struct panthor_device *ptdev,
			   struct vm_area_struct *vma);

int panthor_device_resume(struct device *dev);
int panthor_device_suspend(struct device *dev);

static inline int panthor_device_resume_and_get(struct panthor_device *ptdev)
{
	int ret = pm_runtime_resume_and_get(ptdev->base.dev);

	/* If the resume failed, we need to clear the runtime_error, which
	 * can done by forcing the RPM state to suspended. If multiple
	 * threads called panthor_device_resume_and_get(), we only want
	 * one of them to update the state, hence the cmpxchg. Note that a
	 * thread might enter panthor_device_resume_and_get() and call
	 * pm_runtime_resume_and_get() after another thread had attempted
	 * to resume and failed. This means we will end up with an error
	 * without even attempting a resume ourselves. The only risk here
	 * is to report an error when the second resume attempt might have
	 * succeeded. Given resume errors are not expected, this is probably
	 * something we can live with.
	 */
	if (ret && atomic_cmpxchg(&ptdev->pm.recovery_needed, 1, 0) == 1)
		pm_runtime_set_suspended(ptdev->base.dev);

	return ret;
}

enum drm_panthor_exception_type {
	DRM_PANTHOR_EXCEPTION_OK = 0x00,
	DRM_PANTHOR_EXCEPTION_TERMINATED = 0x04,
	DRM_PANTHOR_EXCEPTION_KABOOM = 0x05,
	DRM_PANTHOR_EXCEPTION_EUREKA = 0x06,
	DRM_PANTHOR_EXCEPTION_ACTIVE = 0x08,
	DRM_PANTHOR_EXCEPTION_CS_RES_TERM = 0x0f,
	DRM_PANTHOR_EXCEPTION_MAX_NON_FAULT = 0x3f,
	DRM_PANTHOR_EXCEPTION_CS_CONFIG_FAULT = 0x40,
	DRM_PANTHOR_EXCEPTION_CS_UNRECOVERABLE = 0x41,
	DRM_PANTHOR_EXCEPTION_CS_ENDPOINT_FAULT = 0x44,
	DRM_PANTHOR_EXCEPTION_CS_BUS_FAULT = 0x48,
	DRM_PANTHOR_EXCEPTION_CS_INSTR_INVALID = 0x49,
	DRM_PANTHOR_EXCEPTION_CS_CALL_STACK_OVERFLOW = 0x4a,
	DRM_PANTHOR_EXCEPTION_CS_INHERIT_FAULT = 0x4b,
	DRM_PANTHOR_EXCEPTION_INSTR_INVALID_PC = 0x50,
	DRM_PANTHOR_EXCEPTION_INSTR_INVALID_ENC = 0x51,
	DRM_PANTHOR_EXCEPTION_INSTR_BARRIER_FAULT = 0x55,
	DRM_PANTHOR_EXCEPTION_DATA_INVALID_FAULT = 0x58,
	DRM_PANTHOR_EXCEPTION_TILE_RANGE_FAULT = 0x59,
	DRM_PANTHOR_EXCEPTION_ADDR_RANGE_FAULT = 0x5a,
	DRM_PANTHOR_EXCEPTION_IMPRECISE_FAULT = 0x5b,
	DRM_PANTHOR_EXCEPTION_OOM = 0x60,
	DRM_PANTHOR_EXCEPTION_CSF_FW_INTERNAL_ERROR = 0x68,
	DRM_PANTHOR_EXCEPTION_CSF_RES_EVICTION_TIMEOUT = 0x69,
	DRM_PANTHOR_EXCEPTION_GPU_BUS_FAULT = 0x80,
	DRM_PANTHOR_EXCEPTION_GPU_SHAREABILITY_FAULT = 0x88,
	DRM_PANTHOR_EXCEPTION_SYS_SHAREABILITY_FAULT = 0x89,
	DRM_PANTHOR_EXCEPTION_GPU_CACHEABILITY_FAULT = 0x8a,
	DRM_PANTHOR_EXCEPTION_TRANSLATION_FAULT_0 = 0xc0,
	DRM_PANTHOR_EXCEPTION_TRANSLATION_FAULT_1 = 0xc1,
	DRM_PANTHOR_EXCEPTION_TRANSLATION_FAULT_2 = 0xc2,
	DRM_PANTHOR_EXCEPTION_TRANSLATION_FAULT_3 = 0xc3,
	DRM_PANTHOR_EXCEPTION_TRANSLATION_FAULT_4 = 0xc4,
	DRM_PANTHOR_EXCEPTION_PERM_FAULT_0 = 0xc8,
	DRM_PANTHOR_EXCEPTION_PERM_FAULT_1 = 0xc9,
	DRM_PANTHOR_EXCEPTION_PERM_FAULT_2 = 0xca,
	DRM_PANTHOR_EXCEPTION_PERM_FAULT_3 = 0xcb,
	DRM_PANTHOR_EXCEPTION_ACCESS_FLAG_1 = 0xd9,
	DRM_PANTHOR_EXCEPTION_ACCESS_FLAG_2 = 0xda,
	DRM_PANTHOR_EXCEPTION_ACCESS_FLAG_3 = 0xdb,
	DRM_PANTHOR_EXCEPTION_ADDR_SIZE_FAULT_IN = 0xe0,
	DRM_PANTHOR_EXCEPTION_ADDR_SIZE_FAULT_OUT0 = 0xe4,
	DRM_PANTHOR_EXCEPTION_ADDR_SIZE_FAULT_OUT1 = 0xe5,
	DRM_PANTHOR_EXCEPTION_ADDR_SIZE_FAULT_OUT2 = 0xe6,
	DRM_PANTHOR_EXCEPTION_ADDR_SIZE_FAULT_OUT3 = 0xe7,
	DRM_PANTHOR_EXCEPTION_MEM_ATTR_FAULT_0 = 0xe8,
	DRM_PANTHOR_EXCEPTION_MEM_ATTR_FAULT_1 = 0xe9,
	DRM_PANTHOR_EXCEPTION_MEM_ATTR_FAULT_2 = 0xea,
	DRM_PANTHOR_EXCEPTION_MEM_ATTR_FAULT_3 = 0xeb,
};

/**
 * panthor_exception_is_fault() - Checks if an exception is a fault.
 *
 * Return: true if the exception is a fault, false otherwise.
 */
static inline bool
panthor_exception_is_fault(u32 exception_code)
{
	return exception_code > DRM_PANTHOR_EXCEPTION_MAX_NON_FAULT;
}

const char *panthor_exception_name(struct panthor_device *ptdev,
				   u32 exception_code);

#define INT_RAWSTAT 0x0
#define INT_CLEAR   0x4
#define INT_MASK    0x8
#define INT_STAT    0xc

/**
 * PANTHOR_IRQ_HANDLER() - Define interrupt handlers and the interrupt
 * registration function.
 *
 * The boiler-plate to gracefully deal with shared interrupts is
 * auto-generated. All you have to do is call PANTHOR_IRQ_HANDLER()
 * just after the actual handler. The handler prototype is:
 *
 * void (*handler)(struct panthor_device *, u32 status);
 */
#define PANTHOR_IRQ_HANDLER(__name, __handler)							\
static irqreturn_t panthor_ ## __name ## _irq_raw_handler(int irq, void *data)			\
{												\
	struct panthor_irq *pirq = data;							\
	enum panthor_irq_state old_state;							\
												\
	guard(spinlock_irqsave)(&pirq->mask_lock);						\
	old_state = atomic_cmpxchg(&pirq->state,						\
				   PANTHOR_IRQ_STATE_ACTIVE,					\
				   PANTHOR_IRQ_STATE_PROCESSING);				\
	if (old_state != PANTHOR_IRQ_STATE_ACTIVE)						\
		return IRQ_NONE;								\
												\
	if (!gpu_read(pirq->iomem, INT_STAT)) {							\
		atomic_cmpxchg(&pirq->state,							\
			       PANTHOR_IRQ_STATE_PROCESSING,					\
			       PANTHOR_IRQ_STATE_ACTIVE);					\
		return IRQ_NONE;								\
	}											\
												\
	gpu_write(pirq->iomem, INT_MASK, 0);							\
	return IRQ_WAKE_THREAD;									\
}												\
												\
static irqreturn_t panthor_ ## __name ## _irq_threaded_handler(int irq, void *data)		\
{												\
	struct panthor_irq *pirq = data;							\
	struct panthor_device *ptdev = pirq->ptdev;						\
	irqreturn_t ret = IRQ_NONE;								\
												\
	while (true) {										\
		/* It's safe to access pirq->mask without the lock held here. If a new		\
		 * event gets added to the mask and the corresponding IRQ is pending,		\
		 * we'll process it right away instead of adding an extra raw -> threaded	\
		 * round trip. If an event is removed and the status bit is set, it will	\
		 * be ignored, just like it would have been if the mask had been adjusted	\
		 * right before the HW event kicks in. TLDR; it's all expected races we're	\
		 * covered for.									\
		 */										\
		u32 status = gpu_read(pirq->iomem, INT_RAWSTAT) & pirq->mask;			\
												\
		if (!status)									\
			break;									\
												\
		__handler(ptdev, status);							\
		ret = IRQ_HANDLED;								\
	}											\
												\
	scoped_guard(spinlock_irqsave, &pirq->mask_lock) {					\
		enum panthor_irq_state old_state;						\
												\
		old_state = atomic_cmpxchg(&pirq->state,					\
					   PANTHOR_IRQ_STATE_PROCESSING,			\
					   PANTHOR_IRQ_STATE_ACTIVE);				\
		if (old_state == PANTHOR_IRQ_STATE_PROCESSING)					\
			gpu_write(pirq->iomem, INT_MASK, pirq->mask);				\
	}											\
												\
	return ret;										\
}												\
												\
static inline void panthor_ ## __name ## _irq_suspend(struct panthor_irq *pirq)			\
{												\
	scoped_guard(spinlock_irqsave, &pirq->mask_lock) {					\
		atomic_set(&pirq->state, PANTHOR_IRQ_STATE_SUSPENDING);				\
		gpu_write(pirq->iomem, INT_MASK, 0);						\
	}											\
	synchronize_irq(pirq->irq);								\
	atomic_set(&pirq->state, PANTHOR_IRQ_STATE_SUSPENDED);					\
}												\
												\
static inline void panthor_ ## __name ## _irq_resume(struct panthor_irq *pirq)			\
{												\
	guard(spinlock_irqsave)(&pirq->mask_lock);						\
												\
	atomic_set(&pirq->state, PANTHOR_IRQ_STATE_ACTIVE);					\
	gpu_write(pirq->iomem, INT_CLEAR, pirq->mask);						\
	gpu_write(pirq->iomem, INT_MASK, pirq->mask);						\
}												\
												\
static int panthor_request_ ## __name ## _irq(struct panthor_device *ptdev,			\
					      struct panthor_irq *pirq,				\
					      int irq, void __iomem *iomem)			\
{												\
	pirq->ptdev = ptdev;									\
	pirq->irq = irq;									\
	pirq->mask = 0;										\
	pirq->iomem = iomem;									\
	spin_lock_init(&pirq->mask_lock);							\
	atomic_set(&pirq->state, PANTHOR_IRQ_STATE_SUSPENDED);					\
	gpu_write(pirq->iomem, INT_MASK, 0);							\
												\
	return devm_request_threaded_irq(ptdev->base.dev, irq,					\
					 panthor_ ## __name ## _irq_raw_handler,		\
					 panthor_ ## __name ## _irq_threaded_handler,		\
					 IRQF_SHARED, KBUILD_MODNAME "-" # __name,		\
					 pirq);							\
}												\
												\
static inline void panthor_ ## __name ## _irq_enable_events(struct panthor_irq *pirq, u32 mask)	\
{												\
	guard(spinlock_irqsave)(&pirq->mask_lock);						\
	pirq->mask |= mask;									\
												\
	/* The only situation where we need to write the new mask is if the IRQ is active.	\
	 * If it's being processed, the mask will be restored for us in _irq_threaded_handler()	\
	 * on the PROCESSING -> ACTIVE transition.						\
	 * If the IRQ is suspended/suspending, the mask is restored at resume time.		\
	 */											\
	if (atomic_read(&pirq->state) == PANTHOR_IRQ_STATE_ACTIVE)				\
		gpu_write(pirq->iomem, INT_MASK, pirq->mask);					\
}												\
												\
static inline void panthor_ ## __name ## _irq_disable_events(struct panthor_irq *pirq, u32 mask)\
{												\
	guard(spinlock_irqsave)(&pirq->mask_lock);						\
	pirq->mask &= ~mask;									\
												\
	/* The only situation where we need to write the new mask is if the IRQ is active.	\
	 * If it's being processed, the mask will be restored for us in _irq_threaded_handler()	\
	 * on the PROCESSING -> ACTIVE transition.						\
	 * If the IRQ is suspended/suspending, the mask is restored at resume time.		\
	 */											\
	if (atomic_read(&pirq->state) == PANTHOR_IRQ_STATE_ACTIVE)				\
		gpu_write(pirq->iomem, INT_MASK, pirq->mask);					\
}

extern struct workqueue_struct *panthor_cleanup_wq;

static inline void gpu_write(void __iomem *iomem, u32 reg, u32 data)
{
	writel(data, iomem + reg);
}

static inline u32 gpu_read(void __iomem *iomem, u32 reg)
{
	return readl(iomem + reg);
}

static inline u32 gpu_read_relaxed(void __iomem *iomem, u32 reg)
{
	return readl_relaxed(iomem + reg);
}

static inline void gpu_write64(void __iomem *iomem, u32 reg, u64 data)
{
	gpu_write(iomem, reg, lower_32_bits(data));
	gpu_write(iomem, reg + 4, upper_32_bits(data));
}

static inline u64 gpu_read64(void __iomem *iomem, u32 reg)
{
	return (gpu_read(iomem, reg) | ((u64)gpu_read(iomem, reg + 4) << 32));
}

static inline u64 gpu_read64_relaxed(void __iomem *iomem, u32 reg)
{
	return (gpu_read_relaxed(iomem, reg) |
		((u64)gpu_read_relaxed(iomem, reg + 4) << 32));
}

static inline u64 gpu_read64_counter(void __iomem *iomem, u32 reg)
{
	u32 lo, hi1, hi2;
	do {
		hi1 = gpu_read(iomem, reg + 4);
		lo = gpu_read(iomem, reg);
		hi2 = gpu_read(iomem, reg + 4);
	} while (hi1 != hi2);
	return lo | ((u64)hi2 << 32);
}

#define gpu_read_poll_timeout(iomem, reg, val, cond, delay_us, timeout_us)	\
	read_poll_timeout(gpu_read, val, cond, delay_us, timeout_us, false,	\
			  iomem, reg)

#define gpu_read_poll_timeout_atomic(iomem, reg, val, cond, delay_us,		\
				     timeout_us)				\
	read_poll_timeout_atomic(gpu_read, val, cond, delay_us, timeout_us,	\
				 false, iomem, reg)

#define gpu_read64_poll_timeout(iomem, reg, val, cond, delay_us, timeout_us)	\
	read_poll_timeout(gpu_read64, val, cond, delay_us, timeout_us, false,	\
			  iomem, reg)

#define gpu_read64_poll_timeout_atomic(iomem, reg, val, cond, delay_us,		\
				       timeout_us)				\
	read_poll_timeout_atomic(gpu_read64, val, cond, delay_us, timeout_us,	\
				 false, iomem, reg)

#define gpu_read_relaxed_poll_timeout_atomic(iomem, reg, val, cond, delay_us,	\
					     timeout_us)			\
	read_poll_timeout_atomic(gpu_read_relaxed, val, cond, delay_us,		\
				 timeout_us, false, iomem, reg)

#define gpu_read64_relaxed_poll_timeout(iomem, reg, val, cond, delay_us,	\
					timeout_us)				\
	read_poll_timeout(gpu_read64_relaxed, val, cond, delay_us, timeout_us,	\
			  false, iomem, reg)

#endif
