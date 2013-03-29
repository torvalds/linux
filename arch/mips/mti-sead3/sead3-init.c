/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/init.h>
#include <linux/io.h>

#include <asm/bootinfo.h>
#include <asm/cacheflush.h>
#include <asm/traps.h>
#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/prom.h>

extern void prom_init_early_console(char port);

extern char except_vec_nmi;
extern char except_vec_ejtag_debug;

int prom_argc;
int *_prom_argv, *_prom_envp;

#define prom_envp(index) ((char *)(long)_prom_envp[(index)])

char *prom_getenv(char *envname)
{
	/*
	 * Return a pointer to the given environment variable.
	 * In 64-bit mode: we're using 64-bit pointers, but all pointers
	 * in the PROM structures are only 32-bit, so we need some
	 * workarounds, if we are running in 64-bit mode.
	 */
	int i, index = 0;

	i = strlen(envname);

	while (prom_envp(index)) {
		if (strncmp(envname, prom_envp(index), i) == 0)
			return prom_envp(index+1);
		index += 2;
	}

	return NULL;
}

static void __init mips_nmi_setup(void)
{
	void *base;

	base = cpu_has_veic ?
		(void *)(CAC_BASE + 0xa80) :
		(void *)(CAC_BASE + 0x380);
	memcpy(base, &except_vec_nmi, 0x80);
	flush_icache_range((unsigned long)base, (unsigned long)base + 0x80);
}

static void __init mips_ejtag_setup(void)
{
	void *base;

	base = cpu_has_veic ?
		(void *)(CAC_BASE + 0xa00) :
		(void *)(CAC_BASE + 0x300);
	memcpy(base, &except_vec_ejtag_debug, 0x80);
	flush_icache_range((unsigned long)base, (unsigned long)base + 0x80);
}

void __init prom_init(void)
{
	prom_argc = fw_arg0;
	_prom_argv = (int *) fw_arg1;
	_prom_envp = (int *) fw_arg2;

	board_nmi_handler_setup = mips_nmi_setup;
	board_ejtag_handler_setup = mips_ejtag_setup;

	prom_init_cmdline();
#ifdef CONFIG_EARLY_PRINTK
	if ((strstr(prom_getcmdline(), "console=ttyS0")) != NULL)
		prom_init_early_console(0);
	else if ((strstr(prom_getcmdline(), "console=ttyS1")) != NULL)
		prom_init_early_console(1);
#endif
#ifdef CONFIG_SERIAL_8250_CONSOLE
	if ((strstr(prom_getcmdline(), "console=")) == NULL)
		strcat(prom_getcmdline(), " console=ttyS0,38400n8r");
#endif
}

void prom_free_prom_memory(void)
{
}
