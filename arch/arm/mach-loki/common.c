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
#include <linux/mv643xx_eth.h>
#include <asm/page.h>
#include <asm/timex.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <mach/loki.h>
#include <plat/orion_nand.h>
#include <plat/time.h>
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
 * GE0
 ****************************************************************************/
struct mv643xx_eth_shared_platform_data loki_ge0_shared_data = {
	.t_clk		= LOKI_TCLK,
	.dram		= &loki_mbus_dram_info,
};

static struct resource loki_ge0_shared_resources[] = {
	{
		.name	= "ge0 base",
		.start	= GE0_PHYS_BASE + 0x2000,
		.end	= GE0_PHYS_BASE + 0x3fff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device loki_ge0_shared = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 0,
	.dev		= {
		.platform_data	= &loki_ge0_shared_data,
	},
	.num_resources	= 1,
	.resource	= loki_ge0_shared_resources,
};

static struct resource loki_ge0_resources[] = {
	{
		.name	= "ge0 irq",
		.start	= IRQ_LOKI_GBE_A_INT,
		.end	= IRQ_LOKI_GBE_A_INT,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device loki_ge0 = {
	.name		= MV643XX_ETH_NAME,
	.id		= 0,
	.num_resources	= 1,
	.resource	= loki_ge0_resources,
};

void __init loki_ge0_init(struct mv643xx_eth_platform_data *eth_data)
{
	eth_data->shared = &loki_ge0_shared;
	loki_ge0.dev.platform_data = eth_data;

	writel(0x00079220, GE0_VIRT_BASE + 0x20b0);
	platform_device_register(&loki_ge0_shared);
	platform_device_register(&loki_ge0);
}


/*****************************************************************************
 * GE1
 ****************************************************************************/
struct mv643xx_eth_shared_platform_data loki_ge1_shared_data = {
	.t_clk		= LOKI_TCLK,
	.dram		= &loki_mbus_dram_info,
};

static struct resource loki_ge1_shared_resources[] = {
	{
		.name	= "ge1 base",
		.start	= GE1_PHYS_BASE + 0x2000,
		.end	= GE1_PHYS_BASE + 0x3fff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device loki_ge1_shared = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 1,
	.dev		= {
		.platform_data	= &loki_ge1_shared_data,
	},
	.num_resources	= 1,
	.resource	= loki_ge1_shared_resources,
};

static struct resource loki_ge1_resources[] = {
	{
		.name	= "ge1 irq",
		.start	= IRQ_LOKI_GBE_B_INT,
		.end	= IRQ_LOKI_GBE_B_INT,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device loki_ge1 = {
	.name		= MV643XX_ETH_NAME,
	.id		= 1,
	.num_resources	= 1,
	.resource	= loki_ge1_resources,
};

void __init loki_ge1_init(struct mv643xx_eth_platform_data *eth_data)
{
	eth_data->shared = &loki_ge1_shared;
	loki_ge1.dev.platform_data = eth_data;

	writel(0x00079220, GE1_VIRT_BASE + 0x20b0);
	platform_device_register(&loki_ge1_shared);
	platform_device_register(&loki_ge1);
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
		.coherent_dma_mask	= 0xffffffff,
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
static struct plat_serial8250_port loki_uart0_data[] = {
	{
		.mapbase	= UART0_PHYS_BASE,
		.membase	= (char *)UART0_VIRT_BASE,
		.irq		= IRQ_LOKI_UART0,
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= LOKI_TCLK,
	}, {
	},
};

static struct resource loki_uart0_resources[] = {
	{
		.start		= UART0_PHYS_BASE,
		.end		= UART0_PHYS_BASE + 0xff,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IRQ_LOKI_UART0,
		.end		= IRQ_LOKI_UART0,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device loki_uart0 = {
	.name			= "serial8250",
	.id			= 0,
	.dev			= {
		.platform_data	= loki_uart0_data,
	},
	.resource		= loki_uart0_resources,
	.num_resources		= ARRAY_SIZE(loki_uart0_resources),
};

void __init loki_uart0_init(void)
{
	platform_device_register(&loki_uart0);
}


/*****************************************************************************
 * UART1
 ****************************************************************************/
static struct plat_serial8250_port loki_uart1_data[] = {
	{
		.mapbase	= UART1_PHYS_BASE,
		.membase	= (char *)UART1_VIRT_BASE,
		.irq		= IRQ_LOKI_UART1,
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= LOKI_TCLK,
	}, {
	},
};

static struct resource loki_uart1_resources[] = {
	{
		.start		= UART1_PHYS_BASE,
		.end		= UART1_PHYS_BASE + 0xff,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IRQ_LOKI_UART1,
		.end		= IRQ_LOKI_UART1,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device loki_uart1 = {
	.name			= "serial8250",
	.id			= 1,
	.dev			= {
		.platform_data	= loki_uart1_data,
	},
	.resource		= loki_uart1_resources,
	.num_resources		= ARRAY_SIZE(loki_uart1_resources),
};

void __init loki_uart1_init(void)
{
	platform_device_register(&loki_uart1);
}


/*****************************************************************************
 * Time handling
 ****************************************************************************/
static void loki_timer_init(void)
{
	orion_time_init(IRQ_LOKI_BRIDGE, LOKI_TCLK);
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
