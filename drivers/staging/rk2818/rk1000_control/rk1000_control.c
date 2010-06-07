#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/delay.h>

int reg_send_data(struct i2c_client *client, const char start_reg,
				const char *buf, int count, unsigned int scl_rate)
{
	int ret;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	char tx_buf[count + 1];
					    
	tx_buf[0] = start_reg;
	memcpy(tx_buf+1, buf, count); 
  
	msg.addr = client->addr;
	msg.buf = tx_buf;
	msg.len = count +1;
	msg.flags = client->flags;   
	msg.scl_rate = scl_rate;
												    
	ret = i2c_transfer(adap, &msg, 1);

	return ret;    
}
static int rk1000_control_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	/* reg[0x00] = 0x88, --> ADC_CON
	   reg[0x01] = 0x0d, --> CODEC_CON
	   reg[0x02] = 0x22, --> I2C_CON
	   reg[0x03] = 0x00, --> TVE_CON
	 */
	char data[4] = {0x88, 0x0d, 0x22, 0x00};
	char start_reg = 0x00;
	unsigned int scl_rate = 100 * 10000; /* 100kHz */
	
	#if (CONFIG_SND_SOC_RK1000)
    data[1] = 0x00;
    #endif
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		dev_err(&client->dev, "i2c bus does not support the rk1000_control\n");
		return -EIO;
	}
	mdelay(10);
	ret = reg_send_data(client, start_reg, data, 4, scl_rate);
	if (ret > 0)
		ret = 0;
	return ret;	
}

static int rk1000_control_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id rk1000_control_id[] = {
	{ "rk1000_control", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rk1000_control_id);

static struct i2c_driver rk1000_control_driver = {
	.driver = {
		.name = "rk1000_control",
	},
	.probe = rk1000_control_probe,
	.remove = rk1000_control_remove,
	.id_table = rk1000_control_id,
};

static int __init rk1000_control_init(void)
{
	return i2c_add_driver(&rk1000_control_driver);
}

static void __exit rk1000_control_exit(void)
{
	i2c_del_driver(&rk1000_control_driver);
}

module_init(rk1000_control_init);
module_exit(rk1000_control_exit);


MODULE_DESCRIPTION("RK1000 control driver");
MODULE_AUTHOR("Rock-chips, <www.rock-chips.com>");
MODULE_LICENSE("GPL");

