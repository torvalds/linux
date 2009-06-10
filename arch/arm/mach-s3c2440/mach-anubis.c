/* linux/arch/arm/mach-s3c2440/mach-anubis.c
 *
 * Copyright (c) 2003-2005,2008 Simtec Electronics
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
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/sm501.h>
#include <linux/sm501-regs.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/anubis-map.h>
#include <mach/anubis-irq.h>
#include <mach/anubis-cpld.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <plat/regs-serial.h>
#include <mach/regs-gpio.h>
#include <mach/regs-mem.h>
#include <mach/regs-lcd.h>
#include <plat/nand.h>
#include <plat/iic.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <net/ax88796.h>

#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/cpu.h>

#define COPYRIGHT ", (c) 2005 Simtec Electronics"

static struct map_desc anubis_iodesc[] __initdata = {
  /* ISA IO areas */

  {
	.virtual	= (u32)S3C24XX_VA_ISA_BYTE,
	.pfn		= __phys_to_pfn(0x0),
	.length		= SZ_4M,
	.type		= MT_DEVICE,
  }, {
	.virtual	= (u32)S3C24XX_VA_ISA_WORD,
	.pfn		= __phys_to_pfn(0x0),
	.length 	= SZ_4M,
	.type		= MT_DEVICE,
  },

  /* we could possibly compress the next set down into a set of smaller tables
   * pagetables, but that would mean using an L2 section, and it still means
   * we cannot actually feed the same register to an LDR due to 16K spacing
   */

  /* CPLD control registers */

  {
	.virtual	= (u32)ANUBIS_VA_CTRL1,
	.pfn		= __phys_to_pfn(ANUBIS_PA_CTRL1),
	.length		= SZ_4K,
	.type		= MT_DEVICE,
  }, {
	.virtual	= (u32)ANUBIS_VA_IDREG,
	.pfn		= __phys_to_pfn(ANUBIS_PA_IDREG),
	.length		= SZ_4K,
	.type		= MT_DEVICE,
  },
};

#define UCON S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c24xx_uart_clksrc anubis_serial_clocks[] = {
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


static struct s3c2410_uartcfg anubis_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = anubis_serial_clocks,
		.clocks_size = ARRAY_SIZE(anubis_serial_clocks),
	},
	[1] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = anubis_serial_clocks,
		.clocks_size = ARRAY_SIZE(anubis_serial_clocks),
	},
};

/* NAND Flash on Anubis board */

static int external_map[]   = { 2 };
static int chip0_map[]      = { 0 };
static int chip1_map[]      = { 1 };

static struct mtd_partition anubis_default_nand_part[] = {
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

static struct mtd_partition anubis_default_nand_part_large[] = {
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

/* the Anubis has 3 selectable slots for nand-flash, the two
 * on-board chip areas, as well as the external slot.
 *
 * Note, there is no current hot-plug support for the External
 * socket.
*/

static struct s3c2410_nand_set anubis_nand_sets[] = {
	[1] = {
		.name		= "External",
		.nr_chips	= 1,
		.nr_map		= external_map,
		.nr_partitions	= ARRAY_SIZE(anubis_default_nand_part),
		.partitions	= anubis_default_nand_part,
	},
	[0] = {
		.name		= "chip0",
		.nr_chips	= 1,
		.nr_map		= chip0_map,
		.nr_partitions	= ARRAY_SIZE(anubis_default_nand_part),
		.partitions	= anubis_default_nand_part,
	},
	[2] = {
		.name		= "chip1",
		.nr_chips	= 1,
		.nr_map		= chip1_map,
		.nr_partitions	= ARRAY_SIZE(anubis_default_nand_part),
		.partitions	= anubis_default_nand_part,
	},
};

static void anubis_nand_select(struct s3c2410_nand_set *set, int slot)
{
	unsigned int tmp;

	slot = set->nr_map[slot] & 3;

	pr_debug("anubis_nand: selecting slot %d (set %p,%p)\n",
		 slot, set, set->nr_map);

	tmp = __raw_readb(ANUBIS_VA_CTRL1);
	tmp &= ~ANUBIS_CTRL1_NANDSEL;
	tmp |= slot;

	pr_debug("anubis_nand: ctrl1 now %02x\n", tmp);

	__raw_writeb(tmp, ANUBIS_VA_CTRL1);
}

static struct s3c2410_platform_nand anubis_nand_info = {
	.tacls		= 25,
	.twrph0		= 55,
	.twrph1		= 40,
	.nr_sets	= ARRAY_SIZE(anubis_nand_sets),
	.sets		= anubis_nand_sets,
	.select_chip	= anubis_nand_select,
};

/* IDE channels */

static struct pata_platform_info anubis_ide_platdata = {
	.ioport_shift	= 5,
};

static struct resource anubis_ide0_resource[] = {
	{
		.start	= S3C2410_CS3,
		.end	= S3C2410_CS3 + (8*32) - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= S3C2410_CS3 + (1<<26) + (6*32),
		.end	= S3C2410_CS3 + (1<<26) + (7*32) - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_IDE0,
		.end	= IRQ_IDE0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device anubis_device_ide0 = {
	.name		= "pata_platform",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(anubis_ide0_resource),
	.resource	= anubis_ide0_resource,
	.dev	= {
		.platform_data = &anubis_ide_platdata,
		.coherent_dma_mask = ~0,
	},
};

static struct resource anubis_ide1_resource[] = {
	{
		.start	= S3C2410_CS4,
		.end	= S3C2410_CS4 + (8*32) - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= S3C2410_CS4 + (1<<26) + (6*32),
		.end	= S3C2410_CS4 + (1<<26) + (7*32) - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_IDE0,
		.end	= IRQ_IDE0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device anubis_device_ide1 = {
	.name		= "pata_platform",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(anubis_ide1_resource),
	.resource	= anubis_ide1_resource,
	.dev	= {
		.platform_data = &anubis_ide_platdata,
		.coherent_dma_mask = ~0,
	},
};

/* Asix AX88796 10/100 ethernet controller */

static struct ax_plat_data anubis_asix_platdata = {
	.flags		= AXFLG_MAC_FROMDEV,
	.wordlength	= 2,
	.dcr_val	= 0x48,
	.rcr_val	= 0x40,
};

static struct resource anubis_asix_resource[] = {
	[0] = {
		.start = S3C2410_CS5,
		.end   = S3C2410_CS5 + (0x20 * 0x20) -1,
		.flags = IORESOURCE_MEM
	},
	[1] = {
		.start = IRQ_ASIX,
		.end   = IRQ_ASIX,
		.flags = IORESOURCE_IRQ
	}
};

static struct platform_device anubis_device_asix = {
	.name		= "ax88796",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(anubis_asix_resource),
	.resource	= anubis_asix_resource,
	.dev		= {
		.platform_data = &anubis_asix_platdata,
	}
};

/* SM501 */

static struct resource anubis_sm501_resource[] = {
	[0] = {
		.start	= S3C2410_CS2,
		.end	= S3C2410_CS2 + SZ_8M,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= S3C2410_CS2 + SZ_64M - SZ_2M,
		.end	= S3C2410_CS2 + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= IRQ_EINT0,
		.end	= IRQ_EINT0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct sm501_initdata anubis_sm501_initdata = {
	.gpio_high	= {
		.set	= 0x3F000000,		/* 24bit panel */
		.mask	= 0x0,
	},
	.misc_timing	= {
		.set	= 0x010100,		/* SDRAM timing */
		.mask	= 0x1F1F00,
	},
	.misc_control	= {
		.set	= SM501_MISC_PNL_24BIT,
		.mask	= 0,
	},

	.devices	= SM501_USE_GPIO,

	/* set the SDRAM and bus clocks */
	.mclk		= 72 * MHZ,
	.m1xclk		= 144 * MHZ,
};

static struct sm501_platdata_gpio_i2c anubis_sm501_gpio_i2c[] = {
	[0] = {
		.bus_num	= 1,
		.pin_scl	= 44,
		.pin_sda	= 45,
	},
	[1] = {
		.bus_num	= 2,
		.pin_scl	= 40,
		.pin_sda	= 41,
	},
};

static struct sm501_platdata anubis_sm501_platdata = {
	.init		= &anubis_sm501_initdata,
	.gpio_base	= -1,
	.gpio_i2c	= anubis_sm501_gpio_i2c,
	.gpio_i2c_nr	= ARRAY_SIZE(anubis_sm501_gpio_i2c),
};

static struct platform_device anubis_device_sm501 = {
	.name		= "sm501",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(anubis_sm501_resource),
	.resource	= anubis_sm501_resource,
	.dev		= {
		.platform_data = &anubis_sm501_platdata,
	},
};

/* Standard Anubis devices */

static struct platform_device *anubis_devices[] __initdata = {
	&s3c_device_usb,
	&s3c_device_wdt,
	&s3c_device_adc,
	&s3c_device_i2c0,
 	&s3c_device_rtc,
	&s3c_device_nand,
	&anubis_device_ide0,
	&anubis_device_ide1,
	&anubis_device_asix,
	&anubis_device_sm501,
};

static struct clk *anubis_clocks[] __initdata = {
	&s3c24xx_dclk0,
	&s3c24xx_dclk1,
	&s3c24xx_clkout0,
	&s3c24xx_clkout1,
	&s3c24xx_uclk,
};

/* I2C devices. */

static struct i2c_board_info anubis_i2c_devs[] __initdata = {
	{
		I2C_BOARD_INFO("tps65011", 0x48),
		.irq	= IRQ_EINT20,
	}
};

static void __init anubis_map_io(void)
{
	/* initialise the clocks */

	s3c24xx_dclk0.parent = &clk_upll;
	s3c24xx_dclk0.rate   = 12*1000*1000;

	s3c24xx_dclk1.parent = &clk_upll;
	s3c24xx_dclk1.rate   = 24*1000*1000;

	s3c24xx_clkout0.parent  = &s3c24xx_dclk0;
	s3c24xx_clkout1.parent  = &s3c24xx_dclk1;

	s3c24xx_uclk.parent  = &s3c24xx_clkout1;

	s3c24xx_register_clocks(anubis_clocks, ARRAY_SIZE(anubis_clocks));

	s3c_device_nand.dev.platform_data = &anubis_nand_info;

	s3c24xx_init_io(anubis_iodesc, ARRAY_SIZE(anubis_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(anubis_uartcfgs, ARRAY_SIZE(anubis_uartcfgs));

	/* check for the newer revision boards with large page nand */

	if ((__raw_readb(ANUBIS_VA_IDREG) & ANUBIS_IDREG_REVMASK) >= 4) {
		printk(KERN_INFO "ANUBIS-B detected (revision %d)\n",
		       __raw_readb(ANUBIS_VA_IDREG) & ANUBIS_IDREG_REVMASK);
		anubis_nand_sets[0].partitions = anubis_default_nand_part_large;
		anubis_nand_sets[0].nr_partitions = ARRAY_SIZE(anubis_default_nand_part_large);
	} else {
		/* ensure that the GPIO is setup */
		s3c2410_gpio_setpin(S3C2410_GPA0, 1);
	}
}

static void __init anubis_init(void)
{
	s3c_i2c0_set_platdata(NULL);
	platform_add_devices(anubis_devices, ARRAY_SIZE(anubis_devices));

	i2c_register_board_info(0, anubis_i2c_devs,
				ARRAY_SIZE(anubis_i2c_devs));
}


MACHINE_START(ANUBIS, "Simtec-Anubis")
	/* Maintainer: Ben Dooks <ben@simtec.co.uk> */
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.map_io		= anubis_map_io,
	.init_machine	= anubis_init,
	.init_irq	= s3c24xx_init_irq,
	.timer		= &s3c24xx_timer,
MACHINE_END
