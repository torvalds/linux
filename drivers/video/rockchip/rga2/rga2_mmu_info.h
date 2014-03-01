#ifndef __RGA_MMU_INFO_H__
#define __RGA_MMU_INFO_H__

#include "rga2.h"

#ifndef MIN
#define MIN(X, Y)           ((X)<(Y)?(X):(Y))
#endif

#ifndef MAX
#define MAX(X, Y)           ((X)>(Y)?(X):(Y))
#endif


int rga2_set_mmu_info(struct rga2_reg *reg, struct rga2_req *req);


#endif


