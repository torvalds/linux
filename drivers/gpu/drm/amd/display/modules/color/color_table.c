/*
 * Copyright (c) 2019 Advanced Micro Devices, Inc. (unpublished)
 *
 * All rights reserved.  This notice is intended as a precaution against
 * inadvertent publication and does not imply publication or any waiver
 * of confidentiality.  The year included in the foregoing notice is the
 * year of creation of the work.
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

