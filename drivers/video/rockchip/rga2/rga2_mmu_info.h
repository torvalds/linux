/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RGA_MMU_INFO_H__
#define __RGA_MMU_INFO_H__

#include "rga2.h"
#include "RGA2_API.h"

#ifndef MIN
#define MIN(X, Y)           ((X)<(Y)?(X):(Y))
#endif

#ifndef MAX
#define MAX(X, Y)           ((X)>(Y)?(X):(Y))
#endif

extern struct rga2_drvdata_t *rga2_drvdata;

int rga2_set_mmu_info(struct rga2_reg *reg, struct rga2_req *req);
void rga2_dma_flush_range(void *pstart, void *pend);

#endif

