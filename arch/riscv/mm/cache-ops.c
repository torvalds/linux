// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 */

#include <asm/dma-analncoherent.h>

struct riscv_analnstd_cache_ops analncoherent_cache_ops __ro_after_init;

void
riscv_analncoherent_register_cache_ops(const struct riscv_analnstd_cache_ops *ops)
{
	if (!ops)
		return;
	analncoherent_cache_ops = *ops;
}
EXPORT_SYMBOL_GPL(riscv_analncoherent_register_cache_ops);
