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

#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/iomux-v3.h>

/*
 * Define the MX51 memory map.
 */
static struct map_desc mx51_io_desc[] __initdata = {
	imx_map_entry(MX51, IRAM, MT_DEVICE),
	imx_map_entry(MX51, DEBUG, MT_DEVICE),
	imx_map_entry(MX51, AIPS1, MT_DEVICE),
	imx_map_entry(MX51, SPBA0, MT_DEVICE),
	imx_map_entry(MX51, AIPS2, MT_DEVICE),
};

/*
 * Define the MX53 memory map.
 */
static struct map_desc mx53_io_desc[] __initdata = {
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
	mxc_set_cpu_type(MXC_CPU_MX51);
	mxc_iomux_v3_init(MX51_IO_ADDRESS(MX51_IOMUXC_BASE_ADDR));
	mxc_arch_reset_init(MX51_IO_ADDRESS(MX51_WDOG_BASE_ADDR));
	iotable_init(mx51_io_desc, ARRAY_SIZE(mx51_io_desc));
}

void __init mx53_map_io(void)
{
	mxc_set_cpu_type(MXC_CPU_MX53);
	mxc_iomux_v3_init(MX53_IO_ADDRESS(MX53_IOMUXC_BASE_ADDR));
	mxc_arch_reset_init(MX53_IO_ADDRESS(MX53_WDOG_BASE_ADDR));
	iotable_init(mx53_io_desc, ARRAY_SIZE(mx53_io_desc));
}

int imx51_register_gpios(void);

void __init mx51_init_irq(void)
{
	unsigned long tzic_addr;
	void __iomem *tzic_virt;

	if (mx51_revision() < IMX_CHIP_REVISION_2_0)
		tzic_addr = MX51_TZIC_BASE_ADDR_TO1;
	else
		tzic_addr = MX51_TZIC_BASE_ADDR;

	tzic_virt = ioremap(tzic_addr, SZ_16K);
	if (!tzic_virt)
		panic("unable to map TZIC interrupt controller\n");

	tzic_init_irq(tzic_virt);
	imx51_register_gpios();
}

int imx53_register_gpios(void);

void __init mx53_init_irq(void)
{
	unsigned long tzic_addr;
	void __iomem *tzic_virt;

	tzic_addr = MX53_TZIC_BASE_ADDR;

	tzic_virt = ioremap(tzic_addr, SZ_16K);
	if (!tzic_virt)
		panic("unable to map TZIC interrupt controller\n");

	tzic_init_irq(tzic_virt);
	imx53_register_gpios();
}
