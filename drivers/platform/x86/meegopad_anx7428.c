// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver to power on the Analogix ANX7428 USB Type-C crosspoint switch
 * on MeeGoPad top-set boxes.
 *
 * The MeeGoPad T8 and T9 are Cherry Trail top-set boxes which
 * use an ANX7428 to provide a Type-C port with USB3.1 Gen 1 and
 * DisplayPort over Type-C alternate mode support.
 *
 * The ANX7428 has a microcontroller which takes care of the PD
 * negotiation and automatically sets the builtin Crosspoint Switch
 * to send the right signal to the 4 highspeed pairs of the Type-C
 * connector. It also takes care of HPD and AUX channel routing for
 * DP alternate mode.
 *
 * IOW the ANX7428 operates fully autonomous and to the x5-Z8350 SoC
 * things look like there simply is a USB-3 Type-A connector and a
 * separate DisplayPort connector. Except that the BIOS does not
 * power on the ANX7428 at boot. This driver takes care of powering
 * on the ANX7428.
 *
 * It should be possible to tell the micro-controller which data- and/or
 * power-role to negotiate and to swap the role(s) after negotiation
 * but the MeeGoPad top-set boxes always draw their power from a separate
 * power-connector and they only support USB host-mode. So this functionality
 * is unnecessary and due to lack of documentation this is tricky to support.
 *
 * For a more complete ANX7428 driver see drivers/usb/misc/anx7418/ of
 * the LineageOS kernel for the LG G5 (International) aka the LG H850:
 * https://github.com/LineageOS/android_kernel_lge_msm8996/
 *
 * (C) Copyright 2024 Hans de Goede <hansg@kernel.org>
 */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/types.h>

/* Register addresses and fields */
#define VENDOR_ID			0x00
#define DEVICE_ID			0x02

#define TX_STATUS			0x16
#define STATUS_SUCCESS			BIT(0)
#define STATUS_ERROR			BIT(1)
#define OCM_STARTUP			BIT(7)

static bool force;
module_param(force, bool, 0444);
MODULE_PARM_DESC(force, "Force the driver to probe on unknown boards");

static const struct acpi_gpio_params enable_gpio = { 0, 0, false };
static const struct acpi_gpio_params reset_gpio = { 1, 0, true };

static const struct acpi_gpio_mapping meegopad_anx7428_gpios[] = {
	{ "enable-gpios", &enable_gpio, 1 },
	{ "reset-gpios", &reset_gpio, 1 },
	{ }
};

static const struct dmi_system_id meegopad_anx7428_ids[] = {
	{
		/* Meegopad T08 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Default string"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Default string"),
			DMI_MATCH(DMI_BOARD_NAME, "T3 MRD"),
			DMI_MATCH(DMI_BOARD_VERSION, "V1.1"),
		},
	},
	{ }
};

static int anx7428_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct gpio_desc *gpio;
	int ret, val;

	if (!dmi_check_system(meegopad_anx7428_ids) && !force) {
		dev_warn(dev, "Not probing unknown board, pass meegopad_anx7428.force=1 to probe");
		return -ENODEV;
	}

	ret = devm_acpi_dev_add_driver_gpios(dev, meegopad_anx7428_gpios);
	if (ret)
		return ret;

	/*
	 * Set GPIOs to desired values while getting them, they are not needed
	 * afterwards. Ordering and delays come from android_kernel_lge_msm8996.
	 */
	gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(gpio))
		return dev_err_probe(dev, PTR_ERR(gpio), "getting enable GPIO\n");

	fsleep(10000);

	gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return dev_err_probe(dev, PTR_ERR(gpio), "getting reset GPIO\n");

	/* Wait for the OCM (On Chip Microcontroller) to start */
	ret = read_poll_timeout(i2c_smbus_read_byte_data, val,
				val >= 0 && (val & OCM_STARTUP),
				5000, 50000, true, client, TX_STATUS);
	if (ret)
		return dev_err_probe(dev, ret,
				     "On Chip Microcontroller did not start, status: 0x%02x\n",
				     val);

	ret = i2c_smbus_read_word_data(client, VENDOR_ID);
	if (ret < 0)
		return dev_err_probe(dev, ret, "reading vendor-id register\n");
	val = ret;

	ret = i2c_smbus_read_word_data(client, DEVICE_ID);
	if (ret < 0)
		return dev_err_probe(dev, ret, "reading device-id register\n");

	dev_dbg(dev, "Powered on ANX7428 id %04x:%04x\n", val, ret);
	return 0;
}

static const struct acpi_device_id anx7428_acpi_match[] = {
	{ "ANXO7418" }, /* ACPI says 7418 (max 2 DP lanes version) but HW is 7428 */
	{ }
};
MODULE_DEVICE_TABLE(acpi, anx7428_acpi_match);

static struct i2c_driver anx7428_driver = {
	.driver = {
		.name = "meegopad_anx7428",
		.acpi_match_table = anx7428_acpi_match,
	},
	.probe = anx7428_probe,
};
module_i2c_driver(anx7428_driver);

MODULE_AUTHOR("Hans de Goede <hansg@kernel.org>");
MODULE_DESCRIPTION("MeeGoPad ANX7428 driver");
MODULE_LICENSE("GPL");
