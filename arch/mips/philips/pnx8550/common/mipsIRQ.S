/*
 * Copyright (c) 2002 Philips, Inc. All rights.
 * Copyright (c) 2002 Red Hat, Inc. All rights.
 *
 * This software may be freely redistributed under the terms of the
 * GNU General Public License.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Based upon arch/mips/galileo-boards/ev64240/int-handler.S
 *
 */
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

/*
 * cp0_irqdispatch
 *
 *    Code to handle in-core interrupt exception.
 */

		.align	5
		.set	reorder
		.set	noat
		NESTED(cp0_irqdispatch, PT_SIZE, sp)
		SAVE_ALL
		CLI
		.set	at
		mfc0	t0,CP0_CAUSE
		mfc0	t2,CP0_STATUS

		and	t0,t2

		andi	t1,t0,STATUSF_IP2 /* int0 hardware line */
		bnez	t1,ll_hw0_irq
		nop

		andi	t1,t0,STATUSF_IP7 /* int5 hardware line */
		bnez	t1,ll_timer_irq
		nop

		/* wrong alarm or masked ... */

		j	spurious_interrupt
		nop
		END(cp0_irqdispatch)

		.align	5
		.set	reorder
ll_hw0_irq:
		li	a0,2
		move	a1,sp
		jal	hw0_irqdispatch
		nop
		j	ret_from_irq
		nop

		.align	5
		.set	reorder
ll_timer_irq:
		mfc0	t3,CP0_CONFIG,7
		andi	t4,t3,0x01c0
		beqz	t4,ll_timer_out
		nop
		li	a0,7
		move	a1,sp
		jal	timer_irqdispatch
		nop

ll_timer_out:	j	ret_from_irq
		nop
