// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/smp.h>
#include <linux/timex.h>

#include <asm/compiler.h>
#include <asm/processor.h>

void __delay(unsigned long cycles)
{
	u64 t0 = get_cycles();

	while ((unsigned long)(get_cycles() - t0) < cycles)
		cpu_relax();
}
EXPORT_SYMBOL(__delay);

/*
 * Division by multiplication: you don't have to worry about
 * loss of precision.
 *
 * Use only for very small delays ( < 1 msec).	Should probably use a
 * lookup table, really, as the multiplications take much too long with
 * short delays.  This is a "reasonable" implementation, though (and the
 * first constant multiplications gets optimized away if the delay is
 * a constant)
 */

void __udelay(unsigned long us)
{
	__delay((us * 0x000010c7ull * HZ * lpj_fine) >> 32);
}
EXPORT_SYMBOL(__udelay);

void __ndelay(unsigned long ns)
{
	__delay((ns * 0x00000005ull * HZ * lpj_fine) >> 32);
}
EXPORT_SYMBOL(__ndelay);
