/* linux/arch/arm/mach-s3c2410/s3c2410.c
 *
 * Copyright (c) 2003-2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.simtec.co.uk/products/EB2410ITX/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *     16-May-2003 BJD  Created initial version
 *     16-Aug-2003 BJD  Fixed header files and copyright, added URL
 *     05-Sep-2003 BJD  Moved to kernel v2.6
 *     18-Jan-2004 BJD  Added serial port configuration
 *     21-Aug-2004 BJD  Added new struct s3c2410_board handler
 *     28-Sep-2004 BJD  Updates for new serial port bits
 *     04-Nov-2004 BJD  Updated UART configuration process
 *     10-Jan-2005 BJD  Removed s3c2410_clock_tick_rate
 *     13-Aug-2005 DA   Removed UART from initial I/O mappings
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <asm/arch/regs-clock.h>
#include <asm/arch/regs-serial.h>

#include "s3c2410.h"
#include "cpu.h"
#include "clock.h"

/* Initial IO mappings */

static struct map_desc s3c2410_iodesc[] __initdata = {
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
	.name		  = "s3c2410-uart",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(s3c_uart0_resource),
	.resource	  = s3c_uart0_resource,
};


static struct platform_device s3c_uart1 = {
	.name		  = "s3c2410-uart",
	.id		  = 1,
	.num_resources	  = ARRAY_SIZE(s3c_uart1_resource),
	.resource	  = s3c_uart1_resource,
};

static struct platform_device s3c_uart2 = {
	.name		  = "s3c2410-uart",
	.id		  = 2,
	.num_resources	  = ARRAY_SIZE(s3c_uart2_resource),
	.resource	  = s3c_uart2_resource,
};

static struct platform_device *uart_devices[] __initdata = {
	&s3c_uart0,
	&s3c_uart1,
	&s3c_uart2
};

static int s3c2410_uart_count = 0;

/* uart registration process */

void __init s3c2410_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	struct platform_device *platdev;
	int uart;

	for (uart = 0; uart < no; uart++, cfg++) {
		platdev = uart_devices[cfg->hwport];

		s3c24xx_uart_devs[uart] = platdev;
		platdev->dev.platform_data = cfg;
	}

	s3c2410_uart_count = uart;
}

/* s3c2410_map_io
 *
 * register the standard cpu IO areas, and any passed in from the
 * machine specific initialisation.
*/

void __init s3c2410_map_io(struct map_desc *mach_desc, int mach_size)
{
	/* register our io-tables */

	iotable_init(s3c2410_iodesc, ARRAY_SIZE(s3c2410_iodesc));
	iotable_init(mach_desc, mach_size);
}

void __init s3c2410_init_clocks(int xtal)
{
	unsigned long tmp;
	unsigned long fclk;
	unsigned long hclk;
	unsigned long pclk;

	/* now we've got our machine bits initialised, work out what
	 * clocks we've got */

	fclk = s3c2410_get_pll(__raw_readl(S3C2410_MPLLCON), xtal);

	tmp = __raw_readl(S3C2410_CLKDIVN);

	/* work out clock scalings */

	hclk = fclk / ((tmp & S3C2410_CLKDIVN_HDIVN) ? 2 : 1);
	pclk = hclk / ((tmp & S3C2410_CLKDIVN_PDIVN) ? 2 : 1);

	/* print brieft summary of clocks, etc */

	printk("S3C2410: core %ld.%03ld MHz, memory %ld.%03ld MHz, peripheral %ld.%03ld MHz\n",
	       print_mhz(fclk), print_mhz(hclk), print_mhz(pclk));

	/* initialise the clocks here, to allow other things like the
	 * console to use them
	 */

	s3c24xx_setup_clocks(xtal, fclk, hclk, pclk);
}

int __init s3c2410_init(void)
{
	printk("S3C2410: Initialising architecture\n");

	return platform_add_devices(s3c24xx_uart_devs, s3c2410_uart_count);
}
