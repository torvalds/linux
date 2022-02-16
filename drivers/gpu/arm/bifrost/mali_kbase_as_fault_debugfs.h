/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2016, 2020-2021 ARM Limited. All rights reserved.
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

#ifndef _KBASE_AS_FAULT_DEBUG_FS_H
#define _KBASE_AS_FAULT_DEBUG_FS_H

/**
 * kbase_as_fault_debugfs_init() - Add debugfs files for reporting page faults
 *
 * @kbdev: Pointer to kbase_device
 */
void kbase_as_fault_debugfs_init(struct kbase_device *kbdev);

/**
 * kbase_as_fault_debugfs_new() - make the last fault available on debugfs
 *
 * @kbdev: Pointer to kbase_device
 * @as_no: The address space the fault occurred on
 */
static inline void
kbase_as_fault_debugfs_new(struct kbase_device *kbdev, int as_no)
{
#if IS_ENABLED(CONFIG_DEBUG_FS)
#ifdef CONFIG_MALI_BIFROST_DEBUG
	kbdev->debugfs_as_read_bitmap |= (1ULL << as_no);
#endif /* CONFIG_DEBUG_FS */
#endif /* CONFIG_MALI_BIFROST_DEBUG */
}

#endif  /*_KBASE_AS_FAULT_DEBUG_FS_H*/
