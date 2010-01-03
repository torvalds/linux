/*
 * Copyright (C) 1999, 2000, 2004, 2005  MIPS Technologies, Inc.
 *	All rights reserved.
 *	Authors: Carsten Langgaard <carstenl@mips.com>
 *		 Maciej W. Rozycki <macro@mips.com>
 * Portions copyright (C) 2009 Cisco Systems, Inc.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * PROM library initialisation code.
 */
#include <linux/init.h>
#include <linux/string.h>
#include <linux/kernel.h>

#include <asm/bootinfo.h>
#include <linux/io.h>
#include <asm/system.h>
#include <asm/cacheflush.h>
#include <asm/traps.h>

#include <asm/mips-boards/prom.h>
#include <asm/mips-boards/generic.h>
#include <asm/mach-powertv/asic.h>

static int *_prom_envp;
unsigned long _prom_memsize;

/*
 * YAMON (32-bit PROM) pass arguments and environment as 32-bit pointer.
 * This macro take care of sign extension, if running in 64-bit mode.
 */
#define prom_envp(index) ((char *)(long)_prom_envp[(index)])

char *prom_getenv(char *envname)
{
	char *result = NULL;

	if (_prom_envp != NULL) {
		/*
		 * Return a pointer to the given environment variable.
		 * In 64-bit mode: we're using 64-bit pointers, but all pointers
		 * in the PROM structures are only 32-bit, so we need some
		 * workarounds, if we are running in 64-bit mode.
		 */
		int i, index = 0;

		i = strlen(envname);

		while (prom_envp(index)) {
			if (strncmp(envname, prom_envp(index), i) == 0) {
				result = prom_envp(index + 1);
				break;
			}
			index += 2;
		}
	}

	return result;
}

/* TODO: Verify on linux-mips mailing list that the following two  */
/* functions are correct                                           */
/* TODO: Copy NMI and EJTAG exception vectors to memory from the   */
/* BootROM exception vectors. Flush their cache entries. test it.  */

static void __init mips_nmi_setup(void)
{
	void *base;
#if defined(CONFIG_CPU_MIPS32_R1)
	base = cpu_has_veic ?
		(void *)(CAC_BASE + 0xa80) :
		(void *)(CAC_BASE + 0x380);
#elif defined(CONFIG_CPU_MIPS32_R2)
	base = (void *)0xbfc00000;
#else
#error NMI exception handler address not defined
#endif
}

static void __init mips_ejtag_setup(void)
{
	void *base;

#if defined(CONFIG_CPU_MIPS32_R1)
	base = cpu_has_veic ?
		(void *)(CAC_BASE + 0xa00) :
		(void *)(CAC_BASE + 0x300);
#elif defined(CONFIG_CPU_MIPS32_R2)
	base = (void *)0xbfc00480;
#else
#error EJTAG exception handler address not defined
#endif
}

void __init prom_init(void)
{
	int prom_argc;
	char *prom_argv;

	prom_argc = fw_arg0;
	prom_argv = (char *) fw_arg1;
	_prom_envp = (int *) fw_arg2;
	_prom_memsize = (unsigned long) fw_arg3;

	board_nmi_handler_setup = mips_nmi_setup;
	board_ejtag_handler_setup = mips_ejtag_setup;

	if (prom_argc == 1)
		strlcat(arcs_cmdline, prom_argv, COMMAND_LINE_SIZE);

	configure_platform();
	prom_meminit();

#ifndef CONFIG_BOOTLOADER_DRIVER
	pr_info("\nBootloader driver isn't loaded...\n");
#endif
}
