/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * Modified from arch/mips/pnx833x/common/prom.c.
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/serial_reg.h>
#include <asm/bootinfo.h>

#include <loongson1.h>
#include <prom.h>

int prom_argc;
char **prom_argv, **prom_envp;
unsigned long memsize, highmemsize;

char *prom_getenv(char *envname)
{
	char **env = prom_envp;
	int i;

	i = strlen(envname);

	while (*env) {
		if (strncmp(envname, *env, i) == 0 && *(*env + i) == '=')
			return *env + i + 1;
		env++;
	}

	return 0;
}

static inline unsigned long env_or_default(char *env, unsigned long dfl)
{
	char *str = prom_getenv(env);
	return str ? simple_strtol(str, 0, 0) : dfl;
}

void __init prom_init_cmdline(void)
{
	char *c = &(arcs_cmdline[0]);
	int i;

	for (i = 1; i < prom_argc; i++) {
		strcpy(c, prom_argv[i]);
		c += strlen(prom_argv[i]);
		if (i < prom_argc - 1)
			*c++ = ' ';
	}
	*c = 0;
}

void __init prom_init(void)
{
	void __iomem *uart_base;
	prom_argc = fw_arg0;
	prom_argv = (char **)fw_arg1;
	prom_envp = (char **)fw_arg2;

	prom_init_cmdline();

	memsize = env_or_default("memsize", DEFAULT_MEMSIZE);
	highmemsize = env_or_default("highmemsize", 0x0);

	if (strstr(arcs_cmdline, "console=ttyS3"))
		uart_base = ioremap_nocache(LS1X_UART3_BASE, 0x0f);
	else if (strstr(arcs_cmdline, "console=ttyS2"))
		uart_base = ioremap_nocache(LS1X_UART2_BASE, 0x0f);
	else if (strstr(arcs_cmdline, "console=ttyS1"))
		uart_base = ioremap_nocache(LS1X_UART1_BASE, 0x0f);
	else
		uart_base = ioremap_nocache(LS1X_UART0_BASE, 0x0f);
	setup_8250_early_printk_port((unsigned long)uart_base, 0, 0);
}

void __init prom_free_prom_memory(void)
{
}
