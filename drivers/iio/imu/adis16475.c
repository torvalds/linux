// SPDX-License-Identifier: GPL-2.0
/*
 * ADIS16475 IMU driver
 *
 * Copyright 2019 Analog Devices Inc.
 */
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/imu/adis.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/irq.h>
#include <linux/lcm.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/spi/spi.h>

#define ADIS16475_REG_DIAG_STAT		0x02
#define ADIS16475_REG_X_GYRO_L		0x04
#define ADIS16475_REG_Y_GYRO_L		0x08
#define ADIS16475_REG_Z_GYRO_L		0x0C
#define ADIS16475_REG_X_ACCEL_L		0x10
#define ADIS16475_REG_Y_ACCEL_L		0x14
#define ADIS16475_REG_Z_ACCEL_L		0x18
#define ADIS16475_REG_TEMP_OUT		0x1c
#define ADIS16475_REG_X_DELTANG_L	0x24
#define ADIS16475_REG_Y_DELTANG_L	0x28
#define ADIS16475_REG_Z_DELTANG_L	0x2C
#define ADIS16475_REG_X_DELTVEL_L	0x30
#define ADIS16475_REG_Y_DELTVEL_L	0x34
#define ADIS16475_REG_Z_DELTVEL_L	0x38
#define ADIS16475_REG_X_GYRO_BIAS_L	0x40
#define ADIS16475_REG_Y_GYRO_BIAS_L	0x44
#define ADIS16475_REG_Z_GYRO_BIAS_L	0x48
#define ADIS16475_REG_X_ACCEL_BIAS_L	0x4c
#define ADIS16475_REG_Y_ACCEL_BIAS_L	0x50
#define ADIS16475_REG_Z_ACCEL_BIAS_L	0x54
#define ADIS16475_REG_FILT_CTRL		0x5c
#define ADIS16475_FILT_CTRL_MASK	GENMASK(2, 0)
#define ADIS16475_FILT_CTRL(x)		FIELD_PREP(ADIS16475_FILT_CTRL_MASK, x)
#define ADIS16475_REG_MSG_CTRL		0x60
#define ADIS16475_MSG_CTRL_DR_POL_MASK	BIT(0)
#define ADIS16475_MSG_CTRL_DR_POL(x) \
				FIELD_PREP(ADIS16475_MSG_CTRL_DR_POL_MASK, x)
#define ADIS16475_SYNC_MODE_MASK	GENMASK(4, 2)
#define ADIS16475_SYNC_MODE(x)		FIELD_PREP(ADIS16475_SYNC_MODE_MASK, x)
#define ADIS16475_REG_UP_SCALE		0x62
#define ADIS16475_REG_DEC_RATE		0x64
#define ADIS16475_REG_GLOB_CMD		0x68
#define ADIS16475_REG_FIRM_REV		0x6c
#define ADIS16475_REG_FIRM_DM		0x6e
#define ADIS16475_REG_FIRM_Y		0x70
#define ADIS16475_REG_PROD_ID		0x72
#define ADIS16475_REG_SERIAL_NUM	0x74
#define ADIS16475_REG_FLASH_CNT		0x7c
#define ADIS16500_BURST_DATA_SEL_MASK	BIT(8)
#define ADIS16500_BURST32_MASK		BIT(9)
#define ADIS16500_BURST32(x)		FIELD_PREP(ADIS16500_BURST32_MASK, x)
/* number of data elements in burst mode */
#define ADIS16475_BURST32_MAX_DATA	32
#define ADIS16475_BURST_MAX_DATA	20
#define ADIS16475_MAX_SCAN_DATA		20
/* spi max speed in brust mode */
#define ADIS16475_BURST_MAX_SPEED	1000000
#define ADIS16475_LSB_DEC_MASK		0
#define ADIS16475_LSB_FIR_MASK		1
#define ADIS16500_BURST_DATA_SEL_0_CHN_MASK	GENMASK(5, 0)
#define ADIS16500_BURST_DATA_SEL_1_CHN_MASK	GENMASK(12, 7)

enum {
	ADIS16475_SYNC_DIRECT = 1,
	ADIS16475_SYNC_SCALED,
	ADIS16475_SYNC_OUTPUT,
	ADIS16475_SYNC_PULSE = 5,
};

struct adis16475_sync {
	u16 sync_mode;
	u16 min_rate;
	u16 max_rate;
};

struct adis16475_chip_info {
	const struct iio_chan_spec *channels;
	const struct adis16475_sync *sync;
	const struct adis_data adis_data;
	const char *name;
#define ADIS16475_HAS_BURST32		BIT(0)
#define ADIS16475_HAS_BURST_DELTA_DATA	BIT(1)
	const long flags;
	u32 num_channels;
	u32 gyro_max_val;
	u32 gyro_max_scale;
	u32 accel_max_val;
	u32 accel_max_scale;
	u32 temp_scale;
	u32 deltang_max_val;
	u32 deltvel_max_val;
	u32 int_clk;
	u16 max_dec;
	u8 num_sync;
};

struct adis16475 {
	const struct adis16475_chip_info *info;
	struct adis adis;
	u32 clk_freq;
	bool burst32;
	unsigned long lsb_flag;
	u16 sync_mode;
	/* Alignment needed for the timestamp */
	__be16 data[ADIS16475_MAX_SCAN_DATA] __aligned(8);
};

enum {
	ADIS16475_SCAN_GYRO_X,
	ADIS16475_SCAN_GYRO_Y,
	ADIS16475_SCAN_GYRO_Z,
	ADIS16475_SCAN_ACCEL_X,
	ADIS16475_SCAN_ACCEL_Y,
	ADIS16475_SCAN_ACCEL_Z,
	ADIS16475_SCAN_TEMP,
	ADIS16475_SCAN_DELTANG_X,
	ADIS16475_SCAN_DELTANG_Y,
	ADIS16475_SCAN_DELTANG_Z,
	ADIS16475_SCAN_DELTVEL_X,
	ADIS16475_SCAN_DELTVEL_Y,
	ADIS16475_SCAN_DELTVEL_Z,
};

static bool low_rate_allow;
module_param(low_rate_allow, bool, 0444);
MODULE_PARM_DESC(low_rate_allow,
		 "Allow IMU rates below the minimum advisable when external clk is used in SCALED mode (default: N)");

#ifdef CONFIG_DEBUG_FS
static ssize_t adis16475_show_firmware_revision(struct file *file,
						char __user *userbuf,
						size_t count, loff_t *ppos)
{
	struct adis16475 *st = file->private_data;
	char buf[7];
	size_t len;
	u16 rev;
	int ret;

	ret = adis_read_reg_16(&st->adis, ADIS16475_REG_FIRM_REV, &rev);
	if (ret)
		return ret;

	len = scnprintf(buf, sizeof(buf), "%x.%x\n", rev >> 8, rev & 0xff);

	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static const struct file_operations adis16475_firmware_revision_fops = {
	.open = simple_open,
	.read = adis16475_show_firmware_revision,
	.llseek = default_llseek,
	.owner = THIS_MODULE,
};

static ssize_t adis16475_show_firmware_date(struct file *file,
					    char __user *userbuf,
					    size_t count, loff_t *ppos)
{
	struct adis16475 *st = file->private_data;
	u16 md, year;
	char buf[12];
	size_t len;
	int ret;

	ret = adis_read_reg_16(&st->adis, ADIS16475_REG_FIRM_Y, &year);
	if (ret)
		return ret;

	ret = adis_read_reg_16(&st->adis, ADIS16475_REG_FIRM_DM, &md);
	if (ret)
		return ret;

	len = snprintf(buf, sizeof(buf), "%.2x-%.2x-%.4x\n", md >> 8, md & 0xff,
		       year);

	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static const struct file_operations adis16475_firmware_date_fops = {
	.open = simple_open,
	.read = adis16475_show_firmware_date,
	.llseek = default_llseek,
	.owner = THIS_MODULE,
};

static int adis16475_show_serial_number(void *arg, u64 *val)
{
	struct adis16475 *st = arg;
	u16 serial;
	int ret;

	ret = adis_read_reg_16(&st->adis, ADIS16475_REG_SERIAL_NUM, &serial);
	if (ret)
		return ret;

	*val = serial;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(adis16475_serial_number_fops,
			 adis16475_show_serial_number, NULL, "0x%.4llx\n");

static int adis16475_show_product_id(void *arg, u64 *val)
{
	struct adis16475 *st = arg;
	u16 prod_id;
	int ret;

	ret = adis_read_reg_16(&st->adis, ADIS16475_REG_PROD_ID, &prod_id);
	if (ret)
		return ret;

	*val = prod_id;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(adis16475_product_id_fops,
			 adis16475_show_product_id, NULL, "%llu\n");

static int adis16475_show_flash_count(void *arg, u64 *val)
{
	struct adis16475 *st = arg;
	u32 flash_count;
	int ret;

	ret = adis_read_reg_32(&st->adis, ADIS16475_REG_FLASH_CNT,
			       &flash_count);
	if (ret)
		return ret;

	*val = flash_count;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(adis16475_flash_count_fops,
			 adis16475_show_flash_count, NULL, "%lld\n");

static void adis16475_debugfs_init(struct iio_dev *indio_dev)
{
	struct adis16475 *st = iio_priv(indio_dev);
	struct dentry *d = iio_get_debugfs_dentry(indio_dev);

	debugfs_create_file_unsafe("serial_number", 0400,
				   d, st, &adis16475_serial_number_fops);
	debugfs_create_file_unsafe("product_id", 0400,
				   d, st, &adis16475_product_id_fops);
	debugfs_create_file_unsafe("flash_count", 0400,
				   d, st, &adis16475_flash_count_fops);
	debugfs_create_file("firmware_revision", 0400,
			    d, st, &adis16475_firmware_revision_fops);
	debugfs_create_file("firmware_date", 0400, d,
			    st, &adis16475_firmware_date_fops);
}
#else
static void adis16475_debugfs_init(struct iio_dev *indio_dev)
{
}
#endif

static int adis16475_get_freq(struct adis16475 *st, u32 *freq)
{
	int ret;
	u16 dec;
	u32 sample_rate = st->clk_freq;

	adis_dev_lock(&st->adis);

	if (st->sync_mode == ADIS16475_SYNC_SCALED) {
		u16 sync_scale;

		ret = __adis_read_reg_16(&st->adis, ADIS16475_REG_UP_SCALE, &sync_scale);
		if (ret)
			goto error;

		sample_rate = st->clk_freq * sync_scale;
	}

	ret = __adis_read_reg_16(&st->adis, ADIS16475_REG_DEC_RATE, &dec);
	if (ret)
		goto error;

	adis_dev_unlock(&st->adis);

	*freq = DIV_ROUND_CLOSEST(sample_rate, dec + 1);

	return 0;
error:
	adis_dev_unlock(&st->adis);
	return ret;
}

static int adis16475_set_freq(struct adis16475 *st, const u32 freq)
{
	u16 dec;
	int ret;
	u32 sample_rate = st->clk_freq;

	if (!freq)
		return -EINVAL;

	adis_dev_lock(&st->adis);
	/*
	 * When using sync scaled mode, the input clock needs to be scaled so that we have
	 * an IMU sample rate between (optimally) 1900 and 2100. After this, we can use the
	 * decimation filter to lower the sampling rate in order to get what the user wants.
	 * Optimally, the user sample rate is a multiple of both the IMU sample rate and
	 * the input clock. Hence, calculating the sync_scale dynamically gives us better
	 * chances of achieving a perfect/integer value for DEC_RATE. The math here is:
	 *	1. lcm of the input clock and the desired output rate.
	 *	2. get the highest multiple of the previous result lower than the adis max rate.
	 *	3. The last result becomes the IMU sample rate. Use that to calculate SYNC_SCALE
	 *	   and DEC_RATE (to get the user output rate)
	 */
	if (st->sync_mode == ADIS16475_SYNC_SCALED) {
		unsigned long scaled_rate = lcm(st->clk_freq, freq);
		int sync_scale;

		/*
		 * If lcm is bigger than the IMU maximum sampling rate there's no perfect
		 * solution. In this case, we get the highest multiple of the input clock
		 * lower than the IMU max sample rate.
		 */
		if (scaled_rate > 2100000)
			scaled_rate = 2100000 / st->clk_freq * st->clk_freq;
		else
			scaled_rate = 2100000 / scaled_rate * scaled_rate;

		/*
		 * This is not an hard requirement but it's not advised to run the IMU
		 * with a sample rate lower than 1900Hz due to possible undersampling
		 * issues. However, there are users that might really want to take the risk.
		 * Hence, we provide a module parameter for them. If set, we allow sample
		 * rates lower than 1.9KHz. By default, we won't allow this and we just roundup
		 * the rate to the next multiple of the input clock bigger than 1.9KHz. This
		 * is done like this as in some cases (when DEC_RATE is 0) might give
		 * us the closest value to the one desired by the user...
		 */
		if (scaled_rate < 1900000 && !low_rate_allow)
			scaled_rate = roundup(1900000, st->clk_freq);

		sync_scale = scaled_rate / st->clk_freq;
		ret = __adis_write_reg_16(&st->adis, ADIS16475_REG_UP_SCALE, sync_scale);
		if (ret)
			goto error;

		sample_rate = scaled_rate;
	}

	dec = DIV_ROUND_CLOSEST(sample_rate, freq);

	if (dec)
		dec--;

	if (dec > st->info->max_dec)
		dec = st->info->max_dec;

	ret = __adis_write_reg_16(&st->adis, ADIS16475_REG_DEC_RATE, dec);
	if (ret)
		goto error;

	adis_dev_unlock(&st->adis);
	/*
	 * If decimation is used, then gyro and accel data will have meaningful
	 * bits on the LSB registers. This info is used on the trigger handler.
	 */
	assign_bit(ADIS16475_LSB_DEC_MASK, &st->lsb_flag, dec);

	return 0;
error:
	adis_dev_unlock(&st->adis);
	return ret;
}

/* The values are approximated. */
static const u32 adis16475_3db_freqs[] = {
	[0] = 720, /* Filter disabled, full BW (~720Hz) */
	[1] = 360,
	[2] = 164,
	[3] = 80,
	[4] = 40,
	[5] = 20,
	[6] = 10,
};

static int adis16475_get_filter(struct adis16475 *st, u32 *filter)
{
	u16 filter_sz;
	int ret;
	const int mask = ADIS16475_FILT_CTRL_MASK;

	ret = adis_read_reg_16(&st->adis, ADIS16475_REG_FILT_CTRL, &filter_sz);
	if (ret)
		return ret;

	*filter = adis16475_3db_freqs[filter_sz & mask];

	return 0;
}

static int adis16475_set_filter(struct adis16475 *st, const u32 filter)
{
	int i = ARRAY_SIZE(adis16475_3db_freqs);
	int ret;

	while (--i) {
		if (adis16475_3db_freqs[i] >= filter)
			break;
	}

	ret = adis_write_reg_16(&st->adis, ADIS16475_REG_FILT_CTRL,
				ADIS16475_FILT_CTRL(i));
	if (ret)
		return ret;

	/*
	 * If FIR is used, then gyro and accel data will have meaningful
	 * bits on the LSB registers. This info is used on the trigger handler.
	 */
	assign_bit(ADIS16475_LSB_FIR_MASK, &st->lsb_flag, i);

	return 0;
}

static const u32 adis16475_calib_regs[] = {
	[ADIS16475_SCAN_GYRO_X] = ADIS16475_REG_X_GYRO_BIAS_L,
	[ADIS16475_SCAN_GYRO_Y] = ADIS16475_REG_Y_GYRO_BIAS_L,
	[ADIS16475_SCAN_GYRO_Z] = ADIS16475_REG_Z_GYRO_BIAS_L,
	[ADIS16475_SCAN_ACCEL_X] = ADIS16475_REG_X_ACCEL_BIAS_L,
	[ADIS16475_SCAN_ACCEL_Y] = ADIS16475_REG_Y_ACCEL_BIAS_L,
	[ADIS16475_SCAN_ACCEL_Z] = ADIS16475_REG_Z_ACCEL_BIAS_L,
};

static int adis16475_read_raw(struct iio_dev *indio_dev,
			      const struct iio_chan_spec *chan,
			      int *val, int *val2, long info)
{
	struct adis16475 *st = iio_priv(indio_dev);
	int ret;
	u32 tmp;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		return adis_single_conversion(indio_dev, chan, 0, val);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			*val = st->info->gyro_max_val;
			*val2 = st->info->gyro_max_scale;
			return IIO_VAL_FRACTIONAL;
		case IIO_ACCEL:
			*val = st->info->accel_max_val;
			*val2 = st->info->accel_max_scale;
			return IIO_VAL_FRACTIONAL;
		case IIO_TEMP:
			*val = st->info->temp_scale;
			return IIO_VAL_INT;
		case IIO_DELTA_ANGL:
			*val = st->info->deltang_max_val;
			*val2 = 31;
			return IIO_VAL_FRACTIONAL_LOG2;
		case IIO_DELTA_VELOCITY:
			*val = st->info->deltvel_max_val;
			*val2 = 31;
			return IIO_VAL_FRACTIONAL_LOG2;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = adis_read_reg_32(&st->adis,
				       adis16475_calib_regs[chan->scan_index],
				       val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		ret = adis16475_get_filter(st, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = adis16475_get_freq(st, &tmp);
		if (ret)
			return ret;

		*val = tmp / 1000;
		*val2 = (tmp % 1000) * 1000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int adis16475_write_raw(struct iio_dev *indio_dev,
			       const struct iio_chan_spec *chan,
			       int val, int val2, long info)
{
	struct adis16475 *st = iio_priv(indio_dev);
	u32 tmp;

	switch (info) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		tmp = val * 1000 + val2 / 1000;
		return adis16475_set_freq(st, tmp);
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		return adis16475_set_filter(st, val);
	case IIO_CHAN_INFO_CALIBBIAS:
		return adis_write_reg_32(&st->adis,
					 adis16475_calib_regs[chan->scan_index],
					 val);
	default:
		return -EINVAL;
	}
}

#define ADIS16475_MOD_CHAN(_type, _mod, _address, _si, _r_bits, _s_bits) \
	{ \
		.type = (_type), \
		.modified = 1, \
		.channel2 = (_mod), \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			BIT(IIO_CHAN_INFO_CALIBBIAS), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
			BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY), \
		.address = (_address), \
		.scan_index = (_si), \
		.scan_type = { \
			.sign = 's', \
			.realbits = (_r_bits), \
			.storagebits = (_s_bits), \
			.endianness = IIO_BE, \
		}, \
	}

#define ADIS16475_GYRO_CHANNEL(_mod) \
	ADIS16475_MOD_CHAN(IIO_ANGL_VEL, IIO_MOD_ ## _mod, \
			   ADIS16475_REG_ ## _mod ## _GYRO_L, \
			   ADIS16475_SCAN_GYRO_ ## _mod, 32, 32)

#define ADIS16475_ACCEL_CHANNEL(_mod) \
	ADIS16475_MOD_CHAN(IIO_ACCEL, IIO_MOD_ ## _mod, \
			   ADIS16475_REG_ ## _mod ## _ACCEL_L, \
			   ADIS16475_SCAN_ACCEL_ ## _mod, 32, 32)

#define ADIS16475_TEMP_CHANNEL() { \
		.type = IIO_TEMP, \
		.indexed = 1, \
		.channel = 0, \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			BIT(IIO_CHAN_INFO_SCALE), \
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
			BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY), \
		.address = ADIS16475_REG_TEMP_OUT, \
		.scan_index = ADIS16475_SCAN_TEMP, \
		.scan_type = { \
			.sign = 's', \
			.realbits = 16, \
			.storagebits = 16, \
			.endianness = IIO_BE, \
		}, \
	}

#define ADIS16475_MOD_CHAN_DELTA(_type, _mod, _address, _si, _r_bits, _s_bits) { \
		.type = (_type), \
		.modified = 1, \
		.channel2 = (_mod), \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
			BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY), \
		.address = (_address), \
		.scan_index = _si, \
		.scan_type = { \
			.sign = 's', \
			.realbits = (_r_bits), \
			.storagebits = (_s_bits), \
			.endianness = IIO_BE, \
		}, \
	}

#define ADIS16475_DELTANG_CHAN(_mod) \
	ADIS16475_MOD_CHAN_DELTA(IIO_DELTA_ANGL, IIO_MOD_ ## _mod, \
			   ADIS16475_REG_ ## _mod ## _DELTANG_L, ADIS16475_SCAN_DELTANG_ ## _mod, 32, 32)

#define ADIS16475_DELTVEL_CHAN(_mod) \
	ADIS16475_MOD_CHAN_DELTA(IIO_DELTA_VELOCITY, IIO_MOD_ ## _mod, \
			   ADIS16475_REG_ ## _mod ## _DELTVEL_L, ADIS16475_SCAN_DELTVEL_ ## _mod, 32, 32)

#define ADIS16475_DELTANG_CHAN_NO_SCAN(_mod) \
	ADIS16475_MOD_CHAN_DELTA(IIO_DELTA_ANGL, IIO_MOD_ ## _mod, \
			   ADIS16475_REG_ ## _mod ## _DELTANG_L, -1, 32, 32)

#define ADIS16475_DELTVEL_CHAN_NO_SCAN(_mod) \
	ADIS16475_MOD_CHAN_DELTA(IIO_DELTA_VELOCITY, IIO_MOD_ ## _mod, \
			   ADIS16475_REG_ ## _mod ## _DELTVEL_L, -1, 32, 32)

static const struct iio_chan_spec adis16477_channels[] = {
	ADIS16475_GYRO_CHANNEL(X),
	ADIS16475_GYRO_CHANNEL(Y),
	ADIS16475_GYRO_CHANNEL(Z),
	ADIS16475_ACCEL_CHANNEL(X),
	ADIS16475_ACCEL_CHANNEL(Y),
	ADIS16475_ACCEL_CHANNEL(Z),
	ADIS16475_TEMP_CHANNEL(),
	ADIS16475_DELTANG_CHAN(X),
	ADIS16475_DELTANG_CHAN(Y),
	ADIS16475_DELTANG_CHAN(Z),
	ADIS16475_DELTVEL_CHAN(X),
	ADIS16475_DELTVEL_CHAN(Y),
	ADIS16475_DELTVEL_CHAN(Z),
	IIO_CHAN_SOFT_TIMESTAMP(13)
};

static const struct iio_chan_spec adis16475_channels[] = {
	ADIS16475_GYRO_CHANNEL(X),
	ADIS16475_GYRO_CHANNEL(Y),
	ADIS16475_GYRO_CHANNEL(Z),
	ADIS16475_ACCEL_CHANNEL(X),
	ADIS16475_ACCEL_CHANNEL(Y),
	ADIS16475_ACCEL_CHANNEL(Z),
	ADIS16475_TEMP_CHANNEL(),
	ADIS16475_DELTANG_CHAN_NO_SCAN(X),
	ADIS16475_DELTANG_CHAN_NO_SCAN(Y),
	ADIS16475_DELTANG_CHAN_NO_SCAN(Z),
	ADIS16475_DELTVEL_CHAN_NO_SCAN(X),
	ADIS16475_DELTVEL_CHAN_NO_SCAN(Y),
	ADIS16475_DELTVEL_CHAN_NO_SCAN(Z),
	IIO_CHAN_SOFT_TIMESTAMP(7)
};

enum adis16475_variant {
	ADIS16470,
	ADIS16475_1,
	ADIS16475_2,
	ADIS16475_3,
	ADIS16477_1,
	ADIS16477_2,
	ADIS16477_3,
	ADIS16465_1,
	ADIS16465_2,
	ADIS16465_3,
	ADIS16467_1,
	ADIS16467_2,
	ADIS16467_3,
	ADIS16500,
	ADIS16505_1,
	ADIS16505_2,
	ADIS16505_3,
	ADIS16507_1,
	ADIS16507_2,
	ADIS16507_3,
};

enum {
	ADIS16475_DIAG_STAT_DATA_PATH = 1,
	ADIS16475_DIAG_STAT_FLASH_MEM,
	ADIS16475_DIAG_STAT_SPI,
	ADIS16475_DIAG_STAT_STANDBY,
	ADIS16475_DIAG_STAT_SENSOR,
	ADIS16475_DIAG_STAT_MEMORY,
	ADIS16475_DIAG_STAT_CLK,
};

static const char * const adis16475_status_error_msgs[] = {
	[ADIS16475_DIAG_STAT_DATA_PATH] = "Data Path Overrun",
	[ADIS16475_DIAG_STAT_FLASH_MEM] = "Flash memory update failure",
	[ADIS16475_DIAG_STAT_SPI] = "SPI communication error",
	[ADIS16475_DIAG_STAT_STANDBY] = "Standby mode",
	[ADIS16475_DIAG_STAT_SENSOR] = "Sensor failure",
	[ADIS16475_DIAG_STAT_MEMORY] = "Memory failure",
	[ADIS16475_DIAG_STAT_CLK] = "Clock error",
};

#define ADIS16475_DATA(_prod_id, _timeouts)				\
{									\
	.msc_ctrl_reg = ADIS16475_REG_MSG_CTRL,				\
	.glob_cmd_reg = ADIS16475_REG_GLOB_CMD,				\
	.diag_stat_reg = ADIS16475_REG_DIAG_STAT,			\
	.prod_id_reg = ADIS16475_REG_PROD_ID,				\
	.prod_id = (_prod_id),						\
	.self_test_mask = BIT(2),					\
	.self_test_reg = ADIS16475_REG_GLOB_CMD,			\
	.cs_change_delay = 16,						\
	.read_delay = 5,						\
	.write_delay = 5,						\
	.status_error_msgs = adis16475_status_error_msgs,		\
	.status_error_mask = BIT(ADIS16475_DIAG_STAT_DATA_PATH) |	\
		BIT(ADIS16475_DIAG_STAT_FLASH_MEM) |			\
		BIT(ADIS16475_DIAG_STAT_SPI) |				\
		BIT(ADIS16475_DIAG_STAT_STANDBY) |			\
		BIT(ADIS16475_DIAG_STAT_SENSOR) |			\
		BIT(ADIS16475_DIAG_STAT_MEMORY) |			\
		BIT(ADIS16475_DIAG_STAT_CLK),				\
	.unmasked_drdy = true,						\
	.timeouts = (_timeouts),					\
	.burst_reg_cmd = ADIS16475_REG_GLOB_CMD,			\
	.burst_len = ADIS16475_BURST_MAX_DATA,				\
	.burst_max_len = ADIS16475_BURST32_MAX_DATA,			\
	.burst_max_speed_hz = ADIS16475_BURST_MAX_SPEED			\
}

static const struct adis16475_sync adis16475_sync_mode[] = {
	{ ADIS16475_SYNC_OUTPUT },
	{ ADIS16475_SYNC_DIRECT, 1900, 2100 },
	{ ADIS16475_SYNC_SCALED, 1, 128 },
	{ ADIS16475_SYNC_PULSE, 1000, 2100 },
};

static const struct adis_timeout adis16475_timeouts = {
	.reset_ms = 200,
	.sw_reset_ms = 200,
	.self_test_ms = 20,
};

static const struct adis_timeout adis1650x_timeouts = {
	.reset_ms = 260,
	.sw_reset_ms = 260,
	.self_test_ms = 30,
};

static const struct adis16475_chip_info adis16475_chip_info[] = {
	[ADIS16470] = {
		.name = "adis16470",
		.num_channels = ARRAY_SIZE(adis16475_channels),
		.channels = adis16475_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(10 << 16),
		.accel_max_val = 1,
		.accel_max_scale = IIO_M_S_2_TO_G(800 << 16),
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(2160),
		.deltvel_max_val = 400,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		.num_sync = ARRAY_SIZE(adis16475_sync_mode),
		.adis_data = ADIS16475_DATA(16470, &adis16475_timeouts),
	},
	[ADIS16475_1] = {
		.name = "adis16475-1",
		.num_channels = ARRAY_SIZE(adis16475_channels),
		.channels = adis16475_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(160 << 16),
		.accel_max_val = 1,
		.accel_max_scale = IIO_M_S_2_TO_G(4000 << 16),
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(360),
		.deltvel_max_val = 100,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		.num_sync = ARRAY_SIZE(adis16475_sync_mode),
		.adis_data = ADIS16475_DATA(16475, &adis16475_timeouts),
	},
	[ADIS16475_2] = {
		.name = "adis16475-2",
		.num_channels = ARRAY_SIZE(adis16475_channels),
		.channels = adis16475_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(40 << 16),
		.accel_max_val = 1,
		.accel_max_scale = IIO_M_S_2_TO_G(4000 << 16),
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(720),
		.deltvel_max_val = 100,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		.num_sync = ARRAY_SIZE(adis16475_sync_mode),
		.adis_data = ADIS16475_DATA(16475, &adis16475_timeouts),
	},
	[ADIS16475_3] = {
		.name = "adis16475-3",
		.num_channels = ARRAY_SIZE(adis16475_channels),
		.channels = adis16475_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(10 << 16),
		.accel_max_val = 1,
		.accel_max_scale = IIO_M_S_2_TO_G(4000 << 16),
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(2160),
		.deltvel_max_val = 100,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		.num_sync = ARRAY_SIZE(adis16475_sync_mode),
		.adis_data = ADIS16475_DATA(16475, &adis16475_timeouts),
	},
	[ADIS16477_1] = {
		.name = "adis16477-1",
		.num_channels = ARRAY_SIZE(adis16477_channels),
		.channels = adis16477_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(160 << 16),
		.accel_max_val = 1,
		.accel_max_scale = IIO_M_S_2_TO_G(800 << 16),
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(360),
		.deltvel_max_val = 400,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		.num_sync = ARRAY_SIZE(adis16475_sync_mode),
		.flags = ADIS16475_HAS_BURST32 | ADIS16475_HAS_BURST_DELTA_DATA,
		.adis_data = ADIS16475_DATA(16477, &adis16475_timeouts),
	},
	[ADIS16477_2] = {
		.name = "adis16477-2",
		.num_channels = ARRAY_SIZE(adis16477_channels),
		.channels = adis16477_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(40 << 16),
		.accel_max_val = 1,
		.accel_max_scale = IIO_M_S_2_TO_G(800 << 16),
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(720),
		.deltvel_max_val = 400,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		.num_sync = ARRAY_SIZE(adis16475_sync_mode),
		.flags = ADIS16475_HAS_BURST32 | ADIS16475_HAS_BURST_DELTA_DATA,
		.adis_data = ADIS16475_DATA(16477, &adis16475_timeouts),
	},
	[ADIS16477_3] = {
		.name = "adis16477-3",
		.num_channels = ARRAY_SIZE(adis16477_channels),
		.channels = adis16477_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(10 << 16),
		.accel_max_val = 1,
		.accel_max_scale = IIO_M_S_2_TO_G(800 << 16),
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(2160),
		.deltvel_max_val = 400,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		.num_sync = ARRAY_SIZE(adis16475_sync_mode),
		.flags = ADIS16475_HAS_BURST32 | ADIS16475_HAS_BURST_DELTA_DATA,
		.adis_data = ADIS16475_DATA(16477, &adis16475_timeouts),
	},
	[ADIS16465_1] = {
		.name = "adis16465-1",
		.num_channels = ARRAY_SIZE(adis16475_channels),
		.channels = adis16475_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(160 << 16),
		.accel_max_val = 1,
		.accel_max_scale = IIO_M_S_2_TO_G(4000 << 16),
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(360),
		.deltvel_max_val = 100,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		.num_sync = ARRAY_SIZE(adis16475_sync_mode),
		.adis_data = ADIS16475_DATA(16465, &adis16475_timeouts),
	},
	[ADIS16465_2] = {
		.name = "adis16465-2",
		.num_channels = ARRAY_SIZE(adis16475_channels),
		.channels = adis16475_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(40 << 16),
		.accel_max_val = 1,
		.accel_max_scale = IIO_M_S_2_TO_G(4000 << 16),
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(720),
		.deltvel_max_val = 100,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		.num_sync = ARRAY_SIZE(adis16475_sync_mode),
		.adis_data = ADIS16475_DATA(16465, &adis16475_timeouts),
	},
	[ADIS16465_3] = {
		.name = "adis16465-3",
		.num_channels = ARRAY_SIZE(adis16475_channels),
		.channels = adis16475_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(10 << 16),
		.accel_max_val = 1,
		.accel_max_scale = IIO_M_S_2_TO_G(4000 << 16),
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(2160),
		.deltvel_max_val = 100,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		.num_sync = ARRAY_SIZE(adis16475_sync_mode),
		.adis_data = ADIS16475_DATA(16465, &adis16475_timeouts),
	},
	[ADIS16467_1] = {
		.name = "adis16467-1",
		.num_channels = ARRAY_SIZE(adis16475_channels),
		.channels = adis16475_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(160 << 16),
		.accel_max_val = 1,
		.accel_max_scale = IIO_M_S_2_TO_G(800 << 16),
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(360),
		.deltvel_max_val = 400,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		.num_sync = ARRAY_SIZE(adis16475_sync_mode),
		.adis_data = ADIS16475_DATA(16467, &adis16475_timeouts),
	},
	[ADIS16467_2] = {
		.name = "adis16467-2",
		.num_channels = ARRAY_SIZE(adis16475_channels),
		.channels = adis16475_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(40 << 16),
		.accel_max_val = 1,
		.accel_max_scale = IIO_M_S_2_TO_G(800 << 16),
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(720),
		.deltvel_max_val = 400,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		.num_sync = ARRAY_SIZE(adis16475_sync_mode),
		.adis_data = ADIS16475_DATA(16467, &adis16475_timeouts),
	},
	[ADIS16467_3] = {
		.name = "adis16467-3",
		.num_channels = ARRAY_SIZE(adis16475_channels),
		.channels = adis16475_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(10 << 16),
		.accel_max_val = 1,
		.accel_max_scale = IIO_M_S_2_TO_G(800 << 16),
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(2160),
		.deltvel_max_val = 400,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		.num_sync = ARRAY_SIZE(adis16475_sync_mode),
		.adis_data = ADIS16475_DATA(16467, &adis16475_timeouts),
	},
	[ADIS16500] = {
		.name = "adis16500",
		.num_channels = ARRAY_SIZE(adis16477_channels),
		.channels = adis16477_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(10 << 16),
		.accel_max_val = 392,
		.accel_max_scale = 32000 << 16,
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(2160),
		.deltvel_max_val = 400,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		/* pulse sync not supported */
		.num_sync = ARRAY_SIZE(adis16475_sync_mode) - 1,
		.flags = ADIS16475_HAS_BURST32 | ADIS16475_HAS_BURST_DELTA_DATA,
		.adis_data = ADIS16475_DATA(16500, &adis1650x_timeouts),
	},
	[ADIS16505_1] = {
		.name = "adis16505-1",
		.num_channels = ARRAY_SIZE(adis16477_channels),
		.channels = adis16477_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(160 << 16),
		.accel_max_val = 78,
		.accel_max_scale = 32000 << 16,
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(360),
		.deltvel_max_val = 100,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		/* pulse sync not supported */
		.num_sync = ARRAY_SIZE(adis16475_sync_mode) - 1,
		.flags = ADIS16475_HAS_BURST32 | ADIS16475_HAS_BURST_DELTA_DATA,
		.adis_data = ADIS16475_DATA(16505, &adis1650x_timeouts),
	},
	[ADIS16505_2] = {
		.name = "adis16505-2",
		.num_channels = ARRAY_SIZE(adis16477_channels),
		.channels = adis16477_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(40 << 16),
		.accel_max_val = 78,
		.accel_max_scale = 32000 << 16,
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(720),
		.deltvel_max_val = 100,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		/* pulse sync not supported */
		.num_sync = ARRAY_SIZE(adis16475_sync_mode) - 1,
		.flags = ADIS16475_HAS_BURST32 | ADIS16475_HAS_BURST_DELTA_DATA,
		.adis_data = ADIS16475_DATA(16505, &adis1650x_timeouts),
	},
	[ADIS16505_3] = {
		.name = "adis16505-3",
		.num_channels = ARRAY_SIZE(adis16477_channels),
		.channels = adis16477_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(10 << 16),
		.accel_max_val = 78,
		.accel_max_scale = 32000 << 16,
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(2160),
		.deltvel_max_val = 100,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		/* pulse sync not supported */
		.num_sync = ARRAY_SIZE(adis16475_sync_mode) - 1,
		.flags = ADIS16475_HAS_BURST32 | ADIS16475_HAS_BURST_DELTA_DATA,
		.adis_data = ADIS16475_DATA(16505, &adis1650x_timeouts),
	},
	[ADIS16507_1] = {
		.name = "adis16507-1",
		.num_channels = ARRAY_SIZE(adis16477_channels),
		.channels = adis16477_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(160 << 16),
		.accel_max_val = 392,
		.accel_max_scale = 32000 << 16,
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(360),
		.deltvel_max_val = 400,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		/* pulse sync not supported */
		.num_sync = ARRAY_SIZE(adis16475_sync_mode) - 1,
		.flags = ADIS16475_HAS_BURST32 | ADIS16475_HAS_BURST_DELTA_DATA,
		.adis_data = ADIS16475_DATA(16507, &adis1650x_timeouts),
	},
	[ADIS16507_2] = {
		.name = "adis16507-2",
		.num_channels = ARRAY_SIZE(adis16477_channels),
		.channels = adis16477_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(40 << 16),
		.accel_max_val = 392,
		.accel_max_scale = 32000 << 16,
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(720),
		.deltvel_max_val = 400,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		/* pulse sync not supported */
		.num_sync = ARRAY_SIZE(adis16475_sync_mode) - 1,
		.flags = ADIS16475_HAS_BURST32 | ADIS16475_HAS_BURST_DELTA_DATA,
		.adis_data = ADIS16475_DATA(16507, &adis1650x_timeouts),
	},
	[ADIS16507_3] = {
		.name = "adis16507-3",
		.num_channels = ARRAY_SIZE(adis16477_channels),
		.channels = adis16477_channels,
		.gyro_max_val = 1,
		.gyro_max_scale = IIO_RAD_TO_DEGREE(10 << 16),
		.accel_max_val = 392,
		.accel_max_scale = 32000 << 16,
		.temp_scale = 100,
		.deltang_max_val = IIO_DEGREE_TO_RAD(2160),
		.deltvel_max_val = 400,
		.int_clk = 2000,
		.max_dec = 1999,
		.sync = adis16475_sync_mode,
		/* pulse sync not supported */
		.num_sync = ARRAY_SIZE(adis16475_sync_mode) - 1,
		.flags = ADIS16475_HAS_BURST32 | ADIS16475_HAS_BURST_DELTA_DATA,
		.adis_data = ADIS16475_DATA(16507, &adis1650x_timeouts),
	},
};

static int adis16475_update_scan_mode(struct iio_dev *indio_dev,
				      const unsigned long *scan_mask)
{
	u16 en;
	int ret;
	struct adis16475 *st = iio_priv(indio_dev);

	if (st->info->flags & ADIS16475_HAS_BURST_DELTA_DATA) {
		if ((*scan_mask & ADIS16500_BURST_DATA_SEL_0_CHN_MASK) &&
		    (*scan_mask & ADIS16500_BURST_DATA_SEL_1_CHN_MASK))
			return -EINVAL;
		if (*scan_mask & ADIS16500_BURST_DATA_SEL_0_CHN_MASK)
			en = FIELD_PREP(ADIS16500_BURST_DATA_SEL_MASK, 0);
		else
			en = FIELD_PREP(ADIS16500_BURST_DATA_SEL_MASK, 1);

		ret = __adis_update_bits(&st->adis, ADIS16475_REG_MSG_CTRL,
					 ADIS16500_BURST_DATA_SEL_MASK, en);
		if (ret)
			return ret;
	}

	return adis_update_scan_mode(indio_dev, scan_mask);
}

static const struct iio_info adis16475_info = {
	.read_raw = &adis16475_read_raw,
	.write_raw = &adis16475_write_raw,
	.update_scan_mode = adis16475_update_scan_mode,
	.debugfs_reg_access = adis_debugfs_reg_access,
};

static bool adis16475_validate_crc(const u8 *buffer, u16 crc,
				   const bool burst32)
{
	int i;
	/* extra 6 elements for low gyro and accel */
	const u16 sz = burst32 ? ADIS16475_BURST32_MAX_DATA :
		ADIS16475_BURST_MAX_DATA;

	for (i = 0; i < sz - 2; i++)
		crc -= buffer[i];

	return crc == 0;
}

static void adis16475_burst32_check(struct adis16475 *st)
{
	int ret;
	struct adis *adis = &st->adis;

	if (!(st->info->flags & ADIS16475_HAS_BURST32))
		return;

	if (st->lsb_flag && !st->burst32) {
		const u16 en = ADIS16500_BURST32(1);

		ret = __adis_update_bits(&st->adis, ADIS16475_REG_MSG_CTRL,
					 ADIS16500_BURST32_MASK, en);
		if (ret)
			return;

		st->burst32 = true;

		/*
		 * In 32-bit mode we need extra 2 bytes for all gyro
		 * and accel channels.
		 */
		adis->burst_extra_len = 6 * sizeof(u16);
		adis->xfer[1].len += 6 * sizeof(u16);
		dev_dbg(&adis->spi->dev, "Enable burst32 mode, xfer:%d",
			adis->xfer[1].len);

	} else if (!st->lsb_flag && st->burst32) {
		const u16 en = ADIS16500_BURST32(0);

		ret = __adis_update_bits(&st->adis, ADIS16475_REG_MSG_CTRL,
					 ADIS16500_BURST32_MASK, en);
		if (ret)
			return;

		st->burst32 = false;

		/* Remove the extra bits */
		adis->burst_extra_len = 0;
		adis->xfer[1].len -= 6 * sizeof(u16);
		dev_dbg(&adis->spi->dev, "Disable burst32 mode, xfer:%d\n",
			adis->xfer[1].len);
	}
}

static irqreturn_t adis16475_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct adis16475 *st = iio_priv(indio_dev);
	struct adis *adis = &st->adis;
	int ret, bit, buff_offset = 0, i = 0;
	__be16 *buffer;
	u16 crc;
	bool valid;
	/* offset until the first element after gyro and accel */
	const u8 offset = st->burst32 ? 13 : 7;

	ret = spi_sync(adis->spi, &adis->msg);
	if (ret)
		goto check_burst32;

	buffer = adis->buffer;

	crc = be16_to_cpu(buffer[offset + 2]);
	valid = adis16475_validate_crc(adis->buffer, crc, st->burst32);
	if (!valid) {
		dev_err(&adis->spi->dev, "Invalid crc\n");
		goto check_burst32;
	}

	for_each_set_bit(bit, indio_dev->active_scan_mask,
			 indio_dev->masklength) {
		/*
		 * When burst mode is used, system flags is the first data
		 * channel in the sequence, but the scan index is 7.
		 */
		switch (bit) {
		case ADIS16475_SCAN_TEMP:
			st->data[i++] = buffer[offset];
			/*
			 * The temperature channel has 16-bit storage size.
			 * We need to perform the padding to have the buffer
			 * elements naturally aligned in case there are any
			 * 32-bit storage size channels enabled which have a
			 * scan index higher than the temperature channel scan
			 * index.
			 */
			if (*indio_dev->active_scan_mask & GENMASK(ADIS16475_SCAN_DELTVEL_Z, ADIS16475_SCAN_DELTANG_X))
				st->data[i++] = 0;
			break;
		case ADIS16475_SCAN_DELTANG_X ... ADIS16475_SCAN_DELTVEL_Z:
			buff_offset = ADIS16475_SCAN_DELTANG_X;
			fallthrough;
		case ADIS16475_SCAN_GYRO_X ... ADIS16475_SCAN_ACCEL_Z:
			/*
			 * The first 2 bytes on the received data are the
			 * DIAG_STAT reg, hence the +1 offset here...
			 */
			if (st->burst32) {
				/* upper 16 */
				st->data[i++] = buffer[(bit - buff_offset) * 2 + 2];
				/* lower 16 */
				st->data[i++] = buffer[(bit - buff_offset) * 2 + 1];
			} else {
				st->data[i++] = buffer[(bit - buff_offset) + 1];
				/*
				 * Don't bother in doing the manual read if the
				 * device supports burst32. burst32 will be
				 * enabled in the next call to
				 * adis16475_burst32_check()...
				 */
				if (st->lsb_flag && !(st->info->flags & ADIS16475_HAS_BURST32)) {
					u16 val = 0;
					const u32 reg = ADIS16475_REG_X_GYRO_L +
						bit * 4;

					adis_read_reg_16(adis, reg, &val);
					st->data[i++] = cpu_to_be16(val);
				} else {
					/* lower not used */
					st->data[i++] = 0;
				}
			}
			break;
		}
	}

	iio_push_to_buffers_with_timestamp(indio_dev, st->data, pf->timestamp);
check_burst32:
	/*
	 * We only check the burst mode at the end of the current capture since
	 * it takes a full data ready cycle for the device to update the burst
	 * array.
	 */
	adis16475_burst32_check(st);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int adis16475_config_sync_mode(struct adis16475 *st)
{
	int ret;
	struct device *dev = &st->adis.spi->dev;
	const struct adis16475_sync *sync;
	u32 sync_mode;

	/* default to internal clk */
	st->clk_freq = st->info->int_clk * 1000;

	ret = device_property_read_u32(dev, "adi,sync-mode", &sync_mode);
	if (ret)
		return 0;

	if (sync_mode >= st->info->num_sync) {
		dev_err(dev, "Invalid sync mode: %u for %s\n", sync_mode,
			st->info->name);
		return -EINVAL;
	}

	sync = &st->info->sync[sync_mode];
	st->sync_mode = sync->sync_mode;

	/* All the other modes require external input signal */
	if (sync->sync_mode != ADIS16475_SYNC_OUTPUT) {
		struct clk *clk = devm_clk_get_enabled(dev, NULL);

		if (IS_ERR(clk))
			return PTR_ERR(clk);

		st->clk_freq = clk_get_rate(clk);
		if (st->clk_freq < sync->min_rate ||
		    st->clk_freq > sync->max_rate) {
			dev_err(dev,
				"Clk rate:%u not in a valid range:[%u %u]\n",
				st->clk_freq, sync->min_rate, sync->max_rate);
			return -EINVAL;
		}

		if (sync->sync_mode == ADIS16475_SYNC_SCALED) {
			u16 up_scale;

			/*
			 * In sync scaled mode, the IMU sample rate is the clk_freq * sync_scale.
			 * Hence, default the IMU sample rate to the highest multiple of the input
			 * clock lower than the IMU max sample rate. The optimal range is
			 * 1900-2100 sps...
			 */
			up_scale = 2100 / st->clk_freq;

			ret = __adis_write_reg_16(&st->adis,
						  ADIS16475_REG_UP_SCALE,
						  up_scale);
			if (ret)
				return ret;
		}

		st->clk_freq *= 1000;
	}
	/*
	 * Keep in mind that the mask for the clk modes in adis1650*
	 * chips is different (1100 instead of 11100). However, we
	 * are not configuring BIT(4) in these chips and the default
	 * value is 0, so we are fine in doing the below operations.
	 * I'm keeping this for simplicity and avoiding extra variables
	 * in chip_info.
	 */
	ret = __adis_update_bits(&st->adis, ADIS16475_REG_MSG_CTRL,
				 ADIS16475_SYNC_MODE_MASK, sync->sync_mode);
	if (ret)
		return ret;

	usleep_range(250, 260);

	return 0;
}

static int adis16475_config_irq_pin(struct adis16475 *st)
{
	int ret;
	struct irq_data *desc;
	u32 irq_type;
	u16 val = 0;
	u8 polarity;
	struct spi_device *spi = st->adis.spi;

	desc = irq_get_irq_data(spi->irq);
	if (!desc) {
		dev_err(&spi->dev, "Could not find IRQ %d\n", spi->irq);
		return -EINVAL;
	}
	/*
	 * It is possible to configure the data ready polarity. Furthermore, we
	 * need to update the adis struct if we want data ready as active low.
	 */
	irq_type = irqd_get_trigger_type(desc);
	if (irq_type == IRQ_TYPE_EDGE_RISING) {
		polarity = 1;
		st->adis.irq_flag = IRQF_TRIGGER_RISING;
	} else if (irq_type == IRQ_TYPE_EDGE_FALLING) {
		polarity = 0;
		st->adis.irq_flag = IRQF_TRIGGER_FALLING;
	} else {
		dev_err(&spi->dev, "Invalid interrupt type 0x%x specified\n",
			irq_type);
		return -EINVAL;
	}

	val = ADIS16475_MSG_CTRL_DR_POL(polarity);
	ret = __adis_update_bits(&st->adis, ADIS16475_REG_MSG_CTRL,
				 ADIS16475_MSG_CTRL_DR_POL_MASK, val);
	if (ret)
		return ret;
	/*
	 * There is a delay writing to any bits written to the MSC_CTRL
	 * register. It should not be bigger than 200us, so 250 should be more
	 * than enough!
	 */
	usleep_range(250, 260);

	return 0;
}


static int adis16475_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct adis16475 *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->info = spi_get_device_match_data(spi);
	if (!st->info)
		return -EINVAL;

	ret = adis_init(&st->adis, indio_dev, spi, &st->info->adis_data);
	if (ret)
		return ret;

	indio_dev->name = st->info->name;
	indio_dev->channels = st->info->channels;
	indio_dev->num_channels = st->info->num_channels;
	indio_dev->info = &adis16475_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = __adis_initial_startup(&st->adis);
	if (ret)
		return ret;

	ret = adis16475_config_irq_pin(st);
	if (ret)
		return ret;

	ret = adis16475_config_sync_mode(st);
	if (ret)
		return ret;

	ret = devm_adis_setup_buffer_and_trigger(&st->adis, indio_dev,
						 adis16475_trigger_handler);
	if (ret)
		return ret;

	ret = devm_iio_device_register(&spi->dev, indio_dev);
	if (ret)
		return ret;

	adis16475_debugfs_init(indio_dev);

	return 0;
}

static const struct of_device_id adis16475_of_match[] = {
	{ .compatible = "adi,adis16470",
		.data = &adis16475_chip_info[ADIS16470] },
	{ .compatible = "adi,adis16475-1",
		.data = &adis16475_chip_info[ADIS16475_1] },
	{ .compatible = "adi,adis16475-2",
		.data = &adis16475_chip_info[ADIS16475_2] },
	{ .compatible = "adi,adis16475-3",
		.data = &adis16475_chip_info[ADIS16475_3] },
	{ .compatible = "adi,adis16477-1",
		.data = &adis16475_chip_info[ADIS16477_1] },
	{ .compatible = "adi,adis16477-2",
		.data = &adis16475_chip_info[ADIS16477_2] },
	{ .compatible = "adi,adis16477-3",
		.data = &adis16475_chip_info[ADIS16477_3] },
	{ .compatible = "adi,adis16465-1",
		.data = &adis16475_chip_info[ADIS16465_1] },
	{ .compatible = "adi,adis16465-2",
		.data = &adis16475_chip_info[ADIS16465_2] },
	{ .compatible = "adi,adis16465-3",
		.data = &adis16475_chip_info[ADIS16465_3] },
	{ .compatible = "adi,adis16467-1",
		.data = &adis16475_chip_info[ADIS16467_1] },
	{ .compatible = "adi,adis16467-2",
		.data = &adis16475_chip_info[ADIS16467_2] },
	{ .compatible = "adi,adis16467-3",
		.data = &adis16475_chip_info[ADIS16467_3] },
	{ .compatible = "adi,adis16500",
		.data = &adis16475_chip_info[ADIS16500] },
	{ .compatible = "adi,adis16505-1",
		.data = &adis16475_chip_info[ADIS16505_1] },
	{ .compatible = "adi,adis16505-2",
		.data = &adis16475_chip_info[ADIS16505_2] },
	{ .compatible = "adi,adis16505-3",
		.data = &adis16475_chip_info[ADIS16505_3] },
	{ .compatible = "adi,adis16507-1",
		.data = &adis16475_chip_info[ADIS16507_1] },
	{ .compatible = "adi,adis16507-2",
		.data = &adis16475_chip_info[ADIS16507_2] },
	{ .compatible = "adi,adis16507-3",
		.data = &adis16475_chip_info[ADIS16507_3] },
	{ },
};
MODULE_DEVICE_TABLE(of, adis16475_of_match);

static const struct spi_device_id adis16475_ids[] = {
	{ "adis16470", (kernel_ulong_t)&adis16475_chip_info[ADIS16470] },
	{ "adis16475-1", (kernel_ulong_t)&adis16475_chip_info[ADIS16475_1] },
	{ "adis16475-2", (kernel_ulong_t)&adis16475_chip_info[ADIS16475_2] },
	{ "adis16475-3", (kernel_ulong_t)&adis16475_chip_info[ADIS16475_3] },
	{ "adis16477-1", (kernel_ulong_t)&adis16475_chip_info[ADIS16477_1] },
	{ "adis16477-2", (kernel_ulong_t)&adis16475_chip_info[ADIS16477_2] },
	{ "adis16477-3", (kernel_ulong_t)&adis16475_chip_info[ADIS16477_3] },
	{ "adis16465-1", (kernel_ulong_t)&adis16475_chip_info[ADIS16465_1] },
	{ "adis16465-2", (kernel_ulong_t)&adis16475_chip_info[ADIS16465_2] },
	{ "adis16465-3", (kernel_ulong_t)&adis16475_chip_info[ADIS16465_3] },
	{ "adis16467-1", (kernel_ulong_t)&adis16475_chip_info[ADIS16467_1] },
	{ "adis16467-2", (kernel_ulong_t)&adis16475_chip_info[ADIS16467_2] },
	{ "adis16467-3", (kernel_ulong_t)&adis16475_chip_info[ADIS16467_3] },
	{ "adis16500", (kernel_ulong_t)&adis16475_chip_info[ADIS16500] },
	{ "adis16505-1", (kernel_ulong_t)&adis16475_chip_info[ADIS16505_1] },
	{ "adis16505-2", (kernel_ulong_t)&adis16475_chip_info[ADIS16505_2] },
	{ "adis16505-3", (kernel_ulong_t)&adis16475_chip_info[ADIS16505_3] },
	{ "adis16507-1", (kernel_ulong_t)&adis16475_chip_info[ADIS16507_1] },
	{ "adis16507-2", (kernel_ulong_t)&adis16475_chip_info[ADIS16507_2] },
	{ "adis16507-3", (kernel_ulong_t)&adis16475_chip_info[ADIS16507_3] },
	{ }
};
MODULE_DEVICE_TABLE(spi, adis16475_ids);

static struct spi_driver adis16475_driver = {
	.driver = {
		.name = "adis16475",
		.of_match_table = adis16475_of_match,
	},
	.probe = adis16475_probe,
	.id_table = adis16475_ids,
};
module_spi_driver(adis16475_driver);

MODULE_AUTHOR("Nuno Sa <nuno.sa@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16475 IMU driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(IIO_ADISLIB);
