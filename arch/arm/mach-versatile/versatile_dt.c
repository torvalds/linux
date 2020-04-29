// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Versatile board support using the device tree
 *
 *  Copyright (C) 2010 Secret Lab Technologies Ltd.
 *  Copyright (C) 2009 Jeremy Kerr <jeremy.kerr@canonical.com>
 *  Copyright (C) 2004 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/amba/bus.h>
#include <linux/amba/mmci.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

/* macro to get at MMIO space when running virtually */
#define IO_ADDRESS(x)		(((x) & 0x0fffffff) + (((x) >> 4) & 0x0f000000) + 0xf0000000)
#define __io_address(n)		((void __iomem __force *)IO_ADDRESS(n))

/*
 * ------------------------------------------------------------------------
 *  Versatile Registers
 * ------------------------------------------------------------------------
 */
#define VERSATILE_SYS_PCICTL_OFFSET           0x44
#define VERSATILE_SYS_MCI_OFFSET              0x48

/*
 * VERSATILE peripheral addresses
 */
#define VERSATILE_MMCI0_BASE           0x10005000	/* MMC interface */
#define VERSATILE_MMCI1_BASE           0x1000B000	/* MMC Interface */
#define VERSATILE_SCTL_BASE            0x101E0000	/* System controller */
#define VERSATILE_IB2_BASE             0x24000000	/* IB2 module */
#define VERSATILE_IB2_CTL_BASE		(VERSATILE_IB2_BASE + 0x03000000)

/*
 * System controller bit assignment
 */
#define VERSATILE_REFCLK	0
#define VERSATILE_TIMCLK	1

#define VERSATILE_TIMER1_EnSel	15
#define VERSATILE_TIMER2_EnSel	17
#define VERSATILE_TIMER3_EnSel	19
#define VERSATILE_TIMER4_EnSel	21

static void __iomem *versatile_sys_base;
static void __iomem *versatile_ib2_ctrl;

unsigned int mmc_status(struct device *dev)
{
	struct amba_device *adev = container_of(dev, struct amba_device, dev);
	u32 mask;

	if (adev->res.start == VERSATILE_MMCI0_BASE)
		mask = 1;
	else
		mask = 2;

	return readl(versatile_sys_base + VERSATILE_SYS_MCI_OFFSET) & mask;
}

static struct mmci_platform_data mmc0_plat_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= mmc_status,
};

static struct mmci_platform_data mmc1_plat_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= mmc_status,
};

/*
 * Lookup table for attaching a specific name and platform_data pointer to
 * devices as they get created by of_platform_populate().  Ideally this table
 * would not exist, but the current clock implementation depends on some devices
 * having a specific name.
 */
struct of_dev_auxdata versatile_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_MMCI0_BASE, "fpga:05", &mmc0_plat_data),
	OF_DEV_AUXDATA("arm,primecell", VERSATILE_MMCI1_BASE, "fpga:0b", &mmc1_plat_data),
	{}
};

static struct map_desc versatile_io_desc[] __initdata __maybe_unused = {
	{
		.virtual	=  IO_ADDRESS(VERSATILE_SCTL_BASE),
		.pfn		= __phys_to_pfn(VERSATILE_SCTL_BASE),
		.length		= SZ_4K * 9,
		.type		= MT_DEVICE
	}
};

static void __init versatile_map_io(void)
{
	debug_ll_io_init();
	iotable_init(versatile_io_desc, ARRAY_SIZE(versatile_io_desc));
}

static void __init versatile_init_early(void)
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
}

static void __init versatile_dt_pci_init(void)
{
	u32 val;
	struct device_node *np;
	struct property *newprop;

	np = of_find_compatible_node(NULL, NULL, "arm,versatile-pci");
	if (!np)
		return;

	/* Check if PCI backplane is detected */
	val = readl(versatile_sys_base + VERSATILE_SYS_PCICTL_OFFSET);
	if (val & 1) {
		/*
		 * Enable PCI accesses. Note that the documentaton is
		 * inconsistent whether or not this is needed, but the old
		 * driver had it so we will keep it.
		 */
		writel(1, versatile_sys_base + VERSATILE_SYS_PCICTL_OFFSET);
		goto out_put_node;
	}

	newprop = kzalloc(sizeof(*newprop), GFP_KERNEL);
	if (!newprop)
		goto out_put_node;

	newprop->name = kstrdup("status", GFP_KERNEL);
	newprop->value = kstrdup("disabled", GFP_KERNEL);
	newprop->length = sizeof("disabled");
	of_update_property(np, newprop);

	pr_info("Not plugged into PCI backplane!\n");

out_put_node:
	of_node_put(np);
}

static void __init versatile_dt_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "arm,core-module-versatile");
	if (np)
		versatile_sys_base = of_iomap(np, 0);
	WARN_ON(!versatile_sys_base);

	versatile_ib2_ctrl = ioremap(VERSATILE_IB2_CTL_BASE, SZ_4K);

	versatile_dt_pci_init();

	of_platform_default_populate(NULL, versatile_auxdata_lookup, NULL);
}

static const char *const versatile_dt_match[] __initconst = {
	"arm,versatile-ab",
	"arm,versatile-pb",
	NULL,
};

DT_MACHINE_START(VERSATILE_PB, "ARM-Versatile (Device Tree Support)")
	.map_io		= versatile_map_io,
	.init_early	= versatile_init_early,
	.init_machine	= versatile_dt_init,
	.dt_compat	= versatile_dt_match,
MACHINE_END
