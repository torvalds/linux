/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 * Portions copyright (C) 2009 Cisco Systems, Inc.
 *
 * This program is free software; you can distribute it and/or modify it
 * under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Kernel command line creation using the prom monitor (YAMON) argc/argv.
 */
#include <linux/init.h>
#include <linux/string.h>

#include <asm/bootinfo.h>

#include "init.h"

/*
 * YAMON (32-bit PROM) pass arguments and environment as 32-bit pointer.
 * This macro take care of sign extension.
 */
#define prom_argv(index) ((char *)(long)_prom_argv[(index)])

char * __init prom_getcmdline(void)
{
	return &(arcs_cmdline[0]);
}

void  __init prom_init_cmdline(void)
{
	int len;

	if (prom_argc != 1)
		return;

	len = strlen(arcs_cmdline);

	arcs_cmdline[len] = ' ';

	strlcpy(arcs_cmdline + len + 1, (char *)_prom_argv,
		COMMAND_LINE_SIZE - len - 1);
}
