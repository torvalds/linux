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
#ifndef __RS780D_H__
#define __RS780D_H__

#define CG_SPLL_FUNC_CNTL                                 0x600
#       define SPLL_RESET                                (1 << 0)
#       define SPLL_SLEEP                                (1 << 1)
#       define SPLL_REF_DIV(x)                           ((x) << 2)
#       define SPLL_REF_DIV_MASK                         (7 << 2)
#       define SPLL_FB_DIV(x)                            ((x) << 5)
#       define SPLL_FB_DIV_MASK                          (0xff << 2)
#       define SPLL_FB_DIV_SHIFT                         2
#       define SPLL_PULSEEN                              (1 << 13)
#       define SPLL_PULSENUM(x)                          ((x) << 14)
#       define SPLL_PULSENUM_MASK                        (3 << 14)
#       define SPLL_SW_HILEN(x)                          ((x) << 16)
#       define SPLL_SW_HILEN_MASK                        (0xf << 16)
#       define SPLL_SW_LOLEN(x)                          ((x) << 20)
#       define SPLL_SW_LOLEN_MASK                        (0xf << 20)
#       define SPLL_DIVEN                                (1 << 24)
#       define SPLL_BYPASS_EN                            (1 << 25)
#       define SPLL_CHG_STATUS                           (1 << 29)
#       define SPLL_CTLREQ                               (1 << 30)
#       define SPLL_CTLACK                               (1 << 31)

/* RS780/RS880 PM */
#define	FVTHROT_CNTRL_REG				0x3000
#define		DONT_WAIT_FOR_FBDIV_WRAP		(1 << 0)
#define		MINIMUM_CIP(x)				((x) << 1)
#define		MINIMUM_CIP_SHIFT			1
#define		MINIMUM_CIP_MASK			0x1fffffe
#define		REFRESH_RATE_DIVISOR(x)			((x) << 25)
#define		REFRESH_RATE_DIVISOR_SHIFT		25
#define		REFRESH_RATE_DIVISOR_MASK		(0x3 << 25)
#define		ENABLE_FV_THROT				(1 << 27)
#define		ENABLE_FV_UPDATE			(1 << 28)
#define		TREND_SEL_MODE				(1 << 29)
#define		FORCE_TREND_SEL				(1 << 30)
#define		ENABLE_FV_THROT_IO			(1 << 31)
#define	FVTHROT_TARGET_REG				0x3004
#define		TARGET_IDLE_COUNT(x)			((x) << 0)
#define		TARGET_IDLE_COUNT_MASK			0xffffff
#define		TARGET_IDLE_COUNT_SHIFT			0
#define	FVTHROT_CB1					0x3008
#define	FVTHROT_CB2					0x300c
#define	FVTHROT_CB3					0x3010
#define	FVTHROT_CB4					0x3014
#define	FVTHROT_UTC0					0x3018
#define	FVTHROT_UTC1					0x301c
#define	FVTHROT_UTC2					0x3020
#define	FVTHROT_UTC3					0x3024
#define	FVTHROT_UTC4					0x3028
#define	FVTHROT_DTC0					0x302c
#define	FVTHROT_DTC1					0x3030
#define	FVTHROT_DTC2					0x3034
#define	FVTHROT_DTC3					0x3038
#define	FVTHROT_DTC4					0x303c
#define	FVTHROT_FBDIV_REG0				0x3040
#define		MIN_FEEDBACK_DIV(x)			((x) << 0)
#define		MIN_FEEDBACK_DIV_MASK			0xfff
#define		MIN_FEEDBACK_DIV_SHIFT			0
#define		MAX_FEEDBACK_DIV(x)			((x) << 12)
#define		MAX_FEEDBACK_DIV_MASK			(0xfff << 12)
#define		MAX_FEEDBACK_DIV_SHIFT			12
#define	FVTHROT_FBDIV_REG1				0x3044
#define		MAX_FEEDBACK_STEP(x)			((x) << 0)
#define		MAX_FEEDBACK_STEP_MASK			0xfff
#define		MAX_FEEDBACK_STEP_SHIFT			0
#define		STARTING_FEEDBACK_DIV(x)		((x) << 12)
#define		STARTING_FEEDBACK_DIV_MASK		(0xfff << 12)
#define		STARTING_FEEDBACK_DIV_SHIFT		12
#define		FORCE_FEEDBACK_DIV			(1 << 24)
#define	FVTHROT_FBDIV_REG2				0x3048
#define		FORCED_FEEDBACK_DIV(x)			((x) << 0)
#define		FORCED_FEEDBACK_DIV_MASK		0xfff
#define		FORCED_FEEDBACK_DIV_SHIFT		0
#define		FB_DIV_TIMER_VAL(x)			((x) << 12)
#define		FB_DIV_TIMER_VAL_MASK			(0xffff << 12)
#define		FB_DIV_TIMER_VAL_SHIFT			12
#define	FVTHROT_FB_US_REG0				0x304c
#define	FVTHROT_FB_US_REG1				0x3050
#define	FVTHROT_FB_DS_REG0				0x3054
#define	FVTHROT_FB_DS_REG1				0x3058
#define	FVTHROT_PWM_CTRL_REG0				0x305c
#define		STARTING_PWM_HIGHTIME(x)		((x) << 0)
#define		STARTING_PWM_HIGHTIME_MASK		0xfff
#define		STARTING_PWM_HIGHTIME_SHIFT		0
#define		NUMBER_OF_CYCLES_IN_PERIOD(x)		((x) << 12)
#define		NUMBER_OF_CYCLES_IN_PERIOD_MASK		(0xfff << 12)
#define		NUMBER_OF_CYCLES_IN_PERIOD_SHIFT	12
#define		FORCE_STARTING_PWM_HIGHTIME		(1 << 24)
#define		INVERT_PWM_WAVEFORM			(1 << 25)
#define	FVTHROT_PWM_CTRL_REG1				0x3060
#define		MIN_PWM_HIGHTIME(x)			((x) << 0)
#define		MIN_PWM_HIGHTIME_MASK			0xfff
#define		MIN_PWM_HIGHTIME_SHIFT			0
#define		MAX_PWM_HIGHTIME(x)			((x) << 12)
#define		MAX_PWM_HIGHTIME_MASK			(0xfff << 12)
#define		MAX_PWM_HIGHTIME_SHIFT			12
#define	FVTHROT_PWM_US_REG0				0x3064
#define	FVTHROT_PWM_US_REG1				0x3068
#define	FVTHROT_PWM_DS_REG0				0x306c
#define	FVTHROT_PWM_DS_REG1				0x3070
#define	FVTHROT_STATUS_REG0				0x3074
#define		CURRENT_FEEDBACK_DIV_MASK		0xfff
#define		CURRENT_FEEDBACK_DIV_SHIFT		0
#define	FVTHROT_STATUS_REG1				0x3078
#define	FVTHROT_STATUS_REG2				0x307c
#define	CG_INTGFX_MISC					0x3080
#define		FVTHROT_VBLANK_SEL			(1 << 9)
#define	FVTHROT_PWM_FEEDBACK_DIV_REG1			0x308c
#define		RANGE0_PWM_FEEDBACK_DIV(x)		((x) << 0)
#define		RANGE0_PWM_FEEDBACK_DIV_MASK		0xfff
#define		RANGE0_PWM_FEEDBACK_DIV_SHIFT		0
#define		RANGE_PWM_FEEDBACK_DIV_EN		(1 << 12)
#define	FVTHROT_PWM_FEEDBACK_DIV_REG2			0x3090
#define		RANGE1_PWM_FEEDBACK_DIV(x)		((x) << 0)
#define		RANGE1_PWM_FEEDBACK_DIV_MASK		0xfff
#define		RANGE1_PWM_FEEDBACK_DIV_SHIFT		0
#define		RANGE2_PWM_FEEDBACK_DIV(x)		((x) << 12)
#define		RANGE2_PWM_FEEDBACK_DIV_MASK		(0xfff << 12)
#define		RANGE2_PWM_FEEDBACK_DIV_SHIFT		12
#define	FVTHROT_PWM_FEEDBACK_DIV_REG3			0x3094
#define		RANGE0_PWM(x)				((x) << 0)
#define		RANGE0_PWM_MASK				0xfff
#define		RANGE0_PWM_SHIFT			0
#define		RANGE1_PWM(x)				((x) << 12)
#define		RANGE1_PWM_MASK				(0xfff << 12)
#define		RANGE1_PWM_SHIFT			12
#define	FVTHROT_PWM_FEEDBACK_DIV_REG4			0x3098
#define		RANGE2_PWM(x)				((x) << 0)
#define		RANGE2_PWM_MASK				0xfff
#define		RANGE2_PWM_SHIFT			0
#define		RANGE3_PWM(x)				((x) << 12)
#define		RANGE3_PWM_MASK				(0xfff << 12)
#define		RANGE3_PWM_SHIFT			12
#define	FVTHROT_SLOW_CLK_FEEDBACK_DIV_REG1		0x30ac
#define		RANGE0_SLOW_CLK_FEEDBACK_DIV(x)		((x) << 0)
#define		RANGE0_SLOW_CLK_FEEDBACK_DIV_MASK	0xfff
#define		RANGE0_SLOW_CLK_FEEDBACK_DIV_SHIFT	0
#define		RANGE_SLOW_CLK_FEEDBACK_DIV_EN		(1 << 12)

#define	GFX_MACRO_BYPASS_CNTL				0x30c0
#define		SPLL_BYPASS_CNTL			(1 << 0)
#define		UPLL_BYPASS_CNTL			(1 << 1)

#endif
