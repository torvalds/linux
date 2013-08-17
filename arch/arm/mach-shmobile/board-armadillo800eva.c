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
#include <linux/platform_data/st1232_pdata.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/regulator/driver.h>
#include <linux/pinctrl/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/gpio-regulator.h>
#include <linux/regulator/machine.h>
#include <linux/sh_eth.h>
#include <linux/videodev2.h>
#include <linux/usb/renesas_usbhs.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/i2c-gpio.h>
#include <linux/reboot.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a7740.h>
#include <media/mt9t112.h>
#include <media/sh_mobile_ceu.h>
#include <media/soc_camera.h>
#include <asm/page.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/hardware/cache-l2x0.h>
#include <video/sh_mobile_lcdc.h>
#include <video/sh_mobile_hdmi.h>
#include <sound/sh_fsi.h>
#include <sound/simple_card.h>

#include "sh-gpio.h"

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
 * FSI-WM8978
 *
 * this command is required when playback.
 *
 * # amixer set "Headphone" 50
 *
 * this command is required when capture.
 *
 * # amixer set "Input PGA" 15
 * # amixer set "Left Input Mixer MicP" on
 * # amixer set "Left Input Mixer MicN" on
 * # amixer set "Right Input Mixer MicN" on
 * # amixer set "Right Input Mixer MicP" on
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
 */
#define IRQ7		irq_pin(7)
#define USBCR1		IOMEM(0xe605810a)
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

static int usbhsf_power_ctrl(struct platform_device *pdev,
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

	return 0;
}

static int usbhsf_get_vbus(struct platform_device *pdev)
{
	return gpio_get_value(209);
}

static irqreturn_t usbhsf_interrupt(int irq, void *data)
{
	struct platform_device *pdev = data;

	renesas_usbhs_call_notify_hotplug(pdev);

	return IRQ_HANDLED;
}

static int usbhsf_hardware_exit(struct platform_device *pdev)
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

	free_irq(IRQ7, pdev);

	return 0;
}

static int usbhsf_hardware_init(struct platform_device *pdev)
{
	struct usbhsf_private *priv = usbhsf_get_priv(pdev);
	int ret;

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

	ret = request_irq(IRQ7, usbhsf_interrupt, IRQF_TRIGGER_NONE,
			  dev_name(&pdev->dev), pdev);
	if (ret) {
		dev_err(&pdev->dev, "request_irq err\n");
		return ret;
	}
	irq_set_irq_type(IRQ7, IRQ_TYPE_EDGE_BOTH);

	/* usb24 use 1/1 of parent clock (= usb24s = 24MHz) */
	clk_set_rate(priv->usb24,
		     clk_get_rate(clk_get_parent(priv->usb24)));

	return 0;
}

static struct usbhsf_private usbhsf_private = {
	.info = {
		.platform_callback = {
			.get_id		= usbhsf_get_id,
			.get_vbus	= usbhsf_get_vbus,
			.hardware_init	= usbhsf_hardware_init,
			.hardware_exit	= usbhsf_hardware_exit,
			.power_ctrl	= usbhsf_power_ctrl,
		},
		.driver_param = {
			.buswait_bwait		= 5,
			.detection_delay	= 5,
			.d0_rx_id	= SHDMA_SLAVE_USBHS_RX,
			.d1_tx_id	= SHDMA_SLAVE_USBHS_TX,
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
		.start	= gic_spi(51),
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
		.start	= gic_spi(110),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sh_eth_device = {
	.name = "r8a7740-gether",
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
		.start	= gic_spi(177),
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

/*
 * LCDC1/HDMI
 */
static struct sh_mobile_hdmi_info hdmi_info = {
	.flags		= HDMI_OUTPUT_PUSH_PULL |
			  HDMI_OUTPUT_POLARITY_HI |
			  HDMI_32BIT_REG |
			  HDMI_HAS_HTOP1 |
			  HDMI_SND_SRC_SPDIF,
};

static struct resource hdmi_resources[] = {
	[0] = {
		.name	= "HDMI",
		.start	= 0xe6be0000,
		.end	= 0xe6be03ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(131),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.name	= "HDMI emma3pf",
		.start	= 0xe6be4000,
		.end	= 0xe6be43ff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device hdmi_device = {
	.name		= "sh-mobile-hdmi",
	.num_resources	= ARRAY_SIZE(hdmi_resources),
	.resource	= hdmi_resources,
	.id             = -1,
	.dev	= {
		.platform_data	= &hdmi_info,
	},
};

static const struct fb_videomode lcdc1_mode = {
	.name		= "HDMI 720p",
	.xres		= 1280,
	.yres		= 720,
	.pixclock	= 13468,
	.left_margin	= 220,
	.right_margin	= 110,
	.hsync_len	= 40,
	.upper_margin	= 20,
	.lower_margin	= 5,
	.vsync_len	= 5,
	.refresh	= 60,
	.sync		= FB_SYNC_VERT_HIGH_ACT | FB_SYNC_HOR_HIGH_ACT,
};

static struct sh_mobile_lcdc_info hdmi_lcdc_info = {
	.clock_source	= LCDC_CLK_PERIPHERAL, /* HDMI clock */
	.ch[0] = {
		.chan			= LCDC_CHAN_MAINLCD,
		.fourcc			= V4L2_PIX_FMT_RGB565,
		.interface_type		= RGB24,
		.clock_divider		= 1,
		.flags			= LCDC_FLAGS_DWPOL,
		.lcd_modes		= &lcdc1_mode,
		.num_modes		= 1,
		.tx_dev			= &hdmi_device,
		.panel_cfg = {
			.width	= 1280,
			.height = 720,
		},
	},
};

static struct resource hdmi_lcdc_resources[] = {
	[0] = {
		.name	= "LCDC1",
		.start	= 0xfe944000,
		.end	= 0xfe948000 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(178),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device hdmi_lcdc_device = {
	.name		= "sh_mobile_lcdc_fb",
	.num_resources	= ARRAY_SIZE(hdmi_lcdc_resources),
	.resource	= hdmi_lcdc_resources,
	.id		= 1,
	.dev	= {
		.platform_data	= &hdmi_lcdc_info,
		.coherent_dma_mask = ~0,
	},
};

/* GPIO KEY */
#define GPIO_KEY(c, g, d, ...) \
	{ .code = c, .gpio = g, .desc = d, .active_low = 1, __VA_ARGS__ }

static struct gpio_keys_button gpio_buttons[] = {
	GPIO_KEY(KEY_POWER,	99,	"SW3", .wakeup = 1),
	GPIO_KEY(KEY_BACK,	100,	"SW4"),
	GPIO_KEY(KEY_MENU,	97,	"SW5"),
	GPIO_KEY(KEY_HOME,	98,	"SW6"),
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

/* Fixed 3.3V regulator to be used by SDHI1, MMCIF */
static struct regulator_consumer_supply fixed3v3_power_consumers[] = {
	REGULATOR_SUPPLY("vmmc", "sh_mmcif"),
	REGULATOR_SUPPLY("vqmmc", "sh_mmcif"),
};

/* Fixed 3.3V regulator to be used by SDHI0 */
static struct regulator_consumer_supply vcc_sdhi0_consumers[] = {
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.0"),
};

static struct regulator_init_data vcc_sdhi0_init_data = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = ARRAY_SIZE(vcc_sdhi0_consumers),
	.consumer_supplies      = vcc_sdhi0_consumers,
};

static struct fixed_voltage_config vcc_sdhi0_info = {
	.supply_name = "SDHI0 Vcc",
	.microvolts = 3300000,
	.gpio = 75,
	.enable_high = 1,
	.init_data = &vcc_sdhi0_init_data,
};

static struct platform_device vcc_sdhi0 = {
	.name = "reg-fixed-voltage",
	.id   = 1,
	.dev  = {
		.platform_data = &vcc_sdhi0_info,
	},
};

/* 1.8 / 3.3V SDHI0 VccQ regulator */
static struct regulator_consumer_supply vccq_sdhi0_consumers[] = {
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.0"),
};

static struct regulator_init_data vccq_sdhi0_init_data = {
	.constraints = {
		.input_uV	= 3300000,
		.min_uV		= 1800000,
		.max_uV         = 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = ARRAY_SIZE(vccq_sdhi0_consumers),
	.consumer_supplies      = vccq_sdhi0_consumers,
};

static struct gpio vccq_sdhi0_gpios[] = {
	{17, GPIOF_OUT_INIT_LOW, "vccq-sdhi0" },
};

static struct gpio_regulator_state vccq_sdhi0_states[] = {
	{ .value = 3300000, .gpios = (0 << 0) },
	{ .value = 1800000, .gpios = (1 << 0) },
};

static struct gpio_regulator_config vccq_sdhi0_info = {
	.supply_name = "vqmmc",

	.enable_gpio = 74,
	.enable_high = 1,
	.enabled_at_boot = 0,

	.gpios = vccq_sdhi0_gpios,
	.nr_gpios = ARRAY_SIZE(vccq_sdhi0_gpios),

	.states = vccq_sdhi0_states,
	.nr_states = ARRAY_SIZE(vccq_sdhi0_states),

	.type = REGULATOR_VOLTAGE,
	.init_data = &vccq_sdhi0_init_data,
};

static struct platform_device vccq_sdhi0 = {
	.name = "gpio-regulator",
	.id   = -1,
	.dev  = {
		.platform_data = &vccq_sdhi0_info,
	},
};

/* Fixed 3.3V regulator to be used by SDHI1 */
static struct regulator_consumer_supply vcc_sdhi1_consumers[] = {
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.1"),
};

static struct regulator_init_data vcc_sdhi1_init_data = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = ARRAY_SIZE(vcc_sdhi1_consumers),
	.consumer_supplies      = vcc_sdhi1_consumers,
};

static struct fixed_voltage_config vcc_sdhi1_info = {
	.supply_name = "SDHI1 Vcc",
	.microvolts = 3300000,
	.gpio = 16,
	.enable_high = 1,
	.init_data = &vcc_sdhi1_init_data,
};

static struct platform_device vcc_sdhi1 = {
	.name = "reg-fixed-voltage",
	.id   = 2,
	.dev  = {
		.platform_data = &vcc_sdhi1_info,
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
#define IRQ31	irq_pin(31)
static struct sh_mobile_sdhi_info sdhi0_info = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI0_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI0_RX,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
			  MMC_CAP_POWER_OFF_CARD,
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_USE_GPIO_CD,
	.cd_gpio	= 167,
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
		.start	= gic_spi(118),
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= SH_MOBILE_SDHI_IRQ_SDIO,
		.start	= gic_spi(119),
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
	.dma_slave_tx	= SHDMA_SLAVE_SDHI1_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI1_RX,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
			  MMC_CAP_POWER_OFF_CARD,
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_USE_GPIO_CD,
	/* Port72 cannot generate IRQs, will be used in polling mode. */
	.cd_gpio	= 72,
};

static struct resource sdhi1_resources[] = {
	[0] = {
		.name	= "SDHI1",
		.start	= 0xe6860000,
		.end	= 0xe6860100 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(121),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= gic_spi(122),
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.start	= gic_spi(123),
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

static const struct pinctrl_map eva_sdhi1_pinctrl_map[] = {
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-r8a7740",
				  "sdhi1_data4", "sdhi1"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-r8a7740",
				  "sdhi1_ctrl", "sdhi1"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-r8a7740",
				  "sdhi1_cd", "sdhi1"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-r8a7740",
				  "sdhi1_wp", "sdhi1"),
};

/* MMCIF */
static struct sh_mmcif_plat_data sh_mmcif_plat = {
	.sup_pclk	= 0,
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
		.start	= gic_spi(56),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* MMC NOR */
		.start	= gic_spi(57),
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

/* Camera */
static int mt9t111_power(struct device *dev, int mode)
{
	struct clk *mclk = clk_get(NULL, "video1");

	if (IS_ERR(mclk)) {
		dev_err(dev, "can't get video1 clock\n");
		return -EINVAL;
	}

	if (mode) {
		/* video1 (= CON1 camera) expect 24MHz */
		clk_set_rate(mclk, clk_round_rate(mclk, 24000000));
		clk_enable(mclk);
		gpio_set_value(158, 1);
	} else {
		gpio_set_value(158, 0);
		clk_disable(mclk);
	}

	clk_put(mclk);

	return 0;
}

static struct i2c_board_info i2c_camera_mt9t111 = {
	I2C_BOARD_INFO("mt9t112", 0x3d),
};

static struct mt9t112_camera_info mt9t111_info = {
	.divider = { 16, 0, 0, 7, 0, 10, 14, 7, 7 },
};

static struct soc_camera_link mt9t111_link = {
	.i2c_adapter_id	= 0,
	.bus_id		= 0,
	.board_info	= &i2c_camera_mt9t111,
	.power		= mt9t111_power,
	.priv		= &mt9t111_info,
};

static struct platform_device camera_device = {
	.name	= "soc-camera-pdrv",
	.id	= 0,
	.dev	= {
		.platform_data = &mt9t111_link,
	},
};

/* CEU0 */
static struct sh_mobile_ceu_info sh_mobile_ceu0_info = {
	.flags = SH_CEU_FLAG_LOWER_8BIT,
};

static struct resource ceu0_resources[] = {
	[0] = {
		.name	= "CEU",
		.start	= 0xfe910000,
		.end	= 0xfe91009f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = gic_spi(160),
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		/* place holder for contiguous memory */
	},
};

static struct platform_device ceu0_device = {
	.name		= "sh_mobile_ceu",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(ceu0_resources),
	.resource	= ceu0_resources,
	.dev	= {
		.platform_data		= &sh_mobile_ceu0_info,
		.coherent_dma_mask	= 0xffffffff,
	},
};

/* FSI */
static struct sh_fsi_platform_info fsi_info = {
	/* FSI-WM8978 */
	.port_a = {
		.tx_id = SHDMA_SLAVE_FSIA_TX,
	},
	/* FSI-HDMI */
	.port_b = {
		.flags		= SH_FSI_FMT_SPDIF |
				  SH_FSI_ENABLE_STREAM_MODE |
				  SH_FSI_CLK_CPG,
		.tx_id		= SHDMA_SLAVE_FSIB_TX,
	}
};

static struct resource fsi_resources[] = {
	[0] = {
		.name	= "FSI",
		.start	= 0xfe1f0000,
		.end	= 0xfe1f8400 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = gic_spi(9),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device fsi_device = {
	.name		= "sh_fsi2",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(fsi_resources),
	.resource	= fsi_resources,
	.dev	= {
		.platform_data	= &fsi_info,
	},
};

/* FSI-WM8978 */
static struct asoc_simple_card_info fsi_wm8978_info = {
	.name		= "wm8978",
	.card		= "FSI2A-WM8978",
	.codec		= "wm8978.0-001a",
	.platform	= "sh_fsi2",
	.daifmt		= SND_SOC_DAIFMT_I2S,
	.cpu_dai = {
		.name	= "fsia-dai",
		.fmt	= SND_SOC_DAIFMT_CBS_CFS | SND_SOC_DAIFMT_IB_NF,
	},
	.codec_dai = {
		.name	= "wm8978-hifi",
		.fmt	= SND_SOC_DAIFMT_CBM_CFM | SND_SOC_DAIFMT_NB_NF,
		.sysclk	= 12288000,
	},
};

static struct platform_device fsi_wm8978_device = {
	.name	= "asoc-simple-card",
	.id	= 0,
	.dev	= {
		.platform_data	= &fsi_wm8978_info,
	},
};

/* FSI-HDMI */
static struct asoc_simple_card_info fsi2_hdmi_info = {
	.name		= "HDMI",
	.card		= "FSI2B-HDMI",
	.codec		= "sh-mobile-hdmi",
	.platform	= "sh_fsi2",
	.cpu_dai = {
		.name	= "fsib-dai",
		.fmt	= SND_SOC_DAIFMT_CBM_CFM,
	},
	.codec_dai = {
		.name = "sh_mobile_hdmi-hifi",
	},
};

static struct platform_device fsi_hdmi_device = {
	.name	= "asoc-simple-card",
	.id	= 1,
	.dev	= {
		.platform_data	= &fsi2_hdmi_info,
	},
};

/* RTC: RTC connects i2c-gpio. */
static struct i2c_gpio_platform_data i2c_gpio_data = {
	.sda_pin	= 208,
	.scl_pin	= 91,
	.udelay		= 5, /* 100 kHz */
};

static struct platform_device i2c_gpio_device = {
	.name = "i2c-gpio",
	.id = 2,
	.dev = {
		.platform_data = &i2c_gpio_data,
	},
};

/* I2C */
static struct st1232_pdata st1232_i2c0_pdata = {
	.reset_gpio = 166,
};

static struct i2c_board_info i2c0_devices[] = {
	{
		I2C_BOARD_INFO("st1232-ts", 0x55),
		.irq = irq_pin(10),
		.platform_data = &st1232_i2c0_pdata,
	},
	{
		I2C_BOARD_INFO("wm8978", 0x1a),
	},
};

static struct i2c_board_info i2c2_devices[] = {
	{
		I2C_BOARD_INFO("s35390a", 0x30),
		.type = "s35390a",
	},
};

/*
 * board devices
 */
static struct platform_device *eva_devices[] __initdata = {
	&lcdc0_device,
	&gpio_keys_device,
	&sh_eth_device,
	&vcc_sdhi0,
	&vccq_sdhi0,
	&sdhi0_device,
	&sh_mmcif_device,
	&hdmi_device,
	&hdmi_lcdc_device,
	&camera_device,
	&ceu0_device,
	&fsi_device,
	&fsi_wm8978_device,
	&fsi_hdmi_device,
	&i2c_gpio_device,
};

static const struct pinctrl_map eva_pinctrl_map[] = {
	/* CEU0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_ceu.0", "pfc-r8a7740",
				  "ceu0_data_0_7", "ceu0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_ceu.0", "pfc-r8a7740",
				  "ceu0_clk_0", "ceu0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_ceu.0", "pfc-r8a7740",
				  "ceu0_sync", "ceu0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_ceu.0", "pfc-r8a7740",
				  "ceu0_field", "ceu0"),
	/* FSIA */
	PIN_MAP_MUX_GROUP_DEFAULT("asoc-simple-card.0", "pfc-r8a7740",
				  "fsia_sclk_in", "fsia"),
	PIN_MAP_MUX_GROUP_DEFAULT("asoc-simple-card.0", "pfc-r8a7740",
				  "fsia_mclk_out", "fsia"),
	PIN_MAP_MUX_GROUP_DEFAULT("asoc-simple-card.0", "pfc-r8a7740",
				  "fsia_data_in_1", "fsia"),
	PIN_MAP_MUX_GROUP_DEFAULT("asoc-simple-card.0", "pfc-r8a7740",
				  "fsia_data_out_0", "fsia"),
	/* FSIB */
	PIN_MAP_MUX_GROUP_DEFAULT("asoc-simple-card.1", "pfc-r8a7740",
				  "fsib_mclk_in", "fsib"),
	/* GETHER */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-eth", "pfc-r8a7740",
				  "gether_mii", "gether"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh-eth", "pfc-r8a7740",
				  "gether_int", "gether"),
	/* HDMI */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-mobile-hdmi", "pfc-r8a7740",
				  "hdmi", "hdmi"),
	/* LCD0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_lcdc_fb.0", "pfc-r8a7740",
				  "lcd0_data24_0", "lcd0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_lcdc_fb.0", "pfc-r8a7740",
				  "lcd0_lclk_1", "lcd0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_lcdc_fb.0", "pfc-r8a7740",
				  "lcd0_sync", "lcd0"),
	/* MMCIF */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.0", "pfc-r8a7740",
				  "mmc0_data8_1", "mmc0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.0", "pfc-r8a7740",
				  "mmc0_ctrl_1", "mmc0"),
	/* SCIFA1 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.1", "pfc-r8a7740",
				  "scifa1_data", "scifa1"),
	/* SDHI0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7740",
				  "sdhi0_data4", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7740",
				  "sdhi0_ctrl", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7740",
				  "sdhi0_wp", "sdhi0"),
	/* ST1232 */
	PIN_MAP_MUX_GROUP_DEFAULT("0-0055", "pfc-r8a7740",
				  "intc_irq10", "intc"),
	/* USBHS */
	PIN_MAP_MUX_GROUP_DEFAULT("renesas_usbhs", "pfc-r8a7740",
				  "intc_irq7_1", "intc"),
};

static void __init eva_clock_init(void)
{
	struct clk *system	= clk_get(NULL, "system_clk");
	struct clk *xtal1	= clk_get(NULL, "extal1");
	struct clk *usb24s	= clk_get(NULL, "usb24s");
	struct clk *fsibck	= clk_get(NULL, "fsibck");

	if (IS_ERR(system)	||
	    IS_ERR(xtal1)	||
	    IS_ERR(usb24s)	||
	    IS_ERR(fsibck)) {
		pr_err("armadillo800eva board clock init failed\n");
		goto clock_error;
	}

	/* armadillo 800 eva extal1 is 24MHz */
	clk_set_rate(xtal1, 24000000);

	/* usb24s use extal1 (= system) clock (= 24MHz) */
	clk_set_parent(usb24s, system);

	/* FSIBCK is 12.288MHz, and it is parent of FSI-B */
	clk_set_rate(fsibck, 12288000);

clock_error:
	if (!IS_ERR(system))
		clk_put(system);
	if (!IS_ERR(xtal1))
		clk_put(xtal1);
	if (!IS_ERR(usb24s))
		clk_put(usb24s);
	if (!IS_ERR(fsibck))
		clk_put(fsibck);
}

/*
 * board init
 */
#define GPIO_PORT7CR	IOMEM(0xe6050007)
#define GPIO_PORT8CR	IOMEM(0xe6050008)
static void __init eva_init(void)
{
	struct platform_device *usb = NULL;

	regulator_register_always_on(0, "fixed-3.3V", fixed3v3_power_consumers,
				     ARRAY_SIZE(fixed3v3_power_consumers), 3300000);

	pinctrl_register_mappings(eva_pinctrl_map, ARRAY_SIZE(eva_pinctrl_map));

	r8a7740_pinmux_init();
	r8a7740_meram_workaround();

	/* LCDC0 */
	gpio_request_one(61, GPIOF_OUT_INIT_HIGH, NULL); /* LCDDON */
	gpio_request_one(202, GPIOF_OUT_INIT_LOW, NULL); /* LCD0_LED_CONT */

	/* GETHER */
	gpio_request_one(18, GPIOF_OUT_INIT_HIGH, NULL); /* PHY_RST */

	/* USB */
	gpio_request_one(159, GPIOF_IN, NULL); /* USB_DEVICE_MODE */

	if (gpio_get_value(159)) {
		/* USB Host */
	} else {
		/* USB Func */
		/*
		 * The USBHS interrupt handlers needs to read the IRQ pin value
		 * (HI/LOW) to diffentiate USB connection and disconnection
		 * events (usbhsf_get_vbus()). We thus need to select both the
		 * intc_irq7_1 pin group and GPIO 209 here.
		 */
		gpio_request_one(209, GPIOF_IN, NULL);

		platform_device_register(&usbhsf_device);
		usb = &usbhsf_device;
	}

	/* CON1/CON15 Camera */
	gpio_request_one(173, GPIOF_OUT_INIT_LOW, NULL);  /* STANDBY */
	gpio_request_one(172, GPIOF_OUT_INIT_HIGH, NULL); /* RST */
	/* see mt9t111_power() */
	gpio_request_one(158, GPIOF_OUT_INIT_LOW, NULL);  /* CAM_PON */

	/* FSI-WM8978 */
	gpio_request(7, NULL);
	gpio_request(8, NULL);
	gpio_direction_none(GPIO_PORT7CR); /* FSIAOBT needs no direction */
	gpio_direction_none(GPIO_PORT8CR); /* FSIAOLR needs no direction */

	/*
	 * CAUTION
	 *
	 * DBGMD/LCDC0/FSIA MUX
	 * DBGMD_SELECT_B should be set after setting PFC Function.
	 */
	gpio_request_one(176, GPIOF_OUT_INIT_HIGH, NULL);

	/*
	 * We can switch CON8/CON14 by SW1.5,
	 * but it needs after DBGMD_SELECT_B
	 */
	gpio_request_one(6, GPIOF_IN, NULL);
	if (gpio_get_value(6)) {
		/* CON14 enable */
	} else {
		/* CON8 (SDHI1) enable */
		pinctrl_register_mappings(eva_sdhi1_pinctrl_map,
					  ARRAY_SIZE(eva_sdhi1_pinctrl_map));

		platform_device_register(&vcc_sdhi1);
		platform_device_register(&sdhi1_device);
	}


#ifdef CONFIG_CACHE_L2X0
	/* Early BRESP enable, Shared attribute override enable, 32K*8way */
	l2x0_init(IOMEM(0xf0002000), 0x40440000, 0x82000fff);
#endif

	i2c_register_board_info(0, i2c0_devices, ARRAY_SIZE(i2c0_devices));
	i2c_register_board_info(2, i2c2_devices, ARRAY_SIZE(i2c2_devices));

	r8a7740_add_standard_devices();

	platform_add_devices(eva_devices,
			     ARRAY_SIZE(eva_devices));

	rmobile_add_device_to_domain("A4LC", &lcdc0_device);
	rmobile_add_device_to_domain("A4LC", &hdmi_lcdc_device);
	if (usb)
		rmobile_add_device_to_domain("A3SP", usb);

	r8a7740_pm_init();
}

static void __init eva_earlytimer_init(void)
{
	r8a7740_clock_init(MD_CK0 | MD_CK2);
	shmobile_earlytimer_init();

	/* the rate of extal1 clock must be set before late_time_init */
	eva_clock_init();
}

static void __init eva_add_early_devices(void)
{
	r8a7740_add_early_devices();
}

#define RESCNT2 IOMEM(0xe6188020)
static void eva_restart(enum reboot_mode mode, const char *cmd)
{
	/* Do soft power on reset */
	writel((1 << 31), RESCNT2);
}

static const char *eva_boards_compat_dt[] __initdata = {
	"renesas,armadillo800eva",
	NULL,
};

DT_MACHINE_START(ARMADILLO800EVA_DT, "armadillo800eva")
	.map_io		= r8a7740_map_io,
	.init_early	= eva_add_early_devices,
	.init_irq	= r8a7740_init_irq,
	.init_machine	= eva_init,
	.init_late	= shmobile_init_late,
	.init_time	= eva_earlytimer_init,
	.dt_compat	= eva_boards_compat_dt,
	.restart	= eva_restart,
MACHINE_END
