/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */
#ifndef __PCIE_DW_DMATEST_H
#define __PCIE_DW_DMATEST_H

struct dma_trx_obj;
struct device;

#if IS_ENABLED(CONFIG_PCIE_DW_DMATEST)
struct dma_trx_obj *pcie_dw_dmatest_register(struct device *dev, bool irq_en);
#else
static inline struct dma_trx_obj *pcie_dw_dmatest_register(struct device *dev, bool irq_en)
{
	return NULL;
}
#endif

#endif
