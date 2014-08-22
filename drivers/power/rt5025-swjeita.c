/* drivers/power/rt5025-swjeita.c
 * swjeita Driver for Richtek RT5025 PMIC
 * Multi function device - multi functional baseband PMIC swjeita part
 *
 * Copyright (C) 2013
 * Author: CY Huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <linux/mfd/rt5025.h>
#include <linux/power/rt5025-swjeita.h>

#define TEMP_TOLERANCE	30  /*'c*10 gap for tolerance*/

static int rt5025_set_charging_cc_switch (struct i2c_client *i2c, int onoff)
{
	int ret;

	RTINFO("onoff = %d\n", onoff);
	if (onoff)
		ret = rt5025_set_bits(i2c, RT5025_REG_CHGCTL7, RT5025_CHGCCEN_MASK);
	else
		ret = rt5025_clr_bits(i2c, RT5025_REG_CHGCTL7, RT5025_CHGCCEN_MASK);
	return ret;
}

static int rt5025_set_charging_cc(struct i2c_client *i2c, int cur_value)
{
	int ret;
	u8 data;

	RTINFO("current value = %d\n", cur_value);
	if (cur_value < 500)
		data = 0;
	else if (cur_value > 2000)
		data = 0xf << RT5025_CHGICC_SHIFT;
	else
		data = ((cur_value - 500) / 100) << RT5025_CHGICC_SHIFT;

	ret = rt5025_assign_bits(i2c, RT5025_REG_CHGCTL4, RT5025_CHGICC_MASK, data);

	if (cur_value < 500)
		rt5025_set_charging_cc_switch(i2c, 0);
	else
		rt5025_set_charging_cc_switch(i2c, 1);

	return ret;
}

static int rt5025_set_charging_cv(struct i2c_client *i2c, int voltage)
{
	int ret;
	u8 data;

	RTINFO("voltage = %d\n", voltage);
	if (voltage < 3500)
		data = 0;
	else if (voltage > 4440)
		data = 0x2f << RT5025_CHGCV_SHIFT;
	else
		data = ((voltage - 3500) / 20) << RT5025_CHGCV_SHIFT;

	ret = rt5025_assign_bits(i2c, RT5025_REG_CHGCTL3, RT5025_CHGCV_MASK, data);
	return ret;
}

static int rt5025_sel_external_temp_index(struct rt5025_swjeita_info *swji)
{
	int temp = swji->cur_temp;
	int sect_index;

	RTINFO("\n");
	if (temp < swji->temp[0])
		sect_index = 0;
	else if (temp >= swji->temp[0] && temp < swji->temp[1])
		sect_index = 1;
	else if (temp >= swji->temp[1] && temp < swji->temp[2])
		sect_index = 2;
	else if (temp >= swji->temp[2] && temp < swji->temp[3])
		sect_index = 3;
	else if (temp >= swji->temp[3])
		sect_index = 4;

	RTINFO("sect_index = %d\n", sect_index);
	return sect_index;
}

static int rt5025_get_external_temp_index(struct rt5025_swjeita_info *swji)
{
	u8 data[2];
	long int temp;
	int sect_index;

	RTINFO("\n");
	if (rt5025_reg_block_read(swji->i2c, RT5025_REG_AINH, 2, data) < 0)
		pr_err("%s: failed to read ext_temp register\n", __func__);

	temp = (data[0] * 256 + data[1]) * 61 / 100;
	temp = (temp  *  (-91738) + 81521000) / 100000;

	swji->cur_temp = temp;

	RTINFO("cur_section = %d, cur_temp = %d\n", swji->cur_section, swji->cur_temp);

	switch (swji->cur_section) {
	case 0:
		if (temp < swji->temp[0] + TEMP_TOLERANCE)
			sect_index = rt5025_sel_external_temp_index(swji);
		else
			sect_index = swji->cur_section;
		break;
	case 1:
		if (temp <= swji->temp[0] - TEMP_TOLERANCE || temp >= swji->temp[1] + TEMP_TOLERANCE)
			sect_index = rt5025_sel_external_temp_index(swji);
		else
			sect_index = swji->cur_section;
		break;
	case 2:
		if (temp <= swji->temp[1] - TEMP_TOLERANCE || temp >= swji->temp[2] + TEMP_TOLERANCE)
			sect_index = rt5025_sel_external_temp_index(swji);
		else
			sect_index = swji->cur_section;
		break;
	case 3:
		if (temp <= swji->temp[2] - TEMP_TOLERANCE || temp >= swji->temp[3] + TEMP_TOLERANCE)
			sect_index = rt5025_sel_external_temp_index(swji);
		else
			sect_index = swji->cur_section;
		break;
	case 4:
		if (temp <= swji->temp[3] - TEMP_TOLERANCE)
			sect_index = rt5025_sel_external_temp_index(swji);
		else
			sect_index = swji->cur_section;
		break;
	default:
		sect_index = swji->cur_section;
		break;
	}
	RTINFO("sect_index = %d\n", sect_index);
	return sect_index;
}

static inline int rt5025_set_ainadc_onoff(struct rt5025_swjeita_info *swji, int enable)
{
	int ret;

	RTINFO("enable = %d\n", enable);
	if (enable)
		ret = rt5025_set_bits(swji->i2c, RT5025_REG_CHANNELL, RT5025_AINEN_MASK);
	else
		ret = rt5025_clr_bits(swji->i2c, RT5025_REG_CHANNELL, RT5025_AINEN_MASK);

	return ret;
}

static inline int rt5025_set_intadc_onoff(struct rt5025_swjeita_info *swji, int enable)
{
	int ret;

	RTINFO("enable = %d\n", enable);
	if (enable)
		ret = rt5025_set_bits(swji->i2c, RT5025_REG_CHANNELL, RT5025_INTEN_MASK);
	else
		ret = rt5025_clr_bits(swji->i2c, RT5025_REG_CHANNELL, RT5025_INTEN_MASK);

	return ret;
}

static int rt5025_set_exttemp_alert(struct rt5025_swjeita_info *swji, int index)
{
	int ret = 0;

	RTINFO("index = %d\n", index);

	switch (index) {
	case 0:
		rt5025_reg_write(swji->i2c, RT5025_REG_TALRTMIN, swji->temp_scalar[1]);
		break;
	case 1:
		rt5025_reg_write(swji->i2c, RT5025_REG_TALRTMAX, swji->temp_scalar[0]);
		rt5025_reg_write(swji->i2c, RT5025_REG_TALRTMIN, swji->temp_scalar[3]);
		break;
	case 2:
		rt5025_reg_write(swji->i2c, RT5025_REG_TALRTMAX, swji->temp_scalar[2]);
		rt5025_reg_write(swji->i2c, RT5025_REG_TALRTMIN, swji->temp_scalar[5]);
		break;
	case 3:
		rt5025_reg_write(swji->i2c, RT5025_REG_TALRTMAX, swji->temp_scalar[4]);
		rt5025_reg_write(swji->i2c, RT5025_REG_TALRTMIN, swji->temp_scalar[7]);
		break;
	case 4:
		rt5025_reg_write(swji->i2c, RT5025_REG_TALRTMAX, swji->temp_scalar[6]);
		break;
	}

	return ret;
}

static int rt5025_exttemp_alert_switch(struct rt5025_swjeita_info *swji, int onoff)
{
	if (!onoff) {
		rt5025_clr_bits(swji->i2c, RT5025_REG_IRQCTL, RT5025_TMXEN_MASK);
		rt5025_clr_bits(swji->i2c, RT5025_REG_IRQCTL, RT5025_TMNEN_MASK);
	} else {
		switch (swji->cur_section) {
		case 0:
			rt5025_set_bits(swji->i2c, RT5025_REG_IRQCTL, RT5025_TMNEN_MASK);
			break;
		case 1:
			rt5025_set_bits(swji->i2c, RT5025_REG_IRQCTL, RT5025_TMXEN_MASK);
			rt5025_set_bits(swji->i2c, RT5025_REG_IRQCTL, RT5025_TMNEN_MASK);
			break;
		case 2:
			rt5025_set_bits(swji->i2c, RT5025_REG_IRQCTL, RT5025_TMXEN_MASK);
			rt5025_set_bits(swji->i2c, RT5025_REG_IRQCTL, RT5025_TMNEN_MASK);
			break;
		case 3:
			rt5025_set_bits(swji->i2c, RT5025_REG_IRQCTL, RT5025_TMXEN_MASK);
			rt5025_set_bits(swji->i2c, RT5025_REG_IRQCTL, RT5025_TMNEN_MASK);
			break;
		case 4:
			rt5025_set_bits(swji->i2c, RT5025_REG_IRQCTL, RT5025_TMXEN_MASK);
			break;
		}
	}

	RTINFO("index=%d, onoff=%d\n", swji->cur_section, onoff);
	return 0;
}

int rt5025_notify_charging_cable(struct rt5025_swjeita_info *swji, int cable_type)
{
	int sect_index;
	int ret = 0;

	RTINFO("cable_type = %d\n", cable_type);

	rt5025_exttemp_alert_switch(swji, 0);

	sect_index = rt5025_get_external_temp_index(swji);
	if (swji->cur_section != sect_index || swji->init_once == 0) {
		rt5025_set_exttemp_alert(swji, sect_index);
		swji->cur_section = sect_index;
		swji->init_once = 1;
	}

	switch (cable_type) {
	case JEITA_NORMAL_USB:
		rt5025_set_charging_cc(swji->i2c, swji->temp_cc[cable_type][swji->cur_section]\
			- swji->dec_current);
		rt5025_set_charging_cv(swji->i2c, swji->temp_cv[cable_type][swji->cur_section]);
		break;
	case JEITA_USB_TA:
		rt5025_set_charging_cc(swji->i2c, swji->temp_cc[cable_type][swji->cur_section]\
			- swji->dec_current);
		rt5025_set_charging_cv(swji->i2c, swji->temp_cv[cable_type][swji->cur_section]);
		break;
	case JEITA_AC_ADAPTER:
		rt5025_set_charging_cc(swji->i2c, swji->temp_cc[cable_type][swji->cur_section]\
			- swji->dec_current);
		rt5025_set_charging_cv(swji->i2c, swji->temp_cv[cable_type][swji->cur_section]);
		break;
	case JEITA_NO_CHARGE:
		rt5025_set_charging_cc(swji->i2c, swji->temp_cc[cable_type][swji->cur_section]);
		rt5025_set_charging_cv(swji->i2c, swji->temp_cv[cable_type][swji->cur_section]);
		break;
	}
	swji->cur_cable = cable_type;

	rt5025_exttemp_alert_switch(swji, 1);

	return ret;
}
EXPORT_SYMBOL(rt5025_notify_charging_cable);

int rt5025_swjeita_irq_handler(struct rt5025_swjeita_info *swji, unsigned char event)
{
	int ret = 0;
	RTINFO("event = 0x%02x\n", event);

	if (event&(RT5025_TMXEN_MASK | RT5025_TMNEN_MASK))
		rt5025_notify_charging_cable(swji, swji->cur_cable);

	return ret;
}
EXPORT_SYMBOL(rt5025_swjeita_irq_handler);

static void rt5025_get_internal_temp(struct rt5025_swjeita_info *swji)
{
	u8 data[2];
	s32 temp;
	if (rt5025_reg_block_read(swji->i2c, RT5025_REG_INTTEMP_MSB, 2, data) < 0)
		pr_err("%s: Failed to read internal TEMPERATURE\n", __func__);

	temp = ((data[0] & 0x1F) << 8) + data[1];
	temp *= 15625;
	temp /= 100000;

	temp = (data[0] & 0x20) ? -temp : temp;
	swji->cur_inttemp = temp;

	RTINFO("internal temperature: %d\n", temp);
}

static void thermal_reg_work_func(struct work_struct *work)
{
	struct delayed_work *delayed_work = (struct delayed_work *)container_of(work, struct delayed_work, work);
	struct rt5025_swjeita_info *swji = (struct rt5025_swjeita_info *)container_of(delayed_work, struct rt5025_swjeita_info, thermal_reg_work);
	int therm_region = 0;

	RTINFO("%s ++", __func__);
	rt5025_get_internal_temp(swji);

	#if 1
	switch (swji->cur_therm_region) {
	case 0:
		if (swji->cur_inttemp >= 820)
			therm_region = 1;
		else
			therm_region = 0;
		break;
	case 1:
		if (swji->cur_inttemp <= 780)
			therm_region = 0;
		else if (swji->cur_inttemp >= 1020)
			therm_region = 2;
		else
			therm_region = 1;
		break;
	case 2:
		if (swji->cur_inttemp <= 980)
			therm_region = 1;
		else
			therm_region = 2;
		break;
		}
	#else
	if (swji->cur_inttemp < 800)
		therm_region = 0;
	else if (swji->cur_inttemp >= 800 && swji->cur_inttemp < 1000)
		therm_region = 1;
	else
		therm_region = 2;
	#endif /* #if 1*/

	if (therm_region != swji->cur_therm_region) {
		switch (therm_region) {
		case 0:
			swji->dec_current = 0;
			break;
		case 1:
			swji->dec_current = 300;
			break;
		case 2:
			swji->dec_current = 800;
			break;
		}
		swji->cur_therm_region = therm_region;
		rt5025_notify_charging_cable(swji, swji->cur_cable);
	}

	if (!swji->suspend)
		schedule_delayed_work(&swji->thermal_reg_work, 5*HZ);

	RTINFO("%s --", __func__);
}

static int rt5025_swjeita_probe(struct platform_device *pdev)
{
	struct rt5025_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5025_platform_data *pdata = chip->dev->platform_data;
	struct rt5025_swjeita_info *swji;
	int ret = 0;

	swji = kzalloc(sizeof(*swji), GFP_KERNEL);
	if (!swji)
		return -ENOMEM;

	#if 0 /* for debug pdata->jeita_data*/
	for (ret = 0; ret < 4; ret++)
		RTINFO("jeita temp value %d\n", pdata->jeita_data->temp[ret]);
	for (ret = 0; ret < 4; ret++) {
		RTINFO("jeita temp_cc value %d, %d, %d, %d, %d\n", pdata->jeita_data->temp_cc[ret][0], \
		pdata->jeita_data->temp_cc[ret][1], pdata->jeita_data->temp_cc[ret][2], \
		pdata->jeita_data->temp_cc[ret][3], pdata->jeita_data->temp_cc[ret][4]);
	}
	for (ret = 0; ret < 4; ret++) {
		RTINFO("jeita temp_cv value %d, %d, %d, %d, %d\n", pdata->jeita_data->temp_cv[ret][0], \
		pdata->jeita_data->temp_cv[ret][1], pdata->jeita_data->temp_cv[ret][2], \
		pdata->jeita_data->temp_cv[ret][3], pdata->jeita_data->temp_cv[ret][4]);
	}
	for (ret = 0; ret < 8; ret++) {
		RTINFO("temp_scalar[%d] = 0x%02x\n", ret, pdata->jeita_data->temp_scalar[ret]);
	}
	ret = 0;
	#endif /* #if 0 */

	swji->i2c = chip->i2c;
	swji->chip = chip;
	swji->cur_section = 2;
	/*initial as the normal temperature*/
	swji->cur_cable = JEITA_NO_CHARGE;
	swji->temp = pdata->jeita_data->temp;
	swji->temp_scalar = pdata->jeita_data->temp_scalar;
	swji->temp_cc = pdata->jeita_data->temp_cc;
	swji->temp_cv = pdata->jeita_data->temp_cv;
	INIT_DELAYED_WORK(&swji->thermal_reg_work, thermal_reg_work_func);
	platform_set_drvdata(pdev, swji);

	rt5025_set_ainadc_onoff(swji, 1);
	rt5025_set_intadc_onoff(swji, 1);
	mdelay(100);
	rt5025_notify_charging_cable(swji, swji->cur_cable);
	schedule_delayed_work(&swji->thermal_reg_work, 1*HZ);

	chip->jeita_info = swji;
	RTINFO("rt5025-swjeita driver is successfully loaded\n");
	return ret;
}

static int rt5025_swjeita_remove(struct platform_device *pdev)
{
	struct rt5025_swjeita_info *swji = platform_get_drvdata(pdev);

	swji->chip->jeita_info = NULL;
	kfree(swji);
	RTINFO("\n");
	return 0;
}

static int rt5025_swjeita_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rt5025_swjeita_info *swji = platform_get_drvdata(pdev);

	swji->suspend = 1;
	cancel_delayed_work_sync(&swji->thermal_reg_work);
	swji->cur_therm_region = swji->dec_current = 0;
	rt5025_notify_charging_cable(swji, swji->cur_cable);
	RTINFO("\n");
	return 0;
}

static int rt5025_swjeita_resume(struct platform_device *pdev)
{
	struct rt5025_swjeita_info *swji = platform_get_drvdata(pdev);

	swji->suspend = 0;
	schedule_delayed_work(&swji->thermal_reg_work, 0);
	RTINFO("\n");
	return 0;
}

static struct platform_driver rt5025_swjeita_driver = {
	.driver = {
		.name = RT5025_DEVICE_NAME "-swjeita",
		.owner = THIS_MODULE,
	},
	.probe = rt5025_swjeita_probe,
	.remove = __devexit_p(rt5025_swjeita_remove),
	.suspend = rt5025_swjeita_suspend,
	.resume = rt5025_swjeita_resume,
};

static int rt5025_swjeita_init(void)
{
	return platform_driver_register(&rt5025_swjeita_driver);
}
module_init(rt5025_swjeita_init);

static void rt5025_swjeita_exit(void)
{
	platform_driver_unregister(&rt5025_swjeita_driver);
}
module_exit(rt5025_swjeita_exit);


MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com");
MODULE_DESCRIPTION("Swjeita driver for RT5025");
MODULE_ALIAS("platform:" RT5025_DEVICE_NAME "-swjeita");
MODULE_VERSION(RT5025_DRV_VER);
