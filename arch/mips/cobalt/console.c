/*
 * (C) P. Horton 2006
 */
#include <linux/io.h>
#include <linux/serial_reg.h>

#include <cobalt.h>

#define UART_BASE	((void __iomem *)CKSEG1ADDR(0x1c800000))

void prom_putchar(char c)
{
	if (cobalt_board_id <= COBALT_BRD_ID_QUBE1)
		return;

	while (!(readb(UART_BASE + UART_LSR) & UART_LSR_THRE))
		;

	writeb(c, UART_BASE + UART_TX);
}
