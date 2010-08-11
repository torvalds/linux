/*drivers/rtc/rtc-s35392a.h - driver for s35392a
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
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

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/i2c.h>
#include <linux/bitrev.h>
#include <linux/bcd.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "rtc-s35392a.h"


#define RTC_RATE	100 * 1000
#define S35392_TEST 1

struct s35392a {
	struct i2c_client *client;
	struct rtc_device *rtc;
	int twentyfourhour;
};

static int s35392a_set_reg(struct s35392a *s35392a, const char reg, char *buf, int len)
{
	struct i2c_client *client = s35392a->client;
	struct i2c_msg msg;
	int ret;
	
	char *buff = buf;
	msg.addr = client->addr | reg;
	msg.flags = client->flags;
	msg.len = len;
	msg.buf = buff;
	msg.scl_rate = RTC_RATE;
	
	ret = i2c_transfer(client->adapter,&msg,1);
	return ret;	
	
}

static int s35392a_get_reg(struct s35392a *s35392a, const char reg, char *buf, int len)
{
	struct i2c_client *client = s35392a->client;
	struct i2c_msg msg;
	int ret;

	msg.addr = client->addr | reg;
	msg.flags = client->flags | I2C_M_RD;
	msg.len = len;
	msg.buf = buf;
	msg.scl_rate = RTC_RATE;

	ret = i2c_transfer(client->adapter,&msg,1);
	return ret;
	
}

#if S35392_TEST
static int s35392_set_reg(struct s35392a *s35392a, const char reg, char *buf, int len,unsigned char head)
{
	struct i2c_client *client = s35392a->client;
	struct i2c_msg msg;
	int ret;
	
	char *buff = buf;
	msg.addr = client->addr | reg | (head << 3);
	msg.flags = client->flags;
	msg.len = len;
	msg.buf = buff;
	msg.scl_rate = RTC_RATE;	
	ret = i2c_transfer(client->adapter,&msg,1);
	return ret;	
	
}

static int s35392_get_reg(struct s35392a *s35392a, const char reg, char *buf, int len,unsigned char head)
{
	struct i2c_client *client = s35392a->client;
	struct i2c_msg msg;
	int ret;

	msg.addr = client->addr | reg |(head << 3);
	msg.flags = client->flags | I2C_M_RD;
	msg.len = len;
	msg.buf = buf;
	msg.scl_rate = RTC_RATE;

	ret = i2c_transfer(client->adapter,&msg,1);
	return ret;
	
}
static int s35392a_test(struct s35392a *s35392a)
{
	char buf[1];
	char i;
	
	i = 50;
	while(i--)
	{
	if (s35392_get_reg(s35392a, S35392A_CMD_STATUS1, buf, sizeof(buf),6) < 0)
		return -EIO;	
	if (!(buf[0] & (S35392A_FLAG_POC | S35392A_FLAG_BLD)))
		return 0;
	
	buf[0] |= (S35392A_FLAG_RESET | S35392A_FLAG_24H);
	buf[0] &= 0xf0;
	s35392_set_reg(s35392a, S35392A_CMD_STATUS1, buf, sizeof(buf),6);
	
	buf[0] = 0;
	s35392_get_reg(s35392a, S35392A_CMD_STATUS1, buf, sizeof(buf),6);
	mdelay(10);	
	}
	return 0;
}
#endif
static int s35392a_init(struct s35392a *s35392a)
{
	char buf[1];

	s35392a_get_reg(s35392a, S35392A_CMD_STATUS1, buf, sizeof(buf));	
	s35392a_get_reg(s35392a, S35392A_CMD_STATUS2, buf, sizeof(buf));	
	s35392a_get_reg(s35392a, 4, buf, sizeof(buf));
	s35392a_get_reg(s35392a, 5, buf, sizeof(buf));
	s35392a_get_reg(s35392a, 6, buf, sizeof(buf));
	s35392a_get_reg(s35392a, 7, buf, sizeof(buf));
	
	buf[0] |= (S35392A_FLAG_RESET | S35392A_FLAG_24H);
	buf[0] &= 0xf0;
	return s35392a_set_reg(s35392a, S35392A_CMD_STATUS1, buf, sizeof(buf));

}

static int s35392a_reset(struct s35392a *s35392a)
{
	char buf[1];

	if (s35392a_get_reg(s35392a, S35392A_CMD_STATUS1, buf, sizeof(buf)) < 0)
		return -EIO;	
	if (!(buf[0] & (S35392A_FLAG_POC | S35392A_FLAG_BLD)))
		return 0;

	buf[0] |= (S35392A_FLAG_RESET | S35392A_FLAG_24H);
	buf[0] &= 0xf0;
	return s35392a_set_reg(s35392a, S35392A_CMD_STATUS1, buf, sizeof(buf));
}


static int s35392a_disable_test_mode(struct s35392a *s35392a)
{
	char buf[1];

	if (s35392a_get_reg(s35392a, S35392A_CMD_STATUS2, buf, sizeof(buf)) < 0)
		return -EIO;

	if (!(buf[0] & S35392A_FLAG_TEST))
		return 0;

	buf[0] &= ~S35392A_FLAG_TEST;
	return s35392a_set_reg(s35392a, S35392A_CMD_STATUS2, buf, sizeof(buf));
}

static char s35392a_hr2reg(struct s35392a *s35392a, int hour)
{
	if (s35392a->twentyfourhour)
		return bin2bcd(hour);

	if (hour < 12)
		return bin2bcd(hour);

	return 0x40 | bin2bcd(hour - 12);
}

static int s35392a_reg2hr(struct s35392a *s35392a, char reg)
{
	unsigned hour;

	if (s35392a->twentyfourhour)
		return bcd2bin(reg & 0x3f);

	hour = bcd2bin(reg & 0x3f);
	if (reg & 0x40)
		hour += 12;

	return hour;
}

static int s35392a_set_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct s35392a	*s35392a = i2c_get_clientdata(client);
	int i, err;
	char buf[7];

	printk("%s: tm is secs=%d, mins=%d, hours=%d mday=%d, "
		"mon=%d, year=%d, wday=%d\n", __func__, tm->tm_sec,
		tm->tm_min, tm->tm_hour, tm->tm_mday, tm->tm_mon, tm->tm_year,
		tm->tm_wday);

	buf[S35392A_BYTE_YEAR] = bin2bcd(tm->tm_year - 100);
	buf[S35392A_BYTE_MONTH] = bin2bcd(tm->tm_mon + 1);
	buf[S35392A_BYTE_DAY] = bin2bcd(tm->tm_mday);
	buf[S35392A_BYTE_WDAY] = bin2bcd(tm->tm_wday);
	buf[S35392A_BYTE_HOURS] = s35392a_hr2reg(s35392a, tm->tm_hour);
	buf[S35392A_BYTE_MINS] = bin2bcd(tm->tm_min);
	buf[S35392A_BYTE_SECS] = bin2bcd(tm->tm_sec);

	/* This chip expects the bits of each byte to be in reverse order */
	for (i = 0; i < 7; ++i)
		buf[i] = bitrev8(buf[i]);

	err = s35392a_set_reg(s35392a, S35392A_CMD_TIME1, buf, sizeof(buf));

	return err;
}

static int s35392a_get_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct s35392a *s35392a = i2c_get_clientdata(client);
	char buf[7];
	int i, err;

	err = s35392a_get_reg(s35392a, S35392A_CMD_TIME1, buf, sizeof(buf));
	if (err < 0)
		return err;

	/* This chip returns the bits of each byte in reverse order */
	for (i = 0; i < 7; ++i)
		buf[i] = bitrev8(buf[i]);

	tm->tm_sec = bcd2bin(buf[S35392A_BYTE_SECS]);
	tm->tm_min = bcd2bin(buf[S35392A_BYTE_MINS]);
	tm->tm_hour = s35392a_reg2hr(s35392a, buf[S35392A_BYTE_HOURS]);
	tm->tm_wday = bcd2bin(buf[S35392A_BYTE_WDAY]);
	tm->tm_mday = bcd2bin(buf[S35392A_BYTE_DAY]);
	tm->tm_mon = bcd2bin(buf[S35392A_BYTE_MONTH]) - 1;
	tm->tm_year = bcd2bin(buf[S35392A_BYTE_YEAR]) + 100;
	//tm->tm_yday = rtc_year_days(tm->tm_mday, tm->tm_mon, tm->tm_year);	

	printk( "%s: tm is secs=%d, mins=%d, hours=%d, mday=%d, "
		"mon=%d, year=%d, wday=%d\n", __func__, tm->tm_sec,
		tm->tm_min, tm->tm_hour, tm->tm_mday, tm->tm_mon, tm->tm_year,
		tm->tm_wday);

	return rtc_valid_tm(tm);
}
static int s35392a_i2c_read_alarm(struct i2c_client *client, struct rtc_wkalrm const *tm)
{
	struct s35392a *s35392a = i2c_get_clientdata(client);
	
}
static int s35392a_i2c_set_alarm(struct i2c_client *client, struct rtc_wkalrm const *tm)
{
	struct s35392a *s35392a = i2c_get_clientdata(client);
	
}
static int s35392a_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return s35392a_get_datetime(to_i2c_client(dev), tm);
}

static int s35392a_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return s35392a_set_datetime(to_i2c_client(dev), tm);
}
static int s35392a_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *tm)
{
	return s35392a_i2c_read_alarm(to_i2c_client(dev),tm);
}
static int s35392a_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *tm)
{	
	return s35392a_i2c_set_alarm(to_i2c_client(dev),tm);	
}

static int s35392a_i2c_open_alarm(struct i2c_client *client )	
{
	u8 data;
	struct s35392a *s35392a = i2c_get_clientdata(client);
	
	s35392a_get_reg( s35392a, S35392A_CMD_STATUS2, &data, 1);
	data = (data |S35392A_FLAG_INT1AE) & 0x3F;
	s35392a_set_reg( s35392a, S35392A_CMD_STATUS2, &data, 1);

	return 0;
}
static int s35392a_i2c_close_alarm(struct i2c_client *client )
{
	u8 data;
	struct s35392a *s35392a = i2c_get_clientdata(client);
	
	s35392a_get_reg( s35392a, S35392A_CMD_STATUS2, &data, 1);
	data &=  ~S35392A_FLAG_INT1AE;
	s35392a_set_reg( s35392a, S35392A_CMD_STATUS2, &data, 1);

	return 0;
}

static int s35392a_rtc_ioctl(struct device *dev,unsigned int cmd,unsigned long arg)
{
	struct i2c_client *client = to_i2c_client(dev);
	
	switch(cmd)
	{
		case RTC_AIE_OFF:
			if(s35392a_i2c_close_alarm(client) < 0)
				goto err;
			break;
		case RTC_AIE_ON:
			if(s35392a_i2c_open_alarm(client) < 0)
				goto err;
			break;
		default:
			return -ENOIOCTLCMD;
	}
	return 0;
err:
	return -EIO;
}
static int  s35392_rtc_proc(struct device *dev, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static const struct rtc_class_ops s35392a_rtc_ops = {
	.read_time	= s35392a_rtc_read_time,
	.set_time	       = s35392a_rtc_set_time,
	.read_alarm    = s35392a_rtc_read_alarm,
	.set_alarm      = s35392a_rtc_set_alarm,
	.ioctl               = s35392a_rtc_ioctl,
	.proc               = s35392_rtc_proc
};

static struct i2c_driver s35392a_driver;

static int s35392a_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int err;
	unsigned int i;
	struct s35392a *s35392a;
	struct rtc_time tm;
	char buf[1];
	printk("@@@@@%s:%d@@@@@\n",__FUNCTION__,__LINE__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit;
	}

	s35392a = kzalloc(sizeof(struct s35392a), GFP_KERNEL);
	if (!s35392a) {
		err = -ENOMEM;
		goto exit;
	}

	s35392a->client = client;
	i2c_set_clientdata(client, s35392a);
	mdelay(500);
	//s35392a_init(s35392a);
	err = s35392a_reset(s35392a);	
	if (err < 0) {
		dev_err(&client->dev, "error resetting chip\n");
		goto exit_dummy;
	}
	
	err = s35392a_disable_test_mode(s35392a);
	if (err < 0) {
		dev_err(&client->dev, "error disabling test mode\n");
		goto exit_dummy;
	}

	err = s35392a_get_reg(s35392a, S35392A_CMD_STATUS1, buf, sizeof(buf));
	if (err < 0) {
		dev_err(&client->dev, "error checking 12/24 hour mode\n");
		goto exit_dummy;
	}
	if (buf[0] & S35392A_FLAG_24H)
		s35392a->twentyfourhour = 1;
	else
		s35392a->twentyfourhour = 0;

	if (s35392a_get_datetime(client, &tm) < 0)
		dev_warn(&client->dev, "clock needs to be set\n");

	s35392a->rtc = rtc_device_register(s35392a_driver.driver.name,
				&client->dev, &s35392a_rtc_ops, THIS_MODULE);

	if (IS_ERR(s35392a->rtc)) {
		err = PTR_ERR(s35392a->rtc);
		goto exit_dummy;
	}
	printk("@@@@@%s:%d@@@@@\n",__FUNCTION__,__LINE__);
	return 0;

exit_dummy:
	rtc_device_unregister(s35392a->rtc);
	kfree(s35392a);
	i2c_set_clientdata(client, NULL);

exit:
	return err;
}

static int s35392a_remove(struct i2c_client *client)
{
	unsigned int i;

	struct s35392a *s35392a = i2c_get_clientdata(client);

		if (s35392a->client)
			i2c_unregister_device(s35392a->client);

	rtc_device_unregister(s35392a->rtc);
	kfree(s35392a);
	i2c_set_clientdata(client, NULL);

	return 0;
}

static const struct i2c_device_id s35392a_id[] = {
	{ "rtc-s35392a", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, s35392a_id);

static struct i2c_driver s35392a_driver = {
	.driver		= {
		.name	= "rtc-s35392a",
	},
	.probe		= s35392a_probe,
	.remove		= s35392a_remove,
	.id_table	= s35392a_id,
};

static int __init s35392a_rtc_init(void)
{
	printk("@@@@@%s:%d@@@@@\n",__FUNCTION__,__LINE__);
	return i2c_add_driver(&s35392a_driver);
}

static void __exit s35392a_rtc_exit(void)
{
	i2c_del_driver(&s35392a_driver);
}

MODULE_AUTHOR("swj@rock-chips.com>");
MODULE_DESCRIPTION("S35392A RTC driver");
MODULE_LICENSE("GPL");

module_init(s35392a_rtc_init);
module_exit(s35392a_rtc_exit);

