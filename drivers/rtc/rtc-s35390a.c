// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Seiko Instruments S-35390A RTC Driver
 *
 * Copyright (c) 2007 Byron Bradley
 */

#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/i2c.h>
#include <linux/bitrev.h>
#include <linux/bcd.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf-generic.h>

#define DRIVER_NAME "rtc-s35390a"

#define S35390A_CMD_STATUS1	0
#define S35390A_CMD_STATUS2	1
#define S35390A_CMD_TIME1	2
#define S35390A_CMD_TIME2	3
#define S35390A_CMD_INT1_REG1	4
#define S35390A_CMD_INT2_REG1	5
#define S35390A_CMD_FREE_REG    7

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
#define S35390A_FLAG_INT1	BIT(3)
#define S35390A_FLAG_24H	BIT(6)
#define S35390A_FLAG_RESET	BIT(7)

/* flag for STATUS2 */
#define S35390A_FLAG_TEST	BIT(0)

#define S35390A_INT_MODE_NOINTR	0x00

/* INT2 pin output mode */
#define S35390A_INT2_MODE_MASK		0x0E
#define S35390A_INT2_MODE_ALARM		BIT(1) /* INT2AE */
#define S35390A_INT2_MODE_PMIN_EDG	BIT(2) /* INT2ME */
#define S35390A_INT2_MODE_FREQ		BIT(3) /* INT2FE */
#define S35390A_INT2_MODE_PMIN1		(BIT(3) | BIT(2)) /* INT2FE | INT2ME */

/* INT1 pin output mode */
#define S35390A_INT1_MODE_MASK		0xF0
#define S35390A_INT1_MODE_ALARM		BIT(5) /* INT1AE */
#define S35390A_INT1_MODE_PMIN_EDG	BIT(6) /* INT1ME */
#define S35390A_INT1_MODE_FREQ		BIT(7) /* INT1FE */
#define S35390A_INT1_MODE_PMIN1		(BIT(7) | BIT(6)) /* INT1FE | INT1ME */
#define S35390A_INT1_MODE_PMIN2		(BIT(7) | BIT(6) | BIT(5)) /* INT1FE | INT1ME | INT1AE */
#define S35390A_INT1_MODE_32768KHZ	BIT(4) /* 32kE */

#define S35390A_FUNC_IGNORE		0x00
#define S35390A_FUNC_DISABLE		0x01
#define S35390A_FUNC_WAKEUP		0x02
#define S35390A_FUNC_CLOCK		0x03
#define S35390A_FUNC_PMIN1		0x04
#define S35390A_FUNC_PMIN2		0x05


static const struct i2c_device_id s35390a_id[] = {
	{ .name = "s35390a" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, s35390a_id);

static const __maybe_unused struct of_device_id s35390a_of_match[] = {
	{ .compatible = "sii,s35390a" },
	{ }
};
MODULE_DEVICE_TABLE(of, s35390a_of_match);

struct s35390a {
	struct i2c_client *client[8];
	struct rtc_device *rtc;

	struct mutex pinfunction_lock; /* lock preventing concurrent access of pin function */
	int pinfunction[2];
};

static int s35390a_set_reg(struct s35390a *s35390a, int reg, u8  *buf, int len)
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

static int s35390a_get_reg(struct s35390a *s35390a, int reg, u8 *buf, int len)
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

static int s35390a_init(struct s35390a *s35390a, u8 *sts)
{
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
	*sts = S35390A_FLAG_RESET | S35390A_FLAG_24H;
	ret = s35390a_set_reg(s35390a, S35390A_CMD_STATUS1, sts, 1);

	if (ret < 0)
		return ret;

	ret = s35390a_get_reg(s35390a, S35390A_CMD_STATUS1, sts, 1);
	if (ret < 0)
		return ret;

	if (*sts & (S35390A_FLAG_POC | S35390A_FLAG_BLD)) {
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

static char s35390a_hr2reg(int hour, bool twentyfourhour)
{
	if (twentyfourhour)
		return bin2bcd(hour);

	if (hour < 12)
		return bin2bcd(hour);

	return 0x40 | bin2bcd(hour - 12);
}

static int s35390a_reg2hr(char reg, bool twentyfourhour)
{
	unsigned hour;

	if (twentyfourhour)
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
	u8 buf[7], status;

	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d mday=%d, "
		"mon=%d, year=%d, wday=%d\n", __func__, tm->tm_sec,
		tm->tm_min, tm->tm_hour, tm->tm_mday, tm->tm_mon, tm->tm_year,
		tm->tm_wday);

	err = s35390a_read_status(s35390a, &status);
	if (err == 1)
		err = s35390a_init(s35390a, &status);

	if (err < 0)
		return err;

	buf[S35390A_BYTE_YEAR] = bin2bcd(tm->tm_year - 100);
	buf[S35390A_BYTE_MONTH] = bin2bcd(tm->tm_mon + 1);
	buf[S35390A_BYTE_DAY] = bin2bcd(tm->tm_mday);
	buf[S35390A_BYTE_WDAY] = bin2bcd(tm->tm_wday);
	buf[S35390A_BYTE_HOURS] = s35390a_hr2reg(tm->tm_hour, status & S35390A_FLAG_24H);
	buf[S35390A_BYTE_MINS] = bin2bcd(tm->tm_min);
	buf[S35390A_BYTE_SECS] = bin2bcd(tm->tm_sec);

	/* This chip expects the bits of each byte to be in reverse order */
	for (i = 0; i < 7; ++i)
		buf[i] = bitrev8(buf[i]);

	return s35390a_set_reg(s35390a, S35390A_CMD_TIME1, buf, sizeof(buf));
}

static int s35390a_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct s35390a *s35390a = i2c_get_clientdata(client);
	u8 buf[7], status;
	int i, err;

	err = s35390a_read_status(s35390a, &status);
	if (err == 1)
		return -EINVAL;
	if (err < 0)
		return err;

	err = s35390a_get_reg(s35390a, S35390A_CMD_TIME1, buf, sizeof(buf));
	if (err < 0)
		return err;

	/* This chip returns the bits of each byte in reverse order */
	for (i = 0; i < 7; ++i)
		buf[i] = bitrev8(buf[i]);

	tm->tm_sec = bcd2bin(buf[S35390A_BYTE_SECS]);
	tm->tm_min = bcd2bin(buf[S35390A_BYTE_MINS]);
	tm->tm_hour = s35390a_reg2hr(buf[S35390A_BYTE_HOURS], status & S35390A_FLAG_24H);
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

static int s35390a_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct s35390a *s35390a = dev_get_drvdata(dev);
	u8 sts;
	int err;

	guard(mutex)(&s35390a->pinfunction_lock);

	err = s35390a_get_reg(s35390a, S35390A_CMD_STATUS2, &sts, sizeof(sts));
	if (err < 0)
		return err;

	if (enabled) {
		if (s35390a->pinfunction[0] == S35390A_FUNC_WAKEUP)
			sts = (sts & ~S35390A_INT1_MODE_MASK) | S35390A_INT1_MODE_ALARM;

		if (s35390a->pinfunction[1] == S35390A_FUNC_WAKEUP)
			sts = (sts & ~S35390A_INT2_MODE_MASK) | S35390A_INT2_MODE_ALARM;
	} else {
		if (s35390a->pinfunction[0] == S35390A_FUNC_WAKEUP)
			sts = (sts & ~S35390A_INT1_MODE_MASK) | S35390A_INT_MODE_NOINTR;

		if (s35390a->pinfunction[1] == S35390A_FUNC_WAKEUP)
			sts = (sts & ~S35390A_INT2_MODE_MASK) | S35390A_INT_MODE_NOINTR;
	}

	err = s35390a_set_reg(s35390a, S35390A_CMD_STATUS2, &sts, sizeof(sts));
	if (err < 0)
		return err;

	return 0;
}

static int s35390a_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct s35390a *s35390a = i2c_get_clientdata(client);
	u8 buf[3], status1, status2 = 0;
	int err, i;

	dev_dbg(&client->dev, "%s: alm is secs=%d, mins=%d, hours=%d mday=%d, "\
		"mon=%d, year=%d, wday=%d\n", __func__, alm->time.tm_sec,
		alm->time.tm_min, alm->time.tm_hour, alm->time.tm_mday,
		alm->time.tm_mon, alm->time.tm_year, alm->time.tm_wday);

	guard(mutex)(&s35390a->pinfunction_lock);

	err = s35390a_get_reg(s35390a, S35390A_CMD_STATUS2, &status2, sizeof(status2));
	if (err < 0)
		return err;

	/* disable interrupt (which deasserts the irq line) */
	if (s35390a->pinfunction[0] == S35390A_FUNC_WAKEUP)
		status2 = (status2 & ~S35390A_INT1_MODE_MASK) | S35390A_INT_MODE_NOINTR;

	if (s35390a->pinfunction[1] == S35390A_FUNC_WAKEUP)
		status2 = (status2 & ~S35390A_INT2_MODE_MASK) | S35390A_INT_MODE_NOINTR;

	err = s35390a_set_reg(s35390a, S35390A_CMD_STATUS2, &status2, sizeof(status2));
	if (err < 0)
		return err;

	/* clear pending interrupt (in STATUS1 only), if any */
	err = s35390a_get_reg(s35390a, S35390A_CMD_STATUS1, &status1, sizeof(status1));
	if (err < 0)
		return err;

	if (alm->time.tm_wday != -1)
		buf[S35390A_ALRM_BYTE_WDAY] = bin2bcd(alm->time.tm_wday) | 0x80;
	else
		buf[S35390A_ALRM_BYTE_WDAY] = 0;

	buf[S35390A_ALRM_BYTE_HOURS] = s35390a_hr2reg(alm->time.tm_hour,
						      status1 & S35390A_FLAG_24H) | 0x80;
	buf[S35390A_ALRM_BYTE_MINS] = bin2bcd(alm->time.tm_min) | 0x80;

	if (alm->time.tm_hour >= 12)
		buf[S35390A_ALRM_BYTE_HOURS] |= 0x40;

	for (i = 0; i < 3; ++i)
		buf[i] = bitrev8(buf[i]);

	if (alm->enabled) {
		/* set interrupt mode */
		if (s35390a->pinfunction[0] == S35390A_FUNC_WAKEUP)
			status2 = (status2 & ~S35390A_INT1_MODE_MASK) | S35390A_INT1_MODE_ALARM;

		if (s35390a->pinfunction[1] == S35390A_FUNC_WAKEUP)
			status2 = (status2 & ~S35390A_INT2_MODE_MASK) | S35390A_INT2_MODE_ALARM;

		err = s35390a_set_reg(s35390a, S35390A_CMD_STATUS2, &status2, sizeof(status2));
		if (err < 0)
			return err;
	}

	if (s35390a->pinfunction[0] == S35390A_FUNC_WAKEUP) {
		err = s35390a_set_reg(s35390a, S35390A_CMD_INT1_REG1, buf, sizeof(buf));
		if (err < 0)
			return err;
	}

	if (s35390a->pinfunction[1] == S35390A_FUNC_WAKEUP) {
		err = s35390a_set_reg(s35390a, S35390A_CMD_INT2_REG1, buf, sizeof(buf));
		if (err < 0)
			return err;
	}

	return 0;
}

static int s35390a_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct s35390a *s35390a = i2c_get_clientdata(client);
	u8 buf[3], status1, status2;
	int i, err, reg;

	guard(mutex)(&s35390a->pinfunction_lock);

	err = s35390a_read_status(s35390a, &status1);
	if (err == 1)
		return -EINVAL;
	if (err < 0)
		return err;

	err = s35390a_get_reg(s35390a, S35390A_CMD_STATUS2, &status2, sizeof(status2));
	if (err < 0)
		return err;

	if (s35390a->pinfunction[1] == S35390A_FUNC_WAKEUP &&
	    (status2 & S35390A_INT2_MODE_MASK) == S35390A_INT2_MODE_ALARM) {
		reg = S35390A_CMD_INT2_REG1;
	} else if (s35390a->pinfunction[0] == S35390A_FUNC_WAKEUP &&
		   (status2 & S35390A_INT1_MODE_MASK) == S35390A_INT1_MODE_ALARM) {
		reg = S35390A_CMD_INT1_REG1;
	} else {
		/*
		 * When the alarm isn't enabled, the register to configure
		 * the alarm time isn't accessible.
		 */
		alm->enabled = 0;
		return 0;
	}

	alm->enabled = 1;

	err = s35390a_get_reg(s35390a, reg, buf, sizeof(buf));
	if (err < 0)
		return err;

	/* This chip returns the bits of each byte in reverse order */
	for (i = 0; i < 3; ++i)
		buf[i] = bitrev8(buf[i]);

	/*
	 * B0 of the three matching registers is an enable flag. If it is set
	 * the configured value is used for matching.
	 */
	if (buf[S35390A_ALRM_BYTE_WDAY] & 0x80)
		alm->time.tm_wday =
			bcd2bin(buf[S35390A_ALRM_BYTE_WDAY] & ~0x80);

	if (buf[S35390A_ALRM_BYTE_HOURS] & 0x80)
		alm->time.tm_hour =
			s35390a_reg2hr(buf[S35390A_ALRM_BYTE_HOURS] & ~0x80,
				       status1 & S35390A_FLAG_24H);

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
	u8 sts;
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
		err = s35390a_init(s35390a, &sts);
		if (err < 0)
			return err;
		break;
	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static const struct rtc_class_ops s35390a_rtc_ops = {
	.read_time		= s35390a_rtc_read_time,
	.set_time		= s35390a_rtc_set_time,
	.set_alarm		= s35390a_rtc_set_alarm,
	.read_alarm		= s35390a_rtc_read_alarm,
	.alarm_irq_enable	= s35390a_rtc_alarm_irq_enable,
	.ioctl			= s35390a_rtc_ioctl,
};

static int s35390a_nvmem_read(void *priv, unsigned int offset, void *val,
			      size_t bytes)
{
	struct s35390a *s35390a = priv;

	/* The offset is ignored because the NVMEM region is only 1 byte */
	return s35390a_get_reg(s35390a, S35390A_CMD_FREE_REG, val, bytes);
}

static int s35390a_nvmem_write(void *priv, unsigned int offset, void *val,
			       size_t bytes)
{
	struct s35390a *s35390a = priv;

	return s35390a_set_reg(s35390a, S35390A_CMD_FREE_REG, val, bytes);
}

static const struct pinctrl_pin_desc s35390a_pins_desc[] = {
	PINCTRL_PIN(0, "int1"),
	PINCTRL_PIN(1, "int2"),
};

static const unsigned int int1_pins[] = { 0 };
static const unsigned int int2_pins[] = { 1 };

static const struct pingroup s35390a_pin_groups[] = {
	PINCTRL_PINGROUP("int1_grp", int1_pins, ARRAY_SIZE(int1_pins)),
	PINCTRL_PINGROUP("int2_grp", int2_pins, ARRAY_SIZE(int2_pins)),
};

static int s35390a_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(s35390a_pin_groups);
}

static const char *s35390a_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						  unsigned int group)
{
	return s35390a_pin_groups[group].name;
}

static int s35390a_pinctrl_get_group_pins(struct pinctrl_dev *pctldev, unsigned int selector,
					  const unsigned int **pins, unsigned int *npins)
{
	*pins = s35390a_pin_groups[selector].pins;
	*npins = s35390a_pin_groups[selector].npins;
	return 0;
}

static const char * const all_groups[] = { "int1_grp", "int2_grp" };
static const char * const int1_groups[] = { "int1_grp" };

static const struct pinfunction s35390a_functions[] = {
	[S35390A_FUNC_IGNORE] = PINCTRL_PINFUNCTION("ignore", all_groups, ARRAY_SIZE(all_groups)),
	[S35390A_FUNC_DISABLE] = PINCTRL_PINFUNCTION("disable", all_groups, ARRAY_SIZE(all_groups)),
	[S35390A_FUNC_WAKEUP] = PINCTRL_PINFUNCTION("wakeup", all_groups, ARRAY_SIZE(all_groups)),
	[S35390A_FUNC_CLOCK] = PINCTRL_PINFUNCTION("clock", all_groups, ARRAY_SIZE(all_groups)),
	[S35390A_FUNC_PMIN1] = PINCTRL_PINFUNCTION("pmin1", all_groups, ARRAY_SIZE(all_groups)),
	[S35390A_FUNC_PMIN2] = PINCTRL_PINFUNCTION("pmin2", int1_groups, ARRAY_SIZE(int1_groups)),
};

static int s35390a_pinctrl_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(s35390a_functions);
}

static const char *s35390a_pinctrl_get_function_name(struct pinctrl_dev *pctldev,
						     unsigned int selector)
{
	return s35390a_functions[selector].name;
}

static int s35390a_pinctrl_get_function_groups(struct pinctrl_dev *pctldev, unsigned int selector,
					       const char * const **groups,
					       unsigned int * const ngroups)
{
	*groups = s35390a_functions[selector].groups;
	*ngroups = s35390a_functions[selector].ngroups;
	return 0;
}

static int s35390a_pinctrl_set_mux(struct pinctrl_dev *pctldev, unsigned int function,
				   unsigned int group)
{
	int err;
	u8 status2, flag, mask;
	struct s35390a *s35390a = pinctrl_dev_get_drvdata(pctldev);

	mask = group == 0 ? S35390A_INT1_MODE_MASK : S35390A_INT2_MODE_MASK;

	guard(mutex)(&s35390a->pinfunction_lock);

	dev_dbg(&s35390a->client[0]->dev, "%s: function=%d group=%d\n",
		__func__, function, group);

	if (function == s35390a->pinfunction[group])
		return 0;

	if (function != S35390A_FUNC_IGNORE) {
		err = s35390a_get_reg(s35390a, S35390A_CMD_STATUS2, &status2, 1);
		if (err < 0) {
			dev_err(&s35390a->client[0]->dev, "error reading status\n");
			return err;
		}

		switch (function) {
		case S35390A_FUNC_DISABLE:
		case S35390A_FUNC_CLOCK: /* not implemented */
			status2 = (status2 & ~mask) | S35390A_INT_MODE_NOINTR;
			break;
		case S35390A_FUNC_WAKEUP:
			flag = group == 0 ? S35390A_INT1_MODE_ALARM : S35390A_INT2_MODE_ALARM;

			if ((status2 & mask) != flag)
				status2 = (status2 & ~mask) | S35390A_INT_MODE_NOINTR;

			break;
		case S35390A_FUNC_PMIN1:
			flag = group == 0 ? S35390A_INT1_MODE_PMIN1 : S35390A_INT2_MODE_PMIN1;
			status2 = (status2 & ~mask) | flag;
			break;

		/* INT1 only modes */
		case S35390A_FUNC_PMIN2:
			if (group == 1)
				return -EINVAL;

			status2 = (status2 & ~mask) | S35390A_INT1_MODE_PMIN2;
			break;
		}

		err = s35390a_set_reg(s35390a, S35390A_CMD_STATUS2, &status2, 1);
		if (err < 0) {
			dev_err(&s35390a->client[0]->dev, "error setting interrupts\n");
			return err;
		}
	}

	s35390a->pinfunction[group] = function;

	return 0;
}

static const struct pinctrl_ops s35390a_pinctrl_ops = {
	.get_groups_count = s35390a_pinctrl_get_groups_count,
	.get_group_name = s35390a_pinctrl_get_group_name,
	.get_group_pins = s35390a_pinctrl_get_group_pins,
#if IS_ENABLED(CONFIG_OF)
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinconf_generic_dt_free_map
#endif
};

static const struct pinmux_ops s35390a_pinmux_ops = {
	.get_functions_count = s35390a_pinctrl_get_functions_count,
	.get_function_name = s35390a_pinctrl_get_function_name,
	.get_function_groups = s35390a_pinctrl_get_function_groups,
	.set_mux = s35390a_pinctrl_set_mux,
	.strict = true,
};

static struct pinctrl_desc s35390a_pinctrl_desc = {
	.name = DRIVER_NAME,
	.pins = s35390a_pins_desc,
	.npins = ARRAY_SIZE(s35390a_pins_desc),
	.pctlops = &s35390a_pinctrl_ops,
	.pmxops = &s35390a_pinmux_ops,
	.owner = THIS_MODULE,
};

static int s35390a_probe(struct i2c_client *client)
{
	int err;
	unsigned int i;
	struct s35390a *s35390a;
	struct rtc_device *rtc;
	struct pinctrl_dev *pctl;
	u8 status1, status2;
	bool irq = false;
	struct device *dev = &client->dev;
	struct nvmem_config nvmem_cfg = {
		.name = "s35390a_nvram",
		.type = NVMEM_TYPE_BATTERY_BACKED,
		.word_size = 1,
		.stride = 1,
		.size = 1,
		.reg_read = s35390a_nvmem_read,
		.reg_write = s35390a_nvmem_write,
	};
	int fallback[ARRAY_SIZE(s35390a_pin_groups)];

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	s35390a = devm_kzalloc(dev, sizeof(struct s35390a), GFP_KERNEL);
	if (!s35390a)
		return -ENOMEM;

	mutex_init(&s35390a->pinfunction_lock);
	memset(s35390a->pinfunction, -1, sizeof(s35390a->pinfunction));

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

	err = s35390a_read_status(s35390a, &status1);
	if (err < 0) {
		dev_err(dev, "error resetting chip\n");
		return err;
	}

	err = s35390a_get_reg(s35390a, S35390A_CMD_STATUS2, &status2, sizeof(status2));
	if (err < 0)
		return dev_err_probe(dev, err, "disabling alarm and test mode failed\n");

	if (status1 & S35390A_FLAG_INT1) {
		status2 = (status2 & ~S35390A_INT1_MODE_MASK) | S35390A_INT_MODE_NOINTR;
		irq = true;
	}

	if (status1 & S35390A_FLAG_INT2) {
		status2 = (status2 & ~S35390A_INT2_MODE_MASK) | S35390A_INT_MODE_NOINTR;
		irq = true;
	}

	status2 &= ~S35390A_FLAG_TEST;
	err = s35390a_set_reg(s35390a, S35390A_CMD_STATUS2, &status2, sizeof(status2));
	if (err < 0)
		return dev_err_probe(dev, err, "disabling alarm and test mode failed\n");

	rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	rtc->ops = &s35390a_rtc_ops;
	rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rtc->range_max = RTC_TIMESTAMP_END_2099;

	set_bit(RTC_FEATURE_ALARM_RES_MINUTE, rtc->features);
	clear_bit(RTC_FEATURE_UPDATE_INTERRUPT, rtc->features);

	s35390a->rtc = rtc;

	device_set_wakeup_capable(dev, 1);

	if (irq)
		rtc_update_irq(rtc, 1, RTC_AF);

	err = devm_pinctrl_register_and_init(dev, &s35390a_pinctrl_desc, s35390a, &pctl);
	if (err)
		return dev_err_probe(dev, err, "pinctrl register failed\n");

	err = pinctrl_enable(pctl);
	if (err)
		return dev_err_probe(dev, err, "pinctrl enable failed\n");

	/* If no pinmux function is defined in DT, fallback to previous behaviour */

	if (dmi_match(DMI_SYS_VENDOR, "Synology Inc.")) {
		/*
		 * Synology uses interrupt signal 1 for wakeup. Since pinctrl cannot be
		 * configured on ACPI, we check against the dmi sys vendor.
		 */
		fallback[0] = S35390A_FUNC_WAKEUP;
		fallback[1] = S35390A_FUNC_DISABLE;
	} else {
		fallback[0] = S35390A_FUNC_IGNORE;
		fallback[1] = S35390A_FUNC_WAKEUP;
	}

	for (i = 0; i < ARRAY_SIZE(s35390a_pin_groups); i++) {
		if (s35390a->pinfunction[i] == -1) {
			err = s35390a_pinctrl_set_mux(pctl, fallback[i], i);
			if (err)
				return err;
		}
	}

	nvmem_cfg.priv = s35390a;
	err = devm_rtc_nvmem_register(rtc, &nvmem_cfg);
	if (err)
		return err;

	return devm_rtc_register_device(rtc);
}

static struct i2c_driver s35390a_driver = {
	.driver		= {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(s35390a_of_match),
	},
	.probe		= s35390a_probe,
	.id_table	= s35390a_id,
};

module_i2c_driver(s35390a_driver);

MODULE_AUTHOR("Byron Bradley <byron.bbradley@gmail.com>");
MODULE_DESCRIPTION("S35390A RTC driver");
MODULE_LICENSE("GPL");
