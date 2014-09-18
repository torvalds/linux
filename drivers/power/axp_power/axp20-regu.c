/*
 * Regulators driver for Dialog Semiconductor DA903x
 *
 * Copyright (C) 2006-2008 Marvell International Ltd.
 * Copyright (C) 2008 Compulab Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/module.h>

#include "axp-regu.h"

static int axp20_ldo4_data[] = { 1250, 1300, 1400, 1500, 1600, 1700,
				   1800, 1900, 2000, 2500, 2700, 2800,
				   3000, 3100, 3200, 3300 };

static inline struct device *to_axp_dev(struct regulator_dev *rdev)
{
	return rdev_get_dev(rdev)->parent->parent;
}

static inline int check_range(struct axp_regulator_info *info,
				int min_uV, int max_uV)
{
	if (min_uV < info->min_uV || min_uV > info->max_uV)
		return -EINVAL;

	return 0;
}

static int axp_get_voltage(struct regulator_dev *rdev);

/* AXP common operations */
static int axp_set_voltage(struct regulator_dev *rdev, int min_uV, int max_uV,
			   unsigned *selector)
{
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);
	uint8_t val, mask;

	if (rdev_get_id(rdev) == AXP20_ID_BUCK3) {
		pr_err("somebody is trying to set dcdc3 range to (%d, %d) uV\n",
			min_uV, max_uV);
		pr_err("but we keep dcdc3 = %d uV from the bootloader\n",
			axp_get_voltage(rdev));
		return 0;
	}

	if (check_range(info, min_uV, max_uV)) {
		pr_err("invalid voltage range (%d, %d) uV\n", min_uV, max_uV);
		return -EINVAL;
	}

	val = (min_uV - info->min_uV + info->step_uV - 1) / info->step_uV;
	val <<= info->vol_shift;
	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;

	return axp_update(axp_dev, info->vol_reg, val, mask);
}

static int axp_get_voltage(struct regulator_dev *rdev)
{
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);
	uint8_t val, mask;
	int ret;

	ret = axp_read(axp_dev, info->vol_reg, &val);
	if (ret)
		return ret;
  
	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;
	val = (val & mask) >> info->vol_shift;

	return info->min_uV + info->step_uV * val;
	
}

static int axp_enable(struct regulator_dev *rdev)
{
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);

	return axp_set_bits(axp_dev, info->enable_reg,
					1 << info->enable_bit);
}

static int axp_disable(struct regulator_dev *rdev)
{
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);

	return axp_clr_bits(axp_dev, info->enable_reg,
					1 << info->enable_bit);
}

static int axp_is_enabled(struct regulator_dev *rdev)
{
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);
	uint8_t reg_val;
	int ret;

	ret = axp_read(axp_dev, info->enable_reg, &reg_val);
	if (ret)
		return ret;

	return !!(reg_val & (1 << info->enable_bit));
}

static int axp_list_voltage(struct regulator_dev *rdev, unsigned selector)
{
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	if(info->desc.id == AXP20_ID_LDO4)
		return axp20_ldo4_data[selector] * 1000;
	
	ret = info->min_uV + info->step_uV * selector;
	if (ret > info->max_uV)
		return -EINVAL;
	return ret;
}

static int axp_set_ldo4_voltage(struct regulator_dev *rdev, int min_uV, int max_uV,
				unsigned *selector)
{
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);
	uint8_t val, mask;
	int i;
	
	if (check_range(info, min_uV, max_uV)) {
		pr_err("invalid voltage range (%d, %d) uV\n", min_uV, max_uV);
		return -EINVAL;
	}

	for(i = 0,val = 0; i < sizeof(axp20_ldo4_data);i++){
		if(min_uV <= axp20_ldo4_data[i] * 1000){
			val = i;
			break;
		}
	}
	
	val <<= info->vol_shift;
	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;
	return axp_update(axp_dev, info->vol_reg, val, mask);
}

static int axp_get_ldo4_voltage(struct regulator_dev *rdev)
{
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);
	uint8_t val, mask;
	int ret;

	ret = axp_read(axp_dev, info->vol_reg, &val);
	if (ret)
		return ret;
  
	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;
	val = (val & mask) >> info->vol_shift;
	ret = axp20_ldo4_data[val]*1000;
	return ret;
}

static int axp_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	int ldo = rdev_get_id(rdev);
	unsigned selector = 0;

	switch (ldo) {
	
	case AXP20_ID_LDO1 ... AXP20_ID_LDO3:
		return axp_set_voltage(rdev, uV, uV, &selector);
	case AXP20_ID_LDO4:
		return axp_set_ldo4_voltage(rdev, uV, uV, &selector);
	case AXP20_ID_BUCK2 ... AXP20_ID_LDOIO0:
		return axp_set_voltage(rdev, uV, uV, &selector);
	default:
		return -EINVAL;
	}
}


static struct regulator_ops axp20_ops = {
	.set_voltage	= axp_set_voltage,
	.get_voltage	= axp_get_voltage,
	.list_voltage	= axp_list_voltage,
	.enable		= axp_enable,
	.disable	= axp_disable,
	.is_enabled	= axp_is_enabled,
	.set_suspend_enable		= axp_enable,
	.set_suspend_disable	= axp_disable,
	.set_suspend_voltage	= axp_set_suspend_voltage,
};

static struct regulator_ops axp20_ldo4_ops = {
	.set_voltage	= axp_set_ldo4_voltage,
	.get_voltage	= axp_get_ldo4_voltage,
	.list_voltage	= axp_list_voltage,
	.enable		= axp_enable,
	.disable	= axp_disable,
	.is_enabled	= axp_is_enabled,
	.set_suspend_enable		= axp_enable,
	.set_suspend_disable	= axp_disable,
	.set_suspend_voltage	= axp_set_suspend_voltage,
};

static int axp_ldoio0_enable(struct regulator_dev *rdev)
{
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);

	 axp_set_bits(axp_dev, info->enable_reg,0x03);
	 return axp_clr_bits(axp_dev, info->enable_reg,0x04);
}

static int axp_ldoio0_disable(struct regulator_dev *rdev)
{
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);

	return axp_clr_bits(axp_dev, info->enable_reg,0x07);
}

static int axp_ldoio0_is_enabled(struct regulator_dev *rdev)
{
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);
	uint8_t reg_val;
	int ret;

	ret = axp_read(axp_dev, info->enable_reg, &reg_val);
	if (ret)
		return ret;

	return (((reg_val &= 0x07)== 0x03)?1:0);
}

static struct regulator_ops axp20_ldoio0_ops = {
	.set_voltage	= axp_set_voltage,
	.get_voltage	= axp_get_voltage,
	.list_voltage	= axp_list_voltage,
	.enable		= axp_ldoio0_enable,
	.disable	= axp_ldoio0_disable,
	.is_enabled	= axp_ldoio0_is_enabled,
	.set_suspend_enable		= axp_ldoio0_enable,
	.set_suspend_disable	= axp_ldoio0_disable,
	.set_suspend_voltage	= axp_set_suspend_voltage,
};


#define AXP20_LDO(_id, min, max, step, vreg, shift, nbits, ereg, ebit)	\
	AXP_LDO(AXP20, _id, min, max, step, vreg, shift, nbits, ereg, ebit)

#define AXP20_BUCK(_id, min, max, step, vreg, shift, nbits, ereg, ebit)	\
	AXP_BUCK(AXP20, _id, min, max, step, vreg, shift, nbits, ereg, ebit)

static struct axp_regulator_info axp_regulator_info[] = {
	AXP20_LDO(	1,	AXP20LDO1,	AXP20LDO1,	0,		LDO1,	0,	0,	LDO1EN,	0),//ldo1 for rtc
	AXP20_LDO(	2,	1800,		3300,		100,	LDO2,	4,	4,	LDO2EN,	2),//ldo2 for analog1 
	AXP20_LDO(	3,	700,		3500,		25,		LDO3,	0,	7,	LDO3EN,	6),//ldo3 for digital
	AXP20_LDO(	4,	1250,		3300,		130,	LDO4,	0,	4,	LDO4EN,	3),//ldo4 for analog2
	AXP20_BUCK(	2,	700,		2275,		25,		BUCK2,	0,	6,	BUCK2EN,4),//buck2 for core
	AXP20_BUCK(	3,	700,		3500,		25,		BUCK3,	0,	7,	BUCK3EN,1),//buck3 for memery
	AXP20_LDO(	IO0,1800,		3300,		100,	LDOIO0,	4,	4,	LDOIOEN,0),//ldoio0 for mic
};

static ssize_t workmode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);
	int ret;
	uint8_t val;
	ret = axp_read(axp_dev, AXP20_BUCKMODE, &val);
	if (ret)
		return sprintf(buf, "IO ERROR\n");
	
	if(info->desc.id == AXP20_ID_BUCK2){
		switch (val & 0x04) {
			case 0:return sprintf(buf, "AUTO\n");
			case 4:return sprintf(buf, "PWM\n");
			default:return sprintf(buf, "UNKNOWN\n");
		}
	}
	else if(info->desc.id == AXP20_ID_BUCK3){
		switch (val & 0x02) {
			case 0:return sprintf(buf, "AUTO\n");
			case 2:return sprintf(buf, "PWM\n");
			default:return sprintf(buf, "UNKNOWN\n");
		}
	}
	else
		return sprintf(buf, "IO ID ERROR\n");
}

static ssize_t workmode_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{	
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);
	char mode;
	uint8_t val;
	if(  buf[0] > '0' && buf[0] < '9' )// 1/AUTO: auto mode; 2/PWM: pwm mode;
		mode = buf[0];
	else
		mode = buf[1];
	
	switch(mode){
	 case 'U':
	 case 'u':
	 case '1':
		val = 0;break;
	 case 'W':
	 case 'w':
	 case '2':
	 	val = 1;break;
	 default:
	    val =0;	
	}
	
	if(info->desc.id == AXP20_ID_BUCK2){
		if(val)
			axp_set_bits(axp_dev, AXP20_BUCKMODE,0x04);
		else
			axp_clr_bits(axp_dev, AXP20_BUCKMODE,0x04);
	}
	else if(info->desc.id == AXP20_ID_BUCK3){
		if(val)
			axp_set_bits(axp_dev, AXP20_BUCKMODE,0x02);
		else
			axp_clr_bits(axp_dev, AXP20_BUCKMODE,0x02);
	}
	
	return count;
}

static ssize_t frequency_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	struct device *axp_dev = to_axp_dev(rdev);
	int ret;
	uint8_t val;
	ret = axp_read(axp_dev, AXP20_BUCKFREQ, &val);
	if (ret)
		return ret;
	ret = val & 0x0F;
	return sprintf(buf, "%d\n",(ret*75 + 750));
}

static ssize_t frequency_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{	
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	struct device *axp_dev = to_axp_dev(rdev);
	uint8_t val,tmp;
	int var;
	var = simple_strtoul(buf, NULL, 10);
	if(var < 750)
		var = 750;
	if(var > 1875)
		var = 1875;
		
	val = (var -750)/75;
	val &= 0x0F;
	
	axp_read(axp_dev, AXP20_BUCKFREQ, &tmp);
	tmp &= 0xF0;
	val |= tmp;
	axp_write(axp_dev, AXP20_BUCKFREQ, val);
	return count;
}


static struct device_attribute axp_regu_attrs[] = {
	AXP_REGU_ATTR(workmode),
	AXP_REGU_ATTR(frequency),
};

int axp_regu_create_attrs(struct platform_device *pdev)
{
	int j,ret;
	for (j = 0; j < ARRAY_SIZE(axp_regu_attrs); j++) {
		ret = device_create_file(&pdev->dev,&axp_regu_attrs[j]);
		if (ret)
			goto sysfs_failed;
	}
    goto succeed;
	
sysfs_failed:
	while (j--)
		device_remove_file(&pdev->dev,&axp_regu_attrs[j]);
succeed:
	return ret;
}

static inline struct axp_regulator_info *find_regulator_info(int id)
{
	struct axp_regulator_info *ri;
	int i;

	for (i = 0; i < ARRAY_SIZE(axp_regulator_info); i++) {
		ri = &axp_regulator_info[i];
		if (ri->desc.id == id)
			return ri;
	}
	return NULL;
}

static int __devinit axp_regulator_probe(struct platform_device *pdev)
{
	struct axp_regulator_info *ri = NULL;
	struct regulator_dev *rdev;
	int ret;

	ri = find_regulator_info(pdev->id);
	if (ri == NULL) {
		dev_err(&pdev->dev, "invalid regulator ID specified\n");
		return -EINVAL;
	}

	if (ri->desc.id == AXP20_ID_LDO1 || ri->desc.id == AXP20_ID_LDO2 \
		|| ri->desc.id == AXP20_ID_LDO3 || ri->desc.id == AXP20_ID_BUCK2 \
		||ri->desc.id == AXP20_ID_BUCK3)
		ri->desc.ops = &axp20_ops;
	
	if(ri->desc.id == AXP20_ID_LDO4)
		ri->desc.ops = &axp20_ldo4_ops;


	if(ri->desc.id == AXP20_ID_LDOIO0)
		ri->desc.ops = &axp20_ldoio0_ops;


	rdev = regulator_register(&ri->desc, &pdev->dev,
				  pdev->dev.platform_data, ri, NULL);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "failed to register regulator %s\n",
				ri->desc.name);
		return PTR_ERR(rdev);
	}
	platform_set_drvdata(pdev, rdev);
	
	if(ri->desc.id == AXP20_ID_BUCK2 ||ri->desc.id == AXP20_ID_BUCK3){
		ret = axp_regu_create_attrs(pdev);
		if(ret){
			return ret;
		}
	}
	
	return 0;
}

static int __devexit axp_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);

	regulator_unregister(rdev);
	return 0;
}

static struct platform_driver axp_regulator_driver = {
	.driver	= {
		.name	= "axp20-regulator",
		.owner	= THIS_MODULE,
	},
	.probe		= axp_regulator_probe,
	.remove		= axp_regulator_remove,
};

static int __init axp_regulator_init(void)
{
	return platform_driver_register(&axp_regulator_driver);
}
module_init(axp_regulator_init);

static void __exit axp_regulator_exit(void)
{
	platform_driver_unregister(&axp_regulator_driver);
}
module_exit(axp_regulator_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Donglu Zhang");
MODULE_DESCRIPTION("Regulator Driver for Krosspower AXP20 PMIC");
MODULE_ALIAS("platform:axp-regulator");
