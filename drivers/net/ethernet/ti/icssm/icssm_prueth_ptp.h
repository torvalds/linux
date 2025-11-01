/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Texas Instruments Incorporated - https://www.ti.com
 */
#ifndef PRUETH_PTP_H
#define PRUETH_PTP_H

#define RX_SYNC_TIMESTAMP_OFFSET_P1		0x8    /* 8 bytes */
#define RX_PDELAY_REQ_TIMESTAMP_OFFSET_P1	0x14   /* 12 bytes */

#define DISABLE_PTP_FRAME_FORWARDING_CTRL_OFFSET 0x14	/* 1 byte */

#define RX_PDELAY_RESP_TIMESTAMP_OFFSET_P1	0x20   /* 12 bytes */
#define RX_SYNC_TIMESTAMP_OFFSET_P2		0x2c   /* 12 bytes */
#define RX_PDELAY_REQ_TIMESTAMP_OFFSET_P2	0x38   /* 12 bytes */
#define RX_PDELAY_RESP_TIMESTAMP_OFFSET_P2	0x44   /* 12 bytes */
#define TIMESYNC_DOMAIN_NUMBER_LIST		0x50   /* 2 bytes */
#define P1_SMA_LINE_DELAY_OFFSET		0x52   /* 4 bytes */
#define P2_SMA_LINE_DELAY_OFFSET		0x56   /* 4 bytes */
#define TIMESYNC_SECONDS_COUNT_OFFSET		0x5a   /* 6 bytes */
#define TIMESYNC_TC_RCF_OFFSET			0x60   /* 4 bytes */
#define DUT_IS_MASTER_OFFSET			0x64   /* 1 byte */
#define MASTER_PORT_NUM_OFFSET			0x65   /* 1 byte */
#define SYNC_MASTER_MAC_OFFSET			0x66   /* 6 bytes */
#define TX_TS_NOTIFICATION_OFFSET_SYNC_P1	0x6c   /* 1 byte */
#define TX_TS_NOTIFICATION_OFFSET_PDEL_REQ_P1	0x6d   /* 1 byte */
#define TX_TS_NOTIFICATION_OFFSET_PDEL_RES_P1	0x6e   /* 1 byte */
#define TX_TS_NOTIFICATION_OFFSET_SYNC_P2	0x6f   /* 1 byte */
#define TX_TS_NOTIFICATION_OFFSET_PDEL_REQ_P2	0x70   /* 1 byte */
#define TX_TS_NOTIFICATION_OFFSET_PDEL_RES_P2	0x71   /* 1 byte */
#define TX_SYNC_TIMESTAMP_OFFSET_P1		0x72   /* 12 bytes */
#define TX_PDELAY_REQ_TIMESTAMP_OFFSET_P1	0x7e   /* 12 bytes */
#define TX_PDELAY_RESP_TIMESTAMP_OFFSET_P1	0x8a   /* 12 bytes */
#define TX_SYNC_TIMESTAMP_OFFSET_P2		0x96   /* 12 bytes */
#define TX_PDELAY_REQ_TIMESTAMP_OFFSET_P2	0xa2   /* 12 bytes */
#define TX_PDELAY_RESP_TIMESTAMP_OFFSET_P2	0xae   /* 12 bytes */
#define TIMESYNC_CTRL_VAR_OFFSET		0xba   /* 1 byte */
#define DISABLE_SWITCH_SYNC_RELAY_OFFSET	0xbb   /* 1 byte */
#define MII_RX_CORRECTION_OFFSET		0xbc   /* 2 bytes */
#define MII_TX_CORRECTION_OFFSET		0xbe   /* 2 bytes */
#define TIMESYNC_CMP1_CMP_OFFSET		0xc0   /* 8 bytes */
#define TIMESYNC_SYNC0_CMP_OFFSET		0xc8   /* 8 bytes */
#define TIMESYNC_CMP1_PERIOD_OFFSET		0xd0   /* 4 bytes */
#define TIMESYNC_SYNC0_WIDTH_OFFSET		0xd4   /* 4 bytes */
#define SINGLE_STEP_IEP_OFFSET_P1		0xd8   /* 8 bytes */
#define SINGLE_STEP_SECONDS_OFFSET_P1		0xe0   /* 8 bytes */
#define SINGLE_STEP_IEP_OFFSET_P2		0xe8   /* 8 bytes */
#define SINGLE_STEP_SECONDS_OFFSET_P2		0xf0   /* 8 bytes */
#define LINK_LOCAL_FRAME_HAS_HSR_TAG		0xf8   /* 1 bytes */
#define PTP_PREV_TX_TIMESTAMP_P1		0xf9  /* 8 bytes */
#define PTP_PREV_TX_TIMESTAMP_P2		0x101  /* 8 bytes */
#define PTP_CLK_IDENTITY_OFFSET			0x109  /* 8 bytes */
#define PTP_SCRATCH_MEM				0x111  /* 16 byte */
#define PTP_IPV4_UDP_E2E_ENABLE			0x121  /* 1 byte */

enum {
	PRUETH_PTP_SYNC,
	PRUETH_PTP_DLY_REQ,
	PRUETH_PTP_DLY_RESP,
	PRUETH_PTP_TS_EVENTS,
};

#define PRUETH_PTP_TS_SIZE		12
#define PRUETH_PTP_TS_NOTIFY_SIZE	1
#define PRUETH_PTP_TS_NOTIFY_MASK	0xff

/* Bit definitions for TIMESYNC_CTRL */
#define TIMESYNC_CTRL_BG_ENABLE    BIT(0)
#define TIMESYNC_CTRL_FORCED_2STEP BIT(1)

static inline u32 icssm_prueth_tx_ts_offs_get(u8 port, u8 event)
{
	return TX_SYNC_TIMESTAMP_OFFSET_P1 + port *
		PRUETH_PTP_TS_EVENTS * PRUETH_PTP_TS_SIZE +
		event * PRUETH_PTP_TS_SIZE;
}

static inline u32 icssm_prueth_tx_ts_notify_offs_get(u8 port, u8 event)
{
	return TX_TS_NOTIFICATION_OFFSET_SYNC_P1 +
		PRUETH_PTP_TS_EVENTS * PRUETH_PTP_TS_NOTIFY_SIZE * port +
		event * PRUETH_PTP_TS_NOTIFY_SIZE;
}

#endif /* PRUETH_PTP_H */
