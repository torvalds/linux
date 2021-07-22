/*
 * Neuros Technologies OSD2 board support
 *
 * Modified from original 644X-EVM board support.
 * 2008 (c) Neuros Technology, LLC.
 * 2009 (c) Jorge Luis Zapata Muga <jorgeluis.zapata@gmail.com>
 * 2009 (c) Andrey A. Porodko <Andrey.Porodko@gmail.com>
 *
 * The Neuros OSD 2.0 is the hardware component of the Neuros Open
 * Internet Television Platform. Hardware is very close to TI
 * DM644X-EVM board. It has:
 * 	DM6446M02 module with 256MB NAND, 256MB RAM, TLV320AIC32 AIC,
 * 	USB, Ethernet, SD/MMC, UART, THS8200, TVP7000 for video.
 * 	Additionally realtime clock, IR remote control receiver,
 * 	IR Blaster based on MSP430 (firmware although is different
 * 	from used in DM644X-EVM), internal ATA-6 3.5” HDD drive
 * 	with PATA interface, two muxed red-green leds.
 *
 * For more information please refer to
 * 		http://wiki.neurostechnology.com/index.php/OSD_2.0_HD
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_data/gpio-davinci.h>
#include <linux/platform_data/i2c-davinci.h>
#include <linux/platform_data/mmc-davinci.h>
#include <linux/platform_data/mtd-davinci.h>
#include <linux/platform_data/usb-davinci.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/common.h>
#include <mach/serial.h>
#include <mach/mux.h>

#include "davinci.h"

#define NEUROS_OSD2_PHY_ID		"davinci_mdio-0:01"
#define LXT971_PHY_ID			0x001378e2
#define LXT971_PHY_MASK			0xfffffff0

#define	NTOSD2_AUDIOSOC_I2C_ADDR	0x18
#define	NTOSD2_MSP430_I2C_ADDR		0x59
#define	NTOSD2_MSP430_IRQ		2

/* Neuros OSD2 has a Samsung 256 MByte NAND flash (Dev ID of 0xAA,
 * 2048 blocks in the device, 64 pages per block, 2048 bytes per
 * page.
 */

#define NAND_BLOCK_SIZE		SZ_128K

static struct mtd_partition davinci_ntosd2_nandflash_partition[] = {
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
		/* Kernel */
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_4M,
		.mask_flags	= 0,
	}, {
		/* File System */
		.name		= "filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0,
	}
	/* A few blocks at end hold a flash Bad Block Table. */
};

static struct davinci_nand_pdata davinci_ntosd2_nandflash_data = {
	.core_chipsel	= 0,
	.parts		= davinci_ntosd2_nandflash_partition,
	.nr_parts	= ARRAY_SIZE(davinci_ntosd2_nandflash_partition),
	.engine_type	= NAND_ECC_ENGINE_TYPE_ON_HOST,
	.ecc_bits	= 1,
	.bbt_options	= NAND_BBT_USE_FLASH,
};

static struct resource davinci_ntosd2_nandflash_resource[] = {
	{
		.start		= DM644X_ASYNC_EMIF_DATA_CE0_BASE,
		.end		= DM644X_ASYNC_EMIF_DATA_CE0_BASE + SZ_16M - 1,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= DM644X_ASYNC_EMIF_CONTROL_BASE,
		.end		= DM644X_ASYNC_EMIF_CONTROL_BASE + SZ_4K - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device davinci_ntosd2_nandflash_device = {
	.name		= "davinci_nand",
	.id		= 0,
	.dev		= {
		.platform_data	= &davinci_ntosd2_nandflash_data,
	},
	.num_resources	= ARRAY_SIZE(davinci_ntosd2_nandflash_resource),
	.resource	= davinci_ntosd2_nandflash_resource,
};

static u64 davinci_fb_dma_mask = DMA_BIT_MASK(32);

static struct platform_device davinci_fb_device = {
	.name		= "davincifb",
	.id		= -1,
	.dev = {
		.dma_mask		= &davinci_fb_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources = 0,
};

static const struct gpio_led ntosd2_leds[] = {
	{ .name = "led1_green", .gpio = 10, },
	{ .name = "led1_red",   .gpio = 11, },
	{ .name = "led2_green", .gpio = 12, },
	{ .name = "led2_red",   .gpio = 13, },
};

static struct gpio_led_platform_data ntosd2_leds_data = {
	.num_leds	= ARRAY_SIZE(ntosd2_leds),
	.leds		= ntosd2_leds,
};

static struct platform_device ntosd2_leds_dev = {
	.name = "leds-gpio",
	.id   = -1,
	.dev = {
		.platform_data 		= &ntosd2_leds_data,
	},
};


static struct platform_device *davinci_ntosd2_devices[] __initdata = {
	&davinci_fb_device,
	&ntosd2_leds_dev,
};

static void __init davinci_ntosd2_map_io(void)
{
	dm644x_init();
}

static struct davinci_mmc_config davinci_ntosd2_mmc_config = {
	.wires		= 4,
};

#define HAS_ATA		(IS_ENABLED(CONFIG_BLK_DEV_PALMCHIP_BK3710) || \
			 IS_ENABLED(CONFIG_PATA_BK3710))

#define HAS_NAND	IS_ENABLED(CONFIG_MTD_NAND_DAVINCI)

static __init void davinci_ntosd2_init(void)
{
	int ret;
	struct clk *aemif_clk;
	struct davinci_soc_info *soc_info = &davinci_soc_info;

	dm644x_register_clocks();

	dm644x_init_devices();

	ret = dm644x_gpio_register();
	if (ret)
		pr_warn("%s: GPIO init failed: %d\n", __func__, ret);

	aemif_clk = clk_get(NULL, "aemif");
	clk_prepare_enable(aemif_clk);

	if (HAS_ATA) {
		if (HAS_NAND)
			pr_warn("WARNING: both IDE and Flash are enabled, but they share AEMIF pins\n"
				"\tDisable IDE for NAND/NOR support\n");
		davinci_init_ide();
	} else if (HAS_NAND) {
		davinci_cfg_reg(DM644X_HPIEN_DISABLE);
		davinci_cfg_reg(DM644X_ATAEN_DISABLE);

		/* only one device will be jumpered and detected */
		if (HAS_NAND)
			platform_device_register(
					&davinci_ntosd2_nandflash_device);
	}

	platform_add_devices(davinci_ntosd2_devices,
				ARRAY_SIZE(davinci_ntosd2_devices));

	davinci_serial_init(dm644x_serial_device);
	dm644x_init_asp();

	soc_info->emac_pdata->phy_id = NEUROS_OSD2_PHY_ID;

	davinci_setup_usb(1000, 8);
	/*
	 * Mux the pins to be GPIOs, VLYNQEN is already done at startup.
	 * The AEAWx are five new AEAW pins that can be muxed by separately.
	 * They are a bitmask for GPIO management. According TI
	 * documentation (https://www.ti.com/lit/gpn/tms320dm6446) to employ
	 * gpio(10,11,12,13) for leds any combination of bits works except
	 * four last. So we are to reset all five.
	 */
	davinci_cfg_reg(DM644X_AEAW0);
	davinci_cfg_reg(DM644X_AEAW1);
	davinci_cfg_reg(DM644X_AEAW2);
	davinci_cfg_reg(DM644X_AEAW3);
	davinci_cfg_reg(DM644X_AEAW4);

	davinci_setup_mmc(0, &davinci_ntosd2_mmc_config);
}

MACHINE_START(NEUROS_OSD2, "Neuros OSD2")
	/* Maintainer: Neuros Technologies <neuros@groups.google.com> */
	.atag_offset	= 0x100,
	.map_io		 = davinci_ntosd2_map_io,
	.init_irq	= dm644x_init_irq,
	.init_time	= dm644x_init_time,
	.init_machine = davinci_ntosd2_init,
	.init_late	= davinci_init_late,
	.dma_zone_size	= SZ_128M,
MACHINE_END
