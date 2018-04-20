/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



/**
 * Kernel-wide include for common macros and types.
 */

#ifndef _MALISW_H_
#define _MALISW_H_

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)
#define U8_MAX          ((u8)~0U)
#define S8_MAX          ((s8)(U8_MAX>>1))
#define S8_MIN          ((s8)(-S8_MAX - 1))
#define U16_MAX         ((u16)~0U)
#define S16_MAX         ((s16)(U16_MAX>>1))
#define S16_MIN         ((s16)(-S16_MAX - 1))
#define U32_MAX         ((u32)~0U)
#define S32_MAX         ((s32)(U32_MAX>>1))
#define S32_MIN         ((s32)(-S32_MAX - 1))
#define U64_MAX         ((u64)~0ULL)
#define S64_MAX         ((s64)(U64_MAX>>1))
#define S64_MIN         ((s64)(-S64_MAX - 1))
#endif /* LINUX_VERSION_CODE */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 5, 0)
#define SIZE_MAX        (~(size_t)0)
#endif /* LINUX_VERSION_CODE */

/**
 * MIN - Return the lesser of two values.
 *
 * As a macro it may evaluate its arguments more than once.
 * Refer to MAX macro for more details
 */
#define MIN(x, y)	((x) < (y) ? (x) : (y))

/**
 * MAX -  Return the greater of two values.
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
 * @hideinitializer
 * Function-like macro for suppressing unused variable warnings. Where possible
 * such variables should be removed; this macro is present for cases where we
 * much support API backwards compatibility.
 */
#define CSTD_UNUSED(x)	((void)(x))

/**
 * @hideinitializer
 * Function-like macro for use where "no behavior" is desired. This is useful
 * when compile time macros turn a function-like macro in to a no-op, but
 * where having no statement is otherwise invalid.
 */
#define CSTD_NOP(...)	((void)#__VA_ARGS__)

/**
 * Function-like macro for converting a pointer in to a u64 for storing into
 * an external data structure. This is commonly used when pairing a 32-bit
 * CPU with a 64-bit peripheral, such as a Midgard GPU. C's type promotion
 * is complex and a straight cast does not work reliably as pointers are
 * often considered as signed.
 */
#define PTR_TO_U64(x)	((uint64_t)((uintptr_t)(x)))

/**
 * @hideinitializer
 * Function-like macro for stringizing a single level macro.
 * @code
 * #define MY_MACRO 32
 * CSTD_STR1( MY_MACRO )
 * > "MY_MACRO"
 * @endcode
 */
#define CSTD_STR1(x)	#x

/**
 * @hideinitializer
 * Function-like macro for stringizing a macro's value. This should not be used
 * if the macro is defined in a way which may have no value; use the
 * alternative @c CSTD_STR2N macro should be used instead.
 * @code
 * #define MY_MACRO 32
 * CSTD_STR2( MY_MACRO )
 * > "32"
 * @endcode
 */
#define CSTD_STR2(x)	CSTD_STR1(x)

/**
 * Specify an assertion value which is evaluated at compile time. Recommended
 * usage is specification of a @c static @c INLINE function containing all of
 * the assertions thus:
 *
 * @code
 * static INLINE [module]_compile_time_assertions( void )
 * {
 *     COMPILE_TIME_ASSERT( sizeof(uintptr_t) == sizeof(intptr_t) );
 * }
 * @endcode
 *
 * @note Use @c static not @c STATIC. We never want to turn off this @c static
 * specification for testing purposes.
 */
#define CSTD_COMPILE_TIME_ASSERT(expr) \
	do { switch (0) { case 0: case (expr):; } } while (false)

#endif /* _MALISW_H_ */
