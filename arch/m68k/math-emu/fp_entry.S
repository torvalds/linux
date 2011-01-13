/*
 * fp_emu.S
 *
 * Copyright Roman Zippel, 1997.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU General Public License, in which case the provisions of the GPL are
 * required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/linkage.h>
#include <asm/entry.h>

#include "fp_emu.h"

	.globl	fpu_emu
	.globl	fp_debugprint
	.globl	fp_err_ua1,fp_err_ua2

	.text
fpu_emu:
	SAVE_ALL_INT
	GET_CURRENT(%d0)

#if defined(CPU_M68020_OR_M68030) && defined(CPU_M68040_OR_M68060)
        tst.l	m68k_is040or060
        jeq	1f
#endif
#if defined(CPU_M68040_OR_M68060)
	move.l	(FPS_PC2,%sp),(FPS_PC,%sp)
#endif
1:
	| emulate the instruction
	jsr	fp_scan

#if defined(CONFIG_M68060)
#if !defined(CPU_M68060_ONLY)
	btst	#3,m68k_cputype+3
	jeq	1f
#endif
	btst	#7,(FPS_SR,%sp)
	jne	fp_sendtrace060
#endif
1:
	| emulation successful?
	tst.l	%d0
	jeq	ret_from_exception

	| send some signal to program here

	jra	ret_from_exception

	| we jump here after an access error while trying to access
	| user space, we correct stackpointer and send a SIGSEGV to
	| the user process
fp_err_ua2:
	addq.l	#4,%sp
fp_err_ua1:
	addq.l	#4,%sp
	move.l	%a0,-(%sp)
	pea	LSEGV_MAPERR
	pea	LSIGSEGV
	jsr	fpemu_signal
	add.w	#12,%sp
	jra	ret_from_exception

#if defined(CONFIG_M68060)
	| send a trace signal if we are debugged
	| it does not really belong here, but...
fp_sendtrace060:
	move.l	(FPS_PC,%sp),-(%sp)
	pea	LTRAP_TRACE
	pea	LSIGTRAP
	jsr	fpemu_signal
	add.w	#12,%sp
	jra	ret_from_exception
#endif

	.globl	fp_get_data_reg, fp_put_data_reg
	.globl	fp_get_addr_reg, fp_put_addr_reg

	| Entry points to get/put a register. Some of them can be get/put
	| directly, others are on the stack, as we read/write the stack
	| directly here, these function may only be called from within
	| instruction decoding, otherwise the stack pointer is incorrect
	| and the stack gets corrupted.
fp_get_data_reg:
	jmp	([0f:w,%pc,%d0.w*4])

	.align	4
0:
	.long	fp_get_d0, fp_get_d1
	.long	fp_get_d2, fp_get_d3
	.long	fp_get_d4, fp_get_d5
	.long	fp_get_d6, fp_get_d7

fp_get_d0:
	move.l	(PT_OFF_D0+8,%sp),%d0
	printf	PREGISTER,"{d0->%08x}",1,%d0
	rts

fp_get_d1:
	move.l	(PT_OFF_D1+8,%sp),%d0
	printf	PREGISTER,"{d1->%08x}",1,%d0
	rts

fp_get_d2:
	move.l	(PT_OFF_D2+8,%sp),%d0
	printf	PREGISTER,"{d2->%08x}",1,%d0
	rts

fp_get_d3:
	move.l	%d3,%d0
	printf	PREGISTER,"{d3->%08x}",1,%d0
	rts

fp_get_d4:
	move.l	%d4,%d0
	printf	PREGISTER,"{d4->%08x}",1,%d0
	rts

fp_get_d5:
	move.l	%d5,%d0
	printf	PREGISTER,"{d5->%08x}",1,%d0
	rts

fp_get_d6:
	move.l	%d6,%d0
	printf	PREGISTER,"{d6->%08x}",1,%d0
	rts

fp_get_d7:
	move.l	%d7,%d0
	printf	PREGISTER,"{d7->%08x}",1,%d0
	rts

fp_put_data_reg:
	jmp	([0f:w,%pc,%d1.w*4])

	.align	4
0:
	.long	fp_put_d0, fp_put_d1
	.long	fp_put_d2, fp_put_d3
	.long	fp_put_d4, fp_put_d5
	.long	fp_put_d6, fp_put_d7

fp_put_d0:
	printf	PREGISTER,"{d0<-%08x}",1,%d0
	move.l	%d0,(PT_OFF_D0+8,%sp)
	rts

fp_put_d1:
	printf	PREGISTER,"{d1<-%08x}",1,%d0
	move.l	%d0,(PT_OFF_D1+8,%sp)
	rts

fp_put_d2:
	printf	PREGISTER,"{d2<-%08x}",1,%d0
	move.l	%d0,(PT_OFF_D2+8,%sp)
	rts

fp_put_d3:
	printf	PREGISTER,"{d3<-%08x}",1,%d0
|	move.l	%d0,%d3
	move.l	%d0,(PT_OFF_D3+8,%sp)
	rts

fp_put_d4:
	printf	PREGISTER,"{d4<-%08x}",1,%d0
|	move.l	%d0,%d4
	move.l	%d0,(PT_OFF_D4+8,%sp)
	rts

fp_put_d5:
	printf	PREGISTER,"{d5<-%08x}",1,%d0
|	move.l	%d0,%d5
	move.l	%d0,(PT_OFF_D5+8,%sp)
	rts

fp_put_d6:
	printf	PREGISTER,"{d6<-%08x}",1,%d0
	move.l	%d0,%d6
	rts

fp_put_d7:
	printf	PREGISTER,"{d7<-%08x}",1,%d0
	move.l	%d0,%d7
	rts

fp_get_addr_reg:
	jmp	([0f:w,%pc,%d0.w*4])

	.align	4
0:
	.long	fp_get_a0, fp_get_a1
	.long	fp_get_a2, fp_get_a3
	.long	fp_get_a4, fp_get_a5
	.long	fp_get_a6, fp_get_a7

fp_get_a0:
	move.l	(PT_OFF_A0+8,%sp),%a0
	printf	PREGISTER,"{a0->%08x}",1,%a0
	rts

fp_get_a1:
	move.l	(PT_OFF_A1+8,%sp),%a0
	printf	PREGISTER,"{a1->%08x}",1,%a0
	rts

fp_get_a2:
	move.l	(PT_OFF_A2+8,%sp),%a0
	printf	PREGISTER,"{a2->%08x}",1,%a0
	rts

fp_get_a3:
	move.l	%a3,%a0
	printf	PREGISTER,"{a3->%08x}",1,%a0
	rts

fp_get_a4:
	move.l	%a4,%a0
	printf	PREGISTER,"{a4->%08x}",1,%a0
	rts

fp_get_a5:
	move.l	%a5,%a0
	printf	PREGISTER,"{a5->%08x}",1,%a0
	rts

fp_get_a6:
	move.l	%a6,%a0
	printf	PREGISTER,"{a6->%08x}",1,%a0
	rts

fp_get_a7:
	move.l	%usp,%a0
	printf	PREGISTER,"{a7->%08x}",1,%a0
	rts

fp_put_addr_reg:
	jmp	([0f:w,%pc,%d0.w*4])

	.align	4
0:
	.long	fp_put_a0, fp_put_a1
	.long	fp_put_a2, fp_put_a3
	.long	fp_put_a4, fp_put_a5
	.long	fp_put_a6, fp_put_a7

fp_put_a0:
	printf	PREGISTER,"{a0<-%08x}",1,%a0
	move.l	%a0,(PT_OFF_A0+8,%sp)
	rts

fp_put_a1:
	printf	PREGISTER,"{a1<-%08x}",1,%a0
	move.l	%a0,(PT_OFF_A1+8,%sp)
	rts

fp_put_a2:
	printf	PREGISTER,"{a2<-%08x}",1,%a0
	move.l	%a0,(PT_OFF_A2+8,%sp)
	rts

fp_put_a3:
	printf	PREGISTER,"{a3<-%08x}",1,%a0
	move.l	%a0,%a3
	rts

fp_put_a4:
	printf	PREGISTER,"{a4<-%08x}",1,%a0
	move.l	%a0,%a4
	rts

fp_put_a5:
	printf	PREGISTER,"{a5<-%08x}",1,%a0
	move.l	%a0,%a5
	rts

fp_put_a6:
	printf	PREGISTER,"{a6<-%08x}",1,%a0
	move.l	%a0,%a6
	rts

fp_put_a7:
	printf	PREGISTER,"{a7<-%08x}",1,%a0
	move.l	%a0,%usp
	rts

	.data
	.align	4

fp_debugprint:
|	.long	PMDECODE
	.long	PMINSTR+PMDECODE+PMCONV+PMNORM
|	.long	PMCONV+PMNORM+PMINSTR
|	.long	0
