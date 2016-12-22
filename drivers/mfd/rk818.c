/*
 * Regulator driver for rk818 PMIC chip for rk31xx
 *
 * Based on rk818.c that is work by zhangqing<zhangqing@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/bug.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/regulator/driver.h>
#include <linux/mfd/rk818.h>
#include <linux/mfd/core.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regmap.h>
#include <linux/syscore_ops.h>


#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif
#if 1
#define DBG_INFO(x...)	printk(KERN_INFO x)
#else
#define DBG_INFO(x...)
#endif
#define PM_CONTROL

struct rk818 *g_rk818;

static struct mfd_cell rk818s[] = {
	{
		.name = "rk818-rtc",
	},

	{
		.name = "rk818-battery",
	},
	/*	{
		.name = "rk818-power",
	},	
	*/
};

#define BUCK_VOL_MASK 0x3f
#define LDO_VOL_MASK 0x3f
#define LDO9_VOL_MASK 0x1f
#define BOOST_VOL_MASK 0xe0

#define VOL_MIN_IDX 0x00
#define VOL_MAX_IDX 0x3f
#define RK818_I2C_ADDR_RATE  200*1000

const static int buck_set_vol_base_addr[] = {
	RK818_BUCK1_ON_REG,
	RK818_BUCK2_ON_REG,
	RK818_BUCK3_CONFIG_REG,
	RK818_BUCK4_ON_REG,
};
const static int buck_set_slp_vol_base_addr[] = {
	RK818_BUCK1_SLP_REG,
	RK818_BUCK2_SLP_REG,
	RK818_BUCK3_CONFIG_REG,
	RK818_BUCK4_SLP_VSEL_REG,
};
const static int buck_contr_base_addr[] = {
	RK818_BUCK1_CONFIG_REG,
 	RK818_BUCK2_CONFIG_REG,
 	RK818_BUCK3_CONFIG_REG,
 	RK818_BUCK4_CONFIG_REG,
};
#define rk818_BUCK_SET_VOL_REG(x) (buck_set_vol_base_addr[x])
#define rk818_BUCK_CONTR_REG(x) (buck_contr_base_addr[x])
#define rk818_BUCK_SET_SLP_VOL_REG(x) (buck_set_slp_vol_base_addr[x])


const static int ldo_set_vol_base_addr[] = {
	RK818_LDO1_ON_VSEL_REG,
	RK818_LDO2_ON_VSEL_REG,
	RK818_LDO3_ON_VSEL_REG,
	RK818_LDO4_ON_VSEL_REG, 
	RK818_LDO5_ON_VSEL_REG, 
	RK818_LDO6_ON_VSEL_REG, 
	RK818_LDO7_ON_VSEL_REG, 
	RK818_LDO8_ON_VSEL_REG, 
	RK818_BOOST_LDO9_ON_VSEL_REG, 
};
const static int ldo_set_slp_vol_base_addr[] = {
	RK818_LDO1_SLP_VSEL_REG,
	RK818_LDO2_SLP_VSEL_REG,
	RK818_LDO3_SLP_VSEL_REG,
	RK818_LDO4_SLP_VSEL_REG, 
	RK818_LDO5_SLP_VSEL_REG, 
	RK818_LDO6_SLP_VSEL_REG, 
	RK818_LDO7_SLP_VSEL_REG, 
	RK818_LDO8_SLP_VSEL_REG, 
	RK818_BOOST_LDO9_SLP_VSEL_REG, 
};

#define rk818_LDO_SET_VOL_REG(x) (ldo_set_vol_base_addr[x])
#define rk818_LDO_SET_SLP_VOL_REG(x) (ldo_set_slp_vol_base_addr[x])

const static int buck_voltage_map[] = {
	  712,  725,  737,  750, 762,  775,  787,  800,
	  812,  825,  837,  850,862,  875,  887,  900,  912,
	  925,  937,  950, 962,  975,  987, 1000, 1012, 1025, 
	  1037, 1050,1062, 1075, 1087, 1100, 1112, 1125, 1137, 
	  1150,1162, 1175, 1187, 1200, 1212, 1225, 1237, 1250,
	  1262, 1275, 1287, 1300, 1312, 1325, 1337, 1350,1362, 
	  1375, 1387, 1400, 1412, 1425, 1437, 1450,1462, 1475, 
	  1487, 1500,
};

const static int buck4_voltage_map[] = {
           1800, 1900, 2000, 2100, 2200,  2300,  2400, 2500, 2600,
          2700, 2800, 2900, 3000, 3100, 3200,3300, 3400,3500,3600,
};

const static int ldo_voltage_map[] = {
	  1800, 1900, 2000, 2100, 2200,  2300,  2400, 2500, 2600, 
	  2700, 2800, 2900, 3000, 3100, 3200,3300, 3400, 
};
const static int ldo3_voltage_map[] = {
	 800, 900, 1000, 1100, 1200,  1300, 1400, 1500, 1600, 
	 1700, 1800, 1900,  2000,2100,  2200,  2500,
};
const static int ldo6_voltage_map[] = {
	 800, 900, 1000, 1100, 1200,  1300, 1400, 1500, 1600, 
	 1700, 1800, 1900,  2000,2100,  2200,  2300,2400,2500,
};
const static int boost_voltage_map[] = {
	 4700,4800,4900,5000,5100,5200,5300,5400,
};

static int rk818_ldo_list_voltage(struct regulator_dev *dev, unsigned index)
{
	int ldo= rdev_get_id(dev) - RK818_LDO1;
	if (ldo == 2){
	if (index >= ARRAY_SIZE(ldo3_voltage_map))
		return -EINVAL;
	return 1000 * ldo3_voltage_map[index];
	}
	else if (ldo == 5 || ldo ==6){
	if (index >= ARRAY_SIZE(ldo6_voltage_map))
		return -EINVAL;
	return 1000 * ldo6_voltage_map[index];
	}
	else{
	if (index >= ARRAY_SIZE(ldo_voltage_map))
		return -EINVAL;
	return 1000 * ldo_voltage_map[index];
	}
}
static int rk818_ldo_is_enabled(struct regulator_dev *dev)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - RK818_LDO1;	
	u16 val;

	if (ldo == 8){
		val = rk818_reg_read(rk818, RK818_DCDC_EN_REG);  //ldo9
		if (val < 0)
			return val;
		if (val & (1 << 5))
			return 1;
		else
			return 0; 
	}
	val = rk818_reg_read(rk818, RK818_LDO_EN_REG);
	if (val < 0)
		return val;
	if (val & (1 << ldo))
		return 1;
	else
		return 0; 		
}
static int rk818_ldo_enable(struct regulator_dev *dev)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - RK818_LDO1;

	if (ldo == 8)
		 rk818_set_bits(rk818, RK818_DCDC_EN_REG, 1 << 5, 1 << 5); //ldo9
	else if (ldo ==9)
		rk818_set_bits(rk818, RK818_DCDC_EN_REG, 1 << 6, 1 << 6); //ldo10 switch
	else
	 	rk818_set_bits(rk818, RK818_LDO_EN_REG, 1 << ldo, 1 << ldo);

	 return 0;	
}
static int rk818_ldo_disable(struct regulator_dev *dev)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - RK818_LDO1;

	if (ldo == 8)
		 rk818_set_bits(rk818, RK818_DCDC_EN_REG, 1 << 5, 1 << 0); //ldo9
	else if(ldo ==9)
		rk818_set_bits(rk818, RK818_DCDC_EN_REG, 1 << 6, 1 << 0); //ldo10 switch
	else
	 	rk818_set_bits(rk818, RK818_LDO_EN_REG, 1 << ldo, 0);

	 return 0;
}
static int rk818_ldo_get_voltage(struct regulator_dev *dev)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - RK818_LDO1;
	u16 reg = 0;
	int val;
	
	if  (ldo ==9){
		reg = rk818_reg_read(rk818,rk818_BUCK_SET_VOL_REG(3));
		reg &= BUCK_VOL_MASK;
		val = 1000 * buck4_voltage_map[reg];		
	}
	else{
		reg = rk818_reg_read(rk818,rk818_LDO_SET_VOL_REG(ldo));
		if (ldo == 8){
			reg &= LDO9_VOL_MASK;
		}
		else
			reg &= LDO_VOL_MASK;
	
		if (ldo ==2){
			val = 1000 * ldo3_voltage_map[reg];	
		}
		else if (ldo == 5 || ldo ==6){
			val = 1000 * ldo6_voltage_map[reg];	
		}
		else{
			val = 1000 * ldo_voltage_map[reg];	
		}
	}
	return val;
}
static int rk818_ldo_suspend_enable(struct regulator_dev *dev)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - RK818_LDO1;

	if (ldo == 8)
		return rk818_set_bits(rk818, RK818_SLEEP_SET_OFF_REG1, 1 << 5, 0); //ldo9
	else if (ldo ==9)
		return rk818_set_bits(rk818, RK818_SLEEP_SET_OFF_REG1, 1 << 6, 0); //ldo10 switch
	else
		return rk818_set_bits(rk818, RK818_SLEEP_SET_OFF_REG2, 1 << ldo, 0);
	
}
static int rk818_ldo_suspend_disable(struct regulator_dev *dev)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - RK818_LDO1;
	
	if (ldo == 8)
		return rk818_set_bits(rk818, RK818_SLEEP_SET_OFF_REG1, 1 << 5, 1 << 5); //ldo9
	else if (ldo ==9)
		return rk818_set_bits(rk818, RK818_SLEEP_SET_OFF_REG1, 1 << 6, 1 << 6); //ldo10 switch
	else
		return rk818_set_bits(rk818, RK818_SLEEP_SET_OFF_REG2, 1 << ldo, 1 << ldo);

}

int rk818_ldo_slp_enable(int ldo_id)
{
	int ldo = ldo_id - 1;

	if (ldo == 8)
		return rk818_set_bits(g_rk818, RK818_SLEEP_SET_OFF_REG1,
				      1 << 5, 0); /*ldo9*/
	else if (ldo == 9)
		return rk818_set_bits(g_rk818, RK818_SLEEP_SET_OFF_REG1,
				      1 << 6, 0); /*ldo10 switch*/
	else
		return rk818_set_bits(g_rk818, RK818_SLEEP_SET_OFF_REG2,
				      1 << ldo, 0);
}

int rk818_ldo_slp_disable(int ldo_id)
{
	int ldo = ldo_id - 1;

	if (ldo == 8)
		return rk818_set_bits(g_rk818, RK818_SLEEP_SET_OFF_REG1,
				      1 << 5, 1 << 5); /*ldo9*/
	else if (ldo == 9)
		return rk818_set_bits(g_rk818, RK818_SLEEP_SET_OFF_REG1,
				      1 << 6, 1 << 6); /*ldo10 switch*/
	else
		return rk818_set_bits(g_rk818, RK818_SLEEP_SET_OFF_REG2,
				      1 << ldo, 1 << ldo);
}

static int rk818_ldo_set_sleep_voltage(struct regulator_dev *dev,
					    int uV)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - RK818_LDO1;
	const int *vol_map = ldo_voltage_map;
	int min_vol = uV / 1000;
	u16 val;
	int ret = 0,num =0;
	
	if (ldo ==2){
	vol_map = ldo3_voltage_map;	
	num = 15;
	}
	else if (ldo == 5 || ldo ==6){
	vol_map = ldo6_voltage_map;		
	num = 17;
	}
	else {
	num = 16;
	}
	
	if (min_vol < vol_map[0] ||
	    min_vol > vol_map[num])
		return -EINVAL;

	for (val = 0; val <= num; val++){
		if (vol_map[val] >= min_vol)
			break;	
        }

	if (ldo == 8){
		ret = rk818_set_bits(rk818, rk818_LDO_SET_SLP_VOL_REG(ldo),LDO9_VOL_MASK, val);
	}
	else
		ret = rk818_set_bits(rk818, rk818_LDO_SET_SLP_VOL_REG(ldo),LDO_VOL_MASK, val);
	
	return ret;
}

static int rk818_ldo_set_voltage(struct regulator_dev *dev,
				  int min_uV, int max_uV,unsigned *selector)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - RK818_LDO1;
	const int *vol_map;
	int min_vol = min_uV / 1000;
	u16 val;
	int ret = 0,num =0;
	
	if (ldo ==2){
	vol_map = ldo3_voltage_map;	
	num = 15;
	}
	else if (ldo == 5 || ldo ==6){
	vol_map = ldo6_voltage_map;		
	num = 17;
	}
	else {
	vol_map = ldo_voltage_map;
	num = 16;
	}
	
	if (min_vol < vol_map[0] ||
	    min_vol > vol_map[num])
		return -EINVAL;

	for (val = 0; val <= num; val++){
		if (vol_map[val] >= min_vol)
			break;	
        }

	if (ldo == 8){
		ret = rk818_set_bits(rk818, rk818_LDO_SET_VOL_REG(ldo),LDO9_VOL_MASK, val);
	}
	else
		ret = rk818_set_bits(rk818, rk818_LDO_SET_VOL_REG(ldo),LDO_VOL_MASK, val);
	
	return ret;

}

static struct regulator_ops rk818_ldo_ops = {
	.set_voltage = rk818_ldo_set_voltage,
	.get_voltage = rk818_ldo_get_voltage,
	.list_voltage = rk818_ldo_list_voltage,
	.is_enabled = rk818_ldo_is_enabled,
	.enable = rk818_ldo_enable,
	.disable = rk818_ldo_disable,
	.set_suspend_enable =rk818_ldo_suspend_enable,
	.set_suspend_disable =rk818_ldo_suspend_disable,
	.set_suspend_voltage = rk818_ldo_set_sleep_voltage,	
};

static int rk818_dcdc_list_voltage(struct regulator_dev *dev, unsigned selector)
{
	int volt;
	int buck = rdev_get_id(dev) - RK818_DCDC1;

	if (selector < 0x0 ||selector > BUCK_VOL_MASK )
		return -EINVAL;

	switch (buck) {
	case 0:
	case 1:
		volt = 712500 + selector * 12500;
		break;
	case 3:
		volt = 1800000 + selector * 100000;
		break;
	case 2:
		volt = 1200000;
		break;
	default:
		BUG();
		return -EINVAL;
	}

	return  volt ;
}
static int rk818_dcdc_is_enabled(struct regulator_dev *dev)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK818_DCDC1;
	u16 val;
	
	val = rk818_reg_read(rk818, RK818_DCDC_EN_REG);
	if (val < 0)
		return val;
	if (val & (1 << buck))
		return 1;
	else
		return 0; 	
}
static int rk818_dcdc_enable(struct regulator_dev *dev)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK818_DCDC1;

	return rk818_set_bits(rk818, RK818_DCDC_EN_REG, 1 << buck, 1 << buck);

}
static int rk818_dcdc_disable(struct regulator_dev *dev)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK818_DCDC1;
	
	 return rk818_set_bits(rk818, RK818_DCDC_EN_REG, 1 << buck , 0);
}
static int rk818_dcdc_get_voltage(struct regulator_dev *dev)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK818_DCDC1;
	u16 reg = 0;
	int val;

	reg = rk818_reg_read(rk818,rk818_BUCK_SET_VOL_REG(buck));
	
	reg &= BUCK_VOL_MASK;
    	val = rk818_dcdc_list_voltage(dev,reg);
	return val;
}
static int rk818_dcdc_select_min_voltage(struct regulator_dev *dev,
					   int min_uV, int max_uV ,int buck)
{
	u16 vsel =0;
	
	if (buck == 0 || buck ==  1){
		if (min_uV < 712500)
		vsel = 0;
		else if (min_uV <= 1500000)
		vsel = ((min_uV - 712500) / 12500) ;
		else
		return -EINVAL;
	}
	else if (buck ==3){
		if (min_uV < 1800000)
		vsel = 0;
		else if (min_uV <= 3300000)
		vsel = ((min_uV - 1800000) / 100000) ;
		else
		return -EINVAL;
	}
	if (rk818_dcdc_list_voltage(dev, vsel) > max_uV)
		return -EINVAL;
	return vsel;
}

static int rk818_dcdc_set_voltage(struct regulator_dev *dev,
				  int min_uV, int max_uV,unsigned *selector)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK818_DCDC1;
	u16 val;
	int ret = 0;

	if (buck ==2){
		return 0;
	}else if (buck==3){
		val = rk818_dcdc_select_min_voltage(dev,min_uV,max_uV,buck);	
		ret = rk818_set_bits(rk818, rk818_BUCK_SET_VOL_REG(buck), BUCK_VOL_MASK, val);
	}
	val = rk818_dcdc_select_min_voltage(dev,min_uV,max_uV,buck);
	ret = rk818_set_bits(rk818, rk818_BUCK_SET_VOL_REG(buck), BUCK_VOL_MASK, val);
	return ret;
}
static int rk818_dcdc_set_sleep_voltage(struct regulator_dev *dev,
					    int uV)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK818_DCDC1;
	u16 val;
	int ret = 0;

	if (buck ==2){
	return 0;
	}else{
	val = rk818_dcdc_select_min_voltage(dev,uV,uV,buck);
	ret = rk818_set_bits(rk818, rk818_BUCK_SET_SLP_VOL_REG(buck) , BUCK_VOL_MASK, val);
	}
	return ret;
}
static unsigned int rk818_dcdc_get_mode(struct regulator_dev *dev)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK818_DCDC1;
	u16 mask = 0x80;
	u16 val;
	val = rk818_reg_read(rk818, rk818_BUCK_SET_VOL_REG(buck));
        if (val < 0) {
                return val;
        }
	val=val & mask;
	if (val== mask)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;

}
static int rk818_dcdc_set_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK818_DCDC1;
	u16 mask = 0x80;
	switch(mode)
	{
	case REGULATOR_MODE_FAST:
		return rk818_set_bits(rk818, rk818_BUCK_SET_VOL_REG(buck), mask, mask);
	case REGULATOR_MODE_NORMAL:
		return rk818_set_bits(rk818, rk818_BUCK_SET_VOL_REG(buck), mask, 0);
	default:
		printk("error:pmu_rk818 only powersave and pwm mode\n");
		return -EINVAL;
	}

}
static int rk818_dcdc_set_voltage_time_sel(struct regulator_dev *dev,   unsigned int old_selector,
				     unsigned int new_selector)
{
	int old_volt, new_volt;
	
	old_volt = rk818_dcdc_list_voltage(dev, old_selector);
	if (old_volt < 0)
		return old_volt;
	
	new_volt = rk818_dcdc_list_voltage(dev, new_selector);
	if (new_volt < 0)
		return new_volt;

	return DIV_ROUND_UP(abs(old_volt - new_volt)*2, 2500);
}

static int rk818_dcdc_suspend_enable(struct regulator_dev *dev)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK818_DCDC1;

	return rk818_set_bits(rk818, RK818_SLEEP_SET_OFF_REG1, 1 << buck, 0);

}
static int rk818_dcdc_suspend_disable(struct regulator_dev *dev)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK818_DCDC1;
	
	 return rk818_set_bits(rk818, RK818_SLEEP_SET_OFF_REG1, 1 << buck , 1 << buck);
}
static int rk818_dcdc_set_suspend_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct rk818 *rk818 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK818_DCDC1;
	u16 mask = 0x80;

	switch(mode)
	{
	case REGULATOR_MODE_FAST:
		return rk818_set_bits(rk818, (rk818_BUCK_SET_VOL_REG(buck) + 0x01), mask, mask);
	case REGULATOR_MODE_NORMAL:
		return rk818_set_bits(rk818, (rk818_BUCK_SET_VOL_REG(buck) + 0x01), mask, 0);
	default:
		printk("error:pmu_rk818 only powersave and pwm mode\n");
		return -EINVAL;
	}

}
static struct regulator_ops rk818_dcdc_ops = { 
	.set_voltage = rk818_dcdc_set_voltage,
	.get_voltage = rk818_dcdc_get_voltage,
	.list_voltage= rk818_dcdc_list_voltage,
	.is_enabled = rk818_dcdc_is_enabled,
	.enable = rk818_dcdc_enable,
	.disable = rk818_dcdc_disable,
	.get_mode = rk818_dcdc_get_mode,
	.set_mode = rk818_dcdc_set_mode,
	.set_suspend_enable =rk818_dcdc_suspend_enable,
	.set_suspend_disable =rk818_dcdc_suspend_disable,
	.set_suspend_mode = rk818_dcdc_set_suspend_mode,
	.set_suspend_voltage = rk818_dcdc_set_sleep_voltage,
	.set_voltage_time_sel = rk818_dcdc_set_voltage_time_sel,
};
static struct regulator_desc regulators[] = {

        {
		.name = "RK818_DCDC1",
		.id = 0,
		.ops = &rk818_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK818_DCDC2",
		.id = 1,
		.ops = &rk818_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK818_DCDC3",
		.id = 2,
		.ops = &rk818_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck4_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK818_DCDC4",
		.id = 3,
		.ops = &rk818_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck4_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},

	{
		.name = "RK818_LDO1",
		.id =4,
		.ops = &rk818_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK818_LDO2",
		.id = 5,
		.ops = &rk818_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK818_LDO3",
		.id = 6,
		.ops = &rk818_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo3_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK818_LDO4",
		.id = 7,
		.ops = &rk818_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},

	{
		.name = "RK818_LDO5",
		.id =8,
		.ops = &rk818_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK818_LDO6",
		.id = 9,
		.ops = &rk818_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo6_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK818_LDO7",
		.id = 10,
		.ops = &rk818_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo6_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK818_LDO8",
		.id = 11,
		.ops = &rk818_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK818_LDO9",
		.id = 12,
		.ops = &rk818_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK818_LDO10",
		.id = 13,
		.ops = &rk818_ldo_ops,
		.n_voltages = ARRAY_SIZE(buck4_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	
};

/*
 *
 */
 int rk818_i2c_read(struct rk818 *rk818, char reg, int count,u8 *dest)
{
	struct i2c_client *i2c = rk818->i2c;

      int ret;
    struct i2c_adapter *adap;
    struct i2c_msg msgs[2];

    if(!i2c)
		return ret;

	if (count != 1)
		return -EIO;  
  
    adap = i2c->adapter;		
    
    msgs[0].addr = i2c->addr;
    msgs[0].buf = &reg;
    msgs[0].flags = 0;
    msgs[0].len = 1;
    msgs[0].scl_rate = 200*1000;
    
    msgs[1].buf = dest;
    msgs[1].addr = i2c->addr;
    msgs[1].flags =  I2C_M_RD;
    msgs[1].len = count;
    msgs[1].scl_rate = RK818_I2C_ADDR_RATE;

    ret = i2c_transfer(adap, msgs, 2);

	DBG("***run in %s %x  % x\n",__FUNCTION__,i2c->addr,msgs[0].buf);
    return 0;
}

int rk818_i2c_write(struct rk818 *rk818, char reg, int count,  const u8 src)
{
	int ret=-1;
	struct i2c_client *i2c = rk818->i2c;
	struct i2c_adapter *adap;
	struct i2c_msg msg;
	char tx_buf[2];

	if(!i2c)
		return ret;
	if (count != 1)
		return -EIO;
    
	adap = i2c->adapter;		
	tx_buf[0] = reg;
	tx_buf[1] = src;
	
	msg.addr = i2c->addr;
	msg.buf = &tx_buf[0];
	msg.len = 1 +1;
	msg.flags = i2c->flags;   
	msg.scl_rate = RK818_I2C_ADDR_RATE;	

	ret = i2c_transfer(adap, &msg, 1);
	return ret;	
}

u8 rk818_reg_read(struct rk818 *rk818, u8 reg)
{
	u8 val = 0;

	mutex_lock(&rk818->io_lock);

	rk818_i2c_read(rk818, reg, 1, &val);

	DBG("reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)val&0xff);

	mutex_unlock(&rk818->io_lock);

	return val & 0xff;	
}
EXPORT_SYMBOL_GPL(rk818_reg_read);

int rk818_reg_write(struct rk818 *rk818, u8 reg, u8 val)
{
	int err =0;

	mutex_lock(&rk818->io_lock);

	err = rk818_i2c_write(rk818, reg, 1,val);
	if (err < 0)
		dev_err(rk818->dev, "Write for reg 0x%x failed\n", reg);

	mutex_unlock(&rk818->io_lock);
	return err;
}
EXPORT_SYMBOL_GPL(rk818_reg_write);

 int rk818_set_bits(struct rk818 *rk818, u8 reg, u8 mask, u8 val)
{
	u8 tmp;
	int ret;

	mutex_lock(&rk818->io_lock);

	ret = rk818_i2c_read(rk818, reg, 1, &tmp);
	DBG("1 reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)tmp&0xff);
	tmp = (tmp & ~mask) | val;
	if (ret == 0) {
		ret = rk818_i2c_write(rk818, reg, 1, tmp);
		DBG("reg write 0x%02x -> 0x%02x\n", (int)reg, (unsigned)val&0xff);
	}
	rk818_i2c_read(rk818, reg, 1, &tmp);
	DBG("2 reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)tmp&0xff);
	mutex_unlock(&rk818->io_lock);

	return 0;//ret;	
}
EXPORT_SYMBOL_GPL(rk818_set_bits);

int rk818_clear_bits(struct rk818 *rk818, u8 reg, u8 mask)
{
	u8 data;
	int err;

	mutex_lock(&rk818->io_lock);
	err = rk818_i2c_read(rk818, reg, 1, &data);
	if (err <0) {
		dev_err(rk818->dev, "read from reg %x failed\n", reg);
		goto out;
	}

	data &= ~mask;
	err = rk818_i2c_write(rk818, reg, 1, data);
	if (err <0)
		dev_err(rk818->dev, "write to reg %x failed\n", reg);

out:
	mutex_unlock(&rk818->io_lock);
	return err;
}
EXPORT_SYMBOL_GPL(rk818_clear_bits);

#if 1
static ssize_t rk818_test_store(struct kobject *kobj, struct kobj_attribute *attr,
                                const char *buf, size_t n)
{
    u32 getdata[8];
    u8 regAddr;
    u8 data;
    char cmd;
    const char *buftmp = buf;
    struct rk818 *rk818 = g_rk818;
    /**
     * W Addr(8Bit) regAddr(8Bit) data0(8Bit) data1(8Bit) data2(8Bit) data3(8Bit)
     * 		:data can be less than 4 byte
     * R regAddr(8Bit)
     * C gpio_name(poweron/powerhold/sleep/boot0/boot1) value(H/L)
     */
	sscanf(buftmp, "%c ", &cmd);
	printk("------zhangqing: get cmd = %c\n", cmd);
	switch (cmd) {
	case 'w':
		sscanf(buftmp, "%c %x %x ", &cmd, &getdata[0], &getdata[1]);
		regAddr = (u8)(getdata[0] & 0xff);
		data = (u8)(getdata[1] & 0xff);
		printk("get value = %x\n", data);

		rk818_i2c_write(rk818, regAddr, 1, data);
		rk818_i2c_read(rk818, regAddr, 1, &data);
		printk("%x   %x\n", getdata[1], data);
		break;
	case 'r':
		sscanf(buftmp, "%c %x ", &cmd, &getdata[0]);
		printk("CMD : %c %x\n", cmd, getdata[0]);

		regAddr = (u8)(getdata[0] & 0xff);
		rk818_i2c_read(rk818, regAddr, 1, &data);
		printk("%x %x\n", getdata[0], data);
		break;
	default:
		printk("Unknown command\n");
		break;
	}
	return n;

}
static ssize_t rk818_test_show(struct kobject *kobj, struct kobj_attribute *attr,
                               char *buf)
{
   char *s = buf;
    buf = "hello";
    return sprintf(s, "%s\n", buf);

}

static struct kobject *rk818_kobj;
struct rk818_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct kobj_attribute *attr,
			const char *buf, size_t n);
};

static struct rk818_attribute rk818_attrs[] = {
	/*     node_name	permision		show_func	store_func */
	__ATTR(rk818_test,	S_IRUGO | S_IWUSR,	rk818_test_show,	rk818_test_store),
};
#endif

#if 0
static irqreturn_t rk818_vbat_lo_irq(int irq, void *data)
{
        printk("rk818 vbat low %s:irq=%d\n",__func__,irq);
	rk818_set_bits(g_rk818,0x4c,(0x1 << 1),(0x1 <<1));
	rk_send_wakeup_key();
        return IRQ_HANDLED;
}
#endif

#ifdef CONFIG_OF
static struct of_device_id rk818_of_match[] = {
	{ .compatible = "rockchip,rk818"},
	{ },
};
MODULE_DEVICE_TABLE(of, rk818_of_match);
#endif

#ifdef CONFIG_OF
static struct of_regulator_match rk818_reg_matches[] = {
	{ .name = "rk818_dcdc1", .driver_data = (void *)0 },
	{ .name = "rk818_dcdc2", .driver_data = (void *)1 },
	{ .name = "rk818_dcdc3", .driver_data = (void *)2 },
	{ .name = "rk818_dcdc4", .driver_data = (void *)3 },
	{ .name = "rk818_ldo1", .driver_data = (void *)4 },
	{ .name = "rk818_ldo2", .driver_data = (void *)5 },
	{ .name = "rk818_ldo3", .driver_data = (void *)6 },
	{ .name = "rk818_ldo4", .driver_data = (void *)7 },
	{ .name = "rk818_ldo5", .driver_data = (void *)8 },
	{ .name = "rk818_ldo6", .driver_data = (void *)9 },
	{ .name = "rk818_ldo7", .driver_data = (void *)10 },
	{ .name = "rk818_ldo8", .driver_data = (void *)11 },
	{ .name = "rk818_ldo9", .driver_data = (void *)12 },
	{ .name = "rk818_ldo10", .driver_data = (void *)13 },
};

static struct rk818_board *rk818_parse_dt(struct rk818 *rk818)
{
	struct rk818_board *pdata;
	struct device_node *regs,*rk818_pmic_np;
	int i, count;

	rk818_pmic_np = of_node_get(rk818->dev->of_node);
	if (!rk818_pmic_np) {
		printk("could not find pmic sub-node\n");
		return NULL;
	}

	regs = of_find_node_by_name(rk818_pmic_np, "regulators");
	if (!regs)
		return NULL;

	count = of_regulator_match(rk818->dev, regs, rk818_reg_matches,
				   rk818_NUM_REGULATORS);
	of_node_put(regs);
	if ((count < 0) || (count > rk818_NUM_REGULATORS))
		return NULL;

	pdata = devm_kzalloc(rk818->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	for (i = 0; i < count; i++) {
		if (!rk818_reg_matches[i].init_data || !rk818_reg_matches[i].of_node)
			continue;

		pdata->rk818_init_data[i] = rk818_reg_matches[i].init_data;
		pdata->of_node[i] = rk818_reg_matches[i].of_node;
	}
	pdata->irq = rk818->chip_irq;
	pdata->irq_base = -1;
	
	pdata->irq_gpio = of_get_named_gpio(rk818_pmic_np,"gpios",0);
		if (!gpio_is_valid(pdata->irq_gpio)) {
			printk("invalid gpio: %d\n",  pdata->irq_gpio);
			return NULL;
		}

	pdata->pmic_sleep_gpio = of_get_named_gpio(rk818_pmic_np,"gpios",1);
			if (!gpio_is_valid(pdata->pmic_sleep_gpio)) {
				printk("invalid gpio: %d\n",  pdata->pmic_sleep_gpio);
		}
	pdata->pmic_sleep = true;
	pdata->pm_off = of_property_read_bool(rk818_pmic_np,"rk818,system-power-controller");
		
	return pdata;
}

#else
static struct rk818_board *rk818_parse_dt(struct i2c_client *i2c)
{
	return NULL;
}
#endif

static void rk818_shutdown(void)
{
	int ret;
	struct rk818 *rk818 = g_rk818;

	pr_info("%s\n", __func__);
	ret = rk818_set_bits(rk818, RK818_INT_STS_MSK_REG1,(0x3<<5),(0x3<<5)); //close rtc int when power off
	ret = rk818_clear_bits(rk818, RK818_RTC_INT_REG,(0x3<<2)); //close rtc int when power off
	/*disable otg_en*/
	ret = rk818_clear_bits(rk818, RK818_DCDC_EN_REG, (0x1<<7));

	mutex_lock(&rk818->io_lock);
	mdelay(100);
}

static struct syscore_ops rk818_syscore_ops = {
	.shutdown = rk818_shutdown,
};

void rk818_device_shutdown(void)
{
	int ret, i;
	u8 reg = 0;
	struct rk818 *rk818 = g_rk818;

	for (i = 0; i < 10; i++) {
		pr_info("%s\n", __func__);
		ret = rk818_i2c_read(rk818, RK818_DEVCTRL_REG, 1, &reg);
		if (ret < 0)
			continue;
		ret = rk818_i2c_write(rk818, RK818_DEVCTRL_REG, 1,
				     (reg | (0x1 << 0)));
		if (ret < 0) {
			pr_err("rk818 power off error!\n");
			continue;
		}
	}
	while(1) wfi();
}
EXPORT_SYMBOL_GPL(rk818_device_shutdown);

__weak void  rk818_device_suspend(void) {}
__weak void  rk818_device_resume(void) {}
#ifdef CONFIG_PM
static int rk818_suspend(struct i2c_client *i2c, pm_message_t mesg)
{
	int ret, val;
	struct rk818 *rk818 = g_rk818;

	rk818_device_suspend();
	/************set vbat low 3v4 to irq**********/
	val = rk818_reg_read(rk818, RK818_VB_MON_REG);
	val &= (~(VBAT_LOW_VOL_MASK | VBAT_LOW_ACT_MASK));
	val |= (RK818_VBAT_LOW_3V4 | EN_VBAT_LOW_IRQ);
	ret = rk818_reg_write(rk818, RK818_VB_MON_REG, val);
	if (ret < 0) {
		pr_err("Unable to write RK818_VB_MON_REG reg\n");
		return ret;
	}
	/*enable low irq*/
	rk818_set_bits(rk818, 0x4d, (0x1 << 1), (0x0 << 1));
	return 0;
}

static int rk818_resume(struct i2c_client *i2c)
{
	int ret, val;
	struct rk818 *rk818 = g_rk818;

	rk818_device_resume();
	/********set vbat low 3v0 to shutdown**********/
	val = rk818_reg_read(rk818, RK818_VB_MON_REG);
	val &= (~(VBAT_LOW_VOL_MASK | VBAT_LOW_ACT_MASK));
	val |= (RK818_VBAT_LOW_3V0 | EN_VABT_LOW_SHUT_DOWN);
	ret = rk818_reg_write(rk818, RK818_VB_MON_REG, val);
	if (ret < 0) {
		pr_err("Unable to write RK818_VB_MON_REG reg\n");
		return ret;
	}
	/*disable low irq*/
	rk818_set_bits(rk818, 0x4d, (0x1 << 1), (0x1 << 1));

	return 0;
}
#else
static int rk818_suspend(struct i2c_client *i2c, pm_message_t mesg)
{		
	return 0;
}

static int rk818_resume(struct i2c_client *i2c)
{
	return 0;
}
#endif

static int rk818_pre_init(struct rk818 *rk818)
{
	int ret,val;
	printk("%s,line=%d\n", __func__,__LINE__);

	ret = rk818_set_bits(rk818, 0xa1, (0xF<<0),(0x7));
	ret = rk818_set_bits(rk818, 0xa1,(0x7<<4),(0x7<<4)); //close charger when usb low then 3.4V
	ret = rk818_set_bits(rk818, 0x52,(0x1<<1),(0x1<<1)); //no action when vref
	ret = rk818_set_bits(rk818, 0x52,(0x1<<0),(0x1<<0)); //enable HDMI 5V

	/*******enable switch and boost***********/
	val = rk818_reg_read(rk818,RK818_DCDC_EN_REG);
	val |= (0x3 << 5);    //enable switch1/2
	val |= (0x1 << 4);    //enable boost
	ret = rk818_reg_write(rk818,RK818_DCDC_EN_REG,val);
	if (ret <0) {
		printk(KERN_ERR "Unable to write RK818_DCDC_EN_REG reg\n");
		return ret;
	}
	/****************************************/
	/****************set vbat low **********/
	val = rk818_reg_read(rk818,RK818_VB_MON_REG);
	val &=(~(VBAT_LOW_VOL_MASK | VBAT_LOW_ACT_MASK));
	val |= (RK818_VBAT_LOW_3V0 | EN_VABT_LOW_SHUT_DOWN);
	ret = rk818_reg_write(rk818,RK818_VB_MON_REG,val);
	if (ret <0) {
		printk(KERN_ERR "Unable to write RK818_VB_MON_REG reg\n");
		return ret;
	}
	/**************************************/

	/**********mask int****************/

	val = rk818_reg_read(rk818,RK818_INT_STS_MSK_REG1);
	val |= (0x1<<0); //mask vout_lo_int
	ret = rk818_reg_write(rk818,RK818_INT_STS_MSK_REG1,val);
	if (ret <0) {
		printk(KERN_ERR "Unable to write RK818_INT_STS_MSK_REG1 reg\n");
		return ret;
	}

	/**********************************/
	/**********enable clkout2****************/
	ret = rk818_reg_write(rk818,RK818_CLK32OUT_REG,0x01);
	if (ret <0) {
		printk(KERN_ERR "Unable to write RK818_CLK32OUT_REG reg\n");
		return ret;
	}
	/**********************************/
	ret = rk818_clear_bits(rk818, RK818_INT_STS_MSK_REG1,(0x3<<5)); //open rtc int when power on
	ret = rk818_set_bits(rk818, RK818_RTC_INT_REG,(0x1<<3),(0x1<<3)); //open rtc int when power on

	/*****disable otg when in sleep mode****/
	val = rk818_reg_read(rk818, RK818_SLEEP_SET_OFF_REG1);
	val |= (0x1 << 7);
	ret =  rk818_reg_write(rk818, RK818_SLEEP_SET_OFF_REG1, val);
	if (ret < 0) {
		pr_err("Unable to write RK818_SLEEP_SET_OFF_REG1 reg\n");
		return ret;
	}

	/*************** improve efficiency **********************/
	ret =  rk818_reg_write(rk818, RK818_BUCK2_CONFIG_REG, 0x1c);
	if (ret < 0) {
		pr_err("Unable to write RK818_BUCK2_CONFIG_REG reg\n");
		return ret;
	}

	ret =  rk818_reg_write(rk818, RK818_BUCK4_CONFIG_REG, 0x04);
	if (ret < 0) {
		pr_err("Unable to write RK818_BUCK4_CONFIG_REG reg\n");
		return ret;
	}

	return 0;
}
 
static int  rk818_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct rk818 *rk818;	
	struct rk818_board *pdev;
	const struct of_device_id *match;
	struct regulator_config config = { };
	struct regulator_dev *rk818_rdev;
	struct regulator_init_data *reg_data;
	const char *rail_name = NULL;
	int ret, i = 0;
	
	printk("%s,line=%d\n", __func__,__LINE__);

	if (i2c->dev.of_node) {
		match = of_match_device(rk818_of_match, &i2c->dev);
		if (!match) {
			dev_err(&i2c->dev,"Failed to find matching dt id\n");
			return -EINVAL;
		}
	}

	rk818 = devm_kzalloc(&i2c->dev,sizeof(struct rk818), GFP_KERNEL);
	if (rk818 == NULL) {
		ret = -ENOMEM;		
		goto err;
	}
	rk818->i2c = i2c;
	rk818->dev = &i2c->dev;
	i2c_set_clientdata(i2c, rk818);
	
	mutex_init(&rk818->io_lock);	

	ret = rk818_reg_read(rk818,0x2f);
	if ((ret < 0) || (ret == 0xff)){
		printk("The device is not rk818 %d\n",ret);
		goto err;
	}

	ret = rk818_pre_init(rk818);
	if (ret < 0){
		printk("The rk818_pre_init failed %d\n",ret);
		goto err;
	}

	if (rk818->dev->of_node)
		pdev = rk818_parse_dt(rk818);
	
	/******************************set sleep vol & dcdc mode******************/
	#ifdef CONFIG_OF
	rk818->pmic_sleep_gpio = pdev->pmic_sleep_gpio;
	if (rk818->pmic_sleep_gpio) {
			ret = gpio_request(rk818->pmic_sleep_gpio, "rk818_pmic_sleep");
			if (ret < 0) {
				dev_err(rk818->dev,"Failed to request gpio %d with ret:""%d\n",	rk818->pmic_sleep_gpio, ret);
				return IRQ_NONE;
			}
			gpio_direction_output(rk818->pmic_sleep_gpio,0);
			ret = gpio_get_value(rk818->pmic_sleep_gpio);
			gpio_free(rk818->pmic_sleep_gpio);
			pr_info("%s: rk818_pmic_sleep=%x\n", __func__, ret);
	}	
	#endif
	/**********************************************************/
	
	if (pdev) {
		rk818->num_regulators = rk818_NUM_REGULATORS;
		rk818->rdev = kcalloc(rk818_NUM_REGULATORS,sizeof(struct regulator_dev *), GFP_KERNEL);
		if (!rk818->rdev) {
			return -ENOMEM;
		}
		/* Instantiate the regulators */
		for (i = 0; i < rk818_NUM_REGULATORS; i++) {
		reg_data = pdev->rk818_init_data[i];
		if (!reg_data)
			continue;
		config.dev = rk818->dev;
		config.driver_data = rk818;
		if (rk818->dev->of_node)
			config.of_node = pdev->of_node[i];
		if (reg_data && reg_data->constraints.name)
				rail_name = reg_data->constraints.name;
			else
				rail_name = regulators[i].name;
			reg_data->supply_regulator = rail_name;
	
		config.init_data =reg_data;

		rk818_rdev = regulator_register(&regulators[i],&config);
		if (IS_ERR(rk818_rdev)) {
			printk("failed to register %d regulator\n",i);
		goto err;
		}
		rk818->rdev[i] = rk818_rdev;
		}
	}

	rk818->irq_gpio = pdev->irq_gpio;
	ret = rk818_irq_init(rk818, rk818->irq_gpio, pdev);
	if (ret < 0)
		goto err;

	ret = mfd_add_devices(rk818->dev, -1,
			      rk818s, ARRAY_SIZE(rk818s),
			      NULL, 0,NULL);
	#if 0	
	/********************vbat low int**************/
	vlow_irq = irq_create_mapping(rk818->irq_domain, RK818_IRQ_VB_LO);
	 ret = devm_request_threaded_irq(&i2c->dev,vlow_irq, NULL, rk818_vbat_lo_irq,
                                   IRQF_ONESHOT, "rk818_vbatlow",
                                   rk818);
        if (ret != 0) {
                dev_err(rk818->dev, "Failed to request periodic IRQ %d: %d\n",
                        vlow_irq+ RK818_IRQ_VB_LO, ret);

        }
	#endif
	/*********************************************/
	
	g_rk818 = rk818;
	if (pdev->pm_off && !pm_power_off) {
		pm_power_off = rk818_device_shutdown;
	}

	#if 1
	rk818_kobj = kobject_create_and_add("rk818", NULL);
	if (!rk818_kobj)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(rk818_attrs); i++) {
		ret = sysfs_create_file(rk818_kobj, &rk818_attrs[i].attr);
		if (ret != 0) {
			printk("create index %d error\n", i);
			return ret;
		}
	}
	#endif
	
	register_syscore_ops(&rk818_syscore_ops);

	return 0;

err:
	mfd_remove_devices(rk818->dev);
	return ret;	

}

static int  rk818_i2c_remove(struct i2c_client *i2c)
{
	struct rk818 *rk818 = i2c_get_clientdata(i2c);
	int i;

	unregister_syscore_ops(&rk818_syscore_ops);
	for (i = 0; i < rk818->num_regulators; i++)
		if (rk818->rdev[i])
			regulator_unregister(rk818->rdev[i]);
	i2c_set_clientdata(i2c, NULL);

	return 0;
}

static const struct i2c_device_id rk818_i2c_id[] = {
       { "rk818", 0 },
       { }
};

MODULE_DEVICE_TABLE(i2c, rk818_i2c_id);

static struct i2c_driver rk818_i2c_driver = {
	.driver = {
		.name = "rk818",
		.owner = THIS_MODULE,
		.of_match_table =of_match_ptr(rk818_of_match),
	},
	.probe    = rk818_i2c_probe,
	.remove   = rk818_i2c_remove,
	.id_table = rk818_i2c_id,
	#ifdef CONFIG_PM
	.suspend	= rk818_suspend,
	.resume		= rk818_resume,
	#endif
};

static int __init rk818_module_init(void)
{
	int ret;
	ret = i2c_add_driver(&rk818_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register I2C driver: %d\n", ret);
	return ret;
}

subsys_initcall_sync(rk818_module_init);

static void __exit rk818_module_exit(void)
{
	i2c_del_driver(&rk818_i2c_driver);
}
module_exit(rk818_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhangqing <zhangqing@rock-chips.com>");
MODULE_DESCRIPTION("rk818 PMIC driver");

