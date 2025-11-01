/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2014 Finn Thain
 */

#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/string.h>
#include <asm/setup.h>


#include "../mvme147/mvme147.h"
#include "../mvme16x/mvme16x.h"

asmlinkage void __init debug_cons_nputs(struct console *c, const char *s, unsigned int n);

static struct console early_console_instance = {
	.name  = "debug",
	.flags = CON_PRINTBUFFER | CON_BOOT,
	.index = -1
};

static int __init setup_early_printk(char *buf)
{
	if (early_console || buf)
		return 0;

	if (MACH_IS_MVME147)
		early_console_instance.write = mvme147_scc_write;
	else if (MACH_IS_MVME16x)
		early_console_instance.write = mvme16x_cons_write;
	else
		early_console_instance.write = debug_cons_nputs;
	early_console = &early_console_instance;
	register_console(early_console);

	return 0;
}
early_param("earlyprintk", setup_early_printk);

static int __init unregister_early_console(void)
{
	/*
	 * debug_cons_nputs() defined in arch/m68k/kernel/head.S cannot be
	 * called after init sections are discarded (for platforms that use it).
	 */
	if (early_console && early_console->write == debug_cons_nputs)
		return unregister_console(early_console);

	return 0;
}
late_initcall(unregister_early_console);
