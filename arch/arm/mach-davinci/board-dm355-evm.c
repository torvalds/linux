/*
 * TI DaVinci EVM board support
 *
 * Author: Kevin Hilman, Deep Root Systems, LLC
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/rawnand.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/gpio/machine.h>
#include <linux/clk.h>
#include <linux/dm9000.h>
#include <linux/videodev2.h>
#include <media/i2c/tvp514x.h>
#include <linux/spi/spi.h>
#include <linux/spi/eeprom.h>
#include <linux/platform_data/gpio-davinci.h>
#include <linux/platform_data/i2c-davinci.h>
#include <linux/platform_data/mtd-davinci.h>
#include <linux/platform_data/mmc-davinci.h>
#include <linux/platform_data/usb-davinci.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/serial.h>
#include <mach/common.h>

#include "davinci.h"

/* NOTE:  this is geared for the standard config, with a socketed
 * 2 GByte Micron NAND (MT29F16G08FAA) using 128KB sectors.  If you
 * swap chips, maybe with a different block size, partitioning may
 * need to be changed.
 */
#define NAND_BLOCK_SIZE		SZ_128K

static struct mtd_partition davinci_nand_partitions[] = {
	{
		/* UBL (a few copies) plus U-Boot */
		.name		= "bootloader",
		.offset		= 0,
		.size		= 15 * NAND_BLOCK_SIZE,
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
	}, {
		/* U-Boot environment */
		.name		= "params",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 1 * NAND_BLOCK_SIZE,
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
	.ecc_mode		= NAND_ECC_HW,
	.bbt_options		= NAND_BBT_USE_FLASH,
	.ecc_bits		= 4,
};

static struct resource davinci_nand_resources[] = {
	{
		.start		= DM355_ASYNC_EMIF_DATA_CE0_BASE,
		.end		= DM355_ASYNC_EMIF_DATA_CE0_BASE + SZ_32M - 1,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= DM355_ASYNC_EMIF_CONTROL_BASE,
		.end		= DM355_ASYNC_EMIF_CONTROL_BASE + SZ_4K - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device davinci_nand_device = {
	.name			= "davinci_nand",
	.id			= 0,

	.num_resources		= ARRAY_SIZE(davinci_nand_resources),
	.resource		= davinci_nand_resources,

	.dev			= {
		.platform_data	= &davinci_nand_data,
	},
};

#define DM355_I2C_SDA_PIN	GPIO_TO_PIN(0, 15)
#define DM355_I2C_SCL_PIN	GPIO_TO_PIN(0, 14)

static struct gpiod_lookup_table i2c_recovery_gpiod_table = {
	.dev_id = "i2c_davinci.1",
	.table = {
		GPIO_LOOKUP("davinci_gpio.0", DM355_I2C_SDA_PIN, "sda",
			    GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN),
		GPIO_LOOKUP("davinci_gpio.0", DM355_I2C_SCL_PIN, "scl",
			    GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN),
	},
};

static struct davinci_i2c_platform_data i2c_pdata = {
	.bus_freq	= 400	/* kHz */,
	.bus_delay	= 0	/* usec */,
	.gpio_recovery	= true,
};

static int dm355evm_mmc_gpios = -EINVAL;

static void dm355evm_mmcsd_gpios(unsigned gpio)
{
	gpio_request(gpio + 0, "mmc0_ro");
	gpio_request(gpio + 1, "mmc0_cd");
	gpio_request(gpio + 2, "mmc1_ro");
	gpio_request(gpio + 3, "mmc1_cd");

	/* we "know" these are input-only so we don't
	 * need to call gpio_direction_input()
	 */

	dm355evm_mmc_gpios = gpio;
}

static struct i2c_board_info dm355evm_i2c_info[] = {
	{	I2C_BOARD_INFO("dm355evm_msp", 0x25),
		.platform_data = dm355evm_mmcsd_gpios,
	},
	/* { plus irq  }, */
	{ I2C_BOARD_INFO("tlv320aic33", 0x1b), },
};

static void __init evm_init_i2c(void)
{
	gpiod_add_lookup_table(&i2c_recovery_gpiod_table);
	davinci_init_i2c(&i2c_pdata);

	gpio_request(5, "dm355evm_msp");
	gpio_direction_input(5);
	dm355evm_i2c_info[0].irq = gpio_to_irq(5);

	i2c_register_board_info(1, dm355evm_i2c_info,
			ARRAY_SIZE(dm355evm_i2c_info));
}

static struct resource dm355evm_dm9000_rsrc[] = {
	{
		/* addr */
		.start	= 0x04014000,
		.end	= 0x04014001,
		.flags	= IORESOURCE_MEM,
	}, {
		/* data */
		.start	= 0x04014002,
		.end	= 0x04014003,
		.flags	= IORESOURCE_MEM,
	}, {
		.flags	= IORESOURCE_IRQ
			| IORESOURCE_IRQ_HIGHEDGE /* rising (active high) */,
	},
};

static struct dm9000_plat_data dm335evm_dm9000_platdata;

static struct platform_device dm355evm_dm9000 = {
	.name		= "dm9000",
	.id		= -1,
	.resource	= dm355evm_dm9000_rsrc,
	.num_resources	= ARRAY_SIZE(dm355evm_dm9000_rsrc),
	.dev		= {
		.platform_data = &dm335evm_dm9000_platdata,
	},
};

static struct tvp514x_platform_data tvp5146_pdata = {
	.clk_polarity = 0,
	.hs_polarity = 1,
	.vs_polarity = 1
};

#define TVP514X_STD_ALL	(V4L2_STD_NTSC | V4L2_STD_PAL)
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
	}
};

static struct vpfe_config vpfe_cfg = {
	.num_subdevs = ARRAY_SIZE(vpfe_sub_devs),
	.i2c_adapter_id = 1,
	.sub_devs = vpfe_sub_devs,
	.card_name = "DM355 EVM",
	.ccdc = "DM355 CCDC",
};

/* venc standards timings */
static struct vpbe_enc_mode_info dm355evm_enc_preset_timing[] = {
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
		.upper_margin	= 0x16
	},
};

#define VENC_STD_ALL	(V4L2_STD_NTSC | V4L2_STD_PAL)

/*
 * The outputs available from VPBE + ecnoders. Keep the
 * the order same as that of encoders. First those from venc followed by that
 * from encoders. Index in the output refers to index on a particular encoder.
 * Driver uses this index to pass it to encoder when it supports more than
 * one output. Application uses index of the array to set an output.
 */
static struct vpbe_output dm355evm_vpbe_outputs[] = {
	{
		.output		= {
			.index		= 0,
			.name		= "Composite",
			.type		= V4L2_OUTPUT_TYPE_ANALOG,
			.std		= VENC_STD_ALL,
			.capabilities	= V4L2_OUT_CAP_STD,
		},
		.subdev_name	= DM355_VPBE_VENC_SUBDEV_NAME,
		.default_mode	= "ntsc",
		.num_modes	= ARRAY_SIZE(dm355evm_enc_preset_timing),
		.modes		= dm355evm_enc_preset_timing,
		.if_params	= MEDIA_BUS_FMT_FIXED,
	},
};

static struct vpbe_config dm355evm_display_cfg = {
	.module_name	= "dm355-vpbe-display",
	.i2c_adapter_id	= 1,
	.osd		= {
		.module_name	= DM355_VPBE_OSD_SUBDEV_NAME,
	},
	.venc		= {
		.module_name	= DM355_VPBE_VENC_SUBDEV_NAME,
	},
	.num_outputs	= ARRAY_SIZE(dm355evm_vpbe_outputs),
	.outputs	= dm355evm_vpbe_outputs,
};

static struct platform_device *davinci_evm_devices[] __initdata = {
	&dm355evm_dm9000,
	&davinci_nand_device,
};

static void __init dm355_evm_map_io(void)
{
	dm355_init();
}

static int dm355evm_mmc_get_cd(int module)
{
	if (!gpio_is_valid(dm355evm_mmc_gpios))
		return -ENXIO;
	/* low == card present */
	return !gpio_get_value_cansleep(dm355evm_mmc_gpios + 2 * module + 1);
}

static int dm355evm_mmc_get_ro(int module)
{
	if (!gpio_is_valid(dm355evm_mmc_gpios))
		return -ENXIO;
	/* high == card's write protect switch active */
	return gpio_get_value_cansleep(dm355evm_mmc_gpios + 2 * module + 0);
}

static struct davinci_mmc_config dm355evm_mmc_config = {
	.get_cd		= dm355evm_mmc_get_cd,
	.get_ro		= dm355evm_mmc_get_ro,
	.wires		= 4,
	.max_freq       = 50000000,
	.caps           = MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED,
};

/* Don't connect anything to J10 unless you're only using USB host
 * mode *and* have to do so with some kind of gender-bender.  If
 * you have proper Mini-B or Mini-A cables (or Mini-A adapters)
 * the ID pin won't need any help.
 */
#define USB_ID_VALUE	1	/* ID pulled low */

static struct spi_eeprom at25640a = {
	.byte_len	= SZ_64K / 8,
	.name		= "at25640a",
	.page_size	= 32,
	.flags		= EE_ADDR2,
};

static const struct spi_board_info dm355_evm_spi_info[] __initconst = {
	{
		.modalias	= "at25",
		.platform_data	= &at25640a,
		.max_speed_hz	= 10 * 1000 * 1000,	/* at 3v3 */
		.bus_num	= 0,
		.chip_select	= 0,
		.mode		= SPI_MODE_0,
	},
};

static __init void dm355_evm_init(void)
{
	struct clk *aemif;
	int ret;

	dm355_register_clocks();

	ret = dm355_gpio_register();
	if (ret)
		pr_warn("%s: GPIO init failed: %d\n", __func__, ret);

	gpio_request(1, "dm9000");
	gpio_direction_input(1);
	dm355evm_dm9000_rsrc[2].start = gpio_to_irq(1);

	aemif = clk_get(&dm355evm_dm9000.dev, "aemif");
	if (!WARN(IS_ERR(aemif), "unable to get AEMIF clock\n"))
		clk_prepare_enable(aemif);

	platform_add_devices(davinci_evm_devices,
			     ARRAY_SIZE(davinci_evm_devices));
	evm_init_i2c();
	davinci_serial_init(dm355_serial_device);

	/* NOTE:  NAND flash timings set by the UBL are slower than
	 * needed by MT29F16G08FAA chips ... EMIF.A1CR is 0x40400204
	 * but could be 0x0400008c for about 25% faster page reads.
	 */

	gpio_request(2, "usb_id_toggle");
	gpio_direction_output(2, USB_ID_VALUE);
	/* irlml6401 switches over 1A in under 8 msec */
	davinci_setup_usb(1000, 8);

	davinci_setup_mmc(0, &dm355evm_mmc_config);
	davinci_setup_mmc(1, &dm355evm_mmc_config);

	dm355_init_video(&vpfe_cfg, &dm355evm_display_cfg);

	dm355_init_spi0(BIT(0), dm355_evm_spi_info,
			ARRAY_SIZE(dm355_evm_spi_info));

	/* DM335 EVM uses ASP1; line-out is a stereo mini-jack */
	dm355_init_asp1(ASP1_TX_EVT_EN | ASP1_RX_EVT_EN);
}

MACHINE_START(DAVINCI_DM355_EVM, "DaVinci DM355 EVM")
	.atag_offset  = 0x100,
	.map_io	      = dm355_evm_map_io,
	.init_irq     = davinci_irq_init,
	.init_time	= dm355_init_time,
	.init_machine = dm355_evm_init,
	.init_late	= davinci_init_late,
	.dma_zone_size	= SZ_128M,
MACHINE_END
