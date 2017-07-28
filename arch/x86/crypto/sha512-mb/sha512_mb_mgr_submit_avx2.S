/*
 * Buffer submit code for multi buffer SHA512 algorithm
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

#define arg1    %rdi
#define arg2    %rsi

#define idx             %rdx
#define last_len        %rdx

#define size_offset     %rcx
#define tmp2            %rcx

# Common definitions
#define state   arg1
#define job     arg2
#define len2    arg2
#define p2      arg2

#define p               %r11
#define start_offset    %r11

#define unused_lanes    %rbx

#define job_rax         %rax
#define len             %rax

#define lane            %r12
#define tmp3            %r12
#define lens3           %r12

#define extra_blocks    %r8
#define lens0           %r8

#define tmp             %r9
#define lens1           %r9

#define lane_data       %r10
#define lens2           %r10

#define DWORD_len %eax

# JOB* sha512_mb_mgr_submit_avx2(MB_MGR *state, JOB *job)
# arg 1 : rcx : state
# arg 2 : rdx : job
ENTRY(sha512_mb_mgr_submit_avx2)
	FRAME_BEGIN
	push	%rbx
	push	%r12

        mov     _unused_lanes(state), unused_lanes
        movzb     %bl,lane
        shr     $8, unused_lanes
        imul    $_LANE_DATA_size, lane,lane_data
        movl    $STS_BEING_PROCESSED, _status(job)
	lea     _ldata(state, lane_data), lane_data
        mov     unused_lanes, _unused_lanes(state)
        movl    _len(job),  DWORD_len

	mov     job, _job_in_lane(lane_data)
        movl    DWORD_len,_lens+4(state , lane, 8)

	# Load digest words from result_digest
	vmovdqu	_result_digest+0*16(job), %xmm0
	vmovdqu _result_digest+1*16(job), %xmm1
	vmovdqu	_result_digest+2*16(job), %xmm2
        vmovdqu	_result_digest+3*16(job), %xmm3

	vmovq    %xmm0, _args_digest(state, lane, 8)
	vpextrq  $1, %xmm0, _args_digest+1*32(state , lane, 8)
	vmovq    %xmm1, _args_digest+2*32(state , lane, 8)
	vpextrq  $1, %xmm1, _args_digest+3*32(state , lane, 8)
	vmovq    %xmm2, _args_digest+4*32(state , lane, 8)
	vpextrq  $1, %xmm2, _args_digest+5*32(state , lane, 8)
	vmovq    %xmm3, _args_digest+6*32(state , lane, 8)
	vpextrq  $1, %xmm3, _args_digest+7*32(state , lane, 8)

	mov     _buffer(job), p
	mov     p, _args_data_ptr(state, lane, 8)

	cmp     $0xFF, unused_lanes
	jne     return_null

start_loop:

	# Find min length
	mov     _lens+0*8(state),lens0
	mov     lens0,idx
	mov     _lens+1*8(state),lens1
	cmp     idx,lens1
	cmovb   lens1, idx
	mov     _lens+2*8(state),lens2
	cmp     idx,lens2
	cmovb   lens2,idx
	mov     _lens+3*8(state),lens3
	cmp     idx,lens3
	cmovb   lens3,idx
	mov     idx,len2
	and     $0xF,idx
	and     $~0xFF,len2
	jz      len_is_0

	sub     len2,lens0
	sub     len2,lens1
	sub     len2,lens2
	sub     len2,lens3
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
	mov     _unused_lanes(state), unused_lanes
	movq    $0, _job_in_lane(lane_data)
	movl    $STS_COMPLETED, _status(job_rax)
	shl     $8, unused_lanes
	or      idx, unused_lanes
	mov     unused_lanes, _unused_lanes(state)

	movl	$0xFFFFFFFF,_lens+4(state,idx,8)
	vmovq    _args_digest+0*32(state , idx, 8), %xmm0
	vpinsrq  $1, _args_digest+1*32(state , idx, 8), %xmm0, %xmm0
	vmovq    _args_digest+2*32(state , idx, 8), %xmm1
	vpinsrq  $1, _args_digest+3*32(state , idx, 8), %xmm1, %xmm1
	vmovq    _args_digest+4*32(state , idx, 8), %xmm2
	vpinsrq  $1, _args_digest+5*32(state , idx, 8), %xmm2, %xmm2
	vmovq    _args_digest+6*32(state , idx, 8), %xmm3
	vpinsrq  $1, _args_digest+7*32(state , idx, 8), %xmm3, %xmm3

	vmovdqu  %xmm0, _result_digest + 0*16(job_rax)
	vmovdqu  %xmm1, _result_digest + 1*16(job_rax)
	vmovdqu  %xmm2, _result_digest + 2*16(job_rax)
	vmovdqu  %xmm3, _result_digest + 3*16(job_rax)

return:
	pop	%r12
	pop	%rbx
	FRAME_END
	ret

return_null:
	xor     job_rax, job_rax
	jmp     return
ENDPROC(sha512_mb_mgr_submit_avx2)

/* UNUSED?
.section	.rodata.cst16, "aM", @progbits, 16
.align 16
H0:     .int  0x6a09e667
H1:     .int  0xbb67ae85
H2:     .int  0x3c6ef372
H3:     .int  0xa54ff53a
H4:     .int  0x510e527f
H5:     .int  0x9b05688c
H6:     .int  0x1f83d9ab
H7:     .int  0x5be0cd19
*/
