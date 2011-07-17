/*
 * linux/arch/arm/mach-pxa/capc7117.c
 *
 * Support for the Embedian CAPC-7117 Evaluation Kit
 * based on the Embedian MXM-8x10 Computer on Module
 *
 * Copyright (C) 2009 Embedian Inc.
 * Copyright (C) 2009 TMT Services & Supplies (Pty) Ltd.
 *
 * 2007-09-04: eric miao <eric.y.miao@gmail.com>
 *             rewrite to align with latest kernel
 *
 * 2010-01-09: Edwin Peer <epeer@tmtservices.co.za>
 *             Hennie van der Merwe <hvdmerwe@tmtservices.co.za>
 *             rework for upstream merge
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/serial_8250.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/pxa320.h>
#include <mach/mxm8x10.h>

#include "generic.h"

/* IDE (PATA) Support */
static struct pata_platform_info pata_platform_data = {
	.ioport_shift = 1
};

static struct resource capc7117_ide_resources[] = {
	[0] = {
	       .start = 0x11000020,
	       .end = 0x1100003f,
	       .flags = IORESOURCE_MEM
	},
	[1] = {
	       .start = 0x1100001c,
	       .end = 0x1100001c,
	       .flags = IORESOURCE_MEM
	},
	[2] = {
	       .start = gpio_to_irq(mfp_to_gpio(MFP_PIN_GPIO76)),
	       .end = gpio_to_irq(mfp_to_gpio(MFP_PIN_GPIO76)),
	       .flags = IORESOURCE_IRQ | IRQF_TRIGGER_RISING
	}
};

static struct platform_device capc7117_ide_device = {
	.name = "pata_platform",
	.num_resources = ARRAY_SIZE(capc7117_ide_resources),
	.resource = capc7117_ide_resources,
	.dev = {
		.platform_data = &pata_platform_data,
		.coherent_dma_mask = ~0		/* grumble */
	}
};

static void __init capc7117_ide_init(void)
{
	platform_device_register(&capc7117_ide_device);
}

/* TI16C752 UART support */
#define	TI16C752_FLAGS		(UPF_BOOT_AUTOCONF | \
					UPF_IOREMAP | \
					UPF_BUGGY_UART | \
					UPF_SKIP_TEST)
#define	TI16C752_UARTCLK	(22118400)
static struct plat_serial8250_port ti16c752_platform_data[] = {
	[0] = {
	       .mapbase = 0x14000000,
	       .irq = gpio_to_irq(mfp_to_gpio(MFP_PIN_GPIO78)),
	       .irqflags = IRQF_TRIGGER_RISING,
	       .flags = TI16C752_FLAGS,
	       .iotype = UPIO_MEM,
	       .regshift = 1,
	       .uartclk = TI16C752_UARTCLK
	},
	[1] = {
	       .mapbase = 0x14000040,
	       .irq = gpio_to_irq(mfp_to_gpio(MFP_PIN_GPIO79)),
	       .irqflags = IRQF_TRIGGER_RISING,
	       .flags = TI16C752_FLAGS,
	       .iotype = UPIO_MEM,
	       .regshift = 1,
	       .uartclk = TI16C752_UARTCLK
	},
	[2] = {
	       .mapbase = 0x14000080,
	       .irq = gpio_to_irq(mfp_to_gpio(MFP_PIN_GPIO80)),
	       .irqflags = IRQF_TRIGGER_RISING,
	       .flags = TI16C752_FLAGS,
	       .iotype = UPIO_MEM,
	       .regshift = 1,
	       .uartclk = TI16C752_UARTCLK
	},
	[3] = {
	       .mapbase = 0x140000c0,
	       .irq = gpio_to_irq(mfp_to_gpio(MFP_PIN_GPIO81)),
	       .irqflags = IRQF_TRIGGER_RISING,
	       .flags = TI16C752_FLAGS,
	       .iotype = UPIO_MEM,
	       .regshift = 1,
	       .uartclk = TI16C752_UARTCLK
	},
	[4] = {
	       /* end of array */
	}
};

static struct platform_device ti16c752_device = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = ti16c752_platform_data
	}
};

static void __init capc7117_uarts_init(void)
{
	platform_device_register(&ti16c752_device);
}

static void __init capc7117_init(void)
{
	/* Init CoM */
	mxm_8x10_barebones_init();

	/* Init evaluation board peripherals */
	mxm_8x10_ac97_init();
	mxm_8x10_usb_host_init();
	mxm_8x10_mmc_init();

	capc7117_uarts_init();
	capc7117_ide_init();
}

MACHINE_START(CAPC7117,
	      "Embedian CAPC-7117 evaluation kit based on the MXM-8x10 CoM")
	.boot_params = 0xa0000100,
	.map_io = pxa3xx_map_io,
	.init_irq = pxa3xx_init_irq,
	.handle_irq = pxa3xx_handle_irq,
	.timer = &pxa_timer,
	.init_machine = capc7117_init
MACHINE_END
