// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Board info for Lenovo X86 tablets which ship with Android as the factory image
 * and which have broken DSDT tables. The factory kernels shipped on these
 * devices typically have a bunch of things hardcoded, rather than specified
 * in their DSDT.
 *
 * Copyright (C) 2021-2023 Hans de Goede <hdegoede@redhat.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/efi.h>
#include <linux/gpio/machine.h>
#include <linux/mfd/arizona/pdata.h>
#include <linux/mfd/arizona/registers.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_data/lp855x.h>
#include <linux/platform_device.h>
#include <linux/power/bq24190_charger.h>
#include <linux/reboot.h>
#include <linux/rmi.h>
#include <linux/spi/spi.h>

#include "shared-psy-info.h"
#include "x86-android-tablets.h"

/*
 * Various Lenovo models use a TI LP8557 LED backlight controller with its PWM
 * input connected to a PWM output coming from the LCD panel's controller.
 * The Android kernels have a hack in the i915 driver to write a non-standard
 * panel specific DSI register to set the duty-cycle of the LCD's PWM output.
 *
 * To avoid having to have a similar hack in the mainline kernel program the
 * LP8557 to directly set the level and use the lp855x_bl driver for control.
 *
 * The LP8557 can either be configured to multiply its PWM input and
 * the I2C register set level (requiring both to be at 100% for 100% output);
 * or to only take the I2C register set level into account.
 *
 * Multiplying the 2 levels is useful because this will turn off the backlight
 * when the panel goes off and turns off its PWM output.
 *
 * But on some models the panel's PWM output defaults to a duty-cycle of
 * much less then 100%, severely limiting max brightness. In this case
 * the LP8557 should be configured to only take the I2C register into
 * account and the i915 driver must turn off the panel and the backlight
 * separately using e.g. VBT MIPI sequences to turn off the backlight.
 */
static struct lp855x_platform_data lenovo_lp8557_pwm_and_reg_pdata = {
	.device_control = 0x86,
	.initial_brightness = 128,
};

static struct lp855x_platform_data lenovo_lp8557_reg_only_pdata = {
	.device_control = 0x85,
	.initial_brightness = 128,
};

/* Lenovo Yoga Book X90F / X90L's Android factory img has everything hardcoded */

static const struct property_entry lenovo_yb1_x90_wacom_props[] = {
	PROPERTY_ENTRY_U32("hid-descr-addr", 0x0001),
	PROPERTY_ENTRY_U32("post-reset-deassert-delay-ms", 150),
	{ }
};

static const struct software_node lenovo_yb1_x90_wacom_node = {
	.properties = lenovo_yb1_x90_wacom_props,
};

/*
 * The HiDeep IST940E touchscreen comes up in I2C-HID mode. The native protocol
 * reports ABS_MT_PRESSURE and ABS_MT_TOUCH_MAJOR which are not reported in HID
 * mode, so using native mode is preferred.
 * It could alternatively be used in HID mode by changing the properties to:
 *	PROPERTY_ENTRY_U32("hid-descr-addr", 0x0020),
 *	PROPERTY_ENTRY_U32("post-reset-deassert-delay-ms", 120),
 * and changing board_info.type to "hid-over-i2c".
 */
static const struct property_entry lenovo_yb1_x90_hideep_ts_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1200),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1920),
	PROPERTY_ENTRY_U32("touchscreen-max-pressure", 16384),
	PROPERTY_ENTRY_BOOL("hideep,force-native-protocol"),
	{ }
};

static const struct software_node lenovo_yb1_x90_hideep_ts_node = {
	.properties = lenovo_yb1_x90_hideep_ts_props,
};

static const struct x86_i2c_client_info lenovo_yb1_x90_i2c_clients[] __initconst = {
	{
		/* BQ27542 fuel-gauge */
		.board_info = {
			.type = "bq27542",
			.addr = 0x55,
			.dev_name = "bq27542",
			.swnode = &fg_bq25890_supply_node,
		},
		.adapter_path = "\\_SB_.PCI0.I2C1",
	}, {
		/* Goodix Touchscreen in keyboard half */
		.board_info = {
			.type = "GDIX1001:00",
			.addr = 0x14,
			.dev_name = "goodix_ts",
		},
		.adapter_path = "\\_SB_.PCI0.I2C2",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FF:01",
			.index = 56,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
			.con_id = "goodix_ts_irq",
			.free_gpio = true,
		},
	}, {
		/* Wacom Digitizer in keyboard half */
		.board_info = {
			.type = "hid-over-i2c",
			.addr = 0x09,
			.dev_name = "wacom",
			.swnode = &lenovo_yb1_x90_wacom_node,
		},
		.adapter_path = "\\_SB_.PCI0.I2C4",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FF:01",
			.index = 49,
			.trigger = ACPI_LEVEL_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
			.con_id = "wacom_irq",
		},
	}, {
		/* LP8557 Backlight controller */
		.board_info = {
			.type = "lp8557",
			.addr = 0x2c,
			.dev_name = "lp8557",
			.platform_data = &lenovo_lp8557_pwm_and_reg_pdata,
		},
		.adapter_path = "\\_SB_.PCI0.I2C4",
	}, {
		/* HiDeep IST940E Touchscreen in display half */
		.board_info = {
			.type = "hideep_ts",
			.addr = 0x6c,
			.dev_name = "hideep_ts",
			.swnode = &lenovo_yb1_x90_hideep_ts_node,
		},
		.adapter_path = "\\_SB_.PCI0.I2C6",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FF:03",
			.index = 77,
			.trigger = ACPI_LEVEL_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
			.con_id = "hideep_ts_irq",
		},
	},
};

static const struct platform_device_info lenovo_yb1_x90_pdevs[] __initconst = {
	{
		.name = "yogabook-touch-kbd-digitizer-switch",
		.id = PLATFORM_DEVID_NONE,
	},
};

/*
 * DSDT says UART path is "\\_SB.PCIO.URT1" with a letter 'O' instead of
 * the number '0' add the link manually.
 */
static const struct x86_serdev_info lenovo_yb1_x90_serdevs[] __initconst = {
	{
		.ctrl_hid = "8086228A",
		.ctrl_uid = "1",
		.ctrl_devname = "serial0",
		.serdev_hid = "BCM2E1A",
	},
};

static const struct x86_gpio_button lenovo_yb1_x90_lid __initconst = {
	.button = {
		.code = SW_LID,
		.active_low = true,
		.desc = "lid_sw",
		.type = EV_SW,
		.wakeup = true,
		.debounce_interval = 50,
	},
	.chip = "INT33FF:02",
	.pin = 19,
};

static struct gpiod_lookup_table lenovo_yb1_x90_goodix_gpios = {
	.dev_id = "i2c-goodix_ts",
	.table = {
		GPIO_LOOKUP("INT33FF:01", 53, "reset", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT33FF:01", 56, "irq", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static struct gpiod_lookup_table lenovo_yb1_x90_hideep_gpios = {
	.dev_id = "i2c-hideep_ts",
	.table = {
		GPIO_LOOKUP("INT33FF:00", 7, "reset", GPIO_ACTIVE_LOW),
		{ }
	},
};

static struct gpiod_lookup_table lenovo_yb1_x90_wacom_gpios = {
	.dev_id = "i2c-wacom",
	.table = {
		GPIO_LOOKUP("INT33FF:00", 82, "reset", GPIO_ACTIVE_LOW),
		{ }
	},
};

static struct gpiod_lookup_table * const lenovo_yb1_x90_gpios[] = {
	&lenovo_yb1_x90_hideep_gpios,
	&lenovo_yb1_x90_goodix_gpios,
	&lenovo_yb1_x90_wacom_gpios,
	NULL
};

static int __init lenovo_yb1_x90_init(void)
{
	/* Enable the regulators used by the touchscreens */

	/* Vprog3B 3.0V used by the goodix touchscreen in the keyboard half */
	intel_soc_pmic_exec_mipi_pmic_seq_element(0x6e, 0x9b, 0x02, 0xff);

	/* Vprog4D 3.0V used by the HiDeep touchscreen in the display half */
	intel_soc_pmic_exec_mipi_pmic_seq_element(0x6e, 0x9f, 0x02, 0xff);

	/* Vprog5A 1.8V used by the HiDeep touchscreen in the display half */
	intel_soc_pmic_exec_mipi_pmic_seq_element(0x6e, 0xa0, 0x02, 0xff);

	/* Vprog5B 1.8V used by the goodix touchscreen in the keyboard half */
	intel_soc_pmic_exec_mipi_pmic_seq_element(0x6e, 0xa1, 0x02, 0xff);

	return 0;
}

const struct x86_dev_info lenovo_yogabook_x90_info __initconst = {
	.i2c_client_info = lenovo_yb1_x90_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(lenovo_yb1_x90_i2c_clients),
	.pdev_info = lenovo_yb1_x90_pdevs,
	.pdev_count = ARRAY_SIZE(lenovo_yb1_x90_pdevs),
	.serdev_info = lenovo_yb1_x90_serdevs,
	.serdev_count = ARRAY_SIZE(lenovo_yb1_x90_serdevs),
	.gpio_button = &lenovo_yb1_x90_lid,
	.gpio_button_count = 1,
	.gpiod_lookup_tables = lenovo_yb1_x90_gpios,
	.init = lenovo_yb1_x90_init,
};

/* Lenovo Yoga Book X91F/L Windows tablet needs manual instantiation of the fg client */
static const struct x86_i2c_client_info lenovo_yogabook_x91_i2c_clients[] __initconst = {
	{
		/* BQ27542 fuel-gauge */
		.board_info = {
			.type = "bq27542",
			.addr = 0x55,
			.dev_name = "bq27542",
			.swnode = &fg_bq25890_supply_node,
		},
		.adapter_path = "\\_SB_.PCI0.I2C1",
	},
};

const struct x86_dev_info lenovo_yogabook_x91_info __initconst = {
	.i2c_client_info = lenovo_yogabook_x91_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(lenovo_yogabook_x91_i2c_clients),
};

/* Lenovo Yoga Tablet 2 1050F/L's Android factory img has everything hardcoded */
static const struct property_entry lenovo_yoga_tab2_830_1050_bq24190_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY_LEN("supplied-from", tusb1211_chg_det_psy, 1),
	PROPERTY_ENTRY_REF("monitored-battery", &generic_lipo_hv_4v35_battery_node),
	PROPERTY_ENTRY_BOOL("omit-battery-class"),
	PROPERTY_ENTRY_BOOL("disable-reset"),
	{ }
};

static const struct software_node lenovo_yoga_tab2_830_1050_bq24190_node = {
	.properties = lenovo_yoga_tab2_830_1050_bq24190_props,
};

static const struct x86_gpio_button lenovo_yoga_tab2_830_1050_lid __initconst = {
	.button = {
		.code = SW_LID,
		.active_low = true,
		.desc = "lid_sw",
		.type = EV_SW,
		.wakeup = true,
		.debounce_interval = 50,
	},
	.chip = "INT33FC:02",
	.pin = 26,
};

/* This gets filled by lenovo_yoga_tab2_830_1050_init() */
static struct rmi_device_platform_data lenovo_yoga_tab2_830_1050_rmi_pdata = { };

static struct x86_i2c_client_info lenovo_yoga_tab2_830_1050_i2c_clients[] __initdata = {
	{
		/*
		 * This must be the first entry because lenovo_yoga_tab2_830_1050_init()
		 * may update its swnode. LSM303DA accelerometer + magnetometer.
		 */
		.board_info = {
			.type = "lsm303d",
			.addr = 0x1d,
			.dev_name = "lsm303d",
		},
		.adapter_path = "\\_SB_.I2C5",
	}, {
		/* AL3320A ambient light sensor */
		.board_info = {
			.type = "al3320a",
			.addr = 0x1c,
			.dev_name = "al3320a",
		},
		.adapter_path = "\\_SB_.I2C5",
	}, {
		/* bq24292i battery charger */
		.board_info = {
			.type = "bq24190",
			.addr = 0x6b,
			.dev_name = "bq24292i",
			.swnode = &lenovo_yoga_tab2_830_1050_bq24190_node,
			.platform_data = &bq24190_pdata,
		},
		.adapter_path = "\\_SB_.I2C1",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FC:02",
			.index = 2,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_HIGH,
			.con_id = "bq24292i_irq",
		},
	}, {
		/* BQ27541 fuel-gauge */
		.board_info = {
			.type = "bq27541",
			.addr = 0x55,
			.dev_name = "bq27541",
			.swnode = &fg_bq24190_supply_node,
		},
		.adapter_path = "\\_SB_.I2C1",
	}, {
		/* Synaptics RMI touchscreen */
		.board_info = {
			.type = "rmi4_i2c",
			.addr = 0x38,
			.dev_name = "rmi4_i2c",
			.platform_data = &lenovo_yoga_tab2_830_1050_rmi_pdata,
		},
		.adapter_path = "\\_SB_.I2C6",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_APIC,
			.index = 0x45,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_HIGH,
		},
	}, {
		/* LP8557 Backlight controller */
		.board_info = {
			.type = "lp8557",
			.addr = 0x2c,
			.dev_name = "lp8557",
			.platform_data = &lenovo_lp8557_pwm_and_reg_pdata,
		},
		.adapter_path = "\\_SB_.I2C3",
	},
};

static struct gpiod_lookup_table lenovo_yoga_tab2_830_1050_int3496_gpios = {
	.dev_id = "intel-int3496",
	.table = {
		GPIO_LOOKUP("INT33FC:02", 1, "mux", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("INT33FC:02", 24, "id", GPIO_ACTIVE_HIGH),
		{ }
	},
};

#define LENOVO_YOGA_TAB2_830_1050_CODEC_NAME "spi-10WM5102:00"

static struct gpiod_lookup_table lenovo_yoga_tab2_830_1050_codec_gpios = {
	.dev_id = LENOVO_YOGA_TAB2_830_1050_CODEC_NAME,
	.table = {
		GPIO_LOOKUP("gpio_crystalcove", 3, "reset", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT33FC:01", 23, "wlf,ldoena", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("arizona", 2, "wlf,spkvdd-ena", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("arizona", 4, "wlf,micd-pol", GPIO_ACTIVE_LOW),
		{ }
	},
};

static struct gpiod_lookup_table * const lenovo_yoga_tab2_830_1050_gpios[] = {
	&lenovo_yoga_tab2_830_1050_int3496_gpios,
	&lenovo_yoga_tab2_830_1050_codec_gpios,
	NULL
};

static int __init lenovo_yoga_tab2_830_1050_init(void);
static void lenovo_yoga_tab2_830_1050_exit(void);

const struct x86_dev_info lenovo_yoga_tab2_830_1050_info __initconst = {
	.i2c_client_info = lenovo_yoga_tab2_830_1050_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(lenovo_yoga_tab2_830_1050_i2c_clients),
	.pdev_info = int3496_pdevs,
	.pdev_count = 1,
	.gpio_button = &lenovo_yoga_tab2_830_1050_lid,
	.gpio_button_count = 1,
	.gpiod_lookup_tables = lenovo_yoga_tab2_830_1050_gpios,
	.bat_swnode = &generic_lipo_hv_4v35_battery_node,
	.modules = bq24190_modules,
	.init = lenovo_yoga_tab2_830_1050_init,
	.exit = lenovo_yoga_tab2_830_1050_exit,
};

/*
 * The Lenovo Yoga Tablet 2 830 and 1050 (8" vs 10") versions use the same
 * mainboard, but the 830 uses a portrait LCD panel with a landscape touchscreen,
 * requiring the touchscreen driver to adjust the touch-coords to match the LCD.
 * And requiring the accelerometer to have a mount-matrix set to correct for
 * the 90° rotation of the LCD vs the frame.
 */
static const char * const lenovo_yoga_tab2_830_lms303d_mount_matrix[] = {
	"0", "1", "0",
	"-1", "0", "0",
	"0", "0", "1"
};

static const struct property_entry lenovo_yoga_tab2_830_lms303d_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("mount-matrix", lenovo_yoga_tab2_830_lms303d_mount_matrix),
	{ }
};

static const struct software_node lenovo_yoga_tab2_830_lms303d_node = {
	.properties = lenovo_yoga_tab2_830_lms303d_props,
};

static int __init lenovo_yoga_tab2_830_1050_init_touchscreen(void)
{
	struct gpio_desc *gpiod;
	int ret;

	/* Use PMIC GPIO 10 bootstrap pin to differentiate 830 vs 1050 */
	ret = x86_android_tablet_get_gpiod("gpio_crystalcove", 10, "yoga_bootstrap",
					   false, GPIOD_ASIS, &gpiod);
	if (ret)
		return ret;

	ret = gpiod_get_value_cansleep(gpiod);
	if (ret) {
		pr_info("detected Lenovo Yoga Tablet 2 1050F/L\n");
	} else {
		pr_info("detected Lenovo Yoga Tablet 2 830F/L\n");
		lenovo_yoga_tab2_830_1050_rmi_pdata.sensor_pdata.axis_align.swap_axes = true;
		lenovo_yoga_tab2_830_1050_rmi_pdata.sensor_pdata.axis_align.flip_y = true;
		lenovo_yoga_tab2_830_1050_i2c_clients[0].board_info.swnode =
			&lenovo_yoga_tab2_830_lms303d_node;
	}

	return 0;
}

/* SUS (INT33FC:02) pin 6 needs to be configured as pmu_clk for the audio codec */
static const struct pinctrl_map lenovo_yoga_tab2_830_1050_codec_pinctrl_map =
	PIN_MAP_MUX_GROUP(LENOVO_YOGA_TAB2_830_1050_CODEC_NAME, "codec_32khz_clk",
			  "INT33FC:02", "pmu_clk2_grp", "pmu_clk");

static struct pinctrl *lenovo_yoga_tab2_830_1050_codec_pinctrl;
static struct sys_off_handler *lenovo_yoga_tab2_830_1050_sys_off_handler;

static int __init lenovo_yoga_tab2_830_1050_init_codec(void)
{
	struct device *codec_dev;
	struct pinctrl *pinctrl;
	int ret;

	codec_dev = bus_find_device_by_name(&spi_bus_type, NULL,
					    LENOVO_YOGA_TAB2_830_1050_CODEC_NAME);
	if (!codec_dev) {
		pr_err("error cannot find %s device\n", LENOVO_YOGA_TAB2_830_1050_CODEC_NAME);
		return -ENODEV;
	}

	ret = pinctrl_register_mappings(&lenovo_yoga_tab2_830_1050_codec_pinctrl_map, 1);
	if (ret)
		goto err_put_device;

	pinctrl = pinctrl_get_select(codec_dev, "codec_32khz_clk");
	if (IS_ERR(pinctrl)) {
		ret = dev_err_probe(codec_dev, PTR_ERR(pinctrl), "selecting codec_32khz_clk\n");
		goto err_unregister_mappings;
	}

	/* We're done with the codec_dev now */
	put_device(codec_dev);

	lenovo_yoga_tab2_830_1050_codec_pinctrl = pinctrl;
	return 0;

err_unregister_mappings:
	pinctrl_unregister_mappings(&lenovo_yoga_tab2_830_1050_codec_pinctrl_map);
err_put_device:
	put_device(codec_dev);
	return ret;
}

/*
 * These tablet's DSDT does not set acpi_gbl_reduced_hardware, so acpi_power_off
 * gets used as pm_power_off handler. This causes "poweroff" on these tablets
 * to hang hard. Requiring pressing the powerbutton for 30 seconds *twice*
 * followed by a normal 3 second press to recover. Avoid this by doing an EFI
 * poweroff instead.
 */
static int lenovo_yoga_tab2_830_1050_power_off(struct sys_off_data *data)
{
	efi.reset_system(EFI_RESET_SHUTDOWN, EFI_SUCCESS, 0, NULL);

	return NOTIFY_DONE;
}

static int __init lenovo_yoga_tab2_830_1050_init(void)
{
	int ret;

	ret = lenovo_yoga_tab2_830_1050_init_touchscreen();
	if (ret)
		return ret;

	ret = lenovo_yoga_tab2_830_1050_init_codec();
	if (ret)
		return ret;

	/* SYS_OFF_PRIO_FIRMWARE + 1 so that it runs before acpi_power_off */
	lenovo_yoga_tab2_830_1050_sys_off_handler =
		register_sys_off_handler(SYS_OFF_MODE_POWER_OFF, SYS_OFF_PRIO_FIRMWARE + 1,
					 lenovo_yoga_tab2_830_1050_power_off, NULL);
	if (IS_ERR(lenovo_yoga_tab2_830_1050_sys_off_handler))
		return PTR_ERR(lenovo_yoga_tab2_830_1050_sys_off_handler);

	return 0;
}

static void lenovo_yoga_tab2_830_1050_exit(void)
{
	unregister_sys_off_handler(lenovo_yoga_tab2_830_1050_sys_off_handler);

	if (lenovo_yoga_tab2_830_1050_codec_pinctrl) {
		pinctrl_put(lenovo_yoga_tab2_830_1050_codec_pinctrl);
		pinctrl_unregister_mappings(&lenovo_yoga_tab2_830_1050_codec_pinctrl_map);
	}
}

/*
 * Lenovo Yoga Tablet 2 Pro 1380F/L
 *
 * The Lenovo Yoga Tablet 2 Pro 1380F/L mostly has the same design as the 830F/L
 * and the 1050F/L so this re-uses some of the handling for that from above.
 */
static const char * const lc824206xa_chg_det_psy[] = { "lc824206xa-charger-detect" };

static const struct property_entry lenovo_yoga_tab2_1380_bq24190_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", lc824206xa_chg_det_psy),
	PROPERTY_ENTRY_REF("monitored-battery", &generic_lipo_hv_4v35_battery_node),
	PROPERTY_ENTRY_BOOL("omit-battery-class"),
	PROPERTY_ENTRY_BOOL("disable-reset"),
	{ }
};

static const struct software_node lenovo_yoga_tab2_1380_bq24190_node = {
	.properties = lenovo_yoga_tab2_1380_bq24190_props,
};

/* For enabling the bq24190 5V boost based on id-pin */
static struct regulator_consumer_supply lc824206xa_consumer = {
	.supply = "vbus",
	.dev_name = "i2c-lc824206xa",
};

static const struct regulator_init_data lenovo_yoga_tab2_1380_bq24190_vbus_init_data = {
	.constraints = {
		.name = "bq24190_vbus",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.consumer_supplies = &lc824206xa_consumer,
	.num_consumer_supplies = 1,
};

struct bq24190_platform_data lenovo_yoga_tab2_1380_bq24190_pdata = {
	.regulator_init_data = &lenovo_yoga_tab2_1380_bq24190_vbus_init_data,
};

static const struct property_entry lenovo_yoga_tab2_1380_lc824206xa_props[] = {
	PROPERTY_ENTRY_BOOL("onnn,enable-miclr-for-dcp"),
	{ }
};

static const struct software_node lenovo_yoga_tab2_1380_lc824206xa_node = {
	.properties = lenovo_yoga_tab2_1380_lc824206xa_props,
};

static const char * const lenovo_yoga_tab2_1380_lms303d_mount_matrix[] = {
	"0", "-1", "0",
	"-1", "0", "0",
	"0", "0", "1"
};

static const struct property_entry lenovo_yoga_tab2_1380_lms303d_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("mount-matrix", lenovo_yoga_tab2_1380_lms303d_mount_matrix),
	{ }
};

static const struct software_node lenovo_yoga_tab2_1380_lms303d_node = {
	.properties = lenovo_yoga_tab2_1380_lms303d_props,
};

static const struct x86_i2c_client_info lenovo_yoga_tab2_1380_i2c_clients[] __initconst = {
	{
		/* BQ27541 fuel-gauge */
		.board_info = {
			.type = "bq27541",
			.addr = 0x55,
			.dev_name = "bq27541",
			.swnode = &fg_bq24190_supply_node,
		},
		.adapter_path = "\\_SB_.I2C1",
	}, {
		/* bq24292i battery charger */
		.board_info = {
			.type = "bq24190",
			.addr = 0x6b,
			.dev_name = "bq24292i",
			.swnode = &lenovo_yoga_tab2_1380_bq24190_node,
			.platform_data = &lenovo_yoga_tab2_1380_bq24190_pdata,
		},
		.adapter_path = "\\_SB_.I2C1",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FC:02",
			.index = 2,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_HIGH,
			.con_id = "bq24292i_irq",
		},
	}, {
		/* LP8557 Backlight controller */
		.board_info = {
			.type = "lp8557",
			.addr = 0x2c,
			.dev_name = "lp8557",
			.platform_data = &lenovo_lp8557_pwm_and_reg_pdata,
		},
		.adapter_path = "\\_SB_.I2C3",
	}, {
		/* LC824206XA Micro USB Switch */
		.board_info = {
			.type = "lc824206xa",
			.addr = 0x48,
			.dev_name = "lc824206xa",
			.swnode = &lenovo_yoga_tab2_1380_lc824206xa_node,
		},
		.adapter_path = "\\_SB_.I2C3",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FC:02",
			.index = 1,
			.trigger = ACPI_LEVEL_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
			.con_id = "lc824206xa_irq",
		},
	}, {
		/* AL3320A ambient light sensor */
		.board_info = {
			.type = "al3320a",
			.addr = 0x1c,
			.dev_name = "al3320a",
		},
		.adapter_path = "\\_SB_.I2C5",
	}, {
		/* LSM303DA accelerometer + magnetometer */
		.board_info = {
			.type = "lsm303d",
			.addr = 0x1d,
			.dev_name = "lsm303d",
			.swnode = &lenovo_yoga_tab2_1380_lms303d_node,
		},
		.adapter_path = "\\_SB_.I2C5",
	}, {
		/* Synaptics RMI touchscreen */
		.board_info = {
			.type = "rmi4_i2c",
			.addr = 0x38,
			.dev_name = "rmi4_i2c",
			.platform_data = &lenovo_yoga_tab2_830_1050_rmi_pdata,
		},
		.adapter_path = "\\_SB_.I2C6",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_APIC,
			.index = 0x45,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_HIGH,
		},
	}
};

static const struct platform_device_info lenovo_yoga_tab2_1380_pdevs[] __initconst = {
	{
		/* For the Tablet 2 Pro 1380's custom fast charging driver */
		.name = "lenovo-yoga-tab2-pro-1380-fastcharger",
		.id = PLATFORM_DEVID_NONE,
	},
};

const char * const lenovo_yoga_tab2_1380_modules[] __initconst = {
	"bq24190_charger",            /* For the Vbus regulator for lc824206xa */
	NULL
};

static int __init lenovo_yoga_tab2_1380_init(void)
{
	int ret;

	/* To verify that the DMI matching works vs the 830 / 1050 models */
	pr_info("detected Lenovo Yoga Tablet 2 Pro 1380F/L\n");

	ret = lenovo_yoga_tab2_830_1050_init_codec();
	if (ret)
		return ret;

	/* SYS_OFF_PRIO_FIRMWARE + 1 so that it runs before acpi_power_off */
	lenovo_yoga_tab2_830_1050_sys_off_handler =
		register_sys_off_handler(SYS_OFF_MODE_POWER_OFF, SYS_OFF_PRIO_FIRMWARE + 1,
					 lenovo_yoga_tab2_830_1050_power_off, NULL);
	if (IS_ERR(lenovo_yoga_tab2_830_1050_sys_off_handler))
		return PTR_ERR(lenovo_yoga_tab2_830_1050_sys_off_handler);

	return 0;
}

static struct gpiod_lookup_table lenovo_yoga_tab2_1380_fc_gpios = {
	.dev_id = "serial0-0",
	.table = {
		GPIO_LOOKUP("INT33FC:00", 57, "uart3_txd", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT33FC:00", 61, "uart3_rxd", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static struct gpiod_lookup_table * const lenovo_yoga_tab2_1380_gpios[] = {
	&lenovo_yoga_tab2_830_1050_codec_gpios,
	&lenovo_yoga_tab2_1380_fc_gpios,
	NULL
};

const struct x86_dev_info lenovo_yoga_tab2_1380_info __initconst = {
	.i2c_client_info = lenovo_yoga_tab2_1380_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(lenovo_yoga_tab2_1380_i2c_clients),
	.pdev_info = lenovo_yoga_tab2_1380_pdevs,
	.pdev_count = ARRAY_SIZE(lenovo_yoga_tab2_1380_pdevs),
	.gpio_button = &lenovo_yoga_tab2_830_1050_lid,
	.gpio_button_count = 1,
	.gpiod_lookup_tables = lenovo_yoga_tab2_1380_gpios,
	.bat_swnode = &generic_lipo_hv_4v35_battery_node,
	.modules = lenovo_yoga_tab2_1380_modules,
	.init = lenovo_yoga_tab2_1380_init,
	.exit = lenovo_yoga_tab2_830_1050_exit,
};

/* Lenovo Yoga Tab 3 Pro YT3-X90F */

/*
 * There are 2 batteries, with 2 bq27500 fuel-gauges and 2 bq25892 chargers,
 * "bq25890-charger-1" is instantiated from: drivers/i2c/busses/i2c-cht-wc.c.
 */
static const char * const lenovo_yt3_bq25892_0_suppliers[] = { "cht_wcove_pwrsrc" };
static const char * const bq25890_1_psy[] = { "bq25890-charger-1" };

static const struct property_entry fg_bq25890_1_supply_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", bq25890_1_psy),
	{ }
};

static const struct software_node fg_bq25890_1_supply_node = {
	.properties = fg_bq25890_1_supply_props,
};

/* bq25892 charger settings for the flat lipo battery behind the screen */
static const struct property_entry lenovo_yt3_bq25892_0_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", lenovo_yt3_bq25892_0_suppliers),
	PROPERTY_ENTRY_U32("linux,iinlim-percentage", 40),
	PROPERTY_ENTRY_BOOL("linux,skip-reset"),
	/* Values taken from Android Factory Image */
	PROPERTY_ENTRY_U32("ti,charge-current", 2048000),
	PROPERTY_ENTRY_U32("ti,battery-regulation-voltage", 4352000),
	PROPERTY_ENTRY_U32("ti,termination-current", 128000),
	PROPERTY_ENTRY_U32("ti,precharge-current", 128000),
	PROPERTY_ENTRY_U32("ti,minimum-sys-voltage", 3700000),
	PROPERTY_ENTRY_U32("ti,boost-voltage", 4998000),
	PROPERTY_ENTRY_U32("ti,boost-max-current", 500000),
	PROPERTY_ENTRY_BOOL("ti,use-ilim-pin"),
	{ }
};

static const struct software_node lenovo_yt3_bq25892_0_node = {
	.properties = lenovo_yt3_bq25892_0_props,
};

static const struct property_entry lenovo_yt3_hideep_ts_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1600),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 2560),
	PROPERTY_ENTRY_U32("touchscreen-max-pressure", 255),
	{ }
};

static const struct software_node lenovo_yt3_hideep_ts_node = {
	.properties = lenovo_yt3_hideep_ts_props,
};

static const struct x86_i2c_client_info lenovo_yt3_i2c_clients[] __initconst = {
	{
		/* bq27500 fuel-gauge for the flat lipo battery behind the screen */
		.board_info = {
			.type = "bq27500",
			.addr = 0x55,
			.dev_name = "bq27500_0",
			.swnode = &fg_bq25890_supply_node,
		},
		.adapter_path = "\\_SB_.PCI0.I2C1",
	}, {
		/* bq25892 charger for the flat lipo battery behind the screen */
		.board_info = {
			.type = "bq25892",
			.addr = 0x6b,
			.dev_name = "bq25892_0",
			.swnode = &lenovo_yt3_bq25892_0_node,
		},
		.adapter_path = "\\_SB_.PCI0.I2C1",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FF:01",
			.index = 5,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
			.con_id = "bq25892_0_irq",
		},
	}, {
		/* bq27500 fuel-gauge for the round li-ion cells in the hinge */
		.board_info = {
			.type = "bq27500",
			.addr = 0x55,
			.dev_name = "bq27500_1",
			.swnode = &fg_bq25890_1_supply_node,
		},
		.adapter_path = "\\_SB_.PCI0.I2C2",
	}, {
		/* HiDeep IST520E Touchscreen */
		.board_info = {
			.type = "hideep_ts",
			.addr = 0x6c,
			.dev_name = "hideep_ts",
			.swnode = &lenovo_yt3_hideep_ts_node,
		},
		.adapter_path = "\\_SB_.PCI0.I2C6",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FF:03",
			.index = 77,
			.trigger = ACPI_LEVEL_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
			.con_id = "hideep_ts_irq",
		},
	}, {
		/* LP8557 Backlight controller */
		.board_info = {
			.type = "lp8557",
			.addr = 0x2c,
			.dev_name = "lp8557",
			.platform_data = &lenovo_lp8557_reg_only_pdata,
		},
		.adapter_path = "\\_SB_.PCI0.I2C1",
	}
};

/*
 * The AOSP 3.5 mm Headset: Accessory Specification gives the following values:
 * Function A Play/Pause:           0 ohm
 * Function D Voice assistant:    135 ohm
 * Function B Volume Up           240 ohm
 * Function C Volume Down         470 ohm
 * Minimum Mic DC resistance     1000 ohm
 * Minimum Ear speaker impedance   16 ohm
 * Note the first max value below must be less then the min. speaker impedance,
 * to allow CTIA/OMTP detection to work. The other max values are the closest
 * value from extcon-arizona.c:arizona_micd_levels halfway 2 button resistances.
 */
static const struct arizona_micd_range arizona_micd_aosp_ranges[] = {
	{ .max =  11, .key = KEY_PLAYPAUSE },
	{ .max = 186, .key = KEY_VOICECOMMAND },
	{ .max = 348, .key = KEY_VOLUMEUP },
	{ .max = 752, .key = KEY_VOLUMEDOWN },
};

/* YT3 WM5102 arizona_micd_config comes from Android kernel sources */
static struct arizona_micd_config lenovo_yt3_wm5102_micd_config[] = {
	{ 0, 1, 0 },
	{ ARIZONA_ACCDET_SRC, 2, 1 },
};

static struct arizona_pdata lenovo_yt3_wm5102_pdata = {
	.irq_flags = IRQF_TRIGGER_LOW,
	.micd_detect_debounce = 200,
	.micd_ranges = arizona_micd_aosp_ranges,
	.num_micd_ranges = ARRAY_SIZE(arizona_micd_aosp_ranges),
	.hpdet_channel = ARIZONA_ACCDET_MODE_HPL,

	/* Below settings come from Android kernel sources */
	.micd_bias_start_time = 1,
	.micd_rate = 6,
	.micd_configs = lenovo_yt3_wm5102_micd_config,
	.num_micd_configs = ARRAY_SIZE(lenovo_yt3_wm5102_micd_config),
	.micbias = {
		[0] = { /* MICBIAS1 */
			.mV = 2800,
			.ext_cap = 1,
			.discharge = 1,
			.soft_start = 0,
			.bypass = 0,
		},
		[1] = { /* MICBIAS2 */
			.mV = 2800,
			.ext_cap = 1,
			.discharge = 1,
			.soft_start = 0,
			.bypass = 0,
		},
		[2] = { /* MICBIAS2 */
			.mV = 2800,
			.ext_cap = 1,
			.discharge = 1,
			.soft_start = 0,
			.bypass = 0,
		},
	},
};

static const struct x86_spi_dev_info lenovo_yt3_spi_devs[] __initconst = {
	{
		/* WM5102 codec */
		.board_info = {
			.modalias = "wm5102",
			.platform_data = &lenovo_yt3_wm5102_pdata,
			.max_speed_hz = 5000000,
		},
		.ctrl_path = "\\_SB_.PCI0.SPI1",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FF:00",
			.index = 91,
			.trigger = ACPI_LEVEL_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
			.con_id = "wm5102_irq",
		},
	}
};

static int __init lenovo_yt3_init(void)
{
	int ret;

	/*
	 * The "bq25892_0" charger IC has its /CE (Charge-Enable) and OTG pins
	 * connected to GPIOs, rather then having them hardwired to the correct
	 * values as is normally done.
	 *
	 * The bq25890_charger driver controls these through I2C, but this only
	 * works if not overridden by the pins. Set these pins here:
	 * 1. Set /CE to 1 to allow charging.
	 * 2. Set OTG to 0 disable V5 boost output since the 5V boost output of
	 *    the main "bq25892_1" charger is used when necessary.
	 */

	/* /CE pin */
	ret = x86_android_tablet_get_gpiod("INT33FF:02", 22, "bq25892_0_ce",
					   true, GPIOD_OUT_HIGH, NULL);
	if (ret < 0)
		return ret;

	/* OTG pin */
	ret = x86_android_tablet_get_gpiod("INT33FF:03", 19, "bq25892_0_otg",
					   false, GPIOD_OUT_LOW, NULL);
	if (ret < 0)
		return ret;

	/* Enable the regulators used by the touchscreen */
	intel_soc_pmic_exec_mipi_pmic_seq_element(0x6e, 0x9b, 0x02, 0xff);
	intel_soc_pmic_exec_mipi_pmic_seq_element(0x6e, 0xa0, 0x02, 0xff);

	return 0;
}

static struct gpiod_lookup_table lenovo_yt3_hideep_gpios = {
	.dev_id = "i2c-hideep_ts",
	.table = {
		GPIO_LOOKUP("INT33FF:00", 7, "reset", GPIO_ACTIVE_LOW),
		{ }
	},
};

static struct gpiod_lookup_table lenovo_yt3_wm5102_gpios = {
	.dev_id = "spi1.0",
	.table = {
		GPIO_LOOKUP("INT33FF:00", 75, "wlf,spkvdd-ena", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT33FF:00", 81, "wlf,ldoena", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT33FF:00", 82, "reset", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("arizona", 2, "wlf,micd-pol", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static struct gpiod_lookup_table * const lenovo_yt3_gpios[] = {
	&lenovo_yt3_hideep_gpios,
	&lenovo_yt3_wm5102_gpios,
	NULL
};

const struct x86_dev_info lenovo_yt3_info __initconst = {
	.i2c_client_info = lenovo_yt3_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(lenovo_yt3_i2c_clients),
	.spi_dev_info = lenovo_yt3_spi_devs,
	.spi_dev_count = ARRAY_SIZE(lenovo_yt3_spi_devs),
	.gpiod_lookup_tables = lenovo_yt3_gpios,
	.init = lenovo_yt3_init,
};
