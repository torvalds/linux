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
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/amba/kmi.h>
#include <linux/amba/clcd.h>
#include <linux/amba/mmci.h>
#include <linux/io.h>
#include <linux/gfp.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_data/clk-integrator.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/sys_soc.h>

#include <mach/hardware.h>
#include <mach/platform.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/hardware/arm_timer.h>
#include <asm/hardware/icst.h>

#include <mach/cm.h>
#include <mach/lm.h>
#include <mach/irqs.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include <asm/hardware/timer-sp.h>

#include <plat/clcd.h>
#include <plat/fpga-irq.h>
#include <plat/sched_clock.h>

#include "common.h"

/* Base address to the CP controller */
static void __iomem *intcp_con_base;

#define INTCP_PA_FLASH_BASE		0x24000000

#define INTCP_PA_CLCD_BASE		0xc0000000

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
	}
};

static void __init intcp_map_io(void)
{
	iotable_init(intcp_io_desc, ARRAY_SIZE(intcp_io_desc));
}

/*
 * Flash handling.
 */
static int intcp_flash_init(struct platform_device *dev)
{
	u32 val;

	val = readl(intcp_con_base + INTCP_FLASHPROG);
	val |= CINTEGRATOR_FLASHPROG_FLWREN;
	writel(val, intcp_con_base + INTCP_FLASHPROG);

	return 0;
}

static void intcp_flash_exit(struct platform_device *dev)
{
	u32 val;

	val = readl(intcp_con_base + INTCP_FLASHPROG);
	val &= ~(CINTEGRATOR_FLASHPROG_FLVPPEN|CINTEGRATOR_FLASHPROG_FLWREN);
	writel(val, intcp_con_base + INTCP_FLASHPROG);
}

static void intcp_flash_set_vpp(struct platform_device *pdev, int on)
{
	u32 val;

	val = readl(intcp_con_base + INTCP_FLASHPROG);
	if (on)
		val |= CINTEGRATOR_FLASHPROG_FLVPPEN;
	else
		val &= ~CINTEGRATOR_FLASHPROG_FLVPPEN;
	writel(val, intcp_con_base + INTCP_FLASHPROG);
}

static struct physmap_flash_data intcp_flash_data = {
	.width		= 4,
	.init		= intcp_flash_init,
	.exit		= intcp_flash_exit,
	.set_vpp	= intcp_flash_set_vpp,
};

/*
 * It seems that the card insertion interrupt remains active after
 * we've acknowledged it.  We therefore ignore the interrupt, and
 * rely on reading it from the SIC.  This also means that we must
 * clear the latched interrupt.
 */
static unsigned int mmc_status(struct device *dev)
{
	unsigned int status = readl(__io_address(0xca000000 + 4));
	writel(8, intcp_con_base + 8);

	return status & 8;
}

static struct mmci_platform_data mmc_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= mmc_status,
	.gpio_wp	= -1,
	.gpio_cd	= -1,
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

#define REFCOUNTER (__io_address(INTEGRATOR_HDR_BASE) + 0x28)

static void __init intcp_init_early(void)
{
#ifdef CONFIG_PLAT_VERSATILE_SCHED_CLOCK
	versatile_sched_clock_init(REFCOUNTER, 24000000);
#endif
}

#ifdef CONFIG_OF

static void __init intcp_timer_init_of(void)
{
	struct device_node *node;
	const char *path;
	void __iomem *base;
	int err;
	int irq;

	err = of_property_read_string(of_aliases,
				"arm,timer-primary", &path);
	if (WARN_ON(err))
		return;
	node = of_find_node_by_path(path);
	base = of_iomap(node, 0);
	if (WARN_ON(!base))
		return;
	writel(0, base + TIMER_CTRL);
	sp804_clocksource_init(base, node->name);

	err = of_property_read_string(of_aliases,
				"arm,timer-secondary", &path);
	if (WARN_ON(err))
		return;
	node = of_find_node_by_path(path);
	base = of_iomap(node, 0);
	if (WARN_ON(!base))
		return;
	irq = irq_of_parse_and_map(node, 0);
	writel(0, base + TIMER_CTRL);
	sp804_clockevents_init(base, irq, node->name);
}

static struct sys_timer cp_of_timer = {
	.init		= intcp_timer_init_of,
};

static const struct of_device_id fpga_irq_of_match[] __initconst = {
	{ .compatible = "arm,versatile-fpga-irq", .data = fpga_irq_of_init, },
	{ /* Sentinel */ }
};

static void __init intcp_init_irq_of(void)
{
	of_irq_init(fpga_irq_of_match);
	integrator_clk_init(true);
}

/*
 * For the Device Tree, add in the UART, MMC and CLCD specifics as AUXDATA
 * and enforce the bus names since these are used for clock lookups.
 */
static struct of_dev_auxdata intcp_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_RTC_BASE,
		"rtc", NULL),
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_UART0_BASE,
		"uart0", &integrator_uart_data),
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_UART1_BASE,
		"uart1", &integrator_uart_data),
	OF_DEV_AUXDATA("arm,primecell", KMI0_BASE,
		"kmi0", NULL),
	OF_DEV_AUXDATA("arm,primecell", KMI1_BASE,
		"kmi1", NULL),
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_CP_MMC_BASE,
		"mmci", &mmc_data),
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_CP_AACI_BASE,
		"aaci", &mmc_data),
	OF_DEV_AUXDATA("arm,primecell", INTCP_PA_CLCD_BASE,
		"clcd", &clcd_data),
	OF_DEV_AUXDATA("cfi-flash", INTCP_PA_FLASH_BASE,
		"physmap-flash", &intcp_flash_data),
	{ /* sentinel */ },
};

static void __init intcp_init_of(void)
{
	struct device_node *root;
	struct device_node *cpcon;
	struct device *parent;
	struct soc_device *soc_dev;
	struct soc_device_attribute *soc_dev_attr;
	u32 intcp_sc_id;
	int err;

	/* Here we create an SoC device for the root node */
	root = of_find_node_by_path("/");
	if (!root)
		return;
	cpcon = of_find_node_by_path("/cpcon");
	if (!cpcon)
		return;

	intcp_con_base = of_iomap(cpcon, 0);
	if (!intcp_con_base)
		return;

	intcp_sc_id = readl(intcp_con_base);

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return;

	err = of_property_read_string(root, "compatible",
				      &soc_dev_attr->soc_id);
	if (err)
		return;
	err = of_property_read_string(root, "model", &soc_dev_attr->machine);
	if (err)
		return;
	soc_dev_attr->family = "Integrator";
	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "%c",
					   'A' + (intcp_sc_id & 0x0f));

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR_OR_NULL(soc_dev)) {
		kfree(soc_dev_attr->revision);
		kfree(soc_dev_attr);
		return;
	}

	parent = soc_device_to_device(soc_dev);

	if (!IS_ERR_OR_NULL(parent))
		integrator_init_sysfs(parent, intcp_sc_id);

	of_platform_populate(root, of_default_bus_match_table,
			intcp_auxdata_lookup, parent);
}

static const char * intcp_dt_board_compat[] = {
	"arm,integrator-cp",
	NULL,
};

DT_MACHINE_START(INTEGRATOR_CP_DT, "ARM Integrator/CP (Device Tree)")
	.reserve	= integrator_reserve,
	.map_io		= intcp_map_io,
	.nr_irqs	= NR_IRQS_INTEGRATOR_CP,
	.init_early	= intcp_init_early,
	.init_irq	= intcp_init_irq_of,
	.handle_irq	= fpga_handle_irq,
	.timer		= &cp_of_timer,
	.init_machine	= intcp_init_of,
	.restart	= integrator_restart,
	.dt_compat      = intcp_dt_board_compat,
MACHINE_END

#endif

#ifdef CONFIG_ATAGS

/*
 * For the ATAG boot some static mappings are needed. This will
 * go away with the ATAG support down the road.
 */

static struct map_desc intcp_io_desc_atag[] __initdata = {
	{
		.virtual	= IO_ADDRESS(INTEGRATOR_CP_CTL_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_CP_CTL_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	},
};

static void __init intcp_map_io_atag(void)
{
	iotable_init(intcp_io_desc_atag, ARRAY_SIZE(intcp_io_desc_atag));
	intcp_con_base = __io_address(INTEGRATOR_CP_CTL_BASE);
	intcp_map_io();
}


/*
 * This is where non-devicetree initialization code is collected and stashed
 * for eventual deletion.
 */

#define INTCP_FLASH_SIZE		SZ_32M

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

#define INTCP_ETH_SIZE			0x10

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

#define INTCP_VA_CIC_BASE		__io_address(INTEGRATOR_HDR_BASE + 0x40)
#define INTCP_VA_PIC_BASE		__io_address(INTEGRATOR_IC_BASE)
#define INTCP_VA_SIC_BASE		__io_address(INTEGRATOR_CP_SIC_BASE)

static void __init intcp_init_irq(void)
{
	u32 pic_mask, cic_mask, sic_mask;

	/* These masks are for the HW IRQ registers */
	pic_mask = ~((~0u) << (11 - IRQ_PIC_START));
	pic_mask |= (~((~0u) << (29 - 22))) << 22;
	cic_mask = ~((~0u) << (1 + IRQ_CIC_END - IRQ_CIC_START));
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

	fpga_irq_init(INTCP_VA_PIC_BASE, "PIC", IRQ_PIC_START,
		      -1, pic_mask, NULL);

	fpga_irq_init(INTCP_VA_CIC_BASE, "CIC", IRQ_CIC_START,
		      -1, cic_mask, NULL);

	fpga_irq_init(INTCP_VA_SIC_BASE, "SIC", IRQ_SIC_START,
		      IRQ_CP_CPPLDINT, sic_mask, NULL);

	integrator_clk_init(true);
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

#define INTEGRATOR_CP_MMC_IRQS	{ IRQ_CP_MMCIINT0, IRQ_CP_MMCIINT1 }
#define INTEGRATOR_CP_AACI_IRQS	{ IRQ_CP_AACIINT }

static AMBA_APB_DEVICE(mmc, "mmci", 0, INTEGRATOR_CP_MMC_BASE,
	INTEGRATOR_CP_MMC_IRQS, &mmc_data);

static AMBA_APB_DEVICE(aaci, "aaci", 0, INTEGRATOR_CP_AACI_BASE,
	INTEGRATOR_CP_AACI_IRQS, NULL);

static AMBA_AHB_DEVICE(clcd, "clcd", 0, INTCP_PA_CLCD_BASE,
	{ IRQ_CP_CLCDCINT }, &clcd_data);

static struct amba_device *amba_devs[] __initdata = {
	&mmc_device,
	&aaci_device,
	&clcd_device,
};

static void __init intcp_init(void)
{
	int i;

	platform_add_devices(intcp_devs, ARRAY_SIZE(intcp_devs));

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}
	integrator_init(true);
}

MACHINE_START(CINTEGRATOR, "ARM-IntegratorCP")
	/* Maintainer: ARM Ltd/Deep Blue Solutions Ltd */
	.atag_offset	= 0x100,
	.reserve	= integrator_reserve,
	.map_io		= intcp_map_io_atag,
	.nr_irqs	= NR_IRQS_INTEGRATOR_CP,
	.init_early	= intcp_init_early,
	.init_irq	= intcp_init_irq,
	.handle_irq	= fpga_handle_irq,
	.timer		= &cp_timer,
	.init_machine	= intcp_init,
	.restart	= integrator_restart,
MACHINE_END

#endif
