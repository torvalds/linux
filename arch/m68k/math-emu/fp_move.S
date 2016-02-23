/*
 * fp_move.S
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

do_no_pc_mode=1

	.globl	fp_fmove_fp2mem

fp_fmove_fp2mem:
	clr.b	(2+FPD_FPSR,FPDATA)
	fp_decode_dest_format
	move.w	%d0,%d1			| store data size twice in %d1
	swap	%d1			| one can be trashed below
	move.w	%d0,%d1
#ifdef FPU_EMU_DEBUG
	lea	0f,%a0
	clr.l	%d0
	move.b	(%a0,%d1.w),%d0
	printf	PDECODE,"fmove.%c ",1,%d0
	fp_decode_src_reg
	printf	PDECODE,"fp%d,",1,%d0

	.data
0:	.byte	'l','s','x','p','w','d','b','p'
	.previous
#endif

	| encode addressing mode for dest
	fp_decode_addr_mode

	.long	fp_data, fp_ill
	.long	fp_indirect, fp_postinc
	.long	fp_predecr, fp_disp16
	.long	fp_extmode0, fp_extmode1

	| addressing mode: data register direct
fp_data:
	fp_mode_data_direct
	move.w	%d0,%d1
	fp_decode_src_reg
	fp_get_fp_reg
	lea	(FPD_TEMPFP1,FPDATA),%a1
	move.l	(%a0)+,(%a1)+
	move.l	(%a0)+,(%a1)+
	move.l	(%a0),(%a1)
	lea	(-8,%a1),%a0
	swap	%d1
	move.l	%d1,%d2
	printf	PDECODE,"\n"
	jmp	([0f:w,%pc,%d1.w*4])

	.align	4
0:
	.long	fp_data_long, fp_data_single
	.long	fp_ill, fp_ill
	.long	fp_data_word, fp_ill
	.long	fp_data_byte, fp_ill

fp_data_byte:
	jsr	fp_normalize_ext
	jsr	fp_conv_ext2byte
	move.l	%d0,%d1
	swap	%d2
	move.w	%d2,%d0
	jsr	fp_get_data_reg
	move.b	%d1,%d0
	move.w	%d2,%d1
	jsr	fp_put_data_reg
	jra	fp_final

fp_data_word:
	jsr	fp_normalize_ext
	jsr	fp_conv_ext2short
	move.l	%d0,%d1
	swap	%d2
	move.w	%d2,%d0
	jsr	fp_get_data_reg
	move.w	%d1,%d0
	move.l	%d2,%d1
	jsr	fp_put_data_reg
	jra	fp_final

fp_data_long:
	jsr	fp_normalize_ext
	jsr	fp_conv_ext2long
	swap	%d2
	move.w	%d2,%d1
	jsr	fp_put_data_reg
	jra	fp_final

fp_data_single:
	jsr	fp_normalize_ext
	jsr	fp_conv_ext2single
	swap	%d2
	move.w	%d2,%d1
	jsr	fp_put_data_reg
	jra	fp_final

	| addressing mode: address register indirect
fp_indirect:
	fp_mode_addr_indirect
	jra	fp_putdest

	| addressing mode: address register indirect with postincrement
fp_postinc:
	fp_mode_addr_indirect_postinc
	jra	fp_putdest

	| addressing mode: address register indirect with predecrement
fp_predecr:
	fp_mode_addr_indirect_predec
	jra	fp_putdest

	| addressing mode: address register indirect with 16bit displacement
fp_disp16:
	fp_mode_addr_indirect_disp16
	jra     fp_putdest

fp_extmode0:
	fp_mode_addr_indirect_extmode0
	jra	fp_putdest

fp_extmode1:
	fp_decode_addr_reg
	jmp	([0f:w,%pc,%d0*4])

	.align	4
0:
	.long	fp_abs_short, fp_abs_long
	.long	fp_ill, fp_ill
	.long	fp_ill, fp_ill
	.long	fp_ill, fp_ill

fp_abs_short:
	fp_mode_abs_short
	jra	fp_putdest

fp_abs_long:
	fp_mode_abs_long
	jra	fp_putdest

fp_putdest:
	move.l	%a0,%a1
	fp_decode_src_reg
	move.l	%d1,%d2			| save size
	fp_get_fp_reg
	printf	PDECODE,"\n"
	addq.l	#8,%a0
	move.l	(%a0),-(%sp)
	move.l	-(%a0),-(%sp)
	move.l	-(%a0),-(%sp)
	move.l	%sp,%a0
	jsr	fp_normalize_ext

	swap	%d2
	jmp	([0f:w,%pc,%d2.w*4])

	.align	4
0:
	.long	fp_format_long, fp_format_single
	.long	fp_format_extended, fp_format_packed
	.long	fp_format_word, fp_format_double
	.long	fp_format_byte, fp_format_packed

fp_format_long:
	jsr	fp_conv_ext2long
	putuser.l %d0,(%a1),fp_err_ua1,%a1
	jra	fp_finish_move

fp_format_single:
	jsr	fp_conv_ext2single
	putuser.l %d0,(%a1),fp_err_ua1,%a1
	jra	fp_finish_move

fp_format_extended:
	move.l	(%a0)+,%d0
	lsl.w	#1,%d0
	lsl.l	#7,%d0
	lsl.l	#8,%d0
	putuser.l %d0,(%a1)+,fp_err_ua1,%a1
	move.l	(%a0)+,%d0
	putuser.l %d0,(%a1)+,fp_err_ua1,%a1
	move.l	(%a0),%d0
	putuser.l %d0,(%a1),fp_err_ua1,%a1
	jra	fp_finish_move

fp_format_packed:
	/* not supported yet */
	lea	(12,%sp),%sp
	jra	fp_ill

fp_format_word:
	jsr	fp_conv_ext2short
	putuser.w %d0,(%a1),fp_err_ua1,%a1
	jra	fp_finish_move

fp_format_double:
	jsr	fp_conv_ext2double
	jra	fp_finish_move

fp_format_byte:
	jsr	fp_conv_ext2byte
	putuser.b %d0,(%a1),fp_err_ua1,%a1
|	jra	fp_finish_move

fp_finish_move:
	lea	(12,%sp),%sp
	jra	fp_final
