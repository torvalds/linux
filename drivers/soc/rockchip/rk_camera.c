/*
 * drivers/soc/rockchip/rk_camera.c
 *
 * Copyright (C) 2017, ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/of_gpio.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_fdt.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include "rk_camera.h"

static int camio_version = KERNEL_VERSION(0, 1, 9);
module_param(camio_version, int, S_IRUGO);

static int camera_debug;
module_param(camera_debug, int, S_IRUGO | S_IWUSR);

#undef  CAMMODULE_NAME
#define CAMMODULE_NAME		"rk_cam_io"

#define ddprintk(level, fmt, arg...)					\
	do {								\
		if (camera_debug >= level)				\
			printk(KERN_WARNING "%s(%d):" fmt"\n",		\
			       CAMMODULE_NAME, __LINE__, ## arg);	\
	} while (0)

#define dprintk(format, ...)					\
	ddprintk(1, format, ## __VA_ARGS__)
#define eprintk(format, ...)					\
	printk(KERN_ERR "%s(%d):" format"\n",			\
	       CAMMODULE_NAME, __LINE__, ## __VA_ARGS__)
#define debug_printk(format, ...)				\
	ddprintk(3, format, ## __VA_ARGS__)

static int rk_sensor_io_init(void);
static int rk_sensor_io_deinit(int sensor);
static int rk_sensor_ioctrl(struct device *dev,
			    enum rk29camera_ioctrl_cmd cmd, int on);
static int rk_sensor_power(struct device *dev, int on);
static int rk_sensor_register(void);
static int rk_dts_sensor_probe(struct platform_device *pdev);
static int rk_dts_sensor_remove(struct platform_device *pdev);
static int rk_sensor_powerdown(struct device *dev, int on);

static struct rkcamera_platform_data *new_camera_head;

struct rk29camera_platform_data rk_camera_platform_data = {
	.io_init = rk_sensor_io_init,
	.io_deinit = rk_sensor_io_deinit,
	.sensor_ioctrl = rk_sensor_ioctrl,
	.sensor_register = rk_sensor_register,
};

struct rk29camera_platform_ioctl_cb sensor_ioctl_cb = {
	.sensor_power_cb = NULL,
	.sensor_reset_cb = NULL,
	.sensor_powerdown_cb = NULL,
	.sensor_flash_cb = NULL,
	.sensor_af_cb = NULL,
};

static const struct of_device_id of_match_sensor[] = {
	{.compatible = "rockchip,sensor",},
	{},
};
MODULE_DEVICE_TABLE(of, of_match_sensor);
static struct platform_driver rk_sensor_driver = {
	.driver = {
		.name	= "rk_sensor",
		.of_match_table = of_match_ptr(of_match_sensor),
	},
	.probe		= rk_dts_sensor_probe,
	.remove		= rk_dts_sensor_remove,
};

static int rk_dts_sensor_remove(struct platform_device *pdev)
{
	return 0;
}

struct rk29_camera_gpio camera_gpios;
static struct gpio_desc *rk_dts_sensor_parse_gpio(
	struct device *dev,
	struct fwnode_handle *child,
	int pltfrm_gpio,
	const char *label)
{
	struct rk29_camera_gpio *camera_gpio;
	struct rk29_camera_gpio *new_gpio;
	struct gpio_desc *gpio;

	if (IS_ERR_VALUE(pltfrm_gpio))
		return NULL;

	list_for_each_entry(camera_gpio, &camera_gpios.gpios, gpios) {
		if (camera_gpio->pltfrm_gpio == pltfrm_gpio)
			break;
	}

	if (camera_gpio->pltfrm_gpio != pltfrm_gpio) {
		dprintk("%s(%d): alloc a new gpio for pltfrm gpio %d\n",
			__func__, __LINE__, pltfrm_gpio);
		new_gpio = kzalloc(sizeof(*new_gpio), GFP_KERNEL);
		if (!new_gpio) {
			eprintk("%s(%d): alloc camera_gpio failed\n",
				__func__, __LINE__);
			return NULL;
		}

		new_gpio->pltfrm_gpio = pltfrm_gpio;
		new_gpio->count = 1;
		new_gpio->gpio_desc =
			devm_get_gpiod_from_child(dev, label, child);
		if (IS_ERR(new_gpio->gpio_desc)) {
			eprintk("gpio reset get failed.\n");
			goto err_alloc;
		}

		list_add_tail(&new_gpio->gpios, &camera_gpios.gpios);
		gpio = new_gpio->gpio_desc;
	} else {
		camera_gpio->count++;
		gpio = camera_gpio->gpio_desc;
		dprintk("%s(%d): gpio %d have get count %d\n",
			__func__, __LINE__,
			pltfrm_gpio, camera_gpio->count);
	}

	return gpio;
err_alloc:
	kfree(new_gpio);

	return NULL;
}

static int rk_dts_sensor_probe(struct platform_device *pdev)
{
	struct device_node *np, *cp;
	int sensor_num = 0, err;
	struct device *dev = &pdev->dev;
	struct rkcamera_platform_data *new_camera_list;
	struct fwnode_handle *child;
	enum of_gpio_flags flags;
	int power = INVALID_GPIO;
	int powerdown = INVALID_GPIO;
	int reset = INVALID_GPIO;
	int af = INVALID_GPIO;
	int flash = INVALID_GPIO;
	int irq = INVALID_GPIO;
	int count;

	np = dev->of_node;
	if (!np)
		return -ENODEV;

	count = device_get_child_node_count(dev);
	if (!count)
		return -ENODEV;

	INIT_LIST_HEAD(&camera_gpios.gpios);
	device_for_each_child_node(dev, child) {
		u32 flash_attach = 0, mir = 0, i2c_chl = 0, i2c_add = 0;
		u32 cif_chl = 0, mclk_rate = 0, is_front = 0;
		u32 resolution = 0, powerup_sequence = 0;
		int pwr_active = 0, rst_active = 0, pwdn_active = 0;
		int flash_active = 0;
		int orientation = 0;

		char sensor_name[20] = {0};
		char *name = NULL;
		const char *status = NULL;

		struct rkcamera_platform_data *new_camera;

		cp = to_of_node(child);

		of_property_read_string(cp, "status", &status);
		if (status && !strcmp(status, "disabled"))
			continue;

		strcpy(sensor_name, cp->name);
		name = sensor_name;
		if (strstr(sensor_name, "_") != NULL)
			name = strsep(&name, "_");

		new_camera = kzalloc(sizeof(struct rkcamera_platform_data),
				     GFP_KERNEL);
		if (!new_camera)
			return -ENOMEM;

		if (!sensor_num) {
			new_camera_head = new_camera;
			rk_camera_platform_data.register_dev_new = new_camera_head;
			new_camera_list = new_camera;
		}
		sensor_num++;
		new_camera_list->next_camera = new_camera;
		new_camera_list = new_camera;

		if (of_property_read_u32(cp, "flash_attach", &flash_attach))
			dprintk("%s:Get %s rockchip,flash_attach failed!\n",
				__func__, cp->name);

		if (of_property_read_u32(cp, "mir", &mir))
			dprintk("%s:Get %s rockchip,mir failed!\n",
				__func__, cp->name);

		if (of_property_read_u32(cp, "i2c_chl", &i2c_chl))
			dprintk("%s:Get %s rockchip,i2c_chl failed!\n",
				__func__, cp->name);

		if (of_property_read_u32(cp, "cif_chl", &cif_chl))
			dprintk("%s:Get %s rockchip,cif_chl failed!\n",
				__func__, cp->name);

		if (of_property_read_u32(cp, "mclk_rate", &mclk_rate))
			dprintk("%s:Get %s rockchip,mclk_rate failed!\n",
				__func__, cp->name);

		if (of_property_read_u32(cp, "is_front", &is_front))
			dprintk("%s:Get %s rockchip,is_front failed!\n",
				__func__, cp->name);

		if (of_property_read_u32(cp, "pwdn_active", &pwdn_active))
			dprintk("%s:Get %s pwdn_active failed!\n",
				__func__, cp->name);

		if (of_property_read_u32(cp, "pwr_active", &pwr_active))
			dprintk("%s:Get %s pwr_active failed!\n",
				__func__, cp->name);

		if (of_property_read_u32(cp, "rst_active", &rst_active))
			dprintk("%s:Get %s rst_active failed!\n",
				__func__, cp->name);

		if (of_property_read_u32(cp, "flash_active", &flash_active))
			dprintk("%s:Get %s flash_active failed!\n",
				__func__, cp->name);

		if (of_property_read_u32(cp, "i2c_add", &i2c_add))
			dprintk("%s:Get %s rockchip,i2c_add failed!\n",
				__func__, cp->name);

		if (of_property_read_u32(cp, "resolution", &resolution))
			dprintk("%s:Get %s rockchip,resolution failed!\n",
				__func__, cp->name);

		if (of_property_read_u32(cp, "powerup_sequence",
					 &powerup_sequence))
			dprintk("%s:Get %s rockchip,powerup_sequence failed!\n",
				__func__, cp->name);

		if (of_property_read_u32(cp, "orientation", &orientation))
			dprintk("%s:Get %s rockchip,orientation failed!\n",
				__func__, cp->name);

		of_property_read_u32_array(
			cp,
			"rockchip,camera-module-defrect0",
			(unsigned int *)&new_camera->defrects[0],
			6);
		of_property_read_string(
			cp,
			"rockchip,camera-module-interface0",
			&new_camera->defrects[0].interface);
		of_property_read_u32_array(
			cp,
			"rockchip,camera-module-defrect1",
			(unsigned int *)&new_camera->defrects[1],
			6);
		of_property_read_string(
			cp,
			"rockchip,camera-module-interface1",
			&new_camera->defrects[1].interface);
		of_property_read_u32_array(
			cp,
			"rockchip,camera-module-defrect2",
			(unsigned int *)&new_camera->defrects[2],
			6);
		of_property_read_string(
			cp,
			"rockchip,camera-module-interface2",
			&new_camera->defrects[2].interface);
		of_property_read_u32_array(
			cp,
			"rockchip,camera-module-defrect3",
			(unsigned int *)&new_camera->defrects[3],
			6);
		of_property_read_string(
			cp,
			"rockchip,camera-module-interface3",
			&new_camera->defrects[3].interface);
		of_property_read_u32_array(
			cp,
			"rockchip,camera-module-channel",
			(unsigned int *)&new_camera->channel_info,
			2);
		of_property_read_string(
			cp,
			"rockchip,camera-module-ain-1",
			&new_camera->channel_info.channel_info[0]);
		of_property_read_string(
			cp,
			"rockchip,camera-module-ain-2",
			&new_camera->channel_info.channel_info[1]);
		of_property_read_string(
			cp,
			"rockchip,camera-module-ain-3",
			&new_camera->channel_info.channel_info[2]);
		of_property_read_string(
			cp,
			"rockchip,camera-module-ain-4",
			&new_camera->channel_info.channel_info[3]);
		of_property_read_string(
			cp,
			"rockchip,camera-module-ain-5",
			&new_camera->channel_info.channel_info[4]);

		strcpy(new_camera->dev.i2c_cam_info.type, name);
		new_camera->dev.i2c_cam_info.addr = i2c_add >> 1;
		new_camera->dev.desc_info.host_desc.bus_id = RK29_CAM_PLATFORM_DEV_ID + cif_chl;
		new_camera->dev.desc_info.host_desc.i2c_adapter_id = i2c_chl;
		new_camera->dev.desc_info.host_desc.module_name = name;
		new_camera->dev.device_info.name = "soc-camera-pdrv";
		if (is_front)
			sprintf(new_camera->dev_name, "%s_%s", name, "front");
		else
			sprintf(new_camera->dev_name, "%s_%s", name, "back");
		new_camera->dev.device_info.dev.init_name = (const char *)&new_camera->dev_name[0];

		power = of_get_named_gpio_flags(cp, "power-gpios",
						0, &flags);
		powerdown = of_get_named_gpio_flags(cp, "powerdown-gpios",
						    0, &flags);
		reset = of_get_named_gpio_flags(cp, "reset-gpios",
						0, &flags);
		af = of_get_named_gpio_flags(cp, "af-gpios",
					     0, &flags);
		flash = of_get_named_gpio_flags(cp, "flash-gpios",
						0, &flags);
		irq = of_get_named_gpio_flags(cp, "irq-gpios",
					      0, &flags);

		new_camera->io.gpio_reset =
			rk_dts_sensor_parse_gpio(dev, child, reset, "reset");

		new_camera->io.gpio_power =
			rk_dts_sensor_parse_gpio(dev, child, power, "power");

		new_camera->io.gpio_powerdown =
			rk_dts_sensor_parse_gpio(dev, child,
						 powerdown, "powerdown");

		new_camera->io.gpio_af =
			rk_dts_sensor_parse_gpio(dev, child, af, "af");

		new_camera->io.gpio_flash =
			rk_dts_sensor_parse_gpio(dev, child, flash, "flash");

		new_camera->io.gpio_irq =
			rk_dts_sensor_parse_gpio(dev, child, irq, "irq");

		new_camera->io.reset = reset;
		new_camera->io.power = power;
		new_camera->io.powerdown = powerdown;
		new_camera->io.af = af;
		new_camera->io.flash = flash;
		new_camera->io.irq = irq;
		new_camera->io.gpio_flag = ((pwr_active & 0x01) << RK29_CAM_POWERACTIVE_BITPOS) |
					    ((rst_active & 0x01) << RK29_CAM_RESETACTIVE_BITPOS) |
					    ((pwdn_active & 0x01) << RK29_CAM_POWERDNACTIVE_BITPOS) |
					    ((flash_active & 0x01) << RK29_CAM_FLASHACTIVE_BITPOS);
		new_camera->orientation = orientation;
		new_camera->resolution = resolution;
		new_camera->mirror = mir;
		new_camera->flash = flash_attach;
		new_camera->pwdn_info = ((pwdn_active & 0x10) | 0x01);
		new_camera->if_powerdown = true;
		new_camera->powerup_sequence = powerup_sequence;
		new_camera->mclk_rate = mclk_rate;
		new_camera->of_node = cp;

		new_camera->powerdown_pmu_name = NULL;
		new_camera->power_pmu_name1 = NULL;
		new_camera->power_pmu_name2 = NULL;
		new_camera->powerdown_pmu_voltage = 0;
		new_camera->power_pmu_name1 = 0;
		new_camera->power_pmu_name2 = 0;

		err = of_property_read_string(cp, "rockchip,powerdown_pmu", &(new_camera->powerdown_pmu_name));
		if(err < 0)
			dprintk("Get rockchip,powerdown_pmu failed\n");

		err = of_property_read_string(cp,"rockchip,power_pmu_name1", &(new_camera->power_pmu_name1));
		if(err < 0)
			dprintk("Get rockchip,power_pmu_name1 failed\n");

		err = of_property_read_string(cp,"rockchip,power_pmu_name2", &(new_camera->power_pmu_name2));
		if(err < 0)
			dprintk("rockchip,power_pmu_name2 failed\n");

		if (of_property_read_u32(cp, "rockchip,powerdown_pmu_voltage", &(new_camera->powerdown_pmu_voltage)))
			dprintk("%s:Get %s rockchip,powerdown_pmu_voltage failed!\n", __func__, cp->name);

		if (of_property_read_u32(cp, "rockchip,power_pmu_voltage1", &(new_camera->power_pmu_voltage1)))
			dprintk("%s:Get %s rockchip,power_pmu_voltage1 failed!\n", __func__, cp->name);

		if (of_property_read_u32(cp, "rockchip,power_pmu_voltage2", &(new_camera->power_pmu_voltage2)))
			dprintk("%s:Get %s rockchip,power_pmu_voltage2 failed!\n", __func__,  cp->name);

		debug_printk("******************* /n i2c_add = %x\n", new_camera->dev.i2c_cam_info.addr << 1);
		debug_printk("******************* /n i2c_chl = %d\n", new_camera->dev.desc_info.host_desc.i2c_adapter_id);
		debug_printk("******************* /n init_name = %s\n", new_camera->dev.device_info.dev.init_name);
		debug_printk("******************* /n dev_name = %s\n", new_camera->dev_name);
		debug_printk("******************* /n module_name = %s\n", new_camera->dev.desc_info.host_desc.module_name);
	}
	new_camera_list->next_camera = NULL;

	return 0;
}

static int sensor_power_default_cb (struct rk29camera_gpio_res *res, int on)
{
	struct gpio_desc *camera_power = res->gpio_power;
	int camera_ioflag = res->gpio_flag;
	int camera_io_init = res->gpio_init;
	int ret = 0;

	struct regulator *ldo_18,*ldo_28;
	struct rkcamera_platform_data *dev =
		container_of(res, struct rkcamera_platform_data, io);

	int power_pmu_voltage1 = dev->power_pmu_voltage1;
	int power_pmu_voltage2 = dev->power_pmu_voltage2;
	const char *camera_power_pmu_name1 = dev->power_pmu_name1;
	const char *camera_power_pmu_name2 = dev->power_pmu_name2;;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

	if (camera_power_pmu_name1 != NULL) {
		ldo_28 = regulator_get(NULL, camera_power_pmu_name1); /* vcc18_cif */
	        if (on) {
			regulator_set_voltage(ldo_28, power_pmu_voltage1, power_pmu_voltage1);
			ret = regulator_enable(ldo_28);
			regulator_put(ldo_28);

			msleep(10);
		} else {
			if (regulator_is_enabled(ldo_28) > 0)
				regulator_disable(ldo_28);
			regulator_put(ldo_28);
		}
	}

	if(camera_power_pmu_name2 != NULL) {
		ldo_18 = regulator_get(NULL, camera_power_pmu_name2); /* vcc18_cif */
        if (on) {
			regulator_set_voltage(ldo_18, power_pmu_voltage2, power_pmu_voltage2);
			//regulator_set_suspend_voltage(ldo, 1800000);
			ret = regulator_enable(ldo_18);
			regulator_put(ldo_18);

			msleep(10);
		} else {
			if (regulator_is_enabled(ldo_18) > 0)
				regulator_disable(ldo_18);
			regulator_put(ldo_18);
		}
	}

	if (camera_power) {
		if (camera_io_init & RK29_CAM_POWERACTIVE_MASK) {
			if (on) {
				gpiod_set_value(camera_power,
					       ((camera_ioflag & RK29_CAM_POWERACTIVE_MASK) >>
						RK29_CAM_POWERACTIVE_BITPOS));
				dprintk("%s PowerPin ..PinLevel = %x",
					res->dev_name,
					((camera_ioflag & RK29_CAM_POWERACTIVE_MASK) >>
					 RK29_CAM_POWERACTIVE_BITPOS));
				msleep(10);
			} else {
				gpiod_set_value(camera_power,
					       (((~camera_ioflag) & RK29_CAM_POWERACTIVE_MASK) >>
						RK29_CAM_POWERACTIVE_BITPOS));
				dprintk("%s PowerPin ..PinLevel = %x",
					res->dev_name,
					(((~camera_ioflag) & RK29_CAM_POWERACTIVE_MASK) >>
					 RK29_CAM_POWERACTIVE_BITPOS));
			}
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			eprintk("%s PowerPin request failed!", res->dev_name);
		}
	} else {
		ret = RK29_CAM_EIO_INVALID;
	}

	return ret;
}

static int sensor_reset_default_cb (struct rk29camera_gpio_res *res, int on)
{
	struct gpio_desc *camera_reset = res->gpio_reset;
	int camera_ioflag = res->gpio_flag;
	int camera_io_init = res->gpio_init;
	int ret = 0;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__, __FUNCTION__);

	if (camera_reset) {
		if (camera_io_init & RK29_CAM_RESETACTIVE_MASK) {
			if (on) {
				gpiod_set_value(camera_reset,
					       ((camera_ioflag & RK29_CAM_RESETACTIVE_MASK) >>
						RK29_CAM_RESETACTIVE_BITPOS));
				dprintk("%s ResetPin ..PinLevel = %x",
					res->dev_name,
					((camera_ioflag & RK29_CAM_RESETACTIVE_MASK) >>
					 RK29_CAM_RESETACTIVE_BITPOS));
			} else {
				gpiod_set_value(camera_reset,
					       (((~camera_ioflag) & RK29_CAM_RESETACTIVE_MASK) >>
						RK29_CAM_RESETACTIVE_BITPOS));
				dprintk("%s ResetPin ..PinLevel = %x",
					res->dev_name,
					(((~camera_ioflag) & RK29_CAM_RESETACTIVE_MASK) >>
					 RK29_CAM_RESETACTIVE_BITPOS));
			}
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			dprintk("%s ResetPin request failed!",
				res->dev_name);
		}
	} else {
		ret = RK29_CAM_EIO_INVALID;
	}

	return ret;
}

static int sensor_powerdown_default_cb (struct rk29camera_gpio_res *res, int on)
{
	struct gpio_desc *camera_powerdown = res->gpio_powerdown;
	int camera_ioflag = res->gpio_flag;
	int camera_io_init = res->gpio_init;
	int ret = 0;

	struct regulator *powerdown_pmu;
	struct rkcamera_platform_data *dev =
		container_of(res, struct rkcamera_platform_data, io);
	int powerdown_pmu_voltage = dev->powerdown_pmu_voltage;
	const char *powerdown_pmu_name = dev->powerdown_pmu_name;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__, __FUNCTION__);

	if (powerdown_pmu_name != NULL) {
		powerdown_pmu = regulator_get(NULL, powerdown_pmu_name);
		if (on) {
			regulator_set_voltage(powerdown_pmu, powerdown_pmu_voltage, powerdown_pmu_voltage);
			ret = regulator_enable(powerdown_pmu);
			regulator_put(powerdown_pmu);
		} else {
			if (regulator_is_enabled(powerdown_pmu) > 0)
				regulator_disable(powerdown_pmu);
			regulator_put(powerdown_pmu);
		}
	}

	if (camera_powerdown) {
		if (camera_io_init & RK29_CAM_POWERDNACTIVE_MASK) {
			if (on) {
				gpiod_set_value(camera_powerdown,
					       ((camera_ioflag & RK29_CAM_POWERDNACTIVE_MASK) >>
						RK29_CAM_POWERDNACTIVE_BITPOS));
				dprintk("%s PowerDownPin ..PinLevel = %x",
					res->dev_name,
					((camera_ioflag & RK29_CAM_POWERDNACTIVE_MASK) >>
					 RK29_CAM_POWERDNACTIVE_BITPOS));
			} else {
				gpiod_set_value(camera_powerdown,
					       (((~camera_ioflag) & RK29_CAM_POWERDNACTIVE_MASK) >>
						RK29_CAM_POWERDNACTIVE_BITPOS));
				dprintk("%s PowerDownPin ..PinLevel = %x",
					res->dev_name,
					(((~camera_ioflag) & RK29_CAM_POWERDNACTIVE_MASK) >>
					 RK29_CAM_POWERDNACTIVE_BITPOS));
			}
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			dprintk("%s PowerDownPin request failed!",
				res->dev_name);
		}
	}else {
		ret = RK29_CAM_EIO_INVALID;
	}

	return ret;
}


static int sensor_flash_default_cb (struct rk29camera_gpio_res *res, int on)
{
	struct gpio_desc *camera_flash = res->gpio_flash;
	int camera_ioflag = res->gpio_flag;
	int camera_io_init = res->gpio_init;
	int ret = 0;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

	if (camera_flash) {
		if (camera_io_init & RK29_CAM_FLASHACTIVE_MASK) {
			switch (on)
			{
				case Flash_Off:
				{
					gpiod_set_value(camera_flash,
						       (((~camera_ioflag) & RK29_CAM_FLASHACTIVE_MASK) >>
							RK29_CAM_FLASHACTIVE_BITPOS));
					dprintk("%s FlashPin ..PinLevel = %x",
						res->dev_name,
						(((~camera_ioflag) & RK29_CAM_FLASHACTIVE_MASK) >>
						 RK29_CAM_FLASHACTIVE_BITPOS));
					break;
				}

				case Flash_On:
				{
					gpiod_set_value(camera_flash,
						       ((camera_ioflag & RK29_CAM_FLASHACTIVE_MASK) >>
							RK29_CAM_FLASHACTIVE_BITPOS));
					dprintk("%s FlashPin ..PinLevel = %x",
						res->dev_name,
						((camera_ioflag & RK29_CAM_FLASHACTIVE_MASK) >>
						 RK29_CAM_FLASHACTIVE_BITPOS));
					break;
				}

				case Flash_Torch:
				{
					gpiod_set_value(camera_flash,
						       ((camera_ioflag & RK29_CAM_FLASHACTIVE_MASK) >>
							RK29_CAM_FLASHACTIVE_BITPOS));
					dprintk("%s FlashPin ..PinLevel = %x",
						res->dev_name,
						((camera_ioflag & RK29_CAM_FLASHACTIVE_MASK) >>
						 RK29_CAM_FLASHACTIVE_BITPOS));
					break;
				}

				default:
				{
					eprintk("%s Flash command(%d) is invalidate",
						res->dev_name, on);
					break;
				}
			}
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			eprintk("%s FlashPin request failed!",
				res->dev_name);
		}
	} else {
		ret = RK29_CAM_EIO_INVALID;
	}

	return ret;
}

static int sensor_afpower_default_cb (struct rk29camera_gpio_res *res, int on)
{
	struct gpio_desc *camera_af = res->gpio_af;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__, __FUNCTION__);

	if (camera_af) {
		gpiod_set_value(camera_af, on);
	}

	return 0;
}

static int _rk_sensor_io_init_(struct rkcamera_platform_data *new_camera)
{
	int ret = 0;
	struct rk29camera_gpio_res *gpio_res = &new_camera->io;
	struct gpio_desc *camera_reset;
	struct gpio_desc *camera_power;
	struct gpio_desc *camera_powerdown;
	struct gpio_desc *camera_flash;
	struct gpio_desc *camera_af;
	unsigned int camera_ioflag;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__, __FUNCTION__);

	camera_reset = gpio_res->gpio_reset;
	camera_power = gpio_res->gpio_power;
	camera_powerdown = gpio_res->gpio_powerdown;
	camera_flash = gpio_res->gpio_flash;
	camera_af = gpio_res->gpio_af;
	camera_ioflag = gpio_res->gpio_flag;
	gpio_res->gpio_init = 0;

	if (camera_power) {
		gpio_res->gpio_init |= RK29_CAM_POWERACTIVE_MASK;
		gpiod_set_value(camera_power,
			       (((~camera_ioflag) & RK29_CAM_POWERACTIVE_MASK) >>
				 RK29_CAM_POWERACTIVE_BITPOS));
		gpiod_direction_output(camera_power,
				      (((~camera_ioflag) & RK29_CAM_POWERACTIVE_MASK) >>
					RK29_CAM_POWERACTIVE_BITPOS));

		dprintk("%s power pin init success(0x%x)",
			gpio_res->dev_name,
			(((~camera_ioflag) & RK29_CAM_POWERACTIVE_MASK) >>
			 RK29_CAM_POWERACTIVE_BITPOS));
	}

	if (camera_reset) {
		gpio_res->gpio_init |= RK29_CAM_RESETACTIVE_MASK;
		gpiod_set_value(camera_reset,
			       ((camera_ioflag & RK29_CAM_RESETACTIVE_MASK) >>
				RK29_CAM_RESETACTIVE_BITPOS));
		gpiod_direction_output(camera_reset,
				      ((camera_ioflag & RK29_CAM_RESETACTIVE_MASK) >>
				       RK29_CAM_RESETACTIVE_BITPOS));

		dprintk("%s reset pin init success(0x%x)",
			gpio_res->dev_name,
			((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
	}

	if (camera_powerdown) {
		debug_printk("gpio_res->dev_name: %s", gpio_res->dev_name);
		if (strstr(gpio_res->dev_name, "tp2825") || strstr(gpio_res->dev_name, "adv7181")) {
			debug_printk("don't control powerdown!\n");
			new_camera->if_powerdown = false;
		}

		gpio_res->gpio_init |= RK29_CAM_POWERDNACTIVE_MASK;
		gpiod_set_value(camera_powerdown, ((camera_ioflag & RK29_CAM_POWERDNACTIVE_MASK) >> RK29_CAM_POWERDNACTIVE_BITPOS));
		gpiod_direction_output(camera_powerdown, ((camera_ioflag & RK29_CAM_POWERDNACTIVE_MASK) >> RK29_CAM_POWERDNACTIVE_BITPOS));

		if (!new_camera->if_powerdown) {
			gpiod_set_value(camera_powerdown, ((camera_ioflag & RK29_CAM_POWERDNACTIVE_MASK) >> RK29_CAM_POWERDNACTIVE_BITPOS));
			msleep(50);
			gpiod_set_value(camera_powerdown, ((~camera_ioflag & RK29_CAM_POWERDNACTIVE_MASK) >> RK29_CAM_POWERDNACTIVE_BITPOS));
		}

		dprintk("%s powerdown pin init success(0x%x)",
			gpio_res->dev_name,
			((camera_ioflag & RK29_CAM_POWERDNACTIVE_MASK) >> RK29_CAM_POWERDNACTIVE_BITPOS));
	}

	if (camera_flash) {
		gpio_res->gpio_init |= RK29_CAM_FLASHACTIVE_MASK;
		gpiod_set_value(camera_flash, ((~camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));   //  falsh off
		gpiod_direction_output(camera_flash, ((~camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));

		dprintk("%s flash pin init success(0x%x)",
			gpio_res->dev_name,
			((camera_ioflag & RK29_CAM_FLASHACTIVE_MASK) >> RK29_CAM_FLASHACTIVE_BITPOS));
	}

	if (camera_af) {
		gpio_res->gpio_init |= RK29_CAM_AFACTIVE_MASK;
		//gpio_direction_output(camera_af, ((camera_ioflag&RK29_CAM_AFACTIVE_MASK)>>RK29_CAM_AFACTIVE_BITPOS));
		dprintk("%s af pin init success",gpio_res->dev_name);
	}

	return ret;
}

static int _rk_sensor_io_deinit_(struct rk29camera_gpio_res *gpio_res)
{
	struct gpio_desc *camera_reset;
	struct gpio_desc *camera_power;
	struct gpio_desc *camera_powerdown;
	struct gpio_desc *camera_flash;
	struct gpio_desc *camera_af;

	debug_printk("/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n",
		     __FILE__, __LINE__, __FUNCTION__);

	camera_reset = gpio_res->gpio_reset;
	camera_power = gpio_res->gpio_power;
	camera_powerdown = gpio_res->gpio_powerdown;
	camera_flash = gpio_res->gpio_flash;
	camera_af = gpio_res->gpio_af;

	if (gpio_res->gpio_init & RK29_CAM_POWERACTIVE_MASK) {
		if (camera_power) {
			gpiod_direction_input(camera_power);
			gpiod_put(camera_power);
		}
	}

	if (gpio_res->gpio_init & RK29_CAM_RESETACTIVE_MASK) {
		if (camera_reset) {
			gpiod_direction_input(camera_reset);
			gpiod_put(camera_reset);
		}
	}

	if (gpio_res->gpio_init & RK29_CAM_POWERDNACTIVE_MASK) {
		if (camera_powerdown) {
			gpiod_direction_input(camera_powerdown);
			gpiod_put(camera_powerdown);
		}
	}

	if (gpio_res->gpio_init & RK29_CAM_FLASHACTIVE_MASK) {
		if (camera_flash) {
			gpiod_direction_input(camera_flash);
			gpiod_put(camera_flash);
		}
	}

	if (gpio_res->gpio_init & RK29_CAM_AFACTIVE_MASK) {
		if (camera_af) {
			/* gpiod_direction_input(camera_af);*/
			gpiod_put(camera_af);
		}
	}
	gpio_res->gpio_init = 0;

	return 0;
}

static int rk_sensor_io_init(void)
{
	static bool is_init = false;

	struct rkcamera_platform_data *new_camera;
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__, __FUNCTION__);

	if (is_init)
		return 0;
	else
		is_init = true;

	if (sensor_ioctl_cb.sensor_power_cb == NULL)
		sensor_ioctl_cb.sensor_power_cb = sensor_power_default_cb;
	if (sensor_ioctl_cb.sensor_reset_cb == NULL)
		sensor_ioctl_cb.sensor_reset_cb = sensor_reset_default_cb;
	if (sensor_ioctl_cb.sensor_powerdown_cb == NULL)
		sensor_ioctl_cb.sensor_powerdown_cb = sensor_powerdown_default_cb;
	if (sensor_ioctl_cb.sensor_flash_cb == NULL)
		sensor_ioctl_cb.sensor_flash_cb = sensor_flash_default_cb;
	if (sensor_ioctl_cb.sensor_af_cb == NULL)
		sensor_ioctl_cb.sensor_af_cb = sensor_afpower_default_cb;

	new_camera = new_camera_head;
	while(new_camera != NULL)
	{
		if (_rk_sensor_io_init_(new_camera) < 0)
			_rk_sensor_io_deinit_(&new_camera->io);
		new_camera = new_camera->next_camera;
	}
	return 0;
}

static int rk_sensor_io_deinit(int sensor)
{
	struct rkcamera_platform_data *new_camera;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

	new_camera = new_camera_head;
	while(new_camera != NULL)
	{
		_rk_sensor_io_deinit_(&new_camera->io);
		new_camera = new_camera->next_camera;
	}

	return 0;
}
static int rk_sensor_ioctrl(struct device *dev,enum rk29camera_ioctrl_cmd cmd, int on)
{
	struct rk29camera_gpio_res *res = NULL;
	struct rkcamera_platform_data *new_cam_dev = NULL;
	struct rk29camera_platform_data* plat_data = &rk_camera_platform_data;
	int ret = RK29_CAM_IO_SUCCESS, i = 0;
	struct soc_camera_desc *dev_icl = NULL;
	struct rkcamera_platform_data *new_camera;
	debug_printk("/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__, __FUNCTION__);

	if (res == NULL) {
		new_camera = new_camera_head;
		while(new_camera != NULL) {
			if (strcmp(new_camera->dev_name, dev_name(dev)) == 0) {
				res = (struct rk29camera_gpio_res *)&new_camera->io;
				new_cam_dev = &new_camera[i];
				dev_icl = &new_camera->dev.desc_info;
				break;
			}
			new_camera = new_camera->next_camera;
		}
	}

	if (res == NULL) {
		eprintk("%s is not regisiterd in rk29_camera_platform_data!!",dev_name(dev));
		ret = RK29_CAM_EIO_INVALID;
		goto rk_sensor_ioctrl_end;
	}

	switch (cmd)
	{
		case Cam_Power:
		{
			if (sensor_ioctl_cb.sensor_power_cb) {
				ret = sensor_ioctl_cb.sensor_power_cb(res, on);
				ret = (ret != RK29_CAM_EIO_INVALID)?ret:0;     /* ddl@rock-chips.com: v0.1.1 */
			} else {
				eprintk("sensor_ioctl_cb.sensor_power_cb is NULL");
				WARN_ON(1);
			}

			break;
		}

		case Cam_Reset:
		{
			if (sensor_ioctl_cb.sensor_reset_cb) {
				ret = sensor_ioctl_cb.sensor_reset_cb(res, on);

				ret = (ret != RK29_CAM_EIO_INVALID) ? ret : 0;
			} else {
				eprintk( "sensor_ioctl_cb.sensor_reset_cb is NULL");
				WARN_ON(1);
			}
			break;
		}

		case Cam_PowerDown:
		{
			if (sensor_ioctl_cb.sensor_powerdown_cb) {
				if (new_camera->if_powerdown)
					ret = sensor_ioctl_cb.sensor_powerdown_cb(res, on);
			} else {
				eprintk("sensor_ioctl_cb.sensor_powerdown_cb is NULL");
				WARN_ON(1);
			}
			break;
		}

		case Cam_Flash:
		{
			if (sensor_ioctl_cb.sensor_flash_cb) {
				ret = sensor_ioctl_cb.sensor_flash_cb(res, on);
			} else {
				eprintk( "sensor_ioctl_cb.sensor_flash_cb is NULL!");
				WARN_ON(1);
			}
			break;
		}

		case Cam_Af:
		{
			if (sensor_ioctl_cb.sensor_af_cb) {
				ret = sensor_ioctl_cb.sensor_af_cb(res, on);
			} else {
				eprintk( "sensor_ioctl_cb.sensor_af_cb is NULL!");
				WARN_ON(1);
			}
			break;
		}

		case Cam_Mclk:
		{
			if (plat_data->sensor_mclk && dev_icl) {
				plat_data->sensor_mclk(dev_icl->host_desc.bus_id,(on!=0)?1:0,on);
			} else {
				eprintk("%s(%d): sensor_mclk(%p) or dev_icl(%p) is NULL",
					__FUNCTION__,__LINE__,plat_data->sensor_mclk,dev_icl);
			}
			break;
		}

		default:
		{
			eprintk("%s cmd(0x%x) is unknown!", __FUNCTION__, cmd);
			break;
		}
	}

rk_sensor_ioctrl_end:
	return ret;
}

static int rk_sensor_pwrseq(struct device *dev,int powerup_sequence, int on, int mclk_rate)
{
	int ret =0;
	int i, powerup_type;

	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

	for (i = 0; i < SENSOR_PWRSEQ_CNT; i++) {

		if (on == 1)
			powerup_type = SENSOR_PWRSEQ_GET(powerup_sequence, i);
		else
			powerup_type = SENSOR_PWRSEQ_GET(powerup_sequence, SENSOR_PWRSEQ_CNT - 1 - i);

		switch (powerup_type)
		{
			case SENSOR_PWRSEQ_AVDD:
			case SENSOR_PWRSEQ_DOVDD:
			case SENSOR_PWRSEQ_DVDD:
			case SENSOR_PWRSEQ_PWR:
			{
				ret = rk_sensor_ioctrl(dev, Cam_Power, on);
				if (ret < 0) {
					eprintk("SENSOR_PWRSEQ_PWR failed");
				} else {
					msleep(10);
					dprintk("SensorPwrSeq-power: %d", on);
				}
				break;
			}

			case SENSOR_PWRSEQ_HWRST:
			{
				if (!on) {
					rk_sensor_ioctrl(dev, Cam_Reset, 1);
				} else {
					ret = rk_sensor_ioctrl(dev, Cam_Reset, 1);
					msleep(2);
					ret |= rk_sensor_ioctrl(dev, Cam_Reset, 0);
				}
				if (ret < 0) {
					eprintk("SENSOR_PWRSEQ_HWRST failed");
				} else {
					dprintk("SensorPwrSeq-reset: %d", on);
				}
				break;
			}

			case SENSOR_PWRSEQ_PWRDN:
			{
				ret = rk_sensor_ioctrl(dev, Cam_PowerDown, !on);
				if (ret < 0) {
					eprintk("SENSOR_PWRSEQ_PWRDN failed");
				} else {
					dprintk("SensorPwrSeq-power down: %d", !on);
				}
				break;
			}

			case SENSOR_PWRSEQ_CLKIN:
			{
				ret = rk_sensor_ioctrl(dev, Cam_Mclk, (on ? mclk_rate : on));
				if (ret<0) {
					eprintk("SENSOR_PWRSEQ_CLKIN failed");
				} else {
					dprintk("SensorPwrSeq-clock: %d", on);
				}
				break;
			}

			default:
				break;
		}
	}

	return ret;
}

static int rk_sensor_power(struct device *dev, int on)
{
	int powerup_sequence,mclk_rate;

	struct rk29camera_platform_data* plat_data = &rk_camera_platform_data;
	struct rk29camera_gpio_res *dev_io = NULL;
	struct rkcamera_platform_data *new_camera=NULL, *new_device=NULL;
	bool real_pwroff = true;
	int ret = 0;

	debug_printk("/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__, __FUNCTION__);

	new_camera = plat_data->register_dev_new;

	while (new_camera != NULL) {
		if (new_camera->io.gpio_powerdown && new_camera->if_powerdown) {
			gpiod_direction_output(new_camera->io.gpio_powerdown,
				((new_camera->io.gpio_flag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
		}
		debug_printk("new_camera->dev_name= %s \n", new_camera->dev_name);
		debug_printk("dev_name(dev)= %s \n", dev_name(dev));
		if (strcmp(new_camera->dev_name,dev_name(dev))) {
			debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i\n", __FILE__, __LINE__);
			if (sensor_ioctl_cb.sensor_powerdown_cb && on)
			{
				debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i\n", __FILE__, __LINE__);
				sensor_ioctl_cb.sensor_powerdown_cb(&new_camera->io,1);
			}
		} else {
			new_device = new_camera;
			dev_io = &new_camera->io;
			debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i\n", __FILE__, __LINE__);
			if (!Sensor_Support_DirectResume(new_camera->pwdn_info))
				real_pwroff = true;
			else
				real_pwroff = false;
		}
		new_camera = new_camera->next_camera;
	}

	if (new_device != NULL) {
		powerup_sequence = new_device->powerup_sequence;
		if ((new_device->mclk_rate == 24) || (new_device->mclk_rate == 48))
			mclk_rate = new_device->mclk_rate*1000000;
		else
			mclk_rate = 24000000;
	} else {
		powerup_sequence = sensor_PWRSEQ_DEFAULT;
		mclk_rate = 24000000;
	}

	if (on) {
		debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i\n", __FILE__, __LINE__);
		rk_sensor_pwrseq(dev, powerup_sequence, on,mclk_rate);
	} else {
		if (real_pwroff) {
			if (rk_sensor_pwrseq(dev, powerup_sequence, on, mclk_rate) < 0)		/* ddl@rock-chips.com: v0.1.5 */
				goto PowerDown;

			/*ddl@rock-chips.com: all power down switch to Hi-Z after power off*/
			new_camera = plat_data->register_dev_new;
			while (new_camera != NULL) {
				if ((new_camera->io.gpio_powerdown) && new_camera->if_powerdown) {
					gpiod_direction_input(new_camera->io.gpio_powerdown);
				}
				new_camera->pwdn_info |= 0x01;
				new_camera = new_camera->next_camera;
			}
		} else {
PowerDown:
			rk_sensor_ioctrl(dev, Cam_PowerDown, !on);

			rk_sensor_ioctrl(dev, Cam_Mclk, 0);
		}

		mdelay(10);/* ddl@rock-chips.com: v0.1.3 */
	}
	return ret;
}

#if 0
static int rk_sensor_reset(struct device *dev)
{
#if 0
	rk_sensor_ioctrl(dev,Cam_Reset,1);
	msleep(2);
	rk_sensor_ioctrl(dev,Cam_Reset,0);
#else
    /*
    *ddl@rock-chips.com : the rest function invalidate, because this operate is put together in rk_sensor_power;
    */
#endif
	return 0;
}
#endif

static int rk_sensor_powerdown(struct device *dev, int on)
{
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

	return rk_sensor_ioctrl(dev,Cam_PowerDown,on);
}

int rk_sensor_register(void)
{
	int i;
	struct rkcamera_platform_data *new_camera;

	i = 0;
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__, __FUNCTION__);

	new_camera = new_camera_head;

	while (new_camera != NULL) {
		if (new_camera->dev.i2c_cam_info.addr == INVALID_VALUE) {
			WARN(1, KERN_ERR "%s(%d): new_camera[%d] i2c addr is invalidate!",
				__FUNCTION__,__LINE__,i);
			continue;
		}
		sprintf(new_camera->dev_name,"%s_%d",new_camera->dev.device_info.dev.init_name,i+3);
		new_camera->dev.device_info.dev.init_name =(const char*)&new_camera->dev_name[0];
		new_camera->io.dev_name =(const char*)&new_camera->dev_name[0];
		if (new_camera->orientation == INVALID_VALUE) {
			if (strstr(new_camera->dev_name,  "back")) {
				new_camera->orientation = 90;
			} else {
				new_camera->orientation = 270;
			}
		}
		/* ddl@rock-chips.com: v0.1.3 */
		if ((new_camera->fov_h <= 0) || (new_camera->fov_h>360))
			new_camera->fov_h = 100;

		if ((new_camera->fov_v <= 0) || (new_camera->fov_v>360))
			new_camera->fov_v = 100;

		new_camera->dev.desc_info.subdev_desc.power = rk_sensor_power;
		new_camera->dev.desc_info.subdev_desc.powerdown = rk_sensor_powerdown;
		new_camera->dev.desc_info.host_desc.board_info =&new_camera->dev.i2c_cam_info;

		new_camera->dev.device_info.id = i+6;
		new_camera->dev.device_info.dev.platform_data = &new_camera->dev.desc_info;
		new_camera->dev.desc_info.subdev_desc.drv_priv = &rk_camera_platform_data;

		platform_device_register(&(new_camera->dev.device_info));
		i++;
		new_camera = new_camera->next_camera;
	}

	return 0;
}

static int rk_register_camera_devices(void)
{
	platform_driver_register(&rk_sensor_driver);

	if (rk_camera_platform_data.sensor_register)
		rk_camera_platform_data.sensor_register();

	return 0;
}

module_init(rk_register_camera_devices);
