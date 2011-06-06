/*
 * arch/arm/mach-loki/common.c
 *
 * Core functions for Marvell Loki (88RC8480) SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/mbus.h>
#include <linux/dma-mapping.h>
#include <asm/page.h>
#include <asm/timex.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <mach/bridge-regs.h>
#include <mach/loki.h>
#include <plat/orion_nand.h>
#include <plat/time.h>
#include <plat/common.h>
#include "common.h"

/*****************************************************************************
 * I/O Address Mapping
 ****************************************************************************/
static struct map_desc loki_io_desc[] __initdata = {
	{
		.virtual	= LOKI_REGS_VIRT_BASE,
		.pfn		= __phys_to_pfn(LOKI_REGS_PHYS_BASE),
		.length		= LOKI_REGS_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init loki_map_io(void)
{
	iotable_init(loki_io_desc, ARRAY_SIZE(loki_io_desc));
}


/*****************************************************************************
 * GE00
 ****************************************************************************/
void __init loki_ge0_init(struct mv643xx_eth_platform_data *eth_data)
{
	writel(0x00079220, GE0_VIRT_BASE + 0x20b0);

	orion_ge00_init(eth_data, &loki_mbus_dram_info,
			GE0_PHYS_BASE, IRQ_LOKI_GBE_A_INT,
			0, LOKI_TCLK);
}


/*****************************************************************************
 * GE01
 ****************************************************************************/
void __init loki_ge1_init(struct mv643xx_eth_platform_data *eth_data)
{
	writel(0x00079220, GE1_VIRT_BASE + 0x20b0);

	orion_ge01_init(eth_data, &loki_mbus_dram_info,
			GE1_PHYS_BASE, IRQ_LOKI_GBE_B_INT,
			0, LOKI_TCLK);
}


/*****************************************************************************
 * SAS/SATA
 ****************************************************************************/
static struct resource loki_sas_resources[] = {
	{
		.name	= "mvsas0 mem",
		.start	= SAS0_PHYS_BASE,
		.end	= SAS0_PHYS_BASE + 0x01ff,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "mvsas0 irq",
		.start	= IRQ_LOKI_SAS_A,
		.end	= IRQ_LOKI_SAS_A,
		.flags	= IORESOURCE_IRQ,
	}, {
		.name	= "mvsas1 mem",
		.start	= SAS1_PHYS_BASE,
		.end	= SAS1_PHYS_BASE + 0x01ff,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "mvsas1 irq",
		.start	= IRQ_LOKI_SAS_B,
		.end	= IRQ_LOKI_SAS_B,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device loki_sas = {
	.name		= "mvsas",
	.id		= 0,
	.dev		= {
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(loki_sas_resources),
	.resource	= loki_sas_resources,
};

void __init loki_sas_init(void)
{
	writel(0x8300f707, DDR_REG(0x1424));
	platform_device_register(&loki_sas);
}


/*****************************************************************************
 * UART0
 ****************************************************************************/
void __init loki_uart0_init(void)
{
	orion_uart0_init(UART0_VIRT_BASE, UART0_PHYS_BASE,
			 IRQ_LOKI_UART0, LOKI_TCLK);
}

/*****************************************************************************
 * UART1
 ****************************************************************************/
void __init loki_uart1_init(void)
{
	orion_uart1_init(UART1_VIRT_BASE, UART1_PHYS_BASE,
			 IRQ_LOKI_UART1, LOKI_TCLK);
}


/*****************************************************************************
 * Time handling
 ****************************************************************************/
void __init loki_init_early(void)
{
	orion_time_set_base(TIMER_VIRT_BASE);
}

static void loki_timer_init(void)
{
	orion_time_init(BRIDGE_VIRT_BASE, BRIDGE_INT_TIMER1_CLR,
			IRQ_LOKI_BRIDGE, LOKI_TCLK);
}

struct sys_timer loki_timer = {
	.init = loki_timer_init,
};


/*****************************************************************************
 * General
 ****************************************************************************/
void __init loki_init(void)
{
	printk(KERN_INFO "Loki ID: 88RC8480. TCLK=%d.\n", LOKI_TCLK);

	loki_setup_cpu_mbus();
}
