// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>

#include <asm/v7m.h>

#include <asm/mach/arch.h>

static const char *const efm32gg_compat[] __initconst = {
	"efm32,dk3750",
	NULL
};

DT_MACHINE_START(EFM32DT, "EFM32 (Device Tree Support)")
	.dt_compat = efm32gg_compat,
	.restart = armv7m_restart,
MACHINE_END
