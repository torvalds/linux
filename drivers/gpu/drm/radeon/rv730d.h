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
#ifndef RV730_H
#define RV730_H

#define	CG_SPLL_FUNC_CNTL				0x600
#define		SPLL_RESET				(1 << 0)
#define		SPLL_SLEEP				(1 << 1)
#define		SPLL_DIVEN				(1 << 2)
#define		SPLL_BYPASS_EN				(1 << 3)
#define		SPLL_REF_DIV(x)				((x) << 4)
#define		SPLL_REF_DIV_MASK			(0x3f << 4)
#define		SPLL_HILEN(x)				((x) << 12)
#define		SPLL_HILEN_MASK				(0xf << 12)
#define		SPLL_LOLEN(x)				((x) << 16)
#define		SPLL_LOLEN_MASK				(0xf << 16)
#define	CG_SPLL_FUNC_CNTL_2				0x604
#define		SCLK_MUX_SEL(x)				((x) << 0)
#define		SCLK_MUX_SEL_MASK			(0x1ff << 0)
#define	CG_SPLL_FUNC_CNTL_3				0x608
#define		SPLL_FB_DIV(x)				((x) << 0)
#define		SPLL_FB_DIV_MASK			(0x3ffffff << 0)
#define		SPLL_DITHEN				(1 << 28)

#define	CG_MPLL_FUNC_CNTL				0x624
#define		MPLL_RESET				(1 << 0)
#define		MPLL_SLEEP				(1 << 1)
#define		MPLL_DIVEN				(1 << 2)
#define		MPLL_BYPASS_EN				(1 << 3)
#define		MPLL_REF_DIV(x)				((x) << 4)
#define		MPLL_REF_DIV_MASK			(0x3f << 4)
#define		MPLL_HILEN(x)				((x) << 12)
#define		MPLL_HILEN_MASK				(0xf << 12)
#define		MPLL_LOLEN(x)				((x) << 16)
#define		MPLL_LOLEN_MASK				(0xf << 16)
#define	CG_MPLL_FUNC_CNTL_2				0x628
#define		MCLK_MUX_SEL(x)				((x) << 0)
#define		MCLK_MUX_SEL_MASK			(0x1ff << 0)
#define	CG_MPLL_FUNC_CNTL_3				0x62c
#define		MPLL_FB_DIV(x)				((x) << 0)
#define		MPLL_FB_DIV_MASK			(0x3ffffff << 0)
#define		MPLL_DITHEN				(1 << 28)

#define	CG_TCI_MPLL_SPREAD_SPECTRUM			0x634
#define	CG_TCI_MPLL_SPREAD_SPECTRUM_2			0x638
#define GENERAL_PWRMGT                                  0x63c
#       define GLOBAL_PWRMGT_EN                         (1 << 0)
#       define STATIC_PM_EN                             (1 << 1)
#       define THERMAL_PROTECTION_DIS                   (1 << 2)
#       define THERMAL_PROTECTION_TYPE                  (1 << 3)
#       define ENABLE_GEN2PCIE                          (1 << 4)
#       define ENABLE_GEN2XSP                           (1 << 5)
#       define SW_SMIO_INDEX(x)                         ((x) << 6)
#       define SW_SMIO_INDEX_MASK                       (3 << 6)
#       define LOW_VOLT_D2_ACPI                         (1 << 8)
#       define LOW_VOLT_D3_ACPI                         (1 << 9)
#       define VOLT_PWRMGT_EN                           (1 << 10)
#       define BACKBIAS_PAD_EN                          (1 << 18)
#       define BACKBIAS_VALUE                           (1 << 19)
#       define DYN_SPREAD_SPECTRUM_EN                   (1 << 23)
#       define AC_DC_SW                                 (1 << 24)

#define SCLK_PWRMGT_CNTL                                  0x644
#       define SCLK_PWRMGT_OFF                            (1 << 0)
#       define SCLK_LOW_D1                                (1 << 1)
#       define FIR_RESET                                  (1 << 4)
#       define FIR_FORCE_TREND_SEL                        (1 << 5)
#       define FIR_TREND_MODE                             (1 << 6)
#       define DYN_GFX_CLK_OFF_EN                         (1 << 7)
#       define GFX_CLK_FORCE_ON                           (1 << 8)
#       define GFX_CLK_REQUEST_OFF                        (1 << 9)
#       define GFX_CLK_FORCE_OFF                          (1 << 10)
#       define GFX_CLK_OFF_ACPI_D1                        (1 << 11)
#       define GFX_CLK_OFF_ACPI_D2                        (1 << 12)
#       define GFX_CLK_OFF_ACPI_D3                        (1 << 13)

#define	TCI_MCLK_PWRMGT_CNTL				0x648
#       define MPLL_PWRMGT_OFF                          (1 << 5)
#       define DLL_READY                                (1 << 6)
#       define MC_INT_CNTL                              (1 << 7)
#       define MRDCKA_SLEEP                             (1 << 8)
#       define MRDCKB_SLEEP                             (1 << 9)
#       define MRDCKC_SLEEP                             (1 << 10)
#       define MRDCKD_SLEEP                             (1 << 11)
#       define MRDCKE_SLEEP                             (1 << 12)
#       define MRDCKF_SLEEP                             (1 << 13)
#       define MRDCKG_SLEEP                             (1 << 14)
#       define MRDCKH_SLEEP                             (1 << 15)
#       define MRDCKA_RESET                             (1 << 16)
#       define MRDCKB_RESET                             (1 << 17)
#       define MRDCKC_RESET                             (1 << 18)
#       define MRDCKD_RESET                             (1 << 19)
#       define MRDCKE_RESET                             (1 << 20)
#       define MRDCKF_RESET                             (1 << 21)
#       define MRDCKG_RESET                             (1 << 22)
#       define MRDCKH_RESET                             (1 << 23)
#       define DLL_READY_READ                           (1 << 24)
#       define USE_DISPLAY_GAP                          (1 << 25)
#       define USE_DISPLAY_URGENT_NORMAL                (1 << 26)
#       define MPLL_TURNOFF_D2                          (1 << 28)
#define	TCI_DLL_CNTL					0x64c

#define	CG_PG_CNTL					0x858
#       define PWRGATE_ENABLE                           (1 << 0)

#define	CG_AT				                0x6d4
#define		CG_R(x)					((x) << 0)
#define		CG_R_MASK				(0xffff << 0)
#define		CG_L(x)					((x) << 16)
#define		CG_L_MASK				(0xffff << 16)

#define	CG_SPLL_SPREAD_SPECTRUM				0x790
#define		SSEN					(1 << 0)
#define		CLK_S(x)				((x) << 4)
#define		CLK_S_MASK				(0xfff << 4)
#define	CG_SPLL_SPREAD_SPECTRUM_2			0x794
#define		CLK_V(x)				((x) << 0)
#define		CLK_V_MASK				(0x3ffffff << 0)

#define	MC_ARB_DRAM_TIMING				0x2774
#define	MC_ARB_DRAM_TIMING2				0x2778

#define	MC_ARB_RFSH_RATE				0x27b0
#define		POWERMODE0(x)				((x) << 0)
#define		POWERMODE0_MASK				(0xff << 0)
#define		POWERMODE1(x)				((x) << 8)
#define		POWERMODE1_MASK				(0xff << 8)
#define		POWERMODE2(x)				((x) << 16)
#define		POWERMODE2_MASK				(0xff << 16)
#define		POWERMODE3(x)				((x) << 24)
#define		POWERMODE3_MASK				(0xff << 24)

#define	MC_ARB_DRAM_TIMING_1				0x27f0
#define	MC_ARB_DRAM_TIMING_2				0x27f4
#define	MC_ARB_DRAM_TIMING_3				0x27f8
#define	MC_ARB_DRAM_TIMING2_1				0x27fc
#define	MC_ARB_DRAM_TIMING2_2				0x2800
#define	MC_ARB_DRAM_TIMING2_3				0x2804

#define	MC4_IO_DQ_PAD_CNTL_D0_I0			0x2978
#define	MC4_IO_DQ_PAD_CNTL_D0_I1			0x297c
#define	MC4_IO_QS_PAD_CNTL_D0_I0			0x2980
#define	MC4_IO_QS_PAD_CNTL_D0_I1			0x2984

#endif
