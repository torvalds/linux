// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lsm6ds3h i2c master driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <asm/unaligned.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0)
#include <linux/iio/buffer_impl.h>
#endif /* LINUX_VERSION_CODE */

#include "st_lsm6ds3h.h"

#define EXT0_INDEX				0

#define ST_LSM6DS3H_ODR_LIST_NUM		4
#define ST_LSM6DS3H_SENSOR_HUB_OP_TIMEOUT	5
#define ST_LSM6DS3H_SRC_FUNC_ADDR		0x53
#define ST_LSM6DS3H_EN_BIT			0x01
#define ST_LSM6DS3H_DIS_BIT			0x00
#define ST_LSM6DS3H_SLV0_ADDR_ADDR		0x02
#define ST_LSM6DS3H_SLV1_ADDR_ADDR		0x05
#define ST_LSM6DS3H_SLV2_ADDR_ADDR		0x08
#define ST_LSM6DS3H_SLV0_OUT_ADDR		0x2e
#define ST_LSM6DS3H_INTER_PULLUP_ADDR		0x1a
#define ST_LSM6DS3H_INTER_PULLUP_MASK		0x08
#define ST_LSM6DS3H_FUNC_MAX_RATE_ADDR		0x18
#define ST_LSM6DS3H_FUNC_MAX_RATE_MASK		0x02
#define ST_LSM6DS3H_DATAWRITE_SLV0		0x0e
#define ST_LSM6DS3H_SLVX_READ			0x01

/* External sensors configuration */
#ifdef CONFIG_ST_LSM6DS3H_IIO_EXT0_LIS3MDL
static int lis3mdl_initialization(struct lsm6ds3h_sensor_data *sdata);

#define ST_LSM6DS3H_EXT0_ADDR			0x1e
#define ST_LSM6DS3H_EXT0_ADDR2			0x1c
#define ST_LSM6DS3H_EXT0_WAI_ADDR		0x0f
#define ST_LSM6DS3H_EXT0_WAI_VALUE		0x3d
#define ST_LSM6DS3H_EXT0_RESET_ADDR		0x21
#define ST_LSM6DS3H_EXT0_RESET_MASK		0x04
#define ST_LSM6DS3H_EXT0_FULLSCALE_ADDR		0x21
#define ST_LSM6DS3H_EXT0_FULLSCALE_MASK		0x60
#define ST_LSM6DS3H_EXT0_FULLSCALE_VALUE	0x02
#define ST_LSM6DS3H_EXT0_ODR_ADDR		0x20
#define ST_LSM6DS3H_EXT0_ODR_MASK		0x1c
#define ST_LSM6DS3H_EXT0_ODR0_HZ		10
#define ST_LSM6DS3H_EXT0_ODR0_VALUE		0x04
#define ST_LSM6DS3H_EXT0_ODR1_HZ		20
#define ST_LSM6DS3H_EXT0_ODR1_VALUE		0x05
#define ST_LSM6DS3H_EXT0_ODR2_HZ		40
#define ST_LSM6DS3H_EXT0_ODR2_VALUE		0x06
#define ST_LSM6DS3H_EXT0_ODR3_HZ		80
#define ST_LSM6DS3H_EXT0_ODR3_VALUE		0x07
#define ST_LSM6DS3H_EXT0_PW_ADDR		0x22
#define ST_LSM6DS3H_EXT0_PW_MASK		0x03
#define ST_LSM6DS3H_EXT0_PW_OFF			0x02
#define ST_LSM6DS3H_EXT0_PW_ON			0x00
#define ST_LSM6DS3H_EXT0_GAIN_VALUE		438
#define ST_LSM6DS3H_EXT0_OUT_X_L_ADDR		0x28
#define ST_LSM6DS3H_EXT0_OUT_Y_L_ADDR		0x2a
#define ST_LSM6DS3H_EXT0_OUT_Z_L_ADDR		0x2c
#define ST_LSM6DS3H_EXT0_READ_DATA_LEN		6
#define ST_LSM6DS3H_EXT0_BDU_ADDR		0x24
#define ST_LSM6DS3H_EXT0_BDU_MASK		0x40
#define ST_LSM6DS3H_EXT0_STD			0
#define ST_LSM6DS3H_EXT0_BOOT_FUNCTION		(&lis3mdl_initialization)
#define ST_LSM6DS3H_SELFTEST_EXT0_MIN		2281
#define ST_LSM6DS3H_SELFTEST_EXT0_MAX		6843
#define ST_LSM6DS3H_SELFTEST_EXT0_MIN_Z		228
#define ST_LSM6DS3H_SELFTEST_EXT0_MAX_Z		2281
#define ST_LSM6DS3H_SELFTEST_ADDR1		0x20
#define ST_LSM6DS3H_SELFTEST_ADDR2		0x21
#define ST_LSM6DS3H_SELFTEST_ADDR3		0x22
#define ST_LSM6DS3H_SELFTEST_ADDR1_VALUE	0x1c
#define ST_LSM6DS3H_SELFTEST_ADDR2_VALUE	0x40
#define ST_LSM6DS3H_SELFTEST_ADDR3_VALUE	0x00
#define ST_LSM6DS3H_SELFTEST_ENABLE		0x1d
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_LIS3MDL */

#ifdef CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09911
static int akm09911_initialization(struct lsm6ds3h_sensor_data *sdata);

#define ST_LSM6DS3H_EXT0_ADDR			0x0c
#define ST_LSM6DS3H_EXT0_ADDR2			0x0d
#define ST_LSM6DS3H_EXT0_WAI_ADDR		0x01
#define ST_LSM6DS3H_EXT0_WAI_VALUE		0x05
#define ST_LSM6DS3H_EXT0_RESET_ADDR		0x32
#define ST_LSM6DS3H_EXT0_RESET_MASK		0x01
#define ST_LSM6DS3H_EXT0_FULLSCALE_ADDR		0x00
#define ST_LSM6DS3H_EXT0_FULLSCALE_MASK		0x00
#define ST_LSM6DS3H_EXT0_FULLSCALE_VALUE	0x00
#define ST_LSM6DS3H_EXT0_ODR_ADDR		0x31
#define ST_LSM6DS3H_EXT0_ODR_MASK		0x1f
#define ST_LSM6DS3H_EXT0_ODR0_HZ		10
#define ST_LSM6DS3H_EXT0_ODR0_VALUE		0x02
#define ST_LSM6DS3H_EXT0_ODR1_HZ		20
#define ST_LSM6DS3H_EXT0_ODR1_VALUE		0x04
#define ST_LSM6DS3H_EXT0_ODR2_HZ		50
#define ST_LSM6DS3H_EXT0_ODR2_VALUE		0x06
#define ST_LSM6DS3H_EXT0_ODR3_HZ		100
#define ST_LSM6DS3H_EXT0_ODR3_VALUE		0x08
#define ST_LSM6DS3H_EXT0_PW_ADDR		ST_LSM6DS3H_EXT0_ODR_ADDR
#define ST_LSM6DS3H_EXT0_PW_MASK		ST_LSM6DS3H_EXT0_ODR_MASK
#define ST_LSM6DS3H_EXT0_PW_OFF			0x00
#define ST_LSM6DS3H_EXT0_PW_ON			ST_LSM6DS3H_EXT0_ODR0_VALUE
#define ST_LSM6DS3H_EXT0_GAIN_VALUE		6000
#define ST_LSM6DS3H_EXT0_OUT_X_L_ADDR		0x11
#define ST_LSM6DS3H_EXT0_OUT_Y_L_ADDR		0x13
#define ST_LSM6DS3H_EXT0_OUT_Z_L_ADDR		0x15
#define ST_LSM6DS3H_EXT0_READ_DATA_LEN		6
#define ST_LSM6DS3H_EXT0_SENSITIVITY_ADDR	0x60
#define ST_LSM6DS3H_EXT0_SENSITIVITY_LEN	3
#define ST_LSM6DS3H_EXT0_STD			0
#define ST_LSM6DS3H_EXT0_BOOT_FUNCTION		(&akm09911_initialization)
#define ST_LSM6DS3H_EXT0_DATA_STATUS		0x18
#define ST_LSM6DS3H_SELFTEST_EXT0_MIN		(-30)
#define ST_LSM6DS3H_SELFTEST_EXT0_MAX		30
#define ST_LSM6DS3H_SELFTEST_EXT0_MIN_Z		(-400)
#define ST_LSM6DS3H_SELFTEST_EXT0_MAX_Z		(-50)
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09911 */

#ifdef CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09912
static int akm09912_initialization(struct lsm6ds3h_sensor_data *sdata);

#define ST_LSM6DS3H_EXT0_ADDR			0x0c
#define ST_LSM6DS3H_EXT0_ADDR2			0x0d
#define ST_LSM6DS3H_EXT0_WAI_ADDR		0x01
#define ST_LSM6DS3H_EXT0_WAI_VALUE		0x04
#define ST_LSM6DS3H_EXT0_RESET_ADDR		0x32
#define ST_LSM6DS3H_EXT0_RESET_MASK		0x01
#define ST_LSM6DS3H_EXT0_FULLSCALE_ADDR		0x00
#define ST_LSM6DS3H_EXT0_FULLSCALE_MASK		0x00
#define ST_LSM6DS3H_EXT0_FULLSCALE_VALUE	0x00
#define ST_LSM6DS3H_EXT0_ODR_ADDR		0x31
#define ST_LSM6DS3H_EXT0_ODR_MASK		0x1f
#define ST_LSM6DS3H_EXT0_ODR0_HZ		10
#define ST_LSM6DS3H_EXT0_ODR0_VALUE		0x02
#define ST_LSM6DS3H_EXT0_ODR1_HZ		20
#define ST_LSM6DS3H_EXT0_ODR1_VALUE		0x04
#define ST_LSM6DS3H_EXT0_ODR2_HZ		50
#define ST_LSM6DS3H_EXT0_ODR2_VALUE		0x06
#define ST_LSM6DS3H_EXT0_ODR3_HZ		100
#define ST_LSM6DS3H_EXT0_ODR3_VALUE		0x08
#define ST_LSM6DS3H_EXT0_PW_ADDR		ST_LSM6DS3H_EXT0_ODR_ADDR
#define ST_LSM6DS3H_EXT0_PW_MASK		ST_LSM6DS3H_EXT0_ODR_MASK
#define ST_LSM6DS3H_EXT0_PW_OFF			0x00
#define ST_LSM6DS3H_EXT0_PW_ON			ST_LSM6DS3H_EXT0_ODR0_VALUE
#define ST_LSM6DS3H_EXT0_GAIN_VALUE		1500
#define ST_LSM6DS3H_EXT0_OUT_X_L_ADDR		0x11
#define ST_LSM6DS3H_EXT0_OUT_Y_L_ADDR		0x13
#define ST_LSM6DS3H_EXT0_OUT_Z_L_ADDR		0x15
#define ST_LSM6DS3H_EXT0_READ_DATA_LEN		6
#define ST_LSM6DS3H_EXT0_SENSITIVITY_ADDR	0x60
#define ST_LSM6DS3H_EXT0_SENSITIVITY_LEN	3
#define ST_LSM6DS3H_EXT0_STD			0
#define ST_LSM6DS3H_EXT0_BOOT_FUNCTION		(&akm09912_initialization)
#define ST_LSM6DS3H_EXT0_DATA_STATUS		0x18
#define ST_LSM6DS3H_SELFTEST_EXT0_MIN		(-200)
#define ST_LSM6DS3H_SELFTEST_EXT0_MAX		200
#define ST_LSM6DS3H_SELFTEST_EXT0_MIN_Z		(-1600)
#define ST_LSM6DS3H_SELFTEST_EXT0_MAX_Z		(-400)
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09912 */

#ifdef CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09916
#define ST_LSM6DS3H_EXT0_ADDR			0x0c
#define ST_LSM6DS3H_EXT0_ADDR2			0x0c
#define ST_LSM6DS3H_EXT0_WAI_ADDR		0x01
#define ST_LSM6DS3H_EXT0_WAI_VALUE		0x09
#define ST_LSM6DS3H_EXT0_RESET_ADDR		0x32
#define ST_LSM6DS3H_EXT0_RESET_MASK		0x01
#define ST_LSM6DS3H_EXT0_FULLSCALE_ADDR		0x00
#define ST_LSM6DS3H_EXT0_FULLSCALE_MASK		0x00
#define ST_LSM6DS3H_EXT0_FULLSCALE_VALUE	0x00
#define ST_LSM6DS3H_EXT0_ODR_ADDR		0x31
#define ST_LSM6DS3H_EXT0_ODR_MASK		0x1f
#define ST_LSM6DS3H_EXT0_ODR0_HZ		10
#define ST_LSM6DS3H_EXT0_ODR0_VALUE		0x02
#define ST_LSM6DS3H_EXT0_ODR1_HZ		20
#define ST_LSM6DS3H_EXT0_ODR1_VALUE		0x04
#define ST_LSM6DS3H_EXT0_ODR2_HZ		50
#define ST_LSM6DS3H_EXT0_ODR2_VALUE		0x06
#define ST_LSM6DS3H_EXT0_ODR3_HZ		100
#define ST_LSM6DS3H_EXT0_ODR3_VALUE		0x08
#define ST_LSM6DS3H_EXT0_PW_ADDR		ST_LSM6DS3H_EXT0_ODR_ADDR
#define ST_LSM6DS3H_EXT0_PW_MASK		ST_LSM6DS3H_EXT0_ODR_MASK
#define ST_LSM6DS3H_EXT0_PW_OFF			0x00
#define ST_LSM6DS3H_EXT0_PW_ON			ST_LSM6DS3H_EXT0_ODR0_VALUE
#define ST_LSM6DS3H_EXT0_GAIN_VALUE		1500
#define ST_LSM6DS3H_EXT0_OUT_X_L_ADDR		0x11
#define ST_LSM6DS3H_EXT0_OUT_Y_L_ADDR		0x13
#define ST_LSM6DS3H_EXT0_OUT_Z_L_ADDR		0x15
#define ST_LSM6DS3H_EXT0_READ_DATA_LEN		6
#define ST_LSM6DS3H_EXT0_SENSITIVITY_ADDR	0x60
#define ST_LSM6DS3H_EXT0_SENSITIVITY_LEN	3
#define ST_LSM6DS3H_EXT0_STD			0
#define ST_LSM6DS3H_EXT0_BOOT_FUNCTION		NULL
#define ST_LSM6DS3H_EXT0_DATA_STATUS		0x18
#define ST_LSM6DS3H_SELFTEST_EXT0_MIN		(-200)
#define ST_LSM6DS3H_SELFTEST_EXT0_MAX		200
#define ST_LSM6DS3H_SELFTEST_EXT0_MIN_Z		(-1000)
#define ST_LSM6DS3H_SELFTEST_EXT0_MAX_Z		(-200)
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09916 */


#ifdef CONFIG_ST_LSM6DS3H_IIO_EXT0_LPS22HB
static int lps22hb_initialization(struct lsm6ds3h_sensor_data *sdata);

#define ST_LSM6DS3H_EXT0_ADDR			0x5d
#define ST_LSM6DS3H_EXT0_ADDR2			0x5c
#define ST_LSM6DS3H_EXT0_WAI_ADDR		0x0f
#define ST_LSM6DS3H_EXT0_WAI_VALUE		0xb1
#define ST_LSM6DS3H_EXT0_RESET_ADDR		0x11
#define ST_LSM6DS3H_EXT0_RESET_MASK		0x80
#define ST_LSM6DS3H_EXT0_FULLSCALE_ADDR		0x00
#define ST_LSM6DS3H_EXT0_FULLSCALE_MASK		0x00
#define ST_LSM6DS3H_EXT0_FULLSCALE_VALUE	0x00
#define ST_LSM6DS3H_EXT0_ODR_ADDR		0x10
#define ST_LSM6DS3H_EXT0_ODR_MASK		0x70
#define ST_LSM6DS3H_EXT0_ODR0_HZ		1
#define ST_LSM6DS3H_EXT0_ODR0_VALUE		0x01
#define ST_LSM6DS3H_EXT0_ODR1_HZ		10
#define ST_LSM6DS3H_EXT0_ODR1_VALUE		0x02
#define ST_LSM6DS3H_EXT0_ODR2_HZ		25
#define ST_LSM6DS3H_EXT0_ODR2_VALUE		0x03
#define ST_LSM6DS3H_EXT0_ODR3_HZ		50
#define ST_LSM6DS3H_EXT0_ODR3_VALUE		0x04
#define ST_LSM6DS3H_EXT0_PW_ADDR		ST_LSM6DS3H_EXT0_ODR_ADDR
#define ST_LSM6DS3H_EXT0_PW_MASK		ST_LSM6DS3H_EXT0_ODR_MASK
#define ST_LSM6DS3H_EXT0_PW_OFF			0x00
#define ST_LSM6DS3H_EXT0_PW_ON			ST_LSM6DS3H_EXT0_ODR0_VALUE
#define ST_LSM6DS3H_EXT0_GAIN_VALUE		244
#define ST_LSM6DS3H_EXT0_OUT_P_L_ADDR		0x28
#define ST_LSM6DS3H_EXT0_OUT_T_L_ADDR		0x2b
#define ST_LSM6DS3H_EXT0_READ_DATA_LEN		5
#define ST_LSM6DS3H_EXT0_BDU_ADDR		0x10
#define ST_LSM6DS3H_EXT0_BDU_MASK		0x02
#define ST_LSM6DS3H_EXT0_STD			0
#define ST_LSM6DS3H_EXT0_BOOT_FUNCTION		(&lps22hb_initialization)
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_LPS22HB */

/* SENSORS SUFFIX NAMES */
#define ST_LSM6DS3H_EXT0_SUFFIX_NAME		"magn"
#define ST_LSM6DS3H_EXT1_SUFFIX_NAME		"press"

#if defined(CONFIG_ST_LSM6DS3H_IIO_EXT0_LIS3MDL) || \
			defined(CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09912) || \
			defined(CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09916) || \
			defined(CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09911)
#define ST_LSM6DS3H_EXT0_HAS_SELFTEST		1
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_MAGN */

#if defined(CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09912) || \
			defined(CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09916) || \
			defined(CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09911)
#define ST_LSM6DS3H_EXT0_IS_AKM			1
#define ST_LSM6DS3H_SELFTEST_STATUS_REG		0x10
#define ST_LSM6DS3H_SELFTEST_ADDR		0x31
#define ST_LSM6DS3H_SELFTEST_ENABLE		0x10
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM0099xx */


struct st_lsm6ds3h_i2c_master_odr_reg {
	unsigned int hz;
	u8 value;
};

struct st_lsm6ds3h_i2c_master_odr_table {
	u8 addr;
	u8 mask;
	struct st_lsm6ds3h_i2c_master_odr_reg odr_avl[ST_LSM6DS3H_ODR_LIST_NUM];
};

static int st_lsm6ds3h_i2c_master_read_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *ch, int *val, int *val2, long mask);

#ifdef CONFIG_ST_LSM6DS3H_IIO_EXT0_LPS22HB
static const struct iio_chan_spec st_lsm6ds3h_ext0_ch[] = {
	ST_LSM6DS3H_LSM_CHANNELS(IIO_PRESSURE, 0, 0, IIO_NO_MOD, IIO_LE,
				24, 24, ST_LSM6DS3H_EXT0_OUT_P_L_ADDR, 'u'),
	ST_LSM6DS3H_LSM_CHANNELS(IIO_TEMP, 0, 1, IIO_NO_MOD, IIO_LE,
				16, 16, ST_LSM6DS3H_EXT0_OUT_T_L_ADDR, 's'),
	ST_LSM6DS3H_FLUSH_CHANNEL(IIO_PRESSURE),
	IIO_CHAN_SOFT_TIMESTAMP(2)
};
#else /* CONFIG_ST_LSM6DS3H_IIO_EXT0_LPS22HB */
static const struct iio_chan_spec st_lsm6ds3h_ext0_ch[] = {
	ST_LSM6DS3H_LSM_CHANNELS(IIO_MAGN, 1, 0, IIO_MOD_X, IIO_LE,
				16, 16, ST_LSM6DS3H_EXT0_OUT_X_L_ADDR, 's'),
	ST_LSM6DS3H_LSM_CHANNELS(IIO_MAGN, 1, 1, IIO_MOD_Y, IIO_LE,
				16, 16, ST_LSM6DS3H_EXT0_OUT_Y_L_ADDR, 's'),
	ST_LSM6DS3H_LSM_CHANNELS(IIO_MAGN, 1, 2, IIO_MOD_Z, IIO_LE,
				16, 16, ST_LSM6DS3H_EXT0_OUT_Z_L_ADDR, 's'),
	ST_LSM6DS3H_FLUSH_CHANNEL(IIO_MAGN),
	IIO_CHAN_SOFT_TIMESTAMP(3)
};
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_LPS22HB */

static int st_lsm6ds3h_i2c_master_set_odr(struct lsm6ds3h_sensor_data *sdata,
						unsigned int odr, bool force);

static int st_lsm6ds3h_i2c_master_write(struct lsm6ds3h_data *cdata,
		u8 reg_addr, int len, u8 *data, bool en_sensor_hub, bool transfer_lock);
static int st_lsm6ds3h_i2c_master_read(struct lsm6ds3h_data *cdata,
		u8 reg_addr, int len, u8 *data, bool en_sensor_hub,
				bool transfer_lock, bool read_status_end, u8 offset);

#ifdef ST_LSM6DS3H_EXT0_HAS_SELFTEST
static ssize_t st_lsm6ds3h_i2c_master_sysfs_get_selftest_available(
		struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t st_lsm6ds3h_i2c_master_sysfs_get_selftest_status(
		struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t st_lsm6ds3h_i2c_master_sysfs_start_selftest(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);
#endif /* ST_LSM6DS3H_EXT0_HAS_SELFTEST	*/

static ssize_t st_lsm6ds3h_i2c_master_sysfs_sampling_frequency_avail(
		struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE,
			"%d %d %d %d\n", 13, 26, 52, 104);
}

static ssize_t st_lsm6ds3h_i2c_master_sysfs_get_sampling_frequency(
		struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lsm6ds3h_sensor_data *sdata = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "%d\n", sdata->cdata->v_odr[sdata->sindex]);
}

static ssize_t st_lsm6ds3h_i2c_master_sysfs_set_sampling_frequency(
			struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	int err;
	unsigned int odr;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lsm6ds3h_sensor_data *sdata = iio_priv(indio_dev);

	err = kstrtoint(buf, 10, &odr);
	if (err < 0)
		return err;

	mutex_lock(&indio_dev->mlock);
	mutex_lock(&sdata->cdata->odr_lock);

	if (sdata->cdata->v_odr[sdata->sindex] != odr)
		err = st_lsm6ds3h_i2c_master_set_odr(sdata, odr, false);

	mutex_unlock(&sdata->cdata->odr_lock);
	mutex_unlock(&indio_dev->mlock);

	return err < 0 ? err : size;
}

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
			st_lsm6ds3h_i2c_master_sysfs_get_sampling_frequency,
			st_lsm6ds3h_i2c_master_sysfs_set_sampling_frequency);

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(
			st_lsm6ds3h_i2c_master_sysfs_sampling_frequency_avail);

static ST_LSM6DS3H_HWFIFO_ENABLED();
static ST_LSM6DS3H_HWFIFO_WATERMARK();
static ST_LSM6DS3H_HWFIFO_WATERMARK_MIN();
static ST_LSM6DS3H_HWFIFO_WATERMARK_MAX();
static ST_LSM6DS3H_HWFIFO_FLUSH();

static IIO_DEVICE_ATTR(module_id, 0444, st_lsm6ds3h_get_module_id, NULL, 0);

#ifdef ST_LSM6DS3H_EXT0_HAS_SELFTEST
static IIO_DEVICE_ATTR(selftest_available, S_IRUGO,
			st_lsm6ds3h_i2c_master_sysfs_get_selftest_available,
			NULL, 0);

static IIO_DEVICE_ATTR(selftest, S_IWUSR | S_IRUGO,
			st_lsm6ds3h_i2c_master_sysfs_get_selftest_status,
			st_lsm6ds3h_i2c_master_sysfs_start_selftest, 0);
#endif /* ST_LSM6DS3H_EXT0_HAS_SELFTEST	*/

static struct attribute *st_lsm6ds3h_ext0_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_hwfifo_enabled.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_min.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_flush.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,

#ifdef ST_LSM6DS3H_EXT0_HAS_SELFTEST
	&iio_dev_attr_selftest_available.dev_attr.attr,
	&iio_dev_attr_selftest.dev_attr.attr,
#endif /* ST_LSM6DS3H_EXT0_HAS_SELFTEST	*/

	NULL,
};

static const struct attribute_group st_lsm6ds3h_ext0_attribute_group = {
	.attrs = st_lsm6ds3h_ext0_attributes,
};

static const struct iio_info st_lsm6ds3h_ext0_info = {
	.attrs = &st_lsm6ds3h_ext0_attribute_group,
	.read_raw = &st_lsm6ds3h_i2c_master_read_raw,
};

struct st_lsm6ds3h_iio_info_data {
	char suffix_name[20];
	struct iio_info *info;
	struct iio_chan_spec *channels;
	int num_channels;
};

struct st_lsm6ds3h_reg {
	u8 addr;
	u8 mask;
	u8 def_value;
};

struct st_lsm6ds3h_power_reg {
	u8 addr;
	u8 mask;
	u8 off_value;
	u8 on_value;
	bool isodr;
};

struct st_lsm6ds3h_custom_function {
	int (*boot_initialization)(struct lsm6ds3h_sensor_data *sdata);
};

static struct st_lsm6ds3h_exs_list {
	struct st_lsm6ds3h_reg wai;
	struct st_lsm6ds3h_reg reset;
	struct st_lsm6ds3h_reg fullscale;
	struct st_lsm6ds3h_i2c_master_odr_table odr;
	struct st_lsm6ds3h_power_reg power;
	u8 fullscale_value;
	u8 samples_to_discard;
	u8 read_data_len;
	u8 num_data_channels;
	bool available;
	unsigned int gain;
	u8 i2c_addr;
	struct st_lsm6ds3h_iio_info_data data;
	struct st_lsm6ds3h_custom_function cf;
} st_lsm6ds3h_exs_list[] = {
	{
		.wai = {
			.addr = ST_LSM6DS3H_EXT0_WAI_ADDR,
			.def_value = ST_LSM6DS3H_EXT0_WAI_VALUE,
		},
		.reset = {
			.addr = ST_LSM6DS3H_EXT0_RESET_ADDR,
			.mask = ST_LSM6DS3H_EXT0_RESET_MASK,
		},
		.fullscale = {
			.addr = ST_LSM6DS3H_EXT0_FULLSCALE_ADDR,
			.mask = ST_LSM6DS3H_EXT0_FULLSCALE_MASK,
			.def_value = ST_LSM6DS3H_EXT0_FULLSCALE_VALUE,
		},
		.odr = {
			.addr = ST_LSM6DS3H_EXT0_ODR_ADDR,
			.mask = ST_LSM6DS3H_EXT0_ODR_MASK,
			.odr_avl = {
				{
				.hz = ST_LSM6DS3H_EXT0_ODR0_HZ,
				.value = ST_LSM6DS3H_EXT0_ODR0_VALUE,
				},
				{
				.hz = ST_LSM6DS3H_EXT0_ODR1_HZ,
				.value = ST_LSM6DS3H_EXT0_ODR1_VALUE,
				},
				{
				.hz = ST_LSM6DS3H_EXT0_ODR2_HZ,
				.value = ST_LSM6DS3H_EXT0_ODR2_VALUE,
				},
				{
				.hz = ST_LSM6DS3H_EXT0_ODR3_HZ,
				.value = ST_LSM6DS3H_EXT0_ODR3_VALUE,
				},
			},
		},
		.power = {
			.addr = ST_LSM6DS3H_EXT0_PW_ADDR,
			.mask = ST_LSM6DS3H_EXT0_PW_MASK,
			.off_value = ST_LSM6DS3H_EXT0_PW_OFF,
			.on_value = ST_LSM6DS3H_EXT0_PW_ON,
		},
		.samples_to_discard = ST_LSM6DS3H_EXT0_STD,
		.read_data_len = ST_LSM6DS3H_EXT0_READ_DATA_LEN,
		.num_data_channels = 3,
		.available = false,
		.gain = ST_LSM6DS3H_EXT0_GAIN_VALUE,
		.i2c_addr = ST_LSM6DS3H_EXT0_ADDR,
		.data = {
			.suffix_name = ST_LSM6DS3H_EXT0_SUFFIX_NAME,
			.info = (struct iio_info *)&st_lsm6ds3h_ext0_info,
			.channels = (struct iio_chan_spec *)&st_lsm6ds3h_ext0_ch,
			.num_channels = ARRAY_SIZE(st_lsm6ds3h_ext0_ch),
		},
		.cf.boot_initialization = ST_LSM6DS3H_EXT0_BOOT_FUNCTION,
	}
};

static inline void st_lsm6ds3h_master_wait_completed(struct lsm6ds3h_data *cdata)
{
	msleep((1000U / cdata->trigger_odr) + 2);
}

static int st_lsm6ds3h_i2c_master_read(struct lsm6ds3h_data *cdata,
		u8 reg_addr, int len, u8 *data, bool en_sensor_hub,
			bool transfer_lock, bool read_status_end, u8 offset)
{
	int err;
	u8 slave_conf[3];

	slave_conf[0] = (st_lsm6ds3h_exs_list[EXT0_INDEX].i2c_addr << 1) |
							ST_LSM6DS3H_SLVX_READ;
	slave_conf[1] = reg_addr;
	slave_conf[2] = (len & 0x07);

	if (transfer_lock)
		mutex_lock(&cdata->i2c_transfer_lock);

	err = st_lsm6ds3h_write_embedded_registers(cdata,
					ST_LSM6DS3H_SLV2_ADDR_ADDR, slave_conf,
					ARRAY_SIZE(slave_conf));
	if (err < 0)
		goto i2c_master_read_unlock_mutex;

	if (en_sensor_hub) {
		err = st_lsm6ds3h_enable_sensor_hub(cdata, true,
						ST_MASK_ID_SENSOR_HUB_ASYNC_OP);
		if (err < 0)
			goto i2c_master_read_unlock_mutex;
	}

	st_lsm6ds3h_master_wait_completed(cdata);

	err = cdata->tf->read(cdata, ST_LSM6DS3H_SLV0_OUT_ADDR +
					offset, len & 0x07, data, true);
	if (err < 0)
		goto i2c_master_read_unlock_mutex;

#ifdef ST_LSM6DS3H_EXT0_IS_AKM
	if (read_status_end) {
		slave_conf[0] = (st_lsm6ds3h_exs_list[EXT0_INDEX].i2c_addr << 1) | 0x01;
		slave_conf[1] = ST_LSM6DS3H_EXT0_DATA_STATUS;
		slave_conf[2] = 0x01;

		err = st_lsm6ds3h_write_embedded_registers(cdata,
					ST_LSM6DS3H_SLV2_ADDR_ADDR, slave_conf,
					ARRAY_SIZE(slave_conf));
		if (err < 0)
			goto i2c_master_read_unlock_mutex;
	}
#endif /* ST_LSM6DS3H_EXT0_IS_AKM */

	if (en_sensor_hub) {
		err = st_lsm6ds3h_enable_sensor_hub(cdata, false,
						ST_MASK_ID_SENSOR_HUB_ASYNC_OP);
		if (err < 0)
			goto i2c_master_read_unlock_mutex;
	}

i2c_master_read_unlock_mutex:
	if (transfer_lock)
		mutex_unlock(&cdata->i2c_transfer_lock);

	return err < 0 ? err : len & 0x07;
}

static int st_lsm6ds3h_i2c_master_write(struct lsm6ds3h_data *cdata,
		u8 reg_addr, int len, u8 *data, bool en_sensor_hub, bool transfer_lock)
{
	int err, i = 0;
	u8 slave0_conf[2];

	if (transfer_lock)
		mutex_lock(&cdata->i2c_transfer_lock);

	while (i < len) {
		slave0_conf[0] = (st_lsm6ds3h_exs_list[EXT0_INDEX].i2c_addr << 1);
		slave0_conf[1] = reg_addr + i;

		err = st_lsm6ds3h_write_embedded_registers(cdata,
						ST_LSM6DS3H_SLV0_ADDR_ADDR,
						slave0_conf,
						ARRAY_SIZE(slave0_conf));
		if (err < 0)
			goto i2c_master_write_unlock_mutex;

		slave0_conf[0] = data[i];

		err = st_lsm6ds3h_write_embedded_registers(cdata,
						ST_LSM6DS3H_DATAWRITE_SLV0,
						slave0_conf, 1);
		if (err < 0)
			goto i2c_master_write_unlock_mutex;

		if (en_sensor_hub) {
			err = st_lsm6ds3h_enable_sensor_hub(cdata, true,
						ST_MASK_ID_SENSOR_HUB_ASYNC_OP);
			if (err < 0)
				goto i2c_master_write_unlock_mutex;
		}

		st_lsm6ds3h_master_wait_completed(cdata);

		if (en_sensor_hub) {
			err = st_lsm6ds3h_enable_sensor_hub(cdata, false,
						ST_MASK_ID_SENSOR_HUB_ASYNC_OP);
			if (err < 0)
				goto i2c_master_write_unlock_mutex;
		}

		i++;
	}

	slave0_conf[0] = (st_lsm6ds3h_exs_list[EXT0_INDEX].i2c_addr << 1);
	slave0_conf[1] = st_lsm6ds3h_exs_list[EXT0_INDEX].wai.addr;

	st_lsm6ds3h_write_embedded_registers(cdata,
						ST_LSM6DS3H_SLV0_ADDR_ADDR,
						slave0_conf,
						ARRAY_SIZE(slave0_conf));

i2c_master_write_unlock_mutex:
	if (transfer_lock)
		mutex_unlock(&cdata->i2c_transfer_lock);

	return err < 0 ? err : len;
}

static int st_lsm6ds3h_i2c_master_write_data_with_mask(
		struct lsm6ds3h_data *cdata, u8 reg_addr, u8 mask, u8 data)
{
	int err;
	u8 new_data = 0x00, old_data = 0x00;

	mutex_lock(&cdata->i2c_transfer_lock);
	disable_irq(cdata->irq);

	err = st_lsm6ds3h_enable_sensor_hub(cdata, true,
						ST_MASK_ID_SENSOR_HUB_ASYNC_OP);
	if (err < 0) {
		enable_irq(cdata->irq);
		mutex_unlock(&cdata->i2c_transfer_lock);
		return err;
	}

	err = st_lsm6ds3h_i2c_master_read(cdata, reg_addr, 1,
					&old_data, false, false, true,
					st_lsm6ds3h_exs_list[0].read_data_len);
	if (err < 0) {
		enable_irq(cdata->irq);
		mutex_unlock(&cdata->i2c_transfer_lock);
		return err;
	}

	new_data = ((old_data & (~mask)) | ((data << __ffs(mask)) & mask));

	if (new_data != old_data)
		err = st_lsm6ds3h_i2c_master_write(cdata, reg_addr,
						1, &new_data, false, false);

	st_lsm6ds3h_enable_sensor_hub(cdata, false,
						ST_MASK_ID_SENSOR_HUB_ASYNC_OP);

	enable_irq(cdata->irq);
	mutex_unlock(&cdata->i2c_transfer_lock);

	return err;
}

#ifdef CONFIG_ST_LSM6DS3H_IIO_EXT0_LIS3MDL
static int lis3mdl_initialization(struct lsm6ds3h_sensor_data *sdata)
{

	return st_lsm6ds3h_i2c_master_write_data_with_mask(
				sdata->cdata,
				ST_LSM6DS3H_EXT0_BDU_ADDR,
				ST_LSM6DS3H_EXT0_BDU_MASK, ST_LSM6DS3H_EN_BIT);
}
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_LIS3MDL */

#ifdef CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09911
static int akm09911_initialization(struct lsm6ds3h_sensor_data *sdata)
{
	int err; u8 data[ST_LSM6DS3H_EXT0_SENSITIVITY_LEN];

	err = st_lsm6ds3h_i2c_master_read(sdata->cdata,
				ST_LSM6DS3H_EXT0_SENSITIVITY_ADDR,
				ST_LSM6DS3H_EXT0_SENSITIVITY_LEN,
				data, true, true, false,
				st_lsm6ds3h_exs_list[0].read_data_len);
	if (err < 0)
		return err;

	/* gain expressed in nT/LSB */
	sdata->c_gain[0] = (((((int)data[0]) * 1000) >> 7) + 1000);
	sdata->c_gain[1] = (((((int)data[1]) * 1000) >> 7) + 1000);
	sdata->c_gain[2] = (((((int)data[2]) * 1000) >> 7) + 1000);

	/* gain expressed in G/LSB */
	sdata->c_gain[0] *= 10;
	sdata->c_gain[1] *= 10;
	sdata->c_gain[2] *= 10;

	return 0;
}
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09911 */

#ifdef CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09912
static int akm09912_initialization(struct lsm6ds3h_sensor_data *sdata)
{
	int err; u8 data[ST_LSM6DS3H_EXT0_SENSITIVITY_LEN];

	err = st_lsm6ds3h_i2c_master_read(sdata->cdata,
				ST_LSM6DS3H_EXT0_SENSITIVITY_ADDR,
				ST_LSM6DS3H_EXT0_SENSITIVITY_LEN,
				data, true, true, false,
				st_lsm6ds3h_exs_list[0].read_data_len);
	if (err < 0)
		return err;

	/* gain expressed in nT/LSB */
	sdata->c_gain[0] = (((((int)data[0] - 128) * 500) >> 7) + 1000);
	sdata->c_gain[1] = (((((int)data[1] - 128) * 500) >> 7) + 1000);
	sdata->c_gain[2] = (((((int)data[2] - 128) * 500) >> 7) + 1000);

	/* gain expressed in G/LSB */
	sdata->c_gain[0] *= 10;
	sdata->c_gain[1] *= 10;
	sdata->c_gain[2] *= 10;

	return 0;
}
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09912 */

#ifdef CONFIG_ST_LSM6DS3H_IIO_EXT0_LPS22HB
static int lps22hb_initialization(struct lsm6ds3h_sensor_data *sdata)
{

	return st_lsm6ds3h_i2c_master_write_data_with_mask(
				sdata->cdata,
				ST_LSM6DS3H_EXT0_BDU_ADDR,
				ST_LSM6DS3H_EXT0_BDU_MASK, ST_LSM6DS3H_EN_BIT);
}
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_LPS22HB */

#ifdef ST_LSM6DS3H_EXT0_HAS_SELFTEST
static ssize_t st_lsm6ds3h_i2c_master_sysfs_get_selftest_available(
		struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "absolute\n");
}

static ssize_t st_lsm6ds3h_i2c_master_sysfs_get_selftest_status(
		struct device *dev, struct device_attribute *attr, char *buf)
{
	int8_t result;
	char *message = NULL;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lsm6ds3h_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&sdata->cdata->odr_lock);
	result = sdata->cdata->ext0_selftest_status;
	mutex_unlock(&sdata->cdata->odr_lock);

	if (result == 0)
		message = ST_LSM6DS3H_SELFTEST_NA_MS;
	else if (result < 0)
		message = ST_LSM6DS3H_SELFTEST_FAIL_MS;
	else if (result > 0)
		message = ST_LSM6DS3H_SELFTEST_PASS_MS;

	return sprintf(buf, "%s\n", message);
}

static ssize_t st_lsm6ds3h_i2c_master_sysfs_start_selftest(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int err;
	int x_selftest = 0, y_selftest = 0, z_selftest = 0;
	u8 outdata[8], reg_addr, reg_status = 0, temp_reg_status;
#ifdef CONFIG_ST_LSM6DS3H_IIO_EXT0_LIS3MDL
	int i, x = 0, y = 0, z = 0;
	u8 reg_status2 = 0, reg_status3 = 0;
	u8 reg_addr2, reg_addr3, temp_reg_status2, temp_reg_status3;
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_LIS3MDL */
#ifdef ST_LSM6DS3H_EXT0_IS_AKM
	u8 temp, sh_config[3], timeout = 0;
#endif /* ST_LSM6DS3H_EXT0_IS_AKM */
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct lsm6ds3h_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&sdata->cdata->odr_lock);
	sdata->cdata->ext0_selftest_status = 0;

	if (sdata->cdata->sensors_enabled > 0) {
		mutex_unlock(&sdata->cdata->odr_lock);
		return -EBUSY;
	}

	if (strncmp(buf, "absolute", size - 2) != 0) {
		mutex_unlock(&sdata->cdata->odr_lock);
		return -EINVAL;
	}

	err = st_lsm6ds3h_enable_sensor_hub(sdata->cdata, true, ST_MASK_ID_EXT0);
	if (err < 0) {
		mutex_unlock(&sdata->cdata->odr_lock);
		return err;
	}

#ifdef CONFIG_ST_LSM6DS3H_IIO_EXT0_LIS3MDL
	reg_addr = ST_LSM6DS3H_SELFTEST_ADDR1;
	temp_reg_status = ST_LSM6DS3H_SELFTEST_ADDR1_VALUE;
	reg_addr2 = ST_LSM6DS3H_SELFTEST_ADDR2;
	temp_reg_status2 = ST_LSM6DS3H_SELFTEST_ADDR2_VALUE;
	reg_addr3 = ST_LSM6DS3H_SELFTEST_ADDR3;
	temp_reg_status3 = ST_LSM6DS3H_SELFTEST_ADDR3_VALUE;
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_LIS3MDL */

#ifdef ST_LSM6DS3H_EXT0_IS_AKM
	reg_addr = ST_LSM6DS3H_SELFTEST_ADDR;
	temp_reg_status = ST_LSM6DS3H_SELFTEST_ENABLE;
#endif /* ST_LSM6DS3H_EXT0_IS_AKM */

	err = st_lsm6ds3h_i2c_master_read(sdata->cdata, reg_addr, 1,
					&reg_status, false, true, false,
					st_lsm6ds3h_exs_list[0].read_data_len);
	if (err < 0)
		goto disable_sensor_hub;

#ifdef ST_LSM6DS3H_EXT0_IS_AKM
	/* SLAVE 1 is disabled for a while, dummy write to wai reg */
	sh_config[0] = (st_lsm6ds3h_exs_list[EXT0_INDEX].i2c_addr << 1) | 0x01;
	sh_config[1] = st_lsm6ds3h_exs_list[EXT0_INDEX].wai.addr;
	sh_config[2] = 1;

	err = st_lsm6ds3h_write_embedded_registers(sdata->cdata,
					ST_LSM6DS3H_SLV1_ADDR_ADDR,
					sh_config, ARRAY_SIZE(sh_config));
	if (err < 0)
		goto disable_sensor_hub;

	/* SLAVE 2 is disabled for a while, dummy read of wai reg */
	sh_config[0] = (st_lsm6ds3h_exs_list[EXT0_INDEX].i2c_addr << 1) | 0x01;
	sh_config[1] = st_lsm6ds3h_exs_list[EXT0_INDEX].wai.addr;
	sh_config[2] = 1;

	err = st_lsm6ds3h_write_embedded_registers(sdata->cdata,
					ST_LSM6DS3H_SLV2_ADDR_ADDR,
					sh_config, ARRAY_SIZE(sh_config));
	if (err < 0)
		goto disable_sensor_hub;
#endif /* ST_LSM6DS3H_EXT0_IS_AKM */

#ifdef CONFIG_ST_LSM6DS3H_IIO_EXT0_LIS3MDL
	err = st_lsm6ds3h_i2c_master_read(sdata->cdata, reg_addr2, 1,
					&reg_status2, false, true, false,
					st_lsm6ds3h_exs_list[0].read_data_len);
	if (err < 0)
		goto disable_sensor_hub;

	err = st_lsm6ds3h_i2c_master_read(sdata->cdata, reg_addr3, 1,
					&reg_status3, false, true, false,
					st_lsm6ds3h_exs_list[0].read_data_len);
	if (err < 0)
		goto disable_sensor_hub;
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_LIS3MDL */

	err = st_lsm6ds3h_i2c_master_write(sdata->cdata, reg_addr, 1,
					&temp_reg_status, false, true);
	if (err < 0)
		goto disable_sensor_hub;

#ifdef CONFIG_ST_LSM6DS3H_IIO_EXT0_LIS3MDL
	err = st_lsm6ds3h_i2c_master_write(sdata->cdata, reg_addr2, 1,
					&temp_reg_status2, false, true);
	if (err < 0)
		goto restore_status_reg;

	err = st_lsm6ds3h_i2c_master_write(sdata->cdata, reg_addr3, 1,
					&temp_reg_status3, false, true);
	if (err < 0)
		goto restore_status_reg2;

	/* get data with selftest disabled */
	msleep(100);

	for (i = 0; i < 10; i++) {
		err = sdata->cdata->tf->read(sdata->cdata, sdata->data_out_reg,
			st_lsm6ds3h_exs_list[0].read_data_len, outdata, true);
		if (err < 0) {
			i--;
			continue;
		}

		x += ((s16)*(u16 *)&outdata[0]) / 10;
		y += ((s16)*(u16 *)&outdata[2]) / 10;
		z += ((s16)*(u16 *)&outdata[4]) / 10;

		msleep((1000U / sdata->cdata->trigger_odr) + 2);
	}

	temp_reg_status = ST_LSM6DS3H_SELFTEST_ENABLE;

	err = st_lsm6ds3h_i2c_master_write(sdata->cdata, reg_addr, 1,
					&temp_reg_status, false, true);
	if (err < 0)
		goto restore_status_reg3;

	/* get data with selftest disabled */
	msleep(100);

	for (i = 0; i < 10; i++) {
		err = sdata->cdata->tf->read(sdata->cdata, sdata->data_out_reg,
			st_lsm6ds3h_exs_list[0].read_data_len, outdata, true);
		if (err < 0) {
			i--;
			continue;
		}

		x_selftest += ((s16)*(u16 *)&outdata[0]) / 10;
		y_selftest += ((s16)*(u16 *)&outdata[2]) / 10;
		z_selftest += ((s16)*(u16 *)&outdata[4]) / 10;

		msleep((1000U / sdata->cdata->trigger_odr) + 2);
	}

	err = st_lsm6ds3h_i2c_master_write(sdata->cdata, reg_addr3, 1,
						&reg_status3, false, true);
	if (err < 0)
		goto restore_status_reg3;

	err = st_lsm6ds3h_i2c_master_write(sdata->cdata, reg_addr2, 1,
						&reg_status2, false, true);
	if (err < 0)
		goto restore_status_reg2;

	err = st_lsm6ds3h_i2c_master_write(sdata->cdata, reg_addr, 1,
						&reg_status, false, true);
	if (err < 0)
		goto restore_status_reg;

	err = st_lsm6ds3h_enable_sensor_hub(sdata->cdata,
						false, ST_MASK_ID_EXT0);
	if (err < 0)
		goto disable_sensor_hub;

	if ((abs(x_selftest - x) < ST_LSM6DS3H_SELFTEST_EXT0_MIN) ||
			(abs(x_selftest - x) > ST_LSM6DS3H_SELFTEST_EXT0_MAX)) {
		sdata->cdata->ext0_selftest_status = -1;
		mutex_unlock(&sdata->cdata->odr_lock);
		return size;
	}

	if ((abs(y_selftest - y) < ST_LSM6DS3H_SELFTEST_EXT0_MIN) ||
			(abs(y_selftest - y) > ST_LSM6DS3H_SELFTEST_EXT0_MAX)) {
		sdata->cdata->ext0_selftest_status = -1;
		mutex_unlock(&sdata->cdata->odr_lock);
		return size;
	}

	if ((abs(z_selftest - z) < ST_LSM6DS3H_SELFTEST_EXT0_MIN_Z) ||
			(abs(z_selftest - z) > ST_LSM6DS3H_SELFTEST_EXT0_MAX_Z)) {
		sdata->cdata->ext0_selftest_status = -1;
		mutex_unlock(&sdata->cdata->odr_lock);
		return size;
	}
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_LIS3MDL */

#ifdef ST_LSM6DS3H_EXT0_IS_AKM
	do {
		msleep(1000U / sdata->cdata->trigger_odr);

		err = st_lsm6ds3h_i2c_master_read(sdata->cdata,
					ST_LSM6DS3H_SELFTEST_STATUS_REG, 1,
					&temp, false, true, false, 1);
		if (err < 0)
			goto restore_status_reg;

		timeout++;
	} while (((temp & 0x01) == 0) && (timeout < 5));

	if (timeout >= 5) {
		err = -EINVAL;
		goto restore_status_reg;
	}

	err = st_lsm6ds3h_i2c_master_read(sdata->cdata,
			st_lsm6ds3h_exs_list[0].data.channels[0].address,
			st_lsm6ds3h_exs_list[0].read_data_len,
			outdata, false, true, true, 1);
	if (err < 0)
		goto restore_status_reg;

#ifdef ST_LSM6DS3H_EXT0_IS_AKM
	/* SLAVE 2 recovering */
	sh_config[0] = (st_lsm6ds3h_exs_list[EXT0_INDEX].i2c_addr << 1) | 0x01;
	sh_config[1] = st_lsm6ds3h_exs_list[0].data.channels[0].address;
	sh_config[2] = st_lsm6ds3h_exs_list[0].read_data_len;

	err = st_lsm6ds3h_write_embedded_registers(sdata->cdata,
					ST_LSM6DS3H_SLV1_ADDR_ADDR,
					sh_config, ARRAY_SIZE(sh_config));
	if (err < 0)
		goto restore_status_reg;
#endif /* ST_LSM6DS3H_EXT0_IS_AKM */

	err = st_lsm6ds3h_i2c_master_write(sdata->cdata, reg_addr, 1,
						&reg_status, false, true);
	if (err < 0)
		goto restore_status_reg;

	err = st_lsm6ds3h_enable_sensor_hub(sdata->cdata,
						false, ST_MASK_ID_EXT0);
	if (err < 0)
		goto disable_sensor_hub;

	x_selftest = ((s16)*(u16 *)&outdata[0]);
	y_selftest = ((s16)*(u16 *)&outdata[2]);
	z_selftest = ((s16)*(u16 *)&outdata[4]);

#if defined(CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09912) || \
			defined(CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM09911)
	x_selftest *= sdata->c_gain[0];
	y_selftest *= sdata->c_gain[1];
	z_selftest *= sdata->c_gain[2];

	x_selftest /= 10000;
	y_selftest /= 10000;
	z_selftest /= 10000;
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_AKM0991X */

	if ((x_selftest < ST_LSM6DS3H_SELFTEST_EXT0_MIN) ||
			(x_selftest > ST_LSM6DS3H_SELFTEST_EXT0_MAX)) {
		sdata->cdata->ext0_selftest_status = -1;
		mutex_unlock(&sdata->cdata->odr_lock);
		return size;
	}

	if ((y_selftest < ST_LSM6DS3H_SELFTEST_EXT0_MIN) ||
			(y_selftest > ST_LSM6DS3H_SELFTEST_EXT0_MAX)) {
		sdata->cdata->ext0_selftest_status = -1;
		mutex_unlock(&sdata->cdata->odr_lock);
		return size;
	}

	if ((z_selftest < ST_LSM6DS3H_SELFTEST_EXT0_MIN_Z) ||
			(z_selftest > ST_LSM6DS3H_SELFTEST_EXT0_MAX_Z)) {
		sdata->cdata->ext0_selftest_status = -1;
		mutex_unlock(&sdata->cdata->odr_lock);
		return size;
	}
#endif /* ST_LSM6DS3H_EXT0_IS_AKM */

	sdata->cdata->ext0_selftest_status = 1;

	mutex_unlock(&sdata->cdata->odr_lock);

	return size;

#ifdef CONFIG_ST_LSM6DS3H_IIO_EXT0_LIS3MDL
restore_status_reg3:
	st_lsm6ds3h_i2c_master_write(sdata->cdata, reg_addr3, 1,
						&reg_status3, false, true);
restore_status_reg2:
	st_lsm6ds3h_i2c_master_write(sdata->cdata, reg_addr2, 1,
						&reg_status2, false, true);
#endif /* CONFIG_ST_LSM6DS3H_IIO_EXT0_LIS3MDL */
restore_status_reg:
	st_lsm6ds3h_i2c_master_write(sdata->cdata, reg_addr, 1,
						&reg_status, false, true);
disable_sensor_hub:
	st_lsm6ds3h_enable_sensor_hub(sdata->cdata, false, ST_MASK_ID_EXT0);
	mutex_unlock(&sdata->cdata->odr_lock);
	return err;
}
#endif /* ST_LSM6DS3H_EXT0_HAS_SELFTEST	*/


static int st_lsm6ds3h_i2c_master_set_odr(struct lsm6ds3h_sensor_data *sdata,
						unsigned int odr, bool force)
{
	int i, err, err2;
	u8 value, mask, addr;
	bool scan_odr = true;
	unsigned int current_odr = sdata->cdata->v_odr[sdata->sindex];
	unsigned int current_hw_odr = sdata->cdata->hw_odr[sdata->sindex];

	if (odr == 0) {
		if (force)
			scan_odr = false;
		else
			return -EINVAL;
	}
	if (scan_odr) {
		switch (odr) {
		case 13:
		case 26:
		case 52:
		case 104:
			break;
		default:
			return -EINVAL;
		}

		for (i = 0; i < ST_LSM6DS3H_ODR_LIST_NUM; i++) {
			if (st_lsm6ds3h_exs_list[0].odr.odr_avl[i].hz >= odr)
				break;
		}
		if (i == ST_LSM6DS3H_ODR_LIST_NUM)
			i--;

		if (!force) {
			if ((sdata->cdata->sensors_enabled & BIT(sdata->sindex)) == 0) {
				sdata->cdata->v_odr[sdata->sindex] = odr;
				return 0;
			}
		}

		addr = st_lsm6ds3h_exs_list[0].odr.addr;
		mask = st_lsm6ds3h_exs_list[0].odr.mask;
		value = st_lsm6ds3h_exs_list[0].odr.odr_avl[i].value;
	} else {
		if (st_lsm6ds3h_exs_list[0].power.isodr) {
			addr = st_lsm6ds3h_exs_list[0].power.addr;
			mask = st_lsm6ds3h_exs_list[0].power.mask;
			value = st_lsm6ds3h_exs_list[0].power.off_value;
		} else
			goto skip_i2c_write;
	}

	sdata->cdata->samples_to_discard[ST_MASK_ID_EXT0] =
				st_lsm6ds3h_exs_list[0].samples_to_discard;

	err = st_lsm6ds3h_i2c_master_write_data_with_mask(sdata->cdata,
							addr, mask, value);
	if (err < 0)
		return err;

skip_i2c_write:
	if (odr == 0)
		sdata->cdata->hw_odr[sdata->sindex] = 0;
	else
		sdata->cdata->hw_odr[sdata->sindex] = odr;

	if (!force) {
		sdata->cdata->v_odr[sdata->sindex] = odr;

		err = st_lsm6ds3h_enable_sensor_hub(sdata->cdata,
							true, ST_MASK_ID_EXT0);
		if (err < 0) {
			sdata->cdata->hw_odr[sdata->sindex] = current_hw_odr;
			sdata->cdata->v_odr[sdata->sindex] = current_odr;
			do {
				err2 = st_lsm6ds3h_enable_sensor_hub(sdata->cdata,
							false, ST_MASK_ID_EXT0);
				msleep(200);
			} while (err2 < 0);

			return err;
		}
	}

	return 0;
}

static int st_lsm6ds3h_i2c_master_set_enable(
			struct lsm6ds3h_sensor_data *sdata, bool enable, bool buffer)
{
	int err;
	u8 reg_value;

	/* If odr != power this part should enable/disable sensor */
	if (!st_lsm6ds3h_exs_list[0].power.isodr) {
		if (enable)
			reg_value = st_lsm6ds3h_exs_list[0].power.on_value;
		else
			reg_value = st_lsm6ds3h_exs_list[0].power.off_value;

		err = st_lsm6ds3h_i2c_master_write_data_with_mask(sdata->cdata,
					st_lsm6ds3h_exs_list[0].power.addr,
					st_lsm6ds3h_exs_list[0].power.mask,
					reg_value);
		if (err < 0)
			return err;
	}

	err =  st_lsm6ds3h_enable_sensor_hub(sdata->cdata,
						enable, ST_MASK_ID_EXT0);
	if (err < 0)
		return err;

	err = st_lsm6ds3h_i2c_master_set_odr(sdata,
			enable ? sdata->cdata->v_odr[sdata->sindex] : 0, true);
	if (err < 0)
		goto disable_sensorhub;

	if (buffer) {
		err = st_lsm6ds3h_set_drdy_irq(sdata, enable);
		if (err < 0)
			goto restore_odr;

		if (enable)
			sdata->cdata->sensors_enabled |= BIT(sdata->sindex);
		else
			sdata->cdata->sensors_enabled &= ~BIT(sdata->sindex);
	}

	return 0;

restore_odr:
	st_lsm6ds3h_i2c_master_set_odr(sdata,
			enable ? 0 : sdata->cdata->v_odr[sdata->sindex], true);
disable_sensorhub:
	st_lsm6ds3h_enable_sensor_hub(sdata->cdata, !enable, ST_MASK_ID_EXT0);

	return err;
}

static int st_lsm6ds3h_i2c_master_read_raw(struct iio_dev *indio_dev,
					   struct iio_chan_spec const *ch,
					   int *val, int *val2, long mask)
{
	struct lsm6ds3h_sensor_data *sdata = iio_priv(indio_dev);
	int err, ch_num_byte = ch->scan_type.storagebits >> 3;
	u8 outdata[4];

	if (ch_num_byte > ARRAY_SIZE(outdata))
		return -ENOMEM;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);

		if (st_lsm6ds3h_iio_dev_currentmode(indio_dev) == INDIO_BUFFER_TRIGGERED) {
			mutex_unlock(&indio_dev->mlock);
			return -EBUSY;
		}

		mutex_lock(&sdata->cdata->odr_lock);

		err = st_lsm6ds3h_i2c_master_set_enable(sdata, true, false);
		if (err < 0) {
			mutex_unlock(&sdata->cdata->odr_lock);
			mutex_unlock(&indio_dev->mlock);
			return err;
		}

		st_lsm6ds3h_master_wait_completed(sdata->cdata);

		msleep((1000U / sdata->cdata->trigger_odr) + 2);

		err = sdata->cdata->tf->read(sdata->cdata, sdata->data_out_reg,
					     ch_num_byte, outdata, true);
		if (err < 0) {
			st_lsm6ds3h_i2c_master_set_enable(sdata, false, false);
			mutex_unlock(&sdata->cdata->odr_lock);
			mutex_unlock(&indio_dev->mlock);
			return err;
		}

		err = st_lsm6ds3h_i2c_master_set_enable(sdata, false, false);
		if (err < 0) {
			mutex_unlock(&sdata->cdata->odr_lock);
			mutex_unlock(&indio_dev->mlock);
			return err;
		}

		if (ch_num_byte > 2)
			*val = (s32)get_unaligned_le32(outdata);
		else
			*val = (s16)get_unaligned_le16(outdata);

		*val = *val >> ch->scan_type.shift;

		mutex_unlock(&sdata->cdata->odr_lock);
		mutex_unlock(&indio_dev->mlock);

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = sdata->c_gain[ch->scan_index];

		if (ch->type == IIO_TEMP) {
			*val = 1;
			*val2 = 0;
			return IIO_VAL_INT;
		}

		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}

	return 0;
}

static int st_lsm6ds3h_i2c_master_buffer_preenable(struct iio_dev *indio_dev)
{
#ifdef CONFIG_ST_LSM6DS3H_XL_DATA_INJECTION
	struct lsm6ds3h_sensor_data *sdata = iio_priv(indio_dev);

	if (sdata->cdata->injection_mode)
		return -EBUSY;
#endif /* CONFIG_ST_LSM6DS3H_XL_DATA_INJECTION */

	return 0;
}

static int st_lsm6ds3h_i2c_master_buffer_postenable(struct iio_dev *indio_dev)
{
	int err;
	struct lsm6ds3h_sensor_data *sdata = iio_priv(indio_dev);

	sdata->cdata->fifo_output[sdata->sindex].initialized = false;

	if ((sdata->cdata->hwfifo_enabled[ST_MASK_ID_EXT0]) &&
		(indio_dev->buffer->length < 2 * ST_LSM6DS3H_MAX_FIFO_LENGHT))
		return -EINVAL;

	sdata->buffer_data = kmalloc(indio_dev->scan_bytes, GFP_KERNEL);
	if (!sdata->buffer_data)
		return -ENOMEM;

	mutex_lock(&sdata->cdata->odr_lock);

	err = st_lsm6ds3h_i2c_master_set_enable(sdata, true, true);
	if (err < 0) {
		mutex_unlock(&sdata->cdata->odr_lock);
		return err;
	}

	mutex_unlock(&sdata->cdata->odr_lock);

	return 0;
}

static int st_lsm6ds3h_i2c_master_buffer_postdisable(struct iio_dev *indio_dev)
{
	int err;
	struct lsm6ds3h_sensor_data *sdata = iio_priv(indio_dev);

	mutex_lock(&sdata->cdata->odr_lock);

	err = st_lsm6ds3h_i2c_master_set_enable(sdata, false, true);

	mutex_unlock(&sdata->cdata->odr_lock);

	kfree(sdata->buffer_data);

	return err < 0 ? err : 0;
}

static const struct iio_trigger_ops st_lsm6ds3h_i2c_master_trigger_ops = {
	.set_trigger_state = &st_lsm6ds3h_trig_set_state,
};

int st_lsm6ds3h_i2c_master_allocate_trigger(struct lsm6ds3h_data *cdata)
{
	int err;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,13,0)
	cdata->trig[ST_MASK_ID_EXT0] = iio_trigger_alloc(cdata->dev,
				"%s-trigger",
				cdata->indio_dev[ST_MASK_ID_EXT0]->name);
#else /* LINUX_VERSION_CODE */
	cdata->trig[ST_MASK_ID_EXT0] = iio_trigger_alloc("%s-trigger",
				cdata->indio_dev[ST_MASK_ID_EXT0]->name);
#endif /* LINUX_VERSION_CODE */

	if (!cdata->trig[ST_MASK_ID_EXT0]) {
		dev_err(cdata->dev, "failed to allocate iio trigger.\n");
		return -ENOMEM;
	}

	iio_trigger_set_drvdata(cdata->trig[ST_MASK_ID_EXT0],
					cdata->indio_dev[ST_MASK_ID_EXT0]);
	cdata->trig[ST_MASK_ID_EXT0]->ops = &st_lsm6ds3h_i2c_master_trigger_ops;
	cdata->trig[ST_MASK_ID_EXT0]->dev.parent = cdata->dev;

	err = iio_trigger_register(cdata->trig[ST_MASK_ID_EXT0]);
	if (err < 0) {
		dev_err(cdata->dev, "failed to register iio trigger.\n");
		goto deallocate_trigger;
	}

	cdata->indio_dev[ST_MASK_ID_EXT0]->trig = cdata->trig[ST_MASK_ID_EXT0];

	return 0;

deallocate_trigger:
	iio_trigger_free(cdata->trig[ST_MASK_ID_EXT0]);
	return err;
}

static void st_lsm6ds3h_i2c_master_deallocate_trigger(struct lsm6ds3h_data *cdata)
{
	iio_trigger_unregister(cdata->trig[ST_MASK_ID_EXT0]);
}

static const struct iio_buffer_setup_ops st_lsm6ds3h_i2c_master_buffer_setup_ops = {
	.preenable = &st_lsm6ds3h_i2c_master_buffer_preenable,
	.postenable = &st_lsm6ds3h_i2c_master_buffer_postenable,
	.postdisable = &st_lsm6ds3h_i2c_master_buffer_postdisable,
};

static inline irqreturn_t st_lsm6ds3h_i2c_master_handler_empty(int irq, void *p)
{
	return IRQ_HANDLED;
}

static int st_lsm6ds3h_i2c_master_allocate_buffer(struct lsm6ds3h_data *cdata)
{
	return iio_triggered_buffer_setup(cdata->indio_dev[ST_MASK_ID_EXT0],
				&st_lsm6ds3h_i2c_master_handler_empty, NULL,
				&st_lsm6ds3h_i2c_master_buffer_setup_ops);
}

static void st_lsm6ds3h_i2c_master_deallocate_buffer(struct lsm6ds3h_data *cdata)
{
	iio_triggered_buffer_cleanup(cdata->indio_dev[ST_MASK_ID_EXT0]);
}

static int st_lsm6ds3h_i2c_master_send_sensor_hub_parameters(
					struct lsm6ds3h_sensor_data *sdata)
{
	int err;
	u8 sh_config[3];

	/* SLAVE 0 is used by write */
	sh_config[0] = (st_lsm6ds3h_exs_list[EXT0_INDEX].i2c_addr << 1);
	sh_config[1] = st_lsm6ds3h_exs_list[EXT0_INDEX].wai.addr;
	sh_config[2] = 0x01 | 0x20;

	err = st_lsm6ds3h_write_embedded_registers(sdata->cdata,
					ST_LSM6DS3H_SLV0_ADDR_ADDR, sh_config,
					ARRAY_SIZE(sh_config));
	if (err < 0)
		return err;

	/* SLAVE 1 is used to read output data */
	sh_config[0] = (st_lsm6ds3h_exs_list[EXT0_INDEX].i2c_addr << 1) | ST_LSM6DS3H_EN_BIT;
	sh_config[1] = st_lsm6ds3h_exs_list[0].data.channels[0].address;
	sh_config[2] = st_lsm6ds3h_exs_list[0].read_data_len;

	err = st_lsm6ds3h_write_embedded_registers(sdata->cdata,
					ST_LSM6DS3H_SLV1_ADDR_ADDR,
					sh_config, ARRAY_SIZE(sh_config));
	if (err < 0)
		return err;

	return 0;
}

static int st_lsm6ds3h_i2c_master_init_sensor(struct lsm6ds3h_sensor_data *sdata)
{
	int err, ext_num = 0;

	err = st_lsm6ds3h_i2c_master_send_sensor_hub_parameters(sdata);
	if (err < 0)
		return err;

	sdata->c_gain[0] = st_lsm6ds3h_exs_list[ext_num].gain;
	sdata->c_gain[1] = st_lsm6ds3h_exs_list[ext_num].gain;
	sdata->c_gain[2] = st_lsm6ds3h_exs_list[ext_num].gain;

	if ((st_lsm6ds3h_exs_list[ext_num].power.addr ==
				st_lsm6ds3h_exs_list[ext_num].odr.addr) &&
			(st_lsm6ds3h_exs_list[ext_num].power.mask ==
					st_lsm6ds3h_exs_list[ext_num].odr.mask))
		st_lsm6ds3h_exs_list[ext_num].power.isodr = true;
	else
		st_lsm6ds3h_exs_list[ext_num].power.isodr = false;

	err = st_lsm6ds3h_i2c_master_write_data_with_mask(sdata->cdata,
					st_lsm6ds3h_exs_list[ext_num].reset.addr,
					st_lsm6ds3h_exs_list[ext_num].reset.mask,
					ST_LSM6DS3H_EN_BIT);
	if (err < 0)
		return err;

	usleep_range(200, 1000);

	if (st_lsm6ds3h_exs_list[ext_num].fullscale.addr > 0) {
		err = st_lsm6ds3h_i2c_master_write_data_with_mask(sdata->cdata,
			st_lsm6ds3h_exs_list[ext_num].fullscale.addr,
			st_lsm6ds3h_exs_list[ext_num].fullscale.mask,
			st_lsm6ds3h_exs_list[ext_num].fullscale.def_value);
		if (err < 0)
			return err;
	}

	if (st_lsm6ds3h_exs_list[0].cf.boot_initialization != NULL) {
		err = st_lsm6ds3h_exs_list[0].cf.boot_initialization(sdata);
		if (err < 0)
			return err;
	}

	err = st_lsm6ds3h_i2c_master_set_enable(sdata, false, false);
	if (err < 0)
		return err;

	return 0;
}

static int st_lsm6ds3h_i2c_master_allocate_device(struct lsm6ds3h_data *cdata)
{
	int err;
	struct lsm6ds3h_sensor_data *sdata_ext;


	sdata_ext = iio_priv(cdata->indio_dev[ST_MASK_ID_EXT0]);

	sdata_ext->num_data_channels =
				st_lsm6ds3h_exs_list[0].num_data_channels;

	cdata->indio_dev[ST_MASK_ID_EXT0]->name = kasprintf(GFP_KERNEL,
				"%s_%s", cdata->name,
				st_lsm6ds3h_exs_list[0].data.suffix_name);

	cdata->indio_dev[ST_MASK_ID_EXT0]->info =
				st_lsm6ds3h_exs_list[0].data.info;
	cdata->indio_dev[ST_MASK_ID_EXT0]->channels =
				st_lsm6ds3h_exs_list[0].data.channels;
	cdata->indio_dev[ST_MASK_ID_EXT0]->num_channels =
				st_lsm6ds3h_exs_list[0].data.num_channels;

	cdata->indio_dev[ST_MASK_ID_EXT0]->modes = INDIO_DIRECT_MODE;

	sdata_ext->data_out_reg = ST_LSM6DS3H_SLV0_OUT_ADDR;

	err = st_lsm6ds3h_i2c_master_init_sensor(sdata_ext);
	if (err < 0)
		return err;

	err = st_lsm6ds3h_i2c_master_allocate_buffer(cdata);
	if (err < 0)
		return err;

	err = st_lsm6ds3h_i2c_master_allocate_trigger(cdata);
	if (err < 0)
		goto iio_deallocate_buffer;

	err = iio_device_register(cdata->indio_dev[ST_MASK_ID_EXT0]);
	if (err < 0)
		goto iio_deallocate_trigger;

	return 0;

iio_deallocate_trigger:
	st_lsm6ds3h_i2c_master_deallocate_trigger(cdata);
iio_deallocate_buffer:
	st_lsm6ds3h_i2c_master_deallocate_buffer(cdata);

	return err;
}

static void st_lsm6ds3h_i2c_master_deallocate_device(struct lsm6ds3h_data *cdata)
{
	iio_device_unregister(cdata->indio_dev[ST_MASK_ID_EXT0]);
	st_lsm6ds3h_i2c_master_deallocate_trigger(cdata);
	st_lsm6ds3h_i2c_master_deallocate_buffer(cdata);
}

int st_lsm6ds3h_i2c_master_probe(struct lsm6ds3h_data *cdata)
{
	int err, i;
	u8 sh_config[3];
	u8 wai, i2c_address;
	struct lsm6ds3h_sensor_data *sdata_ext;

	mutex_init(&cdata->i2c_transfer_lock);
	cdata->v_odr[ST_MASK_ID_EXT0] = 13;
	cdata->ext0_available = false;
	cdata->ext0_selftest_status = false;

#ifdef CONFIG_ST_LSM6DS3H_ENABLE_INTERNAL_PULLUP
	err = st_lsm6ds3h_write_data_with_mask(cdata,
					ST_LSM6DS3H_INTER_PULLUP_ADDR,
					ST_LSM6DS3H_INTER_PULLUP_MASK,
					ST_LSM6DS3H_EN_BIT, true);
	if (err < 0)
		return err;
#endif /* CONFIG_ST_LSM6DS3H_ENABLE_INTERNAL_PULLUP */

	err = st_lsm6ds3h_write_data_with_mask(cdata,
					ST_LSM6DS3H_FUNC_MAX_RATE_ADDR,
					ST_LSM6DS3H_FUNC_MAX_RATE_MASK, 1, true);
	if (err < 0)
		return err;

	cdata->indio_dev[ST_MASK_ID_EXT0] = devm_iio_device_alloc(cdata->dev,
							sizeof(*sdata_ext));
	if (!cdata->indio_dev[ST_MASK_ID_EXT0])
		return -ENOMEM;

	sdata_ext = iio_priv(cdata->indio_dev[ST_MASK_ID_EXT0]);
	sdata_ext->cdata = cdata;
	sdata_ext->sindex = ST_MASK_ID_EXT0;
	cdata->samples_to_discard_2[ST_MASK_ID_EXT0] = 0;
	sdata_ext->cdata->fifo_output[ST_MASK_ID_EXT0].sip = 0;
	sdata_ext->cdata->fifo_output[ST_MASK_ID_EXT0].timestamp_p = 0;

	for (i = 0; i < 2; i++) {
		if (i == 0)
			i2c_address = ST_LSM6DS3H_EXT0_ADDR;
		else
			i2c_address = ST_LSM6DS3H_EXT0_ADDR2;

		/* to check if sensor is available use SLAVE0 first time */
		sh_config[0] = (i2c_address << 1) | 0x01;
		sh_config[1] = st_lsm6ds3h_exs_list[EXT0_INDEX].wai.addr;
		sh_config[2] = 0x01;

		err = st_lsm6ds3h_write_embedded_registers(cdata,
					ST_LSM6DS3H_SLV0_ADDR_ADDR, sh_config,
					ARRAY_SIZE(sh_config));
		if (err < 0)
			return err;

		err = st_lsm6ds3h_enable_sensor_hub(cdata, true,
					ST_MASK_ID_SENSOR_HUB_ASYNC_OP);
		if (err < 0)
			return err;

		msleep(100);

		st_lsm6ds3h_master_wait_completed(cdata);

		err = cdata->tf->read(cdata, ST_LSM6DS3H_SLV0_OUT_ADDR,
								1, &wai, true);
		if (err < 0) {
			err = st_lsm6ds3h_enable_sensor_hub(cdata, false,
					ST_MASK_ID_SENSOR_HUB_ASYNC_OP);
			if (err < 0)
				return err;

			continue;
		}

		err = st_lsm6ds3h_enable_sensor_hub(cdata, false,
					ST_MASK_ID_SENSOR_HUB_ASYNC_OP);
		if (err < 0)
			return err;

		st_lsm6ds3h_exs_list[EXT0_INDEX].i2c_addr = i2c_address;
		break;
	}
	if (i == 2)
		goto ext0_sensor_not_available;

	/* after wai check SLAVE0 is used for write, SLAVE1 for async read
	   and SLAVE2 to read sensor output data */

	if (wai != st_lsm6ds3h_exs_list[EXT0_INDEX].wai.def_value) {
		dev_err(cdata->dev, "wai value of external sensor 0 mismatch\n");
		return err;
	}

	err = st_lsm6ds3h_i2c_master_allocate_device(cdata);
	if (err < 0)
		return err;

	cdata->ext0_available = true;

	return 0;

ext0_sensor_not_available:
	dev_err(cdata->dev, "external sensor 0 not available\n");

	return err;
}
EXPORT_SYMBOL(st_lsm6ds3h_i2c_master_probe);

int st_lsm6ds3h_i2c_master_exit(struct lsm6ds3h_data *cdata)
{
	if (cdata->ext0_available)
		st_lsm6ds3h_i2c_master_deallocate_device(cdata);

	return 0;
}
EXPORT_SYMBOL(st_lsm6ds3h_i2c_master_exit);
