/*
 * Lager board support
 *
 * Copyright (C) 2013-2014  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
 * Copyright (C) 2014  Cogent Embedded, Inc.
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

#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_data/camera-rcar.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_data/rcar-du.h>
#include <linux/platform_data/usb-rcar-gen2-phy.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/gpio-regulator.h>
#include <linux/regulator/machine.h>
#include <linux/sh_eth.h>
#include <linux/usb/phy.h>
#include <linux/usb/renesas_usbhs.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a7790.h>
#include <media/soc_camera.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/mtd.h>
#include <linux/spi/flash.h>
#include <linux/spi/rspi.h>
#include <linux/spi/spi.h>
#include <sound/rcar_snd.h>
#include <sound/simple_card.h>

/*
 * SSI-AK4643
 *
 * SW1: 1: AK4643
 *      2: CN22
 *      3: ADV7511
 *
 * this command is required when playback.
 *
 * # amixer set "LINEOUT Mixer DACL" on
 */

/*
 * SDHI0 (CN8)
 *
 * JP3:  pin1
 * SW20: pin1

 * GP5_24:	1:  VDD  3.3V (defult)
 *		0:  VDD  0.0V
 * GP5_29:	1:  VccQ 3.3V (defult)
 *		0:  VccQ 1.8V
 *
 */

/* DU */
static struct rcar_du_encoder_data lager_du_encoders[] = {
	{
		.type = RCAR_DU_ENCODER_VGA,
		.output = RCAR_DU_OUTPUT_DPAD0,
	}, {
		.type = RCAR_DU_ENCODER_NONE,
		.output = RCAR_DU_OUTPUT_LVDS1,
		.connector.lvds.panel = {
			.width_mm = 210,
			.height_mm = 158,
			.mode = {
				.clock = 65000,
				.hdisplay = 1024,
				.hsync_start = 1048,
				.hsync_end = 1184,
				.htotal = 1344,
				.vdisplay = 768,
				.vsync_start = 771,
				.vsync_end = 777,
				.vtotal = 806,
				.flags = 0,
			},
		},
	},
};

static const struct rcar_du_platform_data lager_du_pdata __initconst = {
	.encoders = lager_du_encoders,
	.num_encoders = ARRAY_SIZE(lager_du_encoders),
};

static const struct resource du_resources[] __initconst = {
	DEFINE_RES_MEM(0xfeb00000, 0x70000),
	DEFINE_RES_MEM_NAMED(0xfeb90000, 0x1c, "lvds.0"),
	DEFINE_RES_MEM_NAMED(0xfeb94000, 0x1c, "lvds.1"),
	DEFINE_RES_IRQ(gic_spi(256)),
	DEFINE_RES_IRQ(gic_spi(268)),
	DEFINE_RES_IRQ(gic_spi(269)),
};

static void __init lager_add_du_device(void)
{
	struct platform_device_info info = {
		.name = "rcar-du-r8a7790",
		.id = -1,
		.res = du_resources,
		.num_res = ARRAY_SIZE(du_resources),
		.data = &lager_du_pdata,
		.size_data = sizeof(lager_du_pdata),
		.dma_mask = DMA_BIT_MASK(32),
	};

	platform_device_register_full(&info);
}

/* LEDS */
static struct gpio_led lager_leds[] = {
	{
		.name		= "led8",
		.gpio		= RCAR_GP_PIN(5, 17),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "led7",
		.gpio		= RCAR_GP_PIN(4, 23),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "led6",
		.gpio		= RCAR_GP_PIN(4, 22),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
};

static const struct gpio_led_platform_data lager_leds_pdata __initconst = {
	.leds		= lager_leds,
	.num_leds	= ARRAY_SIZE(lager_leds),
};

/* GPIO KEY */
#define GPIO_KEY(c, g, d, ...) \
	{ .code = c, .gpio = g, .desc = d, .active_low = 1, \
	  .wakeup = 1, .debounce_interval = 20 }

static struct gpio_keys_button gpio_buttons[] = {
	GPIO_KEY(KEY_4,		RCAR_GP_PIN(1, 28),	"SW2-pin4"),
	GPIO_KEY(KEY_3,		RCAR_GP_PIN(1, 26),	"SW2-pin3"),
	GPIO_KEY(KEY_2,		RCAR_GP_PIN(1, 24),	"SW2-pin2"),
	GPIO_KEY(KEY_1,		RCAR_GP_PIN(1, 14),	"SW2-pin1"),
};

static const struct gpio_keys_platform_data lager_keys_pdata __initconst = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
};

/* Fixed 3.3V regulator to be used by MMCIF */
static struct regulator_consumer_supply fixed3v3_power_consumers[] =
{
	REGULATOR_SUPPLY("vmmc", "sh_mmcif.1"),
};

/*
 * SDHI regulator macro
 *
 ** FIXME**
 * Lager board vqmmc is provided via DA9063 PMIC chip,
 * and we should use ${LINK}/drivers/mfd/da9063-* driver for it.
 * but, it doesn't have regulator support at this point.
 * It uses gpio-regulator for vqmmc as quick-hack.
 */
#define SDHI_REGULATOR(idx, vdd_pin, vccq_pin)				\
static struct regulator_consumer_supply vcc_sdhi##idx##_consumer =	\
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi." #idx);		\
									\
static struct regulator_init_data vcc_sdhi##idx##_init_data = {		\
	.constraints = {						\
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,		\
	},								\
	.consumer_supplies	= &vcc_sdhi##idx##_consumer,		\
	.num_consumer_supplies	= 1,					\
};									\
									\
static const struct fixed_voltage_config vcc_sdhi##idx##_info __initconst = {\
	.supply_name	= "SDHI" #idx "Vcc",				\
	.microvolts	= 3300000,					\
	.gpio		= vdd_pin,					\
	.enable_high	= 1,						\
	.init_data	= &vcc_sdhi##idx##_init_data,			\
};									\
									\
static struct regulator_consumer_supply vccq_sdhi##idx##_consumer =	\
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi." #idx);		\
									\
static struct regulator_init_data vccq_sdhi##idx##_init_data = {	\
	.constraints = {						\
		.input_uV	= 3300000,				\
		.min_uV		= 1800000,				\
		.max_uV		= 3300000,				\
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |		\
				  REGULATOR_CHANGE_STATUS,		\
	},								\
	.consumer_supplies	= &vccq_sdhi##idx##_consumer,		\
	.num_consumer_supplies	= 1,					\
};									\
									\
static struct gpio vccq_sdhi##idx##_gpio =				\
	{ vccq_pin, GPIOF_OUT_INIT_HIGH, "vccq-sdhi" #idx };		\
									\
static struct gpio_regulator_state vccq_sdhi##idx##_states[] = {	\
	{ .value = 1800000, .gpios = 0 },				\
	{ .value = 3300000, .gpios = 1 },				\
};									\
									\
static const struct gpio_regulator_config vccq_sdhi##idx##_info __initconst = {\
	.supply_name	= "vqmmc",					\
	.gpios		= &vccq_sdhi##idx##_gpio,			\
	.nr_gpios	= 1,						\
	.states		= vccq_sdhi##idx##_states,			\
	.nr_states	= ARRAY_SIZE(vccq_sdhi##idx##_states),		\
	.type		= REGULATOR_VOLTAGE,				\
	.init_data	= &vccq_sdhi##idx##_init_data,			\
};

SDHI_REGULATOR(0, RCAR_GP_PIN(5, 24), RCAR_GP_PIN(5, 29));
SDHI_REGULATOR(2, RCAR_GP_PIN(5, 25), RCAR_GP_PIN(5, 30));

/* MMCIF */
static const struct sh_mmcif_plat_data mmcif1_pdata __initconst = {
	.caps		= MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE,
	.clk_ctrl2_present = true,
	.ccs_unsupported = true,
};

static const struct resource mmcif1_resources[] __initconst = {
	DEFINE_RES_MEM(0xee220000, 0x80),
	DEFINE_RES_IRQ(gic_spi(170)),
};

/* Ether */
static const struct sh_eth_plat_data ether_pdata __initconst = {
	.phy			= 0x1,
	.phy_irq		= irq_pin(0),
	.edmac_endian		= EDMAC_LITTLE_ENDIAN,
	.phy_interface		= PHY_INTERFACE_MODE_RMII,
	.ether_link_active_low	= 1,
};

static const struct resource ether_resources[] __initconst = {
	DEFINE_RES_MEM(0xee700000, 0x400),
	DEFINE_RES_IRQ(gic_spi(162)),
};

static const struct platform_device_info ether_info __initconst = {
	.name		= "r8a7790-ether",
	.id		= -1,
	.res		= ether_resources,
	.num_res	= ARRAY_SIZE(ether_resources),
	.data		= &ether_pdata,
	.size_data	= sizeof(ether_pdata),
	.dma_mask	= DMA_BIT_MASK(32),
};

/* SPI Flash memory (Spansion S25FL512SAGMFIG11 64Mb) */
static struct mtd_partition spi_flash_part[] = {
	/* Reserved for user loader program, read-only */
	{
		.name = "loader",
		.offset = 0,
		.size = SZ_256K,
		.mask_flags = MTD_WRITEABLE,
	},
	/* Reserved for user program, read-only */
	{
		.name = "user",
		.offset = MTDPART_OFS_APPEND,
		.size = SZ_4M,
		.mask_flags = MTD_WRITEABLE,
	},
	/* All else is writable (e.g. JFFS2) */
	{
		.name = "flash",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
		.mask_flags = 0,
	},
};

static const struct flash_platform_data spi_flash_data = {
	.name           = "m25p80",
	.parts          = spi_flash_part,
	.nr_parts       = ARRAY_SIZE(spi_flash_part),
	.type           = "s25fl512s",
};

static const struct rspi_plat_data qspi_pdata __initconst = {
	.num_chipselect	= 1,
};

static const struct spi_board_info spi_info[] __initconst = {
	{
		.modalias	= "m25p80",
		.platform_data	= &spi_flash_data,
		.mode		= SPI_MODE_0 | SPI_TX_QUAD | SPI_RX_QUAD,
		.max_speed_hz	= 30000000,
		.bus_num	= 0,
		.chip_select	= 0,
	},
};

/* QSPI resource */
static const struct resource qspi_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6b10000, 0x1000),
	DEFINE_RES_IRQ_NAMED(gic_spi(184), "mux"),
};

/* VIN */
static const struct resource vin_resources[] __initconst = {
	/* VIN0 */
	DEFINE_RES_MEM(0xe6ef0000, 0x1000),
	DEFINE_RES_IRQ(gic_spi(188)),
	/* VIN1 */
	DEFINE_RES_MEM(0xe6ef1000, 0x1000),
	DEFINE_RES_IRQ(gic_spi(189)),
};

static void __init lager_add_vin_device(unsigned idx,
					struct rcar_vin_platform_data *pdata)
{
	struct platform_device_info vin_info = {
		.name		= "r8a7790-vin",
		.id		= idx,
		.res		= &vin_resources[idx * 2],
		.num_res	= 2,
		.dma_mask	= DMA_BIT_MASK(32),
		.data		= pdata,
		.size_data	= sizeof(*pdata),
	};

	BUG_ON(idx > 1);

	platform_device_register_full(&vin_info);
}

#define LAGER_CAMERA(idx, name, addr, pdata, flag)			\
static struct i2c_board_info i2c_cam##idx##_device = {			\
	I2C_BOARD_INFO(name, addr),					\
};									\
									\
static struct rcar_vin_platform_data vin##idx##_pdata = {		\
	.flags = flag,							\
};									\
									\
static struct soc_camera_link cam##idx##_link = {			\
	.bus_id = idx,							\
	.board_info = &i2c_cam##idx##_device,				\
	.i2c_adapter_id = 2,						\
	.module_name = name,						\
	.priv = pdata,							\
}

/* Camera 0 is not currently supported due to adv7612 support missing */
LAGER_CAMERA(1, "adv7180", 0x20, NULL, RCAR_VIN_BT656);

static void __init lager_add_camera1_device(void)
{
	platform_device_register_data(NULL, "soc-camera-pdrv", 1,
				      &cam1_link, sizeof(cam1_link));
	lager_add_vin_device(1, &vin1_pdata);
}

/* SATA1 */
static const struct resource sata1_resources[] __initconst = {
	DEFINE_RES_MEM(0xee500000, 0x2000),
	DEFINE_RES_IRQ(gic_spi(106)),
};

static const struct platform_device_info sata1_info __initconst = {
	.name		= "sata-r8a7790",
	.id		= 1,
	.res		= sata1_resources,
	.num_res	= ARRAY_SIZE(sata1_resources),
	.dma_mask	= DMA_BIT_MASK(32),
};

/* USBHS */
static const struct resource usbhs_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6590000, 0x100),
	DEFINE_RES_IRQ(gic_spi(107)),
};

struct usbhs_private {
	struct renesas_usbhs_platform_info info;
	struct usb_phy *phy;
};

#define usbhs_get_priv(pdev) \
	container_of(renesas_usbhs_get_info(pdev), struct usbhs_private, info)

static int usbhs_power_ctrl(struct platform_device *pdev,
				void __iomem *base, int enable)
{
	struct usbhs_private *priv = usbhs_get_priv(pdev);

	if (!priv->phy)
		return -ENODEV;

	if (enable) {
		int retval = usb_phy_init(priv->phy);

		if (!retval)
			retval = usb_phy_set_suspend(priv->phy, 0);
		return retval;
	}

	usb_phy_set_suspend(priv->phy, 1);
	usb_phy_shutdown(priv->phy);
	return 0;
}

static int usbhs_hardware_init(struct platform_device *pdev)
{
	struct usbhs_private *priv = usbhs_get_priv(pdev);
	struct usb_phy *phy;
	int ret;

	/* USB0 Function - use PWEN as GPIO input to detect DIP Switch SW5
	 * setting to avoid VBUS short circuit due to wrong cable.
	 * PWEN should be pulled up high if USB Function is selected by SW5
	 */
	gpio_request_one(RCAR_GP_PIN(5, 18), GPIOF_IN, NULL); /* USB0_PWEN */
	if (!gpio_get_value(RCAR_GP_PIN(5, 18))) {
		pr_warn("Error: USB Function not selected - check SW5 + SW6\n");
		ret = -ENOTSUPP;
		goto error;
	}

	phy = usb_get_phy_dev(&pdev->dev, 0);
	if (IS_ERR(phy)) {
		ret = PTR_ERR(phy);
		goto error;
	}

	priv->phy = phy;
	return 0;
 error:
	gpio_free(RCAR_GP_PIN(5, 18));
	return ret;
}

static int usbhs_hardware_exit(struct platform_device *pdev)
{
	struct usbhs_private *priv = usbhs_get_priv(pdev);

	if (!priv->phy)
		return 0;

	usb_put_phy(priv->phy);
	priv->phy = NULL;

	gpio_free(RCAR_GP_PIN(5, 18));
	return 0;
}

static int usbhs_get_id(struct platform_device *pdev)
{
	return USBHS_GADGET;
}

static u32 lager_usbhs_pipe_type[] = {
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

static struct usbhs_private usbhs_priv __initdata = {
	.info = {
		.platform_callback = {
			.power_ctrl	= usbhs_power_ctrl,
			.hardware_init	= usbhs_hardware_init,
			.hardware_exit	= usbhs_hardware_exit,
			.get_id		= usbhs_get_id,
		},
		.driver_param = {
			.buswait_bwait	= 4,
			.pipe_type = lager_usbhs_pipe_type,
			.pipe_size = ARRAY_SIZE(lager_usbhs_pipe_type),
		},
	}
};

static void __init lager_register_usbhs(void)
{
	usb_bind_phy("renesas_usbhs", 0, "usb_phy_rcar_gen2");
	platform_device_register_resndata(NULL,
					  "renesas_usbhs", -1,
					  usbhs_resources,
					  ARRAY_SIZE(usbhs_resources),
					  &usbhs_priv.info,
					  sizeof(usbhs_priv.info));
}

/* USBHS PHY */
static const struct rcar_gen2_phy_platform_data usbhs_phy_pdata __initconst = {
	.chan0_pci = 0,	/* Channel 0 is USBHS */
	.chan2_pci = 1,	/* Channel 2 is PCI USB */
};

static const struct resource usbhs_phy_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6590100, 0x100),
};

/* I2C */
static struct i2c_board_info i2c2_devices[] = {
	{
		I2C_BOARD_INFO("ak4643", 0x12),
	}
};

/* Sound */
static struct resource rsnd_resources[] __initdata = {
	[RSND_GEN2_SCU]  = DEFINE_RES_MEM(0xec500000, 0x1000),
	[RSND_GEN2_ADG]  = DEFINE_RES_MEM(0xec5a0000, 0x100),
	[RSND_GEN2_SSIU] = DEFINE_RES_MEM(0xec540000, 0x1000),
	[RSND_GEN2_SSI]  = DEFINE_RES_MEM(0xec541000, 0x1280),
};

static struct rsnd_ssi_platform_info rsnd_ssi[] = {
	RSND_SSI(0, gic_spi(370), 0),
	RSND_SSI(0, gic_spi(371), RSND_SSI_CLK_PIN_SHARE),
};

static struct rsnd_src_platform_info rsnd_src[2] = {
	/* no member at this point */
};

static struct rsnd_dai_platform_info rsnd_dai = {
	.playback = { .ssi = &rsnd_ssi[0], },
	.capture  = { .ssi = &rsnd_ssi[1], },
};

static struct rcar_snd_info rsnd_info = {
	.flags		= RSND_GEN2,
	.ssi_info	= rsnd_ssi,
	.ssi_info_nr	= ARRAY_SIZE(rsnd_ssi),
	.src_info	= rsnd_src,
	.src_info_nr	= ARRAY_SIZE(rsnd_src),
	.dai_info	= &rsnd_dai,
	.dai_info_nr	= 1,
};

static struct asoc_simple_card_info rsnd_card_info = {
	.name		= "AK4643",
	.card		= "SSI01-AK4643",
	.codec		= "ak4642-codec.2-0012",
	.platform	= "rcar_sound",
	.daifmt		= SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_CBM_CFM,
	.cpu_dai = {
		.name	= "rcar_sound",
	},
	.codec_dai = {
		.name	= "ak4642-hifi",
		.sysclk	= 11289600,
	},
};

static void __init lager_add_rsnd_device(void)
{
	struct platform_device_info cardinfo = {
		.name           = "asoc-simple-card",
		.id             = -1,
		.data           = &rsnd_card_info,
		.size_data      = sizeof(struct asoc_simple_card_info),
		.dma_mask       = DMA_BIT_MASK(32),
	};

	i2c_register_board_info(2, i2c2_devices,
				ARRAY_SIZE(i2c2_devices));

	platform_device_register_resndata(
		NULL, "rcar_sound", -1,
		rsnd_resources, ARRAY_SIZE(rsnd_resources),
		&rsnd_info, sizeof(rsnd_info));

	platform_device_register_full(&cardinfo);
}

/* SDHI0 */
static struct sh_mobile_sdhi_info sdhi0_info __initdata = {
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
			  MMC_CAP_POWER_OFF_CARD,
	.tmio_caps2	= MMC_CAP2_NO_MULTI_READ,
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT |
			  TMIO_MMC_WRPROTECT_DISABLE,
};

static struct resource sdhi0_resources[] __initdata = {
	DEFINE_RES_MEM(0xee100000, 0x200),
	DEFINE_RES_IRQ(gic_spi(165)),
};

/* SDHI2 */
static struct sh_mobile_sdhi_info sdhi2_info __initdata = {
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
			  MMC_CAP_POWER_OFF_CARD,
	.tmio_caps2	= MMC_CAP2_NO_MULTI_READ,
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT |
			  TMIO_MMC_WRPROTECT_DISABLE,
};

static struct resource sdhi2_resources[] __initdata = {
	DEFINE_RES_MEM(0xee140000, 0x100),
	DEFINE_RES_IRQ(gic_spi(167)),
};

/* Internal PCI1 */
static const struct resource pci1_resources[] __initconst = {
	DEFINE_RES_MEM(0xee0b0000, 0x10000),	/* CFG */
	DEFINE_RES_MEM(0xee0a0000, 0x10000),	/* MEM */
	DEFINE_RES_IRQ(gic_spi(112)),
};

static const struct platform_device_info pci1_info __initconst = {
	.name		= "pci-rcar-gen2",
	.id		= 1,
	.res		= pci1_resources,
	.num_res	= ARRAY_SIZE(pci1_resources),
	.dma_mask	= DMA_BIT_MASK(32),
};

static void __init lager_add_usb1_device(void)
{
	platform_device_register_full(&pci1_info);
}

/* Internal PCI2 */
static const struct resource pci2_resources[] __initconst = {
	DEFINE_RES_MEM(0xee0d0000, 0x10000),	/* CFG */
	DEFINE_RES_MEM(0xee0c0000, 0x10000),	/* MEM */
	DEFINE_RES_IRQ(gic_spi(113)),
};

static const struct platform_device_info pci2_info __initconst = {
	.name		= "pci-rcar-gen2",
	.id		= 2,
	.res		= pci2_resources,
	.num_res	= ARRAY_SIZE(pci2_resources),
	.dma_mask	= DMA_BIT_MASK(32),
};

static void __init lager_add_usb2_device(void)
{
	platform_device_register_full(&pci2_info);
}

static const struct pinctrl_map lager_pinctrl_map[] = {
	/* DU (CN10: ARGB0, CN13: LVDS) */
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7790", "pfc-r8a7790",
				  "du_rgb666", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7790", "pfc-r8a7790",
				  "du_sync_1", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7790", "pfc-r8a7790",
				  "du_clk_out_0", "du"),
	/* I2C2 */
	PIN_MAP_MUX_GROUP_DEFAULT("i2c-rcar.2", "pfc-r8a7790",
				  "i2c2", "i2c2"),
	/* QSPI */
	PIN_MAP_MUX_GROUP_DEFAULT("qspi.0", "pfc-r8a7790",
				  "qspi_ctrl", "qspi"),
	PIN_MAP_MUX_GROUP_DEFAULT("qspi.0", "pfc-r8a7790",
				  "qspi_data4", "qspi"),
	/* SCIF0 (CN19: DEBUG SERIAL0) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.6", "pfc-r8a7790",
				  "scif0_data", "scif0"),
	/* SCIF1 (CN20: DEBUG SERIAL1) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.7", "pfc-r8a7790",
				  "scif1_data", "scif1"),
	/* SDHI0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7790",
				  "sdhi0_data4", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7790",
				  "sdhi0_ctrl", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7790",
				  "sdhi0_cd", "sdhi0"),
	/* SDHI2 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.2", "pfc-r8a7790",
				  "sdhi2_data4", "sdhi2"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.2", "pfc-r8a7790",
				  "sdhi2_ctrl", "sdhi2"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.2", "pfc-r8a7790",
				  "sdhi2_cd", "sdhi2"),
	/* SSI (CN17: sound) */
	PIN_MAP_MUX_GROUP_DEFAULT("rcar_sound", "pfc-r8a7790",
				  "ssi0129_ctrl", "ssi"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar_sound", "pfc-r8a7790",
				  "ssi0_data", "ssi"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar_sound", "pfc-r8a7790",
				  "ssi1_data", "ssi"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar_sound", "pfc-r8a7790",
				  "audio_clk_a", "audio_clk"),
	/* MMCIF1 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.1", "pfc-r8a7790",
				  "mmc1_data8", "mmc1"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.1", "pfc-r8a7790",
				  "mmc1_ctrl", "mmc1"),
	/* Ether */
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7790-ether", "pfc-r8a7790",
				  "eth_link", "eth"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7790-ether", "pfc-r8a7790",
				  "eth_mdio", "eth"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7790-ether", "pfc-r8a7790",
				  "eth_rmii", "eth"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7790-ether", "pfc-r8a7790",
				  "intc_irq0", "intc"),
	/* VIN0 */
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7790-vin.0", "pfc-r8a7790",
				  "vin0_data24", "vin0"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7790-vin.0", "pfc-r8a7790",
				  "vin0_sync", "vin0"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7790-vin.0", "pfc-r8a7790",
				  "vin0_field", "vin0"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7790-vin.0", "pfc-r8a7790",
				  "vin0_clkenb", "vin0"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7790-vin.0", "pfc-r8a7790",
				  "vin0_clk", "vin0"),
	/* VIN1 */
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7790-vin.1", "pfc-r8a7790",
				  "vin1_data8", "vin1"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7790-vin.1", "pfc-r8a7790",
				  "vin1_clk", "vin1"),
	/* USB0 */
	PIN_MAP_MUX_GROUP_DEFAULT("renesas_usbhs", "pfc-r8a7790",
				  "usb0_ovc_vbus", "usb0"),
	/* USB1 */
	PIN_MAP_MUX_GROUP_DEFAULT("pci-rcar-gen2.1", "pfc-r8a7790",
				  "usb1", "usb1"),
	/* USB2 */
	PIN_MAP_MUX_GROUP_DEFAULT("pci-rcar-gen2.2", "pfc-r8a7790",
				  "usb2", "usb2"),
};

static void __init lager_add_standard_devices(void)
{
	int fixed_regulator_idx = 0;
	int gpio_regulator_idx = 0;

	r8a7790_clock_init();

	pinctrl_register_mappings(lager_pinctrl_map,
				  ARRAY_SIZE(lager_pinctrl_map));
	r8a7790_pinmux_init();

	r8a7790_add_standard_devices();
	platform_device_register_data(NULL, "leds-gpio", -1,
				      &lager_leds_pdata,
				      sizeof(lager_leds_pdata));
	platform_device_register_data(NULL, "gpio-keys", -1,
				      &lager_keys_pdata,
				      sizeof(lager_keys_pdata));
	regulator_register_always_on(fixed_regulator_idx++,
				     "fixed-3.3V", fixed3v3_power_consumers,
				     ARRAY_SIZE(fixed3v3_power_consumers), 3300000);
	platform_device_register_resndata(NULL, "sh_mmcif", 1,
					  mmcif1_resources, ARRAY_SIZE(mmcif1_resources),
					  &mmcif1_pdata, sizeof(mmcif1_pdata));

	platform_device_register_full(&ether_info);

	lager_add_du_device();

	platform_device_register_resndata(NULL, "qspi", 0,
					  qspi_resources,
					  ARRAY_SIZE(qspi_resources),
					  &qspi_pdata, sizeof(qspi_pdata));
	spi_register_board_info(spi_info, ARRAY_SIZE(spi_info));

	platform_device_register_data(NULL, "reg-fixed-voltage", fixed_regulator_idx++,
				      &vcc_sdhi0_info, sizeof(struct fixed_voltage_config));
	platform_device_register_data(NULL, "reg-fixed-voltage", fixed_regulator_idx++,
				      &vcc_sdhi2_info, sizeof(struct fixed_voltage_config));

	platform_device_register_data(NULL, "gpio-regulator", gpio_regulator_idx++,
				      &vccq_sdhi0_info, sizeof(struct gpio_regulator_config));
	platform_device_register_data(NULL, "gpio-regulator", gpio_regulator_idx++,
				      &vccq_sdhi2_info, sizeof(struct gpio_regulator_config));

	lager_add_camera1_device();

	platform_device_register_full(&sata1_info);

	platform_device_register_resndata(NULL, "usb_phy_rcar_gen2",
					  -1, usbhs_phy_resources,
					  ARRAY_SIZE(usbhs_phy_resources),
					  &usbhs_phy_pdata,
					  sizeof(usbhs_phy_pdata));
	lager_register_usbhs();
	lager_add_usb1_device();
	lager_add_usb2_device();

	lager_add_rsnd_device();

	platform_device_register_resndata(NULL, "sh_mobile_sdhi", 0,
					  sdhi0_resources, ARRAY_SIZE(sdhi0_resources),
					  &sdhi0_info, sizeof(struct sh_mobile_sdhi_info));
	platform_device_register_resndata(NULL, "sh_mobile_sdhi", 2,
					  sdhi2_resources, ARRAY_SIZE(sdhi2_resources),
					  &sdhi2_info, sizeof(struct sh_mobile_sdhi_info));
}

/*
 * Ether LEDs on the Lager board are named LINK and ACTIVE which corresponds
 * to non-default 01 setting of the Micrel KSZ8041 PHY control register 1 bits
 * 14-15. We have to set them back to 01 from the default 00 value each time
 * the PHY is reset. It's also important because the PHY's LED0 signal is
 * connected to SoC's ETH_LINK signal and in the PHY's default mode it will
 * bounce on and off after each packet, which we apparently want to avoid.
 */
static int lager_ksz8041_fixup(struct phy_device *phydev)
{
	u16 phyctrl1 = phy_read(phydev, 0x1e);

	phyctrl1 &= ~0xc000;
	phyctrl1 |= 0x4000;
	return phy_write(phydev, 0x1e, phyctrl1);
}

static void __init lager_init(void)
{
	lager_add_standard_devices();

	irq_set_irq_type(irq_pin(0), IRQ_TYPE_LEVEL_LOW);

	if (IS_ENABLED(CONFIG_PHYLIB))
		phy_register_fixup_for_id("r8a7790-ether-ff:01",
					  lager_ksz8041_fixup);
}

static const char * const lager_boards_compat_dt[] __initconst = {
	"renesas,lager",
	NULL,
};

DT_MACHINE_START(LAGER_DT, "lager")
	.smp		= smp_ops(r8a7790_smp_ops),
	.init_early	= r8a7790_init_early,
	.init_time	= rcar_gen2_timer_init,
	.init_machine	= lager_init,
	.init_late	= shmobile_init_late,
	.dt_compat	= lager_boards_compat_dt,
MACHINE_END
