/*
 * board-og.c -- support for the OpenGear KS8695 based boards.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/devices.h>
#include <mach/regs-gpio.h>
#include <mach/gpio-ks8695.h>
#include "generic.h"

static int og_pci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	if (machine_is_im4004() && (slot == 8))
		return KS8695_IRQ_EXTERN1;
	return KS8695_IRQ_EXTERN0;
}

static struct ks8695_pci_cfg __initdata og_pci = {
	.mode		= KS8695_MODE_PCI,
	.map_irq	= og_pci_map_irq,
};

static void __init og_register_pci(void)
{
	/* Initialize the GPIO lines for interrupt mode */
	ks8695_gpio_interrupt(KS8695_GPIO_0, IRQ_TYPE_LEVEL_LOW);

	/* Cardbus Slot */
	if (machine_is_im4004())
		ks8695_gpio_interrupt(KS8695_GPIO_1, IRQ_TYPE_LEVEL_LOW);

	ks8695_init_pci(&og_pci);
}

/*
 * The PCI bus reset is driven by a dedicated GPIO line. Toggle it here
 * and bring the PCI bus out of reset.
 */
static void __init og_pci_bus_reset(void)
{
	unsigned int rstline = 1;

	/* Some boards use a different GPIO as the PCI reset line */
	if (machine_is_im4004())
		rstline = 2;
	else if (machine_is_im42xx())
		rstline = 0;

	gpio_request(rstline, "PCI reset");
	gpio_direction_output(rstline, 0);

	/* Drive a reset on the PCI reset line */
	gpio_set_value(rstline, 1);
	gpio_set_value(rstline, 0);
	mdelay(100);
	gpio_set_value(rstline, 1);
	mdelay(100);
}

/*
 * Direct connect serial ports (non-PCI that is).
 */
#define	S8250_PHYS	0x03800000
#define	S8250_VIRT	0xf4000000
#define	S8250_SIZE	0x00100000

static struct __initdata map_desc og_io_desc[] = {
	{
		.virtual	= S8250_VIRT,
		.pfn		= __phys_to_pfn(S8250_PHYS),
		.length		= S8250_SIZE,
		.type		= MT_DEVICE,
	}
};

static struct resource og_uart_resources[] = {
	{
		.start		= S8250_VIRT,
		.end		= S8250_VIRT + S8250_SIZE,
		.flags		= IORESOURCE_MEM
	},
};

static struct plat_serial8250_port og_uart_data[] = {
	{
		.mapbase	= S8250_VIRT,
		.membase	= (char *) S8250_VIRT,
		.irq		= 3,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 115200 * 16,
	},
	{ },
};

static struct platform_device og_uart = {
	.name			= "serial8250",
	.id			= 0,
	.dev.platform_data	= og_uart_data,
	.num_resources		= 1,
	.resource		= og_uart_resources
};

static struct platform_device *og_devices[] __initdata = {
	&og_uart
};

static void __init og_init(void)
{
	ks8695_register_gpios();

	if (machine_is_cm4002()) {
		ks8695_gpio_interrupt(KS8695_GPIO_1, IRQ_TYPE_LEVEL_HIGH);
		iotable_init(og_io_desc, ARRAY_SIZE(og_io_desc));
		platform_add_devices(og_devices, ARRAY_SIZE(og_devices));
	} else {
		og_pci_bus_reset();
		og_register_pci();
	}

	ks8695_add_device_lan();
	ks8695_add_device_wan();
}

#ifdef CONFIG_MACH_CM4002
MACHINE_START(CM4002, "OpenGear/CM4002")
	/* OpenGear Inc. */
	.atag_offset	= 0x100,
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.init_machine	= og_init,
	.timer		= &ks8695_timer,
	.restart        = ks8695_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_CM4008
MACHINE_START(CM4008, "OpenGear/CM4008")
	/* OpenGear Inc. */
	.atag_offset	= 0x100,
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.init_machine	= og_init,
	.timer		= &ks8695_timer,
	.restart        = ks8695_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_CM41xx
MACHINE_START(CM41XX, "OpenGear/CM41xx")
	/* OpenGear Inc. */
	.atag_offset	= 0x100,
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.init_machine	= og_init,
	.timer		= &ks8695_timer,
	.restart        = ks8695_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_IM4004
MACHINE_START(IM4004, "OpenGear/IM4004")
	/* OpenGear Inc. */
	.atag_offset	= 0x100,
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.init_machine	= og_init,
	.timer		= &ks8695_timer,
	.restart        = ks8695_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_IM42xx
MACHINE_START(IM42XX, "OpenGear/IM42xx")
	/* OpenGear Inc. */
	.atag_offset	= 0x100,
	.map_io		= ks8695_map_io,
	.init_irq	= ks8695_init_irq,
	.init_machine	= og_init,
	.timer		= &ks8695_timer,
	.restart        = ks8695_restart,
MACHINE_END
#endif
