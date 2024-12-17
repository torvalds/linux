// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2017
 */

#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/setup.h>
#include <asm/sclp.h>

static void sclp_early_write(struct console *con, const char *s, unsigned int len)
{
	__sclp_early_printk(s, len);
}

static struct console sclp_early_console = {
	.name  = "earlysclp",
	.write = sclp_early_write,
	.flags = CON_PRINTBUFFER | CON_BOOT,
	.index = -1,
};

void __init register_early_console(void)
{
	if (early_console)
		return;
	if (!sclp.has_linemode && !sclp.has_vt220)
		return;
	early_console = &sclp_early_console;
	register_console(early_console);
}

static int __init setup_early_printk(char *buf)
{
	if (early_console)
		return 0;
	/* Accept only "earlyprintk" and "earlyprintk=sclp" */
	if (buf && !str_has_prefix(buf, "sclp"))
		return 0;
	register_early_console();
	return 0;
}
early_param("earlyprintk", setup_early_printk);
