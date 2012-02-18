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
#include <linux/ata_platform.h>
#include <linux/mtd/nand.h>
#include <linux/dma-mapping.h>
#include <linux/clk-provider.h>
#include <linux/spinlock.h>
#include <net/dsa.h>
#include <asm/page.h>
#include <asm/timex.h>
#include <asm/kexec.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <mach/kirkwood.h>
#include <mach/bridge-regs.h>
#include <plat/audio.h>
#include <plat/cache-feroceon-l2.h>
#include <plat/mvsdio.h>
#include <plat/orion_nand.h>
#include <plat/ehci-orion.h>
#include <plat/common.h>
#include <plat/time.h>
#include <plat/addr-map.h>
#include <plat/mv_xor.h>
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
		.virtual	= KIRKWOOD_PCIE1_IO_VIRT_BASE,
		.pfn		= __phys_to_pfn(KIRKWOOD_PCIE1_IO_PHYS_BASE),
		.length		= KIRKWOOD_PCIE1_IO_SIZE,
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
 * CLK tree
 ****************************************************************************/
static DEFINE_SPINLOCK(gating_lock);
static struct clk *tclk;

static struct clk __init *kirkwood_register_gate(const char *name, u8 bit_idx)
{
	return clk_register_gate(NULL, name, "tclk", CLK_IGNORE_UNUSED,
				 (void __iomem *)CLOCK_GATING_CTRL,
				 bit_idx, 0, &gating_lock);
}

void __init kirkwood_clk_init(void)
{
	struct clk *runit, *ge0, *ge1, *sata0, *sata1, *usb0;

	tclk = clk_register_fixed_rate(NULL, "tclk", NULL,
				       CLK_IS_ROOT, kirkwood_tclk);

	runit = kirkwood_register_gate("runit",  CGC_BIT_RUNIT);
	ge0 = kirkwood_register_gate("ge0",    CGC_BIT_GE0);
	ge1 = kirkwood_register_gate("ge1",    CGC_BIT_GE1);
	sata0 = kirkwood_register_gate("sata0",  CGC_BIT_SATA0);
	sata1 = kirkwood_register_gate("sata1",  CGC_BIT_SATA1);
	kirkwood_register_gate("usb0",   CGC_BIT_USB0);
	kirkwood_register_gate("sdio",   CGC_BIT_SDIO);
	kirkwood_register_gate("crypto", CGC_BIT_CRYPTO);
	kirkwood_register_gate("xor0",   CGC_BIT_XOR0);
	kirkwood_register_gate("xor1",   CGC_BIT_XOR1);
	kirkwood_register_gate("pex0",   CGC_BIT_PEX0);
	kirkwood_register_gate("pex1",   CGC_BIT_PEX1);
	kirkwood_register_gate("audio",  CGC_BIT_AUDIO);
	kirkwood_register_gate("tdm",    CGC_BIT_TDM);
	kirkwood_register_gate("tsu",    CGC_BIT_TSU);

	/* clkdev entries, mapping clks to devices */
	orion_clkdev_add(NULL, "orion_spi.0", runit);
	orion_clkdev_add(NULL, "orion_spi.1", runit);
	orion_clkdev_add(NULL, MV643XX_ETH_NAME ".0", ge0);
	orion_clkdev_add(NULL, MV643XX_ETH_NAME ".1", ge1);
	orion_clkdev_add(NULL, "orion_wdt", tclk);
	orion_clkdev_add("0", "sata_mv.0", sata0);
	orion_clkdev_add("1", "sata_mv.0", sata1);
}

/*****************************************************************************
 * EHCI0
 ****************************************************************************/
void __init kirkwood_ehci_init(void)
{
	kirkwood_clk_ctrl |= CGC_USB0;
	orion_ehci_init(USB_PHYS_BASE, IRQ_KIRKWOOD_USB, EHCI_PHY_NA);
}


/*****************************************************************************
 * GE00
 ****************************************************************************/
void __init kirkwood_ge00_init(struct mv643xx_eth_platform_data *eth_data)
{
	kirkwood_clk_ctrl |= CGC_GE0;

	orion_ge00_init(eth_data,
			GE00_PHYS_BASE, IRQ_KIRKWOOD_GE00_SUM,
			IRQ_KIRKWOOD_GE00_ERR);
}


/*****************************************************************************
 * GE01
 ****************************************************************************/
void __init kirkwood_ge01_init(struct mv643xx_eth_platform_data *eth_data)
{

	kirkwood_clk_ctrl |= CGC_GE1;

	orion_ge01_init(eth_data,
			GE01_PHYS_BASE, IRQ_KIRKWOOD_GE01_SUM,
			IRQ_KIRKWOOD_GE01_ERR);
}


/*****************************************************************************
 * Ethernet switch
 ****************************************************************************/
void __init kirkwood_ge00_switch_init(struct dsa_platform_data *d, int irq)
{
	orion_ge00_switch_init(d, irq);
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

void __init kirkwood_nand_init_rnb(struct mtd_partition *parts, int nr_parts,
				   int (*dev_ready)(struct mtd_info *))
{
	kirkwood_clk_ctrl |= CGC_RUNIT;
	kirkwood_nand_data.parts = parts;
	kirkwood_nand_data.nr_parts = nr_parts;
	kirkwood_nand_data.dev_ready = dev_ready;
	platform_device_register(&kirkwood_nand_flash);
}

/*****************************************************************************
 * SoC RTC
 ****************************************************************************/
static void __init kirkwood_rtc_init(void)
{
	orion_rtc_init(RTC_PHYS_BASE, IRQ_KIRKWOOD_RTC);
}


/*****************************************************************************
 * SATA
 ****************************************************************************/
void __init kirkwood_sata_init(struct mv_sata_platform_data *sata_data)
{
	kirkwood_clk_ctrl |= CGC_SATA0;
	if (sata_data->n_ports > 1)
		kirkwood_clk_ctrl |= CGC_SATA1;

	orion_sata_init(sata_data, SATA_PHYS_BASE, IRQ_KIRKWOOD_SATA);
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

static u64 mvsdio_dmamask = DMA_BIT_MASK(32);

static struct platform_device kirkwood_sdio = {
	.name		= "mvsdio",
	.id		= -1,
	.dev		= {
		.dma_mask = &mvsdio_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(mvsdio_resources),
	.resource	= mvsdio_resources,
};

void __init kirkwood_sdio_init(struct mvsdio_platform_data *mvsdio_data)
{
	u32 dev, rev;

	kirkwood_pcie_id(&dev, &rev);
	if (rev == 0 && dev != MV88F6282_DEV_ID) /* catch all Kirkwood Z0's */
		mvsdio_data->clock = 100000000;
	else
		mvsdio_data->clock = 200000000;
	kirkwood_clk_ctrl |= CGC_SDIO;
	kirkwood_sdio.dev.platform_data = mvsdio_data;
	platform_device_register(&kirkwood_sdio);
}


/*****************************************************************************
 * SPI
 ****************************************************************************/
void __init kirkwood_spi_init()
{
	kirkwood_clk_ctrl |= CGC_RUNIT;
	orion_spi_init(SPI_PHYS_BASE);
}


/*****************************************************************************
 * I2C
 ****************************************************************************/
void __init kirkwood_i2c_init(void)
{
	orion_i2c_init(I2C_PHYS_BASE, IRQ_KIRKWOOD_TWSI, 8);
}


/*****************************************************************************
 * UART0
 ****************************************************************************/

void __init kirkwood_uart0_init(void)
{
	orion_uart0_init(UART0_VIRT_BASE, UART0_PHYS_BASE,
			 IRQ_KIRKWOOD_UART_0, tclk);
}


/*****************************************************************************
 * UART1
 ****************************************************************************/
void __init kirkwood_uart1_init(void)
{
	orion_uart1_init(UART1_VIRT_BASE, UART1_PHYS_BASE,
			 IRQ_KIRKWOOD_UART_1, tclk);
}

/*****************************************************************************
 * Cryptographic Engines and Security Accelerator (CESA)
 ****************************************************************************/
void __init kirkwood_crypto_init(void)
{
	kirkwood_clk_ctrl |= CGC_CRYPTO;
	orion_crypto_init(CRYPTO_PHYS_BASE, KIRKWOOD_SRAM_PHYS_BASE,
			  KIRKWOOD_SRAM_SIZE, IRQ_KIRKWOOD_CRYPTO);
}


/*****************************************************************************
 * XOR0
 ****************************************************************************/
void __init kirkwood_xor0_init(void)
{
	kirkwood_clk_ctrl |= CGC_XOR0;

	orion_xor0_init(XOR0_PHYS_BASE, XOR0_HIGH_PHYS_BASE,
			IRQ_KIRKWOOD_XOR_00, IRQ_KIRKWOOD_XOR_01);
}


/*****************************************************************************
 * XOR1
 ****************************************************************************/
void __init kirkwood_xor1_init(void)
{
	kirkwood_clk_ctrl |= CGC_XOR1;

	orion_xor1_init(XOR1_PHYS_BASE, XOR1_HIGH_PHYS_BASE,
			IRQ_KIRKWOOD_XOR_10, IRQ_KIRKWOOD_XOR_11);
}


/*****************************************************************************
 * Watchdog
 ****************************************************************************/
void __init kirkwood_wdt_init(void)
{
	orion_wdt_init();
}


/*****************************************************************************
 * Time handling
 ****************************************************************************/
void __init kirkwood_init_early(void)
{
	orion_time_set_base(TIMER_VIRT_BASE);
}

int kirkwood_tclk;

static int __init kirkwood_find_tclk(void)
{
	u32 dev, rev;

	kirkwood_pcie_id(&dev, &rev);

	if (dev == MV88F6281_DEV_ID || dev == MV88F6282_DEV_ID)
		if (((readl(SAMPLE_AT_RESET) >> 21) & 1) == 0)
			return 200000000;

	return 166666667;
}

static void __init kirkwood_timer_init(void)
{
	kirkwood_tclk = kirkwood_find_tclk();

	orion_time_init(BRIDGE_VIRT_BASE, BRIDGE_INT_TIMER1_CLR,
			IRQ_KIRKWOOD_BRIDGE, kirkwood_tclk);
}

struct sys_timer kirkwood_timer = {
	.init = kirkwood_timer_init,
};

/*****************************************************************************
 * Audio
 ****************************************************************************/
static struct resource kirkwood_i2s_resources[] = {
	[0] = {
		.start  = AUDIO_PHYS_BASE,
		.end    = AUDIO_PHYS_BASE + SZ_16K - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_KIRKWOOD_I2S,
		.end    = IRQ_KIRKWOOD_I2S,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct kirkwood_asoc_platform_data kirkwood_i2s_data = {
	.burst       = 128,
};

static struct platform_device kirkwood_i2s_device = {
	.name		= "kirkwood-i2s",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(kirkwood_i2s_resources),
	.resource	= kirkwood_i2s_resources,
	.dev		= {
		.platform_data	= &kirkwood_i2s_data,
	},
};

static struct platform_device kirkwood_pcm_device = {
	.name		= "kirkwood-pcm-audio",
	.id		= -1,
};

void __init kirkwood_audio_init(void)
{
	kirkwood_clk_ctrl |= CGC_AUDIO;
	platform_device_register(&kirkwood_i2s_device);
	platform_device_register(&kirkwood_pcm_device);
}

/*****************************************************************************
 * General
 ****************************************************************************/
/*
 * Identify device ID and revision.
 */
char * __init kirkwood_id(void)
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
		else if (rev == MV88F6192_REV_A1)
			return "MV88F6192-A1";
		else
			return "MV88F6192-Rev-Unsupported";
	} else if (dev == MV88F6180_DEV_ID) {
		if (rev == MV88F6180_REV_A0)
			return "MV88F6180-Rev-A0";
		else if (rev == MV88F6180_REV_A1)
			return "MV88F6180-Rev-A1";
		else
			return "MV88F6180-Rev-Unsupported";
	} else if (dev == MV88F6282_DEV_ID) {
		if (rev == MV88F6282_REV_A0)
			return "MV88F6282-Rev-A0";
		else if (rev == MV88F6282_REV_A1)
			return "MV88F6282-Rev-A1";
		else
			return "MV88F6282-Rev-Unsupported";
	} else {
		return "Device-Unknown";
	}
}

void __init kirkwood_l2_init(void)
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

	/*
	 * Disable propagation of mbus errors to the CPU local bus,
	 * as this causes mbus errors (which can occur for example
	 * for PCI aborts) to throw CPU aborts, which we're not set
	 * up to deal with.
	 */
	writel(readl(CPU_CONFIG) & ~CPU_CONFIG_ERROR_PROP, CPU_CONFIG);

	kirkwood_setup_cpu_mbus();

#ifdef CONFIG_CACHE_FEROCEON_L2
	kirkwood_l2_init();
#endif

	/* Setup root of clk tree */
	kirkwood_clk_init();

	/* internal devices that every board has */
	kirkwood_rtc_init();
	kirkwood_wdt_init();
	kirkwood_xor0_init();
	kirkwood_xor1_init();
	kirkwood_crypto_init();

#ifdef CONFIG_KEXEC 
	kexec_reinit = kirkwood_enable_pcie;
#endif
}

static int __init kirkwood_clock_gate(void)
{
	unsigned int curr = readl(CLOCK_GATING_CTRL);
	u32 dev, rev;

	kirkwood_pcie_id(&dev, &rev);
	printk(KERN_DEBUG "Gating clock of unused units\n");
	printk(KERN_DEBUG "before: 0x%08x\n", curr);

	/* Make sure those units are accessible */
	writel(curr | CGC_SATA0 | CGC_SATA1 | CGC_PEX0 | CGC_PEX1, CLOCK_GATING_CTRL);

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

	/* For PCIe 1: first shutdown the phy */
	if (dev == MV88F6282_DEV_ID) {
		if (!(kirkwood_clk_ctrl & CGC_PEX1)) {
			writel(readl(PCIE1_LINK_CTRL) | 0x10, PCIE1_LINK_CTRL);
			while (1)
				if (readl(PCIE1_STATUS) & 0x1)
					break;
			writel(readl(PCIE1_LINK_CTRL) & ~0x10, PCIE1_LINK_CTRL);
		}
	} else  /* keep this bit set for devices that don't have PCIe1 */
		kirkwood_clk_ctrl |= CGC_PEX1;

	/* Now gate clock the required units */
	writel(kirkwood_clk_ctrl, CLOCK_GATING_CTRL);
	printk(KERN_DEBUG " after: 0x%08x\n", readl(CLOCK_GATING_CTRL));

	return 0;
}
late_initcall(kirkwood_clock_gate);

void kirkwood_restart(char mode, const char *cmd)
{
	/*
	 * Enable soft reset to assert RSTOUTn.
	 */
	writel(SOFT_RESET_OUT_EN, RSTOUTn_MASK);

	/*
	 * Assert soft reset.
	 */
	writel(SOFT_RESET, SYSTEM_SOFT_RESET);

	while (1)
		;
}
