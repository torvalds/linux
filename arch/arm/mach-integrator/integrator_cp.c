/*
 *  linux/arch/arm/mach-integrator/integrator_cp.c
 *
 *  Copyright (C) 2003 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/string.h>
#include <linux/sysdev.h>
#include <linux/amba/bus.h>
#include <linux/amba/kmi.h>
#include <linux/amba/clcd.h>
#include <linux/amba/mmci.h>
#include <linux/io.h>
#include <linux/gfp.h>
#include <linux/clkdev.h>
#include <linux/mtd/physmap.h>

#include <mach/hardware.h>
#include <mach/platform.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/hardware/arm_timer.h>
#include <asm/hardware/icst.h>

#include <mach/cm.h>
#include <mach/lm.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include <asm/hardware/timer-sp.h>

#include <plat/clcd.h>
#include <plat/fpga-irq.h>
#include <plat/sched_clock.h>

#include "common.h"

#define INTCP_PA_FLASH_BASE		0x24000000
#define INTCP_FLASH_SIZE		SZ_32M

#define INTCP_PA_CLCD_BASE		0xc0000000

#define INTCP_VA_CIC_BASE		__io_address(INTEGRATOR_HDR_BASE + 0x40)
#define INTCP_VA_PIC_BASE		__io_address(INTEGRATOR_IC_BASE)
#define INTCP_VA_SIC_BASE		__io_address(INTEGRATOR_CP_SIC_BASE)

#define INTCP_ETH_SIZE			0x10

#define INTCP_VA_CTRL_BASE		IO_ADDRESS(INTEGRATOR_CP_CTL_BASE)
#define INTCP_FLASHPROG			0x04
#define CINTEGRATOR_FLASHPROG_FLVPPEN	(1 << 0)
#define CINTEGRATOR_FLASHPROG_FLWREN	(1 << 1)

/*
 * Logical      Physical
 * f1000000	10000000	Core module registers
 * f1100000	11000000	System controller registers
 * f1200000	12000000	EBI registers
 * f1300000	13000000	Counter/Timer
 * f1400000	14000000	Interrupt controller
 * f1600000	16000000	UART 0
 * f1700000	17000000	UART 1
 * f1a00000	1a000000	Debug LEDs
 * fc900000	c9000000	GPIO
 * fca00000	ca000000	SIC
 * fcb00000	cb000000	CP system control
 */

static struct map_desc intcp_io_desc[] __initdata = {
	{
		.virtual	= IO_ADDRESS(INTEGRATOR_HDR_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_HDR_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_SC_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_SC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_EBI_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_EBI_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_CT_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_CT_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_IC_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_IC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_UART0_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_UART0_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_UART1_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_UART1_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_DBG_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_DBG_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_CP_GPIO_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_CP_GPIO_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_CP_SIC_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_CP_SIC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_CP_CTL_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_CP_CTL_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}
};

static void __init intcp_map_io(void)
{
	iotable_init(intcp_io_desc, ARRAY_SIZE(intcp_io_desc));
}

static struct fpga_irq_data cic_irq_data = {
	.base		= INTCP_VA_CIC_BASE,
	.irq_start	= IRQ_CIC_START,
	.chip.name	= "CIC",
};

static struct fpga_irq_data pic_irq_data = {
	.base		= INTCP_VA_PIC_BASE,
	.irq_start	= IRQ_PIC_START,
	.chip.name	= "PIC",
};

static struct fpga_irq_data sic_irq_data = {
	.base		= INTCP_VA_SIC_BASE,
	.irq_start	= IRQ_SIC_START,
	.chip.name	= "SIC",
};

static void __init intcp_init_irq(void)
{
	u32 pic_mask, sic_mask;

	pic_mask = ~((~0u) << (11 - IRQ_PIC_START));
	pic_mask |= (~((~0u) << (29 - 22))) << 22;
	sic_mask = ~((~0u) << (1 + IRQ_SIC_END - IRQ_SIC_START));

	/*
	 * Disable all interrupt sources
	 */
	writel(0xffffffff, INTCP_VA_PIC_BASE + IRQ_ENABLE_CLEAR);
	writel(0xffffffff, INTCP_VA_PIC_BASE + FIQ_ENABLE_CLEAR);
	writel(0xffffffff, INTCP_VA_CIC_BASE + IRQ_ENABLE_CLEAR);
	writel(0xffffffff, INTCP_VA_CIC_BASE + FIQ_ENABLE_CLEAR);
	writel(sic_mask, INTCP_VA_SIC_BASE + IRQ_ENABLE_CLEAR);
	writel(sic_mask, INTCP_VA_SIC_BASE + FIQ_ENABLE_CLEAR);

	fpga_irq_init(-1, pic_mask, &pic_irq_data);

	fpga_irq_init(-1, ~((~0u) << (1 + IRQ_CIC_END - IRQ_CIC_START)),
		&cic_irq_data);

	fpga_irq_init(IRQ_CP_CPPLDINT, sic_mask, &sic_irq_data);
}

/*
 * Clock handling
 */
#define CM_LOCK		(__io_address(INTEGRATOR_HDR_BASE)+INTEGRATOR_HDR_LOCK_OFFSET)
#define CM_AUXOSC	(__io_address(INTEGRATOR_HDR_BASE)+0x1c)

static const struct icst_params cp_auxvco_params = {
	.ref		= 24000000,
	.vco_max	= ICST525_VCO_MAX_5V,
	.vco_min	= ICST525_VCO_MIN,
	.vd_min 	= 8,
	.vd_max 	= 263,
	.rd_min 	= 3,
	.rd_max 	= 65,
	.s2div		= icst525_s2div,
	.idx2s		= icst525_idx2s,
};

static void cp_auxvco_set(struct clk *clk, struct icst_vco vco)
{
	u32 val;

	val = readl(clk->vcoreg) & ~0x7ffff;
	val |= vco.v | (vco.r << 9) | (vco.s << 16);

	writel(0xa05f, CM_LOCK);
	writel(val, clk->vcoreg);
	writel(0, CM_LOCK);
}

static const struct clk_ops cp_auxclk_ops = {
	.round	= icst_clk_round,
	.set	= icst_clk_set,
	.setvco	= cp_auxvco_set,
};

static struct clk cp_auxclk = {
	.ops	= &cp_auxclk_ops,
	.params	= &cp_auxvco_params,
	.vcoreg	= CM_AUXOSC,
};

static struct clk sp804_clk = {
	.rate	= 1000000,
};

static struct clk_lookup cp_lookups[] = {
	{	/* CLCD */
		.dev_id		= "mb:c0",
		.clk		= &cp_auxclk,
	}, {	/* SP804 timers */
		.dev_id		= "sp804",
		.clk		= &sp804_clk,
	},
};

/*
 * Flash handling.
 */
static int intcp_flash_init(struct platform_device *dev)
{
	u32 val;

	val = readl(INTCP_VA_CTRL_BASE + INTCP_FLASHPROG);
	val |= CINTEGRATOR_FLASHPROG_FLWREN;
	writel(val, INTCP_VA_CTRL_BASE + INTCP_FLASHPROG);

	return 0;
}

static void intcp_flash_exit(struct platform_device *dev)
{
	u32 val;

	val = readl(INTCP_VA_CTRL_BASE + INTCP_FLASHPROG);
	val &= ~(CINTEGRATOR_FLASHPROG_FLVPPEN|CINTEGRATOR_FLASHPROG_FLWREN);
	writel(val, INTCP_VA_CTRL_BASE + INTCP_FLASHPROG);
}

static void intcp_flash_set_vpp(struct platform_device *pdev, int on)
{
	u32 val;

	val = readl(INTCP_VA_CTRL_BASE + INTCP_FLASHPROG);
	if (on)
		val |= CINTEGRATOR_FLASHPROG_FLVPPEN;
	else
		val &= ~CINTEGRATOR_FLASHPROG_FLVPPEN;
	writel(val, INTCP_VA_CTRL_BASE + INTCP_FLASHPROG);
}

static struct physmap_flash_data intcp_flash_data = {
	.width		= 4,
	.init		= intcp_flash_init,
	.exit		= intcp_flash_exit,
	.set_vpp	= intcp_flash_set_vpp,
};

static struct resource intcp_flash_resource = {
	.start		= INTCP_PA_FLASH_BASE,
	.end		= INTCP_PA_FLASH_BASE + INTCP_FLASH_SIZE - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device intcp_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &intcp_flash_data,
	},
	.num_resources	= 1,
	.resource	= &intcp_flash_resource,
};

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= INTEGRATOR_CP_ETH_BASE,
		.end	= INTEGRATOR_CP_ETH_BASE + INTCP_ETH_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_CP_ETHINT,
		.end	= IRQ_CP_ETHINT,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static struct platform_device *intcp_devs[] __initdata = {
	&intcp_flash_device,
	&smc91x_device,
};

/*
 * It seems that the card insertion interrupt remains active after
 * we've acknowledged it.  We therefore ignore the interrupt, and
 * rely on reading it from the SIC.  This also means that we must
 * clear the latched interrupt.
 */
static unsigned int mmc_status(struct device *dev)
{
	unsigned int status = readl(IO_ADDRESS(0xca000000 + 4));
	writel(8, IO_ADDRESS(INTEGRATOR_CP_CTL_BASE + 8));

	return status & 8;
}

static struct mmci_platform_data mmc_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= mmc_status,
	.gpio_wp	= -1,
	.gpio_cd	= -1,
};

static struct amba_device mmc_device = {
	.dev		= {
		.init_name = "mb:1c",
		.platform_data = &mmc_data,
	},
	.res		= {
		.start	= INTEGRATOR_CP_MMC_BASE,
		.end	= INTEGRATOR_CP_MMC_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	.irq		= { IRQ_CP_MMCIINT0, IRQ_CP_MMCIINT1 },
	.periphid	= 0,
};

static struct amba_device aaci_device = {
	.dev		= {
		.init_name = "mb:1d",
	},
	.res		= {
		.start	= INTEGRATOR_CP_AACI_BASE,
		.end	= INTEGRATOR_CP_AACI_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	.irq		= { IRQ_CP_AACIINT, NO_IRQ },
	.periphid	= 0,
};


/*
 * CLCD support
 */
/*
 * Ensure VGA is selected.
 */
static void cp_clcd_enable(struct clcd_fb *fb)
{
	struct fb_var_screeninfo *var = &fb->fb.var;
	u32 val = CM_CTRL_STATIC1 | CM_CTRL_STATIC2;

	if (var->bits_per_pixel <= 8 ||
	    (var->bits_per_pixel == 16 && var->green.length == 5))
		/* Pseudocolor, RGB555, BGR555 */
		val |= CM_CTRL_LCDMUXSEL_VGA555_TFT555;
	else if (fb->fb.var.bits_per_pixel <= 16)
		/* truecolor RGB565 */
		val |= CM_CTRL_LCDMUXSEL_VGA565_TFT555;
	else
		val = 0; /* no idea for this, don't trust the docs */

	cm_control(CM_CTRL_LCDMUXSEL_MASK|
		   CM_CTRL_LCDEN0|
		   CM_CTRL_LCDEN1|
		   CM_CTRL_STATIC1|
		   CM_CTRL_STATIC2|
		   CM_CTRL_STATIC|
		   CM_CTRL_n24BITEN, val);
}

static int cp_clcd_setup(struct clcd_fb *fb)
{
	fb->panel = versatile_clcd_get_panel("VGA");
	if (!fb->panel)
		return -EINVAL;

	return versatile_clcd_setup_dma(fb, SZ_1M);
}

static struct clcd_board clcd_data = {
	.name		= "Integrator/CP",
	.caps		= CLCD_CAP_5551 | CLCD_CAP_RGB565 | CLCD_CAP_888,
	.check		= clcdfb_check,
	.decode		= clcdfb_decode,
	.enable		= cp_clcd_enable,
	.setup		= cp_clcd_setup,
	.mmap		= versatile_clcd_mmap_dma,
	.remove		= versatile_clcd_remove_dma,
};

static struct amba_device clcd_device = {
	.dev		= {
		.init_name = "mb:c0",
		.coherent_dma_mask = ~0,
		.platform_data = &clcd_data,
	},
	.res		= {
		.start	= INTCP_PA_CLCD_BASE,
		.end	= INTCP_PA_CLCD_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	.dma_mask	= ~0,
	.irq		= { IRQ_CP_CLCDCINT, NO_IRQ },
	.periphid	= 0,
};

static struct amba_device *amba_devs[] __initdata = {
	&mmc_device,
	&aaci_device,
	&clcd_device,
};

#define REFCOUNTER (__io_address(INTEGRATOR_HDR_BASE) + 0x28)

static void __init intcp_init_early(void)
{
	clkdev_add_table(cp_lookups, ARRAY_SIZE(cp_lookups));

	integrator_init_early();

#ifdef CONFIG_PLAT_VERSATILE_SCHED_CLOCK
	versatile_sched_clock_init(REFCOUNTER, 24000000);
#endif
}

static void __init intcp_init(void)
{
	int i;

	platform_add_devices(intcp_devs, ARRAY_SIZE(intcp_devs));

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}
}

#define TIMER0_VA_BASE __io_address(INTEGRATOR_TIMER0_BASE)
#define TIMER1_VA_BASE __io_address(INTEGRATOR_TIMER1_BASE)
#define TIMER2_VA_BASE __io_address(INTEGRATOR_TIMER2_BASE)

static void __init intcp_timer_init(void)
{
	writel(0, TIMER0_VA_BASE + TIMER_CTRL);
	writel(0, TIMER1_VA_BASE + TIMER_CTRL);
	writel(0, TIMER2_VA_BASE + TIMER_CTRL);

	sp804_clocksource_init(TIMER2_VA_BASE, "timer2");
	sp804_clockevents_init(TIMER1_VA_BASE, IRQ_TIMERINT1, "timer1");
}

static struct sys_timer cp_timer = {
	.init		= intcp_timer_init,
};

MACHINE_START(CINTEGRATOR, "ARM-IntegratorCP")
	/* Maintainer: ARM Ltd/Deep Blue Solutions Ltd */
	.boot_params	= 0x00000100,
	.reserve	= integrator_reserve,
	.map_io		= intcp_map_io,
	.init_early	= intcp_init_early,
	.init_irq	= intcp_init_irq,
	.timer		= &cp_timer,
	.init_machine	= intcp_init,
MACHINE_END
