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
 * Declare different types of inline data types.
 *
 * SECTION: Type and Constant Definitions/Inlines
 *
 * NOTES: The commented lines defining floats and doubles should be uncommented
 * once the functionality is provided.
 *
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma D option quiet


inline char new_char = 'c';
inline short new_short = 10;
inline int new_int = 100;
inline long new_long = 1234567890;
inline long long new_long_long = 1234512345;
inline int8_t new_int8 = 'p';
inline int16_t new_int16 = 20;
inline int32_t new_int32 = 200;
inline int64_t new_int64 = 2000000;
inline intptr_t new_intptr = 0x12345;
inline uint8_t new_uint8 = 'q';
inline uint16_t new_uint16 = 30;
inline uint32_t new_uint32 = 300;
inline uint64_t new_uint64 = 3000000;
inline uintptr_t new_uintptr = 0x67890;
/* inline float new_float = 1.23456;
inline double new_double = 2.34567890;
inline long double new_long_double = 3.567890123;
*/

inline int * pointer = &`kmem_flags;

BEGIN
{
	exit(0);
}
