/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_GT_TYPES__
#define __INTEL_GT_TYPES__

#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/llist.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/seqlock.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "uc/intel_uc.h"
#include "intel_gsc.h"

#include "i915_vma.h"
#include "i915_perf_types.h"
#include "intel_engine_types.h"
#include "intel_gt_buffer_pool_types.h"
#include "intel_hwconfig.h"
#include "intel_llc_types.h"
#include "intel_reset_types.h"
#include "intel_rc6_types.h"
#include "intel_rps_types.h"
#include "intel_migrate_types.h"
#include "intel_wakeref.h"
#include "intel_wopcm.h"

struct drm_i915_private;
struct i915_ggtt;
struct intel_engine_cs;
struct intel_uncore;

struct intel_mmio_range {
	u32 start;
	u32 end;
};

/*
 * The hardware has multiple kinds of multicast register ranges that need
 * special register steering (and future platforms are expected to add
 * additional types).
 *
 * During driver startup, we initialize the steering control register to
 * direct reads to a slice/subslice that are valid for the 'subslice' class
 * of multicast registers.  If another type of steering does not have any
 * overlap in valid steering targets with 'subslice' style registers, we will
 * need to explicitly re-steer reads of registers of the other type.
 *
 * Only the replication types that may need additional non-default steering
 * are listed here.
 */
enum intel_steering_type {
	L3BANK,
	MSLICE,
	LNCF,
	GAM,
	DSS,
	OADDRM,

	/*
	 * On some platforms there are multiple types of MCR registers that
	 * will always return a non-terminated value at instance (0, 0).  We'll
	 * lump those all into a single category to keep things simple.
	 */
	INSTANCE0,

	NUM_STEERING_TYPES
};

enum intel_submission_method {
	INTEL_SUBMISSION_RING,
	INTEL_SUBMISSION_ELSP,
	INTEL_SUBMISSION_GUC,
};

struct gt_defaults {
	u32 min_freq;
	u32 max_freq;
};

enum intel_gt_type {
	GT_PRIMARY,
	GT_TILE,
	GT_MEDIA,
};

struct intel_gt {
	struct drm_i915_private *i915;
	const char *name;
	enum intel_gt_type type;

	struct intel_uncore *uncore;
	struct i915_ggtt *ggtt;

	struct intel_uc uc;
	struct intel_gsc gsc;
	struct intel_wopcm wopcm;

	struct {
		/* Serialize global tlb invalidations */
		struct mutex invalidate_lock;

		/*
		 * Batch TLB invalidations
		 *
		 * After unbinding the PTE, we need to ensure the TLB
		 * are invalidated prior to releasing the physical pages.
		 * But we only need one such invalidation for all unbinds,
		 * so we track how many TLB invalidations have been
		 * performed since unbind the PTE and only emit an extra
		 * invalidate if no full barrier has been passed.
		 */
		seqcount_mutex_t seqno;
	} tlb;

	struct i915_wa_list wa_list;

	struct intel_gt_timelines {
		spinlock_t lock; /* protects active_list */
		struct list_head active_list;
	} timelines;

	struct intel_gt_requests {
		/**
		 * We leave the user IRQ off as much as possible,
		 * but this means that requests will finish and never
		 * be retired once the system goes idle. Set a timer to
		 * fire periodically while the ring is running. When it
		 * fires, go retire requests.
		 */
		struct delayed_work retire_work;
	} requests;

	struct {
		struct llist_head list;
		struct work_struct work;
	} watchdog;

	struct intel_wakeref wakeref;
	atomic_t user_wakeref;

	struct list_head closed_vma;
	spinlock_t closed_lock; /* guards the list of closed_vma */

	ktime_t last_init_time;
	struct intel_reset reset;

	/**
	 * Is the GPU currently considered idle, or busy executing
	 * userspace requests? Whilst idle, we allow runtime power
	 * management to power down the hardware and display clocks.
	 * In order to reduce the effect on performance, there
	 * is a slight delay before we do so.
	 */
	intel_wakeref_t awake;

	u32 clock_frequency;
	u32 clock_period_ns;

	struct intel_llc llc;
	struct intel_rc6 rc6;
	struct intel_rps rps;

	spinlock_t *irq_lock;
	u32 gt_imr;
	u32 pm_ier;
	u32 pm_imr;

	u32 pm_guc_events;

	struct {
		bool active;

		/**
		 * @lock: Lock protecting the below fields.
		 */
		seqcount_mutex_t lock;

		/**
		 * @total: Total time this engine was busy.
		 *
		 * Accumulated time not counting the most recent block in cases
		 * where engine is currently busy (active > 0).
		 */
		ktime_t total;

		/**
		 * @start: Timestamp of the last idle to active transition.
		 *
		 * Idle is defined as active == 0, active is active > 0.
		 */
		ktime_t start;
	} stats;

	struct intel_engine_cs *engine[I915_NUM_ENGINES];
	struct intel_engine_cs *engine_class[MAX_ENGINE_CLASS + 1]
					    [MAX_ENGINE_INSTANCE + 1];
	enum intel_submission_method submission_method;

	/*
	 * Default address space (either GGTT or ppGTT depending on arch).
	 *
	 * Reserved for exclusive use by the kernel.
	 */
	struct i915_address_space *vm;

	/*
	 * A pool of objects to use as shadow copies of client batch buffers
	 * when the command parser is enabled. Prevents the client from
	 * modifying the batch contents after software parsing.
	 *
	 * Buffers older than 1s are periodically reaped from the pool,
	 * or may be reclaimed by the shrinker before then.
	 */
	struct intel_gt_buffer_pool buffer_pool;

	struct i915_vma *scratch;

	struct intel_migrate migrate;

	const struct intel_mmio_range *steering_table[NUM_STEERING_TYPES];

	struct {
		u8 groupid;
		u8 instanceid;
	} default_steering;

	/**
	 * @mcr_lock: Protects the MCR steering register
	 *
	 * Protects the MCR steering register (e.g., GEN8_MCR_SELECTOR).
	 * Should be taken before uncore->lock in cases where both are desired.
	 */
	spinlock_t mcr_lock;

	/*
	 * Base of per-tile GTTMMADR where we can derive the MMIO and the GGTT.
	 */
	phys_addr_t phys_addr;

	struct intel_gt_info {
		unsigned int id;

		intel_engine_mask_t engine_mask;

		u32 l3bank_mask;

		u8 num_engines;

		/* General presence of SFC units */
		u8 sfc_mask;

		/* Media engine access to SFC per instance */
		u8 vdbox_sfc_access;

		/* Slice/subslice/EU info */
		struct sseu_dev_info sseu;

		unsigned long mslice_mask;

		/** @hwconfig: hardware configuration data */
		struct intel_hwconfig hwconfig;
	} info;

	struct {
		u8 uc_index;
		u8 wb_index; /* Only used on HAS_L3_CCS_READ() platforms */
	} mocs;

	/* gt/gtN sysfs */
	struct kobject sysfs_gt;

	/* sysfs defaults per gt */
	struct gt_defaults defaults;
	struct kobject *sysfs_defaults;

	struct i915_perf_gt perf;

	/** link: &ggtt.gt_list */
	struct list_head ggtt_link;
};

struct intel_gt_definition {
	enum intel_gt_type type;
	char *name;
	u32 mapping_base;
	u32 gsi_offset;
	intel_engine_mask_t engine_mask;
};

enum intel_gt_scratch_field {
	/* 8 bytes */
	INTEL_GT_SCRATCH_FIELD_DEFAULT = 0,

	/* 8 bytes */
	INTEL_GT_SCRATCH_FIELD_RENDER_FLUSH = 128,

	/* 8 bytes */
	INTEL_GT_SCRATCH_FIELD_COHERENTL3_WA = 256,
};

#endif /* __INTEL_GT_TYPES_H__ */
