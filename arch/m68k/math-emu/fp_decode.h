/*
 * fp_decode.h
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

#ifndef _FP_DECODE_H
#define _FP_DECODE_H

/* These macros do the dirty work of the instr decoding, several variables
 * can be defined in the source file to modify the work of these macros,
 * currently the following variables are used:
 * ...
 * The register usage:
 * d0 - will contain source operand for data direct mode,
 *	otherwise scratch register
 * d1 - upper 16bit are reserved for caller
 *	lower 16bit may contain further arguments,
 *	is destroyed during decoding
 * d2 - contains first two instruction words,
 *	first word will be used for extension word
 * a0 - will point to source/dest operand for any indirect mode
 *	otherwise scratch register
 * a1 - scratch register
 * a2 - base addr to the task structure
 *
 * the current implementation doesn't check for every disallowed
 * addressing mode (e.g. pc relative modes as destination), as long
 * as it only means a new addressing mode, which should not appear
 * in a program and that doesn't crash the emulation, I think it's
 * not a problem to allow these modes.
 */

do_fmovem=0
do_fmovem_cr=0
do_no_pc_mode=0
do_fscc=0

| first decoding of the instr type
| this separates the conditional instr
.macro	fp_decode_cond_instr_type
	bfextu	%d2{#8,#2},%d0
	jmp	([0f:w,%pc,%d0*4])

	.align	4
0:
|	.long	"f<op>","fscc/fdbcc"
|	.long	"fbccw","fbccl"
.endm

| second decoding of the instr type
| this separates most move instr
.macro	fp_decode_move_instr_type
	bfextu	%d2{#16,#3},%d0
	jmp	([0f:w,%pc,%d0*4])

	.align	4
0:
|	.long	"f<op> fpx,fpx","invalid instr"
|	.long	"f<op> <ea>,fpx","fmove fpx,<ea>"
|	.long	"fmovem <ea>,fpcr","fmovem <ea>,fpx"
|	.long	"fmovem fpcr,<ea>","fmovem fpx,<ea>"
.endm

| extract the source specifier, specifies
| either source fp register or data format
.macro	fp_decode_sourcespec
	bfextu	%d2{#19,#3},%d0
.endm

| decode destination format for fmove reg,ea
.macro	fp_decode_dest_format
	bfextu	%d2{#19,#3},%d0
.endm

| decode source register for fmove reg,ea
.macro	fp_decode_src_reg
	bfextu	%d2{#22,#3},%d0
.endm

| extract the addressing mode
| it depends on the instr which of the modes is valid
.macro	fp_decode_addr_mode
	bfextu	%d2{#10,#3},%d0
	jmp	([0f:w,%pc,%d0*4])

	.align	4
0:
|	.long	"data register direct","addr register direct"
|	.long	"addr register indirect"
|	.long	"addr register indirect postincrement"
|	.long	"addr register indirect predecrement"
|	.long	"addr register + index16"
|	.long	"extension mode1","extension mode2"
.endm

| extract the register for the addressing mode
.macro	fp_decode_addr_reg
	bfextu	%d2{#13,#3},%d0
.endm

| decode the 8bit displacement from the brief extension word
.macro	fp_decode_disp8
	move.b	%d2,%d0
	ext.w	%d0
.endm

| decode the index of the brief/full extension word
.macro	fp_decode_index
	bfextu	%d2{#17,#3},%d0		| get the register nr
	btst	#15,%d2			| test for data/addr register
	jne	1\@f
	printf	PDECODE,"d%d",1,%d0
	jsr	fp_get_data_reg
	jra	2\@f
1\@:	printf	PDECODE,"a%d",1,%d0
	jsr	fp_get_addr_reg
	move.l	%a0,%d0
2\@:
debug	lea	"'l'.w,%a0"
	btst	#11,%d2			| 16/32 bit size?
	jne	3\@f
debug	lea	"'w'.w,%a0"
	ext.l	%d0
3\@:	printf	PDECODE,":%c",1,%a0
	move.w	%d2,%d1			| scale factor
	rol.w	#7,%d1
	and.w	#3,%d1
debug	move.l	"%d1,-(%sp)"
debug	ext.l	"%d1"
	printf	PDECODE,":%d",1,%d1
debug	move.l	"(%sp)+,%d1"
	lsl.l	%d1,%d0
.endm

| decode the base displacement size
.macro	fp_decode_basedisp
	bfextu	%d2{#26,#2},%d0
	jmp	([0f:w,%pc,%d0*4])

	.align	4
0:
|	.long	"reserved","null displacement"
|	.long	"word displacement","long displacement"
.endm

.macro	fp_decode_outerdisp
	bfextu	%d2{#30,#2},%d0
	jmp	([0f:w,%pc,%d0*4])

	.align	4
0:
|	.long	"no memory indirect action/reserved","null outer displacement"
|	.long	"word outer displacement","long outer displacement"
.endm

| get the extension word and test for brief or full extension type
.macro	fp_get_test_extword label
	fp_get_instr_word %d2,fp_err_ua1
	btst	#8,%d2
	jne	\label
.endm


| test if %pc is the base register for the indirect addr mode
.macro	fp_test_basereg_d16	label
	btst	#20,%d2
	jeq	\label
.endm

| test if %pc is the base register for one of the extended modes
.macro	fp_test_basereg_ext	label
	btst	#19,%d2
	jeq	\label
.endm

.macro	fp_test_suppr_index label
	btst	#6,%d2
	jne	\label
.endm


| addressing mode: data register direct
.macro	fp_mode_data_direct
	fp_decode_addr_reg
	printf	PDECODE,"d%d",1,%d0
.endm

| addressing mode: address register indirect
.macro	fp_mode_addr_indirect
	fp_decode_addr_reg
	printf	PDECODE,"(a%d)",1,%d0
	jsr	fp_get_addr_reg
.endm

| adjust stack for byte moves from/to stack
.macro	fp_test_sp_byte_move
	.if	!do_fmovem
	.if	do_fscc
	move.w	#6,%d1
	.endif
	cmp.w	#7,%d0
	jne	1\@f
	.if	!do_fscc
	cmp.w	#6,%d1
	jne	1\@f
	.endif
	move.w	#4,%d1
1\@:
	.endif
.endm

| addressing mode: address register indirect with postincrement
.macro	fp_mode_addr_indirect_postinc
	fp_decode_addr_reg
	printf	PDECODE,"(a%d)+",1,%d0
	fp_test_sp_byte_move
	jsr	fp_get_addr_reg
	move.l	%a0,%a1			| save addr
	.if	do_fmovem
	lea	(%a0,%d1.w*4),%a0
	.if	!do_fmovem_cr
	lea	(%a0,%d1.w*8),%a0
	.endif
	.else
	add.w	(fp_datasize,%d1.w*2),%a0
	.endif
	jsr	fp_put_addr_reg
	move.l	%a1,%a0
.endm

| addressing mode: address register indirect with predecrement
.macro	fp_mode_addr_indirect_predec
	fp_decode_addr_reg
	printf	PDECODE,"-(a%d)",1,%d0
	fp_test_sp_byte_move
	jsr	fp_get_addr_reg
	.if	do_fmovem
	.if	!do_fmovem_cr
	lea	(-12,%a0),%a1		| setup to addr of 1st reg to move
	neg.w	%d1
	lea	(%a0,%d1.w*4),%a0
	add.w	%d1,%d1
	lea	(%a0,%d1.w*4),%a0
	jsr	fp_put_addr_reg
	move.l	%a1,%a0
	.else
	neg.w	%d1
	lea	(%a0,%d1.w*4),%a0
	jsr	fp_put_addr_reg
	.endif
	.else
	sub.w	(fp_datasize,%d1.w*2),%a0
	jsr	fp_put_addr_reg
	.endif
.endm

| addressing mode: address register/programm counter indirect
|		   with 16bit displacement
.macro	fp_mode_addr_indirect_disp16
	.if	!do_no_pc_mode
	fp_test_basereg_d16 1f
	printf	PDECODE,"pc"
	fp_get_pc %a0
	jra	2f
	.endif
1:	fp_decode_addr_reg
	printf	PDECODE,"a%d",1,%d0
	jsr	fp_get_addr_reg
2:	fp_get_instr_word %a1,fp_err_ua1
	printf	PDECODE,"@(%x)",1,%a1
	add.l	%a1,%a0
.endm

| perform preindex (if I/IS == 0xx and xx != 00)
.macro	fp_do_preindex
	moveq	#3,%d0
	and.w	%d2,%d0
	jeq	1f
	btst	#2,%d2
	jne	1f
	printf	PDECODE,")@("
	getuser.l (%a1),%a1,fp_err_ua1,%a1
debug	jra	"2f"
1:	printf	PDECODE,","
2:
.endm

| perform postindex (if I/IS == 1xx)
.macro	fp_do_postindex
	btst	#2,%d2
	jeq	1f
	printf	PDECODE,")@("
	getuser.l (%a1),%a1,fp_err_ua1,%a1
debug	jra	"2f"
1:	printf	PDECODE,","
2:
.endm

| all other indirect addressing modes will finally end up here
.macro	fp_mode_addr_indirect_extmode0
	.if	!do_no_pc_mode
	fp_test_basereg_ext 1f
	printf	PDECODE,"pc"
	fp_get_pc %a0
	jra	2f
	.endif
1:	fp_decode_addr_reg
	printf	PDECODE,"a%d",1,%d0
	jsr	fp_get_addr_reg
2:	move.l	%a0,%a1
	swap	%d2
	fp_get_test_extword 3f
	| addressing mode: address register/programm counter indirect
	|		   with index and 8bit displacement
	fp_decode_disp8
debug	ext.l	"%d0"
	printf	PDECODE,"@(%x,",1,%d0
	add.w	%d0,%a1
	fp_decode_index
	add.l	%d0,%a1
	printf	PDECODE,")"
	jra	9f
3:	| addressing mode: address register/programm counter memory indirect
	|		   with base and/or outer displacement
	btst	#7,%d2			| base register suppressed?
	jeq	1f
	printf	PDECODE,"!"
	sub.l	%a1,%a1
1:	printf	PDECODE,"@("
	fp_decode_basedisp

	.long	fp_ill,1f
	.long	2f,3f

#ifdef FPU_EMU_DEBUG
1:	printf	PDECODE,"0"		| null base displacement
	jra	1f
#endif
2:	fp_get_instr_word %a0,fp_err_ua1 | 16bit base displacement
	printf	PDECODE,"%x:w",1,%a0
	jra	4f
3:	fp_get_instr_long %a0,fp_err_ua1 | 32bit base displacement
	printf	PDECODE,"%x:l",1,%a0
4:	add.l	%a0,%a1
1:
	fp_do_postindex
	fp_test_suppr_index 1f
	fp_decode_index
	add.l	%d0,%a1
1:	fp_do_preindex

	fp_decode_outerdisp

	.long	5f,1f
	.long	2f,3f

#ifdef FPU_EMU_DEBUG
1:	printf	PDECODE,"0"		| null outer displacement
	jra	1f
#endif
2:	fp_get_instr_word %a0,fp_err_ua1 | 16bit outer displacement
	printf	PDECODE,"%x:w",1,%a0
	jra	4f
3:	fp_get_instr_long %a0,fp_err_ua1 | 32bit outer displacement
	printf	PDECODE,"%x:l",1,%a0
4:	add.l	%a0,%a1
1:
5:	printf	PDECODE,")"
9:	move.l	%a1,%a0
	swap	%d2
.endm

| get the absolute short address from user space
.macro	fp_mode_abs_short
	fp_get_instr_word %a0,fp_err_ua1
	printf	PDECODE,"%x.w",1,%a0
.endm

| get the absolute long address from user space
.macro	fp_mode_abs_long
	fp_get_instr_long %a0,fp_err_ua1
	printf	PDECODE,"%x.l",1,%a0
.endm

#endif /* _FP_DECODE_H */
