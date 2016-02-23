/*
 * Copyright 2011 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef RV740_H
#define RV740_H

#define	CG_SPLL_FUNC_CNTL				0x600
#define		SPLL_RESET				(1 << 0)
#define		SPLL_SLEEP				(1 << 1)
#define		SPLL_BYPASS_EN				(1 << 3)
#define		SPLL_REF_DIV(x)				((x) << 4)
#define		SPLL_REF_DIV_MASK			(0x3f << 4)
#define		SPLL_PDIV_A(x)				((x) << 20)
#define		SPLL_PDIV_A_MASK			(0x7f << 20)
#define	CG_SPLL_FUNC_CNTL_2				0x604
#define		SCLK_MUX_SEL(x)				((x) << 0)
#define		SCLK_MUX_SEL_MASK			(0x1ff << 0)
#define	CG_SPLL_FUNC_CNTL_3				0x608
#define		SPLL_FB_DIV(x)				((x) << 0)
#define		SPLL_FB_DIV_MASK			(0x3ffffff << 0)
#define		SPLL_DITHEN				(1 << 28)

#define	MPLL_CNTL_MODE					0x61c
#define		SS_SSEN					(1 << 24)

#define	MPLL_AD_FUNC_CNTL				0x624
#define		CLKF(x)					((x) << 0)
#define		CLKF_MASK				(0x7f << 0)
#define		CLKR(x)					((x) << 7)
#define		CLKR_MASK				(0x1f << 7)
#define		CLKFRAC(x)				((x) << 12)
#define		CLKFRAC_MASK				(0x1f << 12)
#define		YCLK_POST_DIV(x)			((x) << 17)
#define		YCLK_POST_DIV_MASK			(3 << 17)
#define		IBIAS(x)				((x) << 20)
#define		IBIAS_MASK				(0x3ff << 20)
#define		RESET					(1 << 30)
#define		PDNB					(1 << 31)
#define	MPLL_AD_FUNC_CNTL_2				0x628
#define		BYPASS					(1 << 19)
#define		BIAS_GEN_PDNB				(1 << 24)
#define		RESET_EN				(1 << 25)
#define		VCO_MODE				(1 << 29)
#define	MPLL_DQ_FUNC_CNTL				0x62c
#define	MPLL_DQ_FUNC_CNTL_2				0x630

#define	MCLK_PWRMGT_CNTL				0x648
#define		DLL_SPEED(x)				((x) << 0)
#define		DLL_SPEED_MASK				(0x1f << 0)
#       define MPLL_PWRMGT_OFF                          (1 << 5)
#       define DLL_READY                                (1 << 6)
#       define MC_INT_CNTL                              (1 << 7)
#       define MRDCKA0_SLEEP                            (1 << 8)
#       define MRDCKA1_SLEEP                            (1 << 9)
#       define MRDCKB0_SLEEP                            (1 << 10)
#       define MRDCKB1_SLEEP                            (1 << 11)
#       define MRDCKC0_SLEEP                            (1 << 12)
#       define MRDCKC1_SLEEP                            (1 << 13)
#       define MRDCKD0_SLEEP                            (1 << 14)
#       define MRDCKD1_SLEEP                            (1 << 15)
#       define MRDCKA0_RESET                            (1 << 16)
#       define MRDCKA1_RESET                            (1 << 17)
#       define MRDCKB0_RESET                            (1 << 18)
#       define MRDCKB1_RESET                            (1 << 19)
#       define MRDCKC0_RESET                            (1 << 20)
#       define MRDCKC1_RESET                            (1 << 21)
#       define MRDCKD0_RESET                            (1 << 22)
#       define MRDCKD1_RESET                            (1 << 23)
#       define DLL_READY_READ                           (1 << 24)
#       define USE_DISPLAY_GAP                          (1 << 25)
#       define USE_DISPLAY_URGENT_NORMAL                (1 << 26)
#       define MPLL_TURNOFF_D2                          (1 << 28)
#define	DLL_CNTL					0x64c
#       define MRDCKA0_BYPASS                           (1 << 24)
#       define MRDCKA1_BYPASS                           (1 << 25)
#       define MRDCKB0_BYPASS                           (1 << 26)
#       define MRDCKB1_BYPASS                           (1 << 27)
#       define MRDCKC0_BYPASS                           (1 << 28)
#       define MRDCKC1_BYPASS                           (1 << 29)
#       define MRDCKD0_BYPASS                           (1 << 30)
#       define MRDCKD1_BYPASS                           (1 << 31)

#define	CG_SPLL_SPREAD_SPECTRUM				0x790
#define		SSEN					(1 << 0)
#define		CLK_S(x)				((x) << 4)
#define		CLK_S_MASK				(0xfff << 4)
#define	CG_SPLL_SPREAD_SPECTRUM_2			0x794
#define		CLK_V(x)				((x) << 0)
#define		CLK_V_MASK				(0x3ffffff << 0)

#define	MPLL_SS1					0x85c
#define		CLKV(x)					((x) << 0)
#define		CLKV_MASK				(0x3ffffff << 0)
#define	MPLL_SS2					0x860
#define		CLKS(x)					((x) << 0)
#define		CLKS_MASK				(0xfff << 0)

#endif
