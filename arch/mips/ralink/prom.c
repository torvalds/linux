// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 2009 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2010 Joonas Lahtinen <joonas.lahtinen@gmail.com>
 *  Copyright (C) 2013 John Crispin <john@phrozen.org>
 */

#include <linux/string.h>

#include <asm/bootinfo.h>
#include <asm/addrspace.h>

#include <asm/mach-ralink/ralink_regs.h>

#include "common.h"

struct ralink_soc_info soc_info;

enum ralink_soc_type ralink_soc;
EXPORT_SYMBOL_GPL(ralink_soc);

const char *get_system_type(void)
{
	return soc_info.sys_type;
}

static __init void prom_init_cmdline(void)
{
	int argc;
	char **argv;
	int i;

	pr_debug("prom: fw_arg0=%08x fw_arg1=%08x fw_arg2=%08x fw_arg3=%08x\n",
	       (unsigned int)fw_arg0, (unsigned int)fw_arg1,
	       (unsigned int)fw_arg2, (unsigned int)fw_arg3);

	argc = fw_arg0;
	argv = (char **) KSEG1ADDR(fw_arg1);

	if (!argv) {
		pr_debug("argv=%p is invalid, skipping\n",
		       argv);
		return;
	}

	for (i = 0; i < argc; i++) {
		char *p = (char *) KSEG1ADDR(argv[i]);

		if (CPHYSADDR(p) && *p) {
			pr_debug("argv[%d]: %s\n", i, p);
			strlcat(arcs_cmdline, " ", sizeof(arcs_cmdline));
			strlcat(arcs_cmdline, p, sizeof(arcs_cmdline));
		}
	}
}

void __init prom_init(void)
{
	prom_soc_init(&soc_info);

	pr_info("SoC Type: %s\n", get_system_type());

	prom_init_cmdline();
}
