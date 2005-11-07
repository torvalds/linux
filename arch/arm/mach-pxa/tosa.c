/*
 *  Support for Sharp SL-C6000x PDAs
 *  Model: (Tosa)
 *
 *  Copyright (c) 2005 Dirk Opfer
 *
 *	Based on code written by Sharp/Lineo for 2.4 kernels
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/mmc/host.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/arch/irda.h>
#include <asm/arch/mmc.h>
#include <asm/arch/udc.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/irq.h>
#include <asm/arch/tosa.h>

#include <asm/hardware/scoop.h>
#include <asm/mach/sharpsl_param.h>

#include "generic.h"


/*
 * SCOOP Device
 */
static struct resource tosa_scoop_resources[] = {
	[0] = {
		.start	= TOSA_CF_PHYS,
		.end	= TOSA_CF_PHYS + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct scoop_config tosa_scoop_setup = {
	.io_dir 	= TOSA_SCOOP_IO_DIR,
	.io_out		= TOSA_SCOOP_IO_OUT,

};

struct platform_device tosascoop_device = {
	.name		= "sharp-scoop",
	.id		= 0,
	.dev		= {
 		.platform_data	= &tosa_scoop_setup,
	},
	.num_resources	= ARRAY_SIZE(tosa_scoop_resources),
	.resource	= tosa_scoop_resources,
};


/*
 * SCOOP Device Jacket
 */
static struct resource tosa_scoop_jc_resources[] = {
	[0] = {
		.start		= TOSA_SCOOP_PHYS + 0x40,
		.end		= TOSA_SCOOP_PHYS + 0xfff,
		.flags		= IORESOURCE_MEM,
	},
};

static struct scoop_config tosa_scoop_jc_setup = {
	.io_dir 	= TOSA_SCOOP_JC_IO_DIR,
	.io_out		= TOSA_SCOOP_JC_IO_OUT,
};

struct platform_device tosascoop_jc_device = {
	.name		= "sharp-scoop",
	.id		= 1,
	.dev		= {
 		.platform_data	= &tosa_scoop_jc_setup,
		.parent 	= &tosascoop_device.dev,
	},
	.num_resources	= ARRAY_SIZE(tosa_scoop_jc_resources),
	.resource	= tosa_scoop_jc_resources,
};

static struct scoop_pcmcia_dev tosa_pcmcia_scoop[] = {
{
	.dev        = &tosascoop_device.dev,
	.irq        = TOSA_IRQ_GPIO_CF_IRQ,
	.cd_irq     = TOSA_IRQ_GPIO_CF_CD,
	.cd_irq_str = "PCMCIA0 CD",
},{
	.dev        = &tosascoop_jc_device.dev,
	.irq        = TOSA_IRQ_GPIO_JC_CF_IRQ,
	.cd_irq     = -1,
},
};


static struct platform_device *devices[] __initdata = {
	&tosascoop_device,
	&tosascoop_jc_device,
};

static void __init tosa_init(void)
{
	pxa_gpio_mode(TOSA_GPIO_ON_RESET | GPIO_IN);
	pxa_gpio_mode(TOSA_GPIO_TC6393_INT | GPIO_IN);

	/* setup sleep mode values */
	PWER  = 0x00000002;
	PFER  = 0x00000000;
	PRER  = 0x00000002;
	PGSR0 = 0x00000000;
	PGSR1 = 0x00FF0002;
	PGSR2 = 0x00014000;
	PCFR |= PCFR_OPDE;

	// enable batt_fault
	PMCR = 0x01;

	platform_add_devices(devices, ARRAY_SIZE(devices));

	scoop_num = 2;
	scoop_devs = &tosa_pcmcia_scoop[0];
}

static void __init fixup_tosa(struct machine_desc *desc,
		struct tag *tags, char **cmdline, struct meminfo *mi)
{
	sharpsl_save_param();
	mi->nr_banks=1;
	mi->bank[0].start = 0xa0000000;
	mi->bank[0].node = 0;
	mi->bank[0].size = (64*1024*1024);
}

MACHINE_START(TOSA, "SHARP Tosa")
	.phys_ram	= 0xa0000000,
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup          = fixup_tosa,
	.map_io         = pxa_map_io,
	.init_irq       = pxa_init_irq,
	.init_machine   = tosa_init,
	.timer          = &pxa_timer,
MACHINE_END
