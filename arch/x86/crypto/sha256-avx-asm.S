########################################################################
# Implement fast SHA-256 with AVX1 instructions. (x86_64)
#
# Copyright (C) 2013 Intel Corporation.
#
# Authors:
#     James Guilford <james.guilford@intel.com>
#     Kirk Yap <kirk.s.yap@intel.com>
#     Tim Chen <tim.c.chen@linux.intel.com>
#
# This software is available to you under a choice of one of two
# licenses.  You may choose to be licensed under the terms of the GNU
# General Public License (GPL) Version 2, available from the file
# COPYING in the main directory of this source tree, or the
# OpenIB.org BSD license below:
#
#     Redistribution and use in source and binary forms, with or
#     without modification, are permitted provided that the following
#     conditions are met:
#
#      - Redistributions of source code must retain the above
#        copyright notice, this list of conditions and the following
#        disclaimer.
#
#      - Redistributions in binary form must reproduce the above
#        copyright notice, this list of conditions and the following
#        disclaimer in the documentation and/or other materials
#        provided with the distribution.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
########################################################################
#
# This code is described in an Intel White-Paper:
# "Fast SHA-256 Implementations on Intel Architecture Processors"
#
# To find it, surf to http://www.intel.com/p/en_US/embedded
# and search for that title.
#
########################################################################
# This code schedules 1 block at a time, with 4 lanes per block
########################################################################

#include <linux/linkage.h>

## assume buffers not aligned
#define    VMOVDQ vmovdqu

################################ Define Macros

# addm [mem], reg
# Add reg to mem using reg-mem add and store
.macro addm p1 p2
	add     \p1, \p2
	mov     \p2, \p1
.endm


.macro MY_ROR p1 p2
	shld    $(32-(\p1)), \p2, \p2
.endm

################################

# COPY_XMM_AND_BSWAP xmm, [mem], byte_flip_mask
# Load xmm with mem and byte swap each dword
.macro COPY_XMM_AND_BSWAP p1 p2 p3
	VMOVDQ \p2, \p1
	vpshufb \p3, \p1, \p1
.endm

################################

X0 = %xmm4
X1 = %xmm5
X2 = %xmm6
X3 = %xmm7

XTMP0 = %xmm0
XTMP1 = %xmm1
XTMP2 = %xmm2
XTMP3 = %xmm3
XTMP4 = %xmm8
XFER = %xmm9
XTMP5 = %xmm11

SHUF_00BA = %xmm10      # shuffle xBxA -> 00BA
SHUF_DC00 = %xmm12      # shuffle xDxC -> DC00
BYTE_FLIP_MASK = %xmm13

NUM_BLKS = %rdx   # 3rd arg
INP = %rsi        # 2nd arg
CTX = %rdi        # 1st arg

SRND = %rsi       # clobbers INP
c = %ecx
d = %r8d
e = %edx
TBL = %r12
a = %eax
b = %ebx

f = %r9d
g = %r10d
h = %r11d

y0 = %r13d
y1 = %r14d
y2 = %r15d


_INP_END_SIZE = 8
_INP_SIZE = 8
_XFER_SIZE = 16
_XMM_SAVE_SIZE = 0

_INP_END = 0
_INP            = _INP_END  + _INP_END_SIZE
_XFER           = _INP      + _INP_SIZE
_XMM_SAVE       = _XFER     + _XFER_SIZE
STACK_SIZE      = _XMM_SAVE + _XMM_SAVE_SIZE

# rotate_Xs
# Rotate values of symbols X0...X3
.macro rotate_Xs
X_ = X0
X0 = X1
X1 = X2
X2 = X3
X3 = X_
.endm

# ROTATE_ARGS
# Rotate values of symbols a...h
.macro ROTATE_ARGS
TMP_ = h
h = g
g = f
f = e
e = d
d = c
c = b
b = a
a = TMP_
.endm

.macro FOUR_ROUNDS_AND_SCHED
	## compute s0 four at a time and s1 two at a time
	## compute W[-16] + W[-7] 4 at a time

	mov     e, y0			# y0 = e
	MY_ROR  (25-11), y0             # y0 = e >> (25-11)
	mov     a, y1                   # y1 = a
	vpalignr $4, X2, X3, XTMP0      # XTMP0 = W[-7]
	MY_ROR  (22-13), y1             # y1 = a >> (22-13)
	xor     e, y0                   # y0 = e ^ (e >> (25-11))
	mov     f, y2                   # y2 = f
	MY_ROR  (11-6), y0              # y0 = (e >> (11-6)) ^ (e >> (25-6))
	xor     a, y1                   # y1 = a ^ (a >> (22-13)
	xor     g, y2                   # y2 = f^g
	vpaddd  X0, XTMP0, XTMP0        # XTMP0 = W[-7] + W[-16]
	xor     e, y0                   # y0 = e ^ (e >> (11-6)) ^ (e >> (25-6))
	and     e, y2                   # y2 = (f^g)&e
	MY_ROR  (13-2), y1              # y1 = (a >> (13-2)) ^ (a >> (22-2))
	## compute s0
	vpalignr $4, X0, X1, XTMP1      # XTMP1 = W[-15]
	xor     a, y1                   # y1 = a ^ (a >> (13-2)) ^ (a >> (22-2))
	MY_ROR  6, y0                   # y0 = S1 = (e>>6) & (e>>11) ^ (e>>25)
	xor     g, y2                   # y2 = CH = ((f^g)&e)^g
	MY_ROR  2, y1                   # y1 = S0 = (a>>2) ^ (a>>13) ^ (a>>22)
	add     y0, y2                  # y2 = S1 + CH
	add     _XFER(%rsp), y2         # y2 = k + w + S1 + CH
	mov     a, y0                   # y0 = a
	add     y2, h                   # h = h + S1 + CH + k + w
	mov     a, y2                   # y2 = a
	vpsrld  $7, XTMP1, XTMP2
	or      c, y0                   # y0 = a|c
	add     h, d                    # d = d + h + S1 + CH + k + w
	and     c, y2                   # y2 = a&c
	vpslld  $(32-7), XTMP1, XTMP3
	and     b, y0                   # y0 = (a|c)&b
	add     y1, h                   # h = h + S1 + CH + k + w + S0
	vpor    XTMP2, XTMP3, XTMP3     # XTMP1 = W[-15] MY_ROR 7
	or      y2, y0                  # y0 = MAJ = (a|c)&b)|(a&c)
	add     y0, h                   # h = h + S1 + CH + k + w + S0 + MAJ
	ROTATE_ARGS
	mov     e, y0                   # y0 = e
	mov     a, y1                   # y1 = a
	MY_ROR  (25-11), y0             # y0 = e >> (25-11)
	xor     e, y0                   # y0 = e ^ (e >> (25-11))
	mov     f, y2                   # y2 = f
	MY_ROR  (22-13), y1             # y1 = a >> (22-13)
	vpsrld  $18, XTMP1, XTMP2       #
	xor     a, y1                   # y1 = a ^ (a >> (22-13)
	MY_ROR  (11-6), y0              # y0 = (e >> (11-6)) ^ (e >> (25-6))
	xor     g, y2                   # y2 = f^g
	vpsrld  $3, XTMP1, XTMP4        # XTMP4 = W[-15] >> 3
	MY_ROR  (13-2), y1              # y1 = (a >> (13-2)) ^ (a >> (22-2))
	xor     e, y0                   # y0 = e ^ (e >> (11-6)) ^ (e >> (25-6))
	and     e, y2                   # y2 = (f^g)&e
	MY_ROR  6, y0                   # y0 = S1 = (e>>6) & (e>>11) ^ (e>>25)
	vpslld  $(32-18), XTMP1, XTMP1
	xor     a, y1                   # y1 = a ^ (a >> (13-2)) ^ (a >> (22-2))
	xor     g, y2                   # y2 = CH = ((f^g)&e)^g
	vpxor   XTMP1, XTMP3, XTMP3     #
	add     y0, y2                  # y2 = S1 + CH
	add     (1*4 + _XFER)(%rsp), y2 # y2 = k + w + S1 + CH
	MY_ROR  2, y1                   # y1 = S0 = (a>>2) ^ (a>>13) ^ (a>>22)
	vpxor   XTMP2, XTMP3, XTMP3     # XTMP1 = W[-15] MY_ROR 7 ^ W[-15] MY_ROR
	mov     a, y0                   # y0 = a
	add     y2, h                   # h = h + S1 + CH + k + w
	mov     a, y2                   # y2 = a
	vpxor   XTMP4, XTMP3, XTMP1     # XTMP1 = s0
	or      c, y0                   # y0 = a|c
	add     h, d                    # d = d + h + S1 + CH + k + w
	and     c, y2                   # y2 = a&c
	## compute low s1
	vpshufd $0b11111010, X3, XTMP2  # XTMP2 = W[-2] {BBAA}
	and     b, y0                   # y0 = (a|c)&b
	add     y1, h                   # h = h + S1 + CH + k + w + S0
	vpaddd  XTMP1, XTMP0, XTMP0     # XTMP0 = W[-16] + W[-7] + s0
	or      y2, y0                  # y0 = MAJ = (a|c)&b)|(a&c)
	add     y0, h                   # h = h + S1 + CH + k + w + S0 + MAJ
	ROTATE_ARGS
	mov     e, y0                   # y0 = e
	mov     a, y1                   # y1 = a
	MY_ROR  (25-11), y0             # y0 = e >> (25-11)
	xor     e, y0                   # y0 = e ^ (e >> (25-11))
	MY_ROR  (22-13), y1             # y1 = a >> (22-13)
	mov     f, y2                   # y2 = f
	xor     a, y1                   # y1 = a ^ (a >> (22-13)
	MY_ROR  (11-6), y0              # y0 = (e >> (11-6)) ^ (e >> (25-6))
	vpsrld  $10, XTMP2, XTMP4       # XTMP4 = W[-2] >> 10 {BBAA}
	xor     g, y2                   # y2 = f^g
	vpsrlq  $19, XTMP2, XTMP3       # XTMP3 = W[-2] MY_ROR 19 {xBxA}
	xor     e, y0                   # y0 = e ^ (e >> (11-6)) ^ (e >> (25-6))
	and     e, y2                   # y2 = (f^g)&e
	vpsrlq  $17, XTMP2, XTMP2       # XTMP2 = W[-2] MY_ROR 17 {xBxA}
	MY_ROR  (13-2), y1              # y1 = (a >> (13-2)) ^ (a >> (22-2))
	xor     a, y1                   # y1 = a ^ (a >> (13-2)) ^ (a >> (22-2))
	xor     g, y2                   # y2 = CH = ((f^g)&e)^g
	MY_ROR  6, y0                   # y0 = S1 = (e>>6) & (e>>11) ^ (e>>25)
	vpxor   XTMP3, XTMP2, XTMP2     #
	add     y0, y2                  # y2 = S1 + CH
	MY_ROR  2, y1                   # y1 = S0 = (a>>2) ^ (a>>13) ^ (a>>22)
	add     (2*4 + _XFER)(%rsp), y2 # y2 = k + w + S1 + CH
	vpxor   XTMP2, XTMP4, XTMP4     # XTMP4 = s1 {xBxA}
	mov     a, y0                   # y0 = a
	add     y2, h                   # h = h + S1 + CH + k + w
	mov     a, y2                   # y2 = a
	vpshufb SHUF_00BA, XTMP4, XTMP4 # XTMP4 = s1 {00BA}
	or      c, y0                   # y0 = a|c
	add     h, d                    # d = d + h + S1 + CH + k + w
	and     c, y2                   # y2 = a&c
	vpaddd  XTMP4, XTMP0, XTMP0     # XTMP0 = {..., ..., W[1], W[0]}
	and     b, y0                   # y0 = (a|c)&b
	add     y1, h                   # h = h + S1 + CH + k + w + S0
	## compute high s1
	vpshufd $0b01010000, XTMP0, XTMP2 # XTMP2 = W[-2] {DDCC}
	or      y2, y0                  # y0 = MAJ = (a|c)&b)|(a&c)
	add     y0, h                   # h = h + S1 + CH + k + w + S0 + MAJ
	ROTATE_ARGS
	mov     e, y0                   # y0 = e
	MY_ROR  (25-11), y0             # y0 = e >> (25-11)
	mov     a, y1                   # y1 = a
	MY_ROR  (22-13), y1             # y1 = a >> (22-13)
	xor     e, y0                   # y0 = e ^ (e >> (25-11))
	mov     f, y2                   # y2 = f
	MY_ROR  (11-6), y0              # y0 = (e >> (11-6)) ^ (e >> (25-6))
	vpsrld  $10, XTMP2, XTMP5       # XTMP5 = W[-2] >> 10 {DDCC}
	xor     a, y1                   # y1 = a ^ (a >> (22-13)
	xor     g, y2                   # y2 = f^g
	vpsrlq  $19, XTMP2, XTMP3       # XTMP3 = W[-2] MY_ROR 19 {xDxC}
	xor     e, y0                   # y0 = e ^ (e >> (11-6)) ^ (e >> (25-6))
	and     e, y2                   # y2 = (f^g)&e
	MY_ROR  (13-2), y1              # y1 = (a >> (13-2)) ^ (a >> (22-2))
	vpsrlq  $17, XTMP2, XTMP2       # XTMP2 = W[-2] MY_ROR 17 {xDxC}
	xor     a, y1                   # y1 = a ^ (a >> (13-2)) ^ (a >> (22-2))
	MY_ROR  6, y0                   # y0 = S1 = (e>>6) & (e>>11) ^ (e>>25)
	xor     g, y2                   # y2 = CH = ((f^g)&e)^g
	vpxor   XTMP3, XTMP2, XTMP2
	MY_ROR  2, y1                   # y1 = S0 = (a>>2) ^ (a>>13) ^ (a>>22)
	add     y0, y2                  # y2 = S1 + CH
	add     (3*4 + _XFER)(%rsp), y2 # y2 = k + w + S1 + CH
	vpxor   XTMP2, XTMP5, XTMP5     # XTMP5 = s1 {xDxC}
	mov     a, y0                   # y0 = a
	add     y2, h                   # h = h + S1 + CH + k + w
	mov     a, y2                   # y2 = a
	vpshufb SHUF_DC00, XTMP5, XTMP5 # XTMP5 = s1 {DC00}
	or      c, y0                   # y0 = a|c
	add     h, d                    # d = d + h + S1 + CH + k + w
	and     c, y2                   # y2 = a&c
	vpaddd  XTMP0, XTMP5, X0        # X0 = {W[3], W[2], W[1], W[0]}
	and     b, y0                   # y0 = (a|c)&b
	add     y1, h                   # h = h + S1 + CH + k + w + S0
	or      y2, y0                  # y0 = MAJ = (a|c)&b)|(a&c)
	add     y0, h                   # h = h + S1 + CH + k + w + S0 + MAJ
	ROTATE_ARGS
	rotate_Xs
.endm

## input is [rsp + _XFER + %1 * 4]
.macro DO_ROUND round
	mov	e, y0			# y0 = e
        MY_ROR  (25-11), y0             # y0 = e >> (25-11)
        mov     a, y1                   # y1 = a
        xor     e, y0                   # y0 = e ^ (e >> (25-11))
        MY_ROR  (22-13), y1             # y1 = a >> (22-13)
        mov     f, y2                   # y2 = f
        xor     a, y1                   # y1 = a ^ (a >> (22-13)
        MY_ROR  (11-6), y0              # y0 = (e >> (11-6)) ^ (e >> (25-6))
        xor     g, y2                   # y2 = f^g
        xor     e, y0                   # y0 = e ^ (e >> (11-6)) ^ (e >> (25-6))
        MY_ROR  (13-2), y1              # y1 = (a >> (13-2)) ^ (a >> (22-2))
        and     e, y2                   # y2 = (f^g)&e
        xor     a, y1                   # y1 = a ^ (a >> (13-2)) ^ (a >> (22-2))
        MY_ROR  6, y0                   # y0 = S1 = (e>>6) & (e>>11) ^ (e>>25)
        xor     g, y2                   # y2 = CH = ((f^g)&e)^g
        add     y0, y2                  # y2 = S1 + CH
        MY_ROR  2, y1                   # y1 = S0 = (a>>2) ^ (a>>13) ^ (a>>22)
        offset = \round * 4 + _XFER     #
        add     offset(%rsp), y2	# y2 = k + w + S1 + CH
        mov     a, y0			# y0 = a
        add     y2, h                   # h = h + S1 + CH + k + w
        mov     a, y2                   # y2 = a
        or      c, y0                   # y0 = a|c
        add     h, d                    # d = d + h + S1 + CH + k + w
        and     c, y2                   # y2 = a&c
        and     b, y0                   # y0 = (a|c)&b
        add     y1, h                   # h = h + S1 + CH + k + w + S0
        or      y2, y0                  # y0 = MAJ = (a|c)&b)|(a&c)
        add     y0, h                   # h = h + S1 + CH + k + w + S0 + MAJ
        ROTATE_ARGS
.endm

########################################################################
## void sha256_transform_avx(state sha256_state *state, const u8 *data, int blocks)
## arg 1 : pointer to state
## arg 2 : pointer to input data
## arg 3 : Num blocks
########################################################################
.text
SYM_FUNC_START(sha256_transform_avx)
.align 32
	pushq   %rbx
	pushq   %r12
	pushq   %r13
	pushq   %r14
	pushq   %r15
	pushq	%rbp
	movq	%rsp, %rbp

	subq    $STACK_SIZE, %rsp	# allocate stack space
	and	$~15, %rsp		# align stack pointer

	shl     $6, NUM_BLKS		# convert to bytes
	jz      done_hash
	add     INP, NUM_BLKS		# pointer to end of data
	mov     NUM_BLKS, _INP_END(%rsp)

	## load initial digest
	mov     4*0(CTX), a
	mov     4*1(CTX), b
	mov     4*2(CTX), c
	mov     4*3(CTX), d
	mov     4*4(CTX), e
	mov     4*5(CTX), f
	mov     4*6(CTX), g
	mov     4*7(CTX), h

	vmovdqa  PSHUFFLE_BYTE_FLIP_MASK(%rip), BYTE_FLIP_MASK
	vmovdqa  _SHUF_00BA(%rip), SHUF_00BA
	vmovdqa  _SHUF_DC00(%rip), SHUF_DC00
loop0:
	lea     K256(%rip), TBL

	## byte swap first 16 dwords
	COPY_XMM_AND_BSWAP      X0, 0*16(INP), BYTE_FLIP_MASK
	COPY_XMM_AND_BSWAP      X1, 1*16(INP), BYTE_FLIP_MASK
	COPY_XMM_AND_BSWAP      X2, 2*16(INP), BYTE_FLIP_MASK
	COPY_XMM_AND_BSWAP      X3, 3*16(INP), BYTE_FLIP_MASK

	mov     INP, _INP(%rsp)

	## schedule 48 input dwords, by doing 3 rounds of 16 each
	mov     $3, SRND
.align 16
loop1:
	vpaddd  (TBL), X0, XFER
	vmovdqa XFER, _XFER(%rsp)
	FOUR_ROUNDS_AND_SCHED

	vpaddd  1*16(TBL), X0, XFER
	vmovdqa XFER, _XFER(%rsp)
	FOUR_ROUNDS_AND_SCHED

	vpaddd  2*16(TBL), X0, XFER
	vmovdqa XFER, _XFER(%rsp)
	FOUR_ROUNDS_AND_SCHED

	vpaddd  3*16(TBL), X0, XFER
	vmovdqa XFER, _XFER(%rsp)
	add	$4*16, TBL
	FOUR_ROUNDS_AND_SCHED

	sub     $1, SRND
	jne     loop1

	mov     $2, SRND
loop2:
	vpaddd  (TBL), X0, XFER
	vmovdqa XFER, _XFER(%rsp)
	DO_ROUND        0
	DO_ROUND        1
	DO_ROUND        2
	DO_ROUND        3

	vpaddd  1*16(TBL), X1, XFER
	vmovdqa XFER, _XFER(%rsp)
	add     $2*16, TBL
	DO_ROUND        0
	DO_ROUND        1
	DO_ROUND        2
	DO_ROUND        3

	vmovdqa X2, X0
	vmovdqa X3, X1

	sub     $1, SRND
	jne     loop2

	addm    (4*0)(CTX),a
	addm    (4*1)(CTX),b
	addm    (4*2)(CTX),c
	addm    (4*3)(CTX),d
	addm    (4*4)(CTX),e
	addm    (4*5)(CTX),f
	addm    (4*6)(CTX),g
	addm    (4*7)(CTX),h

	mov     _INP(%rsp), INP
	add     $64, INP
	cmp     _INP_END(%rsp), INP
	jne     loop0

done_hash:

	mov	%rbp, %rsp
	popq	%rbp
	popq    %r15
	popq    %r14
	popq    %r13
	popq	%r12
	popq    %rbx
	ret
SYM_FUNC_END(sha256_transform_avx)

.section	.rodata.cst256.K256, "aM", @progbits, 256
.align 64
K256:
	.long 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5
	.long 0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5
	.long 0xd807aa98,0x12835b01,0x243185be,0x550c7dc3
	.long 0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174
	.long 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc
	.long 0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da
	.long 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7
	.long 0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967
	.long 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13
	.long 0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85
	.long 0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3
	.long 0xd192e819,0xd6990624,0xf40e3585,0x106aa070
	.long 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5
	.long 0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3
	.long 0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208
	.long 0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2

.section	.rodata.cst16.PSHUFFLE_BYTE_FLIP_MASK, "aM", @progbits, 16
.align 16
PSHUFFLE_BYTE_FLIP_MASK:
	.octa 0x0c0d0e0f08090a0b0405060700010203

.section	.rodata.cst16._SHUF_00BA, "aM", @progbits, 16
.align 16
# shuffle xBxA -> 00BA
_SHUF_00BA:
	.octa 0xFFFFFFFFFFFFFFFF0b0a090803020100

.section	.rodata.cst16._SHUF_DC00, "aM", @progbits, 16
.align 16
# shuffle xDxC -> DC00
_SHUF_DC00:
	.octa 0x0b0a090803020100FFFFFFFFFFFFFFFF
