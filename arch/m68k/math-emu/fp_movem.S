/*
 * fp_movem.S
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

| set flags for decode macros for fmovem
do_fmovem=1

	.globl	fp_fmovem_fp, fp_fmovem_cr

| %d1 contains the mask and count of the register list
| for other register usage see fp_decode.h

fp_fmovem_fp:
	printf	PDECODE,"fmovem.x "
	| get register list and count them
	btst	#11,%d2
	jne	1f
	bfextu	%d2{#24,#8},%d0		| static register list
	jra	2f
1:	bfextu	%d2{#25,#3},%d0		| dynamic register list
	jsr	fp_get_data_reg
2:	move.l	%d0,%d1
	swap	%d1
	jra	2f
1:	addq.w	#1,%d1			| count the # of registers in
2:	lsr.b	#1,%d0			| register list and keep it in %d1
	jcs	1b
	jne	2b
	printf	PDECODE,"#%08x",1,%d1
#ifdef FPU_EMU_DEBUG
	btst	#12,%d2
	jne	1f
	printf	PDECODE,"-"		| decremental move
	jra	2f
1:	printf	PDECODE,"+"		| incremental move
2:	btst	#13,%d2
	jeq	1f
	printf	PDECODE,"->"		| fpu -> cpu
	jra	2f
1:	printf	PDECODE,"<-"		| fpu <- cpu
2:
#endif

	| decode address mode
	fp_decode_addr_mode

	.long	fp_ill, fp_ill
	.long	fpr_indirect, fpr_postinc
	.long	fpr_predecr, fpr_disp16
	.long	fpr_extmode0, fpr_extmode1

	| addressing mode: address register indirect
fpr_indirect:
	fp_mode_addr_indirect
	jra	fpr_do_movem

	| addressing mode: address register indirect with postincrement
fpr_postinc:
	fp_mode_addr_indirect_postinc
	jra	fpr_do_movem

fpr_predecr:
	fp_mode_addr_indirect_predec
	jra	fpr_do_movem

	| addressing mode: address register/programm counter indirect
	|		   with 16bit displacement
fpr_disp16:
	fp_mode_addr_indirect_disp16
	jra	fpr_do_movem

fpr_extmode0:
	fp_mode_addr_indirect_extmode0
	jra	fpr_do_movem

fpr_extmode1:
	fp_decode_addr_reg
	jmp	([0f:w,%pc,%d0*4])

	.align	4
0:
	.long	fpr_absolute_short, fpr_absolute_long
	.long	fpr_disp16, fpr_extmode0
	.long	fp_ill, fp_ill
	.long	fp_ill, fp_ill

fpr_absolute_short:
	fp_mode_abs_short
	jra	fpr_do_movem

fpr_absolute_long:
	fp_mode_abs_long
|	jra	fpr_do_movem

fpr_do_movem:
	swap	%d1			| get fpu register list
	lea	(FPD_FPREG,FPDATA),%a1
	moveq	#12,%d0
	btst	#12,%d2
	jne	1f
	lea	(-12,%a1,%d0*8),%a1
	neg.l	%d0
1:	btst	#13,%d2
	jne	4f
	| move register from memory into fpu
	jra	3f
1:	printf	PMOVEM,"(%p>%p)",2,%a0,%a1
	getuser.l (%a0)+,%d2,fp_err_ua1,%a0
	lsr.l	#8,%d2
	lsr.l	#7,%d2
	lsr.w	#1,%d2
	move.l	%d2,(%a1)+
	getuser.l (%a0)+,%d2,fp_err_ua1,%a0
	move.l	%d2,(%a1)+
	getuser.l (%a0),%d2,fp_err_ua1,%a0
	move.l	%d2,(%a1)
	subq.l	#8,%a0
	subq.l	#8,%a1
	add.l	%d0,%a0
2:	add.l	%d0,%a1
3:	lsl.b	#1,%d1
	jcs	1b
	jne	2b
	jra	5f
	| move register from fpu into memory
1:	printf	PMOVEM,"(%p>%p)",2,%a1,%a0
	move.l	(%a1)+,%d2
	lsl.w	#1,%d2
	lsl.l	#7,%d2
	lsl.l	#8,%d2
	putuser.l %d2,(%a0)+,fp_err_ua1,%a0
	move.l	(%a1)+,%d2
	putuser.l %d2,(%a0)+,fp_err_ua1,%a0
	move.l	(%a1),%d2
	putuser.l %d2,(%a0),fp_err_ua1,%a0
	subq.l	#8,%a1
	subq.l	#8,%a0
	add.l	%d0,%a0
2:	add.l	%d0,%a1
4:	lsl.b	#1,%d1
	jcs	1b
	jne	2b
5:
	printf	PDECODE,"\n"
#if 0
	lea	(FPD_FPREG,FPDATA),%a0
	printf	PMOVEM,"fp:"
	printx	PMOVEM,%a0@(0)
	printx	PMOVEM,%a0@(12)
	printf	PMOVEM,"\n   "
	printx	PMOVEM,%a0@(24)
	printx	PMOVEM,%a0@(36)
	printf	PMOVEM,"\n   "
	printx	PMOVEM,%a0@(48)
	printx	PMOVEM,%a0@(60)
	printf	PMOVEM,"\n   "
	printx	PMOVEM,%a0@(72)
	printx	PMOVEM,%a0@(84)
	printf	PMOVEM,"\n"
#endif
	jra	fp_end

| set flags for decode macros for fmovem control register
do_fmovem=1
do_fmovem_cr=1

fp_fmovem_cr:
	printf	PDECODE,"fmovem.cr "
	| get register list and count them
	bfextu	%d2{#19,#3},%d0
	move.l	%d0,%d1
	swap	%d1
	jra	2f
1:	addq.w	#1,%d1
2:	lsr.l	#1,%d0
	jcs	1b
	jne	2b
	printf	PDECODE,"#%08x",1,%d1
#ifdef FPU_EMU_DEBUG
	btst	#13,%d2
	jeq	1f
	printf	PDECODE,"->"		| fpu -> cpu
	jra	2f
1:	printf	PDECODE,"<-"		| fpu <- cpu
2:
#endif

	| decode address mode
	fp_decode_addr_mode

	.long	fpc_data, fpc_addr
	.long	fpc_indirect, fpc_postinc
	.long	fpc_predecr, fpc_disp16
	.long	fpc_extmode0, fpc_extmode1

fpc_data:
	fp_mode_data_direct
	move.w	%d0,%d1
	bfffo	%d2{#19,#3},%d0
	sub.w	#19,%d0
	lea	(FPD_FPCR,FPDATA,%d0.w*4),%a1
	btst	#13,%d2
	jne	1f
	move.w	%d1,%d0
	jsr	fp_get_data_reg
	move.l	%d0,(%a1)
	jra	fpc_movem_fin
1:	move.l	(%a1),%d0
	jsr	fp_put_data_reg
	jra	fpc_movem_fin

fpc_addr:
	fp_decode_addr_reg
	printf	PDECODE,"a%d",1,%d0
	btst	#13,%d2
	jne	1f
	jsr	fp_get_addr_reg
	move.l	%a0,(FPD_FPIAR,FPDATA)
	jra	fpc_movem_fin
1:	move.l	(FPD_FPIAR,FPDATA),%a0
	jsr	fp_put_addr_reg
	jra	fpc_movem_fin

fpc_indirect:
	fp_mode_addr_indirect
	jra	fpc_do_movem

fpc_postinc:
	fp_mode_addr_indirect_postinc
	jra	fpc_do_movem

fpc_predecr:
	fp_mode_addr_indirect_predec
	jra	fpc_do_movem

fpc_disp16:
	fp_mode_addr_indirect_disp16
	jra	fpc_do_movem

fpc_extmode0:
	fp_mode_addr_indirect_extmode0
	jra	fpc_do_movem

fpc_extmode1:
	fp_decode_addr_reg
	jmp	([0f:w,%pc,%d0*4])

	.align	4
0:
	.long	fpc_absolute_short, fpc_absolute_long
	.long	fpc_disp16, fpc_extmode0
	.long	fpc_immediate, fp_ill
	.long	fp_ill, fp_ill

fpc_absolute_short:
	fp_mode_abs_short
	jra	fpc_do_movem

fpc_absolute_long:
	fp_mode_abs_long
	jra	fpc_do_movem

fpc_immediate:
	fp_get_pc %a0
	lea	(%a0,%d1.w*4),%a1
	fp_put_pc %a1
	printf	PDECODE,"#imm"
|	jra	fpc_do_movem
#if 0
	swap	%d1
	lsl.l	#5,%d1
	lea	(FPD_FPCR,FPDATA),%a0
	jra	3f
1:	move.l	%d0,(%a0)
2:	addq.l	#4,%a0
3:	lsl.b	#1,%d1
	jcs	1b
	jne	2b
	jra	fpc_movem_fin
#endif

fpc_do_movem:
	swap	%d1			| get fpu register list
	lsl.l	#5,%d1
	lea	(FPD_FPCR,FPDATA),%a1
1:	btst	#13,%d2
	jne	4f

	| move register from memory into fpu
	jra	3f
1:	printf	PMOVEM,"(%p>%p)",2,%a0,%a1
	getuser.l (%a0)+,%d0,fp_err_ua1,%a0
	move.l	%d0,(%a1)
2:	addq.l	#4,%a1
3:	lsl.b	#1,%d1
	jcs	1b
	jne	2b
	jra	fpc_movem_fin

	| move register from fpu into memory
1:	printf	PMOVEM,"(%p>%p)",2,%a1,%a0
	move.l	(%a1),%d0
	putuser.l %d0,(%a0)+,fp_err_ua1,%a0
2:	addq.l	#4,%a1
4:	lsl.b	#1,%d1
	jcs	1b
	jne	2b

fpc_movem_fin:
	and.l	#0x0000fff0,(FPD_FPCR,FPDATA)
	and.l	#0x0ffffff8,(FPD_FPSR,FPDATA)
	move.l	(FPD_FPCR,FPDATA),%d0
	lsr.l	#4,%d0
	moveq	#3,%d1
	and.l	%d0,%d1
	move.w	%d1,(FPD_RND,FPDATA)
	lsr.l	#2,%d0
	moveq	#3,%d1
	and.l	%d0,%d1
	move.w	%d1,(FPD_PREC,FPDATA)
	printf	PDECODE,"\n"
#if 0
	printf	PMOVEM,"fpcr : %08x\n",1,FPDATA@(FPD_FPCR)
	printf	PMOVEM,"fpsr : %08x\n",1,FPDATA@(FPD_FPSR)
	printf	PMOVEM,"fpiar: %08x\n",1,FPDATA@(FPD_FPIAR)
	clr.l	%d0
	move.w	(FPD_PREC,FPDATA),%d0
	printf	PMOVEM,"prec : %04x\n",1,%d0
	move.w	(FPD_RND,FPDATA),%d0
	printf	PMOVEM,"rnd  : %04x\n",1,%d0
#endif
	jra	fp_end
