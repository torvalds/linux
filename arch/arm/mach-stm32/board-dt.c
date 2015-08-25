/*
 * Copyright (C) Maxime Coquelin 2015
 * Author:  Maxime Coquelin <mcoquelin.stm32@gmail.com>
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/kernel.h>
#include <asm/v7m.h>
#include <asm/mach/arch.h>

static const char *const stm32_compat[] __initconst = {
	"st,stm32f429",
	NULL
};

DT_MACHINE_START(STM32DT, "STM32 (Device Tree Support)")
	.dt_compat = stm32_compat,
	.restart = armv7m_restart,
MACHINE_END
