/*
 *
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
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

/**
 * Hardware counter accumulator API.
 */

#ifndef _KBASE_HWCNT_ACCUMULATOR_H_
#define _KBASE_HWCNT_ACCUMULATOR_H_

#include <linux/types.h>

struct kbase_hwcnt_context;
struct kbase_hwcnt_accumulator;
struct kbase_hwcnt_enable_map;
struct kbase_hwcnt_dump_buffer;

/**
 * kbase_hwcnt_accumulator_acquire() - Acquire the hardware counter accumulator
 *                                     for a hardware counter context.
 * @hctx:  Non-NULL pointer to a hardware counter context.
 * @accum: Non-NULL pointer to where the pointer to the created accumulator
 *         will be stored on success.
 *
 * There can exist at most one instance of the hardware counter accumulator per
 * context at a time.
 *
 * If multiple clients need access to the hardware counters at the same time,
 * then an abstraction built on top of the single instance to the hardware
 * counter accumulator is required.
 *
 * No counters will be enabled with the returned accumulator. A subsequent call
 * to kbase_hwcnt_accumulator_set_counters must be used to turn them on.
 *
 * There are four components to a hardware counter dump:
 *  - A set of enabled counters
 *  - A start time
 *  - An end time
 *  - A dump buffer containing the accumulated counter values for all enabled
 *    counters between the start and end times.
 *
 * For each dump, it is guaranteed that all enabled counters were active for the
 * entirety of the period between the start and end times.
 *
 * It is also guaranteed that the start time of dump "n" is always equal to the
 * end time of dump "n - 1".
 *
 * For all dumps, the values of any counters that were not enabled is undefined.
 *
 * Return: 0 on success or error code.
 */
int kbase_hwcnt_accumulator_acquire(
	struct kbase_hwcnt_context *hctx,
	struct kbase_hwcnt_accumulator **accum);

/**
 * kbase_hwcnt_accumulator_release() - Release a hardware counter accumulator.
 * @accum: Non-NULL pointer to the hardware counter accumulator.
 *
 * The accumulator must be released before the context the accumulator was
 * created from is terminated.
 */
void kbase_hwcnt_accumulator_release(struct kbase_hwcnt_accumulator *accum);

/**
 * kbase_hwcnt_accumulator_set_counters() - Perform a dump of the currently
 *                                          enabled counters, and enable a new
 *                                          set of counters that will be used
 *                                          for subsequent dumps.
 * @accum:       Non-NULL pointer to the hardware counter accumulator.
 * @new_map:     Non-NULL pointer to the new counter enable map. Must have the
 *               same metadata as the accumulator.
 * @ts_start_ns: Non-NULL pointer where the start timestamp of the dump will
 *               be written out to on success.
 * @ts_end_ns:   Non-NULL pointer where the end timestamp of the dump will
 *               be written out to on success.
 * @dump_buf:    Pointer to the buffer where the dump will be written out to on
 *               success. If non-NULL, must have the same metadata as the
 *               accumulator. If NULL, the dump will be discarded.
 *
 * If this function fails for some unexpected reason (i.e. anything other than
 * invalid args), then the accumulator will be put into the error state until
 * the parent context is next disabled.
 *
 * Return: 0 on success or error code.
 */
int kbase_hwcnt_accumulator_set_counters(
	struct kbase_hwcnt_accumulator *accum,
	const struct kbase_hwcnt_enable_map *new_map,
	u64 *ts_start_ns,
	u64 *ts_end_ns,
	struct kbase_hwcnt_dump_buffer *dump_buf);

/**
 * kbase_hwcnt_accumulator_dump() - Perform a dump of the currently enabled
 *                                  counters.
 * @accum:       Non-NULL pointer to the hardware counter accumulator.
 * @ts_start_ns: Non-NULL pointer where the start timestamp of the dump will
 *               be written out to on success.
 * @ts_end_ns:   Non-NULL pointer where the end timestamp of the dump will
 *               be written out to on success.
 * @dump_buf:    Pointer to the buffer where the dump will be written out to on
 *               success. If non-NULL, must have the same metadata as the
 *               accumulator. If NULL, the dump will be discarded.
 *
 * If this function fails for some unexpected reason (i.e. anything other than
 * invalid args), then the accumulator will be put into the error state until
 * the parent context is next disabled.
 *
 * Return: 0 on success or error code.
 */
int kbase_hwcnt_accumulator_dump(
	struct kbase_hwcnt_accumulator *accum,
	u64 *ts_start_ns,
	u64 *ts_end_ns,
	struct kbase_hwcnt_dump_buffer *dump_buf);

/**
 * kbase_hwcnt_accumulator_timestamp_ns() - Get the current accumulator backend
 *                                          timestamp.
 * @accum: Non-NULL pointer to the hardware counter accumulator.
 *
 * Return: Accumulator backend timestamp in nanoseconds.
 */
u64 kbase_hwcnt_accumulator_timestamp_ns(struct kbase_hwcnt_accumulator *accum);

#endif /* _KBASE_HWCNT_ACCUMULATOR_H_ */
