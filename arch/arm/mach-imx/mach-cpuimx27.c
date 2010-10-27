/*
 * Copyright (C) 2009 Eric Benard - eric@eukrea.com
 *
 * Based on pcm038.c which is :
 * Copyright 2007 Robert Schwebel <r.schwebel@pengutronix.de>, Pengutronix
 * Copyright (C) 2008 Juergen Beisert (kernel@pengutronix.de)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/mtd/plat-ram.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/fsl_devices.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>

#include <mach/eukrea-baseboards.h>
#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/iomux-mx27.h>
#include <mach/mxc_nand.h>
#include <mach/mxc_ehci.h>
#include <mach/ulpi.h>

#include "devices-imx27.h"
#include "devices.h"

static int eukrea_cpuimx27_pins[] = {
	/* UART1 */
	PE12_PF_UART1_TXD,
	PE13_PF_UART1_RXD,
	PE14_PF_UART1_CTS,
	PE15_PF_UART1_RTS,
	/* UART4 */
#if defined(MACH_EUKREA_CPUIMX27_USEUART4)
	PB26_AF_UART4_RTS,
	PB28_AF_UART4_TXD,
	PB29_AF_UART4_CTS,
	PB31_AF_UART4_RXD,
#endif
	/* FEC */
	PD0_AIN_FEC_TXD0,
	PD1_AIN_FEC_TXD1,
	PD2_AIN_FEC_TXD2,
	PD3_AIN_FEC_TXD3,
	PD4_AOUT_FEC_RX_ER,
	PD5_AOUT_FEC_RXD1,
	PD6_AOUT_FEC_RXD2,
	PD7_AOUT_FEC_RXD3,
	PD8_AF_FEC_MDIO,
	PD9_AIN_FEC_MDC,
	PD10_AOUT_FEC_CRS,
	PD11_AOUT_FEC_TX_CLK,
	PD12_AOUT_FEC_RXD0,
	PD13_AOUT_FEC_RX_DV,
	PD14_AOUT_FEC_RX_CLK,
	PD15_AOUT_FEC_COL,
	PD16_AIN_FEC_TX_ER,
	PF23_AIN_FEC_TX_EN,
	/* I2C1 */
	PD17_PF_I2C_DATA,
	PD18_PF_I2C_CLK,
	/* SDHC2 */
#if defined(CONFIG_MACH_EUKREA_CPUIMX27_USESDHC2)
	PB4_PF_SD2_D0,
	PB5_PF_SD2_D1,
	PB6_PF_SD2_D2,
	PB7_PF_SD2_D3,
	PB8_PF_SD2_CMD,
	PB9_PF_SD2_CLK,
#endif
#if defined(CONFIG_SERIAL_8250) || defined(CONFIG_SERIAL_8250_MODULE)
	/* Quad UART's IRQ */
	GPIO_PORTB | 22 | GPIO_GPIO | GPIO_IN,
	GPIO_PORTB | 23 | GPIO_GPIO | GPIO_IN,
	GPIO_PORTB | 27 | GPIO_GPIO | GPIO_IN,
	GPIO_PORTB | 30 | GPIO_GPIO | GPIO_IN,
#endif
	/* OTG */
	PC7_PF_USBOTG_DATA5,
	PC8_PF_USBOTG_DATA6,
	PC9_PF_USBOTG_DATA0,
	PC10_PF_USBOTG_DATA2,
	PC11_PF_USBOTG_DATA1,
	PC12_PF_USBOTG_DATA4,
	PC13_PF_USBOTG_DATA3,
	PE0_PF_USBOTG_NXT,
	PE1_PF_USBOTG_STP,
	PE2_PF_USBOTG_DIR,
	PE24_PF_USBOTG_CLK,
	PE25_PF_USBOTG_DATA7,
	/* USBH2 */
	PA0_PF_USBH2_CLK,
	PA1_PF_USBH2_DIR,
	PA2_PF_USBH2_DATA7,
	PA3_PF_USBH2_NXT,
	PA4_PF_USBH2_STP,
	PD19_AF_USBH2_DATA4,
	PD20_AF_USBH2_DATA3,
	PD21_AF_USBH2_DATA6,
	PD22_AF_USBH2_DATA0,
	PD23_AF_USBH2_DATA2,
	PD24_AF_USBH2_DATA1,
	PD26_AF_USBH2_DATA5,
};

static struct physmap_flash_data eukrea_cpuimx27_flash_data = {
	.width = 2,
};

static struct resource eukrea_cpuimx27_flash_resource = {
	.start = 0xc0000000,
	.end   = 0xc3ffffff,
	.flags = IORESOURCE_MEM,
};

static struct platform_device eukrea_cpuimx27_nor_mtd_device = {
	.name = "physmap-flash",
	.id = 0,
	.dev = {
		.platform_data = &eukrea_cpuimx27_flash_data,
	},
	.num_resources = 1,
	.resource = &eukrea_cpuimx27_flash_resource,
};

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static const struct mxc_nand_platform_data
cpuimx27_nand_board_info __initconst = {
	.width = 1,
	.hw_ecc = 1,
};

static struct platform_device *platform_devices[] __initdata = {
	&eukrea_cpuimx27_nor_mtd_device,
	&mxc_fec_device,
	&mxc_wdt,
	&mxc_w1_master_device,
};

static const struct imxi2c_platform_data cpuimx27_i2c1_data __initconst = {
	.bitrate = 100000,
};

static struct i2c_board_info eukrea_cpuimx27_i2c_devices[] = {
	{
		I2C_BOARD_INFO("pcf8563", 0x51),
	},
};

#if defined(CONFIG_SERIAL_8250) || defined(CONFIG_SERIAL_8250_MODULE)
static struct plat_serial8250_port serial_platform_data[] = {
	{
		.mapbase = (unsigned long)(MX27_CS3_BASE_ADDR + 0x200000),
		.irq = IRQ_GPIOB(23),
		.uartclk = 14745600,
		.regshift = 1,
		.iotype = UPIO_MEM,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_IOREMAP,
	}, {
		.mapbase = (unsigned long)(MX27_CS3_BASE_ADDR + 0x400000),
		.irq = IRQ_GPIOB(22),
		.uartclk = 14745600,
		.regshift = 1,
		.iotype = UPIO_MEM,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_IOREMAP,
	}, {
		.mapbase = (unsigned long)(MX27_CS3_BASE_ADDR + 0x800000),
		.irq = IRQ_GPIOB(27),
		.uartclk = 14745600,
		.regshift = 1,
		.iotype = UPIO_MEM,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_IOREMAP,
	}, {
		.mapbase = (unsigned long)(MX27_CS3_BASE_ADDR + 0x1000000),
		.irq = IRQ_GPIOB(30),
		.uartclk = 14745600,
		.regshift = 1,
		.iotype = UPIO_MEM,
		.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_IOREMAP,
	}, {
	}
};

static struct platform_device serial_device = {
	.name = "serial8250",
	.id = 0,
	.dev = {
		.platform_data = serial_platform_data,
	},
};
#endif

#if defined(CONFIG_USB_ULPI)
static struct mxc_usbh_platform_data otg_pdata = {
	.portsc	= MXC_EHCI_MODE_ULPI,
	.flags	= MXC_EHCI_INTERFACE_DIFF_UNI,
};

static struct mxc_usbh_platform_data usbh2_pdata = {
	.portsc	= MXC_EHCI_MODE_ULPI,
	.flags	= MXC_EHCI_INTERFACE_DIFF_UNI,
};
#endif

static struct fsl_usb2_platform_data otg_device_pdata = {
	.operating_mode = FSL_USB2_DR_DEVICE,
	.phy_mode       = FSL_USB2_PHY_ULPI,
};

static int otg_mode_host;

static int __init eukrea_cpuimx27_otg_mode(char *options)
{
	if (!strcmp(options, "host"))
		otg_mode_host = 1;
	else if (!strcmp(options, "device"))
		otg_mode_host = 0;
	else
		pr_info("otg_mode neither \"host\" nor \"device\". "
			"Defaulting to device\n");
	return 0;
}
__setup("otg_mode=", eukrea_cpuimx27_otg_mode);

static void __init eukrea_cpuimx27_init(void)
{
	mxc_gpio_setup_multiple_pins(eukrea_cpuimx27_pins,
		ARRAY_SIZE(eukrea_cpuimx27_pins), "CPUIMX27");

	imx27_add_imx_uart0(&uart_pdata);

	imx27_add_mxc_nand(&cpuimx27_nand_board_info);

	i2c_register_board_info(0, eukrea_cpuimx27_i2c_devices,
				ARRAY_SIZE(eukrea_cpuimx27_i2c_devices));

	imx27_add_i2c_imx0(&cpuimx27_i2c1_data);

	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));

#if defined(CONFIG_MACH_EUKREA_CPUIMX27_USESDHC2)
	/* SDHC2 can be used for Wifi */
	mxc_register_device(&mxc_sdhc_device1, NULL);
#endif
#if defined(MACH_EUKREA_CPUIMX27_USEUART4)
	/* in which case UART4 is also used for Bluetooth */
	imx27_add_imx_uart3(&uart_pdata);
#endif

#if defined(CONFIG_SERIAL_8250) || defined(CONFIG_SERIAL_8250_MODULE)
	platform_device_register(&serial_device);
#endif

#if defined(CONFIG_USB_ULPI)
	if (otg_mode_host) {
		otg_pdata.otg = otg_ulpi_create(&mxc_ulpi_access_ops,
				ULPI_OTG_DRVVBUS | ULPI_OTG_DRVVBUS_EXT);

		mxc_register_device(&mxc_otg_host, &otg_pdata);
	}

	usbh2_pdata.otg = otg_ulpi_create(&mxc_ulpi_access_ops,
				ULPI_OTG_DRVVBUS | ULPI_OTG_DRVVBUS_EXT);

	mxc_register_device(&mxc_usbh2, &usbh2_pdata);
#endif
	if (!otg_mode_host)
		mxc_register_device(&mxc_otg_udc_device, &otg_device_pdata);

#ifdef CONFIG_MACH_EUKREA_MBIMX27_BASEBOARD
	eukrea_mbimx27_baseboard_init();
#endif
}

static void __init eukrea_cpuimx27_timer_init(void)
{
	mx27_clocks_init(26000000);
}

static struct sys_timer eukrea_cpuimx27_timer = {
	.init = eukrea_cpuimx27_timer_init,
};

MACHINE_START(CPUIMX27, "EUKREA CPUIMX27")
	.phys_io        = MX27_AIPI_BASE_ADDR,
	.io_pg_offst    = ((MX27_AIPI_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params    = MX27_PHYS_OFFSET + 0x100,
	.map_io         = mx27_map_io,
	.init_irq       = mx27_init_irq,
	.init_machine   = eukrea_cpuimx27_init,
	.timer          = &eukrea_cpuimx27_timer,
MACHINE_END
