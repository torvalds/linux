/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CSF_CSG_DEBUGFS_H_
#define _KBASE_CSF_CSG_DEBUGFS_H_

/* Forward declarations */
struct kbase_device;
struct kbase_context;
struct kbase_queue_group;

#define MALI_CSF_CSG_DEBUGFS_VERSION 0

/**
 * kbase_csf_queue_group_debugfs_init() - Add debugfs entry for queue groups
 *                                        associated with @kctx.
 *
 * @kctx: Pointer to kbase_context
 */
void kbase_csf_queue_group_debugfs_init(struct kbase_context *kctx);

/**
 * kbase_csf_debugfs_init() - Add a global debugfs entry for queue groups
 *
 * @kbdev: Pointer to the device
 */
void kbase_csf_debugfs_init(struct kbase_device *kbdev);

#endif /* _KBASE_CSF_CSG_DEBUGFS_H_ */
