/*
 * Driver for the Synopsys DesignWare DMA Controller
 *
 * Copyright (C) 2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _DMA_DW_INTERNAL_H
#define _DMA_DW_INTERNAL_H

#include <linux/dma/dw.h>

#include "regs.h"

int do_dma_probe(struct dw_dma_chip *chip);
int do_dma_remove(struct dw_dma_chip *chip);

void do_dw_dma_on(struct dw_dma *dw);
void do_dw_dma_off(struct dw_dma *dw);

int do_dw_dma_disable(struct dw_dma_chip *chip);
int do_dw_dma_enable(struct dw_dma_chip *chip);

extern bool dw_dma_filter(struct dma_chan *chan, void *param);

#endif /* _DMA_DW_INTERNAL_H */
