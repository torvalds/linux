/*
 * Declare directives for structure packing. No padding will be provided
 * between the members of packed structures, and therefore, there is no
 * guarantee that structure members will be aligned.
 *
 * Declaring packed structures is compiler specific. In order to handle all
 * cases, packed structures should be delared as:
 *
 * #include <packed_section_start.h>
 *
 * typedef BWL_PRE_PACKED_STRUCT struct foobar_t {
 *    some_struct_members;
 * } BWL_POST_PACKED_STRUCT foobar_t;
 *
 * #include <packed_section_end.h>
 *
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

/* EFI does not support STATIC_ASSERT */
#if defined(EFI)
#define _alignment_test_
#endif /* EFI */

#ifndef _alignment_test_
#define _alignment_test_

/* ASSERT default packing */
typedef struct T4 {
	uint8  a;
	uint32 b;
	uint16 c;
	uint8  d;
} T4_t;

/* 4 byte alignment support */
/*
* a . . .
* b b b b
* c c d .
*/

/*
 * Below function is meant to verify that this file is compiled with the default alignment of 4.
 * Function will fail to compile if the condition is not met.
 */
#ifdef __GNUC__
#define VARIABLE_IS_NOT_USED __attribute__ ((unused))
#else
#define VARIABLE_IS_NOT_USED
#endif
static void alignment_test(void);
static void
VARIABLE_IS_NOT_USED alignment_test(void)
{
	/* verify 4 byte alignment support */
	STATIC_ASSERT(sizeof(T4_t) == 12);
}
#endif /* _alignment_test_ */

/* Error check - BWL_PACKED_SECTION is defined in packed_section_start.h
 * and undefined in packed_section_end.h. If it is already defined at this
 * point, then there is a missing include of packed_section_end.h.
 */
#ifdef BWL_PACKED_SECTION
	#error "BWL_PACKED_SECTION is already defined!"
#else
	#define BWL_PACKED_SECTION
#endif

#if defined(BWL_DEFAULT_PACKING)
	/* generate an error if BWL_DEFAULT_PACKING is defined */
	#error "BWL_DEFAULT_PACKING not supported any more."
#endif /* BWL_PACKED_SECTION */

#if defined(_MSC_VER)
#pragma warning(disable:4103)
#pragma pack(push)
#pragma pack(1)
#endif

#if defined(__GNUC__) && defined(EFI)
#pragma pack(push)
#pragma pack(1)
#endif

/* Declare compiler-specific directives for structure packing. */
#if defined(_MSC_VER)
	#define	BWL_PRE_PACKED_STRUCT
	#define	BWL_POST_PACKED_STRUCT
#elif defined(__GNUC__) || defined(__lint)
	#define	BWL_PRE_PACKED_STRUCT
	#define	BWL_POST_PACKED_STRUCT	__attribute__ ((packed))
#elif defined(__CC_ARM)
	#define	BWL_PRE_PACKED_STRUCT	__packed
	#define	BWL_POST_PACKED_STRUCT
#else
	#error "Unknown compiler!"
#endif
