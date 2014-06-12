/*
 * Regulator driver for syr82x DCDC chip for rk32xx
 *
 * Copyright (C) 2010, 2011 ROCKCHIP, Inc.

 * Based on syr82x.c that is work by zhangqing<zhangqing@rock-chips.com>
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
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>

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

#define SYR82X_SPEED 200*1000
#define syr82x_NUM_REGULATORS 1

struct syr82x {
	struct device *dev;
	struct mutex io_lock;
	struct i2c_client *i2c;
	int num_regulators;
	struct regulator_dev **rdev;
	int irq_base;
	int chip_irq;
	int sleep_gpio; /* */
	unsigned int dcdc_slp_voltage[3]; /* buckx_voltage in uV */
	bool pmic_sleep;
	struct regmap *regmap;
};

struct syr82x_regulator {
	struct device		*dev;
	struct regulator_desc	*desc;
	struct regulator_dev	*rdev;
};
struct syr82x_board {
	int irq;
	int irq_base;
	struct regulator_init_data *syr82x_init_data[syr82x_NUM_REGULATORS];
	struct device_node *of_node[syr82x_NUM_REGULATORS];
	int sleep_gpio; /* */
	unsigned int dcdc_slp_voltage[3]; /* buckx_voltage in uV */
	bool sleep;
};

struct syr82x_regulator_subdev {
	int id;
	struct regulator_init_data *initdata;
	struct device_node *reg_node;
};

struct syr82x_platform_data {
	int ono;
	int num_regulators;
	struct syr82x_regulator_subdev *regulators;
	
	int sleep_gpio; /* */
	unsigned int dcdc_slp_voltage[3]; /* buckx_voltage in uV */
	bool sleep;
};
struct syr82x *g_syr82x;

static int syr82x_reg_read(struct syr82x *syr82x, u8 reg);
static int syr82x_set_bits(struct syr82x *syr82x, u8 reg, u16 mask, u16 val);


#define SYR82X_BUCK1_SET_VOL_BASE 0x00
#define SYR82X_BUCK1_SLP_VOL_BASE 0x01
#define SYR82X_CONTR_REG1 0x02
#define SYR82X_ID1_REG 0x03
#define SYR82X_ID2_REG 0x04
#define SYR82X_CONTR_REG2 0x05

#define BUCK_VOL_MASK 0x3f
#define VOL_MIN_IDX 0x00
#define VOL_MAX_IDX 0x3f

const static int buck_voltage_map[] = {
	 712500, 725000, 737500,750000, 762500,775000,787500,800000,
	 812500, 825000, 837500,850000, 862500,875000,887500,900000,
	 912500, 925000, 937500,950000, 962500,975000,987500,1000000,
	 1012500, 1025000, 1037500,1050000, 1062500,1075000,1087500,1100000,
	 1112500, 1125000, 1137500,1150000, 1162500,1175000,1187500,1200000,
	 1212500, 1225000, 1237500,1250000, 1262500,1275000,1287500,1300000,
	 1312500, 1325000, 1337500,1350000, 1362500,1375000,1387500,1400000,
	 1412500, 1425000, 1437500,1450000, 1462500,1475000,1487500,1500000,
};

static int syr82x_dcdc_list_voltage(struct regulator_dev *dev, unsigned index)
{
	if (index >= ARRAY_SIZE(buck_voltage_map))
		return -EINVAL;
	return  buck_voltage_map[index];
}
static int syr82x_dcdc_is_enabled(struct regulator_dev *dev)
{
	struct syr82x *syr82x = rdev_get_drvdata(dev);
	u16 val;
	u16 mask=0x80;	
	val = syr82x_reg_read(syr82x, SYR82X_BUCK1_SET_VOL_BASE);
	if (val < 0)
		return val;
	 val=val&~0x7f;
	if (val & mask)
		return 1;
	else
		return 0; 	
}
static int syr82x_dcdc_enable(struct regulator_dev *dev)
{
	struct syr82x *syr82x = rdev_get_drvdata(dev);
	u16 mask=0x80;	

	return syr82x_set_bits(syr82x, SYR82X_BUCK1_SET_VOL_BASE, mask, 0x80);

}
static int syr82x_dcdc_disable(struct regulator_dev *dev)
{
	struct syr82x *syr82x = rdev_get_drvdata(dev);
	u16 mask=0x80;
	 return syr82x_set_bits(syr82x, SYR82X_BUCK1_SET_VOL_BASE, mask, 0);
}
static int syr82x_dcdc_get_voltage(struct regulator_dev *dev)
{
	struct syr82x *syr82x = rdev_get_drvdata(dev);
	u16 reg = 0;
	int val;
	reg = syr82x_reg_read(syr82x,SYR82X_BUCK1_SET_VOL_BASE);
	reg &= BUCK_VOL_MASK;
	val = buck_voltage_map[reg];	
	return val;
}
static int syr82x_dcdc_set_voltage(struct regulator_dev *dev,
				  int min_uV, int max_uV,unsigned *selector)
{
	struct syr82x *syr82x = rdev_get_drvdata(dev);
	const int *vol_map = buck_voltage_map;
	u16 val;
	int ret = 0;
	
	if (min_uV < vol_map[VOL_MIN_IDX] ||
	    min_uV > vol_map[VOL_MAX_IDX])
		return -EINVAL;

	for (val = VOL_MIN_IDX; val <= VOL_MAX_IDX; val++){
		if (vol_map[val] >= min_uV)
			break;
        }

	if (vol_map[val] > max_uV)
		printk("WARNING:this voltage is not support!voltage set is %d mv\n",vol_map[val]);

	ret = syr82x_set_bits(syr82x, SYR82X_BUCK1_SET_VOL_BASE ,BUCK_VOL_MASK, val);
	if(ret < 0)
		printk("###################WARNING:set voltage is error!voltage set is %d mv %d\n",vol_map[val],ret);
	
	return ret;
}

static unsigned int syr82x_dcdc_get_mode(struct regulator_dev *dev)
{
	struct syr82x *syr82x = rdev_get_drvdata(dev);
	u16 mask = 0x40;
	u16 val;
	val = syr82x_reg_read(syr82x, SYR82X_BUCK1_SET_VOL_BASE);
        if (val < 0) {
                return val;
        }
	val=val & mask;
	if (val== mask)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;

}
static int syr82x_dcdc_set_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct syr82x *syr82x = rdev_get_drvdata(dev);
	u16 mask = 0x40;

	switch(mode)
	{
	case REGULATOR_MODE_FAST:
		return syr82x_set_bits(syr82x,SYR82X_BUCK1_SET_VOL_BASE, mask, mask);
	case REGULATOR_MODE_NORMAL:
		return syr82x_set_bits(syr82x, SYR82X_BUCK1_SET_VOL_BASE, mask, 0);
	default:
		printk("error:dcdc_syr82x only auto and pwm mode\n");
		return -EINVAL;
	}
}
static int syr82x_dcdc_set_voltage_time_sel(struct regulator_dev *dev,   unsigned int old_selector,
				     unsigned int new_selector)
{
	int old_volt, new_volt;
	
	old_volt = syr82x_dcdc_list_voltage(dev, old_selector);
	if (old_volt < 0)
		return old_volt;
	
	new_volt = syr82x_dcdc_list_voltage(dev, new_selector);
	if (new_volt < 0)
		return new_volt;

	return DIV_ROUND_UP(abs(old_volt - new_volt)*4, 10000);
}
static int syr82x_dcdc_suspend_enable(struct regulator_dev *dev)
{
	struct syr82x *syr82x = rdev_get_drvdata(dev);
	u16 mask=0x80;	
	
	return syr82x_set_bits(syr82x, SYR82X_BUCK1_SLP_VOL_BASE, mask, 0x80);

}
static int syr82x_dcdc_suspend_disable(struct regulator_dev *dev)
{
	struct syr82x *syr82x = rdev_get_drvdata(dev);
	u16 mask=0x80;
	 return syr82x_set_bits(syr82x, SYR82X_BUCK1_SLP_VOL_BASE, mask, 0);
}
static int syr82x_dcdc_set_sleep_voltage(struct regulator_dev *dev,
					    int uV)
{
	struct syr82x *syr82x = rdev_get_drvdata(dev);
	const int *vol_map = buck_voltage_map;
	u16 val;
	int ret = 0;
	
	if (uV < vol_map[VOL_MIN_IDX] ||
	    uV > vol_map[VOL_MAX_IDX])
		return -EINVAL;

	for (val = VOL_MIN_IDX; val <= VOL_MAX_IDX; val++){
		if (vol_map[val] >= uV)
			break;
        }

	if (vol_map[val] > uV)
		printk("WARNING:this voltage is not support!voltage set is %d mv\n",vol_map[val]);
	ret = syr82x_set_bits(syr82x,SYR82X_BUCK1_SLP_VOL_BASE ,BUCK_VOL_MASK, val);	
	return ret;
}


static int syr82x_dcdc_set_suspend_mode(struct regulator_dev *dev, unsigned int mode)
{
	struct syr82x *syr82x = rdev_get_drvdata(dev);
	u16 mask = 0x40;

	switch(mode)
	{
	case REGULATOR_MODE_FAST:
		return syr82x_set_bits(syr82x,SYR82X_BUCK1_SLP_VOL_BASE, mask, mask);
	case REGULATOR_MODE_NORMAL:
		return syr82x_set_bits(syr82x, SYR82X_BUCK1_SLP_VOL_BASE, mask, 0);
	default:
		printk("error:dcdc_syr82x only auto and pwm mode\n");
		return -EINVAL;
	}
}

static struct regulator_ops syr82x_dcdc_ops = { 
	.set_voltage = syr82x_dcdc_set_voltage,
	.get_voltage = syr82x_dcdc_get_voltage,
	.list_voltage= syr82x_dcdc_list_voltage,
	.is_enabled = syr82x_dcdc_is_enabled,
	.enable = syr82x_dcdc_enable,
	.disable = syr82x_dcdc_disable,
	.get_mode = syr82x_dcdc_get_mode,
	.set_mode = syr82x_dcdc_set_mode,
	.set_suspend_voltage = syr82x_dcdc_set_sleep_voltage,
	.set_suspend_enable = syr82x_dcdc_suspend_enable,
	.set_suspend_disable = syr82x_dcdc_suspend_disable,
	.set_suspend_mode = syr82x_dcdc_set_suspend_mode,
	.set_voltage_time_sel = syr82x_dcdc_set_voltage_time_sel,
};
static struct regulator_desc regulators[] = {

        {
		.name = "SY_DCDC1",
		.id = 0,
		.ops = &syr82x_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
};

static int syr82x_i2c_read(struct i2c_client *i2c, char reg, int count,	u16 *dest)
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
    msgs[0].scl_rate = SYR82X_SPEED;
    
    msgs[1].buf = (u8 *)dest;
    msgs[1].addr = i2c->addr;
    msgs[1].flags = i2c->flags | I2C_M_RD;
    msgs[1].len = 1;
    msgs[1].scl_rate = SYR82X_SPEED;
    ret = i2c_transfer(adap, msgs, 2);

	DBG("***run in %s %d msgs[1].buf = %d\n",__FUNCTION__,__LINE__,*(msgs[1].buf));

	return ret;   
}

static int syr82x_i2c_write(struct i2c_client *i2c, char reg, int count, const u16 src)
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
	msg.scl_rate = SYR82X_SPEED;	

	ret = i2c_transfer(adap, &msg, 1);
	return ret;	
}

static int syr82x_reg_read(struct syr82x *syr82x, u8 reg)
{
	u16 val = 0;
	int ret;

	mutex_lock(&syr82x->io_lock);

	ret = syr82x_i2c_read(syr82x->i2c, reg, 1, &val);

	DBG("reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)val&0xff);
	if (ret < 0){
		mutex_unlock(&syr82x->io_lock);
		return ret;
	}
	mutex_unlock(&syr82x->io_lock);

	return val & 0xff;	
}

static int syr82x_set_bits(struct syr82x *syr82x, u8 reg, u16 mask, u16 val)
{
	u16 tmp;
	int ret;

	mutex_lock(&syr82x->io_lock);

	ret = syr82x_i2c_read(syr82x->i2c, reg, 1, &tmp);
	DBG("1 reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)tmp&0xff);
	if (ret < 0){
                mutex_unlock(&syr82x->io_lock);
                return ret;
        }
	tmp = (tmp & ~mask) | val;
	ret = syr82x_i2c_write(syr82x->i2c, reg, 1, tmp);
	DBG("reg write 0x%02x -> 0x%02x\n", (int)reg, (unsigned)val&0xff);
	if (ret < 0){
                mutex_unlock(&syr82x->io_lock);
                return ret;
        }
	ret = syr82x_i2c_read(syr82x->i2c, reg, 1, &tmp);
	DBG("2 reg read 0x%02x -> 0x%02x\n", (int)reg, (unsigned)tmp&0xff);
	if (ret < 0){
                mutex_unlock(&syr82x->io_lock);
                return ret;
        }
	mutex_unlock(&syr82x->io_lock);

	return 0;//ret;	
}

#ifdef CONFIG_OF
static struct of_device_id syr82x_of_match[] = {
	{ .compatible = "silergy,syr82x"},
	{ },
};
MODULE_DEVICE_TABLE(of, syr82x_of_match);
#endif
#ifdef CONFIG_OF
static struct of_regulator_match syr82x_reg_matches[] = {
	{ .name = "syr82x_dcdc1" ,.driver_data = (void *)0},
};

static struct syr82x_board *syr82x_parse_dt(struct syr82x *syr82x)
{
	struct syr82x_board *pdata;
	struct device_node *regs;
	struct device_node *syr82x_np;
	int count;
	DBG("%s,line=%d\n", __func__,__LINE__);	
	
	syr82x_np = of_node_get(syr82x->dev->of_node);
	if (!syr82x_np) {
		printk("could not find pmic sub-node\n");
		return NULL;
	}
	
	regs = of_find_node_by_name(syr82x_np, "regulators");
	if (!regs)
		return NULL;
	count = of_regulator_match(syr82x->dev, regs, syr82x_reg_matches,
				   syr82x_NUM_REGULATORS);
	of_node_put(regs);
	pdata = devm_kzalloc(syr82x->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;
	pdata->syr82x_init_data[0] = syr82x_reg_matches[0].init_data;
	pdata->of_node[0] = syr82x_reg_matches[0].of_node;
	
	return pdata;
}

#else
static struct syr82x_board *syr82x_parse_dt(struct i2c_client *i2c)
{
	return NULL;
}
#endif

static int syr82x_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct syr82x *syr82x;	
	struct syr82x_board *pdev ;
	const struct of_device_id *match;
	struct regulator_config config = { };
	struct regulator_dev *sy_rdev;
	struct regulator_init_data *reg_data;
	const char *rail_name = NULL;
	int ret;
	
	DBG("%s,line=%d\n", __func__,__LINE__);	

	if (i2c->dev.of_node) {
		match = of_match_device(syr82x_of_match, &i2c->dev);
		if (!match) {
			printk("Failed to find matching dt id\n");
			return -EINVAL;
		}
	}

	syr82x = devm_kzalloc(&i2c->dev,sizeof(struct syr82x), GFP_KERNEL);
	if (syr82x == NULL) {
		ret = -ENOMEM;		
		goto err;
	}
	syr82x->i2c = i2c;
	syr82x->dev = &i2c->dev;
	i2c_set_clientdata(i2c, syr82x);
	g_syr82x = syr82x;
		
	mutex_init(&syr82x->io_lock);	

	ret = syr82x_reg_read(syr82x,SYR82X_ID1_REG);
	if ((ret <0) ||(ret ==0xff) ||(ret ==0)){
		printk("The device is not syr82x %x \n",ret);
		goto err;
	}
	
	ret = syr82x_set_bits(syr82x,SYR82X_CONTR_REG1,(1 << 6),(1<<6));  //10mv/2.4us

	if (syr82x->dev->of_node)
		pdev = syr82x_parse_dt(syr82x);
	
	if (pdev) {
		syr82x->num_regulators = syr82x_NUM_REGULATORS;
		syr82x->rdev = kcalloc(syr82x_NUM_REGULATORS,sizeof(struct regulator_dev *), GFP_KERNEL);
		if (!syr82x->rdev) {
			return -ENOMEM;
		}
		/* Instantiate the regulators */
		reg_data = pdev->syr82x_init_data[0];
		config.dev = syr82x->dev;
		config.driver_data = syr82x;
		if (syr82x->dev->of_node)
			config.of_node = pdev->of_node[0];
			if (reg_data && reg_data->constraints.name)
				rail_name = reg_data->constraints.name;
			else
				rail_name = regulators[0].name;
			reg_data->supply_regulator = rail_name;
	
		config.init_data =reg_data;
		sy_rdev = regulator_register(&regulators[0],&config);
		if (IS_ERR(sy_rdev)) {
			printk("failed to register regulator\n");
		goto err;
		}
		syr82x->rdev[0] = sy_rdev;
	}
	return 0;
err:
	return ret;	

}

static int  syr82x_i2c_remove(struct i2c_client *i2c)
{
	struct syr82x *syr82x = i2c_get_clientdata(i2c);

	if (syr82x->rdev[0])
		regulator_unregister(syr82x->rdev[0]);
	i2c_set_clientdata(i2c, NULL);
	return 0;
}

static const struct i2c_device_id syr82x_i2c_id[] = {
       { "syr82x", 0 },
       { }
};

MODULE_DEVICE_TABLE(i2c, syr82x_i2c_id);

static struct i2c_driver syr82x_i2c_driver = {
	.driver = {
		.name = "syr82x",
		.owner = THIS_MODULE,
		.of_match_table =of_match_ptr(syr82x_of_match),
	},
	.probe    = syr82x_i2c_probe,
	.remove   = syr82x_i2c_remove,
	.id_table = syr82x_i2c_id,
};

static int __init syr82x_module_init(void)
{
	int ret;
	ret = i2c_add_driver(&syr82x_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register I2C driver: %d\n", ret);
	return ret;
}
subsys_initcall_sync(syr82x_module_init);

static void __exit syr82x_module_exit(void)
{
	i2c_del_driver(&syr82x_i2c_driver);
}
module_exit(syr82x_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhangqing <zhangqing@rock-chips.com>");
MODULE_DESCRIPTION("syr82x PMIC driver");

