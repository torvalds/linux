/*
 * (C) P. Horton 2006
 */
#include <linux/serial_reg.h>

#include <asm/addrspace.h>

#include <cobalt.h>

void prom_putchar(char c)
{
	while(!(COBALT_UART[UART_LSR] & UART_LSR_THRE))
		;

	COBALT_UART[UART_TX] = c;
}
