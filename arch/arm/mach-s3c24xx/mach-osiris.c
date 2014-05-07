/*
 * Copyright (c) 2005-2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/syscore_ops.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#include <linux/i2c/tps65010.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/irq.h>

#include <linux/platform_data/mtd-nand-s3c2410.h>
#include <linux/platform_data/i2c-s3c2410.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/cpu-freq.h>
#include <plat/devs.h>
#include <plat/gpio-cfg.h>
#include <plat/samsung-time.h>

#include <mach/hardware.h>
#include <mach/regs-gpio.h>
#include <mach/regs-lcd.h>
#include <mach/gpio-samsung.h>

#include "common.h"
#include "osiris.h"
#include "regs-mem.h"

/* onboard perihperal map */

static struct map_desc osiris_iodesc[] __initdata = {
  /* ISA IO areas (may be over-written later) */

  {
	  .virtual	= (u32)S3C24XX_VA_ISA_BYTE,
	  .pfn		= __phys_to_pfn(S3C2410_CS5),
	  .length	= SZ_16M,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)S3C24XX_VA_ISA_WORD,
	  .pfn		= __phys_to_pfn(S3C2410_CS5),
	  .length	= SZ_16M,
	  .type		= MT_DEVICE,
  },

  /* CPLD control registers */

  {
	  .virtual	= (u32)OSIRIS_VA_CTRL0,
	  .pfn		= __phys_to_pfn(OSIRIS_PA_CTRL0),
	  .length	= SZ_16K,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)OSIRIS_VA_CTRL1,
	  .pfn		= __phys_to_pfn(OSIRIS_PA_CTRL1),
	  .length	= SZ_16K,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)OSIRIS_VA_CTRL2,
	  .pfn		= __phys_to_pfn(OSIRIS_PA_CTRL2),
	  .length	= SZ_16K,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)OSIRIS_VA_IDREG,
	  .pfn		= __phys_to_pfn(OSIRIS_PA_IDREG),
	  .length	= SZ_16K,
	  .type		= MT_DEVICE,
  },
};

#define UCON S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg osiris_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clk_sel	= S3C2410_UCON_CLKSEL1 | S3C2410_UCON_CLKSEL2,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clk_sel	= S3C2410_UCON_CLKSEL1 | S3C2410_UCON_CLKSEL2,
	},
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clk_sel	= S3C2410_UCON_CLKSEL1 | S3C2410_UCON_CLKSEL2,
	}
};

/* NAND Flash on Osiris board */

static int external_map[]   = { 2 };
static int chip0_map[]      = { 0 };
static int chip1_map[]      = { 1 };

static struct mtd_partition __initdata osiris_default_nand_part[] = {
	[0] = {
		.name	= "Boot Agent",
		.size	= SZ_16K,
		.offset	= 0,
	},
	[1] = {
		.name	= "/boot",
		.size	= SZ_4M - SZ_16K,
		.offset	= SZ_16K,
	},
	[2] = {
		.name	= "user1",
		.offset	= SZ_4M,
		.size	= SZ_32M - SZ_4M,
	},
	[3] = {
		.name	= "user2",
		.offset	= SZ_32M,
		.size	= MTDPART_SIZ_FULL,
	}
};

static struct mtd_partition __initdata osiris_default_nand_part_large[] = {
	[0] = {
		.name	= "Boot Agent",
		.size	= SZ_128K,
		.offset	= 0,
	},
	[1] = {
		.name	= "/boot",
		.size	= SZ_4M - SZ_128K,
		.offset	= SZ_128K,
	},
	[2] = {
		.name	= "user1",
		.offset	= SZ_4M,
		.size	= SZ_32M - SZ_4M,
	},
	[3] = {
		.name	= "user2",
		.offset	= SZ_32M,
		.size	= MTDPART_SIZ_FULL,
	}
};

/* the Osiris has 3 selectable slots for nand-flash, the two
 * on-board chip areas, as well as the external slot.
 *
 * Note, there is no current hot-plug support for the External
 * socket.
*/

static struct s3c2410_nand_set __initdata osiris_nand_sets[] = {
	[1] = {
		.name		= "External",
		.nr_chips	= 1,
		.nr_map		= external_map,
		.options	= NAND_SCAN_SILENT_NODEV,
		.nr_partitions	= ARRAY_SIZE(osiris_default_nand_part),
		.partitions	= osiris_default_nand_part,
	},
	[0] = {
		.name		= "chip0",
		.nr_chips	= 1,
		.nr_map		= chip0_map,
		.nr_partitions	= ARRAY_SIZE(osiris_default_nand_part),
		.partitions	= osiris_default_nand_part,
	},
	[2] = {
		.name		= "chip1",
		.nr_chips	= 1,
		.nr_map		= chip1_map,
		.options	= NAND_SCAN_SILENT_NODEV,
		.nr_partitions	= ARRAY_SIZE(osiris_default_nand_part),
		.partitions	= osiris_default_nand_part,
	},
};

static void osiris_nand_select(struct s3c2410_nand_set *set, int slot)
{
	unsigned int tmp;

	slot = set->nr_map[slot] & 3;

	pr_debug("osiris_nand: selecting slot %d (set %p,%p)\n",
		 slot, set, set->nr_map);

	tmp = __raw_readb(OSIRIS_VA_CTRL0);
	tmp &= ~OSIRIS_CTRL0_NANDSEL;
	tmp |= slot;

	pr_debug("osiris_nand: ctrl0 now %02x\n", tmp);

	__raw_writeb(tmp, OSIRIS_VA_CTRL0);
}

static struct s3c2410_platform_nand __initdata osiris_nand_info = {
	.tacls		= 25,
	.twrph0		= 60,
	.twrph1		= 60,
	.nr_sets	= ARRAY_SIZE(osiris_nand_sets),
	.sets		= osiris_nand_sets,
	.select_chip	= osiris_nand_select,
};

/* PCMCIA control and configuration */

static struct resource osiris_pcmcia_resource[] = {
	[0] = DEFINE_RES_MEM(0x0f000000, SZ_1M),
	[1] = DEFINE_RES_MEM(0x0c000000, SZ_1M),
};

static struct platform_device osiris_pcmcia = {
	.name		= "osiris-pcmcia",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(osiris_pcmcia_resource),
	.resource	= osiris_pcmcia_resource,
};

/* Osiris power management device */

#ifdef CONFIG_PM
static unsigned char pm_osiris_ctrl0;

static int osiris_pm_suspend(void)
{
	unsigned int tmp;

	pm_osiris_ctrl0 = __raw_readb(OSIRIS_VA_CTRL0);
	tmp = pm_osiris_ctrl0 & ~OSIRIS_CTRL0_NANDSEL;

	/* ensure correct NAND slot is selected on resume */
	if ((pm_osiris_ctrl0 & OSIRIS_CTRL0_BOOT_INT) == 0)
	        tmp |= 2;

	__raw_writeb(tmp, OSIRIS_VA_CTRL0);

	/* ensure that an nRESET is not generated on resume. */
	gpio_request_one(S3C2410_GPA(21), GPIOF_OUT_INIT_HIGH, NULL);
	gpio_free(S3C2410_GPA(21));

	return 0;
}

static void osiris_pm_resume(void)
{
	if (pm_osiris_ctrl0 & OSIRIS_CTRL0_FIX8)
		__raw_writeb(OSIRIS_CTRL1_FIX8, OSIRIS_VA_CTRL1);

	__raw_writeb(pm_osiris_ctrl0, OSIRIS_VA_CTRL0);

	s3c_gpio_cfgpin(S3C2410_GPA(21), S3C2410_GPA21_nRSTOUT);
}

#else
#define osiris_pm_suspend NULL
#define osiris_pm_resume NULL
#endif

static struct syscore_ops osiris_pm_syscore_ops = {
	.suspend	= osiris_pm_suspend,
	.resume		= osiris_pm_resume,
};

/* Link for DVS driver to TPS65011 */

static void osiris_tps_release(struct device *dev)
{
	/* static device, do not need to release anything */
}

static struct platform_device osiris_tps_device = {
	.name	= "osiris-dvs",
	.id	= -1,
	.dev.release = osiris_tps_release,
};

static int osiris_tps_setup(struct i2c_client *client, void *context)
{
	osiris_tps_device.dev.parent = &client->dev;
	return platform_device_register(&osiris_tps_device);
}

static int osiris_tps_remove(struct i2c_client *client, void *context)
{
	platform_device_unregister(&osiris_tps_device);
	return 0;
}

static struct tps65010_board osiris_tps_board = {
	.base		= -1,	/* GPIO can go anywhere at the moment */
	.setup		= osiris_tps_setup,
	.teardown	= osiris_tps_remove,
};

/* I2C devices fitted. */

static struct i2c_board_info osiris_i2c_devs[] __initdata = {
	{
		I2C_BOARD_INFO("tps65011", 0x48),
		.irq	= IRQ_EINT20,
		.platform_data = &osiris_tps_board,
	},
};

/* Standard Osiris devices */

static struct platform_device *osiris_devices[] __initdata = {
	&s3c_device_i2c0,
	&s3c_device_wdt,
	&s3c_device_nand,
	&osiris_pcmcia,
};

static struct clk *osiris_clocks[] __initdata = {
	&s3c24xx_dclk0,
	&s3c24xx_dclk1,
	&s3c24xx_clkout0,
	&s3c24xx_clkout1,
	&s3c24xx_uclk,
};

static struct s3c_cpufreq_board __initdata osiris_cpufreq = {
	.refresh	= 7800, /* refresh period is 7.8usec */
	.auto_io	= 1,
	.need_io	= 1,
};

static void __init osiris_map_io(void)
{
	unsigned long flags;

	/* initialise the clocks */

	s3c24xx_dclk0.parent = &clk_upll;
	s3c24xx_dclk0.rate   = 12*1000*1000;

	s3c24xx_dclk1.parent = &clk_upll;
	s3c24xx_dclk1.rate   = 24*1000*1000;

	s3c24xx_clkout0.parent  = &s3c24xx_dclk0;
	s3c24xx_clkout1.parent  = &s3c24xx_dclk1;

	s3c24xx_uclk.parent  = &s3c24xx_clkout1;

	s3c24xx_register_clocks(osiris_clocks, ARRAY_SIZE(osiris_clocks));

	s3c24xx_init_io(osiris_iodesc, ARRAY_SIZE(osiris_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(osiris_uartcfgs, ARRAY_SIZE(osiris_uartcfgs));
	samsung_set_timer_source(SAMSUNG_PWM3, SAMSUNG_PWM4);

	/* check for the newer revision boards with large page nand */

	if ((__raw_readb(OSIRIS_VA_IDREG) & OSIRIS_ID_REVMASK) >= 4) {
		printk(KERN_INFO "OSIRIS-B detected (revision %d)\n",
		       __raw_readb(OSIRIS_VA_IDREG) & OSIRIS_ID_REVMASK);
		osiris_nand_sets[0].partitions = osiris_default_nand_part_large;
		osiris_nand_sets[0].nr_partitions = ARRAY_SIZE(osiris_default_nand_part_large);
	} else {
		/* write-protect line to the NAND */
		gpio_request_one(S3C2410_GPA(0), GPIOF_OUT_INIT_HIGH, NULL);
		gpio_free(S3C2410_GPA(0));
	}

	/* fix bus configuration (nBE settings wrong on ABLE pre v2.20) */

	local_irq_save(flags);
	__raw_writel(__raw_readl(S3C2410_BWSCON) | S3C2410_BWSCON_ST1 | S3C2410_BWSCON_ST2 | S3C2410_BWSCON_ST3 | S3C2410_BWSCON_ST4 | S3C2410_BWSCON_ST5, S3C2410_BWSCON);
	local_irq_restore(flags);
}

static void __init osiris_init(void)
{
	register_syscore_ops(&osiris_pm_syscore_ops);

	s3c_i2c0_set_platdata(NULL);
	s3c_nand_set_platdata(&osiris_nand_info);

	s3c_cpufreq_setboard(&osiris_cpufreq);

	i2c_register_board_info(0, osiris_i2c_devs,
				ARRAY_SIZE(osiris_i2c_devs));

	platform_add_devices(osiris_devices, ARRAY_SIZE(osiris_devices));
};

MACHINE_START(OSIRIS, "Simtec-OSIRIS")
	/* Maintainer: Ben Dooks <ben@simtec.co.uk> */
	.atag_offset	= 0x100,
	.map_io		= osiris_map_io,
	.init_irq	= s3c2440_init_irq,
	.init_machine	= osiris_init,
	.init_time	= samsung_timer_init,
	.restart	= s3c244x_restart,
MACHINE_END
