/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 Renesas Electronics Corp.
 */

#ifndef __ASM_DMA_NONCOHERENT_H
#define __ASM_DMA_NONCOHERENT_H

#include <linux/dma-direct.h>

/*
 * struct riscv_nonstd_cache_ops - Structure for non-standard CMO function pointers
 *
 * @wback: Function pointer for cache writeback
 * @inv: Function pointer for invalidating cache
 * @wback_inv: Function pointer for flushing the cache (writeback + invalidating)
 */
struct riscv_nonstd_cache_ops {
	void (*wback)(phys_addr_t paddr, size_t size);
	void (*inv)(phys_addr_t paddr, size_t size);
	void (*wback_inv)(phys_addr_t paddr, size_t size);
};

extern struct riscv_nonstd_cache_ops noncoherent_cache_ops;

void riscv_noncoherent_register_cache_ops(const struct riscv_nonstd_cache_ops *ops);

#endif	/* __ASM_DMA_NONCOHERENT_H */
