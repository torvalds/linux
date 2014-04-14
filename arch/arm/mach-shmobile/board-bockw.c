/*
 * Bock-W board support
 *
 * Copyright (C) 2013-2014  Renesas Solutions Corp.
 * Copyright (C) 2013  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 * Copyright (C) 2013-2014  Cogent Embedded, Inc.
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

#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/mtd/partitions.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_data/camera-rcar.h>
#include <linux/platform_data/usb-rcar-phy.h>
#include <linux/platform_device.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/smsc911x.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/usb/renesas_usbhs.h>
#include <media/soc_camera.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a7778.h>
#include <asm/mach/arch.h>
#include <sound/rcar_snd.h>
#include <sound/simple_card.h>

#define FPGA	0x18200000
#define IRQ0MR	0x30
#define COMCTLR	0x101c
static void __iomem *fpga;

/*
 *	CN9(Upper side) SCIF/RCAN selection
 *
 *		1,4	3,6
 * SW40		SCIF	RCAN
 * SW41		SCIF	RCAN
 */

/*
 * MMC (CN26) pin
 *
 * SW6	(D2)	3 pin
 * SW7	(D5)	ON
 * SW8	(D3)	3 pin
 * SW10	(D4)	1 pin
 * SW12	(CLK)	1 pin
 * SW13	(D6)	3 pin
 * SW14	(CMD)	ON
 * SW15	(D6)	1 pin
 * SW16	(D0)	ON
 * SW17	(D1)	ON
 * SW18	(D7)	3 pin
 * SW19	(MMC)	1 pin
 */

/*
 *	SSI settings
 *
 * SW45: 1-4 side	(SSI5 out, ROUT/LOUT CN19 Mid)
 * SW46: 1101		(SSI6 Recorde)
 * SW47: 1110		(SSI5 Playback)
 * SW48: 11		(Recorde power)
 * SW49: 1		(SSI slave mode)
 * SW50: 1111		(SSI7, SSI8)
 * SW51: 1111		(SSI3, SSI4)
 * SW54: 1pin		(ak4554 FPGA control)
 * SW55: 1		(CLKB is 24.5760MHz)
 * SW60: 1pin		(ak4554 FPGA control)
 * SW61: 3pin		(use X11 clock)
 * SW78: 3-6		(ak4642 connects I2C0)
 *
 * You can use sound as
 *
 * hw0: CN19: SSI56-AK4643
 * hw1: CN21: SSI3-AK4554(playback)
 * hw2: CN21: SSI4-AK4554(capture)
 * hw3: CN20: SSI7-AK4554(playback)
 * hw4: CN20: SSI8-AK4554(capture)
 *
 * this command is required when playback on hw0.
 *
 * # amixer set "LINEOUT Mixer DACL" on
 */

/*
 * USB
 *
 * USB1 (CN29) can be Host/Function
 *
 *		Host	Func
 * SW98		1	2
 * SW99		1	3
 */

/* Dummy supplies, where voltage doesn't matter */
static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x"),
};

static struct regulator_consumer_supply fixed3v3_power_consumers[] = {
	REGULATOR_SUPPLY("vmmc", "sh_mmcif"),
	REGULATOR_SUPPLY("vqmmc", "sh_mmcif"),
};

static struct smsc911x_platform_config smsc911x_data __initdata = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
	.flags		= SMSC911X_USE_32BIT,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
};

static struct resource smsc911x_resources[] __initdata = {
	DEFINE_RES_MEM(0x18300000, 0x1000),
	DEFINE_RES_IRQ(irq_pin(0)), /* IRQ 0 */
};

#if IS_ENABLED(CONFIG_USB_RENESAS_USBHS_UDC)
/*
 * When USB1 is Func
 */
static int usbhsf_get_id(struct platform_device *pdev)
{
	return USBHS_GADGET;
}

#define SUSPMODE	0x102
static int usbhsf_power_ctrl(struct platform_device *pdev,
			     void __iomem *base, int enable)
{
	enable = !!enable;

	r8a7778_usb_phy_power(enable);

	iowrite16(enable << 14, base + SUSPMODE);

	return 0;
}

static struct resource usbhsf_resources[] __initdata = {
	DEFINE_RES_MEM(0xffe60000, 0x110),
	DEFINE_RES_IRQ(gic_iid(0x4f)),
};

static struct renesas_usbhs_platform_info usbhs_info __initdata = {
	.platform_callback = {
		.get_id		= usbhsf_get_id,
		.power_ctrl	= usbhsf_power_ctrl,
	},
	.driver_param = {
		.buswait_bwait	= 4,
		.d0_tx_id	= HPBDMA_SLAVE_USBFUNC_TX,
		.d1_rx_id	= HPBDMA_SLAVE_USBFUNC_RX,
	},
};

#define USB_PHY_SETTING {.port1_func = 1, .ovc_pin[1].active_high = 1,}
#define USB1_DEVICE	"renesas_usbhs"
#define ADD_USB_FUNC_DEVICE_IF_POSSIBLE()			\
	platform_device_register_resndata(			\
		&platform_bus, "renesas_usbhs", -1,		\
		usbhsf_resources,				\
		ARRAY_SIZE(usbhsf_resources),			\
		&usbhs_info, sizeof(struct renesas_usbhs_platform_info))

#else
/*
 * When USB1 is Host
 */
#define USB_PHY_SETTING { }
#define USB1_DEVICE	"ehci-platform"
#define ADD_USB_FUNC_DEVICE_IF_POSSIBLE()

#endif

/* USB */
static struct resource usb_phy_resources[] __initdata = {
	DEFINE_RES_MEM(0xffe70800, 0x100),
	DEFINE_RES_MEM(0xffe76000, 0x100),
};

static struct rcar_phy_platform_data usb_phy_platform_data __initdata =
	USB_PHY_SETTING;


/* SDHI */
static struct sh_mobile_sdhi_info sdhi0_info __initdata = {
	.dma_slave_tx	= HPBDMA_SLAVE_SDHI0_TX,
	.dma_slave_rx	= HPBDMA_SLAVE_SDHI0_RX,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED,
	.tmio_ocr_mask	= MMC_VDD_165_195 | MMC_VDD_32_33 | MMC_VDD_33_34,
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT,
};

static struct resource sdhi0_resources[] __initdata = {
	DEFINE_RES_MEM(0xFFE4C000, 0x100),
	DEFINE_RES_IRQ(gic_iid(0x77)),
};

/* Ether */
static struct resource ether_resources[] __initdata = {
	DEFINE_RES_MEM(0xfde00000, 0x400),
	DEFINE_RES_IRQ(gic_iid(0x89)),
};

static struct sh_eth_plat_data ether_platform_data __initdata = {
	.phy		= 0x01,
	.edmac_endian	= EDMAC_LITTLE_ENDIAN,
	.phy_interface	= PHY_INTERFACE_MODE_RMII,
	/*
	 * Although the LINK signal is available on the board, it's connected to
	 * the link/activity LED output of the PHY, thus the link disappears and
	 * reappears after each packet.  We'd be better off ignoring such signal
	 * and getting the link state from the PHY indirectly.
	 */
	.no_ether_link	= 1,
};

static struct platform_device_info ether_info __initdata = {
	.parent		= &platform_bus,
	.name		= "r8a777x-ether",
	.id		= -1,
	.res		= ether_resources,
	.num_res	= ARRAY_SIZE(ether_resources),
	.data		= &ether_platform_data,
	.size_data	= sizeof(ether_platform_data),
	.dma_mask	= DMA_BIT_MASK(32),
};

/* I2C */
static struct i2c_board_info i2c0_devices[] = {
	{
		I2C_BOARD_INFO("rx8581", 0x51),
	}, {
		I2C_BOARD_INFO("ak4643", 0x12),
	}
};

/* HSPI*/
static struct mtd_partition m25p80_spi_flash_partitions[] = {
	{
		.name	= "data(spi)",
		.size	= 0x0100000,
		.offset	= 0,
	},
};

static struct flash_platform_data spi_flash_data = {
	.name		= "m25p80",
	.type		= "s25fl008k",
	.parts		= m25p80_spi_flash_partitions,
	.nr_parts	= ARRAY_SIZE(m25p80_spi_flash_partitions),
};

static struct spi_board_info spi_board_info[] __initdata = {
	{
		.modalias	= "m25p80",
		.max_speed_hz	= 104000000,
		.chip_select	= 0,
		.bus_num	= 0,
		.mode		= SPI_MODE_0,
		.platform_data	= &spi_flash_data,
	},
};

/* MMC */
static struct resource mmc_resources[] __initdata = {
	DEFINE_RES_MEM(0xffe4e000, 0x100),
	DEFINE_RES_IRQ(gic_iid(0x5d)),
};

static struct sh_mmcif_plat_data sh_mmcif_plat __initdata = {
	.sup_pclk	= 0,
	.caps		= MMC_CAP_4_BIT_DATA |
			  MMC_CAP_8_BIT_DATA |
			  MMC_CAP_NEEDS_POLL,
};

/* In the default configuration both decoders reside on I2C bus 0 */
#define BOCKW_CAMERA(idx)						\
static struct i2c_board_info camera##idx##_info = {			\
	I2C_BOARD_INFO("ml86v7667", 0x41 + 2 * (idx)),			\
};									\
									\
static struct soc_camera_link iclink##idx##_ml86v7667 __initdata = {	\
	.bus_id		= idx,						\
	.i2c_adapter_id	= 0,						\
	.board_info	= &camera##idx##_info,				\
}

BOCKW_CAMERA(0);
BOCKW_CAMERA(1);

/* VIN */
static struct rcar_vin_platform_data vin_platform_data __initdata = {
	.flags	= RCAR_VIN_BT656,
};

#define R8A7778_VIN(idx)						\
static struct resource vin##idx##_resources[] __initdata = {		\
	DEFINE_RES_MEM(0xffc50000 + 0x1000 * (idx), 0x1000),		\
	DEFINE_RES_IRQ(gic_iid(0x5a)),					\
};									\
									\
static struct platform_device_info vin##idx##_info __initdata = {	\
	.parent		= &platform_bus,				\
	.name		= "r8a7778-vin",				\
	.id		= idx,						\
	.res		= vin##idx##_resources,				\
	.num_res	= ARRAY_SIZE(vin##idx##_resources),		\
	.dma_mask	= DMA_BIT_MASK(32),				\
	.data		= &vin_platform_data,				\
	.size_data	= sizeof(vin_platform_data),			\
}
R8A7778_VIN(0);
R8A7778_VIN(1);

/* Sound */
static struct resource rsnd_resources[] __initdata = {
	[RSND_GEN1_SRU] = DEFINE_RES_MEM(0xffd90000, 0x1000),
	[RSND_GEN1_SSI] = DEFINE_RES_MEM(0xffd91000, 0x1240),
	[RSND_GEN1_ADG] = DEFINE_RES_MEM(0xfffe0000, 0x24),
};

static struct rsnd_ssi_platform_info rsnd_ssi[] = {
	RSND_SSI_UNUSED, /* SSI 0 */
	RSND_SSI_UNUSED, /* SSI 1 */
	RSND_SSI_UNUSED, /* SSI 2 */
	RSND_SSI_SET(1, HPBDMA_SLAVE_HPBIF3_TX, gic_iid(0x85), RSND_SSI_PLAY),
	RSND_SSI_SET(2, HPBDMA_SLAVE_HPBIF4_RX, gic_iid(0x85), RSND_SSI_CLK_PIN_SHARE),
	RSND_SSI_SET(0, HPBDMA_SLAVE_HPBIF5_TX, gic_iid(0x86), RSND_SSI_PLAY),
	RSND_SSI_SET(0, HPBDMA_SLAVE_HPBIF6_RX, gic_iid(0x86), 0),
	RSND_SSI_SET(3, HPBDMA_SLAVE_HPBIF7_TX, gic_iid(0x86), RSND_SSI_PLAY),
	RSND_SSI_SET(4, HPBDMA_SLAVE_HPBIF8_RX, gic_iid(0x86), RSND_SSI_CLK_PIN_SHARE),
};

static struct rsnd_scu_platform_info rsnd_scu[9] = {
	{ .flags = 0, }, /* SRU 0 */
	{ .flags = 0, }, /* SRU 1 */
	{ .flags = 0, }, /* SRU 2 */
	{ .flags = RSND_SCU_USE_HPBIF, },
	{ .flags = RSND_SCU_USE_HPBIF, },
	{ .flags = RSND_SCU_USE_HPBIF, },
	{ .flags = RSND_SCU_USE_HPBIF, },
	{ .flags = RSND_SCU_USE_HPBIF, },
	{ .flags = RSND_SCU_USE_HPBIF, },
};

enum {
	AK4554_34 = 0,
	AK4643_56,
	AK4554_78,
	SOUND_MAX,
};

static int rsnd_codec_power(int id, int enable)
{
	static int sound_user[SOUND_MAX] = {0, 0, 0};
	int *usr = NULL;
	u32 bit;

	switch (id) {
	case 3:
	case 4:
		usr = sound_user + AK4554_34;
		bit = (1 << 10);
		break;
	case 5:
	case 6:
		usr = sound_user + AK4643_56;
		bit = (1 << 6);
		break;
	case 7:
	case 8:
		usr = sound_user + AK4554_78;
		bit = (1 << 7);
		break;
	}

	if (!usr)
		return -EIO;

	if (enable) {
		if (*usr == 0) {
			u32 val = ioread16(fpga + COMCTLR);
			val &= ~bit;
			iowrite16(val, fpga + COMCTLR);
		}

		(*usr)++;
	} else {
		if (*usr == 0)
			return 0;

		(*usr)--;

		if (*usr == 0) {
			u32 val = ioread16(fpga + COMCTLR);
			val |= bit;
			iowrite16(val, fpga + COMCTLR);
		}
	}

	return 0;
}

static int rsnd_start(int id)
{
	return rsnd_codec_power(id, 1);
}

static int rsnd_stop(int id)
{
	return rsnd_codec_power(id, 0);
}

static struct rcar_snd_info rsnd_info = {
	.flags		= RSND_GEN1,
	.ssi_info	= rsnd_ssi,
	.ssi_info_nr	= ARRAY_SIZE(rsnd_ssi),
	.scu_info	= rsnd_scu,
	.scu_info_nr	= ARRAY_SIZE(rsnd_scu),
	.start		= rsnd_start,
	.stop		= rsnd_stop,
};

static struct asoc_simple_card_info rsnd_card_info[] = {
	/* SSI5, SSI6 */
	{
		.name		= "AK4643",
		.card		= "SSI56-AK4643",
		.codec		= "ak4642-codec.0-0012",
		.platform	= "rcar_sound",
		.daifmt		= SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_CBM_CFM,
		.cpu_dai = {
			.name	= "rsnd-dai.0",
		},
		.codec_dai = {
			.name	= "ak4642-hifi",
			.sysclk	= 11289600,
		},
	},
	/* SSI3 */
	{
		.name		= "AK4554",
		.card		= "SSI3-AK4554(playback)",
		.codec		= "ak4554-adc-dac.0",
		.platform	= "rcar_sound",
		.daifmt		= SND_SOC_DAIFMT_CBS_CFS | SND_SOC_DAIFMT_RIGHT_J,
		.cpu_dai = {
			.name	= "rsnd-dai.1",
		},
		.codec_dai = {
			.name	= "ak4554-hifi",
		},
	},
	/* SSI4 */
	{
		.name		= "AK4554",
		.card		= "SSI4-AK4554(capture)",
		.codec		= "ak4554-adc-dac.0",
		.platform	= "rcar_sound",
		.daifmt		= SND_SOC_DAIFMT_CBS_CFS | SND_SOC_DAIFMT_LEFT_J,
		.cpu_dai = {
			.name	= "rsnd-dai.2",
		},
		.codec_dai = {
			.name	= "ak4554-hifi",
		},
	},
	/* SSI7 */
	{
		.name		= "AK4554",
		.card		= "SSI7-AK4554(playback)",
		.codec		= "ak4554-adc-dac.1",
		.platform	= "rcar_sound",
		.daifmt		= SND_SOC_DAIFMT_CBS_CFS | SND_SOC_DAIFMT_RIGHT_J,
		.cpu_dai = {
			.name	= "rsnd-dai.3",
		},
		.codec_dai = {
			.name	= "ak4554-hifi",
		},
	},
	/* SSI8 */
	{
		.name		= "AK4554",
		.card		= "SSI8-AK4554(capture)",
		.codec		= "ak4554-adc-dac.1",
		.platform	= "rcar_sound",
		.daifmt		= SND_SOC_DAIFMT_CBS_CFS | SND_SOC_DAIFMT_LEFT_J,
		.cpu_dai = {
			.name	= "rsnd-dai.4",
		},
		.codec_dai = {
			.name	= "ak4554-hifi",
		},
	}
};

static const struct pinctrl_map bockw_pinctrl_map[] = {
	/* AUDIO */
	PIN_MAP_MUX_GROUP_DEFAULT("rcar_sound", "pfc-r8a7778",
				  "audio_clk_a", "audio_clk"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar_sound", "pfc-r8a7778",
				  "audio_clk_b", "audio_clk"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar_sound", "pfc-r8a7778",
				  "ssi34_ctrl", "ssi"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar_sound", "pfc-r8a7778",
				  "ssi3_data", "ssi"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar_sound", "pfc-r8a7778",
				  "ssi4_data", "ssi"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar_sound", "pfc-r8a7778",
				  "ssi5_ctrl", "ssi"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar_sound", "pfc-r8a7778",
				  "ssi5_data", "ssi"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar_sound", "pfc-r8a7778",
				  "ssi6_ctrl", "ssi"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar_sound", "pfc-r8a7778",
				  "ssi6_data", "ssi"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar_sound", "pfc-r8a7778",
				  "ssi78_ctrl", "ssi"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar_sound", "pfc-r8a7778",
				  "ssi7_data", "ssi"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar_sound", "pfc-r8a7778",
				  "ssi8_data", "ssi"),
	/* Ether */
	PIN_MAP_MUX_GROUP_DEFAULT("r8a777x-ether", "pfc-r8a7778",
				  "ether_rmii", "ether"),
	/* HSPI0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-hspi.0", "pfc-r8a7778",
				  "hspi0_a", "hspi0"),
	/* MMC */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif", "pfc-r8a7778",
				  "mmc_data8", "mmc"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif", "pfc-r8a7778",
				  "mmc_ctrl", "mmc"),
	/* SCIF0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.0", "pfc-r8a7778",
				  "scif0_data_a", "scif0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.0", "pfc-r8a7778",
				  "scif0_ctrl", "scif0"),
	/* USB */
	PIN_MAP_MUX_GROUP_DEFAULT("ehci-platform", "pfc-r8a7778",
				  "usb0", "usb0"),
	PIN_MAP_MUX_GROUP_DEFAULT(USB1_DEVICE, "pfc-r8a7778",
				  "usb1", "usb1"),
	/* SDHI0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7778",
				  "sdhi0_data4", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7778",
				  "sdhi0_ctrl", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7778",
				  "sdhi0_cd", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7778",
				  "sdhi0_wp", "sdhi0"),
	/* VIN0 */
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7778-vin.0", "pfc-r8a7778",
				  "vin0_clk", "vin0"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7778-vin.0", "pfc-r8a7778",
				  "vin0_data8", "vin0"),
	/* VIN1 */
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7778-vin.1", "pfc-r8a7778",
				  "vin1_clk", "vin1"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7778-vin.1", "pfc-r8a7778",
				  "vin1_data8", "vin1"),
};

#define PFC	0xfffc0000
#define PUPR4	0x110
static void __init bockw_init(void)
{
	void __iomem *base;
	struct clk *clk;
	struct platform_device *pdev;
	int i;

	r8a7778_clock_init();
	r8a7778_init_irq_extpin(1);
	r8a7778_add_standard_devices();

	platform_device_register_full(&ether_info);

	platform_device_register_full(&vin0_info);
	/* VIN1 has a pin conflict with Ether */
	if (!IS_ENABLED(CONFIG_SH_ETH))
		platform_device_register_full(&vin1_info);
	platform_device_register_data(&platform_bus, "soc-camera-pdrv", 0,
				      &iclink0_ml86v7667,
				      sizeof(iclink0_ml86v7667));
	platform_device_register_data(&platform_bus, "soc-camera-pdrv", 1,
				      &iclink1_ml86v7667,
				      sizeof(iclink1_ml86v7667));

	i2c_register_board_info(0, i2c0_devices,
				ARRAY_SIZE(i2c0_devices));
	spi_register_board_info(spi_board_info,
				ARRAY_SIZE(spi_board_info));
	pinctrl_register_mappings(bockw_pinctrl_map,
				  ARRAY_SIZE(bockw_pinctrl_map));
	r8a7778_pinmux_init();

	platform_device_register_resndata(
		&platform_bus, "sh_mmcif", -1,
		mmc_resources, ARRAY_SIZE(mmc_resources),
		&sh_mmcif_plat, sizeof(struct sh_mmcif_plat_data));

	platform_device_register_resndata(
		&platform_bus, "rcar_usb_phy", -1,
		usb_phy_resources,
		ARRAY_SIZE(usb_phy_resources),
		&usb_phy_platform_data,
		sizeof(struct rcar_phy_platform_data));

	regulator_register_fixed(0, dummy_supplies,
				 ARRAY_SIZE(dummy_supplies));
	regulator_register_always_on(1, "fixed-3.3V", fixed3v3_power_consumers,
				     ARRAY_SIZE(fixed3v3_power_consumers), 3300000);

	/* for SMSC */
	fpga = ioremap_nocache(FPGA, SZ_1M);
	if (fpga) {
		/*
		 * CAUTION
		 *
		 * IRQ0/1 is cascaded interrupt from FPGA.
		 * it should be cared in the future
		 * Now, it is assuming IRQ0 was used only from SMSC.
		 */
		u16 val = ioread16(fpga + IRQ0MR);
		val &= ~(1 << 4); /* enable SMSC911x */
		iowrite16(val, fpga + IRQ0MR);

		platform_device_register_resndata(
			&platform_bus, "smsc911x", -1,
			smsc911x_resources, ARRAY_SIZE(smsc911x_resources),
			&smsc911x_data, sizeof(smsc911x_data));
	}

	/* for SDHI */
	base = ioremap_nocache(PFC, 0x200);
	if (base) {
		/*
		 * FIXME
		 *
		 * SDHI CD/WP pin needs pull-up
		 */
		iowrite32(ioread32(base + PUPR4) | (3 << 26), base + PUPR4);
		iounmap(base);

		platform_device_register_resndata(
			&platform_bus, "sh_mobile_sdhi", 0,
			sdhi0_resources, ARRAY_SIZE(sdhi0_resources),
			&sdhi0_info, sizeof(struct sh_mobile_sdhi_info));
	}

	/* for Audio */
	rsnd_codec_power(5, 1); /* enable ak4642 */

	platform_device_register_simple(
		"ak4554-adc-dac", 0, NULL, 0);

	platform_device_register_simple(
		"ak4554-adc-dac", 1, NULL, 0);

	pdev = platform_device_register_resndata(
		&platform_bus, "rcar_sound", -1,
		rsnd_resources, ARRAY_SIZE(rsnd_resources),
		&rsnd_info, sizeof(rsnd_info));

	clk = clk_get(&pdev->dev, "clk_b");
	clk_set_rate(clk, 24576000);
	clk_put(clk);

	for (i = 0; i < ARRAY_SIZE(rsnd_card_info); i++) {
		struct platform_device_info cardinfo = {
			.parent         = &platform_bus,
			.name           = "asoc-simple-card",
			.id             = i,
			.data           = &rsnd_card_info[i],
			.size_data      = sizeof(struct asoc_simple_card_info),
			.dma_mask	= DMA_BIT_MASK(32),
		};

		platform_device_register_full(&cardinfo);
	}
}

static void __init bockw_init_late(void)
{
	r8a7778_init_late();
	ADD_USB_FUNC_DEVICE_IF_POSSIBLE();
}

static const char *bockw_boards_compat_dt[] __initdata = {
	"renesas,bockw",
	NULL,
};

DT_MACHINE_START(BOCKW_DT, "bockw")
	.init_early	= r8a7778_init_delay,
	.init_irq	= r8a7778_init_irq_dt,
	.init_machine	= bockw_init,
	.dt_compat	= bockw_boards_compat_dt,
	.init_late      = bockw_init_late,
MACHINE_END
