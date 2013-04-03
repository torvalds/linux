
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mfd/core.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/mfd/rk616.h>


static struct mfd_cell rk616_devs[] = {
	{
		.name = "rk616-lvds",
		.id = 0,
	},
	{
		.name = "rk616-codec",
		.id = 1,
	},
	{
		.name = "rk616-hdmi",
		.id = 2,
	},
	{
		.name = "rk616-mipi",
		.id = 3,
	},
};

static int rk616_i2c_read_reg(struct mfd_rk616 *rk616, u16 reg,u32 *pval)
{
	struct i2c_client * client = rk616->client;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msgs[2];
	int ret;
	char reg_buf[2];
	
	memcpy(reg_buf, &reg, 2);

	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags;
	msgs[0].len = 2;
	msgs[0].buf = reg_buf;
	msgs[0].scl_rate = rk616->pdata->scl_rate;
	msgs[0].udelay = client->udelay;

	msgs[1].addr = client->addr;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].len = 4;
	msgs[1].buf = (char *)pval;
	msgs[1].scl_rate = rk616->pdata->scl_rate;
	msgs[1].udelay = client->udelay;

	ret = i2c_transfer(adap, msgs, 2);

	
	return (ret == 2)? 4 : ret;

}

static int rk616_i2c_write_reg(struct mfd_rk616 *rk616, u16 reg,u32 *pval)
{
	struct i2c_client *client = rk616->client;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	int ret;
	char *tx_buf = (char *)kmalloc(6, GFP_KERNEL);
	if(!tx_buf)
		return -ENOMEM;
	
	memcpy(tx_buf, &reg, 2); 
	memcpy(tx_buf+2, (char *)pval, 4); 

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = 6;
	msg.buf = (char *)tx_buf;
	msg.scl_rate = rk616->pdata->scl_rate;
	msg.udelay = client->udelay;

	ret = i2c_transfer(adap, &msg, 1);
	kfree(tx_buf);
	
	
	return (ret == 1) ? 4 : ret;
}

static int rk616_i2c_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	int ret;
	struct mfd_rk616 *rk616 = NULL;
	

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		dev_err(&client->dev, "Must have I2C_FUNC_I2C.\n");
		ret = -ENODEV;
	}
	rk616 = kzalloc(sizeof(struct mfd_rk616), GFP_KERNEL);
	if (rk616 == NULL)
	{
		printk(KERN_ALERT "alloc for struct rk616 fail\n");
		ret = -ENOMEM;
	}
	
	rk616->dev = &client->dev;
	rk616->pdata = client->dev.platform_data;
	rk616->client = client;
	i2c_set_clientdata(client, rk616);
	dev_set_drvdata(rk616->dev,rk616);
	
	if(rk616->pdata->power_init)
		rk616->pdata->power_init();
	
	rk616->read_dev = rk616_i2c_read_reg;
	rk616->write_dev = rk616_i2c_write_reg;
	ret = mfd_add_devices(rk616->dev, -1,
				      rk616_devs, ARRAY_SIZE(rk616_devs),
				      NULL, rk616->irq_base);
	
	printk("%s.........\n",__func__);
	return 0;
}

static int __devexit rk616_i2c_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id id_table[] = {
	{"rk616", 0 },
	{ }
};

static struct i2c_driver rk616_i2c_driver  = {
	.driver = {
		.name  = "rk616",
		.owner = THIS_MODULE,
	},
	.probe		= &rk616_i2c_probe,
	.remove     	= &rk616_i2c_remove,
	.id_table	= id_table,
};


static int __init rk616_module_init(void)
{
	return i2c_add_driver(&rk616_i2c_driver);
}

static void __exit rk616_module_exit(void)
{
	i2c_del_driver(&rk616_i2c_driver);
}

subsys_initcall_sync(rk616_module_init);
module_exit(rk616_module_exit);


