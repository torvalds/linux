/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2006 Ben Dooks <ben-linux@fluff.org>
 *
 * H1940 Suspend to RAM
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <mach/hardware.h>
#include <mach/map.h>

#include <mach/regs-gpio.h>

	.text
	.global	h1940_pm_return

h1940_pm_return:
	mov	r0, #S3C2410_PA_GPIO
	ldr	pc, [r0, #S3C2410_GSTATUS3 - S3C24XX_VA_GPIO]
