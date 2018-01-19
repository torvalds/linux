// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include <linux/compiler.h>

unsigned int notrace __bswapsi2(unsigned int u)
{
	return (((u) & 0xff000000) >> 24) |
	       (((u) & 0x00ff0000) >>  8) |
	       (((u) & 0x0000ff00) <<  8) |
	       (((u) & 0x000000ff) << 24);
}

EXPORT_SYMBOL(__bswapsi2);
