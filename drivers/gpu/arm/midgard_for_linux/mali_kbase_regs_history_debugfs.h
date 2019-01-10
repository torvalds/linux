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



/**
 * Header file for register access history support via debugfs
 *
 * This interface is made available via /sys/kernel/debug/mali#/regs_history*.
 *
 * Usage:
 * - regs_history_enabled: whether recording of register accesses is enabled.
 *   Write 'y' to enable, 'n' to disable.
 * - regs_history_size: size of the register history buffer, must be > 0
 * - regs_history: return the information about last accesses to the registers.
 */

#ifndef _KBASE_REGS_HISTORY_DEBUGFS_H
#define _KBASE_REGS_HISTORY_DEBUGFS_H

struct kbase_device;

#if defined(CONFIG_DEBUG_FS) && !defined(CONFIG_MALI_NO_MALI)

/**
 * kbasep_regs_history_debugfs_init - add debugfs entries for register history
 *
 * @kbdev: Pointer to kbase_device containing the register history
 */
void kbasep_regs_history_debugfs_init(struct kbase_device *kbdev);

#else /* CONFIG_DEBUG_FS */

#define kbasep_regs_history_debugfs_init CSTD_NOP

#endif /* CONFIG_DEBUG_FS */

#endif  /*_KBASE_REGS_HISTORY_DEBUGFS_H*/
