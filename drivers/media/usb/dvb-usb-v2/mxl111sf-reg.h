/*
 *  mxl111sf-reg.h - driver for the MaxLinear MXL111SF
 *
 *  Copyright (C) 2010-2014 Michael Krufky <mkrufky@linuxtv.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _DVB_USB_MXL111SF_REG_H_
#define _DVB_USB_MXL111SF_REG_H_

#define CHIP_ID_REG                  0xFC
#define TOP_CHIP_REV_ID_REG          0xFA

#define V6_SNR_RB_LSB_REG            0x27
#define V6_SNR_RB_MSB_REG            0x28

#define V6_N_ACCUMULATE_REG          0x11
#define V6_RS_AVG_ERRORS_LSB_REG     0x2C
#define V6_RS_AVG_ERRORS_MSB_REG     0x2D

#define V6_IRQ_STATUS_REG            0x24
#define  IRQ_MASK_FEC_LOCK       0x10

#define V6_SYNC_LOCK_REG             0x28
#define SYNC_LOCK_MASK           0x10

#define V6_RS_LOCK_DET_REG           0x28
#define  RS_LOCK_DET_MASK        0x08

#define V6_INITACQ_NODETECT_REG    0x20
#define V6_FORCE_NFFT_CPSIZE_REG   0x20

#define V6_CODE_RATE_TPS_REG       0x29
#define V6_CODE_RATE_TPS_MASK      0x07


#define V6_CP_LOCK_DET_REG        0x28
#define V6_CP_LOCK_DET_MASK       0x04

#define V6_TPS_HIERACHY_REG        0x29
#define V6_TPS_HIERARCHY_INFO_MASK  0x40

#define V6_MODORDER_TPS_REG        0x2A
#define V6_PARAM_CONSTELLATION_MASK   0x30

#define V6_MODE_TPS_REG            0x2A
#define V6_PARAM_FFT_MODE_MASK        0x0C


#define V6_CP_TPS_REG             0x29
#define V6_PARAM_GI_MASK              0x30

#define V6_TPS_LOCK_REG           0x2A
#define V6_PARAM_TPS_LOCK_MASK        0x40

#define V6_FEC_PER_COUNT_REG      0x2E
#define V6_FEC_PER_SCALE_REG      0x2B
#define V6_FEC_PER_SCALE_MASK        0x03
#define V6_FEC_PER_CLR_REG        0x20
#define V6_FEC_PER_CLR_MASK          0x01

#define V6_PIN_MUX_MODE_REG       0x1B
#define V6_ENABLE_PIN_MUX            0x1E

#define V6_I2S_NUM_SAMPLES_REG    0x16

#define V6_MPEG_IN_CLK_INV_REG    0x17
#define V6_MPEG_IN_CTRL_REG       0x18

#define V6_INVERTED_CLK_PHASE       0x20
#define V6_MPEG_IN_DATA_PARALLEL    0x01
#define V6_MPEG_IN_DATA_SERIAL      0x02

#define V6_INVERTED_MPEG_SYNC       0x04
#define V6_INVERTED_MPEG_VALID      0x08

#define TSIF_INPUT_PARALLEL         0
#define TSIF_INPUT_SERIAL           1
#define TSIF_NORMAL                 0

#define V6_MPEG_INOUT_BIT_ORDER_CTRL_REG  0x19
#define V6_MPEG_SER_MSB_FIRST                0x80
#define MPEG_SER_MSB_FIRST_ENABLED        0x01

#define V6_656_I2S_BUFF_STATUS_REG   0x2F
#define V6_656_OVERFLOW_MASK_BIT         0x08
#define V6_I2S_OVERFLOW_MASK_BIT         0x01

#define V6_I2S_STREAM_START_BIT_REG  0x14
#define V6_I2S_STREAM_END_BIT_REG    0x15
#define I2S_RIGHT_JUSTIFIED     0
#define I2S_LEFT_JUSTIFIED      1
#define I2S_DATA_FORMAT         2

#define V6_TUNER_LOOP_THRU_CONTROL_REG  0x09
#define V6_ENABLE_LOOP_THRU               0x01

#define TOTAL_NUM_IF_OUTPUT_FREQ       16

#define TUNER_NORMAL_IF_SPECTRUM       0x0
#define TUNER_INVERT_IF_SPECTRUM       0x10

#define V6_TUNER_IF_SEL_REG              0x06
#define V6_TUNER_IF_FCW_REG              0x3C
#define V6_TUNER_IF_FCW_BYP_REG          0x3D
#define V6_RF_LOCK_STATUS_REG            0x23

#define NUM_DIG_TV_CHANNEL     1000

#define V6_DIG_CLK_FREQ_SEL_REG  0x07
#define V6_REF_SYNTH_INT_REG     0x5C
#define V6_REF_SYNTH_REMAIN_REG  0x58
#define V6_DIG_RFREFSELECT_REG   0x32
#define V6_XTAL_CLK_OUT_GAIN_REG   0x31
#define V6_TUNER_LOOP_THRU_CTRL_REG      0x09
#define V6_DIG_XTAL_ENABLE_REG  0x06
#define V6_DIG_XTAL_BIAS_REG  0x66
#define V6_XTAL_CAP_REG    0x08

#define V6_GPO_CTRL_REG     0x18
#define MXL_GPO_0           0x00
#define MXL_GPO_1           0x01
#define V6_GPO_0_MASK       0x10
#define V6_GPO_1_MASK       0x20

#define V6_111SF_GPO_CTRL_REG     0x19
#define MXL_111SF_GPO_1               0x00
#define MXL_111SF_GPO_2               0x01
#define MXL_111SF_GPO_3               0x02
#define MXL_111SF_GPO_4               0x03
#define MXL_111SF_GPO_5               0x04
#define MXL_111SF_GPO_6               0x05
#define MXL_111SF_GPO_7               0x06

#define MXL_111SF_GPO_0_MASK          0x01
#define MXL_111SF_GPO_1_MASK          0x02
#define MXL_111SF_GPO_2_MASK          0x04
#define MXL_111SF_GPO_3_MASK          0x08
#define MXL_111SF_GPO_4_MASK          0x10
#define MXL_111SF_GPO_5_MASK          0x20
#define MXL_111SF_GPO_6_MASK          0x40

#define V6_ATSC_CONFIG_REG  0x0A

#define MXL_MODE_REG    0x03
#define START_TUNE_REG  0x1C

#define V6_IDAC_HYSTERESIS_REG    0x0B
#define V6_IDAC_SETTINGS_REG      0x0C
#define IDAC_MANUAL_CONTROL             1
#define IDAC_CURRENT_SINKING_ENABLE     1
#define IDAC_MANUAL_CONTROL_BIT_MASK      0x80
#define IDAC_CURRENT_SINKING_BIT_MASK     0x40

#define V8_SPI_MODE_REG  0xE9

#define V6_DIG_RF_PWR_LSB_REG  0x46
#define V6_DIG_RF_PWR_MSB_REG  0x47

#endif /* _DVB_USB_MXL111SF_REG_H_ */
