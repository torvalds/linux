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
 * 	Additionaly realtime clock, IR remote control receiver,
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
#include <linux/mtd/partitions.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/dm644x.h>
#include <mach/common.h>
#include <mach/i2c.h>
#include <mach/serial.h>
#include <mach/mux.h>
#include <mach/nand.h>
#include <mach/mmc.h>
#include <mach/usb.h>

#define NEUROS_OSD2_PHY_MASK		0x2
#define NEUROS_OSD2_MDIO_FREQUENCY	2200000 /* PHY bus frequency */

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
	.parts		= davinci_ntosd2_nandflash_partition,
	.nr_parts	= ARRAY_SIZE(davinci_ntosd2_nandflash_partition),
	.ecc_mode	= NAND_ECC_HW,
	.options	= NAND_USE_FLASH_BBT,
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

static struct snd_platform_data dm644x_ntosd2_snd_data;

static struct gpio_led ntosd2_leds[] = {
	{ .name = "led1_green", .gpio = GPIO(10), },
	{ .name = "led1_red",   .gpio = GPIO(11), },
	{ .name = "led2_green", .gpio = GPIO(12), },
	{ .name = "led2_red",   .gpio = GPIO(13), },
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

static struct davinci_uart_config uart_config __initdata = {
	.enabled_uarts = (1 << 0),
};

static void __init davinci_ntosd2_map_io(void)
{
	dm644x_init();
}

/*
 I2C initialization
*/
static struct davinci_i2c_platform_data ntosd2_i2c_pdata = {
	.bus_freq	= 20 /* kHz */,
	.bus_delay	= 100 /* usec */,
};

static struct i2c_board_info __initdata ntosd2_i2c_info[] =  {
};

static	int ntosd2_init_i2c(void)
{
	int	status;

	davinci_init_i2c(&ntosd2_i2c_pdata);
	status = gpio_request(NTOSD2_MSP430_IRQ, ntosd2_i2c_info[0].type);
	if (status == 0) {
		status = gpio_direction_input(NTOSD2_MSP430_IRQ);
		if (status == 0) {
			status = gpio_to_irq(NTOSD2_MSP430_IRQ);
			if (status > 0) {
				ntosd2_i2c_info[0].irq = status;
				i2c_register_board_info(1,
					ntosd2_i2c_info,
					ARRAY_SIZE(ntosd2_i2c_info));
			}
		}
	}
	return status;
}

static struct davinci_mmc_config davinci_ntosd2_mmc_config = {
	.wires		= 4,
	.version	= MMC_CTLR_VERSION_1
};


#if defined(CONFIG_BLK_DEV_PALMCHIP_BK3710) || \
	defined(CONFIG_BLK_DEV_PALMCHIP_BK3710_MODULE)
#define HAS_ATA 1
#else
#define HAS_ATA 0
#endif

#if defined(CONFIG_MTD_NAND_DAVINCI) || \
	defined(CONFIG_MTD_NAND_DAVINCI_MODULE)
#define HAS_NAND 1
#else
#define HAS_NAND 0
#endif

static __init void davinci_ntosd2_init(void)
{
	struct clk *aemif_clk;
	struct davinci_soc_info *soc_info = &davinci_soc_info;
	int	status;

	aemif_clk = clk_get(NULL, "aemif");
	clk_enable(aemif_clk);

	if (HAS_ATA) {
		if (HAS_NAND)
			pr_warning("WARNING: both IDE and Flash are "
				"enabled, but they share AEMIF pins.\n"
				"\tDisable IDE for NAND/NOR support.\n");
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

	/* Initialize I2C interface specific for this board */
	status = ntosd2_init_i2c();
	if (status < 0)
		pr_warning("davinci_ntosd2_init: msp430 irq setup failed:"
						"	 %d\n", status);

	davinci_serial_init(&uart_config);
	dm644x_init_asp(&dm644x_ntosd2_snd_data);

	soc_info->emac_pdata->phy_mask = NEUROS_OSD2_PHY_MASK;
	soc_info->emac_pdata->mdio_max_freq = NEUROS_OSD2_MDIO_FREQUENCY;

	davinci_setup_usb(1000, 8);
	/*
	 * Mux the pins to be GPIOs, VLYNQEN is already done at startup.
	 * The AEAWx are five new AEAW pins that can be muxed by separately.
	 * They are a bitmask for GPIO management. According TI
	 * documentation (http://www.ti.com/lit/gpn/tms320dm6446) to employ
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
	.phys_io	= IO_PHYS,
	.io_pg_offst	= (__IO_ADDRESS(IO_PHYS) >> 18) & 0xfffc,
	.boot_params	= (DAVINCI_DDR_BASE + 0x100),
	.map_io		 = davinci_ntosd2_map_io,
	.init_irq	= davinci_irq_init,
	.timer		= &davinci_timer,
	.init_machine = davinci_ntosd2_init,
MACHINE_END
