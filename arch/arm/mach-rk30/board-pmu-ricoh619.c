#include <linux/regulator/machine.h>
#include <linux/mfd/ricoh619.h>
#include <linux/regulator/ricoh619-regulator.h>
#include <linux/power/ricoh619_battery.h>
#include <linux/rtc/rtc-ricoh619.h>
#include <mach/sram.h>
#include <linux/platform_device.h>

#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>

#ifdef CONFIG_MFD_RICOH619

static struct ricoh619 *Ricoh619;
static int ricoh619_pre_init(struct ricoh619 *ricoh619){

	Ricoh619 = 	ricoh619;
	printk("%s,line=%d\n", __func__,__LINE__);	
	uint8_t cont;
	int ret;

	ret = ricoh619_set_bits(ricoh619->dev,RICOH619_PWR_REP_CNT,(1 << 0));  //set restart when power off

	/**********set dcdc mode when in sleep mode **************/
	
	/*****************************************************/

	return 0;
  }
static int ricoh619_post_init(struct ricoh619 *ricoh619)
{
	struct regulator *dcdc;
	struct regulator *ldo;
	int i = 0,ret=0;
	printk("%s,line=%d\n", __func__,__LINE__);

	#ifndef CONFIG_RK_CONFIG
	g_pmic_type = PMIC_TYPE_RICOH619;
	#endif
	printk("%s:g_pmic_type=%d\n",__func__,g_pmic_type);
	
	for(i = 0; i < ARRAY_SIZE(ricoh619_dcdc_info); i++)
	{

                if(ricoh619_dcdc_info[i].min_uv == 0 && ricoh619_dcdc_info[i].max_uv == 0)
                        continue;
	        dcdc =regulator_get(NULL, ricoh619_dcdc_info[i].name);
	        regulator_set_voltage(dcdc, ricoh619_dcdc_info[i].min_uv, ricoh619_dcdc_info[i].max_uv);
		 regulator_set_suspend_voltage(dcdc, ricoh619_dcdc_info[i].suspend_vol);
		 regulator_set_mode(dcdc, REGULATOR_MODE_NORMAL);
	        regulator_enable(dcdc);
	        printk("%s  %s =%duV end\n", __func__,ricoh619_dcdc_info[i].name, regulator_get_voltage(dcdc));
	        regulator_put(dcdc);
	        udelay(100);
	}
	
	for(i = 0; i < ARRAY_SIZE(ricoh619_ldo_info); i++)
	{
                if(ricoh619_ldo_info[i].min_uv == 0 && ricoh619_ldo_info[i].max_uv == 0)
                        continue;
	        ldo =regulator_get(NULL, ricoh619_ldo_info[i].name);
	        regulator_set_voltage(ldo, ricoh619_ldo_info[i].min_uv, ricoh619_ldo_info[i].max_uv);
	        regulator_enable(ldo);
	        printk("%s  %s =%duV end\n", __func__,ricoh619_ldo_info[i].name, regulator_get_voltage(ldo));
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
	
	ret = ricoh619_clr_bits(ricoh619->dev,0xb1,(7<< 0));  //set vbatdec voltage  3.0v
	ret = ricoh619_set_bits(ricoh619->dev,0xb1,(3<< 0));  //set vbatdec voltage 3.0v
	
	printk("%s,line=%d END\n", __func__,__LINE__);
	
	return 0;
}
static struct regulator_consumer_supply ricoh619_dcdc1_supply[] = {
	{
		.supply = "ricoh_dc1",
	},
	{
		.supply = "vdd_cpu",
	},
	
};
static struct regulator_consumer_supply ricoh619_dcdc2_supply[] = {
	{
		.supply = "ricoh_dc2",
	},
	{
		.supply = "vdd_core",
	},
	
};
static struct regulator_consumer_supply ricoh619_dcdc3_supply[] = {
	{
		.supply = "ricoh_dc3",
	},
};

static struct regulator_consumer_supply ricoh619_dcdc4_supply[] = {
	{
		.supply = "ricoh_dc4",
	},
};

static struct regulator_consumer_supply ricoh619_dcdc5_supply[] = {
	{
		.supply = "ricoh_dc5",
	},
};

static struct regulator_consumer_supply ricoh619_ldo1_supply[] = {
	{
		.supply = "ricoh_ldo1",
	},
};
static struct regulator_consumer_supply ricoh619_ldo2_supply[] = {
	{
		.supply = "ricoh_ldo2",
	},
};

static struct regulator_consumer_supply ricoh619_ldo3_supply[] = {
	{
		.supply = "ricoh_ldo3",
	},
};
static struct regulator_consumer_supply ricoh619_ldo4_supply[] = {
	{
		.supply = "ricoh_ldo4",
	},
};
static struct regulator_consumer_supply ricoh619_ldo5_supply[] = {
	{
		.supply = "ricoh_ldo5",
	},
};
static struct regulator_consumer_supply ricoh619_ldo6_supply[] = {
	{
		.supply = "ricoh_ldo6",
	},
};
static struct regulator_consumer_supply ricoh619_ldo7_supply[] = {
	{
		.supply = "ricoh_ldo7",
	},
};
static struct regulator_consumer_supply ricoh619_ldo8_supply[] = {
	{
		.supply = "ricoh_ldo8",
	},
};
static struct regulator_consumer_supply ricoh619_ldo9_supply[] = {
	{
		.supply = "ricoh_ldo9",
	},
};
static struct regulator_consumer_supply ricoh619_ldo10_supply[] = {
	{
		.supply = "ricoh_ldo10",
	},
};
static struct regulator_consumer_supply ricoh619_ldortc1_supply[] = {
	{
		.supply = "ricoh_ldortc1",
	},
};

static struct regulator_init_data ricoh619_dcdc1 = {
	.constraints = {
		.name           = "RICOH_DC1",
		.min_uV			= 600000,
		.max_uV			= 3500000,
		.apply_uV		= 1,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL | REGULATOR_MODE_FAST,

	},
	.num_consumer_supplies = ARRAY_SIZE(ricoh619_dcdc1_supply),
	.consumer_supplies =  ricoh619_dcdc1_supply,
};

/* */
static struct regulator_init_data ricoh619_dcdc2 = {
	.constraints = {
		.name           = "RICOH_DC2",
		.min_uV			= 600000,
		.max_uV			= 3500000,
		.apply_uV		= 1,
	
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL | REGULATOR_MODE_FAST,

	},
	.num_consumer_supplies = ARRAY_SIZE(ricoh619_dcdc2_supply),
	.consumer_supplies =  ricoh619_dcdc2_supply,
};

/* */
static struct regulator_init_data ricoh619_dcdc3 = {
	.constraints = {
		.name           = "RICOH_DC3",
		.min_uV			= 600000,
		.max_uV			= 3500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL | REGULATOR_MODE_FAST,

	},
	.num_consumer_supplies = ARRAY_SIZE(ricoh619_dcdc3_supply),
	.consumer_supplies =  ricoh619_dcdc3_supply,
};

static struct regulator_init_data ricoh619_dcdc4 = {
	.constraints = {
		.name           = "RICOH_DC4",
		.min_uV			= 600000,
		.max_uV			= 3500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL | REGULATOR_MODE_FAST,

	},
	.num_consumer_supplies = ARRAY_SIZE(ricoh619_dcdc4_supply),
	.consumer_supplies =  ricoh619_dcdc4_supply,
};
static struct regulator_init_data ricoh619_dcdc5 = {
	.constraints = {
		.name           = "RICOH_DC5",
		.min_uV			= 600000,
		.max_uV			= 3500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL | REGULATOR_MODE_FAST,

	},
	.num_consumer_supplies = ARRAY_SIZE(ricoh619_dcdc5_supply),
	.consumer_supplies =  ricoh619_dcdc5_supply,
};

static struct regulator_init_data ricoh619_ldo1 = {
	.constraints = {
		.name           = "RICOH_LDO1",
		.min_uV			= 900000,
		.max_uV			= 3500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(ricoh619_ldo1_supply),
	.consumer_supplies =  ricoh619_ldo1_supply,
};

/* */
static struct regulator_init_data ricoh619_ldo2 = {
	.constraints = {
		.name           = "RICOH_LDO2",
		.min_uV			= 900000,
		.max_uV			= 3500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(ricoh619_ldo2_supply),
	.consumer_supplies =  ricoh619_ldo2_supply,
};

/* */
static struct regulator_init_data ricoh619_ldo3 = {
	.constraints = {
		.name           = "RICOH_LDO3",
		.min_uV			= 900000,
		.max_uV			= 3500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(ricoh619_ldo3_supply),
	.consumer_supplies =  ricoh619_ldo3_supply,
};

/* */
static struct regulator_init_data ricoh619_ldo4 = {
	.constraints = {
		.name           = "RICOH_LDO4",
		.min_uV			= 900000,
		.max_uV			= 3500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(ricoh619_ldo4_supply),
	.consumer_supplies =  ricoh619_ldo4_supply,
};

/* */
static struct regulator_init_data ricoh619_ldo5 = {
	.constraints = {
		.name           = "RICOH_LDO5",
		.min_uV			= 900000,
		.max_uV			= 3500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(ricoh619_ldo5_supply),
	.consumer_supplies =  ricoh619_ldo5_supply,
};

static struct regulator_init_data ricoh619_ldo6 = {
	.constraints = {
		.name           = "RICOH_LDO6",
		.min_uV			= 900000,
		.max_uV			= 3500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(ricoh619_ldo6_supply),
	.consumer_supplies =  ricoh619_ldo6_supply,
};

static struct regulator_init_data ricoh619_ldo7 = {
	.constraints = {
		.name           = "RICOH_LDO7",
		.min_uV			= 900000,
		.max_uV			= 3500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(ricoh619_ldo7_supply),
	.consumer_supplies =  ricoh619_ldo7_supply,
};

static struct regulator_init_data ricoh619_ldo8 = {
	.constraints = {
		.name           = "RICOH_LDO8",
		.min_uV			= 900000,
		.max_uV			= 3500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(ricoh619_ldo8_supply),
	.consumer_supplies =  ricoh619_ldo8_supply,
};

static struct regulator_init_data ricoh619_ldo9 = {
	.constraints = {
		.name           = "RICOH_LDO9",
		.min_uV			= 900000,
		.max_uV			= 3500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(ricoh619_ldo9_supply),
	.consumer_supplies =  ricoh619_ldo9_supply,
};

static struct regulator_init_data ricoh619_ldo10 = {
	.constraints = {
		.name           = "RICOH_LDO10",
		.min_uV			= 900000,
		.max_uV			= 3500000,
		.apply_uV		= 1,
		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(ricoh619_ldo10_supply),
	.consumer_supplies =  ricoh619_ldo10_supply,
};

/* */
static struct regulator_init_data ricoh619_ldortc1 = {
	.constraints = {
		.name           = "RICOH_LDORTC1",
		.min_uV			= 1700000,
		.max_uV			= 3500000,
		.apply_uV		= 1,
  
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,

	},
	.num_consumer_supplies = ARRAY_SIZE(ricoh619_ldortc1_supply),
	.consumer_supplies =  ricoh619_ldortc1_supply,
};

static struct ricoh619_battery_platform_data ricoh619_power_battery = {
	.irq = IRQ_BOARD_BASE,
	.alarm_vol_mv = 3300,
	.multiple =0,
	.monitor_time = 1,
};

static struct rtc_time rk_time = {	//	2012.1.1 12:00:00 Saturday
			.tm_wday = 6,
			.tm_year = 111,
			.tm_mon = 0,
			.tm_mday = 1,
			.tm_hour = 12,
			.tm_min = 0,
			.tm_sec = 0,
};

static struct ricoh619_rtc_platform_data ricoh619_rtc_data = {
	.irq = IRQ_BOARD_BASE ,
	.time = &rk_time,
};

#define RICOH_REG(_id, _data)  \
{ \
		.id = RICOH619_ID_##_id, \
		.name = "ricoh619-regulator", \
		.platform_data = _data, \
} \

static struct ricoh619_pwrkey_platform_data ricoh619_pwrkey_data= { 
	.irq = IRQ_BOARD_BASE + RICOH619_IRQ_POWER_ON, 
	.delay_ms = 20, 
}; 

static struct ricoh619_subdev_info ricoh619_devs[] = {
	RICOH_REG(DC1, &ricoh619_dcdc1),
	RICOH_REG(DC2, &ricoh619_dcdc2),
	RICOH_REG(DC3, &ricoh619_dcdc3),
	RICOH_REG(DC4, &ricoh619_dcdc4),
	RICOH_REG(DC5, &ricoh619_dcdc5),
	
	RICOH_REG(LDO1, &ricoh619_ldo1),
	RICOH_REG(LDO2, &ricoh619_ldo2),
	RICOH_REG(LDO3, &ricoh619_ldo3),
	RICOH_REG(LDO4, &ricoh619_ldo4),
	RICOH_REG(LDO5, &ricoh619_ldo5),
	RICOH_REG(LDO6, &ricoh619_ldo6),
	RICOH_REG(LDO7, &ricoh619_ldo7),
	RICOH_REG(LDO8, &ricoh619_ldo8),
	RICOH_REG(LDO9, &ricoh619_ldo9),
	RICOH_REG(LDO10, &ricoh619_ldo10),
	RICOH_REG(LDORTC1, &ricoh619_ldortc1),
	
	{					
		.id = 16,  
		.name ="ricoh619-battery",
		.platform_data = &ricoh619_power_battery,
	},
	
	{					
		.id = 17,  
		.name ="rtc_ricoh619",
		.platform_data = &ricoh619_rtc_data,
	},
	
	{					
		.id = 18,  
		.name ="ricoh619-pwrkey",
		.platform_data = &ricoh619_pwrkey_data,
	},
	
};

#define RICOH_GPIO_INIT(_init_apply, _output_mode, _output_val, _led_mode, _led_func)  \
{  \
	.output_mode_en = _output_mode,  \
	.output_val = _output_val,  \
	.init_apply = _init_apply,   \
	.led_mode = _led_mode,  \
	.led_func = _led_func,  \
}  \

struct ricoh619_gpio_init_data ricoh_gpio_data[] = { 
	RICOH_GPIO_INIT(0, 1, 0, 0, 1), 
	RICOH_GPIO_INIT(0, 0, 0, 0, 0), 
	RICOH_GPIO_INIT(0, 0, 0, 0, 0), 
	RICOH_GPIO_INIT(0, 0, 0, 0, 0), 
	RICOH_GPIO_INIT(0, 0, 0, 0, 0), 
}; 

static struct ricoh619_platform_data ricoh619_data={
	.irq_base = IRQ_BOARD_BASE,
//	.init_port  = RICOH619_HOST_IRQ,
	.num_subdevs	= ARRAY_SIZE(ricoh619_devs),
	.subdevs	= ricoh619_devs,
	.pre_init = ricoh619_pre_init,
	.post_init = ricoh619_post_init,
	//.gpio_base = RICOH619_GPIO_EXPANDER_BASE, 
	.gpio_init_data = ricoh_gpio_data, 
	.num_gpioinit_data = ARRAY_SIZE(ricoh_gpio_data),
	.enable_shutdown_pin = 0, 
};

void __sramfunc board_pmu_ricoh619_suspend(void)
{	
	#ifdef CONFIG_CLK_SWITCH_TO_32K
	 sram_gpio_set_value(pmic_sleep, GPIO_HIGH);  
	#endif
}
void __sramfunc board_pmu_ricoh619_resume(void)
{
	#ifdef CONFIG_CLK_SWITCH_TO_32K
 	sram_gpio_set_value(pmic_sleep, GPIO_LOW);  
	sram_udelay(2000);
	#endif
}


#endif




