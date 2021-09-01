/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2021 Aspeed Technology Inc.
 */
#ifndef _ASPEED_ESPI_OOB_H_
#define _ASPEED_ESPI_OOB_H_

struct oob_tx_dma_desc {
	uint32_t data_addr;
	uint8_t cyc;
	uint16_t tag : 4;
	uint16_t len : 12;
	uint8_t msg_type : 3;
	uint8_t raz0 : 1;
	uint8_t pec : 1;
	uint8_t int_en : 1;
	uint8_t pause : 1;
	uint8_t raz1 : 1;
	uint32_t raz2;
	uint32_t raz3;
} __packed;

struct oob_rx_dma_desc {
	uint32_t data_addr;
	uint8_t cyc;
	uint16_t tag : 4;
	uint16_t len : 12;
	uint8_t raz : 7;
	uint8_t dirty : 1;
} __packed;

struct aspeed_espi_oob_dma {
	uint32_t tx_desc_num;
	uint32_t rx_desc_num;

	struct oob_tx_dma_desc *tx_desc;
	dma_addr_t tx_desc_addr;

	struct oob_rx_dma_desc *rx_desc;
	dma_addr_t rx_desc_addr;

	void *tx_virt;
	dma_addr_t tx_addr;

	void *rx_virt;
	dma_addr_t rx_addr;
};

struct aspeed_espi_oob {
	uint32_t dma_mode;
	struct aspeed_espi_oob_dma dma;

	uint32_t rx_ready;
	wait_queue_head_t wq;

	struct mutex get_rx_mtx;
	struct mutex put_tx_mtx;

	spinlock_t lock;

	struct miscdevice mdev;
	struct aspeed_espi_ctrl *ctrl;
};

void aspeed_espi_oob_event(uint32_t sts, struct aspeed_espi_oob *espi_oob);
void aspeed_espi_oob_enable(struct aspeed_espi_oob *espi_oob);
void *aspeed_espi_oob_alloc(struct device *dev, struct aspeed_espi_ctrl *espi_ctrl);
void aspeed_espi_oob_free(struct device *dev, struct aspeed_espi_oob *espi_oob);

#endif
