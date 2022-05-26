/*
 * TI DaVinci DM365 EVM board support
 *
 * Copyright (C) 2009 Texas Instruments Incorporated
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/property.h>
#include <linux/leds.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>
#include <linux/mtd/rawnand.h>
#include <linux/nvmem-provider.h>
#include <linux/input.h>
#include <linux/spi/spi.h>
#include <linux/spi/eeprom.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/platform_data/ti-aemif.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <linux/platform_data/i2c-davinci.h>
#include <linux/platform_data/mmc-davinci.h>
#include <linux/platform_data/mtd-davinci.h>
#include <linux/platform_data/keyscan-davinci.h>

#include <media/i2c/ths7303.h>
#include <media/i2c/tvp514x.h>

#include "mux.h"
#include "common.h"
#include "serial.h"
#include "davinci.h"

static inline int have_imager(void)
{
	/* REVISIT when it's supported, trigger via Kconfig */
	return 0;
}

static inline int have_tvp7002(void)
{
	/* REVISIT when it's supported, trigger via Kconfig */
	return 0;
}

#define DM365_EVM_PHY_ID		"davinci_mdio-0:01"
/*
 * A MAX-II CPLD is used for various board control functions.
 */
#define CPLD_OFFSET(a13a8,a2a1)		(((a13a8) << 10) + ((a2a1) << 3))

#define CPLD_VERSION	CPLD_OFFSET(0,0)	/* r/o */
#define CPLD_TEST	CPLD_OFFSET(0,1)
#define CPLD_LEDS	CPLD_OFFSET(0,2)
#define CPLD_MUX	CPLD_OFFSET(0,3)
#define CPLD_SWITCH	CPLD_OFFSET(1,0)	/* r/o */
#define CPLD_POWER	CPLD_OFFSET(1,1)
#define CPLD_VIDEO	CPLD_OFFSET(1,2)
#define CPLD_CARDSTAT	CPLD_OFFSET(1,3)	/* r/o */

#define CPLD_DILC_OUT	CPLD_OFFSET(2,0)
#define CPLD_DILC_IN	CPLD_OFFSET(2,1)	/* r/o */

#define CPLD_IMG_DIR0	CPLD_OFFSET(2,2)
#define CPLD_IMG_MUX0	CPLD_OFFSET(2,3)
#define CPLD_IMG_MUX1	CPLD_OFFSET(3,0)
#define CPLD_IMG_DIR1	CPLD_OFFSET(3,1)
#define CPLD_IMG_MUX2	CPLD_OFFSET(3,2)
#define CPLD_IMG_MUX3	CPLD_OFFSET(3,3)
#define CPLD_IMG_DIR2	CPLD_OFFSET(4,0)
#define CPLD_IMG_MUX4	CPLD_OFFSET(4,1)
#define CPLD_IMG_MUX5	CPLD_OFFSET(4,2)

#define CPLD_RESETS	CPLD_OFFSET(4,3)

#define CPLD_CCD_DIR1	CPLD_OFFSET(0x3e,0)
#define CPLD_CCD_IO1	CPLD_OFFSET(0x3e,1)
#define CPLD_CCD_DIR2	CPLD_OFFSET(0x3e,2)
#define CPLD_CCD_IO2	CPLD_OFFSET(0x3e,3)
#define CPLD_CCD_DIR3	CPLD_OFFSET(0x3f,0)
#define CPLD_CCD_IO3	CPLD_OFFSET(0x3f,1)

static void __iomem *cpld;


/* NOTE:  this is geared for the standard config, with a socketed
 * 2 GByte Micron NAND (MT29F16G08FAA) using 128KB sectors.  If you
 * swap chips with a different block size, partitioning will
 * need to be changed. This NAND chip MT29F16G08FAA is the default
 * NAND shipped with the Spectrum Digital DM365 EVM
 */
#define NAND_BLOCK_SIZE		SZ_128K

static struct mtd_partition davinci_nand_partitions[] = {
	{
		/* UBL (a few copies) plus U-Boot */
		.name		= "bootloader",
		.offset		= 0,
		.size		= 30 * NAND_BLOCK_SIZE,
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
	}, {
		/* U-Boot environment */
		.name		= "params",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 2 * NAND_BLOCK_SIZE,
		.mask_flags	= 0,
	}, {
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_4M,
		.mask_flags	= 0,
	}, {
		.name		= "filesystem1",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_512M,
		.mask_flags	= 0,
	}, {
		.name		= "filesystem2",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0,
	}
	/* two blocks with bad block table (and mirror) at the end */
};

static struct davinci_nand_pdata davinci_nand_data = {
	.core_chipsel		= 0,
	.mask_chipsel		= BIT(14),
	.parts			= davinci_nand_partitions,
	.nr_parts		= ARRAY_SIZE(davinci_nand_partitions),
	.engine_type		= NAND_ECC_ENGINE_TYPE_ON_HOST,
	.bbt_options		= NAND_BBT_USE_FLASH,
	.ecc_bits		= 4,
};

static struct resource davinci_nand_resources[] = {
	{
		.start		= DM365_ASYNC_EMIF_DATA_CE0_BASE,
		.end		= DM365_ASYNC_EMIF_DATA_CE0_BASE + SZ_32M - 1,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= DM365_ASYNC_EMIF_CONTROL_BASE,
		.end		= DM365_ASYNC_EMIF_CONTROL_BASE + SZ_4K - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device davinci_aemif_devices[] = {
	{
		.name		= "davinci_nand",
		.id		= 0,
		.num_resources	= ARRAY_SIZE(davinci_nand_resources),
		.resource	= davinci_nand_resources,
		.dev		= {
			.platform_data	= &davinci_nand_data,
		},
	}
};

static struct resource davinci_aemif_resources[] = {
	{
		.start		= DM365_ASYNC_EMIF_CONTROL_BASE,
		.end		= DM365_ASYNC_EMIF_CONTROL_BASE + SZ_4K - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct aemif_abus_data da850_evm_aemif_abus_data[] = {
	{
		.cs		= 1,
	},
};

static struct aemif_platform_data davinci_aemif_pdata = {
	.abus_data		= da850_evm_aemif_abus_data,
	.num_abus_data		= ARRAY_SIZE(da850_evm_aemif_abus_data),
	.sub_devices		= davinci_aemif_devices,
	.num_sub_devices	= ARRAY_SIZE(davinci_aemif_devices),
};

static struct platform_device davinci_aemif_device = {
	.name			= "ti-aemif",
	.id			= -1,
	.dev = {
		.platform_data	= &davinci_aemif_pdata,
	},
	.resource		= davinci_aemif_resources,
	.num_resources		= ARRAY_SIZE(davinci_aemif_resources),
};

static struct nvmem_cell_info davinci_nvmem_cells[] = {
	{
		.name		= "macaddr",
		.offset		= 0x7f00,
		.bytes		= ETH_ALEN,
	}
};

static struct nvmem_cell_table davinci_nvmem_cell_table = {
	.nvmem_name	= "1-00500",
	.cells		= davinci_nvmem_cells,
	.ncells		= ARRAY_SIZE(davinci_nvmem_cells),
};

static struct nvmem_cell_lookup davinci_nvmem_cell_lookup = {
	.nvmem_name	= "1-00500",
	.cell_name	= "macaddr",
	.dev_id		= "davinci_emac.1",
	.con_id		= "mac-address",
};

static const struct property_entry eeprom_properties[] = {
	PROPERTY_ENTRY_U32("pagesize", 64),
	{ }
};

static const struct software_node eeprom_node = {
	.properties = eeprom_properties,
};

static struct i2c_board_info i2c_info[] = {
	{
		I2C_BOARD_INFO("24c256", 0x50),
		.swnode = &eeprom_node,
	},
	{
		I2C_BOARD_INFO("tlv320aic3x", 0x18),
	},
};

static struct davinci_i2c_platform_data i2c_pdata = {
	.bus_freq	= 400	/* kHz */,
	.bus_delay	= 0	/* usec */,
};

/* Fixed regulator support */
static struct regulator_consumer_supply fixed_supplies_3_3v[] = {
	/* Baseboard 3.3V: 5V -> TPS767D301 -> 3.3V */
	REGULATOR_SUPPLY("AVDD", "1-0018"),
	REGULATOR_SUPPLY("DRVDD", "1-0018"),
	REGULATOR_SUPPLY("IOVDD", "1-0018"),
};

static struct regulator_consumer_supply fixed_supplies_1_8v[] = {
	/* Baseboard 1.8V: 5V -> TPS767D301 -> 1.8V */
	REGULATOR_SUPPLY("DVDD", "1-0018"),
};

static int dm365evm_keyscan_enable(struct device *dev)
{
	return davinci_cfg_reg(DM365_KEYSCAN);
}

static unsigned short dm365evm_keymap[] = {
	KEY_KP2,
	KEY_LEFT,
	KEY_EXIT,
	KEY_DOWN,
	KEY_ENTER,
	KEY_UP,
	KEY_KP1,
	KEY_RIGHT,
	KEY_MENU,
	KEY_RECORD,
	KEY_REWIND,
	KEY_KPMINUS,
	KEY_STOP,
	KEY_FASTFORWARD,
	KEY_KPPLUS,
	KEY_PLAYPAUSE,
	0
};

static struct davinci_ks_platform_data dm365evm_ks_data = {
	.device_enable	= dm365evm_keyscan_enable,
	.keymap		= dm365evm_keymap,
	.keymapsize	= ARRAY_SIZE(dm365evm_keymap),
	.rep		= 1,
	/* Scan period = strobe + interval */
	.strobe		= 0x5,
	.interval	= 0x2,
	.matrix_type	= DAVINCI_KEYSCAN_MATRIX_4X4,
};

static int cpld_mmc_get_cd(int module)
{
	if (!cpld)
		return -ENXIO;

	/* low == card present */
	return !(__raw_readb(cpld + CPLD_CARDSTAT) & BIT(module ? 4 : 0));
}

static int cpld_mmc_get_ro(int module)
{
	if (!cpld)
		return -ENXIO;

	/* high == card's write protect switch active */
	return !!(__raw_readb(cpld + CPLD_CARDSTAT) & BIT(module ? 5 : 1));
}

static struct davinci_mmc_config dm365evm_mmc_config = {
	.get_cd		= cpld_mmc_get_cd,
	.get_ro		= cpld_mmc_get_ro,
	.wires		= 4,
	.max_freq	= 50000000,
	.caps		= MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED,
};

static void dm365evm_emac_configure(void)
{
	/*
	 * EMAC pins are multiplexed with GPIO and UART
	 * Further details are available at the DM365 ARM
	 * Subsystem Users Guide(sprufg5.pdf) pages 125 - 127
	 */
	davinci_cfg_reg(DM365_EMAC_TX_EN);
	davinci_cfg_reg(DM365_EMAC_TX_CLK);
	davinci_cfg_reg(DM365_EMAC_COL);
	davinci_cfg_reg(DM365_EMAC_TXD3);
	davinci_cfg_reg(DM365_EMAC_TXD2);
	davinci_cfg_reg(DM365_EMAC_TXD1);
	davinci_cfg_reg(DM365_EMAC_TXD0);
	davinci_cfg_reg(DM365_EMAC_RXD3);
	davinci_cfg_reg(DM365_EMAC_RXD2);
	davinci_cfg_reg(DM365_EMAC_RXD1);
	davinci_cfg_reg(DM365_EMAC_RXD0);
	davinci_cfg_reg(DM365_EMAC_RX_CLK);
	davinci_cfg_reg(DM365_EMAC_RX_DV);
	davinci_cfg_reg(DM365_EMAC_RX_ER);
	davinci_cfg_reg(DM365_EMAC_CRS);
	davinci_cfg_reg(DM365_EMAC_MDIO);
	davinci_cfg_reg(DM365_EMAC_MDCLK);

	/*
	 * EMAC interrupts are multiplexed with GPIO interrupts
	 * Details are available at the DM365 ARM
	 * Subsystem Users Guide(sprufg5.pdf) pages 133 - 134
	 */
	davinci_cfg_reg(DM365_INT_EMAC_RXTHRESH);
	davinci_cfg_reg(DM365_INT_EMAC_RXPULSE);
	davinci_cfg_reg(DM365_INT_EMAC_TXPULSE);
	davinci_cfg_reg(DM365_INT_EMAC_MISCPULSE);
}

static void dm365evm_mmc_configure(void)
{
	/*
	 * MMC/SD pins are multiplexed with GPIO and EMIF
	 * Further details are available at the DM365 ARM
	 * Subsystem Users Guide(sprufg5.pdf) pages 118, 128 - 131
	 */
	davinci_cfg_reg(DM365_SD1_CLK);
	davinci_cfg_reg(DM365_SD1_CMD);
	davinci_cfg_reg(DM365_SD1_DATA3);
	davinci_cfg_reg(DM365_SD1_DATA2);
	davinci_cfg_reg(DM365_SD1_DATA1);
	davinci_cfg_reg(DM365_SD1_DATA0);
}

static struct tvp514x_platform_data tvp5146_pdata = {
	.clk_polarity = 0,
	.hs_polarity = 1,
	.vs_polarity = 1
};

#define TVP514X_STD_ALL        (V4L2_STD_NTSC | V4L2_STD_PAL)
/* Inputs available at the TVP5146 */
static struct v4l2_input tvp5146_inputs[] = {
	{
		.index = 0,
		.name = "Composite",
		.type = V4L2_INPUT_TYPE_CAMERA,
		.std = TVP514X_STD_ALL,
	},
	{
		.index = 1,
		.name = "S-Video",
		.type = V4L2_INPUT_TYPE_CAMERA,
		.std = TVP514X_STD_ALL,
	},
};

/*
 * this is the route info for connecting each input to decoder
 * ouput that goes to vpfe. There is a one to one correspondence
 * with tvp5146_inputs
 */
static struct vpfe_route tvp5146_routes[] = {
	{
		.input = INPUT_CVBS_VI2B,
		.output = OUTPUT_10BIT_422_EMBEDDED_SYNC,
	},
{
		.input = INPUT_SVIDEO_VI2C_VI1C,
		.output = OUTPUT_10BIT_422_EMBEDDED_SYNC,
	},
};

static struct vpfe_subdev_info vpfe_sub_devs[] = {
	{
		.name = "tvp5146",
		.grp_id = 0,
		.num_inputs = ARRAY_SIZE(tvp5146_inputs),
		.inputs = tvp5146_inputs,
		.routes = tvp5146_routes,
		.can_route = 1,
		.ccdc_if_params = {
			.if_type = VPFE_BT656,
			.hdpol = VPFE_PINPOL_POSITIVE,
			.vdpol = VPFE_PINPOL_POSITIVE,
		},
		.board_info = {
			I2C_BOARD_INFO("tvp5146", 0x5d),
			.platform_data = &tvp5146_pdata,
		},
	},
};

static struct vpfe_config vpfe_cfg = {
	.num_subdevs = ARRAY_SIZE(vpfe_sub_devs),
	.sub_devs = vpfe_sub_devs,
	.i2c_adapter_id = 1,
	.card_name = "DM365 EVM",
	.ccdc = "ISIF",
};

/* venc standards timings */
static struct vpbe_enc_mode_info dm365evm_enc_std_timing[] = {
	{
		.name		= "ntsc",
		.timings_type	= VPBE_ENC_STD,
		.std_id		= V4L2_STD_NTSC,
		.interlaced	= 1,
		.xres		= 720,
		.yres		= 480,
		.aspect		= {11, 10},
		.fps		= {30000, 1001},
		.left_margin	= 0x79,
		.upper_margin	= 0x10,
	},
	{
		.name		= "pal",
		.timings_type	= VPBE_ENC_STD,
		.std_id		= V4L2_STD_PAL,
		.interlaced	= 1,
		.xres		= 720,
		.yres		= 576,
		.aspect		= {54, 59},
		.fps		= {25, 1},
		.left_margin	= 0x7E,
		.upper_margin	= 0x16,
	},
};

/* venc dv timings */
static struct vpbe_enc_mode_info dm365evm_enc_preset_timing[] = {
	{
		.name		= "480p59_94",
		.timings_type	= VPBE_ENC_DV_TIMINGS,
		.dv_timings	= V4L2_DV_BT_CEA_720X480P59_94,
		.interlaced	= 0,
		.xres		= 720,
		.yres		= 480,
		.aspect		= {1, 1},
		.fps		= {5994, 100},
		.left_margin	= 0x8F,
		.upper_margin	= 0x2D,
	},
	{
		.name		= "576p50",
		.timings_type	= VPBE_ENC_DV_TIMINGS,
		.dv_timings	= V4L2_DV_BT_CEA_720X576P50,
		.interlaced	= 0,
		.xres		= 720,
		.yres		= 576,
		.aspect		= {1, 1},
		.fps		= {50, 1},
		.left_margin	= 0x8C,
		.upper_margin   = 0x36,
	},
	{
		.name		= "720p60",
		.timings_type	= VPBE_ENC_DV_TIMINGS,
		.dv_timings	= V4L2_DV_BT_CEA_1280X720P60,
		.interlaced	= 0,
		.xres		= 1280,
		.yres		= 720,
		.aspect		= {1, 1},
		.fps		= {60, 1},
		.left_margin	= 0x117,
		.right_margin	= 70,
		.upper_margin	= 38,
		.lower_margin	= 3,
		.hsync_len	= 80,
		.vsync_len	= 5,
	},
	{
		.name		= "1080i60",
		.timings_type	= VPBE_ENC_DV_TIMINGS,
		.dv_timings	= V4L2_DV_BT_CEA_1920X1080I60,
		.interlaced	= 1,
		.xres		= 1920,
		.yres		= 1080,
		.aspect		= {1, 1},
		.fps		= {30, 1},
		.left_margin	= 0xc9,
		.right_margin	= 80,
		.upper_margin	= 30,
		.lower_margin	= 3,
		.hsync_len	= 88,
		.vsync_len	= 5,
	},
};

#define VENC_STD_ALL	(V4L2_STD_NTSC | V4L2_STD_PAL)

/*
 * The outputs available from VPBE + ecnoders. Keep the
 * the order same as that of encoders. First those from venc followed by that
 * from encoders. Index in the output refers to index on a particular
 * encoder.Driver uses this index to pass it to encoder when it supports more
 * than one output. Application uses index of the array to set an output.
 */
static struct vpbe_output dm365evm_vpbe_outputs[] = {
	{
		.output		= {
			.index		= 0,
			.name		= "Composite",
			.type		= V4L2_OUTPUT_TYPE_ANALOG,
			.std		= VENC_STD_ALL,
			.capabilities	= V4L2_OUT_CAP_STD,
		},
		.subdev_name	= DM365_VPBE_VENC_SUBDEV_NAME,
		.default_mode	= "ntsc",
		.num_modes	= ARRAY_SIZE(dm365evm_enc_std_timing),
		.modes		= dm365evm_enc_std_timing,
		.if_params	= MEDIA_BUS_FMT_FIXED,
	},
	{
		.output		= {
			.index		= 1,
			.name		= "Component",
			.type		= V4L2_OUTPUT_TYPE_ANALOG,
			.capabilities	= V4L2_OUT_CAP_DV_TIMINGS,
		},
		.subdev_name	= DM365_VPBE_VENC_SUBDEV_NAME,
		.default_mode	= "480p59_94",
		.num_modes	= ARRAY_SIZE(dm365evm_enc_preset_timing),
		.modes		= dm365evm_enc_preset_timing,
		.if_params	= MEDIA_BUS_FMT_FIXED,
	},
};

/*
 * Amplifiers on the board
 */
static struct ths7303_platform_data ths7303_pdata = {
	.ch_1 = 3,
	.ch_2 = 3,
	.ch_3 = 3,
};

static struct amp_config_info vpbe_amp = {
	.module_name	= "ths7303",
	.is_i2c		= 1,
	.board_info	= {
		I2C_BOARD_INFO("ths7303", 0x2c),
		.platform_data = &ths7303_pdata,
	}
};

static struct vpbe_config dm365evm_display_cfg = {
	.module_name	= "dm365-vpbe-display",
	.i2c_adapter_id	= 1,
	.amp		= &vpbe_amp,
	.osd		= {
		.module_name	= DM365_VPBE_OSD_SUBDEV_NAME,
	},
	.venc		= {
		.module_name	= DM365_VPBE_VENC_SUBDEV_NAME,
	},
	.num_outputs	= ARRAY_SIZE(dm365evm_vpbe_outputs),
	.outputs	= dm365evm_vpbe_outputs,
};

static void __init evm_init_i2c(void)
{
	davinci_init_i2c(&i2c_pdata);
	i2c_register_board_info(1, i2c_info, ARRAY_SIZE(i2c_info));
}

static inline int have_leds(void)
{
#ifdef CONFIG_LEDS_CLASS
	return 1;
#else
	return 0;
#endif
}

struct cpld_led {
	struct led_classdev	cdev;
	u8			mask;
};

static const struct {
	const char *name;
	const char *trigger;
} cpld_leds[] = {
	{ "dm365evm::ds2", },
	{ "dm365evm::ds3", },
	{ "dm365evm::ds4", },
	{ "dm365evm::ds5", },
	{ "dm365evm::ds6", "nand-disk", },
	{ "dm365evm::ds7", "mmc1", },
	{ "dm365evm::ds8", "mmc0", },
	{ "dm365evm::ds9", "heartbeat", },
};

static void cpld_led_set(struct led_classdev *cdev, enum led_brightness b)
{
	struct cpld_led *led = container_of(cdev, struct cpld_led, cdev);
	u8 reg = __raw_readb(cpld + CPLD_LEDS);

	if (b != LED_OFF)
		reg &= ~led->mask;
	else
		reg |= led->mask;
	__raw_writeb(reg, cpld + CPLD_LEDS);
}

static enum led_brightness cpld_led_get(struct led_classdev *cdev)
{
	struct cpld_led *led = container_of(cdev, struct cpld_led, cdev);
	u8 reg = __raw_readb(cpld + CPLD_LEDS);

	return (reg & led->mask) ? LED_OFF : LED_FULL;
}

static int __init cpld_leds_init(void)
{
	int	i;

	if (!have_leds() ||  !cpld)
		return 0;

	/* setup LEDs */
	__raw_writeb(0xff, cpld + CPLD_LEDS);
	for (i = 0; i < ARRAY_SIZE(cpld_leds); i++) {
		struct cpld_led *led;

		led = kzalloc(sizeof(*led), GFP_KERNEL);
		if (!led)
			break;

		led->cdev.name = cpld_leds[i].name;
		led->cdev.brightness_set = cpld_led_set;
		led->cdev.brightness_get = cpld_led_get;
		led->cdev.default_trigger = cpld_leds[i].trigger;
		led->mask = BIT(i);

		if (led_classdev_register(NULL, &led->cdev) < 0) {
			kfree(led);
			break;
		}
	}

	return 0;
}
/* run after subsys_initcall() for LEDs */
fs_initcall(cpld_leds_init);


static void __init evm_init_cpld(void)
{
	u8 mux, resets;
	const char *label;
	struct clk *aemif_clk;
	int rc;

	/* Make sure we can configure the CPLD through CS1.  Then
	 * leave it on for later access to MMC and LED registers.
	 */
	aemif_clk = clk_get(NULL, "aemif");
	if (IS_ERR(aemif_clk))
		return;
	clk_prepare_enable(aemif_clk);

	if (request_mem_region(DM365_ASYNC_EMIF_DATA_CE1_BASE, SECTION_SIZE,
			"cpld") == NULL)
		goto fail;
	cpld = ioremap(DM365_ASYNC_EMIF_DATA_CE1_BASE, SECTION_SIZE);
	if (!cpld) {
		release_mem_region(DM365_ASYNC_EMIF_DATA_CE1_BASE,
				SECTION_SIZE);
fail:
		pr_err("ERROR: can't map CPLD\n");
		clk_disable_unprepare(aemif_clk);
		return;
	}

	/* External muxing for some signals */
	mux = 0;

	/* Read SW5 to set up NAND + keypad _or_ OneNAND (sync read).
	 * NOTE:  SW4 bus width setting must match!
	 */
	if ((__raw_readb(cpld + CPLD_SWITCH) & BIT(5)) == 0) {
		/* external keypad mux */
		mux |= BIT(7);

		rc = platform_device_register(&davinci_aemif_device);
		if (rc)
			pr_warn("%s(): error registering the aemif device: %d\n",
				__func__, rc);
	} else {
		/* no OneNAND support yet */
	}

	/* Leave external chips in reset when unused. */
	resets = BIT(3) | BIT(2) | BIT(1) | BIT(0);

	/* Static video input config with SN74CBT16214 1-of-3 mux:
	 *  - port b1 == tvp7002 (mux lowbits == 1 or 6)
	 *  - port b2 == imager (mux lowbits == 2 or 7)
	 *  - port b3 == tvp5146 (mux lowbits == 5)
	 *
	 * Runtime switching could work too, with limitations.
	 */
	if (have_imager()) {
		label = "HD imager";
		mux |= 2;

		/* externally mux MMC1/ENET/AIC33 to imager */
		mux |= BIT(6) | BIT(5) | BIT(3);
	} else {
		struct davinci_soc_info *soc_info = &davinci_soc_info;

		/* we can use MMC1 ... */
		dm365evm_mmc_configure();
		davinci_setup_mmc(1, &dm365evm_mmc_config);

		/* ... and ENET ... */
		dm365evm_emac_configure();
		soc_info->emac_pdata->phy_id = DM365_EVM_PHY_ID;
		resets &= ~BIT(3);

		/* ... and AIC33 */
		resets &= ~BIT(1);

		if (have_tvp7002()) {
			mux |= 1;
			resets &= ~BIT(2);
			label = "tvp7002 HD";
		} else {
			/* default to tvp5146 */
			mux |= 5;
			resets &= ~BIT(0);
			label = "tvp5146 SD";
		}
	}
	__raw_writeb(mux, cpld + CPLD_MUX);
	__raw_writeb(resets, cpld + CPLD_RESETS);
	pr_info("EVM: %s video input\n", label);

	/* REVISIT export switches: NTSC/PAL (SW5.6), EXTRA1 (SW5.2), etc */
}

static void __init dm365_evm_map_io(void)
{
	dm365_init();
}

static struct spi_eeprom at25640 = {
	.byte_len	= SZ_64K / 8,
	.name		= "at25640",
	.page_size	= 32,
	.flags		= EE_ADDR2,
};

static const struct spi_board_info dm365_evm_spi_info[] __initconst = {
	{
		.modalias	= "at25",
		.platform_data	= &at25640,
		.max_speed_hz	= 10 * 1000 * 1000,
		.bus_num	= 0,
		.chip_select	= 0,
		.mode		= SPI_MODE_0,
	},
};

static __init void dm365_evm_init(void)
{
	int ret;

	dm365_register_clocks();

	ret = dm365_gpio_register();
	if (ret)
		pr_warn("%s: GPIO init failed: %d\n", __func__, ret);

	regulator_register_always_on(0, "fixed-dummy", fixed_supplies_1_8v,
				     ARRAY_SIZE(fixed_supplies_1_8v), 1800000);
	regulator_register_always_on(1, "fixed-dummy", fixed_supplies_3_3v,
				     ARRAY_SIZE(fixed_supplies_3_3v), 3300000);

	nvmem_add_cell_table(&davinci_nvmem_cell_table);
	nvmem_add_cell_lookups(&davinci_nvmem_cell_lookup, 1);

	evm_init_i2c();
	davinci_serial_init(dm365_serial_device);

	dm365evm_emac_configure();
	dm365evm_mmc_configure();

	davinci_setup_mmc(0, &dm365evm_mmc_config);

	dm365_init_video(&vpfe_cfg, &dm365evm_display_cfg);

	/* maybe setup mmc1/etc ... _after_ mmc0 */
	evm_init_cpld();

#ifdef CONFIG_SND_SOC_DM365_AIC3X_CODEC
	dm365_init_asp();
#elif defined(CONFIG_SND_SOC_DM365_VOICE_CODEC)
	dm365_init_vc();
#endif
	dm365_init_rtc();
	dm365_init_ks(&dm365evm_ks_data);

	dm365_init_spi0(BIT(0), dm365_evm_spi_info,
			ARRAY_SIZE(dm365_evm_spi_info));
}

MACHINE_START(DAVINCI_DM365_EVM, "DaVinci DM365 EVM")
	.atag_offset	= 0x100,
	.map_io		= dm365_evm_map_io,
	.init_irq	= dm365_init_irq,
	.init_time	= dm365_init_time,
	.init_machine	= dm365_evm_init,
	.init_late	= davinci_init_late,
	.dma_zone_size	= SZ_128M,
MACHINE_END

