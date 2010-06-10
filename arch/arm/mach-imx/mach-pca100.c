/*
 * Copyright 2007 Robert Schwebel <r.schwebel@pengutronix.de>, Pengutronix
 * Copyright (C) 2009 Sascha Hauer (kernel@pengutronix.de)
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

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/i2c/at24.h>
#include <linux/dma-mapping.h>
#include <linux/spi/spi.h>
#include <linux/spi/eeprom.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/fsl_devices.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>
#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/iomux-mx27.h>
#include <mach/i2c.h>
#include <asm/mach/time.h>
#if defined(CONFIG_SPI_IMX) || defined(CONFIG_SPI_IMX_MODULE)
#include <mach/spi.h>
#endif
#include <mach/imx-uart.h>
#include <mach/audmux.h>
#include <mach/ssi.h>
#include <mach/mxc_nand.h>
#include <mach/irqs.h>
#include <mach/mmc.h>
#include <mach/mxc_ehci.h>
#include <mach/ulpi.h>

#include "devices.h"

#define OTG_PHY_CS_GPIO (GPIO_PORTB + 23)
#define USBH2_PHY_CS_GPIO (GPIO_PORTB + 24)

static int pca100_pins[] = {
	/* UART1 */
	PE12_PF_UART1_TXD,
	PE13_PF_UART1_RXD,
	PE14_PF_UART1_CTS,
	PE15_PF_UART1_RTS,
	/* SDHC */
	PB4_PF_SD2_D0,
	PB5_PF_SD2_D1,
	PB6_PF_SD2_D2,
	PB7_PF_SD2_D3,
	PB8_PF_SD2_CMD,
	PB9_PF_SD2_CLK,
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
	/* SSI1 */
	PC20_PF_SSI1_FS,
	PC21_PF_SSI1_RXD,
	PC22_PF_SSI1_TXD,
	PC23_PF_SSI1_CLK,
	/* onboard I2C */
	PC5_PF_I2C2_SDA,
	PC6_PF_I2C2_SCL,
	/* external I2C */
	PD17_PF_I2C_DATA,
	PD18_PF_I2C_CLK,
	/* SPI1 */
	PD25_PF_CSPI1_RDY,
	PD29_PF_CSPI1_SCLK,
	PD30_PF_CSPI1_MISO,
	PD31_PF_CSPI1_MOSI,
	/* OTG */
	OTG_PHY_CS_GPIO | GPIO_GPIO | GPIO_OUT,
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
	USBH2_PHY_CS_GPIO | GPIO_GPIO | GPIO_OUT,
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

static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct mxc_nand_platform_data pca100_nand_board_info = {
	.width = 1,
	.hw_ecc = 1,
};

static struct platform_device *platform_devices[] __initdata = {
	&mxc_w1_master_device,
	&mxc_fec_device,
	&mxc_wdt,
};

static struct imxi2c_platform_data pca100_i2c_1_data = {
	.bitrate = 100000,
};

static struct at24_platform_data board_eeprom = {
	.byte_len = 4096,
	.page_size = 32,
	.flags = AT24_FLAG_ADDR16,
};

static struct i2c_board_info pca100_i2c_devices[] = {
	{
		I2C_BOARD_INFO("at24", 0x52), /* E0=0, E1=1, E2=0 */
		.platform_data = &board_eeprom,
	}, {
		I2C_BOARD_INFO("rtc-pcf8563", 0x51),
		.type = "pcf8563"
	}, {
		I2C_BOARD_INFO("lm75", 0x4a),
		.type = "lm75"
	}
};

#if defined(CONFIG_SPI_IMX) || defined(CONFIG_SPI_IMX_MODULE)
static struct spi_eeprom at25320 = {
	.name		= "at25320an",
	.byte_len	= 4096,
	.page_size	= 32,
	.flags		= EE_ADDR2,
};

static struct spi_board_info pca100_spi_board_info[] __initdata = {
	{
		.modalias = "at25",
		.max_speed_hz = 30000,
		.bus_num = 0,
		.chip_select = 1,
		.platform_data = &at25320,
	},
};

static int pca100_spi_cs[] = {GPIO_PORTD + 28, GPIO_PORTD + 27};

static struct spi_imx_master pca100_spi_0_data = {
	.chipselect	= pca100_spi_cs,
	.num_chipselect = ARRAY_SIZE(pca100_spi_cs),
};
#endif

static void pca100_ac97_warm_reset(struct snd_ac97 *ac97)
{
	mxc_gpio_mode(GPIO_PORTC | 20 | GPIO_GPIO | GPIO_OUT);
	gpio_set_value(GPIO_PORTC + 20, 1);
	udelay(2);
	gpio_set_value(GPIO_PORTC + 20, 0);
	mxc_gpio_mode(PC20_PF_SSI1_FS);
	msleep(2);
}

static void pca100_ac97_cold_reset(struct snd_ac97 *ac97)
{
	mxc_gpio_mode(GPIO_PORTC | 20 | GPIO_GPIO | GPIO_OUT);  /* FS */
	gpio_set_value(GPIO_PORTC + 20, 0);
	mxc_gpio_mode(GPIO_PORTC | 22 | GPIO_GPIO | GPIO_OUT);  /* TX */
	gpio_set_value(GPIO_PORTC + 22, 0);
	mxc_gpio_mode(GPIO_PORTC | 28 | GPIO_GPIO | GPIO_OUT);  /* reset */
	gpio_set_value(GPIO_PORTC + 28, 0);
	udelay(10);
	gpio_set_value(GPIO_PORTC + 28, 1);
	mxc_gpio_mode(PC20_PF_SSI1_FS);
	mxc_gpio_mode(PC22_PF_SSI1_TXD);
	msleep(2);
}

static struct imx_ssi_platform_data pca100_ssi_pdata = {
	.ac97_reset		= pca100_ac97_cold_reset,
	.ac97_warm_reset	= pca100_ac97_warm_reset,
	.flags			= IMX_SSI_USE_AC97,
};

static int pca100_sdhc2_init(struct device *dev, irq_handler_t detect_irq,
		void *data)
{
	int ret;

	ret = request_irq(IRQ_GPIOC(29), detect_irq,
			  IRQF_DISABLED | IRQF_TRIGGER_FALLING,
			  "imx-mmc-detect", data);
	if (ret)
		printk(KERN_ERR
			"pca100: Failed to reuest irq for sd/mmc detection\n");

	return ret;
}

static void pca100_sdhc2_exit(struct device *dev, void *data)
{
	free_irq(IRQ_GPIOC(29), data);
}

static struct imxmmc_platform_data sdhc_pdata = {
	.init = pca100_sdhc2_init,
	.exit = pca100_sdhc2_exit,
};

static int otg_phy_init(struct platform_device *pdev)
{
	gpio_set_value(OTG_PHY_CS_GPIO, 0);
	return 0;
}

static struct mxc_usbh_platform_data otg_pdata = {
	.init	= otg_phy_init,
	.portsc	= MXC_EHCI_MODE_ULPI,
	.flags	= MXC_EHCI_INTERFACE_DIFF_UNI,
};

static int usbh2_phy_init(struct platform_device *pdev)
{
	gpio_set_value(USBH2_PHY_CS_GPIO, 0);
	return 0;
}

static struct mxc_usbh_platform_data usbh2_pdata = {
	.init	= usbh2_phy_init,
	.portsc	= MXC_EHCI_MODE_ULPI,
	.flags	= MXC_EHCI_INTERFACE_DIFF_UNI,
};

static struct fsl_usb2_platform_data otg_device_pdata = {
	.operating_mode = FSL_USB2_DR_DEVICE,
	.phy_mode       = FSL_USB2_PHY_ULPI,
};

static int otg_mode_host;

static int __init pca100_otg_mode(char *options)
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
__setup("otg_mode=", pca100_otg_mode);

static void __init pca100_init(void)
{
	int ret;

	/* SSI unit */
	mxc_audmux_v1_configure_port(MX27_AUDMUX_HPCR1_SSI0,
				  MXC_AUDMUX_V1_PCR_SYN | /* 4wire mode */
				  MXC_AUDMUX_V1_PCR_TFCSEL(3) |
				  MXC_AUDMUX_V1_PCR_TCLKDIR | /* clock is output */
				  MXC_AUDMUX_V1_PCR_RXDSEL(3));
	mxc_audmux_v1_configure_port(3,
				  MXC_AUDMUX_V1_PCR_SYN | /* 4wire mode */
				  MXC_AUDMUX_V1_PCR_TFCSEL(0) |
				  MXC_AUDMUX_V1_PCR_TFSDIR |
				  MXC_AUDMUX_V1_PCR_RXDSEL(0));

	ret = mxc_gpio_setup_multiple_pins(pca100_pins,
			ARRAY_SIZE(pca100_pins), "PCA100");
	if (ret)
		printk(KERN_ERR "pca100: Failed to setup pins (%d)\n", ret);

	mxc_register_device(&imx_ssi_device0, &pca100_ssi_pdata);

	mxc_register_device(&mxc_uart_device0, &uart_pdata);

	mxc_gpio_mode(GPIO_PORTC | 29 | GPIO_GPIO | GPIO_IN);
	mxc_register_device(&mxc_sdhc_device1, &sdhc_pdata);

	mxc_register_device(&imx27_nand_device, &pca100_nand_board_info);

	/* only the i2c master 1 is used on this CPU card */
	i2c_register_board_info(1, pca100_i2c_devices,
				ARRAY_SIZE(pca100_i2c_devices));

	mxc_register_device(&mxc_i2c_device1, &pca100_i2c_1_data);

	mxc_gpio_mode(GPIO_PORTD | 28 | GPIO_GPIO | GPIO_OUT);
	mxc_gpio_mode(GPIO_PORTD | 27 | GPIO_GPIO | GPIO_OUT);

	/* GPIO0_IRQ */
	mxc_gpio_mode(GPIO_PORTC | 31 | GPIO_GPIO | GPIO_IN);
	/* GPIO1_IRQ */
	mxc_gpio_mode(GPIO_PORTC | 25 | GPIO_GPIO | GPIO_IN);
	/* GPIO2_IRQ */
	mxc_gpio_mode(GPIO_PORTE | 5 | GPIO_GPIO | GPIO_IN);

#if defined(CONFIG_SPI_IMX) || defined(CONFIG_SPI_IMX_MODULE)
	spi_register_board_info(pca100_spi_board_info,
				ARRAY_SIZE(pca100_spi_board_info));
	mxc_register_device(&mxc_spi_device0, &pca100_spi_0_data);
#endif

	gpio_request(OTG_PHY_CS_GPIO, "usb-otg-cs");
	gpio_direction_output(OTG_PHY_CS_GPIO, 1);
	gpio_request(USBH2_PHY_CS_GPIO, "usb-host2-cs");
	gpio_direction_output(USBH2_PHY_CS_GPIO, 1);

#if defined(CONFIG_USB_ULPI)
	if (otg_mode_host) {
		otg_pdata.otg = otg_ulpi_create(&mxc_ulpi_access_ops,
				USB_OTG_DRV_VBUS | USB_OTG_DRV_VBUS_EXT);

		mxc_register_device(&mxc_otg_host, &otg_pdata);
	}

	usbh2_pdata.otg = otg_ulpi_create(&mxc_ulpi_access_ops,
				USB_OTG_DRV_VBUS | USB_OTG_DRV_VBUS_EXT);

	mxc_register_device(&mxc_usbh2, &usbh2_pdata);
#endif
	if (!otg_mode_host) {
		gpio_set_value(OTG_PHY_CS_GPIO, 0);
		mxc_register_device(&mxc_otg_udc_device, &otg_device_pdata);
	}

	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));
}

static void __init pca100_timer_init(void)
{
	mx27_clocks_init(26000000);
}

static struct sys_timer pca100_timer = {
	.init = pca100_timer_init,
};

MACHINE_START(PCA100, "phyCARD-i.MX27")
	.phys_io        = MX27_AIPI_BASE_ADDR,
	.io_pg_offst    = ((MX27_AIPI_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params    = MX27_PHYS_OFFSET + 0x100,
	.map_io         = mx27_map_io,
	.init_irq       = mx27_init_irq,
	.init_machine   = pca100_init,
	.timer          = &pca100_timer,
MACHINE_END

