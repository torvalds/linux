/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright (c) 2021 Hisilicon Limited.

#ifndef __HCLGE_PTP_H
#define __HCLGE_PTP_H

#include <linux/ptp_clock_kernel.h>
#include <linux/net_tstamp.h>
#include <linux/types.h>

#define HCLGE_PTP_REG_OFFSET	0x29000

#define HCLGE_PTP_TX_TS_SEQID_REG	0x0
#define HCLGE_PTP_TX_TS_NSEC_REG	0x4
#define HCLGE_PTP_TX_TS_NSEC_MASK	GENMASK(29, 0)
#define HCLGE_PTP_TX_TS_SEC_L_REG	0x8
#define HCLGE_PTP_TX_TS_SEC_H_REG	0xC
#define HCLGE_PTP_TX_TS_SEC_H_MASK	GENMASK(15, 0)
#define HCLGE_PTP_TX_TS_CNT_REG		0x30

#define HCLGE_PTP_TIME_SEC_H_REG	0x50
#define HCLGE_PTP_TIME_SEC_H_MASK	GENMASK(15, 0)
#define HCLGE_PTP_TIME_SEC_L_REG	0x54
#define HCLGE_PTP_TIME_NSEC_REG		0x58
#define HCLGE_PTP_TIME_NSEC_MASK	GENMASK(29, 0)
#define HCLGE_PTP_TIME_NSEC_NEG		BIT(31)
#define HCLGE_PTP_TIME_SYNC_REG		0x5C
#define HCLGE_PTP_TIME_SYNC_EN		BIT(0)
#define HCLGE_PTP_TIME_ADJ_REG		0x60
#define HCLGE_PTP_TIME_ADJ_EN		BIT(0)
#define HCLGE_PTP_CYCLE_QUO_REG		0x64
#define HCLGE_PTP_CYCLE_DEN_REG		0x68
#define HCLGE_PTP_CYCLE_NUM_REG		0x6C
#define HCLGE_PTP_CYCLE_CFG_REG		0x70
#define HCLGE_PTP_CYCLE_ADJ_EN		BIT(0)
#define HCLGE_PTP_CUR_TIME_SEC_H_REG	0x74
#define HCLGE_PTP_CUR_TIME_SEC_L_REG	0x78
#define HCLGE_PTP_CUR_TIME_NSEC_REG	0x7C

#define HCLGE_PTP_CYCLE_ADJ_BASE	2
#define HCLGE_PTP_CYCLE_ADJ_MAX		500000000
#define HCLGE_PTP_CYCLE_ADJ_UNIT	100000000
#define HCLGE_PTP_SEC_H_OFFSET		32u
#define HCLGE_PTP_SEC_L_MASK		GENMASK(31, 0)

#define HCLGE_PTP_FLAG_EN		0
#define HCLGE_PTP_FLAG_TX_EN		1
#define HCLGE_PTP_FLAG_RX_EN		2

struct hclge_ptp {
	struct hclge_dev *hdev;
	struct ptp_clock *clock;
	struct sk_buff *tx_skb;
	unsigned long flags;
	void __iomem *io_base;
	struct ptp_clock_info info;
	struct hwtstamp_config ts_cfg;
	spinlock_t lock;	/* protects ptp registers */
	u32 ptp_cfg;
	u32 last_tx_seqid;
	unsigned long tx_start;
	unsigned long tx_cnt;
	unsigned long tx_skipped;
	unsigned long tx_cleaned;
	unsigned long last_rx;
	unsigned long rx_cnt;
	unsigned long tx_timeout;
};

struct hclge_ptp_int_cmd {
#define HCLGE_PTP_INT_EN_B	BIT(0)

	u8 int_en;
	u8 rsvd[23];
};

enum hclge_ptp_udp_type {
	HCLGE_PTP_UDP_NOT_TYPE,
	HCLGE_PTP_UDP_P13F_TYPE,
	HCLGE_PTP_UDP_P140_TYPE,
	HCLGE_PTP_UDP_FULL_TYPE,
};

enum hclge_ptp_msg_type {
	HCLGE_PTP_MSG_TYPE_V2_L2,
	HCLGE_PTP_MSG_TYPE_V2,
	HCLGE_PTP_MSG_TYPE_V2_EVENT,
};

enum hclge_ptp_msg0_type {
	HCLGE_PTP_MSG0_V2_DELAY_REQ = 1,
	HCLGE_PTP_MSG0_V2_PDELAY_REQ,
	HCLGE_PTP_MSG0_V2_DELAY_RESP,
	HCLGE_PTP_MSG0_V2_EVENT = 0xF,
};

#define HCLGE_PTP_MSG1_V2_DEFAULT	1

struct hclge_ptp_cfg_cmd {
#define HCLGE_PTP_EN_B			BIT(0)
#define HCLGE_PTP_TX_EN_B		BIT(1)
#define HCLGE_PTP_RX_EN_B		BIT(2)
#define HCLGE_PTP_UDP_EN_SHIFT		3
#define HCLGE_PTP_UDP_EN_MASK		GENMASK(4, 3)
#define HCLGE_PTP_MSG_TYPE_SHIFT	8
#define HCLGE_PTP_MSG_TYPE_MASK		GENMASK(9, 8)
#define HCLGE_PTP_MSG1_SHIFT		16
#define HCLGE_PTP_MSG1_MASK		GENMASK(19, 16)
#define HCLGE_PTP_MSG0_SHIFT		24
#define HCLGE_PTP_MSG0_MASK		GENMASK(27, 24)

	__le32 cfg;
	u8 rsvd[20];
};

static inline struct hclge_dev *hclge_ptp_get_hdev(struct ptp_clock_info *info)
{
	struct hclge_ptp *ptp = container_of(info, struct hclge_ptp, info);

	return ptp->hdev;
}

bool hclge_ptp_set_tx_info(struct hnae3_handle *handle, struct sk_buff *skb);
void hclge_ptp_clean_tx_hwts(struct hclge_dev *dev);
void hclge_ptp_get_rx_hwts(struct hnae3_handle *handle, struct sk_buff *skb,
			   u32 nsec, u32 sec);
int hclge_ptp_get_cfg(struct hclge_dev *hdev, struct ifreq *ifr);
int hclge_ptp_set_cfg(struct hclge_dev *hdev, struct ifreq *ifr);
int hclge_ptp_init(struct hclge_dev *hdev);
void hclge_ptp_uninit(struct hclge_dev *hdev);
int hclge_ptp_get_ts_info(struct hnae3_handle *handle,
			  struct ethtool_ts_info *info);
int hclge_ptp_cfg_qry(struct hclge_dev *hdev, u32 *cfg);
#endif
