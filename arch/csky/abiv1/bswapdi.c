// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/export.h>
#include <linux/compiler.h>
#include <uapi/linux/swab.h>

unsigned long long notrace __bswapdi2(unsigned long long u)
{
	return ___constant_swab64(u);
}
EXPORT_SYMBOL(__bswapdi2);
