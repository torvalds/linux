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
	imx_writel(0xf00, hsc_addr);

	/* CSI mode: reserved; DI control mode: legacy (from Freescale BSP) */
	imx_writel(imx_readl(hsc_addr + 0x800) | 0x30ff, hsc_addr + 0x800);

	iounmap(hsc_addr);
}

static void __init imx51_dt_init(void)
{
	imx51_ipu_mipi_setup();
	imx_src_init();

	imx_aips_allow_unprivileged_access("fsl,imx51-aipstz");
}

static void __init imx51_init_late(void)
{
	mx51_neon_fixup();
	imx51_pm_init();
}

static const char * const imx51_dt_board_compat[] __initconst = {
	"fsl,imx51",
	NULL
};

DT_MACHINE_START(IMX51_DT, "Freescale i.MX51 (Device Tree Support)")
	.init_early	= imx51_init_early,
	.init_machine	= imx51_dt_init,
	.init_late	= imx51_init_late,
	.dt_compat	= imx51_dt_board_compat,
MACHINE_END
