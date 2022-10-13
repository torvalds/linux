/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
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

#ifndef _KBASE_DEBUG_CSF_FAULT_H
#define _KBASE_DEBUG_CSF_FAULT_H

#if IS_ENABLED(CONFIG_DEBUG_FS)
/**
 * kbase_debug_csf_fault_debugfs_init - Initialize CSF fault debugfs
 * @kbdev:	Device pointer
 */
void kbase_debug_csf_fault_debugfs_init(struct kbase_device *kbdev);

/**
 * kbase_debug_csf_fault_init - Create the fault event wait queue per device
 *                              and initialize the required resources.
 * @kbdev:    Device pointer
 *
 * Return: Zero on success or a negative error code.
 */
int kbase_debug_csf_fault_init(struct kbase_device *kbdev);

/**
 * kbase_debug_csf_fault_term - Clean up resources created by
 *		                @kbase_debug_csf_fault_init.
 * @kbdev:    Device pointer
 */
void kbase_debug_csf_fault_term(struct kbase_device *kbdev);

/**
 * kbase_debug_csf_fault_wait_completion - Wait for the client to complete.
 *
 * @kbdev:    Device Pointer
 *
 * Wait for the user space client to finish reading the fault information.
 * This function must be called in thread context.
 */
void kbase_debug_csf_fault_wait_completion(struct kbase_device *kbdev);

/**
 * kbase_debug_csf_fault_notify - Notify client of a fault.
 *
 * @kbdev:    Device pointer
 * @kctx:     Faulty context (can be NULL)
 * @error:    Error code.
 *
 * Store fault information and wake up the user space client.
 *
 * Return: true if a dump on fault was initiated or was is in progress and
 *         so caller can opt to wait for the dumping to complete.
 */
bool kbase_debug_csf_fault_notify(struct kbase_device *kbdev,
		struct kbase_context *kctx, enum dumpfault_error_type error);

/**
 * kbase_debug_csf_fault_dump_enabled - Check if dump on fault is enabled.
 *
 * @kbdev:  Device pointer
 *
 * Return: true if debugfs file is opened so dump on fault is enabled.
 */
static inline bool kbase_debug_csf_fault_dump_enabled(struct kbase_device *kbdev)
{
	return atomic_read(&kbdev->csf.dof.enabled);
}

/**
 * kbase_debug_csf_fault_dump_complete - Check if dump on fault is completed.
 *
 * @kbdev:  Device pointer
 *
 * Return: true if dump on fault completes or file is closed.
 */
static inline bool kbase_debug_csf_fault_dump_complete(struct kbase_device *kbdev)
{
	unsigned long flags;
	bool ret;

	if (likely(!kbase_debug_csf_fault_dump_enabled(kbdev)))
		return true;

	spin_lock_irqsave(&kbdev->csf.dof.lock, flags);
	ret = (kbdev->csf.dof.error_code == DF_NO_ERROR);
	spin_unlock_irqrestore(&kbdev->csf.dof.lock, flags);

	return ret;
}
#else /* CONFIG_DEBUG_FS */
static inline int kbase_debug_csf_fault_init(struct kbase_device *kbdev)
{
	return 0;
}

static inline void kbase_debug_csf_fault_term(struct kbase_device *kbdev)
{
}

static inline void kbase_debug_csf_fault_wait_completion(struct kbase_device *kbdev)
{
}

static inline bool kbase_debug_csf_fault_notify(struct kbase_device *kbdev,
		struct kbase_context *kctx, enum dumpfault_error_type error)
{
	return false;
}

static inline bool kbase_debug_csf_fault_dump_enabled(struct kbase_device *kbdev)
{
	return false;
}

static inline bool kbase_debug_csf_fault_dump_complete(struct kbase_device *kbdev)
{
	return true;
}
#endif /* CONFIG_DEBUG_FS */

#endif /*_KBASE_DEBUG_CSF_FAULT_H*/
