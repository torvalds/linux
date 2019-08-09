// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADIS16480 and similar IMUs driver
 *
 * Copyright 2012 Analog Devices Inc.
 */

#include <linux/clk.h>
#include <linux/bitfield.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/imu/adis.h>

#include <linux/debugfs.h>

#define ADIS16480_PAGE_SIZE 0x80

#define ADIS16480_REG(page, reg) ((page) * ADIS16480_PAGE_SIZE + (reg))

#define ADIS16480_REG_PAGE_ID 0x00 /* Same address on each page */
#define ADIS16480_REG_SEQ_CNT			ADIS16480_REG(0x00, 0x06)
#define ADIS16480_REG_SYS_E_FLA			ADIS16480_REG(0x00, 0x08)
#define ADIS16480_REG_DIAG_STS			ADIS16480_REG(0x00, 0x0A)
#define ADIS16480_REG_ALM_STS			ADIS16480_REG(0x00, 0x0C)
#define ADIS16480_REG_TEMP_OUT			ADIS16480_REG(0x00, 0x0E)
#define ADIS16480_REG_X_GYRO_OUT		ADIS16480_REG(0x00, 0x10)
#define ADIS16480_REG_Y_GYRO_OUT		ADIS16480_REG(0x00, 0x14)
#define ADIS16480_REG_Z_GYRO_OUT		ADIS16480_REG(0x00, 0x18)
#define ADIS16480_REG_X_ACCEL_OUT		ADIS16480_REG(0x00, 0x1C)
#define ADIS16480_REG_Y_ACCEL_OUT		ADIS16480_REG(0x00, 0x20)
#define ADIS16480_REG_Z_ACCEL_OUT		ADIS16480_REG(0x00, 0x24)
#define ADIS16480_REG_X_MAGN_OUT		ADIS16480_REG(0x00, 0x28)
#define ADIS16480_REG_Y_MAGN_OUT		ADIS16480_REG(0x00, 0x2A)
#define ADIS16480_REG_Z_MAGN_OUT		ADIS16480_REG(0x00, 0x2C)
#define ADIS16480_REG_BAROM_OUT			ADIS16480_REG(0x00, 0x2E)
#define ADIS16480_REG_X_DELTAANG_OUT		ADIS16480_REG(0x00, 0x40)
#define ADIS16480_REG_Y_DELTAANG_OUT		ADIS16480_REG(0x00, 0x44)
#define ADIS16480_REG_Z_DELTAANG_OUT		ADIS16480_REG(0x00, 0x48)
#define ADIS16480_REG_X_DELTAVEL_OUT		ADIS16480_REG(0x00, 0x4C)
#define ADIS16480_REG_Y_DELTAVEL_OUT		ADIS16480_REG(0x00, 0x50)
#define ADIS16480_REG_Z_DELTAVEL_OUT		ADIS16480_REG(0x00, 0x54)
#define ADIS16480_REG_PROD_ID			ADIS16480_REG(0x00, 0x7E)

#define ADIS16480_REG_X_GYRO_SCALE		ADIS16480_REG(0x02, 0x04)
#define ADIS16480_REG_Y_GYRO_SCALE		ADIS16480_REG(0x02, 0x06)
#define ADIS16480_REG_Z_GYRO_SCALE		ADIS16480_REG(0x02, 0x08)
#define ADIS16480_REG_X_ACCEL_SCALE		ADIS16480_REG(0x02, 0x0A)
#define ADIS16480_REG_Y_ACCEL_SCALE		ADIS16480_REG(0x02, 0x0C)
#define ADIS16480_REG_Z_ACCEL_SCALE		ADIS16480_REG(0x02, 0x0E)
#define ADIS16480_REG_X_GYRO_BIAS		ADIS16480_REG(0x02, 0x10)
#define ADIS16480_REG_Y_GYRO_BIAS		ADIS16480_REG(0x02, 0x14)
#define ADIS16480_REG_Z_GYRO_BIAS		ADIS16480_REG(0x02, 0x18)
#define ADIS16480_REG_X_ACCEL_BIAS		ADIS16480_REG(0x02, 0x1C)
#define ADIS16480_REG_Y_ACCEL_BIAS		ADIS16480_REG(0x02, 0x20)
#define ADIS16480_REG_Z_ACCEL_BIAS		ADIS16480_REG(0x02, 0x24)
#define ADIS16480_REG_X_HARD_IRON		ADIS16480_REG(0x02, 0x28)
#define ADIS16480_REG_Y_HARD_IRON		ADIS16480_REG(0x02, 0x2A)
#define ADIS16480_REG_Z_HARD_IRON		ADIS16480_REG(0x02, 0x2C)
#define ADIS16480_REG_BAROM_BIAS		ADIS16480_REG(0x02, 0x40)
#define ADIS16480_REG_FLASH_CNT			ADIS16480_REG(0x02, 0x7C)

#define ADIS16480_REG_GLOB_CMD			ADIS16480_REG(0x03, 0x02)
#define ADIS16480_REG_FNCTIO_CTRL		ADIS16480_REG(0x03, 0x06)
#define ADIS16480_REG_GPIO_CTRL			ADIS16480_REG(0x03, 0x08)
#define ADIS16480_REG_CONFIG			ADIS16480_REG(0x03, 0x0A)
#define ADIS16480_REG_DEC_RATE			ADIS16480_REG(0x03, 0x0C)
#define ADIS16480_REG_SLP_CNT			ADIS16480_REG(0x03, 0x10)
#define ADIS16480_REG_FILTER_BNK0		ADIS16480_REG(0x03, 0x16)
#define ADIS16480_REG_FILTER_BNK1		ADIS16480_REG(0x03, 0x18)
#define ADIS16480_REG_ALM_CNFG0			ADIS16480_REG(0x03, 0x20)
#define ADIS16480_REG_ALM_CNFG1			ADIS16480_REG(0x03, 0x22)
#define ADIS16480_REG_ALM_CNFG2			ADIS16480_REG(0x03, 0x24)
#define ADIS16480_REG_XG_ALM_MAGN		ADIS16480_REG(0x03, 0x28)
#define ADIS16480_REG_YG_ALM_MAGN		ADIS16480_REG(0x03, 0x2A)
#define ADIS16480_REG_ZG_ALM_MAGN		ADIS16480_REG(0x03, 0x2C)
#define ADIS16480_REG_XA_ALM_MAGN		ADIS16480_REG(0x03, 0x2E)
#define ADIS16480_REG_YA_ALM_MAGN		ADIS16480_REG(0x03, 0x30)
#define ADIS16480_REG_ZA_ALM_MAGN		ADIS16480_REG(0x03, 0x32)
#define ADIS16480_REG_XM_ALM_MAGN		ADIS16480_REG(0x03, 0x34)
#define ADIS16480_REG_YM_ALM_MAGN		ADIS16480_REG(0x03, 0x36)
#define ADIS16480_REG_ZM_ALM_MAGN		ADIS16480_REG(0x03, 0x38)
#define ADIS16480_REG_BR_ALM_MAGN		ADIS16480_REG(0x03, 0x3A)
#define ADIS16480_REG_FIRM_REV			ADIS16480_REG(0x03, 0x78)
#define ADIS16480_REG_FIRM_DM			ADIS16480_REG(0x03, 0x7A)
#define ADIS16480_REG_FIRM_Y			ADIS16480_REG(0x03, 0x7C)

/*
 * External clock scaling in PPS mode.
 * Available only for ADIS1649x devices
 */
#define ADIS16495_REG_SYNC_SCALE		ADIS16480_REG(0x03, 0x10)

#define ADIS16480_REG_SERIAL_NUM		ADIS16480_REG(0x04, 0x20)

/* Each filter coefficent bank spans two pages */
#define ADIS16480_FIR_COEF(page) (x < 60 ? ADIS16480_REG(page, (x) + 8) : \
		ADIS16480_REG((page) + 1, (x) - 60 + 8))
#define ADIS16480_FIR_COEF_A(x)			ADIS16480_FIR_COEF(0x05, (x))
#define ADIS16480_FIR_COEF_B(x)			ADIS16480_FIR_COEF(0x07, (x))
#define ADIS16480_FIR_COEF_C(x)			ADIS16480_FIR_COEF(0x09, (x))
#define ADIS16480_FIR_COEF_D(x)			ADIS16480_FIR_COEF(0x0B, (x))

/* ADIS16480_REG_FNCTIO_CTRL */
#define ADIS16480_DRDY_SEL_MSK		GENMASK(1, 0)
#define ADIS16480_DRDY_SEL(x)		FIELD_PREP(ADIS16480_DRDY_SEL_MSK, x)
#define ADIS16480_DRDY_POL_MSK		BIT(2)
#define ADIS16480_DRDY_POL(x)		FIELD_PREP(ADIS16480_DRDY_POL_MSK, x)
#define ADIS16480_DRDY_EN_MSK		BIT(3)
#define ADIS16480_DRDY_EN(x)		FIELD_PREP(ADIS16480_DRDY_EN_MSK, x)
#define ADIS16480_SYNC_SEL_MSK		GENMASK(5, 4)
#define ADIS16480_SYNC_SEL(x)		FIELD_PREP(ADIS16480_SYNC_SEL_MSK, x)
#define ADIS16480_SYNC_EN_MSK		BIT(7)
#define ADIS16480_SYNC_EN(x)		FIELD_PREP(ADIS16480_SYNC_EN_MSK, x)
#define ADIS16480_SYNC_MODE_MSK		BIT(8)
#define ADIS16480_SYNC_MODE(x)		FIELD_PREP(ADIS16480_SYNC_MODE_MSK, x)

struct adis16480_chip_info {
	unsigned int num_channels;
	const struct iio_chan_spec *channels;
	unsigned int gyro_max_val;
	unsigned int gyro_max_scale;
	unsigned int accel_max_val;
	unsigned int accel_max_scale;
	unsigned int temp_scale;
	unsigned int int_clk;
	unsigned int max_dec_rate;
	const unsigned int *filter_freqs;
	bool has_pps_clk_mode;
};

enum adis16480_int_pin {
	ADIS16480_PIN_DIO1,
	ADIS16480_PIN_DIO2,
	ADIS16480_PIN_DIO3,
	ADIS16480_PIN_DIO4
};

enum adis16480_clock_mode {
	ADIS16480_CLK_SYNC,
	ADIS16480_CLK_PPS,
	ADIS16480_CLK_INT
};

struct adis16480 {
	const struct adis16480_chip_info *chip_info;

	struct adis adis;
	struct clk *ext_clk;
	enum adis16480_clock_mode clk_mode;
	unsigned int clk_freq;
};

static const char * const adis16480_int_pin_names[4] = {
	[ADIS16480_PIN_DIO1] = "DIO1",
	[ADIS16480_PIN_DIO2] = "DIO2",
	[ADIS16480_PIN_DIO3] = "DIO3",
	[ADIS16480_PIN_DIO4] = "DIO4",
};

#ifdef CONFIG_DEBUG_FS

static ssize_t adis16480_show_firmware_revision(struct file *file,
		char __user *userbuf, size_t count, loff_t *ppos)
{
	struct adis16480 *adis16480 = file->private_data;
	char buf[7];
	size_t len;
	u16 rev;
	int ret;

	ret = adis_read_reg_16(&adis16480->adis, ADIS16480_REG_FIRM_REV, &rev);
	if (ret < 0)
		return ret;

	len = scnprintf(buf, sizeof(buf), "%x.%x\n", rev >> 8, rev & 0xff);

	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static const struct file_operations adis16480_firmware_revision_fops = {
	.open = simple_open,
	.read = adis16480_show_firmware_revision,
	.llseek = default_llseek,
	.owner = THIS_MODULE,
};

static ssize_t adis16480_show_firmware_date(struct file *file,
		char __user *userbuf, size_t count, loff_t *ppos)
{
	struct adis16480 *adis16480 = file->private_data;
	u16 md, year;
	char buf[12];
	size_t len;
	int ret;

	ret = adis_read_reg_16(&adis16480->adis, ADIS16480_REG_FIRM_Y, &year);
	if (ret < 0)
		return ret;

	ret = adis_read_reg_16(&adis16480->adis, ADIS16480_REG_FIRM_DM, &md);
	if (ret < 0)
		return ret;

	len = snprintf(buf, sizeof(buf), "%.2x-%.2x-%.4x\n",
			md >> 8, md & 0xff, year);

	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static const struct file_operations adis16480_firmware_date_fops = {
	.open = simple_open,
	.read = adis16480_show_firmware_date,
	.llseek = default_llseek,
	.owner = THIS_MODULE,
};

static int adis16480_show_serial_number(void *arg, u64 *val)
{
	struct adis16480 *adis16480 = arg;
	u16 serial;
	int ret;

	ret = adis_read_reg_16(&adis16480->adis, ADIS16480_REG_SERIAL_NUM,
		&serial);
	if (ret < 0)
		return ret;

	*val = serial;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(adis16480_serial_number_fops,
	adis16480_show_serial_number, NULL, "0x%.4llx\n");

static int adis16480_show_product_id(void *arg, u64 *val)
{
	struct adis16480 *adis16480 = arg;
	u16 prod_id;
	int ret;

	ret = adis_read_reg_16(&adis16480->adis, ADIS16480_REG_PROD_ID,
		&prod_id);
	if (ret < 0)
		return ret;

	*val = prod_id;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(adis16480_product_id_fops,
	adis16480_show_product_id, NULL, "%llu\n");

static int adis16480_show_flash_count(void *arg, u64 *val)
{
	struct adis16480 *adis16480 = arg;
	u32 flash_count;
	int ret;

	ret = adis_read_reg_32(&adis16480->adis, ADIS16480_REG_FLASH_CNT,
		&flash_count);
	if (ret < 0)
		return ret;

	*val = flash_count;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(adis16480_flash_count_fops,
	adis16480_show_flash_count, NULL, "%lld\n");

static int adis16480_debugfs_init(struct iio_dev *indio_dev)
{
	struct adis16480 *adis16480 = iio_priv(indio_dev);

	debugfs_create_file_unsafe("firmware_revision", 0400,
		indio_dev->debugfs_dentry, adis16480,
		&adis16480_firmware_revision_fops);
	debugfs_create_file_unsafe("firmware_date", 0400,
		indio_dev->debugfs_dentry, adis16480,
		&adis16480_firmware_date_fops);
	debugfs_create_file_unsafe("serial_number", 0400,
		indio_dev->debugfs_dentry, adis16480,
		&adis16480_serial_number_fops);
	debugfs_create_file_unsafe("product_id", 0400,
		indio_dev->debugfs_dentry, adis16480,
		&adis16480_product_id_fops);
	debugfs_create_file_unsafe("flash_count", 0400,
		indio_dev->debugfs_dentry, adis16480,
		&adis16480_flash_count_fops);

	return 0;
}

#else

static int adis16480_debugfs_init(struct iio_dev *indio_dev)
{
	return 0;
}

#endif

static int adis16480_set_freq(struct iio_dev *indio_dev, int val, int val2)
{
	struct adis16480 *st = iio_priv(indio_dev);
	unsigned int t, reg;

	t =  val * 1000 + val2 / 1000;
	if (t <= 0)
		return -EINVAL;

	/*
	 * When using PPS mode, the rate of data collection is equal to the
	 * product of the external clock frequency and the scale factor in the
	 * SYNC_SCALE register.
	 * When using sync mode, or internal clock, the output data rate is
	 * equal with  the clock frequency divided by DEC_RATE + 1.
	 */
	if (st->clk_mode == ADIS16480_CLK_PPS) {
		t = t / st->clk_freq;
		reg = ADIS16495_REG_SYNC_SCALE;
	} else {
		t = st->clk_freq / t;
		reg = ADIS16480_REG_DEC_RATE;
	}

	if (t > st->chip_info->max_dec_rate)
		t = st->chip_info->max_dec_rate;

	if ((t != 0) && (st->clk_mode != ADIS16480_CLK_PPS))
		t--;

	return adis_write_reg_16(&st->adis, reg, t);
}

static int adis16480_get_freq(struct iio_dev *indio_dev, int *val, int *val2)
{
	struct adis16480 *st = iio_priv(indio_dev);
	uint16_t t;
	int ret;
	unsigned freq;
	unsigned int reg;

	if (st->clk_mode == ADIS16480_CLK_PPS)
		reg = ADIS16495_REG_SYNC_SCALE;
	else
		reg = ADIS16480_REG_DEC_RATE;

	ret = adis_read_reg_16(&st->adis, reg, &t);
	if (ret < 0)
		return ret;

	/*
	 * When using PPS mode, the rate of data collection is equal to the
	 * product of the external clock frequency and the scale factor in the
	 * SYNC_SCALE register.
	 * When using sync mode, or internal clock, the output data rate is
	 * equal with  the clock frequency divided by DEC_RATE + 1.
	 */
	if (st->clk_mode == ADIS16480_CLK_PPS)
		freq = st->clk_freq * t;
	else
		freq = st->clk_freq / (t + 1);

	*val = freq / 1000;
	*val2 = (freq % 1000) * 1000;

	return IIO_VAL_INT_PLUS_MICRO;
}

enum {
	ADIS16480_SCAN_GYRO_X,
	ADIS16480_SCAN_GYRO_Y,
	ADIS16480_SCAN_GYRO_Z,
	ADIS16480_SCAN_ACCEL_X,
	ADIS16480_SCAN_ACCEL_Y,
	ADIS16480_SCAN_ACCEL_Z,
	ADIS16480_SCAN_MAGN_X,
	ADIS16480_SCAN_MAGN_Y,
	ADIS16480_SCAN_MAGN_Z,
	ADIS16480_SCAN_BARO,
	ADIS16480_SCAN_TEMP,
};

static const unsigned int adis16480_calibbias_regs[] = {
	[ADIS16480_SCAN_GYRO_X] = ADIS16480_REG_X_GYRO_BIAS,
	[ADIS16480_SCAN_GYRO_Y] = ADIS16480_REG_Y_GYRO_BIAS,
	[ADIS16480_SCAN_GYRO_Z] = ADIS16480_REG_Z_GYRO_BIAS,
	[ADIS16480_SCAN_ACCEL_X] = ADIS16480_REG_X_ACCEL_BIAS,
	[ADIS16480_SCAN_ACCEL_Y] = ADIS16480_REG_Y_ACCEL_BIAS,
	[ADIS16480_SCAN_ACCEL_Z] = ADIS16480_REG_Z_ACCEL_BIAS,
	[ADIS16480_SCAN_MAGN_X] = ADIS16480_REG_X_HARD_IRON,
	[ADIS16480_SCAN_MAGN_Y] = ADIS16480_REG_Y_HARD_IRON,
	[ADIS16480_SCAN_MAGN_Z] = ADIS16480_REG_Z_HARD_IRON,
	[ADIS16480_SCAN_BARO] = ADIS16480_REG_BAROM_BIAS,
};

static const unsigned int adis16480_calibscale_regs[] = {
	[ADIS16480_SCAN_GYRO_X] = ADIS16480_REG_X_GYRO_SCALE,
	[ADIS16480_SCAN_GYRO_Y] = ADIS16480_REG_Y_GYRO_SCALE,
	[ADIS16480_SCAN_GYRO_Z] = ADIS16480_REG_Z_GYRO_SCALE,
	[ADIS16480_SCAN_ACCEL_X] = ADIS16480_REG_X_ACCEL_SCALE,
	[ADIS16480_SCAN_ACCEL_Y] = ADIS16480_REG_Y_ACCEL_SCALE,
	[ADIS16480_SCAN_ACCEL_Z] = ADIS16480_REG_Z_ACCEL_SCALE,
};

static int adis16480_set_calibbias(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, int bias)
{
	unsigned int reg = adis16480_calibbias_regs[chan->scan_index];
	struct adis16480 *st = iio_priv(indio_dev);

	switch (chan->type) {
	case IIO_MAGN:
	case IIO_PRESSURE:
		if (bias < -0x8000 || bias >= 0x8000)
			return -EINVAL;
		return adis_write_reg_16(&st->adis, reg, bias);
	case IIO_ANGL_VEL:
	case IIO_ACCEL:
		return adis_write_reg_32(&st->adis, reg, bias);
	default:
		break;
	}

	return -EINVAL;
}

static int adis16480_get_calibbias(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, int *bias)
{
	unsigned int reg = adis16480_calibbias_regs[chan->scan_index];
	struct adis16480 *st = iio_priv(indio_dev);
	uint16_t val16;
	uint32_t val32;
	int ret;

	switch (chan->type) {
	case IIO_MAGN:
	case IIO_PRESSURE:
		ret = adis_read_reg_16(&st->adis, reg, &val16);
		*bias = sign_extend32(val16, 15);
		break;
	case IIO_ANGL_VEL:
	case IIO_ACCEL:
		ret = adis_read_reg_32(&st->adis, reg, &val32);
		*bias = sign_extend32(val32, 31);
		break;
	default:
			ret = -EINVAL;
	}

	if (ret < 0)
		return ret;

	return IIO_VAL_INT;
}

static int adis16480_set_calibscale(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, int scale)
{
	unsigned int reg = adis16480_calibscale_regs[chan->scan_index];
	struct adis16480 *st = iio_priv(indio_dev);

	if (scale < -0x8000 || scale >= 0x8000)
		return -EINVAL;

	return adis_write_reg_16(&st->adis, reg, scale);
}

static int adis16480_get_calibscale(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, int *scale)
{
	unsigned int reg = adis16480_calibscale_regs[chan->scan_index];
	struct adis16480 *st = iio_priv(indio_dev);
	uint16_t val16;
	int ret;

	ret = adis_read_reg_16(&st->adis, reg, &val16);
	if (ret < 0)
		return ret;

	*scale = sign_extend32(val16, 15);
	return IIO_VAL_INT;
}

static const unsigned int adis16480_def_filter_freqs[] = {
	310,
	55,
	275,
	63,
};

static const unsigned int adis16495_def_filter_freqs[] = {
	300,
	100,
	300,
	100,
};

static const unsigned int ad16480_filter_data[][2] = {
	[ADIS16480_SCAN_GYRO_X]		= { ADIS16480_REG_FILTER_BNK0, 0 },
	[ADIS16480_SCAN_GYRO_Y]		= { ADIS16480_REG_FILTER_BNK0, 3 },
	[ADIS16480_SCAN_GYRO_Z]		= { ADIS16480_REG_FILTER_BNK0, 6 },
	[ADIS16480_SCAN_ACCEL_X]	= { ADIS16480_REG_FILTER_BNK0, 9 },
	[ADIS16480_SCAN_ACCEL_Y]	= { ADIS16480_REG_FILTER_BNK0, 12 },
	[ADIS16480_SCAN_ACCEL_Z]	= { ADIS16480_REG_FILTER_BNK1, 0 },
	[ADIS16480_SCAN_MAGN_X]		= { ADIS16480_REG_FILTER_BNK1, 3 },
	[ADIS16480_SCAN_MAGN_Y]		= { ADIS16480_REG_FILTER_BNK1, 6 },
	[ADIS16480_SCAN_MAGN_Z]		= { ADIS16480_REG_FILTER_BNK1, 9 },
};

static int adis16480_get_filter_freq(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, int *freq)
{
	struct adis16480 *st = iio_priv(indio_dev);
	unsigned int enable_mask, offset, reg;
	uint16_t val;
	int ret;

	reg = ad16480_filter_data[chan->scan_index][0];
	offset = ad16480_filter_data[chan->scan_index][1];
	enable_mask = BIT(offset + 2);

	ret = adis_read_reg_16(&st->adis, reg, &val);
	if (ret < 0)
		return ret;

	if (!(val & enable_mask))
		*freq = 0;
	else
		*freq = st->chip_info->filter_freqs[(val >> offset) & 0x3];

	return IIO_VAL_INT;
}

static int adis16480_set_filter_freq(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, unsigned int freq)
{
	struct adis16480 *st = iio_priv(indio_dev);
	unsigned int enable_mask, offset, reg;
	unsigned int diff, best_diff;
	unsigned int i, best_freq;
	uint16_t val;
	int ret;

	reg = ad16480_filter_data[chan->scan_index][0];
	offset = ad16480_filter_data[chan->scan_index][1];
	enable_mask = BIT(offset + 2);

	ret = adis_read_reg_16(&st->adis, reg, &val);
	if (ret < 0)
		return ret;

	if (freq == 0) {
		val &= ~enable_mask;
	} else {
		best_freq = 0;
		best_diff = st->chip_info->filter_freqs[0];
		for (i = 0; i < ARRAY_SIZE(adis16480_def_filter_freqs); i++) {
			if (st->chip_info->filter_freqs[i] >= freq) {
				diff = st->chip_info->filter_freqs[i] - freq;
				if (diff < best_diff) {
					best_diff = diff;
					best_freq = i;
				}
			}
		}

		val &= ~(0x3 << offset);
		val |= best_freq << offset;
		val |= enable_mask;
	}

	return adis_write_reg_16(&st->adis, reg, val);
}

static int adis16480_read_raw(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, int *val, int *val2, long info)
{
	struct adis16480 *st = iio_priv(indio_dev);
	unsigned int temp;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		return adis_single_conversion(indio_dev, chan, 0, val);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			*val = st->chip_info->gyro_max_scale;
			*val2 = st->chip_info->gyro_max_val;
			return IIO_VAL_FRACTIONAL;
		case IIO_ACCEL:
			*val = st->chip_info->accel_max_scale;
			*val2 = st->chip_info->accel_max_val;
			return IIO_VAL_FRACTIONAL;
		case IIO_MAGN:
			*val = 0;
			*val2 = 100; /* 0.0001 gauss */
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			/*
			 * +85 degrees Celsius = temp_max_scale
			 * +25 degrees Celsius = 0
			 * LSB, 25 degrees Celsius  = 60 / temp_max_scale
			 */
			*val = st->chip_info->temp_scale / 1000;
			*val2 = (st->chip_info->temp_scale % 1000) * 1000;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_PRESSURE:
			*val = 0;
			*val2 = 4000; /* 40ubar = 0.004 kPa */
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		/* Only the temperature channel has a offset */
		temp = 25 * 1000000LL; /* 25 degree Celsius = 0x0000 */
		*val = DIV_ROUND_CLOSEST_ULL(temp, st->chip_info->temp_scale);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBBIAS:
		return adis16480_get_calibbias(indio_dev, chan, val);
	case IIO_CHAN_INFO_CALIBSCALE:
		return adis16480_get_calibscale(indio_dev, chan, val);
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		return adis16480_get_filter_freq(indio_dev, chan, val);
	case IIO_CHAN_INFO_SAMP_FREQ:
		return adis16480_get_freq(indio_dev, val, val2);
	default:
		return -EINVAL;
	}
}

static int adis16480_write_raw(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, int val, int val2, long info)
{
	switch (info) {
	case IIO_CHAN_INFO_CALIBBIAS:
		return adis16480_set_calibbias(indio_dev, chan, val);
	case IIO_CHAN_INFO_CALIBSCALE:
		return adis16480_set_calibscale(indio_dev, chan, val);
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		return adis16480_set_filter_freq(indio_dev, chan, val);
	case IIO_CHAN_INFO_SAMP_FREQ:
		return adis16480_set_freq(indio_dev, val, val2);

	default:
		return -EINVAL;
	}
}

#define ADIS16480_MOD_CHANNEL(_type, _mod, _address, _si, _info_sep, _bits) \
	{ \
		.type = (_type), \
		.modified = 1, \
		.channel2 = (_mod), \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			BIT(IIO_CHAN_INFO_CALIBBIAS) | \
			_info_sep, \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
		.address = (_address), \
		.scan_index = (_si), \
		.scan_type = { \
			.sign = 's', \
			.realbits = (_bits), \
			.storagebits = (_bits), \
			.endianness = IIO_BE, \
		}, \
	}

#define ADIS16480_GYRO_CHANNEL(_mod) \
	ADIS16480_MOD_CHANNEL(IIO_ANGL_VEL, IIO_MOD_ ## _mod, \
	ADIS16480_REG_ ## _mod ## _GYRO_OUT, ADIS16480_SCAN_GYRO_ ## _mod, \
	BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY) | \
	BIT(IIO_CHAN_INFO_CALIBSCALE), \
	32)

#define ADIS16480_ACCEL_CHANNEL(_mod) \
	ADIS16480_MOD_CHANNEL(IIO_ACCEL, IIO_MOD_ ## _mod, \
	ADIS16480_REG_ ## _mod ## _ACCEL_OUT, ADIS16480_SCAN_ACCEL_ ## _mod, \
	BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY) | \
	BIT(IIO_CHAN_INFO_CALIBSCALE), \
	32)

#define ADIS16480_MAGN_CHANNEL(_mod) \
	ADIS16480_MOD_CHANNEL(IIO_MAGN, IIO_MOD_ ## _mod, \
	ADIS16480_REG_ ## _mod ## _MAGN_OUT, ADIS16480_SCAN_MAGN_ ## _mod, \
	BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY), \
	16)

#define ADIS16480_PRESSURE_CHANNEL() \
	{ \
		.type = IIO_PRESSURE, \
		.indexed = 1, \
		.channel = 0, \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			BIT(IIO_CHAN_INFO_CALIBBIAS) | \
			BIT(IIO_CHAN_INFO_SCALE), \
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
		.address = ADIS16480_REG_BAROM_OUT, \
		.scan_index = ADIS16480_SCAN_BARO, \
		.scan_type = { \
			.sign = 's', \
			.realbits = 32, \
			.storagebits = 32, \
			.endianness = IIO_BE, \
		}, \
	}

#define ADIS16480_TEMP_CHANNEL() { \
		.type = IIO_TEMP, \
		.indexed = 1, \
		.channel = 0, \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			BIT(IIO_CHAN_INFO_SCALE) | \
			BIT(IIO_CHAN_INFO_OFFSET), \
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
		.address = ADIS16480_REG_TEMP_OUT, \
		.scan_index = ADIS16480_SCAN_TEMP, \
		.scan_type = { \
			.sign = 's', \
			.realbits = 16, \
			.storagebits = 16, \
			.endianness = IIO_BE, \
		}, \
	}

static const struct iio_chan_spec adis16480_channels[] = {
	ADIS16480_GYRO_CHANNEL(X),
	ADIS16480_GYRO_CHANNEL(Y),
	ADIS16480_GYRO_CHANNEL(Z),
	ADIS16480_ACCEL_CHANNEL(X),
	ADIS16480_ACCEL_CHANNEL(Y),
	ADIS16480_ACCEL_CHANNEL(Z),
	ADIS16480_MAGN_CHANNEL(X),
	ADIS16480_MAGN_CHANNEL(Y),
	ADIS16480_MAGN_CHANNEL(Z),
	ADIS16480_PRESSURE_CHANNEL(),
	ADIS16480_TEMP_CHANNEL(),
	IIO_CHAN_SOFT_TIMESTAMP(11)
};

static const struct iio_chan_spec adis16485_channels[] = {
	ADIS16480_GYRO_CHANNEL(X),
	ADIS16480_GYRO_CHANNEL(Y),
	ADIS16480_GYRO_CHANNEL(Z),
	ADIS16480_ACCEL_CHANNEL(X),
	ADIS16480_ACCEL_CHANNEL(Y),
	ADIS16480_ACCEL_CHANNEL(Z),
	ADIS16480_TEMP_CHANNEL(),
	IIO_CHAN_SOFT_TIMESTAMP(7)
};

enum adis16480_variant {
	ADIS16375,
	ADIS16480,
	ADIS16485,
	ADIS16488,
	ADIS16495_1,
	ADIS16495_2,
	ADIS16495_3,
	ADIS16497_1,
	ADIS16497_2,
	ADIS16497_3,
};

static const struct adis16480_chip_info adis16480_chip_info[] = {
	[ADIS16375] = {
		.channels = adis16485_channels,
		.num_channels = ARRAY_SIZE(adis16485_channels),
		/*
		 * storing the value in rad/degree and the scale in degree
		 * gives us the result in rad and better precession than
		 * storing the scale directly in rad.
		 */
		.gyro_max_val = IIO_RAD_TO_DEGREE(22887),
		.gyro_max_scale = 300,
		.accel_max_val = IIO_M_S_2_TO_G(21973),
		.accel_max_scale = 18,
		.temp_scale = 5650, /* 5.65 milli degree Celsius */
		.int_clk = 2460000,
		.max_dec_rate = 2048,
		.filter_freqs = adis16480_def_filter_freqs,
	},
	[ADIS16480] = {
		.channels = adis16480_channels,
		.num_channels = ARRAY_SIZE(adis16480_channels),
		.gyro_max_val = IIO_RAD_TO_DEGREE(22500),
		.gyro_max_scale = 450,
		.accel_max_val = IIO_M_S_2_TO_G(12500),
		.accel_max_scale = 10,
		.temp_scale = 5650, /* 5.65 milli degree Celsius */
		.int_clk = 2460000,
		.max_dec_rate = 2048,
		.filter_freqs = adis16480_def_filter_freqs,
	},
	[ADIS16485] = {
		.channels = adis16485_channels,
		.num_channels = ARRAY_SIZE(adis16485_channels),
		.gyro_max_val = IIO_RAD_TO_DEGREE(22500),
		.gyro_max_scale = 450,
		.accel_max_val = IIO_M_S_2_TO_G(20000),
		.accel_max_scale = 5,
		.temp_scale = 5650, /* 5.65 milli degree Celsius */
		.int_clk = 2460000,
		.max_dec_rate = 2048,
		.filter_freqs = adis16480_def_filter_freqs,
	},
	[ADIS16488] = {
		.channels = adis16480_channels,
		.num_channels = ARRAY_SIZE(adis16480_channels),
		.gyro_max_val = IIO_RAD_TO_DEGREE(22500),
		.gyro_max_scale = 450,
		.accel_max_val = IIO_M_S_2_TO_G(22500),
		.accel_max_scale = 18,
		.temp_scale = 5650, /* 5.65 milli degree Celsius */
		.int_clk = 2460000,
		.max_dec_rate = 2048,
		.filter_freqs = adis16480_def_filter_freqs,
	},
	[ADIS16495_1] = {
		.channels = adis16485_channels,
		.num_channels = ARRAY_SIZE(adis16485_channels),
		.gyro_max_val = IIO_RAD_TO_DEGREE(20000),
		.gyro_max_scale = 125,
		.accel_max_val = IIO_M_S_2_TO_G(32000),
		.accel_max_scale = 8,
		.temp_scale = 12500, /* 12.5 milli degree Celsius */
		.int_clk = 4250000,
		.max_dec_rate = 4250,
		.filter_freqs = adis16495_def_filter_freqs,
		.has_pps_clk_mode = true,
	},
	[ADIS16495_2] = {
		.channels = adis16485_channels,
		.num_channels = ARRAY_SIZE(adis16485_channels),
		.gyro_max_val = IIO_RAD_TO_DEGREE(18000),
		.gyro_max_scale = 450,
		.accel_max_val = IIO_M_S_2_TO_G(32000),
		.accel_max_scale = 8,
		.temp_scale = 12500, /* 12.5 milli degree Celsius */
		.int_clk = 4250000,
		.max_dec_rate = 4250,
		.filter_freqs = adis16495_def_filter_freqs,
		.has_pps_clk_mode = true,
	},
	[ADIS16495_3] = {
		.channels = adis16485_channels,
		.num_channels = ARRAY_SIZE(adis16485_channels),
		.gyro_max_val = IIO_RAD_TO_DEGREE(20000),
		.gyro_max_scale = 2000,
		.accel_max_val = IIO_M_S_2_TO_G(32000),
		.accel_max_scale = 8,
		.temp_scale = 12500, /* 12.5 milli degree Celsius */
		.int_clk = 4250000,
		.max_dec_rate = 4250,
		.filter_freqs = adis16495_def_filter_freqs,
		.has_pps_clk_mode = true,
	},
	[ADIS16497_1] = {
		.channels = adis16485_channels,
		.num_channels = ARRAY_SIZE(adis16485_channels),
		.gyro_max_val = IIO_RAD_TO_DEGREE(20000),
		.gyro_max_scale = 125,
		.accel_max_val = IIO_M_S_2_TO_G(32000),
		.accel_max_scale = 40,
		.temp_scale = 12500, /* 12.5 milli degree Celsius */
		.int_clk = 4250000,
		.max_dec_rate = 4250,
		.filter_freqs = adis16495_def_filter_freqs,
		.has_pps_clk_mode = true,
	},
	[ADIS16497_2] = {
		.channels = adis16485_channels,
		.num_channels = ARRAY_SIZE(adis16485_channels),
		.gyro_max_val = IIO_RAD_TO_DEGREE(18000),
		.gyro_max_scale = 450,
		.accel_max_val = IIO_M_S_2_TO_G(32000),
		.accel_max_scale = 40,
		.temp_scale = 12500, /* 12.5 milli degree Celsius */
		.int_clk = 4250000,
		.max_dec_rate = 4250,
		.filter_freqs = adis16495_def_filter_freqs,
		.has_pps_clk_mode = true,
	},
	[ADIS16497_3] = {
		.channels = adis16485_channels,
		.num_channels = ARRAY_SIZE(adis16485_channels),
		.gyro_max_val = IIO_RAD_TO_DEGREE(20000),
		.gyro_max_scale = 2000,
		.accel_max_val = IIO_M_S_2_TO_G(32000),
		.accel_max_scale = 40,
		.temp_scale = 12500, /* 12.5 milli degree Celsius */
		.int_clk = 4250000,
		.max_dec_rate = 4250,
		.filter_freqs = adis16495_def_filter_freqs,
		.has_pps_clk_mode = true,
	},
};

static const struct iio_info adis16480_info = {
	.read_raw = &adis16480_read_raw,
	.write_raw = &adis16480_write_raw,
	.update_scan_mode = adis_update_scan_mode,
};

static int adis16480_stop_device(struct iio_dev *indio_dev)
{
	struct adis16480 *st = iio_priv(indio_dev);
	int ret;

	ret = adis_write_reg_16(&st->adis, ADIS16480_REG_SLP_CNT, BIT(9));
	if (ret)
		dev_err(&indio_dev->dev,
			"Could not power down device: %d\n", ret);

	return ret;
}

static int adis16480_enable_irq(struct adis *adis, bool enable)
{
	uint16_t val;
	int ret;

	ret = adis_read_reg_16(adis, ADIS16480_REG_FNCTIO_CTRL, &val);
	if (ret < 0)
		return ret;

	val &= ~ADIS16480_DRDY_EN_MSK;
	val |= ADIS16480_DRDY_EN(enable);

	return adis_write_reg_16(adis, ADIS16480_REG_FNCTIO_CTRL, val);
}

static int adis16480_initial_setup(struct iio_dev *indio_dev)
{
	struct adis16480 *st = iio_priv(indio_dev);
	uint16_t prod_id;
	unsigned int device_id;
	int ret;

	adis_reset(&st->adis);
	msleep(70);

	ret = adis_write_reg_16(&st->adis, ADIS16480_REG_GLOB_CMD, BIT(1));
	if (ret)
		return ret;
	msleep(30);

	ret = adis_check_status(&st->adis);
	if (ret)
		return ret;

	ret = adis_read_reg_16(&st->adis, ADIS16480_REG_PROD_ID, &prod_id);
	if (ret)
		return ret;

	ret = sscanf(indio_dev->name, "adis%u\n", &device_id);
	if (ret != 1)
		return -EINVAL;

	if (prod_id != device_id)
		dev_warn(&indio_dev->dev, "Device ID(%u) and product ID(%u) do not match.",
				device_id, prod_id);

	return 0;
}

#define ADIS16480_DIAG_STAT_XGYRO_FAIL 0
#define ADIS16480_DIAG_STAT_YGYRO_FAIL 1
#define ADIS16480_DIAG_STAT_ZGYRO_FAIL 2
#define ADIS16480_DIAG_STAT_XACCL_FAIL 3
#define ADIS16480_DIAG_STAT_YACCL_FAIL 4
#define ADIS16480_DIAG_STAT_ZACCL_FAIL 5
#define ADIS16480_DIAG_STAT_XMAGN_FAIL 8
#define ADIS16480_DIAG_STAT_YMAGN_FAIL 9
#define ADIS16480_DIAG_STAT_ZMAGN_FAIL 10
#define ADIS16480_DIAG_STAT_BARO_FAIL 11

static const char * const adis16480_status_error_msgs[] = {
	[ADIS16480_DIAG_STAT_XGYRO_FAIL] = "X-axis gyroscope self-test failure",
	[ADIS16480_DIAG_STAT_YGYRO_FAIL] = "Y-axis gyroscope self-test failure",
	[ADIS16480_DIAG_STAT_ZGYRO_FAIL] = "Z-axis gyroscope self-test failure",
	[ADIS16480_DIAG_STAT_XACCL_FAIL] = "X-axis accelerometer self-test failure",
	[ADIS16480_DIAG_STAT_YACCL_FAIL] = "Y-axis accelerometer self-test failure",
	[ADIS16480_DIAG_STAT_ZACCL_FAIL] = "Z-axis accelerometer self-test failure",
	[ADIS16480_DIAG_STAT_XMAGN_FAIL] = "X-axis magnetometer self-test failure",
	[ADIS16480_DIAG_STAT_YMAGN_FAIL] = "Y-axis magnetometer self-test failure",
	[ADIS16480_DIAG_STAT_ZMAGN_FAIL] = "Z-axis magnetometer self-test failure",
	[ADIS16480_DIAG_STAT_BARO_FAIL] = "Barometer self-test failure",
};

static const struct adis_data adis16480_data = {
	.diag_stat_reg = ADIS16480_REG_DIAG_STS,
	.glob_cmd_reg = ADIS16480_REG_GLOB_CMD,
	.has_paging = true,

	.read_delay = 5,
	.write_delay = 5,

	.status_error_msgs = adis16480_status_error_msgs,
	.status_error_mask = BIT(ADIS16480_DIAG_STAT_XGYRO_FAIL) |
		BIT(ADIS16480_DIAG_STAT_YGYRO_FAIL) |
		BIT(ADIS16480_DIAG_STAT_ZGYRO_FAIL) |
		BIT(ADIS16480_DIAG_STAT_XACCL_FAIL) |
		BIT(ADIS16480_DIAG_STAT_YACCL_FAIL) |
		BIT(ADIS16480_DIAG_STAT_ZACCL_FAIL) |
		BIT(ADIS16480_DIAG_STAT_XMAGN_FAIL) |
		BIT(ADIS16480_DIAG_STAT_YMAGN_FAIL) |
		BIT(ADIS16480_DIAG_STAT_ZMAGN_FAIL) |
		BIT(ADIS16480_DIAG_STAT_BARO_FAIL),

	.enable_irq = adis16480_enable_irq,
};

static int adis16480_config_irq_pin(struct device_node *of_node,
				    struct adis16480 *st)
{
	struct irq_data *desc;
	enum adis16480_int_pin pin;
	unsigned int irq_type;
	uint16_t val;
	int i, irq = 0;

	desc = irq_get_irq_data(st->adis.spi->irq);
	if (!desc) {
		dev_err(&st->adis.spi->dev, "Could not find IRQ %d\n", irq);
		return -EINVAL;
	}

	/* Disable data ready since the default after reset is on */
	val = ADIS16480_DRDY_EN(0);

	/*
	 * Get the interrupt from the devicetre by reading the interrupt-names
	 * property. If it is not specified, use DIO1 pin as default.
	 * According to the datasheet, the factory default assigns DIO2 as data
	 * ready signal. However, in the previous versions of the driver, DIO1
	 * pin was used. So, we should leave it as is since some devices might
	 * be expecting the interrupt on the wrong physical pin.
	 */
	pin = ADIS16480_PIN_DIO1;
	for (i = 0; i < ARRAY_SIZE(adis16480_int_pin_names); i++) {
		irq = of_irq_get_byname(of_node, adis16480_int_pin_names[i]);
		if (irq > 0) {
			pin = i;
			break;
		}
	}

	val |= ADIS16480_DRDY_SEL(pin);

	/*
	 * Get the interrupt line behaviour. The data ready polarity can be
	 * configured as positive or negative, corresponding to
	 * IRQF_TRIGGER_RISING or IRQF_TRIGGER_FALLING respectively.
	 */
	irq_type = irqd_get_trigger_type(desc);
	if (irq_type == IRQF_TRIGGER_RISING) { /* Default */
		val |= ADIS16480_DRDY_POL(1);
	} else if (irq_type == IRQF_TRIGGER_FALLING) {
		val |= ADIS16480_DRDY_POL(0);
	} else {
		dev_err(&st->adis.spi->dev,
			"Invalid interrupt type 0x%x specified\n", irq_type);
		return -EINVAL;
	}
	/* Write the data ready configuration to the FNCTIO_CTRL register */
	return adis_write_reg_16(&st->adis, ADIS16480_REG_FNCTIO_CTRL, val);
}

static int adis16480_of_get_ext_clk_pin(struct adis16480 *st,
					struct device_node *of_node)
{
	const char *ext_clk_pin;
	enum adis16480_int_pin pin;
	int i;

	pin = ADIS16480_PIN_DIO2;
	if (of_property_read_string(of_node, "adi,ext-clk-pin", &ext_clk_pin))
		goto clk_input_not_found;

	for (i = 0; i < ARRAY_SIZE(adis16480_int_pin_names); i++) {
		if (strcasecmp(ext_clk_pin, adis16480_int_pin_names[i]) == 0)
			return i;
	}

clk_input_not_found:
	dev_info(&st->adis.spi->dev,
		"clk input line not specified, using DIO2\n");
	return pin;
}

static int adis16480_ext_clk_config(struct adis16480 *st,
				    struct device_node *of_node,
				    bool enable)
{
	unsigned int mode, mask;
	enum adis16480_int_pin pin;
	uint16_t val;
	int ret;

	ret = adis_read_reg_16(&st->adis, ADIS16480_REG_FNCTIO_CTRL, &val);
	if (ret < 0)
		return ret;

	pin = adis16480_of_get_ext_clk_pin(st, of_node);
	/*
	 * Each DIOx pin supports only one function at a time. When a single pin
	 * has two assignments, the enable bit for a lower priority function
	 * automatically resets to zero (disabling the lower priority function).
	 */
	if (pin == ADIS16480_DRDY_SEL(val))
		dev_warn(&st->adis.spi->dev,
			"DIO%x pin supports only one function at a time\n",
			pin + 1);

	mode = ADIS16480_SYNC_EN(enable) | ADIS16480_SYNC_SEL(pin);
	mask = ADIS16480_SYNC_EN_MSK | ADIS16480_SYNC_SEL_MSK;
	/* Only ADIS1649x devices support pps ext clock mode */
	if (st->chip_info->has_pps_clk_mode) {
		mode |= ADIS16480_SYNC_MODE(st->clk_mode);
		mask |= ADIS16480_SYNC_MODE_MSK;
	}

	val &= ~mask;
	val |= mode;

	ret = adis_write_reg_16(&st->adis, ADIS16480_REG_FNCTIO_CTRL, val);
	if (ret < 0)
		return ret;

	return clk_prepare_enable(st->ext_clk);
}

static int adis16480_get_ext_clocks(struct adis16480 *st)
{
	st->clk_mode = ADIS16480_CLK_INT;
	st->ext_clk = devm_clk_get(&st->adis.spi->dev, "sync");
	if (!IS_ERR_OR_NULL(st->ext_clk)) {
		st->clk_mode = ADIS16480_CLK_SYNC;
		return 0;
	}

	if (PTR_ERR(st->ext_clk) != -ENOENT) {
		dev_err(&st->adis.spi->dev, "failed to get ext clk\n");
		return PTR_ERR(st->ext_clk);
	}

	if (st->chip_info->has_pps_clk_mode) {
		st->ext_clk = devm_clk_get(&st->adis.spi->dev, "pps");
		if (!IS_ERR_OR_NULL(st->ext_clk)) {
			st->clk_mode = ADIS16480_CLK_PPS;
			return 0;
		}

		if (PTR_ERR(st->ext_clk) != -ENOENT) {
			dev_err(&st->adis.spi->dev, "failed to get ext clk\n");
			return PTR_ERR(st->ext_clk);
		}
	}

	return 0;
}

static int adis16480_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct iio_dev *indio_dev;
	struct adis16480 *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	spi_set_drvdata(spi, indio_dev);

	st = iio_priv(indio_dev);

	st->chip_info = &adis16480_chip_info[id->driver_data];
	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->channels = st->chip_info->channels;
	indio_dev->num_channels = st->chip_info->num_channels;
	indio_dev->info = &adis16480_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = adis_init(&st->adis, indio_dev, spi, &adis16480_data);
	if (ret)
		return ret;

	ret = adis16480_config_irq_pin(spi->dev.of_node, st);
	if (ret)
		return ret;

	ret = adis16480_get_ext_clocks(st);
	if (ret)
		return ret;

	if (!IS_ERR_OR_NULL(st->ext_clk)) {
		ret = adis16480_ext_clk_config(st, spi->dev.of_node, true);
		if (ret)
			return ret;

		st->clk_freq = clk_get_rate(st->ext_clk);
		st->clk_freq *= 1000; /* micro */
	} else {
		st->clk_freq = st->chip_info->int_clk;
	}

	ret = adis_setup_buffer_and_trigger(&st->adis, indio_dev, NULL);
	if (ret)
		goto error_clk_disable_unprepare;

	ret = adis16480_initial_setup(indio_dev);
	if (ret)
		goto error_cleanup_buffer;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_stop_device;

	adis16480_debugfs_init(indio_dev);

	return 0;

error_stop_device:
	adis16480_stop_device(indio_dev);
error_cleanup_buffer:
	adis_cleanup_buffer_and_trigger(&st->adis, indio_dev);
error_clk_disable_unprepare:
	clk_disable_unprepare(st->ext_clk);
	return ret;
}

static int adis16480_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct adis16480 *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	adis16480_stop_device(indio_dev);

	adis_cleanup_buffer_and_trigger(&st->adis, indio_dev);
	clk_disable_unprepare(st->ext_clk);

	return 0;
}

static const struct spi_device_id adis16480_ids[] = {
	{ "adis16375", ADIS16375 },
	{ "adis16480", ADIS16480 },
	{ "adis16485", ADIS16485 },
	{ "adis16488", ADIS16488 },
	{ "adis16495-1", ADIS16495_1 },
	{ "adis16495-2", ADIS16495_2 },
	{ "adis16495-3", ADIS16495_3 },
	{ "adis16497-1", ADIS16497_1 },
	{ "adis16497-2", ADIS16497_2 },
	{ "adis16497-3", ADIS16497_3 },
	{ }
};
MODULE_DEVICE_TABLE(spi, adis16480_ids);

static const struct of_device_id adis16480_of_match[] = {
	{ .compatible = "adi,adis16375" },
	{ .compatible = "adi,adis16480" },
	{ .compatible = "adi,adis16485" },
	{ .compatible = "adi,adis16488" },
	{ .compatible = "adi,adis16495-1" },
	{ .compatible = "adi,adis16495-2" },
	{ .compatible = "adi,adis16495-3" },
	{ .compatible = "adi,adis16497-1" },
	{ .compatible = "adi,adis16497-2" },
	{ .compatible = "adi,adis16497-3" },
	{ },
};
MODULE_DEVICE_TABLE(of, adis16480_of_match);

static struct spi_driver adis16480_driver = {
	.driver = {
		.name = "adis16480",
		.of_match_table = adis16480_of_match,
	},
	.id_table = adis16480_ids,
	.probe = adis16480_probe,
	.remove = adis16480_remove,
};
module_spi_driver(adis16480_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Analog Devices ADIS16480 IMU driver");
MODULE_LICENSE("GPL v2");
