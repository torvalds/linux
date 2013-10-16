//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  ODROID IOBOARD Board : IOBOARD BH1780 Sensor driver (charles.park)
//  2013.08.28
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

//[*]--------------------------------------------------------------------------------------------------[*]
#define BH1780_NAME 	    "ioboard-bh1780"

#if defined(CONFIG_ODROIDXU_IOBOARD_DEBUG)
    #define BH1780_WORK_PERIOD  msecs_to_jiffies(1000)    // 1000 ms
#else
    #define BH1780_WORK_PERIOD  msecs_to_jiffies(100)     // 100 ms
#endif

//[*]--------------------------------------------------------------------------------------------------[*]
//
// Registers Define
//
//[*]--------------------------------------------------------------------------------------------------[*]
#define	BH1780_CONTROL_REG			0x00
	#define	BH1780_POWER_UP				0x03
	#define	BH1780_POWER_DOWN			0x00

#define BH1780_PART_REV_REG			0x0A
#define BH1780_CHIP_ID_REG			0x0B
	#define BH1780_CHIP_ID				0x01

#define BH1780_DATA_LOW_REG			0x0C
#define BH1780_DATA_HIGH_REG		0x0D
	#define	BH1780_DATA_CAL(high, low)	((high & 0xFF) << 8 | (low & 0xff))

#define	BH1780_COMMAND_REG			0x80

#define	BH1780_DATA_MIN				0
#define	BH1780_DATA_MAX				0xFFFF

//[*]--------------------------------------------------------------------------------------------------[*]
//
// Driver private data
//
//[*]--------------------------------------------------------------------------------------------------[*]
struct  bh1780_data     {
	struct i2c_client       *client;
	struct delayed_work     work;
	bool                    enabled;
	unsigned short	        light_data;			/* lx : 0 ~ 65535 */
};

//[*]--------------------------------------------------------------------------------------------------[*]
//
// Device dependant operations
//
//[*]--------------------------------------------------------------------------------------------------[*]
static int bh1780_power_up(struct bh1780_data *bh1780)
{
	i2c_smbus_write_byte_data(bh1780->client, (BH1780_COMMAND_REG + BH1780_CONTROL_REG), BH1780_POWER_UP);

	/* wait 200ms for wake-up time from sleep to operational mode */
	msleep(200);

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int bh1780_power_down(struct bh1780_data *bh1780)
{
	i2c_smbus_write_byte_data(bh1780->client, (BH1780_COMMAND_REG + BH1780_CONTROL_REG), BH1780_POWER_DOWN);

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int bh1780_measure(struct bh1780_data *bh1780)
{
	struct i2c_client *client = bh1780->client;
	int	low_data, high_data;

	/* read light sensor data */
	if(i2c_smbus_write_byte(bh1780->client, (BH1780_COMMAND_REG + BH1780_DATA_LOW_REG)) < 0)	{
		dev_err(&client->dev, "I2C write byte error: data=0x%02x\n", (BH1780_COMMAND_REG + BH1780_DATA_LOW_REG));
		goto err;
	}
	if((low_data = i2c_smbus_read_byte(client)) < 0)	{
		dev_err(&client->dev, "I2C read byte error\n");
		goto err;
	}

	if(i2c_smbus_write_byte(bh1780->client, (BH1780_COMMAND_REG + BH1780_DATA_HIGH_REG)) < 0)	{
		dev_err(&client->dev, "I2C write byte error: data=0x%02x\n", (BH1780_COMMAND_REG + BH1780_DATA_HIGH_REG));
		goto err;
	}
	if((high_data = i2c_smbus_read_byte(client)) < 0)	{
		dev_err(&client->dev, "I2C read byte error\n");
		goto err;
	}

	bh1780->light_data = BH1780_DATA_CAL(high_data, low_data);

err:

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static void bh1780_work_func(struct work_struct *work)
{
	struct bh1780_data *bh1780 = container_of((struct delayed_work *)work,
						  struct bh1780_data, work);

	bh1780_measure(bh1780);

    #if defined(CONFIG_ODROIDXU_IOBOARD_DEBUG)
        printk("===> %s : %d \n", __func__, bh1780->light_data);
    #endif

    if(bh1780->enabled)
	    schedule_delayed_work(&bh1780->work, BH1780_WORK_PERIOD);
	else    
	    bh1780->light_data = 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
//
// sysfs device attributes
// 
//[*]--------------------------------------------------------------------------------------------------[*]
static ssize_t bh1780_data_show         (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bh1780_data *bh1780 = dev_get_drvdata(dev);
	
	return sprintf(buf, "%d\n", bh1780->light_data);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t bh1780_enable_show		(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bh1780_data *bh1780 = dev_get_drvdata(dev);

	return	sprintf(buf, "%d\n", bh1780->enabled);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t bh1780_enable_set		(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int	    val;
	struct bh1780_data  *bh1780 = dev_get_drvdata(dev);

    if(!(sscanf(buf, "%d\n", &val))) 	return	-EINVAL;

    val = (val > 0) ? 1 : 0;

    if(bh1780->enabled != val)    {
        bh1780->enabled = val;
        if(bh1780->enabled) schedule_delayed_work(&bh1780->work, BH1780_WORK_PERIOD);
    }

    return 	count;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static DEVICE_ATTR(lux,     S_IRWXUGO, bh1780_data_show,	NULL);
static DEVICE_ATTR(enable,  S_IRWXUGO, bh1780_enable_show,	bh1780_enable_set);

static struct attribute *bh1780_attributes[] = {
	&dev_attr_lux.attr,
	&dev_attr_enable.attr,
	NULL
};

static struct attribute_group bh1780_attribute_group = {
	.attrs = bh1780_attributes
};

//[*]--------------------------------------------------------------------------------------------------[*]
//
// I2C client
//
//[*]--------------------------------------------------------------------------------------------------[*]
static int bh1780_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	int id;

	if(i2c_smbus_write_byte(client, (BH1780_COMMAND_REG + BH1780_CHIP_ID_REG)) < 0)	{
		dev_err(&client->dev, "I2C write byte error: data=0x%02x\n", (BH1780_COMMAND_REG + BH1780_CHIP_ID_REG));
		return -ENODEV;
	}
	if((id = i2c_smbus_read_byte(client)) < 0)	{
		dev_err(&client->dev, "I2C read byte error\n");
		return -ENODEV;
	}

	if ((id & 0xFF) != BH1780_CHIP_ID)  return -ENODEV;
	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int bh1780_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct bh1780_data  *bh1780;
	int                 err;

	/* setup private data */
	bh1780 = kzalloc(sizeof(struct bh1780_data), GFP_KERNEL);
	if (!bh1780) {
		pr_err("%s: failed to allocate memory for module\n", __func__);
		return -ENOMEM;
	}

	/* detect and init hardware */
	if ((err = bh1780_detect(client, NULL)) != 0)   goto error;

	i2c_set_clientdata(client, bh1780);
	
    dev_set_drvdata(&client->dev, bh1780);
	
	bh1780->client = client;

	if((err = i2c_smbus_write_byte(bh1780->client, (BH1780_COMMAND_REG + BH1780_PART_REV_REG))) < 0)	{
		dev_err(&client->dev, "I2C write byte error: data=0x%02x\n", (BH1780_COMMAND_REG + BH1780_PART_REV_REG));
		goto error;
	}
	if((err = i2c_smbus_read_byte(client)) < 0)	{
		dev_err(&client->dev, "I2C read byte error\n");
		goto error;
	}
	
	dev_info(&client->dev, "%s found\n", id->name);
	dev_info(&client->dev, "part number=%d, rev=%d\n", ((err >> 4) & 0x0F), (err & 0x0F));

	bh1780_power_up(bh1780);	

	INIT_DELAYED_WORK(&bh1780->work, bh1780_work_func);
	
	#if defined(CONFIG_ODROIDXU_IOBOARD_DEBUG)
	    bh1780->enabled = 1;
	#endif
	
    if(bh1780->enabled) schedule_delayed_work(&bh1780->work, BH1780_WORK_PERIOD);
	
	if ((err = sysfs_create_group(&client->dev.kobj, &bh1780_attribute_group)) < 0)		goto error;

    printk("\n=================== ioboard_%s ===================\n\n", __func__);

	return 0;

error:
    printk("\n=================== ioboard_%s FAIL! ===================\n\n", __func__);
	kfree(bh1780);
	return err;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int bh1780_remove(struct i2c_client *client)
{
	struct bh1780_data *bh1780 = i2c_get_clientdata(client);

	if(bh1780->enabled) cancel_delayed_work_sync(&bh1780->work);
	sysfs_remove_group(&client->dev.kobj, &bh1780_attribute_group);

	kfree(bh1780);

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int bh1780_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct bh1780_data *bh1780 = i2c_get_clientdata(client);

	if(bh1780->enabled) cancel_delayed_work_sync(&bh1780->work);
	bh1780_power_down(bh1780);

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int bh1780_resume(struct i2c_client *client)
{
	struct bh1780_data *bh1780 = i2c_get_clientdata(client);

	bh1780_power_up(bh1780);
	if(bh1780->enabled) schedule_delayed_work(&bh1780->work, BH1780_WORK_PERIOD);

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static const struct i2c_device_id bh1780_id[] = {
	{BH1780_NAME, 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, bh1780_id);

//[*]--------------------------------------------------------------------------------------------------[*]
struct i2c_driver bh1780_driver ={
	.driver = {
		.name   = BH1780_NAME,
		.owner  = THIS_MODULE,
	},
	.probe      = bh1780_probe,
	.remove     = bh1780_remove,
	.suspend    = bh1780_suspend,
	.resume     = bh1780_resume,
	.id_table   = bh1780_id,
};

//[*]--------------------------------------------------------------------------------------------------[*]
/*
 * Module init and exit
 */
//[*]--------------------------------------------------------------------------------------------------[*]
static int __init bh1780_init(void)
{
	return i2c_add_driver(&bh1780_driver);
}
module_init(bh1780_init);

//[*]--------------------------------------------------------------------------------------------------[*]
static void __exit bh1780_exit(void)
{
	i2c_del_driver(&bh1780_driver);
}
module_exit(bh1780_exit);

//[*]--------------------------------------------------------------------------------------------------[*]
MODULE_DESCRIPTION("IOBOARD driver for ODROIDXU-Dev board");
MODULE_AUTHOR("Hard-Kernel");
MODULE_LICENSE("GPL");

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
