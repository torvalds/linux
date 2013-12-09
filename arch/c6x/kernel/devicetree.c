/*
 *  Architecture specific OF callbacks.
 *
 *  Copyright (C) 2011 Texas Instruments Incorporated
 *  Author: Mark Salter <msalter@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */
#include <linux/init.h>
#include <linux/memblock.h>

void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	c6x_add_memory(base, size);
}
