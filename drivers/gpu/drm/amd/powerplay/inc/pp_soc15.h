/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
#ifndef PP_SOC15_H
#define PP_SOC15_H

#include "vega10/soc15ip.h"

inline static uint32_t soc15_get_register_offset(
		uint32_t hw_id,
		uint32_t inst,
		uint32_t segment,
		uint32_t offset)
{
	uint32_t reg = 0;

	if (hw_id == THM_HWID)
		reg = THM_BASE.instance[inst].segment[segment] + offset;
	else if (hw_id == NBIF_HWID)
		reg = NBIF_BASE.instance[inst].segment[segment] + offset;
	else if (hw_id == MP1_HWID)
		reg = MP1_BASE.instance[inst].segment[segment] + offset;
	else if (hw_id == DF_HWID)
		reg = DF_BASE.instance[inst].segment[segment] + offset;

	return reg;
}

#endif
