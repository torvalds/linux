/*
	$License:
	Copyright (C) 2011 InvenSense Corporation, All Rights Reserved.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
	$
 */

/**
 *  @addtogroup COMPASSDL
 *
 *  @{
 *      @file   ak8963.c
 *      @brief  Magnetometer setup and handling methods for the AKM AK8963,
 *              AKM AK8963B, and AKM AK8963C compass devices.
 */

/* -------------------------------------------------------------------------- */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "mpu-dev.h"

#include <log.h>
#include <linux/mpu.h>
#include "mlsl.h"
#include "mldl_cfg.h"
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "MPL-compass"
#include <linux/of_gpio.h>
#include <linux/iio/consumer.h>

//#include <linux/gpio.h>
//#include <mach/gpio.h>
//#include <plat/gpio-cfg.h>

/* -------------------------------------------------------------------------- */
#define AK8963_REG_ST1  (0x02)
#define AK8963_REG_HXL  (0x03)
#define AK8963_REG_ST2  (0x09)

#define AK8963_REG_CNTL (0x0A)
#define AK8963_REG_ASAX (0x10)
#define AK8963_REG_ASAY (0x11)
#define AK8963_REG_ASAZ (0x12)

//define output bit is 16bit
#define AK8963_CNTL_MODE_POWER_DOWN         (0x10)
#define AK8963_CNTL_MODE_SINGLE_MEASUREMENT (0x11)
#define AK8963_CNTL_MODE_FUSE_ROM_ACCESS    (0x1f)

/* -------------------------------------------------------------------------- */
struct ak8963_config {
	char asa[COMPASS_NUM_AXES];	/* axis sensitivity adjustment */
};

struct ak8963_private_data {
	struct ak8963_config init;
};

struct ext_slave_platform_data ak8963_data;

/* -------------------------------------------------------------------------- */
static int ak8963_init(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata)
{
	int result;
	unsigned char serial_data[COMPASS_NUM_AXES];
	struct ak8963_private_data *private_data;
	private_data = (struct ak8963_private_data *)
	    kzalloc(sizeof(struct ak8963_private_data), GFP_KERNEL);

	if (!private_data)
		return INV_ERROR_MEMORY_EXAUSTED;

	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 AK8963_REG_CNTL,
					 AK8963_CNTL_MODE_POWER_DOWN);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	/* Wait at least 100us */
	udelay(100);

	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 AK8963_REG_CNTL,
					 AK8963_CNTL_MODE_FUSE_ROM_ACCESS);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	/* Wait at least 200us */
	udelay(200);

	result = inv_serial_read(mlsl_handle, pdata->address,
				 AK8963_REG_ASAX,
				 COMPASS_NUM_AXES, serial_data);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	pdata->private_data = private_data;

	private_data->init.asa[0] = serial_data[0];
	private_data->init.asa[1] = serial_data[1];
	private_data->init.asa[2] = serial_data[2];

	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 AK8963_REG_CNTL,
					 AK8963_CNTL_MODE_POWER_DOWN);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	printk("yemk:ak8963_init end\n");
	udelay(100);
	printk(KERN_ERR "invensense: %s ok\n", __func__);
	return INV_SUCCESS;
}

static int ak8963_exit(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata)
{
	kfree(pdata->private_data);
	return INV_SUCCESS;
}

static int ak8963_suspend(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata)
{
	int result = INV_SUCCESS;
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    AK8963_REG_CNTL,
				    AK8963_CNTL_MODE_POWER_DOWN);
	msleep(1);		/* wait at least 100us */
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	return result;
}

static int ak8963_resume(void *mlsl_handle,
		  struct ext_slave_descr *slave,
		  struct ext_slave_platform_data *pdata)
{
	int result = INV_SUCCESS;
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    AK8963_REG_CNTL,
				    AK8963_CNTL_MODE_SINGLE_MEASUREMENT);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	return result;
}

static int ak8963_read(void *mlsl_handle,
		struct ext_slave_descr *slave,
		struct ext_slave_platform_data *pdata, unsigned char *data)
{
	unsigned char regs[8];
	unsigned char *stat = &regs[0];
	unsigned char *stat2 = &regs[7];
	int result = INV_SUCCESS;
	int status = INV_SUCCESS;

	result =
	    inv_serial_read(mlsl_handle, pdata->address, AK8963_REG_ST1,
			    8, regs);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	/* Always return the data and the status registers */
	memcpy(data, &regs[1], 6);
	data[6] = regs[0];
	data[7] = regs[7];

	/*
	 * ST : data ready -
	 * Measurement has been completed and data is ready to be read.
	 */
	if (*stat & 0x01)
		status = INV_SUCCESS;

	/*
	 * ST2 : data error -
	 * occurs when data read is started outside of a readable period;
	 * data read would not be correct.
	 * Valid in continuous measurement mode only.
	 * In single measurement mode this error should not occour but we
	 * stil account for it and return an error, since the data would be
	 * corrupted.
	 * DERR bit is self-clearing when ST2 register is read.
	 */
//	if (*stat2 & 0x04)
//		status = INV_ERROR_COMPASS_DATA_ERROR;
	/*
	 * ST2 : overflow -
	 * the sum of the absolute values of all axis |X|+|Y|+|Z| < 2400uT.
	 * This is likely to happen in presence of an external magnetic
	 * disturbance; it indicates, the sensor data is incorrect and should
	 * be ignored.
	 * An error is returned.
	 * HOFL bit clears when a new measurement starts.
	 */
	if (*stat2 & 0x08)
		status = INV_ERROR_COMPASS_DATA_OVERFLOW;
	/*
	 * ST : overrun -
	 * the previous sample was not fetched and lost.
	 * Valid in continuous measurement mode only.
	 * In single measurement mode this error should not occour and we
	 * don't consider this condition an error.
	 * DOR bit is self-clearing when ST2 or any meas. data register is
	 * read.
	 */
	if (*stat & 0x02) {
		/* status = INV_ERROR_COMPASS_DATA_UNDERFLOW; */
		status = INV_SUCCESS;
	}

	/*
	 * trigger next measurement if:
	 *    - stat is non zero;
	 *    - if stat is zero and stat2 is non zero.
	 * Won't trigger if data is not ready and there was no error.
	 */
	if (*stat != 0x00 || (*stat2 & 0x08) != 0x00 ) {
		result = inv_serial_single_write(
		    mlsl_handle, pdata->address,
		    AK8963_REG_CNTL, AK8963_CNTL_MODE_SINGLE_MEASUREMENT);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}

	return status;
}

static int ak8963_config(void *mlsl_handle,
			 struct ext_slave_descr *slave,
			 struct ext_slave_platform_data *pdata,
			 struct ext_slave_config *data)
{
	int result;
	if (!data->data)
		return INV_ERROR_INVALID_PARAMETER;

	switch (data->key) {
	case MPU_SLAVE_WRITE_REGISTERS:
		result = inv_serial_write(mlsl_handle, pdata->address,
					  data->len,
					  (unsigned char *)data->data);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		break;
	case MPU_SLAVE_CONFIG_ODR_SUSPEND:
	case MPU_SLAVE_CONFIG_ODR_RESUME:
	case MPU_SLAVE_CONFIG_FSR_SUSPEND:
	case MPU_SLAVE_CONFIG_FSR_RESUME:
	case MPU_SLAVE_CONFIG_MOT_THS:
	case MPU_SLAVE_CONFIG_NMOT_THS:
	case MPU_SLAVE_CONFIG_MOT_DUR:
	case MPU_SLAVE_CONFIG_NMOT_DUR:
	case MPU_SLAVE_CONFIG_IRQ_SUSPEND:
	case MPU_SLAVE_CONFIG_IRQ_RESUME:
	default:
		return INV_ERROR_FEATURE_NOT_IMPLEMENTED;
	};

	return INV_SUCCESS;
}

static int ak8963_get_config(void *mlsl_handle,
			     struct ext_slave_descr *slave,
			     struct ext_slave_platform_data *pdata,
			     struct ext_slave_config *data)
{
	struct ak8963_private_data *private_data = pdata->private_data;
	int result;
	if (!data->data)
		return INV_ERROR_INVALID_PARAMETER;

	switch (data->key) {
	case MPU_SLAVE_READ_REGISTERS:
		{
			unsigned char *serial_data =
			    (unsigned char *)data->data;
			result =
			    inv_serial_read(mlsl_handle, pdata->address,
					    serial_data[0], data->len - 1,
					    &serial_data[1]);
			if (result) {
				LOG_RESULT_LOCATION(result);
				return result;
			}
			break;
		}
	case MPU_SLAVE_READ_SCALE:
		{
			unsigned char *serial_data =
			    (unsigned char *)data->data;
			serial_data[0] = private_data->init.asa[0];
			serial_data[1] = private_data->init.asa[1];
			serial_data[2] = private_data->init.asa[2];
			result = INV_SUCCESS;
			if (result) {
				LOG_RESULT_LOCATION(result);
				return result;
			}
			break;
		}
	case MPU_SLAVE_CONFIG_ODR_SUSPEND:
		(*(unsigned long *)data->data) = 0;
		break;
	case MPU_SLAVE_CONFIG_ODR_RESUME:
		(*(unsigned long *)data->data) = 8000;
		break;
	case MPU_SLAVE_CONFIG_FSR_SUSPEND:
	case MPU_SLAVE_CONFIG_FSR_RESUME:
	case MPU_SLAVE_CONFIG_MOT_THS:
	case MPU_SLAVE_CONFIG_NMOT_THS:
	case MPU_SLAVE_CONFIG_MOT_DUR:
	case MPU_SLAVE_CONFIG_NMOT_DUR:
	case MPU_SLAVE_CONFIG_IRQ_SUSPEND:
	case MPU_SLAVE_CONFIG_IRQ_RESUME:
	default:
		return INV_ERROR_FEATURE_NOT_IMPLEMENTED;
	};

	return INV_SUCCESS;
}

static struct ext_slave_read_trigger ak8963_read_trigger = {
	/*.reg              = */ 0x0A,
	/*.value            = */ 0x11
};

static struct ext_slave_descr ak8963_descr = {
	.init             = ak8963_init,
	.exit             = ak8963_exit,
	.suspend          = ak8963_suspend,
	.resume           = ak8963_resume,
	.read             = ak8963_read,
	.config           = ak8963_config,
	.get_config       = ak8963_get_config,
	.name             = "ak8963",
	.type             = EXT_SLAVE_TYPE_COMPASS,
	.id               = COMPASS_ID_AK8963,
	.read_reg         = 0x01,
	.read_len         = 10,
	.endian           = EXT_SLAVE_LITTLE_ENDIAN,
	.range            = {9830, 4000},
	.trigger          = &ak8963_read_trigger,
};

static
struct ext_slave_descr *ak8963_get_slave_descr(void)
{
	return &ak8963_descr;
}

/* -------------------------------------------------------------------------- */
struct ak8963_mod_private_data {
	struct i2c_client *client;
	struct ext_slave_platform_data *pdata;
};

static unsigned short normal_i2c[] = { I2C_CLIENT_END };

static int ak8963_parse_dt(struct i2c_client *client,
				  struct ext_slave_platform_data *data)
{
	int ret;
	struct device_node *np = client->dev.of_node;
	//enum of_gpio_flags gpioflags;
	int length = 0,size = 0;
	struct property *prop;
	int debug = 1;
	int i;
	int orig_x,orig_y,orig_z;
	u32 orientation[9];

	ret = of_property_read_u32(np,"compass-bus",&data->bus);
	if(ret!=0){
		dev_err(&client->dev, "get compass-bus error\n");
		return -EIO;
		}

	ret = of_property_read_u32(np,"compass-adapt_num",&data->adapt_num);
	if(ret!=0){
		dev_err(&client->dev, "get compass-adapt_num error\n");
		return -EIO;
		}

	prop = of_find_property(np, "compass-orientation", &length);
	if (!prop){
		dev_err(&client->dev, "get compass-orientation length error\n");
		return -EINVAL;
	}

	size = length / sizeof(int);

	if((size > 0)&&(size <10)){
		ret = of_property_read_u32_array(np, "compass-orientation",
					 orientation,
					 size);
		if(ret<0){
			dev_err(&client->dev, "get compass-orientation data error\n");
			return -EINVAL;
		}
	}
	else{
		printk(" use default orientation\n");
	}

	for(i=0;i<9;i++)
		data->orientation[i]= orientation[i];


	ret = of_property_read_u32(np,"orientation-x",&orig_x);
	if(ret!=0){
		dev_err(&client->dev, "get orientation-x error\n");
		return -EIO;
	}

	if(orig_x>0){
		for(i=0;i<3;i++)
			if(data->orientation[i])
				data->orientation[i]=-1;
	}


	ret = of_property_read_u32(np,"orientation-y",&orig_y);
	if(ret!=0){
		dev_err(&client->dev, "get orientation-y error\n");
		return -EIO;
	}

	if(orig_y>0){
		for(i=3;i<6;i++)
			if(data->orientation[i])
				data->orientation[i]=-1;
	}


	ret = of_property_read_u32(np,"orientation-z",&orig_z);
	if(ret!=0){
		dev_err(&client->dev, "get orientation-z error\n");
		return -EIO;
	}

	if(orig_z>0){
		for(i=6;i<9;i++)
			if(data->orientation[i])
				data->orientation[i]=-1;
	}
	

	ret = of_property_read_u32(np,"compass-debug",&debug);
	if(ret!=0){
		dev_err(&client->dev, "get compass-debug error\n");
		return -EINVAL;
	}

	if(client->addr)
		data->address=client->addr;
	else
		dev_err(&client->dev, "compass-addr error\n");

	if(debug){
		printk("bus=%d,adapt_num=%d,addr=%x\n",data->bus, \
			data->adapt_num,data->address);

		for(i=0;i<size;i++)
			printk("%d ",data->orientation[i]);	
		
		printk("\n");	

	}
	return 0;
}
static int ak8963_mod_probe(struct i2c_client *client,
			   const struct i2c_device_id *devid)
{
	int ret=0;
	struct ext_slave_platform_data *pdata;
	struct ak8963_mod_private_data *private_data;
	int result = 0;

	ret = ak8963_parse_dt(client,&ak8963_data);
	if(ret< 0)
		printk("parse ak8963 dts failed\n");

	dev_info(&client->adapter->dev, "%s: %s\n", __func__, devid->name);
	printk("yemk:ak8963_mod_probe\n");
	//request gpio for COMPASS_RST
#if 0
	if (gpio_request(COMPASS_RST_PIN, "COMPASS_RST")) {
		pr_err("%s: failed to request gpio for COMPASS_RST\n", __func__);
		//return -ENODEV;
	}
	gpio_direction_output(COMPASS_RST_PIN, 1);
#else
	//if (gpio_request_one(COMPASS_RST_PIN, GPIOF_OUT_INIT_HIGH, "COMPASS_RST")) {
	//	pr_err("%s: failed to request gpio for COMPASS_RST\n", __func__);
		//return -ENODEV;
	//}
#endif

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		result = -ENODEV;
		goto out_no_free;
	}

	pdata = &ak8963_data;
	if (!pdata) {
		dev_err(&client->adapter->dev,
			"Missing platform data for slave %s\n", devid->name);
		result = -EFAULT;
		goto out_no_free;
	}

	private_data = kzalloc(sizeof(*private_data), GFP_KERNEL);
	if (!private_data) {
		result = -ENOMEM;
		goto out_no_free;
	}

	i2c_set_clientdata(client, private_data);
	private_data->client = client;
	private_data->pdata = pdata;

	result = inv_mpu_register_slave(THIS_MODULE, client, pdata,
					ak8963_get_slave_descr);
	printk(KERN_ERR "invensense: in %s, result is %d\n", __func__, result);
	if (result) {
		dev_err(&client->adapter->dev,
			"Slave registration failed: %s, %d\n",
			devid->name, result);
		goto out_free_memory;
	}
	printk("yemk:ak8963_mod_probe end\n");
	return result;

out_free_memory:
	kfree(private_data);
out_no_free:
	dev_err(&client->adapter->dev, "%s failed %d\n", __func__, result);
	return result;

}

static int ak8963_mod_remove(struct i2c_client *client)
{
	struct ak8963_mod_private_data *private_data =
		i2c_get_clientdata(client);

	dev_dbg(&client->adapter->dev, "%s\n", __func__);
	inv_mpu_unregister_slave(client, private_data->pdata,
				ak8963_get_slave_descr);

	kfree(private_data);
	return 0;
}

static const struct i2c_device_id ak8963_mod_id[] = {
	{ "ak8963", COMPASS_ID_AK8963 },
	{}
};

MODULE_DEVICE_TABLE(i2c, ak8963_mod_id);

static const struct of_device_id of_mpu_ak8963_match[] = {
	{ .compatible = "ak8963" },
	{ /* Sentinel */ }
};

static struct i2c_driver ak8963_mod_driver = {
	.class = I2C_CLASS_HWMON,
	.probe = ak8963_mod_probe,
	.remove = ak8963_mod_remove,
	.id_table = ak8963_mod_id,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "ak8963_mod",
		   .of_match_table	= of_mpu_ak8963_match,
		   },
	.address_list = normal_i2c,
};

static int __init ak8963_mod_init(void)
{
	int res = i2c_add_driver(&ak8963_mod_driver);
	pr_info("%s: Probe name %s\n", __func__, "ak8963_mod");
	if (res)
		pr_err("%s failed\n", __func__);
	return res;
}

static void __exit ak8963_mod_exit(void)
{
	pr_info("%s\n", __func__);
	i2c_del_driver(&ak8963_mod_driver);
}

module_init(ak8963_mod_init);
module_exit(ak8963_mod_exit);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Driver to integrate AK8963 sensor with the MPU");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ak8963_mod");

/**
 *  @}
 */
