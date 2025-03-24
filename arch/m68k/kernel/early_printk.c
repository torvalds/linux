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

asmlinkage void __init debug_cons_nputs(const char *s, unsigned n);

static void __ref debug_cons_write(struct console *c,
				   const char *s, unsigned n)
{
#if !(defined(CONFIG_SUN3) || defined(CONFIG_M68000) || \
      defined(CONFIG_COLDFIRE))
	if (MACH_IS_MVME147)
		mvme147_scc_write(c, s, n);
	else if (MACH_IS_MVME16x)
		mvme16x_cons_write(c, s, n);
	else
		debug_cons_nputs(s, n);
#endif
}

static struct console early_console_instance = {
	.name  = "debug",
	.write = debug_cons_write,
	.flags = CON_PRINTBUFFER | CON_BOOT,
	.index = -1
};

static int __init setup_early_printk(char *buf)
{
	if (early_console || buf)
		return 0;

	early_console = &early_console_instance;
	register_console(early_console);

	return 0;
}
early_param("earlyprintk", setup_early_printk);

/*
 * debug_cons_nputs() defined in arch/m68k/kernel/head.S cannot be called
 * after init sections are discarded (for platforms that use it).
 */
#if !(defined(CONFIG_SUN3) || defined(CONFIG_M68000) || \
      defined(CONFIG_COLDFIRE))

static int __init unregister_early_console(void)
{
	if (!early_console || MACH_IS_MVME16x)
		return 0;

	return unregister_console(early_console);
}
late_initcall(unregister_early_console);

#endif
