/*
 * Jprobe specific operations
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) Intel Corporation, 2005
 *
 * 2005-May     Rusty Lynch <rusty.lynch@intel.com> and Anil S Keshavamurthy
 *              <anil.s.keshavamurthy@intel.com> initial implementation
 *
 * Jprobes (a.k.a. "jump probes" which is built on-top of kprobes) allow a
 * probe to be inserted into the beginning of a function call.  The fundamental
 * difference between a jprobe and a kprobe is the jprobe handler is executed
 * in the same context as the target function, while the kprobe handlers
 * are executed in interrupt context.
 *
 * For jprobes we initially gain control by placing a break point in the
 * first instruction of the targeted function.  When we catch that specific
 * break, we:
 *        * set the return address to our jprobe_inst_return() function
 *        * jump to the jprobe handler function
 *
 * Since we fixed up the return address, the jprobe handler will return to our
 * jprobe_inst_return() function, giving us control again.  At this point we
 * are back in the parents frame marker, so we do yet another call to our
 * jprobe_break() function to fix up the frame marker as it would normally
 * exist in the target function.
 *
 * Our jprobe_return function then transfers control back to kprobes.c by
 * executing a break instruction using one of our reserved numbers.  When we
 * catch that break in kprobes.c, we continue like we do for a normal kprobe
 * by single stepping the emulated instruction, and then returning execution
 * to the correct location.
 */
#include <asm/asmmacro.h>
#include <asm-ia64/break.h>

	/*
	 * void jprobe_break(void)
	 */
	.section .kprobes.text, "ax"
ENTRY(jprobe_break)
	break.m __IA64_BREAK_JPROBE
END(jprobe_break)

	/*
	 * void jprobe_inst_return(void)
	 */
GLOBAL_ENTRY(jprobe_inst_return)
	br.call.sptk.many b0=jprobe_break
END(jprobe_inst_return)

GLOBAL_ENTRY(invalidate_stacked_regs)
	movl r16=invalidate_restore_cfm
	;;
	mov b6=r16
	;;
	br.ret.sptk.many b6
	;;
invalidate_restore_cfm:
	mov r16=ar.rsc
	;;
	mov ar.rsc=r0
	;;
	loadrs
	;;
	mov ar.rsc=r16
	;;
	br.cond.sptk.many rp
END(invalidate_stacked_regs)

GLOBAL_ENTRY(flush_register_stack)
	// flush dirty regs to backing store (must be first in insn group)
	flushrs
	;;
	br.ret.sptk.many rp
END(flush_register_stack)

