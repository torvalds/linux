/*
 * linux/arch/arm/mach-sa1100/pleb.c
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/device.h>

#include <linux/mtd/partitions.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <asm/mach/serial_sa1100.h>
#include <asm/arch/irqs.h>

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
	[0] = {
		.start	=  PLEB_ETH0_P,
		.end	=  PLEB_ETH0_P | 0x03ffffff,
		.flags	= IORESOURCE_MEM,
	},
#if 0 /* Autoprobe instead, to get rising/falling edge characteristic right */
	[1] = {
		.start	= IRQ_GPIO_ETH0_IRQ,
		.end	= IRQ_GPIO_ETH0_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
#endif
};


static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
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
	[0] = {
		.start = SA1100_CS0_PHYS,
		.end   = SA1100_CS0_PHYS + SZ_8M - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = SA1100_CS1_PHYS,
		.end   = SA1100_CS1_PHYS + SZ_8M - 1,
		.flags = IORESOURCE_MEM,
	}
};


static struct mtd_partition pleb_partitions[] = {
	{
		.name		= "blob",
		.offset 	= 0,
		.size		= 0x00020000,
	}, {
		.name		= "kernel",
		.offset 	= MTDPART_OFS_APPEND,
		.size		= 0x000e0000,
	}, {
		.name		= "rootfs",
		.offset 	= MTDPART_OFS_APPEND,
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
	sa11x0_set_flash_data(&pleb_flash_data, pleb_flash_resources,
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

	set_irq_type(GPIO_ETH0_IRQ, IRQT_FALLING);
}

MACHINE_START(PLEB, "PLEB")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	MAPIO(pleb_map_io)
	INITIRQ(sa1100_init_irq)
	.timer		= &sa1100_timer,
	.init_machine   = pleb_init,
MACHINE_END
