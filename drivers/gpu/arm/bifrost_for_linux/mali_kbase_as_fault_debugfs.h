/*
 *
 * (C) COPYRIGHT 2016 ARM Limited. All rights reserved.
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
#ifdef CONFIG_DEBUG_FS
#ifdef CONFIG_MALI_BIFROST_DEBUG
	kbdev->debugfs_as_read_bitmap |= (1ULL << as_no);
#endif /* CONFIG_DEBUG_FS */
#endif /* CONFIG_MALI_BIFROST_DEBUG */
	return;
}

#endif  /*_KBASE_AS_FAULT_DEBUG_FS_H*/
