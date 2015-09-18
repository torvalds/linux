/*
 * S3C2442 Machine Support for Openmoko GTA02 / FreeRunner.
 *
 * Copyright (C) 2006-2009 by Openmoko, Inc.
 * Authors: Harald Welte <laforge@openmoko.org>
 *          Andy Green <andy@openmoko.org>
 *          Werner Almesberger <werner@openmoko.org>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/i2c.h>

#include <linux/mmc/host.h>

#include <linux/mfd/pcf50633/adc.h>
#include <linux/mfd/pcf50633/backlight.h>
#include <linux/mfd/pcf50633/core.h>
#include <linux/mfd/pcf50633/gpio.h>
#include <linux/mfd/pcf50633/mbc.h>
#include <linux/mfd/pcf50633/pmic.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>

#include <linux/regulator/machine.h>

#include <linux/spi/spi.h>
#include <linux/spi/s3c24xx.h>

#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <linux/platform_data/i2c-s3c2410.h>
#include <linux/platform_data/mtd-nand-s3c2410.h>
#include <linux/platform_data/touchscreen-s3c2410.h>
#include <linux/platform_data/usb-ohci-s3c2410.h>
#include <linux/platform_data/usb-s3c2410_udc.h>

#include <mach/fb.h>
#include <mach/hardware.h>
#include <mach/regs-gpio.h>
#include <mach/regs-irq.h>
#include <mach/gpio-samsung.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/gpio-cfg.h>
#include <plat/pm.h>
#include <plat/samsung-time.h>

#include "common.h"
#include "gta02.h"

static struct pcf50633 *gta02_pcf;

/*
 * This gets called frequently when we paniced.
 */

static long gta02_panic_blink(int state)
{
	long delay = 0;
	char led;

	led = (state) ? 1 : 0;
	gpio_direction_output(GTA02_GPIO_AUX_LED, led);

	return delay;
}


static struct map_desc gta02_iodesc[] __initdata = {
	{
		.virtual	= 0xe0000000,
		.pfn		= __phys_to_pfn(S3C2410_CS3 + 0x01000000),
		.length		= SZ_1M,
		.type		= MT_DEVICE
	},
};

#define UCON (S3C2410_UCON_DEFAULT | S3C2443_UCON_RXERR_IRQEN)
#define ULCON (S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB)
#define UFCON (S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE)

static struct s3c2410_uartcfg gta02_uartcfgs[] = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= UCON,
		.ulcon		= ULCON,
		.ufcon		= UFCON,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= UCON,
		.ulcon		= ULCON,
		.ufcon		= UFCON,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= UCON,
		.ulcon		= ULCON,
		.ufcon		= UFCON,
	},
};

#ifdef CONFIG_CHARGER_PCF50633
/*
 * On GTA02 the 1A charger features a 48K resistor to 0V on the ID pin.
 * We use this to recognize that we can pull 1A from the USB socket.
 *
 * These constants are the measured pcf50633 ADC levels with the 1A
 * charger / 48K resistor, and with no pulldown resistor.
 */

#define ADC_NOM_CHG_DETECT_1A 6
#define ADC_NOM_CHG_DETECT_USB 43

static void
gta02_configure_pmu_for_charger(struct pcf50633 *pcf, void *unused, int res)
{
	int  ma;

	/* Interpret charger type */
	if (res < ((ADC_NOM_CHG_DETECT_USB + ADC_NOM_CHG_DETECT_1A) / 2)) {

		/*
		 * Sanity - stop GPO driving out now that we have a 1A charger
		 * GPO controls USB Host power generation on GTA02
		 */
		pcf50633_gpio_set(pcf, PCF50633_GPO, 0);

		ma = 1000;
	} else
		ma = 100;

	pcf50633_mbc_usb_curlim_set(pcf, ma);
}

static struct delayed_work gta02_charger_work;
static int gta02_usb_vbus_draw;

static void gta02_charger_worker(struct work_struct *work)
{
	if (gta02_usb_vbus_draw) {
		pcf50633_mbc_usb_curlim_set(gta02_pcf, gta02_usb_vbus_draw);
		return;
	}

#ifdef CONFIG_PCF50633_ADC
	pcf50633_adc_async_read(gta02_pcf,
				PCF50633_ADCC1_MUX_ADCIN1,
				PCF50633_ADCC1_AVERAGE_16,
				gta02_configure_pmu_for_charger,
				NULL);
#else
	/*
	 * If the PCF50633 ADC is disabled we fallback to a
	 * 100mA limit for safety.
	 */
	pcf50633_mbc_usb_curlim_set(gta02_pcf, 100);
#endif
}

#define GTA02_CHARGER_CONFIGURE_TIMEOUT ((3000 * HZ) / 1000)

static void gta02_pmu_event_callback(struct pcf50633 *pcf, int irq)
{
	if (irq == PCF50633_IRQ_USBINS) {
		schedule_delayed_work(&gta02_charger_work,
				      GTA02_CHARGER_CONFIGURE_TIMEOUT);

		return;
	}

	if (irq == PCF50633_IRQ_USBREM) {
		cancel_delayed_work_sync(&gta02_charger_work);
		gta02_usb_vbus_draw = 0;
	}
}

static void gta02_udc_vbus_draw(unsigned int ma)
{
	if (!gta02_pcf)
		return;

	gta02_usb_vbus_draw = ma;

	schedule_delayed_work(&gta02_charger_work,
			      GTA02_CHARGER_CONFIGURE_TIMEOUT);
}
#else /* !CONFIG_CHARGER_PCF50633 */
#define gta02_pmu_event_callback	NULL
#define gta02_udc_vbus_draw		NULL
#endif

/*
 * This is called when pc50633 is probed, unfortunately quite late in the
 * day since it is an I2C bus device. Here we can belatedly define some
 * platform devices with the advantage that we can mark the pcf50633 as the
 * parent. This makes them get suspended and resumed with their parent
 * the pcf50633 still around.
 */

static void gta02_pmu_attach_child_devices(struct pcf50633 *pcf);


static char *gta02_batteries[] = {
	"battery",
};

static struct pcf50633_bl_platform_data gta02_backlight_data = {
	.default_brightness = 0x3f,
	.default_brightness_limit = 0,
	.ramp_time = 5,
};

static struct pcf50633_platform_data gta02_pcf_pdata = {
	.resumers = {
		[0] =	PCF50633_INT1_USBINS |
			PCF50633_INT1_USBREM |
			PCF50633_INT1_ALARM,
		[1] =	PCF50633_INT2_ONKEYF,
		[2] =	PCF50633_INT3_ONKEY1S,
		[3] =	PCF50633_INT4_LOWSYS |
			PCF50633_INT4_LOWBAT |
			PCF50633_INT4_HIGHTMP,
	},

	.batteries = gta02_batteries,
	.num_batteries = ARRAY_SIZE(gta02_batteries),

	.charger_reference_current_ma = 1000,

	.backlight_data = &gta02_backlight_data,

	.reg_init_data = {
		[PCF50633_REGULATOR_AUTO] = {
			.constraints = {
				.min_uV = 3300000,
				.max_uV = 3300000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.always_on = 1,
				.apply_uV = 1,
			},
		},
		[PCF50633_REGULATOR_DOWN1] = {
			.constraints = {
				.min_uV = 1300000,
				.max_uV = 1600000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.always_on = 1,
				.apply_uV = 1,
			},
		},
		[PCF50633_REGULATOR_DOWN2] = {
			.constraints = {
				.min_uV = 1800000,
				.max_uV = 1800000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.apply_uV = 1,
				.always_on = 1,
			},
		},
		[PCF50633_REGULATOR_HCLDO] = {
			.constraints = {
				.min_uV = 2000000,
				.max_uV = 3300000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
						REGULATOR_CHANGE_STATUS,
			},
		},
		[PCF50633_REGULATOR_LDO1] = {
			.constraints = {
				.min_uV = 3300000,
				.max_uV = 3300000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.valid_ops_mask = REGULATOR_CHANGE_STATUS,
				.apply_uV = 1,
			},
		},
		[PCF50633_REGULATOR_LDO2] = {
			.constraints = {
				.min_uV = 3300000,
				.max_uV = 3300000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.apply_uV = 1,
			},
		},
		[PCF50633_REGULATOR_LDO3] = {
			.constraints = {
				.min_uV = 3000000,
				.max_uV = 3000000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.apply_uV = 1,
			},
		},
		[PCF50633_REGULATOR_LDO4] = {
			.constraints = {
				.min_uV = 3200000,
				.max_uV = 3200000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.valid_ops_mask = REGULATOR_CHANGE_STATUS,
				.apply_uV = 1,
			},
		},
		[PCF50633_REGULATOR_LDO5] = {
			.constraints = {
				.min_uV = 3000000,
				.max_uV = 3000000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
				.valid_ops_mask = REGULATOR_CHANGE_STATUS,
				.apply_uV = 1,
			},
		},
		[PCF50633_REGULATOR_LDO6] = {
			.constraints = {
				.min_uV = 3000000,
				.max_uV = 3000000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
			},
		},
		[PCF50633_REGULATOR_MEMLDO] = {
			.constraints = {
				.min_uV = 1800000,
				.max_uV = 1800000,
				.valid_modes_mask = REGULATOR_MODE_NORMAL,
			},
		},

	},
	.probe_done = gta02_pmu_attach_child_devices,
	.mbc_event_callback = gta02_pmu_event_callback,
};


/* NOR Flash. */

#define GTA02_FLASH_BASE	0x18000000 /* GCS3 */
#define GTA02_FLASH_SIZE	0x200000 /* 2MBytes */

static struct physmap_flash_data gta02_nor_flash_data = {
	.width		= 2,
};

static struct resource gta02_nor_flash_resource =
	DEFINE_RES_MEM(GTA02_FLASH_BASE, GTA02_FLASH_SIZE);

static struct platform_device gta02_nor_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &gta02_nor_flash_data,
	},
	.resource	= &gta02_nor_flash_resource,
	.num_resources	= 1,
};


static struct platform_device s3c24xx_pwm_device = {
	.name		= "s3c24xx_pwm",
	.num_resources	= 0,
};

static struct platform_device gta02_dfbmcs320_device = {
	.name = "dfbmcs320",
};

static struct i2c_board_info gta02_i2c_devs[] __initdata = {
	{
		I2C_BOARD_INFO("pcf50633", 0x73),
		.irq = GTA02_IRQ_PCF50633,
		.platform_data = &gta02_pcf_pdata,
	},
	{
		I2C_BOARD_INFO("wm8753", 0x1a),
	},
};

static struct s3c2410_nand_set __initdata gta02_nand_sets[] = {
	[0] = {
		/*
		 * This name is also hard-coded in the boot loaders, so
		 * changing it would would require all users to upgrade
		 * their boot loaders, some of which are stored in a NOR
		 * that is considered to be immutable.
		 */
		.name		= "neo1973-nand",
		.nr_chips	= 1,
		.flash_bbt	= 1,
	},
};

/*
 * Choose a set of timings derived from S3C@2442B MCP54
 * data sheet (K5D2G13ACM-D075 MCP Memory).
 */

static struct s3c2410_platform_nand __initdata gta02_nand_info = {
	.tacls		= 0,
	.twrph0		= 25,
	.twrph1		= 15,
	.nr_sets	= ARRAY_SIZE(gta02_nand_sets),
	.sets		= gta02_nand_sets,
};


/* Get PMU to set USB current limit accordingly. */
static struct s3c2410_udc_mach_info gta02_udc_cfg __initdata = {
	.vbus_draw	= gta02_udc_vbus_draw,
	.pullup_pin = GTA02_GPIO_USB_PULLUP,
};

/* USB */
static struct s3c2410_hcd_info gta02_usb_info __initdata = {
	.port[0]	= {
		.flags	= S3C_HCDFLG_USED,
	},
	.port[1]	= {
		.flags	= 0,
	},
};

/* Touchscreen */
static struct s3c2410_ts_mach_info gta02_ts_info = {
	.delay			= 10000,
	.presc			= 0xff, /* slow as we can go */
	.oversampling_shift	= 2,
};

/* Buttons */
static struct gpio_keys_button gta02_buttons[] = {
	{
		.gpio = GTA02_GPIO_AUX_KEY,
		.code = KEY_PHONE,
		.desc = "Aux",
		.type = EV_KEY,
		.debounce_interval = 100,
	},
	{
		.gpio = GTA02_GPIO_HOLD_KEY,
		.code = KEY_PAUSE,
		.desc = "Hold",
		.type = EV_KEY,
		.debounce_interval = 100,
	},
};

static struct gpio_keys_platform_data gta02_buttons_pdata = {
	.buttons = gta02_buttons,
	.nbuttons = ARRAY_SIZE(gta02_buttons),
};

static struct platform_device gta02_buttons_device = {
	.name = "gpio-keys",
	.id = -1,
	.dev = {
		.platform_data = &gta02_buttons_pdata,
	},
};

static void __init gta02_map_io(void)
{
	s3c24xx_init_io(gta02_iodesc, ARRAY_SIZE(gta02_iodesc));
	s3c24xx_init_uarts(gta02_uartcfgs, ARRAY_SIZE(gta02_uartcfgs));
	samsung_set_timer_source(SAMSUNG_PWM3, SAMSUNG_PWM4);
}


/* These are the guys that don't need to be children of PMU. */

static struct platform_device *gta02_devices[] __initdata = {
	&s3c_device_ohci,
	&s3c_device_wdt,
	&s3c_device_sdi,
	&s3c_device_usbgadget,
	&s3c_device_nand,
	&gta02_nor_flash,
	&s3c24xx_pwm_device,
	&s3c_device_iis,
	&s3c_device_i2c0,
	&gta02_dfbmcs320_device,
	&gta02_buttons_device,
	&s3c_device_adc,
	&s3c_device_ts,
};

/* These guys DO need to be children of PMU. */

static struct platform_device *gta02_devices_pmu_children[] = {
};


/*
 * This is called when pc50633 is probed, quite late in the day since it is an
 * I2C bus device.  Here we can define platform devices with the advantage that
 * we can mark the pcf50633 as the parent.  This makes them get suspended and
 * resumed with their parent the pcf50633 still around.  All devices whose
 * operation depends on something from pcf50633 must have this relationship
 * made explicit like this, or suspend and resume will become an unreliable
 * hellworld.
 */

static void gta02_pmu_attach_child_devices(struct pcf50633 *pcf)
{
	int n;

	/* Grab a copy of the now probed PMU pointer. */
	gta02_pcf = pcf;

	for (n = 0; n < ARRAY_SIZE(gta02_devices_pmu_children); n++)
		gta02_devices_pmu_children[n]->dev.parent = pcf->dev;

	platform_add_devices(gta02_devices_pmu_children,
			     ARRAY_SIZE(gta02_devices_pmu_children));
}

static void gta02_poweroff(void)
{
	pcf50633_reg_set_bit_mask(gta02_pcf, PCF50633_REG_OOCSHDWN, 1, 1);
}

static void __init gta02_machine_init(void)
{
	/* Set the panic callback to turn AUX LED on or off. */
	panic_blink = gta02_panic_blink;

	s3c_pm_init();

#ifdef CONFIG_CHARGER_PCF50633
	INIT_DELAYED_WORK(&gta02_charger_work, gta02_charger_worker);
#endif

	s3c24xx_udc_set_platdata(&gta02_udc_cfg);
	s3c24xx_ts_set_platdata(&gta02_ts_info);
	s3c_ohci_set_platdata(&gta02_usb_info);
	s3c_nand_set_platdata(&gta02_nand_info);
	s3c_i2c0_set_platdata(NULL);

	i2c_register_board_info(0, gta02_i2c_devs, ARRAY_SIZE(gta02_i2c_devs));

	platform_add_devices(gta02_devices, ARRAY_SIZE(gta02_devices));
	pm_power_off = gta02_poweroff;

	regulator_has_full_constraints();
}

static void __init gta02_init_time(void)
{
	s3c2442_init_clocks(12000000);
	samsung_timer_init();
}

MACHINE_START(NEO1973_GTA02, "GTA02")
	/* Maintainer: Nelson Castillo <arhuaco@freaks-unidos.net> */
	.atag_offset	= 0x100,
	.map_io		= gta02_map_io,
	.init_irq	= s3c2442_init_irq,
	.init_machine	= gta02_machine_init,
	.init_time	= gta02_init_time,
MACHINE_END
