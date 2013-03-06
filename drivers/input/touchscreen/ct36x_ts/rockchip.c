
#include <linux/i2c.h>
#include <linux/delay.h>

#include "tscore.h"
#include "rockchip.h"



static struct i2c_device_id ct36x_ts_id[] = {
	{ DRIVER_NAME, 0 },
	{ }
};

static struct i2c_board_info i2c_board_info[] = {
	{
		I2C_BOARD_INFO(DRIVER_NAME, 0x01),
		.platform_data = NULL,
	},
};

struct i2c_driver ct36x_ts_driver  = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRIVER_NAME
	},
	.id_table	= ct36x_ts_id,
	.probe      = ct36x_ts_probe,
	.shutdown	= ct36x_ts_shutdown,
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
	ct36x_ts->i2c_bus = CT36X_TS_I2C_BUS;
	ct36x_ts->i2c_address =	CT36X_TS_I2C_ADDRESS;

	/* GPIO config */
	ct36x_ts->rst = CT36X_TS_RST_PIN;
	ct36x_ts->ss = CT36X_TS_IRQ_PIN;

	/* IRQ config*/
	ct36x_ts->irq = gpio_to_irq(ct36x_ts->ss);

	ct36x_ts->ready = 0;
}

int ct36x_platform_set_dev(struct ct36x_ts_info *ct36x_ts)
{
	struct i2c_adapter *adapter;
	struct i2c_client *client;

	adapter = i2c_get_adapter(ct36x_ts->i2c_bus);
	if ( !adapter ) {
		printk("Unable to get i2c adapter on bus %d.\n", ct36x_ts->i2c_bus);
		return -ENODEV;
	}

	client = i2c_new_device(adapter, i2c_board_info);
	i2c_put_adapter(adapter);
	if (!client) {
		printk("Unable to create i2c device on bus %d.\n", ct36x_ts->i2c_bus);
		return -ENODEV;
	}

	ct36x_ts->client = client;
	i2c_set_clientdata(client, ct36x_ts);

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
