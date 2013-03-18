/*
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/clk.h>

unsigned long core_freq = 800000000;

/*
 * As of now we default to device-tree provided clock
 * In future we can determine this in early boot
 */
int arc_set_core_freq(unsigned long freq)
{
	core_freq = freq;
	return 0;
}
