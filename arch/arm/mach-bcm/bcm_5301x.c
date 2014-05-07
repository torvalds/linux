/*
 * Broadcom BCM470X / BCM5301X ARM platform code.
 *
 * Copyright 2013 Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */
#include <linux/of_platform.h>
#include <asm/hardware/cache-l2x0.h>

#include <asm/mach/arch.h>
#include <asm/siginfo.h>
#include <asm/signal.h>


static bool first_fault = true;

static int bcm5301x_abort_handler(unsigned long addr, unsigned int fsr,
				 struct pt_regs *regs)
{
	if (fsr == 0x1c06 && first_fault) {
		first_fault = false;

		/*
		 * These faults with code 0x1c06 happens for no good reason,
		 * possibly left over from the CFE boot loader.
		 */
		pr_warn("External imprecise Data abort at addr=%#lx, fsr=%#x ignored.\n",
		addr, fsr);

		/* Returning non-zero causes fault display and panic */
		return 0;
	}

	/* Others should cause a fault */
	return 1;
}

static void __init bcm5301x_init_early(void)
{
	/* Install our hook */
	hook_fault_code(16 + 6, bcm5301x_abort_handler, SIGBUS, BUS_OBJERR,
			"imprecise external abort");
}

static void __init bcm5301x_dt_init(void)
{
	l2x0_of_init(0, ~0UL);
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char __initconst *bcm5301x_dt_compat[] = {
	"brcm,bcm4708",
	NULL,
};

DT_MACHINE_START(BCM5301X, "BCM5301X")
	.init_early	= bcm5301x_init_early,
	.init_machine	= bcm5301x_dt_init,
	.dt_compat	= bcm5301x_dt_compat,
MACHINE_END
