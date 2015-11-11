/*
 * adv7183 - Analog Devices ADV7183 video decoder registers
 *
 * Copyright (c) 2011 Analog Devices Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _ADV7183_REGS_H_
#define _ADV7183_REGS_H_

#define ADV7183_IN_CTRL            0x00 /* Input control */
#define ADV7183_VD_SEL             0x01 /* Video selection */
#define ADV7183_OUT_CTRL           0x03 /* Output control */
#define ADV7183_EXT_OUT_CTRL       0x04 /* Extended output control */
#define ADV7183_AUTO_DET_EN        0x07 /* Autodetect enable */
#define ADV7183_CONTRAST           0x08 /* Contrast */
#define ADV7183_BRIGHTNESS         0x0A /* Brightness */
#define ADV7183_HUE                0x0B /* Hue */
#define ADV7183_DEF_Y              0x0C /* Default value Y */
#define ADV7183_DEF_C              0x0D /* Default value C */
#define ADV7183_ADI_CTRL           0x0E /* ADI control */
#define ADV7183_POW_MANAGE         0x0F /* Power Management */
#define ADV7183_STATUS_1           0x10 /* Status 1 */
#define ADV7183_IDENT              0x11 /* Ident */
#define ADV7183_STATUS_2           0x12 /* Status 2 */
#define ADV7183_STATUS_3           0x13 /* Status 3 */
#define ADV7183_ANAL_CLAMP_CTRL    0x14 /* Analog clamp control */
#define ADV7183_DIGI_CLAMP_CTRL_1  0x15 /* Digital clamp control 1 */
#define ADV7183_SHAP_FILT_CTRL     0x17 /* Shaping filter control */
#define ADV7183_SHAP_FILT_CTRL_2   0x18 /* Shaping filter control 2 */
#define ADV7183_COMB_FILT_CTRL     0x19 /* Comb filter control */
#define ADV7183_ADI_CTRL_2         0x1D /* ADI control 2 */
#define ADV7183_PIX_DELAY_CTRL     0x27 /* Pixel delay control */
#define ADV7183_MISC_GAIN_CTRL     0x2B /* Misc gain control */
#define ADV7183_AGC_MODE_CTRL      0x2C /* AGC mode control */
#define ADV7183_CHRO_GAIN_CTRL_1   0x2D /* Chroma gain control 1 */
#define ADV7183_CHRO_GAIN_CTRL_2   0x2E /* Chroma gain control 2 */
#define ADV7183_LUMA_GAIN_CTRL_1   0x2F /* Luma gain control 1 */
#define ADV7183_LUMA_GAIN_CTRL_2   0x30 /* Luma gain control 2 */
#define ADV7183_VS_FIELD_CTRL_1    0x31 /* Vsync field control 1 */
#define ADV7183_VS_FIELD_CTRL_2    0x32 /* Vsync field control 2 */
#define ADV7183_VS_FIELD_CTRL_3    0x33 /* Vsync field control 3 */
#define ADV7183_HS_POS_CTRL_1      0x34 /* Hsync position control 1 */
#define ADV7183_HS_POS_CTRL_2      0x35 /* Hsync position control 2 */
#define ADV7183_HS_POS_CTRL_3      0x36 /* Hsync position control 3 */
#define ADV7183_POLARITY           0x37 /* Polarity */
#define ADV7183_NTSC_COMB_CTRL     0x38 /* NTSC comb control */
#define ADV7183_PAL_COMB_CTRL      0x39 /* PAL comb control */
#define ADV7183_ADC_CTRL           0x3A /* ADC control */
#define ADV7183_MAN_WIN_CTRL       0x3D /* Manual window control */
#define ADV7183_RESAMPLE_CTRL      0x41 /* Resample control */
#define ADV7183_GEMSTAR_CTRL_1     0x48 /* Gemstar ctrl 1 */
#define ADV7183_GEMSTAR_CTRL_2     0x49 /* Gemstar ctrl 2 */
#define ADV7183_GEMSTAR_CTRL_3     0x4A /* Gemstar ctrl 3 */
#define ADV7183_GEMSTAR_CTRL_4     0x4B /* Gemstar ctrl 4 */
#define ADV7183_GEMSTAR_CTRL_5     0x4C /* Gemstar ctrl 5 */
#define ADV7183_CTI_DNR_CTRL_1     0x4D /* CTI DNR ctrl 1 */
#define ADV7183_CTI_DNR_CTRL_2     0x4E /* CTI DNR ctrl 2 */
#define ADV7183_CTI_DNR_CTRL_4     0x50 /* CTI DNR ctrl 4 */
#define ADV7183_LOCK_CNT           0x51 /* Lock count */
#define ADV7183_FREE_LINE_LEN      0x8F /* Free-Run line length 1 */
#define ADV7183_VBI_INFO           0x90 /* VBI info */
#define ADV7183_WSS_1              0x91 /* WSS 1 */
#define ADV7183_WSS_2              0x92 /* WSS 2 */
#define ADV7183_EDTV_1             0x93 /* EDTV 1 */
#define ADV7183_EDTV_2             0x94 /* EDTV 2 */
#define ADV7183_EDTV_3             0x95 /* EDTV 3 */
#define ADV7183_CGMS_1             0x96 /* CGMS 1 */
#define ADV7183_CGMS_2             0x97 /* CGMS 2 */
#define ADV7183_CGMS_3             0x98 /* CGMS 3 */
#define ADV7183_CCAP_1             0x99 /* CCAP 1 */
#define ADV7183_CCAP_2             0x9A /* CCAP 2 */
#define ADV7183_LETTERBOX_1        0x9B /* Letterbox 1 */
#define ADV7183_LETTERBOX_2        0x9C /* Letterbox 2 */
#define ADV7183_LETTERBOX_3        0x9D /* Letterbox 3 */
#define ADV7183_CRC_EN             0xB2 /* CRC enable */
#define ADV7183_ADC_SWITCH_1       0xC3 /* ADC switch 1 */
#define ADV7183_ADC_SWITCH_2       0xC4 /* ADC swithc 2 */
#define ADV7183_LETTERBOX_CTRL_1   0xDC /* Letterbox control 1 */
#define ADV7183_LETTERBOX_CTRL_2   0xDD /* Letterbox control 2 */
#define ADV7183_SD_OFFSET_CB       0xE1 /* SD offset Cb */
#define ADV7183_SD_OFFSET_CR       0xE2 /* SD offset Cr */
#define ADV7183_SD_SATURATION_CB   0xE3 /* SD saturation Cb */
#define ADV7183_SD_SATURATION_CR   0xE4 /* SD saturation Cr */
#define ADV7183_NTSC_V_BEGIN       0xE5 /* NTSC V bit begin */
#define ADV7183_NTSC_V_END         0xE6 /* NTSC V bit end */
#define ADV7183_NTSC_F_TOGGLE      0xE7 /* NTSC F bit toggle */
#define ADV7183_PAL_V_BEGIN        0xE8 /* PAL V bit begin */
#define ADV7183_PAL_V_END          0xE9 /* PAL V bit end */
#define ADV7183_PAL_F_TOGGLE       0xEA /* PAL F bit toggle */
#define ADV7183_DRIVE_STR          0xF4 /* Drive strength */
#define ADV7183_IF_COMP_CTRL       0xF8 /* IF comp control */
#define ADV7183_VS_MODE_CTRL       0xF9 /* VS mode control */

#endif
