/*
 *  linux/arch/arm/mach-versatile/versatile_pb.c
 *
 *  Copyright (C) 2004 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#include <linux/init.h>
#include <linux/device.h>
#include <linux/sysdev.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl061.h>
#include <linux/amba/mmci.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>

#include "core.h"

#if 1
#define IRQ_MMCI1A	IRQ_VICSOURCE23
#else
#define IRQ_MMCI1A	IRQ_SIC_MMCI1A
#endif

static struct mmci_platform_data mmc1_plat_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= mmc_status,
	.gpio_wp	= -1,
	.gpio_cd	= -1,
};

static struct pl061_platform_data gpio2_plat_data = {
	.gpio_base	= 16,
	.irq_base	= IRQ_GPIO2_START,
};

static struct pl061_platform_data gpio3_plat_data = {
	.gpio_base	= 24,
	.irq_base	= IRQ_GPIO3_START,
};

#define UART3_IRQ	{ IRQ_SIC_UART3, NO_IRQ }
#define UART3_DMA	{ 0x86, 0x87 }
#define SCI1_IRQ	{ IRQ_SIC_SCI3, NO_IRQ }
#define SCI1_DMA	{ 0x88, 0x89 }
#define MMCI1_IRQ	{ IRQ_MMCI1A, IRQ_SIC_MMCI1B }
#define MMCI1_DMA	{ 0x85, 0 }

/*
 * These devices are connected via the core APB bridge
 */
#define GPIO2_IRQ	{ IRQ_GPIOINT2, NO_IRQ }
#define GPIO2_DMA	{ 0, 0 }
#define GPIO3_IRQ	{ IRQ_GPIOINT3, NO_IRQ }
#define GPIO3_DMA	{ 0, 0 }

/*
 * These devices are connected via the DMA APB bridge
 */

/* FPGA Primecells */
AMBA_DEVICE(uart3, "fpga:09", UART3,    NULL);
AMBA_DEVICE(sci1,  "fpga:0a", SCI1,     NULL);
AMBA_DEVICE(mmc1,  "fpga:0b", MMCI1,    &mmc1_plat_data);

/* DevChip Primecells */
AMBA_DEVICE(gpio2, "dev:e6",  GPIO2,    &gpio2_plat_data);
AMBA_DEVICE(gpio3, "dev:e7",  GPIO3,    &gpio3_plat_data);

static struct amba_device *amba_devs[] __initdata = {
	&uart3_device,
	&gpio2_device,
	&gpio3_device,
	&sci1_device,
	&mmc1_device,
};

static void __init versatile_pb_init(void)
{
	int i;

	versatile_init();

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}
}

MACHINE_START(VERSATILE_PB, "ARM-Versatile PB")
	/* Maintainer: ARM Ltd/Deep Blue Solutions Ltd */
	.boot_params	= 0x00000100,
	.map_io		= versatile_map_io,
	.init_irq	= versatile_init_irq,
	.timer		= &versatile_timer,
	.init_machine	= versatile_pb_init,
MACHINE_END
