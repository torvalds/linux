// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Marvell PPv2 network controller for Armada 375 SoC.
 *
 * Copyright (C) 2014 Marvell
 *
 * Marcin Wojtas <mw@semihalf.com>
 */

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include <linux/inetdevice.h>
#include <linux/mbus.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/interrupt.h>
#include <linux/cpumask.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/phy/phy.h>
#include <linux/clk.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/regmap.h>
#include <uapi/linux/ppp_defs.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/tso.h>

#include "mvpp2.h"
#include "mvpp2_prs.h"
#include "mvpp2_cls.h"

enum mvpp2_bm_pool_log_num {
	MVPP2_BM_SHORT,
	MVPP2_BM_LONG,
	MVPP2_BM_JUMBO,
	MVPP2_BM_POOLS_NUM
};

static struct {
	int pkt_size;
	int buf_num;
} mvpp2_pools[MVPP2_BM_POOLS_NUM];

/* The prototype is added here to be used in start_dev when using ACPI. This
 * will be removed once phylink is used for all modes (dt+ACPI).
 */
static void mvpp2_mac_config(struct net_device *dev, unsigned int mode,
			     const struct phylink_link_state *state);
static void mvpp2_mac_link_up(struct net_device *dev, unsigned int mode,
			      phy_interface_t interface, struct phy_device *phy);

/* Queue modes */
#define MVPP2_QDIST_SINGLE_MODE	0
#define MVPP2_QDIST_MULTI_MODE	1

static int queue_mode = MVPP2_QDIST_MULTI_MODE;

module_param(queue_mode, int, 0444);
MODULE_PARM_DESC(queue_mode, "Set queue_mode (single=0, multi=1)");

/* Utility/helper methods */

void mvpp2_write(struct mvpp2 *priv, u32 offset, u32 data)
{
	writel(data, priv->swth_base[0] + offset);
}

u32 mvpp2_read(struct mvpp2 *priv, u32 offset)
{
	return readl(priv->swth_base[0] + offset);
}

u32 mvpp2_read_relaxed(struct mvpp2 *priv, u32 offset)
{
	return readl_relaxed(priv->swth_base[0] + offset);
}
/* These accessors should be used to access:
 *
 * - per-CPU registers, where each CPU has its own copy of the
 *   register.
 *
 *   MVPP2_BM_VIRT_ALLOC_REG
 *   MVPP2_BM_ADDR_HIGH_ALLOC
 *   MVPP22_BM_ADDR_HIGH_RLS_REG
 *   MVPP2_BM_VIRT_RLS_REG
 *   MVPP2_ISR_RX_TX_CAUSE_REG
 *   MVPP2_ISR_RX_TX_MASK_REG
 *   MVPP2_TXQ_NUM_REG
 *   MVPP2_AGGR_TXQ_UPDATE_REG
 *   MVPP2_TXQ_RSVD_REQ_REG
 *   MVPP2_TXQ_RSVD_RSLT_REG
 *   MVPP2_TXQ_SENT_REG
 *   MVPP2_RXQ_NUM_REG
 *
 * - global registers that must be accessed through a specific CPU
 *   window, because they are related to an access to a per-CPU
 *   register
 *
 *   MVPP2_BM_PHY_ALLOC_REG    (related to MVPP2_BM_VIRT_ALLOC_REG)
 *   MVPP2_BM_PHY_RLS_REG      (related to MVPP2_BM_VIRT_RLS_REG)
 *   MVPP2_RXQ_THRESH_REG      (related to MVPP2_RXQ_NUM_REG)
 *   MVPP2_RXQ_DESC_ADDR_REG   (related to MVPP2_RXQ_NUM_REG)
 *   MVPP2_RXQ_DESC_SIZE_REG   (related to MVPP2_RXQ_NUM_REG)
 *   MVPP2_RXQ_INDEX_REG       (related to MVPP2_RXQ_NUM_REG)
 *   MVPP2_TXQ_PENDING_REG     (related to MVPP2_TXQ_NUM_REG)
 *   MVPP2_TXQ_DESC_ADDR_REG   (related to MVPP2_TXQ_NUM_REG)
 *   MVPP2_TXQ_DESC_SIZE_REG   (related to MVPP2_TXQ_NUM_REG)
 *   MVPP2_TXQ_INDEX_REG       (related to MVPP2_TXQ_NUM_REG)
 *   MVPP2_TXQ_PENDING_REG     (related to MVPP2_TXQ_NUM_REG)
 *   MVPP2_TXQ_PREF_BUF_REG    (related to MVPP2_TXQ_NUM_REG)
 *   MVPP2_TXQ_PREF_BUF_REG    (related to MVPP2_TXQ_NUM_REG)
 */
void mvpp2_percpu_write(struct mvpp2 *priv, int cpu,
			       u32 offset, u32 data)
{
	writel(data, priv->swth_base[cpu] + offset);
}

u32 mvpp2_percpu_read(struct mvpp2 *priv, int cpu,
			     u32 offset)
{
	return readl(priv->swth_base[cpu] + offset);
}

void mvpp2_percpu_write_relaxed(struct mvpp2 *priv, int cpu,
				       u32 offset, u32 data)
{
	writel_relaxed(data, priv->swth_base[cpu] + offset);
}

static u32 mvpp2_percpu_read_relaxed(struct mvpp2 *priv, int cpu,
				     u32 offset)
{
	return readl_relaxed(priv->swth_base[cpu] + offset);
}

static dma_addr_t mvpp2_txdesc_dma_addr_get(struct mvpp2_port *port,
					    struct mvpp2_tx_desc *tx_desc)
{
	if (port->priv->hw_version == MVPP21)
		return le32_to_cpu(tx_desc->pp21.buf_dma_addr);
	else
		return le64_to_cpu(tx_desc->pp22.buf_dma_addr_ptp) &
		       MVPP2_DESC_DMA_MASK;
}

static void mvpp2_txdesc_dma_addr_set(struct mvpp2_port *port,
				      struct mvpp2_tx_desc *tx_desc,
				      dma_addr_t dma_addr)
{
	dma_addr_t addr, offset;

	addr = dma_addr & ~MVPP2_TX_DESC_ALIGN;
	offset = dma_addr & MVPP2_TX_DESC_ALIGN;

	if (port->priv->hw_version == MVPP21) {
		tx_desc->pp21.buf_dma_addr = cpu_to_le32(addr);
		tx_desc->pp21.packet_offset = offset;
	} else {
		__le64 val = cpu_to_le64(addr);

		tx_desc->pp22.buf_dma_addr_ptp &= ~cpu_to_le64(MVPP2_DESC_DMA_MASK);
		tx_desc->pp22.buf_dma_addr_ptp |= val;
		tx_desc->pp22.packet_offset = offset;
	}
}

static size_t mvpp2_txdesc_size_get(struct mvpp2_port *port,
				    struct mvpp2_tx_desc *tx_desc)
{
	if (port->priv->hw_version == MVPP21)
		return le16_to_cpu(tx_desc->pp21.data_size);
	else
		return le16_to_cpu(tx_desc->pp22.data_size);
}

static void mvpp2_txdesc_size_set(struct mvpp2_port *port,
				  struct mvpp2_tx_desc *tx_desc,
				  size_t size)
{
	if (port->priv->hw_version == MVPP21)
		tx_desc->pp21.data_size = cpu_to_le16(size);
	else
		tx_desc->pp22.data_size = cpu_to_le16(size);
}

static void mvpp2_txdesc_txq_set(struct mvpp2_port *port,
				 struct mvpp2_tx_desc *tx_desc,
				 unsigned int txq)
{
	if (port->priv->hw_version == MVPP21)
		tx_desc->pp21.phys_txq = txq;
	else
		tx_desc->pp22.phys_txq = txq;
}

static void mvpp2_txdesc_cmd_set(struct mvpp2_port *port,
				 struct mvpp2_tx_desc *tx_desc,
				 unsigned int command)
{
	if (port->priv->hw_version == MVPP21)
		tx_desc->pp21.command = cpu_to_le32(command);
	else
		tx_desc->pp22.command = cpu_to_le32(command);
}

static unsigned int mvpp2_txdesc_offset_get(struct mvpp2_port *port,
					    struct mvpp2_tx_desc *tx_desc)
{
	if (port->priv->hw_version == MVPP21)
		return tx_desc->pp21.packet_offset;
	else
		return tx_desc->pp22.packet_offset;
}

static dma_addr_t mvpp2_rxdesc_dma_addr_get(struct mvpp2_port *port,
					    struct mvpp2_rx_desc *rx_desc)
{
	if (port->priv->hw_version == MVPP21)
		return le32_to_cpu(rx_desc->pp21.buf_dma_addr);
	else
		return le64_to_cpu(rx_desc->pp22.buf_dma_addr_key_hash) &
		       MVPP2_DESC_DMA_MASK;
}

static unsigned long mvpp2_rxdesc_cookie_get(struct mvpp2_port *port,
					     struct mvpp2_rx_desc *rx_desc)
{
	if (port->priv->hw_version == MVPP21)
		return le32_to_cpu(rx_desc->pp21.buf_cookie);
	else
		return le64_to_cpu(rx_desc->pp22.buf_cookie_misc) &
		       MVPP2_DESC_DMA_MASK;
}

static size_t mvpp2_rxdesc_size_get(struct mvpp2_port *port,
				    struct mvpp2_rx_desc *rx_desc)
{
	if (port->priv->hw_version == MVPP21)
		return le16_to_cpu(rx_desc->pp21.data_size);
	else
		return le16_to_cpu(rx_desc->pp22.data_size);
}

static u32 mvpp2_rxdesc_status_get(struct mvpp2_port *port,
				   struct mvpp2_rx_desc *rx_desc)
{
	if (port->priv->hw_version == MVPP21)
		return le32_to_cpu(rx_desc->pp21.status);
	else
		return le32_to_cpu(rx_desc->pp22.status);
}

static void mvpp2_txq_inc_get(struct mvpp2_txq_pcpu *txq_pcpu)
{
	txq_pcpu->txq_get_index++;
	if (txq_pcpu->txq_get_index == txq_pcpu->size)
		txq_pcpu->txq_get_index = 0;
}

static void mvpp2_txq_inc_put(struct mvpp2_port *port,
			      struct mvpp2_txq_pcpu *txq_pcpu,
			      struct sk_buff *skb,
			      struct mvpp2_tx_desc *tx_desc)
{
	struct mvpp2_txq_pcpu_buf *tx_buf =
		txq_pcpu->buffs + txq_pcpu->txq_put_index;
	tx_buf->skb = skb;
	tx_buf->size = mvpp2_txdesc_size_get(port, tx_desc);
	tx_buf->dma = mvpp2_txdesc_dma_addr_get(port, tx_desc) +
		mvpp2_txdesc_offset_get(port, tx_desc);
	txq_pcpu->txq_put_index++;
	if (txq_pcpu->txq_put_index == txq_pcpu->size)
		txq_pcpu->txq_put_index = 0;
}

/* Get number of physical egress port */
static inline int mvpp2_egress_port(struct mvpp2_port *port)
{
	return MVPP2_MAX_TCONT + port->id;
}

/* Get number of physical TXQ */
static inline int mvpp2_txq_phys(int port, int txq)
{
	return (MVPP2_MAX_TCONT + port) * MVPP2_MAX_TXQ + txq;
}

static void *mvpp2_frag_alloc(const struct mvpp2_bm_pool *pool)
{
	if (likely(pool->frag_size <= PAGE_SIZE))
		return netdev_alloc_frag(pool->frag_size);
	else
		return kmalloc(pool->frag_size, GFP_ATOMIC);
}

static void mvpp2_frag_free(const struct mvpp2_bm_pool *pool, void *data)
{
	if (likely(pool->frag_size <= PAGE_SIZE))
		skb_free_frag(data);
	else
		kfree(data);
}

/* Buffer Manager configuration routines */

/* Create pool */
static int mvpp2_bm_pool_create(struct platform_device *pdev,
				struct mvpp2 *priv,
				struct mvpp2_bm_pool *bm_pool, int size)
{
	u32 val;

	/* Number of buffer pointers must be a multiple of 16, as per
	 * hardware constraints
	 */
	if (!IS_ALIGNED(size, 16))
		return -EINVAL;

	/* PPv2.1 needs 8 bytes per buffer pointer, PPv2.2 needs 16
	 * bytes per buffer pointer
	 */
	if (priv->hw_version == MVPP21)
		bm_pool->size_bytes = 2 * sizeof(u32) * size;
	else
		bm_pool->size_bytes = 2 * sizeof(u64) * size;

	bm_pool->virt_addr = dma_alloc_coherent(&pdev->dev, bm_pool->size_bytes,
						&bm_pool->dma_addr,
						GFP_KERNEL);
	if (!bm_pool->virt_addr)
		return -ENOMEM;

	if (!IS_ALIGNED((unsigned long)bm_pool->virt_addr,
			MVPP2_BM_POOL_PTR_ALIGN)) {
		dma_free_coherent(&pdev->dev, bm_pool->size_bytes,
				  bm_pool->virt_addr, bm_pool->dma_addr);
		dev_err(&pdev->dev, "BM pool %d is not %d bytes aligned\n",
			bm_pool->id, MVPP2_BM_POOL_PTR_ALIGN);
		return -ENOMEM;
	}

	mvpp2_write(priv, MVPP2_BM_POOL_BASE_REG(bm_pool->id),
		    lower_32_bits(bm_pool->dma_addr));
	mvpp2_write(priv, MVPP2_BM_POOL_SIZE_REG(bm_pool->id), size);

	val = mvpp2_read(priv, MVPP2_BM_POOL_CTRL_REG(bm_pool->id));
	val |= MVPP2_BM_START_MASK;
	mvpp2_write(priv, MVPP2_BM_POOL_CTRL_REG(bm_pool->id), val);

	bm_pool->size = size;
	bm_pool->pkt_size = 0;
	bm_pool->buf_num = 0;

	return 0;
}

/* Set pool buffer size */
static void mvpp2_bm_pool_bufsize_set(struct mvpp2 *priv,
				      struct mvpp2_bm_pool *bm_pool,
				      int buf_size)
{
	u32 val;

	bm_pool->buf_size = buf_size;

	val = ALIGN(buf_size, 1 << MVPP2_POOL_BUF_SIZE_OFFSET);
	mvpp2_write(priv, MVPP2_POOL_BUF_SIZE_REG(bm_pool->id), val);
}

static void mvpp2_bm_bufs_get_addrs(struct device *dev, struct mvpp2 *priv,
				    struct mvpp2_bm_pool *bm_pool,
				    dma_addr_t *dma_addr,
				    phys_addr_t *phys_addr)
{
	int cpu = get_cpu();

	*dma_addr = mvpp2_percpu_read(priv, cpu,
				      MVPP2_BM_PHY_ALLOC_REG(bm_pool->id));
	*phys_addr = mvpp2_percpu_read(priv, cpu, MVPP2_BM_VIRT_ALLOC_REG);

	if (priv->hw_version == MVPP22) {
		u32 val;
		u32 dma_addr_highbits, phys_addr_highbits;

		val = mvpp2_percpu_read(priv, cpu, MVPP22_BM_ADDR_HIGH_ALLOC);
		dma_addr_highbits = (val & MVPP22_BM_ADDR_HIGH_PHYS_MASK);
		phys_addr_highbits = (val & MVPP22_BM_ADDR_HIGH_VIRT_MASK) >>
			MVPP22_BM_ADDR_HIGH_VIRT_SHIFT;

		if (sizeof(dma_addr_t) == 8)
			*dma_addr |= (u64)dma_addr_highbits << 32;

		if (sizeof(phys_addr_t) == 8)
			*phys_addr |= (u64)phys_addr_highbits << 32;
	}

	put_cpu();
}

/* Free all buffers from the pool */
static void mvpp2_bm_bufs_free(struct device *dev, struct mvpp2 *priv,
			       struct mvpp2_bm_pool *bm_pool, int buf_num)
{
	int i;

	if (buf_num > bm_pool->buf_num) {
		WARN(1, "Pool does not have so many bufs pool(%d) bufs(%d)\n",
		     bm_pool->id, buf_num);
		buf_num = bm_pool->buf_num;
	}

	for (i = 0; i < buf_num; i++) {
		dma_addr_t buf_dma_addr;
		phys_addr_t buf_phys_addr;
		void *data;

		mvpp2_bm_bufs_get_addrs(dev, priv, bm_pool,
					&buf_dma_addr, &buf_phys_addr);

		dma_unmap_single(dev, buf_dma_addr,
				 bm_pool->buf_size, DMA_FROM_DEVICE);

		data = (void *)phys_to_virt(buf_phys_addr);
		if (!data)
			break;

		mvpp2_frag_free(bm_pool, data);
	}

	/* Update BM driver with number of buffers removed from pool */
	bm_pool->buf_num -= i;
}

/* Check number of buffers in BM pool */
static int mvpp2_check_hw_buf_num(struct mvpp2 *priv, struct mvpp2_bm_pool *bm_pool)
{
	int buf_num = 0;

	buf_num += mvpp2_read(priv, MVPP2_BM_POOL_PTRS_NUM_REG(bm_pool->id)) &
				    MVPP22_BM_POOL_PTRS_NUM_MASK;
	buf_num += mvpp2_read(priv, MVPP2_BM_BPPI_PTRS_NUM_REG(bm_pool->id)) &
				    MVPP2_BM_BPPI_PTR_NUM_MASK;

	/* HW has one buffer ready which is not reflected in the counters */
	if (buf_num)
		buf_num += 1;

	return buf_num;
}

/* Cleanup pool */
static int mvpp2_bm_pool_destroy(struct platform_device *pdev,
				 struct mvpp2 *priv,
				 struct mvpp2_bm_pool *bm_pool)
{
	int buf_num;
	u32 val;

	buf_num = mvpp2_check_hw_buf_num(priv, bm_pool);
	mvpp2_bm_bufs_free(&pdev->dev, priv, bm_pool, buf_num);

	/* Check buffer counters after free */
	buf_num = mvpp2_check_hw_buf_num(priv, bm_pool);
	if (buf_num) {
		WARN(1, "cannot free all buffers in pool %d, buf_num left %d\n",
		     bm_pool->id, bm_pool->buf_num);
		return 0;
	}

	val = mvpp2_read(priv, MVPP2_BM_POOL_CTRL_REG(bm_pool->id));
	val |= MVPP2_BM_STOP_MASK;
	mvpp2_write(priv, MVPP2_BM_POOL_CTRL_REG(bm_pool->id), val);

	dma_free_coherent(&pdev->dev, bm_pool->size_bytes,
			  bm_pool->virt_addr,
			  bm_pool->dma_addr);
	return 0;
}

static int mvpp2_bm_pools_init(struct platform_device *pdev,
			       struct mvpp2 *priv)
{
	int i, err, size;
	struct mvpp2_bm_pool *bm_pool;

	/* Create all pools with maximum size */
	size = MVPP2_BM_POOL_SIZE_MAX;
	for (i = 0; i < MVPP2_BM_POOLS_NUM; i++) {
		bm_pool = &priv->bm_pools[i];
		bm_pool->id = i;
		err = mvpp2_bm_pool_create(pdev, priv, bm_pool, size);
		if (err)
			goto err_unroll_pools;
		mvpp2_bm_pool_bufsize_set(priv, bm_pool, 0);
	}
	return 0;

err_unroll_pools:
	dev_err(&pdev->dev, "failed to create BM pool %d, size %d\n", i, size);
	for (i = i - 1; i >= 0; i--)
		mvpp2_bm_pool_destroy(pdev, priv, &priv->bm_pools[i]);
	return err;
}

static int mvpp2_bm_init(struct platform_device *pdev, struct mvpp2 *priv)
{
	int i, err;

	for (i = 0; i < MVPP2_BM_POOLS_NUM; i++) {
		/* Mask BM all interrupts */
		mvpp2_write(priv, MVPP2_BM_INTR_MASK_REG(i), 0);
		/* Clear BM cause register */
		mvpp2_write(priv, MVPP2_BM_INTR_CAUSE_REG(i), 0);
	}

	/* Allocate and initialize BM pools */
	priv->bm_pools = devm_kcalloc(&pdev->dev, MVPP2_BM_POOLS_NUM,
				      sizeof(*priv->bm_pools), GFP_KERNEL);
	if (!priv->bm_pools)
		return -ENOMEM;

	err = mvpp2_bm_pools_init(pdev, priv);
	if (err < 0)
		return err;
	return 0;
}

static void mvpp2_setup_bm_pool(void)
{
	/* Short pool */
	mvpp2_pools[MVPP2_BM_SHORT].buf_num  = MVPP2_BM_SHORT_BUF_NUM;
	mvpp2_pools[MVPP2_BM_SHORT].pkt_size = MVPP2_BM_SHORT_PKT_SIZE;

	/* Long pool */
	mvpp2_pools[MVPP2_BM_LONG].buf_num  = MVPP2_BM_LONG_BUF_NUM;
	mvpp2_pools[MVPP2_BM_LONG].pkt_size = MVPP2_BM_LONG_PKT_SIZE;

	/* Jumbo pool */
	mvpp2_pools[MVPP2_BM_JUMBO].buf_num  = MVPP2_BM_JUMBO_BUF_NUM;
	mvpp2_pools[MVPP2_BM_JUMBO].pkt_size = MVPP2_BM_JUMBO_PKT_SIZE;
}

/* Attach long pool to rxq */
static void mvpp2_rxq_long_pool_set(struct mvpp2_port *port,
				    int lrxq, int long_pool)
{
	u32 val, mask;
	int prxq;

	/* Get queue physical ID */
	prxq = port->rxqs[lrxq]->id;

	if (port->priv->hw_version == MVPP21)
		mask = MVPP21_RXQ_POOL_LONG_MASK;
	else
		mask = MVPP22_RXQ_POOL_LONG_MASK;

	val = mvpp2_read(port->priv, MVPP2_RXQ_CONFIG_REG(prxq));
	val &= ~mask;
	val |= (long_pool << MVPP2_RXQ_POOL_LONG_OFFS) & mask;
	mvpp2_write(port->priv, MVPP2_RXQ_CONFIG_REG(prxq), val);
}

/* Attach short pool to rxq */
static void mvpp2_rxq_short_pool_set(struct mvpp2_port *port,
				     int lrxq, int short_pool)
{
	u32 val, mask;
	int prxq;

	/* Get queue physical ID */
	prxq = port->rxqs[lrxq]->id;

	if (port->priv->hw_version == MVPP21)
		mask = MVPP21_RXQ_POOL_SHORT_MASK;
	else
		mask = MVPP22_RXQ_POOL_SHORT_MASK;

	val = mvpp2_read(port->priv, MVPP2_RXQ_CONFIG_REG(prxq));
	val &= ~mask;
	val |= (short_pool << MVPP2_RXQ_POOL_SHORT_OFFS) & mask;
	mvpp2_write(port->priv, MVPP2_RXQ_CONFIG_REG(prxq), val);
}

static void *mvpp2_buf_alloc(struct mvpp2_port *port,
			     struct mvpp2_bm_pool *bm_pool,
			     dma_addr_t *buf_dma_addr,
			     phys_addr_t *buf_phys_addr,
			     gfp_t gfp_mask)
{
	dma_addr_t dma_addr;
	void *data;

	data = mvpp2_frag_alloc(bm_pool);
	if (!data)
		return NULL;

	dma_addr = dma_map_single(port->dev->dev.parent, data,
				  MVPP2_RX_BUF_SIZE(bm_pool->pkt_size),
				  DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(port->dev->dev.parent, dma_addr))) {
		mvpp2_frag_free(bm_pool, data);
		return NULL;
	}
	*buf_dma_addr = dma_addr;
	*buf_phys_addr = virt_to_phys(data);

	return data;
}

/* Release buffer to BM */
static inline void mvpp2_bm_pool_put(struct mvpp2_port *port, int pool,
				     dma_addr_t buf_dma_addr,
				     phys_addr_t buf_phys_addr)
{
	int cpu = get_cpu();

	if (port->priv->hw_version == MVPP22) {
		u32 val = 0;

		if (sizeof(dma_addr_t) == 8)
			val |= upper_32_bits(buf_dma_addr) &
				MVPP22_BM_ADDR_HIGH_PHYS_RLS_MASK;

		if (sizeof(phys_addr_t) == 8)
			val |= (upper_32_bits(buf_phys_addr)
				<< MVPP22_BM_ADDR_HIGH_VIRT_RLS_SHIFT) &
				MVPP22_BM_ADDR_HIGH_VIRT_RLS_MASK;

		mvpp2_percpu_write_relaxed(port->priv, cpu,
					   MVPP22_BM_ADDR_HIGH_RLS_REG, val);
	}

	/* MVPP2_BM_VIRT_RLS_REG is not interpreted by HW, and simply
	 * returned in the "cookie" field of the RX
	 * descriptor. Instead of storing the virtual address, we
	 * store the physical address
	 */
	mvpp2_percpu_write_relaxed(port->priv, cpu,
				   MVPP2_BM_VIRT_RLS_REG, buf_phys_addr);
	mvpp2_percpu_write_relaxed(port->priv, cpu,
				   MVPP2_BM_PHY_RLS_REG(pool), buf_dma_addr);

	put_cpu();
}

/* Allocate buffers for the pool */
static int mvpp2_bm_bufs_add(struct mvpp2_port *port,
			     struct mvpp2_bm_pool *bm_pool, int buf_num)
{
	int i, buf_size, total_size;
	dma_addr_t dma_addr;
	phys_addr_t phys_addr;
	void *buf;

	buf_size = MVPP2_RX_BUF_SIZE(bm_pool->pkt_size);
	total_size = MVPP2_RX_TOTAL_SIZE(buf_size);

	if (buf_num < 0 ||
	    (buf_num + bm_pool->buf_num > bm_pool->size)) {
		netdev_err(port->dev,
			   "cannot allocate %d buffers for pool %d\n",
			   buf_num, bm_pool->id);
		return 0;
	}

	for (i = 0; i < buf_num; i++) {
		buf = mvpp2_buf_alloc(port, bm_pool, &dma_addr,
				      &phys_addr, GFP_KERNEL);
		if (!buf)
			break;

		mvpp2_bm_pool_put(port, bm_pool->id, dma_addr,
				  phys_addr);
	}

	/* Update BM driver with number of buffers added to pool */
	bm_pool->buf_num += i;

	netdev_dbg(port->dev,
		   "pool %d: pkt_size=%4d, buf_size=%4d, total_size=%4d\n",
		   bm_pool->id, bm_pool->pkt_size, buf_size, total_size);

	netdev_dbg(port->dev,
		   "pool %d: %d of %d buffers added\n",
		   bm_pool->id, i, buf_num);
	return i;
}

/* Notify the driver that BM pool is being used as specific type and return the
 * pool pointer on success
 */
static struct mvpp2_bm_pool *
mvpp2_bm_pool_use(struct mvpp2_port *port, unsigned pool, int pkt_size)
{
	struct mvpp2_bm_pool *new_pool = &port->priv->bm_pools[pool];
	int num;

	if (pool >= MVPP2_BM_POOLS_NUM) {
		netdev_err(port->dev, "Invalid pool %d\n", pool);
		return NULL;
	}

	/* Allocate buffers in case BM pool is used as long pool, but packet
	 * size doesn't match MTU or BM pool hasn't being used yet
	 */
	if (new_pool->pkt_size == 0) {
		int pkts_num;

		/* Set default buffer number or free all the buffers in case
		 * the pool is not empty
		 */
		pkts_num = new_pool->buf_num;
		if (pkts_num == 0)
			pkts_num = mvpp2_pools[pool].buf_num;
		else
			mvpp2_bm_bufs_free(port->dev->dev.parent,
					   port->priv, new_pool, pkts_num);

		new_pool->pkt_size = pkt_size;
		new_pool->frag_size =
			SKB_DATA_ALIGN(MVPP2_RX_BUF_SIZE(pkt_size)) +
			MVPP2_SKB_SHINFO_SIZE;

		/* Allocate buffers for this pool */
		num = mvpp2_bm_bufs_add(port, new_pool, pkts_num);
		if (num != pkts_num) {
			WARN(1, "pool %d: %d of %d allocated\n",
			     new_pool->id, num, pkts_num);
			return NULL;
		}
	}

	mvpp2_bm_pool_bufsize_set(port->priv, new_pool,
				  MVPP2_RX_BUF_SIZE(new_pool->pkt_size));

	return new_pool;
}

/* Initialize pools for swf */
static int mvpp2_swf_bm_pool_init(struct mvpp2_port *port)
{
	int rxq;
	enum mvpp2_bm_pool_log_num long_log_pool, short_log_pool;

	/* If port pkt_size is higher than 1518B:
	 * HW Long pool - SW Jumbo pool, HW Short pool - SW Long pool
	 * else: HW Long pool - SW Long pool, HW Short pool - SW Short pool
	 */
	if (port->pkt_size > MVPP2_BM_LONG_PKT_SIZE) {
		long_log_pool = MVPP2_BM_JUMBO;
		short_log_pool = MVPP2_BM_LONG;
	} else {
		long_log_pool = MVPP2_BM_LONG;
		short_log_pool = MVPP2_BM_SHORT;
	}

	if (!port->pool_long) {
		port->pool_long =
			mvpp2_bm_pool_use(port, long_log_pool,
					  mvpp2_pools[long_log_pool].pkt_size);
		if (!port->pool_long)
			return -ENOMEM;

		port->pool_long->port_map |= BIT(port->id);

		for (rxq = 0; rxq < port->nrxqs; rxq++)
			mvpp2_rxq_long_pool_set(port, rxq, port->pool_long->id);
	}

	if (!port->pool_short) {
		port->pool_short =
			mvpp2_bm_pool_use(port, short_log_pool,
					  mvpp2_pools[short_log_pool].pkt_size);
		if (!port->pool_short)
			return -ENOMEM;

		port->pool_short->port_map |= BIT(port->id);

		for (rxq = 0; rxq < port->nrxqs; rxq++)
			mvpp2_rxq_short_pool_set(port, rxq,
						 port->pool_short->id);
	}

	return 0;
}

static int mvpp2_bm_update_mtu(struct net_device *dev, int mtu)
{
	struct mvpp2_port *port = netdev_priv(dev);
	enum mvpp2_bm_pool_log_num new_long_pool;
	int pkt_size = MVPP2_RX_PKT_SIZE(mtu);

	/* If port MTU is higher than 1518B:
	 * HW Long pool - SW Jumbo pool, HW Short pool - SW Long pool
	 * else: HW Long pool - SW Long pool, HW Short pool - SW Short pool
	 */
	if (pkt_size > MVPP2_BM_LONG_PKT_SIZE)
		new_long_pool = MVPP2_BM_JUMBO;
	else
		new_long_pool = MVPP2_BM_LONG;

	if (new_long_pool != port->pool_long->id) {
		/* Remove port from old short & long pool */
		port->pool_long = mvpp2_bm_pool_use(port, port->pool_long->id,
						    port->pool_long->pkt_size);
		port->pool_long->port_map &= ~BIT(port->id);
		port->pool_long = NULL;

		port->pool_short = mvpp2_bm_pool_use(port, port->pool_short->id,
						     port->pool_short->pkt_size);
		port->pool_short->port_map &= ~BIT(port->id);
		port->pool_short = NULL;

		port->pkt_size =  pkt_size;

		/* Add port to new short & long pool */
		mvpp2_swf_bm_pool_init(port);

		/* Update L4 checksum when jumbo enable/disable on port */
		if (new_long_pool == MVPP2_BM_JUMBO && port->id != 0) {
			dev->features &= ~(NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM);
			dev->hw_features &= ~(NETIF_F_IP_CSUM |
					      NETIF_F_IPV6_CSUM);
		} else {
			dev->features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
			dev->hw_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
		}
	}

	dev->mtu = mtu;
	dev->wanted_features = dev->features;

	netdev_update_features(dev);
	return 0;
}

static inline void mvpp2_interrupts_enable(struct mvpp2_port *port)
{
	int i, sw_thread_mask = 0;

	for (i = 0; i < port->nqvecs; i++)
		sw_thread_mask |= port->qvecs[i].sw_thread_mask;

	mvpp2_write(port->priv, MVPP2_ISR_ENABLE_REG(port->id),
		    MVPP2_ISR_ENABLE_INTERRUPT(sw_thread_mask));
}

static inline void mvpp2_interrupts_disable(struct mvpp2_port *port)
{
	int i, sw_thread_mask = 0;

	for (i = 0; i < port->nqvecs; i++)
		sw_thread_mask |= port->qvecs[i].sw_thread_mask;

	mvpp2_write(port->priv, MVPP2_ISR_ENABLE_REG(port->id),
		    MVPP2_ISR_DISABLE_INTERRUPT(sw_thread_mask));
}

static inline void mvpp2_qvec_interrupt_enable(struct mvpp2_queue_vector *qvec)
{
	struct mvpp2_port *port = qvec->port;

	mvpp2_write(port->priv, MVPP2_ISR_ENABLE_REG(port->id),
		    MVPP2_ISR_ENABLE_INTERRUPT(qvec->sw_thread_mask));
}

static inline void mvpp2_qvec_interrupt_disable(struct mvpp2_queue_vector *qvec)
{
	struct mvpp2_port *port = qvec->port;

	mvpp2_write(port->priv, MVPP2_ISR_ENABLE_REG(port->id),
		    MVPP2_ISR_DISABLE_INTERRUPT(qvec->sw_thread_mask));
}

/* Mask the current CPU's Rx/Tx interrupts
 * Called by on_each_cpu(), guaranteed to run with migration disabled,
 * using smp_processor_id() is OK.
 */
static void mvpp2_interrupts_mask(void *arg)
{
	struct mvpp2_port *port = arg;

	mvpp2_percpu_write(port->priv, smp_processor_id(),
			   MVPP2_ISR_RX_TX_MASK_REG(port->id), 0);
}

/* Unmask the current CPU's Rx/Tx interrupts.
 * Called by on_each_cpu(), guaranteed to run with migration disabled,
 * using smp_processor_id() is OK.
 */
static void mvpp2_interrupts_unmask(void *arg)
{
	struct mvpp2_port *port = arg;
	u32 val;

	val = MVPP2_CAUSE_MISC_SUM_MASK |
		MVPP2_CAUSE_RXQ_OCCUP_DESC_ALL_MASK;
	if (port->has_tx_irqs)
		val |= MVPP2_CAUSE_TXQ_OCCUP_DESC_ALL_MASK;

	mvpp2_percpu_write(port->priv, smp_processor_id(),
			   MVPP2_ISR_RX_TX_MASK_REG(port->id), val);
}

static void
mvpp2_shared_interrupt_mask_unmask(struct mvpp2_port *port, bool mask)
{
	u32 val;
	int i;

	if (port->priv->hw_version != MVPP22)
		return;

	if (mask)
		val = 0;
	else
		val = MVPP2_CAUSE_RXQ_OCCUP_DESC_ALL_MASK;

	for (i = 0; i < port->nqvecs; i++) {
		struct mvpp2_queue_vector *v = port->qvecs + i;

		if (v->type != MVPP2_QUEUE_VECTOR_SHARED)
			continue;

		mvpp2_percpu_write(port->priv, v->sw_thread_id,
				   MVPP2_ISR_RX_TX_MASK_REG(port->id), val);
	}
}

/* Port configuration routines */

static void mvpp22_gop_init_rgmii(struct mvpp2_port *port)
{
	struct mvpp2 *priv = port->priv;
	u32 val;

	regmap_read(priv->sysctrl_base, GENCONF_PORT_CTRL0, &val);
	val |= GENCONF_PORT_CTRL0_BUS_WIDTH_SELECT;
	regmap_write(priv->sysctrl_base, GENCONF_PORT_CTRL0, val);

	regmap_read(priv->sysctrl_base, GENCONF_CTRL0, &val);
	if (port->gop_id == 2)
		val |= GENCONF_CTRL0_PORT0_RGMII | GENCONF_CTRL0_PORT1_RGMII;
	else if (port->gop_id == 3)
		val |= GENCONF_CTRL0_PORT1_RGMII_MII;
	regmap_write(priv->sysctrl_base, GENCONF_CTRL0, val);
}

static void mvpp22_gop_init_sgmii(struct mvpp2_port *port)
{
	struct mvpp2 *priv = port->priv;
	u32 val;

	regmap_read(priv->sysctrl_base, GENCONF_PORT_CTRL0, &val);
	val |= GENCONF_PORT_CTRL0_BUS_WIDTH_SELECT |
	       GENCONF_PORT_CTRL0_RX_DATA_SAMPLE;
	regmap_write(priv->sysctrl_base, GENCONF_PORT_CTRL0, val);

	if (port->gop_id > 1) {
		regmap_read(priv->sysctrl_base, GENCONF_CTRL0, &val);
		if (port->gop_id == 2)
			val &= ~GENCONF_CTRL0_PORT0_RGMII;
		else if (port->gop_id == 3)
			val &= ~GENCONF_CTRL0_PORT1_RGMII_MII;
		regmap_write(priv->sysctrl_base, GENCONF_CTRL0, val);
	}
}

static void mvpp22_gop_init_10gkr(struct mvpp2_port *port)
{
	struct mvpp2 *priv = port->priv;
	void __iomem *mpcs = priv->iface_base + MVPP22_MPCS_BASE(port->gop_id);
	void __iomem *xpcs = priv->iface_base + MVPP22_XPCS_BASE(port->gop_id);
	u32 val;

	/* XPCS */
	val = readl(xpcs + MVPP22_XPCS_CFG0);
	val &= ~(MVPP22_XPCS_CFG0_PCS_MODE(0x3) |
		 MVPP22_XPCS_CFG0_ACTIVE_LANE(0x3));
	val |= MVPP22_XPCS_CFG0_ACTIVE_LANE(2);
	writel(val, xpcs + MVPP22_XPCS_CFG0);

	/* MPCS */
	val = readl(mpcs + MVPP22_MPCS_CTRL);
	val &= ~MVPP22_MPCS_CTRL_FWD_ERR_CONN;
	writel(val, mpcs + MVPP22_MPCS_CTRL);

	val = readl(mpcs + MVPP22_MPCS_CLK_RESET);
	val &= ~(MVPP22_MPCS_CLK_RESET_DIV_RATIO(0x7) | MAC_CLK_RESET_MAC |
		 MAC_CLK_RESET_SD_RX | MAC_CLK_RESET_SD_TX);
	val |= MVPP22_MPCS_CLK_RESET_DIV_RATIO(1);
	writel(val, mpcs + MVPP22_MPCS_CLK_RESET);

	val &= ~MVPP22_MPCS_CLK_RESET_DIV_SET;
	val |= MAC_CLK_RESET_MAC | MAC_CLK_RESET_SD_RX | MAC_CLK_RESET_SD_TX;
	writel(val, mpcs + MVPP22_MPCS_CLK_RESET);
}

static int mvpp22_gop_init(struct mvpp2_port *port)
{
	struct mvpp2 *priv = port->priv;
	u32 val;

	if (!priv->sysctrl_base)
		return 0;

	switch (port->phy_interface) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		if (port->gop_id == 0)
			goto invalid_conf;
		mvpp22_gop_init_rgmii(port);
		break;
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		mvpp22_gop_init_sgmii(port);
		break;
	case PHY_INTERFACE_MODE_10GKR:
		if (port->gop_id != 0)
			goto invalid_conf;
		mvpp22_gop_init_10gkr(port);
		break;
	default:
		goto unsupported_conf;
	}

	regmap_read(priv->sysctrl_base, GENCONF_PORT_CTRL1, &val);
	val |= GENCONF_PORT_CTRL1_RESET(port->gop_id) |
	       GENCONF_PORT_CTRL1_EN(port->gop_id);
	regmap_write(priv->sysctrl_base, GENCONF_PORT_CTRL1, val);

	regmap_read(priv->sysctrl_base, GENCONF_PORT_CTRL0, &val);
	val |= GENCONF_PORT_CTRL0_CLK_DIV_PHASE_CLR;
	regmap_write(priv->sysctrl_base, GENCONF_PORT_CTRL0, val);

	regmap_read(priv->sysctrl_base, GENCONF_SOFT_RESET1, &val);
	val |= GENCONF_SOFT_RESET1_GOP;
	regmap_write(priv->sysctrl_base, GENCONF_SOFT_RESET1, val);

unsupported_conf:
	return 0;

invalid_conf:
	netdev_err(port->dev, "Invalid port configuration\n");
	return -EINVAL;
}

static void mvpp22_gop_unmask_irq(struct mvpp2_port *port)
{
	u32 val;

	if (phy_interface_mode_is_rgmii(port->phy_interface) ||
	    port->phy_interface == PHY_INTERFACE_MODE_SGMII ||
	    port->phy_interface == PHY_INTERFACE_MODE_1000BASEX ||
	    port->phy_interface == PHY_INTERFACE_MODE_2500BASEX) {
		/* Enable the GMAC link status irq for this port */
		val = readl(port->base + MVPP22_GMAC_INT_SUM_MASK);
		val |= MVPP22_GMAC_INT_SUM_MASK_LINK_STAT;
		writel(val, port->base + MVPP22_GMAC_INT_SUM_MASK);
	}

	if (port->gop_id == 0) {
		/* Enable the XLG/GIG irqs for this port */
		val = readl(port->base + MVPP22_XLG_EXT_INT_MASK);
		if (port->phy_interface == PHY_INTERFACE_MODE_10GKR)
			val |= MVPP22_XLG_EXT_INT_MASK_XLG;
		else
			val |= MVPP22_XLG_EXT_INT_MASK_GIG;
		writel(val, port->base + MVPP22_XLG_EXT_INT_MASK);
	}
}

static void mvpp22_gop_mask_irq(struct mvpp2_port *port)
{
	u32 val;

	if (port->gop_id == 0) {
		val = readl(port->base + MVPP22_XLG_EXT_INT_MASK);
		val &= ~(MVPP22_XLG_EXT_INT_MASK_XLG |
			 MVPP22_XLG_EXT_INT_MASK_GIG);
		writel(val, port->base + MVPP22_XLG_EXT_INT_MASK);
	}

	if (phy_interface_mode_is_rgmii(port->phy_interface) ||
	    port->phy_interface == PHY_INTERFACE_MODE_SGMII ||
	    port->phy_interface == PHY_INTERFACE_MODE_1000BASEX ||
	    port->phy_interface == PHY_INTERFACE_MODE_2500BASEX) {
		val = readl(port->base + MVPP22_GMAC_INT_SUM_MASK);
		val &= ~MVPP22_GMAC_INT_SUM_MASK_LINK_STAT;
		writel(val, port->base + MVPP22_GMAC_INT_SUM_MASK);
	}
}

static void mvpp22_gop_setup_irq(struct mvpp2_port *port)
{
	u32 val;

	if (phy_interface_mode_is_rgmii(port->phy_interface) ||
	    port->phy_interface == PHY_INTERFACE_MODE_SGMII ||
	    port->phy_interface == PHY_INTERFACE_MODE_1000BASEX ||
	    port->phy_interface == PHY_INTERFACE_MODE_2500BASEX) {
		val = readl(port->base + MVPP22_GMAC_INT_MASK);
		val |= MVPP22_GMAC_INT_MASK_LINK_STAT;
		writel(val, port->base + MVPP22_GMAC_INT_MASK);
	}

	if (port->gop_id == 0) {
		val = readl(port->base + MVPP22_XLG_INT_MASK);
		val |= MVPP22_XLG_INT_MASK_LINK;
		writel(val, port->base + MVPP22_XLG_INT_MASK);
	}

	mvpp22_gop_unmask_irq(port);
}

/* Sets the PHY mode of the COMPHY (which configures the serdes lanes).
 *
 * The PHY mode used by the PPv2 driver comes from the network subsystem, while
 * the one given to the COMPHY comes from the generic PHY subsystem. Hence they
 * differ.
 *
 * The COMPHY configures the serdes lanes regardless of the actual use of the
 * lanes by the physical layer. This is why configurations like
 * "PPv2 (2500BaseX) - COMPHY (2500SGMII)" are valid.
 */
static int mvpp22_comphy_init(struct mvpp2_port *port)
{
	enum phy_mode mode;
	int ret;

	if (!port->comphy)
		return 0;

	switch (port->phy_interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		mode = PHY_MODE_SGMII;
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		mode = PHY_MODE_2500SGMII;
		break;
	case PHY_INTERFACE_MODE_10GKR:
		mode = PHY_MODE_10GKR;
		break;
	default:
		return -EINVAL;
	}

	ret = phy_set_mode(port->comphy, mode);
	if (ret)
		return ret;

	return phy_power_on(port->comphy);
}

static void mvpp2_port_enable(struct mvpp2_port *port)
{
	u32 val;

	/* Only GOP port 0 has an XLG MAC */
	if (port->gop_id == 0 &&
	    (port->phy_interface == PHY_INTERFACE_MODE_XAUI ||
	     port->phy_interface == PHY_INTERFACE_MODE_10GKR)) {
		val = readl(port->base + MVPP22_XLG_CTRL0_REG);
		val |= MVPP22_XLG_CTRL0_PORT_EN |
		       MVPP22_XLG_CTRL0_MAC_RESET_DIS;
		val &= ~MVPP22_XLG_CTRL0_MIB_CNT_DIS;
		writel(val, port->base + MVPP22_XLG_CTRL0_REG);
	} else {
		val = readl(port->base + MVPP2_GMAC_CTRL_0_REG);
		val |= MVPP2_GMAC_PORT_EN_MASK;
		val |= MVPP2_GMAC_MIB_CNTR_EN_MASK;
		writel(val, port->base + MVPP2_GMAC_CTRL_0_REG);
	}
}

static void mvpp2_port_disable(struct mvpp2_port *port)
{
	u32 val;

	/* Only GOP port 0 has an XLG MAC */
	if (port->gop_id == 0 &&
	    (port->phy_interface == PHY_INTERFACE_MODE_XAUI ||
	     port->phy_interface == PHY_INTERFACE_MODE_10GKR)) {
		val = readl(port->base + MVPP22_XLG_CTRL0_REG);
		val &= ~MVPP22_XLG_CTRL0_PORT_EN;
		writel(val, port->base + MVPP22_XLG_CTRL0_REG);

		/* Disable & reset should be done separately */
		val &= ~MVPP22_XLG_CTRL0_MAC_RESET_DIS;
		writel(val, port->base + MVPP22_XLG_CTRL0_REG);
	} else {
		val = readl(port->base + MVPP2_GMAC_CTRL_0_REG);
		val &= ~(MVPP2_GMAC_PORT_EN_MASK);
		writel(val, port->base + MVPP2_GMAC_CTRL_0_REG);
	}
}

/* Set IEEE 802.3x Flow Control Xon Packet Transmission Mode */
static void mvpp2_port_periodic_xon_disable(struct mvpp2_port *port)
{
	u32 val;

	val = readl(port->base + MVPP2_GMAC_CTRL_1_REG) &
		    ~MVPP2_GMAC_PERIODIC_XON_EN_MASK;
	writel(val, port->base + MVPP2_GMAC_CTRL_1_REG);
}

/* Configure loopback port */
static void mvpp2_port_loopback_set(struct mvpp2_port *port,
				    const struct phylink_link_state *state)
{
	u32 val;

	val = readl(port->base + MVPP2_GMAC_CTRL_1_REG);

	if (state->speed == 1000)
		val |= MVPP2_GMAC_GMII_LB_EN_MASK;
	else
		val &= ~MVPP2_GMAC_GMII_LB_EN_MASK;

	if (port->phy_interface == PHY_INTERFACE_MODE_SGMII ||
	    port->phy_interface == PHY_INTERFACE_MODE_1000BASEX ||
	    port->phy_interface == PHY_INTERFACE_MODE_2500BASEX)
		val |= MVPP2_GMAC_PCS_LB_EN_MASK;
	else
		val &= ~MVPP2_GMAC_PCS_LB_EN_MASK;

	writel(val, port->base + MVPP2_GMAC_CTRL_1_REG);
}

struct mvpp2_ethtool_counter {
	unsigned int offset;
	const char string[ETH_GSTRING_LEN];
	bool reg_is_64b;
};

static u64 mvpp2_read_count(struct mvpp2_port *port,
			    const struct mvpp2_ethtool_counter *counter)
{
	u64 val;

	val = readl(port->stats_base + counter->offset);
	if (counter->reg_is_64b)
		val += (u64)readl(port->stats_base + counter->offset + 4) << 32;

	return val;
}

/* Due to the fact that software statistics and hardware statistics are, by
 * design, incremented at different moments in the chain of packet processing,
 * it is very likely that incoming packets could have been dropped after being
 * counted by hardware but before reaching software statistics (most probably
 * multicast packets), and in the oppposite way, during transmission, FCS bytes
 * are added in between as well as TSO skb will be split and header bytes added.
 * Hence, statistics gathered from userspace with ifconfig (software) and
 * ethtool (hardware) cannot be compared.
 */
static const struct mvpp2_ethtool_counter mvpp2_ethtool_regs[] = {
	{ MVPP2_MIB_GOOD_OCTETS_RCVD, "good_octets_received", true },
	{ MVPP2_MIB_BAD_OCTETS_RCVD, "bad_octets_received" },
	{ MVPP2_MIB_CRC_ERRORS_SENT, "crc_errors_sent" },
	{ MVPP2_MIB_UNICAST_FRAMES_RCVD, "unicast_frames_received" },
	{ MVPP2_MIB_BROADCAST_FRAMES_RCVD, "broadcast_frames_received" },
	{ MVPP2_MIB_MULTICAST_FRAMES_RCVD, "multicast_frames_received" },
	{ MVPP2_MIB_FRAMES_64_OCTETS, "frames_64_octets" },
	{ MVPP2_MIB_FRAMES_65_TO_127_OCTETS, "frames_65_to_127_octet" },
	{ MVPP2_MIB_FRAMES_128_TO_255_OCTETS, "frames_128_to_255_octet" },
	{ MVPP2_MIB_FRAMES_256_TO_511_OCTETS, "frames_256_to_511_octet" },
	{ MVPP2_MIB_FRAMES_512_TO_1023_OCTETS, "frames_512_to_1023_octet" },
	{ MVPP2_MIB_FRAMES_1024_TO_MAX_OCTETS, "frames_1024_to_max_octet" },
	{ MVPP2_MIB_GOOD_OCTETS_SENT, "good_octets_sent", true },
	{ MVPP2_MIB_UNICAST_FRAMES_SENT, "unicast_frames_sent" },
	{ MVPP2_MIB_MULTICAST_FRAMES_SENT, "multicast_frames_sent" },
	{ MVPP2_MIB_BROADCAST_FRAMES_SENT, "broadcast_frames_sent" },
	{ MVPP2_MIB_FC_SENT, "fc_sent" },
	{ MVPP2_MIB_FC_RCVD, "fc_received" },
	{ MVPP2_MIB_RX_FIFO_OVERRUN, "rx_fifo_overrun" },
	{ MVPP2_MIB_UNDERSIZE_RCVD, "undersize_received" },
	{ MVPP2_MIB_FRAGMENTS_RCVD, "fragments_received" },
	{ MVPP2_MIB_OVERSIZE_RCVD, "oversize_received" },
	{ MVPP2_MIB_JABBER_RCVD, "jabber_received" },
	{ MVPP2_MIB_MAC_RCV_ERROR, "mac_receive_error" },
	{ MVPP2_MIB_BAD_CRC_EVENT, "bad_crc_event" },
	{ MVPP2_MIB_COLLISION, "collision" },
	{ MVPP2_MIB_LATE_COLLISION, "late_collision" },
};

static void mvpp2_ethtool_get_strings(struct net_device *netdev, u32 sset,
				      u8 *data)
{
	if (sset == ETH_SS_STATS) {
		int i;

		for (i = 0; i < ARRAY_SIZE(mvpp2_ethtool_regs); i++)
			memcpy(data + i * ETH_GSTRING_LEN,
			       &mvpp2_ethtool_regs[i].string, ETH_GSTRING_LEN);
	}
}

static void mvpp2_gather_hw_statistics(struct work_struct *work)
{
	struct delayed_work *del_work = to_delayed_work(work);
	struct mvpp2_port *port = container_of(del_work, struct mvpp2_port,
					       stats_work);
	u64 *pstats;
	int i;

	mutex_lock(&port->gather_stats_lock);

	pstats = port->ethtool_stats;
	for (i = 0; i < ARRAY_SIZE(mvpp2_ethtool_regs); i++)
		*pstats++ += mvpp2_read_count(port, &mvpp2_ethtool_regs[i]);

	/* No need to read again the counters right after this function if it
	 * was called asynchronously by the user (ie. use of ethtool).
	 */
	cancel_delayed_work(&port->stats_work);
	queue_delayed_work(port->priv->stats_queue, &port->stats_work,
			   MVPP2_MIB_COUNTERS_STATS_DELAY);

	mutex_unlock(&port->gather_stats_lock);
}

static void mvpp2_ethtool_get_stats(struct net_device *dev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct mvpp2_port *port = netdev_priv(dev);

	/* Update statistics for the given port, then take the lock to avoid
	 * concurrent accesses on the ethtool_stats structure during its copy.
	 */
	mvpp2_gather_hw_statistics(&port->stats_work.work);

	mutex_lock(&port->gather_stats_lock);
	memcpy(data, port->ethtool_stats,
	       sizeof(u64) * ARRAY_SIZE(mvpp2_ethtool_regs));
	mutex_unlock(&port->gather_stats_lock);
}

static int mvpp2_ethtool_get_sset_count(struct net_device *dev, int sset)
{
	if (sset == ETH_SS_STATS)
		return ARRAY_SIZE(mvpp2_ethtool_regs);

	return -EOPNOTSUPP;
}

static void mvpp2_port_reset(struct mvpp2_port *port)
{
	u32 val;
	unsigned int i;

	/* Read the GOP statistics to reset the hardware counters */
	for (i = 0; i < ARRAY_SIZE(mvpp2_ethtool_regs); i++)
		mvpp2_read_count(port, &mvpp2_ethtool_regs[i]);

	val = readl(port->base + MVPP2_GMAC_CTRL_2_REG) &
		    ~MVPP2_GMAC_PORT_RESET_MASK;
	writel(val, port->base + MVPP2_GMAC_CTRL_2_REG);

	while (readl(port->base + MVPP2_GMAC_CTRL_2_REG) &
	       MVPP2_GMAC_PORT_RESET_MASK)
		continue;
}

/* Change maximum receive size of the port */
static inline void mvpp2_gmac_max_rx_size_set(struct mvpp2_port *port)
{
	u32 val;

	val = readl(port->base + MVPP2_GMAC_CTRL_0_REG);
	val &= ~MVPP2_GMAC_MAX_RX_SIZE_MASK;
	val |= (((port->pkt_size - MVPP2_MH_SIZE) / 2) <<
		    MVPP2_GMAC_MAX_RX_SIZE_OFFS);
	writel(val, port->base + MVPP2_GMAC_CTRL_0_REG);
}

/* Change maximum receive size of the port */
static inline void mvpp2_xlg_max_rx_size_set(struct mvpp2_port *port)
{
	u32 val;

	val =  readl(port->base + MVPP22_XLG_CTRL1_REG);
	val &= ~MVPP22_XLG_CTRL1_FRAMESIZELIMIT_MASK;
	val |= ((port->pkt_size - MVPP2_MH_SIZE) / 2) <<
	       MVPP22_XLG_CTRL1_FRAMESIZELIMIT_OFFS;
	writel(val, port->base + MVPP22_XLG_CTRL1_REG);
}

/* Set defaults to the MVPP2 port */
static void mvpp2_defaults_set(struct mvpp2_port *port)
{
	int tx_port_num, val, queue, ptxq, lrxq;

	if (port->priv->hw_version == MVPP21) {
		/* Update TX FIFO MIN Threshold */
		val = readl(port->base + MVPP2_GMAC_PORT_FIFO_CFG_1_REG);
		val &= ~MVPP2_GMAC_TX_FIFO_MIN_TH_ALL_MASK;
		/* Min. TX threshold must be less than minimal packet length */
		val |= MVPP2_GMAC_TX_FIFO_MIN_TH_MASK(64 - 4 - 2);
		writel(val, port->base + MVPP2_GMAC_PORT_FIFO_CFG_1_REG);
	}

	/* Disable Legacy WRR, Disable EJP, Release from reset */
	tx_port_num = mvpp2_egress_port(port);
	mvpp2_write(port->priv, MVPP2_TXP_SCHED_PORT_INDEX_REG,
		    tx_port_num);
	mvpp2_write(port->priv, MVPP2_TXP_SCHED_CMD_1_REG, 0);

	/* Close bandwidth for all queues */
	for (queue = 0; queue < MVPP2_MAX_TXQ; queue++) {
		ptxq = mvpp2_txq_phys(port->id, queue);
		mvpp2_write(port->priv,
			    MVPP2_TXQ_SCHED_TOKEN_CNTR_REG(ptxq), 0);
	}

	/* Set refill period to 1 usec, refill tokens
	 * and bucket size to maximum
	 */
	mvpp2_write(port->priv, MVPP2_TXP_SCHED_PERIOD_REG,
		    port->priv->tclk / USEC_PER_SEC);
	val = mvpp2_read(port->priv, MVPP2_TXP_SCHED_REFILL_REG);
	val &= ~MVPP2_TXP_REFILL_PERIOD_ALL_MASK;
	val |= MVPP2_TXP_REFILL_PERIOD_MASK(1);
	val |= MVPP2_TXP_REFILL_TOKENS_ALL_MASK;
	mvpp2_write(port->priv, MVPP2_TXP_SCHED_REFILL_REG, val);
	val = MVPP2_TXP_TOKEN_SIZE_MAX;
	mvpp2_write(port->priv, MVPP2_TXP_SCHED_TOKEN_SIZE_REG, val);

	/* Set MaximumLowLatencyPacketSize value to 256 */
	mvpp2_write(port->priv, MVPP2_RX_CTRL_REG(port->id),
		    MVPP2_RX_USE_PSEUDO_FOR_CSUM_MASK |
		    MVPP2_RX_LOW_LATENCY_PKT_SIZE(256));

	/* Enable Rx cache snoop */
	for (lrxq = 0; lrxq < port->nrxqs; lrxq++) {
		queue = port->rxqs[lrxq]->id;
		val = mvpp2_read(port->priv, MVPP2_RXQ_CONFIG_REG(queue));
		val |= MVPP2_SNOOP_PKT_SIZE_MASK |
			   MVPP2_SNOOP_BUF_HDR_MASK;
		mvpp2_write(port->priv, MVPP2_RXQ_CONFIG_REG(queue), val);
	}

	/* At default, mask all interrupts to all present cpus */
	mvpp2_interrupts_disable(port);
}

/* Enable/disable receiving packets */
static void mvpp2_ingress_enable(struct mvpp2_port *port)
{
	u32 val;
	int lrxq, queue;

	for (lrxq = 0; lrxq < port->nrxqs; lrxq++) {
		queue = port->rxqs[lrxq]->id;
		val = mvpp2_read(port->priv, MVPP2_RXQ_CONFIG_REG(queue));
		val &= ~MVPP2_RXQ_DISABLE_MASK;
		mvpp2_write(port->priv, MVPP2_RXQ_CONFIG_REG(queue), val);
	}
}

static void mvpp2_ingress_disable(struct mvpp2_port *port)
{
	u32 val;
	int lrxq, queue;

	for (lrxq = 0; lrxq < port->nrxqs; lrxq++) {
		queue = port->rxqs[lrxq]->id;
		val = mvpp2_read(port->priv, MVPP2_RXQ_CONFIG_REG(queue));
		val |= MVPP2_RXQ_DISABLE_MASK;
		mvpp2_write(port->priv, MVPP2_RXQ_CONFIG_REG(queue), val);
	}
}

/* Enable transmit via physical egress queue
 * - HW starts take descriptors from DRAM
 */
static void mvpp2_egress_enable(struct mvpp2_port *port)
{
	u32 qmap;
	int queue;
	int tx_port_num = mvpp2_egress_port(port);

	/* Enable all initialized TXs. */
	qmap = 0;
	for (queue = 0; queue < port->ntxqs; queue++) {
		struct mvpp2_tx_queue *txq = port->txqs[queue];

		if (txq->descs)
			qmap |= (1 << queue);
	}

	mvpp2_write(port->priv, MVPP2_TXP_SCHED_PORT_INDEX_REG, tx_port_num);
	mvpp2_write(port->priv, MVPP2_TXP_SCHED_Q_CMD_REG, qmap);
}

/* Disable transmit via physical egress queue
 * - HW doesn't take descriptors from DRAM
 */
static void mvpp2_egress_disable(struct mvpp2_port *port)
{
	u32 reg_data;
	int delay;
	int tx_port_num = mvpp2_egress_port(port);

	/* Issue stop command for active channels only */
	mvpp2_write(port->priv, MVPP2_TXP_SCHED_PORT_INDEX_REG, tx_port_num);
	reg_data = (mvpp2_read(port->priv, MVPP2_TXP_SCHED_Q_CMD_REG)) &
		    MVPP2_TXP_SCHED_ENQ_MASK;
	if (reg_data != 0)
		mvpp2_write(port->priv, MVPP2_TXP_SCHED_Q_CMD_REG,
			    (reg_data << MVPP2_TXP_SCHED_DISQ_OFFSET));

	/* Wait for all Tx activity to terminate. */
	delay = 0;
	do {
		if (delay >= MVPP2_TX_DISABLE_TIMEOUT_MSEC) {
			netdev_warn(port->dev,
				    "Tx stop timed out, status=0x%08x\n",
				    reg_data);
			break;
		}
		mdelay(1);
		delay++;

		/* Check port TX Command register that all
		 * Tx queues are stopped
		 */
		reg_data = mvpp2_read(port->priv, MVPP2_TXP_SCHED_Q_CMD_REG);
	} while (reg_data & MVPP2_TXP_SCHED_ENQ_MASK);
}

/* Rx descriptors helper methods */

/* Get number of Rx descriptors occupied by received packets */
static inline int
mvpp2_rxq_received(struct mvpp2_port *port, int rxq_id)
{
	u32 val = mvpp2_read(port->priv, MVPP2_RXQ_STATUS_REG(rxq_id));

	return val & MVPP2_RXQ_OCCUPIED_MASK;
}

/* Update Rx queue status with the number of occupied and available
 * Rx descriptor slots.
 */
static inline void
mvpp2_rxq_status_update(struct mvpp2_port *port, int rxq_id,
			int used_count, int free_count)
{
	/* Decrement the number of used descriptors and increment count
	 * increment the number of free descriptors.
	 */
	u32 val = used_count | (free_count << MVPP2_RXQ_NUM_NEW_OFFSET);

	mvpp2_write(port->priv, MVPP2_RXQ_STATUS_UPDATE_REG(rxq_id), val);
}

/* Get pointer to next RX descriptor to be processed by SW */
static inline struct mvpp2_rx_desc *
mvpp2_rxq_next_desc_get(struct mvpp2_rx_queue *rxq)
{
	int rx_desc = rxq->next_desc_to_proc;

	rxq->next_desc_to_proc = MVPP2_QUEUE_NEXT_DESC(rxq, rx_desc);
	prefetch(rxq->descs + rxq->next_desc_to_proc);
	return rxq->descs + rx_desc;
}

/* Set rx queue offset */
static void mvpp2_rxq_offset_set(struct mvpp2_port *port,
				 int prxq, int offset)
{
	u32 val;

	/* Convert offset from bytes to units of 32 bytes */
	offset = offset >> 5;

	val = mvpp2_read(port->priv, MVPP2_RXQ_CONFIG_REG(prxq));
	val &= ~MVPP2_RXQ_PACKET_OFFSET_MASK;

	/* Offset is in */
	val |= ((offset << MVPP2_RXQ_PACKET_OFFSET_OFFS) &
		    MVPP2_RXQ_PACKET_OFFSET_MASK);

	mvpp2_write(port->priv, MVPP2_RXQ_CONFIG_REG(prxq), val);
}

/* Tx descriptors helper methods */

/* Get pointer to next Tx descriptor to be processed (send) by HW */
static struct mvpp2_tx_desc *
mvpp2_txq_next_desc_get(struct mvpp2_tx_queue *txq)
{
	int tx_desc = txq->next_desc_to_proc;

	txq->next_desc_to_proc = MVPP2_QUEUE_NEXT_DESC(txq, tx_desc);
	return txq->descs + tx_desc;
}

/* Update HW with number of aggregated Tx descriptors to be sent
 *
 * Called only from mvpp2_tx(), so migration is disabled, using
 * smp_processor_id() is OK.
 */
static void mvpp2_aggr_txq_pend_desc_add(struct mvpp2_port *port, int pending)
{
	/* aggregated access - relevant TXQ number is written in TX desc */
	mvpp2_percpu_write(port->priv, smp_processor_id(),
			   MVPP2_AGGR_TXQ_UPDATE_REG, pending);
}

/* Check if there are enough free descriptors in aggregated txq.
 * If not, update the number of occupied descriptors and repeat the check.
 *
 * Called only from mvpp2_tx(), so migration is disabled, using
 * smp_processor_id() is OK.
 */
static int mvpp2_aggr_desc_num_check(struct mvpp2 *priv,
				     struct mvpp2_tx_queue *aggr_txq, int num)
{
	if ((aggr_txq->count + num) > MVPP2_AGGR_TXQ_SIZE) {
		/* Update number of occupied aggregated Tx descriptors */
		int cpu = smp_processor_id();
		u32 val = mvpp2_read_relaxed(priv,
					     MVPP2_AGGR_TXQ_STATUS_REG(cpu));

		aggr_txq->count = val & MVPP2_AGGR_TXQ_PENDING_MASK;

		if ((aggr_txq->count + num) > MVPP2_AGGR_TXQ_SIZE)
			return -ENOMEM;
	}
	return 0;
}

/* Reserved Tx descriptors allocation request
 *
 * Called only from mvpp2_txq_reserved_desc_num_proc(), itself called
 * only by mvpp2_tx(), so migration is disabled, using
 * smp_processor_id() is OK.
 */
static int mvpp2_txq_alloc_reserved_desc(struct mvpp2 *priv,
					 struct mvpp2_tx_queue *txq, int num)
{
	u32 val;
	int cpu = smp_processor_id();

	val = (txq->id << MVPP2_TXQ_RSVD_REQ_Q_OFFSET) | num;
	mvpp2_percpu_write_relaxed(priv, cpu, MVPP2_TXQ_RSVD_REQ_REG, val);

	val = mvpp2_percpu_read_relaxed(priv, cpu, MVPP2_TXQ_RSVD_RSLT_REG);

	return val & MVPP2_TXQ_RSVD_RSLT_MASK;
}

/* Check if there are enough reserved descriptors for transmission.
 * If not, request chunk of reserved descriptors and check again.
 */
static int mvpp2_txq_reserved_desc_num_proc(struct mvpp2 *priv,
					    struct mvpp2_tx_queue *txq,
					    struct mvpp2_txq_pcpu *txq_pcpu,
					    int num)
{
	int req, cpu, desc_count;

	if (txq_pcpu->reserved_num >= num)
		return 0;

	/* Not enough descriptors reserved! Update the reserved descriptor
	 * count and check again.
	 */

	desc_count = 0;
	/* Compute total of used descriptors */
	for_each_present_cpu(cpu) {
		struct mvpp2_txq_pcpu *txq_pcpu_aux;

		txq_pcpu_aux = per_cpu_ptr(txq->pcpu, cpu);
		desc_count += txq_pcpu_aux->count;
		desc_count += txq_pcpu_aux->reserved_num;
	}

	req = max(MVPP2_CPU_DESC_CHUNK, num - txq_pcpu->reserved_num);
	desc_count += req;

	if (desc_count >
	   (txq->size - (num_present_cpus() * MVPP2_CPU_DESC_CHUNK)))
		return -ENOMEM;

	txq_pcpu->reserved_num += mvpp2_txq_alloc_reserved_desc(priv, txq, req);

	/* OK, the descriptor could have been updated: check again. */
	if (txq_pcpu->reserved_num < num)
		return -ENOMEM;
	return 0;
}

/* Release the last allocated Tx descriptor. Useful to handle DMA
 * mapping failures in the Tx path.
 */
static void mvpp2_txq_desc_put(struct mvpp2_tx_queue *txq)
{
	if (txq->next_desc_to_proc == 0)
		txq->next_desc_to_proc = txq->last_desc - 1;
	else
		txq->next_desc_to_proc--;
}

/* Set Tx descriptors fields relevant for CSUM calculation */
static u32 mvpp2_txq_desc_csum(int l3_offs, int l3_proto,
			       int ip_hdr_len, int l4_proto)
{
	u32 command;

	/* fields: L3_offset, IP_hdrlen, L3_type, G_IPv4_chk,
	 * G_L4_chk, L4_type required only for checksum calculation
	 */
	command = (l3_offs << MVPP2_TXD_L3_OFF_SHIFT);
	command |= (ip_hdr_len << MVPP2_TXD_IP_HLEN_SHIFT);
	command |= MVPP2_TXD_IP_CSUM_DISABLE;

	if (l3_proto == htons(ETH_P_IP)) {
		command &= ~MVPP2_TXD_IP_CSUM_DISABLE;	/* enable IPv4 csum */
		command &= ~MVPP2_TXD_L3_IP6;		/* enable IPv4 */
	} else {
		command |= MVPP2_TXD_L3_IP6;		/* enable IPv6 */
	}

	if (l4_proto == IPPROTO_TCP) {
		command &= ~MVPP2_TXD_L4_UDP;		/* enable TCP */
		command &= ~MVPP2_TXD_L4_CSUM_FRAG;	/* generate L4 csum */
	} else if (l4_proto == IPPROTO_UDP) {
		command |= MVPP2_TXD_L4_UDP;		/* enable UDP */
		command &= ~MVPP2_TXD_L4_CSUM_FRAG;	/* generate L4 csum */
	} else {
		command |= MVPP2_TXD_L4_CSUM_NOT;
	}

	return command;
}

/* Get number of sent descriptors and decrement counter.
 * The number of sent descriptors is returned.
 * Per-CPU access
 *
 * Called only from mvpp2_txq_done(), called from mvpp2_tx()
 * (migration disabled) and from the TX completion tasklet (migration
 * disabled) so using smp_processor_id() is OK.
 */
static inline int mvpp2_txq_sent_desc_proc(struct mvpp2_port *port,
					   struct mvpp2_tx_queue *txq)
{
	u32 val;

	/* Reading status reg resets transmitted descriptor counter */
	val = mvpp2_percpu_read_relaxed(port->priv, smp_processor_id(),
					MVPP2_TXQ_SENT_REG(txq->id));

	return (val & MVPP2_TRANSMITTED_COUNT_MASK) >>
		MVPP2_TRANSMITTED_COUNT_OFFSET;
}

/* Called through on_each_cpu(), so runs on all CPUs, with migration
 * disabled, therefore using smp_processor_id() is OK.
 */
static void mvpp2_txq_sent_counter_clear(void *arg)
{
	struct mvpp2_port *port = arg;
	int queue;

	for (queue = 0; queue < port->ntxqs; queue++) {
		int id = port->txqs[queue]->id;

		mvpp2_percpu_read(port->priv, smp_processor_id(),
				  MVPP2_TXQ_SENT_REG(id));
	}
}

/* Set max sizes for Tx queues */
static void mvpp2_txp_max_tx_size_set(struct mvpp2_port *port)
{
	u32	val, size, mtu;
	int	txq, tx_port_num;

	mtu = port->pkt_size * 8;
	if (mtu > MVPP2_TXP_MTU_MAX)
		mtu = MVPP2_TXP_MTU_MAX;

	/* WA for wrong Token bucket update: Set MTU value = 3*real MTU value */
	mtu = 3 * mtu;

	/* Indirect access to registers */
	tx_port_num = mvpp2_egress_port(port);
	mvpp2_write(port->priv, MVPP2_TXP_SCHED_PORT_INDEX_REG, tx_port_num);

	/* Set MTU */
	val = mvpp2_read(port->priv, MVPP2_TXP_SCHED_MTU_REG);
	val &= ~MVPP2_TXP_MTU_MAX;
	val |= mtu;
	mvpp2_write(port->priv, MVPP2_TXP_SCHED_MTU_REG, val);

	/* TXP token size and all TXQs token size must be larger that MTU */
	val = mvpp2_read(port->priv, MVPP2_TXP_SCHED_TOKEN_SIZE_REG);
	size = val & MVPP2_TXP_TOKEN_SIZE_MAX;
	if (size < mtu) {
		size = mtu;
		val &= ~MVPP2_TXP_TOKEN_SIZE_MAX;
		val |= size;
		mvpp2_write(port->priv, MVPP2_TXP_SCHED_TOKEN_SIZE_REG, val);
	}

	for (txq = 0; txq < port->ntxqs; txq++) {
		val = mvpp2_read(port->priv,
				 MVPP2_TXQ_SCHED_TOKEN_SIZE_REG(txq));
		size = val & MVPP2_TXQ_TOKEN_SIZE_MAX;

		if (size < mtu) {
			size = mtu;
			val &= ~MVPP2_TXQ_TOKEN_SIZE_MAX;
			val |= size;
			mvpp2_write(port->priv,
				    MVPP2_TXQ_SCHED_TOKEN_SIZE_REG(txq),
				    val);
		}
	}
}

/* Set the number of packets that will be received before Rx interrupt
 * will be generated by HW.
 */
static void mvpp2_rx_pkts_coal_set(struct mvpp2_port *port,
				   struct mvpp2_rx_queue *rxq)
{
	int cpu = get_cpu();

	if (rxq->pkts_coal > MVPP2_OCCUPIED_THRESH_MASK)
		rxq->pkts_coal = MVPP2_OCCUPIED_THRESH_MASK;

	mvpp2_percpu_write(port->priv, cpu, MVPP2_RXQ_NUM_REG, rxq->id);
	mvpp2_percpu_write(port->priv, cpu, MVPP2_RXQ_THRESH_REG,
			   rxq->pkts_coal);

	put_cpu();
}

/* For some reason in the LSP this is done on each CPU. Why ? */
static void mvpp2_tx_pkts_coal_set(struct mvpp2_port *port,
				   struct mvpp2_tx_queue *txq)
{
	int cpu = get_cpu();
	u32 val;

	if (txq->done_pkts_coal > MVPP2_TXQ_THRESH_MASK)
		txq->done_pkts_coal = MVPP2_TXQ_THRESH_MASK;

	val = (txq->done_pkts_coal << MVPP2_TXQ_THRESH_OFFSET);
	mvpp2_percpu_write(port->priv, cpu, MVPP2_TXQ_NUM_REG, txq->id);
	mvpp2_percpu_write(port->priv, cpu, MVPP2_TXQ_THRESH_REG, val);

	put_cpu();
}

static u32 mvpp2_usec_to_cycles(u32 usec, unsigned long clk_hz)
{
	u64 tmp = (u64)clk_hz * usec;

	do_div(tmp, USEC_PER_SEC);

	return tmp > U32_MAX ? U32_MAX : tmp;
}

static u32 mvpp2_cycles_to_usec(u32 cycles, unsigned long clk_hz)
{
	u64 tmp = (u64)cycles * USEC_PER_SEC;

	do_div(tmp, clk_hz);

	return tmp > U32_MAX ? U32_MAX : tmp;
}

/* Set the time delay in usec before Rx interrupt */
static void mvpp2_rx_time_coal_set(struct mvpp2_port *port,
				   struct mvpp2_rx_queue *rxq)
{
	unsigned long freq = port->priv->tclk;
	u32 val = mvpp2_usec_to_cycles(rxq->time_coal, freq);

	if (val > MVPP2_MAX_ISR_RX_THRESHOLD) {
		rxq->time_coal =
			mvpp2_cycles_to_usec(MVPP2_MAX_ISR_RX_THRESHOLD, freq);

		/* re-evaluate to get actual register value */
		val = mvpp2_usec_to_cycles(rxq->time_coal, freq);
	}

	mvpp2_write(port->priv, MVPP2_ISR_RX_THRESHOLD_REG(rxq->id), val);
}

static void mvpp2_tx_time_coal_set(struct mvpp2_port *port)
{
	unsigned long freq = port->priv->tclk;
	u32 val = mvpp2_usec_to_cycles(port->tx_time_coal, freq);

	if (val > MVPP2_MAX_ISR_TX_THRESHOLD) {
		port->tx_time_coal =
			mvpp2_cycles_to_usec(MVPP2_MAX_ISR_TX_THRESHOLD, freq);

		/* re-evaluate to get actual register value */
		val = mvpp2_usec_to_cycles(port->tx_time_coal, freq);
	}

	mvpp2_write(port->priv, MVPP2_ISR_TX_THRESHOLD_REG(port->id), val);
}

/* Free Tx queue skbuffs */
static void mvpp2_txq_bufs_free(struct mvpp2_port *port,
				struct mvpp2_tx_queue *txq,
				struct mvpp2_txq_pcpu *txq_pcpu, int num)
{
	int i;

	for (i = 0; i < num; i++) {
		struct mvpp2_txq_pcpu_buf *tx_buf =
			txq_pcpu->buffs + txq_pcpu->txq_get_index;

		if (!IS_TSO_HEADER(txq_pcpu, tx_buf->dma))
			dma_unmap_single(port->dev->dev.parent, tx_buf->dma,
					 tx_buf->size, DMA_TO_DEVICE);
		if (tx_buf->skb)
			dev_kfree_skb_any(tx_buf->skb);

		mvpp2_txq_inc_get(txq_pcpu);
	}
}

static inline struct mvpp2_rx_queue *mvpp2_get_rx_queue(struct mvpp2_port *port,
							u32 cause)
{
	int queue = fls(cause) - 1;

	return port->rxqs[queue];
}

static inline struct mvpp2_tx_queue *mvpp2_get_tx_queue(struct mvpp2_port *port,
							u32 cause)
{
	int queue = fls(cause) - 1;

	return port->txqs[queue];
}

/* Handle end of transmission */
static void mvpp2_txq_done(struct mvpp2_port *port, struct mvpp2_tx_queue *txq,
			   struct mvpp2_txq_pcpu *txq_pcpu)
{
	struct netdev_queue *nq = netdev_get_tx_queue(port->dev, txq->log_id);
	int tx_done;

	if (txq_pcpu->cpu != smp_processor_id())
		netdev_err(port->dev, "wrong cpu on the end of Tx processing\n");

	tx_done = mvpp2_txq_sent_desc_proc(port, txq);
	if (!tx_done)
		return;
	mvpp2_txq_bufs_free(port, txq, txq_pcpu, tx_done);

	txq_pcpu->count -= tx_done;

	if (netif_tx_queue_stopped(nq))
		if (txq_pcpu->count <= txq_pcpu->wake_threshold)
			netif_tx_wake_queue(nq);
}

static unsigned int mvpp2_tx_done(struct mvpp2_port *port, u32 cause,
				  int cpu)
{
	struct mvpp2_tx_queue *txq;
	struct mvpp2_txq_pcpu *txq_pcpu;
	unsigned int tx_todo = 0;

	while (cause) {
		txq = mvpp2_get_tx_queue(port, cause);
		if (!txq)
			break;

		txq_pcpu = per_cpu_ptr(txq->pcpu, cpu);

		if (txq_pcpu->count) {
			mvpp2_txq_done(port, txq, txq_pcpu);
			tx_todo += txq_pcpu->count;
		}

		cause &= ~(1 << txq->log_id);
	}
	return tx_todo;
}

/* Rx/Tx queue initialization/cleanup methods */

/* Allocate and initialize descriptors for aggr TXQ */
static int mvpp2_aggr_txq_init(struct platform_device *pdev,
			       struct mvpp2_tx_queue *aggr_txq, int cpu,
			       struct mvpp2 *priv)
{
	u32 txq_dma;

	/* Allocate memory for TX descriptors */
	aggr_txq->descs = dma_zalloc_coherent(&pdev->dev,
				MVPP2_AGGR_TXQ_SIZE * MVPP2_DESC_ALIGNED_SIZE,
				&aggr_txq->descs_dma, GFP_KERNEL);
	if (!aggr_txq->descs)
		return -ENOMEM;

	aggr_txq->last_desc = MVPP2_AGGR_TXQ_SIZE - 1;

	/* Aggr TXQ no reset WA */
	aggr_txq->next_desc_to_proc = mvpp2_read(priv,
						 MVPP2_AGGR_TXQ_INDEX_REG(cpu));

	/* Set Tx descriptors queue starting address indirect
	 * access
	 */
	if (priv->hw_version == MVPP21)
		txq_dma = aggr_txq->descs_dma;
	else
		txq_dma = aggr_txq->descs_dma >>
			MVPP22_AGGR_TXQ_DESC_ADDR_OFFS;

	mvpp2_write(priv, MVPP2_AGGR_TXQ_DESC_ADDR_REG(cpu), txq_dma);
	mvpp2_write(priv, MVPP2_AGGR_TXQ_DESC_SIZE_REG(cpu),
		    MVPP2_AGGR_TXQ_SIZE);

	return 0;
}

/* Create a specified Rx queue */
static int mvpp2_rxq_init(struct mvpp2_port *port,
			  struct mvpp2_rx_queue *rxq)

{
	u32 rxq_dma;
	int cpu;

	rxq->size = port->rx_ring_size;

	/* Allocate memory for RX descriptors */
	rxq->descs = dma_alloc_coherent(port->dev->dev.parent,
					rxq->size * MVPP2_DESC_ALIGNED_SIZE,
					&rxq->descs_dma, GFP_KERNEL);
	if (!rxq->descs)
		return -ENOMEM;

	rxq->last_desc = rxq->size - 1;

	/* Zero occupied and non-occupied counters - direct access */
	mvpp2_write(port->priv, MVPP2_RXQ_STATUS_REG(rxq->id), 0);

	/* Set Rx descriptors queue starting address - indirect access */
	cpu = get_cpu();
	mvpp2_percpu_write(port->priv, cpu, MVPP2_RXQ_NUM_REG, rxq->id);
	if (port->priv->hw_version == MVPP21)
		rxq_dma = rxq->descs_dma;
	else
		rxq_dma = rxq->descs_dma >> MVPP22_DESC_ADDR_OFFS;
	mvpp2_percpu_write(port->priv, cpu, MVPP2_RXQ_DESC_ADDR_REG, rxq_dma);
	mvpp2_percpu_write(port->priv, cpu, MVPP2_RXQ_DESC_SIZE_REG, rxq->size);
	mvpp2_percpu_write(port->priv, cpu, MVPP2_RXQ_INDEX_REG, 0);
	put_cpu();

	/* Set Offset */
	mvpp2_rxq_offset_set(port, rxq->id, NET_SKB_PAD);

	/* Set coalescing pkts and time */
	mvpp2_rx_pkts_coal_set(port, rxq);
	mvpp2_rx_time_coal_set(port, rxq);

	/* Add number of descriptors ready for receiving packets */
	mvpp2_rxq_status_update(port, rxq->id, 0, rxq->size);

	return 0;
}

/* Push packets received by the RXQ to BM pool */
static void mvpp2_rxq_drop_pkts(struct mvpp2_port *port,
				struct mvpp2_rx_queue *rxq)
{
	int rx_received, i;

	rx_received = mvpp2_rxq_received(port, rxq->id);
	if (!rx_received)
		return;

	for (i = 0; i < rx_received; i++) {
		struct mvpp2_rx_desc *rx_desc = mvpp2_rxq_next_desc_get(rxq);
		u32 status = mvpp2_rxdesc_status_get(port, rx_desc);
		int pool;

		pool = (status & MVPP2_RXD_BM_POOL_ID_MASK) >>
			MVPP2_RXD_BM_POOL_ID_OFFS;

		mvpp2_bm_pool_put(port, pool,
				  mvpp2_rxdesc_dma_addr_get(port, rx_desc),
				  mvpp2_rxdesc_cookie_get(port, rx_desc));
	}
	mvpp2_rxq_status_update(port, rxq->id, rx_received, rx_received);
}

/* Cleanup Rx queue */
static void mvpp2_rxq_deinit(struct mvpp2_port *port,
			     struct mvpp2_rx_queue *rxq)
{
	int cpu;

	mvpp2_rxq_drop_pkts(port, rxq);

	if (rxq->descs)
		dma_free_coherent(port->dev->dev.parent,
				  rxq->size * MVPP2_DESC_ALIGNED_SIZE,
				  rxq->descs,
				  rxq->descs_dma);

	rxq->descs             = NULL;
	rxq->last_desc         = 0;
	rxq->next_desc_to_proc = 0;
	rxq->descs_dma         = 0;

	/* Clear Rx descriptors queue starting address and size;
	 * free descriptor number
	 */
	mvpp2_write(port->priv, MVPP2_RXQ_STATUS_REG(rxq->id), 0);
	cpu = get_cpu();
	mvpp2_percpu_write(port->priv, cpu, MVPP2_RXQ_NUM_REG, rxq->id);
	mvpp2_percpu_write(port->priv, cpu, MVPP2_RXQ_DESC_ADDR_REG, 0);
	mvpp2_percpu_write(port->priv, cpu, MVPP2_RXQ_DESC_SIZE_REG, 0);
	put_cpu();
}

/* Create and initialize a Tx queue */
static int mvpp2_txq_init(struct mvpp2_port *port,
			  struct mvpp2_tx_queue *txq)
{
	u32 val;
	int cpu, desc, desc_per_txq, tx_port_num;
	struct mvpp2_txq_pcpu *txq_pcpu;

	txq->size = port->tx_ring_size;

	/* Allocate memory for Tx descriptors */
	txq->descs = dma_alloc_coherent(port->dev->dev.parent,
				txq->size * MVPP2_DESC_ALIGNED_SIZE,
				&txq->descs_dma, GFP_KERNEL);
	if (!txq->descs)
		return -ENOMEM;

	txq->last_desc = txq->size - 1;

	/* Set Tx descriptors queue starting address - indirect access */
	cpu = get_cpu();
	mvpp2_percpu_write(port->priv, cpu, MVPP2_TXQ_NUM_REG, txq->id);
	mvpp2_percpu_write(port->priv, cpu, MVPP2_TXQ_DESC_ADDR_REG,
			   txq->descs_dma);
	mvpp2_percpu_write(port->priv, cpu, MVPP2_TXQ_DESC_SIZE_REG,
			   txq->size & MVPP2_TXQ_DESC_SIZE_MASK);
	mvpp2_percpu_write(port->priv, cpu, MVPP2_TXQ_INDEX_REG, 0);
	mvpp2_percpu_write(port->priv, cpu, MVPP2_TXQ_RSVD_CLR_REG,
			   txq->id << MVPP2_TXQ_RSVD_CLR_OFFSET);
	val = mvpp2_percpu_read(port->priv, cpu, MVPP2_TXQ_PENDING_REG);
	val &= ~MVPP2_TXQ_PENDING_MASK;
	mvpp2_percpu_write(port->priv, cpu, MVPP2_TXQ_PENDING_REG, val);

	/* Calculate base address in prefetch buffer. We reserve 16 descriptors
	 * for each existing TXQ.
	 * TCONTS for PON port must be continuous from 0 to MVPP2_MAX_TCONT
	 * GBE ports assumed to be continuous from 0 to MVPP2_MAX_PORTS
	 */
	desc_per_txq = 16;
	desc = (port->id * MVPP2_MAX_TXQ * desc_per_txq) +
	       (txq->log_id * desc_per_txq);

	mvpp2_percpu_write(port->priv, cpu, MVPP2_TXQ_PREF_BUF_REG,
			   MVPP2_PREF_BUF_PTR(desc) | MVPP2_PREF_BUF_SIZE_16 |
			   MVPP2_PREF_BUF_THRESH(desc_per_txq / 2));
	put_cpu();

	/* WRR / EJP configuration - indirect access */
	tx_port_num = mvpp2_egress_port(port);
	mvpp2_write(port->priv, MVPP2_TXP_SCHED_PORT_INDEX_REG, tx_port_num);

	val = mvpp2_read(port->priv, MVPP2_TXQ_SCHED_REFILL_REG(txq->log_id));
	val &= ~MVPP2_TXQ_REFILL_PERIOD_ALL_MASK;
	val |= MVPP2_TXQ_REFILL_PERIOD_MASK(1);
	val |= MVPP2_TXQ_REFILL_TOKENS_ALL_MASK;
	mvpp2_write(port->priv, MVPP2_TXQ_SCHED_REFILL_REG(txq->log_id), val);

	val = MVPP2_TXQ_TOKEN_SIZE_MAX;
	mvpp2_write(port->priv, MVPP2_TXQ_SCHED_TOKEN_SIZE_REG(txq->log_id),
		    val);

	for_each_present_cpu(cpu) {
		txq_pcpu = per_cpu_ptr(txq->pcpu, cpu);
		txq_pcpu->size = txq->size;
		txq_pcpu->buffs = kmalloc_array(txq_pcpu->size,
						sizeof(*txq_pcpu->buffs),
						GFP_KERNEL);
		if (!txq_pcpu->buffs)
			return -ENOMEM;

		txq_pcpu->count = 0;
		txq_pcpu->reserved_num = 0;
		txq_pcpu->txq_put_index = 0;
		txq_pcpu->txq_get_index = 0;
		txq_pcpu->tso_headers = NULL;

		txq_pcpu->stop_threshold = txq->size - MVPP2_MAX_SKB_DESCS;
		txq_pcpu->wake_threshold = txq_pcpu->stop_threshold / 2;

		txq_pcpu->tso_headers =
			dma_alloc_coherent(port->dev->dev.parent,
					   txq_pcpu->size * TSO_HEADER_SIZE,
					   &txq_pcpu->tso_headers_dma,
					   GFP_KERNEL);
		if (!txq_pcpu->tso_headers)
			return -ENOMEM;
	}

	return 0;
}

/* Free allocated TXQ resources */
static void mvpp2_txq_deinit(struct mvpp2_port *port,
			     struct mvpp2_tx_queue *txq)
{
	struct mvpp2_txq_pcpu *txq_pcpu;
	int cpu;

	for_each_present_cpu(cpu) {
		txq_pcpu = per_cpu_ptr(txq->pcpu, cpu);
		kfree(txq_pcpu->buffs);

		if (txq_pcpu->tso_headers)
			dma_free_coherent(port->dev->dev.parent,
					  txq_pcpu->size * TSO_HEADER_SIZE,
					  txq_pcpu->tso_headers,
					  txq_pcpu->tso_headers_dma);

		txq_pcpu->tso_headers = NULL;
	}

	if (txq->descs)
		dma_free_coherent(port->dev->dev.parent,
				  txq->size * MVPP2_DESC_ALIGNED_SIZE,
				  txq->descs, txq->descs_dma);

	txq->descs             = NULL;
	txq->last_desc         = 0;
	txq->next_desc_to_proc = 0;
	txq->descs_dma         = 0;

	/* Set minimum bandwidth for disabled TXQs */
	mvpp2_write(port->priv, MVPP2_TXQ_SCHED_TOKEN_CNTR_REG(txq->id), 0);

	/* Set Tx descriptors queue starting address and size */
	cpu = get_cpu();
	mvpp2_percpu_write(port->priv, cpu, MVPP2_TXQ_NUM_REG, txq->id);
	mvpp2_percpu_write(port->priv, cpu, MVPP2_TXQ_DESC_ADDR_REG, 0);
	mvpp2_percpu_write(port->priv, cpu, MVPP2_TXQ_DESC_SIZE_REG, 0);
	put_cpu();
}

/* Cleanup Tx ports */
static void mvpp2_txq_clean(struct mvpp2_port *port, struct mvpp2_tx_queue *txq)
{
	struct mvpp2_txq_pcpu *txq_pcpu;
	int delay, pending, cpu;
	u32 val;

	cpu = get_cpu();
	mvpp2_percpu_write(port->priv, cpu, MVPP2_TXQ_NUM_REG, txq->id);
	val = mvpp2_percpu_read(port->priv, cpu, MVPP2_TXQ_PREF_BUF_REG);
	val |= MVPP2_TXQ_DRAIN_EN_MASK;
	mvpp2_percpu_write(port->priv, cpu, MVPP2_TXQ_PREF_BUF_REG, val);

	/* The napi queue has been stopped so wait for all packets
	 * to be transmitted.
	 */
	delay = 0;
	do {
		if (delay >= MVPP2_TX_PENDING_TIMEOUT_MSEC) {
			netdev_warn(port->dev,
				    "port %d: cleaning queue %d timed out\n",
				    port->id, txq->log_id);
			break;
		}
		mdelay(1);
		delay++;

		pending = mvpp2_percpu_read(port->priv, cpu,
					    MVPP2_TXQ_PENDING_REG);
		pending &= MVPP2_TXQ_PENDING_MASK;
	} while (pending);

	val &= ~MVPP2_TXQ_DRAIN_EN_MASK;
	mvpp2_percpu_write(port->priv, cpu, MVPP2_TXQ_PREF_BUF_REG, val);
	put_cpu();

	for_each_present_cpu(cpu) {
		txq_pcpu = per_cpu_ptr(txq->pcpu, cpu);

		/* Release all packets */
		mvpp2_txq_bufs_free(port, txq, txq_pcpu, txq_pcpu->count);

		/* Reset queue */
		txq_pcpu->count = 0;
		txq_pcpu->txq_put_index = 0;
		txq_pcpu->txq_get_index = 0;
	}
}

/* Cleanup all Tx queues */
static void mvpp2_cleanup_txqs(struct mvpp2_port *port)
{
	struct mvpp2_tx_queue *txq;
	int queue;
	u32 val;

	val = mvpp2_read(port->priv, MVPP2_TX_PORT_FLUSH_REG);

	/* Reset Tx ports and delete Tx queues */
	val |= MVPP2_TX_PORT_FLUSH_MASK(port->id);
	mvpp2_write(port->priv, MVPP2_TX_PORT_FLUSH_REG, val);

	for (queue = 0; queue < port->ntxqs; queue++) {
		txq = port->txqs[queue];
		mvpp2_txq_clean(port, txq);
		mvpp2_txq_deinit(port, txq);
	}

	on_each_cpu(mvpp2_txq_sent_counter_clear, port, 1);

	val &= ~MVPP2_TX_PORT_FLUSH_MASK(port->id);
	mvpp2_write(port->priv, MVPP2_TX_PORT_FLUSH_REG, val);
}

/* Cleanup all Rx queues */
static void mvpp2_cleanup_rxqs(struct mvpp2_port *port)
{
	int queue;

	for (queue = 0; queue < port->nrxqs; queue++)
		mvpp2_rxq_deinit(port, port->rxqs[queue]);
}

/* Init all Rx queues for port */
static int mvpp2_setup_rxqs(struct mvpp2_port *port)
{
	int queue, err;

	for (queue = 0; queue < port->nrxqs; queue++) {
		err = mvpp2_rxq_init(port, port->rxqs[queue]);
		if (err)
			goto err_cleanup;
	}
	return 0;

err_cleanup:
	mvpp2_cleanup_rxqs(port);
	return err;
}

/* Init all tx queues for port */
static int mvpp2_setup_txqs(struct mvpp2_port *port)
{
	struct mvpp2_tx_queue *txq;
	int queue, err;

	for (queue = 0; queue < port->ntxqs; queue++) {
		txq = port->txqs[queue];
		err = mvpp2_txq_init(port, txq);
		if (err)
			goto err_cleanup;
	}

	if (port->has_tx_irqs) {
		mvpp2_tx_time_coal_set(port);
		for (queue = 0; queue < port->ntxqs; queue++) {
			txq = port->txqs[queue];
			mvpp2_tx_pkts_coal_set(port, txq);
		}
	}

	on_each_cpu(mvpp2_txq_sent_counter_clear, port, 1);
	return 0;

err_cleanup:
	mvpp2_cleanup_txqs(port);
	return err;
}

/* The callback for per-port interrupt */
static irqreturn_t mvpp2_isr(int irq, void *dev_id)
{
	struct mvpp2_queue_vector *qv = dev_id;

	mvpp2_qvec_interrupt_disable(qv);

	napi_schedule(&qv->napi);

	return IRQ_HANDLED;
}

/* Per-port interrupt for link status changes */
static irqreturn_t mvpp2_link_status_isr(int irq, void *dev_id)
{
	struct mvpp2_port *port = (struct mvpp2_port *)dev_id;
	struct net_device *dev = port->dev;
	bool event = false, link = false;
	u32 val;

	mvpp22_gop_mask_irq(port);

	if (port->gop_id == 0 &&
	    port->phy_interface == PHY_INTERFACE_MODE_10GKR) {
		val = readl(port->base + MVPP22_XLG_INT_STAT);
		if (val & MVPP22_XLG_INT_STAT_LINK) {
			event = true;
			val = readl(port->base + MVPP22_XLG_STATUS);
			if (val & MVPP22_XLG_STATUS_LINK_UP)
				link = true;
		}
	} else if (phy_interface_mode_is_rgmii(port->phy_interface) ||
		   port->phy_interface == PHY_INTERFACE_MODE_SGMII ||
		   port->phy_interface == PHY_INTERFACE_MODE_1000BASEX ||
		   port->phy_interface == PHY_INTERFACE_MODE_2500BASEX) {
		val = readl(port->base + MVPP22_GMAC_INT_STAT);
		if (val & MVPP22_GMAC_INT_STAT_LINK) {
			event = true;
			val = readl(port->base + MVPP2_GMAC_STATUS0);
			if (val & MVPP2_GMAC_STATUS0_LINK_UP)
				link = true;
		}
	}

	if (port->phylink) {
		phylink_mac_change(port->phylink, link);
		goto handled;
	}

	if (!netif_running(dev) || !event)
		goto handled;

	if (link) {
		mvpp2_interrupts_enable(port);

		mvpp2_egress_enable(port);
		mvpp2_ingress_enable(port);
		netif_carrier_on(dev);
		netif_tx_wake_all_queues(dev);
	} else {
		netif_tx_stop_all_queues(dev);
		netif_carrier_off(dev);
		mvpp2_ingress_disable(port);
		mvpp2_egress_disable(port);

		mvpp2_interrupts_disable(port);
	}

handled:
	mvpp22_gop_unmask_irq(port);
	return IRQ_HANDLED;
}

static void mvpp2_timer_set(struct mvpp2_port_pcpu *port_pcpu)
{
	ktime_t interval;

	if (!port_pcpu->timer_scheduled) {
		port_pcpu->timer_scheduled = true;
		interval = MVPP2_TXDONE_HRTIMER_PERIOD_NS;
		hrtimer_start(&port_pcpu->tx_done_timer, interval,
			      HRTIMER_MODE_REL_PINNED);
	}
}

static void mvpp2_tx_proc_cb(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct mvpp2_port *port = netdev_priv(dev);
	struct mvpp2_port_pcpu *port_pcpu = this_cpu_ptr(port->pcpu);
	unsigned int tx_todo, cause;

	if (!netif_running(dev))
		return;
	port_pcpu->timer_scheduled = false;

	/* Process all the Tx queues */
	cause = (1 << port->ntxqs) - 1;
	tx_todo = mvpp2_tx_done(port, cause, smp_processor_id());

	/* Set the timer in case not all the packets were processed */
	if (tx_todo)
		mvpp2_timer_set(port_pcpu);
}

static enum hrtimer_restart mvpp2_hr_timer_cb(struct hrtimer *timer)
{
	struct mvpp2_port_pcpu *port_pcpu = container_of(timer,
							 struct mvpp2_port_pcpu,
							 tx_done_timer);

	tasklet_schedule(&port_pcpu->tx_done_tasklet);

	return HRTIMER_NORESTART;
}

/* Main RX/TX processing routines */

/* Display more error info */
static void mvpp2_rx_error(struct mvpp2_port *port,
			   struct mvpp2_rx_desc *rx_desc)
{
	u32 status = mvpp2_rxdesc_status_get(port, rx_desc);
	size_t sz = mvpp2_rxdesc_size_get(port, rx_desc);
	char *err_str = NULL;

	switch (status & MVPP2_RXD_ERR_CODE_MASK) {
	case MVPP2_RXD_ERR_CRC:
		err_str = "crc";
		break;
	case MVPP2_RXD_ERR_OVERRUN:
		err_str = "overrun";
		break;
	case MVPP2_RXD_ERR_RESOURCE:
		err_str = "resource";
		break;
	}
	if (err_str && net_ratelimit())
		netdev_err(port->dev,
			   "bad rx status %08x (%s error), size=%zu\n",
			   status, err_str, sz);
}

/* Handle RX checksum offload */
static void mvpp2_rx_csum(struct mvpp2_port *port, u32 status,
			  struct sk_buff *skb)
{
	if (((status & MVPP2_RXD_L3_IP4) &&
	     !(status & MVPP2_RXD_IP4_HEADER_ERR)) ||
	    (status & MVPP2_RXD_L3_IP6))
		if (((status & MVPP2_RXD_L4_UDP) ||
		     (status & MVPP2_RXD_L4_TCP)) &&
		     (status & MVPP2_RXD_L4_CSUM_OK)) {
			skb->csum = 0;
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			return;
		}

	skb->ip_summed = CHECKSUM_NONE;
}

/* Reuse skb if possible, or allocate a new skb and add it to BM pool */
static int mvpp2_rx_refill(struct mvpp2_port *port,
			   struct mvpp2_bm_pool *bm_pool, int pool)
{
	dma_addr_t dma_addr;
	phys_addr_t phys_addr;
	void *buf;

	/* No recycle or too many buffers are in use, so allocate a new skb */
	buf = mvpp2_buf_alloc(port, bm_pool, &dma_addr, &phys_addr,
			      GFP_ATOMIC);
	if (!buf)
		return -ENOMEM;

	mvpp2_bm_pool_put(port, pool, dma_addr, phys_addr);

	return 0;
}

/* Handle tx checksum */
static u32 mvpp2_skb_tx_csum(struct mvpp2_port *port, struct sk_buff *skb)
{
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		int ip_hdr_len = 0;
		u8 l4_proto;

		if (skb->protocol == htons(ETH_P_IP)) {
			struct iphdr *ip4h = ip_hdr(skb);

			/* Calculate IPv4 checksum and L4 checksum */
			ip_hdr_len = ip4h->ihl;
			l4_proto = ip4h->protocol;
		} else if (skb->protocol == htons(ETH_P_IPV6)) {
			struct ipv6hdr *ip6h = ipv6_hdr(skb);

			/* Read l4_protocol from one of IPv6 extra headers */
			if (skb_network_header_len(skb) > 0)
				ip_hdr_len = (skb_network_header_len(skb) >> 2);
			l4_proto = ip6h->nexthdr;
		} else {
			return MVPP2_TXD_L4_CSUM_NOT;
		}

		return mvpp2_txq_desc_csum(skb_network_offset(skb),
				skb->protocol, ip_hdr_len, l4_proto);
	}

	return MVPP2_TXD_L4_CSUM_NOT | MVPP2_TXD_IP_CSUM_DISABLE;
}

/* Main rx processing */
static int mvpp2_rx(struct mvpp2_port *port, struct napi_struct *napi,
		    int rx_todo, struct mvpp2_rx_queue *rxq)
{
	struct net_device *dev = port->dev;
	int rx_received;
	int rx_done = 0;
	u32 rcvd_pkts = 0;
	u32 rcvd_bytes = 0;

	/* Get number of received packets and clamp the to-do */
	rx_received = mvpp2_rxq_received(port, rxq->id);
	if (rx_todo > rx_received)
		rx_todo = rx_received;

	while (rx_done < rx_todo) {
		struct mvpp2_rx_desc *rx_desc = mvpp2_rxq_next_desc_get(rxq);
		struct mvpp2_bm_pool *bm_pool;
		struct sk_buff *skb;
		unsigned int frag_size;
		dma_addr_t dma_addr;
		phys_addr_t phys_addr;
		u32 rx_status;
		int pool, rx_bytes, err;
		void *data;

		rx_done++;
		rx_status = mvpp2_rxdesc_status_get(port, rx_desc);
		rx_bytes = mvpp2_rxdesc_size_get(port, rx_desc);
		rx_bytes -= MVPP2_MH_SIZE;
		dma_addr = mvpp2_rxdesc_dma_addr_get(port, rx_desc);
		phys_addr = mvpp2_rxdesc_cookie_get(port, rx_desc);
		data = (void *)phys_to_virt(phys_addr);

		pool = (rx_status & MVPP2_RXD_BM_POOL_ID_MASK) >>
			MVPP2_RXD_BM_POOL_ID_OFFS;
		bm_pool = &port->priv->bm_pools[pool];

		/* In case of an error, release the requested buffer pointer
		 * to the Buffer Manager. This request process is controlled
		 * by the hardware, and the information about the buffer is
		 * comprised by the RX descriptor.
		 */
		if (rx_status & MVPP2_RXD_ERR_SUMMARY) {
err_drop_frame:
			dev->stats.rx_errors++;
			mvpp2_rx_error(port, rx_desc);
			/* Return the buffer to the pool */
			mvpp2_bm_pool_put(port, pool, dma_addr, phys_addr);
			continue;
		}

		if (bm_pool->frag_size > PAGE_SIZE)
			frag_size = 0;
		else
			frag_size = bm_pool->frag_size;

		skb = build_skb(data, frag_size);
		if (!skb) {
			netdev_warn(port->dev, "skb build failed\n");
			goto err_drop_frame;
		}

		err = mvpp2_rx_refill(port, bm_pool, pool);
		if (err) {
			netdev_err(port->dev, "failed to refill BM pools\n");
			goto err_drop_frame;
		}

		dma_unmap_single(dev->dev.parent, dma_addr,
				 bm_pool->buf_size, DMA_FROM_DEVICE);

		rcvd_pkts++;
		rcvd_bytes += rx_bytes;

		skb_reserve(skb, MVPP2_MH_SIZE + NET_SKB_PAD);
		skb_put(skb, rx_bytes);
		skb->protocol = eth_type_trans(skb, dev);
		mvpp2_rx_csum(port, rx_status, skb);

		napi_gro_receive(napi, skb);
	}

	if (rcvd_pkts) {
		struct mvpp2_pcpu_stats *stats = this_cpu_ptr(port->stats);

		u64_stats_update_begin(&stats->syncp);
		stats->rx_packets += rcvd_pkts;
		stats->rx_bytes   += rcvd_bytes;
		u64_stats_update_end(&stats->syncp);
	}

	/* Update Rx queue management counters */
	wmb();
	mvpp2_rxq_status_update(port, rxq->id, rx_done, rx_done);

	return rx_todo;
}

static inline void
tx_desc_unmap_put(struct mvpp2_port *port, struct mvpp2_tx_queue *txq,
		  struct mvpp2_tx_desc *desc)
{
	struct mvpp2_txq_pcpu *txq_pcpu = this_cpu_ptr(txq->pcpu);

	dma_addr_t buf_dma_addr =
		mvpp2_txdesc_dma_addr_get(port, desc);
	size_t buf_sz =
		mvpp2_txdesc_size_get(port, desc);
	if (!IS_TSO_HEADER(txq_pcpu, buf_dma_addr))
		dma_unmap_single(port->dev->dev.parent, buf_dma_addr,
				 buf_sz, DMA_TO_DEVICE);
	mvpp2_txq_desc_put(txq);
}

/* Handle tx fragmentation processing */
static int mvpp2_tx_frag_process(struct mvpp2_port *port, struct sk_buff *skb,
				 struct mvpp2_tx_queue *aggr_txq,
				 struct mvpp2_tx_queue *txq)
{
	struct mvpp2_txq_pcpu *txq_pcpu = this_cpu_ptr(txq->pcpu);
	struct mvpp2_tx_desc *tx_desc;
	int i;
	dma_addr_t buf_dma_addr;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		void *addr = page_address(frag->page.p) + frag->page_offset;

		tx_desc = mvpp2_txq_next_desc_get(aggr_txq);
		mvpp2_txdesc_txq_set(port, tx_desc, txq->id);
		mvpp2_txdesc_size_set(port, tx_desc, frag->size);

		buf_dma_addr = dma_map_single(port->dev->dev.parent, addr,
					      frag->size, DMA_TO_DEVICE);
		if (dma_mapping_error(port->dev->dev.parent, buf_dma_addr)) {
			mvpp2_txq_desc_put(txq);
			goto cleanup;
		}

		mvpp2_txdesc_dma_addr_set(port, tx_desc, buf_dma_addr);

		if (i == (skb_shinfo(skb)->nr_frags - 1)) {
			/* Last descriptor */
			mvpp2_txdesc_cmd_set(port, tx_desc,
					     MVPP2_TXD_L_DESC);
			mvpp2_txq_inc_put(port, txq_pcpu, skb, tx_desc);
		} else {
			/* Descriptor in the middle: Not First, Not Last */
			mvpp2_txdesc_cmd_set(port, tx_desc, 0);
			mvpp2_txq_inc_put(port, txq_pcpu, NULL, tx_desc);
		}
	}

	return 0;
cleanup:
	/* Release all descriptors that were used to map fragments of
	 * this packet, as well as the corresponding DMA mappings
	 */
	for (i = i - 1; i >= 0; i--) {
		tx_desc = txq->descs + i;
		tx_desc_unmap_put(port, txq, tx_desc);
	}

	return -ENOMEM;
}

static inline void mvpp2_tso_put_hdr(struct sk_buff *skb,
				     struct net_device *dev,
				     struct mvpp2_tx_queue *txq,
				     struct mvpp2_tx_queue *aggr_txq,
				     struct mvpp2_txq_pcpu *txq_pcpu,
				     int hdr_sz)
{
	struct mvpp2_port *port = netdev_priv(dev);
	struct mvpp2_tx_desc *tx_desc = mvpp2_txq_next_desc_get(aggr_txq);
	dma_addr_t addr;

	mvpp2_txdesc_txq_set(port, tx_desc, txq->id);
	mvpp2_txdesc_size_set(port, tx_desc, hdr_sz);

	addr = txq_pcpu->tso_headers_dma +
	       txq_pcpu->txq_put_index * TSO_HEADER_SIZE;
	mvpp2_txdesc_dma_addr_set(port, tx_desc, addr);

	mvpp2_txdesc_cmd_set(port, tx_desc, mvpp2_skb_tx_csum(port, skb) |
					    MVPP2_TXD_F_DESC |
					    MVPP2_TXD_PADDING_DISABLE);
	mvpp2_txq_inc_put(port, txq_pcpu, NULL, tx_desc);
}

static inline int mvpp2_tso_put_data(struct sk_buff *skb,
				     struct net_device *dev, struct tso_t *tso,
				     struct mvpp2_tx_queue *txq,
				     struct mvpp2_tx_queue *aggr_txq,
				     struct mvpp2_txq_pcpu *txq_pcpu,
				     int sz, bool left, bool last)
{
	struct mvpp2_port *port = netdev_priv(dev);
	struct mvpp2_tx_desc *tx_desc = mvpp2_txq_next_desc_get(aggr_txq);
	dma_addr_t buf_dma_addr;

	mvpp2_txdesc_txq_set(port, tx_desc, txq->id);
	mvpp2_txdesc_size_set(port, tx_desc, sz);

	buf_dma_addr = dma_map_single(dev->dev.parent, tso->data, sz,
				      DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev->dev.parent, buf_dma_addr))) {
		mvpp2_txq_desc_put(txq);
		return -ENOMEM;
	}

	mvpp2_txdesc_dma_addr_set(port, tx_desc, buf_dma_addr);

	if (!left) {
		mvpp2_txdesc_cmd_set(port, tx_desc, MVPP2_TXD_L_DESC);
		if (last) {
			mvpp2_txq_inc_put(port, txq_pcpu, skb, tx_desc);
			return 0;
		}
	} else {
		mvpp2_txdesc_cmd_set(port, tx_desc, 0);
	}

	mvpp2_txq_inc_put(port, txq_pcpu, NULL, tx_desc);
	return 0;
}

static int mvpp2_tx_tso(struct sk_buff *skb, struct net_device *dev,
			struct mvpp2_tx_queue *txq,
			struct mvpp2_tx_queue *aggr_txq,
			struct mvpp2_txq_pcpu *txq_pcpu)
{
	struct mvpp2_port *port = netdev_priv(dev);
	struct tso_t tso;
	int hdr_sz = skb_transport_offset(skb) + tcp_hdrlen(skb);
	int i, len, descs = 0;

	/* Check number of available descriptors */
	if (mvpp2_aggr_desc_num_check(port->priv, aggr_txq,
				      tso_count_descs(skb)) ||
	    mvpp2_txq_reserved_desc_num_proc(port->priv, txq, txq_pcpu,
					     tso_count_descs(skb)))
		return 0;

	tso_start(skb, &tso);
	len = skb->len - hdr_sz;
	while (len > 0) {
		int left = min_t(int, skb_shinfo(skb)->gso_size, len);
		char *hdr = txq_pcpu->tso_headers +
			    txq_pcpu->txq_put_index * TSO_HEADER_SIZE;

		len -= left;
		descs++;

		tso_build_hdr(skb, hdr, &tso, left, len == 0);
		mvpp2_tso_put_hdr(skb, dev, txq, aggr_txq, txq_pcpu, hdr_sz);

		while (left > 0) {
			int sz = min_t(int, tso.size, left);
			left -= sz;
			descs++;

			if (mvpp2_tso_put_data(skb, dev, &tso, txq, aggr_txq,
					       txq_pcpu, sz, left, len == 0))
				goto release;
			tso_build_data(skb, &tso, sz);
		}
	}

	return descs;

release:
	for (i = descs - 1; i >= 0; i--) {
		struct mvpp2_tx_desc *tx_desc = txq->descs + i;
		tx_desc_unmap_put(port, txq, tx_desc);
	}
	return 0;
}

/* Main tx processing */
static int mvpp2_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct mvpp2_port *port = netdev_priv(dev);
	struct mvpp2_tx_queue *txq, *aggr_txq;
	struct mvpp2_txq_pcpu *txq_pcpu;
	struct mvpp2_tx_desc *tx_desc;
	dma_addr_t buf_dma_addr;
	int frags = 0;
	u16 txq_id;
	u32 tx_cmd;

	txq_id = skb_get_queue_mapping(skb);
	txq = port->txqs[txq_id];
	txq_pcpu = this_cpu_ptr(txq->pcpu);
	aggr_txq = &port->priv->aggr_txqs[smp_processor_id()];

	if (skb_is_gso(skb)) {
		frags = mvpp2_tx_tso(skb, dev, txq, aggr_txq, txq_pcpu);
		goto out;
	}
	frags = skb_shinfo(skb)->nr_frags + 1;

	/* Check number of available descriptors */
	if (mvpp2_aggr_desc_num_check(port->priv, aggr_txq, frags) ||
	    mvpp2_txq_reserved_desc_num_proc(port->priv, txq,
					     txq_pcpu, frags)) {
		frags = 0;
		goto out;
	}

	/* Get a descriptor for the first part of the packet */
	tx_desc = mvpp2_txq_next_desc_get(aggr_txq);
	mvpp2_txdesc_txq_set(port, tx_desc, txq->id);
	mvpp2_txdesc_size_set(port, tx_desc, skb_headlen(skb));

	buf_dma_addr = dma_map_single(dev->dev.parent, skb->data,
				      skb_headlen(skb), DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev->dev.parent, buf_dma_addr))) {
		mvpp2_txq_desc_put(txq);
		frags = 0;
		goto out;
	}

	mvpp2_txdesc_dma_addr_set(port, tx_desc, buf_dma_addr);

	tx_cmd = mvpp2_skb_tx_csum(port, skb);

	if (frags == 1) {
		/* First and Last descriptor */
		tx_cmd |= MVPP2_TXD_F_DESC | MVPP2_TXD_L_DESC;
		mvpp2_txdesc_cmd_set(port, tx_desc, tx_cmd);
		mvpp2_txq_inc_put(port, txq_pcpu, skb, tx_desc);
	} else {
		/* First but not Last */
		tx_cmd |= MVPP2_TXD_F_DESC | MVPP2_TXD_PADDING_DISABLE;
		mvpp2_txdesc_cmd_set(port, tx_desc, tx_cmd);
		mvpp2_txq_inc_put(port, txq_pcpu, NULL, tx_desc);

		/* Continue with other skb fragments */
		if (mvpp2_tx_frag_process(port, skb, aggr_txq, txq)) {
			tx_desc_unmap_put(port, txq, tx_desc);
			frags = 0;
		}
	}

out:
	if (frags > 0) {
		struct mvpp2_pcpu_stats *stats = this_cpu_ptr(port->stats);
		struct netdev_queue *nq = netdev_get_tx_queue(dev, txq_id);

		txq_pcpu->reserved_num -= frags;
		txq_pcpu->count += frags;
		aggr_txq->count += frags;

		/* Enable transmit */
		wmb();
		mvpp2_aggr_txq_pend_desc_add(port, frags);

		if (txq_pcpu->count >= txq_pcpu->stop_threshold)
			netif_tx_stop_queue(nq);

		u64_stats_update_begin(&stats->syncp);
		stats->tx_packets++;
		stats->tx_bytes += skb->len;
		u64_stats_update_end(&stats->syncp);
	} else {
		dev->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
	}

	/* Finalize TX processing */
	if (!port->has_tx_irqs && txq_pcpu->count >= txq->done_pkts_coal)
		mvpp2_txq_done(port, txq, txq_pcpu);

	/* Set the timer in case not all frags were processed */
	if (!port->has_tx_irqs && txq_pcpu->count <= frags &&
	    txq_pcpu->count > 0) {
		struct mvpp2_port_pcpu *port_pcpu = this_cpu_ptr(port->pcpu);

		mvpp2_timer_set(port_pcpu);
	}

	return NETDEV_TX_OK;
}

static inline void mvpp2_cause_error(struct net_device *dev, int cause)
{
	if (cause & MVPP2_CAUSE_FCS_ERR_MASK)
		netdev_err(dev, "FCS error\n");
	if (cause & MVPP2_CAUSE_RX_FIFO_OVERRUN_MASK)
		netdev_err(dev, "rx fifo overrun error\n");
	if (cause & MVPP2_CAUSE_TX_FIFO_UNDERRUN_MASK)
		netdev_err(dev, "tx fifo underrun error\n");
}

static int mvpp2_poll(struct napi_struct *napi, int budget)
{
	u32 cause_rx_tx, cause_rx, cause_tx, cause_misc;
	int rx_done = 0;
	struct mvpp2_port *port = netdev_priv(napi->dev);
	struct mvpp2_queue_vector *qv;
	int cpu = smp_processor_id();

	qv = container_of(napi, struct mvpp2_queue_vector, napi);

	/* Rx/Tx cause register
	 *
	 * Bits 0-15: each bit indicates received packets on the Rx queue
	 * (bit 0 is for Rx queue 0).
	 *
	 * Bits 16-23: each bit indicates transmitted packets on the Tx queue
	 * (bit 16 is for Tx queue 0).
	 *
	 * Each CPU has its own Rx/Tx cause register
	 */
	cause_rx_tx = mvpp2_percpu_read_relaxed(port->priv, qv->sw_thread_id,
						MVPP2_ISR_RX_TX_CAUSE_REG(port->id));

	cause_misc = cause_rx_tx & MVPP2_CAUSE_MISC_SUM_MASK;
	if (cause_misc) {
		mvpp2_cause_error(port->dev, cause_misc);

		/* Clear the cause register */
		mvpp2_write(port->priv, MVPP2_ISR_MISC_CAUSE_REG, 0);
		mvpp2_percpu_write(port->priv, cpu,
				   MVPP2_ISR_RX_TX_CAUSE_REG(port->id),
				   cause_rx_tx & ~MVPP2_CAUSE_MISC_SUM_MASK);
	}

	if (port->has_tx_irqs) {
		cause_tx = cause_rx_tx & MVPP2_CAUSE_TXQ_OCCUP_DESC_ALL_MASK;
		if (cause_tx) {
			cause_tx >>= MVPP2_CAUSE_TXQ_OCCUP_DESC_ALL_OFFSET;
			mvpp2_tx_done(port, cause_tx, qv->sw_thread_id);
		}
	}

	/* Process RX packets */
	cause_rx = cause_rx_tx & MVPP2_CAUSE_RXQ_OCCUP_DESC_ALL_MASK;
	cause_rx <<= qv->first_rxq;
	cause_rx |= qv->pending_cause_rx;
	while (cause_rx && budget > 0) {
		int count;
		struct mvpp2_rx_queue *rxq;

		rxq = mvpp2_get_rx_queue(port, cause_rx);
		if (!rxq)
			break;

		count = mvpp2_rx(port, napi, budget, rxq);
		rx_done += count;
		budget -= count;
		if (budget > 0) {
			/* Clear the bit associated to this Rx queue
			 * so that next iteration will continue from
			 * the next Rx queue.
			 */
			cause_rx &= ~(1 << rxq->logic_rxq);
		}
	}

	if (budget > 0) {
		cause_rx = 0;
		napi_complete_done(napi, rx_done);

		mvpp2_qvec_interrupt_enable(qv);
	}
	qv->pending_cause_rx = cause_rx;
	return rx_done;
}

static void mvpp22_mode_reconfigure(struct mvpp2_port *port)
{
	u32 ctrl3;

	/* comphy reconfiguration */
	mvpp22_comphy_init(port);

	/* gop reconfiguration */
	mvpp22_gop_init(port);

	/* Only GOP port 0 has an XLG MAC */
	if (port->gop_id == 0) {
		ctrl3 = readl(port->base + MVPP22_XLG_CTRL3_REG);
		ctrl3 &= ~MVPP22_XLG_CTRL3_MACMODESELECT_MASK;

		if (port->phy_interface == PHY_INTERFACE_MODE_XAUI ||
		    port->phy_interface == PHY_INTERFACE_MODE_10GKR)
			ctrl3 |= MVPP22_XLG_CTRL3_MACMODESELECT_10G;
		else
			ctrl3 |= MVPP22_XLG_CTRL3_MACMODESELECT_GMAC;

		writel(ctrl3, port->base + MVPP22_XLG_CTRL3_REG);
	}

	if (port->gop_id == 0 &&
	    (port->phy_interface == PHY_INTERFACE_MODE_XAUI ||
	     port->phy_interface == PHY_INTERFACE_MODE_10GKR))
		mvpp2_xlg_max_rx_size_set(port);
	else
		mvpp2_gmac_max_rx_size_set(port);
}

/* Set hw internals when starting port */
static void mvpp2_start_dev(struct mvpp2_port *port)
{
	int i;

	mvpp2_txp_max_tx_size_set(port);

	for (i = 0; i < port->nqvecs; i++)
		napi_enable(&port->qvecs[i].napi);

	/* Enable interrupts on all CPUs */
	mvpp2_interrupts_enable(port);

	if (port->priv->hw_version == MVPP22)
		mvpp22_mode_reconfigure(port);

	if (port->phylink) {
		netif_carrier_off(port->dev);
		phylink_start(port->phylink);
	} else {
		/* Phylink isn't used as of now for ACPI, so the MAC has to be
		 * configured manually when the interface is started. This will
		 * be removed as soon as the phylink ACPI support lands in.
		 */
		struct phylink_link_state state = {
			.interface = port->phy_interface,
		};
		mvpp2_mac_config(port->dev, MLO_AN_INBAND, &state);
		mvpp2_mac_link_up(port->dev, MLO_AN_INBAND, port->phy_interface,
				  NULL);
	}

	netif_tx_start_all_queues(port->dev);
}

/* Set hw internals when stopping port */
static void mvpp2_stop_dev(struct mvpp2_port *port)
{
	int i;

	/* Disable interrupts on all CPUs */
	mvpp2_interrupts_disable(port);

	for (i = 0; i < port->nqvecs; i++)
		napi_disable(&port->qvecs[i].napi);

	if (port->phylink)
		phylink_stop(port->phylink);
	phy_power_off(port->comphy);
}

static int mvpp2_check_ringparam_valid(struct net_device *dev,
				       struct ethtool_ringparam *ring)
{
	u16 new_rx_pending = ring->rx_pending;
	u16 new_tx_pending = ring->tx_pending;

	if (ring->rx_pending == 0 || ring->tx_pending == 0)
		return -EINVAL;

	if (ring->rx_pending > MVPP2_MAX_RXD_MAX)
		new_rx_pending = MVPP2_MAX_RXD_MAX;
	else if (!IS_ALIGNED(ring->rx_pending, 16))
		new_rx_pending = ALIGN(ring->rx_pending, 16);

	if (ring->tx_pending > MVPP2_MAX_TXD_MAX)
		new_tx_pending = MVPP2_MAX_TXD_MAX;
	else if (!IS_ALIGNED(ring->tx_pending, 32))
		new_tx_pending = ALIGN(ring->tx_pending, 32);

	/* The Tx ring size cannot be smaller than the minimum number of
	 * descriptors needed for TSO.
	 */
	if (new_tx_pending < MVPP2_MAX_SKB_DESCS)
		new_tx_pending = ALIGN(MVPP2_MAX_SKB_DESCS, 32);

	if (ring->rx_pending != new_rx_pending) {
		netdev_info(dev, "illegal Rx ring size value %d, round to %d\n",
			    ring->rx_pending, new_rx_pending);
		ring->rx_pending = new_rx_pending;
	}

	if (ring->tx_pending != new_tx_pending) {
		netdev_info(dev, "illegal Tx ring size value %d, round to %d\n",
			    ring->tx_pending, new_tx_pending);
		ring->tx_pending = new_tx_pending;
	}

	return 0;
}

static void mvpp21_get_mac_address(struct mvpp2_port *port, unsigned char *addr)
{
	u32 mac_addr_l, mac_addr_m, mac_addr_h;

	mac_addr_l = readl(port->base + MVPP2_GMAC_CTRL_1_REG);
	mac_addr_m = readl(port->priv->lms_base + MVPP2_SRC_ADDR_MIDDLE);
	mac_addr_h = readl(port->priv->lms_base + MVPP2_SRC_ADDR_HIGH);
	addr[0] = (mac_addr_h >> 24) & 0xFF;
	addr[1] = (mac_addr_h >> 16) & 0xFF;
	addr[2] = (mac_addr_h >> 8) & 0xFF;
	addr[3] = mac_addr_h & 0xFF;
	addr[4] = mac_addr_m & 0xFF;
	addr[5] = (mac_addr_l >> MVPP2_GMAC_SA_LOW_OFFS) & 0xFF;
}

static int mvpp2_irqs_init(struct mvpp2_port *port)
{
	int err, i;

	for (i = 0; i < port->nqvecs; i++) {
		struct mvpp2_queue_vector *qv = port->qvecs + i;

		if (qv->type == MVPP2_QUEUE_VECTOR_PRIVATE)
			irq_set_status_flags(qv->irq, IRQ_NO_BALANCING);

		err = request_irq(qv->irq, mvpp2_isr, 0, port->dev->name, qv);
		if (err)
			goto err;

		if (qv->type == MVPP2_QUEUE_VECTOR_PRIVATE)
			irq_set_affinity_hint(qv->irq,
					      cpumask_of(qv->sw_thread_id));
	}

	return 0;
err:
	for (i = 0; i < port->nqvecs; i++) {
		struct mvpp2_queue_vector *qv = port->qvecs + i;

		irq_set_affinity_hint(qv->irq, NULL);
		free_irq(qv->irq, qv);
	}

	return err;
}

static void mvpp2_irqs_deinit(struct mvpp2_port *port)
{
	int i;

	for (i = 0; i < port->nqvecs; i++) {
		struct mvpp2_queue_vector *qv = port->qvecs + i;

		irq_set_affinity_hint(qv->irq, NULL);
		irq_clear_status_flags(qv->irq, IRQ_NO_BALANCING);
		free_irq(qv->irq, qv);
	}
}

static bool mvpp22_rss_is_supported(void)
{
	return queue_mode == MVPP2_QDIST_MULTI_MODE;
}

static int mvpp2_open(struct net_device *dev)
{
	struct mvpp2_port *port = netdev_priv(dev);
	struct mvpp2 *priv = port->priv;
	unsigned char mac_bcast[ETH_ALEN] = {
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	bool valid = false;
	int err;

	err = mvpp2_prs_mac_da_accept(port, mac_bcast, true);
	if (err) {
		netdev_err(dev, "mvpp2_prs_mac_da_accept BC failed\n");
		return err;
	}
	err = mvpp2_prs_mac_da_accept(port, dev->dev_addr, true);
	if (err) {
		netdev_err(dev, "mvpp2_prs_mac_da_accept own addr failed\n");
		return err;
	}
	err = mvpp2_prs_tag_mode_set(port->priv, port->id, MVPP2_TAG_TYPE_MH);
	if (err) {
		netdev_err(dev, "mvpp2_prs_tag_mode_set failed\n");
		return err;
	}
	err = mvpp2_prs_def_flow(port);
	if (err) {
		netdev_err(dev, "mvpp2_prs_def_flow failed\n");
		return err;
	}

	/* Allocate the Rx/Tx queues */
	err = mvpp2_setup_rxqs(port);
	if (err) {
		netdev_err(port->dev, "cannot allocate Rx queues\n");
		return err;
	}

	err = mvpp2_setup_txqs(port);
	if (err) {
		netdev_err(port->dev, "cannot allocate Tx queues\n");
		goto err_cleanup_rxqs;
	}

	err = mvpp2_irqs_init(port);
	if (err) {
		netdev_err(port->dev, "cannot init IRQs\n");
		goto err_cleanup_txqs;
	}

	/* Phylink isn't supported yet in ACPI mode */
	if (port->of_node) {
		err = phylink_of_phy_connect(port->phylink, port->of_node, 0);
		if (err) {
			netdev_err(port->dev, "could not attach PHY (%d)\n",
				   err);
			goto err_free_irq;
		}

		valid = true;
	}

	if (priv->hw_version == MVPP22 && port->link_irq && !port->phylink) {
		err = request_irq(port->link_irq, mvpp2_link_status_isr, 0,
				  dev->name, port);
		if (err) {
			netdev_err(port->dev, "cannot request link IRQ %d\n",
				   port->link_irq);
			goto err_free_irq;
		}

		mvpp22_gop_setup_irq(port);

		/* In default link is down */
		netif_carrier_off(port->dev);

		valid = true;
	} else {
		port->link_irq = 0;
	}

	if (!valid) {
		netdev_err(port->dev,
			   "invalid configuration: no dt or link IRQ");
		goto err_free_irq;
	}

	/* Unmask interrupts on all CPUs */
	on_each_cpu(mvpp2_interrupts_unmask, port, 1);
	mvpp2_shared_interrupt_mask_unmask(port, false);

	mvpp2_start_dev(port);

	/* Start hardware statistics gathering */
	queue_delayed_work(priv->stats_queue, &port->stats_work,
			   MVPP2_MIB_COUNTERS_STATS_DELAY);

	return 0;

err_free_irq:
	mvpp2_irqs_deinit(port);
err_cleanup_txqs:
	mvpp2_cleanup_txqs(port);
err_cleanup_rxqs:
	mvpp2_cleanup_rxqs(port);
	return err;
}

static int mvpp2_stop(struct net_device *dev)
{
	struct mvpp2_port *port = netdev_priv(dev);
	struct mvpp2_port_pcpu *port_pcpu;
	int cpu;

	mvpp2_stop_dev(port);

	/* Mask interrupts on all CPUs */
	on_each_cpu(mvpp2_interrupts_mask, port, 1);
	mvpp2_shared_interrupt_mask_unmask(port, true);

	if (port->phylink)
		phylink_disconnect_phy(port->phylink);
	if (port->link_irq)
		free_irq(port->link_irq, port);

	mvpp2_irqs_deinit(port);
	if (!port->has_tx_irqs) {
		for_each_present_cpu(cpu) {
			port_pcpu = per_cpu_ptr(port->pcpu, cpu);

			hrtimer_cancel(&port_pcpu->tx_done_timer);
			port_pcpu->timer_scheduled = false;
			tasklet_kill(&port_pcpu->tx_done_tasklet);
		}
	}
	mvpp2_cleanup_rxqs(port);
	mvpp2_cleanup_txqs(port);

	cancel_delayed_work_sync(&port->stats_work);

	return 0;
}

static int mvpp2_prs_mac_da_accept_list(struct mvpp2_port *port,
					struct netdev_hw_addr_list *list)
{
	struct netdev_hw_addr *ha;
	int ret;

	netdev_hw_addr_list_for_each(ha, list) {
		ret = mvpp2_prs_mac_da_accept(port, ha->addr, true);
		if (ret)
			return ret;
	}

	return 0;
}

static void mvpp2_set_rx_promisc(struct mvpp2_port *port, bool enable)
{
	if (!enable && (port->dev->features & NETIF_F_HW_VLAN_CTAG_FILTER))
		mvpp2_prs_vid_enable_filtering(port);
	else
		mvpp2_prs_vid_disable_filtering(port);

	mvpp2_prs_mac_promisc_set(port->priv, port->id,
				  MVPP2_PRS_L2_UNI_CAST, enable);

	mvpp2_prs_mac_promisc_set(port->priv, port->id,
				  MVPP2_PRS_L2_MULTI_CAST, enable);
}

static void mvpp2_set_rx_mode(struct net_device *dev)
{
	struct mvpp2_port *port = netdev_priv(dev);

	/* Clear the whole UC and MC list */
	mvpp2_prs_mac_del_all(port);

	if (dev->flags & IFF_PROMISC) {
		mvpp2_set_rx_promisc(port, true);
		return;
	}

	mvpp2_set_rx_promisc(port, false);

	if (netdev_uc_count(dev) > MVPP2_PRS_MAC_UC_FILT_MAX ||
	    mvpp2_prs_mac_da_accept_list(port, &dev->uc))
		mvpp2_prs_mac_promisc_set(port->priv, port->id,
					  MVPP2_PRS_L2_UNI_CAST, true);

	if (dev->flags & IFF_ALLMULTI) {
		mvpp2_prs_mac_promisc_set(port->priv, port->id,
					  MVPP2_PRS_L2_MULTI_CAST, true);
		return;
	}

	if (netdev_mc_count(dev) > MVPP2_PRS_MAC_MC_FILT_MAX ||
	    mvpp2_prs_mac_da_accept_list(port, &dev->mc))
		mvpp2_prs_mac_promisc_set(port->priv, port->id,
					  MVPP2_PRS_L2_MULTI_CAST, true);
}

static int mvpp2_set_mac_address(struct net_device *dev, void *p)
{
	const struct sockaddr *addr = p;
	int err;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	err = mvpp2_prs_update_mac_da(dev, addr->sa_data);
	if (err) {
		/* Reconfigure parser accept the original MAC address */
		mvpp2_prs_update_mac_da(dev, dev->dev_addr);
		netdev_err(dev, "failed to change MAC address\n");
	}
	return err;
}

static int mvpp2_change_mtu(struct net_device *dev, int mtu)
{
	struct mvpp2_port *port = netdev_priv(dev);
	int err;

	if (!IS_ALIGNED(MVPP2_RX_PKT_SIZE(mtu), 8)) {
		netdev_info(dev, "illegal MTU value %d, round to %d\n", mtu,
			    ALIGN(MVPP2_RX_PKT_SIZE(mtu), 8));
		mtu = ALIGN(MVPP2_RX_PKT_SIZE(mtu), 8);
	}

	if (!netif_running(dev)) {
		err = mvpp2_bm_update_mtu(dev, mtu);
		if (!err) {
			port->pkt_size =  MVPP2_RX_PKT_SIZE(mtu);
			return 0;
		}

		/* Reconfigure BM to the original MTU */
		err = mvpp2_bm_update_mtu(dev, dev->mtu);
		if (err)
			goto log_error;
	}

	mvpp2_stop_dev(port);

	err = mvpp2_bm_update_mtu(dev, mtu);
	if (!err) {
		port->pkt_size =  MVPP2_RX_PKT_SIZE(mtu);
		goto out_start;
	}

	/* Reconfigure BM to the original MTU */
	err = mvpp2_bm_update_mtu(dev, dev->mtu);
	if (err)
		goto log_error;

out_start:
	mvpp2_start_dev(port);
	mvpp2_egress_enable(port);
	mvpp2_ingress_enable(port);

	return 0;
log_error:
	netdev_err(dev, "failed to change MTU\n");
	return err;
}

static void
mvpp2_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct mvpp2_port *port = netdev_priv(dev);
	unsigned int start;
	int cpu;

	for_each_possible_cpu(cpu) {
		struct mvpp2_pcpu_stats *cpu_stats;
		u64 rx_packets;
		u64 rx_bytes;
		u64 tx_packets;
		u64 tx_bytes;

		cpu_stats = per_cpu_ptr(port->stats, cpu);
		do {
			start = u64_stats_fetch_begin_irq(&cpu_stats->syncp);
			rx_packets = cpu_stats->rx_packets;
			rx_bytes   = cpu_stats->rx_bytes;
			tx_packets = cpu_stats->tx_packets;
			tx_bytes   = cpu_stats->tx_bytes;
		} while (u64_stats_fetch_retry_irq(&cpu_stats->syncp, start));

		stats->rx_packets += rx_packets;
		stats->rx_bytes   += rx_bytes;
		stats->tx_packets += tx_packets;
		stats->tx_bytes   += tx_bytes;
	}

	stats->rx_errors	= dev->stats.rx_errors;
	stats->rx_dropped	= dev->stats.rx_dropped;
	stats->tx_dropped	= dev->stats.tx_dropped;
}

static int mvpp2_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mvpp2_port *port = netdev_priv(dev);

	if (!port->phylink)
		return -ENOTSUPP;

	return phylink_mii_ioctl(port->phylink, ifr, cmd);
}

static int mvpp2_vlan_rx_add_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct mvpp2_port *port = netdev_priv(dev);
	int ret;

	ret = mvpp2_prs_vid_entry_add(port, vid);
	if (ret)
		netdev_err(dev, "rx-vlan-filter offloading cannot accept more than %d VIDs per port\n",
			   MVPP2_PRS_VLAN_FILT_MAX - 1);
	return ret;
}

static int mvpp2_vlan_rx_kill_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct mvpp2_port *port = netdev_priv(dev);

	mvpp2_prs_vid_entry_remove(port, vid);
	return 0;
}

static int mvpp2_set_features(struct net_device *dev,
			      netdev_features_t features)
{
	netdev_features_t changed = dev->features ^ features;
	struct mvpp2_port *port = netdev_priv(dev);

	if (changed & NETIF_F_HW_VLAN_CTAG_FILTER) {
		if (features & NETIF_F_HW_VLAN_CTAG_FILTER) {
			mvpp2_prs_vid_enable_filtering(port);
		} else {
			/* Invalidate all registered VID filters for this
			 * port
			 */
			mvpp2_prs_vid_remove_all(port);

			mvpp2_prs_vid_disable_filtering(port);
		}
	}

	if (changed & NETIF_F_RXHASH) {
		if (features & NETIF_F_RXHASH)
			mvpp22_rss_enable(port);
		else
			mvpp22_rss_disable(port);
	}

	return 0;
}

/* Ethtool methods */

static int mvpp2_ethtool_nway_reset(struct net_device *dev)
{
	struct mvpp2_port *port = netdev_priv(dev);

	if (!port->phylink)
		return -ENOTSUPP;

	return phylink_ethtool_nway_reset(port->phylink);
}

/* Set interrupt coalescing for ethtools */
static int mvpp2_ethtool_set_coalesce(struct net_device *dev,
				      struct ethtool_coalesce *c)
{
	struct mvpp2_port *port = netdev_priv(dev);
	int queue;

	for (queue = 0; queue < port->nrxqs; queue++) {
		struct mvpp2_rx_queue *rxq = port->rxqs[queue];

		rxq->time_coal = c->rx_coalesce_usecs;
		rxq->pkts_coal = c->rx_max_coalesced_frames;
		mvpp2_rx_pkts_coal_set(port, rxq);
		mvpp2_rx_time_coal_set(port, rxq);
	}

	if (port->has_tx_irqs) {
		port->tx_time_coal = c->tx_coalesce_usecs;
		mvpp2_tx_time_coal_set(port);
	}

	for (queue = 0; queue < port->ntxqs; queue++) {
		struct mvpp2_tx_queue *txq = port->txqs[queue];

		txq->done_pkts_coal = c->tx_max_coalesced_frames;

		if (port->has_tx_irqs)
			mvpp2_tx_pkts_coal_set(port, txq);
	}

	return 0;
}

/* get coalescing for ethtools */
static int mvpp2_ethtool_get_coalesce(struct net_device *dev,
				      struct ethtool_coalesce *c)
{
	struct mvpp2_port *port = netdev_priv(dev);

	c->rx_coalesce_usecs       = port->rxqs[0]->time_coal;
	c->rx_max_coalesced_frames = port->rxqs[0]->pkts_coal;
	c->tx_max_coalesced_frames = port->txqs[0]->done_pkts_coal;
	c->tx_coalesce_usecs       = port->tx_time_coal;
	return 0;
}

static void mvpp2_ethtool_get_drvinfo(struct net_device *dev,
				      struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, MVPP2_DRIVER_NAME,
		sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, MVPP2_DRIVER_VERSION,
		sizeof(drvinfo->version));
	strlcpy(drvinfo->bus_info, dev_name(&dev->dev),
		sizeof(drvinfo->bus_info));
}

static void mvpp2_ethtool_get_ringparam(struct net_device *dev,
					struct ethtool_ringparam *ring)
{
	struct mvpp2_port *port = netdev_priv(dev);

	ring->rx_max_pending = MVPP2_MAX_RXD_MAX;
	ring->tx_max_pending = MVPP2_MAX_TXD_MAX;
	ring->rx_pending = port->rx_ring_size;
	ring->tx_pending = port->tx_ring_size;
}

static int mvpp2_ethtool_set_ringparam(struct net_device *dev,
				       struct ethtool_ringparam *ring)
{
	struct mvpp2_port *port = netdev_priv(dev);
	u16 prev_rx_ring_size = port->rx_ring_size;
	u16 prev_tx_ring_size = port->tx_ring_size;
	int err;

	err = mvpp2_check_ringparam_valid(dev, ring);
	if (err)
		return err;

	if (!netif_running(dev)) {
		port->rx_ring_size = ring->rx_pending;
		port->tx_ring_size = ring->tx_pending;
		return 0;
	}

	/* The interface is running, so we have to force a
	 * reallocation of the queues
	 */
	mvpp2_stop_dev(port);
	mvpp2_cleanup_rxqs(port);
	mvpp2_cleanup_txqs(port);

	port->rx_ring_size = ring->rx_pending;
	port->tx_ring_size = ring->tx_pending;

	err = mvpp2_setup_rxqs(port);
	if (err) {
		/* Reallocate Rx queues with the original ring size */
		port->rx_ring_size = prev_rx_ring_size;
		ring->rx_pending = prev_rx_ring_size;
		err = mvpp2_setup_rxqs(port);
		if (err)
			goto err_out;
	}
	err = mvpp2_setup_txqs(port);
	if (err) {
		/* Reallocate Tx queues with the original ring size */
		port->tx_ring_size = prev_tx_ring_size;
		ring->tx_pending = prev_tx_ring_size;
		err = mvpp2_setup_txqs(port);
		if (err)
			goto err_clean_rxqs;
	}

	mvpp2_start_dev(port);
	mvpp2_egress_enable(port);
	mvpp2_ingress_enable(port);

	return 0;

err_clean_rxqs:
	mvpp2_cleanup_rxqs(port);
err_out:
	netdev_err(dev, "failed to change ring parameters");
	return err;
}

static void mvpp2_ethtool_get_pause_param(struct net_device *dev,
					  struct ethtool_pauseparam *pause)
{
	struct mvpp2_port *port = netdev_priv(dev);

	if (!port->phylink)
		return;

	phylink_ethtool_get_pauseparam(port->phylink, pause);
}

static int mvpp2_ethtool_set_pause_param(struct net_device *dev,
					 struct ethtool_pauseparam *pause)
{
	struct mvpp2_port *port = netdev_priv(dev);

	if (!port->phylink)
		return -ENOTSUPP;

	return phylink_ethtool_set_pauseparam(port->phylink, pause);
}

static int mvpp2_ethtool_get_link_ksettings(struct net_device *dev,
					    struct ethtool_link_ksettings *cmd)
{
	struct mvpp2_port *port = netdev_priv(dev);

	if (!port->phylink)
		return -ENOTSUPP;

	return phylink_ethtool_ksettings_get(port->phylink, cmd);
}

static int mvpp2_ethtool_set_link_ksettings(struct net_device *dev,
					    const struct ethtool_link_ksettings *cmd)
{
	struct mvpp2_port *port = netdev_priv(dev);

	if (!port->phylink)
		return -ENOTSUPP;

	return phylink_ethtool_ksettings_set(port->phylink, cmd);
}

static int mvpp2_ethtool_get_rxnfc(struct net_device *dev,
				   struct ethtool_rxnfc *info, u32 *rules)
{
	struct mvpp2_port *port = netdev_priv(dev);
	int ret = 0;

	if (!mvpp22_rss_is_supported())
		return -EOPNOTSUPP;

	switch (info->cmd) {
	case ETHTOOL_GRXFH:
		ret = mvpp2_ethtool_rxfh_get(port, info);
		break;
	case ETHTOOL_GRXRINGS:
		info->data = port->nrxqs;
		break;
	default:
		return -ENOTSUPP;
	}

	return ret;
}

static int mvpp2_ethtool_set_rxnfc(struct net_device *dev,
				   struct ethtool_rxnfc *info)
{
	struct mvpp2_port *port = netdev_priv(dev);
	int ret = 0;

	if (!mvpp22_rss_is_supported())
		return -EOPNOTSUPP;

	switch (info->cmd) {
	case ETHTOOL_SRXFH:
		ret = mvpp2_ethtool_rxfh_set(port, info);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return ret;
}

static u32 mvpp2_ethtool_get_rxfh_indir_size(struct net_device *dev)
{
	return mvpp22_rss_is_supported() ? MVPP22_RSS_TABLE_ENTRIES : 0;
}

static int mvpp2_ethtool_get_rxfh(struct net_device *dev, u32 *indir, u8 *key,
				  u8 *hfunc)
{
	struct mvpp2_port *port = netdev_priv(dev);

	if (!mvpp22_rss_is_supported())
		return -EOPNOTSUPP;

	if (indir)
		memcpy(indir, port->indir,
		       ARRAY_SIZE(port->indir) * sizeof(port->indir[0]));

	if (hfunc)
		*hfunc = ETH_RSS_HASH_CRC32;

	return 0;
}

static int mvpp2_ethtool_set_rxfh(struct net_device *dev, const u32 *indir,
				  const u8 *key, const u8 hfunc)
{
	struct mvpp2_port *port = netdev_priv(dev);

	if (!mvpp22_rss_is_supported())
		return -EOPNOTSUPP;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_CRC32)
		return -EOPNOTSUPP;

	if (key)
		return -EOPNOTSUPP;

	if (indir) {
		memcpy(port->indir, indir,
		       ARRAY_SIZE(port->indir) * sizeof(port->indir[0]));
		mvpp22_rss_fill_table(port, port->id);
	}

	return 0;
}

/* Device ops */

static const struct net_device_ops mvpp2_netdev_ops = {
	.ndo_open		= mvpp2_open,
	.ndo_stop		= mvpp2_stop,
	.ndo_start_xmit		= mvpp2_tx,
	.ndo_set_rx_mode	= mvpp2_set_rx_mode,
	.ndo_set_mac_address	= mvpp2_set_mac_address,
	.ndo_change_mtu		= mvpp2_change_mtu,
	.ndo_get_stats64	= mvpp2_get_stats64,
	.ndo_do_ioctl		= mvpp2_ioctl,
	.ndo_vlan_rx_add_vid	= mvpp2_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= mvpp2_vlan_rx_kill_vid,
	.ndo_set_features	= mvpp2_set_features,
};

static const struct ethtool_ops mvpp2_eth_tool_ops = {
	.nway_reset		= mvpp2_ethtool_nway_reset,
	.get_link		= ethtool_op_get_link,
	.set_coalesce		= mvpp2_ethtool_set_coalesce,
	.get_coalesce		= mvpp2_ethtool_get_coalesce,
	.get_drvinfo		= mvpp2_ethtool_get_drvinfo,
	.get_ringparam		= mvpp2_ethtool_get_ringparam,
	.set_ringparam		= mvpp2_ethtool_set_ringparam,
	.get_strings		= mvpp2_ethtool_get_strings,
	.get_ethtool_stats	= mvpp2_ethtool_get_stats,
	.get_sset_count		= mvpp2_ethtool_get_sset_count,
	.get_pauseparam		= mvpp2_ethtool_get_pause_param,
	.set_pauseparam		= mvpp2_ethtool_set_pause_param,
	.get_link_ksettings	= mvpp2_ethtool_get_link_ksettings,
	.set_link_ksettings	= mvpp2_ethtool_set_link_ksettings,
	.get_rxnfc		= mvpp2_ethtool_get_rxnfc,
	.set_rxnfc		= mvpp2_ethtool_set_rxnfc,
	.get_rxfh_indir_size	= mvpp2_ethtool_get_rxfh_indir_size,
	.get_rxfh		= mvpp2_ethtool_get_rxfh,
	.set_rxfh		= mvpp2_ethtool_set_rxfh,

};

/* Used for PPv2.1, or PPv2.2 with the old Device Tree binding that
 * had a single IRQ defined per-port.
 */
static int mvpp2_simple_queue_vectors_init(struct mvpp2_port *port,
					   struct device_node *port_node)
{
	struct mvpp2_queue_vector *v = &port->qvecs[0];

	v->first_rxq = 0;
	v->nrxqs = port->nrxqs;
	v->type = MVPP2_QUEUE_VECTOR_SHARED;
	v->sw_thread_id = 0;
	v->sw_thread_mask = *cpumask_bits(cpu_online_mask);
	v->port = port;
	v->irq = irq_of_parse_and_map(port_node, 0);
	if (v->irq <= 0)
		return -EINVAL;
	netif_napi_add(port->dev, &v->napi, mvpp2_poll,
		       NAPI_POLL_WEIGHT);

	port->nqvecs = 1;

	return 0;
}

static int mvpp2_multi_queue_vectors_init(struct mvpp2_port *port,
					  struct device_node *port_node)
{
	struct mvpp2_queue_vector *v;
	int i, ret;

	port->nqvecs = num_possible_cpus();
	if (queue_mode == MVPP2_QDIST_SINGLE_MODE)
		port->nqvecs += 1;

	for (i = 0; i < port->nqvecs; i++) {
		char irqname[16];

		v = port->qvecs + i;

		v->port = port;
		v->type = MVPP2_QUEUE_VECTOR_PRIVATE;
		v->sw_thread_id = i;
		v->sw_thread_mask = BIT(i);

		snprintf(irqname, sizeof(irqname), "tx-cpu%d", i);

		if (queue_mode == MVPP2_QDIST_MULTI_MODE) {
			v->first_rxq = i * MVPP2_DEFAULT_RXQ;
			v->nrxqs = MVPP2_DEFAULT_RXQ;
		} else if (queue_mode == MVPP2_QDIST_SINGLE_MODE &&
			   i == (port->nqvecs - 1)) {
			v->first_rxq = 0;
			v->nrxqs = port->nrxqs;
			v->type = MVPP2_QUEUE_VECTOR_SHARED;
			strncpy(irqname, "rx-shared", sizeof(irqname));
		}

		if (port_node)
			v->irq = of_irq_get_byname(port_node, irqname);
		else
			v->irq = fwnode_irq_get(port->fwnode, i);
		if (v->irq <= 0) {
			ret = -EINVAL;
			goto err;
		}

		netif_napi_add(port->dev, &v->napi, mvpp2_poll,
			       NAPI_POLL_WEIGHT);
	}

	return 0;

err:
	for (i = 0; i < port->nqvecs; i++)
		irq_dispose_mapping(port->qvecs[i].irq);
	return ret;
}

static int mvpp2_queue_vectors_init(struct mvpp2_port *port,
				    struct device_node *port_node)
{
	if (port->has_tx_irqs)
		return mvpp2_multi_queue_vectors_init(port, port_node);
	else
		return mvpp2_simple_queue_vectors_init(port, port_node);
}

static void mvpp2_queue_vectors_deinit(struct mvpp2_port *port)
{
	int i;

	for (i = 0; i < port->nqvecs; i++)
		irq_dispose_mapping(port->qvecs[i].irq);
}

/* Configure Rx queue group interrupt for this port */
static void mvpp2_rx_irqs_setup(struct mvpp2_port *port)
{
	struct mvpp2 *priv = port->priv;
	u32 val;
	int i;

	if (priv->hw_version == MVPP21) {
		mvpp2_write(priv, MVPP21_ISR_RXQ_GROUP_REG(port->id),
			    port->nrxqs);
		return;
	}

	/* Handle the more complicated PPv2.2 case */
	for (i = 0; i < port->nqvecs; i++) {
		struct mvpp2_queue_vector *qv = port->qvecs + i;

		if (!qv->nrxqs)
			continue;

		val = qv->sw_thread_id;
		val |= port->id << MVPP22_ISR_RXQ_GROUP_INDEX_GROUP_OFFSET;
		mvpp2_write(priv, MVPP22_ISR_RXQ_GROUP_INDEX_REG, val);

		val = qv->first_rxq;
		val |= qv->nrxqs << MVPP22_ISR_RXQ_SUB_GROUP_SIZE_OFFSET;
		mvpp2_write(priv, MVPP22_ISR_RXQ_SUB_GROUP_CONFIG_REG, val);
	}
}

/* Initialize port HW */
static int mvpp2_port_init(struct mvpp2_port *port)
{
	struct device *dev = port->dev->dev.parent;
	struct mvpp2 *priv = port->priv;
	struct mvpp2_txq_pcpu *txq_pcpu;
	int queue, cpu, err;

	/* Checks for hardware constraints */
	if (port->first_rxq + port->nrxqs >
	    MVPP2_MAX_PORTS * priv->max_port_rxqs)
		return -EINVAL;

	if (port->nrxqs % MVPP2_DEFAULT_RXQ ||
	    port->nrxqs > priv->max_port_rxqs || port->ntxqs > MVPP2_MAX_TXQ)
		return -EINVAL;

	/* Disable port */
	mvpp2_egress_disable(port);
	mvpp2_port_disable(port);

	port->tx_time_coal = MVPP2_TXDONE_COAL_USEC;

	port->txqs = devm_kcalloc(dev, port->ntxqs, sizeof(*port->txqs),
				  GFP_KERNEL);
	if (!port->txqs)
		return -ENOMEM;

	/* Associate physical Tx queues to this port and initialize.
	 * The mapping is predefined.
	 */
	for (queue = 0; queue < port->ntxqs; queue++) {
		int queue_phy_id = mvpp2_txq_phys(port->id, queue);
		struct mvpp2_tx_queue *txq;

		txq = devm_kzalloc(dev, sizeof(*txq), GFP_KERNEL);
		if (!txq) {
			err = -ENOMEM;
			goto err_free_percpu;
		}

		txq->pcpu = alloc_percpu(struct mvpp2_txq_pcpu);
		if (!txq->pcpu) {
			err = -ENOMEM;
			goto err_free_percpu;
		}

		txq->id = queue_phy_id;
		txq->log_id = queue;
		txq->done_pkts_coal = MVPP2_TXDONE_COAL_PKTS_THRESH;
		for_each_present_cpu(cpu) {
			txq_pcpu = per_cpu_ptr(txq->pcpu, cpu);
			txq_pcpu->cpu = cpu;
		}

		port->txqs[queue] = txq;
	}

	port->rxqs = devm_kcalloc(dev, port->nrxqs, sizeof(*port->rxqs),
				  GFP_KERNEL);
	if (!port->rxqs) {
		err = -ENOMEM;
		goto err_free_percpu;
	}

	/* Allocate and initialize Rx queue for this port */
	for (queue = 0; queue < port->nrxqs; queue++) {
		struct mvpp2_rx_queue *rxq;

		/* Map physical Rx queue to port's logical Rx queue */
		rxq = devm_kzalloc(dev, sizeof(*rxq), GFP_KERNEL);
		if (!rxq) {
			err = -ENOMEM;
			goto err_free_percpu;
		}
		/* Map this Rx queue to a physical queue */
		rxq->id = port->first_rxq + queue;
		rxq->port = port->id;
		rxq->logic_rxq = queue;

		port->rxqs[queue] = rxq;
	}

	mvpp2_rx_irqs_setup(port);

	/* Create Rx descriptor rings */
	for (queue = 0; queue < port->nrxqs; queue++) {
		struct mvpp2_rx_queue *rxq = port->rxqs[queue];

		rxq->size = port->rx_ring_size;
		rxq->pkts_coal = MVPP2_RX_COAL_PKTS;
		rxq->time_coal = MVPP2_RX_COAL_USEC;
	}

	mvpp2_ingress_disable(port);

	/* Port default configuration */
	mvpp2_defaults_set(port);

	/* Port's classifier configuration */
	mvpp2_cls_oversize_rxq_set(port);
	mvpp2_cls_port_config(port);

	if (mvpp22_rss_is_supported())
		mvpp22_rss_port_init(port);

	/* Provide an initial Rx packet size */
	port->pkt_size = MVPP2_RX_PKT_SIZE(port->dev->mtu);

	/* Initialize pools for swf */
	err = mvpp2_swf_bm_pool_init(port);
	if (err)
		goto err_free_percpu;

	return 0;

err_free_percpu:
	for (queue = 0; queue < port->ntxqs; queue++) {
		if (!port->txqs[queue])
			continue;
		free_percpu(port->txqs[queue]->pcpu);
	}
	return err;
}

/* Checks if the port DT description has the TX interrupts
 * described. On PPv2.1, there are no such interrupts. On PPv2.2,
 * there are available, but we need to keep support for old DTs.
 */
static bool mvpp2_port_has_tx_irqs(struct mvpp2 *priv,
				   struct device_node *port_node)
{
	char *irqs[5] = { "rx-shared", "tx-cpu0", "tx-cpu1",
			  "tx-cpu2", "tx-cpu3" };
	int ret, i;

	if (priv->hw_version == MVPP21)
		return false;

	for (i = 0; i < 5; i++) {
		ret = of_property_match_string(port_node, "interrupt-names",
					       irqs[i]);
		if (ret < 0)
			return false;
	}

	return true;
}

static void mvpp2_port_copy_mac_addr(struct net_device *dev, struct mvpp2 *priv,
				     struct fwnode_handle *fwnode,
				     char **mac_from)
{
	struct mvpp2_port *port = netdev_priv(dev);
	char hw_mac_addr[ETH_ALEN] = {0};
	char fw_mac_addr[ETH_ALEN];

	if (fwnode_get_mac_address(fwnode, fw_mac_addr, ETH_ALEN)) {
		*mac_from = "firmware node";
		ether_addr_copy(dev->dev_addr, fw_mac_addr);
		return;
	}

	if (priv->hw_version == MVPP21) {
		mvpp21_get_mac_address(port, hw_mac_addr);
		if (is_valid_ether_addr(hw_mac_addr)) {
			*mac_from = "hardware";
			ether_addr_copy(dev->dev_addr, hw_mac_addr);
			return;
		}
	}

	*mac_from = "random";
	eth_hw_addr_random(dev);
}

static void mvpp2_phylink_validate(struct net_device *dev,
				   unsigned long *supported,
				   struct phylink_link_state *state)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };

	phylink_set(mask, Autoneg);
	phylink_set_port_modes(mask);
	phylink_set(mask, Pause);
	phylink_set(mask, Asym_Pause);

	switch (state->interface) {
	case PHY_INTERFACE_MODE_10GKR:
		phylink_set(mask, 10000baseCR_Full);
		phylink_set(mask, 10000baseSR_Full);
		phylink_set(mask, 10000baseLR_Full);
		phylink_set(mask, 10000baseLRM_Full);
		phylink_set(mask, 10000baseER_Full);
		phylink_set(mask, 10000baseKR_Full);
		/* Fall-through */
	default:
		phylink_set(mask, 10baseT_Half);
		phylink_set(mask, 10baseT_Full);
		phylink_set(mask, 100baseT_Half);
		phylink_set(mask, 100baseT_Full);
		phylink_set(mask, 10000baseT_Full);
		/* Fall-through */
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		phylink_set(mask, 1000baseT_Full);
		phylink_set(mask, 1000baseX_Full);
		phylink_set(mask, 2500baseX_Full);
	}

	bitmap_and(supported, supported, mask, __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_and(state->advertising, state->advertising, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static void mvpp22_xlg_link_state(struct mvpp2_port *port,
				  struct phylink_link_state *state)
{
	u32 val;

	state->speed = SPEED_10000;
	state->duplex = 1;
	state->an_complete = 1;

	val = readl(port->base + MVPP22_XLG_STATUS);
	state->link = !!(val & MVPP22_XLG_STATUS_LINK_UP);

	state->pause = 0;
	val = readl(port->base + MVPP22_XLG_CTRL0_REG);
	if (val & MVPP22_XLG_CTRL0_TX_FLOW_CTRL_EN)
		state->pause |= MLO_PAUSE_TX;
	if (val & MVPP22_XLG_CTRL0_RX_FLOW_CTRL_EN)
		state->pause |= MLO_PAUSE_RX;
}

static void mvpp2_gmac_link_state(struct mvpp2_port *port,
				  struct phylink_link_state *state)
{
	u32 val;

	val = readl(port->base + MVPP2_GMAC_STATUS0);

	state->an_complete = !!(val & MVPP2_GMAC_STATUS0_AN_COMPLETE);
	state->link = !!(val & MVPP2_GMAC_STATUS0_LINK_UP);
	state->duplex = !!(val & MVPP2_GMAC_STATUS0_FULL_DUPLEX);

	switch (port->phy_interface) {
	case PHY_INTERFACE_MODE_1000BASEX:
		state->speed = SPEED_1000;
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		state->speed = SPEED_2500;
		break;
	default:
		if (val & MVPP2_GMAC_STATUS0_GMII_SPEED)
			state->speed = SPEED_1000;
		else if (val & MVPP2_GMAC_STATUS0_MII_SPEED)
			state->speed = SPEED_100;
		else
			state->speed = SPEED_10;
	}

	state->pause = 0;
	if (val & MVPP2_GMAC_STATUS0_RX_PAUSE)
		state->pause |= MLO_PAUSE_RX;
	if (val & MVPP2_GMAC_STATUS0_TX_PAUSE)
		state->pause |= MLO_PAUSE_TX;
}

static int mvpp2_phylink_mac_link_state(struct net_device *dev,
					struct phylink_link_state *state)
{
	struct mvpp2_port *port = netdev_priv(dev);

	if (port->priv->hw_version == MVPP22 && port->gop_id == 0) {
		u32 mode = readl(port->base + MVPP22_XLG_CTRL3_REG);
		mode &= MVPP22_XLG_CTRL3_MACMODESELECT_MASK;

		if (mode == MVPP22_XLG_CTRL3_MACMODESELECT_10G) {
			mvpp22_xlg_link_state(port, state);
			return 1;
		}
	}

	mvpp2_gmac_link_state(port, state);
	return 1;
}

static void mvpp2_mac_an_restart(struct net_device *dev)
{
	struct mvpp2_port *port = netdev_priv(dev);
	u32 val;

	if (port->phy_interface != PHY_INTERFACE_MODE_SGMII)
		return;

	val = readl(port->base + MVPP2_GMAC_AUTONEG_CONFIG);
	/* The RESTART_AN bit is cleared by the h/w after restarting the AN
	 * process.
	 */
	val |= MVPP2_GMAC_IN_BAND_RESTART_AN | MVPP2_GMAC_IN_BAND_AUTONEG;
	writel(val, port->base + MVPP2_GMAC_AUTONEG_CONFIG);
}

static void mvpp2_xlg_config(struct mvpp2_port *port, unsigned int mode,
			     const struct phylink_link_state *state)
{
	u32 ctrl0, ctrl4;

	ctrl0 = readl(port->base + MVPP22_XLG_CTRL0_REG);
	ctrl4 = readl(port->base + MVPP22_XLG_CTRL4_REG);

	if (state->pause & MLO_PAUSE_TX)
		ctrl0 |= MVPP22_XLG_CTRL0_TX_FLOW_CTRL_EN;
	if (state->pause & MLO_PAUSE_RX)
		ctrl0 |= MVPP22_XLG_CTRL0_RX_FLOW_CTRL_EN;

	ctrl4 &= ~MVPP22_XLG_CTRL4_MACMODSELECT_GMAC;
	ctrl4 |= MVPP22_XLG_CTRL4_FWD_FC | MVPP22_XLG_CTRL4_FWD_PFC |
		 MVPP22_XLG_CTRL4_EN_IDLE_CHECK;

	writel(ctrl0, port->base + MVPP22_XLG_CTRL0_REG);
	writel(ctrl4, port->base + MVPP22_XLG_CTRL4_REG);
}

static void mvpp2_gmac_config(struct mvpp2_port *port, unsigned int mode,
			      const struct phylink_link_state *state)
{
	u32 an, ctrl0, ctrl2, ctrl4;

	an = readl(port->base + MVPP2_GMAC_AUTONEG_CONFIG);
	ctrl0 = readl(port->base + MVPP2_GMAC_CTRL_0_REG);
	ctrl2 = readl(port->base + MVPP2_GMAC_CTRL_2_REG);
	ctrl4 = readl(port->base + MVPP22_GMAC_CTRL_4_REG);

	/* Force link down */
	an &= ~MVPP2_GMAC_FORCE_LINK_PASS;
	an |= MVPP2_GMAC_FORCE_LINK_DOWN;
	writel(an, port->base + MVPP2_GMAC_AUTONEG_CONFIG);

	/* Set the GMAC in a reset state */
	ctrl2 |= MVPP2_GMAC_PORT_RESET_MASK;
	writel(ctrl2, port->base + MVPP2_GMAC_CTRL_2_REG);

	an &= ~(MVPP2_GMAC_CONFIG_MII_SPEED | MVPP2_GMAC_CONFIG_GMII_SPEED |
		MVPP2_GMAC_AN_SPEED_EN | MVPP2_GMAC_FC_ADV_EN |
		MVPP2_GMAC_FC_ADV_ASM_EN | MVPP2_GMAC_FLOW_CTRL_AUTONEG |
		MVPP2_GMAC_CONFIG_FULL_DUPLEX | MVPP2_GMAC_AN_DUPLEX_EN |
		MVPP2_GMAC_FORCE_LINK_DOWN);
	ctrl0 &= ~MVPP2_GMAC_PORT_TYPE_MASK;
	ctrl2 &= ~(MVPP2_GMAC_PORT_RESET_MASK | MVPP2_GMAC_PCS_ENABLE_MASK);

	if (state->interface == PHY_INTERFACE_MODE_1000BASEX ||
	    state->interface == PHY_INTERFACE_MODE_2500BASEX) {
		/* 1000BaseX and 2500BaseX ports cannot negotiate speed nor can
		 * they negotiate duplex: they are always operating with a fixed
		 * speed of 1000/2500Mbps in full duplex, so force 1000/2500
		 * speed and full duplex here.
		 */
		ctrl0 |= MVPP2_GMAC_PORT_TYPE_MASK;
		an |= MVPP2_GMAC_CONFIG_GMII_SPEED |
		      MVPP2_GMAC_CONFIG_FULL_DUPLEX;
	} else if (!phy_interface_mode_is_rgmii(state->interface)) {
		an |= MVPP2_GMAC_AN_SPEED_EN | MVPP2_GMAC_FLOW_CTRL_AUTONEG;
	}

	if (state->duplex)
		an |= MVPP2_GMAC_CONFIG_FULL_DUPLEX;
	if (phylink_test(state->advertising, Pause))
		an |= MVPP2_GMAC_FC_ADV_EN;
	if (phylink_test(state->advertising, Asym_Pause))
		an |= MVPP2_GMAC_FC_ADV_ASM_EN;

	if (state->interface == PHY_INTERFACE_MODE_SGMII ||
	    state->interface == PHY_INTERFACE_MODE_1000BASEX ||
	    state->interface == PHY_INTERFACE_MODE_2500BASEX) {
		an |= MVPP2_GMAC_IN_BAND_AUTONEG;
		ctrl2 |= MVPP2_GMAC_INBAND_AN_MASK | MVPP2_GMAC_PCS_ENABLE_MASK;

		ctrl4 &= ~(MVPP22_CTRL4_EXT_PIN_GMII_SEL |
			   MVPP22_CTRL4_RX_FC_EN | MVPP22_CTRL4_TX_FC_EN);
		ctrl4 |= MVPP22_CTRL4_SYNC_BYPASS_DIS |
			 MVPP22_CTRL4_DP_CLK_SEL |
			 MVPP22_CTRL4_QSGMII_BYPASS_ACTIVE;

		if (state->pause & MLO_PAUSE_TX)
			ctrl4 |= MVPP22_CTRL4_TX_FC_EN;
		if (state->pause & MLO_PAUSE_RX)
			ctrl4 |= MVPP22_CTRL4_RX_FC_EN;
	} else if (phy_interface_mode_is_rgmii(state->interface)) {
		an |= MVPP2_GMAC_IN_BAND_AUTONEG_BYPASS;

		if (state->speed == SPEED_1000)
			an |= MVPP2_GMAC_CONFIG_GMII_SPEED;
		else if (state->speed == SPEED_100)
			an |= MVPP2_GMAC_CONFIG_MII_SPEED;

		ctrl4 &= ~MVPP22_CTRL4_DP_CLK_SEL;
		ctrl4 |= MVPP22_CTRL4_EXT_PIN_GMII_SEL |
			 MVPP22_CTRL4_SYNC_BYPASS_DIS |
			 MVPP22_CTRL4_QSGMII_BYPASS_ACTIVE;
	}

	writel(ctrl0, port->base + MVPP2_GMAC_CTRL_0_REG);
	writel(ctrl2, port->base + MVPP2_GMAC_CTRL_2_REG);
	writel(ctrl4, port->base + MVPP22_GMAC_CTRL_4_REG);
	writel(an, port->base + MVPP2_GMAC_AUTONEG_CONFIG);
}

static void mvpp2_mac_config(struct net_device *dev, unsigned int mode,
			     const struct phylink_link_state *state)
{
	struct mvpp2_port *port = netdev_priv(dev);

	/* Check for invalid configuration */
	if (state->interface == PHY_INTERFACE_MODE_10GKR && port->gop_id != 0) {
		netdev_err(dev, "Invalid mode on %s\n", dev->name);
		return;
	}

	/* Make sure the port is disabled when reconfiguring the mode */
	mvpp2_port_disable(port);

	if (port->priv->hw_version == MVPP22 &&
	    port->phy_interface != state->interface) {
		port->phy_interface = state->interface;

		/* Reconfigure the serdes lanes */
		phy_power_off(port->comphy);
		mvpp22_mode_reconfigure(port);
	}

	/* mac (re)configuration */
	if (state->interface == PHY_INTERFACE_MODE_10GKR)
		mvpp2_xlg_config(port, mode, state);
	else if (phy_interface_mode_is_rgmii(state->interface) ||
		 state->interface == PHY_INTERFACE_MODE_SGMII ||
		 state->interface == PHY_INTERFACE_MODE_1000BASEX ||
		 state->interface == PHY_INTERFACE_MODE_2500BASEX)
		mvpp2_gmac_config(port, mode, state);

	if (port->priv->hw_version == MVPP21 && port->flags & MVPP2_F_LOOPBACK)
		mvpp2_port_loopback_set(port, state);

	mvpp2_port_enable(port);
}

static void mvpp2_mac_link_up(struct net_device *dev, unsigned int mode,
			      phy_interface_t interface, struct phy_device *phy)
{
	struct mvpp2_port *port = netdev_priv(dev);
	u32 val;

	if (!phylink_autoneg_inband(mode) &&
	    interface != PHY_INTERFACE_MODE_10GKR) {
		val = readl(port->base + MVPP2_GMAC_AUTONEG_CONFIG);
		val &= ~MVPP2_GMAC_FORCE_LINK_DOWN;
		if (phy_interface_mode_is_rgmii(interface))
			val |= MVPP2_GMAC_FORCE_LINK_PASS;
		writel(val, port->base + MVPP2_GMAC_AUTONEG_CONFIG);
	}

	mvpp2_port_enable(port);

	mvpp2_egress_enable(port);
	mvpp2_ingress_enable(port);
	netif_tx_wake_all_queues(dev);
}

static void mvpp2_mac_link_down(struct net_device *dev, unsigned int mode,
				phy_interface_t interface)
{
	struct mvpp2_port *port = netdev_priv(dev);
	u32 val;

	if (!phylink_autoneg_inband(mode) &&
	    interface != PHY_INTERFACE_MODE_10GKR) {
		val = readl(port->base + MVPP2_GMAC_AUTONEG_CONFIG);
		val &= ~MVPP2_GMAC_FORCE_LINK_PASS;
		val |= MVPP2_GMAC_FORCE_LINK_DOWN;
		writel(val, port->base + MVPP2_GMAC_AUTONEG_CONFIG);
	}

	netif_tx_stop_all_queues(dev);
	mvpp2_egress_disable(port);
	mvpp2_ingress_disable(port);

	/* When using link interrupts to notify phylink of a MAC state change,
	 * we do not want the port to be disabled (we want to receive further
	 * interrupts, to be notified when the port will have a link later).
	 */
	if (!port->has_phy)
		return;

	mvpp2_port_disable(port);
}

static const struct phylink_mac_ops mvpp2_phylink_ops = {
	.validate = mvpp2_phylink_validate,
	.mac_link_state = mvpp2_phylink_mac_link_state,
	.mac_an_restart = mvpp2_mac_an_restart,
	.mac_config = mvpp2_mac_config,
	.mac_link_up = mvpp2_mac_link_up,
	.mac_link_down = mvpp2_mac_link_down,
};

/* Ports initialization */
static int mvpp2_port_probe(struct platform_device *pdev,
			    struct fwnode_handle *port_fwnode,
			    struct mvpp2 *priv)
{
	struct phy *comphy = NULL;
	struct mvpp2_port *port;
	struct mvpp2_port_pcpu *port_pcpu;
	struct device_node *port_node = to_of_node(port_fwnode);
	struct net_device *dev;
	struct resource *res;
	struct phylink *phylink;
	char *mac_from = "";
	unsigned int ntxqs, nrxqs;
	bool has_tx_irqs;
	u32 id;
	int features;
	int phy_mode;
	int err, i, cpu;

	if (port_node) {
		has_tx_irqs = mvpp2_port_has_tx_irqs(priv, port_node);
	} else {
		has_tx_irqs = true;
		queue_mode = MVPP2_QDIST_MULTI_MODE;
	}

	if (!has_tx_irqs)
		queue_mode = MVPP2_QDIST_SINGLE_MODE;

	ntxqs = MVPP2_MAX_TXQ;
	if (priv->hw_version == MVPP22 && queue_mode == MVPP2_QDIST_MULTI_MODE)
		nrxqs = MVPP2_DEFAULT_RXQ * num_possible_cpus();
	else
		nrxqs = MVPP2_DEFAULT_RXQ;

	dev = alloc_etherdev_mqs(sizeof(*port), ntxqs, nrxqs);
	if (!dev)
		return -ENOMEM;

	phy_mode = fwnode_get_phy_mode(port_fwnode);
	if (phy_mode < 0) {
		dev_err(&pdev->dev, "incorrect phy mode\n");
		err = phy_mode;
		goto err_free_netdev;
	}

	if (port_node) {
		comphy = devm_of_phy_get(&pdev->dev, port_node, NULL);
		if (IS_ERR(comphy)) {
			if (PTR_ERR(comphy) == -EPROBE_DEFER) {
				err = -EPROBE_DEFER;
				goto err_free_netdev;
			}
			comphy = NULL;
		}
	}

	if (fwnode_property_read_u32(port_fwnode, "port-id", &id)) {
		err = -EINVAL;
		dev_err(&pdev->dev, "missing port-id value\n");
		goto err_free_netdev;
	}

	dev->tx_queue_len = MVPP2_MAX_TXD_MAX;
	dev->watchdog_timeo = 5 * HZ;
	dev->netdev_ops = &mvpp2_netdev_ops;
	dev->ethtool_ops = &mvpp2_eth_tool_ops;

	port = netdev_priv(dev);
	port->dev = dev;
	port->fwnode = port_fwnode;
	port->has_phy = !!of_find_property(port_node, "phy", NULL);
	port->ntxqs = ntxqs;
	port->nrxqs = nrxqs;
	port->priv = priv;
	port->has_tx_irqs = has_tx_irqs;

	err = mvpp2_queue_vectors_init(port, port_node);
	if (err)
		goto err_free_netdev;

	if (port_node)
		port->link_irq = of_irq_get_byname(port_node, "link");
	else
		port->link_irq = fwnode_irq_get(port_fwnode, port->nqvecs + 1);
	if (port->link_irq == -EPROBE_DEFER) {
		err = -EPROBE_DEFER;
		goto err_deinit_qvecs;
	}
	if (port->link_irq <= 0)
		/* the link irq is optional */
		port->link_irq = 0;

	if (fwnode_property_read_bool(port_fwnode, "marvell,loopback"))
		port->flags |= MVPP2_F_LOOPBACK;

	port->id = id;
	if (priv->hw_version == MVPP21)
		port->first_rxq = port->id * port->nrxqs;
	else
		port->first_rxq = port->id * priv->max_port_rxqs;

	port->of_node = port_node;
	port->phy_interface = phy_mode;
	port->comphy = comphy;

	if (priv->hw_version == MVPP21) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 2 + id);
		port->base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(port->base)) {
			err = PTR_ERR(port->base);
			goto err_free_irq;
		}

		port->stats_base = port->priv->lms_base +
				   MVPP21_MIB_COUNTERS_OFFSET +
				   port->gop_id * MVPP21_MIB_COUNTERS_PORT_SZ;
	} else {
		if (fwnode_property_read_u32(port_fwnode, "gop-port-id",
					     &port->gop_id)) {
			err = -EINVAL;
			dev_err(&pdev->dev, "missing gop-port-id value\n");
			goto err_deinit_qvecs;
		}

		port->base = priv->iface_base + MVPP22_GMAC_BASE(port->gop_id);
		port->stats_base = port->priv->iface_base +
				   MVPP22_MIB_COUNTERS_OFFSET +
				   port->gop_id * MVPP22_MIB_COUNTERS_PORT_SZ;
	}

	/* Alloc per-cpu and ethtool stats */
	port->stats = netdev_alloc_pcpu_stats(struct mvpp2_pcpu_stats);
	if (!port->stats) {
		err = -ENOMEM;
		goto err_free_irq;
	}

	port->ethtool_stats = devm_kcalloc(&pdev->dev,
					   ARRAY_SIZE(mvpp2_ethtool_regs),
					   sizeof(u64), GFP_KERNEL);
	if (!port->ethtool_stats) {
		err = -ENOMEM;
		goto err_free_stats;
	}

	mutex_init(&port->gather_stats_lock);
	INIT_DELAYED_WORK(&port->stats_work, mvpp2_gather_hw_statistics);

	mvpp2_port_copy_mac_addr(dev, priv, port_fwnode, &mac_from);

	port->tx_ring_size = MVPP2_MAX_TXD_DFLT;
	port->rx_ring_size = MVPP2_MAX_RXD_DFLT;
	SET_NETDEV_DEV(dev, &pdev->dev);

	err = mvpp2_port_init(port);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to init port %d\n", id);
		goto err_free_stats;
	}

	mvpp2_port_periodic_xon_disable(port);

	mvpp2_port_reset(port);

	port->pcpu = alloc_percpu(struct mvpp2_port_pcpu);
	if (!port->pcpu) {
		err = -ENOMEM;
		goto err_free_txq_pcpu;
	}

	if (!port->has_tx_irqs) {
		for_each_present_cpu(cpu) {
			port_pcpu = per_cpu_ptr(port->pcpu, cpu);

			hrtimer_init(&port_pcpu->tx_done_timer, CLOCK_MONOTONIC,
				     HRTIMER_MODE_REL_PINNED);
			port_pcpu->tx_done_timer.function = mvpp2_hr_timer_cb;
			port_pcpu->timer_scheduled = false;

			tasklet_init(&port_pcpu->tx_done_tasklet,
				     mvpp2_tx_proc_cb,
				     (unsigned long)dev);
		}
	}

	features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		   NETIF_F_TSO;
	dev->features = features | NETIF_F_RXCSUM;
	dev->hw_features |= features | NETIF_F_RXCSUM | NETIF_F_GRO |
			    NETIF_F_HW_VLAN_CTAG_FILTER;

	if (mvpp22_rss_is_supported())
		dev->hw_features |= NETIF_F_RXHASH;

	if (port->pool_long->id == MVPP2_BM_JUMBO && port->id != 0) {
		dev->features &= ~(NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM);
		dev->hw_features &= ~(NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM);
	}

	dev->vlan_features |= features;
	dev->gso_max_segs = MVPP2_MAX_TSO_SEGS;
	dev->priv_flags |= IFF_UNICAST_FLT;

	/* MTU range: 68 - 9704 */
	dev->min_mtu = ETH_MIN_MTU;
	/* 9704 == 9728 - 20 and rounding to 8 */
	dev->max_mtu = MVPP2_BM_JUMBO_PKT_SIZE;
	dev->dev.of_node = port_node;

	/* Phylink isn't used w/ ACPI as of now */
	if (port_node) {
		phylink = phylink_create(dev, port_fwnode, phy_mode,
					 &mvpp2_phylink_ops);
		if (IS_ERR(phylink)) {
			err = PTR_ERR(phylink);
			goto err_free_port_pcpu;
		}
		port->phylink = phylink;
	} else {
		port->phylink = NULL;
	}

	err = register_netdev(dev);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register netdev\n");
		goto err_phylink;
	}
	netdev_info(dev, "Using %s mac address %pM\n", mac_from, dev->dev_addr);

	priv->port_list[priv->port_count++] = port;

	return 0;

err_phylink:
	if (port->phylink)
		phylink_destroy(port->phylink);
err_free_port_pcpu:
	free_percpu(port->pcpu);
err_free_txq_pcpu:
	for (i = 0; i < port->ntxqs; i++)
		free_percpu(port->txqs[i]->pcpu);
err_free_stats:
	free_percpu(port->stats);
err_free_irq:
	if (port->link_irq)
		irq_dispose_mapping(port->link_irq);
err_deinit_qvecs:
	mvpp2_queue_vectors_deinit(port);
err_free_netdev:
	free_netdev(dev);
	return err;
}

/* Ports removal routine */
static void mvpp2_port_remove(struct mvpp2_port *port)
{
	int i;

	unregister_netdev(port->dev);
	if (port->phylink)
		phylink_destroy(port->phylink);
	free_percpu(port->pcpu);
	free_percpu(port->stats);
	for (i = 0; i < port->ntxqs; i++)
		free_percpu(port->txqs[i]->pcpu);
	mvpp2_queue_vectors_deinit(port);
	if (port->link_irq)
		irq_dispose_mapping(port->link_irq);
	free_netdev(port->dev);
}

/* Initialize decoding windows */
static void mvpp2_conf_mbus_windows(const struct mbus_dram_target_info *dram,
				    struct mvpp2 *priv)
{
	u32 win_enable;
	int i;

	for (i = 0; i < 6; i++) {
		mvpp2_write(priv, MVPP2_WIN_BASE(i), 0);
		mvpp2_write(priv, MVPP2_WIN_SIZE(i), 0);

		if (i < 4)
			mvpp2_write(priv, MVPP2_WIN_REMAP(i), 0);
	}

	win_enable = 0;

	for (i = 0; i < dram->num_cs; i++) {
		const struct mbus_dram_window *cs = dram->cs + i;

		mvpp2_write(priv, MVPP2_WIN_BASE(i),
			    (cs->base & 0xffff0000) | (cs->mbus_attr << 8) |
			    dram->mbus_dram_target_id);

		mvpp2_write(priv, MVPP2_WIN_SIZE(i),
			    (cs->size - 1) & 0xffff0000);

		win_enable |= (1 << i);
	}

	mvpp2_write(priv, MVPP2_BASE_ADDR_ENABLE, win_enable);
}

/* Initialize Rx FIFO's */
static void mvpp2_rx_fifo_init(struct mvpp2 *priv)
{
	int port;

	for (port = 0; port < MVPP2_MAX_PORTS; port++) {
		mvpp2_write(priv, MVPP2_RX_DATA_FIFO_SIZE_REG(port),
			    MVPP2_RX_FIFO_PORT_DATA_SIZE_4KB);
		mvpp2_write(priv, MVPP2_RX_ATTR_FIFO_SIZE_REG(port),
			    MVPP2_RX_FIFO_PORT_ATTR_SIZE_4KB);
	}

	mvpp2_write(priv, MVPP2_RX_MIN_PKT_SIZE_REG,
		    MVPP2_RX_FIFO_PORT_MIN_PKT);
	mvpp2_write(priv, MVPP2_RX_FIFO_INIT_REG, 0x1);
}

static void mvpp22_rx_fifo_init(struct mvpp2 *priv)
{
	int port;

	/* The FIFO size parameters are set depending on the maximum speed a
	 * given port can handle:
	 * - Port 0: 10Gbps
	 * - Port 1: 2.5Gbps
	 * - Ports 2 and 3: 1Gbps
	 */

	mvpp2_write(priv, MVPP2_RX_DATA_FIFO_SIZE_REG(0),
		    MVPP2_RX_FIFO_PORT_DATA_SIZE_32KB);
	mvpp2_write(priv, MVPP2_RX_ATTR_FIFO_SIZE_REG(0),
		    MVPP2_RX_FIFO_PORT_ATTR_SIZE_32KB);

	mvpp2_write(priv, MVPP2_RX_DATA_FIFO_SIZE_REG(1),
		    MVPP2_RX_FIFO_PORT_DATA_SIZE_8KB);
	mvpp2_write(priv, MVPP2_RX_ATTR_FIFO_SIZE_REG(1),
		    MVPP2_RX_FIFO_PORT_ATTR_SIZE_8KB);

	for (port = 2; port < MVPP2_MAX_PORTS; port++) {
		mvpp2_write(priv, MVPP2_RX_DATA_FIFO_SIZE_REG(port),
			    MVPP2_RX_FIFO_PORT_DATA_SIZE_4KB);
		mvpp2_write(priv, MVPP2_RX_ATTR_FIFO_SIZE_REG(port),
			    MVPP2_RX_FIFO_PORT_ATTR_SIZE_4KB);
	}

	mvpp2_write(priv, MVPP2_RX_MIN_PKT_SIZE_REG,
		    MVPP2_RX_FIFO_PORT_MIN_PKT);
	mvpp2_write(priv, MVPP2_RX_FIFO_INIT_REG, 0x1);
}

/* Initialize Tx FIFO's: the total FIFO size is 19kB on PPv2.2 and 10G
 * interfaces must have a Tx FIFO size of 10kB. As only port 0 can do 10G,
 * configure its Tx FIFO size to 10kB and the others ports Tx FIFO size to 3kB.
 */
static void mvpp22_tx_fifo_init(struct mvpp2 *priv)
{
	int port, size, thrs;

	for (port = 0; port < MVPP2_MAX_PORTS; port++) {
		if (port == 0) {
			size = MVPP22_TX_FIFO_DATA_SIZE_10KB;
			thrs = MVPP2_TX_FIFO_THRESHOLD_10KB;
		} else {
			size = MVPP22_TX_FIFO_DATA_SIZE_3KB;
			thrs = MVPP2_TX_FIFO_THRESHOLD_3KB;
		}
		mvpp2_write(priv, MVPP22_TX_FIFO_SIZE_REG(port), size);
		mvpp2_write(priv, MVPP22_TX_FIFO_THRESH_REG(port), thrs);
	}
}

static void mvpp2_axi_init(struct mvpp2 *priv)
{
	u32 val, rdval, wrval;

	mvpp2_write(priv, MVPP22_BM_ADDR_HIGH_RLS_REG, 0x0);

	/* AXI Bridge Configuration */

	rdval = MVPP22_AXI_CODE_CACHE_RD_CACHE
		<< MVPP22_AXI_ATTR_CACHE_OFFS;
	rdval |= MVPP22_AXI_CODE_DOMAIN_OUTER_DOM
		<< MVPP22_AXI_ATTR_DOMAIN_OFFS;

	wrval = MVPP22_AXI_CODE_CACHE_WR_CACHE
		<< MVPP22_AXI_ATTR_CACHE_OFFS;
	wrval |= MVPP22_AXI_CODE_DOMAIN_OUTER_DOM
		<< MVPP22_AXI_ATTR_DOMAIN_OFFS;

	/* BM */
	mvpp2_write(priv, MVPP22_AXI_BM_WR_ATTR_REG, wrval);
	mvpp2_write(priv, MVPP22_AXI_BM_RD_ATTR_REG, rdval);

	/* Descriptors */
	mvpp2_write(priv, MVPP22_AXI_AGGRQ_DESCR_RD_ATTR_REG, rdval);
	mvpp2_write(priv, MVPP22_AXI_TXQ_DESCR_WR_ATTR_REG, wrval);
	mvpp2_write(priv, MVPP22_AXI_TXQ_DESCR_RD_ATTR_REG, rdval);
	mvpp2_write(priv, MVPP22_AXI_RXQ_DESCR_WR_ATTR_REG, wrval);

	/* Buffer Data */
	mvpp2_write(priv, MVPP22_AXI_TX_DATA_RD_ATTR_REG, rdval);
	mvpp2_write(priv, MVPP22_AXI_RX_DATA_WR_ATTR_REG, wrval);

	val = MVPP22_AXI_CODE_CACHE_NON_CACHE
		<< MVPP22_AXI_CODE_CACHE_OFFS;
	val |= MVPP22_AXI_CODE_DOMAIN_SYSTEM
		<< MVPP22_AXI_CODE_DOMAIN_OFFS;
	mvpp2_write(priv, MVPP22_AXI_RD_NORMAL_CODE_REG, val);
	mvpp2_write(priv, MVPP22_AXI_WR_NORMAL_CODE_REG, val);

	val = MVPP22_AXI_CODE_CACHE_RD_CACHE
		<< MVPP22_AXI_CODE_CACHE_OFFS;
	val |= MVPP22_AXI_CODE_DOMAIN_OUTER_DOM
		<< MVPP22_AXI_CODE_DOMAIN_OFFS;

	mvpp2_write(priv, MVPP22_AXI_RD_SNOOP_CODE_REG, val);

	val = MVPP22_AXI_CODE_CACHE_WR_CACHE
		<< MVPP22_AXI_CODE_CACHE_OFFS;
	val |= MVPP22_AXI_CODE_DOMAIN_OUTER_DOM
		<< MVPP22_AXI_CODE_DOMAIN_OFFS;

	mvpp2_write(priv, MVPP22_AXI_WR_SNOOP_CODE_REG, val);
}

/* Initialize network controller common part HW */
static int mvpp2_init(struct platform_device *pdev, struct mvpp2 *priv)
{
	const struct mbus_dram_target_info *dram_target_info;
	int err, i;
	u32 val;

	/* MBUS windows configuration */
	dram_target_info = mv_mbus_dram_info();
	if (dram_target_info)
		mvpp2_conf_mbus_windows(dram_target_info, priv);

	if (priv->hw_version == MVPP22)
		mvpp2_axi_init(priv);

	/* Disable HW PHY polling */
	if (priv->hw_version == MVPP21) {
		val = readl(priv->lms_base + MVPP2_PHY_AN_CFG0_REG);
		val |= MVPP2_PHY_AN_STOP_SMI0_MASK;
		writel(val, priv->lms_base + MVPP2_PHY_AN_CFG0_REG);
	} else {
		val = readl(priv->iface_base + MVPP22_SMI_MISC_CFG_REG);
		val &= ~MVPP22_SMI_POLLING_EN;
		writel(val, priv->iface_base + MVPP22_SMI_MISC_CFG_REG);
	}

	/* Allocate and initialize aggregated TXQs */
	priv->aggr_txqs = devm_kcalloc(&pdev->dev, num_present_cpus(),
				       sizeof(*priv->aggr_txqs),
				       GFP_KERNEL);
	if (!priv->aggr_txqs)
		return -ENOMEM;

	for_each_present_cpu(i) {
		priv->aggr_txqs[i].id = i;
		priv->aggr_txqs[i].size = MVPP2_AGGR_TXQ_SIZE;
		err = mvpp2_aggr_txq_init(pdev, &priv->aggr_txqs[i], i, priv);
		if (err < 0)
			return err;
	}

	/* Fifo Init */
	if (priv->hw_version == MVPP21) {
		mvpp2_rx_fifo_init(priv);
	} else {
		mvpp22_rx_fifo_init(priv);
		mvpp22_tx_fifo_init(priv);
	}

	if (priv->hw_version == MVPP21)
		writel(MVPP2_EXT_GLOBAL_CTRL_DEFAULT,
		       priv->lms_base + MVPP2_MNG_EXTENDED_GLOBAL_CTRL_REG);

	/* Allow cache snoop when transmiting packets */
	mvpp2_write(priv, MVPP2_TX_SNOOP_REG, 0x1);

	/* Buffer Manager initialization */
	err = mvpp2_bm_init(pdev, priv);
	if (err < 0)
		return err;

	/* Parser default initialization */
	err = mvpp2_prs_default_init(pdev, priv);
	if (err < 0)
		return err;

	/* Classifier default initialization */
	mvpp2_cls_init(priv);

	return 0;
}

static int mvpp2_probe(struct platform_device *pdev)
{
	const struct acpi_device_id *acpi_id;
	struct fwnode_handle *fwnode = pdev->dev.fwnode;
	struct fwnode_handle *port_fwnode;
	struct mvpp2 *priv;
	struct resource *res;
	void __iomem *base;
	int i;
	int err;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (has_acpi_companion(&pdev->dev)) {
		acpi_id = acpi_match_device(pdev->dev.driver->acpi_match_table,
					    &pdev->dev);
		priv->hw_version = (unsigned long)acpi_id->driver_data;
	} else {
		priv->hw_version =
			(unsigned long)of_device_get_match_data(&pdev->dev);
	}

	/* multi queue mode isn't supported on PPV2.1, fallback to single
	 * mode
	 */
	if (priv->hw_version == MVPP21)
		queue_mode = MVPP2_QDIST_SINGLE_MODE;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	if (priv->hw_version == MVPP21) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		priv->lms_base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(priv->lms_base))
			return PTR_ERR(priv->lms_base);
	} else {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (has_acpi_companion(&pdev->dev)) {
			/* In case the MDIO memory region is declared in
			 * the ACPI, it can already appear as 'in-use'
			 * in the OS. Because it is overlapped by second
			 * region of the network controller, make
			 * sure it is released, before requesting it again.
			 * The care is taken by mvpp2 driver to avoid
			 * concurrent access to this memory region.
			 */
			release_resource(res);
		}
		priv->iface_base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(priv->iface_base))
			return PTR_ERR(priv->iface_base);
	}

	if (priv->hw_version == MVPP22 && dev_of_node(&pdev->dev)) {
		priv->sysctrl_base =
			syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							"marvell,system-controller");
		if (IS_ERR(priv->sysctrl_base))
			/* The system controller regmap is optional for dt
			 * compatibility reasons. When not provided, the
			 * configuration of the GoP relies on the
			 * firmware/bootloader.
			 */
			priv->sysctrl_base = NULL;
	}

	mvpp2_setup_bm_pool();

	for (i = 0; i < MVPP2_MAX_THREADS; i++) {
		u32 addr_space_sz;

		addr_space_sz = (priv->hw_version == MVPP21 ?
				 MVPP21_ADDR_SPACE_SZ : MVPP22_ADDR_SPACE_SZ);
		priv->swth_base[i] = base + i * addr_space_sz;
	}

	if (priv->hw_version == MVPP21)
		priv->max_port_rxqs = 8;
	else
		priv->max_port_rxqs = 32;

	if (dev_of_node(&pdev->dev)) {
		priv->pp_clk = devm_clk_get(&pdev->dev, "pp_clk");
		if (IS_ERR(priv->pp_clk))
			return PTR_ERR(priv->pp_clk);
		err = clk_prepare_enable(priv->pp_clk);
		if (err < 0)
			return err;

		priv->gop_clk = devm_clk_get(&pdev->dev, "gop_clk");
		if (IS_ERR(priv->gop_clk)) {
			err = PTR_ERR(priv->gop_clk);
			goto err_pp_clk;
		}
		err = clk_prepare_enable(priv->gop_clk);
		if (err < 0)
			goto err_pp_clk;

		if (priv->hw_version == MVPP22) {
			priv->mg_clk = devm_clk_get(&pdev->dev, "mg_clk");
			if (IS_ERR(priv->mg_clk)) {
				err = PTR_ERR(priv->mg_clk);
				goto err_gop_clk;
			}

			err = clk_prepare_enable(priv->mg_clk);
			if (err < 0)
				goto err_gop_clk;

			priv->mg_core_clk = devm_clk_get(&pdev->dev, "mg_core_clk");
			if (IS_ERR(priv->mg_core_clk)) {
				priv->mg_core_clk = NULL;
			} else {
				err = clk_prepare_enable(priv->mg_core_clk);
				if (err < 0)
					goto err_mg_clk;
			}
		}

		priv->axi_clk = devm_clk_get(&pdev->dev, "axi_clk");
		if (IS_ERR(priv->axi_clk)) {
			err = PTR_ERR(priv->axi_clk);
			if (err == -EPROBE_DEFER)
				goto err_mg_core_clk;
			priv->axi_clk = NULL;
		} else {
			err = clk_prepare_enable(priv->axi_clk);
			if (err < 0)
				goto err_mg_core_clk;
		}

		/* Get system's tclk rate */
		priv->tclk = clk_get_rate(priv->pp_clk);
	} else if (device_property_read_u32(&pdev->dev, "clock-frequency",
					    &priv->tclk)) {
		dev_err(&pdev->dev, "missing clock-frequency value\n");
		return -EINVAL;
	}

	if (priv->hw_version == MVPP22) {
		err = dma_set_mask(&pdev->dev, MVPP2_DESC_DMA_MASK);
		if (err)
			goto err_axi_clk;
		/* Sadly, the BM pools all share the same register to
		 * store the high 32 bits of their address. So they
		 * must all have the same high 32 bits, which forces
		 * us to restrict coherent memory to DMA_BIT_MASK(32).
		 */
		err = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (err)
			goto err_axi_clk;
	}

	/* Initialize network controller */
	err = mvpp2_init(pdev, priv);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to initialize controller\n");
		goto err_axi_clk;
	}

	/* Initialize ports */
	fwnode_for_each_available_child_node(fwnode, port_fwnode) {
		err = mvpp2_port_probe(pdev, port_fwnode, priv);
		if (err < 0)
			goto err_port_probe;
	}

	if (priv->port_count == 0) {
		dev_err(&pdev->dev, "no ports enabled\n");
		err = -ENODEV;
		goto err_axi_clk;
	}

	/* Statistics must be gathered regularly because some of them (like
	 * packets counters) are 32-bit registers and could overflow quite
	 * quickly. For instance, a 10Gb link used at full bandwidth with the
	 * smallest packets (64B) will overflow a 32-bit counter in less than
	 * 30 seconds. Then, use a workqueue to fill 64-bit counters.
	 */
	snprintf(priv->queue_name, sizeof(priv->queue_name),
		 "stats-wq-%s%s", netdev_name(priv->port_list[0]->dev),
		 priv->port_count > 1 ? "+" : "");
	priv->stats_queue = create_singlethread_workqueue(priv->queue_name);
	if (!priv->stats_queue) {
		err = -ENOMEM;
		goto err_port_probe;
	}

	mvpp2_dbgfs_init(priv, pdev->name);

	platform_set_drvdata(pdev, priv);
	return 0;

err_port_probe:
	i = 0;
	fwnode_for_each_available_child_node(fwnode, port_fwnode) {
		if (priv->port_list[i])
			mvpp2_port_remove(priv->port_list[i]);
		i++;
	}
err_axi_clk:
	clk_disable_unprepare(priv->axi_clk);

err_mg_core_clk:
	if (priv->hw_version == MVPP22)
		clk_disable_unprepare(priv->mg_core_clk);
err_mg_clk:
	if (priv->hw_version == MVPP22)
		clk_disable_unprepare(priv->mg_clk);
err_gop_clk:
	clk_disable_unprepare(priv->gop_clk);
err_pp_clk:
	clk_disable_unprepare(priv->pp_clk);
	return err;
}

static int mvpp2_remove(struct platform_device *pdev)
{
	struct mvpp2 *priv = platform_get_drvdata(pdev);
	struct fwnode_handle *fwnode = pdev->dev.fwnode;
	struct fwnode_handle *port_fwnode;
	int i = 0;

	mvpp2_dbgfs_cleanup(priv);

	flush_workqueue(priv->stats_queue);
	destroy_workqueue(priv->stats_queue);

	fwnode_for_each_available_child_node(fwnode, port_fwnode) {
		if (priv->port_list[i]) {
			mutex_destroy(&priv->port_list[i]->gather_stats_lock);
			mvpp2_port_remove(priv->port_list[i]);
		}
		i++;
	}

	for (i = 0; i < MVPP2_BM_POOLS_NUM; i++) {
		struct mvpp2_bm_pool *bm_pool = &priv->bm_pools[i];

		mvpp2_bm_pool_destroy(pdev, priv, bm_pool);
	}

	for_each_present_cpu(i) {
		struct mvpp2_tx_queue *aggr_txq = &priv->aggr_txqs[i];

		dma_free_coherent(&pdev->dev,
				  MVPP2_AGGR_TXQ_SIZE * MVPP2_DESC_ALIGNED_SIZE,
				  aggr_txq->descs,
				  aggr_txq->descs_dma);
	}

	if (is_acpi_node(port_fwnode))
		return 0;

	clk_disable_unprepare(priv->axi_clk);
	clk_disable_unprepare(priv->mg_core_clk);
	clk_disable_unprepare(priv->mg_clk);
	clk_disable_unprepare(priv->pp_clk);
	clk_disable_unprepare(priv->gop_clk);

	return 0;
}

static const struct of_device_id mvpp2_match[] = {
	{
		.compatible = "marvell,armada-375-pp2",
		.data = (void *)MVPP21,
	},
	{
		.compatible = "marvell,armada-7k-pp22",
		.data = (void *)MVPP22,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, mvpp2_match);

static const struct acpi_device_id mvpp2_acpi_match[] = {
	{ "MRVL0110", MVPP22 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, mvpp2_acpi_match);

static struct platform_driver mvpp2_driver = {
	.probe = mvpp2_probe,
	.remove = mvpp2_remove,
	.driver = {
		.name = MVPP2_DRIVER_NAME,
		.of_match_table = mvpp2_match,
		.acpi_match_table = ACPI_PTR(mvpp2_acpi_match),
	},
};

module_platform_driver(mvpp2_driver);

MODULE_DESCRIPTION("Marvell PPv2 Ethernet Driver - www.marvell.com");
MODULE_AUTHOR("Marcin Wojtas <mw@semihalf.com>");
MODULE_LICENSE("GPL v2");
