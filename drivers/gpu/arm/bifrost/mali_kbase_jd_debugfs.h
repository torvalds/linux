/*
 *
 * (C) COPYRIGHT 2014-2018 ARM Limited. All rights reserved.
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
 * @file mali_kbase_jd_debugfs.h
 * Header file for job dispatcher-related entries in debugfs
 */

#ifndef _KBASE_JD_DEBUGFS_H
#define _KBASE_JD_DEBUGFS_H

#include <linux/debugfs.h>

#define MALI_JD_DEBUGFS_VERSION 3

/* Forward declarations */
struct kbase_context;

/**
 * kbasep_jd_debugfs_ctx_init() - Add debugfs entries for JD system
 *
 * @kctx Pointer to kbase_context
 */
void kbasep_jd_debugfs_ctx_init(struct kbase_context *kctx);

#endif  /*_KBASE_JD_DEBUGFS_H*/
