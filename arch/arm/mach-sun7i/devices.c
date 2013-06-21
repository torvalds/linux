/*
 *  arch/arm/mach-$chip/devices.c
 *
 *  Copyright (C) 2012 AllWinner Limited
 *  Benn Huang <benn@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/pda_power.h>
#include <linux/io.h>
#include <linux/i2c.h>
 
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>
#include <mach/includes.h>

/* uart */
static struct plat_serial8250_port debug_uart_platform_data[] = {
	{
		.membase        = (void __iomem *)SW_VA_UART0_IO_BASE,
		.mapbase        = (resource_size_t)SW_PA_UART0_IO_BASE,
		.irq            = 33,
		.flags          = UPF_BOOT_AUTOCONF|UPF_IOREMAP,
		.iotype         = UPIO_MEM32,
		.regshift       = 2,
		.uartclk        = 24000000,
	}, {
		.flags          = 0,
	}
 };

static struct platform_device debug_uart = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = &debug_uart_platform_data[0],
	},
};

/* dma */
static u64 sw_dmac_dmamask = DMA_BIT_MASK(32);

static struct resource sw_dmac_resources[] = {
	[0] = {
		.start 	= SW_PA_DMAC_IO_BASE,
		.end 	= SW_PA_DMAC_IO_BASE + 0x1000,
		.flags 	= IORESOURCE_MEM,
	},
	[1] = {
		.start 	= AW_IRQ_DMA,
		.end 	= AW_IRQ_DMA,
		.flags 	= IORESOURCE_IRQ
	}
};

static struct platform_device sw_dmac_device = {
	.name 		= "sw_dmac",	/* must be same as sw_dmac_driver's name */
	.id 		= 0, 		/* there is only one device for sw_dmac dirver, so id is 0 */
	.num_resources 	= ARRAY_SIZE(sw_dmac_resources),
	.resource 	= sw_dmac_resources,
	.dev 		= {
				.dma_mask = &sw_dmac_dmamask,
				.coherent_dma_mask = DMA_BIT_MASK(32),	/* validate dma_pool_alloc */
			  },
};

struct platform_device sw_pdev_nand =
{
	.name = "sw_nand",
	.id = -1,
};
static struct platform_device *sw_pdevs[] __initdata = {
	&debug_uart,
	&sw_dmac_device,
	&sw_pdev_nand,
};

void sw_pdev_init(void)
{
	platform_add_devices(sw_pdevs, ARRAY_SIZE(sw_pdevs));
}

