/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/amba/bus.h>

#include <mach/hardware.h>
#include <mach/setup.h>

#define __MEM_4K_RESOURCE(x) \
	.res = {.start = (x), .end = (x) + SZ_4K - 1, .flags = IORESOURCE_MEM}

struct amba_device ux500_pl031_device = {
	.dev = {
		.init_name = "pl031",
	},
	.res = {
		.start	= UX500_RTC_BASE,
		.end	= UX500_RTC_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	.irq = {IRQ_RTC_RTT, NO_IRQ},
};

struct amba_device ux500_uart0_device = {
	.dev = { .init_name = "uart0" },
	__MEM_4K_RESOURCE(UX500_UART0_BASE),
	.irq = {IRQ_UART0, NO_IRQ},
};

struct amba_device ux500_uart1_device = {
	.dev = { .init_name = "uart1" },
	__MEM_4K_RESOURCE(UX500_UART1_BASE),
	.irq = {IRQ_UART1, NO_IRQ},
};

struct amba_device ux500_uart2_device = {
	.dev = { .init_name = "uart2" },
	__MEM_4K_RESOURCE(UX500_UART2_BASE),
	.irq = {IRQ_UART2, NO_IRQ},
};

#define UX500_I2C_RESOURCES(id, size)				\
static struct resource ux500_i2c##id##_resources[] = {		\
	[0] = {							\
		.start	= UX500_I2C##id##_BASE,			\
		.end	= UX500_I2C##id##_BASE + size - 1,	\
		.flags	= IORESOURCE_MEM,			\
	},							\
	[1] = {							\
		.start	= IRQ_I2C##id,				\
		.end	= IRQ_I2C##id,				\
		.flags	= IORESOURCE_IRQ			\
	}							\
}

UX500_I2C_RESOURCES(1, SZ_4K);
UX500_I2C_RESOURCES(2, SZ_4K);
UX500_I2C_RESOURCES(3, SZ_4K);

#define UX500_I2C_PDEVICE(cid)					\
struct platform_device ux500_i2c##cid##_device = {		\
	.name		= "nmk-i2c",				\
	.id		= cid,					\
	.num_resources	= 2,					\
	.resource	= ux500_i2c##cid##_resources,		\
}

UX500_I2C_PDEVICE(1);
UX500_I2C_PDEVICE(2);
UX500_I2C_PDEVICE(3);

void __init amba_add_devices(struct amba_device *devs[], int num)
{
	int i;

	for (i = 0; i < num; i++) {
		struct amba_device *d = devs[i];
		amba_device_register(d, &iomem_resource);
	}
}
