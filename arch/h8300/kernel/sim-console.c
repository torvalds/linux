/*
 * arch/h8300/kernel/early_printk.c
 *
 *  Copyright (C) 2009 Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>

static void sim_write(struct console *co, const char *ptr,
				 unsigned len)
{
	register const int fd __asm__("er0") = 1; /* stdout */
	register const char *_ptr __asm__("er1") = ptr;
	register const unsigned _len __asm__("er2") = len;

	__asm__(".byte 0x5e,0x00,0x00,0xc7\n\t" /* jsr @0xc7 (sys_write) */
		: : "g"(fd), "g"(_ptr), "g"(_len));
}

static struct console sim_console = {
	.name		= "sim_console",
	.write		= sim_write,
	.setup		= NULL,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

static char sim_console_buf[32];

static int sim_probe(struct platform_device *pdev)
{
	if (sim_console.data)
		return -EEXIST;

	if (!strstr(sim_console_buf, "keep"))
		sim_console.flags |= CON_BOOT;

	register_console(&sim_console);
	return 0;
}

static int sim_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver sim_driver = {
	.probe		= sim_probe,
	.remove		= sim_remove,
	.driver		= {
		.name	= "h8300-sim",
		.owner	= THIS_MODULE,
	},
};

early_platform_init_buffer("earlyprintk", &sim_driver,
			   sim_console_buf, ARRAY_SIZE(sim_console_buf));

static struct platform_device sim_console_device = {
	.name		= "h8300-sim",
	.id		= 0,
};

static struct platform_device *devices[] __initdata = {
	&sim_console_device,
};

void __init sim_console_register(void)
{
	early_platform_add_devices(devices,
				   ARRAY_SIZE(devices));
}
