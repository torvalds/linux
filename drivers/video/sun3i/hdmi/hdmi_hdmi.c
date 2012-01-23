
#include "dev_hdmi.h"
#include <linux/cdev.h>
#include <linux/i2c.h>

static struct cdev *my_cdev;
static dev_t devid ;
static struct class *hdmi_class;


static struct i2c_client *anx7150_client_0= NULL;
static struct i2c_client *anx7150_client_1 = NULL;
static unsigned char anx7150_reg = 0x04;  //default value is point to debug reg

static ssize_t show_reg(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	return sprintf(buf, "0x%x\n", anx7150_reg);
}

static ssize_t set_reg(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	u8 data = simple_strtoul(buf, NULL, 16);

	anx7150_reg = data;

	return count;
}

static DEVICE_ATTR(anx7150_reg, S_IWUSR | S_IRUGO, show_reg, set_reg);


static ssize_t show_read(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	u8 data;
	int err;

	/* 读必需分成两部分，一个为写设备内寄存器地址，一个读数据，
	中间可以产生stop、start，满足IIC协议的要求 */
	err = i2c_master_send(client, &anx7150_reg, 1);
	if ( err < 0 ) {
		__err("err meet when send reg addr in read\n");
		return err;
	}
	err = i2c_master_recv(client, &data, 1);
	if ( err < 0 ){
		__err("err meet when read data in read\n");
		return err;
	}

	return sprintf(buf, "%x\n", data);
}

static ssize_t set_write(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	u8 data[2];
	int err;

	/* 写必需只能地址和数据同时发出，设备内寄存器地址和数据
		中间不能有start/stop/restart等出现 */
	data[0] = anx7150_reg;
	data[1] = simple_strtoul(buf, NULL, 16);
	err = i2c_master_send(client, data, 2);
	if ( err < 0 ){
		__err("err meet when write\n");
		return err;
	}

	return count;
}

static DEVICE_ATTR(anx7150_iic, S_IWUSR | S_IRUGO, show_read, set_write);

static struct attribute *anx7150_iic_attributes[] = {
	&dev_attr_anx7150_iic.attr,
	&dev_attr_anx7150_reg.attr,
	NULL
};

static const struct attribute_group anx7150_iic_attr_group = {
	.attrs = anx7150_iic_attributes,
};


int ANX7150_i2c_read_p0_reg(u8 reg, __u8 *rt_value)
{
	struct i2c_client *client = anx7150_client_0;

	if (!client || !client->adapter){
		__err("iic not exsit yet when read p0.\n");
		return -ENODEV;
	}

	*rt_value = i2c_smbus_read_byte_data(client,reg);

	return 0;
}

int ANX7150_i2c_read_p1_reg(u8 reg, __u8 *rt_value)
{
	struct i2c_client *client = anx7150_client_1;

	if (!client || !client->adapter){
		__err("iic not exsit yet when read p1.\n");
		return -ENODEV;
	}

	if (!client || !client->adapter){
		__err("iic not exsit yet when read p0.\n");
		return -ENODEV;
	}

	*rt_value = i2c_smbus_read_byte_data(client,reg);

	return 0;
}
int ANX7150_i2c_write_p0_reg(__u8 reg, __u8 d)
{
	struct i2c_client *client = anx7150_client_0;

	if (!client || !client->adapter){
		__err("iic not exsit yet when write p0.\n");
		return -ENODEV;
	}

	i2c_smbus_write_byte_data(client,reg,d);

	return 0;
}
int ANX7150_i2c_write_p1_reg(__u8 reg, __u8 d)
{
	struct i2c_client *client = anx7150_client_1;

	if (!client || !client->adapter){
		__err("iic not exsit yet when write p0.\n");
		return -ENODEV;
	}

	i2c_smbus_write_byte_data(client,reg,d);

	return 0;
}

static int anx7150_iic_probe_0(struct i2c_client *client,
			const struct i2c_device_id *id)
{
   int rc;

	__msg("---------------------enter anx7150_iic_probe_0\n");
   if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_I2C)) {
	   __err("i2c bus does not support the IICDEV\n");
	   rc = -ENODEV;
	   goto exit;
   }
	rc = sysfs_create_group(&client->dev.kobj, &anx7150_iic_attr_group);
   anx7150_client_0= client;
   anx7150_client_0->addr /= 2;

exit:
   return rc;
}

static int anx7150_iic_probe_1(struct i2c_client *client,
			const struct i2c_device_id *id)
{
   int rc;

	__msg("---------------------enter anx7150_iic_probe_1\n");
   if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_I2C)) {
	   __err("i2c bus does not support the IICDEV\n");
	   rc = -ENODEV;
	   goto exit;
   }
   anx7150_client_1= client;
   anx7150_client_1->addr /= 2;

exit:
   return rc;
}

static int anx7150_iic_remove_0(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &anx7150_iic_attr_group);
	anx7150_client_0 = NULL;
	return 0;
}


static int anx7150_iic_remove_1(struct i2c_client *client)
{
	anx7150_client_1 = NULL;
	return 0;
}

static const struct i2c_device_id anx7150_iic_id_0[] = {
	{ "anx7150_0", 0 },
	{ }
};

static const struct i2c_device_id anx7150_iic_id_1[] = {
	{ "anx7150_1", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c,  anx7150_iic_id_0);
MODULE_DEVICE_TABLE(i2c,  anx7150_iic_id_1);

static struct i2c_driver anx7150_iic_driver_0 = {
	.driver = {
		.name = "anx7150_0",
	},
	.probe = anx7150_iic_probe_0,
	.remove = anx7150_iic_remove_0,
	.id_table = anx7150_iic_id_0,
};

static struct i2c_driver anx7150_iic_driver_1 = {
	.driver = {
		.name = "anx7150_1",
	},
	.probe = anx7150_iic_probe_1,
	.remove = anx7150_iic_remove_1,
	.id_table = anx7150_iic_id_1,
};


static const struct file_operations hdmi_fops =
{
	.owner		= THIS_MODULE,
	.open		= hdmi_open,
	.release    = hdmi_release,
	.write      = hdmi_write,
	.read		= hdmi_read,
	.unlocked_ioctl	= hdmi_ioctl,
	.mmap       = hdmi_mmap,
};



int __init hdmi_module_init(void)
{
	int ret = 0, err;

	__msg("----------- hdmi_module_init call --------------\n");

	 alloc_chrdev_region(&devid, 0, 1, "hdmi_chrdev");
	 my_cdev = cdev_alloc();
	 cdev_init(my_cdev, &hdmi_fops);
	 my_cdev->owner = THIS_MODULE;
	 err = cdev_add(my_cdev, devid, 1);
	 if (err)
	 {
		  __err("I was assigned major number %d.\n", MAJOR(devid));
		  return -1;
	 }

    hdmi_class = class_create(THIS_MODULE, "hdmi_class");
    if (IS_ERR(hdmi_class))
    {
        __err("create class error\n");
        return -1;
    }
	ret |= i2c_add_driver(&anx7150_iic_driver_0);
	ret |= i2c_add_driver(&anx7150_iic_driver_1);

	__msg(" drv hdmi init\n");
	DRV_HDMI_MInit();

	__msg("----------- hdmi_module_init call ok --------------\n");
	return ret;
}

static void __exit hdmi_module_exit(void)
{
	__msg("hdmi_module_exit\n");

	i2c_del_driver(&anx7150_iic_driver_1);
	i2c_del_driver(&anx7150_iic_driver_0);

    class_destroy(hdmi_class);

    cdev_del(my_cdev);
}



//module_init(hdmi_module_init);
late_initcall(hdmi_module_init);  //[tt]
module_exit(hdmi_module_exit);

MODULE_AUTHOR("danling_xiao");
MODULE_DESCRIPTION("hdmi driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:hdmi");

