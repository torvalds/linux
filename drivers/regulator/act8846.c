/*
 * Regulator driver for Active-semi act8846 PMIC chip for rk29xx
 *
 * Copyright (C) 2010, 2011 ROCKCHIP, Inc.

 * Based on act8846.c that is work by zhangqing<zhangqing@rock-chips.com>
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
#include <linux/regulator/act8846.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
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
#include <asm/system_misc.h>

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

struct act8846 {
	struct device *dev;
	struct mutex io_lock;
	struct i2c_client *i2c;
	int num_regulators;
	struct regulator_dev **rdev;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend act8846_suspend;
#endif
	int irq_base;
	int chip_irq;
	int pmic_sleep_gpio; /* */
	int pmic_hold_gpio; /* */
	unsigned int dcdc_slp_voltage[3]; /* buckx_voltage in uV */
	bool pmic_sleep;
	struct regmap *regmap;
};

struct act8846_regulator {
	struct device		*dev;
	struct regulator_desc	*desc;
	struct regulator_dev	*rdev;
};


struct act8846 *g_act8846;

static u8 act8846_reg_read(struct act8846 *act8846, u8 reg);
static int act8846_set_bits(struct act8846 *act8846, u8 reg, u16 mask, u16 val);


#define act8846_BUCK1_SET_VOL_BASE 0x10
#define act8846_BUCK2_SET_VOL_BASE 0x20
#define act8846_BUCK3_SET_VOL_BASE 0x30
#define act8846_BUCK4_SET_VOL_BASE 0x40

#define act8846_BUCK2_SLP_VOL_BASE 0x21
#define act8846_BUCK3_SLP_VOL_BASE 0x31
#define act8846_BUCK4_SLP_VOL_BASE 0x41

#define act8846_LDO1_SET_VOL_BASE 0x50
#define act8846_LDO2_SET_VOL_BASE 0x58
#define act8846_LDO3_SET_VOL_BASE 0x60
#define act8846_LDO4_SET_VOL_BASE 0x68
#define act8846_LDO5_SET_VOL_BASE 0x70
#define act8846_LDO6_SET_VOL_BASE 0x80
#define act8846_LDO7_SET_VOL_BASE 0x90
#define act8846_LDO8_SET_VOL_BASE 0xa0
//#define act8846_LDO9_SET_VOL_BASE 0xb1

#define act8846_BUCK1_CONTR_BASE 0x12
#define act8846_BUCK2_CONTR_BASE 0x22
#define act8846_BUCK3_CONTR_BASE 0x32
#define act8846_BUCK4_CONTR_BASE 0x42

#define act8846_LDO1_CONTR_BASE 0x51
#define act8846_LDO2_CONTR_BASE 0x59
#define act8846_LDO3_CONTR_BASE 0x61
#define act8846_LDO4_CONTR_BASE 0x69
#define act8846_LDO5_CONTR_BASE 0x71
#define act8846_LDO6_CONTR_BASE 0x81
#define act8846_LDO7_CONTR_BASE 0x91
#define act8846_LDO8_CONTR_BASE 0xa1
//#define act8846_LDO9_CONTR_BASE 0xb1

#define BUCK_VOL_MASK 0x3f
#define LDO_VOL_MASK 0x3f

#define VOL_MIN_IDX 0x00
#define VOL_MAX_IDX 0x3f

const static int buck_set_vol_base_addr[] = {
	act8846_BUCK1_SET_VOL_BASE,
	act8846_BUCK2_SET_VOL_BASE,
	act8846_BUCK3_SET_VOL_BASE,
	act8846_BUCK4_SET_VOL_BASE,
};
const static int buck_contr_base_addr[] = {
	act8846_BUCK1_CONTR_BASE,
 	act8846_BUCK2_CONTR_BASE,
 	act8846_BUCK3_CONTR_BASE,
 	act8846_BUCK4_CONTR_BASE,
};
#define act8846_BUCK_SET_VOL_REG(x) (buck_set_vol_base_addr[x])
#define act8846_BUCK_CONTR_REG(x) (buck_contr_base_addr[x])


const static int ldo_set_vol_base_addr[] = {
	act8846_LDO1_SET_VOL_BASE,
	act8846_LDO2_SET_VOL_BASE,
	act8846_LDO3_SET_VOL_BASE,
	act8846_LDO4_SET_VOL_BASE, 
	act8846_LDO5_SET_VOL_BASE, 
	act8846_LDO6_SET_VOL_BASE, 
	act8846_LDO7_SET_VOL_BASE, 
	act8846_LDO8_SET_VOL_BASE, 
//	act8846_LDO9_SET_VOL_BASE, 
};
const static int ldo_contr_base_addr[] = {
	act8846_LDO1_CONTR_BASE,
	act8846_LDO2_CONTR_BASE,
	act8846_LDO3_CONTR_BASE,
	act8846_LDO4_CONTR_BASE,
	act8846_LDO5_CONTR_BASE,
	act8846_LDO6_CONTR_BASE,
	act8846_LDO7_CONTR_BASE,
	act8846_LDO8_CONTR_BASE,
//	act8846_LDO9_CONTR_BASE,
};
#define act8846_LDO_SET_VOL_REG(x) (ldo_set_vol_base_addr[x])
#define act8846_LDO_CONTR_REG(x) (ldo_contr_base_addr[x])

const static int buck_voltage_map[] = {
	 600, 625, 650, 675, 700, 725, 750, 775,
	 800, 825, 850, 875, 900, 925, 950, 975,
	 1000, 1025, 1050, 1075, 1100, 1125, 1150,
	 1175, 1200, 1250, 1300, 1350, 1400, 1450,
	 1500, 1550, 1600, 1650, 1700, 1750, 1800, 
	 1850, 1900, 1950, 2000, 2050, 2100, 2150, 
	 2200, 2250, 2300, 2350, 2400, 2500, 2600, 
	 2700, 2800, 2900, 3000, 3100, 3200,
	 3300, 3400, 3500, 3600, 3700, 3800, 3900,
};

const static int ldo_voltage_map[] = {
	 600, 625, 650, 675, 700, 725, 750, 775,
	 800, 825, 850, 875, 900, 925, 950, 975,
	 1000, 1025, 1050, 1075, 1100, 1125, 1150,
	 1175, 1200, 1250, 1300, 1350, 1400, 1450,
	 1500, 1550, 1600, 1650, 1700, 1750, 1800, 
	 1850, 1900, 1950, 2000, 2050, 2100, 2150, 
	 2200, 2250, 2300, 2350, 2400, 2500, 2600, 
	 2700, 2800, 2900, 3000, 3100, 3200,
	 3300, 3400, 3500, 3600, 3700, 3800, 3900,
};

static int act8846_ldo_list_voltage(struct regulator_dev *dev, unsigned index)
{
	if (index >= ARRAY_SIZE(ldo_voltage_map))
		return -EINVAL;
	return 1000 * ldo_voltage_map[index];
}
static int act8846_ldo_is_enabled(struct regulator_dev *dev)
{
	struct act8846 *act8846 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - ACT8846_LDO1;
	u16 val;
	u16 mask=0x80;
	val = act8846_reg_read(act8846, act8846_LDO_CONTR_REG(ldo));	 
	if (val < 0)
		return val;
	val=val&~0x7f;
	if (val & mask)
		return 1;
	else
		return 0; 	
}
static int act8846_ldo_enable(struct regulator_dev *dev)
{
	struct act8846 *act8846 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - ACT8846_LDO1;
	u16 mask=0x80;	

	return act8846_set_bits(act8846, act8846_LDO_CONTR_REG(ldo), mask, 0x80);
	
}
static int act8846_ldo_disable(struct regulator_dev *dev)
{
	struct act8846 *act8846 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - ACT8846_LDO1;
	u16 mask=0x80;
	
	return act8846_set_bits(act8846, act8846_LDO_CONTR_REG(ldo), mask, 0);

}
static int act8846_ldo_get_voltage(struct regulator_dev *dev)
{
	struct act8846 *act8846 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - ACT8846_LDO1;
	u16 reg = 0;
	int val;
	reg = act8846_reg_read(act8846,act8846_LDO_SET_VOL_REG(ldo));
	reg &= LDO_VOL_MASK;
	val = 1000 * ldo_voltage_map[reg];	
	return val;
}
static int act8846_ldo_set_voltage(struct regulator_dev *dev,
				  int min_uV, int max_uV,unsigned *selector)
{
	struct act8846 *act8846 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - ACT8846_LDO1;
	int min_vol = min_uV / 1000, max_vol = max_uV / 1000;
	const int *vol_map =ldo_voltage_map;
	u16 val;
	int ret = 0;

	if (min_vol < vol_map[VOL_MIN_IDX] ||
	    min_vol > vol_map[VOL_MAX_IDX])
		return -EINVAL;

	for (val = VOL_MIN_IDX; val <= VOL_MAX_IDX; val++){
		if (vol_map[val] >= min_vol)
			break;	
        }
		
	if (vol_map[val] > max_vol)
		return -EINVAL;

	ret = act8846_set_bits(act8846, act8846_LDO_SET_VOL_REG(ldo),
	       	LDO_VOL_MASK, val);
	return ret;

}
static unsigned int act8846_ldo_get_mode(struct regulator_dev *dev)
{
	struct act8846 *act8846 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - ACT8846_LDO1;
	u16 mask = 0x80;
	u16 val;
	val = act8846_reg_read(act8846, act8846_LDO_CONTR_REG(ldo));
        if (val < 0) {
                return val;
        }
	val=val & mask;
	if (val== mask)
		return REGULATOR_MODE_NORMAL;
	else
		return REGULATOR_MODE_STANDBY;

}
static int act8846_ldo_set_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct act8846 *act8846 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - ACT8846_LDO1;
	u16 mask = 0x80;
	switch(mode)
	{
	case REGULATOR_MODE_NORMAL:
		return act8846_set_bits(act8846, act8846_LDO_CONTR_REG(ldo), mask, mask);		
	case REGULATOR_MODE_STANDBY:
		return act8846_set_bits(act8846, act8846_LDO_CONTR_REG(ldo), mask, 0);
	default:
		printk("error:pmu_act8846 only lowpower and nomal mode\n");
		return -EINVAL;
	}


}
static struct regulator_ops act8846_ldo_ops = {
	.set_voltage = act8846_ldo_set_voltage,
	.get_voltage = act8846_ldo_get_voltage,
	.list_voltage = act8846_ldo_list_voltage,
	.is_enabled = act8846_ldo_is_enabled,
	.enable = act8846_ldo_enable,
	.disable = act8846_ldo_disable,
	.get_mode = act8846_ldo_get_mode,
	.set_mode = act8846_ldo_set_mode,
	
};

static int act8846_dcdc_list_voltage(struct regulator_dev *dev, unsigned index)
{
	if (index >= ARRAY_SIZE(buck_voltage_map))
		return -EINVAL;
	return 1000 * buck_voltage_map[index];
}
static int act8846_dcdc_is_enabled(struct regulator_dev *dev)
{
	struct act8846 *act8846 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - ACT8846_DCDC1;
	u16 val;
	u16 mask=0x80;	
	val = act8846_reg_read(act8846, act8846_BUCK_CONTR_REG(buck));
	if (val < 0)
		return val;
	 val=val&~0x7f;
	if (val & mask)
		return 1;
	else
		return 0; 	
}
static int act8846_dcdc_enable(struct regulator_dev *dev)
{
	struct act8846 *act8846 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - ACT8846_DCDC1;
	u16 mask=0x80;	
	
	return act8846_set_bits(act8846, act8846_BUCK_CONTR_REG(buck), mask, 0x80);

}
static int act8846_dcdc_disable(struct regulator_dev *dev)
{
	struct act8846 *act8846 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - ACT8846_DCDC1;
	u16 mask=0x80;
	 return act8846_set_bits(act8846, act8846_BUCK_CONTR_REG(buck), mask, 0);
}
static int act8846_dcdc_get_voltage(struct regulator_dev *dev)
{
	struct act8846 *act8846 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - ACT8846_DCDC1;
	u16 reg = 0;
	int val;
	#ifdef CONFIG_ACT8846_SUPPORT_RESET
	reg = act8846_reg_read(act8846,(act8846_BUCK_SET_VOL_REG(buck)+0x1));
	#else
	reg = act8846_reg_read(act8846,act8846_BUCK_SET_VOL_REG(buck));
	#endif
	reg &= BUCK_VOL_MASK;
	val = 1000 * buck_voltage_map[reg];	
	return val;
}
static int act8846_dcdc_set_voltage(struct regulator_dev *dev,
				  int min_uV, int max_uV,unsigned *selector)
{
	struct act8846 *act8846 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - ACT8846_DCDC1;
	int min_vol = min_uV / 1000, max_vol = max_uV / 1000;
	const int *vol_map = buck_voltage_map;
	u16 val;
	int ret = 0;

	if (min_vol < vol_map[VOL_MIN_IDX] ||
	    min_vol > vol_map[VOL_MAX_IDX])
		return -EINVAL;

	for (val = VOL_MIN_IDX; val <= VOL_MAX_IDX; val++){
		if (vol_map[val] >= min_vol)
			break;
        }

	if (vol_map[val] > max_vol)
		printk("WARNING:this voltage is not support!voltage set is %d mv\n",vol_map[val]);

	#ifdef CONFIG_ACT8846_SUPPORT_RESET
	ret = act8846_set_bits(act8846, (act8846_BUCK_SET_VOL_REG(buck) +0x1),BUCK_VOL_MASK, val);
	#else
	ret = act8846_set_bits(act8846, act8846_BUCK_SET_VOL_REG(buck) ,BUCK_VOL_MASK, val);
	#endif

	if(ret < 0)
		printk("##################:set voltage error!voltage set is %d mv\n",vol_map[val]);
	
	return ret;
}
static int act8846_dcdc_set_sleep_voltage(struct regulator_dev *dev,
					    int uV)
{
	struct act8846 *act8846 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - ACT8846_DCDC1;
	int min_vol = uV / 1000,max_vol = uV / 1000;
	const int *vol_map = buck_voltage_map;
	u16 val;
	int ret = 0;

	if (min_vol < vol_map[VOL_MIN_IDX] ||
	    min_vol > vol_map[VOL_MAX_IDX])
		return -EINVAL;

	for (val = VOL_MIN_IDX; val <= VOL_MAX_IDX; val++){
		if (vol_map[val] >= min_vol)
			break;
        }

	if (vol_map[val] > max_vol)
		printk("WARNING:this voltage is not support!voltage set is %d mv\n",vol_map[val]);
	#ifdef CONFIG_ACT8846_SUPPORT_RESET
	 ret = act8846_set_bits(act8846, (act8846_BUCK_SET_VOL_REG(buck) ),BUCK_VOL_MASK, val);
	#else
	ret = act8846_set_bits(act8846, (act8846_BUCK_SET_VOL_REG(buck) +0x01),BUCK_VOL_MASK, val);
	#endif
	
	return ret;
}
static unsigned int act8846_dcdc_get_mode(struct regulator_dev *dev)
{
	struct act8846 *act8846 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - ACT8846_DCDC1;
	u16 mask = 0x08;
	u16 val;
	val = act8846_reg_read(act8846, act8846_BUCK_CONTR_REG(buck));
        if (val < 0) {
                return val;
        }
	val=val & mask;
	if (val== mask)
		return REGULATOR_MODE_NORMAL;
	else
		return REGULATOR_MODE_STANDBY;

}
static int act8846_dcdc_set_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct act8846 *act8846 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - ACT8846_DCDC1;
	u16 mask = 0x80;

	switch(mode)
	{
	case REGULATOR_MODE_STANDBY:
		return act8846_set_bits(act8846, act8846_BUCK_CONTR_REG(buck), mask, 0);
	case REGULATOR_MODE_NORMAL:
		return act8846_set_bits(act8846, act8846_BUCK_CONTR_REG(buck), mask, mask);
	default:
		printk("error:pmu_act8846 only powersave and pwm mode\n");
		return -EINVAL;
	}


}
static int act8846_dcdc_set_voltage_time_sel(struct regulator_dev *dev,   unsigned int old_selector,
				     unsigned int new_selector)
{
	int old_volt, new_volt;
	
	old_volt = act8846_dcdc_list_voltage(dev, old_selector);
	if (old_volt < 0)
		return old_volt;
	
	new_volt = act8846_dcdc_list_voltage(dev, new_selector);
	if (new_volt < 0)
		return new_volt;

	return DIV_ROUND_UP(abs(old_volt - new_volt)*2, 25000);
}

static struct regulator_ops act8846_dcdc_ops = { 
	.set_voltage = act8846_dcdc_set_voltage,
	.get_voltage = act8846_dcdc_get_voltage,
	.list_voltage= act8846_dcdc_list_voltage,
	.is_enabled = act8846_dcdc_is_enabled,
	.enable = act8846_dcdc_enable,
	.disable = act8846_dcdc_disable,
	.get_mode = act8846_dcdc_get_mode,
	.set_mode = act8846_dcdc_set_mode,
	.set_suspend_voltage = act8846_dcdc_set_sleep_voltage,
	.set_voltage_time_sel = act8846_dcdc_set_voltage_time_sel,
};
static struct regulator_desc regulators[] = {

        {
		.name = "ACT_DCDC1",
		.id = 0,
		.ops = &act8846_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "ACT_DCDC2",
		.id = 1,
		.ops = &act8846_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "ACT_DCDC3",
		.id = 2,
		.ops = &act8846_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "ACT_DCDC4",
		.id = 3,
		.ops = &act8846_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},

	{
		.name = "ACT_LDO1",
		.id =4,
		.ops = &act8846_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "ACT_LDO2",
		.id = 5,
		.ops = &act8846_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "ACT_LDO3",
		.id = 6,
		.ops = &act8846_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "ACT_LDO4",
		.id = 7,
		.ops = &act8846_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},

	{
		.name = "ACT_LDO5",
		.id =8,
		.ops = &act8846_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "ACT_LDO6",
		.id = 9,
		.ops = &act8846_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "ACT_LDO7",
		.id = 10,
		.ops = &act8846_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "ACT_LDO8",
		.id = 11,
		.ops = &act8846_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "ACT_LDO9",
		.id = 12,
		.ops = &act8846_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
};

/*
 *
 */
static int act8846_i2c_read(struct i2c_client *i2c, char reg, int count,	u16 *dest)
{
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
    msgs[0].flags = i2c->flags;
    msgs[0].len = 1;
    msgs[0].scl_rate = 200*1000;
    
    msgs[1].buf = (u8 *)dest;
    msgs[1].addr = i2c->addr;
    msgs[1].flags = i2c->flags | I2C_M_RD;
    msgs[1].len = 1;
    msgs[1].scl_rate = 200*1000;
    ret = i2c_transfer(adap, msgs, 2);

	DBG("***run in %s %d msgs[1].buf = %d\n",__FUNCTION__,__LINE__,*(msgs[1].buf));

	return ret;   
}

static int act8846_i2c_write(struct i2c_client *i2c, char reg, int count, const u16 src)
{
	int ret=-1;
	
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
	msg.scl_rate = 200*1000;	

	ret = i2c_transfer(adap, &msg, 1);
	return ret;	
}

static u8 act8846_reg_read(struct act8846 *act8846, u8 reg)
{
	u16 val = 0;
	int ret;

	mutex_lock(&act8846->io_lock);

	ret = act8846_i2c_read(act8846->i2c, reg, 1, &val);
	if(ret < 0){
		mutex_unlock(&act8846->io_lock);
		return ret;
	}

	DBG("reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)val&0xff);

	mutex_unlock(&act8846->io_lock);

	return val & 0xff;	
}

static int act8846_set_bits(struct act8846 *act8846, u8 reg, u16 mask, u16 val)
{
	u16 tmp;
	int ret;

	mutex_lock(&act8846->io_lock);

	ret = act8846_i2c_read(act8846->i2c, reg, 1, &tmp);
	if(ret < 0){
		mutex_unlock(&act8846->io_lock);
		return ret;
	}
	DBG("1 reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)tmp&0xff);
	tmp = (tmp & ~mask) | val;
	ret = act8846_i2c_write(act8846->i2c, reg, 1, tmp);
	if(ret < 0){
		mutex_unlock(&act8846->io_lock);
		return ret;
	}
	DBG("reg write 0x%02x -> 0x%02x\n", (int)reg, (unsigned)val&0xff);
	
	ret = act8846_i2c_read(act8846->i2c, reg, 1, &tmp);
	if(ret < 0){
		mutex_unlock(&act8846->io_lock);
		return ret;
	}
	DBG("2 reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)tmp&0xff);
	mutex_unlock(&act8846->io_lock);

	return 0;//ret;	
}

#ifdef CONFIG_OF
static struct of_device_id act8846_of_match[] = {
	{ .compatible = "act,act8846"},
	{ },
};
MODULE_DEVICE_TABLE(of, act8846_of_match);
#endif
#ifdef CONFIG_OF
static struct of_regulator_match act8846_reg_matches[] = {
	{ .name = "act_dcdc1" ,.driver_data = (void *)0},
	{ .name = "act_dcdc2" ,.driver_data = (void *)1},
	{ .name = "act_dcdc3", .driver_data = (void *)2 },
	{ .name = "act_dcdc4", .driver_data = (void *)3 },
	{ .name = "act_ldo1", .driver_data = (void *)4 },
	{ .name = "act_ldo2", .driver_data = (void *)5 },
	{ .name = "act_ldo3", .driver_data = (void *)6 },
	{ .name = "act_ldo4", .driver_data = (void *)7 },
	{ .name = "act_ldo5", .driver_data = (void *)8 },
	{ .name = "act_ldo6", .driver_data = (void *)9 },
	{ .name = "act_ldo7", .driver_data = (void *)10 },
	{ .name = "act_ldo8", .driver_data = (void *)11 },
};

static struct act8846_board *act8846_parse_dt(struct act8846 *act8846)
{
//	struct act8846 *act8846 = i2c->dev.parent;
	struct act8846_board *pdata;
	struct device_node *regs;
	struct device_node *act8846_pmic_np;
	int i, count;
	int gpio;
	printk("%s,line=%d\n", __func__,__LINE__);	
	
	act8846_pmic_np = of_node_get(act8846->dev->of_node);
	if (!act8846_pmic_np) {
		printk("could not find pmic sub-node\n");
		return NULL;
	}
	
	regs = of_find_node_by_name(act8846_pmic_np, "regulators");
	if (!regs)
		return NULL;
	
	count = of_regulator_match(act8846->dev, regs, act8846_reg_matches,act8846_NUM_REGULATORS);
	of_node_put(regs);

	if ((count < 0) || (count > act8846_NUM_REGULATORS))
		return NULL;

	pdata = devm_kzalloc(act8846->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;
	for (i = 0; i < count; i++) {
		if (!act8846_reg_matches[i].init_data || !act8846_reg_matches[i].of_node)
			continue;
		pdata->act8846_init_data[i] = act8846_reg_matches[i].init_data;
		pdata->of_node[i] = act8846_reg_matches[i].of_node;
	}
	pdata->irq = act8846->chip_irq;
	pdata->irq_base = -1;

	gpio = of_get_named_gpio(act8846_pmic_np,"gpios", 0);
		if (!gpio_is_valid(gpio)) 
			printk("invalid gpio: %d\n",gpio);
	pdata->pmic_sleep_gpio = gpio;	
	pdata->pmic_sleep = true;
	
	gpio = of_get_named_gpio(act8846_pmic_np,"gpios", 1);
		if (!gpio_is_valid(gpio)) 
			printk("invalid gpio: %d\n",gpio);
	pdata->pmic_hold_gpio = gpio;	
	pdata->pm_off = of_property_read_bool(act8846_pmic_np,"act8846,system-power-controller");

	return pdata;
}

#else
static struct act8846_board *act8846_parse_dt(struct i2c_client *i2c)
{
	return NULL;
}
#endif


void act8846_device_shutdown(void)
{
	struct act8846 *act8846 = g_act8846;
	
	printk("%s\n",__func__);
#if 1
	if (act8846->pmic_hold_gpio) {
			gpio_direction_output(act8846->pmic_hold_gpio,0);
			mdelay(100);
			arm_pm_restart('h', "charge");
	}
	
#else
	ret = act8846_reg_read(act8846,0xc3);
	ret = act8846_set_bits(act8846, 0xc3,(0x1<<3),(0x1<<3));
	ret = act8846_set_bits(act8846, 0xc3,(0x1<<4),(0x1<<4));
	if (ret < 0) {
		printk("act8846 set 0xc3 error!\n");
		return err;
	}
#endif
}
EXPORT_SYMBOL_GPL(act8846_device_shutdown);

__weak void  act8846_device_suspend(void) {}
__weak void  act8846_device_resume(void) {}
#ifdef CONFIG_PM
static int act8846_suspend(struct device *dev)
{	
	act8846_device_suspend();
	return 0;
}

static int act8846_resume(struct device *dev)
{
	act8846_device_resume();
	return 0;
}
#else
static int act8846_suspend(struct device *dev)
{		
	return 0;
}

static int act8846_resume(struct device *dev)
{
	return 0;
}
#endif


#ifdef CONFIG_HAS_EARLYSUSPEND
__weak void act8846_early_suspend(struct early_suspend *h) {}
__weak void act8846_late_resume(struct early_suspend *h) {}
#endif

static bool is_volatile_reg(struct device *dev, unsigned int reg)
{

	if ((reg >= act8846_BUCK1_SET_VOL_BASE) && (reg <= act8846_LDO8_CONTR_BASE)) {
		return true;
	}
	return true;
}

static const struct regmap_config act8846_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_reg = is_volatile_reg,
	.max_register = act8846_NUM_REGULATORS - 1,
	.cache_type = REGCACHE_RBTREE,
};
static int act8846_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct act8846 *act8846;	
	struct act8846_board *pdev ;
	const struct of_device_id *match;
	struct regulator_config config = { };
	struct regulator_dev *act_rdev;
	struct regulator_init_data *reg_data;
	const char *rail_name = NULL;
	int ret,i=0;
	
	printk("%s,line=%d\n", __func__,__LINE__);	

	if (i2c->dev.of_node) {
		match = of_match_device(act8846_of_match, &i2c->dev);
		if (!match) {
			printk("Failed to find matching dt id\n");
			return -EINVAL;
		}
	}

	act8846 = devm_kzalloc(&i2c->dev,sizeof(struct act8846), GFP_KERNEL);
	if (act8846 == NULL) {
		ret = -ENOMEM;		
		goto err;
	}
	act8846->i2c = i2c;
	act8846->dev = &i2c->dev;
	i2c_set_clientdata(i2c, act8846);
	g_act8846 = act8846;
	
	act8846->regmap = devm_regmap_init_i2c(i2c, &act8846_regmap_config);
	if (IS_ERR(act8846->regmap)) {
		ret = PTR_ERR(act8846->regmap);
		printk("regmap initialization failed: %d\n", ret);
		return ret;
	}
	
	mutex_init(&act8846->io_lock);	

	ret = act8846_reg_read(act8846,0x22);
	if ((ret < 0) || (ret == 0xff)){
		printk("The device is not act8846 %x \n",ret);
		goto err;
	}

	ret = act8846_set_bits(act8846, 0xf4,(0x1<<7),(0x0<<7));
	if (ret < 0) {
		printk("act8846 set 0xf4 error!\n");
		goto err;
	}

	if (act8846->dev->of_node)
		pdev = act8846_parse_dt(act8846);

	#ifdef CONFIG_OF
	act8846->pmic_hold_gpio = pdev->pmic_hold_gpio;
	if (act8846->pmic_hold_gpio) {
			ret = gpio_request(act8846->pmic_hold_gpio, "act8846_pmic_hold");
			if (ret < 0) {
				dev_err(act8846->dev,"Failed to request gpio %d with ret:""%d\n",	act8846->pmic_hold_gpio, ret);
				return IRQ_NONE;
			}
			gpio_direction_output(act8846->pmic_hold_gpio,1);
			ret = gpio_get_value(act8846->pmic_hold_gpio);
	//		gpio_free(act8846->pmic_hold_gpio);
			printk("%s: act8846_pmic_hold=%x\n", __func__, ret);
	}
	#endif
	
	/******************************set sleep vol & dcdc mode******************/
	#ifdef CONFIG_OF
	act8846->pmic_sleep_gpio = pdev->pmic_sleep_gpio;
	if (act8846->pmic_sleep_gpio) {
			ret = gpio_request(act8846->pmic_sleep_gpio, "act8846_pmic_sleep");
			if (ret < 0) {
				dev_err(act8846->dev,"Failed to request gpio %d with ret:""%d\n",	act8846->pmic_sleep_gpio, ret);
				return IRQ_NONE;
			}
			gpio_direction_output(act8846->pmic_sleep_gpio,1);
			ret = gpio_get_value(act8846->pmic_sleep_gpio);
			gpio_free(act8846->pmic_sleep_gpio);
			printk("%s: act8846_pmic_sleep=%x\n", __func__, ret);
	}
	#endif
	
	if (pdev) {
		act8846->num_regulators = act8846_NUM_REGULATORS;
		act8846->rdev = devm_kcalloc(act8846->dev,
					     act8846_NUM_REGULATORS,
					     sizeof(struct regulator_dev *),
					     GFP_KERNEL);
		if (!act8846->rdev) {
			return -ENOMEM;
		}
		/* Instantiate the regulators */
		for (i = 0; i < act8846_NUM_REGULATORS; i++) {
		reg_data = pdev->act8846_init_data[i];
		if (!reg_data)
			continue;
		config.dev = act8846->dev;
		config.driver_data = act8846;
		config.regmap = act8846->regmap;
		if (act8846->dev->of_node)
			config.of_node = pdev->of_node[i];

			if (reg_data && reg_data->constraints.name)
				rail_name = reg_data->constraints.name;
			else
				rail_name = regulators[i].name;
			reg_data->supply_regulator = rail_name;
	
		config.init_data =reg_data;
		
		act_rdev = regulator_register(&regulators[i],&config);
		if (IS_ERR(act_rdev)) {
			printk("failed to register %d regulator\n",i);
		goto err;
		}
		act8846->rdev[i] = act_rdev;
		}
	}

	if (pdev->pm_off && !pm_power_off) {
		pm_power_off = act8846_device_shutdown;
	}
	
	#ifdef CONFIG_HAS_EARLYSUSPEND
	act8846->act8846_suspend.suspend = act8846_early_suspend,
	act8846->act8846_suspend.resume = act8846_late_resume,
	act8846->act8846_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 1,
	register_early_suspend(&act8846->act8846_suspend);
	#endif	

	return 0;

err:
	return ret;	

}

static int  act8846_i2c_remove(struct i2c_client *i2c)
{
	struct act8846 *act8846 = i2c_get_clientdata(i2c);
	int i;

	for (i = 0; i < act8846->num_regulators; i++)
		if (act8846->rdev[i])
			regulator_unregister(act8846->rdev[i]);
	i2c_set_clientdata(i2c, NULL);

	return 0;
}

static const struct dev_pm_ops act8846_pm_ops = {
	.suspend = act8846_suspend,
	.resume =  act8846_resume,
};

static const struct i2c_device_id act8846_i2c_id[] = {
       { "act8846", 0 },
       { }
};

MODULE_DEVICE_TABLE(i2c, act8846_i2c_id);

static struct i2c_driver act8846_i2c_driver = {
	.driver = {
		.name = "act8846",
		.owner = THIS_MODULE,
		#ifdef CONFIG_PM
		.pm = &act8846_pm_ops,
		#endif
		.of_match_table =of_match_ptr(act8846_of_match),
	},
	.probe    = act8846_i2c_probe,
	.remove   = act8846_i2c_remove,
	.id_table = act8846_i2c_id,
};

static int __init act8846_module_init(void)
{
	int ret;
	ret = i2c_add_driver(&act8846_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register I2C driver: %d\n", ret);
	return ret;
}
//module_init(act8846_module_init);
//subsys_initcall(act8846_module_init);
//rootfs_initcall(act8846_module_init);
subsys_initcall_sync(act8846_module_init);

static void __exit act8846_module_exit(void)
{
	i2c_del_driver(&act8846_i2c_driver);
}
module_exit(act8846_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhangqing <zhangqing@rock-chips.com>");
MODULE_DESCRIPTION("act8846 PMIC driver");

