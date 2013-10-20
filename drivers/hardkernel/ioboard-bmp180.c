//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  ODROID IOBOARD Board : IOBOARD BMP180 Sensor driver (charles.park)
//  2013.08.28
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

//[*]--------------------------------------------------------------------------------------------------[*]
#define BMP180_DRV_NAME		"ioboard-bmp180"

#if defined(CONFIG_ODROIDXU_IOBOARD_DEBUG)
    #define BMP180_WORK_PERIOD  msecs_to_jiffies(1000)    // 1000 ms
#else
    #define BMP180_WORK_PERIOD  msecs_to_jiffies(100)     // 100 ms
#endif    

#define BMP180_OVERSAMPLE   3

#define BMP180_ID           0x55
//[*]--------------------------------------------------------------------------------------------------[*]
// Register definitions
//[*]--------------------------------------------------------------------------------------------------[*]
#define BMP180_ID_REG               0xD0
#define BMP180_TAKE_MEAS_REG		0xf4
#define BMP180_READ_MEAS_REG_U		0xf6
#define BMP180_READ_MEAS_REG_L		0xf7
#define BMP180_READ_MEAS_REG_XL		0xf8

//[*]--------------------------------------------------------------------------------------------------[*]
/*
 * Bytes defined by the spec to take measurements
 * Temperature will take 4.5ms before EOC
 */
#define BMP180_MEAS_TEMP		        0x2e
/* 4.5ms wait for measurement */
#define BMP180_MEAS_PRESS_OVERSAMP_0	0x34
/* 7.5ms wait for measurement */
#define BMP180_MEAS_PRESS_OVERSAMP_1	0x74
/* 13.5ms wait for measurement */
#define BMP180_MEAS_PRESS_OVERSAMP_2	0xb4
/* 25.5ms wait for measurement */
#define BMP180_MEAS_PRESS_OVERSAMP_3	0xf4

/*
 * EEPROM registers each is a two byte value so there is
 * an upper byte and a lower byte
 */
#define BMP180_EEPROM_AC1_U	0xaa

//[*]--------------------------------------------------------------------------------------------------[*]
struct bmp180_eeprom_data {
	s16 AC1, AC2, AC3;
	u16 AC4, AC5, AC6;
	s16 B1, B2;
	s16 MB, MC, MD;
};

//[*]--------------------------------------------------------------------------------------------------[*]
struct bmp180_data {
	struct i2c_client           *client;
	struct delayed_work         work;
	bool                        enabled;

	struct bmp180_eeprom_data   eeprom_vals;
    unsigned int                pressure;
    unsigned int                temperature;
};

//[*]--------------------------------------------------------------------------------------------------[*]
static int bmp180_i2c_read(const struct i2c_client *client, u8 cmd,	u8 *buf, int len)
{
	int err;

	err = i2c_smbus_read_i2c_block_data(client, cmd, len, buf);
	
	if (err == len)     return 0;

	return err;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int bmp180_i2c_write(const struct i2c_client *client, u8 cmd, u8 data)
{
	int err;

	err = i2c_smbus_write_byte_data(client, cmd, data);
	if (!err)           return 0;

	return err;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int bmp180_get_raw_temperature(struct bmp180_data *bmp180, u16 *raw_temperature)
{
	int err;
	u16 buf;

	err = bmp180_i2c_write(bmp180->client, BMP180_TAKE_MEAS_REG, BMP180_MEAS_TEMP);
	if (err) {
		pr_err("%s: can't write BMP180_TAKE_MEAS_REG\n", __func__);
		return err;
	}

	msleep(5);

	err = bmp180_i2c_read(bmp180->client, BMP180_READ_MEAS_REG_U, (u8 *)&buf, 2);
	if (err) {
		pr_err("%s: Fail to read uncompensated temperature\n", __func__);
		return err;
	}

	*raw_temperature = be16_to_cpu(buf);

	return err;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int bmp180_get_raw_pressure(struct bmp180_data *bmp180, u32 *raw_pressure)
{
	int err;
	u32 buf = 0;

	err = bmp180_i2c_write(bmp180->client, BMP180_TAKE_MEAS_REG, 
	                        BMP180_MEAS_PRESS_OVERSAMP_0 | (BMP180_OVERSAMPLE << 6));
	if (err) {
		pr_err("%s: can't write BMP180_TAKE_MEAS_REG\n", __func__);
		return err;
	}

	msleep(2+(3 << BMP180_OVERSAMPLE));

	err = bmp180_i2c_read(bmp180->client, BMP180_READ_MEAS_REG_U, ((u8 *)&buf)+1, 3);
	if (err) {
		pr_err("%s: Fail to read uncompensated pressure\n", __func__);
		return err;
	}

	*raw_pressure = be32_to_cpu(buf);
	*raw_pressure >>= (8 - BMP180_OVERSAMPLE);

	return err;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static void bmp180_work_func(struct work_struct *work)
{
	u16             raw_temperature;
	u32             raw_pressure;
	
	long            x1, x2, x3, b3, b5, b6;
	unsigned long   b4, b7;
	long            p;

	struct bmp180_data *bmp180 = container_of((struct delayed_work *)work,
						  struct bmp180_data, work);

	if (bmp180_get_raw_temperature(bmp180, &raw_temperature)) {
		pr_err("%s: can't read uncompensated temperature\n", __func__);
		return;
	}

	if (bmp180_get_raw_pressure(bmp180, &raw_pressure)) {
		pr_err("%s: Fail to read uncompensated pressure\n", __func__);
		return;
	}

	x1 = ((raw_temperature - bmp180->eeprom_vals.AC6) *
	      bmp180->eeprom_vals.AC5) >> 15;
	x2 = (bmp180->eeprom_vals.MC << 11) /
	    (x1 + bmp180->eeprom_vals.MD);
	b5 = x1 + x2;

	bmp180->temperature = (x1+x2+8) >> 4;

	b6 = (b5 - 4000);
	x1 = (bmp180->eeprom_vals.B2 * ((b6 * b6) >> 12)) >> 11;
	x2 = (bmp180->eeprom_vals.AC2 * b6) >> 11;
	x3 = x1 + x2;
	b3 = (((((long)bmp180->eeprom_vals.AC1) * 4 +
		x3) << BMP180_OVERSAMPLE) + 2) >> 2;
	x1 = (bmp180->eeprom_vals.AC3 * b6) >> 13;
	x2 = (bmp180->eeprom_vals.B1 * (b6 * b6 >> 12)) >> 16;
	x3 = ((x1 + x2) + 2) >> 2;
	b4 = (bmp180->eeprom_vals.AC4 *
	      (unsigned long)(x3 + 32768)) >> 15;
	b7 = ((unsigned long)raw_pressure - b3) *
		(50000 >> BMP180_OVERSAMPLE);
	if (b7 < 0x80000000)
		p = (b7 * 2) / b4;
	else
		p = (b7 / b4) * 2;

	x1 = (p >> 8) * (p >> 8);
	x1 = (x1 * 3038) >> 16;
	x2 = (-7357 * p) >> 16;
	bmp180->pressure = p + ((x1 + x2 + 3791) >> 4);

    #if defined(CONFIG_ODROIDXU_IOBOARD_DEBUG)
        printk("===> %s : %d %d\n", __func__, bmp180->pressure, bmp180->temperature);
    #endif

    if(bmp180->enabled)
	    schedule_delayed_work(&bmp180->work, BMP180_WORK_PERIOD);
    else    {
        bmp180->pressure    = 0;
        bmp180->temperature = 0;
    }	    
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int bmp180_read_store_eeprom_val(struct bmp180_data *bmp180)
{
	int err;
	u16 buf[11];

	err = bmp180_i2c_read(bmp180->client, BMP180_EEPROM_AC1_U, (u8 *)buf, 22);
	if (err) {
		pr_err("%s: Cannot read EEPROM values\n", __func__);
		return err;
	}
	bmp180->eeprom_vals.AC1 = be16_to_cpu(buf[0]);
	bmp180->eeprom_vals.AC2 = be16_to_cpu(buf[1]);
	bmp180->eeprom_vals.AC3 = be16_to_cpu(buf[2]);
	bmp180->eeprom_vals.AC4 = be16_to_cpu(buf[3]);
	bmp180->eeprom_vals.AC5 = be16_to_cpu(buf[4]);
	bmp180->eeprom_vals.AC6 = be16_to_cpu(buf[5]);
	bmp180->eeprom_vals.B1 = be16_to_cpu(buf[6]);
	bmp180->eeprom_vals.B2 = be16_to_cpu(buf[7]);
	bmp180->eeprom_vals.MB = be16_to_cpu(buf[8]);
	bmp180->eeprom_vals.MC = be16_to_cpu(buf[9]);
	bmp180->eeprom_vals.MD = be16_to_cpu(buf[10]);
	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
//
// sysfs device attributes
// 
//[*]--------------------------------------------------------------------------------------------------[*]
static ssize_t bmp180_pressure_show     (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bmp180_data *bmp180 = dev_get_drvdata(dev);
	
	return sprintf(buf, "%d\n", bmp180->pressure);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static ssize_t bmp180_temperature_show  (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bmp180_data *bmp180 = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", bmp180->temperature);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t bmp180_enable_show		(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bmp180_data *bmp180 = dev_get_drvdata(dev);

	return	sprintf(buf, "%d\n", bmp180->enabled);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t bmp180_enable_set		(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct bmp180_data  *bmp180 = dev_get_drvdata(dev);
    unsigned int	    val;

    if(!(sscanf(buf, "%d\n", &val))) 	return	-EINVAL;

    val = (val > 0) ? 1 : 0;

    if(bmp180->enabled != val)    {
        bmp180->enabled = val;
        if(bmp180->enabled) schedule_delayed_work(&bmp180->work, BMP180_WORK_PERIOD);
    }

    return 	count;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static DEVICE_ATTR(pressure,    S_IRWXUGO, bmp180_pressure_show,	NULL);
static DEVICE_ATTR(temperature, S_IRWXUGO, bmp180_temperature_show,	NULL);
static DEVICE_ATTR(enable,      S_IRWXUGO, bmp180_enable_show,	    bmp180_enable_set);

static struct attribute *bmp180_attributes[] = {
	&dev_attr_pressure.attr,
	&dev_attr_temperature.attr,
	&dev_attr_enable.attr,
	NULL
};

static struct attribute_group bmp180_attribute_group = {
	.attrs = bmp180_attributes
};

//[*]--------------------------------------------------------------------------------------------------[*]
static int bmp180_detect(struct i2c_client *client)
{
    unsigned char   id;

	if(bmp180_i2c_read(client, BMP180_ID_REG, &id, sizeof(id)))     return  -1;
	    
	if(id != BMP180_ID)     return  -1;
	    
	return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int bmp180_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct bmp180_data  *bmp180;
	int                 err=0;

	bmp180 = kzalloc(sizeof(struct bmp180_data), GFP_KERNEL);
	if (!bmp180) {
		pr_err("%s: failed to allocate memory for module\n", __func__);
		return -ENOMEM;
	}

    if(bmp180_detect(client) < 0)     goto error;

	i2c_set_clientdata(client, bmp180);
	
    dev_set_drvdata(&client->dev, bmp180);
	
	bmp180->client = client;

	err = bmp180_read_store_eeprom_val(bmp180);

	if (err) {
		pr_err("%s: Reading the EEPROM failed\n", __func__);
		err = -ENODEV;
		goto error;
	}

	INIT_DELAYED_WORK(&bmp180->work, bmp180_work_func);
	
	#if defined(CONFIG_ODROIDXU_IOBOARD_DEBUG)
	    bmp180->enabled = 1;
	#endif
	
	if(bmp180->enabled) schedule_delayed_work(&bmp180->work, BMP180_WORK_PERIOD);
	
	if ((err = sysfs_create_group(&client->dev.kobj, &bmp180_attribute_group)) < 0)		goto error;

    printk("\n=================== ioboard_%s ===================\n\n", __func__);

	return 0;

error:
    printk("\n=================== ioboard_%s FAIL! ===================\n\n", __func__);
	kfree(bmp180);
	return err;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int __devexit bmp180_remove(struct i2c_client *client)
{
	/* TO DO: revisit ordering here once _probe order is finalized */
	struct bmp180_data *bmp180 = i2c_get_clientdata(client);

	if(bmp180->enabled) cancel_delayed_work_sync(&bmp180->work);

	sysfs_remove_group(&client->dev.kobj, &bmp180_attribute_group);

	kfree(bmp180);

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int bmp180_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct bmp180_data *bmp180 = i2c_get_clientdata(client);

	if(bmp180->enabled) cancel_delayed_work_sync(&bmp180->work);

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int bmp180_resume(struct i2c_client *client)
{
	struct bmp180_data *bmp180 = i2c_get_clientdata(client);

	if(bmp180->enabled) schedule_delayed_work(&bmp180->work, BMP180_WORK_PERIOD);

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static const struct i2c_device_id bmp180_id[] = {
	{BMP180_DRV_NAME, 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, bmp180_id);

//[*]--------------------------------------------------------------------------------------------------[*]
struct i2c_driver bmp180_driver = {
	.driver = {
		.name   = BMP180_DRV_NAME,
		.owner  = THIS_MODULE,
	},
	.probe      = bmp180_probe,
	.remove     = bmp180_remove,
	.suspend    = bmp180_suspend,
	.resume     = bmp180_resume,
	.id_table   = bmp180_id,
};

//[*]--------------------------------------------------------------------------------------------------[*]
static int __init bmp180_init(void)
{
	return i2c_add_driver(&bmp180_driver);
}
module_init(bmp180_init);

//[*]--------------------------------------------------------------------------------------------------[*]
static void __exit bmp180_exit(void)
{
	i2c_del_driver(&bmp180_driver);
}
module_exit(bmp180_exit);

//[*]--------------------------------------------------------------------------------------------------[*]
MODULE_DESCRIPTION("IOBOARD driver for ODROIDXU-Dev board");
MODULE_AUTHOR("Hard-Kernel");
MODULE_LICENSE("GPL");

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
