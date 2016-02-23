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
#ifndef __RS780_DPM_H__
#define __RS780_DPM_H__

enum rs780_vddc_level {
	RS780_VDDC_LEVEL_UNKNOWN = 0,
	RS780_VDDC_LEVEL_LOW = 1,
	RS780_VDDC_LEVEL_HIGH = 2,
};

struct igp_power_info {
	/* flags */
	bool invert_pwm_required;
	bool pwm_voltage_control;
	bool voltage_control;
	bool gfx_clock_gating;
	/* stored values */
	u32 system_config;
	u32 bootup_uma_clk;
	u16 max_voltage;
	u16 min_voltage;
	u16 boot_voltage;
	u16 inter_voltage_low;
	u16 inter_voltage_high;
	u16 num_of_cycles_in_period;
	/* variable */
	int crtc_id;
	int refresh_rate;
};

struct igp_ps {
	enum rs780_vddc_level min_voltage;
	enum rs780_vddc_level max_voltage;
	u32 sclk_low;
	u32 sclk_high;
	u32 flags;
};

#define RS780_CGFTV_DFLT                 0x0303000f
#define RS780_FBDIVTIMERVAL_DFLT         0x2710

#define RS780_FVTHROTUTC0_DFLT   0x04010040
#define RS780_FVTHROTUTC1_DFLT   0x04010040
#define RS780_FVTHROTUTC2_DFLT   0x04010040
#define RS780_FVTHROTUTC3_DFLT   0x04010040
#define RS780_FVTHROTUTC4_DFLT   0x04010040

#define RS780_FVTHROTDTC0_DFLT 0x04010040
#define RS780_FVTHROTDTC1_DFLT 0x04010040
#define RS780_FVTHROTDTC2_DFLT 0x04010040
#define RS780_FVTHROTDTC3_DFLT 0x04010040
#define RS780_FVTHROTDTC4_DFLT 0x04010040

#define RS780_FVTHROTFBUSREG0_DFLT       0x00001001
#define RS780_FVTHROTFBUSREG1_DFLT       0x00002002
#define RS780_FVTHROTFBDSREG0_DFLT       0x00004001
#define RS780_FVTHROTFBDSREG1_DFLT       0x00020010

#define RS780_FVTHROTPWMUSREG0_DFLT      0x00002001
#define RS780_FVTHROTPWMUSREG1_DFLT      0x00004003
#define RS780_FVTHROTPWMDSREG0_DFLT      0x00002001
#define RS780_FVTHROTPWMDSREG1_DFLT      0x00004003

#define RS780_FVTHROTPWMFBDIVRANGEREG0_DFLT  0x37
#define RS780_FVTHROTPWMFBDIVRANGEREG1_DFLT  0x4b
#define RS780_FVTHROTPWMFBDIVRANGEREG2_DFLT  0x8b

#define RS780D_FVTHROTPWMFBDIVRANGEREG0_DFLT  0x8b
#define RS780D_FVTHROTPWMFBDIVRANGEREG1_DFLT  0x8c
#define RS780D_FVTHROTPWMFBDIVRANGEREG2_DFLT  0xb5

#define RS880D_FVTHROTPWMFBDIVRANGEREG0_DFLT  0x8d
#define RS880D_FVTHROTPWMFBDIVRANGEREG1_DFLT  0x8e
#define RS880D_FVTHROTPWMFBDIVRANGEREG2_DFLT  0xBa

#define RS780_FVTHROTPWMRANGE0_GPIO_DFLT  0x1a
#define RS780_FVTHROTPWMRANGE1_GPIO_DFLT  0x1a
#define RS780_FVTHROTPWMRANGE2_GPIO_DFLT  0x0
#define RS780_FVTHROTPWMRANGE3_GPIO_DFLT  0x0

#define RS780_SLOWCLKFEEDBACKDIV_DFLT 110

#define RS780_CGCLKGATING_DFLT           0x0000E204

#define RS780_DEFAULT_VCLK_FREQ  53300 /* 10 khz */
#define RS780_DEFAULT_DCLK_FREQ  40000 /* 10 khz */

#endif
