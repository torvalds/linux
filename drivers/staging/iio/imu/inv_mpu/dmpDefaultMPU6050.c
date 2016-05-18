/*
 * Copyright (C) 2012 Invensense, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include "inv_mpu_iio.h"
#include "dmpKey.h"
#include "dmpmap.h"

#define CFG_LP_QUAT             (2500)
#define END_ORIENT_TEMP         (2063)
#define CFG_27                  (2530)
#define CFG_23                  (2533)
#define CFG_PED_ENABLE          (2620)
#define CFG_FIFO_ON_EVENT       (2475)
#define CFG_PED_INT             (2873)
#define END_PREDICTION_UPDATE   (1958)
#define X_GRT_Y_TMP             (1555)
#define CFG_DR_INT              (1029)
#define CFG_AUTH                (1035)
#define UPDATE_PROP_ROT         (2032)
#define END_COMPARE_Y_X_TMP2    (1652)
#define SKIP_X_GRT_Y_TMP        (1556)
#define SKIP_END_COMPARE        (1632)
#define FCFG_3                  (1087)
#define FCFG_2                  (1066)
#define FCFG_1                  (1062)
#define END_COMPARE_Y_X_TMP3    (1631)
#define FCFG_7                  (1073)
#define FCFG_6                  (1105)
#define FLAT_STATE_END          (1910)
#define SWING_END_4             (1813)
#define SWING_END_2             (1762)
#define SWING_END_3             (1784)
#define SWING_END_1             (1747)
#define CFG_8                   (2506)
#define CFG_15                  (2515)
#define CFG_16                  (2534)
#define CFG_EXT_GYRO_BIAS       (1184)
#define END_COMPARE_Y_X_TMP     (1604)
#define DO_NOT_UPDATE_PROP_ROT  (2036)
#define CFG_7                   (1403)
#define FLAT_STATE_END_TEMP     (1880)
#define END_COMPARE_Y_X         (1681)
#define SMD_TP2                 (1366)
#define SKIP_SWING_END_1        (1748)
#define SKIP_SWING_END_3        (1785)
#define SKIP_SWING_END_2        (1763)
#define SMD_TP1                 (1343)
#define TILTG75_START           (1869)
#define CFG_6                   (2541)
#define TILTL75_END             (1866)
#define END_ORIENT              (2081)
#define TILTL75_START           (1840)
#define CFG_MOTION_BIAS         (1405)
#define X_GRT_Y                 (1605)
#define TEMPLABEL               (2105)
#define CFG_DISPLAY_ORIENT_INT  (2050)

#define CFG_GYRO_RAW_DATA       (2510)
#define X_GRT_Y_TMP2            (1576)

#define D_0_22                  (22 + 512)
#define D_0_24                  (24 + 512)

#define D_0_36                  (36)
#define D_0_52                  (52)
#define D_0_96                  (96)
#define D_0_104                 (104)
#define D_0_108                 (108)
#define D_0_163                 (163)
#define D_0_188                 (188)
#define D_0_192                 (192)
#define D_0_224                 (224)
#define D_0_228                 (228)
#define D_0_232                 (232)
#define D_0_236                 (236)

#define D_1_2                   (256 + 2)
#define D_1_4                   (256 + 4)
#define D_1_8                   (256 + 8)
#define D_1_10                  (256 + 10)
#define D_1_24                  (256 + 24)
#define D_1_28                  (256 + 28)
#define D_1_36                  (256 + 36)
#define D_1_40                  (256 + 40)
#define D_1_44                  (256 + 44)
#define D_1_72                  (256 + 72)
#define D_1_74                  (256 + 74)
#define D_1_79                  (256 + 79)
#define D_1_88                  (256 + 88)
#define D_1_90                  (256 + 90)
#define D_1_92                  (256 + 92)
#define D_1_96                  (256 + 96)
#define D_1_98                  (256 + 98)
#define D_1_106                 (256 + 106)
#define D_1_108                 (256 + 108)
#define D_1_112                 (256 + 112)
#define D_1_128                 (256 + 144)
#define D_1_152                 (256 + 12)
#define D_1_160                 (256 + 160)
#define D_1_176                 (256 + 176)
#define D_1_178                 (256 + 178)
#define D_1_218                 (256 + 218)
#define D_1_232                 (256 + 232)
#define D_1_236                 (256 + 236)
#define D_1_240                 (256 + 240)
#define D_1_244                 (256 + 244)
#define D_1_250                 (256 + 250)
#define D_1_252                 (256 + 252)
#define D_2_12                  (512 + 12)
#define D_2_96                  (512 + 96)
#define D_2_108                 (512 + 108)
#define D_2_208                 (512 + 208)
#define D_2_224                 (512 + 224)
#define D_2_236                 (512 + 236)
#define D_2_244                 (512 + 244)
#define D_2_248                 (512 + 248)
#define D_2_252                 (512 + 252)

#define CPASS_BIAS_X            (35 * 16 + 4)
#define CPASS_BIAS_Y            (35 * 16 + 8)
#define CPASS_BIAS_Z            (35 * 16 + 12)
#define CPASS_MTX_00            (36 * 16)
#define CPASS_MTX_01            (36 * 16 + 4)
#define CPASS_MTX_02            (36 * 16 + 8)
#define CPASS_MTX_10            (36 * 16 + 12)
#define CPASS_MTX_11            (37 * 16)
#define CPASS_MTX_12            (37 * 16 + 4)
#define CPASS_MTX_20            (37 * 16 + 8)
#define CPASS_MTX_21            (37 * 16 + 12)
#define CPASS_MTX_22            (43 * 16 + 12)
#define D_EXT_GYRO_BIAS_X       (61 * 16)
#define D_EXT_GYRO_BIAS_Y       (61 * 16 + 4)
#define D_EXT_GYRO_BIAS_Z       (61 * 16 + 8)
#define D_ACT0                  (40 * 16)
#define D_ACSX                  (40 * 16 + 4)
#define D_ACSY                  (40 * 16 + 8)
#define D_ACSZ                  (40 * 16 + 12)

#define FLICK_MSG               (45 * 16 + 4)
#define FLICK_COUNTER           (45 * 16 + 8)
#define FLICK_LOWER             (45 * 16 + 12)
#define FLICK_UPPER             (46 * 16 + 12)

#define D_SMD_ENABLE            (18 * 16)
#define D_SMD_MOT_THLD          (20 * 16)
#define D_SMD_DELAY_THLD        (21 * 16 + 4)
#define D_SMD_DELAY2_THLD       (21 * 16 + 12)
#define D_SMD_EXE_STATE         (22 * 16)
#define D_SMD_DELAY_CNTR        (21 * 16)

#define D_AUTH_OUT              (992)
#define D_AUTH_IN               (996)
#define D_AUTH_A                (1000)
#define D_AUTH_B                (1004)

#define D_PEDSTD_BP_B           (768 + 0x1C)
#define D_PEDSTD_HP_A           (768 + 0x78)
#define D_PEDSTD_HP_B           (768 + 0x7C)
#define D_PEDSTD_BP_A4          (768 + 0x40)
#define D_PEDSTD_BP_A3          (768 + 0x44)
#define D_PEDSTD_BP_A2          (768 + 0x48)
#define D_PEDSTD_BP_A1          (768 + 0x4C)
#define D_PEDSTD_INT_THRSH      (768 + 0x68)
#define D_PEDSTD_CLIP           (768 + 0x6C)
#define D_PEDSTD_SB             (768 + 0x28)
#define D_PEDSTD_SB_TIME        (768 + 0x2C)
#define D_PEDSTD_PEAKTHRSH      (768 + 0x98)
#define D_PEDSTD_TIML           (768 + 0x2A)
#define D_PEDSTD_TIMH           (768 + 0x2E)
#define D_PEDSTD_PEAK           (768 + 0X94)
#define D_PEDSTD_STEPCTR        (768 + 0x60)
#define D_PEDSTD_TIMECTR        (964)
#define D_PEDSTD_DECI           (768 + 0xA0)

#define D_HOST_NO_MOT           (976)
#define D_ACCEL_BIAS            (660)

#define D_ORIENT_GAP            (76)

#define D_TILT0_H               (48)
#define D_TILT0_L               (50)
#define D_TILT1_H               (52)
#define D_TILT1_L               (54)
#define D_TILT2_H               (56)
#define D_TILT2_L               (58)
#define D_TILT3_H               (60)
#define D_TILT3_L               (62)

/* Batch mode */
#define D_BM_BATCH_CNTR         (27 * 16 + 4)
#define D_BM_BATCH_THLD         (27 * 16 + 8)
#define D_BM_ENABLE             (28 * 16 + 6)
#define D_BM_NUMWORD_TOFILL     (28 * 16 + 4)

static const struct tKeyLabel dmpTConfig[] = {
	{KEY_CFG_27,                    CFG_27},
	{KEY_CFG_23,                    CFG_23},
	{KEY_CFG_PED_ENABLE,            CFG_PED_ENABLE},
	{KEY_CFG_FIFO_ON_EVENT,         CFG_FIFO_ON_EVENT},
	{KEY_CFG_DR_INT,                CFG_DR_INT},
	{KEY_CFG_AUTH,                  CFG_AUTH},
	{KEY_FCFG_1,                    FCFG_1},
	{KEY_FCFG_3,                    FCFG_3},
	{KEY_FCFG_2,                    FCFG_2},
	{KEY_CFG_DISPLAY_ORIENT_INT,    CFG_DISPLAY_ORIENT_INT},
	{KEY_FCFG_7,                    FCFG_7},
	{KEY_FCFG_6,                    FCFG_6},
	{KEY_CFG_8,                     CFG_8},
	{KEY_CFG_15,                    CFG_15},
	{KEY_CFG_16,                    CFG_16},
	{KEY_CFG_EXT_GYRO_BIAS,         CFG_EXT_GYRO_BIAS},
	{KEY_CFG_6,                     CFG_6},
	{KEY_CFG_LP_QUAT,               CFG_LP_QUAT},
	{KEY_CFG_7,                     CFG_7},
	{KEY_CFG_MOTION_BIAS,           CFG_MOTION_BIAS},
	{KEY_CFG_DISPLAY_ORIENT_INT,    CFG_DISPLAY_ORIENT_INT},
	{KEY_CFG_GYRO_RAW_DATA,         CFG_GYRO_RAW_DATA},
	{KEY_D_0_22,                D_0_22},
	{KEY_D_0_96,                D_0_96},
	{KEY_D_0_104,               D_0_104},
	{KEY_D_0_108,               D_0_108},
	{KEY_D_1_36,               D_1_36},
	{KEY_D_1_40,               D_1_40},
	{KEY_D_1_44,               D_1_44},
	{KEY_D_1_72,               D_1_72},
	{KEY_D_1_74,               D_1_74},
	{KEY_D_1_79,               D_1_79},
	{KEY_D_1_88,               D_1_88},
	{KEY_D_1_90,               D_1_90},
	{KEY_D_1_92,               D_1_92},
	{KEY_D_1_160,               D_1_160},
	{KEY_D_1_176,               D_1_176},
	{KEY_D_1_178,               D_1_178},
	{KEY_D_1_218,               D_1_218},
	{KEY_D_1_232,               D_1_232},
	{KEY_D_1_250,               D_1_250},
	{KEY_DMP_SH_TH_Y,           DMP_SH_TH_Y},
	{KEY_DMP_SH_TH_X,           DMP_SH_TH_X},
	{KEY_DMP_SH_TH_Z,           DMP_SH_TH_Z},
	{KEY_DMP_ORIENT,            DMP_ORIENT},
	{KEY_D_AUTH_OUT,            D_AUTH_OUT},
	{KEY_D_AUTH_IN,             D_AUTH_IN},
	{KEY_D_AUTH_A,              D_AUTH_A},
	{KEY_D_AUTH_B,              D_AUTH_B},
	{KEY_CPASS_BIAS_X,          CPASS_BIAS_X},
	{KEY_CPASS_BIAS_Y,          CPASS_BIAS_Y},
	{KEY_CPASS_BIAS_Z,          CPASS_BIAS_Z},
	{KEY_CPASS_MTX_00,          CPASS_MTX_00},
	{KEY_CPASS_MTX_01,          CPASS_MTX_01},
	{KEY_CPASS_MTX_02,          CPASS_MTX_02},
	{KEY_CPASS_MTX_10,          CPASS_MTX_10},
	{KEY_CPASS_MTX_11,          CPASS_MTX_11},
	{KEY_CPASS_MTX_12,          CPASS_MTX_12},
	{KEY_CPASS_MTX_20,          CPASS_MTX_20},
	{KEY_CPASS_MTX_21,          CPASS_MTX_21},
	{KEY_CPASS_MTX_22,          CPASS_MTX_22},
	{KEY_D_ACT0,                D_ACT0},
	{KEY_D_ACSX,                D_ACSX},
	{KEY_D_ACSY,                D_ACSY},
	{KEY_D_ACSZ,                D_ACSZ},
	{KEY_FLICK_MSG,             FLICK_MSG},
	{KEY_FLICK_COUNTER,         FLICK_COUNTER},
	{KEY_FLICK_LOWER,           FLICK_LOWER},
	{KEY_FLICK_UPPER,           FLICK_UPPER},
	{KEY_D_PEDSTD_BP_B, D_PEDSTD_BP_B},
	{KEY_D_PEDSTD_HP_A, D_PEDSTD_HP_A},
	{KEY_D_PEDSTD_HP_B, D_PEDSTD_HP_B},
	{KEY_D_PEDSTD_BP_A4, D_PEDSTD_BP_A4},
	{KEY_D_PEDSTD_BP_A3, D_PEDSTD_BP_A3},
	{KEY_D_PEDSTD_BP_A2, D_PEDSTD_BP_A2},
	{KEY_D_PEDSTD_BP_A1, D_PEDSTD_BP_A1},
	{KEY_D_PEDSTD_INT_THRSH, D_PEDSTD_INT_THRSH},
	{KEY_D_PEDSTD_CLIP, D_PEDSTD_CLIP},
	{KEY_D_PEDSTD_SB, D_PEDSTD_SB},
	{KEY_D_PEDSTD_SB_TIME, D_PEDSTD_SB_TIME},
	{KEY_D_PEDSTD_PEAKTHRSH, D_PEDSTD_PEAKTHRSH},
	{KEY_D_PEDSTD_TIML,      D_PEDSTD_TIML},
	{KEY_D_PEDSTD_TIMH,      D_PEDSTD_TIMH},
	{KEY_D_PEDSTD_PEAK,      D_PEDSTD_PEAK},
	{KEY_D_PEDSTD_STEPCTR,   D_PEDSTD_STEPCTR},
	{KEY_D_PEDSTD_TIMECTR,  D_PEDSTD_TIMECTR},
	{KEY_D_PEDSTD_DECI,  D_PEDSTD_DECI},
	{KEY_D_HOST_NO_MOT,  D_HOST_NO_MOT},
	{KEY_D_ACCEL_BIAS,  D_ACCEL_BIAS},
	{KEY_D_ORIENT_GAP,  D_ORIENT_GAP},
	{KEY_D_TILT0_H, D_TILT0_H},
	{KEY_D_TILT0_L, D_TILT0_L},
	{KEY_D_TILT1_H, D_TILT1_H},
	{KEY_D_TILT1_L, D_TILT1_L},
	{KEY_D_TILT2_H, D_TILT2_H},
	{KEY_D_TILT2_L, D_TILT2_L},
	{KEY_D_TILT3_H, D_TILT3_H},
	{KEY_D_TILT3_L, D_TILT3_L},
	{KEY_CFG_EXT_GYRO_BIAS_X, D_EXT_GYRO_BIAS_X},
	{KEY_CFG_EXT_GYRO_BIAS_Y, D_EXT_GYRO_BIAS_Y},
	{KEY_CFG_EXT_GYRO_BIAS_Z, D_EXT_GYRO_BIAS_Z},
	{KEY_CFG_PED_INT, CFG_PED_INT},
	{KEY_SMD_ENABLE, D_SMD_ENABLE},
	{KEY_SMD_ACCEL_THLD, D_SMD_MOT_THLD},
	{KEY_SMD_DELAY_THLD, D_SMD_DELAY_THLD},
	{KEY_SMD_DELAY2_THLD, D_SMD_DELAY2_THLD},
	{KEY_SMD_ENABLE_TESTPT1, SMD_TP1},
	{KEY_SMD_ENABLE_TESTPT2, SMD_TP2},
	{KEY_SMD_EXE_STATE, D_SMD_EXE_STATE},
	{KEY_SMD_DELAY_CNTR, D_SMD_DELAY_CNTR},
	{KEY_BM_ENABLE, D_BM_ENABLE},
	{KEY_BM_BATCH_CNTR, D_BM_BATCH_CNTR},
	{KEY_BM_BATCH_THLD, D_BM_BATCH_THLD},
	{KEY_BM_NUMWORD_TOFILL, D_BM_NUMWORD_TOFILL}
};

#define NUM_LOCAL_KEYS (ARRAY_SIZE(dmpTConfig) / sizeof(dmpTConfig[0]))

static struct tKeyLabel keys[NUM_KEYS];

unsigned short inv_dmp_get_address(unsigned short key)
{
	static int isSorted;

	if (!isSorted) {
		int kk;
		for (kk = 0; kk < NUM_KEYS; ++kk) {
			keys[kk].addr = 0xffff;
			keys[kk].key = kk;
		}
		for (kk = 0; kk < NUM_LOCAL_KEYS; ++kk)
			keys[dmpTConfig[kk].key].addr = dmpTConfig[kk].addr;
		isSorted = 1;
	}
	if (key >= NUM_KEYS) {
		pr_err("ERROR!! key not exist=%d!\n", key);
		return 0xffff;
	}
	if (0xffff == keys[key].addr) {
		pr_err("ERROR!!key not local=%d!\n", key);
		dump_stack();
	}
	return keys[key].addr;
}
