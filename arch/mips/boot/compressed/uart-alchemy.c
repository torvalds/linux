#include <asm/mach-au1x00/au1000.h>

void putc(char c)
{
#ifdef CONFIG_MIPS_DB1300
	alchemy_uart_putchar(AU1300_UART2_PHYS_ADDR, c);
#else
	alchemy_uart_putchar(AU1000_UART0_PHYS_ADDR, c);
#endif
}
