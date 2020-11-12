/*
 *
 * (C) COPYRIGHT 2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#ifndef _KBASE_CCSWE_H_
#define _KBASE_CCSWE_H_

#include <linux/spinlock.h>

/**
 * struct kbase_ccswe - Cycle count software estimator.
 *
 * @access:         Spinlock protecting this structure access.
 * @timestamp_ns:   Timestamp(ns) when the last frequency change
 *                  occurred.
 * @cycles_elapsed: Number of cycles elapsed before the last frequency
 *                  change
 * @gpu_freq:       Current GPU frequency(Hz) value.
 * @prev_gpu_freq:  Previous GPU frequency(Hz) before the last frequency
 *                  change.
 */
struct kbase_ccswe {
	spinlock_t access;
	u64 timestamp_ns;
	u64 cycles_elapsed;
	u32 gpu_freq;
	u32 prev_gpu_freq;
};

/**
 * kbase_ccswe_init() - initialize the cycle count estimator.
 *
 * @self: Cycles count software estimator instance.
 */
void kbase_ccswe_init(struct kbase_ccswe *self);


/**
 * kbase_ccswe_cycle_at() - Estimate cycle count at given timestamp.
 *
 * @self: Cycles count software estimator instance.
 * @timestamp_ns: The timestamp(ns) for cycle count estimation.
 *
 * The timestamp must be bigger than the timestamp of the penultimate
 * frequency change. If only one frequency change occurred, the
 * timestamp must be bigger than the timestamp of the frequency change.
 * This is to allow the following code to be executed w/o synchronization.
 * If lines below executed atomically, it is safe to assume that only
 * one frequency change may happen in between.
 *
 *     u64 ts = ktime_get_raw_ns();
 *     u64 cycle = kbase_ccswe_cycle_at(&ccswe, ts)
 *
 * Returns: estimated value of cycle count at a given time.
 */
u64 kbase_ccswe_cycle_at(struct kbase_ccswe *self, u64 timestamp_ns);

/**
 * kbase_ccswe_freq_change() - update GPU frequency.
 *
 * @self:         Cycles count software estimator instance.
 * @timestamp_ns: Timestamp(ns) when frequency change occurred.
 * @gpu_freq:     New GPU frequency value.
 *
 * The timestamp must be bigger than the timestamp of the previous
 * frequency change. The function is to be called at the frequency
 * change moment (not later).
 */
void kbase_ccswe_freq_change(
	struct kbase_ccswe *self, u64 timestamp_ns, u32 gpu_freq);

/**
 * kbase_ccswe_reset() - reset estimator state
 *
 * @self:    Cycles count software estimator instance.
 */
void kbase_ccswe_reset(struct kbase_ccswe *self);

#endif /* _KBASE_CCSWE_H_ */
