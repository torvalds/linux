/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018-2019 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare eDMA v0 core
 *
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#ifndef _DW_EDMA_V0_CORE_H
#define _DW_EDMA_V0_CORE_H

#include <linux/dma/edma.h>

/* eDMA management callbacks */
void dw_edma_v0_core_off(struct dw_edma *chan);
u16 dw_edma_v0_core_ch_count(struct dw_edma *chan, enum dw_edma_dir dir);
enum dma_status dw_edma_v0_core_ch_status(struct dw_edma_chan *chan);
void dw_edma_v0_core_clear_done_int(struct dw_edma_chan *chan);
void dw_edma_v0_core_clear_abort_int(struct dw_edma_chan *chan);
u32 dw_edma_v0_core_status_done_int(struct dw_edma *chan, enum dw_edma_dir dir);
u32 dw_edma_v0_core_status_abort_int(struct dw_edma *chan, enum dw_edma_dir dir);
void dw_edma_v0_core_start(struct dw_edma_chunk *chunk, bool first);
int dw_edma_v0_core_device_config(struct dw_edma_chan *chan);
/* eDMA debug fs callbacks */
void dw_edma_v0_core_debugfs_on(struct dw_edma *dw);

#endif /* _DW_EDMA_V0_CORE_H */
