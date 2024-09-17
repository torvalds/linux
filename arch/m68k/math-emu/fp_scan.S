/*
 * fp_scan.S
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

	.globl	fp_scan, fp_datasize

	.data

| %d2 - first two instr words
| %d1 - operand size

/* operand formats are:

	Long = 0,		i.e. fmove.l
	Single,			i.e. fmove.s
	Extended,		i.e. fmove.x
	Packed-BCD,		i.e. fmove.p
	Word,			i.e. fmove.w
	Double,			i.e. fmove.d
*/

	.text

| On entry:
| FPDATA - base of emulated FPU registers

fp_scan:
| normal fpu instruction? (this excludes fsave/frestore)
	fp_get_pc %a0
	printf	PDECODE,"%08x: ",1,%a0
	getuser.b (%a0),%d0,fp_err_ua1,%a0
#if 1
	cmp.b	#0xf2,%d0		| cpid = 1
#else
	cmp.b	#0xfc,%d0		| cpid = 6
#endif
	jne	fp_nonstd
| first two instruction words are kept in %d2
	getuser.l (%a0)+,%d2,fp_err_ua1,%a0
	fp_put_pc %a0
fp_decode_cond:				| separate conditional instr
	fp_decode_cond_instr_type

	.long	fp_decode_move, fp_fscc
	.long	fp_fbccw, fp_fbccl

fp_decode_move:				| separate move instr
	fp_decode_move_instr_type

	.long	fp_fgen_fp, fp_ill
	.long	fp_fgen_ea, fp_fmove_fp2mem
	.long	fp_fmovem_cr, fp_fmovem_cr
	.long	fp_fmovem_fp, fp_fmovem_fp

| now all arithmetic instr and a few move instr are left
fp_fgen_fp:				| source is a fpu register
	clr.b	(FPD_FPSR+2,FPDATA)	| clear the exception byte
	fp_decode_sourcespec
	printf	PDECODE,"f<op>.x fp%d",1,%d0
	fp_get_fp_reg
	lea	(FPD_TEMPFP1,FPDATA),%a1 | copy src into a temp location
	move.l	(%a0)+,(%a1)+
	move.l	(%a0)+,(%a1)+
	move.l	(%a0),(%a1)
	lea	(-8,%a1),%a0
	jra	fp_getdest

fp_fgen_ea:				| source is <ea>
	clr.b	(FPD_FPSR+2,FPDATA)	| clear the exception byte
	| sort out fmovecr, keep data size in %d1
	fp_decode_sourcespec
	cmp.w	#7,%d0
	jeq	fp_fmovecr
	move.w	%d0,%d1			| store data size twice in %d1
	swap	%d1			| one can be trashed below
	move.w	%d0,%d1
#ifdef FPU_EMU_DEBUG
	lea	0f,%a0
	clr.l	%d0
	move.b	(%a0,%d1.w),%d0
	printf	PDECODE,"f<op>.%c ",1,%d0

	.data
0:	.byte	'l','s','x','p','w','d','b',0
	.previous
#endif

/*
	fp_getsource, fp_getdest

	basically, we end up with a pointer to the source operand in
	%a1, and a pointer to the destination operand in %a0.  both
	are, of course, 96-bit extended floating point numbers.
*/

fp_getsource:
	| decode addressing mode for source
	fp_decode_addr_mode

	.long	fp_data, fp_ill
	.long	fp_indirect, fp_postinc
	.long	fp_predecr, fp_disp16
	.long	fp_extmode0, fp_extmode1

	| addressing mode: data register direct
fp_data:
	fp_mode_data_direct
	jsr	fp_get_data_reg
	lea	(FPD_TEMPFP1,FPDATA),%a0
	jmp	([0f:w,%pc,%d1.w*4])

	.align	4
0:
	.long	fp_data_long, fp_data_single
	.long	fp_ill, fp_ill
	.long	fp_data_word, fp_ill
	.long	fp_data_byte, fp_ill

	| data types that fit in an integer data register
fp_data_byte:
	extb.l	%d0
	jra	fp_data_long

fp_data_word:
	ext.l	%d0

fp_data_long:
	jsr	fp_conv_long2ext
	jra	fp_getdest

fp_data_single:
	jsr	fp_conv_single2ext
	jra	fp_getdest

	| addressing mode: address register indirect
fp_indirect:
	fp_mode_addr_indirect
	jra	fp_fetchsource

	| addressing mode: address register indirect with postincrement
fp_postinc:
	fp_mode_addr_indirect_postinc
	jra	fp_fetchsource

	| addressing mode: address register indirect with predecrement
fp_predecr:
	fp_mode_addr_indirect_predec
	jra	fp_fetchsource

	| addressing mode: address register/programm counter indirect
	|		   with 16bit displacement
fp_disp16:
	fp_mode_addr_indirect_disp16
	jra	fp_fetchsource

	| all other indirect addressing modes will finally end up here
fp_extmode0:
	fp_mode_addr_indirect_extmode0
	jra	fp_fetchsource

| all pc relative addressing modes and immediate/absolute modes end up here
| the first ones are sent to fp_extmode0 or fp_disp16
| and only the latter are handled here
fp_extmode1:
	fp_decode_addr_reg
	jmp	([0f:w,%pc,%d0*4])

	.align	4
0:
	.long	fp_abs_short, fp_abs_long
	.long	fp_disp16, fp_extmode0
	.long	fp_immediate, fp_ill
	.long	fp_ill, fp_ill

	| addressing mode: absolute short
fp_abs_short:
	fp_mode_abs_short
	jra	fp_fetchsource

	| addressing mode: absolute long
fp_abs_long:
	fp_mode_abs_long
	jra	fp_fetchsource

	| addressing mode: immediate data
fp_immediate:
	printf	PDECODE,"#"
	fp_get_pc %a0
	move.w	(fp_datasize,%d1.w*2),%d0
	addq.w	#1,%d0
	and.w	#-2,%d0
#ifdef FPU_EMU_DEBUG
	movem.l	%d0/%d1,-(%sp)
	movel	%a0,%a1
	clr.l	%d1
	jra	2f
1:	getuser.b (%a1)+,%d1,fp_err_ua1,%a1
	printf	PDECODE,"%02x",1,%d1
2:	dbra	%d0,1b
	movem.l	(%sp)+,%d0/%d1
#endif
	lea	(%a0,%d0.w),%a1
	fp_put_pc %a1
|	jra	fp_fetchsource

fp_fetchsource:
	move.l	%a0,%a1
	swap	%d1
	lea	(FPD_TEMPFP1,FPDATA),%a0
	jmp	([0f:w,%pc,%d1.w*4])

	.align	4
0:	.long	fp_long, fp_single
	.long	fp_ext, fp_pack
	.long	fp_word, fp_double
	.long	fp_byte, fp_ill

fp_long:
	getuser.l (%a1),%d0,fp_err_ua1,%a1
	jsr	fp_conv_long2ext
	jra	fp_getdest

fp_single:
	getuser.l (%a1),%d0,fp_err_ua1,%a1
	jsr	fp_conv_single2ext
	jra	fp_getdest

fp_ext:
	getuser.l (%a1)+,%d0,fp_err_ua1,%a1
	lsr.l	#8,%d0
	lsr.l	#7,%d0
	lsr.w	#1,%d0
	move.l	%d0,(%a0)+
	getuser.l (%a1)+,%d0,fp_err_ua1,%a1
	move.l	%d0,(%a0)+
	getuser.l (%a1),%d0,fp_err_ua1,%a1
	move.l	%d0,(%a0)
	subq.l	#8,%a0
	jra	fp_getdest

fp_pack:
	/* not supported yet */
	jra	fp_ill

fp_word:
	getuser.w (%a1),%d0,fp_err_ua1,%a1
	ext.l	%d0
	jsr	fp_conv_long2ext
	jra	fp_getdest

fp_double:
	jsr	fp_conv_double2ext
	jra	fp_getdest

fp_byte:
	getuser.b (%a1),%d0,fp_err_ua1,%a1
	extb.l	%d0
	jsr	fp_conv_long2ext
|	jra	fp_getdest

fp_getdest:
	move.l	%a0,%a1
	bfextu	%d2{#22,#3},%d0
	printf	PDECODE,",fp%d\n",1,%d0
	fp_get_fp_reg
	movem.l	%a0/%a1,-(%sp)
	pea	fp_finalrounding
	bfextu	%d2{#25,#7},%d0
	jmp	([0f:w,%pc,%d0*4])

	.align	4
0:
	.long	fp_fmove_mem2fp, fp_fint, fp_fsinh, fp_fintrz
	.long	fp_fsqrt, fp_ill, fp_flognp1, fp_ill
	.long	fp_fetoxm1, fp_ftanh, fp_fatan, fp_ill
	.long	fp_fasin, fp_fatanh, fp_fsin, fp_ftan
	.long	fp_fetox, fp_ftwotox, fp_ftentox, fp_ill
	.long	fp_flogn, fp_flog10, fp_flog2, fp_ill
	.long	fp_fabs, fp_fcosh, fp_fneg, fp_ill
	.long	fp_facos, fp_fcos, fp_fgetexp, fp_fgetman
	.long	fp_fdiv, fp_fmod, fp_fadd, fp_fmul
	.long	fpa_fsgldiv, fp_frem, fp_fscale, fpa_fsglmul
	.long	fp_fsub, fp_ill, fp_ill, fp_ill
	.long	fp_ill, fp_ill, fp_ill, fp_ill
	.long	fp_fsincos0, fp_fsincos1, fp_fsincos2, fp_fsincos3
	.long	fp_fsincos4, fp_fsincos5, fp_fsincos6, fp_fsincos7
	.long	fp_fcmp, fp_ill, fp_ftst, fp_ill
	.long	fp_ill, fp_ill, fp_ill, fp_ill
	.long	fp_fsmove, fp_fssqrt, fp_ill, fp_ill
	.long	fp_fdmove, fp_fdsqrt, fp_ill, fp_ill
	.long	fp_ill, fp_ill, fp_ill, fp_ill
	.long	fp_ill, fp_ill, fp_ill, fp_ill
	.long	fp_ill, fp_ill, fp_ill, fp_ill
	.long	fp_ill, fp_ill, fp_ill, fp_ill
	.long	fp_fsabs, fp_ill, fp_fsneg, fp_ill
	.long	fp_fdabs, fp_ill, fp_fdneg, fp_ill
	.long	fp_fsdiv, fp_ill, fp_fsadd, fp_fsmul
	.long	fp_fddiv, fp_ill, fp_fdadd, fp_fdmul
	.long	fp_fssub, fp_ill, fp_ill, fp_ill
	.long	fp_fdsub, fp_ill, fp_ill, fp_ill
	.long	fp_ill, fp_ill, fp_ill, fp_ill
	.long	fp_ill, fp_ill, fp_ill, fp_ill
	.long	fp_ill, fp_ill, fp_ill, fp_ill
	.long	fp_ill, fp_ill, fp_ill, fp_ill

	| Instructions follow

	| Move an (emulated) ROM constant
fp_fmovecr:
	bfextu	%d2{#27,#5},%d0
	printf	PINSTR,"fp_fmovecr #%d",1,%d0
	move.l	%d0,%d1
	add.l	%d0,%d0
	add.l	%d1,%d0
	lea	(fp_constants,%d0*4),%a0
	move.l	#0x801cc0ff,%d0
	addq.l	#1,%d1
	lsl.l	%d1,%d0
	jcc	1f
	fp_set_sr FPSR_EXC_INEX2			| INEX2 exception
1:	moveq	#-128,%d0				| continue with fmove
	and.l	%d0,%d2
	jra	fp_getdest

	.data
	.align	4
fp_constants:
	.long	0x00004000,0xc90fdaa2,0x2168c235	| pi
	.extend	0,0,0,0,0,0,0,0,0,0
	.long	0x00003ffd,0x9a209a84,0xfbcff798	| log10(2)
	.long	0x00004000,0xadf85458,0xa2bb4a9a	| e
	.long	0x00003fff,0xb8aa3b29,0x5c17f0bc	| log2(e)
	.long	0x00003ffd,0xde5bd8a9,0x37287195	| log10(e)
	.long	0x00000000,0x00000000,0x00000000	| 0.0
	.long	0x00003ffe,0xb17217f7,0xd1cf79ac	| 1n(2)
	.long	0x00004000,0x935d8ddd,0xaaa8ac17	| 1n(10)
	| read this as "1.0 * 2^0" - note the high bit in the mantissa
	.long	0x00003fff,0x80000000,0x00000000	| 10^0
	.long	0x00004002,0xa0000000,0x00000000	| 10^1
	.long	0x00004005,0xc8000000,0x00000000	| 10^2
	.long	0x0000400c,0x9c400000,0x00000000	| 10^4
	.long	0x00004019,0xbebc2000,0x00000000	| 10^8
	.long	0x00004034,0x8e1bc9bf,0x04000000	| 10^16
	.long	0x00004069,0x9dc5ada8,0x2b70b59e	| 10^32
	.long	0x000040d3,0xc2781f49,0xffcfa6d5	| 10^64
	.long	0x000041a8,0x93ba47c9,0x80e98ce0	| 10^128
	.long	0x00004351,0xaa7eebfb,0x9df9de8e	| 10^256
	.long	0x000046a3,0xe319a0ae,0xa60e91c7	| 10^512
	.long	0x00004d48,0xc9767586,0x81750c17	| 10^1024
	.long	0x00005a92,0x9e8b3b5d,0xc53d5de5	| 10^2048
	.long	0x00007525,0xc4605202,0x8a20979b	| 10^4096
	.previous

fp_fmove_mem2fp:
	printf	PINSTR,"fmove %p,%p\n",2,%a0,%a1
	move.l	(%a1)+,(%a0)+
	move.l	(%a1)+,(%a0)+
	move.l	(%a1),(%a0)
	subq.l	#8,%a0
	rts

fpa_fsglmul:
	move.l	#fp_finalrounding_single_fast,(%sp)
	jra	fp_fsglmul

fpa_fsgldiv:
	move.l	#fp_finalrounding_single_fast,(%sp)
	jra	fp_fsgldiv

.macro	fp_dosingleprec instr
	printf	PINSTR,"single "
	move.l	#fp_finalrounding_single,(%sp)
	jra	\instr
.endm

.macro	fp_dodoubleprec instr
	printf	PINSTR,"double "
	move.l	#fp_finalrounding_double,(%sp)
	jra	\instr
.endm

fp_fsmove:
	fp_dosingleprec fp_fmove_mem2fp

fp_fssqrt:
	fp_dosingleprec fp_fsqrt

fp_fdmove:
	fp_dodoubleprec fp_fmove_mem2fp

fp_fdsqrt:
	fp_dodoubleprec fp_fsqrt

fp_fsabs:
	fp_dosingleprec fp_fabs

fp_fsneg:
	fp_dosingleprec fp_fneg

fp_fdabs:
	fp_dodoubleprec fp_fabs

fp_fdneg:
	fp_dodoubleprec fp_fneg

fp_fsdiv:
	fp_dosingleprec fp_fdiv

fp_fsadd:
	fp_dosingleprec fp_fadd

fp_fsmul:
	fp_dosingleprec fp_fmul

fp_fddiv:
	fp_dodoubleprec fp_fdiv

fp_fdadd:
	fp_dodoubleprec fp_fadd

fp_fdmul:
	fp_dodoubleprec fp_fmul

fp_fssub:
	fp_dosingleprec fp_fsub

fp_fdsub:
	fp_dodoubleprec fp_fsub

fp_nonstd:
	fp_get_pc %a0
	getuser.l (%a0),%d0,fp_err_ua1,%a0
	printf	,"nonstd ((%08x)=%08x)\n",2,%a0,%d0
	moveq	#-1,%d0
	rts

	.data
	.align	4

	| data sizes corresponding to the operand formats
fp_datasize:
	.word	4, 4, 12, 12, 2, 8, 1, 0
