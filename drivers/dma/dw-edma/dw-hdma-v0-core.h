/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Cai Huoqing
 * Synopsys DesignWare HDMA v0 core
 *
 * Author: Cai Huoqing <cai.huoqing@linux.dev>
 */

#ifndef _DW_HDMA_V0_CORE_H
#define _DW_HDMA_V0_CORE_H

#include <linux/dma/edma.h>

/* HDMA core register */
void dw_hdma_v0_core_register(struct dw_edma *dw);

#endif /* _DW_HDMA_V0_CORE_H */
