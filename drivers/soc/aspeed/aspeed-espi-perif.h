/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2021 ASPEED Technology Inc.
 */
#ifndef _ASPEED_ESPI_PERIF_H_
#define _ASPEED_ESPI_PERIF_H_

struct aspeed_espi_perif_dma {
	void *pc_tx_virt;
	dma_addr_t pc_tx_addr;
	void *pc_rx_virt;
	dma_addr_t pc_rx_addr;
	void *np_tx_virt;
	dma_addr_t np_tx_addr;
};

struct aspeed_espi_perif {
	uint32_t mcyc_enable;
	void *mcyc_virt;
	uint32_t mcyc_saddr;
	phys_addr_t mcyc_taddr;
	uint32_t mcyc_size;
	uint32_t mcyc_mask;

	uint32_t dma_mode;
	struct aspeed_espi_perif_dma dma;

	uint32_t rx_ready;
	wait_queue_head_t wq;

	spinlock_t lock;
	struct mutex pc_rx_mtx;
	struct mutex pc_tx_mtx;
	struct mutex np_tx_mtx;

	struct miscdevice mdev;
	struct aspeed_espi_ctrl *ctrl;
};

void aspeed_espi_perif_event(uint32_t sts, struct aspeed_espi_perif *espi_perif);
void aspeed_espi_perif_enable(struct aspeed_espi_perif *espi_perif);
void *aspeed_espi_perif_alloc(struct device *dev, struct aspeed_espi_ctrl *espi_ctrl);
void aspeed_espi_perif_free(struct device *dev, struct aspeed_espi_perif *espi_perif);

#endif
