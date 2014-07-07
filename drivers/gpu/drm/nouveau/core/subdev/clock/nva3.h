#ifndef __NVKM_CLK_NVA3_H__
#define __NVKM_CLK_NVA3_H__

#include <subdev/clock.h>

struct nva3_clock_info {
	u32 clk;
	u32 pll;
};

int nva3_clock_info(struct nouveau_clock *, int, u32, u32,
		    struct nva3_clock_info *);

#endif
