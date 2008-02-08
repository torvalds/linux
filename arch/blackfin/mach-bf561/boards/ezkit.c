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
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/spi.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/ata_platform.h>
#include <asm/dma.h>
#include <asm/bfin5xx_spi.h>
#include <asm/portmux.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
const char bfin_board_name[] = "ADDS-BF561-EZKIT";

#define ISP1761_BASE       0x2C0F0000
#define ISP1761_IRQ        IRQ_PF10

#if defined(CONFIG_USB_ISP1760_HCD) || defined(CONFIG_USB_ISP1760_HCD_MODULE)
static struct resource bfin_isp1761_resources[] = {
	{
		.name	= "isp1761-regs",
		.start  = ISP1761_BASE + 0x00000000,
		.end    = ISP1761_BASE + 0x000fffff,
		.flags  = IORESOURCE_MEM,
	},
	{
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
	unsigned int num_devices = ARRAY_SIZE(bfin_isp1761_devices);

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

#if defined(CONFIG_USB_ISP1362_HCD) || defined(CONFIG_USB_ISP1362_HCD_MODULE)
#include <linux/usb/isp1362.h>

static struct resource isp1362_hcd_resources[] = {
	{
		.start = 0x2c060000,
		.end = 0x2c060000,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 0x2c060004,
		.end = 0x2c060004,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_PF8,
		.end = IRQ_PF8,
		.flags = IORESOURCE_IRQ,
	},
};

static struct isp1362_platform_data isp1362_priv = {
	.sel15Kres = 1,
	.clknotstop = 0,
	.oc_enable = 0,
	.int_act_high = 0,
	.int_edge_triggered = 0,
	.remote_wakeup_connected = 0,
	.no_power_switching = 1,
	.power_switching_mode = 0,
};

static struct platform_device isp1362_hcd_device = {
	.name = "isp1362-hcd",
	.id = 0,
	.dev = {
		.platform_data = &isp1362_priv,
	},
	.num_resources = ARRAY_SIZE(isp1362_hcd_resources),
	.resource = isp1362_hcd_resources,
};
#endif

#if defined(CONFIG_USB_NET2272) || defined(CONFIG_USB_NET2272_MODULE)
static struct resource net2272_bfin_resources[] = {
	{
		.start = 0x2C000000,
		.end = 0x2C000000 + 0x7F,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_PF10,
		.end = IRQ_PF10,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct platform_device net2272_bfin_device = {
	.name = "net2272",
	.id = -1,
	.num_resources = ARRAY_SIZE(net2272_bfin_resources),
	.resource = net2272_bfin_resources,
};
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
	}, {

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

#if defined(CONFIG_AX88180) || defined(CONFIG_AX88180_MODULE)
static struct resource ax88180_resources[] = {
	[0] = {
		.start	= 0x2c000000,
		.end	= 0x2c000000 + 0x8000,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_PF10,
		.end	= IRQ_PF10,
		.flags	= (IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL),
	},
};

static struct platform_device ax88180_device = {
	.name		= "ax88180",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ax88180_resources),
	.resource	= ax88180_resources,
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

static struct mtd_partition ezkit_partitions[] = {
	{
		.name       = "Bootloader",
		.size       = 0x20000,
		.offset     = 0,
	}, {
		.name       = "Kernel",
		.size       = 0xE0000,
		.offset     = MTDPART_OFS_APPEND,
	}, {
		.name       = "RootFS",
		.size       = MTDPART_SIZ_FULL,
		.offset     = MTDPART_OFS_APPEND,
	}
};

static struct physmap_flash_data ezkit_flash_data = {
	.width      = 2,
	.parts      = ezkit_partitions,
	.nr_parts   = ARRAY_SIZE(ezkit_partitions),
};

static struct resource ezkit_flash_resource = {
	.start = 0x20000000,
	.end   = 0x207fffff,
	.flags = IORESOURCE_MEM,
};

static struct platform_device ezkit_flash_device = {
	.name          = "physmap-flash",
	.id            = 0,
	.dev = {
		.platform_data = &ezkit_flash_data,
	},
	.num_resources = 1,
	.resource      = &ezkit_flash_resource,
};

#ifdef CONFIG_SPI_BFIN
#if defined(CONFIG_SND_BLACKFIN_AD1836) \
	|| defined(CONFIG_SND_BLACKFIN_AD1836_MODULE)
static struct bfin5xx_spi_chip ad1836_spi_chip_info = {
	.enable_dma = 0,
	.bits_per_word = 16,
};
#endif

#if defined(CONFIG_SPI_SPIDEV) || defined(CONFIG_SPI_SPIDEV_MODULE)
static struct bfin5xx_spi_chip spidev_chip_info = {
	.enable_dma = 0,
	.bits_per_word = 8,
};
#endif
#endif

/* SPI (0) */
static struct resource bfin_spi0_resource[] = {
	[0] = {
		.start = SPI0_REGBASE,
		.end   = SPI0_REGBASE + 0xFF,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = CH_SPI,
		.end   = CH_SPI,
		.flags = IORESOURCE_IRQ,
	}
};

/* SPI controller data */
static struct bfin5xx_spi_master bfin_spi0_info = {
	.num_chipselect = 8,
	.enable_dma = 1,  /* master has the ability to do dma transfer */
	.pin_req = {P_SPI0_SCK, P_SPI0_MISO, P_SPI0_MOSI, 0},
};

static struct platform_device bfin_spi0_device = {
	.name = "bfin-spi",
	.id = 0, /* Bus number */
	.num_resources = ARRAY_SIZE(bfin_spi0_resource),
	.resource = bfin_spi0_resource,
	.dev = {
		.platform_data = &bfin_spi0_info, /* Passed to driver */
	},
};

static struct spi_board_info bfin_spi_board_info[] __initdata = {
#if defined(CONFIG_SND_BLACKFIN_AD1836) \
	|| defined(CONFIG_SND_BLACKFIN_AD1836_MODULE)
	{
		.modalias = "ad1836-spi",
		.max_speed_hz = 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = CONFIG_SND_BLACKFIN_SPI_PFBIT,
		.controller_data = &ad1836_spi_chip_info,
	},
#endif
#if defined(CONFIG_SPI_SPIDEV) || defined(CONFIG_SPI_SPIDEV_MODULE)
	{
		.modalias = "spidev",
		.max_speed_hz = 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1,
		.controller_data = &spidev_chip_info,
	},
#endif
};

#if defined(CONFIG_PATA_PLATFORM) || defined(CONFIG_PATA_PLATFORM_MODULE)
#define PATA_INT	55

static struct pata_platform_info bfin_pata_platform_data = {
	.ioport_shift = 1,
	.irq_type = IRQF_TRIGGER_HIGH | IRQF_DISABLED,
};

static struct resource bfin_pata_resources[] = {
	{
		.start = 0x20314020,
		.end = 0x2031403F,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = 0x2031401C,
		.end = 0x2031401F,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = PATA_INT,
		.end = PATA_INT,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_pata_device = {
	.name = "pata_platform",
	.id = -1,
	.num_resources = ARRAY_SIZE(bfin_pata_resources),
	.resource = bfin_pata_resources,
	.dev = {
		.platform_data = &bfin_pata_platform_data,
	}
};
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
#include <linux/input.h>
#include <linux/gpio_keys.h>

static struct gpio_keys_button bfin_gpio_keys_table[] = {
	{BTN_0, GPIO_PF5, 1, "gpio-keys: BTN0"},
	{BTN_1, GPIO_PF6, 1, "gpio-keys: BTN1"},
	{BTN_2, GPIO_PF7, 1, "gpio-keys: BTN2"},
	{BTN_3, GPIO_PF8, 1, "gpio-keys: BTN3"},
};

static struct gpio_keys_platform_data bfin_gpio_keys_data = {
	.buttons        = bfin_gpio_keys_table,
	.nbuttons       = ARRAY_SIZE(bfin_gpio_keys_table),
};

static struct platform_device bfin_device_gpiokeys = {
	.name      = "gpio-keys",
	.dev = {
		.platform_data = &bfin_gpio_keys_data,
	},
};
#endif

#if defined(CONFIG_I2C_GPIO) || defined(CONFIG_I2C_GPIO_MODULE)
#include <linux/i2c-gpio.h>

static struct i2c_gpio_platform_data i2c_gpio_data = {
	.sda_pin		= 1,
	.scl_pin		= 0,
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.udelay			= 40,
};

static struct platform_device i2c_gpio_device = {
	.name		= "i2c-gpio",
	.id		= 0,
	.dev		= {
		.platform_data	= &i2c_gpio_data,
	},
};
#endif

static struct platform_device *ezkit_devices[] __initdata = {
#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
	&smc91x_device,
#endif

#if defined(CONFIG_AX88180) || defined(CONFIG_AX88180_MODULE)
	&ax88180_device,
#endif

#if defined(CONFIG_USB_NET2272) || defined(CONFIG_USB_NET2272_MODULE)
	&net2272_bfin_device,
#endif

#if defined(CONFIG_SPI_BFIN) || defined(CONFIG_SPI_BFIN_MODULE)
	&bfin_spi0_device,
#endif

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
	&bfin_uart_device,
#endif

#if defined(CONFIG_PATA_PLATFORM) || defined(CONFIG_PATA_PLATFORM_MODULE)
	&bfin_pata_device,
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
	&bfin_device_gpiokeys,
#endif

#if defined(CONFIG_I2C_GPIO) || defined(CONFIG_I2C_GPIO_MODULE)
	&i2c_gpio_device,
#endif

#if defined(CONFIG_USB_ISP1362_HCD) || defined(CONFIG_USB_ISP1362_HCD_MODULE)
	&isp1362_hcd_device,
#endif

	&ezkit_flash_device,
};

static int __init ezkit_init(void)
{
	int ret;

	printk(KERN_INFO "%s(): registering device resources\n", __func__);

	ret = platform_add_devices(ezkit_devices, ARRAY_SIZE(ezkit_devices));
	if (ret < 0)
		return ret;

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
	bfin_write_FIO0_DIR(bfin_read_FIO0_DIR() | (1 << 12));
	SSYNC();
#endif

#if defined(CONFIG_SPI_BFIN) || defined(CONFIG_SPI_BFIN_MODULE)
	spi_register_board_info(bfin_spi_board_info,
				ARRAY_SIZE(bfin_spi_board_info));
#endif

#if defined(CONFIG_PATA_PLATFORM) || defined(CONFIG_PATA_PLATFORM_MODULE)
	irq_desc[PATA_INT].status |= IRQ_NOAUTOEN;
#endif
	return 0;
}

arch_initcall(ezkit_init);
