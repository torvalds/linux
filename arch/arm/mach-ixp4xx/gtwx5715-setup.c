/*
 * arch/arm/mach-ixp4xx/gtwx5715-setup.c
 *
 * Gemtek GTWX5715 (Linksys WRV54G) board settup
 *
 * Copyright (C) 2004 George T. Joseph
 * Derived from Coyote
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_8250.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/arch/gtwx5715.h>

/*
 * Xscale UART registers are 32 bits wide with only the least
 * significant 8 bits having any meaning.  From a configuration
 * perspective, this means 2 things...
 *
 *   Setting .regshift = 2 so that the standard 16550 registers
 *   line up on every 4th byte.
 *
 *   Shifting the register start virtual address +3 bytes when
 *   compiled big-endian.  Since register writes are done on a
 *   single byte basis, if the shift isn't done the driver will
 *   write the value into the most significant byte of the register,
 *   which is ignored, instead of the least significant.
 */

#ifdef	__ARMEB__
#define	REG_OFFSET	3
#else
#define	REG_OFFSET	0
#endif

/*
 * Only the second or "console" uart is connected on the gtwx5715.
 */

static struct resource gtwx5715_uart_resources[] = {
	{
		.start	= IXP4XX_UART2_BASE_PHYS,
		.end	= IXP4XX_UART2_BASE_PHYS + 0x0fff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_IXP4XX_UART2,
		.end	= IRQ_IXP4XX_UART2,
		.flags	= IORESOURCE_IRQ,
	},
	{ },
};


static struct plat_serial8250_port gtwx5715_uart_platform_data[] = {
	{
	.mapbase	= IXP4XX_UART2_BASE_PHYS,
	.membase	= (char *)IXP4XX_UART2_BASE_VIRT + REG_OFFSET,
	.irq		= IRQ_IXP4XX_UART2,
	.flags		= UPF_BOOT_AUTOCONF,
	.iotype		= UPIO_MEM,
	.regshift	= 2,
	.uartclk	= IXP4XX_UART_XTAL,
	},
	{ },
};

static struct platform_device gtwx5715_uart_device = {
	.name		= "serial8250",
	.id		= 0,
	.dev			= {
		.platform_data	= gtwx5715_uart_platform_data,
	},
	.num_resources	= 2,
	.resource	= gtwx5715_uart_resources,
};


void __init gtwx5715_map_io(void)
{
	ixp4xx_map_io();
}

static struct flash_platform_data gtwx5715_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
};

static struct resource gtwx5715_flash_resource = {
	.start		= GTWX5715_FLASH_BASE,
	.end		= GTWX5715_FLASH_BASE + GTWX5715_FLASH_SIZE,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device gtwx5715_flash = {
	.name		= "IXP4XX-Flash",
	.id		= 0,
	.dev		= {
		.platform_data = &gtwx5715_flash_data,
	},
	.num_resources	= 1,
	.resource	= &gtwx5715_flash_resource,
};

static struct platform_device *gtwx5715_devices[] __initdata = {
	&gtwx5715_uart_device,
	&gtwx5715_flash,
};

static void __init gtwx5715_init(void)
{
	platform_add_devices(gtwx5715_devices, ARRAY_SIZE(gtwx5715_devices));
}


MACHINE_START(GTWX5715, "Gemtek GTWX5715 (Linksys WRV54G)")
        MAINTAINER("George Joseph")
        BOOT_MEM(PHYS_OFFSET, IXP4XX_UART2_BASE_PHYS,
                IXP4XX_UART2_BASE_VIRT)
        MAPIO(gtwx5715_map_io)
        INITIRQ(ixp4xx_init_irq)
		  .timer		= &ixp4xx_timer,
        BOOT_PARAMS(0x0100)
        INIT_MACHINE(gtwx5715_init)
MACHINE_END


