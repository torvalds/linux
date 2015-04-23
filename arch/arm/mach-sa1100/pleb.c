/*
 * linux/arch/arm/mach-sa1100/pleb.c
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/platform_data/sa11x0-serial.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/mtd/partitions.h>
#include <linux/smc91x.h>

#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <mach/irqs.h>

#include "generic.h"


/*
 * Ethernet IRQ mappings
 */

#define PLEB_ETH0_P		(0x20000300)	/* Ethernet 0 in PCMCIA0 IO */
#define PLEB_ETH0_V		(0xf6000300)

#define GPIO_ETH0_IRQ		GPIO_GPIO(21)
#define GPIO_ETH0_EN		GPIO_GPIO(26)

#define IRQ_GPIO_ETH0_IRQ	IRQ_GPIO21

static struct resource smc91x_resources[] = {
	[0] = DEFINE_RES_MEM(PLEB_ETH0_P, 0x04000000),
#if 0 /* Autoprobe instead, to get rising/falling edge characteristic right */
	[1] = DEFINE_RES_IRQ(IRQ_GPIO_ETH0_IRQ),
#endif
};

static struct smc91x_platdata smc91x_platdata = {
	.flags = SMC91X_USE_16BIT | SMC91X_NOWAIT,
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
	.dev = {
		.platform_data  = &smc91x_platdata,
	},
};

static struct platform_device *devices[] __initdata = {
	&smc91x_device,
};


/*
 * Pleb's memory map
 * has flash memory (typically 4 or 8 meg) selected by
 * the two SA1100 lowest chip select outputs.
 */
static struct resource pleb_flash_resources[] = {
	[0] = DEFINE_RES_MEM(SA1100_CS0_PHYS, SZ_8M),
	[1] = DEFINE_RES_MEM(SA1100_CS1_PHYS, SZ_8M),
};


static struct mtd_partition pleb_partitions[] = {
	{
		.name		= "blob",
		.offset		= 0,
		.size		= 0x00020000,
	}, {
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x000e0000,
	}, {
		.name		= "rootfs",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x00300000,
	}
};


static struct flash_platform_data pleb_flash_data = {
	.map_name = "cfi_probe",
	.parts = pleb_partitions,
	.nr_parts = ARRAY_SIZE(pleb_partitions),
};


static void __init pleb_init(void)
{
	sa11x0_register_mtd(&pleb_flash_data, pleb_flash_resources,
			      ARRAY_SIZE(pleb_flash_resources));


	platform_add_devices(devices, ARRAY_SIZE(devices));
}


static void __init pleb_map_io(void)
{
	sa1100_map_io();

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);

	GAFR |= (GPIO_UART_TXD | GPIO_UART_RXD);
	GPDR |= GPIO_UART_TXD;
	GPDR &= ~GPIO_UART_RXD;
	PPAR |= PPAR_UPR;

	/*
	 * Fix expansion memory timing for network card
	 */
	MECR = ((2<<10) | (2<<5) | (2<<0));

	/*
	 * Enable the SMC ethernet controller
	 */
	GPDR |= GPIO_ETH0_EN;	/* set to output */
	GPCR  = GPIO_ETH0_EN;	/* clear MCLK (enable smc) */

	GPDR &= ~GPIO_ETH0_IRQ;

	irq_set_irq_type(GPIO_ETH0_IRQ, IRQ_TYPE_EDGE_FALLING);
}

MACHINE_START(PLEB, "PLEB")
	.map_io		= pleb_map_io,
	.nr_irqs	= SA1100_NR_IRQS,
	.init_irq	= sa1100_init_irq,
	.init_time	= sa1100_timer_init,
	.init_machine   = pleb_init,
	.init_late	= sa11x0_init_late,
	.restart	= sa11x0_restart,
MACHINE_END
