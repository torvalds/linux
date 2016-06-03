/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _HNS_DSAF_RCB_H
#define _HNS_DSAF_RCB_H

#include <linux/netdevice.h>
#include <linux/platform_device.h>

#include "hnae.h"
#include "hns_dsaf_main.h"

struct rcb_common_cb;

#define HNS_RCB_IRQ_NUM_PER_QUEUE		2
#define HNS_RCB_IRQ_IDX_TX			0
#define HNS_RCB_IRQ_IDX_RX			1
#define HNS_RCB_TX_REG_OFFSET			0x40

#define HNS_RCB_SERVICE_NW_ENGINE_NUM		DSAF_COMM_CHN
#define HNS_RCB_DEBUG_NW_ENGINE_NUM		1
#define HNS_RCB_RING_MAX_BD_PER_PKT		3
#define HNS_RCB_RING_MAX_TXBD_PER_PKT		3
#define HNS_RCBV2_RING_MAX_TXBD_PER_PKT		8
#define HNS_RCB_MAX_PKT_SIZE MAC_MAX_MTU

#define HNS_RCB_RING_MAX_PENDING_BD		1024
#define HNS_RCB_RING_MIN_PENDING_BD		16

#define HNS_RCB_REG_OFFSET			0x10000

#define HNS_RCB_MAX_COALESCED_FRAMES		1023
#define HNS_RCB_MIN_COALESCED_FRAMES		1
#define HNS_RCB_DEF_COALESCED_FRAMES		50
#define HNS_RCB_CLK_FREQ_MHZ			350
#define HNS_RCB_MAX_COALESCED_USECS		0x3ff
#define HNS_RCB_DEF_COALESCED_USECS		3

#define HNS_RCB_COMMON_ENDIAN			1

#define HNS_BD_SIZE_512_TYPE			0
#define HNS_BD_SIZE_1024_TYPE			1
#define HNS_BD_SIZE_2048_TYPE			2
#define HNS_BD_SIZE_4096_TYPE			3

#define HNS_RCB_COMMON_DUMP_REG_NUM 80
#define HNS_RCB_RING_DUMP_REG_NUM 40
#define HNS_RING_STATIC_REG_NUM 28

#define HNS_DUMP_REG_NUM			500
#define HNS_STATIC_REG_NUM			12

#define HNS_TSO_MODE_8BD_32K			1
#define HNS_TSO_MDOE_4BD_16K			0

enum rcb_int_flag {
	RCB_INT_FLAG_TX = 0x1,
	RCB_INT_FLAG_RX = (0x1 << 1),
	RCB_INT_FLAG_MAX = (0x1 << 2),	/*must be the last element */
};

struct hns_ring_hw_stats {
	u64 tx_pkts;
	u64 ppe_tx_ok_pkts;
	u64 ppe_tx_drop_pkts;
	u64 rx_pkts;
	u64 ppe_rx_ok_pkts;
	u64 ppe_rx_drop_pkts;
};

struct ring_pair_cb {
	struct rcb_common_cb *rcb_common;	/*  ring belongs to */
	struct device *dev;	/*device for DMA mapping */
	struct hnae_queue q;

	u16 index;	/* global index in a rcb common device */
	u16 buf_size;

	int virq[HNS_RCB_IRQ_NUM_PER_QUEUE];

	u8 port_id_in_comm;
	u8 used_by_vf;

	struct hns_ring_hw_stats hw_stats;
};

struct rcb_common_cb {
	u8 __iomem *io_base;
	phys_addr_t phy_base;
	struct dsaf_device *dsaf_dev;
	u16 max_vfn;
	u16 max_q_per_vf;

	u8 comm_index;
	u32 ring_num;
	u32 desc_num; /*  desc num per queue*/

	struct ring_pair_cb ring_pair_cb[0];
};

int hns_rcb_buf_size2type(u32 buf_size);

int hns_rcb_common_get_cfg(struct dsaf_device *dsaf_dev, int comm_index);
void hns_rcb_common_free_cfg(struct dsaf_device *dsaf_dev, u32 comm_index);
int hns_rcb_common_init_hw(struct rcb_common_cb *rcb_common);
void hns_rcb_start(struct hnae_queue *q, u32 val);
void hns_rcb_get_cfg(struct rcb_common_cb *rcb_common);
void hns_rcb_get_queue_mode(enum dsaf_mode dsaf_mode,
			    u16 *max_vfn, u16 *max_q_per_vf);

void hns_rcb_common_init_commit_hw(struct rcb_common_cb *rcb_common);

void hns_rcb_ring_enable_hw(struct hnae_queue *q, u32 val);
void hns_rcb_int_clr_hw(struct hnae_queue *q, u32 flag);
void hns_rcb_int_ctrl_hw(struct hnae_queue *q, u32 flag, u32 enable);
void hns_rcbv2_int_ctrl_hw(struct hnae_queue *q, u32 flag, u32 mask);
void hns_rcbv2_int_clr_hw(struct hnae_queue *q, u32 flag);

void hns_rcb_init_hw(struct ring_pair_cb *ring);
void hns_rcb_reset_ring_hw(struct hnae_queue *q);
void hns_rcb_wait_fbd_clean(struct hnae_queue **qs, int q_num, u32 flag);
u32 hns_rcb_get_coalesced_frames(
	struct rcb_common_cb *rcb_common, u32 port_idx);
u32 hns_rcb_get_coalesce_usecs(
	struct rcb_common_cb *rcb_common, u32 port_idx);
int hns_rcb_set_coalesce_usecs(
	struct rcb_common_cb *rcb_common, u32 port_idx, u32 timeout);
int hns_rcb_set_coalesced_frames(
	struct rcb_common_cb *rcb_common, u32 port_idx, u32 coalesced_frames);
void hns_rcb_update_stats(struct hnae_queue *queue);

void hns_rcb_get_stats(struct hnae_queue *queue, u64 *data);

void hns_rcb_get_common_regs(struct rcb_common_cb *rcb_common, void *data);

int hns_rcb_get_ring_sset_count(int stringset);
int hns_rcb_get_common_regs_count(void);
int hns_rcb_get_ring_regs_count(void);

void hns_rcb_get_ring_regs(struct hnae_queue *queue, void *data);

void hns_rcb_get_strings(int stringset, u8 *data, int index);
#endif /* _HNS_DSAF_RCB_H */
