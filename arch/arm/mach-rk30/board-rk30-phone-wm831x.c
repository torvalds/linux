#include <linux/regulator/machine.h>
#include <linux/mfd/wm831x/pdata.h>
#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/gpio.h>

/* wm8326 pmu*/
#if defined(CONFIG_GPIO_WM831X)
static struct rk29_gpio_expander_info wm831x_gpio_settinginfo[] = {
	{
		.gpio_num = WM831X_P01,	// tp3
		.pin_type = GPIO_IN,
	},
	{
		.gpio_num = WM831X_P02,	//tp4
		.pin_type = GPIO_IN,
	},
	{
		.gpio_num = WM831X_P03,	//tp2
		.pin_type = GPIO_OUT,
		.pin_value = GPIO_HIGH,
	},
	{
		.gpio_num = WM831X_P04,	//tp1
		.pin_type = GPIO_IN,
	},
	{
		.gpio_num = WM831X_P05,	//tp1
		.pin_type = GPIO_IN,
	},
	{
		.gpio_num = WM831X_P06,	//tp1
		.pin_type = GPIO_OUT,
		.pin_value = GPIO_HIGH,
	},
	{
		.gpio_num = WM831X_P07,	//tp1
		.pin_type = GPIO_IN,
	},
	{
		.gpio_num = WM831X_P08,	//tp1
		.pin_type = GPIO_OUT,
		.pin_value = GPIO_HIGH,
	},
	{
		.gpio_num = WM831X_P09,	//tp1
		.pin_type = GPIO_OUT,
		.pin_value = GPIO_HIGH,
	},
	{
		.gpio_num = WM831X_P10,	//tp1
		.pin_type = GPIO_IN,
	},
	{
		.gpio_num = WM831X_P11,	//tp1
		.pin_type = GPIO_OUT,
		.pin_value = GPIO_HIGH,
	},
	{
		.gpio_num = WM831X_P12,
		.pin_type = GPIO_OUT,
		.pin_value = GPIO_HIGH,
	},
};
#endif

#if defined(CONFIG_MFD_WM831X)

#define UNLOCK_SECURITY_KEY     ~(0x1<<5)
#define LOCK_SECURITY_KEY       0x00
#define PMU_POWER_SLEEP RK30_PIN6_PB1
static struct wm831x *Wm831x;

static int wm831x_pre_init(struct wm831x *parm)
{
	int ret;
	Wm831x = parm;
//	printk("%s\n", __func__);
	gpio_request(PMU_POWER_SLEEP, "NULL");
	gpio_direction_output(PMU_POWER_SLEEP, GPIO_HIGH);
	
	ret = wm831x_reg_read(parm, WM831X_POWER_STATE) & 0xffff;
	wm831x_reg_write(parm, WM831X_POWER_STATE, (ret & 0xfff8) | 0x04);

	//BATT_FET_ENA = 1
	wm831x_reg_write(parm, WM831X_SECURITY_KEY, 0x9716);	// unlock security key
	wm831x_set_bits(parm, WM831X_RESET_CONTROL, 0x1000, 0x1000);
	ret = wm831x_reg_read(parm, WM831X_RESET_CONTROL) & 0xffff & UNLOCK_SECURITY_KEY;	// enternal reset active in sleep
//	printk("%s:WM831X_RESET_CONTROL=0x%x\n", __func__, ret);
	wm831x_reg_write(parm, WM831X_RESET_CONTROL, ret);

	wm831x_set_bits(parm,WM831X_DC1_ON_CONFIG ,0x0300,0x0000); //set dcdc mode is FCCM
	wm831x_set_bits(parm,WM831X_DC2_ON_CONFIG ,0x0300,0x0000);
	wm831x_set_bits(parm,WM831X_DC3_ON_CONFIG ,0x0300,0x0000);
//	wm831x_set_bits(parm,0x4066,0x0300,0x0000);

//	wm831x_set_bits(parm,WM831X_LDO10_CONTROL ,0x0040,0x0040);// set ldo10 in switch mode

	wm831x_set_bits(parm,WM831X_STATUS_LED_1 ,0xc300,0xc100);// set led1 on(in manual mode)
	wm831x_set_bits(parm,WM831X_STATUS_LED_2 ,0xc300,0xc000);//set led2 off(in manual mode)

	wm831x_reg_write(parm, WM831X_SECURITY_KEY, LOCK_SECURITY_KEY);	// lock security key

	return 0;
}

int wm831x_post_init(struct wm831x *Wm831x)
{
	struct regulator *dcdc;
	struct regulator *ldo;


	ldo = regulator_get(NULL, "ldo8");	//vcca33
	regulator_set_voltage(ldo, 3300000, 3300000);
	regulator_set_suspend_voltage(ldo, 3300000);
	regulator_enable(ldo);
//	printk("%s set ldo8 vcca33=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);
	
	ldo = regulator_get(NULL, "ldo3");	// vdd_11
	regulator_set_voltage(ldo, 1100000, 1100000);
	regulator_set_suspend_voltage(ldo, 1100000);
	regulator_enable(ldo);
//	printk("%s set ldo3 vdd_11=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	dcdc = regulator_get(NULL, "dcdc3");	// vcc_io
	regulator_set_voltage(dcdc, 3000000, 3000000);
	regulator_set_suspend_voltage(dcdc, 3000000);
	regulator_enable(dcdc);
//	printk("%s set dcdc3 vcc_io=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	ldo = regulator_get(NULL, "ldo4");	//vdd_usb11
	regulator_set_voltage(ldo, 1100000, 1100000);
	regulator_set_suspend_voltage(ldo, 1100000);
	regulator_enable(ldo);
//	printk("%s set ldo4 vdd_usb11=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo5");	//vcc_25
	regulator_set_voltage(ldo, 2500000, 2500000);
	regulator_set_suspend_voltage(ldo, 2500000);
	regulator_enable(ldo);
//	printk("%s set ldo5 vcc_25=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);


	ldo = regulator_get(NULL, "ldo10");	//vcc_usb33
	regulator_set_voltage(ldo, 3300000, 3300000);
	regulator_set_suspend_voltage(ldo, 3300000);
	regulator_enable(ldo);
//	printk("%s set ldo10 vcc_usb33=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);

	dcdc = regulator_get(NULL, "dcdc1");	// vcc_lpddr2_1v8
	regulator_set_voltage(dcdc, 1800000, 1800000);
	regulator_set_suspend_voltage(dcdc, 1800000);
	regulator_enable(dcdc);
	printk("%s set dcdc1 vcc_lpddr2_1v8=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	dcdc = regulator_get(NULL, "dcdc2");	// vcc_lpddr2_1v2
	regulator_set_voltage(dcdc, 1200000, 1200000);
	regulator_set_suspend_voltage(dcdc, 1200000);
	regulator_enable(dcdc);
	printk("%s set dcdc2 vcc_lpddr2_1v2=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);
	
	ldo = regulator_get(NULL, "ldo2");	// vcc_emmc3v3
	regulator_set_voltage(ldo, 3000000, 3000000);
	regulator_set_suspend_voltage(ldo, 3000000);
	regulator_enable(ldo);
//	printk("%s set ldo2 vcc_emmc3v3=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo6");	// vcc_codec18
	regulator_set_voltage(ldo, 1800000, 1800000);
	regulator_set_suspend_voltage(ldo, 1800000);
	regulator_enable(ldo);
//	printk("%s set ldo6 vcc_codec18=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo1");	// vcc_lcd
	regulator_set_voltage(ldo, 3000000, 3000000);
	regulator_set_suspend_voltage(ldo, 3000000);
	regulator_enable(ldo);
//	printk("%s set ldo1 vcc_lcd=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);
#if 0 

	ldo = regulator_get(NULL, "ldo7");	//vdd_mtv_1v2
	regulator_set_voltage(ldo, 1200000, 1200000);
	regulator_set_suspend_voltage(ldo, 1200000);
	regulator_enable(ldo);
//	printk("%s set ldo7 vdd_mtv_1v2=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);
	
	ldo = regulator_get(NULL, "ldo9");	//vdd_mtv_3v
	regulator_set_voltage(ldo, 3000000, 3000000);
	regulator_set_suspend_voltage(ldo, 3000000);
	regulator_enable(ldo);
//	printk("%s set ldo9 vdd_mtv_3v=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);
#endif

//discrete dcdc device
#ifdef CONFIG_RK30_PWM_REGULATOR
	dcdc = regulator_get(NULL, "vdd_core"); // vdd_log
	regulator_set_voltage(dcdc, 1050000, 1050000);
	regulator_enable(dcdc);
	printk("%s set vdd_core=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	dcdc = regulator_get(NULL, "vdd_cpu");	// vdd_arm
	regulator_set_voltage(dcdc, 1100000, 1100000);
	regulator_enable(dcdc);
	printk("%s set vdd_cpu=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);
#endif
	printk("wm831x_post_init end");
	return 0;
}

static int wm831x_last_deinit(struct wm831x *Wm831x)
{
	struct regulator *ldo;

	printk("%s\n", __func__);
	ldo = regulator_get(NULL, "ldo1");
	regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo2");
	regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo3");
	//regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo4");
	regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo5");
	//regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo6");
	regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo7");
	regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo8");
	//regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo9");
	regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo10");
	regulator_disable(ldo);
	regulator_put(ldo);

	return 0;
}

struct wm831x_backlight_pdata wm831x_backlight_platdata = {
	.isink = 1,     /** ISINK to use, 1 or 2 */
	.max_uA = 19484,    /** Maximum current to allow */
};

struct wm831x_backup_pdata wm831x_backup_platdata = {
	.charger_enable = 1,
	.no_constant_voltage = 0,  /** Disable constant voltage charging */
	.vlim = 3100,   /** Voltage limit in milivolts */
	.ilim = 300,   /** Current limit in microamps */
};

struct wm831x_battery_pdata wm831x_battery_platdata = {
	.enable = 1,         /** Enable charging */
	.fast_enable = 1,    /** Enable fast charging */
	.off_mask = 1,       /** Mask OFF while charging */
	.trickle_ilim = 200,   /** Trickle charge current limit, in mA */
	.vsel = 4200,           /** Target voltage, in mV */
	.eoc_iterm = 50,      /** End of trickle charge current, in mA */
	.fast_ilim = 500,      /** Fast charge current limit, in mA */
	.timeout = 480,        /** Charge cycle timeout, in minutes */
	.syslo = 3300,    /* syslo threshold, in mV*/
	.sysok = 3500,    /* sysko threshold, in mV*/
};

struct wm831x_status_pdata wm831x_status_platdata[WM831X_MAX_STATUS] = {
	{
		.default_src = WM831X_STATUS_OTP,
		.name = "wm831x_status0",
		.default_trigger = "wm831x_otp",
	},
	{
		.default_src = WM831X_STATUS_POWER,
		.name = "wm831x_status1",
		.default_trigger = "wm831x_power",
	},
};

static struct regulator_consumer_supply dcdc1_consumers[] = {
	{
		.supply = "dcdc1",
	}
};

static struct regulator_consumer_supply dcdc2_consumers[] = {
	{
		.supply = "dcdc2",
	}
	
};

static struct regulator_consumer_supply dcdc3_consumers[] = {
	{
		.supply = "dcdc3",
	}
};

static struct regulator_consumer_supply dcdc4_consumers[] = {
	{
		.supply = "dcdc4",
	}
};


static struct regulator_consumer_supply epe1_consumers[] = {
	{
		.supply = "epe1",
	}
};

static struct regulator_consumer_supply epe2_consumers[] = {
	{
		.supply = "epe2",
	}
};


static struct regulator_consumer_supply ldo1_consumers[] = {
	{
		.supply = "ldo1",
	}
};

static struct regulator_consumer_supply ldo2_consumers[] = {
	{
		.supply = "ldo2",
	}
};

static struct regulator_consumer_supply ldo3_consumers[] = {
	{
		.supply = "ldo3",
	}
};

static struct regulator_consumer_supply ldo4_consumers[] = {
	{
		.supply = "ldo4",
	}
};

static struct regulator_consumer_supply ldo5_consumers[] = {
	{
		.supply = "ldo5",
	}
};

static struct regulator_consumer_supply ldo6_consumers[] = {
	{
		.supply = "ldo6",
	}
};

static struct regulator_consumer_supply ldo7_consumers[] = {
	{
		.supply = "ldo7",
	}
};

static struct regulator_consumer_supply ldo8_consumers[] = {
	{
		.supply = "ldo8",
	}
};

static struct regulator_consumer_supply ldo9_consumers[] = {
	{
		.supply = "ldo9",
	}
};

static struct regulator_consumer_supply ldo10_consumers[] = {
	{
		.supply = "ldo10",
	}
};

static struct regulator_consumer_supply ldo11_consumers[] = {
	{
		.supply = "ldo11",
	}
};

static struct regulator_consumer_supply isink1_consumers[] = {
	{
		.supply = "isink1",
	}
};
static struct regulator_consumer_supply isink2_consumers[] = {
	{
		.supply = "isink2",
	}
};
struct regulator_init_data wm831x_regulator_init_dcdc[WM831X_MAX_DCDC] = {
	{
		.constraints = {
			.name = "DCDC1",
			.min_uV = 600000,
			.max_uV = 1800000,	//0.6-1.8V
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(dcdc1_consumers),
		.consumer_supplies = dcdc1_consumers,
	},
	{
		.constraints = {
			.name = "DCDC2",
			.min_uV = 600000,
			.max_uV = 1800000,	//0.6-1.8V
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(dcdc2_consumers),
		.consumer_supplies = dcdc2_consumers,
	},
	{
		.constraints = {
			.name = "DCDC3",
			.min_uV = 850000,
			.max_uV = 3400000,	//0.85-3.4V
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(dcdc3_consumers),
		.consumer_supplies = dcdc3_consumers,
	},
	{
		.constraints = {
			.name = "DCDC4",
			.min_uV = 00000000,
			.max_uV = 30000000,//30V/40mA
			.apply_uV = true,		
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,		
		},
		.num_consumer_supplies = ARRAY_SIZE(dcdc4_consumers),
		.consumer_supplies = dcdc4_consumers,
	},
};

struct regulator_init_data wm831x_regulator_init_epe[WM831X_MAX_EPE] = {
	{
		.constraints = {
			.name = "EPE1",
			.min_uV = 1200000,
			.max_uV = 3000000,
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(epe1_consumers),
		.consumer_supplies = epe1_consumers,
	},
	{
		.constraints = {
			.name = "EPE2",
			.min_uV = 1200000,
			.max_uV = 3000000,
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(epe2_consumers),
		.consumer_supplies = epe2_consumers,
	},
};


struct regulator_init_data wm831x_regulator_init_ldo[WM831X_MAX_LDO] = {
	{
		.constraints = {
			.name = "LDO1",
			.min_uV = 900000,
			.max_uV = 3300000,
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo1_consumers),
		.consumer_supplies = ldo1_consumers,
	},
	{
		.constraints = {
			.name = "LDO2",
			.min_uV = 900000,
			.max_uV = 3300000,
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo2_consumers),
		.consumer_supplies = ldo2_consumers,
	},
	{
		.constraints = {
			.name = "LDO3",
			.min_uV = 900000,
			.max_uV = 3300000,
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo3_consumers),
		.consumer_supplies = ldo3_consumers,
	},
	{
		.constraints = {
			.name = "LDO4",
			.min_uV = 900000,
			.max_uV = 3300000,
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo4_consumers),
		.consumer_supplies = ldo4_consumers,
	},
	{
		.constraints = {
			.name = "LDO5",
			.min_uV = 900000,
			.max_uV = 3300000,
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo5_consumers),
		.consumer_supplies = ldo5_consumers,
	},
	{
		.constraints = {
			.name = "LDO6",
			.min_uV = 900000,
			.max_uV = 3300000,
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo6_consumers),
		.consumer_supplies = ldo6_consumers,
	},
	{
		.constraints = {
			.name = "LDO7",
			.min_uV = 1000000,
			.max_uV = 3500000,
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo7_consumers),
		.consumer_supplies = ldo7_consumers,
	},
	{
		.constraints = {
			.name = "LDO8",
			.min_uV = 1000000,
			.max_uV = 3500000,
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo8_consumers),
		.consumer_supplies = ldo8_consumers,
	},
	{
		.constraints = {
			.name = "LDO9",
			.min_uV = 1000000,
			.max_uV = 3500000,
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo9_consumers),
		.consumer_supplies = ldo9_consumers,
	},
	{
		.constraints = {
			.name = "LDO10",
			.min_uV = 1000000,
			.max_uV = 3500000,
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo10_consumers),
		.consumer_supplies = ldo10_consumers,
	},
	{
		.constraints = {
			.name = "LDO11",
			.min_uV = 800000,
			.max_uV = 1550000,
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ldo11_consumers),
		.consumer_supplies = ldo11_consumers,
	},
};
struct regulator_init_data wm831x_regulator_init_isink[WM831X_MAX_ISINK] = {
	{
		.constraints = {
			.name = "ISINK1",
			.min_uA = 00000,
			.max_uA = 40000,
			.always_on = true,
			.apply_uV = true,		
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_CURRENT,		
		},
		.num_consumer_supplies = ARRAY_SIZE(isink1_consumers),
		.consumer_supplies = isink1_consumers,
	},
	{
		.constraints = {
			.name = "ISINK2",
			.min_uA = 0000000,
			.max_uA = 0000000,
			.apply_uV = false,		
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_CURRENT,		
		},
		.num_consumer_supplies = ARRAY_SIZE(isink2_consumers),
		.consumer_supplies = isink2_consumers,
	},
};
static int wm831x_checkrange(int start,int num,int val)
{   
	if((val<(start+num))&&(val>=start))
		return 0;
	else 
		return -1;
}
static int wm831x_init_pin_type(struct wm831x *wm831x)
{
	struct wm831x_pdata *pdata;
	struct rk29_gpio_expander_info *wm831x_gpio_settinginfo;
	uint16_t wm831x_settingpin_num;
	int i;

	if (!wm831x || !wm831x->dev)
		goto out;

	pdata = wm831x->dev->platform_data;
	if (!pdata)
		goto out;

	wm831x_gpio_settinginfo = pdata->settinginfo;
	if (!wm831x_gpio_settinginfo)
		goto out;

	wm831x_settingpin_num = pdata->settinginfolen;
	for (i = 0; i < wm831x_settingpin_num; i++) {
		if (wm831x_gpio_settinginfo[i].pin_type == GPIO_IN) {
			wm831x_set_bits(wm831x,
					WM831X_GPIO1_CONTROL + i,
					WM831X_GPN_DIR_MASK | WM831X_GPN_TRI_MASK,
					1 << WM831X_GPN_DIR_SHIFT | 1 << WM831X_GPN_TRI_SHIFT);

			wm831x_set_bits(wm831x,
					WM831X_GPIO1_CONTROL + i,
					WM831X_GPN_PULL_MASK, 1 << WM831X_GPN_PULL_SHIFT);	//pull down
			if (i == 0 ) {
				wm831x_set_bits(wm831x,
						WM831X_GPIO1_CONTROL + i,
						WM831X_GPN_PWR_DOM_MASK,
						WM831X_GPN_PWR_DOM);
			}	// set gpiox power domain
			else {
				wm831x_set_bits(wm831x,
						WM831X_GPIO1_CONTROL + i,
						WM831X_GPN_PWR_DOM_MASK,
						~WM831X_GPN_PWR_DOM);
			}
			if (i == 1) {
				wm831x_set_bits(wm831x,
						WM831X_GPIO1_CONTROL + i,
						WM831X_GPN_POL_MASK,
						0x0000);
				wm831x_set_bits(wm831x,
						WM831X_GPIO1_CONTROL + i,
						WM831X_GPN_FN_MASK,
						0x0004);				
			}	// set gpio2 sleep/wakeup
					
		} else {
			wm831x_set_bits(wm831x,
					WM831X_GPIO1_CONTROL + i,
					WM831X_GPN_DIR_MASK | WM831X_GPN_TRI_MASK,
					1 << WM831X_GPN_TRI_SHIFT);
			if (wm831x_gpio_settinginfo[i].pin_value == GPIO_HIGH) {
				wm831x_set_bits(wm831x, WM831X_GPIO_LEVEL, 1 << i, 1 << i);
			} else {
				wm831x_set_bits(wm831x, WM831X_GPIO_LEVEL, 1 << i, 0 << i);
			}
			if (i == 2) {
				wm831x_set_bits(wm831x,
						WM831X_GPIO1_CONTROL + i,
						WM831X_GPN_PWR_DOM_MASK | WM831X_GPN_POL_MASK |WM831X_GPN_FN_MASK,
						1 << WM831X_GPN_POL_SHIFT | 1 << WM831X_GPN_PWR_DOM_SHIFT | 1 << 0);

			}	// set gpio3 as clkout output 32.768K
			else {
				wm831x_set_bits(wm831x,
						WM831X_GPIO1_CONTROL + i,
						WM831X_GPN_PWR_DOM_MASK,
						~WM831X_GPN_PWR_DOM);
			}
		}
	}

#if 0
	for (i = 0; i < pdata->gpio_pin_num; i++) {
		wm831x_set_bits(wm831x,
				WM831X_GPIO1_CONTROL + i,
				WM831X_GPN_PULL_MASK | WM831X_GPN_POL_MASK | WM831X_GPN_OD_MASK | WM831X_GPN_TRI_MASK,
				1 << WM831X_GPN_POL_SHIFT | 1 << WM831X_GPN_TRI_SHIFT);

		ret = wm831x_reg_read(wm831x, WM831X_GPIO1_CONTROL + i);
		printk("Gpio%d Pin Configuration = %x\n", i, ret);
	}
#endif

out:
	return 0;
}
#if defined(CONFIG_KEYBOARD_WM831X_GPIO)
static struct wm831x_gpio_keys_button wm831x_gpio_buttons[] = {
{	
	.code		= KEY_MEDIA,
	.gpio		= TCA6424_P21,
	.active_low	= 1,
	.desc		= "media",
	.wakeup		= 0,
	.debounce_interval = 120,
},
{
    .code= KEY_VOLUMEUP,
		.gpio= WM831X_P05,
		.active_low= 1,
		.desc= "volume_up",
		.wakeup= 0,
},
{
		.code= KEY_CAMERA,
		.gpio= WM831X_P06,
		.active_low= 1,
		.desc= "camera",
		.wakeup= 0,
},
{
		.code= KEY_VOLUMEDOWN,
		.gpio= WM831X_P07,
		.active_low= 1,
		.desc= "volume_down",
		.wakeup= 0,
},
{
		.code= KEY_END,
		.gpio= WM831X_P09,
		.active_low= 1,
		.desc= "enter",
		.wakeup= 0,
},
{
		.code= KEY_MENU,
		.gpio= WM831X_P10,
		.active_low= 1,
		.desc= "menu",
		.wakeup= 0,
},
{
		.code= KEY_SEND,
		.gpio= WM831X_P11,
		.active_low= 1,
		.desc= "esc",
		.wakeup= 0,
},
{
		.code= KEY_BACK,
		.gpio= WM831X_P12,
		.active_low= 1,
		.desc= "home",
		.wakeup= 0,		    	
},
};

struct wm831x_gpio_keys_pdata wm831x_gpio_keys_platdata = {
	.buttons	= wm831x_gpio_buttons,
	.nbuttons	= ARRAY_SIZE(wm831x_gpio_buttons),
};

#endif

static struct wm831x_pdata wm831x_platdata = {

	/** Called before subdevices are set up */
	.pre_init = wm831x_pre_init,
	/** Called after subdevices are set up */
	.post_init = wm831x_post_init,
	/** Called before subdevices are power down */
	.last_deinit = wm831x_last_deinit,

#if defined(CONFIG_GPIO_WM831X)
	.gpio_base = WM831X_GPIO_EXPANDER_BASE,
	.gpio_pin_num = WM831X_TOTOL_GPIO_NUM,
	.settinginfo = wm831x_gpio_settinginfo,
	.settinginfolen = ARRAY_SIZE(wm831x_gpio_settinginfo),
	.pin_type_init = wm831x_init_pin_type,
	.irq_base = NR_GIC_IRQS + NR_GPIO_IRQS,
#endif
	.backlight = &wm831x_backlight_platdata,

	.backup = &wm831x_backup_platdata,
	
	.battery = &wm831x_battery_platdata,
	//.wm831x_touch_pdata = NULL,
	//.watchdog = NULL,
	
#if defined(CONFIG_KEYBOARD_WM831X_GPIO)	
	.gpio_keys = &wm831x_gpio_keys_platdata,
#endif
	/** LED1 = 0 and so on */
	.status = { &wm831x_status_platdata[0], &wm831x_status_platdata[1] },

	/** DCDC1 = 0 and so on */
	.dcdc = {
		&wm831x_regulator_init_dcdc[0],
		&wm831x_regulator_init_dcdc[1],
		&wm831x_regulator_init_dcdc[2],
		&wm831x_regulator_init_dcdc[3],
	},

	/** EPE1 = 0 and so on */
	.epe = { &wm831x_regulator_init_epe[0], &wm831x_regulator_init_epe[1] },

	/** LDO1 = 0 and so on */
	.ldo = {
		&wm831x_regulator_init_ldo[0],
		&wm831x_regulator_init_ldo[1],
		&wm831x_regulator_init_ldo[2],
		&wm831x_regulator_init_ldo[3],
		&wm831x_regulator_init_ldo[4],
		&wm831x_regulator_init_ldo[5],
		&wm831x_regulator_init_ldo[6],
		&wm831x_regulator_init_ldo[7],
		&wm831x_regulator_init_ldo[8],
		&wm831x_regulator_init_ldo[9],
		&wm831x_regulator_init_ldo[10],
	},
	/** ISINK1 = 0 and so on*/
	//.isink = {&wm831x_regulator_init_isink[0], &wm831x_regulator_init_isink[1]},
};
#endif
