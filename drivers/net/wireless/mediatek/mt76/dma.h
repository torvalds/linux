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
#define MT_DMA_CTL_SDP1_H		GENMASK(19, 16)
#define MT_DMA_CTL_SDP0_H		GENMASK(3, 0)
#define MT_DMA_CTL_WO_DROP		BIT(8)

#define MT_DMA_PPE_CPU_REASON		GENMASK(15, 11)
#define MT_DMA_PPE_ENTRY		GENMASK(30, 16)
#define MT_DMA_INFO_DMA_FRAG		BIT(9)
#define MT_DMA_INFO_PPE_VLD		BIT(31)

#define MT_DMA_CTL_PN_CHK_FAIL		BIT(13)
#define MT_DMA_CTL_VER_MASK		BIT(7)

#define MT_DMA_SDP0			GENMASK(15, 0)
#define MT_DMA_TOKEN_ID			GENMASK(31, 16)
#define MT_DMA_MAGIC_MASK		GENMASK(31, 28)
#define MT_DMA_RRO_EN			BIT(13)

#define MT_DMA_MAGIC_CNT		16

#define MT_DMA_WED_IND_CMD_CNT		8
#define MT_DMA_WED_IND_REASON		GENMASK(15, 12)

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

struct mt76_wed_rro_desc {
	__le32 buf0;
	__le32 buf1;
} __packed __aligned(4);

/* data1 */
#define RRO_RXDMAD_DATA1_LS_MASK		BIT(30)
#define RRO_RXDMAD_DATA1_SDL0_MASK		GENMASK(29, 16)
/* data2 */
#define RRO_RXDMAD_DATA2_RX_TOKEN_ID_MASK	GENMASK(31, 16)
#define RRO_RXDMAD_DATA2_IND_REASON_MASK	GENMASK(15, 12)
/* data3 */
#define RRO_RXDMAD_DATA3_MAGIC_CNT_MASK		GENMASK(31, 28)
struct mt76_rro_rxdmad_c {
	__le32 data0;
	__le32 data1;
	__le32 data2;
	__le32 data3;
};

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

enum mt76_dma_wed_ind_reason {
	MT_DMA_WED_IND_REASON_NORMAL,
	MT_DMA_WED_IND_REASON_REPEAT,
	MT_DMA_WED_IND_REASON_OLDPKT,
};

int mt76_dma_rx_poll(struct napi_struct *napi, int budget);
void mt76_dma_attach(struct mt76_dev *dev);
void mt76_dma_cleanup(struct mt76_dev *dev);
int mt76_dma_rx_fill(struct mt76_dev *dev, struct mt76_queue *q,
		     bool allow_direct);
void mt76_dma_queue_reset(struct mt76_dev *dev, struct mt76_queue *q,
			  bool reset_idx);

static inline void
mt76_dma_reset_tx_queue(struct mt76_dev *dev, struct mt76_queue *q)
{
	dev->queue_ops->reset_q(dev, q, true);
	if (mtk_wed_device_active(&dev->mmio.wed))
		mt76_wed_dma_setup(dev, q, true);
}

static inline void
mt76_dma_should_drop_buf(bool *drop, u32 ctrl, u32 buf1, u32 info)
{
	if (!drop)
		return;

	*drop = !!(ctrl & (MT_DMA_CTL_TO_HOST_A | MT_DMA_CTL_DROP));
	if (!(ctrl & MT_DMA_CTL_VER_MASK))
		return;

	switch (FIELD_GET(MT_DMA_WED_IND_REASON, buf1)) {
	case MT_DMA_WED_IND_REASON_REPEAT:
		*drop = true;
		break;
	case MT_DMA_WED_IND_REASON_OLDPKT:
		*drop = !(info & MT_DMA_INFO_DMA_FRAG);
		break;
	default:
		*drop = !!(ctrl & MT_DMA_CTL_PN_CHK_FAIL);
		break;
	}
}

static inline void *mt76_priv(struct net_device *dev)
{
	struct mt76_dev **priv;

	priv = netdev_priv(dev);

	return *priv;
}

#endif
