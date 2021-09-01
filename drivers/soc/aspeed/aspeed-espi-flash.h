/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2021 ASPEED Technology Inc.
 */
#ifndef _ASPEED_ESPI_FLASH_H_
#define _ASPEED_ESPI_FLASH_H_

enum aspeed_espi_flash_safs_mode {
	SAFS_MODE_MIX,
	SAFS_MODE_SW,
	SAFS_MODE_HW,
	SAFS_MODES,
};

struct aspeed_espi_flash_dma {
	void *tx_virt;
	dma_addr_t tx_addr;
	void *rx_virt;
	dma_addr_t rx_addr;
};

struct aspeed_espi_flash {
	uint32_t safs_mode;

	uint32_t dma_mode;
	struct aspeed_espi_flash_dma dma;

	uint32_t rx_ready;
	wait_queue_head_t wq;

	struct mutex get_rx_mtx;
	struct mutex put_tx_mtx;

	spinlock_t lock;

	struct miscdevice mdev;
	struct aspeed_espi_ctrl *ctrl;
};

void aspeed_espi_flash_event(uint32_t sts, struct aspeed_espi_flash *espi_flash);
void aspeed_espi_flash_enable(struct aspeed_espi_flash *espi_flash);
void *aspeed_espi_flash_alloc(struct device *dev, struct aspeed_espi_ctrl *espi_ctrl);
void aspeed_espi_flash_free(struct device *dev, struct aspeed_espi_flash *espi_flash);

#endif
