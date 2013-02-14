/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/genalloc.h>
#include "linux/platform_data/imx-iram.h"

static unsigned long iram_phys_base;
static void __iomem *iram_virt_base;
static struct gen_pool *iram_pool;

static inline void __iomem *iram_phys_to_virt(unsigned long p)
{
	return iram_virt_base + (p - iram_phys_base);
}

void __iomem *iram_alloc(unsigned int size, unsigned long *dma_addr)
{
	if (!iram_pool)
		return NULL;

	*dma_addr = gen_pool_alloc(iram_pool, size);
	pr_debug("iram alloc - %dB@0x%lX\n", size, *dma_addr);
	if (!*dma_addr)
		return NULL;
	return iram_phys_to_virt(*dma_addr);
}
EXPORT_SYMBOL(iram_alloc);

void iram_free(unsigned long addr, unsigned int size)
{
	if (!iram_pool)
		return;

	gen_pool_free(iram_pool, addr, size);
}
EXPORT_SYMBOL(iram_free);

int __init iram_init(unsigned long base, unsigned long size)
{
	iram_phys_base = base;

	iram_pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!iram_pool)
		return -ENOMEM;

	gen_pool_add(iram_pool, base, size, -1);
	iram_virt_base = ioremap(iram_phys_base, size);
	if (!iram_virt_base)
		return -EIO;

	pr_debug("i.MX IRAM pool: %ld KB@0x%p\n", size / 1024, iram_virt_base);
	return 0;
}
