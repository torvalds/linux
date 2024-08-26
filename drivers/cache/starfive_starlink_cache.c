// SPDX-License-Identifier: GPL-2.0
/*
 * Cache Management Operations for StarFive's Starlink cache controller
 *
 * Copyright (C) 2024 Shanghai StarFive Technology Co., Ltd.
 *
 * Author: Joshua Yeong <joshua.yeong@starfivetech.com>
 */

#include <linux/bitfield.h>
#include <linux/cacheflush.h>
#include <linux/iopoll.h>
#include <linux/of_address.h>

#include <asm/dma-noncoherent.h>

#define STARLINK_CACHE_FLUSH_START_ADDR			0x0
#define STARLINK_CACHE_FLUSH_END_ADDR			0x8
#define STARLINK_CACHE_FLUSH_CTL			0x10
#define STARLINK_CACHE_ALIGN				0x40

#define STARLINK_CACHE_ADDRESS_RANGE_MASK		GENMASK(39, 0)
#define STARLINK_CACHE_FLUSH_CTL_MODE_MASK		GENMASK(2, 1)
#define STARLINK_CACHE_FLUSH_CTL_ENABLE_MASK		BIT(0)

#define STARLINK_CACHE_FLUSH_CTL_CLEAN_INVALIDATE	0
#define STARLINK_CACHE_FLUSH_CTL_MAKE_INVALIDATE	1
#define STARLINK_CACHE_FLUSH_CTL_CLEAN_SHARED		2
#define STARLINK_CACHE_FLUSH_POLL_DELAY_US		1
#define STARLINK_CACHE_FLUSH_TIMEOUT_US			5000000

static void __iomem *starlink_cache_base;

static void starlink_cache_flush_complete(void)
{
	volatile void __iomem *ctl = starlink_cache_base + STARLINK_CACHE_FLUSH_CTL;
	u64 v;
	int ret;

	ret = readq_poll_timeout_atomic(ctl, v, !(v & STARLINK_CACHE_FLUSH_CTL_ENABLE_MASK),
					STARLINK_CACHE_FLUSH_POLL_DELAY_US,
					STARLINK_CACHE_FLUSH_TIMEOUT_US);
	if (ret)
		WARN(1, "StarFive Starlink cache flush operation timeout\n");
}

static void starlink_cache_dma_cache_wback(phys_addr_t paddr, unsigned long size)
{
	writeq(FIELD_PREP(STARLINK_CACHE_ADDRESS_RANGE_MASK, paddr),
	       starlink_cache_base + STARLINK_CACHE_FLUSH_START_ADDR);
	writeq(FIELD_PREP(STARLINK_CACHE_ADDRESS_RANGE_MASK, paddr + size),
	       starlink_cache_base + STARLINK_CACHE_FLUSH_END_ADDR);

	mb();
	writeq(FIELD_PREP(STARLINK_CACHE_FLUSH_CTL_MODE_MASK,
			  STARLINK_CACHE_FLUSH_CTL_CLEAN_SHARED),
	       starlink_cache_base + STARLINK_CACHE_FLUSH_CTL);

	starlink_cache_flush_complete();
}

static void starlink_cache_dma_cache_invalidate(phys_addr_t paddr, unsigned long size)
{
	writeq(FIELD_PREP(STARLINK_CACHE_ADDRESS_RANGE_MASK, paddr),
	       starlink_cache_base + STARLINK_CACHE_FLUSH_START_ADDR);
	writeq(FIELD_PREP(STARLINK_CACHE_ADDRESS_RANGE_MASK, paddr + size),
	       starlink_cache_base + STARLINK_CACHE_FLUSH_END_ADDR);

	mb();
	writeq(FIELD_PREP(STARLINK_CACHE_FLUSH_CTL_MODE_MASK,
			  STARLINK_CACHE_FLUSH_CTL_MAKE_INVALIDATE),
	       starlink_cache_base + STARLINK_CACHE_FLUSH_CTL);

	starlink_cache_flush_complete();
}

static void starlink_cache_dma_cache_wback_inv(phys_addr_t paddr, unsigned long size)
{
	writeq(FIELD_PREP(STARLINK_CACHE_ADDRESS_RANGE_MASK, paddr),
	       starlink_cache_base + STARLINK_CACHE_FLUSH_START_ADDR);
	writeq(FIELD_PREP(STARLINK_CACHE_ADDRESS_RANGE_MASK, paddr + size),
	       starlink_cache_base + STARLINK_CACHE_FLUSH_END_ADDR);

	mb();
	writeq(FIELD_PREP(STARLINK_CACHE_FLUSH_CTL_MODE_MASK,
			  STARLINK_CACHE_FLUSH_CTL_CLEAN_INVALIDATE),
	       starlink_cache_base + STARLINK_CACHE_FLUSH_CTL);

	starlink_cache_flush_complete();
}

static const struct riscv_nonstd_cache_ops starlink_cache_ops = {
	.wback = &starlink_cache_dma_cache_wback,
	.inv = &starlink_cache_dma_cache_invalidate,
	.wback_inv = &starlink_cache_dma_cache_wback_inv,
};

static const struct of_device_id starlink_cache_ids[] = {
	{ .compatible = "starfive,jh8100-starlink-cache" },
	{ /* sentinel */ }
};

static int __init starlink_cache_init(void)
{
	struct device_node *np;
	u32 block_size;
	int ret;

	np = of_find_matching_node(NULL, starlink_cache_ids);
	if (!of_device_is_available(np))
		return -ENODEV;

	ret = of_property_read_u32(np, "cache-block-size", &block_size);
	if (ret)
		return ret;

	if (block_size % STARLINK_CACHE_ALIGN)
		return -EINVAL;

	starlink_cache_base = of_iomap(np, 0);
	if (!starlink_cache_base)
		return -ENOMEM;

	riscv_cbom_block_size = block_size;
	riscv_noncoherent_supported();
	riscv_noncoherent_register_cache_ops(&starlink_cache_ops);

	return 0;
}
arch_initcall(starlink_cache_init);
