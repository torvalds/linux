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
#ifndef _SUMOD_H_
#define _SUMOD_H_

/* pm registers */

/* rcu */
#define RCU_FW_VERSION                                  0x30c

#define RCU_PWR_GATING_SEQ0                             0x408
#define RCU_PWR_GATING_SEQ1                             0x40c
#define RCU_PWR_GATING_CNTL                             0x410
#       define PWR_GATING_EN                            (1 << 0)
#       define RSVD_MASK                                (0x3 << 1)
#       define PCV(x)                                   ((x) << 3)
#       define PCV_MASK                                 (0x1f << 3)
#       define PCV_SHIFT                                3
#       define PCP(x)                                   ((x) << 8)
#       define PCP_MASK                                 (0xf << 8)
#       define PCP_SHIFT                                8
#       define RPW(x)                                   ((x) << 16)
#       define RPW_MASK                                 (0xf << 16)
#       define RPW_SHIFT                                16
#       define ID(x)                                    ((x) << 24)
#       define ID_MASK                                  (0xf << 24)
#       define ID_SHIFT                                 24
#       define PGS(x)                                   ((x) << 28)
#       define PGS_MASK                                 (0xf << 28)
#       define PGS_SHIFT                                28

#define RCU_ALTVDDNB_NOTIFY                             0x430
#define RCU_LCLK_SCALING_CNTL                           0x434
#       define LCLK_SCALING_EN                          (1 << 0)
#       define LCLK_SCALING_TYPE                        (1 << 1)
#       define LCLK_SCALING_TIMER_PRESCALER(x)          ((x) << 4)
#       define LCLK_SCALING_TIMER_PRESCALER_MASK        (0xf << 4)
#       define LCLK_SCALING_TIMER_PRESCALER_SHIFT       4
#       define LCLK_SCALING_TIMER_PERIOD(x)             ((x) << 16)
#       define LCLK_SCALING_TIMER_PERIOD_MASK           (0xf << 16)
#       define LCLK_SCALING_TIMER_PERIOD_SHIFT          16

#define RCU_PWR_GATING_CNTL_2                           0x4a0
#       define MPPU(x)                                  ((x) << 0)
#       define MPPU_MASK                                (0xffff << 0)
#       define MPPU_SHIFT                               0
#       define MPPD(x)                                  ((x) << 16)
#       define MPPD_MASK                                (0xffff << 16)
#       define MPPD_SHIFT                               16
#define RCU_PWR_GATING_CNTL_3                           0x4a4
#       define DPPU(x)                                  ((x) << 0)
#       define DPPU_MASK                                (0xffff << 0)
#       define DPPU_SHIFT                               0
#       define DPPD(x)                                  ((x) << 16)
#       define DPPD_MASK                                (0xffff << 16)
#       define DPPD_SHIFT                               16
#define RCU_PWR_GATING_CNTL_4                           0x4a8
#       define RT(x)                                    ((x) << 0)
#       define RT_MASK                                  (0xffff << 0)
#       define RT_SHIFT                                 0
#       define IT(x)                                    ((x) << 16)
#       define IT_MASK                                  (0xffff << 16)
#       define IT_SHIFT                                 16

/* yes these two have the same address */
#define RCU_PWR_GATING_CNTL_5                           0x504
#define RCU_GPU_BOOST_DISABLE                           0x508

#define MCU_M3ARB_INDEX                                 0x504
#define MCU_M3ARB_PARAMS                                0x508

#define RCU_GNB_PWR_REP_TIMER_CNTL                      0x50C

#define RCU_SclkDpmTdpLimit01                           0x514
#define RCU_SclkDpmTdpLimit23                           0x518
#define RCU_SclkDpmTdpLimit47                           0x51C
#define RCU_SclkDpmTdpLimitPG                           0x520

#define GNB_TDP_LIMIT                                   0x540
#define RCU_BOOST_MARGIN                                0x544
#define RCU_THROTTLE_MARGIN                             0x548

#define SMU_PCIE_PG_ARGS                                0x58C
#define SMU_PCIE_PG_ARGS_2                              0x598
#define SMU_PCIE_PG_ARGS_3                              0x59C

/* mmio */
#define RCU_STATUS                                      0x11c
#       define GMC_PWR_GATER_BUSY                       (1 << 8)
#       define GFX_PWR_GATER_BUSY                       (1 << 9)
#       define UVD_PWR_GATER_BUSY                       (1 << 10)
#       define PCIE_PWR_GATER_BUSY                      (1 << 11)
#       define GMC_PWR_GATER_STATE                      (1 << 12)
#       define GFX_PWR_GATER_STATE                      (1 << 13)
#       define UVD_PWR_GATER_STATE                      (1 << 14)
#       define PCIE_PWR_GATER_STATE                     (1 << 15)
#       define GFX1_PWR_GATER_BUSY                      (1 << 16)
#       define GFX2_PWR_GATER_BUSY                      (1 << 17)
#       define GFX1_PWR_GATER_STATE                     (1 << 18)
#       define GFX2_PWR_GATER_STATE                     (1 << 19)

#define GFX_INT_REQ                                     0x120
#       define INT_REQ                                  (1 << 0)
#       define SERV_INDEX(x)                            ((x) << 1)
#       define SERV_INDEX_MASK                          (0xff << 1)
#       define SERV_INDEX_SHIFT                         1
#define GFX_INT_STATUS                                  0x124
#       define INT_ACK                                  (1 << 0)
#       define INT_DONE                                 (1 << 1)

#define CG_SCLK_CNTL                                    0x600
#       define SCLK_DIVIDER(x)                          ((x) << 0)
#       define SCLK_DIVIDER_MASK                        (0x7f << 0)
#       define SCLK_DIVIDER_SHIFT                       0
#define CG_SCLK_STATUS                                  0x604
#       define SCLK_OVERCLK_DETECT                      (1 << 2)

#define CG_DCLK_CNTL                                    0x610
#       define DCLK_DIVIDER_MASK                        0x7f
#       define DCLK_DIR_CNTL_EN                         (1 << 8)
#define CG_DCLK_STATUS                                  0x614
#       define DCLK_STATUS                              (1 << 0)
#define CG_VCLK_CNTL                                    0x618
#       define VCLK_DIVIDER_MASK                        0x7f
#       define VCLK_DIR_CNTL_EN                         (1 << 8)
#define CG_VCLK_STATUS                                  0x61c

#define GENERAL_PWRMGT                                  0x63c
#       define STATIC_PM_EN                             (1 << 1)

#define SCLK_PWRMGT_CNTL                                0x644
#       define SCLK_PWRMGT_OFF                          (1 << 0)
#       define SCLK_LOW_D1                              (1 << 1)
#       define FIR_RESET                                (1 << 4)
#       define FIR_FORCE_TREND_SEL                      (1 << 5)
#       define FIR_TREND_MODE                           (1 << 6)
#       define DYN_GFX_CLK_OFF_EN                       (1 << 7)
#       define GFX_CLK_FORCE_ON                         (1 << 8)
#       define GFX_CLK_REQUEST_OFF                      (1 << 9)
#       define GFX_CLK_FORCE_OFF                        (1 << 10)
#       define GFX_CLK_OFF_ACPI_D1                      (1 << 11)
#       define GFX_CLK_OFF_ACPI_D2                      (1 << 12)
#       define GFX_CLK_OFF_ACPI_D3                      (1 << 13)
#       define GFX_VOLTAGE_CHANGE_EN                    (1 << 16)
#       define GFX_VOLTAGE_CHANGE_MODE                  (1 << 17)

#define TARGET_AND_CURRENT_PROFILE_INDEX                0x66c
#       define TARG_SCLK_INDEX(x)                       ((x) << 6)
#       define TARG_SCLK_INDEX_MASK                     (0x7 << 6)
#       define TARG_SCLK_INDEX_SHIFT                    6
#       define CURR_SCLK_INDEX(x)                       ((x) << 9)
#       define CURR_SCLK_INDEX_MASK                     (0x7 << 9)
#       define CURR_SCLK_INDEX_SHIFT                    9
#       define TARG_INDEX(x)                            ((x) << 12)
#       define TARG_INDEX_MASK                          (0x7 << 12)
#       define TARG_INDEX_SHIFT                         12
#       define CURR_INDEX(x)                            ((x) << 15)
#       define CURR_INDEX_MASK                          (0x7 << 15)
#       define CURR_INDEX_SHIFT                         15

#define CG_SCLK_DPM_CTRL                                0x684
#       define SCLK_FSTATE_0_DIV(x)                     ((x) << 0)
#       define SCLK_FSTATE_0_DIV_MASK                   (0x7f << 0)
#       define SCLK_FSTATE_0_DIV_SHIFT                  0
#       define SCLK_FSTATE_0_VLD                        (1 << 7)
#       define SCLK_FSTATE_1_DIV(x)                     ((x) << 8)
#       define SCLK_FSTATE_1_DIV_MASK                   (0x7f << 8)
#       define SCLK_FSTATE_1_DIV_SHIFT                  8
#       define SCLK_FSTATE_1_VLD                        (1 << 15)
#       define SCLK_FSTATE_2_DIV(x)                     ((x) << 16)
#       define SCLK_FSTATE_2_DIV_MASK                   (0x7f << 16)
#       define SCLK_FSTATE_2_DIV_SHIFT                  16
#       define SCLK_FSTATE_2_VLD                        (1 << 23)
#       define SCLK_FSTATE_3_DIV(x)                     ((x) << 24)
#       define SCLK_FSTATE_3_DIV_MASK                   (0x7f << 24)
#       define SCLK_FSTATE_3_DIV_SHIFT                  24
#       define SCLK_FSTATE_3_VLD                        (1 << 31)
#define CG_SCLK_DPM_CTRL_2                              0x688
#define CG_GCOOR                                        0x68c
#       define PHC(x)                                   ((x) << 0)
#       define PHC_MASK                                 (0x1f << 0)
#       define PHC_SHIFT                                0
#       define SDC(x)                                   ((x) << 9)
#       define SDC_MASK                                 (0x3ff << 9)
#       define SDC_SHIFT                                9
#       define SU(x)                                    ((x) << 23)
#       define SU_MASK                                  (0xf << 23)
#       define SU_SHIFT                                 23
#       define DIV_ID(x)                                ((x) << 28)
#       define DIV_ID_MASK                              (0x7 << 28)
#       define DIV_ID_SHIFT                             28

#define CG_FTV                                          0x690
#define CG_FFCT_0                                       0x694
#       define UTC_0(x)                                 ((x) << 0)
#       define UTC_0_MASK                               (0x3ff << 0)
#       define UTC_0_SHIFT                              0
#       define DTC_0(x)                                 ((x) << 10)
#       define DTC_0_MASK                               (0x3ff << 10)
#       define DTC_0_SHIFT                              10

#define CG_GIT                                          0x6d8
#       define CG_GICST(x)                              ((x) << 0)
#       define CG_GICST_MASK                            (0xffff << 0)
#       define CG_GICST_SHIFT                           0
#       define CG_GIPOT(x)                              ((x) << 16)
#       define CG_GIPOT_MASK                            (0xffff << 16)
#       define CG_GIPOT_SHIFT                           16

#define CG_SCLK_DPM_CTRL_3                              0x6e0
#       define FORCE_SCLK_STATE(x)                      ((x) << 0)
#       define FORCE_SCLK_STATE_MASK                    (0x7 << 0)
#       define FORCE_SCLK_STATE_SHIFT                   0
#       define FORCE_SCLK_STATE_EN                      (1 << 3)
#       define GNB_TT(x)                                ((x) << 8)
#       define GNB_TT_MASK                              (0xff << 8)
#       define GNB_TT_SHIFT                             8
#       define GNB_THERMTHRO_MASK                       (1 << 16)
#       define CNB_THERMTHRO_MASK_SCLK                  (1 << 17)
#       define DPM_SCLK_ENABLE                          (1 << 18)
#       define GNB_SLOW_FSTATE_0_MASK                   (1 << 23)
#       define GNB_SLOW_FSTATE_0_SHIFT                  23
#       define FORCE_NB_PSTATE_1                        (1 << 31)

#define CG_SSP                                          0x6e8
#       define SST(x)                                   ((x) << 0)
#       define SST_MASK                                 (0xffff << 0)
#       define SST_SHIFT                                0
#       define SSTU(x)                                  ((x) << 16)
#       define SSTU_MASK                                (0xffff << 16)
#       define SSTU_SHIFT                               16

#define CG_ACPI_CNTL                                    0x70c
#       define SCLK_ACPI_DIV(x)                         ((x) << 0)
#       define SCLK_ACPI_DIV_MASK                       (0x7f << 0)
#       define SCLK_ACPI_DIV_SHIFT                      0

#define CG_SCLK_DPM_CTRL_4                              0x71c
#       define DC_HDC(x)                                ((x) << 14)
#       define DC_HDC_MASK                              (0x3fff << 14)
#       define DC_HDC_SHIFT                             14
#       define DC_HU(x)                                 ((x) << 28)
#       define DC_HU_MASK                               (0xf << 28)
#       define DC_HU_SHIFT                              28
#define CG_SCLK_DPM_CTRL_5                              0x720
#       define SCLK_FSTATE_BOOTUP(x)                    ((x) << 0)
#       define SCLK_FSTATE_BOOTUP_MASK                  (0x7 << 0)
#       define SCLK_FSTATE_BOOTUP_SHIFT                 0
#       define TT_TP(x)                                 ((x) << 3)
#       define TT_TP_MASK                               (0xffff << 3)
#       define TT_TP_SHIFT                              3
#       define TT_TU(x)                                 ((x) << 19)
#       define TT_TU_MASK                               (0xff << 19)
#       define TT_TU_SHIFT                              19
#define CG_SCLK_DPM_CTRL_6                              0x724
#define CG_AT_0                                         0x728
#       define CG_R(x)                                  ((x) << 0)
#       define CG_R_MASK                                (0xffff << 0)
#       define CG_R_SHIFT                               0
#       define CG_L(x)                                  ((x) << 16)
#       define CG_L_MASK                                (0xffff << 16)
#       define CG_L_SHIFT                               16
#define CG_AT_1                                         0x72c
#define CG_AT_2                                         0x730
#define	CG_THERMAL_INT					0x734
#define		DIG_THERM_INTH(x)			((x) << 8)
#define		DIG_THERM_INTH_MASK			0x0000FF00
#define		DIG_THERM_INTH_SHIFT			8
#define		DIG_THERM_INTL(x)			((x) << 16)
#define		DIG_THERM_INTL_MASK			0x00FF0000
#define		DIG_THERM_INTL_SHIFT			16
#define 	THERM_INT_MASK_HIGH			(1 << 24)
#define 	THERM_INT_MASK_LOW			(1 << 25)
#define CG_AT_3                                         0x738
#define CG_AT_4                                         0x73c
#define CG_AT_5                                         0x740
#define CG_AT_6                                         0x744
#define CG_AT_7                                         0x748

#define CG_BSP_0                                        0x750
#       define BSP(x)                                   ((x) << 0)
#       define BSP_MASK                                 (0xffff << 0)
#       define BSP_SHIFT                                0
#       define BSU(x)                                   ((x) << 16)
#       define BSU_MASK                                 (0xf << 16)
#       define BSU_SHIFT                                16

#define CG_CG_VOLTAGE_CNTL                              0x770
#       define REQ                                      (1 << 0)
#       define LEVEL(x)                                 ((x) << 1)
#       define LEVEL_MASK                               (0x3 << 1)
#       define LEVEL_SHIFT                              1
#       define CG_VOLTAGE_EN                            (1 << 3)
#       define FORCE                                    (1 << 4)
#       define PERIOD(x)                                ((x) << 8)
#       define PERIOD_MASK                              (0xffff << 8)
#       define PERIOD_SHIFT                             8
#       define UNIT(x)                                  ((x) << 24)
#       define UNIT_MASK                                (0xf << 24)
#       define UNIT_SHIFT                               24

#define CG_ACPI_VOLTAGE_CNTL                            0x780
#       define ACPI_VOLTAGE_EN                          (1 << 8)

#define CG_DPM_VOLTAGE_CNTL                             0x788
#       define DPM_STATE0_LEVEL_MASK                    (0x3 << 0)
#       define DPM_STATE0_LEVEL_SHIFT                   0
#       define DPM_VOLTAGE_EN                           (1 << 16)

#define CG_PWR_GATING_CNTL                              0x7ac
#       define DYN_PWR_DOWN_EN                          (1 << 0)
#       define ACPI_PWR_DOWN_EN                         (1 << 1)
#       define GFX_CLK_OFF_PWR_DOWN_EN                  (1 << 2)
#       define IOC_DISGPU_PWR_DOWN_EN                   (1 << 3)
#       define FORCE_POWR_ON                            (1 << 4)
#       define PGP(x)                                   ((x) << 8)
#       define PGP_MASK                                 (0xffff << 8)
#       define PGP_SHIFT                                8
#       define PGU(x)                                   ((x) << 24)
#       define PGU_MASK                                 (0xf << 24)
#       define PGU_SHIFT                                24

#define CG_CGTT_LOCAL_0                                 0x7d0
#define CG_CGTT_LOCAL_1                                 0x7d4

#define DEEP_SLEEP_CNTL                                 0x818
#       define R_DIS                                    (1 << 3)
#       define HS(x)                                    ((x) << 4)
#       define HS_MASK                                  (0xfff << 4)
#       define HS_SHIFT                                 4
#       define ENABLE_DS                                (1 << 31)
#define DEEP_SLEEP_CNTL2                                0x81c
#       define LB_UFP_EN                                (1 << 0)
#       define INOUT_C(x)                               ((x) << 4)
#       define INOUT_C_MASK                             (0xff << 4)
#       define INOUT_C_SHIFT                            4

#define CG_SCRATCH2                                     0x824

#define CG_SCLK_DPM_CTRL_11                             0x830

#define HW_REV   					0x5564
#       define ATI_REV_ID_MASK                          (0xf << 28)
#       define ATI_REV_ID_SHIFT                         28
/* 0 = A0, 1 = A1, 2 = B0, 3 = C0, etc. */

#define DOUT_SCRATCH3   				0x611c

#define GB_ADDR_CONFIG  				0x98f8

#endif
