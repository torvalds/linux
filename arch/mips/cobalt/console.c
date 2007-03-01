/*
 * (C) P. Horton 2006
 */

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
