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

#define ANT_SWCTRL_TBL_REV3_IDX (0)

typedef phytbl_info_t mimophytbl_info_t;

extern const mimophytbl_info_t mimophytbl_info_rev0[],
    mimophytbl_info_rev0_volatile[];
extern const u32 mimophytbl_info_sz_rev0, mimophytbl_info_sz_rev0_volatile;

extern const mimophytbl_info_t mimophytbl_info_rev3[],
    mimophytbl_info_rev3_volatile[], mimophytbl_info_rev3_volatile1[],
    mimophytbl_info_rev3_volatile2[], mimophytbl_info_rev3_volatile3[];
extern const u32 mimophytbl_info_sz_rev3, mimophytbl_info_sz_rev3_volatile,
    mimophytbl_info_sz_rev3_volatile1, mimophytbl_info_sz_rev3_volatile2,
    mimophytbl_info_sz_rev3_volatile3;

extern const u32 noise_var_tbl_rev3[];

extern const mimophytbl_info_t mimophytbl_info_rev7[];
extern const u32 mimophytbl_info_sz_rev7;
extern const u32 noise_var_tbl_rev7[];

extern const mimophytbl_info_t mimophytbl_info_rev16[];
extern const u32 mimophytbl_info_sz_rev16;
