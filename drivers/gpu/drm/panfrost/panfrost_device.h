/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */

#ifndef __PANFROST_DEVICE_H__
#define __PANFROST_DEVICE_H__

#include <linux/atomic.h>
#include <linux/io-pgtable.h>
#include <linux/pm.h>
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
#define MAX_PM_DOMAINS 5

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
	unsigned long as_faulty_mask;
	struct list_head as_lru_list;

	struct panfrost_job_slot *js;

	struct panfrost_job *jobs[NUM_JOB_SLOTS][2];
	struct list_head scheduled_jobs;

	struct panfrost_perfcnt *perfcnt;
	atomic_t profile_mode;

	struct mutex sched_lock;

	struct {
		struct workqueue_struct *wq;
		struct work_struct work;
		atomic_t pending;
	} reset;

	struct mutex shrinker_lock;
	struct list_head shrinker_list;
	struct shrinker *shrinker;

	struct panfrost_devfreq pfdevfreq;

	struct {
		atomic_t use_count;
		spinlock_t lock;
	} cycle_counter;
};

struct panfrost_mmu {
	struct panfrost_device *pfdev;
	struct kref refcount;
	struct io_pgtable_cfg pgtbl_cfg;
	struct io_pgtable_ops *pgtbl_ops;
	struct drm_mm mm;
	spinlock_t mm_lock;
	int as;
	atomic_t as_count;
	struct list_head list;
};

struct panfrost_engine_usage {
	unsigned long long elapsed_ns[NUM_JOB_SLOTS];
	unsigned long long cycles[NUM_JOB_SLOTS];
};

struct panfrost_file_priv {
	struct panfrost_device *pfdev;

	struct drm_sched_entity sched_entity[NUM_JOB_SLOTS];

	struct panfrost_mmu *mmu;

	struct panfrost_engine_usage engine_usage;
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

extern const struct dev_pm_ops panfrost_pm_ops;

enum drm_panfrost_exception_type {
	DRM_PANFROST_EXCEPTION_OK = 0x00,
	DRM_PANFROST_EXCEPTION_DONE = 0x01,
	DRM_PANFROST_EXCEPTION_INTERRUPTED = 0x02,
	DRM_PANFROST_EXCEPTION_STOPPED = 0x03,
	DRM_PANFROST_EXCEPTION_TERMINATED = 0x04,
	DRM_PANFROST_EXCEPTION_KABOOM = 0x05,
	DRM_PANFROST_EXCEPTION_EUREKA = 0x06,
	DRM_PANFROST_EXCEPTION_ACTIVE = 0x08,
	DRM_PANFROST_EXCEPTION_MAX_NON_FAULT = 0x3f,
	DRM_PANFROST_EXCEPTION_JOB_CONFIG_FAULT = 0x40,
	DRM_PANFROST_EXCEPTION_JOB_POWER_FAULT = 0x41,
	DRM_PANFROST_EXCEPTION_JOB_READ_FAULT = 0x42,
	DRM_PANFROST_EXCEPTION_JOB_WRITE_FAULT = 0x43,
	DRM_PANFROST_EXCEPTION_JOB_AFFINITY_FAULT = 0x44,
	DRM_PANFROST_EXCEPTION_JOB_BUS_FAULT = 0x48,
	DRM_PANFROST_EXCEPTION_INSTR_INVALID_PC = 0x50,
	DRM_PANFROST_EXCEPTION_INSTR_INVALID_ENC = 0x51,
	DRM_PANFROST_EXCEPTION_INSTR_TYPE_MISMATCH = 0x52,
	DRM_PANFROST_EXCEPTION_INSTR_OPERAND_FAULT = 0x53,
	DRM_PANFROST_EXCEPTION_INSTR_TLS_FAULT = 0x54,
	DRM_PANFROST_EXCEPTION_INSTR_BARRIER_FAULT = 0x55,
	DRM_PANFROST_EXCEPTION_INSTR_ALIGN_FAULT = 0x56,
	DRM_PANFROST_EXCEPTION_DATA_INVALID_FAULT = 0x58,
	DRM_PANFROST_EXCEPTION_TILE_RANGE_FAULT = 0x59,
	DRM_PANFROST_EXCEPTION_ADDR_RANGE_FAULT = 0x5a,
	DRM_PANFROST_EXCEPTION_IMPRECISE_FAULT = 0x5b,
	DRM_PANFROST_EXCEPTION_OOM = 0x60,
	DRM_PANFROST_EXCEPTION_OOM_AFBC = 0x61,
	DRM_PANFROST_EXCEPTION_UNKNOWN = 0x7f,
	DRM_PANFROST_EXCEPTION_DELAYED_BUS_FAULT = 0x80,
	DRM_PANFROST_EXCEPTION_GPU_SHAREABILITY_FAULT = 0x88,
	DRM_PANFROST_EXCEPTION_SYS_SHAREABILITY_FAULT = 0x89,
	DRM_PANFROST_EXCEPTION_GPU_CACHEABILITY_FAULT = 0x8a,
	DRM_PANFROST_EXCEPTION_TRANSLATION_FAULT_0 = 0xc0,
	DRM_PANFROST_EXCEPTION_TRANSLATION_FAULT_1 = 0xc1,
	DRM_PANFROST_EXCEPTION_TRANSLATION_FAULT_2 = 0xc2,
	DRM_PANFROST_EXCEPTION_TRANSLATION_FAULT_3 = 0xc3,
	DRM_PANFROST_EXCEPTION_TRANSLATION_FAULT_4 = 0xc4,
	DRM_PANFROST_EXCEPTION_TRANSLATION_FAULT_IDENTITY = 0xc7,
	DRM_PANFROST_EXCEPTION_PERM_FAULT_0 = 0xc8,
	DRM_PANFROST_EXCEPTION_PERM_FAULT_1 = 0xc9,
	DRM_PANFROST_EXCEPTION_PERM_FAULT_2 = 0xca,
	DRM_PANFROST_EXCEPTION_PERM_FAULT_3 = 0xcb,
	DRM_PANFROST_EXCEPTION_TRANSTAB_BUS_FAULT_0 = 0xd0,
	DRM_PANFROST_EXCEPTION_TRANSTAB_BUS_FAULT_1 = 0xd1,
	DRM_PANFROST_EXCEPTION_TRANSTAB_BUS_FAULT_2 = 0xd2,
	DRM_PANFROST_EXCEPTION_TRANSTAB_BUS_FAULT_3 = 0xd3,
	DRM_PANFROST_EXCEPTION_ACCESS_FLAG_0 = 0xd8,
	DRM_PANFROST_EXCEPTION_ACCESS_FLAG_1 = 0xd9,
	DRM_PANFROST_EXCEPTION_ACCESS_FLAG_2 = 0xda,
	DRM_PANFROST_EXCEPTION_ACCESS_FLAG_3 = 0xdb,
	DRM_PANFROST_EXCEPTION_ADDR_SIZE_FAULT_IN0 = 0xe0,
	DRM_PANFROST_EXCEPTION_ADDR_SIZE_FAULT_IN1 = 0xe1,
	DRM_PANFROST_EXCEPTION_ADDR_SIZE_FAULT_IN2 = 0xe2,
	DRM_PANFROST_EXCEPTION_ADDR_SIZE_FAULT_IN3 = 0xe3,
	DRM_PANFROST_EXCEPTION_ADDR_SIZE_FAULT_OUT0 = 0xe4,
	DRM_PANFROST_EXCEPTION_ADDR_SIZE_FAULT_OUT1 = 0xe5,
	DRM_PANFROST_EXCEPTION_ADDR_SIZE_FAULT_OUT2 = 0xe6,
	DRM_PANFROST_EXCEPTION_ADDR_SIZE_FAULT_OUT3 = 0xe7,
	DRM_PANFROST_EXCEPTION_MEM_ATTR_FAULT_0 = 0xe8,
	DRM_PANFROST_EXCEPTION_MEM_ATTR_FAULT_1 = 0xe9,
	DRM_PANFROST_EXCEPTION_MEM_ATTR_FAULT_2 = 0xea,
	DRM_PANFROST_EXCEPTION_MEM_ATTR_FAULT_3 = 0xeb,
	DRM_PANFROST_EXCEPTION_MEM_ATTR_NONCACHE_0 = 0xec,
	DRM_PANFROST_EXCEPTION_MEM_ATTR_NONCACHE_1 = 0xed,
	DRM_PANFROST_EXCEPTION_MEM_ATTR_NONCACHE_2 = 0xee,
	DRM_PANFROST_EXCEPTION_MEM_ATTR_NONCACHE_3 = 0xef,
};

static inline bool
panfrost_exception_is_fault(u32 exception_code)
{
	return exception_code > DRM_PANFROST_EXCEPTION_MAX_NON_FAULT;
}

const char *panfrost_exception_name(u32 exception_code);
bool panfrost_exception_needs_reset(const struct panfrost_device *pfdev,
				    u32 exception_code);

static inline void
panfrost_device_schedule_reset(struct panfrost_device *pfdev)
{
	atomic_set(&pfdev->reset.pending, 1);
	queue_work(pfdev->reset.wq, &pfdev->reset.work);
}

#endif
