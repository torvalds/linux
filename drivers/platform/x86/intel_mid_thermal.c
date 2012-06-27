/*
 * intel_mid_thermal.c - Intel MID platform thermal driver
 *
 * Copyright (C) 2011 Intel Corporation
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.        See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Author: Durgadoss R <durgadoss.r@intel.com>
 */

#define pr_fmt(fmt) "intel_mid_thermal: " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/param.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/thermal.h>
#include <linux/mfd/intel_msic.h>

/* Number of thermal sensors */
#define MSIC_THERMAL_SENSORS	4

/* ADC1 - thermal registers */
#define MSIC_ADC_ENBL		0x10
#define MSIC_ADC_START		0x08

#define MSIC_ADCTHERM_ENBL	0x04
#define MSIC_ADCRRDATA_ENBL	0x05
#define MSIC_CHANL_MASK_VAL	0x0F

#define MSIC_STOPBIT_MASK	16
#define MSIC_ADCTHERM_MASK	4
/* Number of ADC channels */
#define ADC_CHANLS_MAX		15
#define ADC_LOOP_MAX		(ADC_CHANLS_MAX - MSIC_THERMAL_SENSORS)

/* ADC channel code values */
#define SKIN_SENSOR0_CODE	0x08
#define SKIN_SENSOR1_CODE	0x09
#define SYS_SENSOR_CODE		0x0A
#define MSIC_DIE_SENSOR_CODE	0x03

#define SKIN_THERM_SENSOR0	0
#define SKIN_THERM_SENSOR1	1
#define SYS_THERM_SENSOR2	2
#define MSIC_DIE_THERM_SENSOR3	3

/* ADC code range */
#define ADC_MAX			977
#define ADC_MIN			162
#define ADC_VAL0C		887
#define ADC_VAL20C		720
#define ADC_VAL40C		508
#define ADC_VAL60C		315

/* ADC base addresses */
#define ADC_CHNL_START_ADDR	INTEL_MSIC_ADC1ADDR0	/* increments by 1 */
#define ADC_DATA_START_ADDR	INTEL_MSIC_ADC1SNS0H	/* increments by 2 */

/* MSIC die attributes */
#define MSIC_DIE_ADC_MIN	488
#define MSIC_DIE_ADC_MAX	1004

/* This holds the address of the first free ADC channel,
 * among the 15 channels
 */
static int channel_index;

struct platform_info {
	struct platform_device *pdev;
	struct thermal_zone_device *tzd[MSIC_THERMAL_SENSORS];
};

struct thermal_device_info {
	unsigned int chnl_addr;
	int direct;
	/* This holds the current temperature in millidegree celsius */
	long curr_temp;
};

/**
 * to_msic_die_temp - converts adc_val to msic_die temperature
 * @adc_val: ADC value to be converted
 *
 * Can sleep
 */
static int to_msic_die_temp(uint16_t adc_val)
{
	return (368 * (adc_val) / 1000) - 220;
}

/**
 * is_valid_adc - checks whether the adc code is within the defined range
 * @min: minimum value for the sensor
 * @max: maximum value for the sensor
 *
 * Can sleep
 */
static int is_valid_adc(uint16_t adc_val, uint16_t min, uint16_t max)
{
	return (adc_val >= min) && (adc_val <= max);
}

/**
 * adc_to_temp - converts the ADC code to temperature in C
 * @direct: true if ths channel is direct index
 * @adc_val: the adc_val that needs to be converted
 * @tp: temperature return value
 *
 * Linear approximation is used to covert the skin adc value into temperature.
 * This technique is used to avoid very long look-up table to get
 * the appropriate temp value from ADC value.
 * The adc code vs sensor temp curve is split into five parts
 * to achieve very close approximate temp value with less than
 * 0.5C error
 */
static int adc_to_temp(int direct, uint16_t adc_val, unsigned long *tp)
{
	int temp;

	/* Direct conversion for die temperature */
	if (direct) {
		if (is_valid_adc(adc_val, MSIC_DIE_ADC_MIN, MSIC_DIE_ADC_MAX)) {
			*tp = to_msic_die_temp(adc_val) * 1000;
			return 0;
		}
		return -ERANGE;
	}

	if (!is_valid_adc(adc_val, ADC_MIN, ADC_MAX))
		return -ERANGE;

	/* Linear approximation for skin temperature */
	if (adc_val > ADC_VAL0C)
		temp = 177 - (adc_val/5);
	else if ((adc_val <= ADC_VAL0C) && (adc_val > ADC_VAL20C))
		temp = 111 - (adc_val/8);
	else if ((adc_val <= ADC_VAL20C) && (adc_val > ADC_VAL40C))
		temp = 92 - (adc_val/10);
	else if ((adc_val <= ADC_VAL40C) && (adc_val > ADC_VAL60C))
		temp = 91 - (adc_val/10);
	else
		temp = 112 - (adc_val/6);

	/* Convert temperature in celsius to milli degree celsius */
	*tp = temp * 1000;
	return 0;
}

/**
 * mid_read_temp - read sensors for temperature
 * @temp: holds the current temperature for the sensor after reading
 *
 * reads the adc_code from the channel and converts it to real
 * temperature. The converted value is stored in temp.
 *
 * Can sleep
 */
static int mid_read_temp(struct thermal_zone_device *tzd, unsigned long *temp)
{
	struct thermal_device_info *td_info = tzd->devdata;
	uint16_t adc_val, addr;
	uint8_t data = 0;
	int ret;
	unsigned long curr_temp;


	addr = td_info->chnl_addr;

	/* Enable the msic for conversion before reading */
	ret = intel_msic_reg_write(INTEL_MSIC_ADC1CNTL3, MSIC_ADCRRDATA_ENBL);
	if (ret)
		return ret;

	/* Re-toggle the RRDATARD bit (temporary workaround) */
	ret = intel_msic_reg_write(INTEL_MSIC_ADC1CNTL3, MSIC_ADCTHERM_ENBL);
	if (ret)
		return ret;

	/* Read the higher bits of data */
	ret = intel_msic_reg_read(addr, &data);
	if (ret)
		return ret;

	/* Shift bits to accommodate the lower two data bits */
	adc_val = (data << 2);
	addr++;

	ret = intel_msic_reg_read(addr, &data);/* Read lower bits */
	if (ret)
		return ret;

	/* Adding lower two bits to the higher bits */
	data &= 03;
	adc_val += data;

	/* Convert ADC value to temperature */
	ret = adc_to_temp(td_info->direct, adc_val, &curr_temp);
	if (ret == 0)
		*temp = td_info->curr_temp = curr_temp;
	return ret;
}

/**
 * configure_adc - enables/disables the ADC for conversion
 * @val: zero: disables the ADC non-zero:enables the ADC
 *
 * Enable/Disable the ADC depending on the argument
 *
 * Can sleep
 */
static int configure_adc(int val)
{
	int ret;
	uint8_t data;

	ret = intel_msic_reg_read(INTEL_MSIC_ADC1CNTL1, &data);
	if (ret)
		return ret;

	if (val) {
		/* Enable and start the ADC */
		data |= (MSIC_ADC_ENBL | MSIC_ADC_START);
	} else {
		/* Just stop the ADC */
		data &= (~MSIC_ADC_START);
	}
	return intel_msic_reg_write(INTEL_MSIC_ADC1CNTL1, data);
}

/**
 * set_up_therm_channel - enable thermal channel for conversion
 * @base_addr: index of free msic ADC channel
 *
 * Enable all the three channels for conversion
 *
 * Can sleep
 */
static int set_up_therm_channel(u16 base_addr)
{
	int ret;

	/* Enable all the sensor channels */
	ret = intel_msic_reg_write(base_addr, SKIN_SENSOR0_CODE);
	if (ret)
		return ret;

	ret = intel_msic_reg_write(base_addr + 1, SKIN_SENSOR1_CODE);
	if (ret)
		return ret;

	ret = intel_msic_reg_write(base_addr + 2, SYS_SENSOR_CODE);
	if (ret)
		return ret;

	/* Since this is the last channel, set the stop bit
	 * to 1 by ORing the DIE_SENSOR_CODE with 0x10 */
	ret = intel_msic_reg_write(base_addr + 3,
			(MSIC_DIE_SENSOR_CODE | 0x10));
	if (ret)
		return ret;

	/* Enable ADC and start it */
	return configure_adc(1);
}

/**
 * reset_stopbit - sets the stop bit to 0 on the given channel
 * @addr: address of the channel
 *
 * Can sleep
 */
static int reset_stopbit(uint16_t addr)
{
	int ret;
	uint8_t data;
	ret = intel_msic_reg_read(addr, &data);
	if (ret)
		return ret;
	/* Set the stop bit to zero */
	return intel_msic_reg_write(addr, (data & 0xEF));
}

/**
 * find_free_channel - finds an empty channel for conversion
 *
 * If the ADC is not enabled then start using 0th channel
 * itself. Otherwise find an empty channel by looking for a
 * channel in which the stopbit is set to 1. returns the index
 * of the first free channel if succeeds or an error code.
 *
 * Context: can sleep
 *
 * FIXME: Ultimately the channel allocator will move into the intel_scu_ipc
 * code.
 */
static int find_free_channel(void)
{
	int ret;
	int i;
	uint8_t data;

	/* check whether ADC is enabled */
	ret = intel_msic_reg_read(INTEL_MSIC_ADC1CNTL1, &data);
	if (ret)
		return ret;

	if ((data & MSIC_ADC_ENBL) == 0)
		return 0;

	/* ADC is already enabled; Looking for an empty channel */
	for (i = 0; i < ADC_CHANLS_MAX; i++) {
		ret = intel_msic_reg_read(ADC_CHNL_START_ADDR + i, &data);
		if (ret)
			return ret;

		if (data & MSIC_STOPBIT_MASK) {
			ret = i;
			break;
		}
	}
	return (ret > ADC_LOOP_MAX) ? (-EINVAL) : ret;
}

/**
 * mid_initialize_adc - initializing the ADC
 * @dev: our device structure
 *
 * Initialize the ADC for reading thermistor values. Can sleep.
 */
static int mid_initialize_adc(struct device *dev)
{
	u8  data;
	u16 base_addr;
	int ret;

	/*
	 * Ensure that adctherm is disabled before we
	 * initialize the ADC
	 */
	ret = intel_msic_reg_read(INTEL_MSIC_ADC1CNTL3, &data);
	if (ret)
		return ret;

	data &= ~MSIC_ADCTHERM_MASK;
	ret = intel_msic_reg_write(INTEL_MSIC_ADC1CNTL3, data);
	if (ret)
		return ret;

	/* Index of the first channel in which the stop bit is set */
	channel_index = find_free_channel();
	if (channel_index < 0) {
		dev_err(dev, "No free ADC channels");
		return channel_index;
	}

	base_addr = ADC_CHNL_START_ADDR + channel_index;

	if (!(channel_index == 0 || channel_index == ADC_LOOP_MAX)) {
		/* Reset stop bit for channels other than 0 and 12 */
		ret = reset_stopbit(base_addr);
		if (ret)
			return ret;

		/* Index of the first free channel */
		base_addr++;
		channel_index++;
	}

	ret = set_up_therm_channel(base_addr);
	if (ret) {
		dev_err(dev, "unable to enable ADC");
		return ret;
	}
	dev_dbg(dev, "ADC initialization successful");
	return ret;
}

/**
 * initialize_sensor - sets default temp and timer ranges
 * @index: index of the sensor
 *
 * Context: can sleep
 */
static struct thermal_device_info *initialize_sensor(int index)
{
	struct thermal_device_info *td_info =
		kzalloc(sizeof(struct thermal_device_info), GFP_KERNEL);

	if (!td_info)
		return NULL;

	/* Set the base addr of the channel for this sensor */
	td_info->chnl_addr = ADC_DATA_START_ADDR + 2 * (channel_index + index);
	/* Sensor 3 is direct conversion */
	if (index == 3)
		td_info->direct = 1;
	return td_info;
}

/**
 * mid_thermal_resume - resume routine
 * @dev: device structure
 *
 * mid thermal resume: re-initializes the adc. Can sleep.
 */
static int mid_thermal_resume(struct device *dev)
{
	return mid_initialize_adc(dev);
}

/**
 * mid_thermal_suspend - suspend routine
 * @dev: device structure
 *
 * mid thermal suspend implements the suspend functionality
 * by stopping the ADC. Can sleep.
 */
static int mid_thermal_suspend(struct device *dev)
{
	/*
	 * This just stops the ADC and does not disable it.
	 * temporary workaround until we have a generic ADC driver.
	 * If 0 is passed, it disables the ADC.
	 */
	return configure_adc(0);
}

static SIMPLE_DEV_PM_OPS(mid_thermal_pm,
			 mid_thermal_suspend, mid_thermal_resume);

/**
 * read_curr_temp - reads the current temperature and stores in temp
 * @temp: holds the current temperature value after reading
 *
 * Can sleep
 */
static int read_curr_temp(struct thermal_zone_device *tzd, unsigned long *temp)
{
	WARN_ON(tzd == NULL);
	return mid_read_temp(tzd, temp);
}

/* Can't be const */
static struct thermal_zone_device_ops tzd_ops = {
	.get_temp = read_curr_temp,
};

/**
 * mid_thermal_probe - mfld thermal initialize
 * @pdev: platform device structure
 *
 * mid thermal probe initializes the hardware and registers
 * all the sensors with the generic thermal framework. Can sleep.
 */
static int mid_thermal_probe(struct platform_device *pdev)
{
	static char *name[MSIC_THERMAL_SENSORS] = {
		"skin0", "skin1", "sys", "msicdie"
	};

	int ret;
	int i;
	struct platform_info *pinfo;

	pinfo = kzalloc(sizeof(struct platform_info), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	/* Initializing the hardware */
	ret = mid_initialize_adc(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "ADC init failed");
		kfree(pinfo);
		return ret;
	}

	/* Register each sensor with the generic thermal framework*/
	for (i = 0; i < MSIC_THERMAL_SENSORS; i++) {
		struct thermal_device_info *td_info = initialize_sensor(i);

		if (!td_info) {
			ret = -ENOMEM;
			goto err;
		}
		pinfo->tzd[i] = thermal_zone_device_register(name[i],
				0, td_info, &tzd_ops, 0, 0, 0, 0);
		if (IS_ERR(pinfo->tzd[i])) {
			kfree(td_info);
			ret = PTR_ERR(pinfo->tzd[i]);
			goto err;
		}
	}

	pinfo->pdev = pdev;
	platform_set_drvdata(pdev, pinfo);
	return 0;

err:
	while (--i >= 0) {
		kfree(pinfo->tzd[i]->devdata);
		thermal_zone_device_unregister(pinfo->tzd[i]);
	}
	configure_adc(0);
	kfree(pinfo);
	return ret;
}

/**
 * mid_thermal_remove - mfld thermal finalize
 * @dev: platform device structure
 *
 * MLFD thermal remove unregisters all the sensors from the generic
 * thermal framework. Can sleep.
 */
static int mid_thermal_remove(struct platform_device *pdev)
{
	int i;
	struct platform_info *pinfo = platform_get_drvdata(pdev);

	for (i = 0; i < MSIC_THERMAL_SENSORS; i++) {
		kfree(pinfo->tzd[i]->devdata);
		thermal_zone_device_unregister(pinfo->tzd[i]);
	}

	kfree(pinfo);
	platform_set_drvdata(pdev, NULL);

	/* Stop the ADC */
	return configure_adc(0);
}

#define DRIVER_NAME "msic_thermal"

static const struct platform_device_id therm_id_table[] = {
	{ DRIVER_NAME, 1 },
	{ "msic_thermal", 1 },
	{ }
};

static struct platform_driver mid_thermal_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.pm = &mid_thermal_pm,
	},
	.probe = mid_thermal_probe,
	.remove = __devexit_p(mid_thermal_remove),
	.id_table = therm_id_table,
};

module_platform_driver(mid_thermal_driver);

MODULE_AUTHOR("Durgadoss R <durgadoss.r@intel.com>");
MODULE_DESCRIPTION("Intel Medfield Platform Thermal Driver");
MODULE_LICENSE("GPL");
