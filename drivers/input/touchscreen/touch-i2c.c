//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  I2C Touchscreen driver
//  2012.01.17
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/input.h>	/* BUS_I2C */
#include <linux/i2c.h>
#include <linux/module.h>

//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/input/touch-pdata.h>
#include "touch.h"

//[*]--------------------------------------------------------------------------------------------------[*]
//
// function prototype
//
//[*]--------------------------------------------------------------------------------------------------[*]
static 	void __exit		touch_i2c_exit		(void);
static 	int __init 		touch_i2c_init		(void);
static 	int __devexit 	touch_i2c_remove	(struct i2c_client *client);
static 	int __devinit 	touch_i2c_probe		(struct i2c_client *client, const struct i2c_device_id *id);
		int 			touch_i2c_read		(struct i2c_client *client, unsigned char *cmd, unsigned int cmd_len, unsigned char *data, unsigned int len);
		int 			touch_i2c_write		(struct i2c_client *client, unsigned char *cmd, unsigned int cmd_len, unsigned char *data, unsigned int len);
		
//[*]--------------------------------------------------------------------------------------------------[*]
#ifdef CONFIG_PM
static int 	touch_i2c_suspend(struct i2c_client *client, pm_message_t message)
{
	#ifdef CONFIG_HAS_EARLYSUSPEND
		struct	touch	*ts = i2c_get_clientdata(client);
	
		ts->pdata->suspend(&client->dev);
	#endif

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int 	touch_i2c_resume(struct i2c_client *client)
{
	#ifdef CONFIG_HAS_EARLYSUSPEND
		struct	touch	*ts = i2c_get_clientdata(client);

		ts->pdata->resume(&cliet->dev);
	#endif	

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
#else
	#define touch_i2c_suspend 	NULL
	#define touch_i2c_resume  	NULL
#endif

//[*]--------------------------------------------------------------------------------------------------[*]
int 	touch_i2c_read(struct i2c_client *client, unsigned char *cmd, unsigned int cmd_len, unsigned char *data, unsigned int len)
{
	struct i2c_msg	msg[2];
	int 			ret;

	if((len == 0) || (data == NULL))	{
		dev_err(&client->dev, "I2C read error: Null pointer or length == 0\n");	
		return 	-1;
	}

	memset(msg, 0x00, sizeof(msg));

	msg[0].addr 	= client->addr;
	msg[0].flags 	= 0;
	msg[0].len 		= cmd_len;
	msg[0].buf 		= cmd;

	msg[1].addr 	= client->addr;
	msg[1].flags    = I2C_M_RD;
	msg[1].len 		= len;
	msg[1].buf 		= data;
	
	if ((ret = i2c_transfer(client->adapter, msg, 2)) != 2) {
		dev_err(&client->dev, "I2C read error: (%d) reg: 0x%X len: %d\n", ret, cmd[0], len);
		return -EIO;
	}

	return 	len;
}

//[*]--------------------------------------------------------------------------------------------------[*]
int 	touch_i2c_write(struct i2c_client *client, unsigned char *cmd, unsigned int cmd_len, unsigned char *data, unsigned int len)
{
	int 			ret;
	unsigned char	block_data[10];

	if((cmd_len + len) >= sizeof(block_data))	{
		dev_err(&client->dev, "I2C write error: wdata overflow reg: 0x%X len: %d\n", cmd[0], cmd_len + len);
		return	-1;
	}

	memset(block_data, 0x00, sizeof(block_data));
	
	if(cmd_len)	memcpy(&block_data[0]		, &cmd[0]	, cmd_len);
	if(len)		memcpy(&block_data[cmd_len]	, &data[0]	, len);

	if ((ret = i2c_master_send(client, block_data, (cmd_len + len)))< 0) {
		dev_err(&client->dev, "I2C write error: (%d) reg: 0x%X len: %d\n", ret, cmd[0], len);
		return ret;
	}
	
	return len;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int __devinit 	touch_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	return	touch_probe(client);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int __devexit	touch_i2c_remove(struct i2c_client *client)
{
	return	touch_remove(&client->dev);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static const struct i2c_device_id touch_id[] = {
	{ I2C_TOUCH_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, touch_id);

//[*]--------------------------------------------------------------------------------------------------[*]
static struct i2c_driver touch_i2c_driver = {
	.driver = {
		.name	= I2C_TOUCH_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= touch_i2c_probe,
	.remove		= __devexit_p(touch_i2c_remove),
	.suspend	= touch_i2c_suspend,
	.resume		= touch_i2c_resume,
	.id_table	= touch_id,
};

//[*]--------------------------------------------------------------------------------------------------[*]
static int __init 	touch_i2c_init(void)
{
	return i2c_add_driver(&touch_i2c_driver);
}
module_init(touch_i2c_init);

//[*]--------------------------------------------------------------------------------------------------[*]
static void __exit 	touch_i2c_exit(void)
{
	i2c_del_driver(&touch_i2c_driver);
}
module_exit(touch_i2c_exit);

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
