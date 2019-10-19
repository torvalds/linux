/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
 */
#ifndef __COMMON_BOCO_H__
#define __COMMON_BOCO_H__
#include "hwmgr.h"


enum baco_cmd_type {
	CMD_WRITE = 0,
	CMD_READMODIFYWRITE,
	CMD_WAITFOR,
	CMD_DELAY_MS,
	CMD_DELAY_US,
};

struct soc15_baco_cmd_entry {
	enum baco_cmd_type cmd;
	uint32_t 	hwip;
	uint32_t 	inst;
	uint32_t 	seg;
	uint32_t 	reg_offset;
	uint32_t     	mask;
	uint32_t     	shift;
	uint32_t     	timeout;
	uint32_t     	val;
};
extern bool soc15_baco_program_registers(struct pp_hwmgr *hwmgr,
					const struct soc15_baco_cmd_entry *entry,
					const u32 array_size);
#endif
