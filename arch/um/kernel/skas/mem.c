/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/mm.h"
#include "asm/pgtable.h"
#include "mem_user.h"
#include "skas.h"

unsigned long set_task_sizes_skas(unsigned long *task_size_out)
{
	/* Round up to the nearest 4M */
	unsigned long host_task_size = ROUND_4M((unsigned long)
						&host_task_size);

	if (!skas_needs_stub)
		*task_size_out = host_task_size;
	else *task_size_out = CONFIG_STUB_START & PGDIR_MASK;

	return host_task_size;
}
