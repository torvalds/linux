/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
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
#ifndef __MT76_DMA_H
#define __MT76_DMA_H

#define MT_RING_SIZE			0x10

#define MT_DMA_CTL_SD_LEN1		GENMASK(13, 0)
#define MT_DMA_CTL_LAST_SEC1		BIT(14)
#define MT_DMA_CTL_BURST		BIT(15)
#define MT_DMA_CTL_SD_LEN0		GENMASK(29, 16)
#define MT_DMA_CTL_LAST_SEC0		BIT(30)
#define MT_DMA_CTL_DMA_DONE		BIT(31)

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

enum dma_msg_port {
	WLAN_PORT,
	CPU_RX_PORT,
	CPU_TX_PORT,
	HOST_PORT,
	VIRTUAL_CPU_RX_PORT,
	VIRTUAL_CPU_TX_PORT,
	DISCARD,
};

int mt76_dma_attach(struct mt76_dev *dev);
void mt76_dma_cleanup(struct mt76_dev *dev);

#endif
