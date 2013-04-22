/*
 * Regulator driver for rk808 PMIC chip for rk31xx
 *
 * Based on rk808.c that is work by zhangqing<zhangqing@rock-chips.com>
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
#include <linux/mfd/rk808.h>
#include <linux/mfd/core.h>
#include <mach/gpio.h>
#include <linux/delay.h>
#include <mach/iomux.h>
#include <linux/slab.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/interrupt.h>


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

struct rk808 *g_rk808;
#define DCDC_RAISE_VOL_BYSTEP 1
#define DCDC_VOL_STEP 25000  //25mv

static struct mfd_cell rk808s[] = {
	{
		.name = "rk808-rtc",
	},
};

#define BUCK_VOL_MASK 0x3f
#define LDO_VOL_MASK 0x3f

#define VOL_MIN_IDX 0x00
#define VOL_MAX_IDX 0x3f

const static int buck_set_vol_base_addr[] = {
	RK808_BUCK1_ON_REG,
	RK808_BUCK2_ON_REG,
	RK808_BUCK3_CONFIG_REG,
	RK808_BUCK4_ON_REG,
};
const static int buck_contr_base_addr[] = {
	RK808_BUCK1_CONFIG_REG,
 	RK808_BUCK2_CONFIG_REG,
 	RK808_BUCK3_CONFIG_REG,
 	RK808_BUCK4_CONFIG_REG,
};
#define rk808_BUCK_SET_VOL_REG(x) (buck_set_vol_base_addr[x])
#define rk808_BUCK_CONTR_REG(x) (buck_contr_base_addr[x])


const static int ldo_set_vol_base_addr[] = {
	RK808_LDO1_ON_VSEL_REG,
	RK808_LDO2_ON_VSEL_REG,
	RK808_LDO3_ON_VSEL_REG,
	RK808_LDO4_ON_VSEL_REG, 
	RK808_LDO5_ON_VSEL_REG, 
	RK808_LDO6_ON_VSEL_REG, 
	RK808_LDO7_ON_VSEL_REG, 
	RK808_LDO8_ON_VSEL_REG, 
//	RK808_LDO1_ON_VSEL_REG, 
};
/*
const static int ldo_contr_base_addr[] = {
	rk808_LDO1_CONTR_BASE,
	rk808_LDO2_CONTR_BASE,
	rk808_LDO3_CONTR_BASE,
	rk808_LDO4_CONTR_BASE,
	rk808_LDO5_CONTR_BASE,
	rk808_LDO6_CONTR_BASE,
	rk808_LDO7_CONTR_BASE,
	rk808_LDO8_CONTR_BASE,
//	rk808_LDO9_CONTR_BASE,
};
*/
#define rk808_LDO_SET_VOL_REG(x) (ldo_set_vol_base_addr[x])
//#define rk808_LDO_CONTR_REG(x) (ldo_contr_base_addr[x])

const static int buck_voltage_map[] = {
	  700,  712,  725,  737,  750, 762,  775,  787,  800, 
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
          2700, 2800, 2900, 3000, 3100, 3200,3300, 
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

static int rk808_ldo_list_voltage(struct regulator_dev *dev, unsigned index)
{
	int ldo= rdev_get_id(dev) - RK808_LDO1;
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
static int rk808_ldo_is_enabled(struct regulator_dev *dev)
{
	struct rk808 *rk808 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - RK808_LDO1;	
	u16 val;
	
	val = rk808_reg_read(rk808, RK808_LDO_EN_REG);
	if (val < 0)
		return val;
	if (val & (1 << ldo))
		return 1;
	else
		return 0; 		
}
static int rk808_ldo_enable(struct regulator_dev *dev)
{
	struct rk808 *rk808 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - RK808_LDO1;
	
	return rk808_set_bits(rk808, RK808_LDO_EN_REG, 1 << ldo, 1 << ldo);
	
}
static int rk808_ldo_disable(struct regulator_dev *dev)
{
	struct rk808 *rk808 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - RK808_LDO1;
	
	return rk808_set_bits(rk808, RK808_LDO_EN_REG, 1 << ldo, 0);

}
static int rk808_ldo_get_voltage(struct regulator_dev *dev)
{
	struct rk808 *rk808 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - RK808_LDO1;
	u16 reg = 0;
	int val;
	reg = rk808_reg_read(rk808,rk808_LDO_SET_VOL_REG(ldo));
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
	return val;
}
static int rk808_ldo_set_sleep_voltage(struct regulator_dev *dev,
					    int uV)
{
	struct rk808 *rk808 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - RK808_LDO1;
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

	ret = rk808_set_bits(rk808, rk808_LDO_SET_VOL_REG(ldo) +0x01,
	       	LDO_VOL_MASK, val);
	return ret;
}

static int rk808_ldo_set_voltage(struct regulator_dev *dev,
				  int min_uV, int max_uV,unsigned *selector)
{
	struct rk808 *rk808 = rdev_get_drvdata(dev);
	int ldo= rdev_get_id(dev) - RK808_LDO1;
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
	
	ret = rk808_set_bits(rk808, rk808_LDO_SET_VOL_REG(ldo),
	       	LDO_VOL_MASK, val);
	return ret;

}
static unsigned int rk808_ldo_get_mode(struct regulator_dev *dev)
{
	struct rk808 *rk808 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - RK808_LDO1;
	u16 mask = 0x80;
	u16 val;
	val = rk808_reg_read(rk808, rk808_LDO_SET_VOL_REG(ldo));
        if (val < 0) {
                return val;
        }
	val=val & mask;
	if (val== mask)
		return REGULATOR_MODE_NORMAL;
	else
		return REGULATOR_MODE_STANDBY;

}
static int rk808_ldo_set_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct rk808 *rk808 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - RK808_LDO1;
	u16 mask = 0x80;
	switch(mode)
	{
	case REGULATOR_MODE_FAST:
		return rk808_set_bits(rk808, rk808_LDO_SET_VOL_REG(ldo), mask, mask);		
	case REGULATOR_MODE_NORMAL:
		return rk808_set_bits(rk808, rk808_LDO_SET_VOL_REG(ldo), mask, 0);
	default:
		printk("error:pmu_rk808 only lowpower and nomal mode\n");
		return -EINVAL;
	}


}
static struct regulator_ops rk808_ldo_ops = {
	.set_voltage = rk808_ldo_set_voltage,
	.get_voltage = rk808_ldo_get_voltage,
	.list_voltage = rk808_ldo_list_voltage,
	.is_enabled = rk808_ldo_is_enabled,
	.enable = rk808_ldo_enable,
	.disable = rk808_ldo_disable,
	.get_mode = rk808_ldo_get_mode,
	.set_mode = rk808_ldo_set_mode,
	.set_suspend_voltage = rk808_ldo_set_sleep_voltage,
	
};

static int rk808_dcdc_list_voltage(struct regulator_dev *dev, unsigned selector)
{
	int volt;
	int buck = rdev_get_id(dev) - RK808_DCDC1;

	if (selector < 0x0 ||selector > BUCK_VOL_MASK )
		return -EINVAL;

	switch (buck) {
	case 0:
	case 1:
		volt = 700000 + selector * 12500;
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
static int rk808_dcdc_is_enabled(struct regulator_dev *dev)
{
	struct rk808 *rk808 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK808_DCDC1;
	u16 val;
	
	val = rk808_reg_read(rk808, RK808_DCDC_EN_REG);
	if (val < 0)
		return val;
	if (val & (1 << buck))
		return 1;
	else
		return 0; 	
}
static int rk808_dcdc_enable(struct regulator_dev *dev)
{
	struct rk808 *rk808 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK808_DCDC1;

	return rk808_set_bits(rk808, RK808_DCDC_EN_REG, 1 << buck, 1 << buck);

}
static int rk808_dcdc_disable(struct regulator_dev *dev)
{
	struct rk808 *rk808 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK808_DCDC1;
	
	 return rk808_set_bits(rk808, RK808_DCDC_EN_REG, 1 << buck , 0);
}
static int rk808_dcdc_get_voltage(struct regulator_dev *dev)
{
	struct rk808 *rk808 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK808_DCDC1;
	u16 reg = 0;
	int val;

	reg = rk808_reg_read(rk808,rk808_BUCK_SET_VOL_REG(buck));
	
	reg &= BUCK_VOL_MASK;
    	val = rk808_dcdc_list_voltage(dev,reg);
	return val;
}
static int rk808_dcdc_select_min_voltage(struct regulator_dev *dev,
					   int min_uV, int max_uV ,int buck)
{
	u16 vsel =0;
	
	if (buck == 0 || buck ==  1){
		if (min_uV < 700000)
		vsel = 0;
		else if (min_uV <= 1500000)
		vsel = ((min_uV - 700000) / 12500) ;
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
	if (rk808_dcdc_list_voltage(dev, vsel) > max_uV)
		return -EINVAL;
	return vsel;
}

static int rk808_dcdc_set_voltage(struct regulator_dev *dev,
				  int min_uV, int max_uV,unsigned *selector)
{
	struct rk808 *rk808 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK808_DCDC1;
	u16 val;
	int ret = 0,old_voltage =0,vol_temp =0;

	if (buck ==2){
		return 0;
	}else if (buck==3){
		val = rk808_dcdc_select_min_voltage(dev,min_uV,max_uV,buck);	
		ret = rk808_set_bits(rk808, rk808_BUCK_SET_VOL_REG(buck), BUCK_VOL_MASK, val);
	}
	else {
#if defined(DCDC_RAISE_VOL_BYSTEP)
		old_voltage = rk808_dcdc_get_voltage(dev);
			if (max_uV >old_voltage){
				vol_temp = old_voltage;
			       do{
					vol_temp +=   DCDC_VOL_STEP;
					val = rk808_dcdc_select_min_voltage(dev,vol_temp,vol_temp,buck);
				//	printk("rk808_dcdc_set_voltage buck = %d vol_temp= %d old_voltage= %d min_uV =%d \n",buck,vol_temp,old_voltage,min_uV);
					ret = rk808_set_bits(rk808, rk808_BUCK_SET_VOL_REG(buck), BUCK_VOL_MASK, val);	
				}while(vol_temp != max_uV);
			}
			else{
				val = rk808_dcdc_select_min_voltage(dev,min_uV,max_uV,buck);
				ret = rk808_set_bits(rk808, rk808_BUCK_SET_VOL_REG(buck), BUCK_VOL_MASK, val);
			}
#else
		val = rk808_dcdc_select_min_voltage(dev,min_uV,max_uV,buck);
		ret = rk808_set_bits(rk808, rk808_BUCK_SET_VOL_REG(buck), BUCK_VOL_MASK, val);
#endif
	}
	return ret;
}
static int rk808_dcdc_set_sleep_voltage(struct regulator_dev *dev,
					    int uV)
{
	struct rk808 *rk808 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK808_DCDC1;
	u16 val;
	int ret = 0;

	if (buck ==2){
	return 0;
	}else{
	val = rk808_dcdc_select_min_voltage(dev,uV,uV,buck);
	ret = rk808_set_bits(rk808, (rk808_BUCK_SET_VOL_REG(buck) + 0x01), BUCK_VOL_MASK, val);
	}
	return ret;
}
static unsigned int rk808_dcdc_get_mode(struct regulator_dev *dev)
{
	struct rk808 *rk808 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK808_DCDC1;
	u16 mask = 0x80;
	u16 val;
	val = rk808_reg_read(rk808, rk808_BUCK_SET_VOL_REG(buck));
        if (val < 0) {
                return val;
        }
	val=val & mask;
	if (val== mask)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;

}
static int rk808_dcdc_set_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct rk808 *rk808 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - RK808_DCDC1;
	u16 mask = 0x80;
	switch(mode)
	{
	case REGULATOR_MODE_FAST:
		return rk808_set_bits(rk808, rk808_BUCK_SET_VOL_REG(buck), mask, mask);
	case REGULATOR_MODE_NORMAL:
		return rk808_set_bits(rk808, rk808_BUCK_SET_VOL_REG(buck), mask, 0);
	default:
		printk("error:pmu_rk808 only powersave and pwm mode\n");
		return -EINVAL;
	}

}
static int rk808_dcdc_set_voltage_time_sel(struct regulator_dev *dev,   unsigned int old_selector,
				     unsigned int new_selector)
{
	int old_volt, new_volt;
	
	old_volt = rk808_dcdc_list_voltage(dev, old_selector);
	if (old_volt < 0)
		return old_volt;
	
	new_volt = rk808_dcdc_list_voltage(dev, new_selector);
	if (new_volt < 0)
		return new_volt;

	return DIV_ROUND_UP(abs(old_volt - new_volt)*2, 2500);
}

static struct regulator_ops rk808_dcdc_ops = { 
	.set_voltage = rk808_dcdc_set_voltage,
	.get_voltage = rk808_dcdc_get_voltage,
	.list_voltage= rk808_dcdc_list_voltage,
	.is_enabled = rk808_dcdc_is_enabled,
	.enable = rk808_dcdc_enable,
	.disable = rk808_dcdc_disable,
	.get_mode = rk808_dcdc_get_mode,
	.set_mode = rk808_dcdc_set_mode,
	.set_suspend_voltage = rk808_dcdc_set_sleep_voltage,
	.set_voltage_time_sel = rk808_dcdc_set_voltage_time_sel,
};
static struct regulator_desc regulators[] = {

        {
		.name = "RK_DCDC1",
		.id = 0,
		.ops = &rk808_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK_DCDC2",
		.id = 1,
		.ops = &rk808_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK_DCDC3",
		.id = 2,
		.ops = &rk808_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck4_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK_DCDC4",
		.id = 3,
		.ops = &rk808_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck4_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},

	{
		.name = "RK_LDO1",
		.id =4,
		.ops = &rk808_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK_LDO2",
		.id = 5,
		.ops = &rk808_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK_LDO3",
		.id = 6,
		.ops = &rk808_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo3_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK_LDO4",
		.id = 7,
		.ops = &rk808_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},

	{
		.name = "RK_LDO5",
		.id =8,
		.ops = &rk808_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK_LDO6",
		.id = 9,
		.ops = &rk808_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo6_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK_LDO7",
		.id = 10,
		.ops = &rk808_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo6_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "RK_LDO8",
		.id = 11,
		.ops = &rk808_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	
};

/*
 *
 */
 int rk808_i2c_read(struct rk808 *rk808, char reg, int count,u8 *dest)
{
	struct i2c_client *i2c = rk808->i2c;

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
    msgs[1].scl_rate = 200*1000;

    ret = i2c_transfer(adap, msgs, 2);

	DBG("***run in %s %x  % x\n",__FUNCTION__,i2c->addr,msgs[0].buf);
    return 0;
}

int rk808_i2c_write(struct rk808 *rk808, char reg, int count,  const u8 src)
{
	int ret=-1;
	struct i2c_client *i2c = rk808->i2c;
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

u8 rk808_reg_read(struct rk808 *rk808, u8 reg)
{
	u8 val = 0;

	mutex_lock(&rk808->io_lock);

	rk808_i2c_read(rk808, reg, 1, &val);

	DBG("reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)val&0xff);

	mutex_unlock(&rk808->io_lock);

	return val & 0xff;	
}
EXPORT_SYMBOL_GPL(rk808_reg_read);

int rk808_reg_write(struct rk808 *rk808, u8 reg, u8 val)
{
	int err =0;

	mutex_lock(&rk808->io_lock);

	err = rk808_i2c_write(rk808, reg, 1,val);
	if (err < 0)
		dev_err(rk808->dev, "Write for reg 0x%x failed\n", reg);

	mutex_unlock(&rk808->io_lock);
	return err;
}
EXPORT_SYMBOL_GPL(rk808_reg_write);

 int rk808_set_bits(struct rk808 *rk808, u8 reg, u8 mask, u8 val)
{
	u8 tmp;
	int ret;

	mutex_lock(&rk808->io_lock);

	ret = rk808_i2c_read(rk808, reg, 1, &tmp);
	DBG("1 reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)tmp&0xff);
	tmp = (tmp & ~mask) | val;
	if (ret == 0) {
		ret = rk808_i2c_write(rk808, reg, 1, tmp);
		DBG("reg write 0x%02x -> 0x%02x\n", (int)reg, (unsigned)val&0xff);
	}
	rk808_i2c_read(rk808, reg, 1, &tmp);
	DBG("2 reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)tmp&0xff);
	mutex_unlock(&rk808->io_lock);

	return 0;//ret;	
}
EXPORT_SYMBOL_GPL(rk808_set_bits);

int rk808_clear_bits(struct rk808 *rk808, u8 reg, u8 mask)
{
	u8 data;
	int err;

	mutex_lock(&rk808->io_lock);
	err = rk808_i2c_read(rk808, reg, 1, &data);
	if (err <0) {
		dev_err(rk808->dev, "read from reg %x failed\n", reg);
		goto out;
	}

	data &= ~mask;
	err = rk808_i2c_write(rk808, reg, 1, data);
	if (err <0)
		dev_err(rk808->dev, "write to reg %x failed\n", reg);

out:
	mutex_unlock(&rk808->io_lock);
	return err;
}
EXPORT_SYMBOL_GPL(rk808_clear_bits);

int rk808_bulk_read(struct rk808 *rk808, u8 reg,
		     int count, u8 *buf)
{
	int ret;
                    
#if defined(CONFIG_MFD_RK610)    
	int i;             //Solve communication conflict when rk610 and rk808 on the same i2c 

	mutex_lock(&rk808->io_lock);
	for(i=0; i<count; i++){
		ret = rk808_reg_read(rk808, reg+i);
		if(ret < 0){
			printk("%s: failed read reg 0x%0x, ret = %d\n", __FUNCTION__, reg+i, ret);
			mutex_unlock(&rk808->io_lock);
			return ret;
		}else{
			buf[i] = ret & 0x000000FF;
		}
	}
	mutex_unlock(&rk808->io_lock);
#else
	mutex_lock(&rk808->io_lock);
	
	ret = rk808->read(rk808, reg, count, buf);

	mutex_unlock(&rk808->io_lock);
#endif
	return 0;

}
EXPORT_SYMBOL_GPL(rk808_bulk_read);

int rk808_bulk_write(struct rk808 *rk808, u8 reg,
		     int count, u8 *buf)
{
	int ret;
	
#if defined(CONFIG_MFD_RK610)    
	int i;       // //Solve communication conflict when rk610 and 808 on the same i2c 

	mutex_lock(&rk808->io_lock);
	for(i=0; i<count; i++){
		ret = rk808_reg_write(rk808, reg+i, buf[i]);
		if(ret < 0){
			printk("%s: failed write reg=0x%0x, val=0x%0x, ret = %d\n", __FUNCTION__, reg+i, buf[i], ret);
			mutex_unlock(&rk808->io_lock);
			return ret;
		}
	}
	mutex_unlock(&rk808->io_lock);
#else
	mutex_lock(&rk808->io_lock);
	
	ret = rk808->write(rk808, reg, count, buf);

	mutex_unlock(&rk808->io_lock);
#endif
	return 0;

}
EXPORT_SYMBOL_GPL(rk808_bulk_write);


#if 1
static ssize_t rk808_test_store(struct kobject *kobj, struct kobj_attribute *attr,
                                const char *buf, size_t n)
{
    u32 getdata[8];
    u16 regAddr;
    u8 data;
    char cmd;
    const char *buftmp = buf;
    struct rk808 *rk808 = g_rk808;
    /**
     * W Addr(8Bit) regAddr(8Bit) data0(8Bit) data1(8Bit) data2(8Bit) data3(8Bit)
     * 		:data can be less than 4 byte
     * R regAddr(8Bit)
     * C gpio_name(poweron/powerhold/sleep/boot0/boot1) value(H/L)
     */
        regAddr = (u16)(getdata[0] & 0xff);
	 if (strncmp(buf, "start", 5) == 0) {
        

    } else if (strncmp(buf, "stop", 4== 0) ){
    
    } else{
        sscanf(buftmp, "%c ", &cmd);
        printk("------zhangqing: get cmd = %c\n", cmd);
        switch(cmd) {

        case 'w':
		sscanf(buftmp, "%c %x %x ", &cmd, &getdata[0],&getdata[1]);
		 regAddr = (u16)(getdata[0] & 0xff);
                data = (u8)(getdata[1] & 0xff);
                printk("get value = %x\n", data);

             rk808_i2c_write(rk808, regAddr, 1, data);
	     rk808_i2c_read(rk808, regAddr, 1, &data);
	     printk("%x   %x\n", getdata[1],data);

            break;

        case 'r':
            sscanf(buftmp, "%c %x ", &cmd, &getdata[0]);
            printk("CMD : %c %x\n", cmd, getdata[0]);

            regAddr = (u16)(getdata[0] & 0xff);
            rk808_i2c_read(rk808, regAddr, 1, &data);
		printk("%x %x\n", getdata[0],data);

            break;

        default:
            printk("Unknown command\n");
            break;
        }
}
    return n;

}
static ssize_t rk808_test_show(struct kobject *kobj, struct kobj_attribute *attr,
                               char *buf)
{
   char *s = buf;
    buf = "hello";
    return sprintf(s, "%s\n", buf);

}

static struct kobject *rk808_kobj;
struct rk808_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct kobj_attribute *attr,
			const char *buf, size_t n);
};

static struct rk808_attribute rk808_attrs[] = {
	/*     node_name	permision		show_func	store_func */
	__ATTR(rk808_test,	S_IRUGO | S_IWUSR,	rk808_test_show,	rk808_test_store),
};
#endif

static int __devinit setup_regulators(struct rk808 *rk808, struct rk808_platform_data *pdata)
{	
	int i, err;

	rk808->num_regulators = pdata->num_regulators;
	rk808->rdev = kcalloc(pdata->num_regulators,
			       sizeof(struct regulator_dev *), GFP_KERNEL);
	if (!rk808->rdev) {
		return -ENOMEM;
	}
	/* Instantiate the regulators */
	for (i = 0; i < pdata->num_regulators; i++) {
		int id = pdata->regulators[i].id;
		rk808->rdev[i] = regulator_register(&regulators[id],
			rk808->dev, pdata->regulators[i].initdata, rk808);
/*
		if (IS_ERR(rk808->rdev[i])) {
			err = PTR_ERR(rk808->rdev[i]);
			dev_err(rk808->dev, "regulator init failed: %d\n",
				err);
			goto error;
		}*/
	}

	return 0;
error:
	while (--i >= 0)
		regulator_unregister(rk808->rdev[i]);
	kfree(rk808->rdev);
	rk808->rdev = NULL;
	return err;
}

extern void rk28_send_wakeup_key(void);
static irqreturn_t rk808_vbat_lo_irq(int irq, void *data)
{
        printk("rk808 vbat low %s:irq=%d\n",__func__,irq);
	rk808_set_bits(g_rk808,0x4c,(0x1 << 1),(0x1 <<1));
	rk28_send_wakeup_key();
        return IRQ_HANDLED;
}

int rk808_device_shutdown(void)
{
	int ret;
	int err = -1;
	struct rk808 *rk808 = g_rk808;
	
	printk("%s\n",__func__);

	ret = rk808_reg_read(rk808,RK808_DEVCTRL_REG);
	ret = rk808_set_bits(rk808, RK808_DEVCTRL_REG,(0x1<<3),(0x1<<3));
//	ret = rk808_set_bits(rk808, RK808_DEVCTRL_REG,(0x1<<4),(0x1<<4));
	if (ret < 0) {
		printk("rk808 power off error!\n");
		return err;
	}
	return 0;	
}
EXPORT_SYMBOL_GPL(rk808_device_shutdown);

__weak void  rk808_device_suspend(void) {}
__weak void  rk808_device_resume(void) {}
#ifdef CONFIG_PM
static int rk808_suspend(struct i2c_client *i2c, pm_message_t mesg)
{		
	rk808_device_suspend();
	return 0;
}

static int rk808_resume(struct i2c_client *i2c)
{
	rk808_device_resume();
	return 0;
}
#else
static int rk808_suspend(struct i2c_client *i2c, pm_message_t mesg)
{		
	return 0;
}

static int rk808_resume(struct i2c_client *i2c)
{
	return 0;
}
#endif


#ifdef CONFIG_HAS_EARLYSUSPEND
__weak void rk808_early_suspend(struct early_suspend *h) {}
__weak void rk808_late_resume(struct early_suspend *h) {}
#endif

 
static int __devinit rk808_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct rk808 *rk808;	
	struct rk808_platform_data *pdata = i2c->dev.platform_data;
	int ret;
	
	printk("%s,line=%d\n", __func__,__LINE__);

	rk808 = kzalloc(sizeof(struct rk808), GFP_KERNEL);
	if (rk808 == NULL) {
		ret = -ENOMEM;		
		goto err;
	}
	rk808->i2c = i2c;
	rk808->dev = &i2c->dev;
	i2c_set_clientdata(i2c, rk808);
	rk808->read = rk808_i2c_read;
	rk808->write = rk808_i2c_write;
	mutex_init(&rk808->io_lock);	

	ret = mfd_add_devices(rk808->dev, -1,
			      rk808s, ARRAY_SIZE(rk808s),
			      NULL, 0);
	if (ret < 0)
		goto err;

     
	ret = rk808_reg_read(rk808,0x2f);
	if ((ret < 0) || (ret == 0xff)){
		printk("The device is not rk808\n");
		return 0;
	}


	if (pdata) {
		ret = setup_regulators(rk808, pdata);
		if (ret < 0)		
			goto err;
	} else
		dev_warn(rk808->dev, "No platform init data supplied\n");

	pdata->pre_init(rk808);

	ret = rk808_irq_init(rk808, pdata->irq, pdata);
	if (ret < 0)
		goto err;
	/********************vbat low int**************/
	 ret = request_threaded_irq(rk808->irq_base + RK808_IRQ_VB_LO, NULL, rk808_vbat_lo_irq,
                                   IRQF_TRIGGER_RISING, "rk808_vbatlow",
                                   rk808);
        if (ret != 0) {
                dev_err(rk808->dev, "Failed to request periodic IRQ %d: %d\n",
                        rk808->irq_base + RK808_IRQ_VB_LO, ret);

        }

	/*********************************************/
	g_rk808 = rk808;
	pdata->set_init(rk808);

	#ifdef CONFIG_HAS_EARLYSUSPEND
	rk808->rk808_suspend.suspend = rk808_early_suspend,
	rk808->rk808_suspend.resume = rk808_late_resume,
	rk808->rk808_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 1,
	register_early_suspend(&rk808->rk808_suspend);
	#endif

	#if 1
	int i =0;
	rk808_kobj = kobject_create_and_add("rk808", NULL);
	if (!rk808_kobj)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(rk808_attrs); i++) {
		ret = sysfs_create_file(rk808_kobj, &rk808_attrs[i].attr);
		if (ret != 0) {
			printk("create index %d error\n", i);
			return ret;
		}
	}
	#endif
	
	return 0;

err:
	return ret;	

}

static int __devexit rk808_i2c_remove(struct i2c_client *i2c)
{
	struct rk808 *rk808 = i2c_get_clientdata(i2c);
	int i;

	for (i = 0; i < rk808->num_regulators; i++)
		if (rk808->rdev[i])
			regulator_unregister(rk808->rdev[i]);
	kfree(rk808->rdev);
	i2c_set_clientdata(i2c, NULL);
	kfree(rk808);

	return 0;
}

static const struct i2c_device_id rk808_i2c_id[] = {
       { "rk808", 0 },
       { }
};

MODULE_DEVICE_TABLE(i2c, rk808_i2c_id);

static struct i2c_driver rk808_i2c_driver = {
	.driver = {
		.name = "rk808",
		.owner = THIS_MODULE,
	},
	.probe    = rk808_i2c_probe,
	.remove   = __devexit_p(rk808_i2c_remove),
	.id_table = rk808_i2c_id,
	#ifdef CONFIG_PM
	.suspend	= rk808_suspend,
	.resume		= rk808_resume,
	#endif
};

static int __init rk808_module_init(void)
{
	int ret;
	ret = i2c_add_driver(&rk808_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register I2C driver: %d\n", ret);
	return ret;
}
//module_init(rk808_module_init);
//subsys_initcall(rk808_module_init);
//rootfs_initcall(rk808_module_init);
subsys_initcall_sync(rk808_module_init);

static void __exit rk808_module_exit(void)
{
	i2c_del_driver(&rk808_i2c_driver);
}
module_exit(rk808_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhangqing <zhangqing@rock-chips.com>");
MODULE_DESCRIPTION("rk808 PMIC driver");

