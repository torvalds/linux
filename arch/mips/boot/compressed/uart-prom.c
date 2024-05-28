// SPDX-License-Identifier: GPL-2.0
#include <asm/setup.h>

#include "decompress.h"

void putc(char c)
{
	prom_putchar(c);
}
