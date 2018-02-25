/*
 *
 * (C) COPYRIGHT 2012-2016 ARM Limited. All rights reserved.
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
 * @file mali_kbase_mem_profile_debugfs.h
 * Header file for mem profiles entries in debugfs
 *
 */

#ifndef _KBASE_MEM_PROFILE_DEBUGFS_H
#define _KBASE_MEM_PROFILE_DEBUGFS_H

#include <linux/debugfs.h>
#include <linux/seq_file.h>

/**
 * @brief Remove entry from Mali memory profile debugfs
 */
void kbasep_mem_profile_debugfs_remove(struct kbase_context *kctx);

/**
 * @brief Insert @p data to the debugfs file so it can be read by userspace
 *
 * The function takes ownership of @p data and frees it later when new data
 * is inserted.
 *
 * If the debugfs entry corresponding to the @p kctx doesn't exist,
 * an attempt will be made to create it.
 *
 * @param kctx The context whose debugfs file @p data should be inserted to
 * @param data A NULL-terminated string to be inserted to the debugfs file,
 *             without the trailing new line character
 * @param size The length of the @p data string
 * @return 0 if @p data inserted correctly
 *         -EAGAIN in case of error
 * @post @ref mem_profile_initialized will be set to @c true
 *       the first time this function succeeds.
 */
int kbasep_mem_profile_debugfs_insert(struct kbase_context *kctx, char *data,
					size_t size);

#endif  /*_KBASE_MEM_PROFILE_DEBUGFS_H*/

