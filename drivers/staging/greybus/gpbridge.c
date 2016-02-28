/*
 * Greybus GP Bridge driver
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>

#include "greybus.h"
#include "gpbridge.h"

static int __init gpbridge_init(void)
{
	if (gb_gpio_protocol_init()) {
		pr_err("error initializing gpio protocol\n");
		goto error_gpio;
	}
	if (gb_pwm_protocol_init()) {
		pr_err("error initializing pwm protocol\n");
		goto error_pwm;
	}
	if (gb_uart_protocol_init()) {
		pr_err("error initializing uart protocol\n");
		goto error_uart;
	}
	if (gb_sdio_protocol_init()) {
		pr_err("error initializing sdio protocol\n");
		goto error_sdio;
	}
	if (gb_usb_protocol_init()) {
		pr_err("error initializing usb protocol\n");
		goto error_usb;
	}
	if (gb_i2c_protocol_init()) {
		pr_err("error initializing i2c protocol\n");
		goto error_i2c;
	}
	if (gb_spi_protocol_init()) {
		pr_err("error initializing spi protocol\n");
		goto error_spi;
	}

	return 0;

error_spi:
	gb_i2c_protocol_exit();
error_i2c:
	gb_usb_protocol_exit();
error_usb:
	gb_sdio_protocol_exit();
error_sdio:
	gb_uart_protocol_exit();
error_uart:
	gb_pwm_protocol_exit();
error_pwm:
	gb_gpio_protocol_exit();
error_gpio:
	return -EPROTO;
}
module_init(gpbridge_init);

static void __exit gpbridge_exit(void)
{
	gb_spi_protocol_exit();
	gb_i2c_protocol_exit();
	gb_usb_protocol_exit();
	gb_sdio_protocol_exit();
	gb_uart_protocol_exit();
	gb_pwm_protocol_exit();
	gb_gpio_protocol_exit();
}
module_exit(gpbridge_exit);

/*
 * One large list of all classes we support in the gpbridge.ko module.
 *
 * Due to limitations in older kernels, the different phy .c files can not
 * contain their own MODULE_DEVICE_TABLE(), so put them all here for now.
 */
static const struct greybus_bundle_id bridged_phy_id_table[] = {
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_BRIDGED_PHY) },
	{ },
};
MODULE_DEVICE_TABLE(greybus, bridged_phy_id_table);

MODULE_LICENSE("GPL v2");
