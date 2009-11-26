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
#include <linux/mv643xx_i2c.h>
#include <linux/ata_platform.h>
#include <linux/mtd/nand.h>
#include <linux/spi/orion_spi.h>
#include <net/dsa.h>
#include <asm/page.h>
#include <asm/timex.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <mach/kirkwood.h>
#include <mach/bridge-regs.h>
#include <plat/cache-feroceon-l2.h>
#include <plat/ehci-orion.h>
#include <plat/mvsdio.h>
#include <plat/mv_xor.h>
#include <plat/orion_nand.h>
#include <plat/orion_wdt.h>
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

/*
 * Default clock control bits.  Any bit _not_ set in this variable
 * will be cleared from the hardware after platform devices have been
 * registered.  Some reserved bits must be set to 1.
 */
unsigned int kirkwood_clk_ctrl = CGC_DUNIT | CGC_RESERVED;
	

/*****************************************************************************
 * EHCI
 ****************************************************************************/
static struct orion_ehci_data kirkwood_ehci_data = {
	.dram		= &kirkwood_mbus_dram_info,
	.phy_version	= EHCI_PHY_NA,
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
	kirkwood_clk_ctrl |= CGC_USB0;
	platform_device_register(&kirkwood_ehci);
}


/*****************************************************************************
 * GE00
 ****************************************************************************/
struct mv643xx_eth_shared_platform_data kirkwood_ge00_shared_data = {
	.dram		= &kirkwood_mbus_dram_info,
};

static struct resource kirkwood_ge00_shared_resources[] = {
	{
		.name	= "ge00 base",
		.start	= GE00_PHYS_BASE + 0x2000,
		.end	= GE00_PHYS_BASE + 0x3fff,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "ge00 err irq",
		.start	= IRQ_KIRKWOOD_GE00_ERR,
		.end	= IRQ_KIRKWOOD_GE00_ERR,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device kirkwood_ge00_shared = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 0,
	.dev		= {
		.platform_data	= &kirkwood_ge00_shared_data,
	},
	.num_resources	= ARRAY_SIZE(kirkwood_ge00_shared_resources),
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
	.dev		= {
		.coherent_dma_mask	= 0xffffffff,
	},
};

void __init kirkwood_ge00_init(struct mv643xx_eth_platform_data *eth_data)
{
	kirkwood_clk_ctrl |= CGC_GE0;
	eth_data->shared = &kirkwood_ge00_shared;
	kirkwood_ge00.dev.platform_data = eth_data;

	platform_device_register(&kirkwood_ge00_shared);
	platform_device_register(&kirkwood_ge00);
}


/*****************************************************************************
 * GE01
 ****************************************************************************/
struct mv643xx_eth_shared_platform_data kirkwood_ge01_shared_data = {
	.dram		= &kirkwood_mbus_dram_info,
	.shared_smi	= &kirkwood_ge00_shared,
};

static struct resource kirkwood_ge01_shared_resources[] = {
	{
		.name	= "ge01 base",
		.start	= GE01_PHYS_BASE + 0x2000,
		.end	= GE01_PHYS_BASE + 0x3fff,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "ge01 err irq",
		.start	= IRQ_KIRKWOOD_GE01_ERR,
		.end	= IRQ_KIRKWOOD_GE01_ERR,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device kirkwood_ge01_shared = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 1,
	.dev		= {
		.platform_data	= &kirkwood_ge01_shared_data,
	},
	.num_resources	= ARRAY_SIZE(kirkwood_ge01_shared_resources),
	.resource	= kirkwood_ge01_shared_resources,
};

static struct resource kirkwood_ge01_resources[] = {
	{
		.name	= "ge01 irq",
		.start	= IRQ_KIRKWOOD_GE01_SUM,
		.end	= IRQ_KIRKWOOD_GE01_SUM,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device kirkwood_ge01 = {
	.name		= MV643XX_ETH_NAME,
	.id		= 1,
	.num_resources	= 1,
	.resource	= kirkwood_ge01_resources,
	.dev		= {
		.coherent_dma_mask	= 0xffffffff,
	},
};

void __init kirkwood_ge01_init(struct mv643xx_eth_platform_data *eth_data)
{
	kirkwood_clk_ctrl |= CGC_GE1;
	eth_data->shared = &kirkwood_ge01_shared;
	kirkwood_ge01.dev.platform_data = eth_data;

	platform_device_register(&kirkwood_ge01_shared);
	platform_device_register(&kirkwood_ge01);
}


/*****************************************************************************
 * Ethernet switch
 ****************************************************************************/
static struct resource kirkwood_switch_resources[] = {
	{
		.start	= 0,
		.end	= 0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device kirkwood_switch_device = {
	.name		= "dsa",
	.id		= 0,
	.num_resources	= 0,
	.resource	= kirkwood_switch_resources,
};

void __init kirkwood_ge00_switch_init(struct dsa_platform_data *d, int irq)
{
	int i;

	if (irq != NO_IRQ) {
		kirkwood_switch_resources[0].start = irq;
		kirkwood_switch_resources[0].end = irq;
		kirkwood_switch_device.num_resources = 1;
	}

	d->netdev = &kirkwood_ge00.dev;
	for (i = 0; i < d->nr_chips; i++)
		d->chip[i].mii_bus = &kirkwood_ge00_shared.dev;
	kirkwood_switch_device.dev.platform_data = d;

	platform_device_register(&kirkwood_switch_device);
}


/*****************************************************************************
 * NAND flash
 ****************************************************************************/
static struct resource kirkwood_nand_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= KIRKWOOD_NAND_MEM_PHYS_BASE,
	.end		= KIRKWOOD_NAND_MEM_PHYS_BASE +
				KIRKWOOD_NAND_MEM_SIZE - 1,
};

static struct orion_nand_data kirkwood_nand_data = {
	.cle		= 0,
	.ale		= 1,
	.width		= 8,
};

static struct platform_device kirkwood_nand_flash = {
	.name		= "orion_nand",
	.id		= -1,
	.dev		= {
		.platform_data	= &kirkwood_nand_data,
	},
	.resource	= &kirkwood_nand_resource,
	.num_resources	= 1,
};

void __init kirkwood_nand_init(struct mtd_partition *parts, int nr_parts,
			       int chip_delay)
{
	kirkwood_clk_ctrl |= CGC_RUNIT;
	kirkwood_nand_data.parts = parts;
	kirkwood_nand_data.nr_parts = nr_parts;
	kirkwood_nand_data.chip_delay = chip_delay;
	platform_device_register(&kirkwood_nand_flash);
}


/*****************************************************************************
 * SoC RTC
 ****************************************************************************/
static struct resource kirkwood_rtc_resource = {
	.start	= RTC_PHYS_BASE,
	.end	= RTC_PHYS_BASE + SZ_16 - 1,
	.flags	= IORESOURCE_MEM,
};

static void __init kirkwood_rtc_init(void)
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
	kirkwood_clk_ctrl |= CGC_SATA0;
	if (sata_data->n_ports > 1)
		kirkwood_clk_ctrl |= CGC_SATA1;
	sata_data->dram = &kirkwood_mbus_dram_info;
	kirkwood_sata.dev.platform_data = sata_data;
	platform_device_register(&kirkwood_sata);
}


/*****************************************************************************
 * SD/SDIO/MMC
 ****************************************************************************/
static struct resource mvsdio_resources[] = {
	[0] = {
		.start	= SDIO_PHYS_BASE,
		.end	= SDIO_PHYS_BASE + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_KIRKWOOD_SDIO,
		.end	= IRQ_KIRKWOOD_SDIO,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 mvsdio_dmamask = 0xffffffffUL;

static struct platform_device kirkwood_sdio = {
	.name		= "mvsdio",
	.id		= -1,
	.dev		= {
		.dma_mask = &mvsdio_dmamask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(mvsdio_resources),
	.resource	= mvsdio_resources,
};

void __init kirkwood_sdio_init(struct mvsdio_platform_data *mvsdio_data)
{
	u32 dev, rev;

	kirkwood_pcie_id(&dev, &rev);
	if (rev == 0)  /* catch all Kirkwood Z0's */
		mvsdio_data->clock = 100000000;
	else
		mvsdio_data->clock = 200000000;
	mvsdio_data->dram = &kirkwood_mbus_dram_info;
	kirkwood_clk_ctrl |= CGC_SDIO;
	kirkwood_sdio.dev.platform_data = mvsdio_data;
	platform_device_register(&kirkwood_sdio);
}


/*****************************************************************************
 * SPI
 ****************************************************************************/
static struct orion_spi_info kirkwood_spi_plat_data = {
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
	kirkwood_clk_ctrl |= CGC_RUNIT;
	platform_device_register(&kirkwood_spi);
}


/*****************************************************************************
 * I2C
 ****************************************************************************/
static struct mv64xxx_i2c_pdata kirkwood_i2c_pdata = {
	.freq_m		= 8, /* assumes 166 MHz TCLK */
	.freq_n		= 3,
	.timeout	= 1000, /* Default timeout of 1 second */
};

static struct resource kirkwood_i2c_resources[] = {
	{
		.start	= I2C_PHYS_BASE,
		.end	= I2C_PHYS_BASE + 0x1f,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_KIRKWOOD_TWSI,
		.end	= IRQ_KIRKWOOD_TWSI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device kirkwood_i2c = {
	.name		= MV64XXX_I2C_CTLR_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(kirkwood_i2c_resources),
	.resource	= kirkwood_i2c_resources,
	.dev		= {
		.platform_data	= &kirkwood_i2c_pdata,
	},
};

void __init kirkwood_i2c_init(void)
{
	platform_device_register(&kirkwood_i2c);
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
		.uartclk	= 0,
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
		.uartclk	= 0,
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
 * Cryptographic Engines and Security Accelerator (CESA)
 ****************************************************************************/

static struct resource kirkwood_crypto_res[] = {
	{
		.name   = "regs",
		.start  = CRYPTO_PHYS_BASE,
		.end    = CRYPTO_PHYS_BASE + 0xffff,
		.flags  = IORESOURCE_MEM,
	}, {
		.name   = "sram",
		.start  = KIRKWOOD_SRAM_PHYS_BASE,
		.end    = KIRKWOOD_SRAM_PHYS_BASE + KIRKWOOD_SRAM_SIZE - 1,
		.flags  = IORESOURCE_MEM,
	}, {
		.name   = "crypto interrupt",
		.start  = IRQ_KIRKWOOD_CRYPTO,
		.end    = IRQ_KIRKWOOD_CRYPTO,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device kirkwood_crypto_device = {
	.name           = "mv_crypto",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(kirkwood_crypto_res),
	.resource       = kirkwood_crypto_res,
};

void __init kirkwood_crypto_init(void)
{
	kirkwood_clk_ctrl |= CGC_CRYPTO;
	platform_device_register(&kirkwood_crypto_device);
}


/*****************************************************************************
 * XOR
 ****************************************************************************/
static struct mv_xor_platform_shared_data kirkwood_xor_shared_data = {
	.dram		= &kirkwood_mbus_dram_info,
};

static u64 kirkwood_xor_dmamask = DMA_BIT_MASK(32);


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
		.coherent_dma_mask	= DMA_BIT_MASK(64),
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
		.coherent_dma_mask	= DMA_BIT_MASK(64),
		.platform_data		= (void *)&kirkwood_xor01_data,
	},
};

static void __init kirkwood_xor0_init(void)
{
	kirkwood_clk_ctrl |= CGC_XOR0;
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
		.coherent_dma_mask	= DMA_BIT_MASK(64),
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
		.coherent_dma_mask	= DMA_BIT_MASK(64),
		.platform_data		= (void *)&kirkwood_xor11_data,
	},
};

static void __init kirkwood_xor1_init(void)
{
	kirkwood_clk_ctrl |= CGC_XOR1;
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
 * Watchdog
 ****************************************************************************/
static struct orion_wdt_platform_data kirkwood_wdt_data = {
	.tclk		= 0,
};

static struct platform_device kirkwood_wdt_device = {
	.name		= "orion_wdt",
	.id		= -1,
	.dev		= {
		.platform_data	= &kirkwood_wdt_data,
	},
	.num_resources	= 0,
};

static void __init kirkwood_wdt_init(void)
{
	kirkwood_wdt_data.tclk = kirkwood_tclk;
	platform_device_register(&kirkwood_wdt_device);
}


/*****************************************************************************
 * Time handling
 ****************************************************************************/
int kirkwood_tclk;

int __init kirkwood_find_tclk(void)
{
	u32 dev, rev;

	kirkwood_pcie_id(&dev, &rev);
	if (dev == MV88F6281_DEV_ID && (rev == MV88F6281_REV_A0 ||
					rev == MV88F6281_REV_A1))
		return 200000000;

	return 166666667;
}

static void __init kirkwood_timer_init(void)
{
	kirkwood_tclk = kirkwood_find_tclk();
	orion_time_init(IRQ_KIRKWOOD_BRIDGE, kirkwood_tclk);
}

struct sys_timer kirkwood_timer = {
	.init = kirkwood_timer_init,
};


/*****************************************************************************
 * General
 ****************************************************************************/
/*
 * Identify device ID and revision.
 */
static char * __init kirkwood_id(void)
{
	u32 dev, rev;

	kirkwood_pcie_id(&dev, &rev);

	if (dev == MV88F6281_DEV_ID) {
		if (rev == MV88F6281_REV_Z0)
			return "MV88F6281-Z0";
		else if (rev == MV88F6281_REV_A0)
			return "MV88F6281-A0";
		else if (rev == MV88F6281_REV_A1)
			return "MV88F6281-A1";
		else
			return "MV88F6281-Rev-Unsupported";
	} else if (dev == MV88F6192_DEV_ID) {
		if (rev == MV88F6192_REV_Z0)
			return "MV88F6192-Z0";
		else if (rev == MV88F6192_REV_A0)
			return "MV88F6192-A0";
		else
			return "MV88F6192-Rev-Unsupported";
	} else if (dev == MV88F6180_DEV_ID) {
		if (rev == MV88F6180_REV_A0)
			return "MV88F6180-Rev-A0";
		else
			return "MV88F6180-Rev-Unsupported";
	} else {
		return "Device-Unknown";
	}
}

static void __init kirkwood_l2_init(void)
{
#ifdef CONFIG_CACHE_FEROCEON_L2_WRITETHROUGH
	writel(readl(L2_CONFIG_REG) | L2_WRITETHROUGH, L2_CONFIG_REG);
	feroceon_l2_init(1);
#else
	writel(readl(L2_CONFIG_REG) & ~L2_WRITETHROUGH, L2_CONFIG_REG);
	feroceon_l2_init(0);
#endif
}

void __init kirkwood_init(void)
{
	printk(KERN_INFO "Kirkwood: %s, TCLK=%d.\n",
		kirkwood_id(), kirkwood_tclk);
	kirkwood_ge00_shared_data.t_clk = kirkwood_tclk;
	kirkwood_ge01_shared_data.t_clk = kirkwood_tclk;
	kirkwood_spi_plat_data.tclk = kirkwood_tclk;
	kirkwood_uart0_data[0].uartclk = kirkwood_tclk;
	kirkwood_uart1_data[0].uartclk = kirkwood_tclk;

	kirkwood_setup_cpu_mbus();

#ifdef CONFIG_CACHE_FEROCEON_L2
	kirkwood_l2_init();
#endif

	/* internal devices that every board has */
	kirkwood_rtc_init();
	kirkwood_wdt_init();
	kirkwood_xor0_init();
	kirkwood_xor1_init();
	kirkwood_crypto_init();
}

static int __init kirkwood_clock_gate(void)
{
	unsigned int curr = readl(CLOCK_GATING_CTRL);

	printk(KERN_DEBUG "Gating clock of unused units\n");
	printk(KERN_DEBUG "before: 0x%08x\n", curr);

	/* Make sure those units are accessible */
	writel(curr | CGC_SATA0 | CGC_SATA1 | CGC_PEX0, CLOCK_GATING_CTRL);

	/* For SATA: first shutdown the phy */
	if (!(kirkwood_clk_ctrl & CGC_SATA0)) {
		/* Disable PLL and IVREF */
		writel(readl(SATA0_PHY_MODE_2) & ~0xf, SATA0_PHY_MODE_2);
		/* Disable PHY */
		writel(readl(SATA0_IF_CTRL) | 0x200, SATA0_IF_CTRL);
	}
	if (!(kirkwood_clk_ctrl & CGC_SATA1)) {
		/* Disable PLL and IVREF */
		writel(readl(SATA1_PHY_MODE_2) & ~0xf, SATA1_PHY_MODE_2);
		/* Disable PHY */
		writel(readl(SATA1_IF_CTRL) | 0x200, SATA1_IF_CTRL);
	}
	
	/* For PCIe: first shutdown the phy */
	if (!(kirkwood_clk_ctrl & CGC_PEX0)) {
		writel(readl(PCIE_LINK_CTRL) | 0x10, PCIE_LINK_CTRL);
		while (1)
			if (readl(PCIE_STATUS) & 0x1)
				break;
		writel(readl(PCIE_LINK_CTRL) & ~0x10, PCIE_LINK_CTRL);
	}

	/* Now gate clock the required units */
	writel(kirkwood_clk_ctrl, CLOCK_GATING_CTRL);
	printk(KERN_DEBUG " after: 0x%08x\n", readl(CLOCK_GATING_CTRL));

	return 0;
}
late_initcall(kirkwood_clock_gate);
