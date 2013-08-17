/*
 *
 * (C) COPYRIGHT 2007-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @addtogroup malisw
 * @{
 */

/* ============================================================================
    Description
============================================================================ */
/**
 * @defgroup arm_cstd_coding_standard ARM C standard types and constants
 * The common files are a set of standard headers which are used by all parts
 * of this development, describing types, and generic constants.
 *
 * Files in group:
 *     - arm_cstd.h
 *     - arm_cstd_compilers.h
 *     - arm_cstd_types.h
 *     - arm_cstd_types_rvct.h
 *     - arm_cstd_types_gcc.h
 *     - arm_cstd_types_msvc.h
 *     - arm_cstd_pack_push.h
 *     - arm_cstd_pack_pop.h
 */

/**
 * @addtogroup arm_cstd_coding_standard
 * @{
 */

#ifndef _ARM_CSTD_
#define _ARM_CSTD_

/* ============================================================================
	Import standard C99 types
============================================================================ */
#include "arm_cstd_compilers.h"
#include "arm_cstd_types.h"

/* ============================================================================
	Min and Max Values
============================================================================ */
#if !defined(INT8_MAX)
	#define INT8_MAX                ((int8_t) 0x7F)
#endif
#if !defined(INT8_MIN)
	#define INT8_MIN                (-INT8_MAX - 1)
#endif

#if !defined(INT16_MAX)
	#define INT16_MAX               ((int16_t)0x7FFF)
#endif
#if !defined(INT16_MIN)
	#define INT16_MIN               (-INT16_MAX - 1)
#endif

#if !defined(INT32_MAX)
	#define INT32_MAX               ((int32_t)0x7FFFFFFF)
#endif
#if !defined(INT32_MIN)
	#define INT32_MIN               (-INT32_MAX - 1)
#endif

#if !defined(INT64_MAX)
	#define INT64_MAX               ((int64_t)0x7FFFFFFFFFFFFFFFLL)
#endif
#if !defined(INT64_MIN)
	#define INT64_MIN               (-INT64_MAX - 1)
#endif

#if !defined(UINT8_MAX)
	#define UINT8_MAX               ((uint8_t) 0xFF)
#endif

#if !defined(UINT16_MAX)
	#define UINT16_MAX              ((uint16_t)0xFFFF)
#endif

#if !defined(UINT32_MAX)
	#define UINT32_MAX              ((uint32_t)0xFFFFFFFF)
#endif

#if !defined(UINT64_MAX)
	#define UINT64_MAX              ((uint64_t)0xFFFFFFFFFFFFFFFFULL)
#endif

/* fallbacks if limits.h wasn't available */
#if !defined(UCHAR_MAX)
	#define UCHAR_MAX               ((unsigned char)~0U)
#endif

#if !defined(SCHAR_MAX)
	#define SCHAR_MAX               ((signed char)(UCHAR_MAX >> 1))
#endif
#if !defined(SCHAR_MIN)
	#define SCHAR_MIN               ((signed char)(-SCHAR_MAX - 1))
#endif

#if !defined(USHRT_MAX)
	#define USHRT_MAX               ((unsigned short)~0U)
#endif

#if !defined(SHRT_MAX)
	#define SHRT_MAX                ((signed short)(USHRT_MAX >> 1))
#endif
#if !defined(SHRT_MIN)
	#define SHRT_MIN                ((signed short)(-SHRT_MAX - 1))
#endif

#if !defined(UINT_MAX)
	#define UINT_MAX                ((unsigned int)~0U)
#endif

#if !defined(INT_MAX)
	#define INT_MAX                 ((signed int)(UINT_MAX >> 1))
#endif
#if !defined(INT_MIN)
	#define INT_MIN                 ((signed int)(-INT_MAX - 1))
#endif

#if !defined(ULONG_MAX)
	#define ULONG_MAX               ((unsigned long)~0UL)
#endif

#if !defined(LONG_MAX)
	#define LONG_MAX                ((signed long)(ULONG_MAX >> 1))
#endif
#if !defined(LONG_MIN)
	#define LONG_MIN                ((signed long)(-LONG_MAX - 1))
#endif

#if !defined(ULLONG_MAX)
	#define ULLONG_MAX              ((unsigned long long)~0ULL)
#endif

#if !defined(LLONG_MAX)
	#define LLONG_MAX               ((signed long long)(ULLONG_MAX >> 1))
#endif
#if !defined(LLONG_MIN)
	#define LLONG_MIN               ((signed long long)(-LLONG_MAX - 1))
#endif

#if !defined(SIZE_MAX)
	#if 1 == CSTD_CPU_32BIT
		#define SIZE_MAX            UINT32_MAX
	#elif 1 == CSTD_CPU_64BIT
		#define SIZE_MAX            UINT64_MAX
	#endif
#endif

/* ============================================================================
	Keywords
============================================================================ */
/* Portable keywords. */

#if !defined(CONST)
/**
 * @hideinitializer
 * Variable is a C @c const, which can be made non-const for testing purposes.
 */
	#define CONST                   const
#endif

#if !defined(STATIC)
/**
 * @hideinitializer
 * Variable is a C @c static, which can be made non-static for testing
 * purposes.
 */
	#define STATIC                  static
#endif

/**
 * Specifies a function as being exported outside of a logical module.
 */
#define PUBLIC

/**
 * @def PROTECTED
 * Specifies a a function which is internal to an logical module, but which
 * should not be used outside of that module. This cannot be enforced by the
 * compiler, as a module is typically more than one translation unit.
 */
#define PROTECTED

/**
 * Specifies a function as being internal to a translation unit. Private
 * functions would typically be declared as STATIC, unless they are being
 * exported for unit test purposes.
 */
#define PRIVATE STATIC

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
#define CSTD_COMPILE_TIME_ASSERT( expr ) \
	do { switch(0){case 0: case (expr):;} } while( FALSE )

/**
 * @hideinitializer
 * @deprecated Prefered form is @c CSTD_UNUSED
 * Function-like macro for suppressing unused variable warnings. Where possible
 * such variables should be removed; this macro is present for cases where we
 * much support API backwards compatibility.
 */
#define UNUSED( x )                 ((void)(x))

/**
 * @hideinitializer
 * Function-like macro for suppressing unused variable warnings. Where possible
 * such variables should be removed; this macro is present for cases where we
 * much support API backwards compatibility.
 */
#define CSTD_UNUSED( x )            ((void)(x))

/**
 * @hideinitializer
 * Function-like macro for use where "no behavior" is desired. This is useful
 * when compile time macros turn a function-like macro in to a no-op, but
 * where having no statement is otherwise invalid.
 */
#define CSTD_NOP( ... )             ((void)#__VA_ARGS__)

/**
 * @hideinitializer
 * Function-like macro for converting a pointer in to a u64 for storing into
 * an external data structure. This is commonly used when pairing a 32-bit
 * CPU with a 64-bit peripheral, such as a Midgard GPU. C's type promotion
 * is complex and a straight cast does not work reliably as pointers are
 * often considered as signed.
 */
#define CSTD_PTR_TO_U64( x )        ((uint64_t)((uintptr_t)(x)))

/**
 * @hideinitializer
 * Function-like macro for stringizing a single level macro.
 * @code
 * #define MY_MACRO 32
 * CSTD_STR1( MY_MACRO )
 * > "MY_MACRO"
 * @endcode
 */
#define CSTD_STR1( x )             #x

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
#define CSTD_STR2( x )              CSTD_STR1( x )

/**
 * @hideinitializer
 * Utility function for stripping the first character off a string.
 */
static INLINE char* arm_cstd_strstrip( char * string )
{
	return ++string;
}

/**
 * @hideinitializer
 * Function-like macro for stringizing a single level macro where the macro
 * itself may not have a value. Parameter @c a should be set to any single
 * character which is then stripped by the macro via an inline function. This
 * should only be used via the @c CSTD_STR2N macro; for printing a single
 * macro only the @c CSTD_STR1 macro is a better alternative.
 *
 * This macro requires run-time code to handle the case where the macro has
 * no value (you can't concat empty strings in the preprocessor).
 */
#define CSTD_STR1N( a, x )          arm_cstd_strstrip( CSTD_STR1( a##x ) )

/**
 * @hideinitializer
 * Function-like macro for stringizing a two level macro where the macro itself
 * may not have a value.
 * @code
 * #define MY_MACRO 32
 * CSTD_STR2N( MY_MACRO )
 * > "32"
 *
 * #define MY_MACRO 32
 * CSTD_STR2N( MY_MACRO )
 * > "32"
 * @endcode
 */
#define CSTD_STR2N( x )              CSTD_STR1N( _, x )

/* ============================================================================
	Validate portability constructs
============================================================================ */
static INLINE void arm_cstd_compile_time_assertions( void )
{
	CSTD_COMPILE_TIME_ASSERT( sizeof(uint8_t)  == 1 );
	CSTD_COMPILE_TIME_ASSERT( sizeof(int8_t)   == 1 );
	CSTD_COMPILE_TIME_ASSERT( sizeof(uint16_t) == 2 );
	CSTD_COMPILE_TIME_ASSERT( sizeof(int16_t)  == 2 );
	CSTD_COMPILE_TIME_ASSERT( sizeof(uint32_t) == 4 );
	CSTD_COMPILE_TIME_ASSERT( sizeof(int32_t)  == 4 );
	CSTD_COMPILE_TIME_ASSERT( sizeof(uint64_t) == 8 );
	CSTD_COMPILE_TIME_ASSERT( sizeof(int64_t)  == 8 );
	CSTD_COMPILE_TIME_ASSERT( sizeof(intptr_t) == sizeof(uintptr_t) );

	CSTD_COMPILE_TIME_ASSERT( 1 == TRUE );
	CSTD_COMPILE_TIME_ASSERT( 0 == FALSE );

#if 1 == CSTD_CPU_32BIT
	CSTD_COMPILE_TIME_ASSERT( sizeof(uintptr_t) == 4 );
#elif 1 == CSTD_CPU_64BIT
	CSTD_COMPILE_TIME_ASSERT( sizeof(uintptr_t) == 8 );
#endif

}

/* ============================================================================
	Useful function-like macro
============================================================================ */
/**
 * @brief Return the lesser of two values.
 * As a macro it may evaluate its arguments more than once.
 * @see CSTD_MAX
 */
#define CSTD_MIN( x, y )            ((x) < (y) ? (x) : (y))

/**
 * @brief Return the greater of two values.
 * As a macro it may evaluate its arguments more than once.
 * If called on the same two arguments as CSTD_MIN it is guaranteed to return
 * the one that CSTD_MIN didn't return. This is significant for types where not
 * all values are comparable e.g. NaNs in floating-point types. But if you want
 * to retrieve the min and max of two values, consider using a conditional swap
 * instead.
 */
#define CSTD_MAX( x, y )            ((x) < (y) ? (y) : (x))

/**
 * @brief Clamp value @c x to within @c min and @c max inclusive.
 */
#define CSTD_CLAMP( x, min, max )   ((x)<(min) ? (min):((x)>(max) ? (max):(x)))

/**
 * Flag a cast as a reinterpretation, usually of a pointer type.
 */
#define CSTD_REINTERPRET_CAST(type) (type)

/**
 * Flag a cast as casting away const, usually of a pointer type.
 */
#define CSTD_CONST_CAST(type)       (type)

/**
 * Flag a cast as a (potentially complex) value conversion, usually of a
 * numerical type.
 */
#define CSTD_STATIC_CAST(type)      (type)

/* ============================================================================
	Useful bit constants
============================================================================ */
/**
 * @cond arm_cstd_utilities
 */

/* Common bit constant values, useful in embedded programming. */
#define F_BIT_0       ((uint32_t)0x00000001)
#define F_BIT_1       ((uint32_t)0x00000002)
#define F_BIT_2       ((uint32_t)0x00000004)
#define F_BIT_3       ((uint32_t)0x00000008)
#define F_BIT_4       ((uint32_t)0x00000010)
#define F_BIT_5       ((uint32_t)0x00000020)
#define F_BIT_6       ((uint32_t)0x00000040)
#define F_BIT_7       ((uint32_t)0x00000080)
#define F_BIT_8       ((uint32_t)0x00000100)
#define F_BIT_9       ((uint32_t)0x00000200)
#define F_BIT_10      ((uint32_t)0x00000400)
#define F_BIT_11      ((uint32_t)0x00000800)
#define F_BIT_12      ((uint32_t)0x00001000)
#define F_BIT_13      ((uint32_t)0x00002000)
#define F_BIT_14      ((uint32_t)0x00004000)
#define F_BIT_15      ((uint32_t)0x00008000)
#define F_BIT_16      ((uint32_t)0x00010000)
#define F_BIT_17      ((uint32_t)0x00020000)
#define F_BIT_18      ((uint32_t)0x00040000)
#define F_BIT_19      ((uint32_t)0x00080000)
#define F_BIT_20      ((uint32_t)0x00100000)
#define F_BIT_21      ((uint32_t)0x00200000)
#define F_BIT_22      ((uint32_t)0x00400000)
#define F_BIT_23      ((uint32_t)0x00800000)
#define F_BIT_24      ((uint32_t)0x01000000)
#define F_BIT_25      ((uint32_t)0x02000000)
#define F_BIT_26      ((uint32_t)0x04000000)
#define F_BIT_27      ((uint32_t)0x08000000)
#define F_BIT_28      ((uint32_t)0x10000000)
#define F_BIT_29      ((uint32_t)0x20000000)
#define F_BIT_30      ((uint32_t)0x40000000)
#define F_BIT_31      ((uint32_t)0x80000000)

/* Common 2^n size values, useful in embedded programming. */
#define C_SIZE_1B     ((uint32_t)0x00000001)
#define C_SIZE_2B     ((uint32_t)0x00000002)
#define C_SIZE_4B     ((uint32_t)0x00000004)
#define C_SIZE_8B     ((uint32_t)0x00000008)
#define C_SIZE_16B    ((uint32_t)0x00000010)
#define C_SIZE_32B    ((uint32_t)0x00000020)
#define C_SIZE_64B    ((uint32_t)0x00000040)
#define C_SIZE_128B   ((uint32_t)0x00000080)
#define C_SIZE_256B   ((uint32_t)0x00000100)
#define C_SIZE_512B   ((uint32_t)0x00000200)
#define C_SIZE_1KB    ((uint32_t)0x00000400)
#define C_SIZE_2KB    ((uint32_t)0x00000800)
#define C_SIZE_4KB    ((uint32_t)0x00001000)
#define C_SIZE_8KB    ((uint32_t)0x00002000)
#define C_SIZE_16KB   ((uint32_t)0x00004000)
#define C_SIZE_32KB   ((uint32_t)0x00008000)
#define C_SIZE_64KB   ((uint32_t)0x00010000)
#define C_SIZE_128KB  ((uint32_t)0x00020000)
#define C_SIZE_256KB  ((uint32_t)0x00040000)
#define C_SIZE_512KB  ((uint32_t)0x00080000)
#define C_SIZE_1MB    ((uint32_t)0x00100000)
#define C_SIZE_2MB    ((uint32_t)0x00200000)
#define C_SIZE_4MB    ((uint32_t)0x00400000)
#define C_SIZE_8MB    ((uint32_t)0x00800000)
#define C_SIZE_16MB   ((uint32_t)0x01000000)
#define C_SIZE_32MB   ((uint32_t)0x02000000)
#define C_SIZE_64MB   ((uint32_t)0x04000000)
#define C_SIZE_128MB  ((uint32_t)0x08000000)
#define C_SIZE_256MB  ((uint32_t)0x10000000)
#define C_SIZE_512MB  ((uint32_t)0x20000000)
#define C_SIZE_1GB    ((uint32_t)0x40000000)
#define C_SIZE_2GB    ((uint32_t)0x80000000)

/**
 * @endcond
 */

/**
 * @}
 */

/**
 * @}
 */

#endif  /* End (_ARM_CSTD_) */
