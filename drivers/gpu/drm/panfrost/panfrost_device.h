/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */

#ifndef __PANFROST_DEVICE_H__
#define __PANFROST_DEVICE_H__

#include <linux/atomic.h>
#include <linux/io-pgtable.h>
#include <linux/regulator/consumer.h>
#include <linux/spinlock.h>
#include <drm/drm_device.h>
#include <drm/drm_mm.h>
#include <drm/gpu_scheduler.h>

#include "panfrost_devfreq.h"

struct panfrost_device;
struct panfrost_mmu;
struct panfrost_job_slot;
struct panfrost_job;
struct panfrost_perfcnt;

#define NUM_JOB_SLOTS 3
#define MAX_PM_DOMAINS 3

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
	u32 afbc_features;
	u32 texture_features[4];
	u32 js_features[16];

	u32 nr_core_groups;
	u32 thread_tls_alloc;

	unsigned long hw_features[64 / BITS_PER_LONG];
	unsigned long hw_issues[64 / BITS_PER_LONG];
};

/*
 * Features that cannot be automatically detected and need matching using the
 * compatible string, typically SoC-specific.
 */
struct panfrost_compatible {
	/* Supplies count and names. */
	int num_supplies;
	const char * const *supply_names;
	/*
	 * Number of power domains required, note that values 0 and 1 are
	 * handled identically, as only values > 1 need special handling.
	 */
	int num_pm_domains;
	/* Only required if num_pm_domains > 1. */
	const char * const *pm_domain_names;

	/* Vendor implementation quirks callback */
	void (*vendor_quirk)(struct panfrost_device *pfdev);
};

struct panfrost_device {
	struct device *dev;
	struct drm_device *ddev;
	struct platform_device *pdev;

	void __iomem *iomem;
	struct clk *clock;
	struct clk *bus_clock;
	struct regulator_bulk_data *regulators;
	struct reset_control *rstc;
	/* pm_domains for devices with more than one. */
	struct device *pm_domain_devs[MAX_PM_DOMAINS];
	struct device_link *pm_domain_links[MAX_PM_DOMAINS];
	bool coherent;

	struct panfrost_features features;
	const struct panfrost_compatible *comp;

	spinlock_t as_lock;
	unsigned long as_in_use_mask;
	unsigned long as_alloc_mask;
	struct list_head as_lru_list;

	struct panfrost_job_slot *js;

	struct panfrost_job *jobs[NUM_JOB_SLOTS];
	struct list_head scheduled_jobs;

	struct panfrost_perfcnt *perfcnt;

	struct mutex sched_lock;

	struct {
		struct work_struct work;
		atomic_t pending;
	} reset;

	struct mutex shrinker_lock;
	struct list_head shrinker_list;
	struct shrinker shrinker;

	struct panfrost_devfreq pfdevfreq;
};

struct panfrost_mmu {
	struct io_pgtable_cfg pgtbl_cfg;
	struct io_pgtable_ops *pgtbl_ops;
	int as;
	atomic_t as_count;
	struct list_head list;
};

struct panfrost_file_priv {
	struct panfrost_device *pfdev;

	struct drm_sched_entity sched_entity[NUM_JOB_SLOTS];

	struct panfrost_mmu mmu;
	struct drm_mm mm;
	spinlock_t mm_lock;
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
void panfrost_device_reset(struct panfrost_device *pfdev);

int panfrost_device_resume(struct device *dev);
int panfrost_device_suspend(struct device *dev);

const char *panfrost_exception_name(struct panfrost_device *pfdev, u32 exception_code);

#endif
