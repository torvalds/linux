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
*
*/

/**
 *  @addtogroup  DRIVERS
 *  @brief       Hardware drivers.
 *
 *  @{
 *      @file    inv_mpu_core.c
 *      @brief   A sysfs device driver for Invensense devices
 *      @details This driver currently works for the
 *               MPU3050/MPU6050/MPU9150/MPU6500/MPU9250 devices.
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
#include <linux/spinlock.h>

#include "inv_mpu_iio.h"
#ifdef INV_KERNEL_3_10
#include <linux/iio/sysfs.h>
#else
#include "sysfs.h"
#endif
#include "inv_counters.h"

s64 get_time_ns(void)
{
	struct timespec ts;
	ktime_get_ts(&ts);
	return timespec_to_ns(&ts);
}

static const short AKM8975_ST_Lower[3] = {-100, -100, -1000};
static const short AKM8975_ST_Upper[3] = {100, 100, -300};

static const short AKM8972_ST_Lower[3] = {-50, -50, -500};
static const short AKM8972_ST_Upper[3] = {50, 50, -100};

static const short AKM8963_ST_Lower[3] = {-200, -200, -3200};
static const short AKM8963_ST_Upper[3] = {200, 200, -800};

/* This is for compatibility for power state. Should remove once HAL
   does not use power_state sysfs entry */
static bool fake_asleep;

static const struct inv_hw_s hw_info[INV_NUM_PARTS] = {
	{119, "ITG3500"},
	{ 63, "MPU3050"},
	{117, "MPU6050"},
	{118, "MPU9150"},
	{119, "MPU6500"},
	{118, "MPU9250"},
	{128, "MPU6515"},
};

/* define INV_REG_NUM_MAX the max register unmbers of all sensor type */
#define INV_REG_NUM_MAX 128
static u8 inv_reg_data[128] = {0};
/**
 *  inv_reg_store() - Register store for hw poweroff.
 */
int inv_reg_store(struct inv_mpu_iio_s *st)
{
	int ii;
	char data;

	if (!st->chip_config.enable)
		st->set_power_state(st, true);
	for (ii = 0; ii < st->hw->num_reg; ii++) {
		/* don't read fifo r/w register */
		if (ii == st->reg.fifo_r_w)
			data = 0;
		else
			inv_plat_read(st, ii, 1, &data);
		inv_reg_data[ii] = data;
	}
	if (!st->chip_config.enable)
		st->set_power_state(st, false);

	return 0;
}

/**
 *  inv_reg_recover() - Register recover for hw poweroff.
 */
int inv_reg_recover(struct inv_mpu_iio_s *st)
{
	int ii;
	char data;

	if (!st->chip_config.enable)
		st->set_power_state(st, true);
	for (ii = 0; ii < st->hw->num_reg; ii++) {
		data = inv_reg_data[ii];
		/* don't write fifo r/w register */
		if (ii == st->reg.fifo_r_w)
			continue;
		else
			inv_plat_single_write(st, ii, data);
	}
	if (!st->chip_config.enable)
		st->set_power_state(st, false);

	return 0;
}

static void inv_setup_reg(struct inv_reg_map_s *reg)
{
	reg->sample_rate_div	= REG_SAMPLE_RATE_DIV;
	reg->lpf		= REG_CONFIG;
	reg->bank_sel		= REG_BANK_SEL;
	reg->user_ctrl		= REG_USER_CTRL;
	reg->fifo_en		= REG_FIFO_EN;
	reg->gyro_config	= REG_GYRO_CONFIG;
	reg->accl_config	= REG_ACCEL_CONFIG;
	reg->fifo_count_h	= REG_FIFO_COUNT_H;
	reg->fifo_r_w		= REG_FIFO_R_W;
	reg->raw_gyro		= REG_RAW_GYRO;
	reg->raw_accl		= REG_RAW_ACCEL;
	reg->temperature	= REG_TEMPERATURE;
	reg->int_enable		= REG_INT_ENABLE;
	reg->int_status		= REG_INT_STATUS;
	reg->pwr_mgmt_1		= REG_PWR_MGMT_1;
	reg->pwr_mgmt_2		= REG_PWR_MGMT_2;
	reg->mem_start_addr	= REG_MEM_START_ADDR;
	reg->mem_r_w		= REG_MEM_RW;
	reg->prgm_strt_addrh	= REG_PRGM_STRT_ADDRH;
};

static int inv_switch_engine(struct inv_mpu_iio_s *st, bool en, u32 mask)
{
	struct inv_reg_map_s *reg;
	u8 data, mgmt_1;
	int result;
	reg = &st->reg;
	/* switch clock needs to be careful. Only when gyro is on, can
	   clock source be switched to gyro. Otherwise, it must be set to
	   internal clock */
	if (BIT_PWR_GYRO_STBY == mask) {
		result = inv_plat_read(st, reg->pwr_mgmt_1, 1, &mgmt_1);
		if (result)
			return result;

		mgmt_1 &= ~BIT_CLK_MASK;
	}

	if ((BIT_PWR_GYRO_STBY == mask) && (!en)) {
		/* turning off gyro requires switch to internal clock first.
		   Then turn off gyro engine */
		mgmt_1 |= INV_CLK_INTERNAL;
		result = inv_plat_single_write(st, reg->pwr_mgmt_1,
						mgmt_1);
		if (result)
			return result;
	}

	result = inv_plat_read(st, reg->pwr_mgmt_2, 1, &data);
	if (result)
		return result;
	if (en)
		data &= (~mask);
	else
		data |= mask;
	result = inv_plat_single_write(st, reg->pwr_mgmt_2, data);
	if (result)
		return result;

	if ((BIT_PWR_GYRO_STBY == mask) && en) {
		/* only gyro on needs sensor up time */
		msleep(SENSOR_UP_TIME);
		/* after gyro is on & stable, switch internal clock to PLL */
		mgmt_1 |= INV_CLK_PLL;
		result = inv_plat_single_write(st, reg->pwr_mgmt_1,
						mgmt_1);
		if (result)
			return result;
	}
	if ((BIT_PWR_ACCL_STBY == mask) && en)
		msleep(REG_UP_TIME);

	return 0;
}

/**
 *  inv_lpa_freq() - store current low power frequency setting.
 */
static int inv_lpa_freq(struct inv_mpu_iio_s *st, int lpa_freq)
{
	unsigned long result;
	u8 d;
	const u8 mpu6500_lpa_mapping[] = {2, 4, 6, 7};

	if (lpa_freq > MAX_LPA_FREQ_PARAM)
		return -EINVAL;

	if (INV_MPU6500 == st->chip_type) {
		d = mpu6500_lpa_mapping[lpa_freq];
		result = inv_plat_single_write(st, REG_6500_LP_ACCEL_ODR, d);
		if (result)
			return result;
	}
	st->chip_config.lpa_freq = lpa_freq;

	return 0;
}

static int set_power_itg(struct inv_mpu_iio_s *st, bool power_on)
{
	struct inv_reg_map_s *reg;
	u8 data;
	int result;

	if ((!power_on) == st->chip_config.is_asleep)
		return 0;
	reg = &st->reg;
	if (power_on)
		data = 0;
	else
		data = BIT_SLEEP;
	result = inv_plat_single_write(st, reg->pwr_mgmt_1, data);
	if (result)
		return result;

	if (power_on) {
		if (INV_MPU6500 == st->chip_type)
			msleep(POWER_UP_TIME);
		else
			msleep(REG_UP_TIME);
	}

	st->chip_config.is_asleep = !power_on;

	return 0;
}

/**
 *  inv_init_config() - Initialize hardware, disable FIFO.
 *  @indio_dev:	Device driver instance.
 *  Initial configuration:
 *  FSR: +/- 2000DPS
 *  DLPF: 42Hz
 *  FIFO rate: 50Hz
 */
static int inv_init_config(struct iio_dev *indio_dev)
{
	struct inv_reg_map_s *reg;
	int result;
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);


	reg = &st->reg;

#if 0
	/* set int latch en */
	result = inv_plat_single_write(st, REG_INT_PIN_CFG, 0x20);
	if (result)
		return result;
#endif
	result = inv_plat_single_write(st, reg->gyro_config,
		INV_FSR_2000DPS << GYRO_CONFIG_FSR_SHIFT);
	if (result)
		return result;

	st->chip_config.fsr = INV_FSR_2000DPS;

	result = inv_plat_single_write(st, reg->lpf, INV_FILTER_42HZ);
	if (result)
		return result;
	st->chip_config.lpf = INV_FILTER_42HZ;

	result = inv_plat_single_write(st, reg->sample_rate_div,
					ONE_K_HZ / INIT_FIFO_RATE - 1);
	if (result)
		return result;
	st->chip_config.fifo_rate = INIT_FIFO_RATE;
	st->chip_config.new_fifo_rate = INIT_FIFO_RATE;
	st->irq_dur_ns            = INIT_DUR_TIME;
	st->chip_config.prog_start_addr = DMP_START_ADDR;
	st->chip_config.dmp_output_rate = INIT_DMP_OUTPUT_RATE;
	st->self_test.samples = INIT_ST_SAMPLES;
	st->self_test.threshold = INIT_ST_THRESHOLD;
	if (INV_ITG3500 != st->chip_type) {
		st->chip_config.accl_fs = INV_FS_02G;
		result = inv_plat_single_write(st, reg->accl_config,
			(INV_FS_02G << ACCL_CONFIG_FSR_SHIFT));
		if (result)
			return result;
		st->tap.time = INIT_TAP_TIME;
		st->tap.thresh = INIT_TAP_THRESHOLD;
		st->tap.min_count = INIT_TAP_MIN_COUNT;
		st->smd.threshold = MPU_INIT_SMD_THLD;
		st->smd.delay     = MPU_INIT_SMD_DELAY_THLD;
		st->smd.delay2    = MPU_INIT_SMD_DELAY2_THLD;

		result = inv_plat_single_write(st, REG_ACCEL_MOT_DUR,
						INIT_MOT_DUR);
		if (result)
			return result;
		st->mot_int.mot_dur = INIT_MOT_DUR;

		result = inv_plat_single_write(st, REG_ACCEL_MOT_THR,
						INIT_MOT_THR);
		if (result)
			return result;
		st->mot_int.mot_thr = INIT_MOT_THR;
	}

	return 0;
}

/**
 *  inv_compass_scale_show() - show compass scale.
 */
static int inv_compass_scale_show(struct inv_mpu_iio_s *st, int *scale)
{
	if (COMPASS_ID_AK8975 == st->plat_data.sec_slave_id)
		*scale = DATA_AKM8975_SCALE;
	else if (COMPASS_ID_AK8972 == st->plat_data.sec_slave_id)
		*scale = DATA_AKM8972_SCALE;
	else if (COMPASS_ID_AK8963 == st->plat_data.sec_slave_id)
		if (st->compass_scale)
			*scale = DATA_AKM8963_SCALE1;
		else
			*scale = DATA_AKM8963_SCALE0;
	else
		return -EINVAL;

	return IIO_VAL_INT;
}

/**
 *  inv_sensor_show() - Read gyro/accel data directly from registers.
 */
static int inv_sensor_show(struct inv_mpu_iio_s  *st, int reg, int axis,
					int *val)
{
	int ind, result;
	u8 d[2];

	ind = (axis - IIO_MOD_X) * 2;
	result = i2c_smbus_read_i2c_block_data(st->client,
					       reg + ind, 2, d);
	if (result != 2)
		return -EINVAL;
	*val = (short)be16_to_cpup((__be16 *)(d));

	return IIO_VAL_INT;
}

/**
 *  mpu_read_raw() - read raw method.
 */
static int mpu_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val, int *val2, long mask)
{
	struct inv_mpu_iio_s  *st = iio_priv(indio_dev);
	int result;

	switch (mask) {
	case 0:
		/* if enabled, power is on already */
		if (!st->chip_config.enable)
			return -EBUSY;
		switch (chan->type) {
		case IIO_ANGL_VEL:
			if (!st->chip_config.gyro_enable)
				return -EPERM;
			return inv_sensor_show(st, st->reg.raw_gyro,
						chan->channel2, val);
		case IIO_ACCEL:
			if (!st->chip_config.accl_enable)
				return -EPERM;
			return inv_sensor_show(st, st->reg.raw_accl,
						chan->channel2, val);
		case IIO_MAGN:
			if (!st->chip_config.compass_enable)
				return -EPERM;
			*val = st->raw_compass[chan->channel2 - IIO_MOD_X];
			return IIO_VAL_INT;
		case IIO_QUATERNION:
			if (!(st->chip_config.dmp_on
				&& st->chip_config.quaternion_on))
				return -EPERM;
			if (IIO_MOD_R == chan->channel2)
				*val = st->raw_quaternion[0];
			else
				*val = st->raw_quaternion[chan->channel2 -
							  IIO_MOD_X + 1];
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
		{
			const s16 gyro_scale[] = {250, 500, 1000, 2000};

			*val = gyro_scale[st->chip_config.fsr];

			return IIO_VAL_INT;
		}
		case IIO_ACCEL:
		{
			const s16 accel_scale[] = {2, 4, 8, 16};
			*val = accel_scale[st->chip_config.accl_fs] *
					st->chip_info.multi;
			return IIO_VAL_INT;
		}
		case IIO_MAGN:
			return inv_compass_scale_show(st, val);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_CALIBBIAS:
		if (st->chip_config.self_test_run_once == 0) {
			/* This can only be run when enable is zero */
			if (st->chip_config.enable)
				return -EBUSY;
			mutex_lock(&indio_dev->mlock);

			result = inv_power_up_self_test(st);
			if (result)
				goto error_info_calibbias;
			result = inv_do_test(st, 0,  st->gyro_bias,
				st->accel_bias);
			if (result)
				goto error_info_calibbias;
			st->chip_config.self_test_run_once = 1;
error_info_calibbias:
			/* Reset Accel and Gyro full scale range
			   back to default value */
			inv_recover_setting(st);
			mutex_unlock(&indio_dev->mlock);
		}

		switch (chan->type) {
		case IIO_ANGL_VEL:
			*val = st->gyro_bias[chan->channel2 - IIO_MOD_X];
			return IIO_VAL_INT;
		case IIO_ACCEL:
			*val = st->accel_bias[chan->channel2 - IIO_MOD_X] *
					st->chip_info.multi;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_ACCEL:
			*val = st->input_accel_bias[chan->channel2 - IIO_MOD_X];
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

/**
 *  inv_write_fsr() - Configure the gyro's scale range.
 */
static int inv_write_fsr(struct inv_mpu_iio_s *st, int fsr)
{
	struct inv_reg_map_s *reg;
	int result;
	reg = &st->reg;
	if ((fsr < 0) || (fsr > MAX_GYRO_FS_PARAM))
		return -EINVAL;
	if (fsr == st->chip_config.fsr)
		return 0;

	if (INV_MPU3050 == st->chip_type)
		result = inv_plat_single_write(st, reg->lpf,
			(fsr << GYRO_CONFIG_FSR_SHIFT) | st->chip_config.lpf);
	else
		result = inv_plat_single_write(st, reg->gyro_config,
			fsr << GYRO_CONFIG_FSR_SHIFT);

	if (result)
		return result;
	st->chip_config.fsr = fsr;

	return 0;
}

/**
 *  inv_write_accel_fs() - Configure the accelerometer's scale range.
 */
static int inv_write_accel_fs(struct inv_mpu_iio_s *st, int fs)
{
	int result;
	struct inv_reg_map_s *reg;

	reg = &st->reg;
	if (fs < 0 || fs > MAX_ACCL_FS_PARAM)
		return -EINVAL;
	if (fs == st->chip_config.accl_fs)
		return 0;
	if (INV_MPU3050 == st->chip_type)
		result = st->mpu_slave->set_fs(st, fs);
	else
		result = inv_plat_single_write(st, reg->accl_config,
				(fs << ACCL_CONFIG_FSR_SHIFT));
	if (result)
		return result;

	st->chip_config.accl_fs = fs;

	return 0;
}

/**
 *  inv_write_compass_scale() - Configure the compass's scale range.
 */
static int inv_write_compass_scale(struct inv_mpu_iio_s  *st, int data)
{
	char d, en;
	int result;
	if (COMPASS_ID_AK8963 != st->plat_data.sec_slave_id)
		return 0;
	en = !!data;
	if (st->compass_scale == en)
		return 0;
	d = (DATA_AKM_MODE_SM | (st->compass_scale << AKM8963_SCALE_SHIFT));
	result = inv_plat_single_write(st, REG_I2C_SLV1_DO, d);
	if (result)
		return result;
	st->compass_scale = en;

	return 0;
}

/**
 *  mpu_write_raw() - write raw method.
 */
static int mpu_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask) {
	struct inv_mpu_iio_s  *st = iio_priv(indio_dev);
	int result;

	if (st->chip_config.enable)
		return -EBUSY;
	mutex_lock(&indio_dev->mlock);
	result = st->set_power_state(st, true);
	if (result) {
		mutex_unlock(&indio_dev->mlock);
		return result;
	}

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			result = inv_write_fsr(st, val);
			break;
		case IIO_ACCEL:
			result = inv_write_accel_fs(st, val);
			break;
		case IIO_MAGN:
			result = inv_write_compass_scale(st, val);
			break;
		default:
			result = -EINVAL;
			break;
		}
		break;
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_ACCEL:
			if (!st->chip_config.firmware_loaded) {
				result = -EPERM;
				goto error_write_raw;
			}
			result = inv_set_accel_bias_dmp(st);
			if (result)
				goto error_write_raw;
			st->input_accel_bias[chan->channel2 - IIO_MOD_X] = val;
			result = 0;
			break;
		default:
			result = -EINVAL;
			break;
		}
		break;
	default:
		result = -EINVAL;
		break;
	}

error_write_raw:
	result |= st->set_power_state(st, false);
	mutex_unlock(&indio_dev->mlock);

	return result;
}

/**
 *  inv_fifo_rate_store() - Set fifo rate.
 */
static int inv_fifo_rate_store(struct inv_mpu_iio_s *st, int fifo_rate)
{
	if ((fifo_rate < MIN_FIFO_RATE) || (fifo_rate > MAX_FIFO_RATE))
		return -EINVAL;

	if (st->chip_config.has_compass) {
		st->compass_divider = COMPASS_RATE_SCALE * fifo_rate /
					ONE_K_HZ;
		if (st->compass_divider > 0)
			st->compass_divider -= 1;
		st->compass_counter = 0;
	}
	st->irq_dur_ns = (ONE_K_HZ / fifo_rate) * NSEC_PER_MSEC;
	st->chip_config.new_fifo_rate = fifo_rate;

	return 0;
}

/**
 *  inv_reg_dump_show() - Register dump for testing.
 */
static ssize_t inv_reg_dump_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ii;
	char data;
	ssize_t bytes_printed = 0;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);
	if (!st->chip_config.enable)
		st->set_power_state(st, true);
	for (ii = 0; ii < st->hw->num_reg; ii++) {
		/* don't read fifo r/w register */
		if (ii == st->reg.fifo_r_w)
			data = 0;
		else
			inv_plat_read(st, ii, 1, &data);
		bytes_printed += sprintf(buf + bytes_printed, "%#2x: %#2x\n",
					 ii, data);
	}
	if (!st->chip_config.enable)
		st->set_power_state(st, false);
	mutex_unlock(&indio_dev->mlock);

	return bytes_printed;
}

int write_be32_key_to_mem(struct inv_mpu_iio_s *st,
					u32 data, int key)
{
	cpu_to_be32s(&data);
	return mem_w_key(key, sizeof(data), (u8 *)&data);
}

/**
 * inv_quaternion_on() -  calling this function will store
 *                                 current quaternion on
 */
static int inv_quaternion_on(struct inv_mpu_iio_s *st,
				 struct iio_buffer *ring, bool en)
{
	st->chip_config.quaternion_on = en;
	if (!en) {
		clear_bit(INV_MPU_SCAN_QUAT_R, ring->scan_mask);
		clear_bit(INV_MPU_SCAN_QUAT_X, ring->scan_mask);
		clear_bit(INV_MPU_SCAN_QUAT_Y, ring->scan_mask);
		clear_bit(INV_MPU_SCAN_QUAT_Z, ring->scan_mask);
	}

	return 0;
}

/**
 * inv_dmp_attr_store() -  calling this function will store current
 *                        dmp parameter settings
 */
static ssize_t inv_dmp_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int result, data;

	mutex_lock(&indio_dev->mlock);
	if (st->chip_config.enable) {
		result = -EBUSY;
		goto dmp_attr_store_fail;
	}
	if (this_attr->address <= ATTR_DMP_DISPLAY_ORIENTATION_ON) {
		if (!st->chip_config.firmware_loaded) {
			result = -EINVAL;
			goto dmp_attr_store_fail;
		}
		result = st->set_power_state(st, true);
		if (result)
			goto dmp_attr_store_fail;
	}

	result = kstrtoint(buf, 10, &data);
	if (result)
		goto dmp_attr_store_fail;
	switch (this_attr->address) {
	case ATTR_DMP_SMD_ENABLE:
	{
		u8 on[] = {0, 1};
		u8 off[] = {0, 0};
		u8 *d;
		if (data)
			d = on;
		else
			d = off;
		result = mem_w_key(KEY_SMD_ENABLE, ARRAY_SIZE(on), d);
		if (result)
			goto dmp_attr_store_fail;
		st->chip_config.smd_enable = !!data;
	}
		break;
	case ATTR_DMP_SMD_THLD:
		if (data < 0 || data > SHRT_MAX)
			goto dmp_attr_store_fail;
		result = write_be32_key_to_mem(st, data << 16,
						KEY_SMD_ACCEL_THLD);
		if (result)
			goto dmp_attr_store_fail;
		st->smd.threshold = data;
		break;
	case ATTR_DMP_SMD_DELAY_THLD:
		if (data < 0 || data > INT_MAX / MPU_DEFAULT_DMP_FREQ)
			goto dmp_attr_store_fail;
		result = write_be32_key_to_mem(st, data * MPU_DEFAULT_DMP_FREQ,
						KEY_SMD_DELAY_THLD);
		if (result)
			goto dmp_attr_store_fail;
		st->smd.delay = data;
		break;
	case ATTR_DMP_SMD_DELAY_THLD2:
		if (data < 0 || data > INT_MAX / MPU_DEFAULT_DMP_FREQ)
			goto dmp_attr_store_fail;
		result = write_be32_key_to_mem(st, data * MPU_DEFAULT_DMP_FREQ,
						KEY_SMD_DELAY2_THLD);
		if (result)
			goto dmp_attr_store_fail;
		st->smd.delay2 = data;
		break;
	case ATTR_DMP_TAP_ON:
		result = inv_enable_tap_dmp(st, !!data);
		if (result)
			goto dmp_attr_store_fail;
		st->chip_config.tap_on = !!data;
		break;
	case ATTR_DMP_TAP_THRESHOLD: {
		const char ax[] = {INV_TAP_AXIS_X, INV_TAP_AXIS_Y,
							INV_TAP_AXIS_Z};
		int i;
		if (data < 0 || data > USHRT_MAX) {
			result = -EINVAL;
			goto dmp_attr_store_fail;
		}
		for (i = 0; i < ARRAY_SIZE(ax); i++) {
			result = inv_set_tap_threshold_dmp(st, ax[i], data);
			if (result)
				goto dmp_attr_store_fail;
		}
		st->tap.thresh = data;
		break;
	}
	case ATTR_DMP_TAP_MIN_COUNT:
		if (data < 0 || data > USHRT_MAX) {
			result = -EINVAL;
			goto dmp_attr_store_fail;
		}
		result = inv_set_min_taps_dmp(st, data);
		if (result)
			goto dmp_attr_store_fail;
		st->tap.min_count = data;
		break;
	case ATTR_DMP_TAP_TIME:
		if (data < 0 || data > USHRT_MAX) {
			result = -EINVAL;
			goto dmp_attr_store_fail;
		}
		result = inv_set_tap_time_dmp(st, data);
		if (result)
			goto dmp_attr_store_fail;
		st->tap.time = data;
		break;
	case ATTR_DMP_DISPLAY_ORIENTATION_ON:
		result = inv_set_display_orient_interrupt_dmp(st, !!data);
		if (result)
			goto dmp_attr_store_fail;
		st->chip_config.display_orient_on = !!data;
		break;
	/* from here, power of chip is not turned on */
	case ATTR_DMP_ON:
		st->chip_config.dmp_on = !!data;
		break;
	case ATTR_DMP_INT_ON:
		st->chip_config.dmp_int_on = !!data;
		break;
	case ATTR_DMP_EVENT_INT_ON:
		st->chip_config.dmp_event_int_on = !!data;
		break;
	case ATTR_DMP_OUTPUT_RATE:
		if (data <= 0 || data > MAX_DMP_OUTPUT_RATE) {
			result = -EINVAL;
			goto dmp_attr_store_fail;
		}
		st->chip_config.dmp_output_rate = data;
		if (st->chip_config.has_compass) {
			st->compass_dmp_divider = COMPASS_RATE_SCALE * data /
							ONE_K_HZ;
			if (st->compass_dmp_divider > 0)
				st->compass_dmp_divider -= 1;
			st->compass_counter = 0;
		}
		break;
	case ATTR_DMP_QUATERNION_ON:
		result = inv_quaternion_on(st, indio_dev->buffer, !!data);
		break;
#ifdef CONFIG_INV_TESTING
	case ATTR_DEBUG_SMD_ENABLE_TESTP1:
	{
		u8 d[] = {0x42};
		result = st->set_power_state(st, true);
		if (result)
			goto dmp_attr_store_fail;
		result = mem_w_key(KEY_SMD_ENABLE_TESTPT1, ARRAY_SIZE(d), d);
		if (result)
			goto dmp_attr_store_fail;
	}
		break;
	case ATTR_DEBUG_SMD_ENABLE_TESTP2:
	{
		u8 d[] = {0x42};
		result = st->set_power_state(st, true);
		if (result)
			goto dmp_attr_store_fail;
		result = mem_w_key(KEY_SMD_ENABLE_TESTPT2, ARRAY_SIZE(d), d);
		if (result)
			goto dmp_attr_store_fail;
	}
		break;
#endif
	default:
		result = -EINVAL;
		goto dmp_attr_store_fail;
	}

dmp_attr_store_fail:
	if ((this_attr->address <= ATTR_DMP_DISPLAY_ORIENTATION_ON) &&
					(!st->chip_config.enable))
		result |= st->set_power_state(st, false);
	mutex_unlock(&indio_dev->mlock);
	if (result)
		return result;

	return count;
}

/**
 * inv_attr_show() -  calling this function will show current
 *                        dmp parameters.
 */
static ssize_t inv_attr_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int result;
	s8 *m;

	switch (this_attr->address) {
	case ATTR_DMP_SMD_ENABLE:
		return sprintf(buf, "%d\n", st->chip_config.smd_enable);
	case ATTR_DMP_SMD_THLD:
		return sprintf(buf, "%d\n", st->smd.threshold);
	case ATTR_DMP_SMD_DELAY_THLD:
		return sprintf(buf, "%d\n", st->smd.delay);
	case ATTR_DMP_SMD_DELAY_THLD2:
		return sprintf(buf, "%d\n", st->smd.delay2);
	case ATTR_DMP_TAP_ON:
		return sprintf(buf, "%d\n", st->chip_config.tap_on);
	case ATTR_DMP_TAP_THRESHOLD:
		return sprintf(buf, "%d\n", st->tap.thresh);
	case ATTR_DMP_TAP_MIN_COUNT:
		return sprintf(buf, "%d\n", st->tap.min_count);
	case ATTR_DMP_TAP_TIME:
		return sprintf(buf, "%d\n", st->tap.time);
	case ATTR_DMP_DISPLAY_ORIENTATION_ON:
		return sprintf(buf, "%d\n",
			st->chip_config.display_orient_on);

	case ATTR_DMP_ON:
		return sprintf(buf, "%d\n", st->chip_config.dmp_on);
	case ATTR_DMP_INT_ON:
		return sprintf(buf, "%d\n", st->chip_config.dmp_int_on);
	case ATTR_DMP_EVENT_INT_ON:
		return sprintf(buf, "%d\n", st->chip_config.dmp_event_int_on);
	case ATTR_DMP_OUTPUT_RATE:
		return sprintf(buf, "%d\n",
				st->chip_config.dmp_output_rate);
	case ATTR_DMP_QUATERNION_ON:
		return sprintf(buf, "%d\n", st->chip_config.quaternion_on);

	case ATTR_MOTION_LPA_ON:
		return sprintf(buf, "%d\n", st->mot_int.mot_on);
	case ATTR_MOTION_LPA_FREQ:{
		const char *f[] = {"1.25", "5", "20", "40"};
		return sprintf(buf, "%s\n", f[st->chip_config.lpa_freq]);
	}
	case ATTR_MOTION_LPA_THRESHOLD:
		return sprintf(buf, "%d\n", st->mot_int.mot_thr);

	case ATTR_SELF_TEST_SAMPLES:
		return sprintf(buf, "%d\n", st->self_test.samples);
	case ATTR_SELF_TEST_THRESHOLD:
		return sprintf(buf, "%d\n", st->self_test.threshold);
	case ATTR_GYRO_ENABLE:
		return sprintf(buf, "%d\n", st->chip_config.gyro_enable);
	case ATTR_ACCL_ENABLE:
		return sprintf(buf, "%d\n", st->chip_config.accl_enable);
	case ATTR_COMPASS_ENABLE:
		return sprintf(buf, "%d\n", st->chip_config.compass_enable);
	case ATTR_POWER_STATE:
		return sprintf(buf, "%d\n", !fake_asleep);
	case ATTR_FIRMWARE_LOADED:
		return sprintf(buf, "%d\n", st->chip_config.firmware_loaded);
	case ATTR_SAMPLING_FREQ:
		return sprintf(buf, "%d\n", st->chip_config.new_fifo_rate);

	case ATTR_SELF_TEST:
		if (st->chip_config.enable)
			return -EBUSY;
		mutex_lock(&indio_dev->mlock);
		if (INV_MPU3050 == st->chip_type)
			result = 1;
		else
			result = inv_hw_self_test(st);
		mutex_unlock(&indio_dev->mlock);
		return sprintf(buf, "%d\n", result);

	case ATTR_GYRO_MATRIX:
		m = st->plat_data.orientation;
		return sprintf(buf, "%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
			m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
	case ATTR_ACCL_MATRIX:
		if (st->plat_data.sec_slave_type == SECONDARY_SLAVE_TYPE_ACCEL)
			m = st->plat_data.secondary_orientation;
		else
			m = st->plat_data.orientation;
		return sprintf(buf, "%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
			m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
	case ATTR_COMPASS_MATRIX:
		if (st->plat_data.sec_slave_type ==
				SECONDARY_SLAVE_TYPE_COMPASS)
			m = st->plat_data.secondary_orientation;
		else
			return -ENODEV;
		return sprintf(buf, "%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
			m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
	case ATTR_SECONDARY_NAME:{
	const char *n[] = {"0", "AK8975", "AK8972", "AK8963", "BMA250"};
	if (COMPASS_ID_AK8975 == st->plat_data.sec_slave_id)
		return sprintf(buf, "%s\n", n[1]);
	else if (COMPASS_ID_AK8972 == st->plat_data.sec_slave_id)
		return sprintf(buf, "%s\n", n[2]);
	else if (COMPASS_ID_AK8963 == st->plat_data.sec_slave_id)
		return sprintf(buf, "%s\n", n[3]);
	else if (ACCEL_ID_BMA250 == st->plat_data.sec_slave_id)
		return sprintf(buf, "%s\n", n[4]);
	else
		return sprintf(buf, "%s\n", n[0]);
	}

#ifdef CONFIG_INV_TESTING
	case ATTR_REG_WRITE:
		return sprintf(buf, "1\n");
	case ATTR_DEBUG_SMD_EXE_STATE:
	{
		u8 d[2];

		result = st->set_power_state(st, true);
		mpu_memory_read(st, st->i2c_addr,
				inv_dmp_get_address(KEY_SMD_EXE_STATE), 2, d);
		return sprintf(buf, "%d\n", (short)be16_to_cpup((__be16 *)(d)));
	}
	case ATTR_DEBUG_SMD_DELAY_CNTR:
	{
		u8 d[4];

		result = st->set_power_state(st, true);
		mpu_memory_read(st, st->i2c_addr,
				inv_dmp_get_address(KEY_SMD_DELAY_CNTR), 4, d);
		return sprintf(buf, "%d\n", (int)be32_to_cpup((__be32 *)(d)));
	}
#endif
	default:
		return -EPERM;
	}
}

/**
 * inv_dmp_display_orient_show() -  calling this function will
 *			show orientation This event must use poll.
 */
static ssize_t inv_dmp_display_orient_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct inv_mpu_iio_s *st = iio_priv(dev_get_drvdata(dev));
	return sprintf(buf, "%d\n", st->display_orient_data);
}

/**
 * inv_accel_motion_show() -  calling this function showes motion interrupt.
 *                         This event must use poll.
 */
static ssize_t inv_accel_motion_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "1\n");
}

/**
 * inv_smd_show() -  calling this function showes smd interrupt.
 *                         This event must use poll.
 */
static ssize_t inv_smd_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "1\n");
}

/**
 *  inv_temperature_show() - Read temperature data directly from registers.
 */
static ssize_t inv_temperature_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{

	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);
	struct inv_reg_map_s *reg;
	int result, cur_scale, cur_off;
	short temp;
	long scale_t;
	u8 data[2];
	const long scale[] = {3834792L, 3158064L, 3340827L};
	const long offset[] = {5383314L, 2394184L, 1376256L};

	reg = &st->reg;
	mutex_lock(&indio_dev->mlock);
	if (!st->chip_config.enable)
		result = st->set_power_state(st, true);
	else
		result = 0;
	if (result) {
		mutex_unlock(&indio_dev->mlock);
		return result;
	}
	result = inv_plat_read(st, reg->temperature, 2, data);
	if (!st->chip_config.enable)
		result |= st->set_power_state(st, false);
	mutex_unlock(&indio_dev->mlock);
	if (result) {
		pr_err("Could not read temperature register.\n");
		return result;
	}
	if (st->use_hid)
		temp = st->hid_temperature;
	else
		temp = (signed short)(be16_to_cpup((short *)&data[0]));
	switch (st->chip_type) {
	case INV_MPU3050:
		cur_scale = scale[0];
		cur_off   = offset[0];
		break;
	case INV_MPU6050:
		cur_scale = scale[1];
		cur_off   = offset[1];
		break;
	case INV_MPU6500:
		cur_scale = scale[2];
		cur_off   = offset[2];
		break;
	default:
		return -EINVAL;
	};
	scale_t = cur_off +
		inv_q30_mult((int)temp << MPU_TEMP_SHIFT, cur_scale);

	INV_I2C_INC_TEMPREAD(1);

	if (st->use_hid)
		return sprintf(buf, "%ld %lld\n", scale_t, st->hid_timestamp);
	else
		return sprintf(buf, "%ld %lld\n", scale_t, get_time_ns());
}

/**
 * inv_firmware_loaded() -  calling this function will change
 *                        firmware load
 */
static int inv_firmware_loaded(struct inv_mpu_iio_s *st, int data)
{
	if (data)
		return -EINVAL;
	st->chip_config.firmware_loaded = 0;
	st->chip_config.dmp_on = 0;
	st->chip_config.quaternion_on = 0;

	return 0;
}

static int inv_switch_gyro_engine(struct inv_mpu_iio_s *st, bool en)
{
	return inv_switch_engine(st, en, BIT_PWR_GYRO_STBY);
}

static int inv_switch_accl_engine(struct inv_mpu_iio_s *st, bool en)
{
	return inv_switch_engine(st, en, BIT_PWR_ACCL_STBY);
}

/**
 *  inv_gyro_enable() - Enable/disable gyro.
 */
static int inv_gyro_enable(struct inv_mpu_iio_s *st,
				 struct iio_buffer *ring, bool en)
{
	if (en == st->chip_config.gyro_enable)
		return 0;
	if (!en) {
		st->chip_config.gyro_fifo_enable = 0;
		clear_bit(INV_MPU_SCAN_GYRO_X, ring->scan_mask);
		clear_bit(INV_MPU_SCAN_GYRO_Y, ring->scan_mask);
		clear_bit(INV_MPU_SCAN_GYRO_Z, ring->scan_mask);
	}
	st->chip_config.gyro_enable = en;

	return 0;
}

/**
 *  inv_accl_enable() - Enable/disable accl.
 */
static int inv_accl_enable(struct inv_mpu_iio_s *st,
				 struct iio_buffer *ring, bool en)
{
	if (en == st->chip_config.accl_enable)
		return 0;
	if (!en) {
		st->chip_config.accl_fifo_enable = 0;
		clear_bit(INV_MPU_SCAN_ACCL_X, ring->scan_mask);
		clear_bit(INV_MPU_SCAN_ACCL_Y, ring->scan_mask);
		clear_bit(INV_MPU_SCAN_ACCL_Z, ring->scan_mask);
	}
	st->chip_config.accl_enable = en;

	return 0;
}

/**
 * inv_compass_enable() -  calling this function will store compass
 *                         enable
 */
static int inv_compass_enable(struct inv_mpu_iio_s *st,
				 struct iio_buffer *ring, bool en)
{
	if (en == st->chip_config.compass_enable)
		return 0;
	if (!en) {
		st->chip_config.compass_fifo_enable = 0;
		clear_bit(INV_MPU_SCAN_MAGN_X, ring->scan_mask);
		clear_bit(INV_MPU_SCAN_MAGN_Y, ring->scan_mask);
		clear_bit(INV_MPU_SCAN_MAGN_Z, ring->scan_mask);
	}
	st->chip_config.compass_enable = en;

	return 0;
}

/**
 * inv_attr_store() -  calling this function will store current
 *                        non-dmp parameter settings
 */
static ssize_t inv_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);
	struct iio_buffer *ring = indio_dev->buffer;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int data;
	u8  d;
	int result;

	mutex_lock(&indio_dev->mlock);
	if (st->chip_config.enable) {
		result = -EBUSY;
		goto attr_store_fail;
	}
	if (this_attr->address <= ATTR_MOTION_LPA_THRESHOLD) {
		result = st->set_power_state(st, true);
		if (result)
			goto attr_store_fail;
	}

	result = kstrtoint(buf, 10, &data);
	if (result)
		goto attr_store_fail;
	switch (this_attr->address) {
	case ATTR_MOTION_LPA_ON:
		if (INV_MPU6500 == st->chip_type) {
			if (data)
				/* enable and put in MPU6500 mode */
				d = BIT_ACCEL_INTEL_ENABLE
					| BIT_ACCEL_INTEL_MODE;
			else
				d = 0;
			result = inv_plat_single_write(st,
						REG_6500_ACCEL_INTEL_CTRL, d);
			if (result)
				goto attr_store_fail;
		}
		st->mot_int.mot_on = !!data;
		st->chip_config.lpa_mode = !!data;
		break;
	case ATTR_MOTION_LPA_FREQ:
		result = inv_lpa_freq(st, data);
		break;
	case ATTR_MOTION_LPA_THRESHOLD:
		if ((data > MPU6XXX_MAX_MOTION_THRESH) || (data < 0)) {
			result = -EINVAL;
			goto attr_store_fail;
		}
		d = (u8)(data >> MPU6XXX_MOTION_THRESH_SHIFT);
		data = (d << MPU6XXX_MOTION_THRESH_SHIFT);
		result = inv_plat_single_write(st, REG_ACCEL_MOT_THR, d);
		if (result)
			goto attr_store_fail;
		st->mot_int.mot_thr = data;
		break;
	/* from now on, power is not turned on */
	case ATTR_SELF_TEST_SAMPLES:
		if (data > ST_MAX_SAMPLES || data < 0) {
			result = -EINVAL;
			goto attr_store_fail;
		}
		st->self_test.samples = data;
		break;
	case ATTR_SELF_TEST_THRESHOLD:
		if (data > ST_MAX_THRESHOLD || data < 0) {
			result = -EINVAL;
			goto attr_store_fail;
		}
		st->self_test.threshold = data;
	case ATTR_GYRO_ENABLE:
		result = st->gyro_en(st, ring, !!data);
		break;
	case ATTR_ACCL_ENABLE:
		result = st->accl_en(st, ring, !!data);
		break;
	case ATTR_COMPASS_ENABLE:
		result = inv_compass_enable(st, ring, !!data);
		break;
	case ATTR_POWER_STATE:
		fake_asleep = !data;
		break;
	case ATTR_FIRMWARE_LOADED:
		result = inv_firmware_loaded(st, data);
		break;
	case ATTR_SAMPLING_FREQ:
		result = inv_fifo_rate_store(st, data);
		break;
	default:
		result = -EINVAL;
		goto attr_store_fail;
	};

attr_store_fail:
	if ((this_attr->address <= ATTR_MOTION_LPA_THRESHOLD) &&
					(!st->chip_config.enable))
		result |= st->set_power_state(st, false);
	mutex_unlock(&indio_dev->mlock);
	if (result)
		return result;

	return count;
}

#ifdef CONFIG_INV_TESTING
/**
 * inv_reg_write_store() - register write command for testing.
 *                         Format: WSRRDD, where RR is the register in hex,
 *                                         and DD is the data in hex.
 */
static ssize_t inv_reg_write_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct inv_mpu_iio_s *st = iio_priv(indio_dev);
	u32 result;
	u8 wreg, wval;
	int temp;
	char local_buf[10];

	if ((buf[0] != 'W' && buf[0] != 'w') ||
	    (buf[1] != 'S' && buf[1] != 's'))
		return -EINVAL;
	if (strlen(buf) < 6)
		return -EINVAL;

	strncpy(local_buf, buf, 7);
	local_buf[6] = 0;
	result = sscanf(&local_buf[4], "%x", &temp);
	if (result == 0)
		return -EINVAL;
	wval = temp;
	local_buf[4] = 0;
	sscanf(&local_buf[2], "%x", &temp);
	if (result == 0)
		return -EINVAL;
	wreg = temp;

	result = inv_plat_single_write(st, wreg, wval);
	if (result)
		return result;

	return count;
}
#endif /* CONFIG_INV_TESTING */

#define IIO_ST(si, rb, sb, sh)                                          \
	{ .sign = si, .realbits = rb, .storagebits = sb, .shift = sh }
#define INV_MPU_CHAN(_type, _channel2, _index)                \
	{                                                         \
		.type = _type,                                        \
		.modified = 1,                                        \
		.channel2 = _channel2,                                \
		.info_mask_separate = BIT(IIO_CHAN_INFO_CALIBBIAS), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
		.scan_index = _index,                                 \
		.scan_type  = IIO_ST('s', 16, 16, 0)                  \
	}

#define INV_ACCL_CHAN(_type, _channel2, _index)                \
	{                                                         \
		.type = _type,                                        \
		.modified = 1,                                        \
		.channel2 = _channel2,                                \
		.info_mask_separate = BIT(IIO_CHAN_INFO_CALIBBIAS) | \
				      BIT(IIO_CHAN_INFO_OFFSET), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
		.scan_index = _index,                                 \
		.scan_type  = IIO_ST('s', 16, 16, 0)                  \
	}

#define INV_MPU_QUATERNION_CHAN(_channel2, _index)            \
	{                                                         \
		.type = IIO_QUATERNION,                               \
		.modified = 1,                                        \
		.channel2 = _channel2,                                \
		.scan_index = _index,                                 \
		.scan_type  = IIO_ST('s', 32, 32, 0)                  \
	}

#define INV_MPU_MAGN_CHAN(_channel2, _index)                  \
	{                                                         \
		.type = IIO_MAGN,                                     \
		.modified = 1,                                        \
		.channel2 = _channel2,                                \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
		.scan_index = _index,                                 \
		.scan_type  = IIO_ST('s', 16, 16, 0)                  \
	}

static const struct iio_chan_spec inv_mpu_channels[] = {
	IIO_CHAN_SOFT_TIMESTAMP(INV_MPU_SCAN_TIMESTAMP),
	INV_MPU_CHAN(IIO_ANGL_VEL, IIO_MOD_X, INV_MPU_SCAN_GYRO_X),
	INV_MPU_CHAN(IIO_ANGL_VEL, IIO_MOD_Y, INV_MPU_SCAN_GYRO_Y),
	INV_MPU_CHAN(IIO_ANGL_VEL, IIO_MOD_Z, INV_MPU_SCAN_GYRO_Z),

	INV_ACCL_CHAN(IIO_ACCEL, IIO_MOD_X, INV_MPU_SCAN_ACCL_X),
	INV_ACCL_CHAN(IIO_ACCEL, IIO_MOD_Y, INV_MPU_SCAN_ACCL_Y),
	INV_ACCL_CHAN(IIO_ACCEL, IIO_MOD_Z, INV_MPU_SCAN_ACCL_Z),

	INV_MPU_QUATERNION_CHAN(IIO_MOD_R, INV_MPU_SCAN_QUAT_R),
	INV_MPU_QUATERNION_CHAN(IIO_MOD_X, INV_MPU_SCAN_QUAT_X),
	INV_MPU_QUATERNION_CHAN(IIO_MOD_Y, INV_MPU_SCAN_QUAT_Y),
	INV_MPU_QUATERNION_CHAN(IIO_MOD_Z, INV_MPU_SCAN_QUAT_Z),

	INV_MPU_MAGN_CHAN(IIO_MOD_X, INV_MPU_SCAN_MAGN_X),
	INV_MPU_MAGN_CHAN(IIO_MOD_Y, INV_MPU_SCAN_MAGN_Y),
	INV_MPU_MAGN_CHAN(IIO_MOD_Z, INV_MPU_SCAN_MAGN_Z),
};


/*constant IIO attribute */
static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("10 20 50 100 200 500");

/* special sysfs */
static DEVICE_ATTR(reg_dump, S_IRUGO, inv_reg_dump_show, NULL);
static DEVICE_ATTR(temperature, S_IRUGO, inv_temperature_show, NULL);

/* event based sysfs, needs poll to read */
static DEVICE_ATTR(event_display_orientation, S_IRUGO,
	inv_dmp_display_orient_show, NULL);
static DEVICE_ATTR(event_accel_motion, S_IRUGO, inv_accel_motion_show, NULL);
static DEVICE_ATTR(event_smd, S_IRUGO, inv_smd_show, NULL);

/* DMP sysfs with power on/off */
static IIO_DEVICE_ATTR(smd_enable, S_IRUGO | S_IWUSR,
	inv_attr_show, inv_dmp_attr_store, ATTR_DMP_SMD_ENABLE);
static IIO_DEVICE_ATTR(smd_threshold, S_IRUGO | S_IWUSR,
	inv_attr_show, inv_dmp_attr_store, ATTR_DMP_SMD_THLD);
static IIO_DEVICE_ATTR(smd_delay_threshold, S_IRUGO | S_IWUSR,
	inv_attr_show, inv_dmp_attr_store, ATTR_DMP_SMD_DELAY_THLD);
static IIO_DEVICE_ATTR(smd_delay_threshold2, S_IRUGO | S_IWUSR,
	inv_attr_show, inv_dmp_attr_store, ATTR_DMP_SMD_DELAY_THLD2);
static IIO_DEVICE_ATTR(display_orientation_on, S_IRUGO | S_IWUSR,
	inv_attr_show, inv_dmp_attr_store, ATTR_DMP_DISPLAY_ORIENTATION_ON);

/* DMP sysfs without power on/off */
static IIO_DEVICE_ATTR(dmp_on, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_dmp_attr_store, ATTR_DMP_ON);
static IIO_DEVICE_ATTR(dmp_int_on, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_dmp_attr_store, ATTR_DMP_INT_ON);
static IIO_DEVICE_ATTR(dmp_event_int_on, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_dmp_attr_store, ATTR_DMP_EVENT_INT_ON);
static IIO_DEVICE_ATTR(dmp_output_rate, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_dmp_attr_store, ATTR_DMP_OUTPUT_RATE);
static IIO_DEVICE_ATTR(quaternion_on, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_dmp_attr_store, ATTR_DMP_QUATERNION_ON);

/* non DMP sysfs with power on/off */
static IIO_DEVICE_ATTR(motion_lpa_on, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_attr_store, ATTR_MOTION_LPA_ON);
static IIO_DEVICE_ATTR(motion_lpa_freq, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_attr_store, ATTR_MOTION_LPA_FREQ);
static IIO_DEVICE_ATTR(motion_lpa_threshold, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_attr_store, ATTR_MOTION_LPA_THRESHOLD);

/* non DMP sysfs without power on/off */
static IIO_DEVICE_ATTR(self_test_samples, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_attr_store, ATTR_SELF_TEST_SAMPLES);
static IIO_DEVICE_ATTR(self_test_threshold, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_attr_store, ATTR_SELF_TEST_THRESHOLD);
static IIO_DEVICE_ATTR(gyro_enable, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_attr_store, ATTR_GYRO_ENABLE);
static IIO_DEVICE_ATTR(accl_enable, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_attr_store, ATTR_ACCL_ENABLE);
static IIO_DEVICE_ATTR(compass_enable, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_attr_store, ATTR_COMPASS_ENABLE);
static IIO_DEVICE_ATTR(power_state, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_attr_store, ATTR_POWER_STATE);
static IIO_DEVICE_ATTR(firmware_loaded, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_attr_store, ATTR_FIRMWARE_LOADED);
static IIO_DEVICE_ATTR(sampling_frequency, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_attr_store, ATTR_SAMPLING_FREQ);

/* show method only sysfs but with power on/off */
static IIO_DEVICE_ATTR(self_test, S_IRUGO, inv_attr_show, NULL,
	ATTR_SELF_TEST);

/* show method only sysfs */
static IIO_DEVICE_ATTR(gyro_matrix, S_IRUGO, inv_attr_show, NULL,
	ATTR_GYRO_MATRIX);
static IIO_DEVICE_ATTR(accl_matrix, S_IRUGO, inv_attr_show, NULL,
	ATTR_ACCL_MATRIX);
static IIO_DEVICE_ATTR(compass_matrix, S_IRUGO, inv_attr_show, NULL,
	ATTR_COMPASS_MATRIX);
static IIO_DEVICE_ATTR(secondary_name, S_IRUGO, inv_attr_show, NULL,
	ATTR_SECONDARY_NAME);

#ifdef CONFIG_INV_TESTING
static IIO_DEVICE_ATTR(reg_write, S_IRUGO | S_IWUSR, inv_attr_show,
	inv_reg_write_store, ATTR_REG_WRITE);
/* smd debug related sysfs */
static IIO_DEVICE_ATTR(debug_smd_enable_testp1, S_IWUSR, NULL,
	inv_dmp_attr_store, ATTR_DEBUG_SMD_ENABLE_TESTP1);
static IIO_DEVICE_ATTR(debug_smd_enable_testp2, S_IWUSR, NULL,
	inv_dmp_attr_store, ATTR_DEBUG_SMD_ENABLE_TESTP2);
static IIO_DEVICE_ATTR(debug_smd_exe_state, S_IRUGO, inv_attr_show,
	NULL, ATTR_DEBUG_SMD_EXE_STATE);
static IIO_DEVICE_ATTR(debug_smd_delay_cntr, S_IRUGO, inv_attr_show,
	NULL, ATTR_DEBUG_SMD_DELAY_CNTR);
#endif

static const struct attribute *inv_gyro_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&dev_attr_reg_dump.attr,
	&dev_attr_temperature.attr,
	&iio_dev_attr_self_test_samples.dev_attr.attr,
	&iio_dev_attr_self_test_threshold.dev_attr.attr,
	&iio_dev_attr_gyro_enable.dev_attr.attr,
	&iio_dev_attr_power_state.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_self_test.dev_attr.attr,
	&iio_dev_attr_gyro_matrix.dev_attr.attr,
	&iio_dev_attr_secondary_name.dev_attr.attr,
#ifdef CONFIG_INV_TESTING
	&iio_dev_attr_reg_write.dev_attr.attr,
	&iio_dev_attr_debug_smd_enable_testp1.dev_attr.attr,
	&iio_dev_attr_debug_smd_enable_testp2.dev_attr.attr,
	&iio_dev_attr_debug_smd_exe_state.dev_attr.attr,
	&iio_dev_attr_debug_smd_delay_cntr.dev_attr.attr,
#endif
};

static const struct attribute *inv_mpu6050_attributes[] = {
	&dev_attr_event_display_orientation.attr,
	&dev_attr_event_smd.attr,
	&iio_dev_attr_smd_enable.dev_attr.attr,
	&iio_dev_attr_smd_threshold.dev_attr.attr,
	&iio_dev_attr_smd_delay_threshold.dev_attr.attr,
	&iio_dev_attr_smd_delay_threshold2.dev_attr.attr,
	&iio_dev_attr_display_orientation_on.dev_attr.attr,
	&iio_dev_attr_dmp_on.dev_attr.attr,
	&iio_dev_attr_dmp_int_on.dev_attr.attr,
	&iio_dev_attr_dmp_event_int_on.dev_attr.attr,
	&iio_dev_attr_dmp_output_rate.dev_attr.attr,
	&iio_dev_attr_quaternion_on.dev_attr.attr,
	&iio_dev_attr_accl_enable.dev_attr.attr,
	&iio_dev_attr_firmware_loaded.dev_attr.attr,
	&iio_dev_attr_accl_matrix.dev_attr.attr,

};

static const struct attribute *inv_mpu6500_attributes[] = {
	&dev_attr_event_accel_motion.attr,
	&iio_dev_attr_motion_lpa_on.dev_attr.attr,
	&iio_dev_attr_motion_lpa_freq.dev_attr.attr,
	&iio_dev_attr_motion_lpa_threshold.dev_attr.attr,
};

static const struct attribute *inv_compass_attributes[] = {
	&iio_dev_attr_compass_enable.dev_attr.attr,
	&iio_dev_attr_compass_matrix.dev_attr.attr,
};

static const struct attribute *inv_mpu3050_attributes[] = {
	&iio_dev_attr_accl_enable.dev_attr.attr,
	&iio_dev_attr_accl_matrix.dev_attr.attr,
};

static struct attribute *inv_attributes[ARRAY_SIZE(inv_gyro_attributes) +
				ARRAY_SIZE(inv_mpu6050_attributes) +
				ARRAY_SIZE(inv_mpu6500_attributes) +
				ARRAY_SIZE(inv_compass_attributes) + 1];

static const struct attribute_group inv_attribute_group = {
	.name = "mpu",
	.attrs = inv_attributes
};

static const struct iio_info mpu_info = {
	.driver_module = THIS_MODULE,
	.read_raw = &mpu_read_raw,
	.write_raw = &mpu_write_raw,
	.attrs = &inv_attribute_group,
};

void inv_set_iio_info(struct inv_mpu_iio_s *st, struct iio_dev *indio_dev)
{
	indio_dev->channels = inv_mpu_channels;
	indio_dev->num_channels = st->num_channels;

	indio_dev->info = &mpu_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->currentmode = INDIO_DIRECT_MODE;
}

/**
 *  inv_setup_compass() - Configure compass.
 */
static int inv_setup_compass(struct inv_mpu_iio_s *st)
{
	int result;
	u8 data[4];

	if (INV_MPU6050 == st->chip_type) {
		result = inv_plat_read(st, REG_YGOFFS_TC, 1, data);
		if (result)
			return result;
		data[0] &= ~BIT_I2C_MST_VDDIO;
		if (st->plat_data.level_shifter)
			data[0] |= BIT_I2C_MST_VDDIO;
		/*set up VDDIO register */
		result = inv_plat_single_write(st, REG_YGOFFS_TC, data[0]);
		if (result)
			return result;
	}
	/* set to bypass mode */
	result = inv_plat_single_write(st, REG_INT_PIN_CFG,
				st->plat_data.int_config | BIT_BYPASS_EN);
	if (result)
		return result;
	/*read secondary i2c ID register */
	result = inv_secondary_read(st, REG_AKM_ID, 1, data);
	if (result)
		return result;
	if (data[0] != DATA_AKM_ID)
		return -ENXIO;
	/*set AKM to Fuse ROM access mode */
	result = inv_secondary_write(st, REG_AKM_MODE, DATA_AKM_MODE_FR);
	if (result)
		return result;
	result = inv_secondary_read(st, REG_AKM_SENSITIVITY, THREE_AXIS,
					st->chip_info.compass_sens);
	if (result)
		return result;
	/*revert to power down mode */
	result = inv_secondary_write(st, REG_AKM_MODE, DATA_AKM_MODE_PD);
	if (result)
		return result;
	pr_debug("%s senx=%d, seny=%d, senz=%d\n",
		 st->hw->name,
		 st->chip_info.compass_sens[0],
		 st->chip_info.compass_sens[1],
		 st->chip_info.compass_sens[2]);
	/*restore to non-bypass mode */
	result = inv_plat_single_write(st, REG_INT_PIN_CFG,
			st->plat_data.int_config);
	if (result)
		return result;

	/*setup master mode and master clock and ES bit*/
	result = inv_plat_single_write(st, REG_I2C_MST_CTRL, BIT_WAIT_FOR_ES);
	if (result)
		return result;
	/* slave 0 is used to read data from compass */
	/*read mode */
	result = inv_plat_single_write(st, REG_I2C_SLV0_ADDR, BIT_I2C_READ|
		st->plat_data.secondary_i2c_addr);
	if (result)
		return result;
	/* AKM status register address is 2 */
	result = inv_plat_single_write(st, REG_I2C_SLV0_REG, REG_AKM_STATUS);
	if (result)
		return result;
	/* slave 0 is enabled at the beginning, read 8 bytes from here */
	result = inv_plat_single_write(st, REG_I2C_SLV0_CTRL, BIT_SLV_EN |
				NUM_BYTES_COMPASS_SLAVE);
	if (result)
		return result;
	/*slave 1 is used for AKM mode change only*/
	result = inv_plat_single_write(st, REG_I2C_SLV1_ADDR,
		st->plat_data.secondary_i2c_addr);
	if (result)
		return result;
	/* AKM mode register address is 0x0A */
	result = inv_plat_single_write(st, REG_I2C_SLV1_REG, REG_AKM_MODE);
	if (result)
		return result;
	/* slave 1 is enabled, byte length is 1 */
	result = inv_plat_single_write(st, REG_I2C_SLV1_CTRL, BIT_SLV_EN | 1);
	if (result)
		return result;
	/* output data for slave 1 is fixed, single measure mode*/
	st->compass_scale = 1;
	if (COMPASS_ID_AK8975 == st->plat_data.sec_slave_id) {
		st->compass_st_upper = AKM8975_ST_Upper;
		st->compass_st_lower = AKM8975_ST_Lower;
		data[0] = DATA_AKM_MODE_SM;
	} else if (COMPASS_ID_AK8972 == st->plat_data.sec_slave_id) {
		st->compass_st_upper = AKM8972_ST_Upper;
		st->compass_st_lower = AKM8972_ST_Lower;
		data[0] = DATA_AKM_MODE_SM;
	} else if (COMPASS_ID_AK8963 == st->plat_data.sec_slave_id) {
		st->compass_st_upper = AKM8963_ST_Upper;
		st->compass_st_lower = AKM8963_ST_Lower;
		data[0] = DATA_AKM_MODE_SM |
			  (st->compass_scale << AKM8963_SCALE_SHIFT);
	}
	result = inv_plat_single_write(st, REG_I2C_SLV1_DO, data[0]);
	if (result)
		return result;
	/* slave 0 and 1 timer action is enabled every sample*/
	result = inv_plat_single_write(st, REG_I2C_MST_DELAY_CTRL,
				BIT_SLV0_DLY_EN | BIT_SLV1_DLY_EN);
	return result;
}

static void inv_setup_func_ptr(struct inv_mpu_iio_s *st)
{
	if (st->chip_type == INV_MPU3050) {
		st->set_power_state    = set_power_mpu3050;
		st->switch_gyro_engine = inv_switch_3050_gyro_engine;
		st->switch_accl_engine = inv_switch_3050_accl_engine;
		st->init_config        = inv_init_config_mpu3050;
		st->setup_reg          = inv_setup_reg_mpu3050;
	} else {
		st->set_power_state    = set_power_itg;
		st->switch_gyro_engine = inv_switch_gyro_engine;
		st->switch_accl_engine = inv_switch_accl_engine;
		st->init_config        = inv_init_config;
		st->setup_reg          = inv_setup_reg;
		/*MPU6XXX special functions */
		st->compass_en         = inv_compass_enable;
		st->quaternion_en      = inv_quaternion_on;
	}
	st->gyro_en            = inv_gyro_enable;
	st->accl_en            = inv_accl_enable;
}

static int inv_detect_6xxx(struct inv_mpu_iio_s *st)
{
	int result;
	u8 d;

	result = inv_plat_read(st, REG_WHOAMI, 1, &d);
	if (result)
		return result;
	if ((d == MPU6500_ID) || (d == MPU6515_ID)) {
		st->chip_type = INV_MPU6500;
		strcpy(st->name, "mpu6500");
	} else {
		strcpy(st->name, "mpu6050");
	}

	return 0;
}

/**
 *  inv_check_chip_type() - check and setup chip type.
 */
int inv_check_chip_type(struct inv_mpu_iio_s *st, const char *name)
{
	struct inv_reg_map_s *reg;
	int result;
	int t_ind;
	int timeout = 10;

	if (!strcmp(name, "itg3500"))
		st->chip_type = INV_ITG3500;
	else if (!strcmp(name, "mpu3050"))
		st->chip_type = INV_MPU3050;
	else if (!strcmp(name, "mpu6050"))
		st->chip_type = INV_MPU6050;
	else if (!strcmp(name, "mpu9150"))
		st->chip_type = INV_MPU6050;
	else if (!strcmp(name, "mpu6500"))
		st->chip_type = INV_MPU6500;
	else if (!strcmp(name, "mpu9250"))
		st->chip_type = INV_MPU6500;
	else if (!strcmp(name, "mpu6xxx"))
		st->chip_type = INV_MPU6050;
	else if (!strcmp(name, "mpu6515"))
		st->chip_type = INV_MPU6500;
	else
		return -EPERM;
	inv_setup_func_ptr(st);
	st->hw  = &hw_info[st->chip_type];
	st->mpu_slave = NULL;
	reg = &st->reg;
	st->setup_reg(reg);
	/* reset to make sure previous state are not there */
	while (timeout) {
		result = inv_plat_single_write(st, reg->pwr_mgmt_1, BIT_H_RESET);
		if (!result)
			break;
		pr_err("inv_mpu: reset chip failed, err = %d\n", result);
		timeout--;
		msleep(POWER_UP_TIME);
	}
	msleep(POWER_UP_TIME);
	/* toggle power state */
	result = st->set_power_state(st, false);
	if (result)
		return result;

	result = st->set_power_state(st, true);
	if (result)
		return result;

	/* add by lyx@rock-chips.com  */
	result = inv_plat_single_write(st, reg->user_ctrl, st->i2c_dis);
	if (result)
		return result;

	if (!strcmp(name, "mpu6xxx")) {
		/* for MPU6500, reading register need more time */
		msleep(POWER_UP_TIME);
		result = inv_detect_6xxx(st);
		if (result)
			return result;
	}

	switch (st->chip_type) {
	case INV_ITG3500:
		st->num_channels = INV_CHANNEL_NUM_GYRO;
		break;
	case INV_MPU6050:
	case INV_MPU6500:
		if (SECONDARY_SLAVE_TYPE_COMPASS ==
		    st->plat_data.sec_slave_type) {
			st->chip_config.has_compass = 1;
			st->num_channels =
				INV_CHANNEL_NUM_GYRO_ACCL_QUANTERNION_MAGN;
		} else {
			st->chip_config.has_compass = 0;
			st->num_channels =
				INV_CHANNEL_NUM_GYRO_ACCL_QUANTERNION;
		}
		break;
	case INV_MPU3050:
		if (SECONDARY_SLAVE_TYPE_ACCEL ==
		    st->plat_data.sec_slave_type) {
			if (ACCEL_ID_BMA250 == st->plat_data.sec_slave_id)
				inv_register_mpu3050_slave(st);
			st->num_channels = INV_CHANNEL_NUM_GYRO_ACCL;
		} else {
			st->num_channels = INV_CHANNEL_NUM_GYRO;
		}
		break;
	default:
		result = st->set_power_state(st, false);
		return -ENODEV;
	}
	st->chip_info.multi = 1;
	switch (st->chip_type) {
	case INV_MPU6050:
		result = inv_get_silicon_rev_mpu6050(st);
		break;
	case INV_MPU6500:
		result = inv_get_silicon_rev_mpu6500(st);
		break;
	default:
		result = 0;
		break;
	}
	if (result) {
		pr_err("read silicon rev error\n");
		st->set_power_state(st, false);
		return result;
	}
	/* turn off the gyro engine after OTP reading */
	result = st->switch_gyro_engine(st, false);
	if (result)
		return result;
	result = st->switch_accl_engine(st, false);
	if (result)
		return result;
	if (st->chip_config.has_compass) {
		result = inv_setup_compass(st);
		if (result) {
			pr_err("compass setup failed\n");
			st->set_power_state(st, false);
			return result;
		}
	}

	t_ind = 0;
	memcpy(&inv_attributes[t_ind], inv_gyro_attributes,
		sizeof(inv_gyro_attributes));
	t_ind += ARRAY_SIZE(inv_gyro_attributes);

	if (INV_MPU3050 == st->chip_type && st->mpu_slave != NULL) {
		memcpy(&inv_attributes[t_ind], inv_mpu3050_attributes,
		       sizeof(inv_mpu3050_attributes));
		t_ind += ARRAY_SIZE(inv_mpu3050_attributes);
		inv_attributes[t_ind] = NULL;
		return 0;
	}

	if ((INV_MPU6050 == st->chip_type) || (INV_MPU6500 == st->chip_type)) {
		memcpy(&inv_attributes[t_ind], inv_mpu6050_attributes,
		       sizeof(inv_mpu6050_attributes));
		t_ind += ARRAY_SIZE(inv_mpu6050_attributes);
	}

	if (INV_MPU6500 == st->chip_type) {
		memcpy(&inv_attributes[t_ind], inv_mpu6500_attributes,
		       sizeof(inv_mpu6500_attributes));
		t_ind += ARRAY_SIZE(inv_mpu6500_attributes);
	}

	if (st->chip_config.has_compass) {
		memcpy(&inv_attributes[t_ind], inv_compass_attributes,
		       sizeof(inv_compass_attributes));
		t_ind += ARRAY_SIZE(inv_compass_attributes);
	}
	inv_attributes[t_ind] = NULL;

	return 0;
}

/**
 *  inv_create_dmp_sysfs() - create binary sysfs dmp entry.
 */
static const struct bin_attribute dmp_firmware = {
	.attr = {
		.name = "dmp_firmware",
		.mode = S_IRUGO | S_IWUSR
	},
	.size = 4096,
	.read = inv_dmp_firmware_read,
	.write = inv_dmp_firmware_write,
};

int inv_create_dmp_sysfs(struct iio_dev *ind)
{
	int result;
	result = sysfs_create_bin_file(&ind->dev.kobj, &dmp_firmware);

	return result;
}

/**
 *  @}
 */
