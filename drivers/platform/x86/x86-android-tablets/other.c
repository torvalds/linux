// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DMI based code to deal with broken DSDTs on X86 tablets which ship with
 * Android as (part of) the factory image. The factory kernels shipped on these
 * devices typically have a bunch of things hardcoded, rather than specified
 * in their DSDT.
 *
 * Copyright (C) 2021-2023 Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/acpi.h>
#include <linux/gpio/machine.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#include <dt-bindings/leds/common.h>

#include "shared-psy-info.h"
#include "x86-android-tablets.h"

/* Acer Iconia One 7 B1-750 has an Android factory image with everything hardcoded */
static const char * const acer_b1_750_mount_matrix[] = {
	"-1", "0", "0",
	"0", "1", "0",
	"0", "0", "1"
};

static const struct property_entry acer_b1_750_bma250e_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("mount-matrix", acer_b1_750_mount_matrix),
	{ }
};

static const struct software_node acer_b1_750_bma250e_node = {
	.properties = acer_b1_750_bma250e_props,
};

static const struct x86_i2c_client_info acer_b1_750_i2c_clients[] __initconst = {
	{
		/* Novatek NVT-ts touchscreen */
		.board_info = {
			.type = "NVT-ts",
			.addr = 0x34,
			.dev_name = "NVT-ts",
		},
		.adapter_path = "\\_SB_.I2C4",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FC:02",
			.index = 3,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
			.con_id = "NVT-ts_irq",
		},
	}, {
		/* BMA250E accelerometer */
		.board_info = {
			.type = "bma250e",
			.addr = 0x18,
			.swnode = &acer_b1_750_bma250e_node,
		},
		.adapter_path = "\\_SB_.I2C3",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FC:02",
			.index = 25,
			.trigger = ACPI_LEVEL_SENSITIVE,
			.polarity = ACPI_ACTIVE_HIGH,
			.con_id = "bma250e_irq",
		},
	},
};

static struct gpiod_lookup_table acer_b1_750_nvt_ts_gpios = {
	.dev_id = "i2c-NVT-ts",
	.table = {
		GPIO_LOOKUP("INT33FC:01", 26, "reset", GPIO_ACTIVE_LOW),
		{ }
	},
};

static struct gpiod_lookup_table * const acer_b1_750_gpios[] = {
	&acer_b1_750_nvt_ts_gpios,
	&int3496_reference_gpios,
	NULL
};

const struct x86_dev_info acer_b1_750_info __initconst = {
	.i2c_client_info = acer_b1_750_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(acer_b1_750_i2c_clients),
	.pdev_info = int3496_pdevs,
	.pdev_count = 1,
	.gpiod_lookup_tables = acer_b1_750_gpios,
};

/*
 * Advantech MICA-071
 * This is a standard Windows tablet, but it has an extra "quick launch" button
 * which is not described in the ACPI tables in anyway.
 * Use the x86-android-tablets infra to create a gpio-keys device for this.
 */
static const struct x86_gpio_button advantech_mica_071_button __initconst = {
	.button = {
		.code = KEY_PROG1,
		.active_low = true,
		.desc = "prog1_key",
		.type = EV_KEY,
		.wakeup = false,
		.debounce_interval = 50,
	},
	.chip = "INT33FC:00",
	.pin = 2,
};

const struct x86_dev_info advantech_mica_071_info __initconst = {
	.gpio_button = &advantech_mica_071_button,
	.gpio_button_count = 1,
};

/*
 * When booted with the BIOS set to Android mode the Chuwi Hi8 (CWI509) DSDT
 * contains a whole bunch of bogus ACPI I2C devices and is missing entries
 * for the touchscreen and the accelerometer.
 */
static const struct property_entry chuwi_hi8_gsl1680_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 1665),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1140),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	PROPERTY_ENTRY_BOOL("silead,home-button"),
	PROPERTY_ENTRY_STRING("firmware-name", "gsl1680-chuwi-hi8.fw"),
	{ }
};

static const struct software_node chuwi_hi8_gsl1680_node = {
	.properties = chuwi_hi8_gsl1680_props,
};

static const char * const chuwi_hi8_mount_matrix[] = {
	"1", "0", "0",
	"0", "-1", "0",
	"0", "0", "1"
};

static const struct property_entry chuwi_hi8_bma250e_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("mount-matrix", chuwi_hi8_mount_matrix),
	{ }
};

static const struct software_node chuwi_hi8_bma250e_node = {
	.properties = chuwi_hi8_bma250e_props,
};

static const struct x86_i2c_client_info chuwi_hi8_i2c_clients[] __initconst = {
	{
		/* Silead touchscreen */
		.board_info = {
			.type = "gsl1680",
			.addr = 0x40,
			.swnode = &chuwi_hi8_gsl1680_node,
		},
		.adapter_path = "\\_SB_.I2C4",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_APIC,
			.index = 0x44,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_HIGH,
		},
	}, {
		/* BMA250E accelerometer */
		.board_info = {
			.type = "bma250e",
			.addr = 0x18,
			.swnode = &chuwi_hi8_bma250e_node,
		},
		.adapter_path = "\\_SB_.I2C3",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FC:02",
			.index = 23,
			.trigger = ACPI_LEVEL_SENSITIVE,
			.polarity = ACPI_ACTIVE_HIGH,
			.con_id = "bma250e_irq",
		},
	},
};

static int __init chuwi_hi8_init(struct device *dev)
{
	/*
	 * Avoid the acpi_unregister_gsi() call in x86_acpi_irq_helper_get()
	 * breaking the touchscreen + logging various errors when the Windows
	 * BIOS is used.
	 */
	if (acpi_dev_present("MSSL0001", NULL, 1))
		return -ENODEV;

	return 0;
}

const struct x86_dev_info chuwi_hi8_info __initconst = {
	.i2c_client_info = chuwi_hi8_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(chuwi_hi8_i2c_clients),
	.init = chuwi_hi8_init,
};

/*
 * Cyberbook T116 Android version
 * This comes in both Windows and Android versions and even on Android
 * the DSDT is mostly sane. This tablet has 2 extra general purpose buttons
 * in the button row with the power + volume-buttons labeled P and F.
 * Use the x86-android-tablets infra to create a gpio-keys device for these.
 */
static const struct x86_gpio_button cyberbook_t116_buttons[] __initconst = {
	{
		.button = {
			.code = KEY_PROG1,
			.active_low = true,
			.desc = "prog1_key",
			.type = EV_KEY,
			.wakeup = false,
			.debounce_interval = 50,
		},
		.chip = "INT33FF:00",
		.pin = 30,
	},
	{
		.button = {
			.code = KEY_PROG2,
			.active_low = true,
			.desc = "prog2_key",
			.type = EV_KEY,
			.wakeup = false,
			.debounce_interval = 50,
		},
		.chip = "INT33FF:03",
		.pin = 48,
	},
};

const struct x86_dev_info cyberbook_t116_info __initconst = {
	.gpio_button = cyberbook_t116_buttons,
	.gpio_button_count = ARRAY_SIZE(cyberbook_t116_buttons),
};

#define CZC_EC_EXTRA_PORT	0x68
#define CZC_EC_ANDROID_KEYS	0x63

static int __init czc_p10t_init(struct device *dev)
{
	/*
	 * The device boots up in "Windows 7" mode, when the home button sends a
	 * Windows specific key sequence (Left Meta + D) and the second button
	 * sends an unknown one while also toggling the Radio Kill Switch.
	 * This is a surprising behavior when the second button is labeled "Back".
	 *
	 * The vendor-supplied Android-x86 build switches the device to a "Android"
	 * mode by writing value 0x63 to the I/O port 0x68. This just seems to just
	 * set bit 6 on address 0x96 in the EC region; switching the bit directly
	 * seems to achieve the same result. It uses a "p10t_switcher" to do the
	 * job. It doesn't seem to be able to do anything else, and no other use
	 * of the port 0x68 is known.
	 *
	 * In the Android mode, the home button sends just a single scancode,
	 * which can be handled in Linux userspace more reasonably and the back
	 * button only sends a scancode without toggling the kill switch.
	 * The scancode can then be mapped either to Back or RF Kill functionality
	 * in userspace, depending on how the button is labeled on that particular
	 * model.
	 */
	outb(CZC_EC_ANDROID_KEYS, CZC_EC_EXTRA_PORT);
	return 0;
}

const struct x86_dev_info czc_p10t __initconst = {
	.init = czc_p10t_init,
};

/* Medion Lifetab S10346 tablets have an Android factory image with everything hardcoded */
static const char * const medion_lifetab_s10346_accel_mount_matrix[] = {
	"0", "1", "0",
	"1", "0", "0",
	"0", "0", "1"
};

static const struct property_entry medion_lifetab_s10346_accel_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("mount-matrix", medion_lifetab_s10346_accel_mount_matrix),
	{ }
};

static const struct software_node medion_lifetab_s10346_accel_node = {
	.properties = medion_lifetab_s10346_accel_props,
};

/* Note the LCD panel is mounted upside down, this is correctly indicated in the VBT */
static const struct property_entry medion_lifetab_s10346_touchscreen_props[] = {
	PROPERTY_ENTRY_BOOL("touchscreen-inverted-x"),
	PROPERTY_ENTRY_BOOL("touchscreen-swapped-x-y"),
	{ }
};

static const struct software_node medion_lifetab_s10346_touchscreen_node = {
	.properties = medion_lifetab_s10346_touchscreen_props,
};

static const struct x86_i2c_client_info medion_lifetab_s10346_i2c_clients[] __initconst = {
	{
		/* kxtj21009 accelerometer */
		.board_info = {
			.type = "kxtj21009",
			.addr = 0x0f,
			.dev_name = "kxtj21009",
			.swnode = &medion_lifetab_s10346_accel_node,
		},
		.adapter_path = "\\_SB_.I2C3",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FC:02",
			.index = 23,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_HIGH,
			.con_id = "kxtj21009_irq",
		},
	}, {
		/* goodix touchscreen */
		.board_info = {
			.type = "GDIX1001:00",
			.addr = 0x14,
			.dev_name = "goodix_ts",
			.swnode = &medion_lifetab_s10346_touchscreen_node,
		},
		.adapter_path = "\\_SB_.I2C4",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_APIC,
			.index = 0x44,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
		},
	},
};

static struct gpiod_lookup_table medion_lifetab_s10346_goodix_gpios = {
	.dev_id = "i2c-goodix_ts",
	.table = {
		GPIO_LOOKUP("INT33FC:01", 26, "reset", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT33FC:02", 3, "irq", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static struct gpiod_lookup_table * const medion_lifetab_s10346_gpios[] = {
	&medion_lifetab_s10346_goodix_gpios,
	NULL
};

const struct x86_dev_info medion_lifetab_s10346_info __initconst = {
	.i2c_client_info = medion_lifetab_s10346_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(medion_lifetab_s10346_i2c_clients),
	.gpiod_lookup_tables = medion_lifetab_s10346_gpios,
};

/* Nextbook Ares 8 (BYT) tablets have an Android factory image with everything hardcoded */
static const char * const nextbook_ares8_accel_mount_matrix[] = {
	"0", "-1", "0",
	"-1", "0", "0",
	"0", "0", "1"
};

static const struct property_entry nextbook_ares8_accel_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("mount-matrix", nextbook_ares8_accel_mount_matrix),
	{ }
};

static const struct software_node nextbook_ares8_accel_node = {
	.properties = nextbook_ares8_accel_props,
};

static const struct property_entry nextbook_ares8_touchscreen_props[] = {
	PROPERTY_ENTRY_U32("touchscreen-size-x", 800),
	PROPERTY_ENTRY_U32("touchscreen-size-y", 1280),
	{ }
};

static const struct software_node nextbook_ares8_touchscreen_node = {
	.properties = nextbook_ares8_touchscreen_props,
};

static const struct x86_i2c_client_info nextbook_ares8_i2c_clients[] __initconst = {
	{
		/* Freescale MMA8653FC accelerometer */
		.board_info = {
			.type = "mma8653",
			.addr = 0x1d,
			.dev_name = "mma8653",
			.swnode = &nextbook_ares8_accel_node,
		},
		.adapter_path = "\\_SB_.I2C3",
	}, {
		/* FT5416DQ9 touchscreen controller */
		.board_info = {
			.type = "edt-ft5x06",
			.addr = 0x38,
			.dev_name = "ft5416",
			.swnode = &nextbook_ares8_touchscreen_node,
		},
		.adapter_path = "\\_SB_.I2C4",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FC:02",
			.index = 3,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
			.con_id = "ft5416_irq",
		},
	},
};

static struct gpiod_lookup_table * const nextbook_ares8_gpios[] = {
	&int3496_reference_gpios,
	NULL
};

const struct x86_dev_info nextbook_ares8_info __initconst = {
	.i2c_client_info = nextbook_ares8_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(nextbook_ares8_i2c_clients),
	.pdev_info = int3496_pdevs,
	.pdev_count = 1,
	.gpiod_lookup_tables = nextbook_ares8_gpios,
};

/* Nextbook Ares 8A (CHT) tablets have an Android factory image with everything hardcoded */
static const char * const nextbook_ares8a_accel_mount_matrix[] = {
	"1", "0", "0",
	"0", "-1", "0",
	"0", "0", "1"
};

static const struct property_entry nextbook_ares8a_accel_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("mount-matrix", nextbook_ares8a_accel_mount_matrix),
	{ }
};

static const struct software_node nextbook_ares8a_accel_node = {
	.properties = nextbook_ares8a_accel_props,
};

static const struct x86_i2c_client_info nextbook_ares8a_i2c_clients[] __initconst = {
	{
		/* Freescale MMA8653FC accelerometer */
		.board_info = {
			.type = "mma8653",
			.addr = 0x1d,
			.dev_name = "mma8653",
			.swnode = &nextbook_ares8a_accel_node,
		},
		.adapter_path = "\\_SB_.PCI0.I2C3",
	}, {
		/* FT5416DQ9 touchscreen controller */
		.board_info = {
			.type = "edt-ft5x06",
			.addr = 0x38,
			.dev_name = "ft5416",
			.swnode = &nextbook_ares8_touchscreen_node,
		},
		.adapter_path = "\\_SB_.PCI0.I2C6",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FF:01",
			.index = 17,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
			.con_id = "ft5416_irq",
		},
	},
};

static struct gpiod_lookup_table nextbook_ares8a_ft5416_gpios = {
	.dev_id = "i2c-ft5416",
	.table = {
		GPIO_LOOKUP("INT33FF:01", 25, "reset", GPIO_ACTIVE_LOW),
		{ }
	},
};

static struct gpiod_lookup_table * const nextbook_ares8a_gpios[] = {
	&nextbook_ares8a_ft5416_gpios,
	NULL
};

const struct x86_dev_info nextbook_ares8a_info __initconst = {
	.i2c_client_info = nextbook_ares8a_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(nextbook_ares8a_i2c_clients),
	.gpiod_lookup_tables = nextbook_ares8a_gpios,
};

/*
 * Peaq C1010
 * This is a standard Windows tablet, but it has a special Dolby button.
 * This button has a WMI interface, but that is broken. Instead of trying to
 * use the broken WMI interface, instantiate a gpio-keys device for this.
 */
static const struct x86_gpio_button peaq_c1010_button __initconst = {
	.button = {
		.code = KEY_SOUND,
		.active_low = true,
		.desc = "dolby_key",
		.type = EV_KEY,
		.wakeup = false,
		.debounce_interval = 50,
	},
	.chip = "INT33FC:00",
	.pin = 3,
};

const struct x86_dev_info peaq_c1010_info __initconst = {
	.gpio_button = &peaq_c1010_button,
	.gpio_button_count = 1,
};

/*
 * Whitelabel (sold as various brands) TM800A550L tablets.
 * These tablet's DSDT contains a whole bunch of bogus ACPI I2C devices
 * (removed through acpi_quirk_skip_i2c_client_enumeration()) and
 * the touchscreen firmware node has the wrong GPIOs.
 */
static const char * const whitelabel_tm800a550l_accel_mount_matrix[] = {
	"-1", "0", "0",
	"0", "1", "0",
	"0", "0", "1"
};

static const struct property_entry whitelabel_tm800a550l_accel_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("mount-matrix", whitelabel_tm800a550l_accel_mount_matrix),
	{ }
};

static const struct software_node whitelabel_tm800a550l_accel_node = {
	.properties = whitelabel_tm800a550l_accel_props,
};

static const struct property_entry whitelabel_tm800a550l_goodix_props[] = {
	PROPERTY_ENTRY_STRING("firmware-name", "gt912-tm800a550l.fw"),
	PROPERTY_ENTRY_STRING("goodix,config-name", "gt912-tm800a550l.cfg"),
	PROPERTY_ENTRY_U32("goodix,main-clk", 54),
	{ }
};

static const struct software_node whitelabel_tm800a550l_goodix_node = {
	.properties = whitelabel_tm800a550l_goodix_props,
};

static const struct x86_i2c_client_info whitelabel_tm800a550l_i2c_clients[] __initconst = {
	{
		/* goodix touchscreen */
		.board_info = {
			.type = "GDIX1001:00",
			.addr = 0x14,
			.dev_name = "goodix_ts",
			.swnode = &whitelabel_tm800a550l_goodix_node,
		},
		.adapter_path = "\\_SB_.I2C2",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_APIC,
			.index = 0x44,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_HIGH,
		},
	}, {
		/* kxcj91008 accelerometer */
		.board_info = {
			.type = "kxcj91008",
			.addr = 0x0f,
			.dev_name = "kxcj91008",
			.swnode = &whitelabel_tm800a550l_accel_node,
		},
		.adapter_path = "\\_SB_.I2C3",
	},
};

static struct gpiod_lookup_table whitelabel_tm800a550l_goodix_gpios = {
	.dev_id = "i2c-goodix_ts",
	.table = {
		GPIO_LOOKUP("INT33FC:01", 26, "reset", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT33FC:02", 3, "irq", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static struct gpiod_lookup_table * const whitelabel_tm800a550l_gpios[] = {
	&whitelabel_tm800a550l_goodix_gpios,
	NULL
};

const struct x86_dev_info whitelabel_tm800a550l_info __initconst = {
	.i2c_client_info = whitelabel_tm800a550l_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(whitelabel_tm800a550l_i2c_clients),
	.gpiod_lookup_tables = whitelabel_tm800a550l_gpios,
};

/*
 * The firmware node for ktd2026 on Xaomi pad2. It composed of a RGB LED node
 * with three subnodes for each color (B/G/R). The RGB LED node is named
 * "multi-led" to align with the name in the device tree.
 */

/* Main firmware node for ktd2026 */
static const struct software_node ktd2026_node = {
	.name = "ktd2026",
};

static const struct property_entry ktd2026_rgb_led_props[] = {
	PROPERTY_ENTRY_U32("reg", 0),
	PROPERTY_ENTRY_U32("color", LED_COLOR_ID_RGB),
	PROPERTY_ENTRY_STRING("label", "mipad2:rgb:indicator"),
	PROPERTY_ENTRY_STRING("linux,default-trigger", "bq27520-0-charging-orange-full-green"),
	{ }
};

static const struct software_node ktd2026_rgb_led_node = {
	.name = "multi-led",
	.properties = ktd2026_rgb_led_props,
	.parent = &ktd2026_node,
};

static const struct property_entry ktd2026_blue_led_props[] = {
	PROPERTY_ENTRY_U32("reg", 0),
	PROPERTY_ENTRY_U32("color", LED_COLOR_ID_BLUE),
	{ }
};

static const struct software_node ktd2026_blue_led_node = {
	.properties = ktd2026_blue_led_props,
	.parent = &ktd2026_rgb_led_node,
};

static const struct property_entry ktd2026_green_led_props[] = {
	PROPERTY_ENTRY_U32("reg", 1),
	PROPERTY_ENTRY_U32("color", LED_COLOR_ID_GREEN),
	{ }
};

static const struct software_node ktd2026_green_led_node = {
	.properties = ktd2026_green_led_props,
	.parent = &ktd2026_rgb_led_node,
};

static const struct property_entry ktd2026_red_led_props[] = {
	PROPERTY_ENTRY_U32("reg", 2),
	PROPERTY_ENTRY_U32("color", LED_COLOR_ID_RED),
	{ }
};

static const struct software_node ktd2026_red_led_node = {
	.properties = ktd2026_red_led_props,
	.parent = &ktd2026_rgb_led_node,
};

static const struct software_node *ktd2026_node_group[] = {
	&ktd2026_node,
	&ktd2026_rgb_led_node,
	&ktd2026_red_led_node,
	&ktd2026_green_led_node,
	&ktd2026_blue_led_node,
	NULL
};

/*
 * For the LEDs which backlight the Menu / Home / Back capacitive buttons on
 * the bottom bezel. These are attached to a TPS61158 LED controller which
 * is controlled by the "pwm_soc_lpss_2" PWM output.
 */
#define XIAOMI_MIPAD2_LED_PERIOD_NS		19200
#define XIAOMI_MIPAD2_LED_MAX_DUTY_NS		 6000 /* From Android kernel */

static struct pwm_device *xiaomi_mipad2_led_pwm;

static int xiaomi_mipad2_brightness_set(struct led_classdev *led_cdev,
					enum led_brightness val)
{
	struct pwm_state state = {
		.period = XIAOMI_MIPAD2_LED_PERIOD_NS,
		.duty_cycle = XIAOMI_MIPAD2_LED_MAX_DUTY_NS * val / LED_FULL,
		/* Always set PWM enabled to avoid the pin floating */
		.enabled = true,
	};

	return pwm_apply_might_sleep(xiaomi_mipad2_led_pwm, &state);
}

static int __init xiaomi_mipad2_init(struct device *dev)
{
	struct led_classdev *led_cdev;
	int ret;

	xiaomi_mipad2_led_pwm = devm_pwm_get(dev, "pwm_soc_lpss_2");
	if (IS_ERR(xiaomi_mipad2_led_pwm))
		return dev_err_probe(dev, PTR_ERR(xiaomi_mipad2_led_pwm), "getting pwm\n");

	led_cdev = devm_kzalloc(dev, sizeof(*led_cdev), GFP_KERNEL);
	if (!led_cdev)
		return -ENOMEM;

	led_cdev->name = "mipad2:white:touch-buttons-backlight";
	led_cdev->max_brightness = LED_FULL;
	led_cdev->default_trigger = "input-events";
	led_cdev->brightness_set_blocking = xiaomi_mipad2_brightness_set;
	/* Turn LED off during suspend */
	led_cdev->flags = LED_CORE_SUSPENDRESUME;

	ret = devm_led_classdev_register(dev, led_cdev);
	if (ret)
		return dev_err_probe(dev, ret, "registering LED\n");

	return software_node_register_node_group(ktd2026_node_group);
}

static void xiaomi_mipad2_exit(void)
{
	software_node_unregister_node_group(ktd2026_node_group);
}

/*
 * If the EFI bootloader is not Xiaomi's own signed Android loader, then the
 * Xiaomi Mi Pad 2 X86 tablet sets OSID in the DSDT to 1 (Windows), causing
 * a bunch of devices to be hidden.
 *
 * This takes care of instantiating the hidden devices manually.
 */
static const struct x86_i2c_client_info xiaomi_mipad2_i2c_clients[] __initconst = {
	{
		/* BQ27520 fuel-gauge */
		.board_info = {
			.type = "bq27520",
			.addr = 0x55,
			.dev_name = "bq27520",
			.swnode = &fg_bq25890_supply_node,
		},
		.adapter_path = "\\_SB_.PCI0.I2C1",
	}, {
		/* KTD2026 RGB notification LED controller */
		.board_info = {
			.type = "ktd2026",
			.addr = 0x30,
			.dev_name = "ktd2026",
			.swnode = &ktd2026_node,
		},
		.adapter_path = "\\_SB_.PCI0.I2C3",
	},
};

const struct x86_dev_info xiaomi_mipad2_info __initconst = {
	.i2c_client_info = xiaomi_mipad2_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(xiaomi_mipad2_i2c_clients),
	.init = xiaomi_mipad2_init,
	.exit = xiaomi_mipad2_exit,
};
