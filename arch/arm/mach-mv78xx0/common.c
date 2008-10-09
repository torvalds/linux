/*
 * arch/arm/mach-mv78xx0/common.c
 *
 * Core functions for Marvell MV78xx0 SoCs
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
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <mach/mv78xx0.h>
#include <plat/cache-feroceon-l2.h>
#include <plat/ehci-orion.h>
#include <plat/orion_nand.h>
#include <plat/time.h>
#include "common.h"


/*****************************************************************************
 * Common bits
 ****************************************************************************/
int mv78xx0_core_index(void)
{
	u32 extra;

	/*
	 * Read Extra Features register.
	 */
	__asm__("mrc p15, 1, %0, c15, c1, 0" : "=r" (extra));

	return !!(extra & 0x00004000);
}

static int get_hclk(void)
{
	int hclk;

	/*
	 * HCLK tick rate is configured by DEV_D[7:5] pins.
	 */
	switch ((readl(SAMPLE_AT_RESET_LOW) >> 5) & 7) {
	case 0:
		hclk = 166666667;
		break;
	case 1:
		hclk = 200000000;
		break;
	case 2:
		hclk = 266666667;
		break;
	case 3:
		hclk = 333333333;
		break;
	case 4:
		hclk = 400000000;
		break;
	default:
		panic("unknown HCLK PLL setting: %.8x\n",
			readl(SAMPLE_AT_RESET_LOW));
	}

	return hclk;
}

static void get_pclk_l2clk(int hclk, int core_index, int *pclk, int *l2clk)
{
	u32 cfg;

	/*
	 * Core #0 PCLK/L2CLK is configured by bits [13:8], core #1
	 * PCLK/L2CLK by bits [19:14].
	 */
	if (core_index == 0) {
		cfg = (readl(SAMPLE_AT_RESET_LOW) >> 8) & 0x3f;
	} else {
		cfg = (readl(SAMPLE_AT_RESET_LOW) >> 14) & 0x3f;
	}

	/*
	 * Bits [11:8] ([17:14] for core #1) configure the PCLK:HCLK
	 * ratio (1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 5.5, 6).
	 */
	*pclk = ((u64)hclk * (2 + (cfg & 0xf))) >> 1;

	/*
	 * Bits [13:12] ([19:18] for core #1) configure the PCLK:L2CLK
	 * ratio (1, 2, 3).
	 */
	*l2clk = *pclk / (((cfg >> 4) & 3) + 1);
}

static int get_tclk(void)
{
	int tclk;

	/*
	 * TCLK tick rate is configured by DEV_A[2:0] strap pins.
	 */
	switch ((readl(SAMPLE_AT_RESET_HIGH) >> 6) & 7) {
	case 1:
		tclk = 166666667;
		break;
	case 3:
		tclk = 200000000;
		break;
	default:
		panic("unknown TCLK PLL setting: %.8x\n",
			readl(SAMPLE_AT_RESET_HIGH));
	}

	return tclk;
}


/*****************************************************************************
 * I/O Address Mapping
 ****************************************************************************/
static struct map_desc mv78xx0_io_desc[] __initdata = {
	{
		.virtual	= MV78XX0_CORE_REGS_VIRT_BASE,
		.pfn		= 0,
		.length		= MV78XX0_CORE_REGS_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= MV78XX0_PCIE_IO_VIRT_BASE(0),
		.pfn		= __phys_to_pfn(MV78XX0_PCIE_IO_PHYS_BASE(0)),
		.length		= MV78XX0_PCIE_IO_SIZE * 8,
		.type		= MT_DEVICE,
	}, {
		.virtual	= MV78XX0_REGS_VIRT_BASE,
		.pfn		= __phys_to_pfn(MV78XX0_REGS_PHYS_BASE),
		.length		= MV78XX0_REGS_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init mv78xx0_map_io(void)
{
	unsigned long phys;

	/*
	 * Map the right set of per-core registers depending on
	 * which core we are running on.
	 */
	if (mv78xx0_core_index() == 0) {
		phys = MV78XX0_CORE0_REGS_PHYS_BASE;
	} else {
		phys = MV78XX0_CORE1_REGS_PHYS_BASE;
	}
	mv78xx0_io_desc[0].pfn = __phys_to_pfn(phys);

	iotable_init(mv78xx0_io_desc, ARRAY_SIZE(mv78xx0_io_desc));
}


/*****************************************************************************
 * EHCI
 ****************************************************************************/
static struct orion_ehci_data mv78xx0_ehci_data = {
	.dram		= &mv78xx0_mbus_dram_info,
};

static u64 ehci_dmamask = 0xffffffffUL;


/*****************************************************************************
 * EHCI0
 ****************************************************************************/
static struct resource mv78xx0_ehci0_resources[] = {
	{
		.start	= USB0_PHYS_BASE,
		.end	= USB0_PHYS_BASE + 0x0fff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_MV78XX0_USB_0,
		.end	= IRQ_MV78XX0_USB_0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mv78xx0_ehci0 = {
	.name		= "orion-ehci",
	.id		= 0,
	.dev		= {
		.dma_mask		= &ehci_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &mv78xx0_ehci_data,
	},
	.resource	= mv78xx0_ehci0_resources,
	.num_resources	= ARRAY_SIZE(mv78xx0_ehci0_resources),
};

void __init mv78xx0_ehci0_init(void)
{
	platform_device_register(&mv78xx0_ehci0);
}


/*****************************************************************************
 * EHCI1
 ****************************************************************************/
static struct resource mv78xx0_ehci1_resources[] = {
	{
		.start	= USB1_PHYS_BASE,
		.end	= USB1_PHYS_BASE + 0x0fff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_MV78XX0_USB_1,
		.end	= IRQ_MV78XX0_USB_1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mv78xx0_ehci1 = {
	.name		= "orion-ehci",
	.id		= 1,
	.dev		= {
		.dma_mask		= &ehci_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &mv78xx0_ehci_data,
	},
	.resource	= mv78xx0_ehci1_resources,
	.num_resources	= ARRAY_SIZE(mv78xx0_ehci1_resources),
};

void __init mv78xx0_ehci1_init(void)
{
	platform_device_register(&mv78xx0_ehci1);
}


/*****************************************************************************
 * EHCI2
 ****************************************************************************/
static struct resource mv78xx0_ehci2_resources[] = {
	{
		.start	= USB2_PHYS_BASE,
		.end	= USB2_PHYS_BASE + 0x0fff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_MV78XX0_USB_2,
		.end	= IRQ_MV78XX0_USB_2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mv78xx0_ehci2 = {
	.name		= "orion-ehci",
	.id		= 2,
	.dev		= {
		.dma_mask		= &ehci_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &mv78xx0_ehci_data,
	},
	.resource	= mv78xx0_ehci2_resources,
	.num_resources	= ARRAY_SIZE(mv78xx0_ehci2_resources),
};

void __init mv78xx0_ehci2_init(void)
{
	platform_device_register(&mv78xx0_ehci2);
}


/*****************************************************************************
 * GE00
 ****************************************************************************/
struct mv643xx_eth_shared_platform_data mv78xx0_ge00_shared_data = {
	.t_clk		= 0,
	.dram		= &mv78xx0_mbus_dram_info,
};

static struct resource mv78xx0_ge00_shared_resources[] = {
	{
		.name	= "ge00 base",
		.start	= GE00_PHYS_BASE + 0x2000,
		.end	= GE00_PHYS_BASE + 0x3fff,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "ge err irq",
		.start	= IRQ_MV78XX0_GE_ERR,
		.end	= IRQ_MV78XX0_GE_ERR,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mv78xx0_ge00_shared = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 0,
	.dev		= {
		.platform_data	= &mv78xx0_ge00_shared_data,
	},
	.num_resources	= ARRAY_SIZE(mv78xx0_ge00_shared_resources),
	.resource	= mv78xx0_ge00_shared_resources,
};

static struct resource mv78xx0_ge00_resources[] = {
	{
		.name	= "ge00 irq",
		.start	= IRQ_MV78XX0_GE00_SUM,
		.end	= IRQ_MV78XX0_GE00_SUM,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mv78xx0_ge00 = {
	.name		= MV643XX_ETH_NAME,
	.id		= 0,
	.num_resources	= 1,
	.resource	= mv78xx0_ge00_resources,
};

void __init mv78xx0_ge00_init(struct mv643xx_eth_platform_data *eth_data)
{
	eth_data->shared = &mv78xx0_ge00_shared;
	mv78xx0_ge00.dev.platform_data = eth_data;

	platform_device_register(&mv78xx0_ge00_shared);
	platform_device_register(&mv78xx0_ge00);
}


/*****************************************************************************
 * GE01
 ****************************************************************************/
struct mv643xx_eth_shared_platform_data mv78xx0_ge01_shared_data = {
	.t_clk		= 0,
	.dram		= &mv78xx0_mbus_dram_info,
};

static struct resource mv78xx0_ge01_shared_resources[] = {
	{
		.name	= "ge01 base",
		.start	= GE01_PHYS_BASE + 0x2000,
		.end	= GE01_PHYS_BASE + 0x3fff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device mv78xx0_ge01_shared = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 1,
	.dev		= {
		.platform_data	= &mv78xx0_ge01_shared_data,
	},
	.num_resources	= 1,
	.resource	= mv78xx0_ge01_shared_resources,
};

static struct resource mv78xx0_ge01_resources[] = {
	{
		.name	= "ge01 irq",
		.start	= IRQ_MV78XX0_GE01_SUM,
		.end	= IRQ_MV78XX0_GE01_SUM,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mv78xx0_ge01 = {
	.name		= MV643XX_ETH_NAME,
	.id		= 1,
	.num_resources	= 1,
	.resource	= mv78xx0_ge01_resources,
};

void __init mv78xx0_ge01_init(struct mv643xx_eth_platform_data *eth_data)
{
	eth_data->shared = &mv78xx0_ge01_shared;
	eth_data->shared_smi = &mv78xx0_ge00_shared;
	mv78xx0_ge01.dev.platform_data = eth_data;

	platform_device_register(&mv78xx0_ge01_shared);
	platform_device_register(&mv78xx0_ge01);
}


/*****************************************************************************
 * GE10
 ****************************************************************************/
struct mv643xx_eth_shared_platform_data mv78xx0_ge10_shared_data = {
	.t_clk		= 0,
	.dram		= &mv78xx0_mbus_dram_info,
};

static struct resource mv78xx0_ge10_shared_resources[] = {
	{
		.name	= "ge10 base",
		.start	= GE10_PHYS_BASE + 0x2000,
		.end	= GE10_PHYS_BASE + 0x3fff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device mv78xx0_ge10_shared = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 2,
	.dev		= {
		.platform_data	= &mv78xx0_ge10_shared_data,
	},
	.num_resources	= 1,
	.resource	= mv78xx0_ge10_shared_resources,
};

static struct resource mv78xx0_ge10_resources[] = {
	{
		.name	= "ge10 irq",
		.start	= IRQ_MV78XX0_GE10_SUM,
		.end	= IRQ_MV78XX0_GE10_SUM,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mv78xx0_ge10 = {
	.name		= MV643XX_ETH_NAME,
	.id		= 2,
	.num_resources	= 1,
	.resource	= mv78xx0_ge10_resources,
};

void __init mv78xx0_ge10_init(struct mv643xx_eth_platform_data *eth_data)
{
	eth_data->shared = &mv78xx0_ge10_shared;
	eth_data->shared_smi = &mv78xx0_ge00_shared;
	mv78xx0_ge10.dev.platform_data = eth_data;

	platform_device_register(&mv78xx0_ge10_shared);
	platform_device_register(&mv78xx0_ge10);
}


/*****************************************************************************
 * GE11
 ****************************************************************************/
struct mv643xx_eth_shared_platform_data mv78xx0_ge11_shared_data = {
	.t_clk		= 0,
	.dram		= &mv78xx0_mbus_dram_info,
};

static struct resource mv78xx0_ge11_shared_resources[] = {
	{
		.name	= "ge11 base",
		.start	= GE11_PHYS_BASE + 0x2000,
		.end	= GE11_PHYS_BASE + 0x3fff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device mv78xx0_ge11_shared = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 3,
	.dev		= {
		.platform_data	= &mv78xx0_ge11_shared_data,
	},
	.num_resources	= 1,
	.resource	= mv78xx0_ge11_shared_resources,
};

static struct resource mv78xx0_ge11_resources[] = {
	{
		.name	= "ge11 irq",
		.start	= IRQ_MV78XX0_GE11_SUM,
		.end	= IRQ_MV78XX0_GE11_SUM,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mv78xx0_ge11 = {
	.name		= MV643XX_ETH_NAME,
	.id		= 3,
	.num_resources	= 1,
	.resource	= mv78xx0_ge11_resources,
};

void __init mv78xx0_ge11_init(struct mv643xx_eth_platform_data *eth_data)
{
	eth_data->shared = &mv78xx0_ge11_shared;
	eth_data->shared_smi = &mv78xx0_ge00_shared;
	mv78xx0_ge11.dev.platform_data = eth_data;

	platform_device_register(&mv78xx0_ge11_shared);
	platform_device_register(&mv78xx0_ge11);
}


/*****************************************************************************
 * SATA
 ****************************************************************************/
static struct resource mv78xx0_sata_resources[] = {
	{
		.name	= "sata base",
		.start	= SATA_PHYS_BASE,
		.end	= SATA_PHYS_BASE + 0x5000 - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "sata irq",
		.start	= IRQ_MV78XX0_SATA,
		.end	= IRQ_MV78XX0_SATA,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mv78xx0_sata = {
	.name		= "sata_mv",
	.id		= 0,
	.dev		= {
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(mv78xx0_sata_resources),
	.resource	= mv78xx0_sata_resources,
};

void __init mv78xx0_sata_init(struct mv_sata_platform_data *sata_data)
{
	sata_data->dram = &mv78xx0_mbus_dram_info;
	mv78xx0_sata.dev.platform_data = sata_data;
	platform_device_register(&mv78xx0_sata);
}


/*****************************************************************************
 * UART0
 ****************************************************************************/
static struct plat_serial8250_port mv78xx0_uart0_data[] = {
	{
		.mapbase	= UART0_PHYS_BASE,
		.membase	= (char *)UART0_VIRT_BASE,
		.irq		= IRQ_MV78XX0_UART_0,
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 0,
	}, {
	},
};

static struct resource mv78xx0_uart0_resources[] = {
	{
		.start		= UART0_PHYS_BASE,
		.end		= UART0_PHYS_BASE + 0xff,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IRQ_MV78XX0_UART_0,
		.end		= IRQ_MV78XX0_UART_0,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device mv78xx0_uart0 = {
	.name			= "serial8250",
	.id			= 0,
	.dev			= {
		.platform_data	= mv78xx0_uart0_data,
	},
	.resource		= mv78xx0_uart0_resources,
	.num_resources		= ARRAY_SIZE(mv78xx0_uart0_resources),
};

void __init mv78xx0_uart0_init(void)
{
	platform_device_register(&mv78xx0_uart0);
}


/*****************************************************************************
 * UART1
 ****************************************************************************/
static struct plat_serial8250_port mv78xx0_uart1_data[] = {
	{
		.mapbase	= UART1_PHYS_BASE,
		.membase	= (char *)UART1_VIRT_BASE,
		.irq		= IRQ_MV78XX0_UART_1,
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 0,
	}, {
	},
};

static struct resource mv78xx0_uart1_resources[] = {
	{
		.start		= UART1_PHYS_BASE,
		.end		= UART1_PHYS_BASE + 0xff,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IRQ_MV78XX0_UART_1,
		.end		= IRQ_MV78XX0_UART_1,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device mv78xx0_uart1 = {
	.name			= "serial8250",
	.id			= 1,
	.dev			= {
		.platform_data	= mv78xx0_uart1_data,
	},
	.resource		= mv78xx0_uart1_resources,
	.num_resources		= ARRAY_SIZE(mv78xx0_uart1_resources),
};

void __init mv78xx0_uart1_init(void)
{
	platform_device_register(&mv78xx0_uart1);
}


/*****************************************************************************
 * UART2
 ****************************************************************************/
static struct plat_serial8250_port mv78xx0_uart2_data[] = {
	{
		.mapbase	= UART2_PHYS_BASE,
		.membase	= (char *)UART2_VIRT_BASE,
		.irq		= IRQ_MV78XX0_UART_2,
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 0,
	}, {
	},
};

static struct resource mv78xx0_uart2_resources[] = {
	{
		.start		= UART2_PHYS_BASE,
		.end		= UART2_PHYS_BASE + 0xff,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IRQ_MV78XX0_UART_2,
		.end		= IRQ_MV78XX0_UART_2,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device mv78xx0_uart2 = {
	.name			= "serial8250",
	.id			= 2,
	.dev			= {
		.platform_data	= mv78xx0_uart2_data,
	},
	.resource		= mv78xx0_uart2_resources,
	.num_resources		= ARRAY_SIZE(mv78xx0_uart2_resources),
};

void __init mv78xx0_uart2_init(void)
{
	platform_device_register(&mv78xx0_uart2);
}


/*****************************************************************************
 * UART3
 ****************************************************************************/
static struct plat_serial8250_port mv78xx0_uart3_data[] = {
	{
		.mapbase	= UART3_PHYS_BASE,
		.membase	= (char *)UART3_VIRT_BASE,
		.irq		= IRQ_MV78XX0_UART_3,
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 0,
	}, {
	},
};

static struct resource mv78xx0_uart3_resources[] = {
	{
		.start		= UART3_PHYS_BASE,
		.end		= UART3_PHYS_BASE + 0xff,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IRQ_MV78XX0_UART_3,
		.end		= IRQ_MV78XX0_UART_3,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device mv78xx0_uart3 = {
	.name			= "serial8250",
	.id			= 3,
	.dev			= {
		.platform_data	= mv78xx0_uart3_data,
	},
	.resource		= mv78xx0_uart3_resources,
	.num_resources		= ARRAY_SIZE(mv78xx0_uart3_resources),
};

void __init mv78xx0_uart3_init(void)
{
	platform_device_register(&mv78xx0_uart3);
}


/*****************************************************************************
 * Time handling
 ****************************************************************************/
static void mv78xx0_timer_init(void)
{
	orion_time_init(IRQ_MV78XX0_TIMER_1, get_tclk());
}

struct sys_timer mv78xx0_timer = {
	.init = mv78xx0_timer_init,
};


/*****************************************************************************
 * General
 ****************************************************************************/
static int __init is_l2_writethrough(void)
{
	return !!(readl(CPU_CONTROL) & L2_WRITETHROUGH);
}

void __init mv78xx0_init(void)
{
	int core_index;
	int hclk;
	int pclk;
	int l2clk;
	int tclk;

	core_index = mv78xx0_core_index();
	hclk = get_hclk();
	get_pclk_l2clk(hclk, core_index, &pclk, &l2clk);
	tclk = get_tclk();

	printk(KERN_INFO "MV78xx0 core #%d, ", core_index);
	printk("PCLK = %dMHz, ", (pclk + 499999) / 1000000);
	printk("L2 = %dMHz, ", (l2clk + 499999) / 1000000);
	printk("HCLK = %dMHz, ", (hclk + 499999) / 1000000);
	printk("TCLK = %dMHz\n", (tclk + 499999) / 1000000);

	mv78xx0_setup_cpu_mbus();

#ifdef CONFIG_CACHE_FEROCEON_L2
	feroceon_l2_init(is_l2_writethrough());
#endif

	mv78xx0_ge00_shared_data.t_clk = tclk;
	mv78xx0_ge01_shared_data.t_clk = tclk;
	mv78xx0_ge10_shared_data.t_clk = tclk;
	mv78xx0_ge11_shared_data.t_clk = tclk;
	mv78xx0_uart0_data[0].uartclk = tclk;
	mv78xx0_uart1_data[0].uartclk = tclk;
	mv78xx0_uart2_data[0].uartclk = tclk;
	mv78xx0_uart3_data[0].uartclk = tclk;
}
