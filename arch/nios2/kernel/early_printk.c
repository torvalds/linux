/*
 * Early printk for Nios2.
 *
 * Copyright (C) 2010, Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2009, Wind River Systems Inc
 *   Implemented by fredrik.markstrom@gmail.com and ivarholmqvist@gmail.com
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/console.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <asm/prom.h>

static unsigned long base_addr;

#if defined(CONFIG_SERIAL_ALTERA_JTAGUART_CONSOLE)

#define ALTERA_JTAGUART_DATA_REG		0
#define ALTERA_JTAGUART_CONTROL_REG		4
#define ALTERA_JTAGUART_CONTROL_WSPACE_MSK	0xFFFF0000
#define ALTERA_JTAGUART_CONTROL_AC_MSK		0x00000400

#define JUART_GET_CR() \
	__builtin_ldwio((void *)(base_addr + ALTERA_JTAGUART_CONTROL_REG))
#define JUART_SET_CR(v) \
	__builtin_stwio((void *)(base_addr + ALTERA_JTAGUART_CONTROL_REG), v)
#define JUART_SET_TX(v) \
	__builtin_stwio((void *)(base_addr + ALTERA_JTAGUART_DATA_REG), v)

static void early_console_write(struct console *con, const char *s, unsigned n)
{
	unsigned long status;

	while (n-- && *s) {
		while (((status = JUART_GET_CR())
				& ALTERA_JTAGUART_CONTROL_WSPACE_MSK) == 0) {
#if defined(CONFIG_SERIAL_ALTERA_JTAGUART_CONSOLE_BYPASS)
			if ((status & ALTERA_JTAGUART_CONTROL_AC_MSK) == 0)
				return;	/* no connection activity */
#endif
		}
		JUART_SET_TX(*s);
		s++;
	}
}

#elif defined(CONFIG_SERIAL_ALTERA_UART_CONSOLE)

#define ALTERA_UART_TXDATA_REG		4
#define ALTERA_UART_STATUS_REG		8
#define ALTERA_UART_STATUS_TRDY		0x0040

#define UART_GET_SR() \
	__builtin_ldwio((void *)(base_addr + ALTERA_UART_STATUS_REG))
#define UART_SET_TX(v) \
	__builtin_stwio((void *)(base_addr + ALTERA_UART_TXDATA_REG), v)

static void early_console_putc(char c)
{
	while (!(UART_GET_SR() & ALTERA_UART_STATUS_TRDY))
		;

	UART_SET_TX(c);
}

static void early_console_write(struct console *con, const char *s, unsigned n)
{
	while (n-- && *s) {
		early_console_putc(*s);
		if (*s == '\n')
			early_console_putc('\r');
		s++;
	}
}

#else
# error Neither SERIAL_ALTERA_JTAGUART_CONSOLE nor SERIAL_ALTERA_UART_CONSOLE \
selected
#endif

static struct console early_console = {
	.name	= "early",
	.write	= early_console_write,
	.flags	= CON_PRINTBUFFER | CON_BOOT,
	.index	= -1
};

void __init setup_early_printk(void)
{
#if defined(CONFIG_SERIAL_ALTERA_JTAGUART_CONSOLE) ||	\
	defined(CONFIG_SERIAL_ALTERA_UART_CONSOLE)
	base_addr = early_altera_uart_or_juart_console();
#else
	base_addr = 0;
#endif

	if (!base_addr)
		return;

#if defined(CONFIG_SERIAL_ALTERA_JTAGUART_CONSOLE_BYPASS)
	/* Clear activity bit so BYPASS doesn't stall if we've used JTAG for
	 * downloading the kernel. This might cause early data to be lost even
	 * if the JTAG terminal is running.
	 */
	JUART_SET_CR(JUART_GET_CR() | ALTERA_JTAGUART_CONTROL_AC_MSK);
#endif

	register_console(&early_console);
	pr_info("early_console initialized at 0x%08lx\n", base_addr);
}
