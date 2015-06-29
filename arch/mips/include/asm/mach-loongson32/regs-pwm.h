/*
 * Copyright (c) 2014 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * Loongson 1 PWM Register Definitions.
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_MACH_LOONGSON32_REGS_PWM_H
#define __ASM_MACH_LOONGSON32_REGS_PWM_H

/* Loongson 1 PWM Timer Register Definitions */
#define PWM_CNT			0x0
#define PWM_HRC			0x4
#define PWM_LRC			0x8
#define PWM_CTRL		0xc

/* PWM Control Register Bits */
#define CNT_RST			(0x1 << 7)
#define INT_SR			(0x1 << 6)
#define INT_EN			(0x1 << 5)
#define PWM_SINGLE		(0x1 << 4)
#define PWM_OE			(0x1 << 3)
#define CNT_EN			0x1

#endif /* __ASM_MACH_LOONGSON32_REGS_PWM_H */
