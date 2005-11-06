/* linux/arch/arm/mach-s3c2410/s3c2440.c
 *
 * Copyright (c) 2004-2005 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * Samsung S3C2440 Mobile CPU support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *	24-Aug-2004 BJD  Start of s3c2440 support
 *	12-Oct-2004 BJD	 Moved clock info out to clock.c
 *	01-Nov-2004 BJD  Fixed clock build code
 *	09-Nov-2004 BJD  Added sysdev for power management
 *	04-Nov-2004 BJD  New serial registration
 *	15-Nov-2004 BJD  Rename the i2c device for the s3c2440
 *	14-Jan-2005 BJD  Moved clock init code into seperate function
 *	14-Jan-2005 BJD  Removed un-used clock bits
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/sysdev.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/hardware/clock.h>

#include <asm/arch/regs-clock.h>
#include <asm/arch/regs-serial.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/regs-gpioj.h>
#include <asm/arch/regs-dsc.h>

#include "s3c2440.h"
#include "clock.h"
#include "devs.h"
#include "cpu.h"
#include "pm.h"


static struct map_desc s3c2440_iodesc[] __initdata = {
	IODESC_ENT(USBHOST),
	IODESC_ENT(CLKPWR),
	IODESC_ENT(LCD),
	IODESC_ENT(TIMER),
	IODESC_ENT(ADC),
	IODESC_ENT(WATCHDOG),
};

static struct resource s3c_uart0_resource[] = {
	[0] = {
		.start = S3C2410_PA_UART0,
		.end   = S3C2410_PA_UART0 + 0x3fff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_S3CUART_RX0,
		.end   = IRQ_S3CUART_ERR0,
		.flags = IORESOURCE_IRQ,
	}

};

static struct resource s3c_uart1_resource[] = {
	[0] = {
		.start = S3C2410_PA_UART1,
		.end   = S3C2410_PA_UART1 + 0x3fff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_S3CUART_RX1,
		.end   = IRQ_S3CUART_ERR1,
		.flags = IORESOURCE_IRQ,
	}
};

static struct resource s3c_uart2_resource[] = {
	[0] = {
		.start = S3C2410_PA_UART2,
		.end   = S3C2410_PA_UART2 + 0x3fff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_S3CUART_RX2,
		.end   = IRQ_S3CUART_ERR2,
		.flags = IORESOURCE_IRQ,
	}
};

/* our uart devices */

static struct platform_device s3c_uart0 = {
	.name		  = "s3c2440-uart",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(s3c_uart0_resource),
	.resource	  = s3c_uart0_resource,
};

static struct platform_device s3c_uart1 = {
	.name		  = "s3c2440-uart",
	.id		  = 1,
	.num_resources	  = ARRAY_SIZE(s3c_uart1_resource),
	.resource	  = s3c_uart1_resource,
};

static struct platform_device s3c_uart2 = {
	.name		  = "s3c2440-uart",
	.id		  = 2,
	.num_resources	  = ARRAY_SIZE(s3c_uart2_resource),
	.resource	  = s3c_uart2_resource,
};

static struct platform_device *uart_devices[] __initdata = {
	&s3c_uart0,
	&s3c_uart1,
	&s3c_uart2
};

/* uart initialisation */

static int __initdata s3c2440_uart_count;

void __init s3c2440_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	struct platform_device *platdev;
	int uart;

	for (uart = 0; uart < no; uart++, cfg++) {
		platdev = uart_devices[cfg->hwport];

		s3c24xx_uart_devs[uart] = platdev;
		platdev->dev.platform_data = cfg;
	}

	s3c2440_uart_count = uart;
}


#ifdef CONFIG_PM

static struct sleep_save s3c2440_sleep[] = {
	SAVE_ITEM(S3C2440_DSC0),
	SAVE_ITEM(S3C2440_DSC1),
	SAVE_ITEM(S3C2440_GPJDAT),
	SAVE_ITEM(S3C2440_GPJCON),
	SAVE_ITEM(S3C2440_GPJUP)
};

static int s3c2440_suspend(struct sys_device *dev, pm_message_t state)
{
	s3c2410_pm_do_save(s3c2440_sleep, ARRAY_SIZE(s3c2440_sleep));
	return 0;
}

static int s3c2440_resume(struct sys_device *dev)
{
	s3c2410_pm_do_restore(s3c2440_sleep, ARRAY_SIZE(s3c2440_sleep));
	return 0;
}

#else
#define s3c2440_suspend NULL
#define s3c2440_resume  NULL
#endif

struct sysdev_class s3c2440_sysclass = {
	set_kset_name("s3c2440-core"),
	.suspend	= s3c2440_suspend,
	.resume		= s3c2440_resume
};

static struct sys_device s3c2440_sysdev = {
	.cls		= &s3c2440_sysclass,
};

void __init s3c2440_map_io(struct map_desc *mach_desc, int size)
{
	/* register our io-tables */

	iotable_init(s3c2440_iodesc, ARRAY_SIZE(s3c2440_iodesc));
	iotable_init(mach_desc, size);

	/* rename any peripherals used differing from the s3c2410 */

	s3c_device_i2c.name  = "s3c2440-i2c";
	s3c_device_nand.name = "s3c2440-nand";

	/* change irq for watchdog */

	s3c_device_wdt.resource[1].start = IRQ_S3C2440_WDT;
	s3c_device_wdt.resource[1].end   = IRQ_S3C2440_WDT;
}

void __init s3c2440_init_clocks(int xtal)
{
	unsigned long clkdiv;
	unsigned long camdiv;
	unsigned long hclk, fclk, pclk;
	int hdiv = 1;

	/* now we've got our machine bits initialised, work out what
	 * clocks we've got */

	fclk = s3c2410_get_pll(__raw_readl(S3C2410_MPLLCON), xtal) * 2;

	clkdiv = __raw_readl(S3C2410_CLKDIVN);
	camdiv = __raw_readl(S3C2440_CAMDIVN);

	/* work out clock scalings */

	switch (clkdiv & S3C2440_CLKDIVN_HDIVN_MASK) {
	case S3C2440_CLKDIVN_HDIVN_1:
		hdiv = 1;
		break;

	case S3C2440_CLKDIVN_HDIVN_2:
		hdiv = 2;
		break;

	case S3C2440_CLKDIVN_HDIVN_4_8:
		hdiv = (camdiv & S3C2440_CAMDIVN_HCLK4_HALF) ? 8 : 4;
		break;

	case S3C2440_CLKDIVN_HDIVN_3_6:
		hdiv = (camdiv & S3C2440_CAMDIVN_HCLK3_HALF) ? 6 : 3;
		break;
	}

	hclk = fclk / hdiv;
	pclk = hclk / ((clkdiv & S3C2440_CLKDIVN_PDIVN)? 2:1);

	/* print brief summary of clocks, etc */

	printk("S3C2440: core %ld.%03ld MHz, memory %ld.%03ld MHz, peripheral %ld.%03ld MHz\n",
	       print_mhz(fclk), print_mhz(hclk), print_mhz(pclk));

	/* initialise the clocks here, to allow other things like the
	 * console to use them, and to add new ones after the initialisation
	 */

	s3c24xx_setup_clocks(xtal, fclk, hclk, pclk);
}

/* need to register class before we actually register the device, and
 * we also need to ensure that it has been initialised before any of the
 * drivers even try to use it (even if not on an s3c2440 based system)
 * as a driver which may support both 2410 and 2440 may try and use it.
*/

static int __init s3c2440_core_init(void)
{
	return sysdev_class_register(&s3c2440_sysclass);
}

core_initcall(s3c2440_core_init);

int __init s3c2440_init(void)
{
	int ret;

	printk("S3C2440: Initialising architecture\n");

	ret = sysdev_register(&s3c2440_sysdev);
	if (ret != 0)
		printk(KERN_ERR "failed to register sysdev for s3c2440\n");
	else
		ret = platform_add_devices(s3c24xx_uart_devs, s3c2440_uart_count);

	return ret;
}
