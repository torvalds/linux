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

/*
 * mali_kbase_kinstr_jm.h
 * Kernel driver public interface to job manager atom tracing. This API provides
 * a method to get the atom state changes into user space.
 *
 * The flow of operation is:
 *
 * | kernel                              | user                                |
 * | ----------------------------------- | ----------------------------------- |
 * | Initialize API with                 |                                     |
 * | kbase_kinstr_jm_init()              |                                     |
 * |                                     |                                     |
 * | Kernel code injects states with     |                                     |
 * | kbase_kinstr_jm_atom_state_*() APIs |                                     |
 * |                                     | Call ioctl() to get file descriptor |
 * |                                     | via KBASE_IOCTL_KINSTR_JM_FD        |
 * | Allocates a reader attached to FD   |                                     |
 * | Allocates circular buffer and       |                                     |
 * | patches, via ASM goto, the          |                                     |
 * | kbase_kinstr_jm_atom_state_*()      |                                     |
 * |                                     | loop:                               |
 * |                                     |   Call poll() on FD for POLLIN      |
 * |   When threshold of changes is hit, |                                     |
 * |   the poll is interrupted with      |                                     |
 * |   POLLIN. If circular buffer is     |                                     |
 * |   full then store the missed count  |                                     |
 * |   and interrupt poll                |   Call read() to get data from      |
 * |                                     |   circular buffer via the fd        |
 * |   Kernel advances tail of circular  |                                     |
 * |   buffer                            |                                     |
 * |                                     | Close file descriptor               |
 * | Deallocates circular buffer         |                                     |
 * |                                     |                                     |
 * | Terminate API with                  |                                     |
 * | kbase_kinstr_jm_term()              |                                     |
 *
 * All tracepoints are guarded on a static key. The static key is activated when
 * a user space reader gets created. This means that there is negligible cost
 * inserting the tracepoints into code when there are no readers.
 */

#ifndef _KBASE_KINSTR_JM_H_
#define _KBASE_KINSTR_JM_H_

#include <uapi/gpu/arm/bifrost/mali_kbase_kinstr_jm_reader.h>

#ifdef __KERNEL__
#include <linux/version.h>
#include <linux/static_key.h>
#else
/* empty wrapper macros for userspace */
#define static_branch_unlikely(key) (1)
#define KERNEL_VERSION(a, b, c) (0)
#define LINUX_VERSION_CODE (1)
#endif /* __KERNEL__ */

/* Forward declarations */
struct kbase_context;
struct kbase_kinstr_jm;
struct kbase_jd_atom;
union kbase_kinstr_jm_fd;

/**
 * kbase_kinstr_jm_init() - Initialise an instrumentation job manager context.
 * @ctx: Non-NULL pointer to where the pointer to the created context will
 *       be stored on success.
 *
 * Return: 0 on success, else error code.
 */
int kbase_kinstr_jm_init(struct kbase_kinstr_jm **ctx);

/**
 * kbase_kinstr_jm_term() - Terminate an instrumentation job manager context.
 * @ctx: Pointer to context to be terminated.
 */
void kbase_kinstr_jm_term(struct kbase_kinstr_jm *ctx);

/**
 * kbase_kinstr_jm_get_fd() - Retrieves a file descriptor that can be used to
 * read the atom state changes from userspace
 *
 * @ctx: Pointer to the initialized context
 * @jm_fd_arg: Pointer to the union containing the in/out params
 * Return: -1 on failure, valid file descriptor on success
 */
int kbase_kinstr_jm_get_fd(struct kbase_kinstr_jm *const ctx,
			   union kbase_kinstr_jm_fd *jm_fd_arg);

/**
 * kbasep_kinstr_jm_atom_state() - Signifies that an atom has changed state
 * @atom: The atom that has changed state
 * @state: The new state of the atom
 *
 * This performs the actual storage of the state ready for user space to
 * read the data. It is only called when the static key is enabled from
 * kbase_kinstr_jm_atom_state(). There is almost never a need to invoke this
 * function directly.
 */
void kbasep_kinstr_jm_atom_state(
	struct kbase_jd_atom *const atom,
	const enum kbase_kinstr_jm_reader_atom_state state);

/* Allows ASM goto patching to reduce tracing overhead. This is
 * incremented/decremented when readers are created and terminated. This really
 * shouldn't be changed externally, but if you do, make sure you use
 * a static_key_inc()/static_key_dec() pair.
 */
extern struct static_key_false basep_kinstr_jm_reader_static_key;

/**
 * kbase_kinstr_jm_atom_state() - Signifies that an atom has changed state
 * @atom: The atom that has changed state
 * @state: The new state of the atom
 *
 * This uses a static key to reduce overhead when tracing is disabled
 */
static inline void kbase_kinstr_jm_atom_state(
	struct kbase_jd_atom *const atom,
	const enum kbase_kinstr_jm_reader_atom_state state)
{
	if (static_branch_unlikely(&basep_kinstr_jm_reader_static_key))
		kbasep_kinstr_jm_atom_state(atom, state);
}

/**
 * kbase_kinstr_jm_atom_state_queue() - Signifies that an atom has entered a
 *                                      hardware or software queue.
 * @atom: The atom that has changed state
 */
static inline void kbase_kinstr_jm_atom_state_queue(
	struct kbase_jd_atom *const atom)
{
	kbase_kinstr_jm_atom_state(
		atom, KBASE_KINSTR_JM_READER_ATOM_STATE_QUEUE);
}

/**
 * kbase_kinstr_jm_atom_state_start() - Signifies that work has started on an
 *                                      atom
 * @atom: The atom that has changed state
 */
static inline void kbase_kinstr_jm_atom_state_start(
	struct kbase_jd_atom *const atom)
{
	kbase_kinstr_jm_atom_state(
		atom, KBASE_KINSTR_JM_READER_ATOM_STATE_START);
}

/**
 * kbase_kinstr_jm_atom_state_stop() - Signifies that work has stopped on an
 *                                     atom
 * @atom: The atom that has changed state
 */
static inline void kbase_kinstr_jm_atom_state_stop(
	struct kbase_jd_atom *const atom)
{
	kbase_kinstr_jm_atom_state(
		atom, KBASE_KINSTR_JM_READER_ATOM_STATE_STOP);
}

/**
 * kbase_kinstr_jm_atom_state_complete() - Signifies that all work has completed
 *                                         on an atom
 * @atom: The atom that has changed state
 */
static inline void kbase_kinstr_jm_atom_state_complete(
	struct kbase_jd_atom *const atom)
{
	kbase_kinstr_jm_atom_state(
		atom, KBASE_KINSTR_JM_READER_ATOM_STATE_COMPLETE);
}

/**
 * kbase_kinstr_jm_atom_queue() - A software *or* hardware atom is queued for
 *                                execution
 * @atom: The atom that has changed state
 */
static inline void kbase_kinstr_jm_atom_queue(struct kbase_jd_atom *const atom)
{
	kbase_kinstr_jm_atom_state_queue(atom);
}

/**
 * kbase_kinstr_jm_atom_complete() - A software *or* hardware atom is fully
 *                                   completed
 * @atom: The atom that has changed state
 */
static inline void kbase_kinstr_jm_atom_complete(
	struct kbase_jd_atom *const atom)
{
	kbase_kinstr_jm_atom_state_complete(atom);
}

/**
 * kbase_kinstr_jm_atom_sw_start() - A software atom has started work
 * @atom: The atom that has changed state
 */
static inline void kbase_kinstr_jm_atom_sw_start(
	struct kbase_jd_atom *const atom)
{
	kbase_kinstr_jm_atom_state_start(atom);
}

/**
 * kbase_kinstr_jm_atom_sw_stop() - A software atom has stopped work
 * @atom: The atom that has changed state
 */
static inline void kbase_kinstr_jm_atom_sw_stop(
	struct kbase_jd_atom *const atom)
{
	kbase_kinstr_jm_atom_state_stop(atom);
}

/**
 * kbasep_kinstr_jm_atom_hw_submit() - A hardware atom has been submitted
 * @atom: The atom that has been submitted
 *
 * This private implementation should not be called directly, it is protected
 * by a static key in kbase_kinstr_jm_atom_hw_submit(). Use that instead.
 */
void kbasep_kinstr_jm_atom_hw_submit(struct kbase_jd_atom *const atom);

/**
 * kbase_kinstr_jm_atom_hw_submit() - A hardware atom has been submitted
 * @atom: The atom that has been submitted
 */
static inline void kbase_kinstr_jm_atom_hw_submit(
	struct kbase_jd_atom *const atom)
{
	if (static_branch_unlikely(&basep_kinstr_jm_reader_static_key))
		kbasep_kinstr_jm_atom_hw_submit(atom);
}

/**
 * kbasep_kinstr_jm_atom_hw_release() - A hardware atom has been released
 * @atom: The atom that has been released
 *
 * This private implementation should not be called directly, it is protected
 * by a static key in kbase_kinstr_jm_atom_hw_release(). Use that instead.
 */
void kbasep_kinstr_jm_atom_hw_release(struct kbase_jd_atom *const atom);

/**
 * kbase_kinstr_jm_atom_hw_release() - A hardware atom has been released
 * @atom: The atom that has been released
 */
static inline void kbase_kinstr_jm_atom_hw_release(
	struct kbase_jd_atom *const atom)
{
	if (static_branch_unlikely(&basep_kinstr_jm_reader_static_key))
		kbasep_kinstr_jm_atom_hw_release(atom);
}

#endif /* _KBASE_KINSTR_JM_H_ */
