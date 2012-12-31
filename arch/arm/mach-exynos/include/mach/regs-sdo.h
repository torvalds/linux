/* linux/arch/arm/mach-exynos/include/mach/regs-sdo.h
 *
 * Copyright (c) 2010 Samsung Electronics
 *		http://www.samsung.com/
 *
 * SDO register description file for Samsung TVOUT driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_REGS_SDO_H
#define __ARCH_ARM_REGS_SDO_H

/*
 * Register part
 */
#define S5P_SDO_CLKCON				(0x0000)
#define S5P_SDO_CONFIG				(0x0008)
#define S5P_SDO_SCALE				(0x000C)
#define S5P_SDO_SYNC				(0x0010)
#define S5P_SDO_VBI				(0x0014)
#define S5P_SDO_SCALE_CH0			(0x001C)
#define S5P_SDO_SCALE_CH1			(0x0020)
#define S5P_SDO_SCALE_CH2			(0x0024)
#define S5P_SDO_YCDELAY				(0x0034)
#define S5P_SDO_SCHLOCK				(0x0038)
#define S5P_SDO_DAC				(0x003C)
#define S5P_SDO_FINFO				(0x0040)
#define S5P_SDO_Y0				(0x0044)
#define S5P_SDO_Y1				(0x0048)
#define S5P_SDO_Y2				(0x004C)
#define S5P_SDO_Y3				(0x0050)
#define S5P_SDO_Y4				(0x0054)
#define S5P_SDO_Y5				(0x0058)
#define S5P_SDO_Y6				(0x005C)
#define S5P_SDO_Y7				(0x0060)
#define S5P_SDO_Y8				(0x0064)
#define S5P_SDO_Y9				(0x0068)
#define S5P_SDO_Y10				(0x006C)
#define S5P_SDO_Y11				(0x0070)
#define S5P_SDO_CB0				(0x0080)
#define S5P_SDO_CB1				(0x0084)
#define S5P_SDO_CB2				(0x0088)
#define S5P_SDO_CB3				(0x008C)
#define S5P_SDO_CB4				(0x0090)
#define S5P_SDO_CB5				(0x0094)
#define S5P_SDO_CB6				(0x0098)
#define S5P_SDO_CB7				(0x009C)
#define S5P_SDO_CB8				(0x00A0)
#define S5P_SDO_CB9				(0x00A4)
#define S5P_SDO_CB10				(0x00A8)
#define S5P_SDO_CB11				(0x00AC)
#define S5P_SDO_CR0				(0x00C0)
#define S5P_SDO_CR1				(0x00C4)
#define S5P_SDO_CR2				(0x00C8)
#define S5P_SDO_CR3				(0x00CC)
#define S5P_SDO_CR4				(0x00D0)
#define S5P_SDO_CR5				(0x00D4)
#define S5P_SDO_CR6				(0x00D8)
#define S5P_SDO_CR7				(0x00DC)
#define S5P_SDO_CR8				(0x00E0)
#define S5P_SDO_CR9				(0x00E4)
#define S5P_SDO_CR10				(0x00E8)
#define S5P_SDO_CR11				(0x00EC)
#define S5P_SDO_MV_ON				(0x0100)
#define S5P_SDO_MV_SLINE_FIRST_EVEN		(0x0104)
#define S5P_SDO_MV_SLINE_FIRST_SPACE_EVEN	(0x0108)
#define S5P_SDO_MV_SLINE_FIRST_ODD		(0x010C)
#define S5P_SDO_MV_SLINE_FIRST_SPACE_ODD	(0x0110)
#define S5P_SDO_MV_SLINE_SPACING		(0x0114)
#define S5P_SDO_MV_STRIPES_NUMBER		(0x0118)
#define S5P_SDO_MV_STRIPES_THICKNESS		(0x011C)
#define S5P_SDO_MV_PSP_DURATION			(0x0120)
#define S5P_SDO_MV_PSP_FIRST			(0x0124)
#define S5P_SDO_MV_PSP_SPACING			(0x0128)
#define S5P_SDO_MV_SEL_LINE_PSP_AGC		(0x012C)
#define S5P_SDO_MV_SEL_FORMAT_PSP_AGC		(0x0130)
#define S5P_SDO_MV_PSP_AGC_A_ON			(0x0134)
#define S5P_SDO_MV_PSP_AGC_B_ON			(0x0138)
#define S5P_SDO_MV_BACK_PORCH			(0x013C)
#define S5P_SDO_MV_BURST_ADVANCED_ON		(0x0140)
#define S5P_SDO_MV_BURST_DURATION_ZONE1		(0x0144)
#define S5P_SDO_MV_BURST_DURATION_ZONE2		(0x0148)
#define S5P_SDO_MV_BURST_DURATION_ZONE3		(0x014C)
#define S5P_SDO_MV_BURST_PHASE_ZONE		(0x0150)
#define S5P_SDO_MV_SLICE_PHASE_LINE		(0x0154)
#define S5P_SDO_MV_RGB_PROTECTION_ON		(0x0158)
#define S5P_SDO_MV_480P_PROTECTION_ON		(0x015C)
#define S5P_SDO_CCCON				(0x0180)
#define S5P_SDO_YSCALE				(0x0184)
#define S5P_SDO_CBSCALE				(0x0188)
#define S5P_SDO_CRSCALE				(0x018C)
#define S5P_SDO_CB_CR_OFFSET			(0x0190)
#define S5P_SDO_CVBS_CC_Y1			(0x0198)
#define S5P_SDO_CVBS_CC_Y2			(0x019C)
#define S5P_SDO_CVBS_CC_C			(0x01A0)
#define S5P_SDO_CSC_525_PORCH			(0x01B0)
#define S5P_SDO_CSC_625_PORCH			(0x01B4)
#define S5P_SDO_OSFC00_0			(0x0200)
#define S5P_SDO_OSFC01_0			(0x0204)
#define S5P_SDO_OSFC02_0			(0x0208)
#define S5P_SDO_OSFC03_0			(0x020C)
#define S5P_SDO_OSFC04_0			(0x0210)
#define S5P_SDO_OSFC05_0			(0x0214)
#define S5P_SDO_OSFC06_0			(0x0218)
#define S5P_SDO_OSFC07_0			(0x021C)
#define S5P_SDO_OSFC08_0			(0x0220)
#define S5P_SDO_OSFC09_0			(0x0224)
#define S5P_SDO_OSFC10_0			(0x0228)
#define S5P_SDO_OSFC11_0			(0x022C)
#define S5P_SDO_OSFC12_0			(0x0230)
#define S5P_SDO_OSFC13_0			(0x0234)
#define S5P_SDO_OSFC14_0			(0x0238)
#define S5P_SDO_OSFC15_0			(0x023C)
#define S5P_SDO_OSFC16_0			(0x0240)
#define S5P_SDO_OSFC17_0			(0x0244)
#define S5P_SDO_OSFC18_0			(0x0248)
#define S5P_SDO_OSFC19_0			(0x024C)
#define S5P_SDO_OSFC20_0			(0x0250)
#define S5P_SDO_OSFC21_0			(0x0254)
#define S5P_SDO_OSFC22_0			(0x0258)
#define S5P_SDO_OSFC23_0			(0x025C)
#define S5P_SDO_XTALK0				(0x0260)
#define S5P_SDO_XTALK1				(0x0264)
#define S5P_SDO_XTALK2				(0x0268)
#define S5P_SDO_BB_CTRL				(0x026C)
#define S5P_SDO_IRQ				(0x0280)
#define S5P_SDO_IRQMASK				(0x0284)
#define S5P_SDO_OSFC00_1			(0x02C0)
#define S5P_SDO_OSFC01_1			(0x02C4)
#define S5P_SDO_OSFC02_1			(0x02C8)
#define S5P_SDO_OSFC03_1			(0x02CC)
#define S5P_SDO_OSFC04_1			(0x02D0)
#define S5P_SDO_OSFC05_1			(0x02D4)
#define S5P_SDO_OSFC06_1			(0x02D8)
#define S5P_SDO_OSFC07_1			(0x02DC)
#define S5P_SDO_OSFC08_1			(0x02E0)
#define S5P_SDO_OSFC09_1			(0x02E4)
#define S5P_SDO_OSFC10_1			(0x02E8)
#define S5P_SDO_OSFC11_1			(0x02EC)
#define S5P_SDO_OSFC12_1			(0x02E0)
#define S5P_SDO_OSFC13_1			(0x02F4)
#define S5P_SDO_OSFC14_1			(0x02F8)
#define S5P_SDO_OSFC15_1			(0x02FC)
#define S5P_SDO_OSFC16_1			(0x0300)
#define S5P_SDO_OSFC17_1			(0x0304)
#define S5P_SDO_OSFC18_1			(0x0308)
#define S5P_SDO_OSFC19_1			(0x030C)
#define S5P_SDO_OSFC20_1			(0x0310)
#define S5P_SDO_OSFC21_1			(0x0314)
#define S5P_SDO_OSFC22_1			(0x0318)
#define S5P_SDO_OSFC23_1			(0x031C)
#define S5P_SDO_OSFC00_2			(0x0320)
#define S5P_SDO_OSFC01_2			(0x0324)
#define S5P_SDO_OSFC02_2			(0x0328)
#define S5P_SDO_OSFC03_2			(0x032C)
#define S5P_SDO_OSFC04_2			(0x0330)
#define S5P_SDO_OSFC05_2			(0x0334)
#define S5P_SDO_OSFC06_2			(0x0338)
#define S5P_SDO_OSFC07_2			(0x033C)
#define S5P_SDO_OSFC08_2			(0x0340)
#define S5P_SDO_OSFC09_2			(0x0344)
#define S5P_SDO_OSFC10_2			(0x0348)
#define S5P_SDO_OSFC11_2			(0x034C)
#define S5P_SDO_OSFC12_2			(0x0350)
#define S5P_SDO_OSFC13_2			(0x0354)
#define S5P_SDO_OSFC14_2			(0x0358)
#define S5P_SDO_OSFC15_2			(0x035C)
#define S5P_SDO_OSFC16_2			(0x0360)
#define S5P_SDO_OSFC17_2			(0x0364)
#define S5P_SDO_OSFC18_2			(0x0368)
#define S5P_SDO_OSFC19_2			(0x036C)
#define S5P_SDO_OSFC20_2			(0x0370)
#define S5P_SDO_OSFC21_2			(0x0374)
#define S5P_SDO_OSFC22_2			(0x0378)
#define S5P_SDO_OSFC23_2			(0x037C)
#define S5P_SDO_ARMCC				(0x03C0)
#define S5P_SDO_ARMWSS525			(0x03C4)
#define S5P_SDO_ARMWSS625			(0x03C8)
#define S5P_SDO_ARMCGMS525			(0x03CC)
#define S5P_SDO_ARMCGMS625			(0x03D4)
#define S5P_SDO_VERSION				(0x03D8)
#define S5P_SDO_CC				(0x0380)
#define S5P_SDO_WSS525				(0x0384)
#define S5P_SDO_WSS625				(0x0388)
#define S5P_SDO_CGMS525				(0x038C)
#define S5P_SDO_CGMS625				(0x0394)

/*
 * Bit definition part
*/
/* SDO Clock Control Register (SDO_CLKCON) */
#define S5P_SDO_TVOUT_SW_RESET			(1 << 4)
#define S5P_SDO_TVOUT_CLOCK_ON			(1)
#define S5P_SDO_TVOUT_CLOCK_OFF			(0)

/* SDO Video Standard Configuration Register (SDO_CONFIG) */
#define S5P_SDO_DAC2_Y_G			(0 << 20)
#define S5P_SDO_DAC2_PB_B			(1 << 20)
#define S5P_SDO_DAC2_PR_R			(2 << 20)
#define S5P_SDO_DAC1_Y_G			(0 << 18)
#define S5P_SDO_DAC1_PB_B			(1 << 18)
#define S5P_SDO_DAC1_PR_R			(2 << 18)
#define S5P_SDO_DAC0_Y_G			(0 << 16)
#define S5P_SDO_DAC0_PB_B			(1 << 16)
#define S5P_SDO_DAC0_PR_R			(2 << 16)
#define S5P_SDO_DAC2_CVBS			(0 << 12)
#define S5P_SDO_DAC2_Y				(1 << 12)
#define S5P_SDO_DAC2_C				(2 << 12)
#define S5P_SDO_DAC1_CVBS			(0 << 10)
#define S5P_SDO_DAC1_Y				(1 << 10)
#define S5P_SDO_DAC1_C				(2 << 10)
#define S5P_SDO_DAC0_CVBS			(0 << 8)
#define S5P_SDO_DAC0_Y				(1 << 8)
#define S5P_SDO_DAC0_C				(2 << 8)
#define S5P_SDO_COMPOSITE			(0 << 6)
#define S5P_SDO_COMPONENT			(1 << 6)
#define S5P_SDO_RGB				(0 << 5)
#define S5P_SDO_YPBPR				(1 << 5)
#define S5P_SDO_INTERLACED			(0 << 4)
#define S5P_SDO_PROGRESSIVE			(1 << 4)
#define S5P_SDO_NTSC_M				(0)
#define S5P_SDO_PAL_M				(1)
#define S5P_SDO_PAL_BGHID			(2)
#define S5P_SDO_PAL_N				(3)
#define S5P_SDO_PAL_NC				(4)
#define S5P_SDO_NTSC_443			(8)
#define S5P_SDO_PAL_60				(9)

/* SDO Video Scale Configuration Register (SDO_SCALE) */
#define S5P_SDO_COMPONENT_LEVEL_SEL_0IRE	(0 << 3)
#define S5P_SDO_COMPONENT_LEVEL_SEL_75IRE	(1 << 3)
#define S5P_SDO_COMPONENT_VTOS_RATIO_10_4	(0 << 2)
#define S5P_SDO_COMPONENT_VTOS_RATIO_7_3	(1 << 2)
#define S5P_SDO_COMPOSITE_LEVEL_SEL_0IRE	(0 << 1)
#define S5P_SDO_COMPOSITE_LEVEL_SEL_75IRE	(1 << 1)
#define S5P_SDO_COMPOSITE_VTOS_RATIO_10_4	(0)
#define S5P_SDO_COMPOSITE_VTOS_RATIO_7_3	(1)

/* SDO Video sync Register  */
#define S5P_SDO_COMPONENT_SYNC_ABSENT		(0)
#define S5P_SDO_COMPONENT_SYNC_YG		(1)
#define S5P_SDO_COMPONENT_SYNC_ALL		(3)

/* SDO VBI Configuration Register (SDO_VBI) */
#define S5P_SDO_CVBS_NO_WSS			(0 << 14)
#define S5P_SDO_CVBS_WSS_INS			(1 << 14)
#define S5P_SDO_CVBS_NO_CLOSED_CAPTION		(0 << 12)
#define S5P_SDO_CVBS_21H_CLOSED_CAPTION		(1 << 12)
#define S5P_SDO_CVBS_21H_284H_CLOSED_CAPTION	(2 << 12)
#define S5P_SDO_CVBS_USE_OTHERS			(3 << 12)

/* SDO Channel #0 Scale Control Register (SDO_SCALE_CH0) */
#define S5P_SDO_SCALE_CONV_OFFSET(x)		(((x) & 0x3FF) << 16)
#define S5P_SDO_SCALE_CONV_GAIN(x)		((x) & 0xFFF)

/* SDO Video Delay Control Register (SDO_YCDELAY) */
#define S5P_SDO_DELAY_YTOC(x)			(((x) & 0xF) << 16)
#define S5P_SDO_ACTIVE_START_OFFSET(x)		(((x) & 0xFF) << 8)
#define S5P_SDO_ACTIVE_END_OFFSET(x)		((x) & 0xFF)

/* SDO SCH Phase Control Register (SDO_SCHLOCK) */
#define S5P_SDO_COLOR_SC_PHASE_ADJ		(1)
#define S5P_SDO_COLOR_SC_PHASE_NOADJ		(0)

/* SDO DAC Configuration Register (SDO_DAC) */
#define S5P_SDO_POWER_ON_DAC			(1 << 0)
#define S5P_SDO_POWER_DOWN_DAC			(0 << 0)

/* SDO Status Register (SDO_FINFO) */
#define S5P_SDO_FIELD_MOD_1001(x)		(((x) & (0x3ff << 16)) >> 16)
#define S5P_SDO_FIELD_ID_BOTTOM(x)		((x) & (1 << 1))
#define S5P_SDO_FIELD_ID_BOTTOM_PI_INCATION(x)	(1)

#define S5P_SDO_MV_AGC_103_ON			(1)

/* SDO Color Compensation On/Off Control (SDO_CCCON) */
#define S5P_SDO_COMPENSATION_BHS_ADJ_ON		(0 << 4)
#define S5P_SDO_COMPENSATION_BHS_ADJ_OFF	(1 << 4)
#define S5P_SDO_COMPENSATION_CVBS_COMP_ON	(0)
#define S5P_SDO_COMPENSATION_CVBS_COMP_OFF	(1)

/* SDO Brightness Control for Y (SDO_YSCALE) */
#define S5P_SDO_BRIGHTNESS_GAIN(x)		(((x) & 0xFF) << 16)
#define S5P_SDO_BRIGHTNESS_OFFSET(x)		((x) & 0xFF)

/* SDO Hue/Saturation Control for CB (SDO_CBSCALE) */
#define S5P_SDO_HS_CB_GAIN0(x)			(((x) & 0x1FF) << 16)
#define S5P_SDO_HS_CB_GAIN1(x)			((x) & 0x1FF)

/* SDO Hue/Saturation Control for CR (SDO_CRSCALE) */
#define S5P_SDO_HS_CR_GAIN0(x)			(((x) & 0x1FF) << 16)
#define S5P_SDO_HS_CR_GAIN1(x)			((x) & 0x1FF)

/* SDO Hue/Saturation Control for CB/CR (SDO_CB_CR_OFFSET) */
#define S5P_SDO_HS_CR_OFFSET(x)			(((x) & 0x3FF) << 16)
#define S5P_SDO_HS_CB_OFFSET(x)			((x) & 0x3FF)

#define S5P_SDO_MAX_RGB_CUBE(x)			(((x) & 0xFF) << 8)
#define S5P_SDO_MIN_RGB_CUBE(x)			((x) & 0xFF)

/* Color Compensation Control Register for CVBS Output (SDO_CVBS_CC_Y1) */
#define S5P_SDO_Y_LOWER_MID_CVBS_CORN(x)	(((x) & 0x3FF) << 16)
#define S5P_SDO_Y_BOTTOM_CVBS_CORN(x)		((x) & 0x3FF)

/* Color Compensation Control Register for CVBS Output (SDO_CVBS_CC_Y2) */
#define S5P_SDO_Y_TOP_CVBS_CORN(x)		(((x) & 0x3FF) << 16)
#define S5P_SDO_Y_UPPER_MID_CVBS_CORN(x)	((x) & 0x3FF)

/* Color Compensation Control Register for CVBS Output (SDO_CVBS_CC_C) */
#define S5P_SDO_RADIUS_CVBS_CORN(x)		((x) & 0x1FF)

/*
 * SDO 525 Line Component Front/Back Porch Position
 * Control Register (SDO_CSC_525_PORCH)
 */
#define S5P_SDO_COMPONENT_525_BP(x)		(((x) & 0x3FF) << 16)
#define S5P_SDO_COMPONENT_525_FP(x)		((x) & 0x3FF)

/*
 * SDO 625 Line Component Front/Back Porch Position
 * Control Resigter(SDO_CSC_625_PORCH
 */
#define S5P_SDO_COMPONENT_625_BP(x)		(((x) & 0x3FF) << 16)
#define S5P_SDO_COMPONENT_625_FP(x)		((x) & 0x3FF)

/* SDO Oversampling #0 Filter Coefficient (SDO_OSFC00_0) */
#define S5P_SDO_OSF_COEF_ODD(x)			(((x) & 0xFFF) << 16)
#define S5P_SDO_OSF_COEF_EVEN(x)		((x) & 0xFFF)

/* SDO Channel Crosstalk Cancellation Coefficient for Ch. 0 (SDO_XTALK0) */
#define S5P_SDO_XTALK_COEF02(x)			(((x) & 0xFF) << 16)
#define S5P_SDO_XTALK_COEF01(x)			((x) & 0xFF)

/* SDO Black Burst Control Register (SDO_BB_CTRL) */
#define S5P_SDO_REF_BB_LEVEL_NTSC		(0x11A << 8)
#define S5P_SDO_REF_BB_LEVEL_PAL		(0xFB << 8)
#define S5P_SDO_SEL_BB_CJAN_CVBS0_BB1_BB2	(0 << 4)
#define S5P_SDO_SEL_BB_CJAN_BB0_CVBS1_BB2	(1 << 4)
#define S5P_SDO_SEL_BB_CJAN_BB0_BB1_CVBS2	(2 << 4)
#define S5P_SDO_BB_MODE_ENABLE			(1)
#define S5P_SDO_BB_MODE_DISABLE			(0)

/* SDO Interrupt Request Register (SDO_IRQ) */
#define S5P_SDO_VSYNC_IRQ_PEND			(1)
#define S5P_SDO_VSYNC_NO_IRQ			(0)

/* SDO Interrupt Request Masking Register (SDO_IRQMASK) */
#define S5P_SDO_VSYNC_IRQ_ENABLE		(0)
#define S5P_SDO_VSYNC_IRQ_DISABLE		(1)

/* SDO Closed Caption Data Registers (SDO_ARMCC) */
#define S5P_SDO_DISPLAY_CC_CAPTION(x)		(((x) & 0xFF) << 16)
#define S5P_SDO_NON_DISPLAY_CC_CAPTION(x)	((x) & 0xFF)

/* SDO WSS 525 Data Registers (SDO_ARMWSS525) */
#define S5P_SDO_CRC_WSS525(x)				(((x) & 0x3F) << 14)
#define S5P_SDO_WORD2_WSS525_COPY_PERMIT		(0 << 6)
#define S5P_SDO_WORD2_WSS525_ONECOPY_PERMIT		(1 << 6)
#define S5P_SDO_WORD2_WSS525_NOCOPY_PERMIT		(3 << 6)
#define S5P_SDO_WORD2_WSS525_MV_PSP_OFF			(0 << 8)
#define S5P_SDO_WORD2_WSS525_MV_PSP_ON_2LINE_BURST	(1 << 8)
#define S5P_SDO_WORD2_WSS525_MV_PSP_ON_BURST_OFF	(2 << 8)
#define S5P_SDO_WORD2_WSS525_MV_PSP_ON_4LINE_BURST	(3 << 8)
#define S5P_SDO_WORD2_WSS525_ANALOG_OFF			(0 << 10)
#define S5P_SDO_WORD2_WSS525_ANALOG_ON			(1 << 10)
#define S5P_SDO_WORD1_WSS525_COPY_INFO			(0 << 2)
#define S5P_SDO_WORD1_WSS525_DEFAULT			(0xF << 2)
#define S5P_SDO_WORD0_WSS525_4_3_NORMAL			(0)
#define S5P_SDO_WORD0_WSS525_16_9_ANAMORPIC		(1)
#define S5P_SDO_WORD0_WSS525_4_3_LETTERBOX		(2)

/* SDO WSS 625 Data Registers (SDO_ARMWSS625) */
#define S5P_SDO_WSS625_SURROUND_SOUND_DISABLE		(0 << 11)
#define S5P_SDO_WSS625_SURROUND_SOUND_ENABLE		(1 << 11)
#define S5P_SDO_WSS625_NO_COPYRIGHT			(0 << 12)
#define S5P_SDO_WSS625_COPYRIGHT			(1 << 12)
#define S5P_SDO_WSS625_COPY_NOT_RESTRICTED		(0 << 13)
#define S5P_SDO_WSS625_COPY_RESTRICTED			(1 << 13)
#define S5P_SDO_WSS625_TELETEXT_NO_SUBTITLES		(0 << 8)
#define S5P_SDO_WSS625_TELETEXT_SUBTITLES		(1 << 8)
#define S5P_SDO_WSS625_NO_OPEN_SUBTITLES		(0 << 9)
#define S5P_SDO_WSS625_INACT_OPEN_SUBTITLES		(1 << 9)
#define S5P_SDO_WSS625_OUTACT_OPEN_SUBTITLES		(2 << 9)
#define S5P_SDO_WSS625_CAMERA				(0 << 4)
#define S5P_SDO_WSS625_FILM				(1 << 4)
#define S5P_SDO_WSS625_NORMAL_PAL			(0 << 5)
#define S5P_SDO_WSS625_MOTION_ADAPTIVE_COLORPLUS	(1 << 5)
#define S5P_SDO_WSS625_HELPER_NO_SIG			(0 << 6)
#define S5P_SDO_WSS625_HELPER_SIG			(1 << 6)
#define S5P_SDO_WSS625_4_3_FULL_576			(0x8)
#define S5P_SDO_WSS625_14_9_LETTERBOX_CENTER_504	(0x1)
#define S5P_SDO_WSS625_14_9_LETTERBOX_TOP_504		(0x2)
#define S5P_SDO_WSS625_16_9_LETTERBOX_CENTER_430	(0xb)
#define S5P_SDO_WSS625_16_9_LETTERBOX_TOP_430		(0x4)
#define S5P_SDO_WSS625_16_9_LETTERBOX_CENTER		(0xd)
#define S5P_SDO_WSS625_14_9_FULL_CENTER_576		(0xe)
#define S5P_SDO_WSS625_16_9_ANAMORPIC_576		(0x7)

/* SDO CGMS-A 525 Data Registers (SDO_ARMCGMS525) */
#define S5P_SDO_CRC_CGMS525(x)				(((x) & 0x3F) << 14)
#define S5P_SDO_WORD2_CGMS525_COPY_PERMIT		(0 << 6)
#define S5P_SDO_WORD2_CGMS525_ONECOPY_PERMIT		(1 << 6)
#define S5P_SDO_WORD2_CGMS525_NOCOPY_PERMIT		(3 << 6)
#define S5P_SDO_WORD2_CGMS525_MV_PSP_OFF		(0 << 8)
#define S5P_SDO_WORD2_CGMS525_MV_PSP_ON_2LINE_BURST	(1 << 8)
#define S5P_SDO_WORD2_CGMS525_MV_PSP_ON_BURST_OFF	(2 << 8)
#define S5P_SDO_WORD2_CGMS525_MV_PSP_ON_4LINE_BURST	(3 << 8)
#define S5P_SDO_WORD2_CGMS525_ANALOG_OFF		(0 << 10)
#define S5P_SDO_WORD2_CGMS525_ANALOG_ON			(1 << 10)
#define S5P_SDO_WORD1_CGMS525_COPY_INFO			(0 << 2)
#define S5P_SDO_WORD1_CGMS525_DEFAULT			(0xF << 2)
#define S5P_SDO_WORD0_CGMS525_4_3_NORMAL		(0)
#define S5P_SDO_WORD0_CGMS525_16_9_ANAMORPIC		(1)
#define S5P_SDO_WORD0_CGMS525_4_3_LETTERBOX		(2)

/* SDO CGMS-A 625 Data Registers (SDO_ARMCGMS625) */
#define S5P_SDO_CGMS625_SURROUND_SOUND_DISABLE		(0 << 11)
#define S5P_SDO_CGMS625_SURROUND_SOUND_ENABLE		(1 << 11)
#define S5P_SDO_CGMS625_NO_COPYRIGHT			(0 << 12)
#define S5P_SDO_CGMS625_COPYRIGHT			(1 << 12)
#define S5P_SDO_CGMS625_COPY_NOT_RESTRICTED		(0 << 13)
#define S5P_SDO_CGMS625_COPY_RESTRICTED			(1 << 13)
#define S5P_SDO_CGMS625_TELETEXT_NO_SUBTITLES		(0 << 8)
#define S5P_SDO_CGMS625_TELETEXT_SUBTITLES		(1 << 8)
#define S5P_SDO_CGMS625_NO_OPEN_SUBTITLES		(0 << 9)
#define S5P_SDO_CGMS625_INACT_OPEN_SUBTITLES		(1 << 9)
#define S5P_SDO_CGMS625_OUTACT_OPEN_SUBTITLES		(2 << 9)
#define S5P_SDO_CGMS625_CAMERA				(0 << 4)
#define S5P_SDO_CGMS625_FILM				(1 << 4)
#define S5P_SDO_CGMS625_NORMAL_PAL			(0 << 5)
#define S5P_SDO_CGMS625_MOTION_ADAPTIVE_COLORPLUS	(1 << 5)
#define S5P_SDO_CGMS625_HELPER_NO_SIG			(0 << 6)
#define S5P_SDO_CGMS625_HELPER_SIG			(1 << 6)
#define S5P_SDO_CGMS625_4_3_FULL_576			(0x8)
#define S5P_SDO_CGMS625_14_9_LETTERBOX_CENTER_504	(0x1)
#define S5P_SDO_CGMS625_14_9_LETTERBOX_TOP_504		(0x2)
#define S5P_SDO_CGMS625_16_9_LETTERBOX_CENTER_430	(0xb)
#define S5P_SDO_CGMS625_16_9_LETTERBOX_TOP_430		(0x4)
#define S5P_SDO_CGMS625_16_9_LETTERBOX_CENTER		(0xd)
#define S5P_SDO_CGMS625_14_9_FULL_CENTER_576		(0xe)
#define S5P_SDO_CGMS625_16_9_ANAMORPIC_576		(0x7)

/* SDO Version Register (SDO_VERSION) */
#define S5P_SDO_VERSION_NUMBER_MASK			(0xFFFFFFFF)

#endif /* __ARCH_ARM_REGS_SDO_H */
