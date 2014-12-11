/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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



/**
 * @file mali_kbase_jd_debugfs.h
 * Header file for job dispatcher-related entries in debugfs
 */

#ifndef _KBASE_JD_DEBUGFS_H
#define _KBASE_JD_DEBUGFS_H

#include <linux/debugfs.h>

#include <mali_kbase.h>

/**
 * @brief Initialize JD debugfs entries
 *
 * This should be called during device probing after the main mali debugfs
 * directory has been created.
 *
 * @param[in] kbdev Pointer to kbase_device
 */
int kbasep_jd_debugfs_init(struct kbase_device *kbdev);

/**
 * @brief Clean up all JD debugfs entries and related data
 *
 * This should be called during device removal before the main mali debugfs
 * directory will be removed.
 *
 * @param[in] kbdev Pointer to kbase_device
 */
void kbasep_jd_debugfs_term(struct kbase_device *kbdev);

/**
 * @brief Add new entry to JD debugfs
 *
 * @param[in] kctx Pointer to kbase_context
 *
 * @return 0 on success, failure otherwise
 */
int kbasep_jd_debugfs_ctx_add(struct kbase_context *kctx);

/**
 * @brief Remove entry from JD debugfs
 *
 * param[in] kctx Pointer to kbase_context
 */
void kbasep_jd_debugfs_ctx_remove(struct kbase_context *kctx);

#endif  /*_KBASE_JD_DEBUGFS_H*/
