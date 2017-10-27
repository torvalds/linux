/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __ISP_OP_COUNT_H_INCLUDED__
#define __ISP_OP_COUNT_H_INCLUDED__

#include <stdio.h>

typedef struct {
	long long bbb_cnt;   /* number of bbb      */
	int bbb_op;    /* operations per bbb */
	long long total_cnt; /* bbb_cnt * bbb_op   */
} bbb_stat_t;

typedef enum {
	bbb_func_OP_1w_and,
	bbb_func_OP_1w_or,
	bbb_func_OP_1w_xor,
	bbb_func_OP_1w_inv,
	bbb_func_OP_1w_add,
	bbb_func_OP_1w_sub,
	bbb_func_OP_1w_addsat,
	bbb_func_OP_1w_subsat,
	bbb_func_OP_1w_subasr1,
	bbb_func_OP_1w_subhalf,
	bbb_func_OP_1w_subhalfrnd,
	bbb_func_OP_1w_abs,
	bbb_func_OP_1w_subabssat,
#ifdef ISP2401
	bbb_func_OP_1w_subsat_u,
#endif
	bbb_func_OP_1w_muld,
	bbb_func_OP_1w_mul,
	bbb_func_OP_1w_qmul,
	bbb_func_OP_1w_qrmul,
	bbb_func_OP_1w_eq,
	bbb_func_OP_1w_ne,
	bbb_func_OP_1w_le,
	bbb_func_OP_1w_lt,
	bbb_func_OP_1w_ge,
	bbb_func_OP_1w_gt,
	bbb_func_OP_1w_asr,
	bbb_func_OP_1w_asrrnd,
	bbb_func_OP_1w_asl,
	bbb_func_OP_1w_aslsat,
	bbb_func_OP_1w_lsl,
	bbb_func_OP_1w_lsr,
#ifdef ISP2401
	bbb_func_OP_1w_ashift,
	bbb_func_OP_1w_lshift,
#endif
	bbb_func_OP_int_cast_to_1w ,
	bbb_func_OP_1w_cast_to_int ,
	bbb_func_OP_1w_cast_to_2w ,
	bbb_func_OP_2w_cast_to_1w ,
	bbb_func_OP_2w_sat_cast_to_1w ,
	bbb_func_OP_1w_clip_asym,
	bbb_func_OP_1w_clipz,
	bbb_func_OP_1w_div,
	bbb_func_OP_1w_qdiv,
	bbb_func_OP_1w_mod,
	bbb_func_OP_1w_sqrt_u,
	bbb_func_OP_1w_mux,
	bbb_func_OP_1w_avg,
	bbb_func_OP_1w_avgrnd,
	bbb_func_OP_1w_min,
	bbb_func_OP_1w_max,
	bbb_func_OP_2w_and,
	bbb_func_OP_2w_or,
	bbb_func_OP_2w_xor,
	bbb_func_OP_2w_inv,
	bbb_func_OP_2w_add,
	bbb_func_OP_2w_sub,
	bbb_func_OP_2w_addsat,
	bbb_func_OP_2w_subsat,
	bbb_func_OP_2w_subasr1,
	bbb_func_OP_2w_subhalf,
	bbb_func_OP_2w_subhalfrnd,
	bbb_func_OP_2w_abs,
	bbb_func_OP_2w_subabssat,
	bbb_func_OP_2w_mul,
	bbb_func_OP_2w_qmul,
	bbb_func_OP_2w_qrmul,
	bbb_func_OP_2w_eq,
	bbb_func_OP_2w_ne,
	bbb_func_OP_2w_le,
	bbb_func_OP_2w_lt,
	bbb_func_OP_2w_ge,
	bbb_func_OP_2w_gt,
	bbb_func_OP_2w_asr,
	bbb_func_OP_2w_asrrnd,
	bbb_func_OP_2w_asl,
	bbb_func_OP_2w_aslsat,
	bbb_func_OP_2w_lsl,
	bbb_func_OP_2w_lsr,
	bbb_func_OP_2w_clip_asym,
	bbb_func_OP_2w_clipz,
	bbb_func_OP_2w_div,
	bbb_func_OP_2w_divh,
	bbb_func_OP_2w_mod,
	bbb_func_OP_2w_sqrt_u,
	bbb_func_OP_2w_mux,
	bbb_func_OP_2w_avg,
	bbb_func_OP_2w_avgrnd,
	bbb_func_OP_2w_min,
	bbb_func_OP_2w_max,
	bbb_func_OP_1w_mul_realigning,
#ifdef ISP2401
	bbb_func_OP_1w_imax32,
	bbb_func_OP_1w_imaxidx32,
	bbb_func_OP_1w_cond_add,
#endif

	bbb_func_num_functions
} bbb_functions_t;

typedef enum {
	core_func_OP_and,
	core_func_OP_or,
	core_func_OP_xor,
	core_func_OP_inv,
	core_func_OP_add,
	core_func_OP_sub,
	core_func_OP_addsat,
	core_func_OP_subsat,
	core_func_OP_subasr1,
	core_func_OP_abs,
	core_func_OP_subabssat,
#ifdef ISP2401
	core_func_OP_subsat_u,
#endif
	core_func_OP_muld,
	core_func_OP_mul,
	core_func_OP_qrmul,
	core_func_OP_eq,
	core_func_OP_ne,
	core_func_OP_le,
	core_func_OP_lt,
	core_func_OP_ge,
	core_func_OP_gt,
	core_func_OP_asr,
	core_func_OP_asl,
	core_func_OP_asrrnd,
	core_func_OP_lsl,
	core_func_OP_lslsat,
	core_func_OP_lsr,
	core_func_OP_lsrrnd,
	core_func_OP_clip_asym,
	core_func_OP_clipz,
	core_func_OP_div,
	core_func_OP_mod,
	core_func_OP_sqrt,
	core_func_OP_mux,
	core_func_OP_avgrnd,
	core_func_OP_min,
	core_func_OP_max,

	core_func_num_functions

} core_functions_t;

/* inc_bbb_count() can be used for building blocks that are implemented with one operation
   inc_bbb_count_ext() will be used in case the operation count is not known or greater than one.

   For some operations there is a difference in operation count for the cloned version and the
   not cloned version. this difference is not vissible on the reference code side.
   We could add a min and max operation count for those operations, and keep track of those counts
   separately. That way in the report the impact can be seen. */

#ifdef DISABLE_OPCNT
#define inc_bbb_count(func)
#define inc_bbb_count_ext(func, cnt)
#define enable_bbb_count()
#define disable_bbb_count()
#else
#define inc_bbb_count(func) _inc_bbb_count(func)
#define inc_bbb_count_ext(func, cnt) _inc_bbb_count_ext(func, cnt)
#define enable_bbb_count() _enable_bbb_count()
#define disable_bbb_count() _disable_bbb_count()
#endif

void
inc_core_count_n(
	core_functions_t func,
	unsigned n);

void
_enable_bbb_count(void);

void
_disable_bbb_count(void);

void
_inc_bbb_count(
	bbb_functions_t func);

void
_inc_bbb_count_ext(
	bbb_functions_t func,
	int op_count);

void
bbb_func_reset_count(void);

void
bbb_func_print_totals(
	FILE  * fp,
	unsigned non_zero_only);

void
core_func_print_totals(
	FILE* fp,
	unsigned non_zero_only);

#endif
