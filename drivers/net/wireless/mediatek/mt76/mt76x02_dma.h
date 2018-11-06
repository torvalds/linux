/*
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __MT76x02_DMA_H
#define __MT76x02_DMA_H

#include "mt76x02.h"
#include "dma.h"

#define MT_TXD_INFO_LEN			GENMASK(15, 0)
#define MT_TXD_INFO_NEXT_VLD		BIT(16)
#define MT_TXD_INFO_TX_BURST		BIT(17)
#define MT_TXD_INFO_80211		BIT(19)
#define MT_TXD_INFO_TSO			BIT(20)
#define MT_TXD_INFO_CSO			BIT(21)
#define MT_TXD_INFO_WIV			BIT(24)
#define MT_TXD_INFO_QSEL		GENMASK(26, 25)
#define MT_TXD_INFO_DPORT		GENMASK(29, 27)
#define MT_TXD_INFO_TYPE		GENMASK(31, 30)

#define MT_RX_FCE_INFO_LEN		GENMASK(13, 0)
#define MT_RX_FCE_INFO_SELF_GEN		BIT(15)
#define MT_RX_FCE_INFO_CMD_SEQ		GENMASK(19, 16)
#define MT_RX_FCE_INFO_EVT_TYPE		GENMASK(23, 20)
#define MT_RX_FCE_INFO_PCIE_INTR	BIT(24)
#define MT_RX_FCE_INFO_QSEL		GENMASK(26, 25)
#define MT_RX_FCE_INFO_D_PORT		GENMASK(29, 27)
#define MT_RX_FCE_INFO_TYPE		GENMASK(31, 30)

/* MCU request message header  */
#define MT_MCU_MSG_LEN			GENMASK(15, 0)
#define MT_MCU_MSG_CMD_SEQ		GENMASK(19, 16)
#define MT_MCU_MSG_CMD_TYPE		GENMASK(26, 20)
#define MT_MCU_MSG_PORT			GENMASK(29, 27)
#define MT_MCU_MSG_TYPE			GENMASK(31, 30)
#define MT_MCU_MSG_TYPE_CMD		BIT(30)

#define MT_RX_HEADROOM			32
#define MT76X02_RX_RING_SIZE		256

enum dma_msg_port {
	WLAN_PORT,
	CPU_RX_PORT,
	CPU_TX_PORT,
	HOST_PORT,
	VIRTUAL_CPU_RX_PORT,
	VIRTUAL_CPU_TX_PORT,
	DISCARD,
};

static inline bool
mt76x02_wait_for_wpdma(struct mt76_dev *dev, int timeout)
{
	return __mt76_poll(dev, MT_WPDMA_GLO_CFG,
			   MT_WPDMA_GLO_CFG_TX_DMA_BUSY |
			   MT_WPDMA_GLO_CFG_RX_DMA_BUSY,
			   0, timeout);
}

int mt76x02_dma_init(struct mt76x02_dev *dev);
void mt76x02_dma_disable(struct mt76x02_dev *dev);
void mt76x02_dma_cleanup(struct mt76x02_dev *dev);

#endif /* __MT76x02_DMA_H */
