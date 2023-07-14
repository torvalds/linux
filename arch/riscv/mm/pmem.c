// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Ventana Micro Systems Inc.
 */

#include <linux/export.h>
#include <linux/libnvdimm.h>

#include <asm/cacheflush.h>

void arch_wb_cache_pmem(void *addr, size_t size)
{
	ALT_CMO_OP(clean, addr, size, riscv_cbom_block_size);
}
EXPORT_SYMBOL_GPL(arch_wb_cache_pmem);

void arch_invalidate_pmem(void *addr, size_t size)
{
	ALT_CMO_OP(inval, addr, size, riscv_cbom_block_size);
}
EXPORT_SYMBOL_GPL(arch_invalidate_pmem);
