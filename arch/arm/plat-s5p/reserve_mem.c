/* linux/arch/arm/plat-s5p/reserve_mem.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Reserve mem helper functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/err.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <asm/setup.h>
#include <linux/io.h>
#include <mach/memory.h>
#include <plat/media.h>
#include <mach/media.h>

#ifdef CONFIG_CMA
#include <linux/cma.h>
void __init s5p_cma_region_reserve(struct cma_region *regions_normal,
				      struct cma_region *regions_secure,
				      size_t align_secure, const char *map)
{
	struct cma_region *reg;
	phys_addr_t paddr_last = 0xFFFFFFFF;

	for (reg = regions_normal; reg->size != 0; reg++) {
		phys_addr_t paddr;

		if (!IS_ALIGNED(reg->size, PAGE_SIZE)) {
			pr_debug("S5P/CMA: size of '%s' is NOT page-aligned\n",
								reg->name);
			reg->size = PAGE_ALIGN(reg->size);
		}


		if (reg->reserved) {
			pr_err("S5P/CMA: '%s' alread reserved\n", reg->name);
			continue;
		}

		if (reg->alignment) {
			if ((reg->alignment & ~PAGE_MASK) ||
				(reg->alignment & ~reg->alignment)) {
				pr_err("S5P/CMA: Failed to reserve '%s': "
						"incorrect alignment 0x%08x.\n",
						reg->name, reg->alignment);
				continue;
			}
		} else {
			reg->alignment = PAGE_SIZE;
		}

		if (reg->start) {
			if (!memblock_is_region_reserved(reg->start, reg->size)
			    && (memblock_reserve(reg->start, reg->size) == 0))
				reg->reserved = 1;
			else
				pr_err("S5P/CMA: Failed to reserve '%s'\n",
								reg->name);

			if (reg->reserved)
				pr_debug("S5P/CMA: "
					"Reserved 0x%08x/0x%08x for '%s'\n",
					reg->start, reg->size, reg->name);
			continue;
		}

		paddr = memblock_find_in_range(0, MEMBLOCK_ALLOC_ACCESSIBLE,
						reg->size, reg->alignment);
		if (paddr != MEMBLOCK_ERROR) {
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
		}

		pr_debug("S5P/CMA: Reserved 0x%08x/0x%08x for '%s'\n",
					reg->start, reg->size, reg->name);

		if (cma_early_region_register(reg)) {
			pr_err("S5P/CMA: Failed to register '%s'\n",
								reg->name);
			memblock_free(reg->start, reg->size);
		} else {
			paddr_last = min(paddr, paddr_last);
		}
	}

	if (align_secure & ~align_secure) {
		pr_err("S5P/CMA: "
			"Wrong alignment requirement for secure region.\n");
	} else if (regions_secure && regions_secure->size) {
		size_t size_secure = 0;

		for (reg = regions_secure; reg->size != 0; reg++)
			size_secure += reg->size;

		reg--;

		/* Entire secure regions will be merged into 2
		 * consecutive regions. */
		if (align_secure == 0) {
			size_t size_region2;
			size_t order_region2;
			size_t aug_size;

			align_secure = 1 <<
				(get_order((size_secure + 1) / 2) + PAGE_SHIFT);
			/* Calculation of a subregion size */
			size_region2 = size_secure - align_secure;
			order_region2 = get_order(size_region2) + PAGE_SHIFT;
			if (order_region2 < 20)
				order_region2 = 20; /* 1MB */
			order_region2 -= 3; /* divide by 8 */
			size_region2 = ALIGN(size_region2, 1 << order_region2);

			aug_size = align_secure + size_region2 - size_secure;
			if (aug_size > 0) {
				reg->size += aug_size;
				pr_debug("S5P/CMA: "
					"Augmented size of '%s' by %#x B.\n",
					reg->name, aug_size);
			}
		}

		size_secure = ALIGN(size_secure, align_secure);
		pr_debug("S5P/CMA: "
			"Reserving %#x for secure region aligned by %#x.\n",
						size_secure, align_secure);

		if (paddr_last >= memblock.current_limit) {
			paddr_last = memblock_find_in_range(0,
					MEMBLOCK_ALLOC_ACCESSIBLE,
					size_secure, reg->alignment);
		} else {
			paddr_last -= size_secure;
			paddr_last = round_down(paddr_last, align_secure);
		}

		if (paddr_last) {
			while (memblock_reserve(paddr_last, size_secure))
				paddr_last -= align_secure;

			do {
				reg->start = paddr_last;
				reg->reserved = 1;
				paddr_last += reg->size;

				pr_debug("S5P/CMA: "
					"Reserved 0x%08x/0x%08x for '%s'\n",
					reg->start, reg->size, reg->name);
				if (cma_early_region_register(reg)) {
					memblock_free(reg->start, reg->size);
					pr_err("S5P/CMA: "
					"Failed to register secure region "
					"'%s'\n", reg->name);
				} else {
					size_secure -= reg->size;
				}
			} while (reg-- != regions_secure);

			if (size_secure > 0)
				memblock_free(paddr_last, size_secure);
		} else {
			pr_err("S5P/CMA: Failed to reserve secure regions\n");
		}
	}

	if (map)
		cma_set_defaults(NULL, map);
}

#else
extern struct s5p_media_device media_devs[];
extern int nr_media_devs;

static dma_addr_t media_base[NR_BANKS];

static struct s5p_media_device *s5p_get_media_device(int dev_id, int bank)
{
	struct s5p_media_device *mdev = NULL;
	int i = 0, found = 0;

	if (dev_id < 0)
		return NULL;

	while (!found && (i < nr_media_devs)) {
		mdev = &media_devs[i];
		if (mdev->id == dev_id && mdev->bank == bank)
			found = 1;
		else
			i++;
	}

	if (!found)
		mdev = NULL;

	return mdev;
}

dma_addr_t s5p_get_media_memory_bank(int dev_id, int bank)
{
	struct s5p_media_device *mdev;

	mdev = s5p_get_media_device(dev_id, bank);
	if (!mdev) {
		printk(KERN_ERR "invalid media device\n");
		return 0;
	}

	if (!mdev->paddr) {
		printk(KERN_ERR "no memory for %s\n", mdev->name);
		return 0;
	}

	return mdev->paddr;
}
EXPORT_SYMBOL(s5p_get_media_memory_bank);

size_t s5p_get_media_memsize_bank(int dev_id, int bank)
{
	struct s5p_media_device *mdev;

	mdev = s5p_get_media_device(dev_id, bank);
	if (!mdev) {
		printk(KERN_ERR "invalid media device\n");
		return 0;
	}

	return mdev->memsize;
}
EXPORT_SYMBOL(s5p_get_media_memsize_bank);

dma_addr_t s5p_get_media_membase_bank(int bank)
{
	if (bank > meminfo.nr_banks) {
		printk(KERN_ERR "invalid bank.\n");
		return -EINVAL;
	}

	return media_base[bank];
}
EXPORT_SYMBOL(s5p_get_media_membase_bank);

void s5p_reserve_mem(size_t boundary)
{
	struct s5p_media_device *mdev;
	u64 start, end;
	int i, ret;

	for (i = 0; i < meminfo.nr_banks; i++)
		media_base[i] = meminfo.bank[i].start + meminfo.bank[i].size;

	for (i = 0; i < nr_media_devs; i++) {
		mdev = &media_devs[i];
		if (mdev->memsize <= 0)
			continue;

		if (mdev->bank > meminfo.nr_banks) {
			pr_err("mdev %s: mdev->bank(%d), max_bank(%d)\n",
				mdev->name, mdev->bank, meminfo.nr_banks);
			return;
		}

		if (!mdev->paddr) {
			start = meminfo.bank[mdev->bank].start;
			end = start + meminfo.bank[mdev->bank].size;

			if (boundary && (boundary < end - start))
				start = end - boundary;

			mdev->paddr = memblock_find_in_range(start, end,
						mdev->memsize, PAGE_SIZE);
		}

		ret = memblock_reserve(mdev->paddr, mdev->memsize);
		if (ret < 0)
			pr_err("memblock_reserve(%x, %x) failed\n",
				mdev->paddr, mdev->memsize);

		if (media_base[mdev->bank] > mdev->paddr)
			media_base[mdev->bank] = mdev->paddr;

		printk(KERN_INFO "s5p: %lu bytes system memory reserved "
			"for %s at 0x%08x, %d-bank base(0x%08x)\n",
			(unsigned long) mdev->memsize, mdev->name, mdev->paddr,
			mdev->bank, media_base[mdev->bank]);
	}
}
#endif /* CONFIG_CMA */
