/*
 * cmdline.c: read the command line passed to us by the PROM.
 *
 * Copyright (C) 1998 Harald Koerfgen
 * Copyright (C) 2002, 2004  Maciej W. Rozycki
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/bootinfo.h>
#include <asm/dec/prom.h>

#undef PROM_DEBUG

void __init prom_init_cmdline(s32 argc, s32 *argv, u32 magic)
{
	char *arg;
	int start_arg, i;

	/*
	 * collect args and prepare cmd_line
	 */
	if (!prom_is_rex(magic))
		start_arg = 1;
	else
		start_arg = 2;
	for (i = start_arg; i < argc; i++) {
		arg = (void *)(long)(argv[i]);
		strcat(arcs_cmdline, arg);
		if (i < (argc - 1))
			strcat(arcs_cmdline, " ");
	}

#ifdef PROM_DEBUG
	printk("arcs_cmdline: %s\n", &(arcs_cmdline[0]));
#endif
}
