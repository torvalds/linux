/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */
#ifndef _TRINITYD_H_
#define _TRINITYD_H_

/* pm registers */

/* cg */
#define CG_CGTT_LOCAL_0                                 0x0
#define CG_CGTT_LOCAL_1                                 0x1

/* smc */
#define SMU_SCLK_DPM_STATE_0_CNTL_0                     0x1f000
#       define STATE_VALID(x)                           ((x) << 0)
#       define STATE_VALID_MASK                         (0xff << 0)
#       define STATE_VALID_SHIFT                        0
#       define CLK_DIVIDER(x)                           ((x) << 8)
#       define CLK_DIVIDER_MASK                         (0xff << 8)
#       define CLK_DIVIDER_SHIFT                        8
#       define VID(x)                                   ((x) << 16)
#       define VID_MASK                                 (0xff << 16)
#       define VID_SHIFT                                16
#       define LVRT(x)                                  ((x) << 24)
#       define LVRT_MASK                                (0xff << 24)
#       define LVRT_SHIFT                               24
#define SMU_SCLK_DPM_STATE_0_CNTL_1                     0x1f004
#       define DS_DIV(x)                                ((x) << 0)
#       define DS_DIV_MASK                              (0xff << 0)
#       define DS_DIV_SHIFT                             0
#       define DS_SH_DIV(x)                             ((x) << 8)
#       define DS_SH_DIV_MASK                           (0xff << 8)
#       define DS_SH_DIV_SHIFT                          8
#       define DISPLAY_WM(x)                            ((x) << 16)
#       define DISPLAY_WM_MASK                          (0xff << 16)
#       define DISPLAY_WM_SHIFT                         16
#       define VCE_WM(x)                                ((x) << 24)
#       define VCE_WM_MASK                              (0xff << 24)
#       define VCE_WM_SHIFT                             24

#define SMU_SCLK_DPM_STATE_0_CNTL_3                     0x1f00c
#       define GNB_SLOW(x)                              ((x) << 0)
#       define GNB_SLOW_MASK                            (0xff << 0)
#       define GNB_SLOW_SHIFT                           0
#       define FORCE_NBPS1(x)                           ((x) << 8)
#       define FORCE_NBPS1_MASK                         (0xff << 8)
#       define FORCE_NBPS1_SHIFT                        8
#define SMU_SCLK_DPM_STATE_0_AT                         0x1f010
#       define AT(x)                                    ((x) << 0)
#       define AT_MASK                                  (0xff << 0)
#       define AT_SHIFT                                 0

#define SMU_SCLK_DPM_STATE_0_PG_CNTL                    0x1f014
#       define PD_SCLK_DIVIDER(x)                       ((x) << 16)
#       define PD_SCLK_DIVIDER_MASK                     (0xff << 16)
#       define PD_SCLK_DIVIDER_SHIFT                    16

#define SMU_SCLK_DPM_STATE_1_CNTL_0                     0x1f020

#define SMU_SCLK_DPM_CNTL                               0x1f100
#       define SCLK_DPM_EN(x)                           ((x) << 0)
#       define SCLK_DPM_EN_MASK                         (0xff << 0)
#       define SCLK_DPM_EN_SHIFT                        0
#       define SCLK_DPM_BOOT_STATE(x)                   ((x) << 16)
#       define SCLK_DPM_BOOT_STATE_MASK                 (0xff << 16)
#       define SCLK_DPM_BOOT_STATE_SHIFT                16
#       define VOLTAGE_CHG_EN(x)                        ((x) << 24)
#       define VOLTAGE_CHG_EN_MASK                      (0xff << 24)
#       define VOLTAGE_CHG_EN_SHIFT                     24

#define SMU_SCLK_DPM_TT_CNTL                            0x1f108
#       define SCLK_TT_EN(x)                            ((x) << 0)
#       define SCLK_TT_EN_MASK                          (0xff << 0)
#       define SCLK_TT_EN_SHIFT                         0
#define SMU_SCLK_DPM_TTT                                0x1f10c
#       define LT(x)                                    ((x) << 0)
#       define LT_MASK                                  (0xffff << 0)
#       define LT_SHIFT                                 0
#       define HT(x)                                    ((x) << 16)
#       define HT_MASK                                  (0xffff << 16)
#       define HT_SHIFT                                 16

#define SMU_UVD_DPM_STATES                              0x1f1a0
#define SMU_UVD_DPM_CNTL                                0x1f1a4

#define SMU_S_PG_CNTL                                   0x1f118
#       define DS_PG_EN(x)                              ((x) << 16)
#       define DS_PG_EN_MASK                            (0xff << 16)
#       define DS_PG_EN_SHIFT                           16

#define GFX_POWER_GATING_CNTL                           0x1f38c
#       define PDS_DIV(x)                               ((x) << 0)
#       define PDS_DIV_MASK                             (0xff << 0)
#       define PDS_DIV_SHIFT                            0
#       define SSSD(x)                                  ((x) << 8)
#       define SSSD_MASK                                (0xff << 8)
#       define SSSD_SHIFT                               8

#define PM_CONFIG                                       0x1f428
#       define SVI_Mode                                 (1 << 29)

#define PM_I_CNTL_1                                     0x1f464
#       define SCLK_DPM(x)                              ((x) << 0)
#       define SCLK_DPM_MASK                            (0xff << 0)
#       define SCLK_DPM_SHIFT                           0
#       define DS_PG_CNTL(x)                            ((x) << 16)
#       define DS_PG_CNTL_MASK                          (0xff << 16)
#       define DS_PG_CNTL_SHIFT                         16
#define PM_TP                                           0x1f468

#define NB_PSTATE_CONFIG                                0x1f5f8
#       define Dpm0PgNbPsLo(x)                          ((x) << 0)
#       define Dpm0PgNbPsLo_MASK                        (3 << 0)
#       define Dpm0PgNbPsLo_SHIFT                       0
#       define Dpm0PgNbPsHi(x)                          ((x) << 2)
#       define Dpm0PgNbPsHi_MASK                        (3 << 2)
#       define Dpm0PgNbPsHi_SHIFT                       2
#       define DpmXNbPsLo(x)                            ((x) << 4)
#       define DpmXNbPsLo_MASK                          (3 << 4)
#       define DpmXNbPsLo_SHIFT                         4
#       define DpmXNbPsHi(x)                            ((x) << 6)
#       define DpmXNbPsHi_MASK                          (3 << 6)
#       define DpmXNbPsHi_SHIFT                         6

#define DC_CAC_VALUE                                    0x1f908

#define GPU_CAC_AVRG_CNTL                               0x1f920
#       define WINDOW_SIZE(x)                           ((x) << 0)
#       define WINDOW_SIZE_MASK                         (0xff << 0)
#       define WINDOW_SIZE_SHIFT                        0

#define CC_SMU_MISC_FUSES                               0xe0001004
#       define MinSClkDid(x)                   ((x) << 2)
#       define MinSClkDid_MASK                 (0x7f << 2)
#       define MinSClkDid_SHIFT                2

#define CC_SMU_TST_EFUSE1_MISC                          0xe000101c
#       define RB_BACKEND_DISABLE(x)                    ((x) << 16)
#       define RB_BACKEND_DISABLE_MASK                  (3 << 16)
#       define RB_BACKEND_DISABLE_SHIFT                 16

#define SMU_SCRATCH_A                                   0xe0003024

#define SMU_SCRATCH0                                    0xe0003040

/* mmio */
#define SMC_INT_REQ                                     0x220

#define SMC_MESSAGE_0                                   0x22c
#define SMC_RESP_0                                      0x230

#define GENERAL_PWRMGT                                  0x670
#       define GLOBAL_PWRMGT_EN                         (1 << 0)

#define SCLK_PWRMGT_CNTL                                0x678
#       define DYN_PWR_DOWN_EN                          (1 << 2)
#       define RESET_BUSY_CNT                           (1 << 4)
#       define RESET_SCLK_CNT                           (1 << 5)
#       define DYN_GFX_CLK_OFF_EN                       (1 << 7)
#       define GFX_CLK_FORCE_ON                         (1 << 8)
#       define DYNAMIC_PM_EN                            (1 << 21)

#define TARGET_AND_CURRENT_PROFILE_INDEX                0x684
#       define TARGET_STATE(x)                          ((x) << 0)
#       define TARGET_STATE_MASK                        (0xf << 0)
#       define TARGET_STATE_SHIFT                       0
#       define CURRENT_STATE(x)                         ((x) << 4)
#       define CURRENT_STATE_MASK                       (0xf << 4)
#       define CURRENT_STATE_SHIFT                      4

#define CG_GIPOTS                                       0x6d8
#       define CG_GIPOT(x)                              ((x) << 16)
#       define CG_GIPOT_MASK                            (0xffff << 16)
#       define CG_GIPOT_SHIFT                           16

#define CG_PG_CTRL                                      0x6e0
#       define SP(x)                                    ((x) << 0)
#       define SP_MASK                                  (0xffff << 0)
#       define SP_SHIFT                                 0
#       define SU(x)                                    ((x) << 16)
#       define SU_MASK                                  (0xffff << 16)
#       define SU_SHIFT                                 16

#define CG_MISC_REG                                     0x708

#define CG_THERMAL_INT_CTRL                             0x738
#       define DIG_THERM_INTH(x)                        ((x) << 0)
#       define DIG_THERM_INTH_MASK                      (0xff << 0)
#       define DIG_THERM_INTH_SHIFT                     0
#       define DIG_THERM_INTL(x)                        ((x) << 8)
#       define DIG_THERM_INTL_MASK                      (0xff << 8)
#       define DIG_THERM_INTL_SHIFT                     8
#       define THERM_INTH_MASK                          (1 << 24)
#       define THERM_INTL_MASK                          (1 << 25)

#define CG_CG_VOLTAGE_CNTL                              0x770
#       define EN                                       (1 << 9)

#define HW_REV   					0x5564
#       define ATI_REV_ID_MASK                          (0xf << 28)
#       define ATI_REV_ID_SHIFT                         28
/* 0 = A0, 1 = A1, 2 = B0, 3 = C0, etc. */

#define CGTS_SM_CTRL_REG                                0x9150

#define GB_ADDR_CONFIG                                  0x98f8

#endif
