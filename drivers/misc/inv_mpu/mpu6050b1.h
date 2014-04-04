/*
	$License:
	Copyright (C) 2011 InvenSense Corporation, All Rights Reserved.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
	$
 */

/**
 * @defgroup
 * @brief
 *
 * @{
 *      @file     mpu6050.h
 *      @brief
 */

#ifndef __MPU_H_
#error Do not include this file directly.  Include mpu.h instead.
#endif

#ifndef __MPU6050B1_H_
#define __MPU6050B1_H_


#define MPU_NAME "mpu6050B1"
#define DEFAULT_MPU_SLAVEADDR		0x68

/*==== MPU6050B1 REGISTER SET ====*/
enum {
	MPUREG_XG_OFFS_TC = 0,			/* 0x00,   0 */
	MPUREG_YG_OFFS_TC,			/* 0x01,   1 */
	MPUREG_ZG_OFFS_TC,			/* 0x02,   2 */
	MPUREG_X_FINE_GAIN,			/* 0x03,   3 */
	MPUREG_Y_FINE_GAIN,			/* 0x04,   4 */
	MPUREG_Z_FINE_GAIN,			/* 0x05,   5 */
	MPUREG_XA_OFFS_H,			/* 0x06,   6 */
	MPUREG_XA_OFFS_L,			/* 0x07,   7 */
	MPUREG_YA_OFFS_H,			/* 0x08,   8 */
	MPUREG_YA_OFFS_L,			/* 0x09,   9 */
	MPUREG_ZA_OFFS_H,			/* 0x0a,  10 */
	MPUREG_ZA_OFFS_L,			/* 0x0B,  11 */
	MPUREG_PRODUCT_ID,			/* 0x0c,  12 */
	MPUREG_0D_RSVD,				/* 0x0d,  13 */
	MPUREG_0E_RSVD,				/* 0x0e,  14 */
	MPUREG_0F_RSVD,				/* 0x0f,  15 */
	MPUREG_10_RSVD,				/* 0x00,  16 */
	MPUREG_11_RSVD,				/* 0x11,  17 */
	MPUREG_12_RSVD,				/* 0x12,  18 */
	MPUREG_XG_OFFS_USRH,			/* 0x13,  19 */
	MPUREG_XG_OFFS_USRL,			/* 0x14,  20 */
	MPUREG_YG_OFFS_USRH,			/* 0x15,  21 */
	MPUREG_YG_OFFS_USRL,			/* 0x16,  22 */
	MPUREG_ZG_OFFS_USRH,			/* 0x17,  23 */
	MPUREG_ZG_OFFS_USRL,			/* 0x18,  24 */
	MPUREG_SMPLRT_DIV,			/* 0x19,  25 */
	MPUREG_CONFIG,				/* 0x1A,  26 */
	MPUREG_GYRO_CONFIG,			/* 0x1b,  27 */
	MPUREG_ACCEL_CONFIG,			/* 0x1c,  28 */
	MPUREG_ACCEL_FF_THR,			/* 0x1d,  29 */
	MPUREG_ACCEL_FF_DUR,			/* 0x1e,  30 */
	MPUREG_ACCEL_MOT_THR,			/* 0x1f,  31 */
	MPUREG_ACCEL_MOT_DUR,			/* 0x20,  32 */
	MPUREG_ACCEL_ZRMOT_THR,			/* 0x21,  33 */
	MPUREG_ACCEL_ZRMOT_DUR,			/* 0x22,  34 */
	MPUREG_FIFO_EN,				/* 0x23,  35 */
	MPUREG_I2C_MST_CTRL,			/* 0x24,  36 */
	MPUREG_I2C_SLV0_ADDR,			/* 0x25,  37 */
	MPUREG_I2C_SLV0_REG,			/* 0x26,  38 */
	MPUREG_I2C_SLV0_CTRL,			/* 0x27,  39 */
	MPUREG_I2C_SLV1_ADDR,			/* 0x28,  40 */
	MPUREG_I2C_SLV1_REG,			/* 0x29,  41 */
	MPUREG_I2C_SLV1_CTRL,			/* 0x2a,  42 */
	MPUREG_I2C_SLV2_ADDR,			/* 0x2B,  43 */
	MPUREG_I2C_SLV2_REG,			/* 0x2c,  44 */
	MPUREG_I2C_SLV2_CTRL,			/* 0x2d,  45 */
	MPUREG_I2C_SLV3_ADDR,			/* 0x2E,  46 */
	MPUREG_I2C_SLV3_REG,			/* 0x2f,  47 */
	MPUREG_I2C_SLV3_CTRL,			/* 0x30,  48 */
	MPUREG_I2C_SLV4_ADDR,			/* 0x31,  49 */
	MPUREG_I2C_SLV4_REG,			/* 0x32,  50 */
	MPUREG_I2C_SLV4_DO,			/* 0x33,  51 */
	MPUREG_I2C_SLV4_CTRL,			/* 0x34,  52 */
	MPUREG_I2C_SLV4_DI,			/* 0x35,  53 */
	MPUREG_I2C_MST_STATUS,			/* 0x36,  54 */
	MPUREG_INT_PIN_CFG,			/* 0x37,  55 */
	MPUREG_INT_ENABLE,			/* 0x38,  56 */
	MPUREG_DMP_INT_STATUS,			/* 0x39,  57 */
	MPUREG_INT_STATUS,			/* 0x3A,  58 */
	MPUREG_ACCEL_XOUT_H,			/* 0x3B,  59 */
	MPUREG_ACCEL_XOUT_L,			/* 0x3c,  60 */
	MPUREG_ACCEL_YOUT_H,			/* 0x3d,  61 */
	MPUREG_ACCEL_YOUT_L,			/* 0x3e,  62 */
	MPUREG_ACCEL_ZOUT_H,			/* 0x3f,  63 */
	MPUREG_ACCEL_ZOUT_L,			/* 0x40,  64 */
	MPUREG_TEMP_OUT_H,			/* 0x41,  65 */
	MPUREG_TEMP_OUT_L,			/* 0x42,  66 */
	MPUREG_GYRO_XOUT_H,			/* 0x43,  67 */
	MPUREG_GYRO_XOUT_L,			/* 0x44,  68 */
	MPUREG_GYRO_YOUT_H,			/* 0x45,  69 */
	MPUREG_GYRO_YOUT_L,			/* 0x46,  70 */
	MPUREG_GYRO_ZOUT_H,			/* 0x47,  71 */
	MPUREG_GYRO_ZOUT_L,			/* 0x48,  72 */
	MPUREG_EXT_SLV_SENS_DATA_00,		/* 0x49,  73 */
	MPUREG_EXT_SLV_SENS_DATA_01,		/* 0x4a,  74 */
	MPUREG_EXT_SLV_SENS_DATA_02,		/* 0x4b,  75 */
	MPUREG_EXT_SLV_SENS_DATA_03,		/* 0x4c,  76 */
	MPUREG_EXT_SLV_SENS_DATA_04,		/* 0x4d,  77 */
	MPUREG_EXT_SLV_SENS_DATA_05,		/* 0x4e,  78 */
	MPUREG_EXT_SLV_SENS_DATA_06,		/* 0x4F,  79 */
	MPUREG_EXT_SLV_SENS_DATA_07,		/* 0x50,  80 */
	MPUREG_EXT_SLV_SENS_DATA_08,		/* 0x51,  81 */
	MPUREG_EXT_SLV_SENS_DATA_09,		/* 0x52,  82 */
	MPUREG_EXT_SLV_SENS_DATA_10,		/* 0x53,  83 */
	MPUREG_EXT_SLV_SENS_DATA_11,		/* 0x54,  84 */
	MPUREG_EXT_SLV_SENS_DATA_12,		/* 0x55,  85 */
	MPUREG_EXT_SLV_SENS_DATA_13,		/* 0x56,  86 */
	MPUREG_EXT_SLV_SENS_DATA_14,		/* 0x57,  87 */
	MPUREG_EXT_SLV_SENS_DATA_15,		/* 0x58,  88 */
	MPUREG_EXT_SLV_SENS_DATA_16,		/* 0x59,  89 */
	MPUREG_EXT_SLV_SENS_DATA_17,		/* 0x5a,  90 */
	MPUREG_EXT_SLV_SENS_DATA_18,		/* 0x5B,  91 */
	MPUREG_EXT_SLV_SENS_DATA_19,		/* 0x5c,  92 */
	MPUREG_EXT_SLV_SENS_DATA_20,		/* 0x5d,  93 */
	MPUREG_EXT_SLV_SENS_DATA_21,		/* 0x5e,  94 */
	MPUREG_EXT_SLV_SENS_DATA_22,		/* 0x5f,  95 */
	MPUREG_EXT_SLV_SENS_DATA_23,		/* 0x60,  96 */
	MPUREG_ACCEL_INTEL_STATUS,		/* 0x61,  97 */
	MPUREG_62_RSVD,				/* 0x62,  98 */
	MPUREG_I2C_SLV0_DO,			/* 0x63,  99 */
	MPUREG_I2C_SLV1_DO,			/* 0x64, 100 */
	MPUREG_I2C_SLV2_DO,			/* 0x65, 101 */
	MPUREG_I2C_SLV3_DO,			/* 0x66, 102 */
	MPUREG_I2C_MST_DELAY_CTRL,		/* 0x67, 103 */
	MPUREG_SIGNAL_PATH_RESET,		/* 0x68, 104 */
	MPUREG_ACCEL_INTEL_CTRL,		/* 0x69, 105 */
	MPUREG_USER_CTRL,			/* 0x6A, 106 */
	MPUREG_PWR_MGMT_1,			/* 0x6B, 107 */
	MPUREG_PWR_MGMT_2,			/* 0x6C, 108 */
	MPUREG_BANK_SEL,			/* 0x6D, 109 */
	MPUREG_MEM_START_ADDR,			/* 0x6E, 100 */
	MPUREG_MEM_R_W,				/* 0x6F, 111 */
	MPUREG_DMP_CFG_1,			/* 0x70, 112 */
	MPUREG_DMP_CFG_2,			/* 0x71, 113 */
	MPUREG_FIFO_COUNTH,			/* 0x72, 114 */
	MPUREG_FIFO_COUNTL,			/* 0x73, 115 */
	MPUREG_FIFO_R_W,			/* 0x74, 116 */
	MPUREG_WHOAMI,				/* 0x75, 117 */

	NUM_OF_MPU_REGISTERS			/* = 0x76, 118 */
};

/*==== MPU6050B1 MEMORY ====*/
enum MPU_MEMORY_BANKS {
	MEM_RAM_BANK_0 = 0,
	MEM_RAM_BANK_1,
	MEM_RAM_BANK_2,
	MEM_RAM_BANK_3,
	MEM_RAM_BANK_4,
	MEM_RAM_BANK_5,
	MEM_RAM_BANK_6,
	MEM_RAM_BANK_7,
	MEM_RAM_BANK_8,
	MEM_RAM_BANK_9,
	MEM_RAM_BANK_10,
	MEM_RAM_BANK_11,
	MPU_MEM_NUM_RAM_BANKS,
	MPU_MEM_OTP_BANK_0 = 16
};


/*==== MPU6050B1 parameters ====*/

#define NUM_REGS		(NUM_OF_MPU_REGISTERS)
#define START_SENS_REGS		(0x3B)
#define NUM_SENS_REGS		(0x60 - START_SENS_REGS + 1)

/*---- MPU Memory ----*/
#define NUM_BANKS		(MPU_MEM_NUM_RAM_BANKS)
#define BANK_SIZE		(256)
#define MEM_SIZE		(NUM_BANKS * BANK_SIZE)
#define MPU_MEM_BANK_SIZE	(BANK_SIZE)	/*alternative name */

#define FIFO_HW_SIZE		(1024)

#define NUM_EXT_SLAVES		(4)


/*==== BITS FOR MPU6050B1 ====*/
/*---- MPU6050B1 'XG_OFFS_TC' register (0, 1, 2) ----*/
#define BIT_PU_SLEEP_MODE			0x80
#define BITS_XG_OFFS_TC				0x7E
#define BIT_OTP_BNK_VLD				0x01

#define BIT_I2C_MST_VDDIO			0x80
#define BITS_YG_OFFS_TC				0x7E
#define BITS_ZG_OFFS_TC				0x7E
/*---- MPU6050B1 'FIFO_EN' register (23) ----*/
#define	BIT_TEMP_OUT				0x80
#define	BIT_GYRO_XOUT				0x40
#define	BIT_GYRO_YOUT				0x20
#define	BIT_GYRO_ZOUT				0x10
#define	BIT_ACCEL				0x08
#define	BIT_SLV_2				0x04
#define	BIT_SLV_1				0x02
#define	BIT_SLV_0				0x01
/*---- MPU6050B1 'CONFIG' register (1A) ----*/
/*NONE						0xC0 */
#define	BITS_EXT_SYNC_SET			0x38
#define	BITS_DLPF_CFG				0x07
/*---- MPU6050B1 'GYRO_CONFIG' register (1B) ----*/
/* voluntarily modified label from BITS_FS_SEL to
 * BITS_GYRO_FS_SEL to avoid confusion with MPU
 */
#define BITS_GYRO_FS_SEL			0x18
/*NONE						0x07 */
/*---- MPU6050B1 'ACCEL_CONFIG' register (1C) ----*/
#define BITS_ACCEL_FS_SEL			0x18
#define BITS_ACCEL_HPF				0x07
/*---- MPU6050B1 'I2C_MST_CTRL' register (24) ----*/
#define BIT_MULT_MST_EN				0x80
#define BIT_WAIT_FOR_ES				0x40
#define BIT_SLV_3_FIFO_EN			0x20
#define BIT_I2C_MST_PSR				0x10
#define BITS_I2C_MST_CLK			0x0F
/*---- MPU6050B1 'I2C_SLV?_ADDR' register (27,2A,2D,30) ----*/
#define BIT_I2C_READ				0x80
#define BIT_I2C_WRITE				0x00
#define BITS_I2C_ADDR				0x7F
/*---- MPU6050B1 'I2C_SLV?_CTRL' register (27,2A,2D,30) ----*/
#define BIT_SLV_ENABLE				0x80
#define BIT_SLV_BYTE_SW				0x40
#define BIT_SLV_REG_DIS				0x20
#define BIT_SLV_GRP				0x10
#define BITS_SLV_LENG				0x0F
/*---- MPU6050B1 'I2C_SLV4_ADDR' register (31) ----*/
#define BIT_I2C_SLV4_RNW			0x80
/*---- MPU6050B1 'I2C_SLV4_CTRL' register (34) ----*/
#define BIT_I2C_SLV4_EN				0x80
#define BIT_SLV4_DONE_INT_EN			0x40
#define BIT_SLV4_REG_DIS			0x20
#define MASK_I2C_MST_DLY			0x1F
/*---- MPU6050B1 'I2C_MST_STATUS' register (36) ----*/
#define BIT_PASS_THROUGH			0x80
#define BIT_I2C_SLV4_DONE			0x40
#define BIT_I2C_LOST_ARB			0x20
#define BIT_I2C_SLV4_NACK			0x10
#define BIT_I2C_SLV3_NACK			0x08
#define BIT_I2C_SLV2_NACK			0x04
#define BIT_I2C_SLV1_NACK			0x02
#define BIT_I2C_SLV0_NACK			0x01
/*---- MPU6050B1 'INT_PIN_CFG' register (37) ----*/
#define	BIT_ACTL				0x80
#define BIT_ACTL_LOW				0x80
#define BIT_ACTL_HIGH				0x00
#define	BIT_OPEN				0x40
#define	BIT_LATCH_INT_EN			0x20
#define	BIT_INT_ANYRD_2CLEAR			0x10
#define	BIT_ACTL_FSYNC				0x08
#define	BIT_FSYNC_INT_EN			0x04
#define	BIT_BYPASS_EN				0x02
#define	BIT_CLKOUT_EN				0x01
/*---- MPU6050B1 'INT_ENABLE' register (38) ----*/
#define	BIT_FF_EN				0x80
#define	BIT_MOT_EN				0x40
#define	BIT_ZMOT_EN				0x20
#define	BIT_FIFO_OVERFLOW_EN			0x10
#define BIT_I2C_MST_INT_EN			0x08
#define	BIT_PLL_RDY_EN				0x04
#define BIT_DMP_INT_EN				0x02
#define	BIT_RAW_RDY_EN				0x01
/*---- MPU6050B1 'DMP_INT_STATUS' register (39) ----*/
/*NONE						0x80 */
/*NONE						0x40 */
#define	BIT_DMP_INT_5				0x20
#define	BIT_DMP_INT_4				0x10
#define	BIT_DMP_INT_3				0x08
#define	BIT_DMP_INT_2				0x04
#define	BIT_DMP_INT_1				0x02
#define	BIT_DMP_INT_0				0x01
/*---- MPU6050B1 'INT_STATUS' register (3A) ----*/
#define	BIT_FF_INT				0x80
#define	BIT_MOT_INT				0x40
#define	BIT_ZMOT_INT				0x20
#define	BIT_FIFO_OVERFLOW_INT			0x10
#define	BIT_I2C_MST_INT				0x08
#define	BIT_PLL_RDY_INT				0x04
#define BIT_DMP_INT				0x02
#define	BIT_RAW_DATA_RDY_INT			0x01
/*---- MPU6050B1 'MPUREG_I2C_MST_DELAY_CTRL' register (0x67) ----*/
#define	BIT_DELAY_ES_SHADOW			0x80
#define	BIT_SLV4_DLY_EN				0x10
#define	BIT_SLV3_DLY_EN				0x08
#define	BIT_SLV2_DLY_EN				0x04
#define	BIT_SLV1_DLY_EN				0x02
#define	BIT_SLV0_DLY_EN				0x01
/*---- MPU6050B1 'BANK_SEL' register (6D) ----*/
#define	BIT_PRFTCH_EN				0x40
#define	BIT_CFG_USER_BANK			0x20
#define	BITS_MEM_SEL				0x1f
/*---- MPU6050B1 'USER_CTRL' register (6A) ----*/
#define	BIT_DMP_EN				0x80
#define	BIT_FIFO_EN				0x40
#define	BIT_I2C_MST_EN				0x20
#define	BIT_I2C_IF_DIS				0x10
#define	BIT_DMP_RST				0x08
#define	BIT_FIFO_RST				0x04
#define	BIT_I2C_MST_RST				0x02
#define	BIT_SIG_COND_RST			0x01
/*---- MPU6050B1 'PWR_MGMT_1' register (6B) ----*/
#define	BIT_H_RESET				0x80
#define	BIT_SLEEP				0x40
#define	BIT_CYCLE				0x20
#define BIT_PD_PTAT				0x08
#define BITS_CLKSEL				0x07
/*---- MPU6050B1 'PWR_MGMT_2' register (6C) ----*/
#define	BITS_LPA_WAKE_CTRL			0xC0
#define	BITS_LPA_WAKE_1HZ			0x00
#define	BITS_LPA_WAKE_2HZ			0x40
#define	BITS_LPA_WAKE_10HZ			0x80
#define	BITS_LPA_WAKE_40HZ			0xC0
#define	BIT_STBY_XA				0x20
#define	BIT_STBY_YA				0x10
#define	BIT_STBY_ZA				0x08
#define	BIT_STBY_XG				0x04
#define	BIT_STBY_YG				0x02
#define	BIT_STBY_ZG				0x01

#define ACCEL_MOT_THR_LSB (32) /* mg */
#define ACCEL_MOT_DUR_LSB (1)
#define ACCEL_ZRMOT_THR_LSB_CONVERSION(mg) ((mg * 1000) / 255)
#define ACCEL_ZRMOT_DUR_LSB (64)

/*----------------------------------------------------------------------------*/
/*---- Alternative names to take care of conflicts with current mpu3050.h ----*/
/*----------------------------------------------------------------------------*/

/*-- registers --*/
#define MPUREG_DLPF_FS_SYNC	MPUREG_CONFIG			/* 0x1A */

#define MPUREG_PWR_MGM		MPUREG_PWR_MGMT_1		/* 0x6B */
#define MPUREG_FIFO_EN1		MPUREG_FIFO_EN			/* 0x23 */
#define MPUREG_INT_CFG		MPUREG_INT_ENABLE		/* 0x38 */
#define MPUREG_X_OFFS_USRH	MPUREG_XG_OFFS_USRH		/* 0x13 */
#define MPUREG_WHO_AM_I		MPUREG_WHOAMI			/* 0x75 */
#define MPUREG_23_RSVD		MPUREG_EXT_SLV_SENS_DATA_00	/* 0x49 */

/*-- bits --*/
/* 'USER_CTRL' register */
#define BIT_AUX_IF_EN		BIT_I2C_MST_EN
#define BIT_AUX_RD_LENG		BIT_I2C_MST_EN
#define BIT_IME_IF_RST		BIT_I2C_MST_RST
#define BIT_GYRO_RST		BIT_SIG_COND_RST
/* 'INT_ENABLE' register */
#define BIT_RAW_RDY		BIT_RAW_DATA_RDY_INT
#define BIT_MPU_RDY_EN		BIT_PLL_RDY_EN
/* 'INT_STATUS' register */
#define BIT_INT_STATUS_FIFO_OVERLOW BIT_FIFO_OVERFLOW_INT

/*---- MPU6050 Silicon Revisions ----*/
#define MPU_SILICON_REV_A2		1	/* MPU6050A2 Device */
#define MPU_SILICON_REV_B1		2	/* MPU6050B1 Device */

/*---- MPU6050 notable product revisions ----*/
#define MPU_PRODUCT_KEY_B1_E1_5		105
#define MPU_PRODUCT_KEY_B2_F1		431

/*---- structure containing control variables used by MLDL ----*/
/*---- MPU clock source settings ----*/
/*---- MPU filter selections ----*/
enum mpu_filter {
	MPU_FILTER_256HZ_NOLPF2 = 0,
	MPU_FILTER_188HZ,
	MPU_FILTER_98HZ,
	MPU_FILTER_42HZ,
	MPU_FILTER_20HZ,
	MPU_FILTER_10HZ,
	MPU_FILTER_5HZ,
	MPU_FILTER_2100HZ_NOLPF,
	NUM_MPU_FILTER
};

enum mpu_fullscale {
	MPU_FS_250DPS = 0,
	MPU_FS_500DPS,
	MPU_FS_1000DPS,
	MPU_FS_2000DPS,
	NUM_MPU_FS
};

enum mpu_clock_sel {
	MPU_CLK_SEL_INTERNAL = 0,
	MPU_CLK_SEL_PLLGYROX,
	MPU_CLK_SEL_PLLGYROY,
	MPU_CLK_SEL_PLLGYROZ,
	MPU_CLK_SEL_PLLEXT32K,
	MPU_CLK_SEL_PLLEXT19M,
	MPU_CLK_SEL_RESERVED,
	MPU_CLK_SEL_STOP,
	NUM_CLK_SEL
};

enum mpu_ext_sync {
	MPU_EXT_SYNC_NONE = 0,
	MPU_EXT_SYNC_TEMP,
	MPU_EXT_SYNC_GYROX,
	MPU_EXT_SYNC_GYROY,
	MPU_EXT_SYNC_GYROZ,
	MPU_EXT_SYNC_ACCELX,
	MPU_EXT_SYNC_ACCELY,
	MPU_EXT_SYNC_ACCELZ,
	NUM_MPU_EXT_SYNC
};

#define MPUREG_CONFIG_VALUE(ext_sync, lpf) \
	((ext_sync << 3) | lpf)

#define MPUREG_GYRO_CONFIG_VALUE(x_st, y_st, z_st, full_scale)	\
	((x_st ? 0x80 : 0) |				\
	 (y_st ? 0x70 : 0) |				\
	 (z_st ? 0x60 : 0) |				\
	 (full_scale << 3))

#endif				/* __MPU6050_H_ */
