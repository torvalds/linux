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
#include <linux/platform_data/at24.h>
#include <linux/dma-mapping.h>
#include <linux/spi/spi.h>
#include <linux/spi/eeprom.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>
#include <asm/mach/time.h>

#include "common.h"
#include "devices-imx27.h"
#include "ehci.h"
#include "hardware.h"
#include "iomux-mx27.h"
#include "ulpi.h"

#define OTG_PHY_CS_GPIO (GPIO_PORTB + 23)
#define USBH2_PHY_CS_GPIO (GPIO_PORTB + 24)
#define SPI1_SS0 (GPIO_PORTD + 28)
#define SPI1_SS1 (GPIO_PORTD + 27)
#define SD2_CD (GPIO_PORTC + 29)

static const int pca100_pins[] __initconst = {
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
	SD2_CD | GPIO_GPIO | GPIO_IN,
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
	/* display */
	PA5_PF_LSCLK,
	PA6_PF_LD0,
	PA7_PF_LD1,
	PA8_PF_LD2,
	PA9_PF_LD3,
	PA10_PF_LD4,
	PA11_PF_LD5,
	PA12_PF_LD6,
	PA13_PF_LD7,
	PA14_PF_LD8,
	PA15_PF_LD9,
	PA16_PF_LD10,
	PA17_PF_LD11,
	PA18_PF_LD12,
	PA19_PF_LD13,
	PA20_PF_LD14,
	PA21_PF_LD15,
	PA22_PF_LD16,
	PA23_PF_LD17,
	PA26_PF_PS,
	PA28_PF_HSYNC,
	PA29_PF_VSYNC,
	PA31_PF_OE_ACD,
	/* free GPIO */
	GPIO_PORTC | 31 | GPIO_GPIO | GPIO_IN, /* GPIO0_IRQ */
	GPIO_PORTC | 25 | GPIO_GPIO | GPIO_IN, /* GPIO1_IRQ */
	GPIO_PORTE | 5 | GPIO_GPIO | GPIO_IN, /* GPIO2_IRQ */
};

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static const struct mxc_nand_platform_data
pca100_nand_board_info __initconst = {
	.width = 1,
	.hw_ecc = 1,
};

static const struct imxi2c_platform_data pca100_i2c1_data __initconst = {
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
		I2C_BOARD_INFO("pcf8563", 0x51),
	}, {
		I2C_BOARD_INFO("lm75", 0x4a),
	}
};

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

static int pca100_spi_cs[] = {SPI1_SS0, SPI1_SS1};

static const struct spi_imx_master pca100_spi0_data __initconst = {
	.chipselect	= pca100_spi_cs,
	.num_chipselect = ARRAY_SIZE(pca100_spi_cs),
};

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

static const struct imx_ssi_platform_data pca100_ssi_pdata __initconst = {
	.ac97_reset		= pca100_ac97_cold_reset,
	.ac97_warm_reset	= pca100_ac97_warm_reset,
	.flags			= IMX_SSI_USE_AC97,
};

static int pca100_sdhc2_init(struct device *dev, irq_handler_t detect_irq,
		void *data)
{
	int ret;

	ret = request_irq(gpio_to_irq(IMX_GPIO_NR(3, 29)), detect_irq,
			  IRQF_TRIGGER_FALLING, "imx-mmc-detect", data);
	if (ret)
		printk(KERN_ERR
			"pca100: Failed to request irq for sd/mmc detection\n");

	return ret;
}

static void pca100_sdhc2_exit(struct device *dev, void *data)
{
	free_irq(gpio_to_irq(IMX_GPIO_NR(3, 29)), data);
}

static const struct imxmmc_platform_data sdhc_pdata __initconst = {
	.init = pca100_sdhc2_init,
	.exit = pca100_sdhc2_exit,
};

static int otg_phy_init(struct platform_device *pdev)
{
	gpio_set_value(OTG_PHY_CS_GPIO, 0);

	mdelay(10);

	return mx27_initialize_usb_hw(pdev->id, MXC_EHCI_INTERFACE_DIFF_UNI);
}

static struct mxc_usbh_platform_data otg_pdata __initdata = {
	.init	= otg_phy_init,
	.portsc	= MXC_EHCI_MODE_ULPI,
};

static int usbh2_phy_init(struct platform_device *pdev)
{
	gpio_set_value(USBH2_PHY_CS_GPIO, 0);

	mdelay(10);

	return mx27_initialize_usb_hw(pdev->id, MXC_EHCI_INTERFACE_DIFF_UNI);
}

static struct mxc_usbh_platform_data usbh2_pdata __initdata = {
	.init	= usbh2_phy_init,
	.portsc	= MXC_EHCI_MODE_ULPI,
};

static const struct fsl_usb2_platform_data otg_device_pdata __initconst = {
	.operating_mode = FSL_USB2_DR_DEVICE,
	.phy_mode       = FSL_USB2_PHY_ULPI,
};

static bool otg_mode_host __initdata;

static int __init pca100_otg_mode(char *options)
{
	if (!strcmp(options, "host"))
		otg_mode_host = true;
	else if (!strcmp(options, "device"))
		otg_mode_host = false;
	else
		pr_info("otg_mode neither \"host\" nor \"device\". "
			"Defaulting to device\n");
	return 1;
}
__setup("otg_mode=", pca100_otg_mode);

/* framebuffer info */
static struct imx_fb_videomode pca100_fb_modes[] = {
	{
		.mode = {
			.name		= "EMERGING-ETV570G0DHU",
			.refresh	= 60,
			.xres		= 640,
			.yres		= 480,
			.pixclock	= 39722, /* in ps (25.175 MHz) */
			.hsync_len	= 30,
			.left_margin	= 114,
			.right_margin	= 16,
			.vsync_len	= 3,
			.upper_margin	= 32,
			.lower_margin	= 0,
		},
		/*
		 * TFT
		 * Pixel pol active high
		 * HSYNC active low
		 * VSYNC active low
		 * use HSYNC for ACD count
		 * line clock disable while idle
		 * always enable line clock even if no data
		 */
		.pcr = 0xf0c08080,
		.bpp = 16,
	},
};

static const struct imx_fb_platform_data pca100_fb_data __initconst = {
	.mode = pca100_fb_modes,
	.num_modes = ARRAY_SIZE(pca100_fb_modes),

	.pwmr		= 0x00A903FF,
	.lscr1		= 0x00120300,
	.dmacr		= 0x00020010,
};

static void __init pca100_init(void)
{
	int ret;

	imx27_soc_init();

	ret = mxc_gpio_setup_multiple_pins(pca100_pins,
			ARRAY_SIZE(pca100_pins), "PCA100");
	if (ret)
		printk(KERN_ERR "pca100: Failed to setup pins (%d)\n", ret);

	imx27_add_imx_ssi(0, &pca100_ssi_pdata);

	imx27_add_imx_uart0(&uart_pdata);

	imx27_add_mxc_mmc(1, &sdhc_pdata);

	imx27_add_mxc_nand(&pca100_nand_board_info);

	/* only the i2c master 1 is used on this CPU card */
	i2c_register_board_info(1, pca100_i2c_devices,
				ARRAY_SIZE(pca100_i2c_devices));

	imx27_add_imx_i2c(1, &pca100_i2c1_data);

	mxc_gpio_mode(GPIO_PORTD | 28 | GPIO_GPIO | GPIO_IN);
	mxc_gpio_mode(GPIO_PORTD | 27 | GPIO_GPIO | GPIO_IN);
	spi_register_board_info(pca100_spi_board_info,
				ARRAY_SIZE(pca100_spi_board_info));
	imx27_add_spi_imx0(&pca100_spi0_data);

	gpio_request(OTG_PHY_CS_GPIO, "usb-otg-cs");
	gpio_direction_output(OTG_PHY_CS_GPIO, 1);
	gpio_request(USBH2_PHY_CS_GPIO, "usb-host2-cs");
	gpio_direction_output(USBH2_PHY_CS_GPIO, 1);

	if (otg_mode_host) {
		otg_pdata.otg = imx_otg_ulpi_create(ULPI_OTG_DRVVBUS |
				ULPI_OTG_DRVVBUS_EXT);

		if (otg_pdata.otg)
			imx27_add_mxc_ehci_otg(&otg_pdata);
	} else {
		gpio_set_value(OTG_PHY_CS_GPIO, 0);
		imx27_add_fsl_usb2_udc(&otg_device_pdata);
	}

	usbh2_pdata.otg = imx_otg_ulpi_create(
			ULPI_OTG_DRVVBUS | ULPI_OTG_DRVVBUS_EXT);

	if (usbh2_pdata.otg)
		imx27_add_mxc_ehci_hs(2, &usbh2_pdata);

	imx27_add_imx_fb(&pca100_fb_data);

	imx27_add_fec(NULL);
	imx27_add_imx2_wdt();
	imx27_add_mxc_w1();
}

static void __init pca100_timer_init(void)
{
	mx27_clocks_init(26000000);
}

MACHINE_START(PCA100, "phyCARD-i.MX27")
	.atag_offset = 0x100,
	.map_io = mx27_map_io,
	.init_early = imx27_init_early,
	.init_irq = mx27_init_irq,
	.init_machine = pca100_init,
	.init_time	= pca100_timer_init,
	.restart	= mxc_restart,
MACHINE_END
