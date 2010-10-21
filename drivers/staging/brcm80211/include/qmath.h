/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __QMATH_H__
#define __QMATH_H__

s16 qm_sat32(s32 op);

s32 qm_mul321616(s16 op1, s16 op2);

s16 qm_mul16(s16 op1, s16 op2);

s32 qm_muls321616(s16 op1, s16 op2);

u16 qm_mulu16(u16 op1, u16 op2);

s16 qm_muls16(s16 op1, s16 op2);

s32 qm_add32(s32 op1, s32 op2);

s16 qm_add16(s16 op1, s16 op2);

s16 qm_sub16(s16 op1, s16 op2);

s32 qm_sub32(s32 op1, s32 op2);

s32 qm_mac321616(s32 acc, s16 op1, s16 op2);

s32 qm_shl32(s32 op, int shift);

s32 qm_shr32(s32 op, int shift);

s16 qm_shl16(s16 op, int shift);

s16 qm_shr16(s16 op, int shift);

s16 qm_norm16(s16 op);

s16 qm_norm32(s32 op);

s16 qm_div_s(s16 num, s16 denom);

s16 qm_abs16(s16 op);

s16 qm_div16(s16 num, s16 denom, s16 *qQuotient);

s32 qm_abs32(s32 op);

s16 qm_div163232(s32 num, s32 denom, s16 *qquotient);

s32 qm_mul323216(s32 op1, s16 op2);

s32 qm_mulsu321616(s16 op1, u16 op2);

s32 qm_muls323216(s32 op1, s16 op2);

s32 qm_mul32(s32 a, s32 b);

s32 qm_muls32(s32 a, s32 b);

void qm_log10(s32 N, s16 qN, s16 *log10N, s16 *qLog10N);

void qm_1byN(s32 N, s16 qN, s32 *result, s16 *qResult);

#endif				/* #ifndef __QMATH_H__ */
