/*
 * (C) P. Horton 2006
 */
#include <linux/io.h>
#include <linux/serial_reg.h>

#define UART_BASE	((void __iomem *)CKSEG1ADDR(0x1c800000))

void prom_putchar(char c)
{
	while (!(readb(UART_BASE + UART_LSR) & UART_LSR_THRE))
		;

	writeb(c, UART_BASE + UART_TX);
}
