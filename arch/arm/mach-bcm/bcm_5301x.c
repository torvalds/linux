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

#define FSR_EXTERNAL		(1 << 12)
#define FSR_READ		(0 << 10)
#define FSR_IMPRECISE		0x0406

static const char *const bcm5301x_dt_compat[] __initconst = {
	"brcm,bcm4708",
	NULL,
};

static int bcm5301x_abort_handler(unsigned long addr, unsigned int fsr,
				  struct pt_regs *regs)
{
	/*
	 * We want to ignore aborts forwarded from the PCIe bus that are
	 * expected and shouldn't really be passed by the PCIe controller.
	 * The biggest disadvantage is the same FSR code may be reported when
	 * reading non-existing APB register and we shouldn't ignore that.
	 */
	if (fsr == (FSR_EXTERNAL | FSR_READ | FSR_IMPRECISE))
		return 0;

	return 1;
}

static void __init bcm5301x_init_early(void)
{
	hook_fault_code(16 + 6, bcm5301x_abort_handler, SIGBUS, BUS_OBJERR,
			"imprecise external abort");
}

DT_MACHINE_START(BCM5301X, "BCM5301X")
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.dt_compat	= bcm5301x_dt_compat,
	.init_early	= bcm5301x_init_early,
MACHINE_END
