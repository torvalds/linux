/*
 * Buffer submit code for multi buffer SHA1 algorithm
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 *  Copyright(c) 2014 Intel Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU General Public License as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  Contact Information:
 *      James Guilford <james.guilford@intel.com>
 *	Tim Chen <tim.c.chen@linux.intel.com>
 *
 *  BSD LICENSE
 *
 *  Copyright(c) 2014 Intel Corporation.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in
 *      the documentation and/or other materials provided with the
 *      distribution.
 *    * Neither the name of Intel Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/linkage.h>
#include <asm/frame.h>
#include "sha1_mb_mgr_datastruct.S"


.extern sha1_x8_avx

# LINUX register definitions
arg1    = %rdi
arg2    = %rsi
size_offset	= %rcx
tmp2		= %rcx
extra_blocks	= %rdx

# Common definitions
#define state   arg1
#define job     %rsi
#define len2    arg2
#define p2      arg2

# idx must be a register not clobberred by sha1_x8_avx2
idx		= %r8
DWORD_idx	= %r8d
last_len	= %r8

p               = %r11
start_offset    = %r11

unused_lanes    = %rbx
BYTE_unused_lanes = %bl

job_rax         = %rax
len             = %rax
DWORD_len	= %eax

lane            = %r12
tmp3            = %r12

tmp             = %r9
DWORD_tmp	= %r9d

lane_data       = %r10

# JOB* submit_mb_mgr_submit_avx2(MB_MGR *state, job_sha1 *job)
# arg 1 : rcx : state
# arg 2 : rdx : job
ENTRY(sha1_mb_mgr_submit_avx2)
	FRAME_BEGIN
	push	%rbx
	push	%r12

	mov     _unused_lanes(state), unused_lanes
	mov	unused_lanes, lane
	and	$0xF, lane
	shr     $4, unused_lanes
	imul    $_LANE_DATA_size, lane, lane_data
	movl    $STS_BEING_PROCESSED, _status(job)
	lea     _ldata(state, lane_data), lane_data
	mov     unused_lanes, _unused_lanes(state)
	movl    _len(job),  DWORD_len

	mov	job, _job_in_lane(lane_data)
	shl	$4, len
	or	lane, len

	movl    DWORD_len,  _lens(state , lane, 4)

	# Load digest words from result_digest
	vmovdqu	_result_digest(job), %xmm0
	mov	_result_digest+1*16(job), DWORD_tmp
	vmovd    %xmm0, _args_digest(state, lane, 4)
	vpextrd  $1, %xmm0, _args_digest+1*32(state , lane, 4)
	vpextrd  $2, %xmm0, _args_digest+2*32(state , lane, 4)
	vpextrd  $3, %xmm0, _args_digest+3*32(state , lane, 4)
	movl    DWORD_tmp, _args_digest+4*32(state , lane, 4)

	mov     _buffer(job), p
	mov     p, _args_data_ptr(state, lane, 8)

	cmp     $0xF, unused_lanes
	jne     return_null

start_loop:
	# Find min length
	vmovdqa _lens(state), %xmm0
	vmovdqa _lens+1*16(state), %xmm1

	vpminud %xmm1, %xmm0, %xmm2        # xmm2 has {D,C,B,A}
	vpalignr $8, %xmm2, %xmm3, %xmm3   # xmm3 has {x,x,D,C}
	vpminud %xmm3, %xmm2, %xmm2        # xmm2 has {x,x,E,F}
	vpalignr $4, %xmm2, %xmm3, %xmm3   # xmm3 has {x,x,x,E}
	vpminud %xmm3, %xmm2, %xmm2        # xmm2 has min value in low dword

	vmovd   %xmm2, DWORD_idx
	mov    idx, len2
	and    $0xF, idx
	shr    $4, len2
	jz     len_is_0

	vpand   clear_low_nibble(%rip), %xmm2, %xmm2
	vpshufd $0, %xmm2, %xmm2

	vpsubd  %xmm2, %xmm0, %xmm0
	vpsubd  %xmm2, %xmm1, %xmm1

	vmovdqa %xmm0, _lens + 0*16(state)
	vmovdqa %xmm1, _lens + 1*16(state)


	# "state" and "args" are the same address, arg1
	# len is arg2
	call    sha1_x8_avx2

	# state and idx are intact

len_is_0:
	# process completed job "idx"
	imul    $_LANE_DATA_size, idx, lane_data
	lea     _ldata(state, lane_data), lane_data

	mov     _job_in_lane(lane_data), job_rax
	mov     _unused_lanes(state), unused_lanes
	movq    $0, _job_in_lane(lane_data)
	movl    $STS_COMPLETED, _status(job_rax)
	shl     $4, unused_lanes
	or      idx, unused_lanes
	mov     unused_lanes, _unused_lanes(state)

	movl	$0xFFFFFFFF, _lens(state, idx, 4)

	vmovd    _args_digest(state, idx, 4), %xmm0
	vpinsrd  $1, _args_digest+1*32(state , idx, 4), %xmm0, %xmm0
	vpinsrd  $2, _args_digest+2*32(state , idx, 4), %xmm0, %xmm0
	vpinsrd  $3, _args_digest+3*32(state , idx, 4), %xmm0, %xmm0
	movl    4*32(state, idx, 4), DWORD_tmp

	vmovdqu  %xmm0, _result_digest(job_rax)
	movl    DWORD_tmp, _result_digest+1*16(job_rax)

return:
	pop	%r12
	pop	%rbx
	FRAME_END
	ret

return_null:
	xor     job_rax, job_rax
	jmp     return

ENDPROC(sha1_mb_mgr_submit_avx2)

.data

.align 16
clear_low_nibble:
	.octa	0x000000000000000000000000FFFFFFF0
