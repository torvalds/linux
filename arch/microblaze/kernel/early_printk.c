/*
 * Early printk support for Microblaze.
 *
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2003-2006 Yasushi SHOJI <yashi@atmark-techno.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/io.h>
#include <asm/processor.h>
#include <linux/fcntl.h>
#include <asm/setup.h>
#include <asm/prom.h>

static u32 early_console_initialized;
static u32 base_addr;

static void early_printk_putc(char c)
{
	/*
	 * Limit how many times we'll spin waiting for TX FIFO status.
	 * This will prevent lockups if the base address is incorrectly
	 * set, or any other issue on the UARTLITE.
	 * This limit is pretty arbitrary, unless we are at about 10 baud
	 * we'll never timeout on a working UART.
	 */

	unsigned retries = 10000;
	/* read status bit - 0x8 offset */
	while (--retries && (in_be32(base_addr + 8) & (1 << 3)))
		;

	/* Only attempt the iowrite if we didn't timeout */
	/* write to TX_FIFO - 0x4 offset */
	if (retries)
		out_be32(base_addr + 4, c & 0xff);
}

static void early_printk_write(struct console *unused,
					const char *s, unsigned n)
{
	while (*s && n-- > 0) {
		early_printk_putc(*s);
		if (*s == '\n')
			early_printk_putc('\r');
		s++;
	}
}

static struct console early_serial_console = {
	.name = "earlyser",
	.write = early_printk_write,
	.flags = CON_PRINTBUFFER,
	.index = -1,
};

static struct console *early_console = &early_serial_console;

void early_printk(const char *fmt, ...)
{
	char buf[512];
	int n;
	va_list ap;

	if (early_console_initialized) {
		va_start(ap, fmt);
		n = vscnprintf(buf, 512, fmt, ap);
		early_console->write(early_console, buf, n);
		va_end(ap);
	}
}

int __init setup_early_printk(char *opt)
{
	if (early_console_initialized)
		return 1;

	base_addr = early_uartlite_console();
	if (base_addr) {
		early_console_initialized = 1;
#ifdef CONFIG_MMU
		early_console_reg_tlb_alloc(base_addr);
#endif
		early_printk("early_printk_console is enabled at 0x%08x\n",
							base_addr);

		/* register_console(early_console); */

		return 0;
	} else
		return 1;
}

void __init disable_early_printk(void)
{
	if (!early_console_initialized || !early_console)
		return;
	printk(KERN_WARNING "disabling early console\n");
	unregister_console(early_console);
	early_console_initialized = 0;
}
