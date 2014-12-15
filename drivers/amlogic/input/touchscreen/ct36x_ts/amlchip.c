
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/module.h>

#include "tscore.h"
#include "amlchip.h"



static struct i2c_device_id ct36x_ts_id[] = {
	{ DRIVER_NAME, 0 },
	{ }
};

//static struct i2c_board_info i2c_board_info[] = {
//	{
//		I2C_BOARD_INFO(DRIVER_NAME, 0x01),
//		.platform_data = NULL,
//	},
//};

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
	.remove 	= ct36x_ts_remove,
};

void ct36x_ts_reg_read(struct i2c_client *client, unsigned short addr, char *buf, int len)
{
	struct i2c_msg msgs;

	msgs.addr = addr;
	msgs.flags = 0x01;  // 0x00: write 0x01:read 
	msgs.len = len;
	msgs.buf = buf;
	//msgs.scl_rate = CT36X_TS_I2C_SPEED;

	i2c_transfer(client->adapter, &msgs, 1);
}

void ct36x_ts_reg_write(struct i2c_client *client, unsigned short addr, char *buf, int len)
{
	struct i2c_msg msgs;

	msgs.addr = addr;
	msgs.flags = 0x00;  // 0x00: write 0x01:read 
	msgs.len = len;
	msgs.buf = buf;
	//msgs.scl_rate = CT36X_TS_I2C_SPEED;

	i2c_transfer(client->adapter, &msgs, 1);
}

 void ct36x_platform_get_cfg(struct ct36x_ts_info *ct36x_ts)
{
	#ifdef CONFIG_OF
	ct36x_ts->i2c_bus = ts_com->bus_type;
	ct36x_ts->i2c_address =	ts_com->reg;

	/* GPIO config */
	ct36x_ts->rst = ts_com->gpio_reset;
	ct36x_ts->ss = ts_com->gpio_interrupt;

	/* IRQ config*/
	ct36x_ts->irq = ts_com->irq;
	#endif

	ct36x_ts->state = CT36X_STATE_INIT;
}

int ct36x_platform_set_dev(struct ct36x_ts_info *ct36x_ts)
{
	return 0;
}

int ct36x_platform_get_resource(struct ct36x_ts_info *ct36x_ts)
{
//	int err = -1;
	#ifdef CONFIG_OF
	if (request_touch_gpio(ts_com) != ERR_NO)
	 return -EIO;
	aml_gpio_direction_output(ct36x_ts->rst, 1);
	//aml_gpio_direction_input(ct36x_ts->ss);
	//aml_gpio_to_irq(ct36x_ts->ss, ct36x_ts->irq-INT_GPIO_0, ts_com->irq_edge);
	#endif

	return 0;
}

void ct36x_platform_put_resource(struct ct36x_ts_info *ct36x_ts)
{
	#ifdef CONFIG_OF
	free_touch_gpio(ts_com);
	#endif
}
int ct36x_test_read(struct i2c_client *client, unsigned short addr, char *buf, int len)
{
	struct i2c_msg msgs;

	msgs.addr = addr;
	msgs.flags = 0x01;  // 0x00: write 0x01:read
	msgs.len = len;
	msgs.buf = buf;
	//msgs.scl_rate = CT36X_TS_I2C_SPEED;

	return i2c_transfer(client->adapter, &msgs, 1);
}
#ifdef CONFIG_OF
void ct36x_platform_hw_reset(struct ct36x_ts_info *ct36x_ts)
{
	//mdelay(500);
	aml_gpio_direction_output(ct36x_ts->rst, 0);
	msleep(20);
	aml_gpio_direction_output(ct36x_ts->rst, 1);
	msleep(200);
}
void ct36x_hw_reset(struct touch_pdata *pdata)
{
	//mdelay(500);
	aml_gpio_direction_output(pdata->gpio_reset, 0);
	mdelay(50);
	aml_gpio_direction_output(pdata->gpio_reset, 1);
	mdelay(200);
}
#else
void ct36x_platform_hw_reset(struct ct36x_platform_data *pdata)
{
	if (pdata->shutdown) {
		pdata->shutdown(1);
		mdelay(500);
		pdata->shutdown(0);
		//gpio_set_value(ct36x_ts->rst, 0);
		mdelay(50);
		pdata->shutdown(1);
		//gpio_set_value(ct36x_ts->rst, 1);
		mdelay(500);
	}
}
#endif