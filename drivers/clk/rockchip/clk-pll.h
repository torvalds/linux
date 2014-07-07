#ifndef __RK_CLK_PLL_H
#define __RK_CLK_PLL_H

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/rockchip/cru.h>


#define CLK_LOOPS_JIFFY_REF	(11996091ULL)
#define CLK_LOOPS_RATE_REF	(1200UL) //Mhz
#define CLK_LOOPS_RECALC(rate)  \
	div_u64(CLK_LOOPS_JIFFY_REF*(rate),CLK_LOOPS_RATE_REF*MHZ)

#define CLK_DIV_PLUS_ONE_SET(i, shift, width)	\
	((((i)-1) << (shift)) | (((2<<(width)) - 1) << ((shift)+16)))

/*******************RK3188 PLL******************************/
#define RK3188_PLL_CON(i)	((i) * 4)
/*******************PLL WORK MODE*************************/
#define _RK3188_PLL_MODE_MSK		0x3
#define _RK3188_PLL_MODE_SLOW		0x0
#define _RK3188_PLL_MODE_NORM		0x1
#define _RK3188_PLL_MODE_DEEP		0x2

#define _RK3188_PLL_MODE_GET(offset, shift)	\
	((cru_readl(offset) >> (shift)) & _RK3188_PLL_MODE_MSK)

#define _RK3188_PLL_MODE_IS_SLOW(offset, shift)	\
	(_RK3188_PLL_MODE_GET(offset, shift) == _RK3188_PLL_MODE_SLOW)

#define _RK3188_PLL_MODE_IS_NORM(offset, shift)	\
	(_RK3188_PLL_MODE_GET(offset, shift) == _RK3188_PLL_MODE_NORM)

#define _RK3188_PLL_MODE_IS_DEEP(offset, shift)	\
	(_RK3188_PLL_MODE_GET(offset, shift) == _RK3188_PLL_MODE_DEEP)

#define _RK3188_PLL_MODE_SET(val, shift)	\
	((val) << (shift)) | CRU_W_MSK(shift, _RK3188_PLL_MODE_MSK)

#define _RK3188_PLL_MODE_SLOW_SET(shift)	\
	_RK3188_PLL_MODE_SET(_RK3188_PLL_MODE_SLOW, shift)

#define _RK3188_PLL_MODE_NORM_SET(shift)	\
	_RK3188_PLL_MODE_SET(_RK3188_PLL_MODE_NORM, shift)

#define _RK3188_PLL_MODE_DEEP_SET(shift)	\
	_RK3188_PLL_MODE_SET(_RK3188_PLL_MODE_DEEP, shift)

/*******************PLL OPERATION MODE*********************/
#define _RK3188_PLL_BYPASS_SHIFT	0
#define _RK3188_PLL_POWERDOWN_SHIFT	1

#define _RK3188PLUS_PLL_BYPASS_SHIFT	0
#define _RK3188PLUS_PLL_POWERDOWN_SHIFT	1
#define _RK3188PLUS_PLL_RESET_SHIFT	5

#define _RK3188_PLL_OP_SET(val, shift)	\
	((val) << (shift)) | CRU_W_MSK(shift, 1)

#define _RK3188_PLL_BYPASS_SET(val)	\
	_RK3188_PLL_OP_SET(val, _RK3188_PLL_BYPASS_SHIFT)

#define _RK3188_PLL_POWERDOWN_SET(val)	\
	_RK3188_PLL_OP_SET(val, _RK3188_PLL_POWERDOWN_SHIFT)

#define _RK3188PLUS_PLL_BYPASS_SET(val)	\
	_RK3188_PLL_OP_SET(val, _RK3188PLUS_PLL_BYPASS_SHIFT)

#define _RK3188PLUS_PLL_POWERDOWN_SET(val)	\
	_RK3188_PLL_OP_SET(val, _RK3188PLUS_PLL_POWERDOWN_SHIFT)

#define _RK3188PLUS_PLL_RESET_SET(val)	\
	_RK3188_PLL_OP_SET(val, _RK3188PLUS_PLL_RESET_SHIFT)

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

#define RK3188PLUS_PLL_OD_MSK		(0xf)
#define RK3188PLUS_PLL_OD_SHIFT 	(0x0)
#define RK3188PLUS_PLL_CLKOD(val)	RK3188_PLL_CLKFACTOR_SET(val, RK3188PLUS_PLL_OD_SHIFT, RK3188PLUS_PLL_OD_MSK)
#define RK3188PLUS_PLL_NO(reg)		RK3188_PLL_CLKFACTOR_GET(reg, RK3188PLUS_PLL_OD_SHIFT, RK3188PLUS_PLL_OD_MSK)
#define RK3188PLUS_PLL_CLKOD_SET(val)	(RK3188PLUS_PLL_CLKOD(val) | CRU_W_MSK(RK3188PLUS_PLL_OD_SHIFT, RK3188PLUS_PLL_OD_MSK))

#define RK3188PLUS_PLL_NR_MSK		(0x3f)
#define RK3188PLUS_PLL_NR_SHIFT		(8)
#define RK3188PLUS_PLL_CLKR(val)	RK3188_PLL_CLKFACTOR_SET(val, RK3188PLUS_PLL_NR_SHIFT, RK3188PLUS_PLL_NR_MSK)
#define RK3188PLUS_PLL_NR(reg)		RK3188_PLL_CLKFACTOR_GET(reg, RK3188PLUS_PLL_NR_SHIFT, RK3188PLUS_PLL_NR_MSK)
#define RK3188PLUS_PLL_CLKR_SET(val)	(RK3188PLUS_PLL_CLKR(val) | CRU_W_MSK(RK3188PLUS_PLL_NR_SHIFT, RK3188PLUS_PLL_NR_MSK))

/*******************PLL CON1 BITS***************************/
#define RK3188_PLL_NF_MSK		(0xffff)
#define RK3188_PLL_NF_SHIFT		(0)
#define RK3188_PLL_CLKF(val)		RK3188_PLL_CLKFACTOR_SET(val, RK3188_PLL_NF_SHIFT, RK3188_PLL_NF_MSK)
#define RK3188_PLL_NF(reg)		RK3188_PLL_CLKFACTOR_GET(reg, RK3188_PLL_NF_SHIFT, RK3188_PLL_NF_MSK)
#define RK3188_PLL_CLKF_SET(val)	(RK3188_PLL_CLKF(val) | CRU_W_MSK(RK3188_PLL_NF_SHIFT, RK3188_PLL_NF_MSK))

#define RK3188PLUS_PLL_NF_MSK		(0x1fff)
#define RK3188PLUS_PLL_NF_SHIFT		(0)
#define RK3188PLUS_PLL_CLKF(val)	RK3188_PLL_CLKFACTOR_SET(val, RK3188PLUS_PLL_NF_SHIFT, RK3188PLUS_PLL_NF_MSK)
#define RK3188PLUS_PLL_NF(reg)		RK3188_PLL_CLKFACTOR_GET(reg, RK3188PLUS_PLL_NF_SHIFT, RK3188PLUS_PLL_NF_MSK)
#define RK3188PLUS_PLL_CLKF_SET(val)	(RK3188PLUS_PLL_CLKF(val) | CRU_W_MSK(RK3188PLUS_PLL_NF_SHIFT, RK3188PLUS_PLL_NF_MSK))

/*******************PLL CON2 BITS***************************/
#define RK3188_PLL_BWADJ_MSK		(0xfff)
#define RK3188_PLL_BWADJ_SHIFT		(0)
#define RK3188_PLL_CLK_BWADJ_SET(val)	((val) | CRU_W_MSK(RK3188_PLL_BWADJ_SHIFT, RK3188_PLL_BWADJ_MSK))

#define RK3188PLUS_PLL_BWADJ_MSK	(0xfff)
#define RK3188PLUS_PLL_BWADJ_SHIFT	(0)
#define RK3188PLUS_PLL_CLK_BWADJ_SET(val)	((val) | CRU_W_MSK(RK3188PLUS_PLL_BWADJ_SHIFT, RK3188PLUS_PLL_BWADJ_MSK))

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

#define _RK3188PLUS_PLL_SET_CLKS(_mhz, nr, nf, no) \
{ \
	.rate   = (_mhz) * KHZ, \
	.pllcon0 = RK3188PLUS_PLL_CLKR_SET(nr)|RK3188PLUS_PLL_CLKOD_SET(no), \
	.pllcon1 = RK3188PLUS_PLL_CLKF_SET(nf),\
	.pllcon2 = RK3188PLUS_PLL_CLK_BWADJ_SET(nf >> 1),\
	.rst_dly = ((nr*500)/24+1),\
}

#define _RK3188_APLL_SET_CLKS(_mhz, nr, nf, no, _periph_div, _aclk_div) \
{ \
	.rate   = _mhz * MHZ, \
	.pllcon0 = RK3188_PLL_CLKR_SET(nr) | RK3188_PLL_CLKOD_SET(no), \
	.pllcon1 = RK3188_PLL_CLKF_SET(nf),\
	.pllcon2 = RK3188_PLL_CLK_BWADJ_SET(nf >> 1),\
	.rst_dly = ((nr*500)/24+1),\
	.clksel0 = RK3188_CORE_PERIPH_W_MSK | RK3188_CORE_PERIPH_##_periph_div,\
	.clksel1 = RK3188_CORE_ACLK_W_MSK | RK3188_CORE_ACLK_##_aclk_div,\
	.lpj = (CLK_LOOPS_JIFFY_REF*_mhz) / CLK_LOOPS_RATE_REF,\
}


/*******************RK3288 PLL***********************************/
/*******************CLKSEL0 BITS***************************/
#define RK3288_CORE_SEL_PLL_W_MSK	(1 << 31)
#define RK3288_CORE_SEL_APLL		(0 << 15)
#define RK3288_CORE_SEL_GPLL		(1 << 15)

#define RK3288_CORE_CLK_SHIFT		8
#define RK3288_CORE_CLK_WIDTH		5
#define RK3288_CORE_CLK_DIV(i)	\
	CLK_DIV_PLUS_ONE_SET(i, RK3288_CORE_CLK_SHIFT, RK3288_CORE_CLK_WIDTH)
#define RK3288_CORE_CLK_MAX_DIV		(2<<RK3288_CORE_CLK_WIDTH)

#define RK3288_ACLK_M0_SHIFT		0
#define RK3288_ACLK_M0_WIDTH		4
#define RK3288_ACLK_M0_DIV(i)	\
	CLK_DIV_PLUS_ONE_SET(i, RK3288_ACLK_M0_SHIFT, RK3288_ACLK_M0_WIDTH)

#define RK3288_ACLK_MP_SHIFT		4
#define RK3288_ACLK_MP_WIDTH		4
#define RK3288_ACLK_MP_DIV(i)	\
	CLK_DIV_PLUS_ONE_SET(i, RK3288_ACLK_MP_SHIFT, RK3288_ACLK_MP_WIDTH)

/*******************CLKSEL37 BITS***************************/
#define RK3288_CLK_L2RAM_SHIFT		0
#define RK3288_CLK_L2RAM_WIDTH		3
#define RK3288_CLK_L2RAM_DIV(i)	\
	CLK_DIV_PLUS_ONE_SET(i, RK3288_CLK_L2RAM_SHIFT, RK3288_CLK_L2RAM_WIDTH)

#define RK3288_ATCLK_SHIFT		4
#define RK3288_ATCLK_WIDTH		5
#define RK3288_ATCLK_DIV(i)	\
	CLK_DIV_PLUS_ONE_SET(i, RK3288_ATCLK_SHIFT, RK3288_ATCLK_WIDTH)

#define RK3288_PCLK_DBG_SHIFT		9
#define RK3288_PCLK_DBG_WIDTH		5
#define RK3288_PCLK_DBG_DIV(i)	\
	CLK_DIV_PLUS_ONE_SET(i, RK3288_PCLK_DBG_SHIFT, RK3288_PCLK_DBG_WIDTH)

#define _RK3288_APLL_SET_CLKS(_mhz, nr, nf, no, l2_div, m0_div, mp_div, atclk_div, pclk_dbg_div) \
{ \
	.rate   = _mhz * MHZ, \
	.pllcon0 = RK3188PLUS_PLL_CLKR_SET(nr) | RK3188PLUS_PLL_CLKOD_SET(no), \
	.pllcon1 = RK3188PLUS_PLL_CLKF_SET(nf),\
	.pllcon2 = RK3188PLUS_PLL_CLK_BWADJ_SET(nf >> 1),\
	.rst_dly = ((nr*500)/24+1),\
	.clksel0 = RK3288_ACLK_M0_DIV(m0_div) | RK3288_ACLK_MP_DIV(mp_div),\
	.clksel1 = RK3288_CLK_L2RAM_DIV(l2_div) | RK3288_ATCLK_DIV(atclk_div) | RK3288_PCLK_DBG_DIV(pclk_dbg_div),\
	.lpj = (CLK_LOOPS_JIFFY_REF*_mhz) / CLK_LOOPS_RATE_REF,\
}
/***************************RK3036 PLL**************************************/
#define LPJ_24M	(CLK_LOOPS_JIFFY_REF * 24) / CLK_LOOPS_RATE_REF
/*****cru reg offset*****/
#define RK3036_CRU_CLKSEL_CON		0x44
#define RK3036_CRU_CLKGATE_CON		0xd0
#define RK3036_CRU_GLB_SRST_FST	0x100
#define RK3036_CRU_GLB_SRST_SND	0x104
#define RK3036_CRU_SOFTRST_CON		0x110

#define RK3036_CRU_CLKSELS_CON_CNT	(35)
#define RK3036_CRU_CLKSELS_CON(i)	(RK3036_CRU_CLKSEL_CON + ((i) * 4))

#define RK3036_CRU_CLKGATES_CON_CNT	(10)
#define RK3036_CRU_CLKGATES_CON(i)	(RK3036_CRU_CLKGATE_CON + ((i) * 4))

#define RK3036_CRU_SOFTRSTS_CON_CNT	(9)
#define RK3036_CRU_SOFTRSTS_CON(i)	(RK3036_CRU_SOFTRST_CON + ((i) * 4))

/*PLL_CON 0,1,2*/
#define RK3036_PLL_PWR_ON			(0)
#define RK3036_PLL_PWR_DN			(1)
#define RK3036_PLL_BYPASS			(1 << 15)
#define RK3036_PLL_NO_BYPASS			(0 << 15)
/*con0*/
#define RK3036_PLL_BYPASS_SHIFT		(15)

#define RK3036_PLL_POSTDIV1_MASK		(0x7)
#define RK3036_PLL_POSTDIV1_SHIFT		(12)
#define RK3036_PLL_FBDIV_MASK			(0xfff)
#define RK3036_PLL_FBDIV_SHIFT			(0)

/*con1*/
#define RK3036_PLL_RSTMODE_SHIFT		(15)
#define RK3036_PLL_RST_SHIFT			(14)
#define RK3036_PLL_PWR_DN_SHIFT		(13)
#define RK3036_PLL_DSMPD_SHIFT			(12)
#define RK3036_PLL_LOCK_SHIFT			(10)

#define RK3036_PLL_POSTDIV2_MASK		(0x7)
#define RK3036_PLL_POSTDIV2_SHIFT		(6)
#define RK3036_PLL_REFDIV_MASK			(0x3f)
#define RK3036_PLL_REFDIV_SHIFT		(0)

/*con2*/
#define RK3036_PLL_FOUT4PHASE_PWR_DN_SHIFT	(27)
#define RK3036_PLL_FOUTVCO_PWR_DN_SHIFT	(26)
#define RK3036_PLL_FOUTPOSTDIV_PWR_DN_SHIFT	(25)
#define RK3036_PLL_DAC_PWR_DN_SHIFT		(24)

#define RK3036_PLL_FRAC_MASK			(0xffffff)
#define RK3036_PLL_FRAC_SHIFT			(0)

#define CRU_GET_REG_BIT_VAL(reg, bits_shift)		(((reg) >> (bits_shift)) & (0x1))
#define CRU_GET_REG_BITS_VAL(reg, bits_shift, msk)	(((reg) >> (bits_shift)) & (msk))
#define CRU_SET_BIT(val, bits_shift) 			(((val) & (0x1)) << (bits_shift))
#define CRU_W_MSK(bits_shift, msk)			((msk) << ((bits_shift) + 16))

#define CRU_W_MSK_SETBITS(val, bits_shift, msk) 	(CRU_W_MSK(bits_shift, msk)	\
							| CRU_SET_BITS(val, bits_shift, msk))
#define CRU_W_MSK_SETBIT(val, bits_shift) 		(CRU_W_MSK(bits_shift, 0x1)	\
							| CRU_SET_BIT(val, bits_shift))

#define RK3036_PLL_SET_REFDIV(val)				CRU_W_MSK_SETBITS(val, RK3036_PLL_REFDIV_SHIFT, RK3036_PLL_REFDIV_MASK)
#define RK3036_PLL_SET_FBDIV(val)				CRU_W_MSK_SETBITS(val, RK3036_PLL_FBDIV_SHIFT, RK3036_PLL_FBDIV_MASK)
#define RK3036_PLL_SET_POSTDIV1(val)				CRU_W_MSK_SETBITS(val, RK3036_PLL_POSTDIV1_SHIFT, RK3036_PLL_POSTDIV1_MASK)
#define RK3036_PLL_SET_POSTDIV2(val)				CRU_W_MSK_SETBITS(val, RK3036_PLL_POSTDIV2_SHIFT, RK3036_PLL_POSTDIV2_MASK)
#define RK3036_PLL_SET_FRAC(val)				CRU_SET_BITS(val, RK3036_PLL_FRAC_SHIFT, RK3036_PLL_FRAC_MASK)

#define RK3036_PLL_GET_REFDIV(reg)				CRU_GET_REG_BITS_VAL(reg, RK3036_PLL_REFDIV_SHIFT, RK3036_PLL_REFDIV_MASK)
#define RK3036_PLL_GET_FBDIV(reg)				CRU_GET_REG_BITS_VAL(reg, RK3036_PLL_FBDIV_SHIFT, RK3036_PLL_FBDIV_MASK)
#define RK3036_PLL_GET_POSTDIV1(reg)				CRU_GET_REG_BITS_VAL(reg, RK3036_PLL_POSTDIV1_SHIFT, RK3036_PLL_POSTDIV1_MASK)
#define RK3036_PLL_GET_POSTDIV2(reg)				CRU_GET_REG_BITS_VAL(reg, RK3036_PLL_POSTDIV2_SHIFT, RK3036_PLL_POSTDIV2_MASK)
#define RK3036_PLL_GET_FRAC(reg)				CRU_GET_REG_BITS_VAL(reg, RK3036_PLL_FRAC_SHIFT, RK3036_PLL_FRAC_MASK)

/*#define APLL_SET_BYPASS(val)				CRU_SET_BIT(val, PLL_BYPASS_SHIFT)*/
#define RK3036_PLL_SET_DSMPD(val)				CRU_W_MSK_SETBIT(val, RK3036_PLL_DSMPD_SHIFT)
#define RK3036_PLL_GET_DSMPD(reg)				CRU_GET_REG_BIT_VAL(reg, RK3036_PLL_DSMPD_SHIFT)

/*******************CLKSEL0 BITS***************************/
#define RK3036_CLK_SET_DIV_CON_SUB1(val, bits_shift, msk)	CRU_W_MSK_SETBITS((val - 1), bits_shift, msk)

#define RK3036_CPU_CLK_PLL_SEL_SHIFT		(14)
#define RK3036_CPU_CLK_PLL_SEL_MASK	(0x3)
#define RK3036_CORE_CLK_PLL_SEL_SHIFT		(7)
#define RK3036_SEL_APLL			(0)
#define RK3036_SEL_GPLL			(1)
#define RK3036_CPU_SEL_PLL(plls)		CRU_W_MSK_SETBITS(plls, RK3036_CPU_CLK_PLL_SEL_SHIFT, RK3036_CPU_CLK_PLL_SEL_MASK)
#define RK3036_CORE_SEL_PLL(plls)		CRU_W_MSK_SETBIT(plls, RK3036_CORE_CLK_PLL_SEL_SHIFT)

#define RK3036_ACLK_CPU_DIV_MASK		(0x1f)
#define RK3036_ACLK_CPU_DIV_SHIFT		(8)
#define RK3036_A9_CORE_DIV_MASK		(0x1f)
#define RK3036_A9_CORE_DIV_SHIFT		(0)

#define RATIO_11		(1)
#define RATIO_21		(2)
#define RATIO_41		(4)
#define RATIO_81		(8)

#define RK3036_ACLK_CPU_DIV(val)		RK3036_CLK_SET_DIV_CON_SUB1(val, RK3036_ACLK_CPU_DIV_SHIFT, RK3036_ACLK_CPU_DIV_MASK)
#define RK3036_CLK_CORE_DIV(val)		RK3036_CLK_SET_DIV_CON_SUB1(val, RK3036_A9_CORE_DIV_SHIFT, RK3036_A9_CORE_DIV_MASK)
/*******************CLKSEL1 BITS***************************/
#define RK3036_PCLK_CPU_DIV_MASK		(0x7)
#define RK3036_PCLK_CPU_DIV_SHIFT		(12)
#define RK3036_HCLK_CPU_DIV_MASK		(0x3)
#define RK3036_HCLK_CPU_DIV_SHIFT		(8)
#define RK3036_ACLK_CORE_DIV_MASK		(0x7)
#define RK3036_ACLK_CORE_DIV_SHIFT		(4)
#define RK3036_CORE_PERIPH_DIV_MASK		(0xf)
#define RK3036_CORE_PERIPH_DIV_SHIFT		(0)

#define RK3036_PCLK_CPU_DIV(val)		RK3036_CLK_SET_DIV_CON_SUB1(val, RK3036_PCLK_CPU_DIV_SHIFT, RK3036_PCLK_CPU_DIV_MASK)
#define RK3036_HCLK_CPU_DIV(val)		RK3036_CLK_SET_DIV_CON_SUB1(val, RK3036_HCLK_CPU_DIV_SHIFT, RK3036_HCLK_CPU_DIV_MASK)
#define RK3036_ACLK_CORE_DIV(val)		RK3036_CLK_SET_DIV_CON_SUB1(val, RK3036_ACLK_CORE_DIV_SHIFT, RK3036_ACLK_CORE_DIV_MASK)
#define RK3036_CLK_CORE_PERI_DIV(val)		RK3036_CLK_SET_DIV_CON_SUB1(val, RK3036_CORE_PERIPH_DIV_SHIFT, RK3036_CORE_PERIPH_DIV_MASK)

/*******************clksel10***************************/
#define RK3036_PERI_PLL_SEL_SHIFT	14
#define RK3036_PERI_PLL_SEL_MASK	(0x3)
#define RK3036_PERI_PCLK_DIV_MASK	(0x3)
#define RK3036_PERI_PCLK_DIV_SHIFT	(12)
#define RK3036_PERI_HCLK_DIV_MASK	(0x3)
#define RK3036_PERI_HCLK_DIV_SHIFT	(8)
#define RK3036_PERI_ACLK_DIV_MASK	(0x1f)
#define RK3036_PERI_ACLK_DIV_SHIFT	(0)

#define RK3036_SEL_3PLL_APLL		(0)
#define RK3036_SEL_3PLL_DPLL		(1)
#define RK3036_SEL_3PLL_GPLL		(2)


#define RK3036_PERI_CLK_SEL_PLL(plls)	CRU_W_MSK_SETBITS(plls, RK3036_PERI_PLL_SEL_SHIFT, RK3036_PERI_PLL_SEL_MASK)
#define RK3036_PERI_SET_ACLK_DIV(val)		RK3036_CLK_SET_DIV_CON_SUB1(val, RK3036_PERI_ACLK_DIV_SHIFT, RK3036_PERI_ACLK_DIV_MASK)

/*******************gate BITS***************************/
#define RK3036_CLK_GATE_CLKID_CONS(i)	RK3036_CRU_CLKGATES_CON((i) / 16)

#define RK3036_CLK_GATE(i)		(1 << ((i)%16))
#define RK3036_CLK_UN_GATE(i)		(0)

#define RK3036_CLK_GATE_W_MSK(i)	(1 << (((i) % 16) + 16))
#define RK3036_CLK_GATE_CLKID(i)	(16 * (i))

#define _RK3036_APLL_SET_CLKS(_mhz, _refdiv, _fbdiv, _postdiv1, _postdiv2, _dsmpd, _frac, \
		_periph_div, _aclk_core_div, _axi_div, _apb_div, _ahb_div) \
{ \
	.rate	= (_mhz) * MHZ,	\
	.pllcon0 = RK3036_PLL_SET_POSTDIV1(_postdiv1) | RK3036_PLL_SET_FBDIV(_fbdiv),	\
	.pllcon1 = RK3036_PLL_SET_DSMPD(_dsmpd) | RK3036_PLL_SET_POSTDIV2(_postdiv2) | RK3036_PLL_SET_REFDIV(_refdiv),	\
	.pllcon2 = RK3036_PLL_SET_FRAC(_frac),	\
	.clksel1 = RK3036_ACLK_CORE_DIV(RATIO_##_aclk_core_div) | RK3036_CLK_CORE_PERI_DIV(RATIO_##_periph_div),	\
	.lpj	= (CLK_LOOPS_JIFFY_REF * _mhz) / CLK_LOOPS_RATE_REF,	\
	.rst_dly = 0,\
}

#define _RK3036_PLL_SET_CLKS(_mhz, _refdiv, _fbdiv, _postdiv1, _postdiv2, _dsmpd, _frac) \
{ \
	.rate	= (_mhz) * KHZ, \
	.pllcon0 = RK3036_PLL_SET_POSTDIV1(_postdiv1) | RK3036_PLL_SET_FBDIV(_fbdiv),	\
	.pllcon1 = RK3036_PLL_SET_DSMPD(_dsmpd) | RK3036_PLL_SET_POSTDIV2(_postdiv2) | RK3036_PLL_SET_REFDIV(_refdiv),	\
	.pllcon2 = RK3036_PLL_SET_FRAC(_frac),	\
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
	u32		reg;
	u32		width;
	u32		mode_offset;
	u8		mode_shift;
	u32		status_offset;
	u8		status_shift;
	u32		flags;
	const void	*table;
	spinlock_t	*lock;
};

const struct clk_ops *rk_get_pll_ops(u32 pll_flags);

struct clk *rk_clk_register_pll(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags, u32 reg,
		u32 width, u32 mode_offset, u8 mode_shift,
		u32 status_offset, u8 status_shift, u32 pll_flags,
		spinlock_t *lock);


#endif /* __RK_CLK_PLL_H */
