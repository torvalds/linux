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
	u32 clr = 1;

	if (flag & RCB_INT_FLAG_TX) {
		dsaf_write_dev(q, RCB_RING_INTSTS_TX_RING_REG, clr);
		dsaf_write_dev(q, RCB_RING_INTSTS_TX_OVERTIME_REG, clr);
	}

	if (flag & RCB_INT_FLAG_RX) {
		dsaf_write_dev(q, RCB_RING_INTSTS_RX_RING_REG, clr);
		dsaf_write_dev(q, RCB_RING_INTSTS_RX_OVERTIME_REG, clr);
	}
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

/**
 *hns_rcb_ring_init - init rcb ring
 *@ring_pair: ring pair control block
 *@ring_type: ring type, RX_RING or TX_RING
 */
static void hns_rcb_ring_init(struct ring_pair_cb *ring_pair, int ring_type)
{
	struct hnae_queue *q = &ring_pair->q;
	struct rcb_common_cb *rcb_common = ring_pair->rcb_common;
	u32 bd_size_type = rcb_common->dsaf_dev->buf_size_type;
	struct hnae_ring *ring =
		(ring_type == RX_RING) ? &q->rx_ring : &q->tx_ring;
	dma_addr_t dma = ring->desc_dma_addr;

	if (ring_type == RX_RING) {
		dsaf_write_dev(q, RCB_RING_RX_RING_BASEADDR_L_REG,
			       (u32)dma);
		dsaf_write_dev(q, RCB_RING_RX_RING_BASEADDR_H_REG,
			       (u32)((dma >> 31) >> 1));
		dsaf_write_dev(q, RCB_RING_RX_RING_BD_LEN_REG,
			       bd_size_type);
		dsaf_write_dev(q, RCB_RING_RX_RING_BD_NUM_REG,
			       ring_pair->port_id_in_dsa);
		dsaf_write_dev(q, RCB_RING_RX_RING_PKTLINE_REG,
			       ring_pair->port_id_in_dsa);
	} else {
		dsaf_write_dev(q, RCB_RING_TX_RING_BASEADDR_L_REG,
			       (u32)dma);
		dsaf_write_dev(q, RCB_RING_TX_RING_BASEADDR_H_REG,
			       (u32)((dma >> 31) >> 1));
		dsaf_write_dev(q, RCB_RING_TX_RING_BD_LEN_REG,
			       bd_size_type);
		dsaf_write_dev(q, RCB_RING_TX_RING_BD_NUM_REG,
			       ring_pair->port_id_in_dsa);
		dsaf_write_dev(q, RCB_RING_TX_RING_PKTLINE_REG,
			       ring_pair->port_id_in_dsa);
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
	if (port_idx >= HNS_RCB_SERVICE_NW_ENGINE_NUM)
		port_idx = 0;

	dsaf_write_dev(rcb_common, RCB_CFG_BD_NUM_REG + port_idx * 4,
		       desc_cnt);
}

/**
 *hns_rcb_set_port_coalesced_frames - set rcb port coalesced frames
 *@rcb_common: rcb_common device
 *@port_idx:port index
 *@coalesced_frames:BD num for coalesced frames
 */
static int  hns_rcb_set_port_coalesced_frames(struct rcb_common_cb *rcb_common,
					      u32 port_idx,
					      u32 coalesced_frames)
{
	if (port_idx >= HNS_RCB_SERVICE_NW_ENGINE_NUM)
		port_idx = 0;
	if (coalesced_frames >= rcb_common->desc_num ||
	    coalesced_frames > HNS_RCB_MAX_COALESCED_FRAMES)
		return -EINVAL;

	dsaf_write_dev(rcb_common, RCB_CFG_PKTLINE_REG + port_idx * 4,
		       coalesced_frames);
	return 0;
}

/**
 *hns_rcb_get_port_coalesced_frames - set rcb port coalesced frames
 *@rcb_common: rcb_common device
 *@port_idx:port index
 * return coaleseced frames value
 */
static u32 hns_rcb_get_port_coalesced_frames(struct rcb_common_cb *rcb_common,
					     u32 port_idx)
{
	if (port_idx >= HNS_RCB_SERVICE_NW_ENGINE_NUM)
		port_idx = 0;

	return dsaf_read_dev(rcb_common,
			     RCB_CFG_PKTLINE_REG + port_idx * 4);
}

/**
 *hns_rcb_set_timeout - set rcb port coalesced time_out
 *@rcb_common: rcb_common device
 *@time_out:time for coalesced time_out
 */
static void hns_rcb_set_timeout(struct rcb_common_cb *rcb_common,
				u32 timeout)
{
	dsaf_write_dev(rcb_common, RCB_CFG_OVERTIME_REG, timeout);
}

static int hns_rcb_common_get_port_num(struct rcb_common_cb *rcb_common)
{
	if (rcb_common->comm_index == HNS_DSAF_COMM_SERVICE_NW_IDX)
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
		(void)hns_rcb_set_port_coalesced_frames(
			rcb_common, i, rcb_common->coalesced_frames);
	}
	hns_rcb_set_timeout(rcb_common, rcb_common->timeout);

	dsaf_write_dev(rcb_common, RCB_COM_CFG_ENDIAN_REG,
		       HNS_RCB_COMMON_ENDIAN);

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
	u32 buf_size;
	u16 desc_num;
	int irq_idx;

	ring_pair_cb = container_of(q, struct ring_pair_cb, q);
	if (ring_type == RX_RING) {
		ring = &q->rx_ring;
		ring->io_base = ring_pair_cb->q.io_base;
		irq_idx = HNS_RCB_IRQ_IDX_RX;
	} else {
		ring = &q->tx_ring;
		ring->io_base = (u8 __iomem *)ring_pair_cb->q.io_base +
			HNS_RCB_TX_REG_OFFSET;
		irq_idx = HNS_RCB_IRQ_IDX_TX;
	}

	rcb_common = ring_pair_cb->rcb_common;
	buf_size = rcb_common->dsaf_dev->buf_size;
	desc_num = rcb_common->dsaf_dev->desc_num;

	ring->desc = NULL;
	ring->desc_cb = NULL;

	ring->irq = ring_pair_cb->virq[irq_idx];
	ring->desc_dma_addr = 0;

	ring->buf_size = buf_size;
	ring->desc_num = desc_num;
	ring->max_desc_num_per_pkt = HNS_RCB_RING_MAX_BD_PER_PKT;
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

static int hns_rcb_get_port(struct rcb_common_cb *rcb_common, int ring_idx)
{
	int comm_index = rcb_common->comm_index;
	int port;
	int q_num;

	if (comm_index == HNS_DSAF_COMM_SERVICE_NW_IDX) {
		q_num = (int)rcb_common->max_q_per_vf * rcb_common->max_vfn;
		port = ring_idx / q_num;
	} else {
		port = HNS_RCB_SERVICE_NW_ENGINE_NUM + comm_index - 1;
	}

	return port;
}

static int hns_rcb_get_base_irq_idx(struct rcb_common_cb *rcb_common)
{
	int comm_index = rcb_common->comm_index;

	if (comm_index == HNS_DSAF_COMM_SERVICE_NW_IDX)
		return HNS_SERVICE_RING_IRQ_IDX;
	else
		return HNS_DEBUG_RING_IRQ_IDX + (comm_index - 1) * 2;
}

#define RCB_COMM_BASE_TO_RING_BASE(base, ringid)\
	((base) + 0x10000 + HNS_RCB_REG_OFFSET * (ringid))
/**
 *hns_rcb_get_cfg - get rcb config
 *@rcb_common: rcb common device
 */
void hns_rcb_get_cfg(struct rcb_common_cb *rcb_common)
{
	struct ring_pair_cb *ring_pair_cb;
	u32 i;
	u32 ring_num = rcb_common->ring_num;
	int base_irq_idx = hns_rcb_get_base_irq_idx(rcb_common);
	struct device_node *np = rcb_common->dsaf_dev->dev->of_node;

	for (i = 0; i < ring_num; i++) {
		ring_pair_cb = &rcb_common->ring_pair_cb[i];
		ring_pair_cb->rcb_common = rcb_common;
		ring_pair_cb->dev = rcb_common->dsaf_dev->dev;
		ring_pair_cb->index = i;
		ring_pair_cb->q.io_base =
			RCB_COMM_BASE_TO_RING_BASE(rcb_common->io_base, i);
		ring_pair_cb->port_id_in_dsa = hns_rcb_get_port(rcb_common, i);
		ring_pair_cb->virq[HNS_RCB_IRQ_IDX_TX]
			= irq_of_parse_and_map(np, base_irq_idx + i * 2);
		ring_pair_cb->virq[HNS_RCB_IRQ_IDX_RX]
			= irq_of_parse_and_map(np, base_irq_idx + i * 2 + 1);
		ring_pair_cb->q.phy_base =
			RCB_COMM_BASE_TO_RING_BASE(rcb_common->phy_base, i);
		hns_rcb_ring_pair_get_cfg(ring_pair_cb);
	}
}

/**
 *hns_rcb_get_coalesced_frames - get rcb port coalesced frames
 *@rcb_common: rcb_common device
 *@comm_index:port index
 *return coalesced_frames
 */
u32 hns_rcb_get_coalesced_frames(struct dsaf_device *dsaf_dev, int port)
{
	int comm_index =  hns_dsaf_get_comm_idx_by_port(port);
	struct rcb_common_cb *rcb_comm = dsaf_dev->rcb_common[comm_index];

	return hns_rcb_get_port_coalesced_frames(rcb_comm, port);
}

/**
 *hns_rcb_get_coalesce_usecs - get rcb port coalesced time_out
 *@rcb_common: rcb_common device
 *@comm_index:port index
 *return time_out
 */
u32 hns_rcb_get_coalesce_usecs(struct dsaf_device *dsaf_dev, int comm_index)
{
	struct rcb_common_cb *rcb_comm = dsaf_dev->rcb_common[comm_index];

	return rcb_comm->timeout;
}

/**
 *hns_rcb_set_coalesce_usecs - set rcb port coalesced time_out
 *@rcb_common: rcb_common device
 *@comm_index: comm :index
 *@etx_usecs:tx time for coalesced time_out
 *@rx_usecs:rx time for coalesced time_out
 */
void hns_rcb_set_coalesce_usecs(struct dsaf_device *dsaf_dev,
				int port, u32 timeout)
{
	int comm_index =  hns_dsaf_get_comm_idx_by_port(port);
	struct rcb_common_cb *rcb_comm = dsaf_dev->rcb_common[comm_index];

	if (rcb_comm->timeout == timeout)
		return;

	if (comm_index == HNS_DSAF_COMM_SERVICE_NW_IDX) {
		dev_err(dsaf_dev->dev,
			"error: not support coalesce_usecs setting!\n");
		return;
	}
	rcb_comm->timeout = timeout;
	hns_rcb_set_timeout(rcb_comm, rcb_comm->timeout);
}

/**
 *hns_rcb_set_coalesced_frames - set rcb coalesced frames
 *@rcb_common: rcb_common device
 *@tx_frames:tx BD num for coalesced frames
 *@rx_frames:rx BD num for coalesced frames
 *Return 0 on success, negative on failure
 */
int hns_rcb_set_coalesced_frames(struct dsaf_device *dsaf_dev,
				 int port, u32 coalesced_frames)
{
	int comm_index =  hns_dsaf_get_comm_idx_by_port(port);
	struct rcb_common_cb *rcb_comm = dsaf_dev->rcb_common[comm_index];
	u32 coalesced_reg_val;
	int ret;

	coalesced_reg_val = hns_rcb_get_port_coalesced_frames(rcb_comm, port);

	if (coalesced_reg_val == coalesced_frames)
		return 0;

	if (coalesced_frames >= HNS_RCB_MIN_COALESCED_FRAMES) {
		ret = hns_rcb_set_port_coalesced_frames(rcb_comm, port,
							coalesced_frames);
		return ret;
	} else {
		return -EINVAL;
	}
}

/**
 *hns_rcb_get_queue_mode - get max VM number and max ring number per VM
 *						accordding to dsaf mode
 *@dsaf_mode: dsaf mode
 *@max_vfn : max vfn number
 *@max_q_per_vf:max ring number per vm
 */
void hns_rcb_get_queue_mode(enum dsaf_mode dsaf_mode, int comm_index,
			    u16 *max_vfn, u16 *max_q_per_vf)
{
	if (comm_index == HNS_DSAF_COMM_SERVICE_NW_IDX) {
		switch (dsaf_mode) {
		case DSAF_MODE_DISABLE_6PORT_0VM:
			*max_vfn = 1;
			*max_q_per_vf = 16;
			break;
		case DSAF_MODE_DISABLE_FIX:
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
	} else {
		*max_vfn = 1;
		*max_q_per_vf = 1;
	}
}

int hns_rcb_get_ring_num(struct dsaf_device *dsaf_dev, int comm_index)
{
	if (comm_index == HNS_DSAF_COMM_SERVICE_NW_IDX) {
		switch (dsaf_dev->dsaf_mode) {
		case DSAF_MODE_ENABLE_FIX:
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
	} else {
		return 1;
	}
}

void __iomem *hns_rcb_common_get_vaddr(struct dsaf_device *dsaf_dev,
				       int comm_index)
{
	void __iomem *base_addr;

	if (comm_index == HNS_DSAF_COMM_SERVICE_NW_IDX)
		base_addr = dsaf_dev->ppe_base + RCB_COMMON_REG_OFFSET;
	else
		base_addr = dsaf_dev->sds_base
			+ (comm_index - 1) * HNS_DSAF_DEBUG_NW_REG_OFFSET
			+ RCB_COMMON_REG_OFFSET;

	return base_addr;
}

static phys_addr_t hns_rcb_common_get_paddr(struct dsaf_device *dsaf_dev,
					    int comm_index)
{
	struct device_node *np = dsaf_dev->dev->of_node;
	phys_addr_t phy_addr;
	const __be32 *tmp_addr;
	u64 addr_offset = 0;
	u64 size = 0;
	int index = 0;

	if (comm_index == HNS_DSAF_COMM_SERVICE_NW_IDX) {
		index    = 2;
		addr_offset = RCB_COMMON_REG_OFFSET;
	} else {
		index    = 1;
		addr_offset = (comm_index - 1) * HNS_DSAF_DEBUG_NW_REG_OFFSET +
				RCB_COMMON_REG_OFFSET;
	}
	tmp_addr  = of_get_address(np, index, &size, NULL);
	phy_addr  = of_translate_address(np, tmp_addr);
	return phy_addr + addr_offset;
}

int hns_rcb_common_get_cfg(struct dsaf_device *dsaf_dev,
			   int comm_index)
{
	struct rcb_common_cb *rcb_common;
	enum dsaf_mode dsaf_mode = dsaf_dev->dsaf_mode;
	u16 max_vfn;
	u16 max_q_per_vf;
	int ring_num = hns_rcb_get_ring_num(dsaf_dev, comm_index);

	rcb_common =
		devm_kzalloc(dsaf_dev->dev, sizeof(*rcb_common) +
			ring_num * sizeof(struct ring_pair_cb), GFP_KERNEL);
	if (!rcb_common) {
		dev_err(dsaf_dev->dev, "rcb common devm_kzalloc fail!\n");
		return -ENOMEM;
	}
	rcb_common->comm_index = comm_index;
	rcb_common->ring_num = ring_num;
	rcb_common->dsaf_dev = dsaf_dev;

	rcb_common->desc_num = dsaf_dev->desc_num;
	rcb_common->coalesced_frames = HNS_RCB_DEF_COALESCED_FRAMES;
	rcb_common->timeout = HNS_RCB_MAX_TIME_OUT;

	hns_rcb_get_queue_mode(dsaf_mode, comm_index, &max_vfn, &max_q_per_vf);
	rcb_common->max_vfn = max_vfn;
	rcb_common->max_q_per_vf = max_q_per_vf;

	rcb_common->io_base = hns_rcb_common_get_vaddr(dsaf_dev, comm_index);
	rcb_common->phy_base = hns_rcb_common_get_paddr(dsaf_dev, comm_index);

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
	if (stringset == ETH_SS_STATS || stringset == ETH_SS_PRIV_FLAGS)
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

	regs[70] = dsaf_read_dev(rcb_com, RCB_CFG_OVERTIME_REG);
	regs[71] = dsaf_read_dev(rcb_com, RCB_CFG_PKTLINE_INT_NUM_REG);
	regs[72] = dsaf_read_dev(rcb_com, RCB_CFG_OVERTIME_INT_NUM_REG);

	/* mark end of rcb common regs */
	for (i = 73; i < 80; i++)
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
