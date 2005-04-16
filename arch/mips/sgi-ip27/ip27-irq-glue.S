/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

	.text
	.align	5
NESTED(ip27_irq, PT_SIZE, sp)
	SAVE_ALL
	CLI

	mfc0	s0, CP0_CAUSE
	mfc0	t0, CP0_STATUS
	and	s0, t0
	move	a0, sp
	PTR_LA	ra, ret_from_irq

	/* First check for RT interrupt.  */
	andi	t0, s0, CAUSEF_IP4
	bnez	t0, ip4
	andi	t0, s0, CAUSEF_IP2
	bnez	t0, ip2
	andi	t0, s0, CAUSEF_IP3
	bnez	t0, ip3
	andi	t0, s0, CAUSEF_IP5
	bnez	t0, ip5
	andi	t0, s0, CAUSEF_IP6
	bnez	t0, ip6
	j	ra

ip2:	j	ip27_do_irq_mask0	# PI_INT_PEND_0 or CC_PEND_{A|B}
ip3:	j	ip27_do_irq_mask1	# PI_INT_PEND_1
ip4:	j	ip27_rt_timer_interrupt
ip5:	j	ip27_prof_timer
ip6:	j	ip27_hub_error

	END(ip27_irq)
