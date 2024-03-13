/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
 *
 * Authors: AMD
 *
 */
#ifndef _DMUB_TRACE_BUFFER_H_
#define _DMUB_TRACE_BUFFER_H_

#include "dmub_cmd.h"

#define LOAD_DMCU_FW	1
#define LOAD_PHY_FW	2


enum dmucb_trace_code {
	DMCUB__UNKNOWN,
	DMCUB__MAIN_BEGIN,
	DMCUB__PHY_INIT_BEGIN,
	DMCUB__PHY_FW_SRAM_LOAD_BEGIN,
	DMCUB__PHY_FW_SRAM_LOAD_END,
	DMCUB__PHY_INIT_POLL_DONE,
	DMCUB__PHY_INIT_END,
	DMCUB__DMCU_ERAM_LOAD_BEGIN,
	DMCUB__DMCU_ERAM_LOAD_END,
	DMCUB__DMCU_ISR_LOAD_BEGIN,
	DMCUB__DMCU_ISR_LOAD_END,
	DMCUB__MAIN_IDLE,
	DMCUB__PERF_TRACE,
	DMCUB__PG_DONE,
};

struct dmcub_trace_buf_entry {
	enum dmucb_trace_code trace_code;
	uint32_t tick_count;
	uint32_t param0;
	uint32_t param1;
};

#define TRACE_BUF_SIZE (1024) //1 kB
#define PERF_TRACE_MAX_ENTRY ((TRACE_BUF_SIZE - 8)/sizeof(struct dmcub_trace_buf_entry))


struct dmcub_trace_buf {
	uint32_t entry_count;
	uint32_t clk_freq;
	struct dmcub_trace_buf_entry entries[PERF_TRACE_MAX_ENTRY];
};

#endif /* _DMUB_TRACE_BUFFER_H_ */
