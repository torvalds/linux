/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _HNS_DSAF_PPE_H
#define _HNS_DSAF_PPE_H

#include <linux/platform_device.h>

#include "hns_dsaf_main.h"
#include "hns_dsaf_mac.h"
#include "hns_dsaf_rcb.h"

#define HNS_PPE_SERVICE_NW_ENGINE_NUM DSAF_COMM_CHN
#define HNS_PPE_DEBUG_NW_ENGINE_NUM 1
#define HNS_PPE_COM_NUM DSAF_COMM_DEV_NUM

#define PPE_COMMON_REG_OFFSET 0x70000
#define PPE_REG_OFFSET 0x10000

#define ETH_PPE_DUMP_NUM 576
#define ETH_PPE_STATIC_NUM 12
enum ppe_qid_mode {
	PPE_QID_MODE0 = 0,	/* fixed queue id mode */
	PPE_QID_MODE1,		/* switch:128VM non switch:6Port/4VM/4TC */
	PPE_QID_MODE2,		/* switch:32VM/4TC non switch:6Port/16VM */
	PPE_QID_MODE3,		/* switch:4TC/8TAG non switch:2Port/64VM */
	PPE_QID_MODE4,		/* switch:8VM/16TAG non switch:2Port/16VM/4TC */
	PPE_QID_MODE5,		/* non switch:6Port/16TAG */
	PPE_QID_MODE6,		/* non switch:6Port/2VM/8TC */
	PPE_QID_MODE7,		/* non switch:2Port/8VM/8TC */
};

enum ppe_port_mode {
	PPE_MODE_GE = 0,
	PPE_MODE_XGE,
};

enum ppe_common_mode {
	PPE_COMMON_MODE_DEBUG = 0,
	PPE_COMMON_MODE_SERVICE,
	PPE_COMMON_MODE_MAX
};

struct hns_ppe_hw_stats {
	u64 rx_pkts_from_sw;
	u64 rx_pkts;
	u64 rx_drop_no_bd;
	u64 rx_alloc_buf_fail;
	u64 rx_alloc_buf_wait;
	u64 rx_drop_no_buf;
	u64 rx_err_fifo_full;
	u64 tx_bd_form_rcb;
	u64 tx_pkts_from_rcb;
	u64 tx_pkts;
	u64 tx_err_fifo_empty;
	u64 tx_err_checksum;
};

struct hns_ppe_cb {
	struct device *dev;
	struct hns_ppe_cb *next;	/* pointer to next ppe device */
	struct ppe_common_cb *ppe_common_cb; /* belong to */
	struct hns_ppe_hw_stats hw_stats;

	u8 index;	/* index in a ppe common device */
	u8 port;			 /* port id in dsaf  */
	void __iomem *io_base;
	int virq;
};

struct ppe_common_cb {
	struct device *dev;
	struct dsaf_device *dsaf_dev;
	void __iomem *io_base;

	enum ppe_common_mode ppe_mode;

	u8 comm_index;   /*ppe_common index*/

	u32 ppe_num;
	struct hns_ppe_cb ppe_cb[0];

};

int hns_ppe_init(struct dsaf_device *dsaf_dev);

void hns_ppe_uninit(struct dsaf_device *dsaf_dev);

void hns_ppe_reset_common(struct dsaf_device *dsaf_dev, u8 ppe_common_index);

void hns_ppe_update_stats(struct hns_ppe_cb *ppe_cb);

int hns_ppe_get_sset_count(int stringset);
int hns_ppe_get_regs_count(void);
void hns_ppe_get_regs(struct hns_ppe_cb *ppe_cb, void *data);

void hns_ppe_get_strings(struct hns_ppe_cb *ppe_cb, int stringset, u8 *data);
void hns_ppe_get_stats(struct hns_ppe_cb *ppe_cb, u64 *data);
#endif /* _HNS_DSAF_PPE_H */
