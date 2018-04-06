// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Maxime Coquelin 2015
 * Copyright (C) STMicroelectronics 2017
 * Author:  Maxime Coquelin <mcoquelin.stm32@gmail.com>
 */

#include <linux/kernel.h>
#include <asm/mach/arch.h>
#ifdef CONFIG_ARM_SINGLE_ARMV7M
#include <asm/v7m.h>
#endif

static const char *const stm32_compat[] __initconst = {
	"st,stm32f429",
	"st,stm32f469",
	"st,stm32f746",
	"st,stm32f769",
	"st,stm32h743",
	"st,stm32mp157",
	NULL
};

DT_MACHINE_START(STM32DT, "STM32 (Device Tree Support)")
	.dt_compat = stm32_compat,
#ifdef CONFIG_ARM_SINGLE_ARMV7M
	.restart = armv7m_restart,
#endif
MACHINE_END
