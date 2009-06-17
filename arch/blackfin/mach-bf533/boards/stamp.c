/*
 * File:         arch/blackfin/mach-bf533/stamp.c
 * Based on:     arch/blackfin/mach-bf533/ezkit.c
 * Author:       Aidan Williams <aidan@nicta.com.au>
 *
 * Created:      2005
 * Description:  Board Info File for the BF533-STAMP
 *
 * Modified:
 *               Copyright 2005 National ICT Australia (NICTA)
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
#include <linux/spi/flash.h>
#if defined(CONFIG_USB_ISP1362_HCD) || defined(CONFIG_USB_ISP1362_HCD_MODULE)
#include <linux/usb/isp1362.h>
#endif
#include <linux/irq.h>
#include <linux/i2c.h>
#include <asm/dma.h>
#include <asm/bfin5xx_spi.h>
#include <asm/reboot.h>
#include <asm/portmux.h>
#include <asm/dpmc.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
const char bfin_board_name[] = "ADI BF533-STAMP";

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
static struct platform_device rtc_device = {
	.name = "rtc-bfin",
	.id   = -1,
};
#endif

/*
 *  Driver needs to know address, irq and flag pin.
 */
#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
static struct resource smc91x_resources[] = {
	{
		.name = "smc91x-regs",
		.start = 0x20300300,
		.end = 0x20300300 + 16,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_PF7,
		.end = IRQ_PF7,
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

#if defined(CONFIG_FB_BFIN_7393) || defined(CONFIG_FB_BFIN_7393_MODULE)
static struct platform_device bfin_fb_adv7393_device = {
	.name = "bfin-adv7393",
};
#endif

#if defined(CONFIG_USB_NET2272) || defined(CONFIG_USB_NET2272_MODULE)
static struct resource net2272_bfin_resources[] = {
	{
		.start = 0x20300000,
		.end = 0x20300000 + 0x100,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_PF10,
		.end = IRQ_PF10,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

static struct platform_device net2272_bfin_device = {
	.name = "net2272",
	.id = -1,
	.num_resources = ARRAY_SIZE(net2272_bfin_resources),
	.resource = net2272_bfin_resources,
};
#endif

#if defined(CONFIG_MTD_BFIN_ASYNC) || defined(CONFIG_MTD_BFIN_ASYNC_MODULE)
static struct mtd_partition stamp_partitions[] = {
	{
		.name   = "bootloader(nor)",
		.size   = 0x40000,
		.offset = 0,
	}, {
		.name   = "linux kernel(nor)",
		.size   = 0x180000,
		.offset = MTDPART_OFS_APPEND,
	}, {
		.name   = "file system(nor)",
		.size   = MTDPART_SIZ_FULL,
		.offset = MTDPART_OFS_APPEND,
	}
};

static struct physmap_flash_data stamp_flash_data = {
	.width    = 2,
	.parts    = stamp_partitions,
	.nr_parts = ARRAY_SIZE(stamp_partitions),
};

static struct resource stamp_flash_resource[] = {
	{
		.name  = "cfi_probe",
		.start = 0x20000000,
		.end   = 0x203fffff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 0x7BB07BB0,	/* AMBCTL0 setting when accessing flash */
		.end   = 0x7BB07BB0,	/* AMBCTL1 setting when accessing flash */
		.flags = IORESOURCE_MEM,
	}, {
		.start = GPIO_PF0,
		.flags = IORESOURCE_IRQ,
	}
};

static struct platform_device stamp_flash_device = {
	.name          = "bfin-async-flash",
	.id            = 0,
	.dev = {
		.platform_data = &stamp_flash_data,
	},
	.num_resources = ARRAY_SIZE(stamp_flash_resource),
	.resource      = stamp_flash_resource,
};
#endif

#if defined(CONFIG_MTD_M25P80) || defined(CONFIG_MTD_M25P80_MODULE)
static struct mtd_partition bfin_spi_flash_partitions[] = {
	{
		.name = "bootloader(spi)",
		.size = 0x00040000,
		.offset = 0,
		.mask_flags = MTD_CAP_ROM
	}, {
		.name = "linux kernel(spi)",
		.size = 0x180000,
		.offset = MTDPART_OFS_APPEND,
	}, {
		.name = "file system(spi)",
		.size = MTDPART_SIZ_FULL,
		.offset = MTDPART_OFS_APPEND,
	}
};

static struct flash_platform_data bfin_spi_flash_data = {
	.name = "m25p80",
	.parts = bfin_spi_flash_partitions,
	.nr_parts = ARRAY_SIZE(bfin_spi_flash_partitions),
	.type = "m25p64",
};

/* SPI flash chip (m25p64) */
static struct bfin5xx_spi_chip spi_flash_chip_info = {
	.enable_dma = 0,         /* use dma transfer with this chip*/
	.bits_per_word = 8,
};
#endif

#if defined(CONFIG_BFIN_SPI_ADC) || defined(CONFIG_BFIN_SPI_ADC_MODULE)
/* SPI ADC chip */
static struct bfin5xx_spi_chip spi_adc_chip_info = {
	.enable_dma = 1,         /* use dma transfer with this chip*/
	.bits_per_word = 16,
};
#endif

#if defined(CONFIG_SND_BLACKFIN_AD1836) || defined(CONFIG_SND_BLACKFIN_AD1836_MODULE)
static struct bfin5xx_spi_chip ad1836_spi_chip_info = {
	.enable_dma = 0,
	.bits_per_word = 16,
};
#endif

#if defined(CONFIG_PBX)
static struct bfin5xx_spi_chip spi_si3xxx_chip_info = {
	.ctl_reg	= 0x4, /* send zero */
	.enable_dma	= 0,
	.bits_per_word	= 8,
	.cs_change_per_word = 1,
};
#endif

#if defined(CONFIG_SPI_SPIDEV) || defined(CONFIG_SPI_SPIDEV_MODULE)
static struct bfin5xx_spi_chip spidev_chip_info = {
	.enable_dma = 0,
	.bits_per_word = 8,
};
#endif

static struct spi_board_info bfin_spi_board_info[] __initdata = {
#if defined(CONFIG_MTD_M25P80) || defined(CONFIG_MTD_M25P80_MODULE)
	{
		/* the modalias must be the same as spi device driver name */
		.modalias = "m25p80", /* Name of spi_driver for this device */
		.max_speed_hz = 25000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0, /* Framework bus number */
		.chip_select = 2, /* Framework chip select. On STAMP537 it is SPISSEL2*/
		.platform_data = &bfin_spi_flash_data,
		.controller_data = &spi_flash_chip_info,
		.mode = SPI_MODE_3,
	},
#endif

#if defined(CONFIG_BFIN_SPI_ADC) || defined(CONFIG_BFIN_SPI_ADC_MODULE)
	{
		.modalias = "bfin_spi_adc", /* Name of spi_driver for this device */
		.max_speed_hz = 6250000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0, /* Framework bus number */
		.chip_select = 1, /* Framework chip select. */
		.platform_data = NULL, /* No spi_driver specific config */
		.controller_data = &spi_adc_chip_info,
	},
#endif

#if defined(CONFIG_SND_BLACKFIN_AD1836) || defined(CONFIG_SND_BLACKFIN_AD1836_MODULE)
	{
		.modalias = "ad1836-spi",
		.max_speed_hz = 31250000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = CONFIG_SND_BLACKFIN_SPI_PFBIT,
		.controller_data = &ad1836_spi_chip_info,
	},
#endif

#if defined(CONFIG_PBX)
	{
		.modalias = "fxs-spi",
		.max_speed_hz = 12500000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 8 - CONFIG_J11_JUMPER,
		.controller_data = &spi_si3xxx_chip_info,
		.mode = SPI_MODE_3,
	},
	{
		.modalias = "fxo-spi",
		.max_speed_hz = 12500000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 8 - CONFIG_J19_JUMPER,
		.controller_data = &spi_si3xxx_chip_info,
		.mode = SPI_MODE_3,
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

#if defined(CONFIG_SPI_BFIN) || defined(CONFIG_SPI_BFIN_MODULE)
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
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = IRQ_SPI,
		.end   = IRQ_SPI,
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
#endif  /* spi master and devices */

#if defined(CONFIG_FB_BF537_LQ035) || defined(CONFIG_FB_BF537_LQ035_MODULE)
static struct platform_device bfin_fb_device = {
	.name = "bf537-fb",
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

#if defined(CONFIG_BFIN_SIR) || defined(CONFIG_BFIN_SIR_MODULE)
#ifdef CONFIG_BFIN_SIR0
static struct resource bfin_sir0_resources[] = {
	{
		.start = 0xFFC00400,
		.end = 0xFFC004FF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART0_RX,
		.end = IRQ_UART0_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART0_RX,
		.end = CH_UART0_RX+1,
		.flags = IORESOURCE_DMA,
	},
};

static struct platform_device bfin_sir0_device = {
	.name = "bfin_sir",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_sir0_resources),
	.resource = bfin_sir0_resources,
};
#endif
#endif

#if defined(CONFIG_SERIAL_BFIN_SPORT) || defined(CONFIG_SERIAL_BFIN_SPORT_MODULE)
static struct platform_device bfin_sport0_uart_device = {
	.name = "bfin-sport-uart",
	.id = 0,
};

static struct platform_device bfin_sport1_uart_device = {
	.name = "bfin-sport-uart",
	.id = 1,
};
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
#include <linux/input.h>
#include <linux/gpio_keys.h>

static struct gpio_keys_button bfin_gpio_keys_table[] = {
	{BTN_0, GPIO_PF5, 0, "gpio-keys: BTN0"},
	{BTN_1, GPIO_PF6, 0, "gpio-keys: BTN1"},
	{BTN_2, GPIO_PF8, 0, "gpio-keys: BTN2"},
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

static struct resource bfin_gpios_resources = {
	.start = 0,
	.end   = MAX_BLACKFIN_GPIOS - 1,
	.flags = IORESOURCE_IRQ,
};

static struct platform_device bfin_gpios_device = {
	.name = "simple-gpio",
	.id = -1,
	.num_resources = 1,
	.resource = &bfin_gpios_resources,
};

#if defined(CONFIG_I2C_GPIO) || defined(CONFIG_I2C_GPIO_MODULE)
#include <linux/i2c-gpio.h>

static struct i2c_gpio_platform_data i2c_gpio_data = {
	.sda_pin		= 2,
	.scl_pin		= 3,
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

static struct i2c_board_info __initdata bfin_i2c_board_info[] = {
#if defined(CONFIG_JOYSTICK_AD7142) || defined(CONFIG_JOYSTICK_AD7142_MODULE)
	{
		I2C_BOARD_INFO("ad7142_joystick", 0x2C),
		.irq = 39,
	},
#endif
#if defined(CONFIG_BFIN_TWI_LCD) || defined(CONFIG_TWI_LCD_MODULE)
	{
		I2C_BOARD_INFO("pcf8574_lcd", 0x22),
	},
#endif
#if defined(CONFIG_TWI_KEYPAD) || defined(CONFIG_TWI_KEYPAD_MODULE)
	{
		I2C_BOARD_INFO("pcf8574_keypad", 0x27),
		.irq = 39,
	},
#endif
};

static const unsigned int cclk_vlev_datasheet[] =
{
	VRPAIR(VLEV_085, 250000000),
	VRPAIR(VLEV_090, 376000000),
	VRPAIR(VLEV_095, 426000000),
	VRPAIR(VLEV_100, 426000000),
	VRPAIR(VLEV_105, 476000000),
	VRPAIR(VLEV_110, 476000000),
	VRPAIR(VLEV_115, 476000000),
	VRPAIR(VLEV_120, 600000000),
	VRPAIR(VLEV_125, 600000000),
	VRPAIR(VLEV_130, 600000000),
};

static struct bfin_dpmc_platform_data bfin_dmpc_vreg_data = {
	.tuple_tab = cclk_vlev_datasheet,
	.tabsize = ARRAY_SIZE(cclk_vlev_datasheet),
	.vr_settling_time = 25 /* us */,
};

static struct platform_device bfin_dpmc = {
	.name = "bfin dpmc",
	.dev = {
		.platform_data = &bfin_dmpc_vreg_data,
	},
};

static struct platform_device *stamp_devices[] __initdata = {

	&bfin_dpmc,

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
	&rtc_device,
#endif

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
	&smc91x_device,
#endif

#if defined(CONFIG_FB_BFIN_7393) || defined(CONFIG_FB_BFIN_7393_MODULE)
	&bfin_fb_adv7393_device,
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

#if defined(CONFIG_BFIN_SIR) || defined(CONFIG_BFIN_SIR_MODULE)
#ifdef CONFIG_BFIN_SIR0
	&bfin_sir0_device,
#endif
#endif

#if defined(CONFIG_SERIAL_BFIN_SPORT) || defined(CONFIG_SERIAL_BFIN_SPORT_MODULE)
	&bfin_sport0_uart_device,
	&bfin_sport1_uart_device,
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
	&bfin_device_gpiokeys,
#endif

#if defined(CONFIG_I2C_GPIO) || defined(CONFIG_I2C_GPIO_MODULE)
	&i2c_gpio_device,
#endif

	&bfin_gpios_device,

#if defined(CONFIG_MTD_BFIN_ASYNC) || defined(CONFIG_MTD_BFIN_ASYNC_MODULE)
	&stamp_flash_device,
#endif
};

static int __init stamp_init(void)
{
	int ret;

	printk(KERN_INFO "%s(): registering device resources\n", __func__);

	i2c_register_board_info(0, bfin_i2c_board_info,
				ARRAY_SIZE(bfin_i2c_board_info));

	ret = platform_add_devices(stamp_devices, ARRAY_SIZE(stamp_devices));
	if (ret < 0)
		return ret;

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
	/* setup BF533_STAMP CPLD to route AMS3 to Ethernet MAC */
	bfin_write_FIO_DIR(bfin_read_FIO_DIR() | PF0);
	bfin_write_FIO_FLAG_S(PF0);
	SSYNC();
#endif

	spi_register_board_info(bfin_spi_board_info, ARRAY_SIZE(bfin_spi_board_info));
	return 0;
}

arch_initcall(stamp_init);

void native_machine_restart(char *cmd)
{
	/* workaround pull up on cpld / flash pin not being strong enough */
	bfin_write_FIO_INEN(~PF0);
	bfin_write_FIO_DIR(PF0);
	bfin_write_FIO_FLAG_C(PF0);
}
