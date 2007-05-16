/* linux/arch/arm/mach-s3c2412/s3c2412.c
 *
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * http://armlinux.simtec.co.uk/.
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
#include <linux/sysdev.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/proc-fns.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <asm/arch/idle.h>

#include <asm/arch/regs-clock.h>
#include <asm/arch/regs-serial.h>
#include <asm/arch/regs-power.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/regs-gpioj.h>
#include <asm/arch/regs-dsc.h>
#include <asm/arch/regs-spi.h>

#include <asm/plat-s3c24xx/s3c2412.h>
#include <asm/plat-s3c24xx/cpu.h>
#include <asm/plat-s3c24xx/devs.h>
#include <asm/plat-s3c24xx/clock.h>
#include <asm/plat-s3c24xx/pm.h>

#ifndef CONFIG_CPU_S3C2412_ONLY
void __iomem *s3c24xx_va_gpio2 = S3C24XX_VA_GPIO;

static inline void s3c2412_init_gpio2(void)
{
	s3c24xx_va_gpio2 = S3C24XX_VA_GPIO + 0x10;
}
#else
#define s3c2412_init_gpio2() do { } while(0)
#endif

/* Initial IO mappings */

static struct map_desc s3c2412_iodesc[] __initdata = {
	IODESC_ENT(CLKPWR),
	IODESC_ENT(LCD),
	IODESC_ENT(TIMER),
	IODESC_ENT(WATCHDOG),
};

/* uart registration process */

void __init s3c2412_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	s3c24xx_init_uartdevs("s3c2412-uart", s3c2410_uart_resources, cfg, no);

	/* rename devices that are s3c2412/s3c2413 specific */
	s3c_device_sdi.name  = "s3c2412-sdi";
	s3c_device_lcd.name  = "s3c2412-lcd";
	s3c_device_nand.name = "s3c2412-nand";

	/* spi channel related changes, s3c2412/13 specific */
	s3c_device_spi0.name = "s3c2412-spi";
	s3c_device_spi0.resource[0].end = S3C24XX_PA_SPI + 0x24;
	s3c_device_spi1.name = "s3c2412-spi";
	s3c_device_spi1.resource[0].start = S3C24XX_PA_SPI + S3C2412_SPI1;
	s3c_device_spi1.resource[0].end = S3C24XX_PA_SPI + S3C2412_SPI1 + 0x24;

}

/* s3c2412_idle
 *
 * use the standard idle call by ensuring the idle mode
 * in power config, then issuing the idle co-processor
 * instruction
*/

static void s3c2412_idle(void)
{
	unsigned long tmp;

	/* ensure our idle mode is to go to idle */

	tmp = __raw_readl(S3C2412_PWRCFG);
	tmp &= ~S3C2412_PWRCFG_STANDBYWFI_MASK;
	tmp |= S3C2412_PWRCFG_STANDBYWFI_IDLE;
	__raw_writel(tmp, S3C2412_PWRCFG);

	cpu_do_idle();
}

/* s3c2412_map_io
 *
 * register the standard cpu IO areas, and any passed in from the
 * machine specific initialisation.
*/

void __init s3c2412_map_io(struct map_desc *mach_desc, int mach_size)
{
	/* move base of IO */

	s3c2412_init_gpio2();

	/* set our idle function */

	s3c24xx_idle = s3c2412_idle;

	/* register our io-tables */

	iotable_init(s3c2412_iodesc, ARRAY_SIZE(s3c2412_iodesc));
	iotable_init(mach_desc, mach_size);
}

void __init s3c2412_init_clocks(int xtal)
{
	unsigned long tmp;
	unsigned long fclk;
	unsigned long hclk;
	unsigned long pclk;

	/* now we've got our machine bits initialised, work out what
	 * clocks we've got */

	fclk = s3c2410_get_pll(__raw_readl(S3C2410_MPLLCON), xtal*2);

	tmp = __raw_readl(S3C2410_CLKDIVN);

	/* work out clock scalings */

	hclk = fclk / ((tmp & S3C2412_CLKDIVN_HDIVN_MASK) + 1);
	hclk /= ((tmp & S3C2421_CLKDIVN_ARMDIVN) ? 2 : 1);
	pclk = hclk / ((tmp & S3C2412_CLKDIVN_PDIVN) ? 2 : 1);

	/* print brieft summary of clocks, etc */

	printk("S3C2412: core %ld.%03ld MHz, memory %ld.%03ld MHz, peripheral %ld.%03ld MHz\n",
	       print_mhz(fclk), print_mhz(hclk), print_mhz(pclk));

	/* initialise the clocks here, to allow other things like the
	 * console to use them
	 */

	s3c24xx_setup_clocks(xtal, fclk, hclk, pclk);
	s3c2412_baseclk_add();
}

/* need to register class before we actually register the device, and
 * we also need to ensure that it has been initialised before any of the
 * drivers even try to use it (even if not on an s3c2412 based system)
 * as a driver which may support both 2410 and 2440 may try and use it.
*/

struct sysdev_class s3c2412_sysclass = {
	set_kset_name("s3c2412-core"),
};

static int __init s3c2412_core_init(void)
{
	return sysdev_class_register(&s3c2412_sysclass);
}

core_initcall(s3c2412_core_init);

static struct sys_device s3c2412_sysdev = {
	.cls		= &s3c2412_sysclass,
};

int __init s3c2412_init(void)
{
	printk("S3C2412: Initialising architecture\n");

	return sysdev_register(&s3c2412_sysdev);
}
