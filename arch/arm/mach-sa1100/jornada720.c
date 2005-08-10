/*
 * linux/arch/arm/mach-sa1100/jornada720.c
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/ioport.h>

#include <asm/hardware.h>
#include <asm/hardware/sa1111.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include "generic.h"


#define JORTUCR_VAL	0x20000400

static struct resource sa1111_resources[] = {
	[0] = {
		.start		= 0x40000000,
		.end		= 0x40001fff,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_GPIO1,
		.end		= IRQ_GPIO1,
		.flags		= IORESOURCE_IRQ,
	},
};

static u64 sa1111_dmamask = 0xffffffffUL;

static struct platform_device sa1111_device = {
	.name		= "sa1111",
	.id		= 0,
	.dev		= {
		.dma_mask = &sa1111_dmamask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(sa1111_resources),
	.resource	= sa1111_resources,
};

static struct platform_device *devices[] __initdata = {
	&sa1111_device,
};

static int __init jornada720_init(void)
{
	int ret = -ENODEV;

	if (machine_is_jornada720()) {
		GPDR |= GPIO_GPIO20;
		TUCR = JORTUCR_VAL;	/* set the oscillator out to the SA-1101 */

		GPSR = GPIO_GPIO20;
		udelay(1);
		GPCR = GPIO_GPIO20;
		udelay(1);
		GPSR = GPIO_GPIO20;
		udelay(20);

		/* LDD4 is speaker, LDD3 is microphone */
		PPSR &= ~(PPC_LDD3 | PPC_LDD4);
		PPDR |= PPC_LDD3 | PPC_LDD4;

		ret = platform_add_devices(devices, ARRAY_SIZE(devices));
	}
	return ret;
}

arch_initcall(jornada720_init);

static struct map_desc jornada720_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xf0000000, 0x48000000, 0x00100000, MT_DEVICE }, /* Epson registers */
  { 0xf1000000, 0x48200000, 0x00100000, MT_DEVICE }, /* Epson frame buffer */
  { 0xf4000000, 0x40000000, 0x00100000, MT_DEVICE }  /* SA-1111 */
};

static void __init jornada720_map_io(void)
{
	sa1100_map_io();
	iotable_init(jornada720_io_desc, ARRAY_SIZE(jornada720_io_desc));
	
	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
}

MACHINE_START(JORNADA720, "HP Jornada 720")
	/* Maintainer: Michael Gernoth <michael@gernoth.net> */
	.phys_ram	= 0xc0000000,
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((0xf8000000) >> 18) & 0xfffc,
	.boot_params	= 0xc0000100,
	.map_io		= jornada720_map_io,
	.init_irq	= sa1100_init_irq,
	.timer		= &sa1100_timer,
MACHINE_END
