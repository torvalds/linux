/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 * Interface Functions related to the BIOS PowerPlay Tables.
 *
 */

#ifndef PROCESSPPTABLES_H
#define PROCESSPPTABLES_H

struct pp_hwmgr;
struct pp_power_state;
struct pp_hw_power_state;

extern const struct pp_table_func pptable_funcs;

typedef int (*pp_tables_hw_clock_info_callback)(struct pp_hwmgr *hwmgr,
						struct pp_hw_power_state *hw_ps,
						unsigned int index,
						const void *clock_info);

int pp_tables_get_num_of_entries(struct pp_hwmgr *hwmgr,
				 unsigned long *num_of_entries);

int pp_tables_get_entry(struct pp_hwmgr *hwmgr,
			unsigned long entry_index,
			struct pp_power_state *ps,
			pp_tables_hw_clock_info_callback func);

int pp_tables_get_response_times(struct pp_hwmgr *hwmgr,
				 uint32_t *vol_rep_time, uint32_t *bb_rep_time);

#endif
