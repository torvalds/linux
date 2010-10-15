/*
 * TI DaVinci EVM board support
 *
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/i2c/pcf857x.h>
#include <linux/i2c/at24.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/phy.h>
#include <linux/clk.h>
#include <linux/videodev2.h>

#include <media/tvp514x.h>

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

#define DM644X_EVM_PHY_MASK		(0x2)
#define DM644X_EVM_MDIO_FREQUENCY	(2200000) /* PHY bus frequency */

#define LXT971_PHY_ID	(0x001378e2)
#define LXT971_PHY_MASK	(0xfffffff0)

static struct mtd_partition davinci_evm_norflash_partitions[] = {
	/* bootloader (UBL, U-Boot, etc) in first 5 sectors */
	{
		.name		= "bootloader",
		.offset		= 0,
		.size		= 5 * SZ_64K,
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
	},
	/* bootloader params in the next 1 sectors */
	{
		.name		= "params",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_64K,
		.mask_flags	= 0,
	},
	/* kernel */
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_2M,
		.mask_flags	= 0
	},
	/* file system */
	{
		.name		= "filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0
	}
};

static struct physmap_flash_data davinci_evm_norflash_data = {
	.width		= 2,
	.parts		= davinci_evm_norflash_partitions,
	.nr_parts	= ARRAY_SIZE(davinci_evm_norflash_partitions),
};

/* NOTE: CFI probe will correctly detect flash part as 32M, but EMIF
 * limits addresses to 16M, so using addresses past 16M will wrap */
static struct resource davinci_evm_norflash_resource = {
	.start		= DM644X_ASYNC_EMIF_DATA_CE0_BASE,
	.end		= DM644X_ASYNC_EMIF_DATA_CE0_BASE + SZ_16M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device davinci_evm_norflash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &davinci_evm_norflash_data,
	},
	.num_resources	= 1,
	.resource	= &davinci_evm_norflash_resource,
};

/* DM644x EVM includes a 64 MByte small-page NAND flash (16K blocks).
 * It may used instead of the (default) NOR chip to boot, using TI's
 * tools to install the secondary boot loader (UBL) and U-Boot.
 */
static struct mtd_partition davinci_evm_nandflash_partition[] = {
	/* Bootloader layout depends on whose u-boot is installed, but we
	 * can hide all the details.
	 *  - block 0 for u-boot environment ... in mainline u-boot
	 *  - block 1 for UBL (plus up to four backup copies in blocks 2..5)
	 *  - blocks 6...? for u-boot
	 *  - blocks 16..23 for u-boot environment ... in TI's u-boot
	 */
	{
		.name		= "bootloader",
		.offset		= 0,
		.size		= SZ_256K + SZ_128K,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	/* Kernel */
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_4M,
		.mask_flags	= 0,
	},
	/* File system (older GIT kernels started this on the 5MB mark) */
	{
		.name		= "filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0,
	}
	/* A few blocks at end hold a flash BBT ... created by TI's CCS
	 * using flashwriter_nand.out, but ignored by TI's versions of
	 * Linux and u-boot.  We boot faster by using them.
	 */
};

static struct davinci_nand_pdata davinci_evm_nandflash_data = {
	.parts		= davinci_evm_nandflash_partition,
	.nr_parts	= ARRAY_SIZE(davinci_evm_nandflash_partition),
	.ecc_mode	= NAND_ECC_HW,
	.options	= NAND_USE_FLASH_BBT,
};

static struct resource davinci_evm_nandflash_resource[] = {
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

static struct platform_device davinci_evm_nandflash_device = {
	.name		= "davinci_nand",
	.id		= 0,
	.dev		= {
		.platform_data	= &davinci_evm_nandflash_data,
	},
	.num_resources	= ARRAY_SIZE(davinci_evm_nandflash_resource),
	.resource	= davinci_evm_nandflash_resource,
};

static u64 davinci_fb_dma_mask = DMA_BIT_MASK(32);

static struct platform_device davinci_fb_device = {
	.name		= "davincifb",
	.id		= -1,
	.dev = {
		.dma_mask		= &davinci_fb_dma_mask,
		.coherent_dma_mask      = DMA_BIT_MASK(32),
	},
	.num_resources = 0,
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
	},
};

static struct vpfe_config vpfe_cfg = {
	.num_subdevs = ARRAY_SIZE(vpfe_sub_devs),
	.i2c_adapter_id = 1,
	.sub_devs = vpfe_sub_devs,
	.card_name = "DM6446 EVM",
	.ccdc = "DM6446 CCDC",
};

static struct platform_device rtc_dev = {
	.name           = "rtc_davinci_evm",
	.id             = -1,
};

static struct snd_platform_data dm644x_evm_snd_data;

/*----------------------------------------------------------------------*/

/*
 * I2C GPIO expanders
 */

#define PCF_Uxx_BASE(x)	(DAVINCI_N_GPIO + ((x) * 8))


/* U2 -- LEDs */

static struct gpio_led evm_leds[] = {
	{ .name = "DS8", .active_low = 1,
		.default_trigger = "heartbeat", },
	{ .name = "DS7", .active_low = 1, },
	{ .name = "DS6", .active_low = 1, },
	{ .name = "DS5", .active_low = 1, },
	{ .name = "DS4", .active_low = 1, },
	{ .name = "DS3", .active_low = 1, },
	{ .name = "DS2", .active_low = 1,
		.default_trigger = "mmc0", },
	{ .name = "DS1", .active_low = 1,
		.default_trigger = "ide-disk", },
};

static const struct gpio_led_platform_data evm_led_data = {
	.num_leds	= ARRAY_SIZE(evm_leds),
	.leds		= evm_leds,
};

static struct platform_device *evm_led_dev;

static int
evm_led_setup(struct i2c_client *client, int gpio, unsigned ngpio, void *c)
{
	struct gpio_led *leds = evm_leds;
	int status;

	while (ngpio--) {
		leds->gpio = gpio++;
		leds++;
	}

	/* what an extremely annoying way to be forced to handle
	 * device unregistration ...
	 */
	evm_led_dev = platform_device_alloc("leds-gpio", 0);
	platform_device_add_data(evm_led_dev,
			&evm_led_data, sizeof evm_led_data);

	evm_led_dev->dev.parent = &client->dev;
	status = platform_device_add(evm_led_dev);
	if (status < 0) {
		platform_device_put(evm_led_dev);
		evm_led_dev = NULL;
	}
	return status;
}

static int
evm_led_teardown(struct i2c_client *client, int gpio, unsigned ngpio, void *c)
{
	if (evm_led_dev) {
		platform_device_unregister(evm_led_dev);
		evm_led_dev = NULL;
	}
	return 0;
}

static struct pcf857x_platform_data pcf_data_u2 = {
	.gpio_base	= PCF_Uxx_BASE(0),
	.setup		= evm_led_setup,
	.teardown	= evm_led_teardown,
};


/* U18 - A/V clock generator and user switch */

static int sw_gpio;

static ssize_t
sw_show(struct device *d, struct device_attribute *a, char *buf)
{
	char *s = gpio_get_value_cansleep(sw_gpio) ? "on\n" : "off\n";

	strcpy(buf, s);
	return strlen(s);
}

static DEVICE_ATTR(user_sw, S_IRUGO, sw_show, NULL);

static int
evm_u18_setup(struct i2c_client *client, int gpio, unsigned ngpio, void *c)
{
	int	status;

	/* export dip switch option */
	sw_gpio = gpio + 7;
	status = gpio_request(sw_gpio, "user_sw");
	if (status == 0)
		status = gpio_direction_input(sw_gpio);
	if (status == 0)
		status = device_create_file(&client->dev, &dev_attr_user_sw);
	else
		gpio_free(sw_gpio);
	if (status != 0)
		sw_gpio = -EINVAL;

	/* audio PLL:  48 kHz (vs 44.1 or 32), single rate (vs double) */
	gpio_request(gpio + 3, "pll_fs2");
	gpio_direction_output(gpio + 3, 0);

	gpio_request(gpio + 2, "pll_fs1");
	gpio_direction_output(gpio + 2, 0);

	gpio_request(gpio + 1, "pll_sr");
	gpio_direction_output(gpio + 1, 0);

	return 0;
}

static int
evm_u18_teardown(struct i2c_client *client, int gpio, unsigned ngpio, void *c)
{
	gpio_free(gpio + 1);
	gpio_free(gpio + 2);
	gpio_free(gpio + 3);

	if (sw_gpio > 0) {
		device_remove_file(&client->dev, &dev_attr_user_sw);
		gpio_free(sw_gpio);
	}
	return 0;
}

static struct pcf857x_platform_data pcf_data_u18 = {
	.gpio_base	= PCF_Uxx_BASE(1),
	.n_latch	= (1 << 3) | (1 << 2) | (1 << 1),
	.setup		= evm_u18_setup,
	.teardown	= evm_u18_teardown,
};


/* U35 - various I/O signals used to manage USB, CF, ATA, etc */

static int
evm_u35_setup(struct i2c_client *client, int gpio, unsigned ngpio, void *c)
{
	/* p0 = nDRV_VBUS (initial:  don't supply it) */
	gpio_request(gpio + 0, "nDRV_VBUS");
	gpio_direction_output(gpio + 0, 1);

	/* p1 = VDDIMX_EN */
	gpio_request(gpio + 1, "VDDIMX_EN");
	gpio_direction_output(gpio + 1, 1);

	/* p2 = VLYNQ_EN */
	gpio_request(gpio + 2, "VLYNQ_EN");
	gpio_direction_output(gpio + 2, 1);

	/* p3 = n3V3_CF_RESET (initial: stay in reset) */
	gpio_request(gpio + 3, "nCF_RESET");
	gpio_direction_output(gpio + 3, 0);

	/* (p4 unused) */

	/* p5 = 1V8_WLAN_RESET (initial: stay in reset) */
	gpio_request(gpio + 5, "WLAN_RESET");
	gpio_direction_output(gpio + 5, 1);

	/* p6 = nATA_SEL (initial: select) */
	gpio_request(gpio + 6, "nATA_SEL");
	gpio_direction_output(gpio + 6, 0);

	/* p7 = nCF_SEL (initial: deselect) */
	gpio_request(gpio + 7, "nCF_SEL");
	gpio_direction_output(gpio + 7, 1);

	/* irlml6401 switches over 1A, in under 8 msec;
	 * now it can be managed by nDRV_VBUS ...
	 */
	davinci_setup_usb(1000, 8);

	return 0;
}

static int
evm_u35_teardown(struct i2c_client *client, int gpio, unsigned ngpio, void *c)
{
	gpio_free(gpio + 7);
	gpio_free(gpio + 6);
	gpio_free(gpio + 5);
	gpio_free(gpio + 3);
	gpio_free(gpio + 2);
	gpio_free(gpio + 1);
	gpio_free(gpio + 0);
	return 0;
}

static struct pcf857x_platform_data pcf_data_u35 = {
	.gpio_base	= PCF_Uxx_BASE(2),
	.setup		= evm_u35_setup,
	.teardown	= evm_u35_teardown,
};

/*----------------------------------------------------------------------*/

/* Most of this EEPROM is unused, but U-Boot uses some data:
 *  - 0x7f00, 6 bytes Ethernet Address
 *  - 0x0039, 1 byte NTSC vs PAL (bit 0x80 == PAL)
 *  - ... newer boards may have more
 */

static struct at24_platform_data eeprom_info = {
	.byte_len	= (256*1024) / 8,
	.page_size	= 64,
	.flags		= AT24_FLAG_ADDR16,
	.setup          = davinci_get_mac_addr,
	.context	= (void *)0x7f00,
};

/*
 * MSP430 supports RTC, card detection, input from IR remote, and
 * a bit more.  It triggers interrupts on GPIO(7) from pressing
 * buttons on the IR remote, and for card detect switches.
 */
static struct i2c_client *dm6446evm_msp;

static int dm6446evm_msp_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	dm6446evm_msp = client;
	return 0;
}

static int dm6446evm_msp_remove(struct i2c_client *client)
{
	dm6446evm_msp = NULL;
	return 0;
}

static const struct i2c_device_id dm6446evm_msp_ids[] = {
	{ "dm6446evm_msp", 0, },
	{ /* end of list */ },
};

static struct i2c_driver dm6446evm_msp_driver = {
	.driver.name	= "dm6446evm_msp",
	.id_table	= dm6446evm_msp_ids,
	.probe		= dm6446evm_msp_probe,
	.remove		= dm6446evm_msp_remove,
};

static int dm6444evm_msp430_get_pins(void)
{
	static const char txbuf[2] = { 2, 4, };
	char buf[4];
	struct i2c_msg msg[2] = {
		{
			.addr = dm6446evm_msp->addr,
			.flags = 0,
			.len = 2,
			.buf = (void __force *)txbuf,
		},
		{
			.addr = dm6446evm_msp->addr,
			.flags = I2C_M_RD,
			.len = 4,
			.buf = buf,
		},
	};
	int status;

	if (!dm6446evm_msp)
		return -ENXIO;

	/* Command 4 == get input state, returns port 2 and port3 data
	 *   S Addr W [A] len=2 [A] cmd=4 [A]
	 *   RS Addr R [A] [len=4] A [cmd=4] A [port2] A [port3] N P
	 */
	status = i2c_transfer(dm6446evm_msp->adapter, msg, 2);
	if (status < 0)
		return status;

	dev_dbg(&dm6446evm_msp->dev,
		"PINS: %02x %02x %02x %02x\n",
		buf[0], buf[1], buf[2], buf[3]);

	return (buf[3] << 8) | buf[2];
}

static int dm6444evm_mmc_get_cd(int module)
{
	int status = dm6444evm_msp430_get_pins();

	return (status < 0) ? status : !(status & BIT(1));
}

static int dm6444evm_mmc_get_ro(int module)
{
	int status = dm6444evm_msp430_get_pins();

	return (status < 0) ? status : status & BIT(6 + 8);
}

static struct davinci_mmc_config dm6446evm_mmc_config = {
	.get_cd		= dm6444evm_mmc_get_cd,
	.get_ro		= dm6444evm_mmc_get_ro,
	.wires		= 4,
	.version	= MMC_CTLR_VERSION_1
};

static struct i2c_board_info __initdata i2c_info[] =  {
	{
		I2C_BOARD_INFO("dm6446evm_msp", 0x23),
	},
	{
		I2C_BOARD_INFO("pcf8574", 0x38),
		.platform_data	= &pcf_data_u2,
	},
	{
		I2C_BOARD_INFO("pcf8574", 0x39),
		.platform_data	= &pcf_data_u18,
	},
	{
		I2C_BOARD_INFO("pcf8574", 0x3a),
		.platform_data	= &pcf_data_u35,
	},
	{
		I2C_BOARD_INFO("24c256", 0x50),
		.platform_data	= &eeprom_info,
	},
	{
		I2C_BOARD_INFO("tlv320aic33", 0x1b),
	},
};

/* The msp430 uses a slow bitbanged I2C implementation (ergo 20 KHz),
 * which requires 100 usec of idle bus after i2c writes sent to it.
 */
static struct davinci_i2c_platform_data i2c_pdata = {
	.bus_freq	= 20 /* kHz */,
	.bus_delay	= 100 /* usec */,
	.sda_pin        = 44,
	.scl_pin        = 43,
};

static void __init evm_init_i2c(void)
{
	davinci_init_i2c(&i2c_pdata);
	i2c_add_driver(&dm6446evm_msp_driver);
	i2c_register_board_info(1, i2c_info, ARRAY_SIZE(i2c_info));
}

static struct platform_device *davinci_evm_devices[] __initdata = {
	&davinci_fb_device,
	&rtc_dev,
};

static struct davinci_uart_config uart_config __initdata = {
	.enabled_uarts = (1 << 0),
};

static void __init
davinci_evm_map_io(void)
{
	/* setup input configuration for VPFE input devices */
	dm644x_set_vpfe_config(&vpfe_cfg);
	dm644x_init();
}

static int davinci_phy_fixup(struct phy_device *phydev)
{
	unsigned int control;
	/* CRITICAL: Fix for increasing PHY signal drive strength for
	 * TX lockup issue. On DaVinci EVM, the Intel LXT971 PHY
	 * signal strength was low causing  TX to fail randomly. The
	 * fix is to Set bit 11 (Increased MII drive strength) of PHY
	 * register 26 (Digital Config register) on this phy. */
	control = phy_read(phydev, 26);
	phy_write(phydev, 26, (control | 0x800));
	return 0;
}

#if defined(CONFIG_BLK_DEV_PALMCHIP_BK3710) || \
    defined(CONFIG_BLK_DEV_PALMCHIP_BK3710_MODULE)
#define HAS_ATA 1
#else
#define HAS_ATA 0
#endif

#if defined(CONFIG_MTD_PHYSMAP) || \
    defined(CONFIG_MTD_PHYSMAP_MODULE)
#define HAS_NOR 1
#else
#define HAS_NOR 0
#endif

#if defined(CONFIG_MTD_NAND_DAVINCI) || \
    defined(CONFIG_MTD_NAND_DAVINCI_MODULE)
#define HAS_NAND 1
#else
#define HAS_NAND 0
#endif

static __init void davinci_evm_init(void)
{
	struct clk *aemif_clk;
	struct davinci_soc_info *soc_info = &davinci_soc_info;

	aemif_clk = clk_get(NULL, "aemif");
	clk_enable(aemif_clk);

	if (HAS_ATA) {
		if (HAS_NAND || HAS_NOR)
			pr_warning("WARNING: both IDE and Flash are "
				"enabled, but they share AEMIF pins.\n"
				"\tDisable IDE for NAND/NOR support.\n");
		davinci_init_ide();
	} else if (HAS_NAND || HAS_NOR) {
		davinci_cfg_reg(DM644X_HPIEN_DISABLE);
		davinci_cfg_reg(DM644X_ATAEN_DISABLE);

		/* only one device will be jumpered and detected */
		if (HAS_NAND) {
			platform_device_register(&davinci_evm_nandflash_device);
			evm_leds[7].default_trigger = "nand-disk";
			if (HAS_NOR)
				pr_warning("WARNING: both NAND and NOR flash "
					"are enabled; disable one of them.\n");
		} else if (HAS_NOR)
			platform_device_register(&davinci_evm_norflash_device);
	}

	platform_add_devices(davinci_evm_devices,
			     ARRAY_SIZE(davinci_evm_devices));
	evm_init_i2c();

	davinci_setup_mmc(0, &dm6446evm_mmc_config);

	davinci_serial_init(&uart_config);
	dm644x_init_asp(&dm644x_evm_snd_data);

	soc_info->emac_pdata->phy_mask = DM644X_EVM_PHY_MASK;
	soc_info->emac_pdata->mdio_max_freq = DM644X_EVM_MDIO_FREQUENCY;

	/* Register the fixup for PHY on DaVinci */
	phy_register_fixup_for_uid(LXT971_PHY_ID, LXT971_PHY_MASK,
					davinci_phy_fixup);

}

MACHINE_START(DAVINCI_EVM, "DaVinci DM644x EVM")
	/* Maintainer: MontaVista Software <source@mvista.com> */
	.boot_params  = (DAVINCI_DDR_BASE + 0x100),
	.map_io	      = davinci_evm_map_io,
	.init_irq     = davinci_irq_init,
	.timer	      = &davinci_timer,
	.init_machine = davinci_evm_init,
MACHINE_END
