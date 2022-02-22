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
#include <linux/dma-buf-cache.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/fb.h>
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
#include <linux/wakelock.h>
#include <linux/pm_runtime.h>
#include <linux/sched/mm.h>

#include <asm/cacheflush.h>

#include <linux/iommu.h>
#include <linux/iova.h>
#include <linux/dma-iommu.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#include <linux/dma-map-ops.h>
#endif

#include <linux/hrtimer.h>

#include "rga.h"
#include "rga_debugger.h"

#define RGA_CORE_REG_OFFSET 0x10000

/* sample interval: 1000ms */
#define RGA_LOAD_INTERVAL 1000000000

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
#define DRIVER_REVISION_VERSION		4

#define DRIVER_VERSION (STR(DRIVER_MAJOR_VERISON) "." STR(DRIVER_MINOR_VERSION) \
			"." STR(DRIVER_REVISION_VERSION))

/* time limit */
#define RGA_ASYNC_TIMEOUT_DELAY		500
#define RGA_SYNC_TIMEOUT_DELAY		HZ
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

struct rga_fence_context {
	unsigned int context;
	unsigned int seqno;
	spinlock_t spinlock;
};

struct rga_fence_waiter {
	/* Base sync driver waiter structure */
	struct dma_fence_cb waiter;

	struct rga_job *job;
};

struct rga_iommu_dma_cookie {
	enum iommu_dma_cookie_type  type;

	/* Full allocator for IOMMU_DMA_IOVA_COOKIE */
	struct iova_domain  iovad;
};

/*
 * legacy: Wait for the import process to completely replace the current
 * dma_map and remove it
 */
struct rga_dma_buffer_t {
	/* DMABUF information */
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;

	dma_addr_t iova;
	unsigned long size;
	void *vaddr;
	enum dma_data_direction dir;

	/* It indicates whether the buffer is cached */
	bool cached;

	struct list_head link;
	struct kref refcount;

	struct iommu_domain *domain;
	struct rga_iommu_dma_cookie *cookie;

	bool use_viraddr;
};

struct rga_dma_buffer {
	/* DMABUF information */
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	void *vmap_ptr;

	struct iommu_domain *domain;
	struct rga_iommu_dma_cookie *cookie;

	enum dma_data_direction dir;

	dma_addr_t iova;
	unsigned long size;
	/*
	 * The offset of the first page of the sgt.
	 * Since alloc iova must be page aligned, the offset of the first page is
	 * identified separately.
	 */
	size_t offset;

	/* The core of the mapping */
	int core;
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
	uint32_t dma_buffer_size;

	/* virtual address */
	struct rga_virt_addr *virt_addr;

	/* physical address */
	uint64_t phys_addr;

	struct rga_memory_parm memory_parm;


	struct mm_struct *current_mm;

	/* memory type. */
	uint32_t type;

	uint32_t handle;

	uint32_t mm_flag;

	struct kref refcount;
};

/*
 * yqw add:
 * In order to use the virtual address to refresh the cache,
 * it may be merged into sgt later.
 */
struct rga2_mmu_other_t {
	uint32_t *MMU_src0_base;
	uint32_t *MMU_src1_base;
	uint32_t *MMU_dst_base;
	uint32_t MMU_src0_count;
	uint32_t MMU_src1_count;
	uint32_t MMU_dst_count;

	uint32_t MMU_len;
	bool MMU_map;
};

struct rga_job {
	struct list_head head;
	struct rga_req rga_command_base;
	uint32_t cmd_reg[32 * 8];
	uint32_t csc_reg[12];

	struct rga_dma_buffer_t *rga_dma_buffer_src0;
	struct rga_dma_buffer_t *rga_dma_buffer_src1;
	struct rga_dma_buffer_t *rga_dma_buffer_dst;
	/* used by rga2 */
	struct rga_dma_buffer_t *rga_dma_buffer_els;

	struct rga_internal_buffer *src_buffer;
	struct rga_internal_buffer *src1_buffer;
	struct rga_internal_buffer *dst_buffer;
	/* used by rga2 */
	struct rga_internal_buffer *els_buffer;

	struct dma_buf *dma_buf_src0;
	struct dma_buf *dma_buf_src1;
	struct dma_buf *dma_buf_dst;
	struct dma_buf *dma_buf_els;

	/* for rga2 virtual_address */
	struct mm_struct *mm;
	struct rga2_mmu_other_t vir_page_table;

	struct dma_fence *out_fence;
	struct dma_fence *in_fence;
	spinlock_t fence_lock;
	/* job time stamp */
	ktime_t timestamp;
	/* The time when the job is actually executed on the hardware */
	ktime_t hw_running_time;
	/* The time only for hrtimer to calculate the load */
	ktime_t hw_recoder_time;
	unsigned int flags;
	int ctx_id;
	int priority;
	int core;
	int ret;
	pid_t pid;
	bool use_batch_mode;
};

struct rga_scheduler_t;

struct rga_backend_ops {
	int (*get_version)(struct rga_scheduler_t *scheduler);
	int (*set_reg)(struct rga_job *job, struct rga_scheduler_t *scheduler);
	int (*init_reg)(struct rga_job *job);
	void (*soft_reset)(struct rga_scheduler_t *scheduler);
};

struct rga_timer {
	u32 busy_time;
	u32 busy_time_record;
};

struct rga_scheduler_t {
	struct device *dev;
	void __iomem *rga_base;

	struct clk *clks[RGA_MAX_BUS_CLK];
	int num_clks;

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

struct rga_internal_ctx_t {
	struct rga_req *cached_cmd;
	int cmd_num;
	int flags;
	int id;

	uint8_t mpi_config_flags;
	uint32_t sync_mode;

	uint32_t finished_job_count;

	bool use_batch_mode;
	bool is_running;

	struct dma_fence *out_fence;
	int32_t out_fence_fd;

	spinlock_t lock;
	struct kref refcount;

	pid_t pid;
	/* TODO: add some common work */
};

struct rga_pending_ctx_manager {
	struct mutex lock;

	/*
	 * @ctx_id_idr:
	 *
	 * Mapping of ctx id to object pointers. Used by the GEM
	 * subsystem. Protected by @lock.
	 */
	struct idr ctx_id_idr;

	int ctx_count;
};

struct rga_drvdata_t {
	struct miscdevice miscdev;

	struct rga_fence_context *fence_ctx;

	/* used by rga2's mmu lock */
	struct mutex lock;

	struct rga_scheduler_t *rga_scheduler[RGA_MAX_SCHEDULER];
	int num_of_scheduler;

	struct delayed_work power_off_work;
	struct wake_lock wake_lock;

	struct rga_mm *mm;

	/* rga_job pending manager, import by RGA_START_CONFIG */
	struct rga_pending_ctx_manager *pend_ctx_manager;

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
	const struct rga_irqs_data_t *irqs;
	int num_irqs;
};

static inline int rga_read(int offset, struct rga_scheduler_t *rga_scheduler)
{
	return readl(rga_scheduler->rga_base + offset);
}

static inline void rga_write(int value, int offset, struct rga_scheduler_t *rga_scheduler)
{
	writel(value, rga_scheduler->rga_base + offset);
}

int rga_power_enable(struct rga_scheduler_t *rga_scheduler);
int rga_power_disable(struct rga_scheduler_t *rga_scheduler);

int rga_kernel_commit(struct rga_req *cmd);

#endif /* __LINUX_RGA_FENCE_H_ */
