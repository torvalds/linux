#ifndef __MACH_ROCKCHIP_CRU_H
#define __MACH_ROCKCHIP_CRU_H

enum {
	RK3188_APLL_ID = 0,
	RK3188_DPLL_ID,
	RK3188_CPLL_ID,
	RK3188_GPLL_ID,
	RK3188_END_PLL_ID,
};

#define RK3188_CRU_MODE_CON		0x40
#define RK3188_CRU_CLKSEL_CON		0x44
#define RK3188_CRU_CLKGATE_CON		0xd0
#define RK3188_CRU_GLB_SRST_FST		0x100
#define RK3188_CRU_GLB_SRST_SND		0x104
#define RK3188_CRU_SOFTRST_CON		0x110

#define RK3188_PLL_CONS(id, i)		((id) * 0x10 + ((i) * 4))

#define RK3188_CRU_CLKSELS_CON_CNT	(35)
#define RK3188_CRU_CLKSELS_CON(i)	(RK3188_CRU_CLKSEL_CON + ((i) * 4))

#define RK3188_CRU_CLKGATES_CON_CNT	(10)
#define RK3188_CRU_CLKGATES_CON(i)	(RK3188_CRU_CLKGATE_CON + ((i) * 4))

#define RK3188_CRU_SOFTRSTS_CON_CNT	(9)
#define RK3188_CRU_SOFTRSTS_CON(i)	(RK3188_CRU_SOFTRST_CON + ((i) * 4))

#define RK3188_CRU_MISC_CON		(0x134)
#define RK3188_CRU_GLB_CNT_TH		(0x140)

/*******************MODE BITS***************************/

#define RK3188_PLL_MODE_MSK(id)		(0x3 << ((id) * 4))
#define RK3188_PLL_MODE_SLOW(id)	((0x0<<((id)*4))|(0x3<<(16+(id)*4)))
#define RK3188_PLL_MODE_NORM(id)	((0x1<<((id)*4))|(0x3<<(16+(id)*4)))
#define RK3188_PLL_MODE_DEEP(id)	((0x2<<((id)*4))|(0x3<<(16+(id)*4)))

#endif
