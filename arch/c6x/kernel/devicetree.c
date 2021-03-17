// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Architecture specific OF callbacks.
 *
 *  Copyright (C) 2011 Texas Instruments Incorporated
 *  Author: Mark Salter <msalter@redhat.com>
 */
#include <linux/init.h>
#include <linux/memblock.h>

void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	c6x_add_memory(base, size);
}
