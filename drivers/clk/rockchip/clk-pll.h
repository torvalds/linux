#ifndef __RK_CLK_PLL_H
#define __RK_CLK_PLL_H

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include "../../../arch/arm/mach-rockchip/cru.h"


#define CLK_LOOPS_JIFFY_REF	(11996091ULL)
#define CLK_LOOPS_RATE_REF	(1200UL) //Mhz
#define CLK_LOOPS_RECALC(rate)  \
	div_u64(CLK_LOOPS_JIFFY_REF*(rate),CLK_LOOPS_RATE_REF*MHZ)

/*******************RK3188 PLL******************************/

/*******************PLL CON0 BITS***************************/
#define RK3188_PLL_CLKFACTOR_SET(val, shift, msk) \
	((((val) - 1) & (msk)) << (shift))

#define RK3188_PLL_CLKFACTOR_GET(reg, shift, msk) \
	((((reg) >> (shift)) & (msk)) + 1)

#define RK3188_PLL_OD_MSK		(0x3f)
#define RK3188_PLL_OD_SHIFT 		(0x0)
#define RK3188_PLL_CLKOD(val)		RK3188_PLL_CLKFACTOR_SET(val, RK3188_PLL_OD_SHIFT, RK3188_PLL_OD_MSK)
#define RK3188_PLL_NO(reg)		RK3188_PLL_CLKFACTOR_GET(reg, RK3188_PLL_OD_SHIFT, RK3188_PLL_OD_MSK)
#define RK3188_PLL_CLKOD_SET(val)	(RK3188_PLL_CLKOD(val) | CRU_W_MSK(RK3188_PLL_OD_SHIFT, RK3188_PLL_OD_MSK))

#define RK3188_PLL_NR_MSK		(0x3f)
#define RK3188_PLL_NR_SHIFT		(8)
#define RK3188_PLL_CLKR(val)		RK3188_PLL_CLKFACTOR_SET(val, RK3188_PLL_NR_SHIFT, RK3188_PLL_NR_MSK)
#define RK3188_PLL_NR(reg)		RK3188_PLL_CLKFACTOR_GET(reg, RK3188_PLL_NR_SHIFT, RK3188_PLL_NR_MSK)
#define RK3188_PLL_CLKR_SET(val)	(RK3188_PLL_CLKR(val) | CRU_W_MSK(RK3188_PLL_NR_SHIFT, RK3188_PLL_NR_MSK))

/*FIXME*/
#define RK3188PLUS_PLL_OD_MSK		(0xf)
#define RK3188PLUS_PLL_NO(reg)		(RK3188_PLL_NO(reg) & RK3188PLUS_PLL_OD_MSK)

#define RK3188PLUS_PLL_NR_MSK		(0x3f)
#define RK3188PLUS_PLL_NR(reg)		(RK3188_PLL_NR(reg) & RK3188PLUS_PLL_NR_MSK)

#define RK3188PLUS_PLL_CLKR_SET(val)	RK3188_PLL_CLKR_SET(val & RK3188PLUS_PLL_NR_MSK)
#define RK3188PLUS_PLL_CLKOD_SET(val)	RK3188_PLL_CLKOD_SET(val & RK3188PLUS_PLL_OD_MSK)

/*******************PLL CON1 BITS***************************/
#define RK3188_PLL_NF_MSK		(0xffff)
#define RK3188_PLL_NF_SHIFT		(0)
#define RK3188_PLL_CLKF(val)		RK3188_PLL_CLKFACTOR_SET(val, RK3188_PLL_NF_SHIFT, RK3188_PLL_NF_MSK)
#define RK3188_PLL_NF(reg)		RK3188_PLL_CLKFACTOR_GET(reg, RK3188_PLL_NF_SHIFT, RK3188_PLL_NF_MSK)
#define RK3188_PLL_CLKF_SET(val)	(RK3188_PLL_CLKF(val) | CRU_W_MSK(RK3188_PLL_NF_SHIFT, RK3188_PLL_NF_MSK))

#define RK3188PLUS_PLL_NF_MSK		(0x1fff)
#define RK3188PLUS_PLL_NF(reg)		(RK3188_PLL_NF(reg) & RK3188PLUS_PLL_NF_MSK)
#define RK3188PLUS_PLL_CLKF_SET(val)	RK3188_PLL_CLKF_SET(val & RK3188PLUS_PLL_NF_MSK)

/*******************PLL CON2 BITS***************************/
#define RK3188_PLL_BWADJ_MSK		(0xfff)
#define RK3188_PLL_BWADJ_SHIFT		(0)
#define RK3188_PLL_CLK_BWADJ_SET(val)	((val) | CRU_W_MSK(RK3188_PLL_BWADJ_SHIFT, RK3188_PLL_BWADJ_MSK))

/*******************PLL CON3 BITS***************************/
#define RK3188_PLL_RESET_MSK		(1 << 5)
#define RK3188_PLL_RESET_W_MSK		(RK3188_PLL_RESET_MSK << 16)
#define RK3188_PLL_RESET		(1 << 5)
#define RK3188_PLL_RESET_RESUME		(0 << 5)

#define RK3188_PLL_BYPASS_MSK		(1 << 0)
#define RK3188_PLL_BYPASS		(1 << 0)
#define RK3188_PLL_NO_BYPASS		(0 << 0)

#define RK3188_PLL_PWR_DN_MSK		(1 << 1)
#define RK3188_PLL_PWR_DN_W_MSK		(RK3188_PLL_PWR_DN_MSK << 16)
#define RK3188_PLL_PWR_DN		(1 << 1)
#define RK3188_PLL_PWR_ON		(0 << 1)

#define RK3188_PLL_STANDBY_MSK		(1 << 2)
#define RK3188_PLL_STANDBY		(1 << 2)
#define RK3188_PLL_NO_STANDBY		(0 << 2)

/*******************CLKSEL0 BITS***************************/
//core_preiph div
#define RK3188_CORE_PERIPH_W_MSK	(3 << 22)
#define RK3188_CORE_PERIPH_MSK		(3 << 6)
#define RK3188_CORE_PERIPH_2		(0 << 6)
#define RK3188_CORE_PERIPH_4		(1 << 6)
#define RK3188_CORE_PERIPH_8		(2 << 6)
#define RK3188_CORE_PERIPH_16		(3 << 6)

//clk_core
#define RK3188_CORE_SEL_PLL_MSK		(1 << 8)
#define RK3188_CORE_SEL_PLL_W_MSK	(1 << 24)
#define RK3188_CORE_SEL_APLL		(0 << 8)
#define RK3188_CORE_SEL_GPLL		(1 << 8)

#define RK3188_CORE_CLK_DIV_W_MSK	(0x1F << 25)
#define RK3188_CORE_CLK_DIV_MSK		(0x1F << 9)
#define RK3188_CORE_CLK_DIV(i)		((((i) - 1) & 0x1F) << 9)
#define RK3188_CORE_CLK_MAX_DIV		32

/*******************CLKSEL1 BITS***************************/
//aclk_core div
#define RK3188_CORE_ACLK_W_MSK		(7 << 19)
#define RK3188_CORE_ACLK_MSK		(7 << 3)
#define RK3188_CORE_ACLK_11		(0 << 3)
#define RK3188_CORE_ACLK_21		(1 << 3)
#define RK3188_CORE_ACLK_31		(2 << 3)
#define RK3188_CORE_ACLK_41		(3 << 3)
#define RK3188_CORE_ACLK_81		(4 << 3)
#define RK3188_GET_CORE_ACLK_VAL(reg)	((reg)>=4 ? 8:((reg)+1))

/*******************PLL SET*********************************/
#define _RK3188_PLL_SET_CLKS(_mhz, nr, nf, no) \
{ \
	.rate   = (_mhz) * KHZ, \
	.pllcon0 = RK3188_PLL_CLKR_SET(nr)|RK3188_PLL_CLKOD_SET(no), \
	.pllcon1 = RK3188_PLL_CLKF_SET(nf),\
	.pllcon2 = RK3188_PLL_CLK_BWADJ_SET(nf >> 1),\
	.rst_dly = ((nr*500)/24+1),\
}

#define _RK3188_APLL_SET_CLKS(_mhz, nr, nf, no, _periph_div, _aclk_div) \
{ \
        .rate   = _mhz * MHZ, \
        .pllcon0 = RK3188_PLL_CLKR_SET(nr) | RK3188_PLL_CLKOD_SET(no), \
        .pllcon1 = RK3188_PLL_CLKF_SET(nf),\
        .pllcon2 = RK3188_PLL_CLK_BWADJ_SET(nf >> 1),\
        .clksel0 = RK3188_CORE_PERIPH_W_MSK | RK3188_CORE_PERIPH_##_periph_div,\
        .clksel1 = RK3188_CORE_ACLK_W_MSK | RK3188_CORE_ACLK_##_aclk_div,\
        .lpj = (CLK_LOOPS_JIFFY_REF*_mhz) / CLK_LOOPS_RATE_REF,\
        .rst_dly = ((nr*500)/24+1),\
}


struct pll_clk_set {
	unsigned long	rate;
	u32	pllcon0;
	u32	pllcon1;
	u32	pllcon2;
	unsigned long	rst_dly;//us
};

struct apll_clk_set {
	unsigned long	rate;
	u32	pllcon0;
	u32	pllcon1;
	u32	pllcon2;
	u32 	rst_dly;//us
	u32	clksel0;
	u32	clksel1;
	unsigned long	lpj;
};


#define to_clk_pll(_hw) container_of(_hw, struct clk_pll, hw)

struct clk_pll {
	struct clk_hw	hw;
	void __iomem	*reg;
	u32		width;
	u8		id;
	const void	*table;
	spinlock_t	*lock;
};

extern const struct clk_ops clk_pll_ops;
struct clk *rk_clk_register_pll(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags, void __iomem *reg,
		u32 width, u8 id, spinlock_t *lock);


#endif /* __RK_CLK_PLL_H */
