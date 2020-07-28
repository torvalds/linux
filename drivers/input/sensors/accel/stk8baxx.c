/*
 *  stk8baxx.c - Linux kernel modules for sensortek  stk8ba50 / stk8ba50-R /
 *  stk8ba53 accelerometer
 *
 *  Copyright (C) 2012~2016 Lex Hsieh / Sensortek
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
 */
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/math64.h>
#include <linux/init.h>
#include <linux/sensor-dev.h>

#define STK_ACC_DRIVER_VERSION	"3.7.1_rk_0425_0428"

/*------------------User-defined settings-------------------------*/
/* #define CONFIG_SENSORS_STK8BA53 */
#define CONFIG_SENSORS_STK8BA50
/* #define STK_DEBUG_PRINT */
/* #define STK_LOWPASS */
#define STK_FIR_LEN	4	/* 1~32 */
/* #define STK_TUNE */
/* #define STK_ZG_FILTER */
#define STK_HOLD_ODR
#define STK_DEBUG_CALI
#define STK8BAXX_DEF_PLACEMENT	7

/*------------------Miscellaneous settings-------------------------*/
#define STK8BAXX_I2C_NAME	"stk8baxx"
#define ACC_IDEVICE_NAME	"accelerometer"

#define STK8BAXX_INIT_ODR	0xD /* 0xB:125Hz, 0xA:62Hz */

#define STK8BAXX_RNG_2G			0x3
#define STK8BAXX_RNG_4G			0x5
#define STK8BAXX_RNG_8G			0x8
#define STK8BAXX_RNG_16G		0xC

#ifdef CONFIG_SENSORS_STK8BA53
/* Parameters under +-4g dynamic range */
#define STK_DEF_DYNAMIC_RANGE	STK8BAXX_RNG_4G

#if (STK_DEF_DYNAMIC_RANGE == STK8BAXX_RNG_4G)
#define STK_LSB_1G			512
#define STK_DEF_RANGE		4
#elif (STK_DEF_DYNAMIC_RANGE == STK8BAXX_RNG_2G)
#define STK_LSB_1G			1024
#define STK_DEF_RANGE		2
#elif (STK_DEF_DYNAMIC_RANGE == STK8BAXX_RNG_8G)
#define STK_LSB_1G			256
#define STK_DEF_RANGE		8
#elif (STK_DEF_DYNAMIC_RANGE == STK8BAXX_RNG_16G)
#define STK_LSB_1G			128
#define STK_DEF_RANGE		16
#endif

#define STK_ZG_COUNT		(STK_LSB_1G / 128)
#define STK_TUNE_XYOFFSET	(STK_LSB_1G * 3 / 10)
#define STK_TUNE_ZOFFSET	(STK_LSB_1G * 3 / 10) /* (STK_LSB_1G * 3 / 20) */
#define STK_TUNE_NOISE		(STK_LSB_1G / 10)
#else
/* Parameters under +-2g dynamic range */
#define STK_DEF_DYNAMIC_RANGE	STK8BAXX_RNG_2G

#if (STK_DEF_DYNAMIC_RANGE == STK8BAXX_RNG_2G)
#define STK_LSB_1G			256
#define STK_DEF_RANGE		2
#elif (STK_DEF_DYNAMIC_RANGE == STK8BAXX_RNG_4G)
#define STK_LSB_1G			128
#define STK_DEF_RANGE		4
#elif (STK_DEF_DYNAMIC_RANGE == STK8BAXX_RNG_8G)
#define STK_LSB_1G			64
#define STK_DEF_RANGE		8
#elif (STK_DEF_DYNAMIC_RANGE == STK8BAXX_RNG_16G)
#define STK_LSB_1G			32
#define STK_DEF_RANGE		16
#endif
#define STK_ZG_COUNT		(STK_LSB_1G / 128 + 1)
#define STK_TUNE_XYOFFSET	(STK_LSB_1G * 4 / 10)
#define STK_TUNE_ZOFFSET	(STK_LSB_1G * 4 / 10) /* (STK_LSB_1G * 3 / 20) */
#define STK_TUNE_NOISE		(STK_LSB_1G / 10)
#endif

#define STK8BAXX_RANGE_UG	(STK_DEF_RANGE * 16384)

/* STK_OFFSET_REG_LSB_1G is fixed for all dynamic range */
#define STK_OFFSET_REG_LSB_1G	128

#define STK_TUNE_NUM			60
#define STK_TUNE_DELAY			30

#define STK_EVENT_SINCE_EN_LIMIT_DEF	(1)

#define STK8BA50_ID				0x09
#define STK8BA50R_ID			0x86
#define STK8BA53_ID				0x87

/*------------------Calibration prameters-------------------------*/
#define STK_SAMPLE_NO				10
#define STK_ACC_CALI_VER0			0x18
#define STK_ACC_CALI_VER1			0x03
#define STK_ACC_CALI_END			'\0'
#define STK_ACC_CALI_FILE			"/data/misc/stkacccali.conf"
#define STK_ACC_CALI_FILE_SDCARD	"/sdcard/.stkacccali.conf"
#define STK_ACC_CALI_FILE_SIZE		25

#define STK_K_SUCCESS_TUNE		0x04
#define STK_K_SUCCESS_FT2			0x03
#define STK_K_SUCCESS_FT1			0x02
#define STK_K_SUCCESS_FILE			0x01
#define STK_K_NO_CALI				0xFF
#define STK_K_RUNNING				0xFE
#define STK_K_FAIL_LRG_DIFF			0xFD
#define STK_K_FAIL_OPEN_FILE		0xFC
#define STK_K_FAIL_W_FILE			0xFB
#define STK_K_FAIL_R_BACK			0xFA
#define STK_K_FAIL_R_BACK_COMP	0xF9
#define STK_K_FAIL_I2C				0xF8
#define STK_K_FAIL_K_PARA			0xF7
#define STK_K_FAIL_OUT_RG			0xF6
#define STK_K_FAIL_ENG_I2C			0xF5
#define STK_K_FAIL_FT1_USD			0xF4
#define STK_K_FAIL_FT2_USD			0xF3
#define STK_K_FAIL_WRITE_NOFST	0xF2
#define STK_K_FAIL_OTP_5T			0xF1
#define STK_K_FAIL_PLACEMENT		0xF0

/*------------------stk8baxx registers-------------------------*/
#define STK8BAXX_XOUT1				0x02
#define STK8BAXX_XOUT2				0x03
#define STK8BAXX_YOUT1				0x04
#define STK8BAXX_YOUT2				0x05
#define STK8BAXX_ZOUT1				0x06
#define STK8BAXX_ZOUT2				0x07
#define STK8BAXX_INTSTS1			0x09
#define STK8BAXX_INTSTS2			0x0A
#define STK8BAXX_EVENTINFO1		0x0B
#define STK8BAXX_EVENTINFO2		0x0C
#define STK8BAXX_RANGESEL			0x0F
#define STK8BAXX_BWSEL				0x10
#define STK8BAXX_POWMODE			0x11
#define STK8BAXX_DATASETUP		0x13
#define STK8BAXX_SWRST			0x14
#define STK8BAXX_INTEN1			0x16
#define STK8BAXX_INTEN2			0x17
#define STK8BAXX_INTMAP1			0x19
#define STK8BAXX_INTMAP2			0x1A
#define STK8BAXX_INTMAP3			0x1B
#define STK8BAXX_DATASRC			0x1E
#define STK8BAXX_INTCFG1			0x20
#define STK8BAXX_INTCFG2			0x21
#define STK8BAXX_LGDLY				0x22
#define STK8BAXX_LGTHD				0x23
#define STK8BAXX_HLGCFG			0x24
#define STK8BAXX_HGDLY				0x25
#define STK8BAXX_HGTHD				0x26
#define STK8BAXX_SLOPEDLY			0x27
#define STK8BAXX_SLOPETHD			0x28
#define STK8BAXX_TAPTIME			0x2A
#define STK8BAXX_TAPCFG			0x2B
#define STK8BAXX_ORIENTCFG		0x2C
#define STK8BAXX_ORIENTTHETA		0x2D
#define STK8BAXX_FLATTHETA			0x2E
#define STK8BAXX_FLATHOLD			0x2F
#define STK8BAXX_SLFTST			0x32
#define STK8BAXX_INTFCFG			0x34
#define STK8BAXX_OFSTCOMP1		0x36
#define STK8BAXX_OFSTCOMP2		0x37
#define STK8BAXX_OFSTFILTX			0x38
#define STK8BAXX_OFSTFILTY			0x39
#define STK8BAXX_OFSTFILTZ			0x3A
#define STK8BAXX_OFSTUNFILTX		0x3B
#define STK8BAXX_OFSTUNFILTY		0x3C
#define STK8BAXX_OFSTUNFILTZ		0x3D

/* ZOUT1 register */
#define STK8BAXX_O_NEW			0x01

/* SWRST register */
#define  STK8BAXX_SWRST_VAL		0xB6

/* STK8BAXX_POWMODE register */
#define STK8BAXX_MD_SUSPEND		0x80
#define STK8BAXX_MD_NORMAL		0x00
#define STK8BAXX_MD_SLP_MASK		0x1E

/* RANGESEL register */
#define STK8BAXX_RANGE_MASK		0x0F

/* OFSTCOMP1 register */
#define STK8BAXX_OF_CAL_DRY_MASK	0x10
#define CAL_AXIS_X_EN				0x20
#define CAL_AXIS_Y_EN				0x40
#define CAL_AXIS_Z_EN				0x60
#define CAL_OFST_RST				0x80

/* OFSTCOMP2 register */
#define CAL_TG_X0_Y0_ZPOS1			0x20
#define CAL_TG_X0_Y0_ZNEG1			0x40

/*no_create_attr:the initial is 1-->no create attr. if created, change no_create_att to 0.*/
static int no_create_att = 1;
static int enable_status = -1;

/*------------------Data structure-------------------------*/
struct stk8baxx_acc {
	union {
		struct {
			s16 x;
			s16 y;
			s16 z;
		};
		s16 acc[3];
	};
};

#if defined(STK_LOWPASS)
#define MAX_FIR_LEN 32
struct data_filter {
	s16 raw[MAX_FIR_LEN][3];
	int sum[3];
	int num;
	int idx;
};
#endif

struct stk8baxx_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	int irq;
	struct stk8baxx_acc acc_xyz;
	atomic_t enabled;
	bool first_enable;
	struct work_struct stk_work;
	struct hrtimer acc_timer;
	struct workqueue_struct *stk_mems_work_queue;
	unsigned char stk8baxx_placement;
	atomic_t cali_status;
	atomic_t recv_reg;
	bool re_enable;
#if defined(STK_LOWPASS)
	atomic_t                firlength;
	atomic_t                fir_en;
	struct data_filter      fir;
#endif
	int event_since_en;
	int event_since_en_limit;
	u8 stk_tune_offset_record[3];
#ifdef STK_TUNE
	int stk_tune_offset[3];
	int stk_tune_sum[3];
	int stk_tune_max[3];
	int stk_tune_min[3];
	int stk_tune_index;
	int stk_tune_done;
	s64 stk_tune_square_sum[3];
	u32 variance[3];
#endif
};

/*------------------Function prototype-------------------------*/
static int stk8baxx_set_enable(struct stk8baxx_data *stk, char en);
static int stk8baxx_read_sensor_data(struct stk8baxx_data *stk);
/*------------------Global variables-------------------------*/
static struct stk8baxx_data *stk8baxx_data_ptr;
static struct sensor_private_data *sensor_ptr;
/*------------------Main functions-------------------------*/

static s32 stk8baxx_smbus_write_byte_data(u8 command, u8 value)
{
	return sensor_write_reg(stk8baxx_data_ptr->client, command, value);
}

static int stk8baxx_smbus_read_byte_data(u8 command)
{
	return sensor_read_reg(stk8baxx_data_ptr->client, command);
}

static int stk8baxx_chk_for_addr(struct stk8baxx_data *stk, s32 org_address, unsigned short reset_address)
{
	int result;
	s32 expected_reg0 = 0x86;

	if ((org_address & 0xFE) == 0x18)
		expected_reg0 = 0x86;
	else
		expected_reg0 = 0x87;

	stk->client->addr = reset_address;
	result = stk8baxx_smbus_write_byte_data(STK8BAXX_SWRST, STK8BAXX_SWRST_VAL);
	printk(KERN_INFO "%s:issue sw reset to 0x%x, result=%d\n", __func__, reset_address, result);
	usleep_range(2000, 3000);

	stk->client->addr = org_address;
	printk(KERN_INFO "%s Revise I2C Address = 0x%x\n", __func__, org_address);
	result = stk8baxx_smbus_write_byte_data(STK8BAXX_POWMODE, STK8BAXX_MD_NORMAL);
	result = stk8baxx_smbus_read_byte_data(0x0);
	if (result < 0) {
		printk(KERN_INFO "%s: read 0x0, result=%d\n", __func__, result);
		return result;
	}
	if (result == expected_reg0) {
		printk(KERN_INFO "%s:passed, expected_reg0=0x%x\n", __func__, expected_reg0);
		result = stk8baxx_smbus_write_byte_data(STK8BAXX_SWRST, STK8BAXX_SWRST_VAL);
		if (result < 0) {
			printk(KERN_ERR "%s:failed to issue software reset, error=%d\n", __func__, result);
			return result;
		}
		usleep_range(2000, 3000);
		return 1;
	}
	return 0;
}

static int stk8baxx_sw_reset(struct stk8baxx_data *stk)
{
	unsigned short org_addr = 0;
	int result;

	org_addr = stk->client->addr;
	printk(KERN_INFO "%s:org_addr=0x%x\n", __func__, org_addr);

	if ((org_addr & 0xFE) == 0x18) {
		result = stk8baxx_chk_for_addr(stk, org_addr, 0x18);
		if (result == 1)
			return 0;
		result = stk8baxx_chk_for_addr(stk, org_addr, 0x19);
		if (result == 1)
			return 0;
		result = stk8baxx_chk_for_addr(stk, org_addr, 0x08);
		if (result == 1)
			return 0;
		result = stk8baxx_chk_for_addr(stk, org_addr, 0x28);
		if (result == 1)
			return 0;
	} else if (org_addr == 0x28) {
		result = stk8baxx_chk_for_addr(stk, org_addr, 0x28);
		if (result == 1)
			return 0;
		result = stk8baxx_chk_for_addr(stk, org_addr, 0x18);
		if (result == 1)
			return 0;
		result = stk8baxx_chk_for_addr(stk, org_addr, 0x08);
		if (result == 1)
			return 0;
	}
	result = stk8baxx_chk_for_addr(stk, org_addr, 0x0B);
	return 0;
}

static int stk8baxx_reg_init(struct stk8baxx_data *stk, struct i2c_client *client, struct sensor_private_data *sensor)
{
	int result;
	int aa;

#ifdef CONFIG_SENSORS_STK8BA53
	printk(KERN_INFO "%s: Initialize stk8ba53\n", __func__);
#else
	printk(KERN_INFO "%s: Initialize stk8ba50/stk8ba50-r\n", __func__);
#endif

	/* sw reset */
	result = stk8baxx_sw_reset(stk);
	if (result < 0) {
		printk(KERN_ERR "%s:failed to stk8baxx_sw_reset, error=%d\n", __func__, result);
		return result;
	}

	result = stk8baxx_smbus_write_byte_data(STK8BAXX_POWMODE, STK8BAXX_MD_NORMAL);
	if (result < 0) {
		printk(KERN_ERR "%s:failed to write reg 0x%x, error=%d\n", __func__, STK8BAXX_POWMODE, result);
		return result;
	}

	result = stk8baxx_smbus_read_byte_data(STK8BAXX_LGDLY);
	if (result < 0) {
		printk(KERN_ERR "%s: failed to read acc data, error=%d\n", __func__, result);
		return result;
	}

	if (result == STK8BA50_ID) {
		printk(KERN_INFO "%s: chip is stk8ba50\n", __func__);
		sensor->devid = STK8BA50_ID;
	} else {
		result = stk8baxx_smbus_read_byte_data(0x0);
		if (result < 0) {
			printk(KERN_ERR "%s: failed to read acc data, error=%d\n", __func__, result);
			return result;
		}
		printk(KERN_INFO "%s: 0x0=0x%x\n", __func__, result);
		if (result == STK8BA50R_ID) {
			printk(KERN_INFO "%s: chip is stk8ba50-R\n", __func__);
			sensor->devid = STK8BA50R_ID;
		} else {
			printk(KERN_INFO "%s: chip is stk8ba53\n", __func__);
			sensor->devid = STK8BA53_ID;
		}
	}
#ifdef CONFIG_SENSORS_STK8BA53
	if (sensor->devid != STK8BA53_ID) {
		printk(KERN_ERR "%s: stk8ba53 is not attached, devid=0x%x\n", __func__, sensor->devid);
		return -ENODEV;
	}
#else
	if (sensor->devid == STK8BA53_ID) {
		printk(KERN_ERR "%s: stk8ba50/stk8ba50-R is not attached, devid=0x%x\n", __func__, sensor->devid);
		return -ENODEV;
	}
#endif
	if (sensor->pdata->irq_enable) {
		/* map new data int to int1 */
		result = stk8baxx_smbus_write_byte_data(STK8BAXX_INTMAP2, 0x01);
		if (result < 0) {
			printk(KERN_ERR "%s:failed to write reg 0x%x, error=%d\n", __func__, STK8BAXX_INTMAP2, result);
			return result;
		}
		/* enable new data in */
		result = stk8baxx_smbus_write_byte_data(STK8BAXX_INTEN2, 0x10);
		if (result < 0) {
			printk(KERN_ERR "%s:failed to write reg 0x%x, error=%d\n", __func__, STK8BAXX_INTEN2, result);
			return result;
		}
		/* non-latch int */
		result = stk8baxx_smbus_write_byte_data(STK8BAXX_INTCFG2, 0x00);
		if (result < 0) {
			printk(KERN_ERR "%s:failed to write reg 0x%x, error=%d\n", __func__, STK8BAXX_INTCFG2, result);
			return result;
		}
		/* filtered data source for new data int */
		result = stk8baxx_smbus_write_byte_data(STK8BAXX_DATASRC, 0x00);
		if (result < 0) {
			printk(KERN_ERR "%s:failed to write reg 0x%x, error=%d\n", __func__, STK8BAXX_DATASRC, result);
			return result;
		}
		/* int1, push-pull, active high */
		result = stk8baxx_smbus_write_byte_data(STK8BAXX_INTCFG1, 0x01);
		if (result < 0) {
			printk(KERN_ERR "%s:failed to write reg 0x%x, error=%d\n", __func__, STK8BAXX_INTCFG1, result);
			return result;
		}
	}
#ifdef CONFIG_SENSORS_STK8BA53
	/* +- 4g */
	result = stk8baxx_smbus_write_byte_data(STK8BAXX_RANGESEL, STK_DEF_DYNAMIC_RANGE);
	if (result < 0) {
		printk(KERN_ERR "%s:failed to write reg 0x%x, error=%d\n", __func__, STK8BAXX_RANGESEL, result);
		return result;
	}
#else
	/* +- 2g */
	result = stk8baxx_smbus_write_byte_data(STK8BAXX_RANGESEL, STK_DEF_DYNAMIC_RANGE);
	if (result < 0) {
		printk(KERN_ERR "%s:failed to write reg 0x%x, error=%d\n", __func__, STK8BAXX_RANGESEL, result);
		return result;
	}
#endif
	/* ODR = 62 Hz */
	result = stk8baxx_smbus_write_byte_data(STK8BAXX_BWSEL, STK8BAXX_INIT_ODR);
	if (result < 0) {
		printk(KERN_ERR "%s:failed to write reg 0x%x, error=%d\n", __func__, STK8BAXX_BWSEL, result);
		return result;
	}

	/* i2c watchdog enable, 1 ms timer perios */
	result = stk8baxx_smbus_write_byte_data(STK8BAXX_INTFCFG, 0x04);
	if (result < 0) {
		printk(KERN_ERR "%s:failed to write reg 0x%x, error=%d\n", __func__, STK8BAXX_INTFCFG, result);
		return result;
	}

	result = stk8baxx_smbus_write_byte_data(STK8BAXX_POWMODE, STK8BAXX_MD_SUSPEND);
	if (result < 0) {
		printk(KERN_ERR "%s:failed to write reg 0x%x, error=%d\n", __func__, STK8BAXX_POWMODE, result);
		return result;
	}
	atomic_set(&stk->enabled, 0);
	stk->first_enable = true;
	atomic_set(&stk->cali_status, STK_K_NO_CALI);
	atomic_set(&stk->recv_reg, 0);
#ifdef STK_LOWPASS
	memset(&stk->fir, 0x00, sizeof(stk->fir));
	atomic_set(&stk->firlength, STK_FIR_LEN);
	atomic_set(&stk->fir_en, 1);
#endif

	for (aa = 0; aa < 3; aa++)
		stk->stk_tune_offset_record[aa] = 0;
#ifdef STK_TUNE
	for (aa = 0; aa < 3; aa++) {
		stk->stk_tune_offset[aa] = 0;
		stk->stk_tune_sum[aa] = 0;
		stk->stk_tune_max[aa] = 0;
		stk->stk_tune_min[aa] = 0;
		stk->stk_tune_square_sum[aa] = 0LL;
		stk->variance[aa] = 0;
	}
	stk->stk_tune_done = 0;
	stk->stk_tune_index = 0;
#endif
	stk->event_since_en_limit = STK_EVENT_SINCE_EN_LIMIT_DEF;

	return 0;
}

#ifdef STK_LOWPASS
static void stk8baxx_low_pass(struct stk8baxx_data *stk, struct stk8baxx_acc *acc_lp)
{
	int idx, firlength = atomic_read(&stk->firlength);
#ifdef STK_ZG_FILTER
	s16 zero_fir = 0;
#endif

	if (atomic_read(&stk->fir_en)) {
		if (stk->fir.num < firlength) {
			stk->fir.raw[stk->fir.num][0] = acc_lp->x;
			stk->fir.raw[stk->fir.num][1] = acc_lp->y;
			stk->fir.raw[stk->fir.num][2] = acc_lp->z;
			stk->fir.sum[0] += acc_lp->x;
			stk->fir.sum[1] += acc_lp->y;
			stk->fir.sum[2] += acc_lp->z;
			stk->fir.num++;
			stk->fir.idx++;
		} else {
			idx = stk->fir.idx % firlength;
			stk->fir.sum[0] -= stk->fir.raw[idx][0];
			stk->fir.sum[1] -= stk->fir.raw[idx][1];
			stk->fir.sum[2] -= stk->fir.raw[idx][2];
			stk->fir.raw[idx][0] = acc_lp->x;
			stk->fir.raw[idx][1] = acc_lp->y;
			stk->fir.raw[idx][2] = acc_lp->z;
			stk->fir.sum[0] += acc_lp->x;
			stk->fir.sum[1] += acc_lp->y;
			stk->fir.sum[2] += acc_lp->z;
			stk->fir.idx++;
#ifdef STK_ZG_FILTER
			if (abs(stk->fir.sum[0] / firlength) <= STK_ZG_COUNT)
				acc_lp->x = (stk->fir.sum[0] * zero_fir) / firlength;
			else
				acc_lp->x = stk->fir.sum[0] / firlength;
			if (abs(stk->fir.sum[1] / firlength) <= STK_ZG_COUNT)
				acc_lp->y = (stk->fir.sum[1] * zero_fir) / firlength;
			else
				acc_lp->y = stk->fir.sum[1] / firlength;
			if (abs(stk->fir.sum[2] / firlength) <= STK_ZG_COUNT)
				acc_lp->z = (stk->fir.sum[2] * zero_fir) / firlength;
			else
				acc_lp->z = stk->fir.sum[2] / firlength;
#else
			acc_lp->x = stk->fir.sum[0] / firlength;
			acc_lp->y = stk->fir.sum[1] / firlength;
			acc_lp->z = stk->fir.sum[2] / firlength;
#endif
		}
	}
}
#endif

#ifdef STK_TUNE
static void stk8baxx_reset_para(struct stk8baxx_data *stk)
{
	int ii;

	for (ii = 0; ii < 3; ii++) {
		stk->stk_tune_sum[ii] = 0;
		stk->stk_tune_square_sum[ii] = 0LL;
		stk->stk_tune_min[ii] = 4096;
		stk->stk_tune_max[ii] = -4096;
		stk->variance[ii] = 0;
	}
}

static void stk8baxx_tune(struct stk8baxx_data *stk, struct stk8baxx_acc *acc_xyz)
{
	int ii;
	u8 offset[3];
	s16 acc[3];
	s64 s64_temp;
	const s64 var_enlarge_scale = 64;

	if (stk->stk_tune_done != 0)
		return;

	acc[0] = acc_xyz->x;
	acc[1] = acc_xyz->y;
	acc[2] = acc_xyz->z;

	if (stk->event_since_en >= STK_TUNE_DELAY) {
		if ((abs(acc[0]) <= STK_TUNE_XYOFFSET) && (abs(acc[1]) <= STK_TUNE_XYOFFSET) &&
		    (abs(abs(acc[2]) - STK_LSB_1G) <= STK_TUNE_ZOFFSET)) {
			stk->stk_tune_index++;
			/* printk("\n-qhy20161108--%s----acc[0]=0x%x,,acc[1]=0x%x,,acc[2]=0x%x\n",__func__,acc[0],acc[1],acc[2]); */
		} else {
			stk->stk_tune_index = 0;
		}

		if (stk->stk_tune_index == 0) {
			stk8baxx_reset_para(stk);
			/* printk("\n--qhy20161108--%s-- %d--\n",__func__,__LINE__); */
		} else {
			for (ii = 0; ii < 3; ii++) {
				stk->stk_tune_sum[ii] += acc[ii];
				stk->stk_tune_square_sum[ii] += acc[ii] * acc[ii];
				if (acc[ii] > stk->stk_tune_max[ii])
					stk->stk_tune_max[ii] = acc[ii];
				if (acc[ii] < stk->stk_tune_min[ii])
					stk->stk_tune_min[ii] = acc[ii];
			}
		}

		if (stk->stk_tune_index == STK_TUNE_NUM) {
			for (ii = 0; ii < 3; ii++) {
				if ((stk->stk_tune_max[ii] - stk->stk_tune_min[ii]) > STK_TUNE_NOISE) {
					stk->stk_tune_index = 0;
					stk8baxx_reset_para(stk);
					return;
				}
			}

			stk->stk_tune_offset[0] = stk->stk_tune_sum[0] / STK_TUNE_NUM;
			stk->stk_tune_offset[1] = stk->stk_tune_sum[1] / STK_TUNE_NUM;
			if (acc[2] > 0)
				stk->stk_tune_offset[2] = stk->stk_tune_sum[2] / STK_TUNE_NUM - STK_LSB_1G;
			else
				stk->stk_tune_offset[2] = stk->stk_tune_sum[2] / STK_TUNE_NUM - (-STK_LSB_1G);

			offset[0] = (u8)(-stk->stk_tune_offset[0]);
			offset[1] = (u8)(-stk->stk_tune_offset[1]);
			offset[2] = (u8)(-stk->stk_tune_offset[2]);
			stk->stk_tune_offset_record[0] = offset[0];
			stk->stk_tune_offset_record[1] = offset[1];
			stk->stk_tune_offset_record[2] = offset[2];

			stk->stk_tune_done = 1;
			atomic_set(&stk->cali_status, STK_K_SUCCESS_TUNE);
			stk->event_since_en = 0;
			printk(KERN_INFO "%s:TUNE done, %d,%d,%d\n", __func__, offset[0], offset[1], offset[2]);
			printk(KERN_INFO "%s:TUNE done, var=%u,%u,%u\n", __func__, stk->variance[0], stk->variance[1], stk->variance[2]);
		}
	}
}
#endif

static void stk8baxx_sign_conv(struct stk8baxx_data *stk, s16 raw_acc_data[], u8 acc_reg_data[])
{
#ifdef CONFIG_SENSORS_STK8BA53
	raw_acc_data[0] = acc_reg_data[1] << 8 | acc_reg_data[0];
	raw_acc_data[0] >>= 4;
	raw_acc_data[1] = acc_reg_data[3] << 8 | acc_reg_data[2];
	raw_acc_data[1] >>= 4;
	raw_acc_data[2] = acc_reg_data[5] << 8 | acc_reg_data[4];
	raw_acc_data[2] >>= 4;
#else
	raw_acc_data[0] = acc_reg_data[1] << 8 | acc_reg_data[0];
	raw_acc_data[0] >>= 6;
	raw_acc_data[1] = acc_reg_data[3] << 8 | acc_reg_data[2];
	raw_acc_data[1] >>= 6;
	raw_acc_data[2] = acc_reg_data[5] << 8 | acc_reg_data[4];
	raw_acc_data[2] >>= 6;
#endif
}

static int stk8baxx_set_enable(struct stk8baxx_data *stk, char en)
{
	s8 result;
	s8 write_buffer = 0;
	int new_enabled = (en) ? 1 : 0;

	/*int k_status = atomic_read(&stk->cali_status);*/
#ifdef STK_DEBUG_PRINT
	printk("%s:+++1+++--k_status=%d,first_enable=%d\n", __func__, k_status, stk->first_enable);
	if (stk->first_enable && k_status != STK_K_RUNNING) {
		stk->first_enable = false;
		printk("%s:+++2+++first_enable=%d\n", __func__, stk->first_enable);
		stk8baxx_load_cali(stk);
	}
#endif
	enable_status = new_enabled;
	if (new_enabled == atomic_read(&stk->enabled))
		return 0;
	/* printk(KERN_INFO "%s:%x\n", __func__, en); */

	if (en)
		write_buffer = STK8BAXX_MD_NORMAL;
	else
		write_buffer = STK8BAXX_MD_SUSPEND;

	result = stk8baxx_smbus_write_byte_data(STK8BAXX_POWMODE, write_buffer);
	if (result < 0) {
		printk(KERN_ERR "%s:failed to write reg 0x%x, error=%d\n", __func__, STK8BAXX_POWMODE, result);
		goto error_enable;
	}

	if (en) {
		stk->event_since_en = 0;
#ifdef STK_TUNE
		if ((k_status & 0xF0) != 0 && stk->stk_tune_done == 0) {
			stk->stk_tune_index = 0;
			stk8baxx_reset_para(stk);
		}
#endif
	}

	atomic_set(&stk->enabled, new_enabled);
	return 0;

error_enable:
	return result;
}

static int gsensor_report_value(struct i2c_client *client, struct sensor_axis *axis)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);

	/* Report acceleration sensor information */
	input_report_abs(sensor->input_dev, ABS_X, axis->x);
	input_report_abs(sensor->input_dev, ABS_Y, axis->y);
	input_report_abs(sensor->input_dev, ABS_Z, axis->z);
	input_sync(sensor->input_dev);
#ifdef STK_DEBUG_PRINT
	printk(KERN_INFO "Gsensor x==%d  y==%d z==%d\n", axis->x, axis->y, axis->z);
#endif
	return 0;
}

static int stk8baxx_read_sensor_data(struct stk8baxx_data *stk)
{
	int result;
	u8 acc_reg[6];
	int x, y, z;
	struct stk8baxx_acc acc;
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(stk->client);
	struct sensor_platform_data *pdata = sensor->pdata;
	s16 raw_acc[3];

	acc.x = 0;
	acc.y = 0;
	acc.z = 0;

	*acc_reg = sensor->ops->read_reg;
	result = sensor_rx_data(stk->client, (char *)acc_reg, sensor->ops->read_len);
	if (result < 0) {
		printk(KERN_ERR "%s: failed to read acc data, error=%d\n", __func__, result);
		return result;
	}

	stk8baxx_sign_conv(stk, raw_acc, acc_reg);
#ifdef STK_DEBUG_PRINT
	printk(KERN_INFO "%s: raw_acc=%4d,%4d,%4d\n", __func__, (int)raw_acc[0], (int)raw_acc[1], (int)raw_acc[2]);
#endif
	acc.x = raw_acc[0];
	acc.y = raw_acc[1];
	acc.z = raw_acc[2];
#ifdef STK_TUNE
	if ((k_status & 0xF0) != 0)
		stk8baxx_tune(stk, &acc);
#endif
	x = acc.x;
	y = acc.y;
	z = acc.z;
	acc.x = (pdata->orientation[0]) * x + (pdata->orientation[1]) * y + (pdata->orientation[2]) * z;
	acc.y = (pdata->orientation[3]) * x + (pdata->orientation[4]) * y + (pdata->orientation[5]) * z;
	acc.z = (pdata->orientation[6]) * x + (pdata->orientation[7]) * y + (pdata->orientation[8]) * z;

#ifdef STK_LOWPASS
	stk8baxx_low_pass(stk, &acc);
#endif

	stk->acc_xyz.x = acc.x;
	stk->acc_xyz.y = acc.y;
	stk->acc_xyz.z = acc.z;
#ifdef STK_DEBUG_PRINT
	printk(KERN_INFO "stk8baxx acc= %4d, %4d, %4d\n", (int)stk->acc_xyz.x, (int)stk->acc_xyz.y, (int)stk->acc_xyz.z);
#endif
	return 0;
}

static int sensor_report_value(struct i2c_client *client)
{
	unsigned int xyz_adc_rang = 0;
	struct sensor_axis axis;
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	static int flag;

	stk8baxx_read_sensor_data(stk8baxx_data_ptr);

	xyz_adc_rang = STK_LSB_1G * STK_DEF_RANGE;
	axis.x = stk8baxx_data_ptr->acc_xyz.x * (STK8BAXX_RANGE_UG / xyz_adc_rang);
	axis.y = stk8baxx_data_ptr->acc_xyz.y * (STK8BAXX_RANGE_UG / xyz_adc_rang);
	axis.z = stk8baxx_data_ptr->acc_xyz.z * (STK8BAXX_RANGE_UG / xyz_adc_rang);

	/*
	*input dev will ignore report data if data value is the same with last_value,
	*sample rate will not enough by this way, so just avoid this case
	*/
	if ((sensor->axis.x == axis.x) && (sensor->axis.y == axis.y) && (sensor->axis.z == axis.z)) {
		if (flag) {
			flag = 0;
			axis.x += 1;
			axis.y += 1;
			axis.z += 1;
		} else {
			flag = 1;
			axis.x -= 1;
			axis.y -= 1;
			axis.z -= 1;
		}
	}

	gsensor_report_value(client, &axis);

	mutex_lock(&sensor->data_mutex);
	sensor->axis = axis;
	mutex_unlock(&sensor->data_mutex);

	return 0;
}

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	if (enable)
		stk8baxx_set_enable(stk8baxx_data_ptr, 1);
	else
		stk8baxx_set_enable(stk8baxx_data_ptr, 0);

	return 0;
}

static int sensor_init(struct i2c_client *client)
{
	int ret = 0;
	struct stk8baxx_data *stk;
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *)i2c_get_clientdata(client);

	printk(KERN_INFO "driver version:%s\n", STK_ACC_DRIVER_VERSION);
	if (!enable_status)
		return 0;
	stk = kzalloc(sizeof(*stk), GFP_KERNEL);
	if (!stk) {
		printk(KERN_ERR "%s:memory allocation error\n", __func__);
		return -ENOMEM;
	}
	stk8baxx_data_ptr = stk;
	sensor_ptr = sensor;
	stk->stk8baxx_placement = STK8BAXX_DEF_PLACEMENT;
	stk->client = client;
	ret = stk8baxx_reg_init(stk, client, sensor);
	if (ret) {
		printk(KERN_ERR "%s:stk8baxx initialization failed\n", __func__);
		return ret;
	}
	stk->re_enable = false;
	sensor->status_cur = SENSOR_OFF;

	/* Sys Attribute Register */
	if (no_create_att) {
		struct input_dev *p_input_dev = NULL;

		p_input_dev = input_allocate_device();
		if (!p_input_dev) {
			dev_err(&client->dev,
				"Failed to allocate input device\n");
			return -ENOMEM;
		}

		p_input_dev->name = "stk8baxx_attr";
		set_bit(EV_ABS, p_input_dev->evbit);
		dev_set_drvdata(&p_input_dev->dev, stk);
		ret = input_register_device(p_input_dev);
		if (ret) {
			dev_err(&client->dev,
				"Unable to register input device %s\n", p_input_dev->name);
			return ret;
		}

		DBG("Sys Attribute Register here %s is called for stk8baxx.\n", __func__);
		no_create_att = 0;
	}

	return 0;
}

struct sensor_operate gsensor_stk8baxx_ops = {
	.name				= "gs_stk8baxx",
	.type				= SENSOR_TYPE_ACCEL,			/*sensor type and it should be correct*/
	.id_i2c				= ACCEL_ID_STK8BAXX,			/*i2c id number*/
	.read_reg			= STK8BAXX_XOUT1,			/*read data*/
	.read_len			= 6,					/*data length*/
	.id_reg				= SENSOR_UNKNOW_DATA,			/*read device id from this register*/
	.id_data			= SENSOR_UNKNOW_DATA,			/*device id*/
	.precision			= SENSOR_UNKNOW_DATA,			/*12 bit*/
	.ctrl_reg			= STK8BAXX_POWMODE,			/*enable or disable*/
	/*intterupt status register*/
	.int_status_reg			= STK8BAXX_INTSTS2,
	.range				= {-STK8BAXX_RANGE_UG, STK8BAXX_RANGE_UG},	/*range*/
	.trig				= IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
	.active				= sensor_active,
	.init				= sensor_init,
	.report				= sensor_report_value,
};

static int gsensor_stk8baxx_probe(struct i2c_client *client,
				  const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &gsensor_stk8baxx_ops);
}

static int gsensor_stk8baxx_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &gsensor_stk8baxx_ops);
}

static const struct i2c_device_id gsensor_stk8baxx_id[] = {
	{"gs_stk8baxx", ACCEL_ID_STK8BAXX},
	{}
};

static struct i2c_driver gsensor_stk8baxx_driver = {
	.probe = gsensor_stk8baxx_probe,
	.remove = gsensor_stk8baxx_remove,
	.shutdown = sensor_shutdown,
	.id_table = gsensor_stk8baxx_id,
	.driver = {
		.name = "gsensor_stk8baxx",
	#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
	#endif
	},
};

module_i2c_driver(gsensor_stk8baxx_driver);

MODULE_AUTHOR("Lex Hsieh, Sensortek");
MODULE_DESCRIPTION("stk8baxx 3-Axis accelerometer driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(STK_ACC_DRIVER_VERSION);
