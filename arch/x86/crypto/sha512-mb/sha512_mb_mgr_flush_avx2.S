/*
 * Flush routine for SHA512 multibuffer
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2016 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Contact Information:
 *     Megha Dey <megha.dey@linux.intel.com>
 *
 * BSD LICENSE
 *
 * Copyright(c) 2016 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/linkage.h>
#include <asm/frame.h>
#include "sha512_mb_mgr_datastruct.S"

.extern sha512_x4_avx2

# LINUX register definitions
#define arg1    %rdi
#define arg2    %rsi

# idx needs to be other than arg1, arg2, rbx, r12
#define idx     %rdx

# Common definitions
#define state   arg1
#define job     arg2
#define len2    arg2

#define unused_lanes    %rbx
#define lane_data       %rbx
#define tmp2            %rbx

#define job_rax         %rax
#define tmp1            %rax
#define size_offset     %rax
#define tmp             %rax
#define start_offset    %rax

#define tmp3            arg1

#define extra_blocks    arg2
#define p               arg2

#define tmp4            %r8
#define lens0           %r8

#define lens1           %r9
#define lens2           %r10
#define lens3           %r11

.macro LABEL prefix n
\prefix\n\():
.endm

.macro JNE_SKIP i
jne     skip_\i
.endm

.altmacro
.macro SET_OFFSET _offset
offset = \_offset
.endm
.noaltmacro

# JOB* sha512_mb_mgr_flush_avx2(MB_MGR *state)
# arg 1 : rcx : state
ENTRY(sha512_mb_mgr_flush_avx2)
	FRAME_BEGIN
	push	%rbx

	# If bit (32+3) is set, then all lanes are empty
	mov     _unused_lanes(state), unused_lanes
        bt      $32+7, unused_lanes
        jc      return_null

        # find a lane with a non-null job
	xor     idx, idx
        offset = (_ldata + 1*_LANE_DATA_size + _job_in_lane)
        cmpq    $0, offset(state)
        cmovne  one(%rip), idx
        offset = (_ldata + 2*_LANE_DATA_size + _job_in_lane)
        cmpq    $0, offset(state)
        cmovne  two(%rip), idx
        offset = (_ldata + 3*_LANE_DATA_size + _job_in_lane)
        cmpq    $0, offset(state)
        cmovne  three(%rip), idx

        # copy idx to empty lanes
copy_lane_data:
	offset =  (_args + _data_ptr)
        mov     offset(state,idx,8), tmp

        I = 0
.rep 4
	offset =  (_ldata + I * _LANE_DATA_size + _job_in_lane)
        cmpq    $0, offset(state)
.altmacro
        JNE_SKIP %I
        offset =  (_args + _data_ptr + 8*I)
        mov     tmp, offset(state)
        offset =  (_lens + 8*I +4)
        movl    $0xFFFFFFFF, offset(state)
LABEL skip_ %I
        I = (I+1)
.noaltmacro
.endr

        # Find min length
        mov     _lens + 0*8(state),lens0
        mov     lens0,idx
        mov     _lens + 1*8(state),lens1
        cmp     idx,lens1
        cmovb   lens1,idx
        mov     _lens + 2*8(state),lens2
        cmp     idx,lens2
        cmovb   lens2,idx
        mov     _lens + 3*8(state),lens3
        cmp     idx,lens3
        cmovb   lens3,idx
        mov     idx,len2
        and     $0xF,idx
        and     $~0xFF,len2
	jz      len_is_0

        sub     len2, lens0
        sub     len2, lens1
        sub     len2, lens2
        sub     len2, lens3
        shr     $32,len2
        mov     lens0, _lens + 0*8(state)
        mov     lens1, _lens + 1*8(state)
        mov     lens2, _lens + 2*8(state)
        mov     lens3, _lens + 3*8(state)

        # "state" and "args" are the same address, arg1
        # len is arg2
        call    sha512_x4_avx2
        # state and idx are intact

len_is_0:
        # process completed job "idx"
	imul    $_LANE_DATA_size, idx, lane_data
        lea     _ldata(state, lane_data), lane_data

        mov     _job_in_lane(lane_data), job_rax
        movq    $0,  _job_in_lane(lane_data)
        movl    $STS_COMPLETED, _status(job_rax)
        mov     _unused_lanes(state), unused_lanes
        shl     $8, unused_lanes
        or      idx, unused_lanes
        mov     unused_lanes, _unused_lanes(state)

	movl    $0xFFFFFFFF, _lens+4(state,  idx, 8)

	vmovq _args_digest+0*32(state, idx, 8), %xmm0
        vpinsrq $1, _args_digest+1*32(state, idx, 8), %xmm0, %xmm0
	vmovq _args_digest+2*32(state, idx, 8), %xmm1
        vpinsrq $1, _args_digest+3*32(state, idx, 8), %xmm1, %xmm1
	vmovq _args_digest+4*32(state, idx, 8), %xmm2
        vpinsrq $1, _args_digest+5*32(state, idx, 8), %xmm2, %xmm2
	vmovq _args_digest+6*32(state, idx, 8), %xmm3
	vpinsrq $1, _args_digest+7*32(state, idx, 8), %xmm3, %xmm3

	vmovdqu %xmm0, _result_digest(job_rax)
	vmovdqu %xmm1, _result_digest+1*16(job_rax)
	vmovdqu %xmm2, _result_digest+2*16(job_rax)
	vmovdqu %xmm3, _result_digest+3*16(job_rax)

return:
	pop	%rbx
	FRAME_END
        ret

return_null:
        xor     job_rax, job_rax
        jmp     return
ENDPROC(sha512_mb_mgr_flush_avx2)
.align 16

ENTRY(sha512_mb_mgr_get_comp_job_avx2)
        push    %rbx

	mov     _unused_lanes(state), unused_lanes
        bt      $(32+7), unused_lanes
        jc      .return_null

        # Find min length
        mov     _lens(state),lens0
        mov     lens0,idx
        mov     _lens+1*8(state),lens1
        cmp     idx,lens1
        cmovb   lens1,idx
        mov     _lens+2*8(state),lens2
        cmp     idx,lens2
        cmovb   lens2,idx
        mov     _lens+3*8(state),lens3
        cmp     idx,lens3
        cmovb   lens3,idx
        test    $~0xF,idx
        jnz     .return_null
        and     $0xF,idx

        #process completed job "idx"
	imul    $_LANE_DATA_size, idx, lane_data
        lea     _ldata(state, lane_data), lane_data

        mov     _job_in_lane(lane_data), job_rax
        movq    $0,  _job_in_lane(lane_data)
        movl    $STS_COMPLETED, _status(job_rax)
        mov     _unused_lanes(state), unused_lanes
        shl     $8, unused_lanes
        or      idx, unused_lanes
        mov     unused_lanes, _unused_lanes(state)

        movl    $0xFFFFFFFF, _lens+4(state,  idx, 8)

	vmovq   _args_digest(state, idx, 8), %xmm0
        vpinsrq $1, _args_digest+1*32(state, idx, 8), %xmm0, %xmm0
	vmovq    _args_digest+2*32(state, idx, 8), %xmm1
        vpinsrq $1, _args_digest+3*32(state, idx, 8), %xmm1, %xmm1
	vmovq    _args_digest+4*32(state, idx, 8), %xmm2
        vpinsrq $1, _args_digest+5*32(state, idx, 8), %xmm2, %xmm2
        vmovq    _args_digest+6*32(state, idx, 8), %xmm3
        vpinsrq $1, _args_digest+7*32(state, idx, 8), %xmm3, %xmm3

	vmovdqu %xmm0, _result_digest+0*16(job_rax)
	vmovdqu %xmm1, _result_digest+1*16(job_rax)
	vmovdqu %xmm2, _result_digest+2*16(job_rax)
	vmovdqu %xmm3, _result_digest+3*16(job_rax)

	pop     %rbx

        ret

.return_null:
        xor     job_rax, job_rax
	pop     %rbx
        ret
ENDPROC(sha512_mb_mgr_get_comp_job_avx2)

.section	.rodata.cst8.one, "aM", @progbits, 8
.align 8
one:
.quad  1

.section	.rodata.cst8.two, "aM", @progbits, 8
.align 8
two:
.quad  2

.section	.rodata.cst8.three, "aM", @progbits, 8
.align 8
three:
.quad  3
