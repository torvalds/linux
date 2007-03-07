/*
 * (C) P. Horton 2006
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/serial_reg.h>
#include <asm/addrspace.h>
#include <asm/mach-cobalt/cobalt.h>

void prom_putchar(char c)
{
	while(!(COBALT_UART[UART_LSR] & UART_LSR_THRE))
		;

	COBALT_UART[UART_TX] = c;
}
