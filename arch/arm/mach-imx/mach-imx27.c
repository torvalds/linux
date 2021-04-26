// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2012 Sascha Hauer, Pengutronix
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/mm.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include "common.h"
#include "hardware.h"
#include "mx27.h"

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
static void __init mx27_map_io(void)
{
	iotable_init(imx27_io_desc, ARRAY_SIZE(imx27_io_desc));
}

static void __init imx27_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX27);
}

static const char * const imx27_dt_board_compat[] __initconst = {
	"fsl,imx27",
	NULL
};

DT_MACHINE_START(IMX27_DT, "Freescale i.MX27 (Device Tree Support)")
	.map_io		= mx27_map_io,
	.init_early	= imx27_init_early,
	.init_late	= imx27_pm_init,
	.dt_compat	= imx27_dt_board_compat,
MACHINE_END
