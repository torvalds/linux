/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */

#ifndef __PANFROST_DEVICE_H__
#define __PANFROST_DEVICE_H__

#include <linux/spinlock.h>
#include <drm/drm_device.h>
#include <drm/drm_mm.h>
#include <drm/gpu_scheduler.h>

struct panfrost_device;
struct panfrost_mmu;
struct panfrost_job_slot;
struct panfrost_job;
struct panfrost_perfcnt;

#define NUM_JOB_SLOTS 3

struct panfrost_features {
	u16 id;
	u16 revision;

	u64 shader_present;
	u64 tiler_present;
	u64 l2_present;
	u64 stack_present;
	u32 as_present;
	u32 js_present;

	u32 l2_features;
	u32 core_features;
	u32 tiler_features;
	u32 mem_features;
	u32 mmu_features;
	u32 thread_features;
	u32 max_threads;
	u32 thread_max_workgroup_sz;
	u32 thread_max_barrier_sz;
	u32 coherency_features;
	u32 texture_features[4];
	u32 js_features[16];

	u32 nr_core_groups;
	u32 thread_tls_alloc;

	unsigned long hw_features[64 / BITS_PER_LONG];
	unsigned long hw_issues[64 / BITS_PER_LONG];
};

struct panfrost_devfreq_slot {
	ktime_t busy_time;
	ktime_t idle_time;
	ktime_t time_last_update;
	bool busy;
};

struct panfrost_device {
	struct device *dev;
	struct drm_device *ddev;
	struct platform_device *pdev;

	spinlock_t hwaccess_lock;

	struct drm_mm mm;
	spinlock_t mm_lock;

	void __iomem *iomem;
	struct clk *clock;
	struct clk *bus_clock;
	struct regulator *regulator;
	struct reset_control *rstc;

	struct panfrost_features features;

	struct panfrost_mmu *mmu;
	struct panfrost_job_slot *js;

	struct panfrost_job *jobs[NUM_JOB_SLOTS];
	struct list_head scheduled_jobs;

	struct panfrost_perfcnt *perfcnt;

	struct mutex sched_lock;
	struct mutex reset_lock;

	struct {
		struct devfreq *devfreq;
		struct thermal_cooling_device *cooling;
		unsigned long cur_freq;
		unsigned long cur_volt;
		struct panfrost_devfreq_slot slot[NUM_JOB_SLOTS];
	} devfreq;
};

struct panfrost_file_priv {
	struct panfrost_device *pfdev;

	struct drm_sched_entity sched_entity[NUM_JOB_SLOTS];
};

static inline struct panfrost_device *to_panfrost_device(struct drm_device *ddev)
{
	return ddev->dev_private;
}

static inline int panfrost_model_cmp(struct panfrost_device *pfdev, s32 id)
{
	s32 match_id = pfdev->features.id;

	if (match_id & 0xf000)
		match_id &= 0xf00f;
	return match_id - id;
}

static inline bool panfrost_model_is_bifrost(struct panfrost_device *pfdev)
{
	return panfrost_model_cmp(pfdev, 0x1000) >= 0;
}

static inline bool panfrost_model_eq(struct panfrost_device *pfdev, s32 id)
{
	return !panfrost_model_cmp(pfdev, id);
}

int panfrost_unstable_ioctl_check(void);

int panfrost_device_init(struct panfrost_device *pfdev);
void panfrost_device_fini(struct panfrost_device *pfdev);

int panfrost_device_resume(struct device *dev);
int panfrost_device_suspend(struct device *dev);

const char *panfrost_exception_name(struct panfrost_device *pfdev, u32 exception_code);

#endif
