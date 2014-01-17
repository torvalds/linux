#ifndef __RK_CLK_PLL_H
#define __RK_CLK_PLL_H

#include <linux/clk-provider.h>
#include <linux/delay.h>


#define CLK_LOOPS_JIFFY_REF	(11996091ULL)
#define CLK_LOOPS_RATE_REF	(1200UL) //Mhz
#define CLK_LOOPS_RECALC(rate)  \
	div_u64(CLK_LOOPS_JIFFY_REF*(rate),CLK_LOOPS_RATE_REF*MHZ)
/*******************cru reg offset***************************/
#define CRU_MODE_CON		0x40
#define PLL_CONS(id, i)		((id) * 0x10 + ((i) * 4))

/*******************cru BITS*********************************/
#define CRU_GET_REG_BITS_VAL(reg,bits_shift, msk)  (((reg) >> (bits_shift))&(msk))
#define CRU_W_MSK(bits_shift, msk)	((msk) << ((bits_shift) + 16))
#define CRU_SET_BITS(val,bits_shift, msk)	(((val)&(msk)) << (bits_shift))
#define CRU_W_MSK_SETBITS(val,bits_shift,msk) \
	(CRU_W_MSK(bits_shift, msk)|CRU_SET_BITS(val,bits_shift, msk))

/*******************PLL CON0 BITS***************************/
#define PLL_CLKFACTOR_SET(val, shift, msk) \
	((((val) - 1) & (msk)) << (shift))

#define PLL_CLKFACTOR_GET(reg, shift, msk) \
	((((reg) >> (shift)) & (msk)) + 1)

#define PLL_OD_MSK		(0x3f)
#define PLL_OD_SHIFT 		(0x0)

#define PLL_CLKOD(val)		PLL_CLKFACTOR_SET(val, PLL_OD_SHIFT, PLL_OD_MSK)
#define PLL_NO(reg)		PLL_CLKFACTOR_GET(reg, PLL_OD_SHIFT, PLL_OD_MSK)

#define PLL_NO_SHIFT(reg)	PLL_CLKFACTOR_GET(reg, PLL_OD_SHIFT, PLL_OD_MSK)

#define PLL_CLKOD_SET(val)	(PLL_CLKOD(val) | CRU_W_MSK(PLL_OD_SHIFT, PLL_OD_MSK))

#define PLL_NR_MSK		(0x3f)
#define PLL_NR_SHIFT		(8)
#define PLL_CLKR(val)		PLL_CLKFACTOR_SET(val, PLL_NR_SHIFT, PLL_NR_MSK)
#define PLL_NR(reg)		PLL_CLKFACTOR_GET(reg, PLL_NR_SHIFT, PLL_NR_MSK)

#define PLL_CLKR_SET(val)	(PLL_CLKR(val) | CRU_W_MSK(PLL_NR_SHIFT, PLL_NR_MSK))

#define PLUS_PLL_OD_MSK		(0xf)
#define PLUS_PLL_NO(reg)	(PLL_NO(reg) & PLUS_PLL_OD_MSK)

#define PLUS_PLL_NR_MSK		(0x3f)
#define PLUS_PLL_NR(reg)	(PLL_NR(reg) & PLUS_PLL_NR_MSK)

#define PLUS_PLL_CLKR_SET(val)	PLL_CLKR_SET(val & PLUS_PLL_NR_MSK)
#define PLUS_PLL_CLKOD_SET(val)	PLL_CLKOD_SET(val & PLUS_PLL_OD_MSK)

/*******************PLL CON1 BITS***************************/
#define PLL_NF_MSK		(0xffff)
#define PLL_NF_SHIFT		(0)
#define PLL_CLKF(val)		PLL_CLKFACTOR_SET(val, PLL_NF_SHIFT, PLL_NF_MSK)
#define PLL_NF(reg)		PLL_CLKFACTOR_GET(reg, PLL_NF_SHIFT, PLL_NF_MSK)
#define PLL_CLKF_SET(val)	(PLL_CLKF(val) | CRU_W_MSK(PLL_NF_SHIFT, PLL_NF_MSK))

#define PLUS_PLL_NF_MSK		(0x1fff)
#define PLUS_PLL_NF(reg)	(PLL_NF(reg) & PLUS_PLL_NF_MSK)
#define PLUS_PLL_CLKF_SET(val)	PLL_CLKF_SET(val & PLUS_PLL_NF_MSK)

/*******************PLL CON2 BITS***************************/
#define PLL_BWADJ_MSK		(0xfff)
#define PLL_BWADJ_SHIFT		(0)
#define PLL_CLK_BWADJ_SET(val)	((val) | CRU_W_MSK(PLL_BWADJ_SHIFT, PLL_BWADJ_MSK))

/*******************PLL CON3 BITS***************************/
#define PLL_RESET_MSK		(1 << 5)
#define PLL_RESET_W_MSK		(PLL_RESET_MSK << 16)
#define PLL_RESET		(1 << 5)
#define PLL_RESET_RESUME	(0 << 5)

#define PLL_BYPASS_MSK		(1 << 0)
#define PLL_BYPASS		(1 << 0)
#define PLL_NO_BYPASS		(0 << 0)

#define PLL_PWR_DN_MSK		(1 << 1)
#define PLL_PWR_DN_W_MSK	(PLL_PWR_DN_MSK << 16)
#define PLL_PWR_DN		(1 << 1)
#define PLL_PWR_ON		(0 << 1)

#define PLL_STANDBY_MSK		(1 << 2)
#define PLL_STANDBY		(1 << 2)
#define PLL_NO_STANDBY		(0 << 2)

/*******************PLL MODE BITS***************************/
#define PLL_MODE_MSK(id)	(0x3 << ((id) * 4))
#define PLL_MODE_SLOW(id)	((0x0<<((id)*4))|(0x3<<(16+(id)*4)))
#define PLL_MODE_NORM(id)	((0x1<<((id)*4))|(0x3<<(16+(id)*4)))
#define PLL_MODE_DEEP(id)	((0x2<<((id)*4))|(0x3<<(16+(id)*4)))

/*******************PLL SET*********************************/
#define _PLL_SET_CLKS(_mhz, nr, nf, no) \
{ \
	.rate   = (_mhz) * KHZ, \
	.pllcon0 = PLL_CLKR_SET(nr)|PLL_CLKOD_SET(no), \
	.pllcon1 = PLL_CLKF_SET(nf),\
	.pllcon2 = PLL_CLK_BWADJ_SET(nf >> 1),\
	.rst_dly=((nr*500)/24+1),\
}
/*******************OTHERS*********************************/
#define rk30_clock_udelay(a) udelay(a)



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
