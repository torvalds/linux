#ifndef __NVKM_CLK_NVA3_H__
#define __NVKM_CLK_NVA3_H__

#include <subdev/clk.h>

struct nva3_clk_info {
	u32 clk;
	u32 pll;
	enum {
		NVA3_HOST_277,
		NVA3_HOST_CLK,
	} host_out;
	u32 fb_delay;
};

int nva3_pll_info(struct nouveau_clk *, int, u32, u32,
		    struct nva3_clk_info *);
int nva3_clk_pre(struct nouveau_clk *clk, unsigned long *flags);
void nva3_clk_post(struct nouveau_clk *clk, unsigned long *flags);
#endif
