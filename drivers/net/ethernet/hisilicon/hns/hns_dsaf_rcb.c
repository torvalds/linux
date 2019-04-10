/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <asm/cacheflush.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/spinlock.h>

#include "hns_dsaf_main.h"
#include "hns_dsaf_ppe.h"
#include "hns_dsaf_rcb.h"

#define RCB_COMMON_REG_OFFSET 0x80000
#define TX_RING 0
#define RX_RING 1

#define RCB_RESET_WAIT_TIMES 30
#define RCB_RESET_TRY_TIMES 10

/* Because default mtu is 1500, rcb buffer size is set to 2048 enough */
#define RCB_DEFAULT_BUFFER_SIZE 2048

/**
 *hns_rcb_wait_fbd_clean - clean fbd
 *@qs: ring struct pointer array
 *@qnum: num of array
 *@flag: tx or rx flag
 */
void hns_rcb_wait_fbd_clean(struct hnae_queue **qs, int q_num, u32 flag)
{
	int i, wait_cnt;
	u32 fbd_num;

	for (wait_cnt = i = 0; i < q_num; wait_cnt++) {
		usleep_range(200, 300);
		fbd_num = 0;
		if (flag & RCB_INT_FLAG_TX)
			fbd_num += dsaf_read_dev(qs[i],
						 RCB_RING_TX_RING_FBDNUM_REG);
		if (flag & RCB_INT_FLAG_RX)
			fbd_num += dsaf_read_dev(qs[i],
						 RCB_RING_RX_RING_FBDNUM_REG);
		if (!fbd_num)
			i++;
		if (wait_cnt >= 10000)
			break;
	}

	if (i < q_num)
		dev_err(qs[i]->handle->owner_dev,
			"queue(%d) wait fbd(%d) clean fail!!\n", i, fbd_num);
}

int hns_rcb_wait_tx_ring_clean(struct hnae_queue *qs)
{
	u32 head, tail;
	int wait_cnt;

	tail = dsaf_read_dev(&qs->tx_ring, RCB_REG_TAIL);
	wait_cnt = 0;
	while (wait_cnt++ < HNS_MAX_WAIT_CNT) {
		head = dsaf_read_dev(&qs->tx_ring, RCB_REG_HEAD);
		if (tail == head)
			break;

		usleep_range(100, 200);
	}

	if (wait_cnt >= HNS_MAX_WAIT_CNT) {
		dev_err(qs->dev->dev, "rcb wait timeout, head not equal to tail.\n");
		return -EBUSY;
	}

	return 0;
}

/**
 *hns_rcb_reset_ring_hw - ring reset
 *@q: ring struct pointer
 */
void hns_rcb_reset_ring_hw(struct hnae_queue *q)
{
	u32 wait_cnt;
	u32 try_cnt = 0;
	u32 could_ret;

	u32 tx_fbd_num;

	while (try_cnt++ < RCB_RESET_TRY_TIMES) {
		usleep_range(100, 200);
		tx_fbd_num = dsaf_read_dev(q, RCB_RING_TX_RING_FBDNUM_REG);
		if (tx_fbd_num)
			continue;

		dsaf_write_dev(q, RCB_RING_PREFETCH_EN_REG, 0);

		dsaf_write_dev(q, RCB_RING_T0_BE_RST, 1);

		msleep(20);
		could_ret = dsaf_read_dev(q, RCB_RING_COULD_BE_RST);

		wait_cnt = 0;
		while (!could_ret && (wait_cnt < RCB_RESET_WAIT_TIMES)) {
			dsaf_write_dev(q, RCB_RING_T0_BE_RST, 0);

			dsaf_write_dev(q, RCB_RING_T0_BE_RST, 1);

			msleep(20);
			could_ret = dsaf_read_dev(q, RCB_RING_COULD_BE_RST);

			wait_cnt++;
		}

		dsaf_write_dev(q, RCB_RING_T0_BE_RST, 0);

		if (could_ret)
			break;
	}

	if (try_cnt >= RCB_RESET_TRY_TIMES)
		dev_err(q->dev->dev, "port%d reset ring fail\n",
			hns_ae_get_vf_cb(q->handle)->port_index);
}

/**
 *hns_rcb_int_ctrl_hw - rcb irq enable control
 *@q: hnae queue struct pointer
 *@flag:ring flag tx or rx
 *@mask:mask
 */
void hns_rcb_int_ctrl_hw(struct hnae_queue *q, u32 flag, u32 mask)
{
	u32 int_mask_en = !!mask;

	if (flag & RCB_INT_FLAG_TX) {
		dsaf_write_dev(q, RCB_RING_INTMSK_TXWL_REG, int_mask_en);
		dsaf_write_dev(q, RCB_RING_INTMSK_TX_OVERTIME_REG,
			       int_mask_en);
	}

	if (flag & RCB_INT_FLAG_RX) {
		dsaf_write_dev(q, RCB_RING_INTMSK_RXWL_REG, int_mask_en);
		dsaf_write_dev(q, RCB_RING_INTMSK_RX_OVERTIME_REG,
			       int_mask_en);
	}
}

void hns_rcb_int_clr_hw(struct hnae_queue *q, u32 flag)
{
	if (flag & RCB_INT_FLAG_TX) {
		dsaf_write_dev(q, RCB_RING_INTSTS_TX_RING_REG, 1);
		dsaf_write_dev(q, RCB_RING_INTSTS_TX_OVERTIME_REG, 1);
	}

	if (flag & RCB_INT_FLAG_RX) {
		dsaf_write_dev(q, RCB_RING_INTSTS_RX_RING_REG, 1);
		dsaf_write_dev(q, RCB_RING_INTSTS_RX_OVERTIME_REG, 1);
	}
}

void hns_rcbv2_int_ctrl_hw(struct hnae_queue *q, u32 flag, u32 mask)
{
	u32 int_mask_en = !!mask;

	if (flag & RCB_INT_FLAG_TX)
		dsaf_write_dev(q, RCB_RING_INTMSK_TXWL_REG, int_mask_en);

	if (flag & RCB_INT_FLAG_RX)
		dsaf_write_dev(q, RCB_RING_INTMSK_RXWL_REG, int_mask_en);
}

void hns_rcbv2_int_clr_hw(struct hnae_queue *q, u32 flag)
{
	if (flag & RCB_INT_FLAG_TX)
		dsaf_write_dev(q, RCBV2_TX_RING_INT_STS_REG, 1);

	if (flag & RCB_INT_FLAG_RX)
		dsaf_write_dev(q, RCBV2_RX_RING_INT_STS_REG, 1);
}

/**
 *hns_rcb_ring_enable_hw - enable ring
 *@ring: rcb ring
 */
void hns_rcb_ring_enable_hw(struct hnae_queue *q, u32 val)
{
	dsaf_write_dev(q, RCB_RING_PREFETCH_EN_REG, !!val);
}

void hns_rcb_start(struct hnae_queue *q, u32 val)
{
	hns_rcb_ring_enable_hw(q, val);
}

/**
 *hns_rcb_common_init_commit_hw - make rcb common init completed
 *@rcb_common: rcb common device
 */
void hns_rcb_common_init_commit_hw(struct rcb_common_cb *rcb_common)
{
	wmb();	/* Sync point before breakpoint */
	dsaf_write_dev(rcb_common, RCB_COM_CFG_SYS_FSH_REG, 1);
	wmb();	/* Sync point after breakpoint */
}

/* hns_rcb_set_tx_ring_bs - init rcb ring buf size regester
 *@q: hnae_queue
 *@buf_size: buffer size set to hw
 */
void hns_rcb_set_tx_ring_bs(struct hnae_queue *q, u32 buf_size)
{
	u32 bd_size_type = hns_rcb_buf_size2type(buf_size);

	dsaf_write_dev(q, RCB_RING_TX_RING_BD_LEN_REG,
		       bd_size_type);
}

/* hns_rcb_set_rx_ring_bs - init rcb ring buf size regester
 *@q: hnae_queue
 *@buf_size: buffer size set to hw
 */
void hns_rcb_set_rx_ring_bs(struct hnae_queue *q, u32 buf_size)
{
	u32 bd_size_type = hns_rcb_buf_size2type(buf_size);

	dsaf_write_dev(q, RCB_RING_RX_RING_BD_LEN_REG,
		       bd_size_type);
}

/**
 *hns_rcb_ring_init - init rcb ring
 *@ring_pair: ring pair control block
 *@ring_type: ring type, RX_RING or TX_RING
 */
static void hns_rcb_ring_init(struct ring_pair_cb *ring_pair, int ring_type)
{
	struct hnae_queue *q = &ring_pair->q;
	struct hnae_ring *ring =
		(ring_type == RX_RING) ? &q->rx_ring : &q->tx_ring;
	dma_addr_t dma = ring->desc_dma_addr;

	if (ring_type == RX_RING) {
		dsaf_write_dev(q, RCB_RING_RX_RING_BASEADDR_L_REG,
			       (u32)dma);
		dsaf_write_dev(q, RCB_RING_RX_RING_BASEADDR_H_REG,
			       (u32)((dma >> 31) >> 1));

		hns_rcb_set_rx_ring_bs(q, ring->buf_size);

		dsaf_write_dev(q, RCB_RING_RX_RING_BD_NUM_REG,
			       ring_pair->port_id_in_comm);
		dsaf_write_dev(q, RCB_RING_RX_RING_PKTLINE_REG,
			       ring_pair->port_id_in_comm);
	} else {
		dsaf_write_dev(q, RCB_RING_TX_RING_BASEADDR_L_REG,
			       (u32)dma);
		dsaf_write_dev(q, RCB_RING_TX_RING_BASEADDR_H_REG,
			       (u32)((dma >> 31) >> 1));

		hns_rcb_set_tx_ring_bs(q, ring->buf_size);

		dsaf_write_dev(q, RCB_RING_TX_RING_BD_NUM_REG,
			       ring_pair->port_id_in_comm);
		dsaf_write_dev(q, RCB_RING_TX_RING_PKTLINE_REG,
			ring_pair->port_id_in_comm + HNS_RCB_TX_PKTLINE_OFFSET);
	}
}

/**
 *hns_rcb_init_hw - init rcb hardware
 *@ring: rcb ring
 */
void hns_rcb_init_hw(struct ring_pair_cb *ring)
{
	hns_rcb_ring_init(ring, RX_RING);
	hns_rcb_ring_init(ring, TX_RING);
}

/**
 *hns_rcb_set_port_desc_cnt - set rcb port description num
 *@rcb_common: rcb_common device
 *@port_idx:port index
 *@desc_cnt:BD num
 */
static void hns_rcb_set_port_desc_cnt(struct rcb_common_cb *rcb_common,
				      u32 port_idx, u32 desc_cnt)
{
	dsaf_write_dev(rcb_common, RCB_CFG_BD_NUM_REG + port_idx * 4,
		       desc_cnt);
}

static void hns_rcb_set_port_timeout(
	struct rcb_common_cb *rcb_common, u32 port_idx, u32 timeout)
{
	if (AE_IS_VER1(rcb_common->dsaf_dev->dsaf_ver)) {
		dsaf_write_dev(rcb_common, RCB_CFG_OVERTIME_REG,
			       timeout * HNS_RCB_CLK_FREQ_MHZ);
	} else if (!HNS_DSAF_IS_DEBUG(rcb_common->dsaf_dev)) {
		if (timeout > HNS_RCB_DEF_GAP_TIME_USECS)
			dsaf_write_dev(rcb_common,
				       RCB_PORT_INT_GAPTIME_REG + port_idx * 4,
				       HNS_RCB_DEF_GAP_TIME_USECS);
		else
			dsaf_write_dev(rcb_common,
				       RCB_PORT_INT_GAPTIME_REG + port_idx * 4,
				       timeout);

		dsaf_write_dev(rcb_common,
			       RCB_PORT_CFG_OVERTIME_REG + port_idx * 4,
			       timeout);
	} else {
		dsaf_write_dev(rcb_common,
			       RCB_PORT_CFG_OVERTIME_REG + port_idx * 4,
			       timeout);
	}
}

static int hns_rcb_common_get_port_num(struct rcb_common_cb *rcb_common)
{
	if (!HNS_DSAF_IS_DEBUG(rcb_common->dsaf_dev))
		return HNS_RCB_SERVICE_NW_ENGINE_NUM;
	else
		return HNS_RCB_DEBUG_NW_ENGINE_NUM;
}

/*clr rcb comm exception irq**/
static void hns_rcb_comm_exc_irq_en(
			struct rcb_common_cb *rcb_common, int en)
{
	u32 clr_vlue = 0xfffffffful;
	u32 msk_vlue = en ? 0 : 0xfffffffful;

	/* clr int*/
	dsaf_write_dev(rcb_common, RCB_COM_INTSTS_ECC_ERR_REG, clr_vlue);

	dsaf_write_dev(rcb_common, RCB_COM_SF_CFG_RING_STS, clr_vlue);

	dsaf_write_dev(rcb_common, RCB_COM_SF_CFG_BD_RINT_STS, clr_vlue);

	dsaf_write_dev(rcb_common, RCB_COM_RINT_TX_PKT_REG, clr_vlue);
	dsaf_write_dev(rcb_common, RCB_COM_AXI_ERR_STS, clr_vlue);

	/*en msk*/
	dsaf_write_dev(rcb_common, RCB_COM_INTMASK_ECC_ERR_REG, msk_vlue);

	dsaf_write_dev(rcb_common, RCB_COM_SF_CFG_INTMASK_RING, msk_vlue);

	/*for tx bd neednot cacheline, so msk sf_txring_fbd_intmask (bit 1)**/
	dsaf_write_dev(rcb_common, RCB_COM_SF_CFG_INTMASK_BD, msk_vlue | 2);

	dsaf_write_dev(rcb_common, RCB_COM_INTMSK_TX_PKT_REG, msk_vlue);
	dsaf_write_dev(rcb_common, RCB_COM_AXI_WR_ERR_INTMASK, msk_vlue);
}

/**
 *hns_rcb_common_init_hw - init rcb common hardware
 *@rcb_common: rcb_common device
 *retuen 0 - success , negative --fail
 */
int hns_rcb_common_init_hw(struct rcb_common_cb *rcb_common)
{
	u32 reg_val;
	int i;
	int port_num = hns_rcb_common_get_port_num(rcb_common);

	hns_rcb_comm_exc_irq_en(rcb_common, 0);

	reg_val = dsaf_read_dev(rcb_common, RCB_COM_CFG_INIT_FLAG_REG);
	if (0x1 != (reg_val & 0x1)) {
		dev_err(rcb_common->dsaf_dev->dev,
			"RCB_COM_CFG_INIT_FLAG_REG reg = 0x%x\n", reg_val);
		return -EBUSY;
	}

	for (i = 0; i < port_num; i++) {
		hns_rcb_set_port_desc_cnt(rcb_common, i, rcb_common->desc_num);
		hns_rcb_set_rx_coalesced_frames(
			rcb_common, i, HNS_RCB_DEF_RX_COALESCED_FRAMES);
		if (!AE_IS_VER1(rcb_common->dsaf_dev->dsaf_ver) &&
		    !HNS_DSAF_IS_DEBUG(rcb_common->dsaf_dev))
			hns_rcb_set_tx_coalesced_frames(
				rcb_common, i, HNS_RCB_DEF_TX_COALESCED_FRAMES);
		hns_rcb_set_port_timeout(
			rcb_common, i, HNS_RCB_DEF_COALESCED_USECS);
	}

	dsaf_write_dev(rcb_common, RCB_COM_CFG_ENDIAN_REG,
		       HNS_RCB_COMMON_ENDIAN);

	if (AE_IS_VER1(rcb_common->dsaf_dev->dsaf_ver)) {
		dsaf_write_dev(rcb_common, RCB_COM_CFG_FNA_REG, 0x0);
		dsaf_write_dev(rcb_common, RCB_COM_CFG_FA_REG, 0x1);
	} else {
		dsaf_set_dev_bit(rcb_common, RCBV2_COM_CFG_USER_REG,
				 RCB_COM_CFG_FNA_B, false);
		dsaf_set_dev_bit(rcb_common, RCBV2_COM_CFG_USER_REG,
				 RCB_COM_CFG_FA_B, true);
		dsaf_set_dev_bit(rcb_common, RCBV2_COM_CFG_TSO_MODE_REG,
				 RCB_COM_TSO_MODE_B, HNS_TSO_MODE_8BD_32K);
	}

	return 0;
}

int hns_rcb_buf_size2type(u32 buf_size)
{
	int bd_size_type;

	switch (buf_size) {
	case 512:
		bd_size_type = HNS_BD_SIZE_512_TYPE;
		break;
	case 1024:
		bd_size_type = HNS_BD_SIZE_1024_TYPE;
		break;
	case 2048:
		bd_size_type = HNS_BD_SIZE_2048_TYPE;
		break;
	case 4096:
		bd_size_type = HNS_BD_SIZE_4096_TYPE;
		break;
	default:
		bd_size_type = -EINVAL;
	}

	return bd_size_type;
}

static void hns_rcb_ring_get_cfg(struct hnae_queue *q, int ring_type)
{
	struct hnae_ring *ring;
	struct rcb_common_cb *rcb_common;
	struct ring_pair_cb *ring_pair_cb;
	u16 desc_num, mdnum_ppkt;
	bool irq_idx, is_ver1;

	ring_pair_cb = container_of(q, struct ring_pair_cb, q);
	is_ver1 = AE_IS_VER1(ring_pair_cb->rcb_common->dsaf_dev->dsaf_ver);
	if (ring_type == RX_RING) {
		ring = &q->rx_ring;
		ring->io_base = ring_pair_cb->q.io_base;
		irq_idx = HNS_RCB_IRQ_IDX_RX;
		mdnum_ppkt = HNS_RCB_RING_MAX_BD_PER_PKT;
	} else {
		ring = &q->tx_ring;
		ring->io_base = (u8 __iomem *)ring_pair_cb->q.io_base +
			HNS_RCB_TX_REG_OFFSET;
		irq_idx = HNS_RCB_IRQ_IDX_TX;
		mdnum_ppkt = is_ver1 ? HNS_RCB_RING_MAX_TXBD_PER_PKT :
				 HNS_RCBV2_RING_MAX_TXBD_PER_PKT;
	}

	rcb_common = ring_pair_cb->rcb_common;
	desc_num = rcb_common->dsaf_dev->desc_num;

	ring->desc = NULL;
	ring->desc_cb = NULL;

	ring->irq = ring_pair_cb->virq[irq_idx];
	ring->desc_dma_addr = 0;

	ring->buf_size = RCB_DEFAULT_BUFFER_SIZE;
	ring->desc_num = desc_num;
	ring->max_desc_num_per_pkt = mdnum_ppkt;
	ring->max_raw_data_sz_per_desc = HNS_RCB_MAX_PKT_SIZE;
	ring->max_pkt_size = HNS_RCB_MAX_PKT_SIZE;
	ring->next_to_use = 0;
	ring->next_to_clean = 0;
}

static void hns_rcb_ring_pair_get_cfg(struct ring_pair_cb *ring_pair_cb)
{
	ring_pair_cb->q.handle = NULL;

	hns_rcb_ring_get_cfg(&ring_pair_cb->q, RX_RING);
	hns_rcb_ring_get_cfg(&ring_pair_cb->q, TX_RING);
}

static int hns_rcb_get_port_in_comm(
	struct rcb_common_cb *rcb_common, int ring_idx)
{
	return ring_idx / (rcb_common->max_q_per_vf * rcb_common->max_vfn);
}

#define SERVICE_RING_IRQ_IDX(v1) \
	((v1) ? HNS_SERVICE_RING_IRQ_IDX : HNSV2_SERVICE_RING_IRQ_IDX)
static int hns_rcb_get_base_irq_idx(struct rcb_common_cb *rcb_common)
{
	bool is_ver1 = AE_IS_VER1(rcb_common->dsaf_dev->dsaf_ver);

	if (!HNS_DSAF_IS_DEBUG(rcb_common->dsaf_dev))
		return SERVICE_RING_IRQ_IDX(is_ver1);
	else
		return  HNS_DEBUG_RING_IRQ_IDX;
}

#define RCB_COMM_BASE_TO_RING_BASE(base, ringid)\
	((base) + 0x10000 + HNS_RCB_REG_OFFSET * (ringid))
/**
 *hns_rcb_get_cfg - get rcb config
 *@rcb_common: rcb common device
 */
int hns_rcb_get_cfg(struct rcb_common_cb *rcb_common)
{
	struct ring_pair_cb *ring_pair_cb;
	u32 i;
	u32 ring_num = rcb_common->ring_num;
	int base_irq_idx = hns_rcb_get_base_irq_idx(rcb_common);
	struct platform_device *pdev =
		to_platform_device(rcb_common->dsaf_dev->dev);
	bool is_ver1 = AE_IS_VER1(rcb_common->dsaf_dev->dsaf_ver);

	for (i = 0; i < ring_num; i++) {
		ring_pair_cb = &rcb_common->ring_pair_cb[i];
		ring_pair_cb->rcb_common = rcb_common;
		ring_pair_cb->dev = rcb_common->dsaf_dev->dev;
		ring_pair_cb->index = i;
		ring_pair_cb->q.io_base =
			RCB_COMM_BASE_TO_RING_BASE(rcb_common->io_base, i);
		ring_pair_cb->port_id_in_comm =
			hns_rcb_get_port_in_comm(rcb_common, i);
		ring_pair_cb->virq[HNS_RCB_IRQ_IDX_TX] =
		is_ver1 ? platform_get_irq(pdev, base_irq_idx + i * 2) :
			  platform_get_irq(pdev, base_irq_idx + i * 3 + 1);
		ring_pair_cb->virq[HNS_RCB_IRQ_IDX_RX] =
		is_ver1 ? platform_get_irq(pdev, base_irq_idx + i * 2 + 1) :
			  platform_get_irq(pdev, base_irq_idx + i * 3);
		if ((ring_pair_cb->virq[HNS_RCB_IRQ_IDX_TX] == -EPROBE_DEFER) ||
		    (ring_pair_cb->virq[HNS_RCB_IRQ_IDX_RX] == -EPROBE_DEFER))
			return -EPROBE_DEFER;

		ring_pair_cb->q.phy_base =
			RCB_COMM_BASE_TO_RING_BASE(rcb_common->phy_base, i);
		hns_rcb_ring_pair_get_cfg(ring_pair_cb);
	}

	return 0;
}

/**
 *hns_rcb_get_rx_coalesced_frames - get rcb port rx coalesced frames
 *@rcb_common: rcb_common device
 *@port_idx:port id in comm
 *
 *Returns: coalesced_frames
 */
u32 hns_rcb_get_rx_coalesced_frames(
	struct rcb_common_cb *rcb_common, u32 port_idx)
{
	return dsaf_read_dev(rcb_common, RCB_CFG_PKTLINE_REG + port_idx * 4);
}

/**
 *hns_rcb_get_tx_coalesced_frames - get rcb port tx coalesced frames
 *@rcb_common: rcb_common device
 *@port_idx:port id in comm
 *
 *Returns: coalesced_frames
 */
u32 hns_rcb_get_tx_coalesced_frames(
	struct rcb_common_cb *rcb_common, u32 port_idx)
{
	u64 reg;

	reg = RCB_CFG_PKTLINE_REG + (port_idx + HNS_RCB_TX_PKTLINE_OFFSET) * 4;
	return dsaf_read_dev(rcb_common, reg);
}

/**
 *hns_rcb_get_coalesce_usecs - get rcb port coalesced time_out
 *@rcb_common: rcb_common device
 *@port_idx:port id in comm
 *
 *Returns: time_out
 */
u32 hns_rcb_get_coalesce_usecs(
	struct rcb_common_cb *rcb_common, u32 port_idx)
{
	if (AE_IS_VER1(rcb_common->dsaf_dev->dsaf_ver))
		return dsaf_read_dev(rcb_common, RCB_CFG_OVERTIME_REG) /
		       HNS_RCB_CLK_FREQ_MHZ;
	else
		return dsaf_read_dev(rcb_common,
				     RCB_PORT_CFG_OVERTIME_REG + port_idx * 4);
}

/**
 *hns_rcb_set_coalesce_usecs - set rcb port coalesced time_out
 *@rcb_common: rcb_common device
 *@port_idx:port id in comm
 *@timeout:tx/rx time for coalesced time_out
 *
 * Returns:
 * Zero for success, or an error code in case of failure
 */
int hns_rcb_set_coalesce_usecs(
	struct rcb_common_cb *rcb_common, u32 port_idx, u32 timeout)
{
	u32 old_timeout = hns_rcb_get_coalesce_usecs(rcb_common, port_idx);

	if (timeout == old_timeout)
		return 0;

	if (AE_IS_VER1(rcb_common->dsaf_dev->dsaf_ver)) {
		if (!HNS_DSAF_IS_DEBUG(rcb_common->dsaf_dev)) {
			dev_err(rcb_common->dsaf_dev->dev,
				"error: not support coalesce_usecs setting!\n");
			return -EINVAL;
		}
	}
	if (timeout > HNS_RCB_MAX_COALESCED_USECS || timeout == 0) {
		dev_err(rcb_common->dsaf_dev->dev,
			"error: coalesce_usecs setting supports 1~1023us\n");
		return -EINVAL;
	}
	hns_rcb_set_port_timeout(rcb_common, port_idx, timeout);
	return 0;
}

/**
 *hns_rcb_set_tx_coalesced_frames - set rcb coalesced frames
 *@rcb_common: rcb_common device
 *@port_idx:port id in comm
 *@coalesced_frames:tx/rx BD num for coalesced frames
 *
 * Returns:
 * Zero for success, or an error code in case of failure
 */
int hns_rcb_set_tx_coalesced_frames(
	struct rcb_common_cb *rcb_common, u32 port_idx, u32 coalesced_frames)
{
	u32 old_waterline =
		hns_rcb_get_tx_coalesced_frames(rcb_common, port_idx);
	u64 reg;

	if (coalesced_frames == old_waterline)
		return 0;

	if (coalesced_frames != 1) {
		dev_err(rcb_common->dsaf_dev->dev,
			"error: not support tx coalesce_frames setting!\n");
		return -EINVAL;
	}

	reg = RCB_CFG_PKTLINE_REG + (port_idx + HNS_RCB_TX_PKTLINE_OFFSET) * 4;
	dsaf_write_dev(rcb_common, reg,	coalesced_frames);
	return 0;
}

/**
 *hns_rcb_set_rx_coalesced_frames - set rcb rx coalesced frames
 *@rcb_common: rcb_common device
 *@port_idx:port id in comm
 *@coalesced_frames:tx/rx BD num for coalesced frames
 *
 * Returns:
 * Zero for success, or an error code in case of failure
 */
int hns_rcb_set_rx_coalesced_frames(
	struct rcb_common_cb *rcb_common, u32 port_idx, u32 coalesced_frames)
{
	u32 old_waterline =
		hns_rcb_get_rx_coalesced_frames(rcb_common, port_idx);

	if (coalesced_frames == old_waterline)
		return 0;

	if (coalesced_frames >= rcb_common->desc_num ||
	    coalesced_frames > HNS_RCB_MAX_COALESCED_FRAMES ||
	    coalesced_frames < HNS_RCB_MIN_COALESCED_FRAMES) {
		dev_err(rcb_common->dsaf_dev->dev,
			"error: not support coalesce_frames setting!\n");
		return -EINVAL;
	}

	dsaf_write_dev(rcb_common, RCB_CFG_PKTLINE_REG + port_idx * 4,
		       coalesced_frames);
	return 0;
}

/**
 *hns_rcb_get_queue_mode - get max VM number and max ring number per VM
 *						accordding to dsaf mode
 *@dsaf_mode: dsaf mode
 *@max_vfn : max vfn number
 *@max_q_per_vf:max ring number per vm
 */
void hns_rcb_get_queue_mode(enum dsaf_mode dsaf_mode, u16 *max_vfn,
			    u16 *max_q_per_vf)
{
	switch (dsaf_mode) {
	case DSAF_MODE_DISABLE_6PORT_0VM:
		*max_vfn = 1;
		*max_q_per_vf = 16;
		break;
	case DSAF_MODE_DISABLE_FIX:
	case DSAF_MODE_DISABLE_SP:
		*max_vfn = 1;
		*max_q_per_vf = 1;
		break;
	case DSAF_MODE_DISABLE_2PORT_64VM:
		*max_vfn = 64;
		*max_q_per_vf = 1;
		break;
	case DSAF_MODE_DISABLE_6PORT_16VM:
		*max_vfn = 16;
		*max_q_per_vf = 1;
		break;
	default:
		*max_vfn = 1;
		*max_q_per_vf = 16;
		break;
	}
}

static int hns_rcb_get_ring_num(struct dsaf_device *dsaf_dev)
{
	switch (dsaf_dev->dsaf_mode) {
	case DSAF_MODE_ENABLE_FIX:
	case DSAF_MODE_DISABLE_SP:
		return 1;

	case DSAF_MODE_DISABLE_FIX:
		return 6;

	case DSAF_MODE_ENABLE_0VM:
		return 32;

	case DSAF_MODE_DISABLE_6PORT_0VM:
	case DSAF_MODE_ENABLE_16VM:
	case DSAF_MODE_DISABLE_6PORT_2VM:
	case DSAF_MODE_DISABLE_6PORT_16VM:
	case DSAF_MODE_DISABLE_6PORT_4VM:
	case DSAF_MODE_ENABLE_8VM:
		return 96;

	case DSAF_MODE_DISABLE_2PORT_16VM:
	case DSAF_MODE_DISABLE_2PORT_8VM:
	case DSAF_MODE_ENABLE_32VM:
	case DSAF_MODE_DISABLE_2PORT_64VM:
	case DSAF_MODE_ENABLE_128VM:
		return 128;

	default:
		dev_warn(dsaf_dev->dev,
			 "get ring num fail,use default!dsaf_mode=%d\n",
			 dsaf_dev->dsaf_mode);
		return 128;
	}
}

static void __iomem *hns_rcb_common_get_vaddr(struct rcb_common_cb *rcb_common)
{
	struct dsaf_device *dsaf_dev = rcb_common->dsaf_dev;

	return dsaf_dev->ppe_base + RCB_COMMON_REG_OFFSET;
}

static phys_addr_t hns_rcb_common_get_paddr(struct rcb_common_cb *rcb_common)
{
	struct dsaf_device *dsaf_dev = rcb_common->dsaf_dev;

	return dsaf_dev->ppe_paddr + RCB_COMMON_REG_OFFSET;
}

int hns_rcb_common_get_cfg(struct dsaf_device *dsaf_dev,
			   int comm_index)
{
	struct rcb_common_cb *rcb_common;
	enum dsaf_mode dsaf_mode = dsaf_dev->dsaf_mode;
	u16 max_vfn;
	u16 max_q_per_vf;
	int ring_num = hns_rcb_get_ring_num(dsaf_dev);

	rcb_common =
		devm_kzalloc(dsaf_dev->dev,
			     struct_size(rcb_common, ring_pair_cb, ring_num),
			     GFP_KERNEL);
	if (!rcb_common) {
		dev_err(dsaf_dev->dev, "rcb common devm_kzalloc fail!\n");
		return -ENOMEM;
	}
	rcb_common->comm_index = comm_index;
	rcb_common->ring_num = ring_num;
	rcb_common->dsaf_dev = dsaf_dev;

	rcb_common->desc_num = dsaf_dev->desc_num;

	hns_rcb_get_queue_mode(dsaf_mode, &max_vfn, &max_q_per_vf);
	rcb_common->max_vfn = max_vfn;
	rcb_common->max_q_per_vf = max_q_per_vf;

	rcb_common->io_base = hns_rcb_common_get_vaddr(rcb_common);
	rcb_common->phy_base = hns_rcb_common_get_paddr(rcb_common);

	dsaf_dev->rcb_common[comm_index] = rcb_common;
	return 0;
}

void hns_rcb_common_free_cfg(struct dsaf_device *dsaf_dev,
			     u32 comm_index)
{
	dsaf_dev->rcb_common[comm_index] = NULL;
}

void hns_rcb_update_stats(struct hnae_queue *queue)
{
	struct ring_pair_cb *ring =
		container_of(queue, struct ring_pair_cb, q);
	struct dsaf_device *dsaf_dev = ring->rcb_common->dsaf_dev;
	struct ppe_common_cb *ppe_common
		= dsaf_dev->ppe_common[ring->rcb_common->comm_index];
	struct hns_ring_hw_stats *hw_stats = &ring->hw_stats;

	hw_stats->rx_pkts += dsaf_read_dev(queue,
			 RCB_RING_RX_RING_PKTNUM_RECORD_REG);
	dsaf_write_dev(queue, RCB_RING_RX_RING_PKTNUM_RECORD_REG, 0x1);

	hw_stats->ppe_rx_ok_pkts += dsaf_read_dev(ppe_common,
			 PPE_COM_HIS_RX_PKT_QID_OK_CNT_REG + 4 * ring->index);
	hw_stats->ppe_rx_drop_pkts += dsaf_read_dev(ppe_common,
			 PPE_COM_HIS_RX_PKT_QID_DROP_CNT_REG + 4 * ring->index);

	hw_stats->tx_pkts += dsaf_read_dev(queue,
			 RCB_RING_TX_RING_PKTNUM_RECORD_REG);
	dsaf_write_dev(queue, RCB_RING_TX_RING_PKTNUM_RECORD_REG, 0x1);

	hw_stats->ppe_tx_ok_pkts += dsaf_read_dev(ppe_common,
			 PPE_COM_HIS_TX_PKT_QID_OK_CNT_REG + 4 * ring->index);
	hw_stats->ppe_tx_drop_pkts += dsaf_read_dev(ppe_common,
			 PPE_COM_HIS_TX_PKT_QID_ERR_CNT_REG + 4 * ring->index);
}

/**
 *hns_rcb_get_stats - get rcb statistic
 *@ring: rcb ring
 *@data:statistic value
 */
void hns_rcb_get_stats(struct hnae_queue *queue, u64 *data)
{
	u64 *regs_buff = data;
	struct ring_pair_cb *ring =
		container_of(queue, struct ring_pair_cb, q);
	struct hns_ring_hw_stats *hw_stats = &ring->hw_stats;

	regs_buff[0] = hw_stats->tx_pkts;
	regs_buff[1] = hw_stats->ppe_tx_ok_pkts;
	regs_buff[2] = hw_stats->ppe_tx_drop_pkts;
	regs_buff[3] =
		dsaf_read_dev(queue, RCB_RING_TX_RING_FBDNUM_REG);

	regs_buff[4] = queue->tx_ring.stats.tx_pkts;
	regs_buff[5] = queue->tx_ring.stats.tx_bytes;
	regs_buff[6] = queue->tx_ring.stats.tx_err_cnt;
	regs_buff[7] = queue->tx_ring.stats.io_err_cnt;
	regs_buff[8] = queue->tx_ring.stats.sw_err_cnt;
	regs_buff[9] = queue->tx_ring.stats.seg_pkt_cnt;
	regs_buff[10] = queue->tx_ring.stats.restart_queue;
	regs_buff[11] = queue->tx_ring.stats.tx_busy;

	regs_buff[12] = hw_stats->rx_pkts;
	regs_buff[13] = hw_stats->ppe_rx_ok_pkts;
	regs_buff[14] = hw_stats->ppe_rx_drop_pkts;
	regs_buff[15] =
		dsaf_read_dev(queue, RCB_RING_RX_RING_FBDNUM_REG);

	regs_buff[16] = queue->rx_ring.stats.rx_pkts;
	regs_buff[17] = queue->rx_ring.stats.rx_bytes;
	regs_buff[18] = queue->rx_ring.stats.rx_err_cnt;
	regs_buff[19] = queue->rx_ring.stats.io_err_cnt;
	regs_buff[20] = queue->rx_ring.stats.sw_err_cnt;
	regs_buff[21] = queue->rx_ring.stats.seg_pkt_cnt;
	regs_buff[22] = queue->rx_ring.stats.reuse_pg_cnt;
	regs_buff[23] = queue->rx_ring.stats.err_pkt_len;
	regs_buff[24] = queue->rx_ring.stats.non_vld_descs;
	regs_buff[25] = queue->rx_ring.stats.err_bd_num;
	regs_buff[26] = queue->rx_ring.stats.l2_err;
	regs_buff[27] = queue->rx_ring.stats.l3l4_csum_err;
}

/**
 *hns_rcb_get_ring_sset_count - rcb string set count
 *@stringset:ethtool cmd
 *return rcb ring string set count
 */
int hns_rcb_get_ring_sset_count(int stringset)
{
	if (stringset == ETH_SS_STATS)
		return HNS_RING_STATIC_REG_NUM;

	return 0;
}

/**
 *hns_rcb_get_common_regs_count - rcb common regs count
 *return regs count
 */
int hns_rcb_get_common_regs_count(void)
{
	return HNS_RCB_COMMON_DUMP_REG_NUM;
}

/**
 *rcb_get_sset_count - rcb ring regs count
 *return regs count
 */
int hns_rcb_get_ring_regs_count(void)
{
	return HNS_RCB_RING_DUMP_REG_NUM;
}

/**
 *hns_rcb_get_strings - get rcb string set
 *@stringset:string set index
 *@data:strings name value
 *@index:queue index
 */
void hns_rcb_get_strings(int stringset, u8 *data, int index)
{
	char *buff = (char *)data;

	if (stringset != ETH_SS_STATS)
		return;

	snprintf(buff, ETH_GSTRING_LEN, "tx_ring%d_rcb_pkt_num", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "tx_ring%d_ppe_tx_pkt_num", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "tx_ring%d_ppe_drop_pkt_num", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "tx_ring%d_fbd_num", index);
	buff = buff + ETH_GSTRING_LEN;

	snprintf(buff, ETH_GSTRING_LEN, "tx_ring%d_pkt_num", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "tx_ring%d_bytes", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "tx_ring%d_err_cnt", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "tx_ring%d_io_err", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "tx_ring%d_sw_err", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "tx_ring%d_seg_pkt", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "tx_ring%d_restart_queue", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "tx_ring%d_tx_busy", index);
	buff = buff + ETH_GSTRING_LEN;

	snprintf(buff, ETH_GSTRING_LEN, "rx_ring%d_rcb_pkt_num", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "rx_ring%d_ppe_pkt_num", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "rx_ring%d_ppe_drop_pkt_num", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "rx_ring%d_fbd_num", index);
	buff = buff + ETH_GSTRING_LEN;

	snprintf(buff, ETH_GSTRING_LEN, "rx_ring%d_pkt_num", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "rx_ring%d_bytes", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "rx_ring%d_err_cnt", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "rx_ring%d_io_err", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "rx_ring%d_sw_err", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "rx_ring%d_seg_pkt", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "rx_ring%d_reuse_pg", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "rx_ring%d_len_err", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "rx_ring%d_non_vld_desc_err", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "rx_ring%d_bd_num_err", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "rx_ring%d_l2_err", index);
	buff = buff + ETH_GSTRING_LEN;
	snprintf(buff, ETH_GSTRING_LEN, "rx_ring%d_l3l4csum_err", index);
}

void hns_rcb_get_common_regs(struct rcb_common_cb *rcb_com, void *data)
{
	u32 *regs = data;
	bool is_ver1 = AE_IS_VER1(rcb_com->dsaf_dev->dsaf_ver);
	bool is_dbg = HNS_DSAF_IS_DEBUG(rcb_com->dsaf_dev);
	u32 reg_tmp;
	u32 reg_num_tmp;
	u32 i = 0;

	/*rcb common registers */
	regs[0] = dsaf_read_dev(rcb_com, RCB_COM_CFG_ENDIAN_REG);
	regs[1] = dsaf_read_dev(rcb_com, RCB_COM_CFG_SYS_FSH_REG);
	regs[2] = dsaf_read_dev(rcb_com, RCB_COM_CFG_INIT_FLAG_REG);

	regs[3] = dsaf_read_dev(rcb_com, RCB_COM_CFG_PKT_REG);
	regs[4] = dsaf_read_dev(rcb_com, RCB_COM_CFG_RINVLD_REG);
	regs[5] = dsaf_read_dev(rcb_com, RCB_COM_CFG_FNA_REG);
	regs[6] = dsaf_read_dev(rcb_com, RCB_COM_CFG_FA_REG);
	regs[7] = dsaf_read_dev(rcb_com, RCB_COM_CFG_PKT_TC_BP_REG);
	regs[8] = dsaf_read_dev(rcb_com, RCB_COM_CFG_PPE_TNL_CLKEN_REG);

	regs[9] = dsaf_read_dev(rcb_com, RCB_COM_INTMSK_TX_PKT_REG);
	regs[10] = dsaf_read_dev(rcb_com, RCB_COM_RINT_TX_PKT_REG);
	regs[11] = dsaf_read_dev(rcb_com, RCB_COM_INTMASK_ECC_ERR_REG);
	regs[12] = dsaf_read_dev(rcb_com, RCB_COM_INTSTS_ECC_ERR_REG);
	regs[13] = dsaf_read_dev(rcb_com, RCB_COM_EBD_SRAM_ERR_REG);
	regs[14] = dsaf_read_dev(rcb_com, RCB_COM_RXRING_ERR_REG);
	regs[15] = dsaf_read_dev(rcb_com, RCB_COM_TXRING_ERR_REG);
	regs[16] = dsaf_read_dev(rcb_com, RCB_COM_TX_FBD_ERR_REG);
	regs[17] = dsaf_read_dev(rcb_com, RCB_SRAM_ECC_CHK_EN_REG);
	regs[18] = dsaf_read_dev(rcb_com, RCB_SRAM_ECC_CHK0_REG);
	regs[19] = dsaf_read_dev(rcb_com, RCB_SRAM_ECC_CHK1_REG);
	regs[20] = dsaf_read_dev(rcb_com, RCB_SRAM_ECC_CHK2_REG);
	regs[21] = dsaf_read_dev(rcb_com, RCB_SRAM_ECC_CHK3_REG);
	regs[22] = dsaf_read_dev(rcb_com, RCB_SRAM_ECC_CHK4_REG);
	regs[23] = dsaf_read_dev(rcb_com, RCB_SRAM_ECC_CHK5_REG);
	regs[24] = dsaf_read_dev(rcb_com, RCB_ECC_ERR_ADDR0_REG);
	regs[25] = dsaf_read_dev(rcb_com, RCB_ECC_ERR_ADDR3_REG);
	regs[26] = dsaf_read_dev(rcb_com, RCB_ECC_ERR_ADDR4_REG);
	regs[27] = dsaf_read_dev(rcb_com, RCB_ECC_ERR_ADDR5_REG);

	regs[28] = dsaf_read_dev(rcb_com, RCB_COM_SF_CFG_INTMASK_RING);
	regs[29] = dsaf_read_dev(rcb_com, RCB_COM_SF_CFG_RING_STS);
	regs[30] = dsaf_read_dev(rcb_com, RCB_COM_SF_CFG_RING);
	regs[31] = dsaf_read_dev(rcb_com, RCB_COM_SF_CFG_INTMASK_BD);
	regs[32] = dsaf_read_dev(rcb_com, RCB_COM_SF_CFG_BD_RINT_STS);
	regs[33] = dsaf_read_dev(rcb_com, RCB_COM_RCB_RD_BD_BUSY);
	regs[34] = dsaf_read_dev(rcb_com, RCB_COM_RCB_FBD_CRT_EN);
	regs[35] = dsaf_read_dev(rcb_com, RCB_COM_AXI_WR_ERR_INTMASK);
	regs[36] = dsaf_read_dev(rcb_com, RCB_COM_AXI_ERR_STS);
	regs[37] = dsaf_read_dev(rcb_com, RCB_COM_CHK_TX_FBD_NUM_REG);

	/* rcb common entry registers */
	for (i = 0; i < 16; i++) { /* total 16 model registers */
		regs[38 + i]
			= dsaf_read_dev(rcb_com, RCB_CFG_BD_NUM_REG + 4 * i);
		regs[54 + i]
			= dsaf_read_dev(rcb_com, RCB_CFG_PKTLINE_REG + 4 * i);
	}

	reg_tmp = is_ver1 ? RCB_CFG_OVERTIME_REG : RCB_PORT_CFG_OVERTIME_REG;
	reg_num_tmp = (is_ver1 || is_dbg) ? 1 : 6;
	for (i = 0; i < reg_num_tmp; i++)
		regs[70 + i] = dsaf_read_dev(rcb_com, reg_tmp);

	regs[76] = dsaf_read_dev(rcb_com, RCB_CFG_PKTLINE_INT_NUM_REG);
	regs[77] = dsaf_read_dev(rcb_com, RCB_CFG_OVERTIME_INT_NUM_REG);

	/* mark end of rcb common regs */
	for (i = 78; i < 80; i++)
		regs[i] = 0xcccccccc;
}

void hns_rcb_get_ring_regs(struct hnae_queue *queue, void *data)
{
	u32 *regs = data;
	struct ring_pair_cb *ring_pair
		= container_of(queue, struct ring_pair_cb, q);
	u32 i = 0;

	/*rcb ring registers */
	regs[0] = dsaf_read_dev(queue, RCB_RING_RX_RING_BASEADDR_L_REG);
	regs[1] = dsaf_read_dev(queue, RCB_RING_RX_RING_BASEADDR_H_REG);
	regs[2] = dsaf_read_dev(queue, RCB_RING_RX_RING_BD_NUM_REG);
	regs[3] = dsaf_read_dev(queue, RCB_RING_RX_RING_BD_LEN_REG);
	regs[4] = dsaf_read_dev(queue, RCB_RING_RX_RING_PKTLINE_REG);
	regs[5] = dsaf_read_dev(queue, RCB_RING_RX_RING_TAIL_REG);
	regs[6] = dsaf_read_dev(queue, RCB_RING_RX_RING_HEAD_REG);
	regs[7] = dsaf_read_dev(queue, RCB_RING_RX_RING_FBDNUM_REG);
	regs[8] = dsaf_read_dev(queue, RCB_RING_RX_RING_PKTNUM_RECORD_REG);

	regs[9] = dsaf_read_dev(queue, RCB_RING_TX_RING_BASEADDR_L_REG);
	regs[10] = dsaf_read_dev(queue, RCB_RING_TX_RING_BASEADDR_H_REG);
	regs[11] = dsaf_read_dev(queue, RCB_RING_TX_RING_BD_NUM_REG);
	regs[12] = dsaf_read_dev(queue, RCB_RING_TX_RING_BD_LEN_REG);
	regs[13] = dsaf_read_dev(queue, RCB_RING_TX_RING_PKTLINE_REG);
	regs[15] = dsaf_read_dev(queue, RCB_RING_TX_RING_TAIL_REG);
	regs[16] = dsaf_read_dev(queue, RCB_RING_TX_RING_HEAD_REG);
	regs[17] = dsaf_read_dev(queue, RCB_RING_TX_RING_FBDNUM_REG);
	regs[18] = dsaf_read_dev(queue, RCB_RING_TX_RING_OFFSET_REG);
	regs[19] = dsaf_read_dev(queue, RCB_RING_TX_RING_PKTNUM_RECORD_REG);

	regs[20] = dsaf_read_dev(queue, RCB_RING_PREFETCH_EN_REG);
	regs[21] = dsaf_read_dev(queue, RCB_RING_CFG_VF_NUM_REG);
	regs[22] = dsaf_read_dev(queue, RCB_RING_ASID_REG);
	regs[23] = dsaf_read_dev(queue, RCB_RING_RX_VM_REG);
	regs[24] = dsaf_read_dev(queue, RCB_RING_T0_BE_RST);
	regs[25] = dsaf_read_dev(queue, RCB_RING_COULD_BE_RST);
	regs[26] = dsaf_read_dev(queue, RCB_RING_WRR_WEIGHT_REG);

	regs[27] = dsaf_read_dev(queue, RCB_RING_INTMSK_RXWL_REG);
	regs[28] = dsaf_read_dev(queue, RCB_RING_INTSTS_RX_RING_REG);
	regs[29] = dsaf_read_dev(queue, RCB_RING_INTMSK_TXWL_REG);
	regs[30] = dsaf_read_dev(queue, RCB_RING_INTSTS_TX_RING_REG);
	regs[31] = dsaf_read_dev(queue, RCB_RING_INTMSK_RX_OVERTIME_REG);
	regs[32] = dsaf_read_dev(queue, RCB_RING_INTSTS_RX_OVERTIME_REG);
	regs[33] = dsaf_read_dev(queue, RCB_RING_INTMSK_TX_OVERTIME_REG);
	regs[34] = dsaf_read_dev(queue, RCB_RING_INTSTS_TX_OVERTIME_REG);

	/* mark end of ring regs */
	for (i = 35; i < 40; i++)
		regs[i] = 0xcccccc00 + ring_pair->index;
}
