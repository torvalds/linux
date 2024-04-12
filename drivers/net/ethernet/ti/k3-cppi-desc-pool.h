/* SPDX-License-Identifier: GPL-2.0 */
/* TI K3 CPPI5 descriptors pool
 *
 * Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com
 */

#ifndef K3_CPPI_DESC_POOL_H_
#define K3_CPPI_DESC_POOL_H_

#include <linux/device.h>
#include <linux/types.h>

struct k3_cppi_desc_pool;

void k3_cppi_desc_pool_destroy(struct k3_cppi_desc_pool *pool);
struct k3_cppi_desc_pool *
k3_cppi_desc_pool_create_name(struct device *dev, size_t size,
			      size_t desc_size,
			      const char *name);
#define k3_cppi_desc_pool_create(dev, size, desc_size) \
		k3_cppi_desc_pool_create_name(dev, size, desc_size, NULL)
dma_addr_t
k3_cppi_desc_pool_virt2dma(struct k3_cppi_desc_pool *pool, void *addr);
void *
k3_cppi_desc_pool_dma2virt(struct k3_cppi_desc_pool *pool, dma_addr_t dma);
void *k3_cppi_desc_pool_alloc(struct k3_cppi_desc_pool *pool);
void k3_cppi_desc_pool_free(struct k3_cppi_desc_pool *pool, void *addr);
size_t k3_cppi_desc_pool_avail(struct k3_cppi_desc_pool *pool);
size_t k3_cppi_desc_pool_desc_size(const struct k3_cppi_desc_pool *pool);
void *k3_cppi_desc_pool_cpuaddr(const struct k3_cppi_desc_pool *pool);

#endif /* K3_CPPI_DESC_POOL_H_ */
