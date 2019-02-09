/*
 * Defines for the EMIF driver
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * Benoit Cousson (b-cousson@ti.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __EMIF_H
#define __EMIF_H

/*
 * Maximum number of different frequencies supported by EMIF driver
 * Determines the number of entries in the pointer array for register
 * cache
 */
#define EMIF_MAX_NUM_FREQUENCIES			6

/* State of the core voltage */
#define DDR_VOLTAGE_STABLE				0
#define DDR_VOLTAGE_RAMPING				1

/* Defines for timing De-rating */
#define EMIF_NORMAL_TIMINGS				0
#define EMIF_DERATED_TIMINGS				1

/* Length of the forced read idle period in terms of cycles */
#define EMIF_READ_IDLE_LEN_VAL				5

/*
 * forced read idle interval to be used when voltage
 * is changed as part of DVFS/DPS - 1ms
 */
#define READ_IDLE_INTERVAL_DVFS				(1*1000000)

/*
 * Forced read idle interval to be used when voltage is stable
 * 50us - or maximum value will do
 */
#define READ_IDLE_INTERVAL_NORMAL			(50*1000000)

/* DLL calibration interval when voltage is NOT stable - 1us */
#define DLL_CALIB_INTERVAL_DVFS				(1*1000000)

#define DLL_CALIB_ACK_WAIT_VAL				5

/* Interval between ZQCS commands - hw team recommended value */
#define EMIF_ZQCS_INTERVAL_US				(50*1000)
/* Enable ZQ Calibration on exiting Self-refresh */
#define ZQ_SFEXITEN_ENABLE				1
/*
 * ZQ Calibration simultaneously on both chip-selects:
 * Needs one calibration resistor per CS
 */
#define	ZQ_DUALCALEN_DISABLE				0
#define	ZQ_DUALCALEN_ENABLE				1

#define T_ZQCS_DEFAULT_NS				90
#define T_ZQCL_DEFAULT_NS				360
#define T_ZQINIT_DEFAULT_NS				1000

/* DPD_EN */
#define DPD_DISABLE					0
#define DPD_ENABLE					1

/*
 * Default values for the low-power entry to be used if not provided by user.
 * OMAP4/5 has a hw bug(i735) due to which this value can not be less than 512
 * Timeout values are in DDR clock 'cycles' and frequency threshold in Hz
 */
#define EMIF_LP_MODE_TIMEOUT_PERFORMANCE		2048
#define EMIF_LP_MODE_TIMEOUT_POWER			512
#define EMIF_LP_MODE_FREQ_THRESHOLD			400000000

/* DDR_PHY_CTRL_1 values for EMIF4D - ATTILA PHY combination */
#define EMIF_DDR_PHY_CTRL_1_BASE_VAL_ATTILAPHY		0x049FF000
#define EMIF_DLL_SLAVE_DLY_CTRL_400_MHZ_ATTILAPHY	0x41
#define EMIF_DLL_SLAVE_DLY_CTRL_200_MHZ_ATTILAPHY	0x80
#define EMIF_DLL_SLAVE_DLY_CTRL_100_MHZ_AND_LESS_ATTILAPHY 0xFF

/* DDR_PHY_CTRL_1 values for EMIF4D5 INTELLIPHY combination */
#define EMIF_DDR_PHY_CTRL_1_BASE_VAL_INTELLIPHY		0x0E084200
#define EMIF_PHY_TOTAL_READ_LATENCY_INTELLIPHY_PS	10000

/* TEMP_ALERT_CONFIG - corresponding to temp gradient 5 C/s */
#define TEMP_ALERT_POLL_INTERVAL_DEFAULT_MS		360

#define EMIF_T_CSTA					3
#define EMIF_T_PDLL_UL					128

/* External PHY control registers magic values */
#define EMIF_EXT_PHY_CTRL_1_VAL				0x04020080
#define EMIF_EXT_PHY_CTRL_5_VAL				0x04010040
#define EMIF_EXT_PHY_CTRL_6_VAL				0x01004010
#define EMIF_EXT_PHY_CTRL_7_VAL				0x00001004
#define EMIF_EXT_PHY_CTRL_8_VAL				0x04010040
#define EMIF_EXT_PHY_CTRL_9_VAL				0x01004010
#define EMIF_EXT_PHY_CTRL_10_VAL			0x00001004
#define EMIF_EXT_PHY_CTRL_11_VAL			0x00000000
#define EMIF_EXT_PHY_CTRL_12_VAL			0x00000000
#define EMIF_EXT_PHY_CTRL_13_VAL			0x00000000
#define EMIF_EXT_PHY_CTRL_14_VAL			0x80080080
#define EMIF_EXT_PHY_CTRL_15_VAL			0x00800800
#define EMIF_EXT_PHY_CTRL_16_VAL			0x08102040
#define EMIF_EXT_PHY_CTRL_17_VAL			0x00000001
#define EMIF_EXT_PHY_CTRL_18_VAL			0x540A8150
#define EMIF_EXT_PHY_CTRL_19_VAL			0xA81502A0
#define EMIF_EXT_PHY_CTRL_20_VAL			0x002A0540
#define EMIF_EXT_PHY_CTRL_21_VAL			0x00000000
#define EMIF_EXT_PHY_CTRL_22_VAL			0x00000000
#define EMIF_EXT_PHY_CTRL_23_VAL			0x00000000
#define EMIF_EXT_PHY_CTRL_24_VAL			0x00000077

#define EMIF_INTELLI_PHY_DQS_GATE_OPENING_DELAY_PS	1200

/* Registers offset */
#define EMIF_MODULE_ID_AND_REVISION			0x0000
#define EMIF_STATUS					0x0004
#define EMIF_SDRAM_CONFIG				0x0008
#define EMIF_SDRAM_CONFIG_2				0x000c
#define EMIF_SDRAM_REFRESH_CONTROL			0x0010
#define EMIF_SDRAM_REFRESH_CTRL_SHDW			0x0014
#define EMIF_SDRAM_TIMING_1				0x0018
#define EMIF_SDRAM_TIMING_1_SHDW			0x001c
#define EMIF_SDRAM_TIMING_2				0x0020
#define EMIF_SDRAM_TIMING_2_SHDW			0x0024
#define EMIF_SDRAM_TIMING_3				0x0028
#define EMIF_SDRAM_TIMING_3_SHDW			0x002c
#define EMIF_LPDDR2_NVM_TIMING				0x0030
#define EMIF_LPDDR2_NVM_TIMING_SHDW			0x0034
#define EMIF_POWER_MANAGEMENT_CONTROL			0x0038
#define EMIF_POWER_MANAGEMENT_CTRL_SHDW			0x003c
#define EMIF_LPDDR2_MODE_REG_DATA			0x0040
#define EMIF_LPDDR2_MODE_REG_CONFIG			0x0050
#define EMIF_OCP_CONFIG					0x0054
#define EMIF_OCP_CONFIG_VALUE_1				0x0058
#define EMIF_OCP_CONFIG_VALUE_2				0x005c
#define EMIF_IODFT_TEST_LOGIC_GLOBAL_CONTROL		0x0060
#define EMIF_IODFT_TEST_LOGIC_CTRL_MISR_RESULT		0x0064
#define EMIF_IODFT_TEST_LOGIC_ADDRESS_MISR_RESULT	0x0068
#define EMIF_IODFT_TEST_LOGIC_DATA_MISR_RESULT_1	0x006c
#define EMIF_IODFT_TEST_LOGIC_DATA_MISR_RESULT_2	0x0070
#define EMIF_IODFT_TEST_LOGIC_DATA_MISR_RESULT_3	0x0074
#define EMIF_PERFORMANCE_COUNTER_1			0x0080
#define EMIF_PERFORMANCE_COUNTER_2			0x0084
#define EMIF_PERFORMANCE_COUNTER_CONFIG			0x0088
#define EMIF_PERFORMANCE_COUNTER_MASTER_REGION_SELECT	0x008c
#define EMIF_PERFORMANCE_COUNTER_TIME			0x0090
#define EMIF_MISC_REG					0x0094
#define EMIF_DLL_CALIB_CTRL				0x0098
#define EMIF_DLL_CALIB_CTRL_SHDW			0x009c
#define EMIF_END_OF_INTERRUPT				0x00a0
#define EMIF_SYSTEM_OCP_INTERRUPT_RAW_STATUS		0x00a4
#define EMIF_LL_OCP_INTERRUPT_RAW_STATUS		0x00a8
#define EMIF_SYSTEM_OCP_INTERRUPT_STATUS		0x00ac
#define EMIF_LL_OCP_INTERRUPT_STATUS			0x00b0
#define EMIF_SYSTEM_OCP_INTERRUPT_ENABLE_SET		0x00b4
#define EMIF_LL_OCP_INTERRUPT_ENABLE_SET		0x00b8
#define EMIF_SYSTEM_OCP_INTERRUPT_ENABLE_CLEAR		0x00bc
#define EMIF_LL_OCP_INTERRUPT_ENABLE_CLEAR		0x00c0
#define EMIF_SDRAM_OUTPUT_IMPEDANCE_CALIBRATION_CONFIG	0x00c8
#define EMIF_TEMPERATURE_ALERT_CONFIG			0x00cc
#define EMIF_OCP_ERROR_LOG				0x00d0
#define EMIF_READ_WRITE_LEVELING_RAMP_WINDOW		0x00d4
#define EMIF_READ_WRITE_LEVELING_RAMP_CONTROL		0x00d8
#define EMIF_READ_WRITE_LEVELING_CONTROL		0x00dc
#define EMIF_DDR_PHY_CTRL_1				0x00e4
#define EMIF_DDR_PHY_CTRL_1_SHDW			0x00e8
#define EMIF_DDR_PHY_CTRL_2				0x00ec
#define EMIF_PRIORITY_TO_CLASS_OF_SERVICE_MAPPING	0x0100
#define EMIF_CONNECTION_ID_TO_CLASS_OF_SERVICE_1_MAPPING 0x0104
#define EMIF_CONNECTION_ID_TO_CLASS_OF_SERVICE_2_MAPPING 0x0108
#define EMIF_READ_WRITE_EXECUTION_THRESHOLD		0x0120
#define EMIF_COS_CONFIG					0x0124
#define EMIF_PHY_STATUS_1				0x0140
#define EMIF_PHY_STATUS_2				0x0144
#define EMIF_PHY_STATUS_3				0x0148
#define EMIF_PHY_STATUS_4				0x014c
#define EMIF_PHY_STATUS_5				0x0150
#define EMIF_PHY_STATUS_6				0x0154
#define EMIF_PHY_STATUS_7				0x0158
#define EMIF_PHY_STATUS_8				0x015c
#define EMIF_PHY_STATUS_9				0x0160
#define EMIF_PHY_STATUS_10				0x0164
#define EMIF_PHY_STATUS_11				0x0168
#define EMIF_PHY_STATUS_12				0x016c
#define EMIF_PHY_STATUS_13				0x0170
#define EMIF_PHY_STATUS_14				0x0174
#define EMIF_PHY_STATUS_15				0x0178
#define EMIF_PHY_STATUS_16				0x017c
#define EMIF_PHY_STATUS_17				0x0180
#define EMIF_PHY_STATUS_18				0x0184
#define EMIF_PHY_STATUS_19				0x0188
#define EMIF_PHY_STATUS_20				0x018c
#define EMIF_PHY_STATUS_21				0x0190
#define EMIF_EXT_PHY_CTRL_1				0x0200
#define EMIF_EXT_PHY_CTRL_1_SHDW			0x0204
#define EMIF_EXT_PHY_CTRL_2				0x0208
#define EMIF_EXT_PHY_CTRL_2_SHDW			0x020c
#define EMIF_EXT_PHY_CTRL_3				0x0210
#define EMIF_EXT_PHY_CTRL_3_SHDW			0x0214
#define EMIF_EXT_PHY_CTRL_4				0x0218
#define EMIF_EXT_PHY_CTRL_4_SHDW			0x021c
#define EMIF_EXT_PHY_CTRL_5				0x0220
#define EMIF_EXT_PHY_CTRL_5_SHDW			0x0224
#define EMIF_EXT_PHY_CTRL_6				0x0228
#define EMIF_EXT_PHY_CTRL_6_SHDW			0x022c
#define EMIF_EXT_PHY_CTRL_7				0x0230
#define EMIF_EXT_PHY_CTRL_7_SHDW			0x0234
#define EMIF_EXT_PHY_CTRL_8				0x0238
#define EMIF_EXT_PHY_CTRL_8_SHDW			0x023c
#define EMIF_EXT_PHY_CTRL_9				0x0240
#define EMIF_EXT_PHY_CTRL_9_SHDW			0x0244
#define EMIF_EXT_PHY_CTRL_10				0x0248
#define EMIF_EXT_PHY_CTRL_10_SHDW			0x024c
#define EMIF_EXT_PHY_CTRL_11				0x0250
#define EMIF_EXT_PHY_CTRL_11_SHDW			0x0254
#define EMIF_EXT_PHY_CTRL_12				0x0258
#define EMIF_EXT_PHY_CTRL_12_SHDW			0x025c
#define EMIF_EXT_PHY_CTRL_13				0x0260
#define EMIF_EXT_PHY_CTRL_13_SHDW			0x0264
#define EMIF_EXT_PHY_CTRL_14				0x0268
#define EMIF_EXT_PHY_CTRL_14_SHDW			0x026c
#define EMIF_EXT_PHY_CTRL_15				0x0270
#define EMIF_EXT_PHY_CTRL_15_SHDW			0x0274
#define EMIF_EXT_PHY_CTRL_16				0x0278
#define EMIF_EXT_PHY_CTRL_16_SHDW			0x027c
#define EMIF_EXT_PHY_CTRL_17				0x0280
#define EMIF_EXT_PHY_CTRL_17_SHDW			0x0284
#define EMIF_EXT_PHY_CTRL_18				0x0288
#define EMIF_EXT_PHY_CTRL_18_SHDW			0x028c
#define EMIF_EXT_PHY_CTRL_19				0x0290
#define EMIF_EXT_PHY_CTRL_19_SHDW			0x0294
#define EMIF_EXT_PHY_CTRL_20				0x0298
#define EMIF_EXT_PHY_CTRL_20_SHDW			0x029c
#define EMIF_EXT_PHY_CTRL_21				0x02a0
#define EMIF_EXT_PHY_CTRL_21_SHDW			0x02a4
#define EMIF_EXT_PHY_CTRL_22				0x02a8
#define EMIF_EXT_PHY_CTRL_22_SHDW			0x02ac
#define EMIF_EXT_PHY_CTRL_23				0x02b0
#define EMIF_EXT_PHY_CTRL_23_SHDW			0x02b4
#define EMIF_EXT_PHY_CTRL_24				0x02b8
#define EMIF_EXT_PHY_CTRL_24_SHDW			0x02bc
#define EMIF_EXT_PHY_CTRL_25				0x02c0
#define EMIF_EXT_PHY_CTRL_25_SHDW			0x02c4
#define EMIF_EXT_PHY_CTRL_26				0x02c8
#define EMIF_EXT_PHY_CTRL_26_SHDW			0x02cc
#define EMIF_EXT_PHY_CTRL_27				0x02d0
#define EMIF_EXT_PHY_CTRL_27_SHDW			0x02d4
#define EMIF_EXT_PHY_CTRL_28				0x02d8
#define EMIF_EXT_PHY_CTRL_28_SHDW			0x02dc
#define EMIF_EXT_PHY_CTRL_29				0x02e0
#define EMIF_EXT_PHY_CTRL_29_SHDW			0x02e4
#define EMIF_EXT_PHY_CTRL_30				0x02e8
#define EMIF_EXT_PHY_CTRL_30_SHDW			0x02ec

/* Registers shifts and masks */

/* EMIF_MODULE_ID_AND_REVISION */
#define SCHEME_SHIFT					30
#define SCHEME_MASK					(0x3 << 30)
#define MODULE_ID_SHIFT					16
#define MODULE_ID_MASK					(0xfff << 16)
#define RTL_VERSION_SHIFT				11
#define RTL_VERSION_MASK				(0x1f << 11)
#define MAJOR_REVISION_SHIFT				8
#define MAJOR_REVISION_MASK				(0x7 << 8)
#define MINOR_REVISION_SHIFT				0
#define MINOR_REVISION_MASK				(0x3f << 0)

/* STATUS */
#define BE_SHIFT					31
#define BE_MASK						(1 << 31)
#define DUAL_CLK_MODE_SHIFT				30
#define DUAL_CLK_MODE_MASK				(1 << 30)
#define FAST_INIT_SHIFT					29
#define FAST_INIT_MASK					(1 << 29)
#define RDLVLGATETO_SHIFT				6
#define RDLVLGATETO_MASK				(1 << 6)
#define RDLVLTO_SHIFT					5
#define RDLVLTO_MASK					(1 << 5)
#define WRLVLTO_SHIFT					4
#define WRLVLTO_MASK					(1 << 4)
#define PHY_DLL_READY_SHIFT				2
#define PHY_DLL_READY_MASK				(1 << 2)

/* SDRAM_CONFIG */
#define SDRAM_TYPE_SHIFT				29
#define SDRAM_TYPE_MASK					(0x7 << 29)
#define IBANK_POS_SHIFT					27
#define IBANK_POS_MASK					(0x3 << 27)
#define DDR_TERM_SHIFT					24
#define DDR_TERM_MASK					(0x7 << 24)
#define DDR2_DDQS_SHIFT					23
#define DDR2_DDQS_MASK					(1 << 23)
#define DYN_ODT_SHIFT					21
#define DYN_ODT_MASK					(0x3 << 21)
#define DDR_DISABLE_DLL_SHIFT				20
#define DDR_DISABLE_DLL_MASK				(1 << 20)
#define SDRAM_DRIVE_SHIFT				18
#define SDRAM_DRIVE_MASK				(0x3 << 18)
#define CWL_SHIFT					16
#define CWL_MASK					(0x3 << 16)
#define NARROW_MODE_SHIFT				14
#define NARROW_MODE_MASK				(0x3 << 14)
#define CL_SHIFT					10
#define CL_MASK						(0xf << 10)
#define ROWSIZE_SHIFT					7
#define ROWSIZE_MASK					(0x7 << 7)
#define IBANK_SHIFT					4
#define IBANK_MASK					(0x7 << 4)
#define EBANK_SHIFT					3
#define EBANK_MASK					(1 << 3)
#define PAGESIZE_SHIFT					0
#define PAGESIZE_MASK					(0x7 << 0)

/* SDRAM_CONFIG_2 */
#define CS1NVMEN_SHIFT					30
#define CS1NVMEN_MASK					(1 << 30)
#define EBANK_POS_SHIFT					27
#define EBANK_POS_MASK					(1 << 27)
#define RDBNUM_SHIFT					4
#define RDBNUM_MASK					(0x3 << 4)
#define RDBSIZE_SHIFT					0
#define RDBSIZE_MASK					(0x7 << 0)

/* SDRAM_REFRESH_CONTROL */
#define INITREF_DIS_SHIFT				31
#define INITREF_DIS_MASK				(1 << 31)
#define SRT_SHIFT					29
#define SRT_MASK					(1 << 29)
#define ASR_SHIFT					28
#define ASR_MASK					(1 << 28)
#define PASR_SHIFT					24
#define PASR_MASK					(0x7 << 24)
#define REFRESH_RATE_SHIFT				0
#define REFRESH_RATE_MASK				(0xffff << 0)

/* SDRAM_TIMING_1 */
#define T_RTW_SHIFT					29
#define T_RTW_MASK					(0x7 << 29)
#define T_RP_SHIFT					25
#define T_RP_MASK					(0xf << 25)
#define T_RCD_SHIFT					21
#define T_RCD_MASK					(0xf << 21)
#define T_WR_SHIFT					17
#define T_WR_MASK					(0xf << 17)
#define T_RAS_SHIFT					12
#define T_RAS_MASK					(0x1f << 12)
#define T_RC_SHIFT					6
#define T_RC_MASK					(0x3f << 6)
#define T_RRD_SHIFT					3
#define T_RRD_MASK					(0x7 << 3)
#define T_WTR_SHIFT					0
#define T_WTR_MASK					(0x7 << 0)

/* SDRAM_TIMING_2 */
#define T_XP_SHIFT					28
#define T_XP_MASK					(0x7 << 28)
#define T_ODT_SHIFT					25
#define T_ODT_MASK					(0x7 << 25)
#define T_XSNR_SHIFT					16
#define T_XSNR_MASK					(0x1ff << 16)
#define T_XSRD_SHIFT					6
#define T_XSRD_MASK					(0x3ff << 6)
#define T_RTP_SHIFT					3
#define T_RTP_MASK					(0x7 << 3)
#define T_CKE_SHIFT					0
#define T_CKE_MASK					(0x7 << 0)

/* SDRAM_TIMING_3 */
#define T_PDLL_UL_SHIFT					28
#define T_PDLL_UL_MASK					(0xf << 28)
#define T_CSTA_SHIFT					24
#define T_CSTA_MASK					(0xf << 24)
#define T_CKESR_SHIFT					21
#define T_CKESR_MASK					(0x7 << 21)
#define ZQ_ZQCS_SHIFT					15
#define ZQ_ZQCS_MASK					(0x3f << 15)
#define T_TDQSCKMAX_SHIFT				13
#define T_TDQSCKMAX_MASK				(0x3 << 13)
#define T_RFC_SHIFT					4
#define T_RFC_MASK					(0x1ff << 4)
#define T_RAS_MAX_SHIFT					0
#define T_RAS_MAX_MASK					(0xf << 0)

/* POWER_MANAGEMENT_CONTROL */
#define PD_TIM_SHIFT					12
#define PD_TIM_MASK					(0xf << 12)
#define DPD_EN_SHIFT					11
#define DPD_EN_MASK					(1 << 11)
#define LP_MODE_SHIFT					8
#define LP_MODE_MASK					(0x7 << 8)
#define SR_TIM_SHIFT					4
#define SR_TIM_MASK					(0xf << 4)
#define CS_TIM_SHIFT					0
#define CS_TIM_MASK					(0xf << 0)

/* LPDDR2_MODE_REG_DATA */
#define VALUE_0_SHIFT					0
#define VALUE_0_MASK					(0x7f << 0)

/* LPDDR2_MODE_REG_CONFIG */
#define CS_SHIFT					31
#define CS_MASK						(1 << 31)
#define REFRESH_EN_SHIFT				30
#define REFRESH_EN_MASK					(1 << 30)
#define ADDRESS_SHIFT					0
#define ADDRESS_MASK					(0xff << 0)

/* OCP_CONFIG */
#define SYS_THRESH_MAX_SHIFT				24
#define SYS_THRESH_MAX_MASK				(0xf << 24)
#define MPU_THRESH_MAX_SHIFT				20
#define MPU_THRESH_MAX_MASK				(0xf << 20)
#define LL_THRESH_MAX_SHIFT				16
#define LL_THRESH_MAX_MASK				(0xf << 16)

/* PERFORMANCE_COUNTER_1 */
#define COUNTER1_SHIFT					0
#define COUNTER1_MASK					(0xffffffff << 0)

/* PERFORMANCE_COUNTER_2 */
#define COUNTER2_SHIFT					0
#define COUNTER2_MASK					(0xffffffff << 0)

/* PERFORMANCE_COUNTER_CONFIG */
#define CNTR2_MCONNID_EN_SHIFT				31
#define CNTR2_MCONNID_EN_MASK				(1 << 31)
#define CNTR2_REGION_EN_SHIFT				30
#define CNTR2_REGION_EN_MASK				(1 << 30)
#define CNTR2_CFG_SHIFT					16
#define CNTR2_CFG_MASK					(0xf << 16)
#define CNTR1_MCONNID_EN_SHIFT				15
#define CNTR1_MCONNID_EN_MASK				(1 << 15)
#define CNTR1_REGION_EN_SHIFT				14
#define CNTR1_REGION_EN_MASK				(1 << 14)
#define CNTR1_CFG_SHIFT					0
#define CNTR1_CFG_MASK					(0xf << 0)

/* PERFORMANCE_COUNTER_MASTER_REGION_SELECT */
#define MCONNID2_SHIFT					24
#define MCONNID2_MASK					(0xff << 24)
#define REGION_SEL2_SHIFT				16
#define REGION_SEL2_MASK				(0x3 << 16)
#define MCONNID1_SHIFT					8
#define MCONNID1_MASK					(0xff << 8)
#define REGION_SEL1_SHIFT				0
#define REGION_SEL1_MASK				(0x3 << 0)

/* PERFORMANCE_COUNTER_TIME */
#define TOTAL_TIME_SHIFT				0
#define TOTAL_TIME_MASK					(0xffffffff << 0)

/* DLL_CALIB_CTRL */
#define ACK_WAIT_SHIFT					16
#define ACK_WAIT_MASK					(0xf << 16)
#define DLL_CALIB_INTERVAL_SHIFT			0
#define DLL_CALIB_INTERVAL_MASK				(0x1ff << 0)

/* END_OF_INTERRUPT */
#define EOI_SHIFT					0
#define EOI_MASK					(1 << 0)

/* SYSTEM_OCP_INTERRUPT_RAW_STATUS */
#define DNV_SYS_SHIFT					2
#define DNV_SYS_MASK					(1 << 2)
#define TA_SYS_SHIFT					1
#define TA_SYS_MASK					(1 << 1)
#define ERR_SYS_SHIFT					0
#define ERR_SYS_MASK					(1 << 0)

/* LOW_LATENCY_OCP_INTERRUPT_RAW_STATUS */
#define DNV_LL_SHIFT					2
#define DNV_LL_MASK					(1 << 2)
#define TA_LL_SHIFT					1
#define TA_LL_MASK					(1 << 1)
#define ERR_LL_SHIFT					0
#define ERR_LL_MASK					(1 << 0)

/* SYSTEM_OCP_INTERRUPT_ENABLE_SET */
#define EN_DNV_SYS_SHIFT				2
#define EN_DNV_SYS_MASK					(1 << 2)
#define EN_TA_SYS_SHIFT					1
#define EN_TA_SYS_MASK					(1 << 1)
#define EN_ERR_SYS_SHIFT					0
#define EN_ERR_SYS_MASK					(1 << 0)

/* LOW_LATENCY_OCP_INTERRUPT_ENABLE_SET */
#define EN_DNV_LL_SHIFT					2
#define EN_DNV_LL_MASK					(1 << 2)
#define EN_TA_LL_SHIFT					1
#define EN_TA_LL_MASK					(1 << 1)
#define EN_ERR_LL_SHIFT					0
#define EN_ERR_LL_MASK					(1 << 0)

/* SDRAM_OUTPUT_IMPEDANCE_CALIBRATION_CONFIG */
#define ZQ_CS1EN_SHIFT					31
#define ZQ_CS1EN_MASK					(1 << 31)
#define ZQ_CS0EN_SHIFT					30
#define ZQ_CS0EN_MASK					(1 << 30)
#define ZQ_DUALCALEN_SHIFT				29
#define ZQ_DUALCALEN_MASK				(1 << 29)
#define ZQ_SFEXITEN_SHIFT				28
#define ZQ_SFEXITEN_MASK				(1 << 28)
#define ZQ_ZQINIT_MULT_SHIFT				18
#define ZQ_ZQINIT_MULT_MASK				(0x3 << 18)
#define ZQ_ZQCL_MULT_SHIFT				16
#define ZQ_ZQCL_MULT_MASK				(0x3 << 16)
#define ZQ_REFINTERVAL_SHIFT				0
#define ZQ_REFINTERVAL_MASK				(0xffff << 0)

/* TEMPERATURE_ALERT_CONFIG */
#define TA_CS1EN_SHIFT					31
#define TA_CS1EN_MASK					(1 << 31)
#define TA_CS0EN_SHIFT					30
#define TA_CS0EN_MASK					(1 << 30)
#define TA_SFEXITEN_SHIFT				28
#define TA_SFEXITEN_MASK				(1 << 28)
#define TA_DEVWDT_SHIFT					26
#define TA_DEVWDT_MASK					(0x3 << 26)
#define TA_DEVCNT_SHIFT					24
#define TA_DEVCNT_MASK					(0x3 << 24)
#define TA_REFINTERVAL_SHIFT				0
#define TA_REFINTERVAL_MASK				(0x3fffff << 0)

/* OCP_ERROR_LOG */
#define MADDRSPACE_SHIFT				14
#define MADDRSPACE_MASK					(0x3 << 14)
#define MBURSTSEQ_SHIFT					11
#define MBURSTSEQ_MASK					(0x7 << 11)
#define MCMD_SHIFT					8
#define MCMD_MASK					(0x7 << 8)
#define MCONNID_SHIFT					0
#define MCONNID_MASK					(0xff << 0)

/* DDR_PHY_CTRL_1 - EMIF4D */
#define DLL_SLAVE_DLY_CTRL_SHIFT_4D			4
#define DLL_SLAVE_DLY_CTRL_MASK_4D			(0xFF << 4)
#define READ_LATENCY_SHIFT_4D				0
#define READ_LATENCY_MASK_4D				(0xf << 0)

/* DDR_PHY_CTRL_1 - EMIF4D5 */
#define DLL_HALF_DELAY_SHIFT_4D5			21
#define DLL_HALF_DELAY_MASK_4D5				(1 << 21)
#define READ_LATENCY_SHIFT_4D5				0
#define READ_LATENCY_MASK_4D5				(0x1f << 0)

/* DDR_PHY_CTRL_1_SHDW */
#define DDR_PHY_CTRL_1_SHDW_SHIFT			5
#define DDR_PHY_CTRL_1_SHDW_MASK			(0x7ffffff << 5)
#define READ_LATENCY_SHDW_SHIFT				0
#define READ_LATENCY_SHDW_MASK				(0x1f << 0)

#define EMIF_SRAM_AM33_REG_LAYOUT			0x00000000
#define EMIF_SRAM_AM43_REG_LAYOUT			0x00000001

#ifndef __ASSEMBLY__
/*
 * Structure containing shadow of important registers in EMIF
 * The calculation function fills in this structure to be later used for
 * initialisation and DVFS
 */
struct emif_regs {
	u32 freq;
	u32 ref_ctrl_shdw;
	u32 ref_ctrl_shdw_derated;
	u32 sdram_tim1_shdw;
	u32 sdram_tim1_shdw_derated;
	u32 sdram_tim2_shdw;
	u32 sdram_tim3_shdw;
	u32 sdram_tim3_shdw_derated;
	u32 pwr_mgmt_ctrl_shdw;
	union {
		u32 read_idle_ctrl_shdw_normal;
		u32 dll_calib_ctrl_shdw_normal;
	};
	union {
		u32 read_idle_ctrl_shdw_volt_ramp;
		u32 dll_calib_ctrl_shdw_volt_ramp;
	};

	u32 phy_ctrl_1_shdw;
	u32 ext_phy_ctrl_2_shdw;
	u32 ext_phy_ctrl_3_shdw;
	u32 ext_phy_ctrl_4_shdw;
};

struct ti_emif_pm_functions;

extern unsigned int ti_emif_sram;
extern unsigned int ti_emif_sram_sz;
extern struct ti_emif_pm_data ti_emif_pm_sram_data;
extern struct emif_regs_amx3 ti_emif_regs_amx3;

void ti_emif_save_context(void);
void ti_emif_restore_context(void);
void ti_emif_enter_sr(void);
void ti_emif_exit_sr(void);
void ti_emif_abort_sr(void);

#endif /* __ASSEMBLY__ */
#endif /* __EMIF_H */
