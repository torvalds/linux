/*
 * linux/arch/mips/tx4938/common/handler.S
 *
 * Primary interrupt handler for tx4938 based systems
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/tx4938/rbtx4938.h>


		.align	5
		NESTED(tx4938_irq_handler, PT_SIZE, sp)
		SAVE_ALL
		CLI
		.set	at

		mfc0	t0, CP0_CAUSE
		mfc0	t1, CP0_STATUS
		and	t0, t1

		andi	t1, t0, STATUSF_IP7	/* cpu timer */
		bnez	t1, ll_ip7

		/* IP6..IP3 multiplexed -- do not use */

		andi	t1, t0, STATUSF_IP2	/* tx4938 pic */
		bnez	t1, ll_ip2

		andi	t1, t0, STATUSF_IP1	/* user line 1 */
		bnez	t1, ll_ip1

		andi	t1, t0, STATUSF_IP0	/* user line 0 */
		bnez	t1, ll_ip0

		.set	reorder

		nop
		END(tx4938_irq_handler)

		.align	5


ll_ip7:
		li	a0, TX4938_IRQ_CPU_TIMER
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq


ll_ip2:
		jal	tx4938_irq_nested
		nop
		beqz	v0, goto_spurious_interrupt
		nop
		move	a0, v0
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

goto_spurious_interrupt:
		j	ret_from_irq

ll_ip1:
		li	a0, TX4938_IRQ_USER1
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_ip0:
		li	a0, TX4938_IRQ_USER0
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq
