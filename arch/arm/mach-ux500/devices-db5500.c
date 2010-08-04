/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>

#include <mach/hardware.h>
#include <mach/devices.h>

static struct nmk_gpio_platform_data u5500_gpio_data[] = {
	GPIO_DATA("GPIO-0-31", 0),
	GPIO_DATA("GPIO-32-63", 32), /* 36..63 not routed to pin */
	GPIO_DATA("GPIO-64-95", 64), /* 83..95 not routed to pin */
	GPIO_DATA("GPIO-96-127", 96), /* 102..127 not routed to pin */
	GPIO_DATA("GPIO-128-159", 128), /* 149..159 not routed to pin */
	GPIO_DATA("GPIO-160-191", 160),
	GPIO_DATA("GPIO-192-223", 192),
	GPIO_DATA("GPIO-224-255", 224), /* 228..255 not routed to pin */
};

static struct resource u5500_gpio_resources[] = {
	GPIO_RESOURCE(0),
	GPIO_RESOURCE(1),
	GPIO_RESOURCE(2),
	GPIO_RESOURCE(3),
	GPIO_RESOURCE(4),
	GPIO_RESOURCE(5),
	GPIO_RESOURCE(6),
	GPIO_RESOURCE(7),
};

struct platform_device u5500_gpio_devs[] = {
	GPIO_DEVICE(0),
	GPIO_DEVICE(1),
	GPIO_DEVICE(2),
	GPIO_DEVICE(3),
	GPIO_DEVICE(4),
	GPIO_DEVICE(5),
	GPIO_DEVICE(6),
	GPIO_DEVICE(7),
};
