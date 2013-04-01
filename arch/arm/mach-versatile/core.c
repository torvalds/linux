/*
 *  linux/arch/arm/mach-versatile/core.c
 *
 *  Copyright (C) 1999 - 2003 ARM Limited
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
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>
#include <linux/amba/pl061.h>
#include <linux/amba/mmci.h>
#include <linux/amba/pl022.h>
#include <linux/io.h>
#include <linux/irqchip/versatile-fpga.h>
#include <linux/gfp.h>
#include <linux/clkdev.h>
#include <linux/mtd/physmap.h>

#include <asm/irq.h>
#include <asm/hardware/arm_timer.h>
#include <asm/hardware/icst.h>
#include <asm/hardware/vic.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <mach/hardware.h>
#include <mach/platform.h>
#include <asm/hardware/timer-sp.h>

#include <plat/clcd.h>
#include <plat/sched_clock.h>

#include "core.h"

/*
 * All IO addresses are mapped onto VA 0xFFFx.xxxx, where x.xxxx
 * is the (PA >> 12).
 *
 * Setup a VA for the Versatile Vectored Interrupt Controller.
 */
#define VA_VIC_BASE		__io_address(VERSATILE_VIC_BASE)
#define VA_SIC_BASE		__io_address(VERSATILE_SIC_BASE)

#if 1
#define IRQ_MMCI0A	IRQ_VICSOURCE22
#define IRQ_AACI	IRQ_VICSOURCE24
#define IRQ_ETH		IRQ_VICSOURCE25
#define PIC_MASK	0xFFD00000
#else
#define IRQ_MMCI0A	IRQ_SIC_MMCI0A
#define IRQ_AACI	IRQ_SIC_AACI
#define IRQ_ETH		IRQ_SIC_ETH
#define PIC_MASK	0
#endif

/* Lookup table for finding a DT node that represents the vic instance */
static const struct of_device_id vic_of_match[] __initconst = {
	{ .compatible = "arm,versatile-vic", },
	{}
};

static const struct of_device_id sic_of_match[] __initconst = {
	{ .compatible = "arm,versatile-sic", },
	{}
};

void __init versatile_init_irq(void)
{
	struct device_node *np;

	np = of_find_matching_node_by_address(NULL, vic_of_match,
					      VERSATILE_VIC_BASE);
	__vic_init(VA_VIC_BASE, IRQ_VIC_START, ~0, 0, np);

	writel(~0, VA_SIC_BASE + SIC_IRQ_ENABLE_CLEAR);

	np = of_find_matching_node_by_address(NULL, sic_of_match,
					      VERSATILE_SIC_BASE);

	fpga_irq_init(VA_SIC_BASE, "SIC", IRQ_SIC_START,
		IRQ_VICSOURCE31, ~PIC_MASK, np);

	/*
	 * Interrupts on secondary controller from 0 to 8 are routed to
	 * source 31 on PIC.
	 * Interrupts from 21 to 31 are routed directly to the VIC on
	 * the corresponding number on primary controller. This is controlled
	 * by setting PIC_ENABLEx.
	 */
	writel(PIC_MASK, VA_SIC_BASE + SIC_INT_PIC_ENABLE);
}

static struct map_desc versatile_io_desc[] __initdata = {
	{
		.virtual	=  IO_ADDRESS(VERSATILE_SYS_BASE),
		.pfn		= __phys_to_pfn(VERSATILE_SYS_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	=  IO_ADDRESS(VERSATILE_SIC_BASE),
		.pfn		= __phys_to_pfn(VERSATILE_SIC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	=  IO_ADDRESS(VERSATILE_VIC_BASE),
		.pfn		= __phys_to_pfn(VERSATILE_VIC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	=  IO_ADDRESS(VERSATILE_SCTL_BASE),
		.pfn		= __phys_to_pfn(VERSATILE_SCTL_BASE),
		.length		= SZ_4K * 9,
		.type		= MT_DEVICE
	},
#ifdef CONFIG_MACH_VERSATILE_AB
 	{
		.virtual	=  IO_ADDRESS(VERSATILE_IB2_BASE),
		.pfn		= __phys_to_pfn(VERSATILE_IB2_BASE),
		.length		= SZ_64M,
		.type		= MT_DEVICE
	},
#endif
#ifdef CONFIG_DEBUG_LL
 	{
		.virtual	=  IO_ADDRESS(VERSATILE_UART0_BASE),
		.pfn		= __phys_to_pfn(VERSATILE_UART0_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	},
#endif
#ifdef CONFIG_PCI
 	{
		.virtual	=  IO_ADDRESS(VERSATILE_PCI_CORE_BASE),
		.pfn		= __phys_to_pfn(VERSATILE_PCI_CORE_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	=  (unsigned long)VERSATILE_PCI_VIRT_BASE,
		.pfn		= __phys_to_pfn(VERSATILE_PCI_BASE),
		.length		= VERSATILE_PCI_BASE_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	=  (unsigned long)VERSATILE_PCI_CFG_VIRT_BASE,
		.pfn		= __phys_to_pfn(VERSATILE_PCI_CFG_BASE),
		.length		= VERSATILE_PCI_CFG_BASE_SIZE,
		.type		= MT_DEVICE
	},
#endif
};

void __init versatile_map_io(void)
{
	iotable_init(versatile_io_desc, ARRAY_SIZE(versatile_io_desc));
}


#define VERSATILE_FLASHCTRL    (__io_address(VERSATILE_SYS_BASE) + VERSATILE_SYS_FLASH_OFFSET)

static void versatile_flash_set_vpp(struct platform_device *pdev, int on)
{
	u32 val;

	val = __raw_readl(VERSATILE_FLASHCTRL);
	if (on)
		val |= VERSATILE_FLASHPROG_FLVPPEN;
	else
		val &= ~VERSATILE_FLASHPROG_FLVPPEN;
	__raw_writel(val, VERSATILE_FLASHCTRL);
}

static struct physmap_flash_data versatile_flash_data = {
	.width			= 4,
	.set_vpp		= versatile_flash_set_vpp,
};

static struct resource versatile_flash_resource = {
	.start			= VERSATILE_FLASH_BASE,
	.end			= VERSATILE_FLASH_BASE + VERSATILE_FLASH_SIZE - 1,
	.flags			= IORESOURCE_MEM,
};

static struct platform_device versatile_flash_device = {
	.name			= "physmap-flash",
	.id			= 0,
	.dev			= {
		.platform_data	= &versatile_flash_data,
	},
	.num_resources		= 1,
	.resource		= &versatile_flash_resource,
};

static struct resource smc91x_resources[] = {
	[0] = {
		.start		= VERSATILE_ETH_BASE,
		.end		= VERSATILE_ETH_BASE + SZ_64K - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_ETH,
		.end		= IRQ_ETH,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static struct resource versatile_i2c_resource = {
	.start			= VERSATILE_I2C_BASE,
	.end			= VERSATILE_I2C_BASE + SZ_4K - 1,
	.flags			= IORESOURCE_MEM,
};

static struct platform_device versatile_i2c_device = {
	.name			= "versatile-i2c",
	.id			= 0,
	.num_resources		= 1,
	.resource		= &versatile_i2c_resource,
};

static struct i2c_board_info versatile_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("ds1338", 0xd0 >> 1),
	},
};

static int __init versatile_i2c_init(void)
{
	return i2c_register_board_info(0, versatile_i2c_board_info,
				       ARRAY_SIZE(versatile_i2c_board_info));
}
arch_initcall(versatile_i2c_init);

#define VERSATILE_SYSMCI	(__io_address(VERSATILE_SYS_BASE) + VERSATILE_SYS_MCI_OFFSET)

unsigned int mmc_status(struct device *dev)
{
	struct amba_device *adev = container_of(dev, struct amba_device, dev);
	u32 mask;

	if (adev->res.start == VERSATILE_MMCI0_BASE)
		mask = 1;
	else
		mask = 2;

	return readl(VERSATILE_SYSMCI) & mask;
}

static struct mmci_platform_data mmc0_plat_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= mmc_status,
	.gpio_wp	= -1,
	.gpio_cd	= -1,
};

static struct resource char_lcd_resources[] = {
	{
		.start = VERSATILE_CHAR_LCD_BASE,
		.end   = (VERSATILE_CHAR_LCD_BASE + SZ_4K - 1),
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device char_lcd_device = {
	.name           =       "arm-charlcd",
	.id             =       -1,
	.num_resources  =       ARRAY_SIZE(char_lcd_resources),
	.resource       =       char_lcd_resources,
};

/*
 * Clock handling
 */
static const struct icst_params versatile_oscvco_params = {
	.ref		= 24000000,
	.vco_max	= ICST307_VCO_MAX,
	.vco_min	= ICST307_VCO_MIN,
	.vd_min		= 4 + 8,
	.vd_max		= 511 + 8,
	.rd_min		= 1 + 2,
	.rd_max		= 127 + 2,
	.s2div		= icst307_s2div,
	.idx2s		= icst307_idx2s,
};

static void versatile_oscvco_set(struct clk *clk, struct icst_vco vco)
{
	void __iomem *sys_lock = __io_address(VERSATILE_SYS_BASE) + VERSATILE_SYS_LOCK_OFFSET;
	u32 val;

	val = readl(clk->vcoreg) & ~0x7ffff;
	val |= vco.v | (vco.r << 9) | (vco.s << 16);

	writel(0xa05f, sys_lock);
	writel(val, clk->vcoreg);
	writel(0, sys_lock);
}

static const struct clk_ops osc4_clk_ops = {
	.round	= icst_clk_round,
	.set	= icst_clk_set,
	.setvco	= versatile_oscvco_set,
};

static struct clk osc4_clk = {
	.ops	= &osc4_clk_ops,
	.params	= &versatile_oscvco_params,
};

/*
 * These are fixed clocks.
 */
static struct clk ref24_clk = {
	.rate	= 24000000,
};

static struct clk sp804_clk = {
	.rate	= 1000000,
};

static struct clk dummy_apb_pclk;

static struct clk_lookup lookups[] = {
	{	/* AMBA bus clock */
		.con_id		= "apb_pclk",
		.clk		= &dummy_apb_pclk,
	}, {	/* UART0 */
		.dev_id		= "dev:f1",
		.clk		= &ref24_clk,
	}, {	/* UART1 */
		.dev_id		= "dev:f2",
		.clk		= &ref24_clk,
	}, {	/* UART2 */
		.dev_id		= "dev:f3",
		.clk		= &ref24_clk,
	}, {	/* UART3 */
		.dev_id		= "fpga:09",
		.clk		= &ref24_clk,
	}, {	/* KMI0 */
		.dev_id		= "fpga:06",
		.clk		= &ref24_clk,
	}, {	/* KMI1 */
		.dev_id		= "fpga:07",
		.clk		= &ref24_clk,
	}, {	/* MMC0 */
		.dev_id		= "fpga:05",
		.clk		= &ref24_clk,
	}, {	/* MMC1 */
		.dev_id		= "fpga:0b",
		.clk		= &ref24_clk,
	}, {	/* SSP */
		.dev_id		= "dev:f4",
		.clk		= &ref24_clk,
	}, {	/* CLCD */
		.dev_id		= "dev:20",
		.clk		= &osc4_clk,
	}, {	/* SP804 timers */
		.dev_id		= "sp804",
		.clk		= &sp804_clk,
	},
};

/*
 * CLCD support.
 */
#define SYS_CLCD_MODE_MASK	(3 << 0)
#define SYS_CLCD_MODE_888	(0 << 0)
#define SYS_CLCD_MODE_5551	(1 << 0)
#define SYS_CLCD_MODE_565_RLSB	(2 << 0)
#define SYS_CLCD_MODE_565_BLSB	(3 << 0)
#define SYS_CLCD_NLCDIOON	(1 << 2)
#define SYS_CLCD_VDDPOSSWITCH	(1 << 3)
#define SYS_CLCD_PWR3V5SWITCH	(1 << 4)
#define SYS_CLCD_ID_MASK	(0x1f << 8)
#define SYS_CLCD_ID_SANYO_3_8	(0x00 << 8)
#define SYS_CLCD_ID_UNKNOWN_8_4	(0x01 << 8)
#define SYS_CLCD_ID_EPSON_2_2	(0x02 << 8)
#define SYS_CLCD_ID_SANYO_2_5	(0x07 << 8)
#define SYS_CLCD_ID_VGA		(0x1f << 8)

static bool is_sanyo_2_5_lcd;

/*
 * Disable all display connectors on the interface module.
 */
static void versatile_clcd_disable(struct clcd_fb *fb)
{
	void __iomem *sys_clcd = __io_address(VERSATILE_SYS_BASE) + VERSATILE_SYS_CLCD_OFFSET;
	u32 val;

	val = readl(sys_clcd);
	val &= ~SYS_CLCD_NLCDIOON | SYS_CLCD_PWR3V5SWITCH;
	writel(val, sys_clcd);

#ifdef CONFIG_MACH_VERSATILE_AB
	/*
	 * If the LCD is Sanyo 2x5 in on the IB2 board, turn the back-light off
	 */
	if (machine_is_versatile_ab() && is_sanyo_2_5_lcd) {
		void __iomem *versatile_ib2_ctrl = __io_address(VERSATILE_IB2_CTRL);
		unsigned long ctrl;

		ctrl = readl(versatile_ib2_ctrl);
		ctrl &= ~0x01;
		writel(ctrl, versatile_ib2_ctrl);
	}
#endif
}

/*
 * Enable the relevant connector on the interface module.
 */
static void versatile_clcd_enable(struct clcd_fb *fb)
{
	struct fb_var_screeninfo *var = &fb->fb.var;
	void __iomem *sys_clcd = __io_address(VERSATILE_SYS_BASE) + VERSATILE_SYS_CLCD_OFFSET;
	u32 val;

	val = readl(sys_clcd);
	val &= ~SYS_CLCD_MODE_MASK;

	switch (var->green.length) {
	case 5:
		val |= SYS_CLCD_MODE_5551;
		break;
	case 6:
		if (var->red.offset == 0)
			val |= SYS_CLCD_MODE_565_RLSB;
		else
			val |= SYS_CLCD_MODE_565_BLSB;
		break;
	case 8:
		val |= SYS_CLCD_MODE_888;
		break;
	}

	/*
	 * Set the MUX
	 */
	writel(val, sys_clcd);

	/*
	 * And now enable the PSUs
	 */
	val |= SYS_CLCD_NLCDIOON | SYS_CLCD_PWR3V5SWITCH;
	writel(val, sys_clcd);

#ifdef CONFIG_MACH_VERSATILE_AB
	/*
	 * If the LCD is Sanyo 2x5 in on the IB2 board, turn the back-light on
	 */
	if (machine_is_versatile_ab() && is_sanyo_2_5_lcd) {
		void __iomem *versatile_ib2_ctrl = __io_address(VERSATILE_IB2_CTRL);
		unsigned long ctrl;

		ctrl = readl(versatile_ib2_ctrl);
		ctrl |= 0x01;
		writel(ctrl, versatile_ib2_ctrl);
	}
#endif
}

/*
 * Detect which LCD panel is connected, and return the appropriate
 * clcd_panel structure.  Note: we do not have any information on
 * the required timings for the 8.4in panel, so we presently assume
 * VGA timings.
 */
static int versatile_clcd_setup(struct clcd_fb *fb)
{
	void __iomem *sys_clcd = __io_address(VERSATILE_SYS_BASE) + VERSATILE_SYS_CLCD_OFFSET;
	const char *panel_name;
	u32 val;

	is_sanyo_2_5_lcd = false;

	val = readl(sys_clcd) & SYS_CLCD_ID_MASK;
	if (val == SYS_CLCD_ID_SANYO_3_8)
		panel_name = "Sanyo TM38QV67A02A";
	else if (val == SYS_CLCD_ID_SANYO_2_5) {
		panel_name = "Sanyo QVGA Portrait";
		is_sanyo_2_5_lcd = true;
	} else if (val == SYS_CLCD_ID_EPSON_2_2)
		panel_name = "Epson L2F50113T00";
	else if (val == SYS_CLCD_ID_VGA)
		panel_name = "VGA";
	else {
		printk(KERN_ERR "CLCD: unknown LCD panel ID 0x%08x, using VGA\n",
			val);
		panel_name = "VGA";
	}

	fb->panel = versatile_clcd_get_panel(panel_name);
	if (!fb->panel)
		return -EINVAL;

	return versatile_clcd_setup_dma(fb, SZ_1M);
}

static void versatile_clcd_decode(struct clcd_fb *fb, struct clcd_regs *regs)
{
	clcdfb_decode(fb, regs);

	/* Always clear BGR for RGB565: we do the routing externally */
	if (fb->fb.var.green.length == 6)
		regs->cntl &= ~CNTL_BGR;
}

static struct clcd_board clcd_plat_data = {
	.name		= "Versatile",
	.caps		= CLCD_CAP_5551 | CLCD_CAP_565 | CLCD_CAP_888,
	.check		= clcdfb_check,
	.decode		= versatile_clcd_decode,
	.disable	= versatile_clcd_disable,
	.enable		= versatile_clcd_enable,
	.setup		= versatile_clcd_setup,
	.mmap		= versatile_clcd_mmap_dma,
	.remove		= versatile_clcd_remove_dma,
};

static struct pl061_platform_data gpio0_plat_data = {
	.gpio_base	= 0,
	.irq_base	= IRQ_GPIO0_START,
};

static struct pl061_platform_data gpio1_plat_data = {
	.gpio_base	= 8,
	.irq_base	= IRQ_GPIO1_START,
};

static struct pl022_ssp_controller ssp0_plat_data = {
	.bus_id = 0,
	.enable_dma = 0,
	.num_chipselect = 1,
};

#define AACI_IRQ	{ IRQ_AACI }
#define MMCI0_IRQ	{ IRQ_MMCI0A,IRQ_SIC_MMCI0B }
#define KMI0_IRQ	{ IRQ_SIC_KMI0 }
#define KMI1_IRQ	{ IRQ_SIC_KMI1 }

/*
 * These devices are connected directly to the multi-layer AHB switch
 */
#define SMC_IRQ		{ }
#define MPMC_IRQ	{ }
#define CLCD_IRQ	{ IRQ_CLCDINT }
#define DMAC_IRQ	{ IRQ_DMAINT }

/*
 * These devices are connected via the core APB bridge
 */
#define SCTL_IRQ	{ }
#define WATCHDOG_IRQ	{ IRQ_WDOGINT }
#define GPIO0_IRQ	{ IRQ_GPIOINT0 }
#define GPIO1_IRQ	{ IRQ_GPIOINT1 }
#define RTC_IRQ		{ IRQ_RTCINT }

/*
 * These devices are connected via the DMA APB bridge
 */
#define SCI_IRQ		{ IRQ_SCIINT }
#define UART0_IRQ	{ IRQ_UARTINT0 }
#define UART1_IRQ	{ IRQ_UARTINT1 }
#define UART2_IRQ	{ IRQ_UARTINT2 }
#define SSP_IRQ		{ IRQ_SSPINT }

/* FPGA Primecells */
APB_DEVICE(aaci,  "fpga:04", AACI,     NULL);
APB_DEVICE(mmc0,  "fpga:05", MMCI0,    &mmc0_plat_data);
APB_DEVICE(kmi0,  "fpga:06", KMI0,     NULL);
APB_DEVICE(kmi1,  "fpga:07", KMI1,     NULL);

/* DevChip Primecells */
AHB_DEVICE(smc,   "dev:00",  SMC,      NULL);
AHB_DEVICE(mpmc,  "dev:10",  MPMC,     NULL);
AHB_DEVICE(clcd,  "dev:20",  CLCD,     &clcd_plat_data);
AHB_DEVICE(dmac,  "dev:30",  DMAC,     NULL);
APB_DEVICE(sctl,  "dev:e0",  SCTL,     NULL);
APB_DEVICE(wdog,  "dev:e1",  WATCHDOG, NULL);
APB_DEVICE(gpio0, "dev:e4",  GPIO0,    &gpio0_plat_data);
APB_DEVICE(gpio1, "dev:e5",  GPIO1,    &gpio1_plat_data);
APB_DEVICE(rtc,   "dev:e8",  RTC,      NULL);
APB_DEVICE(sci0,  "dev:f0",  SCI,      NULL);
APB_DEVICE(uart0, "dev:f1",  UART0,    NULL);
APB_DEVICE(uart1, "dev:f2",  UART1,    NULL);
APB_DEVICE(uart2, "dev:f3",  UART2,    NULL);
APB_DEVICE(ssp0,  "dev:f4",  SSP,      &ssp0_plat_data);

static struct amba_device *amba_devs[] __initdata = {
	&dmac_device,
	&uart0_device,
	&uart1_device,
	&uart2_device,
	&smc_device,
	&mpmc_device,
	&clcd_device,
	&sctl_device,
	&wdog_device,
	&gpio0_device,
	&gpio1_device,
	&rtc_device,
	&sci0_device,
	&ssp0_device,
	&aaci_device,
	&mmc0_device,
	&kmi0_device,
	&kmi1_device,
};

#ifdef CONFIG_OF
/*
 * Lookup table for attaching a specific name and platform_data pointer to
 * devices as they get created by of_platform_populate().  Ideally this table
 * would not exist, but the current clock implementation depends on some devices
 * having a specific name.
 */
struct of_dev_auxdata versatile_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_MMCI0_BASE, "fpga:05", &mmc0_plat_data),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_KMI0_BASE, "fpga:06", NULL),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_KMI1_BASE, "fpga:07", NULL),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_UART3_BASE, "fpga:09", NULL),
	/* FIXME: this is buggy, the platform data is needed for this MMC instance too */
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_MMCI1_BASE, "fpga:0b", NULL),

	OF_DEV_AUXDATA("arm,primecell", VERSATILE_CLCD_BASE, "dev:20", &clcd_plat_data),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_UART0_BASE, "dev:f1", NULL),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_UART1_BASE, "dev:f2", NULL),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_UART2_BASE, "dev:f3", NULL),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_SSP_BASE, "dev:f4", &ssp0_plat_data),

#if 0
	/*
	 * These entries are unnecessary because no clocks referencing
	 * them.  I've left them in for now as place holders in case
	 * any of them need to be added back, but they should be
	 * removed before actually committing this patch.  --gcl
	 */
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_AACI_BASE, "fpga:04", NULL),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_SCI1_BASE, "fpga:0a", NULL),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_SMC_BASE, "dev:00", NULL),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_MPMC_BASE, "dev:10", NULL),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_DMAC_BASE, "dev:30", NULL),

	OF_DEV_AUXDATA("arm,primecell", VERSATILE_SCTL_BASE, "dev:e0", NULL),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_WATCHDOG_BASE, "dev:e1", NULL),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_GPIO0_BASE, "dev:e4", NULL),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_GPIO1_BASE, "dev:e5", NULL),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_GPIO2_BASE, "dev:e6", NULL),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_GPIO3_BASE, "dev:e7", NULL),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_RTC_BASE, "dev:e8", NULL),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_SCI_BASE, "dev:f0", NULL),
#endif
	{}
};
#endif

#ifdef CONFIG_LEDS
#define VA_LEDS_BASE (__io_address(VERSATILE_SYS_BASE) + VERSATILE_SYS_LED_OFFSET)

static void versatile_leds_event(led_event_t ledevt)
{
	unsigned long flags;
	u32 val;

	local_irq_save(flags);
	val = readl(VA_LEDS_BASE);

	switch (ledevt) {
	case led_idle_start:
		val = val & ~VERSATILE_SYS_LED0;
		break;

	case led_idle_end:
		val = val | VERSATILE_SYS_LED0;
		break;

	case led_timer:
		val = val ^ VERSATILE_SYS_LED1;
		break;

	case led_halted:
		val = 0;
		break;

	default:
		break;
	}

	writel(val, VA_LEDS_BASE);
	local_irq_restore(flags);
}
#endif	/* CONFIG_LEDS */

void versatile_restart(char mode, const char *cmd)
{
	void __iomem *sys = __io_address(VERSATILE_SYS_BASE);
	u32 val;

	val = __raw_readl(sys + VERSATILE_SYS_RESETCTL_OFFSET);
	val |= 0x105;

	__raw_writel(0xa05f, sys + VERSATILE_SYS_LOCK_OFFSET);
	__raw_writel(val, sys + VERSATILE_SYS_RESETCTL_OFFSET);
	__raw_writel(0, sys + VERSATILE_SYS_LOCK_OFFSET);
}

/* Early initializations */
void __init versatile_init_early(void)
{
	void __iomem *sys = __io_address(VERSATILE_SYS_BASE);

	osc4_clk.vcoreg	= sys + VERSATILE_SYS_OSCCLCD_OFFSET;
	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	versatile_sched_clock_init(sys + VERSATILE_SYS_24MHz_OFFSET, 24000000);
}

void __init versatile_init(void)
{
	int i;

	platform_device_register(&versatile_flash_device);
	platform_device_register(&versatile_i2c_device);
	platform_device_register(&smc91x_device);
	platform_device_register(&char_lcd_device);

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}
}

/*
 * Where is the timer (VA)?
 */
#define TIMER0_VA_BASE		 __io_address(VERSATILE_TIMER0_1_BASE)
#define TIMER1_VA_BASE		(__io_address(VERSATILE_TIMER0_1_BASE) + 0x20)
#define TIMER2_VA_BASE		 __io_address(VERSATILE_TIMER2_3_BASE)
#define TIMER3_VA_BASE		(__io_address(VERSATILE_TIMER2_3_BASE) + 0x20)

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
void __init versatile_timer_init(void)
{
	u32 val;

	/* 
	 * set clock frequency: 
	 *	VERSATILE_REFCLK is 32KHz
	 *	VERSATILE_TIMCLK is 1MHz
	 */
	val = readl(__io_address(VERSATILE_SCTL_BASE));
	writel((VERSATILE_TIMCLK << VERSATILE_TIMER1_EnSel) |
	       (VERSATILE_TIMCLK << VERSATILE_TIMER2_EnSel) | 
	       (VERSATILE_TIMCLK << VERSATILE_TIMER3_EnSel) |
	       (VERSATILE_TIMCLK << VERSATILE_TIMER4_EnSel) | val,
	       __io_address(VERSATILE_SCTL_BASE));

	/*
	 * Initialise to a known state (all timers off)
	 */
	writel(0, TIMER0_VA_BASE + TIMER_CTRL);
	writel(0, TIMER1_VA_BASE + TIMER_CTRL);
	writel(0, TIMER2_VA_BASE + TIMER_CTRL);
	writel(0, TIMER3_VA_BASE + TIMER_CTRL);

	sp804_clocksource_init(TIMER3_VA_BASE, "timer3");
	sp804_clockevents_init(TIMER0_VA_BASE, IRQ_TIMERINT0_1, "timer0");
}
