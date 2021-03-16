/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "color_table.h"

static struct fixed31_32 pq_table[MAX_HW_POINTS + 2];
static struct fixed31_32 de_pq_table[MAX_HW_POINTS + 2];
static bool pq_initialized;
static bool de_pg_initialized;

bool mod_color_is_table_init(enum table_type type)
{
	bool ret = false;

	if (type == type_pq_table)
		ret = pq_initialized;
	if (type == type_de_pq_table)
		ret = de_pg_initialized;

	return ret;
}

struct fixed31_32 *mod_color_get_table(enum table_type type)
{
	struct fixed31_32 *table = NULL;

	if (type == type_pq_table)
		table = pq_table;
	if (type == type_de_pq_table)
		table = de_pq_table;

	return table;
}

void mod_color_set_table_init_state(enum table_type type, bool state)
{
	if (type == type_pq_table)
		pq_initialized = state;
	if (type == type_de_pq_table)
		de_pg_initialized = state;
}

