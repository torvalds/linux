#ifndef __NVKM_CLK_NVA3_H__
#define __NVKM_CLK_NVA3_H__
#include <subdev/clk.h>

struct gt215_clk_info {
	u32 clk;
	u32 pll;
	enum {
		NVA3_HOST_277,
		NVA3_HOST_CLK,
	} host_out;
	u32 fb_delay;
};

int  gt215_pll_info(struct nvkm_clk *, int, u32, u32, struct gt215_clk_info *);
int  gt215_clk_pre(struct nvkm_clk *clk, unsigned long *flags);
void gt215_clk_post(struct nvkm_clk *clk, unsigned long *flags);
#endif
