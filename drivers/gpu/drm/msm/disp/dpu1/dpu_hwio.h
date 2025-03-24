/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HWIO_H
#define _DPU_HWIO_H

#include "dpu_hw_util.h"

/**
 * MDP TOP block Register and bit fields and defines
 */
#define DISP_INTF_SEL                   0x004
#define INTR_EN                         0x010
#define INTR_STATUS                     0x014
#define INTR_CLEAR                      0x018
#define INTR2_EN                        0x008
#define INTR2_STATUS                    0x00c
#define SSPP_SPARE                      0x028
#define INTR2_CLEAR                     0x02c
#define HIST_INTR_EN                    0x01c
#define HIST_INTR_STATUS                0x020
#define HIST_INTR_CLEAR                 0x024
#define SPLIT_DISPLAY_EN                0x2F4
#define SPLIT_DISPLAY_UPPER_PIPE_CTRL   0x2F8
#define DSPP_IGC_COLOR0_RAM_LUTN        0x300
#define DSPP_IGC_COLOR1_RAM_LUTN        0x304
#define DSPP_IGC_COLOR2_RAM_LUTN        0x308
#define DANGER_STATUS                   0x360
#define SAFE_STATUS                     0x364
#define HW_EVENTS_CTL                   0x37C
#define MDP_WD_TIMER_0_CTL              0x380
#define MDP_WD_TIMER_0_CTL2             0x384
#define MDP_WD_TIMER_0_LOAD_VALUE       0x388
#define MDP_WD_TIMER_1_CTL              0x390
#define MDP_WD_TIMER_1_CTL2             0x394
#define MDP_WD_TIMER_1_LOAD_VALUE       0x398
#define CLK_CTRL3                       0x3A8
#define CLK_STATUS3                     0x3AC
#define CLK_CTRL4                       0x3B0
#define CLK_STATUS4                     0x3B4
#define CLK_CTRL5                       0x3B8
#define CLK_STATUS5                     0x3BC
#define CLK_CTRL7                       0x3D0
#define CLK_STATUS7                     0x3D4
#define SPLIT_DISPLAY_LOWER_PIPE_CTRL   0x3F0
#define SPLIT_DISPLAY_TE_LINE_INTERVAL  0x3F4
#define INTF_SW_RESET_MASK              0x3FC
#define HDMI_DP_CORE_SELECT             0x408
#define MDP_OUT_CTL_0                   0x410
#define MDP_VSYNC_SEL                   0x414
#define MDP_WD_TIMER_2_CTL              0x420
#define MDP_WD_TIMER_2_CTL2             0x424
#define MDP_WD_TIMER_2_LOAD_VALUE       0x428
#define MDP_WD_TIMER_3_CTL              0x430
#define MDP_WD_TIMER_3_CTL2             0x434
#define MDP_WD_TIMER_3_LOAD_VALUE       0x438
#define MDP_WD_TIMER_4_CTL              0x440
#define MDP_WD_TIMER_4_CTL2             0x444
#define MDP_WD_TIMER_4_LOAD_VALUE       0x448
#define DCE_SEL                         0x450

#define MDP_DP_PHY_INTF_SEL             0x460
#define MDP_DP_PHY_INTF_SEL_INTF0		GENMASK(2, 0)
#define MDP_DP_PHY_INTF_SEL_INTF1		GENMASK(5, 3)
#define MDP_DP_PHY_INTF_SEL_PHY0		GENMASK(8, 6)
#define MDP_DP_PHY_INTF_SEL_PHY1		GENMASK(11, 9)
#define MDP_DP_PHY_INTF_SEL_PHY2		GENMASK(14, 12)

#define MDP_PERIPH_TOP0			MDP_WD_TIMER_0_CTL
#define MDP_PERIPH_TOP0_END		CLK_CTRL3

#endif /*_DPU_HWIO_H */
