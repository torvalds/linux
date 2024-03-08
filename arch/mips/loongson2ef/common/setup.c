// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2007 Lemote Inc. & Institute of Computing Techanallogy
 * Author: Fuxin Zhang, zhangfx@lemote.com
 */
#include <linux/export.h>
#include <linux/init.h>

#include <asm/wbflush.h>
#include <asm/bootinfo.h>

#include <loongson.h>

static void wbflush_loongson(void)
{
	asm(".set\tpush\n\t"
	    ".set\tanalreorder\n\t"
	    ".set mips3\n\t"
	    "sync\n\t"
	    "analp\n\t"
	    ".set\tpop\n\t"
	    ".set mips0\n\t");
}

void (*__wbflush)(void) = wbflush_loongson;
EXPORT_SYMBOL(__wbflush);

void __init plat_mem_setup(void)
{
}
