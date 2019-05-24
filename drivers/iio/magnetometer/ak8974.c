/*
 * Driver for the Asahi Kasei EMD Corporation AK8974
 * and Aichi Steel AMI305 magnetometer chips.
 * Based on a patch from Samu Onkalo and the AK8975 IIO driver.
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (c) 2010 NVIDIA Corporation.
 * Copyright (C) 2016 Linaro Ltd.
 *
 * Author: Samu Onkalo <samu.p.onkalo@nokia.com>
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h> /* For irq_get_irq_data() */
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/random.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

/*
 * 16-bit registers are little-endian. LSB is at the address defined below
 * and MSB is at the next higher address.
 */

/* These registers are common for AK8974 and AMI30x */
#define AK8974_SELFTEST		0x0C
#define AK8974_SELFTEST_IDLE	0x55
#define AK8974_SELFTEST_OK	0xAA

#define AK8974_INFO		0x0D

#define AK8974_WHOAMI		0x0F
#define AK8974_WHOAMI_VALUE_AMI306 0x46
#define AK8974_WHOAMI_VALUE_AMI305 0x47
#define AK8974_WHOAMI_VALUE_AK8974 0x48

#define AK8974_DATA_X		0x10
#define AK8974_DATA_Y		0x12
#define AK8974_DATA_Z		0x14
#define AK8974_INT_SRC		0x16
#define AK8974_STATUS		0x18
#define AK8974_INT_CLEAR	0x1A
#define AK8974_CTRL1		0x1B
#define AK8974_CTRL2		0x1C
#define AK8974_CTRL3		0x1D
#define AK8974_INT_CTRL		0x1E
#define AK8974_INT_THRES	0x26  /* Absolute any axis value threshold */
#define AK8974_PRESET		0x30

/* AK8974-specific offsets */
#define AK8974_OFFSET_X		0x20
#define AK8974_OFFSET_Y		0x22
#define AK8974_OFFSET_Z		0x24
/* AMI305-specific offsets */
#define AMI305_OFFSET_X		0x6C
#define AMI305_OFFSET_Y		0x72
#define AMI305_OFFSET_Z		0x78

/* Different temperature registers */
#define AK8974_TEMP		0x31
#define AMI305_TEMP		0x60

/* AMI306-specific control register */
#define AMI306_CTRL4		0x5C

/* AMI306 factory calibration data */

/* fine axis sensitivity */
#define AMI306_FINEOUTPUT_X	0x90
#define AMI306_FINEOUTPUT_Y	0x92
#define AMI306_FINEOUTPUT_Z	0x94

/* axis sensitivity */
#define AMI306_SENS_X		0x96
#define AMI306_SENS_Y		0x98
#define AMI306_SENS_Z		0x9A

/* axis cross-interference */
#define AMI306_GAIN_PARA_XZ	0x9C
#define AMI306_GAIN_PARA_XY	0x9D
#define AMI306_GAIN_PARA_YZ	0x9E
#define AMI306_GAIN_PARA_YX	0x9F
#define AMI306_GAIN_PARA_ZY	0xA0
#define AMI306_GAIN_PARA_ZX	0xA1

/* offset at ZERO magnetic field */
#define AMI306_OFFZERO_X	0xF8
#define AMI306_OFFZERO_Y	0xFA
#define AMI306_OFFZERO_Z	0xFC


#define AK8974_INT_X_HIGH	BIT(7) /* Axis over +threshold  */
#define AK8974_INT_Y_HIGH	BIT(6)
#define AK8974_INT_Z_HIGH	BIT(5)
#define AK8974_INT_X_LOW	BIT(4) /* Axis below -threshold	*/
#define AK8974_INT_Y_LOW	BIT(3)
#define AK8974_INT_Z_LOW	BIT(2)
#define AK8974_INT_RANGE	BIT(1) /* Range overflow (any axis) */

#define AK8974_STATUS_DRDY	BIT(6) /* Data ready */
#define AK8974_STATUS_OVERRUN	BIT(5) /* Data overrun */
#define AK8974_STATUS_INT	BIT(4) /* Interrupt occurred */

#define AK8974_CTRL1_POWER	BIT(7) /* 0 = standby; 1 = active */
#define AK8974_CTRL1_RATE	BIT(4) /* 0 = 10 Hz; 1 = 20 Hz	 */
#define AK8974_CTRL1_FORCE_EN	BIT(1) /* 0 = normal; 1 = force	 */
#define AK8974_CTRL1_MODE2	BIT(0) /* 0 */

#define AK8974_CTRL2_INT_EN	BIT(4)  /* 1 = enable interrupts	      */
#define AK8974_CTRL2_DRDY_EN	BIT(3)  /* 1 = enable data ready signal */
#define AK8974_CTRL2_DRDY_POL	BIT(2)  /* 1 = data ready active high   */
#define AK8974_CTRL2_RESDEF	(AK8974_CTRL2_DRDY_POL)

#define AK8974_CTRL3_RESET	BIT(7) /* Software reset		  */
#define AK8974_CTRL3_FORCE	BIT(6) /* Start forced measurement */
#define AK8974_CTRL3_SELFTEST	BIT(4) /* Set selftest register	  */
#define AK8974_CTRL3_RESDEF	0x00

#define AK8974_INT_CTRL_XEN	BIT(7) /* Enable interrupt for this axis */
#define AK8974_INT_CTRL_YEN	BIT(6)
#define AK8974_INT_CTRL_ZEN	BIT(5)
#define AK8974_INT_CTRL_XYZEN	(BIT(7)|BIT(6)|BIT(5))
#define AK8974_INT_CTRL_POL	BIT(3) /* 0 = active low; 1 = active high */
#define AK8974_INT_CTRL_PULSE	BIT(1) /* 0 = latched; 1 = pulse (50 usec) */
#define AK8974_INT_CTRL_RESDEF	(AK8974_INT_CTRL_XYZEN | AK8974_INT_CTRL_POL)

/* The AMI305 has elaborate FW version and serial number registers */
#define AMI305_VER		0xE8
#define AMI305_SN		0xEA

#define AK8974_MAX_RANGE	2048

#define AK8974_POWERON_DELAY	50
#define AK8974_ACTIVATE_DELAY	1
#define AK8974_SELFTEST_DELAY	1
/*
 * Set the autosuspend to two orders of magnitude larger than the poweron
 * delay to make sane reasonable power tradeoff savings (5 seconds in
 * this case).
 */
#define AK8974_AUTOSUSPEND_DELAY 5000

#define AK8974_MEASTIME		3

#define AK8974_PWR_ON		1
#define AK8974_PWR_OFF		0

/**
 * struct ak8974 - state container for the AK8974 driver
 * @i2c: parent I2C client
 * @orientation: mounting matrix, flipped axis etc
 * @map: regmap to access the AK8974 registers over I2C
 * @regs: the avdd and dvdd power regulators
 * @name: the name of the part
 * @variant: the whoami ID value (for selecting code paths)
 * @lock: locks the magnetometer for exclusive use during a measurement
 * @drdy_irq: uses the DRDY IRQ line
 * @drdy_complete: completion for DRDY
 * @drdy_active_low: the DRDY IRQ is active low
 */
struct ak8974 {
	struct i2c_client *i2c;
	struct iio_mount_matrix orientation;
	struct regmap *map;
	struct regulator_bulk_data regs[2];
	const char *name;
	u8 variant;
	struct mutex lock;
	bool drdy_irq;
	struct completion drdy_complete;
	bool drdy_active_low;
};

static const char ak8974_reg_avdd[] = "avdd";
static const char ak8974_reg_dvdd[] = "dvdd";

static int ak8974_get_u16_val(struct ak8974 *ak8974, u8 reg, u16 *val)
{
	int ret;
	__le16 bulk;

	ret = regmap_bulk_read(ak8974->map, reg, &bulk, 2);
	if (ret)
		return ret;
	*val = le16_to_cpu(bulk);

	return 0;
}

static int ak8974_set_u16_val(struct ak8974 *ak8974, u8 reg, u16 val)
{
	__le16 bulk = cpu_to_le16(val);

	return regmap_bulk_write(ak8974->map, reg, &bulk, 2);
}

static int ak8974_set_power(struct ak8974 *ak8974, bool mode)
{
	int ret;
	u8 val;

	val = mode ? AK8974_CTRL1_POWER : 0;
	val |= AK8974_CTRL1_FORCE_EN;
	ret = regmap_write(ak8974->map, AK8974_CTRL1, val);
	if (ret < 0)
		return ret;

	if (mode)
		msleep(AK8974_ACTIVATE_DELAY);

	return 0;
}

static int ak8974_reset(struct ak8974 *ak8974)
{
	int ret;

	/* Power on to get register access. Sets CTRL1 reg to reset state */
	ret = ak8974_set_power(ak8974, AK8974_PWR_ON);
	if (ret)
		return ret;
	ret = regmap_write(ak8974->map, AK8974_CTRL2, AK8974_CTRL2_RESDEF);
	if (ret)
		return ret;
	ret = regmap_write(ak8974->map, AK8974_CTRL3, AK8974_CTRL3_RESDEF);
	if (ret)
		return ret;
	ret = regmap_write(ak8974->map, AK8974_INT_CTRL,
			   AK8974_INT_CTRL_RESDEF);
	if (ret)
		return ret;

	/* After reset, power off is default state */
	return ak8974_set_power(ak8974, AK8974_PWR_OFF);
}

static int ak8974_configure(struct ak8974 *ak8974)
{
	int ret;

	ret = regmap_write(ak8974->map, AK8974_CTRL2, AK8974_CTRL2_DRDY_EN |
			   AK8974_CTRL2_INT_EN);
	if (ret)
		return ret;
	ret = regmap_write(ak8974->map, AK8974_CTRL3, 0);
	if (ret)
		return ret;
	if (ak8974->variant == AK8974_WHOAMI_VALUE_AMI306) {
		/* magic from datasheet: set high-speed measurement mode */
		ret = ak8974_set_u16_val(ak8974, AMI306_CTRL4, 0xA07E);
		if (ret)
			return ret;
	}
	ret = regmap_write(ak8974->map, AK8974_INT_CTRL, AK8974_INT_CTRL_POL);
	if (ret)
		return ret;

	return regmap_write(ak8974->map, AK8974_PRESET, 0);
}

static int ak8974_trigmeas(struct ak8974 *ak8974)
{
	unsigned int clear;
	u8 mask;
	u8 val;
	int ret;

	/* Clear any previous measurement overflow status */
	ret = regmap_read(ak8974->map, AK8974_INT_CLEAR, &clear);
	if (ret)
		return ret;

	/* If we have a DRDY IRQ line, use it */
	if (ak8974->drdy_irq) {
		mask = AK8974_CTRL2_INT_EN |
			AK8974_CTRL2_DRDY_EN |
			AK8974_CTRL2_DRDY_POL;
		val = AK8974_CTRL2_DRDY_EN;

		if (!ak8974->drdy_active_low)
			val |= AK8974_CTRL2_DRDY_POL;

		init_completion(&ak8974->drdy_complete);
		ret = regmap_update_bits(ak8974->map, AK8974_CTRL2,
					 mask, val);
		if (ret)
			return ret;
	}

	/* Force a measurement */
	return regmap_update_bits(ak8974->map,
				  AK8974_CTRL3,
				  AK8974_CTRL3_FORCE,
				  AK8974_CTRL3_FORCE);
}

static int ak8974_await_drdy(struct ak8974 *ak8974)
{
	int timeout = 2;
	unsigned int val;
	int ret;

	if (ak8974->drdy_irq) {
		ret = wait_for_completion_timeout(&ak8974->drdy_complete,
					1 + msecs_to_jiffies(1000));
		if (!ret) {
			dev_err(&ak8974->i2c->dev,
				"timeout waiting for DRDY IRQ\n");
			return -ETIMEDOUT;
		}
		return 0;
	}

	/* Default delay-based poll loop */
	do {
		msleep(AK8974_MEASTIME);
		ret = regmap_read(ak8974->map, AK8974_STATUS, &val);
		if (ret < 0)
			return ret;
		if (val & AK8974_STATUS_DRDY)
			return 0;
	} while (--timeout);

	dev_err(&ak8974->i2c->dev, "timeout waiting for DRDY\n");
	return -ETIMEDOUT;
}

static int ak8974_getresult(struct ak8974 *ak8974, __le16 *result)
{
	unsigned int src;
	int ret;

	ret = ak8974_await_drdy(ak8974);
	if (ret)
		return ret;
	ret = regmap_read(ak8974->map, AK8974_INT_SRC, &src);
	if (ret < 0)
		return ret;

	/* Out of range overflow! Strong magnet close? */
	if (src & AK8974_INT_RANGE) {
		dev_err(&ak8974->i2c->dev,
			"range overflow in sensor\n");
		return -ERANGE;
	}

	ret = regmap_bulk_read(ak8974->map, AK8974_DATA_X, result, 6);
	if (ret)
		return ret;

	return ret;
}

static irqreturn_t ak8974_drdy_irq(int irq, void *d)
{
	struct ak8974 *ak8974 = d;

	if (!ak8974->drdy_irq)
		return IRQ_NONE;

	/* TODO: timestamp here to get good measurement stamps */
	return IRQ_WAKE_THREAD;
}

static irqreturn_t ak8974_drdy_irq_thread(int irq, void *d)
{
	struct ak8974 *ak8974 = d;
	unsigned int val;
	int ret;

	/* Check if this was a DRDY from us */
	ret = regmap_read(ak8974->map, AK8974_STATUS, &val);
	if (ret < 0) {
		dev_err(&ak8974->i2c->dev, "error reading DRDY status\n");
		return IRQ_HANDLED;
	}
	if (val & AK8974_STATUS_DRDY) {
		/* Yes this was our IRQ */
		complete(&ak8974->drdy_complete);
		return IRQ_HANDLED;
	}

	/* We may be on a shared IRQ, let the next client check */
	return IRQ_NONE;
}

static int ak8974_selftest(struct ak8974 *ak8974)
{
	struct device *dev = &ak8974->i2c->dev;
	unsigned int val;
	int ret;

	ret = regmap_read(ak8974->map, AK8974_SELFTEST, &val);
	if (ret)
		return ret;
	if (val != AK8974_SELFTEST_IDLE) {
		dev_err(dev, "selftest not idle before test\n");
		return -EIO;
	}

	/* Trigger self-test */
	ret = regmap_update_bits(ak8974->map,
			AK8974_CTRL3,
			AK8974_CTRL3_SELFTEST,
			AK8974_CTRL3_SELFTEST);
	if (ret) {
		dev_err(dev, "could not write CTRL3\n");
		return ret;
	}

	msleep(AK8974_SELFTEST_DELAY);

	ret = regmap_read(ak8974->map, AK8974_SELFTEST, &val);
	if (ret)
		return ret;
	if (val != AK8974_SELFTEST_OK) {
		dev_err(dev, "selftest result NOT OK (%02x)\n", val);
		return -EIO;
	}

	ret = regmap_read(ak8974->map, AK8974_SELFTEST, &val);
	if (ret)
		return ret;
	if (val != AK8974_SELFTEST_IDLE) {
		dev_err(dev, "selftest not idle after test (%02x)\n", val);
		return -EIO;
	}
	dev_dbg(dev, "passed self-test\n");

	return 0;
}

static void ak8974_read_calib_data(struct ak8974 *ak8974, unsigned int reg,
				   __le16 *tab, size_t tab_size)
{
	int ret = regmap_bulk_read(ak8974->map, reg, tab, tab_size);
	if (ret) {
		memset(tab, 0xFF, tab_size);
		dev_warn(&ak8974->i2c->dev,
			 "can't read calibration data (regs %u..%zu): %d\n",
			 reg, reg + tab_size - 1, ret);
	} else {
		add_device_randomness(tab, tab_size);
	}
}

static int ak8974_detect(struct ak8974 *ak8974)
{
	unsigned int whoami;
	const char *name;
	int ret;
	unsigned int fw;
	u16 sn;

	ret = regmap_read(ak8974->map, AK8974_WHOAMI, &whoami);
	if (ret)
		return ret;

	name = "ami305";

	switch (whoami) {
	case AK8974_WHOAMI_VALUE_AMI306:
		name = "ami306";
		/* fall-through */
	case AK8974_WHOAMI_VALUE_AMI305:
		ret = regmap_read(ak8974->map, AMI305_VER, &fw);
		if (ret)
			return ret;
		fw &= 0x7f; /* only bits 0 thru 6 valid */
		ret = ak8974_get_u16_val(ak8974, AMI305_SN, &sn);
		if (ret)
			return ret;
		add_device_randomness(&sn, sizeof(sn));
		dev_info(&ak8974->i2c->dev,
			 "detected %s, FW ver %02x, S/N: %04x\n",
			 name, fw, sn);
		break;
	case AK8974_WHOAMI_VALUE_AK8974:
		name = "ak8974";
		dev_info(&ak8974->i2c->dev, "detected AK8974\n");
		break;
	default:
		dev_err(&ak8974->i2c->dev, "unsupported device (%02x) ",
			whoami);
		return -ENODEV;
	}

	ak8974->name = name;
	ak8974->variant = whoami;

	if (whoami == AK8974_WHOAMI_VALUE_AMI306) {
		__le16 fab_data1[9], fab_data2[3];
		int i;

		ak8974_read_calib_data(ak8974, AMI306_FINEOUTPUT_X,
				       fab_data1, sizeof(fab_data1));
		ak8974_read_calib_data(ak8974, AMI306_OFFZERO_X,
				       fab_data2, sizeof(fab_data2));

		for (i = 0; i < 3; ++i) {
			static const char axis[3] = "XYZ";
			static const char pgaxis[6] = "ZYZXYX";
			unsigned offz = le16_to_cpu(fab_data2[i]) & 0x7F;
			unsigned fine = le16_to_cpu(fab_data1[i]);
			unsigned sens = le16_to_cpu(fab_data1[i + 3]);
			unsigned pgain1 = le16_to_cpu(fab_data1[i + 6]);
			unsigned pgain2 = pgain1 >> 8;

			pgain1 &= 0xFF;

			dev_info(&ak8974->i2c->dev,
				 "factory calibration for axis %c: offz=%u sens=%u fine=%u pga%c=%u pga%c=%u\n",
				 axis[i], offz, sens, fine, pgaxis[i * 2],
				 pgain1, pgaxis[i * 2 + 1], pgain2);
		}
	}

	return 0;
}

static int ak8974_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2,
			   long mask)
{
	struct ak8974 *ak8974 = iio_priv(indio_dev);
	__le16 hw_values[3];
	int ret = -EINVAL;

	pm_runtime_get_sync(&ak8974->i2c->dev);
	mutex_lock(&ak8974->lock);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->address > 2) {
			dev_err(&ak8974->i2c->dev, "faulty channel address\n");
			ret = -EIO;
			goto out_unlock;
		}
		ret = ak8974_trigmeas(ak8974);
		if (ret)
			goto out_unlock;
		ret = ak8974_getresult(ak8974, hw_values);
		if (ret)
			goto out_unlock;

		/*
		 * We read all axes and discard all but one, for optimized
		 * reading, use the triggered buffer.
		 */
		*val = le16_to_cpu(hw_values[chan->address]);

		ret = IIO_VAL_INT;
	}

 out_unlock:
	mutex_unlock(&ak8974->lock);
	pm_runtime_mark_last_busy(&ak8974->i2c->dev);
	pm_runtime_put_autosuspend(&ak8974->i2c->dev);

	return ret;
}

static void ak8974_fill_buffer(struct iio_dev *indio_dev)
{
	struct ak8974 *ak8974 = iio_priv(indio_dev);
	int ret;
	__le16 hw_values[8]; /* Three axes + 64bit padding */

	pm_runtime_get_sync(&ak8974->i2c->dev);
	mutex_lock(&ak8974->lock);

	ret = ak8974_trigmeas(ak8974);
	if (ret) {
		dev_err(&ak8974->i2c->dev, "error triggering measure\n");
		goto out_unlock;
	}
	ret = ak8974_getresult(ak8974, hw_values);
	if (ret) {
		dev_err(&ak8974->i2c->dev, "error getting measures\n");
		goto out_unlock;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, hw_values,
					   iio_get_time_ns(indio_dev));

 out_unlock:
	mutex_unlock(&ak8974->lock);
	pm_runtime_mark_last_busy(&ak8974->i2c->dev);
	pm_runtime_put_autosuspend(&ak8974->i2c->dev);
}

static irqreturn_t ak8974_handle_trigger(int irq, void *p)
{
	const struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;

	ak8974_fill_buffer(indio_dev);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_mount_matrix *
ak8974_get_mount_matrix(const struct iio_dev *indio_dev,
			const struct iio_chan_spec *chan)
{
	struct ak8974 *ak8974 = iio_priv(indio_dev);

	return &ak8974->orientation;
}

static const struct iio_chan_spec_ext_info ak8974_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_DIR, ak8974_get_mount_matrix),
	{ },
};

#define AK8974_AXIS_CHANNEL(axis, index)				\
	{								\
		.type = IIO_MAGN,					\
		.modified = 1,						\
		.channel2 = IIO_MOD_##axis,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.ext_info = ak8974_ext_info,				\
		.address = index,					\
		.scan_index = index,					\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = 16,					\
			.storagebits = 16,				\
			.endianness = IIO_LE				\
		},							\
	}

static const struct iio_chan_spec ak8974_channels[] = {
	AK8974_AXIS_CHANNEL(X, 0),
	AK8974_AXIS_CHANNEL(Y, 1),
	AK8974_AXIS_CHANNEL(Z, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const unsigned long ak8974_scan_masks[] = { 0x7, 0 };

static const struct iio_info ak8974_info = {
	.read_raw = &ak8974_read_raw,
};

static bool ak8974_writeable_reg(struct device *dev, unsigned int reg)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct iio_dev *indio_dev = i2c_get_clientdata(i2c);
	struct ak8974 *ak8974 = iio_priv(indio_dev);

	switch (reg) {
	case AK8974_CTRL1:
	case AK8974_CTRL2:
	case AK8974_CTRL3:
	case AK8974_INT_CTRL:
	case AK8974_INT_THRES:
	case AK8974_INT_THRES + 1:
	case AK8974_PRESET:
	case AK8974_PRESET + 1:
		return true;
	case AK8974_OFFSET_X:
	case AK8974_OFFSET_X + 1:
	case AK8974_OFFSET_Y:
	case AK8974_OFFSET_Y + 1:
	case AK8974_OFFSET_Z:
	case AK8974_OFFSET_Z + 1:
		if (ak8974->variant == AK8974_WHOAMI_VALUE_AK8974)
			return true;
		return false;
	case AMI305_OFFSET_X:
	case AMI305_OFFSET_X + 1:
	case AMI305_OFFSET_Y:
	case AMI305_OFFSET_Y + 1:
	case AMI305_OFFSET_Z:
	case AMI305_OFFSET_Z + 1:
		return ak8974->variant == AK8974_WHOAMI_VALUE_AMI305 ||
		       ak8974->variant == AK8974_WHOAMI_VALUE_AMI306;
	case AMI306_CTRL4:
	case AMI306_CTRL4 + 1:
		return ak8974->variant == AK8974_WHOAMI_VALUE_AMI306;
	default:
		return false;
	}
}

static bool ak8974_precious_reg(struct device *dev, unsigned int reg)
{
	return reg == AK8974_INT_CLEAR;
}

static const struct regmap_config ak8974_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
	.writeable_reg = ak8974_writeable_reg,
	.precious_reg = ak8974_precious_reg,
};

static int ak8974_probe(struct i2c_client *i2c,
			const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct ak8974 *ak8974;
	unsigned long irq_trig;
	int irq = i2c->irq;
	int ret;

	/* Register with IIO */
	indio_dev = devm_iio_device_alloc(&i2c->dev, sizeof(*ak8974));
	if (indio_dev == NULL)
		return -ENOMEM;

	ak8974 = iio_priv(indio_dev);
	i2c_set_clientdata(i2c, indio_dev);
	ak8974->i2c = i2c;
	mutex_init(&ak8974->lock);

	ret = iio_read_mount_matrix(&i2c->dev, "mount-matrix",
				    &ak8974->orientation);
	if (ret)
		return ret;

	ak8974->regs[0].supply = ak8974_reg_avdd;
	ak8974->regs[1].supply = ak8974_reg_dvdd;

	ret = devm_regulator_bulk_get(&i2c->dev,
				      ARRAY_SIZE(ak8974->regs),
				      ak8974->regs);
	if (ret < 0) {
		dev_err(&i2c->dev, "cannot get regulators\n");
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(ak8974->regs), ak8974->regs);
	if (ret < 0) {
		dev_err(&i2c->dev, "cannot enable regulators\n");
		return ret;
	}

	/* Take runtime PM online */
	pm_runtime_get_noresume(&i2c->dev);
	pm_runtime_set_active(&i2c->dev);
	pm_runtime_enable(&i2c->dev);

	ak8974->map = devm_regmap_init_i2c(i2c, &ak8974_regmap_config);
	if (IS_ERR(ak8974->map)) {
		dev_err(&i2c->dev, "failed to allocate register map\n");
		return PTR_ERR(ak8974->map);
	}

	ret = ak8974_set_power(ak8974, AK8974_PWR_ON);
	if (ret) {
		dev_err(&i2c->dev, "could not power on\n");
		goto power_off;
	}

	ret = ak8974_detect(ak8974);
	if (ret) {
		dev_err(&i2c->dev, "neither AK8974 nor AMI30x found\n");
		goto power_off;
	}

	ret = ak8974_selftest(ak8974);
	if (ret)
		dev_err(&i2c->dev, "selftest failed (continuing anyway)\n");

	ret = ak8974_reset(ak8974);
	if (ret) {
		dev_err(&i2c->dev, "AK8974 reset failed\n");
		goto power_off;
	}

	pm_runtime_set_autosuspend_delay(&i2c->dev,
					 AK8974_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(&i2c->dev);
	pm_runtime_put(&i2c->dev);

	indio_dev->dev.parent = &i2c->dev;
	indio_dev->channels = ak8974_channels;
	indio_dev->num_channels = ARRAY_SIZE(ak8974_channels);
	indio_dev->info = &ak8974_info;
	indio_dev->available_scan_masks = ak8974_scan_masks;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = ak8974->name;

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
					 ak8974_handle_trigger,
					 NULL);
	if (ret) {
		dev_err(&i2c->dev, "triggered buffer setup failed\n");
		goto disable_pm;
	}

	/* If we have a valid DRDY IRQ, make use of it */
	if (irq > 0) {
		irq_trig = irqd_get_trigger_type(irq_get_irq_data(irq));
		if (irq_trig == IRQF_TRIGGER_RISING) {
			dev_info(&i2c->dev, "enable rising edge DRDY IRQ\n");
		} else if (irq_trig == IRQF_TRIGGER_FALLING) {
			ak8974->drdy_active_low = true;
			dev_info(&i2c->dev, "enable falling edge DRDY IRQ\n");
		} else {
			irq_trig = IRQF_TRIGGER_RISING;
		}
		irq_trig |= IRQF_ONESHOT;
		irq_trig |= IRQF_SHARED;

		ret = devm_request_threaded_irq(&i2c->dev,
						irq,
						ak8974_drdy_irq,
						ak8974_drdy_irq_thread,
						irq_trig,
						ak8974->name,
						ak8974);
		if (ret) {
			dev_err(&i2c->dev, "unable to request DRDY IRQ "
				"- proceeding without IRQ\n");
			goto no_irq;
		}
		ak8974->drdy_irq = true;
	}

no_irq:
	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&i2c->dev, "device register failed\n");
		goto cleanup_buffer;
	}

	return 0;

cleanup_buffer:
	iio_triggered_buffer_cleanup(indio_dev);
disable_pm:
	pm_runtime_put_noidle(&i2c->dev);
	pm_runtime_disable(&i2c->dev);
	ak8974_set_power(ak8974, AK8974_PWR_OFF);
power_off:
	regulator_bulk_disable(ARRAY_SIZE(ak8974->regs), ak8974->regs);

	return ret;
}

static int ak8974_remove(struct i2c_client *i2c)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(i2c);
	struct ak8974 *ak8974 = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	pm_runtime_get_sync(&i2c->dev);
	pm_runtime_put_noidle(&i2c->dev);
	pm_runtime_disable(&i2c->dev);
	ak8974_set_power(ak8974, AK8974_PWR_OFF);
	regulator_bulk_disable(ARRAY_SIZE(ak8974->regs), ak8974->regs);

	return 0;
}

static int __maybe_unused ak8974_runtime_suspend(struct device *dev)
{
	struct ak8974 *ak8974 =
		iio_priv(i2c_get_clientdata(to_i2c_client(dev)));

	ak8974_set_power(ak8974, AK8974_PWR_OFF);
	regulator_bulk_disable(ARRAY_SIZE(ak8974->regs), ak8974->regs);

	return 0;
}

static int __maybe_unused ak8974_runtime_resume(struct device *dev)
{
	struct ak8974 *ak8974 =
		iio_priv(i2c_get_clientdata(to_i2c_client(dev)));
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ak8974->regs), ak8974->regs);
	if (ret)
		return ret;
	msleep(AK8974_POWERON_DELAY);
	ret = ak8974_set_power(ak8974, AK8974_PWR_ON);
	if (ret)
		goto out_regulator_disable;

	ret = ak8974_configure(ak8974);
	if (ret)
		goto out_disable_power;

	return 0;

out_disable_power:
	ak8974_set_power(ak8974, AK8974_PWR_OFF);
out_regulator_disable:
	regulator_bulk_disable(ARRAY_SIZE(ak8974->regs), ak8974->regs);

	return ret;
}

static const struct dev_pm_ops ak8974_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(ak8974_runtime_suspend,
			   ak8974_runtime_resume, NULL)
};

static const struct i2c_device_id ak8974_id[] = {
	{"ami305", 0 },
	{"ami306", 0 },
	{"ak8974", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, ak8974_id);

static const struct of_device_id ak8974_of_match[] = {
	{ .compatible = "asahi-kasei,ak8974", },
	{}
};
MODULE_DEVICE_TABLE(of, ak8974_of_match);

static struct i2c_driver ak8974_driver = {
	.driver	 = {
		.name	= "ak8974",
		.pm = &ak8974_dev_pm_ops,
		.of_match_table = of_match_ptr(ak8974_of_match),
	},
	.probe	  = ak8974_probe,
	.remove	  = ak8974_remove,
	.id_table = ak8974_id,
};
module_i2c_driver(ak8974_driver);

MODULE_DESCRIPTION("AK8974 and AMI30x 3-axis magnetometer driver");
MODULE_AUTHOR("Samu Onkalo");
MODULE_AUTHOR("Linus Walleij");
MODULE_LICENSE("GPL v2");
