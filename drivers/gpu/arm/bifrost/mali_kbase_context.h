/*
 *
 * (C) COPYRIGHT 2011-2016 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#ifndef _KBASE_CONTEXT_H_
#define _KBASE_CONTEXT_H_

#include <linux/atomic.h>


int kbase_context_set_create_flags(struct kbase_context *kctx, u32 flags);

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
#if KERNEL_VERSION(4, 3, 0) > LINUX_VERSION_CODE
	/*
	 * Earlier kernel versions doesn't have atomic_andnot() or
	 * atomic_and(). atomic_clear_mask() was only available on some
	 * architectures and removed on arm in v3.13 on arm and arm64.
	 *
	 * Use a compare-exchange loop to clear the flag on pre 4.3 kernels,
	 * when atomic_andnot() becomes available.
	 */
	int old, new;

	do {
		old = atomic_read(&kctx->flags);
		new = old & ~flag;

	} while (atomic_cmpxchg(&kctx->flags, old, new) != old);
#else
	atomic_andnot(flag, &kctx->flags);
#endif
}

/**
 * kbase_ctx_flag_set - Set @flag on @kctx
 * @kctx: Pointer to kbase context
 * @flag: Flag to clear
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
