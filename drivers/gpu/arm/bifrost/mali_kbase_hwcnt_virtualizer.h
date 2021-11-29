/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2018, 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

/*
 * Hardware counter virtualizer API.
 *
 * Virtualizes a hardware counter context, so multiple clients can access
 * a single hardware counter resource as though each was the exclusive user.
 */

#ifndef _KBASE_HWCNT_VIRTUALIZER_H_
#define _KBASE_HWCNT_VIRTUALIZER_H_

#include <linux/types.h>
#include <linux/workqueue.h>

struct kbase_hwcnt_context;
struct kbase_hwcnt_virtualizer;
struct kbase_hwcnt_virtualizer_client;
struct kbase_hwcnt_enable_map;
struct kbase_hwcnt_dump_buffer;

/**
 * kbase_hwcnt_virtualizer_init - Initialise a hardware counter virtualizer.
 * @hctx:              Non-NULL pointer to the hardware counter context to
 *                     virtualize.
 * @dump_threshold_ns: Minimum threshold period for dumps between different
 *                     clients where a new accumulator dump will not be
 *                     performed, and instead accumulated values will be used.
 *                     If 0, rate limiting will be disabled.
 * @out_hvirt:         Non-NULL pointer to where the pointer to the created
 *                     virtualizer will be stored on success.
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_virtualizer_init(
	struct kbase_hwcnt_context *hctx,
	u64 dump_threshold_ns,
	struct kbase_hwcnt_virtualizer **out_hvirt);

/**
 * kbase_hwcnt_virtualizer_term - Terminate a hardware counter virtualizer.
 * @hvirt: Pointer to virtualizer to be terminated.
 */
void kbase_hwcnt_virtualizer_term(
	struct kbase_hwcnt_virtualizer *hvirt);

/**
 * kbase_hwcnt_virtualizer_metadata - Get the hardware counter metadata used by
 *                                    the virtualizer, so related counter data
 *                                    structures can be created.
 * @hvirt: Non-NULL pointer to the hardware counter virtualizer.
 *
 * Return: Non-NULL pointer to metadata, or NULL on error.
 */
const struct kbase_hwcnt_metadata *kbase_hwcnt_virtualizer_metadata(
	struct kbase_hwcnt_virtualizer *hvirt);

/**
 * kbase_hwcnt_virtualizer_client_create - Create a new virtualizer client.
 * @hvirt:      Non-NULL pointer to the hardware counter virtualizer.
 * @enable_map: Non-NULL pointer to the enable map for the client. Must have the
 *              same metadata as the virtualizer.
 * @out_hvcli:  Non-NULL pointer to where the pointer to the created client will
 *              be stored on success.
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_virtualizer_client_create(
	struct kbase_hwcnt_virtualizer *hvirt,
	const struct kbase_hwcnt_enable_map *enable_map,
	struct kbase_hwcnt_virtualizer_client **out_hvcli);

/**
 * kbase_hwcnt_virtualizer_client_destroy() - Destroy a virtualizer client.
 * @hvcli: Pointer to the hardware counter client.
 */
void kbase_hwcnt_virtualizer_client_destroy(
	struct kbase_hwcnt_virtualizer_client *hvcli);

/**
 * kbase_hwcnt_virtualizer_client_set_counters - Perform a dump of the client's
 *                                               currently enabled counters, and
 *                                               enable a new set of counters
 *                                               that will be used for
 *                                               subsequent dumps.
 * @hvcli:       Non-NULL pointer to the virtualizer client.
 * @enable_map:  Non-NULL pointer to the new counter enable map for the client.
 *               Must have the same metadata as the virtualizer.
 * @ts_start_ns: Non-NULL pointer where the start timestamp of the dump will
 *               be written out to on success.
 * @ts_end_ns:   Non-NULL pointer where the end timestamp of the dump will
 *               be written out to on success.
 * @dump_buf:    Pointer to the buffer where the dump will be written out to on
 *               success. If non-NULL, must have the same metadata as the
 *               accumulator. If NULL, the dump will be discarded.
 *
 * Return: 0 on success or error code.
 */
int kbase_hwcnt_virtualizer_client_set_counters(
	struct kbase_hwcnt_virtualizer_client *hvcli,
	const struct kbase_hwcnt_enable_map *enable_map,
	u64 *ts_start_ns,
	u64 *ts_end_ns,
	struct kbase_hwcnt_dump_buffer *dump_buf);

/**
 * kbase_hwcnt_virtualizer_client_dump - Perform a dump of the client's
 *                                       currently enabled counters.
 * @hvcli:       Non-NULL pointer to the virtualizer client.
 * @ts_start_ns: Non-NULL pointer where the start timestamp of the dump will
 *               be written out to on success.
 * @ts_end_ns:   Non-NULL pointer where the end timestamp of the dump will
 *               be written out to on success.
 * @dump_buf:    Pointer to the buffer where the dump will be written out to on
 *               success. If non-NULL, must have the same metadata as the
 *               accumulator. If NULL, the dump will be discarded.
 *
 * Return: 0 on success or error code.
 */
int kbase_hwcnt_virtualizer_client_dump(
	struct kbase_hwcnt_virtualizer_client *hvcli,
	u64 *ts_start_ns,
	u64 *ts_end_ns,
	struct kbase_hwcnt_dump_buffer *dump_buf);

/**
 * kbase_hwcnt_virtualizer_queue_work() - Queue hardware counter related async
 *                                        work on a workqueue specialized for
 *                                        hardware counters.
 * @hvirt: Non-NULL pointer to the hardware counter virtualizer.
 * @work:  Non-NULL pointer to work to queue.
 *
 * Return: false if work was already on a queue, true otherwise.
 *
 * This is a convenience function that directly calls the underlying
 * kbase_hwcnt_context's kbase_hwcnt_context_queue_work.
 */
bool kbase_hwcnt_virtualizer_queue_work(struct kbase_hwcnt_virtualizer *hvirt,
					struct work_struct *work);

#endif /* _KBASE_HWCNT_VIRTUALIZER_H_ */
