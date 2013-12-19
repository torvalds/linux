/* saa711x - Philips SAA711x video decoder register specifications
 *
 * Copyright (c) 2006 Mauro Carvalho Chehab <mchehab@infradead.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define R_00_CHIP_VERSION                             0x00
/* Video Decoder */
	/* Video Decoder - Frontend part */
#define R_01_INC_DELAY                                0x01
#define R_02_INPUT_CNTL_1                             0x02
#define R_03_INPUT_CNTL_2                             0x03
#define R_04_INPUT_CNTL_3                             0x04
#define R_05_INPUT_CNTL_4                             0x05
	/* Video Decoder - Decoder part */
#define R_06_H_SYNC_START                             0x06
#define R_07_H_SYNC_STOP                              0x07
#define R_08_SYNC_CNTL                                0x08
#define R_09_LUMA_CNTL                                0x09
#define R_0A_LUMA_BRIGHT_CNTL                         0x0a
#define R_0B_LUMA_CONTRAST_CNTL                       0x0b
#define R_0C_CHROMA_SAT_CNTL                          0x0c
#define R_0D_CHROMA_HUE_CNTL                          0x0d
#define R_0E_CHROMA_CNTL_1                            0x0e
#define R_0F_CHROMA_GAIN_CNTL                         0x0f
#define R_10_CHROMA_CNTL_2                            0x10
#define R_11_MODE_DELAY_CNTL                          0x11
#define R_12_RT_SIGNAL_CNTL                           0x12
#define R_13_RT_X_PORT_OUT_CNTL                       0x13
#define R_14_ANAL_ADC_COMPAT_CNTL                     0x14
#define R_15_VGATE_START_FID_CHG                      0x15
#define R_16_VGATE_STOP                               0x16
#define R_17_MISC_VGATE_CONF_AND_MSB                  0x17
#define R_18_RAW_DATA_GAIN_CNTL                       0x18
#define R_19_RAW_DATA_OFF_CNTL                        0x19
#define R_1A_COLOR_KILL_LVL_CNTL                      0x1a
#define R_1B_MISC_TVVCRDET                            0x1b
#define R_1C_ENHAN_COMB_CTRL1                         0x1c
#define R_1D_ENHAN_COMB_CTRL2                         0x1d
#define R_1E_STATUS_BYTE_1_VD_DEC                     0x1e
#define R_1F_STATUS_BYTE_2_VD_DEC                     0x1f

/* Component processing and interrupt masking part */
#define R_23_INPUT_CNTL_5                             0x23
#define R_24_INPUT_CNTL_6                             0x24
#define R_25_INPUT_CNTL_7                             0x25
#define R_29_COMP_DELAY                               0x29
#define R_2A_COMP_BRIGHT_CNTL                         0x2a
#define R_2B_COMP_CONTRAST_CNTL                       0x2b
#define R_2C_COMP_SAT_CNTL                            0x2c
#define R_2D_INTERRUPT_MASK_1                         0x2d
#define R_2E_INTERRUPT_MASK_2                         0x2e
#define R_2F_INTERRUPT_MASK_3                         0x2f

/* Audio clock generator part */
#define R_30_AUD_MAST_CLK_CYCLES_PER_FIELD            0x30
#define R_34_AUD_MAST_CLK_NOMINAL_INC                 0x34
#define R_38_CLK_RATIO_AMXCLK_TO_ASCLK                0x38
#define R_39_CLK_RATIO_ASCLK_TO_ALRCLK                0x39
#define R_3A_AUD_CLK_GEN_BASIC_SETUP                  0x3a

/* General purpose VBI data slicer part */
#define R_40_SLICER_CNTL_1                            0x40
#define R_41_LCR_BASE                                 0x41
#define R_58_PROGRAM_FRAMING_CODE                     0x58
#define R_59_H_OFF_FOR_SLICER                         0x59
#define R_5A_V_OFF_FOR_SLICER                         0x5a
#define R_5B_FLD_OFF_AND_MSB_FOR_H_AND_V_OFF          0x5b
#define R_5D_DID                                      0x5d
#define R_5E_SDID                                     0x5e
#define R_60_SLICER_STATUS_BYTE_0                     0x60
#define R_61_SLICER_STATUS_BYTE_1                     0x61
#define R_62_SLICER_STATUS_BYTE_2                     0x62

/* X port, I port and the scaler part */
	/* Task independent global settings */
#define R_80_GLOBAL_CNTL_1                            0x80
#define R_81_V_SYNC_FLD_ID_SRC_SEL_AND_RETIMED_V_F    0x81
#define R_83_X_PORT_I_O_ENA_AND_OUT_CLK               0x83
#define R_84_I_PORT_SIGNAL_DEF                        0x84
#define R_85_I_PORT_SIGNAL_POLAR                      0x85
#define R_86_I_PORT_FIFO_FLAG_CNTL_AND_ARBIT          0x86
#define R_87_I_PORT_I_O_ENA_OUT_CLK_AND_GATED         0x87
#define R_88_POWER_SAVE_ADC_PORT_CNTL                 0x88
#define R_8F_STATUS_INFO_SCALER                       0x8f
	/* Task A definition */
		/* Basic settings and acquisition window definition */
#define R_90_A_TASK_HANDLING_CNTL                     0x90
#define R_91_A_X_PORT_FORMATS_AND_CONF                0x91
#define R_92_A_X_PORT_INPUT_REFERENCE_SIGNAL          0x92
#define R_93_A_I_PORT_OUTPUT_FORMATS_AND_CONF         0x93
#define R_94_A_HORIZ_INPUT_WINDOW_START               0x94
#define R_95_A_HORIZ_INPUT_WINDOW_START_MSB           0x95
#define R_96_A_HORIZ_INPUT_WINDOW_LENGTH              0x96
#define R_97_A_HORIZ_INPUT_WINDOW_LENGTH_MSB          0x97
#define R_98_A_VERT_INPUT_WINDOW_START                0x98
#define R_99_A_VERT_INPUT_WINDOW_START_MSB            0x99
#define R_9A_A_VERT_INPUT_WINDOW_LENGTH               0x9a
#define R_9B_A_VERT_INPUT_WINDOW_LENGTH_MSB           0x9b
#define R_9C_A_HORIZ_OUTPUT_WINDOW_LENGTH             0x9c
#define R_9D_A_HORIZ_OUTPUT_WINDOW_LENGTH_MSB         0x9d
#define R_9E_A_VERT_OUTPUT_WINDOW_LENGTH              0x9e
#define R_9F_A_VERT_OUTPUT_WINDOW_LENGTH_MSB          0x9f
		/* FIR filtering and prescaling */
#define R_A0_A_HORIZ_PRESCALING                       0xa0
#define R_A1_A_ACCUMULATION_LENGTH                    0xa1
#define R_A2_A_PRESCALER_DC_GAIN_AND_FIR_PREFILTER    0xa2
#define R_A4_A_LUMA_BRIGHTNESS_CNTL                   0xa4
#define R_A5_A_LUMA_CONTRAST_CNTL                     0xa5
#define R_A6_A_CHROMA_SATURATION_CNTL                 0xa6
		/* Horizontal phase scaling */
#define R_A8_A_HORIZ_LUMA_SCALING_INC                 0xa8
#define R_A9_A_HORIZ_LUMA_SCALING_INC_MSB             0xa9
#define R_AA_A_HORIZ_LUMA_PHASE_OFF                   0xaa
#define R_AC_A_HORIZ_CHROMA_SCALING_INC               0xac
#define R_AD_A_HORIZ_CHROMA_SCALING_INC_MSB           0xad
#define R_AE_A_HORIZ_CHROMA_PHASE_OFF                 0xae
#define R_AF_A_HORIZ_CHROMA_PHASE_OFF_MSB             0xaf
		/* Vertical scaling */
#define R_B0_A_VERT_LUMA_SCALING_INC                  0xb0
#define R_B1_A_VERT_LUMA_SCALING_INC_MSB              0xb1
#define R_B2_A_VERT_CHROMA_SCALING_INC                0xb2
#define R_B3_A_VERT_CHROMA_SCALING_INC_MSB            0xb3
#define R_B4_A_VERT_SCALING_MODE_CNTL                 0xb4
#define R_B8_A_VERT_CHROMA_PHASE_OFF_00               0xb8
#define R_B9_A_VERT_CHROMA_PHASE_OFF_01               0xb9
#define R_BA_A_VERT_CHROMA_PHASE_OFF_10               0xba
#define R_BB_A_VERT_CHROMA_PHASE_OFF_11               0xbb
#define R_BC_A_VERT_LUMA_PHASE_OFF_00                 0xbc
#define R_BD_A_VERT_LUMA_PHASE_OFF_01                 0xbd
#define R_BE_A_VERT_LUMA_PHASE_OFF_10                 0xbe
#define R_BF_A_VERT_LUMA_PHASE_OFF_11                 0xbf
	/* Task B definition */
		/* Basic settings and acquisition window definition */
#define R_C0_B_TASK_HANDLING_CNTL                     0xc0
#define R_C1_B_X_PORT_FORMATS_AND_CONF                0xc1
#define R_C2_B_INPUT_REFERENCE_SIGNAL_DEFINITION      0xc2
#define R_C3_B_I_PORT_FORMATS_AND_CONF                0xc3
#define R_C4_B_HORIZ_INPUT_WINDOW_START               0xc4
#define R_C5_B_HORIZ_INPUT_WINDOW_START_MSB           0xc5
#define R_C6_B_HORIZ_INPUT_WINDOW_LENGTH              0xc6
#define R_C7_B_HORIZ_INPUT_WINDOW_LENGTH_MSB          0xc7
#define R_C8_B_VERT_INPUT_WINDOW_START                0xc8
#define R_C9_B_VERT_INPUT_WINDOW_START_MSB            0xc9
#define R_CA_B_VERT_INPUT_WINDOW_LENGTH               0xca
#define R_CB_B_VERT_INPUT_WINDOW_LENGTH_MSB           0xcb
#define R_CC_B_HORIZ_OUTPUT_WINDOW_LENGTH             0xcc
#define R_CD_B_HORIZ_OUTPUT_WINDOW_LENGTH_MSB         0xcd
#define R_CE_B_VERT_OUTPUT_WINDOW_LENGTH              0xce
#define R_CF_B_VERT_OUTPUT_WINDOW_LENGTH_MSB          0xcf
		/* FIR filtering and prescaling */
#define R_D0_B_HORIZ_PRESCALING                       0xd0
#define R_D1_B_ACCUMULATION_LENGTH                    0xd1
#define R_D2_B_PRESCALER_DC_GAIN_AND_FIR_PREFILTER    0xd2
#define R_D4_B_LUMA_BRIGHTNESS_CNTL                   0xd4
#define R_D5_B_LUMA_CONTRAST_CNTL                     0xd5
#define R_D6_B_CHROMA_SATURATION_CNTL                 0xd6
		/* Horizontal phase scaling */
#define R_D8_B_HORIZ_LUMA_SCALING_INC                 0xd8
#define R_D9_B_HORIZ_LUMA_SCALING_INC_MSB             0xd9
#define R_DA_B_HORIZ_LUMA_PHASE_OFF                   0xda
#define R_DC_B_HORIZ_CHROMA_SCALING                   0xdc
#define R_DD_B_HORIZ_CHROMA_SCALING_MSB               0xdd
#define R_DE_B_HORIZ_PHASE_OFFSET_CRHOMA              0xde
		/* Vertical scaling */
#define R_E0_B_VERT_LUMA_SCALING_INC                  0xe0
#define R_E1_B_VERT_LUMA_SCALING_INC_MSB              0xe1
#define R_E2_B_VERT_CHROMA_SCALING_INC                0xe2
#define R_E3_B_VERT_CHROMA_SCALING_INC_MSB            0xe3
#define R_E4_B_VERT_SCALING_MODE_CNTL                 0xe4
#define R_E8_B_VERT_CHROMA_PHASE_OFF_00               0xe8
#define R_E9_B_VERT_CHROMA_PHASE_OFF_01               0xe9
#define R_EA_B_VERT_CHROMA_PHASE_OFF_10               0xea
#define R_EB_B_VERT_CHROMA_PHASE_OFF_11               0xeb
#define R_EC_B_VERT_LUMA_PHASE_OFF_00                 0xec
#define R_ED_B_VERT_LUMA_PHASE_OFF_01                 0xed
#define R_EE_B_VERT_LUMA_PHASE_OFF_10                 0xee
#define R_EF_B_VERT_LUMA_PHASE_OFF_11                 0xef

/* second PLL (PLL2) and Pulsegenerator Programming */
#define R_F0_LFCO_PER_LINE                            0xf0
#define R_F1_P_I_PARAM_SELECT                         0xf1
#define R_F2_NOMINAL_PLL2_DTO                         0xf2
#define R_F3_PLL_INCREMENT                            0xf3
#define R_F4_PLL2_STATUS                              0xf4
#define R_F5_PULSGEN_LINE_LENGTH                      0xf5
#define R_F6_PULSE_A_POS_LSB_AND_PULSEGEN_CONFIG      0xf6
#define R_F7_PULSE_A_POS_MSB                          0xf7
#define R_F8_PULSE_B_POS                              0xf8
#define R_F9_PULSE_B_POS_MSB                          0xf9
#define R_FA_PULSE_C_POS                              0xfa
#define R_FB_PULSE_C_POS_MSB                          0xfb
#define R_FF_S_PLL_MAX_PHASE_ERR_THRESH_NUM_LINES     0xff

/* SAA7113 bit-masks */
#define SAA7113_R_08_HTC_OFFSET 3
#define SAA7113_R_08_HTC_MASK (0x3 << SAA7113_R_08_HTC_OFFSET)
#define SAA7113_R_08_FSEL 0x40
#define SAA7113_R_08_AUFD 0x80

#define SAA7113_R_10_VRLN_OFFSET 3
#define SAA7113_R_10_VRLN_MASK (0x1 << SAA7113_R_10_VRLN_OFFSET)
#define SAA7113_R_10_OFTS_OFFSET 6
#define SAA7113_R_10_OFTS_MASK (0x3 << SAA7113_R_10_OFTS_OFFSET)

#define SAA7113_R_12_RTS0_OFFSET 0
#define SAA7113_R_12_RTS0_MASK (0xf << SAA7113_R_12_RTS0_OFFSET)
#define SAA7113_R_12_RTS1_OFFSET 4
#define SAA7113_R_12_RTS1_MASK (0xf << SAA7113_R_12_RTS1_OFFSET)

#define SAA7113_R_13_ADLSB_OFFSET 7
#define SAA7113_R_13_ADLSB_MASK (0x1 << SAA7113_R_13_ADLSB_OFFSET)

#if 0
/* Those structs will be used in the future for debug purposes */
struct saa711x_reg_descr {
	u8 reg;
	int count;
	char *name;
};

struct saa711x_reg_descr saa711x_regs[] = {
	/* REG COUNT NAME */
	{R_00_CHIP_VERSION,1,
	 "Chip version"},

	/* Video Decoder: R_01_INC_DELAY to R_1F_STATUS_BYTE_2_VD_DEC */

	/* Video Decoder - Frontend part: R_01_INC_DELAY to R_05_INPUT_CNTL_4 */
	{R_01_INC_DELAY,1,
	 "Increment delay"},
	{R_02_INPUT_CNTL_1,1,
	 "Analog input control 1"},
	{R_03_INPUT_CNTL_2,1,
	 "Analog input control 2"},
	{R_04_INPUT_CNTL_3,1,
	 "Analog input control 3"},
	{R_05_INPUT_CNTL_4,1,
	 "Analog input control 4"},

	/* Video Decoder - Decoder part: R_06_H_SYNC_START to R_1F_STATUS_BYTE_2_VD_DEC */
	{R_06_H_SYNC_START,1,
	 "Horizontal sync start"},
	{R_07_H_SYNC_STOP,1,
	 "Horizontal sync stop"},
	{R_08_SYNC_CNTL,1,
	 "Sync control"},
	{R_09_LUMA_CNTL,1,
	 "Luminance control"},
	{R_0A_LUMA_BRIGHT_CNTL,1,
	 "Luminance brightness control"},
	{R_0B_LUMA_CONTRAST_CNTL,1,
	 "Luminance contrast control"},
	{R_0C_CHROMA_SAT_CNTL,1,
	 "Chrominance saturation control"},
	{R_0D_CHROMA_HUE_CNTL,1,
	 "Chrominance hue control"},
	{R_0E_CHROMA_CNTL_1,1,
	 "Chrominance control 1"},
	{R_0F_CHROMA_GAIN_CNTL,1,
	 "Chrominance gain control"},
	{R_10_CHROMA_CNTL_2,1,
	 "Chrominance control 2"},
	{R_11_MODE_DELAY_CNTL,1,
	 "Mode/delay control"},
	{R_12_RT_SIGNAL_CNTL,1,
	 "RT signal control"},
	{R_13_RT_X_PORT_OUT_CNTL,1,
	 "RT/X port output control"},
	{R_14_ANAL_ADC_COMPAT_CNTL,1,
	 "Analog/ADC/compatibility control"},
	{R_15_VGATE_START_FID_CHG,  1,
	 "VGATE start FID change"},
	{R_16_VGATE_STOP,1,
	 "VGATE stop"},
	{R_17_MISC_VGATE_CONF_AND_MSB,  1,
	 "Miscellaneous VGATE configuration and MSBs"},
	{R_18_RAW_DATA_GAIN_CNTL,1,
	 "Raw data gain control",},
	{R_19_RAW_DATA_OFF_CNTL,1,
	 "Raw data offset control",},
	{R_1A_COLOR_KILL_LVL_CNTL,1,
	 "Color Killer Level Control"},
	{ R_1B_MISC_TVVCRDET, 1,
	  "MISC /TVVCRDET"},
	{ R_1C_ENHAN_COMB_CTRL1, 1,
	 "Enhanced comb ctrl1"},
	{ R_1D_ENHAN_COMB_CTRL2, 1,
	 "Enhanced comb ctrl1"},
	{R_1E_STATUS_BYTE_1_VD_DEC,1,
	 "Status byte 1 video decoder"},
	{R_1F_STATUS_BYTE_2_VD_DEC,1,
	 "Status byte 2 video decoder"},

	/* Component processing and interrupt masking part:  0x20h to R_2F_INTERRUPT_MASK_3 */
	/* 0x20 to 0x22 - Reserved */
	{R_23_INPUT_CNTL_5,1,
	 "Analog input control 5"},
	{R_24_INPUT_CNTL_6,1,
	 "Analog input control 6"},
	{R_25_INPUT_CNTL_7,1,
	 "Analog input control 7"},
	/* 0x26 to 0x28 - Reserved */
	{R_29_COMP_DELAY,1,
	 "Component delay"},
	{R_2A_COMP_BRIGHT_CNTL,1,
	 "Component brightness control"},
	{R_2B_COMP_CONTRAST_CNTL,1,
	 "Component contrast control"},
	{R_2C_COMP_SAT_CNTL,1,
	 "Component saturation control"},
	{R_2D_INTERRUPT_MASK_1,1,
	 "Interrupt mask 1"},
	{R_2E_INTERRUPT_MASK_2,1,
	 "Interrupt mask 2"},
	{R_2F_INTERRUPT_MASK_3,1,
	 "Interrupt mask 3"},

	/* Audio clock generator part: R_30_AUD_MAST_CLK_CYCLES_PER_FIELD to 0x3f */
	{R_30_AUD_MAST_CLK_CYCLES_PER_FIELD,3,
	 "Audio master clock cycles per field"},
	/* 0x33 - Reserved */
	{R_34_AUD_MAST_CLK_NOMINAL_INC,3,
	 "Audio master clock nominal increment"},
	/* 0x37 - Reserved */
	{R_38_CLK_RATIO_AMXCLK_TO_ASCLK,1,
	 "Clock ratio AMXCLK to ASCLK"},
	{R_39_CLK_RATIO_ASCLK_TO_ALRCLK,1,
	 "Clock ratio ASCLK to ALRCLK"},
	{R_3A_AUD_CLK_GEN_BASIC_SETUP,1,
	 "Audio clock generator basic setup"},
	/* 0x3b-0x3f - Reserved */

	/* General purpose VBI data slicer part: R_40_SLICER_CNTL_1 to 0x7f */
	{R_40_SLICER_CNTL_1,1,
	 "Slicer control 1"},
	{R_41_LCR,23,
	 "R_41_LCR"},
	{R_58_PROGRAM_FRAMING_CODE,1,
	 "Programmable framing code"},
	{R_59_H_OFF_FOR_SLICER,1,
	 "Horizontal offset for slicer"},
	{R_5A_V_OFF_FOR_SLICER,1,
	 "Vertical offset for slicer"},
	{R_5B_FLD_OFF_AND_MSB_FOR_H_AND_V_OFF,1,
	 "Field offset and MSBs for horizontal and vertical offset"},
	{R_5D_DID,1,
	 "Header and data identification (R_5D_DID)"},
	{R_5E_SDID,1,
	 "Sliced data identification (R_5E_SDID) code"},
	{R_60_SLICER_STATUS_BYTE_0,1,
	 "Slicer status byte 0"},
	{R_61_SLICER_STATUS_BYTE_1,1,
	 "Slicer status byte 1"},
	{R_62_SLICER_STATUS_BYTE_2,1,
	 "Slicer status byte 2"},
	/* 0x63-0x7f - Reserved */

	/* X port, I port and the scaler part: R_80_GLOBAL_CNTL_1 to R_EF_B_VERT_LUMA_PHASE_OFF_11 */
	/* Task independent global settings: R_80_GLOBAL_CNTL_1 to R_8F_STATUS_INFO_SCALER */
	{R_80_GLOBAL_CNTL_1,1,
	 "Global control 1"},
	{R_81_V_SYNC_FLD_ID_SRC_SEL_AND_RETIMED_V_F,1,
	 "Vertical sync and Field ID source selection, retimed V and F signals"},
	/* 0x82 - Reserved */
	{R_83_X_PORT_I_O_ENA_AND_OUT_CLK,1,
	 "X port I/O enable and output clock"},
	{R_84_I_PORT_SIGNAL_DEF,1,
	 "I port signal definitions"},
	{R_85_I_PORT_SIGNAL_POLAR,1,
	 "I port signal polarities"},
	{R_86_I_PORT_FIFO_FLAG_CNTL_AND_ARBIT,1,
	 "I port FIFO flag control and arbitration"},
	{R_87_I_PORT_I_O_ENA_OUT_CLK_AND_GATED,  1,
	 "I port I/O enable output clock and gated"},
	{R_88_POWER_SAVE_ADC_PORT_CNTL,1,
	 "Power save/ADC port control"},
	/* 089-0x8e - Reserved */
	{R_8F_STATUS_INFO_SCALER,1,
	 "Status information scaler part"},

	/* Task A definition: R_90_A_TASK_HANDLING_CNTL to R_BF_A_VERT_LUMA_PHASE_OFF_11 */
	/* Task A: Basic settings and acquisition window definition */
	{R_90_A_TASK_HANDLING_CNTL,1,
	 "Task A: Task handling control"},
	{R_91_A_X_PORT_FORMATS_AND_CONF,1,
	 "Task A: X port formats and configuration"},
	{R_92_A_X_PORT_INPUT_REFERENCE_SIGNAL,1,
	 "Task A: X port input reference signal definition"},
	{R_93_A_I_PORT_OUTPUT_FORMATS_AND_CONF,1,
	 "Task A: I port output formats and configuration"},
	{R_94_A_HORIZ_INPUT_WINDOW_START,2,
	 "Task A: Horizontal input window start"},
	{R_96_A_HORIZ_INPUT_WINDOW_LENGTH,2,
	 "Task A: Horizontal input window length"},
	{R_98_A_VERT_INPUT_WINDOW_START,2,
	 "Task A: Vertical input window start"},
	{R_9A_A_VERT_INPUT_WINDOW_LENGTH,2,
	 "Task A: Vertical input window length"},
	{R_9C_A_HORIZ_OUTPUT_WINDOW_LENGTH,2,
	 "Task A: Horizontal output window length"},
	{R_9E_A_VERT_OUTPUT_WINDOW_LENGTH,2,
	 "Task A: Vertical output window length"},

	/* Task A: FIR filtering and prescaling */
	{R_A0_A_HORIZ_PRESCALING,1,
	 "Task A: Horizontal prescaling"},
	{R_A1_A_ACCUMULATION_LENGTH,1,
	 "Task A: Accumulation length"},
	{R_A2_A_PRESCALER_DC_GAIN_AND_FIR_PREFILTER,1,
	 "Task A: Prescaler DC gain and FIR prefilter"},
	/* 0xa3 - Reserved */
	{R_A4_A_LUMA_BRIGHTNESS_CNTL,1,
	 "Task A: Luminance brightness control"},
	{R_A5_A_LUMA_CONTRAST_CNTL,1,
	 "Task A: Luminance contrast control"},
	{R_A6_A_CHROMA_SATURATION_CNTL,1,
	 "Task A: Chrominance saturation control"},
	/* 0xa7 - Reserved */

	/* Task A: Horizontal phase scaling */
	{R_A8_A_HORIZ_LUMA_SCALING_INC,2,
	 "Task A: Horizontal luminance scaling increment"},
	{R_AA_A_HORIZ_LUMA_PHASE_OFF,1,
	 "Task A: Horizontal luminance phase offset"},
	/* 0xab - Reserved */
	{R_AC_A_HORIZ_CHROMA_SCALING_INC,2,
	 "Task A: Horizontal chrominance scaling increment"},
	{R_AE_A_HORIZ_CHROMA_PHASE_OFF,1,
	 "Task A: Horizontal chrominance phase offset"},
	/* 0xaf - Reserved */

	/* Task A: Vertical scaling */
	{R_B0_A_VERT_LUMA_SCALING_INC,2,
	 "Task A: Vertical luminance scaling increment"},
	{R_B2_A_VERT_CHROMA_SCALING_INC,2,
	 "Task A: Vertical chrominance scaling increment"},
	{R_B4_A_VERT_SCALING_MODE_CNTL,1,
	 "Task A: Vertical scaling mode control"},
	/* 0xb5-0xb7 - Reserved */
	{R_B8_A_VERT_CHROMA_PHASE_OFF_00,1,
	 "Task A: Vertical chrominance phase offset '00'"},
	{R_B9_A_VERT_CHROMA_PHASE_OFF_01,1,
	 "Task A: Vertical chrominance phase offset '01'"},
	{R_BA_A_VERT_CHROMA_PHASE_OFF_10,1,
	 "Task A: Vertical chrominance phase offset '10'"},
	{R_BB_A_VERT_CHROMA_PHASE_OFF_11,1,
	 "Task A: Vertical chrominance phase offset '11'"},
	{R_BC_A_VERT_LUMA_PHASE_OFF_00,1,
	 "Task A: Vertical luminance phase offset '00'"},
	{R_BD_A_VERT_LUMA_PHASE_OFF_01,1,
	 "Task A: Vertical luminance phase offset '01'"},
	{R_BE_A_VERT_LUMA_PHASE_OFF_10,1,
	 "Task A: Vertical luminance phase offset '10'"},
	{R_BF_A_VERT_LUMA_PHASE_OFF_11,1,
	 "Task A: Vertical luminance phase offset '11'"},

	/* Task B definition: R_C0_B_TASK_HANDLING_CNTL to R_EF_B_VERT_LUMA_PHASE_OFF_11 */
	/* Task B: Basic settings and acquisition window definition */
	{R_C0_B_TASK_HANDLING_CNTL,1,
	 "Task B: Task handling control"},
	{R_C1_B_X_PORT_FORMATS_AND_CONF,1,
	 "Task B: X port formats and configuration"},
	{R_C2_B_INPUT_REFERENCE_SIGNAL_DEFINITION,1,
	 "Task B: Input reference signal definition"},
	{R_C3_B_I_PORT_FORMATS_AND_CONF,1,
	 "Task B: I port formats and configuration"},
	{R_C4_B_HORIZ_INPUT_WINDOW_START,2,
	 "Task B: Horizontal input window start"},
	{R_C6_B_HORIZ_INPUT_WINDOW_LENGTH,2,
	 "Task B: Horizontal input window length"},
	{R_C8_B_VERT_INPUT_WINDOW_START,2,
	 "Task B: Vertical input window start"},
	{R_CA_B_VERT_INPUT_WINDOW_LENGTH,2,
	 "Task B: Vertical input window length"},
	{R_CC_B_HORIZ_OUTPUT_WINDOW_LENGTH,2,
	 "Task B: Horizontal output window length"},
	{R_CE_B_VERT_OUTPUT_WINDOW_LENGTH,2,
	 "Task B: Vertical output window length"},

	/* Task B: FIR filtering and prescaling */
	{R_D0_B_HORIZ_PRESCALING,1,
	 "Task B: Horizontal prescaling"},
	{R_D1_B_ACCUMULATION_LENGTH,1,
	 "Task B: Accumulation length"},
	{R_D2_B_PRESCALER_DC_GAIN_AND_FIR_PREFILTER,1,
	 "Task B: Prescaler DC gain and FIR prefilter"},
	/* 0xd3 - Reserved */
	{R_D4_B_LUMA_BRIGHTNESS_CNTL,1,
	 "Task B: Luminance brightness control"},
	{R_D5_B_LUMA_CONTRAST_CNTL,1,
	 "Task B: Luminance contrast control"},
	{R_D6_B_CHROMA_SATURATION_CNTL,1,
	 "Task B: Chrominance saturation control"},
	/* 0xd7 - Reserved */

	/* Task B: Horizontal phase scaling */
	{R_D8_B_HORIZ_LUMA_SCALING_INC,2,
	 "Task B: Horizontal luminance scaling increment"},
	{R_DA_B_HORIZ_LUMA_PHASE_OFF,1,
	 "Task B: Horizontal luminance phase offset"},
	/* 0xdb - Reserved */
	{R_DC_B_HORIZ_CHROMA_SCALING,2,
	 "Task B: Horizontal chrominance scaling"},
	{R_DE_B_HORIZ_PHASE_OFFSET_CRHOMA,1,
	 "Task B: Horizontal Phase Offset Chroma"},
	/* 0xdf - Reserved */

	/* Task B: Vertical scaling */
	{R_E0_B_VERT_LUMA_SCALING_INC,2,
	 "Task B: Vertical luminance scaling increment"},
	{R_E2_B_VERT_CHROMA_SCALING_INC,2,
	 "Task B: Vertical chrominance scaling increment"},
	{R_E4_B_VERT_SCALING_MODE_CNTL,1,
	 "Task B: Vertical scaling mode control"},
	/* 0xe5-0xe7 - Reserved */
	{R_E8_B_VERT_CHROMA_PHASE_OFF_00,1,
	 "Task B: Vertical chrominance phase offset '00'"},
	{R_E9_B_VERT_CHROMA_PHASE_OFF_01,1,
	 "Task B: Vertical chrominance phase offset '01'"},
	{R_EA_B_VERT_CHROMA_PHASE_OFF_10,1,
	 "Task B: Vertical chrominance phase offset '10'"},
	{R_EB_B_VERT_CHROMA_PHASE_OFF_11,1,
	 "Task B: Vertical chrominance phase offset '11'"},
	{R_EC_B_VERT_LUMA_PHASE_OFF_00,1,
	 "Task B: Vertical luminance phase offset '00'"},
	{R_ED_B_VERT_LUMA_PHASE_OFF_01,1,
	 "Task B: Vertical luminance phase offset '01'"},
	{R_EE_B_VERT_LUMA_PHASE_OFF_10,1,
	 "Task B: Vertical luminance phase offset '10'"},
	{R_EF_B_VERT_LUMA_PHASE_OFF_11,1,
	 "Task B: Vertical luminance phase offset '11'"},

	/* second PLL (PLL2) and Pulsegenerator Programming */
	{ R_F0_LFCO_PER_LINE, 1,
	  "LFCO's per line"},
	{ R_F1_P_I_PARAM_SELECT,1,
	  "P-/I- Param. Select., PLL Mode, PLL H-Src., LFCO's per line"},
	{ R_F2_NOMINAL_PLL2_DTO,1,
	 "Nominal PLL2 DTO"},
	{R_F3_PLL_INCREMENT,1,
	 "PLL2 Increment"},
	{R_F4_PLL2_STATUS,1,
	 "PLL2 Status"},
	{R_F5_PULSGEN_LINE_LENGTH,1,
	 "Pulsgen. line length"},
	{R_F6_PULSE_A_POS_LSB_AND_PULSEGEN_CONFIG,1,
	 "Pulse A Position, Pulsgen Resync., Pulsgen. H-Src., Pulsgen. line length"},
	{R_F7_PULSE_A_POS_MSB,1,
	 "Pulse A Position"},
	{R_F8_PULSE_B_POS,2,
	 "Pulse B Position"},
	{R_FA_PULSE_C_POS,2,
	 "Pulse C Position"},
	/* 0xfc to 0xfe - Reserved */
	{R_FF_S_PLL_MAX_PHASE_ERR_THRESH_NUM_LINES,1,
	 "S_PLL max. phase, error threshold, PLL2 no. of lines, threshold"},
};
#endif
