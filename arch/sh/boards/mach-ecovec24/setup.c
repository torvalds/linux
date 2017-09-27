/*
 * Copyright (C) 2009 Renesas Solutions Corp.
 *
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/mtd/physmap.h>
#include <linux/mfd/tmio.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/usb/r8a66597.h>
#include <linux/usb/renesas_usbhs.h>
#include <linux/i2c.h>
#include <linux/platform_data/tsc2007.h>
#include <linux/spi/spi.h>
#include <linux/spi/sh_msiof.h>
#include <linux/spi/mmc_spi.h>
#include <linux/input.h>
#include <linux/input/sh_keysc.h>
#include <linux/platform_data/gpio_backlight.h>
#include <linux/sh_eth.h>
#include <linux/sh_intc.h>
#include <linux/videodev2.h>
#include <video/sh_mobile_lcdc.h>
#include <sound/sh_fsi.h>
#include <sound/simple_card.h>
#include <media/drv-intf/sh_mobile_ceu.h>
#include <media/soc_camera.h>
#include <media/i2c/tw9910.h>
#include <media/i2c/mt9t112.h>
#include <asm/heartbeat.h>
#include <asm/clock.h>
#include <asm/suspend.h>
#include <cpu/sh7724.h>

/*
 *  Address      Interface        BusWidth
 *-----------------------------------------
 *  0x0000_0000  uboot            16bit
 *  0x0004_0000  Linux romImage   16bit
 *  0x0014_0000  MTD for Linux    16bit
 *  0x0400_0000  Internal I/O     16/32bit
 *  0x0800_0000  DRAM             32bit
 *  0x1800_0000  MFI              16bit
 */

/* SWITCH
 *------------------------------
 * DS2[1] = FlashROM write protect  ON     : write protect
 *                                  OFF    : No write protect
 * DS2[2] = RMII / TS, SCIF         ON     : RMII
 *                                  OFF    : TS, SCIF3
 * DS2[3] = Camera / Video          ON     : Camera
 *                                  OFF    : NTSC/PAL (IN)
 * DS2[5] = NTSC_OUT Clock          ON     : On board OSC
 *                                  OFF    : SH7724 DV_CLK
 * DS2[6-7] = MMC / SD              ON-OFF : SD
 *                                  OFF-ON : MMC
 */

/*
 * FSI - DA7210
 *
 * it needs amixer settings for playing
 *
 * amixer set 'HeadPhone' 80
 * amixer set 'Out Mixer Left DAC Left' on
 * amixer set 'Out Mixer Right DAC Right' on
 */

/* Heartbeat */
static unsigned char led_pos[] = { 0, 1, 2, 3 };

static struct heartbeat_data heartbeat_data = {
	.nr_bits = 4,
	.bit_pos = led_pos,
};

static struct resource heartbeat_resource = {
	.start  = 0xA405012C, /* PTG */
	.end    = 0xA405012E - 1,
	.flags  = IORESOURCE_MEM | IORESOURCE_MEM_8BIT,
};

static struct platform_device heartbeat_device = {
	.name           = "heartbeat",
	.id             = -1,
	.dev = {
		.platform_data = &heartbeat_data,
	},
	.num_resources  = 1,
	.resource       = &heartbeat_resource,
};

/* MTD */
static struct mtd_partition nor_flash_partitions[] = {
	{
		.name = "boot loader",
		.offset = 0,
		.size = (5 * 1024 * 1024),
		.mask_flags = MTD_WRITEABLE,  /* force read-only */
	}, {
		.name = "free-area",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data nor_flash_data = {
	.width		= 2,
	.parts		= nor_flash_partitions,
	.nr_parts	= ARRAY_SIZE(nor_flash_partitions),
};

static struct resource nor_flash_resources[] = {
	[0] = {
		.name	= "NOR Flash",
		.start	= 0x00000000,
		.end	= 0x03ffffff,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device nor_flash_device = {
	.name		= "physmap-flash",
	.resource	= nor_flash_resources,
	.num_resources	= ARRAY_SIZE(nor_flash_resources),
	.dev		= {
		.platform_data = &nor_flash_data,
	},
};

/* SH Eth */
#define SH_ETH_ADDR	(0xA4600000)
static struct resource sh_eth_resources[] = {
	[0] = {
		.start = SH_ETH_ADDR,
		.end   = SH_ETH_ADDR + 0x1FC,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = evt2irq(0xd60),
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

static struct sh_eth_plat_data sh_eth_plat = {
	.phy = 0x1f, /* SMSC LAN8700 */
	.edmac_endian = EDMAC_LITTLE_ENDIAN,
	.phy_interface = PHY_INTERFACE_MODE_MII,
	.ether_link_active_low = 1
};

static struct platform_device sh_eth_device = {
	.name = "sh7724-ether",
	.id = 0,
	.dev = {
		.platform_data = &sh_eth_plat,
	},
	.num_resources = ARRAY_SIZE(sh_eth_resources),
	.resource = sh_eth_resources,
};

/* USB0 host */
static void usb0_port_power(int port, int power)
{
	gpio_set_value(GPIO_PTB4, power);
}

static struct r8a66597_platdata usb0_host_data = {
	.on_chip = 1,
	.port_power = usb0_port_power,
};

static struct resource usb0_host_resources[] = {
	[0] = {
		.start	= 0xa4d80000,
		.end	= 0xa4d80124 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0xa20),
		.end	= evt2irq(0xa20),
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct platform_device usb0_host_device = {
	.name		= "r8a66597_hcd",
	.id		= 0,
	.dev = {
		.dma_mask		= NULL,         /*  not use dma */
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &usb0_host_data,
	},
	.num_resources	= ARRAY_SIZE(usb0_host_resources),
	.resource	= usb0_host_resources,
};

/* USB1 host/function */
static void usb1_port_power(int port, int power)
{
	gpio_set_value(GPIO_PTB5, power);
}

static struct r8a66597_platdata usb1_common_data = {
	.on_chip = 1,
	.port_power = usb1_port_power,
};

static struct resource usb1_common_resources[] = {
	[0] = {
		.start	= 0xa4d90000,
		.end	= 0xa4d90124 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0xa40),
		.end	= evt2irq(0xa40),
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct platform_device usb1_common_device = {
	/* .name will be added in arch_setup */
	.id		= 1,
	.dev = {
		.dma_mask		= NULL,         /*  not use dma */
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &usb1_common_data,
	},
	.num_resources	= ARRAY_SIZE(usb1_common_resources),
	.resource	= usb1_common_resources,
};

/*
 * USBHS
 */
static int usbhs_get_id(struct platform_device *pdev)
{
	return gpio_get_value(GPIO_PTB3);
}

static int usbhs_phy_reset(struct platform_device *pdev)
{
	/* enable vbus if HOST */
	if (!gpio_get_value(GPIO_PTB3))
		gpio_set_value(GPIO_PTB5, 1);

	return 0;
}

static struct renesas_usbhs_platform_info usbhs_info = {
	.platform_callback = {
		.get_id		= usbhs_get_id,
		.phy_reset	= usbhs_phy_reset,
	},
	.driver_param = {
		.buswait_bwait		= 4,
		.detection_delay	= 5,
		.d0_tx_id = SHDMA_SLAVE_USB1D0_TX,
		.d0_rx_id = SHDMA_SLAVE_USB1D0_RX,
		.d1_tx_id = SHDMA_SLAVE_USB1D1_TX,
		.d1_rx_id = SHDMA_SLAVE_USB1D1_RX,
	},
};

static struct resource usbhs_resources[] = {
	[0] = {
		.start	= 0xa4d90000,
		.end	= 0xa4d90124 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0xa40),
		.end	= evt2irq(0xa40),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device usbhs_device = {
	.name	= "renesas_usbhs",
	.id	= 1,
	.dev = {
		.dma_mask		= NULL,         /*  not use dma */
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &usbhs_info,
	},
	.num_resources	= ARRAY_SIZE(usbhs_resources),
	.resource	= usbhs_resources,
};

/* LCDC and backlight */
static const struct fb_videomode ecovec_lcd_modes[] = {
	{
		.name		= "Panel",
		.xres		= 800,
		.yres		= 480,
		.left_margin	= 220,
		.right_margin	= 110,
		.hsync_len	= 70,
		.upper_margin	= 20,
		.lower_margin	= 5,
		.vsync_len	= 5,
		.sync		= 0, /* hsync and vsync are active low */
	},
};

static const struct fb_videomode ecovec_dvi_modes[] = {
	{
		.name		= "DVI",
		.xres		= 1280,
		.yres		= 720,
		.left_margin	= 220,
		.right_margin	= 110,
		.hsync_len	= 40,
		.upper_margin	= 20,
		.lower_margin	= 5,
		.vsync_len	= 5,
		.sync = 0, /* hsync and vsync are active low */
	},
};

static struct sh_mobile_lcdc_info lcdc_info = {
	.ch[0] = {
		.interface_type = RGB18,
		.chan = LCDC_CHAN_MAINLCD,
		.fourcc = V4L2_PIX_FMT_RGB565,
		.panel_cfg = { /* 7.0 inch */
			.width = 152,
			.height = 91,
		},
	}
};

static struct resource lcdc_resources[] = {
	[0] = {
		.name	= "LCDC",
		.start	= 0xfe940000,
		.end	= 0xfe942fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0xf40),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device lcdc_device = {
	.name		= "sh_mobile_lcdc_fb",
	.num_resources	= ARRAY_SIZE(lcdc_resources),
	.resource	= lcdc_resources,
	.dev		= {
		.platform_data	= &lcdc_info,
	},
};

static struct gpio_backlight_platform_data gpio_backlight_data = {
	.fbdev = &lcdc_device.dev,
	.gpio = GPIO_PTR1,
	.def_value = 1,
	.name = "backlight",
};

static struct platform_device gpio_backlight_device = {
	.name = "gpio-backlight",
	.dev = {
		.platform_data = &gpio_backlight_data,
	},
};

/* CEU0 */
static struct sh_mobile_ceu_info sh_mobile_ceu0_info = {
	.flags = SH_CEU_FLAG_USE_8BIT_BUS,
};

static struct resource ceu0_resources[] = {
	[0] = {
		.name	= "CEU0",
		.start	= 0xfe910000,
		.end	= 0xfe91009f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0x880),
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		/* place holder for contiguous memory */
	},
};

static struct platform_device ceu0_device = {
	.name		= "sh_mobile_ceu",
	.id             = 0, /* "ceu0" clock */
	.num_resources	= ARRAY_SIZE(ceu0_resources),
	.resource	= ceu0_resources,
	.dev	= {
		.platform_data	= &sh_mobile_ceu0_info,
	},
};

/* CEU1 */
static struct sh_mobile_ceu_info sh_mobile_ceu1_info = {
	.flags = SH_CEU_FLAG_USE_8BIT_BUS,
};

static struct resource ceu1_resources[] = {
	[0] = {
		.name	= "CEU1",
		.start	= 0xfe914000,
		.end	= 0xfe91409f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0x9e0),
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		/* place holder for contiguous memory */
	},
};

static struct platform_device ceu1_device = {
	.name		= "sh_mobile_ceu",
	.id             = 1, /* "ceu1" clock */
	.num_resources	= ARRAY_SIZE(ceu1_resources),
	.resource	= ceu1_resources,
	.dev	= {
		.platform_data	= &sh_mobile_ceu1_info,
	},
};

/* I2C device */
static struct i2c_board_info i2c0_devices[] = {
	{
		I2C_BOARD_INFO("da7210", 0x1a),
	},
};

static struct i2c_board_info i2c1_devices[] = {
	{
		I2C_BOARD_INFO("r2025sd", 0x32),
	},
	{
		I2C_BOARD_INFO("lis3lv02d", 0x1c),
		.irq = evt2irq(0x620),
	}
};

/* KEYSC */
static struct sh_keysc_info keysc_info = {
	.mode		= SH_KEYSC_MODE_1,
	.scan_timing	= 3,
	.delay		= 50,
	.kycr2_delay	= 100,
	.keycodes	= { KEY_1, 0, 0, 0, 0,
			    KEY_2, 0, 0, 0, 0,
			    KEY_3, 0, 0, 0, 0,
			    KEY_4, 0, 0, 0, 0,
			    KEY_5, 0, 0, 0, 0,
			    KEY_6, 0, 0, 0, 0, },
};

static struct resource keysc_resources[] = {
	[0] = {
		.name	= "KEYSC",
		.start  = 0x044b0000,
		.end    = 0x044b000f,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0xbe0),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device keysc_device = {
	.name           = "sh_keysc",
	.id             = 0, /* keysc0 clock */
	.num_resources  = ARRAY_SIZE(keysc_resources),
	.resource       = keysc_resources,
	.dev	= {
		.platform_data	= &keysc_info,
	},
};

/* TouchScreen */
#define IRQ0 evt2irq(0x600)

static int ts_get_pendown_state(struct device *dev)
{
	int val = 0;
	gpio_free(GPIO_FN_INTC_IRQ0);
	gpio_request(GPIO_PTZ0, NULL);
	gpio_direction_input(GPIO_PTZ0);

	val = gpio_get_value(GPIO_PTZ0);

	gpio_free(GPIO_PTZ0);
	gpio_request(GPIO_FN_INTC_IRQ0, NULL);

	return val ? 0 : 1;
}

static int ts_init(void)
{
	gpio_request(GPIO_FN_INTC_IRQ0, NULL);
	return 0;
}

static struct tsc2007_platform_data tsc2007_info = {
	.model			= 2007,
	.x_plate_ohms		= 180,
	.get_pendown_state	= ts_get_pendown_state,
	.init_platform_hw	= ts_init,
};

static struct i2c_board_info ts_i2c_clients = {
	I2C_BOARD_INFO("tsc2007", 0x48),
	.type		= "tsc2007",
	.platform_data	= &tsc2007_info,
	.irq		= IRQ0,
};

static struct regulator_consumer_supply cn12_power_consumers[] =
{
	REGULATOR_SUPPLY("vmmc", "sh_mmcif.0"),
	REGULATOR_SUPPLY("vqmmc", "sh_mmcif.0"),
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.1"),
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.1"),
};

static struct regulator_init_data cn12_power_init_data = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = ARRAY_SIZE(cn12_power_consumers),
	.consumer_supplies      = cn12_power_consumers,
};

static struct fixed_voltage_config cn12_power_info = {
	.supply_name = "CN12 SD/MMC Vdd",
	.microvolts = 3300000,
	.gpio = GPIO_PTB7,
	.enable_high = 1,
	.init_data = &cn12_power_init_data,
};

static struct platform_device cn12_power = {
	.name = "reg-fixed-voltage",
	.id   = 0,
	.dev  = {
		.platform_data = &cn12_power_info,
	},
};

#if defined(CONFIG_MMC_SDHI) || defined(CONFIG_MMC_SDHI_MODULE)
/* SDHI0 */
static struct regulator_consumer_supply sdhi0_power_consumers[] =
{
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.0"),
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.0"),
};

static struct regulator_init_data sdhi0_power_init_data = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = ARRAY_SIZE(sdhi0_power_consumers),
	.consumer_supplies      = sdhi0_power_consumers,
};

static struct fixed_voltage_config sdhi0_power_info = {
	.supply_name = "CN11 SD/MMC Vdd",
	.microvolts = 3300000,
	.gpio = GPIO_PTB6,
	.enable_high = 1,
	.init_data = &sdhi0_power_init_data,
};

static struct platform_device sdhi0_power = {
	.name = "reg-fixed-voltage",
	.id   = 1,
	.dev  = {
		.platform_data = &sdhi0_power_info,
	},
};

static struct tmio_mmc_data sdhi0_info = {
	.chan_priv_tx	= (void *)SHDMA_SLAVE_SDHI0_TX,
	.chan_priv_rx	= (void *)SHDMA_SLAVE_SDHI0_RX,
	.capabilities	= MMC_CAP_SDIO_IRQ | MMC_CAP_POWER_OFF_CARD |
			  MMC_CAP_NEEDS_POLL,
	.flags		= TMIO_MMC_USE_GPIO_CD,
	.cd_gpio	= GPIO_PTY7,
};

static struct resource sdhi0_resources[] = {
	[0] = {
		.name	= "SDHI0",
		.start  = 0x04ce0000,
		.end    = 0x04ce00ff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0xe80),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi0_device = {
	.name           = "sh_mobile_sdhi",
	.num_resources  = ARRAY_SIZE(sdhi0_resources),
	.resource       = sdhi0_resources,
	.id             = 0,
	.dev	= {
		.platform_data	= &sdhi0_info,
	},
};

#if !defined(CONFIG_MMC_SH_MMCIF) && !defined(CONFIG_MMC_SH_MMCIF_MODULE)
/* SDHI1 */
static struct tmio_mmc_data sdhi1_info = {
	.chan_priv_tx	= (void *)SHDMA_SLAVE_SDHI1_TX,
	.chan_priv_rx	= (void *)SHDMA_SLAVE_SDHI1_RX,
	.capabilities	= MMC_CAP_SDIO_IRQ | MMC_CAP_POWER_OFF_CARD |
			  MMC_CAP_NEEDS_POLL,
	.flags		= TMIO_MMC_USE_GPIO_CD,
	.cd_gpio	= GPIO_PTW7,
};

static struct resource sdhi1_resources[] = {
	[0] = {
		.name	= "SDHI1",
		.start  = 0x04cf0000,
		.end    = 0x04cf00ff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0x4e0),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi1_device = {
	.name           = "sh_mobile_sdhi",
	.num_resources  = ARRAY_SIZE(sdhi1_resources),
	.resource       = sdhi1_resources,
	.id             = 1,
	.dev	= {
		.platform_data	= &sdhi1_info,
	},
};
#endif /* CONFIG_MMC_SH_MMCIF */

#else

/* MMC SPI */
static void mmc_spi_setpower(struct device *dev, unsigned int maskval)
{
	gpio_set_value(GPIO_PTB6, maskval ? 1 : 0);
}

static struct mmc_spi_platform_data mmc_spi_info = {
	.caps = MMC_CAP_NEEDS_POLL,
	.caps2 = MMC_CAP2_RO_ACTIVE_HIGH,
	.ocr_mask = MMC_VDD_32_33 | MMC_VDD_33_34, /* 3.3V only */
	.setpower = mmc_spi_setpower,
	.flags = MMC_SPI_USE_CD_GPIO | MMC_SPI_USE_RO_GPIO,
	.cd_gpio = GPIO_PTY7,
	.ro_gpio = GPIO_PTY6,
};

static struct spi_board_info spi_bus[] = {
	{
		.modalias	= "mmc_spi",
		.platform_data	= &mmc_spi_info,
		.max_speed_hz	= 5000000,
		.mode		= SPI_MODE_0,
		.controller_data = (void *) GPIO_PTM4,
	},
};

/* MSIOF0 */
static struct sh_msiof_spi_info msiof0_data = {
	.num_chipselect = 1,
};

static struct resource msiof0_resources[] = {
	[0] = {
		.name	= "MSIOF0",
		.start	= 0xa4c40000,
		.end	= 0xa4c40063,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0xc80),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device msiof0_device = {
	.name		= "spi_sh_msiof",
	.id		= 0, /* MSIOF0 */
	.dev = {
		.platform_data = &msiof0_data,
	},
	.num_resources	= ARRAY_SIZE(msiof0_resources),
	.resource	= msiof0_resources,
};

#endif

/* I2C Video/Camera */
static struct i2c_board_info i2c_camera[] = {
	{
		I2C_BOARD_INFO("tw9910", 0x45),
	},
	{
		/* 1st camera */
		I2C_BOARD_INFO("mt9t112", 0x3c),
	},
	{
		/* 2nd camera */
		I2C_BOARD_INFO("mt9t112", 0x3c),
	},
};

/* tw9910 */
static int tw9910_power(struct device *dev, int mode)
{
	int val = mode ? 0 : 1;

	gpio_set_value(GPIO_PTU2, val);
	if (mode)
		mdelay(100);

	return 0;
}

static struct tw9910_video_info tw9910_info = {
	.buswidth	= SOCAM_DATAWIDTH_8,
	.mpout		= TW9910_MPO_FIELD,
};

static struct soc_camera_link tw9910_link = {
	.i2c_adapter_id	= 0,
	.bus_id		= 1,
	.power		= tw9910_power,
	.board_info	= &i2c_camera[0],
	.priv		= &tw9910_info,
};

/* mt9t112 */
static int mt9t112_power1(struct device *dev, int mode)
{
	gpio_set_value(GPIO_PTA3, mode);
	if (mode)
		mdelay(100);

	return 0;
}

static struct mt9t112_camera_info mt9t112_info1 = {
	.flags = MT9T112_FLAG_PCLK_RISING_EDGE | MT9T112_FLAG_DATAWIDTH_8,
	.divider = { 0x49, 0x6, 0, 6, 0, 9, 9, 6, 0 }, /* for 24MHz */
};

static struct soc_camera_link mt9t112_link1 = {
	.i2c_adapter_id	= 0,
	.power		= mt9t112_power1,
	.bus_id		= 0,
	.board_info	= &i2c_camera[1],
	.priv		= &mt9t112_info1,
};

static int mt9t112_power2(struct device *dev, int mode)
{
	gpio_set_value(GPIO_PTA4, mode);
	if (mode)
		mdelay(100);

	return 0;
}

static struct mt9t112_camera_info mt9t112_info2 = {
	.flags = MT9T112_FLAG_PCLK_RISING_EDGE | MT9T112_FLAG_DATAWIDTH_8,
	.divider = { 0x49, 0x6, 0, 6, 0, 9, 9, 6, 0 }, /* for 24MHz */
};

static struct soc_camera_link mt9t112_link2 = {
	.i2c_adapter_id	= 1,
	.power		= mt9t112_power2,
	.bus_id		= 1,
	.board_info	= &i2c_camera[2],
	.priv		= &mt9t112_info2,
};

static struct platform_device camera_devices[] = {
	{
		.name	= "soc-camera-pdrv",
		.id	= 0,
		.dev	= {
			.platform_data = &tw9910_link,
		},
	},
	{
		.name	= "soc-camera-pdrv",
		.id	= 1,
		.dev	= {
			.platform_data = &mt9t112_link1,
		},
	},
	{
		.name	= "soc-camera-pdrv",
		.id	= 2,
		.dev	= {
			.platform_data = &mt9t112_link2,
		},
	},
};

/* FSI */
static struct resource fsi_resources[] = {
	[0] = {
		.name	= "FSI",
		.start	= 0xFE3C0000,
		.end	= 0xFE3C021d,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0xf80),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device fsi_device = {
	.name		= "sh_fsi",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(fsi_resources),
	.resource	= fsi_resources,
};

static struct asoc_simple_card_info fsi_da7210_info = {
	.name		= "DA7210",
	.card		= "FSIB-DA7210",
	.codec		= "da7210.0-001a",
	.platform	= "sh_fsi.0",
	.daifmt		= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBM_CFM,
	.cpu_dai = {
		.name	= "fsib-dai",
	},
	.codec_dai = {
		.name	= "da7210-hifi",
	},
};

static struct platform_device fsi_da7210_device = {
	.name	= "asoc-simple-card",
	.dev	= {
		.platform_data	= &fsi_da7210_info,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.dma_mask = &fsi_da7210_device.dev.coherent_dma_mask,
	},
};


/* IrDA */
static struct resource irda_resources[] = {
	[0] = {
		.name	= "IrDA",
		.start  = 0xA45D0000,
		.end    = 0xA45D0049,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0x480),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device irda_device = {
	.name           = "sh_sir",
	.num_resources  = ARRAY_SIZE(irda_resources),
	.resource       = irda_resources,
};

#include <media/i2c/ak881x.h>
#include <media/drv-intf/sh_vou.h>

static struct ak881x_pdata ak881x_pdata = {
	.flags = AK881X_IF_MODE_SLAVE,
};

static struct i2c_board_info ak8813 = {
	I2C_BOARD_INFO("ak8813", 0x20),
	.platform_data = &ak881x_pdata,
};

static struct sh_vou_pdata sh_vou_pdata = {
	.bus_fmt	= SH_VOU_BUS_8BIT,
	.flags		= SH_VOU_HSYNC_LOW | SH_VOU_VSYNC_LOW,
	.board_info	= &ak8813,
	.i2c_adap	= 0,
};

static struct resource sh_vou_resources[] = {
	[0] = {
		.start  = 0xfe960000,
		.end    = 0xfe962043,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0x8e0),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device vou_device = {
	.name           = "sh-vou",
	.id		= -1,
	.num_resources  = ARRAY_SIZE(sh_vou_resources),
	.resource       = sh_vou_resources,
	.dev		= {
		.platform_data	= &sh_vou_pdata,
	},
};

#if defined(CONFIG_MMC_SH_MMCIF) || defined(CONFIG_MMC_SH_MMCIF_MODULE)
/* SH_MMCIF */
static struct resource sh_mmcif_resources[] = {
	[0] = {
		.name	= "SH_MMCIF",
		.start	= 0xA4CA0000,
		.end	= 0xA4CA00FF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* MMC2I */
		.start	= evt2irq(0x5a0),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* MMC3I */
		.start	= evt2irq(0x5c0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct sh_mmcif_plat_data sh_mmcif_plat = {
	.sup_pclk	= 0, /* SH7724: Max Pclk/2 */
	.caps		= MMC_CAP_4_BIT_DATA |
			  MMC_CAP_8_BIT_DATA |
			  MMC_CAP_NEEDS_POLL,
	.ocr		= MMC_VDD_32_33 | MMC_VDD_33_34,
};

static struct platform_device sh_mmcif_device = {
	.name		= "sh_mmcif",
	.id		= 0,
	.dev		= {
		.platform_data		= &sh_mmcif_plat,
	},
	.num_resources	= ARRAY_SIZE(sh_mmcif_resources),
	.resource	= sh_mmcif_resources,
};
#endif

static struct platform_device *ecovec_devices[] __initdata = {
	&heartbeat_device,
	&nor_flash_device,
	&sh_eth_device,
	&usb0_host_device,
	&usb1_common_device,
	&usbhs_device,
	&lcdc_device,
	&gpio_backlight_device,
	&ceu0_device,
	&ceu1_device,
	&keysc_device,
	&cn12_power,
#if defined(CONFIG_MMC_SDHI) || defined(CONFIG_MMC_SDHI_MODULE)
	&sdhi0_power,
	&sdhi0_device,
#if !defined(CONFIG_MMC_SH_MMCIF) && !defined(CONFIG_MMC_SH_MMCIF_MODULE)
	&sdhi1_device,
#endif
#else
	&msiof0_device,
#endif
	&camera_devices[0],
	&camera_devices[1],
	&camera_devices[2],
	&fsi_device,
	&fsi_da7210_device,
	&irda_device,
	&vou_device,
#if defined(CONFIG_MMC_SH_MMCIF) || defined(CONFIG_MMC_SH_MMCIF_MODULE)
	&sh_mmcif_device,
#endif
};

#ifdef CONFIG_I2C
#define EEPROM_ADDR 0x50
static u8 mac_read(struct i2c_adapter *a, u8 command)
{
	struct i2c_msg msg[2];
	u8 buf;
	int ret;

	msg[0].addr  = EEPROM_ADDR;
	msg[0].flags = 0;
	msg[0].len   = 1;
	msg[0].buf   = &command;

	msg[1].addr  = EEPROM_ADDR;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = 1;
	msg[1].buf   = &buf;

	ret = i2c_transfer(a, msg, 2);
	if (ret < 0) {
		printk(KERN_ERR "error %d\n", ret);
		buf = 0xff;
	}

	return buf;
}

static void __init sh_eth_init(struct sh_eth_plat_data *pd)
{
	struct i2c_adapter *a = i2c_get_adapter(1);
	int i;

	if (!a) {
		pr_err("can not get I2C 1\n");
		return;
	}

	/* read MAC address from EEPROM */
	for (i = 0; i < sizeof(pd->mac_addr); i++) {
		pd->mac_addr[i] = mac_read(a, 0x10 + i);
		msleep(10);
	}

	i2c_put_adapter(a);
}
#else
static void __init sh_eth_init(struct sh_eth_plat_data *pd)
{
	pr_err("unable to read sh_eth MAC address\n");
}
#endif

#define PORT_HIZA 0xA4050158
#define IODRIVEA  0xA405018A

extern char ecovec24_sdram_enter_start;
extern char ecovec24_sdram_enter_end;
extern char ecovec24_sdram_leave_start;
extern char ecovec24_sdram_leave_end;

static int __init arch_setup(void)
{
	struct clk *clk;
	bool cn12_enabled = false;

	/* register board specific self-refresh code */
	sh_mobile_register_self_refresh(SUSP_SH_STANDBY | SUSP_SH_SF |
					SUSP_SH_RSTANDBY,
					&ecovec24_sdram_enter_start,
					&ecovec24_sdram_enter_end,
					&ecovec24_sdram_leave_start,
					&ecovec24_sdram_leave_end);

	/* enable STATUS0, STATUS2 and PDSTATUS */
	gpio_request(GPIO_FN_STATUS0, NULL);
	gpio_request(GPIO_FN_STATUS2, NULL);
	gpio_request(GPIO_FN_PDSTATUS, NULL);

	/* enable SCIFA0 */
	gpio_request(GPIO_FN_SCIF0_TXD, NULL);
	gpio_request(GPIO_FN_SCIF0_RXD, NULL);

	/* enable debug LED */
	gpio_request(GPIO_PTG0, NULL);
	gpio_request(GPIO_PTG1, NULL);
	gpio_request(GPIO_PTG2, NULL);
	gpio_request(GPIO_PTG3, NULL);
	gpio_direction_output(GPIO_PTG0, 0);
	gpio_direction_output(GPIO_PTG1, 0);
	gpio_direction_output(GPIO_PTG2, 0);
	gpio_direction_output(GPIO_PTG3, 0);
	__raw_writew((__raw_readw(PORT_HIZA) & ~(0x1 << 1)) , PORT_HIZA);

	/* enable SH-Eth */
	gpio_request(GPIO_PTA1, NULL);
	gpio_direction_output(GPIO_PTA1, 1);
	mdelay(20);

	gpio_request(GPIO_FN_RMII_RXD0,    NULL);
	gpio_request(GPIO_FN_RMII_RXD1,    NULL);
	gpio_request(GPIO_FN_RMII_TXD0,    NULL);
	gpio_request(GPIO_FN_RMII_TXD1,    NULL);
	gpio_request(GPIO_FN_RMII_REF_CLK, NULL);
	gpio_request(GPIO_FN_RMII_TX_EN,   NULL);
	gpio_request(GPIO_FN_RMII_RX_ER,   NULL);
	gpio_request(GPIO_FN_RMII_CRS_DV,  NULL);
	gpio_request(GPIO_FN_MDIO,         NULL);
	gpio_request(GPIO_FN_MDC,          NULL);
	gpio_request(GPIO_FN_LNKSTA,       NULL);

	/* enable USB */
	__raw_writew(0x0000, 0xA4D80000);
	__raw_writew(0x0000, 0xA4D90000);
	gpio_request(GPIO_PTB3,  NULL);
	gpio_request(GPIO_PTB4,  NULL);
	gpio_request(GPIO_PTB5,  NULL);
	gpio_direction_input(GPIO_PTB3);
	gpio_direction_output(GPIO_PTB4, 0);
	gpio_direction_output(GPIO_PTB5, 0);
	__raw_writew(0x0600, 0xa40501d4);
	__raw_writew(0x0600, 0xa4050192);

	if (gpio_get_value(GPIO_PTB3)) {
		printk(KERN_INFO "USB1 function is selected\n");
		usb1_common_device.name = "r8a66597_udc";
	} else {
		printk(KERN_INFO "USB1 host is selected\n");
		usb1_common_device.name = "r8a66597_hcd";
	}

	/* enable LCDC */
	gpio_request(GPIO_FN_LCDD23,   NULL);
	gpio_request(GPIO_FN_LCDD22,   NULL);
	gpio_request(GPIO_FN_LCDD21,   NULL);
	gpio_request(GPIO_FN_LCDD20,   NULL);
	gpio_request(GPIO_FN_LCDD19,   NULL);
	gpio_request(GPIO_FN_LCDD18,   NULL);
	gpio_request(GPIO_FN_LCDD17,   NULL);
	gpio_request(GPIO_FN_LCDD16,   NULL);
	gpio_request(GPIO_FN_LCDD15,   NULL);
	gpio_request(GPIO_FN_LCDD14,   NULL);
	gpio_request(GPIO_FN_LCDD13,   NULL);
	gpio_request(GPIO_FN_LCDD12,   NULL);
	gpio_request(GPIO_FN_LCDD11,   NULL);
	gpio_request(GPIO_FN_LCDD10,   NULL);
	gpio_request(GPIO_FN_LCDD9,    NULL);
	gpio_request(GPIO_FN_LCDD8,    NULL);
	gpio_request(GPIO_FN_LCDD7,    NULL);
	gpio_request(GPIO_FN_LCDD6,    NULL);
	gpio_request(GPIO_FN_LCDD5,    NULL);
	gpio_request(GPIO_FN_LCDD4,    NULL);
	gpio_request(GPIO_FN_LCDD3,    NULL);
	gpio_request(GPIO_FN_LCDD2,    NULL);
	gpio_request(GPIO_FN_LCDD1,    NULL);
	gpio_request(GPIO_FN_LCDD0,    NULL);
	gpio_request(GPIO_FN_LCDDISP,  NULL);
	gpio_request(GPIO_FN_LCDHSYN,  NULL);
	gpio_request(GPIO_FN_LCDDCK,   NULL);
	gpio_request(GPIO_FN_LCDVSYN,  NULL);
	gpio_request(GPIO_FN_LCDDON,   NULL);
	gpio_request(GPIO_FN_LCDLCLK,  NULL);
	__raw_writew((__raw_readw(PORT_HIZA) & ~0x0001), PORT_HIZA);

	gpio_request(GPIO_PTE6, NULL);
	gpio_request(GPIO_PTU1, NULL);
	gpio_request(GPIO_PTA2, NULL);
	gpio_direction_input(GPIO_PTE6);
	gpio_direction_output(GPIO_PTU1, 0);
	gpio_direction_output(GPIO_PTA2, 0);

	/* I/O buffer drive ability is high */
	__raw_writew((__raw_readw(IODRIVEA) & ~0x00c0) | 0x0080 , IODRIVEA);

	if (gpio_get_value(GPIO_PTE6)) {
		/* DVI */
		lcdc_info.clock_source			= LCDC_CLK_EXTERNAL;
		lcdc_info.ch[0].clock_divider		= 1;
		lcdc_info.ch[0].lcd_modes		= ecovec_dvi_modes;
		lcdc_info.ch[0].num_modes		= ARRAY_SIZE(ecovec_dvi_modes);

		/* No backlight */
		gpio_backlight_data.fbdev = NULL;

		gpio_set_value(GPIO_PTA2, 1);
		gpio_set_value(GPIO_PTU1, 1);
	} else {
		/* Panel */
		lcdc_info.clock_source			= LCDC_CLK_PERIPHERAL;
		lcdc_info.ch[0].clock_divider		= 2;
		lcdc_info.ch[0].lcd_modes		= ecovec_lcd_modes;
		lcdc_info.ch[0].num_modes		= ARRAY_SIZE(ecovec_lcd_modes);

		/* FIXME
		 *
		 * LCDDON control is needed for Panel,
		 * but current sh_mobile_lcdc driver doesn't control it.
		 * It is temporary correspondence
		 */
		gpio_request(GPIO_PTF4, NULL);
		gpio_direction_output(GPIO_PTF4, 1);

		/* enable TouchScreen */
		i2c_register_board_info(0, &ts_i2c_clients, 1);
		irq_set_irq_type(IRQ0, IRQ_TYPE_LEVEL_LOW);
	}

	/* enable CEU0 */
	gpio_request(GPIO_FN_VIO0_D15, NULL);
	gpio_request(GPIO_FN_VIO0_D14, NULL);
	gpio_request(GPIO_FN_VIO0_D13, NULL);
	gpio_request(GPIO_FN_VIO0_D12, NULL);
	gpio_request(GPIO_FN_VIO0_D11, NULL);
	gpio_request(GPIO_FN_VIO0_D10, NULL);
	gpio_request(GPIO_FN_VIO0_D9,  NULL);
	gpio_request(GPIO_FN_VIO0_D8,  NULL);
	gpio_request(GPIO_FN_VIO0_D7,  NULL);
	gpio_request(GPIO_FN_VIO0_D6,  NULL);
	gpio_request(GPIO_FN_VIO0_D5,  NULL);
	gpio_request(GPIO_FN_VIO0_D4,  NULL);
	gpio_request(GPIO_FN_VIO0_D3,  NULL);
	gpio_request(GPIO_FN_VIO0_D2,  NULL);
	gpio_request(GPIO_FN_VIO0_D1,  NULL);
	gpio_request(GPIO_FN_VIO0_D0,  NULL);
	gpio_request(GPIO_FN_VIO0_VD,  NULL);
	gpio_request(GPIO_FN_VIO0_CLK, NULL);
	gpio_request(GPIO_FN_VIO0_FLD, NULL);
	gpio_request(GPIO_FN_VIO0_HD,  NULL);
	platform_resource_setup_memory(&ceu0_device, "ceu0", 4 << 20);

	/* enable CEU1 */
	gpio_request(GPIO_FN_VIO1_D7,  NULL);
	gpio_request(GPIO_FN_VIO1_D6,  NULL);
	gpio_request(GPIO_FN_VIO1_D5,  NULL);
	gpio_request(GPIO_FN_VIO1_D4,  NULL);
	gpio_request(GPIO_FN_VIO1_D3,  NULL);
	gpio_request(GPIO_FN_VIO1_D2,  NULL);
	gpio_request(GPIO_FN_VIO1_D1,  NULL);
	gpio_request(GPIO_FN_VIO1_D0,  NULL);
	gpio_request(GPIO_FN_VIO1_FLD, NULL);
	gpio_request(GPIO_FN_VIO1_HD,  NULL);
	gpio_request(GPIO_FN_VIO1_VD,  NULL);
	gpio_request(GPIO_FN_VIO1_CLK, NULL);
	platform_resource_setup_memory(&ceu1_device, "ceu1", 4 << 20);

	/* enable KEYSC */
	gpio_request(GPIO_FN_KEYOUT5_IN5, NULL);
	gpio_request(GPIO_FN_KEYOUT4_IN6, NULL);
	gpio_request(GPIO_FN_KEYOUT3,     NULL);
	gpio_request(GPIO_FN_KEYOUT2,     NULL);
	gpio_request(GPIO_FN_KEYOUT1,     NULL);
	gpio_request(GPIO_FN_KEYOUT0,     NULL);
	gpio_request(GPIO_FN_KEYIN0,      NULL);

	/* enable user debug switch */
	gpio_request(GPIO_PTR0, NULL);
	gpio_request(GPIO_PTR4, NULL);
	gpio_request(GPIO_PTR5, NULL);
	gpio_request(GPIO_PTR6, NULL);
	gpio_direction_input(GPIO_PTR0);
	gpio_direction_input(GPIO_PTR4);
	gpio_direction_input(GPIO_PTR5);
	gpio_direction_input(GPIO_PTR6);

	/* SD-card slot CN11 */
#if defined(CONFIG_MMC_SDHI) || defined(CONFIG_MMC_SDHI_MODULE)
	/* enable SDHI0 on CN11 (needs DS2.4 set to ON) */
	gpio_request(GPIO_FN_SDHI0WP,  NULL);
	gpio_request(GPIO_FN_SDHI0CMD, NULL);
	gpio_request(GPIO_FN_SDHI0CLK, NULL);
	gpio_request(GPIO_FN_SDHI0D3,  NULL);
	gpio_request(GPIO_FN_SDHI0D2,  NULL);
	gpio_request(GPIO_FN_SDHI0D1,  NULL);
	gpio_request(GPIO_FN_SDHI0D0,  NULL);
#else
	/* enable MSIOF0 on CN11 (needs DS2.4 set to OFF) */
	gpio_request(GPIO_FN_MSIOF0_TXD, NULL);
	gpio_request(GPIO_FN_MSIOF0_RXD, NULL);
	gpio_request(GPIO_FN_MSIOF0_TSCK, NULL);
	gpio_request(GPIO_PTM4, NULL); /* software CS control of TSYNC pin */
	gpio_direction_output(GPIO_PTM4, 1); /* active low CS */
	gpio_request(GPIO_PTB6, NULL); /* 3.3V power control */
	gpio_direction_output(GPIO_PTB6, 0); /* disable power by default */

	spi_register_board_info(spi_bus, ARRAY_SIZE(spi_bus));
#endif

	/* MMC/SD-card slot CN12 */
#if defined(CONFIG_MMC_SH_MMCIF) || defined(CONFIG_MMC_SH_MMCIF_MODULE)
	/* enable MMCIF (needs DS2.6,7 set to OFF,ON) */
	gpio_request(GPIO_FN_MMC_D7, NULL);
	gpio_request(GPIO_FN_MMC_D6, NULL);
	gpio_request(GPIO_FN_MMC_D5, NULL);
	gpio_request(GPIO_FN_MMC_D4, NULL);
	gpio_request(GPIO_FN_MMC_D3, NULL);
	gpio_request(GPIO_FN_MMC_D2, NULL);
	gpio_request(GPIO_FN_MMC_D1, NULL);
	gpio_request(GPIO_FN_MMC_D0, NULL);
	gpio_request(GPIO_FN_MMC_CLK, NULL);
	gpio_request(GPIO_FN_MMC_CMD, NULL);

	cn12_enabled = true;
#elif defined(CONFIG_MMC_SDHI) || defined(CONFIG_MMC_SDHI_MODULE)
	/* enable SDHI1 on CN12 (needs DS2.6,7 set to ON,OFF) */
	gpio_request(GPIO_FN_SDHI1WP,  NULL);
	gpio_request(GPIO_FN_SDHI1CMD, NULL);
	gpio_request(GPIO_FN_SDHI1CLK, NULL);
	gpio_request(GPIO_FN_SDHI1D3,  NULL);
	gpio_request(GPIO_FN_SDHI1D2,  NULL);
	gpio_request(GPIO_FN_SDHI1D1,  NULL);
	gpio_request(GPIO_FN_SDHI1D0,  NULL);

	cn12_enabled = true;
#endif

	if (cn12_enabled)
		/* I/O buffer drive ability is high for CN12 */
		__raw_writew((__raw_readw(IODRIVEA) & ~0x3000) | 0x2000,
			     IODRIVEA);

	/* enable Video */
	gpio_request(GPIO_PTU2, NULL);
	gpio_direction_output(GPIO_PTU2, 1);

	/* enable Camera */
	gpio_request(GPIO_PTA3, NULL);
	gpio_request(GPIO_PTA4, NULL);
	gpio_direction_output(GPIO_PTA3, 0);
	gpio_direction_output(GPIO_PTA4, 0);

	/* enable FSI */
	gpio_request(GPIO_FN_FSIMCKB,    NULL);
	gpio_request(GPIO_FN_FSIIBSD,    NULL);
	gpio_request(GPIO_FN_FSIOBSD,    NULL);
	gpio_request(GPIO_FN_FSIIBBCK,   NULL);
	gpio_request(GPIO_FN_FSIIBLRCK,  NULL);
	gpio_request(GPIO_FN_FSIOBBCK,   NULL);
	gpio_request(GPIO_FN_FSIOBLRCK,  NULL);
	gpio_request(GPIO_FN_CLKAUDIOBO, NULL);

	/* set SPU2 clock to 83.4 MHz */
	clk = clk_get(NULL, "spu_clk");
	if (!IS_ERR(clk)) {
		clk_set_rate(clk, clk_round_rate(clk, 83333333));
		clk_put(clk);
	}

	/* change parent of FSI B */
	clk = clk_get(NULL, "fsib_clk");
	if (!IS_ERR(clk)) {
		/* 48kHz dummy clock was used to make sure 1/1 divide */
		clk_set_rate(&sh7724_fsimckb_clk, 48000);
		clk_set_parent(clk, &sh7724_fsimckb_clk);
		clk_set_rate(clk, 48000);
		clk_put(clk);
	}

	gpio_request(GPIO_PTU0, NULL);
	gpio_direction_output(GPIO_PTU0, 0);
	mdelay(20);

	/* enable motion sensor */
	gpio_request(GPIO_FN_INTC_IRQ1, NULL);
	gpio_direction_input(GPIO_FN_INTC_IRQ1);

	/* set VPU clock to 166 MHz */
	clk = clk_get(NULL, "vpu_clk");
	if (!IS_ERR(clk)) {
		clk_set_rate(clk, clk_round_rate(clk, 166000000));
		clk_put(clk);
	}

	/* enable IrDA */
	gpio_request(GPIO_FN_IRDA_OUT, NULL);
	gpio_request(GPIO_FN_IRDA_IN,  NULL);
	gpio_request(GPIO_PTU5, NULL);
	gpio_direction_output(GPIO_PTU5, 0);

	/* enable I2C device */
	i2c_register_board_info(0, i2c0_devices,
				ARRAY_SIZE(i2c0_devices));

	i2c_register_board_info(1, i2c1_devices,
				ARRAY_SIZE(i2c1_devices));

#if defined(CONFIG_VIDEO_SH_VOU) || defined(CONFIG_VIDEO_SH_VOU_MODULE)
	/* VOU */
	gpio_request(GPIO_FN_DV_D15, NULL);
	gpio_request(GPIO_FN_DV_D14, NULL);
	gpio_request(GPIO_FN_DV_D13, NULL);
	gpio_request(GPIO_FN_DV_D12, NULL);
	gpio_request(GPIO_FN_DV_D11, NULL);
	gpio_request(GPIO_FN_DV_D10, NULL);
	gpio_request(GPIO_FN_DV_D9, NULL);
	gpio_request(GPIO_FN_DV_D8, NULL);
	gpio_request(GPIO_FN_DV_CLKI, NULL);
	gpio_request(GPIO_FN_DV_CLK, NULL);
	gpio_request(GPIO_FN_DV_VSYNC, NULL);
	gpio_request(GPIO_FN_DV_HSYNC, NULL);

	/* AK8813 power / reset sequence */
	gpio_request(GPIO_PTG4, NULL);
	gpio_request(GPIO_PTU3, NULL);
	/* Reset */
	gpio_direction_output(GPIO_PTG4, 0);
	/* Power down */
	gpio_direction_output(GPIO_PTU3, 1);

	udelay(10);

	/* Power up, reset */
	gpio_set_value(GPIO_PTU3, 0);

	udelay(10);

	/* Remove reset */
	gpio_set_value(GPIO_PTG4, 1);
#endif

	return platform_add_devices(ecovec_devices,
				    ARRAY_SIZE(ecovec_devices));
}
arch_initcall(arch_setup);

static int __init devices_setup(void)
{
	sh_eth_init(&sh_eth_plat);
	return 0;
}
device_initcall(devices_setup);

static struct sh_machine_vector mv_ecovec __initmv = {
	.mv_name	= "R0P7724 (EcoVec)",
};
