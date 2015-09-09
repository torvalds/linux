/*
 *
 * (C) COPYRIGHT 2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#ifndef _KBASE_VINSTR_H_
#define _KBASE_VINSTR_H_

#include <mali_kbase.h>
#include <mali_kbase_hwcnt_reader.h>

/*****************************************************************************/

struct kbase_vinstr_context;
struct kbase_vinstr_client;

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
 * kbase_vinstr_hwc_suspend - suspends hardware counter collection for
 *                            a given kbase context
 * @vinstr_ctx: vinstr context
 */
void kbase_vinstr_hwc_suspend(struct kbase_vinstr_context *vinstr_ctx);

/**
 * kbase_vinstr_hwc_resume - resumes hardware counter collection for
 *                            a given kbase context
 * @vinstr_ctx: vinstr context
 */
void kbase_vinstr_hwc_resume(struct kbase_vinstr_context *vinstr_ctx);

#endif /* _KBASE_VINSTR_H_ */

