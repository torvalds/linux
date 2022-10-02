// SPDX-License-Identifier: GPL-2.0-only
/*
 * RISC-V specific functions to support DMA for non-coherent devices
 *
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 */

#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <asm/cacheflush.h>

unsigned int riscv_cbom_block_size;
EXPORT_SYMBOL_GPL(riscv_cbom_block_size);

static bool noncoherent_supported;

void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
			      enum dma_data_direction dir)
{
	void *vaddr = phys_to_virt(paddr);

	switch (dir) {
	case DMA_TO_DEVICE:
		ALT_CMO_OP(clean, vaddr, size, riscv_cbom_block_size);
		break;
	case DMA_FROM_DEVICE:
		ALT_CMO_OP(clean, vaddr, size, riscv_cbom_block_size);
		break;
	case DMA_BIDIRECTIONAL:
		ALT_CMO_OP(flush, vaddr, size, riscv_cbom_block_size);
		break;
	default:
		break;
	}
}

void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
			   enum dma_data_direction dir)
{
	void *vaddr = phys_to_virt(paddr);

	switch (dir) {
	case DMA_TO_DEVICE:
		break;
	case DMA_FROM_DEVICE:
	case DMA_BIDIRECTIONAL:
		ALT_CMO_OP(flush, vaddr, size, riscv_cbom_block_size);
		break;
	default:
		break;
	}
}

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	void *flush_addr = page_address(page);

	ALT_CMO_OP(flush, flush_addr, size, riscv_cbom_block_size);
}

void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
		const struct iommu_ops *iommu, bool coherent)
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

#ifdef CONFIG_RISCV_ISA_ZICBOM
void riscv_init_cbom_blocksize(void)
{
	struct device_node *node;
	unsigned long cbom_hartid;
	u32 val, probed_block_size;
	int ret;

	probed_block_size = 0;
	for_each_of_cpu_node(node) {
		unsigned long hartid;

		ret = riscv_of_processor_hartid(node, &hartid);
		if (ret)
			continue;

		/* set block-size for cbom extension if available */
		ret = of_property_read_u32(node, "riscv,cbom-block-size", &val);
		if (ret)
			continue;

		if (!probed_block_size) {
			probed_block_size = val;
			cbom_hartid = hartid;
		} else {
			if (probed_block_size != val)
				pr_warn("cbom-block-size mismatched between harts %lu and %lu\n",
					cbom_hartid, hartid);
		}
	}

	if (probed_block_size)
		riscv_cbom_block_size = probed_block_size;
}
#endif

void riscv_noncoherent_supported(void)
{
	WARN(!riscv_cbom_block_size,
	     "Non-coherent DMA support enabled without a block size\n");
	noncoherent_supported = true;
}
