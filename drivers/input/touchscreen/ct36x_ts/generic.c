
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#include "tscore.h"
#include "generic.h"


static struct i2c_device_id ct36x_ts_id[] = {
	{ DRIVER_NAME, 0 },
	{ }
};

struct i2c_driver ct36x_ts_driver  = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRIVER_NAME
	},
	.id_table	= ct36x_ts_id,
	.probe      = ct36x_ts_probe,
	.suspend	= ct36x_ts_suspend,
	.resume	    = ct36x_ts_resume,
	.remove 	= __devexit_p(ct36x_ts_remove),
};

void ct36x_ts_reg_read(struct i2c_client *client, unsigned short addr, char *buf, int len)
{
	struct i2c_msg msgs;

	msgs.addr = addr;
	msgs.flags = 0x01;  // 0x00: write 0x01:read 
	msgs.len = len;
	msgs.buf = buf;
	msgs.scl_rate = CT36X_TS_I2C_SPEED;

	i2c_transfer(client->adapter, &msgs, 1);
}

void ct36x_ts_reg_write(struct i2c_client *client, unsigned short addr, char *buf, int len)
{
	struct i2c_msg msgs;

	msgs.addr = addr;
	msgs.flags = 0x00;  // 0x00: write 0x01:read 
	msgs.len = len;
	msgs.buf = buf;
	msgs.scl_rate = CT36X_TS_I2C_SPEED;

	i2c_transfer(client->adapter, &msgs, 1);
}

void ct36x_platform_get_cfg(struct ct36x_ts_info *ct36x_ts)
{
	/* I2C config */
	//ct36x_ts->i2c_bus = CT36X_TS_I2C_BUS;
	//ct36x_ts->i2c_address =	CT36X_TS_I2C_ADDRESS;

	/* GPIO config */
	//ct36x_ts->rst = ct36x_ts->pdata->rst;
	//ct36x_ts->ss = ct36x_ts->pdata->ss;

	/* IRQ config*/
	ct36x_ts->irq = gpio_to_irq(ct36x_ts->ss);

	ct36x_ts->ready = 0;
}

int ct36x_platform_set_dev(struct ct36x_ts_info *ct36x_ts)
{
	return 0;
}

int ct36x_platform_get_resource(struct ct36x_ts_info *ct36x_ts)
{
	int err = -1;

	// Init Reset pin
	err = gpio_request(ct36x_ts->rst, "ct36x_ts_rst");
	if ( err ) {
		return -EIO;
	}
	gpio_direction_output(ct36x_ts->rst, 1);
	gpio_set_value(ct36x_ts->rst, 1);

	// Init Int pin
	err = gpio_request(ct36x_ts->ss, "ct36x_ts_int");
	if ( err ) {
		return -EIO;
	}
	gpio_direction_input(ct36x_ts->ss);

	return 0;
}

void ct36x_platform_put_resource(struct ct36x_ts_info *ct36x_ts)
{
	gpio_free(ct36x_ts->rst);
	gpio_free(ct36x_ts->ss);
}

void ct36x_platform_hw_reset(struct ct36x_ts_info *ct36x_ts)
{
	mdelay(500);
	gpio_set_value(ct36x_ts->rst, 0);
	mdelay(50);
	gpio_set_value(ct36x_ts->rst, 1);
	mdelay(500);
}
