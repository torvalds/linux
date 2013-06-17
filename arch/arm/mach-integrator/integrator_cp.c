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
#include <linux/irqchip/versatile-fpga.h>
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

#include <mach/lm.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include <asm/hardware/timer-sp.h>

#include <plat/clcd.h>
#include <plat/sched_clock.h>

#include "cm.h"
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
 * f1300000	13000000	Counter/Timer
 * f1400000	14000000	Interrupt controller
 * f1600000	16000000	UART 0
 * f1700000	17000000	UART 1
 * f1a00000	1a000000	Debug LEDs
 * fc900000	c9000000	GPIO
 * fca00000	ca000000	SIC
 */

static struct map_desc intcp_io_desc[] __initdata __maybe_unused = {
	{
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
	u32 val = CM_CTRL_STATIC1 | CM_CTRL_STATIC2
			| CM_CTRL_LCDEN0 | CM_CTRL_LCDEN1;

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

static const struct of_device_id fpga_irq_of_match[] __initconst = {
	{ .compatible = "arm,versatile-fpga-irq", .data = fpga_irq_of_init, },
	{ /* Sentinel */ }
};

static void __init intcp_init_irq_of(void)
{
	cm_init();
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
		"uart0", NULL),
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_UART1_BASE,
		"uart1", NULL),
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

static const struct of_device_id intcp_syscon_match[] = {
	{ .compatible = "arm,integrator-cp-syscon"},
	{ },
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

	cpcon = of_find_matching_node(root, intcp_syscon_match);
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
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr->revision);
		kfree(soc_dev_attr);
		return;
	}

	parent = soc_device_to_device(soc_dev);
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
	.init_early	= intcp_init_early,
	.init_irq	= intcp_init_irq_of,
	.handle_irq	= fpga_handle_irq,
	.init_machine	= intcp_init_of,
	.restart	= integrator_restart,
	.dt_compat      = intcp_dt_board_compat,
MACHINE_END
