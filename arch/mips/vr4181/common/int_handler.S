/*
 * arch/mips/vr4181/common/int_handler.S
 *
 * Adapted to the VR4181 and almost entirely rewritten:
 * Copyright (C) 1999 Bradley D. LaRonde and Michael Klar
 *
 * Clean up to conform to the new IRQ
 * Copyright (C) 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>

#include <asm/vr4181/vr4181.h>

/*
 * [jsun]
 * See include/asm/vr4181/irq.h for IRQ assignment and strategy.
 */

	.text
	.set	noreorder

	.align	5
	NESTED(vr4181_handle_irq, PT_SIZE, ra)

	.set	noat
	SAVE_ALL
	CLI

	.set	at
	.set	noreorder

	mfc0	t0, CP0_CAUSE
	mfc0	t2, CP0_STATUS

	and	t0, t2

	/* we check IP3 first; it happens most frequently */
	andi	t1, t0, STATUSF_IP3
	bnez	t1, ll_cpu_ip3
	andi	t1, t0, STATUSF_IP2
	bnez	t1, ll_cpu_ip2
	andi	t1, t0, STATUSF_IP7	/* cpu timer */
	bnez	t1, ll_cputimer_irq
	andi	t1, t0, STATUSF_IP4
	bnez	t1, ll_cpu_ip4
	andi	t1, t0, STATUSF_IP5
	bnez	t1, ll_cpu_ip5
	andi	t1, t0, STATUSF_IP6
	bnez	t1, ll_cpu_ip6
	andi	t1, t0, STATUSF_IP0	/* software int 0 */
	bnez	t1, ll_cpu_ip0
	andi	t1, t0, STATUSF_IP1	/* software int 1 */
	bnez	t1, ll_cpu_ip1
	nop

	.set	reorder
do_spurious:
	j	spurious_interrupt

/*
 * regular CPU irqs
 */
ll_cputimer_irq:
	li	a0, VR4181_IRQ_TIMER
	move	a1, sp
	jal	do_IRQ
	j	ret_from_irq


ll_cpu_ip0:
	li	a0, VR4181_IRQ_SW1
	move	a1, sp
	jal	do_IRQ
	j	ret_from_irq

ll_cpu_ip1:
	li	a0, VR4181_IRQ_SW2
	move	a1, sp
	jal	do_IRQ
	j	ret_from_irq

ll_cpu_ip3:
	li	a0, VR4181_IRQ_INT1
	move	a1, sp
	jal	do_IRQ
	j	ret_from_irq

ll_cpu_ip4:
	li	a0, VR4181_IRQ_INT2
	move	a1, sp
	jal	do_IRQ
	j	ret_from_irq

ll_cpu_ip5:
	li	a0, VR4181_IRQ_INT3
	move	a1, sp
	jal	do_IRQ
	j	ret_from_irq

ll_cpu_ip6:
	li	a0, VR4181_IRQ_INT4
	move	a1, sp
	jal	do_IRQ
	j	ret_from_irq

/*
 *  One of the sys irq has happend.
 *
 *  In the interest of speed, we first determine in the following order
 *  which 16-irq block have pending interrupts:
 *	sysint1 (16 sources, including cascading intrs from GPIO)
 *	sysint2
 *	gpio (16 intr sources)
 *
 *  Then we do binary search to find the exact interrupt source.
 */
ll_cpu_ip2:

	lui	t3,%hi(VR4181_SYSINT1REG)
	lhu	t0,%lo(VR4181_SYSINT1REG)(t3)
	lhu	t2,%lo(VR4181_MSYSINT1REG)(t3)
	and	t0, 0xfffb		/* hack - remove RTC Long 1 intr */
	and	t0, t2
	beqz	t0, check_sysint2

	/* check for GPIO interrupts */
	andi	t1, t0, 0x0100
	bnez	t1, check_gpio_int

	/* so we have an interrupt in sysint1 which is not gpio int */
	li	a0, VR4181_SYS_IRQ_BASE - 1
	j	check_16

check_sysint2:

	lhu	t0,%lo(VR4181_SYSINT2REG)(t3)
	lhu	t2,%lo(VR4181_MSYSINT2REG)(t3)
	and	t0, 0xfffe		/* hack - remove RTC Long 2 intr */
	and	t0, t2
	li	a0, VR4181_SYS_IRQ_BASE + 16 - 1
	j	check_16

check_gpio_int:
	lui	t3,%hi(VR4181_GPINTMSK)
	lhu	t0,%lo(VR4181_GPINTMSK)(t3)
	lhu	t2,%lo(VR4181_GPINTSTAT)(t3)
	xori	t0, 0xffff			/* why? reverse logic? */
	and	t0, t2
	li	a0, VR4181_GPIO_IRQ_BASE - 1
	j	check_16

/*
 *  When we reach check_16, we have 16-bit status in t0 and base irq number
 *  in a0.
 */
check_16:
	andi	t1, t0, 0xff
	bnez	t1, check_8

	srl	t0, 8
	addi	a0, 8
	j	check_8

/*
 *  When we reach check_8, we have 8-bit status in t0 and base irq number
 *  in a0.
 */
check_8:
	andi	t1, t0, 0xf
	bnez	t1, check_4

	srl	t0, 4
	addi	a0, 4
	j	check_4

/*
 *  When we reach check_4, we have 4-bit status in t0 and base irq number
 *  in a0.
 */
check_4:
	andi	t0, t0, 0xf
	beqz	t0, do_spurious

loop:
	andi	t2, t0, 0x1
	srl	t0, 1
	addi	a0, 1
	beqz	t2, loop

found_it:
	move	a1, sp
	jal	do_IRQ

	j	ret_from_irq

	END(vr4181_handle_irq)
