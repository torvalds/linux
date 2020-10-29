// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Unisoc (Shanghai) Technologies Co., Ltd
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/android_debug_symbols.h>
#include <asm/stacktrace.h>
#include <asm/sections.h>

#include <linux/cma.h>
#include <linux/mm.h>
#include "../../mm/slab.h"
#include <linux/security.h>

struct ads_entry {
	char *name;
	void *addr;
};

#define _ADS_ENTRY(index, symbol)			\
	[index] = { .name = #symbol, .addr = (void *)symbol }
#define ADS_ENTRY(index, symbol) _ADS_ENTRY(index, symbol)

#define _ADS_PER_CPU_ENTRY(index, symbol)			\
	[index] = { .name = #symbol, .addr = (void *)&symbol }
#define ADS_PER_CPU_ENTRY(index, symbol) _ADS_PER_CPU_ENTRY(index, symbol)

/*
 * This module maintains static array of symbol and address information.
 * Add all required core kernel symbols and their addresses into ads_entries[] array,
 * so that vendor modules can query and to find address of non-exported symbol.
 */
static const struct ads_entry ads_entries[ADS_END] = {
	ADS_ENTRY(ADS_SDATA, _sdata),
	ADS_ENTRY(ADS_BSS_END, __bss_stop),
	ADS_ENTRY(ADS_PER_CPU_START, __per_cpu_start),
	ADS_ENTRY(ADS_PER_CPU_END, __per_cpu_end),
	ADS_ENTRY(ADS_TEXT, _text),
	ADS_ENTRY(ADS_SEND, _end),
	ADS_ENTRY(ADS_LINUX_BANNER, linux_banner),
	ADS_ENTRY(ADS_TOTAL_CMA, &totalcma_pages),
	ADS_ENTRY(ADS_SLAB_CACHES, &slab_caches),
	ADS_ENTRY(ADS_SLAB_MUTEX, &slab_mutex),
};

/*
 * ads_per_cpu_entries array contains all the per_cpu variable address information.
 */
static const struct ads_entry ads_per_cpu_entries[ADS_DEBUG_PER_CPU_END] = {
#ifdef CONFIG_ARM64
	ADS_PER_CPU_ENTRY(ADS_IRQ_STACK_PTR, irq_stack_ptr),
#endif
#ifdef CONFIG_X86
	ADS_PER_CPU_ENTRY(ADS_IRQ_STACK_PTR, hardirq_stack_ptr),
#endif
};

/*
 * android_debug_symbol - Provide address inforamtion of debug symbol.
 * @symbol: Index of debug symbol array.
 *
 * Return address of core kernel symbol on success and a negative errno will be
 * returned in error cases.
 *
 */
void *android_debug_symbol(enum android_debug_symbol symbol)
{
	if (symbol >= ADS_END)
		return ERR_PTR(-EINVAL);

	return ads_entries[symbol].addr;
}
EXPORT_SYMBOL_NS_GPL(android_debug_symbol, MINIDUMP);

/*
 * android_debug_per_cpu_symbol - Provide address inforamtion of per cpu debug symbol.
 * @symbol: Index of per cpu debug symbol array.
 *
 * Return address of core kernel symbol on success and a negative errno will be
 * returned in error cases.
 *
 */
void *android_debug_per_cpu_symbol(enum android_debug_per_cpu_symbol symbol)
{
	if (symbol >= ADS_DEBUG_PER_CPU_END)
		return ERR_PTR(-EINVAL);

	return ads_per_cpu_entries[symbol].addr;
}
EXPORT_SYMBOL_NS_GPL(android_debug_per_cpu_symbol, MINIDUMP);

