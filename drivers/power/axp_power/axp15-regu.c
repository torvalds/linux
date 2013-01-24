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

static int axp15_ldo0_data[] = { 5000, 3300, 2800, 2500};
static int axp15_dcdc1_data[] = { 1700, 1800, 1900, 2000, 2100, 2400, 2500, 2600,\
	2700, 2800, 3000, 3100, 3200, 3300, 3400, 3500};
static int axp15_aldo12_data[] = { 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900,\
	2000, 2500, 2700, 2800, 3000, 3100, 3200, 3300};

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


/* AXP common operations */
static int axp_set_voltage(struct regulator_dev *rdev,
				  int min_uV, int max_uV)
{
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);
	uint8_t val, mask;


	if (check_range(info, min_uV, max_uV)) {
		pr_err("invalid voltage range (%d, %d) uV\n", min_uV, max_uV);
		return -EINVAL;
	}

	val = (min_uV - info->min_uV + info->step_uV - 1) / info->step_uV;
	val <<= info->vol_shift;
	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;
	printk("reg=[%x],val=%d,mask=%d,line=%d\n",info->vol_reg,val,mask,__LINE__);
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

	if(info->desc.id == AXP15_ID_LDO0)
		return axp15_ldo0_data[selector] * 1000;
	if(info->desc.id == AXP15_ID_DCDC1)
		return axp15_dcdc1_data[selector] * 1000;
	if(info->desc.id == AXP15_ID_LDO2)
		return axp15_aldo12_data[selector] * 1000;
	if(info->desc.id == AXP15_ID_LDO3)
		return axp15_aldo12_data[selector] * 1000;

	ret = info->min_uV + info->step_uV * selector;
	if (ret > info->max_uV)
		return -EINVAL;
	return ret;
}

static int axp_set_ldo0_voltage(struct regulator_dev *rdev,
				  int min_uV, int max_uV)
{
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);
	uint8_t val, mask;
	int i;

	if (check_range(info, min_uV, max_uV)) {
		pr_err("invalid voltage range (%d, %d) uV\n", min_uV, max_uV);
		return -EINVAL;
	}

	for(i = 0,val = 0; i < sizeof(axp15_ldo0_data);i++){
		if(min_uV <= axp15_ldo0_data[i] * 1000){
			val = i;
			break;
		}
	}
	val <<= info->vol_shift;
	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;
	return axp_update(axp_dev, info->vol_reg, val, mask);
}

static int axp_set_dcdc1_voltage(struct regulator_dev *rdev,
				  int min_uV, int max_uV)
{
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);
	uint8_t val, mask;
	int i;

	if (check_range(info, min_uV, max_uV)) {
		pr_err("invalid voltage range (%d, %d) uV\n", min_uV, max_uV);
		return -EINVAL;
	}

	for(i = 0,val = 0; i < sizeof(axp15_dcdc1_data);i++){
		if(min_uV <= axp15_dcdc1_data[i] * 1000){
			val = i;
			break;
		}
	}
	val <<= info->vol_shift;
	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;
	return axp_update(axp_dev, info->vol_reg, val, mask);
}

static int axp_set_aldo12_voltage(struct regulator_dev *rdev,
				  int min_uV, int max_uV)
{
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);
	uint8_t val, mask;
	int i;

	if (check_range(info, min_uV, max_uV)) {
		pr_err("invalid voltage range (%d, %d) uV\n", min_uV, max_uV);
		return -EINVAL;
	}

	for(i = 0,val = 0; i < sizeof(axp15_aldo12_data);i++){
		if(min_uV <= axp15_aldo12_data[i] * 1000){
			val = i;
			break;
		}
	}
	val <<= info->vol_shift;
	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;
	return axp_update(axp_dev, info->vol_reg, val, mask);
}

/*static int axp_set_ldoio0_voltage(struct regulator_dev *rdev,
				  int min_uV, int max_uV)
{
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);
	uint8_t val, mask;
	int i;

	if (check_range(info, min_uV, max_uV)) {
		pr_err("invalid voltage range (%d, %d) uV\n", min_uV, max_uV);
		return -EINVAL;
	}

	for(i = 0,val = 0; i < sizeof(axp15_ldoio0_data);i++){
		if(min_uV <= axp15_ldoio0_data[i] * 1000){
			val = i;
			break;
		}
	}
	val <<= info->vol_shift;
	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;
	return axp_update(axp_dev, info->vol_reg, val, mask);
}*/

static int axp_get_ldo0_voltage(struct regulator_dev *rdev)
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
	ret = axp15_ldo0_data[val]*1000;
	return ret;
}

static int axp_get_dcdc1_voltage(struct regulator_dev *rdev)
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
	ret = axp15_dcdc1_data[val]*1000;
	return ret;
}

static int axp_get_aldo12_voltage(struct regulator_dev *rdev)
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
	ret = axp15_aldo12_data[val]*1000;
	return ret;
}

static int axp_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	int ldo = rdev_get_id(rdev);
printk("%s,%d\n", __func__, __LINE__);
	switch (ldo) {

	case AXP15_ID_LDO0:
		printk("%s,line:%d\n", __func__, __LINE__);
		return axp_set_ldo0_voltage(rdev, uV, uV);
	case AXP15_ID_LDO2:
		printk("%s,line:%d\n", __func__, __LINE__);
		return axp_set_aldo12_voltage(rdev, uV, uV);
	case AXP15_ID_LDO3:
		printk("%s,line:%d\n", __func__, __LINE__);
		return axp_set_aldo12_voltage(rdev, uV, uV);
	case AXP15_ID_LDO4:
		printk("%s,line:%d\n", __func__, __LINE__);
		return axp_set_voltage(rdev, uV, uV);
	case AXP15_ID_LDO5:
		printk("%s,line:%d\n", __func__, __LINE__);
		return axp_set_voltage(rdev, uV, uV);
	case AXP15_ID_DCDC1:
		printk("%s,line:%d\n", __func__, __LINE__);
		return axp_set_dcdc1_voltage(rdev, uV, uV);
	case AXP15_ID_DCDC2:
		printk("%s,line:%d\n", __func__, __LINE__);
		return axp_set_voltage(rdev, uV, uV);
	case AXP15_ID_DCDC3:
		printk("%s,line:%d\n", __func__, __LINE__);
		return axp_set_voltage(rdev, uV, uV);
	case AXP15_ID_DCDC4:
		printk("%s,line:%d\n", __func__, __LINE__);
		return axp_set_voltage(rdev, uV, uV);
	case AXP15_ID_LDO1:
		printk("%s,line:%d\n", __func__, __LINE__);
		return axp_set_voltage(rdev, uV, uV);
	case AXP15_ID_LDOIO0:
		printk("%s,line:%d\n", __func__, __LINE__);
		return axp_set_voltage(rdev, uV, uV);
	default:
		return -EINVAL;
	}
}


static struct regulator_ops axp15_ops = {
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

static struct regulator_ops axp15_ldo0_ops = {
	.set_voltage	= axp_set_ldo0_voltage,
	.get_voltage	= axp_get_ldo0_voltage,
	.list_voltage	= axp_list_voltage,
	.enable		= axp_enable,
	.disable	= axp_disable,
	.is_enabled	= axp_is_enabled,
	.set_suspend_enable		= axp_enable,
	.set_suspend_disable	= axp_disable,
	.set_suspend_voltage	= axp_set_suspend_voltage,
};

static struct regulator_ops axp15_dcdc1_ops = {
	.set_voltage	= axp_set_dcdc1_voltage,
	.get_voltage	= axp_get_dcdc1_voltage,
	.list_voltage	= axp_list_voltage,
	.enable		= axp_enable,
	.disable	= axp_disable,
	.is_enabled	= axp_is_enabled,
	.set_suspend_enable		= axp_enable,
	.set_suspend_disable	= axp_disable,
	.set_suspend_voltage	= axp_set_suspend_voltage,
};

static struct regulator_ops axp15_aldo12_ops = {
	.set_voltage	= axp_set_aldo12_voltage,
	.get_voltage	= axp_get_aldo12_voltage,
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

static struct regulator_ops axp15_ldoio0_ops = {
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


#define AXP15_LDO(_id, min, max, step, vreg, shift, nbits, ereg, ebit)	\
	AXP_LDO(AXP15, _id, min, max, step, vreg, shift, nbits, ereg, ebit)

#define AXP15_BUCK(_id, min, max, step, vreg, shift, nbits, ereg, ebit)	\
	AXP_BUCK(AXP15, _id, min, max, step, vreg, shift, nbits, ereg, ebit)

#define AXP15_DCDC(_id, min, max, step, vreg, shift, nbits, ereg, ebit)	\
	AXP_DCDC(AXP15, _id, min, max, step, vreg, shift, nbits, ereg, ebit)

static struct axp_regulator_info axp_regulator_info[] = {
    AXP15_LDO(	0,	2500,	    5000,	    833,	LDO0,		4,	2,	LDO0EN,		7),//ldo0
	AXP15_LDO(	1,	1200,	    3100,	    100,	RTC,	    0,	0,	RTCLDOEN,	0),//ldo1 for rtc
	AXP15_LDO(	2,	1200,		3300,		120,	ANALOG1,	4,	4,	ANALOG1EN,	3),//ldo2 for analog1
	AXP15_LDO(	3,	1200,		3300,		120,	ANALOG2,	0,	4,	ANALOG2EN,	2),//ldo3 for analog2
	AXP15_LDO(	4,	700,		3500,		100,	DIGITAL1,	0,	4,	ANALOG1EN,	1),//ldo2 for DigtalLDO1
	AXP15_LDO(	5,	700,		3500,		100,	DIGITAL2,	0,	4,	ANALOG2EN,	0),//ldo3 for DigtalLDO2
	AXP15_DCDC(	1,	1700,		3500,		120,	DCDC1,		0,	4,	DCDC1EN,	7) , //buck1
	AXP15_DCDC(	2,	700,		2275,		25,		DCDC2,		0,	6,	DCDC2EN,	6),//buck2
	AXP15_DCDC(	3,	700,		3500,		50,		DCDC3,		0,	6,	DCDC3EN,	5),//buck3
	AXP15_DCDC(	4,	700,		3500,		25,		DCDC4,		0,	7,	DCDC4EN,	4),//buck4
	AXP15_LDO(	IO0,	1800,		3300,		100,	LDOIO0,		0,	4,	LDOI0EN,	1),//ldoio0
};

static ssize_t workmode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	struct axp_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *axp_dev = to_axp_dev(rdev);
	int ret;
	uint8_t val;
	ret = axp_read(axp_dev, AXP15_BUCKMODE, &val);
	if (ret)
		return sprintf(buf, "IO ERROR\n");

	if(info->desc.id == AXP15_ID_DCDC1){
		switch (val & 0x08) {
			case 0:return sprintf(buf, "AUTO\n");
			case 8:return sprintf(buf, "PWM\n");
			default:return sprintf(buf, "UNKNOWN\n");
		}
	}
	if(info->desc.id == AXP15_ID_DCDC2){
		switch (val & 0x04) {
			case 0:return sprintf(buf, "AUTO\n");
			case 4:return sprintf(buf, "PWM\n");
			default:return sprintf(buf, "UNKNOWN\n");
		}
	}
	if(info->desc.id == AXP15_ID_DCDC3){
		switch (val & 0x02) {
			case 0:return sprintf(buf, "AUTO\n");
			case 2:return sprintf(buf, "PWM\n");
			default:return sprintf(buf, "UNKNOWN\n");
		}
	}
	else if(info->desc.id == AXP15_ID_DCDC4){
		switch (val & 0x01) {
			case 0:return sprintf(buf, "AUTO\n");
			case 1:return sprintf(buf, "PWM\n");
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

	if(info->desc.id == AXP15_ID_DCDC1){
		if(val)
			axp_set_bits(axp_dev, AXP15_BUCKMODE,0x08);
		else
			axp_clr_bits(axp_dev, AXP15_BUCKMODE,0x08);
	}
	if(info->desc.id == AXP15_ID_DCDC2){
		if(val)
			axp_set_bits(axp_dev, AXP15_BUCKMODE,0x04);
		else
			axp_clr_bits(axp_dev, AXP15_BUCKMODE,0x04);
	}
	if(info->desc.id == AXP15_ID_DCDC3){
		if(val)
			axp_set_bits(axp_dev, AXP15_BUCKMODE,0x02);
		else
			axp_clr_bits(axp_dev, AXP15_BUCKMODE,0x02);
	}
	else if(info->desc.id == AXP15_ID_DCDC4){
		if(val)
			axp_set_bits(axp_dev, AXP15_BUCKMODE,0x01);
		else
			axp_clr_bits(axp_dev, AXP15_BUCKMODE,0x01);
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
	ret = axp_read(axp_dev, AXP15_BUCKFREQ, &val);
	if (ret)
		return ret;
	ret = val & 0x0F;
	return sprintf(buf, "%d\n",(ret*5 + 50));
}

static ssize_t frequency_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	struct device *axp_dev = to_axp_dev(rdev);
	uint8_t val,tmp;
	int var;
	var = simple_strtoul(buf, NULL, 10);
	if(var < 50)
		var = 50;
	if(var > 100)
		var = 100;

	val = (var -50)/5;
	val &= 0x0F;

	axp_read(axp_dev, AXP15_BUCKFREQ, &tmp);
	tmp &= 0xF0;
	val |= tmp;
	axp_write(axp_dev, AXP15_BUCKFREQ, val);
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

	if (ri->desc.id == AXP15_ID_LDO4 || ri->desc.id == AXP15_ID_LDO5 \
		||ri->desc.id == AXP15_ID_DCDC1 ||ri->desc.id == AXP15_ID_DCDC2 \
		||ri->desc.id == AXP15_ID_DCDC3 ||ri->desc.id == AXP15_ID_DCDC4 \
		||ri->desc.id == AXP15_ID_LDO1 ||ri->desc.id == AXP15_ID_LDOIO0)
		{
			ri->desc.ops = &axp15_ops;
			printk("Register AXP15_OPS sucess!\n");
		}


	if(ri->desc.id == AXP15_ID_LDO0)
	{
		ri->desc.ops = &axp15_ldo0_ops;
		printk("Register AXP15_ldo0_OPS finish!\n");
	}

	if(ri->desc.id == AXP15_ID_LDO3 || ri->desc.id == AXP15_ID_LDO2)
	{
		ri->desc.ops = &axp15_aldo12_ops;
		printk("Register AXP15_aldo12_OPS finish!\n");
		}

	if(ri->desc.id == AXP15_ID_DCDC1)
		{
		ri->desc.ops = &axp15_dcdc1_ops;
		printk("Register AXP15_dcdc1_OPS finish!\n");
		}

	rdev = regulator_register(&ri->desc, &pdev->dev,
				  pdev->dev.platform_data, ri);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "failed to register regulator %s\n",
				ri->desc.name);
		return PTR_ERR(rdev);
	}
	platform_set_drvdata(pdev, rdev);

	if(ri->desc.id == AXP15_ID_DCDC1 ||ri->desc.id == AXP15_ID_DCDC2 \
		||ri->desc.id == AXP15_ID_DCDC3 ||ri->desc.id == AXP15_ID_DCDC4){
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
		.name	= "axp15-regulator",
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
MODULE_AUTHOR("Kyle Cheung");
MODULE_DESCRIPTION("Regulator Driver for AXP15 PMIC");
MODULE_ALIAS("platform:axp-regulator");
