/*
 * Flush routine for SHA1 multibuffer
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


.extern sha1_x8_avx2

# LINUX register definitions
#define arg1    %rdi
#define arg2    %rsi

# Common definitions
#define state   arg1
#define job     arg2
#define len2    arg2

# idx must be a register not clobbered by sha1_x8_avx2
#define idx		%r8
#define DWORD_idx	%r8d

#define unused_lanes    %rbx
#define lane_data       %rbx
#define tmp2            %rbx
#define tmp2_w		%ebx

#define job_rax         %rax
#define tmp1            %rax
#define size_offset     %rax
#define tmp             %rax
#define start_offset    %rax

#define tmp3            %arg1

#define extra_blocks    %arg2
#define p               %arg2

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

# JOB* sha1_mb_mgr_flush_avx2(MB_MGR *state)
# arg 1 : rcx : state
ENTRY(sha1_mb_mgr_flush_avx2)
	FRAME_BEGIN
	push	%rbx

	# If bit (32+3) is set, then all lanes are empty
	mov     _unused_lanes(state), unused_lanes
	bt      $32+3, unused_lanes
	jc      return_null

	# find a lane with a non-null job
	xor     idx, idx
	offset = (_ldata + 1 * _LANE_DATA_size + _job_in_lane)
	cmpq    $0, offset(state)
	cmovne  one(%rip), idx
	offset = (_ldata + 2 * _LANE_DATA_size + _job_in_lane)
	cmpq    $0, offset(state)
	cmovne  two(%rip), idx
	offset = (_ldata + 3 * _LANE_DATA_size + _job_in_lane)
	cmpq    $0, offset(state)
	cmovne  three(%rip), idx
	offset = (_ldata + 4 * _LANE_DATA_size + _job_in_lane)
	cmpq    $0, offset(state)
	cmovne  four(%rip), idx
	offset = (_ldata + 5 * _LANE_DATA_size + _job_in_lane)
	cmpq    $0, offset(state)
	cmovne  five(%rip), idx
	offset = (_ldata + 6 * _LANE_DATA_size + _job_in_lane)
	cmpq    $0, offset(state)
	cmovne  six(%rip), idx
	offset = (_ldata + 7 * _LANE_DATA_size + _job_in_lane)
	cmpq    $0, offset(state)
	cmovne  seven(%rip), idx

	# copy idx to empty lanes
copy_lane_data:
	offset =  (_args + _data_ptr)
	mov     offset(state,idx,8), tmp

	I = 0
.rep 8
	offset =  (_ldata + I * _LANE_DATA_size + _job_in_lane)
	cmpq    $0, offset(state)
.altmacro
	JNE_SKIP %I
	offset =  (_args + _data_ptr + 8*I)
	mov     tmp, offset(state)
	offset =  (_lens + 4*I)
	movl    $0xFFFFFFFF, offset(state)
LABEL skip_ %I
	I = (I+1)
.noaltmacro
.endr

	# Find min length
	vmovdqa _lens+0*16(state), %xmm0
	vmovdqa _lens+1*16(state), %xmm1

	vpminud %xmm1, %xmm0, %xmm2     # xmm2 has {D,C,B,A}
	vpalignr $8, %xmm2, %xmm3, %xmm3   # xmm3 has {x,x,D,C}
	vpminud %xmm3, %xmm2, %xmm2        # xmm2 has {x,x,E,F}
	vpalignr $4, %xmm2, %xmm3, %xmm3    # xmm3 has {x,x,x,E}
	vpminud %xmm3, %xmm2, %xmm2        # xmm2 has min value in low dword

	vmovd   %xmm2, DWORD_idx
	mov	idx, len2
	and	$0xF, idx
	shr	$4, len2
	jz	len_is_0

	vpand   clear_low_nibble(%rip), %xmm2, %xmm2
	vpshufd $0, %xmm2, %xmm2

	vpsubd  %xmm2, %xmm0, %xmm0
	vpsubd  %xmm2, %xmm1, %xmm1

	vmovdqa %xmm0, _lens+0*16(state)
	vmovdqa %xmm1, _lens+1*16(state)

	# "state" and "args" are the same address, arg1
	# len is arg2
	call	sha1_x8_avx2
	# state and idx are intact


len_is_0:
	# process completed job "idx"
	imul    $_LANE_DATA_size, idx, lane_data
	lea     _ldata(state, lane_data), lane_data

	mov     _job_in_lane(lane_data), job_rax
	movq    $0, _job_in_lane(lane_data)
	movl    $STS_COMPLETED, _status(job_rax)
	mov     _unused_lanes(state), unused_lanes
	shl     $4, unused_lanes
	or      idx, unused_lanes
	mov     unused_lanes, _unused_lanes(state)

	movl	$0xFFFFFFFF, _lens(state, idx, 4)

	vmovd    _args_digest(state , idx, 4) , %xmm0
	vpinsrd  $1, _args_digest+1*32(state, idx, 4), %xmm0, %xmm0
	vpinsrd  $2, _args_digest+2*32(state, idx, 4), %xmm0, %xmm0
	vpinsrd  $3, _args_digest+3*32(state, idx, 4), %xmm0, %xmm0
	movl    _args_digest+4*32(state, idx, 4), tmp2_w

	vmovdqu  %xmm0, _result_digest(job_rax)
	offset =  (_result_digest + 1*16)
	mov     tmp2_w, offset(job_rax)

return:
	pop	%rbx
	FRAME_END
	ret

return_null:
	xor     job_rax, job_rax
	jmp     return
ENDPROC(sha1_mb_mgr_flush_avx2)


#################################################################

.align 16
ENTRY(sha1_mb_mgr_get_comp_job_avx2)
	push    %rbx

	## if bit 32+3 is set, then all lanes are empty
	mov     _unused_lanes(state), unused_lanes
	bt      $(32+3), unused_lanes
	jc      .return_null

	# Find min length
	vmovdqa _lens(state), %xmm0
	vmovdqa _lens+1*16(state), %xmm1

	vpminud %xmm1, %xmm0, %xmm2        # xmm2 has {D,C,B,A}
	vpalignr $8, %xmm2, %xmm3, %xmm3   # xmm3 has {x,x,D,C}
	vpminud %xmm3, %xmm2, %xmm2        # xmm2 has {x,x,E,F}
	vpalignr $4, %xmm2, %xmm3, %xmm3    # xmm3 has {x,x,x,E}
	vpminud %xmm3, %xmm2, %xmm2        # xmm2 has min value in low dword

	vmovd   %xmm2, DWORD_idx
	test    $~0xF, idx
	jnz     .return_null

	# process completed job "idx"
	imul    $_LANE_DATA_size, idx, lane_data
	lea     _ldata(state, lane_data), lane_data

	mov     _job_in_lane(lane_data), job_rax
	movq    $0,  _job_in_lane(lane_data)
	movl    $STS_COMPLETED, _status(job_rax)
	mov     _unused_lanes(state), unused_lanes
	shl     $4, unused_lanes
	or      idx, unused_lanes
	mov     unused_lanes, _unused_lanes(state)

	movl    $0xFFFFFFFF, _lens(state,  idx, 4)

	vmovd   _args_digest(state, idx, 4), %xmm0
	vpinsrd $1, _args_digest+1*32(state, idx, 4), %xmm0, %xmm0
	vpinsrd $2, _args_digest+2*32(state, idx, 4), %xmm0, %xmm0
	vpinsrd $3, _args_digest+3*32(state, idx, 4), %xmm0, %xmm0
	movl    _args_digest+4*32(state, idx, 4), tmp2_w

	vmovdqu %xmm0, _result_digest(job_rax)
	movl    tmp2_w, _result_digest+1*16(job_rax)

	pop     %rbx

	ret

.return_null:
	xor     job_rax, job_rax
	pop     %rbx
	ret
ENDPROC(sha1_mb_mgr_get_comp_job_avx2)

.data

.align 16
clear_low_nibble:
.octa	0x000000000000000000000000FFFFFFF0
one:
.quad  1
two:
.quad  2
three:
.quad  3
four:
.quad  4
five:
.quad  5
six:
.quad  6
seven:
.quad  7
