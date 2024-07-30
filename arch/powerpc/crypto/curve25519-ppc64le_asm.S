/* SPDX-License-Identifier: GPL-2.0-or-later */
#
# This code is taken from CRYPTOGAMs[1] and is included here using the option
# in the license to distribute the code under the GPL. Therefore this program
# is free software; you can redistribute it and/or modify it under the terms of
# the GNU General Public License version 2 as published by the Free Software
# Foundation.
#
# [1] https://github.com/dot-asm/cryptogams/

# Copyright (c) 2006-2017, CRYPTOGAMS by <appro@openssl.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#       * Redistributions of source code must retain copyright notices,
#         this list of conditions and the following disclaimer.
#
#       * Redistributions in binary form must reproduce the above
#         copyright notice, this list of conditions and the following
#         disclaimer in the documentation and/or other materials
#         provided with the distribution.
#
#       * Neither the name of the CRYPTOGAMS nor the names of its
#         copyright holder and contributors may be used to endorse or
#         promote products derived from this software without specific
#         prior written permission.
#
# ALTERNATIVELY, provided that this notice is retained in full, this
# product may be distributed under the terms of the GNU General Public
# License (GPL), in which case the provisions of the GPL apply INSTEAD OF
# those given above.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see https://www.openssl.org/~appro/cryptogams/.
# ====================================================================

#
# ====================================================================
# Written and Modified by Danny Tsen <dtsen@us.ibm.com>
# - Added x25519_fe51_sqr_times, x25519_fe51_frombytes, x25519_fe51_tobytes
#   and x25519_cswap
#
# Copyright 2024- IBM Corp.
#
# X25519 lower-level primitives for PPC64.
#

#include <linux/linkage.h>

.text

.align	5
SYM_FUNC_START(x25519_fe51_mul)

	stdu	1,-144(1)
	std	21,56(1)
	std	22,64(1)
	std	23,72(1)
	std	24,80(1)
	std	25,88(1)
	std	26,96(1)
	std	27,104(1)
	std	28,112(1)
	std	29,120(1)
	std	30,128(1)
	std	31,136(1)

	ld	6,0(5)
	ld	7,0(4)
	ld	8,8(4)
	ld	9,16(4)
	ld	10,24(4)
	ld	11,32(4)

	mulld	22,7,6
	mulhdu	23,7,6

	mulld	24,8,6
	mulhdu	25,8,6

	mulld	30,11,6
	mulhdu	31,11,6
	ld	4,8(5)
	mulli	11,11,19

	mulld	26,9,6
	mulhdu	27,9,6

	mulld	28,10,6
	mulhdu	29,10,6
	mulld	12,11,4
	mulhdu	21,11,4
	addc	22,22,12
	adde	23,23,21

	mulld	12,7,4
	mulhdu	21,7,4
	addc	24,24,12
	adde	25,25,21

	mulld	12,10,4
	mulhdu	21,10,4
	ld	6,16(5)
	mulli	10,10,19
	addc	30,30,12
	adde	31,31,21

	mulld	12,8,4
	mulhdu	21,8,4
	addc	26,26,12
	adde	27,27,21

	mulld	12,9,4
	mulhdu	21,9,4
	addc	28,28,12
	adde	29,29,21
	mulld	12,10,6
	mulhdu	21,10,6
	addc	22,22,12
	adde	23,23,21

	mulld	12,11,6
	mulhdu	21,11,6
	addc	24,24,12
	adde	25,25,21

	mulld	12,9,6
	mulhdu	21,9,6
	ld	4,24(5)
	mulli	9,9,19
	addc	30,30,12
	adde	31,31,21

	mulld	12,7,6
	mulhdu	21,7,6
	addc	26,26,12
	adde	27,27,21

	mulld	12,8,6
	mulhdu	21,8,6
	addc	28,28,12
	adde	29,29,21
	mulld	12,9,4
	mulhdu	21,9,4
	addc	22,22,12
	adde	23,23,21

	mulld	12,10,4
	mulhdu	21,10,4
	addc	24,24,12
	adde	25,25,21

	mulld	12,8,4
	mulhdu	21,8,4
	ld	6,32(5)
	mulli	8,8,19
	addc	30,30,12
	adde	31,31,21

	mulld	12,11,4
	mulhdu	21,11,4
	addc	26,26,12
	adde	27,27,21

	mulld	12,7,4
	mulhdu	21,7,4
	addc	28,28,12
	adde	29,29,21
	mulld	12,8,6
	mulhdu	21,8,6
	addc	22,22,12
	adde	23,23,21

	mulld	12,9,6
	mulhdu	21,9,6
	addc	24,24,12
	adde	25,25,21

	mulld	12,10,6
	mulhdu	21,10,6
	addc	26,26,12
	adde	27,27,21

	mulld	12,11,6
	mulhdu	21,11,6
	addc	28,28,12
	adde	29,29,21

	mulld	12,7,6
	mulhdu	21,7,6
	addc	30,30,12
	adde	31,31,21

.Lfe51_reduce:
	li	0,-1
	srdi	0,0,13

	srdi	12,26,51
	and	9,26,0
	insrdi	12,27,51,0
	srdi	21,22,51
	and	7,22,0
	insrdi	21,23,51,0
	addc	28,28,12
	addze	29,29
	addc	24,24,21
	addze	25,25

	srdi	12,28,51
	and	10,28,0
	insrdi	12,29,51,0
	srdi	21,24,51
	and	8,24,0
	insrdi	21,25,51,0
	addc	30,30,12
	addze	31,31
	add	9,9,21

	srdi	12,30,51
	and	11,30,0
	insrdi	12,31,51,0
	mulli	12,12,19

	add	7,7,12

	srdi	21,9,51
	and	9,9,0
	add	10,10,21

	srdi	12,7,51
	and	7,7,0
	add	8,8,12

	std	9,16(3)
	std	10,24(3)
	std	11,32(3)
	std	7,0(3)
	std	8,8(3)

	ld	21,56(1)
	ld	22,64(1)
	ld	23,72(1)
	ld	24,80(1)
	ld	25,88(1)
	ld	26,96(1)
	ld	27,104(1)
	ld	28,112(1)
	ld	29,120(1)
	ld	30,128(1)
	ld	31,136(1)
	addi	1,1,144
	blr
SYM_FUNC_END(x25519_fe51_mul)

.align	5
SYM_FUNC_START(x25519_fe51_sqr)

	stdu	1,-144(1)
	std	21,56(1)
	std	22,64(1)
	std	23,72(1)
	std	24,80(1)
	std	25,88(1)
	std	26,96(1)
	std	27,104(1)
	std	28,112(1)
	std	29,120(1)
	std	30,128(1)
	std	31,136(1)

	ld	7,0(4)
	ld	8,8(4)
	ld	9,16(4)
	ld	10,24(4)
	ld	11,32(4)

	add	6,7,7
	mulli	21,11,19

	mulld	22,7,7
	mulhdu	23,7,7
	mulld	24,8,6
	mulhdu	25,8,6
	mulld	26,9,6
	mulhdu	27,9,6
	mulld	28,10,6
	mulhdu	29,10,6
	mulld	30,11,6
	mulhdu	31,11,6
	add	6,8,8
	mulld	12,11,21
	mulhdu	11,11,21
	addc	28,28,12
	adde	29,29,11

	mulli	5,10,19

	mulld	12,8,8
	mulhdu	11,8,8
	addc	26,26,12
	adde	27,27,11
	mulld	12,9,6
	mulhdu	11,9,6
	addc	28,28,12
	adde	29,29,11
	mulld	12,10,6
	mulhdu	11,10,6
	addc	30,30,12
	adde	31,31,11
	mulld	12,21,6
	mulhdu	11,21,6
	add	6,10,10
	addc	22,22,12
	adde	23,23,11
	mulld	12,10,5
	mulhdu	10,10,5
	addc	24,24,12
	adde	25,25,10
	mulld	12,6,21
	mulhdu	10,6,21
	add	6,9,9
	addc	26,26,12
	adde	27,27,10

	mulld	12,9,9
	mulhdu	10,9,9
	addc	30,30,12
	adde	31,31,10
	mulld	12,5,6
	mulhdu	10,5,6
	addc	22,22,12
	adde	23,23,10
	mulld	12,21,6
	mulhdu	10,21,6
	addc	24,24,12
	adde	25,25,10

	b	.Lfe51_reduce
SYM_FUNC_END(x25519_fe51_sqr)

.align	5
SYM_FUNC_START(x25519_fe51_mul121666)

	stdu	1,-144(1)
	std	21,56(1)
	std	22,64(1)
	std	23,72(1)
	std	24,80(1)
	std	25,88(1)
	std	26,96(1)
	std	27,104(1)
	std	28,112(1)
	std	29,120(1)
	std	30,128(1)
	std	31,136(1)

	lis	6,1
	ori	6,6,56130
	ld	7,0(4)
	ld	8,8(4)
	ld	9,16(4)
	ld	10,24(4)
	ld	11,32(4)

	mulld	22,7,6
	mulhdu	23,7,6
	mulld	24,8,6
	mulhdu	25,8,6
	mulld	26,9,6
	mulhdu	27,9,6
	mulld	28,10,6
	mulhdu	29,10,6
	mulld	30,11,6
	mulhdu	31,11,6

	b	.Lfe51_reduce
SYM_FUNC_END(x25519_fe51_mul121666)

.align	5
SYM_FUNC_START(x25519_fe51_sqr_times)

	stdu	1,-144(1)
	std	21,56(1)
	std	22,64(1)
	std	23,72(1)
	std	24,80(1)
	std	25,88(1)
	std	26,96(1)
	std	27,104(1)
	std	28,112(1)
	std	29,120(1)
	std	30,128(1)
	std	31,136(1)

	ld	7,0(4)
	ld	8,8(4)
	ld	9,16(4)
	ld	10,24(4)
	ld	11,32(4)

	mtctr	5

.Lsqr_times_loop:
	add	6,7,7
	mulli	21,11,19

	mulld	22,7,7
	mulhdu	23,7,7
	mulld	24,8,6
	mulhdu	25,8,6
	mulld	26,9,6
	mulhdu	27,9,6
	mulld	28,10,6
	mulhdu	29,10,6
	mulld	30,11,6
	mulhdu	31,11,6
	add	6,8,8
	mulld	12,11,21
	mulhdu	11,11,21
	addc	28,28,12
	adde	29,29,11

	mulli	5,10,19

	mulld	12,8,8
	mulhdu	11,8,8
	addc	26,26,12
	adde	27,27,11
	mulld	12,9,6
	mulhdu	11,9,6
	addc	28,28,12
	adde	29,29,11
	mulld	12,10,6
	mulhdu	11,10,6
	addc	30,30,12
	adde	31,31,11
	mulld	12,21,6
	mulhdu	11,21,6
	add	6,10,10
	addc	22,22,12
	adde	23,23,11
	mulld	12,10,5
	mulhdu	10,10,5
	addc	24,24,12
	adde	25,25,10
	mulld	12,6,21
	mulhdu	10,6,21
	add	6,9,9
	addc	26,26,12
	adde	27,27,10

	mulld	12,9,9
	mulhdu	10,9,9
	addc	30,30,12
	adde	31,31,10
	mulld	12,5,6
	mulhdu	10,5,6
	addc	22,22,12
	adde	23,23,10
	mulld	12,21,6
	mulhdu	10,21,6
	addc	24,24,12
	adde	25,25,10

	# fe51_reduce
	li	0,-1
	srdi	0,0,13

	srdi	12,26,51
	and	9,26,0
	insrdi	12,27,51,0
	srdi	21,22,51
	and	7,22,0
	insrdi	21,23,51,0
	addc	28,28,12
	addze	29,29
	addc	24,24,21
	addze	25,25

	srdi	12,28,51
	and	10,28,0
	insrdi	12,29,51,0
	srdi	21,24,51
	and	8,24,0
	insrdi	21,25,51,0
	addc	30,30,12
	addze	31,31
	add	9,9,21

	srdi	12,30,51
	and	11,30,0
	insrdi	12,31,51,0
	mulli	12,12,19

	add	7,7,12

	srdi	21,9,51
	and	9,9,0
	add	10,10,21

	srdi	12,7,51
	and	7,7,0
	add	8,8,12

	bdnz	.Lsqr_times_loop

	std	9,16(3)
	std	10,24(3)
	std	11,32(3)
	std	7,0(3)
	std	8,8(3)

	ld	21,56(1)
	ld	22,64(1)
	ld	23,72(1)
	ld	24,80(1)
	ld	25,88(1)
	ld	26,96(1)
	ld	27,104(1)
	ld	28,112(1)
	ld	29,120(1)
	ld	30,128(1)
	ld	31,136(1)
	addi	1,1,144
	blr
SYM_FUNC_END(x25519_fe51_sqr_times)

.align	5
SYM_FUNC_START(x25519_fe51_frombytes)

	li	12, -1
	srdi	12, 12, 13	# 0x7ffffffffffff

	ld	5, 0(4)
	ld	6, 8(4)
	ld	7, 16(4)
	ld	8, 24(4)

	srdi	10, 5, 51
	and	5, 5, 12	# h0

	sldi	11, 6, 13
	or	11, 10, 11	# h1t
	srdi	10, 6, 38
	and	6, 11, 12	# h1

	sldi	11, 7, 26
	or	10, 10, 11	# h2t

	srdi	11, 7, 25
	and	7, 10, 12	# h2
	sldi	10, 8, 39
	or	11, 11, 10	# h3t

	srdi	9, 8, 12
	and	8, 11, 12	# h3
	and	9, 9, 12	# h4

	std	5, 0(3)
	std	6, 8(3)
	std	7, 16(3)
	std	8, 24(3)
	std	9, 32(3)

	blr
SYM_FUNC_END(x25519_fe51_frombytes)

.align	5
SYM_FUNC_START(x25519_fe51_tobytes)

	ld	5, 0(4)
	ld	6, 8(4)
	ld	7, 16(4)
	ld	8, 24(4)
	ld	9, 32(4)

	li	12, -1
	srdi	12, 12, 13	# 0x7ffffffffffff

	# Full reducuction
	addi	10, 5, 19
	srdi	10, 10, 51
	add	10, 10, 6
	srdi	10, 10, 51
	add	10, 10, 7
	srdi	10, 10, 51
	add	10, 10, 8
	srdi	10, 10, 51
	add	10, 10, 9
	srdi	10, 10, 51

	mulli	10, 10, 19
	add	5, 5, 10
	srdi	11, 5, 51
	add	6, 6, 11
	srdi	11, 6, 51
	add	7, 7, 11
	srdi	11, 7, 51
	add	8, 8, 11
	srdi	11, 8, 51
	add	9, 9, 11

	and	5, 5, 12
	and	6, 6, 12
	and	7, 7, 12
	and	8, 8, 12
	and	9, 9, 12

	sldi	10, 6, 51
	or	5, 5, 10	# s0

	srdi	11, 6, 13
	sldi	10, 7, 38
	or	6, 11, 10	# s1

	srdi	11, 7, 26
	sldi	10, 8, 25
	or	7, 11, 10	# s2

	srdi	11, 8, 39
	sldi	10, 9, 12
	or	8, 11, 10	# s4

	std	5, 0(3)
	std	6, 8(3)
	std	7, 16(3)
	std	8, 24(3)

	blr
SYM_FUNC_END(x25519_fe51_tobytes)

.align	5
SYM_FUNC_START(x25519_cswap)

	li	7, 5
	neg	6, 5
	mtctr	7

.Lswap_loop:
	ld	8, 0(3)
	ld	9, 0(4)
	xor	10, 8, 9
	and	10, 10, 6
	xor	11, 8, 10
	xor	12, 9, 10
	std	11, 0(3)
	addi	3, 3, 8
	std	12, 0(4)
	addi	4, 4, 8
	bdnz	.Lswap_loop

	blr
SYM_FUNC_END(x25519_cswap)
