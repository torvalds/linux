/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/stddef.h"
#include "linux/mm.h"
#include "asm/uaccess.h"
#include "mem_user.h"
#include "kern_util.h"
#include "user_util.h"
#include "kern.h"
#include "tt.h"

void before_mem_tt(unsigned long brk_start)
{
	if(debug)
		remap_data(UML_ROUND_DOWN(&_stext), UML_ROUND_UP(&_etext), 1);
	remap_data(UML_ROUND_DOWN(&_sdata), UML_ROUND_UP(&_edata), 1);
	remap_data(UML_ROUND_DOWN(&__bss_start), UML_ROUND_UP(&_end), 1);
}

#define SIZE ((CONFIG_NEST_LEVEL + CONFIG_KERNEL_HALF_GIGS) * 0x20000000)
#define START (CONFIG_TOP_ADDR - SIZE)

unsigned long set_task_sizes_tt(unsigned long *task_size_out)
{
	unsigned long host_task_size;

	/* Round up to the nearest 4M */
	host_task_size = ROUND_4M((unsigned long) &host_task_size);
	*task_size_out = START;

	return host_task_size;
}
