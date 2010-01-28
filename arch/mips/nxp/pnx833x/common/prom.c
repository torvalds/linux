/*
 *  prom.c:
 *
 *  Copyright 2008 NXP Semiconductors
 *	  Chris Steel <chris.steel@nxp.com>
 *    Daniel Laird <daniel.j.laird@nxp.com>
 *
 *  Based on software written by:
 *      Nikita Youshchenko <yoush@debian.org>, based on PNX8550 code.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/init.h>
#include <asm/bootinfo.h>
#include <linux/string.h>

void __init prom_init_cmdline(void)
{
	int argc = fw_arg0;
	char **argv = (char **)fw_arg1;
	char *c = &(arcs_cmdline[0]);
	int i;

	for (i = 1; i < argc; i++) {
		strcpy(c, argv[i]);
		c += strlen(argv[i]);
		if (i < argc-1)
			*c++ = ' ';
	}
	*c = 0;
}

char __init *prom_getenv(char *envname)
{
	extern char **prom_envp;
	char **env = prom_envp;
	int i;

	i = strlen(envname);

	while (*env) {
		if (strncmp(envname, *env, i) == 0 && *(*env+i) == '=')
			return *env + i + 1;
		env++;
	}

	return 0;
}

void __init prom_free_prom_memory(void)
{
}
