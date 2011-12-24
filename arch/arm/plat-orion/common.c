/*
 * arch/arm/plat-orion/common.c
 *
 * Marvell Orion SoC common setup code used by multiple mach-/common.c
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/serial_8250.h>
#include <linux/ata_platform.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/mv643xx_eth.h>
#include <linux/mv643xx_i2c.h>
#include <net/dsa.h>
#include <plat/orion_wdt.h>
#include <plat/mv_xor.h>
#include <plat/ehci-orion.h>
#include <mach/bridge-regs.h>

/* Create a clkdev entry for a given device/clk */
void __init orion_clkdev_add(const char *con_id, const char *dev_id,
			     struct clk *clk)
{
	struct clk_lookup *cl;

	cl = clkdev_alloc(clk, con_id, dev_id);
	if (cl)
		clkdev_add(cl);
}

/* Create clkdev entries for all orion platforms except kirkwood.
   Kirkwood has gated clocks for some of its peripherals, so creates
   its own clkdev entries. For all the other orion devices, create
   clkdev entries to the tclk. */
void __init orion_clkdev_init(struct clk *tclk)
{
	orion_clkdev_add(NULL, "orion_spi.0", tclk);
	orion_clkdev_add(NULL, "orion_spi.1", tclk);
	orion_clkdev_add(NULL, MV643XX_ETH_NAME ".0", tclk);
	orion_clkdev_add(NULL, MV643XX_ETH_NAME ".1", tclk);
	orion_clkdev_add(NULL, MV643XX_ETH_NAME ".2", tclk);
	orion_clkdev_add(NULL, MV643XX_ETH_NAME ".3", tclk);
}

/* Fill in the resources structure and link it into the platform
   device structure. There is always a memory region, and nearly
   always an interrupt.*/
static void fill_resources(struct platform_device *device,
			   struct resource *resources,
			   resource_size_t mapbase,
			   resource_size_t size,
			   unsigned int irq)
{
	device->resource = resources;
	device->num_resources = 1;
	resources[0].flags = IORESOURCE_MEM;
	resources[0].start = mapbase;
	resources[0].end = mapbase + size;

	if (irq != NO_IRQ) {
		device->num_resources++;
		resources[1].flags = IORESOURCE_IRQ;
		resources[1].start = irq;
		resources[1].end = irq;
	}
}

/*****************************************************************************
 * UART
 ****************************************************************************/
static void __init uart_complete(
	struct platform_device *orion_uart,
	struct plat_serial8250_port *data,
	struct resource *resources,
	unsigned int membase,
	resource_size_t mapbase,
	unsigned int irq,
	unsigned int uartclk)
{
	data->mapbase = mapbase;
	data->membase = (void __iomem *)membase;
	data->irq = irq;
	data->uartclk = uartclk;
	orion_uart->dev.platform_data = data;

	fill_resources(orion_uart, resources, mapbase, 0xff, irq);
	platform_device_register(orion_uart);
}

/*****************************************************************************
 * UART0
 ****************************************************************************/
static struct plat_serial8250_port orion_uart0_data[] = {
	{
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	}, {
	},
};

static struct resource orion_uart0_resources[2];

static struct platform_device orion_uart0 = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
};

void __init orion_uart0_init(unsigned int membase,
			     resource_size_t mapbase,
			     unsigned int irq,
			     unsigned int uartclk)
{
	uart_complete(&orion_uart0, orion_uart0_data, orion_uart0_resources,
		      membase, mapbase, irq, uartclk);
}

/*****************************************************************************
 * UART1
 ****************************************************************************/
static struct plat_serial8250_port orion_uart1_data[] = {
	{
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	}, {
	},
};

static struct resource orion_uart1_resources[2];

static struct platform_device orion_uart1 = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM1,
};

void __init orion_uart1_init(unsigned int membase,
			     resource_size_t mapbase,
			     unsigned int irq,
			     unsigned int uartclk)
{
	uart_complete(&orion_uart1, orion_uart1_data, orion_uart1_resources,
		      membase, mapbase, irq, uartclk);
}

/*****************************************************************************
 * UART2
 ****************************************************************************/
static struct plat_serial8250_port orion_uart2_data[] = {
	{
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	}, {
	},
};

static struct resource orion_uart2_resources[2];

static struct platform_device orion_uart2 = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM2,
};

void __init orion_uart2_init(unsigned int membase,
			     resource_size_t mapbase,
			     unsigned int irq,
			     unsigned int uartclk)
{
	uart_complete(&orion_uart2, orion_uart2_data, orion_uart2_resources,
		      membase, mapbase, irq, uartclk);
}

/*****************************************************************************
 * UART3
 ****************************************************************************/
static struct plat_serial8250_port orion_uart3_data[] = {
	{
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	}, {
	},
};

static struct resource orion_uart3_resources[2];

static struct platform_device orion_uart3 = {
	.name			= "serial8250",
	.id			= 3,
};

void __init orion_uart3_init(unsigned int membase,
			     resource_size_t mapbase,
			     unsigned int irq,
			     unsigned int uartclk)
{
	uart_complete(&orion_uart3, orion_uart3_data, orion_uart3_resources,
		      membase, mapbase, irq, uartclk);
}

/*****************************************************************************
 * SoC RTC
 ****************************************************************************/
static struct resource orion_rtc_resource[2];

void __init orion_rtc_init(unsigned long mapbase,
			   unsigned long irq)
{
	orion_rtc_resource[0].start = mapbase;
	orion_rtc_resource[0].end = mapbase + SZ_32 - 1;
	orion_rtc_resource[0].flags = IORESOURCE_MEM;
	orion_rtc_resource[1].start = irq;
	orion_rtc_resource[1].end = irq;
	orion_rtc_resource[1].flags = IORESOURCE_IRQ;

	platform_device_register_simple("rtc-mv", -1, orion_rtc_resource, 2);
}

/*****************************************************************************
 * GE
 ****************************************************************************/
static __init void ge_complete(
	struct mv643xx_eth_shared_platform_data *orion_ge_shared_data,
	struct resource *orion_ge_resource, unsigned long irq,
	struct platform_device *orion_ge_shared,
	struct mv643xx_eth_platform_data *eth_data,
	struct platform_device *orion_ge)
{
	orion_ge_resource->start = irq;
	orion_ge_resource->end = irq;
	eth_data->shared = orion_ge_shared;
	orion_ge->dev.platform_data = eth_data;

	platform_device_register(orion_ge_shared);
	platform_device_register(orion_ge);
}

/*****************************************************************************
 * GE00
 ****************************************************************************/
struct mv643xx_eth_shared_platform_data orion_ge00_shared_data;

static struct resource orion_ge00_shared_resources[] = {
	{
		.name	= "ge00 base",
	}, {
		.name	= "ge00 err irq",
	},
};

static struct platform_device orion_ge00_shared = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 0,
	.dev		= {
		.platform_data	= &orion_ge00_shared_data,
	},
};

static struct resource orion_ge00_resources[] = {
	{
		.name	= "ge00 irq",
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device orion_ge00 = {
	.name		= MV643XX_ETH_NAME,
	.id		= 0,
	.num_resources	= 1,
	.resource	= orion_ge00_resources,
	.dev		= {
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init orion_ge00_init(struct mv643xx_eth_platform_data *eth_data,
			    unsigned long mapbase,
			    unsigned long irq,
			    unsigned long irq_err)
{
	fill_resources(&orion_ge00_shared, orion_ge00_shared_resources,
		       mapbase + 0x2000, SZ_16K - 1, irq_err);
	ge_complete(&orion_ge00_shared_data,
		    orion_ge00_resources, irq, &orion_ge00_shared,
		    eth_data, &orion_ge00);
}

/*****************************************************************************
 * GE01
 ****************************************************************************/
struct mv643xx_eth_shared_platform_data orion_ge01_shared_data = {
	.shared_smi	= &orion_ge00_shared,
};

static struct resource orion_ge01_shared_resources[] = {
	{
		.name	= "ge01 base",
	}, {
		.name	= "ge01 err irq",
	},
};

static struct platform_device orion_ge01_shared = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 1,
	.dev		= {
		.platform_data	= &orion_ge01_shared_data,
	},
};

static struct resource orion_ge01_resources[] = {
	{
		.name	= "ge01 irq",
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device orion_ge01 = {
	.name		= MV643XX_ETH_NAME,
	.id		= 1,
	.num_resources	= 1,
	.resource	= orion_ge01_resources,
	.dev		= {
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init orion_ge01_init(struct mv643xx_eth_platform_data *eth_data,
			    unsigned long mapbase,
			    unsigned long irq,
			    unsigned long irq_err)
{
	fill_resources(&orion_ge01_shared, orion_ge01_shared_resources,
		       mapbase + 0x2000, SZ_16K - 1, irq_err);
	ge_complete(&orion_ge01_shared_data,
		    orion_ge01_resources, irq, &orion_ge01_shared,
		    eth_data, &orion_ge01);
}

/*****************************************************************************
 * GE10
 ****************************************************************************/
struct mv643xx_eth_shared_platform_data orion_ge10_shared_data = {
	.shared_smi	= &orion_ge00_shared,
};

static struct resource orion_ge10_shared_resources[] = {
	{
		.name	= "ge10 base",
	}, {
		.name	= "ge10 err irq",
	},
};

static struct platform_device orion_ge10_shared = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 1,
	.dev		= {
		.platform_data	= &orion_ge10_shared_data,
	},
};

static struct resource orion_ge10_resources[] = {
	{
		.name	= "ge10 irq",
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device orion_ge10 = {
	.name		= MV643XX_ETH_NAME,
	.id		= 1,
	.num_resources	= 2,
	.resource	= orion_ge10_resources,
	.dev		= {
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init orion_ge10_init(struct mv643xx_eth_platform_data *eth_data,
			    unsigned long mapbase,
			    unsigned long irq,
			    unsigned long irq_err)
{
	fill_resources(&orion_ge10_shared, orion_ge10_shared_resources,
		       mapbase + 0x2000, SZ_16K - 1, irq_err);
	ge_complete(&orion_ge10_shared_data,
		    orion_ge10_resources, irq, &orion_ge10_shared,
		    eth_data, &orion_ge10);
}

/*****************************************************************************
 * GE11
 ****************************************************************************/
struct mv643xx_eth_shared_platform_data orion_ge11_shared_data = {
	.shared_smi	= &orion_ge00_shared,
};

static struct resource orion_ge11_shared_resources[] = {
	{
		.name	= "ge11 base",
	}, {
		.name	= "ge11 err irq",
	},
};

static struct platform_device orion_ge11_shared = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 1,
	.dev		= {
		.platform_data	= &orion_ge11_shared_data,
	},
};

static struct resource orion_ge11_resources[] = {
	{
		.name	= "ge11 irq",
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device orion_ge11 = {
	.name		= MV643XX_ETH_NAME,
	.id		= 1,
	.num_resources	= 2,
	.resource	= orion_ge11_resources,
	.dev		= {
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init orion_ge11_init(struct mv643xx_eth_platform_data *eth_data,
			    unsigned long mapbase,
			    unsigned long irq,
			    unsigned long irq_err)
{
	fill_resources(&orion_ge11_shared, orion_ge11_shared_resources,
		       mapbase + 0x2000, SZ_16K - 1, irq_err);
	ge_complete(&orion_ge11_shared_data,
		    orion_ge11_resources, irq, &orion_ge11_shared,
		    eth_data, &orion_ge11);
}

/*****************************************************************************
 * Ethernet switch
 ****************************************************************************/
static struct resource orion_switch_resources[] = {
	{
		.start	= 0,
		.end	= 0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device orion_switch_device = {
	.name		= "dsa",
	.id		= 0,
	.num_resources	= 0,
	.resource	= orion_switch_resources,
};

void __init orion_ge00_switch_init(struct dsa_platform_data *d, int irq)
{
	int i;

	if (irq != NO_IRQ) {
		orion_switch_resources[0].start = irq;
		orion_switch_resources[0].end = irq;
		orion_switch_device.num_resources = 1;
	}

	d->netdev = &orion_ge00.dev;
	for (i = 0; i < d->nr_chips; i++)
		d->chip[i].mii_bus = &orion_ge00_shared.dev;
	orion_switch_device.dev.platform_data = d;

	platform_device_register(&orion_switch_device);
}

/*****************************************************************************
 * I2C
 ****************************************************************************/
static struct mv64xxx_i2c_pdata orion_i2c_pdata = {
	.freq_n		= 3,
	.timeout	= 1000, /* Default timeout of 1 second */
};

static struct resource orion_i2c_resources[2];

static struct platform_device orion_i2c = {
	.name		= MV64XXX_I2C_CTLR_NAME,
	.id		= 0,
	.dev		= {
		.platform_data	= &orion_i2c_pdata,
	},
};

static struct mv64xxx_i2c_pdata orion_i2c_1_pdata = {
	.freq_n		= 3,
	.timeout	= 1000, /* Default timeout of 1 second */
};

static struct resource orion_i2c_1_resources[2];

static struct platform_device orion_i2c_1 = {
	.name		= MV64XXX_I2C_CTLR_NAME,
	.id		= 1,
	.dev		= {
		.platform_data	= &orion_i2c_1_pdata,
	},
};

void __init orion_i2c_init(unsigned long mapbase,
			   unsigned long irq,
			   unsigned long freq_m)
{
	orion_i2c_pdata.freq_m = freq_m;
	fill_resources(&orion_i2c, orion_i2c_resources, mapbase,
		       SZ_32 - 1, irq);
	platform_device_register(&orion_i2c);
}

void __init orion_i2c_1_init(unsigned long mapbase,
			     unsigned long irq,
			     unsigned long freq_m)
{
	orion_i2c_1_pdata.freq_m = freq_m;
	fill_resources(&orion_i2c_1, orion_i2c_1_resources, mapbase,
		       SZ_32 - 1, irq);
	platform_device_register(&orion_i2c_1);
}

/*****************************************************************************
 * SPI
 ****************************************************************************/
static struct resource orion_spi_resources;

static struct platform_device orion_spi = {
	.name		= "orion_spi",
	.id		= 0,
};

static struct resource orion_spi_1_resources;

static struct platform_device orion_spi_1 = {
	.name		= "orion_spi",
	.id		= 1,
};

/* Note: The SPI silicon core does have interrupts. However the
 * current Linux software driver does not use interrupts. */

void __init orion_spi_init(unsigned long mapbase)
{
	fill_resources(&orion_spi, &orion_spi_resources,
		       mapbase, SZ_512 - 1, NO_IRQ);
	platform_device_register(&orion_spi);
}

void __init orion_spi_1_init(unsigned long mapbase)
{
	fill_resources(&orion_spi_1, &orion_spi_1_resources,
		       mapbase, SZ_512 - 1, NO_IRQ);
	platform_device_register(&orion_spi_1);
}

/*****************************************************************************
 * Watchdog
 ****************************************************************************/
static struct orion_wdt_platform_data orion_wdt_data;

static struct resource orion_wdt_resource =
		DEFINE_RES_MEM(TIMER_VIRT_BASE, 0x28);

static struct platform_device orion_wdt_device = {
	.name		= "orion_wdt",
	.id		= -1,
	.dev		= {
		.platform_data	= &orion_wdt_data,
	},
	.resource	= &orion_wdt_resource,
	.num_resources	= 1,
};

void __init orion_wdt_init(unsigned long tclk)
{
	orion_wdt_data.tclk = tclk;
	platform_device_register(&orion_wdt_device);
}

/*****************************************************************************
 * XOR
 ****************************************************************************/
static u64 orion_xor_dmamask = DMA_BIT_MASK(32);

void __init orion_xor_init_channels(
	struct mv_xor_platform_data *orion_xor0_data,
	struct platform_device *orion_xor0_channel,
	struct mv_xor_platform_data *orion_xor1_data,
	struct platform_device *orion_xor1_channel)
{
	/*
	 * two engines can't do memset simultaneously, this limitation
	 * satisfied by removing memset support from one of the engines.
	 */
	dma_cap_set(DMA_MEMCPY, orion_xor0_data->cap_mask);
	dma_cap_set(DMA_XOR, orion_xor0_data->cap_mask);
	platform_device_register(orion_xor0_channel);

	dma_cap_set(DMA_MEMCPY, orion_xor1_data->cap_mask);
	dma_cap_set(DMA_MEMSET, orion_xor1_data->cap_mask);
	dma_cap_set(DMA_XOR, orion_xor1_data->cap_mask);
	platform_device_register(orion_xor1_channel);
}

/*****************************************************************************
 * XOR0
 ****************************************************************************/
static struct resource orion_xor0_shared_resources[] = {
	{
		.name	= "xor 0 low",
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "xor 0 high",
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device orion_xor0_shared = {
	.name		= MV_XOR_SHARED_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(orion_xor0_shared_resources),
	.resource	= orion_xor0_shared_resources,
};

static struct resource orion_xor00_resources[] = {
	[0] = {
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv_xor_platform_data orion_xor00_data = {
	.shared		= &orion_xor0_shared,
	.hw_id		= 0,
	.pool_size	= PAGE_SIZE,
};

static struct platform_device orion_xor00_channel = {
	.name		= MV_XOR_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(orion_xor00_resources),
	.resource	= orion_xor00_resources,
	.dev		= {
		.dma_mask		= &orion_xor_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(64),
		.platform_data		= &orion_xor00_data,
	},
};

static struct resource orion_xor01_resources[] = {
	[0] = {
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv_xor_platform_data orion_xor01_data = {
	.shared		= &orion_xor0_shared,
	.hw_id		= 1,
	.pool_size	= PAGE_SIZE,
};

static struct platform_device orion_xor01_channel = {
	.name		= MV_XOR_NAME,
	.id		= 1,
	.num_resources	= ARRAY_SIZE(orion_xor01_resources),
	.resource	= orion_xor01_resources,
	.dev		= {
		.dma_mask		= &orion_xor_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(64),
		.platform_data		= &orion_xor01_data,
	},
};

void __init orion_xor0_init(unsigned long mapbase_low,
			    unsigned long mapbase_high,
			    unsigned long irq_0,
			    unsigned long irq_1)
{
	orion_xor0_shared_resources[0].start = mapbase_low;
	orion_xor0_shared_resources[0].end = mapbase_low + 0xff;
	orion_xor0_shared_resources[1].start = mapbase_high;
	orion_xor0_shared_resources[1].end = mapbase_high + 0xff;

	orion_xor00_resources[0].start = irq_0;
	orion_xor00_resources[0].end = irq_0;
	orion_xor01_resources[0].start = irq_1;
	orion_xor01_resources[0].end = irq_1;

	platform_device_register(&orion_xor0_shared);

	orion_xor_init_channels(&orion_xor00_data, &orion_xor00_channel,
				&orion_xor01_data, &orion_xor01_channel);
}

/*****************************************************************************
 * XOR1
 ****************************************************************************/
static struct resource orion_xor1_shared_resources[] = {
	{
		.name	= "xor 1 low",
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "xor 1 high",
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device orion_xor1_shared = {
	.name		= MV_XOR_SHARED_NAME,
	.id		= 1,
	.num_resources	= ARRAY_SIZE(orion_xor1_shared_resources),
	.resource	= orion_xor1_shared_resources,
};

static struct resource orion_xor10_resources[] = {
	[0] = {
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv_xor_platform_data orion_xor10_data = {
	.shared		= &orion_xor1_shared,
	.hw_id		= 0,
	.pool_size	= PAGE_SIZE,
};

static struct platform_device orion_xor10_channel = {
	.name		= MV_XOR_NAME,
	.id		= 2,
	.num_resources	= ARRAY_SIZE(orion_xor10_resources),
	.resource	= orion_xor10_resources,
	.dev		= {
		.dma_mask		= &orion_xor_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(64),
		.platform_data		= &orion_xor10_data,
	},
};

static struct resource orion_xor11_resources[] = {
	[0] = {
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv_xor_platform_data orion_xor11_data = {
	.shared		= &orion_xor1_shared,
	.hw_id		= 1,
	.pool_size	= PAGE_SIZE,
};

static struct platform_device orion_xor11_channel = {
	.name		= MV_XOR_NAME,
	.id		= 3,
	.num_resources	= ARRAY_SIZE(orion_xor11_resources),
	.resource	= orion_xor11_resources,
	.dev		= {
		.dma_mask		= &orion_xor_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(64),
		.platform_data		= &orion_xor11_data,
	},
};

void __init orion_xor1_init(unsigned long mapbase_low,
			    unsigned long mapbase_high,
			    unsigned long irq_0,
			    unsigned long irq_1)
{
	orion_xor1_shared_resources[0].start = mapbase_low;
	orion_xor1_shared_resources[0].end = mapbase_low + 0xff;
	orion_xor1_shared_resources[1].start = mapbase_high;
	orion_xor1_shared_resources[1].end = mapbase_high + 0xff;

	orion_xor10_resources[0].start = irq_0;
	orion_xor10_resources[0].end = irq_0;
	orion_xor11_resources[0].start = irq_1;
	orion_xor11_resources[0].end = irq_1;

	platform_device_register(&orion_xor1_shared);

	orion_xor_init_channels(&orion_xor10_data, &orion_xor10_channel,
				&orion_xor11_data, &orion_xor11_channel);
}

/*****************************************************************************
 * EHCI
 ****************************************************************************/
static struct orion_ehci_data orion_ehci_data;
static u64 ehci_dmamask = DMA_BIT_MASK(32);


/*****************************************************************************
 * EHCI0
 ****************************************************************************/
static struct resource orion_ehci_resources[2];

static struct platform_device orion_ehci = {
	.name		= "orion-ehci",
	.id		= 0,
	.dev		= {
		.dma_mask		= &ehci_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &orion_ehci_data,
	},
};

void __init orion_ehci_init(unsigned long mapbase,
			    unsigned long irq,
			    enum orion_ehci_phy_ver phy_version)
{
	orion_ehci_data.phy_version = phy_version;
	fill_resources(&orion_ehci, orion_ehci_resources, mapbase, SZ_4K - 1,
		       irq);

	platform_device_register(&orion_ehci);
}

/*****************************************************************************
 * EHCI1
 ****************************************************************************/
static struct resource orion_ehci_1_resources[2];

static struct platform_device orion_ehci_1 = {
	.name		= "orion-ehci",
	.id		= 1,
	.dev		= {
		.dma_mask		= &ehci_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &orion_ehci_data,
	},
};

void __init orion_ehci_1_init(unsigned long mapbase,
			      unsigned long irq)
{
	fill_resources(&orion_ehci_1, orion_ehci_1_resources,
		       mapbase, SZ_4K - 1, irq);

	platform_device_register(&orion_ehci_1);
}

/*****************************************************************************
 * EHCI2
 ****************************************************************************/
static struct resource orion_ehci_2_resources[2];

static struct platform_device orion_ehci_2 = {
	.name		= "orion-ehci",
	.id		= 2,
	.dev		= {
		.dma_mask		= &ehci_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &orion_ehci_data,
	},
};

void __init orion_ehci_2_init(unsigned long mapbase,
			      unsigned long irq)
{
	fill_resources(&orion_ehci_2, orion_ehci_2_resources,
		       mapbase, SZ_4K - 1, irq);

	platform_device_register(&orion_ehci_2);
}

/*****************************************************************************
 * SATA
 ****************************************************************************/
static struct resource orion_sata_resources[2] = {
	{
		.name	= "sata base",
	}, {
		.name	= "sata irq",
	},
};

static struct platform_device orion_sata = {
	.name		= "sata_mv",
	.id		= 0,
	.dev		= {
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init orion_sata_init(struct mv_sata_platform_data *sata_data,
			    unsigned long mapbase,
			    unsigned long irq)
{
	orion_sata.dev.platform_data = sata_data;
	fill_resources(&orion_sata, orion_sata_resources,
		       mapbase, 0x5000 - 1, irq);

	platform_device_register(&orion_sata);
}

/*****************************************************************************
 * Cryptographic Engines and Security Accelerator (CESA)
 ****************************************************************************/
static struct resource orion_crypto_resources[] = {
	{
		.name   = "regs",
	}, {
		.name   = "crypto interrupt",
	}, {
		.name   = "sram",
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device orion_crypto = {
	.name           = "mv_crypto",
	.id             = -1,
};

void __init orion_crypto_init(unsigned long mapbase,
			      unsigned long srambase,
			      unsigned long sram_size,
			      unsigned long irq)
{
	fill_resources(&orion_crypto, orion_crypto_resources,
		       mapbase, 0xffff, irq);
	orion_crypto.num_resources = 3;
	orion_crypto_resources[2].start = srambase;
	orion_crypto_resources[2].end = srambase + sram_size - 1;

	platform_device_register(&orion_crypto);
}
