/*
 * arch/arm/mach-kirkwood/common.c
 *
 * Core functions for Marvell Kirkwood SoCs
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
#include <linux/ata_platform.h>
#include <linux/spi/orion_spi.h>
#include <asm/page.h>
#include <asm/timex.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <mach/kirkwood.h>
#include <plat/cache-feroceon-l2.h>
#include <plat/ehci-orion.h>
#include <plat/mv_xor.h>
#include <plat/orion_nand.h>
#include <plat/time.h>
#include "common.h"

/*****************************************************************************
 * I/O Address Mapping
 ****************************************************************************/
static struct map_desc kirkwood_io_desc[] __initdata = {
	{
		.virtual	= KIRKWOOD_PCIE_IO_VIRT_BASE,
		.pfn		= __phys_to_pfn(KIRKWOOD_PCIE_IO_PHYS_BASE),
		.length		= KIRKWOOD_PCIE_IO_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= KIRKWOOD_REGS_VIRT_BASE,
		.pfn		= __phys_to_pfn(KIRKWOOD_REGS_PHYS_BASE),
		.length		= KIRKWOOD_REGS_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init kirkwood_map_io(void)
{
	iotable_init(kirkwood_io_desc, ARRAY_SIZE(kirkwood_io_desc));
}


/*****************************************************************************
 * EHCI
 ****************************************************************************/
static struct orion_ehci_data kirkwood_ehci_data = {
	.dram		= &kirkwood_mbus_dram_info,
};

static u64 ehci_dmamask = 0xffffffffUL;


/*****************************************************************************
 * EHCI0
 ****************************************************************************/
static struct resource kirkwood_ehci_resources[] = {
	{
		.start	= USB_PHYS_BASE,
		.end	= USB_PHYS_BASE + 0x0fff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_KIRKWOOD_USB,
		.end	= IRQ_KIRKWOOD_USB,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device kirkwood_ehci = {
	.name		= "orion-ehci",
	.id		= 0,
	.dev		= {
		.dma_mask		= &ehci_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &kirkwood_ehci_data,
	},
	.resource	= kirkwood_ehci_resources,
	.num_resources	= ARRAY_SIZE(kirkwood_ehci_resources),
};

void __init kirkwood_ehci_init(void)
{
	platform_device_register(&kirkwood_ehci);
}


/*****************************************************************************
 * GE00
 ****************************************************************************/
struct mv643xx_eth_shared_platform_data kirkwood_ge00_shared_data = {
	.t_clk		= KIRKWOOD_TCLK,
	.dram		= &kirkwood_mbus_dram_info,
};

static struct resource kirkwood_ge00_shared_resources[] = {
	{
		.name	= "ge00 base",
		.start	= GE00_PHYS_BASE + 0x2000,
		.end	= GE00_PHYS_BASE + 0x3fff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device kirkwood_ge00_shared = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 0,
	.dev		= {
		.platform_data	= &kirkwood_ge00_shared_data,
	},
	.num_resources	= 1,
	.resource	= kirkwood_ge00_shared_resources,
};

static struct resource kirkwood_ge00_resources[] = {
	{
		.name	= "ge00 irq",
		.start	= IRQ_KIRKWOOD_GE00_SUM,
		.end	= IRQ_KIRKWOOD_GE00_SUM,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device kirkwood_ge00 = {
	.name		= MV643XX_ETH_NAME,
	.id		= 0,
	.num_resources	= 1,
	.resource	= kirkwood_ge00_resources,
};

void __init kirkwood_ge00_init(struct mv643xx_eth_platform_data *eth_data)
{
	eth_data->shared = &kirkwood_ge00_shared;
	kirkwood_ge00.dev.platform_data = eth_data;

	platform_device_register(&kirkwood_ge00_shared);
	platform_device_register(&kirkwood_ge00);
}


/*****************************************************************************
 * SoC RTC
 ****************************************************************************/
static struct resource kirkwood_rtc_resource = {
	.start	= RTC_PHYS_BASE,
	.end	= RTC_PHYS_BASE + SZ_16 - 1,
	.flags	= IORESOURCE_MEM,
};

void __init kirkwood_rtc_init(void)
{
	platform_device_register_simple("rtc-mv", -1, &kirkwood_rtc_resource, 1);
}


/*****************************************************************************
 * SATA
 ****************************************************************************/
static struct resource kirkwood_sata_resources[] = {
	{
		.name	= "sata base",
		.start	= SATA_PHYS_BASE,
		.end	= SATA_PHYS_BASE + 0x5000 - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "sata irq",
		.start	= IRQ_KIRKWOOD_SATA,
		.end	= IRQ_KIRKWOOD_SATA,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device kirkwood_sata = {
	.name		= "sata_mv",
	.id		= 0,
	.dev		= {
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(kirkwood_sata_resources),
	.resource	= kirkwood_sata_resources,
};

void __init kirkwood_sata_init(struct mv_sata_platform_data *sata_data)
{
	sata_data->dram = &kirkwood_mbus_dram_info;
	kirkwood_sata.dev.platform_data = sata_data;
	platform_device_register(&kirkwood_sata);
}


/*****************************************************************************
 * SPI
 ****************************************************************************/
static struct orion_spi_info kirkwood_spi_plat_data = {
	.tclk		= KIRKWOOD_TCLK,
};

static struct resource kirkwood_spi_resources[] = {
	{
		.start	= SPI_PHYS_BASE,
		.end	= SPI_PHYS_BASE + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device kirkwood_spi = {
	.name		= "orion_spi",
	.id		= 0,
	.resource	= kirkwood_spi_resources,
	.dev		= {
		.platform_data	= &kirkwood_spi_plat_data,
	},
	.num_resources	= ARRAY_SIZE(kirkwood_spi_resources),
};

void __init kirkwood_spi_init()
{
	platform_device_register(&kirkwood_spi);
}


/*****************************************************************************
 * UART0
 ****************************************************************************/
static struct plat_serial8250_port kirkwood_uart0_data[] = {
	{
		.mapbase	= UART0_PHYS_BASE,
		.membase	= (char *)UART0_VIRT_BASE,
		.irq		= IRQ_KIRKWOOD_UART_0,
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= KIRKWOOD_TCLK,
	}, {
	},
};

static struct resource kirkwood_uart0_resources[] = {
	{
		.start		= UART0_PHYS_BASE,
		.end		= UART0_PHYS_BASE + 0xff,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IRQ_KIRKWOOD_UART_0,
		.end		= IRQ_KIRKWOOD_UART_0,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device kirkwood_uart0 = {
	.name			= "serial8250",
	.id			= 0,
	.dev			= {
		.platform_data	= kirkwood_uart0_data,
	},
	.resource		= kirkwood_uart0_resources,
	.num_resources		= ARRAY_SIZE(kirkwood_uart0_resources),
};

void __init kirkwood_uart0_init(void)
{
	platform_device_register(&kirkwood_uart0);
}


/*****************************************************************************
 * UART1
 ****************************************************************************/
static struct plat_serial8250_port kirkwood_uart1_data[] = {
	{
		.mapbase	= UART1_PHYS_BASE,
		.membase	= (char *)UART1_VIRT_BASE,
		.irq		= IRQ_KIRKWOOD_UART_1,
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= KIRKWOOD_TCLK,
	}, {
	},
};

static struct resource kirkwood_uart1_resources[] = {
	{
		.start		= UART1_PHYS_BASE,
		.end		= UART1_PHYS_BASE + 0xff,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IRQ_KIRKWOOD_UART_1,
		.end		= IRQ_KIRKWOOD_UART_1,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device kirkwood_uart1 = {
	.name			= "serial8250",
	.id			= 1,
	.dev			= {
		.platform_data	= kirkwood_uart1_data,
	},
	.resource		= kirkwood_uart1_resources,
	.num_resources		= ARRAY_SIZE(kirkwood_uart1_resources),
};

void __init kirkwood_uart1_init(void)
{
	platform_device_register(&kirkwood_uart1);
}


/*****************************************************************************
 * XOR
 ****************************************************************************/
static struct mv_xor_platform_shared_data kirkwood_xor_shared_data = {
	.dram		= &kirkwood_mbus_dram_info,
};

static u64 kirkwood_xor_dmamask = DMA_32BIT_MASK;


/*****************************************************************************
 * XOR0
 ****************************************************************************/
static struct resource kirkwood_xor0_shared_resources[] = {
	{
		.name	= "xor 0 low",
		.start	= XOR0_PHYS_BASE,
		.end	= XOR0_PHYS_BASE + 0xff,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "xor 0 high",
		.start	= XOR0_HIGH_PHYS_BASE,
		.end	= XOR0_HIGH_PHYS_BASE + 0xff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device kirkwood_xor0_shared = {
	.name		= MV_XOR_SHARED_NAME,
	.id		= 0,
	.dev		= {
		.platform_data = &kirkwood_xor_shared_data,
	},
	.num_resources	= ARRAY_SIZE(kirkwood_xor0_shared_resources),
	.resource	= kirkwood_xor0_shared_resources,
};

static struct resource kirkwood_xor00_resources[] = {
	[0] = {
		.start	= IRQ_KIRKWOOD_XOR_00,
		.end	= IRQ_KIRKWOOD_XOR_00,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv_xor_platform_data kirkwood_xor00_data = {
	.shared		= &kirkwood_xor0_shared,
	.hw_id		= 0,
	.pool_size	= PAGE_SIZE,
};

static struct platform_device kirkwood_xor00_channel = {
	.name		= MV_XOR_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(kirkwood_xor00_resources),
	.resource	= kirkwood_xor00_resources,
	.dev		= {
		.dma_mask		= &kirkwood_xor_dmamask,
		.coherent_dma_mask	= DMA_64BIT_MASK,
		.platform_data		= (void *)&kirkwood_xor00_data,
	},
};

static struct resource kirkwood_xor01_resources[] = {
	[0] = {
		.start	= IRQ_KIRKWOOD_XOR_01,
		.end	= IRQ_KIRKWOOD_XOR_01,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv_xor_platform_data kirkwood_xor01_data = {
	.shared		= &kirkwood_xor0_shared,
	.hw_id		= 1,
	.pool_size	= PAGE_SIZE,
};

static struct platform_device kirkwood_xor01_channel = {
	.name		= MV_XOR_NAME,
	.id		= 1,
	.num_resources	= ARRAY_SIZE(kirkwood_xor01_resources),
	.resource	= kirkwood_xor01_resources,
	.dev		= {
		.dma_mask		= &kirkwood_xor_dmamask,
		.coherent_dma_mask	= DMA_64BIT_MASK,
		.platform_data		= (void *)&kirkwood_xor01_data,
	},
};

void __init kirkwood_xor0_init(void)
{
	platform_device_register(&kirkwood_xor0_shared);

	/*
	 * two engines can't do memset simultaneously, this limitation
	 * satisfied by removing memset support from one of the engines.
	 */
	dma_cap_set(DMA_MEMCPY, kirkwood_xor00_data.cap_mask);
	dma_cap_set(DMA_XOR, kirkwood_xor00_data.cap_mask);
	platform_device_register(&kirkwood_xor00_channel);

	dma_cap_set(DMA_MEMCPY, kirkwood_xor01_data.cap_mask);
	dma_cap_set(DMA_MEMSET, kirkwood_xor01_data.cap_mask);
	dma_cap_set(DMA_XOR, kirkwood_xor01_data.cap_mask);
	platform_device_register(&kirkwood_xor01_channel);
}


/*****************************************************************************
 * XOR1
 ****************************************************************************/
static struct resource kirkwood_xor1_shared_resources[] = {
	{
		.name	= "xor 1 low",
		.start	= XOR1_PHYS_BASE,
		.end	= XOR1_PHYS_BASE + 0xff,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "xor 1 high",
		.start	= XOR1_HIGH_PHYS_BASE,
		.end	= XOR1_HIGH_PHYS_BASE + 0xff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device kirkwood_xor1_shared = {
	.name		= MV_XOR_SHARED_NAME,
	.id		= 1,
	.dev		= {
		.platform_data = &kirkwood_xor_shared_data,
	},
	.num_resources	= ARRAY_SIZE(kirkwood_xor1_shared_resources),
	.resource	= kirkwood_xor1_shared_resources,
};

static struct resource kirkwood_xor10_resources[] = {
	[0] = {
		.start	= IRQ_KIRKWOOD_XOR_10,
		.end	= IRQ_KIRKWOOD_XOR_10,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv_xor_platform_data kirkwood_xor10_data = {
	.shared		= &kirkwood_xor1_shared,
	.hw_id		= 0,
	.pool_size	= PAGE_SIZE,
};

static struct platform_device kirkwood_xor10_channel = {
	.name		= MV_XOR_NAME,
	.id		= 2,
	.num_resources	= ARRAY_SIZE(kirkwood_xor10_resources),
	.resource	= kirkwood_xor10_resources,
	.dev		= {
		.dma_mask		= &kirkwood_xor_dmamask,
		.coherent_dma_mask	= DMA_64BIT_MASK,
		.platform_data		= (void *)&kirkwood_xor10_data,
	},
};

static struct resource kirkwood_xor11_resources[] = {
	[0] = {
		.start	= IRQ_KIRKWOOD_XOR_11,
		.end	= IRQ_KIRKWOOD_XOR_11,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv_xor_platform_data kirkwood_xor11_data = {
	.shared		= &kirkwood_xor1_shared,
	.hw_id		= 1,
	.pool_size	= PAGE_SIZE,
};

static struct platform_device kirkwood_xor11_channel = {
	.name		= MV_XOR_NAME,
	.id		= 3,
	.num_resources	= ARRAY_SIZE(kirkwood_xor11_resources),
	.resource	= kirkwood_xor11_resources,
	.dev		= {
		.dma_mask		= &kirkwood_xor_dmamask,
		.coherent_dma_mask	= DMA_64BIT_MASK,
		.platform_data		= (void *)&kirkwood_xor11_data,
	},
};

void __init kirkwood_xor1_init(void)
{
	platform_device_register(&kirkwood_xor1_shared);

	/*
	 * two engines can't do memset simultaneously, this limitation
	 * satisfied by removing memset support from one of the engines.
	 */
	dma_cap_set(DMA_MEMCPY, kirkwood_xor10_data.cap_mask);
	dma_cap_set(DMA_XOR, kirkwood_xor10_data.cap_mask);
	platform_device_register(&kirkwood_xor10_channel);

	dma_cap_set(DMA_MEMCPY, kirkwood_xor11_data.cap_mask);
	dma_cap_set(DMA_MEMSET, kirkwood_xor11_data.cap_mask);
	dma_cap_set(DMA_XOR, kirkwood_xor11_data.cap_mask);
	platform_device_register(&kirkwood_xor11_channel);
}


/*****************************************************************************
 * Time handling
 ****************************************************************************/
static void kirkwood_timer_init(void)
{
	orion_time_init(IRQ_KIRKWOOD_BRIDGE, KIRKWOOD_TCLK);
}

struct sys_timer kirkwood_timer = {
	.init = kirkwood_timer_init,
};


/*****************************************************************************
 * General
 ****************************************************************************/
static char * __init kirkwood_id(void)
{
	switch (readl(DEVICE_ID) & 0x3) {
	case 0:
		return "88F6180";
	case 1:
		return "88F6192";
	case 2:
		return "88F6281";
	}

	return "unknown 88F6000 variant";
}

static int __init is_l2_writethrough(void)
{
	return !!(readl(L2_CONFIG_REG) & L2_WRITETHROUGH);
}

void __init kirkwood_init(void)
{
	printk(KERN_INFO "Kirkwood: %s, TCLK=%d.\n",
		kirkwood_id(), KIRKWOOD_TCLK);

	kirkwood_setup_cpu_mbus();

#ifdef CONFIG_CACHE_FEROCEON_L2
	feroceon_l2_init(is_l2_writethrough());
#endif
}
