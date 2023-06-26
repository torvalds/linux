/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#ifndef __LINUX_RGA_DRV_H_
#define __LINUX_RGA_DRV_H_

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/fdtable.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/regulator/consumer.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/syscalls.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/pm_runtime.h>
#include <linux/sched/mm.h>
#include <linux/string_helpers.h>

#include <asm/cacheflush.h>

#include <linux/iommu.h>
#include <linux/iova.h>
#include <linux/pagemap.h>

#ifdef CONFIG_DMABUF_CACHE
#include <linux/dma-buf-cache.h>
#else
#include <linux/dma-buf.h>
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#include <linux/dma-map-ops.h>
#endif

#include <linux/hrtimer.h>

#include "rga.h"

#define RGA_CORE_REG_OFFSET 0x10000

/* load interval: 1000ms */
#define RGA_LOAD_INTERVAL_US 1000000

/* timer interval: 1000ms */
#define RGA_TIMER_INTERVAL_NS 1000000000

#if ((defined(CONFIG_RK_IOMMU) || defined(CONFIG_ROCKCHIP_IOMMU)) \
	&& defined(CONFIG_ION_ROCKCHIP))
#define CONFIG_RGA_IOMMU
#endif

/* Driver information */
#define DRIVER_DESC		"RGA multicore Device Driver"
#define DRIVER_NAME		"rga_multicore"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define DRIVER_MAJOR_VERISON		1
#define DRIVER_MINOR_VERSION		2
#define DRIVER_REVISION_VERSION		26
#define DRIVER_PATCH_VERSION

#define DRIVER_VERSION (STR(DRIVER_MAJOR_VERISON) "." STR(DRIVER_MINOR_VERSION) \
			"." STR(DRIVER_REVISION_VERSION) STR(DRIVER_PATCH_VERSION))

/* time limit */
#define RGA_JOB_TIMEOUT_DELAY		HZ
#define RGA_RESET_TIMEOUT			1000

#define RGA_MAX_SCHEDULER	3
#define RGA_MAX_BUS_CLK		10

#define RGA_BUFFER_POOL_MAX_SIZE	64

#ifndef ABS
#define ABS(X)			 (((X) < 0) ? (-(X)) : (X))
#endif

#ifndef CLIP
#define CLIP(x, a, b)	 (((x) < (a)) \
	? (a) : (((x) > (b)) ? (b) : (x)))
#endif

extern struct rga_drvdata_t *rga_drvdata;

enum {
	RGA3_SCHEDULER_CORE0		= 1 << 0,
	RGA3_SCHEDULER_CORE1		= 1 << 1,
	RGA2_SCHEDULER_CORE0		= 1 << 2,
	RGA_CORE_MASK			 = 0x7,
	RGA_NONE_CORE			 = 0x0,
};

enum {
	RGA_CMD_SLAVE		= 1,
	RGA_CMD_MASTER		= 2,
};

enum iommu_dma_cookie_type {
	IOMMU_DMA_IOVA_COOKIE,
	IOMMU_DMA_MSI_COOKIE,
};

enum rga_scheduler_status {
	RGA_SCHEDULER_IDLE = 0,
	RGA_SCHEDULER_WORKING,
	RGA_SCHEDULER_ABORT,
};

enum rga_job_state {
	RGA_JOB_STATE_PENDING = 0,
	RGA_JOB_STATE_PREPARE,
	RGA_JOB_STATE_RUNNING,
	RGA_JOB_STATE_FINISH,
	RGA_JOB_STATE_DONE,
	RGA_JOB_STATE_INTR_ERR,
	RGA_JOB_STATE_HW_TIMEOUT,
	RGA_JOB_STATE_ABORT,
};

struct rga_iommu_dma_cookie {
	enum iommu_dma_cookie_type  type;

	/* Full allocator for IOMMU_DMA_IOVA_COOKIE */
	struct iova_domain  iovad;
};

struct rga_iommu_info {
	struct device *dev;
	struct device *default_dev;		/* for dma-buf_api */
	struct iommu_domain *domain;
	struct iommu_group *group;
};

struct rga_dma_buffer {
	/* DMABUF information */
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	void *vmap_ptr;

	struct iommu_domain *domain;

	enum dma_data_direction dir;

	dma_addr_t iova;
	unsigned long size;
	/*
	 * The offset of the first page of the sgt.
	 * Since alloc iova must be page aligned, the offset of the first page is
	 * identified separately.
	 */
	size_t offset;

	/* The scheduler of the mapping */
	struct rga_scheduler_t *scheduler;
};

struct rga_virt_addr {
	uint64_t addr;

	struct page **pages;
	int pages_order;
	int page_count;
	unsigned long size;

	/* The offset of the first page of the virtual address */
	size_t offset;

	int result;
};

struct rga_internal_buffer {
	/* DMA buffer */
	struct rga_dma_buffer *dma_buffer;

	/* virtual address */
	struct rga_virt_addr *virt_addr;

	/* physical address */
	uint64_t phys_addr;

	/* buffer size */
	unsigned long size;

	struct rga_memory_parm memory_parm;


	struct mm_struct *current_mm;

	/* memory type. */
	uint32_t type;

	uint32_t handle;

	uint32_t mm_flag;

	struct kref refcount;
	struct rga_session *session;
};

struct rga_scheduler_t;

struct rga_session {
	int id;

	pid_t tgid;

	char *pname;
};

struct rga_job_buffer {
	union {
		struct {
			struct rga_external_buffer *ex_y_addr;
			struct rga_external_buffer *ex_uv_addr;
			struct rga_external_buffer *ex_v_addr;
		};
		struct rga_external_buffer *ex_addr;
	};

	union {
		struct {
			struct rga_internal_buffer *y_addr;
			struct rga_internal_buffer *uv_addr;
			struct rga_internal_buffer *v_addr;
		};
		struct rga_internal_buffer *addr;
	};

	uint32_t *page_table;
	int order;
	int page_count;
};

struct rga_job {
	struct list_head head;

	struct rga_scheduler_t *scheduler;
	struct rga_session *session;

	struct rga_req rga_command_base;
	uint32_t cmd_reg[32 * 8];
	struct rga_full_csc full_csc;
	struct rga_pre_intr_info pre_intr_info;

	struct rga_job_buffer src_buffer;
	struct rga_job_buffer src1_buffer;
	struct rga_job_buffer dst_buffer;
	/* used by rga2 */
	struct rga_job_buffer els_buffer;

	/* for rga2 virtual_address */
	struct mm_struct *mm;

	/* job time stamp */
	ktime_t timestamp;
	/* The time when the job is actually executed on the hardware */
	ktime_t hw_running_time;
	/* The time only for hrtimer to calculate the load */
	ktime_t hw_recoder_time;
	unsigned int flags;
	int request_id;
	int priority;
	int core;
	int ret;
	pid_t pid;
	bool use_batch_mode;

	struct kref refcount;
	unsigned long state;
	uint32_t intr_status;
	uint32_t hw_status;
	uint32_t cmd_status;
};

struct rga_backend_ops {
	int (*get_version)(struct rga_scheduler_t *scheduler);
	int (*set_reg)(struct rga_job *job, struct rga_scheduler_t *scheduler);
	int (*init_reg)(struct rga_job *job);
	void (*soft_reset)(struct rga_scheduler_t *scheduler);
	int (*read_back_reg)(struct rga_job *job, struct rga_scheduler_t *scheduler);
	int (*irq)(struct rga_scheduler_t *scheduler);
	int (*isr_thread)(struct rga_job *job, struct rga_scheduler_t *scheduler);
};

struct rga_timer {
	u32 busy_time;
	u32 busy_time_record;
};

struct rga_scheduler_t {
	struct device *dev;
	void __iomem *rga_base;
	struct rga_iommu_info *iommu_info;

	struct clk *clks[RGA_MAX_BUS_CLK];
	int num_clks;

	enum rga_scheduler_status status;
	int pd_refcount;

	struct rga_job *running_job;
	struct list_head todo_list;
	spinlock_t irq_lock;
	wait_queue_head_t job_done_wq;
	const struct rga_backend_ops *ops;
	const struct rga_hw_data *data;
	int job_count;
	int irq;
	struct rga_version_t version;
	int core;

	struct rga_timer timer;
};

struct rga_request {
	struct rga_req *task_list;
	int task_count;
	uint32_t finished_task_count;
	uint32_t failed_task_count;

	bool use_batch_mode;
	bool is_running;
	bool is_done;
	int ret;
	uint32_t sync_mode;

	int32_t acquire_fence_fd;
	int32_t release_fence_fd;
	struct dma_fence *release_fence;
	spinlock_t fence_lock;

	wait_queue_head_t finished_wq;

	int flags;
	uint8_t mpi_config_flags;
	int id;
	struct rga_session *session;

	spinlock_t lock;
	struct kref refcount;

	pid_t pid;

	/*
	 * The mapping of virtual addresses to obtain physical addresses requires
	 * the memory mapping information of the current process.
	 */
	struct mm_struct *current_mm;

	/* TODO: add some common work */
};

struct rga_pending_request_manager {
	struct mutex lock;

	/*
	 * @request_idr:
	 *
	 * Mapping of request id to object pointers. Used by the GEM
	 * subsystem. Protected by @lock.
	 */
	struct idr request_idr;

	int request_count;
};

struct rga_session_manager {
	struct mutex lock;

	struct idr ctx_id_idr;

	int session_cnt;
};

struct rga_drvdata_t {
	/* used by rga2's mmu lock */
	struct mutex lock;

	struct rga_scheduler_t *scheduler[RGA_MAX_SCHEDULER];
	int num_of_scheduler;
	/* The scheduler_index used by default for memory mapping. */
	int map_scheduler_index;
	struct rga_mmu_base *mmu_base;

	struct delayed_work power_off_work;

	struct rga_mm *mm;

	/* rga_job pending manager, import by RGA_START_CONFIG */
	struct rga_pending_request_manager *pend_request_manager;

	struct rga_session_manager *session_manager;

#ifdef CONFIG_ROCKCHIP_RGA_ASYNC
	struct rga_fence_context *fence_ctx;
#endif

#ifdef CONFIG_ROCKCHIP_RGA_DEBUGGER
	struct rga_debugger *debugger;
#endif
};

struct rga_irqs_data_t {
	const char *name;
	irqreturn_t (*irq_hdl)(int irq, void *ctx);
	irqreturn_t (*irq_thread)(int irq, void *ctx);
};

struct rga_match_data_t {
	const char * const *clks;
	int num_clks;
};

static inline int rga_read(int offset, struct rga_scheduler_t *scheduler)
{
	return readl(scheduler->rga_base + offset);
}

static inline void rga_write(int value, int offset, struct rga_scheduler_t *scheduler)
{
	writel(value, scheduler->rga_base + offset);
}

int rga_power_enable(struct rga_scheduler_t *scheduler);
int rga_power_disable(struct rga_scheduler_t *scheduler);

int rga_kernel_commit(struct rga_req *cmd);

#endif /* __LINUX_RGA_FENCE_H_ */
