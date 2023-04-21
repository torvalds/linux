/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */
#ifndef __MT76_DMA_H
#define __MT76_DMA_H

#define DMA_DUMMY_DATA			((void *)~0)

#define MT_RING_SIZE			0x10

#define MT_DMA_CTL_SD_LEN1		GENMASK(13, 0)
#define MT_DMA_CTL_LAST_SEC1		BIT(14)
#define MT_DMA_CTL_BURST		BIT(15)
#define MT_DMA_CTL_SD_LEN0		GENMASK(29, 16)
#define MT_DMA_CTL_LAST_SEC0		BIT(30)
#define MT_DMA_CTL_DMA_DONE		BIT(31)
#define MT_DMA_CTL_TO_HOST		BIT(8)
#define MT_DMA_CTL_TO_HOST_A		BIT(12)
#define MT_DMA_CTL_DROP			BIT(14)
#define MT_DMA_CTL_TOKEN		GENMASK(31, 16)
#define MT_DMA_CTL_WO_DROP		BIT(8)

#define MT_DMA_PPE_CPU_REASON		GENMASK(15, 11)
#define MT_DMA_PPE_ENTRY		GENMASK(30, 16)
#define MT_DMA_INFO_PPE_VLD		BIT(31)

#define MT_DMA_HDR_LEN			4
#define MT_RX_INFO_LEN			4
#define MT_FCE_INFO_LEN			4
#define MT_RX_RXWI_LEN			32

struct mt76_desc {
	__le32 buf0;
	__le32 ctrl;
	__le32 buf1;
	__le32 info;
} __packed __aligned(4);

enum mt76_qsel {
	MT_QSEL_MGMT,
	MT_QSEL_HCCA,
	MT_QSEL_EDCA,
	MT_QSEL_EDCA_2,
};

enum mt76_mcu_evt_type {
	EVT_CMD_DONE,
	EVT_CMD_ERROR,
	EVT_CMD_RETRY,
	EVT_EVENT_PWR_RSP,
	EVT_EVENT_WOW_RSP,
	EVT_EVENT_CARRIER_DETECT_RSP,
	EVT_EVENT_DFS_DETECT_RSP,
};

int mt76_dma_rx_poll(struct napi_struct *napi, int budget);
void mt76_dma_attach(struct mt76_dev *dev);
void mt76_dma_cleanup(struct mt76_dev *dev);
int mt76_dma_wed_setup(struct mt76_dev *dev, struct mt76_queue *q, bool reset);

#endif
