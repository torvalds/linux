/*
 * Copyright 2011 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <asm/page.h>
#include <asm/sizes.h>
#include <asm/mach/map.h>
#include <mach/hardware.h>

static struct map_desc imx_lluart_desc = {
#ifdef CONFIG_DEBUG_IMX6Q_UART
	.virtual	= MX6Q_IO_P2V(MX6Q_UART4_BASE_ADDR),
	.pfn		= __phys_to_pfn(MX6Q_UART4_BASE_ADDR),
	.length		= MX6Q_UART4_SIZE,
	.type		= MT_DEVICE,
#endif
};

void __init imx_lluart_map_io(void)
{
	if (imx_lluart_desc.virtual)
		iotable_init(&imx_lluart_desc, 1);
}
