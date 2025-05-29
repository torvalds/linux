/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_RPS_TYPES_H
#define INTEL_RPS_TYPES_H

#include <linux/atomic.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue.h>

struct intel_ips {
	u64 last_count1;
	unsigned long last_time1;
	unsigned long chipset_power;
	u64 last_count2;
	u64 last_time2;
	unsigned long gfx_power;
	u8 corr;

	int c, m;
};

struct intel_rps_ei {
	ktime_t ktime;
	u32 render_c0;
	u32 media_c0;
};

enum {
	INTEL_RPS_ENABLED = 0,
	INTEL_RPS_ACTIVE,
	INTEL_RPS_INTERRUPTS,
	INTEL_RPS_TIMER,
};

/**
 * struct intel_rps_freq_caps - rps freq capabilities
 * @rp0_freq: non-overclocked max frequency
 * @rp1_freq: "less than" RP0 power/frequency
 * @min_freq: aka RPn, minimum frequency
 *
 * Freq caps exposed by HW, values are in "hw units" and intel_gpu_freq()
 * should be used to convert to MHz
 */
struct intel_rps_freq_caps {
	u8 rp0_freq;
	u8 rp1_freq;
	u8 min_freq;
};

struct intel_rps {
	struct mutex lock; /* protects enabling and the worker */

	/*
	 * work, interrupts_enabled and pm_iir are protected by
	 * gt->irq_lock
	 */
	struct timer_list timer;
	struct work_struct work;
	unsigned long flags;

	ktime_t pm_timestamp;
	u32 pm_interval;
	u32 pm_iir;

	/* PM interrupt bits that should never be masked */
	u32 pm_intrmsk_mbz;
	u32 pm_events;

	/* Frequencies are stored in potentially platform dependent multiples.
	 * In other words, *_freq needs to be multiplied by X to be interesting.
	 * Soft limits are those which are used for the dynamic reclocking done
	 * by the driver (raise frequencies under heavy loads, and lower for
	 * lighter loads). Hard limits are those imposed by the hardware.
	 *
	 * A distinction is made for overclocking, which is never enabled by
	 * default, and is considered to be above the hard limit if it's
	 * possible at all.
	 */
	u8 cur_freq;		/* Current frequency (cached, may not == HW) */
	u8 last_freq;		/* Last SWREQ frequency */
	u8 min_freq_softlimit;	/* Minimum frequency permitted by the driver */
	u8 max_freq_softlimit;	/* Max frequency permitted by the driver */
	u8 max_freq;		/* Maximum frequency, RP0 if not overclocking */
	u8 min_freq;		/* AKA RPn. Minimum frequency */
	u8 boost_freq;		/* Frequency to request when wait boosting */
	u8 idle_freq;		/* Frequency to request when we are idle */
	u8 efficient_freq;	/* AKA RPe. Pre-determined balanced frequency */
	u8 rp1_freq;		/* "less than" RP0 power/frequency */
	u8 rp0_freq;		/* Non-overclocked max frequency. */
	u16 gpll_ref_freq;	/* vlv/chv GPLL reference frequency */

	int last_adj;

	struct {
		struct mutex mutex;

		enum { LOW_POWER, BETWEEN, HIGH_POWER } mode;
		unsigned int interactive;

		u8 up_threshold; /* Current %busy required to uplock */
		u8 down_threshold; /* Current %busy required to downclock */
	} power;

	atomic_t num_waiters;
	unsigned int boosts;

	/* manual wa residency calculations */
	struct intel_rps_ei ei;
	struct intel_ips ips;
};

#endif /* INTEL_RPS_TYPES_H */
