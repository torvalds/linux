#include <linux/regulator/machine.h>
#include <mach/sram.h>
#include <linux/platform_device.h>

#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>

#include <linux/mfd/rt5025.h>

#ifdef CONFIG_MFD_RT5025

static int rt5025_pre_init(struct rt5025_chip *rt5025_chip){

	
	printk("%s,line=%d\n", __func__,__LINE__);	
	int ret;
	/**********set voltage speed***********************/
	ret = rt5025_reg_read(rt5025_chip->i2c, 0x08);
	ret &= (~(3<<0));  //dcdc1 25mv/10us
	rt5025_reg_write(rt5025_chip->i2c, 0x08,ret);
	
	ret = rt5025_reg_read(rt5025_chip->i2c, 0x09);
	ret &= (~(3<<0));//dcdc2 100mv/10us
	rt5025_reg_write(rt5025_chip->i2c, 0x09,ret);
	
	ret = rt5025_reg_read(rt5025_chip->i2c, 0x0a);
	ret &= (~(3<<0));//dcdc3 50mv/12us
	rt5025_reg_write(rt5025_chip->i2c, 0x0a,ret);
	/************************************************/
	/***************set power off voltage***************/
	ret = rt5025_reg_read(rt5025_chip->i2c, 0x17);
	ret &= (~(7<<5));  //power off 2.8v
	rt5025_reg_write(rt5025_chip->i2c, 0x17,ret);

	ret = rt5025_reg_read(rt5025_chip->i2c, 0x17);
	ret |= (1<<3);  //enable DC4 boost
	rt5025_reg_write(rt5025_chip->i2c, 0x17,ret);
	/***********************************************/
	/************************************************/
	return 0;
  }
static int rt5025_post_init(void)
{
	struct regulator *dcdc;
	struct regulator *ldo;
	int i = 0;
	printk("%s,line=%d\n", __func__,__LINE__);

	#ifndef CONFIG_RK_CONFIG
	g_pmic_type = PMIC_TYPE_RT5025;
	#endif
	printk("%s:g_pmic_type=%d\n",__func__,g_pmic_type);
	
	for(i = 0; i < ARRAY_SIZE(rt5025_dcdc_info); i++)
	{
                if(rt5025_dcdc_info[i].min_uv == 0 && rt5025_dcdc_info[i].max_uv == 0)
                        continue;
	        dcdc =regulator_get(NULL, rt5025_dcdc_info[i].name);

	        regulator_set_voltage(dcdc, rt5025_dcdc_info[i].min_uv, rt5025_dcdc_info[i].max_uv);

		regulator_set_mode(dcdc, REGULATOR_MODE_NORMAL);
	        regulator_enable(dcdc);
	        printk("%s  %s =%duV end\n", __func__,rt5025_dcdc_info[i].name, regulator_get_voltage(dcdc));
	        regulator_put(dcdc);
	        udelay(100);
	}
	
	for(i = 0; i < ARRAY_SIZE(rt5025_ldo_info); i++)
	{
                if(rt5025_ldo_info[i].min_uv == 0 && rt5025_ldo_info[i].max_uv == 0)
                        continue;
	        ldo =regulator_get(NULL, rt5025_ldo_info[i].name);
	        regulator_set_voltage(ldo, rt5025_ldo_info[i].min_uv, rt5025_ldo_info[i].max_uv);
	        regulator_enable(ldo);
	        printk("%s  %s =%duV end\n", __func__,rt5025_ldo_info[i].name, regulator_get_voltage(ldo));
	        regulator_put(ldo);
	}
	
	printk("%s,line=%d END\n", __func__,__LINE__);
	
	return 0;
}

 int rt5025_set_otg_enable(int enable)
{
	if (enable)
		rt5025_ext_set_charging_buck(0);
	else
		rt5025_ext_set_charging_buck(1);
	return 0;
}

static struct regulator_consumer_supply rt5025_dcdc1_supply[] = {
	{
		.supply = "rt5025-dcdc1",
	},
	{
		.supply = "vdd_cpu",
	},
	
};
static struct regulator_consumer_supply rt5025_dcdc2_supply[] = {
	{
		.supply = "rt5025-dcdc2",
	},
	{
		.supply = "vdd_core",
	},
};
static struct regulator_consumer_supply rt5025_dcdc3_supply[] = {
	{
		.supply = "rt5025-dcdc3",
	},
};

static struct regulator_consumer_supply rt5025_dcdc4_supply[] = {
	{
		.supply = "rt5025-dcdc4",
	},
};

static struct regulator_consumer_supply rt5025_ldo1_supply[] = {
	{
		.supply = "rt5025-ldo1",
	},
};
static struct regulator_consumer_supply rt5025_ldo2_supply[] = {
	{
		.supply = "rt5025-ldo2",
	},
};

static struct regulator_consumer_supply rt5025_ldo3_supply[] = {
	{
		.supply = "rt5025-ldo3",
	},
};
static struct regulator_consumer_supply rt5025_ldo4_supply[] = {
	{
		.supply = "rt5025-ldo4",
	},
};
static struct regulator_consumer_supply rt5025_ldo5_supply[] = {
	{
		.supply = "rt5025-ldo5",
	},
};
static struct regulator_consumer_supply rt5025_ldo6_supply[] = {
	{
		.supply = "rt5025-ldo6",
	},
};

static struct regulator_init_data rt5025_dcdc1_info = {
	.constraints = {
		.name           = "RT5025-DCDC1",
		.min_uV =  700000,
		.max_uV = 2275000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_FAST,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS| REGULATOR_CHANGE_MODE,
	},
	.num_consumer_supplies = ARRAY_SIZE(rt5025_dcdc1_supply),
	.consumer_supplies =  rt5025_dcdc1_supply,
};

static struct regulator_init_data rt5025_dcdc2_info = {
	.constraints = {
		.name           = "RT5025-DCDC2",
		.min_uV =  700000,
		.max_uV = 3500000,
		.valid_modes_mask =  REGULATOR_MODE_NORMAL | REGULATOR_MODE_FAST,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS  | REGULATOR_CHANGE_MODE,
	},
	.num_consumer_supplies = ARRAY_SIZE(rt5025_dcdc2_supply),
	.consumer_supplies =  rt5025_dcdc2_supply,
};

static struct regulator_init_data rt5025_dcdc3_info = {
	.constraints = {
		.name           = "RT5025-DCDC3",
		.min_uV =  700000,
		.max_uV = 3500000,
		.valid_modes_mask =  REGULATOR_MODE_NORMAL | REGULATOR_MODE_FAST,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS  | REGULATOR_CHANGE_MODE,
	},
	.num_consumer_supplies = ARRAY_SIZE(rt5025_dcdc3_supply),
	.consumer_supplies =  rt5025_dcdc3_supply,
};

static struct regulator_init_data rt5025_dcdc4_info = {
	.constraints = {
		.name           = "RT5025-DCDC4",
		.min_uV = 4500000,
		.max_uV = 5500000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(rt5025_dcdc4_supply),
	.consumer_supplies =  rt5025_dcdc4_supply,
};

static struct regulator_init_data rt5025_ldo1_info = {
	.constraints = {
		.name           = "RT5025-LDO1",
		.min_uV =  700000,
		.max_uV = 3500000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(rt5025_ldo1_supply),
	.consumer_supplies =  rt5025_ldo1_supply,
};

static struct regulator_init_data rt5025_ldo2_info = {
	.constraints = {
		.name           = "RT5025-LDO2",
		.min_uV =  700000,
		.max_uV = 3500000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(rt5025_ldo2_supply),
	.consumer_supplies =  rt5025_ldo2_supply,
};

static struct regulator_init_data rt5025_ldo3_info = {
	.constraints = {
		.name           = "RT5025-LDO3",
		.min_uV = 1000000,
		.max_uV = 3300000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(rt5025_ldo3_supply),
	.consumer_supplies =  rt5025_ldo3_supply,
};

static struct regulator_init_data rt5025_ldo4_info = {
	.constraints = {
		.name           = "RT5025-LDO4",
		.min_uV = 1000000,
		.max_uV = 3300000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(rt5025_ldo4_supply),
	.consumer_supplies =  rt5025_ldo4_supply,
};

static struct regulator_init_data rt5025_ldo5_info = {
	.constraints = {
		.name           = "RT5025-LDO5",
		.min_uV = 1000000,
		.max_uV = 3300000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(rt5025_ldo5_supply),
	.consumer_supplies =  rt5025_ldo5_supply,
};

static struct regulator_init_data rt5025_ldo6_info = {
	.constraints = {
		.name           = "RT5025-LDO6",
		.min_uV = 1000000,
		.max_uV = 3300000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(rt5025_ldo6_supply),
	.consumer_supplies =  rt5025_ldo6_supply,
};

static struct rt5025_power_data rt5025_power_data = {
	.CHGControl2 = {
		.bitfield = {
			.CHGBC_EN = 1,
			.TE = 1,
			.CCCHG_TIMEOUT = RT5025_CCCHG_TO_10H,
			.PRECHG_TIMEOUT = RT5025_PRECHG_TO_60M,
		},
	},
	.CHGControl3 = {
		.bitfield = {
			.VOREG = 0x23,
		},
	},
	.CHGControl4 = {
		.bitfield = {
			.AICR_CON = 1,
			.AICR = RT5025_AICR_1A,
			.ICC = RT5025_ICC_1A,
		},
	},
	.CHGControl5 = {
		.bitfield = {
			.DPM = RT5025_DPM_4P5V,
		},
	},
	.CHGControl6 = {
		.bitfield = {
			.IPREC = RT5025_IPREC_20P,
			.IEOC = RT5025_IEOC_10P,
			.VPREC = RT5025_VPREC_3V,
		},
	},
	.CHGControl7 = {
		.bitfield = {
			.CHGC_EN = 1,
			.CHG_DCDC_MODE = 0,
			.BATD_EN = 0,
		},
	},
//	.fcc = 6200, //6200 mAh
};

static struct rt5025_gpio_data rt5025_gpio_data = {
//	.gpio_base = RT5025_GPIO_BASE,
	.irq_base = IRQ_BOARD_BASE,
};

static struct rt5025_misc_data rt5025_misc_data = {
	.RSTCtrl = {
		.bitfield = {
			.Action = 2,
			.Delayed1 = RT5025_RSTDELAY1_1S,
			.Delayed2 = RT5025_RSTDELAY2_1S,
		},
	},
	.VSYSCtrl = {
		.bitfield = {
			.VOFF = RT5025_VOFF_3P0V,
		},
	},
	.PwrOnCfg = {
		.bitfield = {
			.PG_DLY = RT5025_PGDLY_100MS,
			.SHDN_PRESS = RT5025_SHDNPRESS_6S,
			.LPRESS_TIME = RT5025_LPRESS_1P5S,
			.START_TIME = RT5025_STARTIME_100MS,
		},
	},
	.SHDNCtrl = {
		.bitfield = {
			.SHDN_DLYTIME = RT5025_SHDNDLY_1S,
			.SHDN_TIMING = 1,
			.SHDN_CTRL = 0,
		},
	},
	.PwrOffCond = {
		.bitfield = {
			.OT_ENSHDN = 1,
			.PWRON_ENSHDN = 1,
			.DCDC3LV_ENSHDN = 0,
			.DCDC2LV_ENSHDN = 0,
			.DCDC1LV_ENSHDN = 0,
			.SYSLV_ENSHDN = 1,
		},
	},
};

static struct rt5025_irq_data rt5025_irq_data = {
	.irq_enable1 = {
		.bitfield = {
			.BATABS = 0,
			.INUSB_PLUGIN = 1,
			.INUSBOVP = 1,
			.INAC_PLUGIN = 1,
			.INACOVP = 1,
		},
	},
	.irq_enable2 = {
		.bitfield = {
			.CHTERMI = 1,
			.CHBATOVI = 1,
			.CHGOODI_INUSB = 0,
			.CHBADI_INUSB = 0,
			.CHSLPI_INUSB = 1,
			.CHGOODI_INAC = 0,
			.CHBADI_INAC = 0,
			.CHSLPI_INAC = 1,
		},
	},
	.irq_enable3 = {
		.bitfield = {
			.TIMEOUT_CC = 0,
			.TIMEOUT_PC = 0,
			.CHVSREGI = 0,
			.CHTREGI = 0,
			.CHRCHGI = 1,
		},
	},
	.irq_enable4 = {
		.bitfield = {
			.SYSLV = 0,
			.DCDC4LVHV = 0,
			.PWRONLP = 0,
			.PWRONSP = 0,
			.DCDC3LV = 0,
			.DCDC2LV = 0,
			.DCDC1LV = 0,
			.OT = 1,
		},
	},
	.irq_enable5 = {
		.bitfield = {
			.GPIO0_IE = 0,
			.GPIO1_IE = 0,
			.GPIO2_IE = 0,
			.RESETB = 1,
			.PWRONF = 0,
			.PWRONR = 0,
			.KPSHDN = 1,
		},
	},
};

//temp unit: 'c*10 degree
static int jeita_temp[4] = { 0, 150, 500, 600};
			     //-5',  5',   15', 20',   45'   55'   55',  65'
static u8 jeita_scalar[8] = { 0x30, 0x2B, 0x25, 0x20, 0x15, 0x10, 0x10, 0x0D };
//cc unit: xxx mA
static int jeita_temp_cc[][5] = {{ 500,  500,  500,  500, 500},    // not plugin
			   	 {   0 , 500,  500,  500,   0},    // normal USB
			   	 {   0,  1000, 2000, 1000,   0},    // USB charger
				 {   0,  1000, 2000, 1000,   0}};   // AC Adapter
//cv unit: xxx mV
static int jeita_temp_cv[][5] = {{ 4200, 4200, 4200, 4200, 4200},  // not plugin
				 { 4200, 4200, 4200, 4200, 4200},  // normal USB
				 { 4200, 4200, 4200, 4200, 4200},  // USB charger
				 { 4200, 4200, 4200, 4200, 4200}}; // AC Adapter

static struct rt5025_jeita_data rt5025_jeita_data = {
	.temp = jeita_temp,
	.temp_scalar = jeita_scalar,
	.temp_cc = jeita_temp_cc,
	.temp_cv = jeita_temp_cv,
};

static void rt5025_charger_event_callback(uint32_t detected)
{
	RTINFO("charger event detected = 0x%08x\n", detected);
	if (detected & CHG_EVENT_CHTERMI)
	{
		pr_info("charger termination OK\n");
	}
}

static void rt5025_power_event_callback(uint32_t detected)
{
	RTINFO("power event detected = 0x%08x\n", detected);
}

static struct rt5025_event_callback rt5025_event_callback = {
	.charger_event_callback = rt5025_charger_event_callback,
	.power_event_callkback = rt5025_power_event_callback,
};

static struct rt5025_platform_data rt5025_data = {
	.pre_init=rt5025_pre_init,
	.post_init=rt5025_post_init,
	.regulator = {
		&rt5025_dcdc1_info,
		&rt5025_dcdc2_info,
		&rt5025_dcdc3_info,
		&rt5025_dcdc4_info,
		&rt5025_ldo1_info,
		&rt5025_ldo2_info,
		&rt5025_ldo3_info,
		&rt5025_ldo4_info,
		&rt5025_ldo5_info,
		&rt5025_ldo6_info,
	},
	.power_data = &rt5025_power_data,
	.gpio_data = &rt5025_gpio_data,
	.misc_data = &rt5025_misc_data,
	.irq_data = &rt5025_irq_data,
	.jeita_data = &rt5025_jeita_data,
	.cb = &rt5025_event_callback,
	.intr_pin = 81, //GPIO81
};

void __sramfunc board_pmu_rt5025_suspend(void)
{	
}
void __sramfunc board_pmu_rt5025_resume(void)
{
}


#endif




