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





#ifndef _ARM_CSTD_TYPES_RVCT_H_
#define _ARM_CSTD_TYPES_RVCT_H_

/* ============================================================================
	Type definitions
============================================================================ */
#include <stddef.h>
#include <limits.h>

#if 199901L <= __STDC_VERSION__
	#include <inttypes.h>
#else
	typedef unsigned char           uint8_t;
	typedef signed char             int8_t;
	typedef unsigned short          uint16_t;
	typedef signed short            int16_t;
	typedef unsigned int            uint32_t;
	typedef signed int              int32_t;
	typedef unsigned __int64        uint64_t;
	typedef signed __int64          int64_t;
	typedef ptrdiff_t               intptr_t;
	typedef size_t                  uintptr_t;
#endif

typedef uint32_t                    bool_t;

#if !defined(TRUE)
	#define TRUE                ((bool_t)1)
#endif

#if !defined(FALSE)
	#define FALSE               ((bool_t)0)
#endif

/* ============================================================================
	Keywords
============================================================================ */
/**
 * @addtogroup arm_cstd_coding_standard
 * @{
 */

/**
 * @def ASM
 * @hideinitializer
 * Mark an assembler block. Such blocks are often compiler specific, so often
 * need to be surrounded in appropriate @c ifdef and @c endif blocks
 * using the relevant @c CSTD_TOOLCHAIN macro.
 */
#define ASM                     __asm

/**
 * @def INLINE
 * @hideinitializer
 * Mark a definition as something which should be inlined. This is not always
 * possible on a given compiler, and may be disabled at lower optimization
 * levels.
 */
#define INLINE                  __inline

/**
 * @def FORCE_INLINE
 * @hideinitializer
 * Mark a definition as something which should be inlined. This provides a much
 * stronger hint to the compiler than @c INLINE, and if supported should always
 * result in an inlined function being emitted. If not supported this falls
 * back to using the @c INLINE definition.
 */
#define FORCE_INLINE            __forceinline

/**
 * @def NEVER_INLINE
 * @hideinitializer
 * Mark a definition as something which should not be inlined. This provides a
 * stronger hint to the compiler than the function should not be inlined,
 * bypassing any heuristic rules the compiler normally applies. If not
 * supported by a toolchain this falls back to being an empty macro.
 */
#define NEVER_INLINE            __declspec(noinline)

/**
 * @def PURE
 * @hideinitializer
 * Denotes that a function's return is only dependent on its inputs, enabling
 * more efficient optimizations. Falls back to an empty macro if not supported.
 */
#define PURE                    __pure

/**
 * @def PACKED
 * @hideinitializer
 * Denotes that a structure should be stored in a packed form. This macro must
 * be used in conjunction with the @c arm_cstd_pack_* headers for portability:
 *
 * @code
 * #include <cstd/arm_cstd_pack_push.h>
 *
 * struct PACKED myStruct {
 *     ...
 * };
 *
 * #include <cstd/arm_cstd_pack_pop.h>
 * PACKED
 * @endcode
 */
#define PACKED                  __packed

/**
 * @def UNALIGNED
 * @hideinitializer
 * Denotes that a pointer points to a buffer with lower alignment than the
 * natural alignment required by the C standard. This should only be used
 * in extreme cases, as the emitted code is normally more efficient if memory
 * is aligned.
 *
 * @warning This is \b NON-PORTABLE. The GNU tools are anti-unaligned pointers
 * and have no support for such a construction.
 */
#define UNALIGNED               __packed

/**
 * @def RESTRICT
 * @hideinitializer
 * Denotes that a pointer does not overlap with any other points currently in
 * scope, increasing the range of optimizations which can be performed by the
 * compiler.
 *
 * @warning Specification of @c RESTRICT is a contract between the programmer
 * and the compiler. If you place @c RESTICT on buffers which do actually
 * overlap the behavior is undefined, and likely to vary at different
 * optimization levels.!
 */
#define RESTRICT                __restrict

/**
 * @def CHECK_RESULT
 * @hideinitializer
 * Function attribute which causes a warning to be emitted if the compiler's
 * return value is not used by the caller. Compiles to an empty macro if
 * there is no supported mechanism for this check in the underlying compiler.
 *
 * @note At the time of writing this is only supported by GCC. RVCT does not
 * support this attribute, even in GCC mode, so engineers are encouraged to
 * compile their code using GCC even if primarily working with another
 * compiler.
 *
 * @code
 * CHECK_RESULT int my_func( void );
 * @endcode
  */
#define CHECK_RESULT

/**
 * @def CSTD_FUNC
 * Specify the @c CSTD_FUNC macro, a portable construct containing the name of
 * the current function. On most compilers it is illegal to use this macro
 * outside of a function scope. If not supported by the compiler we define
 * @c CSTD_FUNC as an empty string.
 *
 * @warning Due to the implementation of this on most modern compilers this
 * expands to a magically defined "static const" variable, not a constant
 * string. This makes injecting @c CSTD_FUNC directly in to compile-time
 * strings impossible, so if you want to make the function name part of a
 * larger string you must use a printf-like function with a @c @%s template
 * which is populated with @c CSTD_FUNC
 */
#define CSTD_FUNC            __FUNCTION__

/**
 * @}
 */

#endif /* End (_ARM_CSTD_TYPES_RVCT_H_) */
