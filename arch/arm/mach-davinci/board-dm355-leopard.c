// SPDX-License-Identifier: GPL-2.0-only
/*
 * DM355 leopard board support
 *
 * Based on board-dm355-evm.c
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
#include <linux/clk.h>
#include <linux/spi/spi.h>
#include <linux/spi/eeprom.h>
#include <linux/platform_data/i2c-davinci.h>
#include <linux/platform_data/mmc-davinci.h>
#include <linux/platform_data/mtd-davinci.h>
#include <linux/platform_data/usb-davinci.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "common.h"
#include "serial.h"
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
	.engine_type		= NAND_ECC_ENGINE_TYPE_ON_HOST,
	.ecc_placement		= NAND_ECC_PLACEMENT_INTERLEAVED,
	.ecc_bits		= 4,
	.bbt_options		= NAND_BBT_USE_FLASH,
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

static struct davinci_i2c_platform_data i2c_pdata = {
	.bus_freq	= 400	/* kHz */,
	.bus_delay	= 0	/* usec */,
};

static int leopard_mmc_gpio = -EINVAL;

static void dm355leopard_mmcsd_gpios(unsigned gpio)
{
	gpio_request(gpio + 0, "mmc0_ro");
	gpio_request(gpio + 1, "mmc0_cd");
	gpio_request(gpio + 2, "mmc1_ro");
	gpio_request(gpio + 3, "mmc1_cd");

	/* we "know" these are input-only so we don't
	 * need to call gpio_direction_input()
	 */

	leopard_mmc_gpio = gpio;
}

static struct i2c_board_info dm355leopard_i2c_info[] = {
	{ I2C_BOARD_INFO("dm355leopard_msp", 0x25),
		.platform_data = dm355leopard_mmcsd_gpios,
		/* plus irq */ },
	/* { I2C_BOARD_INFO("tlv320aic3x", 0x1b), }, */
	/* { I2C_BOARD_INFO("tvp5146", 0x5d), }, */
};

static void __init leopard_init_i2c(void)
{
	davinci_init_i2c(&i2c_pdata);

	gpio_request(5, "dm355leopard_msp");
	gpio_direction_input(5);
	dm355leopard_i2c_info[0].irq = gpio_to_irq(5);

	i2c_register_board_info(1, dm355leopard_i2c_info,
			ARRAY_SIZE(dm355leopard_i2c_info));
}

static struct resource dm355leopard_dm9000_rsrc[] = {
	{
		/* addr */
		.start	= 0x04000000,
		.end	= 0x04000001,
		.flags	= IORESOURCE_MEM,
	}, {
		/* data */
		.start	= 0x04000016,
		.end	= 0x04000017,
		.flags	= IORESOURCE_MEM,
	}, {
		.flags	= IORESOURCE_IRQ
			| IORESOURCE_IRQ_HIGHEDGE /* rising (active high) */,
	},
};

static struct platform_device dm355leopard_dm9000 = {
	.name		= "dm9000",
	.id		= -1,
	.resource	= dm355leopard_dm9000_rsrc,
	.num_resources	= ARRAY_SIZE(dm355leopard_dm9000_rsrc),
};

static struct platform_device *davinci_leopard_devices[] __initdata = {
	&dm355leopard_dm9000,
	&davinci_nand_device,
};

static void __init dm355_leopard_map_io(void)
{
	dm355_init();
}

static int dm355leopard_mmc_get_cd(int module)
{
	if (!gpio_is_valid(leopard_mmc_gpio))
		return -ENXIO;
	/* low == card present */
	return !gpio_get_value_cansleep(leopard_mmc_gpio + 2 * module + 1);
}

static int dm355leopard_mmc_get_ro(int module)
{
	if (!gpio_is_valid(leopard_mmc_gpio))
		return -ENXIO;
	/* high == card's write protect switch active */
	return gpio_get_value_cansleep(leopard_mmc_gpio + 2 * module + 0);
}

static struct davinci_mmc_config dm355leopard_mmc_config = {
	.get_cd		= dm355leopard_mmc_get_cd,
	.get_ro		= dm355leopard_mmc_get_ro,
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

static const struct spi_board_info dm355_leopard_spi_info[] __initconst = {
	{
		.modalias	= "at25",
		.platform_data	= &at25640a,
		.max_speed_hz	= 10 * 1000 * 1000,	/* at 3v3 */
		.bus_num	= 0,
		.chip_select	= 0,
		.mode		= SPI_MODE_0,
	},
};

static __init void dm355_leopard_init(void)
{
	struct clk *aemif;
	int ret;

	dm355_register_clocks();

	ret = dm355_gpio_register();
	if (ret)
		pr_warn("%s: GPIO init failed: %d\n", __func__, ret);

	gpio_request(9, "dm9000");
	gpio_direction_input(9);
	dm355leopard_dm9000_rsrc[2].start = gpio_to_irq(9);

	aemif = clk_get(&dm355leopard_dm9000.dev, "aemif");
	if (!WARN(IS_ERR(aemif), "unable to get AEMIF clock\n"))
		clk_prepare_enable(aemif);

	platform_add_devices(davinci_leopard_devices,
			     ARRAY_SIZE(davinci_leopard_devices));
	leopard_init_i2c();
	davinci_serial_init(dm355_serial_device);

	/* NOTE:  NAND flash timings set by the UBL are slower than
	 * needed by MT29F16G08FAA chips ... EMIF.A1CR is 0x40400204
	 * but could be 0x0400008c for about 25% faster page reads.
	 */

	gpio_request(2, "usb_id_toggle");
	gpio_direction_output(2, USB_ID_VALUE);
	/* irlml6401 switches over 1A in under 8 msec */
	davinci_setup_usb(1000, 8);

	davinci_setup_mmc(0, &dm355leopard_mmc_config);
	davinci_setup_mmc(1, &dm355leopard_mmc_config);

	dm355_init_spi0(BIT(0), dm355_leopard_spi_info,
			ARRAY_SIZE(dm355_leopard_spi_info));
}

MACHINE_START(DM355_LEOPARD, "DaVinci DM355 leopard")
	.atag_offset  = 0x100,
	.map_io	      = dm355_leopard_map_io,
	.init_irq     = dm355_init_irq,
	.init_time	= dm355_init_time,
	.init_machine = dm355_leopard_init,
	.init_late	= davinci_init_late,
	.dma_zone_size	= SZ_128M,
MACHINE_END
