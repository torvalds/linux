// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/unicore32/mm/proc-syms.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#include <linux/module.h>
#include <linux/mm.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/page.h>

EXPORT_SYMBOL(cpu_dcache_clean_area);
EXPORT_SYMBOL(cpu_set_pte);

EXPORT_SYMBOL(__cpuc_coherent_kern_range);
