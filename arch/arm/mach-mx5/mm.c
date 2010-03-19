/*
 * Copyright 2008-2009 Freescale Semiconductor, Inc. All Rights Reserved.
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
static struct map_desc mxc_io_desc[] __initdata = {
	{
		.virtual = MX51_IRAM_BASE_ADDR_VIRT,
		.pfn = __phys_to_pfn(MX51_IRAM_BASE_ADDR),
		.length = MX51_IRAM_SIZE,
		.type = MT_DEVICE
	}, {
		.virtual = MX51_DEBUG_BASE_ADDR_VIRT,
		.pfn = __phys_to_pfn(MX51_DEBUG_BASE_ADDR),
		.length = MX51_DEBUG_SIZE,
		.type = MT_DEVICE
	}, {
		.virtual = MX51_TZIC_BASE_ADDR_VIRT,
		.pfn = __phys_to_pfn(MX51_TZIC_BASE_ADDR),
		.length = MX51_TZIC_SIZE,
		.type = MT_DEVICE
	}, {
		.virtual = MX51_AIPS1_BASE_ADDR_VIRT,
		.pfn = __phys_to_pfn(MX51_AIPS1_BASE_ADDR),
		.length = MX51_AIPS1_SIZE,
		.type = MT_DEVICE
	}, {
		.virtual = MX51_SPBA0_BASE_ADDR_VIRT,
		.pfn = __phys_to_pfn(MX51_SPBA0_BASE_ADDR),
		.length = MX51_SPBA0_SIZE,
		.type = MT_DEVICE
	}, {
		.virtual = MX51_AIPS2_BASE_ADDR_VIRT,
		.pfn = __phys_to_pfn(MX51_AIPS2_BASE_ADDR),
		.length = MX51_AIPS2_SIZE,
		.type = MT_DEVICE
	}, {
		.virtual = MX51_NFC_AXI_BASE_ADDR_VIRT,
		.pfn = __phys_to_pfn(MX51_NFC_AXI_BASE_ADDR),
		.length = MX51_NFC_AXI_SIZE,
		.type = MT_DEVICE
	},
};

/*
 * This function initializes the memory map. It is called during the
 * system startup to create static physical to virtual memory mappings
 * for the IO modules.
 */
void __init mx51_map_io(void)
{
	u32 tzic_addr;

	if (mx51_revision() < MX51_CHIP_REV_2_0)
		tzic_addr = 0x8FFFC000;
	else
		tzic_addr = 0xE0003000;
	mxc_io_desc[2].pfn =  __phys_to_pfn(tzic_addr);

	mxc_set_cpu_type(MXC_CPU_MX51);
	mxc_iomux_v3_init(MX51_IO_ADDRESS(MX51_IOMUXC_BASE_ADDR));
	mxc_arch_reset_init(MX51_IO_ADDRESS(MX51_WDOG_BASE_ADDR));
	iotable_init(mxc_io_desc, ARRAY_SIZE(mxc_io_desc));
}

void __init mx51_init_irq(void)
{
	tzic_init_irq(MX51_IO_ADDRESS(MX51_TZIC_BASE_ADDR));
}
