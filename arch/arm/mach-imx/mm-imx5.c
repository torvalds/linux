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
