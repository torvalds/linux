#include <asm/mach-au1x00/au1000.h>

void putc(char c)
{
	/* all current (Jan. 2010) in-kernel boards */
	alchemy_uart_putchar(AU1000_UART0_PHYS_ADDR, c);
}
