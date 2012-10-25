#include <linux/regulator/machine.h>
#include <linux/regulator/act8931.h>
#include <mach/sram.h>
#include <linux/platform_device.h>

#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>

#ifdef CONFIG_REGULATOR_ACT8931

#define ACT8931_CHGSEL_VALUE GPIO_HIGH /* Declined to 20% current */

extern int platform_device_register(struct platform_device *pdev);

static int act8931_set_init(struct act8931 *act8931)
{
	struct regulator *dcdc;
	struct regulator *ldo;
	int i = 0;
	printk("%s,line=%d\n", __func__,__LINE__);

	g_pmic_type = PMIC_TYPE_ACT8931;
	printk("%s:g_pmic_type=%d\n",__func__,g_pmic_type);

	#ifdef CONFIG_RK30_PWM_REGULATOR
	platform_device_register(&pwm_regulator_device[0]);
	#endif
	
	for(i = 0; i < ARRAY_SIZE(act8931_dcdc_info); i++)
	{
	dcdc =regulator_get(NULL, act8931_dcdc_info[i].name);
	regulator_set_voltage(dcdc, act8931_dcdc_info[i].min_uv, act8931_dcdc_info[i].max_uv);
	regulator_enable(dcdc);
	printk("%s  %s =%dmV end\n", __func__,act8931_dcdc_info[i].name, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);
	}
	
	for(i = 0; i < ARRAY_SIZE(act8931_ldo_info); i++)
	{
	ldo =regulator_get(NULL, act8931_ldo_info[i].name);
	regulator_set_voltage(ldo, act8931_ldo_info[i].min_uv, act8931_ldo_info[i].max_uv);
	regulator_enable(ldo);
	//printk("%s  %s =%dmV end\n", __func__,act8931_ldo_info[i].name, regulator_get_voltage(ldo));
	regulator_put(ldo);
	}

	printk("%s,line=%d END\n", __func__,__LINE__);
	
	return 0;
}

static struct regulator_consumer_supply act8931_buck1_supply[] = {
	{
		.supply = "act_dcdc1",
	},

};
static struct regulator_consumer_supply act8931_buck2_supply[] = {
	{
		.supply = "act_dcdc2",
	},
	
};
static struct regulator_consumer_supply act8931_buck3_supply[] = {
	{
		.supply = "act_dcdc3",
	},
	{
		.supply = "vdd_cpu",
	},
};

static struct regulator_consumer_supply act8931_ldo1_supply[] = {
	{
		.supply = "act_ldo1",
	},
};
static struct regulator_consumer_supply act8931_ldo2_supply[] = {
	{
		.supply = "act_ldo2",
	},
};

static struct regulator_consumer_supply act8931_ldo3_supply[] = {
	{
		.supply = "act_ldo3",
	},
};
static struct regulator_consumer_supply act8931_ldo4_supply[] = {
	{
		.supply = "act_ldo4",
	},
};

static struct regulator_init_data act8931_buck1 = {
	.constraints = {
		.name           = "ACT_DCDC1",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8931_buck1_supply),
	.consumer_supplies =  act8931_buck1_supply,
};

/* */
static struct regulator_init_data act8931_buck2 = {
	.constraints = {
		.name           = "ACT_DCDC2",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8931_buck2_supply),
	.consumer_supplies =  act8931_buck2_supply,
};

/* */
static struct regulator_init_data act8931_buck3 = {
	.constraints = {
		.name           = "ACT_DCDC3",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8931_buck3_supply),
	.consumer_supplies =  act8931_buck3_supply,
};

static struct regulator_init_data act8931_ldo1 = {
	.constraints = {
		.name           = "ACT_LDO1",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8931_ldo1_supply),
	.consumer_supplies =  act8931_ldo1_supply,
};

/* */
static struct regulator_init_data act8931_ldo2 = {
	.constraints = {
		.name           = "ACT_LDO2",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8931_ldo2_supply),
	.consumer_supplies =  act8931_ldo2_supply,
};

/* */
static struct regulator_init_data act8931_ldo3 = {
	.constraints = {
		.name           = "ACT_LDO3",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8931_ldo3_supply),
	.consumer_supplies =  act8931_ldo3_supply,
};

/* */
static struct regulator_init_data act8931_ldo4 = {
	.constraints = {
		.name           = "ACT_LDO1",
		.min_uV			= 600000,
		.max_uV			= 3900000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(act8931_ldo4_supply),
	.consumer_supplies =  act8931_ldo4_supply,
};

static struct act8931_regulator_subdev act8931_regulator_subdev_id[] = {
	{
		.id=0,
		.initdata=&act8931_ldo1,		
	 },

	{
		.id=1,
		.initdata=&act8931_ldo2,		
	 },

	{
		.id=2,
		.initdata=&act8931_ldo3,		
	 },

	{
		.id=3,
		.initdata=&act8931_ldo4,		
	 },

	{
		.id=4,
		.initdata=&act8931_buck1,		
	 },

	{
		.id=5,
		.initdata=&act8931_buck2,		
	 },
	{
		.id=6,
		.initdata=&act8931_buck3,		
	 },

};

static struct act8931_platform_data act8931_data={
	.set_init=act8931_set_init,
	.num_regulators=7,
	.regulators=act8931_regulator_subdev_id,
};

#ifdef CONFIG_HAS_EARLYSUSPEND
void act8931_early_suspend(struct early_suspend *h)
{
#if CONFIG_RK_CONFIG
        port_output_off(chg_sel);
#else
	gpio_direction_output(ACT8931_CHGSEL_PIN, !ACT8931_CHGSEL_VALUE);
#endif
}

void act8931_late_resume(struct early_suspend *h)
{
#if CONFIG_RK_CONFIG
        port_output_on(chg_sel);
#else
	gpio_direction_output(ACT8931_CHGSEL_PIN, ACT8931_CHGSEL_VALUE);
#endif
}
#endif

#endif

