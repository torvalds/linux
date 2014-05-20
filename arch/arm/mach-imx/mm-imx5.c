/*
 * Copyright 2008-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License.  You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 *
 * Create static mapping between physical to virtual memory.
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/pinctrl/machine.h>
#include <linux/of_address.h>

#include <asm/mach/map.h>

#include "common.h"
#include "devices/devices-common.h"
#include "hardware.h"
#include "iomux-v3.h"

/*
 * Define the MX51 memory map.
 */
static struct map_desc mx51_io_desc[] __initdata = {
	imx_map_entry(MX51, TZIC, MT_DEVICE),
	imx_map_entry(MX51, IRAM, MT_DEVICE),
	imx_map_entry(MX51, AIPS1, MT_DEVICE),
	imx_map_entry(MX51, SPBA0, MT_DEVICE),
	imx_map_entry(MX51, AIPS2, MT_DEVICE),
};

/*
 * Define the MX53 memory map.
 */
static struct map_desc mx53_io_desc[] __initdata = {
	imx_map_entry(MX53, TZIC, MT_DEVICE),
	imx_map_entry(MX53, AIPS1, MT_DEVICE),
	imx_map_entry(MX53, SPBA0, MT_DEVICE),
	imx_map_entry(MX53, AIPS2, MT_DEVICE),
};

/*
 * This function initializes the memory map. It is called during the
 * system startup to create static physical to virtual memory mappings
 * for the IO modules.
 */
void __init mx51_map_io(void)
{
	iotable_init(mx51_io_desc, ARRAY_SIZE(mx51_io_desc));
}

void __init mx53_map_io(void)
{
	iotable_init(mx53_io_desc, ARRAY_SIZE(mx53_io_desc));
}

/*
 * The MIPI HSC unit has been removed from the i.MX51 Reference Manual by
 * the Freescale marketing division. However this did not remove the
 * hardware from the chip which still needs to be configured for proper
 * IPU support.
 */
static void __init imx51_ipu_mipi_setup(void)
{
	void __iomem *hsc_addr;
	hsc_addr = MX51_IO_ADDRESS(MX51_MIPI_HSC_BASE_ADDR);

	/* setup MIPI module to legacy mode */
	__raw_writel(0xf00, hsc_addr);

	/* CSI mode: reserved; DI control mode: legacy (from Freescale BSP) */
	__raw_writel(__raw_readl(hsc_addr + 0x800) | 0x30ff,
		hsc_addr + 0x800);
}

void __init imx51_init_early(void)
{
	imx51_ipu_mipi_setup();
	mxc_set_cpu_type(MXC_CPU_MX51);
	mxc_iomux_v3_init(MX51_IO_ADDRESS(MX51_IOMUXC_BASE_ADDR));
	imx_src_init();
}

void __init imx53_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX53);
	imx_src_init();
}

void __init imx51_init_late(void)
{
	mx51_neon_fixup();
	imx51_pm_init();
}

void __init imx53_init_late(void)
{
	imx53_pm_init();
}
