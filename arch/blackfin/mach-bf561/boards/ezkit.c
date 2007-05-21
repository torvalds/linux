/*
 * File:         arch/blackfin/mach-bf561/ezkit.c
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <asm/irq.h>
#include <asm/bfin5xx_spi.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
char *bfin_board_name = "ADDS-BF561-EZKIT";

#define ISP1761_BASE       0x2C0F0000
#define ISP1761_IRQ        IRQ_PF10

#if defined(CONFIG_USB_ISP1760_HCD) || defined(CONFIG_USB_ISP1760_HCD_MODULE)
static struct resource bfin_isp1761_resources[] = {
	[0] = {
		.name	= "isp1761-regs",
		.start  = ISP1761_BASE + 0x00000000,
		.end    = ISP1761_BASE + 0x000fffff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = ISP1761_IRQ,
		.end    = ISP1761_IRQ,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_isp1761_device = {
	.name           = "isp1761",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(bfin_isp1761_resources),
	.resource       = bfin_isp1761_resources,
};

static struct platform_device *bfin_isp1761_devices[] = {
	&bfin_isp1761_device,
};

int __init bfin_isp1761_init(void)
{
	unsigned int num_devices=ARRAY_SIZE(bfin_isp1761_devices);

	printk(KERN_INFO "%s(): registering device resources\n", __FUNCTION__);
	set_irq_type(ISP1761_IRQ, IRQF_TRIGGER_FALLING);

	return platform_add_devices(bfin_isp1761_devices, num_devices);
}

void __exit bfin_isp1761_exit(void)
{
	platform_device_unregister(&bfin_isp1761_device);
}

arch_initcall(bfin_isp1761_init);
#endif

/*
 *  USB-LAN EzExtender board
 *  Driver needs to know address, irq and flag pin.
 */
#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
static struct resource smc91x_resources[] = {
	{
		.name = "smc91x-regs",
		.start = 0x2C010300,
		.end = 0x2C010300 + 16,
		.flags = IORESOURCE_MEM,
	},{

		.start = IRQ_PF9,
		.end = IRQ_PF9,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

static struct platform_device smc91x_device = {
	.name = "smc91x",
	.id = 0,
	.num_resources = ARRAY_SIZE(smc91x_resources),
	.resource = smc91x_resources,
};
#endif

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
static struct resource bfin_uart_resources[] = {
        {
                .start = 0xFFC00400,
                .end = 0xFFC004FF,
                .flags = IORESOURCE_MEM,
        },
};

static struct platform_device bfin_uart_device = {
        .name = "bfin-uart",
        .id = 1,
        .num_resources = ARRAY_SIZE(bfin_uart_resources),
        .resource = bfin_uart_resources,
};
#endif

#ifdef CONFIG_SPI_BFIN
#if defined(CONFIG_SND_BLACKFIN_AD1836) \
	|| defined(CONFIG_SND_BLACKFIN_AD1836_MODULE)
static struct bfin5xx_spi_chip ad1836_spi_chip_info = {
	.enable_dma = 0,
	.bits_per_word = 16,
};
#endif
#endif

/* SPI controller data */
static struct bfin5xx_spi_master spi_bfin_master_info = {
	.num_chipselect = 8,
	.enable_dma = 1,  /* master has the ability to do dma transfer */
};

static struct platform_device spi_bfin_master_device = {
	.name = "bfin-spi-master",
	.id = 1, /* Bus number */
	.dev = {
		.platform_data = &spi_bfin_master_info, /* Passed to driver */
	},
};

static struct spi_board_info bfin_spi_board_info[] __initdata = {
#if defined(CONFIG_SND_BLACKFIN_AD1836) \
	|| defined(CONFIG_SND_BLACKFIN_AD1836_MODULE)
	{
		.modalias = "ad1836-spi",
		.max_speed_hz = 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 1,
		.chip_select = CONFIG_SND_BLACKFIN_SPI_PFBIT,
		.controller_data = &ad1836_spi_chip_info,
	},
#endif
};

static struct platform_device *ezkit_devices[] __initdata = {
#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
	&smc91x_device,
#endif
#if defined(CONFIG_SPI_BFIN) || defined(CONFIG_SPI_BFIN_MODULE)
	&spi_bfin_master_device,
#endif
#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
        &bfin_uart_device,
#endif
};

static int __init ezkit_init(void)
{
	int ret;

	printk(KERN_INFO "%s(): registering device resources\n", __FUNCTION__);
	ret = platform_add_devices(ezkit_devices,
		 ARRAY_SIZE(ezkit_devices));
	if (ret < 0)
		return ret;
	return spi_register_board_info(bfin_spi_board_info,
				ARRAY_SIZE(bfin_spi_board_info));
}

arch_initcall(ezkit_init);
