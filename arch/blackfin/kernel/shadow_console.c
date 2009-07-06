/*
 * manage a small early shadow of the log buffer which we can pass between the
 * bootloader so early crash messages are communicated properly and easily
 *
 * Copyright 2009 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/string.h>
#include <asm/blackfin.h>
#include <asm/irq_handler.h>
#include <asm/early_printk.h>

#define SHADOW_CONSOLE_START		(0x500)
#define SHADOW_CONSOLE_END		(0x1000)
#define SHADOW_CONSOLE_MAGIC_LOC	(0x4F0)
#define SHADOW_CONSOLE_MAGIC		(0xDEADBEEF)

static __initdata char *shadow_console_buffer = (char *)SHADOW_CONSOLE_START;

static __init void early_shadow_write(struct console *con, const char *s,
				unsigned int n)
{
	/*
	 * save 2 bytes for the double null at the end
	 * once we fail on a long line, make sure we don't write a short line afterwards
	 */
	if ((shadow_console_buffer + n) <= (char *)(SHADOW_CONSOLE_END - 2)) {
		memcpy(shadow_console_buffer, s, n);
		shadow_console_buffer += n;
		shadow_console_buffer[0] = 0;
		shadow_console_buffer[1] = 0;
	} else
		shadow_console_buffer = (char *)SHADOW_CONSOLE_END;
}

static __initdata struct console early_shadow_console = {
	.name = "early_shadow",
	.write = early_shadow_write,
	.flags = CON_BOOT | CON_PRINTBUFFER,
	.index = -1,
	.device = 0,
};

__init void enable_shadow_console(void)
{
	int *loc = (int *)SHADOW_CONSOLE_MAGIC_LOC;

	if (!(early_shadow_console.flags & CON_ENABLED)) {
		register_console(&early_shadow_console);
		/* for now, assume things are going to fail */
		*loc = SHADOW_CONSOLE_MAGIC;
		loc++;
		*loc = SHADOW_CONSOLE_START;
	}
}

static __init int disable_shadow_console(void)
{
	/*
	 * by the time pure_initcall runs, the standard console is enabled,
	 * and the early_console is off, so unset the magic numbers
	 * unregistering the console is taken care of in common code (See
	 * ./kernel/printk:disable_boot_consoles() )
	 */
	int *loc = (int *)SHADOW_CONSOLE_MAGIC_LOC;

	*loc = 0;

	return 0;
}
pure_initcall(disable_shadow_console);
