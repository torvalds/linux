/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * Copyright (C) 2015 Naveen N. Rao, IBM Corporation
 */

#include <asm/trace_clock.h>
#include <asm/time.h>

u64 notrace trace_clock_ppc_tb(void)
{
	return get_tb();
}
