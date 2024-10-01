// SPDX-License-Identifier: GPL-2.0
/* TI K3 CPPI5 descriptors pool API
 *
 * Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/genalloc.h>
#include <linux/kernel.h>

#include "k3-cppi-desc-pool.h"

struct k3_cppi_desc_pool {
	struct device		*dev;
	dma_addr_t		dma_addr;
	void			*cpumem;	/* dma_alloc map */
	size_t			desc_size;
	size_t			mem_size;
	size_t			num_desc;
	struct gen_pool		*gen_pool;
	void			**desc_infos;
};

void k3_cppi_desc_pool_destroy(struct k3_cppi_desc_pool *pool)
{
	if (!pool)
		return;

	WARN(gen_pool_size(pool->gen_pool) != gen_pool_avail(pool->gen_pool),
	     "k3_knav_desc_pool size %zu != avail %zu",
	     gen_pool_size(pool->gen_pool),
	     gen_pool_avail(pool->gen_pool));
	if (pool->cpumem)
		dma_free_coherent(pool->dev, pool->mem_size, pool->cpumem,
				  pool->dma_addr);

	kfree(pool->desc_infos);

	gen_pool_destroy(pool->gen_pool);	/* frees pool->name */

	kfree(pool);
}
EXPORT_SYMBOL_GPL(k3_cppi_desc_pool_destroy);

struct k3_cppi_desc_pool *
k3_cppi_desc_pool_create_name(struct device *dev, size_t size,
			      size_t desc_size,
			      const char *name)
{
	struct k3_cppi_desc_pool *pool;
	const char *pool_name = NULL;
	int ret = -ENOMEM;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return ERR_PTR(ret);

	pool->dev = dev;
	pool->desc_size	= roundup_pow_of_two(desc_size);
	pool->num_desc	= size;
	pool->mem_size	= pool->num_desc * pool->desc_size;

	pool_name = kstrdup_const(name ? name : dev_name(pool->dev),
				  GFP_KERNEL);
	if (!pool_name)
		goto gen_pool_create_fail;

	pool->gen_pool = gen_pool_create(ilog2(pool->desc_size), -1);
	if (!pool->gen_pool) {
		kfree_const(pool_name);
		goto gen_pool_create_fail;
	}

	pool->gen_pool->name = pool_name;

	pool->desc_infos = kcalloc(pool->num_desc,
				   sizeof(*pool->desc_infos), GFP_KERNEL);
	if (!pool->desc_infos)
		goto gen_pool_desc_infos_alloc_fail;

	pool->cpumem = dma_alloc_coherent(pool->dev, pool->mem_size,
					  &pool->dma_addr, GFP_KERNEL);

	if (!pool->cpumem)
		goto dma_alloc_fail;

	ret = gen_pool_add_virt(pool->gen_pool, (unsigned long)pool->cpumem,
				(phys_addr_t)pool->dma_addr, pool->mem_size,
				-1);
	if (ret < 0) {
		dev_err(pool->dev, "pool add failed %d\n", ret);
		goto gen_pool_add_virt_fail;
	}

	return pool;

gen_pool_add_virt_fail:
	dma_free_coherent(pool->dev, pool->mem_size, pool->cpumem,
			  pool->dma_addr);
dma_alloc_fail:
	kfree(pool->desc_infos);
gen_pool_desc_infos_alloc_fail:
	gen_pool_destroy(pool->gen_pool);	/* frees pool->name */
gen_pool_create_fail:
	kfree(pool);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(k3_cppi_desc_pool_create_name);

dma_addr_t k3_cppi_desc_pool_virt2dma(struct k3_cppi_desc_pool *pool,
				      void *addr)
{
	return addr ? pool->dma_addr + (addr - pool->cpumem) : 0;
}
EXPORT_SYMBOL_GPL(k3_cppi_desc_pool_virt2dma);

void *k3_cppi_desc_pool_dma2virt(struct k3_cppi_desc_pool *pool, dma_addr_t dma)
{
	return dma ? pool->cpumem + (dma - pool->dma_addr) : NULL;
}
EXPORT_SYMBOL_GPL(k3_cppi_desc_pool_dma2virt);

void *k3_cppi_desc_pool_alloc(struct k3_cppi_desc_pool *pool)
{
	return (void *)gen_pool_alloc(pool->gen_pool, pool->desc_size);
}
EXPORT_SYMBOL_GPL(k3_cppi_desc_pool_alloc);

void k3_cppi_desc_pool_free(struct k3_cppi_desc_pool *pool, void *addr)
{
	gen_pool_free(pool->gen_pool, (unsigned long)addr, pool->desc_size);
}
EXPORT_SYMBOL_GPL(k3_cppi_desc_pool_free);

size_t k3_cppi_desc_pool_avail(struct k3_cppi_desc_pool *pool)
{
	return gen_pool_avail(pool->gen_pool) / pool->desc_size;
}
EXPORT_SYMBOL_GPL(k3_cppi_desc_pool_avail);

size_t k3_cppi_desc_pool_desc_size(const struct k3_cppi_desc_pool *pool)
{
	return pool->desc_size;
}
EXPORT_SYMBOL_GPL(k3_cppi_desc_pool_desc_size);

void *k3_cppi_desc_pool_cpuaddr(const struct k3_cppi_desc_pool *pool)
{
	return pool->cpumem;
}
EXPORT_SYMBOL_GPL(k3_cppi_desc_pool_cpuaddr);

void k3_cppi_desc_pool_desc_info_set(struct k3_cppi_desc_pool *pool,
				     int desc_idx, void *info)
{
	pool->desc_infos[desc_idx] = info;
}
EXPORT_SYMBOL_GPL(k3_cppi_desc_pool_desc_info_set);

void *k3_cppi_desc_pool_desc_info(const struct k3_cppi_desc_pool *pool,
				  int desc_idx)
{
	return pool->desc_infos[desc_idx];
}
EXPORT_SYMBOL_GPL(k3_cppi_desc_pool_desc_info);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TI K3 CPPI5 descriptors pool API");
