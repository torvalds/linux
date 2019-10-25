// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Seiko Instruments S-35390A RTC Driver
 *
 * Copyright (c) 2007 Byron Bradley
 */

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/i2c.h>
#include <linux/bitrev.h>
#include <linux/bcd.h>
#include <linux/slab.h>
#include <linux/delay.h>

#define S35390A_CMD_STATUS1	0
#define S35390A_CMD_STATUS2	1
#define S35390A_CMD_TIME1	2
#define S35390A_CMD_TIME2	3
#define S35390A_CMD_INT2_REG1	5

#define S35390A_BYTE_YEAR	0
#define S35390A_BYTE_MONTH	1
#define S35390A_BYTE_DAY	2
#define S35390A_BYTE_WDAY	3
#define S35390A_BYTE_HOURS	4
#define S35390A_BYTE_MINS	5
#define S35390A_BYTE_SECS	6

#define S35390A_ALRM_BYTE_WDAY	0
#define S35390A_ALRM_BYTE_HOURS	1
#define S35390A_ALRM_BYTE_MINS	2

/* flags for STATUS1 */
#define S35390A_FLAG_POC	BIT(0)
#define S35390A_FLAG_BLD	BIT(1)
#define S35390A_FLAG_INT2	BIT(2)
#define S35390A_FLAG_24H	BIT(6)
#define S35390A_FLAG_RESET	BIT(7)

/* flag for STATUS2 */
#define S35390A_FLAG_TEST	BIT(0)

/* INT2 pin output mode */
#define S35390A_INT2_MODE_MASK		0x0E
#define S35390A_INT2_MODE_NOINTR	0x00
#define S35390A_INT2_MODE_ALARM		BIT(1) /* INT2AE */
#define S35390A_INT2_MODE_PMIN_EDG	BIT(2) /* INT2ME */
#define S35390A_INT2_MODE_FREQ		BIT(3) /* INT2FE */
#define S35390A_INT2_MODE_PMIN		(BIT(3) | BIT(2)) /* INT2FE | INT2ME */

static const struct i2c_device_id s35390a_id[] = {
	{ "s35390a", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, s35390a_id);

static const struct of_device_id s35390a_of_match[] = {
	{ .compatible = "s35390a" },
	{ .compatible = "sii,s35390a" },
	{ }
};
MODULE_DEVICE_TABLE(of, s35390a_of_match);

struct s35390a {
	struct i2c_client *client[8];
	struct rtc_device *rtc;
	int twentyfourhour;
};

static int s35390a_set_reg(struct s35390a *s35390a, int reg, char *buf, int len)
{
	struct i2c_client *client = s35390a->client[reg];
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.len = len,
			.buf = buf
		},
	};

	if ((i2c_transfer(client->adapter, msg, 1)) != 1)
		return -EIO;

	return 0;
}

static int s35390a_get_reg(struct s35390a *s35390a, int reg, char *buf, int len)
{
	struct i2c_client *client = s35390a->client[reg];
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = buf
		},
	};

	if ((i2c_transfer(client->adapter, msg, 1)) != 1)
		return -EIO;

	return 0;
}

static int s35390a_init(struct s35390a *s35390a)
{
	u8 buf;
	int ret;
	unsigned initcount = 0;

	/*
	 * At least one of POC and BLD are set, so reinitialise chip. Keeping
	 * this information in the hardware to know later that the time isn't
	 * valid is unfortunately not possible because POC and BLD are cleared
	 * on read. So the reset is best done now.
	 *
	 * The 24H bit is kept over reset, so set it already here.
	 */
initialize:
	buf = S35390A_FLAG_RESET | S35390A_FLAG_24H;
	ret = s35390a_set_reg(s35390a, S35390A_CMD_STATUS1, &buf, 1);

	if (ret < 0)
		return ret;

	ret = s35390a_get_reg(s35390a, S35390A_CMD_STATUS1, &buf, 1);
	if (ret < 0)
		return ret;

	if (buf & (S35390A_FLAG_POC | S35390A_FLAG_BLD)) {
		/* Try up to five times to reset the chip */
		if (initcount < 5) {
			++initcount;
			goto initialize;
		} else
			return -EIO;
	}

	return 1;
}

/*
 * Returns <0 on error, 0 if rtc is setup fine and 1 if the chip was reset.
 * To keep the information if an irq is pending, pass the value read from
 * STATUS1 to the caller.
 */
static int s35390a_read_status(struct s35390a *s35390a, char *status1)
{
	int ret;

	ret = s35390a_get_reg(s35390a, S35390A_CMD_STATUS1, status1, 1);
	if (ret < 0)
		return ret;

	if (*status1 & S35390A_FLAG_POC) {
		/*
		 * Do not communicate for 0.5 seconds since the power-on
		 * detection circuit is in operation.
		 */
		msleep(500);
		return 1;
	} else if (*status1 & S35390A_FLAG_BLD)
		return 1;
	/*
	 * If both POC and BLD are unset everything is fine.
	 */
	return 0;
}

static int s35390a_disable_test_mode(struct s35390a *s35390a)
{
	char buf[1];

	if (s35390a_get_reg(s35390a, S35390A_CMD_STATUS2, buf, sizeof(buf)) < 0)
		return -EIO;

	if (!(buf[0] & S35390A_FLAG_TEST))
		return 0;

	buf[0] &= ~S35390A_FLAG_TEST;
	return s35390a_set_reg(s35390a, S35390A_CMD_STATUS2, buf, sizeof(buf));
}

static char s35390a_hr2reg(struct s35390a *s35390a, int hour)
{
	if (s35390a->twentyfourhour)
		return bin2bcd(hour);

	if (hour < 12)
		return bin2bcd(hour);

	return 0x40 | bin2bcd(hour - 12);
}

static int s35390a_reg2hr(struct s35390a *s35390a, char reg)
{
	unsigned hour;

	if (s35390a->twentyfourhour)
		return bcd2bin(reg & 0x3f);

	hour = bcd2bin(reg & 0x3f);
	if (reg & 0x40)
		hour += 12;

	return hour;
}

static int s35390a_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct s35390a	*s35390a = i2c_get_clientdata(client);
	int i, err;
	char buf[7], status;

	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d mday=%d, "
		"mon=%d, year=%d, wday=%d\n", __func__, tm->tm_sec,
		tm->tm_min, tm->tm_hour, tm->tm_mday, tm->tm_mon, tm->tm_year,
		tm->tm_wday);

	if (s35390a_read_status(s35390a, &status) == 1)
		s35390a_init(s35390a);

	buf[S35390A_BYTE_YEAR] = bin2bcd(tm->tm_year - 100);
	buf[S35390A_BYTE_MONTH] = bin2bcd(tm->tm_mon + 1);
	buf[S35390A_BYTE_DAY] = bin2bcd(tm->tm_mday);
	buf[S35390A_BYTE_WDAY] = bin2bcd(tm->tm_wday);
	buf[S35390A_BYTE_HOURS] = s35390a_hr2reg(s35390a, tm->tm_hour);
	buf[S35390A_BYTE_MINS] = bin2bcd(tm->tm_min);
	buf[S35390A_BYTE_SECS] = bin2bcd(tm->tm_sec);

	/* This chip expects the bits of each byte to be in reverse order */
	for (i = 0; i < 7; ++i)
		buf[i] = bitrev8(buf[i]);

	err = s35390a_set_reg(s35390a, S35390A_CMD_TIME1, buf, sizeof(buf));

	return err;
}

static int s35390a_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct s35390a *s35390a = i2c_get_clientdata(client);
	char buf[7], status;
	int i, err;

	if (s35390a_read_status(s35390a, &status) == 1)
		return -EINVAL;

	err = s35390a_get_reg(s35390a, S35390A_CMD_TIME1, buf, sizeof(buf));
	if (err < 0)
		return err;

	/* This chip returns the bits of each byte in reverse order */
	for (i = 0; i < 7; ++i)
		buf[i] = bitrev8(buf[i]);

	tm->tm_sec = bcd2bin(buf[S35390A_BYTE_SECS]);
	tm->tm_min = bcd2bin(buf[S35390A_BYTE_MINS]);
	tm->tm_hour = s35390a_reg2hr(s35390a, buf[S35390A_BYTE_HOURS]);
	tm->tm_wday = bcd2bin(buf[S35390A_BYTE_WDAY]);
	tm->tm_mday = bcd2bin(buf[S35390A_BYTE_DAY]);
	tm->tm_mon = bcd2bin(buf[S35390A_BYTE_MONTH]) - 1;
	tm->tm_year = bcd2bin(buf[S35390A_BYTE_YEAR]) + 100;

	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d, mday=%d, "
		"mon=%d, year=%d, wday=%d\n", __func__, tm->tm_sec,
		tm->tm_min, tm->tm_hour, tm->tm_mday, tm->tm_mon, tm->tm_year,
		tm->tm_wday);

	return 0;
}

static int s35390a_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct s35390a *s35390a = i2c_get_clientdata(client);
	char buf[3], sts = 0;
	int err, i;

	dev_dbg(&client->dev, "%s: alm is secs=%d, mins=%d, hours=%d mday=%d, "\
		"mon=%d, year=%d, wday=%d\n", __func__, alm->time.tm_sec,
		alm->time.tm_min, alm->time.tm_hour, alm->time.tm_mday,
		alm->time.tm_mon, alm->time.tm_year, alm->time.tm_wday);

	if (alm->time.tm_sec != 0)
		dev_warn(&client->dev, "Alarms are only supported on a per minute basis!\n");

	/* disable interrupt (which deasserts the irq line) */
	err = s35390a_set_reg(s35390a, S35390A_CMD_STATUS2, &sts, sizeof(sts));
	if (err < 0)
		return err;

	/* clear pending interrupt (in STATUS1 only), if any */
	err = s35390a_get_reg(s35390a, S35390A_CMD_STATUS1, &sts, sizeof(sts));
	if (err < 0)
		return err;

	if (alm->enabled)
		sts = S35390A_INT2_MODE_ALARM;
	else
		sts = S35390A_INT2_MODE_NOINTR;

	/* set interupt mode*/
	err = s35390a_set_reg(s35390a, S35390A_CMD_STATUS2, &sts, sizeof(sts));
	if (err < 0)
		return err;

	if (alm->time.tm_wday != -1)
		buf[S35390A_ALRM_BYTE_WDAY] = bin2bcd(alm->time.tm_wday) | 0x80;
	else
		buf[S35390A_ALRM_BYTE_WDAY] = 0;

	buf[S35390A_ALRM_BYTE_HOURS] = s35390a_hr2reg(s35390a,
			alm->time.tm_hour) | 0x80;
	buf[S35390A_ALRM_BYTE_MINS] = bin2bcd(alm->time.tm_min) | 0x80;

	if (alm->time.tm_hour >= 12)
		buf[S35390A_ALRM_BYTE_HOURS] |= 0x40;

	for (i = 0; i < 3; ++i)
		buf[i] = bitrev8(buf[i]);

	err = s35390a_set_reg(s35390a, S35390A_CMD_INT2_REG1, buf,
								sizeof(buf));

	return err;
}

static int s35390a_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct s35390a *s35390a = i2c_get_clientdata(client);
	char buf[3], sts;
	int i, err;

	err = s35390a_get_reg(s35390a, S35390A_CMD_STATUS2, &sts, sizeof(sts));
	if (err < 0)
		return err;

	if ((sts & S35390A_INT2_MODE_MASK) != S35390A_INT2_MODE_ALARM) {
		/*
		 * When the alarm isn't enabled, the register to configure
		 * the alarm time isn't accessible.
		 */
		alm->enabled = 0;
		return 0;
	} else {
		alm->enabled = 1;
	}

	err = s35390a_get_reg(s35390a, S35390A_CMD_INT2_REG1, buf, sizeof(buf));
	if (err < 0)
		return err;

	/* This chip returns the bits of each byte in reverse order */
	for (i = 0; i < 3; ++i)
		buf[i] = bitrev8(buf[i]);

	/*
	 * B0 of the three matching registers is an enable flag. Iff it is set
	 * the configured value is used for matching.
	 */
	if (buf[S35390A_ALRM_BYTE_WDAY] & 0x80)
		alm->time.tm_wday =
			bcd2bin(buf[S35390A_ALRM_BYTE_WDAY] & ~0x80);

	if (buf[S35390A_ALRM_BYTE_HOURS] & 0x80)
		alm->time.tm_hour =
			s35390a_reg2hr(s35390a,
				       buf[S35390A_ALRM_BYTE_HOURS] & ~0x80);

	if (buf[S35390A_ALRM_BYTE_MINS] & 0x80)
		alm->time.tm_min = bcd2bin(buf[S35390A_ALRM_BYTE_MINS] & ~0x80);

	/* alarm triggers always at s=0 */
	alm->time.tm_sec = 0;

	dev_dbg(&client->dev, "%s: alm is mins=%d, hours=%d, wday=%d\n",
			__func__, alm->time.tm_min, alm->time.tm_hour,
			alm->time.tm_wday);

	return 0;
}

static int s35390a_rtc_ioctl(struct device *dev, unsigned int cmd,
			     unsigned long arg)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct s35390a *s35390a = i2c_get_clientdata(client);
	char sts;
	int err;

	switch (cmd) {
	case RTC_VL_READ:
		/* s35390a_reset set lowvoltage flag and init RTC if needed */
		err = s35390a_read_status(s35390a, &sts);
		if (err < 0)
			return err;
		if (copy_to_user((void __user *)arg, &err, sizeof(int)))
			return -EFAULT;
		break;
	case RTC_VL_CLR:
		/* update flag and clear register */
		err = s35390a_init(s35390a);
		if (err < 0)
			return err;
		break;
	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static const struct rtc_class_ops s35390a_rtc_ops = {
	.read_time	= s35390a_rtc_read_time,
	.set_time	= s35390a_rtc_set_time,
	.set_alarm	= s35390a_rtc_set_alarm,
	.read_alarm	= s35390a_rtc_read_alarm,
	.ioctl          = s35390a_rtc_ioctl,
};

static struct i2c_driver s35390a_driver;

static int s35390a_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int err, err_read;
	unsigned int i;
	struct s35390a *s35390a;
	char buf, status1;
	struct device *dev = &client->dev;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	s35390a = devm_kzalloc(dev, sizeof(struct s35390a), GFP_KERNEL);
	if (!s35390a)
		return -ENOMEM;

	s35390a->client[0] = client;
	i2c_set_clientdata(client, s35390a);

	/* This chip uses multiple addresses, use dummy devices for them */
	for (i = 1; i < 8; ++i) {
		s35390a->client[i] = devm_i2c_new_dummy_device(dev,
							       client->adapter,
							       client->addr + i);
		if (IS_ERR(s35390a->client[i])) {
			dev_err(dev, "Address %02x unavailable\n",
				client->addr + i);
			return PTR_ERR(s35390a->client[i]);
		}
	}

	err_read = s35390a_read_status(s35390a, &status1);
	if (err_read < 0) {
		dev_err(dev, "error resetting chip\n");
		return err_read;
	}

	if (status1 & S35390A_FLAG_24H)
		s35390a->twentyfourhour = 1;
	else
		s35390a->twentyfourhour = 0;

	if (status1 & S35390A_FLAG_INT2) {
		/* disable alarm (and maybe test mode) */
		buf = 0;
		err = s35390a_set_reg(s35390a, S35390A_CMD_STATUS2, &buf, 1);
		if (err < 0) {
			dev_err(dev, "error disabling alarm");
			return err;
		}
	} else {
		err = s35390a_disable_test_mode(s35390a);
		if (err < 0) {
			dev_err(dev, "error disabling test mode\n");
			return err;
		}
	}

	device_set_wakeup_capable(dev, 1);

	s35390a->rtc = devm_rtc_device_register(dev, s35390a_driver.driver.name,
						&s35390a_rtc_ops, THIS_MODULE);

	if (IS_ERR(s35390a->rtc))
		return PTR_ERR(s35390a->rtc);

	/* supports per-minute alarms only, therefore set uie_unsupported */
	s35390a->rtc->uie_unsupported = 1;

	if (status1 & S35390A_FLAG_INT2)
		rtc_update_irq(s35390a->rtc, 1, RTC_AF);

	return 0;
}

static struct i2c_driver s35390a_driver = {
	.driver		= {
		.name	= "rtc-s35390a",
		.of_match_table = of_match_ptr(s35390a_of_match),
	},
	.probe		= s35390a_probe,
	.id_table	= s35390a_id,
};

module_i2c_driver(s35390a_driver);

MODULE_AUTHOR("Byron Bradley <byron.bbradley@gmail.com>");
MODULE_DESCRIPTION("S35390A RTC driver");
MODULE_LICENSE("GPL");
