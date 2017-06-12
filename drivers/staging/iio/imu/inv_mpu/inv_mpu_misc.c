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

/**
 *  @addtogroup  DRIVERS
 *  @brief       Hardware drivers.
 *
 *  @{
 *      @file    inv_mpu_misc.c
 *      @brief   A sysfs device driver for Invensense mpu.
 *      @details This file is part of invensense mpu driver code
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/crc32.h>

#include "inv_mpu_iio.h"
#include "inv_counters.h"

/* DMP defines */
#define DMP_ORIENTATION_TIME		500
#define DMP_ORIENTATION_ANGLE		60
#define DMP_DEFAULT_FIFO_RATE           200
#define DMP_TAP_SCALE                   (767603923 / 5)
#define DMP_MULTI_SHIFT                 30
#define DMP_MULTI_TAP_TIME              500
#define DMP_SHAKE_REJECT_THRESH         100
#define DMP_SHAKE_REJECT_TIME           10
#define DMP_SHAKE_REJECT_TIMEOUT        10
#define DMP_ANGLE_SCALE                 15
#define DMP_PRECISION                   1000
#define DMP_MAX_DIVIDER                 4
#define DMP_MAX_MIN_TAPS                4
#define DMP_IMAGE_CRC_VALUE             0x665f5a73
#define DMP_IMAGE_SIZE                  2913

/*--- Test parameters defaults --- */
#define DEF_OLDEST_SUPP_PROD_REV    8
#define DEF_OLDEST_SUPP_SW_REV      2

/* sample rate */
#define DEF_SELFTEST_SAMPLE_RATE             0
/* full scale setting dps */
#define DEF_SELFTEST_GYRO_FS         (0 << 3)
#define DEF_SELFTEST_ACCEL_FS         (2 << 3)
#define DEF_SELFTEST_GYRO_SENS            (32768 / 250)
/* wait time before collecting data */
#define DEF_GYRO_WAIT_TIME          10
#define DEF_ST_STABLE_TIME          200
#define DEF_ST_6500_STABLE_TIME     20
#define DEF_GYRO_SCALE              131
#define DEF_ST_PRECISION            1000
#define DEF_ST_ACCEL_FS_MG          8000UL
#define DEF_ST_SCALE                (1L << 15)
#define DEF_ST_TRY_TIMES            2
#define DEF_ST_COMPASS_RESULT_SHIFT 2
#define DEF_ST_ACCEL_RESULT_SHIFT   1
#define DEF_ST_OTP0_THRESH          60
#define DEF_ST_ABS_THRESH           20
#define DEF_ST_TOR                  2

#define DEF_ST_COMPASS_WAIT_MIN     (10 * 1000)
#define DEF_ST_COMPASS_WAIT_MAX     (15 * 1000)
#define DEF_ST_COMPASS_TRY_TIMES    10
#define DEF_ST_COMPASS_8963_SHIFT   2

#define X                           0
#define Y                           1
#define Z                           2
/*---- MPU6050 notable product revisions ----*/
#define MPU_PRODUCT_KEY_B1_E1_5      105
#define MPU_PRODUCT_KEY_B2_F1        431
/* accelerometer Hw self test min and max bias shift (mg) */
#define DEF_ACCEL_ST_SHIFT_MIN       300
#define DEF_ACCEL_ST_SHIFT_MAX       950

#define DEF_ACCEL_ST_SHIFT_DELTA     140
#define DEF_GYRO_CT_SHIFT_DELTA      140
/* gyroscope Coriolis self test min and max bias shift (dps) */
#define DEF_GYRO_CT_SHIFT_MIN        10
#define DEF_GYRO_CT_SHIFT_MAX        105

/*---- MPU6500 Self Test Pass/Fail Criteria ----*/
/* Gyro Offset Max Value (dps) */
#define DEF_GYRO_OFFSET_MAX             20
/* Gyro Self Test Absolute Limits ST_AL (dps) */
#define DEF_GYRO_ST_AL                  60
/* Accel Self Test Absolute Limits ST_AL (mg) */
#define DEF_ACCEL_ST_AL_MIN             225
#define DEF_ACCEL_ST_AL_MAX             675
#define DEF_6500_ACCEL_ST_SHIFT_DELTA   500
#define DEF_6500_GYRO_CT_SHIFT_DELTA    500
#define DEF_ST_MPU6500_ACCEL_LPF        2
#define DEF_ST_6500_ACCEL_FS_MG         2000UL
#define DEF_SELFTEST_6500_ACCEL_FS      (0 << 3)

/* Note: The ST_AL values are only used when ST_OTP = 0,
 * i.e no factory self test values for reference
 */

/* NOTE: product entries are in chronological order */
static const struct prod_rev_map_t prod_rev_map[] = {
	/* prod_ver = 0 */
	{MPL_PROD_KEY(0,   1), MPU_SILICON_REV_A2, 131, 16384},
	{MPL_PROD_KEY(0,   2), MPU_SILICON_REV_A2, 131, 16384},
	{MPL_PROD_KEY(0,   3), MPU_SILICON_REV_A2, 131, 16384},
	{MPL_PROD_KEY(0,   4), MPU_SILICON_REV_A2, 131, 16384},
	{MPL_PROD_KEY(0,   5), MPU_SILICON_REV_A2, 131, 16384},
	{MPL_PROD_KEY(0,   6), MPU_SILICON_REV_A2, 131, 16384},
	/* prod_ver = 1 */
	{MPL_PROD_KEY(0,   7), MPU_SILICON_REV_A2, 131, 16384},
	{MPL_PROD_KEY(0,   8), MPU_SILICON_REV_A2, 131, 16384},
	{MPL_PROD_KEY(0,   9), MPU_SILICON_REV_A2, 131, 16384},
	{MPL_PROD_KEY(0,  10), MPU_SILICON_REV_A2, 131, 16384},
	{MPL_PROD_KEY(0,  11), MPU_SILICON_REV_A2, 131, 16384},
	{MPL_PROD_KEY(0,  12), MPU_SILICON_REV_A2, 131, 16384},
	{MPL_PROD_KEY(0,  13), MPU_SILICON_REV_A2, 131, 16384},
	{MPL_PROD_KEY(0,  14), MPU_SILICON_REV_A2, 131, 16384},
	{MPL_PROD_KEY(0,  15), MPU_SILICON_REV_A2, 131, 16384},
	{MPL_PROD_KEY(0,  27), MPU_SILICON_REV_A2, 131, 16384},
	/* prod_ver = 1 */
	{MPL_PROD_KEY(1,  16), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(1,  17), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(1,  18), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(1,  19), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(1,  20), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(1,  28), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(1,   1), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(1,   2), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(1,   3), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(1,   4), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(1,   5), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(1,   6), MPU_SILICON_REV_B1, 131, 16384},
	/* prod_ver = 2 */
	{MPL_PROD_KEY(2,   7), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(2,   8), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(2,   9), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(2,  10), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(2,  11), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(2,  12), MPU_SILICON_REV_B1, 131, 16384},
	{MPL_PROD_KEY(2,  29), MPU_SILICON_REV_B1, 131, 16384},
	/* prod_ver = 3 */
	{MPL_PROD_KEY(3,  30), MPU_SILICON_REV_B1, 131, 16384},
	/* prod_ver = 4 */
	{MPL_PROD_KEY(4,  31), MPU_SILICON_REV_B1, 131,  8192},
	{MPL_PROD_KEY(4,   1), MPU_SILICON_REV_B1, 131,  8192},
	{MPL_PROD_KEY(4,   3), MPU_SILICON_REV_B1, 131,  8192},
	/* prod_ver = 5 */
	{MPL_PROD_KEY(5,   3), MPU_SILICON_REV_B1, 131, 16384},
	/* prod_ver = 6 */
	{MPL_PROD_KEY(6,  19), MPU_SILICON_REV_B1, 131, 16384},
	/* prod_ver = 7 */
	{MPL_PROD_KEY(7,  19), MPU_SILICON_REV_B1, 131, 16384},
	/* prod_ver = 8 */
	{MPL_PROD_KEY(8,  19), MPU_SILICON_REV_B1, 131, 16384},
	/* prod_ver = 9 */
	{MPL_PROD_KEY(9,  19), MPU_SILICON_REV_B1, 131, 16384},
	/* prod_ver = 10 */
	{MPL_PROD_KEY(10, 19), MPU_SILICON_REV_B1, 131, 16384}
};

/*
*   List of product software revisions
*
*   NOTE :
*   software revision 0 falls back to the old detection method
*   based off the product version and product revision per the
*   table above
*/
static const struct prod_rev_map_t sw_rev_map[] = {
	{0,		     0,   0,     0},
	{1, MPU_SILICON_REV_B1, 131,  8192},	/* rev C */
	{2, MPU_SILICON_REV_B1, 131, 16384}	/* rev D */
};

static const u16 mpu_6500_st_tb[256] = {
	2620, 2646, 2672, 2699, 2726, 2753, 2781, 2808,
	2837, 2865, 2894, 2923, 2952, 2981, 3011, 3041,
	3072, 3102, 3133, 3165, 3196, 3228, 3261, 3293,
	3326, 3359, 3393, 3427, 3461, 3496, 3531, 3566,
	3602, 3638, 3674, 3711, 3748, 3786, 3823, 3862,
	3900, 3939, 3979, 4019, 4059, 4099, 4140, 4182,
	4224, 4266, 4308, 4352, 4395, 4439, 4483, 4528,
	4574, 4619, 4665, 4712, 4759, 4807, 4855, 4903,
	4953, 5002, 5052, 5103, 5154, 5205, 5257, 5310,
	5363, 5417, 5471, 5525, 5581, 5636, 5693, 5750,
	5807, 5865, 5924, 5983, 6043, 6104, 6165, 6226,
	6289, 6351, 6415, 6479, 6544, 6609, 6675, 6742,
	6810, 6878, 6946, 7016, 7086, 7157, 7229, 7301,
	7374, 7448, 7522, 7597, 7673, 7750, 7828, 7906,
	7985, 8065, 8145, 8227, 8309, 8392, 8476, 8561,
	8647, 8733, 8820, 8909, 8998, 9088, 9178, 9270,
	9363, 9457, 9551, 9647, 9743, 9841, 9939, 10038,
	10139, 10240, 10343, 10446, 10550, 10656, 10763, 10870,
	10979, 11089, 11200, 11312, 11425, 11539, 11654, 11771,
	11889, 12008, 12128, 12249, 12371, 12495, 12620, 12746,
	12874, 13002, 13132, 13264, 13396, 13530, 13666, 13802,
	13940, 14080, 14221, 14363, 14506, 14652, 14798, 14946,
	15096, 15247, 15399, 15553, 15709, 15866, 16024, 16184,
	16346, 16510, 16675, 16842, 17010, 17180, 17352, 17526,
	17701, 17878, 18057, 18237, 18420, 18604, 18790, 18978,
	19167, 19359, 19553, 19748, 19946, 20145, 20347, 20550,
	20756, 20963, 21173, 21385, 21598, 21814, 22033, 22253,
	22475, 22700, 22927, 23156, 23388, 23622, 23858, 24097,
	24338, 24581, 24827, 25075, 25326, 25579, 25835, 26093,
	26354, 26618, 26884, 27153, 27424, 27699, 27976, 28255,
	28538, 28823, 29112, 29403, 29697, 29994, 30294, 30597,
	30903, 31212, 31524, 31839, 32157, 32479, 32804
};

static const int accl_st_tb[31] = {
	340, 351, 363, 375, 388, 401, 414, 428,
	443, 458, 473, 489, 506, 523, 541, 559,
	578, 597, 617, 638, 660, 682, 705, 729,
	753, 779, 805, 832, 860, 889, 919};

static const int gyro_6050_st_tb[31] = {
	3275, 3425, 3583, 3748, 3920, 4100, 4289, 4486,
	4693, 4909, 5134, 5371, 5618, 5876, 6146, 6429,
	6725, 7034, 7358, 7696, 8050, 8421, 8808, 9213,
	9637, 10080, 10544, 11029, 11537, 12067, 12622};
static const int gyro_3500_st_tb[255] = {
	2620, 2646, 2672, 2699, 2726, 2753, 2781, 2808,
	2837, 2865, 2894, 2923, 2952, 2981, 3011, 3041,
	3072, 3102, 3133, 3165, 3196, 3228, 3261, 3293,
	3326, 3359, 3393, 3427, 3461, 3496, 3531, 3566,
	3602, 3638, 3674, 3711, 3748, 3786, 3823, 3862,
	3900, 3939, 3979, 4019, 4059, 4099, 4140, 4182,
	4224, 4266, 4308, 4352, 4395, 4439, 4483, 4528,
	4574, 4619, 4665, 4712, 4759, 4807, 4855, 4903,
	4953, 5002, 5052, 5103, 5154, 5205, 5257, 5310,
	5363, 5417, 5471, 5525, 5581, 5636, 5693, 5750,
	5807, 5865, 5924, 5983, 6043, 6104, 6165, 6226,
	6289, 6351, 6415, 6479, 6544, 6609, 6675, 6742,
	6810, 6878, 6946, 7016, 7086, 7157, 7229, 7301,
	7374, 7448, 7522, 7597, 7673, 7750, 7828, 7906,
	7985, 8065, 8145, 8227, 8309, 8392, 8476, 8561,
	8647, 8733, 8820, 8909, 8998, 9088, 9178, 9270,
	9363, 9457, 9551, 9647, 9743, 9841, 9939, 10038,
	10139, 10240, 10343, 10446, 10550, 10656, 10763, 10870,
	10979, 11089, 11200, 11312, 11425, 11539, 11654, 11771,
	11889, 12008, 12128, 12249, 12371, 12495, 12620, 12746,
	12874, 13002, 13132, 13264, 13396, 13530, 13666, 13802,
	13940, 14080, 14221, 14363, 14506, 14652, 14798, 14946,
	15096, 15247, 15399, 15553, 15709, 15866, 16024, 16184,
	16346, 16510, 16675, 16842, 17010, 17180, 17352, 17526,
	17701, 17878, 18057, 18237, 18420, 18604, 18790, 18978,
	19167, 19359, 19553, 19748, 19946, 20145, 20347, 20550,
	20756, 20963, 21173, 21385, 21598, 21814, 22033, 22253,
	22475, 22700, 22927, 23156, 23388, 23622, 23858, 24097,
	24338, 24581, 24827, 25075, 25326, 25579, 25835, 26093,
	26354, 26618, 26884, 27153, 27424, 27699, 27976, 28255,
	28538, 28823, 29112, 29403, 29697, 29994, 30294, 30597,
	30903, 31212, 31524, 31839, 32157, 32479, 32804};

char *wr_pr_debug_begin(u8 const *data, u32 len, char *string)
{
	int ii;
	string = kmalloc(len * 2 + 1, GFP_KERNEL);
	for (ii = 0; ii < len; ii++)
		sprintf(&string[ii * 2], "%02X", data[ii]);
	string[len * 2] = 0;
	return string;
}

char *wr_pr_debug_end(char *string)
{
	kfree(string);
	return "";
}

int mpu_memory_write_unaligned(struct inv_mpu_iio_s *st, u16 key, int len,
								u8 const *d)
{
	u32 addr;
	int start, end;
	int len1, len2;
	int result = 0;
	if (len > MPU_MEM_BANK_SIZE)
		return -EINVAL;
	addr = inv_dmp_get_address(key);
	if (addr > MPU6XXX_MAX_MPU_MEM)
		return -EINVAL;
	start = (addr >> 8);
	end   = ((addr + len - 1) >> 8);
	if (start == end) {
		result = mpu_memory_write(st, st->i2c_addr, addr, len, d);
	} else {
		end <<= 8;
		len1 = end - addr;
		len2 = len - len1;
		result = mpu_memory_write(st, st->i2c_addr, addr, len1, d);
		result |= mpu_memory_write(st, st->i2c_addr, end, len2,
								d + len1);
	}

	return result;
}

/**
 *  index_of_key()- Inverse lookup of the index of an MPL product key .
 *  @key: the MPL product indentifier also referred to as 'key'.
 */
static short index_of_key(u16 key)
{
	int i;
	for (i = 0; i < NUM_OF_PROD_REVS; i++)
		if (prod_rev_map[i].mpl_product_key == key)
			return (short)i;
	return -EINVAL;
}

int inv_get_silicon_rev_mpu6500(struct inv_mpu_iio_s *st)
{
	struct inv_chip_info_s *chip_info = &st->chip_info;
	int result;
	u8 whoami, sw_rev;

	if (!st->use_hid) {
		result = inv_plat_read(st, REG_WHOAMI, 1, &whoami);
		if (result)
			return result;
		if (whoami != MPU6500_ID && whoami != MPU9250_ID &&
				whoami != MPU6515_ID && whoami != MPU6880_ID)
			return -EINVAL;

		/*memory read need more time after power up */
		msleep(POWER_UP_TIME);
		result = mpu_memory_read(st, st->i2c_addr,
				MPU6500_MEM_REV_ADDR, 1, &sw_rev);
		sw_rev &= INV_MPU_REV_MASK;
		if (result)
			return result;
		if (sw_rev != 0)
			return -EINVAL;
	}
	/* these values are place holders and not real values */
	chip_info->product_id = MPU6500_PRODUCT_REVISION;
	chip_info->product_revision = MPU6500_PRODUCT_REVISION;
	chip_info->silicon_revision = MPU6500_PRODUCT_REVISION;
	chip_info->software_revision = sw_rev;
	chip_info->gyro_sens_trim = DEFAULT_GYRO_TRIM;
	chip_info->accl_sens_trim = DEFAULT_ACCL_TRIM;
	chip_info->multi = 1;

	return 0;
}

int inv_get_silicon_rev_mpu6050(struct inv_mpu_iio_s *st)
{
	int result;
	struct inv_reg_map_s *reg;
	u8 prod_ver = 0x00, prod_rev = 0x00;
	struct prod_rev_map_t *p_rev;
	u8 bank =
	    (BIT_PRFTCH_EN | BIT_CFG_USER_BANK | MPU_MEM_OTP_BANK_0);
	u16 mem_addr = ((bank << 8) | MEM_ADDR_PROD_REV);
	u16 key;
	u8 regs[5];
	u16 sw_rev;
	short index;
	struct inv_chip_info_s *chip_info = &st->chip_info;
	reg = &st->reg;

	result = inv_plat_read(st, REG_PRODUCT_ID, 1, &prod_ver);
	if (result)
		return result;
	prod_ver &= 0xf;
	/*memory read need more time after power up */
	msleep(POWER_UP_TIME);
	result = mpu_memory_read(st, st->i2c_addr, mem_addr,
			1, &prod_rev);
	if (result)
		return result;
	prod_rev >>= 2;
	/* clean the prefetch and cfg user bank bits */
	result = inv_plat_single_write(st, reg->bank_sel, 0);
	if (result)
		return result;
	/* get the software-product version, read from XA_OFFS_L */
	result = inv_plat_read(st, REG_XA_OFFS_L_TC,
				SOFT_PROD_VER_BYTES, regs);
	if (result)
		return result;

	sw_rev = (regs[4] & 0x01) << 2 |	/* 0x0b, bit 0 */
		 (regs[2] & 0x01) << 1 |	/* 0x09, bit 0 */
		 (regs[0] & 0x01);		/* 0x07, bit 0 */
	/* if 0, use the product key to determine the type of part */
	if (sw_rev == 0) {
		key = MPL_PROD_KEY(prod_ver, prod_rev);
		if (key == 0)
			return -EINVAL;
		index = index_of_key(key);
		if (index < 0 || index >= NUM_OF_PROD_REVS)
			return -EINVAL;
		/* check MPL is compiled for this device */
		if (prod_rev_map[index].silicon_rev != MPU_SILICON_REV_B1)
			return -EINVAL;
		p_rev = (struct prod_rev_map_t *)&prod_rev_map[index];
	/* if valid, use the software product key */
	} else if (sw_rev < ARRAY_SIZE(sw_rev_map)) {
		p_rev = (struct prod_rev_map_t *)&sw_rev_map[sw_rev];
	} else {
		return -EINVAL;
	}
	chip_info->product_id = prod_ver;
	chip_info->product_revision = prod_rev;
	chip_info->silicon_revision = p_rev->silicon_rev;
	chip_info->software_revision = sw_rev;
	chip_info->gyro_sens_trim = p_rev->gyro_trim;
	chip_info->accl_sens_trim = p_rev->accel_trim;
	if (chip_info->accl_sens_trim == 0)
		chip_info->accl_sens_trim = DEFAULT_ACCL_TRIM;
	chip_info->multi = DEFAULT_ACCL_TRIM / chip_info->accl_sens_trim;
	if (chip_info->multi != 1)
		pr_info("multi is %d\n", chip_info->multi);
	return result;
}

/**
 *  read_accel_hw_self_test_prod_shift()- read the accelerometer hardware
 *                                         self-test bias shift calculated
 *                                         during final production test and
 *                                         stored in chip non-volatile memory.
 *  @st:  main data structure.
 *  @st_prod:   A pointer to an array of 3 elements to hold the values
 *              for production hardware self-test bias shifts returned to the
 *              user.
 *  @accl_sens: accel sensitivity.
 */
static int read_accel_hw_self_test_prod_shift(struct inv_mpu_iio_s *st,
					int *st_prod, int *accl_sens)
{
	u8 regs[4];
	u8 shift_code[3];
	int result, i;

	for (i = 0; i < 3; i++)
		st_prod[i] = 0;

	result = inv_plat_read(st, REG_ST_GCT_X, ARRAY_SIZE(regs), regs);

	if (result)
		return result;
	if ((0 == regs[0])  && (0 == regs[1]) &&
	    (0 == regs[2]) && (0 == regs[3]))
		return -EINVAL;
	shift_code[X] = ((regs[0] & 0xE0) >> 3) | ((regs[3] & 0x30) >> 4);
	shift_code[Y] = ((regs[1] & 0xE0) >> 3) | ((regs[3] & 0x0C) >> 2);
	shift_code[Z] = ((regs[2] & 0xE0) >> 3) |  (regs[3] & 0x03);
	for (i = 0; i < 3; i++) {
		if (shift_code[i] != 0)
			st_prod[i] = accl_sens[i] *
				accl_st_tb[shift_code[i] - 1];
	}

	return 0;
}

/**
* inv_check_accl_self_test()- check accel self test. this function returns
*                              zero as success. A non-zero return value
*                              indicates failure in self test.
*  @*st: main data structure.
*  @*reg_avg: average value of normal test.
*  @*st_avg:  average value of self test
*/
static int inv_check_accl_self_test(struct inv_mpu_iio_s *st,
	int *reg_avg, int *st_avg){
	int gravity, reg_z_avg, g_z_sign, j, ret_val;
	int tmp;
	int st_shift_prod[THREE_AXIS], st_shift_cust[THREE_AXIS];
	int st_shift_ratio[THREE_AXIS];
	int accel_sens[THREE_AXIS];

	if (st->chip_info.software_revision < DEF_OLDEST_SUPP_SW_REV &&
	    st->chip_info.product_revision < DEF_OLDEST_SUPP_PROD_REV)
		return 0;
	g_z_sign = 1;
	ret_val = 0;
	tmp = DEF_ST_SCALE * DEF_ST_PRECISION / DEF_ST_ACCEL_FS_MG;
	for (j = 0; j < 3; j++)
		accel_sens[j] = tmp;

	if (MPL_PROD_KEY(st->chip_info.product_id,
			 st->chip_info.product_revision) ==
	    MPU_PRODUCT_KEY_B1_E1_5) {
		/* half sensitivity Z accelerometer parts */
		accel_sens[Z] /= 2;
	} else {
		/* half sensitivity X, Y, Z accelerometer parts */
		accel_sens[X] /= st->chip_info.multi;
		accel_sens[Y] /= st->chip_info.multi;
		accel_sens[Z] /= st->chip_info.multi;
	}
	gravity = accel_sens[Z];
	reg_z_avg = reg_avg[Z] - g_z_sign * gravity * DEF_ST_PRECISION;
	ret_val = read_accel_hw_self_test_prod_shift(st, st_shift_prod,
							accel_sens);
	if (ret_val)
		return ret_val;

	for (j = 0; j < 3; j++) {
		st_shift_cust[j] = abs(reg_avg[j] - st_avg[j]);
		if (st_shift_prod[j]) {
			tmp = st_shift_prod[j] / DEF_ST_PRECISION;
			st_shift_ratio[j] = abs(st_shift_cust[j] / tmp
				- DEF_ST_PRECISION);
			if (st_shift_ratio[j] > DEF_ACCEL_ST_SHIFT_DELTA)
				ret_val = 1;
		} else {
			if (st_shift_cust[j] <
				DEF_ACCEL_ST_SHIFT_MIN * gravity)
				ret_val = 1;
			if (st_shift_cust[j] >
				DEF_ACCEL_ST_SHIFT_MAX * gravity)
				ret_val = 1;
		}
	}

	return ret_val;
}

/**
* inv_check_3500_gyro_self_test() check gyro self test. this function returns
*                                 zero as success. A non-zero return value
*                                 indicates failure in self test.
*  @*st: main data structure.
*  @*reg_avg: average value of normal test.
*  @*st_avg:  average value of self test
*/

static int inv_check_3500_gyro_self_test(struct inv_mpu_iio_s *st,
	int *reg_avg, int *st_avg){
	int result;
	int gst[3], ret_val;
	int gst_otp[3], i;
	u8 st_code[THREE_AXIS];
	ret_val = 0;

	for (i = 0; i < 3; i++)
		gst[i] = st_avg[i] - reg_avg[i];
	result = inv_plat_read(st, REG_3500_OTP, THREE_AXIS, st_code);
	if (result)
		return result;
	gst_otp[0] = 0;
	gst_otp[1] = 0;
	gst_otp[2] = 0;
	for (i = 0; i < 3; i++) {
		if (st_code[i] != 0)
			gst_otp[i] = gyro_3500_st_tb[st_code[i] - 1];
	}
	/* check self test value passing criterion. Using the DEF_ST_TOR
	 * for certain degree of tolerance */
	for (i = 0; i < 3; i++) {
		if (gst_otp[i] == 0) {
			if (abs(gst[i]) * DEF_ST_TOR < DEF_ST_OTP0_THRESH *
							DEF_ST_PRECISION *
							DEF_GYRO_SCALE)
				ret_val |= (1 << i);
		} else {
			if (abs(gst[i]/gst_otp[i] - DEF_ST_PRECISION) >
					DEF_GYRO_CT_SHIFT_DELTA)
				ret_val |= (1 << i);
		}
	}
	/* check for absolute value passing criterion. Using DEF_ST_TOR
	 * for certain degree of tolerance */
	for (i = 0; i < 3; i++) {
		if (abs(reg_avg[i]) > DEF_ST_TOR * DEF_ST_ABS_THRESH *
		    DEF_ST_PRECISION * DEF_GYRO_SCALE)
			ret_val |= (1 << i);
	}

	return ret_val;
}

/**
* inv_check_6050_gyro_self_test() - check 6050 gyro self test. this function
*                                   returns zero as success. A non-zero return
*                                   value indicates failure in self test.
*  @*st: main data structure.
*  @*reg_avg: average value of normal test.
*  @*st_avg:  average value of self test
*/
static int inv_check_6050_gyro_self_test(struct inv_mpu_iio_s *st,
	int *reg_avg, int *st_avg){
	int result;
	int ret_val;
	int st_shift_prod[3], st_shift_cust[3], st_shift_ratio[3], i;
	u8 regs[3];

	if (st->chip_info.software_revision < DEF_OLDEST_SUPP_SW_REV &&
	    st->chip_info.product_revision < DEF_OLDEST_SUPP_PROD_REV)
		return 0;

	ret_val = 0;
	result = inv_plat_read(st, REG_ST_GCT_X, 3, regs);
	if (result)
		return result;
	regs[X] &= 0x1f;
	regs[Y] &= 0x1f;
	regs[Z] &= 0x1f;
	for (i = 0; i < 3; i++) {
		if (regs[i] != 0)
			st_shift_prod[i] = gyro_6050_st_tb[regs[i] - 1];
		else
			st_shift_prod[i] = 0;
	}
	st_shift_prod[1] = -st_shift_prod[1];

	for (i = 0; i < 3; i++) {
		st_shift_cust[i] =  st_avg[i] - reg_avg[i];
		if (st_shift_prod[i]) {
			st_shift_ratio[i] = abs(st_shift_cust[i] /
				st_shift_prod[i] - DEF_ST_PRECISION);
			if (st_shift_ratio[i] > DEF_GYRO_CT_SHIFT_DELTA)
				ret_val = 1;
		} else {
			if (st_shift_cust[i] < DEF_ST_PRECISION *
				DEF_GYRO_CT_SHIFT_MIN * DEF_SELFTEST_GYRO_SENS)
				ret_val = 1;
			if (st_shift_cust[i] > DEF_ST_PRECISION *
				DEF_GYRO_CT_SHIFT_MAX * DEF_SELFTEST_GYRO_SENS)
				ret_val = 1;
		}
	}
	/* check for absolute value passing criterion. Using DEF_ST_TOR
	 * for certain degree of tolerance */
	for (i = 0; i < 3; i++)
		if (abs(reg_avg[i]) > DEF_ST_TOR * DEF_ST_ABS_THRESH *
		    DEF_ST_PRECISION * DEF_GYRO_SCALE)
			ret_val = 1;

	return ret_val;
}

/**
* inv_check_6500_gyro_self_test() - check 6500 gyro self test. this function
*                                   returns zero as success. A non-zero return
*                                   value indicates failure in self test.
*  @*st: main data structure.
*  @*reg_avg: average value of normal test.
*  @*st_avg:  average value of self test
*/
static int inv_check_6500_gyro_self_test(struct inv_mpu_iio_s *st,
	int *reg_avg, int *st_avg)
{
	u8 regs[3];
	int ret_val, result;
	int otp_value_zero = 0;
	int st_shift_prod[3], st_shift_cust[3], i;

	ret_val = 0;
	result = inv_plat_read(st, REG_6500_XG_ST_DATA, 3, regs);
	if (result)
		return result;
	pr_debug("%s self_test gyro shift_code - %02x %02x %02x\n",
		 st->hw->name, regs[0], regs[1], regs[2]);

	for (i = 0; i < 3; i++) {
		if (regs[i] != 0) {
			st_shift_prod[i] = mpu_6500_st_tb[regs[i] - 1];
		} else {
			st_shift_prod[i] = 0;
			otp_value_zero = 1;
		}
	}
	pr_debug("%s self_test gyro st_shift_prod - %+d %+d %+d\n",
		 st->hw->name, st_shift_prod[0], st_shift_prod[1],
		 st_shift_prod[2]);

	for (i = 0; i < 3; i++) {
		st_shift_cust[i] = st_avg[i] - reg_avg[i];
		if (!otp_value_zero) {
			/* Self Test Pass/Fail Criteria A */
			if (st_shift_cust[i] < DEF_6500_GYRO_CT_SHIFT_DELTA
						* st_shift_prod[i])
					ret_val = 1;
		} else {
			/* Self Test Pass/Fail Criteria B */
			if (st_shift_cust[i] < DEF_GYRO_ST_AL *
						DEF_SELFTEST_GYRO_SENS *
						DEF_ST_PRECISION)
				ret_val = 1;
		}
	}
	pr_debug("%s self_test gyro st_shift_cust - %+d %+d %+d\n",
		 st->hw->name, st_shift_cust[0], st_shift_cust[1],
		 st_shift_cust[2]);

	if (ret_val == 0) {
		/* Self Test Pass/Fail Criteria C */
		for (i = 0; i < 3; i++)
			if (abs(reg_avg[i]) > DEF_GYRO_OFFSET_MAX *
						DEF_SELFTEST_GYRO_SENS *
						DEF_ST_PRECISION)
				ret_val = 1;
	}

	return ret_val;
}

/**
* inv_check_6500_accel_self_test() - check 6500 accel self test. this function
*                                   returns zero as success. A non-zero return
*                                   value indicates failure in self test.
*  @*st: main data structure.
*  @*reg_avg: average value of normal test.
*  @*st_avg:  average value of self test
*/
static int inv_check_6500_accel_self_test(struct inv_mpu_iio_s *st,
						int *reg_avg, int *st_avg) {
	int ret_val, result;
	int st_shift_prod[3], st_shift_cust[3], st_shift_ratio[3], i;
	u8 regs[3];
	int otp_value_zero = 0;

#define ACCEL_ST_AL_MIN ((DEF_ACCEL_ST_AL_MIN * DEF_ST_SCALE \
				 / DEF_ST_6500_ACCEL_FS_MG) * DEF_ST_PRECISION)
#define ACCEL_ST_AL_MAX ((DEF_ACCEL_ST_AL_MAX * DEF_ST_SCALE \
				 / DEF_ST_6500_ACCEL_FS_MG) * DEF_ST_PRECISION)

	ret_val = 0;
	result = inv_plat_read(st, REG_6500_XA_ST_DATA, 3, regs);
	if (result)
		return result;
	pr_debug("%s self_test accel shift_code - %02x %02x %02x\n",
		 st->hw->name, regs[0], regs[1], regs[2]);

	for (i = 0; i < 3; i++) {
		if (regs[i] != 0) {
			st_shift_prod[i] = mpu_6500_st_tb[regs[i] - 1];
		} else {
			st_shift_prod[i] = 0;
			otp_value_zero = 1;
		}
	}
	pr_debug("%s self_test accel st_shift_prod - %+d %+d %+d\n",
		 st->hw->name, st_shift_prod[0], st_shift_prod[1],
		 st_shift_prod[2]);

	if (!otp_value_zero) {
		/* Self Test Pass/Fail Criteria A */
		for (i = 0; i < 3; i++) {
			st_shift_cust[i] = st_avg[i] - reg_avg[i];
			st_shift_ratio[i] = abs(st_shift_cust[i] /
					st_shift_prod[i] - DEF_ST_PRECISION);
			if (st_shift_ratio[i] > DEF_6500_ACCEL_ST_SHIFT_DELTA)
				ret_val = 1;
		}
	} else {
		/* Self Test Pass/Fail Criteria B */
		for (i = 0; i < 3; i++) {
			st_shift_cust[i] = abs(st_avg[i] - reg_avg[i]);
			if (st_shift_cust[i] < ACCEL_ST_AL_MIN ||
					st_shift_cust[i] > ACCEL_ST_AL_MAX)
				ret_val = 1;
		}
	}
	pr_debug("%s self_test accel st_shift_cust - %+d %+d %+d\n",
		 st->hw->name, st_shift_cust[0], st_shift_cust[1],
		 st_shift_cust[2]);

	return ret_val;
}

/*
 *  inv_do_test() - do the actual test of self testing
 */
int inv_do_test(struct inv_mpu_iio_s *st, int self_test_flag,
		int *gyro_result, int *accl_result)
{
	struct inv_reg_map_s *reg;
	int result, i, j, packet_size;
	u8 data[BYTES_PER_SENSOR * 2], d;
	bool has_accl;
	int fifo_count, packet_count, ind, s;

	reg = &st->reg;
	has_accl = (st->chip_type != INV_ITG3500);
	if (has_accl)
		packet_size = BYTES_PER_SENSOR * 2;
	else
		packet_size = BYTES_PER_SENSOR;

	result = inv_plat_single_write(st, reg->int_enable, 0);
	if (result)
		return result;
	/* disable the sensor output to FIFO */
	result = inv_plat_single_write(st, reg->fifo_en, 0);
	if (result)
		return result;
	/* disable fifo reading */
	result = inv_plat_single_write(st, reg->user_ctrl, st->i2c_dis);
	if (result)
		return result;
	/* clear FIFO */
	result = inv_plat_single_write(st, reg->user_ctrl, BIT_FIFO_RST | st->i2c_dis);
	if (result)
		return result;
	/* setup parameters */
	result = inv_plat_single_write(st, reg->lpf, INV_FILTER_98HZ);
	if (result)
		return result;

	if (INV_MPU6500 == st->chip_type) {
		/* config accel LPF register for MPU6500 */
		result = inv_plat_single_write(st, REG_6500_ACCEL_CONFIG2,
						DEF_ST_MPU6500_ACCEL_LPF |
						BIT_FIFO_SIZE_1K);
		if (result)
			return result;
	}

	result = inv_plat_single_write(st, reg->sample_rate_div,
			DEF_SELFTEST_SAMPLE_RATE);
	if (result)
		return result;
	/* wait for the sampling rate change to stabilize */
	mdelay(INV_MPU_SAMPLE_RATE_CHANGE_STABLE);
	result = inv_plat_single_write(st, reg->gyro_config,
		self_test_flag | DEF_SELFTEST_GYRO_FS);
	if (result)
		return result;
	if (has_accl) {
		if (INV_MPU6500 == st->chip_type)
			d = DEF_SELFTEST_6500_ACCEL_FS;
		else
			d = DEF_SELFTEST_ACCEL_FS;
		d |= self_test_flag;
		result = inv_plat_single_write(st, reg->accl_config, d);
		if (result)
			return result;
	}
	/* wait for the output to get stable */
	if (self_test_flag) {
		if (INV_MPU6500 == st->chip_type)
			msleep(DEF_ST_6500_STABLE_TIME);
		else
			msleep(DEF_ST_STABLE_TIME);
	}

	/* enable FIFO reading */
	result = inv_plat_single_write(st, reg->user_ctrl, BIT_FIFO_EN | st->i2c_dis);
	if (result)
		return result;
	/* enable sensor output to FIFO */
	if (has_accl)
		d = BITS_GYRO_OUT | BIT_ACCEL_OUT;
	else
		d = BITS_GYRO_OUT;
	for (i = 0; i < THREE_AXIS; i++) {
		gyro_result[i] = 0;
		accl_result[i] = 0;
	}
	s = 0;
	while (s < st->self_test.samples) {
		result = inv_plat_single_write(st, reg->fifo_en, d);
		if (result)
			return result;
		mdelay(DEF_GYRO_WAIT_TIME);
		result = inv_plat_single_write(st, reg->fifo_en, 0);
		if (result)
			return result;

		result = inv_plat_read(st, reg->fifo_count_h,
					FIFO_COUNT_BYTE, data);
		if (result)
			return result;
		fifo_count = be16_to_cpup((__be16 *)(&data[0]));
		pr_debug("%s self_test fifo_count - %d\n",
			 st->hw->name, fifo_count);
		packet_count = fifo_count / packet_size;
		i = 0;
		while ((i < packet_count) && (s < st->self_test.samples)) {
			short vals[3];
			result = inv_plat_read(st, reg->fifo_r_w,
				packet_size, data);
			if (result)
				return result;
			ind = 0;
			if (has_accl) {
				for (j = 0; j < THREE_AXIS; j++) {
					vals[j] = (short)be16_to_cpup(
					    (__be16 *)(&data[ind + 2 * j]));
					accl_result[j] += vals[j];
				}
				ind += BYTES_PER_SENSOR;
				pr_debug(
				    "%s self_test accel data - %d %+d %+d %+d",
				    st->hw->name, s, vals[0], vals[1], vals[2]);
			}

			for (j = 0; j < THREE_AXIS; j++) {
				vals[j] = (short)be16_to_cpup(
					(__be16 *)(&data[ind + 2 * j]));
				gyro_result[j] += vals[j];
			}
			pr_debug("%s self_test gyro data - %d %+d %+d %+d",
				 st->hw->name, s, vals[0], vals[1], vals[2]);

			s++;
			i++;
		}
	}

	if (has_accl) {
		for (j = 0; j < THREE_AXIS; j++) {
			accl_result[j] = accl_result[j] / s;
			accl_result[j] *= DEF_ST_PRECISION;
		}
	}
	for (j = 0; j < THREE_AXIS; j++) {
		gyro_result[j] = gyro_result[j] / s;
		gyro_result[j] *= DEF_ST_PRECISION;
	}

	return 0;
}

/*
 *  inv_recover_setting() recover the old settings after everything is done
 */
void inv_recover_setting(struct inv_mpu_iio_s *st)
{
	struct inv_reg_map_s *reg;
	int data;

	reg = &st->reg;
	inv_plat_single_write(st, reg->gyro_config,
			     st->chip_config.fsr << GYRO_CONFIG_FSR_SHIFT);
	inv_plat_single_write(st, reg->lpf, st->chip_config.lpf);
	data = ONE_K_HZ/st->chip_config.new_fifo_rate - 1;
	inv_plat_single_write(st, reg->sample_rate_div, data);
	if (INV_ITG3500 != st->chip_type) {
		inv_plat_single_write(st, reg->accl_config,
				     (st->chip_config.accl_fs <<
				     ACCL_CONFIG_FSR_SHIFT));
	}
	st->switch_gyro_engine(st, false);
	st->switch_accl_engine(st, false);
	st->set_power_state(st, false);
}

/*
 *  inv_resume_recover_setting() recover the old settings after from hw powerdown
 */
void inv_resume_recover_setting(struct inv_mpu_iio_s *st)
{
	struct inv_reg_map_s *reg;
	int data;

	reg = &st->reg;
	inv_plat_single_write(st, reg->gyro_config,
				st->chip_config.fsr << GYRO_CONFIG_FSR_SHIFT);
	inv_plat_single_write(st, reg->lpf, st->chip_config.lpf);
	data = ONE_K_HZ / st->chip_config.new_fifo_rate - 1;
	inv_plat_single_write(st, reg->sample_rate_div, data);
	if (INV_ITG3500 != st->chip_type) {
		inv_plat_single_write(st, reg->accl_config,
					(st->chip_config.accl_fs <<
					ACCL_CONFIG_FSR_SHIFT));
	}
}

static int inv_check_compass_self_test(struct inv_mpu_iio_s *st)
{
	int result;
	u8 data[6];
	u8 counter, cntl;
	short x, y, z;
	u8 *sens;
	sens = st->chip_info.compass_sens;

	/* set to bypass mode */
	result = inv_plat_single_write(st, REG_INT_PIN_CFG,
				st->plat_data.int_config | BIT_BYPASS_EN);
	if (result) {
		result = inv_plat_single_write(st, REG_INT_PIN_CFG,
				st->plat_data.int_config);
		return result;
	}
	/* set to power down mode */
	result = inv_secondary_write(st, REG_AKM_MODE, DATA_AKM_MODE_PD);
	if (result)
		goto AKM_fail;

	/* write 1 to ASTC register */
	result = inv_secondary_write(st, REG_AKM_ST_CTRL, DATA_AKM_SELF_TEST);
	if (result)
		goto AKM_fail;
	/* set self test mode */
	result = inv_secondary_write(st, REG_AKM_MODE, DATA_AKM_MODE_ST);
	if (result)
		goto AKM_fail;
	counter = DEF_ST_COMPASS_TRY_TIMES;
	while (counter > 0) {
		usleep_range(DEF_ST_COMPASS_WAIT_MIN, DEF_ST_COMPASS_WAIT_MAX);
		result = inv_secondary_read(st, REG_AKM_STATUS, 1, data);
		if (result)
			goto AKM_fail;
		if ((data[0] & DATA_AKM_DRDY) == 0)
			counter--;
		else
			counter = 0;
	}
	if ((data[0] & DATA_AKM_DRDY) == 0) {
		result = -EINVAL;
		goto AKM_fail;
	}
	result = inv_secondary_read(st, REG_AKM_MEASURE_DATA,
					BYTES_PER_SENSOR, data);
	if (result)
		goto AKM_fail;

	x = le16_to_cpup((__le16 *)(&data[0]));
	y = le16_to_cpup((__le16 *)(&data[2]));
	z = le16_to_cpup((__le16 *)(&data[4]));
	x = ((x * (sens[0] + 128)) >> 8);
	y = ((y * (sens[1] + 128)) >> 8);
	z = ((z * (sens[2] + 128)) >> 8);
	if (COMPASS_ID_AK8963 == st->plat_data.sec_slave_id) {
		result = inv_secondary_read(st, REG_AKM8963_CNTL1, 1, &cntl);
		if (result)
			goto AKM_fail;
		if (0 == (cntl & DATA_AKM8963_BIT)) {
			x <<= DEF_ST_COMPASS_8963_SHIFT;
			y <<= DEF_ST_COMPASS_8963_SHIFT;
			z <<= DEF_ST_COMPASS_8963_SHIFT;
		}
	}
	result = -EINVAL;
	if (x > st->compass_st_upper[X] || x < st->compass_st_lower[X])
		goto AKM_fail;
	if (y > st->compass_st_upper[Y] || y < st->compass_st_lower[Y])
		goto AKM_fail;
	if (z > st->compass_st_upper[Z] || z < st->compass_st_lower[Z])
		goto AKM_fail;
	result = 0;
AKM_fail:
	/*write 0 to ASTC register */
	result |= inv_secondary_write(st, REG_AKM_ST_CTRL, 0);
	/*set to power down mode */
	result |= inv_secondary_write(st, REG_AKM_MODE, DATA_AKM_MODE_PD);
	/*restore to non-bypass mode */
	result |= inv_plat_single_write(st, REG_INT_PIN_CFG,
			st->plat_data.int_config);
	return result;
}

int inv_power_up_self_test(struct inv_mpu_iio_s *st)
{
	int result;

	result = st->set_power_state(st, true);
	if (result)
		return result;
	result = st->switch_accl_engine(st, true);
	if (result)
		return result;
	result = st->switch_gyro_engine(st, true);
	if (result)
		return result;

	return 0;
}

/*
 *  inv_hw_self_test() - main function to do hardware self test
 */
int inv_hw_self_test(struct inv_mpu_iio_s *st)
{
	int result;
	int gyro_bias_st[THREE_AXIS], gyro_bias_regular[THREE_AXIS];
	int accl_bias_st[THREE_AXIS], accl_bias_regular[THREE_AXIS];
	int test_times;
	char compass_result, accel_result, gyro_result;

	result = inv_power_up_self_test(st);
	if (result)
		return result;
	compass_result = 0;
	accel_result   = 0;
	gyro_result    = 0;
	test_times = DEF_ST_TRY_TIMES;
	while (test_times > 0) {
		result = inv_do_test(st, 0, gyro_bias_regular,
			accl_bias_regular);
		if (result == -EAGAIN)
			test_times--;
		else
			test_times = 0;
	}
	if (result)
		goto test_fail;

	test_times = DEF_ST_TRY_TIMES;
	while (test_times > 0) {
		result = inv_do_test(st, BITS_SELF_TEST_EN, gyro_bias_st,
					accl_bias_st);
		if (result == -EAGAIN)
			test_times--;
		else
			break;
	}
	if (result)
		goto test_fail;
	if (st->chip_type == INV_ITG3500) {
		gyro_result = !inv_check_3500_gyro_self_test(st,
			gyro_bias_regular, gyro_bias_st);
	} else {
		if (st->chip_config.has_compass)
			compass_result = !inv_check_compass_self_test(st);

		 if (INV_MPU6050 == st->chip_type) {
			accel_result = !inv_check_accl_self_test(st,
				accl_bias_regular, accl_bias_st);
			gyro_result = !inv_check_6050_gyro_self_test(st,
				gyro_bias_regular, gyro_bias_st);
		} else if (INV_MPU6500 == st->chip_type) {
			accel_result = !inv_check_6500_accel_self_test(st,
				accl_bias_regular, accl_bias_st);
			gyro_result = !inv_check_6500_gyro_self_test(st,
				gyro_bias_regular, gyro_bias_st);
		}
	}
test_fail:
	inv_recover_setting(st);

	return (compass_result << DEF_ST_COMPASS_RESULT_SHIFT) |
		(accel_result << DEF_ST_ACCEL_RESULT_SHIFT) | gyro_result;
}

static int inv_load_firmware(struct inv_mpu_iio_s *st,
	u8 *data, int size)
{
	int bank, write_size;
	int result;
	u16 memaddr;

	/* Write and verify memory */
	for (bank = 0; size > 0; bank++,
		size -= write_size,
		data += write_size) {
		if (size > MPU_MEM_BANK_SIZE)
			write_size = MPU_MEM_BANK_SIZE;
		else
			write_size = size;

		memaddr = ((bank << 8) | 0x00);

		result = mem_w(memaddr, write_size, data);
		if (result)
			return result;
	}
	return 0;
}

static int inv_verify_firmware(struct inv_mpu_iio_s *st,
	u8 *data, int size)
{
	int bank, write_size;
	int result;
	u16 memaddr;
	u8 firmware[MPU_MEM_BANK_SIZE];

	/* Write and verify memory */
	for (bank = 0; size > 0; bank++,
		size -= write_size,
		data += write_size) {
		if (size > MPU_MEM_BANK_SIZE)
			write_size = MPU_MEM_BANK_SIZE;
		else
			write_size = size;

		memaddr = ((bank << 8) | 0x00);
		result = mpu_memory_read(st,
			st->i2c_addr, memaddr, write_size, firmware);
		if (result)
			return result;
		if (0 != memcmp(firmware, data, write_size))
			return -EINVAL;
	}
	return 0;
}

static int inv_set_fifo_div(struct inv_mpu_iio_s *st,
		u16 fifoRate)
{
	u8 regs[2];
	int result = 0;
	/*For some reason DINAC4 is defined as 0xb8, but DINBC4 is not*/
	const u8 regs_end[] = {DINAFE, DINAF2, DINAAB, 0xc4,
					DINAAA, DINAF1, DINADF, DINADF,
					0xbb, 0xaf, DINADF, DINADF};

	regs[0] = (u8)((fifoRate >> 8) & 0xff);
	regs[1] = (u8)(fifoRate & 0xff);
	result = mem_w_key(KEY_D_0_22, ARRAY_SIZE(regs), regs);
	if (result)
		return result;

	/*Modify the FIFO handler to reset the tap/orient interrupt flags*/
	/* each time the FIFO handler runs*/
	result = mem_w_key(KEY_CFG_6, ARRAY_SIZE(regs_end), regs_end);

	return result;
}

int inv_send_quaternion(struct inv_mpu_iio_s *st, bool on)
{
	const u8 regs_on[] = {DINBC0, DINBC2,
					 DINBC4, DINBC6};
	const u8 regs_off[] = {DINA80, DINA80,
					  DINA80, DINA80};
	const u8 *regs;
	u8 result;
	if (on)
		regs = regs_on;
	else
		regs = regs_off;
	result = mem_w_key(KEY_CFG_LP_QUAT, ARRAY_SIZE(regs_on), regs);

	return result;
}

int inv_set_display_orient_interrupt_dmp(struct inv_mpu_iio_s *st,
						bool on)
{
	/*Turn on the display orientation interrupt in the DMP*/
	int result;
	u8  regs[] = {0xd8};

	if (on)
		regs[0] = 0xd9;
	result = mem_w_key(KEY_CFG_DISPLAY_ORIENT_INT, 1, regs);
	return result;
}

int inv_set_fifo_rate(struct inv_mpu_iio_s *st, u16 fifo_rate)
{
	u8 divider;
	int result;

	divider = (u8)(ONE_K_HZ / fifo_rate) - 1;
	if (divider > DMP_MAX_DIVIDER) {
		st->sample_divider = DMP_MAX_DIVIDER;
		st->fifo_divider =
			(u8)(DMP_DEFAULT_FIFO_RATE / fifo_rate) - 1;
	} else {
		st->sample_divider = divider;
		st->fifo_divider = 0;
	}

	result = inv_set_fifo_div(st, st->fifo_divider);
	return result;
}

static int inv_set_tap_interrupt_dmp(struct inv_mpu_iio_s *st,
	u8 on)
{
	int result;
	u8  regs[] = {0};

	if (on)
		regs[0] = 0xf8;
	else
		regs[0] = DINAD8;
	result = mem_w_key(KEY_CFG_20, ARRAY_SIZE(regs), regs);
	if (result)
		return result;
	return result;
}

int inv_set_tap_threshold_dmp(struct inv_mpu_iio_s *st,
				u32 axis, u16 threshold)
{
	/* Sets the tap threshold in the dmp
	Simultaneously sets secondary tap threshold to help correct the tap
	direction for soft taps */
	int result;
	/* DMP Algorithm */
	u8 data[2];
	int sampleDivider;
	int scaledThreshold;
	u32 dmpThreshold;
	u8 sample_div;
	const u32  accel_sens = (0x20000000 / 0x00010000);

	if ((axis & ~(INV_TAP_AXIS_ALL)) || (threshold > (1 << 15)))
		return -EINVAL;
	sample_div = st->sample_divider;

	sampleDivider = (1 + sample_div);
	/* Scale factor corresponds linearly using
	* 0  : 0
	* 25 : 0.0250  g/ms
	* 50 : 0.0500  g/ms
	* 100: 1.0000  g/ms
	* 200: 2.0000  g/ms
	* 400: 4.0000  g/ms
	* 800: 8.0000  g/ms
	*/
	/*multiply by 1000 to avoid floating point 1000/1000*/
	scaledThreshold = threshold;
	/* Convert to per sample */
	scaledThreshold *= sampleDivider;

	/* Scale to DMP 16 bit value */
	if (accel_sens != 0)
		dmpThreshold = (u32)(scaledThreshold * accel_sens);
	else
		return -EINVAL;
	dmpThreshold = dmpThreshold / DMP_PRECISION;

	data[0] = dmpThreshold >> 8;
	data[1] = dmpThreshold & 0xFF;

	/* MPL algorithm */
	if (axis & INV_TAP_AXIS_X) {
		result = mem_w_key(KEY_DMP_TAP_THR_X, ARRAY_SIZE(data), data);
		if (result)
			return result;

		/*Also set additional threshold for correcting the direction
		of taps that were very near the threshold. */
		data[0] = (dmpThreshold * 3 / 4) >> 8;
		data[1] = (dmpThreshold * 3 / 4) & 0xFF;
		result = mem_w_key(KEY_D_1_36, ARRAY_SIZE(data), data);
		if (result)
			return result;
	}
	if (axis & INV_TAP_AXIS_Y) {
		result = mem_w_key(KEY_DMP_TAP_THR_Y, 2, data);
		if (result)
			return result;
		data[0] = (dmpThreshold * 3 / 4) >> 8;
		data[1] = (dmpThreshold * 3 / 4) & 0xFF;

		result = mem_w_key(KEY_D_1_40, ARRAY_SIZE(data), data);
		if (result)
			return result;
	}
	if (axis & INV_TAP_AXIS_Z) {
		result = mem_w_key(KEY_DMP_TAP_THR_Z, ARRAY_SIZE(data), data);
		if (result)
			return result;
		data[0] = (dmpThreshold * 3 / 4) >> 8;
		data[1] = (dmpThreshold * 3 / 4) & 0xFF;

		result = mem_w_key(KEY_D_1_44, ARRAY_SIZE(data), data);
		if (result)
			return result;
	}
	return 0;
}

static int inv_set_tap_axes_dmp(struct inv_mpu_iio_s *st,
				u32 axes)
{
	/* Sets a mask in the DMP that indicates what tap events
	should result in an interrupt */
	u8 regs[4];
	u8 result;

	/* check if any spurious bit other the ones expected are set */
	if (axes & (~(INV_TAP_ALL_DIRECTIONS)))
		return -EINVAL;

	regs[0] = (u8)axes;
	result = mem_w_key(KEY_D_1_72, 1, regs);

	return result;
}

int inv_set_min_taps_dmp(struct inv_mpu_iio_s *st,
				u16 min_taps) {
	/*Indicates the minimum number of consecutive taps required
		before the DMP will generate an interrupt */
	u8 regs[1];
	u8 result;
	/* check if any spurious bit other the ones expected are set */
	if ((min_taps > DMP_MAX_MIN_TAPS) || (min_taps < 1))
		return -EINVAL;
	regs[0] = (u8)(min_taps-1);
	result = mem_w_key(KEY_D_1_79, ARRAY_SIZE(regs), regs);

	return result;
}

int  inv_set_tap_time_dmp(struct inv_mpu_iio_s *st, u16 time)
{
	/* Determines how long after a tap the DMP requires before
	  another tap can be registered*/
	int result;
	/* DMP Algorithm */
	u16 dmpTime;
	u8 data[2];
	u8 sampleDivider;

	sampleDivider = st->sample_divider;
	sampleDivider++;

	/* 60 ms minimum time added */
	dmpTime = ((time) / sampleDivider);
	data[0] = dmpTime >> 8;
	data[1] = dmpTime & 0xFF;

	result = mem_w_key(KEY_DMP_TAPW_MIN, ARRAY_SIZE(data), data);

	return result;
}

static int inv_set_multiple_tap_time_dmp(struct inv_mpu_iio_s *st,
					u32 time)
{
	/*Determines how close together consecutive taps must occur
	to be considered double/triple taps*/
	int result;
	/* DMP Algorithm */
	u16 dmpTime;
	u8 data[2];
	u8 sampleDivider;

	sampleDivider = st->sample_divider;
	sampleDivider++;

	/* 60 ms minimum time added */
	dmpTime = ((time) / sampleDivider);
	data[0] = dmpTime >> 8;
	data[1] = dmpTime & 0xFF;
	result = mem_w_key(KEY_D_1_218, ARRAY_SIZE(data), data);

	return result;
}

int inv_q30_mult(int a, int b)
{
	u64 temp;
	int result;

	temp = (u64)a * b;
	result = (int)(temp >> DMP_MULTI_SHIFT);

	return result;
}

static u16 inv_row_2_scale(const s8 *row)
{
	u16 b;

	if (row[0] > 0)
		b = 0;
	else if (row[0] < 0)
		b = 4;
	else if (row[1] > 0)
		b = 1;
	else if (row[1] < 0)
		b = 5;
	else if (row[2] > 0)
		b = 2;
	else if (row[2] < 0)
		b = 6;
	else
		b = 7;

	return b;
}

/** Converts an orientation matrix made up of 0,+1,and -1 to a scalar
*	representation.
* @param[in] mtx Orientation matrix to convert to a scalar.
* @return Description of orientation matrix. The lowest 2 bits (0 and 1)
* represent the column the one is on for the
* first row, with the bit number 2 being the sign. The next 2 bits
* (3 and 4) represent
* the column the one is on for the second row with bit number 5 being
* the sign.
* The next 2 bits (6 and 7) represent the column the one is on for the
* third row with
* bit number 8 being the sign. In binary the identity matrix would therefor
* be: 010_001_000 or 0x88 in hex.
*/
static u16 inv_orientation_matrix_to_scaler(const signed char *mtx)
{

	u16 scalar;
	scalar = inv_row_2_scale(mtx);
	scalar |= inv_row_2_scale(mtx + 3) << 3;
	scalar |= inv_row_2_scale(mtx + 6) << 6;

	return scalar;
}
#if 0
static int inv_disable_gyro_cal(struct inv_mpu_iio_s *st)
{
	const u8 regs[] = {
		0xb8, 0xaa, 0xaa, 0xaa,
		0xb0, 0x88, 0xc3, 0xc5,
		0xc7
	};
	return mem_w_key(KEY_CFG_MOTION_BIAS, ARRAY_SIZE(regs), regs);
}
#endif

static int inv_gyro_dmp_cal(struct inv_mpu_iio_s *st)
{
	int inv_gyro_orient;
	u8 regs[3];
	int result;

	u8 tmpD = DINA4C;
	u8 tmpE = DINACD;
	u8 tmpF = DINA6C;

	inv_gyro_orient =
		inv_orientation_matrix_to_scaler(st->plat_data.orientation);

	if ((inv_gyro_orient & 3) == 0)
		regs[0] = tmpD;
	else if ((inv_gyro_orient & 3) == 1)
		regs[0] = tmpE;
	else if ((inv_gyro_orient & 3) == 2)
		regs[0] = tmpF;
	if ((inv_gyro_orient & 0x18) == 0)
		regs[1] = tmpD;
	else if ((inv_gyro_orient & 0x18) == 0x8)
		regs[1] = tmpE;
	else if ((inv_gyro_orient & 0x18) == 0x10)
		regs[1] = tmpF;
	if ((inv_gyro_orient & 0xc0) == 0)
		regs[2] = tmpD;
	else if ((inv_gyro_orient & 0xc0) == 0x40)
		regs[2] = tmpE;
	else if ((inv_gyro_orient & 0xc0) == 0x80)
		regs[2] = tmpF;

	result = mem_w_key(KEY_FCFG_1, ARRAY_SIZE(regs), regs);
	if (result)
		return result;

	if (inv_gyro_orient & 4)
		regs[0] = DINA36 | 1;
	else
		regs[0] = DINA36;
	if (inv_gyro_orient & 0x20)
		regs[1] = DINA56 | 1;
	else
		regs[1] = DINA56;
	if (inv_gyro_orient & 0x100)
		regs[2] = DINA76 | 1;
	else
		regs[2] = DINA76;

	result = mem_w_key(KEY_FCFG_3, ARRAY_SIZE(regs), regs);

	return result;
}

static int inv_accel_dmp_cal(struct inv_mpu_iio_s *st)
{
	int inv_accel_orient;
	int result;
	u8 regs[3];
	const u8 tmp[3] = { DINA0C, DINAC9, DINA2C };
	inv_accel_orient =
		inv_orientation_matrix_to_scaler(st->plat_data.orientation);

	regs[0] = tmp[inv_accel_orient & 3];
	regs[1] = tmp[(inv_accel_orient >> 3) & 3];
	regs[2] = tmp[(inv_accel_orient >> 6) & 3];
	result = mem_w_key(KEY_FCFG_2, ARRAY_SIZE(regs), regs);
	if (result)
		return result;

	regs[0] = DINA26;
	regs[1] = DINA46;
	regs[2] = DINA66;
	if (inv_accel_orient & 4)
		regs[0] |= 1;
	if (inv_accel_orient & 0x20)
		regs[1] |= 1;
	if (inv_accel_orient & 0x100)
		regs[2] |= 1;
	result = mem_w_key(KEY_FCFG_7, ARRAY_SIZE(regs), regs);

	return result;
}

static u16 inv_orientation_matrix_to_scalar(const s8 *mtx)
{

	u16 scalar;

	/*
       XYZ  010_001_000 Identity Matrix
       XZY  001_010_000
       YXZ  010_000_001
       YZX  000_010_001
       ZXY  001_000_010
       ZYX  000_001_010
	*/

	scalar = inv_row_2_scale(mtx);
	scalar |= inv_row_2_scale(mtx + 3) << 3;
	scalar |= inv_row_2_scale(mtx + 6) << 6;

	return scalar;
}

int inv_set_accel_bias_dmp(struct inv_mpu_iio_s *st)
{
	int inv_accel_orient, result, i, accel_bias_body[3], out[3];
	int tmp[] = {1, 1, 1};
	int mask[] = {4, 0x20, 0x100};
	int accel_sf = 0x20000000;/* 536870912 */
	u8 *regs;

	inv_accel_orient =
		inv_orientation_matrix_to_scalar(st->plat_data.orientation);

	for (i = 0; i < 3; i++)
		if (inv_accel_orient & mask[i])
			tmp[i] = -1;

	for (i = 0; i < 3; i++)
		accel_bias_body[i] = st->input_accel_bias[(inv_accel_orient >>
					(i * 3)) & 3] * tmp[i];
	for (i = 0; i < 3; i++)
		accel_bias_body[i] = inv_q30_mult(accel_sf,
					accel_bias_body[i]);
	for (i = 0; i < 3; i++)
		out[i] = cpu_to_be32p(&accel_bias_body[i]);
	regs = (u8 *)out;
	result = mem_w_key(KEY_D_ACCEL_BIAS, sizeof(out), regs);

	return result;
}

static int inv_set_gyro_sf_dmp(struct inv_mpu_iio_s *st)
{
	/*The gyro threshold, in dps, above which taps will be rejected*/
	int result;
	/* DMP Algorithm */
	u8 sampleDivider;
	u32 gyro_sf;
	const u32 gyro_sens = 0x03e80000;

	sampleDivider = st->sample_divider;
	gyro_sf = inv_q30_mult(gyro_sens,
			(int)(DMP_TAP_SCALE * (sampleDivider + 1)));
	result = write_be32_key_to_mem(st, gyro_sf, KEY_D_0_104);

	return result;
}

static int inv_set_shake_reject_thresh_dmp(struct inv_mpu_iio_s *st,
						int thresh)
{	/*THIS FUNCTION FAILS MEM_W*/
	/*The gyro threshold, in dps, above which taps will be rejected */
	int result;
	/* DMP Algorithm */
	u8 sampleDivider;
	int thresh_scaled;
	u32 gyro_sf;
	const u32 gyro_sens = 0x03e80000;
	sampleDivider = st->sample_divider;
	gyro_sf = inv_q30_mult(gyro_sens, (int)(DMP_TAP_SCALE *
			(sampleDivider + 1)));
	/* We're in units of DPS, convert it back to chip units*/
	/*split the operation to aviod overflow of integer*/
	thresh_scaled = gyro_sens / (1L << 16);
	thresh_scaled = thresh_scaled / thresh;
	thresh_scaled = gyro_sf / thresh_scaled;
	result = write_be32_key_to_mem(st, thresh_scaled, KEY_D_1_92);

	return result;
}

static int inv_set_shake_reject_time_dmp(struct inv_mpu_iio_s *st,
						u32 time)
{
	/* How long a gyro axis must remain above its threshold
	before taps are rejected */
	int result;
	/* DMP Algorithm */
	u16 dmpTime;
	u8 data[2];
	u8 sampleDivider;

	sampleDivider = st->sample_divider;
	sampleDivider++;

	/* 60 ms minimum time added */
	dmpTime = ((time) / sampleDivider);
	data[0] = dmpTime >> 8;
	data[1] = dmpTime & 0xFF;

	result = mem_w_key(KEY_D_1_88, ARRAY_SIZE(data), data);
	return result;
}

static int inv_set_shake_reject_timeout_dmp(struct inv_mpu_iio_s *st,
						u32 time)
{
	/*How long the gyros must remain below their threshold,
	after taps have been rejected, before taps can be detected again*/
	int result;
	/* DMP Algorithm */
	u16 dmpTime;
	u8 data[2];
	u8 sampleDivider;

	sampleDivider = st->sample_divider;
	sampleDivider++;

	/* 60 ms minimum time added */
	dmpTime = ((time) / sampleDivider);
	data[0] = dmpTime >> 8;
	data[1] = dmpTime & 0xFF;

	result = mem_w_key(KEY_D_1_90, ARRAY_SIZE(data), data);
	return result;
}

int inv_set_interrupt_on_gesture_event(struct inv_mpu_iio_s *st, bool on)
{
	u8 result;
	const u8 regs_on[] = {DINADA, DINAB1, DINAB9,
					 DINAF3, DINA8B, DINAA3, DINA91,
					 DINAB6, DINADA, DINAB4, DINADA};
	const u8 regs_off[] = {0xd8, 0xb1, 0xb9, 0xf3, 0x8b,
					  0xa3, 0x91, 0xb6, 0x09, 0xb4, 0xd9};
	/*For some reason DINAC4 is defined as 0xb8,
	but DINBC4 is not defined.*/
	const u8 regs_end[] = {DINAFE, DINAF2, DINAAB, 0xc4,
					DINAAA, DINAF1, DINADF, DINADF,
					0xbb, 0xaf, DINADF, DINADF};
	const u8 regs[] = {0, 0};
	/* reset fifo count to zero */
	result = mem_w_key(KEY_D_1_178, ARRAY_SIZE(regs), regs);
	if (result)
		return result;

	if (on)
		/*Sets the DMP to send an interrupt and put a FIFO packet
		in the FIFO if and only if a tap/orientation event
		just occurred*/
		result = mem_w_key(KEY_CFG_FIFO_ON_EVENT, ARRAY_SIZE(regs_on),
					regs_on);
	else
		/*Sets the DMP to send an interrupt and put a FIFO packet
		in the FIFO at the rate specified by the FIFO div.
		see inv_set_fifo_div in hw_setup.c to set the FIFO div.*/
		result = mem_w_key(KEY_CFG_FIFO_ON_EVENT, ARRAY_SIZE(regs_off),
					regs_off);
	if (result)
		return result;

	result = mem_w_key(KEY_CFG_6, ARRAY_SIZE(regs_end), regs_end);

	return result;
}

/**
 * inv_enable_tap_dmp() -  calling this function will enable/disable tap function.
 */
int inv_enable_tap_dmp(struct inv_mpu_iio_s *st, bool on)
{
	int result;

	result = inv_set_tap_interrupt_dmp(st, on);
	if (result)
		return result;
	if (on) {
		result = inv_set_tap_threshold_dmp(st, INV_TAP_AXIS_X,
						   st->tap.thresh);
		if (result)
			return result;
		result = inv_set_tap_threshold_dmp(st, INV_TAP_AXIS_Y,
						   st->tap.thresh);
		if (result)
			return result;
		result = inv_set_tap_threshold_dmp(st, INV_TAP_AXIS_Z,
						   st->tap.thresh);
		if (result)
			return result;
	}

	result = inv_set_tap_axes_dmp(st, INV_TAP_ALL_DIRECTIONS);
	if (result)
		return result;
	result = inv_set_min_taps_dmp(st, st->tap.min_count);
	if (result)
		return result;

	result = inv_set_tap_time_dmp(st, st->tap.time);
	if (result)
		return result;

	result = inv_set_multiple_tap_time_dmp(st, DMP_MULTI_TAP_TIME);
	if (result)
		return result;

	result = inv_set_gyro_sf_dmp(st);
	if (result)
		return result;

	result = inv_set_shake_reject_thresh_dmp(st, DMP_SHAKE_REJECT_THRESH);
	if (result)
		return result;

	result = inv_set_shake_reject_time_dmp(st, DMP_SHAKE_REJECT_TIME);
	if (result)
		return result;

	result = inv_set_shake_reject_timeout_dmp(st,
						  DMP_SHAKE_REJECT_TIMEOUT);
	return result;
}

int inv_send_sensor_data(struct inv_mpu_iio_s *st, u16 elements)
{
	int result;
	u8 regs[] = {DINAA0 + 3, DINAA0 + 3, DINAA0 + 3,
				DINAA0 + 3, DINAA0 + 3, DINAA0 + 3,
				DINAA0 + 3, DINAA0 + 3, DINAA0 + 3,
				DINAA0 + 3};

	if (elements & INV_ELEMENT_1)
		regs[0] = DINACA;
	if (elements & INV_ELEMENT_2)
		regs[4] = DINBC4;
	if (elements & INV_ELEMENT_3)
		regs[5] = DINACC;
	if (elements & INV_ELEMENT_4)
		regs[6] = DINBC6;
	if ((elements & INV_ELEMENT_5) || (elements & INV_ELEMENT_6) ||
	    (elements & INV_ELEMENT_7)) {
		regs[1] = DINBC0;
		regs[2] = DINAC8;
		regs[3] = DINBC2;
	}
	result = mem_w_key(KEY_CFG_15, ARRAY_SIZE(regs), regs);
	return result;
}

int inv_send_interrupt_word(struct inv_mpu_iio_s *st, bool on)
{
	const u8 regs_on[] = { DINA20 };
	const u8 regs_off[] = { DINAA3 };
	u8 result;

	if (on)
		result = mem_w_key(KEY_CFG_27, ARRAY_SIZE(regs_on), regs_on);
	else
		result = mem_w_key(KEY_CFG_27, ARRAY_SIZE(regs_off), regs_off);

	return result;
}

/**
 * inv_dmp_firmware_write() -  calling this function will load the firmware.
 *                        This is the write function of file "dmp_firmware".
 */
ssize_t inv_dmp_firmware_write(struct file *fp, struct kobject *kobj,
	struct bin_attribute *attr,
	char *buf, loff_t pos, size_t size)
{
	u8 *firmware;
	int result;
	struct inv_reg_map_s *reg;
	struct iio_dev *indio_dev;
	struct inv_mpu_iio_s *st;

	indio_dev = dev_get_drvdata(container_of(kobj, struct device, kobj));
	st = iio_priv(indio_dev);

	if (st->chip_config.firmware_loaded)
		return -EINVAL;
	if (st->chip_config.enable)
		return -EBUSY;

	reg = &st->reg;
	if (DMP_IMAGE_SIZE != size) {
		pr_err("wrong DMP image size\n");
		return -EINVAL;
	}

	firmware = kmalloc(size, GFP_KERNEL);
	if (!firmware)
		return -ENOMEM;

	mutex_lock(&indio_dev->mlock);

	memcpy(firmware, buf, size);
	result = crc32(CRC_FIRMWARE_SEED, firmware, size);
	if (DMP_IMAGE_CRC_VALUE != result) {
		pr_err("firmware CRC error - 0x%08x vs 0x%08x\n",
			result, DMP_IMAGE_CRC_VALUE);
		result = -EINVAL;
		goto firmware_write_fail;
	}

	result = st->set_power_state(st, true);
	if (result)
		goto firmware_write_fail;

	result = inv_load_firmware(st, firmware, size);
	if (result)
		goto firmware_write_fail;

	result = inv_verify_firmware(st, firmware, size);
	if (result)
		goto firmware_write_fail;

	result = inv_plat_single_write(st, reg->prgm_strt_addrh,
	st->chip_config.prog_start_addr >> 8);
	if (result)
		goto firmware_write_fail;
	result = inv_plat_single_write(st, reg->prgm_strt_addrh + 1,
	st->chip_config.prog_start_addr & 0xff);
	if (result)
		goto firmware_write_fail;

	result = inv_set_fifo_rate(st, DMP_DEFAULT_FIFO_RATE);
	if (result)
		goto firmware_write_fail;
	result = inv_gyro_dmp_cal(st);
	if (result)
		goto firmware_write_fail;
	result = inv_accel_dmp_cal(st);
	if (result)
		goto firmware_write_fail;
	/* result = inv_disable_gyro_cal(st);*/
	if (result)
		goto firmware_write_fail;

	st->chip_config.firmware_loaded = 1;

firmware_write_fail:
	result |= st->set_power_state(st, false);
	mutex_unlock(&indio_dev->mlock);
	kfree(firmware);
	if (result)
		return result;
	return size;
}

ssize_t inv_dmp_firmware_read(struct file *filp,
				struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t off, size_t count)
{
	int bank, write_size, size, data, result;
	u16 memaddr;
	struct iio_dev *indio_dev;
	struct inv_mpu_iio_s *st;

	size = count;
	indio_dev = dev_get_drvdata(container_of(kobj, struct device, kobj));
	st = iio_priv(indio_dev);

	data = 0;
	mutex_lock(&indio_dev->mlock);
	if (!st->chip_config.enable) {
		result = st->set_power_state(st, true);
		if (result) {
			mutex_unlock(&indio_dev->mlock);
			return result;
		}
	}
	for (bank = 0; size > 0; bank++, size -= write_size,
					data += write_size) {
		if (size > MPU_MEM_BANK_SIZE)
			write_size = MPU_MEM_BANK_SIZE;
		else
			write_size = size;

		memaddr = (bank << 8);
		result = mpu_memory_read(st,
			st->i2c_addr, memaddr, write_size, &buf[data]);
		if (result) {
			mutex_unlock(&indio_dev->mlock);
			return result;
		}
	}
	if (!st->chip_config.enable)
		result = st->set_power_state(st, false);
	mutex_unlock(&indio_dev->mlock);
	if (result)
		return result;

	return count;
}
/**
 *  @}
 */
