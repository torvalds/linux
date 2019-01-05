/*
 *  linux/arch/arm/mach-pxa/lubbock.c
 *
 *  Support for the Intel DBPXA250 Development Platform.
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/clkdev.h>
#include <linux/gpio.h>
#include <linux/gpio/gpio-reg.h>
#include <linux/gpio/machine.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/syscore_ops.h>
#include <linux/major.h>
#include <linux/fb.h>
#include <linux/interrupt.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/smc91x.h>
#include <linux/slab.h>
#include <linux/leds.h>

#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/spi/pxa2xx_spi.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/sizes.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/flash.h>

#include <asm/hardware/sa1111.h>

#include "pxa25x.h"
#include <mach/audio.h>
#include <mach/lubbock.h>
#include "udc.h"
#include <linux/platform_data/irda-pxaficp.h>
#include <linux/platform_data/video-pxafb.h>
#include <linux/platform_data/mmc-pxamci.h>
#include "pm.h"
#include <mach/smemc.h>

#include "generic.h"
#include "devices.h"

static unsigned long lubbock_pin_config[] __initdata = {
	GPIO15_nCS_1,	/* CS1 - Flash */
	GPIO78_nCS_2,	/* CS2 - Baseboard FGPA */
	GPIO79_nCS_3,	/* CS3 - SMC ethernet */
	GPIO80_nCS_4,	/* CS4 - SA1111 */

	/* SSP data pins */
	GPIO23_SSP1_SCLK,
	GPIO25_SSP1_TXD,
	GPIO26_SSP1_RXD,

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,

	/* LCD - 16bpp DSTN */
	GPIOxx_LCD_DSTN_16BPP,

	/* BTUART */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,
	GPIO45_BTUART_RTS,

	/* PC Card */
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO52_nPCE_1,
	GPIO53_nPCE_2,
	GPIO54_nPSKTSEL,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,

	/* MMC */
	GPIO6_MMC_CLK,
	GPIO8_MMC_CS0,

	/* SA1111 chip */
	GPIO11_3_6MHz,

	/* wakeup */
	GPIO1_GPIO | WAKEUP_ON_EDGE_RISE,
};

#define LUB_HEXLED		__LUB_REG(LUBBOCK_FPGA_PHYS + 0x010)

void lubbock_set_hexled(uint32_t value)
{
	LUB_HEXLED = value;
}

static struct gpio_chip *lubbock_misc_wr_gc;

void lubbock_set_misc_wr(unsigned int mask, unsigned int set)
{
	unsigned long m = mask, v = set;
	lubbock_misc_wr_gc->set_multiple(lubbock_misc_wr_gc, &m, &v);
}
EXPORT_SYMBOL(lubbock_set_misc_wr);

static int lubbock_udc_is_connected(void)
{
	return (LUB_MISC_RD & (1 << 9)) == 0;
}

static struct pxa2xx_udc_mach_info udc_info __initdata = {
	.udc_is_connected	= lubbock_udc_is_connected,
	// no D+ pullup; lubbock can't connect/disconnect in software
};

/* GPIOs for SA1111 PCMCIA */
static struct gpiod_lookup_table sa1111_pcmcia_gpio_table = {
	.dev_id = "1800",
	.table = {
		{ "sa1111", 0, "a0vpp", GPIO_ACTIVE_HIGH },
		{ "sa1111", 1, "a1vpp", GPIO_ACTIVE_HIGH },
		{ "sa1111", 2, "a0vcc", GPIO_ACTIVE_HIGH },
		{ "sa1111", 3, "a1vcc", GPIO_ACTIVE_HIGH },
		{ "lubbock", 14, "b0vcc", GPIO_ACTIVE_HIGH },
		{ "lubbock", 15, "b1vcc", GPIO_ACTIVE_HIGH },
		{ },
	},
};

static void lubbock_init_pcmcia(void)
{
	struct clk *clk;

	gpiod_add_lookup_table(&sa1111_pcmcia_gpio_table);

	/* Add an alias for the SA1111 PCMCIA clock */
	clk = clk_get_sys("pxa2xx-pcmcia", NULL);
	if (!IS_ERR(clk)) {
		clkdev_create(clk, NULL, "1800");
		clk_put(clk);
	}
}

static struct resource sa1111_resources[] = {
	[0] = {
		.start	= 0x10000000,
		.end	= 0x10001fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= LUBBOCK_SA1111_IRQ,
		.end	= LUBBOCK_SA1111_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct sa1111_platform_data sa1111_info = {
	.irq_base	= LUBBOCK_SA1111_IRQ_BASE,
	.disable_devs	= SA1111_DEVID_SAC,
};

static struct platform_device sa1111_device = {
	.name		= "sa1111",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(sa1111_resources),
	.resource	= sa1111_resources,
	.dev		= {
		.platform_data	= &sa1111_info,
	},
};

/* ADS7846 is connected through SSP ... and if your board has J5 populated,
 * you can select it to replace the ucb1400 by switching the touchscreen cable
 * (to J5) and poking board registers (as done below).  Else it's only useful
 * for the temperature sensors.
 */
static struct pxa2xx_spi_master pxa_ssp_master_info = {
	.num_chipselect	= 1,
};

static int lubbock_ads7846_pendown_state(void)
{
	/* TS_BUSY is bit 8 in LUB_MISC_RD, but pendown is irq-only */
	return 0;
}

static struct ads7846_platform_data ads_info = {
	.model			= 7846,
	.vref_delay_usecs	= 100,		/* internal, no cap */
	.get_pendown_state	= lubbock_ads7846_pendown_state,
	// .x_plate_ohms		= 500,	/* GUESS! */
	// .y_plate_ohms		= 500,	/* GUESS! */
};

static void ads7846_cs(u32 command)
{
	static const unsigned	TS_nCS = 1 << 11;
	lubbock_set_misc_wr(TS_nCS, (command == PXA2XX_CS_ASSERT) ? 0 : TS_nCS);
}

static struct pxa2xx_spi_chip ads_hw = {
	.tx_threshold		= 1,
	.rx_threshold		= 2,
	.cs_control		= ads7846_cs,
};

static struct spi_board_info spi_board_info[] __initdata = { {
	.modalias	= "ads7846",
	.platform_data	= &ads_info,
	.controller_data = &ads_hw,
	.irq		= LUBBOCK_BB_IRQ,
	.max_speed_hz	= 120000 /* max sample rate at 3V */
				* 26 /* command + data + overhead */,
	.bus_num	= 1,
	.chip_select	= 0,
},
};

static struct resource smc91x_resources[] = {
	[0] = {
		.name	= "smc91x-regs",
		.start	= 0x0c000c00,
		.end	= 0x0c0fffff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= LUBBOCK_ETH_IRQ,
		.end	= LUBBOCK_ETH_IRQ,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
	[2] = {
		.name	= "smc91x-attrib",
		.start	= 0x0e000000,
		.end	= 0x0e0fffff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct smc91x_platdata lubbock_smc91x_info = {
	.flags	= SMC91X_USE_16BIT | SMC91X_NOWAIT | SMC91X_IO_SHIFT_2,
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
	.dev		= {
		.platform_data = &lubbock_smc91x_info,
	},
};

static struct resource flash_resources[] = {
	[0] = {
		.start	= 0x00000000,
		.end	= SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0x04000000,
		.end	= 0x04000000 + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct mtd_partition lubbock_partitions[] = {
	{
		.name =		"Bootloader",
		.size =		0x00040000,
		.offset =	0,
		.mask_flags =	MTD_WRITEABLE  /* force read-only */
	},{
		.name =		"Kernel",
		.size =		0x00100000,
		.offset =	0x00040000,
	},{
		.name =		"Filesystem",
		.size =		MTDPART_SIZ_FULL,
		.offset =	0x00140000
	}
};

static struct flash_platform_data lubbock_flash_data[2] = {
	{
		.map_name	= "cfi_probe",
		.parts		= lubbock_partitions,
		.nr_parts	= ARRAY_SIZE(lubbock_partitions),
	}, {
		.map_name	= "cfi_probe",
		.parts		= NULL,
		.nr_parts	= 0,
	}
};

static struct platform_device lubbock_flash_device[2] = {
	{
		.name		= "pxa2xx-flash",
		.id		= 0,
		.dev = {
			.platform_data = &lubbock_flash_data[0],
		},
		.resource = &flash_resources[0],
		.num_resources = 1,
	},
	{
		.name		= "pxa2xx-flash",
		.id		= 1,
		.dev = {
			.platform_data = &lubbock_flash_data[1],
		},
		.resource = &flash_resources[1],
		.num_resources = 1,
	},
};

static struct resource lubbock_cplds_resources[] = {
	[0] = {
		.start	= LUBBOCK_FPGA_PHYS + 0xc0,
		.end	= LUBBOCK_FPGA_PHYS + 0xe0 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= PXA_GPIO_TO_IRQ(0),
		.end	= PXA_GPIO_TO_IRQ(0),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	},
	[2] = {
		.start	= LUBBOCK_IRQ(0),
		.end	= LUBBOCK_IRQ(6),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device lubbock_cplds_device = {
	.name		= "pxa_cplds_irqs",
	.id		= -1,
	.resource	= &lubbock_cplds_resources[0],
	.num_resources	= 3,
};


static struct platform_device *devices[] __initdata = {
	&sa1111_device,
	&smc91x_device,
	&lubbock_flash_device[0],
	&lubbock_flash_device[1],
	&lubbock_cplds_device,
};

static struct pxafb_mode_info sharp_lm8v31_mode = {
	.pixclock	= 270000,
	.xres		= 640,
	.yres		= 480,
	.bpp		= 16,
	.hsync_len	= 1,
	.left_margin	= 3,
	.right_margin	= 3,
	.vsync_len	= 1,
	.upper_margin	= 0,
	.lower_margin	= 0,
	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.cmap_greyscale	= 0,
};

static struct pxafb_mach_info sharp_lm8v31 = {
	.modes		= &sharp_lm8v31_mode,
	.num_modes	= 1,
	.cmap_inverse	= 0,
	.cmap_static	= 0,
	.lcd_conn	= LCD_COLOR_DSTN_16BPP | LCD_PCLK_EDGE_FALL |
			  LCD_AC_BIAS_FREQ(255),
};

#define	MMC_POLL_RATE		msecs_to_jiffies(1000)

static irq_handler_t mmc_detect_int;
static void *mmc_detect_int_data;
static struct timer_list mmc_timer;

static void lubbock_mmc_poll(struct timer_list *unused)
{
	unsigned long flags;

	/* clear any previous irq state, then ... */
	local_irq_save(flags);
	LUB_IRQ_SET_CLR &= ~(1 << 0);
	local_irq_restore(flags);

	/* poll until mmc/sd card is removed */
	if (LUB_IRQ_SET_CLR & (1 << 0))
		mod_timer(&mmc_timer, jiffies + MMC_POLL_RATE);
	else {
		(void) mmc_detect_int(LUBBOCK_SD_IRQ, mmc_detect_int_data);
		enable_irq(LUBBOCK_SD_IRQ);
	}
}

static irqreturn_t lubbock_detect_int(int irq, void *data)
{
	/* IRQ is level triggered; disable, and poll for removal */
	disable_irq(irq);
	mod_timer(&mmc_timer, jiffies + MMC_POLL_RATE);

	return mmc_detect_int(irq, data);
}

static int lubbock_mci_init(struct device *dev,
		irq_handler_t detect_int,
		void *data)
{
	/* detect card insert/eject */
	mmc_detect_int = detect_int;
	mmc_detect_int_data = data;
	timer_setup(&mmc_timer, lubbock_mmc_poll, 0);
	return request_irq(LUBBOCK_SD_IRQ, lubbock_detect_int,
			   0, "lubbock-sd-detect", data);
}

static int lubbock_mci_get_ro(struct device *dev)
{
	return (LUB_MISC_RD & (1 << 2)) != 0;
}

static void lubbock_mci_exit(struct device *dev, void *data)
{
	free_irq(LUBBOCK_SD_IRQ, data);
	del_timer_sync(&mmc_timer);
}

static struct pxamci_platform_data lubbock_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33|MMC_VDD_33_34,
	.detect_delay_ms	= 10,
	.init 			= lubbock_mci_init,
	.get_ro			= lubbock_mci_get_ro,
	.exit 			= lubbock_mci_exit,
};

static void lubbock_irda_transceiver_mode(struct device *dev, int mode)
{
	unsigned long flags;

	local_irq_save(flags);
	if (mode & IR_SIRMODE) {
		lubbock_set_misc_wr(BIT(4), 0);
	} else if (mode & IR_FIRMODE) {
		lubbock_set_misc_wr(BIT(4), BIT(4));
	}
	pxa2xx_transceiver_mode(dev, mode);
	local_irq_restore(flags);
}

static struct pxaficp_platform_data lubbock_ficp_platform_data = {
	.gpio_pwdown		= -1,
	.transceiver_cap	= IR_SIRMODE | IR_FIRMODE,
	.transceiver_mode	= lubbock_irda_transceiver_mode,
};

static void __init lubbock_init(void)
{
	int flashboot = (LUB_CONF_SWITCHES & 1);

	pxa2xx_mfp_config(ARRAY_AND_SIZE(lubbock_pin_config));

	lubbock_misc_wr_gc = gpio_reg_init(NULL, (void *)&LUB_MISC_WR,
					   -1, 16, "lubbock", 0, LUB_MISC_WR,
					   NULL, NULL, NULL);
	if (IS_ERR(lubbock_misc_wr_gc)) {
		pr_err("Lubbock: unable to register lubbock GPIOs: %ld\n",
		       PTR_ERR(lubbock_misc_wr_gc));
		lubbock_misc_wr_gc = NULL;
	}

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	lubbock_init_pcmcia();

	clk_add_alias("SA1111_CLK", NULL, "GPIO11_CLK", NULL);
	pxa_set_udc_info(&udc_info);
	pxa_set_fb_info(NULL, &sharp_lm8v31);
	pxa_set_mci_info(&lubbock_mci_platform_data);
	pxa_set_ficp_info(&lubbock_ficp_platform_data);
	pxa_set_ac97_info(NULL);

	lubbock_flash_data[0].width = lubbock_flash_data[1].width =
		(__raw_readl(BOOT_DEF) & 1) ? 2 : 4;
	/* Compensate for the nROMBT switch which swaps the flash banks */
	printk(KERN_NOTICE "Lubbock configured to boot from %s (bank %d)\n",
	       flashboot?"Flash":"ROM", flashboot);

	lubbock_flash_data[flashboot^1].name = "application-flash";
	lubbock_flash_data[flashboot].name = "boot-rom";
	(void) platform_add_devices(devices, ARRAY_SIZE(devices));

	pxa2xx_set_spi_info(1, &pxa_ssp_master_info);
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));
}

static struct map_desc lubbock_io_desc[] __initdata = {
  	{	/* CPLD */
		.virtual	=  LUBBOCK_FPGA_VIRT,
		.pfn		= __phys_to_pfn(LUBBOCK_FPGA_PHYS),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}
};

static void __init lubbock_map_io(void)
{
	pxa25x_map_io();
	iotable_init(lubbock_io_desc, ARRAY_SIZE(lubbock_io_desc));

	PCFR |= PCFR_OPDE;
}

/*
 * Driver for the 8 discrete LEDs available for general use:
 * Note: bits [15-8] are used to enable/blank the 8 7 segment hex displays
 * so be sure to not monkey with them here.
 */

#if defined(CONFIG_NEW_LEDS) && defined(CONFIG_LEDS_CLASS)
struct lubbock_led {
	struct led_classdev	cdev;
	u8			mask;
};

/*
 * The triggers lines up below will only be used if the
 * LED triggers are compiled in.
 */
static const struct {
	const char *name;
	const char *trigger;
} lubbock_leds[] = {
	{ "lubbock:D28", "default-on", },
	{ "lubbock:D27", "cpu0", },
	{ "lubbock:D26", "heartbeat" },
	{ "lubbock:D25", },
	{ "lubbock:D24", },
	{ "lubbock:D23", },
	{ "lubbock:D22", },
	{ "lubbock:D21", },
};

static void lubbock_led_set(struct led_classdev *cdev,
			      enum led_brightness b)
{
	struct lubbock_led *led = container_of(cdev,
					 struct lubbock_led, cdev);
	u32 reg = LUB_DISC_BLNK_LED;

	if (b != LED_OFF)
		reg |= led->mask;
	else
		reg &= ~led->mask;

	LUB_DISC_BLNK_LED = reg;
}

static enum led_brightness lubbock_led_get(struct led_classdev *cdev)
{
	struct lubbock_led *led = container_of(cdev,
					 struct lubbock_led, cdev);
	u32 reg = LUB_DISC_BLNK_LED;

	return (reg & led->mask) ? LED_FULL : LED_OFF;
}

static int __init lubbock_leds_init(void)
{
	int i;

	if (!machine_is_lubbock())
		return -ENODEV;

	/* All ON */
	LUB_DISC_BLNK_LED |= 0xff;
	for (i = 0; i < ARRAY_SIZE(lubbock_leds); i++) {
		struct lubbock_led *led;

		led = kzalloc(sizeof(*led), GFP_KERNEL);
		if (!led)
			break;

		led->cdev.name = lubbock_leds[i].name;
		led->cdev.brightness_set = lubbock_led_set;
		led->cdev.brightness_get = lubbock_led_get;
		led->cdev.default_trigger = lubbock_leds[i].trigger;
		led->mask = BIT(i);

		if (led_classdev_register(NULL, &led->cdev) < 0) {
			kfree(led);
			break;
		}
	}

	return 0;
}

/*
 * Since we may have triggers on any subsystem, defer registration
 * until after subsystem_init.
 */
fs_initcall(lubbock_leds_init);
#endif

MACHINE_START(LUBBOCK, "Intel DBPXA250 Development Platform (aka Lubbock)")
	/* Maintainer: MontaVista Software Inc. */
	.map_io		= lubbock_map_io,
	.nr_irqs	= LUBBOCK_NR_IRQS,
	.init_irq	= pxa25x_init_irq,
	.handle_irq	= pxa25x_handle_irq,
	.init_time	= pxa_timer_init,
	.init_machine	= lubbock_init,
	.restart	= pxa_restart,
MACHINE_END
