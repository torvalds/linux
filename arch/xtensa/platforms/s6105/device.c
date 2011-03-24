/*
 * s6105 platform devices
 *
 * Copyright (c) 2009 emlix GmbH
 */

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>

#include <variant/hardware.h>
#include <variant/dmac.h>

#include <platform/gpio.h>

#define GPIO3_INTNUM		3
#define UART_INTNUM		4
#define GMAC_INTNUM		5

static const signed char gpio3_irq_mappings[] = {
	S6_INTC_GPIO(3),
	-1
};

static const signed char uart_irq_mappings[] = {
	S6_INTC_UART(0),
	S6_INTC_UART(1),
	-1,
};

static const signed char gmac_irq_mappings[] = {
	S6_INTC_GMAC_STAT,
	S6_INTC_GMAC_ERR,
	S6_INTC_DMA_HOSTTERMCNT(0),
	S6_INTC_DMA_HOSTTERMCNT(1),
	-1
};

const signed char *platform_irq_mappings[NR_IRQS] = {
	[GPIO3_INTNUM] = gpio3_irq_mappings,
	[UART_INTNUM] = uart_irq_mappings,
	[GMAC_INTNUM] = gmac_irq_mappings,
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

static struct resource s6_gmac_resource[] = {
	{
		.name   = "mem",
		.start  = (resource_size_t)S6_REG_GMAC,
		.end    = (resource_size_t)S6_REG_GMAC + 0x10000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "dma",
		.start  = (resource_size_t)
			DMA_CHNL(S6_REG_HIFDMA, S6_HIFDMA_GMACTX),
		.end    = (resource_size_t)
			DMA_CHNL(S6_REG_HIFDMA, S6_HIFDMA_GMACTX) + 0x100 - 1,
		.flags  = IORESOURCE_DMA,
	},
	{
		.name   = "dma",
		.start  = (resource_size_t)
			DMA_CHNL(S6_REG_HIFDMA, S6_HIFDMA_GMACRX),
		.end    = (resource_size_t)
			DMA_CHNL(S6_REG_HIFDMA, S6_HIFDMA_GMACRX) + 0x100 - 1,
		.flags  = IORESOURCE_DMA,
	},
	{
		.name   = "io",
		.start  = (resource_size_t)S6_MEM_GMAC,
		.end    = (resource_size_t)S6_MEM_GMAC + 0x2000000 - 1,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "irq",
		.start  = (resource_size_t)GMAC_INTNUM,
		.flags  = IORESOURCE_IRQ,
	},
	{
		.name   = "irq",
		.start  = (resource_size_t)PHY_POLL,
		.flags  = IORESOURCE_IRQ,
	},
};

static int __init prepare_phy_irq(int pin)
{
	int irq;
	if (gpio_request(pin, "s6gmac_phy") < 0)
		goto fail;
	if (gpio_direction_input(pin) < 0)
		goto free;
	irq = gpio_to_irq(pin);
	if (irq < 0)
		goto free;
	if (irq_set_irq_type(irq, IRQ_TYPE_LEVEL_LOW) < 0)
		goto free;
	return irq;
free:
	gpio_free(pin);
fail:
	return PHY_POLL;
}

static struct platform_device platform_devices[] = {
	{
		.name = "serial8250",
		.id = PLAT8250_DEV_PLATFORM,
		.dev = {
			.platform_data = serial_platform_data,
		},
	},
	{
		.name = "s6gmac",
		.id = 0,
		.resource = s6_gmac_resource,
		.num_resources = ARRAY_SIZE(s6_gmac_resource),
	},
	{
		I2C_BOARD_INFO("m41t62", S6I2C_ADDR_M41T62),
	},
};

static int __init device_init(void)
{
	int i;

	s6_gmac_resource[5].start = prepare_phy_irq(GPIO_PHY_IRQ);

	for (i = 0; i < ARRAY_SIZE(platform_devices); i++)
		platform_device_register(&platform_devices[i]);
	return 0;
}
arch_initcall_sync(device_init);
