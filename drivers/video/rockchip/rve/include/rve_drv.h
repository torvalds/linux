/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#ifndef __LINUX_RVE_DRV_H_
#define __LINUX_RVE_DRV_H_

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
#include <linux/dma-map-ops.h>
#include <linux/hrtimer.h>

#include "rve_debugger.h"
#include "rve.h"

/* sample interval: 1000ms */
#define RVE_LOAD_INTERVAL 1000000000

/* Driver information */
#define DRIVER_DESC		"RVE Device Driver"
#define DRIVER_NAME		"rve"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define RVE_MAJOR_VERSION_MASK		(0x0000FF00)
#define RVE_MINOR_VERSION_MASK		(0x000000FF)
#define RVE_PROD_NUM_MASK				(0xFFFF0000)

#define DRIVER_MAJOR_VERSION		1
#define DRIVER_MINOR_VERSION		0
#define DRIVER_REVISION_VERSION		4

#define DRIVER_VERSION (STR(DRIVER_MAJOR_VERSION) "." STR(DRIVER_MINOR_VERSION) \
			"." STR(DRIVER_REVISION_VERSION))

/* time limit */
#define RVE_ASYNC_TIMEOUT_DELAY		500
#define RVE_SYNC_TIMEOUT_DELAY		HZ
#define RVE_RESET_TIMEOUT			10000

#define RVE_BUFFER_POOL_MAX_SIZE	64
#define RVE_MAX_SCHEDULER 1

#define RVE_MAX_BUS_CLK 10
#define RVE_MAX_PID_INFO 10

extern struct rve_drvdata_t *rve_drvdata;

enum {
	RVE_SCHEDULER_CORE0		= 1,
	RVE_NONE_CORE			 = 0,
};

enum {
	RVE_CMD_SLAVE		= 1,
	RVE_CMD_MASTER		= 2,
};

struct rve_fence_context {
	unsigned int context;
	unsigned int seqno;
	spinlock_t spinlock;
};

struct rve_fence_waiter {
	/* Base sync driver waiter structure */
	struct dma_fence_cb waiter;

	struct rve_job *job;
};

struct rve_scheduler_t;
struct rve_internal_ctx_t;

struct rve_session {
	int id;

	pid_t tgid;
};

struct rve_job {
	struct list_head head;
	struct rve_scheduler_t *scheduler;
	struct rve_session *session;

	struct rve_cmd_reg_array_t *regcmd_data;

	struct rve_internal_ctx_t *ctx;

	/* for rve virtual_address */
	struct mm_struct *mm;

	struct dma_fence *out_fence;
	struct dma_fence *in_fence;
	spinlock_t fence_lock;
	ktime_t timestamp;
	ktime_t hw_running_time;
	ktime_t hw_recoder_time;
	unsigned int flags;

	int priority;
	int core;
	int ret;
	pid_t pid;
};

struct rve_backend_ops {
	int (*get_version)(struct rve_scheduler_t *scheduler);
	int (*set_reg)(struct rve_job *job, struct rve_scheduler_t *scheduler);
	int (*init_reg)(struct rve_job *job);
	void (*soft_reset)(struct rve_scheduler_t *scheduler);
};

struct rve_timer {
	u32 busy_time;
	u32 busy_time_record;
};

struct rve_sche_pid_info_t {
	pid_t pid;
	/* hw total use time, per hrtimer */
	u32 hw_time_total;

	uint32_t last_job_rd_bandwidth;
	uint32_t last_job_wr_bandwidth;
	uint32_t last_job_cycle_cnt;
};

struct rve_sche_session_info_t {
	struct rve_sche_pid_info_t pid_info[RVE_MAX_PID_INFO];

	int pd_refcount;

	/* the bandwidth of total read bytes, per hrtimer */
	uint32_t rd_bandwidth;
	/* the bandwidth of total write bytes, per hrtimer */
	uint32_t wr_bandwidth;
	/* the total running cycle of current frame, per hrtimer */
	uint32_t cycle_cnt;
	/* total interrupt count */
	uint64_t total_int_cnt;
};

struct rve_scheduler_t {
	struct device *dev;
	void __iomem *rve_base;

	struct clk *clks[RVE_MAX_BUS_CLK];
	int num_clks;

	struct rve_job *running_job;
	struct list_head todo_list;
	spinlock_t irq_lock;
	wait_queue_head_t job_done_wq;
	const struct rve_backend_ops *ops;
	const struct rve_hw_data *data;
	int job_count;
	int irq;
	struct rve_version_t version;
	int core;

	struct rve_timer timer;

	struct rve_sche_session_info_t session;
};

struct rve_cmd_reg_array_t {
	uint32_t cmd_reg[58];
};

struct rve_ctx_debug_info_t {
	pid_t pid;
	u32 timestamp;
	/* hw total use time, per hrtimer */
	u32 hw_time_total;
	/* last job use time, per hrtimer*/
	u32 last_job_use_time;
	/* last job hardware use time, per hrtimer*/
	u32 last_job_hw_use_time;
	/* the most time-consuming job, per hrtimer */
	u32 max_cost_time_per_sec;
};

struct rve_internal_ctx_t {
	struct rve_scheduler_t *scheduler;
	struct rve_session *session;

	struct rve_cmd_reg_array_t *regcmd_data;
	uint32_t cmd_num;

	uint32_t sync_mode;
	int flags;
	int id;

	uint32_t running_job_count;
	uint32_t finished_job_count;
	bool is_running;

	uint32_t disable_auto_cancel;

	int priority;
	int32_t out_fence_fd;
	int32_t in_fence_fd;

	struct dma_fence *out_fence;

	spinlock_t lock;
	struct kref refcount;

	/* debug info */
	struct rve_ctx_debug_info_t debug_info;

	/* TODO: add some common work */
};

struct rve_pending_ctx_manager {
	spinlock_t lock;

	/*
	 * @ctx_id_idr:
	 *
	 * Mapping of ctx id to object pointers. Used by the GEM
	 * subsystem. Protected by @lock.
	 */
	struct idr ctx_id_idr;

	int ctx_count;
};

struct rve_session_manager {
	struct mutex lock;

	struct idr ctx_id_idr;

	int session_cnt;
};

struct rve_drvdata_t {
	struct rve_fence_context *fence_ctx;

	/* used by rve2's mmu lock */
	struct mutex lock;

	struct rve_scheduler_t *scheduler[RVE_MAX_SCHEDULER];
	int num_of_scheduler;

	struct delayed_work power_off_work;
	struct wake_lock wake_lock;

	struct rve_mm *mm;

	/* rve_job pending manager, import by RVE_IOC_START_CONFIG */
	struct rve_pending_ctx_manager *pend_ctx_manager;

	struct rve_session_manager *session_manager;

#ifdef CONFIG_ROCKCHIP_RVE_DEBUGGER
	struct rve_debugger *debugger;
#endif
};

struct rve_irqs_data_t {
	const char *name;
	irqreturn_t (*irq_hdl)(int irq, void *ctx);
	irqreturn_t (*irq_thread)(int irq, void *ctx);
};

struct rve_match_data_t {
	const char * const *clks;
	int num_clks;
	const struct rve_irqs_data_t *irqs;
	int num_irqs;
};

static inline int rve_read(int offset, struct rve_scheduler_t *scheduler)
{
	return readl(scheduler->rve_base + offset);
}

static inline void rve_write(int value, int offset, struct rve_scheduler_t *scheduler)
{
	writel(value, scheduler->rve_base + offset);
}

int rve_power_enable(struct rve_scheduler_t *scheduler);
int rve_power_disable(struct rve_scheduler_t *scheduler);

#endif /* __LINUX_RVE_FENCE_H_ */
