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
 * @file mali_kbase_mem_profile_debugfs.h
 * Header file for mem profiles entries in debugfs
 *
 */

#ifndef _KBASE_MEM_PROFILE_DEBUGFS_H
#define _KBASE_MEM_PROFILE_DEBUGFS_H

#include <mali_kbase.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

/**
 * @brief Add new entry to Mali memory profile debugfs
 */
mali_error kbasep_mem_profile_debugfs_add(struct kbase_context *kctx);

/**
 * @brief Remove entry from Mali memory profile debugfs
 */
void kbasep_mem_profile_debugfs_remove(struct kbase_context *kctx);

/**
 * @brief Insert data to debugfs file, so it can be read by userspce
 *
 * Function takes ownership of @c data and frees it later when new data
 * are inserted.
 *
 * @param kctx Context to which file data should be inserted
 * @param data NULL-terminated string to be inserted to mem_profile file,
		without trailing new line character
 * @param size @c buf length
 */
void kbasep_mem_profile_debugfs_insert(struct kbase_context *kctx, char *data,
		size_t size);

#endif  /*_KBASE_MEM_PROFILE_DEBUGFS_H*/

