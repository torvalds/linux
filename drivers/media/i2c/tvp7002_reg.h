/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Texas Instruments Triple 8-/10-BIT 165-/110-MSPS Video and Graphics
 * Digitizer with Horizontal PLL registers
 *
 * Copyright (C) 2009 Texas Instruments Inc
 * Author: Santiago Nunez-Corrales <santiago.nunez@ridgerun.com>
 *
 * This code is partially based upon the TVP5150 driver
 * written by Mauro Carvalho Chehab <mchehab@kernel.org>,
 * the TVP514x driver written by Vaibhav Hiremath <hvaibhav@ti.com>
 * and the TVP7002 driver in the TI LSP 2.10.00.14
 */

/* Naming conventions
 * ------------------
 *
 * FDBK:  Feedback
 * DIV:   Divider
 * CTL:   Control
 * SEL:   Select
 * IN:    Input
 * OUT:   Output
 * R:     Red
 * G:     Green
 * B:     Blue
 * OFF:   Offset
 * THRS:  Threshold
 * DGTL:  Digital
 * LVL:   Level
 * PWR:   Power
 * MVIS:  Macrovision
 * W:     Width
 * H:     Height
 * ALGN:  Alignment
 * CLK:   Clocks
 * TOL:   Tolerance
 * BWTH:  Bandwidth
 * COEF:  Coefficient
 * STAT:  Status
 * AUTO:  Automatic
 * FLD:   Field
 * L:	  Line
 */

#define TVP7002_CHIP_REV		0x00
#define TVP7002_HPLL_FDBK_DIV_MSBS	0x01
#define TVP7002_HPLL_FDBK_DIV_LSBS	0x02
#define TVP7002_HPLL_CRTL		0x03
#define TVP7002_HPLL_PHASE_SEL		0x04
#define TVP7002_CLAMP_START		0x05
#define TVP7002_CLAMP_W			0x06
#define TVP7002_HSYNC_OUT_W		0x07
#define TVP7002_B_FINE_GAIN		0x08
#define TVP7002_G_FINE_GAIN		0x09
#define TVP7002_R_FINE_GAIN		0x0a
#define TVP7002_B_FINE_OFF_MSBS		0x0b
#define TVP7002_G_FINE_OFF_MSBS         0x0c
#define TVP7002_R_FINE_OFF_MSBS         0x0d
#define TVP7002_SYNC_CTL_1		0x0e
#define TVP7002_HPLL_AND_CLAMP_CTL	0x0f
#define TVP7002_SYNC_ON_G_THRS		0x10
#define TVP7002_SYNC_SEPARATOR_THRS	0x11
#define TVP7002_HPLL_PRE_COAST		0x12
#define TVP7002_HPLL_POST_COAST		0x13
#define TVP7002_SYNC_DETECT_STAT	0x14
#define TVP7002_OUT_FORMATTER		0x15
#define TVP7002_MISC_CTL_1		0x16
#define TVP7002_MISC_CTL_2              0x17
#define TVP7002_MISC_CTL_3              0x18
#define TVP7002_IN_MUX_SEL_1		0x19
#define TVP7002_IN_MUX_SEL_2            0x1a
#define TVP7002_B_AND_G_COARSE_GAIN	0x1b
#define TVP7002_R_COARSE_GAIN		0x1c
#define TVP7002_FINE_OFF_LSBS		0x1d
#define TVP7002_B_COARSE_OFF		0x1e
#define TVP7002_G_COARSE_OFF            0x1f
#define TVP7002_R_COARSE_OFF            0x20
#define TVP7002_HSOUT_OUT_START		0x21
#define TVP7002_MISC_CTL_4		0x22
#define TVP7002_B_DGTL_ALC_OUT_LSBS	0x23
#define TVP7002_G_DGTL_ALC_OUT_LSBS     0x24
#define TVP7002_R_DGTL_ALC_OUT_LSBS     0x25
#define TVP7002_AUTO_LVL_CTL_ENABLE	0x26
#define TVP7002_DGTL_ALC_OUT_MSBS	0x27
#define TVP7002_AUTO_LVL_CTL_FILTER	0x28
/* Reserved 0x29*/
#define TVP7002_FINE_CLAMP_CTL		0x2a
#define TVP7002_PWR_CTL			0x2b
#define TVP7002_ADC_SETUP		0x2c
#define TVP7002_COARSE_CLAMP_CTL	0x2d
#define TVP7002_SOG_CLAMP		0x2e
#define TVP7002_RGB_COARSE_CLAMP_CTL	0x2f
#define TVP7002_SOG_COARSE_CLAMP_CTL	0x30
#define TVP7002_ALC_PLACEMENT		0x31
/* Reserved 0x32 */
/* Reserved 0x33 */
#define TVP7002_MVIS_STRIPPER_W		0x34
#define TVP7002_VSYNC_ALGN		0x35
#define TVP7002_SYNC_BYPASS		0x36
#define TVP7002_L_FRAME_STAT_LSBS	0x37
#define TVP7002_L_FRAME_STAT_MSBS	0x38
#define TVP7002_CLK_L_STAT_LSBS		0x39
#define TVP7002_CLK_L_STAT_MSBS		0x3a
#define TVP7002_HSYNC_W			0x3b
#define TVP7002_VSYNC_W                 0x3c
#define TVP7002_L_LENGTH_TOL		0x3d
/* Reserved 0x3e */
#define TVP7002_VIDEO_BWTH_CTL		0x3f
#define TVP7002_AVID_START_PIXEL_LSBS	0x40
#define TVP7002_AVID_START_PIXEL_MSBS   0x41
#define TVP7002_AVID_STOP_PIXEL_LSBS	0x42
#define TVP7002_AVID_STOP_PIXEL_MSBS    0x43
#define TVP7002_VBLK_F_0_START_L_OFF	0x44
#define TVP7002_VBLK_F_1_START_L_OFF    0x45
#define TVP7002_VBLK_F_0_DURATION	0x46
#define TVP7002_VBLK_F_1_DURATION       0x47
#define TVP7002_FBIT_F_0_START_L_OFF	0x48
#define TVP7002_FBIT_F_1_START_L_OFF    0x49
#define TVP7002_YUV_Y_G_COEF_LSBS	0x4a
#define TVP7002_YUV_Y_G_COEF_MSBS       0x4b
#define TVP7002_YUV_Y_B_COEF_LSBS       0x4c
#define TVP7002_YUV_Y_B_COEF_MSBS       0x4d
#define TVP7002_YUV_Y_R_COEF_LSBS       0x4e
#define TVP7002_YUV_Y_R_COEF_MSBS       0x4f
#define TVP7002_YUV_U_G_COEF_LSBS       0x50
#define TVP7002_YUV_U_G_COEF_MSBS       0x51
#define TVP7002_YUV_U_B_COEF_LSBS       0x52
#define TVP7002_YUV_U_B_COEF_MSBS       0x53
#define TVP7002_YUV_U_R_COEF_LSBS       0x54
#define TVP7002_YUV_U_R_COEF_MSBS       0x55
#define TVP7002_YUV_V_G_COEF_LSBS       0x56
#define TVP7002_YUV_V_G_COEF_MSBS       0x57
#define TVP7002_YUV_V_B_COEF_LSBS       0x58
#define TVP7002_YUV_V_B_COEF_MSBS       0x59
#define TVP7002_YUV_V_R_COEF_LSBS       0x5a
#define TVP7002_YUV_V_R_COEF_MSBS       0x5b

