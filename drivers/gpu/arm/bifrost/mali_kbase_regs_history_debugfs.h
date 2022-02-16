/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014, 2016, 2020-2022 ARM Limited. All rights reserved.
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

/**
 * DOC: Header file for register access history support via debugfs
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

#if defined(CONFIG_DEBUG_FS) && !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)

/**
 * kbase_io_history_init - initialize data struct for register access history
 *
 * @h: The register history to initialize
 * @n: The number of register accesses that the buffer could hold
 *
 * Return: 0 if successfully initialized, failure otherwise
 */
int kbase_io_history_init(struct kbase_io_history *h, u16 n);

/**
 * kbase_io_history_term - uninit all resources for the register access history
 *
 * @h: The register history to terminate
 */
void kbase_io_history_term(struct kbase_io_history *h);

/**
 * kbase_io_history_dump - print the register history to the kernel ring buffer
 *
 * @kbdev: Pointer to kbase_device containing the register history to dump
 */
void kbase_io_history_dump(struct kbase_device *kbdev);

/**
 * kbasep_regs_history_debugfs_init - add debugfs entries for register history
 *
 * @kbdev: Pointer to kbase_device containing the register history
 */
void kbasep_regs_history_debugfs_init(struct kbase_device *kbdev);

#else /* !defined(CONFIG_DEBUG_FS) || IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI) */

#define kbase_io_history_init(...) (0)

#define kbase_io_history_term CSTD_NOP

#define kbase_io_history_dump CSTD_NOP

#define kbasep_regs_history_debugfs_init CSTD_NOP

#endif /* defined(CONFIG_DEBUG_FS) && !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI) */

#endif  /*_KBASE_REGS_HISTORY_DEBUGFS_H*/
