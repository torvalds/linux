#include <linux/regulator/machine.h>
#include <linux/mfd/rk808.h>
#include <mach/sram.h>
#include <linux/platform_device.h>

#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>

#ifdef CONFIG_MFD_RK808

static int rk808_pre_init(struct rk808 *rk808)
{
	int ret,val;
	 printk("%s,line=%d\n", __func__,__LINE__);
	 /***********set ILIM ************/
	val = rk808_reg_read(rk808,RK808_BUCK3_CONFIG_REG);
	val &= (~(0x7 <<0));
	val |= (0x2 <<0);
	ret = rk808_reg_write(rk808,RK808_BUCK3_CONFIG_REG,val);
	if (ret < 0) {
                printk(KERN_ERR "Unable to write RK808_BUCK3_CONFIG_REG reg\n");
                return ret;
        }

	val = rk808_reg_read(rk808,RK808_BUCK4_CONFIG_REG);
	val &= (~(0x7 <<0));
	val |= (0x3 <<0);
	ret = rk808_reg_write(rk808,RK808_BUCK4_CONFIG_REG,val);
	if (ret < 0) {
                printk(KERN_ERR "Unable to write RK808_BUCK4_CONFIG_REG reg\n");
                return ret;
        }
	
	val = rk808_reg_read(rk808,RK808_BOOST_CONFIG_REG);
	val &= (~(0x7 <<0));
	val |= (0x1 <<0);
	ret = rk808_reg_write(rk808,RK808_BOOST_CONFIG_REG,val);
	if (ret < 0) {
                printk(KERN_ERR "Unable to write RK808_BOOST_CONFIG_REG reg\n");
                return ret;
        }
	/*****************************************/
	/***********set buck OTP function************/
	ret = rk808_reg_write(rk808,0x6f,0x5a);
	if (ret < 0) {
                printk(KERN_ERR "Unable to write 0x6f reg\n");
                return ret;
        }
	
	ret = rk808_reg_write(rk808,0x91,0x80);
	if (ret < 0) {
                printk(KERN_ERR "Unable to write 0x91 reg\n");
                return ret;
        }

        ret = rk808_reg_write(rk808,0x92,0x55);
	 if (ret <0) {
                printk(KERN_ERR "Unable to write 0x92 reg\n");
                return ret;
        }
	/*****************************************/
	/***********set buck 12.5mv/us ************/
	val = rk808_reg_read(rk808,RK808_BUCK1_CONFIG_REG);
	val &= (~(0x3 <<3));
	val |= (0x3 <<0);
	ret = rk808_reg_write(rk808,RK808_BUCK1_CONFIG_REG,val);
	if (ret < 0) {
                printk(KERN_ERR "Unable to write RK808_BUCK1_CONFIG_REG reg\n");
                return ret;
        }

	val = rk808_reg_read(rk808,RK808_BUCK2_CONFIG_REG);
        val &= (~(0x3 <<3));
	val |= (0x3 <<0);
        ret = rk808_reg_write(rk808,RK808_BUCK2_CONFIG_REG,val);
	 if (ret <0) {
                printk(KERN_ERR "Unable to write RK808_BUCK2_CONFIG_REG reg\n");
                return ret;
        }
	/*****************************************/

	/*******enable switch and boost***********/
	val = rk808_reg_read(rk808,RK808_DCDC_EN_REG);
        val |= (0x3 << 5);    //enable switch1/2
//	val |= (0x1 << 4);    //enable boost
        ret = rk808_reg_write(rk808,RK808_DCDC_EN_REG,val);
         if (ret <0) {
                printk(KERN_ERR "Unable to write RK808_DCDC_EN_REG reg\n");
                return ret;
	}
	/****************************************/
	/***********set dc1\2 dvs voltge*********/
	 val = rk808_reg_read(rk808,RK808_BUCK1_DVS_REG);
        val |= 0x34;    //set dc1 dvs 1.35v
        ret = rk808_reg_write(rk808,RK808_BUCK1_DVS_REG,val);
         if (ret <0) {
                printk(KERN_ERR "Unable to write RK808_BUCK1_DVS_REG reg\n");
                return ret;
        }
	
	  val = rk808_reg_read(rk808,RK808_BUCK2_DVS_REG);
        val |= 0x34;    //set dc2 dvs 1.35v
        ret = rk808_reg_write(rk808,RK808_BUCK2_DVS_REG,val);
         if (ret <0) {
                printk(KERN_ERR "Unable to write RK808_BUCK2_DVS_REG reg\n");
                return ret;
        }
	/***********************************/

	/****************set vbat low **********/
	 val = rk808_reg_read(rk808,RK808_VB_MON_REG);
        val &= (~(0x7<<0));    //set vbat < 3.5v irq
	val &= (~(0x1 <<4));
	val |= (0x7 <<0);
	val |= (0x1 << 4);
        ret = rk808_reg_write(rk808,RK808_VB_MON_REG,val);
         if (ret <0) {
                printk(KERN_ERR "Unable to write RK808_VB_MON_REG reg\n");
                return ret;
        }
	/**************************************/
	/********set dcdc/ldo/switch off when in sleep******/
	  val = rk808_reg_read(rk808,RK808_SLEEP_SET_OFF_REG1);
//        val |= (0x3<<5);   
        ret = rk808_reg_write(rk808,RK808_SLEEP_SET_OFF_REG1,val);
         if (ret <0) {
                printk(KERN_ERR "Unable to write RK808_SLEEP_SET_OFF_REG1 reg\n");
                return ret;
        }
	val = rk808_reg_read(rk808,RK808_SLEEP_SET_OFF_REG2);
//        val |= (0x1<<4);     
        ret = rk808_reg_write(rk808,RK808_SLEEP_SET_OFF_REG2,val);
         if (ret <0) {
                printk(KERN_ERR "Unable to write RK808_SLEEP_SET_OFF_REG2 reg\n");
                return ret;
        }
	/**************************************************/
	/**********mask int****************/
	 val = rk808_reg_read(rk808,RK808_INT_STS_MSK_REG1);
         val |= (0x1<<0); //mask vout_lo_int    
        ret = rk808_reg_write(rk808,RK808_INT_STS_MSK_REG1,val);
         if (ret <0) {
                printk(KERN_ERR "Unable to write RK808_INT_STS_MSK_REG1 reg\n");
                return ret;
        }
	/**********************************/
	printk("%s,line=%d\n", __func__,__LINE__);
}
static int rk808_set_init(struct rk808 *rk808)
{
	struct regulator *dcdc;
	struct regulator *ldo;
	int i = 0;
	printk("%s,line=%d\n", __func__,__LINE__);

	#ifndef CONFIG_RK_CONFIG
	g_pmic_type = PMIC_TYPE_RK808;
	#endif
	printk("%s:g_pmic_type=%d\n",__func__,g_pmic_type);
	
	for(i = 0; i < ARRAY_SIZE(rk808_dcdc_info); i++)
	{

                if(rk808_dcdc_info[i].min_uv == 0 && rk808_dcdc_info[i].max_uv == 0)
                        continue;
	        dcdc =regulator_get(NULL, rk808_dcdc_info[i].name);
	        regulator_set_voltage(dcdc, rk808_dcdc_info[i].min_uv, rk808_dcdc_info[i].max_uv);
		 regulator_set_suspend_voltage(dcdc, rk808_dcdc_info[i].suspend_vol);
	        regulator_enable(dcdc);
	        printk("%s  %s =%duV end\n", __func__,rk808_dcdc_info[i].name, regulator_get_voltage(dcdc));
	        regulator_put(dcdc);
	        udelay(100);
	}
	
	for(i = 0; i < ARRAY_SIZE(rk808_ldo_info); i++)
	{
                if(rk808_ldo_info[i].min_uv == 0 && rk808_ldo_info[i].max_uv == 0)
                        continue;
	        ldo =regulator_get(NULL, rk808_ldo_info[i].name);

	        regulator_set_voltage(ldo, rk808_ldo_info[i].min_uv, rk808_ldo_info[i].max_uv);
		 regulator_set_suspend_voltage(ldo, rk808_ldo_info[i].suspend_vol);
	        regulator_enable(ldo);
	        printk("%s  %s =%duV end\n", __func__,rk808_ldo_info[i].name, regulator_get_voltage(ldo));
	        regulator_put(ldo);
	}

	#ifdef CONFIG_RK_CONFIG
	if(sram_gpio_init(get_port_config(pmic_slp).gpio, &pmic_sleep) < 0){
		printk(KERN_ERR "sram_gpio_init failed\n");
		return -EINVAL;
	}
	if(port_output_init(pmic_slp, 0, "pmic_slp") < 0){
		printk(KERN_ERR "port_output_init failed\n");
		return -EINVAL;
	}
	#else
	if(sram_gpio_init(PMU_POWER_SLEEP, &pmic_sleep) < 0){
		printk(KERN_ERR "sram_gpio_init failed\n");
		return -EINVAL;
	}
	gpio_request(PMU_POWER_SLEEP, "NULL");
	gpio_direction_output(PMU_POWER_SLEEP, GPIO_LOW);
		
	#endif
	
	printk("%s,line=%d END\n", __func__,__LINE__);
	
	
	return 0;
}

static struct regulator_consumer_supply rk808_buck1_supply[] = {
	{
		.supply = "rk_dcdc1",
	},

	 {
                .supply = "vdd_cpu",
        },

};
static struct regulator_consumer_supply rk808_buck2_supply[] = {
	{
		.supply = "rk_dcdc2",
	},	

	 {
                .supply = "vdd_core",
        },

};
static struct regulator_consumer_supply rk808_buck3_supply[] = {
	{
		.supply = "rk_dcdc3",
	},
};

static struct regulator_consumer_supply rk808_buck4_supply[] = {
	{
		.supply = "rk_dcdc4",
	},

};

static struct regulator_consumer_supply rk808_ldo1_supply[] = {
	{
		.supply = "rk_ldo1",
	},
};
static struct regulator_consumer_supply rk808_ldo2_supply[] = {
	{
		.supply = "rk_ldo2",
	},
};

static struct regulator_consumer_supply rk808_ldo3_supply[] = {
	{
		.supply = "rk_ldo3",
	},
};
static struct regulator_consumer_supply rk808_ldo4_supply[] = {
	{
		.supply = "rk_ldo4",
	},
};
static struct regulator_consumer_supply rk808_ldo5_supply[] = {
	{
		.supply = "rk_ldo5",
	},
};
static struct regulator_consumer_supply rk808_ldo6_supply[] = {
	{
		.supply = "rk_ldo6",
	},
};

static struct regulator_consumer_supply rk808_ldo7_supply[] = {
	{
		.supply = "rk_ldo7",
	},
};
static struct regulator_consumer_supply rk808_ldo8_supply[] = {
	{
		.supply = "rk_ldo8",
	},
};

static struct regulator_init_data rk808_buck1 = {
	.constraints = {
		.name           = "RK_DCDC1",
		.min_uV			= 700000,
		.max_uV			= 1500000,
		.apply_uV		= 1,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_FAST | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(rk808_buck1_supply),
	.consumer_supplies =  rk808_buck1_supply,
};

/* */
static struct regulator_init_data rk808_buck2 = {
	.constraints = {
		.name           = "RK_DCDC2",
		.min_uV			= 700000,
		.max_uV			= 1500000,
		.apply_uV		= 1,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_FAST | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(rk808_buck2_supply),
	.consumer_supplies =  rk808_buck2_supply,
};

/* */
static struct regulator_init_data rk808_buck3 = {
	.constraints = {
		.name           = "RK_DCDC3",
		.min_uV			= 1000000,
		.max_uV			= 1800000,
		.apply_uV		= 1,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_FAST | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(rk808_buck3_supply),
	.consumer_supplies =  rk808_buck3_supply,
};

static struct regulator_init_data rk808_buck4 = {
	.constraints = {
		.name           = "RK_DCDC4",
		.min_uV			= 1800000,
		.max_uV			= 3300000,
		.apply_uV		= 1,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_FAST | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(rk808_buck4_supply),
	.consumer_supplies =  rk808_buck4_supply,
};

static struct regulator_init_data rk808_ldo1 = {
	.constraints = {
		.name           = "RK_LDO1",
		.min_uV			= 1800000,
		.max_uV			= 3400000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_FAST | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(rk808_ldo1_supply),
	.consumer_supplies =  rk808_ldo1_supply,
};

/* */
static struct regulator_init_data rk808_ldo2 = {
	.constraints = {
		.name           = "RK_LDO2",
		.min_uV			= 1800000,
		.max_uV			= 3400000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_FAST | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(rk808_ldo2_supply),
	.consumer_supplies =  rk808_ldo2_supply,
};

/* */
static struct regulator_init_data rk808_ldo3 = {
	.constraints = {
		.name           = "RK_LDO3",
		.min_uV			= 800000,
		.max_uV			= 2500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_FAST | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(rk808_ldo3_supply),
	.consumer_supplies =  rk808_ldo3_supply,
};

/* */
static struct regulator_init_data rk808_ldo4 = {
	.constraints = {
		.name           = "RK_LDO4",
		.min_uV			= 1800000,
		.max_uV			= 3400000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_FAST | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(rk808_ldo4_supply),
	.consumer_supplies =  rk808_ldo4_supply,
};

static struct regulator_init_data rk808_ldo5 = {
	.constraints = {
		.name           = "RK_LDO5",
		.min_uV			= 1800000,
		.max_uV			= 3400000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_FAST | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(rk808_ldo5_supply),
	.consumer_supplies =  rk808_ldo5_supply,
};

static struct regulator_init_data rk808_ldo6 = {
	.constraints = {
		.name           = "RK_LDO6",
		.min_uV			= 800000,
		.max_uV			= 2500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_FAST | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(rk808_ldo6_supply),
	.consumer_supplies =  rk808_ldo6_supply,
};

static struct regulator_init_data rk808_ldo7 = {
	.constraints = {
		.name           = "RK_LDO7",
		.min_uV			= 800000,
		.max_uV			= 2500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_FAST | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(rk808_ldo7_supply),
	.consumer_supplies =  rk808_ldo7_supply,
};

static struct regulator_init_data rk808_ldo8 = {
	.constraints = {
		.name           = "RK_LDO8",
		.min_uV			= 1800000,
		.max_uV			= 3400000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_FAST | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(rk808_ldo8_supply),
	.consumer_supplies =  rk808_ldo8_supply,
};

struct rk808_regulator_subdev rk808_regulator_subdev_id[] = {
	{
		.id=0,
		.initdata=&rk808_buck1,		
	 },

	{
		.id=1,
		.initdata=&rk808_buck2,		
	 },
	{
		.id=2,
		.initdata=&rk808_buck3,		
	 },
        {
		.id=3,
		.initdata=&rk808_buck4,		
	 },

	{
		.id=4,
		.initdata=&rk808_ldo1,		
	 },

	{
		.id=5,
		.initdata=&rk808_ldo2,		
	 },

	{
		.id=6,
		.initdata=&rk808_ldo3,		
	 },

	{
		.id=7,
		.initdata=&rk808_ldo4,		
	 },

	{
		.id=8,
		.initdata=&rk808_ldo5,		
	 },

	{
		.id=9,
		.initdata=&rk808_ldo6,		
	 },

	{
		.id=10,
		.initdata=&rk808_ldo7,		
	 },

	{
		.id=11,
		.initdata=&rk808_ldo8,		
	 },
};

 struct rk808_platform_data rk808_data={
	.pre_init=rk808_pre_init,
	.set_init=rk808_set_init,
	.num_regulators=12,
	.regulators=rk808_regulator_subdev_id,
	.irq 	= (unsigned)RK808_HOST_IRQ,		
	.irq_base = IRQ_BOARD_BASE,
};

#ifdef CONFIG_HAS_EARLYSUSPEND
void rk808_early_suspend(struct early_suspend *h)
{
}

void rk808_late_resume(struct early_suspend *h)
{
}
#endif
#ifdef CONFIG_PM

void rk808_device_suspend(void)
{
}
void rk808_device_resume(void)
{
}       
#endif

void __sramfunc board_pmu_rk808_suspend(void)
{	
	#ifdef CONFIG_CLK_SWITCH_TO_32K
	 sram_gpio_set_value(pmic_sleep, GPIO_HIGH);  
	#endif
}
void __sramfunc board_pmu_rk808_resume(void)
{
	#ifdef CONFIG_CLK_SWITCH_TO_32K
 	sram_gpio_set_value(pmic_sleep, GPIO_LOW);  
	sram_32k_udelay(10000);
	#endif
}

#endif




