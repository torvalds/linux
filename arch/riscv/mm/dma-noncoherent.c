// SPDX-License-Identifier: GPL-2.0-only
/*
 * RISC-V specific functions to support DMA for non-coherent devices
 *
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 */

#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>
#include <asm/dma-noncoherent.h>

static bool noncoherent_supported __ro_after_init;
int dma_cache_alignment __ro_after_init = ARCH_DMA_MINALIGN;
EXPORT_SYMBOL_GPL(dma_cache_alignment);

static inline void arch_dma_cache_wback(phys_addr_t paddr, size_t size)
{
	void *vaddr = phys_to_virt(paddr);

#ifdef CONFIG_RISCV_NONSTANDARD_CACHE_OPS
	if (unlikely(noncoherent_cache_ops.wback)) {
		noncoherent_cache_ops.wback(paddr, size);
		return;
	}
#endif
	ALT_CMO_OP(CLEAN, vaddr, size, riscv_cbom_block_size);
}

static inline void arch_dma_cache_inv(phys_addr_t paddr, size_t size)
{
	void *vaddr = phys_to_virt(paddr);

#ifdef CONFIG_RISCV_NONSTANDARD_CACHE_OPS
	if (unlikely(noncoherent_cache_ops.inv)) {
		noncoherent_cache_ops.inv(paddr, size);
		return;
	}
#endif

	ALT_CMO_OP(INVAL, vaddr, size, riscv_cbom_block_size);
}

static inline void arch_dma_cache_wback_inv(phys_addr_t paddr, size_t size)
{
	void *vaddr = phys_to_virt(paddr);

#ifdef CONFIG_RISCV_NONSTANDARD_CACHE_OPS
	if (unlikely(noncoherent_cache_ops.wback_inv)) {
		noncoherent_cache_ops.wback_inv(paddr, size);
		return;
	}
#endif

	ALT_CMO_OP(FLUSH, vaddr, size, riscv_cbom_block_size);
}

static inline bool arch_sync_dma_clean_before_fromdevice(void)
{
	return true;
}

static inline bool arch_sync_dma_cpu_needs_post_dma_flush(void)
{
	return true;
}

void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
			      enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		arch_dma_cache_wback(paddr, size);
		break;

	case DMA_FROM_DEVICE:
		if (!arch_sync_dma_clean_before_fromdevice()) {
			arch_dma_cache_inv(paddr, size);
			break;
		}
		fallthrough;

	case DMA_BIDIRECTIONAL:
		/* Skip the invalidate here if it's done later */
		if (IS_ENABLED(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) &&
		    arch_sync_dma_cpu_needs_post_dma_flush())
			arch_dma_cache_wback(paddr, size);
		else
			arch_dma_cache_wback_inv(paddr, size);
		break;

	default:
		break;
	}
}

void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
			   enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		break;

	case DMA_FROM_DEVICE:
	case DMA_BIDIRECTIONAL:
		/* FROM_DEVICE invalidate needed if speculative CPU prefetch only */
		if (arch_sync_dma_cpu_needs_post_dma_flush())
			arch_dma_cache_inv(paddr, size);
		break;

	default:
		break;
	}
}

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	void *flush_addr = page_address(page);

#ifdef CONFIG_RISCV_NONSTANDARD_CACHE_OPS
	if (unlikely(noncoherent_cache_ops.wback_inv)) {
		noncoherent_cache_ops.wback_inv(page_to_phys(page), size);
		return;
	}
#endif

	ALT_CMO_OP(FLUSH, flush_addr, size, riscv_cbom_block_size);
}

void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
			bool coherent)
{
	WARN_TAINT(!coherent && riscv_cbom_block_size > ARCH_DMA_MINALIGN,
		   TAINT_CPU_OUT_OF_SPEC,
		   "%s %s: ARCH_DMA_MINALIGN smaller than riscv,cbom-block-size (%d < %d)",
		   dev_driver_string(dev), dev_name(dev),
		   ARCH_DMA_MINALIGN, riscv_cbom_block_size);

	WARN_TAINT(!coherent && !noncoherent_supported, TAINT_CPU_OUT_OF_SPEC,
		   "%s %s: device non-coherent but no non-coherent operations supported",
		   dev_driver_string(dev), dev_name(dev));

	dev->dma_coherent = coherent;
}

void riscv_noncoherent_supported(void)
{
	WARN(!riscv_cbom_block_size,
	     "Non-coherent DMA support enabled without a block size\n");
	noncoherent_supported = true;
}

void __init riscv_set_dma_cache_alignment(void)
{
	if (!noncoherent_supported)
		dma_cache_alignment = 1;
}
