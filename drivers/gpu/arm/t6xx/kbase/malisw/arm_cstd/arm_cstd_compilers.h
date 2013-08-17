/*
 *
 * (C) COPYRIGHT 2005-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



#ifndef _ARM_CSTD_COMPILERS_H_
#define _ARM_CSTD_COMPILERS_H_

/* ============================================================================
	Document default definitions - assuming nothing set at this point.
============================================================================ */
/**
 * @addtogroup arm_cstd_coding_standard
 * @{
 */

/**
 * @hideinitializer
 * Defined with value of 1 if toolchain is Microsoft Visual Studio, 0
 * otherwise.
 */
#define CSTD_TOOLCHAIN_MSVC         0

/**
 * @hideinitializer
 * Defined with value of 1 if toolchain is the GNU Compiler Collection, 0
 * otherwise.
 */
#define CSTD_TOOLCHAIN_GCC          0

/**
 * @hideinitializer
 * Defined with value of 1 if toolchain is ARM RealView Compiler Tools, 0
 * otherwise. Note - if running RVCT in GCC mode this define will be set to 0;
 * @c CSTD_TOOLCHAIN_GCC and @c CSTD_TOOLCHAIN_RVCT_GCC_MODE will both be
 * defined as 1.
 */
#define CSTD_TOOLCHAIN_RVCT         0

/**
 * @hideinitializer
 * Defined with value of 1 if toolchain is ARM RealView Compiler Tools running
 * in GCC mode, 0 otherwise.
 */
#define CSTD_TOOLCHAIN_RVCT_GCC_MODE 0

/**
 * @hideinitializer
 * Defined with value of 1 if processor is an x86 32-bit machine, 0 otherwise.
 */
#define CSTD_CPU_X86_32             0

/**
 * @hideinitializer
 * Defined with value of 1 if processor is an x86-64 (AMD64) machine, 0
 * otherwise.
 */
#define CSTD_CPU_X86_64             0

/**
 * @hideinitializer
 * Defined with value of 1 if processor is an ARM machine, 0 otherwise.
 */
#define CSTD_CPU_ARM                0

/**
 * @hideinitializer
 * Defined with value of 1 if processor is a MIPS machine, 0 otherwise.
 */
#define CSTD_CPU_MIPS               0

/**
 * @hideinitializer
 * Defined with value of 1 if CPU is 32-bit, 0 otherwise.
 */
#define CSTD_CPU_32BIT              0

/**
 * @hideinitializer
 * Defined with value of 1 if CPU is 64-bit, 0 otherwise.
 */
#define CSTD_CPU_64BIT              0

/**
 * @hideinitializer
 * Defined with value of 1 if processor configured as big-endian, 0 if it
 * is little-endian.
 */
#define CSTD_CPU_BIG_ENDIAN         0

/**
 * @hideinitializer
 * Defined with value of 1 if operating system is a version of Windows, 0 if
 * it is not.
 */
#define CSTD_OS_WINDOWS             0

/**
 * @hideinitializer
 * Defined with value of 1 if operating system is a 32-bit version of Windows,
 * 0 if it is not.
 */
#define CSTD_OS_WIN32               0

/**
 * @hideinitializer
 * Defined with value of 1 if operating system is a 64-bit version of Windows,
 * 0 if it is not.
 */
#define CSTD_OS_WIN64               0

/**
 * @hideinitializer
 * Defined with value of 1 if operating system is Linux, 0 if it is not.
 */
#define CSTD_OS_LINUX               0

/**
 * @hideinitializer
 * Defined with value of 1 if we are compiling Linux kernel code, 0 otherwise.
 */
#define CSTD_OS_LINUX_KERNEL        0

/**
 * @hideinitializer
 * Defined with value of 1 if operating system is a 32-bit version of Linux,
 * 0 if it is not.
 */
#define CSTD_OS_LINUX32             0

/**
 * @hideinitializer
 * Defined with value of 1 if operating system is a 64-bit version of Linux,
 * 0 if it is not.
 */
#define CSTD_OS_LINUX64             0

/**
 * @hideinitializer
 * Defined with value of 1 if operating system is Android, 0 if it is not.
 */
#define CSTD_OS_ANDROID             0

/**
 * @hideinitializer
 * Defined with value of 1 if we are compiling Android kernel code, 0 otherwise.
 */
#define CSTD_OS_ANDROID_KERNEL      0

/**
 * @hideinitializer
 * Defined with value of 1 if operating system is a 32-bit version of Android,
 * 0 if it is not.
 */
#define CSTD_OS_ANDROID32           0

/**
 * @hideinitializer
 * Defined with value of 1 if operating system is a 64-bit version of Android,
 * 0 if it is not.
 */
#define CSTD_OS_ANDROID64           0

/**
 * @hideinitializer
 * Defined with value of 1 if operating system is a version of Apple OS,
 * 0 if it is not.
 */
#define CSTD_OS_APPLEOS             0

/**
 * @hideinitializer
 * Defined with value of 1 if operating system is a 32-bit version of Apple OS,
 * 0 if it is not.
 */
#define CSTD_OS_APPLEOS32           0

/**
 * @hideinitializer
 * Defined with value of 1 if operating system is a 64-bit version of Apple OS,
 * 0 if it is not.
 */
#define CSTD_OS_APPLEOS64           0

/**
 * @def CSTD_OS_SYMBIAN
 * @hideinitializer
 * Defined with value of 1 if operating system is Symbian, 0 if it is not.
 */
#define CSTD_OS_SYMBIAN             0

/**
 * @def CSTD_OS_NONE
 * @hideinitializer
 * Defined with value of 1 if there is no operating system (bare metal), 0
 * otherwise
 */
#define CSTD_OS_NONE                0

/* ============================================================================
	Determine the compiler in use
============================================================================ */
#if defined(_MSC_VER)
	#undef CSTD_TOOLCHAIN_MSVC
	#define CSTD_TOOLCHAIN_MSVC         1

#elif defined(__GNUC__)
	#undef CSTD_TOOLCHAIN_GCC
	#define CSTD_TOOLCHAIN_GCC          1

	/* Detect RVCT pretending to be GCC. */
	#if defined(__ARMCC_VERSION)
		#undef CSTD_TOOLCHAIN_RVCT_GCC_MODE
		#define CSTD_TOOLCHAIN_RVCT_GCC_MODE    1
	#endif

#elif defined(__ARMCC_VERSION)
	#undef CSTD_TOOLCHAIN_RVCT
	#define CSTD_TOOLCHAIN_RVCT         1

#else
	#warning "Unsupported or unknown toolchain"

#endif

/* ============================================================================
	Determine the processor
============================================================================ */
#if 1 == CSTD_TOOLCHAIN_MSVC
	#if defined(_M_IX86)
		#undef CSTD_CPU_X86_32
		#define CSTD_CPU_X86_32         1

	#elif defined(_M_X64) || defined(_M_AMD64)
		#undef CSTD_CPU_X86_64
		#define CSTD_CPU_X86_64         1

	#elif defined(_M_ARM)
		#undef CSTD_CPU_ARM
		#define CSTD_CPU_ARM            1

	#elif defined(_M_MIPS)
		#undef CSTD_CPU_MIPS
		#define CSTD_CPU_MIPS           1

	#else
		#warning "Unsupported or unknown host CPU for MSVC tools"

	#endif

#elif 1 == CSTD_TOOLCHAIN_GCC
	#if defined(__amd64__)
		#undef CSTD_CPU_X86_64
		#define CSTD_CPU_X86_64         1

	#elif defined(__i386__)
		#undef CSTD_CPU_X86_32
		#define CSTD_CPU_X86_32         1

	#elif defined(__arm__)
		#undef CSTD_CPU_ARM
		#define CSTD_CPU_ARM            1

	#elif defined(__mips__)
		#undef CSTD_CPU_MIPS
		#define CSTD_CPU_MIPS           1

	#else
		#warning "Unsupported or unknown host CPU for GCC tools"

	#endif

#elif 1 == CSTD_TOOLCHAIN_RVCT
	#undef CSTD_CPU_ARM
	#define CSTD_CPU_ARM                1

#else
	#warning "Unsupported or unknown toolchain"

#endif

/* ============================================================================
	Determine the Processor Endianness
============================================================================ */

#if ((1 == CSTD_CPU_X86_32) || (1 == CSTD_CPU_X86_64))
	/* Note: x86 and x86-64 are always little endian, so leave at default. */

#elif 1 == CSTD_TOOLCHAIN_RVCT
	#if defined(__BIG_ENDIAN)
		#undef CSTD_ENDIAN_BIG
		#define CSTD_ENDIAN_BIG         1
	#endif

#elif ((1 == CSTD_TOOLCHAIN_GCC) && (1 == CSTD_CPU_ARM))
	#if defined(__ARMEB__)
		#undef CSTD_ENDIAN_BIG
		#define CSTD_ENDIAN_BIG         1
	#endif

#elif ((1 == CSTD_TOOLCHAIN_GCC) && (1 == CSTD_CPU_MIPS))
	#if defined(__MIPSEB__)
		#undef CSTD_ENDIAN_BIG
		#define CSTD_ENDIAN_BIG         1
	#endif

#elif 1 == CSTD_TOOLCHAIN_MSVC
	/* Note: Microsoft only support little endian, so leave at default. */

#else
	#warning "Unsupported or unknown CPU"

#endif

/* ============================================================================
	Determine the operating system and addressing width
============================================================================ */
#if 1 == CSTD_TOOLCHAIN_MSVC
	#if defined(_WIN32) && !defined(_WIN64)
		#undef CSTD_OS_WINDOWS
		#define CSTD_OS_WINDOWS         1
		#undef CSTD_OS_WIN32
		#define CSTD_OS_WIN32           1
		#undef CSTD_CPU_32BIT
		#define CSTD_CPU_32BIT          1

	#elif defined(_WIN32) && defined(_WIN64)
		#undef CSTD_OS_WINDOWS
		#define CSTD_OS_WINDOWS         1
		#undef CSTD_OS_WIN64
		#define CSTD_OS_WIN64           1
		#undef CSTD_CPU_64BIT
		#define CSTD_CPU_64BIT          1

	#else
		#warning "Unsupported or unknown host OS for MSVC tools"

	#endif

#elif 1 == CSTD_TOOLCHAIN_GCC
	#if defined(_WIN32) && defined(_WIN64)
		#undef CSTD_OS_WINDOWS
		#define CSTD_OS_WINDOWS         1
		#undef CSTD_OS_WIN64
		#define CSTD_OS_WIN64           1
		#undef CSTD_CPU_64BIT
		#define CSTD_CPU_64BIT          1

	#elif defined(_WIN32) && !defined(_WIN64)
		#undef CSTD_OS_WINDOWS
		#define CSTD_OS_WINDOWS         1
		#undef CSTD_OS_WIN32
		#define CSTD_OS_WIN32           1
		#undef CSTD_CPU_32BIT
		#define CSTD_CPU_32BIT          1

	#elif defined(ANDROID)
		#undef CSTD_OS_ANDROID
		#define CSTD_OS_ANDROID         1

		#if defined(__KERNEL__)
			#undef CSTD_OS_ANDROID_KERNEL
			#define CSTD_OS_ANDROID_KERNEL  1
		#endif

		#if defined(__LP64__) || defined(_LP64)
			#undef CSTD_OS_ANDROID64
			#define CSTD_OS_ANDROID64       1
			#undef CSTD_CPU_64BIT
			#define CSTD_CPU_64BIT          1
		#else
			#undef CSTD_OS_ANDROID32
			#define CSTD_OS_ANDROID32       1
			#undef CSTD_CPU_32BIT
			#define CSTD_CPU_32BIT          1
		#endif

	#elif defined(__KERNEL__) || defined(__linux)
		#undef CSTD_OS_LINUX
		#define CSTD_OS_LINUX           1
		
		#if defined(__KERNEL__)
			#undef CSTD_OS_LINUX_KERNEL
			#define CSTD_OS_LINUX_KERNEL    1
		#endif

		#if defined(__LP64__) || defined(_LP64)
			#undef CSTD_OS_LINUX64
			#define CSTD_OS_LINUX64         1
			#undef CSTD_CPU_64BIT
			#define CSTD_CPU_64BIT          1
		#else
			#undef CSTD_OS_LINUX32
			#define CSTD_OS_LINUX32         1
			#undef CSTD_CPU_32BIT
			#define CSTD_CPU_32BIT          1
		#endif

	#elif defined(__APPLE__)
		#undef CSTD_OS_APPLEOS
		#define CSTD_OS_APPLEOS         1

		#if defined(__LP64__) || defined(_LP64)
			#undef CSTD_OS_APPLEOS64
			#define CSTD_OS_APPLEOS64       1
			#undef CSTD_CPU_64BIT
			#define CSTD_CPU_64BIT          1
		#else
			#undef CSTD_OS_APPLEOS32
			#define CSTD_OS_APPLEOS32       1
			#undef CSTD_CPU_32BIT
			#define CSTD_CPU_32BIT          1
		#endif

	#elif defined(__SYMBIAN32__)
		#undef CSTD_OS_SYMBIAN
		#define CSTD_OS_SYMBIAN         1
		#undef CSTD_CPU_32BIT
		#define CSTD_CPU_32BIT          1

	#else
		#undef CSTD_OS_NONE
		#define CSTD_OS_NONE            1
		#undef CSTD_CPU_32BIT
		#define CSTD_CPU_32BIT          1

#endif

#elif 1 == CSTD_TOOLCHAIN_RVCT

	#if defined(ANDROID)
		#undef CSTD_OS_ANDROID
		#undef CSTD_OS_ANDROID32
		#define CSTD_OS_ANDROID         1
		#define CSTD_OS_ANDROID32       1

	#elif defined(__linux)
		#undef CSTD_OS_LINUX
		#undef CSTD_OS_LINUX32
		#define CSTD_OS_LINUX           1
		#define CSTD_OS_LINUX32         1

	#elif defined(__SYMBIAN32__)
		#undef CSTD_OS_SYMBIAN
		#define CSTD_OS_SYMBIAN         1

	#else
		#undef CSTD_OS_NONE
		#define CSTD_OS_NONE            1

#endif

#else
	#warning "Unsupported or unknown host OS"

#endif

/* ============================================================================
	Determine the correct linker symbol Import and Export Macros
============================================================================ */
/**
 * @defgroup arm_cstd_linkage_specifiers Linkage Specifiers
 * @{
 *
 * This set of macros contain system-dependent linkage specifiers which
 * determine the visibility of symbols across DLL boundaries. A header for a
 * particular DLL should define a set of local macros derived from these,
 * and should not use these macros to decorate functions directly as there may
 * be multiple DLLs being used.
 *
 * These DLL library local macros should be (with appropriate library prefix)
 * <tt>[MY_LIBRARY]_API</tt>, <tt>[MY_LIBRARY]_IMPL</tt>, and
 * <tt>[MY_LIBRARY]_LOCAL</tt>.
 *
 *    - <tt>[MY_LIBRARY]_API</tt> should be use to decorate the function
 *      declarations in the header. It should be defined as either
 *      @c CSTD_LINK_IMPORT or @c CSTD_LINK_EXPORT, depending whether the
 *      current situation is a compile of the DLL itself (use export) or a
 *      compile of an external user of the DLL (use import).
 *    - <tt>[MY_LIBRARY]_IMPL</tt> should be defined as @c CSTD_LINK_IMPL
 *      and should be used to decorate the definition of functions in the C
 *      file.
 *    - <tt>[MY_LIBRARY]_LOCAL</tt> should be used to decorate function
 *      declarations which are exported across translation units within the
 *      DLL, but which are not exported outside of the DLL boundary.
 *
 * Functions which are @c static in either a C file or in a header file do not
 * need any form of linkage decoration, and should therefore have no linkage
 * macro applied to them.
 */

/**
 * @def CSTD_LINK_IMPORT
 * Specifies a function as being imported to a translation unit across a DLL
 * boundary.
 */

/**
 * @def CSTD_LINK_EXPORT
 * Specifies a function as being exported across a DLL boundary by a
 * translation unit.
 */

/**
 * @def CSTD_LINK_IMPL
 * Specifies a function which will be exported across a DLL boundary as
 * being implemented by a translation unit.
 */

/**
 * @def CSTD_LINK_LOCAL
 * Specifies a function which is internal to a DLL, and which should not be
 * exported outside of it.
 */

/**
 * @}
 */

#if 1 ==  CSTD_OS_LINUX
	#define CSTD_LINK_IMPORT __attribute__((visibility("default")))
	#define CSTD_LINK_EXPORT __attribute__((visibility("default")))
	#define CSTD_LINK_IMPL   __attribute__((visibility("default")))
	#define CSTD_LINK_LOCAL  __attribute__((visibility("hidden")))

#elif 1 ==  CSTD_OS_WINDOWS
	#define CSTD_LINK_IMPORT __declspec(dllimport)
	#define CSTD_LINK_EXPORT __declspec(dllexport)
	#define CSTD_LINK_IMPL   __declspec(dllexport)
	#define CSTD_LINK_LOCAL

#elif 1 ==  CSTD_OS_SYMBIAN
	#define CSTD_LINK_IMPORT IMPORT_C
	#define CSTD_LINK_EXPORT IMPORT_C
	#define CSTD_LINK_IMPL   EXPORT_C
	#define CSTD_LINK_LOCAL

#elif 1 ==  CSTD_OS_APPLEOS
	#define CSTD_LINK_IMPORT __attribute__((visibility("default")))
	#define CSTD_LINK_EXPORT __attribute__((visibility("default")))
	#define CSTD_LINK_IMPL   __attribute__((visibility("default")))
	#define CSTD_LINK_LOCAL  __attribute__((visibility("hidden")))

#elif 1 ==  CSTD_OS_ANDROID
	#define CSTD_LINK_IMPORT __attribute__((visibility("default")))
	#define CSTD_LINK_EXPORT __attribute__((visibility("default")))
	#define CSTD_LINK_IMPL   __attribute__((visibility("default")))
	#define CSTD_LINK_LOCAL  __attribute__((visibility("hidden")))

#else /* CSTD_OS_NONE */
	#define CSTD_LINK_IMPORT
	#define CSTD_LINK_EXPORT
	#define CSTD_LINK_IMPL
	#define CSTD_LINK_LOCAL

#endif

/**
 * @}
 */

#endif /* End (_ARM_CSTD_COMPILERS_H_) */
