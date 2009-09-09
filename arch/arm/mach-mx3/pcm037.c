/*
 *  Copyright (C) 2008 Sascha Hauer, Pengutronix
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/init.h>

#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/plat-ram.h>
#include <linux/memory.h>
#include <linux/gpio.h>
#include <linux/smsc911x.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/i2c/at24.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/irq.h>
#include <linux/fsl_devices.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <mach/common.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx3.h>
#include <mach/ipu.h>
#include <mach/board-pcm037.h>
#include <mach/mx3fb.h>
#include <mach/mxc_nand.h>
#include <mach/mmc.h>
#ifdef CONFIG_I2C_IMX
#include <mach/i2c.h>
#endif

#include "devices.h"

static unsigned int pcm037_pins[] = {
	/* I2C */
	MX31_PIN_CSPI2_MOSI__SCL,
	MX31_PIN_CSPI2_MISO__SDA,
	/* SDHC1 */
	MX31_PIN_SD1_DATA3__SD1_DATA3,
	MX31_PIN_SD1_DATA2__SD1_DATA2,
	MX31_PIN_SD1_DATA1__SD1_DATA1,
	MX31_PIN_SD1_DATA0__SD1_DATA0,
	MX31_PIN_SD1_CLK__SD1_CLK,
	MX31_PIN_SD1_CMD__SD1_CMD,
	IOMUX_MODE(MX31_PIN_SCK6, IOMUX_CONFIG_GPIO), /* card detect */
	IOMUX_MODE(MX31_PIN_SFS6, IOMUX_CONFIG_GPIO), /* write protect */
	/* SPI1 */
	MX31_PIN_CSPI1_MOSI__MOSI,
	MX31_PIN_CSPI1_MISO__MISO,
	MX31_PIN_CSPI1_SCLK__SCLK,
	MX31_PIN_CSPI1_SPI_RDY__SPI_RDY,
	MX31_PIN_CSPI1_SS0__SS0,
	MX31_PIN_CSPI1_SS1__SS1,
	MX31_PIN_CSPI1_SS2__SS2,
	/* UART1 */
	MX31_PIN_CTS1__CTS1,
	MX31_PIN_RTS1__RTS1,
	MX31_PIN_TXD1__TXD1,
	MX31_PIN_RXD1__RXD1,
	/* UART2 */
	MX31_PIN_TXD2__TXD2,
	MX31_PIN_RXD2__RXD2,
	MX31_PIN_CTS2__CTS2,
	MX31_PIN_RTS2__RTS2,
	/* UART3 */
	MX31_PIN_CSPI3_MOSI__RXD3,
	MX31_PIN_CSPI3_MISO__TXD3,
	MX31_PIN_CSPI3_SCLK__RTS3,
	MX31_PIN_CSPI3_SPI_RDY__CTS3,
	/* LAN9217 irq pin */
	IOMUX_MODE(MX31_PIN_GPIO3_1, IOMUX_CONFIG_GPIO),
	/* Onewire */
	MX31_PIN_BATT_LINE__OWIRE,
	/* Framebuffer */
	MX31_PIN_LD0__LD0,
	MX31_PIN_LD1__LD1,
	MX31_PIN_LD2__LD2,
	MX31_PIN_LD3__LD3,
	MX31_PIN_LD4__LD4,
	MX31_PIN_LD5__LD5,
	MX31_PIN_LD6__LD6,
	MX31_PIN_LD7__LD7,
	MX31_PIN_LD8__LD8,
	MX31_PIN_LD9__LD9,
	MX31_PIN_LD10__LD10,
	MX31_PIN_LD11__LD11,
	MX31_PIN_LD12__LD12,
	MX31_PIN_LD13__LD13,
	MX31_PIN_LD14__LD14,
	MX31_PIN_LD15__LD15,
	MX31_PIN_LD16__LD16,
	MX31_PIN_LD17__LD17,
	MX31_PIN_VSYNC3__VSYNC3,
	MX31_PIN_HSYNC__HSYNC,
	MX31_PIN_FPSHIFT__FPSHIFT,
	MX31_PIN_DRDY0__DRDY0,
	MX31_PIN_D3_REV__D3_REV,
	MX31_PIN_CONTRAST__CONTRAST,
	MX31_PIN_D3_SPL__D3_SPL,
	MX31_PIN_D3_CLS__D3_CLS,
	MX31_PIN_LCS0__GPI03_23,
};

static struct physmap_flash_data pcm037_flash_data = {
	.width  = 2,
};

static struct resource pcm037_flash_resource = {
	.start	= 0xa0000000,
	.end	= 0xa1ffffff,
	.flags	= IORESOURCE_MEM,
};

static int usbotg_pins[] = {
	MX31_PIN_USBOTG_DATA0__USBOTG_DATA0,
	MX31_PIN_USBOTG_DATA1__USBOTG_DATA1,
	MX31_PIN_USBOTG_DATA2__USBOTG_DATA2,
	MX31_PIN_USBOTG_DATA3__USBOTG_DATA3,
	MX31_PIN_USBOTG_DATA4__USBOTG_DATA4,
	MX31_PIN_USBOTG_DATA5__USBOTG_DATA5,
	MX31_PIN_USBOTG_DATA6__USBOTG_DATA6,
	MX31_PIN_USBOTG_DATA7__USBOTG_DATA7,
	MX31_PIN_USBOTG_CLK__USBOTG_CLK,
	MX31_PIN_USBOTG_DIR__USBOTG_DIR,
	MX31_PIN_USBOTG_NXT__USBOTG_NXT,
	MX31_PIN_USBOTG_STP__USBOTG_STP,
};

/* USB OTG HS port */
static int __init gpio_usbotg_hs_activate(void)
{
	int ret = mxc_iomux_setup_multiple_pins(usbotg_pins,
					ARRAY_SIZE(usbotg_pins), "usbotg");

	if (ret < 0) {
		printk(KERN_ERR "Cannot set up OTG pins\n");
		return ret;
	}

	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA0, PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA1, PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA2, PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA3, PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA4, PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA5, PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA6, PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA7, PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_CLK,   PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DIR,   PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_NXT,   PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_STP,   PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST);

	return 0;
}

/* OTG config */
static struct fsl_usb2_platform_data usb_pdata = {
	.operating_mode	= FSL_USB2_DR_DEVICE,
	.phy_mode	= FSL_USB2_PHY_ULPI,
};

static struct platform_device pcm037_flash = {
	.name	= "physmap-flash",
	.id	= 0,
	.dev	= {
		.platform_data  = &pcm037_flash_data,
	},
	.resource = &pcm037_flash_resource,
	.num_resources = 1,
};

static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct resource smsc911x_resources[] = {
	[0] = {
		.start		= CS1_BASE_ADDR + 0x300,
		.end		= CS1_BASE_ADDR + 0x300 + SZ_64K - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IOMUX_TO_IRQ(MX31_PIN_GPIO3_1),
		.end		= IOMUX_TO_IRQ(MX31_PIN_GPIO3_1),
		.flags		= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct smsc911x_platform_config smsc911x_info = {
	.flags		= SMSC911X_USE_32BIT | SMSC911X_FORCE_INTERNAL_PHY |
			  SMSC911X_SAVE_MAC_ADDRESS,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
};

static struct platform_device pcm037_eth = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smsc911x_resources),
	.resource	= smsc911x_resources,
	.dev		= {
		.platform_data = &smsc911x_info,
	},
};

static struct platdata_mtd_ram pcm038_sram_data = {
	.bankwidth = 2,
};

static struct resource pcm038_sram_resource = {
	.start = CS4_BASE_ADDR,
	.end   = CS4_BASE_ADDR + 512 * 1024 - 1,
	.flags = IORESOURCE_MEM,
};

static struct platform_device pcm037_sram_device = {
	.name = "mtd-ram",
	.id = 0,
	.dev = {
		.platform_data = &pcm038_sram_data,
	},
	.num_resources = 1,
	.resource = &pcm038_sram_resource,
};

static struct mxc_nand_platform_data pcm037_nand_board_info = {
	.width = 1,
	.hw_ecc = 1,
};

#ifdef CONFIG_I2C_IMX
static struct imxi2c_platform_data pcm037_i2c_1_data = {
	.bitrate = 100000,
};

static struct at24_platform_data board_eeprom = {
	.byte_len = 4096,
	.page_size = 32,
	.flags = AT24_FLAG_ADDR16,
};

static struct i2c_board_info pcm037_i2c_devices[] = {
       {
		I2C_BOARD_INFO("at24", 0x52), /* E0=0, E1=1, E2=0 */
		.platform_data = &board_eeprom,
	}, {
		I2C_BOARD_INFO("rtc-pcf8563", 0x51),
		.type = "pcf8563",
	}
};
#endif

/* Not connected by default */
#ifdef PCM970_SDHC_RW_SWITCH
static int pcm970_sdhc1_get_ro(struct device *dev)
{
	return gpio_get_value(IOMUX_TO_GPIO(MX31_PIN_SFS6));
}
#endif

#define SDHC1_GPIO_WP	IOMUX_TO_GPIO(MX31_PIN_SFS6)
#define SDHC1_GPIO_DET	IOMUX_TO_GPIO(MX31_PIN_SCK6)

static int pcm970_sdhc1_init(struct device *dev, irq_handler_t detect_irq,
		void *data)
{
	int ret;

	ret = gpio_request(SDHC1_GPIO_DET, "sdhc-detect");
	if (ret)
		return ret;

	gpio_direction_input(SDHC1_GPIO_DET);

#ifdef PCM970_SDHC_RW_SWITCH
	ret = gpio_request(SDHC1_GPIO_WP, "sdhc-wp");
	if (ret)
		goto err_gpio_free;
	gpio_direction_input(SDHC1_GPIO_WP);
#endif

	ret = request_irq(IOMUX_TO_IRQ(MX31_PIN_SCK6), detect_irq,
			IRQF_DISABLED | IRQF_TRIGGER_FALLING,
				"sdhc-detect", data);
	if (ret)
		goto err_gpio_free_2;

	return 0;

err_gpio_free_2:
#ifdef PCM970_SDHC_RW_SWITCH
	gpio_free(SDHC1_GPIO_WP);
err_gpio_free:
#endif
	gpio_free(SDHC1_GPIO_DET);

	return ret;
}

static void pcm970_sdhc1_exit(struct device *dev, void *data)
{
	free_irq(IOMUX_TO_IRQ(MX31_PIN_SCK6), data);
	gpio_free(SDHC1_GPIO_DET);
	gpio_free(SDHC1_GPIO_WP);
}

static struct imxmmc_platform_data sdhc_pdata = {
#ifdef PCM970_SDHC_RW_SWITCH
	.get_ro = pcm970_sdhc1_get_ro,
#endif
	.init = pcm970_sdhc1_init,
	.exit = pcm970_sdhc1_exit,
};

static struct platform_device *devices[] __initdata = {
	&pcm037_flash,
	&pcm037_sram_device,
};

static struct ipu_platform_data mx3_ipu_data = {
	.irq_base = MXC_IPU_IRQ_START,
};

static const struct fb_videomode fb_modedb[] = {
	{
		/* 240x320 @ 60 Hz Sharp */
		.name		= "Sharp-LQ035Q7DH06-QVGA",
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
		.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_SHARP_MODE |
				  FB_SYNC_CLK_INVERT | FB_SYNC_CLK_IDLE_EN,
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

static struct mx3fb_platform_data mx3fb_pdata = {
	.dma_dev	= &mx3_ipu.dev,
	.name		= "Sharp-LQ035Q7DH06-QVGA",
	.mode		= fb_modedb,
	.num_modes	= ARRAY_SIZE(fb_modedb),
};

/*
 * Board specific initialization.
 */
static void __init mxc_board_init(void)
{
	int ret;

	mxc_iomux_setup_multiple_pins(pcm037_pins, ARRAY_SIZE(pcm037_pins),
			"pcm037");

	platform_add_devices(devices, ARRAY_SIZE(devices));

	mxc_register_device(&mxc_uart_device0, &uart_pdata);
	mxc_register_device(&mxc_uart_device1, &uart_pdata);
	mxc_register_device(&mxc_uart_device2, &uart_pdata);

	mxc_register_device(&mxc_w1_master_device, NULL);

	/* LAN9217 IRQ pin */
	ret = gpio_request(IOMUX_TO_GPIO(MX31_PIN_GPIO3_1), "lan9217-irq");
	if (ret)
		pr_warning("could not get LAN irq gpio\n");
	else {
		gpio_direction_input(IOMUX_TO_GPIO(MX31_PIN_GPIO3_1));
		platform_device_register(&pcm037_eth);
	}


#ifdef CONFIG_I2C_IMX
	i2c_register_board_info(1, pcm037_i2c_devices,
			ARRAY_SIZE(pcm037_i2c_devices));

	mxc_register_device(&mxc_i2c_device1, &pcm037_i2c_1_data);
#endif
	mxc_register_device(&mxc_nand_device, &pcm037_nand_board_info);
	mxc_register_device(&mxcsdhc_device0, &sdhc_pdata);
	mxc_register_device(&mx3_ipu, &mx3_ipu_data);
	mxc_register_device(&mx3_fb, &mx3fb_pdata);
	if (!gpio_usbotg_hs_activate())
		mxc_register_device(&mxc_otg_udc_device, &usb_pdata);
}

static void __init pcm037_timer_init(void)
{
	mx31_clocks_init(26000000);
}

struct sys_timer pcm037_timer = {
	.init	= pcm037_timer_init,
};

MACHINE_START(PCM037, "Phytec Phycore pcm037")
	/* Maintainer: Pengutronix */
	.phys_io	= AIPS1_BASE_ADDR,
	.io_pg_offst	= ((AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params    = PHYS_OFFSET + 0x100,
	.map_io         = mx31_map_io,
	.init_irq       = mxc_init_irq,
	.init_machine   = mxc_board_init,
	.timer          = &pcm037_timer,
MACHINE_END

