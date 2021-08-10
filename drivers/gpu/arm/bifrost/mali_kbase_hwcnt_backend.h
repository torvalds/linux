/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2018, 2020-2021 ARM Limited. All rights reserved.
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
 * Virtual interface for hardware counter backends.
 */

#ifndef _KBASE_HWCNT_BACKEND_H_
#define _KBASE_HWCNT_BACKEND_H_

#include <linux/types.h>

struct kbase_hwcnt_metadata;
struct kbase_hwcnt_enable_map;
struct kbase_hwcnt_dump_buffer;

/*
 * struct kbase_hwcnt_backend_info - Opaque pointer to information used to
 *                                   create an instance of a hardware counter
 *                                   backend.
 */
struct kbase_hwcnt_backend_info;

/*
 * struct kbase_hwcnt_backend - Opaque pointer to a hardware counter
 *                              backend, used to perform dumps.
 */
struct kbase_hwcnt_backend;

/*
 * typedef kbase_hwcnt_backend_metadata_fn - Get the immutable hardware counter
 *                                           metadata that describes the layout
 *                                           of the counter data structures.
 * @info:        Non-NULL pointer to backend info.
 *
 * Multiple calls to this function with the same info are guaranteed to return
 * the same metadata object each time.
 *
 * Return: Non-NULL pointer to immutable hardware counter metadata.
 */
typedef const struct kbase_hwcnt_metadata *
kbase_hwcnt_backend_metadata_fn(const struct kbase_hwcnt_backend_info *info);

/**
 * typedef kbase_hwcnt_backend_init_fn - Initialise a counter backend.
 * @info:        Non-NULL pointer to backend info.
 * @out_backend: Non-NULL pointer to where backend is stored on success.
 *
 * All uses of the created hardware counter backend must be externally
 * synchronised.
 *
 * Return: 0 on success, else error code.
 */
typedef int
kbase_hwcnt_backend_init_fn(const struct kbase_hwcnt_backend_info *info,
			    struct kbase_hwcnt_backend **out_backend);

/**
 * typedef kbase_hwcnt_backend_term_fn - Terminate a counter backend.
 * @backend: Pointer to backend to be terminated.
 */
typedef void kbase_hwcnt_backend_term_fn(struct kbase_hwcnt_backend *backend);

/**
 * typedef kbase_hwcnt_backend_timestamp_ns_fn - Get the current backend
 *                                               timestamp.
 * @backend: Non-NULL pointer to backend.
 *
 * Return: Backend timestamp in nanoseconds.
 */
typedef u64
kbase_hwcnt_backend_timestamp_ns_fn(struct kbase_hwcnt_backend *backend);

/**
 * typedef kbase_hwcnt_backend_dump_enable_fn - Start counter dumping with the
 *                                              backend.
 * @backend:    Non-NULL pointer to backend.
 * @enable_map: Non-NULL pointer to enable map specifying enabled counters.
 *
 * The enable_map must have been created using the interface's metadata.
 * If the backend has already been enabled, an error is returned.
 *
 * May be called in an atomic context.
 *
 * Return: 0 on success, else error code.
 */
typedef int kbase_hwcnt_backend_dump_enable_fn(
	struct kbase_hwcnt_backend *backend,
	const struct kbase_hwcnt_enable_map *enable_map);

/**
 * typedef kbase_hwcnt_backend_dump_enable_nolock_fn - Start counter dumping
 *                                                     with the backend.
 * @backend:    Non-NULL pointer to backend.
 * @enable_map: Non-NULL pointer to enable map specifying enabled counters.
 *
 * Exactly the same as kbase_hwcnt_backend_dump_enable_fn(), except must be
 * called in an atomic context with the spinlock documented by the specific
 * backend interface held.
 *
 * Return: 0 on success, else error code.
 */
typedef int kbase_hwcnt_backend_dump_enable_nolock_fn(
	struct kbase_hwcnt_backend *backend,
	const struct kbase_hwcnt_enable_map *enable_map);

/**
 * typedef kbase_hwcnt_backend_dump_disable_fn - Disable counter dumping with
 *                                               the backend.
 * @backend: Non-NULL pointer to backend.
 *
 * If the backend is already disabled, does nothing.
 * Any undumped counter values since the last dump get will be lost.
 */
typedef void
kbase_hwcnt_backend_dump_disable_fn(struct kbase_hwcnt_backend *backend);

/**
 * typedef kbase_hwcnt_backend_dump_clear_fn - Reset all the current undumped
 *                                             counters.
 * @backend: Non-NULL pointer to backend.
 *
 * If the backend is not enabled, returns an error.
 *
 * Return: 0 on success, else error code.
 */
typedef int
kbase_hwcnt_backend_dump_clear_fn(struct kbase_hwcnt_backend *backend);

/**
 * typedef kbase_hwcnt_backend_dump_request_fn - Request an asynchronous counter
 *                                               dump.
 * @backend: Non-NULL pointer to backend.
 * @dump_time_ns: Non-NULL pointer where the timestamp of when the dump was
 *                requested will be written out to on success.
 *
 * If the backend is not enabled or another dump is already in progress,
 * returns an error.
 *
 * Return: 0 on success, else error code.
 */
typedef int
kbase_hwcnt_backend_dump_request_fn(struct kbase_hwcnt_backend *backend,
				    u64 *dump_time_ns);

/**
 * typedef kbase_hwcnt_backend_dump_wait_fn - Wait until the last requested
 *                                            counter dump has completed.
 * @backend: Non-NULL pointer to backend.
 *
 * If the backend is not enabled, returns an error.
 *
 * Return: 0 on success, else error code.
 */
typedef int
kbase_hwcnt_backend_dump_wait_fn(struct kbase_hwcnt_backend *backend);

/**
 * typedef kbase_hwcnt_backend_dump_get_fn - Copy or accumulate enable the
 *                                           counters dumped after the last dump
 *                                           request into the dump buffer.
 * @backend:     Non-NULL pointer to backend.
 * @dump_buffer: Non-NULL pointer to destination dump buffer.
 * @enable_map:  Non-NULL pointer to enable map specifying enabled values.
 * @accumulate:  True if counters should be accumulated into dump_buffer, rather
 *               than copied.
 *
 * The resultant contents of the dump buffer are only well defined if a prior
 * call to dump_wait returned successfully, and a new dump has not yet been
 * requested by a call to dump_request.
 *
 * Return: 0 on success, else error code.
 */
typedef int
kbase_hwcnt_backend_dump_get_fn(struct kbase_hwcnt_backend *backend,
				struct kbase_hwcnt_dump_buffer *dump_buffer,
				const struct kbase_hwcnt_enable_map *enable_map,
				bool accumulate);

/**
 * struct kbase_hwcnt_backend_interface - Hardware counter backend virtual
 *                                        interface.
 * @info:               Immutable info used to initialise an instance of the
 *                      backend.
 * @metadata:           Function ptr to get the immutable hardware counter
 *                      metadata.
 * @init:               Function ptr to initialise an instance of the backend.
 * @term:               Function ptr to terminate an instance of the backend.
 * @timestamp_ns:       Function ptr to get the current backend timestamp.
 * @dump_enable:        Function ptr to enable dumping.
 * @dump_enable_nolock: Function ptr to enable dumping while the
 *                      backend-specific spinlock is already held.
 * @dump_disable:       Function ptr to disable dumping.
 * @dump_clear:         Function ptr to clear counters.
 * @dump_request:       Function ptr to request a dump.
 * @dump_wait:          Function ptr to wait until dump to complete.
 * @dump_get:           Function ptr to copy or accumulate dump into a dump
 *                      buffer.
 */
struct kbase_hwcnt_backend_interface {
	const struct kbase_hwcnt_backend_info *info;
	kbase_hwcnt_backend_metadata_fn *metadata;
	kbase_hwcnt_backend_init_fn *init;
	kbase_hwcnt_backend_term_fn *term;
	kbase_hwcnt_backend_timestamp_ns_fn *timestamp_ns;
	kbase_hwcnt_backend_dump_enable_fn *dump_enable;
	kbase_hwcnt_backend_dump_enable_nolock_fn *dump_enable_nolock;
	kbase_hwcnt_backend_dump_disable_fn *dump_disable;
	kbase_hwcnt_backend_dump_clear_fn *dump_clear;
	kbase_hwcnt_backend_dump_request_fn *dump_request;
	kbase_hwcnt_backend_dump_wait_fn *dump_wait;
	kbase_hwcnt_backend_dump_get_fn *dump_get;
};

#endif /* _KBASE_HWCNT_BACKEND_H_ */
