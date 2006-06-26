/*
 * (C) P. Horton 2006
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/serial_reg.h>
#include <asm/addrspace.h>
#include <asm/mach-cobalt/cobalt.h>

static void putchar(int c)
{
	if(c == '\n')
		putchar('\r');

	while(!(COBALT_UART[UART_LSR] & UART_LSR_THRE))
		;

	COBALT_UART[UART_TX] = c;
}

static void cons_write(struct console *c, const char *s, unsigned n)
{
	while(n-- && *s)
		putchar(*s++);
}

static struct console cons_info =
{
	.name	= "uart",
	.write	= cons_write,
	.flags	= CON_PRINTBUFFER | CON_BOOT,
	.index	= -1,
};

void __init cobalt_early_console(void)
{
	register_console(&cons_info);

	printk("Cobalt: early console registered\n");
}

void __init disable_early_printk(void)
{
	unregister_console(&cons_info);
}
