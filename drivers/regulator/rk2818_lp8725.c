/* drivers/regulator/rk2818_lp8725.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
/*******************************************************************/
/*	  COPYRIGHT (C)  ROCK-CHIPS FUZHOU . ALL RIGHTS RESERVED.			  */
/*******************************************************************
FILE		:	  rk2818_lp8725.c
DESC		:	  LP8725 PMIC driver
AUTHOR		:	  cym  
DATE		:	  2010-08-06
NOTES		:
$LOG: GPIO.C,V $
REVISION 0.01
********************************************************************/


#include <linux/bug.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/rk2818_lp8725.h>
#include <mach/gpio.h>
#include <linux/delay.h>
#include <mach/iomux.h>

//add by robert for reboot notifier
#include <linux/notifier.h>
#include <linux/reboot.h>

//end add



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


struct lp8725 {
	struct device *dev;
	struct mutex io_lock;
	struct i2c_client *i2c;
	int num_regulators;
	struct regulator_dev **rdev;
};

static u8 lp8725_reg_read(struct lp8725 *lp8725, u8 reg);
static int lp8725_set_bits(struct lp8725 *lp8725, u8 reg, u16 mask, u16 val);



#define LP8725_BUCK_VOL_ENABLE_REG 0x00
#define LP8725_BUCK1_BASE 0x08
#define LP8725_BUCK2_BASE 0x0A

const static int buck_base_addr[] = {
	LP8725_BUCK1_BASE,
	LP8725_BUCK2_BASE,	
};



#define LP8725_BUCK_TARGET_VOL1_REG(x) (buck_base_addr[x])
#define LP8725_BUCK_TARGET_VOL2_REG(x) (buck_base_addr[x]+1)

const static int buck_voltage_map[] = {
	 800,  850,  900,  950, 1000, 1050, 1100, 1150,
	1200, 1250, 1300, 1350, 1400, 1500, 1600, 1700,
	1750, 1800, 1850, 1900, 2000, 2100, 2200, 2300,
	2400, 2500, 2600, 2700, 2800, 2850, 2900, 3000,
};

#define BUCK_TARGET_VOL_MASK 0x1f
#define BUCK_TARGET_VOL_MIN_IDX 0x00
#define BUCK_TARGET_VOL_MAX_IDX 0x1f



#define LP8725_LDO_ENABLE_REG 0x0d
#define LP8725_LDO_VOL_CONTR_BASE 0x01

#define LP8725_LDO_VOL_CONTR_REG(x)	(LP8725_LDO_VOL_CONTR_BASE + x)

const static int ldo_voltage_map[] = {
	1200, 1250, 1300, 1350, 1400, 1450, 1500, 1550,
	1600, 1650, 1700, 1750, 1800, 1850, 1900, 2000,
	2100, 2200, 2300, 2400, 2500, 2600, 2650, 2700,
	2750, 2800, 2850, 2900, 2950, 3000, 3100, 3300,
};

#define LDO_VOL_CONTR_MASK 0x1f
#define LDO_VOL_MIN_IDX 0x00
#define LDO_VOL_MAX_IDX 0x1f

#define LP8725_LILO_ENABLE_REG 0x0d
#define LP8725_LILO_VOL_CONTR_BASE 0x06

#define LP8725_LILO_VOL_CONTR_REG(x)	(LP8725_LILO_VOL_CONTR_BASE + x)

const static int lilo_voltage_map[] = {
	 800,  850,  900,  950, 1000, 1050, 1100, 1150,
	1200, 1250, 1300, 1350, 1400, 1500, 1600, 1700,
	1800, 1900, 2000, 2100, 2200, 2300, 2400, 2500,
	2600, 2700, 2800, 2850, 2900, 3000, 3100, 3300,
};

#define LILO_VOL_CONTR_MASK 0x1f
#define LILO_VOL_MIN_IDX 0x00
#define LILO_VOL_MAX_IDX 0x1f


static int lp8725_ldo_list_voltage(struct regulator_dev *dev, unsigned index)
{
	return 1000 * ldo_voltage_map[index];
}

static int lp8725_ldo_is_enabled(struct regulator_dev *dev)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP8725_LDO1;
	u16 mask = 1 << (ldo);
	u16 val;

	val = lp8725_reg_read(lp8725, LP8725_LDO_ENABLE_REG);
	return (val & mask) != 0;
}

static int lp8725_ldo_enable(struct regulator_dev *dev)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP8725_LDO1;
	u16 mask = 1 << (ldo);
	return lp8725_set_bits(lp8725, LP8725_LDO_ENABLE_REG, mask, mask);
}

static int lp8725_ldo_disable(struct regulator_dev *dev)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP8725_LDO1;
	u16 mask = 1 << (ldo);
	return lp8725_set_bits(lp8725, LP8725_LDO_ENABLE_REG, mask, 0);
}

static int lp8725_ldo_get_voltage(struct regulator_dev *dev)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP8725_LDO1;	
	u16 reg;
	u32 val;

	reg = lp8725_reg_read(lp8725, LP8725_LDO_VOL_CONTR_REG(ldo));
	reg &= BUCK_TARGET_VOL_MASK;

	val = 1000 * ldo_voltage_map[reg];	
	return val;
}

static int lp8725_ldo_set_voltage(struct regulator_dev *dev,
				  int min_uV, int max_uV)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP8725_LDO1;
	int min_vol = min_uV / 1000, max_vol = max_uV / 1000;
	const int *vol_map = ldo_voltage_map;
	u16 val;	

	if (min_vol < vol_map[LDO_VOL_MIN_IDX] ||
	    min_vol > vol_map[LDO_VOL_MAX_IDX])
		return -EINVAL;

	for (val = LDO_VOL_MIN_IDX; val <= LDO_VOL_MAX_IDX; val++)
		if (vol_map[val] >= min_vol)
			break;

	if (vol_map[val] > max_vol)
		return -EINVAL;

	DBG("***run in %s %d reg=0x%x val=0x%x",__FUNCTION__,__LINE__,LP8725_LDO_VOL_CONTR_REG(ldo),val);

	return lp8725_set_bits(lp8725, LP8725_LDO_VOL_CONTR_REG(ldo),
		LDO_VOL_CONTR_MASK, val);
}

static struct regulator_ops lp8725_ldo_ops = {
	.list_voltage = lp8725_ldo_list_voltage,
	.is_enabled = lp8725_ldo_is_enabled,
	.enable = lp8725_ldo_enable,
	.disable = lp8725_ldo_disable,
	.get_voltage = lp8725_ldo_get_voltage,
	.set_voltage = lp8725_ldo_set_voltage,
};

static int lp8725_lilo_list_voltage(struct regulator_dev *dev, unsigned index)
{
	return 1000 * lilo_voltage_map[index];
}

static int lp8725_lilo_is_enabled(struct regulator_dev *dev)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);
	int lilo = rdev_get_id(dev) - LP8725_LILO1;
	u16 mask = 1 << (lilo+5);
	u16 val;

	val = lp8725_reg_read(lp8725, LP8725_LILO_ENABLE_REG);
	return (val & mask) != 0;
}

static int lp8725_lilo_enable(struct regulator_dev *dev)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);
	int lilo = rdev_get_id(dev) - LP8725_LILO1;
	u16 mask = 1 << (lilo+5);

	return lp8725_set_bits(lp8725, LP8725_LILO_ENABLE_REG, mask, mask);
}

static int lp8725_lilo_disable(struct regulator_dev *dev)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);
	int lilo = rdev_get_id(dev) - LP8725_LILO1;
	u16 mask = 1 << (lilo+5);

	return lp8725_set_bits(lp8725, LP8725_LILO_ENABLE_REG, mask, 0);
}

static int lp8725_lilo_get_voltage(struct regulator_dev *dev)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);
	int lilo = rdev_get_id(dev) - LP8725_LILO1;
	u16 reg;
	u32 val;

	reg = lp8725_reg_read(lp8725, LP8725_LILO_VOL_CONTR_REG(lilo));
	reg &= BUCK_TARGET_VOL_MASK;

	val = 1000 * lilo_voltage_map[reg];	
	return val;
}

static int lp8725_lilo_set_voltage(struct regulator_dev *dev,
				  int min_uV, int max_uV)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);
	int lilo = rdev_get_id(dev) - LP8725_LILO1;
	int min_vol = min_uV / 1000, max_vol = max_uV / 1000;
	const int *vol_map = lilo_voltage_map;
	u16 val;

	if (min_vol < vol_map[LILO_VOL_MIN_IDX] ||
	    min_vol > vol_map[LILO_VOL_MAX_IDX])
		return -EINVAL;

	for (val = LILO_VOL_MIN_IDX; val <= LILO_VOL_MAX_IDX; val++)
		if (vol_map[val] >= min_vol)
			break;

	if (vol_map[val] > max_vol)
		return -EINVAL;

	return lp8725_set_bits(lp8725, LP8725_LILO_VOL_CONTR_REG(lilo),
		LILO_VOL_CONTR_MASK, val);
}

static struct regulator_ops lp8725_lilo_ops = {
	.list_voltage = lp8725_lilo_list_voltage,
	.is_enabled = lp8725_lilo_is_enabled,
	.enable = lp8725_lilo_enable,
	.disable = lp8725_lilo_disable,
	.get_voltage = lp8725_lilo_get_voltage,
	.set_voltage = lp8725_lilo_set_voltage,
};

static int lp8725_dcdc_list_voltage(struct regulator_dev *dev, unsigned index)
{
	return 1000 * buck_voltage_map[index];
}

static int lp8725_dcdc_is_enabled(struct regulator_dev *dev)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev);
	u16 val;
	u16 mask,mask2;

	switch(buck)
	{
	case LP8725_DCDC1:
		mask = 1 << 0;
		mask2 = 1 << 2;		
		val = lp8725_reg_read(lp8725, LP8725_BUCK_VOL_ENABLE_REG);		
		return ((val & mask) && (val & mask2)) != 0;	
	case LP8725_DCDC2:		
		mask = 1 << 4;
		mask2 = 1 << 3;
		val = lp8725_reg_read(lp8725, LP8725_BUCK_VOL_ENABLE_REG);		
		return ((val & mask) && (val & mask2)) != 0;
	case LP8725_DCDC1_V2:
		mask = 1 << 0;
		mask2 = 1<< 2;
		val = lp8725_reg_read(lp8725, LP8725_BUCK_VOL_ENABLE_REG);		
		return  ((val & mask) && (!(val & mask2))) !=0;
	case LP8725_DCDC2_V2:		
		mask = 1 << 4;
		mask2 = 1 << 3;
		val = lp8725_reg_read(lp8725, LP8725_BUCK_VOL_ENABLE_REG);		
		return ((val & mask) && (!(val & mask2))) !=0;
	}
}

static int lp8725_dcdc_enable(struct regulator_dev *dev)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev);
	u16 mask;
	int ret = 0;
	switch(buck)
	{
	case LP8725_DCDC1:
		mask = 1 << 0;
		ret = lp8725_set_bits(lp8725, LP8725_BUCK_VOL_ENABLE_REG, mask, mask);
		mask = 1 << 2;
		ret = lp8725_set_bits(lp8725, LP8725_BUCK_VOL_ENABLE_REG, mask, mask);		
		break;
	case LP8725_DCDC1_V2:
		mask = 1 << 0;
		ret = lp8725_set_bits(lp8725, LP8725_BUCK_VOL_ENABLE_REG, mask, mask);
		mask = 1 << 2;
		ret = lp8725_set_bits(lp8725, LP8725_BUCK_VOL_ENABLE_REG, mask, 0);		
		break;
	case LP8725_DCDC2:
		mask = 1 << 4;
		ret = lp8725_set_bits(lp8725, LP8725_BUCK_VOL_ENABLE_REG, mask, mask);	
		mask = 1 << 3;
		ret = lp8725_set_bits(lp8725, LP8725_BUCK_VOL_ENABLE_REG, mask, mask);		
		break;
	case LP8725_DCDC2_V2:
		mask = 1 << 4;
		ret = lp8725_set_bits(lp8725, LP8725_BUCK_VOL_ENABLE_REG, mask, mask);	
		mask = 1 << 3;
		ret = lp8725_set_bits(lp8725, LP8725_BUCK_VOL_ENABLE_REG, mask, 0);		
		break;
	}
	dev->use_count--;
	return ret;
}

static int lp8725_dcdc_disable(struct regulator_dev *dev)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) ;
	u16 mask;	

	switch(buck)
	{
	case LP8725_DCDC1:
	case LP8725_DCDC1_V2:
		mask = 1 << 0;
		return lp8725_set_bits(lp8725, LP8725_BUCK_VOL_ENABLE_REG, mask, 0);
	case LP8725_DCDC2:
	case LP8725_DCDC2_V2:
		mask = 1 << 4;
		return lp8725_set_bits(lp8725, LP8725_BUCK_VOL_ENABLE_REG, mask,0);
	}
}

static int lp8725_dcdc_get_voltage(struct regulator_dev *dev)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) ;
	u16 reg = 0;
	int val;

	switch(buck)
	{
	case LP8725_DCDC1:
	case LP8725_DCDC2:
		buck -= LP8725_DCDC1;
		reg = lp8725_reg_read(lp8725, LP8725_BUCK_TARGET_VOL1_REG(buck));
		break;
	case LP8725_DCDC1_V2:
	case LP8725_DCDC2_V2:
		buck -= LP8725_DCDC1_V2;
		reg = lp8725_reg_read(lp8725, LP8725_BUCK_TARGET_VOL2_REG(buck));
		break;
	}

	reg &= BUCK_TARGET_VOL_MASK;
	val = 1000 * buck_voltage_map[reg];
	
	return val;
}

static int lp8725_dcdc_set_voltage(struct regulator_dev *dev,
				  int min_uV, int max_uV)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev);
	int min_vol = min_uV / 1000, max_vol = max_uV / 1000;
	const int *vol_map = buck_voltage_map;
	u16 val;
	int ret = 0;

	if (min_vol < vol_map[BUCK_TARGET_VOL_MIN_IDX] ||
	    min_vol > vol_map[BUCK_TARGET_VOL_MAX_IDX])
		return -EINVAL;

	for (val = BUCK_TARGET_VOL_MIN_IDX; val <= BUCK_TARGET_VOL_MAX_IDX;
	     val++)
		if (vol_map[val] >= min_vol)
			break;

	if (vol_map[val] > max_vol)
		return -EINVAL;

	switch(buck)
	{
	case LP8725_DCDC1:
	case LP8725_DCDC2:
		buck -= LP8725_DCDC1;
		ret = lp8725_set_bits(lp8725, LP8725_BUCK_TARGET_VOL1_REG(buck),
	       	BUCK_TARGET_VOL_MASK, val);
		if (ret)
			return ret;
		break;
	case LP8725_DCDC1_V2:
	case LP8725_DCDC2_V2:
		buck -= LP8725_DCDC1_V2;
		ret = lp8725_set_bits(lp8725, LP8725_BUCK_TARGET_VOL2_REG(buck),
	       	BUCK_TARGET_VOL_MASK, val);
		if (ret)
			return ret;
		break;
	}

	return ret;
}

static unsigned int lp8725_dcdc_get_mode(struct regulator_dev *dev)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);
	u16 mask = 1 << 1;
	u16 val;
	val = lp8725_reg_read(lp8725, LP8725_BUCK_VOL_ENABLE_REG);
	if ((val & mask) == 0)
		return REGULATOR_MODE_NORMAL;
	else
		return REGULATOR_MODE_IDLE;
}

static int lp8725_dcdc_set_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct lp8725 *lp8725 = rdev_get_drvdata(dev);	
	u16 mask = 1 << 1;
	switch(mode)
	{
	case REGULATOR_MODE_NORMAL:
		return lp8725_set_bits(lp8725, LP8725_BUCK_VOL_ENABLE_REG, mask, 0);		
	case REGULATOR_MODE_IDLE:
		return lp8725_set_bits(lp8725, LP8725_BUCK_VOL_ENABLE_REG, mask, mask);
	default:
		printk("error:pmu_lp8725 only normal and idle mode\n");
		return -EINVAL;
	}	
}

static struct regulator_ops lp8725_dcdc_ops = {
	.list_voltage = lp8725_dcdc_list_voltage,
	.is_enabled = lp8725_dcdc_is_enabled,
	.enable = lp8725_dcdc_enable,
	.disable = lp8725_dcdc_disable,
	.get_voltage = lp8725_dcdc_get_voltage,
	.set_voltage = lp8725_dcdc_set_voltage,
	.get_mode = lp8725_dcdc_get_mode,
	.set_mode = lp8725_dcdc_set_mode,
};

static struct regulator_desc regulators[] = {
	{
		.name = "LDO1",
		.id = LP8725_LDO1,
		.ops = &lp8725_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO2",
		.id = LP8725_LDO2,
		.ops = &lp8725_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO3",
		.id = LP8725_LDO3,
		.ops = &lp8725_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO4",
		.id = LP8725_LDO4,
		.ops = &lp8725_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO5",
		.id = LP8725_LDO5,
		.ops = &lp8725_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LILO1",
		.id = LP8725_LILO1,
		.ops = &lp8725_lilo_ops,
		.n_voltages = ARRAY_SIZE(lilo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LILO2",
		.id = LP8725_LILO2,
		.ops = &lp8725_lilo_ops,
		.n_voltages = ARRAY_SIZE(lilo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC1",
		.id = LP8725_DCDC1,
		.ops = &lp8725_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC2",
		.id = LP8725_DCDC2,
		.ops = &lp8725_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC1_V2",
		.id = LP8725_DCDC1_V2,
		.ops = &lp8725_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC2_V2",
		.id = LP8725_DCDC2_V2,
		.ops = &lp8725_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
};

static int lp8725_i2c_read(struct i2c_client *i2c, char reg, int count,	u16 *dest)
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
    
    msgs[1].buf = dest;
    msgs[1].addr = i2c->addr;
    msgs[1].flags = i2c->flags | I2C_M_RD;
    msgs[1].len = 1;
    msgs[1].scl_rate = 200*1000;
    ret = i2c_transfer(adap, msgs, 2);

	DBG("***run in %s %d msgs[1].buf = %d\n",__FUNCTION__,__LINE__,*(msgs[1].buf));

	return 0;     
}

static int lp8725_i2c_write(struct i2c_client *i2c, char reg, int count, const u16 src)
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

static u8 lp8725_reg_read(struct lp8725 *lp8725, u8 reg)
{
	u16 val = 0;

	mutex_lock(&lp8725->io_lock);

	lp8725_i2c_read(lp8725->i2c, reg, 1, &val);

	DBG("reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)val&0xff);

	mutex_unlock(&lp8725->io_lock);

	return val & 0xff;
}

static int lp8725_set_bits(struct lp8725 *lp8725, u8 reg, u16 mask, u16 val)
{
	u16 tmp;
	int ret;

	mutex_lock(&lp8725->io_lock);

	ret = lp8725_i2c_read(lp8725->i2c, reg, 1, &tmp);
	tmp = (tmp & ~mask) | val;
	if (ret == 0) {
		ret = lp8725_i2c_write(lp8725->i2c, reg, 1, tmp);
		DBG("reg write 0x%02x -> 0x%02x\n", (int)reg, (unsigned)val&0xff);
	}
	mutex_unlock(&lp8725->io_lock);

	return ret;
}


//add by robert for power on bp
#define AP_TD_UNDEFINED_GBIN5 FPGA_PIO2_02
#define AP_RESET_TD FPGA_PIO2_04
#define AP_SHUTDOWN_TD_PMU FPGA_PIO2_05
#define AP_PW_EN_TD FPGA_PIO2_03

static int bp_power_on(void)
{
	int ret=0;
	
	ret = gpio_request(AP_TD_UNDEFINED_GBIN5, NULL);
	if (ret) {
		printk("%s:failed to request fpga s %d\n",__FUNCTION__,__LINE__);
		goto err;
	}
	ret = gpio_request(AP_RESET_TD, NULL);
	if (ret) {
		printk("%s:failed to request fpga s %d\n",__FUNCTION__,__LINE__);
		goto err0;
	}
	

	ret = gpio_request(AP_SHUTDOWN_TD_PMU, NULL);
	if (ret) {
		printk("%s:failed to request fpga %d\n",__FUNCTION__,__LINE__);
		goto err1;
	}

	ret = gpio_request(AP_PW_EN_TD, NULL);
	if (ret) {
		printk("%s:failed to request fpga  %d\n",__FUNCTION__,__LINE__);
		goto err2;
	}

	gpio_set_value(AP_TD_UNDEFINED_GBIN5, 1);
       gpio_direction_output(AP_TD_UNDEFINED_GBIN5, 1);   
	gpio_direction_input(AP_RESET_TD);

	 gpio_set_value(AP_SHUTDOWN_TD_PMU, 0);
        gpio_direction_output(AP_SHUTDOWN_TD_PMU, 0);  

	gpio_set_value(AP_PW_EN_TD, 0);
	gpio_direction_output(AP_PW_EN_TD, 0);  
	mdelay(1);
	gpio_set_value(AP_PW_EN_TD, 1);
	mdelay(1200);
	gpio_set_value(AP_PW_EN_TD, 0);

	return true;
err2:
	gpio_free(AP_SHUTDOWN_TD_PMU);
err1:
	gpio_free(AP_RESET_TD);
err0:
	gpio_free(AP_TD_UNDEFINED_GBIN5);
err:	
	return false;
}



static int bp_power_off(struct notifier_block *this,
					unsigned long code, void *unused)
{
	printk("+++--++++++%s_________%d \r\n",__FUNCTION__,code);

	 gpio_set_value(AP_TD_UNDEFINED_GBIN5, 0);
	
	gpio_set_value(AP_PW_EN_TD, 0);
	//gpio_direction_output(AP_PW_EN_TD, 0);  
	mdelay(1);
	gpio_set_value(AP_PW_EN_TD, 1);
	mdelay(1200);
	gpio_set_value(AP_PW_EN_TD, 0);

	mdelay(5000);
	 gpio_set_value(AP_SHUTDOWN_TD_PMU, 1);
	mdelay(1200);
	// gpio_free(AP_PW_EN_TD);
printk("++++--+++++%s   ok_________\r\n",__FUNCTION__);
	 return NOTIFY_DONE;
}
//add end

static int lp8725_set_init(void)
{
	int tmp = 0;
	struct regulator *ldo1,*ldo2,*ldo3,*ldo4,*ldo5;
	struct regulator *lilo1,*lilo2;
	struct regulator *buck1,*buck1_v2,*buck2;

	DBG_INFO("***run in %s %d ",__FUNCTION__,__LINE__);

	/*init ldo1*/
	DBG_INFO("***ldo1 vcc init\n");
	ldo1 = regulator_get(NULL, "ldo1");
	regulator_enable(ldo1);
//	regulator_set_voltage(ldo1,2500000,2500000);
	tmp = regulator_get_voltage(ldo1);
	DBG_INFO("***regulator_set_init: ldo1 vcc =%d\n",tmp);

	/*init ldo2*/
	DBG_INFO("***ldo2 vcc init\n");
	ldo2 = regulator_get(NULL, "ldo2");
	regulator_enable(ldo2);
//	regulator_set_voltage(ldo2,3000000,3000000);
	tmp = regulator_get_voltage(ldo2);
	DBG_INFO("***regulator_set_init: ldo2 vcc =%d\n",tmp);

	/*init ldo3*/
	DBG_INFO("***ldo3 vcc init\n");
	ldo3 = regulator_get(NULL, "ldo3");
	regulator_enable(ldo3);
//	regulator_set_voltage(ldo3,3000000,3000000);
	tmp = regulator_get_voltage(ldo3);
	DBG_INFO("***regulator_set_init: ldo3 vcc =%d\n",tmp);

	/*init ldo4*/
	DBG_INFO("***ldo4 vcc init\n");
	ldo4 = regulator_get(NULL, "ldo4");
	regulator_enable(ldo4);
//	regulator_set_voltage(ldo4,1900000,1900000);
	tmp = regulator_get_voltage(ldo4);
	DBG_INFO("***regulator_set_init: ldo4 vcc =%d\n",tmp);

	/*init ldo5*/
	DBG_INFO("***ldo5 vcc init\n");
	ldo5 = regulator_get(NULL, "ldo5");
	regulator_enable(ldo5);
	regulator_set_voltage(ldo5,1800000,1800000);
	tmp = regulator_get_voltage(ldo5);
	DBG_INFO("***regulator_set_init: ldo5 vcc =%d\n",tmp);

	/*init lilo1*/
	DBG_INFO("***lilo1 vcc init\n");
	lilo1 = regulator_get(NULL, "lilo1");
	regulator_enable(lilo1);
//	regulator_set_voltage(lilo1,3300000,3300000);
	tmp = regulator_get_voltage(lilo1);
	DBG_INFO("***regulator_set_init: lilo1 vcc =%d\n",tmp);

	/*init lilo2*/
	DBG_INFO("***lilo2 vcc init\n");
	lilo2 = regulator_get(NULL, "lilo2");
	regulator_enable(lilo2);
//	regulator_set_voltage(lilo2,3300000,3300000);
	tmp = regulator_get_voltage(lilo2);
	DBG_INFO("***regulator_set_init: lilo2 vcc =%d\n",tmp);
	
	/*init buck1*/
	DBG_INFO("***buck1 vcc init\n");
	buck1 = regulator_get(NULL, "vdd12");
//	regulator_set_voltage(buck1,1200000,1200000);
	tmp = regulator_get_voltage(buck1);
	DBG_INFO("***regulator_set_init: buck1 vcc =%d\n",tmp);

	#ifdef PM_CONTROL
	DBG_INFO("***buck1 v2 init\n");
	buck1_v2 = regulator_get(NULL, "vdd12_v2");// dvs 0
	regulator_enable(buck1_v2);
	regulator_set_voltage(buck1_v2,1000000,1000000);//1300000
	tmp = regulator_get_voltage(buck1_v2);
	DBG_INFO("***regulator_set_init: buck1 v2 =%d\n",tmp);
	#endif

	/*init buck2*/
	DBG_INFO("***buck2 vcc init\n");
	buck2 = regulator_get(NULL, "vccdr");
//	regulator_set_voltage(buck1,1800000,1800000);
	tmp = regulator_get_voltage(buck2);
	DBG_INFO("***regulator_set_init: buck2 vcc =%d\n",tmp);

	
//add by robert for power on bp
	bp_power_on();
//end add

	return(0);
}


static int __devinit setup_regulators(struct lp8725 *lp8725, struct lp8725_platform_data *pdata)
{	
	int i, err;

	lp8725->num_regulators = pdata->num_regulators;
	lp8725->rdev = kcalloc(pdata->num_regulators,
			       sizeof(struct regulator_dev *), GFP_KERNEL);
	if (!lp8725->rdev) {
		return -ENOMEM;
	}

	/* Instantiate the regulators */
	for (i = 0; i < pdata->num_regulators; i++) {
		int id = pdata->regulators[i].id;
		lp8725->rdev[i] = regulator_register(&regulators[id],
			lp8725->dev, pdata->regulators[i].initdata, lp8725);

		if (IS_ERR(lp8725->rdev[i])) {
			err = PTR_ERR(lp8725->rdev[i]);
			dev_err(lp8725->dev, "regulator init failed: %d\n",
				err);
			goto error;
		}
	}

	return 0;
error:
	while (--i >= 0)
		regulator_unregister(lp8725->rdev[i]);
	kfree(lp8725->rdev);
	lp8725->rdev = NULL;
	return err;
}

static int __devinit lp8725_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct lp8725 *lp8725;	
	struct lp8725_platform_data *pdata = i2c->dev.platform_data;		
	int ret;

	lp8725 = kzalloc(sizeof(struct lp8725), GFP_KERNEL);
	if (lp8725 == NULL) {
		ret = -ENOMEM;		
		goto err;
	}

	lp8725->i2c = i2c;
	lp8725->dev = &i2c->dev;
	i2c_set_clientdata(i2c, lp8725);

	mutex_init(&lp8725->io_lock);		

	if (pdata) {
		ret = setup_regulators(lp8725, pdata);
		if (ret < 0)		
			goto err;
	} else
		dev_warn(lp8725->dev, "No platform init data supplied\n");

	//DVS pin control, make sure it is high level at start.
	#ifdef PM_CONTROL
	rk2818_lp8725_pm_control();
	#endif
	lp8725_set_init();

	return 0;

err:
	return ret;
}

static int __devexit lp8725_i2c_remove(struct i2c_client *i2c)
{
	struct lp8725 *lp8725 = i2c_get_clientdata(i2c);
	int i;

	for (i = 0; i < lp8725->num_regulators; i++)
		if (lp8725->rdev[i])
			regulator_unregister(lp8725->rdev[i]);
	kfree(lp8725->rdev);
	i2c_set_clientdata(i2c, NULL);
	kfree(lp8725);

	return 0;
}

static const struct i2c_device_id lp8725_i2c_id[] = {
       { "lp8725", 0 },
       { }
};

MODULE_DEVICE_TABLE(i2c, lp8725_i2c_id);

static struct i2c_driver lp8725_i2c_driver = {
	.driver = {
		.name = "lp8725",
		.owner = THIS_MODULE,
	},
	.probe    = lp8725_i2c_probe,
	.remove   = __devexit_p(lp8725_i2c_remove),
	.id_table = lp8725_i2c_id,
};


//add by robert for bp powerdown register
static struct notifier_block BP_powerdown_notifier = {
	.notifier_call =	bp_power_off,
};
//end add



static int __init lp8725_module_init(void)
{
	int ret;

	ret = i2c_add_driver(&lp8725_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register I2C driver: %d\n", ret);

	//add by robert for bp powerdown register
	ret = register_reboot_notifier(&BP_powerdown_notifier);
	if (ret != 0) 
		{
		printk("cannot register reboot notifier (err=%d), %s\n", ret,__FUNCTION__);
		}
	//end add


	return ret;
}
module_init(lp8725_module_init);

static void __exit lp8725_module_exit(void)
{
//add by robert for bp power down
	unregister_reboot_notifier(&BP_powerdown_notifier);
//end add

	i2c_del_driver(&lp8725_i2c_driver);
}
module_exit(lp8725_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("cym <cym@rock-chips.com>");
MODULE_DESCRIPTION("LP8725 PMIC driver");
