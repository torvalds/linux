/*
 * Driver for the Synopsys DesignWare DMA Controller
 *
 * Copyright (C) 2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _DW_DMAC_INTERNAL_H
#define _DW_DMAC_INTERNAL_H

#include <linux/device.h>
#include <linux/dw_dmac.h>

#include "regs.h"

/**
 * struct dw_dma_chip - representation of DesignWare DMA controller hardware
 * @dev:		struct device of the DMA controller
 * @irq:		irq line
 * @regs:		memory mapped I/O space
 * @dw:			struct dw_dma that is filed by dw_dma_probe()
 */
struct dw_dma_chip {
	struct device	*dev;
	int		irq;
	void __iomem	*regs;
	struct dw_dma	*dw;
};

/* Export to the platform drivers */
int dw_dma_probe(struct dw_dma_chip *chip, struct dw_dma_platform_data *pdata);
int dw_dma_remove(struct dw_dma_chip *chip);

void dw_dma_shutdown(struct dw_dma_chip *chip);

#ifdef CONFIG_PM_SLEEP

int dw_dma_suspend(struct dw_dma_chip *chip);
int dw_dma_resume(struct dw_dma_chip *chip);

#endif /* CONFIG_PM_SLEEP */

/**
 * dwc_get_dms - get destination master
 * @slave:	pointer to the custom slave configuration
 *
 * Returns destination master in the custom slave configuration if defined, or
 * default value otherwise.
 */
static inline unsigned int dwc_get_dms(struct dw_dma_slave *slave)
{
	return slave ? slave->dst_master : 0;
}

/**
 * dwc_get_sms - get source master
 * @slave:	pointer to the custom slave configuration
 *
 * Returns source master in the custom slave configuration if defined, or
 * default value otherwise.
 */
static inline unsigned int dwc_get_sms(struct dw_dma_slave *slave)
{
	return slave ? slave->src_master : 1;
}

#endif /* _DW_DMAC_INTERNAL_H */
