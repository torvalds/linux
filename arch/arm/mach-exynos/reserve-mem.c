/* linux/arch/arm/mach-exynos/reserve-mem.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Reserve mem helper functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef CONFIG_CMA_DEBUG
#define DEBUG
#endif

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <plat/cpu.h>
#include "reserve-mem.h"

#ifndef CONFIG_CMA_DEBUG
static inline void print_reserved_region(struct cma_region *reg)
{
	pr_info("S5P/CMA: Reserved 0x%08x@0x%08x for '%s'\n",
				reg->size, reg->start, reg->name);
}
#else
#define print_reserved_region(reg) do { } whlie (0)
#endif

static phys_addr_t __init __cma_reserve_regionset(struct cma_region *reg,
						  phys_addr_t max_addr)
{
	phys_addr_t last_addr = max_addr;

	for (; reg && reg->size; reg++) {
		reg->alignment = (reg->alignment) ?
				ALIGN(reg->alignment, SZ_64K) : SZ_64K;
		reg->size = ALIGN(reg->size, SZ_64K);

		if (!reg->start) {
			reg->start = __memblock_alloc_base(
					reg->size, reg->alignment, max_addr);
			if (reg->start == 0) {
				pr_err("S5P/CMA: Failed to reserve '%s'\n",
					reg->name);
				continue;
			}

			last_addr = reg->start;
		} else {
			reg->start = ALIGN(reg->size, SZ_64K);

			if (memblock_is_region_reserved(reg->start, reg->size)
				|| memblock_reserve(reg->start, reg->size)) {
				pr_err("S5P/CMA: Failed to reserve '%s' @ %#x\n",
					reg->name, reg->start);
				continue;
			}
		}

		reg->reserved = 1;

		if (cma_early_region_register(reg)) {
			pr_err("S5P/CMA: Failed to register '%s'\n", reg->name);
			memblock_free(reg->start, reg->size);
		}
		print_reserved_region(reg);
	}

	return last_addr;
}

static void __init __cma_secure_reserve_5410(phys_addr_t paddr_last,
	struct cma_region *regions_secure, struct cma_region *regions_adjacent)
{
	paddr_last = __cma_reserve_regionset(regions_adjacent, paddr_last);
	__cma_reserve_regionset(regions_secure, paddr_last);
}

static void __init __cma_secure_reserve(phys_addr_t paddr_last,
	struct cma_region *regions_secure, struct cma_region *regions_adjacent)
{
	size_t size_secure = 0, size_adj = 0;
	struct cma_region *reg_sec, *reg_adj;
	size_t order_region2;
	size_t size_region2;
	size_t aug_size;
	size_t align_secure;
	int ret;

	if (!regions_secure || !regions_secure->size)
		goto reserve_adjcent_regions;

	for (reg_sec = regions_secure; reg_sec->size != 0; reg_sec++) {
		reg_sec->size = PAGE_ALIGN(reg_sec->size);
		size_secure += reg_sec->size;
		if (!reg_sec->alignment)
			reg_sec->alignment = PAGE_SIZE;
	}

	reg_sec--;

	for (reg_adj = regions_adjacent; reg_adj && reg_adj->size; reg_adj++) {
		reg_adj->size = PAGE_ALIGN(reg_adj->size);

		if (reg_adj->alignment < PAGE_SIZE)
			reg_adj->alignment = PAGE_SIZE;
		if (reg_adj->alignment & ~reg_adj->alignment) {
			pr_err("S5P/CMA: Wrong alignment %#x for '%s'\n",
					reg_adj->alignment, reg_adj->name);
			reg_adj->alignment = PAGE_SIZE;
		}

		size_adj += reg_adj->size + reg_adj->alignment; /*approximate*/
	}

	/* Entire secure regions will be merged into 2
	 * consecutive regions. */
	align_secure = 1 << (get_order(size_secure) + PAGE_SHIFT - 1);

	/* Calculation of a subregion size */
	size_region2 = size_secure - align_secure;

	/* the sum of subregions must aligned by 1MB at least */
	order_region2 = max(20, get_order(size_region2) + PAGE_SHIFT);

	order_region2 -= 3; /* divide by 8 */
	size_region2 = ALIGN(size_region2, 1 << order_region2);

	aug_size = align_secure + size_region2 - size_secure;
	if (aug_size > 0) {
		reg_sec->size += aug_size;
		size_region2 += aug_size;
		size_secure += aug_size;
		pr_debug("S5P/CMA: Augmented size of '%s' by %#x B.\n",
			reg_sec->name, aug_size);
	}

	size_secure += size_adj;

	paddr_last -= size_secure;
	paddr_last = round_down(paddr_last, align_secure);

	while ((paddr_last >= PHYS_OFFSET) &&
		(!memblock_is_region_memory(paddr_last, size_secure) ||
		  memblock_is_region_reserved(paddr_last, size_secure) ||
		  memblock_reserve(paddr_last, size_secure)))
		paddr_last -= align_secure;

	if (paddr_last < PHYS_OFFSET) {
		pr_err("S5P/CMA: Failed to reserve secure regions\n");
		goto reserve_adjcent_regions;
	}

	size_secure -= size_adj;

	if (!paddr_last || memblock_reserve(paddr_last, size_secure)) {
		pr_err("S5P/CMA: Failed to reserve secure regions\n");
		goto reserve_adjcent_regions;
	}

	do {
		reg_sec->reserved = 1;
		reg_sec->start = paddr_last;

		paddr_last = reg_sec->start + reg_sec->size;

		print_reserved_region(reg_sec);
		ret = cma_early_region_register(reg_sec); /* 100% success */
	} while (reg_sec-- != regions_secure);

	if (reg_adj) {
		reg_adj--;

		do {
			reg_adj->start = ALIGN(paddr_last, reg_adj->alignment);

			if (memblock_reserve(reg_adj->start, reg_adj->size)) {
				pr_err("S5P/CMA: "
					"Failed to reserve %#x bytes for '%s'",
					reg_adj->size, reg_adj->name);
			} else {
				print_reserved_region(reg_adj);
				reg_adj->reserved = 1;
				ret = cma_early_region_register(reg_adj);
				paddr_last = reg_adj->start + reg_adj->size;
			}
		} while (reg_adj-- != regions_adjacent);
	}

	return;
reserve_adjcent_regions:
	if (regions_adjacent)
		exynos_cma_region_reserve(regions_adjacent, NULL, NULL, NULL);
}

/* exynos_cma_region_reserve - reserving physically contiguous memory regions
 * @regions: ordinary regions to reserve
 * @regions_secure: regions to be locked by secure regions that are not
 *           accessible from normal world when DRM play-back is working.
 * @regions_adjacent: regions that needs to be adjacent to the @regions_secure.
 *           All reiongs of @regions_adjacent must be adjacent to other near
 *           regions and the first region of @regions_adjacent is also adjacent
 *           to the last region of @regions_secure. all 'start' members of
 *           @regions_adjacent are ignored.
 *
 * This function reserves given physically contiguous regions earlier than
 * the buddy system is initialized so that prevents allocating the regions
 * by the kernel.
 */
void __init exynos_cma_region_reserve(struct cma_region *regions,
					struct cma_region *regions_secure,
					struct cma_region *regions_adjacent,
					const char *map)
{
	phys_addr_t paddr_last = 0xFFFFFFFF;
	struct cma_region *reg;
	int ret;

	if (map)
		cma_set_defaults(NULL, map);

	/* reserving order:
	   1. normal regions with start address
	   2. normal regions without start address
	 */
	for (reg = regions; reg && reg->size; reg++) {
		if (!reg->start)
			continue;

		if (!IS_ALIGNED(reg->start, PAGE_SIZE)) {
			pr_err("S5P/CMA: Failed to reserve '%s': "
				"address %#x not page-aligned\n",
				reg->name, reg->start);
			continue;
		}

		if (!IS_ALIGNED(reg->size, PAGE_SIZE)) {
			pr_debug("S5P/CMA: size of '%s' is not page-aligned\n",
								reg->name);
			reg->size = PAGE_ALIGN(reg->size);
		}

		if (!memblock_is_region_memory(reg->start, reg->size)) {
			pr_err("S5P/CMA: %s(0x%08x ~ 0x%08x)"
				" is outside of system memory",
				reg->name, reg->start, reg->start + reg->size);
			continue;
		}

		if (memblock_is_region_reserved(reg->start, reg->size) ||
			memblock_reserve(reg->start, reg->size)) {
			pr_err("S5P/CMA: Failed to reserve '%s'\n",
							reg->name);
			continue;
		}

		reg->reserved = 1;
		reg->alignment = PAGE_SIZE; /* for 100% success to register */

		print_reserved_region(reg);

		ret = cma_early_region_register(reg);
		paddr_last = min(paddr_last, reg->start);
	}

	for (reg = regions; reg && (reg->size != 0); reg++) {
		phys_addr_t paddr;

		if (reg->start)
			continue;

		if (!IS_ALIGNED(reg->size, PAGE_SIZE)) {
			pr_debug("S5P/CMA: size of '%s' is NOT page-aligned\n",
								reg->name);
			reg->size = PAGE_ALIGN(reg->size);
		}

		if (reg->reserved) {
			pr_err("S5P/CMA: '%s' already reserved\n", reg->name);
			continue;
		}

		if (reg->alignment) {
			if ((reg->alignment & ~PAGE_MASK) ||
				(reg->alignment & ~reg->alignment)) {
				pr_err("S5P/CMA: Failed to reserve '%s': "
						"incorrect alignment %#x.\n",
						reg->name, reg->alignment);
				continue;
			}
		} else {
			reg->alignment = PAGE_SIZE;
		}

		paddr = memblock_find_in_range(0, MEMBLOCK_ALLOC_ANYWHERE,
						reg->size, reg->alignment);

		if (paddr != 0) {
			if (memblock_reserve(paddr, reg->size)) {
				pr_err("S5P/CMA: Failed to reserve '%s'\n",
								reg->name);
				continue;
			}

			reg->start = paddr;
			reg->reserved = 1;
		} else {
			pr_err("S5P/CMA: No free space in memory for '%s'\n",
								reg->name);
			continue;
		}

		print_reserved_region(reg);

		ret = cma_early_region_register(reg); /* 100% success */
		paddr_last = min(paddr_last, reg->start);
	}

	if (soc_is_exynos5410())
		__cma_secure_reserve_5410(
				paddr_last, regions_secure, regions_adjacent);
	else
		__cma_secure_reserve(
				paddr_last, regions_secure, regions_adjacent);
}
