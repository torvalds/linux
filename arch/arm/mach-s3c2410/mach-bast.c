/* linux/arch/arm/mach-s3c2410/mach-bast.c
 *
 * Copyright 2003-2008 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.simtec.co.uk/products/EB2410ITX/
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
#include <linux/syscore_ops.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/dm9000.h>
#include <linux/ata_platform.h>
#include <linux/i2c.h>
#include <linux/io.h>

#include <net/ax88796.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/bast-map.h>
#include <mach/bast-irq.h>
#include <mach/bast-cpld.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

//#include <asm/debug-ll.h>
#include <plat/regs-serial.h>
#include <mach/regs-gpio.h>
#include <mach/regs-mem.h>
#include <mach/regs-lcd.h>

#include <plat/hwmon.h>
#include <plat/nand.h>
#include <plat/iic.h>
#include <mach/fb.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <linux/serial_8250.h>

#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/cpu-freq.h>
#include <plat/gpio-cfg.h>
#include <plat/audio-simtec.h>

#include "usb-simtec.h"
#include "nor-simtec.h"

#define COPYRIGHT ", Copyright 2004-2008 Simtec Electronics"

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

static struct map_desc bast_iodesc[] __initdata = {
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
  /* bast CPLD control registers, and external interrupt controls */
  {
	  .virtual	= (u32)BAST_VA_CTRL1,
	  .pfn		= __phys_to_pfn(BAST_PA_CTRL1),
	  .length	= SZ_1M,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)BAST_VA_CTRL2,
	  .pfn		= __phys_to_pfn(BAST_PA_CTRL2),
	  .length	= SZ_1M,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)BAST_VA_CTRL3,
	  .pfn		= __phys_to_pfn(BAST_PA_CTRL3),
	  .length	= SZ_1M,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)BAST_VA_CTRL4,
	  .pfn		= __phys_to_pfn(BAST_PA_CTRL4),
	  .length	= SZ_1M,
	  .type		= MT_DEVICE,
  },
  /* PC104 IRQ mux */
  {
	  .virtual	= (u32)BAST_VA_PC104_IRQREQ,
	  .pfn		= __phys_to_pfn(BAST_PA_PC104_IRQREQ),
	  .length	= SZ_1M,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)BAST_VA_PC104_IRQRAW,
	  .pfn		= __phys_to_pfn(BAST_PA_PC104_IRQRAW),
	  .length	= SZ_1M,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)BAST_VA_PC104_IRQMASK,
	  .pfn		= __phys_to_pfn(BAST_PA_PC104_IRQMASK),
	  .length	= SZ_1M,
	  .type		= MT_DEVICE,
  },

  /* peripheral space... one for each of fast/slow/byte/16bit */
  /* note, ide is only decoded in word space, even though some registers
   * are only 8bit */

  /* slow, byte */
  { VA_C2(BAST_VA_ISAIO),   PA_CS2(BAST_PA_ISAIO),    SZ_16M, MT_DEVICE },
  { VA_C2(BAST_VA_ISAMEM),  PA_CS2(BAST_PA_ISAMEM),   SZ_16M, MT_DEVICE },
  { VA_C2(BAST_VA_SUPERIO), PA_CS2(BAST_PA_SUPERIO),  SZ_1M,  MT_DEVICE },

  /* slow, word */
  { VA_C3(BAST_VA_ISAIO),   PA_CS3(BAST_PA_ISAIO),    SZ_16M, MT_DEVICE },
  { VA_C3(BAST_VA_ISAMEM),  PA_CS3(BAST_PA_ISAMEM),   SZ_16M, MT_DEVICE },
  { VA_C3(BAST_VA_SUPERIO), PA_CS3(BAST_PA_SUPERIO),  SZ_1M,  MT_DEVICE },

  /* fast, byte */
  { VA_C4(BAST_VA_ISAIO),   PA_CS4(BAST_PA_ISAIO),    SZ_16M, MT_DEVICE },
  { VA_C4(BAST_VA_ISAMEM),  PA_CS4(BAST_PA_ISAMEM),   SZ_16M, MT_DEVICE },
  { VA_C4(BAST_VA_SUPERIO), PA_CS4(BAST_PA_SUPERIO),  SZ_1M,  MT_DEVICE },

  /* fast, word */
  { VA_C5(BAST_VA_ISAIO),   PA_CS5(BAST_PA_ISAIO),    SZ_16M, MT_DEVICE },
  { VA_C5(BAST_VA_ISAMEM),  PA_CS5(BAST_PA_ISAMEM),   SZ_16M, MT_DEVICE },
  { VA_C5(BAST_VA_SUPERIO), PA_CS5(BAST_PA_SUPERIO),  SZ_1M,  MT_DEVICE },
};

#define UCON S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c24xx_uart_clksrc bast_serial_clocks[] = {
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


static struct s3c2410_uartcfg bast_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = bast_serial_clocks,
		.clocks_size = ARRAY_SIZE(bast_serial_clocks),
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = bast_serial_clocks,
		.clocks_size = ARRAY_SIZE(bast_serial_clocks),
	},
	/* port 2 is not actually used */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = bast_serial_clocks,
		.clocks_size = ARRAY_SIZE(bast_serial_clocks),
	}
};

/* NAND Flash on BAST board */

#ifdef CONFIG_PM
static int bast_pm_suspend(void)
{
	/* ensure that an nRESET is not generated on resume. */
	gpio_direction_output(S3C2410_GPA(21), 1);
	return 0;
}

static void bast_pm_resume(void)
{
	s3c_gpio_cfgpin(S3C2410_GPA(21), S3C2410_GPA21_nRSTOUT);
}

#else
#define bast_pm_suspend NULL
#define bast_pm_resume NULL
#endif

static struct syscore_ops bast_pm_syscore_ops = {
	.suspend	= bast_pm_suspend,
	.resume		= bast_pm_resume,
};

static int smartmedia_map[] = { 0 };
static int chip0_map[] = { 1 };
static int chip1_map[] = { 2 };
static int chip2_map[] = { 3 };

static struct mtd_partition __initdata bast_default_nand_part[] = {
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
		.name	= "user",
		.offset	= SZ_4M,
		.size	= MTDPART_SIZ_FULL,
	}
};

/* the bast has 4 selectable slots for nand-flash, the three
 * on-board chip areas, as well as the external SmartMedia
 * slot.
 *
 * Note, there is no current hot-plug support for the SmartMedia
 * socket.
*/

static struct s3c2410_nand_set __initdata bast_nand_sets[] = {
	[0] = {
		.name		= "SmartMedia",
		.nr_chips	= 1,
		.nr_map		= smartmedia_map,
		.options        = NAND_SCAN_SILENT_NODEV,
		.nr_partitions	= ARRAY_SIZE(bast_default_nand_part),
		.partitions	= bast_default_nand_part,
	},
	[1] = {
		.name		= "chip0",
		.nr_chips	= 1,
		.nr_map		= chip0_map,
		.nr_partitions	= ARRAY_SIZE(bast_default_nand_part),
		.partitions	= bast_default_nand_part,
	},
	[2] = {
		.name		= "chip1",
		.nr_chips	= 1,
		.nr_map		= chip1_map,
		.options        = NAND_SCAN_SILENT_NODEV,
		.nr_partitions	= ARRAY_SIZE(bast_default_nand_part),
		.partitions	= bast_default_nand_part,
	},
	[3] = {
		.name		= "chip2",
		.nr_chips	= 1,
		.nr_map		= chip2_map,
		.options        = NAND_SCAN_SILENT_NODEV,
		.nr_partitions	= ARRAY_SIZE(bast_default_nand_part),
		.partitions	= bast_default_nand_part,
	}
};

static void bast_nand_select(struct s3c2410_nand_set *set, int slot)
{
	unsigned int tmp;

	slot = set->nr_map[slot] & 3;

	pr_debug("bast_nand: selecting slot %d (set %p,%p)\n",
		 slot, set, set->nr_map);

	tmp = __raw_readb(BAST_VA_CTRL2);
	tmp &= BAST_CPLD_CTLR2_IDERST;
	tmp |= slot;
	tmp |= BAST_CPLD_CTRL2_WNAND;

	pr_debug("bast_nand: ctrl2 now %02x\n", tmp);

	__raw_writeb(tmp, BAST_VA_CTRL2);
}

static struct s3c2410_platform_nand __initdata bast_nand_info = {
	.tacls		= 30,
	.twrph0		= 60,
	.twrph1		= 60,
	.nr_sets	= ARRAY_SIZE(bast_nand_sets),
	.sets		= bast_nand_sets,
	.select_chip	= bast_nand_select,
};

/* DM9000 */

static struct resource bast_dm9k_resource[] = {
	[0] = {
		.start = S3C2410_CS5 + BAST_PA_DM9000,
		.end   = S3C2410_CS5 + BAST_PA_DM9000 + 3,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = S3C2410_CS5 + BAST_PA_DM9000 + 0x40,
		.end   = S3C2410_CS5 + BAST_PA_DM9000 + 0x40 + 0x3f,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start = IRQ_DM9000,
		.end   = IRQ_DM9000,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	}

};

/* for the moment we limit ourselves to 16bit IO until some
 * better IO routines can be written and tested
*/

static struct dm9000_plat_data bast_dm9k_platdata = {
	.flags		= DM9000_PLATF_16BITONLY,
};

static struct platform_device bast_device_dm9k = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(bast_dm9k_resource),
	.resource	= bast_dm9k_resource,
	.dev		= {
		.platform_data = &bast_dm9k_platdata,
	}
};

/* serial devices */

#define SERIAL_BASE  (S3C2410_CS2 + BAST_PA_SUPERIO)
#define SERIAL_FLAGS (UPF_BOOT_AUTOCONF | UPF_IOREMAP | UPF_SHARE_IRQ)
#define SERIAL_CLK   (1843200)

static struct plat_serial8250_port bast_sio_data[] = {
	[0] = {
		.mapbase	= SERIAL_BASE + 0x2f8,
		.irq		= IRQ_PCSERIAL1,
		.flags		= SERIAL_FLAGS,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= SERIAL_CLK,
	},
	[1] = {
		.mapbase	= SERIAL_BASE + 0x3f8,
		.irq		= IRQ_PCSERIAL2,
		.flags		= SERIAL_FLAGS,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= SERIAL_CLK,
	},
	{ }
};

static struct platform_device bast_sio = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= &bast_sio_data,
	},
};

/* we have devices on the bus which cannot work much over the
 * standard 100KHz i2c bus frequency
*/

static struct s3c2410_platform_i2c __initdata bast_i2c_info = {
	.flags		= 0,
	.slave_addr	= 0x10,
	.frequency	= 100*1000,
};

/* Asix AX88796 10/100 ethernet controller */

static struct ax_plat_data bast_asix_platdata = {
	.flags		= AXFLG_MAC_FROMDEV,
	.wordlength	= 2,
	.dcr_val	= 0x48,
	.rcr_val	= 0x40,
};

static struct resource bast_asix_resource[] = {
	[0] = {
		.start = S3C2410_CS5 + BAST_PA_ASIXNET,
		.end   = S3C2410_CS5 + BAST_PA_ASIXNET + (0x18 * 0x20) - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = S3C2410_CS5 + BAST_PA_ASIXNET + (0x1f * 0x20),
		.end   = S3C2410_CS5 + BAST_PA_ASIXNET + (0x1f * 0x20),
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start = IRQ_ASIX,
		.end   = IRQ_ASIX,
		.flags = IORESOURCE_IRQ
	}
};

static struct platform_device bast_device_asix = {
	.name		= "ax88796",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(bast_asix_resource),
	.resource	= bast_asix_resource,
	.dev		= {
		.platform_data = &bast_asix_platdata
	}
};

/* Asix AX88796 10/100 ethernet controller parallel port */

static struct resource bast_asixpp_resource[] = {
	[0] = {
		.start = S3C2410_CS5 + BAST_PA_ASIXNET + (0x18 * 0x20),
		.end   = S3C2410_CS5 + BAST_PA_ASIXNET + (0x1b * 0x20) - 1,
		.flags = IORESOURCE_MEM,
	}
};

static struct platform_device bast_device_axpp = {
	.name		= "ax88796-pp",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(bast_asixpp_resource),
	.resource	= bast_asixpp_resource,
};

/* LCD/VGA controller */

static struct s3c2410fb_display __initdata bast_lcd_info[] = {
	{
		.type		= S3C2410_LCDCON1_TFT,
		.width		= 640,
		.height		= 480,

		.pixclock	= 33333,
		.xres		= 640,
		.yres		= 480,
		.bpp		= 4,
		.left_margin	= 40,
		.right_margin	= 20,
		.hsync_len	= 88,
		.upper_margin	= 30,
		.lower_margin	= 32,
		.vsync_len	= 3,

		.lcdcon5	= 0x00014b02,
	},
	{
		.type		= S3C2410_LCDCON1_TFT,
		.width		= 640,
		.height		= 480,

		.pixclock	= 33333,
		.xres		= 640,
		.yres		= 480,
		.bpp		= 8,
		.left_margin	= 40,
		.right_margin	= 20,
		.hsync_len	= 88,
		.upper_margin	= 30,
		.lower_margin	= 32,
		.vsync_len	= 3,

		.lcdcon5	= 0x00014b02,
	},
	{
		.type		= S3C2410_LCDCON1_TFT,
		.width		= 640,
		.height		= 480,

		.pixclock	= 33333,
		.xres		= 640,
		.yres		= 480,
		.bpp		= 16,
		.left_margin	= 40,
		.right_margin	= 20,
		.hsync_len	= 88,
		.upper_margin	= 30,
		.lower_margin	= 32,
		.vsync_len	= 3,

		.lcdcon5	= 0x00014b02,
	},
};

/* LCD/VGA controller */

static struct s3c2410fb_mach_info __initdata bast_fb_info = {

	.displays = bast_lcd_info,
	.num_displays = ARRAY_SIZE(bast_lcd_info),
	.default_display = 1,
};

/* I2C devices fitted. */

static struct i2c_board_info bast_i2c_devs[] __initdata = {
	{
		I2C_BOARD_INFO("tlv320aic23", 0x1a),
	}, {
		I2C_BOARD_INFO("simtec-pmu", 0x6b),
	}, {
		I2C_BOARD_INFO("ch7013", 0x75),
	},
};

static struct s3c_hwmon_pdata bast_hwmon_info = {
	/* LCD contrast (0-6.6V) */
	.in[0] = &(struct s3c_hwmon_chcfg) {
		.name		= "lcd-contrast",
		.mult		= 3300,
		.div		= 512,
	},
	/* LED current feedback */
	.in[1] = &(struct s3c_hwmon_chcfg) {
		.name		= "led-feedback",
		.mult		= 3300,
		.div		= 1024,
	},
	/* LCD feedback (0-6.6V) */
	.in[2] = &(struct s3c_hwmon_chcfg) {
		.name		= "lcd-feedback",
		.mult		= 3300,
		.div		= 512,
	},
	/* Vcore (1.8-2.0V), Vref 3.3V  */
	.in[3] = &(struct s3c_hwmon_chcfg) {
		.name		= "vcore",
		.mult		= 3300,
		.div		= 1024,
	},
};

/* Standard BAST devices */
// cat /sys/devices/platform/s3c24xx-adc/s3c-hwmon/in_0

static struct platform_device *bast_devices[] __initdata = {
	&s3c_device_ohci,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c0,
 	&s3c_device_rtc,
	&s3c_device_nand,
	&s3c_device_adc,
	&s3c_device_hwmon,
	&bast_device_dm9k,
	&bast_device_asix,
	&bast_device_axpp,
	&bast_sio,
};

static struct clk *bast_clocks[] __initdata = {
	&s3c24xx_dclk0,
	&s3c24xx_dclk1,
	&s3c24xx_clkout0,
	&s3c24xx_clkout1,
	&s3c24xx_uclk,
};

static struct s3c_cpufreq_board __initdata bast_cpufreq = {
	.refresh	= 7800, /* 7.8usec */
	.auto_io	= 1,
	.need_io	= 1,
};

static struct s3c24xx_audio_simtec_pdata __initdata bast_audio = {
	.have_mic	= 1,
	.have_lout	= 1,
};

static void __init bast_map_io(void)
{
	/* initialise the clocks */

	s3c24xx_dclk0.parent = &clk_upll;
	s3c24xx_dclk0.rate   = 12*1000*1000;

	s3c24xx_dclk1.parent = &clk_upll;
	s3c24xx_dclk1.rate   = 24*1000*1000;

	s3c24xx_clkout0.parent  = &s3c24xx_dclk0;
	s3c24xx_clkout1.parent  = &s3c24xx_dclk1;

	s3c24xx_uclk.parent  = &s3c24xx_clkout1;

	s3c24xx_register_clocks(bast_clocks, ARRAY_SIZE(bast_clocks));

	s3c_hwmon_set_platdata(&bast_hwmon_info);

	s3c24xx_init_io(bast_iodesc, ARRAY_SIZE(bast_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(bast_uartcfgs, ARRAY_SIZE(bast_uartcfgs));
}

static void __init bast_init(void)
{
	register_syscore_ops(&bast_pm_syscore_ops);

	s3c_i2c0_set_platdata(&bast_i2c_info);
	s3c_nand_set_platdata(&bast_nand_info);
	s3c24xx_fb_set_platdata(&bast_fb_info);
	platform_add_devices(bast_devices, ARRAY_SIZE(bast_devices));

	i2c_register_board_info(0, bast_i2c_devs,
				ARRAY_SIZE(bast_i2c_devs));

	usb_simtec_init();
	nor_simtec_init();
	simtec_audio_add(NULL, true, &bast_audio);

	WARN_ON(gpio_request(S3C2410_GPA(21), "bast nreset"));
	
	s3c_cpufreq_setboard(&bast_cpufreq);
}

MACHINE_START(BAST, "Simtec-BAST")
	/* Maintainer: Ben Dooks <ben@simtec.co.uk> */
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.map_io		= bast_map_io,
	.init_irq	= s3c24xx_init_irq,
	.init_machine	= bast_init,
	.timer		= &s3c24xx_timer,
MACHINE_END
