/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright 2018-2019 Qiang Yu <yuq825@gmail.com> */

#ifndef __LIMA_DEVICE_H__
#define __LIMA_DEVICE_H__

#include <drm/drm_device.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include "lima_sched.h"
#include "lima_dump.h"
#include "lima_devfreq.h"

enum lima_gpu_id {
	lima_gpu_mali400 = 0,
	lima_gpu_mali450,
	lima_gpu_num,
};

enum lima_ip_id {
	lima_ip_pmu,
	lima_ip_gpmmu,
	lima_ip_ppmmu0,
	lima_ip_ppmmu1,
	lima_ip_ppmmu2,
	lima_ip_ppmmu3,
	lima_ip_ppmmu4,
	lima_ip_ppmmu5,
	lima_ip_ppmmu6,
	lima_ip_ppmmu7,
	lima_ip_gp,
	lima_ip_pp0,
	lima_ip_pp1,
	lima_ip_pp2,
	lima_ip_pp3,
	lima_ip_pp4,
	lima_ip_pp5,
	lima_ip_pp6,
	lima_ip_pp7,
	lima_ip_l2_cache0,
	lima_ip_l2_cache1,
	lima_ip_l2_cache2,
	lima_ip_dlbu,
	lima_ip_bcast,
	lima_ip_pp_bcast,
	lima_ip_ppmmu_bcast,
	lima_ip_num,
};

struct lima_device;

struct lima_ip {
	struct lima_device *dev;
	enum lima_ip_id id;
	bool present;

	void __iomem *iomem;
	int irq;

	union {
		/* gp/pp */
		bool async_reset;
		/* l2 cache */
		spinlock_t lock;
		/* pmu/bcast */
		u32 mask;
	} data;
};

enum lima_pipe_id {
	lima_pipe_gp,
	lima_pipe_pp,
	lima_pipe_num,
};

struct lima_device {
	struct device *dev;
	struct drm_device *ddev;

	enum lima_gpu_id id;
	u32 gp_version;
	u32 pp_version;
	int num_pp;

	void __iomem *iomem;
	struct clk *clk_bus;
	struct clk *clk_gpu;
	struct reset_control *reset;
	struct regulator *regulator;

	struct lima_ip ip[lima_ip_num];
	struct lima_sched_pipe pipe[lima_pipe_num];

	struct lima_vm *empty_vm;
	uint64_t va_start;
	uint64_t va_end;

	u32 *dlbu_cpu;
	dma_addr_t dlbu_dma;

	struct lima_devfreq devfreq;

	/* debug info */
	struct lima_dump_head dump;
	struct list_head error_task_list;
	struct mutex error_task_list_lock;
};

static inline struct lima_device *
to_lima_dev(struct drm_device *dev)
{
	return dev->dev_private;
}

int lima_device_init(struct lima_device *ldev);
void lima_device_fini(struct lima_device *ldev);

const char *lima_ip_name(struct lima_ip *ip);

typedef int (*lima_poll_func_t)(struct lima_ip *);

static inline int lima_poll_timeout(struct lima_ip *ip, lima_poll_func_t func,
				    int sleep_us, int timeout_us)
{
	ktime_t timeout = ktime_add_us(ktime_get(), timeout_us);

	might_sleep_if(sleep_us);
	while (1) {
		if (func(ip))
			return 0;

		if (timeout_us && ktime_compare(ktime_get(), timeout) > 0)
			return -ETIMEDOUT;

		if (sleep_us)
			usleep_range((sleep_us >> 2) + 1, sleep_us);
	}
	return 0;
}

int lima_device_suspend(struct device *dev);
int lima_device_resume(struct device *dev);

#endif
