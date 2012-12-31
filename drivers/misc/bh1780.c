/*
 * BH1780GLI Light sensor driver
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

#define BH1780_NAME 	"bh1780"

/*
 * Registers
 */
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

#define	BH1780_DEFAULT_DELAY		150
#define	BH1780_MAX_DELAY			200

/*
 * driver private data
 */
struct bh1780_data {
	atomic_t enable;					/* attribute value */
	atomic_t delay;						/* attribute value */
	
	unsigned short	light_data;			/* lx : 0 ~ 65535 */
	unsigned short	light_data_last;	/* lx : 0 ~ 65535 */
	
	struct mutex enable_mutex;
	struct mutex data_mutex;
	struct i2c_client *client;
	struct input_dev *input;
	struct delayed_work work;
};

#define delay_to_jiffies(d) ((d)?msecs_to_jiffies(d):1)
#define actual_delay(d)     (jiffies_to_msecs(delay_to_jiffies(d)))

/*
 * Device dependant operations
 */
static int bh1780_power_up(struct bh1780_data *bh1780)
{
	i2c_smbus_write_byte_data(bh1780->client, (BH1780_COMMAND_REG + BH1780_CONTROL_REG), BH1780_POWER_UP);

	/* wait 200ms for wake-up time from sleep to operational mode */
	msleep(200);

	return 0;
}

static int bh1780_power_down(struct bh1780_data *bh1780)
{
	i2c_smbus_write_byte_data(bh1780->client, (BH1780_COMMAND_REG + BH1780_CONTROL_REG), BH1780_POWER_DOWN);

	return 0;
}

static int bh1780_get_enable(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bh1780_data *bh1780 = i2c_get_clientdata(client);

	return atomic_read(&bh1780->enable);
}

static void bh1780_set_enable(struct device *dev, int enable)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bh1780_data *bh1780 = i2c_get_clientdata(client);
	int delay = atomic_read(&bh1780->delay);

	mutex_lock(&bh1780->enable_mutex);

	if (enable) {                   /* enable if state will be changed */
		if (!atomic_cmpxchg(&bh1780->enable, 0, 1)) {
			bh1780_power_up(bh1780);
			schedule_delayed_work(&bh1780->work,
					      delay_to_jiffies(delay) + 1);
		}
	} else {                        /* disable if state will be changed */
		if (atomic_cmpxchg(&bh1780->enable, 1, 0)) {
			cancel_delayed_work_sync(&bh1780->work);
			bh1780_power_down(bh1780);
		}
	}
	atomic_set(&bh1780->enable, enable);

	mutex_unlock(&bh1780->enable_mutex);
}

static int bh1780_get_delay(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bh1780_data *bh1780 = i2c_get_clientdata(client);

	return atomic_read(&bh1780->delay);
}

static void bh1780_set_delay(struct device *dev, int delay)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bh1780_data *bh1780 = i2c_get_clientdata(client);

	mutex_lock(&bh1780->enable_mutex);

	atomic_set(&bh1780->delay, delay);

	if (bh1780_get_enable(dev)) {
		cancel_delayed_work_sync(&bh1780->work);
		schedule_delayed_work(&bh1780->work,
				      delay_to_jiffies(delay) + 1);
	} 

	mutex_unlock(&bh1780->enable_mutex);
}

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

static void bh1780_work_func(struct work_struct *work)
{
	struct bh1780_data *bh1780 = container_of((struct delayed_work *)work,
						  struct bh1780_data, work);
	unsigned long delay = delay_to_jiffies(atomic_read(&bh1780->delay));

	bh1780_measure(bh1780);

	input_report_abs(bh1780->input, ABS_X, bh1780->light_data);
	input_sync(bh1780->input);

	mutex_lock(&bh1780->data_mutex);
	bh1780->light_data_last = bh1780->light_data;
	mutex_unlock(&bh1780->data_mutex);

	schedule_delayed_work(&bh1780->work, delay);
}

/*
 * Input device interface
 */
static int bh1780_input_init(struct bh1780_data *bh1780)
{
	struct input_dev *dev;
	int err;

	dev = input_allocate_device();
	if (!dev) {
		return -ENOMEM;
	}
	dev->name = "light";
	dev->id.bustype = BUS_I2C;

	input_set_capability(dev, EV_ABS, ABS_MISC);
	input_set_abs_params(dev, ABS_X, BH1780_DATA_MIN, BH1780_DATA_MAX, 0, 0);
	input_set_drvdata(dev, bh1780);

	err = input_register_device(dev);
	if (err < 0) {
		input_free_device(dev);
		return err;
	}
	bh1780->input = dev;

	return 0;
}

static void bh1780_input_fini(struct bh1780_data *bh1780)
{
	struct input_dev *dev = bh1780->input;

	input_unregister_device(dev);
	input_free_device(dev);
}

/*
 * sysfs device attributes
 */
static ssize_t bh1780_enable_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", bh1780_get_enable(dev));
}

static ssize_t bh1780_enable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	unsigned long enable = simple_strtoul(buf, NULL, 10);

	if ((enable == 0) || (enable == 1)) {
		bh1780_set_enable(dev, enable);
	}

	return count;
}

static ssize_t bh1780_delay_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", bh1780_get_delay(dev));
}

static ssize_t bh1780_delay_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	unsigned long delay = simple_strtoul(buf, NULL, 10);

	if (delay > BH1780_MAX_DELAY) {
		delay = BH1780_MAX_DELAY;
	}

	bh1780_set_delay(dev, delay);

	return count;
}

static ssize_t bh1780_wake_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct input_dev *input = to_input_dev(dev);
	static atomic_t serial = ATOMIC_INIT(0);

	input_report_abs(input, ABS_MISC, atomic_inc_return(&serial));

	return count;
}

static ssize_t bh1780_data_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct bh1780_data *bh1780 = input_get_drvdata(input);
	unsigned short	light_data;
	
	mutex_lock(&bh1780->data_mutex);
	light_data = bh1780->light_data_last;
	mutex_unlock(&bh1780->data_mutex);

	return sprintf(buf, "%d\n", light_data);
}

static DEVICE_ATTR(enable,	S_IRWXUGO, bh1780_enable_show, 	bh1780_enable_store);
static DEVICE_ATTR(delay, 	S_IRWXUGO, bh1780_delay_show, 	bh1780_delay_store);
static DEVICE_ATTR(wake, 	S_IRWXUGO, NULL, 				bh1780_wake_store);
static DEVICE_ATTR(data, 	S_IRWXUGO, bh1780_data_show,	NULL);

static struct attribute *bh1780_attributes[] = {
	&dev_attr_enable.attr,
	&dev_attr_delay.attr,
	&dev_attr_wake.attr,
	&dev_attr_data.attr,
	NULL
};

static struct attribute_group bh1780_attribute_group = {
	.attrs = bh1780_attributes
};

/*
 * I2C client
 */
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

	if ((id & 0xFF) != BH1780_CHIP_ID)
		return -ENODEV;
	return 0;
}

static int bh1780_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct bh1780_data *bh1780;
	int err;

	/* setup private data */
	bh1780 = kzalloc(sizeof(struct bh1780_data), GFP_KERNEL);
	if (!bh1780) {
		err = -ENOMEM;
		goto error_0;
	}
	mutex_init(&bh1780->enable_mutex);
	mutex_init(&bh1780->data_mutex);

	/* setup i2c client */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto error_1;
	}
	i2c_set_clientdata(client, bh1780);
	bh1780->client = client;

	/* detect and init hardware */
	if ((err = bh1780_detect(client, NULL))) {
		goto error_1;
	}

	if((err = i2c_smbus_write_byte(bh1780->client, (BH1780_COMMAND_REG + BH1780_PART_REV_REG))) < 0)	{
		dev_err(&client->dev, "I2C write byte error: data=0x%02x\n", (BH1780_COMMAND_REG + BH1780_PART_REV_REG));
		goto error_1;
	}
	if((err = i2c_smbus_read_byte(client)) < 0)	{
		dev_err(&client->dev, "I2C read byte error\n");
		goto error_1;
	}
	
	dev_info(&client->dev, "%s found\n", id->name);
	dev_info(&client->dev, "part number=%d, rev=%d\n", ((err >> 4) & 0x0F), (err & 0x0F));

	bh1780_power_up(bh1780);	bh1780_set_delay(&client->dev, BH1780_DEFAULT_DELAY);

	/* setup driver interfaces */
	INIT_DELAYED_WORK(&bh1780->work, bh1780_work_func);
	
	if ((err = bh1780_input_init(bh1780)) < 0)		goto error_1;

	if ((err = sysfs_create_group(&bh1780->input->dev.kobj, &bh1780_attribute_group)) < 0)		goto error_2;

	return 0;

error_2:
	bh1780_input_fini(bh1780);
error_1:
	kfree(bh1780);
error_0:
	return err;
}

static int bh1780_remove(struct i2c_client *client)
{
	struct bh1780_data *bh1780 = i2c_get_clientdata(client);

	bh1780_set_enable(&client->dev, 0);

	sysfs_remove_group(&bh1780->input->dev.kobj, &bh1780_attribute_group);
	bh1780_input_fini(bh1780);
	kfree(bh1780);

	return 0;
}

static int bh1780_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct bh1780_data *bh1780 = i2c_get_clientdata(client);

	mutex_lock(&bh1780->enable_mutex);

	if (bh1780_get_enable(&client->dev)) {
		cancel_delayed_work_sync(&bh1780->work);
		bh1780_power_down(bh1780);
	}

	mutex_unlock(&bh1780->enable_mutex);

	return 0;
}

static int bh1780_resume(struct i2c_client *client)
{
	struct bh1780_data *bh1780 = i2c_get_clientdata(client);
	int delay = atomic_read(&bh1780->delay);

	bh1780_power_up(bh1780);
	bh1780_set_delay(&client->dev, delay);

	mutex_lock(&bh1780->enable_mutex);

	if (bh1780_get_enable(&client->dev)) {
		bh1780_power_up(bh1780);
		schedule_delayed_work(&bh1780->work,
				      delay_to_jiffies(delay) + 1);
	}

	mutex_unlock(&bh1780->enable_mutex);

	return 0;
}

static const struct i2c_device_id bh1780_id[] = {
	{BH1780_NAME, 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, bh1780_id);

struct i2c_driver bh1780_driver ={
	.driver = {
		.name = BH1780_NAME,
		.owner = THIS_MODULE,
	},
	.probe = bh1780_probe,
	.remove = bh1780_remove,
	.suspend = bh1780_suspend,
	.resume = bh1780_resume,
	.id_table = bh1780_id,
};

/*
 * Module init and exit
 */
static int __init bh1780_init(void)
{
	return i2c_add_driver(&bh1780_driver);
}
module_init(bh1780_init);

static void __exit bh1780_exit(void)
{
	i2c_del_driver(&bh1780_driver);
}
module_exit(bh1780_exit);

MODULE_DESCRIPTION("BH1780 Light sensor driver");
MODULE_LICENSE("GPL");
