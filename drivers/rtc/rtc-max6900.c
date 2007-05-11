/*
 * rtc class driver for the Maxim MAX6900 chip
 *
 * Author: Dale Farnsworth <dale@farnsworth.org>
 *
 * based on previously existing rtc class drivers
 *
 * 2007 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/delay.h>

#define DRV_NAME "max6900"
#define DRV_VERSION "0.1"

/*
 * register indices
 */
#define MAX6900_REG_SC			0	/* seconds	00-59 */
#define MAX6900_REG_MN			1	/* minutes	00-59 */
#define MAX6900_REG_HR			2	/* hours	00-23 */
#define MAX6900_REG_DT			3	/* day of month	00-31 */
#define MAX6900_REG_MO			4	/* month	01-12 */
#define MAX6900_REG_DW			5	/* day of week	 1-7  */
#define MAX6900_REG_YR			6	/* year		00-99 */
#define MAX6900_REG_CT			7	/* control */
#define MAX6900_REG_LEN			8

#define MAX6900_REG_CT_WP		(1 << 7)	/* Write Protect */

/*
 * register read/write commands
 */
#define MAX6900_REG_CONTROL_WRITE	0x8e
#define MAX6900_REG_BURST_READ		0xbf
#define MAX6900_REG_BURST_WRITE		0xbe
#define MAX6900_REG_RESERVED_READ	0x96

#define MAX6900_IDLE_TIME_AFTER_WRITE	3	/* specification says 2.5 mS */

#define MAX6900_I2C_ADDR		0xa0

static unsigned short normal_i2c[] = {
	MAX6900_I2C_ADDR >> 1,
	I2C_CLIENT_END
};

I2C_CLIENT_INSMOD;			/* defines addr_data */

static int max6900_probe(struct i2c_adapter *adapter, int addr, int kind);

static int max6900_i2c_read_regs(struct i2c_client *client, u8 *buf)
{
	u8 reg_addr[1] = { MAX6900_REG_BURST_READ };
	struct i2c_msg msgs[2] = {
		{
			.addr	= client->addr,
			.flags	= 0, /* write */
			.len	= sizeof(reg_addr),
			.buf	= reg_addr
		},
		{
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= MAX6900_REG_LEN,
			.buf	= buf
		}
	};
	int rc;

	rc = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (rc != ARRAY_SIZE(msgs)) {
		dev_err(&client->dev, "%s: register read failed\n",
			__FUNCTION__);
		return -EIO;
	}
	return 0;
}

static int max6900_i2c_write_regs(struct i2c_client *client, u8 const *buf)
{
	u8 i2c_buf[MAX6900_REG_LEN + 1] = { MAX6900_REG_BURST_WRITE };
	struct i2c_msg msgs[1] = {
		{
			.addr	= client->addr,
			.flags	= 0, /* write */
			.len	= MAX6900_REG_LEN + 1,
			.buf	= i2c_buf
		}
	};
	int rc;

	memcpy(&i2c_buf[1], buf, MAX6900_REG_LEN);

	rc = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (rc != ARRAY_SIZE(msgs)) {
		dev_err(&client->dev, "%s: register write failed\n",
			__FUNCTION__);
		return -EIO;
	}
	msleep(MAX6900_IDLE_TIME_AFTER_WRITE);
	return 0;
}

static int max6900_i2c_validate_client(struct i2c_client *client)
{
	u8 regs[MAX6900_REG_LEN];
	u8 zero_mask[MAX6900_REG_LEN] = {
		0x80,	/* seconds */
		0x80,	/* minutes */
		0x40,	/* hours */
		0xc0,	/* day of month */
		0xe0,	/* month */
		0xf8,	/* day of week */
		0x00,	/* year */
		0x7f,	/* control */
	};
	int i;
	int rc;
	int reserved;

	reserved = i2c_smbus_read_byte_data(client, MAX6900_REG_RESERVED_READ);
	if (reserved != 0x07)
		return -ENODEV;

	rc = max6900_i2c_read_regs(client, regs);
	if (rc < 0)
		return rc;

	for (i = 0; i < MAX6900_REG_LEN; ++i) {
		if (regs[i] & zero_mask[i])
			return -ENODEV;
	}

	return 0;
}

static int max6900_i2c_read_time(struct i2c_client *client, struct rtc_time *tm)
{
	int rc;
	u8 regs[MAX6900_REG_LEN];

	rc = max6900_i2c_read_regs(client, regs);
	if (rc < 0)
		return rc;

	tm->tm_sec = BCD2BIN(regs[MAX6900_REG_SC]);
	tm->tm_min = BCD2BIN(regs[MAX6900_REG_MN]);
	tm->tm_hour = BCD2BIN(regs[MAX6900_REG_HR] & 0x3f);
	tm->tm_mday = BCD2BIN(regs[MAX6900_REG_DT]);
	tm->tm_mon = BCD2BIN(regs[MAX6900_REG_MO]) - 1;
	tm->tm_year = BCD2BIN(regs[MAX6900_REG_YR]) + 100;
	tm->tm_wday = BCD2BIN(regs[MAX6900_REG_DW]);

	return 0;
}

static int max6900_i2c_clear_write_protect(struct i2c_client *client)
{
	int rc;
	rc = i2c_smbus_write_byte_data (client, MAX6900_REG_CONTROL_WRITE, 0);
	if (rc < 0) {
		dev_err(&client->dev, "%s: control register write failed\n",
			__FUNCTION__);
		return -EIO;
	}
	return 0;
}

static int max6900_i2c_set_time(struct i2c_client *client,
				struct rtc_time const *tm)
{
	u8 regs[MAX6900_REG_LEN];
	int rc;

	rc = max6900_i2c_clear_write_protect(client);
	if (rc < 0)
		return rc;

	regs[MAX6900_REG_SC] = BIN2BCD(tm->tm_sec);
	regs[MAX6900_REG_MN] = BIN2BCD(tm->tm_min);
	regs[MAX6900_REG_HR] = BIN2BCD(tm->tm_hour);
	regs[MAX6900_REG_DT] = BIN2BCD(tm->tm_mday);
	regs[MAX6900_REG_MO] = BIN2BCD(tm->tm_mon + 1);
	regs[MAX6900_REG_YR] = BIN2BCD(tm->tm_year - 100);
	regs[MAX6900_REG_DW] = BIN2BCD(tm->tm_wday);
	regs[MAX6900_REG_CT] = MAX6900_REG_CT_WP;	/* set write protect */

	rc = max6900_i2c_write_regs(client, regs);
	if (rc < 0)
		return rc;

	return 0;
}

static int max6900_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return max6900_i2c_read_time(to_i2c_client(dev), tm);
}

static int max6900_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return max6900_i2c_set_time(to_i2c_client(dev), tm);
}

static int max6900_attach_adapter(struct i2c_adapter *adapter)
{
	return i2c_probe(adapter, &addr_data, max6900_probe);
}

static int max6900_detach_client(struct i2c_client *client)
{
	struct rtc_device *const rtc = i2c_get_clientdata(client);

	if (rtc)
		rtc_device_unregister(rtc);

	return i2c_detach_client(client);
}

static struct i2c_driver max6900_driver = {
	.driver		= {
		.name	= DRV_NAME,
	},
	.id		= I2C_DRIVERID_MAX6900,
	.attach_adapter = max6900_attach_adapter,
	.detach_client	= max6900_detach_client,
};

static const struct rtc_class_ops max6900_rtc_ops = {
	.read_time	= max6900_rtc_read_time,
	.set_time	= max6900_rtc_set_time,
};

static int max6900_probe(struct i2c_adapter *adapter, int addr, int kind)
{
	int rc = 0;
	struct i2c_client *client = NULL;
	struct rtc_device *rtc = NULL;

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		rc = -ENODEV;
		goto failout;
	}

	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == NULL) {
		rc = -ENOMEM;
		goto failout;
	}

	client->addr = addr;
	client->adapter = adapter;
	client->driver = &max6900_driver;
	strlcpy(client->name, DRV_NAME, I2C_NAME_SIZE);

	if (kind < 0) {
		rc = max6900_i2c_validate_client(client);
		if (rc < 0)
			goto failout;
	}

	rc = i2c_attach_client(client);
	if (rc < 0)
		goto failout;

	dev_info(&client->dev,
		 "chip found, driver version " DRV_VERSION "\n");

	rtc = rtc_device_register(max6900_driver.driver.name,
				  &client->dev,
				  &max6900_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		rc = PTR_ERR(rtc);
		goto failout_detach;
	}

	i2c_set_clientdata(client, rtc);

	return 0;

failout_detach:
	i2c_detach_client(client);
failout:
	kfree(client);
	return rc;
}

static int __init max6900_init(void)
{
	return i2c_add_driver(&max6900_driver);
}

static void __exit max6900_exit(void)
{
	i2c_del_driver(&max6900_driver);
}

MODULE_DESCRIPTION("Maxim MAX6900 RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

module_init(max6900_init);
module_exit(max6900_exit);
