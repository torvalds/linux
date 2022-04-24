/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014-2015, 2018, 2020-2022 ARM Limited. All rights reserved.
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
 * Kernel-wide include for common macros and types.
 */

#ifndef _MALISW_H_
#define _MALISW_H_

#include <linux/version.h>

/**
 * MIN - Return the lesser of two values.
 * @x: value1
 * @y: value2
 *
 * As a macro it may evaluate its arguments more than once.
 * Refer to MAX macro for more details
 */
#define MIN(x, y)	((x) < (y) ? (x) : (y))

/**
 * MAX - Return the greater of two values.
 * @x: value1
 * @y: value2
 *
 * As a macro it may evaluate its arguments more than once.
 * If called on the same two arguments as MIN it is guaranteed to return
 * the one that MIN didn't return. This is significant for types where not
 * all values are comparable e.g. NaNs in floating-point types. But if you want
 * to retrieve the min and max of two values, consider using a conditional swap
 * instead.
 */
#define MAX(x, y)	((x) < (y) ? (y) : (x))

/**
 * CSTD_UNUSED - Function-like macro for suppressing unused variable warnings.
 *
 * @x: unused variable
 *
 * Where possible such variables should be removed; this macro is present for
 * cases where we much support API backwards compatibility.
 */
#define CSTD_UNUSED(x)	((void)(x))

/**
 * CSTD_NOP - Function-like macro for use where "no behavior" is desired.
 * @...: no-op
 *
 * This is useful when compile time macros turn a function-like macro in to a
 * no-op, but where having no statement is otherwise invalid.
 */
#define CSTD_NOP(...)	((void)#__VA_ARGS__)

/**
 * CSTD_STR1 - Function-like macro for stringizing a single level macro.
 * @x: macro's value
 *
 * @code
 * #define MY_MACRO 32
 * CSTD_STR1( MY_MACRO )
 * > "MY_MACRO"
 * @endcode
 */
#define CSTD_STR1(x)	#x

/**
 * CSTD_STR2 - Function-like macro for stringizing a macro's value.
 * @x: macro's value
 *
 * This should not be used if the macro is defined in a way which may have no
 * value; use the alternative @c CSTD_STR2N macro should be used instead.
 * @code
 * #define MY_MACRO 32
 * CSTD_STR2( MY_MACRO )
 * > "32"
 * @endcode
 */
#define CSTD_STR2(x)	CSTD_STR1(x)

/* LINUX_VERSION_CODE < 5.4 */
#if (KERNEL_VERSION(5, 4, 0) > LINUX_VERSION_CODE)
#if defined(GCC_VERSION) && GCC_VERSION >= 70000
#ifndef __fallthrough
#define __fallthrough  __attribute__((fallthrough))
#endif /* __fallthrough */
#define fallthrough    __fallthrough
#else
#define fallthrough	   CSTD_NOP(...) /* fallthrough */
#endif /* GCC_VERSION >= 70000 */
#endif /* KERNEL_VERSION(5, 4, 0) */

#endif /* _MALISW_H_ */
