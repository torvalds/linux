/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2011-2013 MaxLinear, Inc. All rights reserved
 *
 * This program may alternatively be licensed under a proprietary license from
 * MaxLinear, Inc.
 *
 */

#ifndef __MXL58X_REGISTERS_H__
#define __MXL58X_REGISTERS_H__

#define HYDRA_INTR_STATUS_REG               0x80030008
#define HYDRA_INTR_MASK_REG                 0x8003000C

#define HYDRA_CRYSTAL_SETTING               0x3FFFC5F0 /* 0 - 24 MHz & 1 - 27 MHz */
#define HYDRA_CRYSTAL_CAP                   0x3FFFEDA4 /* 0 - 24 MHz & 1 - 27 MHz */

#define HYDRA_CPU_RESET_REG                 0x8003003C
#define HYDRA_CPU_RESET_DATA                0x00000400

#define HYDRA_RESET_TRANSPORT_FIFO_REG      0x80030028
#define HYDRA_RESET_TRANSPORT_FIFO_DATA     0x00000000

#define HYDRA_RESET_BBAND_REG               0x80030024
#define HYDRA_RESET_BBAND_DATA              0x00000000

#define HYDRA_RESET_XBAR_REG                0x80030020
#define HYDRA_RESET_XBAR_DATA               0x00000000

#define HYDRA_MODULES_CLK_1_REG             0x80030014
#define HYDRA_DISABLE_CLK_1                 0x00000000

#define HYDRA_MODULES_CLK_2_REG             0x8003001C
#define HYDRA_DISABLE_CLK_2                 0x0000000B

#define HYDRA_PRCM_ROOT_CLK_REG             0x80030018
#define HYDRA_PRCM_ROOT_CLK_DISABLE         0x00000000

#define HYDRA_CPU_RESET_CHECK_REG           0x80030008
#define HYDRA_CPU_RESET_CHECK_OFFSET        0x40000000  /* <bit 30> */

#define HYDRA_SKU_ID_REG                    0x90000190

#define FW_DL_SIGN_ADDR                     0x3FFFEAE0

/* Register to check if FW is running or not */
#define HYDRA_HEAR_BEAT                     0x3FFFEDDC

/* Firmware version */
#define HYDRA_FIRMWARE_VERSION              0x3FFFEDB8
#define HYDRA_FW_RC_VERSION                 0x3FFFCFAC

/* Firmware patch version */
#define HYDRA_FIRMWARE_PATCH_VERSION        0x3FFFEDC2

/* SOC operating temperature in C */
#define HYDRA_TEMPARATURE                   0x3FFFEDB4

/* Demod & Tuner status registers */
/* Demod 0 status base address */
#define HYDRA_DEMOD_0_BASE_ADDR             0x3FFFC64C

/* Tuner 0 status base address */
#define HYDRA_TUNER_0_BASE_ADDR             0x3FFFCE4C

#define POWER_FROM_ADCRSSI_READBACK         0x3FFFEB6C

/* Macros to determine base address of respective demod or tuner */
#define HYDRA_DMD_STATUS_OFFSET(demodID)        ((demodID) * 0x100)
#define HYDRA_TUNER_STATUS_OFFSET(tunerID)      ((tunerID) * 0x40)

/* Demod status address offset from respective demod's base address */
#define HYDRA_DMD_AGC_DIG_LEVEL_ADDR_OFFSET               0x3FFFC64C
#define HYDRA_DMD_LOCK_STATUS_ADDR_OFFSET                 0x3FFFC650
#define HYDRA_DMD_ACQ_STATUS_ADDR_OFFSET                  0x3FFFC654

#define HYDRA_DMD_STANDARD_ADDR_OFFSET                    0x3FFFC658
#define HYDRA_DMD_SPECTRUM_INVERSION_ADDR_OFFSET          0x3FFFC65C
#define HYDRA_DMD_SPECTRUM_ROLL_OFF_ADDR_OFFSET           0x3FFFC660
#define HYDRA_DMD_SYMBOL_RATE_ADDR_OFFSET                 0x3FFFC664
#define HYDRA_DMD_MODULATION_SCHEME_ADDR_OFFSET           0x3FFFC668
#define HYDRA_DMD_FEC_CODE_RATE_ADDR_OFFSET               0x3FFFC66C

#define HYDRA_DMD_SNR_ADDR_OFFSET                         0x3FFFC670
#define HYDRA_DMD_FREQ_OFFSET_ADDR_OFFSET                 0x3FFFC674
#define HYDRA_DMD_CTL_FREQ_OFFSET_ADDR_OFFSET             0x3FFFC678
#define HYDRA_DMD_STR_FREQ_OFFSET_ADDR_OFFSET             0x3FFFC67C
#define HYDRA_DMD_FTL_FREQ_OFFSET_ADDR_OFFSET             0x3FFFC680
#define HYDRA_DMD_STR_NBC_SYNC_LOCK_ADDR_OFFSET           0x3FFFC684
#define HYDRA_DMD_CYCLE_SLIP_COUNT_ADDR_OFFSET            0x3FFFC688

#define HYDRA_DMD_DISPLAY_I_ADDR_OFFSET                   0x3FFFC68C
#define HYDRA_DMD_DISPLAY_Q_ADDR_OFFSET                   0x3FFFC68E

#define HYDRA_DMD_DVBS2_CRC_ERRORS_ADDR_OFFSET            0x3FFFC690
#define HYDRA_DMD_DVBS2_PER_COUNT_ADDR_OFFSET             0x3FFFC694
#define HYDRA_DMD_DVBS2_PER_WINDOW_ADDR_OFFSET            0x3FFFC698

#define HYDRA_DMD_DVBS_CORR_RS_ERRORS_ADDR_OFFSET         0x3FFFC69C
#define HYDRA_DMD_DVBS_UNCORR_RS_ERRORS_ADDR_OFFSET       0x3FFFC6A0
#define HYDRA_DMD_DVBS_BER_COUNT_ADDR_OFFSET              0x3FFFC6A4
#define HYDRA_DMD_DVBS_BER_WINDOW_ADDR_OFFSET             0x3FFFC6A8

/* Debug-purpose DVB-S DMD 0 */
#define HYDRA_DMD_DVBS_1ST_CORR_RS_ERRORS_ADDR_OFFSET     0x3FFFC6C8  /* corrected RS Errors: 1st iteration */
#define HYDRA_DMD_DVBS_1ST_UNCORR_RS_ERRORS_ADDR_OFFSET   0x3FFFC6CC  /* uncorrected RS Errors: 1st iteration */
#define HYDRA_DMD_DVBS_BER_COUNT_1ST_ADDR_OFFSET          0x3FFFC6D0
#define HYDRA_DMD_DVBS_BER_WINDOW_1ST_ADDR_OFFSET         0x3FFFC6D4

#define HYDRA_DMD_TUNER_ID_ADDR_OFFSET                    0x3FFFC6AC
#define HYDRA_DMD_DVBS2_PILOT_ON_OFF_ADDR_OFFSET          0x3FFFC6B0
#define HYDRA_DMD_FREQ_SEARCH_RANGE_KHZ_ADDR_OFFSET       0x3FFFC6B4
#define HYDRA_DMD_STATUS_LOCK_ADDR_OFFSET                 0x3FFFC6B8
#define HYDRA_DMD_STATUS_CENTER_FREQ_IN_KHZ_ADDR          0x3FFFC704
#define HYDRA_DMD_STATUS_INPUT_POWER_ADDR                 0x3FFFC708

/* DVB-S new scaled_BER_count for a new BER API, see HYDRA-1343 "DVB-S post viterbi information" */
#define DMD0_STATUS_DVBS_1ST_SCALED_BER_COUNT_ADDR        0x3FFFC710 /* DMD 0: 1st iteration BER count scaled by HYDRA_BER_COUNT_SCALING_FACTOR */
#define DMD0_STATUS_DVBS_SCALED_BER_COUNT_ADDR            0x3FFFC714 /* DMD 0: 2nd iteration BER count scaled by HYDRA_BER_COUNT_SCALING_FACTOR */

#define DMD0_SPECTRUM_MIN_GAIN_STATUS                     0x3FFFC73C
#define DMD0_SPECTRUM_MIN_GAIN_WB_SAGC_VALUE              0x3FFFC740
#define DMD0_SPECTRUM_MIN_GAIN_NB_SAGC_VALUE              0x3FFFC744

#define HYDRA_DMD_STATUS_END_ADDR_OFFSET                  0x3FFFC748

/* Tuner status address offset from respective tuners's base address */
#define HYDRA_TUNER_DEMOD_ID_ADDR_OFFSET                  0x3FFFCE4C
#define HYDRA_TUNER_AGC_LOCK_OFFSET                       0x3FFFCE50
#define HYDRA_TUNER_SPECTRUM_STATUS_OFFSET                0x3FFFCE54
#define HYDRA_TUNER_SPECTRUM_BIN_SIZE_OFFSET              0x3FFFCE58
#define HYDRA_TUNER_SPECTRUM_ADDRESS_OFFSET               0x3FFFCE5C
#define HYDRA_TUNER_ENABLE_COMPLETE                       0x3FFFEB78

#define HYDRA_DEMOD_STATUS_LOCK(devId, demodId)   write_register(devId, (HYDRA_DMD_STATUS_LOCK_ADDR_OFFSET + HYDRA_DMD_STATUS_OFFSET(demodId)), MXL_YES)
#define HYDRA_DEMOD_STATUS_UNLOCK(devId, demodId) write_register(devId, (HYDRA_DMD_STATUS_LOCK_ADDR_OFFSET + HYDRA_DMD_STATUS_OFFSET(demodId)), MXL_NO)

#define HYDRA_VERSION                                     0x3FFFEDB8
#define HYDRA_DEMOD0_VERSION                              0x3FFFEDBC
#define HYDRA_DEMOD1_VERSION                              0x3FFFEDC0
#define HYDRA_DEMOD2_VERSION                              0x3FFFEDC4
#define HYDRA_DEMOD3_VERSION                              0x3FFFEDC8
#define HYDRA_DEMOD4_VERSION                              0x3FFFEDCC
#define HYDRA_DEMOD5_VERSION                              0x3FFFEDD0
#define HYDRA_DEMOD6_VERSION                              0x3FFFEDD4
#define HYDRA_DEMOD7_VERSION                              0x3FFFEDD8
#define HYDRA_HEAR_BEAT                                   0x3FFFEDDC
#define HYDRA_SKU_MGMT                                    0x3FFFEBC0

#define MXL_HYDRA_FPGA_A_ADDRESS                          0x91C00000
#define MXL_HYDRA_FPGA_B_ADDRESS                          0x91D00000

/* TS control base address */
#define HYDRA_TS_CTRL_BASE_ADDR                           0x90700000

#define MPEG_MUX_MODE_SLICE0_REG            (HYDRA_TS_CTRL_BASE_ADDR + 0x08)

#define MPEG_MUX_MODE_SLICE1_REG            (HYDRA_TS_CTRL_BASE_ADDR + 0x08)

#define PID_BANK_SEL_SLICE0_REG             (HYDRA_TS_CTRL_BASE_ADDR + 0x190)
#define PID_BANK_SEL_SLICE1_REG             (HYDRA_TS_CTRL_BASE_ADDR + 0x1B0)

#define MPEG_CLK_GATED_REG                  (HYDRA_TS_CTRL_BASE_ADDR + 0x20)

#define MPEG_CLK_ALWAYS_ON_REG              (HYDRA_TS_CTRL_BASE_ADDR + 0x1D4)

#define HYDRA_REGULAR_PID_BANK_A_REG        (HYDRA_TS_CTRL_BASE_ADDR + 0x190)

#define HYDRA_FIXED_PID_BANK_A_REG          (HYDRA_TS_CTRL_BASE_ADDR + 0x190)

#define HYDRA_REGULAR_PID_BANK_B_REG        (HYDRA_TS_CTRL_BASE_ADDR + 0x1B0)

#define HYDRA_FIXED_PID_BANK_B_REG          (HYDRA_TS_CTRL_BASE_ADDR + 0x1B0)

#define FIXED_PID_TBL_REG_ADDRESS_0         (HYDRA_TS_CTRL_BASE_ADDR + 0x9000)
#define FIXED_PID_TBL_REG_ADDRESS_1         (HYDRA_TS_CTRL_BASE_ADDR + 0x9100)
#define FIXED_PID_TBL_REG_ADDRESS_2         (HYDRA_TS_CTRL_BASE_ADDR + 0x9200)
#define FIXED_PID_TBL_REG_ADDRESS_3         (HYDRA_TS_CTRL_BASE_ADDR + 0x9300)

#define FIXED_PID_TBL_REG_ADDRESS_4         (HYDRA_TS_CTRL_BASE_ADDR + 0xB000)
#define FIXED_PID_TBL_REG_ADDRESS_5         (HYDRA_TS_CTRL_BASE_ADDR + 0xB100)
#define FIXED_PID_TBL_REG_ADDRESS_6         (HYDRA_TS_CTRL_BASE_ADDR + 0xB200)
#define FIXED_PID_TBL_REG_ADDRESS_7         (HYDRA_TS_CTRL_BASE_ADDR + 0xB300)

#define REGULAR_PID_TBL_REG_ADDRESS_0       (HYDRA_TS_CTRL_BASE_ADDR + 0x8000)
#define REGULAR_PID_TBL_REG_ADDRESS_1       (HYDRA_TS_CTRL_BASE_ADDR + 0x8200)
#define REGULAR_PID_TBL_REG_ADDRESS_2       (HYDRA_TS_CTRL_BASE_ADDR + 0x8400)
#define REGULAR_PID_TBL_REG_ADDRESS_3       (HYDRA_TS_CTRL_BASE_ADDR + 0x8600)

#define REGULAR_PID_TBL_REG_ADDRESS_4       (HYDRA_TS_CTRL_BASE_ADDR + 0xA000)
#define REGULAR_PID_TBL_REG_ADDRESS_5       (HYDRA_TS_CTRL_BASE_ADDR + 0xA200)
#define REGULAR_PID_TBL_REG_ADDRESS_6       (HYDRA_TS_CTRL_BASE_ADDR + 0xA400)
#define REGULAR_PID_TBL_REG_ADDRESS_7       (HYDRA_TS_CTRL_BASE_ADDR + 0xA600)

/***************************************************************************/

#define PAD_MUX_GPIO_00_SYNC_BASEADDR                          0x90000188


#define PAD_MUX_UART_RX_C_PINMUX_BASEADDR 0x9000001C

#define   XPT_PACKET_GAP_MIN_BASEADDR                            0x90700044
#define   XPT_NCO_COUNT_BASEADDR                                 0x90700238

#define   XPT_NCO_COUNT_BASEADDR1                                0x9070023C

/* V2 DigRF status register */

#define   XPT_PID_BASEADDR                                       0x90708000

#define   XPT_PID_REMAP_BASEADDR                                 0x90708004

#define   XPT_KNOWN_PID_BASEADDR                                 0x90709000

#define   XPT_PID_BASEADDR1                                      0x9070A000

#define   XPT_PID_REMAP_BASEADDR1                                0x9070A004

#define   XPT_KNOWN_PID_BASEADDR1                                0x9070B000

#define   XPT_BERT_LOCK_BASEADDR                                 0x907000B8

#define   XPT_BERT_BASEADDR                                      0x907000BC

#define   XPT_BERT_INVERT_BASEADDR                               0x907000C0

#define   XPT_BERT_HEADER_BASEADDR                               0x907000C4

#define   XPT_BERT_BASEADDR1                                     0x907000C8

#define   XPT_BERT_BIT_COUNT0_BASEADDR                           0x907000CC

#define   XPT_BERT_BIT_COUNT0_BASEADDR1                          0x907000D0

#define   XPT_BERT_BIT_COUNT1_BASEADDR                           0x907000D4

#define   XPT_BERT_BIT_COUNT1_BASEADDR1                          0x907000D8

#define   XPT_BERT_BIT_COUNT2_BASEADDR                           0x907000DC

#define   XPT_BERT_BIT_COUNT2_BASEADDR1                          0x907000E0

#define   XPT_BERT_BIT_COUNT3_BASEADDR                           0x907000E4

#define   XPT_BERT_BIT_COUNT3_BASEADDR1                          0x907000E8

#define   XPT_BERT_BIT_COUNT4_BASEADDR                           0x907000EC

#define   XPT_BERT_BIT_COUNT4_BASEADDR1                          0x907000F0

#define   XPT_BERT_BIT_COUNT5_BASEADDR                           0x907000F4

#define   XPT_BERT_BIT_COUNT5_BASEADDR1                          0x907000F8

#define   XPT_BERT_BIT_COUNT6_BASEADDR                           0x907000FC

#define   XPT_BERT_BIT_COUNT6_BASEADDR1                          0x90700100

#define   XPT_BERT_BIT_COUNT7_BASEADDR                           0x90700104

#define   XPT_BERT_BIT_COUNT7_BASEADDR1                          0x90700108

#define   XPT_BERT_ERR_COUNT0_BASEADDR                           0x9070010C

#define   XPT_BERT_ERR_COUNT0_BASEADDR1                          0x90700110

#define   XPT_BERT_ERR_COUNT1_BASEADDR                           0x90700114

#define   XPT_BERT_ERR_COUNT1_BASEADDR1                          0x90700118

#define   XPT_BERT_ERR_COUNT2_BASEADDR                           0x9070011C

#define   XPT_BERT_ERR_COUNT2_BASEADDR1                          0x90700120

#define   XPT_BERT_ERR_COUNT3_BASEADDR                           0x90700124

#define   XPT_BERT_ERR_COUNT3_BASEADDR1                          0x90700128

#define   XPT_BERT_ERR_COUNT4_BASEADDR                           0x9070012C

#define   XPT_BERT_ERR_COUNT4_BASEADDR1                          0x90700130

#define   XPT_BERT_ERR_COUNT5_BASEADDR                           0x90700134

#define   XPT_BERT_ERR_COUNT5_BASEADDR1                          0x90700138

#define   XPT_BERT_ERR_COUNT6_BASEADDR                           0x9070013C

#define   XPT_BERT_ERR_COUNT6_BASEADDR1                          0x90700140

#define   XPT_BERT_ERR_COUNT7_BASEADDR                           0x90700144

#define   XPT_BERT_ERR_COUNT7_BASEADDR1                          0x90700148

#define   XPT_BERT_ERROR_BASEADDR                                0x9070014C

#define   XPT_BERT_ANALYZER_BASEADDR                             0x90700150

#define   XPT_BERT_ANALYZER_BASEADDR1                            0x90700154

#define   XPT_BERT_ANALYZER_BASEADDR2                            0x90700158

#define   XPT_BERT_ANALYZER_BASEADDR3                            0x9070015C

#define   XPT_BERT_ANALYZER_BASEADDR4                            0x90700160

#define   XPT_BERT_ANALYZER_BASEADDR5                            0x90700164

#define   XPT_BERT_ANALYZER_BASEADDR6                            0x90700168

#define   XPT_BERT_ANALYZER_BASEADDR7                            0x9070016C

#define   XPT_BERT_ANALYZER_BASEADDR8                            0x90700170

#define   XPT_BERT_ANALYZER_BASEADDR9                            0x90700174

#define   XPT_DMD0_BASEADDR                                      0x9070024C

/* V2 AGC Gain Freeze & step */
#define   DBG_ENABLE_DISABLE_AGC                                 (0x3FFFCF60) /* 1: DISABLE, 0:ENABLE */
#define   WB_DFE0_DFE_FB_RF1_BASEADDR                            0x903004A4

#define   WB_DFE1_DFE_FB_RF1_BASEADDR                            0x904004A4

#define   WB_DFE2_DFE_FB_RF1_BASEADDR                            0x905004A4

#define   WB_DFE3_DFE_FB_RF1_BASEADDR                            0x906004A4

#define   AFE_REG_D2A_TA_RFFE_LNA_BO_1P8_BASEADDR                0x90200104

#define   AFE_REG_AFE_REG_SPARE_BASEADDR                         0x902000A0

#define   AFE_REG_AFE_REG_SPARE_BASEADDR1                        0x902000B4

#define   AFE_REG_AFE_REG_SPARE_BASEADDR2                        0x902000C4

#define   AFE_REG_AFE_REG_SPARE_BASEADDR3                        0x902000D4

#define   WB_DFE0_DFE_FB_AGC_BASEADDR                            0x90300498

#define   WB_DFE1_DFE_FB_AGC_BASEADDR                            0x90400498

#define   WB_DFE2_DFE_FB_AGC_BASEADDR                            0x90500498

#define   WB_DFE3_DFE_FB_AGC_BASEADDR                            0x90600498

#define   WDT_WD_INT_BASEADDR                                    0x8002000C

#define   FSK_TX_FTM_BASEADDR                                    0x80090000

#define   FSK_TX_FTM_TX_CNT_BASEADDR                             0x80090018

#define   AFE_REG_D2A_FSK_BIAS_BASEADDR                          0x90200040

#define   DMD_TEI_BASEADDR                                       0x3FFFEBE0

#endif /* __MXL58X_REGISTERS_H__ */
