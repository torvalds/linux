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

static int wm831x_pre_init(struct wm831x *parm)
{
	int ret;

	printk("%s\n", __func__);
	//ILIM = 900ma
	ret = wm831x_reg_read(parm, WM831X_POWER_STATE) & 0xffff;
	wm831x_reg_write(parm, WM831X_POWER_STATE, (ret & 0xfff8) | 0x04);

	//BATT_FET_ENA = 1
	wm831x_reg_write(parm, WM831X_SECURITY_KEY, 0x9716);	// unlock security key
	wm831x_set_bits(parm, WM831X_RESET_CONTROL, 0x1000, 0x1000);
	ret = wm831x_reg_read(parm, WM831X_RESET_CONTROL) & 0xffff & UNLOCK_SECURITY_KEY;	// enternal reset active in sleep
	printk("%s:WM831X_RESET_CONTROL=0x%x\n", __func__, ret);
	wm831x_reg_write(parm, WM831X_RESET_CONTROL, ret);

	wm831x_reg_write(parm, WM831X_SECURITY_KEY, LOCK_SECURITY_KEY);	// lock security key

	return 0;
}

int wm831x_post_init(struct wm831x *Wm831x)
{
	struct regulator *dcdc;
	struct regulator *ldo;

	ldo = regulator_get(NULL, "ldo6");	//vcc_33
	regulator_set_voltage(ldo, 2800000, 2800000);
	regulator_set_suspend_voltage(ldo, 2800000);
	regulator_enable(ldo);
	printk("%s set ldo6=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo4");	// vdd_11
	regulator_set_voltage(ldo, 2500000, 2500000);
	regulator_set_suspend_voltage(ldo, 0000000);
	regulator_enable(ldo);
	printk("%s set ldo4=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo5");	//vcc_25
	regulator_set_voltage(ldo, 3000000, 3000000);
	regulator_set_suspend_voltage(ldo, 3000000);
	regulator_enable(ldo);
	printk("%s set ldo5=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);

	dcdc = regulator_get(NULL, "dcdc4");	// vcc_io
	regulator_set_voltage(dcdc, 3000000, 3000000);
	regulator_set_suspend_voltage(dcdc, 3000000);
	regulator_enable(dcdc);
	printk("%s set dcdc4=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	dcdc = regulator_get(NULL, "dcdc2");	// vdd_arm
	regulator_set_voltage(dcdc, 1100000, 1100000);
	regulator_set_suspend_voltage(dcdc, 1000000);
	regulator_enable(dcdc);
	printk("%s set dcdc2=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	dcdc = regulator_get(NULL, "dcdc1");	// vdd_log
	regulator_set_voltage(dcdc, 1100000, 1100000);
	regulator_set_suspend_voltage(dcdc, 1100000);
	regulator_enable(dcdc);
	printk("%s set dcdc1=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	dcdc = regulator_get(NULL, "dcdc3");	// vcc_ddr
	regulator_set_voltage(dcdc, 1200000, 1200000);
	regulator_set_suspend_voltage(dcdc, 1200000);
	regulator_enable(dcdc);
	printk("%s set dcdc3=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	ldo = regulator_get(NULL, "ldo7");	// vcc28_cif
	regulator_set_voltage(ldo, 3300000, 3300000);
	regulator_set_suspend_voltage(ldo, 3300000);
	regulator_enable(ldo);
	printk("%s set ldo7=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo1");	// vcc18_cif
	regulator_set_voltage(ldo, 1800000, 1800000);
	regulator_set_suspend_voltage(ldo, 1800000);
	regulator_enable(ldo);
	printk("%s set ldo1=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo8");	// vcc18_cif
	regulator_set_voltage(ldo, 3300000, 3300000);
	regulator_set_suspend_voltage(ldo, 3300000);
	regulator_enable(ldo);
	printk("%s set ldo8=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo2");	//vccio_wl
	regulator_set_voltage(ldo, 2800000, 2800000);
	regulator_set_suspend_voltage(ldo, 2800000);
	regulator_enable(ldo);
	printk("%s set ldo2=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo10");	//vdd_12
	regulator_set_voltage(ldo, 3300000, 3300000);
	regulator_set_suspend_voltage(ldo, 3300000);
	regulator_enable(ldo);
	printk("%s set ldo10=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo3");	//vdd_12
	regulator_set_voltage(ldo, 1200000, 1200000);
	regulator_set_suspend_voltage(ldo, 1200000);
	regulator_enable(ldo);
	printk("%s set ldo3=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo9");	//vccio_wl
	regulator_set_voltage(ldo, 3300000, 3300000);
	regulator_set_suspend_voltage(ldo, 3300000);
	regulator_enable(ldo);
	printk("%s set ldo9=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

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
	regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo4");
	//regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo5");
	regulator_disable(ldo);
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
	},
	//      {
	//      .supply = "vcore",
	//}
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

#if 0
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
#endif

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
			.min_uV = 850000,
			.max_uV = 3400000,	//0.85-3.4V
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(dcdc4_consumers),
		.consumer_supplies = dcdc4_consumers,
	},
};

#if 0
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
#endif

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
					(WM831X_GPIO1_CONTROL + i),
					WM831X_GPN_PULL_MASK, 1 << WM831X_GPN_PULL_SHIFT);	//pull down
			if (i == 0) {
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
	//.epe = { &wm831x_regulator_init_epe[0], &wm831x_regulator_init_epe[1] },

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
};
#endif
