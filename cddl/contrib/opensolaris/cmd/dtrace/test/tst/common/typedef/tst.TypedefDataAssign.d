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
 * Test the typedef keyword with the different D data types. Declare different
 * data types and test some of them with values.
 *
 * SECTION: Type and Constant Definitions/Typedef
 *
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma D option quiet

typedef char new_char;
typedef short new_short;
typedef int new_int;
typedef long new_long;
typedef long long new_long_long;
typedef int8_t new_int8;
typedef int16_t new_int16;
typedef int32_t new_int32;
typedef int64_t new_int64;
typedef intptr_t new_intptr;
typedef uint8_t new_uint8;
typedef uint16_t new_uint16;
typedef uint32_t new_uint32;
typedef uint64_t new_uint64;
typedef uintptr_t new_uintptr;
typedef float new_float;
typedef double new_double;
typedef long double new_long_double;

typedef int * pointer;

typedef struct {
	char ch;
	int in;
	long lg;
} new_struct;

typedef union {
	char ch;
	int in;
	long lg;
} new_union;

typedef enum {
	RED,
	GREEN,
	BLUE
} new_enum;

new_char c;
new_short s;
new_int i;
new_long l;
new_long_long ll;
new_int8 i8;
new_int16 i16;
new_int32 i32;
new_int64 i64;
new_intptr iptr;
new_uint8 ui8;
new_uint16 ui16;
new_uint32 ui32;
new_uint64 ui64;
new_uintptr uiptr;
new_float f;
new_double d;
new_long_double ld;
new_struct ns;
new_union nu;
new_enum ne;

pointer p;

BEGIN
{
	ns.ch = 'c';
	ns.in = 4;
	ns.lg = 4;

	nu.ch = 'd';
	nu.in = 5;
	nu.lg = 5;

	i = 10;

	printf("Struct: %c, %d, %d\n", ns.ch, ns.in, ns.lg);
	printf("Union: %c, %d, %d\n", nu.ch, nu.in, nu.lg);
	exit(0);
}
