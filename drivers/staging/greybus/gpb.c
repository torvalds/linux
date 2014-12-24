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
	return 0;

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

static void __exit gpbridge_exit(void)
{
	gb_usb_protocol_exit();
	gb_sdio_protocol_exit();
	gb_uart_protocol_exit();
	gb_pwm_protocol_exit();
	gb_gpio_protocol_exit();
}

module_init(gpbridge_init);
module_exit(gpbridge_exit);

MODULE_LICENSE("GPL");
