/* SPDX-License-Identifier: GPL-2.0 or MIT */
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */
/* Copyright 2023 Collabora ltd. */

#ifndef __PANTHOR_DEVICE_H__
#define __PANTHOR_DEVICE_H__

#include <linux/atomic.h>
#include <linux/io-pgtable.h>
#include <linux/regulator/consumer.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#include <drm/drm_device.h>
#include <drm/drm_mm.h>
#include <drm/gpu_scheduler.h>
#include <drm/panthor_drm.h>

struct panthor_csf;
struct panthor_csf_ctx;
struct panthor_device;
struct panthor_gpu;
struct panthor_group_pool;
struct panthor_heap_pool;
struct panthor_job;
struct panthor_mmu;
struct panthor_fw;
struct panthor_perfcnt;
struct panthor_vm;
struct panthor_vm_pool;

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

/**
 * struct panthor_irq - IRQ data
 *
 * Used to automate IRQ handling for the 3 different IRQs we have in this driver.
 */
struct panthor_irq {
	/** @ptdev: Panthor device */
	struct panthor_device *ptdev;

	/** @irq: IRQ number. */
	int irq;

	/** @mask: Current mask being applied to xxx_INT_MASK. */
	u32 mask;

	/** @suspended: Set to true when the IRQ is suspended. */
	atomic_t suspended;
};

/**
 * struct panthor_device - Panthor device
 */
struct panthor_device {
	/** @base: Base drm_device. */
	struct drm_device base;

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
	} pm;
};

/**
 * struct panthor_file - Panthor file
 */
struct panthor_file {
	/** @ptdev: Device attached to this file. */
	struct panthor_device *ptdev;

	/** @vms: VM pool attached to this file. */
	struct panthor_vm_pool *vms;

	/** @groups: Scheduling group pool attached to this file. */
	struct panthor_group_pool *groups;
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
#define PANTHOR_IRQ_HANDLER(__name, __reg_prefix, __handler)					\
static irqreturn_t panthor_ ## __name ## _irq_raw_handler(int irq, void *data)			\
{												\
	struct panthor_irq *pirq = data;							\
	struct panthor_device *ptdev = pirq->ptdev;						\
												\
	if (atomic_read(&pirq->suspended))							\
		return IRQ_NONE;								\
	if (!gpu_read(ptdev, __reg_prefix ## _INT_STAT))					\
		return IRQ_NONE;								\
												\
	gpu_write(ptdev, __reg_prefix ## _INT_MASK, 0);						\
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
		u32 status = gpu_read(ptdev, __reg_prefix ## _INT_RAWSTAT) & pirq->mask;	\
												\
		if (!status)									\
			break;									\
												\
		gpu_write(ptdev, __reg_prefix ## _INT_CLEAR, status);				\
												\
		__handler(ptdev, status);							\
		ret = IRQ_HANDLED;								\
	}											\
												\
	if (!atomic_read(&pirq->suspended))							\
		gpu_write(ptdev, __reg_prefix ## _INT_MASK, pirq->mask);			\
												\
	return ret;										\
}												\
												\
static inline void panthor_ ## __name ## _irq_suspend(struct panthor_irq *pirq)			\
{												\
	pirq->mask = 0;										\
	gpu_write(pirq->ptdev, __reg_prefix ## _INT_MASK, 0);					\
	synchronize_irq(pirq->irq);								\
	atomic_set(&pirq->suspended, true);							\
}												\
												\
static inline void panthor_ ## __name ## _irq_resume(struct panthor_irq *pirq, u32 mask)	\
{												\
	atomic_set(&pirq->suspended, false);							\
	pirq->mask = mask;									\
	gpu_write(pirq->ptdev, __reg_prefix ## _INT_CLEAR, mask);				\
	gpu_write(pirq->ptdev, __reg_prefix ## _INT_MASK, mask);				\
}												\
												\
static int panthor_request_ ## __name ## _irq(struct panthor_device *ptdev,			\
					      struct panthor_irq *pirq,				\
					      int irq, u32 mask)				\
{												\
	pirq->ptdev = ptdev;									\
	pirq->irq = irq;									\
	panthor_ ## __name ## _irq_resume(pirq, mask);						\
												\
	return devm_request_threaded_irq(ptdev->base.dev, irq,					\
					 panthor_ ## __name ## _irq_raw_handler,		\
					 panthor_ ## __name ## _irq_threaded_handler,		\
					 IRQF_SHARED, KBUILD_MODNAME "-" # __name,		\
					 pirq);							\
}

extern struct workqueue_struct *panthor_cleanup_wq;

#endif
