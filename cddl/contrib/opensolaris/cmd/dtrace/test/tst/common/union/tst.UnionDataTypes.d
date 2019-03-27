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

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * ASSERTION: Declaration of the different data types within a union and
 * their definitions in a later clause should work fine.
 *
 * SECTION: Structs and Unions/Unions
 *
 * NOTES: The floats, doubles and strings have not been implemented yet.
 * When they do, appropriate lines in the code below should be uncommented.
 * Similarly, the lines with the kmem_flags pointer assignment should be
 * uncommented when the issues pertaining to it are clarified.
 *
 */

#pragma D option quiet

union record {
	char new_char;
	short new_short;
	int new_int;
	long new_long;
	long long new_long_long;
	int8_t new_int8;
	int16_t new_int16;
	int32_t new_int32;
	int64_t new_int64;
	intptr_t new_intptr;
	uint8_t new_uint8;
	uint16_t new_uint16;
	uint32_t new_uint32;
	uint64_t new_uint64;
	uintptr_t new_uintptr;

	/*float new_float;
	double new_double;
	long double new_long_double;

	string new_string;
	*/

	struct {
	     char ch;
	     int in;
	     long lg;
	} new_struct;

	union {
	     char ch;
	     int in;
	     long lg;
	} new_union;

enum {
	RED,
	GREEN,
	BLUE
} new_enum;


	int *pointer;
} var;

/*
	var.pointer = &`kmem_flags;
*/
BEGIN
{
	var.new_char = 'c';
	var.new_short = 10;
	var.new_int = 100;
	var.new_long = 1234567890;
	var.new_long_long = 1234512345;
	var.new_int8 = 'p';
	var.new_int16 = 20;
	var.new_int32 = 200;
	var.new_int64 = 2000000;
	var.new_intptr = 0x12345;
	var.new_uint8 = 'q';
	var.new_uint16 = 30;
	var.new_uint32 = 300;
	var.new_uint64 = 3000000;
	var.new_uintptr = 0x67890;

	/* var.new_float = 1.23456;
	var.new_double = 2.34567890;
	var.new_long_double = 3.567890123;

	var.new_string = "hello";
	*/

	/*
	var.pointer = &`kmem_flags;
	*/

	var.new_struct.ch = 'c';
	var.new_struct.in = 4;
	var.new_struct.lg = 4;

	var.new_union.ch = 'd';
	var.new_union.in = 5;
	var.new_union.lg = 5;


	exit(0);
}
