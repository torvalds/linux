// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Support for 'mpc5200-simple-platform' compatible boards.
 *
 * Written by Marian Balakowicz <m8@semihalf.com>
 * Copyright (C) 2007 Semihalf
 *
 * Description:
 * This code implements support for a simple MPC52xx based boards which
 * do not need a custom platform specific setup. Such boards are
 * supported assuming the following:
 *
 * - GPIO pins are configured by the firmware,
 * - CDM configuration (clocking) is setup correctly by firmware,
 * - if the 'fsl,has-wdt' property is present in one of the
 *   gpt nodes, then it is safe to use such gpt to reset the board,
 * - PCI is supported if enabled in the kernel configuration
 *   and if there is a PCI bus node defined in the device tree.
 *
 * Boards that are compatible with this generic platform support
 * are listed in a 'board' table.
 */

#undef DEBUG
#include <asm/time.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/mpc52xx.h>

/*
 * Setup the architecture
 */
static void __init mpc5200_simple_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("mpc5200_simple_setup_arch()", 0);

	/* Map important registers from the internal memory map */
	mpc52xx_map_common_devices();

	/* Some mpc5200 & mpc5200b related configuration */
	mpc5200_setup_xlb_arbiter();
}

/* list of the supported boards */
static const char *board[] __initdata = {
	"anonymous,a3m071",
	"anonymous,a4m072",
	"anon,charon",
	"ifm,o2d",
	"intercontrol,digsy-mtc",
	"manroland,mucmc52",
	"manroland,uc101",
	"phytec,pcm030",
	"phytec,pcm032",
	"promess,motionpro",
	"schindler,cm5200",
	"tqc,tqm5200",
	NULL
};

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc5200_simple_probe(void)
{
	return of_device_compatible_match(of_root, board);
}

define_machine(mpc5200_simple_platform) {
	.name		= "mpc5200-simple-platform",
	.probe		= mpc5200_simple_probe,
	.setup_arch	= mpc5200_simple_setup_arch,
	.discover_phbs	= mpc52xx_setup_pci,
	.init		= mpc52xx_declare_of_platform_devices,
	.init_IRQ	= mpc52xx_init_irq,
	.get_irq	= mpc52xx_get_irq,
	.restart	= mpc52xx_restart,
	.calibrate_decr	= generic_calibrate_decr,
};
