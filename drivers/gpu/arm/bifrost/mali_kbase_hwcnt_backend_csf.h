/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2021 ARM Limited. All rights reserved.
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

/*
 * Concrete implementation of mali_kbase_hwcnt_backend interface for CSF
 * backend.
 */

#ifndef _KBASE_HWCNT_BACKEND_CSF_H_
#define _KBASE_HWCNT_BACKEND_CSF_H_

#include "mali_kbase_hwcnt_backend.h"
#include "mali_kbase_hwcnt_backend_csf_if.h"
#include "mali_kbase_hwcnt_watchdog_if.h"

/**
 * kbase_hwcnt_backend_csf_create() - Create a CSF hardware counter backend
 *                                    interface.
 * @csf_if:       Non-NULL pointer to a hwcnt backend CSF interface structure
 *                used to create backend interface.
 * @ring_buf_cnt: The buffer count of CSF hwcnt backend, used when allocate ring
 *                buffer, MUST be power of 2.
 * @watchdog_if:  Non-NULL pointer to a hwcnt watchdog interface structure used
 *                to create backend interface.
 * @iface:        Non-NULL pointer to backend interface structure that is filled
 *                in on creation success.
 *
 * Calls to iface->dump_enable_nolock() require the CSF Scheduler IRQ lock.
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_backend_csf_create(
	struct kbase_hwcnt_backend_csf_if *csf_if, u32 ring_buf_cnt,
	struct kbase_hwcnt_watchdog_interface *watchdog_if,
	struct kbase_hwcnt_backend_interface *iface);

/**
 * kbase_hwcnt_backend_csf_metadata_init() - Initialize the metadata for a CSF
 *                                           hardware counter backend.
 * @iface: Non-NULL pointer to backend interface structure
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_backend_csf_metadata_init(
	struct kbase_hwcnt_backend_interface *iface);

/**
 * kbase_hwcnt_backend_csf_metadata_term() - Terminate the metadata for a CSF
 *                                           hardware counter backend.
 * @iface: Non-NULL pointer to backend interface structure.
 */
void kbase_hwcnt_backend_csf_metadata_term(
	struct kbase_hwcnt_backend_interface *iface);

/**
 * kbase_hwcnt_backend_csf_destroy() - Destroy a CSF hardware counter backend
 *                                     interface.
 * @iface: Pointer to interface to destroy.
 *
 * Can be safely called on an all-zeroed interface, or on an already destroyed
 * interface.
 */
void kbase_hwcnt_backend_csf_destroy(
	struct kbase_hwcnt_backend_interface *iface);

/**
 * kbase_hwcnt_backend_csf_protm_entered() - CSF HWC backend function to receive
 *                                           notification that protected mode
 *                                           has been entered.
 * @iface: Non-NULL pointer to HWC backend interface.
 */
void kbase_hwcnt_backend_csf_protm_entered(
	struct kbase_hwcnt_backend_interface *iface);

/**
 * kbase_hwcnt_backend_csf_protm_exited() - CSF HWC backend function to receive
 *                                          notification that protected mode has
 *                                          been exited.
 * @iface: Non-NULL pointer to HWC backend interface.
 */
void kbase_hwcnt_backend_csf_protm_exited(
	struct kbase_hwcnt_backend_interface *iface);

/**
 * kbase_hwcnt_backend_csf_on_unrecoverable_error() - CSF HWC backend function
 *                                                    called when unrecoverable
 *                                                    errors are detected.
 * @iface: Non-NULL pointer to HWC backend interface.
 *
 * This should be called on encountering errors that can only be recovered from
 * with reset, or that may put HWC logic in state that could result in hang. For
 * example, on bus error, or when FW becomes unresponsive.
 */
void kbase_hwcnt_backend_csf_on_unrecoverable_error(
	struct kbase_hwcnt_backend_interface *iface);

/**
 * kbase_hwcnt_backend_csf_on_before_reset() - CSF HWC backend function to be
 *                                             called immediately before a
 *                                             reset. Takes us out of the
 *                                             unrecoverable error state, if we
 *                                             were in it.
 * @iface: Non-NULL pointer to HWC backend interface.
 */
void kbase_hwcnt_backend_csf_on_before_reset(
	struct kbase_hwcnt_backend_interface *iface);

/**
 * kbase_hwcnt_backend_csf_on_prfcnt_sample() - CSF performance counter sample
 *                                              complete interrupt handler.
 * @iface: Non-NULL pointer to HWC backend interface.
 */
void kbase_hwcnt_backend_csf_on_prfcnt_sample(
	struct kbase_hwcnt_backend_interface *iface);

/**
 * kbase_hwcnt_backend_csf_on_prfcnt_threshold() - CSF performance counter
 *                                                 buffer reach threshold
 *                                                 interrupt handler.
 * @iface: Non-NULL pointer to HWC backend interface.
 */
void kbase_hwcnt_backend_csf_on_prfcnt_threshold(
	struct kbase_hwcnt_backend_interface *iface);

/**
 * kbase_hwcnt_backend_csf_on_prfcnt_overflow() - CSF performance counter buffer
 *                                                overflow interrupt handler.
 * @iface: Non-NULL pointer to HWC backend interface.
 */
void kbase_hwcnt_backend_csf_on_prfcnt_overflow(
	struct kbase_hwcnt_backend_interface *iface);

/**
 * kbase_hwcnt_backend_csf_on_prfcnt_enable() - CSF performance counter enabled
 *                                              interrupt handler.
 * @iface: Non-NULL pointer to HWC backend interface.
 */
void kbase_hwcnt_backend_csf_on_prfcnt_enable(
	struct kbase_hwcnt_backend_interface *iface);

/**
 * kbase_hwcnt_backend_csf_on_prfcnt_disable() - CSF performance counter
 *                                               disabled interrupt handler.
 * @iface: Non-NULL pointer to HWC backend interface.
 */
void kbase_hwcnt_backend_csf_on_prfcnt_disable(
	struct kbase_hwcnt_backend_interface *iface);

#endif /* _KBASE_HWCNT_BACKEND_CSF_H_ */
