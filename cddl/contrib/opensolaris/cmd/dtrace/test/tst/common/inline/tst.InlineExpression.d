/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * ASSERTION:
 *
 * Test different inline assignments by various expressions.
 *
 * SECTION: Type and Constant Definitions/Inlines
 *
 * NOTES: The commented lines for the floats and doubles should be uncommented
 * once the functionality is implemented.
 *
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma D option quiet


inline char new_char = 'c' + 2;
inline short new_short = 10 * new_char;
inline int new_int = 100 + new_short;
inline long new_long = 1234567890;
inline long long new_long_long = 1234512345 * new_long;
inline int8_t new_int8 = 'p';
inline int16_t new_int16 = 20 / new_int8;
inline int32_t new_int32 = 200;
inline int64_t new_int64 = 2000000 * (-new_int16);
inline intptr_t new_intptr = 0x12345 - 129;
inline uint8_t new_uint8 = 'q';
inline uint16_t new_uint16 = 30 - new_uint8;
inline uint32_t new_uint32 = 300 - 0;
inline uint64_t new_uint64 = 3000000;
inline uintptr_t new_uintptr = 0x67890 / new_uint64;

/* inline float new_float = 1.23456;
inline double new_double = 2.34567890;
inline long double new_long_double = 3.567890123;
*/

inline int * pointer = &`kmem_flags;
inline int result = 3 > 2 ? 3 : 2;

BEGIN
{
	printf("new_char: %c\nnew_short: %d\nnew_int: %d\nnew_long: %d\n",
	    new_char, new_short, new_int, new_long);
	printf("new_long_long: %d\nnew_int8: %d\nnew_int16: %d\n",
	    new_long_long, new_int8, new_int16);
	printf("new_int32: %d\nnew_int64: %d\n", new_int32, new_int64);
	printf("new_intptr: %d\nnew_uint8: %d\nnew_uint16: %d\n",
	    new_intptr, new_uint8, new_uint16);
	printf("new_uint32:%d\nnew_uint64: %d\nnew_uintptr:%d\nresult:%d",
	    new_uint32, new_uint64, new_uintptr, result);
	exit(0);
}
