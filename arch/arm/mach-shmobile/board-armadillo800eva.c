/*
 * armadillo 800 eva board support
 *
 * Copyright (C) 2012 Renesas Solutions Corp.
 * Copyright (C) 2012 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
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
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/sh_eth.h>
#include <linux/videodev2.h>
#include <linux/usb/renesas_usbhs.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <asm/page.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/hardware/cache-l2x0.h>
#include <mach/r8a7740.h>
#include <video/sh_mobile_lcdc.h>

/*
 * CON1		Camera Module
 * CON2		Extension Bus
 * CON3		HDMI Output
 * CON4		Composite Video Output
 * CON5		H-UDI JTAG
 * CON6		ARM JTAG
 * CON7		SD1
 * CON8		SD2
 * CON9		RTC BackUp
 * CON10	Monaural Mic Input
 * CON11	Stereo Headphone Output
 * CON12	Audio Line Output(L)
 * CON13	Audio Line Output(R)
 * CON14	AWL13 Module
 * CON15	Extension
 * CON16	LCD1
 * CON17	LCD2
 * CON19	Power Input
 * CON20	USB1
 * CON21	USB2
 * CON22	Serial
 * CON23	LAN
 * CON24	USB3
 * LED1		Camera LED(Yellow)
 * LED2		Power LED (Green)
 * ED3-LED6	User LED(Yellow)
 * LED7		LAN link LED(Green)
 * LED8		LAN activity LED(Yellow)
 */

/*
 * DipSwitch
 *
 *                    SW1
 *
 * -12345678-+---------------+----------------------------
 *  1        | boot          | hermit
 *  0        | boot          | OS auto boot
 * -12345678-+---------------+----------------------------
 *   00      | boot device   | eMMC
 *   10      | boot device   | SDHI0 (CON7)
 *   01      | boot device   | -
 *   11      | boot device   | Extension Buss (CS0)
 * -12345678-+---------------+----------------------------
 *     0     | Extension Bus | D8-D15 disable, eMMC enable
 *     1     | Extension Bus | D8-D15 enable,  eMMC disable
 * -12345678-+---------------+----------------------------
 *      0    | SDHI1         | COM8 disable, COM14 enable
 *      1    | SDHI1         | COM8 enable,  COM14 disable
 * -12345678-+---------------+----------------------------
 *       0   | USB0          | COM20 enable,  COM24 disable
 *       1   | USB0          | COM20 disable, COM24 enable
 * -12345678-+---------------+----------------------------
 *        00 | JTAG          | SH-X2
 *        10 | JTAG          | ARM
 *        01 | JTAG          | -
 *        11 | JTAG          | Boundary Scan
 *-----------+---------------+----------------------------
 */

/*
 * USB function
 *
 * When you use USB Function,
 * set SW1.6 ON, and connect cable to CN24.
 *
 * USBF needs workaround on R8A7740 chip.
 * These are a little bit complex.
 * see
 *	usbhsf_power_ctrl()
 *
 * CAUTION
 *
 * It uses autonomy mode for USB hotplug at this point
 * (= usbhs_private.platform_callback.get_vbus is NULL),
 * since we don't know what's happen on PM control
 * on this workaround.
 */
#define USBCR1		0xe605810a
#define USBH		0xC6700000
#define USBH_USBCTR	0x10834

struct usbhsf_private {
	struct clk *phy;
	struct clk *usb24;
	struct clk *pci;
	struct clk *func;
	struct clk *host;
	void __iomem *usbh_base;
	struct renesas_usbhs_platform_info info;
};

#define usbhsf_get_priv(pdev)				\
	container_of(renesas_usbhs_get_info(pdev),	\
		     struct usbhsf_private, info)

static int usbhsf_get_id(struct platform_device *pdev)
{
	return USBHS_GADGET;
}

static void usbhsf_power_ctrl(struct platform_device *pdev,
			      void __iomem *base, int enable)
{
	struct usbhsf_private *priv = usbhsf_get_priv(pdev);

	/*
	 * Work around for USB Function.
	 * It needs USB host clock, and settings
	 */
	if (enable) {
		/*
		 * enable all the related usb clocks
		 * for usb workaround
		 */
		clk_enable(priv->usb24);
		clk_enable(priv->pci);
		clk_enable(priv->host);
		clk_enable(priv->func);
		clk_enable(priv->phy);

		/*
		 * set USBCR1
		 *
		 * Port1 is driven by USB function,
		 * Port2 is driven by USB HOST
		 * One HOST (Port1 or Port2 is HOST)
		 * USB PLL input clock = 24MHz
		 */
		__raw_writew(0xd750, USBCR1);
		mdelay(1);

		/*
		 * start USB Host
		 */
		__raw_writel(0x0000000c, priv->usbh_base + USBH_USBCTR);
		__raw_writel(0x00000008, priv->usbh_base + USBH_USBCTR);
		mdelay(10);

		/*
		 * USB PHY Power ON
		 */
		__raw_writew(0xd770, USBCR1);
		__raw_writew(0x4000, base + 0x102); /* USBF :: SUSPMODE */

	} else {
		__raw_writel(0x0000010f, priv->usbh_base + USBH_USBCTR);
		__raw_writew(0xd7c0, USBCR1); /* GPIO */

		clk_disable(priv->phy);
		clk_disable(priv->func);	/* usb work around */
		clk_disable(priv->host);	/* usb work around */
		clk_disable(priv->pci);		/* usb work around */
		clk_disable(priv->usb24);	/* usb work around */
	}
}

static void usbhsf_hardware_exit(struct platform_device *pdev)
{
	struct usbhsf_private *priv = usbhsf_get_priv(pdev);

	if (!IS_ERR(priv->phy))
		clk_put(priv->phy);
	if (!IS_ERR(priv->usb24))
		clk_put(priv->usb24);
	if (!IS_ERR(priv->pci))
		clk_put(priv->pci);
	if (!IS_ERR(priv->host))
		clk_put(priv->host);
	if (!IS_ERR(priv->func))
		clk_put(priv->func);
	if (priv->usbh_base)
		iounmap(priv->usbh_base);

	priv->phy	= NULL;
	priv->usb24	= NULL;
	priv->pci	= NULL;
	priv->host	= NULL;
	priv->func	= NULL;
	priv->usbh_base	= NULL;
}

static int usbhsf_hardware_init(struct platform_device *pdev)
{
	struct usbhsf_private *priv = usbhsf_get_priv(pdev);

	priv->phy	= clk_get(&pdev->dev, "phy");
	priv->usb24	= clk_get(&pdev->dev, "usb24");
	priv->pci	= clk_get(&pdev->dev, "pci");
	priv->func	= clk_get(&pdev->dev, "func");
	priv->host	= clk_get(&pdev->dev, "host");
	priv->usbh_base	= ioremap_nocache(USBH, 0x20000);

	if (IS_ERR(priv->phy)		||
	    IS_ERR(priv->usb24)		||
	    IS_ERR(priv->pci)		||
	    IS_ERR(priv->host)		||
	    IS_ERR(priv->func)		||
	    !priv->usbh_base) {
		dev_err(&pdev->dev, "USB clock setting failed\n");
		usbhsf_hardware_exit(pdev);
		return -EIO;
	}

	/* usb24 use 1/1 of parent clock (= usb24s = 24MHz) */
	clk_set_rate(priv->usb24,
		     clk_get_rate(clk_get_parent(priv->usb24)));

	return 0;
}

static struct usbhsf_private usbhsf_private = {
	.info = {
		.platform_callback = {
			.get_id		= usbhsf_get_id,
			.hardware_init	= usbhsf_hardware_init,
			.hardware_exit	= usbhsf_hardware_exit,
			.power_ctrl	= usbhsf_power_ctrl,
		},
		.driver_param = {
			.buswait_bwait		= 5,
			.detection_delay	= 5,
		},
	}
};

static struct resource usbhsf_resources[] = {
	{
		.name	= "USBHS",
		.start	= 0xe6890000,
		.end	= 0xe6890104 - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= evt2irq(0x0A20),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device usbhsf_device = {
	.name	= "renesas_usbhs",
	.dev = {
		.platform_data = &usbhsf_private.info,
	},
	.id = -1,
	.num_resources	= ARRAY_SIZE(usbhsf_resources),
	.resource	= usbhsf_resources,
};

/* Ether */
static struct sh_eth_plat_data sh_eth_platdata = {
	.phy			= 0x00, /* LAN8710A */
	.edmac_endian		= EDMAC_LITTLE_ENDIAN,
	.register_type		= SH_ETH_REG_GIGABIT,
	.phy_interface		= PHY_INTERFACE_MODE_MII,
};

static struct resource sh_eth_resources[] = {
	{
		.start	= 0xe9a00000,
		.end	= 0xe9a00800 - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= 0xe9a01800,
		.end	= 0xe9a02000 - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= evt2irq(0x0500),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sh_eth_device = {
	.name = "sh-eth",
	.id = -1,
	.dev = {
		.platform_data = &sh_eth_platdata,
	},
	.resource = sh_eth_resources,
	.num_resources = ARRAY_SIZE(sh_eth_resources),
};

/* LCDC */
static struct fb_videomode lcdc0_mode = {
	.name		= "AMPIER/AM-800480",
	.xres		= 800,
	.yres		= 480,
	.left_margin	= 88,
	.right_margin	= 40,
	.hsync_len	= 128,
	.upper_margin	= 20,
	.lower_margin	= 5,
	.vsync_len	= 5,
	.sync		= 0,
};

static struct sh_mobile_lcdc_info lcdc0_info = {
	.clock_source	= LCDC_CLK_BUS,
	.ch[0] = {
		.chan		= LCDC_CHAN_MAINLCD,
		.fourcc		= V4L2_PIX_FMT_RGB565,
		.interface_type	= RGB24,
		.clock_divider	= 5,
		.flags		= 0,
		.lcd_modes	= &lcdc0_mode,
		.num_modes	= 1,
		.panel_cfg = {
			.width	= 111,
			.height = 68,
		},
	},
};

static struct resource lcdc0_resources[] = {
	[0] = {
		.name	= "LCD0",
		.start	= 0xfe940000,
		.end	= 0xfe943fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x580),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device lcdc0_device = {
	.name		= "sh_mobile_lcdc_fb",
	.num_resources	= ARRAY_SIZE(lcdc0_resources),
	.resource	= lcdc0_resources,
	.id		= 0,
	.dev	= {
		.platform_data	= &lcdc0_info,
		.coherent_dma_mask = ~0,
	},
};

/* GPIO KEY */
#define GPIO_KEY(c, g, d) { .code = c, .gpio = g, .desc = d, .active_low = 1 }

static struct gpio_keys_button gpio_buttons[] = {
	GPIO_KEY(KEY_POWER,	GPIO_PORT99,	"SW1"),
	GPIO_KEY(KEY_BACK,	GPIO_PORT100,	"SW2"),
	GPIO_KEY(KEY_MENU,	GPIO_PORT97,	"SW3"),
	GPIO_KEY(KEY_HOME,	GPIO_PORT98,	"SW4"),
};

static struct gpio_keys_platform_data gpio_key_info = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
};

static struct platform_device gpio_keys_device = {
	.name   = "gpio-keys",
	.id     = -1,
	.dev    = {
		.platform_data  = &gpio_key_info,
	},
};

/* SDHI0 */
/*
 * FIXME
 *
 * It use polling mode here, since
 * CD (= Card Detect) pin is not connected to SDHI0_CD.
 * We can use IRQ31 as card detect irq,
 * but it needs chattering removal operation
 */
#define IRQ31	evt2irq(0x33E0)
static struct sh_mobile_sdhi_info sdhi0_info = {
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |\
			  MMC_CAP_NEEDS_POLL,
	.tmio_ocr_mask	= MMC_VDD_165_195 | MMC_VDD_32_33 | MMC_VDD_33_34,
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT,
};

static struct resource sdhi0_resources[] = {
	{
		.name	= "SDHI0",
		.start	= 0xe6850000,
		.end	= 0xe6850100 - 1,
		.flags	= IORESOURCE_MEM,
	},
	/*
	 * no SH_MOBILE_SDHI_IRQ_CARD_DETECT here
	 */
	{
		.name	= SH_MOBILE_SDHI_IRQ_SDCARD,
		.start	= evt2irq(0x0E20),
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= SH_MOBILE_SDHI_IRQ_SDIO,
		.start	= evt2irq(0x0E40),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi0_device = {
	.name		= "sh_mobile_sdhi",
	.id		= 0,
	.dev		= {
		.platform_data	= &sdhi0_info,
	},
	.num_resources	= ARRAY_SIZE(sdhi0_resources),
	.resource	= sdhi0_resources,
};

/* SDHI1 */
static struct sh_mobile_sdhi_info sdhi1_info = {
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ,
	.tmio_ocr_mask	= MMC_VDD_165_195 | MMC_VDD_32_33 | MMC_VDD_33_34,
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT,
};

static struct resource sdhi1_resources[] = {
	[0] = {
		.name	= "SDHI1",
		.start	= 0xe6860000,
		.end	= 0xe6860100 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x0E80),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= evt2irq(0x0EA0),
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.start	= evt2irq(0x0EC0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi1_device = {
	.name		= "sh_mobile_sdhi",
	.id		= 1,
	.dev		= {
		.platform_data	= &sdhi1_info,
	},
	.num_resources	= ARRAY_SIZE(sdhi1_resources),
	.resource	= sdhi1_resources,
};

/* MMCIF */
static struct sh_mmcif_plat_data sh_mmcif_plat = {
	.sup_pclk	= 0,
	.ocr		= MMC_VDD_165_195 | MMC_VDD_32_33 | MMC_VDD_33_34,
	.caps		= MMC_CAP_4_BIT_DATA |
			  MMC_CAP_8_BIT_DATA |
			  MMC_CAP_NONREMOVABLE,
};

static struct resource sh_mmcif_resources[] = {
	[0] = {
		.name	= "MMCIF",
		.start	= 0xe6bd0000,
		.end	= 0xe6bd0100 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* MMC ERR */
		.start	= evt2irq(0x1AC0),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* MMC NOR */
		.start	= evt2irq(0x1AE0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sh_mmcif_device = {
	.name		= "sh_mmcif",
	.id		= -1,
	.dev		= {
		.platform_data	= &sh_mmcif_plat,
	},
	.num_resources	= ARRAY_SIZE(sh_mmcif_resources),
	.resource	= sh_mmcif_resources,
};

/* I2C */
static struct i2c_board_info i2c0_devices[] = {
	{
		I2C_BOARD_INFO("st1232-ts", 0x55),
		.irq = evt2irq(0x0340),
	},
};

/*
 * board devices
 */
static struct platform_device *eva_devices[] __initdata = {
	&lcdc0_device,
	&gpio_keys_device,
	&sh_eth_device,
	&sdhi0_device,
	&sh_mmcif_device,
};

static void __init eva_clock_init(void)
{
	struct clk *system	= clk_get(NULL, "system_clk");
	struct clk *xtal1	= clk_get(NULL, "extal1");
	struct clk *usb24s	= clk_get(NULL, "usb24s");

	if (IS_ERR(system)	||
	    IS_ERR(xtal1)	||
	    IS_ERR(usb24s)) {
		pr_err("armadillo800eva board clock init failed\n");
		goto clock_error;
	}

	/* armadillo 800 eva extal1 is 24MHz */
	clk_set_rate(xtal1, 24000000);

	/* usb24s use extal1 (= system) clock (= 24MHz) */
	clk_set_parent(usb24s, system);

clock_error:
	if (!IS_ERR(system))
		clk_put(system);
	if (!IS_ERR(xtal1))
		clk_put(xtal1);
	if (!IS_ERR(usb24s))
		clk_put(usb24s);
}

/*
 * board init
 */
static void __init eva_init(void)
{
	eva_clock_init();

	r8a7740_pinmux_init();

	/* SCIFA1 */
	gpio_request(GPIO_FN_SCIFA1_RXD, NULL);
	gpio_request(GPIO_FN_SCIFA1_TXD, NULL);

	/* LCDC0 */
	gpio_request(GPIO_FN_LCDC0_SELECT,	NULL);
	gpio_request(GPIO_FN_LCD0_D0,		NULL);
	gpio_request(GPIO_FN_LCD0_D1,		NULL);
	gpio_request(GPIO_FN_LCD0_D2,		NULL);
	gpio_request(GPIO_FN_LCD0_D3,		NULL);
	gpio_request(GPIO_FN_LCD0_D4,		NULL);
	gpio_request(GPIO_FN_LCD0_D5,		NULL);
	gpio_request(GPIO_FN_LCD0_D6,		NULL);
	gpio_request(GPIO_FN_LCD0_D7,		NULL);
	gpio_request(GPIO_FN_LCD0_D8,		NULL);
	gpio_request(GPIO_FN_LCD0_D9,		NULL);
	gpio_request(GPIO_FN_LCD0_D10,		NULL);
	gpio_request(GPIO_FN_LCD0_D11,		NULL);
	gpio_request(GPIO_FN_LCD0_D12,		NULL);
	gpio_request(GPIO_FN_LCD0_D13,		NULL);
	gpio_request(GPIO_FN_LCD0_D14,		NULL);
	gpio_request(GPIO_FN_LCD0_D15,		NULL);
	gpio_request(GPIO_FN_LCD0_D16,		NULL);
	gpio_request(GPIO_FN_LCD0_D17,		NULL);
	gpio_request(GPIO_FN_LCD0_D18_PORT40,	NULL);
	gpio_request(GPIO_FN_LCD0_D19_PORT4,	NULL);
	gpio_request(GPIO_FN_LCD0_D20_PORT3,	NULL);
	gpio_request(GPIO_FN_LCD0_D21_PORT2,	NULL);
	gpio_request(GPIO_FN_LCD0_D22_PORT0,	NULL);
	gpio_request(GPIO_FN_LCD0_D23_PORT1,	NULL);
	gpio_request(GPIO_FN_LCD0_DCK,		NULL);
	gpio_request(GPIO_FN_LCD0_VSYN,		NULL);
	gpio_request(GPIO_FN_LCD0_HSYN,		NULL);
	gpio_request(GPIO_FN_LCD0_DISP,		NULL);
	gpio_request(GPIO_FN_LCD0_LCLK_PORT165,	NULL);

	gpio_request(GPIO_PORT61, NULL); /* LCDDON */
	gpio_direction_output(GPIO_PORT61, 1);

	gpio_request(GPIO_PORT202, NULL); /* LCD0_LED_CONT */
	gpio_direction_output(GPIO_PORT202, 0);

	/* Touchscreen */
	gpio_request(GPIO_FN_IRQ10,	NULL); /* TP_INT */
	gpio_request(GPIO_PORT166,	NULL); /* TP_RST_B */
	gpio_direction_output(GPIO_PORT166, 1);

	/* GETHER */
	gpio_request(GPIO_FN_ET_CRS,		NULL);
	gpio_request(GPIO_FN_ET_MDC,		NULL);
	gpio_request(GPIO_FN_ET_MDIO,		NULL);
	gpio_request(GPIO_FN_ET_TX_ER,		NULL);
	gpio_request(GPIO_FN_ET_RX_ER,		NULL);
	gpio_request(GPIO_FN_ET_ERXD0,		NULL);
	gpio_request(GPIO_FN_ET_ERXD1,		NULL);
	gpio_request(GPIO_FN_ET_ERXD2,		NULL);
	gpio_request(GPIO_FN_ET_ERXD3,		NULL);
	gpio_request(GPIO_FN_ET_TX_CLK,		NULL);
	gpio_request(GPIO_FN_ET_TX_EN,		NULL);
	gpio_request(GPIO_FN_ET_ETXD0,		NULL);
	gpio_request(GPIO_FN_ET_ETXD1,		NULL);
	gpio_request(GPIO_FN_ET_ETXD2,		NULL);
	gpio_request(GPIO_FN_ET_ETXD3,		NULL);
	gpio_request(GPIO_FN_ET_PHY_INT,	NULL);
	gpio_request(GPIO_FN_ET_COL,		NULL);
	gpio_request(GPIO_FN_ET_RX_DV,		NULL);
	gpio_request(GPIO_FN_ET_RX_CLK,		NULL);

	gpio_request(GPIO_PORT18, NULL); /* PHY_RST */
	gpio_direction_output(GPIO_PORT18, 1);

	/* USB */
	gpio_request(GPIO_PORT159, NULL); /* USB_DEVICE_MODE */
	gpio_direction_input(GPIO_PORT159);

	if (gpio_get_value(GPIO_PORT159)) {
		/* USB Host */
	} else {
		/* USB Func */
		gpio_request(GPIO_FN_VBUS, NULL);
		platform_device_register(&usbhsf_device);
	}

	/* SDHI0 */
	gpio_request(GPIO_FN_SDHI0_CMD, NULL);
	gpio_request(GPIO_FN_SDHI0_CLK, NULL);
	gpio_request(GPIO_FN_SDHI0_D0, NULL);
	gpio_request(GPIO_FN_SDHI0_D1, NULL);
	gpio_request(GPIO_FN_SDHI0_D2, NULL);
	gpio_request(GPIO_FN_SDHI0_D3, NULL);
	gpio_request(GPIO_FN_SDHI0_WP, NULL);

	gpio_request(GPIO_PORT17, NULL);	/* SDHI0_18/33_B */
	gpio_request(GPIO_PORT74, NULL);	/* SDHI0_PON */
	gpio_request(GPIO_PORT75, NULL);	/* SDSLOT1_PON */
	gpio_direction_output(GPIO_PORT17, 0);
	gpio_direction_output(GPIO_PORT74, 1);
	gpio_direction_output(GPIO_PORT75, 1);

	/* we can use GPIO_FN_IRQ31_PORT167 here for SDHI0 CD irq */

	/*
	 * MMCIF
	 *
	 * Here doesn't care SW1.4 status,
	 * since CON2 is not mounted.
	 */
	gpio_request(GPIO_FN_MMC1_CLK_PORT103,	NULL);
	gpio_request(GPIO_FN_MMC1_CMD_PORT104,	NULL);
	gpio_request(GPIO_FN_MMC1_D0_PORT149,	NULL);
	gpio_request(GPIO_FN_MMC1_D1_PORT148,	NULL);
	gpio_request(GPIO_FN_MMC1_D2_PORT147,	NULL);
	gpio_request(GPIO_FN_MMC1_D3_PORT146,	NULL);
	gpio_request(GPIO_FN_MMC1_D4_PORT145,	NULL);
	gpio_request(GPIO_FN_MMC1_D5_PORT144,	NULL);
	gpio_request(GPIO_FN_MMC1_D6_PORT143,	NULL);
	gpio_request(GPIO_FN_MMC1_D7_PORT142,	NULL);

	/*
	 * CAUTION
	 *
	 * DBGMD/LCDC0/FSIA MUX
	 * DBGMD_SELECT_B should be set after setting PFC Function.
	 */
	gpio_request(GPIO_PORT176, NULL);
	gpio_direction_output(GPIO_PORT176, 1);

	/*
	 * We can switch CON8/CON14 by SW1.5,
	 * but it needs after DBGMD_SELECT_B
	 */
	gpio_request(GPIO_PORT6, NULL);
	gpio_direction_input(GPIO_PORT6);
	if (gpio_get_value(GPIO_PORT6)) {
		/* CON14 enable */
	} else {
		/* CON8 (SDHI1) enable */
		gpio_request(GPIO_FN_SDHI1_CLK,	NULL);
		gpio_request(GPIO_FN_SDHI1_CMD,	NULL);
		gpio_request(GPIO_FN_SDHI1_D0,	NULL);
		gpio_request(GPIO_FN_SDHI1_D1,	NULL);
		gpio_request(GPIO_FN_SDHI1_D2,	NULL);
		gpio_request(GPIO_FN_SDHI1_D3,	NULL);
		gpio_request(GPIO_FN_SDHI1_CD,	NULL);
		gpio_request(GPIO_FN_SDHI1_WP,	NULL);

		gpio_request(GPIO_PORT16, NULL); /* SDSLOT2_PON */
		gpio_direction_output(GPIO_PORT16, 1);

		platform_device_register(&sdhi1_device);
	}


#ifdef CONFIG_CACHE_L2X0
	/* Early BRESP enable, Shared attribute override enable, 32K*8way */
	l2x0_init(__io(0xf0002000), 0x40440000, 0x82000fff);
#endif

	i2c_register_board_info(0, i2c0_devices, ARRAY_SIZE(i2c0_devices));

	r8a7740_add_standard_devices();

	platform_add_devices(eva_devices,
			     ARRAY_SIZE(eva_devices));
}

static void __init eva_earlytimer_init(void)
{
	r8a7740_clock_init(MD_CK0 | MD_CK2);
	shmobile_earlytimer_init();
}

static void __init eva_add_early_devices(void)
{
	r8a7740_add_early_devices();

	/* override timer setup with board-specific code */
	shmobile_timer.init = eva_earlytimer_init;
}

static const char *eva_boards_compat_dt[] __initdata = {
	"renesas,armadillo800eva",
	NULL,
};

DT_MACHINE_START(ARMADILLO800EVA_DT, "armadillo800eva")
	.map_io		= r8a7740_map_io,
	.init_early	= eva_add_early_devices,
	.init_irq	= r8a7740_init_irq,
	.handle_irq	= shmobile_handle_irq_intc,
	.init_machine	= eva_init,
	.init_late	= shmobile_init_late,
	.timer		= &shmobile_timer,
	.dt_compat	= eva_boards_compat_dt,
MACHINE_END
