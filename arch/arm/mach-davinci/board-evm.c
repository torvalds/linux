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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/leds.h>

#include <linux/i2c.h>
#include <linux/i2c/pcf857x.h>
#include <linux/i2c/at24.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/io.h>

#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/i2c.h>

/* other misc. init functions */
void __init davinci_psc_init(void);
void __init davinci_irq_init(void);
void __init davinci_map_common_io(void);
void __init davinci_init_common_hw(void);

#if defined(CONFIG_MTD_PHYSMAP) || \
    defined(CONFIG_MTD_PHYSMAP_MODULE)

static struct mtd_partition davinci_evm_norflash_partitions[] = {
	/* bootloader (U-Boot, etc) in first 4 sectors */
	{
		.name		= "bootloader",
		.offset		= 0,
		.size		= 4 * SZ_64K,
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
	.start		= DAVINCI_ASYNC_EMIF_DATA_CE0_BASE,
	.end		= DAVINCI_ASYNC_EMIF_DATA_CE0_BASE + SZ_16M - 1,
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

#endif

#if defined(CONFIG_BLK_DEV_PALMCHIP_BK3710) || \
    defined(CONFIG_BLK_DEV_PALMCHIP_BK3710_MODULE)

static struct resource ide_resources[] = {
	{
		.start          = DAVINCI_CFC_ATA_BASE,
		.end            = DAVINCI_CFC_ATA_BASE + 0x7ff,
		.flags          = IORESOURCE_MEM,
	},
	{
		.start          = IRQ_IDE,
		.end            = IRQ_IDE,
		.flags          = IORESOURCE_IRQ,
	},
};

static u64 ide_dma_mask = DMA_BIT_MASK(32);

static struct platform_device ide_dev = {
	.name           = "palm_bk3710",
	.id             = -1,
	.resource       = ide_resources,
	.num_resources  = ARRAY_SIZE(ide_resources),
	.dev = {
		.dma_mask		= &ide_dma_mask,
		.coherent_dma_mask      = DMA_BIT_MASK(32),
	},
};

#endif

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

	/* irlml6401 sustains over 3A, switches 5V in under 8 msec */
	setup_usb(500, 8);

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
};

static struct i2c_board_info __initdata i2c_info[] =  {
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
	/* ALSO:
	 * - tvl320aic33 audio codec (0x1b)
	 * - msp430 microcontroller (0x23)
	 * - tvp5146 video decoder (0x5d)
	 */
};

/* The msp430 uses a slow bitbanged I2C implementation (ergo 20 KHz),
 * which requires 100 usec of idle bus after i2c writes sent to it.
 */
static struct davinci_i2c_platform_data i2c_pdata = {
	.bus_freq	= 20 /* kHz */,
	.bus_delay	= 100 /* usec */,
};

static void __init evm_init_i2c(void)
{
	davinci_init_i2c(&i2c_pdata);
	i2c_register_board_info(1, i2c_info, ARRAY_SIZE(i2c_info));
}

static struct platform_device *davinci_evm_devices[] __initdata = {
#if defined(CONFIG_MTD_PHYSMAP) || \
    defined(CONFIG_MTD_PHYSMAP_MODULE)
	&davinci_evm_norflash_device,
#endif
#if defined(CONFIG_BLK_DEV_PALMCHIP_BK3710) || \
    defined(CONFIG_BLK_DEV_PALMCHIP_BK3710_MODULE)
	&ide_dev,
#endif
};

static void __init
davinci_evm_map_io(void)
{
	davinci_map_common_io();
}

static __init void davinci_evm_init(void)
{
	davinci_psc_init();

#if defined(CONFIG_BLK_DEV_PALMCHIP_BK3710) || \
    defined(CONFIG_BLK_DEV_PALMCHIP_BK3710_MODULE)
#if defined(CONFIG_MTD_PHYSMAP) || \
    defined(CONFIG_MTD_PHYSMAP_MODULE)
	printk(KERN_WARNING "WARNING: both IDE and NOR flash are enabled, "
	       "but share pins.\n\t Disable IDE for NOR support.\n");
#endif
#endif

	platform_add_devices(davinci_evm_devices,
			     ARRAY_SIZE(davinci_evm_devices));
	evm_init_i2c();
}

static __init void davinci_evm_irq_init(void)
{
	davinci_init_common_hw();
	davinci_irq_init();
}

MACHINE_START(DAVINCI_EVM, "DaVinci EVM")
	/* Maintainer: MontaVista Software <source@mvista.com> */
	.phys_io      = IO_PHYS,
	.io_pg_offst  = (__IO_ADDRESS(IO_PHYS) >> 18) & 0xfffc,
	.boot_params  = (DAVINCI_DDR_BASE + 0x100),
	.map_io	      = davinci_evm_map_io,
	.init_irq     = davinci_evm_irq_init,
	.timer	      = &davinci_timer,
	.init_machine = davinci_evm_init,
MACHINE_END
