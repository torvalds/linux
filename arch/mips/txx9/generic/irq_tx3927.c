/*
 * Common tx3927 irq handler
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Copyright (C) 2000-2001 Toshiba Corporation
 */
#include <linux/init.h>
#include <asm/txx9irq.h>
#include <asm/txx9/tx3927.h>

void __init tx3927_irq_init(void)
{
	int i;

	txx9_irq_init(TX3927_IRC_REG);
	/* raise priority for timers, sio */
	for (i = 0; i < TX3927_NR_TMR; i++)
		txx9_irq_set_pri(TX3927_IR_TMR(i), 6);
	for (i = 0; i < TX3927_NR_SIO; i++)
		txx9_irq_set_pri(TX3927_IR_SIO(i), 7);
}
