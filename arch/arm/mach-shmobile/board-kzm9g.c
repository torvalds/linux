/*
 * KZM-A9-GT board support
 *
 * Copyright (C) 2012	Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/i2c/pcf857x.h>
#include <linux/input.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/mfd/tmio.h>
#include <linux/platform_device.h>
#include <linux/smsc911x.h>
#include <linux/usb/r8a66597.h>
#include <linux/videodev2.h>
#include <mach/irqs.h>
#include <mach/sh73a0.h>
#include <mach/common.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/gic.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <video/sh_mobile_lcdc.h>

/*
 * external GPIO
 */
#define GPIO_PCF8575_BASE	(GPIO_NR)
#define GPIO_PCF8575_PORT10	(GPIO_NR + 8)
#define GPIO_PCF8575_PORT11	(GPIO_NR + 9)
#define GPIO_PCF8575_PORT12	(GPIO_NR + 10)
#define GPIO_PCF8575_PORT13	(GPIO_NR + 11)
#define GPIO_PCF8575_PORT14	(GPIO_NR + 12)
#define GPIO_PCF8575_PORT15	(GPIO_NR + 13)
#define GPIO_PCF8575_PORT16	(GPIO_NR + 14)

/* SMSC 9221 */
static struct resource smsc9221_resources[] = {
	[0] = {
		.start	= 0x10000000, /* CS4 */
		.end	= 0x100000ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x260), /* IRQ3 */
		.flags	= IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config smsc9221_platdata = {
	.flags		= SMSC911X_USE_32BIT | SMSC911X_SAVE_MAC_ADDRESS,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
};

static struct platform_device smsc_device = {
	.name		= "smsc911x",
	.dev  = {
		.platform_data = &smsc9221_platdata,
	},
	.resource	= smsc9221_resources,
	.num_resources	= ARRAY_SIZE(smsc9221_resources),
};

/* USB external chip */
static struct r8a66597_platdata usb_host_data = {
	.on_chip	= 0,
	.xtal		= R8A66597_PLATDATA_XTAL_48MHZ,
};

static struct resource usb_resources[] = {
	[0] = {
		.start	= 0x10010000,
		.end	= 0x1001ffff - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x220), /* IRQ1 */
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device usb_host_device = {
	.name	= "r8a66597_hcd",
	.dev = {
		.platform_data		= &usb_host_data,
		.dma_mask		= NULL,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(usb_resources),
	.resource	= usb_resources,
};

/* LCDC */
static struct fb_videomode kzm_lcdc_mode = {
	.name		= "WVGA Panel",
	.xres		= 800,
	.yres		= 480,
	.left_margin	= 220,
	.right_margin	= 110,
	.hsync_len	= 70,
	.upper_margin	= 20,
	.lower_margin	= 5,
	.vsync_len	= 5,
	.sync		= 0,
};

static struct sh_mobile_lcdc_info lcdc_info = {
	.clock_source = LCDC_CLK_BUS,
	.ch[0] = {
		.chan		= LCDC_CHAN_MAINLCD,
		.fourcc		= V4L2_PIX_FMT_RGB565,
		.interface_type	= RGB24,
		.lcd_modes	= &kzm_lcdc_mode,
		.num_modes	= 1,
		.clock_divider	= 5,
		.flags		= 0,
		.panel_cfg = {
			.width	= 152,
			.height	= 91,
		},
	}
};

static struct resource lcdc_resources[] = {
	[0] = {
		.name	= "LCDC",
		.start	= 0xfe940000,
		.end	= 0xfe943fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x580),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device lcdc_device = {
	.name		= "sh_mobile_lcdc_fb",
	.num_resources	= ARRAY_SIZE(lcdc_resources),
	.resource	= lcdc_resources,
	.dev	= {
		.platform_data	= &lcdc_info,
		.coherent_dma_mask = ~0,
	},
};

/* MMCIF */
static struct resource sh_mmcif_resources[] = {
	[0] = {
		.name	= "MMCIF",
		.start	= 0xe6bd0000,
		.end	= 0xe6bd00ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(141),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= gic_spi(140),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct sh_mmcif_plat_data sh_mmcif_platdata = {
	.ocr		= MMC_VDD_165_195,
	.caps		= MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE,
};

static struct platform_device mmc_device = {
	.name		= "sh_mmcif",
	.dev		= {
		.dma_mask		= NULL,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &sh_mmcif_platdata,
	},
	.num_resources	= ARRAY_SIZE(sh_mmcif_resources),
	.resource	= sh_mmcif_resources,
};

/* SDHI */
static struct sh_mobile_sdhi_info sdhi0_info = {
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED,
	.tmio_ocr_mask	= MMC_VDD_27_28 | MMC_VDD_28_29,
};

static struct resource sdhi0_resources[] = {
	[0] = {
		.name	= "SDHI0",
		.start	= 0xee100000,
		.end	= 0xee1000ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= SH_MOBILE_SDHI_IRQ_CARD_DETECT,
		.start	= gic_spi(83),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.name	= SH_MOBILE_SDHI_IRQ_SDCARD,
		.start	= gic_spi(84),
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.name	= SH_MOBILE_SDHI_IRQ_SDIO,
		.start	= gic_spi(85),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi0_device = {
	.name		= "sh_mobile_sdhi",
	.num_resources	= ARRAY_SIZE(sdhi0_resources),
	.resource	= sdhi0_resources,
	.dev	= {
		.platform_data	= &sdhi0_info,
	},
};

/* KEY */
#define GPIO_KEY(c, g, d) { .code = c, .gpio = g, .desc = d, .active_low = 1 }

static struct gpio_keys_button gpio_buttons[] = {
	GPIO_KEY(KEY_BACK,	GPIO_PCF8575_PORT10,	"SW3"),
	GPIO_KEY(KEY_RIGHT,	GPIO_PCF8575_PORT11,	"SW2-R"),
	GPIO_KEY(KEY_LEFT,	GPIO_PCF8575_PORT12,	"SW2-L"),
	GPIO_KEY(KEY_ENTER,	GPIO_PCF8575_PORT13,	"SW2-P"),
	GPIO_KEY(KEY_UP,	GPIO_PCF8575_PORT14,	"SW2-U"),
	GPIO_KEY(KEY_DOWN,	GPIO_PCF8575_PORT15,	"SW2-D"),
	GPIO_KEY(KEY_HOME,	GPIO_PCF8575_PORT16,	"SW1"),
};

static struct gpio_keys_platform_data gpio_key_info = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
	.poll_interval	= 250, /* poling at this point */
};

static struct platform_device gpio_keys_device = {
	/* gpio-pcf857x.c driver doesn't support gpio_to_irq() */
	.name	= "gpio-keys-polled",
	.dev	= {
		.platform_data  = &gpio_key_info,
	},
};

/* I2C */
static struct pcf857x_platform_data pcf8575_pdata = {
	.gpio_base	= GPIO_PCF8575_BASE,
};

static struct i2c_board_info i2c1_devices[] = {
	{
		I2C_BOARD_INFO("st1232-ts", 0x55),
		.irq = intcs_evt2irq(0x300), /* IRQ8 */
	},
};

static struct i2c_board_info i2c3_devices[] = {
	{
		I2C_BOARD_INFO("pcf8575", 0x20),
		.platform_data = &pcf8575_pdata,
	},
};

static struct platform_device *kzm_devices[] __initdata = {
	&smsc_device,
	&usb_host_device,
	&lcdc_device,
	&mmc_device,
	&sdhi0_device,
	&gpio_keys_device,
};

/*
 * FIXME
 *
 * This is quick hack for enabling LCDC backlight
 */
static int __init as3711_enable_lcdc_backlight(void)
{
	struct i2c_adapter *a = i2c_get_adapter(0);
	struct i2c_msg msg;
	int i, ret;
	__u8 magic[] = {
		0x40, 0x2a,
		0x43, 0x3c,
		0x44, 0x3c,
		0x45, 0x3c,
		0x54, 0x03,
		0x51, 0x00,
		0x51, 0x01,
		0xff, 0x00, /* wait */
		0x43, 0xf0,
		0x44, 0xf0,
		0x45, 0xf0,
	};

	if (!machine_is_kzm9g())
		return 0;

	if (!a)
		return 0;

	msg.addr	= 0x40;
	msg.len		= 2;
	msg.flags	= 0;

	for (i = 0; i < ARRAY_SIZE(magic); i += 2) {
		msg.buf = magic + i;

		if (0xff == msg.buf[0]) {
			udelay(500);
			continue;
		}

		ret = i2c_transfer(a, &msg, 1);
		if (ret < 0) {
			pr_err("i2c transfer fail\n");
			break;
		}
	}

	return 0;
}
device_initcall(as3711_enable_lcdc_backlight);

static void __init kzm_init(void)
{
	sh73a0_pinmux_init();

	/* enable SCIFA4 */
	gpio_request(GPIO_FN_SCIFA4_TXD, NULL);
	gpio_request(GPIO_FN_SCIFA4_RXD, NULL);
	gpio_request(GPIO_FN_SCIFA4_RTS_, NULL);
	gpio_request(GPIO_FN_SCIFA4_CTS_, NULL);

	/* CS4 for SMSC/USB */
	gpio_request(GPIO_FN_CS4_, NULL); /* CS4 */

	/* SMSC */
	gpio_request(GPIO_PORT224, NULL); /* IRQ3 */
	gpio_direction_input(GPIO_PORT224);

	/* LCDC */
	gpio_request(GPIO_FN_LCDD23,	NULL);
	gpio_request(GPIO_FN_LCDD22,	NULL);
	gpio_request(GPIO_FN_LCDD21,	NULL);
	gpio_request(GPIO_FN_LCDD20,	NULL);
	gpio_request(GPIO_FN_LCDD19,	NULL);
	gpio_request(GPIO_FN_LCDD18,	NULL);
	gpio_request(GPIO_FN_LCDD17,	NULL);
	gpio_request(GPIO_FN_LCDD16,	NULL);
	gpio_request(GPIO_FN_LCDD15,	NULL);
	gpio_request(GPIO_FN_LCDD14,	NULL);
	gpio_request(GPIO_FN_LCDD13,	NULL);
	gpio_request(GPIO_FN_LCDD12,	NULL);
	gpio_request(GPIO_FN_LCDD11,	NULL);
	gpio_request(GPIO_FN_LCDD10,	NULL);
	gpio_request(GPIO_FN_LCDD9,	NULL);
	gpio_request(GPIO_FN_LCDD8,	NULL);
	gpio_request(GPIO_FN_LCDD7,	NULL);
	gpio_request(GPIO_FN_LCDD6,	NULL);
	gpio_request(GPIO_FN_LCDD5,	NULL);
	gpio_request(GPIO_FN_LCDD4,	NULL);
	gpio_request(GPIO_FN_LCDD3,	NULL);
	gpio_request(GPIO_FN_LCDD2,	NULL);
	gpio_request(GPIO_FN_LCDD1,	NULL);
	gpio_request(GPIO_FN_LCDD0,	NULL);
	gpio_request(GPIO_FN_LCDDISP,	NULL);
	gpio_request(GPIO_FN_LCDDCK,	NULL);

	gpio_request(GPIO_PORT222,	NULL); /* LCDCDON */
	gpio_request(GPIO_PORT226,	NULL); /* SC */
	gpio_direction_output(GPIO_PORT222, 1);
	gpio_direction_output(GPIO_PORT226, 1);

	/* Touchscreen */
	gpio_request(GPIO_PORT223, NULL); /* IRQ8 */
	gpio_direction_input(GPIO_PORT223);

	/* enable MMCIF */
	gpio_request(GPIO_FN_MMCCLK0,		NULL);
	gpio_request(GPIO_FN_MMCCMD0_PU,	NULL);
	gpio_request(GPIO_FN_MMCD0_0_PU,	NULL);
	gpio_request(GPIO_FN_MMCD0_1_PU,	NULL);
	gpio_request(GPIO_FN_MMCD0_2_PU,	NULL);
	gpio_request(GPIO_FN_MMCD0_3_PU,	NULL);
	gpio_request(GPIO_FN_MMCD0_4_PU,	NULL);
	gpio_request(GPIO_FN_MMCD0_5_PU,	NULL);
	gpio_request(GPIO_FN_MMCD0_6_PU,	NULL);
	gpio_request(GPIO_FN_MMCD0_7_PU,	NULL);

	/* enable SD */
	gpio_request(GPIO_FN_SDHIWP0,		NULL);
	gpio_request(GPIO_FN_SDHICD0,		NULL);
	gpio_request(GPIO_FN_SDHICMD0,		NULL);
	gpio_request(GPIO_FN_SDHICLK0,		NULL);
	gpio_request(GPIO_FN_SDHID0_3,		NULL);
	gpio_request(GPIO_FN_SDHID0_2,		NULL);
	gpio_request(GPIO_FN_SDHID0_1,		NULL);
	gpio_request(GPIO_FN_SDHID0_0,		NULL);
	gpio_request(GPIO_FN_SDHI0_VCCQ_MC0_ON,	NULL);
	gpio_request(GPIO_PORT15, NULL);
	gpio_direction_output(GPIO_PORT15, 1); /* power */

	/* I2C 3 */
	gpio_request(GPIO_FN_PORT27_I2C_SCL3, NULL);
	gpio_request(GPIO_FN_PORT28_I2C_SDA3, NULL);

#ifdef CONFIG_CACHE_L2X0
	/* Early BRESP enable, Shared attribute override enable, 64K*8way */
	l2x0_init(IOMEM(0xf0100000), 0x40460000, 0x82000fff);
#endif

	i2c_register_board_info(1, i2c1_devices, ARRAY_SIZE(i2c1_devices));
	i2c_register_board_info(3, i2c3_devices, ARRAY_SIZE(i2c3_devices));

	sh73a0_add_standard_devices();
	platform_add_devices(kzm_devices, ARRAY_SIZE(kzm_devices));
}

static const char *kzm9g_boards_compat_dt[] __initdata = {
	"renesas,kzm9g",
	NULL,
};

DT_MACHINE_START(KZM9G_DT, "kzm9g")
	.map_io		= sh73a0_map_io,
	.init_early	= sh73a0_add_early_devices,
	.nr_irqs	= NR_IRQS_LEGACY,
	.init_irq	= sh73a0_init_irq,
	.handle_irq	= gic_handle_irq,
	.init_machine	= kzm_init,
	.timer		= &shmobile_timer,
	.dt_compat	= kzm9g_boards_compat_dt,
MACHINE_END
