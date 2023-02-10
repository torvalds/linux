/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2017-2018, 2020-2022 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CTX_SCHED_H_
#define _KBASE_CTX_SCHED_H_

#include <mali_kbase.h>

/**
 * DOC: The Context Scheduler manages address space assignment and reference
 * counting to kbase_context. The interface has been designed to minimise
 * interactions between the Job Scheduler and Power Management/MMU to support
 * the existing Job Scheduler interface.
 *
 * The initial implementation of the Context Scheduler does not schedule
 * contexts. Instead it relies on the Job Scheduler to make decisions of
 * when to schedule/evict contexts if address spaces are starved. In the
 * future, once an interface between the CS and JS has been devised to
 * provide enough information about how each context is consuming GPU resources,
 * those decisions can be made in the CS itself, thereby reducing duplicated
 * code.
 */

/**
 * kbase_ctx_sched_init() - Initialise the context scheduler
 * @kbdev: The device for which the context scheduler needs to be initialised
 *
 * This must be called during device initialisation. The number of hardware
 * address spaces must already be established before calling this function.
 *
 * Return: 0 for success, otherwise failure
 */
int kbase_ctx_sched_init(struct kbase_device *kbdev);

/**
 * kbase_ctx_sched_term - Terminate the context scheduler
 * @kbdev: The device for which the context scheduler needs to be terminated
 *
 * This must be called during device termination after all contexts have been
 * destroyed.
 */
void kbase_ctx_sched_term(struct kbase_device *kbdev);

/**
 * kbase_ctx_sched_ctx_init - Initialize per-context data fields for scheduling
 * @kctx: The context to initialize
 *
 * This must be called during context initialization before any other context
 * scheduling functions are called on @kctx
 */
void kbase_ctx_sched_init_ctx(struct kbase_context *kctx);

/**
 * kbase_ctx_sched_retain_ctx - Retain a reference to the @ref kbase_context
 * @kctx: The context to which to retain a reference
 *
 * This function should be called whenever an address space should be assigned
 * to a context and programmed onto the MMU. It should typically be called
 * when jobs are ready to be submitted to the GPU.
 *
 * It can be called as many times as necessary. The address space will be
 * assigned to the context for as long as there is a reference to said context.
 *
 * The kbase_device::mmu_hw_mutex and kbase_device::hwaccess_lock locks must be
 * held whilst calling this function.
 *
 * Return: The address space that the context has been assigned to or
 *         KBASEP_AS_NR_INVALID if no address space was available.
 */
int kbase_ctx_sched_retain_ctx(struct kbase_context *kctx);

/**
 * kbase_ctx_sched_retain_ctx_refcount - Retain a reference to the @ref kbase_context
 * @kctx: The context to which to retain a reference
 *
 * This function only retains a reference to the context. It must be called
 * only when the context already has a reference.
 *
 * This is typically called inside an atomic session where we know the context
 * is already scheduled in but want to take an extra reference to ensure that
 * it doesn't get descheduled.
 *
 * The kbase_device::hwaccess_lock must be held whilst calling this function
 */
void kbase_ctx_sched_retain_ctx_refcount(struct kbase_context *kctx);

/**
 * kbase_ctx_sched_release_ctx - Release a reference to the @ref kbase_context
 * @kctx: The context from which to release a reference
 *
 * This function should be called whenever an address space could be unassigned
 * from a context. When there are no more references to said context, the
 * address space previously assigned to this context shall be reassigned to
 * other contexts as needed.
 *
 * The kbase_device::hwaccess_lock must be held whilst calling this function
 */
void kbase_ctx_sched_release_ctx(struct kbase_context *kctx);

/**
 * kbase_ctx_sched_remove_ctx - Unassign previously assigned address space
 * @kctx: The context to be removed
 *
 * This function should be called when a context is being destroyed. The
 * context must no longer have any reference. If it has been assigned an
 * address space before then the AS will be unprogrammed.
 */
void kbase_ctx_sched_remove_ctx(struct kbase_context *kctx);

/**
 * kbase_ctx_sched_restore_all_as - Reprogram all address spaces
 * @kbdev: The device for which address spaces to be reprogrammed
 *
 * This function shall reprogram all address spaces previously assigned to
 * contexts. It can be used after the GPU is reset.
 *
 * The kbase_device::mmu_hw_mutex and kbase_device::hwaccess_lock locks must be
 * held whilst calling this function.
 */
void kbase_ctx_sched_restore_all_as(struct kbase_device *kbdev);

/**
 * kbase_ctx_sched_as_to_ctx_refcount - Lookup a context based on its current
 * address space and ensure that is stays scheduled in
 * @kbdev: The device for which the returned context must belong
 * @as_nr: address space assigned to the context of interest
 *
 * The context is refcounted as being busy to prevent it from scheduling
 * out. It must be released with kbase_ctx_sched_release_ctx() when it is no
 * longer required to stay scheduled in.
 *
 * This function can safely be called from IRQ context.
 *
 * The following locking conditions are made on the caller:
 * * it must not hold the kbase_device::hwaccess_lock, because it will be used
 *   internally.
 *
 * Return: a valid struct kbase_context on success, which has been refcounted
 * as being busy or return NULL on failure, indicating that no context was found
 * in as_nr.
 */
struct kbase_context *kbase_ctx_sched_as_to_ctx_refcount(
		struct kbase_device *kbdev, size_t as_nr);

/**
 * kbase_ctx_sched_as_to_ctx - Lookup a context based on its current address
 * space
 * @kbdev: The device for which the returned context must belong
 * @as_nr: address space assigned to the context of interest
 *
 * Return: a valid struct kbase_context on success or NULL on failure,
 * indicating that no context was found in as_nr.
 */
struct kbase_context *kbase_ctx_sched_as_to_ctx(struct kbase_device *kbdev,
		size_t as_nr);

/**
 * kbase_ctx_sched_as_to_ctx_nolock - Lookup a context based on its current
 * address space.
 * @kbdev: The device for which the returned context must belong
 * @as_nr: address space assigned to the context of interest
 *
 * The following lock must be held by the caller:
 * * kbase_device::hwaccess_lock
 *
 * Return: a valid struct kbase_context on success or NULL on failure,
 * indicating that no context was found in as_nr.
 */
struct kbase_context *kbase_ctx_sched_as_to_ctx_nolock(
		struct kbase_device *kbdev, size_t as_nr);

/**
 * kbase_ctx_sched_inc_refcount_nolock - Refcount a context as being busy,
 * preventing it from being scheduled out.
 * @kctx: Context to be refcounted
 *
 * The following locks must be held by the caller:
 * &kbase_device.mmu_hw_mutex
 * &kbase_device.hwaccess_lock
 *
 * Return: true if refcount succeeded, and the context will not be scheduled
 * out, false if the refcount failed (because the context is being/has been
 * scheduled out).
 */
bool kbase_ctx_sched_inc_refcount_nolock(struct kbase_context *kctx);

/**
 * kbase_ctx_sched_inc_refcount - Refcount a context as being busy, preventing
 * it from being scheduled out.
 * @kctx: Context to be refcounted
 *
 * The following locking conditions are made on the caller:
 * * it must not hold kbase_device::mmu_hw_mutex and
 *   kbase_device::hwaccess_lock, because they will be used internally.
 *
 * Return: true if refcount succeeded, and the context will not be scheduled
 * out, false if the refcount failed (because the context is being/has been
 * scheduled out).
 */
bool kbase_ctx_sched_inc_refcount(struct kbase_context *kctx);

/**
 * kbase_ctx_sched_release_ctx_lock - Release a reference count of a context
 * @kctx: Context for which refcount should be decreased
 *
 * Effectivelly, this is a wrapper for kbase_ctx_sched_release_ctx, but
 * kbase_device::hwaccess_lock is required NOT to be locked.
 */
void kbase_ctx_sched_release_ctx_lock(struct kbase_context *kctx);

#if MALI_USE_CSF
/**
 * kbase_ctx_sched_inc_refcount_if_as_valid - Refcount the context if it has GPU
 *                                            address space slot assigned to it.
 *
 * @kctx: Context to be refcounted
 *
 * This function takes a reference on the context if it has a GPU address space
 * slot assigned to it. The address space slot will not be available for
 * re-assignment until the reference is released.
 *
 * Return: true if refcount succeeded and the address space slot will not be
 * reassigned, false if the refcount failed (because the address space slot
 * was not assigned).
 */
bool kbase_ctx_sched_inc_refcount_if_as_valid(struct kbase_context *kctx);
#endif

#endif /* _KBASE_CTX_SCHED_H_ */
