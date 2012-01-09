/*
 *  Copyright (C) 2009 Sascha Hauer, Pengutronix
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
 */

#include <linux/types.h>
#include <linux/init.h>

#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/plat-ram.h>
#include <linux/memory.h>
#include <linux/gpio.h>
#include <linux/smc911x.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c/at24.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/iomux-mx35.h>
#include <mach/ulpi.h>
#include <mach/audmux.h>

#include "devices-imx35.h"

static const struct fb_videomode fb_modedb[] = {
	{
		/* 240x320 @ 60 Hz */
		.name		= "Sharp-LQ035Q7",
		.refresh	= 60,
		.xres		= 240,
		.yres		= 320,
		.pixclock	= 185925,
		.left_margin	= 9,
		.right_margin	= 16,
		.upper_margin	= 7,
		.lower_margin	= 9,
		.hsync_len	= 1,
		.vsync_len	= 1,
		.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_SHARP_MODE | FB_SYNC_CLK_INVERT | FB_SYNC_CLK_IDLE_EN,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	}, {
		/* 240x320 @ 60 Hz */
		.name		= "TX090",
		.refresh	= 60,
		.xres		= 240,
		.yres		= 320,
		.pixclock	= 38255,
		.left_margin	= 144,
		.right_margin	= 0,
		.upper_margin	= 7,
		.lower_margin	= 40,
		.hsync_len	= 96,
		.vsync_len	= 1,
		.sync		= FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_ACT_HIGH,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	},
};

static const struct ipu_platform_data mx3_ipu_data __initconst = {
	.irq_base = MXC_IPU_IRQ_START,
};

static struct mx3fb_platform_data mx3fb_pdata __initdata = {
	.name		= "Sharp-LQ035Q7",
	.mode		= fb_modedb,
	.num_modes	= ARRAY_SIZE(fb_modedb),
};

static struct physmap_flash_data pcm043_flash_data = {
	.width  = 2,
};

static struct resource pcm043_flash_resource = {
	.start	= 0xa0000000,
	.end	= 0xa1ffffff,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device pcm043_flash = {
	.name	= "physmap-flash",
	.id	= 0,
	.dev	= {
		.platform_data  = &pcm043_flash_data,
	},
	.resource = &pcm043_flash_resource,
	.num_resources = 1,
};

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static const struct imxi2c_platform_data pcm043_i2c0_data __initconst = {
	.bitrate = 50000,
};

static struct at24_platform_data board_eeprom = {
	.byte_len = 4096,
	.page_size = 32,
	.flags = AT24_FLAG_ADDR16,
};

static struct i2c_board_info pcm043_i2c_devices[] = {
	{
		I2C_BOARD_INFO("at24", 0x52), /* E0=0, E1=1, E2=0 */
		.platform_data = &board_eeprom,
	}, {
		I2C_BOARD_INFO("pcf8563", 0x51),
	},
};

static struct platform_device *devices[] __initdata = {
	&pcm043_flash,
};

static iomux_v3_cfg_t pcm043_pads[] = {
	/* UART1 */
	MX35_PAD_CTS1__UART1_CTS,
	MX35_PAD_RTS1__UART1_RTS,
	MX35_PAD_TXD1__UART1_TXD_MUX,
	MX35_PAD_RXD1__UART1_RXD_MUX,
	/* UART2 */
	MX35_PAD_CTS2__UART2_CTS,
	MX35_PAD_RTS2__UART2_RTS,
	MX35_PAD_TXD2__UART2_TXD_MUX,
	MX35_PAD_RXD2__UART2_RXD_MUX,
	/* FEC */
	MX35_PAD_FEC_TX_CLK__FEC_TX_CLK,
	MX35_PAD_FEC_RX_CLK__FEC_RX_CLK,
	MX35_PAD_FEC_RX_DV__FEC_RX_DV,
	MX35_PAD_FEC_COL__FEC_COL,
	MX35_PAD_FEC_RDATA0__FEC_RDATA_0,
	MX35_PAD_FEC_TDATA0__FEC_TDATA_0,
	MX35_PAD_FEC_TX_EN__FEC_TX_EN,
	MX35_PAD_FEC_MDC__FEC_MDC,
	MX35_PAD_FEC_MDIO__FEC_MDIO,
	MX35_PAD_FEC_TX_ERR__FEC_TX_ERR,
	MX35_PAD_FEC_RX_ERR__FEC_RX_ERR,
	MX35_PAD_FEC_CRS__FEC_CRS,
	MX35_PAD_FEC_RDATA1__FEC_RDATA_1,
	MX35_PAD_FEC_TDATA1__FEC_TDATA_1,
	MX35_PAD_FEC_RDATA2__FEC_RDATA_2,
	MX35_PAD_FEC_TDATA2__FEC_TDATA_2,
	MX35_PAD_FEC_RDATA3__FEC_RDATA_3,
	MX35_PAD_FEC_TDATA3__FEC_TDATA_3,
	/* I2C1 */
	MX35_PAD_I2C1_CLK__I2C1_SCL,
	MX35_PAD_I2C1_DAT__I2C1_SDA,
	/* Display */
	MX35_PAD_LD0__IPU_DISPB_DAT_0,
	MX35_PAD_LD1__IPU_DISPB_DAT_1,
	MX35_PAD_LD2__IPU_DISPB_DAT_2,
	MX35_PAD_LD3__IPU_DISPB_DAT_3,
	MX35_PAD_LD4__IPU_DISPB_DAT_4,
	MX35_PAD_LD5__IPU_DISPB_DAT_5,
	MX35_PAD_LD6__IPU_DISPB_DAT_6,
	MX35_PAD_LD7__IPU_DISPB_DAT_7,
	MX35_PAD_LD8__IPU_DISPB_DAT_8,
	MX35_PAD_LD9__IPU_DISPB_DAT_9,
	MX35_PAD_LD10__IPU_DISPB_DAT_10,
	MX35_PAD_LD11__IPU_DISPB_DAT_11,
	MX35_PAD_LD12__IPU_DISPB_DAT_12,
	MX35_PAD_LD13__IPU_DISPB_DAT_13,
	MX35_PAD_LD14__IPU_DISPB_DAT_14,
	MX35_PAD_LD15__IPU_DISPB_DAT_15,
	MX35_PAD_LD16__IPU_DISPB_DAT_16,
	MX35_PAD_LD17__IPU_DISPB_DAT_17,
	MX35_PAD_D3_HSYNC__IPU_DISPB_D3_HSYNC,
	MX35_PAD_D3_FPSHIFT__IPU_DISPB_D3_CLK,
	MX35_PAD_D3_DRDY__IPU_DISPB_D3_DRDY,
	MX35_PAD_CONTRAST__IPU_DISPB_CONTR,
	MX35_PAD_D3_VSYNC__IPU_DISPB_D3_VSYNC,
	MX35_PAD_D3_REV__IPU_DISPB_D3_REV,
	MX35_PAD_D3_CLS__IPU_DISPB_D3_CLS,
	/* gpio */
	MX35_PAD_ATA_CS0__GPIO2_6,
	/* USB host */
	MX35_PAD_I2C2_CLK__USB_TOP_USBH2_PWR,
	MX35_PAD_I2C2_DAT__USB_TOP_USBH2_OC,
	/* SSI */
	MX35_PAD_STXFS4__AUDMUX_AUD4_TXFS,
	MX35_PAD_STXD4__AUDMUX_AUD4_TXD,
	MX35_PAD_SRXD4__AUDMUX_AUD4_RXD,
	MX35_PAD_SCK4__AUDMUX_AUD4_TXC,
	/* CAN2 */
	MX35_PAD_TX5_RX0__CAN2_TXCAN,
	MX35_PAD_TX4_RX1__CAN2_RXCAN,
	/* esdhc */
	MX35_PAD_SD1_CMD__ESDHC1_CMD,
	MX35_PAD_SD1_CLK__ESDHC1_CLK,
	MX35_PAD_SD1_DATA0__ESDHC1_DAT0,
	MX35_PAD_SD1_DATA1__ESDHC1_DAT1,
	MX35_PAD_SD1_DATA2__ESDHC1_DAT2,
	MX35_PAD_SD1_DATA3__ESDHC1_DAT3,
	MX35_PAD_ATA_DATA10__GPIO2_23, /* WriteProtect */
	MX35_PAD_ATA_DATA11__GPIO2_24, /* CardDetect */
};

#define AC97_GPIO_TXFS	IMX_GPIO_NR(2, 31)
#define AC97_GPIO_TXD	IMX_GPIO_NR(2, 28)
#define AC97_GPIO_RESET	IMX_GPIO_NR(2, 0)
#define SD1_GPIO_WP	IMX_GPIO_NR(2, 23)
#define SD1_GPIO_CD	IMX_GPIO_NR(2, 24)

static void pcm043_ac97_warm_reset(struct snd_ac97 *ac97)
{
	iomux_v3_cfg_t txfs_gpio = MX35_PAD_STXFS4__GPIO2_31;
	iomux_v3_cfg_t txfs = MX35_PAD_STXFS4__AUDMUX_AUD4_TXFS;
	int ret;

	ret = gpio_request(AC97_GPIO_TXFS, "SSI");
	if (ret) {
		printk("failed to get GPIO_TXFS: %d\n", ret);
		return;
	}

	mxc_iomux_v3_setup_pad(txfs_gpio);

	/* warm reset */
	gpio_direction_output(AC97_GPIO_TXFS, 1);
	udelay(2);
	gpio_set_value(AC97_GPIO_TXFS, 0);

	gpio_free(AC97_GPIO_TXFS);
	mxc_iomux_v3_setup_pad(txfs);
}

static void pcm043_ac97_cold_reset(struct snd_ac97 *ac97)
{
	iomux_v3_cfg_t txfs_gpio = MX35_PAD_STXFS4__GPIO2_31;
	iomux_v3_cfg_t txfs = MX35_PAD_STXFS4__AUDMUX_AUD4_TXFS;
	iomux_v3_cfg_t txd_gpio = MX35_PAD_STXD4__GPIO2_28;
	iomux_v3_cfg_t txd = MX35_PAD_STXD4__AUDMUX_AUD4_TXD;
	iomux_v3_cfg_t reset_gpio = MX35_PAD_SD2_CMD__GPIO2_0;
	int ret;

	ret = gpio_request(AC97_GPIO_TXFS, "SSI");
	if (ret)
		goto err1;

	ret = gpio_request(AC97_GPIO_TXD, "SSI");
	if (ret)
		goto err2;

	ret = gpio_request(AC97_GPIO_RESET, "SSI");
	if (ret)
		goto err3;

	mxc_iomux_v3_setup_pad(txfs_gpio);
	mxc_iomux_v3_setup_pad(txd_gpio);
	mxc_iomux_v3_setup_pad(reset_gpio);

	gpio_direction_output(AC97_GPIO_TXFS, 0);
	gpio_direction_output(AC97_GPIO_TXD, 0);

	/* cold reset */
	gpio_direction_output(AC97_GPIO_RESET, 0);
	udelay(10);
	gpio_direction_output(AC97_GPIO_RESET, 1);

	mxc_iomux_v3_setup_pad(txd);
	mxc_iomux_v3_setup_pad(txfs);

	gpio_free(AC97_GPIO_RESET);
err3:
	gpio_free(AC97_GPIO_TXD);
err2:
	gpio_free(AC97_GPIO_TXFS);
err1:
	if (ret)
		printk("%s failed with %d\n", __func__, ret);
	mdelay(1);
}

static const struct imx_ssi_platform_data pcm043_ssi_pdata __initconst = {
	.ac97_reset = pcm043_ac97_cold_reset,
	.ac97_warm_reset = pcm043_ac97_warm_reset,
	.flags = IMX_SSI_USE_AC97,
};

static const struct mxc_nand_platform_data
pcm037_nand_board_info __initconst = {
	.width = 1,
	.hw_ecc = 1,
};

static int pcm043_otg_init(struct platform_device *pdev)
{
	return mx35_initialize_usb_hw(pdev->id, MXC_EHCI_INTERFACE_DIFF_UNI);
}

static struct mxc_usbh_platform_data otg_pdata __initdata = {
	.init	= pcm043_otg_init,
	.portsc	= MXC_EHCI_MODE_UTMI,
};

static int pcm043_usbh1_init(struct platform_device *pdev)
{
	return mx35_initialize_usb_hw(pdev->id, MXC_EHCI_INTERFACE_SINGLE_UNI |
			MXC_EHCI_INTERNAL_PHY | MXC_EHCI_IPPUE_DOWN);
}

static const struct mxc_usbh_platform_data usbh1_pdata __initconst = {
	.init	= pcm043_usbh1_init,
	.portsc	= MXC_EHCI_MODE_SERIAL,
};

static const struct fsl_usb2_platform_data otg_device_pdata __initconst = {
	.operating_mode = FSL_USB2_DR_DEVICE,
	.phy_mode       = FSL_USB2_PHY_UTMI,
};

static int otg_mode_host;

static int __init pcm043_otg_mode(char *options)
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
__setup("otg_mode=", pcm043_otg_mode);

static struct esdhc_platform_data sd1_pdata = {
	.wp_gpio = SD1_GPIO_WP,
	.cd_gpio = SD1_GPIO_CD,
	.wp_type = ESDHC_WP_GPIO,
	.cd_type = ESDHC_CD_GPIO,
};

/*
 * Board specific initialization.
 */
static void __init pcm043_init(void)
{
	imx35_soc_init();

	mxc_iomux_v3_setup_multiple_pads(pcm043_pads, ARRAY_SIZE(pcm043_pads));

	mxc_audmux_v2_configure_port(3,
			MXC_AUDMUX_V2_PTCR_SYN | /* 4wire mode */
			MXC_AUDMUX_V2_PTCR_TFSEL(0) |
			MXC_AUDMUX_V2_PTCR_TFSDIR,
			MXC_AUDMUX_V2_PDCR_RXDSEL(0));

	mxc_audmux_v2_configure_port(0,
			MXC_AUDMUX_V2_PTCR_SYN | /* 4wire mode */
			MXC_AUDMUX_V2_PTCR_TCSEL(3) |
			MXC_AUDMUX_V2_PTCR_TCLKDIR, /* clock is output */
			MXC_AUDMUX_V2_PDCR_RXDSEL(3));

	imx35_add_fec(NULL);
	platform_add_devices(devices, ARRAY_SIZE(devices));
	imx35_add_imx2_wdt(NULL);

	imx35_add_imx_uart0(&uart_pdata);
	imx35_add_mxc_nand(&pcm037_nand_board_info);
	imx35_add_imx_ssi(0, &pcm043_ssi_pdata);

	imx35_add_imx_uart1(&uart_pdata);

	i2c_register_board_info(0, pcm043_i2c_devices,
			ARRAY_SIZE(pcm043_i2c_devices));

	imx35_add_imx_i2c0(&pcm043_i2c0_data);

	imx35_add_ipu_core(&mx3_ipu_data);
	imx35_add_mx3_sdc_fb(&mx3fb_pdata);

	if (otg_mode_host) {
		otg_pdata.otg = imx_otg_ulpi_create(ULPI_OTG_DRVVBUS |
				ULPI_OTG_DRVVBUS_EXT);
		if (otg_pdata.otg)
			imx35_add_mxc_ehci_otg(&otg_pdata);
	}
	imx35_add_mxc_ehci_hs(&usbh1_pdata);

	if (!otg_mode_host)
		imx35_add_fsl_usb2_udc(&otg_device_pdata);

	imx35_add_flexcan1(NULL);
	imx35_add_sdhci_esdhc_imx(0, &sd1_pdata);
}

static void __init pcm043_timer_init(void)
{
	mx35_clocks_init();
}

struct sys_timer pcm043_timer = {
	.init	= pcm043_timer_init,
};

MACHINE_START(PCM043, "Phytec Phycore pcm043")
	/* Maintainer: Pengutronix */
	.atag_offset = 0x100,
	.map_io = mx35_map_io,
	.init_early = imx35_init_early,
	.init_irq = mx35_init_irq,
	.handle_irq = imx35_handle_irq,
	.timer = &pcm043_timer,
	.init_machine = pcm043_init,
MACHINE_END
