/*************************************************************************/ /*!
@File
@Title          Common header containing type definitions for portability
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Contains variable and structure definitions. Any platform
                specific types should be defined in this file.
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#if !defined (__IMG_DEFS_H__)
#define __IMG_DEFS_H__

#include <stddef.h>

#include "img_types.h"

#if defined (NO_INLINE_FUNCS)
	#define	INLINE
	#define	FORCE_INLINE
#elif defined(INTEGRITY_OS)
	#ifndef INLINE
	#define	INLINE
	#endif
	#define	FORCE_INLINE			static
	#define INLINE_IS_PRAGMA
#else
#if defined (__cplusplus)
	#define INLINE					inline
	#define	FORCE_INLINE			static inline
#else
#if	!defined(INLINE)
	#define	INLINE					__inline
#endif
#if (defined(UNDER_WDDM) || defined(WINDOWS_WDF)) && defined(_X86_)
	#define	FORCE_INLINE			__forceinline
#else
	#define	FORCE_INLINE			static __inline
#endif
#endif
#endif

/* True if the GCC version is at least the given version. False for older
 * versions of GCC, or other compilers.
 */
#define GCC_VERSION_AT_LEAST(major, minor)						\
	(defined(__GNUC__) && (										\
		__GNUC__ > (major) ||									\
		(__GNUC__ == (major) && __GNUC_MINOR__ >= (minor))))

/* Ensure Clang's __has_extension macro is defined for all compilers so we
 * can use it safely in preprocessor conditionals.
 */
#if !defined(__has_extension)
#define __has_extension(e) 0
#endif

/* Use this in any file, or use attributes under GCC - see below */
#ifndef PVR_UNREFERENCED_PARAMETER
#define	PVR_UNREFERENCED_PARAMETER(param) ((void)(param))
#endif

/* static_assert(condition, "message to print if it fails");
 *
 * Assert something at compile time. If the assertion fails, try to print
 * the message, otherwise do nothing. static_assert is available if:
 *
 * - It's already defined as a macro (e.g. by <assert.h> in C11)
 * - We're using MSVC which exposes static_assert unconditionally
 * - We're using a C++ compiler that supports C++11
 * - We're using GCC 4.6 and up in C mode (in which case it's available as
 *   _Static_assert)
 *
 * In all other cases, fall back to an equivalent that makes an invalid
 * declaration.
 */
#if !defined(static_assert) && !defined(_MSC_VER) && \
		(!defined(__cplusplus) || __cplusplus < 201103L)
	/* static_assert isn't already available */
	#if !defined(__cplusplus) && (GCC_VERSION_AT_LEAST(4, 6) || \
								  (defined(__clang__) && __has_extension(c_static_assert)))
		#define static_assert _Static_assert
	#else
		#define static_assert(expr, message) \
			extern int _static_assert_failed[2*!!(expr) - 1] __attribute__((unused))
	#endif
#else
#if defined(CONFIG_L4)
	/* Defined but not compatible with DDK usage
	   so undefine & ignore */
	#undef static_assert
	#define static_assert(expr, message)
#endif
#endif

/*! Macro to calculate the n-byte aligned value from that supplied rounding up.
 * n must be a power of two.
 *
 * Both arguments should be of a type with the same size otherwise the macro may
 * cut off digits, e.g. imagine a 64 bit address in _x and a 32 bit value in _n.
 */
#define PVR_ALIGN(_x, _n)   (((_x)+((_n)-1)) & ~((_n)-1))

#if defined(_WIN32)

#if defined(WINDOWS_WDF)

	/*
	 * For WINDOWS_WDF drivers we don't want these defines to overwrite calling conventions propagated through the build system.
	 * This 'empty' choice helps to resolve all the calling conv issues.
	 *
	 */
	#define IMG_CALLCONV
	#define C_CALLCONV

	#define IMG_INTERNAL
	#define IMG_RESTRICT __restrict

	/*
	 * The proper way of dll linking under MS compilers is made of two things:
	 * - decorate implementation with __declspec(dllexport)
	 *   this decoration helps compiler with making the so called 'export library'
	 * - decorate forward-declaration (in a source dependent on a dll) with __declspec(dllimport),
	 *   this decoration helps compiler with making faster and smaller code in terms of calling dll-imported functions
	 *
	 * Usually these decorations are performed by having a single macro define that expands to a proper __declspec()
	 * depending on the translation unit, dllexport inside the dll source and dllimport outside the dll source.
	 * Having IMG_EXPORT and IMG_IMPORT resolving to the same __declspec() makes no sense, but at least works.
	 */
	#define IMG_IMPORT __declspec(dllexport)
	#define IMG_EXPORT __declspec(dllexport)

#else

	#define IMG_CALLCONV __stdcall
	#define IMG_INTERNAL
	#define	IMG_EXPORT	__declspec(dllexport)
	#define IMG_RESTRICT __restrict
	#define C_CALLCONV	__cdecl

	/*
	 * IMG_IMPORT is defined as IMG_EXPORT so that headers and implementations match.
	 * Some compilers require the header to be declared IMPORT, while the implementation is declared EXPORT 
	 */
	#define	IMG_IMPORT	IMG_EXPORT

#endif

#if defined(UNDER_WDDM)
	#ifndef	_INC_STDLIB
		#if defined(__mips)
			/* do nothing */
		#elif defined(UNDER_MSBUILD)
			_CRTIMP __declspec(noreturn) void __cdecl abort(void);
		#else
			_CRTIMP void __cdecl abort(void);
		#endif
	#endif
#endif /* UNDER_WDDM */
#else
	#if defined(LINUX) || defined(__METAG) || defined(__QNXNTO__)

		#define IMG_CALLCONV
		#define C_CALLCONV
		#if defined(__linux__) || defined(__QNXNTO__)
			#define IMG_INTERNAL	__attribute__((visibility("hidden")))
		#else
			#define IMG_INTERNAL
		#endif
		#define IMG_EXPORT		__attribute__((visibility("default")))
		#define IMG_IMPORT
		#define IMG_RESTRICT	__restrict__

	#elif defined(INTEGRITY_OS)
		#define IMG_CALLCONV
		#define IMG_INTERNAL
		#define IMG_EXPORT
		#define IMG_RESTRICT
		#define C_CALLCONV
		#define __cdecl
		/* IMG_IMPORT is defined as IMG_EXPORT so that headers and implementations match.
		 * Some compilers require the header to be declared IMPORT, while the implementation is declared EXPORT 
		 */
		#define	IMG_IMPORT	IMG_EXPORT 
		#ifndef USE_CODE
		#define IMG_ABORT()	printf("IMG_ABORT was called.\n")

		#endif
	#else
		#error("define an OS")
	#endif
#endif

// Use default definition if not overridden
#ifndef IMG_ABORT
	#if defined(EXIT_ON_ABORT)
		#define IMG_ABORT()	exit(1)
	#else
		#define IMG_ABORT()	abort()
	#endif
#endif

/* The best way to suppress unused parameter warnings using GCC is to use a
 * variable attribute.  Place the __maybe_unused between the type and name of an
 * unused parameter in a function parameter list, eg `int __maybe_unused var'. This
 * should only be used in GCC build environments, for example, in files that
 * compile only on Linux. Other files should use PVR_UNREFERENCED_PARAMETER */

/* Kernel macros for compiler attributes */
/* Note: param positions start at 1 */
#if defined(LINUX) && defined(__KERNEL__)
	#include <linux/compiler.h>
#elif defined(__GNUC__) || defined(HAS_GNUC_ATTRIBUTES)
	#define __must_check       __attribute__((warn_unused_result))
	#define __maybe_unused     __attribute__((unused))
	#define __malloc           __attribute__((malloc))

	/* Bionic's <sys/cdefs.h> might have defined these already */
	#if !defined(__packed)
		#define __packed           __attribute__((packed))
	#endif
	#if !defined(__aligned)
		#define __aligned(n)       __attribute__((aligned(n)))
	#endif

	/* That one compiler that supports attributes but doesn't support
	 * the printf attribute... */
	#if defined(__GNUC__)
		#define __printf(fmt, va)  __attribute__((format(printf, fmt, va)))
	#else
		#define __printf(fmt, va)
	#endif /* defined(__GNUC__) */

#else
	/* Silently ignore those attributes */
	#define __printf(fmt, va)
	#define __packed
	#define __aligned(n)
	#define __must_check
	#define __maybe_unused
	#define __malloc
#endif


/* Other attributes, following the same style */
#if defined(__GNUC__) || defined(HAS_GNUC_ATTRIBUTES)
	#define __param_nonnull(...)  __attribute__((nonnull(__VA_ARGS__)))
	#define __returns_nonnull     __attribute__((returns_nonnull))
#else
	#define __param_nonnull(...)
	#define __returns_nonnull
#endif


/* GCC builtins */
#if defined(LINUX) && defined(__KERNEL__)
	#include <linux/compiler.h>
#elif defined(__GNUC__)
	#define likely(x)   __builtin_expect(!!(x), 1)
	#define unlikely(x) __builtin_expect(!!(x), 0)

	/* Compiler memory barrier to prevent reordering */
	#define barrier() __asm__ __volatile__("": : :"memory")
#else
	#define barrier() do { static_assert(0, "barrier() isn't supported by your compiler"); } while(0)
#endif

/* That one OS that defines one but not the other... */
#ifndef likely
	#define likely(x)   (x)
#endif
#ifndef unlikely
	#define unlikely(x) (x)
#endif


#if defined(__noreturn)
	/* Already defined by the Kernel */
#elif defined(_MSC_VER) || defined(CC_ARM)
	#define __noreturn __declspec(noreturn)
#elif defined(__GNUC__) || defined(HAS_GNUC_ATTRIBUTES)
	#define __noreturn __attribute__((noreturn))
#else
	#define __noreturn
#endif

#ifndef MAX
#define MAX(a,b) 					(((a) > (b)) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a,b) 					(((a) < (b)) ? (a) : (b))
#endif

/* Get a structures address from the address of a member */
#define IMG_CONTAINER_OF(ptr, type, member) \
	(type *) ((IMG_UINT8 *) (ptr) - offsetof(type, member))

/* The number of elements in a fixed-sized array, IMGs ARRAY_SIZE macro */
#define IMG_ARR_NUM_ELEMS(ARR) \
	(sizeof(ARR) / sizeof((ARR)[0]))

/* To guarantee that __func__ can be used, define it as a macro here if it
   isn't already provided by the compiler. */
#if defined(_MSC_VER)
#define __func__ __FUNCTION__
#endif

#if defined(__cplusplus)
/* C++ Specific:
 * Disallow use of copy and assignment operator within a class.
 * Should be placed under private. */
#define IMG_DISALLOW_COPY_AND_ASSIGN(C) \
	C(const C&); \
	void operator=(const C&)
#endif

#if defined(SUPPORT_PVR_VALGRIND) && !defined(__METAG)
	#include "/usr/include/valgrind/memcheck.h"

	#define VG_MARK_INITIALIZED(pvData,ui32Size)  VALGRIND_MAKE_MEM_DEFINED(pvData,ui32Size)
	#define VG_MARK_NOACCESS(pvData,ui32Size) VALGRIND_MAKE_MEM_NOACCESS(pvData,ui32Size)
	#define VG_MARK_ACCESS(pvData,ui32Size) VALGRIND_MAKE_MEM_UNDEFINED(pvData,ui32Size)
#else
	#if defined(_MSC_VER)
	#	define PVR_MSC_SUPPRESS_4127 __pragma(warning(suppress:4127))
	#else
	#	define PVR_MSC_SUPPRESS_4127
	#endif

	#define VG_MARK_INITIALIZED(pvData,ui32Size) PVR_MSC_SUPPRESS_4127 do { } while(0)
	#define VG_MARK_NOACCESS(pvData,ui32Size) PVR_MSC_SUPPRESS_4127 do { } while(0)
	#define VG_MARK_ACCESS(pvData,ui32Size) PVR_MSC_SUPPRESS_4127 do { } while(0)
#endif

#define _STRINGIFY(x) # x
#define IMG_STRINGIFY(x) _STRINGIFY(x)

#if defined(INTEGRITY_OS)
	/* Definitions not present in INTEGRITY. */
	#define PATH_MAX	200
#endif

#if defined (__clang__) || defined (__GNUC__)
	/* __SIZEOF_POINTER__ is defined already by these compilers */
#elif defined (INTEGRITY_OS)
	#if defined (__Ptr_Is_64)
		#define __SIZEOF_POINTER__ 8
	#else
		#define __SIZEOF_POINTER__ 4
	#endif
#elif defined(_WIN32)
	#define __SIZEOF_POINTER__ sizeof(char *)
#else
	#warning Unknown OS - using default method to determine whether CPU arch is 64-bit.
	#define __SIZEOF_POINTER__ sizeof(char *)
#endif

/* RDI8567: clang/llvm load/store optimisations cause issues with device
 * memory allocations. Some pointers are made 'volatile' to prevent
 * this optimisations being applied to writes through that particular pointer.
 */
#if defined(__clang__) && defined(__aarch64__)
#define NOLDSTOPT volatile
/* after applying 'volatile' to a pointer, we may need to cast it to 'void *'
 * to keep it compatible with its existing uses
 */
#define NOLDSTOPT_VOID (void *)
#else
#define NOLDSTOPT
#define NOLDSTOPT_VOID
#endif

#endif /* #if !defined (__IMG_DEFS_H__) */
/*****************************************************************************
 End of file (IMG_DEFS.H)
*****************************************************************************/

