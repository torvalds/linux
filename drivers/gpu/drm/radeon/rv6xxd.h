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
#ifndef RV6XXD_H
#define RV6XXD_H

/* RV6xx power management */
#define SPLL_CNTL_MODE                                    0x60c
#       define SPLL_DIV_SYNC                              (1 << 5)

#define GENERAL_PWRMGT                                    0x618
#       define GLOBAL_PWRMGT_EN                           (1 << 0)
#       define STATIC_PM_EN                               (1 << 1)
#       define MOBILE_SU                                  (1 << 2)
#       define THERMAL_PROTECTION_DIS                     (1 << 3)
#       define THERMAL_PROTECTION_TYPE                    (1 << 4)
#       define ENABLE_GEN2PCIE                            (1 << 5)
#       define SW_GPIO_INDEX(x)                           ((x) << 6)
#       define SW_GPIO_INDEX_MASK                         (3 << 6)
#       define LOW_VOLT_D2_ACPI                           (1 << 8)
#       define LOW_VOLT_D3_ACPI                           (1 << 9)
#       define VOLT_PWRMGT_EN                             (1 << 10)
#       define BACKBIAS_PAD_EN                            (1 << 16)
#       define BACKBIAS_VALUE                             (1 << 17)
#       define BACKBIAS_DPM_CNTL                          (1 << 18)
#       define DYN_SPREAD_SPECTRUM_EN                     (1 << 21)

#define MCLK_PWRMGT_CNTL                                  0x624
#       define MPLL_PWRMGT_OFF                            (1 << 0)
#       define YCLK_TURNOFF                               (1 << 1)
#       define MPLL_TURNOFF                               (1 << 2)
#       define SU_MCLK_USE_BCLK                           (1 << 3)
#       define DLL_READY                                  (1 << 4)
#       define MC_BUSY                                    (1 << 5)
#       define MC_INT_CNTL                                (1 << 7)
#       define MRDCKA_SLEEP                               (1 << 8)
#       define MRDCKB_SLEEP                               (1 << 9)
#       define MRDCKC_SLEEP                               (1 << 10)
#       define MRDCKD_SLEEP                               (1 << 11)
#       define MRDCKE_SLEEP                               (1 << 12)
#       define MRDCKF_SLEEP                               (1 << 13)
#       define MRDCKG_SLEEP                               (1 << 14)
#       define MRDCKH_SLEEP                               (1 << 15)
#       define MRDCKA_RESET                               (1 << 16)
#       define MRDCKB_RESET                               (1 << 17)
#       define MRDCKC_RESET                               (1 << 18)
#       define MRDCKD_RESET                               (1 << 19)
#       define MRDCKE_RESET                               (1 << 20)
#       define MRDCKF_RESET                               (1 << 21)
#       define MRDCKG_RESET                               (1 << 22)
#       define MRDCKH_RESET                               (1 << 23)
#       define DLL_READY_READ                             (1 << 24)
#       define USE_DISPLAY_GAP                            (1 << 25)
#       define USE_DISPLAY_URGENT_NORMAL                  (1 << 26)
#       define USE_DISPLAY_GAP_CTXSW                      (1 << 27)
#       define MPLL_TURNOFF_D2                            (1 << 28)
#       define USE_DISPLAY_URGENT_CTXSW                   (1 << 29)

#define MPLL_FREQ_LEVEL_0                                 0x6e8
#       define LEVEL0_MPLL_POST_DIV(x)                    ((x) << 0)
#       define LEVEL0_MPLL_POST_DIV_MASK                  (0xff << 0)
#       define LEVEL0_MPLL_FB_DIV(x)                      ((x) << 8)
#       define LEVEL0_MPLL_FB_DIV_MASK                    (0xfff << 8)
#       define LEVEL0_MPLL_REF_DIV(x)                     ((x) << 20)
#       define LEVEL0_MPLL_REF_DIV_MASK                   (0x3f << 20)
#       define LEVEL0_MPLL_DIV_EN                         (1 << 28)
#       define LEVEL0_DLL_BYPASS                          (1 << 29)
#       define LEVEL0_DLL_RESET                           (1 << 30)

#define VID_RT                                            0x6f8
#       define VID_CRT(x)                                 ((x) << 0)
#       define VID_CRT_MASK                               (0x1fff << 0)
#       define VID_CRTU(x)                                ((x) << 13)
#       define VID_CRTU_MASK                              (7 << 13)
#       define SSTU(x)                                    ((x) << 16)
#       define SSTU_MASK                                  (7 << 16)
#       define VID_SWT(x)                                 ((x) << 19)
#       define VID_SWT_MASK                               (0x1f << 19)
#       define BRT(x)                                     ((x) << 24)
#       define BRT_MASK                                   (0xff << 24)

#define TARGET_AND_CURRENT_PROFILE_INDEX                  0x70c
#       define TARGET_PROFILE_INDEX_MASK                  (3 << 0)
#       define TARGET_PROFILE_INDEX_SHIFT                 0
#       define CURRENT_PROFILE_INDEX_MASK                 (3 << 2)
#       define CURRENT_PROFILE_INDEX_SHIFT                2
#       define DYN_PWR_ENTER_INDEX(x)                     ((x) << 4)
#       define DYN_PWR_ENTER_INDEX_MASK                   (3 << 4)
#       define DYN_PWR_ENTER_INDEX_SHIFT                  4
#       define CURR_MCLK_INDEX_MASK                       (3 << 6)
#       define CURR_MCLK_INDEX_SHIFT                      6
#       define CURR_SCLK_INDEX_MASK                       (0x1f << 8)
#       define CURR_SCLK_INDEX_SHIFT                      8
#       define CURR_VID_INDEX_MASK                        (3 << 13)
#       define CURR_VID_INDEX_SHIFT                       13

#define VID_UPPER_GPIO_CNTL                               0x740
#       define CTXSW_UPPER_GPIO_VALUES(x)                 ((x) << 0)
#       define CTXSW_UPPER_GPIO_VALUES_MASK               (7 << 0)
#       define HIGH_UPPER_GPIO_VALUES(x)                  ((x) << 3)
#       define HIGH_UPPER_GPIO_VALUES_MASK                (7 << 3)
#       define MEDIUM_UPPER_GPIO_VALUES(x)                ((x) << 6)
#       define MEDIUM_UPPER_GPIO_VALUES_MASK              (7 << 6)
#       define LOW_UPPER_GPIO_VALUES(x)                   ((x) << 9)
#       define LOW_UPPER_GPIO_VALUES_MASK                 (7 << 9)
#       define CTXSW_BACKBIAS_VALUE                       (1 << 12)
#       define HIGH_BACKBIAS_VALUE                        (1 << 13)
#       define MEDIUM_BACKBIAS_VALUE                      (1 << 14)
#       define LOW_BACKBIAS_VALUE                         (1 << 15)

#define CG_DISPLAY_GAP_CNTL                               0x7dc
#       define DISP1_GAP(x)                               ((x) << 0)
#       define DISP1_GAP_MASK                             (3 << 0)
#       define DISP2_GAP(x)                               ((x) << 2)
#       define DISP2_GAP_MASK                             (3 << 2)
#       define VBI_TIMER_COUNT(x)                         ((x) << 4)
#       define VBI_TIMER_COUNT_MASK                       (0x3fff << 4)
#       define VBI_TIMER_UNIT(x)                          ((x) << 20)
#       define VBI_TIMER_UNIT_MASK                        (7 << 20)
#       define DISP1_GAP_MCHG(x)                          ((x) << 24)
#       define DISP1_GAP_MCHG_MASK                        (3 << 24)
#       define DISP2_GAP_MCHG(x)                          ((x) << 26)
#       define DISP2_GAP_MCHG_MASK                        (3 << 26)

#define CG_THERMAL_CTRL                                   0x7f0
#       define DPM_EVENT_SRC(x)                           ((x) << 0)
#       define DPM_EVENT_SRC_MASK                         (7 << 0)
#       define THERM_INC_CLK                              (1 << 3)
#       define TOFFSET(x)                                 ((x) << 4)
#       define TOFFSET_MASK                               (0xff << 4)
#       define DIG_THERM_DPM(x)                           ((x) << 12)
#       define DIG_THERM_DPM_MASK                         (0xff << 12)
#       define CTF_SEL(x)                                 ((x) << 20)
#       define CTF_SEL_MASK                               (7 << 20)
#       define CTF_PAD_POLARITY                           (1 << 23)
#       define CTF_PAD_EN                                 (1 << 24)

#define CG_SPLL_SPREAD_SPECTRUM_LOW                       0x820
#       define SSEN                                       (1 << 0)
#       define CLKS(x)                                    ((x) << 3)
#       define CLKS_MASK                                  (0xff << 3)
#       define CLKS_SHIFT                                 3
#       define CLKV(x)                                    ((x) << 11)
#       define CLKV_MASK                                  (0x7ff << 11)
#       define CLKV_SHIFT                                 11
#define CG_MPLL_SPREAD_SPECTRUM                           0x830

#define CITF_CNTL					0x200c
#       define BLACKOUT_RD                              (1 << 0)
#       define BLACKOUT_WR                              (1 << 1)

#define RAMCFG						0x2408
#define		NOOFBANK_SHIFT					0
#define		NOOFBANK_MASK					0x00000001
#define		NOOFRANK_SHIFT					1
#define		NOOFRANK_MASK					0x00000002
#define		NOOFROWS_SHIFT					2
#define		NOOFROWS_MASK					0x0000001C
#define		NOOFCOLS_SHIFT					5
#define		NOOFCOLS_MASK					0x00000060
#define		CHANSIZE_SHIFT					7
#define		CHANSIZE_MASK					0x00000080
#define		BURSTLENGTH_SHIFT				8
#define		BURSTLENGTH_MASK				0x00000100
#define		CHANSIZE_OVERRIDE				(1 << 10)

#define SQM_RATIO					0x2424
#       define STATE0(x)                                ((x) << 0)
#       define STATE0_MASK                              (0xff << 0)
#       define STATE1(x)                                ((x) << 8)
#       define STATE1_MASK                              (0xff << 8)
#       define STATE2(x)                                ((x) << 16)
#       define STATE2_MASK                              (0xff << 16)
#       define STATE3(x)                                ((x) << 24)
#       define STATE3_MASK                              (0xff << 24)

#define ARB_RFSH_CNTL					0x2460
#       define ENABLE                                   (1 << 0)
#define ARB_RFSH_RATE					0x2464
#       define POWERMODE0(x)                            ((x) << 0)
#       define POWERMODE0_MASK                          (0xff << 0)
#       define POWERMODE1(x)                            ((x) << 8)
#       define POWERMODE1_MASK                          (0xff << 8)
#       define POWERMODE2(x)                            ((x) << 16)
#       define POWERMODE2_MASK                          (0xff << 16)
#       define POWERMODE3(x)                            ((x) << 24)
#       define POWERMODE3_MASK                          (0xff << 24)

#define MC_SEQ_DRAM					0x2608
#       define CKE_DYN                                  (1 << 12)

#define MC_SEQ_CMD					0x26c4

#define MC_SEQ_RESERVE_S				0x2890
#define MC_SEQ_RESERVE_M				0x2894

#define LVTMA_DATA_SYNCHRONIZATION                      0x7adc
#       define LVTMA_PFREQCHG                           (1 << 8)
#define DCE3_LVTMA_DATA_SYNCHRONIZATION                 0x7f98

/* PCIE indirect regs */
#define PCIE_P_CNTL                                       0x40
#       define P_PLL_PWRDN_IN_L1L23                       (1 << 3)
#       define P_PLL_BUF_PDNB                             (1 << 4)
#       define P_PLL_PDNB                                 (1 << 9)
#       define P_ALLOW_PRX_FRONTEND_SHUTOFF               (1 << 12)
/* PCIE PORT indirect regs */
#define PCIE_LC_CNTL                                      0xa0
#       define LC_L0S_INACTIVITY(x)                       ((x) << 8)
#       define LC_L0S_INACTIVITY_MASK                     (0xf << 8)
#       define LC_L0S_INACTIVITY_SHIFT                    8
#       define LC_L1_INACTIVITY(x)                        ((x) << 12)
#       define LC_L1_INACTIVITY_MASK                      (0xf << 12)
#       define LC_L1_INACTIVITY_SHIFT                     12
#       define LC_PMI_TO_L1_DIS                           (1 << 16)
#       define LC_ASPM_TO_L1_DIS                          (1 << 24)
#define PCIE_LC_SPEED_CNTL                                0xa4
#       define LC_GEN2_EN                                 (1 << 0)
#       define LC_INITIATE_LINK_SPEED_CHANGE              (1 << 7)
#       define LC_CURRENT_DATA_RATE                       (1 << 11)
#       define LC_HW_VOLTAGE_IF_CONTROL(x)                ((x) << 12)
#       define LC_HW_VOLTAGE_IF_CONTROL_MASK              (3 << 12)
#       define LC_HW_VOLTAGE_IF_CONTROL_SHIFT             12
#       define LC_OTHER_SIDE_EVER_SENT_GEN2               (1 << 23)
#       define LC_OTHER_SIDE_SUPPORTS_GEN2                (1 << 24)

#endif
