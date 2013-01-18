/*
 * ARC FPGA Platform IRQ hookups
 *
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <asm/irq.h>

void __init plat_init_IRQ(void)
{
	/*
	 * SMP Hack because UART IRQ hardwired to cpu0 (boot-cpu) but if the
	 * request_irq() comes from any other CPU, the low level IRQ unamsking
	 * essential for getting Interrupts won't be enabled on cpu0, locking
	 * up the UART state machine.
	 */
#ifdef CONFIG_SMP
	arch_unmask_irq(UART0_IRQ);
#endif
}
