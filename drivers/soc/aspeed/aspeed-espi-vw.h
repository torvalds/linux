/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2021 ASPEED Technology Inc.
 */
#ifndef _ASPEED_ESPI_VW_H_
#define _ASPEED_ESPI_VW_H_

struct aspeed_espi_vw {
	int irq;
	int irq_reset;

	struct miscdevice mdev;
	struct aspeed_espi_ctrl *ctrl;
};

void aspeed_espi_vw_event(uint32_t sts, struct aspeed_espi_vw *espi_vw);
void aspeed_espi_vw_enable(struct aspeed_espi_vw *espi_vw);
void *aspeed_espi_vw_alloc(struct device *dev, struct aspeed_espi_ctrl *espi_ctrl);
void aspeed_espi_vw_free(struct device *dev, struct aspeed_espi_vw *espi_vw);

#endif
