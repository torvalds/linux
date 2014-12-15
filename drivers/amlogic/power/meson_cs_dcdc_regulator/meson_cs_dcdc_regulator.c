/*
 * meson_cs_dcdc_regulator.c
 *
 * Support for Meson current source DCDC voltage regulator
 *
 * Copyright (C) 2012 Elvis Yu <elvis.yu@amlogic.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <mach/am_regs.h>
#include <linux/amlogic/meson_cs_dcdc_regulator.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>

static struct meson_cs_pdata_t* g_vcck_voltage = NULL;
static unsigned int *vcck_pwm_table;


static int set_voltage(int from, int to)
{
	if(to<0 || to>MESON_CS_MAX_STEPS)
	{
		printk(KERN_ERR "%s: to(%d) out of range!\n", __FUNCTION__, to);
		return -EINVAL;
	}
	if(from<0 || from>MESON_CS_MAX_STEPS)
	{
		if(g_vcck_voltage && (g_vcck_voltage->set_voltage))
			g_vcck_voltage->set_voltage(to);
		else
			aml_set_reg32_bits(P_VGHL_PWM_REG0, to, 0, 4);
		udelay(200);

	}
	else if(to < from)
	{
		// going to higher voltage
		// lower index is higher voltage.
		if (from - to > 3) {
			if(g_vcck_voltage && (g_vcck_voltage->set_voltage))
				g_vcck_voltage->set_voltage(to+3);
			else
				aml_set_reg32_bits(P_VGHL_PWM_REG0, to + 3, 0, 4);
			udelay(100);
		}
		if (from - to > 1) {
			if(g_vcck_voltage && (g_vcck_voltage->set_voltage))
				g_vcck_voltage->set_voltage(to+1);
			else
				aml_set_reg32_bits(P_VGHL_PWM_REG0, to + 1, 0, 4);
			udelay(100);
		}
		if(g_vcck_voltage && (g_vcck_voltage->set_voltage))
			g_vcck_voltage->set_voltage(to);
		else
			aml_set_reg32_bits(P_VGHL_PWM_REG0, to, 0, 4);
		udelay(100);
	}
	else if(to > from)
	{
		// going to lower voltage
		if (to - from > 3) {
			if(g_vcck_voltage && (g_vcck_voltage->set_voltage))
				g_vcck_voltage->set_voltage(to-3);
			else
				aml_set_reg32_bits(P_VGHL_PWM_REG0, to - 3, 0, 4);
			udelay(100);
		}
		if (to - from > 1) {
			if(g_vcck_voltage && (g_vcck_voltage->set_voltage))
				g_vcck_voltage->set_voltage(to-1);
			else
				aml_set_reg32_bits(P_VGHL_PWM_REG0, to - 1, 0, 4);
			udelay(100);
		}
		if(g_vcck_voltage && (g_vcck_voltage->set_voltage))
			g_vcck_voltage->set_voltage(to);
		else
			aml_set_reg32_bits(P_VGHL_PWM_REG0, to, 0, 4);
		// don't need to delay when going down since freq will
		// already be lowered
	}
	return 0;
}


static int meson_cs_dcdc_get_voltage(struct regulator_dev *dev)
{
	struct meson_cs_regulator_dev *meson_cs_regulator = rdev_get_drvdata(dev);
	u32 reg_val;
	int data;
	mutex_lock(&meson_cs_regulator->io_lock);

	if(g_vcck_voltage && (g_vcck_voltage->get_voltage)) {
		reg_val = g_vcck_voltage->get_voltage();
		if(reg_val < 0) {
			data = -1;
			goto out;
		}
	}
	else {
		reg_val = aml_read_reg32(P_VGHL_PWM_REG0);

		if ((reg_val>>12&3) != 1) {
			dev_err(&dev->dev, "Error getting voltage\n");
			data = -1;
			goto out;
		}

	}
	/* Convert the data from table & step to microvolts */
	data = meson_cs_regulator->voltage_step_table[reg_val & 0xf];

out:
	mutex_unlock(&meson_cs_regulator->io_lock);
	return data;
}

static int meson_cs_dcdc_set_voltage(struct regulator_dev *dev,
				int minuV, int maxuV,
				unsigned *selector)
{
	struct meson_cs_regulator_dev *meson_cs_regulator = rdev_get_drvdata(dev);
	int cur_idx, last_idx;
	
	if (minuV < meson_cs_regulator->voltage_step_table[MESON_CS_MAX_STEPS-1] || minuV > meson_cs_regulator->voltage_step_table[0])
		return -EINVAL;
	if (maxuV < meson_cs_regulator->voltage_step_table[MESON_CS_MAX_STEPS-1] || maxuV > meson_cs_regulator->voltage_step_table[0])
		return -EINVAL;

	for(last_idx=0; last_idx<MESON_CS_MAX_STEPS; last_idx++)
	{
		if(meson_cs_regulator->cur_uV >= meson_cs_regulator->voltage_step_table[last_idx])
		{
			break;
		}
	}

	for(cur_idx=0; cur_idx<MESON_CS_MAX_STEPS; cur_idx++)
	{
		if(minuV >= meson_cs_regulator->voltage_step_table[cur_idx])
		{
			break;
		}
	}

	*selector = cur_idx;
	
	if(meson_cs_regulator->voltage_step_table[cur_idx] != minuV)
	{
		printk("set voltage to %d; selector=%d\n", meson_cs_regulator->voltage_step_table[cur_idx], cur_idx);
	}
	mutex_lock(&meson_cs_regulator->io_lock);

	set_voltage(last_idx, cur_idx);

	meson_cs_regulator->cur_uV = meson_cs_regulator->voltage_step_table[cur_idx];
	mutex_unlock(&meson_cs_regulator->io_lock);
	return 0;
}

static int meson_cs_dcdc_list_voltage(struct regulator_dev *dev, unsigned selector)
{
	struct meson_cs_regulator_dev *meson_cs_regulator = rdev_get_drvdata(dev);
	return meson_cs_regulator->voltage_step_table[selector];
}

static struct regulator_ops meson_cs_ops = {
	.get_voltage	= meson_cs_dcdc_get_voltage,
	.set_voltage	= meson_cs_dcdc_set_voltage,
	.list_voltage	= meson_cs_dcdc_list_voltage,
};


static void update_voltage_constraints(struct meson_cs_regulator_dev *data)
{
	int ret;

	if (data->min_uV && data->max_uV
	    && data->min_uV <= data->max_uV) {
		ret = regulator_set_voltage(data->regulator,
					    data->min_uV, data->max_uV);
		if (ret != 0) {
			printk(KERN_ERR "regulator_set_voltage() failed: %d\n",
			       ret);
			return;
		}
	}
}

static ssize_t show_min_uV(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct meson_cs_regulator_dev *data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", data->min_uV);
}

static ssize_t set_min_uV(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct meson_cs_regulator_dev *data = dev_get_drvdata(dev);
	long val;

	if (strict_strtol(buf, 10, &val) != 0)
		return count;

	data->min_uV = val;
	update_voltage_constraints(data);

	return count;
}

static ssize_t show_max_uV(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct meson_cs_regulator_dev *data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", data->max_uV);
}

static ssize_t set_max_uV(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct meson_cs_regulator_dev *data = dev_get_drvdata(dev);
	long val;

	if (strict_strtol(buf, 10, &val) != 0)
		return count;

	data->max_uV = val;
	update_voltage_constraints(data);

	return count;
}

static DEVICE_ATTR(min_microvolts, 0644, show_min_uV, set_min_uV);
static DEVICE_ATTR(max_microvolts, 0644, show_max_uV, set_max_uV);


static struct device_attribute *attributes_virtual[] = {
	&dev_attr_min_microvolts,
	&dev_attr_max_microvolts,
};


static int of_get_voltage(void) {
	//printk("***vcck: get_voltage\n");	 
	int i;    
	unsigned int reg = aml_read_reg32(P_PWM_PWM_C);	  
	for(i=0; i<MESON_CS_MAX_STEPS; i++) { 	   
		if(reg == *(vcck_pwm_table+i)) 	 
			break;    
		}	
    //printk("reg is %d\n",reg);
    //printk("i is %d\n",i);
	if(i >= MESON_CS_MAX_STEPS) 	   
		return -1;	 
	else		  
		return i;
}

static int of_set_voltage(unsigned int level) {
	//printk("***vcck: set_voltage\n");	 
	//printk("level is %d  *(vcck_pwm_table+level) is %d\n",level,*(vcck_pwm_table+level));
	aml_write_reg32(P_PWM_PWM_C, *(vcck_pwm_table+level));
    return 0;
}


int get_dt_vcck_init_data(struct device_node *np, struct regulator_init_data *vcck_init_data)
{
	int ret;
	ret = of_property_read_string(np,"cons_name",&(vcck_init_data->constraints.name));
	if(ret){
			printk("don't find constraints-name\n");
			return 1;
	}

	ret = of_property_read_u32(np,"min_uV",&(vcck_init_data->constraints.min_uV));
	if(ret){
			printk("don't find constraints-min_uV\n");
			return 1;
	}

	ret = of_property_read_u32(np,"max_uV",&(vcck_init_data->constraints.max_uV));
	if(ret){
			printk("don't find constraints-max_uV\n");
			return 1;
	}

	ret = of_property_read_u32(np,"valid_ops_mask",&(vcck_init_data->constraints.valid_ops_mask));
	if(ret){
			printk("don't find constraints-valid_ops_mask\n");
			return 1;
	}

	ret = of_property_read_u32(np,"num",&(vcck_init_data->num_consumer_supplies));
	if(ret){
			printk("don't find num_consumer_supplies\n");
			return 1;
	}

	vcck_init_data->consumer_supplies = kzalloc(sizeof(struct regulator_consumer_supply)*(vcck_init_data->num_consumer_supplies), GFP_KERNEL);
	if(!vcck_init_data->consumer_supplies)
	{
		printk("vcck_init_data->consumer_supplies can not get mem\n");
		return -1;
	}
		
	ret = of_property_read_string(np,"vcck_data-supply",&(vcck_init_data->consumer_supplies->supply));
	if(ret){
			printk("don't find consumer_supplies->supply\n");
			kfree(vcck_init_data->consumer_supplies);
			return 1;
	}

	return 0;
}


static void vcck_pwm_init(struct device * dev) {
	printk("***vcck: vcck_pwm_init\n");    
	//enable pwm clk & pwm output    
	aml_write_reg32(P_PWM_MISC_REG_CD, (aml_read_reg32(P_PWM_MISC_REG_CD) & ~(0x7f << 8)) | ((1 << 15) | (0 << 8) | (1 << 0)));    
	aml_write_reg32(P_PWM_PWM_C, *(vcck_pwm_table+0));    
	//enable pwm_C pinmux    1<<3 pwm_D    

	//pinmux_set(&vcck_pwm_set);    
    if (IS_ERR(devm_pinctrl_get_select_default(dev))) {
		printk("did not get pins for pwm--------\n");
	}
	else
	    printk("get pin for pwm--------\n");
	//aml_write_reg32(P_PERIPHS_PIN_MUX_2, aml_read_reg32(P_PERIPHS_PIN_MUX_2) | (1 << 2));
}

static 
int meson_cs_probe(struct platform_device *pdev)
{
	struct meson_cs_pdata_t *meson_cs_pdata  = pdev->dev.platform_data;
	struct meson_cs_regulator_dev *meson_cs_regulator;
	int error = 0, i, cur_idx;
	struct regulator_config *meson_regulator_config;

#ifdef CONFIG_USE_OF
	struct device_node *np = pdev->dev.of_node;
	struct device_node *np_init_data;
	struct meson_cs_pdata_t *vcck_pdata;
	phandle phandle;
	int val=0;
	int ret;

	if (pdev->dev.of_node) {
		vcck_pdata = kzalloc(sizeof(struct meson_cs_pdata_t), GFP_KERNEL);
		if(!vcck_pdata)
		{
			printk("vcck_pdata can not get mem\n");
			return -1;
		}
			
		ret = of_property_read_u32(np,"default_uV",&(vcck_pdata->default_uV));
		if(ret){
			printk("don't find  match default_uV\n");
			goto err;
		}

		ret = of_property_read_u32_array(np,"voltage_step_table",(u32 *)&(vcck_pdata->voltage_step_table),(MESON_CS_MAX_STEPS)*sizeof(int)/sizeof(&vcck_pdata->voltage_step_table));
		if(ret){
			printk("don't find  match voltage_step_table\n");
			goto err;
		}
			
		ret = of_property_read_u32(np,"init-data",&val);
		if(ret){
			printk("don't find  match init-data\n");
			goto err;
		}
		if(ret==0){
			phandle=val;
			np_init_data = of_find_node_by_phandle(phandle);
			if(!np_init_data){
				printk("%s:%d,can't find device node\n",__func__,__LINE__);
				goto err;
			}

			vcck_pdata->meson_cs_init_data = kzalloc(sizeof(struct regulator_init_data), GFP_KERNEL);
			if(!vcck_pdata->meson_cs_init_data)
			{
				printk("vcck_pdata->meson_cs_init_data can not get mem\n");
				goto err;
			}
			
			ret = get_dt_vcck_init_data(np_init_data,(vcck_pdata->meson_cs_init_data));
			if(ret){
				printk("don't find meson_cs_init_data\n");
				kfree(vcck_pdata->meson_cs_init_data);
				goto err;
			}
		}

		if(of_find_property(pdev->dev.of_node,"vcck_pwm_table",NULL))
		{
			vcck_pwm_table=kzalloc(sizeof(int) * MESON_CS_MAX_STEPS, GFP_KERNEL);
			ret=of_property_read_u32_array(np,"vcck_pwm_table",vcck_pwm_table,(MESON_CS_MAX_STEPS)*sizeof(int)/sizeof(vcck_pwm_table));
			if(ret){
			    printk("don't find  match vcck_pwm_table\n");
			    goto err;
		    }
			printk("*(vcck_pwm_table+15) is %d\n *(vcck_pwm_table+16) is %d\n",*(vcck_pwm_table+15),*(vcck_pwm_table+16));
			vcck_pdata->set_voltage = of_set_voltage;
			vcck_pdata->get_voltage = of_get_voltage;
			
			vcck_pwm_init(&(pdev->dev));
		}
		else
		{
		printk("can not get vcck_pwm_table\n");
			vcck_pdata->set_voltage = NULL;
			vcck_pdata->get_voltage = NULL;
		}
		
		pdev->dev.platform_data = vcck_pdata;	
	}
#endif

	printk("======================enter %s done!\n",__func__);
	       g_vcck_voltage = meson_cs_pdata;
	meson_cs_regulator = kzalloc(sizeof(struct meson_cs_regulator_dev), GFP_KERNEL);
	if (!meson_cs_regulator)
		return -ENOMEM;

	meson_cs_regulator->rdev = NULL;
	meson_cs_regulator->regulator = NULL;
	meson_cs_regulator->voltage_step_table = meson_cs_pdata->voltage_step_table;

	meson_cs_regulator->desc.name = "meson_cs_desc";
	meson_cs_regulator->desc.id = 0;
	meson_cs_regulator->desc.n_voltages = MESON_CS_MAX_STEPS;
	meson_cs_regulator->desc.ops = &meson_cs_ops;
	meson_cs_regulator->desc.type = REGULATOR_VOLTAGE;
	meson_cs_regulator->desc.owner = THIS_MODULE;

	mutex_init(&meson_cs_regulator->io_lock);

	aml_set_reg32_bits(P_VGHL_PWM_REG0, 1, 12, 2);		//Enable
	meson_regulator_config = devm_kzalloc(&pdev->dev, sizeof(*meson_regulator_config), GFP_KERNEL);
	meson_regulator_config->dev=&pdev->dev;
	meson_regulator_config->init_data=meson_cs_pdata->meson_cs_init_data;
	meson_regulator_config->driver_data=meson_cs_regulator;
	meson_regulator_config->of_node=pdev->dev.of_node;
	meson_cs_regulator->rdev = regulator_register(&meson_cs_regulator->desc, meson_regulator_config);
	if (IS_ERR(meson_cs_regulator->rdev)) {
			dev_err(&pdev->dev,
				"failed to register %s regulator\n",
				pdev->name);
			error = PTR_ERR(meson_cs_regulator->rdev);
			goto fail;
	}

	meson_cs_regulator->regulator = regulator_get(NULL,
		meson_cs_pdata->meson_cs_init_data->consumer_supplies->supply);

	if (IS_ERR(meson_cs_regulator->regulator)) {
			dev_err(&pdev->dev,
				"failed to get %s regulator\n",
				pdev->name);
			error = PTR_ERR(meson_cs_regulator->rdev);
			goto fail;
	}

	for (i = 0; i < ARRAY_SIZE(attributes_virtual); i++) {
		error = device_create_file(&pdev->dev, attributes_virtual[i]);
		if (error != 0)
			goto fail;
	}

	platform_set_drvdata(pdev, meson_cs_regulator);		//Elvis

	for(cur_idx=0; cur_idx<MESON_CS_MAX_STEPS; cur_idx++)
	{
		if(meson_cs_pdata->default_uV >= meson_cs_regulator->voltage_step_table[cur_idx])
		{
			break;
		}
	}

	if(set_voltage(0, cur_idx))
	{
		goto fail;
	}

	meson_cs_regulator->cur_uV = meson_cs_regulator->voltage_step_table[cur_idx];

	
	printk("================init %s done!\n",__func__);
	return 0;

err:
	kfree(vcck_pdata);
	return -1;
	
fail:

	if(meson_cs_regulator->rdev)
		regulator_unregister(meson_cs_regulator->rdev);
	
	if(meson_cs_regulator->regulator)
		regulator_put(meson_cs_regulator->regulator);
	
	kfree(meson_cs_regulator);
	return error;
}

static int  meson_cs_remove(struct platform_device *pdev)
{
	struct meson_cs_regulator_dev *meson_cs_regulator = platform_get_drvdata(pdev);
#ifdef CONFIG_USE_OF
	struct meson_cs_pdata_t *meson_cs_pdata;
#endif 
	if(meson_cs_regulator->rdev)
		regulator_unregister(meson_cs_regulator->rdev);
	
	if(meson_cs_regulator->regulator)
		regulator_put(meson_cs_regulator->regulator);

#ifdef CONFIG_USE_OF
	//meson_cs_pdata = container_of(meson_cs_regulator->voltage_step_table,struct meson_cs_pdata_t,voltage_step_table);
	meson_cs_pdata =  (struct meson_cs_pdata_t *)((char *)meson_cs_regulator->voltage_step_table -
												  offsetof(struct meson_cs_pdata_t, voltage_step_table));
	kfree(meson_cs_pdata->meson_cs_init_data->consumer_supplies);
	kfree(meson_cs_pdata->meson_cs_init_data);
	kfree(meson_cs_pdata);
	kfree(meson_cs_regulator);
#endif 
	return 0;
}


#ifdef CONFIG_OF
static const struct of_device_id amlogic_meson_cs_dt_match[]={
	{	.compatible = "amlogic,meson-cs-regulator",
	},
	{},
};
#else
#define amlogic_meson_cs_dt_match NULL
#endif

static struct platform_driver meson_cs_driver = {
	.driver = {
		.name = "meson-cs-regulator",
		.owner = THIS_MODULE,
		.of_match_table = amlogic_meson_cs_dt_match,
	},
	.probe = meson_cs_probe,
	.remove = meson_cs_remove,
};


static int __init meson_cs_init(void)
{
	return platform_driver_register(&meson_cs_driver);
}

static void __exit meson_cs_cleanup(void)
{
	platform_driver_unregister(&meson_cs_driver);
}

subsys_initcall(meson_cs_init);
module_exit(meson_cs_cleanup);

#ifdef CONFIG_AML_DVFS
#include <linux/amlogic/aml_dvfs.h>
#define PWM_A   0
#define PWM_B   1
#define PWM_C   2
#define PWM_D   3

struct cs_voltage {
    int pwm_value;
    int voltage;
};
static struct cs_voltage *g_table = NULL;
static int g_table_cnt = 0;
static int use_pwm = 0;
static int pwm_ctrl = PWM_C; 
static unsigned long pmw_base[] = {
    P_PWM_PWM_A,    
    P_PWM_PWM_B,    
    P_PWM_PWM_C,    
    P_PWM_PWM_D
};

static int dvfs_get_voltage_step(void)
{
    int i = 0;
    unsigned int reg_val;

    if (use_pwm) {
        reg_val = aml_read_reg32(pmw_base[pwm_ctrl]); 
        for (i = 0; i < g_table_cnt; i++) {
            if (g_table[i].pwm_value == reg_val) {
                return i;    
            }
        }
        if (i >= g_table_cnt) {
            return -1;    
        }
    } else {
		reg_val = aml_read_reg32(P_VGHL_PWM_REG0);
		if ((reg_val>>12&3) != 1) {
			return -1;
		}
        return reg_val & 0xf;
    }
	return -1;
}

static int dvfs_set_voltage(int from, int to)
{
    int cur;

	if (to < 0 || to > g_table_cnt) {
		printk(KERN_ERR "%s: to(%d) out of range!\n", __FUNCTION__, to);
		return -EINVAL;
	} 
	if (from < 0 || from > g_table_cnt) {
        if (use_pwm) {
            /*
             * use PMW method to adjust vcck voltage
             */
            aml_write_reg32(pmw_base[pwm_ctrl], g_table[to].pwm_value);
        } else {
            /*
             * use constant-current source to adjust vcck voltage
             */
			aml_set_reg32_bits(P_VGHL_PWM_REG0, to, 0, 4);
        }
		udelay(200);
        return 0;
	}
    cur = from;
    while (cur != to) {
        /*
         * if target step is far away from current step, don't change 
         * voltage by one-step-done. You should change voltage step by
         * step to make sure voltage output is stable
         */
        if (cur < to) {
            if (cur < to - 3) {
                cur += 3;    
            } else {
                cur = to;    
            }
        } else {
            if (cur > to + 3) {
                cur -= 3;    
            } else {
                cur = to;    
            }
        }
        if (use_pwm) {
            aml_write_reg32(pmw_base[pwm_ctrl], g_table[cur].pwm_value);
        } else {
			aml_set_reg32_bits(P_VGHL_PWM_REG0, cur, 0, 4);
        }
        udelay(100);
    }
    return 0;
}

static int meson_cs_set_voltage(uint32_t id, uint32_t min_uV, uint32_t max_uV)
{
    uint32_t vol = 0;
    int      i;
    int      cur;

    if (min_uV > max_uV || !g_table) {
        printk("%s, invalid voltage or NULL table\n", __func__);
        return -1;    
    }   
    vol = (min_uV + max_uV) / 2;
    for (i = 0; i < g_table_cnt; i++) {
        if (g_table[i].voltage >= vol) {
            break;
        }
    }
    if (i == g_table_cnt) {
        printk("%s, voltage is too large:%d\n", __func__, vol);    
        return -EINVAL;
    }

    cur = dvfs_get_voltage_step();
    return dvfs_set_voltage(cur, i);
}

static int meson_cs_get_voltage(uint32_t id, uint32_t *uV)
{
    int cur;

    if (!g_table) {
        printk("%s, no voltage table\n", __func__);
        return -1;
    }
    cur = dvfs_get_voltage_step(); 
    if (cur < 0) {
        return cur;
    } else {
        *uV = g_table[cur].voltage; 
        return 0;
    }
}

struct aml_dvfs_driver aml_cs_dvfs_driver = { 
    .name        = "meson-cs-dvfs",
    .id_mask     = (AML_DVFS_ID_VCCK),
    .set_voltage = meson_cs_set_voltage, 
    .get_voltage = meson_cs_get_voltage,
};

#define DEBUG_PARSE 1
#define PARSE_UINT32_PROPERTY(node, prop_name, value, exception)        \
    if (of_property_read_u32(node, prop_name, (u32*)(&value))) {        \
        printk("failed to get property: %s\n", prop_name);              \
        goto exception;                                                 \
    }                                                                   \
    if (DEBUG_PARSE) {                                                  \
        printk("get property:%25s, value:0x%08x, dec:%8d\n",            \
            prop_name, value, value);                                   \
    }

#ifdef CONFIG_OF
static const struct of_device_id amlogic_meson_cs_dvfs_match[]={
	{	.compatible = "amlogic, meson_vcck_dvfs",
	},
	{},
};
#endif

static void dvfs_vcck_pwm_init(struct device * dev) {
    switch (pwm_ctrl) {
    case PWM_A:
        aml_write_reg32(P_PWM_MISC_REG_AB, (aml_read_reg32(P_PWM_MISC_REG_AB) & ~(0x7f <<  8)) | ((1 << 15) | (1 << 0)));    
        break;
    case PWM_B:
        aml_write_reg32(P_PWM_MISC_REG_AB, (aml_read_reg32(P_PWM_MISC_REG_AB) & ~(0x7f << 16)) | ((1 << 23) | (1 << 1)));    
        break;
    case PWM_C:
        aml_write_reg32(P_PWM_MISC_REG_CD, (aml_read_reg32(P_PWM_MISC_REG_CD) & ~(0x7f <<  8)) | ((1 << 15) | (1 << 0)));    
        break;
    case PWM_D:
        aml_write_reg32(P_PWM_MISC_REG_CD, (aml_read_reg32(P_PWM_MISC_REG_CD) & ~(0x7f << 16)) | ((1 << 23) | (1 << 1)));    
        break;
    }
    aml_write_reg32(pmw_base[pwm_ctrl], g_table[g_table_cnt - 1].pwm_value);    

    if (IS_ERR(devm_pinctrl_get_select_default(dev))) {
		printk("did not get pins for pwm--------\n");
	} else {
	    printk("get pin for pwm--------\n");
    }
}

static int meson_cs_dvfs_probe(struct platform_device *pdev)
{
    int ret;    
	struct device_node *np = pdev->dev.of_node;
    int i = 0;
    char *out_str = NULL;

    if (!np) {
        return -ENODEV;
    }
    PARSE_UINT32_PROPERTY(np, "use_pwm", use_pwm, out);
    if (use_pwm) {
        ret = of_property_read_string(np, "pmw_controller", (const char **)&out_str); 
        if (ret) {
            printk("%s, not found 'pwm_controller', use pmw_c as default\n", __func__);    
        }
        if (out_str) {
            if (!strncmp(out_str, "PWM_", 4)) {
                i = out_str[4] - 'A'; 
                if (i > PWM_D || i < 0) {
                    printk("%s, bad pwm controller value:%s\n", __func__, out_str);
                } else {
                    pwm_ctrl = i;    
                }
            } else {
                printk("%s, bad pwm controller value:%s\n", __func__, out_str);
            }
        }
    }
    PARSE_UINT32_PROPERTY(np, "table_count", g_table_cnt, out);
    g_table = kzalloc(sizeof(struct cs_voltage) * g_table_cnt, GFP_KERNEL);
    if (g_table == NULL) {
        printk("%s, allocate memory failed\n", __func__);    
        return -ENOMEM;
    }
    ret = of_property_read_u32_array(np, 
                                     "cs_voltage_table", 
                                     (u32 *)g_table, 
                                     (sizeof(struct cs_voltage) * g_table_cnt) / sizeof(int));
    if (ret < 0) {
        printk("%s, failed to read 'cs_voltage_table', ret:%d\n", __func__, ret);
        goto out;
    }
    printk("%s, table count:%d, use_pwm:%d, pwm controller:%d\n", __func__, g_table_cnt, use_pwm, pwm_ctrl);
    for (i = 0; i < g_table_cnt; i++) {
        printk("%2d, %08x, %7d\n", i, g_table[i].pwm_value, g_table[i].voltage);    
    }

    if (use_pwm) {
        dvfs_vcck_pwm_init(&pdev->dev);
    }
    aml_dvfs_register_driver(&aml_cs_dvfs_driver);
    return 0;
out:
    if (g_table) {
        kfree(g_table);
        g_table = NULL;
    }
    return -1;
}

static int meson_cs_dvfs_remove(struct platform_device *pdev)
{
    if (g_table) {
        kfree(g_table);    
    } 
    aml_dvfs_unregister_driver(&aml_cs_dvfs_driver);
    return 0;
}

static struct platform_driver meson_cs_dvfs_driver = {
	.driver = {
		.name = "meson_vcck_dvfs",
		.owner = THIS_MODULE,
    #ifdef CONFIG_OF
		.of_match_table = amlogic_meson_cs_dvfs_match,
    #endif
	},
	.probe = meson_cs_dvfs_probe,
	.remove = meson_cs_dvfs_remove,
};


static int __init meson_cs_dvfs_init(void)
{
	return platform_driver_register(&meson_cs_dvfs_driver);
}

static void __exit meson_cs_dvfs_cleanup(void)
{
	platform_driver_unregister(&meson_cs_driver);
}

subsys_initcall(meson_cs_dvfs_init);
module_exit(meson_cs_dvfs_cleanup);

#endif      /* CONFIG_AML_DVFS */

MODULE_AUTHOR("Elvis Yu <elvis.yu@amlogic.com>");
MODULE_DESCRIPTION("Amlogic Meson current source voltage regulator driver");
MODULE_LICENSE("GPL v2");
