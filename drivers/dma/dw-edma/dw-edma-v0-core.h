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

/* eDMA core register */
void dw_edma_v0_core_register(struct dw_edma *dw);

#endif /* _DW_EDMA_V0_CORE_H */
