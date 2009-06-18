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
#include <linux/dma-mapping.h>
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
#include <linux/can/platform/sja1000.h>

#include <media/soc_camera.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <mach/board-pcm037.h>
#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/i2c.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx3.h>
#include <mach/ipu.h>
#include <mach/mmc.h>
#include <mach/mx3_camera.h>
#include <mach/mx3fb.h>
#include <mach/mxc_nand.h>

#include "devices.h"
#include "pcm037.h"

static enum pcm037_board_variant pcm037_instance = PCM037_PCM970;

static int __init pcm037_variant_setup(char *str)
{
	if (!strcmp("eet", str))
		pcm037_instance = PCM037_EET;
	else if (strcmp("pcm970", str))
		pr_warning("Unknown pcm037 baseboard variant %s\n", str);

	return 1;
}

/* Supported values: "pcm970" (default) and "eet" */
__setup("pcm037_variant=", pcm037_variant_setup);

enum pcm037_board_variant pcm037_variant(void)
{
	return pcm037_instance;
}

/* UART1 with RTS/CTS handshake signals */
static unsigned int pcm037_uart1_handshake_pins[] = {
	MX31_PIN_CTS1__CTS1,
	MX31_PIN_RTS1__RTS1,
	MX31_PIN_TXD1__TXD1,
	MX31_PIN_RXD1__RXD1,
};

/* UART1 without RTS/CTS handshake signals */
static unsigned int pcm037_uart1_pins[] = {
	MX31_PIN_TXD1__TXD1,
	MX31_PIN_RXD1__RXD1,
};

static unsigned int pcm037_pins[] = {
	/* I2C */
	MX31_PIN_CSPI2_MOSI__SCL,
	MX31_PIN_CSPI2_MISO__SDA,
	MX31_PIN_CSPI2_SS2__I2C3_SDA,
	MX31_PIN_CSPI2_SCLK__I2C3_SCL,
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
	/* CSI */
	IOMUX_MODE(MX31_PIN_CSI_D5, IOMUX_CONFIG_GPIO),
	MX31_PIN_CSI_D6__CSI_D6,
	MX31_PIN_CSI_D7__CSI_D7,
	MX31_PIN_CSI_D8__CSI_D8,
	MX31_PIN_CSI_D9__CSI_D9,
	MX31_PIN_CSI_D10__CSI_D10,
	MX31_PIN_CSI_D11__CSI_D11,
	MX31_PIN_CSI_D12__CSI_D12,
	MX31_PIN_CSI_D13__CSI_D13,
	MX31_PIN_CSI_D14__CSI_D14,
	MX31_PIN_CSI_D15__CSI_D15,
	MX31_PIN_CSI_HSYNC__CSI_HSYNC,
	MX31_PIN_CSI_MCLK__CSI_MCLK,
	MX31_PIN_CSI_PIXCLK__CSI_PIXCLK,
	MX31_PIN_CSI_VSYNC__CSI_VSYNC,
	/* GPIO */
	IOMUX_MODE(MX31_PIN_ATA_DMACK, IOMUX_CONFIG_GPIO),
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
	{
		.start		= CS1_BASE_ADDR + 0x300,
		.end		= CS1_BASE_ADDR + 0x300 + SZ_64K - 1,
		.flags		= IORESOURCE_MEM,
	}, {
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

static struct imxi2c_platform_data pcm037_i2c_1_data = {
	.bitrate = 100000,
};

static struct imxi2c_platform_data pcm037_i2c_2_data = {
	.bitrate = 20000,
};

static struct at24_platform_data board_eeprom = {
	.byte_len = 4096,
	.page_size = 32,
	.flags = AT24_FLAG_ADDR16,
};

static int pcm037_camera_power(struct device *dev, int on)
{
	/* disable or enable the camera in X7 or X8 PCM970 connector */
	gpio_set_value(IOMUX_TO_GPIO(MX31_PIN_CSI_D5), !on);
	return 0;
}

static struct i2c_board_info pcm037_i2c_2_devices[] = {
	{
		I2C_BOARD_INFO("mt9t031", 0x5d),
	},
};

static struct soc_camera_link iclink = {
	.bus_id		= 0,		/* Must match with the camera ID */
	.power		= pcm037_camera_power,
	.board_info	= &pcm037_i2c_2_devices[0],
	.i2c_adapter_id	= 2,
	.module_name	= "mt9t031",
};

static struct i2c_board_info pcm037_i2c_devices[] = {
	{
		I2C_BOARD_INFO("at24", 0x52), /* E0=0, E1=1, E2=0 */
		.platform_data = &board_eeprom,
	}, {
		I2C_BOARD_INFO("pcf8563", 0x51),
	}
};

static struct platform_device pcm037_camera = {
	.name	= "soc-camera-pdrv",
	.id	= 0,
	.dev	= {
		.platform_data = &iclink,
	},
};

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

struct mx3_camera_pdata camera_pdata = {
	.dma_dev	= &mx3_ipu.dev,
	.flags		= MX3_CAMERA_DATAWIDTH_8 | MX3_CAMERA_DATAWIDTH_10,
	.mclk_10khz	= 2000,
};

static int __init pcm037_camera_alloc_dma(const size_t buf_size)
{
	dma_addr_t dma_handle;
	void *buf;
	int dma;

	if (buf_size < 2 * 1024 * 1024)
		return -EINVAL;

	buf = dma_alloc_coherent(NULL, buf_size, &dma_handle, GFP_KERNEL);
	if (!buf) {
		pr_err("%s: cannot allocate camera buffer-memory\n", __func__);
		return -ENOMEM;
	}

	memset(buf, 0, buf_size);

	dma = dma_declare_coherent_memory(&mx3_camera.dev,
					dma_handle, dma_handle, buf_size,
					DMA_MEMORY_MAP | DMA_MEMORY_EXCLUSIVE);

	/* The way we call dma_declare_coherent_memory only a malloc can fail */
	return dma & DMA_MEMORY_MAP ? 0 : -ENOMEM;
}

static struct platform_device *devices[] __initdata = {
	&pcm037_flash,
	&pcm037_sram_device,
	&pcm037_camera,
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
	}, {
		/* 240x320 @ 60 Hz */
		.name		= "CMEL-OLED",
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
		.sync		= FB_SYNC_OE_ACT_HIGH | FB_SYNC_CLK_INVERT,
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

static struct resource pcm970_sja1000_resources[] = {
	{
		.start   = CS5_BASE_ADDR,
		.end     = CS5_BASE_ADDR + 0x100 - 1,
		.flags   = IORESOURCE_MEM,
	}, {
		.start   = IOMUX_TO_IRQ(IOMUX_PIN(48, 105)),
		.end     = IOMUX_TO_IRQ(IOMUX_PIN(48, 105)),
		.flags   = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	},
};

struct sja1000_platform_data pcm970_sja1000_platform_data = {
	.clock		= 16000000 / 2,
	.ocr		= 0x40 | 0x18,
	.cdr		= 0x40,
};

static struct platform_device pcm970_sja1000 = {
	.name = "sja1000_platform",
	.dev = {
		.platform_data = &pcm970_sja1000_platform_data,
	},
	.resource = pcm970_sja1000_resources,
	.num_resources = ARRAY_SIZE(pcm970_sja1000_resources),
};

/*
 * Board specific initialization.
 */
static void __init mxc_board_init(void)
{
	int ret;

	mxc_iomux_setup_multiple_pins(pcm037_pins, ARRAY_SIZE(pcm037_pins),
			"pcm037");

	if (pcm037_variant() == PCM037_EET)
		mxc_iomux_setup_multiple_pins(pcm037_uart1_pins,
			ARRAY_SIZE(pcm037_uart1_pins), "pcm037_uart1");
	else
		mxc_iomux_setup_multiple_pins(pcm037_uart1_handshake_pins,
			ARRAY_SIZE(pcm037_uart1_handshake_pins),
			"pcm037_uart1");

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


	/* I2C adapters and devices */
	i2c_register_board_info(1, pcm037_i2c_devices,
			ARRAY_SIZE(pcm037_i2c_devices));

	mxc_register_device(&mxc_i2c_device1, &pcm037_i2c_1_data);
	mxc_register_device(&mxc_i2c_device2, &pcm037_i2c_2_data);

	mxc_register_device(&mxc_nand_device, &pcm037_nand_board_info);
	mxc_register_device(&mxcsdhc_device0, &sdhc_pdata);
	mxc_register_device(&mx3_ipu, &mx3_ipu_data);
	mxc_register_device(&mx3_fb, &mx3fb_pdata);
	if (!gpio_usbotg_hs_activate())
		mxc_register_device(&mxc_otg_udc_device, &usb_pdata);

	/* CSI */
	/* Camera power: default - off */
	ret = gpio_request(IOMUX_TO_GPIO(MX31_PIN_CSI_D5), "mt9t031-power");
	if (!ret)
		gpio_direction_output(IOMUX_TO_GPIO(MX31_PIN_CSI_D5), 1);
	else
		iclink.power = NULL;

	if (!pcm037_camera_alloc_dma(4 * 1024 * 1024))
		mxc_register_device(&mx3_camera, &camera_pdata);

	platform_device_register(&pcm970_sja1000);
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
	.init_irq       = mx31_init_irq,
	.init_machine   = mxc_board_init,
	.timer          = &pcm037_timer,
MACHINE_END
