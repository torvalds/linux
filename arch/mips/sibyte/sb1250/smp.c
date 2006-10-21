/*
 * Copyright (C) 2001, 2002, 2003 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>

#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_int.h>

static void *mailbox_set_regs[] = {
	IOADDR(A_IMR_CPU0_BASE + R_IMR_MAILBOX_SET_CPU),
	IOADDR(A_IMR_CPU1_BASE + R_IMR_MAILBOX_SET_CPU)
};

static void *mailbox_clear_regs[] = {
	IOADDR(A_IMR_CPU0_BASE + R_IMR_MAILBOX_CLR_CPU),
	IOADDR(A_IMR_CPU1_BASE + R_IMR_MAILBOX_CLR_CPU)
};

static void *mailbox_regs[] = {
	IOADDR(A_IMR_CPU0_BASE + R_IMR_MAILBOX_CPU),
	IOADDR(A_IMR_CPU1_BASE + R_IMR_MAILBOX_CPU)
};

/*
 * SMP init and finish on secondary CPUs
 */
void sb1250_smp_init(void)
{
	unsigned int imask = STATUSF_IP4 | STATUSF_IP3 | STATUSF_IP2 |
		STATUSF_IP1 | STATUSF_IP0;

	/* Set interrupt mask, but don't enable */
	change_c0_status(ST0_IM, imask);
}

void sb1250_smp_finish(void)
{
	extern void sb1250_time_init(void);
	sb1250_time_init();
	local_irq_enable();
}

/*
 * These are routines for dealing with the sb1250 smp capabilities
 * independent of board/firmware
 */

/*
 * Simple enough; everything is set up, so just poke the appropriate mailbox
 * register, and we should be set
 */
void core_send_ipi(int cpu, unsigned int action)
{
	__raw_writeq((((u64)action) << 48), mailbox_set_regs[cpu]);
}

void sb1250_mailbox_interrupt(void)
{
	int cpu = smp_processor_id();
	unsigned int action;

	kstat_this_cpu.irqs[K_INT_MBOX_0]++;
	/* Load the mailbox register to figure out what we're supposed to do */
	action = (____raw_readq(mailbox_regs[cpu]) >> 48) & 0xffff;

	/* Clear the mailbox to clear the interrupt */
	____raw_writeq(((u64)action) << 48, mailbox_clear_regs[cpu]);

	/*
	 * Nothing to do for SMP_RESCHEDULE_YOURSELF; returning from the
	 * interrupt will do the reschedule for us
	 */

	if (action & SMP_CALL_FUNCTION)
		smp_call_function_interrupt();
}
