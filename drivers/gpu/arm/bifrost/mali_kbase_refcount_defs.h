/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2023 ARM Limited. All rights reserved.
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

#ifndef _KBASE_REFCOUNT_DEFS_H_
#define _KBASE_REFCOUNT_DEFS_H_

/*
 * The Refcount API is available from 4.11 onwards
 * This file hides the compatibility issues with this for the rest the driver
 */

#include <linux/version.h>
#include <linux/types.h>

#if (KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE)

#define kbase_refcount_t atomic_t
#define kbase_refcount_read(x) atomic_read(x)
#define kbase_refcount_set(x, v) atomic_set(x, v)
#define kbase_refcount_dec_and_test(x) atomic_dec_and_test(x)
#define kbase_refcount_dec(x) atomic_dec(x)
#define kbase_refcount_inc_not_zero(x) atomic_inc_not_zero(x)
#define kbase_refcount_inc(x) atomic_inc(x)

#else

#include <linux/refcount.h>

#define kbase_refcount_t refcount_t
#define kbase_refcount_read(x) refcount_read(x)
#define kbase_refcount_set(x, v) refcount_set(x, v)
#define kbase_refcount_dec_and_test(x) refcount_dec_and_test(x)
#define kbase_refcount_dec(x) refcount_dec(x)
#define kbase_refcount_inc_not_zero(x) refcount_inc_not_zero(x)
#define kbase_refcount_inc(x) refcount_inc(x)

#endif /* (KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE) */

#endif /* _KBASE_REFCOUNT_DEFS_H_ */
