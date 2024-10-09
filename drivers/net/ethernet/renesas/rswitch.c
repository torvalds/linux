// SPDX-License-Identifier: GPL-2.0
/* Renesas Ethernet Switch device driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/net_tstamp.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sys_soc.h>

#include "rswitch.h"

static int rswitch_reg_wait(void __iomem *addr, u32 offs, u32 mask, u32 expected)
{
	u32 val;

	return readl_poll_timeout_atomic(addr + offs, val, (val & mask) == expected,
					 1, RSWITCH_TIMEOUT_US);
}

static void rswitch_modify(void __iomem *addr, enum rswitch_reg reg, u32 clear, u32 set)
{
	iowrite32((ioread32(addr + reg) & ~clear) | set, addr + reg);
}

/* Common Agent block (COMA) */
static void rswitch_reset(struct rswitch_private *priv)
{
	iowrite32(RRC_RR, priv->addr + RRC);
	iowrite32(RRC_RR_CLR, priv->addr + RRC);
}

static void rswitch_clock_enable(struct rswitch_private *priv)
{
	iowrite32(RCEC_ACE_DEFAULT | RCEC_RCE, priv->addr + RCEC);
}

static void rswitch_clock_disable(struct rswitch_private *priv)
{
	iowrite32(RCDC_RCD, priv->addr + RCDC);
}

static bool rswitch_agent_clock_is_enabled(void __iomem *coma_addr,
					   unsigned int port)
{
	u32 val = ioread32(coma_addr + RCEC);

	if (val & RCEC_RCE)
		return (val & BIT(port)) ? true : false;
	else
		return false;
}

static void rswitch_agent_clock_ctrl(void __iomem *coma_addr, unsigned int port,
				     int enable)
{
	u32 val;

	if (enable) {
		val = ioread32(coma_addr + RCEC);
		iowrite32(val | RCEC_RCE | BIT(port), coma_addr + RCEC);
	} else {
		val = ioread32(coma_addr + RCDC);
		iowrite32(val | BIT(port), coma_addr + RCDC);
	}
}

static int rswitch_bpool_config(struct rswitch_private *priv)
{
	u32 val;

	val = ioread32(priv->addr + CABPIRM);
	if (val & CABPIRM_BPR)
		return 0;

	iowrite32(CABPIRM_BPIOG, priv->addr + CABPIRM);

	return rswitch_reg_wait(priv->addr, CABPIRM, CABPIRM_BPR, CABPIRM_BPR);
}

static void rswitch_coma_init(struct rswitch_private *priv)
{
	iowrite32(CABPPFLC_INIT_VALUE, priv->addr + CABPPFLC0);
}

/* R-Switch-2 block (TOP) */
static void rswitch_top_init(struct rswitch_private *priv)
{
	unsigned int i;

	for (i = 0; i < RSWITCH_MAX_NUM_QUEUES; i++)
		iowrite32((i / 16) << (GWCA_INDEX * 8), priv->addr + TPEMIMC7(i));
}

/* Forwarding engine block (MFWD) */
static void rswitch_fwd_init(struct rswitch_private *priv)
{
	unsigned int i;

	/* For ETHA */
	for (i = 0; i < RSWITCH_NUM_PORTS; i++) {
		iowrite32(FWPC0_DEFAULT, priv->addr + FWPC0(i));
		iowrite32(0, priv->addr + FWPBFC(i));
	}

	for (i = 0; i < RSWITCH_NUM_PORTS; i++) {
		iowrite32(priv->rdev[i]->rx_queue->index,
			  priv->addr + FWPBFCSDC(GWCA_INDEX, i));
		iowrite32(BIT(priv->gwca.index), priv->addr + FWPBFC(i));
	}

	/* For GWCA */
	iowrite32(FWPC0_DEFAULT, priv->addr + FWPC0(priv->gwca.index));
	iowrite32(FWPC1_DDE, priv->addr + FWPC1(priv->gwca.index));
	iowrite32(0, priv->addr + FWPBFC(priv->gwca.index));
	iowrite32(GENMASK(RSWITCH_NUM_PORTS - 1, 0), priv->addr + FWPBFC(priv->gwca.index));
}

/* Gateway CPU agent block (GWCA) */
static int rswitch_gwca_change_mode(struct rswitch_private *priv,
				    enum rswitch_gwca_mode mode)
{
	int ret;

	if (!rswitch_agent_clock_is_enabled(priv->addr, priv->gwca.index))
		rswitch_agent_clock_ctrl(priv->addr, priv->gwca.index, 1);

	iowrite32(mode, priv->addr + GWMC);

	ret = rswitch_reg_wait(priv->addr, GWMS, GWMS_OPS_MASK, mode);

	if (mode == GWMC_OPC_DISABLE)
		rswitch_agent_clock_ctrl(priv->addr, priv->gwca.index, 0);

	return ret;
}

static int rswitch_gwca_mcast_table_reset(struct rswitch_private *priv)
{
	iowrite32(GWMTIRM_MTIOG, priv->addr + GWMTIRM);

	return rswitch_reg_wait(priv->addr, GWMTIRM, GWMTIRM_MTR, GWMTIRM_MTR);
}

static int rswitch_gwca_axi_ram_reset(struct rswitch_private *priv)
{
	iowrite32(GWARIRM_ARIOG, priv->addr + GWARIRM);

	return rswitch_reg_wait(priv->addr, GWARIRM, GWARIRM_ARR, GWARIRM_ARR);
}

static bool rswitch_is_any_data_irq(struct rswitch_private *priv, u32 *dis, bool tx)
{
	u32 *mask = tx ? priv->gwca.tx_irq_bits : priv->gwca.rx_irq_bits;
	unsigned int i;

	for (i = 0; i < RSWITCH_NUM_IRQ_REGS; i++) {
		if (dis[i] & mask[i])
			return true;
	}

	return false;
}

static void rswitch_get_data_irq_status(struct rswitch_private *priv, u32 *dis)
{
	unsigned int i;

	for (i = 0; i < RSWITCH_NUM_IRQ_REGS; i++) {
		dis[i] = ioread32(priv->addr + GWDIS(i));
		dis[i] &= ioread32(priv->addr + GWDIE(i));
	}
}

static void rswitch_enadis_data_irq(struct rswitch_private *priv,
				    unsigned int index, bool enable)
{
	u32 offs = enable ? GWDIE(index / 32) : GWDID(index / 32);

	iowrite32(BIT(index % 32), priv->addr + offs);
}

static void rswitch_ack_data_irq(struct rswitch_private *priv,
				 unsigned int index)
{
	u32 offs = GWDIS(index / 32);

	iowrite32(BIT(index % 32), priv->addr + offs);
}

static unsigned int rswitch_next_queue_index(struct rswitch_gwca_queue *gq,
					     bool cur, unsigned int num)
{
	unsigned int index = cur ? gq->cur : gq->dirty;

	if (index + num >= gq->ring_size)
		index = (index + num) % gq->ring_size;
	else
		index += num;

	return index;
}

static unsigned int rswitch_get_num_cur_queues(struct rswitch_gwca_queue *gq)
{
	if (gq->cur >= gq->dirty)
		return gq->cur - gq->dirty;
	else
		return gq->ring_size - gq->dirty + gq->cur;
}

static bool rswitch_is_queue_rxed(struct rswitch_gwca_queue *gq)
{
	struct rswitch_ext_ts_desc *desc = &gq->rx_ring[gq->dirty];

	if ((desc->desc.die_dt & DT_MASK) != DT_FEMPTY)
		return true;

	return false;
}

static int rswitch_gwca_queue_alloc_rx_buf(struct rswitch_gwca_queue *gq,
					   unsigned int start_index,
					   unsigned int num)
{
	unsigned int i, index;

	for (i = 0; i < num; i++) {
		index = (i + start_index) % gq->ring_size;
		if (gq->rx_bufs[index])
			continue;
		gq->rx_bufs[index] = netdev_alloc_frag(RSWITCH_BUF_SIZE);
		if (!gq->rx_bufs[index])
			goto err;
	}

	return 0;

err:
	for (; i-- > 0; ) {
		index = (i + start_index) % gq->ring_size;
		skb_free_frag(gq->rx_bufs[index]);
		gq->rx_bufs[index] = NULL;
	}

	return -ENOMEM;
}

static void rswitch_gwca_queue_free(struct net_device *ndev,
				    struct rswitch_gwca_queue *gq)
{
	unsigned int i;

	if (!gq->dir_tx) {
		dma_free_coherent(ndev->dev.parent,
				  sizeof(struct rswitch_ext_ts_desc) *
				  (gq->ring_size + 1), gq->rx_ring, gq->ring_dma);
		gq->rx_ring = NULL;

		for (i = 0; i < gq->ring_size; i++)
			skb_free_frag(gq->rx_bufs[i]);
		kfree(gq->rx_bufs);
		gq->rx_bufs = NULL;
	} else {
		dma_free_coherent(ndev->dev.parent,
				  sizeof(struct rswitch_ext_desc) *
				  (gq->ring_size + 1), gq->tx_ring, gq->ring_dma);
		gq->tx_ring = NULL;
		kfree(gq->skbs);
		gq->skbs = NULL;
		kfree(gq->unmap_addrs);
		gq->unmap_addrs = NULL;
	}
}

static void rswitch_gwca_ts_queue_free(struct rswitch_private *priv)
{
	struct rswitch_gwca_queue *gq = &priv->gwca.ts_queue;

	dma_free_coherent(&priv->pdev->dev,
			  sizeof(struct rswitch_ts_desc) * (gq->ring_size + 1),
			  gq->ts_ring, gq->ring_dma);
	gq->ts_ring = NULL;
}

static int rswitch_gwca_queue_alloc(struct net_device *ndev,
				    struct rswitch_private *priv,
				    struct rswitch_gwca_queue *gq,
				    bool dir_tx, unsigned int ring_size)
{
	unsigned int i, bit;

	gq->dir_tx = dir_tx;
	gq->ring_size = ring_size;
	gq->ndev = ndev;

	if (!dir_tx) {
		gq->rx_bufs = kcalloc(gq->ring_size, sizeof(*gq->rx_bufs), GFP_KERNEL);
		if (!gq->rx_bufs)
			return -ENOMEM;
		if (rswitch_gwca_queue_alloc_rx_buf(gq, 0, gq->ring_size) < 0)
			goto out;

		gq->rx_ring = dma_alloc_coherent(ndev->dev.parent,
						 sizeof(struct rswitch_ext_ts_desc) *
						 (gq->ring_size + 1), &gq->ring_dma, GFP_KERNEL);
	} else {
		gq->skbs = kcalloc(gq->ring_size, sizeof(*gq->skbs), GFP_KERNEL);
		if (!gq->skbs)
			return -ENOMEM;
		gq->unmap_addrs = kcalloc(gq->ring_size, sizeof(*gq->unmap_addrs), GFP_KERNEL);
		if (!gq->unmap_addrs)
			goto out;
		gq->tx_ring = dma_alloc_coherent(ndev->dev.parent,
						 sizeof(struct rswitch_ext_desc) *
						 (gq->ring_size + 1), &gq->ring_dma, GFP_KERNEL);
	}

	if (!gq->rx_ring && !gq->tx_ring)
		goto out;

	i = gq->index / 32;
	bit = BIT(gq->index % 32);
	if (dir_tx)
		priv->gwca.tx_irq_bits[i] |= bit;
	else
		priv->gwca.rx_irq_bits[i] |= bit;

	return 0;

out:
	rswitch_gwca_queue_free(ndev, gq);

	return -ENOMEM;
}

static void rswitch_desc_set_dptr(struct rswitch_desc *desc, dma_addr_t addr)
{
	desc->dptrl = cpu_to_le32(lower_32_bits(addr));
	desc->dptrh = upper_32_bits(addr) & 0xff;
}

static dma_addr_t rswitch_desc_get_dptr(const struct rswitch_desc *desc)
{
	return __le32_to_cpu(desc->dptrl) | (u64)(desc->dptrh) << 32;
}

static int rswitch_gwca_queue_format(struct net_device *ndev,
				     struct rswitch_private *priv,
				     struct rswitch_gwca_queue *gq)
{
	unsigned int ring_size = sizeof(struct rswitch_ext_desc) * gq->ring_size;
	struct rswitch_ext_desc *desc;
	struct rswitch_desc *linkfix;
	dma_addr_t dma_addr;
	unsigned int i;

	memset(gq->tx_ring, 0, ring_size);
	for (i = 0, desc = gq->tx_ring; i < gq->ring_size; i++, desc++) {
		if (!gq->dir_tx) {
			dma_addr = dma_map_single(ndev->dev.parent,
						  gq->rx_bufs[i] + RSWITCH_HEADROOM,
						  RSWITCH_MAP_BUF_SIZE,
						  DMA_FROM_DEVICE);
			if (dma_mapping_error(ndev->dev.parent, dma_addr))
				goto err;

			desc->desc.info_ds = cpu_to_le16(RSWITCH_DESC_BUF_SIZE);
			rswitch_desc_set_dptr(&desc->desc, dma_addr);
			desc->desc.die_dt = DT_FEMPTY | DIE;
		} else {
			desc->desc.die_dt = DT_EEMPTY | DIE;
		}
	}
	rswitch_desc_set_dptr(&desc->desc, gq->ring_dma);
	desc->desc.die_dt = DT_LINKFIX;

	linkfix = &priv->gwca.linkfix_table[gq->index];
	linkfix->die_dt = DT_LINKFIX;
	rswitch_desc_set_dptr(linkfix, gq->ring_dma);

	iowrite32(GWDCC_BALR | (gq->dir_tx ? GWDCC_DCP(GWCA_IPV_NUM) | GWDCC_DQT : 0) | GWDCC_EDE,
		  priv->addr + GWDCC_OFFS(gq->index));

	return 0;

err:
	if (!gq->dir_tx) {
		for (desc = gq->tx_ring; i-- > 0; desc++) {
			dma_addr = rswitch_desc_get_dptr(&desc->desc);
			dma_unmap_single(ndev->dev.parent, dma_addr,
					 RSWITCH_MAP_BUF_SIZE, DMA_FROM_DEVICE);
		}
	}

	return -ENOMEM;
}

static void rswitch_gwca_ts_queue_fill(struct rswitch_private *priv,
				       unsigned int start_index,
				       unsigned int num)
{
	struct rswitch_gwca_queue *gq = &priv->gwca.ts_queue;
	struct rswitch_ts_desc *desc;
	unsigned int i, index;

	for (i = 0; i < num; i++) {
		index = (i + start_index) % gq->ring_size;
		desc = &gq->ts_ring[index];
		desc->desc.die_dt = DT_FEMPTY_ND | DIE;
	}
}

static int rswitch_gwca_queue_ext_ts_fill(struct net_device *ndev,
					  struct rswitch_gwca_queue *gq,
					  unsigned int start_index,
					  unsigned int num)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_ext_ts_desc *desc;
	unsigned int i, index;
	dma_addr_t dma_addr;

	for (i = 0; i < num; i++) {
		index = (i + start_index) % gq->ring_size;
		desc = &gq->rx_ring[index];
		if (!gq->dir_tx) {
			dma_addr = dma_map_single(ndev->dev.parent,
						  gq->rx_bufs[index] + RSWITCH_HEADROOM,
						  RSWITCH_MAP_BUF_SIZE,
						  DMA_FROM_DEVICE);
			if (dma_mapping_error(ndev->dev.parent, dma_addr))
				goto err;

			desc->desc.info_ds = cpu_to_le16(RSWITCH_DESC_BUF_SIZE);
			rswitch_desc_set_dptr(&desc->desc, dma_addr);
			dma_wmb();
			desc->desc.die_dt = DT_FEMPTY | DIE;
			desc->info1 = cpu_to_le64(INFO1_SPN(rdev->etha->index));
		} else {
			desc->desc.die_dt = DT_EEMPTY | DIE;
		}
	}

	return 0;

err:
	if (!gq->dir_tx) {
		for (; i-- > 0; ) {
			index = (i + start_index) % gq->ring_size;
			desc = &gq->rx_ring[index];
			dma_addr = rswitch_desc_get_dptr(&desc->desc);
			dma_unmap_single(ndev->dev.parent, dma_addr,
					 RSWITCH_MAP_BUF_SIZE, DMA_FROM_DEVICE);
		}
	}

	return -ENOMEM;
}

static int rswitch_gwca_queue_ext_ts_format(struct net_device *ndev,
					    struct rswitch_private *priv,
					    struct rswitch_gwca_queue *gq)
{
	unsigned int ring_size = sizeof(struct rswitch_ext_ts_desc) * gq->ring_size;
	struct rswitch_ext_ts_desc *desc;
	struct rswitch_desc *linkfix;
	int err;

	memset(gq->rx_ring, 0, ring_size);
	err = rswitch_gwca_queue_ext_ts_fill(ndev, gq, 0, gq->ring_size);
	if (err < 0)
		return err;

	desc = &gq->rx_ring[gq->ring_size];	/* Last */
	rswitch_desc_set_dptr(&desc->desc, gq->ring_dma);
	desc->desc.die_dt = DT_LINKFIX;

	linkfix = &priv->gwca.linkfix_table[gq->index];
	linkfix->die_dt = DT_LINKFIX;
	rswitch_desc_set_dptr(linkfix, gq->ring_dma);

	iowrite32(GWDCC_BALR | (gq->dir_tx ? GWDCC_DCP(GWCA_IPV_NUM) | GWDCC_DQT : 0) |
		  GWDCC_ETS | GWDCC_EDE,
		  priv->addr + GWDCC_OFFS(gq->index));

	return 0;
}

static int rswitch_gwca_linkfix_alloc(struct rswitch_private *priv)
{
	unsigned int i, num_queues = priv->gwca.num_queues;
	struct rswitch_gwca *gwca = &priv->gwca;
	struct device *dev = &priv->pdev->dev;

	gwca->linkfix_table_size = sizeof(struct rswitch_desc) * num_queues;
	gwca->linkfix_table = dma_alloc_coherent(dev, gwca->linkfix_table_size,
						 &gwca->linkfix_table_dma, GFP_KERNEL);
	if (!gwca->linkfix_table)
		return -ENOMEM;
	for (i = 0; i < num_queues; i++)
		gwca->linkfix_table[i].die_dt = DT_EOS;

	return 0;
}

static void rswitch_gwca_linkfix_free(struct rswitch_private *priv)
{
	struct rswitch_gwca *gwca = &priv->gwca;

	if (gwca->linkfix_table)
		dma_free_coherent(&priv->pdev->dev, gwca->linkfix_table_size,
				  gwca->linkfix_table, gwca->linkfix_table_dma);
	gwca->linkfix_table = NULL;
}

static int rswitch_gwca_ts_queue_alloc(struct rswitch_private *priv)
{
	struct rswitch_gwca_queue *gq = &priv->gwca.ts_queue;
	struct rswitch_ts_desc *desc;

	gq->ring_size = TS_RING_SIZE;
	gq->ts_ring = dma_alloc_coherent(&priv->pdev->dev,
					 sizeof(struct rswitch_ts_desc) *
					 (gq->ring_size + 1), &gq->ring_dma, GFP_KERNEL);

	if (!gq->ts_ring)
		return -ENOMEM;

	rswitch_gwca_ts_queue_fill(priv, 0, TS_RING_SIZE);
	desc = &gq->ts_ring[gq->ring_size];
	desc->desc.die_dt = DT_LINKFIX;
	rswitch_desc_set_dptr(&desc->desc, gq->ring_dma);
	INIT_LIST_HEAD(&priv->gwca.ts_info_list);

	return 0;
}

static struct rswitch_gwca_queue *rswitch_gwca_get(struct rswitch_private *priv)
{
	struct rswitch_gwca_queue *gq;
	unsigned int index;

	index = find_first_zero_bit(priv->gwca.used, priv->gwca.num_queues);
	if (index >= priv->gwca.num_queues)
		return NULL;
	set_bit(index, priv->gwca.used);
	gq = &priv->gwca.queues[index];
	memset(gq, 0, sizeof(*gq));
	gq->index = index;

	return gq;
}

static void rswitch_gwca_put(struct rswitch_private *priv,
			     struct rswitch_gwca_queue *gq)
{
	clear_bit(gq->index, priv->gwca.used);
}

static int rswitch_txdmac_alloc(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_private *priv = rdev->priv;
	int err;

	rdev->tx_queue = rswitch_gwca_get(priv);
	if (!rdev->tx_queue)
		return -EBUSY;

	err = rswitch_gwca_queue_alloc(ndev, priv, rdev->tx_queue, true, TX_RING_SIZE);
	if (err < 0) {
		rswitch_gwca_put(priv, rdev->tx_queue);
		return err;
	}

	return 0;
}

static void rswitch_txdmac_free(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);

	rswitch_gwca_queue_free(ndev, rdev->tx_queue);
	rswitch_gwca_put(rdev->priv, rdev->tx_queue);
}

static int rswitch_txdmac_init(struct rswitch_private *priv, unsigned int index)
{
	struct rswitch_device *rdev = priv->rdev[index];

	return rswitch_gwca_queue_format(rdev->ndev, priv, rdev->tx_queue);
}

static int rswitch_rxdmac_alloc(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_private *priv = rdev->priv;
	int err;

	rdev->rx_queue = rswitch_gwca_get(priv);
	if (!rdev->rx_queue)
		return -EBUSY;

	err = rswitch_gwca_queue_alloc(ndev, priv, rdev->rx_queue, false, RX_RING_SIZE);
	if (err < 0) {
		rswitch_gwca_put(priv, rdev->rx_queue);
		return err;
	}

	return 0;
}

static void rswitch_rxdmac_free(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);

	rswitch_gwca_queue_free(ndev, rdev->rx_queue);
	rswitch_gwca_put(rdev->priv, rdev->rx_queue);
}

static int rswitch_rxdmac_init(struct rswitch_private *priv, unsigned int index)
{
	struct rswitch_device *rdev = priv->rdev[index];
	struct net_device *ndev = rdev->ndev;

	return rswitch_gwca_queue_ext_ts_format(ndev, priv, rdev->rx_queue);
}

static int rswitch_gwca_hw_init(struct rswitch_private *priv)
{
	unsigned int i;
	int err;

	err = rswitch_gwca_change_mode(priv, GWMC_OPC_DISABLE);
	if (err < 0)
		return err;
	err = rswitch_gwca_change_mode(priv, GWMC_OPC_CONFIG);
	if (err < 0)
		return err;

	err = rswitch_gwca_mcast_table_reset(priv);
	if (err < 0)
		return err;
	err = rswitch_gwca_axi_ram_reset(priv);
	if (err < 0)
		return err;

	iowrite32(GWVCC_VEM_SC_TAG, priv->addr + GWVCC);
	iowrite32(0, priv->addr + GWTTFC);
	iowrite32(lower_32_bits(priv->gwca.linkfix_table_dma), priv->addr + GWDCBAC1);
	iowrite32(upper_32_bits(priv->gwca.linkfix_table_dma), priv->addr + GWDCBAC0);
	iowrite32(lower_32_bits(priv->gwca.ts_queue.ring_dma), priv->addr + GWTDCAC10);
	iowrite32(upper_32_bits(priv->gwca.ts_queue.ring_dma), priv->addr + GWTDCAC00);
	iowrite32(GWMDNC_TSDMN(1) | GWMDNC_TXDMN(0x1e) | GWMDNC_RXDMN(0x1f),
		  priv->addr + GWMDNC);
	iowrite32(GWCA_TS_IRQ_BIT, priv->addr + GWTSDCC0);

	iowrite32(GWTPC_PPPL(GWCA_IPV_NUM), priv->addr + GWTPC0);

	for (i = 0; i < RSWITCH_NUM_PORTS; i++) {
		err = rswitch_rxdmac_init(priv, i);
		if (err < 0)
			return err;
		err = rswitch_txdmac_init(priv, i);
		if (err < 0)
			return err;
	}

	err = rswitch_gwca_change_mode(priv, GWMC_OPC_DISABLE);
	if (err < 0)
		return err;
	return rswitch_gwca_change_mode(priv, GWMC_OPC_OPERATION);
}

static int rswitch_gwca_hw_deinit(struct rswitch_private *priv)
{
	int err;

	err = rswitch_gwca_change_mode(priv, GWMC_OPC_DISABLE);
	if (err < 0)
		return err;
	err = rswitch_gwca_change_mode(priv, GWMC_OPC_RESET);
	if (err < 0)
		return err;

	return rswitch_gwca_change_mode(priv, GWMC_OPC_DISABLE);
}

static int rswitch_gwca_halt(struct rswitch_private *priv)
{
	int err;

	priv->gwca_halt = true;
	err = rswitch_gwca_hw_deinit(priv);
	dev_err(&priv->pdev->dev, "halted (%d)\n", err);

	return err;
}

static struct sk_buff *rswitch_rx_handle_desc(struct net_device *ndev,
					      struct rswitch_gwca_queue *gq,
					      struct rswitch_ext_ts_desc *desc)
{
	dma_addr_t dma_addr = rswitch_desc_get_dptr(&desc->desc);
	u16 pkt_len = le16_to_cpu(desc->desc.info_ds) & RX_DS;
	u8 die_dt = desc->desc.die_dt & DT_MASK;
	struct sk_buff *skb = NULL;

	dma_unmap_single(ndev->dev.parent, dma_addr, RSWITCH_MAP_BUF_SIZE,
			 DMA_FROM_DEVICE);

	/* The RX descriptor order will be one of the following:
	 * - FSINGLE
	 * - FSTART -> FEND
	 * - FSTART -> FMID -> FEND
	 */

	/* Check whether the descriptor is unexpected order */
	switch (die_dt) {
	case DT_FSTART:
	case DT_FSINGLE:
		if (gq->skb_fstart) {
			dev_kfree_skb_any(gq->skb_fstart);
			gq->skb_fstart = NULL;
			ndev->stats.rx_dropped++;
		}
		break;
	case DT_FMID:
	case DT_FEND:
		if (!gq->skb_fstart) {
			ndev->stats.rx_dropped++;
			return NULL;
		}
		break;
	default:
		break;
	}

	/* Handle the descriptor */
	switch (die_dt) {
	case DT_FSTART:
	case DT_FSINGLE:
		skb = build_skb(gq->rx_bufs[gq->cur], RSWITCH_BUF_SIZE);
		if (skb) {
			skb_reserve(skb, RSWITCH_HEADROOM);
			skb_put(skb, pkt_len);
			gq->pkt_len = pkt_len;
			if (die_dt == DT_FSTART) {
				gq->skb_fstart = skb;
				skb = NULL;
			}
		}
		break;
	case DT_FMID:
	case DT_FEND:
		skb_add_rx_frag(gq->skb_fstart, skb_shinfo(gq->skb_fstart)->nr_frags,
				virt_to_page(gq->rx_bufs[gq->cur]),
				offset_in_page(gq->rx_bufs[gq->cur]) + RSWITCH_HEADROOM,
				pkt_len, RSWITCH_BUF_SIZE);
		if (die_dt == DT_FEND) {
			skb = gq->skb_fstart;
			gq->skb_fstart = NULL;
		}
		gq->pkt_len += pkt_len;
		break;
	default:
		netdev_err(ndev, "%s: unexpected value (%x)\n", __func__, die_dt);
		break;
	}

	return skb;
}

static bool rswitch_rx(struct net_device *ndev, int *quota)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_gwca_queue *gq = rdev->rx_queue;
	struct rswitch_ext_ts_desc *desc;
	int limit, boguscnt, ret;
	struct sk_buff *skb;
	unsigned int num;
	u32 get_ts;

	if (*quota <= 0)
		return true;

	boguscnt = min_t(int, gq->ring_size, *quota);
	limit = boguscnt;

	desc = &gq->rx_ring[gq->cur];
	while ((desc->desc.die_dt & DT_MASK) != DT_FEMPTY) {
		dma_rmb();
		skb = rswitch_rx_handle_desc(ndev, gq, desc);
		if (!skb)
			goto out;

		get_ts = rdev->priv->ptp_priv->tstamp_rx_ctrl & RCAR_GEN4_RXTSTAMP_TYPE_V2_L2_EVENT;
		if (get_ts) {
			struct skb_shared_hwtstamps *shhwtstamps;
			struct timespec64 ts;

			shhwtstamps = skb_hwtstamps(skb);
			memset(shhwtstamps, 0, sizeof(*shhwtstamps));
			ts.tv_sec = __le32_to_cpu(desc->ts_sec);
			ts.tv_nsec = __le32_to_cpu(desc->ts_nsec & cpu_to_le32(0x3fffffff));
			shhwtstamps->hwtstamp = timespec64_to_ktime(ts);
		}
		skb->protocol = eth_type_trans(skb, ndev);
		napi_gro_receive(&rdev->napi, skb);
		rdev->ndev->stats.rx_packets++;
		rdev->ndev->stats.rx_bytes += gq->pkt_len;

out:
		gq->rx_bufs[gq->cur] = NULL;
		gq->cur = rswitch_next_queue_index(gq, true, 1);
		desc = &gq->rx_ring[gq->cur];

		if (--boguscnt <= 0)
			break;
	}

	num = rswitch_get_num_cur_queues(gq);
	ret = rswitch_gwca_queue_alloc_rx_buf(gq, gq->dirty, num);
	if (ret < 0)
		goto err;
	ret = rswitch_gwca_queue_ext_ts_fill(ndev, gq, gq->dirty, num);
	if (ret < 0)
		goto err;
	gq->dirty = rswitch_next_queue_index(gq, false, num);

	*quota -= limit - boguscnt;

	return boguscnt <= 0;

err:
	rswitch_gwca_halt(rdev->priv);

	return 0;
}

static void rswitch_tx_free(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_gwca_queue *gq = rdev->tx_queue;
	struct rswitch_ext_desc *desc;
	struct sk_buff *skb;

	for (; rswitch_get_num_cur_queues(gq) > 0;
	     gq->dirty = rswitch_next_queue_index(gq, false, 1)) {
		desc = &gq->tx_ring[gq->dirty];
		if ((desc->desc.die_dt & DT_MASK) != DT_FEMPTY)
			break;

		dma_rmb();
		skb = gq->skbs[gq->dirty];
		if (skb) {
			rdev->ndev->stats.tx_packets++;
			rdev->ndev->stats.tx_bytes += skb->len;
			dma_unmap_single(ndev->dev.parent,
					 gq->unmap_addrs[gq->dirty],
					 skb->len, DMA_TO_DEVICE);
			dev_kfree_skb_any(gq->skbs[gq->dirty]);
			gq->skbs[gq->dirty] = NULL;
		}
		desc->desc.die_dt = DT_EEMPTY;
	}
}

static int rswitch_poll(struct napi_struct *napi, int budget)
{
	struct net_device *ndev = napi->dev;
	struct rswitch_private *priv;
	struct rswitch_device *rdev;
	unsigned long flags;
	int quota = budget;

	rdev = netdev_priv(ndev);
	priv = rdev->priv;

retry:
	rswitch_tx_free(ndev);

	if (rswitch_rx(ndev, &quota))
		goto out;
	else if (rdev->priv->gwca_halt)
		goto err;
	else if (rswitch_is_queue_rxed(rdev->rx_queue))
		goto retry;

	netif_wake_subqueue(ndev, 0);

	if (napi_complete_done(napi, budget - quota)) {
		spin_lock_irqsave(&priv->lock, flags);
		rswitch_enadis_data_irq(priv, rdev->tx_queue->index, true);
		rswitch_enadis_data_irq(priv, rdev->rx_queue->index, true);
		spin_unlock_irqrestore(&priv->lock, flags);
	}

out:
	return budget - quota;

err:
	napi_complete(napi);

	return 0;
}

static void rswitch_queue_interrupt(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);

	if (napi_schedule_prep(&rdev->napi)) {
		spin_lock(&rdev->priv->lock);
		rswitch_enadis_data_irq(rdev->priv, rdev->tx_queue->index, false);
		rswitch_enadis_data_irq(rdev->priv, rdev->rx_queue->index, false);
		spin_unlock(&rdev->priv->lock);
		__napi_schedule(&rdev->napi);
	}
}

static irqreturn_t rswitch_data_irq(struct rswitch_private *priv, u32 *dis)
{
	struct rswitch_gwca_queue *gq;
	unsigned int i, index, bit;

	for (i = 0; i < priv->gwca.num_queues; i++) {
		gq = &priv->gwca.queues[i];
		index = gq->index / 32;
		bit = BIT(gq->index % 32);
		if (!(dis[index] & bit))
			continue;

		rswitch_ack_data_irq(priv, gq->index);
		rswitch_queue_interrupt(gq->ndev);
	}

	return IRQ_HANDLED;
}

static irqreturn_t rswitch_gwca_irq(int irq, void *dev_id)
{
	struct rswitch_private *priv = dev_id;
	u32 dis[RSWITCH_NUM_IRQ_REGS];
	irqreturn_t ret = IRQ_NONE;

	rswitch_get_data_irq_status(priv, dis);

	if (rswitch_is_any_data_irq(priv, dis, true) ||
	    rswitch_is_any_data_irq(priv, dis, false))
		ret = rswitch_data_irq(priv, dis);

	return ret;
}

static int rswitch_gwca_request_irqs(struct rswitch_private *priv)
{
	char *resource_name, *irq_name;
	int i, ret, irq;

	for (i = 0; i < GWCA_NUM_IRQS; i++) {
		resource_name = kasprintf(GFP_KERNEL, GWCA_IRQ_RESOURCE_NAME, i);
		if (!resource_name)
			return -ENOMEM;

		irq = platform_get_irq_byname(priv->pdev, resource_name);
		kfree(resource_name);
		if (irq < 0)
			return irq;

		irq_name = devm_kasprintf(&priv->pdev->dev, GFP_KERNEL,
					  GWCA_IRQ_NAME, i);
		if (!irq_name)
			return -ENOMEM;

		ret = devm_request_irq(&priv->pdev->dev, irq, rswitch_gwca_irq,
				       0, irq_name, priv);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void rswitch_ts(struct rswitch_private *priv)
{
	struct rswitch_gwca_queue *gq = &priv->gwca.ts_queue;
	struct rswitch_gwca_ts_info *ts_info, *ts_info2;
	struct skb_shared_hwtstamps shhwtstamps;
	struct rswitch_ts_desc *desc;
	struct timespec64 ts;
	unsigned int num;
	u32 tag, port;

	desc = &gq->ts_ring[gq->cur];
	while ((desc->desc.die_dt & DT_MASK) != DT_FEMPTY_ND) {
		dma_rmb();

		port = TS_DESC_DPN(__le32_to_cpu(desc->desc.dptrl));
		tag = TS_DESC_TSUN(__le32_to_cpu(desc->desc.dptrl));

		list_for_each_entry_safe(ts_info, ts_info2, &priv->gwca.ts_info_list, list) {
			if (!(ts_info->port == port && ts_info->tag == tag))
				continue;

			memset(&shhwtstamps, 0, sizeof(shhwtstamps));
			ts.tv_sec = __le32_to_cpu(desc->ts_sec);
			ts.tv_nsec = __le32_to_cpu(desc->ts_nsec & cpu_to_le32(0x3fffffff));
			shhwtstamps.hwtstamp = timespec64_to_ktime(ts);
			skb_tstamp_tx(ts_info->skb, &shhwtstamps);
			dev_consume_skb_irq(ts_info->skb);
			list_del(&ts_info->list);
			kfree(ts_info);
			break;
		}

		gq->cur = rswitch_next_queue_index(gq, true, 1);
		desc = &gq->ts_ring[gq->cur];
	}

	num = rswitch_get_num_cur_queues(gq);
	rswitch_gwca_ts_queue_fill(priv, gq->dirty, num);
	gq->dirty = rswitch_next_queue_index(gq, false, num);
}

static irqreturn_t rswitch_gwca_ts_irq(int irq, void *dev_id)
{
	struct rswitch_private *priv = dev_id;

	if (ioread32(priv->addr + GWTSDIS) & GWCA_TS_IRQ_BIT) {
		iowrite32(GWCA_TS_IRQ_BIT, priv->addr + GWTSDIS);
		rswitch_ts(priv);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int rswitch_gwca_ts_request_irqs(struct rswitch_private *priv)
{
	int irq;

	irq = platform_get_irq_byname(priv->pdev, GWCA_TS_IRQ_RESOURCE_NAME);
	if (irq < 0)
		return irq;

	return devm_request_irq(&priv->pdev->dev, irq, rswitch_gwca_ts_irq,
				0, GWCA_TS_IRQ_NAME, priv);
}

/* Ethernet TSN Agent block (ETHA) and Ethernet MAC IP block (RMAC) */
static int rswitch_etha_change_mode(struct rswitch_etha *etha,
				    enum rswitch_etha_mode mode)
{
	int ret;

	if (!rswitch_agent_clock_is_enabled(etha->coma_addr, etha->index))
		rswitch_agent_clock_ctrl(etha->coma_addr, etha->index, 1);

	iowrite32(mode, etha->addr + EAMC);

	ret = rswitch_reg_wait(etha->addr, EAMS, EAMS_OPS_MASK, mode);

	if (mode == EAMC_OPC_DISABLE)
		rswitch_agent_clock_ctrl(etha->coma_addr, etha->index, 0);

	return ret;
}

static void rswitch_etha_read_mac_address(struct rswitch_etha *etha)
{
	u32 mrmac0 = ioread32(etha->addr + MRMAC0);
	u32 mrmac1 = ioread32(etha->addr + MRMAC1);
	u8 *mac = &etha->mac_addr[0];

	mac[0] = (mrmac0 >>  8) & 0xFF;
	mac[1] = (mrmac0 >>  0) & 0xFF;
	mac[2] = (mrmac1 >> 24) & 0xFF;
	mac[3] = (mrmac1 >> 16) & 0xFF;
	mac[4] = (mrmac1 >>  8) & 0xFF;
	mac[5] = (mrmac1 >>  0) & 0xFF;
}

static void rswitch_etha_write_mac_address(struct rswitch_etha *etha, const u8 *mac)
{
	iowrite32((mac[0] << 8) | mac[1], etha->addr + MRMAC0);
	iowrite32((mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5],
		  etha->addr + MRMAC1);
}

static int rswitch_etha_wait_link_verification(struct rswitch_etha *etha)
{
	iowrite32(MLVC_PLV, etha->addr + MLVC);

	return rswitch_reg_wait(etha->addr, MLVC, MLVC_PLV, 0);
}

static void rswitch_rmac_setting(struct rswitch_etha *etha, const u8 *mac)
{
	u32 val;

	rswitch_etha_write_mac_address(etha, mac);

	switch (etha->speed) {
	case 100:
		val = MPIC_LSC_100M;
		break;
	case 1000:
		val = MPIC_LSC_1G;
		break;
	case 2500:
		val = MPIC_LSC_2_5G;
		break;
	default:
		return;
	}

	iowrite32(MPIC_PIS_GMII | val, etha->addr + MPIC);
}

static void rswitch_etha_enable_mii(struct rswitch_etha *etha)
{
	rswitch_modify(etha->addr, MPIC, MPIC_PSMCS_MASK | MPIC_PSMHT_MASK,
		       MPIC_PSMCS(etha->psmcs) | MPIC_PSMHT(0x06));
	rswitch_modify(etha->addr, MPSM, 0, MPSM_MFF_C45);
}

static int rswitch_etha_hw_init(struct rswitch_etha *etha, const u8 *mac)
{
	int err;

	err = rswitch_etha_change_mode(etha, EAMC_OPC_DISABLE);
	if (err < 0)
		return err;
	err = rswitch_etha_change_mode(etha, EAMC_OPC_CONFIG);
	if (err < 0)
		return err;

	iowrite32(EAVCC_VEM_SC_TAG, etha->addr + EAVCC);
	rswitch_rmac_setting(etha, mac);
	rswitch_etha_enable_mii(etha);

	err = rswitch_etha_wait_link_verification(etha);
	if (err < 0)
		return err;

	err = rswitch_etha_change_mode(etha, EAMC_OPC_DISABLE);
	if (err < 0)
		return err;

	return rswitch_etha_change_mode(etha, EAMC_OPC_OPERATION);
}

static int rswitch_etha_set_access(struct rswitch_etha *etha, bool read,
				   int phyad, int devad, int regad, int data)
{
	int pop = read ? MDIO_READ_C45 : MDIO_WRITE_C45;
	u32 val;
	int ret;

	if (devad == 0xffffffff)
		return -ENODEV;

	writel(MMIS1_CLEAR_FLAGS, etha->addr + MMIS1);

	val = MPSM_PSME | MPSM_MFF_C45;
	iowrite32((regad << 16) | (devad << 8) | (phyad << 3) | val, etha->addr + MPSM);

	ret = rswitch_reg_wait(etha->addr, MMIS1, MMIS1_PAACS, MMIS1_PAACS);
	if (ret)
		return ret;

	rswitch_modify(etha->addr, MMIS1, MMIS1_PAACS, MMIS1_PAACS);

	if (read) {
		writel((pop << 13) | (devad << 8) | (phyad << 3) | val, etha->addr + MPSM);

		ret = rswitch_reg_wait(etha->addr, MMIS1, MMIS1_PRACS, MMIS1_PRACS);
		if (ret)
			return ret;

		ret = (ioread32(etha->addr + MPSM) & MPSM_PRD_MASK) >> 16;

		rswitch_modify(etha->addr, MMIS1, MMIS1_PRACS, MMIS1_PRACS);
	} else {
		iowrite32((data << 16) | (pop << 13) | (devad << 8) | (phyad << 3) | val,
			  etha->addr + MPSM);

		ret = rswitch_reg_wait(etha->addr, MMIS1, MMIS1_PWACS, MMIS1_PWACS);
	}

	return ret;
}

static int rswitch_etha_mii_read_c45(struct mii_bus *bus, int addr, int devad,
				     int regad)
{
	struct rswitch_etha *etha = bus->priv;

	return rswitch_etha_set_access(etha, true, addr, devad, regad, 0);
}

static int rswitch_etha_mii_write_c45(struct mii_bus *bus, int addr, int devad,
				      int regad, u16 val)
{
	struct rswitch_etha *etha = bus->priv;

	return rswitch_etha_set_access(etha, false, addr, devad, regad, val);
}

/* Call of_node_put(port) after done */
static struct device_node *rswitch_get_port_node(struct rswitch_device *rdev)
{
	struct device_node *ports, *port;
	int err = 0;
	u32 index;

	ports = of_get_child_by_name(rdev->ndev->dev.parent->of_node,
				     "ethernet-ports");
	if (!ports)
		return NULL;

	for_each_child_of_node(ports, port) {
		err = of_property_read_u32(port, "reg", &index);
		if (err < 0) {
			port = NULL;
			goto out;
		}
		if (index == rdev->etha->index) {
			if (!of_device_is_available(port))
				port = NULL;
			break;
		}
	}

out:
	of_node_put(ports);

	return port;
}

static int rswitch_etha_get_params(struct rswitch_device *rdev)
{
	u32 max_speed;
	int err;

	if (!rdev->np_port)
		return 0;	/* ignored */

	err = of_get_phy_mode(rdev->np_port, &rdev->etha->phy_interface);
	if (err)
		return err;

	err = of_property_read_u32(rdev->np_port, "max-speed", &max_speed);
	if (!err) {
		rdev->etha->speed = max_speed;
		return 0;
	}

	/* if no "max-speed" property, let's use default speed */
	switch (rdev->etha->phy_interface) {
	case PHY_INTERFACE_MODE_MII:
		rdev->etha->speed = SPEED_100;
		break;
	case PHY_INTERFACE_MODE_SGMII:
		rdev->etha->speed = SPEED_1000;
		break;
	case PHY_INTERFACE_MODE_USXGMII:
		rdev->etha->speed = SPEED_2500;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rswitch_mii_register(struct rswitch_device *rdev)
{
	struct device_node *mdio_np;
	struct mii_bus *mii_bus;
	int err;

	mii_bus = mdiobus_alloc();
	if (!mii_bus)
		return -ENOMEM;

	mii_bus->name = "rswitch_mii";
	sprintf(mii_bus->id, "etha%d", rdev->etha->index);
	mii_bus->priv = rdev->etha;
	mii_bus->read_c45 = rswitch_etha_mii_read_c45;
	mii_bus->write_c45 = rswitch_etha_mii_write_c45;
	mii_bus->parent = &rdev->priv->pdev->dev;

	mdio_np = of_get_child_by_name(rdev->np_port, "mdio");
	err = of_mdiobus_register(mii_bus, mdio_np);
	if (err < 0) {
		mdiobus_free(mii_bus);
		goto out;
	}

	rdev->etha->mii = mii_bus;

out:
	of_node_put(mdio_np);

	return err;
}

static void rswitch_mii_unregister(struct rswitch_device *rdev)
{
	if (rdev->etha->mii) {
		mdiobus_unregister(rdev->etha->mii);
		mdiobus_free(rdev->etha->mii);
		rdev->etha->mii = NULL;
	}
}

static void rswitch_adjust_link(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;

	if (phydev->link != rdev->etha->link) {
		phy_print_status(phydev);
		if (phydev->link)
			phy_power_on(rdev->serdes);
		else if (rdev->serdes->power_count)
			phy_power_off(rdev->serdes);

		rdev->etha->link = phydev->link;

		if (!rdev->priv->etha_no_runtime_change &&
		    phydev->speed != rdev->etha->speed) {
			rdev->etha->speed = phydev->speed;

			rswitch_etha_hw_init(rdev->etha, rdev->ndev->dev_addr);
			phy_set_speed(rdev->serdes, rdev->etha->speed);
		}
	}
}

static void rswitch_phy_remove_link_mode(struct rswitch_device *rdev,
					 struct phy_device *phydev)
{
	if (!rdev->priv->etha_no_runtime_change)
		return;

	switch (rdev->etha->speed) {
	case SPEED_2500:
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Full_BIT);
		break;
	case SPEED_1000:
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_2500baseX_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Full_BIT);
		break;
	case SPEED_100:
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_2500baseX_Full_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Full_BIT);
		break;
	default:
		break;
	}

	phy_set_max_speed(phydev, rdev->etha->speed);
}

static int rswitch_phy_device_init(struct rswitch_device *rdev)
{
	struct phy_device *phydev;
	struct device_node *phy;
	int err = -ENOENT;

	if (!rdev->np_port)
		return -ENODEV;

	phy = of_parse_phandle(rdev->np_port, "phy-handle", 0);
	if (!phy)
		return -ENODEV;

	/* Set phydev->host_interfaces before calling of_phy_connect() to
	 * configure the PHY with the information of host_interfaces.
	 */
	phydev = of_phy_find_device(phy);
	if (!phydev)
		goto out;
	__set_bit(rdev->etha->phy_interface, phydev->host_interfaces);
	phydev->mac_managed_pm = true;

	phydev = of_phy_connect(rdev->ndev, phy, rswitch_adjust_link, 0,
				rdev->etha->phy_interface);
	if (!phydev)
		goto out;

	phy_set_max_speed(phydev, SPEED_2500);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Full_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);
	rswitch_phy_remove_link_mode(rdev, phydev);

	phy_attached_info(phydev);

	err = 0;
out:
	of_node_put(phy);

	return err;
}

static void rswitch_phy_device_deinit(struct rswitch_device *rdev)
{
	if (rdev->ndev->phydev)
		phy_disconnect(rdev->ndev->phydev);
}

static int rswitch_serdes_set_params(struct rswitch_device *rdev)
{
	int err;

	err = phy_set_mode_ext(rdev->serdes, PHY_MODE_ETHERNET,
			       rdev->etha->phy_interface);
	if (err < 0)
		return err;

	return phy_set_speed(rdev->serdes, rdev->etha->speed);
}

static int rswitch_ether_port_init_one(struct rswitch_device *rdev)
{
	int err;

	if (!rdev->etha->operated) {
		err = rswitch_etha_hw_init(rdev->etha, rdev->ndev->dev_addr);
		if (err < 0)
			return err;
		if (rdev->priv->etha_no_runtime_change)
			rdev->etha->operated = true;
	}

	err = rswitch_mii_register(rdev);
	if (err < 0)
		return err;

	err = rswitch_phy_device_init(rdev);
	if (err < 0)
		goto err_phy_device_init;

	rdev->serdes = devm_of_phy_get(&rdev->priv->pdev->dev, rdev->np_port, NULL);
	if (IS_ERR(rdev->serdes)) {
		err = PTR_ERR(rdev->serdes);
		goto err_serdes_phy_get;
	}

	err = rswitch_serdes_set_params(rdev);
	if (err < 0)
		goto err_serdes_set_params;

	return 0;

err_serdes_set_params:
err_serdes_phy_get:
	rswitch_phy_device_deinit(rdev);

err_phy_device_init:
	rswitch_mii_unregister(rdev);

	return err;
}

static void rswitch_ether_port_deinit_one(struct rswitch_device *rdev)
{
	rswitch_phy_device_deinit(rdev);
	rswitch_mii_unregister(rdev);
}

static int rswitch_ether_port_init_all(struct rswitch_private *priv)
{
	unsigned int i;
	int err;

	rswitch_for_each_enabled_port(priv, i) {
		err = rswitch_ether_port_init_one(priv->rdev[i]);
		if (err)
			goto err_init_one;
	}

	rswitch_for_each_enabled_port(priv, i) {
		err = phy_init(priv->rdev[i]->serdes);
		if (err)
			goto err_serdes;
	}

	return 0;

err_serdes:
	rswitch_for_each_enabled_port_continue_reverse(priv, i)
		phy_exit(priv->rdev[i]->serdes);
	i = RSWITCH_NUM_PORTS;

err_init_one:
	rswitch_for_each_enabled_port_continue_reverse(priv, i)
		rswitch_ether_port_deinit_one(priv->rdev[i]);

	return err;
}

static void rswitch_ether_port_deinit_all(struct rswitch_private *priv)
{
	unsigned int i;

	for (i = 0; i < RSWITCH_NUM_PORTS; i++) {
		phy_exit(priv->rdev[i]->serdes);
		rswitch_ether_port_deinit_one(priv->rdev[i]);
	}
}

static int rswitch_open(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	unsigned long flags;

	phy_start(ndev->phydev);

	napi_enable(&rdev->napi);
	netif_start_queue(ndev);

	spin_lock_irqsave(&rdev->priv->lock, flags);
	rswitch_enadis_data_irq(rdev->priv, rdev->tx_queue->index, true);
	rswitch_enadis_data_irq(rdev->priv, rdev->rx_queue->index, true);
	spin_unlock_irqrestore(&rdev->priv->lock, flags);

	if (bitmap_empty(rdev->priv->opened_ports, RSWITCH_NUM_PORTS))
		iowrite32(GWCA_TS_IRQ_BIT, rdev->priv->addr + GWTSDIE);

	bitmap_set(rdev->priv->opened_ports, rdev->port, 1);

	return 0;
};

static int rswitch_stop(struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_gwca_ts_info *ts_info, *ts_info2;
	unsigned long flags;

	netif_tx_stop_all_queues(ndev);
	bitmap_clear(rdev->priv->opened_ports, rdev->port, 1);

	if (bitmap_empty(rdev->priv->opened_ports, RSWITCH_NUM_PORTS))
		iowrite32(GWCA_TS_IRQ_BIT, rdev->priv->addr + GWTSDID);

	list_for_each_entry_safe(ts_info, ts_info2, &rdev->priv->gwca.ts_info_list, list) {
		if (ts_info->port != rdev->port)
			continue;
		dev_kfree_skb_irq(ts_info->skb);
		list_del(&ts_info->list);
		kfree(ts_info);
	}

	spin_lock_irqsave(&rdev->priv->lock, flags);
	rswitch_enadis_data_irq(rdev->priv, rdev->tx_queue->index, false);
	rswitch_enadis_data_irq(rdev->priv, rdev->rx_queue->index, false);
	spin_unlock_irqrestore(&rdev->priv->lock, flags);

	phy_stop(ndev->phydev);
	napi_disable(&rdev->napi);

	return 0;
};

static bool rswitch_ext_desc_set_info1(struct rswitch_device *rdev,
				       struct sk_buff *skb,
				       struct rswitch_ext_desc *desc)
{
	desc->info1 = cpu_to_le64(INFO1_DV(BIT(rdev->etha->index)) |
				  INFO1_IPV(GWCA_IPV_NUM) | INFO1_FMT);
	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
		struct rswitch_gwca_ts_info *ts_info;

		ts_info = kzalloc(sizeof(*ts_info), GFP_ATOMIC);
		if (!ts_info)
			return false;

		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		rdev->ts_tag++;
		desc->info1 |= cpu_to_le64(INFO1_TSUN(rdev->ts_tag) | INFO1_TXC);

		ts_info->skb = skb_get(skb);
		ts_info->port = rdev->port;
		ts_info->tag = rdev->ts_tag;
		list_add_tail(&ts_info->list, &rdev->priv->gwca.ts_info_list);

		skb_tx_timestamp(skb);
	}

	return true;
}

static bool rswitch_ext_desc_set(struct rswitch_device *rdev,
				 struct sk_buff *skb,
				 struct rswitch_ext_desc *desc,
				 dma_addr_t dma_addr, u16 len, u8 die_dt)
{
	rswitch_desc_set_dptr(&desc->desc, dma_addr);
	desc->desc.info_ds = cpu_to_le16(len);
	if (!rswitch_ext_desc_set_info1(rdev, skb, desc))
		return false;

	dma_wmb();

	desc->desc.die_dt = die_dt;

	return true;
}

static u8 rswitch_ext_desc_get_die_dt(unsigned int nr_desc, unsigned int index)
{
	if (nr_desc == 1)
		return DT_FSINGLE | DIE;
	if (index == 0)
		return DT_FSTART;
	if (nr_desc - 1 == index)
		return DT_FEND | DIE;
	return DT_FMID;
}

static u16 rswitch_ext_desc_get_len(u8 die_dt, unsigned int orig_len)
{
	switch (die_dt & DT_MASK) {
	case DT_FSINGLE:
	case DT_FEND:
		return (orig_len % RSWITCH_DESC_BUF_SIZE) ?: RSWITCH_DESC_BUF_SIZE;
	case DT_FSTART:
	case DT_FMID:
		return RSWITCH_DESC_BUF_SIZE;
	default:
		return 0;
	}
}

static netdev_tx_t rswitch_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rswitch_gwca_queue *gq = rdev->tx_queue;
	dma_addr_t dma_addr, dma_addr_orig;
	netdev_tx_t ret = NETDEV_TX_OK;
	struct rswitch_ext_desc *desc;
	unsigned int i, nr_desc;
	u8 die_dt;
	u16 len;

	nr_desc = (skb->len - 1) / RSWITCH_DESC_BUF_SIZE + 1;
	if (rswitch_get_num_cur_queues(gq) >= gq->ring_size - nr_desc) {
		netif_stop_subqueue(ndev, 0);
		return NETDEV_TX_BUSY;
	}

	if (skb_put_padto(skb, ETH_ZLEN))
		return ret;

	dma_addr_orig = dma_map_single(ndev->dev.parent, skb->data, skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(ndev->dev.parent, dma_addr_orig))
		goto err_kfree;

	gq->skbs[gq->cur] = skb;
	gq->unmap_addrs[gq->cur] = dma_addr_orig;

	/* DT_FSTART should be set at last. So, this is reverse order. */
	for (i = nr_desc; i-- > 0; ) {
		desc = &gq->tx_ring[rswitch_next_queue_index(gq, true, i)];
		die_dt = rswitch_ext_desc_get_die_dt(nr_desc, i);
		dma_addr = dma_addr_orig + i * RSWITCH_DESC_BUF_SIZE;
		len = rswitch_ext_desc_get_len(die_dt, skb->len);
		if (!rswitch_ext_desc_set(rdev, skb, desc, dma_addr, len, die_dt))
			goto err_unmap;
	}

	wmb();	/* gq->cur must be incremented after die_dt was set */

	gq->cur = rswitch_next_queue_index(gq, true, nr_desc);
	rswitch_modify(rdev->addr, GWTRC(gq->index), 0, BIT(gq->index % 32));

	return ret;

err_unmap:
	dma_unmap_single(ndev->dev.parent, dma_addr_orig, skb->len, DMA_TO_DEVICE);

err_kfree:
	dev_kfree_skb_any(skb);

	return ret;
}

static struct net_device_stats *rswitch_get_stats(struct net_device *ndev)
{
	return &ndev->stats;
}

static int rswitch_hwstamp_get(struct net_device *ndev, struct ifreq *req)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	struct rcar_gen4_ptp_private *ptp_priv;
	struct hwtstamp_config config;

	ptp_priv = rdev->priv->ptp_priv;

	config.flags = 0;
	config.tx_type = ptp_priv->tstamp_tx_ctrl ? HWTSTAMP_TX_ON :
						    HWTSTAMP_TX_OFF;
	switch (ptp_priv->tstamp_rx_ctrl & RCAR_GEN4_RXTSTAMP_TYPE) {
	case RCAR_GEN4_RXTSTAMP_TYPE_V2_L2_EVENT:
		config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		break;
	case RCAR_GEN4_RXTSTAMP_TYPE_ALL:
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		config.rx_filter = HWTSTAMP_FILTER_NONE;
		break;
	}

	return copy_to_user(req->ifr_data, &config, sizeof(config)) ? -EFAULT : 0;
}

static int rswitch_hwstamp_set(struct net_device *ndev, struct ifreq *req)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	u32 tstamp_rx_ctrl = RCAR_GEN4_RXTSTAMP_ENABLED;
	struct hwtstamp_config config;
	u32 tstamp_tx_ctrl;

	if (copy_from_user(&config, req->ifr_data, sizeof(config)))
		return -EFAULT;

	if (config.flags)
		return -EINVAL;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		tstamp_tx_ctrl = 0;
		break;
	case HWTSTAMP_TX_ON:
		tstamp_tx_ctrl = RCAR_GEN4_TXTSTAMP_ENABLED;
		break;
	default:
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		tstamp_rx_ctrl = 0;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
		tstamp_rx_ctrl |= RCAR_GEN4_RXTSTAMP_TYPE_V2_L2_EVENT;
		break;
	default:
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		tstamp_rx_ctrl |= RCAR_GEN4_RXTSTAMP_TYPE_ALL;
		break;
	}

	rdev->priv->ptp_priv->tstamp_tx_ctrl = tstamp_tx_ctrl;
	rdev->priv->ptp_priv->tstamp_rx_ctrl = tstamp_rx_ctrl;

	return copy_to_user(req->ifr_data, &config, sizeof(config)) ? -EFAULT : 0;
}

static int rswitch_eth_ioctl(struct net_device *ndev, struct ifreq *req, int cmd)
{
	if (!netif_running(ndev))
		return -EINVAL;

	switch (cmd) {
	case SIOCGHWTSTAMP:
		return rswitch_hwstamp_get(ndev, req);
	case SIOCSHWTSTAMP:
		return rswitch_hwstamp_set(ndev, req);
	default:
		return phy_mii_ioctl(ndev->phydev, req, cmd);
	}
}

static const struct net_device_ops rswitch_netdev_ops = {
	.ndo_open = rswitch_open,
	.ndo_stop = rswitch_stop,
	.ndo_start_xmit = rswitch_start_xmit,
	.ndo_get_stats = rswitch_get_stats,
	.ndo_eth_ioctl = rswitch_eth_ioctl,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_mac_address = eth_mac_addr,
};

static int rswitch_get_ts_info(struct net_device *ndev, struct kernel_ethtool_ts_info *info)
{
	struct rswitch_device *rdev = netdev_priv(ndev);

	info->phc_index = ptp_clock_index(rdev->priv->ptp_priv->clock);
	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON);
	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) | BIT(HWTSTAMP_FILTER_ALL);

	return 0;
}

static const struct ethtool_ops rswitch_ethtool_ops = {
	.get_ts_info = rswitch_get_ts_info,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

static const struct of_device_id renesas_eth_sw_of_table[] = {
	{ .compatible = "renesas,r8a779f0-ether-switch", },
	{ }
};
MODULE_DEVICE_TABLE(of, renesas_eth_sw_of_table);

static void rswitch_etha_init(struct rswitch_private *priv, unsigned int index)
{
	struct rswitch_etha *etha = &priv->etha[index];

	memset(etha, 0, sizeof(*etha));
	etha->index = index;
	etha->addr = priv->addr + RSWITCH_ETHA_OFFSET + index * RSWITCH_ETHA_SIZE;
	etha->coma_addr = priv->addr;

	/* MPIC.PSMCS = (clk [MHz] / (MDC frequency [MHz] * 2) - 1.
	 * Calculating PSMCS value as MDC frequency = 2.5MHz. So, multiply
	 * both the numerator and the denominator by 10.
	 */
	etha->psmcs = clk_get_rate(priv->clk) / 100000 / (25 * 2) - 1;
}

static int rswitch_device_alloc(struct rswitch_private *priv, unsigned int index)
{
	struct platform_device *pdev = priv->pdev;
	struct rswitch_device *rdev;
	struct net_device *ndev;
	int err;

	if (index >= RSWITCH_NUM_PORTS)
		return -EINVAL;

	ndev = alloc_etherdev_mqs(sizeof(struct rswitch_device), 1, 1);
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ether_setup(ndev);

	rdev = netdev_priv(ndev);
	rdev->ndev = ndev;
	rdev->priv = priv;
	priv->rdev[index] = rdev;
	rdev->port = index;
	rdev->etha = &priv->etha[index];
	rdev->addr = priv->addr;

	ndev->base_addr = (unsigned long)rdev->addr;
	snprintf(ndev->name, IFNAMSIZ, "tsn%d", index);
	ndev->netdev_ops = &rswitch_netdev_ops;
	ndev->ethtool_ops = &rswitch_ethtool_ops;
	ndev->max_mtu = RSWITCH_MAX_MTU;
	ndev->min_mtu = ETH_MIN_MTU;

	netif_napi_add(ndev, &rdev->napi, rswitch_poll);

	rdev->np_port = rswitch_get_port_node(rdev);
	rdev->disabled = !rdev->np_port;
	err = of_get_ethdev_address(rdev->np_port, ndev);
	of_node_put(rdev->np_port);
	if (err) {
		if (is_valid_ether_addr(rdev->etha->mac_addr))
			eth_hw_addr_set(ndev, rdev->etha->mac_addr);
		else
			eth_hw_addr_random(ndev);
	}

	err = rswitch_etha_get_params(rdev);
	if (err < 0)
		goto out_get_params;

	if (rdev->priv->gwca.speed < rdev->etha->speed)
		rdev->priv->gwca.speed = rdev->etha->speed;

	err = rswitch_rxdmac_alloc(ndev);
	if (err < 0)
		goto out_rxdmac;

	err = rswitch_txdmac_alloc(ndev);
	if (err < 0)
		goto out_txdmac;

	return 0;

out_txdmac:
	rswitch_rxdmac_free(ndev);

out_rxdmac:
out_get_params:
	netif_napi_del(&rdev->napi);
	free_netdev(ndev);

	return err;
}

static void rswitch_device_free(struct rswitch_private *priv, unsigned int index)
{
	struct rswitch_device *rdev = priv->rdev[index];
	struct net_device *ndev = rdev->ndev;

	rswitch_txdmac_free(ndev);
	rswitch_rxdmac_free(ndev);
	netif_napi_del(&rdev->napi);
	free_netdev(ndev);
}

static int rswitch_init(struct rswitch_private *priv)
{
	unsigned int i;
	int err;

	for (i = 0; i < RSWITCH_NUM_PORTS; i++)
		rswitch_etha_init(priv, i);

	rswitch_clock_enable(priv);
	for (i = 0; i < RSWITCH_NUM_PORTS; i++)
		rswitch_etha_read_mac_address(&priv->etha[i]);

	rswitch_reset(priv);

	rswitch_clock_enable(priv);
	rswitch_top_init(priv);
	err = rswitch_bpool_config(priv);
	if (err < 0)
		return err;

	rswitch_coma_init(priv);

	err = rswitch_gwca_linkfix_alloc(priv);
	if (err < 0)
		return -ENOMEM;

	err = rswitch_gwca_ts_queue_alloc(priv);
	if (err < 0)
		goto err_ts_queue_alloc;

	for (i = 0; i < RSWITCH_NUM_PORTS; i++) {
		err = rswitch_device_alloc(priv, i);
		if (err < 0) {
			for (; i-- > 0; )
				rswitch_device_free(priv, i);
			goto err_device_alloc;
		}
	}

	rswitch_fwd_init(priv);

	err = rcar_gen4_ptp_register(priv->ptp_priv, RCAR_GEN4_PTP_REG_LAYOUT,
				     clk_get_rate(priv->clk));
	if (err < 0)
		goto err_ptp_register;

	err = rswitch_gwca_request_irqs(priv);
	if (err < 0)
		goto err_gwca_request_irq;

	err = rswitch_gwca_ts_request_irqs(priv);
	if (err < 0)
		goto err_gwca_ts_request_irq;

	err = rswitch_gwca_hw_init(priv);
	if (err < 0)
		goto err_gwca_hw_init;

	err = rswitch_ether_port_init_all(priv);
	if (err)
		goto err_ether_port_init_all;

	rswitch_for_each_enabled_port(priv, i) {
		err = register_netdev(priv->rdev[i]->ndev);
		if (err) {
			rswitch_for_each_enabled_port_continue_reverse(priv, i)
				unregister_netdev(priv->rdev[i]->ndev);
			goto err_register_netdev;
		}
	}

	rswitch_for_each_enabled_port(priv, i)
		netdev_info(priv->rdev[i]->ndev, "MAC address %pM\n",
			    priv->rdev[i]->ndev->dev_addr);

	return 0;

err_register_netdev:
	rswitch_ether_port_deinit_all(priv);

err_ether_port_init_all:
	rswitch_gwca_hw_deinit(priv);

err_gwca_hw_init:
err_gwca_ts_request_irq:
err_gwca_request_irq:
	rcar_gen4_ptp_unregister(priv->ptp_priv);

err_ptp_register:
	for (i = 0; i < RSWITCH_NUM_PORTS; i++)
		rswitch_device_free(priv, i);

err_device_alloc:
	rswitch_gwca_ts_queue_free(priv);

err_ts_queue_alloc:
	rswitch_gwca_linkfix_free(priv);

	return err;
}

static const struct soc_device_attribute rswitch_soc_no_speed_change[]  = {
	{ .soc_id = "r8a779f0", .revision = "ES1.0" },
	{ /* Sentinel */ }
};

static int renesas_eth_sw_probe(struct platform_device *pdev)
{
	const struct soc_device_attribute *attr;
	struct rswitch_private *priv;
	struct resource *res;
	int ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "secure_base");
	if (!res) {
		dev_err(&pdev->dev, "invalid resource\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	spin_lock_init(&priv->lock);

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	attr = soc_device_match(rswitch_soc_no_speed_change);
	if (attr)
		priv->etha_no_runtime_change = true;

	priv->ptp_priv = rcar_gen4_ptp_alloc(pdev);
	if (!priv->ptp_priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);
	priv->pdev = pdev;
	priv->addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->addr))
		return PTR_ERR(priv->addr);

	priv->ptp_priv->addr = priv->addr + RCAR_GEN4_GPTP_OFFSET_S4;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(40));
	if (ret < 0) {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret < 0)
			return ret;
	}

	priv->gwca.index = AGENT_INDEX_GWCA;
	priv->gwca.num_queues = min(RSWITCH_NUM_PORTS * NUM_QUEUES_PER_NDEV,
				    RSWITCH_MAX_NUM_QUEUES);
	priv->gwca.queues = devm_kcalloc(&pdev->dev, priv->gwca.num_queues,
					 sizeof(*priv->gwca.queues), GFP_KERNEL);
	if (!priv->gwca.queues)
		return -ENOMEM;

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	ret = rswitch_init(priv);
	if (ret < 0) {
		pm_runtime_put(&pdev->dev);
		pm_runtime_disable(&pdev->dev);
		return ret;
	}

	device_set_wakeup_capable(&pdev->dev, 1);

	return ret;
}

static void rswitch_deinit(struct rswitch_private *priv)
{
	unsigned int i;

	rswitch_gwca_hw_deinit(priv);
	rcar_gen4_ptp_unregister(priv->ptp_priv);

	rswitch_for_each_enabled_port(priv, i) {
		struct rswitch_device *rdev = priv->rdev[i];

		unregister_netdev(rdev->ndev);
		rswitch_ether_port_deinit_one(rdev);
		phy_exit(priv->rdev[i]->serdes);
	}

	for (i = 0; i < RSWITCH_NUM_PORTS; i++)
		rswitch_device_free(priv, i);

	rswitch_gwca_ts_queue_free(priv);
	rswitch_gwca_linkfix_free(priv);

	rswitch_clock_disable(priv);
}

static void renesas_eth_sw_remove(struct platform_device *pdev)
{
	struct rswitch_private *priv = platform_get_drvdata(pdev);

	rswitch_deinit(priv);

	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	platform_set_drvdata(pdev, NULL);
}

static int renesas_eth_sw_suspend(struct device *dev)
{
	struct rswitch_private *priv = dev_get_drvdata(dev);
	struct net_device *ndev;
	unsigned int i;

	rswitch_for_each_enabled_port(priv, i) {
		ndev = priv->rdev[i]->ndev;
		if (netif_running(ndev)) {
			netif_device_detach(ndev);
			rswitch_stop(ndev);
		}
		if (priv->rdev[i]->serdes->init_count)
			phy_exit(priv->rdev[i]->serdes);
	}

	return 0;
}

static int renesas_eth_sw_resume(struct device *dev)
{
	struct rswitch_private *priv = dev_get_drvdata(dev);
	struct net_device *ndev;
	unsigned int i;

	rswitch_for_each_enabled_port(priv, i) {
		phy_init(priv->rdev[i]->serdes);
		ndev = priv->rdev[i]->ndev;
		if (netif_running(ndev)) {
			rswitch_open(ndev);
			netif_device_attach(ndev);
		}
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(renesas_eth_sw_pm_ops, renesas_eth_sw_suspend,
				renesas_eth_sw_resume);

static struct platform_driver renesas_eth_sw_driver_platform = {
	.probe = renesas_eth_sw_probe,
	.remove = renesas_eth_sw_remove,
	.driver = {
		.name = "renesas_eth_sw",
		.pm = pm_sleep_ptr(&renesas_eth_sw_pm_ops),
		.of_match_table = renesas_eth_sw_of_table,
	}
};
module_platform_driver(renesas_eth_sw_driver_platform);
MODULE_AUTHOR("Yoshihiro Shimoda");
MODULE_DESCRIPTION("Renesas Ethernet Switch device driver");
MODULE_LICENSE("GPL");
