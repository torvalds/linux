########################################################################
# Implement fast SHA-512 with AVX2 instructions. (x86_64)
#
# Copyright (C) 2013 Intel Corporation.
#
# Authors:
#     James Guilford <james.guilford@intel.com>
#     Kirk Yap <kirk.s.yap@intel.com>
#     David Cote <david.m.cote@intel.com>
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
#
########################################################################
#
# This code is described in an Intel White-Paper:
# "Fast SHA-512 Implementations on Intel Architecture Processors"
#
# To find it, surf to http://www.intel.com/p/en_US/embedded
# and search for that title.
#
########################################################################
# This code schedules 1 blocks at a time, with 4 lanes per block
########################################################################

#include <linux/linkage.h>
#include <linux/cfi_types.h>

.text

# Virtual Registers
Y_0 = %ymm4
Y_1 = %ymm5
Y_2 = %ymm6
Y_3 = %ymm7

YTMP0 = %ymm0
YTMP1 = %ymm1
YTMP2 = %ymm2
YTMP3 = %ymm3
YTMP4 = %ymm8
XFER  = YTMP0

BYTE_FLIP_MASK  = %ymm9

# 1st arg is %rdi, which is saved to the stack and accessed later via %r12
CTX1        = %rdi
CTX2        = %r12
# 2nd arg
INP         = %rsi
# 3rd arg
NUM_BLKS    = %rdx

c           = %rcx
d           = %r8
e           = %rdx
y3          = %rsi

TBL   = %rdi # clobbers CTX1

a     = %rax
b     = %rbx

f     = %r9
g     = %r10
h     = %r11
old_h = %r11

T1    = %r12 # clobbers CTX2
y0    = %r13
y1    = %r14
y2    = %r15

# Local variables (stack frame)
XFER_SIZE = 4*8
SRND_SIZE = 1*8
INP_SIZE = 1*8
INPEND_SIZE = 1*8
CTX_SIZE = 1*8

frame_XFER = 0
frame_SRND = frame_XFER + XFER_SIZE
frame_INP = frame_SRND + SRND_SIZE
frame_INPEND = frame_INP + INP_SIZE
frame_CTX = frame_INPEND + INPEND_SIZE
frame_size = frame_CTX + CTX_SIZE

## assume buffers not aligned
#define	VMOVDQ vmovdqu

# addm [mem], reg
# Add reg to mem using reg-mem add and store
.macro addm p1 p2
	add	\p1, \p2
	mov	\p2, \p1
.endm


# COPY_YMM_AND_BSWAP ymm, [mem], byte_flip_mask
# Load ymm with mem and byte swap each dword
.macro COPY_YMM_AND_BSWAP p1 p2 p3
	VMOVDQ \p2, \p1
	vpshufb \p3, \p1, \p1
.endm
# rotate_Ys
# Rotate values of symbols Y0...Y3
.macro rotate_Ys
	Y_ = Y_0
	Y_0 = Y_1
	Y_1 = Y_2
	Y_2 = Y_3
	Y_3 = Y_
.endm

# RotateState
.macro RotateState
	# Rotate symbols a..h right
	old_h  = h
	TMP_   = h
	h      = g
	g      = f
	f      = e
	e      = d
	d      = c
	c      = b
	b      = a
	a      = TMP_
.endm

# macro MY_VPALIGNR	YDST, YSRC1, YSRC2, RVAL
# YDST = {YSRC1, YSRC2} >> RVAL*8
.macro MY_VPALIGNR YDST YSRC1 YSRC2 RVAL
	vperm2f128      $0x3, \YSRC2, \YSRC1, \YDST     # YDST = {YS1_LO, YS2_HI}
	vpalignr        $\RVAL, \YSRC2, \YDST, \YDST    # YDST = {YDS1, YS2} >> RVAL*8
.endm

.macro FOUR_ROUNDS_AND_SCHED
################################### RND N + 0 #########################################

	# Extract w[t-7]
	MY_VPALIGNR	YTMP0, Y_3, Y_2, 8		# YTMP0 = W[-7]
	# Calculate w[t-16] + w[t-7]
	vpaddq		Y_0, YTMP0, YTMP0		# YTMP0 = W[-7] + W[-16]
	# Extract w[t-15]
	MY_VPALIGNR	YTMP1, Y_1, Y_0, 8		# YTMP1 = W[-15]

	# Calculate sigma0

	# Calculate w[t-15] ror 1
	vpsrlq		$1, YTMP1, YTMP2
	vpsllq		$(64-1), YTMP1, YTMP3
	vpor		YTMP2, YTMP3, YTMP3		# YTMP3 = W[-15] ror 1
	# Calculate w[t-15] shr 7
	vpsrlq		$7, YTMP1, YTMP4		# YTMP4 = W[-15] >> 7

	mov	a, y3		# y3 = a                                # MAJA
	rorx	$41, e, y0	# y0 = e >> 41				# S1A
	rorx	$18, e, y1	# y1 = e >> 18				# S1B
	add	frame_XFER(%rsp),h		# h = k + w + h         # --
	or	c, y3		# y3 = a|c                              # MAJA
	mov	f, y2		# y2 = f                                # CH
	rorx	$34, a, T1	# T1 = a >> 34				# S0B

	xor	y1, y0		# y0 = (e>>41) ^ (e>>18)		# S1
	xor	g, y2		# y2 = f^g                              # CH
	rorx	$14, e, y1	# y1 = (e >> 14)			# S1

	and	e, y2		# y2 = (f^g)&e                          # CH
	xor	y1, y0		# y0 = (e>>41) ^ (e>>18) ^ (e>>14)	# S1
	rorx	$39, a, y1	# y1 = a >> 39				# S0A
	add	h, d		# d = k + w + h + d                     # --

	and	b, y3		# y3 = (a|c)&b                          # MAJA
	xor	T1, y1		# y1 = (a>>39) ^ (a>>34)		# S0
	rorx	$28, a, T1	# T1 = (a >> 28)			# S0

	xor	g, y2		# y2 = CH = ((f^g)&e)^g                 # CH
	xor	T1, y1		# y1 = (a>>39) ^ (a>>34) ^ (a>>28)	# S0
	mov	a, T1		# T1 = a                                # MAJB
	and	c, T1		# T1 = a&c                              # MAJB

	add	y0, y2		# y2 = S1 + CH                          # --
	or	T1, y3		# y3 = MAJ = (a|c)&b)|(a&c)             # MAJ
	add	y1, h		# h = k + w + h + S0                    # --

	add	y2, d		# d = k + w + h + d + S1 + CH = d + t1  # --

	add	y2, h		# h = k + w + h + S0 + S1 + CH = t1 + S0# --
	add	y3, h		# h = t1 + S0 + MAJ                     # --

	RotateState

################################### RND N + 1 #########################################

	# Calculate w[t-15] ror 8
	vpsrlq		$8, YTMP1, YTMP2
	vpsllq		$(64-8), YTMP1, YTMP1
	vpor		YTMP2, YTMP1, YTMP1		# YTMP1 = W[-15] ror 8
	# XOR the three components
	vpxor		YTMP4, YTMP3, YTMP3		# YTMP3 = W[-15] ror 1 ^ W[-15] >> 7
	vpxor		YTMP1, YTMP3, YTMP1		# YTMP1 = s0


	# Add three components, w[t-16], w[t-7] and sigma0
	vpaddq		YTMP1, YTMP0, YTMP0		# YTMP0 = W[-16] + W[-7] + s0
	# Move to appropriate lanes for calculating w[16] and w[17]
	vperm2f128	$0x0, YTMP0, YTMP0, Y_0		# Y_0 = W[-16] + W[-7] + s0 {BABA}
	# Move to appropriate lanes for calculating w[18] and w[19]
	vpand		MASK_YMM_LO(%rip), YTMP0, YTMP0	# YTMP0 = W[-16] + W[-7] + s0 {DC00}

	# Calculate w[16] and w[17] in both 128 bit lanes

	# Calculate sigma1 for w[16] and w[17] on both 128 bit lanes
	vperm2f128	$0x11, Y_3, Y_3, YTMP2		# YTMP2 = W[-2] {BABA}
	vpsrlq		$6, YTMP2, YTMP4		# YTMP4 = W[-2] >> 6 {BABA}


	mov	a, y3		# y3 = a                                # MAJA
	rorx	$41, e, y0	# y0 = e >> 41				# S1A
	rorx	$18, e, y1	# y1 = e >> 18				# S1B
	add	1*8+frame_XFER(%rsp), h		# h = k + w + h         # --
	or	c, y3		# y3 = a|c                              # MAJA


	mov	f, y2		# y2 = f                                # CH
	rorx	$34, a, T1	# T1 = a >> 34				# S0B
	xor	y1, y0		# y0 = (e>>41) ^ (e>>18)		# S1
	xor	g, y2		# y2 = f^g                              # CH


	rorx	$14, e, y1	# y1 = (e >> 14)			# S1
	xor	y1, y0		# y0 = (e>>41) ^ (e>>18) ^ (e>>14)	# S1
	rorx	$39, a, y1	# y1 = a >> 39				# S0A
	and	e, y2		# y2 = (f^g)&e                          # CH
	add	h, d		# d = k + w + h + d                     # --

	and	b, y3		# y3 = (a|c)&b                          # MAJA
	xor	T1, y1		# y1 = (a>>39) ^ (a>>34)		# S0

	rorx	$28, a, T1	# T1 = (a >> 28)			# S0
	xor	g, y2		# y2 = CH = ((f^g)&e)^g                 # CH

	xor	T1, y1		# y1 = (a>>39) ^ (a>>34) ^ (a>>28)	# S0
	mov	a, T1		# T1 = a                                # MAJB
	and	c, T1		# T1 = a&c                              # MAJB
	add	y0, y2		# y2 = S1 + CH                          # --

	or	T1, y3		# y3 = MAJ = (a|c)&b)|(a&c)             # MAJ
	add	y1, h		# h = k + w + h + S0                    # --

	add	y2, d		# d = k + w + h + d + S1 + CH = d + t1  # --
	add	y2, h		# h = k + w + h + S0 + S1 + CH = t1 + S0# --
	add	y3, h		# h = t1 + S0 + MAJ                     # --

	RotateState


################################### RND N + 2 #########################################

	vpsrlq		$19, YTMP2, YTMP3		# YTMP3 = W[-2] >> 19 {BABA}
	vpsllq		$(64-19), YTMP2, YTMP1		# YTMP1 = W[-2] << 19 {BABA}
	vpor		YTMP1, YTMP3, YTMP3		# YTMP3 = W[-2] ror 19 {BABA}
	vpxor		YTMP3, YTMP4, YTMP4		# YTMP4 = W[-2] ror 19 ^ W[-2] >> 6 {BABA}
	vpsrlq		$61, YTMP2, YTMP3		# YTMP3 = W[-2] >> 61 {BABA}
	vpsllq		$(64-61), YTMP2, YTMP1		# YTMP1 = W[-2] << 61 {BABA}
	vpor		YTMP1, YTMP3, YTMP3		# YTMP3 = W[-2] ror 61 {BABA}
	vpxor		YTMP3, YTMP4, YTMP4		# YTMP4 = s1 = (W[-2] ror 19) ^
							#  (W[-2] ror 61) ^ (W[-2] >> 6) {BABA}

	# Add sigma1 to the other compunents to get w[16] and w[17]
	vpaddq		YTMP4, Y_0, Y_0			# Y_0 = {W[1], W[0], W[1], W[0]}

	# Calculate sigma1 for w[18] and w[19] for upper 128 bit lane
	vpsrlq		$6, Y_0, YTMP4			# YTMP4 = W[-2] >> 6 {DC--}

	mov	a, y3		# y3 = a                                # MAJA
	rorx	$41, e, y0	# y0 = e >> 41				# S1A
	add	2*8+frame_XFER(%rsp), h		# h = k + w + h         # --

	rorx	$18, e, y1	# y1 = e >> 18				# S1B
	or	c, y3		# y3 = a|c                              # MAJA
	mov	f, y2		# y2 = f                                # CH
	xor	g, y2		# y2 = f^g                              # CH

	rorx	$34, a, T1	# T1 = a >> 34				# S0B
	xor	y1, y0		# y0 = (e>>41) ^ (e>>18)		# S1
	and	e, y2		# y2 = (f^g)&e                          # CH

	rorx	$14, e, y1	# y1 = (e >> 14)			# S1
	add	h, d		# d = k + w + h + d                     # --
	and	b, y3		# y3 = (a|c)&b                          # MAJA

	xor	y1, y0		# y0 = (e>>41) ^ (e>>18) ^ (e>>14)	# S1
	rorx	$39, a, y1	# y1 = a >> 39				# S0A
	xor	g, y2		# y2 = CH = ((f^g)&e)^g                 # CH

	xor	T1, y1		# y1 = (a>>39) ^ (a>>34)		# S0
	rorx	$28, a, T1	# T1 = (a >> 28)			# S0

	xor	T1, y1		# y1 = (a>>39) ^ (a>>34) ^ (a>>28)	# S0
	mov	a, T1		# T1 = a                                # MAJB
	and	c, T1		# T1 = a&c                              # MAJB
	add	y0, y2		# y2 = S1 + CH                          # --

	or	T1, y3		# y3 = MAJ = (a|c)&b)|(a&c)             # MAJ
	add	y1, h		# h = k + w + h + S0                    # --
	add	y2, d		# d = k + w + h + d + S1 + CH = d + t1  # --
	add	y2, h		# h = k + w + h + S0 + S1 + CH = t1 + S0# --

	add	y3, h		# h = t1 + S0 + MAJ                     # --

	RotateState

################################### RND N + 3 #########################################

	vpsrlq		$19, Y_0, YTMP3			# YTMP3 = W[-2] >> 19 {DC--}
	vpsllq		$(64-19), Y_0, YTMP1		# YTMP1 = W[-2] << 19 {DC--}
	vpor		YTMP1, YTMP3, YTMP3		# YTMP3 = W[-2] ror 19 {DC--}
	vpxor		YTMP3, YTMP4, YTMP4		# YTMP4 = W[-2] ror 19 ^ W[-2] >> 6 {DC--}
	vpsrlq		$61, Y_0, YTMP3			# YTMP3 = W[-2] >> 61 {DC--}
	vpsllq		$(64-61), Y_0, YTMP1		# YTMP1 = W[-2] << 61 {DC--}
	vpor		YTMP1, YTMP3, YTMP3		# YTMP3 = W[-2] ror 61 {DC--}
	vpxor		YTMP3, YTMP4, YTMP4		# YTMP4 = s1 = (W[-2] ror 19) ^
							#  (W[-2] ror 61) ^ (W[-2] >> 6) {DC--}

	# Add the sigma0 + w[t-7] + w[t-16] for w[18] and w[19]
	# to newly calculated sigma1 to get w[18] and w[19]
	vpaddq		YTMP4, YTMP0, YTMP2		# YTMP2 = {W[3], W[2], --, --}

	# Form w[19, w[18], w17], w[16]
	vpblendd		$0xF0, YTMP2, Y_0, Y_0		# Y_0 = {W[3], W[2], W[1], W[0]}

	mov	a, y3		# y3 = a                                # MAJA
	rorx	$41, e, y0	# y0 = e >> 41				# S1A
	rorx	$18, e, y1	# y1 = e >> 18				# S1B
	add	3*8+frame_XFER(%rsp), h		# h = k + w + h         # --
	or	c, y3		# y3 = a|c                              # MAJA


	mov	f, y2		# y2 = f                                # CH
	rorx	$34, a, T1	# T1 = a >> 34				# S0B
	xor	y1, y0		# y0 = (e>>41) ^ (e>>18)		# S1
	xor	g, y2		# y2 = f^g                              # CH


	rorx	$14, e, y1	# y1 = (e >> 14)			# S1
	and	e, y2		# y2 = (f^g)&e                          # CH
	add	h, d		# d = k + w + h + d                     # --
	and	b, y3		# y3 = (a|c)&b                          # MAJA

	xor	y1, y0		# y0 = (e>>41) ^ (e>>18) ^ (e>>14)	# S1
	xor	g, y2		# y2 = CH = ((f^g)&e)^g                 # CH

	rorx	$39, a, y1	# y1 = a >> 39				# S0A
	add	y0, y2		# y2 = S1 + CH                          # --

	xor	T1, y1		# y1 = (a>>39) ^ (a>>34)		# S0
	add	y2, d		# d = k + w + h + d + S1 + CH = d + t1  # --

	rorx	$28, a, T1	# T1 = (a >> 28)			# S0

	xor	T1, y1		# y1 = (a>>39) ^ (a>>34) ^ (a>>28)	# S0
	mov	a, T1		# T1 = a                                # MAJB
	and	c, T1		# T1 = a&c                              # MAJB
	or	T1, y3		# y3 = MAJ = (a|c)&b)|(a&c)             # MAJ

	add	y1, h		# h = k + w + h + S0                    # --
	add	y2, h		# h = k + w + h + S0 + S1 + CH = t1 + S0# --
	add	y3, h		# h = t1 + S0 + MAJ                     # --

	RotateState

	rotate_Ys
.endm

.macro DO_4ROUNDS

################################### RND N + 0 #########################################

	mov	f, y2		# y2 = f                                # CH
	rorx	$41, e, y0	# y0 = e >> 41				# S1A
	rorx	$18, e, y1	# y1 = e >> 18				# S1B
	xor	g, y2		# y2 = f^g                              # CH

	xor	y1, y0		# y0 = (e>>41) ^ (e>>18)		# S1
	rorx	$14, e, y1	# y1 = (e >> 14)			# S1
	and	e, y2		# y2 = (f^g)&e                          # CH

	xor	y1, y0		# y0 = (e>>41) ^ (e>>18) ^ (e>>14)	# S1
	rorx	$34, a, T1	# T1 = a >> 34				# S0B
	xor	g, y2		# y2 = CH = ((f^g)&e)^g                 # CH
	rorx	$39, a, y1	# y1 = a >> 39				# S0A
	mov	a, y3		# y3 = a                                # MAJA

	xor	T1, y1		# y1 = (a>>39) ^ (a>>34)		# S0
	rorx	$28, a, T1	# T1 = (a >> 28)			# S0
	add	frame_XFER(%rsp), h		# h = k + w + h         # --
	or	c, y3		# y3 = a|c                              # MAJA

	xor	T1, y1		# y1 = (a>>39) ^ (a>>34) ^ (a>>28)	# S0
	mov	a, T1		# T1 = a                                # MAJB
	and	b, y3		# y3 = (a|c)&b                          # MAJA
	and	c, T1		# T1 = a&c                              # MAJB
	add	y0, y2		# y2 = S1 + CH                          # --

	add	h, d		# d = k + w + h + d                     # --
	or	T1, y3		# y3 = MAJ = (a|c)&b)|(a&c)             # MAJ
	add	y1, h		# h = k + w + h + S0                    # --

	add	y2, d		# d = k + w + h + d + S1 + CH = d + t1  # --

	RotateState

################################### RND N + 1 #########################################

	add	y2, old_h	# h = k + w + h + S0 + S1 + CH = t1 + S0# --
	mov	f, y2		# y2 = f                                # CH
	rorx	$41, e, y0	# y0 = e >> 41				# S1A
	rorx	$18, e, y1	# y1 = e >> 18				# S1B
	xor	g, y2		# y2 = f^g                              # CH

	xor	y1, y0		# y0 = (e>>41) ^ (e>>18)		# S1
	rorx	$14, e, y1	# y1 = (e >> 14)			# S1
	and	e, y2		# y2 = (f^g)&e                          # CH
	add	y3, old_h	# h = t1 + S0 + MAJ                     # --

	xor	y1, y0		# y0 = (e>>41) ^ (e>>18) ^ (e>>14)	# S1
	rorx	$34, a, T1	# T1 = a >> 34				# S0B
	xor	g, y2		# y2 = CH = ((f^g)&e)^g                 # CH
	rorx	$39, a, y1	# y1 = a >> 39				# S0A
	mov	a, y3		# y3 = a                                # MAJA

	xor	T1, y1		# y1 = (a>>39) ^ (a>>34)		# S0
	rorx	$28, a, T1	# T1 = (a >> 28)			# S0
	add	8*1+frame_XFER(%rsp), h		# h = k + w + h         # --
	or	c, y3		# y3 = a|c                              # MAJA

	xor	T1, y1		# y1 = (a>>39) ^ (a>>34) ^ (a>>28)	# S0
	mov	a, T1		# T1 = a                                # MAJB
	and	b, y3		# y3 = (a|c)&b                          # MAJA
	and	c, T1		# T1 = a&c                              # MAJB
	add	y0, y2		# y2 = S1 + CH                          # --

	add	h, d		# d = k + w + h + d                     # --
	or	T1, y3		# y3 = MAJ = (a|c)&b)|(a&c)             # MAJ
	add	y1, h		# h = k + w + h + S0                    # --

	add	y2, d		# d = k + w + h + d + S1 + CH = d + t1  # --

	RotateState

################################### RND N + 2 #########################################

	add	y2, old_h	# h = k + w + h + S0 + S1 + CH = t1 + S0# --
	mov	f, y2		# y2 = f                                # CH
	rorx	$41, e, y0	# y0 = e >> 41				# S1A
	rorx	$18, e, y1	# y1 = e >> 18				# S1B
	xor	g, y2		# y2 = f^g                              # CH

	xor	y1, y0		# y0 = (e>>41) ^ (e>>18)		# S1
	rorx	$14, e, y1	# y1 = (e >> 14)			# S1
	and	e, y2		# y2 = (f^g)&e                          # CH
	add	y3, old_h	# h = t1 + S0 + MAJ                     # --

	xor	y1, y0		# y0 = (e>>41) ^ (e>>18) ^ (e>>14)	# S1
	rorx	$34, a, T1	# T1 = a >> 34				# S0B
	xor	g, y2		# y2 = CH = ((f^g)&e)^g                 # CH
	rorx	$39, a, y1	# y1 = a >> 39				# S0A
	mov	a, y3		# y3 = a                                # MAJA

	xor	T1, y1		# y1 = (a>>39) ^ (a>>34)		# S0
	rorx	$28, a, T1	# T1 = (a >> 28)			# S0
	add	8*2+frame_XFER(%rsp), h		# h = k + w + h         # --
	or	c, y3		# y3 = a|c                              # MAJA

	xor	T1, y1		# y1 = (a>>39) ^ (a>>34) ^ (a>>28)	# S0
	mov	a, T1		# T1 = a                                # MAJB
	and	b, y3		# y3 = (a|c)&b                          # MAJA
	and	c, T1		# T1 = a&c                              # MAJB
	add	y0, y2		# y2 = S1 + CH                          # --

	add	h, d		# d = k + w + h + d                     # --
	or	T1, y3		# y3 = MAJ = (a|c)&b)|(a&c)             # MAJ
	add	y1, h		# h = k + w + h + S0                    # --

	add	y2, d		# d = k + w + h + d + S1 + CH = d + t1  # --

	RotateState

################################### RND N + 3 #########################################

	add	y2, old_h	# h = k + w + h + S0 + S1 + CH = t1 + S0# --
	mov	f, y2		# y2 = f                                # CH
	rorx	$41, e, y0	# y0 = e >> 41				# S1A
	rorx	$18, e, y1	# y1 = e >> 18				# S1B
	xor	g, y2		# y2 = f^g                              # CH

	xor	y1, y0		# y0 = (e>>41) ^ (e>>18)		# S1
	rorx	$14, e, y1	# y1 = (e >> 14)			# S1
	and	e, y2		# y2 = (f^g)&e                          # CH
	add	y3, old_h	# h = t1 + S0 + MAJ                     # --

	xor	y1, y0		# y0 = (e>>41) ^ (e>>18) ^ (e>>14)	# S1
	rorx	$34, a, T1	# T1 = a >> 34				# S0B
	xor	g, y2		# y2 = CH = ((f^g)&e)^g                 # CH
	rorx	$39, a, y1	# y1 = a >> 39				# S0A
	mov	a, y3		# y3 = a                                # MAJA

	xor	T1, y1		# y1 = (a>>39) ^ (a>>34)		# S0
	rorx	$28, a, T1	# T1 = (a >> 28)			# S0
	add	8*3+frame_XFER(%rsp), h		# h = k + w + h         # --
	or	c, y3		# y3 = a|c                              # MAJA

	xor	T1, y1		# y1 = (a>>39) ^ (a>>34) ^ (a>>28)	# S0
	mov	a, T1		# T1 = a                                # MAJB
	and	b, y3		# y3 = (a|c)&b                          # MAJA
	and	c, T1		# T1 = a&c                              # MAJB
	add	y0, y2		# y2 = S1 + CH                          # --


	add	h, d		# d = k + w + h + d                     # --
	or	T1, y3		# y3 = MAJ = (a|c)&b)|(a&c)             # MAJ
	add	y1, h		# h = k + w + h + S0                    # --

	add	y2, d		# d = k + w + h + d + S1 + CH = d + t1  # --

	add	y2, h		# h = k + w + h + S0 + S1 + CH = t1 + S0# --

	add	y3, h		# h = t1 + S0 + MAJ                     # --

	RotateState

.endm

########################################################################
# void sha512_transform_rorx(sha512_state *state, const u8 *data, int blocks)
# Purpose: Updates the SHA512 digest stored at "state" with the message
# stored in "data".
# The size of the message pointed to by "data" must be an integer multiple
# of SHA512 message blocks.
# "blocks" is the message length in SHA512 blocks
########################################################################
SYM_TYPED_FUNC_START(sha512_transform_rorx)
	# Save GPRs
	push	%rbx
	push	%r12
	push	%r13
	push	%r14
	push	%r15

	# Allocate Stack Space
	push	%rbp
	mov	%rsp, %rbp
	sub	$frame_size, %rsp
	and	$~(0x20 - 1), %rsp

	shl	$7, NUM_BLKS	# convert to bytes
	jz	done_hash
	add	INP, NUM_BLKS	# pointer to end of data
	mov	NUM_BLKS, frame_INPEND(%rsp)

	## load initial digest
	mov	8*0(CTX1), a
	mov	8*1(CTX1), b
	mov	8*2(CTX1), c
	mov	8*3(CTX1), d
	mov	8*4(CTX1), e
	mov	8*5(CTX1), f
	mov	8*6(CTX1), g
	mov	8*7(CTX1), h

	# save %rdi (CTX) before it gets clobbered
	mov	%rdi, frame_CTX(%rsp)

	vmovdqa	PSHUFFLE_BYTE_FLIP_MASK(%rip), BYTE_FLIP_MASK

loop0:
	lea	K512(%rip), TBL

	## byte swap first 16 dwords
	COPY_YMM_AND_BSWAP	Y_0, (INP), BYTE_FLIP_MASK
	COPY_YMM_AND_BSWAP	Y_1, 1*32(INP), BYTE_FLIP_MASK
	COPY_YMM_AND_BSWAP	Y_2, 2*32(INP), BYTE_FLIP_MASK
	COPY_YMM_AND_BSWAP	Y_3, 3*32(INP), BYTE_FLIP_MASK

	mov	INP, frame_INP(%rsp)

	## schedule 64 input dwords, by doing 12 rounds of 4 each
	movq	$4, frame_SRND(%rsp)

.align 16
loop1:
	vpaddq	(TBL), Y_0, XFER
	vmovdqa XFER, frame_XFER(%rsp)
	FOUR_ROUNDS_AND_SCHED

	vpaddq	1*32(TBL), Y_0, XFER
	vmovdqa XFER, frame_XFER(%rsp)
	FOUR_ROUNDS_AND_SCHED

	vpaddq	2*32(TBL), Y_0, XFER
	vmovdqa XFER, frame_XFER(%rsp)
	FOUR_ROUNDS_AND_SCHED

	vpaddq	3*32(TBL), Y_0, XFER
	vmovdqa XFER, frame_XFER(%rsp)
	add	$(4*32), TBL
	FOUR_ROUNDS_AND_SCHED

	subq	$1, frame_SRND(%rsp)
	jne	loop1

	movq	$2, frame_SRND(%rsp)
loop2:
	vpaddq	(TBL), Y_0, XFER
	vmovdqa XFER, frame_XFER(%rsp)
	DO_4ROUNDS
	vpaddq	1*32(TBL), Y_1, XFER
	vmovdqa XFER, frame_XFER(%rsp)
	add	$(2*32), TBL
	DO_4ROUNDS

	vmovdqa	Y_2, Y_0
	vmovdqa	Y_3, Y_1

	subq	$1, frame_SRND(%rsp)
	jne	loop2

	mov	frame_CTX(%rsp), CTX2
	addm	8*0(CTX2), a
	addm	8*1(CTX2), b
	addm	8*2(CTX2), c
	addm	8*3(CTX2), d
	addm	8*4(CTX2), e
	addm	8*5(CTX2), f
	addm	8*6(CTX2), g
	addm	8*7(CTX2), h

	mov	frame_INP(%rsp), INP
	add	$128, INP
	cmp	frame_INPEND(%rsp), INP
	jne	loop0

done_hash:

	# Restore Stack Pointer
	mov	%rbp, %rsp
	pop	%rbp

	# Restore GPRs
	pop	%r15
	pop	%r14
	pop	%r13
	pop	%r12
	pop	%rbx

	RET
SYM_FUNC_END(sha512_transform_rorx)

########################################################################
### Binary Data


# Mergeable 640-byte rodata section. This allows linker to merge the table
# with other, exactly the same 640-byte fragment of another rodata section
# (if such section exists).
.section	.rodata.cst640.K512, "aM", @progbits, 640
.align 64
# K[t] used in SHA512 hashing
K512:
	.quad	0x428a2f98d728ae22,0x7137449123ef65cd
	.quad	0xb5c0fbcfec4d3b2f,0xe9b5dba58189dbbc
	.quad	0x3956c25bf348b538,0x59f111f1b605d019
	.quad	0x923f82a4af194f9b,0xab1c5ed5da6d8118
	.quad	0xd807aa98a3030242,0x12835b0145706fbe
	.quad	0x243185be4ee4b28c,0x550c7dc3d5ffb4e2
	.quad	0x72be5d74f27b896f,0x80deb1fe3b1696b1
	.quad	0x9bdc06a725c71235,0xc19bf174cf692694
	.quad	0xe49b69c19ef14ad2,0xefbe4786384f25e3
	.quad	0x0fc19dc68b8cd5b5,0x240ca1cc77ac9c65
	.quad	0x2de92c6f592b0275,0x4a7484aa6ea6e483
	.quad	0x5cb0a9dcbd41fbd4,0x76f988da831153b5
	.quad	0x983e5152ee66dfab,0xa831c66d2db43210
	.quad	0xb00327c898fb213f,0xbf597fc7beef0ee4
	.quad	0xc6e00bf33da88fc2,0xd5a79147930aa725
	.quad	0x06ca6351e003826f,0x142929670a0e6e70
	.quad	0x27b70a8546d22ffc,0x2e1b21385c26c926
	.quad	0x4d2c6dfc5ac42aed,0x53380d139d95b3df
	.quad	0x650a73548baf63de,0x766a0abb3c77b2a8
	.quad	0x81c2c92e47edaee6,0x92722c851482353b
	.quad	0xa2bfe8a14cf10364,0xa81a664bbc423001
	.quad	0xc24b8b70d0f89791,0xc76c51a30654be30
	.quad	0xd192e819d6ef5218,0xd69906245565a910
	.quad	0xf40e35855771202a,0x106aa07032bbd1b8
	.quad	0x19a4c116b8d2d0c8,0x1e376c085141ab53
	.quad	0x2748774cdf8eeb99,0x34b0bcb5e19b48a8
	.quad	0x391c0cb3c5c95a63,0x4ed8aa4ae3418acb
	.quad	0x5b9cca4f7763e373,0x682e6ff3d6b2b8a3
	.quad	0x748f82ee5defb2fc,0x78a5636f43172f60
	.quad	0x84c87814a1f0ab72,0x8cc702081a6439ec
	.quad	0x90befffa23631e28,0xa4506cebde82bde9
	.quad	0xbef9a3f7b2c67915,0xc67178f2e372532b
	.quad	0xca273eceea26619c,0xd186b8c721c0c207
	.quad	0xeada7dd6cde0eb1e,0xf57d4f7fee6ed178
	.quad	0x06f067aa72176fba,0x0a637dc5a2c898a6
	.quad	0x113f9804bef90dae,0x1b710b35131c471b
	.quad	0x28db77f523047d84,0x32caab7b40c72493
	.quad	0x3c9ebe0a15c9bebc,0x431d67c49c100d4c
	.quad	0x4cc5d4becb3e42b6,0x597f299cfc657e2a
	.quad	0x5fcb6fab3ad6faec,0x6c44198c4a475817

.section	.rodata.cst32.PSHUFFLE_BYTE_FLIP_MASK, "aM", @progbits, 32
.align 32
# Mask for byte-swapping a couple of qwords in an XMM register using (v)pshufb.
PSHUFFLE_BYTE_FLIP_MASK:
	.octa 0x08090a0b0c0d0e0f0001020304050607
	.octa 0x18191a1b1c1d1e1f1011121314151617

.section	.rodata.cst32.MASK_YMM_LO, "aM", @progbits, 32
.align 32
MASK_YMM_LO:
	.octa 0x00000000000000000000000000000000
	.octa 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
