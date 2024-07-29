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

#ifndef _DMUB_REG_H_
#define _DMUB_REG_H_

#include "../inc/dmub_cmd.h"

struct dmub_srv;

/* Register offset and field lookup. */

#define BASE(seg) BASE_INNER(seg)

#define REG_OFFSET(reg_name) (BASE(mm##reg_name##_BASE_IDX) + mm##reg_name)

#define FD_SHIFT(reg_name, field) reg_name##__##field##__SHIFT

#define FD_MASK(reg_name, field) reg_name##__##field##_MASK

#define REG(reg) (REGS)->offset.reg

#define FD(reg_field) (REGS)->shift.reg_field, (REGS)->mask.reg_field

#define FN(reg_name, field) FD(reg_name##__##field)

/* Register reads and writes. */

#define REG_READ(reg) ((CTX)->funcs.reg_read((CTX)->user_ctx, REG(reg)))

#define REG_WRITE(reg, val) \
	((CTX)->funcs.reg_write((CTX)->user_ctx, REG(reg), (val)))

/* Register field setting. */

#define REG_SET_N(reg_name, n, initial_val, ...) \
	dmub_reg_set(CTX, REG(reg_name), initial_val, n, __VA_ARGS__)

#define REG_SET(reg_name, initial_val, field, val) \
		REG_SET_N(reg_name, 1, initial_val, \
				FN(reg_name, field), val)

#define REG_SET_2(reg, init_value, f1, v1, f2, v2) \
		REG_SET_N(reg, 2, init_value, \
				FN(reg, f1), v1, \
				FN(reg, f2), v2)

#define REG_SET_3(reg, init_value, f1, v1, f2, v2, f3, v3) \
		REG_SET_N(reg, 3, init_value, \
				FN(reg, f1), v1, \
				FN(reg, f2), v2, \
				FN(reg, f3), v3)

#define REG_SET_4(reg, init_value, f1, v1, f2, v2, f3, v3, f4, v4) \
		REG_SET_N(reg, 4, init_value, \
				FN(reg, f1), v1, \
				FN(reg, f2), v2, \
				FN(reg, f3), v3, \
				FN(reg, f4), v4)

/* Register field updating. */

#define REG_UPDATE_N(reg_name, n, ...)\
		dmub_reg_update(CTX, REG(reg_name), n, __VA_ARGS__)

#define REG_UPDATE(reg_name, field, val)	\
		REG_UPDATE_N(reg_name, 1, \
				FN(reg_name, field), val)

#define REG_UPDATE_2(reg, f1, v1, f2, v2)	\
		REG_UPDATE_N(reg, 2,\
				FN(reg, f1), v1,\
				FN(reg, f2), v2)

#define REG_UPDATE_3(reg, f1, v1, f2, v2, f3, v3) \
		REG_UPDATE_N(reg, 3, \
				FN(reg, f1), v1, \
				FN(reg, f2), v2, \
				FN(reg, f3), v3)

#define REG_UPDATE_4(reg, f1, v1, f2, v2, f3, v3, f4, v4) \
		REG_UPDATE_N(reg, 4, \
				FN(reg, f1), v1, \
				FN(reg, f2), v2, \
				FN(reg, f3), v3, \
				FN(reg, f4), v4)

/* Register field getting. */
#define REG_GET(reg_name, field, val) \
	dmub_reg_get(CTX, REG(reg_name), FN(reg_name, field), val)

void dmub_reg_set(struct dmub_srv *srv, uint32_t addr, uint32_t reg_val, int n,
		  uint8_t shift1, uint32_t mask1, uint32_t field_value1, ...);

void dmub_reg_update(struct dmub_srv *srv, uint32_t addr, int n, uint8_t shift1,
		     uint32_t mask1, uint32_t field_value1, ...);

void dmub_reg_get(struct dmub_srv *srv, uint32_t addr, uint8_t shift,
		  uint32_t mask, uint32_t *field_value);

#endif /* _DMUB_REG_H_ */
