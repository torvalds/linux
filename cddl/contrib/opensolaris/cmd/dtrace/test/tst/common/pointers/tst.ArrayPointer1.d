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
 * ASSERTION: Declare arrays of different data types and verify that the
 * addresses of the individual elements differ by an amount equal to the number
 * elements separating them multiplied by the size of each element.
 *
 * SECTION: Pointers and Arrays/Array Declarations and Storage;
 * 	Pointers and Arrays/Pointer Arithmetic
 *
 * NOTES:
 *
 */

#pragma D option quiet

char char_array[5];
short short_array[5];
int int_array[5];
long long_array[5];
long long long_long_array[5];
int8_t int8_array[5];
int16_t int16_array[5];
int32_t int32_array[5];
int64_t int64_array[5];
intptr_t intptr_array[5];
uint8_t uint8_array[5];
uint16_t uint16_array[5];
uint32_t uint32_array[5];
uint64_t uint64_array[5];
uintptr_t uintptr_array[5];

/*
float float_array[5];
double double_array[5];
long double long_double_array[5];

string string_array[5];
*/

struct record {
	char ch;
	int in;
} struct_array[5];

struct {
	char ch;
	int in;
} anon_struct_array[5];

union record {
	char ch;
	int in;
} union_array[5];

union {
	char ch;
	int in;
} anon_union_array[5];

enum colors {
	RED,
	GREEN,
	BLUE
} enum_array[5];

BEGIN
{
	char_var0 = &char_array[0]; char_var2 = &char_array[2];
	short_var0 = &short_array[0]; short_var3 = &short_array[3];
	int_var3 = &int_array[3]; int_var5 = &int_array[5];

	long_var0 = &long_array[0]; long_var4 = &long_array[4];
	long_long_var0 = &long_long_array[0];
	long_long_var2 = &long_long_array[2];
	int8_var3 = &int8_array[3]; int8_var5 = &int8_array[5];

	int16_var0 = &int16_array[0]; int16_var4 = &int16_array[4];
	int32_var0 = &int32_array[0]; int32_var3 = &int32_array[3];
	int64_var0 = &int64_array[0]; int64_var1 = &int64_array[1];

	uintptr_var0 = &uintptr_array[0]; uintptr_var2 = &uintptr_array[2];
	struct_var0 = &struct_array[0]; struct_var2 = &struct_array[2];
	anon_struct_var3 = &anon_struct_array[3];
	anon_struct_var5 = &anon_struct_array[5];

	union_var0 = &union_array[0]; union_var3 = &union_array[3];
	anon_union_var0 = &anon_union_array[0];
	anon_union_var4 = &anon_union_array[4];
	enum_var0 = &enum_array[0]; enum_var2 = &enum_array[2];

	printf("char_var2 - char_var0: %d\n",
	(int) char_var2 - (int) char_var0);
	printf("short_var3 - short_var0: %d\n",
	(int) short_var3 - (int) short_var0);
	printf("int_var5 - int_var3: %d\n", (int) int_var5 - (int) int_var3);

	printf("long_var4 - long_var0: %d\n",
	(int) long_var4 - (int) long_var0);
	printf("long_long_var2 - long_long_var0: %d\n",
	(int) long_long_var2 - (int) long_long_var0);
	printf("int8_var5 - int8_var3: %d\n",
	(int) int8_var5 - (int) int8_var3);

	printf("int16_var4 - int16_var0: %d\n",
	(int) int16_var4 - (int) int16_var0);
	printf("int32_var3 - int32_var0: %d\n",
	(int) int32_var3 - (int) int32_var0);
	printf("int64_var1 - int64_var0: %d\n",
	(int) int64_var1 - (int) int64_var0);

	printf("uintptr_var2 - uintptr_var0: %d\n",
	(int) uintptr_var2 - (int) uintptr_var0);
	printf("struct_var2 - struct_var0: %d\n",
	(int) struct_var2 - (int) struct_var0);
	printf("anon_struct_var5 - anon_struct_var3: %d\n",
	(int) anon_struct_var5 - (int) anon_struct_var3);

	printf("union_var3 - union_var0: %d\n",
	(int) union_var3 - (int) union_var0);
	printf("anon_union_var4 - anon_union_var0: %d\n",
	(int) anon_union_var4 - (int) anon_union_var0);
	printf("enum_var2 - enum_var0: %d\n",
	(int) enum_var2 - (int) enum_var0);
	exit(0);
}

END
/(2 != ((int) char_var2 - (int) char_var0)) ||
    (6 != ((int) short_var3 - (int) short_var0)) ||
    (8 != ((int) int_var5 - (int) int_var3)) ||
    ((32 != ((int) long_var4 - (int) long_var0)) &&
    (16 != ((int) long_var4 - (int) long_var0))) ||
    (16 != ((int) long_long_var2 - (int) long_long_var0)) ||
    (2 != ((int) int8_var5 - (int) int8_var3))
    || (8 != ((int) int16_var4 - (int) int16_var0)) ||
    (12 != ((int) int32_var3 - (int) int32_var0)) ||
    (8 != ((int) int64_var1 - (int) int64_var0)) ||
    ((16 != ((int) uintptr_var2 - (int) uintptr_var0))
    && (8 != ((int) uintptr_var2 - (int) uintptr_var0))) ||
    (16 != ((int) struct_var2 - (int) struct_var0)) ||
    (16 != ((int) anon_struct_var5 - (int) anon_struct_var3))
    || (12 != ((int) union_var3 - (int) union_var0)) ||
    (16 != ((int) anon_union_var4 - (int) anon_union_var0)) ||
    (8 != ((int) enum_var2 - (int) enum_var0))/
{
	exit(1);
}
