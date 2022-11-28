/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RGA_MMU_INFO_H__
#define __RGA_MMU_INFO_H__

#include "rga.h"
#include "RGA_API.h"

#ifndef MIN
#define MIN(X, Y)           ((X)<(Y)?(X):(Y))
#endif

#ifndef MAX
#define MAX(X, Y)           ((X)>(Y)?(X):(Y))
#endif

extern struct rga_drvdata *rga_drvdata;

void rga_dma_flush_range(void *pstart, void *pend);
int rga_set_mmu_info(struct rga_reg *reg, struct rga_req *req);


#endif


