// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/arm/mach-imx/mm-imx27.c
 *
 * Copyright (C) 2008 Juergen Beisert (kernel@pengutronix.de)
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/pinctrl/machine.h>
#include <asm/mach/map.h>

#include "common.h"
#include "devices/devices-common.h"
#include "hardware.h"
#include "iomux-v1.h"

/* MX27 memory map definition */
static struct map_desc imx27_io_desc[] __initdata = {
	/*
	 * this fixed mapping covers:
	 * - AIPI1
	 * - AIPI2
	 * - AITC
	 * - ROM Patch
	 * - and some reserved space
	 */
	imx_map_entry(MX27, AIPI, MT_DEVICE),
	/*
	 * this fixed mapping covers:
	 * - CSI
	 * - ATA
	 */
	imx_map_entry(MX27, SAHB1, MT_DEVICE),
	/*
	 * this fixed mapping covers:
	 * - EMI
	 */
	imx_map_entry(MX27, X_MEMC, MT_DEVICE),
};

/*
 * Initialize the memory map. It is called during the
 * system startup to create static physical to virtual
 * memory map for the IO modules.
 */
void __init mx27_map_io(void)
{
	iotable_init(imx27_io_desc, ARRAY_SIZE(imx27_io_desc));
}

void __init imx27_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX27);
	imx_iomuxv1_init(MX27_IO_ADDRESS(MX27_GPIO_BASE_ADDR),
			MX27_NUM_GPIO_PORT);
}

void __init mx27_init_irq(void)
{
	mxc_init_irq(MX27_IO_ADDRESS(MX27_AVIC_BASE_ADDR));
}
