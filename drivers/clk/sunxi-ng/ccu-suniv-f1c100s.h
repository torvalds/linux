/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2017 Icenowy Zheng <icenowy@aosc.io>
 *
 */

#ifndef _CCU_SUNIV_F1C100S_H_
#define _CCU_SUNIV_F1C100S_H_

#include <dt-bindings/clock/suniv-ccu-f1c100s.h>
#include <dt-bindings/reset/suniv-ccu-f1c100s.h>

#define CLK_PLL_CPU		0
#define CLK_PLL_AUDIO_BASE	1
#define CLK_PLL_AUDIO		2
#define CLK_PLL_AUDIO_2X	3
#define CLK_PLL_AUDIO_4X	4
#define CLK_PLL_AUDIO_8X	5
#define CLK_PLL_VIDEO		6
#define CLK_PLL_VIDEO_2X	7
#define CLK_PLL_VE		8
#define CLK_PLL_DDR0		9
#define CLK_PLL_PERIPH		10

/* CPU clock is exported */

#define CLK_AHB			12
#define CLK_APB			13

/* All bus gates, DRAM gates and mod clocks are exported */

#define CLK_NUMBER		(CLK_AVS + 1)

#endif /* _CCU_SUNIV_F1C100S_H_ */
