/*
 *
 * (C) COPYRIGHT 2015-2017 ARM Limited. All rights reserved.
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

#ifndef _KBASE_VINSTR_H_
#define _KBASE_VINSTR_H_

#include <mali_kbase_hwcnt_reader.h>

/*****************************************************************************/

struct kbase_vinstr_context;
struct kbase_vinstr_client;

struct kbase_uk_hwcnt_setup {
	/* IN */
	u64 dump_buffer;
	u32 jm_bm;
	u32 shader_bm;
	u32 tiler_bm;
	u32 unused_1; /* keep for backwards compatibility */
	u32 mmu_l2_bm;
	u32 padding;
	/* OUT */
};

struct kbase_uk_hwcnt_reader_setup {
	/* IN */
	u32 buffer_count;
	u32 jm_bm;
	u32 shader_bm;
	u32 tiler_bm;
	u32 mmu_l2_bm;

	/* OUT */
	s32 fd;
};
/*****************************************************************************/

/**
 * kbase_vinstr_init() - initialize the vinstr core
 * @kbdev: kbase device
 *
 * Return: pointer to the vinstr context on success or NULL on failure
 */
struct kbase_vinstr_context *kbase_vinstr_init(struct kbase_device *kbdev);

/**
 * kbase_vinstr_term() - terminate the vinstr core
 * @vinstr_ctx: vinstr context
 */
void kbase_vinstr_term(struct kbase_vinstr_context *vinstr_ctx);

/**
 * kbase_vinstr_hwcnt_reader_setup - configure hw counters reader
 * @vinstr_ctx: vinstr context
 * @setup:      reader's configuration
 *
 * Return: zero on success
 */
int kbase_vinstr_hwcnt_reader_setup(
		struct kbase_vinstr_context        *vinstr_ctx,
		struct kbase_uk_hwcnt_reader_setup *setup);

/**
 * kbase_vinstr_legacy_hwc_setup - configure hw counters for dumping
 * @vinstr_ctx: vinstr context
 * @cli:        pointer where to store pointer to new vinstr client structure
 * @setup:      hwc configuration
 *
 * Return: zero on success
 */
int kbase_vinstr_legacy_hwc_setup(
		struct kbase_vinstr_context *vinstr_ctx,
		struct kbase_vinstr_client  **cli,
		struct kbase_uk_hwcnt_setup *setup);

/**
 * kbase_vinstr_hwcnt_kernel_setup - configure hw counters for kernel side
 *                                   client
 * @vinstr_ctx:    vinstr context
 * @setup:         reader's configuration
 * @kernel_buffer: pointer to dump buffer
 *
 * setup->buffer_count and setup->fd are not used for kernel side clients.
 *
 * Return: pointer to client structure, or NULL on failure
 */
struct kbase_vinstr_client *kbase_vinstr_hwcnt_kernel_setup(
		struct kbase_vinstr_context *vinstr_ctx,
		struct kbase_uk_hwcnt_reader_setup *setup,
		void *kernel_buffer);

/**
 * kbase_vinstr_hwc_dump - issue counter dump for vinstr client
 * @cli:      pointer to vinstr client
 * @event_id: id of event that triggered hwcnt dump
 *
 * Return: zero on success
 */
int kbase_vinstr_hwc_dump(
		struct kbase_vinstr_client   *cli,
		enum base_hwcnt_reader_event event_id);

/**
 * kbase_vinstr_hwc_clear - performs a reset of the hardware counters for
 *                          a given kbase context
 * @cli: pointer to vinstr client
 *
 * Return: zero on success
 */
int kbase_vinstr_hwc_clear(struct kbase_vinstr_client *cli);

/**
 * kbase_vinstr_try_suspend - try suspending operation of a given vinstr context
 * @vinstr_ctx: vinstr context
 *
 * Return: 0 on success, or negative if state change is in progress
 *
 * Warning: This API call is non-generic. It is meant to be used only by
 *          job scheduler state machine.
 *
 * Function initiates vinstr switch to suspended state. Once it was called
 * vinstr enters suspending state. If function return non-zero value, it
 * indicates that state switch is not complete and function must be called
 * again. On state switch vinstr will trigger job scheduler state machine
 * cycle.
 */
int kbase_vinstr_try_suspend(struct kbase_vinstr_context *vinstr_ctx);

/**
 * kbase_vinstr_suspend - suspends operation of a given vinstr context
 * @vinstr_ctx: vinstr context
 *
 * Function initiates vinstr switch to suspended state. Then it blocks until
 * operation is completed.
 */
void kbase_vinstr_suspend(struct kbase_vinstr_context *vinstr_ctx);

/**
 * kbase_vinstr_resume - resumes operation of a given vinstr context
 * @vinstr_ctx: vinstr context
 *
 * Function can be called only if it was preceded by a successful call
 * to kbase_vinstr_suspend.
 */
void kbase_vinstr_resume(struct kbase_vinstr_context *vinstr_ctx);

/**
 * kbase_vinstr_dump_size - Return required size of dump buffer
 * @kbdev: device pointer
 *
 * Return : buffer size in bytes
 */
size_t kbase_vinstr_dump_size(struct kbase_device *kbdev);

/**
 * kbase_vinstr_detach_client - Detach a client from the vinstr core
 * @cli: pointer to vinstr client
 */
void kbase_vinstr_detach_client(struct kbase_vinstr_client *cli);

#endif /* _KBASE_VINSTR_H_ */

