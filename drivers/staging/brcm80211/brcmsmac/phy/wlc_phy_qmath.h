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

u16 qm_mulu16(u16 op1, u16 op2);

s16 qm_muls16(s16 op1, s16 op2);

s32 qm_add32(s32 op1, s32 op2);

s16 qm_add16(s16 op1, s16 op2);

s16 qm_sub16(s16 op1, s16 op2);

s32 qm_shl32(s32 op, int shift);

s16 qm_shl16(s16 op, int shift);

s16 qm_shr16(s16 op, int shift);

s16 qm_norm32(s32 op);

void qm_log10(s32 N, s16 qN, s16 *log10N, s16 *qLog10N);

#endif				/* #ifndef __QMATH_H__ */
