// SPDX-License-Identifier: GPL-2.0
#include <asm/mach-au1x00/au1000.h>

#include "decompress.h"

void putc(char c)
{
	alchemy_uart_putchar(AU1000_UART0_PHYS_ADDR, c);
}
