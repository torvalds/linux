/*
 * Copyright (C) 1996 Paul Mackerras.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#include <asm/machdep.h>
#include <asm/udbg.h>
#include "nonstdio.h"

int xmon_write(const void *ptr, int nb)
{
	return udbg_write(ptr, nb);
}

int xmon_readchar(void)
{
	if (udbg_getc)
		return udbg_getc();
	return -1;
}
