/*
 * s6105 platform devices
 *
 * Copyright (c) 2009 emlix GmbH
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>

#include <variant/hardware.h>

#define UART_INTNUM		4

static const signed char uart_irq_mappings[] = {
	S6_INTC_UART(0),
	S6_INTC_UART(1),
	-1,
};

const signed char *platform_irq_mappings[NR_IRQS] = {
	[UART_INTNUM] = uart_irq_mappings,
};

static struct plat_serial8250_port serial_platform_data[] = {
	{
		.membase = (void *)S6_REG_UART + 0x0000,
		.mapbase = S6_REG_UART + 0x0000,
		.irq = UART_INTNUM,
		.uartclk = S6_SCLK,
		.regshift = 2,
		.iotype = SERIAL_IO_MEM,
		.flags = ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST,
	},
	{
		.membase = (void *)S6_REG_UART + 0x1000,
		.mapbase = S6_REG_UART + 0x1000,
		.irq = UART_INTNUM,
		.uartclk = S6_SCLK,
		.regshift = 2,
		.iotype = SERIAL_IO_MEM,
		.flags = ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST,
	},
	{ },
};

static struct platform_device platform_devices[] = {
	{
		.name = "serial8250",
		.id = PLAT8250_DEV_PLATFORM,
		.dev = {
			.platform_data = serial_platform_data,
		},
	},
};

static int __init device_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(platform_devices); i++)
		platform_device_register(&platform_devices[i]);
	return 0;
}
arch_initcall_sync(device_init);
