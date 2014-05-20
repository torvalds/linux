/*
 * Copyright 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "common.h"
#include "hardware.h"
#include "mx51.h"

static void __init imx51_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX51);
}

/*
 * The MIPI HSC unit has been removed from the i.MX51 Reference Manual by
 * the Freescale marketing division. However this did not remove the
 * hardware from the chip which still needs to be configured for proper
 * IPU support.
 */
#define MX51_MIPI_HSC_BASE 0x83fdc000
static void __init imx51_ipu_mipi_setup(void)
{
	void __iomem *hsc_addr;

	hsc_addr = ioremap(MX51_MIPI_HSC_BASE, SZ_16K);
	WARN_ON(!hsc_addr);

	/* setup MIPI module to legacy mode */
	__raw_writel(0xf00, hsc_addr);

	/* CSI mode: reserved; DI control mode: legacy (from Freescale BSP) */
	__raw_writel(__raw_readl(hsc_addr + 0x800) | 0x30ff,
		hsc_addr + 0x800);

	iounmap(hsc_addr);
}

static void __init imx51_dt_init(void)
{
	struct platform_device_info devinfo = { .name = "cpufreq-cpu0", };

	mxc_arch_reset_init_dt();
	imx51_ipu_mipi_setup();
	imx_src_init();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
	platform_device_register_full(&devinfo);
}

static void __init imx51_init_late(void)
{
	mx51_neon_fixup();
	imx51_pm_init();
}

static const char *imx51_dt_board_compat[] __initconst = {
	"fsl,imx51",
	NULL
};

DT_MACHINE_START(IMX51_DT, "Freescale i.MX51 (Device Tree Support)")
	.map_io		= mx51_map_io,
	.init_early	= imx51_init_early,
	.init_irq	= tzic_init_irq,
	.init_machine	= imx51_dt_init,
	.init_late	= imx51_init_late,
	.dt_compat	= imx51_dt_board_compat,
	.restart	= mxc_restart,
MACHINE_END
