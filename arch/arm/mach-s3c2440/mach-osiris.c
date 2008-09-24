/* linux/arch/arm/mach-s3c2440/mach-osiris.c
 *
 * Copyright (c) 2005,2008 Simtec Electronics
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
#include <linux/device.h>
#include <linux/sysdev.h>
#include <linux/serial_core.h>
#include <linux/clk.h>
#include <linux/i2c.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/osiris-map.h>
#include <mach/osiris-cpld.h>

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <asm/plat-s3c/regs-serial.h>
#include <mach/regs-gpio.h>
#include <mach/regs-mem.h>
#include <mach/regs-lcd.h>
#include <asm/plat-s3c/nand.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/plat-s3c24xx/clock.h>
#include <asm/plat-s3c24xx/devs.h>
#include <asm/plat-s3c24xx/cpu.h>

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

static struct s3c24xx_uart_clksrc osiris_serial_clocks[] = {
	[0] = {
		.name		= "uclk",
		.divisor	= 1,
		.min_baud	= 0,
		.max_baud	= 0,
	},
	[1] = {
		.name		= "pclk",
		.divisor	= 1,
		.min_baud	= 0,
		.max_baud	= 0,
	}
};

static struct s3c2410_uartcfg osiris_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = osiris_serial_clocks,
		.clocks_size = ARRAY_SIZE(osiris_serial_clocks),
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = osiris_serial_clocks,
		.clocks_size = ARRAY_SIZE(osiris_serial_clocks),
	},
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = osiris_serial_clocks,
		.clocks_size = ARRAY_SIZE(osiris_serial_clocks),
	}
};

/* NAND Flash on Osiris board */

static int external_map[]   = { 2 };
static int chip0_map[]      = { 0 };
static int chip1_map[]      = { 1 };

static struct mtd_partition osiris_default_nand_part[] = {
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

static struct mtd_partition osiris_default_nand_part_large[] = {
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

static struct s3c2410_nand_set osiris_nand_sets[] = {
	[1] = {
		.name		= "External",
		.nr_chips	= 1,
		.nr_map		= external_map,
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

static struct s3c2410_platform_nand osiris_nand_info = {
	.tacls		= 25,
	.twrph0		= 60,
	.twrph1		= 60,
	.nr_sets	= ARRAY_SIZE(osiris_nand_sets),
	.sets		= osiris_nand_sets,
	.select_chip	= osiris_nand_select,
};

/* PCMCIA control and configuration */

static struct resource osiris_pcmcia_resource[] = {
	[0] = {
		.start	= 0x0f000000,
		.end	= 0x0f100000,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0x0c000000,
		.end	= 0x0c100000,
		.flags	= IORESOURCE_MEM,
	}
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

static int osiris_pm_suspend(struct sys_device *sd, pm_message_t state)
{
	unsigned int tmp;

	pm_osiris_ctrl0 = __raw_readb(OSIRIS_VA_CTRL0);
	tmp = pm_osiris_ctrl0 & ~OSIRIS_CTRL0_NANDSEL;

	/* ensure correct NAND slot is selected on resume */
	if ((pm_osiris_ctrl0 & OSIRIS_CTRL0_BOOT_INT) == 0)
	        tmp |= 2;

	__raw_writeb(tmp, OSIRIS_VA_CTRL0);

	/* ensure that an nRESET is not generated on resume. */
	s3c2410_gpio_setpin(S3C2410_GPA21, 1);
	s3c2410_gpio_cfgpin(S3C2410_GPA21, S3C2410_GPA21_OUT);

	return 0;
}

static int osiris_pm_resume(struct sys_device *sd)
{
	if (pm_osiris_ctrl0 & OSIRIS_CTRL0_FIX8)
		__raw_writeb(OSIRIS_CTRL1_FIX8, OSIRIS_VA_CTRL1);

	__raw_writeb(pm_osiris_ctrl0, OSIRIS_VA_CTRL0);

	s3c2410_gpio_cfgpin(S3C2410_GPA21, S3C2410_GPA21_nRSTOUT);

	return 0;
}

#else
#define osiris_pm_suspend NULL
#define osiris_pm_resume NULL
#endif

static struct sysdev_class osiris_pm_sysclass = {
	.name		= "mach-osiris",
	.suspend	= osiris_pm_suspend,
	.resume		= osiris_pm_resume,
};

static struct sys_device osiris_pm_sysdev = {
	.cls		= &osiris_pm_sysclass,
};

/* I2C devices fitted. */

static struct i2c_board_info osiris_i2c_devs[] __initdata = {
	{
		I2C_BOARD_INFO("tps65011", 0x48),
		.irq	= IRQ_EINT20,
	},
};

/* Standard Osiris devices */

static struct platform_device *osiris_devices[] __initdata = {
	&s3c_device_i2c,
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

	s3c_device_nand.dev.platform_data = &osiris_nand_info;

	s3c24xx_init_io(osiris_iodesc, ARRAY_SIZE(osiris_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(osiris_uartcfgs, ARRAY_SIZE(osiris_uartcfgs));

	/* check for the newer revision boards with large page nand */

	if ((__raw_readb(OSIRIS_VA_IDREG) & OSIRIS_ID_REVMASK) >= 4) {
		printk(KERN_INFO "OSIRIS-B detected (revision %d)\n",
		       __raw_readb(OSIRIS_VA_IDREG) & OSIRIS_ID_REVMASK);
		osiris_nand_sets[0].partitions = osiris_default_nand_part_large;
		osiris_nand_sets[0].nr_partitions = ARRAY_SIZE(osiris_default_nand_part_large);
	} else {
		/* write-protect line to the NAND */
		s3c2410_gpio_setpin(S3C2410_GPA0, 1);
	}

	/* fix bus configuration (nBE settings wrong on ABLE pre v2.20) */

	local_irq_save(flags);
	__raw_writel(__raw_readl(S3C2410_BWSCON) | S3C2410_BWSCON_ST1 | S3C2410_BWSCON_ST2 | S3C2410_BWSCON_ST3 | S3C2410_BWSCON_ST4 | S3C2410_BWSCON_ST5, S3C2410_BWSCON);
	local_irq_restore(flags);
}

static void __init osiris_init(void)
{
	sysdev_class_register(&osiris_pm_sysclass);
	sysdev_register(&osiris_pm_sysdev);

	i2c_register_board_info(0, osiris_i2c_devs,
				ARRAY_SIZE(osiris_i2c_devs));

	platform_add_devices(osiris_devices, ARRAY_SIZE(osiris_devices));
};

MACHINE_START(OSIRIS, "Simtec-OSIRIS")
	/* Maintainer: Ben Dooks <ben@simtec.co.uk> */
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.map_io		= osiris_map_io,
	.init_machine	= osiris_init,
	.init_irq	= s3c24xx_init_irq,
	.init_machine	= osiris_init,
	.timer		= &s3c24xx_timer,
MACHINE_END
