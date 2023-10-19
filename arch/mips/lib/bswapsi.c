// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include <linux/compiler.h>
#include <uapi/linux/swab.h>

/* To silence -Wmissing-prototypes. */
unsigned int __bswapsi2(unsigned int u);

unsigned int notrace __bswapsi2(unsigned int u)
{
	return ___constant_swab32(u);
}
EXPORT_SYMBOL(__bswapsi2);
