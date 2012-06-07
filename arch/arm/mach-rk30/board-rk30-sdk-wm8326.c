#include <linux/regulator/machine.h>
#include <linux/mfd/wm831x/pdata.h>
#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/gpio.h>
#include <linux/mfd/wm831x/pmu.h>

#include <mach/sram.h>

#define cru_readl(offset)	readl_relaxed(RK30_CRU_BASE + offset)
#define cru_writel(v, offset)	do { writel_relaxed(v, RK30_CRU_BASE + offset); dsb(); } while (0)

#define grf_readl(offset)	readl_relaxed(RK30_GRF_BASE + offset)
#define grf_writel(v, offset)	do { writel_relaxed(v, RK30_GRF_BASE + offset); dsb(); } while (0)

#define CRU_CLKGATE5_CON_ADDR 0x00e4
#define GRF_GPIO6L_DIR_ADDR 0x0030
#define GRF_GPIO6L_DO_ADDR 0x0068
#define GRF_GPIO6L_EN_ADDR 0x00a0
#define CRU_CLKGATE5_GRFCLK_ON 0x00100000
#define CRU_CLKGATE5_GRFCLK_OFF 0x00100010
#define GPIO6_PB1_DIR_OUT  0x02000200
#define GPIO6_PB1_DO_LOW  0x02000000
#define GPIO6_PB1_DO_HIGH  0x02000200
#define GPIO6_PB1_EN_MASK  0x02000200
#define GPIO6_PB1_UNEN_MASK  0x02000000

/* wm8326 pmu*/
#if defined(CONFIG_GPIO_WM831X)
static struct rk29_gpio_expander_info wm831x_gpio_settinginfo[] = {
	{
		.gpio_num = WM831X_P01,	// tp3
		.pin_type = GPIO_OUT,
		.pin_value = GPIO_LOW,
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
	gpio_direction_output(PMU_POWER_SLEEP, GPIO_LOW);
	
	ret = wm831x_reg_read(parm, WM831X_POWER_STATE) & 0xffff;
	wm831x_reg_write(parm, WM831X_POWER_STATE, (ret & 0xfff8) | 0x04);

	wm831x_set_bits(parm, WM831X_RTC_CONTROL, WM831X_RTC_ALAM_ENA_MASK, 0x0400);//enable rtc alam
	//BATT_FET_ENA = 1
	wm831x_reg_write(parm, WM831X_SECURITY_KEY, 0x9716);	// unlock security key
	wm831x_set_bits(parm, WM831X_RESET_CONTROL, 0x1003, 0x1001);
	ret = wm831x_reg_read(parm, WM831X_RESET_CONTROL) & 0xffff & UNLOCK_SECURITY_KEY;	// enternal reset active in sleep
//	printk("%s:WM831X_RESET_CONTROL=0x%x\n", __func__, ret);
	wm831x_reg_write(parm, WM831X_RESET_CONTROL, ret);

	wm831x_set_bits(parm,WM831X_DC1_ON_CONFIG ,0x0300,0x0000); //set dcdc mode is FCCM
	wm831x_set_bits(parm,WM831X_DC2_ON_CONFIG ,0x0300,0x0000);
	wm831x_set_bits(parm,WM831X_DC3_ON_CONFIG ,0x0300,0x0000);
	wm831x_set_bits(parm,0x4066,0x0300,0x0000);

	wm831x_set_bits(parm,WM831X_LDO10_CONTROL ,0x0040,0x0040);// set ldo10 in switch mode

	wm831x_set_bits(parm,WM831X_STATUS_LED_1 ,0xc300,0xc100);// set led1 on(in manual mode)
	wm831x_set_bits(parm,WM831X_STATUS_LED_2 ,0xc300,0xc000);//set led2 off(in manual mode)

	wm831x_set_bits(parm,WM831X_LDO5_SLEEP_CONTROL ,0xe000,0x2000);// set ldo5 is disable in sleep mode 
	wm831x_set_bits(parm,WM831X_LDO1_SLEEP_CONTROL ,0xe000,0x2000);// set ldo1 is disable in sleep mode 
	
	wm831x_reg_write(parm, WM831X_SECURITY_KEY, LOCK_SECURITY_KEY);	// lock security key

	return 0;
}
static int wm831x_mask_interrupt(struct wm831x *Wm831x)
{
	/**************************clear interrupt********************/
	wm831x_reg_write(Wm831x,WM831X_INTERRUPT_STATUS_1,0xffff);
	wm831x_reg_write(Wm831x,WM831X_INTERRUPT_STATUS_2,0xffff);
	wm831x_reg_write(Wm831x,WM831X_INTERRUPT_STATUS_3,0xffff);
	wm831x_reg_write(Wm831x,WM831X_INTERRUPT_STATUS_4,0xffff);
	wm831x_reg_write(Wm831x,WM831X_INTERRUPT_STATUS_5,0xffff);
	
	wm831x_reg_write(Wm831x,WM831X_SYSTEM_INTERRUPTS_MASK,0xbedc); //mask interrupt which not used
	return 0;
	/*****************************************************************/
}

#ifdef CONFIG_WM8326_VBAT_LOW_DETECTION
static int wm831x_low_power_detection(struct wm831x *wm831x)
{
	wm831x_reg_write(wm831x,WM831X_AUXADC_CONTROL,0x803f);     //open adc 
	wm831x_reg_write(wm831x,WM831X_AUXADC_CONTROL,0xd03f);
	wm831x_reg_write(wm831x,WM831X_AUXADC_SOURCE,0x0001);
	
	wm831x_reg_write(wm831x,WM831X_COMPARATOR_CONTROL,0x0001);
	wm831x_reg_write(wm831x,WM831X_COMPARATOR_1,0x2910);   //set the low power is 3.4v
	
	wm831x_reg_write(wm831x,WM831X_INTERRUPT_STATUS_1_MASK,0x99ee);
	wm831x_set_bits(wm831x,WM831X_SYSTEM_INTERRUPTS_MASK,0x0100,0x0000);
	if (wm831x_reg_read(wm831x,WM831X_AUXADC_DATA)< 0x1900){
		printk("The vbat is too low.\n");
		wm831x_device_shutdown(wm831x);
	}
	return 0;
}
#endif


int wm831x_post_init(struct wm831x *Wm831x)
{
	struct regulator *dcdc;
	struct regulator *ldo;


	ldo = regulator_get(NULL, "ldo6");	//vcc_33
	regulator_set_voltage(ldo, 3300000, 3300000);
	regulator_set_suspend_voltage(ldo, 3300000);
	regulator_enable(ldo);
//	printk("%s set ldo6 vcc_33=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);
	
	ldo = regulator_get(NULL, "ldo4");	// vdd_11
	regulator_set_voltage(ldo, 1100000, 1100000);
	regulator_set_suspend_voltage(ldo, 1000000);
	regulator_enable(ldo);
//	printk("%s set ldo4 vdd_11=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo5");	//vcc_25
	regulator_set_voltage(ldo, 2500000, 2500000);
	regulator_set_suspend_voltage(ldo, 2500000);
	regulator_enable(ldo);
//	printk("%s set ldo5 vcc_25=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);

	dcdc = regulator_get(NULL, "dcdc4");	// vcc_io
#ifdef CONFIG_MACH_RK3066_SDK1
	regulator_set_voltage(dcdc, 3300000, 3300000);
	regulator_set_suspend_voltage(dcdc, 3100000);
#else
	regulator_set_voltage(dcdc, 3000000, 3000000);
	regulator_set_suspend_voltage(dcdc, 2800000);
#endif
	regulator_enable(dcdc);
//	printk("%s set dcdc4 vcc_io=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	dcdc = regulator_get(NULL, "vdd_cpu");	// vdd_arm
	regulator_set_voltage(dcdc, 1100000, 1100000);
	regulator_set_suspend_voltage(dcdc, 1000000);
	regulator_enable(dcdc);
	printk("%s set dcdc2 vdd_cpu(vdd_arm)=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	dcdc = regulator_get(NULL, "vdd_core");	// vdd_log
	regulator_set_voltage(dcdc, 1150000, 1150000);
	regulator_set_suspend_voltage(dcdc, 1000000);
	regulator_enable(dcdc);
	printk("%s set dcdc1 vdd_core(vdd_log)=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	dcdc = regulator_get(NULL, "dcdc3");	// vcc_ddr
	regulator_set_voltage(dcdc, 1150000, 1150000);
	regulator_set_suspend_voltage(dcdc, 1150000);
	regulator_enable(dcdc);
//	printk("%s set dcdc3 vcc_ddr=%dmV end\n", __func__, regulator_get_voltage(dcdc));
	regulator_put(dcdc);
	udelay(100);

	ldo = regulator_get(NULL, "ldo7");	// vcc28_cif
	regulator_set_voltage(ldo, 2800000, 2800000);
	regulator_set_suspend_voltage(ldo, 2800000);
	regulator_enable(ldo);
//	printk("%s set ldo7 vcc28_cif=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo1");	// vcc18_cif
	regulator_set_voltage(ldo, 1800000, 1800000);
	regulator_set_suspend_voltage(ldo, 1800000);
	regulator_enable(ldo);
//	printk("%s set ldo1 vcc18_cif=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo8");	// vcca_33
	regulator_set_voltage(ldo, 3300000, 3300000);
	regulator_set_suspend_voltage(ldo, 3300000);
	regulator_enable(ldo);
//	printk("%s set ldo8 vcca_33=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo2");	//vccio_wl
	regulator_set_voltage(ldo, 1800000, 1800000);
	regulator_set_suspend_voltage(ldo, 1800000);
	regulator_enable(ldo);
//	printk("%s set ldo2 vccio_wl=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo10");	//flash io
	regulator_set_voltage(ldo, 1800000, 1800000);
	regulator_set_suspend_voltage(ldo, 1800000);
	regulator_enable(ldo);
//	printk("%s set ldo10 vcca_wl=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

#ifdef CONFIG_MACH_RK3066_SDK1
	ldo = regulator_get(NULL, "ldo3");	//vdd11_hdmi
	regulator_set_voltage(ldo, 1100000, 1100000);
	regulator_set_suspend_voltage(ldo, 1100000);
#else
	ldo = regulator_get(NULL, "ldo3");	//vdd_12
	regulator_set_voltage(ldo, 1200000, 1200000);
	regulator_set_suspend_voltage(ldo, 1200000);
#endif
	regulator_enable(ldo);
//	printk("%s set ldo3 vdd_12=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	ldo = regulator_get(NULL, "ldo9");	//vcc_tp
	regulator_set_voltage(ldo, 3300000, 3300000);
	regulator_set_suspend_voltage(ldo, 3300000);
	regulator_enable(ldo);
//	printk("%s set ldo9 vcc_tp=%dmV end\n", __func__, regulator_get_voltage(ldo));
	regulator_put(ldo);
	udelay(100);

	wm831x_mask_interrupt(Wm831x);

	#ifdef CONFIG_WM8326_VBAT_LOW_DETECTION
	wm831x_low_power_detection(Wm831x);
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
	regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo4");
	//regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo5");
//	regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo6");
//	regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo7");
	regulator_disable(ldo);
	regulator_put(ldo);

	ldo = regulator_get(NULL, "ldo8");
	regulator_disable(ldo);
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
		.supply = "vdd_core",
	}
};

static struct regulator_consumer_supply dcdc2_consumers[] = {
	{
		.supply = "vdd_cpu",
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
		if (i == 1) {
				wm831x_set_bits(wm831x,
						WM831X_GPIO1_CONTROL + i,
						WM831X_GPN_POL_MASK,
						0x0400);
				wm831x_set_bits(wm831x,
						WM831X_GPIO1_CONTROL + i,
						WM831X_GPN_FN_MASK,
						0x0003);				
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

void __sramfunc board_pmu_suspend(void)
{	
	cru_writel(CRU_CLKGATE5_GRFCLK_ON,CRU_CLKGATE5_CON_ADDR); //open grf clk
	grf_writel(GPIO6_PB1_DIR_OUT, GRF_GPIO6L_DIR_ADDR);
	grf_writel(GPIO6_PB1_DO_HIGH, GRF_GPIO6L_DO_ADDR);  //set gpio6_b1 output low
	grf_writel(GPIO6_PB1_EN_MASK, GRF_GPIO6L_EN_ADDR);
}
void __sramfunc board_pmu_resume(void)
{
	grf_writel(GPIO6_PB1_DIR_OUT, GRF_GPIO6L_DIR_ADDR);
	grf_writel(GPIO6_PB1_DO_LOW, GRF_GPIO6L_DO_ADDR);     //set gpio6_b1 output high
	grf_writel(GPIO6_PB1_EN_MASK, GRF_GPIO6L_EN_ADDR);
#ifdef CONFIG_CLK_SWITCH_TO_32K
	sram_32k_udelay(10000);
#else
	sram_udelay(10000);
#endif
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
