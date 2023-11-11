/*
 * fp_cond.S
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

#include "fp_emu.h"
#include "fp_decode.h"

	.globl	fp_fscc, fp_fbccw, fp_fbccl

#ifdef FPU_EMU_DEBUG
fp_fnop:
	printf	PDECODE,"fnop\n"
	jra	fp_end
#else
#define fp_fnop fp_end
#endif

fp_fbccw:
	tst.w	%d2
	jeq	fp_fnop
	printf	PDECODE,"fbccw "
	fp_get_pc %a0
	lea	(-2,%a0,%d2.w),%a0
	jra	1f

fp_fbccl:
	printf	PDECODE,"fbccl "
	fp_get_pc %a0
	move.l	%d2,%d0
	swap	%d0
	fp_get_instr_word %d0,fp_err_ua1
	lea	(-2,%a0,%d0.l),%a0
1:	printf	PDECODE,"%x",1,%a0
	move.l	%d2,%d0
	swap	%d0
	jsr	fp_compute_cond
	tst.l	%d0
	jeq	1f
	fp_put_pc %a0,1
1:	printf	PDECODE,"\n"
	jra	fp_end

fp_fdbcc:
	printf	PDECODE,"fdbcc "
	fp_get_pc %a1				| calculate new pc
	fp_get_instr_word %d0,fp_err_ua1
	add.w	%d0,%a1
	fp_decode_addr_reg
	printf	PDECODE,"d%d,%x\n",2,%d0,%a1
	swap	%d1				| test condition in %d1
	tst.w	%d1
	jne	2f
	move.l	%d0,%d1
	jsr	fp_get_data_reg
	subq.w	#1,%d0
	jcs	1f
	fp_put_pc %a1,1
1:	jsr	fp_put_data_reg
2:	jra	fp_end

| set flags for decode macros for fs<cc>
do_fscc=1
do_no_pc_mode=1

fp_fscc:
	printf	PDECODE,"fscc "
	move.l	%d2,%d0
	jsr	fp_compute_cond
	move.w	%d0,%d1
	swap	%d1

	| decode addressing mode
	fp_decode_addr_mode

	.long	fp_data, fp_fdbcc
	.long	fp_indirect, fp_postinc
	.long	fp_predecr, fp_disp16
	.long	fp_extmode0, fp_extmode1

	| addressing mode: data register direct
fp_data:
	fp_mode_data_direct
	move.w	%d0,%d1			| save register nr
	jsr	fp_get_data_reg
	swap	%d1
	move.b	%d1,%d0
	swap	%d1
	jsr	fp_put_data_reg
	printf	PDECODE,"\n"
	jra	fp_end

fp_indirect:
	fp_mode_addr_indirect
	jra	fp_do_scc

fp_postinc:
	fp_mode_addr_indirect_postinc
	jra	fp_do_scc

fp_predecr:
	fp_mode_addr_indirect_predec
	jra	fp_do_scc

fp_disp16:
	fp_mode_addr_indirect_disp16
	jra	fp_do_scc

fp_extmode0:
	fp_mode_addr_indirect_extmode0
	jra	fp_do_scc

fp_extmode1:
	bfextu	%d2{#13,#3},%d0
	jmp	([0f:w,%pc,%d0*4])

	.align	4
0:
	.long	fp_absolute_short, fp_absolute_long
	.long	fp_ill, fp_ill		| NOTE: jump here to ftrap.x
	.long	fp_ill, fp_ill
	.long	fp_ill, fp_ill

fp_absolute_short:
	fp_mode_abs_short
	jra	fp_do_scc

fp_absolute_long:
	fp_mode_abs_long
|	jra	fp_do_scc

fp_do_scc:
	swap	%d1
	putuser.b %d1,(%a0),fp_err_ua1,%a0
	printf	PDECODE,"\n"
	jra	fp_end


#define tst_NAN	btst #24,%d1
#define tst_Z	btst #26,%d1
#define tst_N	btst #27,%d1

fp_compute_cond:
	move.l	(FPD_FPSR,FPDATA),%d1
	btst	#4,%d0
	jeq	1f
	tst_NAN
	jeq	1f
	bset	#15,%d1
	bset	#7,%d1
	move.l	%d1,(FPD_FPSR,FPDATA)
1:	and.w	#0xf,%d0
	jmp	([0f:w,%pc,%d0.w*4])

	.align	4
0:
	.long	fp_f  , fp_eq , fp_ogt, fp_oge
	.long	fp_olt, fp_ole, fp_ogl, fp_or
	.long	fp_un , fp_ueq, fp_ugt, fp_uge
	.long	fp_ult, fp_ule, fp_ne , fp_t

fp_f:
	moveq	#0,%d0
	rts

fp_eq:
	moveq	#0,%d0
	tst_Z
	jeq	1f
	moveq	#-1,%d0
1:	rts

fp_ogt:
	moveq	#0,%d0
	tst_NAN
	jne	1f
	tst_Z
	jne	1f
	tst_N
	jne	1f
	moveq	#-1,%d0
1:	rts

fp_oge:
	moveq	#-1,%d0
	tst_Z
	jne	2f
	tst_NAN
	jne	1f
	tst_N
	jeq	2f
1:	moveq	#0,%d0
2:	rts

fp_olt:
	moveq	#0,%d0
	tst_NAN
	jne	1f
	tst_Z
	jne	1f
	tst_N
	jeq	1f
	moveq	#-1,%d0
1:	rts

fp_ole:
	moveq	#-1,%d0
	tst_Z
	jne	2f
	tst_NAN
	jne	1f
	tst_N
	jne	2f
1:	moveq	#0,%d0
2:	rts

fp_ogl:
	moveq	#0,%d0
	tst_NAN
	jne	1f
	tst_Z
	jne	1f
	moveq	#-1,%d0
1:	rts

fp_or:
	moveq	#0,%d0
	tst_NAN
	jne	1f
	moveq	#-1,%d0
1:	rts

fp_un:
	moveq	#0,%d0
	tst_NAN
	jeq	1f
	moveq	#-1,%d0
	rts

fp_ueq:
	moveq	#-1,%d0
	tst_NAN
	jne	1f
	tst_Z
	jne	1f
	moveq	#0,%d0
1:	rts

fp_ugt:
	moveq	#-1,%d0
	tst_NAN
	jne	2f
	tst_N
	jne	1f
	tst_Z
	jeq	2f
1:	moveq	#0,%d0
2:	rts

fp_uge:
	moveq	#-1,%d0
	tst_NAN
	jne	1f
	tst_Z
	jne	1f
	tst_N
	jeq	1f
	moveq	#0,%d0
1:	rts

fp_ult:
	moveq	#-1,%d0
	tst_NAN
	jne	2f
	tst_Z
	jne	1f
	tst_N
	jne	2f
1:	moveq	#0,%d0
2:	rts

fp_ule:
	moveq	#-1,%d0
	tst_NAN
	jne	1f
	tst_Z
	jne	1f
	tst_N
	jne	1f
	moveq	#0,%d0
1:	rts

fp_ne:
	moveq	#0,%d0
	tst_Z
	jne	1f
	moveq	#-1,%d0
1:	rts

fp_t:
	moveq	#-1,%d0
	rts
