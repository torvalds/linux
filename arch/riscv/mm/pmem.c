// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Ventana Micro Systems Inc.
 */

#include <linux/export.h>
#include <linux/libnvdimm.h>

#include <asm/cacheflush.h>
#include <asm/dma-noncoherent.h>

void arch_wb_cache_pmem(void *addr, size_t size)
{
#ifdef CONFIG_RISCV_NONSTANDARD_CACHE_OPS
	if (unlikely(noncoherent_cache_ops.wback)) {
		noncoherent_cache_ops.wback(virt_to_phys(addr), size);
		return;
	}
#endif
	ALT_CMO_OP(CLEAN, addr, size, riscv_cbom_block_size);
}
EXPORT_SYMBOL_GPL(arch_wb_cache_pmem);

void arch_invalidate_pmem(void *addr, size_t size)
{
#ifdef CONFIG_RISCV_NONSTANDARD_CACHE_OPS
	if (unlikely(noncoherent_cache_ops.inv)) {
		noncoherent_cache_ops.inv(virt_to_phys(addr), size);
		return;
	}
#endif
	ALT_CMO_OP(INVAL, addr, size, riscv_cbom_block_size);
}
EXPORT_SYMBOL_GPL(arch_invalidate_pmem);
