/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Yangtao Li <frank@allwinnertech.com>
 */

#ifndef _CCU_SUN50I_A100_R_H
#define _CCU_SUN50I_A100_R_H

#include <dt-bindings/clock/sun50i-a100-r-ccu.h>
#include <dt-bindings/reset/sun50i-a100-r-ccu.h>

#define CLK_R_CPUS		0
#define CLK_R_AHB		1

/* exported except APB1 for R_PIO */

#define CLK_R_APB2		3

#define CLK_NUMBER	(CLK_R_AHB_BUS_RTC + 1)

#endif /* _CCU_SUN50I_A100_R_H */
