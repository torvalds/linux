/*
 * Copyright (c) 2003-2008 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * Machine support for Thorcom VR1000 board. Designed for Thorcom by
 * Simtec Electronics, http://www.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/dm9000.h>
#include <linux/i2c.h>

#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#include <linux/serial_s3c.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/irq.h>
#include <asm/mach-types.h>

#include <linux/platform_data/leds-s3c24xx.h>
#include <linux/platform_data/i2c-s3c2410.h>
#include <linux/platform_data/asoc-s3c24xx_simtec.h>

#include <mach/hardware.h>
#include <mach/regs-gpio.h>
#include <mach/gpio-samsung.h>

#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/samsung-time.h>

#include "bast.h"
#include "common.h"
#include "simtec.h"
#include "vr1000.h"

/* macros for virtual address mods for the io space entries */
#define VA_C5(item) ((unsigned long)(item) + BAST_VAM_CS5)
#define VA_C4(item) ((unsigned long)(item) + BAST_VAM_CS4)
#define VA_C3(item) ((unsigned long)(item) + BAST_VAM_CS3)
#define VA_C2(item) ((unsigned long)(item) + BAST_VAM_CS2)

/* macros to modify the physical addresses for io space */

#define PA_CS2(item) (__phys_to_pfn((item) + S3C2410_CS2))
#define PA_CS3(item) (__phys_to_pfn((item) + S3C2410_CS3))
#define PA_CS4(item) (__phys_to_pfn((item) + S3C2410_CS4))
#define PA_CS5(item) (__phys_to_pfn((item) + S3C2410_CS5))

static struct map_desc vr1000_iodesc[] __initdata = {
  /* ISA IO areas */
  {
	  .virtual	= (u32)S3C24XX_VA_ISA_BYTE,
	  .pfn		= PA_CS2(BAST_PA_ISAIO),
	  .length	= SZ_16M,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)S3C24XX_VA_ISA_WORD,
	  .pfn		= PA_CS3(BAST_PA_ISAIO),
	  .length	= SZ_16M,
	  .type		= MT_DEVICE,
  },

  /*  CPLD control registers, and external interrupt controls */
  {
	  .virtual	= (u32)VR1000_VA_CTRL1,
	  .pfn		= __phys_to_pfn(VR1000_PA_CTRL1),
	  .length	= SZ_1M,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)VR1000_VA_CTRL2,
	  .pfn		= __phys_to_pfn(VR1000_PA_CTRL2),
	  .length	= SZ_1M,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)VR1000_VA_CTRL3,
	  .pfn		= __phys_to_pfn(VR1000_PA_CTRL3),
	  .length	= SZ_1M,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)VR1000_VA_CTRL4,
	  .pfn		= __phys_to_pfn(VR1000_PA_CTRL4),
	  .length	= SZ_1M,
	  .type		= MT_DEVICE,
  },
};

#define UCON S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg vr1000_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
	/* port 2 is not actually used */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	}
};

/* definitions for the vr1000 extra 16550 serial ports */

#define VR1000_BAUDBASE (3692307)

#define VR1000_SERIAL_MAPBASE(x) (VR1000_PA_SERIAL + 0x80 + ((x) << 5))

static struct plat_serial8250_port serial_platform_data[] = {
	[0] = {
		.mapbase	= VR1000_SERIAL_MAPBASE(0),
		.irq		= VR1000_IRQ_SERIAL + 0,
		.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= VR1000_BAUDBASE,
	},
	[1] = {
		.mapbase	= VR1000_SERIAL_MAPBASE(1),
		.irq		= VR1000_IRQ_SERIAL + 1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= VR1000_BAUDBASE,
	},
	[2] = {
		.mapbase	= VR1000_SERIAL_MAPBASE(2),
		.irq		= VR1000_IRQ_SERIAL + 2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= VR1000_BAUDBASE,
	},
	[3] = {
		.mapbase	= VR1000_SERIAL_MAPBASE(3),
		.irq		= VR1000_IRQ_SERIAL + 3,
		.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= VR1000_BAUDBASE,
	},
	{ },
};

static struct platform_device serial_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= serial_platform_data,
	},
};

/* DM9000 ethernet devices */

static struct resource vr1000_dm9k0_resource[] = {
	[0] = DEFINE_RES_MEM(S3C2410_CS5 + VR1000_PA_DM9000, 4),
	[1] = DEFINE_RES_MEM(S3C2410_CS5 + VR1000_PA_DM9000 + 0x40, 0x40),
	[2] = DEFINE_RES_NAMED(VR1000_IRQ_DM9000A, 1, NULL, IORESOURCE_IRQ \
						| IORESOURCE_IRQ_HIGHLEVEL),
};

static struct resource vr1000_dm9k1_resource[] = {
	[0] = DEFINE_RES_MEM(S3C2410_CS5 + VR1000_PA_DM9000 + 0x80, 4),
	[1] = DEFINE_RES_MEM(S3C2410_CS5 + VR1000_PA_DM9000 + 0xC0, 0x40),
	[2] = DEFINE_RES_NAMED(VR1000_IRQ_DM9000N, 1, NULL, IORESOURCE_IRQ \
						| IORESOURCE_IRQ_HIGHLEVEL),
};

/* for the moment we limit ourselves to 16bit IO until some
 * better IO routines can be written and tested
*/

static struct dm9000_plat_data vr1000_dm9k_platdata = {
	.flags		= DM9000_PLATF_16BITONLY,
};

static struct platform_device vr1000_dm9k0 = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(vr1000_dm9k0_resource),
	.resource	= vr1000_dm9k0_resource,
	.dev		= {
		.platform_data = &vr1000_dm9k_platdata,
	}
};

static struct platform_device vr1000_dm9k1 = {
	.name		= "dm9000",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(vr1000_dm9k1_resource),
	.resource	= vr1000_dm9k1_resource,
	.dev		= {
		.platform_data = &vr1000_dm9k_platdata,
	}
};

/* LEDS */

static struct s3c24xx_led_platdata vr1000_led1_pdata = {
	.name		= "led1",
	.gpio		= S3C2410_GPB(0),
	.def_trigger	= "",
};

static struct s3c24xx_led_platdata vr1000_led2_pdata = {
	.name		= "led2",
	.gpio		= S3C2410_GPB(1),
	.def_trigger	= "",
};

static struct s3c24xx_led_platdata vr1000_led3_pdata = {
	.name		= "led3",
	.gpio		= S3C2410_GPB(2),
	.def_trigger	= "",
};

static struct platform_device vr1000_led1 = {
	.name		= "s3c24xx_led",
	.id		= 1,
	.dev		= {
		.platform_data	= &vr1000_led1_pdata,
	},
};

static struct platform_device vr1000_led2 = {
	.name		= "s3c24xx_led",
	.id		= 2,
	.dev		= {
		.platform_data	= &vr1000_led2_pdata,
	},
};

static struct platform_device vr1000_led3 = {
	.name		= "s3c24xx_led",
	.id		= 3,
	.dev		= {
		.platform_data	= &vr1000_led3_pdata,
	},
};

/* I2C devices. */

static struct i2c_board_info vr1000_i2c_devs[] __initdata = {
	{
		I2C_BOARD_INFO("tlv320aic23", 0x1a),
	}, {
		I2C_BOARD_INFO("tmp101", 0x48),
	}, {
		I2C_BOARD_INFO("m41st87", 0x68),
	},
};

/* devices for this board */

static struct platform_device *vr1000_devices[] __initdata = {
	&s3c_device_ohci,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c0,
	&s3c_device_adc,
	&serial_device,
	&vr1000_dm9k0,
	&vr1000_dm9k1,
	&vr1000_led1,
	&vr1000_led2,
	&vr1000_led3,
};

static struct clk *vr1000_clocks[] __initdata = {
	&s3c24xx_dclk0,
	&s3c24xx_dclk1,
	&s3c24xx_clkout0,
	&s3c24xx_clkout1,
	&s3c24xx_uclk,
};

static void vr1000_power_off(void)
{
	gpio_direction_output(S3C2410_GPB(9), 1);
}

static void __init vr1000_map_io(void)
{
	/* initialise clock sources */

	s3c24xx_dclk0.parent = &clk_upll;
	s3c24xx_dclk0.rate   = 12*1000*1000;

	s3c24xx_dclk1.parent = NULL;
	s3c24xx_dclk1.rate   = 3692307;

	s3c24xx_clkout0.parent  = &s3c24xx_dclk0;
	s3c24xx_clkout1.parent  = &s3c24xx_dclk1;

	s3c24xx_uclk.parent  = &s3c24xx_clkout1;

	s3c24xx_register_clocks(vr1000_clocks, ARRAY_SIZE(vr1000_clocks));

	pm_power_off = vr1000_power_off;

	s3c24xx_init_io(vr1000_iodesc, ARRAY_SIZE(vr1000_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(vr1000_uartcfgs, ARRAY_SIZE(vr1000_uartcfgs));
	samsung_set_timer_source(SAMSUNG_PWM3, SAMSUNG_PWM4);
}

static void __init vr1000_init(void)
{
	s3c_i2c0_set_platdata(NULL);
	platform_add_devices(vr1000_devices, ARRAY_SIZE(vr1000_devices));

	i2c_register_board_info(0, vr1000_i2c_devs,
				ARRAY_SIZE(vr1000_i2c_devs));

	nor_simtec_init();
	simtec_audio_add(NULL, true, NULL);

	WARN_ON(gpio_request(S3C2410_GPB(9), "power off"));
}

MACHINE_START(VR1000, "Thorcom-VR1000")
	/* Maintainer: Ben Dooks <ben@simtec.co.uk> */
	.atag_offset	= 0x100,
	.map_io		= vr1000_map_io,
	.init_machine	= vr1000_init,
	.init_irq	= s3c2410_init_irq,
	.init_time	= samsung_timer_init,
	.restart	= s3c2410_restart,
MACHINE_END
