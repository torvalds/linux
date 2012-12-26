#include <linux/regulator/machine.h>
#include <linux/regulator/act8846.h>
#include <mach/sram.h>
#include <linux/platform_device.h>

#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>

#ifdef CONFIG_REGULATOR_ACT8846

static int act8846_set_init(struct act8846 *act8846)
{
	struct regulator *dcdc;
	struct regulator *ldo;
	int i = 0;
	printk("%s,line=%d\n", __func__,__LINE__);

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


	#ifndef CONFIG_RK_CONFIG
	g_pmic_type = PMIC_TYPE_ACT8846;
	#endif
	printk("%s:g_pmic_type=%d\n",__func__,g_pmic_type);
	
	for(i = 0; i < ARRAY_SIZE(act8846_dcdc_info); i++)
	{

                if(act8846_dcdc_info[i].min_uv == 0 && act8846_dcdc_info[i].max_uv == 0)
                        continue;
	        dcdc =regulator_get(NULL, act8846_dcdc_info[i].name);
	        regulator_set_voltage(dcdc, act8846_dcdc_info[i].min_uv, act8846_dcdc_info[i].max_uv);
		 regulator_set_suspend_voltage(dcdc, act8846_dcdc_info[i].suspend_vol);
	        regulator_enable(dcdc);
	        printk("%s  %s =%dmV end\n", __func__,act8846_dcdc_info[i].name, regulator_get_voltage(dcdc));
	        regulator_put(dcdc);
	        udelay(100);
	}
	
	for(i = 0; i < ARRAY_SIZE(act8846_ldo_info); i++)
	{
                if(act8846_ldo_info[i].min_uv == 0 && act8846_ldo_info[i].max_uv == 0)
                        continue;
	        ldo =regulator_get(NULL, act8846_ldo_info[i].name);
	        regulator_set_voltage(ldo, act8846_ldo_info[i].min_uv, act8846_ldo_info[i].max_uv);
	        regulator_enable(ldo);
	        printk("%s  %s =%dmV end\n", __func__,act8846_ldo_info[i].name, regulator_get_voltage(ldo));
	        regulator_put(ldo);
	}

	printk("%s,line=%d END\n", __func__,__LINE__);
	
	return 0;
}

static struct regulator_consumer_supply act8846_buck1_supply[] = {
	{
		.supply = "act_dcdc1",
	},

};
static struct regulator_consumer_supply act8846_buck2_supply[] = {
	{
		.supply = "act_dcdc2",
	},
	{
		.supply = "vdd_core",
	},
	
};
static struct regulator_consumer_supply act8846_buck3_supply[] = {
	{
		.supply = "act_dcdc3",
	},
	{
		.supply = "vdd_cpu",
	},
};

static struct regulator_consumer_supply act8846_buck4_supply[] = {
	{
		.supply = "act_dcdc4",
	},

};

static struct regulator_consumer_supply act8846_ldo1_supply[] = {
	{
		.supply = "act_ldo1",
	},
};
static struct regulator_consumer_supply act8846_ldo2_supply[] = {
	{
		.supply = "act_ldo2",
	},
};

static struct regulator_consumer_supply act8846_ldo3_supply[] = {
	{
		.supply = "act_ldo3",
	},
};
static struct regulator_consumer_supply act8846_ldo4_supply[] = {
	{
		.supply = "act_ldo4",
	},
};
static struct regulator_consumer_supply act8846_ldo5_supply[] = {
	{
		.supply = "act_ldo5",
	},
};
static struct regulator_consumer_supply act8846_ldo6_supply[] = {
	{
		.supply = "act_ldo6",
	},
};

static struct regulator_consumer_supply act8846_ldo7_supply[] = {
	{
		.supply = "act_ldo7",
	},
};
static struct regulator_consumer_supply act8846_ldo8_supply[] = {
	{
		.supply = "act_ldo8",
	},
};
static struct regulator_consumer_supply act8846_ldo9_supply[] = {
	{
		.supply = "act_ldo9",
	},
};


static struct regulator_init_data act8846_buck1 = {
	.constraints = {
		.name           = "ACT_DCDC1",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8846_buck1_supply),
	.consumer_supplies =  act8846_buck1_supply,
};

/* */
static struct regulator_init_data act8846_buck2 = {
	.constraints = {
		.name           = "ACT_DCDC2",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8846_buck2_supply),
	.consumer_supplies =  act8846_buck2_supply,
};

/* */
static struct regulator_init_data act8846_buck3 = {
	.constraints = {
		.name           = "ACT_DCDC3",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8846_buck3_supply),
	.consumer_supplies =  act8846_buck3_supply,
};

static struct regulator_init_data act8846_buck4 = {
	.constraints = {
		.name           = "ACT_DCDC4",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8846_buck4_supply),
	.consumer_supplies =  act8846_buck4_supply,
};

static struct regulator_init_data act8846_ldo1 = {
	.constraints = {
		.name           = "ACT_LDO1",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8846_ldo1_supply),
	.consumer_supplies =  act8846_ldo1_supply,
};

/* */
static struct regulator_init_data act8846_ldo2 = {
	.constraints = {
		.name           = "ACT_LDO2",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8846_ldo2_supply),
	.consumer_supplies =  act8846_ldo2_supply,
};

/* */
static struct regulator_init_data act8846_ldo3 = {
	.constraints = {
		.name           = "ACT_LDO3",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8846_ldo3_supply),
	.consumer_supplies =  act8846_ldo3_supply,
};

/* */
static struct regulator_init_data act8846_ldo4 = {
	.constraints = {
		.name           = "ACT_LDO1",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8846_ldo4_supply),
	.consumer_supplies =  act8846_ldo4_supply,
};

static struct regulator_init_data act8846_ldo5 = {
	.constraints = {
		.name           = "ACT_LDO5",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8846_ldo5_supply),
	.consumer_supplies =  act8846_ldo5_supply,
};

static struct regulator_init_data act8846_ldo6 = {
	.constraints = {
		.name           = "ACT_LDO6",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8846_ldo6_supply),
	.consumer_supplies =  act8846_ldo6_supply,
};

static struct regulator_init_data act8846_ldo7 = {
	.constraints = {
		.name           = "ACT_LDO7",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8846_ldo7_supply),
	.consumer_supplies =  act8846_ldo7_supply,
};

static struct regulator_init_data act8846_ldo8 = {
	.constraints = {
		.name           = "ACT_LDO8",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8846_ldo8_supply),
	.consumer_supplies =  act8846_ldo8_supply,
};

static struct regulator_init_data act8846_ldo9 = {
	.constraints = {
		.name           = "ACT_LDO9",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8846_ldo9_supply),
	.consumer_supplies =  act8846_ldo9_supply,
};

struct act8846_regulator_subdev act8846_regulator_subdev_id[] = {
	{
		.id=0,
		.initdata=&act8846_buck1,		
	 },

	{
		.id=1,
		.initdata=&act8846_buck2,		
	 },
	{
		.id=2,
		.initdata=&act8846_buck3,		
	 },
        {
		.id=3,
		.initdata=&act8846_buck4,		
	 },

	{
		.id=4,
		.initdata=&act8846_ldo1,		
	 },

	{
		.id=5,
		.initdata=&act8846_ldo2,		
	 },

	{
		.id=6,
		.initdata=&act8846_ldo3,		
	 },

	{
		.id=7,
		.initdata=&act8846_ldo4,		
	 },

	{
		.id=8,
		.initdata=&act8846_ldo5,		
	 },

	{
		.id=9,
		.initdata=&act8846_ldo6,		
	 },

	{
		.id=10,
		.initdata=&act8846_ldo7,		
	 },

	{
		.id=11,
		.initdata=&act8846_ldo8,		
	 },
#if 1
	{
		.id=12,
		.initdata=&act8846_ldo9,		
	 },
#endif
};

static struct act8846_platform_data act8846_data={
	.set_init=act8846_set_init,
	.num_regulators=13,
	.regulators=act8846_regulator_subdev_id,
};

#ifdef CONFIG_HAS_EARLYSUSPEND
void act8846_early_suspend(struct early_suspend *h)
{
}

void act8846_late_resume(struct early_suspend *h)
{
}
#endif

#endif

void __sramfunc board_pmu_act8846_suspend(void)
{	
	#ifdef CONFIG_CLK_SWITCH_TO_32K
	 sram_gpio_set_value(pmic_sleep, GPIO_HIGH);  
	#endif
}
void __sramfunc board_pmu_act8846_resume(void)
{
	#ifdef CONFIG_CLK_SWITCH_TO_32K
 	sram_gpio_set_value(pmic_sleep, GPIO_LOW);  
	sram_udelay(2000);
	#endif
}


