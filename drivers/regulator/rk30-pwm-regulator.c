/* drivers/regulator/rk29-pwm-regulator.c
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
FILE		:	    	rk29-pwm-regulator.c
DESC		:	rk29 pwm regulator driver
AUTHOR		:	hxy
DATE		:	2010-12-20
NOTES		:
$LOG: GPIO.C,V $
REVISION 0.01
********************************************************************/
#include <linux/bug.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/rk29-pwm-regulator.h>
#include <mach/iomux.h>
#include <linux/gpio.h>
#include <mach/board.h>


#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif


#define	PWM_VCORE_120		40
#define PWM_VCORE_125		32
#define	PWM_VCORE_130		21
#define	PWM_VCORE_135		10
#define	PWM_VCORE_140		0

#define PWM_DCDC_MAX_NAME	2
struct rk_pwm_dcdc {
        char name[PWM_DCDC_MAX_NAME];
        struct regulator_desc desc;
        int pwm_id;
        struct regulator_dev *regulator;
	struct pwm_platform_data *pdata;
};


#if defined(CONFIG_ARCH_RK30)
#define pwm_write_reg(id, addr, val)        __raw_writel(val, addr+(RK30_PWM01_BASE+(id>>1)*0x20000)+id*0x10)
#define pwm_read_reg(id, addr)              __raw_readl(addr+(RK30_PWM01_BASE+(id>>1)*0x20000+id*0x10))
#elif defined(CONFIG_ARCH_RK29)
#define pwm_write_reg(id, addr, val)        __raw_writel(val, addr+(RK29_PWM_BASE+id*0x10))
#define pwm_read_reg(id, addr)              __raw_readl(addr+(RK29_PWM_BASE+id*0x10))    
#elif defined(CONFIG_ARCH_RK2928)
#define pwm_write_reg(id, addr, val)        __raw_writel(val, addr+(RK2928_PWM_BASE+id*0x10))
#define pwm_read_reg(id, addr)              __raw_readl(addr+(RK2928_PWM_BASE+id*0x10))
#endif

const static int pwm_voltage_map[] = {
	1000000, 1025000, 1050000, 1075000, 1100000, 1125000, 1150000, 1175000, 1200000, 1225000, 1250000, 1275000, 1300000, 1325000, 1350000, 1375000, 1400000
};

static struct clk *pwm_clk[2];
static struct rk_pwm_dcdc *g_dcdc;

static int pwm_set_rate(struct pwm_platform_data *pdata,int nHz,u32 rate)
{
	u32 divh,divTotal;
	int id = pdata->pwm_id;
	unsigned long clkrate;

#if defined(CONFIG_ARCH_RK29) || defined(CONFIG_ARCH_RK2928)
	clkrate = clk_get_rate(pwm_clk[0]);
#elif defined(CONFIG_ARCH_RK30)
	if (id == 0 || id == 1) {
		clkrate = clk_get_rate(pwm_clk[0]);
	} else if (id== 2 || id == 3) {
		clkrate = clk_get_rate(pwm_clk[1]);
	} else {
		printk("%s:pwm id error,id=%d\n",__func__,id);
		return -1;
	}
#endif
	
	DBG("%s:id=%d,rate=%d,clkrate=%d\n",__func__,id,rate,clkrate); 

	if(rate == 0)
	{
		// iomux pwm to gpio
		rk29_mux_api_set(pdata->pwm_iomux_name, pdata->pwm_iomux_gpio);
		//disable pull up or down
		gpio_pull_updown(pdata->pwm_gpio,PullDisable);
		// set gpio to low level
		gpio_direction_output(pdata->pwm_gpio,GPIO_LOW);
	}
	else if (rate < 100)
	{
		// iomux pwm
		rk29_mux_api_set(pdata->pwm_iomux_name, pdata->pwm_iomux_pwm);

		pwm_write_reg(id,PWM_REG_CTRL, PWM_DIV|PWM_RESET);
		divh = clkrate / nHz;
		divh = divh >> (1+(PWM_DIV>>9));
		pwm_write_reg(id,PWM_REG_LRC,(divh == 0)?1:divh);

		divTotal =pwm_read_reg(id,PWM_REG_LRC);
		divh = divTotal*rate/100;
		pwm_write_reg(id, PWM_REG_HRC, divh?divh:1);
		pwm_write_reg(id,PWM_REG_CNTR,0);
		pwm_write_reg(id, PWM_REG_CTRL,pwm_read_reg(id,PWM_REG_CTRL)|PWM_DIV|PWM_ENABLE|PWM_TimeEN);
	}
	else if (rate == 100)
	{
		// iomux pwm to gpio
		rk29_mux_api_set(pdata->pwm_iomux_name, pdata->pwm_iomux_gpio);
		//disable pull up or down
		gpio_pull_updown(pdata->pwm_gpio,PullDisable);
		// set gpio to low level
		gpio_direction_output(pdata->pwm_gpio,GPIO_HIGH);

	}
	else
	{
		printk("%s:rate error\n",__func__);
		return -1;
	}

	usleep_range(10*1000, 10*1000);

	return (0);
}

static int pwm_regulator_list_voltage(struct regulator_dev *dev,unsigned int index)
{
	struct rk_pwm_dcdc *dcdc = rdev_get_drvdata(dev);
	if (index < dcdc->desc.n_voltages)
	return dcdc->pdata->pwm_voltage_map[index];
	else
		return -1;
}

static int pwm_regulator_is_enabled(struct regulator_dev *dev)
{
	DBG("Enter %s\n",__FUNCTION__);
	return 0;
}

static int pwm_regulator_enable(struct regulator_dev *dev)
{
	DBG("Enter %s\n",__FUNCTION__);
	return 0;
}

static int pwm_regulator_disable(struct regulator_dev *dev)
{
	DBG("Enter %s\n",__FUNCTION__);
	return 0;
}

static int pwm_regulator_get_voltage(struct regulator_dev *dev)
{
	//struct pwm_platform_data *pdata = rdev_get_drvdata(dev);
	
	struct rk_pwm_dcdc *dcdc = rdev_get_drvdata(dev);

	DBG("Enter %s\n",__FUNCTION__);  

	return (dcdc->pdata->pwm_voltage);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38))
static int pwm_regulator_set_voltage(struct regulator_dev *dev,
		int min_uV, int max_uV, unsigned *selector)
#else
static int pwm_regulator_set_voltage(struct regulator_dev *dev,
		int min_uV, int max_uV)
#endif
{	   
	struct rk_pwm_dcdc *dcdc = rdev_get_drvdata(dev);
	const int *voltage_map = dcdc->pdata->pwm_voltage_map;
	int max = dcdc->pdata->max_uV;
	int coefficient = dcdc->pdata->coefficient;
	u32 size = dcdc->desc.n_voltages, i, vol,pwm_value;

	DBG("%s:  min_uV = %d, max_uV = %d\n",__FUNCTION__, min_uV,max_uV);

	if (min_uV < voltage_map[0] ||max_uV > voltage_map[size-1])
	{
		printk("%s:voltage is out of table\n",__func__);
		return -EINVAL;
	}

	for (i = 0; i < size; i++)
	{
		if (voltage_map[i] >= min_uV)
			break;
	}


	vol =  voltage_map[i];

	dcdc->pdata->pwm_voltage = vol;

	// VDD12 = 1.40 - 0.455*D , 其中D为PWM占空比, 
	pwm_value = (max-vol)/coefficient/10;  // pwm_value %, coefficient *1000

	if (pwm_set_rate(dcdc->pdata,1000*1000,pwm_value)!=0)
	{
		printk("%s:fail to set pwm rate,pwm_value=%d\n",__func__,pwm_value);
		return -1;

	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38))
	*selector = i;
#endif

	DBG("%s:ok,vol=%d,pwm_value=%d\n",__FUNCTION__,vol,pwm_value);

	return 0;

}

static struct regulator_ops pwm_voltage_ops = {
	.list_voltage	= pwm_regulator_list_voltage,
	.set_voltage	=pwm_regulator_set_voltage,
	.get_voltage	= pwm_regulator_get_voltage,
	.enable		= pwm_regulator_enable,
	.disable	= pwm_regulator_disable,
	.is_enabled	= pwm_regulator_is_enabled,
};

static struct regulator_desc pwm_regulator= {
	.name = "pwm-regulator",
	.ops = &pwm_voltage_ops,
	.type = REGULATOR_VOLTAGE,
};

static int __devinit pwm_regulator_probe(struct platform_device *pdev)
{
	struct pwm_platform_data *pdata = pdev->dev.platform_data;
	struct rk_pwm_dcdc *dcdc;
	int pwm_id  =  pdata->pwm_id;
	int id = pdev->id;
	int ret ;
    	char gpio_name[20];
	unsigned selector = 0;
	
	if (!pdata)
		return -ENODEV;

	if (!pdata->pwm_voltage)
		pdata->pwm_voltage = 1100000;	// default 1.1v

	if(!pdata->pwm_voltage_map)
		pdata->pwm_voltage_map = pwm_voltage_map;

	if(!pdata->max_uV)
		pdata->max_uV = 1400000;

	if(!pdata->min_uV)
		pdata->min_uV = 1000000;
	
	if(pdata->suspend_voltage < pdata->min_uV)
		pdata->suspend_voltage = pdata->min_uV;
	
	if(pdata->suspend_voltage > pdata->max_uV)	
		pdata->suspend_voltage = pdata->max_uV;
	
	dcdc = kzalloc(sizeof(struct rk_pwm_dcdc), GFP_KERNEL);
	if (dcdc == NULL) {
		dev_err(&pdev->dev, "Unable to allocate private data\n");
		return -ENOMEM;
	}

	snprintf(dcdc->name, sizeof(dcdc->name), "PWM_DCDC%d", id + 1);
	dcdc->desc.name = dcdc->name;
	dcdc->desc.id = id;
	dcdc->desc.type = REGULATOR_VOLTAGE;
	dcdc->desc.n_voltages = ARRAY_SIZE(pwm_voltage_map);
	dcdc->desc.ops = &pwm_voltage_ops;
	dcdc->desc.owner = THIS_MODULE;
	dcdc->pdata = pdata;
	printk("%s:n_voltages=%d\n",__func__,dcdc->desc.n_voltages);
	dcdc->regulator = regulator_register(&dcdc->desc, &pdev->dev,
					     pdata->init_data, dcdc);
	if (IS_ERR(dcdc->regulator)) {
		ret = PTR_ERR(dcdc->regulator);
		dev_err(&pdev->dev, "Failed to register PWM_DCDC%d: %d\n",
			id + 1, ret);
		goto err;
	}

	snprintf(gpio_name, sizeof(gpio_name), "PWM_DCDC%d", id + 1);
	ret = gpio_request(pdata->pwm_gpio,gpio_name);
	if (ret) {
		dev_err(&pdev->dev,"failed to request pwm gpio\n");
		goto err_gpio;
	}
	
#if defined(CONFIG_ARCH_RK29)
		pwm_clk[0] = clk_get(NULL, "pwm");
#elif defined(CONFIG_ARCH_RK30)
		if (pwm_id == 0 || pwm_id == 1)
		{
			pwm_clk[0] = clk_get(NULL, "pwm01");	
			clk_enable(pwm_clk[0]);
		}
		else if (pwm_id== 2 || pwm_id == 3)
		{
			pwm_clk[1] = clk_get(NULL, "pwm23");		
			clk_enable(pwm_clk[1]);
		}
#elif defined(CONFIG_ARCH_RK2928)
		pwm_clk[0] = clk_get(NULL, "pwm01");
		if (IS_ERR(pwm_clk[0])) {
			printk("pwm_clk get error %p\n", pwm_clk[0]);
			return -EINVAL;
		}
		clk_enable(pwm_clk[0]);
#endif
	
	g_dcdc	= dcdc;
	platform_set_drvdata(pdev, dcdc);	
	printk(KERN_INFO "pwm_regulator.%d: driver initialized\n",id);
	pwm_regulator_set_voltage(dcdc->regulator,pdata->pwm_voltage,pdata->pwm_voltage,&selector);

	return 0;


err_gpio:
	gpio_free(pdata->pwm_gpio);
err:
	printk("%s:error\n",__func__);
	return ret;

}

static int  __sramdata g_PWM_REG_LRC = 0;
static int  __sramdata g_PWM_REG_HRC = 0;
void pwm_suspend_voltage(void)
{
	struct rk_pwm_dcdc *dcdc = g_dcdc;
	int suspend_voltage = 0;
	int pwm_id = 0;
	
	if(!dcdc)
		return;
	pwm_id = dcdc->pdata->pwm_id;
	suspend_voltage = dcdc->pdata->suspend_voltage;
	
	g_PWM_REG_LRC = pwm_read_reg(pwm_id, PWM_REG_LRC);
	g_PWM_REG_HRC = pwm_read_reg(pwm_id,PWM_REG_HRC);

	switch(suspend_voltage)
	{
		case 1000000:
		pwm_write_reg(pwm_id, PWM_REG_LRC, 0x25);
		pwm_write_reg(pwm_id,PWM_REG_HRC,0x20); // 1 .00
		break;
		
		case 1050000:
		pwm_write_reg(pwm_id, PWM_REG_LRC, 0x25);
		pwm_write_reg(pwm_id,PWM_REG_HRC,0x1c); // 1 .05
		break;
		
		case 1100000:
		pwm_write_reg(pwm_id, PWM_REG_LRC, 0x25);
		pwm_write_reg(pwm_id,PWM_REG_HRC,0x18); // 1 .1
		break;

		case 1150000:
		pwm_write_reg(pwm_id, PWM_REG_LRC, 0x25);
		pwm_write_reg(pwm_id,PWM_REG_HRC,0x13); // 1 .15
		break;

		default:
		pwm_write_reg(pwm_id, PWM_REG_LRC, 0x25);
		pwm_write_reg(pwm_id,PWM_REG_HRC,0x20); // 1 .00
		break;

	}
		
}

void pwm_resume_voltage(void)
	{
	struct rk_pwm_dcdc *dcdc = g_dcdc;	
	int pwm_id = 0;
	
	if(!dcdc)
		return;
	pwm_id = dcdc->pdata->pwm_id;
	pwm_write_reg(pwm_id, PWM_REG_LRC, g_PWM_REG_LRC);
	pwm_write_reg(pwm_id,PWM_REG_HRC, g_PWM_REG_HRC);
			
}


static int pwm_regulator_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct pwm_platform_data *pdata = pdev->dev.platform_data;
	//struct rk_pwm_dcdc *dcdc = platform_get_drvdata(pdev);
	//unsigned selector = 0;
	//pwm_regulator_set_voltage(dcdc->regulator, 1100000, 1100000, &selector);
	DBG("%s,pwm_id=%d\n",__func__,pdata->pwm_id);
	return 0;
}

static int pwm_regulator_resume(struct platform_device *pdev)
{
	struct pwm_platform_data *pdata = pdev->dev.platform_data;
	//struct rk_pwm_dcdc *dcdc = platform_get_drvdata(pdev);
	//unsigned selector = 0;
	//pwm_regulator_set_voltage(dcdc->regulator, 1150000, 1150000, &selector);
	DBG("%s,pwm_id=%d\n",__func__,pdata->pwm_id);
	return 0;
}

static int __devexit pwm_regulator_remove(struct platform_device *pdev)
{
	struct pwm_platform_data *pdata = pdev->dev.platform_data;
	struct regulator_dev *rdev = platform_get_drvdata(pdev);

	regulator_unregister(rdev);
	gpio_free(pdata->pwm_gpio);

	return 0;
}

static struct platform_driver pwm_regulator_driver = {
	.driver = {
		.name = "pwm-voltage-regulator",
	},
	.suspend = pwm_regulator_suspend,
	.resume = pwm_regulator_resume,
	.remove = __devexit_p(pwm_regulator_remove),
};


static int __init pwm_regulator_module_init(void)
{
	return platform_driver_probe(&pwm_regulator_driver, pwm_regulator_probe);
}

static void __exit pwm_regulator_module_exit(void)
{
	platform_driver_unregister(&pwm_regulator_driver);
}


fs_initcall(pwm_regulator_module_init);

module_exit(pwm_regulator_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("hxy <hxy@rock-chips.com>");
MODULE_DESCRIPTION("k29 pwm change driver");
