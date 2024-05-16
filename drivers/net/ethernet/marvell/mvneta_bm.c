/*
 * Driver for Marvell NETA network controller Buffer Manager.
 *
 * Copyright (C) 2015 Marvell
 *
 * Marcin Wojtas <mw@semihalf.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mbus.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include <net/hwbm.h>
#include "mvneta_bm.h"

#define MVNETA_BM_DRIVER_NAME "mvneta_bm"
#define MVNETA_BM_DRIVER_VERSION "1.0"

static void mvneta_bm_write(struct mvneta_bm *priv, u32 offset, u32 data)
{
	writel(data, priv->reg_base + offset);
}

static u32 mvneta_bm_read(struct mvneta_bm *priv, u32 offset)
{
	return readl(priv->reg_base + offset);
}

static void mvneta_bm_pool_enable(struct mvneta_bm *priv, int pool_id)
{
	u32 val;

	val = mvneta_bm_read(priv, MVNETA_BM_POOL_BASE_REG(pool_id));
	val |= MVNETA_BM_POOL_ENABLE_MASK;
	mvneta_bm_write(priv, MVNETA_BM_POOL_BASE_REG(pool_id), val);

	/* Clear BM cause register */
	mvneta_bm_write(priv, MVNETA_BM_INTR_CAUSE_REG, 0);
}

static void mvneta_bm_pool_disable(struct mvneta_bm *priv, int pool_id)
{
	u32 val;

	val = mvneta_bm_read(priv, MVNETA_BM_POOL_BASE_REG(pool_id));
	val &= ~MVNETA_BM_POOL_ENABLE_MASK;
	mvneta_bm_write(priv, MVNETA_BM_POOL_BASE_REG(pool_id), val);
}

static inline void mvneta_bm_config_set(struct mvneta_bm *priv, u32 mask)
{
	u32 val;

	val = mvneta_bm_read(priv, MVNETA_BM_CONFIG_REG);
	val |= mask;
	mvneta_bm_write(priv, MVNETA_BM_CONFIG_REG, val);
}

static inline void mvneta_bm_config_clear(struct mvneta_bm *priv, u32 mask)
{
	u32 val;

	val = mvneta_bm_read(priv, MVNETA_BM_CONFIG_REG);
	val &= ~mask;
	mvneta_bm_write(priv, MVNETA_BM_CONFIG_REG, val);
}

static void mvneta_bm_pool_target_set(struct mvneta_bm *priv, int pool_id,
				      u8 target_id, u8 attr)
{
	u32 val;

	val = mvneta_bm_read(priv, MVNETA_BM_XBAR_POOL_REG(pool_id));
	val &= ~MVNETA_BM_TARGET_ID_MASK(pool_id);
	val &= ~MVNETA_BM_XBAR_ATTR_MASK(pool_id);
	val |= MVNETA_BM_TARGET_ID_VAL(pool_id, target_id);
	val |= MVNETA_BM_XBAR_ATTR_VAL(pool_id, attr);

	mvneta_bm_write(priv, MVNETA_BM_XBAR_POOL_REG(pool_id), val);
}

int mvneta_bm_construct(struct hwbm_pool *hwbm_pool, void *buf)
{
	struct mvneta_bm_pool *bm_pool =
		(struct mvneta_bm_pool *)hwbm_pool->priv;
	struct mvneta_bm *priv = bm_pool->priv;
	dma_addr_t phys_addr;

	/* In order to update buf_cookie field of RX descriptor properly,
	 * BM hardware expects buf virtual address to be placed in the
	 * first four bytes of mapped buffer.
	 */
	*(u32 *)buf = (u32)buf;
	phys_addr = dma_map_single(&priv->pdev->dev, buf, bm_pool->buf_size,
				   DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(&priv->pdev->dev, phys_addr)))
		return -ENOMEM;

	mvneta_bm_pool_put_bp(priv, bm_pool, phys_addr);
	return 0;
}
EXPORT_SYMBOL_GPL(mvneta_bm_construct);

/* Create pool */
static int mvneta_bm_pool_create(struct mvneta_bm *priv,
				 struct mvneta_bm_pool *bm_pool)
{
	struct platform_device *pdev = priv->pdev;
	u8 target_id, attr;
	int size_bytes, err;
	size_bytes = sizeof(u32) * bm_pool->hwbm_pool.size;
	bm_pool->virt_addr = dma_alloc_coherent(&pdev->dev, size_bytes,
						&bm_pool->phys_addr,
						GFP_KERNEL);
	if (!bm_pool->virt_addr)
		return -ENOMEM;

	if (!IS_ALIGNED((u32)bm_pool->virt_addr, MVNETA_BM_POOL_PTR_ALIGN)) {
		dma_free_coherent(&pdev->dev, size_bytes, bm_pool->virt_addr,
				  bm_pool->phys_addr);
		dev_err(&pdev->dev, "BM pool %d is not %d bytes aligned\n",
			bm_pool->id, MVNETA_BM_POOL_PTR_ALIGN);
		return -ENOMEM;
	}

	err = mvebu_mbus_get_dram_win_info(bm_pool->phys_addr, &target_id,
					   &attr);
	if (err < 0) {
		dma_free_coherent(&pdev->dev, size_bytes, bm_pool->virt_addr,
				  bm_pool->phys_addr);
		return err;
	}

	/* Set pool address */
	mvneta_bm_write(priv, MVNETA_BM_POOL_BASE_REG(bm_pool->id),
			bm_pool->phys_addr);

	mvneta_bm_pool_target_set(priv, bm_pool->id, target_id,  attr);
	mvneta_bm_pool_enable(priv, bm_pool->id);

	return 0;
}

/* Notify the driver that BM pool is being used as specific type and return the
 * pool pointer on success
 */
struct mvneta_bm_pool *mvneta_bm_pool_use(struct mvneta_bm *priv, u8 pool_id,
					  enum mvneta_bm_type type, u8 port_id,
					  int pkt_size)
{
	struct mvneta_bm_pool *new_pool = &priv->bm_pools[pool_id];
	int num, err;

	if (new_pool->type == MVNETA_BM_LONG &&
	    new_pool->port_map != 1 << port_id) {
		dev_err(&priv->pdev->dev,
			"long pool cannot be shared by the ports\n");
		return NULL;
	}

	if (new_pool->type == MVNETA_BM_SHORT && new_pool->type != type) {
		dev_err(&priv->pdev->dev,
			"mixing pools' types between the ports is forbidden\n");
		return NULL;
	}

	if (new_pool->pkt_size == 0 || type != MVNETA_BM_SHORT)
		new_pool->pkt_size = pkt_size;

	/* Allocate buffers in case BM pool hasn't been used yet */
	if (new_pool->type == MVNETA_BM_FREE) {
		struct hwbm_pool *hwbm_pool = &new_pool->hwbm_pool;

		new_pool->priv = priv;
		new_pool->type = type;
		new_pool->buf_size = MVNETA_RX_BUF_SIZE(new_pool->pkt_size);
		hwbm_pool->frag_size =
			SKB_DATA_ALIGN(MVNETA_RX_BUF_SIZE(new_pool->pkt_size)) +
			SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
		hwbm_pool->construct = mvneta_bm_construct;
		hwbm_pool->priv = new_pool;
		mutex_init(&hwbm_pool->buf_lock);

		/* Create new pool */
		err = mvneta_bm_pool_create(priv, new_pool);
		if (err) {
			dev_err(&priv->pdev->dev, "fail to create pool %d\n",
				new_pool->id);
			return NULL;
		}

		/* Allocate buffers for this pool */
		num = hwbm_pool_add(hwbm_pool, hwbm_pool->size);
		if (num != hwbm_pool->size) {
			WARN(1, "pool %d: %d of %d allocated\n",
			     new_pool->id, num, hwbm_pool->size);
			return NULL;
		}
	}

	return new_pool;
}
EXPORT_SYMBOL_GPL(mvneta_bm_pool_use);

/* Free all buffers from the pool */
void mvneta_bm_bufs_free(struct mvneta_bm *priv, struct mvneta_bm_pool *bm_pool,
			 u8 port_map)
{
	int i;

	bm_pool->port_map &= ~port_map;
	if (bm_pool->port_map)
		return;

	mvneta_bm_config_set(priv, MVNETA_BM_EMPTY_LIMIT_MASK);

	for (i = 0; i < bm_pool->hwbm_pool.buf_num; i++) {
		dma_addr_t buf_phys_addr;
		u32 *vaddr;

		/* Get buffer physical address (indirect access) */
		buf_phys_addr = mvneta_bm_pool_get_bp(priv, bm_pool);

		/* Work-around to the problems when destroying the pool,
		 * when it occurs that a read access to BPPI returns 0.
		 */
		if (buf_phys_addr == 0)
			continue;

		vaddr = phys_to_virt(buf_phys_addr);
		if (!vaddr)
			break;

		dma_unmap_single(&priv->pdev->dev, buf_phys_addr,
				 bm_pool->buf_size, DMA_FROM_DEVICE);
		hwbm_buf_free(&bm_pool->hwbm_pool, vaddr);
	}

	mvneta_bm_config_clear(priv, MVNETA_BM_EMPTY_LIMIT_MASK);

	/* Update BM driver with number of buffers removed from pool */
	bm_pool->hwbm_pool.buf_num -= i;
}
EXPORT_SYMBOL_GPL(mvneta_bm_bufs_free);

/* Cleanup pool */
void mvneta_bm_pool_destroy(struct mvneta_bm *priv,
			    struct mvneta_bm_pool *bm_pool, u8 port_map)
{
	struct hwbm_pool *hwbm_pool = &bm_pool->hwbm_pool;
	bm_pool->port_map &= ~port_map;
	if (bm_pool->port_map)
		return;

	bm_pool->type = MVNETA_BM_FREE;

	mvneta_bm_bufs_free(priv, bm_pool, port_map);
	if (hwbm_pool->buf_num)
		WARN(1, "cannot free all buffers in pool %d\n", bm_pool->id);

	if (bm_pool->virt_addr) {
		dma_free_coherent(&priv->pdev->dev,
				  sizeof(u32) * hwbm_pool->size,
				  bm_pool->virt_addr, bm_pool->phys_addr);
		bm_pool->virt_addr = NULL;
	}

	mvneta_bm_pool_disable(priv, bm_pool->id);
}
EXPORT_SYMBOL_GPL(mvneta_bm_pool_destroy);

static void mvneta_bm_pools_init(struct mvneta_bm *priv)
{
	struct device_node *dn = priv->pdev->dev.of_node;
	struct mvneta_bm_pool *bm_pool;
	char prop[15];
	u32 size;
	int i;

	/* Activate BM unit */
	mvneta_bm_write(priv, MVNETA_BM_COMMAND_REG, MVNETA_BM_START_MASK);

	/* Create all pools with maximum size */
	for (i = 0; i < MVNETA_BM_POOLS_NUM; i++) {
		bm_pool = &priv->bm_pools[i];
		bm_pool->id = i;
		bm_pool->type = MVNETA_BM_FREE;

		/* Reset read pointer */
		mvneta_bm_write(priv, MVNETA_BM_POOL_READ_PTR_REG(i), 0);

		/* Reset write pointer */
		mvneta_bm_write(priv, MVNETA_BM_POOL_WRITE_PTR_REG(i), 0);

		/* Configure pool size according to DT or use default value */
		sprintf(prop, "pool%d,capacity", i);
		if (of_property_read_u32(dn, prop, &size)) {
			size = MVNETA_BM_POOL_CAP_DEF;
		} else if (size > MVNETA_BM_POOL_CAP_MAX) {
			dev_warn(&priv->pdev->dev,
				 "Illegal pool %d capacity %d, set to %d\n",
				 i, size, MVNETA_BM_POOL_CAP_MAX);
			size = MVNETA_BM_POOL_CAP_MAX;
		} else if (size < MVNETA_BM_POOL_CAP_MIN) {
			dev_warn(&priv->pdev->dev,
				 "Illegal pool %d capacity %d, set to %d\n",
				 i, size, MVNETA_BM_POOL_CAP_MIN);
			size = MVNETA_BM_POOL_CAP_MIN;
		} else if (!IS_ALIGNED(size, MVNETA_BM_POOL_CAP_ALIGN)) {
			dev_warn(&priv->pdev->dev,
				 "Illegal pool %d capacity %d, round to %d\n",
				 i, size, ALIGN(size,
				 MVNETA_BM_POOL_CAP_ALIGN));
			size = ALIGN(size, MVNETA_BM_POOL_CAP_ALIGN);
		}
		bm_pool->hwbm_pool.size = size;

		mvneta_bm_write(priv, MVNETA_BM_POOL_SIZE_REG(i),
				bm_pool->hwbm_pool.size);

		/* Obtain custom pkt_size from DT */
		sprintf(prop, "pool%d,pkt-size", i);
		if (of_property_read_u32(dn, prop, &bm_pool->pkt_size))
			bm_pool->pkt_size = 0;
	}
}

static void mvneta_bm_default_set(struct mvneta_bm *priv)
{
	u32 val;

	/* Mask BM all interrupts */
	mvneta_bm_write(priv, MVNETA_BM_INTR_MASK_REG, 0);

	/* Clear BM cause register */
	mvneta_bm_write(priv, MVNETA_BM_INTR_CAUSE_REG, 0);

	/* Set BM configuration register */
	val = mvneta_bm_read(priv, MVNETA_BM_CONFIG_REG);

	/* Reduce MaxInBurstSize from 32 BPs to 16 BPs */
	val &= ~MVNETA_BM_MAX_IN_BURST_SIZE_MASK;
	val |= MVNETA_BM_MAX_IN_BURST_SIZE_16BP;
	mvneta_bm_write(priv, MVNETA_BM_CONFIG_REG, val);
}

static int mvneta_bm_init(struct mvneta_bm *priv)
{
	mvneta_bm_default_set(priv);

	/* Allocate and initialize BM pools structures */
	priv->bm_pools = devm_kcalloc(&priv->pdev->dev, MVNETA_BM_POOLS_NUM,
				      sizeof(struct mvneta_bm_pool),
				      GFP_KERNEL);
	if (!priv->bm_pools)
		return -ENOMEM;

	mvneta_bm_pools_init(priv);

	return 0;
}

static int mvneta_bm_get_sram(struct device_node *dn,
			      struct mvneta_bm *priv)
{
	priv->bppi_pool = of_gen_pool_get(dn, "internal-mem", 0);
	if (!priv->bppi_pool)
		return -ENOMEM;

	priv->bppi_virt_addr = gen_pool_dma_alloc(priv->bppi_pool,
						  MVNETA_BM_BPPI_SIZE,
						  &priv->bppi_phys_addr);
	if (!priv->bppi_virt_addr)
		return -ENOMEM;

	return 0;
}

static void mvneta_bm_put_sram(struct mvneta_bm *priv)
{
	gen_pool_free(priv->bppi_pool, priv->bppi_phys_addr,
		      MVNETA_BM_BPPI_SIZE);
}

struct mvneta_bm *mvneta_bm_get(struct device_node *node)
{
	struct platform_device *pdev = of_find_device_by_node(node);

	return pdev ? platform_get_drvdata(pdev) : NULL;
}
EXPORT_SYMBOL_GPL(mvneta_bm_get);

void mvneta_bm_put(struct mvneta_bm *priv)
{
	platform_device_put(priv->pdev);
}
EXPORT_SYMBOL_GPL(mvneta_bm_put);

static int mvneta_bm_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct mvneta_bm *priv;
	int err;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct mvneta_bm), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->reg_base))
		return PTR_ERR(priv->reg_base);

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);
	err = clk_prepare_enable(priv->clk);
	if (err < 0)
		return err;

	err = mvneta_bm_get_sram(dn, priv);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to allocate internal memory\n");
		goto err_clk;
	}

	priv->pdev = pdev;

	/* Initialize buffer manager internals */
	err = mvneta_bm_init(priv);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to initialize controller\n");
		goto err_sram;
	}

	dn->data = priv;
	platform_set_drvdata(pdev, priv);

	dev_info(&pdev->dev, "Buffer Manager for network controller enabled\n");

	return 0;

err_sram:
	mvneta_bm_put_sram(priv);
err_clk:
	clk_disable_unprepare(priv->clk);
	return err;
}

static void mvneta_bm_remove(struct platform_device *pdev)
{
	struct mvneta_bm *priv = platform_get_drvdata(pdev);
	u8 all_ports_map = 0xff;
	int i = 0;

	for (i = 0; i < MVNETA_BM_POOLS_NUM; i++) {
		struct mvneta_bm_pool *bm_pool = &priv->bm_pools[i];

		mvneta_bm_pool_destroy(priv, bm_pool, all_ports_map);
	}

	mvneta_bm_put_sram(priv);

	/* Dectivate BM unit */
	mvneta_bm_write(priv, MVNETA_BM_COMMAND_REG, MVNETA_BM_STOP_MASK);

	clk_disable_unprepare(priv->clk);
}

static const struct of_device_id mvneta_bm_match[] = {
	{ .compatible = "marvell,armada-380-neta-bm" },
	{ }
};
MODULE_DEVICE_TABLE(of, mvneta_bm_match);

static struct platform_driver mvneta_bm_driver = {
	.probe = mvneta_bm_probe,
	.remove_new = mvneta_bm_remove,
	.driver = {
		.name = MVNETA_BM_DRIVER_NAME,
		.of_match_table = mvneta_bm_match,
	},
};

module_platform_driver(mvneta_bm_driver);

MODULE_DESCRIPTION("Marvell NETA Buffer Manager Driver - www.marvell.com");
MODULE_AUTHOR("Marcin Wojtas <mw@semihalf.com>");
MODULE_LICENSE("GPL v2");
