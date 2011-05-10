/*
 * arch/arm/mach-dove/common.c
 *
 * Core functions for Marvell Dove 88AP510 System On Chip
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/serial_8250.h>
#include <linux/clk.h>
#include <linux/mbus.h>
#include <linux/mv643xx_eth.h>
#include <linux/mv643xx_i2c.h>
#include <linux/ata_platform.h>
#include <linux/spi/orion_spi.h>
#include <linux/gpio.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <asm/timex.h>
#include <asm/hardware/cache-tauros2.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/mach/pci.h>
#include <mach/dove.h>
#include <mach/bridge-regs.h>
#include <asm/mach/arch.h>
#include <linux/irq.h>
#include <plat/mv_xor.h>
#include <plat/ehci-orion.h>
#include <plat/time.h>
#include "common.h"

/*****************************************************************************
 * I/O Address Mapping
 ****************************************************************************/
static struct map_desc dove_io_desc[] __initdata = {
	{
		.virtual	= DOVE_SB_REGS_VIRT_BASE,
		.pfn		= __phys_to_pfn(DOVE_SB_REGS_PHYS_BASE),
		.length		= DOVE_SB_REGS_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= DOVE_NB_REGS_VIRT_BASE,
		.pfn		= __phys_to_pfn(DOVE_NB_REGS_PHYS_BASE),
		.length		= DOVE_NB_REGS_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= DOVE_PCIE0_IO_VIRT_BASE,
		.pfn		= __phys_to_pfn(DOVE_PCIE0_IO_PHYS_BASE),
		.length		= DOVE_PCIE0_IO_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= DOVE_PCIE1_IO_VIRT_BASE,
		.pfn		= __phys_to_pfn(DOVE_PCIE1_IO_PHYS_BASE),
		.length		= DOVE_PCIE1_IO_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init dove_map_io(void)
{
	iotable_init(dove_io_desc, ARRAY_SIZE(dove_io_desc));
}

/*****************************************************************************
 * EHCI
 ****************************************************************************/
static struct orion_ehci_data dove_ehci_data = {
	.dram		= &dove_mbus_dram_info,
	.phy_version	= EHCI_PHY_NA,
};

static u64 ehci_dmamask = DMA_BIT_MASK(32);

/*****************************************************************************
 * EHCI0
 ****************************************************************************/
static struct resource dove_ehci0_resources[] = {
	{
		.start	= DOVE_USB0_PHYS_BASE,
		.end	= DOVE_USB0_PHYS_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_DOVE_USB0,
		.end	= IRQ_DOVE_USB0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dove_ehci0 = {
	.name		= "orion-ehci",
	.id		= 0,
	.dev		= {
		.dma_mask		= &ehci_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &dove_ehci_data,
	},
	.resource	= dove_ehci0_resources,
	.num_resources	= ARRAY_SIZE(dove_ehci0_resources),
};

void __init dove_ehci0_init(void)
{
	platform_device_register(&dove_ehci0);
}

/*****************************************************************************
 * EHCI1
 ****************************************************************************/
static struct resource dove_ehci1_resources[] = {
	{
		.start	= DOVE_USB1_PHYS_BASE,
		.end	= DOVE_USB1_PHYS_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_DOVE_USB1,
		.end	= IRQ_DOVE_USB1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dove_ehci1 = {
	.name		= "orion-ehci",
	.id		= 1,
	.dev		= {
		.dma_mask		= &ehci_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &dove_ehci_data,
	},
	.resource	= dove_ehci1_resources,
	.num_resources	= ARRAY_SIZE(dove_ehci1_resources),
};

void __init dove_ehci1_init(void)
{
	platform_device_register(&dove_ehci1);
}

/*****************************************************************************
 * GE00
 ****************************************************************************/
struct mv643xx_eth_shared_platform_data dove_ge00_shared_data = {
	.t_clk		= 0,
	.dram		= &dove_mbus_dram_info,
};

static struct resource dove_ge00_shared_resources[] = {
	{
		.name	= "ge00 base",
		.start	= DOVE_GE00_PHYS_BASE + 0x2000,
		.end	= DOVE_GE00_PHYS_BASE + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device dove_ge00_shared = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 0,
	.dev		= {
		.platform_data	= &dove_ge00_shared_data,
	},
	.num_resources	= 1,
	.resource	= dove_ge00_shared_resources,
};

static struct resource dove_ge00_resources[] = {
	{
		.name	= "ge00 irq",
		.start	= IRQ_DOVE_GE00_SUM,
		.end	= IRQ_DOVE_GE00_SUM,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dove_ge00 = {
	.name		= MV643XX_ETH_NAME,
	.id		= 0,
	.num_resources	= 1,
	.resource	= dove_ge00_resources,
	.dev		= {
		.coherent_dma_mask	= 0xffffffff,
	},
};

void __init dove_ge00_init(struct mv643xx_eth_platform_data *eth_data)
{
	eth_data->shared = &dove_ge00_shared;
	dove_ge00.dev.platform_data = eth_data;

	platform_device_register(&dove_ge00_shared);
	platform_device_register(&dove_ge00);
}

/*****************************************************************************
 * SoC RTC
 ****************************************************************************/
static struct resource dove_rtc_resource[] = {
	{
		.start	= DOVE_RTC_PHYS_BASE,
		.end	= DOVE_RTC_PHYS_BASE + 32 - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_DOVE_RTC,
		.flags	= IORESOURCE_IRQ,
	}
};

void __init dove_rtc_init(void)
{
	platform_device_register_simple("rtc-mv", -1, dove_rtc_resource, 2);
}

/*****************************************************************************
 * SATA
 ****************************************************************************/
static struct resource dove_sata_resources[] = {
	{
		.name	= "sata base",
		.start	= DOVE_SATA_PHYS_BASE,
		.end	= DOVE_SATA_PHYS_BASE + 0x5000 - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "sata irq",
		.start	= IRQ_DOVE_SATA,
		.end	= IRQ_DOVE_SATA,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dove_sata = {
	.name		= "sata_mv",
	.id		= 0,
	.dev		= {
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(dove_sata_resources),
	.resource	= dove_sata_resources,
};

void __init dove_sata_init(struct mv_sata_platform_data *sata_data)
{
	sata_data->dram = &dove_mbus_dram_info;
	dove_sata.dev.platform_data = sata_data;
	platform_device_register(&dove_sata);
}

/*****************************************************************************
 * UART0
 ****************************************************************************/
static struct plat_serial8250_port dove_uart0_data[] = {
	{
		.mapbase	= DOVE_UART0_PHYS_BASE,
		.membase	= (char *)DOVE_UART0_VIRT_BASE,
		.irq		= IRQ_DOVE_UART_0,
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 0,
	}, {
	},
};

static struct resource dove_uart0_resources[] = {
	{
		.start		= DOVE_UART0_PHYS_BASE,
		.end		= DOVE_UART0_PHYS_BASE + SZ_256 - 1,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IRQ_DOVE_UART_0,
		.end		= IRQ_DOVE_UART_0,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device dove_uart0 = {
	.name			= "serial8250",
	.id			= 0,
	.dev			= {
		.platform_data	= dove_uart0_data,
	},
	.resource		= dove_uart0_resources,
	.num_resources		= ARRAY_SIZE(dove_uart0_resources),
};

void __init dove_uart0_init(void)
{
	platform_device_register(&dove_uart0);
}

/*****************************************************************************
 * UART1
 ****************************************************************************/
static struct plat_serial8250_port dove_uart1_data[] = {
	{
		.mapbase	= DOVE_UART1_PHYS_BASE,
		.membase	= (char *)DOVE_UART1_VIRT_BASE,
		.irq		= IRQ_DOVE_UART_1,
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 0,
	}, {
	},
};

static struct resource dove_uart1_resources[] = {
	{
		.start		= DOVE_UART1_PHYS_BASE,
		.end		= DOVE_UART1_PHYS_BASE + SZ_256 - 1,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IRQ_DOVE_UART_1,
		.end		= IRQ_DOVE_UART_1,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device dove_uart1 = {
	.name			= "serial8250",
	.id			= 1,
	.dev			= {
		.platform_data	= dove_uart1_data,
	},
	.resource		= dove_uart1_resources,
	.num_resources		= ARRAY_SIZE(dove_uart1_resources),
};

void __init dove_uart1_init(void)
{
	platform_device_register(&dove_uart1);
}

/*****************************************************************************
 * UART2
 ****************************************************************************/
static struct plat_serial8250_port dove_uart2_data[] = {
	{
		.mapbase	= DOVE_UART2_PHYS_BASE,
		.membase	= (char *)DOVE_UART2_VIRT_BASE,
		.irq		= IRQ_DOVE_UART_2,
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 0,
	}, {
	},
};

static struct resource dove_uart2_resources[] = {
	{
		.start		= DOVE_UART2_PHYS_BASE,
		.end		= DOVE_UART2_PHYS_BASE + SZ_256 - 1,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IRQ_DOVE_UART_2,
		.end		= IRQ_DOVE_UART_2,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device dove_uart2 = {
	.name			= "serial8250",
	.id			= 2,
	.dev			= {
		.platform_data	= dove_uart2_data,
	},
	.resource		= dove_uart2_resources,
	.num_resources		= ARRAY_SIZE(dove_uart2_resources),
};

void __init dove_uart2_init(void)
{
	platform_device_register(&dove_uart2);
}

/*****************************************************************************
 * UART3
 ****************************************************************************/
static struct plat_serial8250_port dove_uart3_data[] = {
	{
		.mapbase	= DOVE_UART3_PHYS_BASE,
		.membase	= (char *)DOVE_UART3_VIRT_BASE,
		.irq		= IRQ_DOVE_UART_3,
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 0,
	}, {
	},
};

static struct resource dove_uart3_resources[] = {
	{
		.start		= DOVE_UART3_PHYS_BASE,
		.end		= DOVE_UART3_PHYS_BASE + SZ_256 - 1,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IRQ_DOVE_UART_3,
		.end		= IRQ_DOVE_UART_3,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device dove_uart3 = {
	.name			= "serial8250",
	.id			= 3,
	.dev			= {
		.platform_data	= dove_uart3_data,
	},
	.resource		= dove_uart3_resources,
	.num_resources		= ARRAY_SIZE(dove_uart3_resources),
};

void __init dove_uart3_init(void)
{
	platform_device_register(&dove_uart3);
}

/*****************************************************************************
 * SPI0
 ****************************************************************************/
static struct orion_spi_info dove_spi0_data = {
	.tclk		= 0,
};

static struct resource dove_spi0_resources[] = {
	{
		.start	= DOVE_SPI0_PHYS_BASE,
		.end	= DOVE_SPI0_PHYS_BASE + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_DOVE_SPI0,
		.end	= IRQ_DOVE_SPI0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dove_spi0 = {
	.name		= "orion_spi",
	.id		= 0,
	.resource	= dove_spi0_resources,
	.dev		= {
		.platform_data	= &dove_spi0_data,
	},
	.num_resources	= ARRAY_SIZE(dove_spi0_resources),
};

void __init dove_spi0_init(void)
{
	platform_device_register(&dove_spi0);
}

/*****************************************************************************
 * SPI1
 ****************************************************************************/
static struct orion_spi_info dove_spi1_data = {
	.tclk		= 0,
};

static struct resource dove_spi1_resources[] = {
	{
		.start	= DOVE_SPI1_PHYS_BASE,
		.end	= DOVE_SPI1_PHYS_BASE + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_DOVE_SPI1,
		.end	= IRQ_DOVE_SPI1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dove_spi1 = {
	.name		= "orion_spi",
	.id		= 1,
	.resource	= dove_spi1_resources,
	.dev		= {
		.platform_data	= &dove_spi1_data,
	},
	.num_resources	= ARRAY_SIZE(dove_spi1_resources),
};

void __init dove_spi1_init(void)
{
	platform_device_register(&dove_spi1);
}

/*****************************************************************************
 * I2C
 ****************************************************************************/
static struct mv64xxx_i2c_pdata dove_i2c_data = {
	.freq_m		= 10, /* assumes 166 MHz TCLK gets 94.3kHz */
	.freq_n		= 3,
	.timeout	= 1000, /* Default timeout of 1 second */
};

static struct resource dove_i2c_resources[] = {
	{
		.name	= "i2c base",
		.start	= DOVE_I2C_PHYS_BASE,
		.end	= DOVE_I2C_PHYS_BASE + 0x20 - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "i2c irq",
		.start	= IRQ_DOVE_I2C,
		.end	= IRQ_DOVE_I2C,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dove_i2c = {
	.name		= MV64XXX_I2C_CTLR_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(dove_i2c_resources),
	.resource	= dove_i2c_resources,
	.dev		= {
		.platform_data = &dove_i2c_data,
	},
};

void __init dove_i2c_init(void)
{
	platform_device_register(&dove_i2c);
}

/*****************************************************************************
 * Time handling
 ****************************************************************************/
void __init dove_init_early(void)
{
	orion_time_set_base(TIMER_VIRT_BASE);
}

static int get_tclk(void)
{
	/* use DOVE_RESET_SAMPLE_HI/LO to detect tclk */
	return 166666667;
}

static void dove_timer_init(void)
{
	orion_time_init(BRIDGE_VIRT_BASE, BRIDGE_INT_TIMER1_CLR,
			IRQ_DOVE_BRIDGE, get_tclk());
}

struct sys_timer dove_timer = {
	.init = dove_timer_init,
};

/*****************************************************************************
 * XOR
 ****************************************************************************/
static struct mv_xor_platform_shared_data dove_xor_shared_data = {
	.dram		= &dove_mbus_dram_info,
};

/*****************************************************************************
 * XOR 0
 ****************************************************************************/
static u64 dove_xor0_dmamask = DMA_BIT_MASK(32);

static struct resource dove_xor0_shared_resources[] = {
	{
		.name	= "xor 0 low",
		.start	= DOVE_XOR0_PHYS_BASE,
		.end	= DOVE_XOR0_PHYS_BASE + 0xff,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "xor 0 high",
		.start	= DOVE_XOR0_HIGH_PHYS_BASE,
		.end	= DOVE_XOR0_HIGH_PHYS_BASE + 0xff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device dove_xor0_shared = {
	.name		= MV_XOR_SHARED_NAME,
	.id		= 0,
	.dev		= {
		.platform_data = &dove_xor_shared_data,
	},
	.num_resources	= ARRAY_SIZE(dove_xor0_shared_resources),
	.resource	= dove_xor0_shared_resources,
};

static struct resource dove_xor00_resources[] = {
	[0] = {
		.start	= IRQ_DOVE_XOR_00,
		.end	= IRQ_DOVE_XOR_00,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv_xor_platform_data dove_xor00_data = {
	.shared		= &dove_xor0_shared,
	.hw_id		= 0,
	.pool_size	= PAGE_SIZE,
};

static struct platform_device dove_xor00_channel = {
	.name		= MV_XOR_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(dove_xor00_resources),
	.resource	= dove_xor00_resources,
	.dev		= {
		.dma_mask		= &dove_xor0_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(64),
		.platform_data		= &dove_xor00_data,
	},
};

static struct resource dove_xor01_resources[] = {
	[0] = {
		.start	= IRQ_DOVE_XOR_01,
		.end	= IRQ_DOVE_XOR_01,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv_xor_platform_data dove_xor01_data = {
	.shared		= &dove_xor0_shared,
	.hw_id		= 1,
	.pool_size	= PAGE_SIZE,
};

static struct platform_device dove_xor01_channel = {
	.name		= MV_XOR_NAME,
	.id		= 1,
	.num_resources	= ARRAY_SIZE(dove_xor01_resources),
	.resource	= dove_xor01_resources,
	.dev		= {
		.dma_mask		= &dove_xor0_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(64),
		.platform_data		= &dove_xor01_data,
	},
};

void __init dove_xor0_init(void)
{
	platform_device_register(&dove_xor0_shared);

	/*
	 * two engines can't do memset simultaneously, this limitation
	 * satisfied by removing memset support from one of the engines.
	 */
	dma_cap_set(DMA_MEMCPY, dove_xor00_data.cap_mask);
	dma_cap_set(DMA_XOR, dove_xor00_data.cap_mask);
	platform_device_register(&dove_xor00_channel);

	dma_cap_set(DMA_MEMCPY, dove_xor01_data.cap_mask);
	dma_cap_set(DMA_MEMSET, dove_xor01_data.cap_mask);
	dma_cap_set(DMA_XOR, dove_xor01_data.cap_mask);
	platform_device_register(&dove_xor01_channel);
}

/*****************************************************************************
 * XOR 1
 ****************************************************************************/
static u64 dove_xor1_dmamask = DMA_BIT_MASK(32);

static struct resource dove_xor1_shared_resources[] = {
	{
		.name	= "xor 0 low",
		.start	= DOVE_XOR1_PHYS_BASE,
		.end	= DOVE_XOR1_PHYS_BASE + 0xff,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "xor 0 high",
		.start	= DOVE_XOR1_HIGH_PHYS_BASE,
		.end	= DOVE_XOR1_HIGH_PHYS_BASE + 0xff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device dove_xor1_shared = {
	.name		= MV_XOR_SHARED_NAME,
	.id		= 1,
	.dev		= {
		.platform_data = &dove_xor_shared_data,
	},
	.num_resources	= ARRAY_SIZE(dove_xor1_shared_resources),
	.resource	= dove_xor1_shared_resources,
};

static struct resource dove_xor10_resources[] = {
	[0] = {
		.start	= IRQ_DOVE_XOR_10,
		.end	= IRQ_DOVE_XOR_10,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv_xor_platform_data dove_xor10_data = {
	.shared		= &dove_xor1_shared,
	.hw_id		= 0,
	.pool_size	= PAGE_SIZE,
};

static struct platform_device dove_xor10_channel = {
	.name		= MV_XOR_NAME,
	.id		= 2,
	.num_resources	= ARRAY_SIZE(dove_xor10_resources),
	.resource	= dove_xor10_resources,
	.dev		= {
		.dma_mask		= &dove_xor1_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(64),
		.platform_data		= &dove_xor10_data,
	},
};

static struct resource dove_xor11_resources[] = {
	[0] = {
		.start	= IRQ_DOVE_XOR_11,
		.end	= IRQ_DOVE_XOR_11,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv_xor_platform_data dove_xor11_data = {
	.shared		= &dove_xor1_shared,
	.hw_id		= 1,
	.pool_size	= PAGE_SIZE,
};

static struct platform_device dove_xor11_channel = {
	.name		= MV_XOR_NAME,
	.id		= 3,
	.num_resources	= ARRAY_SIZE(dove_xor11_resources),
	.resource	= dove_xor11_resources,
	.dev		= {
		.dma_mask		= &dove_xor1_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(64),
		.platform_data		= &dove_xor11_data,
	},
};

void __init dove_xor1_init(void)
{
	platform_device_register(&dove_xor1_shared);

	/*
	 * two engines can't do memset simultaneously, this limitation
	 * satisfied by removing memset support from one of the engines.
	 */
	dma_cap_set(DMA_MEMCPY, dove_xor10_data.cap_mask);
	dma_cap_set(DMA_XOR, dove_xor10_data.cap_mask);
	platform_device_register(&dove_xor10_channel);

	dma_cap_set(DMA_MEMCPY, dove_xor11_data.cap_mask);
	dma_cap_set(DMA_MEMSET, dove_xor11_data.cap_mask);
	dma_cap_set(DMA_XOR, dove_xor11_data.cap_mask);
	platform_device_register(&dove_xor11_channel);
}

/*****************************************************************************
 * SDIO
 ****************************************************************************/
static u64 sdio_dmamask = DMA_BIT_MASK(32);

static struct resource dove_sdio0_resources[] = {
	{
		.start	= DOVE_SDIO0_PHYS_BASE,
		.end	= DOVE_SDIO0_PHYS_BASE + 0xff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_DOVE_SDIO0,
		.end	= IRQ_DOVE_SDIO0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dove_sdio0 = {
	.name		= "sdhci-dove",
	.id		= 0,
	.dev		= {
		.dma_mask		= &sdio_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= dove_sdio0_resources,
	.num_resources	= ARRAY_SIZE(dove_sdio0_resources),
};

void __init dove_sdio0_init(void)
{
	platform_device_register(&dove_sdio0);
}

static struct resource dove_sdio1_resources[] = {
	{
		.start	= DOVE_SDIO1_PHYS_BASE,
		.end	= DOVE_SDIO1_PHYS_BASE + 0xff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_DOVE_SDIO1,
		.end	= IRQ_DOVE_SDIO1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dove_sdio1 = {
	.name		= "sdhci-dove",
	.id		= 1,
	.dev		= {
		.dma_mask		= &sdio_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= dove_sdio1_resources,
	.num_resources	= ARRAY_SIZE(dove_sdio1_resources),
};

void __init dove_sdio1_init(void)
{
	platform_device_register(&dove_sdio1);
}

void __init dove_init(void)
{
	int tclk;

	tclk = get_tclk();

	printk(KERN_INFO "Dove 88AP510 SoC, ");
	printk(KERN_INFO "TCLK = %dMHz\n", (tclk + 499999) / 1000000);

#ifdef CONFIG_CACHE_TAUROS2
	tauros2_init();
#endif
	dove_setup_cpu_mbus();

	dove_ge00_shared_data.t_clk = tclk;
	dove_uart0_data[0].uartclk = tclk;
	dove_uart1_data[0].uartclk = tclk;
	dove_uart2_data[0].uartclk = tclk;
	dove_uart3_data[0].uartclk = tclk;
	dove_spi0_data.tclk = tclk;
	dove_spi1_data.tclk = tclk;

	/* internal devices that every board has */
	dove_rtc_init();
	dove_xor0_init();
	dove_xor1_init();
}
