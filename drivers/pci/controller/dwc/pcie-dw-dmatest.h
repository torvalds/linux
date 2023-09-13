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
void pcie_dw_dmatest_unregister(struct dma_trx_obj *obj);
int pcie_dw_rc_dma_frombus(struct dma_trx_obj *obj, u32 chn, u64 local_paddr, u64 bus_paddr, u32 size);
int pcie_dw_rc_dma_tobus(struct dma_trx_obj *obj, u32 chn, u64 bus_paddr, u64 local_paddr, u32 size);
#else
static inline struct dma_trx_obj *pcie_dw_dmatest_register(struct device *dev, bool irq_en)
{
	return NULL;
}

static inline void pcie_dw_dmatest_unregister(struct dma_trx_obj *obj) { }

static inline int pcie_dw_rc_dma_frombus(struct dma_trx_obj *obj, u32 chn, u64 local_paddr, u64 bus_paddr, u32 size)
{
	return -1;
}

static inline int pcie_dw_rc_dma_tobus(struct dma_trx_obj *obj, u32 chn, u64 bus_paddr, u64 local_paddr, u32 size)
{
	return -1;
}
#endif

#endif
