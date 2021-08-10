/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2011-2017, 2019-2021 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CONTEXT_H_
#define _KBASE_CONTEXT_H_

#include <linux/atomic.h>

/**
 * kbase_context_debugfs_init - Initialize the kctx platform
 *                              specific debugfs
 *
 * @kctx: kbase context
 *
 * This initializes some debugfs interfaces specific to the platform the source
 * is compiled for.
 */
void kbase_context_debugfs_init(struct kbase_context *const kctx);

/**
 * kbase_context_debugfs_term - Terminate the kctx platform
 *                              specific debugfs
 *
 * @kctx: kbase context
 *
 * This terminates some debugfs interfaces specific to the platform the source
 * is compiled for.
 */
void kbase_context_debugfs_term(struct kbase_context *const kctx);

/**
 * kbase_create_context() - Create a kernel base context.
 *
 * @kbdev:       Object representing an instance of GPU platform device,
 *               allocated from the probe method of the Mali driver.
 * @is_compat:   Force creation of a 32-bit context
 * @flags:       Flags to set, which shall be any combination of
 *               BASEP_CONTEXT_CREATE_KERNEL_FLAGS.
 * @api_version: Application program interface version, as encoded in
 *               a single integer by the KBASE_API_VERSION macro.
 * @filp:        Pointer to the struct file corresponding to device file
 *               /dev/malixx instance, passed to the file's open method.
 *
 * Up to one context can be created for each client that opens the device file
 * /dev/malixx. Context creation is deferred until a special ioctl() system call
 * is made on the device file. Each context has its own GPU address space.
 *
 * Return: new kbase context or NULL on failure
 */
struct kbase_context *
kbase_create_context(struct kbase_device *kbdev, bool is_compat,
	base_context_create_flags const flags,
	unsigned long api_version,
	struct file *filp);

/**
 * kbase_destroy_context - Destroy a kernel base context.
 * @kctx: Context to destroy
 *
 * Will release all outstanding regions.
 */
void kbase_destroy_context(struct kbase_context *kctx);

/**
 * kbase_ctx_flag - Check if @flag is set on @kctx
 * @kctx: Pointer to kbase context to check
 * @flag: Flag to check
 *
 * Return: true if @flag is set on @kctx, false if not.
 */
static inline bool kbase_ctx_flag(struct kbase_context *kctx,
				      enum kbase_context_flags flag)
{
	return atomic_read(&kctx->flags) & flag;
}

/**
 * kbase_ctx_flag_clear - Clear @flag on @kctx
 * @kctx: Pointer to kbase context
 * @flag: Flag to clear
 *
 * Clear the @flag on @kctx. This is done atomically, so other flags being
 * cleared or set at the same time will be safe.
 *
 * Some flags have locking requirements, check the documentation for the
 * respective flags.
 */
static inline void kbase_ctx_flag_clear(struct kbase_context *kctx,
					enum kbase_context_flags flag)
{
	atomic_andnot(flag, &kctx->flags);
}

/**
 * kbase_ctx_flag_set - Set @flag on @kctx
 * @kctx: Pointer to kbase context
 * @flag: Flag to set
 *
 * Set the @flag on @kctx. This is done atomically, so other flags being
 * cleared or set at the same time will be safe.
 *
 * Some flags have locking requirements, check the documentation for the
 * respective flags.
 */
static inline void kbase_ctx_flag_set(struct kbase_context *kctx,
				      enum kbase_context_flags flag)
{
	atomic_or(flag, &kctx->flags);
}
#endif /* _KBASE_CONTEXT_H_ */
