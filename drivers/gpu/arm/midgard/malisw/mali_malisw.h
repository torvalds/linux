/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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





#ifndef _MALISW_H_
#define _MALISW_H_

/**
 * @file mali_malisw.h
 * Driver-wide include for common macros and types.
 */

/**
 * @defgroup malisw Mali software definitions and types
 * @{
 */

#include <stddef.h>

#include "mali_stdtypes.h"

/** @brief Gets the container object when given a pointer to a member of an object. */
#define CONTAINER_OF(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type,member)))

/** @brief Gets the number of elements of type s in a fixed length array of s */
#define NELEMS(s)       (sizeof(s)/sizeof((s)[0]))

/**
 * @brief The lesser of two values.
 * May evaluate its arguments more than once.
 * @see CSTD_MIN
 */
#define MIN(x,y) CSTD_MIN(x,y)

/**
 * @brief The greater of two values.
 * May evaluate its arguments more than once.
 * @see CSTD_MAX
 */
#define MAX(x,y) CSTD_MAX(x,y)

/**
 * @brief Clamp value x to within min and max inclusive
 * May evaluate its arguments more than once.
 * @see CSTD_CLAMP
 */
#define CLAMP( x, min, max ) CSTD_CLAMP( x, min, max )

/**
 * @brief Convert a pointer into a u64 for storing in a data structure.
 * This is commonly used when pairing a 32-bit CPU with a 64-bit peripheral,
 * such as a Midgard GPU. C's type promotion is complex and a straight cast
 * does not work reliably as pointers are often considered as signed.
 */
#define PTR_TO_U64( x ) CSTD_PTR_TO_U64( x )

/**
 * @name Mali library linkage specifiers
 * These directly map to the cstd versions described in detail here: @ref arm_cstd_linkage_specifiers
 * @{
 */
#define MALI_IMPORT CSTD_LINK_IMPORT
#define MALI_EXPORT CSTD_LINK_EXPORT
#define MALI_IMPL   CSTD_LINK_IMPL
#define MALI_LOCAL  CSTD_LINK_LOCAL

/** @brief Decorate exported function prototypes.
 *
 * The file containing the implementation of the function should define this to be MALI_EXPORT before including
 * malisw/mali_malisw.h.
 */
#ifndef MALI_API
#define MALI_API MALI_IMPORT
#endif
/** @} */

/** @name Testable static functions
 * @{
 *
 * These macros can be used to allow functions to be static in release builds but exported from a shared library in unit
 * test builds, allowing them to be tested or used to assist testing.
 *
 * Example mali_foo_bar.c containing the function to test:
 *
 * @code
 * #define MALI_API MALI_EXPORT
 *
 * #include <malisw/mali_malisw.h>
 * #include "mali_foo_testable_statics.h"
 *
 * MALI_TESTABLE_STATIC_IMPL void my_func()
 * {
 *	//Implementation
 * }
 * @endcode
 *
 * Example mali_foo_testable_statics.h:
 *
 * @code
 * #if 1 == MALI_UNIT_TEST
 * #include <malisw/mali_malisw.h>
 *
 * MALI_TESTABLE_STATIC_API void my_func();
 *
 * #endif
 * @endcode
 *
 * Example mali_foo_tests.c:
 *
 * @code
 * #include <foo/src/mali_foo_testable_statics.h>
 *
 * void my_test_func()
 * {
 * 	my_func();
 * }
 * @endcode
 */

/** @brief Decorate testable static function implementations.
 *
 * A header file containing a MALI_TESTABLE_STATIC_API-decorated prototype for each static function will be required
 * when MALI_UNIT_TEST == 1 in order to link the function from the test.
 */
#if 1 == MALI_UNIT_TEST
#define MALI_TESTABLE_STATIC_IMPL MALI_IMPL
#else
#define MALI_TESTABLE_STATIC_IMPL static
#endif

/** @brief Decorate testable static function prototypes.
 *
 * @note Prototypes should @em only be declared when MALI_UNIT_TEST == 1
 */
#define MALI_TESTABLE_STATIC_API MALI_API
/** @} */

/** @name Testable local functions
 * @{
 *
 * These macros can be used to allow functions to be local to a shared library in release builds but be exported in unit
 * test builds, allowing them to be tested or used to assist testing.
 *
 * Example mali_foo_bar.c containing the function to test:
 *
 * @code
 * #define MALI_API MALI_EXPORT
 *
 * #include <malisw/mali_malisw.h>
 * #include "mali_foo_bar.h"
 *
 * MALI_TESTABLE_LOCAL_IMPL void my_func()
 * {
 *	//Implementation
 * }
 * @endcode
 *
 * Example mali_foo_bar.h:
 *
 * @code
 * #include <malisw/mali_malisw.h>
 *
 * MALI_TESTABLE_LOCAL_API void my_func();
 *
 * @endcode
 *
 * Example mali_foo_tests.c:
 *
 * @code
 * #include <foo/src/mali_foo_bar.h>
 *
 * void my_test_func()
 * {
 * 	my_func();
 * }
 * @endcode
 */

/** @brief Decorate testable local function implementations.
 *
 * This can be used to have a function normally local to the shared library except in debug builds where it will be
 * exported.
 */
#ifdef CONFIG_MALI_DEBUG
#define MALI_TESTABLE_LOCAL_IMPL MALI_IMPL
#else
#define MALI_TESTABLE_LOCAL_IMPL MALI_LOCAL
#endif /* CONFIG_MALI_DEBUG */

/** @brief Decorate testable local function prototypes.
 *
 * This can be used to have a function normally local to the shared library except in debug builds where it will be
 * exported.
 */
#ifdef CONFIG_MALI_DEBUG
#define MALI_TESTABLE_LOCAL_API MALI_API
#else
#define MALI_TESTABLE_LOCAL_API MALI_LOCAL
#endif /* CONFIG_MALI_DEBUG */
/** @} */

/**
 * Flag a cast as a reinterpretation, usually of a pointer type.
 * @see CSTD_REINTERPRET_CAST
 */
#define REINTERPRET_CAST(type) CSTD_REINTERPRET_CAST(type)

/**
 * Flag a cast as casting away const, usually of a pointer type.
 * @see CSTD_CONST_CAST
 */
#define CONST_CAST(type) (type) CSTD_CONST_CAST(type)

/**
 * Flag a cast as a (potentially complex) value conversion, usually of a numerical type.
 * @see CSTD_STATIC_CAST
 */
#define STATIC_CAST(type) (type) CSTD_STATIC_CAST(type)


/** @} */

#endif /* _MALISW_H_ */
