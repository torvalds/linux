// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 */

#include <asm/dma-noncoherent.h>

struct riscv_nonstd_cache_ops noncoherent_cache_ops __ro_after_init;

void
riscv_noncoherent_register_cache_ops(const struct riscv_nonstd_cache_ops *ops)
{
	if (!ops)
		return;
	noncoherent_cache_ops = *ops;
}
EXPORT_SYMBOL_GPL(riscv_noncoherent_register_cache_ops);
