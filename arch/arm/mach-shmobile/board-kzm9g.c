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
#include <linux/irqchip/arm-gic.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/mfd/tmio.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_device.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/smsc911x.h>
#include <linux/usb/r8a66597.h>
#include <linux/usb/renesas_usbhs.h>
#include <linux/videodev2.h>
#include <sound/sh_fsi.h>
#include <sound/simple_card.h>
#include <mach/irqs.h>
#include <mach/sh73a0.h>
#include <mach/common.h>
#include <asm/hardware/cache-l2x0.h>
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

/* Dummy supplies, where voltage doesn't matter */
static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x.0"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x.0"),
};

/*
 * FSI-AK4648
 *
 * this command is required when playback.
 *
 * # amixer set "LINEOUT Mixer DACL" on
 */

/* SMSC 9221 */
static struct resource smsc9221_resources[] = {
	[0] = {
		.start	= 0x10000000, /* CS4 */
		.end	= 0x100000ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= irq_pin(3), /* IRQ3 */
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
		.start	= irq_pin(1), /* IRQ1 */
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

/* USB Func CN17 */
struct usbhs_private {
	void __iomem *phy;
	void __iomem *cr2;
	struct renesas_usbhs_platform_info info;
};

#define IRQ15			irq_pin(15)
#define USB_PHY_MODE		(1 << 4)
#define USB_PHY_INT_EN		((1 << 3) | (1 << 2))
#define USB_PHY_ON		(1 << 1)
#define USB_PHY_OFF		(1 << 0)
#define USB_PHY_INT_CLR		(USB_PHY_ON | USB_PHY_OFF)

#define usbhs_get_priv(pdev) \
	container_of(renesas_usbhs_get_info(pdev), struct usbhs_private, info)

static int usbhs_get_vbus(struct platform_device *pdev)
{
	struct usbhs_private *priv = usbhs_get_priv(pdev);

	return !((1 << 7) & __raw_readw(priv->cr2));
}

static int usbhs_phy_reset(struct platform_device *pdev)
{
	struct usbhs_private *priv = usbhs_get_priv(pdev);

	/* init phy */
	__raw_writew(0x8a0a, priv->cr2);

	return 0;
}

static int usbhs_get_id(struct platform_device *pdev)
{
	return USBHS_GADGET;
}

static irqreturn_t usbhs_interrupt(int irq, void *data)
{
	struct platform_device *pdev = data;
	struct usbhs_private *priv = usbhs_get_priv(pdev);

	renesas_usbhs_call_notify_hotplug(pdev);

	/* clear status */
	__raw_writew(__raw_readw(priv->phy) | USB_PHY_INT_CLR, priv->phy);

	return IRQ_HANDLED;
}

static int usbhs_hardware_init(struct platform_device *pdev)
{
	struct usbhs_private *priv = usbhs_get_priv(pdev);
	int ret;

	/* clear interrupt status */
	__raw_writew(USB_PHY_MODE | USB_PHY_INT_CLR, priv->phy);

	ret = request_irq(IRQ15, usbhs_interrupt, IRQF_TRIGGER_HIGH,
			  dev_name(&pdev->dev), pdev);
	if (ret) {
		dev_err(&pdev->dev, "request_irq err\n");
		return ret;
	}

	/* enable USB phy interrupt */
	__raw_writew(USB_PHY_MODE | USB_PHY_INT_EN, priv->phy);

	return 0;
}

static int usbhs_hardware_exit(struct platform_device *pdev)
{
	struct usbhs_private *priv = usbhs_get_priv(pdev);

	/* clear interrupt status */
	__raw_writew(USB_PHY_MODE | USB_PHY_INT_CLR, priv->phy);

	free_irq(IRQ15, pdev);

	return 0;
}

static u32 usbhs_pipe_cfg[] = {
	USB_ENDPOINT_XFER_CONTROL,
	USB_ENDPOINT_XFER_ISOC,
	USB_ENDPOINT_XFER_ISOC,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_INT,
	USB_ENDPOINT_XFER_INT,
	USB_ENDPOINT_XFER_INT,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_BULK,
	USB_ENDPOINT_XFER_BULK,
};

static struct usbhs_private usbhs_private = {
	.phy	= IOMEM(0xe60781e0),		/* USBPHYINT */
	.cr2	= IOMEM(0xe605810c),		/* USBCR2 */
	.info = {
		.platform_callback = {
			.hardware_init	= usbhs_hardware_init,
			.hardware_exit	= usbhs_hardware_exit,
			.get_id		= usbhs_get_id,
			.phy_reset	= usbhs_phy_reset,
			.get_vbus	= usbhs_get_vbus,
		},
		.driver_param = {
			.buswait_bwait	= 4,
			.has_otg	= 1,
			.pipe_type	= usbhs_pipe_cfg,
			.pipe_size	= ARRAY_SIZE(usbhs_pipe_cfg),
		},
	},
};

static struct resource usbhs_resources[] = {
	[0] = {
		.start	= 0xE6890000,
		.end	= 0xE68900e6 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(62),
		.end	= gic_spi(62),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device usbhs_device = {
	.name	= "renesas_usbhs",
	.id	= -1,
	.dev = {
		.dma_mask		= NULL,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &usbhs_private.info,
	},
	.num_resources	= ARRAY_SIZE(usbhs_resources),
	.resource	= usbhs_resources,
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

/* Fixed 1.8V regulator to be used by MMCIF */
static struct regulator_consumer_supply fixed1v8_power_consumers[] =
{
	REGULATOR_SUPPLY("vmmc", "sh_mmcif.0"),
	REGULATOR_SUPPLY("vqmmc", "sh_mmcif.0"),
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
		.start	= gic_spi(140),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= gic_spi(141),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct sh_mmcif_plat_data sh_mmcif_platdata = {
	.ocr		= MMC_VDD_165_195,
	.caps		= MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE,
	.slave_id_tx	= SHDMA_SLAVE_MMCIF_TX,
	.slave_id_rx	= SHDMA_SLAVE_MMCIF_RX,
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

/* Fixed 3.3V regulators to be used by SDHI0 */
static struct regulator_consumer_supply vcc_sdhi0_consumers[] =
{
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
	.gpio = 15,
	.enable_high = 1,
	.init_data = &vcc_sdhi0_init_data,
};

static struct platform_device vcc_sdhi0 = {
	.name = "reg-fixed-voltage",
	.id   = 0,
	.dev  = {
		.platform_data = &vcc_sdhi0_info,
	},
};

/* Fixed 3.3V regulators to be used by SDHI2 */
static struct regulator_consumer_supply vcc_sdhi2_consumers[] =
{
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.2"),
};

static struct regulator_init_data vcc_sdhi2_init_data = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = ARRAY_SIZE(vcc_sdhi2_consumers),
	.consumer_supplies      = vcc_sdhi2_consumers,
};

static struct fixed_voltage_config vcc_sdhi2_info = {
	.supply_name = "SDHI2 Vcc",
	.microvolts = 3300000,
	.gpio = 14,
	.enable_high = 1,
	.init_data = &vcc_sdhi2_init_data,
};

static struct platform_device vcc_sdhi2 = {
	.name = "reg-fixed-voltage",
	.id   = 1,
	.dev  = {
		.platform_data = &vcc_sdhi2_info,
	},
};

/* SDHI */
static struct sh_mobile_sdhi_info sdhi0_info = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI0_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI0_RX,
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
			  MMC_CAP_POWER_OFF_CARD,
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

/* Micro SD */
static struct sh_mobile_sdhi_info sdhi2_info = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI2_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI2_RX,
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT |
			  TMIO_MMC_USE_GPIO_CD |
			  TMIO_MMC_WRPROTECT_DISABLE,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_POWER_OFF_CARD,
	.cd_gpio	= 13,
};

static struct resource sdhi2_resources[] = {
	[0] = {
		.name	= "SDHI2",
		.start	= 0xee140000,
		.end	= 0xee1400ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= SH_MOBILE_SDHI_IRQ_CARD_DETECT,
		.start	= gic_spi(103),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.name	= SH_MOBILE_SDHI_IRQ_SDCARD,
		.start	= gic_spi(104),
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.name	= SH_MOBILE_SDHI_IRQ_SDIO,
		.start	= gic_spi(105),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi2_device = {
	.name		= "sh_mobile_sdhi",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(sdhi2_resources),
	.resource	= sdhi2_resources,
	.dev	= {
		.platform_data	= &sdhi2_info,
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
};

static struct platform_device gpio_keys_device = {
	.name	= "gpio-keys",
	.dev	= {
		.platform_data  = &gpio_key_info,
	},
};

/* FSI-AK4648 */
static struct sh_fsi_platform_info fsi_info = {
	.port_a = {
		.tx_id = SHDMA_SLAVE_FSI2A_TX,
	},
};

static struct resource fsi_resources[] = {
	[0] = {
		.name	= "FSI",
		.start	= 0xEC230000,
		.end	= 0xEC230400 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = gic_spi(146),
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

static struct asoc_simple_card_info fsi2_ak4648_info = {
	.name		= "AK4648",
	.card		= "FSI2A-AK4648",
	.codec		= "ak4642-codec.0-0012",
	.platform	= "sh_fsi2",
	.daifmt		= SND_SOC_DAIFMT_LEFT_J,
	.cpu_dai = {
		.name	= "fsia-dai",
		.fmt	= SND_SOC_DAIFMT_CBS_CFS,
	},
	.codec_dai = {
		.name	= "ak4642-hifi",
		.fmt	= SND_SOC_DAIFMT_CBM_CFM,
		.sysclk	= 11289600,
	},
};

static struct platform_device fsi_ak4648_device = {
	.name	= "asoc-simple-card",
	.dev	= {
		.platform_data	= &fsi2_ak4648_info,
	},
};

/* I2C */
static struct pcf857x_platform_data pcf8575_pdata = {
	.gpio_base	= GPIO_PCF8575_BASE,
};

static struct i2c_board_info i2c0_devices[] = {
	{
		I2C_BOARD_INFO("ak4648", 0x12),
	},
	{
		I2C_BOARD_INFO("r2025sd", 0x32),
	},
	{
		I2C_BOARD_INFO("ak8975", 0x0c),
		.irq = irq_pin(28), /* IRQ28 */
	},
	{
		I2C_BOARD_INFO("adxl34x", 0x1d),
		.irq = irq_pin(26), /* IRQ26 */
	},
};

static struct i2c_board_info i2c1_devices[] = {
	{
		I2C_BOARD_INFO("st1232-ts", 0x55),
		.irq = irq_pin(8), /* IRQ8 */
	},
};

static struct i2c_board_info i2c3_devices[] = {
	{
		I2C_BOARD_INFO("pcf8575", 0x20),
		.irq = irq_pin(19), /* IRQ19 */
		.platform_data = &pcf8575_pdata,
	},
};

static struct platform_device *kzm_devices[] __initdata = {
	&smsc_device,
	&usb_host_device,
	&usbhs_device,
	&lcdc_device,
	&mmc_device,
	&vcc_sdhi0,
	&vcc_sdhi2,
	&sdhi0_device,
	&sdhi2_device,
	&gpio_keys_device,
	&fsi_device,
	&fsi_ak4648_device,
};

static unsigned long pin_pullup_conf[] = {
	PIN_CONF_PACKED(PIN_CONFIG_BIAS_PULL_UP, 0),
};

static const struct pinctrl_map kzm_pinctrl_map[] = {
	/* FSIA (AK4648) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_fsi2.0", "pfc-sh73a0",
				  "fsia_mclk_in", "fsia"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_fsi2.0", "pfc-sh73a0",
				  "fsia_sclk_in", "fsia"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_fsi2.0", "pfc-sh73a0",
				  "fsia_data_in", "fsia"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_fsi2.0", "pfc-sh73a0",
				  "fsia_data_out", "fsia"),
	/* I2C3 */
	PIN_MAP_MUX_GROUP_DEFAULT("i2c-sh_mobile.3", "pfc-sh73a0",
				  "i2c3_1", "i2c3"),
	/* LCD */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_lcdc_fb.0", "pfc-sh73a0",
				  "lcd_data24", "lcd"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_lcdc_fb.0", "pfc-sh73a0",
				  "lcd_sync", "lcd"),
	/* MMCIF */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.0", "pfc-sh73a0",
				  "mmc0_data8_0", "mmc0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.0", "pfc-sh73a0",
				  "mmc0_ctrl_0", "mmc0"),
	PIN_MAP_CONFIGS_PIN_DEFAULT("sh_mmcif.0", "pfc-sh73a0",
				    "PORT279", pin_pullup_conf),
	PIN_MAP_CONFIGS_GROUP_DEFAULT("sh_mmcif.0", "pfc-sh73a0",
				      "mmc0_data8_0", pin_pullup_conf),
	/* SCIFA4 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.4", "pfc-sh73a0",
				  "scifa4_data", "scifa4"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.4", "pfc-sh73a0",
				  "scifa4_ctrl", "scifa4"),
	/* SDHI0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-sh73a0",
				  "sdhi0_data4", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-sh73a0",
				  "sdhi0_ctrl", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-sh73a0",
				  "sdhi0_cd", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-sh73a0",
				  "sdhi0_wp", "sdhi0"),
	/* SDHI2 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.2", "pfc-sh73a0",
				  "sdhi2_data4", "sdhi2"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.2", "pfc-sh73a0",
				  "sdhi2_ctrl", "sdhi2"),
	/* SMSC */
	PIN_MAP_MUX_GROUP_DEFAULT("smsc911x.0", "pfc-sh73a0",
				  "bsc_cs4", "bsc"),
	/* USB */
	PIN_MAP_MUX_GROUP_DEFAULT("renesas_usbhs", "pfc-sh73a0",
				  "usb_vbus", "usb"),
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

	if (!of_machine_is_compatible("renesas,kzm9g"))
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
	regulator_register_always_on(2, "fixed-1.8V", fixed1v8_power_consumers,
				     ARRAY_SIZE(fixed1v8_power_consumers), 1800000);
	regulator_register_fixed(3, dummy_supplies, ARRAY_SIZE(dummy_supplies));

	pinctrl_register_mappings(kzm_pinctrl_map, ARRAY_SIZE(kzm_pinctrl_map));

	sh73a0_pinmux_init();

	/* SMSC */
	gpio_request_one(224, GPIOF_IN, NULL); /* IRQ3 */

	/* LCDC */
	gpio_request_one(222, GPIOF_OUT_INIT_HIGH, NULL); /* LCDCDON */
	gpio_request_one(226, GPIOF_OUT_INIT_HIGH, NULL); /* SC */

	/* Touchscreen */
	gpio_request_one(223, GPIOF_IN, NULL); /* IRQ8 */

	/* enable SD */
	gpio_request(GPIO_FN_SDHI0_VCCQ_MC0_ON,	NULL);

#ifdef CONFIG_CACHE_L2X0
	/* Early BRESP enable, Shared attribute override enable, 64K*8way */
	l2x0_init(IOMEM(0xf0100000), 0x40460000, 0x82000fff);
#endif

	i2c_register_board_info(0, i2c0_devices, ARRAY_SIZE(i2c0_devices));
	i2c_register_board_info(1, i2c1_devices, ARRAY_SIZE(i2c1_devices));
	i2c_register_board_info(3, i2c3_devices, ARRAY_SIZE(i2c3_devices));

	sh73a0_add_standard_devices();
	platform_add_devices(kzm_devices, ARRAY_SIZE(kzm_devices));

	sh73a0_pm_init();
}

static void kzm9g_restart(char mode, const char *cmd)
{
#define RESCNT2 IOMEM(0xe6188020)
	/* Do soft power on reset */
	writel((1 << 31), RESCNT2);
}

static const char *kzm9g_boards_compat_dt[] __initdata = {
	"renesas,kzm9g",
	NULL,
};

DT_MACHINE_START(KZM9G_DT, "kzm9g")
	.smp		= smp_ops(sh73a0_smp_ops),
	.map_io		= sh73a0_map_io,
	.init_early	= sh73a0_add_early_devices,
	.nr_irqs	= NR_IRQS_LEGACY,
	.init_irq	= sh73a0_init_irq,
	.init_machine	= kzm_init,
	.init_late	= shmobile_init_late,
	.init_time	= sh73a0_earlytimer_init,
	.restart	= kzm9g_restart,
	.dt_compat	= kzm9g_boards_compat_dt,
MACHINE_END
